#pragma once
#include "CoreMinimal.h"

// Which authored action motion a body shows for its current attack or cast (AUDIO_AND_FEEL §7.2). Presentation only: the
// clip is still time-warped so its release lands on the authoritative impact (LDBasketballMotion::SampleTime); nothing here
// changes timing, movement, collision or authority. Built on the project-owned KB basketball bank (ULDBasketballMotionSet).
namespace LDActionMotion
{
    enum class EFamily : uint8
    {
        // Basic combo: step 1 a right-hand jab, step 2 a left-hand cross with more torso, step 3 a full-body finisher.
        Jab,
        Cross,
        Finisher,
        // Skills by what they do: a one-hand throw (projectiles, targeted strikes), a two-hand set shot (areas, voice,
        // fields), a full-body heavy slam (impacts and charges), a guarded ready stance (self buffs and stances).
        OneHandThrow,
        TwoHandShot,
        HeavySlam,
        Ready,
    };
    // ComboStep 1..3 for a basic attack (0 = not a basic); Ability is the cast's ELDDemoAbilityId (0 = none).
    LOCKDOWN_API EFamily FamilyOf(bool bAttack, uint8 ComboStep, uint8 Ability);
    LOCKDOWN_API const TCHAR* FamilyName(EFamily Family);

    // How much of the body a family drives over the locomotion: 0..1 for the full body, the upper body and the arms.
    struct FBlend
    {
        float Full = 0.f;
        float Upper = 0.f;
        float Arm = 0.f;
    };
    // Moving bodies keep their legs (no full-body family while running).
    LOCKDOWN_API FBlend BlendOf(EFamily Family, bool bMoving);

    // The body's shape over a strike, read from above: the chest winds one way through the windup and snaps through the
    // other at the release, the cross steps aside, the finisher sinks then drives in. Degrees of yaw (positive turns the
    // body to its right), centimetres to its right and a scale on the forward lunge. Rest before the onset and after
    // Impact + SettleSeconds, peaking exactly at Impact, so the authoritative release time is the body's turning point.
    struct FShape
    {
        float YawDeg = 0.f;
        float SideCm = 0.f;
        float LungeScale = 1.f;
    };
    constexpr float SettleSeconds = .25f;
    LOCKDOWN_API FShape ShapeOf(EFamily Family, float AgeSeconds, float ImpactSeconds);
}
