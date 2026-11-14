#pragma once

#include "CoreMinimal.h"

class AActor;
class FScopedTransaction;

enum class EGizmoFreeOperation : uint8 { PlaneMove, VerticalMove, Rotate, Roll, Scale };

struct FGizmoFreeDragFrame
{
    FVector PixelRight = FVector::RightVector;
    FVector PixelDown = -FVector::UpVector;
    FVector ViewForward = FVector::ForwardVector;
    FVector ViewRight = FVector::RightVector;
    double MoveSensitivity = 0.7;
    double RotationSensitivity = 0.3;
    double ScaleSensitivity = 0.005;
};

// Captures start transforms once; each update is absolute relative to that snapshot.
class FGizmoFreeDragSession
{
public:
    ~FGizmoFreeDragSession();
    bool Begin(const TArray<AActor*>& Selection, EGizmoFreeOperation Operation, const FGizmoFreeDragFrame& Frame);
    void Update(const FVector2D& TotalPixels);
    void End(bool bCancel = false);
    bool IsActive() const { return !Actors.IsEmpty(); }
    const FVector& GetCentroid() const { return Centroid; }

private:
    struct FActorStart { TWeakObjectPtr<AActor> Actor; FTransform Transform; };
    TArray<FActorStart> Actors;
    TUniquePtr<FScopedTransaction> Transaction;
    FVector Centroid = FVector::ZeroVector;
    FGizmoFreeDragFrame Frame;
    EGizmoFreeOperation Operation = EGizmoFreeOperation::PlaneMove;
    int32 RotationAxis = 0;
};
