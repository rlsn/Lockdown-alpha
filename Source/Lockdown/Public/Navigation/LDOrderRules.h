#pragma once

#include "CoreMinimal.h"
#include "Navigation/LDOrderTypes.h"
#include "Combat/LDCombatTypes.h"
#include "Vision/LDViewTypes.h"

// Authority order book of one controller, bound to one match and life. Its request watermark and activation
// throttle only mean something against that match's clock (which restarts at 0 in every match) and that life's
// identities, while the controller outlives both: a new binding or a match reset starts all of it over together.
struct FLDOrderBook
{
    uint32 Match=0;
    uint32 Life=0;
    // Newest identity received for this binding; a later order must exceed it.
    FLDMoveIdentity Received;
    // Earliest authority second of this match at which the next non-Stop order may activate.
    double NextActivationSeconds=0;

    bool IsBoundTo(uint32 InMatch, uint32 InLife) const { return Match==InMatch && Life==InLife; }
    void Bind(const FLDMoveIdentity& Identity)
    {
        Reset();
        Match=Received.Match=Identity.Match;
        Life=Received.Life=Identity.Life;
    }
    void Reset() { *this=FLDOrderBook(); }
    bool ActivationDue(double NowSeconds) const { return NowSeconds>=NextActivationSeconds; }
    void ArmActivation(double NowSeconds, double IntervalSeconds) { NextActivationSeconds=NowSeconds+IntervalSeconds; }
};

namespace LDOrderRules
{
    // D03 attack-move acquisition among the currently visible units: the planar-nearest unit within RadiusCm
    // of FootCm, never the attacker itself; equal distances take the smaller observer handle.
    // Returns the unit index, or INDEX_NONE when no candidate qualifies.
    LOCKDOWN_API int32 NearestCandidate(const TArray<FLDObservedUnitView>& Units, const FVector& FootCm,
        double RadiusCm, const FGuid& Self);

    // Holding the move button repeats the ground order toward the pointer: at most once per HeldMoveRepeatSeconds
    // (8 per second) and only once the pointer's ground point moved HeldMoveShiftCm from the last order's, so a still
    // pointer sends nothing more. The server's activation throttle and newest-pending replacement stay the only
    // admission path.
    constexpr double HeldMoveRepeatSeconds = .125;
    constexpr double HeldMoveShiftCm = 40.;
    LOCKDOWN_API bool ShouldRepeatHeldMove(double NowSeconds, double LastSentSeconds, const FVector& LastGroundCm,
        const FVector& GroundCm);

    // Where a right click on the stall model walks to: the point nearest FromCm on the stall's planar footprint grown by
    // the body radius, so the body stands right beside the model, never inside it (the decoration has no collision). Planar; Z is FromCm's. Returns false when FromCm already
    // touches that grown footprint (the body stays where it is) or the footprint is empty.
    LOCKDOWN_API bool ShopFrontPoint(const FBox2D& FootprintCm, const FVector& FromCm, double BodyRadiusCm, FVector& OutCm);
    // Candidate goals from Front back toward From every StepCm (Front first, From last): the first reachable one is the
    // nearest walkable point beside the stall.
    LOCKDOWN_API TArray<FVector> ShopStopCandidates(const FVector& FrontCm, const FVector& FromCm, double StepCm);

    // Shift queue (2026-11-06 user decision): Shift + right click / A-click / Q W 1 2 D appends a step behind the current
    // order instead of replacing it. At most MaxQueuedSteps steps wait; a further Shift command is refused (QueueFull).
    constexpr int32 MaxQueuedSteps = 8;
    // The queue's head starts only when nothing else holds the body: no pending request, no current order and no queued
    // action still in Combat (queued, winding up or releasing).
    LOCKDOWN_API bool QueueHeadDue(bool bPending, bool bActive, bool bActionRunning);
    // How the end of the current step (or a refused step) treats the steps behind it: an arrival, a refusal or an early
    // end of that one step (no path, blocked, target lost or dead, knocked back) moves on to the next step; a Stop,
    // death, a lost life, match or lease, or a taunt/lure takeover drops the whole queue.
    enum class EQueueAfter : uint8 { Next, Clear };
    LOCKDOWN_API EQueueAfter QueueAfter(ELDOrderFailure Failure);
    // A queued action step is settled once Combat finished or refused it (an expired receipt also counts).
    LOCKDOWN_API bool QueuedActionSettled(ELDCombatPhase Phase);
    // A queued direction skill keeps the ground point it was aimed at and is re-aimed from where the body stands when its
    // turn comes: the planar unit direction from FootCm to AimCm. False when the point is under the body (or not finite).
    LOCKDOWN_API bool QueuedAimDirection(const FVector& FootCm, const FVector& AimCm, FVector& OutDirection);

    // A then left click (2026-11-07 user decision): a click on an attackable visible unit (the same pointer pick as a right
    // click) attacks that unit exactly like a right click (AttackUnit); a click anywhere else is an attack-move toward the
    // ground point (AttackMove).
    LOCKDOWN_API ELDSpatialOrderKind AttackConfirmKind(bool bUnitUnderPointer);
    // A non-Shift Q/W/D request (2026-11-07 user decision): an entry whose current availability is a refusal
    // (cooldown, mana, control, empty slot, no active, ...) is refused at once and leaves the current move or attack
    // order, the pending request and the shift queue as they were. Only a ready entry goes on (the own ability windup,
    // Busy, only delays it); an admitted request then replaces the order or drops the queue as before.
    LOCKDOWN_API bool SkillRequestReady(ELDCombatReject Unavailable);

    // #153 opening countdown (2026-11-07 user decision: "开局五秒允许移动和选技能,但不允许攻击和购买"): before T=0 a
    // movement order (ground move or the walk to the shop, Shift-queued or not) and the D dash are admitted; attack and
    // cast orders and Q/W are refused. Trades are refused by the shop itself.
    LOCKDOWN_API bool CountdownAdmitsOrder(const FLDSpatialOrderRequest& Request);
    LOCKDOWN_API bool CountdownAdmitsAction(ELDCombatEntry Entry);

    // A taunt forces the one current order of a body (CONTENT_AND_BALANCE 15.5): Begin replaces any other order (or a
    // forced chase of an earlier taunter), Keep advances the forced chase, End returns control when the taunt is gone
    // (the replaced order is not resumed), None leaves ordinary orders alone. While taunted every own order is refused.
    enum class EForcedOrder : uint8 { None, Begin, Keep, End };
    LOCKDOWN_API EForcedOrder ForcedOrderStep(bool bTaunted, bool bActiveForced, bool bSameTaunter);

    // A forced chase that ended by itself is re-created only after RetryAt, on the match-relative authority clock. That
    // clock restarts at 0 every match while the order component persists, so a new match must clear it.
    struct FForcedRetry
    {
        double RetryAtSeconds = 0;
        void Hold(double NowSeconds, double IntervalSeconds) { RetryAtSeconds = NowSeconds + IntervalSeconds; }
        bool IsDue(double NowSeconds) const { return NowSeconds >= RetryAtSeconds; }
        void ResetMatch() { RetryAtSeconds = 0; }
    };

    // Basic-attack pursuit without idle gaps (2026-11-07 user decision: a started windup lands at any distance while the
    // target stays seen, so a chase at a similar speed runs approach -> windup in range -> hit -> keep chasing -> next windup).
    // Only an action still winding up holds the attack order in place. A basic due at NowMs releases later in this same
    // Actions phase (ReleaseDue runs after Navigation) and lands wherever the seen target is, so it no longer holds the body.
    LOCKDOWN_API bool ActionHoldsChase(bool bActionInProgress, ELDCombatEntry Entry, int64 ReleaseAtMs, int64 NowMs);
    // Press: in range while the next beat is not ready yet, a target that walked away since the order's previous sample
    // (by at least PressShiftCm along the attacker-to-target line) is followed instead of waited for at the range edge;
    // one that walked closer ends the press and a still sample keeps the current choice. Out of range, an action in
    // progress, a blocked or ready attack, or closing in after a refused start never press, and neither does a sample
    // jump beyond PressSampleLimitCm (another target or a displacement, not a walk).
    constexpr double PressShiftCm = 1.;
    constexpr double PressSampleLimitCm = 200.;
    struct FChasePressInput
    {
        bool bWasPressing = false;
        bool bInRange = false;
        bool bActing = false;
        bool bAttackAllowed = false;
        bool bBeatReady = false;
        bool bCloseIn = false;
        FVector AttackerFootCm = FVector::ZeroVector;
        FVector PreviousTargetFootCm = FVector::ZeroVector;
        FVector TargetFootCm = FVector::ZeroVector;
    };
    LOCKDOWN_API bool ChasePress(const FChasePressInput& Input);
}
