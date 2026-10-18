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

ALDPlayerState::ALDPlayerState() { bOnlyRelevantToOwner = true; bAlwaysRelevant = false; }
void ALDGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(ALDGameState,bPublicSensorActive);
    DOREPLIFETIME(ALDGameState,DefensiveFlags);DOREPLIFETIME(ALDGameState,bClaimActive);DOREPLIFETIME(ALDGameState,bContested);
    DOREPLIFETIME(ALDGameState,ClaimRoute);DOREPLIFETIME(ALDGameState,ClaimProgress);DOREPLIFETIME(ALDGameState,MatchDeadline);
    DOREPLIFETIME(ALDGameState,EndReason);DOREPLIFETIME(ALDGameState,WinningRoute);DOREPLIFETIME(ALDGameState,MatchEndedAt);
    DOREPLIFETIME(ALDGameState,ResultReceipt);DOREPLIFETIME(ALDGameState,ResultSerial);DOREPLIFETIME(ALDGameState,SealNoticeSerial);
    DOREPLIFETIME(ALDGameState,SealNoticeKind);DOREPLIFETIME(ALDGameState,SealNoticeDistrict);
}
void ALDGameState::BeginPlay()
{
    Super::BeginPlay();
    if(GetNetMode()==NM_DedicatedServer||GetWorld()->GetMapName().Contains(TEXT("L_Frontend"))){return;}
    for(uint8 Site=1;Site<=3;++Site){if(auto* Landmark=GetWorld()->SpawnActor<ALDObjectiveSite>()){Landmark->Configure(Site);}}
}
ALDGameMode::ALDGameMode()
{
    DefaultPawnClass = ALDCharacter::StaticClass();
    PlayerControllerClass = ALDPlayerController::StaticClass();
    PlayerStateClass = ALDPlayerState::StaticClass();
    GameStateClass = ALDGameState::StaticClass();
    SpawnRandom.Initialize(int32(FPlatformTime::Cycles()));
}

FVector ALDGameMode::TakeSpawn()
{
    if (SpawnPool.IsEmpty())
    {
        SpawnPool = {{-1700,-1000,100},{-1300,-1000,100},{-500,-1000,100},{-200,700,100},
            {-1800,0,100},{100,1000,100},{1000,1000,100},{1900,-500,100},{1900,1000,100},
            {-600,600,100},{-1300,1000,100},{100,-900,100},{0,0,100},{-1900,-1000,100}};
    }
    const int32 Index = SpawnRandom.RandRange(0,SpawnPool.Num()-1);
    const FVector Location = SpawnPool[Index];
    SpawnPool.RemoveAtSwap(Index);
    return Location;
}

void ALDGameMode::RestartPlayer(AController* NewPlayer)
{
    if (GetWorld()->GetMapName().Contains(TEXT("L_Frontend"))) { return; }
    RestartPlayerAtTransform(NewPlayer, FTransform(FRotator::ZeroRotator, TakeSpawn()));
}

void ALDGameMode::StartPlay()
{
    Super::StartPlay();
    if (GetWorld()->GetMapName().Contains(TEXT("L_Frontend"))) { return; }
    MatchStartedAt=GetWorld()->GetTimeSeconds();
    Combat=GetWorld()->SpawnActor<ALDCombatDirector>();
    Vision=GetWorld()->SpawnActor<ALDVisionDirector>();
    Vision->AddTickPrerequisiteActor(Combat);
    Vision->Generation=++CastChecked<ULDGameInstance>(GetGameInstance())->AuthorityGeneration;
    Objectives=GetWorld()->SpawnActor<ALDObjectiveDirector>();
    for (FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
    { if (ALDPlayerController* PC=Cast<ALDPlayerController>(It->Get())) { SyncOwner(PC); Vision->Register(PC); } }
    if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
    {
        for (TActorIterator<ANavMeshBoundsVolume> It(GetWorld()); It; ++It) { Nav->OnNavigationBoundsUpdated(*It); }
        Nav->GetDefaultNavDataInstance(FNavigationSystem::Create);
        Nav->Build();
    }
    for (int32 Index=0; Index<8; ++Index)
    {
        GetWorld()->SpawnActor<ALDResident>(TakeSpawn(), FRotator::ZeroRotator);
    }
#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(), TEXT("LDMovementProbe"))) { GetWorld()->SpawnActor<ALDMovementProbe>(); }
    if(bScaffoldProfile&&FParse::Param(FCommandLine::Get(),TEXT("LDCombatProbe"))){GetWorld()->SpawnActor<ALDCombatProbe>();}
    if (bScaffoldProfile && FParse::Param(FCommandLine::Get(),TEXT("LDViewProbe"))) { GetWorld()->SpawnActor<ALDViewNetworkProbe>(); }
    if(bScaffoldProfile&&FParse::Param(FCommandLine::Get(),TEXT("LDCombatNetProbe"))){GetWorld()->SpawnActor<ALDCombatNetworkProbe>();}
    if(bScaffoldProfile&&FParse::Param(FCommandLine::Get(),TEXT("LDObjectiveProbe"))){GetWorld()->SpawnActor<ALDObjectiveProbe>();}
    if(bScaffoldProfile&&FParse::Param(FCommandLine::Get(),TEXT("LDObjectiveNetProbe"))){GetWorld()->SpawnActor<ALDObjectiveNetworkProbe>();}
#endif
}

void ALDGameMode::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);
    if (ALDPlayerController* PC=Cast<ALDPlayerController>(NewPlayer))
    {
        FLDContenderRecord Record; Record.Session=FGuid::NewGuid();
        Contenders.Add(PC,Record); SyncOwner(PC); if (Vision) { Vision->Register(PC); }
    }
}
void ALDGameMode::Logout(AController* Exiting)
{
    if (ALDPlayerController* PC=Cast<ALDPlayerController>(Exiting))
    {
        if(Objectives){Objectives->Withdraw(PC,PC->GetPawn()?PC->GetPawn()->GetActorLocation():LDObjective::Position(LDObjective::Guild),RuleTime());}
        if (Vision) { Vision->Unregister(PC); } Contenders.Remove(PC);
    }
    Super::Logout(Exiting);
}
FLDOwnerState* ALDGameMode::StateFor(ALDPlayerController* PC)
{ FLDContenderRecord* Record=Contenders.Find(PC); return Record ? &Record->State : nullptr; }
void ALDGameMode::SyncOwner(ALDPlayerController* PC)
{
    if (FLDOwnerState* State=StateFor(PC))
    { State->Match=Vision ? Vision->Generation : 0; ++State->Revision; PC->OwnerState=*State; PC->ForceNetUpdate(); }
}

void ALDGameMode::EndContenderLife(ALDPlayerController* PC,double Now)
{
    FLDOwnerState* State=StateFor(PC);
    if (!State || !State->bActive || IsFinished()) { return; }
    if(Objectives){Objectives->EndLife(PC,PC->GetPawn()?PC->GetPawn()->GetActorLocation():LDObjective::Position(LDObjective::Guild),Now);}
    const auto Cost=LDRules::DeathCost(LDRules::LevelFromXp(State->Xp),State->Gold,FMath::Max(0.,Now-MatchStartedAt));
    State->LastLostGold=Cost.LostGold; State->Gold-=Cost.LostGold;
    State->bActive=false; State->Hp=0; State->WardCharges=0;
    State->ReturnAt=float(Now+Cost.DelaySeconds); State->WarningUntil=0; State->ActionCue=0;
    ++State->DeathSerial;
    PC->UnPossess(); SyncOwner(PC); PC->ClientLifeReceipt(*State);
    UE_LOG(LogTemp,Display,TEXT("LD_DEATH_RECEIPT life=%u death=%u loss=%lld return=%.3f"),State->Life,State->DeathSerial,State->LastLostGold,State->ReturnAt);
}
FVector ALDGameMode::ReturnSpawn(ALDPlayerController* PC)
{
    const TArray<FVector> Candidates={{-1700,-1000,100},{-1300,-1000,100},{-500,-1000,100},{-200,700,100},
        {-1800,0,100},{100,1000,100},{1000,1000,100},{1900,-500,100},{1900,1000,100},
        {-600,600,100},{-1300,1000,100},{100,-900,100},{0,0,100},{-1900,-1000,100}};
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
        ++State.Life; State.bActive=true; State.Hp=100; State.ReturnAt=0; State.ProtectedUntil=float(Now+4);
        State.WarningUntil=0; State.DamageUntil=0; State.ExposedUntil=0; State.ActionCue=0;
        RestartPlayerAtTransform(PC,FTransform(FRotator::ZeroRotator,ReturnSpawn(PC)));
        if(!PC->GetPawn()){State.bActive=false;State.ReturnAt=float(Now+.1);--State.Life;continue;}
        if(auto* Body=Cast<ALDCharacter>(PC->GetPawn())){if(auto* S=Combat->StateFor(Body)){S->ProtectionUntil=State.ProtectedUntil;}}
        SyncOwner(PC); PC->ClientLifeReceipt(State);
        UE_LOG(LogTemp,Display,TEXT("LD_RETURN_RECEIPT life=%u protection=%.3f"),State.Life,State.ProtectedUntil);
    }
}
bool ALDGameMode::IsFinished() const
{const auto* GS=GetGameState<ALDGameState>();return GS&&GS->EndReason!=0;}
double ALDGameMode::RuleTime() const
{const auto* GS=GetGameState<ALDGameState>();return GS&&GS->EndReason?GS->MatchEndedAt:GetWorld()->GetTimeSeconds();}
