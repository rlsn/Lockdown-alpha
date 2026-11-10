#include "Framework/LDGameMode.h"
#include "Core/LDLog.h"
#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDevelopmentDirector.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Region/LDMapGeometry.h"
#include "Framework/LDPlayerController.h"
#include "Navigation/LDNavigationComponent.h"
#include "Region/LDMapRegistry.h"
#include "World/LDCampDirector.h"
#include "Engine/World.h"

// #198 rest phases (approved outer parameters, issue #198 comment 6752601175 §1/§8/§9; the private rest space is a
// coordinator decision under the user's delegation). The pure FLDMatchTimeline schedules the rests and
// the field progress T; this file applies its transitions to lives, the world and the public read model.
//
// Private rest space: every contender's rest life stands at a fixed approved spawn anchor chosen by its locked roster slot
// (same world, same connection, same contender identity and growth record), in its own Combat isolation group. The frozen
// world (camp members, Boss) is suspended in Combat and takes no motion, intent or contact.
//
// Phase boundaries (approved §8): a field -> rest boundary retires every living field life (no death, kill or reward),
// ends the remaining field respawn waits, cancels every in-flight effect and control, and gives everyone a fresh rest life
// at full HP/MP with every cooldown ready. A rest -> field boundary does the same in reverse: every rest life is retired and
// the next field life stands at the remembered field foot (or within REST.ReturnSearchRadiusCm, else a normal spawn; a
// contender dead at the rest entry takes a normal spawn) with the shared LIFE.ProtectionSec.

bool ALDGameMode::InitializeTimeline(FString& Error)
{
    FLDTimelineParameters Parameters;
    if (!DemoContent || !FLDTimelineParameters::FromContent(DemoContent->Get().Parameters, Parameters, Error)
        || !Timeline.Begin(Parameters, AuthorityNowMs, Error))
    {
        if (Error.IsEmpty()) { Error = TEXT("The timeline needs the approved content parameters."); }
        return false;
    }
    TimelineRevision = 1;
    RestSignups.Reset();
    LockedEntrants.Reset();
    RestReturns.Reset();
    RestLives.Reset();
    PendingPhasePlacement.Reset();
    UE_LOG(LogLDMatch, Display, TEXT("LD_TIMELINE_BEGIN generation=%u at_ms=%lld rest=0 build_ends_at_ms=%lld"),
        MatchContext.Generation, AuthorityNowMs, Timeline.GetStepEndsAtMs());
    Error.Reset();
    return true;
}

bool ALDGameMode::IsRestPhase(const FLDMatchContext& Context) const
{
    return IsGameplayWriteOpen(Context) && MatchPhase == ELDMatchPhase::Rest;
}

bool ALDGameMode::IsInputLocked(const FLDMatchContext& Context) const
{
    return IsRestPhase(Context) && Timeline.GetStep() == ELDRestStep::Return;
}

LDProgression::EMatchStage ALDGameMode::ReadBuildStage(const FLDMatchContext& Match) const
{
    if (!IsGameplayWriteOpen(Match) || !Timeline.IsActive()) { return LDProgression::EMatchStage::Closed; }
    if (MatchPhase == ELDMatchPhase::Rest)
    {
        return Timeline.IsRestBuildOpen() ? LDProgression::EMatchStage::Rest : LDProgression::EMatchStage::RestLocked;
    }
    return LDProgression::EMatchStage::Live;
}

int64 ALDGameMode::GetFieldNowMs() const
{
    return Timeline.IsActive() ? Timeline.FieldMsAt(AuthorityNowMs) : 0;
}

bool ALDGameMode::IsFieldRunning() const
{
    return IsGameplayWriteOpen(MatchContext) && MatchPhase != ELDMatchPhase::Rest && Timeline.IsFieldRunning();
}

int64 ALDGameMode::FieldDueAuthorityMs(int64 FieldAtMs) const
{
    return IsFieldRunning() ? Timeline.FieldDueAuthorityMs(AuthorityNowMs, FieldAtMs) : MAX_int64;
}

int64 ALDGameMode::NextTimelineDueMs() const
{
    return IsGameplayWriteOpen(MatchContext) && Timeline.IsActive() ? Timeline.NextDueMs() : MAX_int64;
}

int64 ALDGameMode::RespawnWaitAtFieldMs(int64 FieldT) const
{
    return Timeline.IsActive() ? Timeline.RespawnWaitMs(FieldT) : 0;
}

bool ALDGameMode::IsInRestSpace(const FGuid& Contender) const
{
    return RestLives.Contains(Contender);
}

FName ALDGameMode::RestAnchorFor(const FGuid& Contender) const
{
    // The six roster slots spread evenly over the approved spawn anchors in catalog order (with the twelve current
    // anchors: s00, s02, s04, s06, s08, s10, at least ~2200 cm apart).
    const int32 Slot = RosterOrder.IndexOfByKey(Contender);
    if (!DemoContent || Slot == INDEX_NONE || Slot >= 6) { return NAME_None; }
    const TArray<FString>& Anchors = DemoContent->Get().World.SpawnIds;
    if (Anchors.Num() < 6) { return NAME_None; }
    return FName(*Anchors[Slot * Anchors.Num() / 6]);
}

void ALDGameMode::PublishTimeline()
{
    if (TimelineRevision == MAX_uint64)
    {
        FaultAuthority(TEXT("Timeline revision exhausted."));
        return;
    }
    ++TimelineRevision;
    bPublicMatchDirty = true;
    bLobbyDirty = true;
}

ELDRestSignupResult ALDGameMode::SubmitRestSignup(ALDPlayerController& Controller, uint32 Generation, bool bJoin)
{
    const FGuid Id = ContenderId(&Controller);
    if (!Id.IsValid() || !IsMemberPresent(Id)) { return ELDRestSignupResult::NotContender; }
    if (Generation != MatchContext.Generation) { return ELDRestSignupResult::StaleMatch; }
    if (!IsRestPhase(MatchContext) || !RestLives.Contains(Id)) { return ELDRestSignupResult::NotInRest; }
    if (!Timeline.IsSignupOpen(AuthorityNowMs)) { return ELDRestSignupResult::WindowClosed; }
    const FLDMatchProviderHooks* PK = LDMatchProviders::Find(ELDMatchProvider::PK);
    if (!PK || !PK->RestStep) { return ELDRestSignupResult::NoPKProvider; }
    const bool bChanged = bJoin ? !RestSignups.Contains(Id) : RestSignups.Contains(Id);
    if (bJoin) { RestSignups.Add(Id); } else { RestSignups.Remove(Id); }
    if (bChanged)
    {
        bPublicMatchDirty = true;
        UE_LOG(LogLDMatch, Display, TEXT("LD_REST_SIGNUP contender=%s join=%d rest=%u signups=%d"),
            *LDLog::Id(Id), bJoin ? 1 : 0, Timeline.GetRestIndex(), RestSignups.Num());
    }
    return bJoin ? ELDRestSignupResult::Joined : ELDRestSignupResult::Withdrawn;
}

bool ALDGameMode::FinishRestPK(const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (ValidateWritePermit(Permit, Permit.GetPhase()) != ELDCommitReject::None || !IsRestPhase(MatchContext)
        || !Timeline.FinishPK(Permit.GetBoundary().AtMs, Error))
    {
        if (Error.IsEmpty()) { Error = TEXT("Only a running PK ends early, under the current permit."); }
        return false;
    }
    PublishTimeline();
    return true;
}

bool ALDGameMode::AdvanceTimeline(const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (ValidateWritePermit(Permit, ELDCommitPhase::RespawnsAndWorld) != ELDCommitReject::None || !Timeline.IsActive())
    {
        Error = TEXT("The timeline advances only under the current RespawnsAndWorld permit of an active match.");
        return false;
    }
    TArray<FLDTimelineEvent> Events;
    if (!Timeline.Advance(Permit.GetBoundary().AtMs, RestSignups.Num(), false, Events, Error)
        || !ApplyTimelineEvents(Events, Permit, Error)
        || !RetryPhasePlacements(Permit, Error)
        || (IsRestPhase(MatchContext) && !ProcessTrainingResets(Permit, Error)))
    {
        return false;
    }
    if (!Events.IsEmpty()) { PublishTimeline(); }
    Error.Reset();
    return true;
}

bool ALDGameMode::LockTimelineBuild(const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (ValidateWritePermit(Permit, ELDCommitPhase::Transactions) != ELDCommitReject::None || !Timeline.IsActive())
    {
        Error = TEXT("The build lock needs the current Transactions permit of an active match.");
        return false;
    }
    const int64 AtMs = Permit.GetBoundary().AtMs;
    if (!Timeline.IsBuildLockDue(AtMs)) { return true; }
    // Only present contenders still standing in their rest space enter; the locked list never changes afterwards.
    LockedEntrants.Reset();
    for (const FGuid& Id : RestSignups)
    {
        if (IsMemberPresent(Id) && RestLives.Contains(Id)) { LockedEntrants.Add(Id); }
    }
    LockedEntrants.Sort();
    RestSignups.Reset();
    TArray<FLDTimelineEvent> Events;
    if (!Timeline.Advance(AtMs, LockedEntrants.Num(), true, Events, Error)) { return false; }
    if (Timeline.GetStep() == ELDRestStep::Return) { LockedEntrants.Reset(); }
    for (const FLDTimelineEvent& Event : Events)
    {
        // The lock enters Draw or Return; neither moves a body, so no RespawnsAndWorld work can be due here.
        if (Event.Kind == ELDTimelineEventKind::EnterRest || Event.Kind == ELDTimelineEventKind::EnterField)
        {
            Error = TEXT("A build lock cannot enter a rest or a field in the same Transactions phase.");
            return false;
        }
    }
    if (!ApplyTimelineEvents(Events, Permit, Error)) { return false; }
    PublishTimeline();
    return true;
}

bool ALDGameMode::ApplyTimelineEvents(const TArray<FLDTimelineEvent>& Events, const FLDGameplayWritePermit& Permit, FString& Error)
{
    const FLDMatchProviderHooks* PK = LDMatchProviders::Find(ELDMatchProvider::PK);
    for (const FLDTimelineEvent& Event : Events)
    {
        switch (Event.Kind)
        {
        case ELDTimelineEventKind::EnterRest:
            UE_LOG(LogLDMatch, Display, TEXT("LD_REST_ENTER generation=%u rest=%u at_ms=%lld field_t_ms=%lld"),
                MatchContext.Generation, Event.RestIndex, Event.AtMs, Event.Value);
            // The phase changes first: the retiring field lives leave the spray roster as a rest boundary.
            MatchPhase = ELDMatchPhase::Rest;
            if (!MoveContendersToRest(Permit, Error) || !EnterRestWorld(Permit, Error)) { return false; }
            break;
        case ELDTimelineEventKind::EnterStep:
            UE_LOG(LogLDMatch, Display, TEXT("LD_REST_STEP generation=%u rest=%u step=%s at_ms=%lld"),
                MatchContext.Generation, Event.RestIndex, LDMatchTimeline::StepName(Event.Step), Event.AtMs);
            if (Event.Step == ELDRestStep::Build)
            {
                RestSignups.Reset();
                LockedEntrants.Reset();
            }
            break;
        case ELDTimelineEventKind::LockBuild:
            UE_LOG(LogLDMatch, Display, TEXT("LD_REST_LOCK generation=%u rest=%u at_ms=%lld entrants=%lld pk=%s"),
                MatchContext.Generation, Event.RestIndex, Event.AtMs, Event.Value, Event.Value >= 2 ? TEXT("run") : TEXT("skipped"));
            break;
        case ELDTimelineEventKind::EnterField:
        {
            const bool bFinal = Event.FieldSegment >= FLDTimelineParameters::TimedSegments;
            // Rest lives return while the phase is still Rest (the R0 return is every contender's spray-roster join).
            if (!ExitRestWorld(Permit, Error) || !MoveContendersToField(bFinal, Permit, Error)) { return false; }
            MatchPhase = ELDMatchPhase::Development;
            if (bFinal && !CaptureReducer.ScheduleOpen(Event.AtMs, Error)) { return false; }
            LockedEntrants.Reset();
            UE_LOG(LogLDMatch, Display, TEXT("LD_FIELD_ENTER generation=%u segment=%u at_ms=%lld field_t_ms=%lld final=%d"),
                MatchContext.Generation, Event.FieldSegment, Event.AtMs, Event.Value, bFinal ? 1 : 0);
            break;
        }
        }
        // Session D's PK hears every step entry and the lock; without it the rest simply skips its PK.
        if (PK && PK->RestStep && (Event.Kind == ELDTimelineEventKind::EnterStep || Event.Kind == ELDTimelineEventKind::LockBuild))
        {
            FLDRestStepNotice Notice;
            Notice.Match = MatchContext;
            Notice.RestIndex = Event.RestIndex;
            Notice.Step = Event.Step;
            Notice.bBuildLocked = Event.Kind == ELDTimelineEventKind::LockBuild;
            Notice.AtMs = Event.AtMs;
            Notice.Entrants = LockedEntrants;
            if (!PK->RestStep(*this, Notice, Permit, Error))
            {
                if (Error.IsEmpty()) { Error = TEXT("The PK provider refused a rest step."); }
                return false;
            }
        }
    }
    Error.Reset();
    return true;
}

bool ALDGameMode::EnterRestWorld(const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (ValidateWritePermit(Permit, ELDCommitPhase::RespawnsAndWorld) != ELDCommitReject::None || !Combat || !Camps)
    {
        Error = TEXT("The rest world needs the current RespawnsAndWorld permit, Combat and the camp owner.");
        return false;
    }
    if (Combat->CancelAllForPhase(Permit) == INDEX_NONE)
    {
        Error = TEXT("Combat could not end the field's effects at the rest boundary.");
        return false;
    }
    // #198: unclaimed charms leave at rest entry without any reward.
    if (!EndCharmsForRest(Permit, Error)) { return false; }
    return Camps->FreezeForRest(Permit, Error);
}

bool ALDGameMode::ExitRestWorld(const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (ValidateWritePermit(Permit, ELDCommitPhase::RespawnsAndWorld) != ELDCommitReject::None || !Combat || !Camps)
    {
        Error = TEXT("The field world needs the current RespawnsAndWorld permit, Combat and the camp owner.");
        return false;
    }
    // Training effects never reach the field.
    if (Combat->CancelAllForPhase(Permit) == INDEX_NONE)
    {
        Error = TEXT("Combat could not end the rest's effects at the field boundary.");
        return false;
    }
    return Camps->ThawAfterRest(Permit, Error);
}

bool ALDGameMode::RetireLifeForPhase(const FGuid& Contender, const FLDGameplayWritePermit& Permit, FString& Error)
{
    FLDLifeRecord* Life = Lives.Find(Contender);
    if (!Life || Life->Kind != ELDVictimKind::Contender || Life->Phase != ELDLifePhase::Alive || Life->Revision == MAX_uint64)
    {
        Error = TEXT("A phase boundary retires exactly a living contender life.");
        return false;
    }
    const FLDCombatBodyRef Body = Life->Body;
    // The owner's training dummy leaves with its rest life; nothing of the training reaches the field.
    RetireTrainingDummy(Contender);
    if (!RestLives.Contains(Contender))
    {
        // A field life leaves the spray roster without a kill, a death count or a reward.
        FLDCaptureEligibilityEvent Retired;
        Retired.Body = Body;
        Retired.Change = ELDCaptureEligibilityChange::PhaseRetired;
        if (!FLDCaptureEventTime::FromMs(AuthorityNowMs, Retired.Time, Error) || !PublishEligibility(Permit, Retired, Error))
        {
            return false;
        }
    }
    auto* Controller = FindContenderController(Contender);
    if (Controller) { Controller->Navigation->EndLifeOrders(); }
    Combat->RetireBodyForPhase(Body);
    Life->Phase = ELDLifePhase::Dead;
    Life->RetiredThroughLife = Life->Life;
    ++Life->Revision;
    Life->ProtectedUntilMs = 0;
    Life->RespawnAtMs = 0;
    Life->Body = {};
    if (auto* Actor = Life->Actor.Get())
    {
        if (Controller && Controller->GetPawn() == Actor) { Controller->UnPossess(); }
        Actor->Destroy();
    }
    Life->Actor.Reset();
    // This step's batches of the retired body were consumed when accepted (capture reads them synchronously); release
    // them now, like the next step would, so the next life can be placed at this boundary.
    if (!Life->NativeMotion.IsEmpty())
    {
        Life->MotionAckStep = Life->NativeMotion.Last()->Step.StepSerial;
        Life->MotionAckSequence = Life->NativeMotion.Last()->ThroughSequence;
        Life->NativeMotion.Reset();
    }
    RestLives.Remove(Contender);
    if (Controller) { SyncOwner(Controller); }
    NotifyLife(Body, ELDLifeChange::PhaseRetired, Life->Revision, AuthorityNowMs);
    Error.Reset();
    return true;
}

bool ALDGameMode::PlacePhaseLife(const FGuid& Contender, bool bRest, int64 ProtectedUntilMs,
    const FLDGameplayWritePermit& Permit, bool& bBlocked, FString& Error)
{
    bBlocked = false;
    auto* Registry = GetWorld()->GetSubsystem<ULDMapRegistry>();
    const FLDLifeRecord* Life = Lives.Find(Contender);
    if (!Registry || !Combat || !Life || Life->Phase != ELDLifePhase::Dead || Life->Life == MAX_uint32)
    {
        Error = TEXT("A phase placement needs a bodiless contender, its next life and the registered world.");
        return false;
    }
    if (!Life->NativeMotion.IsEmpty())
    {
        // The retired body's last motion is consumed next step; the placement retries then.
        bBlocked = true;
        return true;
    }
    FLDPlacementTarget Target;
    if (bRest)
    {
        Target.Kind = FLDPlacementTarget::EKind::Anchor;
        Target.Anchor = RestAnchorFor(Contender);
        if (Target.Anchor.IsNone())
        {
            Error = TEXT("A contender has no private rest anchor.");
            return false;
        }
    }
    else if (const FLDRestReturn* Return = RestReturns.Find(Contender); Return && Return->bAliveAtRest)
    {
        double RadiusCm = 0;
        const FLDDemoRational& Radius = DemoContent->Get().Parameters.RestReturnSearchRadiusCm;
        if (Radius.Denominator <= 0 || Radius.Numerator < 0)
        {
            Error = TEXT("The approved return search radius is invalid.");
            return false;
        }
        RadiusCm = double(Radius.Numerator) / double(Radius.Denominator);
        Target.Kind = FLDPlacementTarget::EKind::Near;
        Target.FootCm = Return->FieldFootCm;
        Target.Surface = Return->FieldSurface;
        Target.RadiusCm = RadiusCm;
    }
    const uint32 NewLife = Life->Life + 1;
    FLDPlacedBody Placed;
    FLDBodyCombatState CombatState;
    if (!PlaceContenderBody(Contender, NewLife, Permit, Placed, Error, Target)
        || !Combat->PrepareContenderBody(*Placed.Body.Get(), {ELDVictimKind::Contender, Contender, NewLife}, Permit, CombatState, Error)
        || !Registry->ValidateSpawnReservation(Placed.Reservation, Permit, Placed.Body.Get(), Error))
    {
        ReleasePlacedBody(Placed);
        if (!Error.StartsWith(TEXT("SpawnBlocked"))) { return false; }
        bBlocked = true;
        UE_LOG(LogLDMatch, Warning, TEXT("LD_PHASE_PLACEMENT_BLOCKED contender=%s life=%u rest=%d %s"),
            *LDLog::Id(Contender), NewLife, bRest ? 1 : 0, *Error);
        Error.Reset();
        return true;
    }
    if (!CommitPlacedLife(Placed, MoveTemp(CombatState), ProtectedUntilMs, Permit, Error)
        || !ActivatePlacedBody(Placed, Permit, Error))
    {
        FaultAuthority(Error);
        return false;
    }
    const FLDLifeRecord& Committed = Lives.FindChecked(Contender);
    if (bRest)
    {
        RestLives.Add(Contender);
        if (!Combat->SetRestIsolation(Committed.Body, true, Permit, Error) || !SpawnTrainingDummy(Contender, Permit, Error))
        {
            return false;
        }
        NotifyLife(Committed.Body, ELDLifeChange::Respawned, Committed.Revision, AuthorityNowMs);
    }
    else
    {
        // The first field life (the R0 return) joins the spray roster; every later one returns to it.
        FLDCaptureEligibilityEvent Fact;
        Fact.Body = Committed.Body;
        Fact.Change = CaptureReducer.IsCommittedMember(Contender) ? ELDCaptureEligibilityChange::Respawned
            : ELDCaptureEligibilityChange::Joined;
        Fact.ProtectedUntilMs = Committed.ProtectedUntilMs;
        Fact.InitialFootCm = Committed.MotionLastFoot;
        Fact.InitialSupport.Grounded = Committed.MotionGrounded;
        Fact.InitialSupport.Floor = Committed.MotionFloor;
        if (!FLDCaptureEventTime::FromMs(AuthorityNowMs, Fact.Time, Error) || !PublishEligibility(Permit, Fact, Error))
        {
            return false;
        }
        NotifyLife(Committed.Body, Fact.Change == ELDCaptureEligibilityChange::Joined ? ELDLifeChange::Joined
            : ELDLifeChange::Respawned, Committed.Revision, AuthorityNowMs);
    }
    PendingPhasePlacement.Remove(Contender);
    UE_LOG(LogLDMatch, Display, TEXT("LD_PHASE_PLACED contender=%s life=%u rest=%d anchor=%s protected_until_ms=%lld"),
        *LDLog::Id(Contender), NewLife, bRest ? 1 : 0, *Committed.SpawnAnchor.ToString(), ProtectedUntilMs);
    return Combat->ReadyAllForPhase(Contender, Permit, Error);
}

bool ALDGameMode::MoveContendersToRest(const FLDGameplayWritePermit& Permit, FString& Error)
{
    auto* Registry = GetWorld()->GetSubsystem<ULDMapRegistry>();
    const FLDMapBinding* Capture = Registry && DemoContent
        ? Registry->Resolve(FName(*DemoContent->Get().World.CaptureId), Registry->Generation) : nullptr;
    if (!Capture)
    {
        Error = TEXT("The rest entry needs the registered field surface.");
        return false;
    }
    TArray<FGuid> Order = RosterOrder;
    Order.RemoveAll([this](const FGuid& Id) { return !IsMemberPresent(Id); });
    Order.Sort();
    // Every field body is retired before any rest body is placed.
    for (const FGuid& Id : Order)
    {
        FLDLifeRecord& Life = Lives.FindChecked(Id);
        FLDRestReturn& Return = RestReturns.FindOrAdd(Id);
        Return = {};
        PendingPhasePlacement.Remove(Id);
        if (Life.Phase == ELDLifePhase::Alive)
        {
            Return.bAliveAtRest = true;
            Return.FieldFootCm = Life.MotionLastFoot;
            Return.FieldSurface = Capture->Surface;
            if (!RetireLifeForPhase(Id, Permit, Error)) { return false; }
        }
        else
        {
            // The rest ends the remaining field respawn wait.
            Life.RespawnAtMs = 0;
        }
    }
    for (const FGuid& Id : Order)
    {
        bool bBlocked = false;
        if (!PlacePhaseLife(Id, true, 0, Permit, bBlocked, Error)) { return false; }
        if (bBlocked) { PendingPhasePlacement.Add(Id); }
    }
    Error.Reset();
    return true;
}

bool ALDGameMode::MoveContendersToField(bool bFinalField, const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (!DemoContent)
    {
        Error = TEXT("The field return needs the approved content.");
        return false;
    }
    int64 ProtectedUntil = 0;
    if (!LDMatchLifecycle::AddDurationMs(AuthorityNowMs, DemoContent->Get().Parameters.ProtectionMs, ProtectedUntil, Error))
    {
        return false;
    }
    TArray<FGuid> Order = RosterOrder;
    Order.RemoveAll([this](const FGuid& Id) { return !IsMemberPresent(Id); });
    Order.Sort();
    for (const FGuid& Id : Order)
    {
        PendingPhasePlacement.Remove(Id);
        if (Lives.FindChecked(Id).Phase == ELDLifePhase::Alive && !RetireLifeForPhase(Id, Permit, Error)) { return false; }
    }
    // Survivors claim their old spots first, so a normal draw never takes a returning foot.
    Order.StableSort([this](const FGuid& A, const FGuid& B)
    {
        const FLDRestReturn* RA = RestReturns.Find(A);
        const FLDRestReturn* RB = RestReturns.Find(B);
        return (RA && RA->bAliveAtRest) && !(RB && RB->bAliveAtRest);
    });
    for (const FGuid& Id : Order)
    {
        bool bBlocked = false;
        if (!PlacePhaseLife(Id, false, ProtectedUntil, Permit, bBlocked, Error)) { return false; }
        if (bBlocked) { PendingPhasePlacement.Add(Id); }
    }
    RestReturns.Reset();
    UE_LOG(LogLDMatch, Display, TEXT("LD_REST_RETURN generation=%u final=%d protected_until_ms=%lld pending=%d"),
        MatchContext.Generation, bFinalField ? 1 : 0, ProtectedUntil, PendingPhasePlacement.Num());
    Error.Reset();
    return true;
}

bool ALDGameMode::RetryPhasePlacements(const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (PendingPhasePlacement.IsEmpty()) { return true; }
    TArray<FGuid> Pending = PendingPhasePlacement.Array();
    Pending.Sort();
    const bool bRest = MatchPhase == ELDMatchPhase::Rest;
    int64 ProtectedUntil = 0;
    if (!bRest && !LDMatchLifecycle::AddDurationMs(AuthorityNowMs, DemoContent->Get().Parameters.ProtectionMs, ProtectedUntil, Error))
    {
        return false;
    }
    for (const FGuid& Id : Pending)
    {
        const FLDLifeRecord* Life = Lives.Find(Id);
        if (!IsMemberPresent(Id) || !Life || Life->Phase != ELDLifePhase::Dead)
        {
            PendingPhasePlacement.Remove(Id);
            continue;
        }
        bool bBlocked = false;
        if (!PlacePhaseLife(Id, bRest, ProtectedUntil, Permit, bBlocked, Error)) { return false; }
    }
    Error.Reset();
    return true;
}

// ---- #198 personal training dummy ----

bool ALDGameMode::SpawnTrainingDummy(const FGuid& OwnerId, const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (TrainingDummies.Contains(OwnerId)) { Error.Reset(); return true; }
    auto* Registry = GetWorld()->GetSubsystem<ULDMapRegistry>();
    const FLDLifeRecord* OwnerLife = Lives.Find(OwnerId);
    const FLDGrowthBuild* Build = Development ? Development->FindBuild(OwnerId) : nullptr;
    const FLDMapBinding* Anchor = Registry ? Registry->Resolve(RestAnchorFor(OwnerId), Registry->Generation) : nullptr;
    FCollisionShape Capsule;
    if (ValidateWritePermit(Permit, ELDCommitPhase::RespawnsAndWorld) != ELDCommitReject::None || !Combat || !DemoContent
        || !OwnerLife || !Build || !Anchor || !Registry->GetGeometry() || !ULDMapRegistry::ContentCapsule(*DemoContent, Capsule, Error))
    {
        if (Error.IsEmpty()) { Error = TEXT("A training dummy needs its owner's rest anchor, growth record and the RespawnsAndWorld permit."); }
        return false;
    }
    // The free approved local offset at the owner's rest anchor, searched from the far end of the list (the rest life
    // takes the first free one); the same legal-ground and occupancy rules as a spawn.
    const TArray<FLDDemoWorldPoint>& Offsets = Registry->GetGeometry()->SpawnOffsetsCm;
    const double Gap = (UCharacterMovementComponent::MIN_FLOOR_DIST + UCharacterMovementComponent::MAX_FLOOR_DIST) * .5;
    for (int32 Index = Offsets.Num() - 1; Index >= 0; --Index)
    {
        FVector Delta;
        if (!Offsets[Index].ToVector(Delta, Error)) { return false; }
        FLDLegalGround Ground;
        ELDWorldQueryFailure Failure;
        if (!Registry->QueryGround(Registry->Revision(), Anchor->Position + Delta, Capsule, Anchor->Surface, Ground, Failure, Error))
        {
            if (Failure == ELDWorldQueryFailure::Blocked || Failure == ELDWorldQueryFailure::NotGrounded
                || Failure == ELDWorldQueryFailure::WrongSurface) { continue; }
            return false;
        }
        const FVector Origin = Ground.SurfaceCm + FVector(0, 0, Capsule.GetCapsuleHalfHeight() + Gap);
        if (GetWorld()->OverlapAnyTestByObjectType(Origin, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn), Capsule,
            FCollisionQueryParams(SCENE_QUERY_STAT(DemoTrainingDummy)))) { continue; }
        const FTransform Transform(FRotator::ZeroRotator, Origin);
        ALDCharacter* Body = GetWorld()->SpawnActorDeferred<ALDCharacter>(ALDCharacter::StaticClass(), Transform, nullptr, nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!Body)
        {
            Error = TEXT("The training dummy body could not be allocated.");
            return false;
        }
        Body->SetReplicates(false);
        Body->SetActorHiddenInGame(true);
        Body->SetActorTickEnabled(false);
        Body->PresentationKey = OwnerLife->PresentationSlot;
        if (!Body->ConfigureDemoMovement(*DemoContent, Error))
        {
            Body->Destroy();
            return false;
        }
        Body->FinishSpawning(Transform);
        auto* Movement = IsValid(Body) ? Body->GetCharacterMovement() : nullptr;
        if (!Movement)
        {
            Error = TEXT("The training dummy failed native construction.");
            return false;
        }
        Movement->SetComponentTickEnabled(false);
        Movement->SetMovementMode(MOVE_Walking);
        Movement->FindFloor(Body->GetActorLocation(), Movement->CurrentFloor, false, nullptr);
        Movement->SetBaseFromFloor(Movement->CurrentFloor);
        const FGuid Entity = FGuid::NewGuid();
        FLDBodyCombatState State;
        FLDCombatBodyRef Ref;
        if (!Movement->CurrentFloor.IsWalkableFloor()
            || !Combat->PrepareTrainingDummy(*Body, {ELDVictimKind::Contender, Entity, 1}, OwnerId, Build->Level, Permit, State, Error)
            || !Combat->InstallCombatBody(*Body, MoveTemp(State), Permit, Ref, Error))
        {
            if (Error.IsEmpty()) { Error = TEXT("The training dummy does not stand on its registered placement."); }
            Body->Destroy();
            return false;
        }
        FLDLifeRecord& Life = Lives.Add(Entity);
        Life.Kind = ELDVictimKind::Contender;
        Life.Phase = ELDLifePhase::Alive;
        Life.Life = 1;
        Life.Revision = 1;
        Life.Body = Ref;
        Life.Actor = Body;
        Life.PresentationSlot = OwnerLife->PresentationSlot;
        BeginLifeMotion(Life, Ref, *Body);
        Body->SetActorHiddenInGame(false);
        Body->SetActorTickEnabled(true);
        TrainingDummies.Add(OwnerId, {Entity, Ref, Body});
        TrainingDummyIds.Add(Entity);
        UE_LOG(LogLDMatch, Display, TEXT("LD_TRAINING_DUMMY_SPAWN owner=%s dummy=%s level=%u offset=%d"),
            *LDLog::Id(OwnerId), *LDLog::Id(Entity), uint32(Build->Level), Index);
        Error.Reset();
        return true;
    }
    // No free spot: the owner trains without a dummy this rest; this is reported, never silently replaced.
    UE_LOG(LogLDMatch, Warning, TEXT("LD_TRAINING_DUMMY_BLOCKED owner=%s anchor=%s"), *LDLog::Id(OwnerId), *Anchor->Id.ToString());
    Error.Reset();
    return true;
}

void ALDGameMode::RetireTrainingDummy(const FGuid& OwnerId)
{
    FLDTrainingDummy Dummy;
    if (!TrainingDummies.RemoveAndCopyValue(OwnerId, Dummy)) { return; }
    TrainingDummyIds.Remove(Dummy.Entity);
    PendingTrainingResets.Remove(OwnerId);
    if (Combat && Dummy.Body.IsValid()) { Combat->RetireTrainingEntity(Dummy.Body); }
    if (ALDCharacter* Actor = Dummy.Actor.Get()) { Actor->Destroy(); }
    Lives.Remove(Dummy.Entity);
    UE_LOG(LogLDMatch, Display, TEXT("LD_TRAINING_DUMMY_RETIRE owner=%s dummy=%s"), *LDLog::Id(OwnerId), *LDLog::Id(Dummy.Entity));
}

bool ALDGameMode::AdvanceTrainingDummyMotion(const FLDCombatBodyRef& Body, const FLDGameplayWritePermit& Permit, FString& Error)
{
    ALDCharacter* Actor = nullptr;
    if (!TrainingDummyIds.Contains(Body.Victim.EntityId) || ResolveBody(MatchContext, Body, Actor) != ELDCommitReject::None || !Actor)
    {
        Error = TEXT("Training dummy motion was issued for a stale or unregistered dummy.");
        return false;
    }
    auto* Movement = CastChecked<ULDCharacterMovementComponent>(Actor->GetCharacterMovement());
    return Movement->AdvanceAuthorityMovement(AuthorityStep, Body, FVector::ZeroVector, Permit, Error);
}

ELDTrainingResetResult ALDGameMode::SubmitTrainingReset(ALDPlayerController& Controller, uint32 Generation)
{
    const FGuid Id = ContenderId(&Controller);
    if (!Id.IsValid() || !IsMemberPresent(Id)) { return ELDTrainingResetResult::NotContender; }
    if (Generation != MatchContext.Generation) { return ELDTrainingResetResult::StaleMatch; }
    if (!IsRestPhase(MatchContext) || !RestLives.Contains(Id)) { return ELDTrainingResetResult::NotInRest; }
    if (!TrainingDummies.Contains(Id)) { return ELDTrainingResetResult::NoDummy; }
    PendingTrainingResets.Add(Id);
    return ELDTrainingResetResult::Queued;
}

bool ALDGameMode::ProcessTrainingResets(const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (PendingTrainingResets.IsEmpty()) { return true; }
    TArray<FGuid> Owners = PendingTrainingResets.Array();
    PendingTrainingResets.Reset();
    Owners.Sort();
    for (const FGuid& OwnerId : Owners)
    {
        const FLDTrainingDummy* Dummy = TrainingDummies.Find(OwnerId);
        // A request that outlived its rest space or dummy has nothing left to reset.
        if (!Dummy || !RestLives.Contains(OwnerId) || Lives.FindChecked(OwnerId).Phase != ELDLifePhase::Alive) { continue; }
        if (!Combat->ResetTraining(OwnerId, Dummy->Entity, Permit, Error)) { return false; }
        UE_LOG(LogLDMatch, Display, TEXT("LD_TRAINING_RESET owner=%s dummy=%s"), *LDLog::Id(OwnerId), *LDLog::Id(Dummy->Entity));
    }
    Error.Reset();
    return true;
}