#include "Tests/LDScenarioWalk.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDDemoEntry.h"
#include "Navigation/LDPathSteering.h"
#include "Region/LDMapDefinition.h"
#include "Region/LDMapRegistry.h"
#include "Vision/LDVisionDirector.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDVisionArrivalRoutesTest, "Lockdown.Rules.DemoVision.ArrivalRoutes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDVisionArrivalRoutesTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    TSharedPtr<const FLDDemoWorldGeometry> Geometry;
    FString Error;
    const auto* Definition = LoadObject<ULDMapDefinition>(nullptr, *(FString(LDDemoEntry::WorldDefinition) + TEXT(".DA_DemoWorld")));
    if (!TestNotNull(TEXT("Demo definition"), Definition)
        || !TestTrue(TEXT("Runtime catalog"), LDDemoEntry::LoadRuntimeContent(Content, Error))
        || !TestTrue(TEXT("Registered geometry"), Definition->Read(*Content, Geometry, Error))) { AddError(Error); return false; }
    FCollisionShape Capsule;
    if (!TestTrue(TEXT("Approved body capsule"), ULDMapRegistry::ContentCapsule(*Content, Capsule, Error))) { AddError(Error); return false; }
    const double BodyRadius = Capsule.GetCapsuleRadius(), Arrival = 150;
    const auto* Capture = Geometry->Anchors.FindByPredicate([](const auto& Row) { return Row.Id == TEXT("capture"); });
    FVector Centre;
    if (!TestNotNull(TEXT("Public capture anchor"), Capture) || !Capture->PositionCm.ToVector(Centre, Error)) { return false; }
    TArray<FVector> Goals;
    const auto& Vision = Content->Get().Parameters.VisionRadiusCm;
    const double SightRadius = double(Vision.Numerator) / Vision.Denominator;
    if (!TestTrue(TEXT("Separate arrival goals"), LDScenarioWalk::VisionGoals(Centre, BodyRadius, Arrival, SightRadius, Goals, Error)))
    { AddError(Error); return false; }
    TestEqual(TEXT("Three owning roles"), Goals.Num(), 3);
    FLDWorldRevision Revision;
    Revision.MapId = FName(*Geometry->MapId);
    Revision.LayoutRevision = Geometry->LayoutRevision;
    Revision.GeometryRevision = Geometry->GeometryRevision;
    FLDSightGeometry Sight;
    LDDemoWorld::FTerrain Terrain;
    if (!Sight.Build(*Geometry, Revision, Vision, Error) || !Terrain.Build(*Geometry, Error)) { AddError(Error); return false; }
    for (int32 A = 0; A < Goals.Num(); ++A)
    {
        for (int32 B = A + 1; B < Goals.Num(); ++B)
        {
            const double Distance = FVector::Dist2D(Goals[A], Goals[B]);
            TestTrue(TEXT("Arrival disks cannot occupy another role's goal"), Distance - 2 * Arrival > 2 * BodyRadius + LDPathSteering::WaypointToleranceCm);
            TestTrue(TEXT("Even opposite arrival edges remain in observation radius"), Distance + 2 * Arrival < SightRadius);
            TestEqual(TEXT("Approved terrain does not conceal either target"), Sight.Trace(Goals[A], Goals[B]), ELDSightResult::Visible);
            TestEqual(TEXT("Observation is mutual"), Sight.Trace(Goals[B], Goals[A]), ELDSightResult::Visible);
        }
    }
    TArray<LDScenarioWalk::FKeepout> Keepouts;
    if (!LDScenarioWalk::Keepouts(*Geometry, *Content, BodyRadius, Arrival, Keepouts, Error)) { AddError(Error); return false; }
    const TArray<LDScenarioWalk::FKeepout> OriginalKeepouts = Keepouts;
    const TArray<FVector> OriginalGoals = Goals;
    TArray<FVector> Patrol;
    for (const auto& Anchor : Geometry->Anchors)
    {
        if (Anchor.Kind != ELDDemoWorldAnchorKind::BossWaypoint) { continue; }
        FVector Point;
        if (!Anchor.PositionCm.ToVector(Point, Error)) { AddError(Error); return false; }
        Patrol.Add(Point);
    }
    TArray<FLDCompletePath> PatrolPaths;
    for (int32 I = 0; I < Patrol.Num(); ++I)
    {
        FLDCompletePath Path;
        Path.Points = {Patrol[I], Patrol[(I + 1) % Patrol.Num()]};
        PatrolPaths.Add(Path);
    }
    const auto& Aggro = Content->Get().Boss.AggroRadiusCm;
    LDScenarioWalk::PatrolKeepouts(PatrolPaths, double(Aggro.Numerator) / Aggro.Denominator
        + BodyRadius + Arrival + LDPathSteering::WaypointToleranceCm, Keepouts);
    const auto Validate = [&](TArray<FVector>& Points)
    {
        for (FVector& Point : Points) { FVector Normal; Terrain.Ground(FVector2D(Point), Point.Z, Normal); }
        for (int32 A = 0; A < Points.Num(); ++A)
        { for (int32 B = 0; B < Points.Num(); ++B)
            { if (A != B && Sight.Trace(Points[A], Points[B]) != ELDSightResult::Visible) { return false; } } }
        return true;
    };
    if (!LDScenarioWalk::VisionTargets(Centre, BodyRadius, Arrival, SightRadius, Keepouts, Validate, Goals, Error))
    { AddError(Error); return false; }
    for (const FVector& Goal : Goals)
    { TestFalse(TEXT("Each arrival disk avoids the public patrol corridor"), Keepouts.ContainsByPredicate([&](const auto& Zone)
        { return FVector::Dist2D(Goal, Zone.Centre) <= Zone.Radius; })); }
    TestTrue(TEXT("Captured blocked route crosses authored camp homes"),
        LDScenarioWalk::RouteKeepout({FVector(4478,2613,0), FVector(4257,1522,0), FVector(0,0,0)}, Keepouts) != INDEX_NONE);
    // Exercise the shared bounded selector with deterministic query results; the live probe separately validates
    // each owner's selected route through QueryGround/QueryCompletePath against the real native world.
    int32 GroundCalls = 0, PathCalls = 0;
    const auto Ground = [&](const FVector& Point, FLDLegalGround& Out, FString& Why)
    {
        ++GroundCalls;
        Out.World = Revision;
        Out.SurfaceCm = Out.FootCm = Point;
        FVector Normal;
        Terrain.Ground(FVector2D(Point), Out.SurfaceCm.Z, Normal);
        Why.Reset();
        return true;
    };
    const auto Path = [&](const FLDLegalGround& A, const FLDLegalGround& B, FLDCompletePath& Out, FString& Why)
    {
        ++PathCalls;
        Out.Points = {A.SurfaceCm, B.SurfaceCm};
        Out.LengthCm = FVector::Dist2D(A.SurfaceCm, B.SurfaceCm);
        Why.Reset();
        return true;
    };
    int32 Spawns = 0, Routes = 0;
    bool bCapturedSpawn = false;
    for (const auto& Anchor : Geometry->Anchors)
    {
        if (Anchor.Kind != ELDDemoWorldAnchorKind::Spawn) { continue; }
        FVector Spawn;
        if (!Anchor.PositionCm.ToVector(Spawn, Error)) { AddError(Error); return false; }
        ++Spawns;
        const bool bCaptured = FVector::Dist2D(Spawn, FVector(4478,2613,0)) < 2;
        bCapturedSpawn |= bCaptured;
        for (const FVector& Goal : Goals)
        {
            FLDLegalGround From, To;
            Ground(Spawn, From, Error);
            Ground(Goal, To, Error);
            TArray<FVector> Waypoints;
            if (!TestTrue(Anchor.Id + TEXT(" bounded route"), LDScenarioWalk::Plan(From, To, Keepouts, Arrival, Ground, Path, Waypoints, Error)))
            { AddError(Error); return false; }
            TestTrue(TEXT("Route ends at the role's own target"), Waypoints.Last().Equals(To.SurfaceCm));
            TArray<FVector> Segments = {From.SurfaceCm};
            Segments.Append(Waypoints);
            TestEqual(TEXT("All selected segments avoid authored homes"), LDScenarioWalk::RouteKeepout(Segments, Keepouts), INDEX_NONE);
            LDScenarioWalk::FPlanner Sliced;
            Sliced.Begin(From, To, Keepouts, Arrival);
            TArray<FVector> SlicedWaypoints;
            LDScenarioWalk::EPlan Result = LDScenarioWalk::EPlan::Working;
            int32 Steps = 0, MaxQueries = 0;
            const int32 CandidateBound = Keepouts.Num() * 48 + 1;
            const int64 SliceBound = int64(CandidateBound) * CandidateBound;
            while (Result == LDScenarioWalk::EPlan::Working && int64(Steps++) < SliceBound)
            {
                const int32 Before = GroundCalls + PathCalls;
                Result = Sliced.Step(Ground, Path, SlicedWaypoints, Error, 0);
                MaxQueries = FMath::Max(MaxQueries, GroundCalls + PathCalls - Before);
            }
            TestEqual(TEXT("Candidate slicing finishes"), Result, LDScenarioWalk::EPlan::Complete);
            TestTrue(TEXT("Sliced and synchronous selectors choose identical waypoints"), SlicedWaypoints == Waypoints);
            TestTrue(TEXT("One slice performs at most one candidate plus a frontier entry query"), MaxQueries <= 4);
            ++Routes;
        }
    }
    TestEqual(TEXT("Every authored spawn covered"), Spawns, 12);
    TestEqual(TEXT("Every spawn reaches every role target"), Routes, 36);
    TestTrue(TEXT("Captured northeast spawn covered"), bCapturedSpawn);
    // Preserve the captured camp regression independently of the relocated patrol-safe targets.
    FLDLegalGround CapturedFrom, CapturedTo;
    Ground(FVector(4478,2613,0), CapturedFrom, Error);
    bool bCapturedDetour = false;
    for (const FVector& Goal : OriginalGoals)
    {
        Ground(Goal, CapturedTo, Error);
        if (LDScenarioWalk::RouteKeepout({CapturedFrom.SurfaceCm, CapturedTo.SurfaceCm}, OriginalKeepouts) == INDEX_NONE)
        { continue; }
        TArray<FVector> CapturedWaypoints;
        if (!TestTrue(TEXT("Captured camp obstruction has a bounded detour"),
            LDScenarioWalk::Plan(CapturedFrom, CapturedTo, OriginalKeepouts, Arrival, Ground, Path, CapturedWaypoints, Error)))
        { AddError(Error); return false; }
        TArray<FVector> CapturedSegments = {CapturedFrom.SurfaceCm};
        CapturedSegments.Append(CapturedWaypoints);
        TestEqual(TEXT("Captured detour avoids the original authored exclusions"),
            LDScenarioWalk::RouteKeepout(CapturedSegments, OriginalKeepouts), INDEX_NONE);
        bCapturedDetour |= CapturedWaypoints.Num() > 1;
    }
    TestTrue(TEXT("Captured northeast camp regression selects a detour"), bCapturedDetour);
    TestTrue(TEXT("Selector validates detour ground and full path pairs"), GroundCalls > 72 && PathCalls > 36);
    FLDLegalGround From, To;
    Ground(FVector(0,0,0), From, Error);
    Ground(FVector(600,0,0), To, Error);
    TArray<FVector> Ignored;
    const TArray<LDScenarioWalk::FKeepout> Blocker = {{FVector(300,0,0), 80}};
    const auto RejectGround = [](const FVector&, FLDLegalGround&, FString& Why)
    { Why = TEXT("Test: no legal detour ground"); return false; };
    TestFalse(TEXT("No legal detour fails instead of repeating the blocked route"),
        LDScenarioWalk::Plan(From, To, Blocker, Arrival, RejectGround, Path, Ignored, Error));
    TestFalse(TEXT("Detour failure is explicit"), Error.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDVisionPatrolCorridorTest, "Lockdown.Rules.DemoVision.PatrolCorridor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDVisionPatrolCorridorTest::RunTest(const FString& Parameters)
{
    FLDCompletePath Path;
    Path.Points = {FVector(0,0,0), FVector(500,300,0), FVector(1000,0,0)};
    TArray<LDScenarioWalk::FKeepout> Zones;
    LDScenarioWalk::PatrolKeepouts({Path}, 400, Zones);
    for (int32 Segment = 1; Segment < Path.Points.Num(); ++Segment)
    {
        for (int32 I = 0; I <= 100; ++I)
        {
            const FVector Point = FMath::Lerp(Path.Points[Segment - 1], Path.Points[Segment], I / 100.);
            TestTrue(TEXT("Full native polyline, including bends, is excluded"), Zones.ContainsByPredicate([&](const auto& Zone)
            { return FVector::Dist2D(Point, Zone.Centre) <= Zone.Radius; }));
        }
    }
    TestTrue(TEXT("A route through the bend cannot be accepted"),
        LDScenarioWalk::RouteKeepout({FVector(500,-500,0), FVector(500,800,0)}, Zones) != INDEX_NONE);
    TestEqual(TEXT("A route outside the public corridor remains available"),
        LDScenarioWalk::RouteKeepout({FVector(0,1500,0), FVector(1000,1500,0)}, Zones), INDEX_NONE);
    return true;
}
#endif
