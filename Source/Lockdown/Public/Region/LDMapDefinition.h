#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameFramework/Actor.h"
#include "Region/LDMapGeometry.h"
#include "LDMapDefinition.generated.h"

class FLDDemoContentSnapshot;

class LOCKDOWN_API FLDDemoPreparedWorld
{
public:
    ~FLDDemoPreparedWorld() = default;
    FLDDemoPreparedWorld(const FLDDemoPreparedWorld&) = delete;
    FLDDemoPreparedWorld& operator=(const FLDDemoPreparedWorld&) = delete;

private:
    friend class ULDMapDefinition;
    FLDDemoPreparedWorld() = default;
    FString SourceJson;
    FString Checksum;
    FLDDemoContentDigest ContentRoot;
    FLDDemoWorldGeometry Geometry;
};

// Independent of the legacy 60x40 fog grid and uint8 service-address space.
USTRUCT()
struct FLDMapPoint
{
    GENERATED_BODY()
    UPROPERTY() FName Id;
    UPROPERTY() FString Name;
    UPROPERTY() FString Kind;
    UPROPERTY() FVector Position = FVector::ZeroVector;
    UPROPERTY() FVector Size = FVector::ZeroVector;
    UPROPERTY() double Radius = 0;
    UPROPERTY() bool Enterable = false;
    UPROPERTY() FName Settlement;
    UPROPERTY() FName POI;
    UPROPERTY() FString Identity;
    UPROPERTY() FString Security;
    UPROPERTY() TArray<FName> Functions;
    UPROPERTY() TArray<FName> Doors;
    UPROPERTY() int32 Floors = 1;
    UPROPERTY() bool Roof = false;
    UPROPERTY() double CivicRadius = 0;
};

USTRUCT()
struct FLDMapService
{
    GENERATED_BODY()
    UPROPERTY() FName Id;
    UPROPERTY() FName POI;
    UPROPERTY() FName Building;
    UPROPERTY() FName Settlement;
    UPROPERTY() FVector Position = FVector::ZeroVector;
    UPROPERTY() TArray<FName> Functions;
};

USTRUCT()
struct FLDMapLine
{
    GENERATED_BODY()
    UPROPERTY() FName Id;
    UPROPERTY() FName From;
    UPROPERTY() FName To;
    UPROPERTY() double Width = 0;
    UPROPERTY() double Bank = 0;
    UPROPERTY() TArray<FVector> Points;
};

UCLASS()
class LOCKDOWN_API ULDMapDefinition : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY() FString SourceJson;
    UPROPERTY() FString Checksum;
    UPROPERTY() int32 Seed = 0;
    UPROPERTY() int32 LayoutRevision = 0;
    UPROPERTY() int32 FitterRevision = 0;
    UPROPERTY() TArray<FLDMapPoint> POIs;
    UPROPERTY() TArray<FLDMapPoint> Spawns;
    UPROPERTY() TArray<FLDMapPoint> Settlements;
    UPROPERTY() TArray<FLDMapPoint> Buildings;
    UPROPERTY() TArray<FLDMapPoint> Plateaus;
    UPROPERTY() TArray<FLDMapLine> Routes;
    UPROPERTY() TArray<FLDMapLine> Rivers;
    UPROPERTY() TArray<FName> Bridges;
    UPROPERTY() FVector WetInset = FVector::ZeroVector;
    UPROPERTY() FVector2D WetSize = FVector2D::ZeroVector;
    UPROPERTY() TArray<FLDMapService> Services;
    UPROPERTY() TMap<FName,FName> Aliases;
    UPROPERTY() double CivicHeartOverride = 0;

    bool Import(const FString& Json, FString& Error);
    bool Import(const TArray<uint8>& Json, const FLDDemoContentSnapshot& Content, FString& Error);
    static bool PrepareProjection(const TArray<uint8>& Json, const FLDDemoContentSnapshot& Content,
        TUniquePtr<FLDDemoPreparedWorld>& Out, FString& Error);
    bool ValidatePrepared(const FLDDemoPreparedWorld& Expected, FString& Error) const;
    bool Validate(FString& Error) const;
    bool Read(const FLDDemoContentSnapshot& Content, TSharedPtr<const FLDDemoWorldGeometry>& Out, FString& Error) const;
    const FLDMapPoint* Find(FName Id) const;
    FVector NavigationTarget(FName Id) const;
    FName CanonicalId(FName Id) const;
    const FLDMapService* ServiceFor(FName POI) const;
    FName CivicPolicy(const FVector& Point) const;
    double Height(const FVector2D& XY) const;
    bool RiverAt(const FVector& Surface) const;
    bool UpwardLedge(const FVector& From,const FVector& To) const;
    static bool InBounds(const FVector& Point);
    static const ULDMapDefinition* Get(const UWorld* World);
private:
    UPROPERTY() FLDDemoContentDigest ContentRoot;
    UPROPERTY() FLDDemoWorldGeometry DemoGeometry;
};

UCLASS()
class LOCKDOWN_API ALDMapAnchor : public AActor
{
    GENERATED_BODY()
public:
    ALDMapAnchor();
    UPROPERTY(EditAnywhere) TObjectPtr<ULDMapDefinition> Definition;
};
