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

UCLASS()
class LOCKDOWN_API ALDGameState : public AGameStateBase
{
    GENERATED_BODY()
public:
    UPROPERTY(ReplicatedUsing=OnRep_DemoLobby) FLDLobbyView DemoLobby;
    UFUNCTION() void OnRep_DemoLobby();
    UPROPERTY(ReplicatedUsing=OnRep_DemoClock) FLDDemoClockView DemoClock;
    UFUNCTION() void OnRep_DemoClock();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
private:
    FLDLobbyView LatestDemoLobby;
    FLDDemoClockView LatestDemoClock;
};

class ALDVisionDirector;
class ALDCombatDirector;
class ALDPlayerController;
class ALDCharacter;
class FLDDemoContentSnapshot;
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

UCLASS()
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
    bool ReportAuthorityFault(const FLDMatchContext& Match, const FString& Diagnostic);
    bool ReadContenderLife(const FLDMatchContext& Match, const FGuid& ContenderId, FLDContenderLifeRead& Out, FString& Error) const;
    bool ReadObservedPublicFacts(const FLDMatchContext& Match, const FLDCombatBodyRef& Body,
        FLDObservedPublicFacts& Out, FString& Error) const;
    bool ReadBodyMotionStream(const FLDMatchContext& Match, const FLDCombatBodyRef& Body, FGuid& Out, FString& Error) const;
    FLDLobbyView ReadLobby() const;
    FLDSessionReceipt ProcessSessionCommand(ALDPlayerController* Controller, const FLDSessionRequest& Request);
    FLDOwnerState* StateFor(ALDPlayerController* Controller);
    FGuid ContenderId(ALDPlayerController* Controller) const;
    void SyncOwner(ALDPlayerController* Controller);
    bool IsPlaying() const;
    void EconomyWallets(TMap<FName,int64>& Out) const;
    int32 EconomyContenderCount() const {return Contenders.Num();}
    bool IsFinished() const;
    UPROPERTY() TObjectPtr<ALDVisionDirector> Vision;
    UPROPERTY() TObjectPtr<ALDCombatDirector> Combat;
    UPROPERTY() TObjectPtr<class ALDInventoryDirector> Inventory;
    UPROPERTY() TObjectPtr<class ALDEconomyDirector> Economy;
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
    bool PublishAuthorityClock(FString& Error);
    ELDCommitReject CompleteNativeMotion(const FLDAuthorityStep& Step, const FLDCombatBodyRef& Body,
        const FLDGameplayWritePermit& Permit, TSharedPtr<const FLDNativeMotionBatch> Batch);
    bool AcknowledgeNativeMotion(const FLDMatchContext& Match, const FLDCombatBodyRef& Body,
        uint64 ThroughStep, uint64 ThroughSequence, FString& Error);
    bool SpawnInitialContenders(const FLDGameplayWritePermit& Permit, FString& Error);
    void FaultAuthority(const FString& Error);
    bool IsMemberPresent(const FGuid& EntityId) const;
    bool ProjectLife(ALDPlayerController& Controller, FLDOwnerState& Out, FString& Error) const;
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
    TWeakObjectPtr<ALDPlayerController> LobbyHost;
    TMap<TWeakObjectPtr<ALDPlayerController>,FLDContenderRecord> Contenders;
    FRandomStream SpawnRandom;
};
