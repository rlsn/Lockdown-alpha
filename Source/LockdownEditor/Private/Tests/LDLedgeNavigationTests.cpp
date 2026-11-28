#include "LDSceneAuthoringLibrary.h"
#include "Framework/LDDemoEntry.h"
#include "AssetCompilingManager.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "NavLinkComponent.h"
#include "NavMesh/RecastNavMesh.h"
#include "Character/LDCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Navigation/LDPathSteering.h"
#include "GameFramework/CharacterMovementComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDLedgeNavigation, "Lockdown.Rules.DemoMotionProbe.LedgeNavigation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDLedgeNavigation::RunTest(const FString& Parameters)
{
    UWorld* World=UEditorLoadingAndSavingUtils::LoadMap(
        FPackageName::LongPackageNameToFilename(LDDemoEntry::WorldMap,FPackageName::GetMapPackageExtension()));
    if (!TestNotNull(TEXT("Authored world"),World)) { return false; }
    FlushAsyncLoading();
    FAssetCompilingManager::Get().FinishAllCompilation();
    // Isolated raised tops in the central open space; cleanup never saves the fixture over the author's map.
    TArray<AActor*> Fixtures;
    ON_SCOPE_EXIT { for (auto* Actor : Fixtures) { if (IsValid(Actor)) { Actor->Destroy(); } } };
    auto Box=[&](FVector Centre,FVector Extent)
    {
        auto* Actor=World->SpawnActor<AActor>();
        Fixtures.Add(Actor);
        auto* Shape=NewObject<UBoxComponent>(Actor);
        Actor->SetRootComponent(Shape);
        Actor->AddInstanceComponent(Shape);
        Shape->SetMobility(EComponentMobility::Static);
        Shape->SetBoxExtent(Extent);
        Shape->SetCollisionProfileName(TEXT("BlockAll"));
        Shape->CanCharacterStepUpOn=ECB_Yes;
        Shape->SetCanEverAffectNavigation(true);
        Shape->RegisterComponent();
        Actor->SetActorLocation(Centre);
    };
    Box({0,0,250},{300,300,250});
    Box({0,-1200,10},{200,200,10});
    Box({1000,-1200,35},{200,200,35});
    // Seed obsolete derived data so a bake must actively retire it when ledge walking is disabled.
    auto* PreviousLinks=World->SpawnActor<AActor>();
    Fixtures.Add(PreviousLinks);
    PreviousLinks->Tags.Add(TEXT("LDDerivedLedgeLinks"));
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    if (!TestNotNull(TEXT("Editor navigation system"),Nav)) { return false; }
    UNavigationSystemV1::SetNavigationAutoUpdateEnabled(false,Nav);
    Nav->AddNavigationBuildLock(ENavigationBuildLock::NoUpdateInPIE);
    for (TActorIterator<ARecastNavMesh> It(World); It; ++It) { It->SetActorLocation(FVector(0,0,500)); }
    FString Error;
    if (!TestTrue(TEXT("Bake current edges: ")+Error,ULDSceneAuthoringLibrary::BakeNavigation(World,Error))) { return false; }
    TestTrue(TEXT("Explicit bake restores the engine's temporary PIE lock"),Nav->IsNavigationBuildingLocked(ENavigationBuildLock::NoUpdateInPIE));
    TestFalse(TEXT("A bake deletes obsolete downward link data"),IsValid(PreviousLinks));
    const auto* Movement=GetDefault<ALDCharacter>()->GetCharacterMovement();
    for (TActorIterator<ARecastNavMesh> It(World); It; ++It)
    {
        TestTrue(TEXT("Derived Recast transforms cannot offset saved world-coordinate paths"),It->GetActorTransform().Equals(FTransform::Identity));
        TestEqual(TEXT("Saved navigation and native walking share their slope limit"),It->AgentMaxSlope,Movement->GetWalkableFloorAngle());
        TestFalse(TEXT("No experimental jump links are generated"),It->bGenerateNavLinks);
    }
    Nav->RemoveNavigationBuildLock(ENavigationBuildLock::NoUpdateInPIE,UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
    const auto* Data=Nav?Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate):nullptr;
    if (!TestNotNull(TEXT("Baked native nav data"),Data)) { return false; }
    auto Route=[&](FVector From,FVector To)
    {
        FPathFindingQuery Query(World,*Data,From,To);
        Query.SetAllowPartialPaths(false);
        return Nav->FindPathSync(Data->GetConfig(),Query);
    };
    auto Complete=[](const FPathFindingResult& R)
    { return R.IsSuccessful() && R.Path.IsValid() && !R.Path->IsPartial(); };
    TestFalse(TEXT("Navigation cannot route off an isolated 5 m cliff"),Complete(Route({0,0,500},{800,0,0})));
    TestFalse(TEXT("The same cliff cannot be climbed in reverse"),Complete(Route({800,0,0},{0,0,500})));
    // Small-step traversal is exercised in TerrainTraversal using isolated native collision;
    // this authored map can place different floors under these XY coordinates.
    TestFalse(TEXT("A 70 cm vertical face has no climbing route"),Complete(Route({600,-1200,0},{1000,-1200,70})));

    // An author can remove a platform without managing cached link actors. The normal Play hook rebakes it.
    Fixtures[0]->Destroy();
    FEditorDelegates::PreBeginPIE.Broadcast(false);
    int32 DerivedActors=0;
    bool StaleLink=false;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (!It->ActorHasTag(TEXT("LDDerivedLedgeLinks"))) { continue; }
        ++DerivedActors;
        if (const auto* Links=It->FindComponentByClass<UNavLinkComponent>())
        {
            for (const auto& Link : Links->Links)
            {
                StaleLink|=FMath::Abs(Link.Left.X)<400 && FMath::Abs(Link.Left.Y)<400 && Link.Left.Z>400;
            }
        }
    }
    TestEqual(TEXT("Ledge walking disabled produces no downward link actors"),DerivedActors,0);
    TestFalse(TEXT("Play preparation removes links from a deleted platform"),StaleLink);
    return !HasAnyErrors();
}

// B01: the Boss stood on the low floor beside a raised top in L_Demo; a tall navmesh projection of its foot answered with
// that top (a 71 cm rise), so every route from it was refused and the Boss froze. Its own layer has navmesh within reach,
// and the planner's start now prefers it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDPlanStartLayer, "Lockdown.Rules.DemoMotionProbe.PlanStartLayer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDPlanStartLayer::RunTest(const FString& Parameters)
{
    UWorld* World=UEditorLoadingAndSavingUtils::LoadMap(
        FPackageName::LongPackageNameToFilename(LDDemoEntry::WorldMap,FPackageName::GetMapPackageExtension()));
    if (!TestNotNull(TEXT("Authored world"),World)) { return false; }
    FlushAsyncLoading();
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    const ANavigationData* Data=Nav?Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate):nullptr;
    if (!TestNotNull(TEXT("Saved native nav data"),Data)) { return false; }
    const ALDCharacter* Body=GetDefault<ALDCharacter>();
    const double Reach=Body->GetCapsuleComponent()->GetUnscaledCapsuleRadius()*2.;
    const double Step=Body->GetCharacterMovement()->MaxStepHeight;
    const FVector Foot(682.006,2020.499,500.550);
    FNavLocation Tall;
    if (Data->ProjectPoint(Foot,Tall,FVector(Reach,Reach,500.)))
    {
        AddInfo(FString::Printf(TEXT("A tall projection of the foot answers %s (%.1f cm up)"),*Tall.Location.ToString(),Tall.Location.Z-Foot.Z));
    }
    FVector Layer;
    if (!TestTrue(TEXT("The foot's own layer has navmesh within reach and one step"),
        LDPathSteering::SameLayerNavPoint(*Data,Foot,Reach,Step,Layer))) { return false; }
    TestTrue(TEXT("That point is on the floor the Boss stands on"),FMath::Abs(Layer.Z-Foot.Z)<=Step && FVector::Dist2D(Layer,Foot)<=Reach);
    FPathFindingQuery Query(World,*Data,Layer,FVector(329.406,1093.551,500.550));
    Query.SetAllowPartialPaths(false);
    const FPathFindingResult Result=Nav->FindPathSync(Data->GetConfig(),Query);
    TestTrue(TEXT("From it the Boss has a complete route back to its first waypoint"),
        Result.IsSuccessful() && Result.Path.IsValid() && !Result.Path->IsPartial());
    return !HasAnyErrors();
}
