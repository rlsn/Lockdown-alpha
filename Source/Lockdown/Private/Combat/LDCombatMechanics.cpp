#include "Combat/LDCombatMechanics.h"

namespace LDCombatMechanics
{
bool ShieldBroken(int64 BeforeP2, int64 AfterP2, int64 AbsorbedP2)
{
    return BeforeP2 > 0 && AfterP2 <= 0 && AbsorbedP2 > 0;
}

bool KnockbackDirection(const FVector& SourceFoot, const FVector& TargetFoot, double SourceYawDeg, FVector& OutDirection)
{
    OutDirection = FVector::ZeroVector;
    if (SourceFoot.ContainsNaN() || TargetFoot.ContainsNaN() || !FMath::IsFinite(SourceYawDeg)) { return false; }
    const FVector Planar(TargetFoot.X - SourceFoot.X, TargetFoot.Y - SourceFoot.Y, 0.);
    if (Planar.SizeSquared() > FMath::Square(UE_KINDA_SMALL_NUMBER))
    {
        OutDirection = Planar.GetSafeNormal();
        return true;
    }
    const double Radians = FMath::DegreesToRadians(SourceYawDeg);
    OutDirection = FVector(FMath::Cos(Radians), FMath::Sin(Radians), 0.);
    return true;
}

int64 NextPulseAtMs(int64 StartMs, int64 DelayMs, int64 IntervalMs, int32 Count, int64 FromMs)
{
    if (DelayMs < 0 || IntervalMs <= 0 || Count <= 0 || StartMs < 0
        || StartMs > MAX_int64 - DelayMs - IntervalMs * int64(Count - 1)) { return 0; }
    const int64 First = StartMs + DelayMs;
    if (FromMs <= First) { return First; }
    const int64 Steps = (FromMs - First + IntervalMs - 1) / IntervalMs;
    return Steps < Count ? First + Steps * IntervalMs : 0;
}
}
