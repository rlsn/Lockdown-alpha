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

ALDPlayerState::ALDPlayerState() { bOnlyRelevantToOwner = true; bAlwaysRelevant = false; }
void ALDGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{ Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(ALDGameState,bPublicSensorActive); }
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
    Vision=GetWorld()->SpawnActor<ALDVisionDirector>();
    Vision->Generation=++CastChecked<ULDGameInstance>(GetGameInstance())->AuthorityGeneration;
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
    if (bScaffoldProfile && FParse::Param(FCommandLine::Get(),TEXT("LDViewProbe"))) { GetWorld()->SpawnActor<ALDViewNetworkProbe>(); }
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
    { if (Vision) { Vision->Unregister(PC); } Contenders.Remove(PC); }
    Super::Logout(Exiting);
}
FLDOwnerState* ALDGameMode::StateFor(ALDPlayerController* PC)
{ FLDContenderRecord* Record=Contenders.Find(PC); return Record ? &Record->State : nullptr; }
void ALDGameMode::SyncOwner(ALDPlayerController* PC)
{
    if (FLDOwnerState* State=StateFor(PC))
    { State->Match=Vision ? Vision->Generation : 0; PC->OwnerState=*State; PC->ForceNetUpdate(); }
}
