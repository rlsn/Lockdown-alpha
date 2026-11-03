#pragma once

#include "CoreMinimal.h"

// The body's feedback timelines (APP_SPEC 8), shared by the own body, observed proxies and fall puppets. Every curve
// is a pure function of the seconds since its event began on the presentation clock and is back at rest by its end;
// the humanoid component poses the art catalog's clips and places effect shapes from them. Nothing here reads gameplay
// state.
namespace LDBodyMotion
{
    enum class EAction : uint8 { None, Attack, Cast, Hit };

    // Effect amounts at one instant: distances in cm, the rest 0..1. The limbs move by the skeletal clips.
    struct FChannels
    {
        float Lunge = 0;     // whole body shifted forward (negative: knocked back)
        float Charge = 0;    // cast orb at the hand
        float Burst = 0;     // impact flash at the hand or chest
        float Flash = 0;     // hit flash over the whole body
    };

    // A basic swing lands, and a cast releases, here: near the 250 ms base windup of a basic release and the 200 ms
    // windup of the Demo actives.
    constexpr float StrikeAt = .2f;
    LOCKDOWN_API FChannels Evaluate(EAction Action, float AgeSeconds);

    // The clip time that shows an action AgeSeconds after it began: an attack or cast clip with an impact frame
    // (ImpactSeconds at rate 1) reaches it at StrikeAt; any other clip plays at its authored rate. Never negative.
    LOCKDOWN_API float ClipTime(EAction Action, float AgeSeconds, float ImpactSeconds, float AuthoredRate);

    // Two reports of one action kind are one instance unless the later one began this much later (a repeated hit or a
    // new swing). Without a known age a continuing kind is never restarted.
    constexpr double SameInstanceSeconds = .08;
    LOCKDOWN_API bool IsNewInstance(EAction Previous, double PreviousOnset, EAction Next, double NextOnset, bool bAgeKnown);

    // A respawned body materialises; a dead body plays its death clip for LieSeconds, then sinks and shrinks away.
    constexpr float AppearSeconds = .6f;
    constexpr float FadeSeconds = .6f;
    struct FLife
    {
        float Scale = 1;   // body scale about its feet
        float Sink = 0;    // cm lowered
        float Column = 0;  // appear light column
        float Glow = 0;    // appear glow over the whole body
        bool bGone = false;
    };
    LOCKDOWN_API FLife Appear(float AgeSeconds);
    LOCKDOWN_API FLife Fall(float AgeSeconds, float LieSeconds);
}
