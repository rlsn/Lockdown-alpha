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
    UPROPERTY(Replicated) uint8 DefensiveFlags = 15;
    UPROPERTY(Replicated) bool bClaimActive = false;
    UPROPERTY(Replicated) bool bContested = false;
    UPROPERTY(Replicated) uint8 ClaimRoute = 0;
    UPROPERTY(Replicated) float ClaimProgress = 0;
    UPROPERTY(Replicated) float MatchDeadline = 0;
    UPROPERTY(Replicated) uint8 EndReason = 0; // playing, Heart claimed, No claim
    UPROPERTY(Replicated) uint8 WinningRoute = 0;
    UPROPERTY(Replicated) float MatchEndedAt = 0;
    UPROPERTY(Replicated) FGuid ResultReceipt;
    UPROPERTY(Replicated) uint32 ResultSerial = 0;
    UPROPERTY(Replicated) uint32 SealNoticeSerial = 0;
    UPROPERTY(Replicated) uint8 SealNoticeKind = 0;
    UPROPERTY(Replicated) uint8 SealNoticeDistrict = 0;
    UPROPERTY(Replicated) bool bDevelopmentProfile = false;
    UPROPERTY(Replicated) bool bMatchStarted = true;
    UPROPERTY(Replicated) uint8 LobbyCount = 0;
    UPROPERTY(Replicated) uint8 ReadyCount = 0;
    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
};

class ALDVisionDirector;
class ALDCombatDirector;
class ALDObjectiveDirector;
class ALDDevelopmentDirector;
class ALDPlayerController;
struct FLDContenderRecord { FGuid Session; FLDOwnerState State; };

UCLASS(Config=Game)
class LOCKDOWN_API ALDGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    ALDGameMode();
    virtual void InitGame(const FString& MapName,const FString& Options,FString& ErrorMessage) override;
    virtual void StartPlay() override;
    virtual void RestartPlayer(AController* NewPlayer) override;
    virtual void PreLogin(const FString& Options,const FString& Address,const FUniqueNetIdRepl& UniqueId,FString& ErrorMessage) override;
    virtual void PostLogin(APlayerController* NewPlayer) override;
    virtual void Logout(AController* Exiting) override;
    FLDOwnerState* StateFor(ALDPlayerController* Controller);
    void SyncOwner(ALDPlayerController* Controller);
    void EndContenderLife(ALDPlayerController* Controller,double Now);
    void ReturnDueContenders(double Now);
    bool SetLobbyChoice(ALDPlayerController* Controller,uint8 ClassId,bool Ready,FString& Reason);
    bool StartDevelopmentMatch(ALDPlayerController* Controller,FString& Reason);
    void AwardExperience(ALDPlayerController* Controller,int64 Amount);
    void RefreshBuild(ALDPlayerController* Controller,bool RestoreFullHP=false);
    bool IsPlaying() const;
    bool IsDevelopmentProfile() const {return bDevelopmentProfile;}
    const ULDCharacterCatalog* CharacterCatalog() const {return Catalog;}
    bool IsFinished() const;
    double RuleTime() const;
    double StartedAt() const {return MatchStartedAt;}
    UPROPERTY() TObjectPtr<ALDVisionDirector> Vision;
    UPROPERTY() TObjectPtr<ALDCombatDirector> Combat;
    UPROPERTY() TObjectPtr<ALDObjectiveDirector> Objectives;
    UPROPERTY() TObjectPtr<ALDDevelopmentDirector> Development;
    UPROPERTY(Config) bool bScaffoldProfile = true;
private:
    bool bDevelopmentProfile = false;
    UPROPERTY() TObjectPtr<ULDCharacterCatalog> Catalog;
    TWeakObjectPtr<ALDPlayerController> LobbyHost;
    void UpdateLobby();
    TMap<TWeakObjectPtr<ALDPlayerController>,FLDContenderRecord> Contenders;
    TArray<FVector> SpawnPool;
    FRandomStream SpawnRandom;
    double MatchStartedAt = 0;
    FVector TakeSpawn();
    FVector ReturnSpawn(ALDPlayerController* Controller);
};
