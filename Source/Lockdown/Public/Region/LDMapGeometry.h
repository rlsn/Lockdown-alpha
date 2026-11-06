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
}

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
enum class ELDDemoWorldAnchorKind : uint8 { Spawn, Camp, Shop, Capture, Dorm, Airdrop, BossWaypoint };

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
    UPROPERTY() TArray<FLDDemoWorldBlocker> Blockers;
    UPROPERTY() FLDDemoRational SightSampleHeightCm;
    UPROPERTY() FLDDemoRational CampClearanceCm;
    UPROPERTY() TArray<FLDDemoWorldPoint> SpawnOffsetsCm;
    UPROPERTY() FLDDemoRational SpawnValidationRadiusCm;
    UPROPERTY() TArray<FString> ParameterRefs;
    UPROPERTY() TArray<FLDDemoWorldAnchor> Anchors;
};
