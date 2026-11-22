#include "World/LDSceneTags.h"
#include "World/LDNativeGround.h"
#include "Vision/LDVisionDirector.h"
#include "Region/LDMapGeometry.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

namespace
{
FLDDemoVisionRangeParameters SceneTagVisionRange()
{
    FLDDemoVisionRangeParameters Range;
    Range.NeutralHeightCm={35,1}; Range.FullHeightCm={120,1};
    Range.UphillRadiusBp=7500; Range.DownhillRadiusBp=12500;
    return Range;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDSceneTagCollision, "Lockdown.Rules.SceneTags.CollisionAndNavigation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDSceneTagCollision::RunTest(const FString& Parameters)
{
    UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
    ON_SCOPE_EXIT { UEditorLoadingAndSavingUtils::NewBlankMap(false); };
    auto* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    auto* Model = World->SpawnActor<AStaticMeshActor>();
    auto* Part = Model->GetStaticMeshComponent();
    Part->SetStaticMesh(Cube); Part->SetCollisionProfileName(TEXT("BlockAll"));
    Part->SetCanEverAffectNavigation(true);
    TestTrue(TEXT("Untagged authored collision supplies support"), LDNativeGround::IsSupport(*Part));
    Model->Tags.Add(TEXT("nOcOlLiSiOn"));
    TestFalse(TEXT("A tagged model is excluded from support before runtime mutation"), LDNativeGround::IsSupport(*Part));
    FHitResult Hit;
    TestFalse(TEXT("Native ground queries skip tagged physics bodies"), LDNativeGround::Trace(*World, Hit,
        FVector(0,0,200), FVector(0,0,-200), LDNativeGround::Query(*World)));
    LDSceneTags::PrepareEditorNavigation(*World);
    TestFalse(TEXT("Tagged models do not enter saved navigation"), Part->CanEverAffectNavigation());
    const auto* Saved = Cast<ULDSceneTagNavigationState>(Part->GetAssetUserDataOfClass(ULDSceneTagNavigationState::StaticClass()));
    TestTrue(TEXT("The original navigation flag is saved on its component"), Saved && Saved->bWasNavigationRelevant);
    Model->Tags.Reset(); LDSceneTags::PrepareEditorNavigation(*World);
    TestTrue(TEXT("Removing the tag restores authored navigation"), Part->CanEverAffectNavigation());
    TestNull(TEXT("A removed tag leaves no stale policy metadata"), Part->GetAssetUserDataOfClass(ULDSceneTagNavigationState::StaticClass()));
    auto* Group = World->SpawnActor<AActor>(); Group->SetRootComponent(NewObject<USceneComponent>(Group));
    Group->GetRootComponent()->SetMobility(EComponentMobility::Static);
    Group->GetRootComponent()->RegisterComponent();
    Model->AttachToActor(Group, FAttachmentTransformRules::KeepWorldTransform);
    Group->Tags.Add(TEXT("Nocollision"));
    TestTrue(TEXT("A model's attached collision actor inherits its tag"), LDSceneTags::NoCollision(*Part));
    Group->Tags.Reset();
    Part->ComponentTags.Add(TEXT("Nocollision"));
    LDSceneTags::ApplyRuntimePolicy(*Model);
    TestEqual(TEXT("Runtime collision and physics queries are disabled"), Part->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
    TestFalse(TEXT("Runtime overlaps are disabled"), Part->GetGenerateOverlapEvents());
    TestFalse(TEXT("Runtime tagged geometry is not navigation relevant"), Part->CanEverAffectNavigation());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDSceneTagSight, "Lockdown.Rules.SceneTags.SightAndTerrain",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDSceneTagSight::RunTest(const FString& Parameters)
{
    auto* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
    ON_SCOPE_EXIT { UEditorLoadingAndSavingUtils::NewBlankMap(false); };
    auto* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    const auto Place = [&](FVector At, FVector Scale)
    {
        auto* Actor = World->SpawnActor<AStaticMeshActor>();
        Actor->GetStaticMeshComponent()->SetStaticMesh(Cube);
        Actor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
        Actor->SetActorScale3D(Scale); Actor->SetActorLocation(At); return Actor;
    };
    auto* Floor = Place(FVector(0,0,-50), FVector(100,100,1));
    auto* Plateau = Place(FVector(0,0,25), FVector(1,4,.5));
    Plateau->Tags = {LDDemoWorld::TerrainTag, TEXT("test_plateau")};
    FLDDemoWorldGeometry Geometry; Geometry.LevelStepCm={50,1}; Geometry.SightSampleHeightCm={150,1};
    auto& Surface=Geometry.Surfaces.AddDefaulted_GetRef(); Surface.Id=TEXT("test_floor");
    Surface.MinCm.X={-5000,1}; Surface.MinCm.Y={-5000,1}; Surface.MaxCm.X={5000,1}; Surface.MaxCm.Y={5000,1};
    auto& Region=Geometry.Plateaus.AddDefaulted_GetRef(); Region.Id=TEXT("test_plateau"); Region.Level=1;
    for (const FVector2D Corner : {FVector2D(-50,-200),FVector2D(50,-200),FVector2D(50,200),FVector2D(-50,200)})
    { auto& Point=Region.PolygonCm.AddDefaulted_GetRef(); Point.X={int64(Corner.X),1}; Point.Y={int64(Corner.Y),1}; Point.Z={50,1}; }
    FString Error; FLDSightGeometry Sight;
    const auto Bind=[&]()
    {
        LDDemoWorld::FTerrain Terrain;
        if (!Terrain.Build(Geometry,Error)) { return false; }
        Terrain.BindScene(*World,FBox(FVector(-5000,-5000,-100),FVector(5000,5000,1000)));
        return Sight.Build(Geometry,{TEXT("scene_tags"),1,1},{3000,1},SceneTagVisionRange(),Error,World,&Terrain);
    };
    const FVector Left(-100,0,0),Right(100,0,0);
    TestTrue(TEXT("Native sight binds"),Bind());
    TestEqual(TEXT("Untagged elevated terrain blocks sight below eye height"),Sight.Trace(Left,Right),ELDSightResult::Blocked);
    Plateau->Tags.Add(TEXT("NoViewBlock"));
    TestTrue(TEXT("Tagged terrain rebinds"),Bind());
    TestEqual(TEXT("NoViewBlock bypasses semantic terrain occlusion too"),Sight.Trace(Left,Right),ELDSightResult::Visible);
    TestTrue(TEXT("NoViewBlock preserves standing support"),LDNativeGround::IsSupport(*Plateau->GetStaticMeshComponent()));
    Plateau->Destroy();
    auto* Wall=Place(FVector(0,0,200),FVector(.5,4,4));
    Wall->GetStaticMeshComponent()->ComponentTags.Add(TEXT("NoViewBlock"));
    TestTrue(TEXT("Component-tagged wall binds"),Bind());
    TestEqual(TEXT("Component tag bypasses ordinary line-of-sight collision"),Sight.Trace(Left,Right),ELDSightResult::Visible);
    LDSceneTags::ApplyRuntimePolicy(*Wall);
    TestEqual(TEXT("Runtime NoViewBlock keeps physical collision"),Wall->GetStaticMeshComponent()->GetCollisionResponseToChannel(ECC_Pawn),ECR_Block);
    TestEqual(TEXT("Runtime NoViewBlock ignores only LDVision"),Wall->GetStaticMeshComponent()->GetCollisionResponseToChannel(ECC_GameTraceChannel2),ECR_Ignore);
    TestTrue(TEXT("Unrelated floor remains collidable"),LDNativeGround::IsSupport(*Floor->GetStaticMeshComponent()));
    auto* Overhead=Place(FVector(400,0,400),FVector(2,2,1));
    const FVector Below(400,0,0);
    TestTrue(TEXT("Overhead endpoint fixture binds"),Bind());
    TestEqual(TEXT("A roof does not replace the known body's ground underneath"),Sight.Trace(Left,Below),ELDSightResult::Visible);
    TestEqual(TEXT("A body on the roof remains higher than the ground observer"),Sight.Trace(Left,FVector(400,0,450)),ELDSightResult::Blocked);
    Overhead->GetStaticMeshComponent()->ComponentTags.Add(TEXT("NoViewBlock"));
    TestTrue(TEXT("View-transparent endpoint fixture rebinds"),Bind());
    TestEqual(TEXT("NoViewBlock does not raise the endpoint tier beneath a canopy or roof"),Sight.Trace(Left,Below),ELDSightResult::Visible);
    FHitResult Physical;
    TestTrue(TEXT("The sight-only filter leaves native collision queries unchanged"),
        LDNativeGround::Trace(*World,Physical,Below+FVector(0,0,800),Below,LDNativeGround::Query(*World)) && Physical.GetActor()==Overhead);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDNativeMeshSight, "Lockdown.Rules.SceneTags.NativeMeshSight",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDNativeMeshSight::RunTest(const FString& Parameters)
{
    auto* World=UEditorLoadingAndSavingUtils::NewBlankMap(false);
    ON_SCOPE_EXIT { UEditorLoadingAndSavingUtils::NewBlankMap(false); };
    auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    const auto Place=[&](FVector At,FVector Scale)
    {
        auto* A=World->SpawnActor<AStaticMeshActor>();auto* C=A->GetStaticMeshComponent();
        C->SetStaticMesh(Cube);C->SetCollisionProfileName(TEXT("BlockAll"));A->SetActorScale3D(Scale);A->SetActorLocation(At);return A;
    };
    auto* Floor=Place(FVector(0,0,-50),FVector(100,100,1));
    Floor->Tags.Add(TEXT("NoViewBlock"));
    auto* Ramp=Place(FVector(1000,0,120),FVector(6,2,.2));
    Ramp->Tags.Add(LDDemoWorld::TerrainTag);Ramp->SetActorRotation(FRotator(10,0,0));
    FLDDemoWorldGeometry Geometry;Geometry.LevelStepCm={50,1};Geometry.SightSampleHeightCm={150,1};
    auto& Surface=Geometry.Surfaces.AddDefaulted_GetRef();Surface.Id=TEXT("floor");
    Surface.MinCm.X={-5000,1};Surface.MinCm.Y={-5000,1};Surface.MaxCm.X={5000,1};Surface.MaxCm.Y={5000,1};
    LDDemoWorld::FTerrain Terrain;FString Error;FLDSightGeometry Sight;
    if(!TestTrue(TEXT("Native terrain builds"),Terrain.Build(Geometry,Error))){return false;}
    Terrain.BindScene(*World,FBox(FVector(-5000,-5000,-100),FVector(5000,5000,1000)));
    FHitResult Hit;
    if(!TestTrue(TEXT("Local slope support exists"),LDNativeGround::Trace(*World,Hit,FVector(1150,0,400),FVector(1150,0,0),LDNativeGround::Query(*World)))){return false;}
    const FVector Foot=Hit.ImpactPoint+FVector(0,0,2.15);
    if(!TestTrue(TEXT("Native slope sight binds"),Sight.Build(Geometry,{TEXT("native_mesh_sight"),1,1},{3000,1},SceneTagVisionRange(),Error,World,&Terrain))){return false;}
    TestEqual(TEXT("A sloped mesh cannot conceal a body from itself using the mesh's maximum height"),Sight.Trace(Foot,Foot),ELDSightResult::Visible);
    TestEqual(TEXT("The same slope's equal-height neighbour stays visible"),Sight.Trace(Foot,Foot+FVector(0,30,0)),ELDSightResult::Visible);
    TestEqual(TEXT("A view-transparent floor still provides the observer's actual support level"),Sight.Trace(FVector(-1000,0,2.15),FVector(-900,0,2.15)),ELDSightResult::Visible);
    TArray<uint8> Bits;Sight.Coverage(Foot,Bits);
    TestTrue(TEXT("A body on the low portion of one mesh has nonempty coverage"),Bits.ContainsByPredicate([](uint8 B){return B!=0;}));
    Place(FVector(-1000,1000,120),FVector(.1,.1,.2));
    Place(FVector(-1000,1000,1000),FVector(15,15,1));
    TestTrue(TEXT("Narrow support beneath a roof binds"),Sight.Build(Geometry,{TEXT("native_mesh_sight"),1,2},{3000,1},SceneTagVisionRange(),Error,World,&Terrain));
    const FVector NarrowFoot(-1000,1000,132.15);
    Sight.Coverage(NarrowFoot,Bits);
    bool bNear=false;
    for(int32 I=0;I<LDObservation::CoverageCells;++I)
    { bNear|=LDObservation::CoverageBit(Bits,I)&&FVector2D::DistSquared(Sight.CellCentre(I),FVector2D(NarrowFoot))<250.*250.; }
    TestTrue(TEXT("Coarse coverage cells find lower ground beneath a roof beside narrow support"),bNear);
    return !HasAnyErrors();
}
