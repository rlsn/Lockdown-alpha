#pragma once
#include "CoreMinimal.h"
#include "Vision/LDViewTypes.h"

// Local presentation only. Other-subject formatting accepts the eight-field
// observation, never a canonical actor or another player's owner state.
namespace LDHUD
{
    LOCKDOWN_API FString Inspector(const FLDOwnerState& Own,const FLDObservedEntity* Observed,double Now);
    LOCKDOWN_API FString Cue(uint8 Cue);
    LOCKDOWN_API double Range(ELDSkill Skill);
    LOCKDOWN_API FString Tooltip(const FLDCharacterBuild& Build,ELDSkill Skill);
    LOCKDOWN_API FVector2D MapPoint(const FVector& World,const FLDVisionGrid& Grid);
    LOCKDOWN_API FVector MapWorld(const FVector2D& Fraction,const FLDVisionGrid& Grid);
}
