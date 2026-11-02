#pragma once

#include "CoreMinimal.h"
#include "Vision/LDViewTypes.h"

namespace LDOrderRules
{
    // D03 attack-move acquisition among the currently visible units: the planar-nearest unit within RadiusCm
    // of FootCm, never the attacker itself; equal distances take the smaller observer handle.
    // Returns the unit index, or INDEX_NONE when no candidate qualifies.
    LOCKDOWN_API int32 NearestCandidate(const TArray<FLDObservedUnitView>& Units, const FVector& FootCm,
        double RadiusCm, const FGuid& Self);
}
