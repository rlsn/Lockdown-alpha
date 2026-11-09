#pragma once

#include "CoreMinimal.h"
#include "Framework/LDMatchLifecycleTypes.h"

class UPrimitiveComponent;

struct FLDWorldRevision
{
    FName MapId;
    uint32 LayoutRevision = 0;
    uint32 GeometryRevision = 0;

    bool IsValid() const { return !MapId.IsNone() && LayoutRevision != 0 && GeometryRevision != 0; }
    bool operator==(const FLDWorldRevision& Other) const
    {
        return MapId == Other.MapId && LayoutRevision == Other.LayoutRevision && GeometryRevision == Other.GeometryRevision;
    }
    bool operator!=(const FLDWorldRevision& Other) const { return !(*this == Other); }
};

enum class ELDWorldQueryFailure : uint8
{
    None, MissingWorld, StaleWorld, InvalidPosition, InvalidCapsule, InvalidSurface, WrongSurface,
    NotGrounded, Blocked, MissingNavigation, IncompletePath, Unreachable
};

struct FLDLegalGround
{
    FLDWorldRevision World;
    FVector FootCm = FVector::ZeroVector;
    FVector SurfaceCm = FVector::ZeroVector;
    FName SurfaceId;
    TWeakObjectPtr<UPrimitiveComponent> Floor;
};

struct FLDCompletePath
{
    FLDWorldRevision World;
    TArray<FVector> Points;
    FVector StartProjectionCm = FVector::ZeroVector;
    FVector EndProjectionCm = FVector::ZeroVector;
    double LengthCm = 0;
};

