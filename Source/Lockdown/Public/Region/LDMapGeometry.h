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

USTRUCT()
struct FLDDemoWorldBlocker
{
    GENERATED_BODY()

    UPROPERTY() FString Id;
    UPROPERTY() FString SurfaceId;
    UPROPERTY() TArray<FLDDemoWorldPoint> PolygonCm;
    UPROPERTY() FLDDemoRational HeightCm;
    UPROPERTY() bool BlocksMovement = false;
    UPROPERTY() bool BlocksSight = false;
};

USTRUCT()
struct FLDDemoWorldMemberHome
{
    GENERATED_BODY()

    UPROPERTY() FString Id;
    UPROPERTY() FLDDemoWorldPoint PositionCm;
};

UENUM()
enum class ELDDemoWorldAnchorKind : uint8 { Spawn, Camp, Shop, Capture };

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
struct FLDDemoWorldConnection
{
    GENERATED_BODY()

    UPROPERTY() FString Id;
    UPROPERTY() TArray<FString> WallIds;
    UPROPERTY() FString CampId;
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
    UPROPERTY() TArray<FLDDemoWorldConnection> Connections;
    UPROPERTY() TArray<FString> ParameterRefs;
    UPROPERTY() TArray<FLDDemoWorldAnchor> Anchors;
};
