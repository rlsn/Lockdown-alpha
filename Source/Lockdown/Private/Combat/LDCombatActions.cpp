#include "Combat/LDCombatDirector.h"
#include "Combat/LDCombatCues.h"
#include "Combat/LDGroundLanding.h"
#include "Core/LDLog.h"
#include "Combat/LDCombatSourceRules.h"
#include "Navigation/LDOrderRules.h"
#include "Navigation/LDNavigationComponent.h"
#include "Combat/LDTacticalRules.h"
#include "Combat/LDControlRules.h"
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDSkillGrowth.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Region/LDMapRegistry.h"
#include "Vision/LDVisionDirector.h"
#include "World/LDCampDirector.h"
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
    // A contender's basic windup cut before its release moment (a dash, a skill, S, control, a lost target) swung
    // nothing, so it gives back the cadence it committed at its start. A swing refused at its release spent it.
    const auto* Mode=Core();
    if (!bReleased && Body.Action->Kind==ELDCombatActionKind::Basic && !Body.Action->bMonsterSpecial && Mode
        && Mode->GetAuthorityNowMs()<Body.Action->ReleaseAtMs)
    {
        if (auto* Record=FindRecord(Body.Victim.EntityId); Record && Record->Kind==ELDVictimKind::Contender)
        { Record->NextAttackAtMs=FMath::Min(Record->NextAttackAtMs,Body.Action->StartedAtMs); }
    }
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

// Every queued Direct request (held behind an own windup included) ends with a terminal receipt, never silently.
void ALDCombatDirector::ClosePending(ELDCombatReject Reason)
{
    for (auto& Pair : Records) { ClosePending(Pair.Value,Reason); }
}

void ALDCombatDirector::ClosePending(FLDCombatEntityRecord& Record, ELDCombatReject Reason)
{
    {
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
            Closed.Reject=Reason;
            StoreReceipt(Record,Request,Closed);
            if (const auto* Stored=Record.Recent.FindByPredicate([&Request](const FLDCombatDirectEntry& Entry)
                { return Entry.Request.DirectSequence==Request.DirectSequence; }))
            { SendReceipt(Record,Stored->Receipt); }
        }
    }
    ClearPreInputQueue(Record.Entity,Reason,
        Reason==ELDCombatReject::Dead ? ELDOrderFailure::Dead
        : Reason==ELDCombatReject::MatchClosed ? ELDOrderFailure::MatchEnded
        : ELDOrderFailure::Stopped);
}

void ALDCombatDirector::EndMatchActions()
{
    Channels.Reset();
    RemoveBags([](const FLDCombatBag&) { return true; });
    GroundEffects.Reset();
    MonsterStrikes.Reset();
    ContactMotion.Reset();
    ClosePending(ELDCombatReject::MatchClosed);
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
    if (!bQueuedStep && ShouldBufferPreInput(Caller.EntityId,Request.Entry))
    {
        StoreReceipt(*Record,Request,Receipt);
        FPendingCombatIntent Buffered;
        Buffered.Entry=Request.Entry;
        Buffered.CastMode=ELDCombatPreInputMode::Direct;
        Buffered.AimDirection=Request.Target.Direction;
        Buffered.GroundPointCm=Request.Target.Point;
        Buffered.ExpiresAtMs=Mode->GetAuthorityNowMs()+LDCombatPreInput::HoldWindowMs;
        Buffered.Action=Request;
        BufferPreInputIntent(Caller.EntityId,MoveTemp(Buffered));
        const auto* Stored=Record->Recent.FindByPredicate([&Request](const FLDCombatDirectEntry& Entry)
            { return Entry.Request.DirectSequence==Request.DirectSequence; });
        return Stored ? Stored->Receipt : Receipt;
    }
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
    ClearPreInputQueue(Caller.EntityId,ELDCombatReject::Cancelled,ELDOrderFailure::Stopped);
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
    // The combo advances from the last released basic; a windup cancelled before release never advances it.
    if (Body->ForcedCombo3Charges>0)
    {
        Action.ComboStep=3;
        --Body->ForcedCombo3Charges;
    }
    else
    {
        Action.ComboStep=LDMomentum::NextComboStep(Body->ComboStep,Body->ComboStartMs,Now,Parameters.Combo.ResetMs);
    }
    Action.BasicIntervalMs=Timing.Value->IntervalMs;
    CancelChannel(*Body);
    Body->Action=Action;
    Body->LastAttackTarget=Target;
    Body->BasicTiming=Timing.Value.GetValue();
    MarkEngaged(*Body,Now);
    ++Body->StateRevision;
    // #198: an accepted basic windup is a combat action of the build safe window.
    MarkBuildUnsafe(Caller.EntityId,Now);
    LDCA::FaceToward(*Actor,TargetFoot);
    // The cadence is committed now; a windup cut before its release gives it back (FinishAction).
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
    if (!Body) { return; }
    const auto* Mode=Core();
    // Issue #335 §3.2: only an unreleased basic windup (< ReleaseAtMs) cancels; once ReleaseAtMs is reached, the due hit
    // lands this tick and movement immediately cancels any remaining post-release recovery cue without resetting NextAttackAtMs.
    if (Body->Action.IsSet() && Body->Action->Kind==ELDCombatActionKind::Basic
        && Body->Action->Key.Origin==ELDCombatRequestOrigin::Nav && Body->Action->Key.Move==Move
        && !(Keep.IsValid() && Body->Action->TargetBody==Keep)
        && (!Mode || Mode->GetAuthorityNowMs()<Body->Action->ReleaseAtMs))
    {
        FinishAction(*Body,ELDCombatReject::Cancelled,false);
        return;
    }
    if (LDCombatCues::CancelReleasedBasic(Body->Cues))
    {
        ++Body->StateRevision;
        MarkViewChanged(Entity);
    }
}

bool ALDCombatDirector::ShouldBufferPreInput(const FGuid& Entity, ELDCombatEntry Entry) const
{
    const auto* Mode=Core();
    const auto* Record=FindRecord(Entity);
    const auto* Body=FindEntityBody(Entity);
    if (!Mode || !Record || !Body || Body->bPendingDeath || !LDCombatPreInput::IsDirectEntry(Entry)) { return false; }
    const int64 Now=Mode->GetAuthorityNowMs();
    if (Body->Action.IsSet() && Body->Action->Kind==ELDCombatActionKind::Ability)
    {
        return LDCombatPreInput::ServerHolds(Entry,true,Body->Action->ReleaseAtMs,Now);
    }
    if (!Record->PreInputQueue.IsEmpty()) { return true; }
    if (const auto* PC=Mode->FindContenderController(Entity))
    {
        if (PC->Navigation && PC->Navigation->IsActiveCastOrderInFlight()) { return true; }
    }
    return false;
}

bool ALDCombatDirector::HasBufferedDirectSequence(const FGuid& Entity, uint64 DirectSequence) const
{
    const auto* Record=FindRecord(Entity);
    return Record && Record->PreInputQueue.ContainsByPredicate([DirectSequence](const FPendingCombatIntent& Intent)
    {
        return Intent.Action.DirectSequence==DirectSequence;
    });
}

void ALDCombatDirector::BufferPreInputIntent(const FGuid& Entity, FPendingCombatIntent&& Intent)
{
    auto* Record=FindRecord(Entity);
    const auto* Mode=Core();
    if (!Record || !Mode) { return; }
    TOptional<FPendingCombatIntent> Evicted;
    LDCombatPreInput::EnqueueFIFO(Record->PreInputQueue,MoveTemp(Intent),Evicted);
    if (!Evicted.IsSet()) { return; }
    const FPendingCombatIntent& Dropped=Evicted.GetValue();
    if (Dropped.CastMode==ELDCombatPreInputMode::Direct)
    {
        FLDCombatActionReceipt Cancelled;
        Cancelled.Generation=Dropped.Action.Generation;
        Cancelled.Life=Dropped.Action.Life;
        Cancelled.DirectSequence=Dropped.Action.DirectSequence;
        Cancelled.Entry=Dropped.Action.Entry;
        Cancelled.Phase=ELDCombatPhase::Rejected;
        Cancelled.Reject=ELDCombatReject::Cancelled;
        StoreReceipt(*Record,Dropped.Action,Cancelled);
        if (const auto* Stored=Record->Recent.FindByPredicate([&Dropped](const FLDCombatDirectEntry& Row)
            { return Row.Request.DirectSequence==Dropped.Action.DirectSequence; }))
        {
            SendReceipt(*Record,Stored->Receipt);
        }
    }
    else if (auto* PC=Mode->FindContenderController(Entity); PC && PC->Navigation)
    {
        PC->Navigation->ReplyBufferedCastCancelled(Dropped.SpatialOrder,Dropped.Action,ELDOrderFailure::Superseded,
            ELDCombatReject::Cancelled);
    }
}

void ALDCombatDirector::ClearPreInputQueue(const FGuid& Entity, ELDCombatReject DirectReason, ELDOrderFailure OrderFailure)
{
    auto* Record=FindRecord(Entity);
    const auto* Mode=Core();
    if (!Record || Record->PreInputQueue.IsEmpty()) { return; }
    const TArray<FPendingCombatIntent> Dropped=MoveTemp(Record->PreInputQueue);
    Record->PreInputQueue.Reset();
    auto* PC=Mode?Mode->FindContenderController(Entity):nullptr;
    for (const FPendingCombatIntent& Intent : Dropped)
    {
        if (Intent.CastMode==ELDCombatPreInputMode::Direct)
        {
            FLDCombatActionReceipt Cancelled;
            Cancelled.Generation=Intent.Action.Generation;
            Cancelled.Life=Intent.Action.Life;
            Cancelled.DirectSequence=Intent.Action.DirectSequence;
            Cancelled.Entry=Intent.Action.Entry;
            Cancelled.Phase=ELDCombatPhase::Rejected;
            Cancelled.Reject=DirectReason;
            StoreReceipt(*Record,Intent.Action,Cancelled);
            if (const auto* Stored=Record->Recent.FindByPredicate([&Intent](const FLDCombatDirectEntry& Row)
                { return Row.Request.DirectSequence==Intent.Action.DirectSequence; }))
            {
                SendReceipt(*Record,Stored->Receipt);
            }
        }
        else if (PC && PC->Navigation)
        {
            PC->Navigation->ReplyBufferedCastCancelled(Intent.SpatialOrder,Intent.Action,OrderFailure,DirectReason);
        }
    }
}

bool ALDCombatDirector::FlushPreInputQueues(const FLDGameplayWritePermit& Permit, FString& Error)
{
    auto* Mode=Core();
    if (!Mode || Mode->ValidateWritePermit(Permit,ELDCommitPhase::Actions)!=ELDCommitReject::None)
    {
        Error=TEXT("Pre-input flush requires the current Actions permit.");
        return false;
    }
    const int64 Now=Mode->GetAuthorityNowMs();
    TArray<FGuid> Entities;
    Records.GetKeys(Entities);
    Entities.Sort();
    for (const FGuid& Entity : Entities)
    {
        for (int32 Pass=0; Pass<LDCombatPreInput::MaxBufferedSteps; ++Pass)
        {
            auto* Record=FindRecord(Entity);
            if (!Record || Record->PreInputQueue.IsEmpty()) { break; }
            auto* Body=FindEntityBody(Entity);
            if (!Body || Body->bPendingDeath)
            {
                ClearPreInputQueue(Entity,ELDCombatReject::Dead,ELDOrderFailure::Dead);
                break;
            }
            const int64 WindupEnd=Body->Action.IsSet() && Body->Action->Kind==ELDCombatActionKind::Ability
                ? Body->Action->ReleaseAtMs : Now;
            int64 LongestHardControl=0;
            for (const FLDCombatRoot& Stun : Body->Stuns) { LongestHardControl=FMath::Max(LongestHardControl,Stun.ExpiresAtMs); }
            for (const FLDCombatRoot& Silence : Body->Silences) { LongestHardControl=FMath::Max(LongestHardControl,Silence.ExpiresAtMs); }
            if (LDCombatPreInput::ShouldClearForHardControl(LongestHardControl,WindupEnd))
            {
                ClearPreInputQueue(Entity,ELDCombatReject::ControlBlocked,ELDOrderFailure::Stopped);
                break;
            }
            while (!Record->PreInputQueue.IsEmpty() && Now>Record->PreInputQueue[0].ExpiresAtMs)
            {
                FPendingCombatIntent Expired=MoveTemp(Record->PreInputQueue[0]);
                Record->PreInputQueue.RemoveAt(0);
                if (Expired.CastMode==ELDCombatPreInputMode::Direct)
                {
                    FLDCombatActionReceipt Cancelled;
                    Cancelled.Generation=Expired.Action.Generation;
                    Cancelled.Life=Expired.Action.Life;
                    Cancelled.DirectSequence=Expired.Action.DirectSequence;
                    Cancelled.Entry=Expired.Action.Entry;
                    Cancelled.Phase=ELDCombatPhase::Rejected;
                    Cancelled.Reject=ELDCombatReject::Cancelled;
                    StoreReceipt(*Record,Expired.Action,Cancelled);
                    if (const auto* Stored=Record->Recent.FindByPredicate([&Expired](const FLDCombatDirectEntry& Row)
                        { return Row.Request.DirectSequence==Expired.Action.DirectSequence; }))
                    {
                        SendReceipt(*Record,Stored->Receipt);
                    }
                }
                else if (auto* PC=Mode->FindContenderController(Entity); PC && PC->Navigation)
                {
                    PC->Navigation->ReplyBufferedCastCancelled(Expired.SpatialOrder,Expired.Action,ELDOrderFailure::Expired,
                        ELDCombatReject::Cancelled);
                }
            }
            if (Record->PreInputQueue.IsEmpty()) { break; }
            if (Body->Action.IsSet() && Body->Action->Kind==ELDCombatActionKind::Ability) { break; }
            auto* PC=Mode->FindContenderController(Entity);
            if (PC && PC->Navigation && PC->Navigation->IsActiveCastOrderInFlight()) { break; }
            FPendingCombatIntent Head=MoveTemp(Record->PreInputQueue[0]);
            Record->PreInputQueue.RemoveAt(0);
            if (Head.CastMode==ELDCombatPreInputMode::Direct)
            {
                FLDCombatActionReceipt Receipt;
                if (!ProcessDirect(*Record,Head.Action,Permit,Receipt,Error)) { return false; }
                Record=FindRecord(Entity);
                if (!Record) { break; }
                StoreReceipt(*Record,Head.Action,Receipt);
                SendReceipt(*Record,Receipt);
            }
            else if (PC && PC->Navigation)
            {
                if (!PC->Navigation->ActivateBufferedCast(Head.SpatialOrder,Head.Action,Permit,Error)) { return false; }
            }
            // Extend any second buffered step's expiry while the first popped step begins winding up or approaching.
            Record=FindRecord(Entity);
            Body=FindEntityBody(Entity);
            if (Record && !Record->PreInputQueue.IsEmpty())
            {
                const int64 Anchor=Body && Body->Action.IsSet() ? Body->Action->ReleaseAtMs : Now;
                Record->PreInputQueue[0].ExpiresAtMs=FMath::Max(Record->PreInputQueue[0].ExpiresAtMs,
                    Anchor+LDCombatPreInput::HoldWindowMs);
            }
        }
    }
    return true;
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
        const FLDDemoMonsterDefinition* Monster=nullptr;
        if(!Catalog()->FindMonsterByName(Body->MonsterId,Monster,Error)){return false;}
        const auto& Skill=Monster->Skill;
        bool Special=Skill.Pattern!=ELDMonsterPattern::Basic && Skill.Pattern!=ELDMonsterPattern::Enrage
            && Skill.Every>0 && (Body->MonsterAttackCount+1)%Skill.Every==0;
        double ChargeRange=LDCA::ToDouble(Skill.RangeCm);
        if(Special && Skill.Pattern==ELDMonsterPattern::Charge && Mode->Camps)
        {
            int32 Camp=INDEX_NONE;const auto& State=Mode->Camps->GetState();
            if(!State.FindLiving({Body->Victim,Body->Identity},&Camp)){Error=TEXT("Charge has no current camp member.");return false;}
            const FVector Offset=Foot-State.GetRoster()->Camps[Camp].AnchorCm;
            const double Leash=LDCA::ToDouble(Monster->LeashRadiusCm);
            if(Offset.SizeSquared2D()>Leash*Leash)
            {Special=false;} // A stranded member may strike, but cannot leave its hold-ground position.
            else
            {
                const double Along=FVector::DotProduct(Offset,(TargetFoot-Foot).GetSafeNormal2D());
                ChargeRange=FMath::Min(ChargeRange,FMath::Max(0.,-Along+FMath::Sqrt(FMath::Max(0.,Along*Along+Leash*Leash-Offset.SizeSquared2D()))));
                Special=ChargeRange>UE_KINDA_SMALL_NUMBER; // No legal forward prefix means a normal strike, never a zero-length warning.
            }
        }
        if(Special && (IsActionBlocked(*Body,ELDCombatControlAction::Cast)||IsActionBlocked(*Body,ELDCombatControlAction::Skill)))
        {return Refuse(ELDCombatReject::ControlBlocked);}
        int64 ReleaseAt=0, NextAttack=0;
        // Spec §11.2 audit affix: the camp's skill warning is AFFIX.AuditTelegraphCut shorter.
        const int64 SkillWindupMs=Body->CampAffix==ELDCampAffix::Audit
            ? FMath::Max<int64>(1,Skill.WindupMs*(10000-Catalog()->Get().Parameters.Affix.AuditTelegraphCutBp)/10000) : Skill.WindupMs;
        if (Record.NextToken==MAX_uint64 || !LDMatchLifecycle::AddDurationMs(Now,Special?SkillWindupMs:Body->BasicTiming.WindupMs,ReleaseAt,Error)
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
        Action.bMonsterSpecial=Special;
        if(Special){Action.Source.DefinitionId=Body->MonsterId;}
        Action.Direction=(TargetFoot-Foot).GetSafeNormal2D();
        Action.TargetPoint=TargetFoot;
        if(Special)
        {
            auto Areas=LDMonsterRules::Areas(Skill,Body->CampStage,Foot,TargetFoot);
            if(Skill.Pattern==ELDMonsterPattern::Charge){for(auto& Area:Areas){Area.Range=FMath::Min(Area.Range,ChargeRange);}}
            if(Areas.IsEmpty()){Error=TEXT("A monster special has no executable shape.");return false;}
            const int64 LastDelay=Areas.Last().DelayMs;
            int64 LastAt=0,RecoverAt=0;
            if(!LDMatchLifecycle::AddDurationMs(ReleaseAt,LastDelay,LastAt,Error)
                || !LDMatchLifecycle::AddDurationMs(LastAt,Skill.RecoveryMs,RecoverAt,Error)){return false;}
            NextAttack=FMath::Max(NextAttack,RecoverAt);
            Body->MonsterRecoverUntilMs=RecoverAt;
            for(const auto& Area:Areas)
            {
                FLDMonsterStrike Strike;
                Strike.Key=FGuid::NewGuid();Strike.Area=Area;Strike.Token=Action.Token;
                if(!LDMatchLifecycle::AddDurationMs(ReleaseAt,Area.DelayMs,Strike.AtMs,Error)){return false;}
                Strike.Payload.SourceBody={Body->Victim,Body->Identity};
                Strike.Payload.SourceEntity=Body->Victim.EntityId;
                MonsterStrikes.Add(Strike);
            }
        }
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
    // Skill branches: an open recast window releases without cooldown or momentum; a free cast without momentum.
    const bool bRecastOpen=BranchRecastOpen(*Body,Source,Now);
    Gate.ReadyAtMs=bRecastOpen ? Now : Track->ReadyAtMs;
    if (Source.Kind!=ELDCombatSourceKind::Innate)
    {
        if (!SkillChargesOf(*Body,*ValuesDef,Gate.Charges,Error)) { return false; }
        Gate.ChargeMs=Track->LastDurationMs;
    }
    Gate.MomentumP2=Body->Resources.MomentumP2;
    // The release's own cost (ReleaseAbility): D08's dash momentum unless a D05 segment is open; nothing for a free cast
    // (S05 B2, D04) or a K12 health payment.
    const bool bInnateEntry=Source.Kind==ELDCombatSourceKind::Innate;
    const bool bSplitOpen=bInnateEntry && Body->SplitDashLeft>0 && Now<=Body->SplitDashUntilMs;
    Gate.MomentumCostP2=bInnateEntry ? (bSplitOpen ? 0 : Terms.DashMomentumCostP2)
        : bRecastOpen || BranchFreeCast(*Body,Source,Now) || Terms.HPCostBpPerMomentum>0 ? 0 : ValuesDef->MomentumCostP2;
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
        // A ground zone: the release clamps a point beyond the authored reach toward it. A vector-aimed shape may carry its
        // planar facing (LDCombatSourceRules::VectorAimed); any other ground skill sends none.
        if (Request.Target.Kind!=ELDCombatTargetKind::Point || !LDCA::ValidPoint(Request.Target.Point))
        { return Refuse(ELDCombatReject::InvalidDirection); }
        if (!FVector(Request.Target.Direction).IsZero()
            && (!LDCombatSourceRules::VectorAimed(*ValuesDef) || !LDCA::ValidDirection(Request.Target.Direction)))
        { return Refuse(ELDCombatReject::InvalidDirection); }
        Action.TargetPoint=Request.Target.Point;
        Action.Direction=FVector(Request.Target.Direction).GetSafeNormal2D();
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
    // A new ability interrupts an unreleased basic windup; FinishAction gives its committed cadence back.
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
                After->Cues.bReleasedBasic=Action.Kind==ELDCombatActionKind::Basic && !Action.bMonsterSpecial;
                After->Cues.ReleasedComboStep=After->Cues.bReleasedBasic ? Action.ComboStep : 0;
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
    return FlushPreInputQueues(Permit,Error);
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
    if(Action.bMonsterSpecial)
    {
        if(IsActionBlocked(Body,ELDCombatControlAction::Basic)||IsActionBlocked(Body,ELDCombatControlAction::Cast)||IsActionBlocked(Body,ELDCombatControlAction::Skill))
        {Outcome=ELDCombatReject::ControlBlocked;FinishAction(Body,Outcome,false);return true;}
        return ReleaseMonsterSkill(Actor,Body,Action,Outcome,Error);
    }
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
    if(Body.Victim.Kind==ELDVictimKind::CampMember)
    {
        const FLDDemoMonsterDefinition* Monster=nullptr;
        if(!Content->FindMonsterByName(Body.MonsterId,Monster,Error)){return false;}
        Body.MonsterAttackCount=Monster->Skill.Every>0?(Body.MonsterAttackCount+1)%Monster->Skill.Every:0;
        if(Body.bMonsterEnraged){Hit.ADRatioBp+=Body.CampStage>=Monster->Skill.EvolveStage?Monster->Skill.EvolvedPassiveBp:Monster->Skill.PassiveBp;}
    }
    Hit.SourceAPP2=Final.APP2;
    if (!StampSourceTerms(Body,Hit,Error) || !SnapshotOnHit(Body,10000,Hit,Error)) { return false; }
    const int64 Now=Mode->GetAuthorityNowMs();
    // COMBO.StepRatios: the contender's combo step scales the basic's own raw package (not the merged empowered ones)
    // before its critical roll. The released step is the body's combo from now on.
    if (Body.Victim.Kind==ELDVictimKind::Contender && Action.ComboStep>0)
    {
        const int64 ComboBp=LDMomentum::ComboRatioBp(Content->Get().Parameters.Combo,Action.ComboStep);
        if (ComboBp<=0 || Hit.ADRatioBp>MAX_int64/ComboBp) { Error=TEXT("A combo step leaves the checked numeric domain."); return false; }
        Hit.ADRatioBp=Hit.ADRatioBp*ComboBp/10000;
        Hit.ComboStep=Action.ComboStep;
        Hit.BasicIntervalMs=Action.BasicIntervalMs;
        Body.ComboStep=Action.ComboStep;
        Body.ComboStartMs=Action.StartedAtMs;
    }
    // Spec §10.2: every live empowered package (one per source: S11, S17, B12) joins this one primary hit before its
    // critical roll, so the hit crits, heals and counts once.
    const int32 PendingBefore=Body.Empowers.Num();
    TArray<FLDCombatEmpower> Taken;
    int64 EmpowerP2=0;
    if (!LDCombatControl::ConsumeEmpowers(Body.Empowers,Now,Taken,EmpowerP2) || EmpowerP2>MAX_int64-Hit.BaseP2)
    { Error=TEXT("The empowered packages of a basic leave the checked numeric domain."); return false; }
    // B04: lifesteal heals only the primary hit's own share of the raw package; merged packages (S11, S17, B12, C09) never
    // feed it. A critical multiplies both parts alike, so the share holds.
    if (EmpowerP2>0)
    {
        const auto Part=[](int64 ValueP2, int64 Bp) { return ValueP2<=0 || Bp<=0 ? int64(0) : ValueP2/10000*Bp+ValueP2%10000*Bp/10000; };
        const int64 OwnP2=FMath::Max<int64>(Hit.BaseP2,0)+Part(Hit.SourceADP2,Hit.ADRatioBp)+Part(Hit.SourceAPP2,Hit.APRatioBp);
        if (OwnP2<0 || OwnP2>MAX_int64/10000-EmpowerP2)
        { Error=TEXT("The own share of an empowered basic leaves the checked numeric domain."); return false; }
        Hit.LifestealShareBp=OwnP2*10000/(OwnP2+EmpowerP2);
    }
    Hit.BaseP2+=EmpowerP2;
    // Spec §6.5: a contender's basic opens its chain; every card its landing triggers (consumed packages, A04/A06/A07/A09/A11,
    // A08) is a step of it.
    TGuardValue<FGuid> ChainScope(CurrentChain,OpenChain(Body,Hit.EffectId,FGuid(),Now));
    bool bChargeBonusEnded=false;
    for (const FLDCombatEmpower& Package : Taken)
    {
        // A card's package (D03) is one trigger when a landed basic consumes it; arming it is none.
        if (Package.Source.Kind==ELDCombatSourceKind::Card) { LedgerCardTriggered(Body,Package.Source.DefinitionId); }
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
    // Skill branches: a pending next-basic bonus joins this hit once; basic riders shock or breach; the empowered packages'
    // own terms (S11, S17) add momentum, breach, a last-charge stun or a certain crit.
    if (Body.Branch.NextBasicAmpBp>0 && Body.Branch.NextBasicAmpUntilMs>Now) { Hit.BranchAmpBp=Body.Branch.NextBasicAmpBp; }
    Body.Branch.NextBasicAmpBp=0;
    Body.Branch.NextBasicAmpUntilMs=0;
    if (Body.Branch.RiderUntilMs>Now)
    {
        if (Body.Branch.bRiderElectric) { Hit.NativeElements|=LDCombatElements::Bit(ELDCombatElement::Shock); }
        Hit.BreachStacks+=Body.Branch.RiderBreach;
        if (Body.Branch.bRiderElectric && !StampElementSnapshot(Body,Hit,Error)) { return false; }
    }
    for (const FLDCombatEmpower& Package : Taken)
    {
        Hit.bCritical|=Package.bAlwaysCrit;
        Hit.BreachStacks+=Package.BreachStacks;
        if (Package.MomentumP2>0) { GrantMomentum(Body,Package.MomentumP2,ELDMomentumBook::Skill); }
        if (Package.Charges==0 && Package.LastStunMs>0) { Hit.StunDurationMs=FMath::Max(Hit.StunDurationMs,Package.LastStunMs); }
    }
    // An A04 push leaves from the holder's foot (this one if the holder is gone when it lands).
    Hit.KnockbackAnchorCm=FootOf(Actor);
    // Cards of the landing basic: A04's push, A06/A09 strikes and A11 riders queue with it.
    TArray<FLDCombatHit> Extra;
    if (!OnBasicLanded(Body,Hit,Extra,Error)) { return false; }
    // #345 active_flurry_rush FrenzyWaves: while frenzy is active, each primary basic launches a piercing adaptive fist-wind wave.
    if (Body.ActiveFrenzy.IsSet() && Now<Body.FrenzyUntilMs)
    {
        ALDCharacter* FrenzyTargetActor=nullptr;
        FindBody(Action.TargetBody,&FrenzyTargetActor);
        if (FrenzyTargetActor)
        {
            const FLDDemoEffectDefinition& Frenzy=Body.ActiveFrenzy.GetValue();
            const FVector WaveDir=(FootOf(*FrenzyTargetActor)-FootOf(Actor)).GetSafeNormal2D();
            if (!WaveDir.IsNearlyZero())
            {
                FLDCombatHit WaveHit;
                WaveHit.EffectId=FGuid::NewGuid();
                WaveHit.Token=Action.Token;
                WaveHit.SourceBody=Hit.SourceBody;
                WaveHit.SourceEntity=Hit.SourceEntity;
                WaveHit.CreditOwner=Hit.CreditOwner;
                WaveHit.Source=Body.FrenzySource;
                WaveHit.HitClass=ELDCombatHitClass::Ability;
                WaveHit.bDirectAbilityDamage=true;
                WaveHit.BaseP2=Frenzy.BaseP2;
                WaveHit.AdaptiveRatioBp=Frenzy.AdaptiveRatioBp;
                const int64 BonusADP2=LDTactical::BonusADOf(Content->Get().Parameters,GrowthLevelOf(Body),Final.ADP2);
                WaveHit.DamageType=LDTactical::EffectiveDamageType(ELDDemoDamageType::Physical,Frenzy.AdaptiveRatioBp,BonusADP2,Final.APP2);
                WaveHit.SourceADP2=Final.ADP2;
                WaveHit.SourceAPP2=Final.APP2;
                WaveHit.SourceBonusADP2=BonusADP2;
                WaveHit.BreachStacks=Frenzy.Count;
                if (Frenzy.RatioBp>0)
                {
                    WaveHit.OnHitScaleBp=10000;
                    WaveHit.bLifesteal=true;
                    WaveHit.LifestealRatioBp=Frenzy.RatioBp;
                }
                if (Hit.ComboStep==3 && Frenzy.MultiplierBp>10000)
                {
                    // A multiplier above 1 is a package multiplier; the delivery share stays within 1.
                    const int64 Scale=WaveHit.PackageScaleBp,Bp=Frenzy.MultiplierBp;
                    WaveHit.PackageScaleBp=FMath::Min<int64>(100000,Scale/10000*Bp+Scale%10000*Bp/10000);
                }
                if (!StampSourceTerms(Body,WaveHit,Error) || !StampElementSnapshot(Body,WaveHit,Error)) { return false; }
                FLDCombatProjectile WaveProj;
                WaveProj.EffectId=WaveHit.EffectId;
                WaveProj.FlightId=FGuid::NewGuid();
                WaveProj.Token=Action.Token;
                WaveProj.SourceBody=Hit.SourceBody;
                WaveProj.SourceEntity=Hit.SourceEntity;
                WaveProj.CreditOwner=Hit.CreditOwner;
                WaveProj.Source=Body.FrenzySource;
                WaveProj.DamageType=WaveHit.DamageType;
                WaveProj.OriginCm=FootOf(Actor);
                WaveProj.Direction=WaveDir;
                WaveProj.RangeCm=LDCA::ToDouble(Frenzy.DistanceCm);
                WaveProj.RadiusCm=LDCA::ToDouble(Frenzy.RadiusCm);
                WaveProj.SpeedCmPerSec=FMath::Max(800.,LDCA::ToDouble(Frenzy.ExtraDistanceCm));
                WaveProj.AdvancedThroughMs=Now;
                WaveProj.PierceRemaining=8;
                WaveProj.PierceScaleBp=10000;
                WaveProj.Payload=WaveHit;
                Projectiles.Add(MoveTemp(WaveProj));
            }
        }
    }
    // #345 card_spellblade_echo: after casting a skill, next charges of basics unleash an AP area wave carrying that skill's element.
    if (Body.SpellbladeCharges>0 && Now<Body.SpellbladeUntilMs)
    {
        TArray<FLDPassiveEffectRef> Echoes;
        if (!CollectPassiveEffects(Body.AttributeProjection,ELDDemoEffectOp::SpellbladeEcho,Echoes,Error)) { return false; }
        ALDCharacter* EchoTargetActor=nullptr;
        FindBody(Action.TargetBody,&EchoTargetActor);
        if (!Echoes.IsEmpty() && EchoTargetActor)
        {
            --Body.SpellbladeCharges;
            int64 EchoAPBp=0;
            for (const FLDPassiveEffectRef& Ref : Echoes) { EchoAPBp+=Ref.Effect.RatioBp; }
            const double EchoRadius=LDCA::ToDouble(Echoes[0].Effect.RadiusCm)*HulkingAreaScaleOf(Body);
            TArray<FLDCombatBodyRef> Around;
            if (EchoRadius>0 && CollectAreaTargets(FootOf(*EchoTargetActor),EchoRadius,&Body,Body.Victim.EntityId,Around,Error))
            {
                const FGuid EchoId=FGuid::NewGuid();
                for (const FLDCombatBodyRef& Enemy : Around)
                {
                    FLDCombatHit Blade;
                    Blade.HitId=FGuid::NewGuid();
                    Blade.EffectId=EchoId;
                    Blade.Target=Enemy;
                    Blade.SourceBody=Hit.SourceBody;
                    Blade.SourceEntity=Hit.SourceEntity;
                    Blade.CreditOwner=Hit.CreditOwner;
                    Blade.Source=Echoes[0].Source;
                    Blade.HitClass=ELDCombatHitClass::Ability;
                    Blade.APRatioBp=EchoAPBp;
                    Blade.DamageType=ELDDemoDamageType::Magical;
                    Blade.SourceADP2=Final.ADP2;
                    Blade.SourceAPP2=Final.APP2;
                    if (Body.SpellbladeElement==ELDCombatElement::Fire) { Blade.BurnStacks=1; }
                    else if (Body.SpellbladeElement!=ELDCombatElement::None)
                    {
                        Blade.NativeElements|=LDCombatElements::Bit(Body.SpellbladeElement);
                    }
                    if (!StampSourceTerms(Body,Blade,Error) || !StampElementSnapshot(Body,Blade,Error)) { return false; }
                    Extra.Add(MoveTemp(Blade));
                }
                if (!Around.IsEmpty()) { LedgerCardTriggered(Body,Echoes[0].Key); }
            }
        }
    }
    // A05: a contender's basic flies toward its target as a dodgeable projectile carrying its strikes and riders; it lands on
    // the first legal body in its way instead of the target for certain.
    TArray<FLDPassiveEffectRef> Flights;
    if (Body.Victim.Kind==ELDVictimKind::Contender
        && !CardsOn(Body.AttributeProjection,ELDDemoEffectOp::BasicProjectile,{ELDCombatSourceKind::Basic,NAME_None},Flights,Error))
    { return false; }
    ALDCharacter* TargetActor=nullptr;
    FindBody(Action.TargetBody,&TargetActor);
    if (!Flights.IsEmpty() && TargetActor)
    {
        const FLDDemoEffectDefinition& Flight=Flights[0].Effect;
        const auto Cm=[](const FLDDemoRational& V) { return V.Denominator>0 ? double(V.Numerator)/double(V.Denominator) : 0.; };
        FLDCombatProjectile Projectile;
        Projectile.EffectId=Hit.EffectId;
        Projectile.FlightId=FGuid::NewGuid();
        Projectile.Token=Action.Token;
        Projectile.SourceBody=Hit.SourceBody;
        Projectile.SourceEntity=Hit.SourceEntity;
        Projectile.CreditOwner=Hit.CreditOwner;
        Projectile.Source=Hit.Source;
        Projectile.DamageType=Hit.DamageType;
        Projectile.OriginCm=FootOf(Actor);
        Projectile.Direction=(FootOf(*TargetActor)-Projectile.OriginCm).GetSafeNormal2D();
        Projectile.SpeedCmPerSec=Cm(Flight.ExtraDistanceCm);
        // It reaches as far as the holder's (card-extended) attack range plus the target's body.
        Projectile.RangeCm=Cm(Final.AttackRangeCm)+TargetActor->GetCapsuleComponent()->GetScaledCapsuleRadius();
        Projectile.RadiusCm=Cm(Flight.RadiusCm);
        Projectile.AdvancedThroughMs=Now;
        Projectile.Payload=Hit;
        Projectile.Riders=MoveTemp(Extra);
        // A08 waits for the flight to strike a body.
        Projectile.bComboCast=true;
        Projectile.ComboBasic=Action;
        if (Projectile.Direction.IsNearlyZero() || Projectile.SpeedCmPerSec<=0)
        { Error=TEXT("A flying basic has no direction or speed."); return false; }
        Projectiles.Add(MoveTemp(Projectile));
        LedgerCardTriggered(Body,Flights[0].Key);
    }
    else
    {
        PendingHits.Add(Hit);
        PendingHits.Append(MoveTemp(Extra));
        // A08: the third step also releases the Q skill (queued for the next authority step).
        if (!QueueComboCast(Body,Action,Hit,Now,Error)) { return false; }
    }
    LedgerEffectReleased(Body,Hit.EffectId,ELDBlueprintFact::BasicAttacks);
    if (!Taken.IsEmpty() || Body.Empowers.Num()!=PendingBefore)
    {
        ++Body.StateRevision;
        MarkViewChanged(Body.Victim.EntityId);
    }
    FinishAction(Body,ELDCombatReject::None,true);
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
    // A08: a combo cast releases its whole effect at its share (damage and shields by ShareBp = 0.45, hard-control durations
    // by 0.50 per #330 §5.2.6); K06: a repeat releases at its share of the damage.
    if (Action.bAutoRecast && Action.bWholeEffectShare)
    {
        const int64 ShareBp=Action.AutoRecastScaleBp;
        const int64 HardControlShareBp=5000;
        // A brace keeps its record cap: its answer, heal and shield from the record take the share once at settlement.
        TArray<int64> BraceCaps;
        for (const FLDDemoEffectDefinition& Effect : Terms.Values.Effects)
        { if (Effect.GetOp()==ELDDemoEffectOp::Brace) { BraceCaps.Add(Effect.LimitP2); } }
        if (!LDSkillGrowth::ScaleValues(Terms.Values,ShareBp,ShareBp))
        { Error=TEXT("A combo cast's share overflows its effect."); return false; }
        int32 BraceIndex=0;
        for (FLDDemoEffectDefinition& Effect : Terms.Values.Effects)
        { if (Effect.GetOp()==ELDDemoEffectOp::Brace) { Effect.LimitP2=BraceCaps[BraceIndex++]; } }
        const auto Share=[ShareBp](int64& Value) { Value=Value/10000*ShareBp+Value%10000*ShareBp/10000; };
        const auto ShareHardControl=[HardControlShareBp](int64& Value)
        { Value=FMath::Max<int64>(1,Value/10000*HardControlShareBp+Value%10000*HardControlShareBp/10000); };
        for (FLDDemoEffectDefinition& Effect : Terms.Values.Effects)
        {
            const ELDDemoEffectOp Op=Effect.GetOp();
            if (Effect.IsControl() || Op==ELDDemoEffectOp::BraceStun) { ShareHardControl(Effect.DurationMs); }
            else if (Op==ELDDemoEffectOp::Slow || Op==ELDDemoEffectOp::AttackSlow || Op==ELDDemoEffectOp::Knockback
                || Op==ELDDemoEffectOp::BraceShove)
            { Share(Effect.DurationMs); }
            // S22's cone slow.
            if (Op==ELDDemoEffectOp::ChannelCone) { Share(Effect.SecondaryDurationMs); }
            if (int64* Composed=Effect.ComposedControlMs(); Composed && Composed!=&Effect.DurationMs) { ShareHardControl(*Composed); }
            // Amounts ScaleValues does not grow: a pull's meeting damage.
            if (Op==ELDDemoEffectOp::Pull) { Share(Effect.BaseP2); Share(Effect.ADRatioBp); }
        }
        // A pull a card or branch carries outside the value set meets at its share too.
        if (Terms.PullTerms.IsSet()) { Share(Terms.PullTerms->BaseP2); Share(Terms.PullTerms->ADRatioBp); Share(Terms.PullTerms->DurationMs); }
        if (Terms.FinalKnockbackTerms.IsSet()) { Share(Terms.FinalKnockbackTerms->DurationMs); }
    }
    else if (Action.bAutoRecast) { Terms.PackageScaleBp=Terms.PackageScaleBp/10000*Action.AutoRecastScaleBp+Terms.PackageScaleBp%10000*Action.AutoRecastScaleBp/10000; }
    const FLDDemoAbilityValues* ValuesDef=&Terms.Values;
    const FLDDemoTargetDefinition& TargetDef=Terms.Target;
    // The caster's foot before any displacement of this release (areas, zones and pushes centre here).
    const FVector ReleaseFoot=FootOf(Actor);
    // The innate dash takes the approved character's dash terms (D19).
    const bool bInnate=Source.Kind==ELDCombatSourceKind::Innate;
    FLDInnateDashTerms Innate;
    if (bInnate && !InnateDashFor(Innate,Error)) { return false; }
    // K08 and D05 on D: its issued cooldown and segment distance follow the cards (Terms).
    if (bInnate) { LDCombatSourceRules::ScaleInnateDash(Innate,Terms); }
    // A stun or taunt that landed during the windup ends it (normally already abandoned at the hit).
    FLDActiveGateInput Gate;
    Gate.NowMs=Now;
    // A K06 repeat and an open D05 second segment cost nothing and issue no cooldown, like a branch recast.
    const bool bSplitSegment=bInnate && Body.SplitDashLeft>0 && Now<=Body.SplitDashUntilMs;
    const bool bBranchRecast=!bInnate && !Action.bAutoRecast && BranchRecastOpen(Body,Source,Now);
    const bool bRecast=bBranchRecast || Action.bAutoRecast || bSplitSegment;
    // S05 B2's window or D04's free cast of a perfect dodge.
    const bool bFreeCast=!bInnate && !bRecast && BranchFreeCast(Body,Source,Now);
    // K12: the release pays health instead of momentum (refused while the body cannot pay it and live).
    const bool bHealthCost=!bInnate && !bRecast && !bFreeCast && Terms.HPCostBpPerMomentum>0;
    const int64 HealthCostP2=bHealthCost ? Body.Resources.MaxHpP2/10000*Terms.HPCostBpPerMomentum*ValuesDef->MomentumCostP2/100
        +Body.Resources.MaxHpP2%10000*Terms.HPCostBpPerMomentum/10000*ValuesDef->MomentumCostP2/100 : 0;
    if (bHealthCost && Body.Resources.HpP2<=HealthCostP2) { Reject=ELDCombatReject::HealthShort; return true; }
    Gate.ReadyAtMs=bRecast ? Now : Track->ReadyAtMs;
    if (!bInnate)
    {
        if (!SkillChargesOf(Body,*ValuesDef,Gate.Charges,Error)) { return false; }
        Gate.ChargeMs=Track->LastDurationMs;
    }
    Gate.MomentumP2=Body.Resources.MomentumP2;
    // D08: the innate dash pays its card's momentum (an open D05 segment costs nothing).
    Gate.MomentumCostP2=bInnate ? (bSplitSegment ? 0 : Terms.DashMomentumCostP2)
        : bRecast || bFreeCast || bHealthCost ? 0 : ValuesDef->MomentumCostP2;
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
    // S02 B / S12 B: the recast takes its own terms over this release's snapshot.
    TOptional<FLDCombatRecastWindow> RecastWindow;
    if (bBranchRecast)
    {
        RecastWindow=Body.Branch.Recast.GetValue();
        Body.Branch.Recast.Reset();
        ApplyRecastTerms(RecastWindow.GetValue(),Terms,DeliveryAction);
    }
    // Every ground-point skill lands on legal ground (LDGroundLanding): a wall, a roof or ground off the walk lands short,
    // back along the cast. An impact or trap with nowhere to land refuses; a zone without a registered map, or with no
    // landing, keeps its clamped point.
    TOptional<FVector> LandedPoint;
    double GroundReachCm=LDCA::ToDouble(TargetDef.RangeCm);
    const bool bNeedsGround=TargetDef.Mode==ELDDemoTargetMode::GroundPoint
        && (LDCA::FindEffect(*ValuesDef,ELDDemoEffectOp::GroundImpact) || LDCA::FindEffect(*ValuesDef,ELDDemoEffectOp::ContactTrap));
    if (TargetDef.Mode==ELDDemoTargetMode::GroundPoint)
    {
        const auto* Registry=GetWorld()->GetSubsystem<ULDMapRegistry>();
        const auto* Geometry=Registry ? Registry->GetGeometry() : nullptr;
        const bool bRegistered=Registry && Registry->Enabled() && Geometry && !Geometry->Surfaces.IsEmpty();
        if (bNeedsGround && !bRegistered) { Error=TEXT("A ground skill requires the registered ground surface."); return false; }
        // The release range (U05 eagle stance included), the reach the order approached with.
        if (!CastRangeCm(Body,TargetDef,GroundReachCm,Error)) { return false; }
        if (bRegistered && !LDGroundLanding::Resolve(*Registry,ReleaseFoot,Action.TargetPoint,GroundReachCm,
            Actor.GetCapsuleComponent()->GetCollisionShape(),LandedPoint,Error)) { return false; }
        if (LandedPoint.IsSet()) { DeliveryAction.TargetPoint=*LandedPoint; }
        else if (bNeedsGround) { Reject=ELDCombatReject::Blocked; return true; }
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
    const FLDDemoEffectDefinition* RotEffect=LDCA::FindEffect(*ValuesDef,ELDDemoEffectOp::Rot);
    const FLDDemoEffectDefinition* ChillEffect=LDCA::FindEffect(*ValuesDef,ELDDemoEffectOp::Chill);
    const FLDDemoEffectDefinition* ChainEffect=LDCA::FindEffect(*ValuesDef,ELDDemoEffectOp::ChainBounce);
    const bool bHostile=DamageEffect || RootEffect || SlowEffect || DisarmEffect || StunEffect
        || SilenceEffect || BreachEffect || BurnEffect || WetEffect || ElectrifyEffect || RotEffect || ChillEffect
        || LDCA::FindEffect(*ValuesDef,ELDDemoEffectOp::Knockback)
        // N2's fan carries its push in the release payload (StartSkillDelivery).
        || LDCA::FindEffect(*ValuesDef,ELDDemoEffectOp::Fan);
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
    // The reshapes, the hulking presence and the warcry ring's own growth add into one multiplier of the self area.
    const double EffectiveSelfAreaRadiusCm=AreaEffect
        ? LDCA::ToDouble(AreaEffect->RadiusCm)*HulkingAreaScaleOf(Body,Terms.AreaBp,WarcryAreaBpOf(Body,*ValuesDef)) : 0.;
    if (bHostile && bAreaMode && AreaEffect
        && !CollectAreaTargets(FootOf(Actor),EffectiveSelfAreaRadiusCm,&Body,Body.Victim.EntityId,AreaTargets,Error))
    { return false; }
    const int64 BaseCooldownMs=bInnate ? 0 : ValuesDef->CooldownMs;
    // The duration is fixed at the actual release moment; later build changes never move this endpoint. The innate
    // dash issues its whole cooldown without haste.
    int64 DurationMs=bInnate ? Innate.CooldownMs : 0, ReadyAt=0;
    // Spec §11.1 gym day: the dash recovers DAILY.GymDashCooldownCut sooner.
    if (bInnate && Authority() && Authority()->HasDaily(ELDDailyModifier::Gym))
    {
        DurationMs=FMath::Max<int64>(1,DurationMs*(10000-Content->Get().Parameters.Daily.GymDashCooldownCutBp)/10000);
    }
    // The agility keystone: the D dash recovers its share sooner.
    if (bInnate && Body.AttributeProjection.Keystones.bAgility)
    {
        DurationMs=FMath::Max<int64>(1,DurationMs*(10000-Content->Get().Parameters.KeystoneAgilityDashCooldownCutBp)/10000);
    }
    if (!bInnate && !LDTactical::CooldownDurationMs(BaseCooldownMs,Body.AttributeProjection.Final.CooldownHasteP2,
            Content->Get().Formulas.HasteScaleP2,DurationMs,Error)) { return false; }
    // SKILL.CooldownFloorSec: the technical floor of a hasted Q/W/E (animation and network), not a gameplay cap.
    if (!bInnate) { DurationMs=FMath::Max(DurationMs,Content->Get().Parameters.SkillRanks.CooldownFloorMs); }
    // One charge spent: a single-charge track is ready (ReadyAt <= now) here, so this is now + duration; a charged track
    // with a recovery running queues this charge after it.
    if (!LDCombatSourceRules::SpendCharge(Track->ReadyAtMs,Now,DurationMs,ReadyAt))
    { Error=TEXT("A release's cooldown end overflows the match clock."); return false; }
    // The release's locked damage factors (K09/K12/K16/K17), read from the track's real ReadyAt before this release issues
    // the next one (K16's wait restarts after it).
    TArray<FLDDamageFactor> ReleaseFactorsNow;
    if (!ReleaseFactors(Body,*Record,Source,Track,Now,ReleaseFactorsNow,Error)) { return false; }
    // Every fallible step is done: pay once and register the effects.
    const int64 PaidP2=Gate.MomentumCostP2;
    GrantMomentum(Body,-PaidP2,ELDMomentumBook::Spent);
    if (bHealthCost)
    {
        Body.Resources.HpP2-=HealthCostP2;
        UE_LOG(LogLDCombat,Log,TEXT("LD_COMBAT_HEALTH_COST t=%lld src=%s active=%s paid=%s"),Now,*LDLog::Id(Body.Victim.EntityId),
            *Source.DefinitionId.ToString(),*LDLog::P2(HealthCostP2));
    }
    // D05: the first segment opens the next ones; each later segment uses one.
    if (bInnate && Terms.SplitSegments>1)
    {
        if (bSplitSegment) { --Body.SplitDashLeft; }
        else { Body.SplitDashLeft=Terms.SplitSegments-1; }
        if (!LDMatchLifecycle::AddDurationMs(Now,Terms.SplitWindowMs+Innate.DurationMs,Body.SplitDashUntilMs,Error)) { return false; }
    }
    if (bFreeCast) { Body.Branch.FreeCastUntilMs=0; }
    // K05: the next skill took the control bonus; K18: this release joins the recent ones (repeats and recasts do not).
    if (Source.Kind==ELDCombatSourceKind::Learned && !Body.ControlAmpCard.IsNone())
    {
        Body.ControlAmpCard=NAME_None;
        Body.ControlAmpBp=0;
        Body.ControlAmpUntilMs=0;
    }
    if (Source.Kind==ELDCombatSourceKind::Learned && !bRecast)
    {
        Body.RecentCasts.RemoveAll([Now](const TPair<FName,int64>& Row) { return Now-Row.Value>10000; });
        Body.RecentCasts.Emplace(Source.DefinitionId,Now);
        // card_martial_scholar: casting a skill consumes any pending scholar skill bonus.
        Body.ScholarSkillUntilMs=0;
        // card_spellblade_echo: casting a learned skill arms Spellblade charges and snapshots the skill's element/medal element.
        TArray<FLDPassiveEffectRef> Echoes;
        if (!CollectPassiveEffects(Body.AttributeProjection,ELDDemoEffectOp::SpellbladeEcho,Echoes,Error)) { return false; }
        if (!Echoes.IsEmpty())
        {
            Body.SpellbladeCharges=Echoes[0].Effect.Count;
            if (!LDMatchLifecycle::AddDurationMs(Now,Echoes[0].Effect.DurationMs,Body.SpellbladeUntilMs,Error)) { return false; }
            Body.SpellbladeHost=Source;
            ELDCombatElement Elem=ELDCombatElement::None;
            if (Ability)
            {
                switch (Ability->Element)
                {
                case ELDDemoSkillElement::Fire: Elem=ELDCombatElement::Fire; break;
                case ELDDemoSkillElement::Water: Elem=ELDCombatElement::Water; break;
                case ELDDemoSkillElement::Shock: Elem=ELDCombatElement::Shock; break;
                case ELDDemoSkillElement::Chill: Elem=ELDCombatElement::Chill; break;
                case ELDDemoSkillElement::Rot: Elem=ELDCombatElement::Rot; break;
                default: break;
                }
            }
            if (Elem==ELDCombatElement::None)
            {
                TArray<FLDPassiveEffectRef> Medals;
                if (!CardsOn(Body.AttributeProjection,ELDDemoEffectOp::SkillElement,Source,Medals,Error)) { return false; }
                if (!Medals.IsEmpty()) { Elem=LDCombatElements::FromDemo(Medals[0].Effect.Element); }
            }
            Body.SpellbladeElement=Elem;
        }
    }
    ++Body.Resources.Revision;
    // A recast keeps the first release's cooldown.
    if (!bRecast)
    {
        Track->ReadyAtMs=ReadyAt;
        Track->LastDurationMs=DurationMs;
    }
    const auto& Final=Body.AttributeProjection.Final;
    const FGuid EffectId=FGuid::NewGuid();
    Track->LastEffectId=EffectId;
    // Spec §6.5: the release opens its chain (a free repeat continues the chain of the action that queued it).
    TGuardValue<FGuid> ChainScope(CurrentChain,OpenChain(Body,EffectId,Action.ChainId,Now));
    LastReleaseChain=CurrentChain;
    // The innate dash is no build ability: its hits still belong to the version, but it is not counted as a cast.
    LedgerEffectReleased(Body,EffectId,bInnate ? ELDBlueprintFact::Count : ELDBlueprintFact::AbilityCasts);
    auto CastLedger=MakeShared<FLDCombatCastLedger>();
    CastLedger->PaidMomentumP2=PaidP2;
    CastLedger->Factors=MoveTemp(ReleaseFactorsNow);
    CastLedger->bAutoRecast=Action.bAutoRecast;
    if (RecastWindow.IsSet())
    {
        CastLedger->bRecast=true;
        if (RecastWindow->FirstLedger) { CastLedger->RecastFirst=RecastWindow->FirstLedger->RiderStruck; }
    }
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
            const int64 ResistSum=Final.ArmorP2+Final.MagicResistP2;
            if (Effect.APRatioBp<0 || Final.APP2<0 || (Effect.APRatioBp>0 && Final.APP2>MAX_int64/Effect.APRatioBp)
                || Effect.ADRatioBp<0 || Final.ADP2<0 || (Effect.ADRatioBp>0 && Final.ADP2>MAX_int64/Effect.ADRatioBp)
                || Effect.MaxHPRatioBp<0 || MaxHP<0 || Effect.ResistRatioBp<0 || ResistSum<0)
            { Error=TEXT("Shield scaling overflowed."); return false; }
            // active_open_mic 3B2: detonate existing shield before granting the new warcry shield.
            if (const FLDDemoEffectDefinition* Warcry=LDCA::FindEffect(*ValuesDef,ELDDemoEffectOp::WarcryRing))
            {
                if (Warcry->CostRatioBp>0 && Body.Shield.IsSet() && Body.Shield->ExpiresAtMs>Now && Body.Resources.ShieldP2>0)
                {
                    const int64 OldShieldP2=Body.Resources.ShieldP2;
                    Body.Resources.ShieldP2=0;
                    Body.Shield.Reset();
                    Body.ShieldLayers.Reset();
                    ++Body.Resources.Revision;
                    const int64 DetonateP2=OldShieldP2/10000*Warcry->CostRatioBp+OldShieldP2%10000*Warcry->CostRatioBp/10000;
                    const FGuid DetonateId=FGuid::NewGuid();
                    for (const FLDCombatBodyRef& TargetRef : AreaTargets)
                    {
                        FLDCombatHit Det;
                        Det.HitId=FGuid::NewGuid();
                        Det.EffectId=DetonateId;
                        Det.Target=TargetRef;
                        Det.SourceBody=SelfRef;
                        Det.SourceEntity=Body.Victim.EntityId;
                        Det.CreditOwner=Body.Victim.Kind==ELDVictimKind::Contender ? Body.Victim.EntityId : FGuid();
                        Det.Source=Source;
                        Det.HitClass=ELDCombatHitClass::Ability;
                        Det.bAddedDamage=true;
                        Det.BaseP2=DetonateP2;
                        Det.DamageType=ELDDemoDamageType::Magical;
                        Det.SourceADP2=Final.ADP2;
                        Det.SourceAPP2=Final.APP2;
                        PendingHits.Add(MoveTemp(Det));
                    }
                }
            }
            // Base + AP share + AD share + own max-HP share + resist share, through the single shield entry (StackToLatestExpiry).
            int64 Amount=Effect.BaseP2+Effect.APRatioBp*Final.APP2/10000+Effect.ADRatioBp*Final.ADP2/10000
                +MaxHP/10000*Effect.MaxHPRatioBp+MaxHP%10000*Effect.MaxHPRatioBp/10000
                +ResistSum/10000*Effect.ResistRatioBp+ResistSum%10000*Effect.ResistRatioBp/10000;
            if (const FLDDemoEffectDefinition* Warcry=LDCA::FindEffect(*ValuesDef,ELDDemoEffectOp::WarcryRing))
            {
                if (Warcry->RatioBp>0 && !AreaTargets.IsEmpty())
                {
                    const int64 ScaleBp=10000+int64(AreaTargets.Num())*Warcry->RatioBp;
                    Amount=Amount/10000*ScaleBp+Amount%10000*ScaleBp/10000;
                }
            }
            if (!GrantShieldTo(Body,Amount,Effect.DurationMs,EffectId,Body.Victim.EntityId,Now,Error,Source)) { return false; }
            break;
        }
        case ELDDemoEffectOp::DispelRoot:
        case ELDDemoEffectOp::DispelSlow:
            // Resolved before this loop.
            break;
        case ELDDemoEffectOp::Dash:
        {
            // Dash cards that read a contact of this dash (D01 reset, D02 cuts, D06 phase strikes, D12 shock).
            TArray<FLDPassiveEffectRef> Resets, Hastes, Phases, Shocks, Trains;
            if (!CardsOn(Body.AttributeProjection,ELDDemoEffectOp::DashContactReset,Source,Resets,Error)
                || !CardsOn(Body.AttributeProjection,ELDDemoEffectOp::HitHasteOther,Source,Hastes,Error)
                || !CardsOn(Body.AttributeProjection,ELDDemoEffectOp::DashPhase,Source,Phases,Error)
                || !CardsOn(Body.AttributeProjection,ELDDemoEffectOp::DashContactElement,Source,Shocks,Error)
                || !CollectPassiveEffects(Body.AttributeProjection,ELDDemoEffectOp::RunawayTrain,Trains,Error))
            { return false; }
            double Distance=LDCA::ToDouble(bInnate ? Innate.DistanceCm : Effect.DistanceCm);
            // Spec §8.3: a dash from an enemy's ice goes GROUND.IceDisplacementScale as far.
            if (Body.bOnEnemyIce) { Distance*=double(Content->Get().Parameters.Ground.IceDisplacementScaleBp)/10000.; }
            const double PlannedDistance=Distance;
            if (bInnate && Phases.IsEmpty())
            {
                FVector EnemyCenterCm=FVector::ZeroVector;
                bool bHasEnemy=false;
                if (const auto* PC=Mode->FindContenderController(Body.Victim.EntityId); PC && PC->Navigation)
                {
                    bHasEnemy=PC->Navigation->ReadPinnedTargetFoot(EnemyCenterCm);
                }
                if (!bHasEnemy && Body.LastAttackTarget.IsValid())
                {
                    ALDCharacter* TargetActor=nullptr;
                    if (const auto* TargetState=FindBody(Body.LastAttackTarget,&TargetActor);
                        TargetState && TargetActor && !TargetState->bPendingDeath)
                    {
                        EnemyCenterCm=FootOf(*TargetActor);
                        bHasEnemy=true;
                    }
                }
                if (bHasEnemy)
                {
                    Distance=LDCombatPreInput::ClampInnateDashDistance(Distance,ReleaseFoot,DeliveryAction.Direction,EnemyCenterCm,false);
                }
            }
            const bool bClampedZeroStop=Distance<=0.;
            const int64 DashDurationMs=bClampedZeroStop ? 1
                : PlannedDistance>0. && Distance<PlannedDistance
                    ? FMath::Max<int64>(1,FMath::RoundToInt64(double(Effect.DurationMs)*(Distance/PlannedDistance)))
                    : Effect.DurationMs;
            if (!bClampedZeroStop)
            {
                const double Seconds=double(DashDurationMs)/1000.;
                if (!Movement->StartDemoDash(DeliveryAction.Direction,Distance,Seconds,Error)) { return false; }
            }
            if (!LDMatchLifecycle::AddDurationMs(Now,DashDurationMs,Body.DashUntilMs,Error)) { return false; }
            Body.DashContact.Reset();
            if (Body.AttributeProjection.Keystones.bAgility)
            {
                FLDCombatTimedBonus AgilityHaste;
                AgilityHaste.EffectId=FGuid::NewGuid();
                AgilityHaste.SourceDefinition=FName(TEXT("keystone_agility_haste"));
                FLDDemoStatBonus& MoveStat=AgilityHaste.Stats.AddDefaulted_GetRef();
                MoveStat.Stat=ELDDemoBonusStat::MoveSpeedBonusRatio;
                MoveStat.Amount=2500;
                if (!LDMatchLifecycle::AddDurationMs(Now,1500,AgilityHaste.ExpiresAtMs,Error)) { return false; }
                LDTactical::ApplyTimedBonus(Body.TimedBonuses,AgilityHaste);
                bTimedBonus=true;
            }
            // B12: a voluntary dash (D or an active's) that completes naturally arms DashComplete empowers.
            FLDCombatVoluntaryDash& Voluntary=Body.VoluntaryDash.Emplace();
            Voluntary.EffectId=EffectId;
            Voluntary.Source=Source;
            Voluntary.StartFootCm=ReleaseFoot;
            Voluntary.EndMs=Body.DashUntilMs;
            Voluntary.bClampedStop=bClampedZeroStop;
            const bool bCards=!Resets.IsEmpty() || !Hastes.IsEmpty() || !Phases.IsEmpty() || !Shocks.IsEmpty() || !Trains.IsEmpty();
            if (bCards || LDCA::FindEffect(*ValuesDef,ELDDemoEffectOp::DashContact))
            {
                Body.DashContact=MakeShared<FLDCombatDashContact>();
                Body.DashContact->EffectId=EffectId;
                Body.DashContact->Body=SelfRef;
                Body.DashContact->Source=Source;
                Body.DashContact->StartMs=Now;
                Body.DashContact->EndMs=Body.DashUntilMs;
                Body.DashContact->PaidMomentumP2=PaidP2;
                Body.DashContact->bCanDamage=false;
                Body.DashContact->bPhase=!Phases.IsEmpty();
                Body.DashContact->bReset=!Resets.IsEmpty();
                Body.DashContact->Direction=DeliveryAction.Direction;
            }
            break;
        }
        case ELDDemoEffectOp::EmpowerBasic:
        {
            // S11/S17: the package is fixed at release (spec §10.2).
            FLDCombatEmpower Armed;
            if (!MakeEmpower(Body,Source,Effect,EffectId,Now,Armed,Error)) { return false; }
            TArray<FLDDemoStatBonus> ChargeStats=Effect.Stats;
            if (Terms.EmpowerReach.IsSet())
            {
                // M19: a weaker package that lends basic range while it is pending (the same charge bonus).
                const int64 Bp=Terms.EmpowerReach->Effect.MultiplierBp;
                Armed.AmountP2=Armed.AmountP2/10000*Bp+Armed.AmountP2%10000*Bp/10000;
                FLDDemoStatBonus& Reach=ChargeStats.AddDefaulted_GetRef();
                Reach.Stat=ELDDemoBonusStat::AttackRangeCmFlat;
                Reach.Cm=Terms.EmpowerReach->Effect.DistanceCm;
            }
            // #147: several charges and the stats held while charges remain (they end with the last charge or the window).
            if (!ChargeStats.IsEmpty())
            {
                FLDCombatTimedBonus Bonus;
                Bonus.EffectId=EffectId;
                Bonus.SourceDefinition=Source.DefinitionId;
                Bonus.Stats=ChargeStats;
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
        {
            TArray<FLDCombatBodyRef> Taunted;
            if (!ApplyTauntEffect(Actor,Body,Effect,EffectId,Source,Error,&Taunted)) { return false; }
            if (!Terms.PullTerms.IsSet()) { break; }
            // M20 on S15: every body the taunt landed on is pulled toward the caster as a knockback of S15.
            const FLDDemoEffectDefinition& Pull=Terms.PullTerms.GetValue();
            for (const FLDCombatBodyRef& Target : Taunted)
            {
                FLDCombatHit& Hit=PendingHits.AddDefaulted_GetRef();
                Hit.HitId=FGuid::NewGuid();
                Hit.EffectId=EffectId;
                Hit.Token=Action.Token;
                Hit.Target=Target;
                Hit.SourceBody=SelfRef;
                Hit.SourceEntity=Body.Victim.EntityId;
                Hit.CreditOwner=Body.Victim.Kind==ELDVictimKind::Contender ? Body.Victim.EntityId : FGuid();
                Hit.Source=Source;
                Hit.HitClass=ELDCombatHitClass::Ability;
                Hit.KnockbackCm=LDCA::ToDouble(Pull.DistanceCm);
                Hit.KnockbackMs=Pull.DurationMs;
                Hit.KnockbackAnchorCm=ReleaseFoot;
                Hit.KnockbackSource=Source;
                Hit.bKnockbackPull=true;
                Hit.KnockbackStopCm=LDCA::ToDouble(Pull.RadiusCm);
            }
            // S15 B1: when the pull gathers two or more bodies, each takes the pull's physical package.
            if (Taunted.Num()>=2 && (Pull.BaseP2>0 || Pull.ADRatioBp>0))
            {
                for (int32 Index=PendingHits.Num()-Taunted.Num(); Index<PendingHits.Num(); ++Index)
                {
                    FLDCombatHit& Pulled=PendingHits[Index];
                    Pulled.BaseP2=Pull.BaseP2;
                    Pulled.ADRatioBp=Pull.ADRatioBp;
                    Pulled.SourceADP2=Final.ADP2;
                    Pulled.DamageType=ELDDemoDamageType::Physical;
                    Pulled.bAddedDamage=true;
                }
            }
            break;
        }
        case ELDDemoEffectOp::Silence:
        case ELDDemoEffectOp::Breach:
        case ELDDemoEffectOp::Burn:
        case ELDDemoEffectOp::Wet:
        case ELDDemoEffectOp::Electrify:
        case ELDDemoEffectOp::Rot:
        case ELDDemoEffectOp::Chill:
        case ELDDemoEffectOp::GroundState:
        case ELDDemoEffectOp::ChainBounce:
            // Carried by the hit payload (and the S10 chain, a projectile's landing or a cone's area) below.
            break;
        case ELDDemoEffectOp::Fan:
        case ELDDemoEffectOp::Bag:
            // N2's fan and N4's bag stand at the target point (StartSkillDelivery).
            break;
        // Skill branch operations: hit riders on the cast ledger and self terms (ArmBranchRelease), pulses and shield
        // bonuses (ArmBranchHostile), zone terms (ReleaseMechanics), trap terms and lingering areas (StartSkillDelivery).
        case ELDDemoEffectOp::Vulnerable:
        case ELDDemoEffectOp::NextBasicAmp:
        case ELDDemoEffectOp::MomentumGain:
        case ELDDemoEffectOp::MomentumSteal:
        case ELDDemoEffectOp::FollowUp:
        case ELDDemoEffectOp::Recast:
        case ELDDemoEffectOp::DashBack:
        case ELDDemoEffectOp::SelfBuff:
        case ELDDemoEffectOp::BurnBurst:
        case ELDDemoEffectOp::Pulse:
        case ELDDemoEffectOp::ZoneGuard:
        case ELDDemoEffectOp::ZoneMomentum:
        case ELDDemoEffectOp::Overcharge:
        case ELDDemoEffectOp::FollowBasic:
        case ELDDemoEffectOp::ShieldBurstBreak:
        case ELDDemoEffectOp::BasicRider:
        case ELDDemoEffectOp::DelayedBurst:
        case ELDDemoEffectOp::ShieldReflect:
        case ELDDemoEffectOp::ShieldEndHeal:
        case ELDDemoEffectOp::ShieldHitBonus:
        case ELDDemoEffectOp::BraceStun:
        case ELDDemoEffectOp::BraceHeal:
        case ELDDemoEffectOp::BraceStack:
        case ELDDemoEffectOp::BraceShove:
        case ELDDemoEffectOp::BraceShield:
            break;
        case ELDDemoEffectOp::ParryStance:
            Body.ActiveParry=Effect;
            Body.ParryEffectId=EffectId;
            Body.ParrySource=Source;
            Body.bParryTriggered=false;
            if (!LDMatchLifecycle::AddDurationMs(Now,Effect.DurationMs,Body.ParryUntilMs,Error)) { return false; }
            break;
        case ELDDemoEffectOp::WarcryRing:
        {
            // Clear enemy non-return projectiles inside the warcry ring.
            const double ClearRadius=EffectiveSelfAreaRadiusCm>0. ? EffectiveSelfAreaRadiusCm : 340.;
            Projectiles.RemoveAll([&](const FLDCombatProjectile& Proj)
            {
                if (Proj.SourceEntity==Body.Victim.EntityId || Proj.bReturn || IsIsolated(Proj.SourceEntity,Body)) { return false; }
                const FVector2D Heading(Proj.Direction.X,Proj.Direction.Y);
                const FVector Pos=Proj.OriginCm+FVector(Heading.GetSafeNormal().X,Heading.GetSafeNormal().Y,0)*Proj.TravelledCm;
                return FVector::Dist2D(ReleaseFoot,Pos)<=ClearRadius+Proj.RadiusCm;
            });
            break;
        }
        case ELDDemoEffectOp::FrenzyWaves:
        {
            Body.ActiveFrenzy=Effect;
            Body.FrenzyEffectId=EffectId;
            Body.FrenzySource=Source;
            if (!LDMatchLifecycle::AddDurationMs(Now,Effect.DurationMs,Body.FrenzyUntilMs,Error)) { return false; }
            if (!Effect.Stats.IsEmpty())
            {
                FLDCombatTimedBonus FrenzyBuff;
                FrenzyBuff.EffectId=EffectId;
                FrenzyBuff.SourceDefinition=Source.DefinitionId;
                FrenzyBuff.Stats=Effect.Stats;
                FrenzyBuff.ExpiresAtMs=Body.FrenzyUntilMs;
                LDTactical::ApplyTimedBonus(Body.TimedBonuses,FrenzyBuff);
                bTimedBonus=true;
            }
            break;
        }
        case ELDDemoEffectOp::GrabLine:
        case ELDDemoEffectOp::BackstabLunge:
        case ELDDemoEffectOp::SlipperPickup:
        case ELDDemoEffectOp::CordonLine:
            break;
        case ELDDemoEffectOp::Brace:
            if (!ReleaseBrace(Body,Effect,Source,EffectId,Action.Token,Now,Error,CastLedger,
                Action.bAutoRecast && Action.bWholeEffectShare ? Action.AutoRecastScaleBp : 10000)) { return false; }
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
    if (!ArmBranchRelease(Actor,Body,Source,Terms,EffectId,CastLedger,DeliveryAction,Now,Error)) { return false; }
    if (bHostile)
    {
        // One hit payload per target, snapshotted at release; a skill's own delivery takes it, or the standard one does.
        FLDCombatHit Template;
        if (!BuildReleasePayload(Body,Source,Terms,ReleaseFoot,EffectId,Action.Token,CastLedger,Template,Error)) { return false; }
        bool bHandled=false;
        if (!StartSkillDelivery(Body,Terms,DeliveryAction,Template,Now,bHandled,Error)) { return false; }
        if (!bHandled && !DeliverReleasePayload(Actor,Body,Terms,DeliveryAction,Template,UnitTarget,TargetFoot,ReleaseFoot,AreaTargets,Now,Error))
        { return false; }
        if (!ArmBranchHostile(Body,Terms,Template,AreaTargets,Now,Error)) { return false; }
    }
    // A ground zone sits on the landed ground point, else at the requested point clamped to the authored reach.
    FVector ZonePoint=Action.TargetPoint;
    if (LandedPoint.IsSet()) { ZonePoint=*LandedPoint; }
    else if (TargetDef.Mode==ELDDemoTargetMode::GroundPoint)
    {
        const FVector Offset(ZonePoint.X-ReleaseFoot.X,ZonePoint.Y-ReleaseFoot.Y,0.);
        const double Reach=GroundReachCm;
        ZonePoint=Offset.Size2D()>Reach ? ReleaseFoot+Offset.GetSafeNormal2D()*Reach : FVector(ZonePoint.X,ZonePoint.Y,ReleaseFoot.Z);
    }
    if (!ReleaseMechanics(Body,Source,Terms,ReleaseFoot,ZonePoint,Action.Direction,EffectId,Action.Token,CastLedger,Error)) { return false; }
    // Every damage pulse of this release's zone carries its package (its factors ride on the cast ledger).
    for (FLDCombatZone& Zone : Zones)
    {
        if (Zone.EffectId==EffectId && Zone.bDamage) { Zone.PackageScaleBp=Terms.PackageScaleBp; }
    }
    // K06: the repeats of this release.
    if (!ScheduleAutoRecasts(Body,Source,Terms,Action,Now,Error)) { return false; }
    if (bDealsDamage) { MarkEngaged(Body,Now); }
    // #198: an offensive release is a combat action of the build safe window; a self buff, shield or plain dash is not.
    if (LDCombatSourceRules::IsOffensive(*ValuesDef)) { MarkBuildUnsafe(Body.Victim.EntityId,Now); }
    if (bTimedBonus && !RefreshTemporary(Body,&Actor,Now,Error)) { return false; }
    ++Body.StateRevision;
    MarkViewChanged(Body.Victim.EntityId);
    return true;
}

bool ALDCombatDirector::BuildReleasePayload(const FLDBodyCombatState& Body, const FLDCombatSourceRef& Source,
    const FLDCombatActiveTerms& Terms, const FVector& ReleaseFoot, const FGuid& EffectId, uint64 Token,
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
    const FLDDemoEffectDefinition* RotEffect=LDCA::FindEffect(Values,ELDDemoEffectOp::Rot);
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
    // The release's package multiplier of this native damage (a K06 repeat's share).
    Template.PackageScaleBp=Terms.PackageScaleBp;
    const int64 ResistSum=Final.ArmorP2+Final.MagicResistP2;
    const int64 ResistShareP2=(DamageEffect && DamageEffect->ResistRatioBp>0 && ResistSum>0)
        ? (ResistSum/10000*DamageEffect->ResistRatioBp+ResistSum%10000*DamageEffect->ResistRatioBp/10000) : 0;
    Template.BaseP2=(DamageEffect ? DamageEffect->BaseP2 : 0)+ResistShareP2;
    Template.MonsterMultiplierBp=DamageEffect ? DamageEffect->CostRatioBp : 0;
    Template.ADRatioBp=DamageEffect ? DamageEffect->ADRatioBp : 0;
    Template.APRatioBp=DamageEffect ? DamageEffect->APRatioBp : 0;
    Template.AdaptiveRatioBp=DamageEffect ? DamageEffect->AdaptiveRatioBp : 0;
    if (const FLDDemoEffectDefinition* DashEf=LDCA::FindEffect(Values,ELDDemoEffectOp::Dash))
    {
        Template.SourceDashDistanceCm=LDCA::ToDouble(DashEf->DistanceCm);
    }
    if (const FLDDemoEffectDefinition* Backstab=LDCA::FindEffect(Values,ELDDemoEffectOp::BackstabLunge))
    {
        Template.BackstabTerms=*Backstab;
        Template.bTriggersBasicOnHit=true;
    }
    if (LDCA::FindEffect(Values,ELDDemoEffectOp::SlipperPickup))
    {
        Template.bTriggersBasicOnHit=true;
    }
    const int64 BonusADP2=LDTactical::BonusADOf(Catalog()->Get().Parameters,GrowthLevelOf(Body),Final.ADP2);
    Template.DamageType=DamageEffect
        ? LDTactical::EffectiveDamageType(DamageEffect->DamageType,DamageEffect->AdaptiveRatioBp,BonusADP2,Final.APP2)
        : ELDDemoDamageType::Physical;
    Template.SourceADP2=Final.ADP2;
    Template.SourceAPP2=Final.APP2;
    Template.SourceBonusADP2=BonusADP2;
    Template.MaxHPRatioBp=DamageEffect ? DamageEffect->MaxHPRatioBp : 0;
    // Damage scales with the max HP above the character's own base and level growth.
    Template.SourceMaxHPP2=BonusMaxHpP2Of(Body);
    Template.RootDurationMs=RootEffect ? RootEffect->DurationMs : 0;
    Template.SlowBp=SlowEffect ? SlowEffect->RatioBp : 0;
    Template.SlowDurationMs=SlowEffect ? SlowEffect->DurationMs : 0;
    Template.DisarmDurationMs=DisarmEffect ? DisarmEffect->DurationMs : 0;
    Template.StunDurationMs=StunEffect ? StunEffect->DurationMs : 0;
    // #147 keyword payload: silence, shared breach and burn stacks (the burn snapshotted now).
    Template.SilenceDurationMs=SilenceEffect ? SilenceEffect->DurationMs : 0;
    Template.BreachStacks=BreachEffect ? BreachEffect->Count : 0;
    Template.BurnStacks=BurnEffect ? BurnEffect->Count : 0;
    // Elements of every hit of this release, with the caster's snapshot now (a rot's own cut and duration).
    for (const TPair<ELDDemoEffectOp,ELDCombatElement>& Row : {TPair<ELDDemoEffectOp,ELDCombatElement>(ELDDemoEffectOp::Wet,ELDCombatElement::Water),
        {ELDDemoEffectOp::Electrify,ELDCombatElement::Shock}, {ELDDemoEffectOp::Chill,ELDCombatElement::Chill},
        {ELDDemoEffectOp::Rot,ELDCombatElement::Rot}})
    {
        if (LDCA::FindEffect(Values,Row.Key)) { Template.NativeElements|=LDCombatElements::Bit(Row.Value); }
    }
    if (!StampElementSnapshot(Body,Template,Error)) { return false; }
    Template.RotCutBp=RotEffect ? RotEffect->RatioBp : 0;
    Template.RotMs=RotEffect ? RotEffect->DurationMs : 0;
    // Abilities trigger no on-hit effects unless explicitly dual-classified (active_crotch_grab, active_snark_bolt).
    if (!StampSourceTerms(Body,Template,Error) || !SnapshotOnHit(Body,Template.bTriggersBasicOnHit ? 10000 : 0,Template,Error)) { return false; }
    FillMechanicsTemplate(Values,ReleaseFoot,Template);
    // Y08 reads the push of this exit; a branch's wall stun rides on it.
    if (Template.KnockbackCm>0) { Template.KnockbackSource=Source; }
    if (const FLDDemoEffectDefinition* Push=LDCA::FindEffect(Values,ELDDemoEffectOp::Knockback)) { Template.KnockbackWallStunMs=Push->ExtraDurationMs; }
    if (Template.KnockbackCm>0 && Terms.PullTerms.IsSet())
    {
        // M20: toward the caster's current foot (the release foot if the caster is gone), clamped short of it.
        Template.bKnockbackPull=true;
        Template.bKnockbackFromAnchor=false;
        Template.KnockbackStopCm=LDCA::ToDouble(Terms.PullTerms->RadiusCm);
    }
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
        Projectile.AdaptiveRatioBp=Template.AdaptiveRatioBp;
        Projectile.DamageType=Template.DamageType;
        Projectile.SourceADP2=Template.SourceADP2;
        Projectile.SourceAPP2=Template.SourceAPP2;
        Projectile.SourceBonusADP2=Template.SourceBonusADP2;
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
        // A branch pierce (extra distinct bodies) and return (a flight home where it ends).
        if (Terms.ProjectilePierce.IsSet())
        {
            Projectile.PierceRemaining=Terms.ProjectilePierce->Effect.Count;
            Projectile.PierceScaleBp=Terms.ProjectilePierce->Effect.MultiplierBp;
        }
        if (Terms.ProjectileReturn.IsSet())
        {
            Projectile.ReturnScaleBp=Terms.ProjectileReturn->Effect.MultiplierBp;
            Projectile.ReturnElement=Terms.ProjectileReturn->Effect.Element;
        }
        if (const FLDDemoEffectDefinition* Landing=LDCA::FindEffect(Terms.Values,ELDDemoEffectOp::GroundState)) { Projectile.LandingGround=*Landing; }
        Projectile.Payload=Template;
        Projectiles.Add(Projectile);
    }
    else if (TargetDef.Mode==ELDDemoTargetMode::Unit)
    {
        Deliver(UnitTarget);
        const FLDDemoEffectDefinition* ChainEffect=LDCA::FindEffect(Terms.Values,ELDDemoEffectOp::ChainBounce);
        if (ChainEffect && !StartChain(Template,UnitTarget,TargetFoot,*ChainEffect,NowMs,Error)) { return false; }
    }
    else if (TargetDef.Mode==ELDDemoTargetMode::DirectionLine)
    {
        // #198 S09 / #345 S02: every legal body in the band (or only the closest first enemy for grab_line).
        TArray<TPair<FLDCombatBodyRef,FVector>> Line;
        // The release range (U05 eagle stance included), the reach the order approached with.
        double LineRangeCm=0;
        if (!CastRangeCm(Body,TargetDef,LineRangeCm,Error)) { return false; }
        if (!CollectLineTargets(ReleaseFoot,Action.Direction,LineRangeCm,
            LDCA::ToDouble(TargetDef.LineHalfWidthCm),Body,Line,Error)) { return false; }
        const FVector Along=FVector(Action.Direction.X,Action.Direction.Y,0).GetSafeNormal();
        const FLDDemoEffectDefinition* Grab=LDCA::FindEffect(Terms.Values,ELDDemoEffectOp::GrabLine);
        if (Grab)
        {
            Line.Sort([&ReleaseFoot,&Along](const TPair<FLDCombatBodyRef,FVector>& A, const TPair<FLDCombatBodyRef,FVector>& B)
            {
                return FVector::DotProduct(A.Value-ReleaseFoot,Along)<FVector::DotProduct(B.Value-ReleaseFoot,Along);
            });
            if (!Line.IsEmpty()) { Line.SetNum(1); }
        }
        if (Grab && Line.IsEmpty() && Grab->RatioBp>0 && Grab->DurationMs>0)
        {
            TArray<TPair<FLDCombatBodyRef,FVector>> MissBand;
            if (!CollectLineTargets(ReleaseFoot,Action.Direction,LineRangeCm,
                LDCA::ToDouble(TargetDef.LineHalfWidthCm)*1.8,Body,MissBand,Error)) { return false; }
            for (const TPair<FLDCombatBodyRef,FVector>& Swept : MissBand)
            {
                FLDCombatHit Gust=Template;
                Gust.HitId=FGuid::NewGuid();
                Gust.Target=Swept.Key;
                Gust.BaseP2=0; Gust.ADRatioBp=0; Gust.APRatioBp=0; Gust.AdaptiveRatioBp=0; Gust.MaxHPRatioBp=0;
                Gust.RootDurationMs=0; Gust.BreachStacks=0; Gust.KnockbackCm=0; Gust.KnockbackMs=0;
                Gust.SlowBp=Grab->RatioBp; Gust.SlowDurationMs=Grab->DurationMs;
                PendingHits.Add(MoveTemp(Gust));
            }
        }
        for (const TPair<FLDCombatBodyRef,FVector>& Struck : Line)
        {
            FLDCombatHit Hit=Template;
            Hit.HitId=FGuid::NewGuid();
            Hit.Target=Struck.Key;
            if (Grab && Grab->ExtraDurationMs>0 && Hit.RootDurationMs>0)
            {
                ALDCharacter* StruckActor=nullptr;
                if (const FLDBodyCombatState* StruckState=FindBody(Struck.Key,&StruckActor))
                {
                    const bool bMovingOrDashing=(StruckState->DashUntilMs>NowMs)
                        || (StruckActor && StruckActor->GetVelocity().Size2D()>5.);
                    if (bMovingOrDashing) { Hit.RootDurationMs+=Grab->ExtraDurationMs; }
                }
            }
            if (Hit.KnockbackCm>0 && !Hit.bKnockbackPull) { Hit.bKnockbackFromAnchor=true; Hit.KnockbackAnchorCm=Struck.Value-Along*100.; }
            PendingHits.Add(MoveTemp(Hit));
        }
    }
    else
    {
        const FLDDemoEffectDefinition* Around=LDCA::FindEffect(Terms.Values,ELDDemoEffectOp::SelfArea);
        // The same circle the release collected its targets in.
        const double AroundRadius=Around
            ? LDCA::ToDouble(Around->RadiusCm)*HulkingAreaScaleOf(Body,Terms.AreaBp,WarcryAreaBpOf(Body,Terms.Values)) : 0.;
        for (const FLDCombatBodyRef& Target : AreaTargets) { Deliver(Target); }
        if (Around && !HitBagsInCircle(ReleaseFoot, AroundRadius, Template, NowMs, Error)) { return false; }
        if (Around) { HitFenceInCircle(ReleaseFoot, AroundRadius, Template); }
        // M15 on S07: a blast of the same radius at the release foot after the delay, with one burn stack.
        const FLDDemoEffectDefinition* AreaEffect=LDCA::FindEffect(Terms.Values,ELDDemoEffectOp::SelfArea);
        if (Terms.EchoImpact.IsSet() && AreaEffect)
        {
            FLDCombatGroundEffect Blast;
            Blast.Payload=Template;
            Blast.Payload.BurnStacks=1;
            Blast.CentreCm=ReleaseFoot;
            Blast.RadiusCm=Blast.InnerRadiusCm=AroundRadius;
            if (!ScheduleEcho(Template.SourceBody,Terms.EchoImpact.GetValue(),Blast,NowMs,Error)) { return false; }
        }
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
    Out.MomentumP2=Body->Resources.MomentumP2;
    Out.MaxMomentumP2=Catalog() ? Catalog()->Get().Parameters.Momentum.MaxP2 : 0;
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
    Out.bMovementAllowed=!Body->bPendingDeath && Now>=Body->MonsterRecoverUntilMs && !IsActionBlocked(*Body,ELDCombatControlAction::Move);
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

float ALDCombatDirector::MoveAccelScaleOf(const FLDBodyCombatState& Body) const
{
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!Content || !Body.bOnEnemyIce) { return 1.f; }
    return float(double(Content->Get().Parameters.Ground.IceAccelScaleBp)/10000.);
}

float ALDCombatDirector::ReadMoveAccelScale(const ALDCharacter& Actor) const
{
    const auto* Body=Bodies.Find(const_cast<ALDCharacter*>(&Actor));
    return Body && !Body->bPendingDeath ? MoveAccelScaleOf(*Body) : 1.f;
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

bool ALDCombatDirector::ReadDirectRequestCursor(const FGuid& Entity,uint64& Through,FString& Error) const
{
    const FLDCombatEntityRecord* Record=Records.Find(Entity);
    if(!Record){Error=TEXT("Reconnect direct cursor has no current combat record.");return false;}
    Through=FMath::Max(Record->DirectHighWater,Record->CancelledThrough);
    if(Through==MAX_uint64){Error=TEXT("Reconnect direct cursor exhausted its sequence.");return false;}
    Error.Reset();return true;
}
