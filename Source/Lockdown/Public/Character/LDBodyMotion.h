#pragma once

#include "CoreMinimal.h"

// The body's feedback timelines (APP_SPEC 8), shared by the own body, observed proxies and fall puppets. Every curve
// is a pure function of the seconds since its event began on the presentation clock and is back at rest by its end;
// the humanoid component only places shapes from them. Nothing here reads gameplay state.
namespace LDBodyMotion
{
    enum class EAction : uint8 { None, Attack, Cast, Hit };

    // Channel amounts at one instant: distances in cm, angles in degrees, the rest 0..1.
    struct FChannels
    {
        float WindBack = 0;  // striking arm drawn back
        float Strike = 0;    // striking arm flung forward
        float Raise = 0;     // both arms raised over the head
        float Thrust = 0;    // both arms pushed forward
        float Flinch = 0;    // arms thrown outward by a hit
        float Lunge = 0;     // body shifted forward (negative: knocked back)
        float Lean = 0;      // torso pitched forward (negative: back)
        float Charge = 0;    // cast orb over the hands
        float Burst = 0;     // impact flash at the hand or chest
        float Flash = 0;     // hit tint over the whole body
    };

    // A basic swing is drawn back first and strikes here, near the 250 ms base windup of its release.
    constexpr float StrikeAt = .2f;
    LOCKDOWN_API FChannels Evaluate(EAction Action, float AgeSeconds);

    // Two reports of one action kind are one instance unless the later one began this much later (a repeated hit or a
    // new swing). Without a known age a continuing kind is never restarted.
    constexpr double SameInstanceSeconds = .08;
    LOCKDOWN_API bool IsNewInstance(EAction Previous, double PreviousOnset, EAction Next, double NextOnset, bool bAgeKnown);

    // A respawned body materialises; a dead body falls on its back, sinks and shrinks away.
    constexpr float AppearSeconds = .6f;
    constexpr float FallSeconds = 1.2f;
    struct FLife
    {
        float Scale = 1;   // body scale about its feet
        float Tilt = 0;    // degrees fallen backward
        float Sink = 0;    // cm lowered
        float Column = 0;  // appear light column
        float Glow = 0;    // appear tint over the whole body
        bool bGone = false;
    };
    LOCKDOWN_API FLife Appear(float AgeSeconds);
    LOCKDOWN_API FLife Fall(float AgeSeconds);
}
