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

#if !UE_BUILD_SHIPPING
    // DEV play-test presets (console ld.Dev.FeelPreset 0-3); 0 keeps the shipped camera and pace.
    struct FFeelPreset
    {
        float ArmScale = 1.f;
        float Pitch = LDCamera::Pitch;
        float FieldOfView = LDCamera::FieldOfView;
        // How far the view leads the body's movement direction at full speed (0 = rigid follow).
        float LookAheadCm = 0.f;
        // Applied to every body's move speed on the authority (Listen Host) only.
        float MoveSpeedScale = 1.f;
    };
    LOCKDOWN_API FFeelPreset DevFeelPreset();
#endif

    inline float PanDelta(float Delta)
    {
        // A stalled frame is not a history of pointer/key input; do not turn it into a multi-metre pan.
        return FMath::IsFinite(Delta) ? FMath::Clamp(Delta,0.f,.05f) : 0.f;
    }

    inline bool IsValidArmLength(float Value)
    { return FMath::IsFinite(Value) && Value >= MinArmLength && Value <= MaxArmLength; }
}
