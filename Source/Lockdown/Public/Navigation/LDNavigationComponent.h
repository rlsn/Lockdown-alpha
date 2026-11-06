#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "Combat/LDCombatTypes.h"
#include "Navigation/LDOrderTypes.h"
#include "Navigation/LDOrderRules.h"
#include "Navigation/LDPathSteering.h"
#include "World/LDWorldQueryTypes.h"
#include "LDNavigationComponent.generated.h"

class ALDPlayerController;
class ALDCharacter;
class ALDGameMode;
class ALDPointerFeedback;
class FLDDemoContentSnapshot;
struct FLDAuthorityTargetSample;
struct FCollisionShape;

// The only writer of a contender body's current order. The owning client sends typed intents and predicts
// its own accepted order; the server queues, admits and advances it inside the core Actions/Motion permits.
UCLASS()
class LOCKDOWN_API ULDNavigationComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    ULDNavigationComponent();
    virtual void TickComponent(float Delta,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction) override;
    bool Enabled() const;
    // ---- Owning client intents; each takes a fresh local identity. bQueue (Shift) appends the order to the server's
    // shift queue instead of replacing the current order and the queue (LDOrderRules::MaxQueuedSteps).
    void Navigate(const FVector& Surface, bool bQueue=false);
    // MarkerCm is where the move marker appears when it is not the order's own point (a right click on the stall model
    // walks beside it but marks where the click landed); its colour still answers the order's own point.
    void NavigateGround(const FVector& Surface, FName SurfaceHint, const FVector* MarkerCm=nullptr, bool bQueue=false);
    void AttackUnit(const FGuid& Observed, uint32 ObservationEpoch, bool bQueue=false);
    void AttackMove(const FVector& Surface, FName SurfaceHint, bool bQueue=false);
    void ApproachShop(FName ShopId);
    void Stop();
    void ResetLife();
    // Right click: visible unit under the pointer, else the public shop, else ground. One class per click. A ground
    // click keeps walking toward the pointer while its button is held (LDOrderRules::ShouldRepeatHeldMove); a Shift
    // click is one queued step and never repeats.
    void ClickMove(bool bQueue=false);
    // Owning client: a Shift Q/W/1/2/D request goes to the server's shift queue rather than straight to Combat. A
    // direction aim also carries the ground point it was aimed at, so the server re-aims it when its turn comes.
    void QueueCombatAction(const FLDCombatActionRequest& Request, const FVector* AimCm);
    // Owning client: own queued spatial steps still waiting (their markers and the path line), newest last.
    int32 QueuedMarkCount() const { return QueuedMarks.Num(); }
    // The pointer in viewport pixels for world clicks and hover: the mouse, or a DEV scenario's pointer while one is set.
    bool PointerPosition(FVector2D& Out) const;
    // The ground under the pointer on the world-click channel.
    bool GroundUnderPointer(FHitResult& Out) const;
    // The public shop whose decoration model lies under the pointer ray (its mesh bounds, not the interaction radius).
    bool ShopUnderPointer(FName& OutShop);
    // The ground goal of a right click: the pointer's ground point, or over the stall model the nearest walkable point
    // beside it (the model takes a right click like a wall: where the body already stands when it is beside the model).
    void PointerGoal(const FHitResult& GroundHit, FVector& OutGoal);
    // The visible unit a right click would attack now (the hover ring and attack cursor); false when none.
    bool ReadHovered(FGuid& OutHandle, uint32& OutEpoch) const;
    // DEV scenarios (WITH_DEV_AUTOMATION_TESTS) stand in for the mouse; an empty value restores it. Inert otherwise.
    void DevSetPointer(const TOptional<FVector2D>& Pointer);
    ALDPointerFeedback* PointerFeedback() const;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    // Registered id of the single Demo ground surface; None when the world is unavailable.
    FName GroundSurfaceHint() const;
    void PanCamera(const FVector2D& Axis,float Delta);
    void Recenter(bool Held);
    void ToggleFollow();
    void PanTo(const FVector& Point);
    bool bFollowCamera=true,bEdgePan=true;
    bool IsFollowing() const {return bFollowing;}
    bool HasMoveOrder() const {return bLocalActive;}
    // Owning client: heartbeats sent so far by this controller (monotonic).
    uint32 HeartbeatsSent() const {return HeartbeatSequence;}
    bool IsAuthorityActive() const {return ActiveOrder.IsSet();}
    FLDMoveIdentity LocalIdentity() const {return Local;}
    // Latest owner receipt applied by the owning client (Serial 0 before the first one).
    const FLDOrderReceipt& LatestReceipt() const {return LastReceipt;}
    // Owning client: the shop of the current ApproachShop order, en route or arrived and awaiting the
    // shop domain's open result. A new order, Stop, a non-completed end or a new life ends the binding.
    bool ReadShopInteraction(FName& OutShopId, uint32& OutLife) const;
    // Owning client, once per binding: the current ApproachShop order completed inside the shop's range.
    // The shop domain requests its session from here; Navigation itself never opens a shop.
    DECLARE_MULTICAST_DELEGATE_TwoParams(FLDOnShopArrived, FName /*ShopId*/, uint32 /*Life*/);
    FLDOnShopArrived OnShopArrived;
    // Owning client: whether the own body's feet stand inside the shop's interaction radius, by the same check as a
    // right-click on the shop. A local trigger for shop pre-orders only; the server re-checks reach on every trade.
    bool OwnBodyAtShop(FName& OutShop);
    // ---- Server entries shared by the owning RPCs and the registered opponent AI.
    // Receives and queues only; protection removal, admission and replacement happen in the Actions phase.
    FLDOrderReceipt SubmitOrder(const FLDOrderCaller& Caller, const FLDSpatialOrderRequest& Request);
    // Stop semantics: Identity must advance both the order and the cancellation epoch.
    FLDOrderReceipt CancelOrder(const FLDOrderCaller& Caller, const FLDMoveIdentity& Identity, ELDOrderCancelReason Reason);
    FLDOrderReceipt QueryOrder(const FLDOrderCaller& Caller, const FLDMoveIdentity& Identity) const;
    // Shift queue: appends one Q/W/1/2/D request behind the current order and the queued steps. Only checks the caller,
    // the request shape, its freshness against Combat's Direct window and the queue bound; Combat judges the action when
    // its turn comes (ALDCombatDirector::SubmitAction). AimCm re-aims a direction request from the body at that time.
    FLDCombatActionReceipt QueueAction(const FLDOrderCaller& Caller, const FLDCombatActionRequest& Request, const FVector* AimCm);
    // Drops every queued step that has not started (a non-Shift command, S, death, a lost life or match, a takeover).
    // Spatial steps answer Cancelled with Failure; action steps answer a Combat Cancelled refusal.
    void ClearQueue(ELDOrderFailure Failure);
    // Server: queued steps still waiting (not the current order nor a running queued action).
    int32 QueuedSteps() const { return Queue.Num(); }
    // Death, leave or match close: ends pending and active orders without any new movement.
    void EndLifeOrders(ELDOrderCancelReason Reason=ELDOrderCancelReason::LifeEnded);
    // Combat's MatchReset: the controller outlives the match, its orders and order book do not. Ends the retired
    // match's orders and drops the whole book (idempotency window, request watermark, activation throttle) and the
    // content cache. The receipt serial stays: it is the owning connection's monotonic order, not match state.
    void ResetMatch();
    bool AuthorizesMove(const FLDMoveIdentity& Identity) const;
    bool AdvanceAuthorityMotion(const FLDAuthorityStep& Step, const FLDCombatBodyRef& Body,
        const FLDGameplayWritePermit& Permit, FString& Error);
    bool ProcessAuthorityOrders(const FLDGameplayWritePermit& Permit, FString& Error);
    uint32 QueryCount=0,RejectedMoves=0,Replans=0;
    double QueryMilliseconds=0;
    TArray<double> AcknowledgementMs;
    TArray<double> RouteAcceptanceMs;
    FString Status;
    FVector Destination=FVector::ZeroVector;
    // Explicitly callable for deterministic adversarial RPC probes; all validation is authority-side.
    UFUNCTION(Server,Reliable) void ServerSubmitOrder(const FLDSpatialOrderRequest& Request);
    UFUNCTION(Server,Reliable) void ServerQueueCombatAction(const FLDCombatActionRequest& Request, bool bAimPoint, FVector_NetQuantize10 AimCm);
    UFUNCTION(Server,Reliable) void ServerStop(FLDMoveIdentity Identity);
    UFUNCTION(Server,Unreliable) void ServerHeartbeat(FLDMoveIdentity Identity,uint32 Sequence);
    UFUNCTION(Client,Reliable) void ClientOrderReceipt(const FLDOrderReceipt& Receipt);
private:
    static constexpr double OrderQueryIntervalSeconds=.1;
    static constexpr double OrderNoProgressSeconds=2.;
    static constexpr double GoalShiftCm=50.;
    static constexpr double GoalAcceptanceCm=40.;
    // Spacing of the goals tried from beside the stall back toward the body when the stall model is right-clicked.
    static constexpr double ShopStopStepCm=25.;
    static constexpr int32 MaxOrderRecords=16;
    struct FPendingOrder
    {
        FLDSpatialOrderRequest Request;
        FLDMatchContext Match;
        FLDCombatBodyRef Body;
        int64 NotBeforeMs=0;
        bool bStop=false;
        ELDOrderCancelReason Reason=ELDOrderCancelReason::Stop;
        // Earliest aggressive intent of this life that a newer pending request superseded.
        TOptional<FLDAggressiveIntent> OwedAggression;
    };
    struct FActiveOrder
    {
        FLDSpatialOrderRequest Request;
        FLDMatchContext Match;
        FLDCombatBodyRef Body;
        TArray<FVector> Points;
        int32 PointIndex=0;
        FVector Goal=FVector::ZeroVector;          // end of the current path
        FVector Destination=FVector::ZeroVector;   // fixed ground destination (MoveGround/AttackMove)
        FVector ShopCentre=FVector::ZeroVector;
        FVector TargetFoot=FVector::ZeroVector;    // pinned target at the latest authorized sample
        FVector PlannedTargetFoot=FVector::ZeroVector;
        FVector SentGoal=FVector::ZeroVector;
        FName Surface;
        float Acceptance=40;
        double RangeCm=0;
        FGuid Target;
        uint32 TargetEpoch=0;
        uint32 HeartbeatSequence=0;
        uint64 AttackCycle=0;
        uint64 LifeRevision=0;
        double LastHeartbeat=0, LastProgress=0, BestProgress=0, NextChasePlan=0;
        bool bHumanLease=false, bPinned=false, bEngaged=false, bCloseIn=false;
        bool bReplanned=false, bForceReplan=false, bProgressReplan=false;
        bool bProtectionRemoved=false, bSentGoal=false, bSentEngaged=false;
        // Taunt chase: server-made, never in the owner's book or receipts; Identity.Order stays 0.
        bool bForced=false;
        FLDCombatBodyRef ForcedTarget;
        // Decoy lure (item_decoy_wig): a forced walk to LurePoint; no attack is ever started.
        bool bLure=false;
        FVector LurePoint=FVector::ZeroVector;
    };
    struct FOrderRecord
    {
        FLDSpatialOrderRequest Request;
        bool bStop=false;
        FLDOrderReceipt Receipt;
    };
    // One shift-queued step: a spatial order (bAction false) or a Direct Q/W/1/2/D request handed to Combat in turn.
    struct FQueuedStep
    {
        bool bAction=false;
        FLDSpatialOrderRequest Order;
        FLDCombatActionRequest Action;
        // A direction action re-aimed toward this ground point from the body when its turn comes.
        bool bAimPoint=false;
        FVector AimCm=FVector::ZeroVector;
        FLDMatchContext Match;
        FLDCombatBodyRef Body;
        int64 NotBeforeMs=0;
    };
    // An aggressive step ends protection in the next Actions phase after it was sent (APP_SPEC 7.1), not when it runs,
    // even when it is dropped before then. The earliest such intent of the life is enough.
    struct FOwedAggression
    {
        FLDAggressiveIntent Intent;
        FLDMatchContext Match;
        FLDCombatBodyRef Body;
    };
    ALDPlayerController* Controller() const;
    ALDGameMode* AuthorityMode() const;
    bool IsLocalOwner() const;
    bool IsHumanOwner() const;
    const FLDDemoContentSnapshot* DemoContent();
    bool QueryBodyGround(FLDLegalGround& Out, FCollisionShape& Capsule, ELDWorldQueryFailure& Failure, FString& Error) const;
    // Route start for every planner: the body's registered ground through LDPathSteering::PlanStart.
    bool QueryPlanStart(FLDLegalGround& Out, FCollisionShape& Capsule, ELDWorldQueryFailure& Failure, FString& Error) const;
    // Route from this owner's body to Goal: a clicked ground point stays strict; a chased body's foot is planned as a
    // body goal (LDPathSteering::PlanRoute). Projected is the planned end.
    bool Plan(const FVector& Goal, FName SurfaceHint, LDPathSteering::EGoal GoalKind, TArray<FVector>& Out, FVector& Projected,
        ELDWorldQueryFailure& Failure, FString& Error);
    bool ShopAt(const FVector& GroundPoint, FName& OutShop, bool bNotify=true);
    // Local route check of a ground point from this body, for click feedback only: false with bKnown when the route
    // fails; true without bKnown when the body's own support cannot be read this frame. The server still decides.
    bool ProbeGround(const FVector& GroundCm, bool& bKnown) const;
    // ---- Owning client pointer: hover target, cursor and the held move button.
    void TickPointer();
    void ClearPointer();
    void TickHeldMove();
    ALDPointerFeedback* EnsurePointer();
    TWeakObjectPtr<ALDPointerFeedback> Pointer;
    TOptional<FVector2D> DevPointer;
    FGuid Hovered;
    uint32 HoveredEpoch=0;
    FVector HeldPoint=FVector::ZeroVector;
    double HeldSentAt=0;
    bool bHeldMove=false;
    // ---- Owning client.
    bool NextLocalIdentity(bool Cancel, FLDMoveIdentity& Out);
    // False when the order was not sent (no life, suppressed input or exhausted identities).
    bool SendOrder(FLDSpatialOrderRequest Request, const TCHAR* PendingStatus);
    void ApplyPrediction();
    void RefreshLocalActive();
    void TickCamera(float Delta);
    void Face(const FVector& Direction);
    // ---- Authority order book (LDNavigationOrders.cpp).
    bool ResolveOwnerCaller(FLDOrderCaller& Out, ELDOrderFailure& Failure) const;
    bool ValidateCaller(const FLDOrderCaller& Caller, const FLDMoveIdentity& Identity, ELDOrderFailure& Failure) const;
    void EnsureBook(const FLDMoveIdentity& Identity);
    void EndStaleServerOrders();
    // Drops the bound book and its idempotency window together.
    void ResetBook();
    const FOrderRecord* FindRecord(const FLDMoveIdentity& Identity) const;
    FOrderRecord* FindRecord(const FLDMoveIdentity& Identity);
    void AddRecord(const FLDSpatialOrderRequest& Request, bool bStop);
    FLDOrderReceipt Enqueue(const FLDOrderCaller& Caller, const FLDSpatialOrderRequest& Request, bool bStop,
        ELDOrderCancelReason Reason);
    FLDOrderReceipt Reply(const FLDSpatialOrderRequest& Request, ELDOrderStatus Outcome, ELDOrderFailure Failure,
        bool bStore, bool bProtectionRemoved=false, uint64 LifeRevision=0);
    FLDOrderReceipt Replay(const FOrderRecord& Record);
    FLDOrderReceipt Publish(FLDOrderReceipt Receipt, bool bStore);
    FLDOrderReceipt MakeReceipt(const FActiveOrder& Order, ELDOrderStatus Outcome, ELDOrderFailure Failure) const;
    void PublishActive(ELDOrderStatus Outcome);
    bool LeaseFresh(const ALDGameMode& Mode, const FActiveOrder& Order) const;
    bool ActivatePending(ALDGameMode& Mode, const FLDGameplayWritePermit& Permit, FString& Error);
    bool AdmitOrder(ALDGameMode& Mode, ALDCharacter& Body, const FLDGameplayWritePermit& Permit, FActiveOrder& Order,
        ELDOrderFailure& Failure, FString& Error);
    bool AdvanceActive(ALDGameMode& Mode, ALDCharacter& Body, const FLDGameplayWritePermit& Permit, FString& Error);
    // Taunted body: refuses its own orders and chases the taunter through this observer's current view.
    bool AdvanceForced(ALDGameMode& Mode, ALDCharacter& Body, const FLDCombatBodyRef& Self, const FLDCombatBodyRef& Taunter,
        const FLDGameplayWritePermit& Permit, FString& Error);
    // Lured body: refuses its own orders and walks to the decoy point (#112 §5).
    bool AdvanceLure(ALDGameMode& Mode, ALDCharacter& Body, const FLDCombatBodyRef& Self, const FVector& Point,
        const FLDGameplayWritePermit& Permit, FString& Error);
    bool AcquireTarget(ALDGameMode& Mode, ALDCharacter& Body, const FLDGameplayWritePermit& Permit,
        FLDAuthorityTargetSample& Out, bool& bFound, FString& Error);
    bool EngageTarget(ALDGameMode& Mode, ALDCharacter& Body, const FLDAuthorityTargetSample& Sample,
        const FLDGameplayWritePermit& Permit, FString& Error);
    FVector AuthoritySteering(const ALDCharacter& Body);
    void TrackProgress(const ALDCharacter& Body, double Now, bool bMoving);
    void EndActive(ELDOrderStatus Outcome, ELDOrderFailure Failure, bool bHalt);
    // ---- Shift queue (LDNavigationOrders.cpp).
    // The end or refusal of one step: the next step follows, or the queue is dropped (LDOrderRules::QueueAfter).
    void SettleStep(ELDOrderFailure Failure);
    // Settles the protection owed by aggressive steps sent since the last Actions phase.
    void SettleQueuedAggression(ALDGameMode& Mode, const FLDGameplayWritePermit& Permit);
    // Starts queued steps while nothing else holds the body: a spatial step becomes the pending request, an action
    // step is handed to Combat and runs until its receipt settles.
    void AdvanceQueue(ALDGameMode& Mode);
    void StartQueuedAction(ALDGameMode& Mode, FQueuedStep Step);
    // The next queued step is a ground walk: an arrival keeps its momentum instead of halting.
    bool QueueContinuesWalking() const;
    void ReleaseOrderLinks(const FActiveOrder& Order, bool bReleasePin);
    // ---- Owning-client state.
    // A queued own ground step still waiting on the server, shown as a waypoint until it starts or ends.
    struct FQueuedMark
    {
        FLDMoveIdentity Identity;
        FVector GroundCm=FVector::ZeroVector;
        bool bAttack=false;
    };
    TArray<FQueuedMark> QueuedMarks;
    void RefreshQueuedPath();
    FLDMoveIdentity Local, ClientActive;
    FName ShopBindingId;
    FLDMoveIdentity ShopBindingIdentity;
    bool bShopArrived=false;
    ELDSpatialOrderKind ActiveKind=ELDSpatialOrderKind::MoveGround;
    FLDOrderReceipt LastReceipt;
    uint64 LastReceiptSerial=0;
    uint32 Serial=0,Epoch=0,AcceptedOrder=0,AcknowledgedOrder=0,HeartbeatSequence=0;
    uint32 SeenMatch=0,SeenLife=0;
    bool bFollowing=false,bLocalActive=false,bLocalPending=false,bLocalStop=false,bReplanned=false;
    bool bActiveHasGoal=false,bActiveEngaged=false;
    bool bRecenterHeld=false,bCameraInitialized=false;
    // CameraCentre holds a real static-map point from a body or a pan: the dead owner's camera may start there.
    bool bCameraCentreKnown=false;
    bool bHadFocus=true;
    TArray<FVector> Points;
    int32 PointIndex=0;
    FVector ActiveGoal=FVector::ZeroVector;
    FVector CameraCentre=FVector::ZeroVector;
    float AcceptanceRadius=40,ActiveAcceptance=40;
    double LastHeartbeatSent=-1000,LastProgress=0,BestProgress=0,LocalSentAt=0;
    TWeakObjectPtr<ALDCharacter> LastPawn;
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    // ---- Authority order book (server only).
    TOptional<FPendingOrder> PendingOrder;
    TOptional<FActiveOrder> ActiveOrder;
    TArray<FQueuedStep> Queue;
    // DirectSequence of the queued action Combat is judging or running (0 when none); the queue waits for it.
    uint64 RunningQueuedAction=0;
    TOptional<FOwedAggression> QueueOwedAggression;
    TArray<FOrderRecord> Records;
    FLDOrderBook Book;
    uint64 NextReceiptSerial=0;
    uint32 ForcedSerial=0;
    // While a taunt replaces an order: that order's unreleased swing at this taunter is kept (ReleaseOrderLinks).
    FLDCombatBodyRef KeptSwingTarget;
    // A forced chase that ended by itself (no path, no progress) is retried after one order query interval.
    LDOrderRules::FForcedRetry ForcedRetry;
    // The last knockback count Navigation saw on its body life; a new knockback drops the current order (#112 §5).
    FLDCombatBodyRef KnockbackBody;
    uint64 SeenKnockbackSerial=0;
};
