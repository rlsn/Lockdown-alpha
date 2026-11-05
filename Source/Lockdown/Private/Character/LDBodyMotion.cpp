#include "Character/LDBodyMotion.h"

float LDBodyMotion::ImpactPoseAge(EAction Action, float AgeSeconds, float HitAgeSeconds)
{
    return (Action==EAction::Attack || Action==EAction::Cast) && HitAgeSeconds>=0.f && HitAgeSeconds<HitPoseHoldSeconds
        ? FMath::Max(0.f,AgeSeconds-HitAgeSeconds) : AgeSeconds;
}

LDBodyMotion::FChannels LDBodyMotion::WithHit(EAction Action, float AgeSeconds, float HitAgeSeconds)
{
    FChannels Result=Evaluate(Action,AgeSeconds);
    if (Action!=EAction::Hit)
    {
        const FChannels Hit=Evaluate(EAction::Hit,HitAgeSeconds);
        Result.Flash=FMath::Max(Result.Flash,Hit.Flash);
        Result.HitBurst=Hit.Burst;
        Result.Lunge+=Hit.Lunge;
    }
    return Result;
}

namespace LDBodyMotionDetail
{
    float Smooth(float X)
    {
        X = FMath::Clamp(X, 0.f, 1.f);
        return X * X * (3.f - 2.f * X);
    }

    // 0 until From, 1 at Peak, 0 again from To.
    float Pulse(float T, float From, float Peak, float To)
    {
        if (T <= From || T >= To) { return 0.f; }
        return T < Peak ? Smooth((T - From) / (Peak - From)) : 1.f - Smooth((T - Peak) / (To - Peak));
    }
}

LDBodyMotion::FChannels LDBodyMotion::Evaluate(EAction Action, float T)
{
    using namespace LDBodyMotionDetail;
    FChannels C;
    if (!FMath::IsFinite(T) || T < 0.f) { return C; }
    switch (Action)
    {
    case EAction::Attack:
        // The body steps into the blow and back while the clip swings; a spark flashes at the strike.
        C.Lunge = 14.f * Pulse(T, .08f, StrikeAt + .02f, .45f);
        C.Burst = Pulse(T, StrikeAt - .01f, StrikeAt + .03f, StrikeAt + .14f);
        break;
    case EAction::Cast:
        // An orb grows at the hand over the windup and bursts at the release.
        C.Charge = Smooth(T / .15f) * (1.f - Smooth((T - .18f) / .05f));
        C.Lunge = 6.f * Pulse(T, .15f, StrikeAt + .02f, .5f);
        C.Burst = Pulse(T, .17f, .21f, .34f);
        break;
    case EAction::Hit:
        // A short white-to-red flash and an impact spark, knocked back and recovering within the 300 ms cue.
        C.Lunge = -16.f * Pulse(T, 0.f, .05f, .3f);
        C.Flash = T < .18f ? 1.f - T / .18f : 0.f;
        C.Burst = Pulse(T, 0.f, .03f, .14f);
        break;
    default:
        break;
    }
    return C;
}

float LDBodyMotion::ClipTime(EAction Action, float T, float ImpactSeconds, float AuthoredRate)
{
    if (Action == EAction::None || !FMath::IsFinite(T) || T <= 0.f) { return 0.f; }
    const bool bTimed = (Action == EAction::Attack || Action == EAction::Cast) && FMath::IsFinite(ImpactSeconds) && ImpactSeconds > 0.f;
    const float Rate = bTimed ? ImpactSeconds / StrikeAt : FMath::IsFinite(AuthoredRate) ? FMath::Max(AuthoredRate, 0.f) : 0.f;
    return T * Rate;
}

bool LDBodyMotion::IsNewInstance(EAction Previous, double PreviousOnset, EAction Next, double NextOnset, bool bAgeKnown)
{
    if (Next == EAction::None) { return false; }
    if (Next != Previous) { return true; }
    return bAgeKnown && NextOnset - PreviousOnset > SameInstanceSeconds;
}

LDBodyMotion::FLife LDBodyMotion::Appear(float T)
{
    using namespace LDBodyMotionDetail;
    FLife Life;
    if (!FMath::IsFinite(T) || T >= AppearSeconds) { return Life; }
    T = FMath::Max(T, 0.f);
    // Grows from a fifth of its size with a slight overshoot inside a fading light column.
    Life.Scale = T < .35f ? FMath::Lerp(.2f, 1.08f, Smooth(T / .35f)) : FMath::Lerp(1.08f, 1.f, Smooth((T - .35f) / .15f));
    Life.Column = 1.f - Smooth(T / AppearSeconds);
    Life.Glow = 1.f - Smooth(T / .4f);
    return Life;
}

LDBodyMotion::FLife LDBodyMotion::Fall(float T, float LieSeconds)
{
    using namespace LDBodyMotionDetail;
    FLife Life;
    const float Lie = FMath::IsFinite(LieSeconds) ? FMath::Max(LieSeconds, 0.f) : 0.f;
    if (!FMath::IsFinite(T)) { T = Lie + FadeSeconds; }
    T = FMath::Max(T, 0.f);
    // Lies down through the death clip, then sinks and shrinks away.
    Life.Sink = 40.f * Smooth((T - Lie) / .5f);
    Life.Scale = 1.f - Smooth((T - Lie - .05f) / .5f);
    Life.bGone = T >= Lie + FadeSeconds;
    return Life;
}
