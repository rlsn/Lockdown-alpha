#pragma once
#include "CoreMinimal.h"
namespace LDTactical
{
    struct FSmokeCircle {FVector Centre;double Radius;};
    // A ray may stay inside the union; crossing any exposed union boundary occludes.
    LOCKDOWN_API bool SmokeOccludes(const FVector& From,const FVector& To,const TArray<FSmokeCircle>& Circles);
}
