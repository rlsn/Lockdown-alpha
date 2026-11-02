#pragma once

#include "CoreMinimal.h"
#include "Character/LDNativeMotionTypes.h"

class ULDMapRegistry;
class FLDDemoContentSnapshot;
class UPrimitiveComponent;

// The unique capture disk. A body occupies it only while natively grounded on the registered
// capture ground with its feet inside the closed XY disk (the boundary counts as inside).
struct LOCKDOWN_API FLDCaptureCircle
{
    FName CaptureId;
    FVector2D CenterCm = FVector2D::ZeroVector;
    double RadiusCm = 0;
    TWeakObjectPtr<UPrimitiveComponent> Ground;

    bool IsValid() const;
    bool Supports(const FLDNativeSupportSample& Support) const;
    bool Contains(const FVector& FootCm, const FLDNativeSupportSample& Support) const;
};

struct FLDCaptureCrossing
{
    bool bInside = false;
    double Seconds = 0;
};

namespace LDCaptureCircle
{
    // Binds the compiled capture anchor and the approved CAPTURE.RadiusCm; no fallback geometry.
    LOCKDOWN_API bool Resolve(const ULDMapRegistry& Registry, const FLDDemoContentSnapshot& Content,
        FLDCaptureCircle& Out, FString& Error);
    // Walks accepted native records in order. Continuous movement reports every boundary contact
    // at its interpolated real time (a chord through the disk yields enter and exit); support
    // changes and depenetration adjustments are discontinuous before/after states.
    LOCKDOWN_API bool Extract(const FLDCaptureCircle& Circle, const FLDNativeMotionBatch& Batch,
        bool& InOutInside, TArray<FLDCaptureCrossing>& Out, FString& Error);
}
