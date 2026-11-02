#pragma once

#include "CoreMinimal.h"
#include "World/LDWorldQueryTypes.h"

class ACharacter;
class ULDMapRegistry;
struct FCollisionShape;

// Shared path-following math for every server-steered body (contenders and camp members).
// Navigation only supplies direction to the native CharacterMovement; it never integrates position.
namespace LDPathSteering
{
    constexpr double WaypointToleranceCm=12.;

    // Stops voluntary locomotion without cancelling gravity or an active root-motion source.
    LOCKDOWN_API void HaltLocomotion(ACharacter* Body);
    // Planar distance travelled along the polyline at the point closest to Position.
    LOCKDOWN_API double AlongPath(const TArray<FVector>& Path, const FVector& Position);
    // Unit-bounded planar steering towards Path[Index]; advances Index past reached waypoints.
    LOCKDOWN_API FVector Steering(const TArray<FVector>& Path, int32& Index, const ACharacter& Body);
    // Grounded, no root motion and within Acceptance of Goal (planar footpoint distance).
    LOCKDOWN_API bool AtGoal(const ACharacter& Body, const FVector& Goal, double AcceptanceCm);
    // Route start for any registered body from its validated BodyGround. Recast erodes the walkable area
    // conservatively, so a legal capsule against a wall or another body can stand just outside the mesh: the start
    // then moves to the nearest navmesh point at most one capsule radius away, re-validated as legal ground on the
    // same support. Otherwise Out is BodyGround and the strict registry path query explains any refusal.
    LOCKDOWN_API bool PlanStart(const ULDMapRegistry& Registry, const FLDLegalGround& BodyGround, const FCollisionShape& Capsule,
        FLDLegalGround& Out, ELDWorldQueryFailure& Failure, FString& Error);
}
