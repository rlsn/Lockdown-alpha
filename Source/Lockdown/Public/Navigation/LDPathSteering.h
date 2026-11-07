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
    // Unit-bounded planar steering towards Path[Index]; advances Index past reached or passed corners. Only the final
    // point eases in, so hugging an obstacle never slows the body at intermediate corners.
    LOCKDOWN_API FVector Steering(const TArray<FVector>& Path, int32& Index, const ACharacter& Body);
    // Grounded, no root motion and within Acceptance of Goal (planar footpoint distance).
    LOCKDOWN_API bool AtGoal(const ACharacter& Body, const FVector& Goal, double AcceptanceCm);
    // A blocking body's planar footprint.
    struct FBodyDisc { FVector2D Foot; double Radius=0; };
    // #97: bodies block each other, so a clicked spot another body stands on can never be reached. The spot is held by
    // every other body too close to it for this body to stand there, and by any body touching a holder (a crowd at the
    // spot). A ground walk that touches the holding group has arrived. Touching means footprints within
    // WaypointToleranceCm of contact, the native movement's separation from a blocking capsule included.
    LOCKDOWN_API bool BlockedAtHeldGoal(const FVector2D& Foot, double Radius, const FVector2D& Goal, TConstArrayView<FBodyDisc> Others);
    // BlockedAtHeldGoal for a grounded body against every other live character in its world whose capsule blocks pawns.
    LOCKDOWN_API bool BlockedAtHeldGoal(const ACharacter& Body, const FVector& Goal);
    // Route endpoint for any registered body from its validated ground: a planner's own start, or the foot of a body it
    // chases. Recast erodes the walkable area conservatively, so a legal capsule against a wall or another body, or one
    // cutting a corner, can stand just outside the mesh: the endpoint then moves to the nearest navmesh point at most one
    // capsule radius away, re-validated as legal ground on the same support. Otherwise Out is BodyGround and the strict
    // registry path query explains any refusal.
    LOCKDOWN_API bool PlanStart(const ULDMapRegistry& Registry, const FLDLegalGround& BodyGround, const FCollisionShape& Capsule,
        FLDLegalGround& Out, ELDWorldQueryFailure& Failure, FString& Error);
    // How a route treats its goal: a clicked ground point, or a body's foot treated like a body.
    enum class EGoal : uint8 { Ground, Body };
    // Complete route from a body's validated ground to Goal on SurfaceId. The start always goes through PlanStart. A body
    // goal goes through PlanStart too. A ground goal is planned exactly as clicked when that is a legal reachable stand;
    // otherwise (a wall, a box top, too close to an obstacle, or a disconnected spot) it is projected at the body's floor
    // height toward the nearest reachable navmesh point. A partial path limits candidates to the start's connected
    // navmesh; candidates must pass legal-ground and complete-route validation. Stale/missing world, surface or capsule
    // errors never project, and projection is refused only when no legal reachable candidate exists. OutGoal is the planned end.
    LOCKDOWN_API bool PlanRoute(const ULDMapRegistry& Registry, const FLDLegalGround& BodyGround, const FVector& Goal,
        FName SurfaceId, EGoal GoalKind, const FCollisionShape& Capsule, FLDCompletePath& OutPath, FLDLegalGround& OutGoal,
        ELDWorldQueryFailure& Failure, FString& Error);
}
