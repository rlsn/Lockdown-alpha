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
