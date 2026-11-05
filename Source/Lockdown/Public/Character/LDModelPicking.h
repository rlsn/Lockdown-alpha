#pragma once

#include "CoreMinimal.h"

namespace LDModelPicking
{
    // Approved control feel: a pointer this many screen pixels outside a unit's pick volume still selects it.
    constexpr double PickTolerancePx = 12.;
    // Narrow phase in the displayed mesh's space; empty space inside its bounds is never a hit.
    LOCKDOWN_API bool Trace(TConstArrayView<FVector3f> Vertices, TConstArrayView<uint32> Indices,
        const FVector& Start, const FVector& End, double& OutFraction);
    // Lenient fallback when no model surface is under the pointer: the ray passes within AngleTolerance (radians, the
    // screen tolerance seen from the ray start) of the upright pick volume, the segment Foot..Top swollen by RadiusCm.
    // OutAngle is how far outside the volume the ray passes (0 when it goes through it) and OutDistance the ray length
    // to its closest approach; only that approach within MaxDistance counts.
    LOCKDOWN_API bool NearVolume(const FVector& Start, const FVector& Direction, double MaxDistance, const FVector& Foot,
        const FVector& Top, double RadiusCm, double AngleTolerance, double& OutAngle, double& OutDistance);
}
