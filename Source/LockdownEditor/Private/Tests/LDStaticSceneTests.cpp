#include "Art/LDStaticScene.h"
#include "Art/LDStaticSceneEditor.h"
#include "Art/LDArtPlacement.h"
#include "AssetCompilingManager.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/StaticMeshActor.h"
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
    TestTrue(TEXT("Saved scene matches its derived index: ") + F.Error, LDStaticScene::Validate(*F.World, *F.Catalog, F.Error));
    TestTrue(TEXT("Saved layout matches the approved geometry: ") + F.Error, LDStaticScene::ValidateGeometry(*F.World, *F.Geometry, F.Error));
    TestTrue(TEXT("Ordinary and final capture landmarks and blocker art match their physical geometry: ") + F.Error,
        LDArt::CheckPlacements(*F.Catalog, *F.Geometry, *F.Content, F.Error));
    FLDDemoWorldGeometry SeedWithoutFinalAnchors = *F.Geometry;
    SeedWithoutFinalAnchors.Anchors.RemoveAll([](const FLDDemoWorldAnchor& Anchor)
    {
        return Anchor.Kind == ELDDemoWorldAnchorKind::FinalSpawn || Anchor.Kind == ELDDemoWorldAnchorKind::FinalCapture;
    });
    FLDDemoWorldGeometry Projected;
    TestTrue(TEXT("Saved final anchor tags extend a legacy world projection: ") + F.Error,
        LDStaticScene::ProjectGeometry(*F.World, SeedWithoutFinalAnchors, Projected, F.Error));
    TestEqual(TEXT("All six final spawn anchors and capture are exported"),
        Projected.Anchors.FilterByPredicate([](const FLDDemoWorldAnchor& Anchor)
        {
            return Anchor.Kind == ELDDemoWorldAnchorKind::FinalSpawn || Anchor.Kind == ELDDemoWorldAnchorKind::FinalCapture;
        }).Num(), LDDemoWorld::FinalSpawns + 1);
    const auto Parts = LDStaticScene::Visuals(*F.World);
    TestEqual(TEXT("Every indexed placement is a saved standard mesh"), Parts.Num(), F.Catalog->Decorations.Num());
    for (const auto* Part : Parts)
    {
        if (!TestTrue(TEXT("Visible editable mesh has no extra collision/navigation"), Part->IsVisible()
            && Part->GetCollisionEnabled() == ECollisionEnabled::NoCollision && !Part->CanEverAffectNavigation())) { return false; }
    }
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
    auto* System = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
    auto* Copy = Cast<AStaticMeshActor>(System->DuplicateActor(F.Tree, F.World, FVector(600, 200, 0)));
    if (!TestNotNull(TEXT("Standard editor duplication creates a tree"), Copy)) { return false; }
    TestTrue(TEXT("Copied tree includes its collision"), LDStaticScene::Collision(*Copy) != Copy->GetStaticMeshComponent());
    FLDDemoWorldGeometry Projected;
    TestFalse(TEXT("Duplicate gameplay identity cannot silently run"), LDStaticScene::ProjectGeometry(*F.World, *F.Geometry, Projected, F.Error));
    TestTrue(TEXT("Synchronization assigns a fresh copy identity: ") + F.Error, LDStaticSceneEditor::NormalizeCopies(*F.World, *F.Geometry, F.Error));
    TestTrue(TEXT("Both copied collision bodies enter the scene projection: ") + F.Error, LDStaticScene::ProjectGeometry(*F.World, *F.Geometry, Projected, F.Error));
    TestEqual(TEXT("The extra tree is retained, not ignored"), Projected.Blockers.Num(), F.Geometry->Blockers.Num() + 1);
    TSet<FString> Ids;
    for (const auto& B : Projected.Blockers) { Ids.Add(B.Id); }
    TestEqual(TEXT("Every blocker identity is unique"), Ids.Num(), Projected.Blockers.Num());
    Copy->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
    TArray<FLDArtDecoration> Rows; TArray<FString> Hidden;
    TestFalse(TEXT("Accidental canopy collision is explicitly rejected"), LDStaticScene::Read(*F.World, *F.Catalog, Rows, Hidden, F.Error));
    return !HasAnyErrors();
}
