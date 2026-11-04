#include "Combat/LDCombatDirector.h"
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
    { return Entry==ELDCombatEntry::Q || Entry==ELDCombatEntry::W || Entry==ELDCombatEntry::Item1 || Entry==ELDCombatEntry::Item2; }

    bool SameTerms(const FLDCombatActionRequest& A, const FLDCombatActionRequest& B)
    {
        return A.Generation==B.Generation && A.Life==B.Life && A.DirectSequence==B.DirectSequence && A.Entry==B.Entry
            && A.Target.Kind==B.Target.Kind && A.Target.Observed==B.Target.Observed
            && A.Target.ObservationEpoch==B.Target.ObservationEpoch
            && FVector(A.Target.Direction).Equals(FVector(B.Target.Direction),0.0001)
            && A.Target.ServerBody==B.Target.ServerBody && A.ExpectedSourceRevision==B.ExpectedSourceRevision;
    }

    bool ValidDirection(const FVector& Direction)
    {
        return !Direction.ContainsNaN() && FMath::Abs(Direction.Z)<=0.01
            && FMath::IsNearlyEqual(Direction.Size2D(),1.,0.02);
    }

    const FLDDemoRankDefinition* FindRank(const FLDDemoAbilityDefinition& Ability, uint8 Rank)
    { return Ability.Ranks.FindByPredicate([Rank](const FLDDemoRankDefinition& Candidate) { return Candidate.Rank==Rank; }); }

    const FLDDemoEffectDefinition* FindEffect(const FLDDemoRankDefinition& Rank, ELDDemoEffectOp Op)
    { return Rank.Effects.FindByPredicate([Op](const FLDDemoEffectDefinition& Effect) { return Effect.GetOp()==Op; }); }
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
    UE_LOG(LogTemp,Verbose,TEXT("LD_COMBAT_ACTION_END token=%llu released=%d reason=%u"),Body.Action->Token,bReleased,uint8(Reason));
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

FLDCombatActionReceipt ALDCombatDirector::SubmitAction(const FLDCombatCaller& Caller, const FLDCombatActionRequest& Request)
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
    default:
        Shape=false;
    }
    if (!Shape) { return Refuse(ELDCombatReject::InvalidRequest); }
    if (Request.DirectSequence<=Record->DirectHighWater)
    {
        if (const auto* Known=Record->Recent.FindByPredicate([&Request](const FLDCombatDirectEntry& Entry)
            { return Entry.Request.DirectSequence==Request.DirectSequence; }))
        { return LDCA::SameTerms(Known->Request,Request) ? Known->Receipt : Refuse(ELDCombatReject::RequestConflict); }
        return Refuse(ELDCombatReject::ReceiptExpired);
    }
    Record->DirectHighWater=Request.DirectSequence;
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
    if (!Timing.IsSuccess() || Record->NextToken==MAX_uint64
        || !LDMatchLifecycle::AddDurationMs(Now,Timing.Value->WindupMs,ReleaseAt,Error)
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
    // The committed cadence survives cancellation, orders, death and respawn; time keeps running.
    Record->NextAttackAtMs=NextAttack;
    // An accepted swing reveals a stealthed attacker before it lands.
    RevealStealth(*Body);
    MarkViewChanged(Caller.EntityId);
    Receipt.CombatToken=Action.Token;
    Receipt.Phase=ELDCombatPhase::Accepted;
    Receipt.Reject=ELDCombatReject::None;
    return Receipt;
}

void ALDCombatDirector::CancelNavAttack(const FGuid& Entity, const FLDMoveIdentity& Move)
{
    auto* Body=FindEntityBody(Entity);
    if (Body && Body->Action.IsSet() && Body->Action->Kind==ELDCombatActionKind::Basic
        && Body->Action->Key.Origin==ELDCombatRequestOrigin::Nav && Body->Action->Key.Move==Move)
    { FinishAction(*Body,ELDCombatReject::Cancelled,false); }
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
    if (Record.Kind==ELDVictimKind::Contender)
    {
        // Every admitted Q/W/1/2 attempt is an aggressive intent: protection ends before the action is checked.
        auto* PC=Mode->FindContenderController(Record.Entity);
        if (!PC) { return Refuse(ELDCombatReject::NotOwner); }
        FLDAggressiveIntent Intent;
        Intent.Kind=Request.Entry==ELDCombatEntry::Item1 || Request.Entry==ELDCombatEntry::Item2
            ? ELDAggressiveIntentKind::ActivateEquipment : ELDAggressiveIntentKind::ActivateCharacter;
        Intent.Request={ELDAggressionOrigin::Combat,Request.DirectSequence,0};
        const auto Protection=Mode->EndProtectionForAggressiveIntent(*PC,Mode->GetMatchContext(),SelfRef,Intent,Permit);
        if (Protection.Outcome==ELDProtectionOutcome::Rejected)
        { return Refuse(Protection.Reason==ELDCommitReject::StaleLife ? ELDCombatReject::StaleLife : ELDCombatReject::MatchClosed); }
        Receipt.bProtectionRemoved=Protection.bProtectionRemoved;
        Receipt.LifeRevision=Protection.LifeRevision;
        // A taunted body may only basic-attack its taunter: no ability or item until the taunt ends.
        if (LDControl::IsTaunted(Body->Taunt,Now)) { return Refuse(ELDCombatReject::ControlBlocked); }
    }
    if (Record.Kind==ELDVictimKind::CampMember)
    {
        // Registered camp basic attack: same body, cadence, range, sight and hit pipeline as contenders.
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
        Receipt.CombatToken=Action.Token;
        Receipt.Phase=ELDCombatPhase::Accepted;
        Receipt.Reject=ELDCombatReject::None;
        return true;
    }
    FLDCombatSourceRef Source;
    uint8 Rank=0;
    const FLDDemoAbilityDefinition* Ability=nullptr;
    const FLDDemoEquipmentDefinition* Equipment=nullptr;
    if (!ResolveEntry(Record,Request.Entry,Source,Rank,Ability,Equipment)) { return Refuse(ELDCombatReject::SourceUnavailable); }
    if (Request.ExpectedSourceRevision!=Record.SourceRevision) { return Refuse(ELDCombatReject::StaleSource); }
    if (Equipment && !Equipment->HasActive) { return Refuse(ELDCombatReject::NoActiveEffect); }
    const FLDDemoRankDefinition* RankDef=Ability ? LDCA::FindRank(*Ability,Rank) : Equipment ? &Equipment->Active.Rank : nullptr;
    const FLDDemoTargetDefinition* TargetDef=Ability ? &Ability->Target : Equipment ? &Equipment->Active.Target : nullptr;
    const int64 WindupMs=Ability ? Ability->WindupMs : Equipment->Active.WindupMs;
    if (!RankDef || !TargetDef) { return Refuse(ELDCombatReject::SourceUnavailable); }
    if (Body->Action.IsSet() && Body->Action->Kind==ELDCombatActionKind::Ability) { return Refuse(ELDCombatReject::Busy); }
    const auto* Track=TrackFor(Record,Request.Entry);
    if (!Track) { Error=TEXT("A held active source has no persistent cooldown track."); return false; }
    if (Track->ReadyAtMs>Now) { return Refuse(ELDCombatReject::Cooldown); }
    if (Body->Resources.MpP2<RankDef->ManaCostP2) { return Refuse(ELDCombatReject::InsufficientMana); }
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
        FVector TargetFoot;
        ELDCombatReject Reject;
        if (!ResolveUnitTarget(Record,*Actor,Request.Target,Action.TargetBody,TargetFoot,Reject)) { return Refuse(Reject); }
        const FVector Foot=FootOf(*Actor);
        if (FVector::Dist2D(Foot,TargetFoot)>LDCA::ToDouble(TargetDef->RangeCm)) { return Refuse(ELDCombatReject::OutOfRange); }
        bool bFault=false;
        if (!LDCA::ClearSight(*Mode,*GetWorld(),Foot,TargetFoot,bFault))
        {
            if (bFault) { Error=TEXT("Ability sight failed on a registered body."); return false; }
            return Refuse(ELDCombatReject::Blocked);
        }
        break;
    }
    case ELDDemoTargetMode::DirectionProjectile:
    case ELDDemoTargetMode::DirectionDash:
        if (Request.Target.Kind!=ELDCombatTargetKind::Direction || !LDCA::ValidDirection(Request.Target.Direction))
        { return Refuse(ELDCombatReject::InvalidDirection); }
        Action.Direction=FVector(Request.Target.Direction).GetSafeNormal2D();
        if (TargetDef->Mode==ELDDemoTargetMode::DirectionDash)
        {
            if (LDTactical::RootBlocks(Body->Roots,Now,Content->Get().Policies,ELDDemoRootBlock::Dash))
            { return Refuse(ELDCombatReject::ControlBlocked); }
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
    // Any accepted ability or item activation reveals a stealthed body (a new stealth is applied at its own release).
    RevealStealth(*Body);
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
    // The windup already spent the cadence; a target lost by now is a committed swing that misses.
    FLDCombatBodyRef TargetRef;
    FVector TargetFoot;
    ELDCombatReject Reject;
    if (!ResolveUnitTarget(*Record,Actor,Action.Target,TargetRef,TargetFoot,Reject) || TargetRef!=Action.TargetBody)
    { Outcome=ELDCombatReject::TargetUnavailable; FinishAction(Body,Outcome,false); return true; }
    if (!LDControl::TauntAllows(Body.Taunt,Mode->GetAuthorityNowMs(),true,TargetRef))
    { Outcome=ELDCombatReject::ControlBlocked; FinishAction(Body,Outcome,false); return true; }
    const FVector Foot=FootOf(Actor);
    if (FVector::Dist2D(Foot,TargetFoot)>LDCA::ToDouble(Body.AttributeProjection.Final.AttackRangeCm))
    { Outcome=ELDCombatReject::OutOfRange; FinishAction(Body,Outcome,false); return true; }
    bool bFault=false;
    if (!LDCA::ClearSight(*Mode,*GetWorld(),Foot,TargetFoot,bFault))
    {
        if (bFault) { Error=TEXT("Basic release sight failed on a registered body."); return false; }
        Outcome=ELDCombatReject::Blocked;
        FinishAction(Body,Outcome,false);
        return true;
    }
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
    // One critical sample per AttackId, at the successful release only.
    Hit.bCritical=Final.CritChanceBp>0 && CritRandom.RandRange(0,9999)<Final.CritChanceBp;
    for (const auto& Family : Body.AttributeProjection.EquipmentPassives)
    {
        const FLDDemoEquipmentDefinition* Equipment=nullptr;
        if (!Content->FindEquipment(Family.EquipmentId,Equipment,Error)) { return false; }
        if (!Equipment->HasPassive) { continue; }
        for (const auto& Effect : Equipment->Passive.Effects)
        {
            if (Effect.GetOp()==ELDDemoEffectOp::Cleave)
            {
                Hit.bCleave=true;
                Hit.CleaveRatioBp=Effect.RatioBp*Hit.ADRatioBp/10000;
                Hit.CleaveRadiusCm=LDCA::ToDouble(Effect.RadiusCm);
            }
            else if (Effect.GetOp()==ELDDemoEffectOp::Lifesteal)
            {
                Hit.bLifesteal=true;
                Hit.LifestealRatioBp=Effect.RatioBp;
            }
        }
    }
    PendingHits.Add(Hit);
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
    // Re-check the exact source: a sold, forgotten or replaced source cannot release (and pays nothing).
    FLDCombatSourceRef Source;
    uint8 Rank=0;
    const FLDDemoAbilityDefinition* Ability=nullptr;
    const FLDDemoEquipmentDefinition* Equipment=nullptr;
    if (!ResolveEntry(*Record,Action.Entry,Source,Rank,Ability,Equipment) || Source!=Action.Source || Rank!=Action.Rank)
    { Reject=ELDCombatReject::SourceUnavailable; return true; }
    if (LDControl::IsTaunted(Body.Taunt,Now)) { Reject=ELDCombatReject::ControlBlocked; return true; }
    const FLDDemoRankDefinition* RankDef=Ability ? LDCA::FindRank(*Ability,Rank) : &Equipment->Active.Rank;
    const FLDDemoTargetDefinition& TargetDef=Ability ? Ability->Target : Equipment->Active.Target;
    auto* Track=TrackFor(*Record,Action.Entry);
    if (!RankDef || !Track) { Reject=ELDCombatReject::SourceUnavailable; return true; }
    if (Track->ReadyAtMs>Now) { Reject=ELDCombatReject::Cooldown; return true; }
    if (Body.Resources.MpP2<RankDef->ManaCostP2) { Reject=ELDCombatReject::InsufficientMana; return true; }
    FLDCombatBodyRef UnitTarget;
    if (TargetDef.Mode==ELDDemoTargetMode::Unit)
    {
        FVector TargetFoot;
        if (!ResolveUnitTarget(*Record,Actor,Action.Target,UnitTarget,TargetFoot,Reject) || UnitTarget!=Action.TargetBody)
        { Reject=ELDCombatReject::TargetUnavailable; return true; }
        const FVector Foot=FootOf(Actor);
        if (FVector::Dist2D(Foot,TargetFoot)>LDCA::ToDouble(TargetDef.RangeCm)) { Reject=ELDCombatReject::OutOfRange; return true; }
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
    if (DashEffect && LDTactical::RootBlocks(Body.Roots,Now,Content->Get().Policies,ELDDemoRootBlock::Dash))
    { Reject=ELDCombatReject::ControlBlocked; return true; }
    if (DashEffect && (!Movement || !Movement->CanStartDash(Action.Direction,LDCA::DashProbeCm)))
    { Reject=ELDCombatReject::Blocked; return true; }
    // The duration is fixed at the actual release moment; later build changes never move this endpoint.
    int64 DurationMs=0, ReadyAt=0;
    if (!LDTactical::CooldownDurationMs(RankDef->CooldownMs,Body.AttributeProjection.Final.CooldownHasteP2,
            Content->Get().Formulas.HasteScaleP2,DurationMs,Error)
        || !LDMatchLifecycle::AddDurationMs(Now,DurationMs,ReadyAt,Error)) { return false; }
    // Every fallible step is done: pay once and register the effects.
    Body.Resources.MpP2-=RankDef->ManaCostP2;
    ++Body.Resources.Revision;
    Track->ReadyAtMs=ReadyAt;
    Track->LastDurationMs=DurationMs;
    const auto& Final=Body.AttributeProjection.Final;
    const FGuid EffectId=FGuid::NewGuid();
    const FLDCombatBodyRef SelfRef{Body.Victim,Body.Identity};
    bool bDealsDamage=false;
    const FLDDemoEffectDefinition* RootEffect=LDCA::FindEffect(*RankDef,ELDDemoEffectOp::Root);
    for (const auto& Effect : RankDef->Effects)
    {
        switch (Effect.GetOp())
        {
        case ELDDemoEffectOp::Damage:
            bDealsDamage=true;
            if (TargetDef.Mode==ELDDemoTargetMode::DirectionProjectile)
            {
                FLDCombatProjectile Projectile;
                Projectile.EffectId=EffectId;
                Projectile.Token=Action.Token;
                Projectile.SourceBody=SelfRef;
                Projectile.SourceEntity=Body.Victim.EntityId;
                Projectile.CreditOwner=Body.Victim.EntityId;
                Projectile.Source=Source;
                Projectile.BaseP2=Effect.BaseP2;
                Projectile.ADRatioBp=Effect.ADRatioBp;
                Projectile.APRatioBp=Effect.APRatioBp;
                Projectile.DamageType=Effect.DamageType;
                Projectile.SourceADP2=Final.ADP2;
                Projectile.SourceAPP2=Final.APP2;
                Projectile.OriginCm=FootOf(Actor);
                Projectile.Direction=Action.Direction;
                Projectile.SpeedCmPerSec=LDCA::ToDouble(TargetDef.ProjectileSpeedCmPerSec);
                Projectile.RangeCm=LDCA::ToDouble(TargetDef.RangeCm);
                Projectile.RadiusCm=LDCA::ToDouble(TargetDef.ProjectileRadiusCm);
                Projectile.AdvancedThroughMs=Now;
                Projectiles.Add(Projectile);
            }
            else if (TargetDef.Mode==ELDDemoTargetMode::Unit)
            {
                FLDCombatHit Hit;
                Hit.HitId=FGuid::NewGuid();
                Hit.EffectId=EffectId;
                Hit.Token=Action.Token;
                Hit.Target=UnitTarget;
                Hit.SourceBody=SelfRef;
                Hit.SourceEntity=Body.Victim.EntityId;
                Hit.CreditOwner=Body.Victim.EntityId;
                Hit.Source=Source;
                Hit.HitClass=ELDCombatHitClass::Ability;
                Hit.BaseP2=Effect.BaseP2;
                Hit.ADRatioBp=Effect.ADRatioBp;
                Hit.APRatioBp=Effect.APRatioBp;
                Hit.DamageType=Effect.DamageType;
                Hit.SourceADP2=Final.ADP2;
                Hit.SourceAPP2=Final.APP2;
                Hit.RootDurationMs=RootEffect ? RootEffect->DurationMs : 0;
                PendingHits.Add(Hit);
            }
            break;
        case ELDDemoEffectOp::Shield:
        {
            if (Effect.APRatioBp<0 || Final.APP2<0 || (Effect.APRatioBp>0 && Final.APP2>MAX_int64/Effect.APRatioBp))
            { Error=TEXT("Shield scaling overflowed."); return false; }
            const int64 Amount=Effect.BaseP2+Effect.APRatioBp*Final.APP2/10000;
            int64 Expires=0;
            if (!LDMatchLifecycle::AddDurationMs(Now,Effect.DurationMs,Expires,Error)) { return false; }
            // A weaker barrier is a stated sub-effect outcome, not a failed release.
            if (LDTactical::ApplyShield(Body.Shield,Body.Resources.ShieldP2,
                FLDCombatShield{EffectId,Body.Victim.EntityId,Expires},Amount,Now))
            { ++Body.Resources.Revision; }
            break;
        }
        case ELDDemoEffectOp::DispelRoot:
            if (LDTactical::DispelRoots(Body.Roots)>0) { ++Body.StateRevision; }
            break;
        case ELDDemoEffectOp::RestoreMana:
        {
            const int64 Amount=Body.Resources.MaxMpP2/10000*Effect.RatioBp+Body.Resources.MaxMpP2%10000*Effect.RatioBp/10000;
            Body.Resources.MpP2=FMath::Min(Body.Resources.MaxMpP2,Body.Resources.MpP2+Amount);
            ++Body.Resources.Revision;
            break;
        }
        case ELDDemoEffectOp::Dash:
        {
            const double Seconds=double(Effect.DurationMs)/1000.;
            if (!Movement->StartDemoDash(Action.Direction,LDCA::ToDouble(Effect.DistanceCm),Seconds,Error)) { return false; }
            if (!LDMatchLifecycle::AddDurationMs(Now,Effect.DurationMs,Body.DashUntilMs,Error)) { return false; }
            break;
        }
        case ELDDemoEffectOp::Root:
            break;
        case ELDDemoEffectOp::Taunt:
            if (!ApplyTauntEffect(Actor,Body,Effect,EffectId,Error)) { return false; }
            break;
        case ELDDemoEffectOp::Stealth:
        {
            FLDCombatStealth Stealth;
            Stealth.EffectId=EffectId;
            Stealth.RevealRadiusCm=LDCA::ToDouble(Effect.RadiusCm);
            Stealth.MoveSpeedPenaltyBp=Effect.RatioBp;
            if (!LDMatchLifecycle::AddDurationMs(Now,Effect.DurationMs,Stealth.ExpiresAtMs,Error)) { return false; }
            if (!LDControl::ApplyStealth(Body.Stealth,Stealth,Now))
            { Error=TEXT("A stealth release has no valid duration, reveal radius or speed penalty."); return false; }
            break;
        }
        default:
            Error=FString::Printf(TEXT("Active effect opcode %u has no Combat handler."),Effect.Opcode);
            return false;
        }
    }
    if (bDealsDamage) { Body.LastCombatAtMs=Now; }
    ++Body.StateRevision;
    MarkViewChanged(Body.Victim.EntityId);
    return true;
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
    const auto* Record=FindRecord(Ref.Victim.EntityId);
    Out.NextAttackAtMs=Ref.Victim.Kind==ELDVictimKind::Contender ? (Record ? Record->NextAttackAtMs : 0) : Body->MonsterNextAttackAtMs;
    Out.AttackRangeCm=LDCA::ToDouble(Body->AttributeProjection.Final.AttackRangeCm);
    Out.bAttackAllowed=!Body->bPendingDeath;
    Out.bMovementAllowed=Out.bAttackAllowed && Out.RootedUntilMs==0;
    Out.bBasicAttackReady=Out.bAttackAllowed && !Out.bActionInProgress && Now>=Out.NextAttackAtMs;
    return true;
}

bool ALDCombatDirector::IsRooted(const ALDCharacter& Actor) const
{
    const auto* Mode=Core();
    const auto* Content=Catalog();
    const auto* Body=Bodies.Find(const_cast<ALDCharacter*>(&Actor));
    if (!Mode || !Content || !Body) { return false; }
    return LDTactical::RootBlocks(Body->Roots,Mode->GetAuthorityNowMs(),Content->Get().Policies,ELDDemoRootBlock::Move);
}

bool ALDCombatDirector::ReadMoveSpeed(const ALDCharacter& Actor, float& OutCmPerSec) const
{
    OutCmPerSec=0;
    const auto* Body=Bodies.Find(const_cast<ALDCharacter*>(&Actor));
    if (!Body || Body->bPendingDeath) { return false; }
    const auto* Mode=Core();
    double Speed=0;
    if (!Mode || !EffectiveMoveSpeed(*Body,Mode->GetAuthorityNowMs(),Speed) || !FMath::IsFinite(Speed) || Speed<=0 || Speed>MAX_flt)
    { return false; }
    OutCmPerSec=float(Speed);
    return true;
}
