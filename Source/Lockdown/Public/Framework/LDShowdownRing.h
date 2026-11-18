#pragma once

#include "CoreMinimal.h"

struct FLDDemoContentParameters;
struct FLDNativeMotionBatch;

// The final showdown's street closure (SHOWDOWN.Ring*): from the closure start the ring shrinks linearly from InitialRadiusCm
// to FinalRadiusCm over DurationMs and then holds. Outside it a contender loses EarlyDamageBp of its max HP per second in the
// first half and LateDamageBp in the second, settled every TickMs; the shrink is previewed PreviewMs before it starts. Pure
// rules: the authority (ALDGameMode) passes every fact in. Times are ms since the closure start.
struct LOCKDOWN_API FLDShowdownRing
{
    double InitialRadiusCm = 0, FinalRadiusCm = 0;
    int64 DurationMs = 0, TickMs = 0, PreviewMs = 0;
    int64 EarlyDamageBp = 0, LateDamageBp = 0;

    // The approved ring of a closure DurationMs long.
    static bool FromContent(const FLDDemoContentParameters& Content, int64 DurationMs, FLDShowdownRing& Out, FString& Error);
    bool Validate(FString& Error) const;
    int64 MidMs() const { return DurationMs / 2; }
};

namespace LDShowdownRing
{
    LOCKDOWN_API double RadiusCm(const FLDShowdownRing& Ring, int64 ElapsedMs);
    // Damage rate outside the ring at ElapsedMs (0 before the closure).
    LOCKDOWN_API int64 DamageBp(const FLDShowdownRing& Ring, int64 ElapsedMs);
    // The next multiple of TickMs strictly after ElapsedMs (MAX_int64 when it cannot be represented or TickMs is invalid).
    LOCKDOWN_API int64 NextTickMs(const FLDShowdownRing& Ring, int64 ElapsedMs);
    // Adds MaxHpP2 * rate(bp) * ms over the outside interval [FromMs, ToMs), split at the rate boundary, in units of P2 * 1e7.
    // False (accumulator unchanged) on a negative or reversed input or an overflow.
    LOCKDOWN_API bool AccumulateDamage(const FLDShowdownRing& Ring, int64 MaxHpP2, int64 FromMs, int64 ToMs, int64& InOutAccumulatorE7);
    // Removes and returns the whole P2 part of the accumulator; the remainder stays for the next settlement.
    LOCKDOWN_API int64 TakeDamageP2(int64& InOutAccumulatorE7);
    // One straight piece of a contender's authoritative foot path, times in ms since the closure start (fractional).
    struct FFootSegment
    {
        double FromMs = 0, ToMs = 0;
        FVector FromCm = FVector::ZeroVector, ToCm = FVector::ZeroVector;
    };
    // The step's authoritative foot path over [FromMs, ToMs) from its native motion: standing at the batch's start foot until
    // the first record, every movement record as it was integrated, standing at the end foot after the last. False on a
    // record outside the window, a discontinuous or non-finite record.
    LOCKDOWN_API bool FootPathFromMotion(const FLDNativeMotionBatch& Batch, int64 ClosureStartedAtMs, int64 FromMs, int64 ToMs,
        TArray<FFootSegment>& Out);
    // The parts of the path spent outside the shrinking ring (XY, boundary inside), split exactly where the foot and the ring
    // cross; each crossing falls into its authority event ms (rounded up). Out holds sorted, disjoint [From, To) intervals.
    LOCKDOWN_API bool OutsideIntervals(const FLDShowdownRing& Ring, const FVector& CentreCm, const TArray<FFootSegment>& Path,
        TArray<TPair<int64, int64>>& Out);
}
