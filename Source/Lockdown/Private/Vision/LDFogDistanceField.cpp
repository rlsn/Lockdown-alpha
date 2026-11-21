#include "Vision/LDFogDistanceField.h"
#include "Vision/LDViewTypes.h"

namespace LDFogDistanceFieldPrivate
{
constexpr int32 Width = LDObservation::CoverageWidth, Height = LDObservation::CoverageHeight;
constexpr int32 Longest = Width > Height ? Width : Height;
void DistanceLine(const float* Input, int32 Count, int32 Stride, double CellSquared, float* Output)
{
    int32 Sites[Longest];
    double Cuts[Longest + 1];
    int32 K = 0;
    Sites[0] = 0; Cuts[0] = -DBL_MAX; Cuts[1] = DBL_MAX;
    for (int32 Q = 1; Q < Count; ++Q)
    {
        double Crossing;
        do
        {
            const int32 V = Sites[K];
            Crossing = (double(Input[Q * Stride]) + CellSquared * Q * Q
                - double(Input[V * Stride]) - CellSquared * V * V) / (2 * CellSquared * (Q - V));
            if (Crossing > Cuts[K]) { break; }
            --K;
        } while (K >= 0);
        ++K; Sites[K] = Q; Cuts[K] = Crossing; Cuts[K + 1] = DBL_MAX;
    }
    K = 0;
    for (int32 Q = 0; Q < Count; ++Q)
    {
        while (Cuts[K + 1] < Q) { ++K; }
        const double Delta = Q - Sites[K];
        Output[Q * Stride] = float(CellSquared * Delta * Delta + Input[Sites[K] * Stride]);
    }
}
TArray<float> DistanceTo(const TArray<uint8>& Bits, bool bVisible, double CellX, double CellY)
{
    TArray<float> Input, Rows, Result;
    Input.SetNumUninitialized(Width * Height); Rows.SetNumUninitialized(Width * Height); Result.SetNumUninitialized(Width * Height);
    for (int32 I = 0; I < Input.Num(); ++I)
    { Input[I] = (LDObservation::CoverageBit(Bits, I) == bVisible) ? 0.f : 1.e12f; }
    for (int32 Y = 0; Y < Height; ++Y) { DistanceLine(Input.GetData() + Y * Width, Width, 1, CellX * CellX, Rows.GetData() + Y * Width); }
    for (int32 X = 0; X < Width; ++X) { DistanceLine(Rows.GetData() + X, Height, Width, CellY * CellY, Result.GetData() + X); }
    return Result;
}
}

void LDFogDistanceField::Build(const TArray<uint8>& Bits, double CellX, double CellY, TArray<float>& Out)
{
    check(Bits.Num() == LDObservation::CoverageBytes && CellX > 0 && CellY > 0);
    const TArray<float> ToVisible = LDFogDistanceFieldPrivate::DistanceTo(Bits, true, CellX, CellY);
    const TArray<float> ToHidden = LDFogDistanceFieldPrivate::DistanceTo(Bits, false, CellX, CellY);
    Out.SetNumUninitialized(LDObservation::CoverageCells);
    for (int32 I = 0; I < Out.Num(); ++I)
    {
        const bool Visible = LDObservation::CoverageBit(Bits, I);
        const float Distance = FMath::Max(0.f, FMath::Sqrt(Visible ? ToHidden[I] : ToVisible[I]) - float(FMath::Min(CellX, CellY) * .5));
        Out[I] = FMath::Clamp((Visible ? Distance : -Distance) / RangeCm * .5f + .5f, 0.f, 1.f);
    }
}
