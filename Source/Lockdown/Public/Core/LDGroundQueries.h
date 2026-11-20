#pragma once
#include "CoreMinimal.h"
class UWorld;
class AActor;
namespace LDGround
{
    // Public authored ground only; both trace channels ignore dynamic bodies.
    LOCKDOWN_API bool MapSurface(UWorld* World,const FVector& XY,FVector& Surface,FVector* Normal=nullptr);
}
