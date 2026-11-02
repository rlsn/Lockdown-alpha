#pragma once
#include "CoreMinimal.h"
#include "Combat/LDCombatEvaluationTypes.h"

class FLDDemoContentSnapshot;

namespace LDTactical
{
    LOCKDOWN_API FLDDamageEvaluationResult EvaluateDamage(
        const FLDDamageEvaluationInput& Input, const FLDDemoContentSnapshot& Catalog);
    LOCKDOWN_API FLDBasicTimingResult EvaluateBasicTiming(
        const FLDBasicTimingInput& Input, const FLDDemoContentSnapshot& Catalog);

}
