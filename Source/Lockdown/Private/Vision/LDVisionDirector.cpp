#include "Vision/LDVisionDirector.h"
#include "Core/LDLog.h"
#include "World/LDSceneTags.h"
#include "Components/PrimitiveComponent.h"
#include "World/LDNativeGround.h"
#include "PhysicsEngine/BodyInstance.h"
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
#include "World/LDFieldEcology.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Engine/Brush.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Core/LDTrace.h"

THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END

FGuid LDObservation::ScopedHitId(const FGuid& Handle, const FGuid& HitId)
{
    if (!Handle.IsValid() || !HitId.IsValid()) { return FGuid(); }
    const uint32 Input[] = {Handle.A, Handle.B, Handle.C, Handle.D, HitId.A, HitId.B, HitId.C, HitId.D};
    uint8 Digest[SHA256_DIGEST_LENGTH];
    if (!SHA256(reinterpret_cast<const uint8*>(Input), sizeof(Input), Digest)) { return FGuid(); }
    FGuid Result;
    FMemory::Memcpy(&Result.A, Digest, sizeof(uint32));
    FMemory::Memcpy(&Result.B, Digest + 4, sizeof(uint32));
    FMemory::Memcpy(&Result.C, Digest + 8, sizeof(uint32));
    FMemory::Memcpy(&Result.D, Digest + 12, sizeof(uint32));
    return Result;
}

namespace
{
constexpr double SightCoordinateLimitCm = 1000000.0;
constexpr double SightSurfaceOffsetCm = 0.1;

void PKTeammateFeet(const ALDGameMode& Mode, const ALDPKDirector* PK, const FGuid& Observer, TArray<FVector>& Out)
{
    Out.Reset();
    if (!PK || !PK->IsArenaActive() || !Mode.IsPKLife(Observer)) { return; }
    TArray<FGuid> Mates;
    PK->LivingTeammates(Observer, Mates);
    for (const FGuid& Mate : Mates)
    {
        FLDCombatBodyRef Body;
        FVector Foot;
        if (Mode.ReadLivingBody(Mate, Body, Foot)) { Out.Add(Foot); }
    }
}

ELDObservedResolve PresentationSight(const ALDGameMode& Mode, const ALDCombatDirector* Combat, const ALDPKDirector* PK,
    const FGuid& Observer, bool bArena, bool bSpectating, bool bPKObserver, const FVector& Foot,
    const TArray<FVector>& Teammates, const FLDCombatBodyRef& Target, const FVector& TargetFoot,
    const FLDSightGeometry& Sight, FString& Error)
{
    const bool bArenaBody = bArena && Target.Victim.Kind == ELDVictimKind::Contender
        && PK->SideOf(Target.Victim.EntityId) != 0 && Mode.IsPKLife(Target.Victim.EntityId);
    if (bSpectating && bArenaBody) { return ELDObservedResolve::Resolved; }
    if (Combat && Combat->IsHiddenFrom(Observer, Target)) { return ELDObservedResolve::TargetUnavailable; }
    if (Mode.IsInRestSpace(Observer)
        && (Mode.IsInRestSpace(Target.Victim.EntityId) || Mode.IsTrainingDummy(Target.Victim.EntityId)))
    { return ELDObservedResolve::Resolved; }
    const auto Trace = [&](const FVector& From)
    {
        const ELDSightResult Result = Sight.Trace(From, TargetFoot);
        const ELDObservedResolve Outcome = LDObservation::ResolveFromSight(Result);
        if (Outcome == ELDObservedResolve::AuthorityFault)
        { Error = FString::Printf(TEXT("Observation sight failed at this authority moment (%u)."), uint32(Result)); }
        return Outcome;
    };
    ELDObservedResolve Outcome = Trace(Foot);
    if (Outcome != ELDObservedResolve::TargetUnavailable) { return Outcome; }
    if (bPKObserver && bArenaBody)
    {
        if (Combat && Combat->IsExposed(Target, Mode.GetAuthorityNowMs())) { return ELDObservedResolve::Resolved; }
        for (const FVector& MateFoot : Teammates)
        {
            Outcome = Trace(MateFoot);
            if (Outcome != ELDObservedResolve::TargetUnavailable) { return Outcome; }
        }
    }
    return ELDObservedResolve::TargetUnavailable;
}

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

bool SightBlockerLevel(double BaseZ, double Step, int32& Out)
{
    Out = Step > 0 ? FMath::RoundToInt32(BaseZ / Step) : -1;
    return Out >= 0 && Out <= LDDemoWorld::MaxLevel && FMath::IsNearlyEqual(Out * Step, BaseZ, 1e-6);
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
        && A.ActionCue == B.ActionCue && A.ActionAbility == B.ActionAbility && A.HitLook == B.HitLook && A.HitFlags == B.HitFlags
        && (A.RecentHitAgePlusOneMs != 0) == (B.RecentHitAgePlusOneMs != 0) && A.RecentReaction == B.RecentReaction
        && A.RecentHitId == B.RecentHitId
        && A.ContenderId == B.ContenderId && A.CreatureId == B.CreatureId && A.Level == B.Level
        && A.Health.CurrentP2 == B.Health.CurrentP2 && A.Health.MaxP2 == B.Health.MaxP2 && A.ShieldBar == B.ShieldBar
        && A.MomentumBar == B.MomentumBar && A.EmpowerReady == B.EmpowerReady
        && A.ActionComboStep == B.ActionComboStep
        && A.Actives.Abilities[0] == B.Actives.Abilities[0] && A.Actives.Abilities[1] == B.Actives.Abilities[1]
        && A.Actives.Abilities[2] == B.Actives.Abilities[2]
        && SameObservedStatuses(A.Statuses, B.Statuses);
}
}

bool FLDSightGeometry::Build(const FLDDemoWorldGeometry& Geometry, const FLDWorldRevision& InWorld,
    const FLDDemoRational& InRadius, const FLDDemoVisionRangeParameters& InRangeParameters,
    FString& Error, UWorld* NativeScene, const LDDemoWorld::FTerrain* NativeTerrain, bool bLazyCoverage)
{
    *this = FLDSightGeometry();
    double RadiusValue = 0, SampleHeight = 0;
    if (!InWorld.IsValid() || !SightRationalValue(InRadius, RadiusValue) || RadiusValue <= 0 || RadiusValue > SightCoordinateLimitCm)
    {
        Error = TEXT("Observation requires the approved finite positive VISION.RadiusCm and a valid world revision.");
        return false;
    }
    double Neutral = 0, Full = 0;
    if (!InRangeParameters.IsValid() || !SightRationalValue(InRangeParameters.NeutralHeightCm, Neutral)
        || !SightRationalValue(InRangeParameters.FullHeightCm, Full) || Neutral < 0 || Full <= Neutral
        || InRangeParameters.UphillRadiusBp <= 0 || InRangeParameters.UphillRadiusBp > 10000
        || InRangeParameters.DownhillRadiusBp < 10000
        || RadiusValue * double(InRangeParameters.DownhillRadiusBp) / 10000. > SightCoordinateLimitCm)
    {
        Error = TEXT("Observation requires approved ordered height thresholds and bounded uphill/downhill radius scales.");
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
    TArray<double> PreparedHeights;
    TArray<FCircle> PreparedCircles;
    for (const FLDDemoWorldBlocker& Blocker : Geometry.Blockers)
    {
        if (NativeScene) { break; } // Saved native collision supplies sight, including newly placed meshes.
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
            Circle.HeightCm = Height;
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
        PreparedHeights.Add(Height);
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
    this->RangeParameters = InRangeParameters;
    DownhillScale = double(InRangeParameters.DownhillRadiusBp) / 10000.;
    Blockers = MoveTemp(Prepared);
    BlockerLevels = MoveTemp(PreparedLevels);
    BlockerHeights = MoveTemp(PreparedHeights);
    Circles = MoveTemp(PreparedCircles);
    Terrain = MoveTemp(PreparedTerrain);
    Scene = NativeScene;
    NativePropQuery = FCollisionQueryParams(SCENE_QUERY_STAT(DemoNativeSight), false);
    NativeTerrainQuery = FCollisionQueryParams(SCENE_QUERY_STAT(DemoTerrainSight), false);
    NativeTerrainParts.Reset();
    NativePropQueries.Reset();
    NativeTerrainQueries.Reset();NativeTerrainExcludedParts.Reset();NativePropExcludedParts.Reset();
    SampleHeightCm = SampleHeight;
    if (NativeScene)
    {
        const auto* Registry = NativeScene->GetSubsystem<ULDMapRegistry>();
        if (NativeTerrain && NativeTerrain->IsValid()) { Terrain = *NativeTerrain; }
        else if (Registry && Registry->Enabled()) { Terrain = Registry->GetTerrain(); }
        else { Error = TEXT("Native sight requires bound terrain or an enabled map registry."); return false; }
        const FBox& NativeBounds = Terrain.NativeBounds();
        if (NativeBounds.IsValid)
        {
            Min.X = FMath::Min(Min.X, NativeBounds.Min.X); Min.Y = FMath::Min(Min.Y, NativeBounds.Min.Y);
            Max.X = FMath::Max(Max.X, NativeBounds.Max.X); Max.Y = FMath::Max(Max.Y, NativeBounds.Max.Y);
        }
        // Transparency affects occlusion only; the physical support cache keeps NoViewBlock floors.
        NativePropQuery.AddIgnoredActor(NativeScene->GetDefaultBrush());
        NativeTerrainQuery = LDNativeGround::Query(*NativeScene);
        // Terrain keeps its semantic tier, but its footprint comes only from the edited native body.
        // Ordinary obstacles use LDVision independently of Pawn/ground-aim collision.
        for (TActorIterator<AActor> It(NativeScene); It; ++It)
        {
            TInlineComponentArray<UPrimitiveComponent*> Parts(*It);
            for (auto* Part : Parts)
            {
                const auto* Body=Part->GetBodyInstance();
                const FExcludedPart Exclusion{Part,Body&&Body->IsValidBodyInstance()?Body->GetBodyBounds():Part->Bounds.GetBox()};
                if (LDSceneTags::NoCollision(*Part) || LDSceneTags::NoViewBlock(*Part))
                {
                    NativePropQuery.AddIgnoredComponent(Part);
                    NativeTerrainQuery.AddIgnoredComponent(Part);
                    NativeTerrainExcludedParts.Add(Exclusion);NativePropExcludedParts.Add(Exclusion);
                    continue;
                }
                if (!It->ActorHasTag(LDDemoWorld::TerrainTag) || !LDNativeGround::IsSupport(*Part))
                { NativeTerrainQuery.AddIgnoredComponent(Part); NativeTerrainExcludedParts.Add(Exclusion); continue; }
                NativeTerrainParts.Add(FObjectKey(Part));
                NativePropQuery.AddIgnoredComponent(Part);
                NativePropExcludedParts.Add(Exclusion);
            }
        }
        NativePropQuery.GetIgnoredComponents();
        NativeTerrainQuery.GetIgnoredComponents();
        SupportSurfaces.Build(*NativeScene);
    }
    CoverageWorldBounds=LDObservation::SquareCoverageBounds(FBox2D(Min,Max));
    BuildGrid(bLazyCoverage);
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

void FLDSightGeometry::BuildGrid(bool bLazyCoverage)
{
    GridWidth = FMath::Max(1, FMath::CeilToInt32((Max.X - Min.X) / GridCellCm));
    GridHeight = FMath::Max(1, FMath::CeilToInt32((Max.Y - Min.Y) / GridCellCm));
    CellPolygons.Empty(); CellPolygons.SetNum(GridWidth * GridHeight);
    CellCircles.Empty(); CellCircles.SetNum(GridWidth * GridHeight);
    CellRegions.Empty(); CellRegions.SetNum(GridWidth * GridHeight);
    int32 X0, Y0, X1, Y1;
    for (int32 Index = 0; !Scene.IsValid() && Index < Terrain.Regions.Num(); ++Index)
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
    CoverageGround.SetNum(LDObservation::CoverageCells);
    CachedCoverageColumns.Init(false, CoverageGround.Num());
    if (!bLazyCoverage)
    {
        for (int32 Index = 0; Index < CoverageGround.Num(); ++Index)
        { CacheCoverageGround(Index, CellCentre(Index)); }
    }
}

void FLDSightGeometry::CacheCoverageGround(int32 Index, const FVector2D& Centre) const
{
    CachedCoverageColumns[Index] = true;
    if (!Scene.IsValid())
    {
        double Z; FVector Normal; Terrain.Ground(Centre, Z, Normal);
        CoverageGround[Index].Add({FVector(Centre, Z), FHitResult()});
        return;
    }
    // Cache physical columns once per world revision, including separate floors under bridges/roofs.
    const FBox& Bounds = Terrain.NativeBounds();
    double StartZ = Bounds.Max.Z + 1.;
    while (StartZ > Bounds.Min.Z - 1.)
    {
        FHitResult Hit;
        if (!Terrain.TraceScene(FVector(Centre, StartZ), FVector(Centre, Bounds.Min.Z - 1.), Hit)) { break; }
        if (!Hit.bStartPenetrating && Hit.ImpactNormal.Z > .5)
        { CoverageGround[Index].Add({Hit.ImpactPoint, Hit}); }
        double NextZ = Hit.ImpactPoint.Z - SightSurfaceOffsetCm;
        if (Hit.bStartPenetrating)
        {
            const auto* Body = Hit.GetComponent() ? Hit.GetComponent()->GetBodyInstance() : nullptr;
            if (!Body || !Body->IsValidBodyInstance()) { break; }
            NextZ = Body->GetBodyBounds().Min.Z - SightSurfaceOffsetCm;
        }
        if (NextZ >= StartZ) { break; }
        StartZ = NextZ;
    }
}

bool FLDSightGeometry::InBounds(const FVector& FootCm) const
{
    return FMath::IsFinite(FootCm.X) && FMath::IsFinite(FootCm.Y) && FMath::IsFinite(FootCm.Z)
        && FootCm.X >= Min.X && FootCm.X <= Max.X && FootCm.Y >= Min.Y && FootCm.Y <= Max.Y;
}

double FLDSightGeometry::RadiusForHeights(double SourceGroundZ, double TargetGroundZ) const
{
    return RadiusCm * LDObservation::HeightRadiusScale(RangeParameters, SourceGroundZ, TargetGroundZ);
}

double LDObservation::HeightRadiusScale(const FLDDemoVisionRangeParameters& Parameters, double SourceGroundZ, double TargetGroundZ)
{
    const double NeutralHeightCm = double(Parameters.NeutralHeightCm.Numerator) / Parameters.NeutralHeightCm.Denominator;
    const double FullHeightCm = double(Parameters.FullHeightCm.Numerator) / Parameters.FullHeightCm.Denominator;
    const double Delta = TargetGroundZ - SourceGroundZ;
    const double Alpha = FMath::Clamp((FMath::Abs(Delta) - NeutralHeightCm) / (FullHeightCm - NeutralHeightCm), 0., 1.);
    const double Smooth = Alpha * Alpha * (3. - 2. * Alpha);
    const double Limit = double(Delta > 0 ? Parameters.UphillRadiusBp : Parameters.DownhillRadiusBp) / 10000.;
    return FMath::Lerp(1., Limit, Smooth);
}

namespace
{
bool SightClipPolygon(const FVector2D& From, const FVector2D& To, const TArray<FVector2D>& Polygon, double& Lo, double& Hi)
{
    Lo = 0.; Hi = 1.;
    for (int32 I = 0; I < Polygon.Num(); ++I)
    {
        const FVector2D Edge = Polygon[(I + 1) % Polygon.Num()] - Polygon[I];
        const double C = FVector2D::CrossProduct(Edge, From - Polygon[I]);
        const double D = FVector2D::CrossProduct(Edge, To - From);
        if (FMath::Abs(D) < UE_DOUBLE_SMALL_NUMBER) { if (C < 0) { return false; } }
        else if (D > 0) { Lo = FMath::Max(Lo, -C / D); }
        else { Hi = FMath::Min(Hi, -C / D); }
    }
    return Lo <= Hi;
}

bool SightHeightOverlap(double Start, double End, double Lo, double Hi, double Base, double Top)
{
    const double A = FMath::Lerp(Start, End, Lo), B = FMath::Lerp(Start, End, Hi);
    return FMath::Max(A, B) >= Base && FMath::Min(A, B) <= Top;
}
}

void FLDSightGeometry::PrepareNativeScope(const FVector2D& From) const
{
    const FIntPoint Cell(FMath::FloorToInt32(From.X/GridCellCm),FMath::FloorToInt32(From.Y/GridCellCm));
    if(bHasNativeScope&&Cell==NativeScopeCell){return;}
    const double Started=FPlatformTime::Seconds();++Diagnostics.Scopes;
    NativeScopeCell=Cell;bHasNativeScope=true;
    const double Reach=MaximumRadius()+SightSurfaceOffsetCm;
    const FBox2D Bounds(FVector2D(Cell.X*GridCellCm-Reach,Cell.Y*GridCellCm-Reach),
        FVector2D((Cell.X+1)*GridCellCm+Reach,(Cell.Y+1)*GridCellCm+Reach));
    LocalTerrainQuery=NativeTerrainQuery;LocalTerrainQuery.ClearIgnoredComponents();
    LocalPropQuery=NativePropQuery;LocalPropQuery.ClearIgnoredComponents();
    LocalTerrainExcludedParts.Reset();NativeTerrainQueries.Reset();NativePropQueries.Reset();
    // Every allowed ray lies inside this source-cell envelope. Outside components cannot intersect it;
    // excluding only nearby components keeps native physics filters small without changing occlusion.
    const auto InScope=[&](const FExcludedPart& Part)
    {return Part.Bounds.IsValid&&Bounds.Intersect(FBox2D(FVector2D(Part.Bounds.Min),FVector2D(Part.Bounds.Max)));};
    for(const auto& Part:NativeTerrainExcludedParts)
    {
        if(!InScope(Part)){continue;}
        if(auto* Component=Part.Component.Get())
        {LocalTerrainQuery.AddIgnoredComponent(Component);LocalTerrainExcludedParts.Add(Component);}
    }
    for(const auto& Part:NativePropExcludedParts)
    {if(InScope(Part)){if(auto* Component=Part.Component.Get()){LocalPropQuery.AddIgnoredComponent(Component);}}}
    LocalTerrainQuery.GetIgnoredComponents();LocalPropQuery.GetIgnoredComponents();
    Diagnostics.ScopeSeconds+=FPlatformTime::Seconds()-Started;
}

bool FLDSightGeometry::SegmentBlocked(const FVector& Source, const FVector& Target,
    const FHitResult& SourceSupport, const FHitResult& TargetSupport, double DistanceFractionSquared) const
{
    const FVector2D From(Source), To(Target);
    const FVector Eye = Source + FVector(0, 0, SampleHeightCm);
    // A grazing view of a higher platform opens near its lip first. Require progressively
    // more terrain clearance for farther targets while keeping the real eye for prop occlusion.
    const double Rise = FMath::Clamp(Target.Z - Source.Z, 0., SampleHeightCm);
    const FVector TerrainEye = Eye - FVector(0, 0, .5 * Rise * DistanceFractionSquared);
    const FVector GroundEnd = Target + FVector(0, 0, SightSurfaceOffsetCm);
    // This raised guard rejects intervening high terrain; the real ray below exempts only the departure plane.
    const FVector TerrainEnd(To, FMath::Max(Source.Z, Target.Z) + SightSurfaceOffsetCm);
    const FVector2D Low(FMath::Min(From.X, To.X), FMath::Min(From.Y, To.Y));
    const FVector2D High(FMath::Max(From.X, To.X), FMath::Max(From.Y, To.Y));
    int32 X0, Y0, X1, Y1;
    CellRange(Low, High, X0, Y0, X1, Y1);
    if (const UWorld* Native = Scene.Get())
    {
        PrepareNativeScope(From);
        const bool bDownhill=Target.Z<Source.Z-SightSurfaceOffsetCm;
        auto* SourcePart=SourceSupport.GetComponent();auto* TargetPart=TargetSupport.GetComponent();
        const auto ExtraPart=[&](UPrimitiveComponent* Part)
        {return Part&&!NativeTerrainParts.Contains(FObjectKey(Part))&&!LDSceneTags::NoViewBlock(*Part)&&!LDSceneTags::NoCollision(*Part);};
        const bool bExtraSource=ExtraPart(SourcePart),bExtraTarget=ExtraPart(TargetPart);
        const FCollisionQueryParams* TerrainQuery=&LocalTerrainQuery;
        if(bExtraSource||bExtraTarget)
        {
            const TPair<FObjectKey,FObjectKey> Key(bExtraSource?FObjectKey(SourcePart):FObjectKey(),bExtraTarget?FObjectKey(TargetPart):FObjectKey());
            auto* Prepared=NativeTerrainQueries.Find(Key);
            if(!Prepared)
            {
                const double PreparationStarted=FPlatformTime::Seconds();++Diagnostics.TerrainBuilds;
                if(NativeTerrainQueries.Num()>=256){NativeTerrainQueries.Reset();}
                Prepared=&NativeTerrainQueries.Add(Key,LocalTerrainQuery);Prepared->ClearIgnoredComponents();
                for(const auto& Weak:LocalTerrainExcludedParts)
                {
                    auto* Part=Weak.Get();if(!Part||(bExtraSource&&Part==SourcePart)||(bExtraTarget&&Part==TargetPart)){continue;}
                    Prepared->AddIgnoredComponent(Part);
                }
                Prepared->GetIgnoredComponents();
                Diagnostics.TerrainBuildSeconds+=FPlatformTime::Seconds()-PreparationStarted;
            }
            TerrainQuery=Prepared;
        }
        const auto TraceTerrain = [&](FHitResult& Out, const FVector& Start, const FVector& End)
        {
            const FCollisionQueryParams* Query = TerrainQuery;
            TOptional<FCollisionQueryParams> Filtered;
            while (LDNativeGround::Trace(*Native, Out, Start, End, *Query))
            {
                auto* Part = Out.GetComponent();
                if (!Part) { return true; }
                const bool bTerrain = NativeTerrainParts.Contains(FObjectKey(Part))
                    || Part == SourcePart || Part == TargetPart
                    || (Part->GetOwner() && Part->GetOwner()->ActorHasTag(LDDemoWorld::TerrainTag));
                if (bTerrain && !LDSceneTags::NoViewBlock(*Part) && !LDSceneTags::NoCollision(*Part))
                {
                    return true;
                }
                // Physics-only props can spawn after the scene cache, e.g. a capture fence. They remain
                // movement obstacles; only LDVision may make them sight obstacles. Never cache their visibility.
                if (!Filtered.IsSet()) { Filtered = *TerrainQuery; }
                Filtered->AddIgnoredComponent(Part);
                Query = &Filtered.GetValue();
            }
            return false;
        };
        const auto ClearsRampGrazing = [&](const FHitResult& BlockingHit)
        {
            // A walkable incline can meet the low ground ray long before the viewer reaches the crest.
            // Use a higher terrain sample nearby, falling continuously to the foot at the range edge.
            // Only this first, grazing hit is eligible. The alternate ray still tests every solid surface.
            const double NormalZ=BlockingHit.ImpactNormal.Z;
            const double WalkableZ=GetDefault<ALDCharacter>()->GetCharacterMovement()->GetWalkableFloorZ();
            if (BlockingHit.bStartPenetrating || NormalZ<WalkableZ || NormalZ>=.9995) { return false; }
            const auto* Ramp=BlockingHit.GetComponent();
            const auto SharesRampPlane = [&](const FHitResult& Support, const FVector& Foot)
            {
                return Ramp && Ramp==Support.GetComponent()
                    && BlockingHit.ImpactNormal.Equals(Support.ImpactNormal,.01)
                    && FMath::Abs(FVector::DotProduct(BlockingHit.ImpactPoint-Foot,BlockingHit.ImpactNormal))<=2.;
            };
            const bool bOnRamp=SharesRampPlane(SourceSupport,Source)||SharesRampPlane(TargetSupport,Target);
            const bool bLowShoulder=FMath::Abs(Target.Z-Source.Z)<=SampleHeightCm
                && BlockingHit.ImpactPoint.Z<=FMath::Max(Source.Z,Target.Z)+SampleHeightCm*.5;
            if (!bOnRamp && !bLowShoulder) { return false; }
            const double Lift=SampleHeightCm*(1.-DistanceFractionSquared);
            if (Lift<=SightSurfaceOffsetCm) { return false; }
            FHitResult RaisedBlocker;
            return !TraceTerrain(RaisedBlocker,TerrainEye,GroundEnd+FVector(0,0,Lift));
        };
        FHitResult Hit;
        // A solid deck/roof above the destination is never exempted as a departure edge.
        if(bDownhill&&TraceTerrain(Hit,FVector(To,Source.Z+SightSurfaceOffsetCm),GroundEnd)){return true;}
        if(TraceTerrain(Hit,TerrainEye,TerrainEnd)&&!ClearsRampGrazing(Hit)){return true;}
        if(bDownhill)
        {
            FHitResult First;
            const bool bHit=TraceTerrain(First,Eye,GroundEnd);
            const FVector Direction=GroundEnd-Eye;
            const auto Fraction=[&](const FHitResult& H){return FVector::DotProduct(H.ImpactPoint-Eye,Direction)/Direction.SizeSquared();};
            if(bHit)
            {
                // Follow the observer's contiguous support to its edge. A descending walkable ramp joined there
                // can continue that support, including where the capsule crosses from the ramp to a separate mesh.
                const bool bDeparturePlane=SourceSupport.GetComponent()&&First.ImpactNormal.Equals(SourceSupport.ImpactNormal,.001)
                    &&FMath::Abs(FVector::DotProduct(First.ImpactPoint-Source,SourceSupport.ImpactNormal))<=.15;
                const auto DepartureExit = [&](const FVector& Start,const FHitResult& Support)
                {
                    const double Started=FPlatformTime::Seconds(); ++Diagnostics.DepartureCalls;
                    const double Exit=SupportSurfaces.DepartureExit(Start,Target,Support);
                    Diagnostics.DepartureSeconds+=FPlatformTime::Seconds()-Started;
                    return Exit;
                };
                double Exit=0.;
                if (bDeparturePlane) { Exit=DepartureExit(Source,SourceSupport); }
                else if (SourceSupport.GetComponent() && !First.bStartPenetrating)
                {
                    const auto& Walk=*GetDefault<ALDCharacter>()->GetCharacterMovement();
                    const FVector& Normal=First.ImpactNormal;
                    const FVector2D Across=To-From;
                    if (Normal.Z>=Walk.GetWalkableFloorZ() && Normal.Z<.9995 && FVector2D::DotProduct(FVector2D(Normal),Across)>0.)
                    {
                        const double AcrossLength=Across.Size();
                        double Edge=DepartureExit(Source,SourceSupport);
                        auto* Ramp=First.GetComponent();
                        FVector SupportPoint=Source;
                        FVector SupportNormal=SourceSupport.ImpactNormal;
                        // Scaled ramps can have a short bevel at the landing. Follow each descending
                        // support face on the hit ramp instead of extrapolating a single plane past it.
                        constexpr double SeamProbeCm=2.;
                        constexpr int32 MaxSeamFaces=8;
                        const FCollisionQueryParams SeamQuery(SCENE_QUERY_STAT(DemoRampSeamSupport),false);
                        for (int32 Face=0; Ramp && Face<MaxSeamFaces && Edge<1.; ++Face)
                        {
                            // Probe across the seam even when the ray travels almost along it.
                            const double AcrossSlope=FVector2D::DotProduct(FVector2D(Normal).GetSafeNormal(),Across/AcrossLength);
                            const double SeamTravel=SeamProbeCm/FMath::Max(.1,AcrossSlope);
                            const double Join=FMath::Min(1.,Edge+SeamTravel/AcrossLength);
                            const FVector2D Seam=FMath::Lerp(From,To,Join);
                            const double Height=SupportPoint.Z-FVector2D::DotProduct(Seam-FVector2D(SupportPoint),
                                FVector2D(SupportNormal))/SupportNormal.Z;
                            const double MaxHeightDelta=Face==0?Walk.MaxStepHeight:SeamProbeCm;
                            FHitResult RampSupport;
                            if (!Ramp->LineTraceComponent(RampSupport,
                                FVector(Seam,Height+MaxHeightDelta+SightSurfaceOffsetCm),
                                FVector(Seam,Height-MaxHeightDelta-SightSurfaceOffsetCm),SeamQuery)
                                || RampSupport.bStartPenetrating || RampSupport.ImpactNormal.Z<Walk.GetWalkableFloorZ()
                                || FVector2D::DotProduct(FVector2D(RampSupport.ImpactNormal),Across)<=0.
                                || FMath::Abs(RampSupport.ImpactPoint.Z-Height)>MaxHeightDelta)
                            {
                                break;
                            }
                            const double SupportExit=DepartureExit(RampSupport.ImpactPoint,RampSupport);
                            if (SupportExit<=0.) { break; }
                            Edge=FMath::Lerp(Join,1.,SupportExit);
                            Exit=Edge;
                            SupportPoint=RampSupport.ImpactPoint;
                            SupportNormal=RampSupport.ImpactNormal;
                        }
                    }
                }
                if (Exit+.000001<Fraction(First))
                {
                    if (bDeparturePlane || !ClearsRampGrazing(First)) { return true; }
                }
                else if (Exit<1.)
                {
                    const FVector Start=FMath::Lerp(Eye,GroundEnd,Exit)+Direction.GetSafeNormal()*SightSurfaceOffsetCm;
                    if(TraceTerrain(Hit,FVector(FVector2D(Start),Source.Z+SightSurfaceOffsetCm),Start)){return true;}
                    if(TraceTerrain(Hit,Start,GroundEnd)&&!ClearsRampGrazing(Hit)){return true;}
                }
            }
        }
        const auto NeedsIgnore = [&](UPrimitiveComponent* Part)
        {
            return Part && !NativeTerrainParts.Contains(FObjectKey(Part))
                && Part->GetCollisionResponseToChannel(ECC_GameTraceChannel2)==ECR_Block
                && !LDSceneTags::NoViewBlock(*Part) && !LDSceneTags::NoCollision(*Part);
        };
        const bool bIgnoreSource=NeedsIgnore(SourcePart),bIgnoreTarget=NeedsIgnore(TargetPart);
        // Reuse the already-normalized ignore list. Adding duplicate terrain ids per cell re-sorts a large list.
        if (!bIgnoreSource && !bIgnoreTarget)
        { return Native->LineTraceTestByChannel(Eye,GroundEnd,ECC_GameTraceChannel2,LocalPropQuery); }
        const TPair<FObjectKey,FObjectKey> Key(bIgnoreSource?FObjectKey(SourcePart):FObjectKey(),
            bIgnoreTarget?FObjectKey(TargetPart):FObjectKey());
        auto* Query=NativePropQueries.Find(Key);
        if(!Query)
        {
            const double PreparationStarted=FPlatformTime::Seconds();++Diagnostics.PropBuilds;
            // Bound geometry-filter memory; no visibility result or actor state is cached here.
            if(NativePropQueries.Num()>=256){NativePropQueries.Reset();}
            Query=&NativePropQueries.Add(Key,LocalPropQuery);
            if(bIgnoreSource){Query->AddIgnoredComponent(SourcePart);}
            if(bIgnoreTarget){Query->AddIgnoredComponent(TargetPart);}
            Query->GetIgnoredComponents();
            Diagnostics.PropBuildSeconds+=FPlatformTime::Seconds()-PreparationStarted;
        }
        return Native->LineTraceTestByChannel(Eye, GroundEnd, ECC_GameTraceChannel2, *Query);
    }

    double Departure=0;
    if(Target.Z<Source.Z)
    {
        double Z;FVector Normal;Terrain.Ground(From,Z,Normal);
        TArray<FVector2D,TInlineAllocator<8>> Intervals;
        for(const auto& Region:Terrain.Regions)
        {
            const FVector2D Up(-Region.Along.Y,Region.Along.X);
            const double Gradient=Region.Rise/Region.Width;
            const FVector N=FVector(-Up.X*Gradient,-Up.Y*Gradient,1).GetSafeNormal();
            const double At=Region.Base+Gradient*FVector2D::CrossProduct(Region.Along,From-Region.Origin);
            double Lo,Hi;
            if(N.Equals(Normal,.001)&&FMath::Abs(At-Source.Z)<.15&&SightClipPolygon(From,To,Region.Polygon,Lo,Hi))
            {Intervals.Add(FVector2D(Lo,Hi));}
        }
        Intervals.Sort([](const FVector2D& L,const FVector2D& R){return L.X<R.X;});
        const double Tolerance=.15/FMath::Max(.001,FVector2D::Distance(From,To));
        for(const auto& Interval:Intervals)
        {if(Interval.X>Departure+Tolerance){break;}Departure=FMath::Max(Departure,Interval.Y);}
    }
    TBitArray<TInlineAllocator<4>> Tested(false, Terrain.Regions.Num());
    for (int32 Y = Y0; Y <= Y1; ++Y) { for (int32 X = X0; X <= X1; ++X)
    {
        for (const int32 I : CellRegions[Y * GridWidth + X])
        {
            if (Tested[I]) { continue; } Tested[I] = true;
            const auto& Region = Terrain.Regions[I];
            double Lo, Hi;
            if (!SightClipPolygon(From, To, Region.Polygon, Lo, Hi)) { continue; }
            const auto Height = [&](double T)
            {
                return Region.Base + Region.Rise * FMath::Clamp(
                    FVector2D::CrossProduct(Region.Along, FMath::Lerp(From, To, T) - Region.Origin), 0., Region.Width) / Region.Width;
            };
            if (Height(Lo) > FMath::Lerp(TerrainEye.Z, TerrainEnd.Z, Lo)
                || Height(Hi) > FMath::Lerp(TerrainEye.Z, TerrainEnd.Z, Hi)) { return true; }
            if(Target.Z<Source.Z&&Hi>Departure)
            {
                const double Begin=FMath::Max(Lo,Departure+.000001);
                if(Begin<=Hi&&(Height(Begin)>FMath::Lerp(Eye.Z,GroundEnd.Z,Begin)
                    ||Height(Hi)>FMath::Lerp(Eye.Z,GroundEnd.Z,Hi))){return true;}
            }
        }
        for (const int32 I : CellPolygons[Y * GridWidth + X])
        {
            double Lo, Hi;
            if (SightClipPolygon(From, To, Blockers[I], Lo, Hi)
                && SightHeightOverlap(Eye.Z, GroundEnd.Z, Lo, Hi, BlockerLevels[I] * Terrain.StepCm,
                    BlockerLevels[I] * Terrain.StepCm + BlockerHeights[I])) { return true; }
        }
    } }
    const FVector2D Reach(MaxCircleRadius, MaxCircleRadius);
    CellRange(Low - Reach, High + Reach, X0, Y0, X1, Y1);
    for (int32 Y = Y0; Y <= Y1; ++Y) { for (int32 X = X0; X <= X1; ++X)
    {
        for (const int32 I : CellCircles[Y * GridWidth + X])
        {
            const auto& Circle = Circles[I];
            const FVector2D D = To - From, P = From - Circle.Centre;
            const double A = D.SizeSquared(), B = FVector2D::DotProduct(P, D);
            const double C = P.SizeSquared() - Circle.Radius * Circle.Radius;
            const double Disc = B * B - A * C;
            if (A <= UE_DOUBLE_SMALL_NUMBER)
            { if (C <= 0 && SightHeightOverlap(Eye.Z, GroundEnd.Z, 0, 1, Circle.Level * Terrain.StepCm,
                Circle.Level * Terrain.StepCm + Circle.HeightCm)) { return true; } continue; }
            if (Disc < 0) { continue; }
            const double Root = FMath::Sqrt(Disc);
            const double Lo = FMath::Max(0., (-B - Root) / A), Hi = FMath::Min(1., (-B + Root) / A);
            if (Lo <= Hi && SightHeightOverlap(Eye.Z, GroundEnd.Z, Lo, Hi, Circle.Level * Terrain.StepCm,
                Circle.Level * Terrain.StepCm + Circle.HeightCm)) { return true; }
        }
    } }
    return false;
}

double FLDSightGeometry::GroundZ(const FVector& Foot) const
{
    FVector Ground;
    FHitResult Support;
    ResolveFoot(Foot, Ground, Support);
    return Ground.Z;
}

void FLDSightGeometry::ResolveFoot(const FVector& Foot, FVector& Ground, FHitResult& Support) const
{
    if (const FGroundSample* Cached = FootGround.Find(Foot)) { Ground = Cached->Foot; Support = Cached->Support; return; }
    Ground = Foot;
    int32 IgnoredTier;
    double Z;
    if (Terrain.GroundNear(Foot, Z, IgnoredTier, false, &Support)
        || Terrain.GroundNear(Foot, Z, IgnoredTier, true, &Support)) { Ground.Z = Z; }
    // Moving bodies add a foot per step; the bound keeps the map small and standing bodies re-resolve once after a reset.
    if (FootGround.Num() >= 1024) { FootGround.Reset(); }
    FootGround.Add(Foot, {Ground, Support});
}

ELDSightResult FLDSightGeometry::TracePrepared(const FVector& From, const FVector& To,
    const FHitResult& SourceSupport, const FHitResult& TargetSupport, double FixedRangeCm) const
{
    const double RangeSquared = FMath::Square(FixedRangeCm > 0. ? FixedRangeCm : RadiusForHeights(From.Z, To.Z));
    const double DistanceSquared = FVector::DistSquared2D(From, To);
    if (DistanceSquared > RangeSquared) { return ELDSightResult::OutOfRange; }
    return SegmentBlocked(From, To, SourceSupport, TargetSupport, DistanceSquared / RangeSquared)
        ? ELDSightResult::Blocked : ELDSightResult::Visible;
}

ELDSightResult FLDSightGeometry::Trace(const FVector& SourceFootCm, const FVector& TargetFootCm, double FixedRangeCm) const
{
    if (!IsValid()) { return ELDSightResult::StaleWorld; }
    if (!InBounds(SourceFootCm) || !InBounds(TargetFootCm)) { return ELDSightResult::InvalidPoint; }
    if (FVector::DistSquared2D(SourceFootCm, TargetFootCm) > FMath::Square(FixedRangeCm > 0. ? FixedRangeCm : MaximumRadius())) { return ELDSightResult::OutOfRange; }
    FVector From, To; FHitResult SourceSupport, TargetSupport;
    ResolveFoot(SourceFootCm, From, SourceSupport); ResolveFoot(TargetFootCm, To, TargetSupport);
    return TracePrepared(From, To, SourceSupport, TargetSupport, FixedRangeCm);
}

FVector2D FLDSightGeometry::CellCentre(int32 Index) const
{
    const int32 Width = LDObservation::CoverageWidth, Height = LDObservation::CoverageHeight;
    const FVector2D Span=CoverageWorldBounds.GetSize();
    return CoverageWorldBounds.Min+FVector2D(((Index % Width)+.5)*Span.X/Width,((Index / Width)+.5)*Span.Y/Height);
}

bool FLDSightGeometry::BeginCoverage(const FVector& SourceFootCm, FCoverageScan& Scan) const
{
    Scan.Bits.Init(0, LDObservation::CoverageBytes);
    if (!IsValid() || !InBounds(SourceFootCm)) { return false; }
    const int32 Width = LDObservation::CoverageWidth, Height = LDObservation::CoverageHeight;
    const FVector2D Span=CoverageWorldBounds.GetSize(), GridMin=CoverageWorldBounds.Min;
    const double CellX=Span.X/Width, CellY=Span.Y/Height;
    const double Reach = MaximumRadius();
    const int32 X0 = FMath::Clamp(FMath::FloorToInt32((SourceFootCm.X - Reach - GridMin.X) / CellX), 0, Width - 1);
    const int32 X1 = FMath::Clamp(FMath::FloorToInt32((SourceFootCm.X + Reach - GridMin.X) / CellX), 0, Width - 1);
    const int32 Y0 = FMath::Clamp(FMath::FloorToInt32((SourceFootCm.Y - Reach - GridMin.Y) / CellY), 0, Height - 1);
    const int32 Y1 = FMath::Clamp(FMath::FloorToInt32((SourceFootCm.Y + Reach - GridMin.Y) / CellY), 0, Height - 1);
    Scan.Region = FIntRect(X0, Y0, X1 + 1, Y1 + 1);
    Scan.Next = Scan.Region.Min;
    ResolveFoot(SourceFootCm, Scan.Ground, Scan.Support);
    return true;
}

bool FLDSightGeometry::ContinueCoverage(FCoverageScan& Scan, double DeadlineSeconds) const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(LDVisionCoverage);
    const double ReachSquared = FMath::Square(MaximumRadius());
    int32 Visited = 0;
    while (Scan.Next.Y < Scan.Region.Max.Y)
    {
        if (DeadlineSeconds > 0. && Visited > 0 && (Visited % 16) == 0
            && FPlatformTime::Seconds() >= DeadlineSeconds) { return false; }
        ++Visited;
        const int32 Index = Scan.Next.Y * LDObservation::CoverageWidth + Scan.Next.X;
        if (++Scan.Next.X == Scan.Region.Max.X)
        { Scan.Next.X = Scan.Region.Min.X; ++Scan.Next.Y; }
        const FVector2D Centre = CellCentre(Index);
        if (FVector2D::DistSquared(FVector2D(Scan.Ground), Centre) > ReachSquared) { continue; }
        if (!CachedCoverageColumns[Index]) { CacheCoverageGround(Index, Centre); }
        for (const auto& Target : CoverageGround[Index])
        {
            if (TracePrepared(Scan.Ground, Target.Foot, Scan.Support, Target.Support) == ELDSightResult::Visible)
            { LDObservation::SetCoverageBit(Scan.Bits, Index); break; }
        }
    }
    return true;
}

void FLDSightGeometry::Coverage(const FVector& SourceFootCm, TArray<uint8>& OutBits) const
{
    FCoverageScan Scan;
    Scan.Bits = MoveTemp(OutBits);
    if (BeginCoverage(SourceFootCm, Scan)) { ContinueCoverage(Scan); }
    OutBits = MoveTemp(Scan.Bits);
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

bool FLDObserverCoverage::Sample(const FLDSightGeometry& Sight, const FVector& Foot, double NowSeconds, bool bPublish, bool bHasRelay, TArray<uint8>& OutBits,
    int32* MoveBudget)
{
    if (!bPublish || !bHasRelay)
    {
        OutBits.Init(0, LDObservation::CoverageBytes);
        return false;
    }
    const bool bFresh = Bits.Num() != LDObservation::CoverageBytes || World != Sight.Revision();
    bool bRecompute = bFresh || (SourceFeet != Foot && NowSeconds >= NextMoveRefreshAt);
    if (bRecompute && !bFresh && MoveBudget)
    {
        if (*MoveBudget <= 0) { bRecompute = false; }
        else { --*MoveBudget; }
    }
    if (bRecompute)
    {
        Sight.Coverage(Foot, Bits);
        SourceFeet = Foot;
        World = Sight.Revision();
        NextMoveRefreshAt = NowSeconds + MovingRefreshSeconds;
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
        if (Unit.RecentHitId.IsValid())
        {
            Unit.RecentHitId = LDObservation::ScopedHitId(Unit.Handle, Unit.RecentHitId);
            if (!Unit.RecentHitId.IsValid())
            {
                Error = TEXT("Could not scope a visible hit identity to its observation interval.");
                return false;
            }
        }
        Units.Add(MoveTemp(Unit));
    }
    // Output order is observer-local: a frame or concealment never lists units in canonical body order.
    Units.Sort([](const FLDObservedUnitView& A, const FLDObservedUnitView& B) { return A.Handle < B.Handle; });
    Removed.Sort();
    Error.Reset();
    return true;
}

bool FLDObserverLedger::SampleProjectiles(const TArray<const FLDObservedProjectileFact*>& Visible, TArray<FGuid>& Removed,
    TArray<FLDObservedProjectileView>& Out, FString& Error, const FGuid& ObserverEntity)
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
        View.LengthCm=Fact->LengthCm;
        View.bOwn=ObserverEntity.IsValid()&&Fact->SourceEntity==ObserverEntity;
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
    // Spec §11.1 blackout day: every sight line of the match is DAILY.BlackoutVisionCut shorter.
    FLDDemoRational SightRadius = Loaded->Get().Parameters.VisionRadiusCm;
    if (Core->HasDaily(ELDDailyModifier::Blackout))
    {
        SightRadius.Numerator *= 10000 - Loaded->Get().Parameters.Daily.BlackoutVisionCutBp;
        SightRadius.Denominator *= 10000;
    }
    if (!Prepared.Build(*Registry->GetGeometry(), Registry->Revision(), SightRadius, Loaded->Get().Parameters.VisionRange, Error, GetWorld())) { return false; }
    int64 CampMembers = 0;
    FLDCampRoster CampRoster;
    if (!LDCamp::BuildRoster(Loaded->Get(), *Registry->GetGeometry(), CampRoster, Error)) { return false; }
    for (const auto& Camp : CampRoster.Camps) { CampMembers += Camp.Members.Num(); }
    // The roaming dealer is independent of the authored camp roster.
    ++CampMembers;
    const int64 Contenders = Core->ReadLobby().Members.Num();
    // An observer sees at most every other contender, every camp member and the Boss.
    if (Contenders < 1 || Contenders + CampMembers > LDObservation::MaxUnits)
    {
        Error = FString::Printf(TEXT("CapacityExceeded: %lld contenders, %lld camp members and the Boss exceed the %d-unit observation bound."),
            Contenders, CampMembers, LDObservation::MaxUnits);
        return false;
    }
    const int64 SharedTargets = Registry->GetGeometry()->Anchors.FilterByPredicate([](const FLDDemoWorldAnchor& Anchor)
    {
        return Anchor.Kind == ELDDemoWorldAnchorKind::TrainingDummy && Anchor.Id.StartsWith(TEXT("training_shared_"));
    }).Num();
    const int64 RestUnits = 2 * Contenders - 1 + SharedTargets;
    if (RestUnits > LDObservation::MaxUnits)
    {
        Error = FString::Printf(TEXT("CapacityExceeded: this roster supports at most %lld additional shared training targets; the level has %lld."),
            LDObservation::MaxUnits - (2 * Contenders - 1), SharedTargets);
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
        Unit.ActionComboStep = Facts.ActionCue == ELDObservedActionCue::Attack ? Facts.ActionComboStep : 0;
        Unit.HitLook = Facts.HitLook;
        Unit.HitFlags = Facts.HitFlags;
        Unit.RecentHitId = Facts.LastHitId;
        Unit.RecentHitAgePlusOneMs = Facts.LastHitAtMs > 0
            ? uint16(NowMs - Facts.LastHitAtMs + 1) : 0;
        const bool bReaction = Facts.LastReaction != 0 && Facts.LastReactionAtMs > 0 && Facts.LastReactionAtMs <= NowMs
            && NowMs - Facts.LastReactionAtMs < LDObservation::RecentReactionWindowMs;
        Unit.RecentReaction = bReaction ? Facts.LastReaction : 0;
        Unit.RecentReactionAgePlusOneMs = bReaction ? uint16(NowMs - Facts.LastReactionAtMs + 1) : 0;
        Unit.RecentReactionElement = bReaction ? Facts.LastReactionElement : 0;
        Unit.ActionCueAgeMs = Facts.ActionCue == ELDObservedActionCue::None ? 0 : uint16(FMath::Min<int64>(NowMs - Facts.ActionCueAtMs, MAX_uint16));
        if (Facts.ActionCue != ELDObservedActionCue::None)
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
        Unit.ShieldBar = State->Resources.MaxHpP2 > 0 ? uint8(FMath::Clamp<int64>(FMath::Max<int64>(State->Resources.ShieldP2, 0)
            * LDObservation::ShieldBarFull / State->Resources.MaxHpP2, 0, LDObservation::ShieldBarFull)) : 0;
        Unit.Statuses = Facts.Statuses;
        if (Ref.Victim.Kind == ELDVictimKind::Contender)
        {
            Unit.Kind = ELDObservedUnitKind::Contender;
            Unit.AuthorizedFieldMask = LDObservation::ContenderFields;
            // Combat writes the locked roster seat; validation refuses anything outside the eight seats.
            Unit.Presentation = Body->PresentationKey;
            Unit.ContenderId = Ref.Victim.EntityId;
            Unit.Level = Facts.Level;
            Unit.MomentumBar = Facts.MomentumBar;
            Unit.EmpowerReady = Facts.EmpowerReady;
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
            Unit.CampAffix = Facts.CampAffix;
            Unit.bPeddler = Facts.bPeddler;
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
    const TArray<FLDObservedProjectileFact>& Flights, const TArray<FLDObservedGroundFact>& Grounds, bool bPublish, FString& Error)
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
    if (bReset) { Observer.Coverage.Reset(); Observer.MateCoverage.Reset(); Observer.LastSeen.Reset(); }
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
    if (bPKObserver) { PKTeammateFeet(*Mode, PK, Observer.ContenderId, TeammateFeet); }
    if (bAlive)
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(LDVisionObserverTargets);
        // Most of the map is out of sight of any one observer. Where neither the arena nor the shared rest space grants
        // sight, a target beyond the longest sight is unavailable whatever else is read for it: PresentationSight would
        // answer the same from Sight.Trace's range test, after its group and rest lookups.
        const bool bSightOnly = !bArena && Sight.IsValid() && Sight.InBounds(Foot) && !Mode->IsInRestSpace(Observer.ContenderId);
        const double ReachSquared = FMath::Square(Sight.MaximumRadius());
        for (const FLDObservedTarget& Target : Targets)
        {
            if (Target.Body.Victim.EntityId == Observer.ContenderId) { continue; }
            const bool bBeyondReach = bSightOnly && FVector::DistSquared2D(Foot, Target.FootCm) > ReachSquared
                && Sight.InBounds(Target.FootCm);
            const ELDObservedResolve Seen = bBeyondReach ? ELDObservedResolve::TargetUnavailable
                : PresentationSight(*Mode, Combat, PK, Observer.ContenderId, bArena,
                    bSpectating, bPKObserver, Foot, TeammateFeet, Target.Body, Target.FootCm, Sight, Error);
            if (Seen == ELDObservedResolve::AuthorityFault) { return false; }
            if (Seen == ELDObservedResolve::Resolved)
            {
                Visible.Add(&Target);
                if (!bArena) { Observer.LastSeen.Add(Target.Body.Victim.EntityId, {Target.Body.Victim.Life, Mode->GetAuthorityNowSeconds(), Target.FootCm}); }
            }
            // A thin obstacle that hides it for a moment keeps it in sight (and its lock and windup) for the grace.
            else if (!bArena && InSightGrace(Observer, Target.Body, Foot, Target.FootCm)) { Visible.Add(&Target); }
        }
    }
    TArray<FGuid> Removed;
    TArray<FLDObservedUnitView> Units;
    if (!LD_TRACED(LDVisionObserverLedger, Observer.Ledger.Sample(Visible, Removed, Units, Error))) { return false; }
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
    if (!Observer.Ledger.SampleProjectiles(VisibleFlights, RemovedFlights, Projectiles, Error,Observer.ContenderId)) { return false; }
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
        View.Footprint.RadiusCm = float(Sight.MaximumRadius());
        {
            TRACE_CPUPROFILER_EVENT_SCOPE(LDVisionObserverCoverage);
            Observer.Coverage.Sample(Sight, Foot, View.SampledAtServerSec, bPublish, Relay != nullptr, View.Footprint.CurrentBits, &CoverageMoveBudget);
        }
        // #198 session D: a spectator's authorized coverage includes the whole arena; a PK life's includes its teammates'.
        // A teammate joining or leaving rescans every teammate at once rather than reuse a departed one's coverage.
        if (Observer.MateCoverage.Num() != TeammateFeet.Num()) { Observer.MateCoverage.Reset(); Observer.MateCoverage.SetNum(TeammateFeet.Num()); }
        if (bPublish && Relay && (bSpectating || !TeammateFeet.IsEmpty()))
        {
            TRACE_CPUPROFILER_EVENT_SCOPE(LDVisionMateCoverage);
            if (bSpectating) { Sight.AddDisk(PK->GetView().ArenaCentreCm, PK->GetParameters().ArenaRadiusCm, View.Footprint.CurrentBits); }
            for (int32 Mate = 0; Mate < TeammateFeet.Num(); ++Mate)
            {
                TArray<uint8> MateBits;
                Observer.MateCoverage[Mate].Sample(Sight, TeammateFeet[Mate], View.SampledAtServerSec, true, true, MateBits, &CoverageMoveBudget);
                for (int32 Index = 0; Index < MateBits.Num() && Index < View.Footprint.CurrentBits.Num(); ++Index)
                {
                    View.Footprint.CurrentBits[Index] |= MateBits[Index];
                }
            }
        }
        View.Units = MoveTemp(Units);
        View.Projectiles = MoveTemp(Projectiles);
        Observer.Ledger.RecentTerminations(View.RecentTerminations);
        // Ground states: the observer's own ones, and every other whose centre (or a band's end) it sees, nearest first, at
        // most LDObservation::MaxGroundStates; isolation groups follow the owner like a skill effect.
        TArray<TPair<double, const FLDObservedGroundFact*>> SeenGround;
        for (const FLDObservedGroundFact& Fact : Grounds)
        {
            FLDObservedProjectileFact Probe;
            Probe.SourceEntity = Fact.OwnerEntity;
            // An ownerless (DEV) ground state belongs to no isolation group: plain sight decides.
            const ELDObservedFlightAccess Rule = Fact.OwnerEntity.IsValid() ? FlightAccess(Probe) : ELDObservedFlightAccess::Sight;
            bool bVisible = Rule == ELDObservedFlightAccess::Granted || (Fact.OwnerEntity.IsValid() && Fact.OwnerEntity == Observer.ContenderId);
            if (!bVisible && (Rule == ELDObservedFlightAccess::Sight || Rule == ELDObservedFlightAccess::SharedSight))
            {
                // A PK life sees what its living teammates see too, as for flights and coverage.
                TArray<FVector, TInlineAllocator<4>> Eyes;
                Eyes.Add(Foot);
                if (Rule == ELDObservedFlightAccess::SharedSight) { Eyes.Append(TeammateFeet); }
                for (const FVector& Eye : Eyes)
                {
                    const ELDSightResult Result = Sight.Trace(Eye, Fact.CentreCm);
                    if (Result == ELDSightResult::StaleWorld) { Error = TEXT("StaleWorld: ground-state sight has no current world."); return false; }
                    if (Result == ELDSightResult::Visible) { bVisible = true; break; }
                }
            }
            if (bVisible)
            {
                SeenGround.Emplace(FVector2D::DistSquared(FVector2D(Foot.X, Foot.Y), FVector2D(Fact.CentreCm.X, Fact.CentreCm.Y)), &Fact);
            }
        }
        SeenGround.Sort([](const TPair<double, const FLDObservedGroundFact*>& A, const TPair<double, const FLDObservedGroundFact*>& B)
        { return A.Key < B.Key; });
        for (int32 Index = 0; Index < SeenGround.Num() && Index < LDObservation::MaxGroundStates; ++Index)
        { View.GroundStates.Add(LDObservation::GroundView(*SeenGround[Index].Value)); }
        // Camp-clear recovery orbs are strictly fog-of-war authorized: only a field observer with line-of-sight sees them.
        if (Mode && Mode->IsFieldRunning() && !Mode->IsShowdown() && !Mode->IsInRestSpace(Observer.ContenderId) && !bArena)
        {
            const uint8 Segment = Mode->GetTimeline().GetFieldSegment();
            if (Observer.EmptyCampSegment != Segment)
            {
                Observer.EmptyCampSegment = Segment;
                Observer.WitnessedEmptyCamps.Reset();
            }
            if (const ALDCampDirector* Camps = Mode->Camps.Get())
            {
                const FLDCampMatchState& State = Camps->GetState();
                for (int32 Index = 0; Index < State.NumCamps(); ++Index)
                {
                    const FLDCampSpec& Spec = State.GetRoster()->Camps[Index];
                    const FLDCampGroupState& Group = *State.FindCamp(Index);
                    if (Group.Generation == 0 || Sight.Trace(Foot, Spec.AnchorCm) != ELDSightResult::Visible) { continue; }
                    if (Group.Members.ContainsByPredicate([](const FLDCampMemberState& Member) { return Member.bAlive; }))
                    {
                        Observer.WitnessedEmptyCamps.Remove(Spec.CampId);
                    }
                    else
                    {
                        Observer.WitnessedEmptyCamps.Add(Spec.CampId);
                    }
                }
            }
            View.WitnessedEmptyCamps = Observer.WitnessedEmptyCamps.Array();
            View.WitnessedEmptyCamps.Sort(FNameLexicalLess());
            const int64 FieldNowMs = Mode->GetFieldNowMs();
            TArray<TPair<double, const FLDRecoveryOrbView*>> SeenOrbs;
            for (const FLDRecoveryOrbView& Orb : Mode->GetRecoveryOrbs())
            {
                if (Orb.Serial == 0 || FieldNowMs >= Orb.ExpireAtMs) { continue; }
                const double PackRange = Orb.ExpireAtMs == MAX_int64 ? LDFieldEcology::Centimeters(Content->Get().Parameters.FieldMapRecoverySightCm) : 0.;
                const ELDSightResult Result = Sight.Trace(Foot, Orb.PositionCm, PackRange);
                if (Result == ELDSightResult::StaleWorld)
                {
                    Error = TEXT("StaleWorld: recovery-orb sight has no current world.");
                    return false;
                }
                if (Result == ELDSightResult::Visible)
                {
                    SeenOrbs.Emplace(FVector2D::DistSquared(FVector2D(Foot.X, Foot.Y), FVector2D(Orb.PositionCm.X, Orb.PositionCm.Y)), &Orb);
                }
            }
            SeenOrbs.Sort([](const TPair<double, const FLDRecoveryOrbView*>& A, const TPair<double, const FLDRecoveryOrbView*>& B)
            { return A.Key < B.Key; });
            for (int32 Index = 0; Index < SeenOrbs.Num() && Index < LDObservation::MaxRecoveryOrbs; ++Index)
            {
                FLDObservedRecoveryOrb AuthorizedOrb;
                AuthorizedOrb.Serial = SeenOrbs[Index].Value->Serial;
                AuthorizedOrb.PositionCm = SeenOrbs[Index].Value->PositionCm;
                View.RecoveryOrbs.Add(AuthorizedOrb);
            }
        }
    }
    if (!LDObservation::ValidateView(View, Error)) { return false; }
    // The view (units and the whole coverage mask) is kept, not copied, and published from where it is kept.
    Observer.Latest = MoveTemp(View);
    Observer.bHasLatest = true;
    if (bPublish && Relay)
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(LDVisionObserverSend);
        Relay->ClientFullView(Observer.Latest);
    }
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
    if (!LD_TRACED(LDVisionGatherTargets, GatherTargets(Targets, Error))) { return false; }
    // Combat's flights after its last Effects advance; Vision only decides who may see them.
    TArray<FLDObservedProjectileFact> Flights;
    if (!Core->Combat->ReadObservedProjectiles(Flights, Error))
    {
        if (Error.IsEmpty()) { Error = TEXT("LD_OBSERVATION_PROJECTILES_UNAVAILABLE"); }
        return false;
    }
    // Spec §8.3/§14: every live ground state is public; each observer sees the ones in its sight.
    TArray<FLDObservedGroundFact> Grounds;
    if (!Core->Combat->ReadObservedGround(Grounds, Error))
    {
        if (Error.IsEmpty()) { Error = TEXT("LD_OBSERVATION_GROUND_UNAVAILABLE"); }
        return false;
    }
    CoverageMoveBudget = FLDObserverCoverage::MovingRescansPerSample;
    for (FObserver& Observer : Observers)
    {
        if (!SampleObserver(Observer, Targets, Flights, Grounds, bPublish, Error)) { return false; }
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
    const bool bArena = Core->PKDirector.Get() && Core->PKDirector->IsArenaActive();
    if (Outcome != ELDObservedResolve::Resolved && (bArena || !InSightGrace(*Observer, Target, Foot, TargetFoot)))
    { return Refuse(Outcome, TEXT("The target is not visible at this authority moment.")); }
    Out.Target = Target;
    Out.FootCm = TargetFoot;
    Out.World = Sight.Revision();
    Out.SampledAtSeconds = Core->GetAuthorityNowSeconds();
    if (Diagnostic) { Diagnostic->Reset(); }
    return ELDObservedResolve::Resolved;
}

ELDObservedResolve ALDVisionDirector::ResolvePresentedUnit(const ALDPlayerController& RegisteredCaller, const FGuid& Handle,
    uint32 ObservationEpoch, FLDAuthorityTargetSample& Out, FString* Diagnostic) const
{
    const auto Refuse = [Diagnostic](ELDObservedResolve Result, const FString& Detail)
    {
        if (Diagnostic) { *Diagnostic = Detail; }
        return Result;
    };
    const FObserver* Observer = FindObserver(RegisteredCaller);
    ALDGameMode* Core = AuthorityMode();
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
    const auto* Registry = GetWorld() ? GetWorld()->GetSubsystem<ULDMapRegistry>() : nullptr;
    if (!Registry || !Sight.IsValid() || Registry->Revision() != Sight.Revision())
    { return Refuse(ELDObservedResolve::AuthorityFault, TEXT("The registered world changed under observation.")); }
    const ALDPKDirector* PK = Core->PKDirector.Get();
    const bool bArena = PK && PK->IsArenaActive();
    const bool bSpectating = bArena && PK->CanSpectate(Observer->ContenderId);
    const bool bPKObserver = bArena && Core->IsPKLife(Observer->ContenderId);
    TArray<FVector> Teammates;
    PKTeammateFeet(*Core, PK, Observer->ContenderId, Teammates);
    const FVector TargetFoot = FootOf(*TargetBody);
    const ELDObservedResolve Outcome = PresentationSight(*Core, Core->Combat.Get(), PK, Observer->ContenderId,
        bArena, bSpectating, bPKObserver, Foot, Teammates, Target, TargetFoot, Sight, Error);
    if (Outcome != ELDObservedResolve::Resolved)
    { return Refuse(Outcome, Outcome == ELDObservedResolve::AuthorityFault ? Error : TEXT("The target is not currently presentable.")); }
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

double ALDVisionDirector::SightGroundZ(const FVector& FootCm) const
{
    return Sight.IsValid() ? Sight.GroundZ(FootCm) : FootCm.Z;
}

ELDSightResult ALDVisionDirector::TraceObservation(const FVector& SourceFootCm, const ALDCharacter& Target,
    const FLDWorldRevision& Expected) const
{
    return TraceSight(SourceFootCm, FootOf(Target), Expected);
}

bool ALDVisionDirector::InSightGrace(const FObserver& Observer, const FLDCombatBodyRef& Target, const FVector& ObserverFootCm,
    const FVector& TargetFootCm) const
{
    // A body that moves farther than this between two samples was placed (respawn, rest, arena), not hidden.
    constexpr double TeleportCm = 400.;
    const FObserver::FSeen* Seen = Observer.LastSeen.Find(Target.Victim.EntityId);
    const ALDGameMode* Core = AuthorityMode();
    const auto* Registry = GetWorld() ? GetWorld()->GetSubsystem<ULDMapRegistry>() : nullptr;
    if (!Seen || !Core || !Content || !Registry || Seen->Life != Target.Victim.Life
        || FVector::Dist2D(Seen->FootCm, TargetFootCm) > TeleportCm
        || Core->GetAuthorityNowSeconds() - Seen->AtSeconds > double(Content->Get().Parameters.VisionSightGraceMs) / 1000.)
    { return false; }
    return TraceSight(ObserverFootCm, TargetFootCm, Registry->Revision()) == ELDSightResult::Blocked;
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

void ALDVisionDirector::SendOwnHit(const FLDCombatBodyRef& Attacker, const FLDCombatBodyRef& Target, const FGuid& HitId,
    int64 OccurredAtMs, ELDObservedHitLook HitLook, uint8 HitFlags)
{
    if (!bActive || Attacker.Victim.Kind != ELDVictimKind::Contender || !HitId.IsValid() || OccurredAtMs <= 0) { return; }
    FObserver* Observer = Observers.FindByPredicate([&Attacker](const FObserver& Candidate)
        { return Candidate.ContenderId == Attacker.Victim.EntityId; });
    ALDPlayerController* Controller = Observer ? Observer->Controller.Get() : nullptr;
    ALDWorldViewRelay* Relay = Observer ? Observer->Relay.Get() : nullptr;
    if (!Observer || !Controller || !Relay || Observer->Ledger.Phase() != ELDLifePhase::Alive
        || Observer->Ledger.ObserverLife() != Attacker.Victim.Life) { return; }
    FGuid Handle;
    uint32 Epoch = 0;
    if (!FindObservedHandle(*Controller, Target, Handle, Epoch)) { return; }
    FLDAuthorityTargetSample Resolved;
    FString Diagnostic;
    const ELDObservedResolve Outcome = ResolvePresentedUnit(*Controller, Handle, Epoch, Resolved, &Diagnostic);
    if (Outcome == ELDObservedResolve::AuthorityFault)
    { UE_LOG(LogLDWorld, Error, TEXT("LD_HIT_RECEIPT_AUTHORITY_FAULT %s"), *Diagnostic); }
    if (Outcome != ELDObservedResolve::Resolved || !(Resolved.Target == Target)) { return; }
    FLDObservedHitReceipt Receipt;
    Receipt.MatchGeneration = Observer->Ledger.MatchGeneration();
    Receipt.ObserverLife = Observer->Ledger.ObserverLife();
    Receipt.Generation = Observer->Ledger.Generation();
    Receipt.Handle = Handle;
    Receipt.ObservationEpoch = Epoch;
    Receipt.HitId = LDObservation::ScopedHitId(Handle, HitId);
    if (!Receipt.HitId.IsValid())
    {
        UE_LOG(LogLDWorld, Error, TEXT("LD_HIT_RECEIPT_ID scope=failed"));
        return;
    }
    Receipt.OccurredAtMs = OccurredAtMs;
    Receipt.HitLook = HitLook;
    Receipt.HitFlags = HitFlags;
    Relay->ClientPresentedHit(Receipt);
}
