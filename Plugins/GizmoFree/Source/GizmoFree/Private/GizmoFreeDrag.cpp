#include "GizmoFreeDrag.h"

#include "Components/SceneComponent.h"
#include "Editor.h"
#include "GameFramework/Actor.h"
#include "ScopedTransaction.h"

FGizmoFreeDragSession::~FGizmoFreeDragSession() { End(); }

bool FGizmoFreeDragSession::Begin(const TArray<AActor*>& Selection, EGizmoFreeOperation InOperation, const FGizmoFreeDragFrame& InFrame)
{
    End();
    TSet<AActor*> Eligible;
    Centroid = FVector::ZeroVector;
    for (AActor* Actor : Selection)
    {
        if (IsValid(Actor) && Actor->GetRootComponent() && Actor->IsEditable() && !Actor->IsLockLocation() && !Eligible.Contains(Actor))
        {
            Eligible.Add(Actor);
            Centroid += Actor->GetActorLocation();
        }
    }
    if (Eligible.IsEmpty()) { return false; }
    Centroid /= Eligible.Num();
    for (AActor* Actor : Eligible)
    {
        bool bHasSelectedParent = false;
        for (AActor* Parent = Actor->GetAttachParentActor(); Parent; Parent = Parent->GetAttachParentActor())
        {
            if (Eligible.Contains(Parent)) { bHasSelectedParent = true; break; }
        }
        // Children inherit their parent's transform; do not apply the delta twice.
        if (!bHasSelectedParent) { Actors.Add({Actor, Actor->GetActorTransform()}); }
    }
    Operation = InOperation;
    Frame = InFrame;
    RotationAxis = 0;
    return IsActive();
}

void FGizmoFreeDragSession::Update(const FVector2D& TotalPixels)
{
    if (!IsActive()) { return; }
    FVector Translation = FVector::ZeroVector;
    FQuat Rotation = FQuat::Identity;
    double Scale = 1.0;
    switch (Operation)
    {
    case EGizmoFreeOperation::PlaneMove:
        Translation = (Frame.PixelRight * TotalPixels.X + Frame.PixelDown * TotalPixels.Y) * Frame.MoveSensitivity;
        break;
    case EGizmoFreeOperation::VerticalMove:
        Translation = FVector::UpVector * -TotalPixels.Y * Frame.PixelDown.Length() * Frame.MoveSensitivity;
        break;
    case EGizmoFreeOperation::Rotate:
        if (!RotationAxis && TotalPixels.SizeSquared() >= 4.0)
        {
            RotationAxis = FMath::Abs(TotalPixels.X) >= FMath::Abs(TotalPixels.Y) ? 1 : 2;
        }
        if (!RotationAxis) { return; }
        Rotation = FQuat(RotationAxis == 1 ? FVector::UpVector : Frame.ViewRight,
            FMath::DegreesToRadians((RotationAxis == 1 ? TotalPixels.X : -TotalPixels.Y) * Frame.RotationSensitivity));
        break;
    case EGizmoFreeOperation::Roll:
        Rotation = FQuat(Frame.ViewForward, FMath::DegreesToRadians(TotalPixels.X * Frame.RotationSensitivity));
        break;
    case EGizmoFreeOperation::Scale:
        Scale = FMath::Exp(FMath::Clamp((TotalPixels.X - TotalPixels.Y) * Frame.ScaleSensitivity, -6.0, 6.0));
        break;
    }
    if (!Transaction)
    {
        if (Translation.IsNearlyZero() && Rotation.IsIdentity() && FMath::IsNearlyEqual(Scale, 1.0)) { return; }
        if (!GEditor || GEditor->IsTransactionActive()) { return; }
        Transaction = MakeUnique<FScopedTransaction>(NSLOCTEXT("GizmoFree", "Transform", "GizmoFree Alt-drag actors"));
        for (const FActorStart& Start : Actors)
        {
            if (AActor* Actor = Start.Actor.Get())
            {
                Actor->Modify();
                Actor->GetRootComponent()->Modify();
            }
        }
    }
    for (const FActorStart& Start : Actors)
    {
        if (AActor* Actor = Start.Actor.Get())
        {
            FTransform Transform = Start.Transform;
            Transform.SetLocation(Centroid + Rotation.RotateVector((Start.Transform.GetLocation() - Centroid) * Scale) + Translation);
            Transform.SetRotation((Rotation * Start.Transform.GetRotation()).GetNormalized());
            Transform.SetScale3D(Start.Transform.GetScale3D() * Scale);
            Actor->SetActorTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
            Actor->PostEditMove(false);
        }
    }
    if (GEditor) { GEditor->RedrawLevelEditingViewports(false); }
}

void FGizmoFreeDragSession::End(bool bCancel)
{
    if (Transaction)
    {
        for (const FActorStart& Start : Actors)
        {
            if (AActor* Actor = Start.Actor.Get())
            {
                if (bCancel) { Actor->SetActorTransform(Start.Transform, false, nullptr, ETeleportType::TeleportPhysics); }
                Actor->PostEditMove(true);
            }
        }
        if (bCancel) { Transaction->Cancel(); }
        Transaction.Reset();
        if (GEditor) { GEditor->RedrawLevelEditingViewports(); }
    }
    Actors.Reset();
}
