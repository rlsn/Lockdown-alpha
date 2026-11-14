#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "Framework/LDMatchTimeline.h"
#include "Development/LDProgressionRules.h"
#include "Framework/LDCaptureReducer.h"
#include "Framework/LDMatchProviders.h"
#include "World/LDCaptureCircle.h"
#include "World/LDCaptureMarker.h"
#include "World/LDAirdrop.h"
#include "World/LDCharms.h"
#include "World/LDFieldMarkers.h"
#include "World/LDDormZones.h"
#include "World/LDLeaderNotice.h"
#include "World/LDDormMarker.h"
#include "World/LDAirdropMarker.h"
#include "World/LDCharmMarker.h"
#include "Vision/LDViewTypes.h"
#include "Core/LDLog.h"
#include "PK/LDPKRules.h"
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
    // Local presentation only: the dorm boundaries of the current match (D21, #79), never replicated.
    TArray<const ALDDormMarker*> GetDormMarkers() const;
    // Local presentation only: the current public airdrop (D26, #79), never replicated.
    const ALDAirdropMarker* GetAirdropMarker() const { return AirdropMarker.Get(); }
    // Local presentation only: one marker per lying public charm (#198, #226), in serial order, never replicated.
    TArray<const ALDCharmMarker*> GetCharmMarkers() const;
private:
    // The match phases whose public world events (airdrop, charms) are drawn in the scene of this machine.
    bool ShowsWorldEvents() const;
    // Builds the boundary from this machine's world data for an active public match, and removes it otherwise.
    void SyncCaptureMarker();
    TWeakObjectPtr<ALDCaptureMarker> CaptureMarker;
    uint32 CaptureMarkerFailedGeneration = 0;
    // The same for every dorm ring.
    void SyncDormMarkers();
    void ClearDormMarkers();
    TArray<TWeakObjectPtr<ALDDormMarker>> DormMarkers;
    uint32 DormMarkerFailedGeneration = 0;
    // The airdrop ring and crate, rebuilt for each drop serial.
    void SyncAirdropMarker();
    TWeakObjectPtr<ALDAirdropMarker> AirdropMarker;
    uint32 AirdropMarkerFailedGeneration = 0;
    // The charm rings and tokens, one per lying charm serial; removed as soon as the public view drops the charm.
    void SyncCharmMarkers();
    void ClearCharmMarkers();
    TArray<TWeakObjectPtr<ALDCharmMarker>> CharmMarkers;
    uint32 CharmMarkerFailedGeneration = 0;
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
    uint8 CampStage=0; // #97 growth stage of this life; the Boss is always 0
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
    bool bDisconnected = false;
    FGuid ReconnectTicket;
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
    virtual int32 GetNumPlayers() override;
    virtual void PreLogin(const FString& Options,const FString& Address,const FUniqueNetIdRepl& UniqueId,FString& ErrorMessage) override;
    virtual FString InitNewPlayer(APlayerController* NewPlayerController,const FUniqueNetIdRepl& UniqueId,const FString& Options,const FString& Portal=TEXT("")) override;
    virtual void PostLogin(APlayerController* NewPlayer) override;
    virtual void Logout(AController* Exiting) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    const FLDMatchContext& GetMatchContext() const { return MatchContext; }
    bool IsDemoRoom() const { return bDemoWorld && bEntryAccepted; }
    bool IsGameplayWriteOpen(const FLDMatchContext& Context) const;
    bool CanParkDisconnectedMember(ALDPlayerController& Controller) const;
    bool ParkDisconnectedMember(ALDPlayerController& Controller, FString& Error);
    bool ResumeRoomMember(ALDPlayerController& Controller, const FGuid& Ticket, FString& Error);
    void LeaveRoomMember(ALDPlayerController& Controller);
    // ---- #198 rest phases and field clock (Framework/LDGameModeRest.cpp) ----
    // A rest of the current match is running (any step); the world (camps, Boss, airdrop, leader) is frozen.
    bool IsRestPhase(const FLDMatchContext& Context) const;
    // The rest return countdown: build and every input (orders, attacks, casts, dash) are locked.
    // #198 session D: the PK ready countdown locks every input too, and while the PK runs only PK lives act (spectators
    // and the eliminated watch). Contender: the acting contender (invalid: judged as a non-PK life).
    bool IsInputLocked(const FLDMatchContext& Context, const FGuid& Contender = FGuid()) const;
    // Build-command stage for the growth domain: Live in the field, Rest in the open rest steps (build/training and the
    // payout), RestLocked in every other rest step, Closed otherwise.
    LDProgression::EMatchStage ReadBuildStage(const FLDMatchContext& Match) const;
    // Field progress T (ms) at the current authority instant; frozen outside the field, 0 before the first field.
    int64 GetFieldNowMs() const;
    bool IsFieldRunning() const;
    bool IsShowdown() const { return Timeline.GetStage() == ELDMatchStage::FinalField; }
    int64 NextShowdownDueMs() const;
    bool AdvanceShowdownEffects(const FLDGameplayWritePermit& Permit, FString& Error);
    // The authority ms at which T reaches FieldAtMs inside the running segment; MAX_int64 when the field is not running,
    // it is already reached, or it lies after this segment's end. For driver step planning.
    int64 FieldDueAuthorityMs(int64 FieldAtMs) const;
    // Next timeline boundary (rest step end or segment end); MAX_int64 in the final field or outside a match.
    int64 NextTimelineDueMs() const;
    const FLDMatchTimeline& GetTimeline() const { return Timeline; }
    // Field respawn wait of a death at field progress FieldT (approved 8/10/12/15 s by segment).
    int64 RespawnWaitAtFieldMs(int64 FieldT) const;
    // RespawnsAndWorld: settles every timeline transition due at this boundary except the build lock, which waits for
    // the same boundary's Transactions so operations committed at the deadline count. False only on an authority fault.
    bool AdvanceTimeline(const FLDGameplayWritePermit& Permit, FString& Error);
    // Transactions (after the growth commands of the boundary): locks the build and the PK entrants when due.
    bool LockTimelineBuild(const FLDGameplayWritePermit& Permit, FString& Error);
    // The field world pauses at a field -> rest boundary and resumes at rest -> field (RespawnsAndWorld permit). Other
    // world owners (charms, ...) add their own rest entry/exit here.
    bool EnterRestWorld(const FLDGameplayWritePermit& Permit, FString& Error);
    bool ExitRestWorld(const FLDGameplayWritePermit& Permit, FString& Error);
    // The shared PK sign-up disk in the rest room; false when the world or the contender's slot is unavailable.
    bool SignupDiskFor(const FGuid& Contender, FVector& OutCentreCm, double& OutRadiusCm) const;
    // The locked PK entrants of the current rest (sorted); empty before the lock or when PK was skipped.
    const TArray<FGuid>& GetLockedEntrants() const { return LockedEntrants; }
    // Session D: a running PK ended early at this boundary (a side won or a draw was decided).
    bool FinishRestPK(const FLDGameplayWritePermit& Permit, FString& Error);
    // Whether a contender currently has a shared-room rest life, not a field or PK life.
    bool IsInRestSpace(const FGuid& Contender) const;
    static FGuid RestIsolationGroup() { return FGuid(0x198, 0x52455354, 0x4c4f554e, 0x47450001); }
    ELDRestService NearbyRestService(const FGuid& Contender) const;
    bool CanUseRestService(const FGuid& Contender, ELDBuildCommandKind Kind) const;
    bool IsRestReady(const FGuid& Contender) const { return RestReady.Contains(Contender); }
    bool CanRestReady(const FGuid& Contender) const;
    ELDRestReadyResult SubmitRestReady(ALDPlayerController& Controller, uint32 Generation, uint8 RestIndex, int64 StartedAtMs, bool bReady);
    // ---- #198 session D: PK lives (Framework/LDGameModePK.cpp). A PK life is a rest-phase life in the arena: no capture
    // eligibility, no protection, no field respawn; its death returns the contender to its rest space at once. ----
    struct FLDPKPlacement
    {
        FGuid Contender;
        FName Anchor;
        FGuid Group;
        uint8 Side = 0;
    };
    // RespawnsAndWorld: each listed contender's rest life retires, then a PK life is prepared for every entrant at its
    // anchor in Group with Side (full resources, every cooldown ready). The bodies stand only together (bPlaced); while
    // any anchor is blocked nobody stands and the entry waits, tried again every boundary (approved outer parameters §1:
    // entry freezes while the scene is not ready). False only on an authority fault.
    bool EnterPKLives(const TArray<FLDPKPlacement>& Placements, const FLDGameplayWritePermit& Permit, bool& bPlaced, FString& Error);
    // RespawnsAndWorld: every living PK life retires and stands again in its rest space (blocked: pending placement).
    bool ReturnPKLivesToRest(const FLDGameplayWritePermit& Permit, FString& Error);
    bool IsPKLife(const FGuid& Contender) const { return PKLives.Contains(Contender); }
    // Whether a PK entry still waits for its bodies.
    bool IsPKEntryWaiting() const { return !PendingPKEntry.IsEmpty(); }
    // The current living body of a contender and its committed foot; false when it has none.
    bool ReadLivingBody(const FGuid& Contender, FLDCombatBodyRef& OutBody, FVector& OutFootCm) const;
    // Inside an authority step, after Motion: the native motion the contender's living body completed in this step;
    // null when it has none.
    const FLDNativeMotionBatch* ReadStepMotion(const FGuid& Contender) const;
    bool IsContenderPresent(const FGuid& Contender) const { return IsMemberPresent(Contender); }
    const TArray<FGuid>& GetRosterOrder() const { return RosterOrder; }
    // Shared training dummies keep each slot's generation-time level chassis and no build; they grant nothing.
    bool IsTrainingDummy(const FGuid& Entity) const { return TrainingDummyIds.Contains(Entity); }
    // Motion: the dummy integrates zero input (only forced motion such as a knockback moves it).
    bool AdvanceTrainingDummyMotion(const FLDCombatBodyRef& Body, const FLDGameplayWritePermit& Permit, FString& Error);
    // Owner-only: queues a reset of the owner's training (owner and dummy refilled, every cooldown ready, every training
    // effect and status cleared) for the next boundary.
    ELDTrainingResetResult SubmitTrainingReset(ALDPlayerController& Controller, uint32 Generation, uint32 Life, uint8 RestIndex, int64 StartedAtMs);
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
    // D21 (#79): the public dorm disks of this match; empty outside a running match.
    const FLDDormZones& GetDormZones() const { return DormZones; }
    // D26 (#79): the current airdrop of this match; inactive outside a running match.
    const FLDAirdropState& GetAirdrop() const { return Airdrop; }
    // RespawnsAndWorld: announces and lands the scheduled drop. False only on an authority fault.
    bool AdvanceAirdrop(const FLDGameplayWritePermit& Permit, FString& Error);
    // Transactions (growth applies only there or in DeathsAndRewards): the first living contender inside the pickup disk
    // of a landed drop claims it through the growth reward composite. False only on an authority fault.
    bool ClaimAirdrop(const FLDGameplayWritePermit& Permit, FString& Error);
    // Driver step planning: the next airdrop announcement or landing boundary, MAX_int64 when none.
    int64 NextAirdropDueMs() const;
    // #198 field charms (World/LDCharms.h, Framework/LDGameModeCharms.cpp), keyed to field progress T.
    const FLDCharmState& GetCharms() const { return Charms; }
    // RespawnsAndWorld, after AdvanceAirdrop and only while a field segment runs: expires charms and places due waves.
    // False only on an authority fault.
    bool AdvanceCharms(const FLDGameplayWritePermit& Permit, FString& Error);
    // Transactions: each lying charm goes to one eligible contender inside its disk and applies its effect through Combat.
    bool ClaimCharms(const FLDGameplayWritePermit& Permit, FString& Error);
    // RespawnsAndWorld at a field-to-rest boundary: every unclaimed charm leaves without any reward.
    bool EndCharmsForRest(const FLDGameplayWritePermit& Permit, FString& Error);
    // Driver step planning: the authority instant of the next charm wave or expiry in the running field, MAX_int64 when none.
    int64 NextCharmDueMs() const;
    // #198 outer §5 camp markers (World/LDFieldMarkers.h, Framework/LDGameModeFieldMarkers.cpp), keyed to field progress T.
    const FLDFieldMarkerState& GetFieldMarkers() const { return FieldMarkers; }
    // RespawnsAndWorld, only while a field segment runs: reveals the segment's markers when due. False only on a fault.
    bool AdvanceFieldMarkers(const FLDGameplayWritePermit& Permit, FString& Error);
    // RespawnsAndWorld at a field-to-rest boundary: the segment's unclaimed markers expire without any reward.
    bool EndFieldMarkersForRest(const FLDGameplayWritePermit& Permit, FString& Error);
    int64 NextFieldMarkerDueMs() const;
    // DeathsAndRewards (camp owner, after the death that cleared CampId at field progress FieldMs): the first clear of a
    // revealed marker's camp gives its credit owner the marker reward once. Without an eligible contender owner it stays open.
    bool ConsumeCampClear(FName CampId, const FGuid& ClearOwner, int64 FieldMs, const FLDGameplayWritePermit& Permit, FString& Error);
    // DeathsAndRewards or Transactions: pays one map event reward (#198 G) through the growth composite. A retryable
    // refusal stays owed and is retried at each later Transactions boundary; any other refusal is an authority fault.
    bool PayFieldReward(const FGuid& Recipient, FName Event, uint64 Serial, int64 XP, ELDFieldRewardKind Kind,
        const FLDGameplayWritePermit& Permit, FString& Error);
    // Transactions: retries every owed map event reward once.
    bool RetryFieldRewards(const FLDGameplayWritePermit& Permit, FString& Error);
    // RespawnsAndWorld: retires a living camp body (the Boss leaving the field) without a death: no kill credit, reward
    // or refresh. The camp owner destroys its actor afterwards.
    bool RetireCampBody(const FLDCombatBodyRef& Body, const FLDGameplayWritePermit& Permit, FString& Error);
    // D27 (#79) 上热搜: RespawnsAndWorld names the leader at each refresh boundary. False only on an authority fault.
    bool AdvanceLeaderNotice(const FLDGameplayWritePermit& Permit, FString& Error);
    int64 NextLeaderDueMs() const;
    const FLDLeaderState& GetLeaderNotice() const { return LeaderNotice; }
    // A world owner (the camp director's Boss) changed what the public match view shows.
    void MarkPublicMatchDirty() { bPublicMatchDirty = true; }
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
    // Server-only opening timing: draws once while a registered opponent binds during match preparation (#198: the moment
    // from which it makes its countdown build decisions). Seed records the stream state before the draw, so the plan can
    // be replayed without a new RNG.
    bool DrawOpponentOpeningPlan(const FLDAIContenderBinding& Binding, int64& AfterMs, int32& Seed, FString& Error);
    // ---- Playtest log (Framework/LDGameModeTrace.cpp; docs/DEVELOPMENT.md, playtest troubleshooting) ----
    // After each authority advance: one LD_HEARTBEAT line per contender once per authority second (LogLDTrace only).
    void LogPlaytestHeartbeat();
    // A human's mark key reached the Host (ALDPlayerController::ServerPlaytestMark): LD_PLAYER_MARK when Match is the running
    // match (LDPlaytest::InRunningMatch), otherwise LD_PLAYER_MARK_REJECTED; at most one line per second per contender.
    void LogPlaytestMark(ALDPlayerController& Controller, uint32 Match, uint32 Sequence, int64 ClientAuthorityMs);
    UPROPERTY() TObjectPtr<ALDVisionDirector> Vision;
    UPROPERTY() TObjectPtr<ALDCombatDirector> Combat;
    // ---- Growth domain directors (spawned and retired by ALDGrowthDirector) ----
    UPROPERTY() TObjectPtr<class ALDGrowthDirector> Growth;
    UPROPERTY() TObjectPtr<class ALDDevelopmentDirector> Development;
    // ---- end Growth ----
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
    // #198 session D: the optional PK provider's director of this match (PK/LDPKDirector.h); null without the provider.
    UPROPERTY() TObjectPtr<class ALDPKDirector> PKDirector;
#if WITH_DEV_AUTOMATION_TESTS
    // DEV test session: queues the final rest/return under the next world permit; arena entry and unlock stay real.
    // bOutsideDevelopment when the match is in another phase; false with Detail when nothing changed.
    bool DevOpenCaptureNow(const FLDGameplayWritePermit& Permit, bool& bOutsideDevelopment, FString& Detail);
    // DEV telemetry only: the current match's reduced spray and guard changes; empty outside a match.
    const FLDCaptureDevTally& DevCaptureTally() const { return CaptureReducer.DevTally(); }
#endif
private:
#if WITH_DEV_AUTOMATION_TESTS
    bool bDevShowdownEntryPending = false;
#endif
    friend class FLDDemoSessionTest;
    friend class FLDGroundBossBodyTest;
    friend class FLDOwnerViewTeardownTest;
    friend class FLDCaptureSessionTest;
    friend class FLDDemoCoreTest;
    friend class FLDDemoCoreProbe;
    friend class FLDRewardSettlementTest;
    friend class FLDCaptureInsideStepTest;
    friend class FLDCaptureCombatInterruptTest;
    friend class FLDDemoMechanicsEffectLifetimesTest;
    friend class FLDPlaytestLogHitVampTest;
    friend class FLDCombatCommitControlTest;
    friend class FLDDemoDevTestCommandsTest;
    friend class FLDDemoAssistSettlementTest;
    friend class FLDDemoGrowthBuildCommandsTest;
    friend struct FLDBuildPrimitivesAccess;
    friend class FLDRestPhaseTest;
    friend class FLDDemoOpponentProbe;
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
#if WITH_DEV_AUTOMATION_TESTS
    TFunction<void(ALDGameMode&)> DevFieldReturnCommitted;
#endif
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
    TMap<TWeakObjectPtr<ALDPlayerController>,FTimerHandle> PendingReconnects;
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
    // Where a placed life stands: a uniform spawn draw, a fixed spawn anchor (#198 private rest space) or near an old
    // field foot (#198 return to the field, falling back to a draw).
    struct FLDPlacementTarget
    {
        enum class EKind : uint8 { Draw, Anchor, Near };
        EKind Kind = EKind::Draw;
        FName Anchor;
        FVector FootCm = FVector::ZeroVector;
        FName Surface;
        double RadiusCm = 0;
    };
    // Draws, reserves and physically places one contender body without committing any life state.
    bool PlaceContenderBody(const FGuid& Contender, uint32 ProposedLife, const FLDGameplayWritePermit& Permit,
        FLDPlacedBody& Out, FString& Error, const FLDPlacementTarget& Target = FLDPlacementTarget());
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
    FLDDormZones DormZones;
    FLDAirdropState Airdrop;
    // #198 field charms of this match; inactive outside a running match.
    FLDCharmState Charms;
    // #198 outer §5 camp markers of this match; inactive outside a running match.
    FLDFieldMarkerState FieldMarkers;
    // Map event rewards refused as retryable, paid at the next Transactions boundaries.
    struct FLDOwedFieldReward
    {
        FGuid Recipient;
        FName Event;
        uint64 Serial = 0;
        int64 XP = 0;
        ELDFieldRewardKind Kind = ELDFieldRewardKind::None;
    };
    TArray<FLDOwedFieldReward> OwedFieldRewards;
    bool CommitOwedFieldReward(const FLDOwedFieldReward& Reward, const FLDGameplayWritePermit& Permit, FString& Error);
    FLDLeaderState LeaderNotice;
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
    // The authority instant the match started (R0 begins); the result duration and the public clock count from here.
    // Field-progress timers (camp growth/refresh, Boss, airdrop, leader notice, field respawn waits) use T instead.
    int64 MatchStartedAtMs = 0;
    FLDShowdownView Showdown;
    FLDPKParameters ShowdownRing;
    FLDShowdownParameters ShowdownRules;
    TMap<FGuid, int32> ShowdownRespawns;
    struct FLDShowdownBurn
    {
        uint32 Life = 0;
        int64 AccumulatorE7 = 0;
    };
    TMap<FGuid, FLDShowdownBurn> ShowdownBurn;
    int64 ShowdownRingSettledThroughMs = 0;
    int64 ShowdownRingTickAtMs = 0;
    bool bShowdownClosePending = false;
    bool BeginShowdown(const FLDGameplayWritePermit& Permit, FString& Error);
    bool CloseShowdownIfDue(const FLDGameplayWritePermit& Permit, FString& Error);
    // ---- #198 rest phases (Framework/LDGameModeRest.cpp) ----
    FLDMatchTimeline Timeline;
    uint64 TimelineRevision = 0;
    // PK sign-ups of the current build step (the rest lives standing in their sign-up disks while the window is open) and
    // the entrants locked from them.
    TSet<FGuid> RestSignups;
    TSet<FGuid> RestReady;
    TArray<TWeakObjectPtr<class ALDRestServiceNPC>> RestServiceNPCs;
    TWeakObjectPtr<class ALDTrainingRoomDisplay> TrainingRoomDisplay;
    void ClearRestServiceNPCs();
    // RespawnsAndWorld and the lock: RestSignups from the committed feet (empty outside the window). False on a fault.
    bool RefreshRestSignups(FString& Error);
    TArray<FGuid> LockedEntrants;
    // Per contender: where its field life stood when the rest began (returns there), and whether it was alive.
    struct FLDRestReturn
    {
        bool bAliveAtRest = false;
        FVector FieldFootCm = FVector::ZeroVector;
        FName FieldSurface;
    };
    TMap<FGuid, FLDRestReturn> RestReturns;
    // Contenders whose current life is a rest life (private rest space, no capture eligibility).
    TSet<FGuid> RestLives;
    // #198 session D: contenders whose current life is a PK life in the arena.
    TSet<FGuid> PKLives;
    // Contenders still waiting for a rest placement that was blocked at its boundary (retried every boundary of the rest).
    TSet<FGuid> PendingPhasePlacement;
    // A phase body prepared but not committed: approved §8 joint allocation verifies every body of an entry before any
    // of them stands.
    struct FLDPhasePrepared
    {
        FLDPlacedBody Placed;
        TSharedPtr<FLDBodyCombatState> CombatState;
    };
    struct FLDPhaseTarget
    {
        FGuid Contender;
        FLDPlacementTarget Where;
    };
    // Prepares the next life of a bodiless contender at Where without committing it. bBlocked (nothing kept, the reason in
    // BlockedReason) when the placement is not free or the retired body's motion is not consumed yet. False only on a
    // fault.
    bool PreparePhaseBody(const FLDPhaseTarget& Target, const FLDGameplayWritePermit& Permit, FLDPhasePrepared& Out,
        bool& bBlocked, FString& BlockedReason, FString& Error);
    // Commits and activates a prepared life; any failure is an authority fault.
    bool CommitPhaseBody(FLDPhasePrepared& Prepared, int64 ProtectedUntilMs, const FLDGameplayWritePermit& Permit, FString& Error);
    void ReleasePhasePrepared(TArray<FLDPhasePrepared>& Prepared);
    // The contender's pending spawn request for its next life never replays: the next attempt (another anchor, or the
    // same entry tried again) uses a newer request identity.
    bool WithdrawPhaseRequest(const FGuid& Contender, const FLDGameplayWritePermit& Permit, FString& Error);
    // Prepares every target in order. When one is blocked every prepared body is released, every target's request is
    // withdrawn and bBlocked names the first blocked contender's reason; Out is then empty.
    bool PrepareJointPhase(const TArray<FLDPhaseTarget>& Targets, const FLDGameplayWritePermit& Permit,
        TArray<FLDPhasePrepared>& Out, bool& bBlocked, FString& BlockedReason, FString& Error);
    // The return to the field of the current Return step: started (every rest life retired) when it first fell due, its
    // bodies prepared jointly at the boundary that enters the field and committed there together.
    bool bFieldReturnStarted = false;
    TArray<FLDPhasePrepared> PreparedFieldReturn;
    // #198 session D: the PK entrants whose arena bodies wait to stand together (the PK cannot start before they do).
    TArray<FLDPKPlacement> PendingPKEntry;
    bool bPKEntryLogged = false;
    // RespawnsAndWorld, before the timeline advances: tries a waiting PK entry again, and for a due entry (PK start or
    // field) reports whether its bodies stand ready. A field return still unready REST.EntryReadyTimeoutSec after its
    // boundary is an explicit fault; a PK entry still waiting then ends as NoContest (approved outer parameters §1).
    bool PrepareEntry(const FLDGameplayWritePermit& Permit, bool& bBodiesReady, FString& Error);
    bool PrepareFieldReturn(const FLDGameplayWritePermit& Permit, bool& bReady, FString& Error);
    bool CommitFieldReturn(bool bFinalField, const FLDGameplayWritePermit& Permit, FString& Error);
    bool TryPKEntry(const FLDGameplayWritePermit& Permit, bool& bPlaced, FString& Error);
    bool AbandonPKEntry(const FLDGameplayWritePermit& Permit, FString& Error);
    struct FLDTrainingDummy
    {
        FGuid Entity;
        FLDCombatBodyRef Body;
        TWeakObjectPtr<ALDCharacter> Actor;
    };
    // By roster slot contender; the association fixes the chassis, not access or attack attribution.
    TMap<FGuid, FLDTrainingDummy> TrainingDummies;
    TSet<FGuid> TrainingDummyIds;
    struct FLDTrainingResetWindow { uint32 Life = 0; uint8 RestIndex = 0; int64 StartedAtMs = 0; };
    TMap<FGuid, FLDTrainingResetWindow> PendingTrainingResets;
    bool PrepareTrainingGround(const FGuid& Owner, struct FLDLegalGround& Out, FString& Error) const;
    // RespawnsAndWorld: shared target at its approved slot, using the existing legal offsets when temporarily occupied.
    bool SpawnTrainingDummy(const FGuid& Owner, const FLDGameplayWritePermit& Permit, FString& Error);
    void RetireTrainingDummy(const FGuid& Owner);
    bool ProcessTrainingResets(const FLDGameplayWritePermit& Permit, FString& Error);
    bool ApplyTimelineEvents(const TArray<FLDTimelineEvent>& Events, const FLDGameplayWritePermit& Permit, FString& Error);
    bool MoveContendersToRest(const FLDGameplayWritePermit& Permit, FString& Error);
    // Ends a living contender life without a death (phase boundary): no K/D, kill feed, reward or respawn wait.
    bool RetireLifeForPhase(const FGuid& Contender, const FLDGameplayWritePermit& Permit, FString& Error);
    // Places the next life and its shared training target only after both placements are available.
    bool PlaceRestLife(const FGuid& Contender, const FLDGameplayWritePermit& Permit, bool& bBlocked, FString& Error);
    bool RetryPhasePlacements(const FLDGameplayWritePermit& Permit, FString& Error);
    FName RestAnchorFor(const FGuid& Contender) const;
    void PublishTimeline();
    bool InitializeTimeline(FString& Error);
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
    bool IsCloseAwaitingSettlement() const { return (PendingCapture.IsSet() || bShowdownClosePending) && !bAuthorityStepOpen; }
    bool TryCloseMatch(ELDMatchEndReason Reason, const FGuid& Winner, int64 EndedAtMs, FString& Error);
    void PublishMatchOutcome();
    // #198 §10 (Framework/LDGameModeBlueprint.cpp): once the result is published, each present human contender's own
    // build blueprint (or the explicit reason it has none) goes to that contender's owning connection only.
    void PublishBuildBlueprints();
    void PublishPublicMatch();
    void QueueLeaveFact(const FGuid& ContenderId);
    FLDContenderRecord* FindContender(const FGuid& ContenderId);
    // Playtest log: header and roster at the start, every contender's final state at the result.
    void LogPlaytestRoster() const;
    void LogPlaytestSummary(const FLDResultView& View) const;
    LDLog::FPeriodicGate HeartbeatGate{1000};
    TMap<FGuid, double> LastMarkAt;
};
