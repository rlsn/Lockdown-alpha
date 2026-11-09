#pragma once

#include "Development/LDDemoContentDefinitions.h"
#include "LDMapGeometry.generated.h"

USTRUCT()
struct LOCKDOWN_API FLDDemoWorldPoint
{
    GENERATED_BODY()

    UPROPERTY() FLDDemoRational X;
    UPROPERTY() FLDDemoRational Y;
    UPROPERTY() FLDDemoRational Z;

    bool ToVector(FVector& Out, FString& Error) const;
};

USTRUCT()
struct FLDDemoWorldSurface
{
    GENERATED_BODY()

    UPROPERTY() FString Id;
    UPROPERTY() FLDDemoWorldPoint MinCm;
    UPROPERTY() FLDDemoWorldPoint MaxCm;
};

// A blocker's footprint: a strictly convex counterclockwise polygon (walls, stalls) or an upright cylinder (trees).
UENUM()
enum class ELDDemoWorldBlockerShape : uint8 { Polygon, Cylinder };

USTRUCT()
struct FLDDemoWorldBlocker
{
    GENERATED_BODY()

    UPROPERTY() FString Id;
    UPROPERTY() FString SurfaceId;
    UPROPERTY() ELDDemoWorldBlockerShape Shape = ELDDemoWorldBlockerShape::Polygon;
    // Polygon only: 3..16 vertices on the supporting surface.
    UPROPERTY() TArray<FLDDemoWorldPoint> PolygonCm;
    // Cylinder only: footprint centre on the supporting surface and its radius.
    UPROPERTY() FLDDemoWorldPoint CenterCm;
    UPROPERTY() FLDDemoRational RadiusCm;
    UPROPERTY() FLDDemoRational HeightCm;
    UPROPERTY() bool BlocksMovement = false;
    UPROPERTY() bool BlocksSight = false;
};

namespace LDDemoWorld
{
    // Sides of the regular polygon that stands in for a cylinder where a consumer needs vertices (meshes, overlap
    // tests). It circumscribes the true circle, so a point outside the polygon is outside the cylinder.
    constexpr int32 CylinderSides = 24;
    // The footprint as XY vertices: the polygon itself, or the circumscribing CylinderSides-gon of a cylinder.
    LOCKDOWN_API bool Footprint(const FLDDemoWorldBlocker& Blocker, TArray<FVector2D>& Out, FString& Error);
    constexpr int32 MaxLevel = 2;
    // #97 anchor counts follow the authored world within these bounds (at least one spawn per match seat, one
    // capture, exactly four Boss waypoints).
    constexpr int32 MaxAnchors = 128, MinSpawns = 6, MaxSpawns = 32, MaxCamps = 48, MaxDorms = 8,
        MaxAirdrops = 16, BossWaypoints = 4, MaxBlockers = 2048;
    // Actor tag of every generated plateau and ramp floor piece; the base ground keeps its surface id tag.
    inline const FName TerrainTag(TEXT("LDDemoTerrain"));
    // Every generated blocker actor carries this tag and its blocker id as a second tag.
    inline const FName StaticGeometryTag(TEXT("LDDemoStaticGeometry"));
    // Steepest ramp: rise / run at most 7 / 10 (about 35 degrees), under the bodies' 40 degree walkable floor.
    constexpr int64 RampRise = 7, RampRun = 10;

    // Ground of the registered terrain in native doubles, for runtime queries. Tier counts half levels: 2L on level L,
    // 2L + 1 on a ramp from L to L + 1; where regions meet, the higher tier wins.
    class LOCKDOWN_API FTerrain
    {
    public:
        bool Build(const FLDDemoWorldGeometry& Geometry, FString& Error);
        bool IsValid() const { return StepCm > 0; }
        int32 Tier(const FVector2D& Point) const;
        // Supporting height and upward unit normal at Point.
        void Ground(const FVector2D& Point, double& OutZ, FVector& OutNormal) const;
        double StepCm = 0;
        struct FRegion
        {
            TArray<FVector2D> Polygon;
            int32 Tier = 0;
            // Ramps: height = Base + Rise * Cross(Along, Point - Origin) / Width; flat regions leave Rise 0.
            FVector2D Origin = FVector2D::ZeroVector, Along = FVector2D::ZeroVector;
            double Base = 0, Rise = 0, Width = 1;
        };
        TArray<FRegion> Regions;
    private:
        static constexpr double GridCellCm = 400.;
        FBox2D RegionBounds = FBox2D(ForceInit);
        TMap<FIntPoint, TArray<int32>> RegionCells;
    };
    LOCKDOWN_API bool InsideConvex(const TArray<FVector2D>& Polygon, const FVector2D& Point);
}

// #97 elevation: flat levels 0 (the ground), 1 and 2, LevelStepCm apart. A plateau is a convex prism raising its
// footprint to Level; a ramp joins Lower to Lower + 1. Every plateau edge not met by a ramp is a cliff: it stops
// movement, and only the higher side sees across it.
USTRUCT()
struct FLDDemoWorldPlateau
{
    GENERATED_BODY()

    UPROPERTY() FString Id;
    UPROPERTY() int32 Level = 0;
    // Strictly convex counterclockwise footprint at the plateau's top height.
    UPROPERTY() TArray<FLDDemoWorldPoint> PolygonCm;
};

USTRUCT()
struct FLDDemoWorldRamp
{
    GENERATED_BODY()

    UPROPERTY() FString Id;
    UPROPERTY() int32 LowerLevel = 0;
    // Counterclockwise quad: [0]->[1] is the low edge on the lower level, [2]->[3] the parallel high edge laid along an
    // upper plateau's edge; the surface between them is one plane.
    UPROPERTY() TArray<FLDDemoWorldPoint> PolygonCm;
};

USTRUCT()
struct FLDDemoWorldMemberHome
{
    GENERATED_BODY()

    UPROPERTY() FString Id;
    UPROPERTY() FLDDemoWorldPoint PositionCm;
};

UENUM()
// D21 (#79): a Dorm is a public recovery disk of DORM.RadiusCm; D26 (#79): an Airdrop is a public drop point whose
// pickup disk is AIRDROP.PickupRadiusCm. Neither adds collision, navigation or sight blocking.
// D26 (#79): a BossWaypoint is one of the four public points the 画饼老板 Boss patrols in id order; a point only.
enum class ELDDemoWorldAnchorKind : uint8 { Spawn, Camp, Capture, Dorm, Airdrop, BossWaypoint };

USTRUCT()
struct FLDDemoWorldAnchor
{
    GENERATED_BODY()

    UPROPERTY() FString Id;
    UPROPERTY() ELDDemoWorldAnchorKind Kind = ELDDemoWorldAnchorKind::Spawn;
    UPROPERTY() FString SurfaceId;
    UPROPERTY() FLDDemoWorldPoint PositionCm;
    UPROPERTY() TArray<FLDDemoWorldMemberHome> Members;
};

USTRUCT()
struct LOCKDOWN_API FLDDemoWorldGeometry
{
    GENERATED_BODY()

    UPROPERTY() FString MapId;
    UPROPERTY() int32 LayoutRevision = 0;
    UPROPERTY() int32 GeometryRevision = 0;
    UPROPERTY() FLDDemoContentDigest SourceDigest;
    UPROPERTY() FLDDemoContentDigest AuthorSchemaDigest;
    UPROPERTY() FString ApprovalDecision;
    UPROPERTY() TArray<FLDDemoWorldSurface> Surfaces;
    UPROPERTY() FLDDemoRational LevelStepCm;
    UPROPERTY() TArray<FLDDemoWorldPlateau> Plateaus;
    UPROPERTY() TArray<FLDDemoWorldRamp> Ramps;
    UPROPERTY() TArray<FLDDemoWorldBlocker> Blockers;
    UPROPERTY() FLDDemoRational SightSampleHeightCm;
    UPROPERTY() FLDDemoRational CampClearanceCm;
    UPROPERTY() TArray<FLDDemoWorldPoint> SpawnOffsetsCm;
    UPROPERTY() FLDDemoRational SpawnValidationRadiusCm;
    UPROPERTY() TArray<FString> ParameterRefs;
    UPROPERTY() TArray<FLDDemoWorldAnchor> Anchors;
};
