#include "Framework/LDGameMode.h"
#include "Core/LDLog.h"
#include "Framework/LDDemoEntry.h"
#include "Framework/LDPlayerController.h"
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Combat/LDCombatDirector.h"
#include "Components/CapsuleComponent.h"
#include "Development/LDDemoContentCatalog.h"
#include "Economy/LDEconomyDirector.h"
#include "Engine/World.h"
#include "Region/LDMapRegistry.h"
#include "Character/LDNativeMotionTypes.h"
#include "World/LDSpawnPlacement.h"
#include "Vision/LDVisionDirector.h"
#include "Misc/ScopeExit.h"

namespace
{
    constexpr int32 MaxPendingNativeBatches=256;
    constexpr int32 MaxNativeRecordsPerBatch=128;
}

bool ALDGameMode::IsMemberPresent(const FGuid& EntityId) const
{
    const auto* Life = Lives.Find(EntityId);
    return Life && Life->Kind == ELDVictimKind::Contender && Life->Phase != ELDLifePhase::Left;
}

bool ALDGameMode::InitializeAuthorityCore(FString& Error)
{
    if (!HasAuthority() || !IsDemoRoom() || MatchPhase != ELDMatchPhase::Starting
        || bAuthorityInitialized || Combat || Vision || !MatchContext.IsValid())
    {
        Error = TEXT("Authority initialization requires the current uninitialized Starting match.");
        return false;
    }
    auto* Registry = GetWorld()->GetSubsystem<ULDMapRegistry>();
#if WITH_DEV_AUTOMATION_TESTS
    // DEV content hot reload: each match pins the content synced before it started; nothing reloads mid-match.
    if (LDDemoEntry::IsDevContentReload() && !LDDemoEntry::RefreshDevContent(Error)) { return false; }
#endif
    if (!LDDemoEntry::LoadRuntimeContent(DemoContent, Error) || !Registry || !Registry->Ensure(Error))
    {
        if (Error.IsEmpty()) { Error = TEXT("The current Demo world registry is missing."); }
        return false;
    }
    if (!Registry->BeginSpawnMatch(MatchContext,Error)) { return false; }
    AuthorityOriginWorldSeconds = GetWorld()->GetTimeSeconds();
    if (!FMath::IsFinite(AuthorityOriginWorldSeconds) || AuthorityOriginWorldSeconds < 0)
    {
        Error = TEXT("The native world clock cannot initialize match simulation time.");
        return false;
    }
    FActorSpawnParameters Spawn;
    Spawn.Owner = this;
    Combat = GetWorld()->SpawnActor<ALDCombatDirector>(Spawn);
    if (!Combat)
    {
        Error = TEXT("The sole Combat driver could not be created.");
        return false;
    }
    LastWorldClockSample = AuthorityOriginWorldSeconds;
    AuthorityNowSeconds = 0;
    AuthorityNowMs = 0;
    AuthorityStep = {MatchContext, 1, 0, 0};
    AuthorityBoundary = {MatchContext, 1, 0};
    Vision=GetWorld()->SpawnActor<ALDVisionDirector>(Spawn);
    if (!Vision || !Vision->BeginMatch(MatchContext,Error))
    {
        if (Error.IsEmpty()) { Error=TEXT("The current observation owner could not initialize."); }
        RetireMatchDomains();
        return false;
    }
    for (const auto& Pair : Contenders)
    {
        if (!IsMemberPresent(Pair.Value.Session)) { continue; }
        if (!Vision->RegisterObserver(Pair.Value.Session,Pair.Key.Get(),Error))
        {
            RetireMatchDomains();
            return false;
        }
    }
    // Growth/Shop: ledger, growth, equipment and shop directors for this match; roster setup is the provider Prepare.
    if (!ALDEconomyDirector::SpawnDomain(*this,Spawn,Error))
    {
        RetireMatchDomains();
        return false;
    }
    // Capture/endgame: the unique circle and the approved opening/hold for this match.
    if (!InitializeCaptureMatch(Error))
    {
        RetireMatchDomains();
        return false;
    }
    // D21 (#79): the public dorm disks that Combat's recovery reads for this match.
    const ULDMapRegistry* DormRegistry = GetWorld()->GetSubsystem<ULDMapRegistry>();
    if (!DemoContent || !DormRegistry || !LDDormZones::Resolve(*DormRegistry, *DemoContent, DormZones, Error))
    {
        if (Error.IsEmpty()) { Error = TEXT("Dorm initialization requires the approved content and the Demo world registry."); }
        RetireMatchDomains();
        return false;
    }
    // D26 (#79): the airdrop schedule from this match's start.
    if (!LDAirdrop::Resolve(*DormRegistry, *DemoContent, MatchStartedAtMs, MatchContext.LedgerMatch, Airdrop, Error))
    {
        RetireMatchDomains();
        return false;
    }
    // D27 (#79): the leader notice refreshes from this match's start.
    if (!LDLeaderNotice::Resolve(*DemoContent, MatchStartedAtMs, MatchContext.LedgerMatch, LeaderNotice, Error))
    {
        RetireMatchDomains();
        return false;
    }
    bAuthorityInitialized = true;
    if (!PublishAuthorityClock(Error))
    {
        RetireMatchDomains();
        return false;
    }
    return true;
}

void ALDGameMode::RetireMatchDomains()
{
    // Per-match domain actors. Each domain retires here, both after a failed initialization
    // and when RetireAuthorityCore ends a match for the next Lobby generation.
    if (Vision) { Vision->Destroy(); Vision=nullptr; }
    if (Combat) { Combat->Destroy(); Combat=nullptr; }
    // Growth/Shop: the ledger, growth, equipment and shop directors of the retired match.
    ALDEconomyDirector::RetireDomain(*this);
    CaptureReducer.Reset();
    CaptureCircle={};
    DormZones={};
    Airdrop={};
    LeaderNotice={};
    CaptureTracks.Reset();
    PendingCapture.Reset();
    bAuthorityInitialized=false;
}

bool ALDGameMode::AvailableAuthorityTime(int64& ThroughMs, FString& Error)
{
    ThroughMs = 0;
    const double WorldNow = GetWorld()->GetTimeSeconds();
    if (!bAuthorityInitialized || !FMath::IsFinite(WorldNow) || WorldNow < LastWorldClockSample)
    {
        Error = TEXT("Match simulation requires a finite, nonregressing native game clock.");
        return false;
    }
    LastWorldClockSample = WorldNow;
    return LDMatchLifecycle::AuthoritySecondsToNowMs(WorldNow - AuthorityOriginWorldSeconds, ThroughMs, Error);
}

bool ALDGameMode::CaptureIncomingTime(int64& NotBeforeMs, FString& Error) const
{
    NotBeforeMs=0;
    const double WorldNow=GetWorld()->GetTimeSeconds();
    if (!bAuthorityInitialized || !IsGameplayWriteOpen(MatchContext) || !FMath::IsFinite(WorldNow)
        || WorldNow<LastWorldClockSample)
    {
        Error=TEXT("An incoming intent has no valid current match-time sample.");
        return false;
    }
    return LDMatchLifecycle::AuthoritySecondsToEventMs(WorldNow-AuthorityOriginWorldSeconds,NotBeforeMs,Error);
}

ELDCommitReject ALDGameMode::BeginAuthorityStep(ALDCombatDirector& Driver, const FLDMatchContext& Match, int64 EndAtMs)
{
    if (!HasAuthority() || Combat != &Driver) { return ELDCommitReject::NotAuthority; }
    if (Match != MatchContext) { return ELDCommitReject::StaleMatch; }
    if (!IsGameplayWriteOpen(Match)) { return ELDCommitReject::MatchClosed; }
    if (ActiveWritePermit || bAuthorityStepOpen) { return ELDCommitReject::Reentrant; }
    // A held capture completion admits only its settlement boundary, never a later instant.
    if (PendingCapture.IsSet()) { return ELDCommitReject::WrongPhase; }
    if (EndAtMs <= AuthorityNowMs) { return ELDCommitReject::TimeRegression; }
    int64 Available = 0;
    FString Error;
    if (!AvailableAuthorityTime(Available, Error)) { FaultAuthority(Error); return ELDCommitReject::AuthorityFault; }
    if (EndAtMs > Available) { return ELDCommitReject::TimeUnavailable; }
    if (AuthorityStep.StepSerial == MAX_uint64) { return ELDCommitReject::CounterExhausted; }
    const double EndSeconds = double(EndAtMs) / 1000.;
    if (!FMath::IsFinite(EndSeconds) || EndSeconds <= AuthorityNowSeconds) { return ELDCommitReject::TimeUnavailable; }
    AuthorityStep = {MatchContext, AuthorityStep.StepSerial + 1, AuthorityNowSeconds, EndSeconds};
    AuthorityBoundary = {MatchContext, AuthorityStep.StepSerial, EndAtMs};
    bAuthorityStepOpen = true;
    bAuthorityPhaseExecuted = false;
    bSettlementStep = false;
    bCaptureCompletesInsideStep = false;
    return ELDCommitReject::None;
}

ELDCommitReject ALDGameMode::BeginSettlementStep(ALDCombatDirector& Driver, const FLDMatchContext& Match)
{
    if (!HasAuthority() || Combat != &Driver) { return ELDCommitReject::NotAuthority; }
    if (Match != MatchContext) { return ELDCommitReject::StaleMatch; }
    if (!IsGameplayWriteOpen(Match)) { return ELDCommitReject::MatchClosed; }
    if (ActiveWritePermit || bAuthorityStepOpen) { return ELDCommitReject::Reentrant; }
    if (!PendingCapture.IsSet() || PendingCapture->DueAtMs != AuthorityNowMs || !HasPendingRewardRetries())
    { return ELDCommitReject::WrongPhase; }
    if (AuthorityStep.StepSerial == MAX_uint64) { return ELDCommitReject::CounterExhausted; }
    // No time passes: the boundary only gives the owed reward its fresh permit before the same-instant Close.
    AuthorityStep = {MatchContext, AuthorityStep.StepSerial + 1, AuthorityNowSeconds, AuthorityNowSeconds};
    AuthorityBoundary = {MatchContext, AuthorityStep.StepSerial, AuthorityNowMs};
    bAuthorityStepOpen = true;
    bAuthorityPhaseExecuted = false;
    bSettlementStep = true;
    bCaptureCompletesInsideStep = false;
    return ELDCommitReject::None;
}

ELDCommitReject ALDGameMode::ValidateWritePermit(const FLDGameplayWritePermit& Permit, ELDCommitPhase Phase) const
{
    if (!HasAuthority() || Permit.Owner != this) { return ELDCommitReject::NotAuthority; }
    if (Permit.Boundary.Match != MatchContext) { return ELDCommitReject::StaleMatch; }
    if (Permit.GateRevision != GateRevision || ActiveWritePermit != &Permit) { return ELDCommitReject::ScopeExpired; }
    if (Permit.Phase != Phase || !(Permit.Boundary == AuthorityBoundary)) { return ELDCommitReject::WrongPhase; }
    const bool Initializing = MatchPhase == ELDMatchPhase::Starting && Phase == ELDCommitPhase::RespawnsAndWorld;
    if (!bAuthorityInitialized || (!Initializing && !IsGameplayWriteOpen(MatchContext)))
    { return ELDCommitReject::MatchClosed; }
    return ELDCommitReject::None;
}

ELDCommitReject ALDGameMode::ResolveBody(const FLDMatchContext& Match, const FLDCombatBodyRef& Body, ALDCharacter*& Out) const
{
    Out = nullptr;
    if (Match != MatchContext || !Match.IsValid()) { return ELDCommitReject::StaleMatch; }
    if (!Body.IsValid()) { return ELDCommitReject::InvalidRequest; }
    const auto* Life = Lives.Find(Body.Victim.EntityId);
    if (!Life || Life->Kind != Body.Victim.Kind) { return ELDCommitReject::UnknownBody; }
    if (Life->Life != Body.Victim.Life || Body.Victim.Life <= Life->RetiredThroughLife
        || Life->Phase != ELDLifePhase::Alive) { return ELDCommitReject::StaleLife; }
    const auto* State = Combat && Life->Actor.IsValid() ? Combat->StateFor(Life->Actor.Get()) : nullptr;
    if (Life->Body != Body || !State || State->Identity != Body.BodyIdentity || State->Victim != Body.Victim)
    { return ELDCommitReject::UnknownBody; }
    Out = Life->Actor.Get();
    return ELDCommitReject::None;
}

bool ALDGameMode::ReadBodyRef(const ALDCharacter& Body, FLDCombatBodyRef& Out) const
{
    Out = {};
    const auto* State = Combat ? Combat->StateFor(&Body) : nullptr;
    if (!State || !State->Victim.IsValid()) { return false; }
    const FLDCombatBodyRef Ref{State->Victim, State->Identity};
    ALDCharacter* Current = nullptr;
    if (ResolveBody(MatchContext, Ref, Current) != ELDCommitReject::None || Current != &Body) { return false; }
    Out = Ref;
    return true;
}

bool ALDGameMode::ReadContenderLife(const FLDMatchContext& Match, const FGuid& ContenderId,
    FLDContenderLifeRead& Out, FString& Error) const
{
    Out={};
    const auto* Life=Lives.Find(ContenderId);
    if (!HasAuthority() || Match!=MatchContext || !Match.IsValid() || !Life || Life->Kind!=ELDVictimKind::Contender)
    {
        Error=TEXT("A current registered contender is required for a life read.");
        return false;
    }
    Out={Life->Phase,Life->Life,Life->Revision,Life->Body};
    Error.Reset();
    return true;
}

bool ALDGameMode::ReadBodyMotionStream(const FLDMatchContext& Match, const FLDCombatBodyRef& Body,
    FGuid& Out, FString& Error) const
{
    Out.Invalidate();
    ALDCharacter* Actor=nullptr;
    if (ResolveBody(Match,Body,Actor)!=ELDCommitReject::None)
    {
        Error=TEXT("Motion stream requires the current registered body.");
        return false;
    }
    const auto& Life=Lives.FindChecked(Body.Victim.EntityId);
    if (!Life.MotionStreamId.IsValid() || Life.MotionBody!=Body)
    {
        Error=TEXT("Current body has no registered motion stream.");
        return false;
    }
    Out=Life.MotionStreamId;
    Error.Reset();
    return true;
}

ELDCommitReject ALDGameMode::ValidateMotionStep(const FLDAuthorityStep& Step, const FLDCombatBodyRef& Body) const
{
    if (Step.Match!=MatchContext) { return ELDCommitReject::StaleMatch; }
    if (!IsGameplayWriteOpen(Step.Match)) { return ELDCommitReject::MatchClosed; }
    if (!bAuthorityStepOpen || !(Step == AuthorityStep)) { return ELDCommitReject::StaleStep; }
    ALDCharacter* Current = nullptr;
    const auto Result = ResolveBody(Step.Match, Body, Current);
    if (Result != ELDCommitReject::None) { return Result; }
    const auto& Life = Lives.FindChecked(Body.Victim.EntityId);
    if (Life.NativeMotion.Num()>=MaxPendingNativeBatches) { return ELDCommitReject::CapacityExceeded; }
    return Life.CompletedMotionStep == Step.StepSerial ? ELDCommitReject::StepAlreadyCompleted : ELDCommitReject::None;
}

ELDCommitReject ALDGameMode::CompleteNativeMotion(const FLDAuthorityStep& Step, const FLDCombatBodyRef& Body,
    const FLDGameplayWritePermit& Permit, TSharedPtr<const FLDNativeMotionBatch> Batch)
{
    const auto Permission = ValidateWritePermit(Permit, ELDCommitPhase::Motion);
    if (Permission != ELDCommitReject::None) { return Permission; }
    const auto Motion = ValidateMotionStep(Step, Body);
    if (Motion != ELDCommitReject::None) { return Motion; }
    auto& Life=Lives.FindChecked(Body.Victim.EntityId);
    const auto* Registry=GetWorld()->GetSubsystem<ULDMapRegistry>();
    if (!Batch || !(Batch->Step==Step) || Batch->Body!=Body || Batch->StreamId!=Life.MotionStreamId
        || Life.MotionBody!=Body || !Registry || Batch->World!=Registry->Revision()
        || Batch->PreviousSequence!=Life.MotionThroughSequence || Batch->Records.Num()>MaxNativeRecordsPerBatch
        || Batch->StartFootCm.ContainsNaN() || Batch->EndFootCm.ContainsNaN()
        || !Batch->StartFootCm.Equals(Life.MotionLastFoot,.000001)
        || Batch->InitialSupport.Grounded!=Life.MotionGrounded || Batch->InitialSupport.Floor!=Life.MotionFloor)
    { return ELDCommitReject::GeometryUnavailable; }
    uint64 Sequence=Batch->PreviousSequence;
    double Time=Step.StartServerSeconds;
    FVector Foot=Batch->StartFootCm;
    FLDNativeSupportSample Support=Batch->InitialSupport;
    for (const auto& Record : Batch->Records)
    {
        if (Sequence==MAX_uint64 || Record.Sequence!=++Sequence
            || uint8(Record.Kind)>uint8(ELDNativeMotionRecordKind::Support)
            || Record.FromFootCm.ContainsNaN() || Record.ToFootCm.ContainsNaN()
            || !Record.FromFootCm.Equals(Foot,.000001)
            || !FMath::IsFinite(Record.StartServerSeconds) || !FMath::IsFinite(Record.EndServerSeconds)
            || Record.StartServerSeconds<Time || Record.EndServerSeconds<Record.StartServerSeconds
            || Record.EndServerSeconds>Step.EndServerSeconds
            || (Record.Kind==ELDNativeMotionRecordKind::Support
                && (Record.FromFootCm!=Record.ToFootCm || Record.StartServerSeconds!=Record.EndServerSeconds
                    || (Record.Support.Grounded && !Record.Support.Floor.IsValid()))))
        { return ELDCommitReject::GeometryUnavailable; }
        Time=Record.EndServerSeconds;
        Foot=Record.ToFootCm;
        if (Record.Kind==ELDNativeMotionRecordKind::Support) { Support=Record.Support; }
    }
    if (Sequence!=Batch->ThroughSequence || !Foot.Equals(Batch->EndFootCm,.000001)
        || Support.Grounded!=Batch->FinalSupport.Grounded || Support.Floor!=Batch->FinalSupport.Floor)
    { return ELDCommitReject::GeometryUnavailable; }
    // Capture reads every accepted contender batch synchronously; the driver releases it next step.
    if (Body.Victim.Kind==ELDVictimKind::Contender && CaptureReducer.IsActive())
    {
        const auto Captured=PublishCaptureMotion(Permit,*Batch);
        if (Captured!=ELDCommitReject::None) { return Captured; }
    }
    Life.MotionThroughSequence=Sequence;
    Life.MotionLastFoot=Batch->EndFootCm;
    Life.MotionGrounded=Batch->FinalSupport.Grounded;
    Life.MotionFloor=Batch->FinalSupport.Floor;
    if (Combat) { Combat->CaptureSkillMotion(Batch); }
    Life.NativeMotion.Add(MoveTemp(Batch));
    Life.CompletedMotionStep = Step.StepSerial;
    return ELDCommitReject::None;
}

bool ALDGameMode::AcknowledgeNativeMotion(const FLDMatchContext& Match, const FLDCombatBodyRef& Body,
    uint64 ThroughStep, uint64 ThroughSequence, FString& Error)
{
    auto* Life=Lives.Find(Body.Victim.EntityId);
    if (!HasAuthority() || Match!=MatchContext || !Life || Life->MotionBody!=Body)
    {
        Error=TEXT("Native motion acknowledgement has a stale match or stream body.");
        return false;
    }
    if (ThroughStep==Life->MotionAckStep && ThroughSequence==Life->MotionAckSequence) { return true; }
    const int32 End=Life->NativeMotion.IndexOfByPredicate([&](const auto& Batch)
    { return Batch->Step.StepSerial==ThroughStep && Batch->ThroughSequence==ThroughSequence; });
    if (ThroughStep<=Life->MotionAckStep || ThroughSequence<Life->MotionAckSequence || End==INDEX_NONE)
    {
        Error=TEXT("Native motion acknowledgement does not cover a retained complete prefix.");
        return false;
    }
    Life->NativeMotion.RemoveAt(0,End+1,EAllowShrinking::No);
    Life->MotionAckStep=ThroughStep;
    Life->MotionAckSequence=ThroughSequence;
    Error.Reset();
    return true;
}

bool ALDGameMode::PublishAuthorityClock(FString& Error)
{
    auto* Public=GetGameState<ALDGameState>();
    if (!HasAuthority() || !bAuthorityInitialized || bAuthorityStepOpen || !Public)
    {
        Error=TEXT("A completed authority clock requires the current initialized core and native GameState.");
        return false;
    }
    const FLDDemoClockView Candidate{MatchContext.Generation,AuthorityStep.StepSerial,AuthorityNowMs};
    const auto Result=LDMatchLifecycle::AcceptClockView(Candidate,Public->DemoClock,Error);
    if (Result==ELDClockProjectionResult::Malformed || Result==ELDClockProjectionResult::Stale)
    {
        if (Error.IsEmpty()) { Error=TEXT("Core attempted to publish an obsolete completed clock."); }
        return false;
    }
    if (Result==ELDClockProjectionResult::Accepted) { Public->ForceNetUpdate(); }
    Error.Reset();
    return true;
}

bool ALDGameMode::ReadObservedPublicFacts(const FLDMatchContext& Match, const FLDCombatBodyRef& Body,
    FLDObservedPublicFacts& Out, FString& Error) const
{
    ALDCharacter* Actor=nullptr;
    const auto Resolved=ResolveBody(Match,Body,Actor);
    const auto* Life=Lives.Find(Body.Victim.EntityId);
    const auto* State=Actor && Combat ? Combat->StateFor(Actor) : nullptr;
    if (!HasAuthority() || Resolved!=ELDCommitReject::None || !Life || Life->Phase!=ELDLifePhase::Alive
        || Life->Body!=Body || !State || !State->PublicFacts.IsSet() || !State->Resources.Revision || !State->AttributeRevision)
    {
        Error=TEXT("Observed public facts require a current living body with a committed resource/build projection.");
        return false;
    }
    FLDObservedPublicFacts Candidate=State->PublicFacts.GetValue();
    const uint8 Protection=uint8(ELDDemoStatusId::RespawnProtection);
    if (Candidate.Statuses.ContainsByPredicate([Protection](const auto& Status) { return Status.StatusId==Protection; }))
    {
        Error=TEXT("Respawn protection must be derived from Core life state, not a stored combat status.");
        return false;
    }
    // Root and Shield cues are derived from the live Combat state at this read, like protection.
    Combat->AppendObservedStatuses(*Actor,AuthorityNowMs,Candidate.Statuses);
    if (!Combat->AppendObservedAction(*Actor,AuthorityNowMs,Candidate,Error)) { return false; }
    if (Body.Victim.Kind==ELDVictimKind::Contender && AuthorityNowMs<Life->ProtectedUntilMs)
    {
        Candidate.Statuses.Add({Protection,1,Life->ProtectedUntilMs});
    }
    Out=MoveTemp(Candidate);
    Error.Reset();
    return true;
}

bool ALDGameMode::ReportAuthorityFault(const FLDMatchContext& Match, const FString& Diagnostic)
{
    if (!HasAuthority() || !IsDemoRoom() || !Match.IsValid() || Match!=MatchContext || Diagnostic.IsEmpty())
    {
        UE_LOG(LogLDMatch,Warning,TEXT("LD_AUTHORITY_FAULT_REPORT_REJECTED Invalid authority, match or diagnostic."));
        return false;
    }
    if (MatchPhase==ELDMatchPhase::Faulted) { return true; }
    if (MatchPhase==ELDMatchPhase::Results || MatchPhase==ELDMatchPhase::Closing)
    {
        UE_LOG(LogLDMatch,Warning,TEXT("LD_AUTHORITY_FAULT_REPORT_REJECTED The result or room is already closed."));
        return false;
    }
    FaultAuthority(Diagnostic);
    return true;
}

void ALDGameMode::FaultAuthority(const FString& Error)
{
    if (bResultLocked)
    {
        // A locked result is never replaced by a later fault: the room stays in Results.
        bAuthorityStepOpen = false;
        bSettlementStep = false;
        UE_LOG(LogLDMatch, Error, TEXT("LD_DEMO_AUTHORITY_FAULT_AFTER_RESULT %s"), *Error);
        return;
    }
    EntryFailure = Error;
    MatchPhase = ELDMatchPhase::Faulted;
    if (GateRevision != MAX_uint64) { ++GateRevision; }
    bAuthorityStepOpen = false;
    bSettlementStep = false;
    // A completion held for an owed reward never locks once the match faulted.
    PendingCapture.Reset();
    if (Vision) { Vision->EndMatch(); }
    for (auto& Pair : Lives)
    {
        if (auto* Body = Pair.Value.Actor.Get())
        {
            Body->GetCharacterMovement()->SetComponentTickEnabled(false);
            Body->GetCharacterMovement()->StopMovementImmediately();
        }
    }
    UE_LOG(LogLDMatch, Error, TEXT("LD_DEMO_AUTHORITY_FAULT %s"), *Error);
    PublishLobby();
    PublishPublicMatch();
}
