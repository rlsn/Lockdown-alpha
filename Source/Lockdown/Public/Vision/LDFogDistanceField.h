#pragma once
#include "CoreMinimal.h"

// Presentation only: reconstructs the received coverage bitmap. Never authorizes units or performs sight queries.
namespace LDFogDistanceField
{
    inline constexpr float RangeCm = 900.f;
    LOCKDOWN_API void Build(const TArray<uint8>& Bits, double CellX, double CellY, TArray<float>& Out);
    // The smallest rectangle of cells holding every cell where two fields differ; empty when none does.
    LOCKDOWN_API FIntRect Difference(const TArray<float>& A, const TArray<float>& B);
    // One blend step of Shown toward Target for the cells of Region alone: every other cell already shows its target and
    // keeps the pixel it was last given. Writes Region's BGRA pixels row by row (pitch Region.Width() * 4) and returns the
    // rectangle of the cells still short of their target, empty when the blend has settled.
    LOCKDOWN_API FIntRect Blend(const TArray<float>& Target, TArray<float>& Shown, const FIntRect& Region, float Alpha, uint8* OutPixels);
}
