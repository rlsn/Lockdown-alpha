#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "Vision/LDViewTypes.h"
#include "LDGameMode.generated.h"

UCLASS()
class LOCKDOWN_API ALDPlayerState : public APlayerState
{
    GENERATED_BODY()
public:
    ALDPlayerState();
};

UCLASS()
class LOCKDOWN_API ALDGameState : public AGameStateBase
{
    GENERATED_BODY()
public:
    UPROPERTY(Replicated) bool bPublicSensorActive = true;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
};

class ALDVisionDirector;
class ALDCombatDirector;
class ALDPlayerController;
struct FLDContenderRecord { FGuid Session; FLDOwnerState State; };

UCLASS(Config=Game)
class LOCKDOWN_API ALDGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    ALDGameMode();
    virtual void StartPlay() override;
    virtual void RestartPlayer(AController* NewPlayer) override;
    virtual void PostLogin(APlayerController* NewPlayer) override;
    virtual void Logout(AController* Exiting) override;
    FLDOwnerState* StateFor(ALDPlayerController* Controller);
    void SyncOwner(ALDPlayerController* Controller);
    void EndContenderLife(ALDPlayerController* Controller,double Now);
    void ReturnDueContenders(double Now);
    UPROPERTY() TObjectPtr<ALDVisionDirector> Vision;
    UPROPERTY() TObjectPtr<ALDCombatDirector> Combat;
    UPROPERTY(Config) bool bScaffoldProfile = true;
private:
    TMap<TWeakObjectPtr<ALDPlayerController>,FLDContenderRecord> Contenders;
    TArray<FVector> SpawnPool;
    FRandomStream SpawnRandom;
    double MatchStartedAt = 0;
    FVector TakeSpawn();
    FVector ReturnSpawn(ALDPlayerController* Controller);
};
