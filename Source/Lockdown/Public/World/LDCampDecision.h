#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "Vision/LDVisionDirector.h"
#include "World/LDWorldQueryTypes.h"

class FLDDemoContentSnapshot;

enum class ELDCampBrainPhase : uint8 { Idle, Engaged, Returning };
enum class ELDCampIntent : uint8 { Hold, Chase, RequestBasicAttack, ReturnHome, ResetReady };

struct FLDCampBrain
{
    ELDCampBrainPhase Phase = ELDCampBrainPhase::Idle;
    TOptional<double> LostTargetSinceSeconds;
};

struct FLDCampHome
{
    FLDWorldRevision World;
    FVector PositionCm = FVector::ZeroVector;
    FName SurfaceId;
};

struct FLDCampDecisionInput
{
    FLDMatchContext Match;
    FLDWorldRevision World;
    FLDCombatBodyRef Self;
    double NowSeconds = 0;
    FVector FootCm = FVector::ZeroVector;
    bool Grounded = false;
    FName SurfaceId;
    FVector CampAnchorFootCm = FVector::ZeroVector;
    FLDCampHome MemberHome;
    FLDCampBrain Brain;
    bool bObservationComplete = false;
    bool MovementAllowed = false;
    bool AttackAllowed = false;
    bool BasicAttackReady = false;
};

// #28: bounds one Returning cycle that finds no legal way home. Pure bookkeeping; the camp director reports
// plans and motion and resets the member where it stands once bDeadEnd is set (CAMP.ReturnDeadEnd*).
struct FLDCampReturnWatch
{
    uint64 Epoch = 0;
    int32 RefusedPlans = 0;
    double LastProgressSeconds = 0;
    double NextPlanSeconds = 0;
    bool bWarned = false;
    bool bDeadEnd = false;

    // A new ReturnEpoch starts fresh bookkeeping; the current epoch keeps it.
    void Observe(uint64 InEpoch, double NowSeconds);
    // Path progress, and time the member is not free to move, restart the no-progress clock.
    void Progressed(double NowSeconds) { LastProgressSeconds = NowSeconds; }
    bool MayPlan(double NowSeconds) const { return !bDeadEnd && NowSeconds >= NextPlanSeconds; }
    void PlanAccepted() { RefusedPlans = 0; }
    // A refused plan waits RetrySeconds before the next one; the MaxRefused-th consecutive refusal ends the return.
    // Returns true when this call set bDeadEnd.
    bool PlanRefused(double NowSeconds, int32 MaxRefused, double RetrySeconds);
    // Free to move but without path progress for LimitSeconds ends the return. Returns true when this call set bDeadEnd.
    bool Stalled(double NowSeconds, double LimitSeconds);
};

struct FLDCampDecision
{
    FLDCampBrain NextBrain;
    ELDCampIntent Intent = ELDCampIntent::Hold;
    TOptional<FLDCombatBodyRef> Target;
    TOptional<FVector> GoalFootCm;
};

namespace LDCampDecision
{
    // HostileSources are the contender bodies whose committed hostile contact with this camp the
    // member has consumed since its last reset. Candidates are this authority phase's samples that the
    // caller proved Visible with TraceSight from the member foot at the current registry revision; sight
    // faults are reported before this call. TraceSight decides occlusion only: a target is eligible
    // only when it is a hostile source within PerceptionRadiusCm of the member's current foot. The
    // leash bounds the member itself, never its target. Both views are borrowed; nothing is stored
    // and no write permit is granted.
    LOCKDOWN_API bool Evaluate(const FLDDemoContentSnapshot& Catalog, FName CampId,
        const FLDCampDecisionInput& Input, TConstArrayView<FLDCombatBodyRef> HostileSources,
        TConstArrayView<FLDAuthorityTargetSample> Candidates, FLDCampDecision& Out, FString& Error);
}
