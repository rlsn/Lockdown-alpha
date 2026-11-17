#pragma once

#include "CoreMinimal.h"

// The body's feedback timelines (APP_SPEC 8), shared by the own body, observed proxies and fall puppets. Every curve
// is a pure function of the seconds since its event began on the presentation clock and is back at rest by its end;
// the humanoid component poses the art catalog's clips and places effect shapes from them. Nothing here reads gameplay
// state.
namespace LDBodyMotion
{
    // A struck body has no action of its own: being hit never replaces, pauses or pushes the victim's pose.
    enum class EAction : uint8 { None, Attack, Cast };

    // Effect amounts at one instant: distances in cm, the rest 0..1. The limbs move by the skeletal clips.
    struct FChannels
    {
        float Lunge = 0;     // whole body shifted forward
        float Charge = 0;    // cast orb at the hand
        float Burst = 0;     // release flash at the striking or casting hand
        float Flash = 0;     // hit flash over the whole body
    };

    // Fallback impact age for cues that predate a known authoritative release time.
    constexpr float StrikeAt = .2f;
    LOCKDOWN_API FChannels Evaluate(EAction Action, float AgeSeconds, float ImpactSeconds = StrikeAt);
    // The victim's hit overlay alone: a short flash, no movement. A negative age shows nothing.
    constexpr float HitOverlaySeconds = .18f;
    LOCKDOWN_API FChannels HitOverlay(float HitAgeSeconds);
    // The action's channels with the hit overlay laid over them; the action keeps its pose and lunge.
    LOCKDOWN_API FChannels WithHit(EAction Action, float AgeSeconds, float HitAgeSeconds, float ImpactSeconds = StrikeAt);

    // Attacker confirmation (the owner's own hit receipt, presentation only): the striking pose holds for StopSeconds
    // from the confirmation, then catches up at 1.5x until it is back on the timeline after another 2 x StopSeconds.
    // Input, movement and authority time never pause. A negative confirmation age or no stop returns AgeSeconds.
    LOCKDOWN_API float HitStopPoseAge(float AgeSeconds, float ConfirmAgeSeconds, float StopSeconds);
    // The camera's push along the attacker's facing, in cm: up to KickCm a quarter in, back to 0 at KickSeconds.
    LOCKDOWN_API float KickOffset(float ConfirmAgeSeconds, float KickCm, float KickSeconds);
    // The confirmation spark at the striking hand, 0..1, over ConfirmSparkSeconds.
    constexpr float ConfirmSparkSeconds = .16f;
    LOCKDOWN_API float ConfirmSpark(float ConfirmAgeSeconds);
    // Presentation-only tilt opposite the actual horizontal push, in the body's local space.
    LOCKDOWN_API FRotator KnockbackLean(const FVector& LocalVelocity);

    // The clip time that shows an action AgeSeconds after it began: an attack or cast reaches its authored impact
    // frame at the authoritative release age; any other clip plays at its authored rate. Never negative.
    LOCKDOWN_API float ClipTime(EAction Action, float AgeSeconds, float ClipImpactSeconds, float ActionImpactSeconds, float AuthoredRate);

    // Two reports of one action kind are one instance unless the later one began this much later (a repeated hit or a
    // new swing). Without a known age a continuing kind is never restarted.
    constexpr double SameInstanceSeconds = .08;
    LOCKDOWN_API bool IsNewInstance(EAction Previous, double PreviousOnset, EAction Next, double NextOnset, bool bAgeKnown);

    // A withdrawn predicted swing rewinds from the age it showed back to rest over RetractSeconds instead of snapping;
    // a negative result means the rewind is over.
    constexpr float RetractSeconds = .1f;
    LOCKDOWN_API float RetractAge(float FromAgeSeconds, float SinceSeconds);

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
