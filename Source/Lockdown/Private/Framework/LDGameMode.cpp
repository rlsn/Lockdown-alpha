#include "Framework/LDGameMode.h"
#include "Core/LDLog.h"
#include "Framework/LDDemoEntry.h"
#include "Framework/LDGameInstance.h"
#include "Framework/LDPlayerController.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDGrowthDirector.h"
#include "Development/LDDevelopmentDirector.h"
#include "Combat/LDCombatDirector.h"
#include "Region/LDMapRegistry.h"
#include "UI/LDHUDModel.h"
#include "Probes/LDDemoEntryProbe.h"
#include "Vision/LDVisionDirector.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/NetDriver.h"
#include "GameFramework/GameSession.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "UObject/UObjectGlobals.h"

ILDLifeAuthority* LDAuthority::Find(const UWorld* World)
{
    return World ? World->GetAuthGameMode<ALDGameMode>() : nullptr;
}

ALDGameMode::ALDGameMode()
{
    DefaultPawnClass=nullptr;
    PlayerControllerClass=ALDPlayerController::StaticClass();
    PlayerStateClass=ALDPlayerState::StaticClass();
    GameStateClass=ALDGameState::StaticClass();
    SpectatorClass=nullptr;
    SpawnRandom.Initialize(int32(FPlatformTime::Cycles()));
}

void ALDGameMode::InitGame(const FString& MapName,const FString& Options,FString& ErrorMessage)
{
    Super::InitGame(MapName,Options,ErrorMessage);
    if (!ErrorMessage.IsEmpty()) { EntryFailure=ErrorMessage; return; }
    const FString Package=UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName());
    if (!LDDemoEntry::ValidateSelection(Package,Options,ErrorMessage) || GetClass()!=ALDGameMode::StaticClass())
    {
        if (ErrorMessage.IsEmpty()) { ErrorMessage=TEXT("Retired GameMode override. Demo uses the native LDGameMode only."); }
        EntryFailure=ErrorMessage;
        UE_LOG(LogLDMatch,Error,TEXT("LD_DEMO_ENTRY_REJECTED %s"),*ErrorMessage);
        return;
    }
    bDemoWorld=Package==LDDemoEntry::WorldMap;
    if (!bDemoWorld)
    {
        bEntryAccepted=true;
        UE_LOG(LogLDMatch,Display,TEXT("LD_DEMO_FRONTEND"));
        return;
    }
    if (!LDDemoEntry::VerifyRuntimeFiles(ErrorMessage,&EntryCatalogDigest) || !LDDemoEntry::VerifyWorldPackages(ErrorMessage))
    {
        EntryFailure=ErrorMessage;
        UE_LOG(LogLDMatch,Error,TEXT("LD_DEMO_ENTRY_REJECTED %s"),*ErrorMessage);
        return;
    }
    auto* Instance=GetGameInstance<ULDGameInstance>();
    if (!Instance || !Instance->AllocateMatchGeneration(MatchContext.Generation,ErrorMessage))
    {
        if (ErrorMessage.IsEmpty()) { ErrorMessage=TEXT("Demo requires the native LDGameInstance."); }
        EntryFailure=ErrorMessage;
        return;
    }
    MatchContext.LedgerMatch=FGuid::NewGuid();
    MatchPhase=ELDMatchPhase::Lobby;
    RosterRevision=1;
    bEntryAccepted=true;
    HostTravelHandle=FCoreUObjectDelegates::PreLoadMapWithContext.AddUObject(this,&ALDGameMode::HostWorldUnloading);
    // The full roster never reallocates record storage that callers briefly hold.
    Contenders.Reserve(LDMatchLifecycle::MaxContenders);
    if (GameSession) { GameSession->MaxPlayers=LDMatchLifecycle::MaxContenders; }
    UE_LOG(LogLDMatch,Display,TEXT("LD_DEMO_LOBBY generation=%u"),MatchContext.Generation);
}

void ALDGameMode::RestartPlayer(AController* NewPlayer)
{
    if (!bEntryAccepted || !bDemoWorld || MatchPhase==ELDMatchPhase::Lobby) { return; }
    // PostLogin's engine restart precedes private proof; recovery rebinds the retained life instead of spawning.
    if (PendingReconnects.Contains(Cast<ALDPlayerController>(NewPlayer))) { return; }
    UE_LOG(LogLDMatch,Error,TEXT("LD_DEMO_SPAWN_REJECTED Body creation requires the committed Demo lifecycle."));
}

void ALDGameMode::StartPlay()
{
    if (!bEntryAccepted) { return; }
    Super::StartPlay();
    if (bDemoWorld)
    {
        auto* Registry=GetWorld()->GetSubsystem<ULDMapRegistry>();
        FString Error;
        if (!Registry || !Registry->Ensure(Error))
        {
            EntryFailure=Error.IsEmpty()?TEXT("Demo world registry is unavailable."):Error;
            MatchPhase=ELDMatchPhase::Faulted;
            UE_LOG(LogLDMatch,Error,TEXT("LD_DEMO_WORLD_ENTRY_REJECTED %s"),*EntryFailure);
        }
        PublishLobby();
        PublishPublicMatch();
#if WITH_DEV_AUTOMATION_TESTS
        LDDemoTests::StartFrontendSettingsProbe(*GetWorld());
#endif
    }
#if WITH_DEV_AUTOMATION_TESTS
    else { LDDemoTests::ProbeRejectedEntry(*GetWorld()); LDDemoTests::StartFrontendSettingsProbe(*GetWorld()); }
#endif
}

void ALDGameMode::PreLogin(const FString& Options,const FString& Address,const FUniqueNetIdRepl& UniqueId,FString& ErrorMessage)
{
    using namespace LDDemoEntry;
    // The Demo's own refusals run first and carry stable codes the client maps to its reason. The engine session's
    // capacity check (MaxPlayers) would otherwise answer a full room with an uncoded "Server full.".
    if (!IsDemoRoom())
    { ErrorMessage=MakeJoinRefusal(JoinRefusal::RoomNotReady,TEXT("this world is not an initialized Demo room.")); return; }
    if (!ValidateSelection(GetWorld()->GetOutermost()->GetName(),Options,ErrorMessage)) { return; }
#if WITH_DEV_AUTOMATION_TESTS
    // A remote client keeps its own content snapshot and cannot follow the Host's per-match reload.
    if (IsDevContentReload())
    { ErrorMessage=MakeJoinRefusal(JoinRefusal::ContentMismatch,TEXT("a DEV content hot reload room plays only the Host and AI.")); return; }
#endif
    const bool bResume=UGameplayStatics::ParseOption(Options,TEXT("LDReconnect"))==TEXT("1");
    bool bHasResume=false;
    if(bResume)
    {
        for(const auto& Pair:Contenders)
        {bHasResume|=Pair.Value.bDisconnected&&!Pair.Value.bLeft&&Pair.Value.ReconnectTicket.IsValid();}
    }
    if (MatchPhase!=ELDMatchPhase::Lobby&&!bHasResume)
    { ErrorMessage=MakeJoinRefusal(JoinRefusal::MatchAlreadyStarted,TEXT("mid-match entry requires an existing disconnected participant.")); return; }
    if(bResume&&!bHasResume)
    {ErrorMessage=MakeJoinRefusal(JoinRefusal::RoomNotReady,TEXT("no disconnected participant remains in this match."));return;}
    if (!bResume&&Contenders.Num()+LobbyAISlots.Num()>=LDMatchLifecycle::MaxContenders)
    { ErrorMessage=MakeJoinRefusal(JoinRefusal::RoomFull,TEXT("all eight contender slots are occupied.")); return; }
    if (UGameplayStatics::ParseOption(Options,TEXT("LDWire"))!=LexToString(WireProtocol))
    { ErrorMessage=MakeJoinRefusal(JoinRefusal::ProtocolMismatch,TEXT("use the same Demo network build as the Host.")); return; }
    FString Version;
    if (!ReadProductVersion(Version,ErrorMessage)) { return; }
    if (UGameplayStatics::ParseOption(Options,TEXT("LDProduct"))!=Version)
    { ErrorMessage=MakeJoinRefusal(JoinRefusal::ProductMismatch,TEXT("use the same product version as the Host.")); return; }
    if (EntryCatalogDigest.IsEmpty() || UGameplayStatics::ParseOption(Options,TEXT("LDCatalog"))!=EntryCatalogDigest)
    { ErrorMessage=MakeJoinRefusal(JoinRefusal::ContentMismatch,TEXT("use the same approved Demo content as the Host.")); return; }
    FString Name;
    if (!LDMatchLifecycle::ResolveContenderName(Options,Name,ErrorMessage)) { return; }
    Super::PreLogin(Options,Address,UniqueId,ErrorMessage);
}

FString ALDGameMode::InitNewPlayer(APlayerController* NewPlayerController,const FUniqueNetIdRepl& UniqueId,const FString& Options,const FString& Portal)
{
    FString Error=Super::InitNewPlayer(NewPlayerController,UniqueId,Options,Portal);
    if (!Error.IsEmpty() || !bEntryAccepted || !bDemoWorld) { return Error; }
    // The engine has just named the PlayerState after the online subsystem's nickname; the contender keeps the
    // name chosen in the frontend (?LDName=), or the explicit default when none was sent.
    FString Name;
    if (!LDMatchLifecycle::ResolveContenderName(Options,Name,Error)) { return Error; }
    ChangeName(NewPlayerController,Name,false);
    if(UGameplayStatics::ParseOption(Options,TEXT("LDReconnect"))==TEXT("1"))
    {PendingReconnects.Add(CastChecked<ALDPlayerController>(NewPlayerController));}
    return Error;
}

void ALDGameMode::PostLogin(APlayerController* NewPlayer)
{
    if (bEntryAccepted && !bDemoWorld) { Super::PostLogin(NewPlayer); return; }
    FString Reason;
    if(auto* Controller=Cast<ALDPlayerController>(NewPlayer);Controller&&PendingReconnects.Contains(Controller))
    {
        Super::PostLogin(NewPlayer);
        if(auto* Driver=GetWorld()->GetNetDriver())
        {
            FTimerHandle& Handle=PendingReconnects.FindChecked(Controller);
            GetWorldTimerManager().SetTimer(Handle,FTimerDelegate::CreateWeakLambda(this,[this,Weak=TWeakObjectPtr<ALDPlayerController>(Controller)]()
            {
                if(Weak.IsValid()&&PendingReconnects.Contains(Weak)){FString Failure;ResumeRoomMember(*Weak.Get(),FGuid(),Failure);}
            }),Driver->InitialConnectTimeout,false);
        }
        Controller->ClientReconnectChallenge();
        return;
    }
    if (!AdmitRoomMember(Cast<ALDPlayerController>(NewPlayer),Reason))
    {
        UE_LOG(LogLDMatch,Warning,TEXT("LD_DEMO_ADMISSION_REJECTED %s"),*Reason);
        if (NewPlayer && (!GameSession || !GameSession->KickPlayer(NewPlayer,FText::FromString(Reason)))) { NewPlayer->Destroy(); }
        return;
    }
    Super::PostLogin(NewPlayer);
    PublishLobby();
}

void ALDGameMode::Logout(AController* Exiting)
{
    auto* Controller=Cast<ALDPlayerController>(Exiting);
    auto* Record=Contenders.Find(Controller);
    if(Controller&&CanParkDisconnectedMember(*Controller))
    {
        FString Error;
        if(ParkDisconnectedMember(*Controller,Error)){Super::Logout(Exiting);return;}
        FaultAuthority(Error);
        Record=Contenders.Find(Controller);
    }
    if(FTimerHandle* Handle=PendingReconnects.Find(Controller)){GetWorldTimerManager().ClearTimer(*Handle);}
    PendingReconnects.Remove(Controller);
    if (Record && IsMemberPresent(Record->Session))
    {
        const FGuid Session=Record->Session;
        UE_LOG(LogLDMatch,Display,TEXT("LD_MEMBER_LEFT contender=%s name=%s generation=%u phase=%s t=%lld host=%d"),*LDLog::Id(Session),
            *LDLog::Quote(Record->Name),MatchContext.Generation,*LDLog::EnumName(MatchPhase),AuthorityNowMs,LobbyHost==Controller?1:0);
        if (LobbyHost==Controller) { CloseRoomForHostDeparture(Controller); }
        // An ordinary leave is not a death: no reward, no automatic win. The body leaves now,
        // the reducer receives the leave at its actual receipt instant and the result row stays.
        const bool InMatch=IsGameplayWriteOpen(MatchContext);
        const auto* LeavingLife=Lives.Find(Session);
        FLDLifeNotice Left;
        Left.Match=MatchContext;
        Left.Body=LeavingLife?LeavingLife->Body:FLDCombatBodyRef();
        Left.Change=ELDLifeChange::Left;
        Left.OccurredAtMs=AuthorityNowMs;
        RemoveLifeBinding(Session);
        // #198: a leaver is out of the rest sign-up and of every pending rest placement (locked PK entrants stay for D).
        RestSignups.Remove(Session);
        PendingPhasePlacement.Remove(Session);
        RestLives.Remove(Session);
        // #198 session D: a PK entrant that leaves forfeits (ALDPKDirector reads presence at its next judgement).
        PKLives.Remove(Session);
        PreviousFieldSpawns.Remove(Session);
        PendingFieldSpawns.Remove(Session);
        RetireTrainingDummy(Session);
        Record->State.bActive=false;
        if (MatchPhase!=ELDMatchPhase::Lobby) { Record->bLeft=true; }
        if (InMatch)
        {
            QueueLeaveFact(Session);
            if (const auto* RetiredLife=Lives.Find(Session)) { Left.LifeRevision=RetiredLife->Revision; }
            NotifyLifeCommitted(Left);
        }
        if (Vision) { Vision->UnregisterObserver(Session); }
        if (MatchPhase==ELDMatchPhase::Lobby)
        {
            Contenders.Remove(Controller);
            Lives.Remove(Session);
            RosterOrder.Remove(Session);
            FString Reason;
            if (!AdvanceRoster(Reason)) { EntryFailure=Reason; }
        }
        PublishLobby();
    }
    Super::Logout(Exiting);
}

void ALDGameMode::CloseRoomForHostDeparture(const ALDPlayerController* Host)
{
    if (!LobbyHost.IsValid()) { return; }
    UE_LOG(LogLDMatch,Display,TEXT("LD_DEMO_ROOM_CLOSED_BY_HOST generation=%u"),MatchContext.Generation);
    MatchPhase=ELDMatchPhase::Closing;
    if (GateRevision!=MAX_uint64) { ++GateRevision; }
    if (Vision) { Vision->EndMatch(); }
    for (const auto& Pair : Contenders)
    {
        if (auto* Member=Pair.Key.Get(); Member && Member!=Host)
        { Member->ClientDemoRoomClosed(MatchContext.Generation,TEXT("房主已离开，房间已关闭（不支持更换房主）。")); }
    }
    LobbyHost.Reset();
}

void ALDGameMode::HostWorldUnloading(const FWorldContext& Context,const FString& Map)
{
    // Only this Listen Host's own map change; the reliable notices go out with the engine's final flush of each
    // connection, ahead of its close.
    const ALDPlayerController* Host=LobbyHost.Get();
    if (Context.World()!=GetWorld() || GetNetMode()!=NM_ListenServer || !Host || !Host->IsLocalController()) { return; }
    CloseRoomForHostDeparture(Host);
}

void ALDGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
    FCoreUObjectDelegates::PreLoadMapWithContext.Remove(HostTravelHandle);
    HostTravelHandle.Reset();
    Super::EndPlay(Reason);
}

FLDOwnerState* ALDGameMode::StateFor(ALDPlayerController* PC)
{
    auto* Record=Contenders.Find(PC);
    return Record?&Record->State:nullptr;
}

FGuid ALDGameMode::ContenderId(ALDPlayerController* PC) const
{
    const auto* Record=Contenders.Find(PC);
    return Record?Record->Session:FGuid();
}

void ALDGameMode::SyncOwner(ALDPlayerController* PC)
{
    // Owner views stop before any close releases bodies or resource records. Teardown begins before the
    // net driver destroys remote pawns; a closed room or a retiring match publishes nothing either. These
    // paths therefore cannot report a projection fault, while every playing phase keeps the full check.
    const UWorld* World = GetWorld();
    if (!World || World->bIsTearingDown || MatchPhase == ELDMatchPhase::Closing || MatchPhase == ELDMatchPhase::Resetting)
    { return; }
    if (auto* State=StateFor(PC))
    {
        if (State->Revision==MAX_uint64)
        {
            MatchPhase=ELDMatchPhase::Faulted;
            UE_LOG(LogLDMatch,Error,TEXT("LD_OWNER_REVISION_EXHAUSTED"));
            return;
        }
        FString Error;
        if (IsDemoRoom() && !ProjectLife(*PC,*State,Error))
        {
            if (MatchPhase!=ELDMatchPhase::Faulted) { FaultAuthority(Error); }
            else { UE_LOG(LogLDMatch,Warning,TEXT("LD_OWNER_PROJECTION_REJECTED %s"),*Error); }
            return;
        }
        State->Match=MatchContext.Generation;
        State->bRestReady=IsRestReady(State->ContenderId);
        State->bCanRestReady=CanRestReady(State->ContenderId);
        // P6: the death screen's spawn choice.
        State->Pacing=FLDOwnerPacingView();
        if (const FLDSpawnChoice* Choice=SpawnChoices.Find(State->ContenderId); Choice && State->LifePhase==ELDLifePhase::Dead)
        {
            const ULDMapRegistry* Registry=World->GetSubsystem<ULDMapRegistry>();
            State->Pacing.SpawnLife=Choice->Life;
            State->Pacing.SpawnCandidates=Choice->Candidates;
            for (const FName& Id : Choice->Candidates)
            {
                const FLDMapBinding* Binding=Registry ? Registry->Resolve(Id,Registry->Generation) : nullptr;
                State->Pacing.SpawnCandidateCm.Add(Binding ? Binding->Position : FVector::ZeroVector);
            }
            State->Pacing.SpawnChosen=int8(Choice->Chosen);
            State->Pacing.SpawnChoiceEndsAtMs=Choice->DeadlineMs;
        }
        // Growth: the owner's level, XP and offer in the same publication.
        State->Growth=Growth?Growth->ReadOwnerView(State->ContenderId):FLDGrowthOwnerView();
        ++State->Revision;
        PC->OwnerState=*State;
#if WITH_DEV_AUTOMATION_TESTS
        PC->bDevPeaceful=IsDevPeaceful(State->ContenderId);
#endif
        if (PC->IsLocalController()) { PC->OnRep_OwnerState(); }
        PC->ForceNetUpdate();
    }
}

bool ALDGameMode::IsFinished() const
{ return MatchPhase==ELDMatchPhase::Results || MatchPhase==ELDMatchPhase::Closing || MatchPhase==ELDMatchPhase::Faulted; }

bool ALDGameMode::IsPlaying() const
{ return IsGameplayWriteOpen(MatchContext); }
