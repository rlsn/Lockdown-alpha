#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Combat/LDCombatTypes.h"
#include "Framework/LDCaptureTypes.h"

class FLDDemoContentSnapshot;
struct FLDDemoWorldGeometry;

// D26 (#79) 画饼老板 Boss. One body per match appears at field progress T = BOSS appear_at (#198 outer §5: 240 s) on the
// first public waypoint and walks the four waypoints in id order on legal navigation. It attacks anyone who hit it within
// attacker_memory (the nearest such attacker first), otherwise the nearest visible contender within aggro_radius.
// It never respawns; the last hit gets its ordinary kill reward and one build reward draw. Still alive at T = BOSS
// leave_at (540 s, the third rest), it leaves the field without a death or any reward and never reaches the final field.
namespace LDBoss
{
    // A living contender as the Boss perceives it this step.
    struct FCandidate
    {
        FLDCombatBodyRef Body;
        FVector FootCm = FVector::ZeroVector;
        bool bVisible = false;
    };
}

struct LOCKDOWN_API FLDBossRules
{
    int64 StartedAtMs = 0;
    int64 AppearAtMs = 0;
    int64 LeaveAtMs = 0;
    double AggroRadiusCm = 0;
    double AttackRangeCm = 0;
    int64 AttackerMemoryMs = 0;
    FName MonsterId;
    uint8 CreatureId = 0;
    FName SurfaceId;
    TArray<FVector> Waypoints;
    bool IsValid() const;
};

enum class ELDBossIntent : uint8 { None, Patrol, Chase, Attack };

struct FLDBossDecision
{
    ELDBossIntent Intent = ELDBossIntent::None;
    FLDCombatBodyRef Target;
    FVector GoalFootCm = FVector::ZeroVector;
};

struct LOCKDOWN_API FLDBossState
{
    struct FAttacker
    {
        FLDCombatBodyRef Source;
        int64 AtMs = 0;
    };

    FLDBossRules Rules;
    ELDBossPhase Phase = ELDBossPhase::None;
    FGuid EntityId;
    FLDCombatBodyRef Body;
    int32 Waypoint = 0;
    TArray<FAttacker> Attackers;
    FGuid KilledBy;
    int64 DownAtMs = 0;

    bool IsActive() const { return Rules.IsValid() && EntityId.IsValid(); }
    int64 AppearAtMs() const { return Rules.StartedAtMs + Rules.AppearAtMs; }
    int64 LeaveAtMs() const { return Rules.StartedAtMs + Rules.LeaveAtMs; }
    bool IsSpawnDue(int64 NowMs) const
    { return IsActive() && Phase == ELDBossPhase::Pending && NowMs >= AppearAtMs() && NowMs < LeaveAtMs(); }
    // Still pending or alive at its leave instant: it leaves (a living body is retired by the owner first).
    bool IsLeaveDue(int64 NowMs) const
    { return IsActive() && (Phase == ELDBossPhase::Pending || Phase == ELDBossPhase::Alive) && NowMs >= LeaveAtMs(); }
    // The spawn boundary while pending, the leave boundary while alive, MAX_int64 otherwise.
    int64 NextDueMs() const
    {
        return !IsActive() ? MAX_int64 : Phase == ELDBossPhase::Pending ? AppearAtMs() : Phase == ELDBossPhase::Alive ? LeaveAtMs() : MAX_int64;
    }
    bool IsBody(const FLDCombatBodyRef& Ref) const { return Phase == ELDBossPhase::Alive && Ref == Body; }
    // Pending -> Alive with the core's registered body; false when not due or already spawned.
    bool Spawned(const FLDCombatBodyRef& Ref, int64 NowMs);
    // A committed hostile contact from a contender body; refreshes that body's memory.
    void NoteAttack(const FLDCombatBodyRef& Source, int64 AtMs);
    // Alive -> Down once; false for a duplicate or another body.
    bool Died(const FLDCombatBodyRef& Ref, const FGuid& Killer, int64 AtMs);
    // Pending or Alive -> Left once at or after the leave instant; false otherwise. No killer, no reward.
    bool Leave(int64 NowMs);
    // The patrol goal, advancing past every waypoint already reached.
    const FVector& PatrolGoal() const { return Rules.Waypoints[Waypoint]; }
    void ReachedWaypoint() { Waypoint = (Waypoint + 1) % Rules.Waypoints.Num(); }
    // Target choice for one Actions step; drops attackers older than the memory.
    FLDBossDecision Decide(int64 NowMs, const FVector& SelfFootCm, TConstArrayView<LDBoss::FCandidate> Living,
        bool bMovementAllowed, bool bAttackReady);
    FLDBossView View() const;
};

namespace LDBoss
{
    // Rules from the approved Boss block (appear/leave T) and the four BossWaypoint anchors, for a match that started at
    // StartedAtMs.
    LOCKDOWN_API bool Resolve(const FLDDemoContentSnapshot& Content, const FLDDemoWorldGeometry& Geometry, int64 StartedAtMs,
        FLDBossState& Out, FString& Error);
}
