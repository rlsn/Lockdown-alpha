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
        return true;
    }
    // The production map may replace or remove every saved tree. Tests that exercise a registered blocker
    // place their own: a licensed visual over a hidden collision instance bound to an approved cylinder identity.
    bool AddTree(const FVector& At)
    {
        const auto* Cylinder = Geometry->Blockers.FindByPredicate([](const FLDDemoWorldBlocker& B) { return B.Shape == ELDDemoWorldBlockerShape::Cylinder; });
        auto* Shape = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
        if (!Cylinder || !Shape || Catalog->Props.IsEmpty() || !Catalog->Props[0].Mesh)
        { Error = TEXT("Approved world has a cylinder blocker and the catalog has a licensed prop."); return false; }
        const double Radius = double(Cylinder->RadiusCm.Numerator) / Cylinder->RadiusCm.Denominator;
        const double Height = double(Cylinder->HeightCm.Numerator) / Cylinder->HeightCm.Denominator;
        Tree = World->SpawnActor<AStaticMeshActor>(At, FRotator::ZeroRotator);
        Tree->Tags = { FName(*Cylinder->Id), LDDemoWorld::StaticGeometryTag, LDStaticScene::VisualTag,
            FName(*(TEXT("LDSceneZone:") + LDArt::ZoneId(ELDArtZone::Blocker))) };
        auto* Visual = Tree->GetStaticMeshComponent();
        Visual->SetMobility(EComponentMobility::Static);
        Visual->SetStaticMesh(Catalog->Props[0].Mesh);
        Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Visual->SetCollisionResponseToAllChannels(ECR_Ignore);
        Visual->SetCanEverAffectNavigation(false);
        if (Catalog->Props[0].bFadeable) { Visual->ComponentTags.Add(ULDArtCatalog::FadeableTag); }
        auto* Body = NewObject<UStaticMeshComponent>(Tree, TEXT("TreeCollision"));
        Body->CreationMethod = EComponentCreationMethod::Instance;
        Body->SetupAttachment(Visual);
        Body->SetMobility(EComponentMobility::Static);
        Body->SetStaticMesh(Shape);
        Body->SetRelativeLocation(FVector(0, 0, Height * .5));
        Body->SetRelativeScale3D(FVector(Radius / 50., Radius / 50., Height / 100.));
        Body->SetCollisionProfileName(TEXT("BlockAll"));
        Body->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Ignore);
        Body->SetVisibility(false);
        Body->ComponentTags.Add(LDStaticScene::CollisionTag);
        Tree->AddInstanceComponent(Body);
        Body->RegisterComponent();
        return true;
    }
    ~FSceneFixture() { UEditorLoadingAndSavingUtils::NewBlankMap(false); }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDStaticScenePersistence, "Lockdown.Rules.StaticScene.PersistenceAndBinding",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDStaticScenePersistence::RunTest(const FString& Parameters)
{
    FSceneFixture F;
    if (!TestTrue(TEXT("Saved formal scene loads without requiring a template decoration: ")+F.Error,F.Load())) { return false; }
    auto* Registry=F.World->GetSubsystem<ULDMapRegistry>();
    if(!TestTrue(TEXT("Current authored scene binds: ")+F.Error,Registry->Rebuild(F.Error))){return false;}
    const auto* Definition=LoadObject<ULDMapDefinition>(nullptr,LDDemoEntry::WorldDefinition);
    if(!TestNotNull(TEXT("Saved world definition"),Definition)){return false;}
    FLDDemoWorldGeometry Base;
    TestTrue(TEXT("Authoring reads its complete saved geometry"),Definition->ReadSavedForEditing(Base,F.Error)
        &&FLDDemoWorldGeometry::StaticStruct()->CompareScriptStruct(&Base,F.Geometry.Get(),PPF_None));
    TStrongObjectPtr<ULDMapDefinition> Damaged(DuplicateObject<ULDMapDefinition>(Definition,GetTransientPackage()));
    Damaged->Checksum=TEXT("invalid");
    TestFalse(TEXT("Altered checksum is refused"),Damaged->ReadSavedForEditing(Base,F.Error));
    Damaged->Checksum=Definition->Checksum;Damaged->SourceJson+=TEXT(" ");
    TestFalse(TEXT("Altered saved bytes are refused"),Damaged->ReadSavedForEditing(Base,F.Error));
    const auto* Rest=Registry->Resolve(TEXT("rest_spawn_0"),Registry->Generation);
    if(!TestNotNull(TEXT("Semantic rest spawn"),Rest)){return false;}
    // This test owns its meshes. The production map may replace or remove every old tree, wall and visual overlay.
    const FVector Centre(Rest->Position.X,Rest->Position.Y,10000);
    auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    const auto Place=[&](FVector At,FVector Scale)
    {
        auto* A=F.World->SpawnActor<AStaticMeshActor>();auto* C=A->GetStaticMeshComponent();
        C->SetStaticMesh(Cube);C->SetCollisionProfileName(TEXT("BlockAll"));
        A->SetActorScale3D(Scale);A->SetActorLocation(At);return A;
    };
    Place(Centre-FVector(0,0,50),FVector(40,40,1));
    auto* Model=Place(Centre+FVector(0,0,200),FVector(1,1,4));
    auto* Part=Model->GetStaticMeshComponent();
    Part->SetCollisionResponseToChannel(ECC_GameTraceChannel2,ECR_Block);
    const FString Name=Model->GetName();
    if(!TestTrue(TEXT("Ordinary untagged native meshes bind"),Registry->Rebuild(F.Error))){return false;}
    const auto Revision=Registry->Revision();
    const FVector Before=Model->GetActorLocation(),Delta(600,200,0);
    const auto Hit=[&](const FVector& At)
    {
        FCollisionQueryParams Q;
        for(TActorIterator<AActor> It(F.World);It;++It){if(*It!=Model){Q.AddIgnoredActor(*It);}}
        FHitResult H;
        return F.World->LineTraceSingleByChannel(H,At-FVector(150,0,0),At+FVector(150,0,0),ECC_Pawn,Q)&&H.GetComponent()==Part;
    };
    TestTrue(TEXT("Authored native collision is queryable"),Hit(Before));
    Model->SetActorLocation(Before+Delta);
    TestFalse(TEXT("Moving a mesh clears its previous collision"),Hit(Before));
    TestTrue(TEXT("Collision follows the moved mesh"),Hit(Before+Delta));
    TestTrue(TEXT("A moved native mesh binds without export"),Registry->Rebuild(F.Error));
    TestTrue(TEXT("The physics edit invalidates the world revision"),Registry->Revision()!=Revision);
    FLDSightGeometry Sight;
    const auto Bind=[&]() {return Registry->Rebuild(F.Error)&&Sight.Build(*Registry->GetGeometry(),Registry->Revision(),
        F.Content->Get().Parameters.VisionRadiusCm,F.Error,F.World);};
    const FVector Left=Centre+Delta-FVector(100,0,0),Right=Centre+Delta+FVector(100,0,0);
    TestTrue(TEXT("Moved native sight binds"),Bind());
    TestEqual(TEXT("The moved native mesh blocks its actual sight location"),Sight.Trace(Left,Right),ELDSightResult::Blocked);
    Part->SetCollisionResponseToChannel(ECC_GameTraceChannel2,ECR_Ignore);
    TestTrue(TEXT("A sight channel edit rebinds"),Bind());
    TestEqual(TEXT("Pawn collision alone does not block sight"),Sight.Trace(Left,Right),ELDSightResult::Visible);
    Part->SetCollisionResponseToChannel(ECC_GameTraceChannel2,ECR_Block);
    const FString Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("Artifacts/StaticSceneTest/"));
    IFileManager::Get().MakeDirectory(*Directory,true);FPackageName::RegisterMountPoint(TEXT("/LDStaticSceneTest/"),Directory);
    ON_SCOPE_EXIT {UEditorLoadingAndSavingUtils::NewBlankMap(false);FPackageName::UnRegisterMountPoint(TEXT("/LDStaticSceneTest/"),Directory);};
    if(!TestTrue(TEXT("Edited native scene saves into an isolated fixture"),UEditorLoadingAndSavingUtils::SaveMap(F.World,TEXT("/LDStaticSceneTest/NativeMoved")))){return false;}
    F.World=UEditorLoadingAndSavingUtils::LoadMap(Directory/TEXT("NativeMoved.umap"));
    if(!TestNotNull(TEXT("Native fixture reopens"),F.World)){return false;}
    Model=nullptr;
    for(TActorIterator<AStaticMeshActor> It(F.World);It;++It){if(It->GetName()==Name){Model=*It;break;}}
    if(!TestNotNull(TEXT("Authored model survives reopen"),Model)){return false;}
    Part=Model->GetStaticMeshComponent();
    TestTrue(TEXT("Its transform and collision profile persist"),Model->GetActorLocation().Equals(Before+Delta,.01)
        &&Part->GetCollisionResponseToChannel(ECC_Pawn)==ECR_Block&&Part->GetCollisionResponseToChannel(ECC_GameTraceChannel2)==ECR_Block);
    TestTrue(TEXT("Reloaded collision blocks at its edited position"),Hit(Before+Delta));
    Registry=F.World->GetSubsystem<ULDMapRegistry>();Model->Destroy();
    TestTrue(TEXT("Deleting the model rebinds"),Registry->Rebuild(F.Error));
    Sight.Build(*Registry->GetGeometry(),Registry->Revision(),F.Content->Get().Parameters.VisionRadiusCm,F.Error,F.World);
    TestEqual(TEXT("Deleted geometry leaves no stale sight blocker"),Sight.Trace(Left,Right),ELDSightResult::Visible);
    return !HasAnyErrors();
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDStaticSceneCopy, "Lockdown.Rules.StaticScene.CopyIdentityAndSafety",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDStaticSceneCopy::RunTest(const FString& Parameters)
{
    FSceneFixture F;
    if (!TestTrue(TEXT("Saved formal scene loads: ") + F.Error, F.Load())) { return false; }
    FVector TreeAt;
    const auto* Cylinder = F.Geometry->Blockers.FindByPredicate([](const FLDDemoWorldBlocker& B) { return B.Shape == ELDDemoWorldBlockerShape::Cylinder; });
    if (!TestNotNull(TEXT("Approved world has a cylinder blocker identity"), Cylinder)
        || !TestTrue(TEXT("Cylinder centre parses: ") + F.Error, Cylinder->CenterCm.ToVector(TreeAt, F.Error))
        || !TestTrue(TEXT("Test-owned registered tree is placed: ") + F.Error, F.AddTree(FVector(TreeAt.X, TreeAt.Y, 10000)))) { return false; }
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
    // Only ramps the scene still binds by identity keep the approved half-level tier; replaced ramps are ordinary native terrain.
    for (const auto& Ramp : Registry->GetGeometry()->Ramps)
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
    const FVector RequiredFoot = RestSpawn->Position;
    Overlay->Destroy(); Left->Destroy(); Right->Destroy(); VisualOnly->Destroy();
    // Authored scenes may contain another genuine floor below the replaced one. Remove that support
    // too in this disposable world, so the assertion tests missing ground rather than a layered layout.
    const auto Query = LDNativeGround::Query(*F.World);
    TSet<UPrimitiveComponent*> Removed;
    FHitResult Remaining;
    while (LDNativeGround::Trace(*F.World, Remaining, RequiredFoot + FVector(0,0,100000),
        RequiredFoot - FVector(0,0,100000), Query))
    {
        auto* Part = Remaining.GetComponent();
        if (!Part || Removed.Contains(Part)) { AddError(TEXT("Failed to remove test support")); return false; }
        Removed.Add(Part);
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
    TestFalse(TEXT("Removing actual support still refuses unsafe spawn"), Registry->Rebuild(F.Error));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDOptionalAnchorMinimums, "Lockdown.Rules.StaticScene.OptionalAnchorMinimums",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// #293 acceptance: the mini flag, locker and vending points are whatever the saved level holds, but a level whose
// definition declares a kind (the Demo world) fails clearly once it holds fewer than that kind's minimum.
bool FLDOptionalAnchorMinimums::RunTest(const FString& Parameters)
{
    auto* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
    ON_SCOPE_EXIT { UEditorLoadingAndSavingUtils::NewBlankMap(false); };
    auto* Floor = World->SpawnActor<AStaticMeshActor>();
    Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
    Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
    Floor->SetActorScale3D(FVector(100,100,1));
    Floor->SetActorLocation(FVector(0,0,-50));
    FLDDemoWorldGeometry Base, Projected;
    auto& Surface = Base.Surfaces.AddDefaulted_GetRef();
    Surface.Id = TEXT("demo_ground");
    TMap<FString, TArray<ALDLayoutPoint*>> Placed;
    const auto Declare = [&](const TCHAR* Prefix, ELDDemoWorldAnchorKind Kind, int32 Count, double Y)
    {
        for (int32 Index = 0; Index < Count; ++Index)
        {
            const FString Id = FString::Printf(TEXT("%s%d"), Prefix, Index);
            auto& Anchor = Base.Anchors.AddDefaulted_GetRef();
            Anchor.Id = Id; Anchor.Kind = Kind; Anchor.SurfaceId = Surface.Id;
            auto* Point = World->SpawnActor<ALDLayoutPoint>();
            Point->SetActorLocation(FVector(500.0 * Index, Y, 0));
            Point->Tags.Add(FName(*(TEXT("LDSceneAnchor:") + Id)));
            Placed.FindOrAdd(Prefix).Add(Point);
        }
    };
    Declare(LDDemoWorld::MiniFlagPrefix(), ELDDemoWorldAnchorKind::MiniFlag, LDDemoWorld::MinMiniFlags, 0);
    Declare(LDDemoWorld::LockerPrefix(), ELDDemoWorldAnchorKind::Locker, LDDemoWorld::MinLockers, 1000);
    Declare(LDDemoWorld::VendingPrefix(), ELDDemoWorldAnchorKind::Vending, LDDemoWorld::MinVending, 2000);
    FString Error;
    if (!TestTrue(TEXT("A level holding every declared minimum projects: ") + Error,
        LDStaticScene::ProjectRuntimeGeometry(*World, Base, Projected, Error))) { return false; }
    TestEqual(TEXT("Every saved point becomes an anchor"), Projected.Anchors.Num(),
        LDDemoWorld::MinMiniFlags + LDDemoWorld::MinLockers + LDDemoWorld::MinVending);
    for (const TCHAR* Prefix : {LDDemoWorld::MiniFlagPrefix(), LDDemoWorld::LockerPrefix(), LDDemoWorld::VendingPrefix()})
    {
        TArray<ALDLayoutPoint*>& Points = Placed.FindChecked(Prefix);
        ALDLayoutPoint* Last = Points.Pop();
        Last->Destroy();
        TestFalse(*FString::Printf(TEXT("Deleting one %s* point below the minimum fails"), Prefix),
            LDStaticScene::ProjectRuntimeGeometry(*World, Base, Projected, Error));
        TestTrue(*FString::Printf(TEXT("The diagnostic names the %s* points"), Prefix), Error.Contains(Prefix));
        for (ALDLayoutPoint* Point : Points) { Point->Destroy(); }
        Points.Reset();
        TestFalse(*FString::Printf(TEXT("Deleting every %s* point fails"), Prefix),
            LDStaticScene::ProjectRuntimeGeometry(*World, Base, Projected, Error));
        // Restore the kind before checking the next one.
        const ELDDemoWorldAnchorKind Kind = Base.Anchors.FindByPredicate([Prefix](const FLDDemoWorldAnchor& A) { return A.Id.StartsWith(Prefix); })->Kind;
        const int32 Minimum = Kind == ELDDemoWorldAnchorKind::MiniFlag ? LDDemoWorld::MinMiniFlags
            : Kind == ELDDemoWorldAnchorKind::Locker ? LDDemoWorld::MinLockers : LDDemoWorld::MinVending;
        Base.Anchors.RemoveAll([Prefix](const FLDDemoWorldAnchor& A) { return A.Id.StartsWith(Prefix); });
        Declare(Prefix, Kind, Minimum, Kind == ELDDemoWorldAnchorKind::MiniFlag ? 0 : Kind == ELDDemoWorldAnchorKind::Locker ? 1000 : 2000);
    }
    TestTrue(TEXT("Restored, the level projects again: ") + Error, LDStaticScene::ProjectRuntimeGeometry(*World, Base, Projected, Error));
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
