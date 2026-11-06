#include "Framework/LDGameMode.h"
#include "Core/LDLog.h"
#include "Framework/LDPlayerController.h"
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Combat/LDCombatDirector.h"
#include "Combat/LDCombatTypes.h"
#include "Development/LDDemoContentCatalog.h"
#include "Navigation/LDNavigationComponent.h"
#include "Region/LDMapRegistry.h"
#include "World/LDCampDirector.h"
#include "World/LDSpawnPlacement.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

// Life ending, return and protection semantics extracted from the d70211a ALDGameMode::EndContenderLife /
// ReturnDueContenders, retargeted to the Demo life ledger, the ten-anchor placement and authority milliseconds.

namespace LDGameModeLifeDetail
{
    bool IsSpawnBlocked(const FString& Error) { return Error.StartsWith(TEXT("SpawnBlocked")); }
}

int32 ALDGameMode::PhaseOrder(ELDCommitPhase Phase)
{
    switch (Phase)
    {
    case ELDCommitPhase::Motion: return 0;
    case ELDCommitPhase::Session: return 1;
    case ELDCommitPhase::Actions: return 2;
    case ELDCommitPhase::Effects: return 3;
    case ELDCommitPhase::DeathsAndRewards: return 4;
    case ELDCommitPhase::RespawnsAndWorld: return 5;
    case ELDCommitPhase::Transactions: return 6;
    case ELDCommitPhase::Capture: return 7;
    case ELDCommitPhase::Close: return 8;
    default: return INDEX_NONE;
    }
}

bool ALDGameMode::ExecuteAuthorityPhase(ALDCombatDirector& Driver, ELDCommitPhase Phase,
    TFunctionRef<bool(const FLDGameplayWritePermit&)> Execute, FString& Error)
{
    const bool Initializing = MatchPhase == ELDMatchPhase::Starting && Phase == ELDCommitPhase::RespawnsAndWorld;
    if (Combat != &Driver || !HasAuthority() || ActiveWritePermit || (!bAuthorityStepOpen && !Initializing))
    {
        Error = TEXT("Authority phase has no driver-owned step or attempted a reentrant write.");
        return false;
    }
    if (!Initializing)
    {
        const int32 Order = PhaseOrder(Phase);
        if (Order == INDEX_NONE)
        {
            Error = FString::Printf(TEXT("Authority phase %u has no wired provider."), uint32(Phase));
            return false;
        }
        if (bSettlementStep && Phase != ELDCommitPhase::DeathsAndRewards && Phase != ELDCommitPhase::Close)
        {
            Error = TEXT("A settlement boundary only retries owed rewards and then closes.");
            return false;
        }
        const ELDCommitPhase First = bSettlementStep ? ELDCommitPhase::DeathsAndRewards : ELDCommitPhase::Motion;
        if (Phase == First ? bAuthorityPhaseExecuted
            : (!bAuthorityPhaseExecuted || Order <= PhaseOrder(LastAuthorityPhase)))
        {
            Error = TEXT("Authority phases run once each, Motion (or a settlement's DeathsAndRewards) first, then in contract order.");
            return false;
        }
    }
    const FLDGameplayWritePermit Permit(*this, AuthorityBoundary, Phase, GateRevision);
    TGuardValue<const FLDGameplayWritePermit*> Scope(ActiveWritePermit, &Permit);
    if (ValidateWritePermit(Permit, Phase) != ELDCommitReject::None)
    {
        Error = TEXT("Authority phase has a closed or stale write gate.");
        return false;
    }
    if (!Execute(Permit)) { return false; }
    if (!Initializing)
    {
        bAuthorityPhaseExecuted = true;
        LastAuthorityPhase = Phase;
        if (Phase == ELDCommitPhase::Motion)
        {
            for (const auto& Pair : Lives)
            {
                if (Pair.Value.Phase == ELDLifePhase::Alive && Pair.Value.CompletedMotionStep != AuthorityStep.StepSerial)
                {
                    Error = TEXT("Native motion did not cover every registered live body.");
                    return false;
                }
            }
            AuthorityNowSeconds = AuthorityStep.EndServerSeconds;
            AuthorityNowMs = AuthorityBoundary.AtMs;
        }
    }
    return true;
}

bool ALDGameMode::FinishAuthorityStep(ALDCombatDirector& Driver, FString& Error)
{
    // Close may lock the unique result inside this step; the step still completes its boundary.
    const bool bClosedHere = bResultLocked && LastAuthorityPhase == ELDCommitPhase::Close;
    if (Combat != &Driver || !bAuthorityStepOpen || ActiveWritePermit
        || (!IsGameplayWriteOpen(MatchContext) && !bClosedHere)
        || !bAuthorityPhaseExecuted || PhaseOrder(LastAuthorityPhase) < PhaseOrder(ELDCommitPhase::Actions)
        || (bSettlementStep && LastAuthorityPhase != ELDCommitPhase::Close))
    {
        Error = TEXT("Cannot finish a missing, active or closed authority step.");
        return false;
    }
    // A settlement boundary integrates no time, so no body has native motion to complete.
    for (const auto& Pair : Lives)
    {
        if (!bSettlementStep && Pair.Value.Phase == ELDLifePhase::Alive && Pair.Value.CompletedMotionStep != AuthorityStep.StepSerial)
        {
            Error = TEXT("A registered live body has not completed native motion for this step.");
            return false;
        }
    }
    AuthorityNowSeconds = AuthorityStep.EndServerSeconds;
    AuthorityNowMs = AuthorityBoundary.AtMs;
    bAuthorityStepOpen = false;
    bSettlementStep = false;
    return PublishAuthorityClock(Error);
}

ALDPlayerController* ALDGameMode::FindContenderController(const FGuid& ContenderId) const
{
    if (!ContenderId.IsValid()) { return nullptr; }
    for (const auto& Pair : Contenders)
    {
        if (Pair.Value.Session == ContenderId) { return Pair.Key.Get(); }
    }
    return nullptr;
}

bool ALDGameMode::IsContenderProtected(const FGuid& ContenderId) const
{
    const auto* Life = Lives.Find(ContenderId);
    return Life && Life->Kind == ELDVictimKind::Contender && Life->Phase == ELDLifePhase::Alive
        && AuthorityNowMs < Life->ProtectedUntilMs;
}

int64 ALDGameMode::NextLifeDueMs() const
{
    int64 Due = MAX_int64;
    for (const auto& Pair : Lives)
    {
        const auto& Life = Pair.Value;
        if (Life.Kind != ELDVictimKind::Contender) { continue; }
        const int64 At = Life.Phase == ELDLifePhase::Alive ? Life.ProtectedUntilMs
            : Life.Phase == ELDLifePhase::Dead ? Life.RespawnAtMs : 0;
        if (At > AuthorityNowMs) { Due = FMath::Min(Due, At); }
    }
    return Due;
}

bool ALDGameMode::PublishEligibility(const FLDGameplayWritePermit& Permit, const FLDCaptureEligibilityEvent& Event, FString& Error)
{
    const ELDCommitReject Reject = PublishCaptureEligibility(Permit, Event);
    if (Reject != ELDCommitReject::None)
    {
        Error = FString::Printf(TEXT("Capture refused life fact %u of %s (reject %u)."), uint32(Event.Change),
            *Event.Body.Victim.EntityId.ToString(), uint32(Reject));
        return false;
    }
    Error.Reset();
    return true;
}

void ALDGameMode::NotifyLife(const FLDCombatBodyRef& Body, ELDLifeChange Change, uint64 LifeRevision, int64 AtMs)
{
    FLDLifeNotice Notice;
    Notice.Match = MatchContext;
    Notice.Body = Body;
    Notice.Change = Change;
    Notice.LifeRevision = LifeRevision;
    Notice.OccurredAtMs = AtMs;
    NotifyLifeCommitted(Notice);
}

bool ALDGameMode::KillCauseOf(const FLDCommittedDeath& Fact, ELDKillCause& OutCause, FGuid& OutCredit, FString& Error) const
{
    OutCause = ELDKillCause::Unowned;
    OutCredit.Invalidate();
    const FGuid& Victim = Fact.Key.Victim.EntityId;
    if (Fact.CreditOwner.IsValid() && Fact.CreditOwner != Victim)
    {
        for (const auto& Pair : Contenders)
        {
            if (Pair.Value.Session == Fact.CreditOwner)
            {
                OutCredit = Fact.CreditOwner;
                OutCause = ELDKillCause::Contender;
                Error.Reset();
                return true;
            }
        }
    }
    if (Fact.SourceBodyAtRelease.Victim.Kind == ELDVictimKind::CampMember)
    {
        // The killer's tier outlives its body through the persistent Combat record.
        const auto* Record = Combat ? Combat->FindRecord(Fact.SourceBodyAtRelease.Victim.EntityId) : nullptr;
        const FLDDemoMonsterDefinition* Monster = nullptr;
        if (!Record || !DemoContent || !DemoContent->FindMonster(Record->MonsterId.ToString(), Monster, Error))
        {
            if (Error.IsEmpty()) { Error = TEXT("A camp kill has no recorded monster definition."); }
            return false;
        }
        OutCause = Monster->Tier == ELDDemoCampTier::Boss ? ELDKillCause::Boss
            : Monster->Tier == ELDDemoCampTier::High ? ELDKillCause::CampHigh : ELDKillCause::CampLow;
    }
    Error.Reset();
    return true;
}

void ALDGameMode::ReleaseConsumedMotion()
{
    for (auto& Pair : Lives)
    {
        auto& Life = Pair.Value;
        if (Life.NativeMotion.IsEmpty()) { continue; }
        const auto& Last = Life.NativeMotion.Last();
        Life.MotionAckStep = Last->Step.StepSerial;
        Life.MotionAckSequence = Last->ThroughSequence;
        Life.NativeMotion.Reset();
    }
}

void ALDGameMode::CloseCombatMatch(const FLDMatchClosed& Closed)
{
    if (Closed.Match != MatchContext) { return; }
    // The gate is already closed: cancel only, no movement, cost or effect is committed here.
    for (const auto& Pair : Contenders)
    {
        if (auto* Controller = Pair.Key.Get(); Controller && Controller->Navigation)
        { Controller->Navigation->EndLifeOrders(ELDOrderCancelReason::MatchClosed); }
    }
    if (Combat) { Combat->EndMatchActions(); }
}

bool ALDGameMode::ResetCombatMatch(const FLDMatchReset& Reset, FString& Error)
{
    if (Reset.Old != MatchContext || !Reset.New.IsValid())
    {
        Error = TEXT("Combat reset names a match other than the one being retired.");
        return false;
    }
    for (const auto& Pair : Contenders)
    {
        auto* Controller = Pair.Key.Get();
        if (!Controller) { continue; }
        // The controller outlives the match while the authority clock restarts at 0: its orders and order book
        // (watermark, idempotency window, activation throttle) end with the old match.
        if (Controller->Navigation) { Controller->Navigation->ResetMatch(); }
        // A new-generation empty view replaces the old match's projection on the owner.
        FLDCombatOwnerView Cleared;
        Cleared.Generation = Reset.New.Generation;
        Controller->CombatView = Cleared;
        Controller->CombatReceipts.Clear();
        if (Controller->IsLocalController()) { Controller->OnRep_CombatView(); }
        Controller->ForceNetUpdate();
    }
    // The core keeps only contender lives across a reset; camp lives belong to the old match.
    for (auto It = Lives.CreateIterator(); It; ++It)
    {
        if (It.Value().Kind == ELDVictimKind::CampMember) { It.RemoveCurrent(); }
    }
    Error.Reset();
    return true;
}

FLDProtectionReceipt ALDGameMode::EndProtectionForAggressiveIntent(AController& Caller, const FLDMatchContext& Match,
    const FLDCombatBodyRef& Body, const FLDAggressiveIntent& Intent, const FLDGameplayWritePermit& Permit)
{
    FLDProtectionReceipt Receipt;
    Receipt.Generation = MatchContext.Generation;
    Receipt.Body = Body;
    Receipt.Request = Intent.Request;
    const auto Permission = ValidateWritePermit(Permit, ELDCommitPhase::Actions);
    if (Permission != ELDCommitReject::None) { Receipt.Reason = Permission; return Receipt; }
    if (Match != MatchContext) { Receipt.Reason = ELDCommitReject::StaleMatch; return Receipt; }
    auto* Controller = Cast<ALDPlayerController>(&Caller);
    const auto* Record = Controller ? Contenders.Find(Controller) : nullptr;
    auto* Life = Lives.Find(Body.Victim.EntityId);
    if (!Record || Record->Session != Body.Victim.EntityId || Body.Victim.Kind != ELDVictimKind::Contender
        || !Life || Life->Kind != ELDVictimKind::Contender)
    {
        Receipt.Reason = ELDCommitReject::NotAuthority;
        return Receipt;
    }
    if (Life->Phase != ELDLifePhase::Alive || Life->Life != Body.Victim.Life || Life->Body != Body)
    {
        Receipt.Reason = ELDCommitReject::StaleLife;
        return Receipt;
    }
    Receipt.LifeRevision = Life->Revision;
    Receipt.Reason = ELDCommitReject::None;
    if (AuthorityNowMs >= Life->ProtectedUntilMs)
    {
        Receipt.Outcome = ELDProtectionOutcome::NotProtected;
        return Receipt;
    }
    if (Life->Revision == MAX_uint64)
    {
        Receipt.Reason = ELDCommitReject::CounterExhausted;
        return Receipt;
    }
    FLDCaptureEligibilityEvent Ended;
    Ended.Body = Body;
    Ended.Change = ELDCaptureEligibilityChange::ProtectionEnded;
    FString Error;
    if (!FLDCaptureEventTime::FromMs(AuthorityNowMs, Ended.Time, Error) || !PublishEligibility(Permit, Ended, Error))
    {
        // The life and capture domains disagree about this protected body: an authority fault, not a refusal.
        FaultAuthority(Error);
        Receipt.Reason = ELDCommitReject::AuthorityFault;
        return Receipt;
    }
    // Removal is final for this life: a later action rejection never restores it.
    Life->ProtectedUntilMs = 0;
    ++Life->Revision;
    Receipt.LifeRevision = Life->Revision;
    Receipt.Outcome = ELDProtectionOutcome::Removed;
    Receipt.bProtectionRemoved = true;
    SyncOwner(Controller);
    NotifyLife(Body, ELDLifeChange::ProtectionEnded, Life->Revision, AuthorityNowMs);
    UE_LOG(LogLDMatch, Display, TEXT("LD_PROTECTION_REMOVED contender=%s life=%u intent=%u"),
        *Body.Victim.EntityId.ToString(), Body.Victim.Life, uint32(Intent.Kind));
    return Receipt;
}

bool ALDGameMode::ExpireProtections(const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (ValidateWritePermit(Permit, ELDCommitPhase::Session) != ELDCommitReject::None)
    {
        Error = TEXT("Protection expiry requires the current Session permit.");
        return false;
    }
    TArray<FGuid> Expired;
    for (const auto& Pair : Lives)
    {
        const auto& Life = Pair.Value;
        if (Life.Kind == ELDVictimKind::Contender && Life.Phase == ELDLifePhase::Alive
            && Life.ProtectedUntilMs != 0 && Life.ProtectedUntilMs <= AuthorityNowMs)
        {
            Expired.Add(Pair.Key);
        }
    }
    Expired.Sort();
    for (const FGuid& Id : Expired)
    {
        auto& Life = Lives.FindChecked(Id);
        if (Life.Revision == MAX_uint64)
        {
            Error = TEXT("Life revision exhausted at protection expiry.");
            return false;
        }
        // Half-open: the protection ends exactly at its deadline, which the driver made a boundary.
        FLDCaptureEligibilityEvent Ended;
        Ended.Body = Life.Body;
        Ended.Change = ELDCaptureEligibilityChange::ProtectionEnded;
        if (!FLDCaptureEventTime::FromMs(Life.ProtectedUntilMs, Ended.Time, Error) || !PublishEligibility(Permit, Ended, Error))
        {
            return false;
        }
        const int64 EndedAtMs = Life.ProtectedUntilMs;
        Life.ProtectedUntilMs = 0;
        ++Life.Revision;
        if (auto* Controller = FindContenderController(Id)) { SyncOwner(Controller); }
        NotifyLife(Life.Body, ELDLifeChange::ProtectionEnded, Life.Revision, EndedAtMs);
    }
    Error.Reset();
    return true;
}

bool ALDGameMode::EndContenderLife(const FLDCommittedDeath& Fact, const FLDGameplayWritePermit& Permit, FString& Error)
{
    auto* Life = Lives.Find(Fact.Key.Victim.EntityId);
    if (ValidateWritePermit(Permit, ELDCommitPhase::DeathsAndRewards) != ELDCommitReject::None || !Combat || !DemoContent
        || Fact.Key.Victim.Kind != ELDVictimKind::Contender || !Life || Life->Kind != ELDVictimKind::Contender
        || Life->Phase != ELDLifePhase::Alive || Life->Life != Fact.Key.Victim.Life || Life->Body != Fact.VictimBody)
    {
        Error = TEXT("A locked contender death must end exactly its current living body.");
        return false;
    }
    int64 RespawnAt = 0;
    if (Life->Revision == MAX_uint64
        || !LDMatchLifecycle::AddDurationMs(AuthorityNowMs, DemoContent->Get().Parameters.RespawnMs, RespawnAt, Error))
    {
        if (Error.IsEmpty()) { Error = TEXT("Life revision exhausted at death."); }
        return false;
    }
    FLDCaptureEligibilityEvent Died;
    Died.Body = Fact.VictimBody;
    Died.Change = ELDCaptureEligibilityChange::Died;
    Died.DeathKey = Fact.Key;
    if (!FLDCaptureEventTime::FromMs(Fact.OccurredAtMs, Died.Time, Error)
        || !KillCauseOf(Fact, Died.KillCause, Died.CreditContender, Error))
    {
        return false;
    }
    if (Died.KillCause == ELDKillCause::Contender) { Died.Assisters = Fact.Assisters; }
    if (!PublishEligibility(Permit, Died, Error))
    {
        return false;
    }
    auto* Controller = FindContenderController(Fact.Key.Victim.EntityId);
    // Orders and unreleased actions die with the life; released independent effects keep running.
    if (Controller) { Controller->Navigation->EndLifeOrders(); }
    Combat->RemoveRegisteredBody(Life->Body);
    Life->Phase = ELDLifePhase::Dead;
    Life->RetiredThroughLife = Life->Life;
    ++Life->Revision;
    Life->ProtectedUntilMs = 0;
    Life->RespawnAtMs = RespawnAt;
    Life->Body = {};
    if (auto* Body = Life->Actor.Get())
    {
        if (Controller && Controller->GetPawn() == Body) { Controller->UnPossess(); }
        Body->Destroy();
    }
    Life->Actor.Reset();
    if (Controller) { SyncOwner(Controller); }
    NotifyLife(Fact.VictimBody, ELDLifeChange::Died, Life->Revision, Fact.OccurredAtMs);
    // Killer (credit owner), fatal source and assisters are appended after the original fields (docs/DEVELOPMENT.md).
    FString Assisters;
    for (const FGuid& Assister : Fact.Assisters) { Assisters += (Assisters.IsEmpty() ? TEXT("") : TEXT(",")) + LDLog::Id(Assister); }
    UE_LOG(LogLDMatch, Display, TEXT("LD_DEATH_RECEIPT contender=%s life=%u respawn_at_ms=%lld t=%lld killer=%s cause=%s fatal=%s fatal_body=%s assisters=%s no_reward=%s victim_level=%u bounty=%lld"),
        *Fact.Key.Victim.EntityId.ToString(), Fact.Key.Victim.Life, RespawnAt, Fact.OccurredAtMs, *LDLog::Id(Fact.CreditOwner),
        *LDLog::EnumName(Died.KillCause), *LDLog::Source(Fact.ReleasedSource), *LDLog::Id(Fact.SourceBodyAtRelease.Victim.EntityId),
        Assisters.IsEmpty() ? TEXT("-") : *Assisters, *LDLog::EnumName(Fact.RewardBasis.NoRewardReason),
        uint32(Fact.RewardBasis.VictimLevelAtDeath), Fact.RewardBasis.LeaderBountyGoldAtDeath);
    Error.Reset();
    return true;
}

bool ALDGameMode::EndCampMemberLife(const FLDCommittedDeath& Fact, const FLDGameplayWritePermit& Permit, FString& Error)
{
    auto* Life = Lives.Find(Fact.Key.Victim.EntityId);
    if (ValidateWritePermit(Permit, ELDCommitPhase::DeathsAndRewards) != ELDCommitReject::None || !Combat
        || Fact.Key.Victim.Kind != ELDVictimKind::CampMember || !Life || Life->Kind != ELDVictimKind::CampMember
        || Life->Phase != ELDLifePhase::Alive || Life->Life != Fact.Key.Victim.Life || Life->Body != Fact.VictimBody
        || Life->Revision == MAX_uint64)
    {
        Error = TEXT("A locked camp death must end exactly its current living member.");
        return false;
    }
    // The camp owner destroys its actor after consuming the fact; Core only retires the combat life.
    Combat->RemoveRegisteredBody(Life->Body);
    Life->Phase = ELDLifePhase::Dead;
    Life->RetiredThroughLife = Life->Life;
    ++Life->Revision;
    Life->Body = {};
    Life->Actor.Reset();
    UE_LOG(LogLDMatch, Log, TEXT("LD_CAMP_DEATH member=%s life=%u monster=%s t=%lld killer=%s fatal=%s no_reward=%s"),
        *LDLog::Id(Fact.Key.Victim.EntityId), Fact.Key.Victim.Life, *Fact.RewardBasis.RewardDefinitionId.ToString(), Fact.OccurredAtMs,
        *LDLog::Id(Fact.CreditOwner), *LDLog::Source(Fact.ReleasedSource), *LDLog::EnumName(Fact.RewardBasis.NoRewardReason));
    Error.Reset();
    return true;
}

void ALDGameMode::BeginLifeMotion(FLDLifeRecord& Life, const FLDCombatBodyRef& Ref, ALDCharacter& Body)
{
    auto* Movement = Body.GetCharacterMovement();
    Life.MotionStreamId = FGuid::NewGuid();
    Life.MotionBody = Ref;
    Life.MotionThroughSequence = 0;
    Life.MotionAckStep = 0;
    Life.MotionAckSequence = 0;
    Life.MotionLastFoot = Movement->GetActorFeetLocation();
    Life.MotionGrounded = true;
    Life.MotionFloor = Movement->CurrentFloor.HitResult.GetComponent();
    // A body committed inside a step did not exist during that step's Motion phase.
    Life.CompletedMotionStep = bAuthorityStepOpen ? AuthorityStep.StepSerial : 0;
}

bool ALDGameMode::PlaceContenderBody(const FGuid& Contender, uint32 ProposedLife, const FLDGameplayWritePermit& Permit,
    FLDPlacedBody& Out, FString& Error)
{
    Out = {};
    auto* Registry = GetWorld()->GetSubsystem<ULDMapRegistry>();
    auto* Life = Lives.Find(Contender);
    auto* Controller = FindContenderController(Contender);
    FCollisionShape Capsule;
    if (!Registry || !DemoContent || !Life || Life->Kind != ELDVictimKind::Contender || !Controller
        || Controller->GetPawn() || ProposedLife == 0 || ProposedLife != Life->Life + 1
        || !ULDMapRegistry::ContentCapsule(*DemoContent, Capsule, Error))
    {
        if (Error.IsEmpty()) { Error = TEXT("Placement requires a registered bodiless contender and its next life."); }
        return false;
    }
    if (Life->SpawnForLife != ProposedLife)
    {
        if (Life->SpawnSequence == MAX_uint64) { Error = TEXT("Spawn request sequence exhausted."); return false; }
        ++Life->SpawnSequence;
        Life->SpawnForLife = ProposedLife;
    }
    // The first body fixes the contender's public identity slot: its position in the locked start roster.
    if (Life->PresentationSlot == MAX_uint8)
    {
        const int32 Slot = RosterOrder.IndexOfByKey(Contender);
        if (Slot == INDEX_NONE || Slot >= ALDCharacter::ContenderPresentations)
        {
            Error = TEXT("A placed contender has no public identity slot in the locked roster.");
            return false;
        }
        Life->PresentationSlot = uint8(Slot);
    }
    // A repeated request for the same life replays the original anchor instead of drawing again.
    const FLDSpawnRequestKey Request{MatchContext, Contender, ProposedLife, Life->SpawnSequence};
    TSharedPtr<const FLDSpawnSelection> Selection;
    Out.Contender = Contender;
    Out.Life = ProposedLife;
    Out.Controller = Controller;
    if (!Registry->DrawSpawnAnchor(Request, SpawnRandom, Permit, Selection, Error)
        || !Registry->ReserveSpawnPlacement(Selection, Capsule, Permit, Out.Reservation, Error))
    {
        return false;
    }
    const FTransform Transform(FRotator::ZeroRotator, Out.Reservation->BodyOrigin());
    auto* Body = GetWorld()->SpawnActorDeferred<ALDCharacter>(ALDCharacter::StaticClass(), Transform,
        Controller, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!Body)
    {
        Error = TEXT("Prepared body allocation failed.");
        ReleasePlacedBody(Out);
        return false;
    }
    Out.Body = Body;
    Body->SetReplicates(false);
    Body->SetActorHiddenInGame(true);
    Body->SetActorTickEnabled(false);
    Body->PresentationKey = Life->PresentationSlot;
    if (!Body->ConfigureDemoMovement(*DemoContent, Error))
    {
        ReleasePlacedBody(Out);
        return false;
    }
    Body->FinishSpawning(Transform);
    if (!IsValid(Body))
    {
        Error = TEXT("Prepared body failed native construction.");
        ReleasePlacedBody(Out);
        return false;
    }
    auto* Movement = Body->GetCharacterMovement();
    Movement->SetComponentTickEnabled(false);
    Movement->SetMovementMode(MOVE_Walking);
    Movement->FindFloor(Body->GetActorLocation(), Movement->CurrentFloor, false, nullptr);
    Movement->SetBaseFromFloor(Movement->CurrentFloor);
    if (!Movement->CurrentFloor.IsWalkableFloor()
        || Movement->CurrentFloor.HitResult.GetComponent() != Out.Reservation->Ground().Floor.Get()
        || !Body->GetActorLocation().Equals(Out.Reservation->BodyOrigin(), .000001))
    {
        Error = TEXT("Prepared body does not occupy its exact registered placement.");
        ReleasePlacedBody(Out);
        return false;
    }
    Error.Reset();
    return true;
}

void ALDGameMode::ReleasePlacedBody(FLDPlacedBody& Placed)
{
    if (auto* Body = Placed.Body.Get()) { Body->Destroy(); }
    Placed.Body.Reset();
    if (Placed.Reservation)
    {
        FString CleanupError;
        auto* Registry = GetWorld()->GetSubsystem<ULDMapRegistry>();
        if (!Registry || !Registry->ReleaseSpawnReservation(Placed.Reservation, CleanupError))
        {
            UE_LOG(LogLDMatch, Error, TEXT("LD_SPAWN_PREPARE_CLEANUP_FAILED %s"), *CleanupError);
        }
        Placed.Reservation.Reset();
    }
}

bool ALDGameMode::CommitPlacedLife(FLDPlacedBody& Placed, FLDBodyCombatState&& CombatState, int64 ProtectedUntilMs,
    const FLDGameplayWritePermit& Permit, FString& Error)
{
    auto* Body = Placed.Body.Get();
    auto* Life = Lives.Find(Placed.Contender);
    if (!Body || !Life || Life->Revision == MAX_uint64 || Life->Life + 1 != Placed.Life)
    {
        Error = TEXT("A prepared life lost its body or ledger record before commit.");
        return false;
    }
    FLDCombatBodyRef Ref;
    if (!Combat->InstallCombatBody(*Body, MoveTemp(CombatState), Permit, Ref, Error)) { return false; }
    Life->Life = Placed.Life;
    Life->Body = Ref;
    Life->Actor = Body;
    Life->Phase = ELDLifePhase::Alive;
    ++Life->Revision;
    Life->RespawnAtMs = 0;
    Life->ProtectedUntilMs = ProtectedUntilMs;
    Life->SpawnAnchor = Placed.Reservation->AnchorId();
    Life->SpawnOffset = Placed.Reservation->OffsetIndex();
    BeginLifeMotion(*Life, Ref, *Body);
    Error.Reset();
    return true;
}

bool ALDGameMode::ActivatePlacedBody(FLDPlacedBody& Placed, const FLDGameplayWritePermit& Permit, FString& Error)
{
    auto* Body = Placed.Body.Get();
    auto* Controller = Placed.Controller.Get();
    auto* Registry = GetWorld()->GetSubsystem<ULDMapRegistry>();
    if (!Body || !Controller || !Registry)
    {
        Error = TEXT("Committed life lost its body, controller or world before activation.");
        return false;
    }
    Body->SetReplicates(true);
    Controller->Possess(Body);
    if (!IsValid(Body) || Controller->GetPawn() != Body
        || !Registry->FinalizeSpawnPlacement(Placed.Reservation, Permit, Error))
    {
        if (Error.IsEmpty()) { Error = TEXT("Committed life failed native possession/finalization."); }
        return false;
    }
    Body->SetActorHiddenInGame(false);
    Body->SetActorTickEnabled(true);
    SyncOwner(Controller);
    if (!Controller->IsLocalController()) { Controller->Navigation->ResetLife(); }
    Error.Reset();
    return true;
}

bool ALDGameMode::SpawnInitialContenders(const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (ValidateWritePermit(Permit, ELDCommitPhase::RespawnsAndWorld) != ELDCommitReject::None
        || MatchPhase != ELDMatchPhase::Starting || !DemoContent || !Combat)
    {
        Error = TEXT("Initial spawn requires the current Starting permit and validated core providers.");
        return false;
    }
    auto* Registry = GetWorld()->GetSubsystem<ULDMapRegistry>();
    if (!Registry)
    {
        Error = TEXT("Initial spawn has no current world registry.");
        return false;
    }
    TArray<FGuid> Order = RosterOrder;
    Order.RemoveAll([&](const FGuid& Id) { return !IsMemberPresent(Id); });
    Order.Sort();
    if (Order.IsEmpty() || Order.Num() > 6)
    {
        Error = TEXT("Initial spawn requires a bounded registered contender roster.");
        return false;
    }
    struct FPrepared
    {
        FLDPlacedBody Placed;
        FLDBodyCombatState CombatState;
    };
    TArray<FPrepared> Prepared;
    Prepared.Reserve(Order.Num());
    bool CommitStarted = false;
    ON_SCOPE_EXIT
    {
        if (!CommitStarted)
        {
            for (auto& Entry : Prepared) { ReleasePlacedBody(Entry.Placed); }
        }
    };
    for (const auto& Id : Order)
    {
        const auto* Life = Lives.Find(Id);
        if (!Life || Life->Life != 0 || Life->Phase != ELDLifePhase::Unspawned || !Life->NativeMotion.IsEmpty())
        {
            Error = TEXT("The initial roster contains a stale, already spawned or unbound contender.");
            return false;
        }
        auto& Entry = Prepared.Emplace_GetRef();
        if (!PlaceContenderBody(Id, 1, Permit, Entry.Placed, Error)) { return false; }
        const FLDVictimKey Victim{ELDVictimKind::Contender, Id, 1};
        if (!Combat->PrepareContenderBody(*Entry.Placed.Body.Get(), Victim, Permit, Entry.CombatState, Error)) { return false; }
    }
    Combat->ReserveInitialBodyBatch(Prepared.Num());
    for (const auto& Entry : Prepared)
    {
        const auto* Life = Lives.Find(Entry.Placed.Contender);
        const auto* Controller = Entry.Placed.Controller.Get();
        if (ValidateWritePermit(Permit, ELDCommitPhase::RespawnsAndWorld) != ELDCommitReject::None
            || !Life || Life->Life != 0 || Life->Phase != ELDLifePhase::Unspawned
            || !Controller || Controller->GetPawn() || !Entry.Placed.Body.IsValid()
            || !Registry->ValidateSpawnReservation(Entry.Placed.Reservation, Permit, Entry.Placed.Body.Get(), Error))
        {
            if (Error.IsEmpty()) { Error = TEXT("Prepared initial batch became stale before commit."); }
            return false;
        }
    }
    CommitStarted = true;
    // The opening spawn grants no protection; only a respawn does.
    for (auto& Entry : Prepared)
    {
        if (!CommitPlacedLife(Entry.Placed, MoveTemp(Entry.CombatState), 0, Permit, Error))
        {
            FaultAuthority(Error);
            return false;
        }
    }
    for (auto& Entry : Prepared)
    {
        if (!ActivatePlacedBody(Entry.Placed, Permit, Error))
        {
            FaultAuthority(Error);
            return false;
        }
    }
    // Every contender joins the capture set at t=0 with its committed foot and native support.
    for (const auto& Entry : Prepared)
    {
        const auto& Life = Lives.FindChecked(Entry.Placed.Contender);
        FLDCaptureEligibilityEvent Joined;
        Joined.Body = Life.Body;
        Joined.Change = ELDCaptureEligibilityChange::Joined;
        Joined.InitialFootCm = Life.MotionLastFoot;
        Joined.InitialSupport.Grounded = Life.MotionGrounded;
        Joined.InitialSupport.Floor = Life.MotionFloor;
        if (!FLDCaptureEventTime::FromMs(AuthorityNowMs, Joined.Time, Error) || !PublishEligibility(Permit, Joined, Error))
        {
            FaultAuthority(Error);
            return false;
        }
        NotifyLife(Life.Body, ELDLifeChange::Joined, Life.Revision, AuthorityNowMs);
    }
    // The camps populate the world after every contender stands; a refusal aborts this start explicitly.
    if (!Camps)
    {
        Error = TEXT("The initial world has no camp owner for this match.");
        return false;
    }
    if (!Camps->SpawnInitialCamps(Permit, Error)) { return false; }
    Error.Reset();
    return true;
}

bool ALDGameMode::RespawnDueContenders(const FLDGameplayWritePermit& Permit, FString& Error)
{
    auto* Registry = GetWorld()->GetSubsystem<ULDMapRegistry>();
    if (ValidateWritePermit(Permit, ELDCommitPhase::RespawnsAndWorld) != ELDCommitReject::None
        || !Combat || !DemoContent || !Registry)
    {
        Error = TEXT("Respawn requires the current RespawnsAndWorld permit and validated providers.");
        return false;
    }
    TArray<FGuid> Due;
    for (const auto& Pair : Lives)
    {
        const auto& Life = Pair.Value;
        if (Life.Kind == ELDVictimKind::Contender && Life.Phase == ELDLifePhase::Dead
            && Life.RespawnAtMs <= AuthorityNowMs && IsMemberPresent(Pair.Key))
        {
            Due.Add(Pair.Key);
        }
    }
    if (Due.IsEmpty())
    {
        Error.Reset();
        return true;
    }
    Due.Sort();
    int64 ProtectedUntil = 0;
    if (!LDMatchLifecycle::AddDurationMs(AuthorityNowMs, DemoContent->Get().Parameters.ProtectionMs, ProtectedUntil, Error))
    {
        return false;
    }
    for (const FGuid& Id : Due)
    {
        const auto& Life = Lives.FindChecked(Id);
        // The first attempt is at the due boundary; later boundaries retry the same request.
        const bool bFirstAttempt = Life.RespawnAtMs == AuthorityNowMs;
        if (!Life.NativeMotion.IsEmpty())
        {
            if (bFirstAttempt)
            {
                UE_LOG(LogLDMatch, Warning, TEXT("LD_DEMO_RESPAWN_WAITING contender=%s unconsumed_motion=%d"),
                    *Id.ToString(), Life.NativeMotion.Num());
            }
            continue;
        }
        if (Life.Life == MAX_uint32)
        {
            Error = TEXT("Contender life counter exhausted.");
            return false;
        }
        const uint32 NewLife = Life.Life + 1;
        FLDPlacedBody Placed;
        FLDBodyCombatState CombatState;
        if (!PlaceContenderBody(Id, NewLife, Permit, Placed, Error)
            || !Combat->PrepareContenderBody(*Placed.Body.Get(), {ELDVictimKind::Contender, Id, NewLife}, Permit, CombatState, Error)
            || !Registry->ValidateSpawnReservation(Placed.Reservation, Permit, Placed.Body.Get(), Error))
        {
            ReleasePlacedBody(Placed);
            if (!LDGameModeLifeDetail::IsSpawnBlocked(Error)) { return false; }
            // Same selected anchor, one attempt per authority boundary; the life stays Dead meanwhile.
            if (bFirstAttempt)
            {
                UE_LOG(LogLDMatch, Warning, TEXT("LD_DEMO_RESPAWN_BLOCKED contender=%s life=%u %s"), *Id.ToString(), NewLife, *Error);
            }
            continue;
        }
        if (!CommitPlacedLife(Placed, MoveTemp(CombatState), ProtectedUntil, Permit, Error)
            || !ActivatePlacedBody(Placed, Permit, Error))
        {
            return false;
        }
        // Respawned carries the final Alive state and its protection end in one fact.
        const auto& Committed = Lives.FindChecked(Id);
        FLDCaptureEligibilityEvent Respawned;
        Respawned.Body = Committed.Body;
        Respawned.Change = ELDCaptureEligibilityChange::Respawned;
        Respawned.ProtectedUntilMs = Committed.ProtectedUntilMs;
        Respawned.InitialFootCm = Committed.MotionLastFoot;
        Respawned.InitialSupport.Grounded = Committed.MotionGrounded;
        Respawned.InitialSupport.Floor = Committed.MotionFloor;
        if (!FLDCaptureEventTime::FromMs(AuthorityNowMs, Respawned.Time, Error) || !PublishEligibility(Permit, Respawned, Error))
        {
            return false;
        }
        NotifyLife(Committed.Body, ELDLifeChange::Respawned, Committed.Revision, AuthorityNowMs);
        UE_LOG(LogLDMatch, Display, TEXT("LD_RETURN_RECEIPT contender=%s life=%u protected_until_ms=%lld"),
            *Id.ToString(), NewLife, ProtectedUntil);
    }
    Error.Reset();
    return true;
}

bool ALDGameMode::SpawnCampMembers(const TArray<FLDCampMemberSpawn>& Spawns, const FLDGameplayWritePermit& Permit,
    TArray<FLDCombatBodyRef>& OutBodies, FString& Error)
{
    OutBodies.Reset();
    if (ValidateWritePermit(Permit, ELDCommitPhase::RespawnsAndWorld) != ELDCommitReject::None || !Combat || Spawns.IsEmpty())
    {
        Error = TEXT("Camp members spawn only as a nonempty group inside the current RespawnsAndWorld permit.");
        return false;
    }
    TSet<FGuid> Seen;
    TArray<FLDBodyCombatState> Prepared;
    Prepared.Reserve(Spawns.Num());
    for (const auto& Spawn : Spawns)
    {
        bool bDuplicate = false;
        Seen.Add(Spawn.EntityId, &bDuplicate);
        const auto* Life = Lives.Find(Spawn.EntityId);
        auto* Body = Spawn.Body;
        if (!Spawn.EntityId.IsValid() || bDuplicate || !IsValid(Body) || Body->GetWorld() != GetWorld()
            || !Body->GetCharacterMovement()->CurrentFloor.IsWalkableFloor()
            || (Life && (Life->Kind != ELDVictimKind::CampMember || Life->Phase == ELDLifePhase::Alive
                || Life->Phase == ELDLifePhase::Left || Life->Life == MAX_uint32 || Life->Revision == MAX_uint64
                || !Life->NativeMotion.IsEmpty())))
        {
            Error = TEXT("A camp group member is duplicated, unplaced, still alive or not a camp entity.");
            return false;
        }
        const FLDVictimKey Victim{ELDVictimKind::CampMember, Spawn.EntityId, (Life ? Life->Life : 0) + 1};
        if (!Combat->PrepareCampMemberBody(*Body, Victim, Spawn.MonsterId, Permit, Prepared.Emplace_GetRef(), Error))
        {
            return false;
        }
    }
    // Every member is prepared: the whole group commits without a normal failure branch.
    for (int32 Index = 0; Index < Spawns.Num(); ++Index)
    {
        const auto& Spawn = Spawns[Index];
        const uint32 NewLife = Prepared[Index].Victim.Life;
        FLDCombatBodyRef Ref;
        if (!Combat->InstallCombatBody(*Spawn.Body, MoveTemp(Prepared[Index]), Permit, Ref, Error))
        {
            // Members before this one are already live: a broken group commit is an authority fault.
            FaultAuthority(Error);
            return false;
        }
        auto& Life = Lives.FindOrAdd(Spawn.EntityId);
        Life.Kind = ELDVictimKind::CampMember;
        Life.Life = NewLife;
        Life.Phase = ELDLifePhase::Alive;
        ++Life.Revision;
        Life.Body = Ref;
        Life.Actor = Spawn.Body;
        Life.RespawnAtMs = 0;
        Life.ProtectedUntilMs = 0;
        BeginLifeMotion(Life, Ref, *Spawn.Body);
        OutBodies.Add(Ref);
    }
    Error.Reset();
    return true;
}

bool ALDGameMode::ProjectLife(ALDPlayerController& Controller, FLDOwnerState& Out, FString& Error) const
{
    const auto* Record = Contenders.Find(&Controller);
    const auto* Life = Record ? Lives.Find(Record->Session) : nullptr;
    if (!Record || !Life)
    {
        Error = TEXT("The owner has no current contender/life projection.");
        return false;
    }
    const auto* State = Combat && Life->Actor.IsValid() ? Combat->StateFor(Life->Actor.Get()) : nullptr;
    if (Life->Phase == ELDLifePhase::Alive
        && (!State || !Life->Body.IsValid() || State->Identity != Life->Body.BodyIdentity
            || State->Victim != Life->Body.Victim || !State->Resources.Revision))
    {
        Error = TEXT("A living owner has no matching committed resource/body projection.");
        return false;
    }
    FLDOwnerState Candidate = Out;
    Candidate.ContenderId = Record->Session;
    Candidate.Life = Life->Life;
    Candidate.LifePhase = Life->Phase;
    Candidate.LifeRevision = Life->Revision;
    Candidate.BodyRef = Life->Body;
    Candidate.bActive = Life->Phase == ELDLifePhase::Alive;
    Candidate.RespawnAtMs = Life->RespawnAtMs;
    Candidate.ProtectedUntilMs = Life->ProtectedUntilMs;
    if (State)
    {
        Candidate.Hp = double(State->Resources.HpP2) / 100.;
        Candidate.MaxHp = double(State->Resources.MaxHpP2) / 100.;
        Candidate.Mp = double(State->Resources.MpP2) / 100.;
        Candidate.MaxMp = double(State->Resources.MaxMpP2) / 100.;
        Candidate.Barrier = double(State->Resources.ShieldP2) / 100.;
    }
    else { Candidate.Hp = Candidate.Mp = Candidate.Barrier = 0; }
    Out = MoveTemp(Candidate);
    Error.Reset();
    return true;
}

void ALDGameMode::RemoveLifeBinding(const FGuid& EntityId)
{
    auto* Life = Lives.Find(EntityId);
    if (!Life || Life->Phase == ELDLifePhase::Left) { return; }
    Life->Phase = ELDLifePhase::Left;
    Life->RetiredThroughLife = Life->Life;
    if (Life->Revision == MAX_uint64) { FaultAuthority(TEXT("Life revision exhausted while leaving.")); }
    else { ++Life->Revision; }
    Life->ProtectedUntilMs = 0;
    Life->RespawnAtMs = 0;
    // Native disconnect may already have destroyed the Pawn before Logout.
    // Retire Combat ownership by its stable binding, not weak-pointer liveness.
    if (Combat)
    {
        if (Life->Body.IsValid()) { Combat->RemoveRegisteredBody(Life->Body); }
        // Leaving cancels the entity's unsettled independent effects; earlier committed facts stay.
        Combat->CancelEntityEffects(EntityId);
    }
    if (auto* Body = Life->Actor.Get())
    {
        if (auto* Controller = Cast<ALDPlayerController>(Body->GetController()))
        {
            Controller->Navigation->ResetLife();
            Controller->UnPossess();
        }
        Body->Destroy();
    }
    Life->Body = {};
    Life->Actor.Reset();
}
