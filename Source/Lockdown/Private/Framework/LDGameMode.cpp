#include "Framework/LDGameMode.h"
#include "Framework/LDDemoEntry.h"
#include "Region/LDMapRegistry.h"
#include "Tests/LDDemoEntryProbe.h"
#include "Region/LDMapDefinition.h"
#include "Population/LDB92Roster.h"
#include "Development/LDV08Loadout.h"
#include "World/LDWorldDirector.h"
#include "Combat/LDTacticalCatalog.h"
#include "Inventory/LDEquipmentCatalog.h"
#include "Inventory/LDAcquisitionDirector.h"
#include "Combat/LDTacticalRules.h"
#include "Economy/LDEconomyDirector.h"
#include "Economy/LDBankDirector.h"
#include "Economy/LDCommerceDirector.h"
#include "Framework/LDPlayerController.h"
#include "Character/LDCharacter.h"
#include "Navigation/LDNavigationComponent.h"
#include "Region/LDRegionNavBounds.h"
#include "Engine/World.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "EngineUtils.h"
#include "Vision/LDVisionDirector.h"
#include "Framework/LDGameInstance.h"
#include "Net/UnrealNetwork.h"
#include "Combat/LDCombatDirector.h"
#include "Core/LDRules.h"
#include "Objectives/LDObjectiveDirector.h"
#include "Objectives/LDObjectiveRules.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameSession.h"
#include "Development/LDDevelopmentDirector.h"
#include "Region/LDRegionDefinition.h"
#include "Inventory/LDInventoryDirector.h"
#include "Economy/LDProductionDirector.h"

ALDPlayerState::ALDPlayerState() { bOnlyRelevantToOwner = true; bAlwaysRelevant = false; }
void ALDGameState::OnRep_DemoLobby()
{
    FString Error;
    const auto Result = LDMatchLifecycle::AcceptLobbyView(DemoLobby, LatestDemoLobby, Error);
    if (Result == ELDLobbyProjectionResult::Accepted) { return; }
    DemoLobby = LatestDemoLobby;
    if (Result == ELDLobbyProjectionResult::Malformed)
    { UE_LOG(LogTemp, Warning, TEXT("LD_LOBBY_PROJECTION_REJECTED %s"), *Error); }
    else { UE_LOG(LogTemp, Verbose, TEXT("LD_LOBBY_PROJECTION_STALE")); }
}

void ALDGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    DOREPLIFETIME(ALDGameState,DemoLobby);
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(ALDGameState,bPublicSensorActive);
    DOREPLIFETIME(ALDGameState,DefensiveFlags);DOREPLIFETIME(ALDGameState,bClaimActive);DOREPLIFETIME(ALDGameState,bContested);
    DOREPLIFETIME(ALDGameState,ClaimRoute);DOREPLIFETIME(ALDGameState,ClaimProgress);DOREPLIFETIME(ALDGameState,MatchDeadline);
    DOREPLIFETIME(ALDGameState,EndReason);DOREPLIFETIME(ALDGameState,WinningRoute);DOREPLIFETIME(ALDGameState,MatchEndedAt);
    DOREPLIFETIME(ALDGameState,ResultReceipt);DOREPLIFETIME(ALDGameState,ResultSerial);DOREPLIFETIME(ALDGameState,SealNoticeSerial);
    DOREPLIFETIME(ALDGameState,SealNoticeKind);DOREPLIFETIME(ALDGameState,SealNoticeDistrict);
    DOREPLIFETIME(ALDGameState,bDevelopmentProfile);DOREPLIFETIME(ALDGameState,bMatchStarted);
    DOREPLIFETIME(ALDGameState,bUIControlProfile);
    DOREPLIFETIME(ALDGameState,bEnvironmentProfile);
    DOREPLIFETIME(ALDGameState,EconomyNextTick);DOREPLIFETIME(ALDGameState,ShortagePhase);DOREPLIFETIME(ALDGameState,ShortageRemaining);
    DOREPLIFETIME(ALDGameState,CoreOrdersRemaining);DOREPLIFETIME(ALDGameState,SupplyOrdersRemaining);DOREPLIFETIME(ALDGameState,Breaches);DOREPLIFETIME(ALDGameState,CharterWindow);DOREPLIFETIME(ALDGameState,CharterPhase);DOREPLIFETIME(ALDGameState,CharterWarningAt);DOREPLIFETIME(ALDGameState,CharterOpenAt);DOREPLIFETIME(ALDGameState,CharterCloseAt);DOREPLIFETIME(ALDGameState,OutcropOne);DOREPLIFETIME(ALDGameState,OutcropTwo);
    DOREPLIFETIME(ALDGameState,LobbyCount);DOREPLIFETIME(ALDGameState,ReadyCount);
    DOREPLIFETIME(ALDGameState,PublicSensorOfflineUntil);DOREPLIFETIME(ALDGameState,PublicSensorLockoutUntil);
}
void ALDGameState::BeginPlay()
{
    Super::BeginPlay();
    if (UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName()) == LDDemoEntry::WorldMap) { return; }
    if(const auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();Mode&&Mode->IsEnvironmentProfile()){return;}
    // The historical sandbox's BSP-only bounds disappear in a cook. Reuse the
    // established noncolliding envelope, on both authority and owning clients,
    // without modifying its frozen saved map or rebuilding valid Editor bounds.
    if(GetWorld()->GetMapName().Contains(TEXT("L_MigrationSandbox")))
    {
        bool HasBounds=false;
        for(TActorIterator<ANavMeshBoundsVolume> It(GetWorld());It;++It)
        {const FVector Size=It->GetComponentsBoundingBox(true).GetSize();HasBounds|=Size.X>100&&Size.Y>100&&Size.Z>100;}
        if(!HasBounds)
        {
            auto* Bounds=GetWorld()->SpawnActor<ALDRegionNavBounds>(FVector(0,0,200),FRotator::ZeroRotator);
            Bounds->Configure(FVector(4800,3200,800));
            if(auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
            {Nav->OnNavigationBoundsUpdated(Bounds);Nav->GetDefaultNavDataInstance(FNavigationSystem::Create);Nav->Build();}
            UE_LOG(LogTemp,Display,TEXT("LD_SANDBOX_COOKED_NAV_ENVELOPE %s"),*Bounds->GetComponentsBoundingBox(true).GetSize().ToString());
        }
    }
    if(GetNetMode()==NM_DedicatedServer||GetWorld()->GetMapName().Contains(TEXT("L_Frontend"))){return;}
    for(uint8 Site:{uint8(1),uint8(2),uint8(3),uint8(6),uint8(8)}){if(Site>3&&!LDRegion::Definition(GetWorld())){continue;}if(auto* Landmark=GetWorld()->SpawnActor<ALDObjectiveSite>()){Landmark->Configure(Site);}}
}
ALDGameMode::ALDGameMode()
{
    DefaultPawnClass = ALDCharacter::StaticClass();
    PlayerControllerClass = ALDPlayerController::StaticClass();
    PlayerStateClass = ALDPlayerState::StaticClass();
    GameStateClass = ALDGameState::StaticClass();
    SpectatorClass = nullptr;
    SpawnRandom.Initialize(int32(FPlatformTime::Cycles()));
}

void ALDGameMode::InitGame(const FString& MapName,const FString& Options,FString& ErrorMessage)
{
    Super::InitGame(MapName,Options,ErrorMessage);
    if (!ErrorMessage.IsEmpty()) { EntryFailure = ErrorMessage; return; }
    const FString Package = UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName());
    if (!LDDemoEntry::ValidateSelection(Package, Options, ErrorMessage)
        || GetClass() != ALDGameMode::StaticClass())
    {
        if (ErrorMessage.IsEmpty()) { ErrorMessage = TEXT("Retired GameMode override. Demo uses the native LDGameMode only."); }
        EntryFailure = ErrorMessage;
        UE_LOG(LogTemp, Error, TEXT("LD_DEMO_ENTRY_REJECTED %s"), *ErrorMessage);
        return;
    }
    bScaffoldProfile = false;
    DefaultPawnClass = nullptr;
    bDemoWorld = Package == LDDemoEntry::WorldMap;
    if (!bDemoWorld)
    {
        bEntryAccepted = true;
        UE_LOG(LogTemp, Display, TEXT("LD_DEMO_FRONTEND"));
        return;
    }
    if (!LDDemoEntry::VerifyRuntimeFiles(ErrorMessage, &EntryCatalogDigest) || !LDDemoEntry::VerifyWorldPackages(ErrorMessage))
    {
        EntryFailure = ErrorMessage;
        UE_LOG(LogTemp, Error, TEXT("LD_DEMO_ENTRY_REJECTED %s"), *ErrorMessage);
        return;
    }
    auto* Instance = GetGameInstance<ULDGameInstance>();
    if (!Instance || !Instance->AllocateMatchGeneration(MatchContext.Generation, ErrorMessage))
    {
        if (ErrorMessage.IsEmpty()) { ErrorMessage = TEXT("Demo requires the native LDGameInstance."); }
        EntryFailure = ErrorMessage;
        return;
    }
    MatchContext.LedgerMatch = FGuid::NewGuid();
    MatchPhase = ELDMatchPhase::Lobby;
    RosterRevision = 1;
    bEntryAccepted = true;
    if (GameSession) { GameSession->MaxPlayers = 3; }
    UE_LOG(LogTemp, Display, TEXT("LD_DEMO_LOBBY generation=%u"), MatchContext.Generation);
}

FVector ALDGameMode::TakeSpawn()
{
    if(bEnvironmentProfile){const auto* D=ULDMapDefinition::Get(GetWorld());return D->Spawns[Contenders.Num()%D->Spawns.Num()].Position+FVector(0,0,95);}
    if (SpawnPool.IsEmpty())
    {
        if(const auto* Definition=LDRegion::Definition(GetWorld())){SpawnPool=Definition->Spawns;}
        else{SpawnPool = {{-1700,-1000,100},{-1300,-1000,100},{-500,-1000,100},{-200,700,100},
            {-1800,0,100},{100,1000,100},{1000,1000,100},{1900,-500,100},{1900,1000,100},
            {-600,600,100},{-1300,1000,100},{100,-900,100},{0,0,100},{-1900,-1000,100}};}
    }
    const int32 Index = SpawnRandom.RandRange(0,SpawnPool.Num()-1);
    const FVector Location = SpawnPool[Index];
    SpawnPool.RemoveAtSwap(Index);
    return Location;
}

void ALDGameMode::RestartPlayer(AController* NewPlayer)
{
    if (!bEntryAccepted || !bDemoWorld || MatchPhase == ELDMatchPhase::Lobby) { return; }
    UE_LOG(LogTemp, Error, TEXT("LD_DEMO_SPAWN_REJECTED Body creation requires the committed Demo lifecycle."));
}

void ALDGameMode::StartPlay()
{
    if (!bEntryAccepted) { return; }
    Super::StartPlay();
    if (bDemoWorld)
    {
        auto* Registry = GetWorld()->GetSubsystem<ULDMapRegistry>();
        FString Error;
        if (!Registry || !Registry->Ensure(Error))
        {
            EntryFailure = Error.IsEmpty() ? TEXT("Demo world registry is unavailable.") : Error;
            MatchPhase = ELDMatchPhase::Faulted;
            UE_LOG(LogTemp, Error, TEXT("LD_DEMO_WORLD_ENTRY_REJECTED %s"), *EntryFailure);
        }
        PublishLobby();
    }
#if WITH_DEV_AUTOMATION_TESTS
    else { LDDemoTests::ProbeRejectedEntry(*GetWorld()); LDDemoTests::StartFrontendSettingsProbe(*GetWorld()); }
#endif
}

void ALDGameMode::PreLogin(const FString& Options,const FString& Address,const FUniqueNetIdRepl& UniqueId,FString& ErrorMessage)
{
    Super::PreLogin(Options,Address,UniqueId,ErrorMessage);
    if (!ErrorMessage.IsEmpty()) { return; }
    if (!IsDemoRoom()) { ErrorMessage = TEXT("MissingDemoDefinition: this world is not an initialized Demo room."); return; }
    if (!LDDemoEntry::ValidateSelection(GetWorld()->GetOutermost()->GetName(), Options, ErrorMessage)) { return; }
    if (MatchPhase != ELDMatchPhase::Lobby) { ErrorMessage = TEXT("MatchAlreadyStarted: mid-match joining and reconnecting are not supported."); return; }
    if (Contenders.Num() >= 3) { ErrorMessage = TEXT("RoomFull: the room already has three human players, including the Host."); return; }
    if (UGameplayStatics::ParseOption(Options, TEXT("LDWire")) != LexToString(LDDemoEntry::WireProtocol))
    { ErrorMessage = TEXT("ProtocolMismatch: use the same Demo network build as the Host."); return; }
    FString Version;
    if (!LDDemoEntry::ReadProductVersion(Version, ErrorMessage)) { return; }
    if (UGameplayStatics::ParseOption(Options, TEXT("LDProduct")) != Version)
    { ErrorMessage = TEXT("ProductMismatch: use the same product version as the Host."); return; }
    if (EntryCatalogDigest.IsEmpty() || UGameplayStatics::ParseOption(Options, TEXT("LDCatalog")) != EntryCatalogDigest)
    { ErrorMessage = TEXT("ContentMismatch: use the same approved Demo content as the Host."); }
}

void ALDGameMode::PostLogin(APlayerController* NewPlayer)
{
    if (bEntryAccepted && !bDemoWorld) { Super::PostLogin(NewPlayer); return; }
    FString Reason;
    if (!AdmitRoomMember(Cast<ALDPlayerController>(NewPlayer), Reason))
    {
        UE_LOG(LogTemp, Warning, TEXT("LD_DEMO_ADMISSION_REJECTED %s"), *Reason);
        if (NewPlayer && (!GameSession || !GameSession->KickPlayer(NewPlayer, FText::FromString(Reason)))) { NewPlayer->Destroy(); }
        return;
    }
    Super::PostLogin(NewPlayer);
    PublishLobby();
}
void ALDGameMode::Logout(AController* Exiting)
{
    auto* Controller = Cast<ALDPlayerController>(Exiting);
    auto* Record = Contenders.Find(Controller);
    if (Record && IsMemberPresent(Record->Session))
    {
        const FGuid Session = Record->Session;
        if (LobbyHost == Controller)
        {
            MatchPhase = ELDMatchPhase::Closing;
            if (GateRevision != MAX_uint64) { ++GateRevision; }
            for (const auto& Pair : Contenders)
            {
                if (auto* Member = Pair.Key.Get(); Member && Member != Controller)
                { Member->ClientDemoRoomClosed(MatchContext.Generation, TEXT("The Host left the room. Host migration is not supported.")); }
            }
            LobbyHost.Reset();
        }
        RemoveLifeBinding(Session);
        Record->State.bActive = false;
        if (Vision) { Vision->Unregister(Controller); }
        if (MatchPhase == ELDMatchPhase::Lobby)
        {
            Contenders.Remove(Controller);
            Lives.Remove(Session);
            RosterOrder.Remove(Session);
            FString Reason;
            if (!AdvanceRoster(Reason)) { EntryFailure = Reason; }
        }
        PublishLobby();
    }
    Super::Logout(Exiting);
}
FLDOwnerState* ALDGameMode::StateFor(ALDPlayerController* PC)
{ FLDContenderRecord* Record=Contenders.Find(PC); return Record ? &Record->State : nullptr; }
FGuid ALDGameMode::ContenderId(ALDPlayerController* PC) const
{const auto* Record=Contenders.Find(PC);return Record?Record->Session:FGuid();}
void ALDGameMode::SyncOwner(ALDPlayerController* PC)
{
    if (FLDOwnerState* State=StateFor(PC))
    {
        if (State->Revision == MAX_uint64)
        {
            MatchPhase = ELDMatchPhase::Faulted;
            UE_LOG(LogTemp, Error, TEXT("LD_OWNER_REVISION_EXHAUSTED"));
            return;
        }
        if (IsDemoRoom()) { ProjectLife(*PC, *State); }
        State->Match=MatchContext.Generation; ++State->Revision; PC->OwnerState=*State;if(PC->IsLocalController()){PC->OnRep_OwnerState();}PC->ForceNetUpdate();
    }
}

void ALDGameMode::EndContenderLife(ALDPlayerController* PC,double Now)
{
    if(Combat){Combat->FlushPastCommand();}
    FLDOwnerState* State=StateFor(PC);
    if (!State || !State->bActive || IsFinished()) { return; }
    if(WorldRuntime){WorldRuntime->EndLife(PC);}
    if(bTacticalProfile){if(Development){Development->Cancel(PC,TEXT("Death cancelled preparation"));}Combat->CancelTactical(Cast<ALDCharacter>(PC->GetPawn()),TEXT("Life ended"));}
    if(bTacticalProfile){if(Combat){Combat->EndContentLife(Cast<ALDCharacter>(PC->GetPawn()));}if(Vision){Vision->EndLife(PC);}}if(Production){Production->EndLife(PC);}if(Inventory){Inventory->EndLife(PC);}
    if(Development){Development->DropBooks(PC,PC->GetPawn()?PC->GetPawn()->GetActorLocation():LDRegion::Site(GetWorld(),LDObjective::Guild));}
    if(Objectives){Objectives->EndLife(PC,PC->GetPawn()?PC->GetPawn()->GetActorLocation():LDRegion::Site(GetWorld(),LDObjective::Guild),Now);}
    const auto Cost=bDevelopmentProfile
        ? LDRules::FDeathCost{(bTacticalProfile?LDDevelopment::TacticalDeathDelay(State->Build.Level,Now-MatchStartedAt):LDDevelopment::DeathDelay(State->Build.Level,Now-MatchStartedAt)),LDDevelopment::DeathLoss(State->Gold)}
        : LDRules::DeathCost(LDRules::LevelFromXp(State->Xp),State->Gold,FMath::Max(0.,Now-MatchStartedAt));
    State->LastLostGold=Cost.LostGold; State->Gold-=Cost.LostGold;RecordEconomyGold(TEXT("DeathTax"),0,Cost.LostGold);
    if(Economy&&State->CharterPurchase.IsValid()&&!State->CharterActivation.IsValid()){State->bCharterAwaitingRespawn=true;State->CharterReadyAt=0;}
    State->bAidConsent=false;State->bActive=false; State->Hp=0; State->WardCharges=0;
    if(PC->Navigation){PC->Navigation->ResetLife();}
    State->ReturnAt=float(Now+Cost.DelaySeconds); State->WarningUntil=0; State->ActionCue=0;
    State->ActiveSkill=ELDSkill::None;State->SkillEndsAt=0;
    ++State->DeathSerial;
    PC->UnPossess(); SyncOwner(PC); PC->ClientLifeReceipt(*State);
    UE_LOG(LogTemp,Display,TEXT("LD_DEATH_RECEIPT life=%u death=%u loss=%lld return=%.3f"),State->Life,State->DeathSerial,State->LastLostGold,State->ReturnAt);
}
FVector ALDGameMode::ReturnSpawn(ALDPlayerController* PC)
{
    if(bPopulationProfile&&Population){return Population->StartFor(PC);}
    if(bEnvironmentProfile){return TakeSpawn();}
    TArray<FVector> Candidates={{-1700,-1000,100},{-1300,-1000,100},{-500,-1000,100},{-200,700,100},
        {-1800,0,100},{100,1000,100},{1000,1000,100},{1900,-500,100},{1900,1000,100},
        {-600,600,100},{-1300,1000,100},{100,-900,100},{0,0,100},{-1900,-1000,100}};
    if(const auto* Definition=LDRegion::Definition(GetWorld())){Candidates=Definition->Spawns;}
    TArray<FVector> Safe, Farthest;
    double Best=-1;
    for(const FVector& Point:Candidates)
    {
        FHitResult Ground;
        if(!GetWorld()->LineTraceSingleByChannel(Ground,Point+FVector(0,0,500),Point-FVector(0,0,300),ECC_GameTraceChannel1)
            ||Ground.ImpactNormal.Z<FMath::Cos(FMath::DegreesToRadians(40.))
            ||GetWorld()->OverlapBlockingTestByChannel(Point,FQuat::Identity,ECC_WorldStatic,FCollisionShape::MakeCapsule(28,90))){continue;}
        double Nearest=DBL_MAX;
        for(const auto& Pair:Contenders)
        { if(Pair.Key.IsValid() && Pair.Key.Get()!=PC && Pair.Value.State.bActive && Pair.Key->GetPawn()){Nearest=FMath::Min(Nearest,FVector::DistSquared2D(Point,Pair.Key->GetPawn()->GetActorLocation()));} }
        if(Nearest>=440*440){Safe.Add(Point);}
        if(Nearest>Best){Best=Nearest;Farthest.Reset();Farthest.Add(Point);}else if(Nearest==Best){Farthest.Add(Point);}
    }
    const auto& Pool=Safe.IsEmpty()?Farthest:Safe;
    checkf(!Pool.IsEmpty(),TEXT("Authored return pool has no legal capsule position"));
    return Pool[SpawnRandom.RandRange(0,Pool.Num()-1)];
}
void ALDGameMode::ReturnDueContenders(double Now)
{
    if(IsFinished()){return;}
    for(auto& Pair:Contenders)
    {
        FLDOwnerState& State=Pair.Value.State; ALDPlayerController* PC=Pair.Key.Get();
        if(!PC || State.bActive || State.ReturnAt<=0 || Now<State.ReturnAt){continue;}
        ++State.Life; State.bActive=true; State.Hp=bDevelopmentProfile?LDDevelopment::MaxHP(State.Build):100; State.ReturnAt=0;if(bTacticalProfile){State.StatusMask=0;State.Barrier=0;State.StatusFeedback.Reset();State.bAidConsent=false;} State.ProtectedUntil=float(Now+(bTacticalProfile?3:4));
        State.WarningUntil=0; State.DamageUntil=0; State.ExposedUntil=0; State.ActionCue=0;
        State.ActiveSkill=ELDSkill::None;State.SkillEndsAt=0;
        RestartPlayerAtTransform(PC,FTransform(FRotator::ZeroRotator,ReturnSpawn(PC)));
        if(!PC->GetPawn()){State.bActive=false;State.ReturnAt=float(Now+.1);--State.Life;continue;}
        if(State.bCharterAwaitingRespawn){State.CharterReadyAt=Now+30;State.bCharterAwaitingRespawn=false;}
        if (bDevelopmentProfile) { RefreshBuild(PC,true); }
        if(auto* Body=Cast<ALDCharacter>(PC->GetPawn())){if(auto* S=Combat->StateFor(Body)){S->ProtectionUntil=State.ProtectedUntil;}}
        SyncOwner(PC); PC->ClientLifeReceipt(State);
        UE_LOG(LogTemp,Display,TEXT("LD_RETURN_RECEIPT life=%u protection=%.3f"),State.Life,State.ProtectedUntil);
    }
}
bool ALDGameMode::IsFinished() const
{return MatchPhase==ELDMatchPhase::Results||MatchPhase==ELDMatchPhase::Closing||MatchPhase==ELDMatchPhase::Faulted;}
double ALDGameMode::RuleTime() const
{const auto* GS=GetGameState<ALDGameState>();return GS&&GS->EndReason?GS->MatchEndedAt:GetWorld()->GetTimeSeconds();}

bool ALDGameMode::IsPlaying() const
{
    return IsGameplayWriteOpen(MatchContext);
}

void ALDGameMode::UpdateLobby()
{
    auto* Public=GetGameState<ALDGameState>(); if (!Public) { return; }
    Public->LobbyCount=uint8(Contenders.Num()); Public->ReadyCount=0;
    for (const auto& Pair:Contenders) { if (Pair.Value.State.bReady) { ++Public->ReadyCount; } }
    Public->ForceNetUpdate();
}

bool ALDGameMode::SetLobbyChoice(ALDPlayerController* PC,uint8 ClassId,bool Ready,FString& Reason)
{
    auto* State=StateFor(PC); const auto* Public=GetGameState<ALDGameState>();
    if (!bDevelopmentProfile || !State || !Public || Public->bMatchStarted || (bV08Profile&&!LDV08::PresetEnabled(ClassId)) || ClassId>=(bTacticalProfile?9:3) || !Catalog)
    { Reason=TEXT("Class choice is available in the lobby only."); return false; }
    if (State->Build.Preset!=ClassId)
    {
        const uint32 Revision=State->Build.Revision+1;
        State->Build=bTacticalProfile?LDDevelopment::TacticalStart(ClassId,TacticalCatalog->Checksum,RulesVersion()):LDDevelopment::StartingBuild(ClassId,Catalog); State->Build.Revision=Revision;
        if(bTacticalProfile){State->WardCharges=bV08Profile?0:ClassId>=3?0:1;}State->Xp=0; RefreshBuild(PC,true);
    }
    State->bReady=Ready; SyncOwner(PC); UpdateLobby();
    Reason=Ready?TEXT("Ready. Starting class remains private."):TEXT("Class selected. Mark Ready when prepared."); return true;
}

bool ALDGameMode::StartDevelopmentMatch(ALDPlayerController* PC,FString& Reason)
{
    if(bEnvironmentProfile){Reason=TEXT("B91 environment is active; full-match economy admission belongs to B92");return false;}
    auto* Public=GetGameState<ALDGameState>();
    if (!bDevelopmentProfile || !PC || PC!=LobbyHost.Get() || !Public || Public->bMatchStarted || Contenders.Num()<1 || Contenders.Num()>4)
    { Reason=TEXT("Only the lobby host can start once."); return false; }
    for (const auto& Pair:Contenders) { if (!Pair.Value.State.bReady) { Reason=TEXT("Every connected contender must be Ready."); return false; } }
    if(bUIControlProfile)
    {
        Economy=GetWorld()->SpawnActor<ALDEconomyDirector>();
        if(!Economy||!Economy->Initialize(Reason)){if(Economy){Economy->Destroy();Economy=nullptr;}return false;}
    }
    if(Economy){Bank=GetWorld()->SpawnActor<ALDBankDirector>();Commerce=GetWorld()->SpawnActor<ALDCommerceDirector>();Production=GetWorld()->SpawnActor<ALDProductionDirector>();Production->Initialize();if(IsTacticalProfile()){Acquisition=GetWorld()->SpawnActor<ALDAcquisitionDirector>();if(!Acquisition->Initialize(Reason)){return false;}}}
    MatchStartedAt=GetWorld()->GetTimeSeconds(); Public->bMatchStarted=true;
    if(bTacticalProfile&&(GetWorld()->GetMapName().Contains(TEXT("L_V07Region"))||GetWorld()->GetMapName().Contains(TEXT("L_V08Region")))){WorldRuntime=GetWorld()->SpawnActor<ALDWorldDirector>();WorldRuntime->Initialize();}
    Public->MatchDeadline=float(MatchStartedAt+(bTacticalProfile?2100:LDDevelopment::DeadlineSeconds));
    for (auto& Pair:Contenders)
    {
        if (auto* Player=Pair.Key.Get())
        {
            if(Inventory){Inventory->Initialize(Player);}
            RefreshBuild(Player,true); Pair.Value.State.ProtectedUntil=float(MatchStartedAt+(bTacticalProfile?3:4));
            if (auto* Body=Cast<ALDCharacter>(Player->GetPawn())) { if (auto* S=Combat->StateFor(Body)) { S->ProtectionUntil=MatchStartedAt+(bTacticalProfile?3:4); } }
            SyncOwner(Player);
        }
    }
    if(Economy){Economy->SampleCharter(MatchStartedAt);Economy->SampleInstitutions(MatchStartedAt);FString EvidenceError;Economy->Export(TEXT("Endowment"),EvidenceError);}
    Public->ForceNetUpdate(); Reason=Contenders.Num()==1?(bTacticalProfile?TEXT("Practice started: one human; 35 minute cap."):TEXT("Practice started: one human; 25 minute cap.")):TEXT("Integration match started. Roster locked.");
    UE_LOG(LogTemp,Display,TEXT("LD_V04_START humans=%d practice=%d deadline=%.3f catalog=%s"),Contenders.Num(),Contenders.Num()==1,Public->MatchDeadline,*Catalog->SourceChecksum);
    return true;
}

void ALDGameMode::RefreshBuild(ALDPlayerController* PC,bool RestoreFullHP)
{
    auto* State=StateFor(PC); if (!State || !bDevelopmentProfile) { return; }
    if(bTacticalProfile&&!RestoreFullHP){if(const auto* Body=Cast<ALDCharacter>(PC->GetPawn())){if(const auto* S=Combat?Combat->StateFor(Body):nullptr){State->Hp=S->Hp;State->Mp=S->Tactical.MP;}}}
    if(bTacticalProfile){State->Build.Gear=LDGear::Derive(State->Inventory);}
    State->MaxHp=LDDevelopment::MaxHP(State->Build);
    if(bTacticalProfile){State->MaxMp=LDTactical::MaxMP(State->Build);State->Mp=RestoreFullHP?State->MaxMp:FMath::Min(State->Mp,State->MaxMp);}
    State->Hp=RestoreFullHP?State->MaxHp:FMath::Min(State->Hp,State->MaxHp);
    if (auto* Body=Cast<ALDCharacter>(PC->GetPawn()))
    {
        Body->GetCharacterMovement()->MaxWalkSpeed=float(180*LDDevelopment::SpeedFactor(State->Build));
        if (Combat) { if (auto* S=Combat->StateFor(Body)) { S->Hp=State->Hp;if(bTacticalProfile){S->NullBellReadyAt=State->NullBellReadyAt;S->Tactical.MajorReadyAt=State->MajorProtectionReadyAt;S->Tactical.MP=State->Mp;S->Tactical.LastRegen=LDTactical::Milliseconds(RuleTime());} } }
    }
    SyncOwner(PC);
}

void ALDGameMode::AwardExperience(ALDPlayerController* PC,int64 Amount)
{
    auto* State=StateFor(PC); if (!State || Amount<=0) { return; }
    if (bDevelopmentProfile) { LDDevelopment::AwardXP(State->Build,State->Xp,Amount);if(bTacticalProfile){RefreshBuild(PC);return;} }
    else { State->Xp+=Amount; }
    SyncOwner(PC);
}

void ALDGameMode::EconomyWallets(TMap<FName,int64>& Out) const
{for(const auto& Pair:Contenders){Out.Add(ALDEconomyDirector::Wallet(Pair.Value.Session),Pair.Value.State.Gold);Out.Add(ALDEconomyDirector::BankAccount(Pair.Value.Session),Pair.Value.State.BankGold);}}
void ALDGameMode::RecordEconomyGold(FName Cause,int64 Mint,int64 Burn)
{if(bPopulationProfile&&Population){FString Why;if(!Population->RecordGold(Cause,Mint,Burn,Why)){UE_LOG(LogTemp,Fatal,TEXT("B92 unclassified wallet mutation: %s"),*Why);}return;}if(Economy){Economy->GoldChanged(Cause,Mint,Burn);}}
