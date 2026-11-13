#include "Vision/LDVisionDirector.h"
#include "PK/LDPKDirector.h"
#include "Vision/LDWorldViewRelay.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDDemoEntry.h"
#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Development/LDDemoContentCatalog.h"
#include "Region/LDMapRegistry.h"
#include "Region/LDMapGeometry.h"
#include "World/LDCampDirector.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
constexpr double SightCoordinateLimitCm = 1000000.0;

bool SightRationalValue(const FLDDemoRational& Value, double& Out)
{
    Out = 0;
    if (Value.Denominator <= 0) { return false; }
    Out = double(Value.Numerator) / double(Value.Denominator);
    return FMath::IsFinite(Out);
}

double SightOrientation(const FVector2D& A, const FVector2D& B, const FVector2D& C)
{
    return (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X);
}

bool SightWithinBox(const FVector2D& A, const FVector2D& B, const FVector2D& Point)
{
    return FMath::Min(A.X, B.X) <= Point.X && Point.X <= FMath::Max(A.X, B.X)
        && FMath::Min(A.Y, B.Y) <= Point.Y && Point.Y <= FMath::Max(A.Y, B.Y);
}

// Closed segment intersection: touching a blocker edge or corner blocks sight.
bool SightSegmentsTouch(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FVector2D& D)
{
    const double O1 = SightOrientation(A, B, C), O2 = SightOrientation(A, B, D);
    const double O3 = SightOrientation(C, D, A), O4 = SightOrientation(C, D, B);
    if (((O1 > 0 && O2 < 0) || (O1 < 0 && O2 > 0)) && ((O3 > 0 && O4 < 0) || (O3 < 0 && O4 > 0))) { return true; }
    return (O1 == 0 && SightWithinBox(A, B, C)) || (O2 == 0 && SightWithinBox(A, B, D))
        || (O3 == 0 && SightWithinBox(C, D, A)) || (O4 == 0 && SightWithinBox(C, D, B));
}

// Closed disc test: a segment touching a trunk's edge blocks sight, as a polygon edge does.
bool SightSegmentTouchesDisc(const FVector2D& A, const FVector2D& B, const FVector2D& Centre, double Radius)
{
    const FVector2D Delta = B - A;
    const double Length = Delta.SizeSquared();
    const double T = Length > 0 ? FMath::Clamp(FVector2D::DotProduct(Centre - A, Delta) / Length, 0.0, 1.0) : 0.0;
    return FVector2D::DistSquared(A + Delta * T, Centre) <= Radius * Radius;
}

// The segment passes through the convex counterclockwise polygon's open interior.
bool SightSegmentEnters(const FVector2D& A, const FVector2D& B, const TArray<FVector2D>& Polygon)
{
    double Lower = 0, Upper = 1;
    for (int32 Index = 0; Index < Polygon.Num(); ++Index)
    {
        const FVector2D Delta = Polygon[(Index + 1) % Polygon.Num()] - Polygon[Index];
        const double Constant = FVector2D::CrossProduct(Delta, A - Polygon[Index]), Slope = FVector2D::CrossProduct(Delta, B - A);
        if (Slope == 0)
        {
            if (Constant <= 0) { return false; }
        }
        else if (Slope > 0) { Lower = FMath::Max(Lower, -Constant / Slope); }
        else { Upper = FMath::Min(Upper, -Constant / Slope); }
    }
    return Lower < Upper;
}

// The flat level a blocker stands on; the importer requires its base to lie at that level's height.
bool SightBlockerLevel(double BaseZ, double Step, int32& Out)
{
    Out = Step > 0 ? FMath::RoundToInt32(BaseZ / Step) : -1;
    return Out >= 0 && Out <= LDDemoWorld::MaxLevel && FMath::IsNearlyEqual(Out * Step, BaseZ, 1e-6);
}

bool SightInsideConvex(const TArray<FVector2D>& Polygon, const FVector2D& Point)
{
    for (int32 Index = 0; Index < Polygon.Num(); ++Index)
    {
        if (SightOrientation(Polygon[Index], Polygon[(Index + 1) % Polygon.Num()], Point) < 0) { return false; }
    }
    return true;
}

uint16 QuantizeObservedYaw(double Yaw)
{
    return uint16(FMath::RoundToInt32(FRotator::ClampAxis(Yaw) / 360.0 * 65536.0) & 0xFFFF);
}

bool SameObservedStatuses(const TArray<FLDObservedStatus>& A, const TArray<FLDObservedStatus>& B)
{
    if (A.Num() != B.Num()) { return false; }
    for (int32 Index = 0; Index < A.Num(); ++Index)
    {
        if (A[Index].StatusId != B[Index].StatusId || A[Index].Stacks != B[Index].Stacks
            || A[Index].ExpiresAtServerMs != B[Index].ExpiresAtServerMs) { return false; }
    }
    return true;
}

// Pose is not presented state; it changes every movement sample without a new revision.
bool SameObservedPresentation(const FLDObservedUnitView& A, const FLDObservedUnitView& B)
{
    return A.AuthorizedFieldMask == B.AuthorizedFieldMask && A.Kind == B.Kind && A.Presentation == B.Presentation
        && A.ActionCue == B.ActionCue && A.ActionAbility == B.ActionAbility && A.HitLook == B.HitLook
        && (A.RecentHitAgePlusOneMs != 0) == (B.RecentHitAgePlusOneMs != 0)
        && A.ContenderId == B.ContenderId && A.CreatureId == B.CreatureId && A.Level == B.Level
        && A.Health.CurrentP2 == B.Health.CurrentP2 && A.Health.MaxP2 == B.Health.MaxP2
        && A.Mana.CurrentP2 == B.Mana.CurrentP2 && A.Mana.MaxP2 == B.Mana.MaxP2
        && A.Actives.Abilities[0] == B.Actives.Abilities[0] && A.Actives.Abilities[1] == B.Actives.Abilities[1]
        && A.Actives.Abilities[2] == B.Actives.Abilities[2]
        && SameObservedStatuses(A.Statuses, B.Statuses);
}
}

bool FLDSightGeometry::Build(const FLDDemoWorldGeometry& Geometry, const FLDWorldRevision& InWorld,
    const FLDDemoRational& InRadius, FString& Error)
{
    *this = FLDSightGeometry();
    double RadiusValue = 0, SampleHeight = 0;
    if (!InWorld.IsValid() || !SightRationalValue(InRadius, RadiusValue) || RadiusValue <= 0 || RadiusValue > SightCoordinateLimitCm)
    {
        Error = TEXT("Observation requires the approved finite positive VISION.RadiusCm and a valid world revision.");
        return false;
    }
    if (Geometry.Surfaces.Num() != 1 || !SightRationalValue(Geometry.SightSampleHeightCm, SampleHeight) || SampleHeight <= 0)
    {
        Error = TEXT("Observation requires the single registered ground surface and a positive sight sample height.");
        return false;
    }
    FVector SurfaceMin, SurfaceMax;
    if (!Geometry.Surfaces[0].MinCm.ToVector(SurfaceMin, Error) || !Geometry.Surfaces[0].MaxCm.ToVector(SurfaceMax, Error)) { return false; }
    if (!(SurfaceMin.X < SurfaceMax.X && SurfaceMin.Y < SurfaceMax.Y))
    {
        Error = TEXT("Observation ground bounds are empty.");
        return false;
    }
    LDDemoWorld::FTerrain PreparedTerrain;
    if (!PreparedTerrain.Build(Geometry, Error)) { return false; }
    TArray<TArray<FVector2D>> Prepared;
    TArray<int32> PreparedLevels;
    TArray<FCircle> PreparedCircles;
    for (const FLDDemoWorldBlocker& Blocker : Geometry.Blockers)
    {
        double Height = 0;
        if (!SightRationalValue(Blocker.HeightCm, Height))
        {
            Error = FString::Printf(TEXT("Blocker %s has an invalid height."), *Blocker.Id);
            return false;
        }
        if (!Blocker.BlocksSight) { continue; }
        if (Height <= SampleHeight)
        {
            Error = FString::Printf(TEXT("Sight blocker %s must be taller than the sight sample."), *Blocker.Id);
            return false;
        }
        if (Blocker.Shape == ELDDemoWorldBlockerShape::Cylinder)
        {
            FVector Centre;
            FCircle Circle;
            if (!Blocker.CenterCm.ToVector(Centre, Error) || !SightRationalValue(Blocker.RadiusCm, Circle.Radius) || Circle.Radius <= 0)
            {
                Error = FString::Printf(TEXT("Sight blocker %s has an invalid cylinder."), *Blocker.Id);
                return false;
            }
            Circle.Centre = FVector2D(Centre.X, Centre.Y);
            if (!SightBlockerLevel(Centre.Z, PreparedTerrain.StepCm, Circle.Level))
            {
                Error = FString::Printf(TEXT("Sight blocker %s does not stand on a flat level."), *Blocker.Id);
                return false;
            }
            PreparedCircles.Add(Circle);
            continue;
        }
        if (Blocker.PolygonCm.Num() < 3)
        {
            Error = FString::Printf(TEXT("Sight blocker %s must be a polygon of at least three vertices."), *Blocker.Id);
            return false;
        }
        TArray<FVector2D> Polygon;
        double BaseZ = 0;
        for (const FLDDemoWorldPoint& Point : Blocker.PolygonCm)
        {
            FVector Vertex;
            if (!Point.ToVector(Vertex, Error)) { return false; }
            Polygon.Add(FVector2D(Vertex.X, Vertex.Y));
            BaseZ = Vertex.Z;
        }
        int32 Level = 0;
        if (!SightBlockerLevel(BaseZ, PreparedTerrain.StepCm, Level))
        {
            Error = FString::Printf(TEXT("Sight blocker %s does not stand on a flat level."), *Blocker.Id);
            return false;
        }
        PreparedLevels.Add(Level);
        for (int32 Index = 0; Index < Polygon.Num(); ++Index)
        {
            if (SightOrientation(Polygon[Index], Polygon[(Index + 1) % Polygon.Num()], Polygon[(Index + 2) % Polygon.Num()]) <= 0)
            {
                Error = FString::Printf(TEXT("Sight blocker %s is not strictly convex counterclockwise."), *Blocker.Id);
                return false;
            }
        }
        Prepared.Add(MoveTemp(Polygon));
    }
    World = InWorld;
    Min = FVector2D(SurfaceMin.X, SurfaceMin.Y);
    Max = FVector2D(SurfaceMax.X, SurfaceMax.Y);
    RadiusCm = RadiusValue;
    Blockers = MoveTemp(Prepared);
    BlockerLevels = MoveTemp(PreparedLevels);
    Circles = MoveTemp(PreparedCircles);
    Terrain = MoveTemp(PreparedTerrain);
    BuildGrid();
    Error.Reset();
    return true;
}

void FLDSightGeometry::CellRange(const FVector2D& Low, const FVector2D& High, int32& X0, int32& Y0, int32& X1, int32& Y1) const
{
    X0 = FMath::Clamp(FMath::FloorToInt32((Low.X - Min.X) / GridCellCm), 0, GridWidth - 1);
    Y0 = FMath::Clamp(FMath::FloorToInt32((Low.Y - Min.Y) / GridCellCm), 0, GridHeight - 1);
    X1 = FMath::Clamp(FMath::FloorToInt32((High.X - Min.X) / GridCellCm), 0, GridWidth - 1);
    Y1 = FMath::Clamp(FMath::FloorToInt32((High.Y - Min.Y) / GridCellCm), 0, GridHeight - 1);
}

void FLDSightGeometry::BuildGrid()
{
    GridWidth = FMath::Max(1, FMath::CeilToInt32((Max.X - Min.X) / GridCellCm));
    GridHeight = FMath::Max(1, FMath::CeilToInt32((Max.Y - Min.Y) / GridCellCm));
    CellPolygons.SetNum(GridWidth * GridHeight);
    CellCircles.SetNum(GridWidth * GridHeight);
    CellRegions.SetNum(GridWidth * GridHeight);
    int32 X0, Y0, X1, Y1;
    for (int32 Index = 0; Index < Terrain.Regions.Num(); ++Index)
    {
        FBox2D Box(ForceInit);
        for (const FVector2D& Point : Terrain.Regions[Index].Polygon) { Box += Point; }
        CellRange(Box.Min, Box.Max, X0, Y0, X1, Y1);
        for (int32 Y = Y0; Y <= Y1; ++Y) { for (int32 X = X0; X <= X1; ++X) { CellRegions[Y * GridWidth + X].Add(Index); } }
    }
    for (int32 Index = 0; Index < Blockers.Num(); ++Index)
    {
        FBox2D Box(ForceInit);
        for (const FVector2D& Point : Blockers[Index]) { Box += Point; }
        CellRange(Box.Min, Box.Max, X0, Y0, X1, Y1);
        for (int32 Y = Y0; Y <= Y1; ++Y) { for (int32 X = X0; X <= X1; ++X) { CellPolygons[Y * GridWidth + X].Add(Index); } }
    }
    for (int32 Index = 0; Index < Circles.Num(); ++Index)
    {
        MaxCircleRadius = FMath::Max(MaxCircleRadius, Circles[Index].Radius);
        CellRange(Circles[Index].Centre, Circles[Index].Centre, X0, Y0, X1, Y1);
        CellCircles[Y0 * GridWidth + X0].Add(Index);
    }
    CoverageTiers.SetNumUninitialized(LDObservation::CoverageCells);
    for (int32 Index = 0; Index < CoverageTiers.Num(); ++Index) { CoverageTiers[Index] = Terrain.Tier(CellCentre(Index)); }
}

bool FLDSightGeometry::InBounds(const FVector& FootCm) const
{
    return FMath::IsFinite(FootCm.X) && FMath::IsFinite(FootCm.Y) && FMath::IsFinite(FootCm.Z)
        && FootCm.X >= Min.X && FootCm.X <= Max.X && FootCm.Y >= Min.Y && FootCm.Y <= Max.Y;
}

bool FLDSightGeometry::SegmentBlocked(const FVector2D& From, const FVector2D& To, int32 Observer, int32 TargetTier) const
{
    if (TargetTier > Observer) { return true; }
    const int32 Level = Observer / 2;
    const FVector2D Low(FMath::Min(From.X, To.X), FMath::Min(From.Y, To.Y)), High(FMath::Max(From.X, To.X), FMath::Max(From.Y, To.Y));
    int32 X0, Y0, X1, Y1;
    // Polygons are listed in every cell their bounds touch; circles only in their centre's cell, so widen by a radius.
    CellRange(Low, High, X0, Y0, X1, Y1);
    TBitArray<TInlineAllocator<4>> TestedRegions(false, Terrain.Regions.Num());
    for (int32 Y = Y0; Y <= Y1; ++Y)
    {
        for (int32 X = X0; X <= X1; ++X)
        {
            for (const int32 Index : CellRegions[Y * GridWidth + X])
            {
                if (TestedRegions[Index]) { continue; }
                TestedRegions[Index] = true;
                const auto& Region = Terrain.Regions[Index];
                if (Region.Tier > Observer && SightSegmentEnters(From, To, Region.Polygon)) { return true; }
            }
        }
    }
    for (int32 Y = Y0; Y <= Y1; ++Y)
    {
        for (int32 X = X0; X <= X1; ++X)
        {
            for (const int32 Blocker : CellPolygons[Y * GridWidth + X])
            {
                if (BlockerLevels[Blocker] < Level) { continue; }
                const TArray<FVector2D>& Polygon = Blockers[Blocker];
                if (SightInsideConvex(Polygon, From) || SightInsideConvex(Polygon, To)) { return true; }
                for (int32 Index = 0; Index < Polygon.Num(); ++Index)
                {
                    if (SightSegmentsTouch(From, To, Polygon[Index], Polygon[(Index + 1) % Polygon.Num()])) { return true; }
                }
            }
        }
    }
    const FVector2D Reach(MaxCircleRadius, MaxCircleRadius);
    CellRange(Low - Reach, High + Reach, X0, Y0, X1, Y1);
    for (int32 Y = Y0; Y <= Y1; ++Y)
    {
        for (int32 X = X0; X <= X1; ++X)
        {
            for (const int32 Index : CellCircles[Y * GridWidth + X])
            {
                const FCircle& Circle = Circles[Index];
                if (Circle.Level >= Level && SightSegmentTouchesDisc(From, To, Circle.Centre, Circle.Radius)) { return true; }
            }
        }
    }
    return false;
}

ELDSightResult FLDSightGeometry::Trace(const FVector& SourceFootCm, const FVector& TargetFootCm) const
{
    if (!IsValid()) { return ELDSightResult::StaleWorld; }
    if (!InBounds(SourceFootCm) || !InBounds(TargetFootCm)) { return ELDSightResult::InvalidPoint; }
    const FVector2D From(SourceFootCm.X, SourceFootCm.Y), To(TargetFootCm.X, TargetFootCm.Y);
    if (FVector2D::DistSquared(From, To) > RadiusCm * RadiusCm) { return ELDSightResult::OutOfRange; }
    return SegmentBlocked(From, To, Terrain.Tier(From), Terrain.Tier(To)) ? ELDSightResult::Blocked : ELDSightResult::Visible;
}

FVector2D FLDSightGeometry::CellCentre(int32 Index) const
{
    const int32 Side = LDObservation::CoverageSide;
    const double CellX = (Max.X - Min.X) / Side, CellY = (Max.Y - Min.Y) / Side;
    return FVector2D(Min.X + ((Index % Side) + .5) * CellX, Min.Y + ((Index / Side) + .5) * CellY);
}

void FLDSightGeometry::Coverage(const FVector& SourceFootCm, TArray<uint8>& OutBits) const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(LDVisionCoverage);
    OutBits.Init(0, LDObservation::CoverageBytes);
    if (!IsValid() || !InBounds(SourceFootCm)) { return; }
    const int32 Side = LDObservation::CoverageSide;
    const double CellX = (Max.X - Min.X) / Side, CellY = (Max.Y - Min.Y) / Side;
    const int32 X0 = FMath::Clamp(FMath::FloorToInt32((SourceFootCm.X - RadiusCm - Min.X) / CellX), 0, Side - 1);
    const int32 X1 = FMath::Clamp(FMath::FloorToInt32((SourceFootCm.X + RadiusCm - Min.X) / CellX), 0, Side - 1);
    const int32 Y0 = FMath::Clamp(FMath::FloorToInt32((SourceFootCm.Y - RadiusCm - Min.Y) / CellY), 0, Side - 1);
    const int32 Y1 = FMath::Clamp(FMath::FloorToInt32((SourceFootCm.Y + RadiusCm - Min.Y) / CellY), 0, Side - 1);
    const FVector2D From(SourceFootCm);
    const int32 ObserverTier = Terrain.Tier(From);
    for (int32 Y = Y0; Y <= Y1; ++Y)
    {
        for (int32 X = X0; X <= X1; ++X)
        {
            const int32 Index = Y * Side + X;
            const FVector2D Centre = CellCentre(Index);
            if (FVector2D::DistSquared(From, Centre) <= RadiusCm * RadiusCm && !SegmentBlocked(From, Centre, ObserverTier, CoverageTiers[Index]))
            { LDObservation::SetCoverageBit(OutBits, Index); }
        }
    }
}

void FLDSightGeometry::AddDisk(const FVector& CentreCm, double DiskRadiusCm, TArray<uint8>& InOutBits) const
{
    if (!IsValid() || InOutBits.Num() != LDObservation::CoverageBytes || !FMath::IsFinite(DiskRadiusCm) || DiskRadiusCm <= 0) { return; }
    const FVector2D Centre(CentreCm);
    for (int32 Index = 0; Index < LDObservation::CoverageCells; ++Index)
    {
        if (FVector2D::DistSquared(Centre, CellCentre(Index)) <= DiskRadiusCm * DiskRadiusCm) { LDObservation::SetCoverageBit(InOutBits, Index); }
    }
}

bool FLDObserverCoverage::Sample(const FLDSightGeometry& Sight, const FVector& Foot, bool bPublish, bool bHasRelay, TArray<uint8>& OutBits)
{
    if (!bPublish || !bHasRelay)
    {
        OutBits.Init(0, LDObservation::CoverageBytes);
        return false;
    }
    const bool bRecompute = Bits.Num() != LDObservation::CoverageBytes || SourceFeet != Foot || World != Sight.Revision();
    if (bRecompute)
    {
        Sight.Coverage(Foot, Bits);
        SourceFeet = Foot;
        World = Sight.Revision();
    }
    OutBits = Bits;
    return bRecompute;
}

bool FLDObserverLedger::Begin(uint32 InMatchGeneration, FString& Error)
{
    if (InMatchGeneration == 0)
    {
        Error = TEXT("An observer ledger requires the current match generation.");
        return false;
    }
    *this = FLDObserverLedger();
    Match = InMatchGeneration;
    Gen = 1;
    bBegun = true;
    Error.Reset();
    return true;
}

bool FLDObserverLedger::Bind(ELDLifePhase InPhase, uint32 InLife, bool& bReset, FString& Error)
{
    bReset = false;
    if (!bBegun)
    {
        Error = TEXT("The observer ledger has not begun.");
        return false;
    }
    if (InPhase == LifePhase && InLife == Life) { Error.Reset(); return true; }
    if (Gen == MAX_uint32)
    {
        Error = TEXT("Observation generation exhausted.");
        return false;
    }
    ++Gen;
    LifePhase = InPhase;
    Life = InLife;
    Bindings.Reset();
    ProjectileBindings.Reset();
    Terminations.Reset();
    Pin.Reset();
    NextEpoch = 0;
    NextSequence = 0;
    bReset = true;
    Error.Reset();
    return true;
}

bool FLDObserverLedger::AllocateRevision(uint32& Out, FString& Error)
{
    if (!bBegun || Rev == MAX_uint32)
    {
        Error = TEXT("Observation revision exhausted.");
        return false;
    }
    Out = ++Rev;
    return true;
}

bool FLDObserverLedger::Sample(const TArray<const FLDObservedTarget*>& Visible, TArray<FGuid>& Removed,
    TArray<FLDObservedUnitView>& Units, FString& Error)
{
    Removed.Reset();
    Units.Reset();
    if (!bBegun)
    {
        Error = TEXT("The observer ledger has not begun.");
        return false;
    }
    if (Visible.Num() > LDObservation::MaxUnits)
    {
        Error = FString::Printf(TEXT("CapacityExceeded: %d visible units exceed the %d-unit observation bound."), Visible.Num(), LDObservation::MaxUnits);
        return false;
    }
    if (LifePhase != ELDLifePhase::Alive && Visible.Num() != 0)
    {
        Error = TEXT("An observer without a living body cannot authorize units.");
        return false;
    }
    // Every check runs before any binding changes, so a provider gap or exhausted counter leaves no partial interval.
    int32 Opening = 0;
    for (int32 Index = 0; Index < Visible.Num(); ++Index)
    {
        const FLDObservedTarget* Target = Visible[Index];
        if (!Target || !Target->Body.IsValid()
            || Visible.IndexOfByPredicate([Target](const FLDObservedTarget* Other) { return Other && Other->Body == Target->Body; }) != Index)
        {
            Error = TEXT("Visible observation targets require unique complete body references.");
            return false;
        }
        FLDObservedUnitView Probe = Target->Presented;
        Probe.Handle = FGuid(1, 1, 1, 1);
        Probe.ObservationEpoch = 1;
        Probe.StateRevision = 1;
        if (!LDObservation::ValidateUnit(Probe, Error)) { return false; }
        const FBinding* Existing = Bindings.FindByPredicate([Target](const FBinding& Binding) { return Binding.Body == Target->Body; });
        if (!Existing) { ++Opening; }
        else if (Existing->StateRevision == MAX_uint32 && !SameObservedPresentation(Existing->LastPresented, Target->Presented))
        {
            Error = TEXT("Observed state revision exhausted.");
            return false;
        }
    }
    if (uint64(NextEpoch) + uint64(Opening) > uint64(MAX_uint32))
    {
        Error = TEXT("Observation epoch exhausted.");
        return false;
    }
    for (int32 Index = 0; Index < Bindings.Num();)
    {
        const FLDCombatBodyRef& Body = Bindings[Index].Body;
        if (Visible.ContainsByPredicate([&Body](const FLDObservedTarget* Target) { return Target->Body == Body; })) { ++Index; continue; }
        Removed.Add(Bindings[Index].Handle);
        Bindings.RemoveAt(Index);
    }
    TArray<int32> Opened;
    for (const FLDObservedTarget* Target : Visible)
    {
        const int32 Index = Bindings.IndexOfByPredicate([Target](const FBinding& Binding) { return Binding.Body == Target->Body; });
        if (Index == INDEX_NONE)
        {
            FBinding Fresh;
            Fresh.Body = Target->Body;
            Fresh.Handle = FGuid::NewGuid();
            Fresh.StateRevision = 1;
            Fresh.LastPresented = Target->Presented;
            Opened.Add(Bindings.Add(MoveTemp(Fresh)));
        }
        else if (!SameObservedPresentation(Bindings[Index].LastPresented, Target->Presented))
        {
            ++Bindings[Index].StateRevision;
            Bindings[Index].LastPresented = Target->Presented;
        }
    }
    // Intervals opened together take epochs in random handle order, never in canonical body order.
    Opened.Sort([this](int32 A, int32 B) { return Bindings[A].Handle < Bindings[B].Handle; });
    for (const int32 Index : Opened) { Bindings[Index].Epoch = ++NextEpoch; }
    for (const FLDObservedTarget* Target : Visible)
    {
        const FBinding* Binding = Bindings.FindByPredicate([Target](const FBinding& Candidate) { return Candidate.Body == Target->Body; });
        FLDObservedUnitView Unit = Target->Presented;
        Unit.Handle = Binding->Handle;
        Unit.ObservationEpoch = Binding->Epoch;
        Unit.StateRevision = Binding->StateRevision;
        Units.Add(MoveTemp(Unit));
    }
    // Output order is observer-local: a frame or concealment never lists units in canonical body order.
    Units.Sort([](const FLDObservedUnitView& A, const FLDObservedUnitView& B) { return A.Handle < B.Handle; });
    Removed.Sort();
    Error.Reset();
    return true;
}

bool FLDObserverLedger::SampleProjectiles(const TArray<const FLDObservedProjectileFact*>& Visible, TArray<FGuid>& Removed,
    TArray<FLDObservedProjectileView>& Out, FString& Error)
{
    Removed.Reset();
    Out.Reset();
    if (!bBegun)
    {
        Error = TEXT("The observer ledger has not begun.");
        return false;
    }
    if (Visible.Num() > LDObservation::MaxProjectiles)
    {
        Error = FString::Printf(TEXT("CapacityExceeded: %d visible flights exceed the %d-flight observation bound."), Visible.Num(), LDObservation::MaxProjectiles);
        return false;
    }
    if (LifePhase != ELDLifePhase::Alive && Visible.Num() != 0)
    {
        Error = TEXT("An observer without a living body cannot authorize projectiles.");
        return false;
    }
    // Every check runs before any binding changes, as for units.
    TArray<FLDObservedProjectileView> Presented;
    for (int32 Index = 0; Index < Visible.Num(); ++Index)
    {
        const FLDObservedProjectileFact* Fact = Visible[Index];
        if (!Fact || !Fact->Key.IsValid() || !FMath::IsFinite(Fact->Heading.X) || !FMath::IsFinite(Fact->Heading.Y) || Fact->Heading.IsNearlyZero()
            || Visible.IndexOfByPredicate([Fact](const FLDObservedProjectileFact* Other) { return Other && Other->Key == Fact->Key; }) != Index)
        {
            Error = TEXT("Observed skill effects require unique keys and a planar heading.");
            return false;
        }
        FLDObservedProjectileView& View = Presented.AddDefaulted_GetRef();
        View.Handle = FGuid(1, 1, 1, 1);
        View.PositionCm = Fact->PositionCm;
        View.Heading = QuantizeObservedYaw(FMath::RadiansToDegrees(FMath::Atan2(Fact->Heading.Y, Fact->Heading.X)));
        View.Look = Fact->Look;
        View.RadiusCm = Fact->RadiusCm;
        View.InnerRadiusCm = Fact->InnerRadiusCm;
        View.AngleDegrees = Fact->AngleDegrees;
        View.WarningRemainingMs = Fact->WarningRemainingMs;
        if (!LDObservation::ValidateProjectile(View, Error)) { return false; }
    }
    for (int32 Index = 0; Index < ProjectileBindings.Num();)
    {
        const FGuid& Key = ProjectileBindings[Index].Key;
        if (Visible.ContainsByPredicate([&Key](const FLDObservedProjectileFact* Fact) { return Fact->Key == Key; })) { ++Index; continue; }
        Removed.Add(ProjectileBindings[Index].Handle);
        ProjectileBindings.RemoveAt(Index);
    }
    for (int32 Index = 0; Index < Visible.Num(); ++Index)
    {
        const FGuid& Key = Visible[Index]->Key;
        const FProjectileBinding* Binding = ProjectileBindings.FindByPredicate([&Key](const FProjectileBinding& Candidate) { return Candidate.Key == Key; });
        Presented[Index].Handle = Binding ? Binding->Handle : ProjectileBindings.Add_GetRef({Key, FGuid::NewGuid()}).Handle;
    }
    Out = MoveTemp(Presented);
    Out.Sort([](const FLDObservedProjectileView& A, const FLDObservedProjectileView& B) { return A.Handle < B.Handle; });
    Removed.Sort();
    Error.Reset();
    return true;
}

ELDObservedFlightAccess LDObservation::ClassifyFlight(bool bSourceActs, const FGuid& SourceGroup, bool bObserverGroup,
    const FGuid& ObserverGroup, const FGuid& ArenaGroup, bool bSpectating, bool bPKObserver)
{
    // A suspended (frozen) world body's effect is never shown, like the body itself.
    if (!bSourceActs) { return ELDObservedFlightAccess::Hidden; }
    const bool bArenaFlight = ArenaGroup.IsValid() && SourceGroup == ArenaGroup;
    // Spectators watch the whole arena: its effects need no sight line, like its bodies.
    if (bSpectating && bArenaFlight) { return ELDObservedFlightAccess::Granted; }
    // Another isolation group (another rest space, the arena for a non-spectator) stays hidden, like its bodies.
    if (!bObserverGroup || SourceGroup != ObserverGroup) { return ELDObservedFlightAccess::Hidden; }
    // A PK life sees what its living teammates legally see.
    return bPKObserver && bArenaFlight ? ELDObservedFlightAccess::SharedSight : ELDObservedFlightAccess::Sight;
}

bool LDObservation::SelectVisibleProjectiles(const FLDSightGeometry& Sight, const FVector& SourceFootCm,
    const TArray<FVector>& SharedFeetCm, const TArray<FLDObservedProjectileFact>& Facts,
    TFunctionRef<ELDObservedFlightAccess(const FLDObservedProjectileFact&)> Access,
    TArray<const FLDObservedProjectileFact*>& Out, FString& Error)
{
    Out.Reset();
    TArray<TPair<double, const FLDObservedProjectileFact*>> InSight;
    const auto Seen = [&Sight, &Error](const FVector& From, const FLDObservedProjectileFact& Fact, bool& bOut)
    {
        const ELDSightResult Result = Sight.Trace(From, Fact.PositionCm);
        if (Result == ELDSightResult::StaleWorld)
        {
            Error = TEXT("StaleWorld: projectile sight has no current world.");
            return false;
        }
        bOut = Result == ELDSightResult::Visible;
        return true;
    };
    for (const FLDObservedProjectileFact& Fact : Facts)
    {
        if (!Fact.Key.IsValid() || !FMath::IsFinite(Fact.PositionCm.X) || !FMath::IsFinite(Fact.PositionCm.Y) || !FMath::IsFinite(Fact.PositionCm.Z))
        {
            Error = TEXT("A Combat flight has no key or no finite position.");
            return false;
        }
        const ELDObservedFlightAccess Rule = Access(Fact);
        bool bVisible = Rule == ELDObservedFlightAccess::Granted;
        if (Rule == ELDObservedFlightAccess::Sight || Rule == ELDObservedFlightAccess::SharedSight)
        {
            if (!Seen(SourceFootCm, Fact, bVisible)) { return false; }
            for (int32 Index = 0; !bVisible && Rule == ELDObservedFlightAccess::SharedSight && Index < SharedFeetCm.Num(); ++Index)
            {
                if (!Seen(SharedFeetCm[Index], Fact, bVisible)) { return false; }
            }
        }
        if (bVisible)
        { InSight.Emplace(FVector2D::DistSquared(FVector2D(SourceFootCm.X, SourceFootCm.Y), FVector2D(Fact.PositionCm.X, Fact.PositionCm.Y)), &Fact); }
    }
    InSight.Sort([](const TPair<double, const FLDObservedProjectileFact*>& A, const TPair<double, const FLDObservedProjectileFact*>& B)
    { return A.Key < B.Key; });
    for (int32 Index = 0; Index < InSight.Num() && Index < MaxProjectiles; ++Index) { Out.Add(InSight[Index].Value); }
    Error.Reset();
    return true;
}

bool FLDObserverLedger::OrderBound(const FLDObservedTerminationFact& Fact) const
{
    return Pin.IsSet() && Pin->Handle == Fact.Wire.Handle && Pin->Epoch == Fact.Wire.ObservationEpoch;
}

bool FLDObserverLedger::Newer(const FLDMoveIdentity& Candidate, const FLDMoveIdentity& Current) const
{
    return Candidate.Match == Current.Match && Candidate.Life == Current.Life
        && (Candidate.Order > Current.Order || (Candidate.Order == Current.Order && Candidate.Epoch > Current.Epoch));
}

FLDObserverLedger::ETerminate FLDObserverLedger::Terminate(const FLDCombatBodyRef& Victim, const FLDMatchContext& InMatch,
    double OccurredAtSeconds, int64 OccurredAtServerMs, FLDObservedTerminationFact& Out, FString& Error)
{
    const int32 Index = Bindings.IndexOfByPredicate([&Victim](const FBinding& Binding) { return Binding.Body == Victim; });
    if (Index == INDEX_NONE) { return ETerminate::NotBound; }
    if (NextSequence == MAX_uint32)
    {
        Error = TEXT("Observed termination sequence exhausted.");
        return ETerminate::Failed;
    }
    if (Terminations.Num() >= LDObservation::MaxTerminations)
    {
        const int32 Tail = Terminations.IndexOfByPredicate([this](const FLDObservedTerminationFact& Fact) { return !OrderBound(Fact); });
        if (Tail == INDEX_NONE)
        {
            Error = TEXT("LD_OBSERVATION_TERMINATION_CAPACITY: every retained ObservedDeath fact is bound to a current order.");
            return ETerminate::Failed;
        }
        Terminations.RemoveAt(Tail);
    }
    FLDObservedTerminationFact Fact;
    Fact.Wire.Handle = Bindings[Index].Handle;
    Fact.Wire.ObservationEpoch = Bindings[Index].Epoch;
    Fact.Wire.Generation = Gen;
    Fact.Wire.TerminationSequence = NextSequence + 1;
    Fact.Wire.OccurredAtServerMs = OccurredAtServerMs;
    Fact.Wire.Kind = ELDObservedTerminationKind::ObservedDeath;
    Fact.Match = InMatch;
    Fact.Victim = Victim.Victim;
    Fact.OccurredAtSeconds = OccurredAtSeconds;
    if (!LDObservation::ValidateTermination(Fact.Wire, Error)) { return ETerminate::Failed; }
    ++NextSequence;
    Terminations.Add(Fact);
    Bindings.RemoveAt(Index);
    Out = MoveTemp(Fact);
    Error.Reset();
    return ETerminate::Terminated;
}

bool FLDObserverLedger::BindOrder(const FLDMoveIdentity& Order, const FGuid& Handle, uint32 ObservationEpoch, FString& Error)
{
    if (!bBegun || LifePhase != ELDLifePhase::Alive)
    {
        Error = TEXT("ObserverUnavailable: an order pin requires an observer with a living body.");
        return false;
    }
    if (Order.Match != Match || Order.Life != Life || Order.Order == 0)
    {
        Error = TEXT("StaleOrder: the order does not belong to this observer match and life.");
        return false;
    }
    if (Pin.IsSet())
    {
        if (Pin->Order == Order)
        {
            if (Pin->Handle == Handle && Pin->Epoch == ObservationEpoch) { Error.Reset(); return true; }
            if (!FindTermination(Pin->Handle, Pin->Epoch))
            {
                Error = TEXT("The current order's pinned target has not ended; it cannot switch targets.");
                return false;
            }
        }
        else if (!Newer(Order, Pin->Order))
        {
            Error = TEXT("StaleOrder: an older order cannot replace the current order pin.");
            return false;
        }
    }
    if (!Bindings.ContainsByPredicate([&Handle, ObservationEpoch](const FBinding& Binding) { return Binding.Handle == Handle && Binding.Epoch == ObservationEpoch; }))
    {
        Error = TEXT("TargetUnavailable: an order can only pin a currently observed unit.");
        return false;
    }
    FOrderPin Next;
    Next.Order = Order;
    Next.Handle = Handle;
    Next.Epoch = ObservationEpoch;
    Pin = Next;
    Error.Reset();
    return true;
}

bool FLDObserverLedger::ReleaseOrder(const FLDMoveIdentity& Order, FString& Error)
{
    if (!Pin.IsSet() || Pin->Order == Order || Newer(Order, Pin->Order))
    {
        Pin.Reset();
        Error.Reset();
        return true;
    }
    Error = TEXT("StaleOrder: an older order cannot release the current order pin.");
    return false;
}

bool FLDObserverLedger::FindBinding(const FGuid& Handle, uint32 ObservationEpoch, FLDCombatBodyRef& Out) const
{
    const FBinding* Binding = Bindings.FindByPredicate([&Handle, ObservationEpoch](const FBinding& Candidate)
    { return Candidate.Handle == Handle && Candidate.Epoch == ObservationEpoch; });
    if (!Binding) { return false; }
    Out = Binding->Body;
    return true;
}

bool FLDObserverLedger::FindHandle(const FLDCombatBodyRef& Body, FGuid& OutHandle, uint32& OutEpoch) const
{
    const FBinding* Binding = Bindings.FindByPredicate([&Body](const FBinding& Candidate) { return Candidate.Body == Body; });
    if (!Binding) { return false; }
    OutHandle = Binding->Handle;
    OutEpoch = Binding->Epoch;
    return true;
}

bool FLDObserverLedger::IsBound(const FLDCombatBodyRef& Body) const
{
    return Bindings.ContainsByPredicate([&Body](const FBinding& Binding) { return Binding.Body == Body; });
}

const FLDObservedTerminationFact* FLDObserverLedger::FindTermination(const FGuid& Handle, uint32 ObservationEpoch) const
{
    return Terminations.FindByPredicate([&Handle, ObservationEpoch](const FLDObservedTerminationFact& Fact)
    { return Fact.Wire.Handle == Handle && Fact.Wire.ObservationEpoch == ObservationEpoch; });
}

void FLDObserverLedger::RecentTerminations(TArray<FLDObservedUnitTermination>& Out) const
{
    Out.Reset();
    for (const FLDObservedTerminationFact& Fact : Terminations) { Out.Add(Fact.Wire); }
}
namespace
{
bool ObserverHasLiveBinding(const FLDObserverLedger& Ledger, const FLDContenderLifeRead& Life)
{
    return Life.Phase == ELDLifePhase::Alive && Ledger.Phase() == ELDLifePhase::Alive && Ledger.ObserverLife() == Life.Life;
}
}

ALDVisionDirector::ALDVisionDirector()
{
    bReplicates = false;
    PrimaryActorTick.bCanEverTick = false;
}

void ALDVisionDirector::EndPlay(const EEndPlayReason::Type Reason)
{
    EndMatch();
    Super::EndPlay(Reason);
}

ALDGameMode* ALDVisionDirector::AuthorityMode() const
{
    auto* Core = GetWorld() ? GetWorld()->GetAuthGameMode<ALDGameMode>() : nullptr;
    return Core && Core->Vision.Get() == this ? Core : nullptr;
}

FVector ALDVisionDirector::FootOf(const ALDCharacter& Body)
{
    return Body.GetActorLocation() - FVector(0, 0, Body.GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
}

bool ALDVisionDirector::BeginMatch(const FLDMatchContext& InMatch, FString& Error)
{
    auto* Core = AuthorityMode();
    if (!HasAuthority() || !Core || bActive || !InMatch.IsValid() || InMatch != Core->GetMatchContext())
    {
        Error = TEXT("Observation can only begin once for the core's current match.");
        return false;
    }
    auto* Registry = GetWorld()->GetSubsystem<ULDMapRegistry>();
    if (!Registry || !Registry->Enabled() || !Registry->GetGeometry())
    {
        Error = TEXT("Observation requires the registered Demo world geometry.");
        return false;
    }
    TSharedPtr<const FLDDemoContentSnapshot> Loaded;
    if (!LDDemoEntry::LoadRuntimeContent(Loaded, Error) || !Loaded)
    {
        if (Error.IsEmpty()) { Error = TEXT("Observation requires the approved Demo content."); }
        return false;
    }
    FLDSightGeometry Prepared;
    if (!Prepared.Build(*Registry->GetGeometry(), Registry->Revision(), Loaded->Get().Parameters.VisionRadiusCm, Error)) { return false; }
    int64 CampMembers = 0;
    for (const FLDDemoCampPlacement& Camp : Loaded->Get().Camps)
    {
        const FLDDemoMonsterDefinition* Monster = nullptr;
        if (!Loaded->FindMonster(Camp.MonsterId, Monster, Error) || !Monster || Monster->CountPerCamp < 0)
        {
            if (Error.IsEmpty()) { Error = FString::Printf(TEXT("Camp %s has no approved member definition."), *Camp.AnchorId); }
            return false;
        }
        CampMembers += Monster->CountPerCamp;
    }
    const int64 Contenders = Core->ReadLobby().Members.Num();
    // An observer sees at most every other contender, every camp member and the Boss.
    if (Contenders < 1 || Contenders + CampMembers > LDObservation::MaxUnits)
    {
        Error = FString::Printf(TEXT("CapacityExceeded: %lld contenders, %lld camp members and the Boss exceed the %d-unit observation bound."),
            Contenders, CampMembers, LDObservation::MaxUnits);
        return false;
    }
    ActiveMatch = InMatch;
    Sight = MoveTemp(Prepared);
    Content = Loaded;
    Observers.Reset();
    NextSampleMs = 0;
    NextPublishMs = 0;
    bSampled = false;
    bActive = true;
    Error.Reset();
    return true;
}

void ALDVisionDirector::EndMatch()
{
    const bool bWorldAlive = GetWorld() && !GetWorld()->bIsTearingDown;
    for (FObserver& Observer : Observers)
    {
        if (auto* Relay = Observer.Relay.Get(); Relay && bWorldAlive) { Relay->Destroy(); }
    }
    Observers.Reset();
    Content.Reset();
    Sight = FLDSightGeometry();
    ActiveMatch = FLDMatchContext();
    bSampled = false;
    bActive = false;
}

bool ALDVisionDirector::RegisterObserver(const FGuid& ContenderId, ALDPlayerController* Controller, FString& Error)
{
    auto* Core = AuthorityMode();
    if (!bActive || !Core || !Controller || !ContenderId.IsValid() || Core->ContenderId(Controller) != ContenderId)
    {
        Error = TEXT("An observer must be the core-registered controller of this contender.");
        return false;
    }
    if (Observers.ContainsByPredicate([&ContenderId, Controller](const FObserver& Existing)
        { return Existing.ContenderId == ContenderId || Existing.Controller.Get() == Controller; }))
    {
        Error = TEXT("This contender or controller is already an observer.");
        return false;
    }
    FObserver Observer;
    Observer.ContenderId = ContenderId;
    Observer.Controller = Controller;
    if (!Observer.Ledger.Begin(ActiveMatch.Generation, Error)) { return false; }
    // Only a real local viewer or a remote connection presents a view. IsLocalController() cannot reliably tell
    // server-side opponents apart across net modes, so GetLocalPlayer()/GetNetConnection() decide; opponents read EnumerateVisibleUnits.
    if (Controller->GetLocalPlayer() || Controller->GetNetConnection())
    {
        FActorSpawnParameters Params;
        Params.Owner = Controller;
        Observer.Relay = GetWorld()->SpawnActor<ALDWorldViewRelay>(ALDWorldViewRelay::StaticClass(), FTransform::Identity, Params);
        if (!Observer.Relay.IsValid())
        {
            Error = TEXT("The owner-only observation relay could not be created.");
            return false;
        }
    }
    Observers.Add(MoveTemp(Observer));
    Error.Reset();
    return true;
}

void ALDVisionDirector::UnregisterObserver(const FGuid& ContenderId)
{
    for (int32 Index = Observers.Num() - 1; Index >= 0; --Index)
    {
        if (Observers[Index].ContenderId != ContenderId) { continue; }
        if (auto* Relay = Observers[Index].Relay.Get()) { Relay->Destroy(); }
        Observers.RemoveAt(Index);
    }
}

ALDVisionDirector::FObserver* ALDVisionDirector::FindObserver(const ALDPlayerController& Caller)
{
    return Observers.FindByPredicate([&Caller](const FObserver& Observer) { return Observer.Controller.Get() == &Caller; });
}

const ALDVisionDirector::FObserver* ALDVisionDirector::FindObserver(const ALDPlayerController& Caller) const
{
    return Observers.FindByPredicate([&Caller](const FObserver& Observer) { return Observer.Controller.Get() == &Caller; });
}

bool ALDVisionDirector::ReadObserver(const FObserver& Observer, FLDContenderLifeRead& Life, FVector& Foot, FString& Error) const
{
    Foot = FVector::ZeroVector;
    auto* Core = AuthorityMode();
    if (!Core || !Core->ReadContenderLife(ActiveMatch, Observer.ContenderId, Life, Error))
    {
        if (Error.IsEmpty()) { Error = TEXT("The core has no current life for this observer."); }
        return false;
    }
    if (Life.Phase != ELDLifePhase::Alive) { Error.Reset(); return true; }
    ALDCharacter* Body = nullptr;
    if (Core->ResolveBody(ActiveMatch, Life.Body, Body) != ELDCommitReject::None || !Body)
    {
        Error = TEXT("The observer's living body is not the current registered body.");
        return false;
    }
    Foot = FootOf(*Body);
    Error.Reset();
    return true;
}
bool ALDVisionDirector::GatherTargets(TArray<FLDObservedTarget>& Out, FString& Error) const
{
    Out.Reset();
    auto* Core = AuthorityMode();
    if (!Core || !Core->Combat || !Content)
    {
        Error = TEXT("Observation requires the core, the sole Combat body owner and approved content.");
        return false;
    }
    for (TActorIterator<ALDCharacter> It(GetWorld()); It; ++It)
    {
        ALDCharacter* Body = *It;
        FLDCombatBodyRef Ref;
        if (!Body || !Core->ReadBodyRef(*Body, Ref)) { continue; }
        const FLDBodyCombatState* State = Core->Combat->StateFor(Body);
        if (!State)
        {
            Error = TEXT("A registered body has no Combat resource owner.");
            return false;
        }
        FLDObservedPublicFacts Facts;
        if (!Core->ReadObservedPublicFacts(ActiveMatch, Ref, Facts, Error))
        {
            if (Error.IsEmpty()) { Error = TEXT("LD_OBSERVATION_FACTS_UNAVAILABLE"); }
            return false;
        }
        // Camps owns a member's public projection (identity and the untimed Returning cue). Every body the core
        // accepted above is a registered member, so a refusal is a real fault and publication fails closed.
        if (Ref.Victim.Kind == ELDVictimKind::CampMember
            && (!Core->Camps || !Core->Camps->AppendObservedFacts(ActiveMatch, Ref, Facts, Error)))
        {
            if (!Core->Camps || Error.IsEmpty()) { Error = TEXT("LD_OBSERVATION_CAMP_FACTS_UNAVAILABLE"); }
            return false;
        }
        FLDObservedTarget Target;
        Target.Body = Ref;
        Target.FootCm = FootOf(*Body);
        FLDObservedUnitView& Unit = Target.Presented;
        Unit.FeetCm = Target.FootCm;
        Unit.Facing = QuantizeObservedYaw(Body->GetActorRotation().Yaw);
        // Combat's cue began at an authority time no later than now; the wire carries only its age.
        const int64 NowMs = Core->GetAuthorityNowMs();
        if (Facts.ActionCue != ELDObservedActionCue::None && (Facts.ActionCueAtMs <= 0 || Facts.ActionCueAtMs > NowMs))
        {
            Error = TEXT("LD_OBSERVATION_ACTION_CUE_INVALID: a Combat action cue has no begin time at or before now.");
            return false;
        }
        if (Facts.ActionAbility != 0 && Facts.ActionCue != ELDObservedActionCue::Cast)
        {
            Error = TEXT("LD_OBSERVATION_ACTION_CUE_INVALID: a Combat cast source needs a Cast cue and an ability.");
            return false;
        }
        if (Facts.LastHitAtMs != 0 && (Facts.LastHitAtMs < 0 || Facts.LastHitAtMs > NowMs
            || NowMs - Facts.LastHitAtMs >= LDObservation::RecentHitWindowMs))
        {
            Error = TEXT("LD_OBSERVATION_ACTION_CUE_INVALID: a visible hit is outside its presentation window.");
            return false;
        }
        Unit.ActionCue = Facts.ActionCue;
        Unit.ActionAbility = Facts.ActionAbility;
        Unit.HitLook = Facts.HitLook;
        Unit.RecentHitAgePlusOneMs = Facts.LastHitAtMs > 0
            ? uint16(NowMs - Facts.LastHitAtMs + 1) : 0;
        Unit.ActionCueAgeMs = Facts.ActionCue == ELDObservedActionCue::None ? 0 : uint16(FMath::Min<int64>(NowMs - Facts.ActionCueAtMs, MAX_uint16));
        if (Facts.ActionCue != ELDObservedActionCue::None && Facts.ActionCue != ELDObservedActionCue::Hit)
        {
            if (Facts.ActionCueImpactAtMs < Facts.ActionCueAtMs || Facts.ActionCueImpactAtMs - Facts.ActionCueAtMs > MAX_uint16)
            {
                Error = TEXT("LD_OBSERVATION_ACTION_CUE_INVALID: action release timing is outside the visible action window.");
                return false;
            }
            Unit.ActionCueImpactDelayMs = uint16(Facts.ActionCueImpactAtMs - Facts.ActionCueAtMs);
        }
        else { Unit.ActionCueImpactDelayMs = 0; }
        Unit.Health.CurrentP2 = State->Resources.HpP2;
        Unit.Health.MaxP2 = State->Resources.MaxHpP2;
        Unit.Statuses = Facts.Statuses;
        if (Ref.Victim.Kind == ELDVictimKind::Contender)
        {
            Unit.Kind = ELDObservedUnitKind::Contender;
            Unit.AuthorizedFieldMask = LDObservation::ContenderFields;
            // Combat writes the locked roster seat; validation refuses anything outside the six seats.
            Unit.Presentation = Body->PresentationKey;
            Unit.ContenderId = Ref.Victim.EntityId;
            Unit.Level = Facts.Level;
            Unit.Mana.CurrentP2 = State->Resources.MpP2;
            Unit.Mana.MaxP2 = State->Resources.MaxMpP2;
            Unit.Actives = Facts.Actives;
        }
        else
        {
            const uint8 CreatureId = Facts.CreatureId;
            const FLDDemoMonsterDefinition* Monster = Content->Get().Monsters.FindByPredicate(
                [CreatureId](const FLDDemoMonsterDefinition& Candidate) { return Candidate.NativeId == CreatureId; });
            if (!Monster)
            {
                Error = TEXT("An observed camp member has no approved creature definition.");
                return false;
            }
            Unit.Kind = ELDObservedUnitKind::CampMember;
            Unit.AuthorizedFieldMask = LDObservation::CampFields;
            Unit.CreatureId = CreatureId;
            if (Monster->MaxMPP2 > 0)
            {
                Unit.AuthorizedFieldMask |= LDObservation::Mana;
                Unit.Mana.CurrentP2 = State->Resources.MpP2;
                Unit.Mana.MaxP2 = State->Resources.MaxMpP2;
            }
        }
        Out.Add(MoveTemp(Target));
    }
    Out.Sort([](const FLDObservedTarget& A, const FLDObservedTarget& B)
    {
        return A.Body.Victim.EntityId != B.Body.Victim.EntityId ? A.Body.Victim.EntityId < B.Body.Victim.EntityId
            : A.Body.Victim.Life < B.Body.Victim.Life;
    });
    Error.Reset();
    return true;
}

bool ALDVisionDirector::SampleObserver(FObserver& Observer, const TArray<FLDObservedTarget>& Targets,
    const TArray<FLDObservedProjectileFact>& Flights, bool bPublish, FString& Error)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(LDVisionSampleObserver);
    if (!Observer.Controller.IsValid())
    {
        Error = TEXT("An observer controller disappeared without leaving the observation registry.");
        return false;
    }
    FLDContenderLifeRead Life;
    FVector Foot;
    if (!ReadObserver(Observer, Life, Foot, Error)) { return false; }
    bool bReset = false;
    if (!Observer.Ledger.Bind(Life.Phase, Life.Life, bReset, Error)) { return false; }
    if (bReset) { Observer.Coverage.Reset(); }
    ALDWorldViewRelay* Relay = Observer.Relay.Get();
    if (bReset && Relay)
    {
        FLDObservedConceal ResetNotice;
        ResetNotice.MatchGeneration = ActiveMatch.Generation;
        ResetNotice.Generation = Observer.Ledger.Generation();
        if (!Observer.Ledger.AllocateRevision(ResetNotice.Revision, Error)) { return false; }
        Relay->ClientConceal(ResetNotice);
    }
    const bool bAlive = Life.Phase == ELDLifePhase::Alive;
    TArray<const FLDObservedTarget*> Visible;
    // #198 rest vision filter: contenders share the rest group, while the frozen field stays hidden.
    const ALDGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ALDGameMode>() : nullptr;
    const ALDCombatDirector* Combat = Mode ? Mode->Combat.Get() : nullptr;
    // #198 session D (approved §3/§6): spectators (and the eliminated) are explicitly allowed to watch every arena body;
    // a PK life additionally sees what its living teammates legally see, and an Exposed entrant for the exposure.
    const ALDPKDirector* PK = Mode ? Mode->PKDirector.Get() : nullptr;
    const bool bArena = PK && PK->IsArenaActive();
    const bool bSpectating = bAlive && bArena && PK->CanSpectate(Observer.ContenderId);
    const bool bPKObserver = bAlive && bArena && Mode->IsPKLife(Observer.ContenderId);
    TArray<FVector> TeammateFeet;
    if (bPKObserver)
    {
        TArray<FGuid> Mates;
        PK->LivingTeammates(Observer.ContenderId, Mates);
        for (const FGuid& Mate : Mates)
        {
            FLDCombatBodyRef MateBody;
            FVector MateFoot;
            if (Mode->ReadLivingBody(Mate, MateBody, MateFoot)) { TeammateFeet.Add(MateFoot); }
        }
    }
    const auto IsArenaBody = [&](const FLDObservedTarget& Target)
    {
        const FGuid& Id = Target.Body.Victim.EntityId;
        return bArena && Target.Body.Victim.Kind == ELDVictimKind::Contender && PK->SideOf(Id) != 0 && Mode->IsPKLife(Id);
    };
    const auto Resolve = [&](const FVector& From, const FLDObservedTarget& Target, bool& bOut)
    {
        const ELDSightResult Result = Sight.Trace(From, Target.FootCm);
        const ELDObservedResolve Outcome = LDObservation::ResolveFromSight(Result);
        if (Outcome == ELDObservedResolve::AuthorityFault)
        {
            Error = FString::Printf(TEXT("Observation sight failed for a registered body (%u)."), uint32(Result));
            return false;
        }
        bOut = Outcome == ELDObservedResolve::Resolved;
        return true;
    };
    if (bAlive)
    {
        for (const FLDObservedTarget& Target : Targets)
        {
            if (Target.Body.Victim.EntityId == Observer.ContenderId) { continue; }
            const bool bArenaBody = IsArenaBody(Target);
            if (bSpectating && bArenaBody)
            {
                Visible.Add(&Target);
                continue;
            }
            if (Combat && Combat->IsHiddenFrom(Observer.ContenderId, Target.Body)) { continue; }
            if (Mode->IsInRestSpace(Observer.ContenderId)
                && (Mode->IsInRestSpace(Target.Body.Victim.EntityId)
                    || Mode->IsTrainingDummy(Target.Body.Victim.EntityId)))
            {
                Visible.Add(&Target);
                continue;
            }
            bool bSeen = false;
            if (!Resolve(Foot, Target, bSeen)) { return false; }
            if (!bSeen && bPKObserver && bArenaBody)
            {
                bSeen = Combat && Combat->IsExposed(Target.Body, Mode->GetAuthorityNowMs());
                for (int32 Index = 0; !bSeen && Index < TeammateFeet.Num(); ++Index)
                {
                    if (!Resolve(TeammateFeet[Index], Target, bSeen)) { return false; }
                }
            }
            if (bSeen) { Visible.Add(&Target); }
        }
    }
    TArray<FGuid> Removed;
    TArray<FLDObservedUnitView> Units;
    if (!Observer.Ledger.Sample(Visible, Removed, Units, Error)) { return false; }
    // #198 R15: a skill effect has its source's body permissions: hidden across isolation groups, the whole arena for a
    // spectator, the living teammates' sight for a PK life; a flight that left sight or ended is concealed with the units.
    FGuid ObserverGroup;
    const bool bObserverGroup = Combat && Combat->ActingGroup(Observer.ContenderId, ObserverGroup);
    const FGuid ArenaGroup = bArena ? PK->GetGroup() : FGuid();
    const auto FlightAccess = [&](const FLDObservedProjectileFact& Fact)
    {
        if (!Combat) { return ELDObservedFlightAccess::Sight; }
        FGuid Group;
        const bool bSourceActs = Combat->ActingGroup(Fact.SourceEntity, Group);
        return LDObservation::ClassifyFlight(bSourceActs, Group, bObserverGroup, ObserverGroup, ArenaGroup, bSpectating, bPKObserver);
    };
    TArray<const FLDObservedProjectileFact*> VisibleFlights;
    if (bAlive && !LDObservation::SelectVisibleProjectiles(Sight, Foot, TeammateFeet, Flights, FlightAccess, VisibleFlights, Error))
    {
        return false;
    }
    TArray<FGuid> RemovedFlights;
    TArray<FLDObservedProjectileView> Projectiles;
    if (!Observer.Ledger.SampleProjectiles(VisibleFlights, RemovedFlights, Projectiles, Error)) { return false; }
    if (RemovedFlights.Num() > 0)
    {
        Removed.Append(RemovedFlights);
        Removed.Sort();
    }
    if (Removed.Num() > 0 && Relay)
    {
        FLDObservedConceal Conceal;
        Conceal.MatchGeneration = ActiveMatch.Generation;
        Conceal.Generation = Observer.Ledger.Generation();
        Conceal.Removed = MoveTemp(Removed);
        if (!Observer.Ledger.AllocateRevision(Conceal.Revision, Error)) { return false; }
        Relay->ClientConceal(Conceal);
    }
    FLDWorldView View;
    View.Schema = LDObservation::Schema;
    View.MatchGeneration = ActiveMatch.Generation;
    View.ObserverLife = Life.Life;
    View.Generation = Observer.Ledger.Generation();
    if (!Observer.Ledger.AllocateRevision(View.Revision, Error)) { return false; }
    View.WorldMapId = Sight.Revision().MapId;
    View.WorldLayoutRevision = Sight.Revision().LayoutRevision;
    View.WorldGeometryRevision = Sight.Revision().GeometryRevision;
    View.SampledAtServerSec = AuthorityMode()->GetAuthorityNowSeconds();
    if (bAlive)
    {
        View.Footprint.SourceStatus = ELDObservationSource::ActiveBody;
        View.Footprint.SourceFeetCm = Foot;
        View.Footprint.RadiusCm = float(Sight.Radius());
        Observer.Coverage.Sample(Sight, Foot, bPublish, Relay != nullptr, View.Footprint.CurrentBits);
        // #198 session D: a spectator's authorized coverage includes the whole arena; a PK life's includes its teammates'.
        if (bPublish && Relay && (bSpectating || !TeammateFeet.IsEmpty()))
        {
            if (bSpectating) { Sight.AddDisk(PK->GetView().ArenaCentreCm, PK->GetParameters().ArenaRadiusCm, View.Footprint.CurrentBits); }
            for (const FVector& MateFoot : TeammateFeet)
            {
                TArray<uint8> MateBits;
                Sight.Coverage(MateFoot, MateBits);
                for (int32 Index = 0; Index < MateBits.Num() && Index < View.Footprint.CurrentBits.Num(); ++Index)
                {
                    View.Footprint.CurrentBits[Index] |= MateBits[Index];
                }
            }
        }
        View.Units = MoveTemp(Units);
        View.Projectiles = MoveTemp(Projectiles);
        Observer.Ledger.RecentTerminations(View.RecentTerminations);
    }
    if (!LDObservation::ValidateView(View, Error)) { return false; }
    Observer.Latest = View;
    Observer.bHasLatest = true;
    if (bPublish && Relay) { Relay->ClientFullView(View); }
    return true;
}

bool ALDVisionDirector::PublishAuthority(const FLDMatchContext& InMatch, FString& Error)
{
    auto* Core = AuthorityMode();
    if (!bActive || !Core || InMatch != ActiveMatch || Core->GetMatchContext() != ActiveMatch)
    {
        Error = TEXT("Observation publication requires the current active match.");
        return false;
    }
    const auto* Registry = GetWorld()->GetSubsystem<ULDMapRegistry>();
    if (!Registry || Registry->Revision() != Sight.Revision())
    {
        Error = TEXT("StaleWorld: the registered world changed under observation.");
        return false;
    }
    const int64 NowMs = Core->GetAuthorityNowMs();
    if (bSampled && NowMs < NextSampleMs) { Error.Reset(); return true; }
    const bool bPublish = !bSampled || NowMs >= NextPublishMs;
    NextSampleMs = LDObservation::NextSampleDue(NowMs, NextSampleMs, SamplePeriodMs, bSampled);
    if (bPublish) { NextPublishMs = LDObservation::NextSampleDue(NowMs, NextPublishMs, PublishPeriodMs, bSampled); }
    bSampled = true;
    TRACE_CPUPROFILER_EVENT_SCOPE(LDVisionSample);
    TArray<FLDObservedTarget> Targets;
    if (!GatherTargets(Targets, Error)) { return false; }
    // Combat's flights after its last Effects advance; Vision only decides who may see them.
    TArray<FLDObservedProjectileFact> Flights;
    if (!Core->Combat->ReadObservedProjectiles(Flights, Error))
    {
        if (Error.IsEmpty()) { Error = TEXT("LD_OBSERVATION_PROJECTILES_UNAVAILABLE"); }
        return false;
    }
    for (FObserver& Observer : Observers)
    {
        if (!SampleObserver(Observer, Targets, Flights, bPublish, Error)) { return false; }
    }
    Error.Reset();
    return true;
}
bool ALDVisionDirector::RecordObservedDeath(const FLDGameplayWritePermit& Permit, const FLDCombatBodyRef& Victim,
    double OccurredAtSeconds, FString& Error)
{
    auto* Core = AuthorityMode();
    if (!bActive || !Core)
    {
        Error = TEXT("Observation is not active for this match.");
        return false;
    }
    const ELDCommitReject Permission = Core->ValidateWritePermit(Permit, ELDCommitPhase::DeathsAndRewards);
    if (Permission != ELDCommitReject::None || Permit.GetBoundary().Match != ActiveMatch)
    {
        Error = FString::Printf(TEXT("An observed death requires the current DeathsAndRewards permit (%u)."), uint32(Permission));
        return false;
    }
    if (!FMath::IsFinite(OccurredAtSeconds) || OccurredAtSeconds != Core->GetAuthorityNowSeconds())
    {
        Error = TEXT("An observed death is witnessed only at the core's current actual time; no historical pose evidence exists.");
        return false;
    }
    ALDCharacter* VictimBody = nullptr;
    if (Core->ResolveBody(ActiveMatch, Victim, VictimBody) != ELDCommitReject::None || !VictimBody)
    {
        Error = TEXT("An observed death requires the victim's current registered body.");
        return false;
    }
    int64 OccurredAtMs = 0;
    if (!LDMatchLifecycle::AuthoritySecondsToEventMs(OccurredAtSeconds, OccurredAtMs, Error)) { return false; }
    for (FObserver& Observer : Observers)
    {
        if (!Observer.Ledger.IsBound(Victim)) { continue; }
        FLDContenderLifeRead Life;
        FVector Foot;
        if (!ReadObserver(Observer, Life, Foot, Error)) { return false; }
        if (!ObserverHasLiveBinding(Observer.Ledger, Life)) { continue; }
        const ELDSightResult Result = CurrentObservation(Foot, *VictimBody);
        const ELDObservedResolve Witnessed = LDObservation::ResolveFromSight(Result);
        if (Witnessed == ELDObservedResolve::AuthorityFault)
        {
            Error = FString::Printf(TEXT("Observed death sight failed (%u)."), uint32(Result));
            return false;
        }
        if (Witnessed != ELDObservedResolve::Resolved) { continue; }
        FLDObservedTerminationFact Fact;
        const auto Outcome = Observer.Ledger.Terminate(Victim, ActiveMatch, OccurredAtSeconds, OccurredAtMs, Fact, Error);
        if (Outcome == FLDObserverLedger::ETerminate::Failed) { return false; }
        if (Outcome != FLDObserverLedger::ETerminate::Terminated) { continue; }
        if (ALDWorldViewRelay* Relay = Observer.Relay.Get())
        {
            FLDObservedTerminationNotice Notice;
            Notice.MatchGeneration = ActiveMatch.Generation;
            Notice.Termination = Fact.Wire;
            if (!Observer.Ledger.AllocateRevision(Notice.Revision, Error)) { return false; }
            Relay->ClientTermination(Notice);
        }
    }
    Error.Reset();
    return true;
}

ELDObservedResolve ALDVisionDirector::ResolveObservedUnit(const ALDPlayerController& RegisteredCaller, const FGuid& Handle,
    uint32 ObservationEpoch, ELDObservationPurpose Purpose, FLDAuthorityTargetSample& Out, FString* Diagnostic) const
{
    // Purpose never widens authorization: every purpose requires current sight; range rules stay with their owners.
    auto Refuse = [Diagnostic](ELDObservedResolve Result, const FString& Detail)
    {
        if (Diagnostic) { *Diagnostic = Detail; }
        return Result;
    };
    const FObserver* Observer = FindObserver(RegisteredCaller);
    auto* Core = AuthorityMode();
    if (!bActive || !Core || !Observer) { return Refuse(ELDObservedResolve::ObserverUnavailable, TEXT("The caller is not a registered observer.")); }
    FLDContenderLifeRead Life;
    FVector Foot;
    FString Error;
    if (!ReadObserver(*Observer, Life, Foot, Error)) { return Refuse(ELDObservedResolve::AuthorityFault, Error); }
    if (!ObserverHasLiveBinding(Observer->Ledger, Life))
    { return Refuse(ELDObservedResolve::ObserverUnavailable, TEXT("The observer has no living body bound to its current observation generation.")); }
    FLDCombatBodyRef Target;
    if (!Observer->Ledger.FindBinding(Handle, ObservationEpoch, Target))
    { return Refuse(ELDObservedResolve::TargetUnavailable, TEXT("No current observation interval matches this handle and epoch.")); }
    ALDCharacter* TargetBody = nullptr;
    if (Core->ResolveBody(ActiveMatch, Target, TargetBody) != ELDCommitReject::None || !TargetBody)
    { return Refuse(ELDObservedResolve::TargetUnavailable, TEXT("The observed life has no current registered body.")); }
    const FVector TargetFoot = FootOf(*TargetBody);
    const ELDSightResult Sighted = CurrentObservation(Foot, *TargetBody);
    const ELDObservedResolve Outcome = LDObservation::ResolveFromSight(Sighted);
    if (Outcome == ELDObservedResolve::AuthorityFault)
    { return Refuse(Outcome, FString::Printf(TEXT("Observation sight failed at this authority moment (%u)."), uint32(Sighted))); }
    if (Outcome != ELDObservedResolve::Resolved)
    { return Refuse(Outcome, TEXT("The target is not visible at this authority moment.")); }
    Out.Target = Target;
    Out.FootCm = TargetFoot;
    Out.World = Sight.Revision();
    Out.SampledAtSeconds = Core->GetAuthorityNowSeconds();
    if (Diagnostic) { Diagnostic->Reset(); }
    return ELDObservedResolve::Resolved;
}

bool ALDVisionDirector::EnumerateVisibleUnits(const ALDPlayerController& RegisteredCaller, ELDObservationPurpose Purpose,
    FLDWorldView& Out, FString& Error) const
{
    const FObserver* Observer = FindObserver(RegisteredCaller);
    if (!bActive || !Observer || !Observer->bHasLatest)
    {
        Error = TEXT("No authorized observation frame exists for this caller.");
        return false;
    }
    FLDContenderLifeRead Life;
    FVector Foot;
    if (!ReadObserver(*Observer, Life, Foot, Error)) { return false; }
    if (Life.Phase != Observer->Ledger.Phase() || Life.Life != Observer->Ledger.ObserverLife())
    {
        Error = TEXT("The observer changed after its latest frame; wait for the next authorized sample.");
        return false;
    }
    Out = Observer->Latest;
    Error.Reset();
    return true;
}

bool ALDVisionDirector::FindObservedTermination(const ALDPlayerController& RegisteredCaller, const FGuid& Handle,
    uint32 ObservationEpoch, FLDObservedTerminationFact& Out) const
{
    const FObserver* Observer = FindObserver(RegisteredCaller);
    const FLDObservedTerminationFact* Fact = bActive && Observer ? Observer->Ledger.FindTermination(Handle, ObservationEpoch) : nullptr;
    if (!Fact) { return false; }
    Out = *Fact;
    return true;
}

bool ALDVisionDirector::BindObservedOrder(const ALDPlayerController& Caller, const FLDMoveIdentity& Order, FGuid Handle,
    uint32 ObservationEpoch, const FLDGameplayWritePermit& Permit, FString& Error)
{
    auto* Core = AuthorityMode();
    FObserver* Observer = FindObserver(Caller);
    if (!bActive || !Core || !Observer)
    {
        Error = TEXT("ObserverUnavailable: the caller is not a registered observer.");
        return false;
    }
    const ELDCommitReject Permission = Core->ValidateWritePermit(Permit, ELDCommitPhase::Actions);
    if (Permission != ELDCommitReject::None || Permit.GetBoundary().Match != ActiveMatch)
    {
        Error = FString::Printf(TEXT("AuthorityFault: an order pin requires the current Actions permit (%u)."), uint32(Permission));
        return false;
    }
    FLDContenderLifeRead Life;
    FVector Foot;
    if (!ReadObserver(*Observer, Life, Foot, Error))
    {
        Error = TEXT("AuthorityFault: ") + Error;
        return false;
    }
    if (!ObserverHasLiveBinding(Observer->Ledger, Life))
    {
        Error = TEXT("ObserverUnavailable: the observer has no living body bound to its current observation generation.");
        return false;
    }
    FLDCombatBodyRef Target;
    ALDCharacter* TargetBody = nullptr;
    if (!Observer->Ledger.FindBinding(Handle, ObservationEpoch, Target)
        || Core->ResolveBody(ActiveMatch, Target, TargetBody) != ELDCommitReject::None || !TargetBody)
    {
        Error = TEXT("TargetUnavailable: an order can only pin a unit visible at this authority moment.");
        return false;
    }
    const ELDSightResult Sighted = CurrentObservation(Foot, *TargetBody);
    const ELDObservedResolve Outcome = LDObservation::ResolveFromSight(Sighted);
    if (Outcome != ELDObservedResolve::Resolved)
    {
        Error = Outcome == ELDObservedResolve::AuthorityFault
            ? FString::Printf(TEXT("AuthorityFault: observation sight failed at this authority moment (%u)."), uint32(Sighted))
            : FString(TEXT("TargetUnavailable: an order can only pin a unit visible at this authority moment."));
        return false;
    }
    return Observer->Ledger.BindOrder(Order, Handle, ObservationEpoch, Error);
}

bool ALDVisionDirector::ReleaseObservedOrder(const ALDPlayerController& Caller, const FLDMoveIdentity& Order, FString& Error)
{
    FObserver* Observer = FindObserver(Caller);
    if (!bActive || !Observer)
    {
        Error = TEXT("ObserverUnavailable: the caller is not a registered observer.");
        return false;
    }
    return Observer->Ledger.ReleaseOrder(Order, Error);
}

ELDSightResult ALDVisionDirector::TraceSight(const FVector& SourceFootCm, const FVector& TargetFootCm, const FLDWorldRevision& Expected) const
{
    if (!Sight.IsValid() || Expected != Sight.Revision()) { return ELDSightResult::StaleWorld; }
    return Sight.Trace(SourceFootCm, TargetFootCm);
}

ELDSightResult ALDVisionDirector::TraceObservation(const FVector& SourceFootCm, const ALDCharacter& Target,
    const FLDWorldRevision& Expected) const
{
    return TraceSight(SourceFootCm, FootOf(Target), Expected);
}

ELDSightResult ALDVisionDirector::CurrentObservation(const FVector& SourceFootCm, const ALDCharacter& Target) const
{
    const auto* Registry = GetWorld() ? GetWorld()->GetSubsystem<ULDMapRegistry>() : nullptr;
    return Registry ? TraceObservation(SourceFootCm, Target, Registry->Revision()) : ELDSightResult::StaleWorld;
}

bool ALDVisionDirector::FindObservedHandle(const ALDPlayerController& Caller, const FLDCombatBodyRef& Body, FGuid& OutHandle,
    uint32& OutEpoch) const
{
    const FObserver* Observer = FindObserver(Caller);
    return bActive && Observer && Observer->Ledger.FindHandle(Body, OutHandle, OutEpoch);
}
