#include "Character/LDModelPicking.h"

bool LDModelPicking::NearVolume(const FVector& Start, const FVector& Direction, double MaxDistance, const FVector& Foot,
    const FVector& Top, double RadiusCm, double AngleTolerance, double& OutAngle, double& OutDistance)
{
    OutAngle = 0.;
    OutDistance = 0.;
    const FVector Ray = Direction.GetSafeNormal();
    if (Start.ContainsNaN() || Ray.IsNearlyZero() || Foot.ContainsNaN() || Top.ContainsNaN() || !FMath::IsFinite(RadiusCm)
        || RadiusCm < 0 || !(AngleTolerance >= 0) || !(MaxDistance > 0)) { return false; }
    FVector OnRay, OnAxis;
    FMath::SegmentDistToSegmentSafe(Start, Start + Ray * MaxDistance, Foot, Top, OnRay, OnAxis);
    const double Along = FVector::DotProduct(OnRay - Start, Ray);
    if (Along <= UE_KINDA_SMALL_NUMBER) { return false; }
    const double Outside = FMath::Max(0., FVector::Dist(OnRay, OnAxis) - RadiusCm);
    OutAngle = FMath::Atan2(Outside, Along);
    OutDistance = Along;
    return OutAngle <= AngleTolerance;
}

bool LDModelPicking::Trace(TConstArrayView<FVector3f> Vertices, TConstArrayView<uint32> Indices,
    const FVector& Start, const FVector& End, double& OutFraction)
{
    OutFraction = 1.;
    if (Start.ContainsNaN() || End.ContainsNaN() || Indices.Num() % 3 != 0) { return false; }
    const FVector Direction = End - Start;
    if (Direction.IsNearlyZero()) { return false; }
    bool bHit = false;
    for (int32 Index = 0; Index < Indices.Num(); Index += 3)
    {
        const uint32 A = Indices[Index], B = Indices[Index + 1], C = Indices[Index + 2];
        if (A >= uint32(Vertices.Num()) || B >= uint32(Vertices.Num()) || C >= uint32(Vertices.Num())) { continue; }
        const FVector V0(Vertices[A]), Edge1 = FVector(Vertices[B]) - V0, Edge2 = FVector(Vertices[C]) - V0;
        const FVector P = FVector::CrossProduct(Direction, Edge2);
        const double Determinant = FVector::DotProduct(Edge1, P);
        if (FMath::Abs(Determinant) <= UE_DOUBLE_SMALL_NUMBER) { continue; }
        const double Inverse = 1. / Determinant;
        const FVector T = Start - V0;
        const double U = FVector::DotProduct(T, P) * Inverse;
        if (U < -UE_DOUBLE_SMALL_NUMBER || U > 1. + UE_DOUBLE_SMALL_NUMBER) { continue; }
        const FVector Q = FVector::CrossProduct(T, Edge1);
        const double V = FVector::DotProduct(Direction, Q) * Inverse;
        if (V < -UE_DOUBLE_SMALL_NUMBER || U + V > 1. + UE_DOUBLE_SMALL_NUMBER) { continue; }
        const double Fraction = FVector::DotProduct(Edge2, Q) * Inverse;
        if (Fraction >= 0. && Fraction <= OutFraction)
        {
            OutFraction = Fraction;
            bHit = true;
        }
    }
    return bHit;
}
