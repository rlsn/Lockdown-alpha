#include "Combat/LDCombatDirector.h"
#include "Combat/LDCombatSourceRules.h"
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
        return Entry==ELDCombatEntry::Q || Entry==ELDCombatEntry::W || Entry==ELDCombatEntry::Item1 || Entry==ELDCombatEntry::Item2
            || Entry==ELDCombatEntry::Dash;
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

    ELDCombatControlAction EntryControl(ELDCombatEntry Entry)
    { return Entry==ELDCombatEntry::Item1 || Entry==ELDCombatEntry::Item2 ? ELDCombatControlAction::Gear : ELDCombatControlAction::Cast; }
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
    case ELDCombatTargetKind::Point:
        Shape&=!Camp && LDCA::ValidPoint(Request.Target.Point) && !Request.Target.Observed.IsValid()
            && !Request.Target.ObservationEpoch && !Request.Target.ServerBody.IsValid();
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
    // passive_battle_rhythm Lv3: at full stacks the windup is shorter by the passive's ratio.
    int64 WindupMs=Timing.IsSuccess() ? Timing.Value->WindupMs : 0;
    TArray<FLDPassiveEffectRef> Stacks;
    if (!CollectPassiveEffects(Body->AttributeProjection,ELDDemoEffectOp::BasicStacks,Stacks,Error))
    { Mode->FaultAuthority(Error); return Refuse(ELDCombatReject::AuthorityDataUnavailable); }
    for (const FLDPassiveEffectRef& Ref : Stacks)
    {
        const FName StackKey(*Ref.Key);
        const bool bFull=Ref.Effect->RatioBp>0 && Body->BasicStacks>=Ref.Effect->Count
            && Body->TimedBonuses.ContainsByPredicate([StackKey,Now](const FLDCombatTimedBonus& Bonus)
            { return Bonus.SourceDefinition==StackKey && Bonus.ExpiresAtMs>Now; });
        if (bFull) { WindupMs=FMath::Max<int64>(1,WindupMs-WindupMs*Ref.Effect->RatioBp/10000); }
    }
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
    // Starting the swing enters combat, so the ambush window is read before LastCombatAt moves.
    const auto& Ambush=Body->AttributeProjection.Ambush;
    Action.bAmbush=Ambush.IsSet() && AmbushReadyNow(*Body,Now);
    Body->Action=Action;
    Body->BasicTiming=Timing.Value.GetValue();
    Body->LastCombatAtMs=Now;
    ++Body->StateRevision;
    LDCA::FaceToward(*Actor,TargetFoot);
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

void ALDCombatDirector::CancelNavAttack(const FGuid& Entity, const FLDMoveIdentity& Move, const FLDCombatBodyRef& Keep)
{
    auto* Body=FindEntityBody(Entity);
    if (Body && Body->Action.IsSet() && Body->Action->Kind==ELDCombatActionKind::Basic
        && Body->Action->Key.Origin==ELDCombatRequestOrigin::Nav && Body->Action->Key.Move==Move
        && !(Keep.IsValid() && Body->Action->TargetBody==Keep))
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
    // The innate D dash is movement, not an aggressive intent (APP_SPEC 7.1: a protected body may move).
    if (Record.Kind==ELDVictimKind::Contender && Request.Entry!=ELDCombatEntry::Dash)
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
    const FLDDemoEquipmentDefinition* Equipment=nullptr;
    if (!ResolveEntry(Record,Request.Entry,Source,Rank,Ability,Equipment)) { return Refuse(ELDCombatReject::SourceUnavailable); }
    if (Request.ExpectedSourceRevision!=Record.SourceRevision) { return Refuse(ELDCombatReject::StaleSource); }
    if (Equipment && !Equipment->HasActive) { return Refuse(ELDCombatReject::NoActiveEffect); }
    const FLDDemoRankDefinition* RankDef=nullptr;
    const FLDDemoTargetDefinition* TargetDef=nullptr;
    int64 WindupMs=0;
    if (!ActiveOf(Source,Rank,Ability,Equipment,TargetDef,RankDef,WindupMs)) { return Refuse(ELDCombatReject::SourceUnavailable); }
    const auto* Track=TrackFor(Record,Request.Entry);
    if (!Track) { Error=TEXT("A held active source has no persistent cooldown track."); return false; }
    FLDActiveGateInput Gate;
    Gate.NowMs=Now;
    Gate.ReadyAtMs=Track->ReadyAtMs;
    Gate.MpP2=Body->Resources.MpP2;
    Gate.ManaCostP2=RankDef->ManaCostP2;
    Gate.bDash=TargetDef->Mode==ELDDemoTargetMode::DirectionDash || TargetDef->Mode==ELDDemoTargetMode::GroundPoint;
    Gate.Roots=&Body->Roots;
    Gate.bControlBlocked=IsActionBlocked(*Body,LDCA::EntryControl(Request.Entry));
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
    {
        // A jump is a displacement: Root and Stun refuse it before any cost; a point beyond range is clamped.
        if (Request.Target.Kind!=ELDCombatTargetKind::Point || !LDCA::ValidPoint(Request.Target.Point))
        { return Refuse(ELDCombatReject::InvalidDirection); }
        if (IsActionBlocked(*Body,ELDCombatControlAction::Displacement)) { return Refuse(ELDCombatReject::ControlBlocked); }
        double DistanceCm=0, JumpRangeCm=0;
        if (!CastRangeCm(*Body,*TargetDef,JumpRangeCm,Error)) { return false; }
        if (!LDCombatControl::PlanJump(FootOf(*Actor),Request.Target.Point,JumpRangeCm,Action.Direction,DistanceCm))
        { return Refuse(ELDCombatReject::InvalidDirection); }
        auto* Movement=Cast<ULDCharacterMovementComponent>(Actor->GetCharacterMovement());
        if (DistanceCm>0 && (!Movement || !Movement->CanStartDash(Action.Direction,LDCA::DashProbeCm))) { return Refuse(ELDCombatReject::Blocked); }
        Action.TargetPoint=Request.Target.Point;
        break;
    }
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
    // Any accepted ability or item activation reveals a stealthed body (a new stealth is applied at its own release).
    RevealStealth(*Body);
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
    // A disarm or stun that landed during the windup ends it without a swing.
    if (IsActionBlocked(Body,ELDCombatControlAction::Basic))
    { Outcome=ELDCombatReject::ControlBlocked; FinishAction(Body,Outcome,false); return true; }
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
    if (!StampSourceTerms(Body,Hit,Error) || !SnapshotOnHit(Body,10000,Hit,Error)) { return false; }
    const int64 Now=Mode->GetAuthorityNowMs();
    // active_lunge and active_slick_moves Lv3 join the primary hit before its critical roll, so they take crit, vamp, cleave and on-hit.
    const bool bWasEmpowered=Body.Empower.IsSet();
    const TOptional<FLDCombatEmpower> Bonus=LDCombatControl::ConsumeEmpower(Body.Empower,Now);
    if (Bonus.IsSet())
    {
        Hit.BaseP2+=Bonus->BaseP2;
        Hit.ADRatioBp+=Bonus->ADRatioBp;
    }
    if (Body.Momentum.IsSet() && Now>=Body.Momentum->AccumulateUntilMs && Now<Body.Momentum->ConsumeUntilMs)
    {
        const FLDCombatMomentum& Gathered=Body.Momentum.GetValue();
        const double Points=FMath::FloorToDouble(Gathered.DistanceCm*double(Gathered.PerHundredCmP2)/100.);
        Hit.BaseP2+=FMath::Clamp<int64>(int64(Points),0,Gathered.CapP2);
        Body.Momentum.Reset();
        ++Body.StateRevision;
    }
    // One critical sample per AttackId, at the successful release only.
    Hit.bCritical=Final.CritChanceBp>0 && CritRandom.RandRange(0,9999)<Final.CritChanceBp;
    PendingHits.Add(Hit);
    if (Action.bAmbush && Body.AttributeProjection.Ambush.IsSet())
    { PendingHits.Add(MakeAmbushHit(Hit,Body.AttributeProjection.Ambush.GetValue())); }
    // passive_keyboard_warrior: a separate magic hit rides on every swing; it never crits or triggers on-hit effects.
    FLDCombatSourceRef RiderRef;
    if (const FLDDemoEffectDefinition* Rider=FindTrigger(Body.AttributeProjection,ELDDemoEffectOp::BasicRider,&RiderRef))
    {
        FLDCombatHit Extra=MakeAmbushHit(Hit,FLDAmbushDamageTerm());
        Extra.Source=RiderRef;
        Extra.BaseP2=Rider->BaseP2;
        Extra.ADRatioBp=0;
        Extra.APRatioBp=Rider->APRatioBp;
        Extra.DamageType=Rider->DamageType;
        PendingHits.Add(Extra);
    }
    // active_lunge Lv3: the empowered basic that lands cuts the arming source's remaining cooldown.
    if (Bonus.IsSet() && Bonus->RefundMs>0) { ReduceLearnedCooldown(Body.Victim.EntityId,Bonus->SourceDefinitionId,Bonus->RefundMs); }
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
    // passive_battle_rhythm: each landed basic adds a stack of its timed speed bonus (refreshing all of them), up to its cap.
    TArray<FLDPassiveEffectRef> Stacks;
    if (!CollectPassiveEffects(Body.AttributeProjection,ELDDemoEffectOp::BasicStacks,Stacks,Error)) { return false; }
    for (const FLDPassiveEffectRef& Ref : Stacks)
    {
        const FName Key(*Ref.Key);
        const bool bActive=Body.TimedBonuses.ContainsByPredicate([Key,Now](const FLDCombatTimedBonus& Bonus)
        { return Bonus.SourceDefinition==Key && Bonus.ExpiresAtMs>Now; });
        Body.BasicStacks=FMath::Clamp(bActive ? Body.BasicStacks+1 : 1,1,FMath::Max(1,Ref.Effect->Count));
        FLDCombatTimedBonus Bonus;
        Bonus.EffectId=FGuid::NewGuid();
        Bonus.SourceDefinition=Key;
        for (FLDDemoStatBonus Stat : Ref.Effect->Stats)
        {
            Stat.Amount*=Body.BasicStacks;
            Stat.Cm.Numerator*=Body.BasicStacks;
            Bonus.Stats.Add(Stat);
        }
        if (!LDMatchLifecycle::AddDurationMs(Now,Ref.Effect->DurationMs,Bonus.ExpiresAtMs,Error)) { return false; }
        LDTactical::ApplyTimedBonus(Body.TimedBonuses,Bonus);
        ++Body.StateRevision;
    }
    // active_combo_flurry Lv3: while its window lasts each landed basic cuts the other learned actives.
    if (Body.BasicHitRefund.IsSet() && Now<Body.BasicHitRefund->ExpiresAtMs)
    {
        if (const FLDCombatEntityRecord* Record=FindRecord(Entity))
        {
            for (const ELDCombatEntry Slot : {ELDCombatEntry::Q,ELDCombatEntry::W})
            {
                const FLDCombatTrack* Track=TrackFor(*Record,Slot);
                if (Track && Track->DefinitionId!=Body.BasicHitRefund->SourceDefinitionId)
                { ReduceCooldown(Entity,Slot,Body.BasicHitRefund->AmountMs); }
            }
        }
    }
    // passive_precision: a critical basic cuts both learned actives.
    if (Primary.bCritical)
    {
        TArray<FLDPassiveEffectRef> Refunds;
        if (!CollectPassiveEffects(Body.AttributeProjection,ELDDemoEffectOp::CooldownRefund,Refunds,Error)) { return false; }
        for (const FLDPassiveEffectRef& Ref : Refunds)
        {
            if (Ref.Effect->Trigger!=ELDDemoEffectTrigger::BasicCrit) { continue; }
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
        FLDCombatHit Extra=MakeAmbushHit(Primary,FLDAmbushDamageTerm());
        Extra.BaseP2=Ref.Effect->BaseP2;
        Extra.ADRatioBp=Extra.APRatioBp=0;
        Extra.DamageType=Ref.Effect->DamageType;
        Extra.ShredMs=Extra.ShredArmorBp=Extra.ShredMagicResistBp=0;
        Extra.ShredKey=NAME_None;
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
    // Re-check the exact source: a sold, forgotten or replaced source cannot release (and pays nothing).
    FLDCombatSourceRef Source;
    uint8 Rank=0;
    const FLDDemoAbilityDefinition* Ability=nullptr;
    const FLDDemoEquipmentDefinition* Equipment=nullptr;
    if (!ResolveEntry(*Record,Action.Entry,Source,Rank,Ability,Equipment) || Source!=Action.Source || Rank!=Action.Rank)
    { Reject=ELDCombatReject::SourceUnavailable; return true; }
    const FLDDemoRankDefinition* RankDef=nullptr;
    const FLDDemoTargetDefinition* TargetPtr=nullptr;
    int64 WindupMs=0;
    auto* Track=TrackFor(*Record,Action.Entry);
    if (!ActiveOf(Source,Rank,Ability,Equipment,TargetPtr,RankDef,WindupMs) || !Track)
    { Reject=ELDCombatReject::SourceUnavailable; return true; }
    const FLDDemoTargetDefinition& TargetDef=*TargetPtr;
    // The caster's foot before any displacement of this release (areas, zones, decoys and pushes centre here).
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
    Gate.bDash=LDCA::FindEffect(*RankDef,ELDDemoEffectOp::Dash) || LDCA::FindEffect(*RankDef,ELDDemoEffectOp::Jump)
        || LDCA::FindEffect(*RankDef,ELDDemoEffectOp::LeapBack);
    Gate.Roots=&Body.Roots;
    Gate.bControlBlocked=IsActionBlocked(Body,LDCA::EntryControl(Action.Entry));
    Gate.bDisplacementBlocked=IsActionBlocked(Body,ELDCombatControlAction::Displacement);
    Reject=LDCombatSourceRules::ActiveGate(Gate,Content->Get().Policies);
    if (Reject!=ELDCombatReject::None) { return true; }
    // item_energy_drink needs a Q or W cooling down; otherwise it is refused before paying.
    if (LDCA::FindEffect(*RankDef,ELDDemoEffectOp::CooldownRefresh))
    {
        const FLDCombatTrack* Q=TrackFor(*Record,ELDCombatEntry::Q);
        const FLDCombatTrack* W=TrackFor(*Record,ELDCombatEntry::W);
        if ((!Q || Q->ReadyAtMs<=Now) && (!W || W->ReadyAtMs<=Now)) { Reject=ELDCombatReject::NoActiveEffect; return true; }
    }
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
    const auto* JumpEffect=LDCA::FindEffect(*RankDef,ELDDemoEffectOp::Jump);
    if (DashEffect && (!Movement || !Movement->CanStartDash(Action.Direction,LDCA::DashProbeCm)))
    { Reject=ELDCombatReject::Blocked; return true; }
    // The jump is planned again from the foot at release; the requested point is clamped to the authored reach.
    FVector JumpDirection=FVector::ZeroVector;
    double JumpCm=0;
    if (JumpEffect)
    {
        double JumpRangeCm=0;
        if (!CastRangeCm(Body,TargetDef,JumpRangeCm,Error)) { return false; }
        if (!LDCombatControl::PlanJump(FootOf(Actor),Action.TargetPoint,JumpRangeCm,JumpDirection,JumpCm))
        { Reject=ELDCombatReject::InvalidDirection; return true; }
        if (JumpCm>0 && (!Movement || !Movement->CanStartDash(JumpDirection,LDCA::DashProbeCm))) { Reject=ELDCombatReject::Blocked; return true; }
    }
    // Hit effects name their targets before anything is paid: a unit, a projectile, or every legal body in a self area.
    // A jump's area is named at its landing, around the landing foot.
    const FLDDemoEffectDefinition* DamageEffect=LDCA::FindEffect(*RankDef,ELDDemoEffectOp::Damage);
    const FLDDemoEffectDefinition* RootEffect=LDCA::FindEffect(*RankDef,ELDDemoEffectOp::Root);
    const FLDDemoEffectDefinition* SlowEffect=LDCA::FindEffect(*RankDef,ELDDemoEffectOp::Slow);
    const FLDDemoEffectDefinition* DisarmEffect=LDCA::FindEffect(*RankDef,ELDDemoEffectOp::Disarm);
    const FLDDemoEffectDefinition* StunEffect=LDCA::FindEffect(*RankDef,ELDDemoEffectOp::Stun);
    const FLDDemoEffectDefinition* BackstabEffect=LDCA::FindEffect(*RankDef,ELDDemoEffectOp::Backstab);
    const FLDDemoEffectDefinition* AreaEffect=LDCA::FindEffect(*RankDef,ELDDemoEffectOp::SelfArea);
    const FLDDemoEffectDefinition* BurstEffect=LDCA::FindEffect(*RankDef,ELDDemoEffectOp::ShieldBurst);
    // A damage guard holds its slow/stun and shield for its recoil (active_porcelain_flop); nothing hits at release.
    const bool bGuard=LDCA::FindEffect(*RankDef,ELDDemoEffectOp::DamageGuard)!=nullptr;
    const bool bHostile=!bGuard && (DamageEffect || RootEffect || SlowEffect || DisarmEffect || StunEffect || BurstEffect
        || LDCA::FindEffect(*RankDef,ELDDemoEffectOp::Dot) || LDCA::FindEffect(*RankDef,ELDDemoEffectOp::HealCut)
        || LDCA::FindEffect(*RankDef,ELDDemoEffectOp::Knockback));
    const bool bAreaMode=TargetDef.Mode==ELDDemoTargetMode::Self || TargetDef.Mode==ELDDemoTargetMode::DirectionDash
        || TargetDef.Mode==ELDDemoTargetMode::GroundPoint;
    if (bHostile && bAreaMode && !AreaEffect)
    {
        Error=TEXT("A self, dash or jump action with hit effects has no self_area to name its targets.");
        return false;
    }
    TArray<FLDCombatBodyRef> AreaTargets;
    // The origin is the caster's foot at release, before any dash of the same action moves it.
    if (bHostile && bAreaMode && !JumpEffect
        && !CollectAreaTargets(FootOf(Actor),LDCA::ToDouble(AreaEffect->RadiusCm),&Body,Body.Victim.EntityId,AreaTargets,Error))
    { return false; }
    const auto& AmbushTerm=Body.AttributeProjection.Ambush;
    const bool bAmbush=DamageEffect && AmbushReadyNow(Body,Now);
    // item_phase_anchor: a character or gear active with a dash or jump takes the skill boost held at this release (base
    // cooldown x ratio before haste, distance x (1 + bonus)); the innate dash has its own terms.
    int64 SkillCooldownRatioBp=10000, SkillDistanceBonusBp=0;
    if (!bInnate && (DashEffect || JumpEffect) && !LDCombatSourceRules::SkillDisplacementBoost(*Content,
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
    bool bTimedBonus=false;
    FLDCombatLanding Landing;
    // Cleanses resolve before the release's other self effects so a shield can see them (active_guard_pulse Lv3).
    bool bCleansed=false;
    if (!JumpEffect)
    {
        for (const auto& Effect : RankDef->Effects)
        {
            if (Effect.GetOp()==ELDDemoEffectOp::DispelRoot)
            {
                const bool bActive=LDTactical::HasActiveRoot(Body.Roots,Now);
                if (LDTactical::DispelRoots(Body.Roots)>0) { ++Body.StateRevision; }
                bCleansed|=bActive;
            }
            else if (Effect.GetOp()==ELDDemoEffectOp::DispelSlow && LDTactical::DispelSlows(Body.Slows,Now)>0)
            {
                bCleansed=true;
                ++Body.StateRevision;
                bTimedBonus=true;
            }
        }
    }
    // Moving actives credit the ambush passive (passive_lurking), the innate dash included.
    if ((DashEffect || JumpEffect) && Body.AttributeProjection.Ambush.IsSet())
    { CreditAmbush(Body,Body.AttributeProjection.Ambush->DashCreditMs); }
    for (const auto& Effect : RankDef->Effects)
    {
        // Every self effect released with a jump resolves at its landing; hit effects travel in the landing's template.
        if (JumpEffect && Effect.GetOp()!=ELDDemoEffectOp::Jump)
        {
            if (Effect.GetOp()==ELDDemoEffectOp::Damage) { bDealsDamage=true; }
            if (Effect.GetOp()==ELDDemoEffectOp::Shield || Effect.GetOp()==ELDDemoEffectOp::DispelRoot
                || Effect.GetOp()==ELDDemoEffectOp::DispelSlow || Effect.GetOp()==ELDDemoEffectOp::RestoreMana)
            { Landing.Effects.Add(Effect); }
            if (Effect.GetOp()==ELDDemoEffectOp::TriggeredShield) { Landing.LandingShield=Effect; }
            continue;
        }
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
            // A guard's shield is its recoil's payload.
            if (bGuard) { break; }
            if (!ApplySelfEffect(Body,Effect,EffectId,Now,Error,bCleansed)) { return false; }
            break;
        case ELDDemoEffectOp::RestoreMana:
            if (!ApplySelfEffect(Body,Effect,EffectId,Now,Error,bCleansed)) { return false; }
            break;
        case ELDDemoEffectOp::DispelRoot:
        case ELDDemoEffectOp::DispelSlow:
            // Resolved before this loop.
            break;
        case ELDDemoEffectOp::Dash:
        {
            const double Seconds=double(Effect.DurationMs)/1000.;
            const double Distance=LDCA::ToDouble(bInnate ? Innate.DistanceCm : Effect.DistanceCm)*double(10000+SkillDistanceBonusBp)/10000.;
            if (!Movement->StartDemoDash(Action.Direction,Distance,Seconds,Error)) { return false; }
            if (!LDMatchLifecycle::AddDurationMs(Now,Effect.DurationMs,Body.DashUntilMs,Error)) { return false; }
            if (!OnMobilityUsed(Body,Now,Error)) { return false; }
            break;
        }
        case ELDDemoEffectOp::Jump:
        {
            // The same native displacement as a dash, toward the planned point and lasting the airtime; terrain stops
            // it at the legal front segment, the ground keeps it (no separate layer) and capture reads its positions.
            if (JumpCm>0 && !Movement->StartDemoDash(JumpDirection,JumpCm,double(Effect.DurationMs)/1000.,Error)) { return false; }
            if (!LDMatchLifecycle::AddDurationMs(Now,Effect.DurationMs,Body.DashUntilMs,Error)) { return false; }
            Landing.Body=SelfRef;
            Landing.EffectId=EffectId;
            Landing.Token=Action.Token;
            Landing.Source=Source;
            Landing.LandAtMs=Body.DashUntilMs;
            if (!OnMobilityUsed(Body,Now,Error)) { return false; }
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
            // active_lunge Lv3: the landed empowered basic refunds part of this source's cooldown.
            if (const auto* Refund=LDCA::FindEffect(*RankDef,ELDDemoEffectOp::CooldownRefund))
            { if (Refund->Trigger==ELDDemoEffectTrigger::EmpowerHit) { Armed.RefundMs=Refund->AmountMs; } }
            // A newer release replaces an unspent one; it never stacks.
            Body.Empower=Armed;
            break;
        }
        case ELDDemoEffectOp::CooldownRefund:
            switch (Effect.Trigger)
            {
            case ELDDemoEffectTrigger::Release:
                // active_monkey_peach: the basic cadence restarts at this release.
                if (Effect.Entries==ELDDemoCooldownEntries::Basic && Record->NextAttackAtMs>Now)
                {
                    Record->NextAttackAtMs=Now;
                    MarkViewChanged(Record->Entity);
                }
                break;
            case ELDDemoEffectTrigger::Cleanse:
                // active_guard_pulse Lv3: a release that cleansed refunds a share of the cooldown it just issued.
                if (bCleansed && Effect.RatioBp>0)
                { Track->ReadyAtMs=LDCombatSourceRules::ReduceReadyAt(Track->ReadyAtMs,Now,DurationMs/10000*Effect.RatioBp+DurationMs%10000*Effect.RatioBp/10000); }
                break;
            case ELDDemoEffectTrigger::BasicHit:
            {
                // active_combo_flurry Lv3: the window in which landed basics cut the other learned actives.
                FLDCombatHitRefundWindow Window;
                Window.SourceDefinitionId=Source.DefinitionId;
                Window.AmountMs=Effect.AmountMs;
                if (!LDMatchLifecycle::AddDurationMs(Now,Effect.DurationMs,Window.ExpiresAtMs,Error)) { return false; }
                Body.BasicHitRefund=Window;
                break;
            }
            case ELDDemoEffectTrigger::EmpowerHit:
                // Carried by the armed empowerment.
                break;
            default:
                Error=FString::Printf(TEXT("Cooldown refund trigger %u has no release handler."),uint32(Effect.Trigger));
                return false;
            }
            break;
        case ELDDemoEffectOp::Momentum:
        {
            // active_slick_moves Lv3: gather movement from this release, consumed by the next landed basic after the gathering.
            FLDCombatMomentum Gathered;
            Gathered.EffectId=EffectId;
            Gathered.SourceDefinitionId=Source.DefinitionId;
            Gathered.PerHundredCmP2=Effect.BaseP2;
            Gathered.CapP2=Effect.CapP2;
            Gathered.LastFootCm=ReleaseFoot;
            if (!LDMatchLifecycle::AddDurationMs(Now,Effect.DurationMs,Gathered.AccumulateUntilMs,Error)
                || !LDMatchLifecycle::AddDurationMs(Gathered.AccumulateUntilMs,Effect.DelayMs,Gathered.ConsumeUntilMs,Error))
            { return false; }
            Body.Momentum=Gathered;
            break;
        }
        case ELDDemoEffectOp::TauntRetaliate:
        {
            // active_come_here Lv3: answered when this release's taunt duration ends.
            const auto* TauntEffect=LDCA::FindEffect(*RankDef,ELDDemoEffectOp::Taunt);
            if (!TauntEffect) { Error=TEXT("A taunt retaliation needs its taunt."); return false; }
            FLDCombatRetaliation Answer;
            Answer.EffectId=EffectId;
            Answer.Source=Source;
            Answer.MaxHits=Effect.Count;
            Answer.BaseP2=Effect.BaseP2;
            Answer.PerHitP2=Effect.PerCountP2;
            Answer.ArmorRatioBp=Effect.ArmorRatioBp;
            Answer.DamageType=Effect.DamageType;
            Answer.RadiusCm=LDCA::ToDouble(Effect.RadiusCm);
            if (!LDMatchLifecycle::AddDurationMs(Now,TauntEffect->DurationMs,Answer.EndAtMs,Error)) { return false; }
            Body.Retaliation=Answer;
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
        case ELDDemoEffectOp::AbilityCrit:
        case ELDDemoEffectOp::ResistShred:
            // Carried by the hit payload below.
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
            // item_paper_box: entering stealth readies the ambush passive at once.
            if (Effect.bAmbushReady && Body.AttributeProjection.Ambush.IsSet()) { CreditAmbush(Body,Body.AttributeProjection.Ambush->DelayMs); }
            break;
        }
        case ELDDemoEffectOp::Root:
        case ELDDemoEffectOp::Slow:
        case ELDDemoEffectOp::Disarm:
        case ELDDemoEffectOp::Stun:
        case ELDDemoEffectOp::Backstab:
        case ELDDemoEffectOp::SelfArea:
        // #112: hit extras ride on the hits below (FillMechanicsTemplate); release content follows them (ReleaseMechanics).
        case ELDDemoEffectOp::Knockback:
        case ELDDemoEffectOp::HealCut:
        case ELDDemoEffectOp::Dot:
        case ELDDemoEffectOp::MobilityPunish:
        case ELDDemoEffectOp::Volley:
        case ELDDemoEffectOp::RefreshDebuffs:
        case ELDDemoEffectOp::OnHit:
        case ELDDemoEffectOp::DebuffAmp:
        case ELDDemoEffectOp::ShieldShatter:
        case ELDDemoEffectOp::DebuffHeal:
        case ELDDemoEffectOp::Vulnerable:
        case ELDDemoEffectOp::ShieldBurst:
        case ELDDemoEffectOp::Zone:
        case ELDDemoEffectOp::ZoneShield:
        case ELDDemoEffectOp::ZoneSlow:
        case ELDDemoEffectOp::ZoneRoot:
        case ELDDemoEffectOp::ZoneDamage:
        case ELDDemoEffectOp::DamageGuard:
        case ELDDemoEffectOp::Decoy:
        case ELDDemoEffectOp::LeapBack:
        case ELDDemoEffectOp::CooldownRefresh:
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
        Template.BackstabRatioBp=BackstabEffect ? BackstabEffect->RatioBp : 0;
        Template.BackstabStunMs=BackstabEffect ? BackstabEffect->DurationMs : 0;
        Template.bBackstabImpaired=BackstabEffect && BackstabEffect->bImpaired;
        Template.SlowVampBp=SlowEffect ? SlowEffect->VampRatioBp : 0;
        if (const auto* Shred=LDCA::FindEffect(*RankDef,ELDDemoEffectOp::ResistShred))
        {
            Template.ShredKey=Source.DefinitionId;
            Template.ShredArmorBp=Shred->ArmorRatioBp;
            Template.ShredMagicResistBp=Shred->MagicResistRatioBp;
            Template.ShredMs=Shred->DurationMs;
        }
        // Ability criticals: active_monkey_peach Lv3 against an impaired or breached target; passive_precision Lv3 at a share of the crit chance.
        if (const auto* Crit=LDCA::FindEffect(*RankDef,ELDDemoEffectOp::AbilityCrit))
        { Template.bCritIfImpaired=Crit->CritMode==ELDDemoCritMode::Impaired; }
        TArray<FLDPassiveEffectRef> Crits;
        if (!CollectPassiveEffects(Body.AttributeProjection,ELDDemoEffectOp::AbilityCrit,Crits,Error)) { return false; }
        for (const FLDPassiveEffectRef& Ref : Crits)
        {
            if (Ref.Effect->CritMode==ELDDemoCritMode::Chance && Ref.Effect->AbilityIds.Contains(Source.DefinitionId.ToString()))
            { Template.AbilityCritChanceBp=FMath::Max(Template.AbilityCritChanceBp,Final.CritChanceBp*Ref.Effect->RatioBp/10000); }
        }
        // Abilities trigger no on-hit effects unless their own definition grants a coefficient (C2 active_barrage_rant, active_fart_dash).
        if (!StampSourceTerms(Body,Template,Error) || !SnapshotOnHit(Body,0,Template,Error)) { return false; }
        FillMechanicsTemplate(Body,*RankDef,ReleaseFoot,Now,Template);
        // A #112 rank with an on-hit coefficient (active_barrage_rant Lv3) snapshots its on-hit terms at that coefficient.
        if (Template.OnHitScaleBp>0 && !SnapshotOnHit(Body,Template.OnHitScaleBp,Template,Error)) { return false; }
        if (BurstEffect)
        {
            // active_table_flip detonates the shield it holds now (the one just granted when it was not weaker):
            // base + ratio x min(shield, cap). The shield is consumed, which is not a break.
            Template.BaseP2=LDCombatMechanics::ShieldBurstBaseP2(BurstEffect->BaseP2,Body.Resources.ShieldP2,BurstEffect->RatioBp,BurstEffect->CapP2);
            Template.ADRatioBp=Template.APRatioBp=Template.MaxHPRatioBp=0;
            Template.DamageType=BurstEffect->DamageType;
            if (Body.Resources.ShieldP2!=0 || Body.Shield.IsSet())
            {
                Body.Shield.Reset();
                Body.Resources.ShieldP2=0;
                Body.ShieldGrantedP2=0;
                ++Body.Resources.Revision;
            }
        }
        const auto Deliver=[this,&Template,bAmbush,&AmbushTerm](const FLDCombatBodyRef& Target)
        {
            FLDCombatHit Hit=Template;
            Hit.HitId=FGuid::NewGuid();
            Hit.Target=Target;
            PendingHits.Add(Hit);
            if (bAmbush) { PendingHits.Add(MakeAmbushHit(Hit,AmbushTerm.GetValue())); }
        };
        if (JumpEffect)
        {
            // The landing names its area around the landing foot and delivers this release-time payload.
            Landing.HitTemplate=Template;
            Landing.AreaRadiusCm=LDCA::ToDouble(AreaEffect->RadiusCm);
            if (bAmbush) { Landing.Ambush=AmbushTerm; }
        }
        else if (TargetDef.Mode==ELDDemoTargetMode::DirectionProjectile)
        {
            FLDCombatProjectile Projectile;
            Projectile.EffectId=EffectId;
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
            if (bAmbush) { Projectile.Ambush=AmbushTerm; }
            Projectiles.Add(Projectile);
        }
        else if (TargetDef.Mode==ELDDemoTargetMode::Unit) { Deliver(UnitTarget); }
        else { for (const FLDCombatBodyRef& Target : AreaTargets) { Deliver(Target); } }
    }
    if (JumpEffect) { Landings.Add(MoveTemp(Landing)); }
    // A ground zone sits at the requested point, clamped to the authored reach.
    FVector ZonePoint=Action.TargetPoint;
    if (TargetDef.Mode==ELDDemoTargetMode::GroundPoint)
    {
        const FVector Offset(ZonePoint.X-ReleaseFoot.X,ZonePoint.Y-ReleaseFoot.Y,0.);
        const double Reach=LDCA::ToDouble(TargetDef.RangeCm);
        ZonePoint=Offset.Size2D()>Reach ? ReleaseFoot+Offset.GetSafeNormal2D()*Reach : FVector(ZonePoint.X,ZonePoint.Y,ReleaseFoot.Z);
    }
    if (!ReleaseMechanics(Actor,Body,Source,*RankDef,nullptr,ReleaseFoot,ZonePoint,EffectId,Action.Token,Error)) { return false; }
    // passive_keyboard_warrior Lv3: a released skill quickens the caster for a while.
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
    if (bDealsDamage || BurstEffect) { Body.LastCombatAtMs=Now; }
    if (bTimedBonus && !RefreshTemporary(Body,&Actor,Now,Error)) { return false; }
    ++Body.StateRevision;
    MarkViewChanged(Body.Victim.EntityId);
    return true;
}

bool ALDCombatDirector::ApplySelfEffect(FLDBodyCombatState& Body, const FLDDemoEffectDefinition& Effect, const FGuid& EffectId,
    int64 NowMs, FString& Error, bool bCleansed)
{
    const auto& Final=Body.AttributeProjection.Final;
    switch (Effect.GetOp())
    {
    case ELDDemoEffectOp::Shield:
    {
        const int64 MaxHP=Body.Resources.MaxHpP2;
        if (Effect.APRatioBp<0 || Final.APP2<0 || (Effect.APRatioBp>0 && Final.APP2>MAX_int64/Effect.APRatioBp)
            || Effect.MaxHPRatioBp<0 || MaxHP<0 || Effect.MultiplierBp<0)
        { Error=TEXT("Shield scaling overflowed."); return false; }
        // Base + AP share + own max-HP share (active_guard_pulse), doubled when the same release cleansed (Lv3).
        int64 Amount=Effect.BaseP2+Effect.APRatioBp*Final.APP2/10000+MaxHP/10000*Effect.MaxHPRatioBp+MaxHP%10000*Effect.MaxHPRatioBp/10000;
        if (bCleansed && Effect.MultiplierBp>0)
        {
            if (Amount>MAX_int64/Effect.MultiplierBp) { Error=TEXT("Shield scaling overflowed."); return false; }
            Amount=Amount*Effect.MultiplierBp/10000;
        }
        // The single shield entry: heal cut, ReplaceIfNotWeaker and the refresh event (#112 §5).
        return GrantShieldTo(Body,Amount,Effect.DurationMs,EffectId,Body.Victim.EntityId,false,NowMs,Error);
    }
    case ELDDemoEffectOp::DispelRoot:
        if (LDTactical::DispelRoots(Body.Roots)>0) { ++Body.StateRevision; }
        return true;
    case ELDDemoEffectOp::DispelSlow:
        if (LDTactical::DispelSlows(Body.Slows,NowMs)>0) { ++Body.StateRevision; }
        return true;
    case ELDDemoEffectOp::RestoreMana:
    {
        const int64 Amount=Body.Resources.MaxMpP2/10000*Effect.RatioBp+Body.Resources.MaxMpP2%10000*Effect.RatioBp/10000;
        Body.Resources.MpP2=FMath::Min(Body.Resources.MaxMpP2,Body.Resources.MpP2+Amount);
        ++Body.Resources.Revision;
        return true;
    }
    default:
        Error=FString::Printf(TEXT("Self effect opcode %u has no Combat handler."),Effect.Opcode);
        return false;
    }
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
    return LDCombatControl::Blocks(Body.Roots,Body.Disarms,Body.Stuns,Now,Content->Get().Policies,Action)
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
    if (!Mode || !EffectiveMoveSpeed(*Body,Mode->GetAuthorityNowMs(),Speed) || !FMath::IsFinite(Speed) || Speed<=0 || Speed>MAX_flt)
    { return false; }
    OutCmPerSec=float(Speed);
    return true;
}
