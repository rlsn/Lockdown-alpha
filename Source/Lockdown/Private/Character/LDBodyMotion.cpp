#include "Character/LDBodyMotion.h"

FRotator LDBodyMotion::KnockbackLean(const FVector& LocalVelocity)
{
    if (LocalVelocity.ContainsNaN()) { return FRotator::ZeroRotator; }
    return FRotator(FMath::Clamp(LocalVelocity.X / 300.0, -1.0, 1.0) * 14.0, 0,
        -FMath::Clamp(LocalVelocity.Y / 300.0, -1.0, 1.0) * 14.0);
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

LDBodyMotion::FChannels LDBodyMotion::Evaluate(EAction Action, float T, float ImpactSeconds)
{
    using namespace LDBodyMotionDetail;
    FChannels C;
    if (!FMath::IsFinite(T) || T < 0.f) { return C; }
    const float Impact = FMath::IsFinite(ImpactSeconds) ? FMath::Max(0.f, ImpactSeconds) : StrikeAt;
    const float TimingScale = Impact > 0.f ? Impact / StrikeAt : 1.f;
    switch (Action)
    {
    case EAction::Attack:
        // The body steps into the blow and back while the clip swings; a spark flashes at the strike.
        C.Lunge = 14.f * Pulse(T, .08f * TimingScale, Impact + .02f * TimingScale, .45f * TimingScale);
        C.Burst = Pulse(T, Impact - .04f * TimingScale, Impact, Impact + .14f * TimingScale);
        break;
    case EAction::Cast:
        // An orb grows at the hand over the windup and bursts at the release.
        C.Charge = Smooth(T / FMath::Max(.01f, Impact * .75f))
            * (1.f - Smooth((T - Impact * .9f) / FMath::Max(.01f, Impact * .25f)));
        C.Lunge = 6.f * Pulse(T, Impact * .75f, Impact + .02f * TimingScale, Impact + .3f * TimingScale);
        C.Burst = Pulse(T, Impact - .03f * TimingScale, Impact, Impact + .14f * TimingScale);
        break;
    default:
        break;
    }
    return C;
}

LDBodyMotion::FChannels LDBodyMotion::HitOverlay(float T)
{
    FChannels C;
    if (!FMath::IsFinite(T) || T < 0.f) { return C; }
    C.Flash = T < HitOverlaySeconds ? 1.f - T / HitOverlaySeconds : 0.f;
    return C;
}

LDBodyMotion::FChannels LDBodyMotion::WithHit(EAction Action, float AgeSeconds, float HitAgeSeconds, float ImpactSeconds)
{
    FChannels Result = Evaluate(Action, AgeSeconds, ImpactSeconds);
    const FChannels Hit = HitOverlay(HitAgeSeconds);
    Result.Flash = FMath::Max(Result.Flash, Hit.Flash);
    return Result;
}

float LDBodyMotion::HitStopPoseAge(float T, float A, float Stop)
{
    if (!FMath::IsFinite(T) || !FMath::IsFinite(A) || !FMath::IsFinite(Stop) || A < 0.f || Stop <= 0.f) { return T; }
    // The deficit grows with the hold, then shrinks linearly while the pose plays at 1.5x.
    const float Deficit = A < Stop ? A : Stop * FMath::Max(0.f, 1.f - (A - Stop) / (2.f * Stop));
    return FMath::Max(0.f, T - Deficit);
}

float LDBodyMotion::KickOffset(float A, float KickCm, float KickSeconds)
{
    if (!FMath::IsFinite(A) || !FMath::IsFinite(KickCm) || !FMath::IsFinite(KickSeconds) || KickSeconds <= 0.f) { return 0.f; }
    return KickCm * LDBodyMotionDetail::Pulse(A, 0.f, KickSeconds * .25f, KickSeconds);
}

float LDBodyMotion::ConfirmSpark(float A)
{
    return FMath::IsFinite(A) ? LDBodyMotionDetail::Pulse(A, 0.f, .025f, ConfirmSparkSeconds) : 0.f;
}

float LDBodyMotion::ClipTime(EAction Action, float T, float ClipImpactSeconds, float ActionImpactSeconds, float AuthoredRate)
{
    if (Action == EAction::None || !FMath::IsFinite(T) || T <= 0.f) { return 0.f; }
    const bool bTimed = (Action == EAction::Attack || Action == EAction::Cast)
        && FMath::IsFinite(ClipImpactSeconds) && ClipImpactSeconds > 0.f;
    if (bTimed && FMath::IsFinite(ActionImpactSeconds))
    {
        return ActionImpactSeconds <= 0.f ? ClipImpactSeconds : T * ClipImpactSeconds / ActionImpactSeconds;
    }
    return T * (FMath::IsFinite(AuthoredRate) ? FMath::Max(AuthoredRate, 0.f) : 0.f);
}

bool LDBodyMotion::IsNewInstance(EAction Previous, double PreviousOnset, EAction Next, double NextOnset, bool bAgeKnown)
{
    if (Next == EAction::None) { return false; }
    if (Next != Previous) { return true; }
    return bAgeKnown && NextOnset - PreviousOnset > SameInstanceSeconds;
}

float LDBodyMotion::RetractAge(float FromAgeSeconds, float SinceSeconds)
{
    if (!FMath::IsFinite(FromAgeSeconds) || !FMath::IsFinite(SinceSeconds) || SinceSeconds < 0.f || SinceSeconds >= RetractSeconds)
    { return -1.f; }
    return FMath::Max(FromAgeSeconds, 0.f) * (1.f - SinceSeconds / RetractSeconds);
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
