#pragma once

#include "CoreMinimal.h"
#include "Character/LDNativeMotionTypes.h"

class ULDMapRegistry;
class FLDDemoContentSnapshot;
class UPrimitiveComponent;

// A capture disk. A body occupies it only while natively grounded on the registered capture ground with its feet inside
// the closed XY disk (the boundary counts as inside). A disk away from its anchor (a carried or private flagpole) accepts
// any natively grounded floor (bAnyGround).
struct LOCKDOWN_API FLDCaptureCircle
{
    FName CaptureId;
    FVector2D CenterCm = FVector2D::ZeroVector;
    double RadiusCm = 0;
    TWeakObjectPtr<UPrimitiveComponent> Ground;
    bool bAnyGround = false;

    bool IsValid() const;
    bool Supports(const FLDNativeSupportSample& Support) const;
    bool Contains(const FVector& FootCm, const FLDNativeSupportSample& Support) const;
};

struct FLDCaptureCrossing
{
    bool bInside = false;
    double Seconds = 0;
};

// The main flagpole's zone in the final field after a boundary: final_carry_flag moves it to its living owner's foot unless a
// final_cage fence stands (it stays where its owner fell or lost it); final_double_yellow shrinks it while its holder owns it.
struct FLDFinalZoneInput
{
    FVector2D CurrentCm = FVector2D::ZeroVector;
    double BaseRadiusCm = 0;
    double DoubleYellowRadiusCm = 0;
    // The graffiti owner's card (0 without an owner or a card) and his living foot (unset while dead or absent).
    uint8 OwnerMod = 0;
    TOptional<FVector2D> OwnerFootCm;
    bool bCageStanding = false;
};

namespace LDCaptureCircle
{
    LOCKDOWN_API void FinalZone(const FLDFinalZoneInput& In, FVector2D& OutCentreCm, double& OutRadiusCm);
    // Binds the compiled capture anchor and the approved CAPTURE.RadiusCm; no fallback geometry.
    LOCKDOWN_API bool Resolve(const ULDMapRegistry& Registry, const FLDDemoContentSnapshot& Content,
        FLDCaptureCircle& Out, FString& Error);
    // Walks accepted native records in order. Continuous movement reports every boundary contact
    // at its interpolated real time (a chord through the disk yields enter and exit); support
    // changes and depenetration adjustments are discontinuous before/after states.
    LOCKDOWN_API bool Extract(const FLDCaptureCircle& Circle, const FLDNativeMotionBatch& Batch,
        bool& InOutInside, TArray<FLDCaptureCrossing>& Out, FString& Error);
}
