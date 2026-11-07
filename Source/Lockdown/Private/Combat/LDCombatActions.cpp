#include "Combat/LDCombatDirector.h"
#include "Core/LDLog.h"
#include "Combat/LDCombatSourceRules.h"
#include "Navigation/LDOrderRules.h"
#include "Combat/LDTacticalRules.h"
#include "Combat/LDControlRules.h"
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
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
        return Entry==ELDCombatEntry::Q || Entry==ELDCombatEntry::W || Entry==ELDCombatEntry::Dash;
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

    const FLDDemoEffectDefinition* FindEffect(const FLDDemoRankDefinition& Rank, ELDDemoEffectOp Op)
    { return Rank.Effects.FindByPredicate([Op](const FLDDemoEffectDefinition& Effect) { return Effect.GetOp()==Op; }); }

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
        Existing->Receipt=Receipt;
        return;
    }
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
    for (auto& Pair : Bodies) { AbandonAction(Pair.Value,ELDCombatReject::MatchClosed); }
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
    if (Mode->IsOpeningCountdown(Caller.Match) && !LDOrderRules::CountdownAdmitsAction(Request.Entry))
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
    if (!TargetBody || !TargetActor || TargetBody->bPendingDeath || OutBody.Victim.EntityId==Record.Entity) { return false; }
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
    Body->Action=Action;
    Body->BasicTiming=Timing.Value.GetValue();
    Body->LastCombatAtMs=Now;
    ++Body->StateRevision;
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
    if (Record.Kind==ELDVictimKind::Contender && Request.Entry!=ELDCombatEntry::Dash)
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
    uint8 Rank=0;
    const FLDDemoAbilityDefinition* Ability=nullptr;
    if (!ResolveEntry(Record,Request.Entry,Source,Rank,Ability)) { return Refuse(ELDCombatReject::SourceUnavailable); }
    if (Request.ExpectedSourceRevision!=Record.SourceRevision) { return Refuse(ELDCombatReject::StaleSource); }
    const FLDDemoRankDefinition* RankDef=nullptr;
    const FLDDemoTargetDefinition* TargetDef=nullptr;
    int64 WindupMs=0;
    if (!ActiveOf(Source,Rank,Record.Build.Level,Ability,TargetDef,RankDef,WindupMs)) { return Refuse(ELDCombatReject::SourceUnavailable); }
    const auto* Track=TrackFor(Record,Request.Entry);
    if (!Track) { Error=TEXT("A held active source has no persistent cooldown track."); return false; }
    FLDActiveGateInput Gate;
    Gate.NowMs=Now;
    Gate.ReadyAtMs=Track->ReadyAtMs;
    Gate.MpP2=Body->Resources.MpP2;
    Gate.ManaCostP2=RankDef->ManaCostP2;
    // Only an action that displaces its caster is a dash for the root matrix (a ground zone is not).
    Gate.bDash=LDCombatSourceRules::DisplacesCaster(*RankDef);
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
    Action.Rank=Rank;
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
    // A started swing always lands on its committed life (2026-11-07 user decision, LDCombatSourceRules::BasicReleaseGate):
    // range, sight and observation gate only the start (StartNavAttack / camp admission). The release re-checks only the
    // attacker's own control and that the exact target life still stands; the windup already spent the cadence.
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
    Outcome=LDCombatSourceRules::BasicReleaseGate(Gate);
    if (Outcome!=ELDCombatReject::None)
    {
        UE_LOG(LogLDCombat,Log,TEXT("LD_COMBAT_BASIC_RELEASE token=%llu outcome=refused reason=%u"),Action.Token,uint32(Outcome));
        FinishAction(Body,Outcome,false);
        return true;
    }
    UE_LOG(LogLDCombat,Verbose,TEXT("LD_COMBAT_BASIC_RELEASE token=%llu outcome=hit distance_cm=%.0f"),Action.Token,
        FVector::Dist2D(FootOf(Actor),FootOf(*TargetActor)));
    const auto& Final=Body.AttributeProjection.Final;
    FLDCombatHit Hit;
    Hit.HitId=FGuid::NewGuid();
    Hit.EffectId=FGuid::NewGuid();
    Hit.Token=Action.Token;
    Hit.Target=TargetRef;
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
    const int64 Now=Mode->GetAuthorityNowMs();
    // An empowered basic joins the primary hit before its critical roll, so it takes crit, vamp, cleave and on-hit.
    const bool bWasEmpowered=Body.Empower.IsSet();
    const TOptional<FLDCombatEmpower> Bonus=LDCombatControl::ConsumeEmpower(Body.Empower,Now);
    if (Bonus.IsSet())
    {
        Hit.BaseP2+=Bonus->BaseP2;
        Hit.ADRatioBp+=Bonus->ADRatioBp;
        // #147: an empowered basic may carry a cleave of its own.
        if (Bonus->CleaveRatioBp>0 && Bonus->CleaveRadiusCm>0)
        {
            Hit.bCleave=true;
            Hit.CleaveRatioBp=FMath::Max(Hit.CleaveRatioBp,Bonus->CleaveRatioBp);
            Hit.CleaveRadiusCm=FMath::Max(Hit.CleaveRadiusCm,Bonus->CleaveRadiusCm);
        }
        // The stats held while charges remain end with the last charge.
        if (Bonus->Charges==0 && Bonus->bChargeBonus
            && Body.TimedBonuses.RemoveAll([&Bonus](const FLDCombatTimedBonus& Row) { return Row.EffectId==Bonus->EffectId; })>0
            && !RefreshTemporary(Body,&Actor,Now,Error))
        { return false; }
    }
    // One critical sample per AttackId, at the successful release only.
    Hit.bCritical=Final.CritChanceBp>0 && CritRandom.RandRange(0,9999)<Final.CritChanceBp;
    PendingHits.Add(Hit);
    if (bWasEmpowered)
    {
        ++Body.StateRevision;
        MarkViewChanged(Body.Victim.EntityId);
    }
    if (!OnBasicLanded(Body,Hit,Error)) { return false; }
    FinishAction(Body,ELDCombatReject::None,true);
    return true;
}

bool ALDCombatDirector::OnBasicLanded(FLDBodyCombatState& Body, const FLDCombatHit& Primary, FString& Error)
{
    const ALDGameMode* Mode=Core();
    if (!Mode) { Error=TEXT("A landed basic requires the current core."); return false; }
    const int64 Now=Mode->GetAuthorityNowMs();
    const FGuid& Entity=Body.Victim.EntityId;
    // #147 passive_iced_americano advanced: every landed basic cuts both learned actives.
    {
        TArray<FLDPassiveEffectRef> Refunds;
        if (!CollectPassiveEffects(Body.AttributeProjection,ELDDemoEffectOp::CooldownRefund,Refunds,Error)) { return false; }
        for (const FLDPassiveEffectRef& Ref : Refunds)
        {
            if (Ref.Effect->Trigger!=ELDDemoEffectTrigger::BasicHit) { continue; }
            ReduceCooldown(Entity,ELDCombatEntry::Q,Ref.Effect->AmountMs);
            ReduceCooldown(Entity,ELDCombatEntry::W,Ref.Effect->AmountMs);
        }
    }
    // item_edge_blade: every Count-th landed basic adds its own physical hit (added damage: no crit, no on-hit).
    TArray<FLDPassiveEffectRef> Counters;
    if (!CollectPassiveEffects(Body.AttributeProjection,ELDDemoEffectOp::NthBasicBonus,Counters,Error)) { return false; }
    if (!Counters.IsEmpty()) { ++Body.LandedBasics; }
    for (const FLDPassiveEffectRef& Ref : Counters)
    {
        if (Ref.Effect->Count<=0 || Body.LandedBasics%Ref.Effect->Count!=0) { continue; }
        FLDCombatHit Extra=AddedHit(Primary);
        Extra.BaseP2=Ref.Effect->BaseP2;
        Extra.DamageType=Ref.Effect->DamageType;
        PendingHits.Add(Extra);
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
    uint8 Rank=0;
    const FLDDemoAbilityDefinition* Ability=nullptr;
    if (!ResolveEntry(*Record,Action.Entry,Source,Rank,Ability) || Source!=Action.Source || Rank!=Action.Rank)
    { Reject=ELDCombatReject::SourceUnavailable; return true; }
    const FLDDemoRankDefinition* RankDef=nullptr;
    const FLDDemoTargetDefinition* TargetPtr=nullptr;
    int64 WindupMs=0;
    auto* Track=TrackFor(*Record,Action.Entry);
    if (!ActiveOf(Source,Rank,Record->Build.Level,Ability,TargetPtr,RankDef,WindupMs) || !Track)
    { Reject=ELDCombatReject::SourceUnavailable; return true; }
    const FLDDemoTargetDefinition& TargetDef=*TargetPtr;
    // The caster's foot before any displacement of this release (areas, zones and pushes centre here).
    const FVector ReleaseFoot=FootOf(Actor);
    // The innate dash takes its distance and cooldown from the equipment passives held at this release (D20).
    const bool bInnate=Source.Kind==ELDCombatSourceKind::Innate;
    FLDInnateDashTerms Innate;
    if (bInnate && !InnateDashFor(*Record,&Body,Innate,Error)) { return false; }
    // A stun or taunt that landed during the windup ends it (normally already abandoned at the hit).
    FLDActiveGateInput Gate;
    Gate.NowMs=Now;
    Gate.ReadyAtMs=Track->ReadyAtMs;
    Gate.MpP2=Body.Resources.MpP2;
    Gate.ManaCostP2=bInnate ? Innate.ManaCostP2 : RankDef->ManaCostP2;
    // Only an action that displaces its caster is a dash for the root matrix (a ground zone is not).
    Gate.bDash=LDCombatSourceRules::DisplacesCaster(*RankDef);
    Gate.Roots=&Body.Roots;
    Gate.bControlBlocked=IsCastBlocked(Body,Source.Kind);
    Gate.bDisplacementBlocked=IsActionBlocked(Body,ELDCombatControlAction::Displacement);
    Reject=LDCombatSourceRules::ActiveGate(Gate,Content->Get().Policies);
    if (Reject!=ELDCombatReject::None) { return true; }
    FLDCombatBodyRef UnitTarget;
    if (TargetDef.Mode==ELDDemoTargetMode::Unit)
    {
        FVector TargetFoot;
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
    const auto* DashEffect=LDCA::FindEffect(*RankDef,ELDDemoEffectOp::Dash);
    if (DashEffect && (!Movement || !Movement->CanStartDash(Action.Direction,LDCA::DashProbeCm)))
    { Reject=ELDCombatReject::Blocked; return true; }
    // Hit effects name their targets before anything is paid: a unit, a projectile, or every legal body in a self area.
    const FLDDemoEffectDefinition* DamageEffect=LDCA::FindEffect(*RankDef,ELDDemoEffectOp::Damage);
    const FLDDemoEffectDefinition* RootEffect=LDCA::FindEffect(*RankDef,ELDDemoEffectOp::Root);
    const FLDDemoEffectDefinition* SlowEffect=LDCA::FindEffect(*RankDef,ELDDemoEffectOp::Slow);
    const FLDDemoEffectDefinition* DisarmEffect=LDCA::FindEffect(*RankDef,ELDDemoEffectOp::Disarm);
    const FLDDemoEffectDefinition* StunEffect=LDCA::FindEffect(*RankDef,ELDDemoEffectOp::Stun);
    const FLDDemoEffectDefinition* AreaEffect=LDCA::FindEffect(*RankDef,ELDDemoEffectOp::SelfArea);
    // #147 build v7 payload of the hits and the release.
    const FLDDemoEffectDefinition* SilenceEffect=LDCA::FindEffect(*RankDef,ELDDemoEffectOp::Silence);
    const FLDDemoEffectDefinition* BreachEffect=LDCA::FindEffect(*RankDef,ELDDemoEffectOp::Breach);
    const FLDDemoEffectDefinition* BurnEffect=LDCA::FindEffect(*RankDef,ELDDemoEffectOp::Burn);
    const FLDDemoEffectDefinition* ExecuteEffect=LDCA::FindEffect(*RankDef,ELDDemoEffectOp::Execute);
    const FLDDemoEffectDefinition* EchoEffect=LDCA::FindEffect(*RankDef,ELDDemoEffectOp::Echo);
    const FLDDemoEffectDefinition* TauntShieldEffect=LDCA::FindEffect(*RankDef,ELDDemoEffectOp::TauntShield);
    const bool bHostile=DamageEffect || RootEffect || SlowEffect || DisarmEffect || StunEffect
        || SilenceEffect || BreachEffect || BurnEffect || LDCA::FindEffect(*RankDef,ELDDemoEffectOp::Knockback);
    const bool bAreaMode=TargetDef.Mode==ELDDemoTargetMode::Self || TargetDef.Mode==ELDDemoTargetMode::DirectionDash;
    if (bHostile && bAreaMode && !AreaEffect)
    {
        Error=TEXT("A self or dash action with hit effects has no self_area to name its targets.");
        return false;
    }
    TArray<FLDCombatBodyRef> AreaTargets;
    // The origin is the caster's foot at release, before any dash of the same action moves it.
    if (bHostile && bAreaMode
        && !CollectAreaTargets(FootOf(Actor),LDCA::ToDouble(AreaEffect->RadiusCm),&Body,Body.Victim.EntityId,AreaTargets,Error))
    { return false; }
    // item_phase_anchor: a character active with a dash takes the skill boost held at this release (base
    // cooldown x ratio before haste, distance x (1 + bonus)); the innate dash has its own terms.
    int64 SkillCooldownRatioBp=10000, SkillDistanceBonusBp=0;
    if (!bInnate && LDCombatSourceRules::DisplacesCaster(*RankDef) && !LDCombatSourceRules::SkillDisplacementBoost(*Content,
        Body.AttributeProjection.EquipmentPassives,SkillCooldownRatioBp,SkillDistanceBonusBp,Error))
    { return false; }
    const int64 BaseCooldownMs=bInnate ? 0 : (RankDef->CooldownMs/10000*SkillCooldownRatioBp
        +(RankDef->CooldownMs%10000*SkillCooldownRatioBp+9999)/10000);
    // The duration is fixed at the actual release moment; later build changes never move this endpoint. The innate
    // dash issues its whole (possibly boosted) cooldown without haste.
    int64 DurationMs=bInnate ? Innate.CooldownMs : 0, ReadyAt=0;
    if ((!bInnate && !LDTactical::CooldownDurationMs(BaseCooldownMs,Body.AttributeProjection.Final.CooldownHasteP2,
            Content->Get().Formulas.HasteScaleP2,DurationMs,Error))
        || !LDMatchLifecycle::AddDurationMs(Now,DurationMs,ReadyAt,Error)) { return false; }
    // Every fallible step is done: pay once and register the effects.
    Body.Resources.MpP2-=Gate.ManaCostP2;
    ++Body.Resources.Revision;
    Track->ReadyAtMs=ReadyAt;
    Track->LastDurationMs=DurationMs;
    const auto& Final=Body.AttributeProjection.Final;
    const FGuid EffectId=FGuid::NewGuid();
    const FLDCombatBodyRef SelfRef{Body.Victim,Body.Identity};
    bool bDealsDamage=false;
    // Cleanses resolve before the release's other self effects.
    bool bTimedBonus=ApplyCleanses(Body,*RankDef,Now);
    for (const auto& Effect : RankDef->Effects)
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
            if (!GrantShieldTo(Body,Amount,Effect.DurationMs,EffectId,Body.Victim.EntityId,Now,Error)) { return false; }
            break;
        }
        case ELDDemoEffectOp::DispelRoot:
        case ELDDemoEffectOp::DispelSlow:
        case ELDDemoEffectOp::DispelDisarm:
            // Resolved before this loop.
            break;
        case ELDDemoEffectOp::Dash:
        {
            const double Seconds=double(Effect.DurationMs)/1000.;
            const double Distance=LDCA::ToDouble(bInnate ? Innate.DistanceCm : Effect.DistanceCm)*double(10000+SkillDistanceBonusBp)/10000.;
            if (!Movement->StartDemoDash(Action.Direction,Distance,Seconds,Error)) { return false; }
            if (!LDMatchLifecycle::AddDurationMs(Now,Effect.DurationMs,Body.DashUntilMs,Error)) { return false; }
            break;
        }
        case ELDDemoEffectOp::EmpowerBasic:
        {
            FLDCombatEmpower Armed;
            Armed.EffectId=EffectId;
            Armed.SourceDefinitionId=Source.DefinitionId;
            Armed.BaseP2=Effect.BaseP2;
            Armed.ADRatioBp=Effect.ADRatioBp;
            Armed.APRatioBp=Effect.APRatioBp;
            Armed.DamageType=Effect.DamageType;
            if (!LDMatchLifecycle::AddDurationMs(Now,Effect.DurationMs,Armed.ExpiresAtMs,Error)) { return false; }
            // #147: several charges, the cleave they carry and the stats held while charges remain (they end with the
            // last charge or the window).
            Armed.Charges=FMath::Max(Effect.Count,1);
            Armed.CleaveRatioBp=Effect.SecondaryRatioBp;
            Armed.CleaveRadiusCm=LDCA::ToDouble(Effect.RadiusCm);
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
            // A newer release replaces an unspent one; it never stacks.
            Body.Empower=Armed;
            break;
        }
        case ELDDemoEffectOp::PathSlow:
        {
            // active_lunge Lv3: bodies along the travelled segment are slowed when the dash ends.
            FLDCombatDashSweep Sweep;
            Sweep.Body=SelfRef;
            Sweep.EffectId=EffectId;
            Sweep.Token=Action.Token;
            Sweep.Source=Source;
            Sweep.StartFootCm=ReleaseFoot;
            Sweep.SlowBp=Effect.RatioBp;
            Sweep.SlowMs=Effect.DurationMs;
            Sweep.HalfWidthCm=LDCA::ToDouble(Effect.RadiusCm);
            const auto* Move=LDCA::FindEffect(*RankDef,ELDDemoEffectOp::Dash);
            if (!Move || !LDMatchLifecycle::AddDurationMs(Now,Move->DurationMs,Sweep.EndAtMs,Error))
            { if (Error.IsEmpty()) { Error=TEXT("A path slow needs its dash."); } return false; }
            DashSweeps.Add(Sweep);
            break;
        }
        case ELDDemoEffectOp::Taunt:
        {
            int32 Taunted=0;
            if (!ApplyTauntEffect(Actor,Body,Effect,EffectId,Error,&Taunted)) { return false; }
            // #147: a shield sized by every contender this release taunted.
            if (TauntShieldEffect && Taunted>0)
            {
                const int64 MaxHP=Body.Resources.MaxHpP2;
                // The released rank row is already grown for the caster's level (#147, ActiveOf).
                const int64 Each=TauntShieldEffect->BaseP2
                    +MaxHP/10000*TauntShieldEffect->MaxHPRatioBp+MaxHP%10000*TauntShieldEffect->MaxHPRatioBp/10000;
                if (Each>MAX_int64/Taunted) { Error=TEXT("Taunt shield overflowed."); return false; }
                if (!GrantShieldTo(Body,Each*Taunted,TauntShieldEffect->DurationMs,EffectId,Body.Victim.EntityId,Now,Error)) { return false; }
            }
            break;
        }
        case ELDDemoEffectOp::TauntShield:
            // Granted with its taunt above.
            break;
        case ELDDemoEffectOp::Silence:
        case ELDDemoEffectOp::Breach:
        case ELDDemoEffectOp::Burn:
        case ELDDemoEffectOp::Execute:
        case ELDDemoEffectOp::Pierce:
        case ELDDemoEffectOp::Echo:
            // Carried by the hit payload below (the echo is scheduled after its first blast).
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
        case ELDDemoEffectOp::ZoneRoot:
        case ELDDemoEffectOp::ZoneDamage:
        case ELDDemoEffectOp::ZoneBurn:
            break;
        default:
            Error=FString::Printf(TEXT("Active effect opcode %u has no Combat handler."),Effect.Opcode);
            return false;
        }
    }
    if (bHostile)
    {
        // One hit payload per target, snapshotted at release: damage terms, source AD/AP, controls and amplification.
        FLDCombatHit Template;
        Template.EffectId=EffectId;
        Template.Token=Action.Token;
        Template.SourceBody=SelfRef;
        Template.SourceEntity=Body.Victim.EntityId;
        Template.CreditOwner=Body.Victim.EntityId;
        Template.Source=Source;
        Template.HitClass=ELDCombatHitClass::Ability;
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
        // #147 keyword payload: silence, shared breach and burn stacks (the burn snapshotted now), execute, the root that
        // becomes a stun on a slowed body, and a piercing flight.
        Template.SilenceDurationMs=SilenceEffect ? SilenceEffect->DurationMs : 0;
        Template.BreachStacks=BreachEffect ? BreachEffect->Count : 0;
        Template.BurnStacks=BurnEffect ? BurnEffect->Count : 0;
        if (BurnEffect)
        {
            FLDCombatBurnLayer Layer;
            if (!BurnLayerOf(Body,Source,Layer,Error)) { return false; }
            Template.BurnLevel=Layer.Level;
            Template.BurnAPP2=Layer.SourceAPP2;
        }
        Template.ExecuteBp=ExecuteEffect ? ExecuteEffect->RatioBp : 0;
        Template.ExecuteBelowBp=ExecuteEffect ? ExecuteEffect->SecondaryRatioBp : 0;
        Template.SlowedStunMs=RootEffect ? RootEffect->SecondaryDurationMs : 0;
        Template.bPierce=LDCA::FindEffect(*RankDef,ELDDemoEffectOp::Pierce)!=nullptr;
        // Ability criticals: passive_lucky_star advanced at a share of the crit chance for its listed abilities.
        TArray<FLDPassiveEffectRef> Crits;
        if (!CollectPassiveEffects(Body.AttributeProjection,ELDDemoEffectOp::AbilityCrit,Crits,Error)) { return false; }
        for (const FLDPassiveEffectRef& Ref : Crits)
        {
            if (Ref.Effect->CritMode==ELDDemoCritMode::Chance && Ref.Effect->AbilityIds.Contains(Source.DefinitionId.ToString()))
            { Template.AbilityCritChanceBp=FMath::Max(Template.AbilityCritChanceBp,Final.CritChanceBp*Ref.Effect->RatioBp/10000); }
        }
        // Abilities trigger no on-hit effects.
        if (!StampSourceTerms(Body,Template,Error) || !SnapshotOnHit(Body,0,Template,Error)) { return false; }
        FillMechanicsTemplate(*RankDef,ReleaseFoot,Template);
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
            Projectile.EffectId=EffectId;
            Projectile.FlightId=FGuid::NewGuid();
            Projectile.Token=Action.Token;
            Projectile.SourceBody=SelfRef;
            Projectile.SourceEntity=Body.Victim.EntityId;
            Projectile.CreditOwner=Body.Victim.EntityId;
            Projectile.Source=Source;
            Projectile.BaseP2=Template.BaseP2;
            Projectile.ADRatioBp=Template.ADRatioBp;
            Projectile.APRatioBp=Template.APRatioBp;
            Projectile.DamageType=Template.DamageType;
            Projectile.SourceADP2=Final.ADP2;
            Projectile.SourceAPP2=Final.APP2;
            Projectile.MaxHPRatioBp=Template.MaxHPRatioBp;
            Projectile.SourceMaxHPP2=Template.SourceMaxHPP2;
            Projectile.OriginCm=FootOf(Actor);
            Projectile.Direction=Action.Direction;
            Projectile.SpeedCmPerSec=LDCA::ToDouble(TargetDef.ProjectileSpeedCmPerSec);
            if (!CastRangeCm(Body,TargetDef,Projectile.RangeCm,Error)) { return false; }
            Projectile.RadiusCm=LDCA::ToDouble(TargetDef.ProjectileRadiusCm);
            Projectile.AdvancedThroughMs=Now;
            Projectile.SlowBp=Template.SlowBp;
            Projectile.SlowDurationMs=Template.SlowDurationMs;
            Projectile.Payload=Template;
            Projectiles.Add(Projectile);
        }
        else if (TargetDef.Mode==ELDDemoTargetMode::Unit) { Deliver(UnitTarget); }
        else
        {
            for (const FLDCombatBodyRef& Target : AreaTargets) { Deliver(Target); }
            // #147: a second blast later around the same living body, its damage scaled (other payload unchanged).
            if (EchoEffect && AreaEffect)
            {
                FLDCombatEcho& Echo=Echoes.AddDefaulted_GetRef();
                Echo.Body=SelfRef;
                Echo.RadiusCm=LDCA::ToDouble(AreaEffect->RadiusCm);
                Echo.Template=EchoHit(Template,EchoEffect->RatioBp);
                if (!LDMatchLifecycle::AddDurationMs(Now,EchoEffect->DelayMs,Echo.AtMs,Error)) { return false; }
            }
        }
    }
    // A ground zone sits at the requested point, clamped to the authored reach.
    FVector ZonePoint=Action.TargetPoint;
    if (TargetDef.Mode==ELDDemoTargetMode::GroundPoint)
    {
        const FVector Offset(ZonePoint.X-ReleaseFoot.X,ZonePoint.Y-ReleaseFoot.Y,0.);
        const double Reach=LDCA::ToDouble(TargetDef.RangeCm);
        ZonePoint=Offset.Size2D()>Reach ? ReleaseFoot+Offset.GetSafeNormal2D()*Reach : FVector(ZonePoint.X,ZonePoint.Y,ReleaseFoot.Z);
    }
    if (!ReleaseMechanics(Body,Source,*RankDef,ReleaseFoot,ZonePoint,Error)) { return false; }
    // #147 passive_special_forces: a released skill quickens the caster for a while.
    if (Source.Kind==ELDCombatSourceKind::Learned)
    {
        FLDCombatSourceRef FrenzyRef;
        if (const FLDDemoEffectDefinition* Frenzy=FindTrigger(Body.AttributeProjection,ELDDemoEffectOp::CastFrenzy,&FrenzyRef))
        {
            FLDCombatTimedBonus Bonus;
            Bonus.EffectId=FGuid::NewGuid();
            Bonus.SourceDefinition=FrenzyRef.DefinitionId;
            Bonus.Stats=Frenzy->Stats;
            if (!LDMatchLifecycle::AddDurationMs(Now,Frenzy->DurationMs,Bonus.ExpiresAtMs,Error)) { return false; }
            LDTactical::ApplyTimedBonus(Body.TimedBonuses,Bonus);
            bTimedBonus=true;
        }
    }
    if (bDealsDamage) { Body.LastCombatAtMs=Now; }
    if (bTimedBonus && !RefreshTemporary(Body,&Actor,Now,Error)) { return false; }
    ++Body.StateRevision;
    MarkViewChanged(Body.Victim.EntityId);
    return true;
}

bool ALDCombatDirector::ApplyCleanses(FLDBodyCombatState& Body, const FLDDemoRankDefinition& Rank, int64 NowMs)
{
    bool bSlowsChanged=false;
    for (const auto& Effect : Rank.Effects)
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
        // #147: a disarm cleanse (a silenced or stunned body never reaches this release).
        else if (Effect.GetOp()==ELDDemoEffectOp::DispelDisarm && !Body.Disarms.IsEmpty())
        {
            Body.Disarms.Reset();
            ++Body.StateRevision;
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
