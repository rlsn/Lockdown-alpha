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
#include "World/LDSpawnPlacement.h"
#include "World/LDCampDirector.h"
#include "Engine/World.h"
#include "Region/LDMapGeometry.h"
#include "Misc/ScopeExit.h"
#include "World/LDRestServiceNPC.h"
#include "World/LDTrainingRoomDisplay.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Evaluation/LDEvaluationSeeds.h"
#endif

// #198 rest phases (approved outer parameters and the playtest's shared-room decision).
// The pure FLDMatchTimeline schedules the rests and
// the field progress T; this file applies its transitions to lives, the world and the public read model.
//
// Every contender stands in the closed shared room at its fixed roster slot, in one Combat isolation group
// (same world, same connection, same contender identity and growth record). The frozen
// world (camp members, Boss) is suspended in Combat and takes no motion, intent or contact.
//
// Phase boundaries (approved §8): a field -> rest boundary retires every living field life (no death, kill or reward),
// ends the remaining field respawn waits, cancels every in-flight effect and control, and gives everyone a fresh rest life
// at full HP/MP with every cooldown ready. A rest -> field boundary does the same in reverse: every rest life is retired and
// the next field life stands at the remembered field foot (or within REST.ReturnSearchRadiusCm, else a normal spawn; a
// contender dead at the rest entry takes a normal spawn) with the shared LIFE.ProtectionSec. The field bodies (and a PK's
// arena bodies) are prepared jointly and stand only together; while any is blocked the entry holds (T, the flagpole and
// the PK clock wait), each attempt under a newer spawn identity, until REST.EntryReadyTimeoutSec ends it explicitly
// (approved outer parameters §1/§8).

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
    RestReady.Reset();
    LockedEntrants.Reset();
    RestReturns.Reset();
    RestLives.Reset();
    PKLives.Reset();
    PendingPhasePlacement.Reset();
    PendingPKEntry.Reset();
    bFieldReturnStarted = false;
    ReleasePhasePrepared(PreparedFieldReturn);
    UE_LOG(LogLDMatch, Display, TEXT("LD_TIMELINE_BEGIN generation=%u at_ms=%lld rest=0 build_ends_at_ms=%lld"),
        MatchContext.Generation, AuthorityNowMs, Timeline.GetStepEndsAtMs());
    Error.Reset();
    return true;
}

bool ALDGameMode::IsRestPhase(const FLDMatchContext& Context) const
{
    return IsGameplayWriteOpen(Context) && MatchPhase == ELDMatchPhase::Rest;
}

bool ALDGameMode::IsInputLocked(const FLDMatchContext& Context, const FGuid& Contender) const
{
    if (!IsRestPhase(Context)) { return false; }
    switch (Timeline.GetStep())
    {
    case ELDRestStep::Return:
    case ELDRestStep::PKReady: return true;
    case ELDRestStep::PK: return !PKLives.Contains(Contender);
    default: return false;
    }
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
    const int32 Slot = RosterOrder.IndexOfByKey(Contender);
    if (!DemoContent || Slot == INDEX_NONE || Slot >= 6) { return NAME_None; }
    return FName(*FString::Printf(TEXT("rest_spawn_%d"), Slot));
}

bool ALDGameMode::CanRestReady(const FGuid& Contender) const
{
    const FLDContenderRecord* Record = nullptr;
    for (const auto& Pair : Contenders)
    {
        if (Pair.Value.State.ContenderId == Contender) { Record = &Pair.Value; break; }
    }
    return Record && !Record->bLeft && Record->Kind == ELDContenderKind::Human && IsRestPhase(MatchContext)
        && Timeline.GetStep() == ELDRestStep::Build && AuthorityNowMs < Timeline.GetStepEndsAtMs()
        && RestLives.Contains(Contender);
}

ELDRestReadyResult ALDGameMode::SubmitRestReady(ALDPlayerController& Controller, uint32 Generation, uint8 RestIndex,
    int64 StartedAtMs, bool bReady)
{
    const FGuid Id = ContenderId(&Controller);
    if (!Id.IsValid() || !IsMemberPresent(Id)) { return ELDRestReadyResult::NotContender; }
    if (Generation != MatchContext.Generation) { return ELDRestReadyResult::StaleMatch; }
    const FLDTimelineView View = Timeline.MakeView(Generation, TimelineRevision);
    if (RestIndex != View.RestIndex || StartedAtMs != View.StepStartedAtMs) { return ELDRestReadyResult::StaleWindow; }
    if (!bReady || !CanRestReady(Id)) { return ELDRestReadyResult::NotOpen; }
    if (!RestReady.Contains(Id))
    {
        RestReady.Add(Id);
        PublishTimeline();
        SyncOwner(&Controller);
        UE_LOG(LogLDMatch, Display, TEXT("LD_REST_READY contender=%s rest=%u"), *LDLog::Id(Id), RestIndex);
    }
    return ELDRestReadyResult::Accepted;
}

ELDRestService ALDGameMode::NearbyRestService(const FGuid& Contender) const
{
    const FLDLifeRecord* Life = Lives.Find(Contender);
    const auto* Registry = GetWorld() ? GetWorld()->GetSubsystem<ULDMapRegistry>() : nullptr;
    if (!DemoContent || !Registry || !Life || Life->Phase != ELDLifePhase::Alive || !RestLives.Contains(Contender)
        || !IsRestPhase(MatchContext) || !Timeline.IsRestBuildOpen()) { return ELDRestService::None; }
    const auto& Radius = DemoContent->Get().Parameters.AirdropPickupRadiusCm;
    const double Range = double(Radius.Numerator) / double(Radius.Denominator);
    for (uint8 Index = 1; Index <= LDDemoWorld::RestServices; ++Index)
    {
        const auto* Anchor = Registry->Resolve(FName(*LDDemoWorld::RestServiceId(Index)), Registry->Generation);
        if (Anchor && FVector::DistSquared2D(Life->MotionLastFoot, Anchor->Position) <= Range * Range)
        {
            return static_cast<ELDRestService>(Index);
        }
    }
    return ELDRestService::None;
}

bool ALDGameMode::CanUseRestService(const FGuid& Contender, ELDBuildCommandKind Kind) const
{
    const ELDRestService Required = LDProgression::RestServiceFor(Kind);
    return Required == ELDRestService::None || NearbyRestService(Contender) == Required;
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

bool ALDGameMode::SignupDiskFor(const FGuid& Contender, FVector& OutCentreCm, double& OutRadiusCm) const
{
    OutCentreCm = FVector::ZeroVector;
    OutRadiusCm = 0;
    const int32 Slot = RosterOrder.IndexOfByKey(Contender);
    const auto* Registry = GetWorld() ? GetWorld()->GetSubsystem<ULDMapRegistry>() : nullptr;
    if (!DemoContent || !Registry || Slot == INDEX_NONE || Slot >= LDDemoWorld::PKSignups) { return false; }
    const FLDDemoRational& Radius = DemoContent->Get().Parameters.AirdropPickupRadiusCm;
    if (Radius.Denominator <= 0 || Radius.Numerator <= 0 || !Registry->SharedSignupCentre(OutCentreCm)) { return false; }
    OutRadiusCm = double(Radius.Numerator) / double(Radius.Denominator);
    return true;
}

bool ALDGameMode::RefreshRestSignups(FString& Error)
{
    // During the sign-up window every contender uses the same disk (XY, boundary inside).
    TSet<FGuid> Inside;
    const FLDMatchProviderHooks* PK = LDMatchProviders::Find(ELDMatchProvider::PK);
    if (IsRestPhase(MatchContext) && Timeline.IsSignupOpen(AuthorityNowMs) && PK && PK->RestStep)
    {
        for (const FGuid& Id : RosterOrder)
        {
            const FLDLifeRecord* Life = Lives.Find(Id);
            if (!IsMemberPresent(Id) || !RestLives.Contains(Id) || !Life || Life->Phase != ELDLifePhase::Alive) { continue; }
            FVector Centre;
            double Radius = 0;
            if (!SignupDiskFor(Id, Centre, Radius))
            {
                Error = FString::Printf(TEXT("Contender %s has no registered PK sign-up disk."), *LDLog::Id(Id));
                return false;
            }
            if (FVector::DistSquared2D(Life->MotionLastFoot, Centre) <= Radius * Radius) { Inside.Add(Id); }
        }
    }
    bool bChanged = Inside.Num() != RestSignups.Num();
    for (const FGuid& Id : Inside)
    {
        if (RestSignups.Contains(Id)) { continue; }
        bChanged = true;
        UE_LOG(LogLDMatch, Display, TEXT("LD_REST_SIGNUP contender=%s join=1 rest=%u at_ms=%lld signups=%d"), *LDLog::Id(Id),
            Timeline.GetRestIndex(), AuthorityNowMs, Inside.Num());
    }
    for (const FGuid& Id : RestSignups)
    {
        if (Inside.Contains(Id)) { continue; }
        UE_LOG(LogLDMatch, Display, TEXT("LD_REST_SIGNUP contender=%s join=0 rest=%u at_ms=%lld signups=%d"), *LDLog::Id(Id),
            Timeline.GetRestIndex(), AuthorityNowMs, Inside.Num());
    }
    if (bChanged)
    {
        RestSignups = MoveTemp(Inside);
        bPublicMatchDirty = true;
    }
    Error.Reset();
    return true;
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
    // Prepared field bodies live only inside this boundary: whatever the entry did not commit is released.
    ON_SCOPE_EXIT { ReleasePhasePrepared(PreparedFieldReturn); };
    bool bBodiesReady = true;
    TArray<FLDTimelineEvent> Events;
    if (IsRestPhase(MatchContext) && Timeline.GetStep() == ELDRestStep::Build && AuthorityNowMs < Timeline.GetStepEndsAtMs())
    {
        int32 Humans = 0;
        bool bAllReady = true;
        for (const auto& Pair : Contenders)
        {
            const auto& Record = Pair.Value;
            if (Record.bLeft || Record.Kind != ELDContenderKind::Human || !RosterOrder.Contains(Record.State.ContenderId)) { continue; }
            ++Humans;
            bAllReady &= RestReady.Contains(Record.State.ContenderId);
        }
        bool bChanged = false;
        if (Humans > 0 && bAllReady)
        {
            if (!Timeline.FinishBuildEarly(AuthorityNowMs, bChanged, Error)) { return false; }
            if (bChanged) { PublishTimeline(); }
        }
    }
    if (!PrepareEntry(Permit, bBodiesReady, Error)
        || !Timeline.Advance(Permit.GetBoundary().AtMs, RestSignups.Num(), false, bBodiesReady, Events, Error)
        || !ApplyTimelineEvents(Events, Permit, Error)
        || !RetryPhasePlacements(Permit, Error)
        || !RefreshRestSignups(Error)
        || (IsRestPhase(MatchContext) && !ProcessTrainingResets(Permit, Error)))
    {
        return false;
    }
    if (!Events.IsEmpty()) { PublishTimeline(); }
    Error.Reset();
    return true;
}

bool ALDGameMode::PrepareEntry(const FLDGameplayWritePermit& Permit, bool& bBodiesReady, FString& Error)
{
    bBodiesReady = true;
    if (!IsRestPhase(MatchContext) || !DemoContent) { Error.Reset(); return true; }
    const int64 AtMs = Permit.GetBoundary().AtMs;
    if (!PendingPKEntry.IsEmpty() && Timeline.GetStep() == ELDRestStep::PKReady)
    {
        bool bPlaced = false;
        if (!TryPKEntry(Permit, bPlaced, Error)) { return false; }
    }
    if (!Timeline.IsEntryDue(AtMs)) { Error.Reset(); return true; }
    const int64 TimeoutMs = DemoContent->Get().Parameters.RestEntryReadyTimeoutMs;
    const int64 HeldMs = AtMs - Timeline.GetStepEndsAtMs();
    if (Timeline.GetStep() == ELDRestStep::Return)
    {
        if (!PrepareFieldReturn(Permit, bBodiesReady, Error)) { return false; }
        if (!bBodiesReady && HeldMs >= TimeoutMs)
        {
            Error = FString::Printf(TEXT("LD_REST_RETURN_UNREADY rest=%u: the field return could not stand every contender within %lld ms of its boundary."),
                Timeline.GetRestIndex(), TimeoutMs);
            return false;
        }
        Error.Reset();
        return true;
    }
    bBodiesReady = PendingPKEntry.IsEmpty();
    if (!bBodiesReady && HeldMs >= TimeoutMs)
    {
        // The arena never became legal: this PK is NoContest and the entry no longer waits.
        if (!AbandonPKEntry(Permit, Error)) { return false; }
        bBodiesReady = true;
    }
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
    // Whoever stands in its sign-up disk at the deadline (this boundary's committed feet) enters; only present contenders
    // still standing in their rest space do, and the locked list never changes afterwards.
    if (!RefreshRestSignups(Error)) { return false; }
    LockedEntrants.Reset();
    for (const FGuid& Id : RestSignups)
    {
        if (IsMemberPresent(Id) && RestLives.Contains(Id)) { LockedEntrants.Add(Id); }
    }
    LockedEntrants.Sort();
    RestSignups.Reset();
    TArray<FLDTimelineEvent> Events;
    // The lock enters Draw or Return, never a body entry, so no body readiness is claimed here.
    if (!Timeline.Advance(AtMs, LockedEntrants.Num(), true, false, Events, Error)) { return false; }
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
                RestReady.Reset();
                LockedEntrants.Reset();
            }
            break;
        case ELDTimelineEventKind::LockBuild:
            UE_LOG(LogLDMatch, Display, TEXT("LD_REST_LOCK generation=%u rest=%u at_ms=%lld entrants=%lld pk=%s"),
                MatchContext.Generation, Event.RestIndex, Event.AtMs, Event.Value, Event.Value >= 2 ? TEXT("run") : TEXT("skipped"));
            break;
        case ELDTimelineEventKind::EnterField:
        {
            RestReady.Reset();
            const bool bFinal = Event.FieldSegment >= FLDTimelineParameters::TimedSegments;
            // Every field body was prepared jointly before the timeline entered the field; they stand together now, while
            // the phase is still Rest (the R0 return is every contender's spray-roster join).
            if (!ExitRestWorld(Permit, Error) || !CommitFieldReturn(bFinal, Permit, Error)) { return false; }
            MatchPhase = ELDMatchPhase::Development;
            if (bFinal && !CaptureReducer.ScheduleOpen(Event.AtMs, Error)) { return false; }
            LockedEntrants.Reset();
            UE_LOG(LogLDMatch, Display, TEXT("LD_FIELD_ENTER generation=%u segment=%u at_ms=%lld field_t_ms=%lld final=%d"),
                MatchContext.Generation, Event.FieldSegment, Event.AtMs, Event.Value, bFinal ? 1 : 0);
#if WITH_DEV_AUTOMATION_TESTS
            if (Event.FieldSegment == 0 && LDEvaluationSeeds::Armed())
            {
                for (const FGuid& Contender : LockedAISlots)
                {
                    FLDCombatBodyRef Body;
                    FVector Foot;
                    if (ReadLivingBody(Contender, Body, Foot))
                    {
                        UE_LOG(LogLDTest, Display, TEXT("LD_EVAL_FIELD_SPAWN t=%lld generation=%u contender=%s life=%u pos=%s"),
                            Event.AtMs, MatchContext.Generation, *LDLog::Id(Contender), Body.Victim.Life, *LDLog::Cm(Foot));
                    }
                }
            }
#endif
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
    // #198 outer §5: the segment's unclaimed camp markers expire with it.
    if (!EndFieldMarkersForRest(Permit, Error)) { return false; }
    if (!Camps->FreezeForRest(Permit, Error)) { return false; }
    ClearRestServiceNPCs();
    auto* Registry = GetWorld()->GetSubsystem<ULDMapRegistry>();
    for (uint8 Index = 1; Index <= LDDemoWorld::RestServices; ++Index)
    {
        const auto* Anchor = Registry ? Registry->Resolve(FName(*LDDemoWorld::RestServiceId(Index)), Registry->Generation) : nullptr;
        if (!Anchor)
        {
            Error = TEXT("The shared rest room is missing a service NPC anchor.");
            return false;
        }
        auto* NPC = GetWorld()->SpawnActor<ALDRestServiceNPC>(Anchor->Position + FVector(0, 0, 80), FRotator::ZeroRotator);
        if (!NPC) { Error = TEXT("The shared rest service NPC could not spawn."); return false; }
        NPC->SetService(static_cast<ELDRestService>(Index));
        RestServiceNPCs.Add(NPC);
    }
    ALDTrainingRoomDisplay* Display = GetWorld()->SpawnActor<ALDTrainingRoomDisplay>();
    if (!Display)
    {
        Error = TEXT("The shared training area boundary could not spawn.");
        return false;
    }
    TrainingRoomDisplay = Display;
    return SpawnSharedTrainingDummies(Permit, Error);
}

void ALDGameMode::ClearRestServiceNPCs()
{
    for (const auto& NPC : RestServiceNPCs) { if (NPC.IsValid()) { NPC->Destroy(); } }
    RestServiceNPCs.Reset();
    if (TrainingRoomDisplay.IsValid()) { TrainingRoomDisplay->Destroy(); }
    TrainingRoomDisplay.Reset();
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
    ClearRestServiceNPCs();
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
    // #198 session D: a PK life (like a rest life) never joined the spray roster.
    if (!RestLives.Contains(Contender) && !PKLives.Contains(Contender))
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
    PKLives.Remove(Contender);
    if (Controller) { SyncOwner(Controller); }
    NotifyLife(Body, ELDLifeChange::PhaseRetired, Life->Revision, AuthorityNowMs);
    Error.Reset();
    return true;
}

bool ALDGameMode::PreparePhaseBody(const FLDPhaseTarget& Target, const FLDGameplayWritePermit& Permit, FLDPhasePrepared& Out,
    bool& bBlocked, FString& BlockedReason, FString& Error)
{
    bBlocked = false;
    BlockedReason.Reset();
    auto* Registry = GetWorld()->GetSubsystem<ULDMapRegistry>();
    const FLDLifeRecord* Life = Lives.Find(Target.Contender);
    if (!Registry || !Combat || !Life || Life->Phase != ELDLifePhase::Dead || Life->Life == MAX_uint32)
    {
        Error = TEXT("A phase placement needs a bodiless contender, its next life and the registered world.");
        return false;
    }
    if (!Life->NativeMotion.IsEmpty())
    {
        // The retired body's last motion is consumed next step; the placement is tried again then.
        bBlocked = true;
        BlockedReason = TEXT("the retired body's last motion is not consumed yet");
        Error.Reset();
        return true;
    }
    const uint32 NewLife = Life->Life + 1;
    Out.Placed = {};
    Out.CombatState = MakeShared<FLDBodyCombatState>();
    if (!PlaceContenderBody(Target.Contender, NewLife, Permit, Out.Placed, Error, Target.Where)
        || !Combat->PrepareContenderBody(*Out.Placed.Body.Get(), {ELDVictimKind::Contender, Target.Contender, NewLife}, Permit,
            *Out.CombatState, Error)
        || !Registry->ValidateSpawnReservation(Out.Placed.Reservation, Permit, Out.Placed.Body.Get(), Error))
    {
        ReleasePlacedBody(Out.Placed);
        Out.CombatState.Reset();
        if (!Error.StartsWith(TEXT("SpawnBlocked"))) { return false; }
        bBlocked = true;
        BlockedReason = MoveTemp(Error);
        Error.Reset();
        return true;
    }
    Error.Reset();
    return true;
}

bool ALDGameMode::CommitPhaseBody(FLDPhasePrepared& Prepared, int64 ProtectedUntilMs, const FLDGameplayWritePermit& Permit,
    FString& Error)
{
    if (!Prepared.CombatState.IsValid())
    {
        Error = TEXT("A phase body was committed without its prepared combat state.");
        FaultAuthority(Error);
        return false;
    }
    if (!CommitPlacedLife(Prepared.Placed, MoveTemp(*Prepared.CombatState), ProtectedUntilMs, Permit, Error)
        || !ActivatePlacedBody(Prepared.Placed, Permit, Error))
    {
        FaultAuthority(Error);
        return false;
    }
    Prepared.CombatState.Reset();
    // The body now belongs to its committed life: releasing this entry later must not touch it.
    Prepared.Placed.Body.Reset();
    PendingPhasePlacement.Remove(Prepared.Placed.Contender);
    Error.Reset();
    return true;
}

void ALDGameMode::ReleasePhasePrepared(TArray<FLDPhasePrepared>& Prepared)
{
    for (FLDPhasePrepared& Entry : Prepared) { ReleasePlacedBody(Entry.Placed); }
    Prepared.Reset();
}

bool ALDGameMode::WithdrawPhaseRequest(const FGuid& Contender, const FLDGameplayWritePermit& Permit, FString& Error)
{
    auto* Registry = GetWorld()->GetSubsystem<ULDMapRegistry>();
    FLDLifeRecord* Life = Lives.Find(Contender);
    if (!Registry || !Life || Life->Life == MAX_uint32)
    {
        Error = TEXT("A phase spawn request is withdrawn only for a registered contender of the registered world.");
        return false;
    }
    // No request was issued for the next life yet.
    if (Life->SpawnForLife != Life->Life + 1 || Life->SpawnSequence == 0)
    {
        Error.Reset();
        return true;
    }
    if (!Registry->WithdrawSpawnRequest({MatchContext, Contender, Life->SpawnForLife, Life->SpawnSequence}, Permit, Error)) { return false; }
    // The next attempt for this life issues a newer sequence (PlaceContenderBody).
    Life->SpawnForLife = 0;
    Error.Reset();
    return true;
}

bool ALDGameMode::PrepareJointPhase(const TArray<FLDPhaseTarget>& Targets, const FLDGameplayWritePermit& Permit,
    TArray<FLDPhasePrepared>& Out, bool& bBlocked, FString& BlockedReason, FString& Error)
{
    ReleasePhasePrepared(Out);
    bBlocked = false;
    BlockedReason.Reset();
    for (const FLDPhaseTarget& Target : Targets)
    {
        FLDPhasePrepared Entry;
        FString Reason;
        if (!PreparePhaseBody(Target, Permit, Entry, bBlocked, Reason, Error))
        {
            ReleasePhasePrepared(Out);
            return false;
        }
        if (bBlocked)
        {
            BlockedReason = FString::Printf(TEXT("contender=%s %s"), *LDLog::Id(Target.Contender), *Reason);
            break;
        }
        Out.Add(MoveTemp(Entry));
    }
    if (!bBlocked)
    {
        Error.Reset();
        return true;
    }
    // Nobody stands until everybody can: every prepared body is released, and each target tries again later under a newer
    // spawn identity (another anchor or the same one; a withdrawn identity never replays).
    ReleasePhasePrepared(Out);
    for (const FLDPhaseTarget& Target : Targets)
    {
        if (!WithdrawPhaseRequest(Target.Contender, Permit, Error)) { return false; }
    }
    Error.Reset();
    return true;
}

bool ALDGameMode::PlaceRestLife(const FGuid& Contender, const FLDGameplayWritePermit& Permit, bool& bBlocked, FString& Error)
{
    bBlocked = false;
    FLDPhaseTarget Target;
    Target.Contender = Contender;
    Target.Where.Kind = FLDPlacementTarget::EKind::Anchor;
    Target.Where.Anchor = RestAnchorFor(Contender);
    if (Target.Where.Anchor.IsNone())
    {
        Error = TEXT("A contender has no shared-room rest anchor.");
        return false;
    }
    FLDPhasePrepared Prepared;
    FString Reason;
    if (!PreparePhaseBody(Target, Permit, Prepared, bBlocked, Reason, Error)) { return false; }
    if (!bBlocked && !TrainingDummies.Contains(Contender))
    {
        FLDLegalGround Ground;
        if (!PrepareTrainingGround(Contender, Ground, Error))
        {
            ReleasePlacedBody(Prepared.Placed);
            Prepared.CombatState.Reset();
            if (!Error.StartsWith(TEXT("SpawnBlocked"))) { return false; }
            bBlocked = true;
            Reason = MoveTemp(Error);
        }
    }
    if (bBlocked)
    {
        if (!PendingPhasePlacement.Contains(Contender))
        {
            UE_LOG(LogLDMatch, Warning, TEXT("LD_PHASE_PLACEMENT_BLOCKED contender=%s rest=1 %s"), *LDLog::Id(Contender), *Reason);
        }
        PendingPhasePlacement.Add(Contender);
        Error.Reset();
        return true;
    }
    if (!CommitPhaseBody(Prepared, 0, Permit, Error)) { return false; }
    // Copies of the committed life: the training dummy's own record is added to Lives below, which may move every record.
    const FLDLifeRecord& Committed = Lives.FindChecked(Contender);
    const FLDCombatBodyRef Body = Committed.Body;
    const uint64 Revision = Committed.Revision;
    const uint32 NewLife = Committed.Life;
    const FName Anchor = Committed.SpawnAnchor;
    RestLives.Add(Contender);
    if (!Combat->SetRestIsolation(Body, true, Permit, Error) || !SpawnTrainingDummy(Contender, Permit, Error)) { return false; }
    NotifyLife(Body, ELDLifeChange::Respawned, Revision, AuthorityNowMs);
    UE_LOG(LogLDMatch, Display, TEXT("LD_PHASE_PLACED contender=%s life=%u rest=1 anchor=%s protected_until_ms=0"),
        *LDLog::Id(Contender), NewLife, *Anchor.ToString());
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
        FLDRestReturn& Return = RestReturns.FindOrAdd(Id);
        Return = {};
        PendingPhasePlacement.Remove(Id);
        FLDLifeRecord& Life = Lives.FindChecked(Id);
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
        if (!PlaceRestLife(Id, Permit, bBlocked, Error)) { return false; }
    }
    Error.Reset();
    return true;
}

bool ALDGameMode::PrepareFieldReturn(const FLDGameplayWritePermit& Permit, bool& bReady, FString& Error)
{
    bReady = false;
    if (!DemoContent)
    {
        Error = TEXT("The field return needs the approved content.");
        return false;
    }
    const FLDDemoRational& Radius = DemoContent->Get().Parameters.RestReturnSearchRadiusCm;
    if (Radius.Denominator <= 0 || Radius.Numerator < 0)
    {
        Error = TEXT("The approved return search radius is invalid.");
        return false;
    }
    TArray<FGuid> Order = RosterOrder;
    Order.RemoveAll([this](const FGuid& Id) { return !IsMemberPresent(Id); });
    Order.Sort();
    if (!bFieldReturnStarted)
    {
        TArray<FGuid> DummySlots;
        TrainingDummies.GetKeys(DummySlots);
        for (const FGuid& Slot : DummySlots) { RetireTrainingDummy(Slot); }
        RetireSharedTrainingDummies();
        // Every rest life leaves first (no death, nothing of the rest reaches the field); the field bodies then stand
        // only together.
        for (const FGuid& Id : Order)
        {
            if (Lives.FindChecked(Id).Phase == ELDLifePhase::Alive && !RetireLifeForPhase(Id, Permit, Error)) { return false; }
        }
        PendingPhasePlacement.Reset();
        bFieldReturnStarted = true;
    }
    // Survivors claim their old spots first, so a normal draw never takes a returning foot.
    Order.StableSort([this](const FGuid& A, const FGuid& B)
    {
        const FLDRestReturn* RA = RestReturns.Find(A);
        const FLDRestReturn* RB = RestReturns.Find(B);
        return (RA && RA->bAliveAtRest) && !(RB && RB->bAliveAtRest);
    });
    TArray<FLDPhaseTarget> Targets;
    for (const FGuid& Id : Order)
    {
        FLDPhaseTarget& Target = Targets.AddDefaulted_GetRef();
        Target.Contender = Id;
        // Alive at the rest entry: the old foot or a free spot within the approved radius, else a normal spawn; dead at
        // the rest entry: a normal spawn.
        if (const FLDRestReturn* Return = RestReturns.Find(Id); Return && Return->bAliveAtRest)
        {
            Target.Where.Kind = FLDPlacementTarget::EKind::Near;
            Target.Where.FootCm = Return->FieldFootCm;
            Target.Where.Surface = Return->FieldSurface;
            Target.Where.RadiusCm = double(Radius.Numerator) / double(Radius.Denominator);
        }
    }
    bool bBlocked = false;
    FString Reason;
    if (!PrepareJointPhase(Targets, Permit, PreparedFieldReturn, bBlocked, Reason, Error)) { return false; }
    if (bBlocked && !Timeline.IsEntryHeld())
    {
        UE_LOG(LogLDMatch, Warning, TEXT("LD_REST_RETURN_HELD generation=%u rest=%u at_ms=%lld %s"), MatchContext.Generation,
            Timeline.GetRestIndex(), Permit.GetBoundary().AtMs, *Reason);
    }
    bReady = !bBlocked;
    Error.Reset();
    return true;
}

bool ALDGameMode::CommitFieldReturn(bool bFinalField, const FLDGameplayWritePermit& Permit, FString& Error)
{
    int64 ProtectedUntil = 0;
    if (!bFieldReturnStarted || !DemoContent
        || !LDMatchLifecycle::AddDurationMs(AuthorityNowMs, DemoContent->Get().Parameters.ProtectionMs, ProtectedUntil, Error))
    {
        if (Error.IsEmpty()) { Error = TEXT("The field entry has no jointly prepared return."); }
        return false;
    }
    // Every body was verified free together; they all stand now, with the shared protection, and the field starts after.
    for (FLDPhasePrepared& Prepared : PreparedFieldReturn)
    {
        if (!CommitPhaseBody(Prepared, ProtectedUntil, Permit, Error)) { return false; }
    }
    for (const FLDPhasePrepared& Prepared : PreparedFieldReturn)
    {
        const FGuid Contender = Prepared.Placed.Contender;
        const FLDLifeRecord& Committed = Lives.FindChecked(Contender);
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
        UE_LOG(LogLDMatch, Display, TEXT("LD_PHASE_PLACED contender=%s life=%u rest=0 anchor=%s protected_until_ms=%lld"),
            *LDLog::Id(Contender), Committed.Life, *Committed.SpawnAnchor.ToString(), ProtectedUntil);
        if (!Combat->ReadyAllForPhase(Contender, Permit, Error)) { return false; }
    }
#if WITH_DEV_AUTOMATION_TESTS
    if (DevFieldReturnCommitted) { DevFieldReturnCommitted(*this); }
#endif
    const int32 Placed = PreparedFieldReturn.Num();
    PreparedFieldReturn.Reset();
    RestReturns.Reset();
    bFieldReturnStarted = false;
    UE_LOG(LogLDMatch, Display, TEXT("LD_REST_RETURN generation=%u final=%d protected_until_ms=%lld placed=%d"),
        MatchContext.Generation, bFinalField ? 1 : 0, ProtectedUntil, Placed);
    Error.Reset();
    return true;
}

bool ALDGameMode::RetryPhasePlacements(const FLDGameplayWritePermit& Permit, FString& Error)
{
    // Only rest lives wait here; the field return and a PK entry stand their bodies jointly.
    if (PendingPhasePlacement.IsEmpty() || !IsRestPhase(MatchContext) || bFieldReturnStarted) { return true; }
    TArray<FGuid> Pending = PendingPhasePlacement.Array();
    Pending.Sort();
    for (const FGuid& Id : Pending)
    {
        const FLDLifeRecord* Life = Lives.Find(Id);
        if (!IsMemberPresent(Id) || !Life || Life->Phase != ELDLifePhase::Dead)
        {
            PendingPhasePlacement.Remove(Id);
            continue;
        }
        bool bBlocked = false;
        if (!PlaceRestLife(Id, Permit, bBlocked, Error)) { return false; }
    }
    Error.Reset();
    return true;
}

// ---- #198 shared training dummies ----

bool ALDGameMode::PrepareTrainingGround(const FGuid& OwnerId, FLDLegalGround& Out, FString& Error) const
{
    const FLDLifeRecord* Life = Lives.Find(OwnerId);
    return PrepareTrainingGroundAt(Life ? FName(*LDDemoWorld::TrainingDummyId(Life->PresentationSlot)) : NAME_None, Out, Error);
}

bool ALDGameMode::PrepareTrainingGroundAt(FName AnchorId, FLDLegalGround& Out, FString& Error) const
{
    auto* Registry = GetWorld()->GetSubsystem<ULDMapRegistry>();
    const FLDMapBinding* Anchor = Registry ? Registry->Resolve(AnchorId, Registry->Generation) : nullptr;
    FCollisionShape Capsule;
    if (!DemoContent || !Anchor || !Registry->GetGeometry()
        || !ULDMapRegistry::ContentCapsule(*DemoContent, Capsule, Error))
    {
        if (Error.IsEmpty()) { Error = TEXT("Training placement needs a registered slot and capsule."); }
        return false;
    }
    for (const auto& Value : Registry->GetGeometry()->SpawnOffsetsCm)
    {
        FVector Offset;
        if (!Value.ToVector(Offset, Error)) { return false; }
        FLDLegalGround Candidate;
        ELDWorldQueryFailure Failure;
        if (!Registry->QueryGround(Registry->Revision(), Anchor->Position + Offset, Capsule, Anchor->Surface,
            Candidate, Failure, Error)) { continue; }
        const FVector Origin = LDSpawnPlacement::StandingOrigin(Candidate);
        if (LDSpawnPlacement::OverlapsStatic(*GetWorld(), Origin, Capsule)
            || GetWorld()->OverlapAnyTestByObjectType(Origin, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
                Capsule, FCollisionQueryParams(SCENE_QUERY_STAT(TrainingPlacement))))
        { continue; }
        Out = MoveTemp(Candidate);
        Error.Reset();
        return true;
    }
    Error = TEXT("SpawnBlocked: all legal offsets of the approved training slot are temporarily occupied.");
    return false;
}

bool ALDGameMode::SpawnTrainingDummy(const FGuid& OwnerId, const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (TrainingDummies.Contains(OwnerId)) { Error.Reset(); return true; }
    auto* Registry = GetWorld()->GetSubsystem<ULDMapRegistry>();
    const FLDGrowthBuild* Build = Development ? Development->FindBuild(OwnerId) : nullptr;
    // Only a copy of the owner's slot is kept: the dummy's own life record is added to Lives below, which may move every
    // record of the map.
    bool bOwnerLife = false;
    uint8 OwnerSlot = MAX_uint8;
    if (const FLDLifeRecord* OwnerLife = Lives.Find(OwnerId))
    {
        bOwnerLife = true;
        OwnerSlot = OwnerLife->PresentationSlot;
    }
    const FLDMapBinding* Anchor = Registry && OwnerSlot < LDDemoWorld::TrainingDummies
        ? Registry->Resolve(FName(*LDDemoWorld::TrainingDummyId(OwnerSlot)), Registry->Generation) : nullptr;
    FCollisionShape Capsule;
    if (ValidateWritePermit(Permit, ELDCommitPhase::RespawnsAndWorld) != ELDCommitReject::None || !Combat || !DemoContent
        || !bOwnerLife || !Build || !Anchor || !Registry->GetGeometry() || !ULDMapRegistry::ContentCapsule(*DemoContent, Capsule, Error))
    {
        if (Error.IsEmpty()) { Error = TEXT("A training dummy needs its owner's public training slot, growth record and the RespawnsAndWorld permit."); }
        return false;
    }
    FLDTrainingDummy Dummy;
    if (!SpawnTrainingBody(Anchor->Id, Build->Level, OwnerSlot, Permit, Dummy, Error)) { return false; }
    TrainingDummies.Add(OwnerId, Dummy);
    UE_LOG(LogLDMatch, Display, TEXT("LD_TRAINING_DUMMY_SPAWN owner=%s dummy=%s level=%u slot=%u"),
        *LDLog::Id(OwnerId), *LDLog::Id(Dummy.Entity), uint32(Build->Level), uint32(OwnerSlot));
    Error.Reset();
    return true;
}

bool ALDGameMode::SpawnTrainingBody(FName AnchorId, uint8 Level, uint8 PresentationSlot,
    const FLDGameplayWritePermit& Permit, FLDTrainingDummy& Out, FString& Error)
{
    FLDLegalGround Ground;
    if (!PrepareTrainingGroundAt(AnchorId, Ground, Error)) { return false; }
    const FVector Origin = LDSpawnPlacement::StandingOrigin(Ground);
    const FTransform Transform(FRotator::ZeroRotator, Origin);
    ALDCharacter* Body = GetWorld()->SpawnActorDeferred<ALDCharacter>(ALDCharacter::StaticClass(), Transform, nullptr, nullptr,
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!Body)
    {
        Error = TEXT("The training dummy body could not be allocated.");
        return false;
    }
    Body->SetReplicates(false);
    // The authority body is presented through each authorized observation proxy, including on the listen server.
    Body->SetActorHiddenInGame(true);
    Body->SetActorTickEnabled(false);
    Body->PresentationKey = PresentationSlot;
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
        || Movement->CurrentFloor.HitResult.GetComponent() != Ground.Floor.Get()
        || !Body->GetActorLocation().Equals(Origin, .000001)
        || !Combat->PrepareTrainingDummy(*Body, {ELDVictimKind::Contender, Entity, 1}, Level, Permit, State, Error)
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
    Life.PresentationSlot = PresentationSlot;
    BeginLifeMotion(Life, Ref, *Body);
    Body->SetActorTickEnabled(true);
    Out = {Entity, Ref, Body};
    TrainingDummyIds.Add(Entity);
    Error.Reset();
    return true;
}

void ALDGameMode::RetireTrainingDummy(const FGuid& OwnerId)
{
    FLDTrainingDummy Dummy;
    if (!TrainingDummies.RemoveAndCopyValue(OwnerId, Dummy)) { return; }
    PendingTrainingResets.Remove(OwnerId);
    RetireTrainingBody(Dummy);
    UE_LOG(LogLDMatch, Display, TEXT("LD_TRAINING_DUMMY_RETIRE owner=%s dummy=%s"), *LDLog::Id(OwnerId), *LDLog::Id(Dummy.Entity));
}

void ALDGameMode::RetireTrainingBody(const FLDTrainingDummy& Dummy)
{
    TrainingDummyIds.Remove(Dummy.Entity);
    if (Combat && Dummy.Body.IsValid()) { Combat->RetireTrainingEntity(Dummy.Body); }
    if (ALDCharacter* Actor = Dummy.Actor.Get()) { Actor->Destroy(); }
    Lives.Remove(Dummy.Entity);
}

bool ALDGameMode::SpawnSharedTrainingDummies(const FLDGameplayWritePermit& Permit, FString& Error)
{
    auto* Registry = GetWorld()->GetSubsystem<ULDMapRegistry>();
    uint8 Level = 0;
    if (!Registry || !Registry->GetGeometry() || !DemoContent || !Combat
        || ValidateWritePermit(Permit, ELDCommitPhase::RespawnsAndWorld) != ELDCommitReject::None
        || LDProgression::LevelForXP(DemoContent->Get().Progression, DemoContent->Get().Progression.XPStart, Level) != ELDGrowthReason::None)
    { Error = TEXT("Shared training targets need current world bindings, starting chassis and world-write permit."); return false; }
    for (const auto& Anchor : Registry->GetGeometry()->Anchors)
    {
        if (Anchor.Kind != ELDDemoWorldAnchorKind::TrainingDummy || !Anchor.Id.StartsWith(TEXT("training_shared_"))) { continue; }
        const FName Id(*Anchor.Id);
        if (SharedTrainingDummies.Contains(Id)) { continue; }
        FLDTrainingDummy Dummy;
        if (!SpawnTrainingBody(Id, Level, 0, Permit, Dummy, Error)) { Error = Anchor.Id + TEXT(": ") + Error; return false; }
        SharedTrainingDummies.Add(Id, Dummy);
        UE_LOG(LogLDMatch, Display, TEXT("LD_SHARED_TRAINING_SPAWN anchor=%s dummy=%s level=%u"),
            *Anchor.Id, *LDLog::Id(Dummy.Entity), uint32(Level));
    }
    Error.Reset();
    return true;
}

void ALDGameMode::RetireSharedTrainingDummies()
{
    for (const auto& Pair : SharedTrainingDummies) { RetireTrainingBody(Pair.Value); }
    SharedTrainingDummies.Reset();
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

ELDTrainingResetResult ALDGameMode::SubmitTrainingReset(ALDPlayerController& Controller, uint32 Generation, uint32 LifeNumber,
    uint8 RestIndex, int64 StartedAtMs)
{
    const FGuid Id = ContenderId(&Controller);
    if (!Id.IsValid() || !IsMemberPresent(Id)) { return ELDTrainingResetResult::NotContender; }
    if (Generation != MatchContext.Generation) { return ELDTrainingResetResult::StaleMatch; }
    const FLDTimelineView View = Timeline.MakeView(Generation, TimelineRevision);
    const FLDLifeRecord* Life = Lives.Find(Id);
    if (!IsRestPhase(MatchContext) || !Timeline.IsRestBuildOpen() || !RestLives.Contains(Id) || !Life
        || Life->Life != LifeNumber || Life->Phase != ELDLifePhase::Alive || View.RestIndex != RestIndex
        || View.StepStartedAtMs != StartedAtMs) { return ELDTrainingResetResult::NotInRest; }
    if (!TrainingDummies.Contains(Id)) { return ELDTrainingResetResult::NoDummy; }
    PendingTrainingResets.Add(Id, {LifeNumber, RestIndex, StartedAtMs});
    return ELDTrainingResetResult::Queued;
}

bool ALDGameMode::ProcessTrainingResets(const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (PendingTrainingResets.IsEmpty()) { return true; }
    TMap<FGuid, FLDTrainingResetWindow> Pending = MoveTemp(PendingTrainingResets);
    PendingTrainingResets.Reset();
    TArray<FGuid> Owners;
    Pending.GetKeys(Owners);
    Owners.Sort();
    for (const FGuid& OwnerId : Owners)
    {
        const FLDTrainingDummy* Dummy = TrainingDummies.Find(OwnerId);
        // A request that outlived its rest space or dummy has nothing left to reset.
        const auto& Window = Pending.FindChecked(OwnerId);
        const FLDTimelineView View = Timeline.MakeView(MatchContext.Generation, TimelineRevision);
        const FLDLifeRecord* Life = Lives.Find(OwnerId);
        if (!Dummy || !RestLives.Contains(OwnerId) || !Life || Life->Phase != ELDLifePhase::Alive
            || Life->Life != Window.Life || !Timeline.IsRestBuildOpen() || View.RestIndex != Window.RestIndex
            || View.StepStartedAtMs != Window.StartedAtMs)
        {
            UE_LOG(LogLDMatch, Display, TEXT("LD_TRAINING_RESET_EXPIRED contender=%s"), *LDLog::Id(OwnerId));
            continue;
        }
        if (!Combat->ResetTraining(OwnerId, Dummy->Entity, Permit, Error)) { return false; }
        UE_LOG(LogLDMatch, Display, TEXT("LD_TRAINING_RESET owner=%s dummy=%s"), *LDLog::Id(OwnerId), *LDLog::Id(Dummy->Entity));
    }
    Error.Reset();
    return true;
}
