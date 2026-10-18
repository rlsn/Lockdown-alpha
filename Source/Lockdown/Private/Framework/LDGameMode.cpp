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

ALDPlayerState::ALDPlayerState() { bOnlyRelevantToOwner = true; bAlwaysRelevant = false; }
ALDGameMode::ALDGameMode()
{
    DefaultPawnClass = ALDCharacter::StaticClass();
    PlayerControllerClass = ALDPlayerController::StaticClass();
    PlayerStateClass = ALDPlayerState::StaticClass();
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
#endif
}
