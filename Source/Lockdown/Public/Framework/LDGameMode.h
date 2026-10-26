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

USTRUCT()
struct FLDBreachNotice
{
    GENERATED_BODY()
    UPROPERTY() uint8 Route=0;
    UPROPERTY() double WarningAt=0;
    UPROPERTY() double OpenAt=0;
    UPROPERTY() double CloseAt=0;
};
UCLASS()
class LOCKDOWN_API ALDGameState : public AGameStateBase
{
    GENERATED_BODY()
public:
    UPROPERTY(Replicated) TArray<FLDBreachNotice> Breaches;
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
    UPROPERTY(Replicated) bool bUIControlProfile = false;
    UPROPERTY(Replicated) bool bMatchStarted = true;
    UPROPERTY(Replicated) float EconomyNextTick=0;
    UPROPERTY(Replicated) uint8 ShortagePhase=0;
    UPROPERTY(Replicated) uint8 ShortageRemaining=1;
    UPROPERTY(Replicated) int32 CoreOrdersRemaining=0;
    UPROPERTY(Replicated) int32 SupplyOrdersRemaining=0;
    UPROPERTY(Replicated) FGuid CharterWindow;
    UPROPERTY(Replicated) uint8 CharterPhase=0; // idle, warning, open, closed
    UPROPERTY(Replicated) double CharterWarningAt=0;
    UPROPERTY(Replicated) double CharterOpenAt=0;
    UPROPERTY(Replicated) double CharterCloseAt=0;
    UPROPERTY(Replicated) uint8 OutcropOne=3;
    UPROPERTY(Replicated) uint8 OutcropTwo=3;
    UPROPERTY(Replicated) uint8 LobbyCount = 0;
    UPROPERTY(Replicated) uint8 ReadyCount = 0;
    UPROPERTY(Replicated) float PublicSensorOfflineUntil=0;
    UPROPERTY(Replicated) float PublicSensorLockoutUntil=0;
    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
};

class ALDVisionDirector;
class ALDCombatDirector;
class ALDObjectiveDirector;
class ALDDevelopmentDirector;
class ALDWorldChecks;
class ALDRegionDirector;
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
    FGuid ContenderId(ALDPlayerController* Controller) const;
    void SyncOwner(ALDPlayerController* Controller);
    void EndContenderLife(ALDPlayerController* Controller,double Now);
    void ReturnDueContenders(double Now);
    bool SetLobbyChoice(ALDPlayerController* Controller,uint8 ClassId,bool Ready,FString& Reason);
    bool StartDevelopmentMatch(ALDPlayerController* Controller,FString& Reason);
    void AwardExperience(ALDPlayerController* Controller,int64 Amount);
    void RefreshBuild(ALDPlayerController* Controller,bool RestoreFullHP=false);
    bool IsPlaying() const;
    void EconomyWallets(TMap<FName,int64>& Out) const;
    int32 EconomyContenderCount() const {return Contenders.Num();}
    void RecordEconomyGold(FName Cause,int64 Mint=0,int64 Burn=0);
    bool IsDevelopmentProfile() const {return bDevelopmentProfile;}
    bool IsTacticalProfile() const {return bTacticalProfile;}
    bool IsV08Profile() const {return bV08Profile;}
    uint8 RulesVersion() const {return bV08Profile?8:bTacticalProfile?7:6;}
    UPROPERTY() TObjectPtr<class ULDTacticalCatalog> TacticalCatalog;
    bool IsUIControlProfile() const {return bUIControlProfile;}
    const ULDCharacterCatalog* CharacterCatalog() const {return Catalog;}
    bool IsFinished() const;
    double RuleTime() const;
    double StartedAt() const {return MatchStartedAt;}
    UPROPERTY() TObjectPtr<class ALDWorldDirector> WorldRuntime;
    bool bDrainingEvents=false;
    UPROPERTY() TObjectPtr<ALDVisionDirector> Vision;
    UPROPERTY() TObjectPtr<ALDCombatDirector> Combat;
    UPROPERTY() TObjectPtr<ALDObjectiveDirector> Objectives;
    UPROPERTY() TObjectPtr<ALDDevelopmentDirector> Development;
    UPROPERTY() TObjectPtr<ALDWorldChecks> WorldChecks;
    UPROPERTY() TObjectPtr<ALDRegionDirector> Region;
    UPROPERTY() TObjectPtr<class ALDInventoryDirector> Inventory;
    UPROPERTY() TObjectPtr<class ALDVendorDirector> Vendor;
    UPROPERTY() TObjectPtr<class ALDTradeDirector> Trade;
    UPROPERTY() TObjectPtr<class ALDEconomyDirector> Economy;
    UPROPERTY() TObjectPtr<class ALDBankDirector> Bank;
    UPROPERTY() TObjectPtr<class ALDCommerceDirector> Commerce;
    UPROPERTY() TObjectPtr<class ALDAcquisitionDirector> Acquisition;
    UPROPERTY() TObjectPtr<class ALDProductionDirector> Production;
    UPROPERTY(Config) bool bScaffoldProfile = true;
private:
    bool bTacticalProfile=false;
    bool bV08Profile=false;
    bool bDevelopmentProfile = false;
    bool bUIControlProfile = false;
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
