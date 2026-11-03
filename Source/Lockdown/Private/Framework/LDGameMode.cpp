#include "Framework/LDGameMode.h"
#include "Framework/LDDemoEntry.h"
#include "Framework/LDGameInstance.h"
#include "Framework/LDPlayerController.h"
#include "Economy/LDEconomyDirector.h"
#include "Region/LDMapRegistry.h"
#include "Tests/LDDemoEntryProbe.h"
#include "Vision/LDVisionDirector.h"
#include "Engine/World.h"
#include "GameFramework/GameSession.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"

ALDPlayerState::ALDPlayerState() { bOnlyRelevantToOwner=true; bAlwaysRelevant=false; }

void ALDGameState::OnRep_DemoLobby()
{
    FString Error;
    const auto Result=LDMatchLifecycle::AcceptLobbyView(DemoLobby,LatestDemoLobby,Error);
    if (Result==ELDLobbyProjectionResult::Accepted) { return; }
    DemoLobby=LatestDemoLobby;
    if (Result==ELDLobbyProjectionResult::Malformed)
    { UE_LOG(LogTemp,Warning,TEXT("LD_LOBBY_PROJECTION_REJECTED %s"),*Error); }
    else { UE_LOG(LogTemp,Verbose,TEXT("LD_LOBBY_PROJECTION_STALE")); }
}

void ALDGameState::OnRep_DemoClock()
{
    FString Error;
    const auto Result=LDMatchLifecycle::AcceptClockView(DemoClock,LatestDemoClock,Error);
    if (Result==ELDClockProjectionResult::Accepted) { return; }
    DemoClock=LatestDemoClock;
    if (Result==ELDClockProjectionResult::Malformed)
    { UE_LOG(LogTemp,Warning,TEXT("LD_CLOCK_PROJECTION_REJECTED %s"),*Error); }
}

void ALDGameState::OnRep_PublicMatch()
{
    FString Error;
    const auto Result=LDCapture::AcceptPublicMatchView(PublicMatch,LatestPublicMatch,Error);
    if (Result==ELDLobbyProjectionResult::Accepted) { return; }
    PublicMatch=LatestPublicMatch;
    if (Result==ELDLobbyProjectionResult::Malformed)
    { UE_LOG(LogTemp,Warning,TEXT("LD_PUBLIC_MATCH_REJECTED %s"),*Error); }
}

void ALDGameState::OnRep_MatchResult()
{
    FString Error;
    const auto Result=LDCapture::AcceptResultView(MatchResult,LatestMatchResult,Error);
    if (Result==ELDLobbyProjectionResult::Accepted) { return; }
    MatchResult=LatestMatchResult;
    if (Result==ELDLobbyProjectionResult::Malformed)
    { UE_LOG(LogTemp,Warning,TEXT("LD_MATCH_RESULT_REJECTED %s"),*Error); }
}

void ALDGameState::OnRep_KillFeed()
{
    FString Error;
    const auto Result=LDCapture::AcceptKillFeed(KillFeed,LatestKillFeed,Error);
    if (Result==ELDLobbyProjectionResult::Accepted) { return; }
    KillFeed=LatestKillFeed;
    if (Result==ELDLobbyProjectionResult::Malformed)
    { UE_LOG(LogTemp,Warning,TEXT("LD_KILL_FEED_REJECTED %s"),*Error); }
}

void ALDGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ALDGameState,DemoLobby);
    DOREPLIFETIME(ALDGameState,DemoClock);
    DOREPLIFETIME(ALDGameState,PublicMatch);
    DOREPLIFETIME(ALDGameState,MatchResult);
    DOREPLIFETIME(ALDGameState,KillFeed);
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
        UE_LOG(LogTemp,Error,TEXT("LD_DEMO_ENTRY_REJECTED %s"),*ErrorMessage);
        return;
    }
    bDemoWorld=Package==LDDemoEntry::WorldMap;
    if (!bDemoWorld)
    {
        bEntryAccepted=true;
        UE_LOG(LogTemp,Display,TEXT("LD_DEMO_FRONTEND"));
        return;
    }
    if (!LDDemoEntry::VerifyRuntimeFiles(ErrorMessage,&EntryCatalogDigest) || !LDDemoEntry::VerifyWorldPackages(ErrorMessage))
    {
        EntryFailure=ErrorMessage;
        UE_LOG(LogTemp,Error,TEXT("LD_DEMO_ENTRY_REJECTED %s"),*ErrorMessage);
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
    // Three humans plus three AI never reallocate record storage that callers briefly hold.
    Contenders.Reserve(6);
    if (GameSession) { GameSession->MaxPlayers=3; }
    UE_LOG(LogTemp,Display,TEXT("LD_DEMO_LOBBY generation=%u"),MatchContext.Generation);
}

void ALDGameMode::RestartPlayer(AController* NewPlayer)
{
    if (!bEntryAccepted || !bDemoWorld || MatchPhase==ELDMatchPhase::Lobby) { return; }
    UE_LOG(LogTemp,Error,TEXT("LD_DEMO_SPAWN_REJECTED Body creation requires the committed Demo lifecycle."));
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
            UE_LOG(LogTemp,Error,TEXT("LD_DEMO_WORLD_ENTRY_REJECTED %s"),*EntryFailure);
        }
        PublishLobby();
        PublishPublicMatch();
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
    if (MatchPhase!=ELDMatchPhase::Lobby)
    { ErrorMessage=MakeJoinRefusal(JoinRefusal::MatchAlreadyStarted,TEXT("mid-match joining and reconnecting are not supported.")); return; }
    if (Contenders.Num()>=3)
    { ErrorMessage=MakeJoinRefusal(JoinRefusal::RoomFull,TEXT("the room already has three human players, including the Host.")); return; }
    if (UGameplayStatics::ParseOption(Options,TEXT("LDWire"))!=LexToString(WireProtocol))
    { ErrorMessage=MakeJoinRefusal(JoinRefusal::ProtocolMismatch,TEXT("use the same Demo network build as the Host.")); return; }
    FString Version;
    if (!ReadProductVersion(Version,ErrorMessage)) { return; }
    if (UGameplayStatics::ParseOption(Options,TEXT("LDProduct"))!=Version)
    { ErrorMessage=MakeJoinRefusal(JoinRefusal::ProductMismatch,TEXT("use the same product version as the Host.")); return; }
    if (EntryCatalogDigest.IsEmpty() || UGameplayStatics::ParseOption(Options,TEXT("LDCatalog"))!=EntryCatalogDigest)
    { ErrorMessage=MakeJoinRefusal(JoinRefusal::ContentMismatch,TEXT("use the same approved Demo content as the Host.")); return; }
    Super::PreLogin(Options,Address,UniqueId,ErrorMessage);
}

void ALDGameMode::PostLogin(APlayerController* NewPlayer)
{
    if (bEntryAccepted && !bDemoWorld) { Super::PostLogin(NewPlayer); return; }
    FString Reason;
    if (!AdmitRoomMember(Cast<ALDPlayerController>(NewPlayer),Reason))
    {
        UE_LOG(LogTemp,Warning,TEXT("LD_DEMO_ADMISSION_REJECTED %s"),*Reason);
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
    if (Record && IsMemberPresent(Record->Session))
    {
        const FGuid Session=Record->Session;
        if (LobbyHost==Controller)
        {
            MatchPhase=ELDMatchPhase::Closing;
            if (GateRevision!=MAX_uint64) { ++GateRevision; }
            if (Vision) { Vision->EndMatch(); }
            for (const auto& Pair : Contenders)
            {
                if (auto* Member=Pair.Key.Get(); Member && Member!=Controller)
                { Member->ClientDemoRoomClosed(MatchContext.Generation,TEXT("The Host left the room. Host migration is not supported.")); }
            }
            LobbyHost.Reset();
        }
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
            UE_LOG(LogTemp,Error,TEXT("LD_OWNER_REVISION_EXHAUSTED"));
            return;
        }
        FString Error;
        if (IsDemoRoom() && !ProjectLife(*PC,*State,Error))
        {
            if (MatchPhase!=ELDMatchPhase::Faulted) { FaultAuthority(Error); }
            else { UE_LOG(LogTemp,Warning,TEXT("LD_OWNER_PROJECTION_REJECTED %s"),*Error); }
            return;
        }
        State->Match=MatchContext.Generation;
        // Growth/Shop: the owner's gold, equipment, level, offer and shop session in the same publication.
        State->Growth=Economy?Economy->ReadOwnerView(State->ContenderId):FLDGrowthOwnerView();
        ++State->Revision;
        PC->OwnerState=*State;
        if (PC->IsLocalController()) { PC->OnRep_OwnerState(); }
        PC->ForceNetUpdate();
    }
}

bool ALDGameMode::IsFinished() const
{ return MatchPhase==ELDMatchPhase::Results || MatchPhase==ELDMatchPhase::Closing || MatchPhase==ELDMatchPhase::Faulted; }

bool ALDGameMode::IsPlaying() const
{ return IsGameplayWriteOpen(MatchContext); }
