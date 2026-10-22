#pragma once
#include "CoreMinimal.h"
class UWorld;
namespace LDGround
{
    // Shared planar melee/interaction reachability: blockers, walkable floor and no vertical step.
    LOCKDOWN_API bool ClearSegment(UWorld* World,const FVector& From,const FVector& To);
    // Public authored ground only; both trace channels ignore dynamic bodies.
    LOCKDOWN_API bool MapSurface(UWorld* World,const FVector& XY,FVector& Surface);
}
