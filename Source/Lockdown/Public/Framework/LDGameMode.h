#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "Framework/LDCaptureReducer.h"
#include "Framework/LDMatchProviders.h"
#include "World/LDCaptureCircle.h"
#include "World/LDCaptureMarker.h"
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
    // Capture/endgame public read models: the circle, the frozen result and the kill feed.
    UPROPERTY(ReplicatedUsing=OnRep_PublicMatch) FLDPublicMatchView PublicMatch;
    UFUNCTION() void OnRep_PublicMatch();
    UPROPERTY(ReplicatedUsing=OnRep_MatchResult) FLDResultView MatchResult;
    UFUNCTION() void OnRep_MatchResult();
    UPROPERTY(ReplicatedUsing=OnRep_KillFeed) FLDKillFeedView KillFeed;
    UFUNCTION() void OnRep_KillFeed();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    // Local presentation only: the visible flagpole boundary of the current match, never replicated.
    const ALDCaptureMarker* GetCaptureMarker() const { return CaptureMarker.Get(); }
private:
    // Builds the boundary from this machine's world data for an active public match, and removes it otherwise.
    void SyncCaptureMarker();
    TWeakObjectPtr<ALDCaptureMarker> CaptureMarker;
    uint32 CaptureMarkerFailedGeneration = 0;
    FLDLobbyView LatestDemoLobby;
    FLDDemoClockView LatestDemoClock;
    FLDPublicMatchView LatestPublicMatch;
    FLDResultView LatestMatchResult;
    FLDKillFeedView LatestKillFeed;
};

class ALDVisionDirector;
class ALDCombatDirector;
class ALDPlayerController;
class ALDCharacter;
class FLDDemoContentSnapshot;
struct FWorldContext;
struct FLDNativeMotionBatch;
struct FLDCommittedDeath;
struct FLDAggressiveIntent;
struct FLDProtectionReceipt;
struct FLDBodyCombatState;
// One camp member body prepared by the camp owner for a group-atomic life commit.
struct FLDCampMemberSpawn
{
    FGuid EntityId;
    ALDCharacter* Body=nullptr;
    FName MonsterId;
};
struct FLDContenderRecord
{
    FGuid Session;
    FLDOwnerState State;
    FString Name;
    ELDContenderKind Kind = ELDContenderKind::Human;
    uint64 ReadyRosterRevision = 0;
    FLDSessionRequestWindow SessionRequests;
    // Result accounting: kept after an ordinary leave until the room returns to the Lobby.
    bool bLeft = false;
    uint64 Kills = 0;
    uint64 Deaths = 0;
    uint64 Assists = 0;
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
    virtual FString InitNewPlayer(APlayerController* NewPlayerController,const FUniqueNetIdRepl& UniqueId,const FString& Options,const FString& Portal=TEXT("")) override;
    virtual void PostLogin(APlayerController* NewPlayer) override;
    virtual void Logout(AController* Exiting) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
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
    bool IsFinished() const;
    // --- Capture, result and match lifecycle (capture/endgame module) ---
    // Next integer boundary the authority driver must stop at; MAX_int64 when none.
    int64 NextCaptureDueMs() const;
    // Right after Motion: whether this step's motion completes a guard strictly before the step's end (a guard a
    // sole sprayer paused resumes). If so the driver skips the later gameplay phases of the step, retries owed
    // rewards, and Capture/Close lock the result at that earlier instant.
    bool CaptureCompletesInsideStep();
    // Driver phase entries, called in order after Transactions with the matching permit.
    bool ExecuteCapturePhase(const FLDGameplayWritePermit& Permit, FString& Error);
    // Locks the unique result and closes the write gate before any cleanup or publication.
    bool ExecuteClosePhase(const FLDGameplayWritePermit& Permit, FString& Error);
    // Permit-free cleanup and public projection after Close, also when Close shut the gate.
    bool ExecuteCapturePublish(const FLDAuthorityBoundary& Boundary, FString& Error);
    // Motion phase: the native batch just accepted for this body.
    ELDCommitReject PublishCaptureMotion(const FLDGameplayWritePermit& Permit, const FLDNativeMotionBatch& Batch);
    // Life provider: Joined/Respawned/ProtectionEnded/Died facts; Left is core-only.
    ELDCommitReject PublishCaptureEligibility(const FLDGameplayWritePermit& Permit, const FLDCaptureEligibilityEvent& Event);
    // Combat's control path (Actions or Effects permit): a root, stun, taunt or knockback just committed on this
    // living contender body at the permit's boundary instant. It clears that body's spray progress; disarm, slow
    // and plain damage never reach it.
    ELDCommitReject PublishCaptureInterrupt(const FLDGameplayWritePermit& Permit, const FLDCombatBodyRef& Body);
    void NotifyLifeCommitted(const FLDLifeNotice& Notice);
    FLDPublicMatchView ReadPublicMatch() const;
    FLDAIRegistrationResult RegisterAIContender(AController& Controller, uint8 RosterSlot, const FLDMatchContext& Match);
    FLDAISelfReadResult ReadAISelf(const FLDAIContenderBinding& Binding) const;
    UPROPERTY() TObjectPtr<ALDVisionDirector> Vision;
    UPROPERTY() TObjectPtr<ALDCombatDirector> Combat;
    // ---- Growth/Shop domain directors (spawned and retired by ALDEconomyDirector) ----
    UPROPERTY() TObjectPtr<class ALDInventoryDirector> Inventory;
    UPROPERTY() TObjectPtr<class ALDEconomyDirector> Economy;
    UPROPERTY() TObjectPtr<class ALDDevelopmentDirector> Development;
    UPROPERTY() TObjectPtr<class ALDVendorDirector> Vendor;
    // ---- end Growth/Shop ----
    // ---- Combat and life (combat-orders module, Framework/LDGameModeLife.cpp) ----
    // The only protection-removal entry: an admitted aggressive intent removes protection before
    // the action itself is checked; a later action rejection never restores it.
    FLDProtectionReceipt EndProtectionForAggressiveIntent(AController& Caller, const FLDMatchContext& Match,
        const FLDCombatBodyRef& Body, const FLDAggressiveIntent& Intent, const FLDGameplayWritePermit& Permit);
    // Current contender controller binding (human or registered opponent AI); never a camp member.
    ALDPlayerController* FindContenderController(const FGuid& ContenderId) const;
    // Life-domain protection (immune to damage and control); Combat reads it at the hit point.
    bool IsContenderProtected(const FGuid& ContenderId) const;
    // Camps: registers members on first use and commits one whole group's new lives atomically
    // inside RespawnsAndWorld (no partial group). Bodies are prepared, hidden and unpossessed by camps.
    bool SpawnCampMembers(const TArray<FLDCampMemberSpawn>& Spawns, const FLDGameplayWritePermit& Permit,
        TArray<FLDCombatBodyRef>& OutBodies, FString& Error);
    // Combat provider hooks (Combat\LDCombatProvider.cpp): close cancels live orders; reset drops
    // old-match owner views and camp lives before the core retires bodies and the driver.
    void CloseCombatMatch(const FLDMatchClosed& Closed);
    bool ResetCombatMatch(const FLDMatchReset& Reset, FString& Error);
    // Camps: spawned and destroyed by the Camps match provider (World/LDCampDirector.cpp).
    UPROPERTY() TObjectPtr<class ALDCampDirector> Camps;
#if WITH_DEV_AUTOMATION_TESTS
    // DEV test session (Transactions permit, Development only): the flagpole opens at this boundary's Capture phase
    // instead of after the full development time (FLDCaptureReducer::DevOpenAt); spray, guard and every rule stay.
    // bOutsideDevelopment when the match is in another phase; false with Detail when nothing changed.
    bool DevOpenCaptureNow(const FLDGameplayWritePermit& Permit, bool& bOutsideDevelopment, FString& Detail);
    // DEV telemetry only: the current match's reduced spray and guard changes; empty outside a match.
    const FLDCaptureDevTally& DevCaptureTally() const { return CaptureReducer.DevTally(); }
#endif
private:
    friend class FLDDemoSessionTest;
    friend class FLDOwnerViewTeardownTest;
    friend class FLDCaptureSessionTest;
    friend class FLDDemoCoreTest;
    friend class FLDDemoCoreProbe;
    friend class FLDRewardSettlementTest;
    friend class FLDCaptureInsideStepTest;
    friend class FLDCaptureCombatInterruptTest;
    friend class FLDDemoMechanicsEffectLifetimesTest;
    friend class FLDCombatCommitControlTest;
    friend class FLDAccountRetireAwaitsRewardTest;
    friend class FLDDemoDevTestCommandsTest;
    friend class FLDDemoAssistSettlementTest;
    friend class FLDDemoGrowthRerollTest;
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
        // Public identity slot (locked roster position 0..5) for every body of this contender in this match.
        uint8 PresentationSlot=MAX_uint8;
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
    // A settlement boundary repeats the instant of a held capture completion: DeathsAndRewards retries, then Close.
    bool bSettlementStep = false;
    // This step's motion completes the capture before the step's end; set after Motion, reset per step.
    bool bCaptureCompletesInsideStep = false;
    ELDCommitPhase LastAuthorityPhase = ELDCommitPhase::Session;
    bool InitializeAuthorityCore(FString& Error);
    bool AvailableAuthorityTime(int64& ThroughMs, FString& Error);
    ELDCommitReject BeginAuthorityStep(ALDCombatDirector& Driver, const FLDMatchContext& Match, int64 EndAtMs);
    // Opens the settlement boundary of a held capture completion at the same authority instant.
    ELDCommitReject BeginSettlementStep(ALDCombatDirector& Driver, const FLDMatchContext& Match);
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
    // The Listen Host leaving closes the room for every member. Its map change shuts the net driver down before its
    // controller logs out, so the room closes when that map change begins, while the members' connections still exist.
    void HostWorldUnloading(const FWorldContext& Context, const FString& Map);
    void CloseRoomForHostDeparture(const ALDPlayerController* Host);
    FDelegateHandle HostTravelHandle;
    FRandomStream SpawnRandom;
    // ---- Combat and life (combat-orders module, Framework/LDGameModeLife.cpp) ----
    // Locked Combat death -> life ends once; the body leaves combat and the capture set, growth is kept.
    bool EndContenderLife(const FLDCommittedDeath& Fact, const FLDGameplayWritePermit& Permit, FString& Error);
    bool EndCampMemberLife(const FLDCommittedDeath& Fact, const FLDGameplayWritePermit& Permit, FString& Error);
    // Session: natural protection expiry at ProtectedUntilMs (half-open), before this boundary's hits.
    bool ExpireProtections(const FLDGameplayWritePermit& Permit, FString& Error);
    // RespawnsAndWorld: due contenders draw one of the ten anchors with equal probability.
    bool RespawnDueContenders(const FLDGameplayWritePermit& Permit, FString& Error);
    // Earliest future protection expiry or respawn, for the driver's step boundaries.
    int64 NextLifeDueMs() const;
    struct FLDPlacedBody
    {
        FGuid Contender;
        uint32 Life=0;
        TWeakObjectPtr<ALDPlayerController> Controller;
        TWeakObjectPtr<ALDCharacter> Body;
        TSharedPtr<const class FLDSpawnReservation> Reservation;
    };
    // Draws, reserves and physically places one contender body without committing any life state.
    bool PlaceContenderBody(const FGuid& Contender, uint32 ProposedLife, const FLDGameplayWritePermit& Permit,
        FLDPlacedBody& Out, FString& Error);
    void ReleasePlacedBody(FLDPlacedBody& Placed);
    // Life commit after every preparation succeeded; a failure here is an authority fault, never a retry.
    bool CommitPlacedLife(FLDPlacedBody& Placed, FLDBodyCombatState&& CombatState, int64 ProtectedUntilMs,
        const FLDGameplayWritePermit& Permit, FString& Error);
    bool ActivatePlacedBody(FLDPlacedBody& Placed, const FLDGameplayWritePermit& Permit, FString& Error);
    // Installs one committed life's motion stream at its placed foot.
    void BeginLifeMotion(FLDLifeRecord& Life, const FLDCombatBodyRef& Ref, ALDCharacter& Body);
    // Capture eligibility fact of a contender life change. Published before the life mutation where the
    // change can still be refused; a refusal after a committed life is an authority fault.
    bool PublishEligibility(const FLDGameplayWritePermit& Permit, const FLDCaptureEligibilityEvent& Event, FString& Error);
    void NotifyLife(const FLDCombatBodyRef& Body, ELDLifeChange Change, uint64 LifeRevision, int64 AtMs);
    // Public cause of a contender death for the kill feed and K/D.
    bool KillCauseOf(const FLDCommittedDeath& Fact, ELDKillCause& OutCause, FGuid& OutCredit, FString& Error) const;
    // Start of each step: native batches of earlier steps were consumed when accepted (capture reads them
    // synchronously; camp members have no consumer), so the retained prefixes are released.
    void ReleaseConsumedMotion();
    static int32 PhaseOrder(ELDCommitPhase Phase);
    // --- Capture, result and match lifecycle (capture/endgame module) ---
    struct FLDCaptureTrack
    {
        FLDCombatBodyRef Body;
        bool bInside = false;
        uint64 RegisteredStep = 0;
        uint64 LastStep = 0;
    };
    FLDCaptureReducer CaptureReducer;
    FLDCaptureCircle CaptureCircle;
    TMap<FGuid, FLDCaptureTrack> CaptureTracks;
    TOptional<FLDCaptureProposal> PendingCapture;
    TSet<FLDDeathKey> CountedDeaths;
    TArray<FLDKillNotice> KillNotices;
    uint64 NextKillSerial = 0;
    bool bKillFeedDirty = false;
    FLDResultView ResultView;
    bool bResultLocked = false;
    bool bResultPublished = false;
    bool bPublicMatchDirty = false;
    bool bLobbyDirty = false;
    uint64 PublicMatchRevision = 0;
    int64 MatchStartedAtMs = 0;
    TArray<FGuid> LockedAISlots;
    bool InitializeCaptureMatch(FString& Error);
    FString StartGateBlocker() const;
    bool StartDemoMatch(ELDSessionReject& Reject, FString& Detail);
    bool RetireMatch(bool bAbortedStart, FString& Error);
    void RetireAuthorityCore();
    void RetireMatchDomains();
    // A locked death whose reward was refused as retryable is still owed; no result may lock over it.
    bool HasPendingRewardRetries() const;
    // Between steps: a completed capture whose boundary still owed a reward waits for its settlement boundary.
    bool IsCloseAwaitingSettlement() const { return PendingCapture.IsSet() && !bAuthorityStepOpen; }
    bool TryCloseMatch(ELDMatchEndReason Reason, const FGuid& Winner, int64 EndedAtMs, FString& Error);
    void PublishMatchOutcome();
    void PublishPublicMatch();
    void QueueLeaveFact(const FGuid& ContenderId);
    FLDContenderRecord* FindContender(const FGuid& ContenderId);
};
