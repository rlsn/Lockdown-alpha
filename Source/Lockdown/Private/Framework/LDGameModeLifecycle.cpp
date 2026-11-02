#include "Framework/LDGameMode.h"
#include "Framework/LDDemoEntry.h"
#include "Framework/LDPlayerController.h"
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Combat/LDCombatDirector.h"
#include "Components/CapsuleComponent.h"
#include "Development/LDDemoContentCatalog.h"
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
    auto RetirePreparedDomains=[&]
    {
        if (Vision) { Vision->Destroy(); Vision=nullptr; }
        if (Combat) { Combat->Destroy(); Combat=nullptr; }
        bAuthorityInitialized=false;
    };
    if (!Vision || !Vision->BeginMatch(MatchContext,Error))
    {
        if (Error.IsEmpty()) { Error=TEXT("The current observation owner could not initialize."); }
        RetirePreparedDomains();
        return false;
    }
    for (const auto& Pair : Contenders)
    {
        if (!IsMemberPresent(Pair.Value.Session)) { continue; }
        if (!Vision->RegisterObserver(Pair.Value.Session,Pair.Key.Get(),Error))
        {
            RetirePreparedDomains();
            return false;
        }
    }
    bAuthorityInitialized = true;
    if (!PublishAuthorityClock(Error))
    {
        RetirePreparedDomains();
        return false;
    }
    return true;
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

bool ALDGameMode::ExecuteAuthorityPhase(ALDCombatDirector& Driver, ELDCommitPhase Phase,
    TFunctionRef<bool(const FLDGameplayWritePermit&)> Execute, FString& Error)
{
    const bool Initializing = MatchPhase == ELDMatchPhase::Starting && Phase == ELDCommitPhase::RespawnsAndWorld;
    if (Combat != &Driver || !HasAuthority() || ActiveWritePermit || (!bAuthorityStepOpen && !Initializing))
    {
        Error = TEXT("Authority phase has no driver-owned step or attempted a reentrant write.");
        return false;
    }
    if (!Initializing && ((Phase != ELDCommitPhase::Motion && Phase != ELDCommitPhase::Actions)
        || (Phase == ELDCommitPhase::Motion && bAuthorityPhaseExecuted)
        || (Phase == ELDCommitPhase::Actions && (!bAuthorityPhaseExecuted || LastAuthorityPhase != ELDCommitPhase::Motion))))
    {
        Error = TEXT("This executable core slice supports ordered Motion then Actions, not unimplemented provider phases.");
        return false;
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
    Life.MotionThroughSequence=Sequence;
    Life.MotionLastFoot=Batch->EndFootCm;
    Life.MotionGrounded=Batch->FinalSupport.Grounded;
    Life.MotionFloor=Batch->FinalSupport.Floor;
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

bool ALDGameMode::FinishAuthorityStep(ALDCombatDirector& Driver, FString& Error)
{
    if (Combat != &Driver || !bAuthorityStepOpen || ActiveWritePermit || !IsGameplayWriteOpen(MatchContext)
        || !bAuthorityPhaseExecuted || LastAuthorityPhase != ELDCommitPhase::Actions)
    {
        Error = TEXT("Cannot finish a missing, active or closed authority step.");
        return false;
    }
    for (const auto& Pair : Lives)
    {
        if (Pair.Value.Phase == ELDLifePhase::Alive && Pair.Value.CompletedMotionStep != AuthorityStep.StepSerial)
        {
            Error = TEXT("A registered live body has not completed native motion for this step.");
            return false;
        }
    }
    AuthorityNowSeconds = AuthorityStep.EndServerSeconds;
    AuthorityNowMs = AuthorityBoundary.AtMs;
    bAuthorityStepOpen = false;
    return PublishAuthorityClock(Error);
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

bool ALDGameMode::SpawnInitialContenders(const FLDGameplayWritePermit& Permit, FString& Error)
{
    if (ValidateWritePermit(Permit, ELDCommitPhase::RespawnsAndWorld) != ELDCommitReject::None
        || MatchPhase!=ELDMatchPhase::Starting || !DemoContent || !Combat)
    {
        Error=TEXT("Initial spawn requires the current Starting permit and validated core providers.");
        return false;
    }
    auto* Registry=GetWorld()->GetSubsystem<ULDMapRegistry>();
    FCollisionShape Capsule;
    if (!Registry || !ULDMapRegistry::ContentCapsule(*DemoContent,Capsule,Error))
    {
        if (Error.IsEmpty()) { Error=TEXT("Initial spawn has no current world/capsule."); }
        return false;
    }
    TArray<FGuid> Order=RosterOrder;
    Order.RemoveAll([&](const FGuid& Id) { return !IsMemberPresent(Id); });
    Order.Sort();
    if (Order.IsEmpty() || Order.Num()>6)
    {
        Error=TEXT("Initial spawn requires a bounded registered contender roster.");
        return false;
    }
    struct FPrepared
    {
        FGuid Contender;
        FGuid MotionStream;
        TWeakObjectPtr<ALDPlayerController> Controller;
        TWeakObjectPtr<ALDCharacter> Body;
        TSharedPtr<const FLDSpawnReservation> Reservation;
        FLDBodyCombatState CombatState;
    };
    TArray<FPrepared> Prepared;
    Prepared.Reserve(Order.Num());
    bool CommitStarted=false;
    ON_SCOPE_EXIT
    {
        if (!CommitStarted)
        {
            for (auto& Entry : Prepared)
            {
                if (auto* Body=Entry.Body.Get()) { Body->Destroy(); }
                if (Entry.Reservation)
                {
                    FString CleanupError;
                    if (!Registry->ReleaseSpawnReservation(Entry.Reservation,CleanupError))
                    { UE_LOG(LogTemp,Error,TEXT("LD_SPAWN_PREPARE_CLEANUP_FAILED %s"),*CleanupError); }
                }
            }
        }
    };
    for (const auto& Id : Order)
    {
        ALDPlayerController* Controller=nullptr;
        for (const auto& Pair : Contenders) { if (Pair.Value.Session==Id) { Controller=Pair.Key.Get(); break; } }
        auto* Life=Lives.Find(Id);
        if (!Controller || !Life || Life->Life!=0 || Life->Phase!=ELDLifePhase::Unspawned || Controller->GetPawn())
        {
            Error=TEXT("The initial roster contains a stale, already spawned or unbound contender.");
            return false;
        }
        if (Life->SpawnForLife!=1)
        {
            if (Life->SpawnSequence==MAX_uint64)
            { Error=TEXT("Spawn request sequence exhausted."); return false; }
            ++Life->SpawnSequence;
            Life->SpawnForLife=1;
        }
        const FLDSpawnRequestKey Request{MatchContext,Id,1,Life->SpawnSequence};
        TSharedPtr<const FLDSpawnSelection> Selection;
        auto& Entry=Prepared.Emplace_GetRef();
        Entry.Contender=Id;
        Entry.Controller=Controller;
        if (!Registry->DrawSpawnAnchor(Request,SpawnRandom,Permit,Selection,Error)
            || !Registry->ReserveSpawnPlacement(Selection,Capsule,Permit,Entry.Reservation,Error)) { return false; }
        const FTransform Transform(FRotator::ZeroRotator,Entry.Reservation->BodyOrigin());
        auto* Body=GetWorld()->SpawnActorDeferred<ALDCharacter>(ALDCharacter::StaticClass(),Transform,
            Controller,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!Body) { Error=TEXT("Prepared initial body allocation failed."); return false; }
        Entry.Body=Body;
        Body->SetReplicates(false);
        Body->SetActorHiddenInGame(true);
        Body->SetActorTickEnabled(false);
        if (!Body->ConfigureDemoMovement(*DemoContent,Error)) { return false; }
        Body->FinishSpawning(Transform);
        if (!IsValid(Body)) { Error=TEXT("Prepared initial body failed native construction."); return false; }
        auto* Movement=Body->GetCharacterMovement();
        Movement->SetComponentTickEnabled(false);
        Movement->SetMovementMode(MOVE_Walking);
        Movement->FindFloor(Body->GetActorLocation(),Movement->CurrentFloor,false,nullptr);
        Movement->SetBaseFromFloor(Movement->CurrentFloor);
        if (!Movement->CurrentFloor.IsWalkableFloor()
            || Movement->CurrentFloor.HitResult.GetComponent()!=Entry.Reservation->Ground().Floor.Get()
            || !Body->GetActorLocation().Equals(Entry.Reservation->BodyOrigin(),.000001))
        { Error=TEXT("Prepared body does not occupy its exact registered placement."); return false; }
        const FLDVictimKey Victim{ELDVictimKind::Contender,Id,1};
        if (!Combat->PrepareInitialBody(*Body,Victim,*DemoContent,Permit,Entry.CombatState,Error)) { return false; }
        Entry.MotionStream=FGuid::NewGuid();
        if (!Entry.MotionStream.IsValid() || !Entry.CombatState.Identity.IsValid())
        { Error=TEXT("Prepared body could not reserve valid native identities."); return false; }
    }
    Combat->ReserveInitialBodyBatch(Prepared.Num());
    for (const auto& Entry : Prepared)
    {
        const auto* Life=Lives.Find(Entry.Contender);
        const auto* Controller=Entry.Controller.Get();
        if (ValidateWritePermit(Permit,ELDCommitPhase::RespawnsAndWorld)!=ELDCommitReject::None
            || !Life || Life->Life!=0 || Life->Phase!=ELDLifePhase::Unspawned
            || !Controller || Controller->GetPawn() || !Entry.Body.IsValid()
            || !Registry->ValidateSpawnReservation(Entry.Reservation,Permit,Entry.Body.Get(),Error))
        {
            if (Error.IsEmpty()) { Error=TEXT("Prepared initial batch became stale before commit."); }
            return false;
        }
    }
    CommitStarted=true;
    for (auto& Entry : Prepared)
    {
        auto* Body=Entry.Body.Get();
        FLDCombatBodyRef Ref;
        if (!Combat->InstallInitialBody(*Body,MoveTemp(Entry.CombatState),Permit,Ref,Error))
        { FaultAuthority(Error); return false; }
        auto& Life=Lives.FindChecked(Entry.Contender);
        Life.Life=1;
        Life.Body=Life.MotionBody=Ref;
        Life.Actor=Body;
        Life.Phase=ELDLifePhase::Alive;
        Life.Revision=1;
        Life.ProtectedUntilMs=0;
        Life.MotionStreamId=Entry.MotionStream;
        Life.SpawnAnchor=Entry.Reservation->AnchorId();
        Life.SpawnOffset=Entry.Reservation->OffsetIndex();
        Life.MotionLastFoot=Body->GetCharacterMovement()->GetActorFeetLocation();
        Life.MotionGrounded=true;
        Life.MotionFloor=Body->GetCharacterMovement()->CurrentFloor.HitResult.GetComponent();
    }
    for (auto& Entry : Prepared)
    {
        auto* Body=Entry.Body.Get();
        auto* Controller=Entry.Controller.Get();
        Body->SetReplicates(true);
        Controller->Possess(Body);
        if (!IsValid(Body) || Controller->GetPawn()!=Body
            || !Registry->FinalizeSpawnPlacement(Entry.Reservation,Permit,Error))
        {
            if (Error.IsEmpty()) { Error=TEXT("Committed initial batch failed native possession/finalization."); }
            FaultAuthority(Error);
            return false;
        }
        Body->SetActorHiddenInGame(false);
        Body->SetActorTickEnabled(true);
        SyncOwner(Controller);
        if (!Controller->IsLocalController()) { Controller->Navigation->ResetLife(); }
    }
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
    if (Body.Victim.Kind==ELDVictimKind::Contender && AuthorityNowMs<Life->ProtectedUntilMs)
    {
        Candidate.Statuses.Add({Protection,1,Life->ProtectedUntilMs});
    }
    Out=MoveTemp(Candidate);
    Error.Reset();
    return true;
}

bool ALDGameMode::ProjectLife(ALDPlayerController& Controller, FLDOwnerState& Out, FString& Error) const
{
    const auto* Record = Contenders.Find(&Controller);
    const auto* Life = Record ? Lives.Find(Record->Session) : nullptr;
    if (!Record || !Life)
    {
        Error=TEXT("The owner has no current contender/life projection.");
        return false;
    }
    const auto* State=Combat && Life->Actor.IsValid()?Combat->StateFor(Life->Actor.Get()):nullptr;
    if (Life->Phase==ELDLifePhase::Alive
        && (!State || !Life->Body.IsValid() || State->Identity!=Life->Body.BodyIdentity
            || State->Victim!=Life->Body.Victim || !State->Resources.Revision))
    {
        Error=TEXT("A living owner has no matching committed resource/body projection.");
        return false;
    }
    FLDOwnerState Candidate=Out;
    Candidate.ContenderId=Record->Session;
    Candidate.Life=Life->Life;
    Candidate.LifePhase=Life->Phase;
    Candidate.LifeRevision=Life->Revision;
    Candidate.BodyRef=Life->Body;
    Candidate.bActive=Life->Phase==ELDLifePhase::Alive;
    Candidate.RespawnAtMs=Life->RespawnAtMs;
    Candidate.ProtectedUntilMs=Life->ProtectedUntilMs;
    if (State)
    {
        Candidate.Hp=double(State->Resources.HpP2)/100.;
        Candidate.MaxHp=double(State->Resources.MaxHpP2)/100.;
        Candidate.Mp=double(State->Resources.MpP2)/100.;
        Candidate.MaxMp=double(State->Resources.MaxMpP2)/100.;
        Candidate.Barrier=double(State->Resources.ShieldP2)/100.;
    }
    else { Candidate.Hp=Candidate.Mp=Candidate.Barrier=0; }
    Out=MoveTemp(Candidate);
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
    // Native disconnect may already have destroyed the Pawn before Logout.
    // Retire Combat ownership by its stable binding, not weak-pointer liveness.
    if (Combat && Life->Body.IsValid()) { Combat->RemoveRegisteredBody(Life->Body); }
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

bool ALDGameMode::ReportAuthorityFault(const FLDMatchContext& Match, const FString& Diagnostic)
{
    if (!HasAuthority() || !IsDemoRoom() || !Match.IsValid() || Match!=MatchContext || Diagnostic.IsEmpty())
    {
        UE_LOG(LogTemp,Warning,TEXT("LD_AUTHORITY_FAULT_REPORT_REJECTED Invalid authority, match or diagnostic."));
        return false;
    }
    if (MatchPhase==ELDMatchPhase::Faulted) { return true; }
    if (MatchPhase==ELDMatchPhase::Results || MatchPhase==ELDMatchPhase::Closing)
    {
        UE_LOG(LogTemp,Warning,TEXT("LD_AUTHORITY_FAULT_REPORT_REJECTED The result or room is already closed."));
        return false;
    }
    FaultAuthority(Diagnostic);
    return true;
}

void ALDGameMode::FaultAuthority(const FString& Error)
{
    EntryFailure = Error;
    MatchPhase = ELDMatchPhase::Faulted;
    if (GateRevision != MAX_uint64) { ++GateRevision; }
    bAuthorityStepOpen = false;
    if (Vision) { Vision->EndMatch(); }
    for (auto& Pair : Lives)
    {
        if (auto* Body = Pair.Value.Actor.Get())
        {
            Body->GetCharacterMovement()->SetComponentTickEnabled(false);
            Body->GetCharacterMovement()->StopMovementImmediately();
        }
    }
    UE_LOG(LogTemp, Error, TEXT("LD_DEMO_AUTHORITY_FAULT %s"), *Error);
    PublishLobby();
}
