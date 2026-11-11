#include "Combat/LDCombatDirector.h"
#include "Core/LDLog.h"
#include "Combat/LDCombatSourceRules.h"
#include "Navigation/LDOrderRules.h"
#include "Combat/LDTacticalRules.h"
#include "Combat/LDControlRules.h"
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Region/LDMapRegistry.h"
#include "Vision/LDVisionDirector.h"
#include "Engine/World.h"

namespace LDCA
{
    constexpr int32 MaxRecentReceipts=32;
    constexpr int32 MaxPendingDirect=8;
    // A dash with no legal first segment is refused before any cost is paid.
    constexpr double DashProbeCm=20.;
    // CONTENT_AND_BALANCE monster Attack row: single-target melee Physical at 1.0 AD. The monster
    // catalog row carries the AttackP2 value only, so the approved ratio is fixed here.
    constexpr int64 MonsterBasicADRatioBp=10000;

    double ToDouble(const FLDDemoRational& Value)
    { return Value.Denominator>0 ? double(Value.Numerator)/double(Value.Denominator) : 0.; }

    bool IsContenderEntry(ELDCombatEntry Entry)
    {
        return Entry==ELDCombatEntry::Q || Entry==ELDCombatEntry::W || Entry==ELDCombatEntry::E || Entry==ELDCombatEntry::Dash;
    }

    bool SameTerms(const FLDCombatActionRequest& A, const FLDCombatActionRequest& B)
    {
        return A.Generation==B.Generation && A.Life==B.Life && A.DirectSequence==B.DirectSequence && A.Entry==B.Entry
            && A.Target.Kind==B.Target.Kind && A.Target.Observed==B.Target.Observed
            && A.Target.ObservationEpoch==B.Target.ObservationEpoch
            && FVector(A.Target.Direction).Equals(FVector(B.Target.Direction),0.0001)
            && FVector(A.Target.Point).Equals(FVector(B.Target.Point),0.0001)
            && A.Target.ServerBody==B.Target.ServerBody && A.ExpectedSourceRevision==B.ExpectedSourceRevision;
    }

    bool ValidDirection(const FVector& Direction)
    {
        return !Direction.ContainsNaN() && FMath::Abs(Direction.Z)<=0.01
            && FMath::IsNearlyEqual(Direction.Size2D(),1.,0.02);
    }

    const FLDDemoEffectDefinition* FindEffect(const FLDDemoAbilityValues& Values, ELDDemoEffectOp Op)
    { return Values.Effects.FindByPredicate([Op](const FLDDemoEffectDefinition& Effect) { return Effect.GetOp()==Op; }); }

    bool ValidPoint(const FVector& Point)
    { return !Point.ContainsNaN() && FMath::Abs(Point.X)<=1.e6 && FMath::Abs(Point.Y)<=1.e6 && FMath::Abs(Point.Z)<=1.e6; }

    // The authority facing turns to what an accepted action aims at; the rear-arc judgement reads this facing.
    void FaceToward(ALDCharacter& Actor, const FVector& Point)
    {
        const FVector Foot=ALDCombatDirector::FootOf(Actor);
        const FVector Planar(Point.X-Foot.X,Point.Y-Foot.Y,0);
        if (!Planar.ContainsNaN() && Planar.SizeSquared()>=1.) { Actor.SetFacing(Planar.Rotation().Yaw); }
    }

    void FaceAlong(ALDCharacter& Actor, const FVector& Direction)
    { if (!Direction.ContainsNaN() && !Direction.IsNearlyZero()) { Actor.SetFacing(Direction.Rotation().Yaw); } }
}

FLDCombatEntityRecord* ALDCombatDirector::FindRecord(const FGuid& Entity) { return Records.Find(Entity); }
const FLDCombatEntityRecord* ALDCombatDirector::FindRecord(const FGuid& Entity) const { return Records.Find(Entity); }

FLDBodyCombatState* ALDCombatDirector::FindBody(const FLDCombatBodyRef& Ref, ALDCharacter** OutActor)
{
    if (OutActor) { *OutActor=nullptr; }
    for (auto& Pair : Bodies)
    {
        if (Pair.Key.IsValid() && Pair.Value.Victim==Ref.Victim && Pair.Value.Identity==Ref.BodyIdentity)
        {
            if (OutActor) { *OutActor=Pair.Key.Get(); }
            return &Pair.Value;
        }
    }
    return nullptr;
}

const FLDBodyCombatState* ALDCombatDirector::FindBody(const FLDCombatBodyRef& Ref, ALDCharacter** OutActor) const
{ return const_cast<ALDCombatDirector*>(this)->FindBody(Ref,OutActor); }

FLDBodyCombatState* ALDCombatDirector::FindEntityBody(const FGuid& Entity, ALDCharacter** OutActor)
{
    if (OutActor) { *OutActor=nullptr; }
    for (auto& Pair : Bodies)
    {
        if (Pair.Key.IsValid() && Pair.Value.Victim.EntityId==Entity)
        {
            if (OutActor) { *OutActor=Pair.Key.Get(); }
            return &Pair.Value;
        }
    }
    return nullptr;
}

const FLDBodyCombatState* ALDCombatDirector::FindEntityBody(const FGuid& Entity, ALDCharacter** OutActor) const
{ return const_cast<ALDCombatDirector*>(this)->FindEntityBody(Entity,OutActor); }

bool ALDCombatDirector::ResolveControllerCaller(const AController& Controller, FLDCombatCaller& Out, FString& Error) const
{
    Out={};
    auto* Mode=Core();
    auto* PC=Cast<ALDPlayerController>(const_cast<AController*>(&Controller));
    const FGuid Contender=Mode && PC ? Mode->ContenderId(PC) : FGuid();
    if (!Contender.IsValid() || !Mode->IsMemberPresent(Contender) || !Records.Contains(Contender))
    {
        Error=TEXT("NotOwner: the controller is not a present contender of the current match.");
        return false;
    }
    Out={Mode->GetMatchContext(),ELDVictimKind::Contender,Contender,PC};
    Error.Reset();
    return true;
}

void ALDCombatDirector::StoreReceipt(FLDCombatEntityRecord& Record, const FLDCombatActionRequest& Request, FLDCombatActionReceipt Receipt)
{
    if (Record.ReceiptRevision==MAX_uint64) { return; }
    Receipt.ReceiptRevision=++Record.ReceiptRevision;
    if (auto* Existing=Record.Recent.FindByPredicate([&Request](const FLDCombatDirectEntry& Entry)
        { return Entry.Request.DirectSequence==Request.DirectSequence; }))
    {
        // #198 §10: a request is refused once, when its receipt first turns Rejected.
        if (Existing->Receipt.Phase!=ELDCombatPhase::Rejected && Receipt.Phase==ELDCombatPhase::Rejected)
        { LedgerActionRefused(Record.Entity,Receipt.Reject); }
        Existing->Receipt=Receipt;
        return;
    }
    if (Receipt.Phase==ELDCombatPhase::Rejected) { LedgerActionRefused(Record.Entity,Receipt.Reject); }
    Record.Recent.Add({Request,Receipt});
    if (Record.Recent.Num()>LDCA::MaxRecentReceipts) { Record.Recent.RemoveAt(0,Record.Recent.Num()-LDCA::MaxRecentReceipts); }
}

void ALDCombatDirector::SendReceipt(const FLDCombatEntityRecord& Record, const FLDCombatActionReceipt& Receipt) const
{
    if (Record.Kind!=ELDVictimKind::Contender) { return; }
    const auto* Mode=Core();
    if (auto* PC=Mode?Mode->FindContenderController(Record.Entity):nullptr) { PC->ClientCombatReceipt(Receipt); }
}

void ALDCombatDirector::FinishAction(FLDBodyCombatState& Body, ELDCombatReject Reason, bool bReleased)
{
    if (!Body.Action.IsSet()) { return; }
    UE_LOG(LogLDCombat,Verbose,TEXT("LD_COMBAT_ACTION_END token=%llu released=%d reason=%u"),Body.Action->Token,bReleased,uint8(Reason));
    Body.Action.Reset();
    ++Body.StateRevision;
    MarkViewChanged(Body.Victim.EntityId);
}

void ALDCombatDirector::AbandonAction(FLDBodyCombatState& Body, ELDCombatReject Reason)
{
    if (!Body.Action.IsSet()) { return; }
    const FLDCombatAction Action=Body.Action.GetValue();
    FinishAction(Body,Reason,false);
    auto* Record=Action.Key.Origin==ELDCombatRequestOrigin::Direct ? FindRecord(Action.Key.Entity) : nullptr;
    const auto* Known=Record ? Record->Recent.FindByPredicate([&Action](const FLDCombatDirectEntry& Entry)
        { return Entry.Request.DirectSequence==Action.Key.DirectSequence; }) : nullptr;
    // An expired window entry has nobody left to read it; the Nav origin reports through its order.
    if (!Known) { return; }
    FLDCombatActionReceipt Receipt=Known->Receipt;
    Receipt.CombatToken=Action.Token;
    Receipt.Phase=ELDCombatPhase::Rejected;
    Receipt.Reject=Reason;
    // The entry exists, so StoreReceipt updates it in place and Known stays valid.
    StoreReceipt(*Record,Known->Request,Receipt);
    SendReceipt(*Record,Known->Receipt);
}

void ALDCombatDirector::EndMatchActions()
{
    Channels.Reset();
    GroundEffects.Reset();
    ContactMotion.Reset();
    for (auto& Pair : Records)
    {
        auto& Record=Pair.Value;
        const TArray<FLDCombatActionRequest> Pending=MoveTemp(Record.Pending);
        Record.Pending.Reset();
        for (const auto& Request : Pending)
        {
            FLDCombatActionReceipt Closed;
            Closed.Generation=Request.Generation;
            Closed.Life=Request.Life;
            Closed.DirectSequence=Request.DirectSequence;
            Closed.Entry=Request.Entry;
            Closed.Phase=ELDCombatPhase::Rejected;
            Closed.Reject=ELDCombatReject::MatchClosed;
            StoreReceipt(Record,Request,Closed);
            if (const auto* Stored=Record.Recent.FindByPredicate([&Request](const FLDCombatDirectEntry& Entry)
                { return Entry.Request.DirectSequence==Request.DirectSequence; }))
            { SendReceipt(Record,Stored->Receipt); }
        }
    }
    for (auto& Pair : Bodies)
    {
        AbandonAction(Pair.Value,ELDCombatReject::MatchClosed);
        Pair.Value.ChannelEffectId.Invalidate();
        Pair.Value.ChannelDirectSequence=0;
        Pair.Value.DashContact.Reset();
    }
}

FLDCombatActionReceipt ALDCombatDirector::SubmitAction(const FLDCombatCaller& Caller, const FLDCombatActionRequest& Request,
    bool bQueuedStep)
{
    FLDCombatActionReceipt Receipt;
    Receipt.Generation=Request.Generation;
    Receipt.Life=Request.Life;
    Receipt.Origin=ELDCombatRequestOrigin::Direct;
    Receipt.DirectSequence=Request.DirectSequence;
    Receipt.Entry=Request.Entry;
    const auto Refuse=[&Receipt](ELDCombatReject Reason) { Receipt.Phase=ELDCombatPhase::Rejected; Receipt.Reject=Reason; return Receipt; };
    auto* Mode=Core();
    if (!Mode || !Catalog()) { return Refuse(ELDCombatReject::AuthorityDataUnavailable); }
    if (Caller.Match!=Mode->GetMatchContext() || Request.Generation!=Caller.Match.Generation) { return Refuse(ELDCombatReject::StaleMatch); }
    if (!Mode->IsGameplayWriteOpen(Caller.Match)) { return Refuse(ELDCombatReject::MatchClosed); }
    auto* Record=FindRecord(Caller.EntityId);
    if (!Record || Record->Kind!=Caller.Kind) { return Refuse(ELDCombatReject::NotOwner); }
    const bool Camp=Caller.Kind==ELDVictimKind::CampMember;
    bool Shape=Request.DirectSequence!=0 && Request.Life!=0 && (Camp ? Request.Entry==ELDCombatEntry::Basic : LDCA::IsContenderEntry(Request.Entry));
    switch (Request.Target.Kind)
    {
    case ELDCombatTargetKind::Missing:
    case ELDCombatTargetKind::Self:
        Shape&=!Camp && !Request.Target.Observed.IsValid() && !Request.Target.ObservationEpoch && !Request.Target.ServerBody.IsValid();
        break;
    case ELDCombatTargetKind::Unit:
        Shape&=Camp ? Request.Target.ServerBody.IsValid() && !Request.Target.Observed.IsValid()
            : Request.Target.Observed.IsValid() && Request.Target.ObservationEpoch!=0 && !Request.Target.ServerBody.IsValid();
        break;
    case ELDCombatTargetKind::Direction:
        Shape&=!Camp && LDCA::ValidDirection(Request.Target.Direction) && !Request.Target.ServerBody.IsValid();
        break;
    case ELDCombatTargetKind::Point:
        Shape&=!Camp && LDCA::ValidPoint(Request.Target.Point) && !Request.Target.Observed.IsValid()
            && !Request.Target.ObservationEpoch && !Request.Target.ServerBody.IsValid();
        break;
    default:
        Shape=false;
    }
    if (!Shape) { return Refuse(ELDCombatReject::InvalidRequest); }
    const auto* Known=Record->Recent.FindByPredicate([&Request](const FLDCombatDirectEntry& Entry)
        { return Entry.Request.DirectSequence==Request.DirectSequence; });
    switch (LDCombatSourceRules::DirectSequenceVerdict(Known!=nullptr,Known && LDCA::SameTerms(Known->Request,Request),
        Request.DirectSequence,Record->DirectHighWater,bQueuedStep))
    {
    case ELDDirectSequenceVerdict::Replay: return Known->Receipt;
    case ELDDirectSequenceVerdict::Conflict: return Refuse(ELDCombatReject::RequestConflict);
    case ELDDirectSequenceVerdict::Expired: return Refuse(ELDCombatReject::ReceiptExpired);
    default: break;
    }
    Record->DirectHighWater=FMath::Max(Record->DirectHighWater,Request.DirectSequence);
    // #153: before T=0 only the D dash is admitted; basic attacks and Q/W are refused. The refusal is kept, so a replay
    // after T=0 cannot start it late.
    // #198: the rest return countdown locks every input; the refusal is kept, so a replay never starts late.
    if (Mode->IsInputLocked(Caller.Match,Caller.EntityId))
    {
        Refuse(ELDCombatReject::NotStarted);
        StoreReceipt(*Record,Request,Receipt);
        return Receipt;
    }
    if (Request.DirectSequence<=Record->CancelledThrough)
    {
        Refuse(ELDCombatReject::Cancelled);
        StoreReceipt(*Record,Request,Receipt);
        return Receipt;
    }
    const auto* Body=FindEntityBody(Caller.EntityId);
    if (!Body || Body->bPendingDeath || Body->Victim.Life!=Request.Life)
    {
        Refuse(!Body || Body->bPendingDeath ? ELDCombatReject::Dead : ELDCombatReject::StaleLife);
        StoreReceipt(*Record,Request,Receipt);
        return Receipt;
    }
    if (Record->Pending.Num()>=LDCA::MaxPendingDirect)
    {
        Refuse(ELDCombatReject::CapacityUnavailable);
        StoreReceipt(*Record,Request,Receipt);
        return Receipt;
    }
    Receipt.Phase=ELDCombatPhase::Queued;
    Receipt.Reject=ELDCombatReject::None;
    Record->Pending.Add(Request);
    StoreReceipt(*Record,Request,Receipt);
    return Record->Recent.Last().Receipt;
}

FLDCombatActionReceipt ALDCombatDirector::CancelAction(const FLDCombatCaller& Caller, const FLDCombatCancelRequest& Request)
{
    FLDCombatActionReceipt Receipt;
    Receipt.Generation=Request.Generation;
    Receipt.Life=Request.Life;
    Receipt.DirectSequence=Request.ThroughDirectSequence;
    Receipt.Phase=ELDCombatPhase::Rejected;
    auto* Mode=Core();
    auto* Record=FindRecord(Caller.EntityId);
    if (!Mode || Caller.Match!=Mode->GetMatchContext() || Request.Generation!=Caller.Match.Generation)
    { Receipt.Reject=ELDCombatReject::StaleMatch; return Receipt; }
    if (!Record || Record->Kind!=Caller.Kind || !Request.ThroughDirectSequence)
    { Receipt.Reject=!Record ? ELDCombatReject::NotOwner : ELDCombatReject::InvalidRequest; return Receipt; }
    auto* CurrentBody=FindEntityBody(Caller.EntityId);
    if (!CurrentBody || CurrentBody->Victim.Life!=Request.Life)
    { Receipt.Reject=ELDCombatReject::StaleLife; return Receipt; }
    Record->CancelledThrough=FMath::Max(Record->CancelledThrough,Request.ThroughDirectSequence);
    for (int32 Index=Record->Pending.Num()-1; Index>=0; --Index)
    {
        const auto Pending=Record->Pending[Index];
        if (Pending.DirectSequence>Record->CancelledThrough) { continue; }
        Record->Pending.RemoveAt(Index);
        FLDCombatActionReceipt Cancelled;
        Cancelled.Generation=Pending.Generation;
        Cancelled.Life=Pending.Life;
        Cancelled.DirectSequence=Pending.DirectSequence;
        Cancelled.Entry=Pending.Entry;
        Cancelled.Phase=ELDCombatPhase::Rejected;
        Cancelled.Reject=ELDCombatReject::Cancelled;
        StoreReceipt(*Record,Pending,Cancelled);
        SendReceipt(*Record,Record->Recent.Last().Receipt);
    }
    // Interruptible work only: an unreleased Direct windup at or below the stop bound ends without cost.
    if (auto* Body=FindEntityBody(Caller.EntityId); Body && Body->Action.IsSet()
        && Body->Action->Key.Origin==ELDCombatRequestOrigin::Direct
        && Body->Action->Key.DirectSequence<=Record->CancelledThrough)
    { AbandonAction(*Body,ELDCombatReject::Cancelled); }
    if (CurrentBody->ChannelEffectId.IsValid() && CurrentBody->ChannelDirectSequence<=Record->CancelledThrough)
    { CancelChannel(*CurrentBody); }
    Receipt.Phase=ELDCombatPhase::Finished;
    Receipt.Reject=ELDCombatReject::Cancelled;
    return Receipt;
}

FLDCombatActionReceipt ALDCombatDirector::QueryActionReceipt(const FLDCombatCaller& Caller, uint64 DirectSequence) const
{
    FLDCombatActionReceipt Receipt;
    Receipt.DirectSequence=DirectSequence;
    Receipt.Phase=ELDCombatPhase::Rejected;
    const auto* Mode=Core();
    const auto* Record=FindRecord(Caller.EntityId);
    if (!Mode || Caller.Match!=Mode->GetMatchContext() || !Record || Record->Kind!=Caller.Kind)
    { Receipt.Reject=ELDCombatReject::StaleMatch; return Receipt; }
    if (const auto* Known=Record->Recent.FindByPredicate([DirectSequence](const FLDCombatDirectEntry& Entry)
        { return Entry.Request.DirectSequence==DirectSequence; }))
    { return Known->Receipt; }
    Receipt.Reject=DirectSequence<=Record->DirectHighWater ? ELDCombatReject::ReceiptExpired : ELDCombatReject::StaleRequest;
    return Receipt;
}

bool ALDCombatDirector::ResolveUnitTarget(const FLDCombatEntityRecord& Record, const ALDCharacter& Actor,
    const FLDCombatTarget& Target, FLDCombatBodyRef& OutBody, FVector& OutFoot, ELDCombatReject& Reject) const
{
    OutBody={};
    OutFoot=FVector::ZeroVector;
    Reject=ELDCombatReject::TargetUnavailable;
    auto* Mode=Core();
    if (!Mode || Target.Kind!=ELDCombatTargetKind::Unit) { return false; }
    ALDCharacter* TargetActor=nullptr;
    if (Record.Kind==ELDVictimKind::Contender)
    {
        auto* PC=Mode->FindContenderController(Record.Entity);
        FLDAuthorityTargetSample Sample;
        if (!PC || !Mode->Vision || Mode->Vision->ResolveObservedUnit(*PC,Target.Observed,Target.ObservationEpoch,
            ELDObservationPurpose::AttackStart,Sample)!=ELDObservedResolve::Resolved)
        { return false; }
        OutBody=Sample.Target;
    }
    else
    {
        // Camp perception is the camp module's duty; Combat only checks the life is current and a contender.
        if (Target.ServerBody.Victim.Kind!=ELDVictimKind::Contender) { return false; }
        OutBody=Target.ServerBody;
    }
    const auto* TargetBody=FindBody(OutBody,&TargetActor);
    if (!TargetBody || !TargetActor || TargetBody->bPendingDeath || OutBody.Victim.EntityId==Record.Entity
        || IsIsolated(Record.Entity,*TargetBody)) { return false; }
    OutFoot=FootOf(*TargetActor);
    Reject=ELDCombatReject::None;
    return true;
}

namespace LDCA
{
    bool ClearSight(const ALDGameMode& Mode, const UWorld& World, const FVector& From, const FVector& To, bool& bFault)
    {
        bFault=false;
        const auto* Registry=World.GetSubsystem<ULDMapRegistry>();
        if (!Mode.Vision || !Registry) { bFault=true; return false; }
        const ELDSightResult Sight=Mode.Vision->TraceSight(From,To,Registry->Revision());
        if (Sight==ELDSightResult::InvalidPoint || Sight==ELDSightResult::StaleWorld) { bFault=true; return false; }
        return Sight==ELDSightResult::Visible;
    }
}

FLDCombatActionReceipt ALDCombatDirector::StartNavAttack(const FLDCombatCaller& Caller, const FLDCombatRequestKey& Key,
    const FLDCombatBodyRef& Target, const FGuid& Observed, uint32 ObservationEpoch, const FLDGameplayWritePermit& Permit)
{
    FLDCombatActionReceipt Receipt;
    Receipt.Generation=Key.Generation;
    Receipt.Life=Key.Life;
    Receipt.Origin=ELDCombatRequestOrigin::Nav;
    Receipt.Entry=ELDCombatEntry::Basic;
    Receipt.Phase=ELDCombatPhase::Rejected;
    const auto Refuse=[&Receipt](ELDCombatReject Reason) { Receipt.Reject=Reason; return Receipt; };
    auto* Mode=Core();
    if (!Mode || Mode->ValidateWritePermit(Permit,ELDCommitPhase::Actions)!=ELDCommitReject::None
        || Caller.Match!=Mode->GetMatchContext() || Caller.Kind!=ELDVictimKind::Contender)
    { return Refuse(ELDCombatReject::MatchClosed); }
    auto* Record=FindRecord(Caller.EntityId);
    ALDCharacter* Actor=nullptr;
    auto* Body=FindEntityBody(Caller.EntityId,&Actor);
    if (!Record || !Body || !Actor || Body->bPendingDeath) { return Refuse(ELDCombatReject::Dead); }
    if (Key.Origin!=ELDCombatRequestOrigin::Nav || Key.Entity!=Caller.EntityId || Key.Life!=Body->Victim.Life
        || Key.Generation!=Caller.Match.Generation || Key.Move.Life!=Key.Life || Key.Move.Match!=Key.Generation)
    { return Refuse(ELDCombatReject::InvalidRequest); }
    if (IsActionBlocked(*Body,ELDCombatControlAction::Basic)) { return Refuse(ELDCombatReject::ControlBlocked); }
    if (Body->Action.IsSet()) { return Refuse(ELDCombatReject::Busy); }
    const int64 Now=Mode->GetAuthorityNowMs();
    if (!LDControl::TauntAllows(Body->Taunt,Now,true,Target)) { return Refuse(ELDCombatReject::ControlBlocked); }
    if (Now<Record->NextAttackAtMs) { return Refuse(ELDCombatReject::AttackNotReady); }
    ALDCharacter* TargetActor=nullptr;
    const auto* TargetBody=FindBody(Target,&TargetActor);
    if (!TargetBody || !TargetActor || TargetBody->bPendingDeath || Target.Victim.EntityId==Caller.EntityId)
    { return Refuse(ELDCombatReject::TargetUnavailable); }
    const FVector Foot=FootOf(*Actor), TargetFoot=FootOf(*TargetActor);
    if (FVector::Dist2D(Foot,TargetFoot)>LDCA::ToDouble(Body->AttributeProjection.Final.AttackRangeCm))
    { return Refuse(ELDCombatReject::OutOfRange); }
    bool bFault=false;
    if (!LDCA::ClearSight(*Mode,*GetWorld(),Foot,TargetFoot,bFault))
    {
        if (bFault) { Mode->FaultAuthority(TEXT("Basic attack sight failed on a registered body.")); return Refuse(ELDCombatReject::AuthorityDataUnavailable); }
        return Refuse(ELDCombatReject::Blocked);
    }
    const auto& Parameters=Catalog()->Get().Parameters;
    FLDBasicTimingInput TimingInput;
    TimingInput.BaseAttackSpeed=Parameters.AttackSpeed.Base;
    TimingInput.CurrentAttackSpeed=Body->AttributeProjection.Final.AttackSpeed;
    TimingInput.BaseWindupMs=Parameters.BasicAttackWindupMs;
    const auto Timing=LDTactical::EvaluateBasicTiming(TimingInput,*Catalog());
    int64 ReleaseAt=0, NextAttack=0;
    FString Error;
    const int64 WindupMs=Timing.IsSuccess() ? Timing.Value->WindupMs : 0;
    if (!Timing.IsSuccess() || Record->NextToken==MAX_uint64
        || !LDMatchLifecycle::AddDurationMs(Now,WindupMs,ReleaseAt,Error)
        || !LDMatchLifecycle::AddDurationMs(Now,Timing.Value->IntervalMs,NextAttack,Error))
    { return Refuse(ELDCombatReject::AuthorityDataUnavailable); }
    FLDCombatAction Action;
    Action.Kind=ELDCombatActionKind::Basic;
    Action.Key=Key;
    Action.Token=Record->NextToken++;
    Action.Entry=ELDCombatEntry::Basic;
    Action.Source.Kind=ELDCombatSourceKind::Basic;
    Action.Target.Kind=ELDCombatTargetKind::Unit;
    Action.Target.Observed=Observed;
    Action.Target.ObservationEpoch=ObservationEpoch;
    Action.TargetBody=Target;
    Action.StartedAtMs=Now;
    Action.ReleaseAtMs=ReleaseAt;
    CancelChannel(*Body);
    Body->Action=Action;
    Body->BasicTiming=Timing.Value.GetValue();
    Body->LastCombatAtMs=Now;
    ++Body->StateRevision;
    // #198: an accepted basic windup is a combat action of the build safe window.
    MarkBuildUnsafe(Caller.EntityId,Now);
    LDCA::FaceToward(*Actor,TargetFoot);
    // The committed cadence survives cancellation, orders, death and respawn; time keeps running.
    Record->NextAttackAtMs=NextAttack;
    MarkViewChanged(Caller.EntityId);
    Receipt.CombatToken=Action.Token;
    Receipt.Phase=ELDCombatPhase::Accepted;
    Receipt.Reject=ELDCombatReject::None;
    return Receipt;
}

bool ALDCombatDirector::StartNavCast(const FLDCombatCaller& Caller, const FLDCombatActionRequest& Request,
    const FLDGameplayWritePermit& Permit, FLDCombatActionReceipt& Out, bool& bFinal, FString& Error, bool bQueuedStep)
{
    bFinal=true;
    Out={};
    Out.Generation=Request.Generation;
    Out.Life=Request.Life;
    Out.Origin=ELDCombatRequestOrigin::Direct;
    Out.DirectSequence=Request.DirectSequence;
    Out.Entry=Request.Entry;
    Out.Phase=ELDCombatPhase::Rejected;
    auto* Mode=Core();
    if (!Mode || Mode->ValidateWritePermit(Permit,ELDCommitPhase::Actions)!=ELDCommitReject::None
        || Caller.Match!=Mode->GetMatchContext() || Caller.Kind!=ELDVictimKind::Contender)
    { Out.Reject=ELDCombatReject::MatchClosed; return true; }
    auto* Record=FindRecord(Caller.EntityId);
    if (!Record || Record->Kind!=Caller.Kind) { Out.Reject=ELDCombatReject::NotOwner; return true; }
    const FLDCombatTarget& Aim=Request.Target;
    const bool bUnit=Aim.Kind==ELDCombatTargetKind::Unit;
    const bool bAim=!Aim.ServerBody.IsValid() && (bUnit ? Aim.Observed.IsValid() && Aim.ObservationEpoch!=0
        : Aim.Kind==ELDCombatTargetKind::Point ? LDCA::ValidPoint(Aim.Point) && !Aim.Observed.IsValid() && !Aim.ObservationEpoch
        : Aim.Kind==ELDCombatTargetKind::Direction && LDCA::ValidDirection(Aim.Direction));
    const bool bShape=Request.DirectSequence!=0 && Request.Life!=0 && LDCA::IsContenderEntry(Request.Entry)
        && Request.Entry!=ELDCombatEntry::Dash && bAim;
    if (!bShape) { Out.Reject=ELDCombatReject::InvalidRequest; return true; }
    const bool bRecorded=Record->Recent.ContainsByPredicate([&Request](const FLDCombatDirectEntry& Entry)
        { return Entry.Request.DirectSequence==Request.DirectSequence; });
    // A cast order judges its request once: any recorded or expired sequence is stale here.
    if (LDCombatSourceRules::DirectSequenceVerdict(bRecorded,false,Request.DirectSequence,Record->DirectHighWater,bQueuedStep)
        !=ELDDirectSequenceVerdict::Fresh)
    { Out.Reject=ELDCombatReject::StaleRequest; return true; }
    FLDCombatActionReceipt Judged;
    if (!ProcessDirect(*Record,Request,Permit,Judged,Error)) { return false; }
    if (Judged.Phase==ELDCombatPhase::Rejected && (Judged.Reject==ELDCombatReject::OutOfRange
        || (bUnit && Judged.Reject==ELDCombatReject::Blocked) || Judged.Reject==ELDCombatReject::Busy))
    {
        // Not yet: the order closes in (or waits for the own windup) and tries the same request again.
        Out=Judged;
        bFinal=false;
        return true;
    }
    Record=FindRecord(Caller.EntityId);
    if (!Record) { Out=Judged; return true; }
    Record->DirectHighWater=FMath::Max(Record->DirectHighWater,Request.DirectSequence);
    StoreReceipt(*Record,Request,Judged);
    Out=Record->Recent.Last().Receipt;
    SendReceipt(*Record,Out);
    return true;
}

bool ALDCombatDirector::RecordDirectRefusal(const FLDCombatCaller& Caller, const FLDCombatActionRequest& Request,
    ELDCombatReject Reject, FLDCombatActionReceipt& Out)
{
    Out={};
    Out.Generation=Request.Generation;
    Out.Life=Request.Life;
    Out.Origin=ELDCombatRequestOrigin::Direct;
    Out.DirectSequence=Request.DirectSequence;
    Out.Entry=Request.Entry;
    Out.Phase=ELDCombatPhase::Rejected;
    Out.Reject=Reject;
    const auto* Mode=Core();
    auto* Record=FindRecord(Caller.EntityId);
    if (!Mode || Caller.Match!=Mode->GetMatchContext() || !Record || Record->Kind!=Caller.Kind || !Request.DirectSequence
        || Reject==ELDCombatReject::None)
    { return false; }
    // Only a sequence the window has never seen: a recorded or expired one keeps Combat's own answer.
    const bool bRecorded=Record->Recent.ContainsByPredicate([&Request](const FLDCombatDirectEntry& Entry)
        { return Entry.Request.DirectSequence==Request.DirectSequence; });
    if (LDCombatSourceRules::DirectSequenceVerdict(bRecorded,false,Request.DirectSequence,Record->DirectHighWater,false)
        !=ELDDirectSequenceVerdict::Fresh)
    { return false; }
    StoreReceipt(*Record,Request,Out);
    const auto* Stored=Record->Recent.FindByPredicate([&Request](const FLDCombatDirectEntry& Entry)
        { return Entry.Request.DirectSequence==Request.DirectSequence; });
    if (!Stored) { return false; }
    Out=Stored->Receipt;
    return true;
}

void ALDCombatDirector::CancelNavAttack(const FGuid& Entity, const FLDMoveIdentity& Move, const FLDCombatBodyRef& Keep)
{
    auto* Body=FindEntityBody(Entity);
    if (Body && Body->Action.IsSet() && Body->Action->Kind==ELDCombatActionKind::Basic
        && Body->Action->Key.Origin==ELDCombatRequestOrigin::Nav && Body->Action->Key.Move==Move
        && !(Keep.IsValid() && Body->Action->TargetBody==Keep))
    { FinishAction(*Body,ELDCombatReject::Cancelled,false); }
}

bool ALDCombatDirector::AdoptNavAttack(const FGuid& Entity, const FLDMoveIdentity& From, const FLDMoveIdentity& To,
    const FGuid& Observed, uint32 ObservationEpoch)
{
    auto* Body=FindEntityBody(Entity);
    if (!Body || Body->bPendingDeath || !Body->Action.IsSet() || !Observed.IsValid() || !ObservationEpoch) { return false; }
    FLDCombatAction& Action=Body->Action.GetValue();
    if (Action.Kind!=ELDCombatActionKind::Basic || Action.Key.Origin!=ELDCombatRequestOrigin::Nav || Action.Key.Move!=From
        || To==From || To.Match!=Action.Key.Generation || To.Life!=Action.Key.Life || To.Life!=Body->Victim.Life
        || Action.Target.Observed!=Observed || Action.Target.ObservationEpoch!=ObservationEpoch)
    { return false; }
    Action.Key.Move=To;
    UE_LOG(LogLDCombat,Verbose,TEXT("LD_COMBAT_SWING_ADOPTED token=%llu from_order=%u to_order=%u"),Action.Token,From.Order,To.Order);
    return true;
}

bool ALDCombatDirector::ProcessDirect(FLDCombatEntityRecord& Record, const FLDCombatActionRequest& Request,
    const FLDGameplayWritePermit& Permit, FLDCombatActionReceipt& Receipt, FString& Error)
{
    Receipt={};
    Receipt.Generation=Request.Generation;
    Receipt.Life=Request.Life;
    Receipt.Origin=ELDCombatRequestOrigin::Direct;
    Receipt.DirectSequence=Request.DirectSequence;
    Receipt.Entry=Request.Entry;
    Receipt.Phase=ELDCombatPhase::Rejected;
    const auto Refuse=[&Receipt](ELDCombatReject Reason) { Receipt.Phase=ELDCombatPhase::Rejected; Receipt.Reject=Reason; return true; };
    auto* Mode=Core();
    const auto* Content=Catalog();
    if (!Mode || !Content || Mode->ValidateWritePermit(Permit,ELDCommitPhase::Actions)!=ELDCommitReject::None)
    { Error=TEXT("Direct combat admission requires the current Actions permit."); return false; }
    if (Request.Generation!=Mode->GetMatchContext().Generation) { return Refuse(ELDCombatReject::StaleMatch); }
    if (Request.DirectSequence<=Record.CancelledThrough) { return Refuse(ELDCombatReject::Cancelled); }
    ALDCharacter* Actor=nullptr;
    auto* Body=FindEntityBody(Record.Entity,&Actor);
    if (!Body || !Actor || Body->bPendingDeath) { return Refuse(ELDCombatReject::Dead); }
    if (Body->Victim.Life!=Request.Life) { return Refuse(ELDCombatReject::StaleLife); }
    const int64 Now=Mode->GetAuthorityNowMs();
    const FLDCombatBodyRef SelfRef{Body->Victim,Body->Identity};
    // The innate D dash is movement, not an aggressive intent (APP_SPEC 7.1: a protected body may move).
    if (Record.Kind==ELDVictimKind::Contender && IsAggressiveEntry(SelfRef,Request.Entry))
    {
        // Every admitted Q/W attempt is an aggressive intent: protection ends before the action is checked.
        auto* PC=Mode->FindContenderController(Record.Entity);
        if (!PC) { return Refuse(ELDCombatReject::NotOwner); }
        FLDAggressiveIntent Intent;
        Intent.Kind=ELDAggressiveIntentKind::ActivateCharacter;
        Intent.Request={ELDAggressionOrigin::Combat,Request.DirectSequence,0};
        const auto Protection=Mode->EndProtectionForAggressiveIntent(*PC,Mode->GetMatchContext(),SelfRef,Intent,Permit);
        if (Protection.Outcome==ELDProtectionOutcome::Rejected)
        { return Refuse(Protection.Reason==ELDCommitReject::StaleLife ? ELDCombatReject::StaleLife : ELDCombatReject::MatchClosed); }
        Receipt.bProtectionRemoved=Protection.bProtectionRemoved;
        Receipt.LifeRevision=Protection.LifeRevision;
        // A taunted body may only basic-attack its taunter: no ability until the taunt ends.
        if (LDControl::IsTaunted(Body->Taunt,Now)) { return Refuse(ELDCombatReject::ControlBlocked); }
    }
    if (Record.Kind==ELDVictimKind::CampMember)
    {
        // Registered camp basic attack: same body, cadence, range, sight and hit pipeline as contenders.
        if (IsActionBlocked(*Body,ELDCombatControlAction::Basic)) { return Refuse(ELDCombatReject::ControlBlocked); }
        if (Body->Action.IsSet()) { return Refuse(ELDCombatReject::Busy); }
        if (Now<Body->MonsterNextAttackAtMs) { return Refuse(ELDCombatReject::AttackNotReady); }
        FLDCombatBodyRef TargetRef;
        FVector TargetFoot;
        ELDCombatReject Reject;
        if (!ResolveUnitTarget(Record,*Actor,Request.Target,TargetRef,TargetFoot,Reject)) { return Refuse(Reject); }
        const FVector Foot=FootOf(*Actor);
        if (FVector::Dist2D(Foot,TargetFoot)>LDCA::ToDouble(Body->AttributeProjection.Final.AttackRangeCm))
        { return Refuse(ELDCombatReject::OutOfRange); }
        bool bFault=false;
        if (!LDCA::ClearSight(*Mode,*GetWorld(),Foot,TargetFoot,bFault))
        {
            if (bFault) { Error=TEXT("Camp attack sight failed on a registered body."); return false; }
            return Refuse(ELDCombatReject::Blocked);
        }
        int64 ReleaseAt=0, NextAttack=0;
        if (Record.NextToken==MAX_uint64 || !LDMatchLifecycle::AddDurationMs(Now,Body->BasicTiming.WindupMs,ReleaseAt,Error)
            || !LDMatchLifecycle::AddDurationMs(Now,Body->BasicTiming.IntervalMs,NextAttack,Error))
        { return false; }
        FLDCombatAction Action;
        Action.Kind=ELDCombatActionKind::Basic;
        Action.Key={ELDCombatRequestOrigin::Direct,Request.Generation,Record.Entity,Request.Life,Request.DirectSequence};
        Action.Token=Record.NextToken++;
        Action.Entry=ELDCombatEntry::Basic;
        Action.Source.Kind=ELDCombatSourceKind::Basic;
        Action.Target=Request.Target;
        Action.TargetBody=TargetRef;
        Action.StartedAtMs=Now;
        Action.ReleaseAtMs=ReleaseAt;
        Body->Action=Action;
        Body->MonsterNextAttackAtMs=NextAttack;
        Body->LastCombatAtMs=Now;
        ++Body->StateRevision;
        LDCA::FaceToward(*Actor,TargetFoot);
        Receipt.CombatToken=Action.Token;
        Receipt.Phase=ELDCombatPhase::Accepted;
        Receipt.Reject=ELDCombatReject::None;
        return true;
    }
    FLDCombatSourceRef Source;
    const FLDDemoAbilityDefinition* Ability=nullptr;
    if (!ResolveEntry(Record,Request.Entry,Source,Ability)) { return Refuse(ELDCombatReject::SourceUnavailable); }
    if (Request.ExpectedSourceRevision!=Record.SourceRevision) { return Refuse(ELDCombatReject::StaleSource); }
    FLDCombatActiveTerms Terms;
    if (!ActiveOf(Record,Source,Ability,Terms)) { return Refuse(ELDCombatReject::SourceUnavailable); }
    const FLDDemoAbilityValues* ValuesDef=&Terms.Values;
    const FLDDemoTargetDefinition* TargetDef=&Terms.Target;
    const int64 WindupMs=Terms.WindupMs;
    const auto* Track=TrackFor(Record,Request.Entry);
    if (!Track) { Error=TEXT("A held active source has no persistent cooldown track."); return false; }
    FLDActiveGateInput Gate;
    Gate.NowMs=Now;
    Gate.ReadyAtMs=Track->ReadyAtMs;
    Gate.MpP2=Body->Resources.MpP2;
    Gate.ManaCostP2=ValuesDef->ManaCostP2;
    // Only an action that displaces its caster is a dash for the root matrix (a ground zone is not).
    Gate.bDash=LDCombatSourceRules::DisplacesCaster(*ValuesDef);
    Gate.Roots=&Body->Roots;
    Gate.bControlBlocked=IsCastBlocked(*Body,Source.Kind);
    Gate.bDisplacementBlocked=IsActionBlocked(*Body,ELDCombatControlAction::Displacement);
    Gate.bCheckBusy=true;
    Gate.bAbilityInProgress=Body->Action.IsSet() && Body->Action->Kind==ELDCombatActionKind::Ability;
    if (const ELDCombatReject Blocked=LDCombatSourceRules::ActiveGate(Gate,Content->Get().Policies); Blocked!=ELDCombatReject::None)
    { return Refuse(Blocked); }
    FLDCombatAction Action;
    Action.Kind=ELDCombatActionKind::Ability;
    Action.Key={ELDCombatRequestOrigin::Direct,Request.Generation,Record.Entity,Request.Life,Request.DirectSequence};
    Action.Entry=Request.Entry;
    Action.Source=Source;
    Action.Target=Request.Target;
    Action.SourceRevision=Record.SourceRevision;
    switch (TargetDef->Mode)
    {
    case ELDDemoTargetMode::Self:
        Action.Target.Kind=ELDCombatTargetKind::Self;
        Action.TargetBody=SelfRef;
        break;
    case ELDDemoTargetMode::Unit:
    {
        ELDCombatReject Reject;
        if (!ResolveUnitTarget(Record,*Actor,Request.Target,Action.TargetBody,Action.TargetPoint,Reject)) { return Refuse(Reject); }
        const FVector Foot=FootOf(*Actor);
        double RangeCm=0;
        if (!CastRangeCm(*Body,*TargetDef,RangeCm,Error)) { return false; }
        if (FVector::Dist2D(Foot,Action.TargetPoint)>RangeCm) { return Refuse(ELDCombatReject::OutOfRange); }
        bool bFault=false;
        if (!LDCA::ClearSight(*Mode,*GetWorld(),Foot,Action.TargetPoint,bFault))
        {
            if (bFault) { Error=TEXT("Ability sight failed on a registered body."); return false; }
            return Refuse(ELDCombatReject::Blocked);
        }
        break;
    }
    case ELDDemoTargetMode::GroundPoint:
        // A ground zone: the release clamps a point beyond the authored reach toward it.
        if (Request.Target.Kind!=ELDCombatTargetKind::Point || !LDCA::ValidPoint(Request.Target.Point))
        { return Refuse(ELDCombatReject::InvalidDirection); }
        Action.TargetPoint=Request.Target.Point;
        break;
    case ELDDemoTargetMode::DirectionProjectile:
    case ELDDemoTargetMode::DirectionCone:
    case ELDDemoTargetMode::DirectionLine:
    case ELDDemoTargetMode::DirectionDash:
        if (Request.Target.Kind!=ELDCombatTargetKind::Direction || !LDCA::ValidDirection(Request.Target.Direction))
        { return Refuse(ELDCombatReject::InvalidDirection); }
        Action.Direction=FVector(Request.Target.Direction).GetSafeNormal2D();
        if (TargetDef->Mode==ELDDemoTargetMode::DirectionDash)
        {
            auto* Movement=Cast<ULDCharacterMovementComponent>(Actor->GetCharacterMovement());
            if (!Movement || !Movement->CanStartDash(Action.Direction,LDCA::DashProbeCm)) { return Refuse(ELDCombatReject::Blocked); }
        }
        break;
    default:
        return Refuse(ELDCombatReject::SourceUnavailable);
    }
    if (Record.NextToken==MAX_uint64 || !LDMatchLifecycle::AddDurationMs(Now,WindupMs,Action.ReleaseAtMs,Error)) { return false; }
    Action.Token=Record.NextToken++;
    Action.StartedAtMs=Now;
    CancelChannel(*Body);
    // A new ability interrupts an unreleased basic windup; its committed cadence is not refunded.
    if (Body->Action.IsSet()) { AbandonAction(*Body,ELDCombatReject::Cancelled); }
    Body->Action=Action;
    if (TargetDef->Mode==ELDDemoTargetMode::Unit || TargetDef->Mode==ELDDemoTargetMode::GroundPoint) { LDCA::FaceToward(*Actor,Action.TargetPoint); }
    else { LDCA::FaceAlong(*Actor,Action.Direction); }
    ++Body->StateRevision;
    MarkViewChanged(Record.Entity);
    Receipt.CombatToken=Action.Token;
    Receipt.Phase=ELDCombatPhase::Accepted;
    Receipt.Reject=ELDCombatReject::None;
    if (Action.ReleaseAtMs>Now) { return true; }
    ELDCombatReject Reject=ELDCombatReject::None;
    if (!ReleaseAbility(*Actor,*Body,Action,Reject,Error)) { return false; }
    FinishAction(*Body,Reject,Reject==ELDCombatReject::None);
    if (Reject!=ELDCombatReject::None) { Receipt.Phase=ELDCombatPhase::Rejected; Receipt.Reject=Reject; }
    else { Receipt.Phase=ELDCombatPhase::Finished; }
    return true;
}

bool ALDCombatDirector::ReleaseDue(const FLDGameplayWritePermit& Permit, FString& Error)
{
    auto* Mode=Core();
    if (!Mode || Mode->ValidateWritePermit(Permit,ELDCommitPhase::Actions)!=ELDCommitReject::None)
    { Error=TEXT("Releases require the current Actions permit."); return false; }
    const int64 Now=Mode->GetAuthorityNowMs();
    TArray<TPair<FGuid,TWeakObjectPtr<ALDCharacter>>> Due;
    for (const auto& Pair : Bodies)
    {
        if (Pair.Key.IsValid() && !Pair.Value.bPendingDeath && Pair.Value.Action.IsSet() && Pair.Value.Action->ReleaseAtMs<=Now)
        { Due.Emplace(Pair.Value.Victim.EntityId,Pair.Key); }
    }
    Due.Sort([](const auto& A,const auto& B) { return A.Key<B.Key; });
    for (const auto& Entry : Due)
    {
        auto* Actor=Entry.Value.Get();
        auto* Body=Actor?Bodies.Find(Actor):nullptr;
        if (!Body || !Body->Action.IsSet() || Body->bPendingDeath) { continue; }
        const FLDCombatAction Action=Body->Action.GetValue();
        ELDCombatReject Reject=ELDCombatReject::None;
        // A released action stays visible for its cue window (LDCombatCues); a refused release shows nothing more.
        const auto Released=[this,Actor,&Action,Now]()
        {
            if (FLDBodyCombatState* After=Bodies.Find(Actor))
            {
                After->Cues.bReleasedBasic=Action.Kind==ELDCombatActionKind::Basic;
                After->Cues.ReleasedStartedAtMs=Action.StartedAtMs;
                After->Cues.ReleasedAtMs=Now;
                After->ReleasedSource=Action.Source;
                MarkViewChanged(After->Victim.EntityId);
            }
        };
        if (Action.Kind==ELDCombatActionKind::Basic)
        {
            if (!ReleaseBasic(*Actor,*Body,Action,Reject,Error)) { return false; }
            if (Reject==ELDCombatReject::None) { Released(); }
            // A Nav-origin basic reports through its order; only Direct requests own a combat receipt.
            if (Action.Key.Origin==ELDCombatRequestOrigin::Direct) { FinishDirectReceipt(Entry.Key,Action,Reject); }
            continue;
        }
        if (!ReleaseAbility(*Actor,*Body,Action,Reject,Error)) { return false; }
        FinishAction(*Body,Reject,Reject==ELDCombatReject::None);
        if (Reject==ELDCombatReject::None) { Released(); }
        FinishDirectReceipt(Entry.Key,Action,Reject);
    }
    return true;
}

void ALDCombatDirector::FinishDirectReceipt(const FGuid& Entity, const FLDCombatAction& Action, ELDCombatReject Reject)
{
    auto* Record=FindRecord(Entity);
    if (!Record) { return; }
    FLDCombatActionReceipt Receipt;
    Receipt.Generation=Action.Key.Generation;
    Receipt.Life=Action.Key.Life;
    Receipt.DirectSequence=Action.Key.DirectSequence;
    Receipt.Entry=Action.Entry;
    Receipt.CombatToken=Action.Token;
    Receipt.Phase=Reject==ELDCombatReject::None ? ELDCombatPhase::Finished : ELDCombatPhase::Rejected;
    Receipt.Reject=Reject;
    if (const auto* Known=Record->Recent.FindByPredicate([&Action](const FLDCombatDirectEntry& Entry)
        { return Entry.Request.DirectSequence==Action.Key.DirectSequence; }))
    {
        Receipt.bProtectionRemoved=Known->Receipt.bProtectionRemoved;
        Receipt.LifeRevision=Known->Receipt.LifeRevision;
        StoreReceipt(*Record,Known->Request,Receipt);
    }
    SendReceipt(*Record,Receipt);
}

bool ALDCombatDirector::ReleaseBasic(ALDCharacter& Actor, FLDBodyCombatState& Body, const FLDCombatAction& Action,
    ELDCombatReject& Outcome, FString& Error)
{
    Outcome=ELDCombatReject::None;
    auto* Mode=Core();
    const auto* Content=Catalog();
    auto* Record=FindRecord(Body.Victim.EntityId);
    if (!Mode || !Content || !Record) { Error=TEXT("Basic release lost its core, catalog or entity record."); return false; }
    // A started swing lands on its committed life however far it went (2026-11-07 user decision,
    // LDCombatSourceRules::BasicReleaseGate): range gates only the start (StartNavAttack / camp admission). The release
    // re-checks the attacker's own control, that the exact target life still stands, and (a later 2026-11-07 decision)
    // that it is still observed and in clear sight; the windup already spent the cadence.
    const FLDCombatBodyRef TargetRef=Action.TargetBody;
    ALDCharacter* TargetActor=nullptr;
    const FLDBodyCombatState* TargetBody=TargetRef.IsValid() ? FindBody(TargetRef,&TargetActor) : nullptr;
    FLDBasicReleaseInput Gate;
    Gate.bAttackBlocked=IsActionBlocked(Body,ELDCombatControlAction::Basic);
    Gate.bTauntAllows=LDControl::TauntAllows(Body.Taunt,Mode->GetAuthorityNowMs(),true,TargetRef);
    Gate.Committed=TargetRef;
    if (TargetBody && TargetActor) { Gate.Current={TargetBody->Victim,TargetBody->Identity}; }
    Gate.bCurrentPendingDeath=TargetBody && TargetBody->bPendingDeath;
    Gate.bTargetPresent=TargetRef.Victim.Kind!=ELDVictimKind::Contender || Mode->IsMemberPresent(TargetRef.Victim.EntityId);
    Gate.bSelf=TargetRef.Victim.EntityId==Record->Entity;
    // Observation is resolved afresh for this release (contenders through Vision; camp attackers by server body).
    FLDCombatBodyRef Seen;
    FVector SeenFoot;
    ELDCombatReject SeenReject;
    Gate.bObserved=ResolveUnitTarget(*Record,Actor,Action.Target,Seen,SeenFoot,SeenReject) && Seen==TargetRef;
    if (Gate.bObserved)
    {
        bool bFault=false;
        Gate.bClearSight=LDCA::ClearSight(*Mode,*GetWorld(),FootOf(Actor),SeenFoot,bFault);
        if (bFault) { Error=TEXT("Basic release sight failed on a registered body."); return false; }
    }
    Outcome=LDCombatSourceRules::BasicReleaseGate(Gate);
    if (Outcome!=ELDCombatReject::None)
    {
        UE_LOG(LogLDCombat,Log,TEXT("LD_COMBAT_BASIC_RELEASE token=%llu outcome=refused reason=%u"),Action.Token,uint32(Outcome));
        // A Direct swing's refusal is counted when its receipt turns Rejected (StoreReceipt); a Nav-origin one only here.
        if (Action.Key.Origin!=ELDCombatRequestOrigin::Direct) { LedgerActionRefused(Body.Victim.EntityId,Outcome); }
        FinishAction(Body,Outcome,false);
        return true;
    }
    UE_LOG(LogLDCombat,Verbose,TEXT("LD_COMBAT_BASIC_RELEASE token=%llu outcome=hit distance_cm=%.0f"),Action.Token,
        FVector::Dist2D(FootOf(Actor),FootOf(*TargetActor)));
    return LandBasic(Actor,Body,Action,Error);
}

bool ALDCombatDirector::LandBasic(ALDCharacter& Actor, FLDBodyCombatState& Body, const FLDCombatAction& Action, FString& Error)
{
    auto* Mode=Core();
    const auto* Content=Catalog();
    if (!Mode || !Content || !Action.TargetBody.IsValid())
    { Error=TEXT("A landing basic lost its core, catalog or committed target."); return false; }
    const auto& Final=Body.AttributeProjection.Final;
    FLDCombatHit Hit;
    Hit.HitId=FGuid::NewGuid();
    Hit.EffectId=FGuid::NewGuid();
    Hit.Token=Action.Token;
    Hit.Target=Action.TargetBody;
    Hit.SourceBody={Body.Victim,Body.Identity};
    Hit.SourceEntity=Body.Victim.EntityId;
    Hit.CreditOwner=Body.Victim.Kind==ELDVictimKind::Contender ? Body.Victim.EntityId : FGuid();
    Hit.Source.Kind=ELDCombatSourceKind::Basic;
    Hit.HitClass=ELDCombatHitClass::BasicPrimary;
    Hit.DamageType=ELDDemoDamageType::Physical;
    Hit.ADRatioBp=Body.Victim.Kind==ELDVictimKind::Contender ? Content->Get().Character.BasicADRatioBp : LDCA::MonsterBasicADRatioBp;
    Hit.SourceADP2=Final.ADP2;
    Hit.SourceAPP2=Final.APP2;
    if (!StampSourceTerms(Body,Hit,Error) || !SnapshotOnHit(Body,10000,Hit,Error)) { return false; }
    // B14: only the primary basic's own AD package scales; the empowered packages added below never do.
    TArray<FLDPassiveEffectRef> Scales;
    if (!CollectPassiveEffects(Body.AttributeProjection,ELDDemoEffectOp::BasicDamageScale,Scales,Error)) { return false; }
    for (const FLDPassiveEffectRef& Scale : Scales)
    {
        if (Scale.Effect.RatioBp<0 || Scale.Effect.RatioBp>10000)
        { Error=FString::Printf(TEXT("basic_damage_scale of %s is outside 0..1."),*Scale.Key); return false; }
        Hit.ADRatioBp=Hit.ADRatioBp*Scale.Effect.RatioBp/10000;
    }
    const int64 Now=Mode->GetAuthorityNowMs();
    // Spec §10.2: every live empowered package (one per source: S11, S17, B12) joins this one primary hit before its
    // critical roll, so the hit crits, heals and counts once.
    const int32 PendingBefore=Body.Empowers.Num();
    TArray<FLDCombatEmpower> Taken;
    int64 EmpowerP2=0;
    if (!LDCombatControl::ConsumeEmpowers(Body.Empowers,Now,Taken,EmpowerP2) || EmpowerP2>MAX_int64-Hit.BaseP2)
    { Error=TEXT("The empowered packages of a basic leave the checked numeric domain."); return false; }
    Hit.BaseP2+=EmpowerP2;
    bool bChargeBonusEnded=false;
    for (const FLDCombatEmpower& Package : Taken)
    {
        // §10: a component's package (B12, C09) is one trigger when a landed basic consumes it; arming it is none.
        if (Package.Source.Kind==ELDCombatSourceKind::Component) { LedgerComponentTriggered(Body,Package.Source.DefinitionId); }
        // The stats held while charges remain end with the last charge.
        if (Package.Charges==0 && Package.bChargeBonus)
        {
            bChargeBonusEnded|=Body.TimedBonuses.RemoveAll([&Package](const FLDCombatTimedBonus& Row)
            { return Row.EffectId==Package.EffectId; })>0;
        }
    }
    if (bChargeBonusEnded && !RefreshTemporary(Body,&Actor,Now,Error)) { return false; }
    // One critical sample per AttackId, at the successful release only.
    Hit.bCritical=Final.CritChanceBp>0 && CritRandom.RandRange(0,9999)<Final.CritChanceBp;
    // A B06 push leaves from the holder's foot (this one if the holder is gone when it lands).
    Hit.KnockbackAnchorCm=FootOf(Actor);
    if (!OnBasicLanded(Body,Hit,Error)) { return false; }
    PendingHits.Add(Hit);
    LedgerEffectReleased(Body,Hit.EffectId,ELDBlueprintFact::BasicAttacks);
    if (!Taken.IsEmpty() || Body.Empowers.Num()!=PendingBefore)
    {
        ++Body.StateRevision;
        MarkViewChanged(Body.Victim.EntityId);
    }
    FinishAction(Body,ELDCombatReject::None,true);
    return true;
}

bool ALDCombatDirector::OnBasicLanded(FLDBodyCombatState& Body, FLDCombatHit& Primary, FString& Error)
{
    const ALDGameMode* Mode=Core();
    if (!Mode) { Error=TEXT("A landed basic requires the current core."); return false; }
    const FGuid& Entity=Body.Victim.EntityId;
    // cooldown_refund (BasicHit): every landed basic cuts all three learned actives.
    {
        TArray<FLDPassiveEffectRef> Refunds;
        if (!CollectPassiveEffects(Body.AttributeProjection,ELDDemoEffectOp::CooldownRefund,Refunds,Error)) { return false; }
        for (const FLDPassiveEffectRef& Ref : Refunds)
        {
            if (Ref.Effect.Trigger!=ELDDemoEffectTrigger::BasicHit) { continue; }
            ReduceCooldown(Entity,ELDCombatEntry::Q,Ref.Effect.AmountMs);
            ReduceCooldown(Entity,ELDCombatEntry::W,Ref.Effect.AmountMs);
            ReduceCooldown(Entity,ELDCombatEntry::E,Ref.Effect.AmountMs);
        }
    }
    // B06 (nth_basic_bonus): every Count-th landed primary basic of this life pushes its primary target DistanceCm over
    // DurationMs away from the holder; no damage. The count survives target changes and ends with the life.
    TArray<FLDPassiveEffectRef> Counters;
    if (!CollectPassiveEffects(Body.AttributeProjection,ELDDemoEffectOp::NthBasicBonus,Counters,Error)) { return false; }
    if (!Counters.IsEmpty()) { ++Body.LandedBasics; }
    for (const FLDPassiveEffectRef& Ref : Counters)
    {
        const FLDDemoRational& Distance=Ref.Effect.DistanceCm;
        if (Ref.Effect.Count<=0 || Ref.Effect.DurationMs<=0 || Distance.Denominator<=0 || Distance.Numerator<=0)
        { Error=FString::Printf(TEXT("nth_basic_bonus of %s needs a positive count, distance and duration."),*Ref.Key); return false; }
        if (Body.LandedBasics%Ref.Effect.Count!=0) { continue; }
        Primary.KnockbackCm=double(Distance.Numerator)/double(Distance.Denominator);
        Primary.KnockbackMs=Ref.Effect.DurationMs;
        Primary.bKnockbackFromAnchor=false;
        // #198 F: C02 reads this push as B06's; M03 on B06 pushes farther for a lighter third hit.
        Primary.KnockbackSource=Ref.Source;
        TArray<FLDPassiveEffectRef> Farther;
        if (!WorkshopOn(Body.AttributeProjection,ELDDemoEffectOp::KnockbackScale,Ref.Source,Farther,Error)) { return false; }
        for (const FLDPassiveEffectRef& Mod : Farther)
        {
            Primary.KnockbackCm=Primary.KnockbackCm*double(Mod.Effect.RatioBp)/10000.;
            Primary.KnockbackMs=Primary.KnockbackMs*Mod.Effect.RatioBp/10000;
            Primary.PackageScaleBp=Primary.PackageScaleBp*Mod.Effect.MultiplierBp/10000;
        }
    }
    MarkViewChanged(Entity);
    return true;
}

bool ALDCombatDirector::ReleaseAbility(ALDCharacter& Actor, FLDBodyCombatState& Body, const FLDCombatAction& Action,
    ELDCombatReject& Reject, FString& Error)
{
    Reject=ELDCombatReject::None;
    auto* Mode=Core();
    const auto* Content=Catalog();
    auto* Record=FindRecord(Body.Victim.EntityId);
    if (!Mode || !Content || !Record) { Error=TEXT("Ability release lost its core, catalog or entity record."); return false; }
    const int64 Now=Mode->GetAuthorityNowMs();
    // Re-check the exact source: a forgotten or replaced source cannot release (and pays nothing).
    FLDCombatSourceRef Source;
    const FLDDemoAbilityDefinition* Ability=nullptr;
    if (!ResolveEntry(*Record,Action.Entry,Source,Ability) || Source!=Action.Source)
    { Reject=ELDCombatReject::SourceUnavailable; return true; }
    FLDCombatActiveTerms Terms;
    auto* Track=TrackFor(*Record,Action.Entry);
    if (!ActiveOf(*Record,Source,Ability,Terms) || !Track)
    { Reject=ELDCombatReject::SourceUnavailable; return true; }
    const FLDDemoAbilityValues* ValuesDef=&Terms.Values;
    const FLDDemoTargetDefinition& TargetDef=Terms.Target;
    // The caster's foot before any displacement of this release (areas, zones and pushes centre here).
    const FVector ReleaseFoot=FootOf(Actor);
    // The innate dash takes the approved character's dash terms (D19).
    const bool bInnate=Source.Kind==ELDCombatSourceKind::Innate;
    FLDInnateDashTerms Innate;
    if (bInnate && !InnateDashFor(Innate,Error)) { return false; }
    // #198 F M08/M09 on D: its distance and issued cooldown scale like a learned dash's (Terms).
    if (bInnate)
    {
        Innate.DistanceCm.Numerator*=Terms.DashScaleBp;
        Innate.DistanceCm.Denominator*=10000;
        Innate.CooldownMs=Innate.CooldownMs/10000*Terms.CooldownScaleBp+Innate.CooldownMs%10000*Terms.CooldownScaleBp/10000;
    }
    // A stun or taunt that landed during the windup ends it (normally already abandoned at the hit).
    FLDActiveGateInput Gate;
    Gate.NowMs=Now;
    Gate.ReadyAtMs=Track->ReadyAtMs;
    Gate.MpP2=Body.Resources.MpP2;
    Gate.ManaCostP2=bInnate ? Innate.ManaCostP2 : ValuesDef->ManaCostP2;
    // Only an action that displaces its caster is a dash for the root matrix (a ground zone is not).
    Gate.bDash=LDCombatSourceRules::DisplacesCaster(*ValuesDef);
    Gate.Roots=&Body.Roots;
    Gate.bControlBlocked=IsCastBlocked(Body,Source.Kind);
    Gate.bDisplacementBlocked=IsActionBlocked(Body,ELDCombatControlAction::Displacement);
    Reject=LDCombatSourceRules::ActiveGate(Gate,Content->Get().Policies);
    if (Reject!=ELDCombatReject::None) { return true; }
    FLDCombatBodyRef UnitTarget;
    FVector TargetFoot=FVector::ZeroVector;
    if (TargetDef.Mode==ELDDemoTargetMode::Unit)
    {
        if (!ResolveUnitTarget(*Record,Actor,Action.Target,UnitTarget,TargetFoot,Reject) || UnitTarget!=Action.TargetBody)
        { Reject=ELDCombatReject::TargetUnavailable; return true; }
        const FVector Foot=FootOf(Actor);
        double RangeCm=0;
        if (!CastRangeCm(Body,TargetDef,RangeCm,Error)) { return false; }
        if (FVector::Dist2D(Foot,TargetFoot)>RangeCm) { Reject=ELDCombatReject::OutOfRange; return true; }
        bool bFault=false;
        if (!LDCA::ClearSight(*Mode,*GetWorld(),Foot,TargetFoot,bFault))
        {
            if (bFault) { Error=TEXT("Ability release sight failed on a registered body."); return false; }
            Reject=ELDCombatReject::Blocked;
            return true;
        }
    }
    auto* Movement=Cast<ULDCharacterMovementComponent>(Actor.GetCharacterMovement());
    const auto* DashEffect=LDCA::FindEffect(*ValuesDef,ELDDemoEffectOp::Dash);
    if (DashEffect && (!Movement || !Movement->CanStartDash(Action.Direction,LDCA::DashProbeCm)))
    { Reject=ELDCombatReject::Blocked; return true; }
    FLDCombatAction DeliveryAction=Action;
    if (TargetDef.Mode==ELDDemoTargetMode::GroundPoint
        && (LDCA::FindEffect(*ValuesDef,ELDDemoEffectOp::GroundImpact) || LDCA::FindEffect(*ValuesDef,ELDDemoEffectOp::ContactTrap)))
    {
        const auto* Registry=GetWorld()->GetSubsystem<ULDMapRegistry>();
        const auto* Geometry=Registry ? Registry->GetGeometry() : nullptr;
        if (!Registry || !Registry->Enabled() || !Geometry || Geometry->Surfaces.IsEmpty())
        { Error=TEXT("A ground skill requires the registered ground surface."); return false; }
        FVector Point=ReleaseFoot+FVector(Action.TargetPoint.X-ReleaseFoot.X,Action.TargetPoint.Y-ReleaseFoot.Y,0)
            .GetClampedToMaxSize(LDCA::ToDouble(TargetDef.RangeCm));
        FVector Normal;
        Registry->GetTerrain().Ground(FVector2D(Point.X,Point.Y),Point.Z,Normal);
        FLDLegalGround Ground;
        ELDWorldQueryFailure Failure=ELDWorldQueryFailure::None;
        if (!Registry->QueryGround(Registry->Revision(),Point,Actor.GetCapsuleComponent()->GetCollisionShape(),
            FName(*Geometry->Surfaces[0].Id),Ground,Failure,Error))
        {
            if (Failure==ELDWorldQueryFailure::Blocked || Failure==ELDWorldQueryFailure::NotGrounded
                || Failure==ELDWorldQueryFailure::InvalidPosition)
            { Reject=ELDCombatReject::Blocked; Error.Reset(); return true; }
            return false;
        }
        DeliveryAction.TargetPoint=Ground.FootCm;
    }
    // Hit effects name their targets before anything is paid: a unit, a projectile, or every legal body in a self area.
    const FLDDemoEffectDefinition* DamageEffect=LDCA::FindEffect(*ValuesDef,ELDDemoEffectOp::Damage);
    const FLDDemoEffectDefinition* RootEffect=LDCA::FindEffect(*ValuesDef,ELDDemoEffectOp::Root);
    const FLDDemoEffectDefinition* SlowEffect=LDCA::FindEffect(*ValuesDef,ELDDemoEffectOp::Slow);
    const FLDDemoEffectDefinition* DisarmEffect=LDCA::FindEffect(*ValuesDef,ELDDemoEffectOp::Disarm);
    const FLDDemoEffectDefinition* StunEffect=LDCA::FindEffect(*ValuesDef,ELDDemoEffectOp::Stun);
    const FLDDemoEffectDefinition* AreaEffect=LDCA::FindEffect(*ValuesDef,ELDDemoEffectOp::SelfArea);
    // #147 build v7 payload of the hits and the release.
    const FLDDemoEffectDefinition* SilenceEffect=LDCA::FindEffect(*ValuesDef,ELDDemoEffectOp::Silence);
    const FLDDemoEffectDefinition* BreachEffect=LDCA::FindEffect(*ValuesDef,ELDDemoEffectOp::Breach);
    const FLDDemoEffectDefinition* BurnEffect=LDCA::FindEffect(*ValuesDef,ELDDemoEffectOp::Burn);
    // #198 elements and their riders on the same hits.
    const FLDDemoEffectDefinition* WetEffect=LDCA::FindEffect(*ValuesDef,ELDDemoEffectOp::Wet);
    const FLDDemoEffectDefinition* ElectrifyEffect=LDCA::FindEffect(*ValuesDef,ELDDemoEffectOp::Electrify);
    const FLDDemoEffectDefinition* GrievousEffect=LDCA::FindEffect(*ValuesDef,ELDDemoEffectOp::Grievous);
    const FLDDemoEffectDefinition* ChainEffect=LDCA::FindEffect(*ValuesDef,ELDDemoEffectOp::ChainBounce);
    const bool bHostile=DamageEffect || RootEffect || SlowEffect || DisarmEffect || StunEffect
        || SilenceEffect || BreachEffect || BurnEffect || WetEffect || ElectrifyEffect || GrievousEffect
        || LDCA::FindEffect(*ValuesDef,ELDDemoEffectOp::Knockback);
    if (ChainEffect && TargetDef.Mode!=ELDDemoTargetMode::Unit)
    { Error=TEXT("A chain needs a unit target to start from."); return false; }
    const bool bAreaMode=TargetDef.Mode==ELDDemoTargetMode::Self || TargetDef.Mode==ELDDemoTargetMode::DirectionDash;
    if (bHostile && bAreaMode && !AreaEffect && !LDCA::FindEffect(*ValuesDef,ELDDemoEffectOp::DashContact))
    {
        Error=TEXT("A self or dash action with hit effects has no self_area to name its targets.");
        return false;
    }
    TArray<FLDCombatBodyRef> AreaTargets;
    // The origin is the caster's foot at release, before any dash of the same action moves it.
    if (bHostile && bAreaMode && AreaEffect
        && !CollectAreaTargets(FootOf(Actor),LDCA::ToDouble(AreaEffect->RadiusCm),&Body,Body.Victim.EntityId,AreaTargets,Error))
    { return false; }
    const int64 BaseCooldownMs=bInnate ? 0 : ValuesDef->CooldownMs;
    // The duration is fixed at the actual release moment; later build changes never move this endpoint. The innate
    // dash issues its whole cooldown without haste.
    int64 DurationMs=bInnate ? Innate.CooldownMs : 0, ReadyAt=0;
    if ((!bInnate && !LDTactical::CooldownDurationMs(BaseCooldownMs,Body.AttributeProjection.Final.CooldownHasteP2,
            Content->Get().Formulas.HasteScaleP2,DurationMs,Error))
        || !LDMatchLifecycle::AddDurationMs(Now,DurationMs,ReadyAt,Error)) { return false; }
    // B17: read from the track's real ReadyAt before this release issues the next one (the wait restarts after it).
    int64 IdleAmpBp=0;
    if (!IdleReadyAmpBp(Body,*Track,Now,IdleAmpBp,Error)) { return false; }
    // Every fallible step is done: pay once and register the effects.
    Body.Resources.MpP2-=Gate.ManaCostP2;
    ++Body.Resources.Revision;
    Track->ReadyAtMs=ReadyAt;
    Track->LastDurationMs=DurationMs;
    const auto& Final=Body.AttributeProjection.Final;
    const FGuid EffectId=FGuid::NewGuid();
    Track->LastEffectId=EffectId;
    // The innate dash is no build ability: its hits still belong to the version, but it is not counted as a cast.
    LedgerEffectReleased(Body,EffectId,bInnate ? ELDBlueprintFact::Count : ELDBlueprintFact::AbilityCasts);
    auto CastLedger=MakeShared<FLDCombatCastLedger>();
    const FLDCombatBodyRef SelfRef{Body.Victim,Body.Identity};
    bool bDealsDamage=false;
    // Cleanses resolve before the release's other self effects.
    bool bTimedBonus=ApplyCleanses(Body,*ValuesDef,Now);
    for (const auto& Effect : ValuesDef->Effects)
    {
        switch (Effect.GetOp())
        {
        case ELDDemoEffectOp::Damage:
            bDealsDamage=true;
            break;
        case ELDDemoEffectOp::TimedStatBonus:
        {
            FLDCombatTimedBonus Bonus;
            Bonus.EffectId=EffectId;
            Bonus.SourceDefinition=Source.DefinitionId;
            Bonus.Stats=Effect.Stats;
            if (!LDMatchLifecycle::AddDurationMs(Now,Effect.DurationMs,Bonus.ExpiresAtMs,Error)) { return false; }
            LDTactical::ApplyTimedBonus(Body.TimedBonuses,Bonus);
            bTimedBonus=true;
            break;
        }
        case ELDDemoEffectOp::Shield:
        {
            const int64 MaxHP=Body.Resources.MaxHpP2;
            if (Effect.APRatioBp<0 || Final.APP2<0 || (Effect.APRatioBp>0 && Final.APP2>MAX_int64/Effect.APRatioBp)
                || Effect.MaxHPRatioBp<0 || MaxHP<0)
            { Error=TEXT("Shield scaling overflowed."); return false; }
            // Base + AP share + own max-HP share, through the single shield entry (ReplaceIfNotWeaker).
            const int64 Amount=Effect.BaseP2+Effect.APRatioBp*Final.APP2/10000+MaxHP/10000*Effect.MaxHPRatioBp+MaxHP%10000*Effect.MaxHPRatioBp/10000;
            if (!GrantShieldTo(Body,Amount,Effect.DurationMs,EffectId,Body.Victim.EntityId,Now,Error,Source)) { return false; }
            break;
        }
        case ELDDemoEffectOp::DispelRoot:
        case ELDDemoEffectOp::DispelSlow:
            // Resolved before this loop.
            break;
        case ELDDemoEffectOp::Dash:
        {
            const double Seconds=double(Effect.DurationMs)/1000.;
            const double Distance=LDCA::ToDouble(bInnate ? Innate.DistanceCm : Effect.DistanceCm);
            if (!Movement->StartDemoDash(Action.Direction,Distance,Seconds,Error)) { return false; }
            if (!LDMatchLifecycle::AddDurationMs(Now,Effect.DurationMs,Body.DashUntilMs,Error)) { return false; }
            Body.DashContact.Reset();
            // B12: a voluntary dash (D or an active's) that completes naturally arms DashComplete empowers.
            FLDCombatVoluntaryDash& Voluntary=Body.VoluntaryDash.Emplace();
            Voluntary.EffectId=EffectId;
            Voluntary.Source=Source;
            Voluntary.StartFootCm=ReleaseFoot;
            Voluntary.EndMs=Body.DashUntilMs;
            TOptional<FLDPassiveEffectRef> ContactDamage;
            TArray<FLDPassiveEffectRef> ContactRefunds, ContactKnocks;
            if (!FindTrigger(Body.AttributeProjection,ELDDemoEffectOp::DashContactDamage,ContactDamage,Error)
                || !WorkshopOn(Body.AttributeProjection,ELDDemoEffectOp::DashContactRefund,Source,ContactRefunds,Error)
                || !WorkshopOn(Body.AttributeProjection,ELDDemoEffectOp::ContactKnockback,Source,ContactKnocks,Error))
            { return false; }
            const bool bDamage=ContactDamage.IsSet();
            // #198 F C03/C04: installed on this dash (D or the active) only.
            const bool bRefund=!ContactRefunds.IsEmpty();
            const bool bKnock=!ContactKnocks.IsEmpty();
            if (bDamage || bRefund || bKnock || LDCA::FindEffect(*ValuesDef,ELDDemoEffectOp::DashContact))
            {
                Body.DashContact=MakeShared<FLDCombatDashContact>();
                Body.DashContact->EffectId=EffectId;
                Body.DashContact->Body=SelfRef;
                Body.DashContact->Source=Source;
                Body.DashContact->StartMs=Now;
                Body.DashContact->EndMs=Body.DashUntilMs;
                Body.DashContact->PaidManaP2=Gate.ManaCostP2;
                Body.DashContact->bCanDamage=bDamage;
                Body.DashContact->bCanRefund=bRefund;
                Body.DashContact->bCanKnock=bKnock;
                Body.DashContact->Direction=Action.Direction;
                Body.DashContact->ContactScaleBp=Terms.ContactScaleBp;
            }
            break;
        }
        case ELDDemoEffectOp::EmpowerBasic:
        {
            // S11/S17: the package is fixed at release (spec §10.2).
            FLDCombatEmpower Armed;
            if (!MakeEmpower(Body,Source,Effect,EffectId,Now,Armed,Error)) { return false; }
            // #147: several charges and the stats held while charges remain (they end with the last charge or the window).
            if (!Effect.Stats.IsEmpty())
            {
                FLDCombatTimedBonus Bonus;
                Bonus.EffectId=EffectId;
                Bonus.SourceDefinition=Source.DefinitionId;
                Bonus.Stats=Effect.Stats;
                Bonus.ExpiresAtMs=Armed.ExpiresAtMs;
                LDTactical::ApplyTimedBonus(Body.TimedBonuses,Bonus);
                Armed.bChargeBonus=true;
                bTimedBonus=true;
            }
            // A newer release of the same source replaces its unspent package; other sources' packages stay.
            LDCombatControl::ArmEmpower(Body.Empowers,Armed);
            break;
        }
        case ELDDemoEffectOp::Taunt:
            if (!ApplyTauntEffect(Actor,Body,Effect,EffectId,Source,Error)) { return false; }
            break;
        case ELDDemoEffectOp::Silence:
        case ELDDemoEffectOp::Breach:
        case ELDDemoEffectOp::Burn:
        case ELDDemoEffectOp::Wet:
        case ELDDemoEffectOp::Electrify:
        case ELDDemoEffectOp::Grievous:
        case ELDDemoEffectOp::ChainBounce:
            // Carried by the hit payload (and the S10 chain) below.
            break;
        case ELDDemoEffectOp::Brace:
            if (!ReleaseBrace(Body,Effect,Source,EffectId,Action.Token,Now,Error)) { return false; }
            break;
        case ELDDemoEffectOp::Root:
        case ELDDemoEffectOp::Slow:
        case ELDDemoEffectOp::Disarm:
        case ELDDemoEffectOp::Stun:
        case ELDDemoEffectOp::SelfArea:
        // #112: hit extras ride on the hits below (FillMechanicsTemplate); the zone follows them (ReleaseMechanics).
        case ELDDemoEffectOp::Knockback:
        case ELDDemoEffectOp::Zone:
        case ELDDemoEffectOp::ZoneSlow:
        case ELDDemoEffectOp::ZoneDamage:
        case ELDDemoEffectOp::ZoneBurn:
        case ELDDemoEffectOp::ZoneWet:
        case ELDDemoEffectOp::ZoneHaste:
        case ELDDemoEffectOp::ChannelCone:
        case ELDDemoEffectOp::GroundImpact:
        case ELDDemoEffectOp::ContactTrap:
        case ELDDemoEffectOp::DashContact:
        case ELDDemoEffectOp::AttackSlow:
            break;
        default:
            Error=FString::Printf(TEXT("Active effect opcode %u has no Combat handler."),Effect.Opcode);
            return false;
        }
    }
    if (bHostile)
    {
        // One hit payload per target, snapshotted at release; a skill's own delivery takes it, or the standard one does.
        FLDCombatHit Template;
        if (!BuildReleasePayload(Body,Source,Terms,ReleaseFoot,EffectId,Action.Token,IdleAmpBp,CastLedger,Template,Error)) { return false; }
        bool bHandled=false;
        if (!StartSkillDelivery(Body,Terms,DeliveryAction,Template,Now,bHandled,Error)) { return false; }
        if (!bHandled && !DeliverReleasePayload(Actor,Body,Terms,Action,Template,UnitTarget,TargetFoot,ReleaseFoot,AreaTargets,Now,Error))
        { return false; }
    }
    // A ground zone sits at the requested point, clamped to the authored reach.
    FVector ZonePoint=Action.TargetPoint;
    if (TargetDef.Mode==ELDDemoTargetMode::GroundPoint)
    {
        const FVector Offset(ZonePoint.X-ReleaseFoot.X,ZonePoint.Y-ReleaseFoot.Y,0.);
        const double Reach=LDCA::ToDouble(TargetDef.RangeCm);
        ZonePoint=Offset.Size2D()>Reach ? ReleaseFoot+Offset.GetSafeNormal2D()*Reach : FVector(ZonePoint.X,ZonePoint.Y,ReleaseFoot.Z);
    }
    if (!ReleaseMechanics(Body,Source,Terms,ReleaseFoot,ZonePoint,EffectId,Action.Token,IdleAmpBp,CastLedger,Error)) { return false; }
    // B17: every damage pulse of this release's zone carries its locked amplification.
    for (FLDCombatZone& Zone : Zones)
    {
        if (Zone.EffectId==EffectId && Zone.bDamage) { Zone.IdleReadyAmpBp=IdleAmpBp; Zone.PackageScaleBp=Terms.PackageScaleBp; }
    }
    if (bDealsDamage) { Body.LastCombatAtMs=Now; }
    // #198: an offensive release is a combat action of the build safe window; a self buff, shield or plain dash is not.
    if (LDCombatSourceRules::IsOffensive(*ValuesDef)) { MarkBuildUnsafe(Body.Victim.EntityId,Now); }
    if (bTimedBonus && !RefreshTemporary(Body,&Actor,Now,Error)) { return false; }
    ++Body.StateRevision;
    MarkViewChanged(Body.Victim.EntityId);
    return true;
}

bool ALDCombatDirector::BuildReleasePayload(const FLDBodyCombatState& Body, const FLDCombatSourceRef& Source,
    const FLDCombatActiveTerms& Terms, const FVector& ReleaseFoot, const FGuid& EffectId, uint64 Token, int64 IdleAmpBp,
    const TSharedPtr<FLDCombatCastLedger>& CastLedger, FLDCombatHit& Out, FString& Error) const
{
    const FLDDemoAbilityValues& Values=Terms.Values;
    const FLDDemoEffectDefinition* DamageEffect=LDCA::FindEffect(Values,ELDDemoEffectOp::Damage);
    const FLDDemoEffectDefinition* RootEffect=LDCA::FindEffect(Values,ELDDemoEffectOp::Root);
    const FLDDemoEffectDefinition* SlowEffect=LDCA::FindEffect(Values,ELDDemoEffectOp::Slow);
    const FLDDemoEffectDefinition* DisarmEffect=LDCA::FindEffect(Values,ELDDemoEffectOp::Disarm);
    const FLDDemoEffectDefinition* StunEffect=LDCA::FindEffect(Values,ELDDemoEffectOp::Stun);
    const FLDDemoEffectDefinition* SilenceEffect=LDCA::FindEffect(Values,ELDDemoEffectOp::Silence);
    const FLDDemoEffectDefinition* BreachEffect=LDCA::FindEffect(Values,ELDDemoEffectOp::Breach);
    const FLDDemoEffectDefinition* BurnEffect=LDCA::FindEffect(Values,ELDDemoEffectOp::Burn);
    const FLDDemoEffectDefinition* GrievousEffect=LDCA::FindEffect(Values,ELDDemoEffectOp::Grievous);
    const auto& Final=Body.AttributeProjection.Final;
    FLDCombatHit Template;
    Template.EffectId=EffectId;
    Template.Token=Token;
    Template.SourceBody={Body.Victim,Body.Identity};
    Template.SourceEntity=Body.Victim.EntityId;
    Template.CreditOwner=Body.Victim.EntityId;
    Template.Source=Source;
    Template.HitClass=ELDCombatHitClass::Ability;
    Template.CastLedger=CastLedger;
    Template.bDirectAbilityDamage=DamageEffect!=nullptr;
    // B17's locked amplification rides only on this release's native direct damage.
    Template.IdleReadyAmpBp=DamageEffect ? IdleAmpBp : 0;
    // #198 F: the mods' package multiplier of this native damage (M01/M03/M05); a dash's contact adds M08's.
    Template.PackageScaleBp=Terms.PackageScaleBp;
    if (LDCA::FindEffect(Values,ELDDemoEffectOp::DashContact))
    { Template.PackageScaleBp=Template.PackageScaleBp/10000*Terms.ContactScaleBp+Template.PackageScaleBp%10000*Terms.ContactScaleBp/10000; }
    Template.BaseP2=DamageEffect ? DamageEffect->BaseP2 : 0;
    Template.ADRatioBp=DamageEffect ? DamageEffect->ADRatioBp : 0;
    Template.APRatioBp=DamageEffect ? DamageEffect->APRatioBp : 0;
    Template.DamageType=DamageEffect ? DamageEffect->DamageType : ELDDemoDamageType::Physical;
    Template.SourceADP2=Final.ADP2;
    Template.SourceAPP2=Final.APP2;
    Template.MaxHPRatioBp=DamageEffect ? DamageEffect->MaxHPRatioBp : 0;
    Template.SourceMaxHPP2=Body.Resources.MaxHpP2;
    Template.RootDurationMs=RootEffect ? RootEffect->DurationMs : 0;
    Template.SlowBp=SlowEffect ? SlowEffect->RatioBp : 0;
    Template.SlowDurationMs=SlowEffect ? SlowEffect->DurationMs : 0;
    Template.DisarmDurationMs=DisarmEffect ? DisarmEffect->DurationMs : 0;
    Template.StunDurationMs=StunEffect ? StunEffect->DurationMs : 0;
    // #147 keyword payload: silence, shared breach and burn stacks (the burn snapshotted now).
    Template.SilenceDurationMs=SilenceEffect ? SilenceEffect->DurationMs : 0;
    Template.BreachStacks=BreachEffect ? BreachEffect->Count : 0;
    Template.BurnStacks=BurnEffect ? BurnEffect->Count : 0;
    // #198 elements of every hit of this release, with the caster's snapshot now; S21's cut of new heals and shields.
    Template.bApplyWet=LDCA::FindEffect(Values,ELDDemoEffectOp::Wet)!=nullptr;
    Template.bApplyElectric=LDCA::FindEffect(Values,ELDDemoEffectOp::Electrify)!=nullptr;
    if (!StampElementSnapshot(Body,Template,Error)) { return false; }
    Template.GrievousBp=GrievousEffect ? GrievousEffect->RatioBp : 0;
    Template.GrievousMs=GrievousEffect ? GrievousEffect->DurationMs : 0;
    // Abilities trigger no on-hit effects.
    if (!StampSourceTerms(Body,Template,Error) || !SnapshotOnHit(Body,0,Template,Error)) { return false; }
    FillMechanicsTemplate(Values,ReleaseFoot,Template);
    // #198 F C02 reads the push of this exit.
    if (Template.KnockbackCm>0) { Template.KnockbackSource=Source; }
    Out=MoveTemp(Template);
    return true;
}

bool ALDCombatDirector::DeliverReleasePayload(const ALDCharacter& Actor, const FLDBodyCombatState& Body,
    const FLDCombatActiveTerms& Terms, const FLDCombatAction& Action, const FLDCombatHit& Template, const FLDCombatBodyRef& UnitTarget,
    const FVector& TargetFoot, const FVector& ReleaseFoot, const TArray<FLDCombatBodyRef>& AreaTargets, int64 NowMs, FString& Error)
{
    const FLDDemoTargetDefinition& TargetDef=Terms.Target;
    const auto Deliver=[this,&Template](const FLDCombatBodyRef& Target)
    {
        FLDCombatHit Hit=Template;
        Hit.HitId=FGuid::NewGuid();
        Hit.Target=Target;
        PendingHits.Add(Hit);
    };
    if (TargetDef.Mode==ELDDemoTargetMode::DirectionProjectile)
    {
        FLDCombatProjectile Projectile;
        Projectile.EffectId=Template.EffectId;
        Projectile.FlightId=FGuid::NewGuid();
        Projectile.Token=Template.Token;
        Projectile.SourceBody=Template.SourceBody;
        Projectile.SourceEntity=Template.SourceEntity;
        Projectile.CreditOwner=Template.CreditOwner;
        Projectile.Source=Template.Source;
        Projectile.BaseP2=Template.BaseP2;
        Projectile.ADRatioBp=Template.ADRatioBp;
        Projectile.APRatioBp=Template.APRatioBp;
        Projectile.DamageType=Template.DamageType;
        Projectile.SourceADP2=Template.SourceADP2;
        Projectile.SourceAPP2=Template.SourceAPP2;
        Projectile.MaxHPRatioBp=Template.MaxHPRatioBp;
        Projectile.SourceMaxHPP2=Template.SourceMaxHPP2;
        Projectile.OriginCm=FootOf(Actor);
        Projectile.Direction=Action.Direction;
        Projectile.SpeedCmPerSec=LDCA::ToDouble(TargetDef.ProjectileSpeedCmPerSec);
        if (!CastRangeCm(Body,TargetDef,Projectile.RangeCm,Error)) { return false; }
        Projectile.RadiusCm=LDCA::ToDouble(TargetDef.ProjectileRadiusCm);
        Projectile.AdvancedThroughMs=NowMs;
        Projectile.SlowBp=Template.SlowBp;
        Projectile.SlowDurationMs=Template.SlowDurationMs;
        // #198 F M01: the first contact spreads its damage around the struck body.
        Projectile.SpreadRadiusCm=Terms.SpreadProjectile.IsSet() ? LDCA::ToDouble(Terms.SpreadProjectile->RadiusCm) : 0.;
        Projectile.Payload=Template;
        Projectiles.Add(Projectile);
    }
    else if (TargetDef.Mode==ELDDemoTargetMode::Unit)
    {
        Deliver(UnitTarget);
        // #198 F M01: a cone of the final range toward the primary target; controls stay on the primary target.
        if (Terms.SpreadUnit.IsSet() && !SpreadUnitHits(Body,Terms,ReleaseFoot,TargetFoot,UnitTarget,Template,Error)) { return false; }
        // #198 F M07: a compressed chain lands its whole budget on the first body (Terms) and bounces no further.
        const FLDDemoEffectDefinition* ChainEffect=LDCA::FindEffect(Terms.Values,ELDDemoEffectOp::ChainBounce);
        if (ChainEffect && !Terms.CompressChain.IsSet() && !StartChain(Template,UnitTarget,TargetFoot,*ChainEffect,NowMs,Error))
        { return false; }
    }
    else if (TargetDef.Mode==ELDDemoTargetMode::DirectionLine)
    {
        // #198 S09: every legal body in the band, pushed along the line (away from a point behind its own foot).
        TArray<TPair<FLDCombatBodyRef,FVector>> Line;
        if (!CollectLineTargets(ReleaseFoot,Action.Direction,LDCA::ToDouble(TargetDef.RangeCm),
            LDCA::ToDouble(TargetDef.LineHalfWidthCm),Body,Line,Error)) { return false; }
        const FVector Along=FVector(Action.Direction.X,Action.Direction.Y,0).GetSafeNormal();
        for (const TPair<FLDCombatBodyRef,FVector>& Struck : Line)
        {
            FLDCombatHit Hit=Template;
            Hit.HitId=FGuid::NewGuid();
            Hit.Target=Struck.Key;
            if (Hit.KnockbackCm>0) { Hit.bKnockbackFromAnchor=true; Hit.KnockbackAnchorCm=Struck.Value-Along*100.; }
            PendingHits.Add(MoveTemp(Hit));
        }
    }
    else
    {
        for (const FLDCombatBodyRef& Target : AreaTargets) { Deliver(Target); }
    }
    return true;
}

bool ALDCombatDirector::ApplyCleanses(FLDBodyCombatState& Body, const FLDDemoAbilityValues& Values, int64 NowMs)
{
    bool bSlowsChanged=false;
    for (const auto& Effect : Values.Effects)
    {
        if (Effect.GetOp()==ELDDemoEffectOp::DispelRoot)
        {
            if (LDTactical::DispelRoots(Body.Roots)>0) { ++Body.StateRevision; }
        }
        else if (Effect.GetOp()==ELDDemoEffectOp::DispelSlow && LDTactical::DispelSlows(Body.Slows,NowMs)>0)
        {
            ++Body.StateRevision;
            bSlowsChanged=true;
        }
    }
    return bSlowsChanged;
}

bool ALDCombatDirector::ReadCombatBody(const FLDCombatBodyRef& Ref, FLDCombatBodyRead& Out) const
{
    Out={};
    const auto* Mode=Core();
    ALDCharacter* Actor=nullptr;
    const auto* Body=FindBody(Ref,&Actor);
    if (!Mode || !Body || !Actor) { return false; }
    const int64 Now=Mode->GetAuthorityNowMs();
    Out.Body=Ref;
    Out.bPendingDeath=Body->bPendingDeath;
    Out.HPP2=Body->Resources.HpP2;
    Out.MaxHPP2=Body->Resources.MaxHpP2;
    Out.MPP2=Body->Resources.MpP2;
    Out.MaxMPP2=Body->Resources.MaxMpP2;
    Out.ShieldP2=Body->Resources.ShieldP2;
    Out.ResourceRevision=Body->Resources.Revision;
    Out.StateRevision=Body->StateRevision;
    Out.RootedUntilMs=LDTactical::ActiveRootUntilMs(Body->Roots,Now);
    Out.bActionInProgress=Body->Action.IsSet();
    Out.ActionEntry=Body->Action.IsSet() ? Body->Action->Entry : ELDCombatEntry::None;
    Out.ActionReleaseAtMs=Body->Action.IsSet() ? Body->Action->ReleaseAtMs : 0;
    const auto* Record=FindRecord(Ref.Victim.EntityId);
    Out.NextAttackAtMs=Ref.Victim.Kind==ELDVictimKind::Contender ? (Record ? Record->NextAttackAtMs : 0) : Body->MonsterNextAttackAtMs;
    Out.AttackRangeCm=LDCA::ToDouble(Body->AttributeProjection.Final.AttackRangeCm);
    Out.bAttackAllowed=!Body->bPendingDeath && !IsActionBlocked(*Body,ELDCombatControlAction::Basic);
    Out.bMovementAllowed=!Body->bPendingDeath && !IsActionBlocked(*Body,ELDCombatControlAction::Move);
    Out.bBasicAttackReady=Out.bAttackAllowed && !Out.bActionInProgress && Now>=Out.NextAttackAtMs;
    return true;
}

bool ALDCombatDirector::IsMoveBlocked(const ALDCharacter& Actor) const
{
    const auto* Body=Bodies.Find(const_cast<ALDCharacter*>(&Actor));
    return Body && IsActionBlocked(*Body,ELDCombatControlAction::Move);
}

bool ALDCombatDirector::IsActionBlocked(const FLDBodyCombatState& Body, ELDCombatControlAction Action) const
{
    const auto* Mode=Core();
    const auto* Content=Catalog();
    if (!Mode || !Content) { return false; }
    const int64 Now=Mode->GetAuthorityNowMs();
    // A knockback blocks every voluntary action while its displacement lasts (#112 R4).
    return LDCombatControl::Blocks(Body.Roots,Body.Disarms,Body.Stuns,Now,Content->Get().Policies,Action,&Body.Silences)
        || LDControl::TauntBlocks(Body.Taunt,Now,Action)
        || Body.Knockbacks.ContainsByPredicate([Now](const FLDCombatRoot& Row) { return Row.ExpiresAtMs>Now; });
}

bool ALDCombatDirector::ReadMoveSpeed(const ALDCharacter& Actor, float& OutCmPerSec) const
{
    OutCmPerSec=0;
    const auto* Body=Bodies.Find(const_cast<ALDCharacter*>(&Actor));
    if (!Body || Body->bPendingDeath) { return false; }
    const auto* Mode=Core();
    double Speed=0;
    if (!Mode || !EffectiveMoveSpeed(*Body,Speed) || !FMath::IsFinite(Speed) || Speed<=0 || Speed>MAX_flt)
    { return false; }
    OutCmPerSec=float(Speed);
    return true;
}
