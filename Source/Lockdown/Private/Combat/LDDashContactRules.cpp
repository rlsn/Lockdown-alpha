#include "Combat/LDDashContactRules.h"

bool LDDashContactRules::FirstContact(const FVector& SourceFrom, const FVector& SourceTo,
    const FVector& TargetFrom, const FVector& TargetTo, double RadiusSum, double StemHalfLengthSum,
    TOptional<double>& OutFraction, FString& Error)
{
    OutFraction.Reset();
    Error.Reset();
    if (SourceFrom.ContainsNaN() || SourceTo.ContainsNaN() || TargetFrom.ContainsNaN() || TargetTo.ContainsNaN()
        || !FMath::IsFinite(RadiusSum) || RadiusSum <= 0
        || !FMath::IsFinite(StemHalfLengthSum) || StemHalfLengthSum < 0)
    {
        Error = TEXT("Dash contact requires finite capsule trajectories and positive radii.");
        return false;
    }
    if (SourceFrom.Equals(SourceTo, UE_DOUBLE_SMALL_NUMBER)) { return true; }
    const FVector Offset = SourceFrom - TargetFrom;
    const FVector Velocity = (SourceTo - SourceFrom) - (TargetTo - TargetFrom);
    const auto SurfaceOffset = [StemHalfLengthSum](FVector Value)
    {
        Value.Z = Value.Z > StemHalfLengthSum ? Value.Z - StemHalfLengthSum
            : Value.Z < -StemHalfLengthSum ? Value.Z + StemHalfLengthSum : 0.;
        return Value;
    };
    const FVector Initial = SurfaceOffset(Offset);
    const double RadiusSquared = RadiusSum * RadiusSum;
    if (Offset.ContainsNaN() || Velocity.ContainsNaN() || !FMath::IsFinite(RadiusSquared)
        || !FMath::IsFinite(Initial.SizeSquared()) || !FMath::IsFinite(Velocity.SizeSquared()))
    { Error = TEXT("Dash contact arithmetic exceeded the finite geometry domain."); return false; }
    // Departing an existing overlap is not another collision, even when a new dash was just issued.
    if (Initial.SizeSquared() <= RadiusSquared
        && FVector::DotProduct(Initial, Velocity) >= 0) { return true; }

    TArray<double, TInlineAllocator<4>> Cuts{0., 1.};
    if (FMath::Abs(Velocity.Z) > UE_DOUBLE_SMALL_NUMBER)
    {
        for (const double Height : {-StemHalfLengthSum, StemHalfLengthSum})
        {
            const double At = (Height - Offset.Z) / Velocity.Z;
            if (At > 0 && At < 1) { Cuts.Add(At); }
        }
    }
    Cuts.Sort();
    // The closest vertical points change only at the two capsule-stem boundaries.
    for (int32 Index = 1; Index < Cuts.Num(); ++Index)
    {
        const double From = Cuts[Index - 1], To = Cuts[Index];
        FVector Local = Offset + Velocity * From;
        FVector Rate = Velocity;
        const double MidHeight = Offset.Z + Velocity.Z * (From + To) * .5;
        if (MidHeight > StemHalfLengthSum) { Local.Z -= StemHalfLengthSum; }
        else if (MidHeight < -StemHalfLengthSum) { Local.Z += StemHalfLengthSum; }
        else { Local.Z = 0; Rate.Z = 0; }
        const double C = Local.SizeSquared() - RadiusSquared;
        if (C <= 0) { OutFraction = From; return true; }
        const double A = Rate.SizeSquared();
        if (A <= UE_DOUBLE_SMALL_NUMBER) { continue; }
        const double B = 2. * FVector::DotProduct(Local, Rate);
        const double Discriminant = B * B - 4. * A * C;
        if (!FMath::IsFinite(Discriminant))
        { Error = TEXT("Dash contact quadratic exceeded the finite geometry domain."); return false; }
        if (Discriminant < 0) { continue; }
        const double Root = (-B - FMath::Sqrt(Discriminant)) / (2. * A);
        if (Root >= 0 && Root <= To - From)
        {
            OutFraction = From + Root;
            return true;
        }
    }
    return true;
}
