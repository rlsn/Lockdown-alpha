#pragma once
#include "CoreMinimal.h"

// Presentation only: reconstructs the received coverage bitmap. Never authorizes units or performs sight queries.
namespace LDFogDistanceField
{
    inline constexpr float RangeCm = 900.f;
    LOCKDOWN_API void Build(const TArray<uint8>& Bits, double CellX, double CellY, TArray<float>& Out);
}
