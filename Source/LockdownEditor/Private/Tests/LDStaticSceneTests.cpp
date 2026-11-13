#include "Art/LDStaticScene.h"
#include "Art/LDStaticSceneEditor.h"
#include "Art/LDArtPlacement.h"
#include "Development/LDDemoContentCatalog.h"
#include "AssetCompilingManager.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "Framework/LDDemoEntry.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Region/LDMapGeometry.h"
#include "Region/LDMapRegistry.h"
#include "Character/LDCharacter.h"
#include "World/LDNativeGround.h"
#include "Vision/LDVisionDirector.h"
#include "Subsystems/EditorActorSubsystem.h"

namespace
{
struct FSceneFixture
{
    UWorld* World = nullptr;
    ULDArtCatalog* Catalog = nullptr;
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    TSharedPtr<const FLDDemoWorldGeometry> Geometry;
    AStaticMeshActor* Tree = nullptr;
    FString Error;
    bool Load()
    {
        World = UEditorLoadingAndSavingUtils::LoadMap(FPackageName::LongPackageNameToFilename(
            LDDemoEntry::WorldMap, FPackageName::GetMapPackageExtension()));
        Catalog = LoadObject<ULDArtCatalog>(nullptr, ULDArtCatalog::AssetPath);
        if (!World || !Catalog || !LDArt::LoadDemoWorld(Content, Geometry, Error)) { return false; }
        FAssetCompilingManager::Get().FinishAllCompilation();
        const auto* Cylinder = Geometry->Blockers.FindByPredicate([](const FLDDemoWorldBlocker& B) { return B.Shape == ELDDemoWorldBlockerShape::Cylinder; });
        for (TActorIterator<AStaticMeshActor> It(World); Cylinder && It; ++It)
        { if (It->ActorHasTag(FName(*Cylinder->Id))) { Tree = *It; break; } }
        return Tree != nullptr;
    }
    ~FSceneFixture() { UEditorLoadingAndSavingUtils::NewBlankMap(false); }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDStaticScenePersistence, "Lockdown.Rules.StaticScene.PersistenceAndBinding",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDStaticScenePersistence::RunTest(const FString& Parameters)
{
    FSceneFixture F;
    if (!TestTrue(TEXT("Saved formal scene and existing tree load: ") + F.Error, F.Load())) { return false; }
    auto* Registry = F.World->GetSubsystem<ULDMapRegistry>();
    if (!TestTrue(TEXT("Current saved scene binds without a synchronized decoration index: ") + F.Error,
        Registry->Rebuild(F.Error))) { return false; }
    const auto Parts = LDStaticScene::Visuals(*F.World);
    TestTrue(TEXT("Saved standard meshes supply visuals directly"), Parts.Num() > 0);
    for (const auto* Part : Parts)
    {
        if (!TestTrue(TEXT("Visible editable mesh has no extra collision/navigation"), Part->IsVisible()
            && Part->GetCollisionEnabled() == ECollisionEnabled::NoCollision && !Part->CanEverAffectNavigation())) { return false; }
    }
    AActor* Decoration = nullptr;
    for (const auto* Part : Parts)
    {
        if (!Part->GetOwner()->ActorHasTag(LDDemoWorld::StaticGeometryTag)) { Decoration = Part->GetOwner(); break; }
    }
    if (!TestNotNull(TEXT("An editable decoration exists"), Decoration)) { return false; }
    Decoration->Destroy();
    TestTrue(TEXT("Deleting a decoration plays with the untouched catalog"), Registry->Rebuild(F.Error));
    auto* Shape = LDStaticScene::Collision(*F.Tree);
    if (!TestTrue(TEXT("Tree carries its actual hidden collision instance"), Shape != F.Tree->GetStaticMeshComponent()
        && Shape->CreationMethod == EComponentCreationMethod::Instance && !Shape->IsVisible())) { return false; }
    FCollisionQueryParams Query;
    for (TActorIterator<AActor> It(F.World); It; ++It) { if (*It != F.Tree) { Query.AddIgnoredActor(*It); } }
    auto Hit = [&](const FVector& Centre)
    {
        FHitResult Result;
        return F.World->LineTraceSingleByChannel(Result, Centre - FVector(150, 0, 0), Centre + FVector(150, 0, 0), ECC_Pawn, Query)
            && Result.GetComponent() == Shape;
    };
    const FString TreeName = F.Tree->GetName();
    const FVector Before = Shape->Bounds.Origin;
    TestTrue(TEXT("Original collision is queryable"), Hit(Before));
    const FVector Delta(600, 200, 0);
    F.Tree->SetActorLocation(F.Tree->GetActorLocation() + Delta);
    TestTrue(TEXT("Collision follows an ordinary actor move"), Shape->Bounds.Origin.Equals(Before + Delta, .01));
    TestFalse(TEXT("Old tree position is clear"), Hit(Before));
    TestTrue(TEXT("New tree position blocks"), Hit(Before + Delta));
    TestFalse(TEXT("Unsynchronized layout explicitly refuses stale geometry"), LDStaticScene::ValidateGeometry(*F.World, *F.Geometry, F.Error));
    FLDDemoWorldGeometry Current;
    TestTrue(TEXT("The same moved tree is admitted by runtime projection"), LDStaticScene::ProjectRuntimeGeometry(*F.World, *F.Geometry, Current, F.Error));
    TestTrue(TEXT("A moved tree binds without export"), Registry->Rebuild(F.Error));
    const auto Revision = Registry->Revision();
    auto* Added = F.World->SpawnActor<AStaticMeshActor>();
    auto* AddedMesh = Added->GetStaticMeshComponent();
    AddedMesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
    AddedMesh->SetCollisionProfileName(TEXT("BlockAll"));
    AddedMesh->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);
    AddedMesh->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Block);
    Added->SetActorLocation(FVector(Before.X, Before.Y, 200));
    Added->SetActorScale3D(FVector(1, 1, 4));
    TestTrue(TEXT("An untagged native collision mesh binds"), Registry->Rebuild(F.Error));
    TestTrue(TEXT("Native collision edits invalidate the world revision"), Registry->Revision() != Revision);
    auto* Overhead = NewObject<UStaticMeshComponent>(Added);
    Added->AddInstanceComponent(Overhead);
    Overhead->SetStaticMesh(AddedMesh->GetStaticMesh());
    Overhead->SetCollisionProfileName(TEXT("BlockAll"));
    Overhead->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Ignore);
    Overhead->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Ignore);
    Overhead->RegisterComponent();
    Overhead->SetWorldLocation(FVector(Before.X, Before.Y, 450));
    Overhead->SetWorldScale3D(FVector(.5, .5, 1));
    FLDLegalGround Standing;
    ELDWorldQueryFailure Failure;
    FCollisionShape ApprovedCapsule;
    if (!TestTrue(TEXT("Approved body shape"), ULDMapRegistry::ContentCapsule(*F.Content, ApprovedCapsule, F.Error))) { return false; }
    TestFalse(TEXT("A floor does not hide another collision component of the same actor"), Registry->QueryGround(
        Registry->Revision(), FVector(Before.X, Before.Y, 400), ApprovedCapsule,
        FName(*Registry->GetGeometry()->Surfaces[0].Id), Standing, Failure, F.Error));
    TestEqual(TEXT("The sibling obstruction is reported as blocked"), Failure, ELDWorldQueryFailure::Blocked);
    Overhead->DestroyComponent();
    FLDSightGeometry Sight;
    TestTrue(TEXT("Native sight accepts the current scene"), Sight.Build(*Registry->GetGeometry(), Registry->Revision(),
        F.Content->Get().Parameters.VisionRadiusCm, F.Error, F.World));
    const FVector Left(Before.X - 60, Before.Y, 0), Right(Before.X + 60, Before.Y, 0);
    TestEqual(TEXT("New mesh blocks sight at its actual position"), Sight.Trace(Left, Right), ELDSightResult::Blocked);
    Added->Destroy();
    TestTrue(TEXT("Deleting the new mesh rebuilds successfully"), Registry->Rebuild(F.Error));
    Sight.Build(*Registry->GetGeometry(), Registry->Revision(), F.Content->Get().Parameters.VisionRadiusCm, F.Error, F.World);
    TestEqual(TEXT("Deleted collision leaves no stale sight blocker"), Sight.Trace(Left, Right), ELDSightResult::Visible);
    const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Artifacts/StaticSceneTest/"));
    IFileManager::Get().MakeDirectory(*Directory, true);
    FPackageName::RegisterMountPoint(TEXT("/LDStaticSceneTest/"), Directory);
    ON_SCOPE_EXIT { UEditorLoadingAndSavingUtils::NewBlankMap(false); FPackageName::UnRegisterMountPoint(TEXT("/LDStaticSceneTest/"), Directory); };
    if (!TestTrue(TEXT("Edited scene saves as an isolated fixture"), UEditorLoadingAndSavingUtils::SaveMap(F.World, TEXT("/LDStaticSceneTest/Moved")))) { return false; }
    F.World = UEditorLoadingAndSavingUtils::LoadMap(Directory / TEXT("Moved.umap"));
    if (!TestNotNull(TEXT("Edited scene reloads from disk"), F.World)) { return false; }
    F.Tree = nullptr;
    for (TActorIterator<AStaticMeshActor> It(F.World); It; ++It) { if (It->GetName() == TreeName) { F.Tree = *It; break; } }
    if (!TestNotNull(TEXT("Tree survives reopen"), F.Tree)) { return false; }
    Shape = LDStaticScene::Collision(*F.Tree);
    TestTrue(TEXT("Collision and root relationship persist"), Shape != F.Tree->GetStaticMeshComponent()
        && Shape->Bounds.Origin.Equals(Before + Delta, .01));
    TestEqual(TEXT("Visual NoCollision profile persists"), F.Tree->GetStaticMeshComponent()->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
    Query.ClearIgnoredSourceObjects();
    for (TActorIterator<AActor> It(F.World); It; ++It) { if (*It != F.Tree) { Query.AddIgnoredActor(*It); } }
    TestTrue(TEXT("Reloaded saved collision blocks at the edited position"), Hit(Before + Delta));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDStaticSceneCopy, "Lockdown.Rules.StaticScene.CopyIdentityAndSafety",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDStaticSceneCopy::RunTest(const FString& Parameters)
{
    FSceneFixture F;
    if (!TestTrue(TEXT("Saved formal tree loads: ") + F.Error, F.Load())) { return false; }
    FLDDemoWorldGeometry Baseline;
    if (!TestTrue(TEXT("Current editable scene projects"), LDStaticScene::ProjectRuntimeGeometry(*F.World, *F.Geometry, Baseline, F.Error))) { return false; }
    auto* System = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
    auto* Copy = Cast<AStaticMeshActor>(System->DuplicateActor(F.Tree, F.World, FVector(600, 200, 0)));
    if (!TestNotNull(TEXT("Standard editor duplication creates a tree"), Copy)) { return false; }
    TestTrue(TEXT("Copied tree includes its collision"), LDStaticScene::Collision(*Copy) != Copy->GetStaticMeshComponent());
    FLDDemoWorldGeometry Projected;
    TestFalse(TEXT("Explicit source export requires unique identities"), LDStaticScene::ProjectGeometry(*F.World, *F.Geometry, Projected, F.Error));
    TestTrue(TEXT("Play derives stable identities for an ordinary duplicate"), LDStaticScene::ProjectRuntimeGeometry(*F.World, *F.Geometry, Projected, F.Error));
    TestEqual(TEXT("Play retains the copied blocker"), Projected.Blockers.Num(), Baseline.Blockers.Num() + 1);
    TestTrue(TEXT("Synchronization assigns a fresh copy identity: ") + F.Error, LDStaticSceneEditor::NormalizeCopies(*F.World, *F.Geometry, F.Error));
    TestTrue(TEXT("Both copied collision bodies enter the scene projection: ") + F.Error, LDStaticScene::ProjectGeometry(*F.World, *F.Geometry, Projected, F.Error));
    TestEqual(TEXT("The extra tree is retained, not ignored"), Projected.Blockers.Num(), Baseline.Blockers.Num() + 1);
    TSet<FString> Ids;
    for (const auto& B : Projected.Blockers) { Ids.Add(B.Id); }
    TestEqual(TEXT("Every blocker identity is unique"), Ids.Num(), Projected.Blockers.Num());
    Copy->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
    TArray<FLDArtDecoration> Rows; TArray<FString> Hidden;
    TestFalse(TEXT("Accidental canopy collision is explicitly rejected"), LDStaticScene::Read(*F.World, *F.Catalog, Rows, Hidden, F.Error));
    Copy->SetActorRotation(FRotator(24, 41, 17));
    Copy->SetActorScale3D(FVector(-.7, 1.3, .4));
    TestTrue(TEXT("Rotated, mirrored and nonuniform meshes remain playable"), LDStaticScene::ProjectRuntimeGeometry(*F.World, *F.Geometry, Projected, F.Error));
    // Rebuilding after a terrain move uses native support and moves runtime anchors, without changing editor markers.
    auto* Registry = F.World->GetSubsystem<ULDMapRegistry>();
    Copy->Destroy();
    if (!TestTrue(TEXT("Native scene binds after deleting the copied collision"), Registry->Rebuild(F.Error))) { return false; }
    for (const auto& Ramp : F.Geometry->Ramps)
    {
        FVector Centre = FVector::ZeroVector;
        for (const auto& Corner : Ramp.PolygonCm)
        { FVector Point; if (!Corner.ToVector(Point, F.Error)) { return false; } Centre += Point; }
        Centre /= Ramp.PolygonCm.Num();
        TestEqual(TEXT("Native ramps retain the approved half-level sight tier"), Registry->GetTerrain().Tier(FVector2D(Centre)), 2 * Ramp.LowerLevel + 1);
    }
    AStaticMeshActor* Floor = nullptr;
    for (TActorIterator<AStaticMeshActor> It(F.World); It; ++It)
    { if (It->ActorHasTag(TEXT("demo_ground"))) { Floor = *It; break; } }
    if (!TestNotNull(TEXT("Editable native ground"), Floor)) { return false; }
    Floor->SetActorLocation(Floor->GetActorLocation() + FVector(0, 0, 10));
    TestTrue(TEXT("Ground height edits bind from actual collision"), Registry->Rebuild(F.Error));
    Floor->Destroy();
    TestFalse(TEXT("Missing ground under required points still fails clearly"), Registry->Rebuild(F.Error));
    TestTrue(TEXT("Failure identifies the ground problem"), F.Error.Contains(TEXT("NotGrounded")));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDStaticSceneReplacementFloor, "Lockdown.Rules.StaticScene.ReplacementFloorAndOverlay",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDStaticSceneReplacementFloor::RunTest(const FString& Parameters)
{
    FSceneFixture F;
    if (!TestTrue(TEXT("Saved editable scene loads: ") + F.Error, F.Load())) { return false; }
    auto* Registry = F.World->GetSubsystem<ULDMapRegistry>();
    AStaticMeshActor* Original = nullptr;
    for (TActorIterator<AStaticMeshActor> It(F.World); It; ++It)
    { if (It->ActorHasTag(TEXT("demo_ground"))) { Original = *It; break; } }
    if (!TestNotNull(TEXT("Original floor exists"), Original)) { return false; }
    const FBox Bounds = Original->GetComponentsBoundingBox(true);
    auto* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    const auto Floor = [&](const FVector& Centre, const FVector& Size)
    {
        auto* Actor = F.World->SpawnActor<AStaticMeshActor>(Centre, FRotator::ZeroRotator);
        auto* Part = Actor->GetStaticMeshComponent();
        Part->SetStaticMesh(Cube);
        Actor->SetActorScale3D(Size / 100.);
        Part->SetCollisionProfileName(TEXT("BlockAll"));
        Part->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Ignore);
        return Actor;
    };
    // New meshes have no project tags and deliberately do not respond to the old custom ground channel.
    const FVector Size = Bounds.GetSize(), Centre = Bounds.GetCenter();
    auto* Left = Floor(Centre - FVector(Size.X * .25,0,0), FVector(Size.X * .5,Size.Y,Size.Z));
    auto* Right = Floor(Centre + FVector(Size.X * .25,0,0), FVector(Size.X * .5,Size.Y,Size.Z));
    Original->Destroy();
    TestTrue(TEXT("Deleting the named floor and replacing it with two ordinary collision meshes plays: ") + F.Error, Registry->Rebuild(F.Error));
    FHitResult Hit;
    TestTrue(TEXT("Pointer/terrain ray finds ordinary replacement collision"), Registry->TraceGround(
        FVector(Centre.X,Centre.Y,1000),FVector(Centre.X,Centre.Y,-1000),Hit));
    const double Top = Bounds.Max.Z;
    auto* Overlay = Floor(FVector(Centre.X,Centre.Y,Top+2.5),FVector(Size.X,Size.Y,5));
    TestTrue(TEXT("A thin collision overlay updates standing heights: ") + F.Error, Registry->Rebuild(F.Error));
    TestTrue(TEXT("Overlay is the native support on flat terrain"), Registry->TraceGround(
        FVector(Centre.X,Centre.Y,1000),FVector(Centre.X,Centre.Y,-1000),Hit) && Hit.GetActor()==Overlay);
    const auto* Spawn = Registry->Resolve(TEXT("rest_spawn_0"),Registry->Generation);
    if (TestNotNull(TEXT("Spawn remains bound"), Spawn))
    { TestEqual(TEXT("Spawn height follows overlay"), Spawn->Position.Z, Top+5); }
    auto* Body = F.World->SpawnActor<ALDCharacter>(FVector(Centre.X,Centre.Y,Top+95),FRotator::ZeroRotator);
    TestTrue(TEXT("Pawn bodies do not become ground"), Registry->TraceGround(FVector(Centre.X,Centre.Y,1000),FVector(Centre.X,Centre.Y,-1000),Hit)
        && Hit.GetActor()==Overlay);
    Body->Destroy();
    auto* VisualOnly = Floor(FVector(Centre.X,Centre.Y,Top+25),FVector(Size.X,Size.Y,5));
    VisualOnly->GetStaticMeshComponent()->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore);
    VisualOnly->GetStaticMeshComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel1,ECR_Block);
    TestTrue(TEXT("A surface that does not block characters does not prevent Play: ") + F.Error, Registry->Rebuild(F.Error));
    Overlay->Destroy(); Left->Destroy(); Right->Destroy(); VisualOnly->Destroy();
    TestFalse(TEXT("Removing actual support still refuses unsafe spawn"), Registry->Rebuild(F.Error));
    return !HasAnyErrors();
}
