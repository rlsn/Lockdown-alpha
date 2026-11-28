#pragma once
#include "CoreMinimal.h"
#include "CollisionShape.h"

class ULDMapRegistry;

// Where a ground-point skill lands: the requested point clamped to the reach when it is legal ground with a complete
// native route from the caster's own legal ground; otherwise the first such ground back along the cast toward the caster,
// sampled every StepCm. A wall, a point off the walkable ground and a roof (standing room without a walk to it) land short;
// a raised platform reached by its ramp stays legal. A caster without legal ground (in the air) lands nothing. Only a
// world without navigation data (a rule fixture) leaves the route unjudged.
namespace LDGroundLanding
{
    constexpr double StepCm = 25.;
    // False only on an error (a missing or stale world, an unexpected query failure); Out is unset when nothing lands.
    LOCKDOWN_API bool Resolve(const ULDMapRegistry& Registry, const FVector& CasterFoot, const FVector& Requested, double RangeCm,
        const FCollisionShape& Capsule, TOptional<FVector>& Out, FString& Error);

    // Where a forced push (knockback, pull) from Foot along a planar Direction stops: the farthest point of its straight line
    // whose every sample from the start (each PushStepCm, and the end) is legal ground no steeper than a walkable rise from
    // the one before. Unlike a ground skill's landing it never crosses an obstacle or goes round it; 0 when the first
    // sample is not legal. False only on an error.
    constexpr double PushStepCm = 10.;
    LOCKDOWN_API bool ResolvePush(const ULDMapRegistry& Registry, const FVector& Foot, const FVector& Direction, double DistanceCm,
        const FCollisionShape& Capsule, double& OutCm, FString& Error);
}
