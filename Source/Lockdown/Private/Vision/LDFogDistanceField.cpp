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
TArray<float> DistanceTo(const TArray<uint8>& Bits,const FIntRect& Region,bool bVisible,double CellX,double CellY)
{
    const int32 Columns=Region.Width(), RowsCount=Region.Height();
    TArray<float> Input, Rows, Result;
    Input.SetNumUninitialized(Region.Area()); Rows.SetNumUninitialized(Region.Area()); Result.SetNumUninitialized(Region.Area());
    for (int32 Y=0; Y<RowsCount; ++Y)
    {
        for (int32 X=0; X<Columns; ++X)
        {
            const int32 Index=(Region.Min.Y+Y)*Width+Region.Min.X+X;
            Input[Y*Columns+X]=(LDObservation::CoverageBit(Bits,Index)==bVisible)?0.f:1.e12f;
        }
        DistanceLine(Input.GetData()+Y*Columns,Columns,1,CellX*CellX,Rows.GetData()+Y*Columns);
    }
    for (int32 X=0; X<Columns; ++X) { DistanceLine(Rows.GetData()+X,RowsCount,Columns,CellY*CellY,Result.GetData()+X); }
    return Result;
}
}

void LDFogDistanceField::Build(const TArray<uint8>& Bits, double CellX, double CellY, TArray<float>& Out)
{
    check(Bits.Num() == LDObservation::CoverageBytes && CellX > 0 && CellY > 0);
    using namespace LDFogDistanceFieldPrivate;
    Out.Init(0.f,LDObservation::CoverageCells);
    FIntRect Region(Width,Height,0,0);
    for (int32 I=0; I<Bits.Num(); ++I)
    {
        if (!Bits[I]) { continue; }
        const int32 X=(I*8)%Width, Y=(I*8)/Width;
        Region.Min.X=FMath::Min(Region.Min.X,X); Region.Max.X=FMath::Max(Region.Max.X,X+8);
        Region.Min.Y=FMath::Min(Region.Min.Y,Y); Region.Max.Y=FMath::Max(Region.Max.Y,Y+1);
    }
    if (Region.Width()<=0) { return; }
    // Outside this rectangle the hidden distance is already saturated. The extra hidden rows and columns also
    // supply the nearest hidden site for a visible cell next to a cropped edge.
    const double HalfCell=FMath::Min(CellX,CellY)*.5;
    const int32 PadX=FMath::CeilToInt(FMath::Min(double(Width),(RangeCm+HalfCell)/CellX));
    const int32 PadY=FMath::CeilToInt(FMath::Min(double(Height),(RangeCm+HalfCell)/CellY));
    Region.Min=FIntPoint(FMath::Max(0,Region.Min.X-PadX),FMath::Max(0,Region.Min.Y-PadY));
    Region.Max=FIntPoint(FMath::Min(Width,Region.Max.X+PadX),FMath::Min(Height,Region.Max.Y+PadY));
    const TArray<float> ToVisible=DistanceTo(Bits,Region,true,CellX,CellY);
    const TArray<float> ToHidden=DistanceTo(Bits,Region,false,CellX,CellY);
    for (int32 Y=Region.Min.Y; Y<Region.Max.Y; ++Y)
    {
        for (int32 X=Region.Min.X; X<Region.Max.X; ++X)
        {
            const int32 Index=Y*Width+X, Local=(Y-Region.Min.Y)*Region.Width()+X-Region.Min.X;
            const bool bVisible=LDObservation::CoverageBit(Bits,Index);
            const float Distance=FMath::Max(0.f,FMath::Sqrt(bVisible?ToHidden[Local]:ToVisible[Local])-float(HalfCell));
            Out[Index]=FMath::Clamp((bVisible?Distance:-Distance)/RangeCm*.5f+.5f,0.f,1.f);
        }
    }
}
