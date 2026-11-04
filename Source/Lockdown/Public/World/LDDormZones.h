#pragma once

#include "CoreMinimal.h"

class ULDMapRegistry;
class FLDDemoContentSnapshot;

// D21 (#79): the public dorm disks. A contender whose foot stands inside one (closed XY disk, boundary inside) receives
// the dorm's fast recovery once out of combat long enough. A judgment area only: no collision, navigation, sight or
// protection of any kind.
struct LOCKDOWN_API FLDDormZones
{
    struct FDorm
    {
        FName Id;
        FVector2D CenterCm = FVector2D::ZeroVector;
    };
    TArray<FDorm> Dorms;
    double RadiusCm = 0;

    bool IsValid() const;
    // The dorm containing FootCm, or nullptr.
    const FDorm* Find(const FVector& FootCm) const;
};

namespace LDDormZones
{
    // Binds every compiled dorm anchor to its registered ground position and the approved DORM.RadiusCm; no fallback.
    LOCKDOWN_API bool Resolve(const ULDMapRegistry& Registry, const FLDDemoContentSnapshot& Content,
        FLDDormZones& Out, FString& Error);
}
