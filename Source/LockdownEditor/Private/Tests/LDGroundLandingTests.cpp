#include "Combat/LDGroundLanding.h"
#include "Core/LDGroundQueries.h"
#include "Navigation/LDPathSteering.h"
#include "Region/LDMapRegistry.h"
#include "Framework/LDDemoEntry.h"
#include "Development/LDDemoContentCatalog.h"
#include "AssetCompilingManager.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"

// A ground-point skill over the saved Demo world (native collision and navigation): open ground lands where aimed, a
// block standing on the aim (its wall and its roof, which has standing room but no walk to it) lands short on legal
// ground back toward the caster, and a caster in the air lands nothing.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDGroundLandingTest, "Lockdown.Rules.DemoGround.SkillLanding",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDGroundLandingTest::RunTest(const FString& Parameters)
{
    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(
        FPackageName::LongPackageNameToFilename(LDDemoEntry::WorldMap, FPackageName::GetMapPackageExtension()));
    if (!TestNotNull(TEXT("Saved Demo world"), World)) { return false; }
    FlushAsyncLoading();
    FAssetCompilingManager::Get().FinishAllCompilation();
    auto* Registry = World->GetSubsystem<ULDMapRegistry>();
    FString Error;
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    FCollisionShape Capsule;
    if (!TestNotNull(TEXT("Registry"), Registry)
        || !TestTrue(TEXT("Native world binding: ") + Error, Registry->Rebuild(Error))
        || !TestTrue(TEXT("Runtime content"), LDDemoEntry::LoadRuntimeContent(Content, Error))
        || !TestTrue(TEXT("Approved capsule"), ULDMapRegistry::ContentCapsule(*Content, Capsule, Error))
        || !TestTrue(TEXT("The world has a field site"), !Registry->Sites().IsEmpty())) { return false; }
    const FVector Caster = Registry->Sites()[0].Position;
    constexpr double Range = 450., Aim = 320.;

    // An open direction from the site: the aimed point lands exactly where it was aimed.
    FVector Requested = FVector::ZeroVector, Direction = FVector::ZeroVector;
    for (int32 Turn = 0; Turn < 16 && Direction.IsZero(); ++Turn)
    {
        const FVector Candidate = FVector(1, 0, 0).RotateAngleAxis(Turn * 22.5, FVector::UpVector);
        TOptional<FVector> Landed;
        if (!LDGroundLanding::Resolve(*Registry, Caster, Caster + Candidate * Aim, Range, Capsule, Landed, Error))
        { return TestTrue(TEXT("Landing query: ") + Error, false); }
        if (Landed && FVector::Dist2D(*Landed, Caster + Candidate * Aim) < 1.) { Direction = Candidate; Requested = Caster + Candidate * Aim; }
    }
    if (!TestFalse(TEXT("Some direction from the site is open ground"), Direction.IsZero())) { return false; }
    {
        TOptional<FVector> Far;
        TestTrue(TEXT("A point beyond the reach is clamped to it"), LDGroundLanding::Resolve(*Registry, Caster, Caster + Direction * 2000.,
            Aim, Capsule, Far, Error) && Far && FMath::Abs(FVector::Dist2D(*Far, Caster) - Aim) < 1.);
    }

    // A 4 m block standing on the aim: its top is standing room the body cannot walk to, its sides are walls.
    AActor* Block = World->SpawnActor<AActor>();
    auto* Box = NewObject<UBoxComponent>(Block);
    Block->SetRootComponent(Box);
    Block->AddInstanceComponent(Box);
    Box->SetBoxExtent(FVector(90, 90, 200));
    Box->SetCollisionObjectType(ECC_WorldStatic);
    Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Box->SetCollisionResponseToAllChannels(ECR_Block);
    Box->SetCanEverAffectNavigation(false);
    Box->RegisterComponent();
    Block->SetActorLocation(Requested + FVector(0, 0, 200));
    ON_SCOPE_EXIT
    {
        if (IsValid(Block)) { Block->Destroy(); }
        FString Restore;
        Registry->Rebuild(Restore);
    };
    if (!TestTrue(TEXT("The block binds as ordinary scene support: ") + Error, Registry->Rebuild(Error))) { return false; }
    // Rooftop & minimap regression: elevated 4 m block roof is excluded from walkable terrain floors, pointer/ground rays pass
    // through it to the walkable floor below, and minimap/PlanRoute project obstacle clicks to connected ground.
    TestFalse(TEXT("4 m elevated block roof is never admitted as a walkable terrain floor"), Registry->IsTerrainFloor(Box));
    {
        FHitResult PointerRayHit;
        TestTrue(TEXT("Pointer/terrain floor trace ignores 4 m rooftop and hits the walkable ground underneath"),
            Registry->TraceGround(Requested + FVector(0, 0, 1000), Requested - FVector(0, 0, 1000), PointerRayHit)
            && PointerRayHit.GetComponent() != Box && FMath::Abs(PointerRayHit.ImpactPoint.Z - Caster.Z) < 10.);
        FLDLegalGround RoofAttempt;
        ELDWorldQueryFailure RoofFail = ELDWorldQueryFailure::None;
        TestFalse(TEXT("QueryGround strictly rejects standing on the 4 m rooftop"),
            Registry->QueryGround(Registry->Revision(), Requested + FVector(0, 0, 400), Capsule,
                FName(*Registry->GetGeometry()->Surfaces[0].Id), RoofAttempt, RoofFail, Error));
        FVector MinimapSurface = FVector::ZeroVector;
        TestTrue(TEXT("Minimap MapSurface resolves walkable ground beneath the 4 m block instead of failing"),
            LDGround::MapSurface(World, FVector(Requested.X, Requested.Y, 0.), MinimapSurface)
            && FMath::Abs(MinimapSurface.Z - Caster.Z) < 10.);
        FLDLegalGround CasterGround, ProjectedGoal;
        FLDCompletePath ProjectedPath;
        ELDWorldQueryFailure RouteFail = ELDWorldQueryFailure::None;
        const FName SurfaceId(*Registry->GetGeometry()->Surfaces[0].Id);
        if (TestTrue(TEXT("Caster stands on legal ground"),
            Registry->QueryGround(Registry->Revision(), Caster, Capsule, SurfaceId, CasterGround, RouteFail, Error)))
        {
            TestTrue(TEXT("Minimap click inside 4 m block projects to a reachable connected ground point: ") + Error,
                LDPathSteering::PlanRoute(*Registry, CasterGround, MinimapSurface, SurfaceId, LDPathSteering::EGoal::Ground,
                    Capsule, ProjectedPath, ProjectedGoal, RouteFail, Error)
                && FMath::Abs(ProjectedGoal.SurfaceCm.Z - Caster.Z) < 100.
                && FVector::Dist2D(ProjectedGoal.SurfaceCm, Requested) >= 90.);
            // Near-edge click within capsule radius of the block wall (where Clicked ~= Recast edge): must also project cleanly.
            const FVector NearWallEdge = Requested - Direction * (90. + Capsule.GetCapsuleRadius() * .5);
            FVector NearEdgeSurface = FVector::ZeroVector;
            TestTrue(TEXT("Minimap near-wall-edge click resolves and routes without false Unreachable"),
                LDGround::MapSurface(World, NearWallEdge, NearEdgeSurface)
                && LDPathSteering::PlanRoute(*Registry, CasterGround, NearEdgeSurface, SurfaceId, LDPathSteering::EGoal::Ground,
                    Capsule, ProjectedPath, ProjectedGoal, RouteFail, Error));
            // Also verify a registered StaticGeometryTag blocker in L_Demo (e.g. tree or wall) resolves on minimap and projects.
            for (const FLDDemoWorldBlocker& Blocker : Registry->GetGeometry()->Blockers)
            {
                if (!Blocker.BlocksMovement) { continue; }
                FVector BlockerPt = FVector::ZeroVector;
                if (Blocker.Shape == ELDDemoWorldBlockerShape::Cylinder)
                {
                    if (!Blocker.CenterCm.ToVector(BlockerPt, Error)) { continue; }
                }
                else
                {
                    for (const FLDDemoWorldPoint& Corner : Blocker.PolygonCm)
                    {
                        FVector C;
                        if (Corner.ToVector(C, Error)) { BlockerPt += C / double(Blocker.PolygonCm.Num()); }
                    }
                }
                const double BlockerHeight = Blocker.HeightCm.Denominator > 0
                    ? double(Blocker.HeightCm.Numerator) / double(Blocker.HeightCm.Denominator) : 0.;
                if (BlockerHeight < 120. || FVector::Dist2D(BlockerPt, Caster) > 3500.) { continue; }
                FVector BlockerSurface = FVector::ZeroVector;
                TestTrue(TEXT("Minimap click directly on a registered scene blocker resolves walkable ground below its roof and routes: ") + Blocker.Id,
                    LDGround::MapSurface(World, BlockerPt, BlockerSurface)
                    && BlockerSurface.Z < BlockerPt.Z + BlockerHeight - 50.
                    && LDPathSteering::PlanRoute(*Registry, CasterGround, BlockerSurface, SurfaceId, LDPathSteering::EGoal::Ground,
                        Capsule, ProjectedPath, ProjectedGoal, RouteFail, Error));
                break;
            }
        }
    }
    TOptional<FVector> Short;
    if (!TestTrue(TEXT("Landing beside the block: ") + Error,
        LDGroundLanding::Resolve(*Registry, Caster, Requested, Range, Capsule, Short, Error))) { return false; }
    TestTrue(TEXT("A point on the roof or inside the walls lands short, outside the block, back toward the caster"),
        Short && FVector::Dist2D(*Short, Caster) < Aim - 90. && FVector::Dist2D(*Short, Caster) > 0.
        && FMath::Abs(Short->Z - Caster.Z) < 100.);
    if (Short)
    {
        FLDLegalGround Ground;
        ELDWorldQueryFailure Failure = ELDWorldQueryFailure::None;
        TestTrue(TEXT("The landing is legal ground"), Registry->QueryGround(Registry->Revision(), *Short, Capsule,
            FName(*Registry->GetGeometry()->Surfaces[0].Id), Ground, Failure, Error));
    }

    // A caster with no ground under it vouches for no route: nothing lands, and that is no error.
    TOptional<FVector> Airborne;
    TestTrue(TEXT("An airborne caster lands nothing"), LDGroundLanding::Resolve(*Registry, Caster + FVector(0, 0, 600), Requested - Direction * 150.,
        Range, Capsule, Airborne, Error) && !Airborne.IsSet());
    return true;
}
