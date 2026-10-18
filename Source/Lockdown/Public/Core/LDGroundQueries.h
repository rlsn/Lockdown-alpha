#pragma once
#include "CoreMinimal.h"
class UWorld;
namespace LDGround
{
    // Shared planar melee/interaction reachability: blockers, walkable floor and no vertical step.
    LOCKDOWN_API bool ClearSegment(UWorld* World,const FVector& From,const FVector& To);
}
