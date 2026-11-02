#pragma once

#include "CoreMinimal.h"
#include "Combat/LDCombatTypes.h"

struct FLDCombatEntityRecord;
class FLDDemoContentSnapshot;

namespace LDCombatSourceRules
{
    // COMBAT 5.1 persistent cooldown binding for one complete post-change build. A learned definition keeps
    // its single track and end time. Each held active gear copy keeps the track bound to its instance, else
    // takes the latest unexpired unbound track of its definition (debt first, stable id on ties), else a
    // ready one, else a new track up to Progression.EquipmentSlots per definition. Pure; Apply installs it.
    LOCKDOWN_API bool PlanTrackBindings(const FLDCombatEntityRecord& Record, const FLDBuildProjectionInput& Input,
        int64 NowMs, const FLDDemoContentSnapshot& Content, TArray<FLDCombatBindingPlan>& Out,
        ELDCombatPrepareError& Code, FString& Error);
}
