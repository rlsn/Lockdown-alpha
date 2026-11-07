#pragma once

#include "CoreMinimal.h"

namespace LDCamera
{
    inline constexpr float DefaultArmLength = 2250.f;
    inline constexpr float MinArmLength = 1300.f;
    inline constexpr float MaxArmLength = 2400.f;
    inline constexpr float ZoomStep = 80.f;
    inline constexpr float Pitch = -60.f;
    // #97: north-up (world -Y at the top of the screen), so the square field and its minimap line up with the screen.
    inline constexpr float Yaw = -90.f;
    inline constexpr float FieldOfView = 55.f;
    inline constexpr float TargetOffsetZ = 20.f;

    inline bool IsValidArmLength(float Value)
    { return FMath::IsFinite(Value) && Value >= MinArmLength && Value <= MaxArmLength; }
}
