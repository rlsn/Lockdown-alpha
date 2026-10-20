#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Character/LDCharacter.h"
#include "Tests/LDMovementProbe.h"
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
    DOREPLIFETIME(ALDGameState,LobbyCount);DOREPLIFETIME(ALDGameState,ReadyCount);
    DOREPLIFETIME(ALDGameState,PublicSensorOfflineUntil);DOREPLIFETIME(ALDGameState,PublicSensorLockoutUntil);
}
void ALDGameState::BeginPlay()
{
    Super::BeginPlay();
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
    bDevelopmentProfile=UGameplayStatics::ParseOption(Options,TEXT("Profile"))==TEXT("V04")||MapName.Contains(TEXT("L_DevelopmentRegion"))||MapName.Contains(TEXT("L_RegionAlternate"));
    if (bDevelopmentProfile)
    {
        bScaffoldProfile=false;
        if (GameSession) { GameSession->MaxPlayers=4; }
        Catalog=ULDCharacterCatalog::Load(); FString Error;
        if (!Catalog || !Catalog->Validate(Error)) { ErrorMessage=TEXT("Invalid v0.4 character catalog: ")+Error; }
    }
    UE_LOG(LogTemp,Display,TEXT("LD_PROFILE %s"),bDevelopmentProfile?TEXT("V04 Integration: level5/slots4/1500s"):TEXT("Legacy V03 migration"));
}

FVector ALDGameMode::TakeSpawn()
{
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
    if((bScaffoldProfile||(bDevelopmentProfile&&FParse::Param(FCommandLine::Get(),TEXT("LDDevelopmentProbe"))))&&NewPlayer->IsLocalController()&&FParse::Param(FCommandLine::Get(),TEXT("LDRemoteAuthority"))){return;}
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
    Combat=GetWorld()->SpawnActor<ALDCombatDirector>();
    Vision=GetWorld()->SpawnActor<ALDVisionDirector>();
    Vision->AddTickPrerequisiteActor(Combat);
    Vision->Generation=++CastChecked<ULDGameInstance>(GetGameInstance())->AuthorityGeneration;
    Objectives=GetWorld()->SpawnActor<ALDObjectiveDirector>();
    if(bDevelopmentProfile){Development=GetWorld()->SpawnActor<ALDDevelopmentDirector>();WorldChecks=GetWorld()->SpawnActor<ALDWorldChecks>();}
    if(bDevelopmentProfile&&LDRegion::Definition(GetWorld())){Region=GetWorld()->SpawnActor<ALDRegionDirector>();}
    if (bDevelopmentProfile) { Public->MatchDeadline=0; UpdateLobby(); }
    for (FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
    { if (ALDPlayerController* PC=Cast<ALDPlayerController>(It->Get());PC&&StateFor(PC)) { SyncOwner(PC); Vision->Register(PC); } }
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
    if (bDevelopmentProfile&&FParse::Param(FCommandLine::Get(),TEXT("LDDevelopmentProbe"))) { GetWorld()->SpawnActor<ALDDevelopmentProbe>(); }
    if (FParse::Param(FCommandLine::Get(), TEXT("LDMovementProbe"))) { GetWorld()->SpawnActor<ALDMovementProbe>(); }
    if(bScaffoldProfile&&FParse::Param(FCommandLine::Get(),TEXT("LDCombatProbe"))){GetWorld()->SpawnActor<ALDCombatProbe>();}
    if (bScaffoldProfile && FParse::Param(FCommandLine::Get(),TEXT("LDViewProbe"))) { GetWorld()->SpawnActor<ALDViewNetworkProbe>(); }
    if(bScaffoldProfile&&FParse::Param(FCommandLine::Get(),TEXT("LDCombatNetProbe"))){GetWorld()->SpawnActor<ALDCombatNetworkProbe>();}
    if(bScaffoldProfile&&FParse::Param(FCommandLine::Get(),TEXT("LDObjectiveProbe"))){GetWorld()->SpawnActor<ALDObjectiveProbe>();}
    if(bScaffoldProfile&&FParse::Param(FCommandLine::Get(),TEXT("LDObjectiveNetProbe"))){GetWorld()->SpawnActor<ALDObjectiveNetworkProbe>();}
    if(bScaffoldProfile&&FParse::Param(FCommandLine::Get(),TEXT("LDResetProbe"))){GetWorld()->SpawnActor<ALDResetProbe>();}
    if(bScaffoldProfile&&FParse::Param(FCommandLine::Get(),TEXT("LDSoakProbe"))){GetWorld()->SpawnActor<ALDSoakProbe>();}
#endif
}

void ALDGameMode::PreLogin(const FString& Options,const FString& Address,const FUniqueNetIdRepl& UniqueId,FString& ErrorMessage)
{
    Super::PreLogin(Options,Address,UniqueId,ErrorMessage);
    if(IsFinished()){ErrorMessage=TEXT("Demo finished. Host a new run.");}
    if (bDevelopmentProfile)
    {
        const auto* Public=GetGameState<ALDGameState>();
        if (Public && Public->bDevelopmentProfile && Public->bMatchStarted) { ErrorMessage=TEXT("Roster locked. Join the next lobby."); }
        else if (Contenders.Num()>=4) { ErrorMessage=TEXT("The v0.4 lobby has four contenders."); }
    }
}

void ALDGameMode::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);
    if (ALDPlayerController* PC=Cast<ALDPlayerController>(NewPlayer))
    {
#if !UE_BUILD_SHIPPING
        if((bScaffoldProfile||(bDevelopmentProfile&&FParse::Param(FCommandLine::Get(),TEXT("LDDevelopmentProbe"))))&&PC->IsLocalController()&&FParse::Param(FCommandLine::Get(),TEXT("LDRemoteAuthority"))){return;}
#endif
        // PreLogin can precede a start request or another completed admission.
        // Check again before creating any private record or fresh reward budget.
        const auto* Public=GetGameState<ALDGameState>();
        if (bDevelopmentProfile && ((Public&&Public->bDevelopmentProfile&&Public->bMatchStarted)||Contenders.Num()>=4))
        {
            const FText Reason=FText::FromString(TEXT("Roster locked or lobby full. Join the next lobby."));
            if (!GameSession || !GameSession->KickPlayer(PC,Reason)) { PC->Destroy(); }
            return;
        }
        FLDContenderRecord Record; Record.Session=FGuid::NewGuid();
        if (bDevelopmentProfile)
        {
            Record.State.bDevelopmentProfile=true;
            Record.State.Build=LDDevelopment::StartingBuild(0,Catalog);
            Record.State.Hp=Record.State.MaxHp=LDDevelopment::MaxHP(Record.State.Build);
            if (!LobbyHost.IsValid()) { LobbyHost=PC; }
            Record.State.bLobbyHost=LobbyHost==PC;
        }
        Contenders.Add(PC,Record); SyncOwner(PC); if (Vision) { Vision->Register(PC); }
        if (bDevelopmentProfile) { RefreshBuild(PC,true); UpdateLobby(); }
    }
}
void ALDGameMode::Logout(AController* Exiting)
{
    if (ALDPlayerController* PC=Cast<ALDPlayerController>(Exiting))
    {
        if(Development){Development->DropBooks(PC,PC->GetPawn()?PC->GetPawn()->GetActorLocation():LDRegion::Site(GetWorld(),LDObjective::Guild));}
        if(Objectives){Objectives->Withdraw(PC,PC->GetPawn()?PC->GetPawn()->GetActorLocation():LDRegion::Site(GetWorld(),LDObjective::Guild),RuleTime());}
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
    { State->Match=Vision ? Vision->Generation : 0; ++State->Revision; PC->OwnerState=*State; PC->ForceNetUpdate(); }
}

void ALDGameMode::EndContenderLife(ALDPlayerController* PC,double Now)
{
    FLDOwnerState* State=StateFor(PC);
    if (!State || !State->bActive || IsFinished()) { return; }
    if(Development){Development->DropBooks(PC,PC->GetPawn()?PC->GetPawn()->GetActorLocation():LDRegion::Site(GetWorld(),LDObjective::Guild));}
    if(Objectives){Objectives->EndLife(PC,PC->GetPawn()?PC->GetPawn()->GetActorLocation():LDRegion::Site(GetWorld(),LDObjective::Guild),Now);}
    const auto Cost=bDevelopmentProfile
        ? LDRules::FDeathCost{LDDevelopment::DeathDelay(State->Build.Level,Now-MatchStartedAt),LDDevelopment::DeathLoss(State->Gold)}
        : LDRules::DeathCost(LDRules::LevelFromXp(State->Xp),State->Gold,FMath::Max(0.,Now-MatchStartedAt));
    State->LastLostGold=Cost.LostGold; State->Gold-=Cost.LostGold;
    State->bActive=false; State->Hp=0; State->WardCharges=0;
    State->ReturnAt=float(Now+Cost.DelaySeconds); State->WarningUntil=0; State->ActionCue=0;
    State->ActiveSkill=ELDSkill::None;State->SkillEndsAt=0;
    ++State->DeathSerial;
    PC->UnPossess(); SyncOwner(PC); PC->ClientLifeReceipt(*State);
    UE_LOG(LogTemp,Display,TEXT("LD_DEATH_RECEIPT life=%u death=%u loss=%lld return=%.3f"),State->Life,State->DeathSerial,State->LastLostGold,State->ReturnAt);
}
FVector ALDGameMode::ReturnSpawn(ALDPlayerController* PC)
{
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
        ++State.Life; State.bActive=true; State.Hp=bDevelopmentProfile?LDDevelopment::MaxHP(State.Build):100; State.ReturnAt=0; State.ProtectedUntil=float(Now+4);
        State.WarningUntil=0; State.DamageUntil=0; State.ExposedUntil=0; State.ActionCue=0;
        State.ActiveSkill=ELDSkill::None;State.SkillEndsAt=0;
        RestartPlayerAtTransform(PC,FTransform(FRotator::ZeroRotator,ReturnSpawn(PC)));
        if(!PC->GetPawn()){State.bActive=false;State.ReturnAt=float(Now+.1);--State.Life;continue;}
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
    if (!bDevelopmentProfile || !State || !Public || Public->bMatchStarted || ClassId>=3 || !Catalog)
    { Reason=TEXT("Class choice is available in the lobby only."); return false; }
    if (State->Build.ClassId!=ClassId)
    {
        const uint32 Revision=State->Build.Revision+1;
        State->Build=LDDevelopment::StartingBuild(ClassId,Catalog); State->Build.Revision=Revision;
        State->Xp=0; RefreshBuild(PC,true);
    }
    State->bReady=Ready; SyncOwner(PC); UpdateLobby();
    Reason=Ready?TEXT("Ready. Starting class remains private."):TEXT("Class selected. Mark Ready when prepared."); return true;
}

bool ALDGameMode::StartDevelopmentMatch(ALDPlayerController* PC,FString& Reason)
{
    auto* Public=GetGameState<ALDGameState>();
    if (!bDevelopmentProfile || !PC || PC!=LobbyHost.Get() || !Public || Public->bMatchStarted || Contenders.Num()<1 || Contenders.Num()>4)
    { Reason=TEXT("Only the lobby host can start once."); return false; }
    for (const auto& Pair:Contenders) { if (!Pair.Value.State.bReady) { Reason=TEXT("Every connected contender must be Ready."); return false; } }
    MatchStartedAt=GetWorld()->GetTimeSeconds(); Public->bMatchStarted=true;
    Public->MatchDeadline=float(MatchStartedAt+LDDevelopment::DeadlineSeconds);
    for (auto& Pair:Contenders)
    {
        if (auto* Player=Pair.Key.Get())
        {
            RefreshBuild(Player,true); Pair.Value.State.ProtectedUntil=float(MatchStartedAt+4);
            if (auto* Body=Cast<ALDCharacter>(Player->GetPawn())) { if (auto* S=Combat->StateFor(Body)) { S->ProtectionUntil=MatchStartedAt+4; } }
            SyncOwner(Player);
        }
    }
    Public->ForceNetUpdate(); Reason=Contenders.Num()==1?TEXT("Practice started: one human; 25 minute cap."):TEXT("Integration match started. Roster locked.");
    UE_LOG(LogTemp,Display,TEXT("LD_V04_START humans=%d practice=%d deadline=%.3f catalog=%s"),Contenders.Num(),Contenders.Num()==1,Public->MatchDeadline,*Catalog->SourceChecksum);
    return true;
}

void ALDGameMode::RefreshBuild(ALDPlayerController* PC,bool RestoreFullHP)
{
    auto* State=StateFor(PC); if (!State || !bDevelopmentProfile) { return; }
    State->MaxHp=LDDevelopment::MaxHP(State->Build);
    State->Hp=RestoreFullHP?State->MaxHp:FMath::Min(State->Hp,State->MaxHp);
    if (auto* Body=Cast<ALDCharacter>(PC->GetPawn()))
    {
        Body->GetCharacterMovement()->MaxWalkSpeed=float(180*LDDevelopment::SpeedFactor(State->Build));
        if (Combat) { if (auto* S=Combat->StateFor(Body)) { S->Hp=State->Hp; } }
    }
    SyncOwner(PC);
}

void ALDGameMode::AwardExperience(ALDPlayerController* PC,int64 Amount)
{
    auto* State=StateFor(PC); if (!State || Amount<=0) { return; }
    if (bDevelopmentProfile) { LDDevelopment::AwardXP(State->Build,State->Xp,Amount); }
    else { State->Xp+=Amount; }
    SyncOwner(PC);
}
