#pragma once
#include "CoreMinimal.h"
#include "Combat/LDCombatTypes.h"
#include "Navigation/LDOrderTypes.h"

enum class ELDCombatPreInputMode : uint8
{
    Direct,
    Unit,
    GroundPoint
};

struct LOCKDOWN_API FPendingCombatIntent
{
    ELDCombatEntry Entry = ELDCombatEntry::None;
    ELDCombatPreInputMode CastMode = ELDCombatPreInputMode::Direct;
    FGuid TargetHandle;
    uint32 ObservationEpoch = 0;
    FVector GroundPointCm = FVector::ZeroVector;
    FName SurfaceHint;
    FVector AimDirection = FVector::ZeroVector;
    bool bOverdrive = false;
    int64 ExpiresAtMs = 0;
    FLDCombatActionRequest Action;
    FLDSpatialOrderRequest SpatialOrder;
};

namespace LDCombatPreInput
{
    constexpr int64 HoldWindowMs = 450;
    constexpr double HoldSeconds = 0.45;
    constexpr int32 MaxBufferedSteps = 2;
    constexpr double CloseDashConeCm = 220.0;
    constexpr double CloseDashStopCm = 135.0;
    constexpr double CloseDashCosHalfAngle = 0.8660254037844386; // cos(30 deg)

    // The server keeps a queued Q/W/E/D request while the caller's own ability windup releases within HoldWindowMs.
    LOCKDOWN_API bool ServerHolds(ELDCombatEntry Entry, bool bOwnAbilityWindup, int64 ReleaseAtMs, int64 NowMs);
    LOCKDOWN_API bool IsDirectEntry(ELDCombatEntry Entry);
    LOCKDOWN_API bool FiresOnPress(ELDCombatEntry Entry);
    LOCKDOWN_API bool IsWindingUp(const FLDCombatOwnerView& View, int64 NowMs);
    // Bounded 2-step FIFO buffer: a 3rd buffered intent evicts the oldest buffered intent and retains the newest 2 steps.
    LOCKDOWN_API void EnqueueFIFO(TArray<FPendingCombatIntent>& Queue, FPendingCombatIntent&& Intent,
        TOptional<FPendingCombatIntent>& OutEvicted);
    // Hard CC (stun/silence) clears buffered pre-inputs when it outlasts the remaining windup by more than HoldWindowMs.
    LOCKDOWN_API bool ShouldClearForHardControl(int64 ControlExpiresAtMs, int64 WindupReleaseAtMs);
    // Clamps an innate dash without card_pass_through to stop at 135cm from an engaged/hovered enemy within a 220cm, +-30 deg cone.
    LOCKDOWN_API double ClampInnateDashDistance(double PlannedDistanceCm, const FVector& StartFootCm,
        const FVector& Direction, const FVector& EnemyCenterCm, bool bHasPassThroughCard);
}
