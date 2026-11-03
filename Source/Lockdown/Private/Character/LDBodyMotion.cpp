#include "Character/LDBodyMotion.h"

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
    {
        // Drawn back over .15 s, flung forward over the next .06 s, held, then eased back.
        const float Back = Smooth(T / .15f);
        const float Swing = Smooth((T - .15f) / .06f);
        const float Recover = Smooth((T - .3f) / .2f);
        C.WindBack = Back * (1.f - Swing);
        C.Strike = Swing * (1.f - .6f * Recover);
        C.Lunge = 30.f * Swing * (1.f - Recover) - 4.f * C.WindBack;
        C.Lean = 14.f * Swing * (1.f - Recover) - 6.f * C.WindBack;
        C.Burst = Pulse(T, StrikeAt - .01f, StrikeAt + .03f, StrikeAt + .14f);
        break;
    }
    case EAction::Cast:
    {
        // Both arms raised with a growing orb, then pushed forward as the orb bursts.
        const float Up = Smooth(T / .18f);
        const float Push = Smooth((T - .18f) / .08f);
        const float Recover = Smooth((T - .4f) / .25f);
        C.Raise = Up * (1.f - Push);
        C.Thrust = Push * (1.f - .5f * Recover);
        C.Charge = Up * (1.f - Push);
        C.Lean = -8.f * C.Raise + 10.f * Push * (1.f - Recover);
        C.Lunge = 10.f * Push * (1.f - Recover);
        C.Burst = Pulse(T, .17f, .21f, .34f);
        break;
    }
    case EAction::Hit:
    {
        // A short white-to-red flash and an impact spark, knocked back and recovering within the 300 ms cue.
        const float Knock = Pulse(T, 0.f, .05f, .3f);
        C.Flinch = Knock;
        C.Lunge = -16.f * Knock;
        C.Lean = -24.f * Knock;
        C.Flash = T < .18f ? 1.f - T / .18f : 0.f;
        C.Burst = Pulse(T, 0.f, .03f, .14f);
        break;
    }
    default:
        break;
    }
    return C;
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

LDBodyMotion::FLife LDBodyMotion::Fall(float T)
{
    using namespace LDBodyMotionDetail;
    FLife Life;
    if (!FMath::IsFinite(T)) { T = FallSeconds; }
    T = FMath::Max(T, 0.f);
    // Topples backward with growing speed, then sinks and shrinks away.
    const float Topple = FMath::Clamp(T / .35f, 0.f, 1.f);
    Life.Tilt = 90.f * Topple * Topple;
    Life.Sink = 40.f * Smooth((T - .65f) / .5f);
    Life.Scale = 1.f - Smooth((T - .7f) / .45f);
    Life.bGone = T >= FallSeconds;
    return Life;
}
