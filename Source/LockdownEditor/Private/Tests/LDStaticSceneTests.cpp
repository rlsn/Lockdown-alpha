#include "Art/LDStaticScene.h"
#include "Art/LDStaticSceneEditor.h"
#include "LDSceneAuthoringLibrary.h"
#include "Art/LDArtPlacement.h"
#include "Development/LDDemoContentCatalog.h"
#include "AssetCompilingManager.h"
#include "Components/StaticMeshComponent.h"
#include "PhysicsEngine/BodyInstance.h"
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
#include "World/LDSpawnPlacement.h"
#include "Components/BoxComponent.h"
#include "World/LDLayoutPoint.h"
#include "Character/LDHumanoidVisualComponent.h"
#include "Vision/LDVisionDirector.h"
#include "Region/LDMapDefinition.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "UObject/StrongObjectPtr.h"

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
    const auto* Definition = LoadObject<ULDMapDefinition>(nullptr, LDDemoEntry::WorldDefinition);
    if (!TestNotNull(TEXT("Saved world definition is present"), Definition)) { return false; }
    FLDDemoWorldGeometry AuthoringBase;
    TestTrue(TEXT("Authoring reads the saved world without current content inputs: ") + F.Error,
        Definition->ReadSavedForEditing(AuthoringBase, F.Error));
    TestTrue(TEXT("Authoring base is the same complete typed geometry"),
        FLDDemoWorldGeometry::StaticStruct()->CompareScriptStruct(&AuthoringBase, F.Geometry.Get(), PPF_None));
    TStrongObjectPtr<ULDMapDefinition> Damaged(DuplicateObject<ULDMapDefinition>(Definition, GetTransientPackage()));
    Damaged->Checksum = TEXT("invalid");
    TestFalse(TEXT("Authoring rejects an altered world checksum"), Damaged->ReadSavedForEditing(AuthoringBase, F.Error));
    Damaged->Checksum = Definition->Checksum;
    Damaged->SourceJson += TEXT(" ");
    TestFalse(TEXT("Authoring rejects altered saved world bytes"), Damaged->ReadSavedForEditing(AuthoringBase, F.Error));
    FLDDemoWorldGeometry SeedWithoutFinalAnchors = *F.Geometry;
    SeedWithoutFinalAnchors.Anchors.RemoveAll([](const FLDDemoWorldAnchor& Anchor)
    {
        return Anchor.Kind == ELDDemoWorldAnchorKind::FinalSpawn || Anchor.Kind == ELDDemoWorldAnchorKind::FinalCapture;
    });
    FLDDemoWorldGeometry Projected;
    TestTrue(TEXT("Saved final anchor tags extend a legacy world projection: ") + F.Error,
        LDStaticScene::ProjectRuntimeGeometry(*F.World, SeedWithoutFinalAnchors, Projected, F.Error));
    TestEqual(TEXT("All six final spawn anchors and capture are exported"),
        Projected.Anchors.FilterByPredicate([](const FLDDemoWorldAnchor& Anchor)
        {
            return Anchor.Kind == ELDDemoWorldAnchorKind::FinalSpawn || Anchor.Kind == ELDDemoWorldAnchorKind::FinalCapture;
        }).Num(), LDDemoWorld::FinalSpawns + 1);
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
    int32 FinalWalls = 0;
    for (TActorIterator<AStaticMeshActor> It(F.World); It; ++It)
    {
        if (!It->ActorHasTag(LDDemoWorld::StaticGeometryTag) || It->Tags.IsEmpty()
            || !It->Tags[0].ToString().StartsWith(TEXT("final_arena_"))) { continue; }
        ++FinalWalls;
        const auto* Collision = LDStaticScene::Collision(**It);
        FCollisionQueryParams OnlyWall;
        for (TActorIterator<AActor> Other(F.World); Other; ++Other)
        { if (*Other != *It) { OnlyWall.AddIgnoredActor(*Other); } }
        const FVector Centre = Collision->Bounds.Origin;
        const FVector HalfRay(Collision->Bounds.BoxExtent.X + 100, 0, 0);
        for (const auto Channel : { ECC_Pawn, ECC_GameTraceChannel2 })
        {
            FHitResult Result;
            TestTrue(TEXT("Saved final wall physically blocks movement and sight: ") + It->GetActorLabel(),
                F.World->LineTraceSingleByChannel(Result, Centre - HalfRay, Centre + HalfRay, Channel, OnlyWall)
                && Result.GetComponent() == Collision);
        }
    }
    TestTrue(TEXT("Final arena contains saved hard collision walls"), FinalWalls > 0);
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
    double TestGroundZ = 0; FVector TestGroundNormal;
    Registry->GetTerrain().Ground(FVector2D(Before), TestGroundZ, TestGroundNormal);
    if (!TestTrue(TEXT("The sight fixture has finite actual support"), FMath::IsFinite(TestGroundZ))) { return false; }
    auto* Added = F.World->SpawnActor<AStaticMeshActor>();
    auto* AddedMesh = Added->GetStaticMeshComponent();
    AddedMesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
    AddedMesh->SetCollisionProfileName(TEXT("BlockAll"));
    AddedMesh->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);
    AddedMesh->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Block);
    Added->SetActorLocation(FVector(Before.X, Before.Y, TestGroundZ + 200));
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
    Overhead->SetWorldLocation(FVector(Before.X, Before.Y, TestGroundZ + 450));
    Overhead->SetWorldScale3D(FVector(.5, .5, 1));
    FLDLegalGround Standing;
    ELDWorldQueryFailure Failure;
    FCollisionShape ApprovedCapsule;
    if (!TestTrue(TEXT("Approved body shape"), ULDMapRegistry::ContentCapsule(*F.Content, ApprovedCapsule, F.Error))) { return false; }
    TestFalse(TEXT("A floor does not hide another collision component of the same actor"), Registry->QueryGround(
        Registry->Revision(), FVector(Before.X, Before.Y, TestGroundZ + 400), ApprovedCapsule,
        FName(*Registry->GetGeometry()->Surfaces[0].Id), Standing, Failure, F.Error));
    TestEqual(TEXT("The sibling obstruction is reported as blocked"), Failure, ELDWorldQueryFailure::Blocked);
    Overhead->DestroyComponent();
    FLDSightGeometry Sight;
    TestTrue(TEXT("Native sight accepts the current scene"), Sight.Build(*Registry->GetGeometry(), Registry->Revision(),
        F.Content->Get().Parameters.VisionRadiusCm, F.Error, F.World));
    const FVector Left(Before.X - 60, Before.Y, TestGroundZ), Right(Before.X + 60, Before.Y, TestGroundZ);
    TestEqual(TEXT("New mesh blocks sight at its actual position"), Sight.Trace(Left, Right), ELDSightResult::Blocked);
    AddedMesh->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Ignore);
    TestTrue(TEXT("Sight channel edit binds"), Registry->Rebuild(F.Error));
    Sight.Build(*Registry->GetGeometry(), Registry->Revision(), F.Content->Get().Parameters.VisionRadiusCm, F.Error, F.World);
    TestEqual(TEXT("Ground-aim collision alone does not hide units"), Sight.Trace(Left, Right), ELDSightResult::Visible);
    Added->Destroy();
    TestTrue(TEXT("Deleting the new mesh rebuilds successfully"), Registry->Rebuild(F.Error));
    Sight.Build(*Registry->GetGeometry(), Registry->Revision(), F.Content->Get().Parameters.VisionRadiusCm, F.Error, F.World);
    TestEqual(TEXT("Deleted collision leaves no stale sight blocker"), Sight.Trace(Left, Right), ELDSightResult::Visible);
    auto* Generic = F.World->SpawnActor<AActor>();
    auto* Box = NewObject<UBoxComponent>(Generic);
    Generic->AddInstanceComponent(Box);
    Generic->SetRootComponent(Box);
    Box->SetBoxExtent(FVector(20,20,20));
    Box->SetCollisionProfileName(TEXT("BlockAll"));
    Box->RegisterComponent();
    Generic->SetActorLocation(FVector(Before.X, Before.Y, 300));
    TestTrue(TEXT("A generic collision actor binds"), Registry->Rebuild(F.Error));
    const auto BoxRevision = Registry->Revision();
    Generic->SetActorLocation(Generic->GetActorLocation() + FVector(100,0,0));
    TestTrue(TEXT("A moved generic collision actor binds"), Registry->Rebuild(F.Error));
    TestTrue(TEXT("Generic collision transforms invalidate the world"), Registry->Revision() != BoxRevision);
    const auto MovedBoxRevision = Registry->Revision();
    Box->SetBoxExtent(FVector(40,20,20));
    TestTrue(TEXT("A resized generic collision actor binds"), Registry->Rebuild(F.Error));
    TestTrue(TEXT("Generic collision shapes invalidate the world"), Registry->Revision() != MovedBoxRevision);
    Generic->Destroy();
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
    TestTrue(TEXT("Both copied collision bodies enter the scene projection: ") + F.Error, LDStaticScene::ProjectRuntimeGeometry(*F.World, *F.Geometry, Projected, F.Error));
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
    const auto* Rest = Registry->Resolve(TEXT("rest_spawn_0"), Registry->Generation);
    FHitResult Support;
    if (!TestNotNull(TEXT("Required rest spawn is bound"), Rest)
        || !TestTrue(TEXT("Required spawn has actual support"), Registry->TraceGround(
            Rest->Position + FVector(0,0,100), Rest->Position - FVector(0,0,100), Support))) { return false; }
    auto* Floor = Cast<AStaticMeshActor>(Support.GetActor());
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
    if (!TestTrue(TEXT("Authored scene binds: ") + F.Error, Registry->Rebuild(F.Error))) { return false; }
    const auto* Rest = Registry->Resolve(TEXT("rest_spawn_0"), Registry->Generation);
    FHitResult Support;
    if (!TestNotNull(TEXT("Required rest spawn is bound"), Rest)
        || !TestTrue(TEXT("Required spawn has actual support"), Registry->TraceGround(
            Rest->Position + FVector(0,0,100), Rest->Position - FVector(0,0,100), Support))) { return false; }
    auto* Original = Cast<AStaticMeshActor>(Support.GetActor());
    if (!TestNotNull(TEXT("Editable supporting mesh exists"), Original)) { return false; }
    const FBox Bounds = Support.GetComponent()->GetBodyInstance()->GetBodyBounds();
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
    const FVector Size(Bounds.GetSize().X, Bounds.GetSize().Y, FMath::Max(10., Bounds.GetSize().Z));
    const FVector Centre(Bounds.GetCenter().X, Bounds.GetCenter().Y, Bounds.Max.Z - Size.Z * .5);
    auto* Left = Floor(Centre - FVector(Size.X * .25,0,0), FVector(Size.X * .5,Size.Y,Size.Z));
    auto* Right = Floor(Centre + FVector(Size.X * .25,0,0), FVector(Size.X * .5,Size.Y,Size.Z));
    Original->Destroy();
    TestTrue(TEXT("Deleting the supporting floor and replacing it with two ordinary collision meshes plays: ") + F.Error, Registry->Rebuild(F.Error));
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
    FCollisionShape Capsule;
    FLDLegalGround Standing;
    ELDWorldQueryFailure Failure;
    const auto* RestSpawn = Registry->Resolve(TEXT("rest_spawn_0"), Registry->Generation);
    if (!TestNotNull(TEXT("Rest spawn remains bound under a visual overlay"), RestSpawn)
        || !TestTrue(TEXT("Approved body capsule"), ULDMapRegistry::ContentCapsule(*F.Content, Capsule, F.Error))
        || !TestTrue(TEXT("Rest spawn ground remains legal"), Registry->QueryGround(Registry->Revision(), RestSpawn->Position,
            Capsule, RestSpawn->Surface, Standing, Failure, F.Error))) { return false; }
    TestFalse(TEXT("The same non-Pawn-blocking overlay also permits actual body placement"),
        LDSpawnPlacement::OverlapsStatic(*F.World, LDSpawnPlacement::StandingOrigin(Standing), Capsule));
    Overlay->Destroy(); Left->Destroy(); Right->Destroy(); VisualOnly->Destroy();
    TestFalse(TEXT("Removing actual support still refuses unsafe spawn"), Registry->Rebuild(F.Error));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDTrainingPointCopies, "Lockdown.Rules.StaticScene.SharedTrainingCopies",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDTrainingPointCopies::RunTest(const FString& Parameters)
{
    auto* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
    ON_SCOPE_EXIT { UEditorLoadingAndSavingUtils::NewBlankMap(false); };
    auto* Floor = World->SpawnActor<AStaticMeshActor>();
    Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
    Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
    Floor->SetActorScale3D(FVector(100,100,1));
    Floor->SetActorLocation(FVector(0,0,-50));
    auto* Original = World->SpawnActor<ALDLayoutPoint>();
    Original->Tags.Add(TEXT("LDSceneAnchor:training_dummy_0"));
    auto* Editor = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
    auto* Copy = Cast<ALDLayoutPoint>(Editor->DuplicateActor(Original, World, FVector(400,0,0)));
    if (!TestNotNull(TEXT("A normal editor duplicate retains the layout actor"), Copy)) { return false; }
    TestTrue(TEXT("Copy becomes an additional attackable shared target"), Copy->bSharedTrainingDummy);
    TestFalse(TEXT("The original roster target keeps its role"), Original->bSharedTrainingDummy);
    auto* Second = Cast<ALDLayoutPoint>(Editor->DuplicateActor(Copy, World, FVector(400,0,0)));
    if (!TestNotNull(TEXT("Shared targets can themselves be duplicated"), Second)) { return false; }
    FLDDemoWorldGeometry Base, Projected, Again;
    auto& Surface = Base.Surfaces.AddDefaulted_GetRef();
    Surface.Id = TEXT("demo_ground");
    auto& Slot = Base.Anchors.AddDefaulted_GetRef();
    Slot.Id = TEXT("training_dummy_0"); Slot.Kind = ELDDemoWorldAnchorKind::TrainingDummy; Slot.SurfaceId = Surface.Id;
    FString Error;
    if (!TestTrue(TEXT("Copies project without a duplicate-slot rejection: ") + Error,
        LDStaticScene::ProjectRuntimeGeometry(*World, Base, Projected, Error))) { return false; }
    TestEqual(TEXT("Original and both additional targets are retained"), Projected.Anchors.Num(), 3);
    TestTrue(TEXT("The saved actor name gives each additional target a distinct identity"), Copy->SharedTrainingId() != Second->SharedTrainingId());
    TestTrue(TEXT("Projection is stable without modifying the editor"), LDStaticScene::ProjectRuntimeGeometry(*World, Base, Again, Error)
        && FLDDemoWorldGeometry::StaticStruct()->CompareScriptStruct(&Projected, &Again, PPF_None));
    TArray<USceneComponent*> Children;
    Original->BodyPreview->GetChildrenComponents(true, Children);
    for (const auto* Child : Children)
    { TestTrue(TEXT("Even an empty body preview has only editor-only children: ") + Child->GetName(), Child->IsEditorOnly()); }
    Copy->Destroy();
    TestTrue(TEXT("Deleting a shared target leaves the original and the other target"),
        LDStaticScene::ProjectRuntimeGeometry(*World, Base, Projected, Error) && Projected.Anchors.Num() == 2);
    auto* Conflict = World->SpawnActor<ALDLayoutPoint>();
    Conflict->Tags.Add(TEXT("LDSceneAnchor:training_dummy_0"));
    TestFalse(TEXT("A real conflicting reserved slot still reports an error"), LDStaticScene::ProjectRuntimeGeometry(*World, Base, Projected, Error));
    TestTrue(TEXT("The diagnostic names the conflicting slot"), Error.Contains(TEXT("training_dummy_0")));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDNativeSightEdits, "Lockdown.Rules.StaticScene.NativeSightEdits",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDNativeSightEdits::RunTest(const FString& Parameters)
{
    auto* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
    ON_SCOPE_EXIT { UEditorLoadingAndSavingUtils::NewBlankMap(false); };
    auto* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    const auto Place = [&](const FVector& Position, const FVector& Scale)
    {
        auto* Actor = World->SpawnActor<AStaticMeshActor>();
        Actor->GetStaticMeshComponent()->SetStaticMesh(Cube);
        Actor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
        Actor->GetStaticMeshComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Ignore);
        Actor->SetActorScale3D(Scale); Actor->SetActorLocation(Position);
        return Actor;
    };
    auto* Floor = Place(FVector(0,0,-50), FVector(100,100,1));
    auto* Plateau = Place(FVector(0,0,25), FVector(1,4,.5));
    Plateau->Tags = { LDDemoWorld::TerrainTag, TEXT("test_plateau") };
    FLDDemoWorldGeometry Geometry;
    Geometry.LevelStepCm = {50,1}; Geometry.SightSampleHeightCm = {150,1};
    auto& Surface = Geometry.Surfaces.AddDefaulted_GetRef(); Surface.Id = TEXT("test_floor");
    Surface.MinCm.X = {-5000,1}; Surface.MinCm.Y = {-5000,1};
    Surface.MaxCm.X = {5000,1}; Surface.MaxCm.Y = {5000,1};
    auto& Region = Geometry.Plateaus.AddDefaulted_GetRef(); Region.Id = TEXT("test_plateau"); Region.Level = 1;
    for (const FVector2D Corner : {FVector2D(-50,-200), FVector2D(50,-200), FVector2D(50,200), FVector2D(-50,200)})
    {
        auto& Point = Region.PolygonCm.AddDefaulted_GetRef();
        Point.X = {int64(Corner.X),1}; Point.Y = {int64(Corner.Y),1}; Point.Z = {50,1};
    }
    FString Error;
    FLDSightGeometry Sight;
    const auto Bind = [&]()
    {
        LDDemoWorld::FTerrain Terrain;
        if (!Terrain.Build(Geometry, Error)) { return false; }
        Terrain.BindScene(*World, FBox(FVector(-5000,-5000,-100), FVector(5000,5000,1000)));
        return Sight.Build(Geometry, {TEXT("native_sight"),1,1}, {3000,1}, Error, World, &Terrain);
    };
    if (!TestTrue(TEXT("Native terrain sight binds: ") + Error, Bind())) { return false; }
    const FVector Left(-100,0,0), Right(100,0,0);
    TestEqual(TEXT("Intermediate plateau blocks lower observers even below eye height"), Sight.Trace(Left, Right), ELDSightResult::Blocked);
    Plateau->SetActorLocation(FVector(0,1000,25));
    TestTrue(TEXT("Moved terrain rebinds"), Bind());
    TestEqual(TEXT("The old template polygon no longer occludes"), Sight.Trace(Left, Right), ELDSightResult::Visible);
    Plateau->SetActorLocation(FVector(-100,0,25));
    auto* Wall = Place(FVector(150,0,200), FVector(.5,4,4));
    Wall->GetStaticMeshComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Block);
    TestTrue(TEXT("Higher observer and lower wall bind"), Bind());
    TestEqual(TEXT("High ground sees across a tall wall rooted on the lower level"),
        Sight.Trace(FVector(-100,0,50), FVector(300,0,0)), ELDSightResult::Visible);
    Wall->Destroy(); Plateau->Destroy();
    auto* Obstacle = Place(FVector(0,0,200), FVector(.5,4,4));
    Obstacle->GetStaticMeshComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);
    TestTrue(TEXT("Sight-ignore obstacle binds"), Bind());
    TestEqual(TEXT("Pawn and ground-aim blocking alone do not hide units"), Sight.Trace(Left, Right), ELDSightResult::Visible);
    Obstacle->GetStaticMeshComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Block);
    TestTrue(TEXT("Explicit sight blocker binds"), Bind());
    TestEqual(TEXT("Same-level explicit sight blocker hides units"), Sight.Trace(Left, Right), ELDSightResult::Blocked);
    auto* Guard = Place(FVector(0,1000,100), FVector(.02,4,2));
    Guard->Tags.Add(TEXT("LDDemoCliffGuard"));
    TWeakObjectPtr<AActor> RemovedGuard(Guard);
    TestEqual(TEXT("Legacy derived collision is retired"), LDNativeGround::RetireLegacyCliffGuards(*World), 1);
    TestFalse(TEXT("Derived actor is destroyed rather than kept as a stale hidden fence"), RemovedGuard.IsValid());
    TestTrue(TEXT("Ordinary authored geometry remains"), IsValid(Floor) && IsValid(Obstacle));
    return !HasAnyErrors();
}
