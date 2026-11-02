#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "Framework/LDMatchLifecycleTypes.h"
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
    UPROPERTY(ReplicatedUsing=OnRep_DemoLobby) FLDLobbyView DemoLobby;
    UFUNCTION() void OnRep_DemoLobby();
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
    UPROPERTY(Replicated) bool bEnvironmentProfile = false;
    UPROPERTY(Replicated) bool bMatchStarted = false;
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
private:
    FLDLobbyView LatestDemoLobby;
};

class ALDVisionDirector;
class ALDCombatDirector;
class ALDObjectiveDirector;
class ALDDevelopmentDirector;
class ALDWorldChecks;
class ALDRegionDirector;
class ALDPlayerController;
class ALDCharacter;
class FLDDemoContentSnapshot;
struct FLDLegalGround;
struct FLDNativeMotionBatch;
struct FLDContenderRecord
{
    FGuid Session;
    FLDOwnerState State;
    FString Name;
    ELDContenderKind Kind = ELDContenderKind::Human;
    uint64 ReadyRosterRevision = 0;
    FLDSessionRequestWindow SessionRequests;
};

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
    const FLDMatchContext& GetMatchContext() const { return MatchContext; }
    bool IsDemoRoom() const { return bDemoWorld && bEntryAccepted; }
    bool IsGameplayWriteOpen(const FLDMatchContext& Context) const;
    const FLDAuthorityStep& GetAuthorityStep() const { return AuthorityStep; }
    double GetAuthorityNowSeconds() const { return AuthorityNowSeconds; }
    int64 GetAuthorityNowMs() const { return AuthorityNowMs; }
    ELDCommitReject ValidateWritePermit(const FLDGameplayWritePermit& Permit, ELDCommitPhase Phase) const;
    ELDCommitReject ValidateMotionStep(const FLDAuthorityStep& Step, const FLDCombatBodyRef& Body) const;
    ELDCommitReject ResolveBody(const FLDMatchContext& Match, const FLDCombatBodyRef& Body, ALDCharacter*& Out) const;
    bool ReadBodyRef(const ALDCharacter& Body, FLDCombatBodyRef& Out) const;
    bool CaptureIncomingTime(int64& NotBeforeMs, FString& Error) const;
    bool ReadContenderLife(const FLDMatchContext& Match, const FGuid& ContenderId, FLDContenderLifeRead& Out, FString& Error) const;
    bool ReadBodyMotionStream(const FLDMatchContext& Match, const FLDCombatBodyRef& Body, FGuid& Out, FString& Error) const;
    FLDLobbyView ReadLobby() const;
    FLDSessionReceipt ProcessSessionCommand(ALDPlayerController* Controller, const FLDSessionRequest& Request);
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
    bool IsEnvironmentProfile() const {return bEnvironmentProfile;}
    bool IsPopulationProfile() const {return bPopulationProfile;}
    UPROPERTY() TObjectPtr<class ALDB92Roster> Population;
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
    UPROPERTY() TObjectPtr<class ALDEconomyDirector> Economy;
    UPROPERTY() TObjectPtr<class ALDBankDirector> Bank;
    UPROPERTY() TObjectPtr<class ALDCommerceDirector> Commerce;
    UPROPERTY() TObjectPtr<class ALDAcquisitionDirector> Acquisition;
    UPROPERTY() TObjectPtr<class ALDProductionDirector> Production;
    UPROPERTY(Config) bool bScaffoldProfile = false;
private:
    friend class FLDDemoSessionTest;
    friend class FLDDemoCoreTest;
    friend class FLDDemoCoreProbe;
    friend class ALDCombatDirector;
    friend class ULDCharacterMovementComponent;
    struct FLDLifeRecord
    {
        ELDVictimKind Kind = ELDVictimKind::Contender;
        ELDLifePhase Phase = ELDLifePhase::Unspawned;
        uint32 Life = 0;
        uint32 RetiredThroughLife = 0;
        uint64 Revision = 0;
        FLDCombatBodyRef Body;
        TWeakObjectPtr<ALDCharacter> Actor;
        int64 RespawnAtMs = 0;
        int64 ProtectedUntilMs = 0;
        uint64 CompletedMotionStep = 0;
        uint64 SpawnSequence=0;
        uint32 SpawnForLife=0;
        FName SpawnAnchor;
        int32 SpawnOffset=INDEX_NONE;
        FGuid MotionStreamId;
        FLDCombatBodyRef MotionBody;
        uint64 MotionThroughSequence=0;
        uint64 MotionAckStep=0;
        uint64 MotionAckSequence=0;
        FVector MotionLastFoot=FVector::ZeroVector;
        bool MotionGrounded=false;
        TWeakObjectPtr<class UPrimitiveComponent> MotionFloor;
        TArray<TSharedPtr<const FLDNativeMotionBatch>> NativeMotion;
    };
    TMap<FGuid, FLDLifeRecord> Lives;
    TSharedPtr<const FLDDemoContentSnapshot> DemoContent;
    FLDAuthorityStep AuthorityStep;
    FLDAuthorityBoundary AuthorityBoundary;
    double AuthorityOriginWorldSeconds = 0;
    double LastWorldClockSample = 0;
    double AuthorityNowSeconds = 0;
    int64 AuthorityNowMs = 0;
    uint64 GateRevision = 1;
    const FLDGameplayWritePermit* ActiveWritePermit = nullptr;
    bool bAuthorityInitialized = false;
    bool bAuthorityStepOpen = false;
    bool bAuthorityPhaseExecuted = false;
    ELDCommitPhase LastAuthorityPhase = ELDCommitPhase::Session;
    bool InitializeAuthorityCore(FString& Error);
    bool AvailableAuthorityTime(int64& ThroughMs, FString& Error);
    ELDCommitReject BeginAuthorityStep(ALDCombatDirector& Driver, const FLDMatchContext& Match, int64 EndAtMs);
    bool ExecuteAuthorityPhase(ALDCombatDirector& Driver, ELDCommitPhase Phase,
        TFunctionRef<bool(const FLDGameplayWritePermit&)> Execute, FString& Error);
    bool FinishAuthorityStep(ALDCombatDirector& Driver, FString& Error);
    ELDCommitReject CompleteNativeMotion(const FLDAuthorityStep& Step, const FLDCombatBodyRef& Body,
        const FLDGameplayWritePermit& Permit, TSharedPtr<const FLDNativeMotionBatch> Batch);
    bool AcknowledgeNativeMotion(const FLDMatchContext& Match, const FLDCombatBodyRef& Body,
        uint64 ThroughStep, uint64 ThroughSequence, FString& Error);
    bool SpawnInitialContenders(const FLDGameplayWritePermit& Permit, FString& Error);
    void FaultAuthority(const FString& Error);
    bool IsMemberPresent(const FGuid& EntityId) const;
    void ProjectLife(ALDPlayerController& Controller, FLDOwnerState& Out) const;
    void RemoveLifeBinding(const FGuid& EntityId);
    FLDMatchContext MatchContext;
    ELDMatchPhase MatchPhase = ELDMatchPhase::Faulted;
    uint64 RosterRevision = 0;
    uint64 LobbyPublicRevision = 0;
    bool bDemoWorld = false;
    bool bEntryAccepted = false;
    FString EntryFailure;
    FString EntryCatalogDigest;
    TArray<FGuid> RosterOrder;
    TArray<FGuid> LobbyAISlots;
    bool AdmitRoomMember(ALDPlayerController* Controller, FString& Reason);
    bool AdvanceRoster(FString& Reason);
    void PublishLobby();
    bool CheckDemoStartInputs(FString& Reason) const;
    friend class ALDB92Roster;
    friend class ALDB92RosterProbe;
    bool bPopulationProfile=false;
    int32 PopulationHumanLimit=4;
    bool bTacticalProfile=false;
    bool bEnvironmentProfile=false;
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
