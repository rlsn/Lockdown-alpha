#include "Framework/LDGameMode.h"
#include "Core/LDLog.h"
#include "World/LDCampDirector.h"
#include "PK/LDPKDirector.h"
#include "Framework/LDPlayerController.h"
#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Development/LDDemoContentCatalog.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Region/LDMapRegistry.h"
#include "Engine/World.h"

// Capture/endgame module: the unique spray zone and graffiti guard, the locked result and the
// public projections. The authority driver calls Capture -> Close -> Publish after Transactions.

int64 ALDGameMode::NextCaptureDueMs() const
{
    if (!IsGameplayWriteOpen(MatchContext)) { return MAX_int64; }
    int64 Due = CaptureReducer.NextDueMs();
    for (const FLDPrivatePole& Pole : PrivatePoles) { Due = FMath::Min(Due, Pole.Reducer.NextDueMs()); }
    return Due;
}

bool ALDGameMode::CaptureCompletesInsideStep()
{
    // Only between Motion and every later phase: afterwards the boundary instant already has side effects.
    CaptureInsideSource = INDEX_NONE;
    bCaptureCompletesInsideStep = false;
    if (!bAuthorityStepOpen || bSettlementStep || ActiveWritePermit || !bAuthorityPhaseExecuted
        || LastAuthorityPhase != ELDCommitPhase::Motion || !IsGameplayWriteOpen(MatchContext) || !CaptureReducer.IsActive())
    { return false; }
    const int64 AtMs = AuthorityBoundary.AtMs;
    int64 Earliest = CaptureReducer.CompletionInsideMs(AtMs);
    for (int32 Index = 0; Index < PrivatePoles.Num(); ++Index)
    {
        const int64 Due = PrivatePoles[Index].Reducer.CompletionInsideMs(AtMs);
        if (Due < Earliest)
        {
            Earliest = Due;
            CaptureInsideSource = Index;
        }
    }
    bCaptureCompletesInsideStep = Earliest < AtMs;
    return bCaptureCompletesInsideStep;
}

bool ALDGameMode::EnqueueCaptureFact(const FLDCaptureEvent& Fact, const FVector* InitialFootCm,
    const FLDNativeSupportSample* InitialSupport, FString& Error)
{
    const bool bInitial = InitialFootCm && InitialSupport;
    FLDCaptureEvent Main = Fact;
    if (bInitial) { Main.bInside = CaptureCircle.Contains(*InitialFootCm, *InitialSupport); }
    if (!CaptureReducer.Enqueue(Main, Error)) { return false; }
    for (FLDPrivatePole& Pole : PrivatePoles)
    {
        FLDCaptureEvent Private = Fact;
        Private.bInside = bInitial && Pole.Circle.Contains(*InitialFootCm, *InitialSupport);
        if (!Pole.Reducer.Enqueue(Private, Error)) { return false; }
        const FGuid& Id = Fact.Body.Victim.EntityId;
        if (Fact.Kind == ELDCaptureEventKind::Joined || Fact.Kind == ELDCaptureEventKind::Respawned) { Pole.Inside.Add(Id, Private.bInside); }
        else if (Fact.Kind == ELDCaptureEventKind::Died || Fact.Kind == ELDCaptureEventKind::Left) { Pole.Inside.Add(Id, false); }
    }
    Error.Reset();
    return true;
}

bool ALDGameMode::RejudgeCapturePresence(const FGuid& Contender, const FLDCombatBodyRef& Body, const FVector& FootCm,
    const FLDNativeSupportSample& Support, const FLDCaptureEventTime& Time, ELDCommitPhase Phase, FString& Error)
{
    FLDCaptureEvent Fact;
    Fact.Kind = ELDCaptureEventKind::Presence;
    Fact.Body = Body;
    Fact.Phase = Phase;
    Fact.Time = Time;
    if (FLDCaptureTrack* Track = CaptureTracks.Find(Contender))
    {
        Fact.bInside = CaptureCircle.Contains(FootCm, Support);
        if (Track->bInside != Fact.bInside && !CaptureReducer.Enqueue(Fact, Error)) { return false; }
        Track->bInside = Fact.bInside;
        Track->CircleSerial = CaptureCircleSerial;
    }
    for (FLDPrivatePole& Pole : PrivatePoles)
    {
        bool* Inside = Pole.Inside.Find(Contender);
        if (!Inside) { continue; }
        Fact.bInside = Pole.Circle.Contains(FootCm, Support);
        if (*Inside != Fact.bInside && !Pole.Reducer.Enqueue(Fact, Error)) { return false; }
        *Inside = Fact.bInside;
    }
    Error.Reset();
    return true;
}

bool ALDGameMode::InitializeCaptureMatch(FString& Error)
{
    auto* Registry = GetWorld()->GetSubsystem<ULDMapRegistry>();
    if (!DemoContent || !Registry)
    {
        Error = TEXT("Capture initialization requires the approved content and the Demo world registry.");
        return false;
    }
    if (!LDCaptureCircle::Resolve(*Registry, *DemoContent, CaptureCircle, Error)) { return false; }
    // #198: the match starts with the opening rest R0; the flagpole opens only when the final field begins, an instant
    // the timeline schedules (FLDCaptureReducer::ScheduleOpen), so the reducer starts unscheduled.
    MatchStartedAtMs = AuthorityNowMs;
    if (!FLDShowdownParameters::FromContent(DemoContent->Get().Parameters, ShowdownRules, Error) || !InitializeTimeline(Error)
        || !CaptureReducer.Begin(MAX_int64, ShowdownRules.SprayMs, ShowdownRules.GuardMs, Error))
    {
        if (Error.IsEmpty()) { Error = TEXT("The approved timeline, spray or guard duration is invalid."); }
        MatchStartedAtMs = 0;
        Timeline.Reset();
        CaptureCircle = {};
        return false;
    }
    CaptureTracks.Reset();
    PrivatePoles.Reset();
    ShowdownMods.Reset();
    CageSprays.Reset();
    CageUntilMs = 0;
    CageDurationMs = 0;
    DoubleYellowRadiusCm = 0;
    CaptureBaseRadiusCm = CaptureCircle.RadiusCm;
    CaptureAnchorCm = CaptureCircle.CenterCm;
    Showdown = {};
    ShowdownRespawns.Reset();
    ShowdownBurn.Reset();
    bShowdownClosePending = false;
    PendingCapture.Reset();
    CountedDeaths.Reset();
    KillNotices.Reset();
    NextKillSerial = 0;
    ResultView = {};
    bResultLocked = false;
    bResultPublished = false;
    bKillFeedDirty = true;
    bPublicMatchDirty = true;
    Error.Reset();
    return true;
}

FLDContenderRecord* ALDGameMode::FindContender(const FGuid& ContenderId)
{
    for (auto& Pair : Contenders)
    {
        if (Pair.Value.Session == ContenderId) { return &Pair.Value; }
    }
    return nullptr;
}

ELDCommitReject ALDGameMode::PublishCaptureMotion(const FLDGameplayWritePermit& Permit, const FLDNativeMotionBatch& Batch)
{
    const auto Permission = ValidateWritePermit(Permit, ELDCommitPhase::Motion);
    if (Permission != ELDCommitReject::None) { return Permission; }
    if (!CaptureReducer.IsActive() || !CaptureCircle.IsValid()) { return ELDCommitReject::WrongPhase; }
    auto* Track = CaptureTracks.Find(Batch.Body.Victim.EntityId);
    if (!Track || Track->Body != Batch.Body || !CaptureReducer.IsCommittedAliveBody(Batch.Body))
    { return ELDCommitReject::UnknownBody; }
    if (!(Batch.Step == AuthorityStep) || Track->LastStep == Batch.Step.StepSerial) { return ELDCommitReject::StaleStep; }
    // Buckets stay inside (last closed boundary, this boundary]; the step end is exact.
    const int64 AtMs = Permit.GetBoundary().AtMs;
    const int64 FirstOpenMs = FMath::Max<int64>(CaptureReducer.GetClosedThroughMs() + 1, 0);
    FString Error;
    // One zone's crossings of this batch, enqueued in order. A zone that moved or resized at the last boundary first
    // takes the body's presence at the step start (a discontinuity of the zone, not of the body).
    const auto Publish = [&](const FLDCaptureCircle& Circle, FLDCaptureReducer& Reducer, bool& InOutInside, bool bZoneMoved)
    {
        if (bZoneMoved)
        {
            const bool bStart = Circle.Contains(Batch.StartFootCm, Batch.InitialSupport);
            if (bStart != InOutInside)
            {
                FLDCaptureEvent Fact;
                Fact.Kind = ELDCaptureEventKind::Presence;
                Fact.Body = Batch.Body;
                Fact.Phase = ELDCommitPhase::Motion;
                Fact.bInside = bStart;
                Fact.Time.Seconds = Batch.Step.StartServerSeconds;
                Fact.Time.BucketMs = FMath::Min(FirstOpenMs, AtMs);
                if (!Reducer.Enqueue(Fact, Error)) { return false; }
                InOutInside = bStart;
            }
        }
        TArray<FLDCaptureCrossing> Crossings;
        if (!LDCaptureCircle::Extract(Circle, Batch, InOutInside, Crossings, Error)) { return false; }
        for (const FLDCaptureCrossing& Crossing : Crossings)
        {
            FLDCaptureEvent Fact;
            Fact.Kind = ELDCaptureEventKind::Presence;
            Fact.Body = Batch.Body;
            Fact.Phase = ELDCommitPhase::Motion;
            Fact.bInside = Crossing.bInside;
            Fact.Time.Seconds = Crossing.Seconds;
            int64 Bucket = AtMs;
            if (Crossing.Seconds < Batch.Step.EndServerSeconds
                && !LDMatchLifecycle::AuthoritySecondsToEventMs(Crossing.Seconds, Bucket, Error)) { return false; }
            Fact.Time.BucketMs = FMath::Clamp(Bucket, FirstOpenMs, AtMs);
            if (!Reducer.Enqueue(Fact, Error)) { return false; }
        }
        return true;
    };
    bool Inside = Track->bInside;
    bool bPublished = Publish(CaptureCircle, CaptureReducer, Inside, Track->CircleSerial != CaptureCircleSerial);
    for (int32 Index = 0; bPublished && Index < PrivatePoles.Num(); ++Index)
    {
        FLDPrivatePole& Pole = PrivatePoles[Index];
        bool* PoleInside = Pole.Inside.Find(Batch.Body.Victim.EntityId);
        bPublished = !PoleInside || Publish(Pole.Circle, Pole.Reducer, *PoleInside, false);
    }
    if (!bPublished)
    {
        UE_LOG(LogLDMatch, Error, TEXT("LD_CAPTURE_MOTION_REJECTED %s"), *Error);
        return ELDCommitReject::GeometryUnavailable;
    }
    Track->bInside = Inside;
    Track->CircleSerial = CaptureCircleSerial;
    Track->LastStep = Batch.Step.StepSerial;
    return ELDCommitReject::None;
}

ELDCommitReject ALDGameMode::PublishCaptureEligibility(const FLDGameplayWritePermit& Permit,
    const FLDCaptureEligibilityEvent& Event)
{
    const ELDCommitPhase Phase = Permit.GetPhase();
    ELDCaptureEventKind Kind = ELDCaptureEventKind::Joined;
    bool PhaseAllowed = false;
    switch (Event.Change)
    {
    case ELDCaptureEligibilityChange::Joined:
        Kind = ELDCaptureEventKind::Joined;
        // #198: a contender's first field life starts when the opening rest R0 returns to the field.
        PhaseAllowed = Phase == ELDCommitPhase::RespawnsAndWorld && MatchPhase == ELDMatchPhase::Rest;
        break;
    case ELDCaptureEligibilityChange::Respawned:
        Kind = ELDCaptureEventKind::Respawned;
        PhaseAllowed = Phase == ELDCommitPhase::RespawnsAndWorld;
        break;
    case ELDCaptureEligibilityChange::PhaseRetired:
        // #198: a field life retired by a field -> rest boundary leaves the spray roster like a death, without a kill.
        Kind = ELDCaptureEventKind::Died;
        PhaseAllowed = Phase == ELDCommitPhase::RespawnsAndWorld && MatchPhase == ELDMatchPhase::Rest;
        break;
    case ELDCaptureEligibilityChange::ProtectionEnded:
        Kind = ELDCaptureEventKind::ProtectionEnded;
        PhaseAllowed = Phase == ELDCommitPhase::Session || Phase == ELDCommitPhase::Actions;
        break;
    case ELDCaptureEligibilityChange::Died:
        Kind = ELDCaptureEventKind::Died;
        PhaseAllowed = Phase == ELDCommitPhase::DeathsAndRewards;
        break;
    default:
        // Left is published by the core's leave path only.
        return ELDCommitReject::InvalidRequest;
    }
    const auto Permission = ValidateWritePermit(Permit, Phase);
    if (Permission != ELDCommitReject::None) { return Permission; }
    if (!PhaseAllowed) { return ELDCommitReject::WrongPhase; }
    if (!CaptureReducer.IsActive() || !CaptureCircle.IsValid()) { return ELDCommitReject::WrongPhase; }
    const FGuid& Id = Event.Body.Victim.EntityId;
    if (Event.Body.Victim.Kind != ELDVictimKind::Contender || !Event.Body.IsValid() || !FindContender(Id))
    { return ELDCommitReject::UnknownBody; }
    if (!FMath::IsFinite(Event.Time.Seconds) || Event.Time.BucketMs < 0 || Event.Time.BucketMs > Permit.GetBoundary().AtMs)
    { return ELDCommitReject::TimeRegression; }
    FLDCaptureEvent Fact;
    Fact.Kind = Kind;
    Fact.Body = Event.Body;
    Fact.Time = Event.Time;
    Fact.Phase = Phase;
    Fact.ProtectedUntilMs = Event.ProtectedUntilMs;
    const bool bInitial = Kind == ELDCaptureEventKind::Joined || Kind == ELDCaptureEventKind::Respawned;
    if (bInitial)
    {
        if (Event.InitialFootCm.ContainsNaN()) { return ELDCommitReject::GeometryUnavailable; }
        Fact.bInside = CaptureCircle.Contains(Event.InitialFootCm, Event.InitialSupport);
    }
    const FLDDeathKey DeathKey{MatchContext.LedgerMatch, Event.Body.Victim};
    FLDContenderRecord* Killer = nullptr;
    TArray<FLDContenderRecord*> Assisters;
    const bool bKill = Kind == ELDCaptureEventKind::Died && Event.Change == ELDCaptureEligibilityChange::Died;
    if (Kind == ELDCaptureEventKind::Died && !bKill
        && (Event.DeathKey.IsValid() || Event.CreditContender.IsValid() || !Event.Assisters.IsEmpty()))
    { return ELDCommitReject::InvalidRequest; }
    if (bKill)
    {
        Killer = Event.CreditContender.IsValid() ? FindContender(Event.CreditContender) : nullptr;
        if (!(Event.DeathKey == DeathKey) || CountedDeaths.Contains(DeathKey)
            || uint8(Event.KillCause) > uint8(LDCapture::LastKillCause)
            || (Event.KillCause == ELDKillCause::Contender) != Event.CreditContender.IsValid()
            || Event.CreditContender == Id || (Event.CreditContender.IsValid() && !Killer)
            || (!Killer && !Event.Assisters.IsEmpty()))
        { return ELDCommitReject::InvalidRequest; }
        for (int32 Index = 0; Index < Event.Assisters.Num(); ++Index)
        {
            const FGuid& Assister = Event.Assisters[Index];
            FLDContenderRecord* Record = Assister.IsValid() ? FindContender(Assister) : nullptr;
            if (!Record || Assister == Id || Assister == Event.CreditContender
                || (Index > 0 && !(Event.Assisters[Index - 1] < Assister)))
            { return ELDCommitReject::InvalidRequest; }
            Assisters.Add(Record);
        }
        if (NextKillSerial == MAX_uint64) { return ELDCommitReject::CounterExhausted; }
        Fact.Killer = Event.CreditContender;
    }
    FString Error;
    if (!EnqueueCaptureFact(Fact, bInitial ? &Event.InitialFootCm : nullptr, bInitial ? &Event.InitialSupport : nullptr, Error))
    {
        UE_LOG(LogLDMatch, Warning, TEXT("LD_CAPTURE_FACT_REJECTED %s"), *Error);
        return ELDCommitReject::InvalidRequest;
    }
    if (bInitial)
    {
        FLDCaptureTrack& Track = CaptureTracks.FindOrAdd(Id);
        Track = {Event.Body, Fact.bInside, AuthorityStep.StepSerial, 0, CaptureCircleSerial};
    }
    else if (bKill)
    {
        CountedDeaths.Add(DeathKey);
        ++FindContender(Id)->Deaths;
        if (Killer) { ++Killer->Kills; }
        for (FLDContenderRecord* Assister : Assisters) { ++Assister->Assists; }
        // D27: the shown leader's death spends the notice.
        if (LeaderNotice.LeaderDied(Id, GetFieldNowMs()))
        {
            bPublicMatchDirty = true;
            UE_LOG(LogLDMatch, Display, TEXT("LD_LEADER_DOWN leader=%s killer=%s"), *Id.ToString(),
                *Event.CreditContender.ToString());
        }
        FLDKillNotice& Notice = KillNotices.AddDefaulted_GetRef();
        Notice.Serial = ++NextKillSerial;
        Notice.Victim = Id;
        Notice.Killer = Event.CreditContender;
        Notice.Assisters = Event.Assisters;
        Notice.Cause = Event.KillCause;
        Notice.CreatureName = Event.CreatureName;
        Notice.OccurredAtMs = Event.Time.BucketMs;
        while (KillNotices.Num() > LDCapture::MaxKillNotices) { KillNotices.RemoveAt(0); }
        bKillFeedDirty = true;
    }
    return ELDCommitReject::None;
}

ELDCommitReject ALDGameMode::PublishCaptureInterrupt(const FLDGameplayWritePermit& Permit, const FLDCombatBodyRef& Body)
{
    const ELDCommitPhase Phase = Permit.GetPhase();
    if (Phase != ELDCommitPhase::Actions && Phase != ELDCommitPhase::Effects) { return ELDCommitReject::WrongPhase; }
    const auto Permission = ValidateWritePermit(Permit, Phase);
    if (Permission != ELDCommitReject::None) { return Permission; }
    if (!CaptureReducer.IsActive() || !CaptureCircle.IsValid()) { return ELDCommitReject::WrongPhase; }
    if (Body.Victim.Kind != ELDVictimKind::Contender || !CaptureReducer.IsCommittedAliveBody(Body))
    { return ELDCommitReject::UnknownBody; }
    FLDCaptureEvent Fact;
    Fact.Kind = ELDCaptureEventKind::Interrupted;
    Fact.Body = Body;
    Fact.Phase = Phase;
    FString Error;
    // The control lands at this boundary instant: before any spray or guard completion due at the same instant.
    if (!FLDCaptureEventTime::FromMs(Permit.GetBoundary().AtMs, Fact.Time, Error) || !EnqueueCaptureFact(Fact, nullptr, nullptr, Error))
    {
        UE_LOG(LogLDMatch, Warning, TEXT("LD_CAPTURE_FACT_REJECTED %s"), *Error);
        return ELDCommitReject::InvalidRequest;
    }
    return ELDCommitReject::None;
}

void ALDGameMode::QueueLeaveFact(const FGuid& ContenderId)
{
    // A contender outside the capture roster (no committed join) has no eligibility to revoke.
    if (!IsGameplayWriteOpen(MatchContext) || !CaptureReducer.IsActive() || !CaptureReducer.IsCommittedMember(ContenderId))
    { return; }
    FLDCaptureEvent Fact;
    Fact.Kind = ELDCaptureEventKind::Left;
    Fact.Phase = ELDCommitPhase::Session;
    const FLDCaptureTrack* Track = CaptureTracks.Find(ContenderId);
    Fact.Body = Track ? Track->Body : FLDCombatBodyRef{{ELDVictimKind::Contender, ContenderId, 0}, FGuid()};
    // The leave happens at its actual receipt instant, after the last closed boundary.
    const double WorldNow = GetWorld()->GetTimeSeconds();
    double LeaveSeconds = WorldNow - AuthorityOriginWorldSeconds;
    if (!FMath::IsFinite(LeaveSeconds) || LeaveSeconds < AuthorityNowSeconds) { LeaveSeconds = AuthorityNowSeconds; }
    FString Error;
    if (!FLDCaptureEventTime::FromSeconds(LeaveSeconds, Fact.Time, Error))
    {
        FaultAuthority(Error);
        return;
    }
    Fact.Time.BucketMs = FMath::Max(Fact.Time.BucketMs, CaptureReducer.GetClosedThroughMs() + 1);
    if (!EnqueueCaptureFact(Fact, nullptr, nullptr, Error))
    {
        FaultAuthority(TEXT("A leave could not reach the capture reducer: ") + Error);
        return;
    }
    CaptureTracks.Remove(ContenderId);
}

#if WITH_DEV_AUTOMATION_TESTS
bool ALDGameMode::DevOpenCaptureNow(const FLDGameplayWritePermit& Permit, bool& bOutsideDevelopment, FString& Detail)
{
    bOutsideDevelopment = MatchPhase != ELDMatchPhase::Development || Timeline.GetStage() != ELDMatchStage::Field;
    if (bOutsideDevelopment)
    {
        Detail = TEXT("The capture circle opens early only during a timed field segment.");
        return false;
    }
    if (ValidateWritePermit(Permit, ELDCommitPhase::Transactions) != ELDCommitReject::None)
    {
        Detail = TEXT("An early capture opening needs the current Transactions permit.");
        return false;
    }
    bDevShowdownEntryPending = true;
    Detail = TEXT("Final rest return queued; all contenders enter the registered arena before the normal unlock delay.");
    return true;
}
#endif

bool ALDGameMode::ExecuteCapturePhase(const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (ValidateWritePermit(Permit, ELDCommitPhase::Capture) != ELDCommitReject::None || !CaptureReducer.IsActive())
    {
        Error = TEXT("Capture requires the current Capture permit and an active match reducer.");
        return false;
    }
    // Provider fence: every committed living body published this step's native motion,
    // except a body whose life was committed during this step after Motion.
    const uint64 Step = Permit.GetBoundary().BatchSerial;
    for (const auto& Pair : CaptureTracks)
    {
        const FLDCaptureTrack& Track = Pair.Value;
        if (CaptureReducer.IsCommittedAliveBody(Track.Body) && Track.RegisteredStep != Step && Track.LastStep != Step)
        {
            Error = FString::Printf(TEXT("Capture motion evidence is missing for a living contender at step %llu."), Step);
            return false;
        }
    }
    const uint64 EpochBefore = CaptureReducer.GetEpoch();
    const bool WasOpen = CaptureReducer.IsOpen();
    const int64 AtMs = Permit.GetBoundary().AtMs;
    TArray<FLDCaptureProposal> Candidates;
    const auto Close = [&](FLDCaptureReducer& Reducer, bool bInside)
    {
        TOptional<FLDCaptureProposal> Proposal;
        if (!(bInside ? Reducer.CloseThroughCompletion(AtMs, Proposal, Error) : Reducer.CloseBoundary(AtMs, Proposal, Error)))
        { return false; }
        if (Proposal) { Candidates.Add(Proposal.GetValue()); }
        return true;
    };
    if (!Close(CaptureReducer, bCaptureCompletesInsideStep && CaptureInsideSource == INDEX_NONE)) { return false; }
    bool bPrivateChanged = false;
    for (int32 Index = 0; Index < PrivatePoles.Num(); ++Index)
    {
        const uint64 Before = PrivatePoles[Index].Reducer.GetEpoch();
        if (!Close(PrivatePoles[Index].Reducer, bCaptureCompletesInsideStep && CaptureInsideSource == Index)) { return false; }
        bPrivateChanged |= Before != PrivatePoles[Index].Reducer.GetEpoch();
    }
    // Wins reached at once across the flagpoles settle together: the earliest instant, then guard > sprays > clear-out.
    const TOptional<FLDCaptureProposal> Proposal = FLDCaptureReducer::Settle(Candidates);
    if (CaptureReducer.IsOpen() && MatchPhase == ELDMatchPhase::Development)
    {
        MatchPhase = ELDMatchPhase::CaptureOpen;
        bLobbyDirty = true;
        UE_LOG(LogLDMatch, Display, TEXT("LD_CAPTURE_OPEN generation=%u at_ms=%lld"), MatchContext.Generation, Permit.GetBoundary().AtMs);
    }
    bPublicMatchDirty |= WasOpen != CaptureReducer.IsOpen() || EpochBefore != CaptureReducer.GetEpoch() || bPrivateChanged;
    PendingCapture = Proposal;
    PendingCaptureAtMs = AtMs;
    if (Proposal)
    {
        UE_LOG(LogLDMatch, Display, TEXT("LD_CAPTURE_DECIDED generation=%u kind=%u winner=%s at_ms=%lld"), MatchContext.Generation,
            uint8(Proposal->Kind), *LDLog::Id(Proposal->Winner), Proposal->DueAtMs);
    }
    else { UpdateCaptureGeometry(AtMs); }
    return true;
}

void ALDGameMode::UpdateCaptureGeometry(int64 AtMs)
{
    if (!Showdown.bStarted || !CaptureReducer.IsOpen()) { return; }
    // final_cage: a holder's new graffiti raises the fence on the zone as it stands now.
    for (auto& Pair : CageSprays)
    {
        const uint32 Sprays = CaptureReducer.GetCompletedSprays(Pair.Key);
        if (Sprays == Pair.Value) { continue; }
        Pair.Value = Sprays;
        if (CaptureReducer.GetGraffitiOwner() != Pair.Key) { continue; }
        CageUntilMs = AtMs + CageDurationMs;
        CageCentreCm = CaptureCircle.CenterCm;
        CageRadiusCm = CaptureCircle.RadiusCm;
        bPublicMatchDirty = true;
        UE_LOG(LogLDMatch, Display, TEXT("LD_FINAL_CAGE_UP holder=%s until_ms=%lld"), *LDLog::Id(Pair.Key), CageUntilMs);
    }
    if (CageUntilMs != 0 && AtMs >= CageUntilMs)
    {
        CageUntilMs = 0;
        CageCentreCm = FVector2D::ZeroVector;
        CageRadiusCm = 0;
        bPublicMatchDirty = true;
        UE_LOG(LogLDMatch, Display, TEXT("LD_FINAL_CAGE_DOWN at_ms=%lld"), AtMs);
    }
    const FGuid Holder = CaptureReducer.GetGraffitiOwner();
    FLDFinalZoneInput Zone;
    Zone.CurrentCm = CaptureCircle.CenterCm;
    Zone.BaseRadiusCm = CaptureBaseRadiusCm;
    Zone.DoubleYellowRadiusCm = DoubleYellowRadiusCm;
    Zone.OwnerMod = Holder.IsValid() ? uint8(CaptureReducer.ModOf(Holder)) : 0;
    Zone.bCageStanding = CageUntilMs != 0;
    FLDCombatBodyRef Body;
    FVector Foot = FVector::ZeroVector;
    if (Holder.IsValid() && ReadLivingBody(Holder, Body, Foot)) { Zone.OwnerFootCm = FVector2D(Foot.X, Foot.Y); }
    FVector2D Centre;
    double Radius = 0;
    LDCaptureCircle::FinalZone(Zone, Centre, Radius);
    if (!Centre.Equals(CaptureCircle.CenterCm, .01)) { CapturePoleCm = Centre.Equals(CaptureAnchorCm, .01) ? Showdown.CentreCm : Foot; }
    if (Centre.Equals(CaptureCircle.CenterCm, .01) && Radius == CaptureCircle.RadiusCm) { return; }
    CaptureCircle.CenterCm = Centre;
    CaptureCircle.RadiusCm = Radius;
    CaptureCircle.bAnyGround = !Centre.Equals(CaptureAnchorCm, .01);
    ++CaptureCircleSerial;
    bPublicMatchDirty = true;
}

bool ALDGameMode::ExecuteClosePhase(const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (ValidateWritePermit(Permit, ELDCommitPhase::Close) != ELDCommitReject::None)
    {
        Error = TEXT("Close requires the current Close permit.");
        return false;
    }
    if (!PendingCapture) { return CloseShowdownIfDue(Permit, Error); }
    const FLDCaptureProposal Proposal = PendingCapture.GetValue();
    // A win planned inside this step happened before its boundary; any other one was reached by this boundary.
    const int64 AtMs = Permit.GetBoundary().AtMs;
    if (PendingCaptureAtMs != AtMs || (bCaptureCompletesInsideStep ? Proposal.DueAtMs >= AtMs : Proposal.DueAtMs > AtMs))
    {
        PendingCapture.Reset();
        Error = TEXT("The decided capture win no longer matches this boundary.");
        return false;
    }
    if (HasPendingRewardRetries())
    {
        if (bSettlementStep || bCaptureCompletesInsideStep)
        {
            PendingCapture.Reset();
            Error = TEXT("The settlement boundary still owes a reward; the guard win cannot lock over it.");
            return false;
        }
        // The guard is complete at this instant, but a reward of a death up to it is still owed. The proposal
        // waits for a settlement boundary at the same instant: its single retry first, then this Close.
        UE_LOG(LogLDMatch, Display, TEXT("LD_CAPTURE_CLOSE_AWAITS_REWARD generation=%u due_ms=%lld"),
            MatchContext.Generation, Proposal.DueAtMs);
        return true;
    }
    PendingCapture.Reset();
    const ELDMatchEndReason Reason = !Proposal.Winner.IsValid() ? ELDMatchEndReason::NoChampion
        : Proposal.Kind == ELDCaptureWin::Sprays ? ELDMatchEndReason::SprayCount
        : Proposal.Kind == ELDCaptureWin::ClearOut ? ELDMatchEndReason::ClearOut : ELDMatchEndReason::GuardSucceeded;
    return TryCloseMatch(Reason, Proposal.Winner, Proposal.DueAtMs, Error);
}

bool ALDGameMode::HasPendingRewardRetries() const
{
    return Combat && !Combat->RetryRewards.IsEmpty();
}

bool ALDGameMode::TryCloseMatch(ELDMatchEndReason Reason, const FGuid& Winner, int64 EndedAtMs, FString& Error)
{
    if (bResultLocked)
    {
        Error = TEXT("AlreadyTerminal: the match result is already locked.");
        return false;
    }
    if (HasPendingRewardRetries())
    {
        Error = TEXT("RewardPending: a locked death's reward awaits its retry; the result cannot lock before it settles.");
        return false;
    }
    if (!IsGameplayWriteOpen(MatchContext) || (ActiveWritePermit && ActiveWritePermit->GetPhase() != ELDCommitPhase::Close))
    {
        Error = TEXT("Only an active match can lock its result, outside other commit phases.");
        return false;
    }
    const FLDMatchProviderHooks* Progression = LDMatchProviders::Find(ELDMatchProvider::Progression);
    if (!Progression || !Progression->ReadFinalLevel)
    {
        Error = TEXT("The result cannot be frozen without committed final levels from Progression.");
        return false;
    }
    // Prepare the complete frozen result first; nothing below can fail after the gate closes.
    FLDResultView View;
    View.Generation = MatchContext.Generation;
    View.ResultId = FGuid::NewGuid();
    View.ResultSerial = 1;
    View.Reason = Reason;
    View.Winner = Winner;
    View.StartedAtMs = MatchStartedAtMs;
    View.EndedAtMs = EndedAtMs;
    View.DurationMs = EndedAtMs - MatchStartedAtMs;
    for (const FGuid& Id : RosterOrder)
    {
        const FLDContenderRecord* Record = FindContender(Id);
        if (!Record)
        {
            Error = TEXT("The locked roster lost a contender record before the result.");
            return false;
        }
        FLDResultRow& Row = View.Rows.AddDefaulted_GetRef();
        Row.ContenderId = Id;
        Row.Name = Record->Name;
        Row.Kind = Record->Kind;
        Row.bLeft = Record->bLeft;
        Row.Kills = Record->Kills;
        Row.Deaths = Record->Deaths;
        Row.Assists = Record->Assists;
        Row.BankedGuardMs = CaptureReducer.GetBankedGuardMs(Id);
        Row.CompletedSprays = CaptureReducer.GetCompletedSprays(Id);
        if (!Progression->ReadFinalLevel(*this, Id, Row.FinalLevel, Error))
        {
            if (Error.IsEmpty()) { Error = TEXT("Progression refused a committed final level."); }
            return false;
        }
    }
    if (!LDCapture::ValidateResultView(View, Error)) { return false; }
    if (GateRevision == MAX_uint64)
    {
        Error = TEXT("Write gate revision exhausted.");
        return false;
    }
    MatchPhase = ELDMatchPhase::Results;
    ++GateRevision;
    ResultView = MoveTemp(View);
    bResultLocked = true;
    bResultPublished = false;
    bPublicMatchDirty = true;
    UE_LOG(LogLDMatch, Display, TEXT("LD_MATCH_RESULT generation=%u reason=%u winner=%s ended_ms=%lld"),
        MatchContext.Generation, uint8(Reason), *Winner.ToString(EGuidFormats::Digits), EndedAtMs);
    LogPlaytestCasts();
    LogPlaytestSummary(ResultView);
    Error.Reset();
    return true;
}

bool ALDGameMode::ExecuteCapturePublish(const FLDAuthorityBoundary& Boundary, FString& Error)
{
    if (!HasAuthority() || ActiveWritePermit || !(Boundary == AuthorityBoundary) || Boundary.Match != MatchContext)
    {
        Error = TEXT("Capture publication requires the boundary that was just closed, outside any permit.");
        return false;
    }
    PublishMatchOutcome();
    if (bResultLocked && bAuthorityStepOpen)
    {
        // The result closed the gate inside this step. The step ends here, at its completed
        // boundary; the driver stops instead of finishing a step of a closed match.
        AuthorityNowSeconds = AuthorityStep.EndServerSeconds;
        AuthorityNowMs = AuthorityBoundary.AtMs;
        bAuthorityStepOpen = false;
        if (!PublishAuthorityClock(Error)) { return false; }
    }
    Error.Reset();
    return true;
}

void ALDGameMode::PublishMatchOutcome()
{
    auto* Public = GetGameState<ALDGameState>();
    bool Published = false;
    if (bKillFeedDirty && Public)
    {
        bKillFeedDirty = false;
        FLDKillFeedView Feed;
        Feed.Generation = MatchContext.Generation;
        Feed.Notices = KillNotices;
        for (const FGuid& Id : RosterOrder)
        {
            if (const FLDContenderRecord* Record = FindContender(Id)) { Feed.Scores.Add({Id, Record->Kills, Record->Deaths, Record->Assists}); }
        }
        FString Error;
        if (!LDCapture::ValidateKillFeed(Feed, Error)) { UE_LOG(LogLDMatch, Error, TEXT("LD_KILL_FEED_INVALID %s"), *Error); }
        else
        {
            Public->KillFeed = MoveTemp(Feed);
            Public->OnRep_KillFeed();
            Published = true;
        }
    }
    if (bResultLocked && !bResultPublished)
    {
        bResultPublished = true;
        const FLDMatchProviderHooks* Progression = LDMatchProviders::Find(ELDMatchProvider::Progression);
        for (FLDResultRow& Row : ResultView.Rows)
        {
            FLDResultBuild Build;
            FString BuildError;
            if (!Progression || !Progression->ReadResultBuild
                || !Progression->ReadResultBuild(*this, Row.ContenderId, Build, BuildError)
                || !Build.bAvailable || !LDCapture::ValidateResultBuild(Build, BuildError))
            {
                if (BuildError.IsEmpty()) { BuildError = TEXT("The result build projection is unavailable."); }
                UE_LOG(LogLDMatch, Warning, TEXT("LD_RESULT_BUILD_UNAVAILABLE contender=%s reason=%s"),
                    *LDLog::Id(Row.ContenderId), *LDLog::Quote(BuildError));
                continue;
            }
            Row.Build = MoveTemp(Build);
        }
        // The gate is already closed: providers may only cancel and clean up.
        FLDMatchClosed Closed;
        Closed.Match = MatchContext;
        Closed.ResultId = ResultView.ResultId;
        Closed.Reason = ResultView.Reason;
        Closed.Winner = ResultView.Winner;
        Closed.EndedAtMs = ResultView.EndedAtMs;
        for (int32 Index = 0; Index < int32(ELDMatchProvider::Count); ++Index)
        {
            const FLDMatchProviderHooks* Hooks = LDMatchProviders::Find(ELDMatchProvider(Index));
            if (Hooks && Hooks->MatchClosed) { Hooks->MatchClosed(*this, Closed); }
        }
        for (const auto& Pair : Lives)
        {
            if (auto* Body = Pair.Value.Actor.Get()) { Body->GetCharacterMovement()->StopMovementImmediately(); }
        }
        if (Public)
        {
            Public->MatchResult = ResultView;
            Public->OnRep_MatchResult();
            Published = true;
        }
        bLobbyDirty = true;
        PublishBuildBlueprints();
        PublishDamageSummaries();
    }
    if (bPublicMatchDirty)
    {
        PublishPublicMatch();
        Published = true;
    }
    if (bLobbyDirty)
    {
        bLobbyDirty = false;
        PublishLobby();
    }
    if (Public && Published) { Public->ForceNetUpdate(); }
}

FLDPublicMatchView ALDGameMode::ReadPublicMatch() const
{
    FLDPublicMatchView View;
    View.Generation = MatchContext.Generation;
    View.PublicRevision = PublicMatchRevision;
    View.Phase = MatchPhase;
    const bool Active = MatchPhase == ELDMatchPhase::Rest || MatchPhase == ELDMatchPhase::Development
        || MatchPhase == ELDMatchPhase::CaptureOpen || MatchPhase == ELDMatchPhase::Results;
    if (!Active || !CaptureReducer.IsActive()) { return View; }
    View.ServerNowMs = bResultLocked ? ResultView.EndedAtMs : AuthorityNowMs;
    View.StartedAtMs = MatchStartedAtMs;
    View.Timeline = Timeline.MakeView(MatchContext.Generation, TimelineRevision);
    View.Timeline.Signups = RestSignups.Array();
    View.Timeline.Signups.Sort();
    View.Timeline.ReadyContenders = RestReady.Array();
    View.Timeline.ReadyContenders.Sort();
    // #198 session D: the current rest's PK; empty without the provider or outside a PK.
    if (PKDirector) { View.PK = PKDirector->GetView(); }
    View.PK.Generation = MatchContext.Generation;
    View.OpenAtMs = CaptureReducer.GetOpenAtMs();
    View.CircleState = CaptureReducer.GetState();
    View.CaptureEpoch = CaptureReducer.GetEpoch();
    if (View.CircleState == ELDCircleState::Solo)
    {
        View.Occupier = CaptureReducer.GetOccupier();
        View.SoloSinceMs = CaptureReducer.GetSoloSinceMs();
        View.DueAtMs = CaptureReducer.GetDueAtMs();
    }
    else if (View.CircleState == ELDCircleState::Contested)
    {
        View.Occupier = CaptureReducer.GetOccupier();
        View.PausedProgressMs = CaptureReducer.GetPausedProgressMs();
    }
    View.GraffitiOwner = CaptureReducer.GetGraffitiOwner();
    if (View.GraffitiOwner.IsValid())
    {
        // The owner's public identity slot: his start roster position, the same key his body tint uses.
        const int32 Slot = RosterOrder.IndexOfByKey(View.GraffitiOwner);
        View.GraffitiPresentation = Slot >= 0 && Slot < MAX_uint8 ? uint8(Slot) : MAX_uint8;
    }
    View.GuardDueAtMs = CaptureReducer.GetGuardDueAtMs();
    View.GuardKeptMs = CaptureReducer.GetGuardKeptMs();
    View.OwnerBankedGuardMs = CaptureReducer.GetBankedGuardMs(View.GraffitiOwner);
    View.FadeAtMs = CaptureReducer.GetFadeAtMs();
    View.Showdown = Showdown;
    if (Showdown.bStarted)
    {
        View.PoleCentreCm = CapturePoleCm;
        View.PoleRadiusCm = float(CaptureCircle.RadiusCm);
        if (CageUntilMs != 0)
        {
            View.CageUntilMs = CageUntilMs;
            View.CageCentreCm = FVector(CageCentreCm.X, CageCentreCm.Y, Showdown.CentreCm.Z);
            View.CageRadiusCm = float(CageRadiusCm);
        }
        View.FinalMods = ShowdownMods;
        for (const FLDPrivatePole& Pole : PrivatePoles)
        {
            const FLDCaptureReducer& R = Pole.Reducer;
            FLDPrivatePoleView& Row = View.PrivatePoles.AddDefaulted_GetRef();
            Row.Holder = Pole.Holder;
            const int32 Slot = RosterOrder.IndexOfByKey(Pole.Holder);
            Row.Presentation = Slot >= 0 && Slot < MAX_uint8 ? uint8(Slot) : MAX_uint8;
            Row.CentreCm = FVector(Pole.Circle.CenterCm.X, Pole.Circle.CenterCm.Y, Showdown.CentreCm.Z);
            Row.RadiusCm = float(Pole.Circle.RadiusCm);
            Row.CircleState = R.GetState();
            Row.DueAtMs = R.GetDueAtMs();
            Row.PausedProgressMs = R.GetPausedProgressMs();
            Row.bGraffiti = R.GetGraffitiOwner().IsValid();
            Row.GuardDueAtMs = R.GetGuardDueAtMs();
            Row.GuardKeptMs = R.GetGuardKeptMs();
            Row.GuardMs = R.GetGuardMs();
        }
    }
    if (bResultLocked) { View.ResultId = ResultView.ResultId; }
    if (!Showdown.bStarted)
    {
        View.Airdrop = Airdrop.View();
        View.Leader = LeaderNotice.View();
        View.Boss = Camps ? Camps->BossView() : FLDBossView();
        FieldMarkers.View(View.Markers);
        // P6: the current (or last) mid-field objective.
        View.Objective = Objective.View;
    }
    View.DailyModifiers = DailyModifiers;
    return View;
}

void ALDGameMode::PublishPublicMatch()
{
    bPublicMatchDirty = false;
    auto* Public = GetGameState<ALDGameState>();
    if (!Public || !MatchContext.IsValid()) { return; }
    if (PublicMatchRevision == MAX_uint64)
    {
        UE_LOG(LogLDMatch, Error, TEXT("LD_PUBLIC_MATCH_REVISION_EXHAUSTED"));
        return;
    }
    ++PublicMatchRevision;
    FLDPublicMatchView View = ReadPublicMatch();
    FString Error;
    if (!LDCapture::ValidatePublicMatchView(View, Error))
    {
        UE_LOG(LogLDMatch, Error, TEXT("LD_PUBLIC_MATCH_INVALID %s"), *Error);
        return;
    }
    Public->PublicMatch = MoveTemp(View);
    Public->OnRep_PublicMatch();
}

void ALDGameMode::NotifyLifeCommitted(const FLDLifeNotice& Notice)
{
    if (Notice.Match != MatchContext) { return; }
    for (int32 Index = 0; Index < int32(ELDMatchProvider::Count); ++Index)
    {
        const FLDMatchProviderHooks* Hooks = LDMatchProviders::Find(ELDMatchProvider(Index));
        if (Hooks && Hooks->LifeCommitted) { Hooks->LifeCommitted(*this, Notice); }
    }
}
