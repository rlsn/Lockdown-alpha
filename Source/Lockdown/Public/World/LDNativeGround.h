#pragma once

#include "CoreMinimal.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"

class UPrimitiveComponent;
class UWorld;

// Standing support follows CharacterMovement's Pawn response; LDGroundAim is not an authoring requirement.
namespace LDNativeGround
{
    LOCKDOWN_API bool IsSupport(const UPrimitiveComponent& Component);
    LOCKDOWN_API FCollisionQueryParams Query(const UWorld& World);
    LOCKDOWN_API bool Trace(const UWorld& World, FHitResult& Hit, const FVector& Start, const FVector& End,
        const FCollisionQueryParams& Params);
    LOCKDOWN_API bool Sweep(const UWorld& World, FHitResult& Hit, const FVector& Start, const FVector& End,
        const FCollisionShape& Shape, const FCollisionQueryParams& Params);
    LOCKDOWN_API bool Overlaps(const UWorld& World, const FVector& Centre, const FCollisionShape& Shape,
        const FCollisionQueryParams& Params);
}
