#include "GizmoFreeDrag.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/SceneComponent.h"
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGizmoFreeTransformsTest, "GizmoFree.Transforms.SelectionAndUndo",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGizmoFreeTransformsTest::RunTest(const FString& Parameters)
{
    // This test creates a disposable map: run in the supplied isolated HostProject.
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    if (!TestNotNull(TEXT("Editor test world"), World)) { return false; }
    auto Spawn = [&](FVector Position)
    {
        AActor* Actor = World->SpawnActor<AActor>();
        Actor->SetFlags(RF_Transactional);
        USceneComponent* Root = NewObject<USceneComponent>(Actor, NAME_None, RF_Transactional);
        Actor->SetRootComponent(Root);
        Root->RegisterComponent();
        Actor->SetActorLocation(Position);
        return Actor;
    };
    AActor* A = Spawn(FVector(-100, 0, 0));
    AActor* B = Spawn(FVector(100, 0, 0));
    FGizmoFreeDragFrame Frame;
    Frame.MoveSensitivity = 1.0;
    Frame.RotationSensitivity = 1.0;
    FGizmoFreeDragSession Drag;
    auto Reset = [&]()
    {
        Drag.End();
        A->SetActorTransform(FTransform(FVector(-100, 0, 0)));
        B->SetActorTransform(FTransform(FVector(100, 0, 0)));
    };

    TestTrue(TEXT("Begin multi-selection"), Drag.Begin({A, B}, EGizmoFreeOperation::PlaneMove, Frame));
    Drag.Update(FVector2D(10, -20));
    Drag.End();
    TestTrue(TEXT("Plane move follows camera plane"), A->GetActorLocation().Equals(FVector(-100, 10, 20)));
    GEditor->UndoTransaction();
    TestTrue(TEXT("Undo restores A"), A->GetActorLocation().Equals(FVector(-100, 0, 0)));
    TestTrue(TEXT("One undo restores B"), B->GetActorLocation().Equals(FVector(100, 0, 0)));
    GEditor->RedoTransaction();
    TestTrue(TEXT("Redo reapplies move"), B->GetActorLocation().Equals(FVector(100, 10, 20)));

    Reset();
    Drag.Begin({A, B}, EGizmoFreeOperation::VerticalMove, Frame);
    Drag.Update(FVector2D(100, -25));
    Drag.End();
    TestTrue(TEXT("Vertical move only changes Z"), A->GetActorLocation().Equals(FVector(-100, 0, 25)));

    Reset();
    Drag.Begin({A, B}, EGizmoFreeOperation::Rotate, Frame);
    Drag.Update(FVector2D(3, 0));
    Drag.Update(FVector2D(90, 150));
    Drag.End();
    TestTrue(TEXT("Yaw locks on first meaningful delta"), A->GetActorLocation().Equals(FVector(0, -100, 0), 0.001));
    TestTrue(TEXT("Rotation uses group centroid"), B->GetActorLocation().Equals(FVector(0, 100, 0), 0.001));

    Reset();
    Drag.Begin({A, B}, EGizmoFreeOperation::Rotate, Frame);
    Drag.Update(FVector2D(0, -90));
    Drag.End();
    TestTrue(TEXT("Pitch uses camera right"), B->GetActorLocation().Equals(FVector(0, 0, -100), 0.001));

    Reset();
    Drag.Begin({A, B}, EGizmoFreeOperation::Roll, Frame);
    Drag.Update(FVector2D(90, 0));
    Drag.End();
    TestTrue(TEXT("Roll uses camera direction"), A->GetActorQuat().Equals(FQuat(FVector::ForwardVector, PI / 2), 0.001));

    Reset();
    Drag.Begin({A, B}, EGizmoFreeOperation::Scale, Frame);
    const double PixelsForDouble = FMath::Loge(2.0) / Frame.ScaleSensitivity;
    Drag.Update(FVector2D(PixelsForDouble / 2, 0));
    Drag.Update(FVector2D(PixelsForDouble, 0));
    Drag.End();
    TestTrue(TEXT("Scale updates do not accumulate drift"), A->GetActorScale3D().Equals(FVector(2), 0.001));
    TestTrue(TEXT("Scale moves actors around centroid"), B->GetActorLocation().Equals(FVector(200, 0, 0), 0.001));

    Reset();
    Drag.Begin({A, B}, EGizmoFreeOperation::PlaneMove, Frame);
    Drag.Update(FVector2D(40, 30));
    Drag.End(true);
    TestTrue(TEXT("Escape restores A"), A->GetActorLocation().Equals(FVector(-100, 0, 0)));
    TestTrue(TEXT("Escape restores B"), B->GetActorLocation().Equals(FVector(100, 0, 0)));

    B->SetLockLocation(true);
    Drag.Begin({A, B}, EGizmoFreeOperation::PlaneMove, Frame);
    Drag.Update(FVector2D(10, 0));
    Drag.End();
    TestTrue(TEXT("Locked actor is excluded"), B->GetActorLocation().Equals(FVector(100, 0, 0)));
    TestFalse(TEXT("Locked-only selection rejected"), Drag.Begin({B}, EGizmoFreeOperation::PlaneMove, Frame));
    TestFalse(TEXT("Empty selection rejected"), Drag.Begin({}, EGizmoFreeOperation::PlaneMove, Frame));
    B->SetLockLocation(false);

    Reset();
    B->AttachToActor(A, FAttachmentTransformRules::KeepWorldTransform);
    Drag.Begin({A, B}, EGizmoFreeOperation::Scale, Frame);
    Drag.Update(FVector2D(PixelsForDouble, 0));
    Drag.End();
    TestTrue(TEXT("Attached selected child is not scaled twice"), B->GetActorScale3D().Equals(FVector(2), 0.001));
    TestTrue(TEXT("Attached selection preserves centroid"), B->GetActorLocation().Equals(FVector(200, 0, 0), 0.001));
    B->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
    World->EditorDestroyActor(B, true);
    World->EditorDestroyActor(A, true);
    return true;
}
#endif
