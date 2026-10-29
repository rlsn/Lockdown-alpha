#include "Framework/LDGameMode.h"
#include "World/LDEnvironmentDirector.h"
#include "Region/LDMapDefinition.h"
#include "Tests/LDB91Probe.h"
#include "Tests/LDB91NetProbe.h"
#include "Tests/LDB91ObjectsProbe.h"
#include "Tests/LDB91ToolsProbe.h"
#include "Tests/LDB91SaplingsProbe.h"
#include "Tests/LDB91FieldsProbe.h"
#include "Tests/LDB91IgnitionProbe.h"
#include "Tests/LDB91GrassProbe.h"
#include "Tests/LDB91CheckpointProbe.h"
#include "Tests/LDB91CombatProbe.h"
#include "Tests/LDB91EncounterProbe.h"
#include "Tests/LDB91TraversalProbe.h"
#include "Development/LDV08Loadout.h"
#include "World/LDWorldDirector.h"
#include "Combat/LDTacticalCatalog.h"
#include "Inventory/LDEquipmentCatalog.h"
#include "Inventory/LDAcquisitionDirector.h"
#include "Inventory/LDAcquisitionCatalog.h"
#include "Combat/LDTacticalRules.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Economy/LDEconomyDirector.h"
#include "Economy/LDBankDirector.h"
#include "Inventory/LDTradeDirector.h"
#include "Framework/LDPlayerController.h"
#include "Character/LDCharacter.h"
#include "Tests/LDMovementProbe.h"
#include "Tests/LDNavigationProbe.h"
#include "Tests/LDInputProbe.h"
#include "Navigation/LDNavigationComponent.h"
#include "Region/LDRegionNavBounds.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Engine/World.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "EngineUtils.h"
#include "Vision/LDVisionDirector.h"
#include "Framework/LDGameInstance.h"
#include "Net/UnrealNetwork.h"
#include "Tests/LDViewNetworkProbe.h"
#include "Combat/LDCombatDirector.h"
#include "Core/LDRules.h"
#include "Tests/LDCombatProbe.h"
#include "Tests/LDCombatNetworkProbe.h"
#include "Objectives/LDObjectiveDirector.h"
#include "Objectives/LDObjectiveRules.h"
#include "Tests/LDObjectiveProbe.h"
#include "Tests/LDObjectiveNetworkProbe.h"
#include "Tests/LDResetProbe.h"
#include "Tests/LDSoakProbe.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameSession.h"
#include "Tests/LDDevelopmentProbe.h"
#include "Development/LDDevelopmentDirector.h"
#include "Development/LDWorldChecks.h"
#include "Region/LDRegionDefinition.h"
#include "Region/LDRegionDirector.h"
#include "Inventory/LDInventoryDirector.h"
#include "Inventory/LDVendorDirector.h"
#include "Tests/LDInventoryProbe.h"
#include "Tests/LDVendorProbe.h"
#include "Tests/LDTradeProbe.h"
#include "Tests/LDEconomyProbe.h"
#include "Tests/LDCommerceProbe.h"
#include "Economy/LDProductionDirector.h"
#include "Tests/LDProductionProbe.h"
#include "Tests/LDInstitutionProbe.h"
#include "Tests/LDCharterProbe.h"
#include "Tests/LDAcquisitionProbe.h"
#include "Tests/LDContentProbe.h"
#include "Tests/LDWorldProbe.h"
#include "Tests/LDContentNetworkProbe.h"
#include "Tests/LDTacticalProbe.h"
#include "Tests/LDGeometryProbe.h"
#include "Tests/LDLoadoutProbe.h"
#include "Tests/LDB81Probe.h"
#include "Tests/LDB82Probe.h"
#include "Tests/LDB83Probe.h"
#include "Tests/LDB84Probe.h"
#include "Tests/LDB86Probe.h"
#include "Tests/LDAcquisitionNetworkProbe.h"
#include "Tests/LDEconomyUIProbe.h"

ALDPlayerState::ALDPlayerState() { bOnlyRelevantToOwner = true; bAlwaysRelevant = false; }
void ALDGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
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
    SpawnRandom.Initialize(int32(FPlatformTime::Cycles()));
}

void ALDGameMode::InitGame(const FString& MapName,const FString& Options,FString& ErrorMessage)
{
    Super::InitGame(MapName,Options,ErrorMessage);
    const FString Profile=UGameplayStatics::ParseOption(Options,TEXT("Profile"));
    if(!Profile.IsEmpty()&&Profile!=TEXT("V03")&&Profile!=TEXT("V04")&&Profile!=TEXT("V05")&&Profile!=TEXT("v06_regression")&&Profile!=TEXT("v07_integration")&&Profile!=TEXT("v08_integration")&&Profile!=TEXT("v09_environment")){ErrorMessage=TEXT("Unknown rules profile");return;}
    bEnvironmentProfile=Profile==TEXT("v09_environment");
    bV08Profile=bEnvironmentProfile||Profile==TEXT("v08_integration");bTacticalProfile=bV08Profile||Profile==TEXT("v07_integration");
    if(bEnvironmentProfile)
    {
        FString Why,Json,Staged;const auto* D=ULDMapDefinition::Get(GetWorld());
        if(!IsA<ALDB91GameMode>()||!D||!D->Validate(Why)||!ALDEnvironmentDirector::LoadContract(Json,Why)
           ||!FFileHelper::LoadFileToString(Staged,*(FPaths::ProjectContentDir()/TEXT("Lockdown/Data/V09/map_b90.json")))||Staged.Replace(TEXT("\r\n"),TEXT("\n"))!=D->SourceJson)
        {ErrorMessage=TEXT("B91 requires the saved full map and closed environment contract: ")+Why;return;}
    }
    if(bTacticalProfile){auto* Equipment=ULDEquipmentCatalog::Load(RulesVersion());FString Why,StagedGear;if(!Equipment||!Equipment->Validate(Why)||!FFileHelper::LoadFileToString(StagedGear,*(FPaths::ProjectContentDir()/(bV08Profile?TEXT("Lockdown/Data/V08/equipment_b84.json"):TEXT("Lockdown/Data/V07/equipment.json"))))||StagedGear.Replace(TEXT("\r\n"),TEXT("\n"))!=Equipment->SourceJson){ErrorMessage=TEXT("Invalid/stale v07 equipment catalog; run bootstrap: ")+Why;return;}}
    if(bTacticalProfile){FString E;TacticalCatalog=ULDTacticalCatalog::Load(RulesVersion());if(!TacticalCatalog||!TacticalCatalog->Validate(E)){ErrorMessage=TEXT("Invalid v07 combat catalog: ")+E;return;}FString Staged;if(!FFileHelper::LoadFileToString(Staged,*(FPaths::ProjectContentDir()/(bV08Profile?TEXT("Lockdown/Data/V08/combat_b84.json"):TEXT("Lockdown/Data/V07/combat.json"))))||Staged.Replace(TEXT("\r\n"),TEXT("\n"))!=TacticalCatalog->SourceJson){ErrorMessage=TEXT("Staged v07 catalog differs from native asset; run bootstrap");return;}}
    bUIControlProfile=bTacticalProfile||Profile==TEXT("v06_regression")||Profile==TEXT("V05")||(Profile.IsEmpty()&&(MapName.Contains(TEXT("L_DevelopmentRegion"))||MapName.Contains(TEXT("L_RegionAlternate"))));
    bDevelopmentProfile=bUIControlProfile||UGameplayStatics::ParseOption(Options,TEXT("Profile"))==TEXT("V04")||(Profile.IsEmpty()&&(MapName.Contains(TEXT("L_DevelopmentRegion"))||MapName.Contains(TEXT("L_RegionAlternate"))));
    if (bDevelopmentProfile)
    {
        bScaffoldProfile=false;
        if (GameSession)
        {
            GameSession->MaxPlayers=4;
#if !UE_BUILD_SHIPPING
            // The packaged diagnostic listen host is infrastructure, not a contender.
            if(bV08Profile&&FParse::Param(FCommandLine::Get(),TEXT("LDB86Probe"))&&FParse::Param(FCommandLine::Get(),TEXT("LDRemoteAuthority"))){GameSession->MaxPlayers=5;}
#endif
        }
        Catalog=ULDCharacterCatalog::Load(); FString Error;
        if (!Catalog || !Catalog->Validate(Error)) { ErrorMessage=TEXT("Invalid v0.4 character catalog: ")+Error; }
    }
    UE_LOG(LogTemp,Display,TEXT("LD_PROFILE %s"),bEnvironmentProfile?TEXT("v09_environment B91 structural/door implementation; full-match admission pending"):bV08Profile?TEXT("v08_integration B86 native UI: complete catalog / four earned Heart routes"):bTacticalProfile?TEXT("v07_integration B71 partial: 2100s level15 combat core"):bUIControlProfile?TEXT("V05 input checkpoint: retained level5/slots4/1500s"):bDevelopmentProfile?TEXT("V04 Integration: level5/slots4/1500s"):TEXT("Legacy V03 migration"));
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
#if !UE_BUILD_SHIPPING
    // A packaged Game target uses a headless listen world for the two-remote-client
    // topology. Its local connection is diagnostic infrastructure, not a contender.
    if((bScaffoldProfile||(bDevelopmentProfile&&(FParse::Param(FCommandLine::Get(),TEXT("LDDevelopmentProbe"))||(FParse::Param(FCommandLine::Get(),TEXT("LDSoakProbe"))||FParse::Param(FCommandLine::Get(),TEXT("LDB86Probe")))||FParse::Param(FCommandLine::Get(),TEXT("LDNavigationProbe"))||(FParse::Param(FCommandLine::Get(),TEXT("LDInputProbe"))||FParse::Param(FCommandLine::Get(),TEXT("LDInventoryProbe"))||(FParse::Param(FCommandLine::Get(),TEXT("LDVendorProbe"))||FParse::Param(FCommandLine::Get(),TEXT("LDTradeProbe")))))))&&NewPlayer->IsLocalController()&&FParse::Param(FCommandLine::Get(),TEXT("LDRemoteAuthority"))){return;}
#endif
    if (GetWorld()->GetMapName().Contains(TEXT("L_Frontend"))) { return; }
    RestartPlayerAtTransform(NewPlayer, FTransform(FRotator::ZeroRotator, TakeSpawn()));
    if (bDevelopmentProfile) { if (auto* PC=Cast<ALDPlayerController>(NewPlayer)) { RefreshBuild(PC,true); } }
}

void ALDGameMode::StartPlay()
{
    Super::StartPlay();
    if (GetWorld()->GetMapName().Contains(TEXT("L_Frontend"))) { return; }
    MatchStartedAt=GetWorld()->GetTimeSeconds();
    auto* Public=GetGameState<ALDGameState>();
    Public->bDevelopmentProfile=bDevelopmentProfile; Public->bMatchStarted=!bDevelopmentProfile;
    Public->bUIControlProfile=bUIControlProfile;
    Public->bEnvironmentProfile=bEnvironmentProfile;
    Combat=GetWorld()->SpawnActor<ALDCombatDirector>();
    Vision=GetWorld()->SpawnActor<ALDVisionDirector>();
    Vision->AddTickPrerequisiteActor(Combat);
    Vision->Generation=++CastChecked<ULDGameInstance>(GetGameInstance())->AuthorityGeneration;
    Objectives=GetWorld()->SpawnActor<ALDObjectiveDirector>();
    if(bDevelopmentProfile){Development=GetWorld()->SpawnActor<ALDDevelopmentDirector>();if(!bEnvironmentProfile){WorldChecks=GetWorld()->SpawnActor<ALDWorldChecks>();}}
    if(bUIControlProfile){Inventory=GetWorld()->SpawnActor<ALDInventoryDirector>();if(!bEnvironmentProfile){Vendor=GetWorld()->SpawnActor<ALDVendorDirector>();}Trade=GetWorld()->SpawnActor<ALDTradeDirector>();}
    if(bDevelopmentProfile&&LDRegion::Definition(GetWorld())){Region=GetWorld()->SpawnActor<ALDRegionDirector>();}
    if (bDevelopmentProfile) { Public->MatchDeadline=0; UpdateLobby(); }
    for (FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
    { if (ALDPlayerController* PC=Cast<ALDPlayerController>(It->Get());PC&&StateFor(PC)) { SyncOwner(PC); Vision->Register(PC); } }
    if(bEnvironmentProfile)
    {
        FString Why;auto* Environment=GetWorld()->SpawnActor<ALDEnvironmentDirector>();
        if(!Environment->Initialize(Why)){UE_LOG(LogTemp,Fatal,TEXT("B91 environment rejected: %s"),*Why);return;}
        Environment->AddTickPrerequisiteActor(Combat);Public->bMatchStarted=true;Public->bPublicSensorActive=false;
        auto* Objects=GetWorld()->SpawnActor<ALDWorldObjects>();
        if(!Objects->Initialize(Why)){UE_LOG(LogTemp,Fatal,TEXT("B91 world objects rejected: %s"),*Why);return;}
        Objects->AddTickPrerequisiteActor(Environment);
        for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
        {if(auto* PC=Cast<ALDPlayerController>(It->Get())){Inventory->Initialize(PC);RefreshBuild(PC,true);}}
        if(FParse::Param(FCommandLine::Get(),TEXT("LDB91Probe"))){GetWorld()->SpawnActor<ALDB91Probe>();}
        if(FParse::Param(FCommandLine::Get(),TEXT("LDB91CheckpointProbe"))){GetWorld()->SpawnActor<ALDB91CheckpointProbe>();}
        else if(FParse::Param(FCommandLine::Get(),TEXT("LDB91EncounterProbe"))||FParse::Param(FCommandLine::Get(),TEXT("LDB91EncounterResume"))||FParse::Param(FCommandLine::Get(),TEXT("LDB91ForestRoutes"))){GetWorld()->SpawnActor<ALDB91EncounterProbe>();}
        else if(FParse::Param(FCommandLine::Get(),TEXT("LDB91CombatProbe"))){GetWorld()->SpawnActor<ALDB91CombatProbe>();}
        else if(FParse::Param(FCommandLine::Get(),TEXT("LDB91GrassProbe"))){GetWorld()->SpawnActor<ALDB91GrassProbe>();}
        else if(FParse::Param(FCommandLine::Get(),TEXT("LDB91IgnitionProbe"))){GetWorld()->SpawnActor<ALDB91IgnitionProbe>();}
        else if(FParse::Param(FCommandLine::Get(),TEXT("LDB91FieldsProbe"))){GetWorld()->SpawnActor<ALDB91FieldsProbe>();}
        else if(FParse::Param(FCommandLine::Get(),TEXT("LDB91SaplingsProbe"))){GetWorld()->SpawnActor<ALDB91SaplingsProbe>();}
        else if(FParse::Param(FCommandLine::Get(),TEXT("LDB91TraversalProbe"))){GetWorld()->SpawnActor<ALDB91TraversalProbe>();}
        else if(FParse::Param(FCommandLine::Get(),TEXT("LDB91ToolsProbe"))){GetWorld()->SpawnActor<ALDB91ToolsProbe>();}
        else if(FParse::Param(FCommandLine::Get(),TEXT("LDB91ObjectsProbe"))){GetWorld()->SpawnActor<ALDB91ObjectsProbe>();}
        else if(FParse::Param(FCommandLine::Get(),TEXT("LDB91NetProbe"))){GetWorld()->SpawnActor<ALDB91NetProbe>();}
        UE_LOG(LogTemp,Display,TEXT("LD_B91_ENVIRONMENT doors/interiors review; no markets, population or victory executors"));return;
    }
    if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
    {
        for (TActorIterator<ANavMeshBoundsVolume> It(GetWorld()); It; ++It) { Nav->OnNavigationBoundsUpdated(*It); }
        Nav->GetDefaultNavDataInstance(FNavigationSystem::Create);
        Nav->Build();
        if(Region){UE_LOG(LogTemp,Display,TEXT("LD_REGION_NAVIGATION bounds=%s"),*Nav->GetNavigableWorldBounds().ToString());}
    }
    for (int32 Index=0; Index<8; ++Index)
    {
        const auto* Definition=LDRegion::Definition(GetWorld());auto* Resident=GetWorld()->SpawnActor<ALDResident>(Definition?Definition->Residents[Index]:TakeSpawn(),FRotator::ZeroRotator);
        if(Region){Region->SetupResident(Resident,Index==Definition->TrainedResident);}
    }
#if !UE_BUILD_SHIPPING
    if(bTacticalProfile&&FParse::Param(FCommandLine::Get(),TEXT("LDAcquisitionNetProbe"))){GetWorld()->SpawnActor<ALDAcquisitionNetworkProbe>();}
    if(bTacticalProfile&&FParse::Param(FCommandLine::Get(),TEXT("LDContentNetProbe"))){GetWorld()->SpawnActor<ALDContentNetworkProbe>();}
    if(bTacticalProfile&&(FParse::Param(FCommandLine::Get(),TEXT("LDWorldProbe"))||FParse::Param(FCommandLine::Get(),TEXT("LDWorldNetProbe")))){GetWorld()->SpawnActor<ALDWorldProbe>();}
    if(bTacticalProfile&&FParse::Param(FCommandLine::Get(),TEXT("LDContentProbe"))){GetWorld()->SpawnActor<ALDContentProbe>();}
    if(bTacticalProfile&&FParse::Param(FCommandLine::Get(),TEXT("LDLoadoutProbe"))){GetWorld()->SpawnActor<ALDLoadoutProbe>();}
    if(bV08Profile&&FParse::Param(FCommandLine::Get(),TEXT("LDB86Probe"))){GetWorld()->SpawnActor<ALDB86Probe>();}
    if(bV08Profile&&FParse::Param(FCommandLine::Get(),TEXT("LDB84Probe"))){GetWorld()->SpawnActor<ALDB84Probe>();}
    if(bV08Profile&&FParse::Param(FCommandLine::Get(),TEXT("LDB83Probe"))){GetWorld()->SpawnActor<ALDB83Probe>();}
    if(bV08Profile&&FParse::Param(FCommandLine::Get(),TEXT("LDB82Probe"))){GetWorld()->SpawnActor<ALDB82Probe>();}
    if(bV08Profile&&FParse::Param(FCommandLine::Get(),TEXT("LDB81Probe"))){GetWorld()->SpawnActor<ALDB81Probe>();}
    if(bTacticalProfile&&FParse::Param(FCommandLine::Get(),TEXT("LDGeometryProbe"))){GetWorld()->SpawnActor<ALDGeometryProbe>();}
    if(bTacticalProfile&&FParse::Param(FCommandLine::Get(),TEXT("LDTacticalProbe"))){GetWorld()->SpawnActor<ALDTacticalProbe>();}
    if(bUIControlProfile&&FParse::Param(FCommandLine::Get(),TEXT("LDCommerceProbe"))){GetWorld()->SpawnActor<ALDCommerceProbe>();}
    if(bUIControlProfile&&FParse::Param(FCommandLine::Get(),TEXT("LDProductionProbe"))){GetWorld()->SpawnActor<ALDProductionProbe>();}
    if(bUIControlProfile&&FParse::Param(FCommandLine::Get(),TEXT("LDEconomyUIProbe"))){GetWorld()->SpawnActor<ALDEconomyUIProbe>();}
    if(bUIControlProfile&&FParse::Param(FCommandLine::Get(),TEXT("LDCharterProbe"))){GetWorld()->SpawnActor<ALDCharterProbe>();}
    if(bUIControlProfile&&FParse::Param(FCommandLine::Get(),TEXT("LDAcquisitionProbe"))){GetWorld()->SpawnActor<ALDAcquisitionProbe>();}
    if(bUIControlProfile&&FParse::Param(FCommandLine::Get(),TEXT("LDInstitutionProbe"))){GetWorld()->SpawnActor<ALDInstitutionProbe>();}
    if(bUIControlProfile&&FParse::Param(FCommandLine::Get(),TEXT("LDEconomyProbe"))){GetWorld()->SpawnActor<ALDEconomyProbe>();}
    if(bUIControlProfile&&FParse::Param(FCommandLine::Get(),TEXT("LDTradeProbe"))){GetWorld()->SpawnActor<ALDTradeProbe>();}
    if(bUIControlProfile&&FParse::Param(FCommandLine::Get(),TEXT("LDVendorProbe"))){GetWorld()->SpawnActor<ALDVendorProbe>();}
    if(bUIControlProfile&&FParse::Param(FCommandLine::Get(),TEXT("LDInventoryProbe"))){GetWorld()->SpawnActor<ALDInventoryProbe>();}
    if(bUIControlProfile&&FParse::Param(FCommandLine::Get(),TEXT("LDInputProbe"))){GetWorld()->SpawnActor<ALDInputProbe>();}
    if(bUIControlProfile&&FParse::Param(FCommandLine::Get(),TEXT("LDNavigationProbe"))){GetWorld()->SpawnActor<ALDNavigationProbe>();}
    if (bDevelopmentProfile&&FParse::Param(FCommandLine::Get(),TEXT("LDDevelopmentProbe"))) { GetWorld()->SpawnActor<ALDDevelopmentProbe>(); }
    if (FParse::Param(FCommandLine::Get(), TEXT("LDMovementProbe"))) { GetWorld()->SpawnActor<ALDMovementProbe>(); }
    if(bScaffoldProfile&&FParse::Param(FCommandLine::Get(),TEXT("LDCombatProbe"))){GetWorld()->SpawnActor<ALDCombatProbe>();}
    if (bScaffoldProfile && FParse::Param(FCommandLine::Get(),TEXT("LDViewProbe"))) { GetWorld()->SpawnActor<ALDViewNetworkProbe>(); }
    if(bScaffoldProfile&&FParse::Param(FCommandLine::Get(),TEXT("LDCombatNetProbe"))){GetWorld()->SpawnActor<ALDCombatNetworkProbe>();}
    if(bScaffoldProfile&&FParse::Param(FCommandLine::Get(),TEXT("LDObjectiveProbe"))){GetWorld()->SpawnActor<ALDObjectiveProbe>();}
    if(bScaffoldProfile&&FParse::Param(FCommandLine::Get(),TEXT("LDObjectiveNetProbe"))){GetWorld()->SpawnActor<ALDObjectiveNetworkProbe>();}
    if(bScaffoldProfile&&FParse::Param(FCommandLine::Get(),TEXT("LDResetProbe"))){GetWorld()->SpawnActor<ALDResetProbe>();}
    if((bScaffoldProfile||bDevelopmentProfile)&&FParse::Param(FCommandLine::Get(),TEXT("LDSoakProbe"))){GetWorld()->SpawnActor<ALDSoakProbe>();}
#endif
}

void ALDGameMode::PreLogin(const FString& Options,const FString& Address,const FUniqueNetIdRepl& UniqueId,FString& ErrorMessage)
{
    Super::PreLogin(Options,Address,UniqueId,ErrorMessage);
    if(IsFinished()){ErrorMessage=TEXT("Demo finished. Host a new run.");}
    if (bDevelopmentProfile)
    {
        const auto* Public=GetGameState<ALDGameState>();
        if (!bEnvironmentProfile && Public && Public->bDevelopmentProfile && Public->bMatchStarted) { ErrorMessage=TEXT("Roster locked. Join the next lobby."); }
        else if (Contenders.Num()>=4) { ErrorMessage=TEXT("The v0.4 lobby has four contenders."); }
    }
}

void ALDGameMode::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);
    if (ALDPlayerController* PC=Cast<ALDPlayerController>(NewPlayer))
    {
#if !UE_BUILD_SHIPPING
        if((bScaffoldProfile||(bDevelopmentProfile&&(FParse::Param(FCommandLine::Get(),TEXT("LDDevelopmentProbe"))||(FParse::Param(FCommandLine::Get(),TEXT("LDSoakProbe"))||FParse::Param(FCommandLine::Get(),TEXT("LDB86Probe")))||FParse::Param(FCommandLine::Get(),TEXT("LDNavigationProbe"))||(FParse::Param(FCommandLine::Get(),TEXT("LDInputProbe"))||FParse::Param(FCommandLine::Get(),TEXT("LDInventoryProbe"))||(FParse::Param(FCommandLine::Get(),TEXT("LDVendorProbe"))||FParse::Param(FCommandLine::Get(),TEXT("LDTradeProbe")))))))&&PC->IsLocalController()&&FParse::Param(FCommandLine::Get(),TEXT("LDRemoteAuthority"))){return;}
#endif
        // PreLogin can precede a start request or another completed admission.
        // Check again before creating any private record or fresh reward budget.
        const auto* Public=GetGameState<ALDGameState>();
        if (bDevelopmentProfile && ((!bEnvironmentProfile&&Public&&Public->bDevelopmentProfile&&Public->bMatchStarted)||Contenders.Num()>=4))
        {
            const FText Reason=FText::FromString(TEXT("Roster locked or lobby full. Join the next lobby."));
            if (!GameSession || !GameSession->KickPlayer(PC,Reason)) { PC->Destroy(); }
            return;
        }
        FLDContenderRecord Record; Record.Session=FGuid::NewGuid();
        if (bDevelopmentProfile)
        {
            Record.State.bDevelopmentProfile=true;if(bV08Profile){Record.State.WardCharges=0;}
            Record.State.Build=bTacticalProfile?LDDevelopment::TacticalStart(0,TacticalCatalog->Checksum,RulesVersion()):LDDevelopment::StartingBuild(0,Catalog);
            Record.State.Hp=Record.State.MaxHp=LDDevelopment::MaxHP(Record.State.Build);
            if (!LobbyHost.IsValid()) { LobbyHost=PC; }
            Record.State.bLobbyHost=LobbyHost==PC;
        }
        Contenders.Add(PC,Record); SyncOwner(PC); if (Vision) { Vision->Register(PC); }
        if (bDevelopmentProfile) { RefreshBuild(PC,true); UpdateLobby(); }
        if(bEnvironmentProfile&&Inventory){Inventory->Initialize(PC);}
    }
}
void ALDGameMode::Logout(AController* Exiting)
{
    if(Combat){Combat->FlushPastCommand();}
    if (ALDPlayerController* PC=Cast<ALDPlayerController>(Exiting))
    {
        if(WorldRuntime){WorldRuntime->EndLife(PC);}
        if(bTacticalProfile){if(Combat){Combat->EndContentLife(Cast<ALDCharacter>(PC->GetPawn()));}if(Vision){Vision->EndLife(PC);}}if(Production){Production->EndLife(PC);}if(Trade){Trade->CancelFor(PC,TEXT("Cancelled · Life or connection ended"));}if(Inventory){Inventory->EndLife(PC,true);}
        if(Development){Development->DropBooks(PC,PC->GetPawn()?PC->GetPawn()->GetActorLocation():LDRegion::Site(GetWorld(),LDObjective::Guild));}
        if(Objectives){Objectives->Withdraw(PC,PC->GetPawn()?PC->GetPawn()->GetActorLocation():LDRegion::Site(GetWorld(),LDObjective::Guild),RuleTime());}
        if(Economy){Economy->Retire(PC);}
        if (Vision) { Vision->Unregister(PC); } Contenders.Remove(PC);
        if (LobbyHost==PC) { LobbyHost.Reset(); for (auto& Pair:Contenders) { if (Pair.Key.IsValid()) { LobbyHost=Pair.Key; Pair.Value.State.bLobbyHost=true; SyncOwner(Pair.Key.Get()); break; } } }
        if (bDevelopmentProfile) { UpdateLobby(); }
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
    { State->Match=Vision ? Vision->Generation : 0; ++State->Revision; PC->OwnerState=*State;if(PC->IsLocalController()){PC->OnRep_OwnerState();}PC->ForceNetUpdate(); }
}

void ALDGameMode::EndContenderLife(ALDPlayerController* PC,double Now)
{
    if(Combat){Combat->FlushPastCommand();}
    FLDOwnerState* State=StateFor(PC);
    if (!State || !State->bActive || IsFinished()) { return; }
    if(WorldRuntime){WorldRuntime->EndLife(PC);}
    if(bTacticalProfile){if(Development){Development->Cancel(PC,TEXT("Death cancelled preparation"));}Combat->CancelTactical(Cast<ALDCharacter>(PC->GetPawn()),TEXT("Life ended"));}
    if(bTacticalProfile){if(Combat){Combat->EndContentLife(Cast<ALDCharacter>(PC->GetPawn()));}if(Vision){Vision->EndLife(PC);}}if(Production){Production->EndLife(PC);}if(Trade){Trade->CancelFor(PC,TEXT("Cancelled · Life or connection ended"));}if(Inventory){Inventory->EndLife(PC);}
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
{const auto* GS=GetGameState<ALDGameState>();return GS&&GS->EndReason!=0;}
double ALDGameMode::RuleTime() const
{const auto* GS=GetGameState<ALDGameState>();return GS&&GS->EndReason?GS->MatchEndedAt:GetWorld()->GetTimeSeconds();}

bool ALDGameMode::IsPlaying() const
{
    const auto* Public=GetGameState<ALDGameState>();
    return !IsFinished() && (!bDevelopmentProfile || (Public && Public->bDevelopmentProfile && Public->bMatchStarted));
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
{if(Economy){Economy->GoldChanged(Cause,Mint,Burn);}}
