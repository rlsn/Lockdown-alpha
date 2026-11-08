#include "Tests/LDScenarioWalk.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Development/LDDemoContentCatalog.h"
#include "Navigation/LDPathSteering.h"
#include "Region/LDMapRegistry.h"

namespace LDScenarioWalk
{
bool Keepouts(const FLDDemoWorldGeometry& Geometry, const FLDDemoContentSnapshot& Content,
    double BodyRadius, double Arrival, TArray<FKeepout>& Out, FString& Error)
{
    Out.Reset();
    const auto Rational = [](const FLDDemoRational& Value)
    { return Value.Denominator > 0 ? double(Value.Numerator) / Value.Denominator : -1.; };
    const auto& Defs = Content.Get();
    for (const auto& Anchor : Geometry.Anchors)
    {
        if (Anchor.Kind != ELDDemoWorldAnchorKind::Camp && Anchor.Kind != ELDDemoWorldAnchorKind::BossWaypoint) { continue; }
        double Radius = BodyRadius + Arrival;
        if (Anchor.Kind == ELDDemoWorldAnchorKind::BossWaypoint)
        {
            Radius += Rational(Defs.Boss.AggroRadiusCm);
            FVector Centre;
            if (!Anchor.PositionCm.ToVector(Centre, Error)) { return false; }
            if (!FMath::IsFinite(Radius) || Radius <= 0)
            { Error = TEXT("The fixture walking exclusion has an invalid radius."); return false; }
            Out.Add({Centre, Radius});
            continue;
        }
        const auto* Placement = Defs.Camps.FindByPredicate([&](const auto& Row) { return Row.AnchorId == Anchor.Id; });
        const FLDDemoMonsterDefinition* Monster = nullptr;
        if (!Placement || !Content.FindMonster(Placement->MonsterId, Monster, Error))
        { Error = TEXT("The authored camp has no approved monster: ") + Anchor.Id; return false; }
        const double MonsterRadius = Rational(Monster->CollisionRadiusCm);
        if (MonsterRadius <= 0 || Anchor.Members.IsEmpty())
        { Error = TEXT("The authored camp has invalid body homes or radius: ") + Anchor.Id; return false; }
        Radius += MonsterRadius;
        for (const auto& Member : Anchor.Members)
        {
            FVector Home;
            if (!Member.PositionCm.ToVector(Home, Error)) { return false; }
            Out.Add({Home, Radius});
        }
    }
    Error.Reset();
    return true;
}

void PatrolKeepouts(const TArray<FLDCompletePath>& Paths, double Radius, TArray<FKeepout>& Out)
{
    for (const FLDCompletePath& Path : Paths)
    {
        // Conservative discs cover each public native patrol segment without gaps.
        for (int32 I = 1; I < Path.Points.Num(); ++I)
        {
            const FVector A = Path.Points[I - 1], B = Path.Points[I];
            const int32 Steps = FMath::Max(1, FMath::CeilToInt(FVector::Dist2D(A, B) / Radius));
            const double HalfSpacing = FVector::Dist2D(A, B) / Steps / 2;
            for (int32 Step = 0; Step <= Steps; ++Step)
            { Out.Add({FMath::Lerp(A, B, double(Step) / Steps), Radius + HalfSpacing}); }
        }
    }
}

bool PatrolPaths(const ULDMapRegistry& Registry, const FLDDemoContentSnapshot& Content,
    TArray<FLDCompletePath>& Out, FString& Error)
{
    const auto* Geometry = Registry.GetGeometry();
    if (!Geometry) { Error = TEXT("Missing authored Boss patrol geometry."); return false; }
    TArray<const FLDDemoWorldAnchor*> Patrol;
    for (const auto& Anchor : Geometry->Anchors)
    { if (Anchor.Kind == ELDDemoWorldAnchorKind::BossWaypoint) { Patrol.Add(&Anchor); } }
    Patrol.Sort([](const auto& A, const auto& B) { return A.Id < B.Id; });
    if (Patrol.Num() != 4) { Error = TEXT("The Vision fixture requires the four approved Boss patrol waypoints."); return false; }
    FCollisionShape Capsule;
    if (!ULDMapRegistry::ContentCapsule(Content, Capsule, Error)) { return false; }
    Out.Reset();
    for (int32 I = 0; I < Patrol.Num(); ++I)
    {
        FVector A, B;
        FLDLegalGround GroundA, GroundB, From, To;
        FLDCompletePath Path;
        ELDWorldQueryFailure Failure = ELDWorldQueryFailure::None;
        if (Patrol[I]->SurfaceId != Patrol[0]->SurfaceId || Patrol[(I + 1) % Patrol.Num()]->SurfaceId != Patrol[0]->SurfaceId
            || !Patrol[I]->PositionCm.ToVector(A, Error) || !Patrol[(I + 1) % Patrol.Num()]->PositionCm.ToVector(B, Error)
            || !Registry.QueryGround(Registry.Revision(), A, Capsule, FName(*Patrol[0]->SurfaceId), GroundA, Failure, Error)
            || !LDPathSteering::PlanStart(Registry, GroundA, Capsule, From, Failure, Error)
            || !Registry.QueryGround(Registry.Revision(), B, Capsule, FName(*Patrol[0]->SurfaceId), GroundB, Failure, Error)
            || !LDPathSteering::PlanStart(Registry, GroundB, Capsule, To, Failure, Error)
            || !Registry.QueryCompletePath(Registry.Revision(), From, To, Capsule, Path, Failure, Error))
        { Error = TEXT("The Vision fixture's public native Boss patrol corridor is unavailable: ") + Error; return false; }
        Out.Add(MoveTemp(Path));
    }
    return true;
}

int32 RouteKeepout(const TArray<FVector>& Path, const TArray<FKeepout>& Keepouts)
{
    for (int32 I = 1; I < Path.Num(); ++I)
    {
        const FVector A(Path[I - 1].X, Path[I - 1].Y, 0), B(Path[I].X, Path[I].Y, 0);
        const FVector Direction = (B - A).GetSafeNormal();
        double FirstEntry = DBL_MAX;
        int32 FirstZone = INDEX_NONE;
        for (int32 Zone = 0; Zone < Keepouts.Num(); ++Zone)
        {
            const auto& Keepout = Keepouts[Zone];
            const FVector Delta = FVector(Keepout.Centre.X, Keepout.Centre.Y, 0) - A;
            if (FMath::PointDistToSegment(A + Delta, A, B) >= Keepout.Radius) { continue; }
            const double Along = FVector::DotProduct(Delta, Direction);
            const double AcrossSquared = FMath::Max(0., Delta.SizeSquared() - Along * Along);
            const double Entry = FMath::Max(0., Along - FMath::Sqrt(FMath::Max(0., FMath::Square(Keepout.Radius) - AcrossSquared)));
            if (Entry < FirstEntry) { FirstEntry = Entry; FirstZone = Zone; }
        }
        if (FirstZone != INDEX_NONE) { return FirstZone; }
    }
    return INDEX_NONE;
}

void FPlanner::Begin(const FLDLegalGround& From, const FLDLegalGround& InGoal, const TArray<FKeepout>& Keepouts, double InArrival)
{
    *this = FPlanner();
    Goal = InGoal;
    Zones = Keepouts;
    Arrival = InArrival;
    Visits.Add({From, {}, 0, FVector::Dist2D(From.SurfaceCm, Goal.SurfaceCm), false});
}

EPlan FPlanner::Step(FGroundQuery Ground, FPathQuery Path, TArray<FVector>& Waypoints, FString& Error, double BudgetSeconds)
{
    const double Began = FPlatformTime::Seconds();
    // Each authored exclusion contributes exactly 48 candidates. A candidate is expanded only once; unlike a
    // single greedy walk, this bounded frontier can backtrack out of overlapping camp/Boss exclusions.
    while (true)
    {
        if (!bExpanding)
        {
            if (Expanded >= Zones.Num() * 48 + 1) { break; }
            int32 Next = INDEX_NONE;
            for (int32 I = 0; I < Visits.Num(); ++I)
            {
                if (!Visits[I].Expanded && (Next == INDEX_NONE || Visits[I].Estimate < Visits[Next].Estimate)) { Next = I; }
            }
            if (Next == INDEX_NONE) { break; }
            Visits[Next].Expanded = true;
            ++Expanded;
            Current = Visits[Next];
            FLDCompletePath Direct;
            if (!Path(Current.Ground, Goal, Direct, Error)) { return EPlan::Failed; }
            const int32 Blocked = RouteKeepout(Direct.Points, Zones);
            if (Blocked == INDEX_NONE)
            {
                Waypoints = Current.Waypoints;
                Waypoints.Add(Goal.SurfaceCm);
                Error.Reset();
                return EPlan::Complete;
            }
            ClearTail.Reset();
            ClearCost = DBL_MAX;
            PendingZones = {Blocked};
            Pending = Candidate = 0;
            bExpanding = true;
        }
        if (Pending >= PendingZones.Num()) { bExpanding = false; continue; }
        const auto& Zone = Zones[PendingZones[Pending]];
        const double Scales[] = {1.5, 2., 3.};
        const double Scale = Scales[Candidate / 16];
        const int32 Angle = Candidate % 16;
        const FLDLegalGround& Start = Current.Ground;
        const auto TryCandidate = [&]()
        {
            const double Radians = Angle * UE_TWO_PI / 16;
            const FVector Point(Zone.Centre.X + Zone.Radius * Scale * FMath::Cos(Radians),
                Zone.Centre.Y + Zone.Radius * Scale * FMath::Sin(Radians), 0);
            if (FVector::Dist2D(Point, Start.SurfaceCm) <= Arrival
                || Current.Waypoints.ContainsByPredicate([&Point](const FVector& Previous)
                    { return FVector::Dist2D(Previous, Point) <= LDPathSteering::WaypointToleranceCm; })) { return; }
            FLDLegalGround Detour;
            FLDCompletePath First, Second;
            FString Why;
            if (!Ground(Point, Detour, Why) || !Path(Start, Detour, First, Why)) { return; }
            const int32 Earlier = RouteKeepout(First.Points, Zones);
            if (Earlier != INDEX_NONE) { PendingZones.AddUnique(Earlier); return; }
            if (!Path(Detour, Goal, Second, Why)) { return; }
            const double Cost = Current.Cost + First.LengthCm;
            if (RouteKeepout(Second.Points, Zones) == INDEX_NONE && Cost + Second.LengthCm < ClearCost)
            {
                ClearCost = Cost + Second.LengthCm;
                ClearTail = Current.Waypoints;
                ClearTail.Add(Detour.SurfaceCm);
                ClearTail.Add(Goal.SurfaceCm);
            }
            const int32 Existing = Visits.IndexOfByPredicate([&](const FVisit& Visit)
            { return FVector::Dist2D(Visit.Ground.SurfaceCm, Detour.SurfaceCm) <= LDPathSteering::WaypointToleranceCm; });
            if (Existing != INDEX_NONE && (Visits[Existing].Expanded || Visits[Existing].Cost <= Cost)) { return; }
            FVisit Visit;
            Visit.Ground = Detour;
            Visit.Waypoints = Current.Waypoints;
            Visit.Waypoints.Add(Detour.SurfaceCm);
            Visit.Cost = Cost;
            Visit.Estimate = Cost + Second.LengthCm;
            if (Existing == INDEX_NONE) { Visits.Add(MoveTemp(Visit)); }
            else { Visits[Existing] = MoveTemp(Visit); }
        };
        TryCandidate();
        ++Candidate;
        if (Candidate % 16 == 0 && !ClearTail.IsEmpty())
        {
            Waypoints = MoveTemp(ClearTail);
            Error.Reset();
            return EPlan::Complete;
        }
        if (Candidate == 48) { Candidate = 0; ++Pending; }
        if (BudgetSeconds >= 0 && FPlatformTime::Seconds() - Began >= BudgetSeconds)
        {
            Error.Reset();
            return EPlan::Working;
        }
    }
    Error = TEXT("No legal native fixture route in the bounded camp/Boss detour frontier.");
    return EPlan::Failed;
}

bool Plan(const FLDLegalGround& From, const FLDLegalGround& Goal, const TArray<FKeepout>& Keepouts,
    double Arrival, FGroundQuery Ground, FPathQuery Path, TArray<FVector>& Waypoints, FString& Error)
{
    FPlanner Planner;
    Planner.Begin(From, Goal, Keepouts, Arrival);
    Waypoints.Reset();
    return Planner.Step(Ground, Path, Waypoints, Error, -1) == EPlan::Complete;
}

bool Begin(FPlanner& Planner, const ULDMapRegistry& Registry, const FLDDemoContentSnapshot& Content, const FVector& Foot,
    const FVector& Site, const TArray<FKeepout>& Keepouts, double Arrival, FString& Error, double* MaxQuerySeconds, int32* QueryCount)
{
    FCollisionShape Capsule;
    FLDLegalGround From, Start, Goal;
    ELDWorldQueryFailure Failure = ELDWorldQueryFailure::None;
    const auto* Geometry = Registry.GetGeometry();
    if (!Geometry || Geometry->Surfaces.IsEmpty())
    { Error = TEXT("The fixture's registered ground surface is missing."); return false; }
    const FName Surface(*Geometry->Surfaces[0].Id);
    const auto Ground = [&](const FVector& Point, FLDLegalGround& Out)
    {
        const double Began = FPlatformTime::Seconds();
        if (QueryCount) { ++*QueryCount; }
        const bool bResult = Registry.QueryGround(Registry.Revision(), Point, Capsule, Surface, Out, Failure, Error);
        if (MaxQuerySeconds) { *MaxQuerySeconds = FMath::Max(*MaxQuerySeconds, FPlatformTime::Seconds() - Began); }
        return bResult;
    };
    if (!ULDMapRegistry::ContentCapsule(Content, Capsule, Error)
        || !Ground(Foot, From)
        || !LDPathSteering::PlanStart(Registry, From, Capsule, Start, Failure, Error)
        || !Ground(Site, Goal)) { return false; }
    Planner.Begin(Start, Goal, Keepouts, Arrival);
    return true;
}

EPlan Step(FPlanner& Planner, const ULDMapRegistry& Registry, const FLDDemoContentSnapshot& Content,
    TArray<FVector>& Waypoints, FString& Error, double* MaxQuerySeconds, int32* QueryCount)
{
    FCollisionShape Capsule;
    if (!ULDMapRegistry::ContentCapsule(Content, Capsule, Error)) { return EPlan::Failed; }
    const auto* Geometry = Registry.GetGeometry();
    if (!Geometry || Geometry->Surfaces.IsEmpty())
    { Error = TEXT("The fixture's registered ground surface is missing."); return EPlan::Failed; }
    const FName Surface(*Geometry->Surfaces[0].Id);
    ELDWorldQueryFailure Failure = ELDWorldQueryFailure::None;
    const auto Ground = [&](FVector Point, FLDLegalGround& Out, FString& Why)
    {
        FVector Normal;
        Registry.GetTerrain().Ground(FVector2D(Point), Point.Z, Normal);
        const double Began = FPlatformTime::Seconds();
        if (QueryCount) { ++*QueryCount; }
        const bool bResult = Registry.QueryGround(Registry.Revision(), Point, Capsule, Surface, Out, Failure, Why);
        if (MaxQuerySeconds) { *MaxQuerySeconds = FMath::Max(*MaxQuerySeconds, FPlatformTime::Seconds() - Began); }
        return bResult;
    };
    const auto Path = [&](const FLDLegalGround& A, const FLDLegalGround& B, FLDCompletePath& Out, FString& Why)
    {
        const double Began = FPlatformTime::Seconds();
        if (QueryCount) { ++*QueryCount; }
        const bool bResult = Registry.QueryCompletePath(Registry.Revision(), A, B, Capsule, Out, Failure, Why);
        if (MaxQuerySeconds) { *MaxQuerySeconds = FMath::Max(*MaxQuerySeconds, FPlatformTime::Seconds() - Began); }
        return bResult;
    };
    return Planner.Step(Ground, Path, Waypoints, Error);
}

bool Plan(const ULDMapRegistry& Registry, const FLDDemoContentSnapshot& Content, const FVector& Foot,
    const FVector& Site, const TArray<FKeepout>& Keepouts, double Arrival, TArray<FVector>& Waypoints, FString& Error)
{
    FPlanner Planner;
    if (!Begin(Planner, Registry, Content, Foot, Site, Keepouts, Arrival, Error)) { return false; }
    EPlan Result;
    do { Result = Step(Planner, Registry, Content, Waypoints, Error); } while (Result == EPlan::Working);
    return Result == EPlan::Complete;
}

bool VisionGoals(const FVector& Capture, double BodyRadius, double Arrival, double SightRadius,
    TArray<FVector>& Out, FString& Error)
{
    Out.Reset();
    // Even endpoints approached from opposite sides of their arrival disks must not form a held-goal crowd.
    const double Separation = 2 * Arrival + 2 * BodyRadius + LDPathSteering::WaypointToleranceCm * 2;
    if (!FMath::IsFinite(Separation) || BodyRadius <= 0 || Arrival <= 0 || Separation + 2 * Arrival >= SightRadius)
    { Error = TEXT("The Vision fixture cannot separate three arrivals within approved sight reach."); return false; }
    const double Radius = Separation / FMath::Sqrt(3.);
    for (int32 I = 0; I < 3; ++I)
    {
        const double Angle = I * UE_TWO_PI / 3;
        Out.Add(Capture + FVector(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle), 0));
    }

    Error.Reset();
    return true;
}

bool VisionTargets(const FVector& Capture, double BodyRadius, double Arrival, double SightRadius,
        const TArray<FKeepout>& Keepouts, TFunctionRef<bool(TArray<FVector>&)> Validate,
        TArray<FVector>& Out, FString& Error)
    {
        TArray<FVector> Offsets;
        if (!VisionGoals(FVector::ZeroVector, BodyRadius, Arrival, SightRadius, Offsets, Error)) { return false; }
        // Closest bounded authored-space candidate first. This is fixture placement, never a product default.
        for (int32 Ring = 0; Ring <= 4; ++Ring)
        {
            for (int32 Angle = 0; Angle < (Ring == 0 ? 1 : 16); ++Angle)
            {
                const double Radians = Angle * UE_TWO_PI / 16;
                const FVector Centre = Capture + FVector(Ring * 600 * FMath::Cos(Radians), Ring * 600 * FMath::Sin(Radians), 0);
                TArray<FVector> Candidate;
                bool Clear = true;
                for (const FVector& Offset : Offsets)
                {
                    const FVector Point = Centre + Offset;
                    if (Keepouts.ContainsByPredicate([&](const FKeepout& Zone)
                        { return FVector::Dist2D(Point, Zone.Centre) <= Zone.Radius; })) { Clear = false; break; }
                    Candidate.Add(Point);
                }
                if (Clear && Validate(Candidate)) { Out = MoveTemp(Candidate); Error.Reset(); return true; }
            }
        }
        Error = TEXT("No three legal mutually visible Vision fixture targets outside authored camp/Boss patrol exclusions.");
        return false;
    }
}
#endif
