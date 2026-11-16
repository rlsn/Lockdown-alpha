#include "Vision/LDVisionDirector.h"
#include "Region/LDMapDefinition.h"
#include "Framework/LDDemoEntry.h"
#include "Development/LDDemoContentCatalog.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace LDDemoVisionParityTests
{
namespace
{
double Orient(const FVector2D& A, const FVector2D& B, const FVector2D& C)
{
    return (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X);
}

bool Inside(const TArray<FVector2D>& Polygon, const FVector2D& Point)
{
    for (int32 I = 0; I < Polygon.Num(); ++I)
    {
        if (Orient(Polygon[I], Polygon[(I + 1) % Polygon.Num()], Point) < 0) { return false; }
    }
    return true;
}

bool Within(const FVector2D& A, const FVector2D& B, const FVector2D& Point)
{
    return FMath::Min(A.X, B.X) <= Point.X && Point.X <= FMath::Max(A.X, B.X)
        && FMath::Min(A.Y, B.Y) <= Point.Y && Point.Y <= FMath::Max(A.Y, B.Y);
}

bool Touch(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FVector2D& D)
{
    const double O1 = Orient(A, B, C), O2 = Orient(A, B, D), O3 = Orient(C, D, A), O4 = Orient(C, D, B);
    return (((O1 > 0 && O2 < 0) || (O1 < 0 && O2 > 0)) && ((O3 > 0 && O4 < 0) || (O3 < 0 && O4 > 0)))
        || (O1 == 0 && Within(A, B, C)) || (O2 == 0 && Within(A, B, D))
        || (O3 == 0 && Within(C, D, A)) || (O4 == 0 && Within(C, D, B));
}

bool Enters(const FVector2D& A, const FVector2D& B, const TArray<FVector2D>& Polygon)
{
    double Lower = 0, Upper = 1;
    for (int32 I = 0; I < Polygon.Num(); ++I)
    {
        const FVector2D Delta = Polygon[(I + 1) % Polygon.Num()] - Polygon[I];
        const double Constant = FVector2D::CrossProduct(Delta, A - Polygon[I]);
        const double Slope = FVector2D::CrossProduct(Delta, B - A);
        if (Slope == 0) { if (Constant <= 0) { return false; } }
        else if (Slope > 0) { Lower = FMath::Max(Lower, -Constant / Slope); }
        else { Upper = FMath::Min(Upper, -Constant / Slope); }
    }
    return Lower < Upper;
}

// The pre-#178 narrow-phase arithmetic, deliberately without any spatial index or cached tiers.
struct FReferenceSight
{
    struct FBlocker
    {
        TArray<FVector2D> Polygon;
        FVector2D Centre = FVector2D::ZeroVector;
        double Radius = 0;
        int32 Level = 0;
    };
    LDDemoWorld::FTerrain Terrain;
    TArray<FBlocker> Blockers;
    FVector2D Min = FVector2D::ZeroVector, Max = FVector2D::ZeroVector;
    double Radius = 0;

    bool Build(const FLDDemoWorldGeometry& Geometry, double InRadius, FString& Error)
    {
        if (!Terrain.Build(Geometry, Error)) { return false; }
        FVector Low, High;
        if (!Geometry.Surfaces[0].MinCm.ToVector(Low, Error) || !Geometry.Surfaces[0].MaxCm.ToVector(High, Error)) { return false; }
        Min = FVector2D(Low);
        Max = FVector2D(High);
        Radius = InRadius;
        for (const FLDDemoWorldBlocker& Source : Geometry.Blockers)
        {
            if (!Source.BlocksSight) { continue; }
            FBlocker& Blocker = Blockers.AddDefaulted_GetRef();
            FVector Vertex = FVector::ZeroVector;
            if (Source.Shape == ELDDemoWorldBlockerShape::Cylinder)
            {
                if (!Source.CenterCm.ToVector(Vertex, Error)) { return false; }
                Blocker.Centre = FVector2D(Vertex);
                Blocker.Radius = double(Source.RadiusCm.Numerator) / Source.RadiusCm.Denominator;
            }
            else
            {
                if (Source.PolygonCm.Num() < 3) { Error = TEXT("Reference sight needs a polygon."); return false; }
                for (const FLDDemoWorldPoint& Point : Source.PolygonCm)
                {
                    if (!Point.ToVector(Vertex, Error)) { return false; }
                    Blocker.Polygon.Add(FVector2D(Vertex));
                }
            }
            Blocker.Level = FMath::RoundToInt32(Vertex.Z / Terrain.StepCm);
        }
        return true;
    }

    int32 Tier(const FVector2D& Point) const
    {
        int32 Best = 0;
        for (const auto& Region : Terrain.Regions)
        {
            if (Region.Tier > Best && Inside(Region.Polygon, Point)) { Best = Region.Tier; }
        }
        return Best;
    }

    ELDSightResult Trace(const FVector& Source, const FVector& Target) const
    {
        const auto Valid = [this](const FVector& P)
        {
            return FMath::IsFinite(P.X) && FMath::IsFinite(P.Y) && FMath::IsFinite(P.Z)
                && P.X >= Min.X && P.X <= Max.X && P.Y >= Min.Y && P.Y <= Max.Y;
        };
        if (!Valid(Source) || !Valid(Target)) { return ELDSightResult::InvalidPoint; }
        const FVector2D A(Source), B(Target);
        if (FVector2D::DistSquared(A, B) > Radius * Radius) { return ELDSightResult::OutOfRange; }
        const int32 Observer = Tier(A);
        if (Tier(B) > Observer) { return ELDSightResult::Blocked; }
        for (const auto& Region : Terrain.Regions)
        {
            if (Region.Tier > Observer && Enters(A, B, Region.Polygon)) { return ELDSightResult::Blocked; }
        }
        for (const FBlocker& Blocker : Blockers)
        {
            if (Blocker.Level < Observer / 2) { continue; }
            if (Blocker.Radius > 0)
            {
                const FVector2D Delta = B - A;
                const double Length = Delta.SizeSquared();
                const double T = Length > 0 ? FMath::Clamp(FVector2D::DotProduct(Blocker.Centre - A, Delta) / Length, 0.0, 1.0) : 0.0;
                if (FVector2D::DistSquared(A + Delta * T, Blocker.Centre) <= Blocker.Radius * Blocker.Radius)
                { return ELDSightResult::Blocked; }
            }
            else
            {
                if (Inside(Blocker.Polygon, A) || Inside(Blocker.Polygon, B)) { return ELDSightResult::Blocked; }
                for (int32 I = 0; I < Blocker.Polygon.Num(); ++I)
                {
                    if (Touch(A, B, Blocker.Polygon[I], Blocker.Polygon[(I + 1) % Blocker.Polygon.Num()]))
                    { return ELDSightResult::Blocked; }
                }
            }
        }
        return ELDSightResult::Visible;
    }

    void Coverage(const FVector& Source, TArray<uint8>& Out) const
    {
        Out.Init(0, LDObservation::CoverageBytes);
        const double CellX = (Max.X - Min.X) / LDObservation::CoverageSide;
        const double CellY = (Max.Y - Min.Y) / LDObservation::CoverageSide;
        for (int32 I = 0; I < LDObservation::CoverageCells; ++I)
        {
            const double X = Min.X + ((I % LDObservation::CoverageSide) + .5) * CellX;
            const double Y = Min.Y + ((I / LDObservation::CoverageSide) + .5) * CellY;
            if (Trace(Source, FVector(X, Y, Source.Z)) == ELDSightResult::Visible) { LDObservation::SetCoverageBit(Out, I); }
        }
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoVisionGeometryParityTest, "Lockdown.Rules.DemoVision.GeometryParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDemoVisionGeometryParityTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    TSharedPtr<const FLDDemoWorldGeometry> Geometry;
    FString Error;
    const auto* Definition = LoadObject<ULDMapDefinition>(nullptr, *(FString(LDDemoEntry::WorldDefinition) + TEXT(".DA_DemoWorld")));
    if (!TestNotNull(TEXT("Registered Demo world asset"), Definition)
        || !TestTrue(TEXT("Approved runtime content"), LDDemoEntry::LoadRuntimeContent(Content, Error))
        || !TestTrue(TEXT("Approved world projection"), Definition->Read(*Content, Geometry, Error))) { AddError(Error); return false; }
    FLDWorldRevision World;
    World.MapId = FName(*Geometry->MapId);
    World.LayoutRevision = Geometry->LayoutRevision;
    World.GeometryRevision = Geometry->GeometryRevision;
    FLDSightGeometry Sight;
    FReferenceSight Reference;
    const auto& Radius = Content->Get().Parameters.VisionRadiusCm;
    if (!TestTrue(TEXT("Indexed sight builds"), Sight.Build(*Geometry, World, Radius, Error))
        || !TestTrue(TEXT("Reference sight builds"), Reference.Build(*Geometry, Sight.Radius(), Error))) { AddError(Error); return false; }
    TArray<FVector> Sources;
    Sources.Add(FVector(Reference.Min, 0));
    Sources.Add(FVector(Reference.Max, 0));
    Sources.Add(FVector(Reference.Min.X, Reference.Max.Y, 0));
    Sources.Add(FVector(Reference.Max.X, Reference.Min.Y, 0));
    for (const auto& Anchor : Geometry->Anchors)
    {
        FVector At;
        if (!Anchor.PositionCm.ToVector(At, Error)) { AddError(Error); return false; }
        Sources.Add(At);
    }
    const auto ProbePolygon = [&](const TArray<FVector2D>& Polygon)
    {
        FVector2D Centre = FVector2D::ZeroVector;
        for (int32 I = 0; I < Polygon.Num(); ++I)
        {
            Centre += Polygon[I] / Polygon.Num();
            const FVector2D Mid = (Polygon[I] + Polygon[(I + 1) % Polygon.Num()]) * .5;
            for (const FVector2D& At : {Polygon[I], Mid})
            {
                for (const FVector2D& Offset : {FVector2D::ZeroVector, FVector2D(.000001, .000001), FVector2D(-.000001, -.000001)})
                {
                    const FVector2D Point = At + Offset;
                    TestEqual(TEXT("Tier parity at terrain/blocker corners and edges"), Reference.Terrain.Tier(Point), Reference.Tier(Point));
                    const FVector A(Point, 0), B(Point + FVector2D(80, -80), 0);
                    TestTrue(TEXT("Trace parity at terrain/blocker corners and edges"), Sight.Trace(A, B) == Reference.Trace(A, B));
                }
            }
        }
        Sources.Add(FVector(Centre, 0));
    };
    for (const auto& Region : Reference.Terrain.Regions) { ProbePolygon(Region.Polygon); }
    for (const FReferenceSight::FBlocker& Blocker : Reference.Blockers)
    {
        if (Blocker.Radius == 0) { ProbePolygon(Blocker.Polygon); }
        else
        {
            const FVector A(Blocker.Centre + FVector2D(-100, Blocker.Radius), 0);
            const FVector B(Blocker.Centre + FVector2D(100, Blocker.Radius), 0);
            TestTrue(TEXT("Closed trunk tangent parity"), Sight.Trace(A, B) == Reference.Trace(A, B));
        }
    }
    FRandomStream Random(178);
    for (int32 I = 0; I < 2048; ++I)
    {
        const FVector2D Point(Random.FRandRange(Reference.Min.X - 400, Reference.Max.X + 400),
            Random.FRandRange(Reference.Min.Y - 400, Reference.Max.Y + 400));
        const FVector A(Point, 0), B(Point + FVector2D(Random.FRandRange(-1200, 1200), Random.FRandRange(-1200, 1200)), 0);
        TestEqual(TEXT("Seeded tier parity including beyond the map"), Reference.Terrain.Tier(Point), Reference.Tier(Point));
        TestTrue(TEXT("Seeded trace parity including range and map edges"), Sight.Trace(A, B) == Reference.Trace(A, B));
    }
    for (const FVector& Source : Sources)
    {
        TArray<uint8> Actual, Expected;
        Sight.Coverage(Source, Actual);
        Reference.Coverage(Source, Expected);
        if (!TestTrue(*FString::Printf(TEXT("All 16384 coverage bits agree at %s"), *Source.ToString()), Actual == Expected))
        { return false; }
    }
    AddInfo(FString::Printf(TEXT("Exact full-bitmap parity at %d registered anchors, terrain and wall centres, and map corners."), Sources.Num()));
    return true;
}
}
#endif
