#pragma once

#include "CoreMinimal.h"

namespace LDCamera
{
    inline constexpr float DefaultArmLength = 2250.f;
    inline constexpr float MinArmLength = 1300.f;
    inline constexpr float MaxArmLength = 2400.f;
    inline constexpr float ZoomStep = 80.f;
    inline constexpr float Pitch = -60.f;
    inline constexpr float Yaw = -45.f;
    inline constexpr float FieldOfView = 55.f;
    inline constexpr float TargetOffsetZ = 20.f;

    inline bool IsValidArmLength(float Value)
    { return FMath::IsFinite(Value) && Value >= MinArmLength && Value <= MaxArmLength; }
}
