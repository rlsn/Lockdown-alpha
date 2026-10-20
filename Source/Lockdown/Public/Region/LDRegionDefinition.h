#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameFramework/Actor.h"
#include "Vision/LDViewTypes.h"
#include "LDRegionDefinition.generated.h"

USTRUCT()
struct FLDRegionNode
{
    GENERATED_BODY()
    UPROPERTY() uint8 Id=0;
    UPROPERTY() FString Name;
    UPROPERTY() uint8 Site=0; // Public interaction address; distinct from region identity.
    UPROPERTY() FVector Position=FVector::ZeroVector;
    UPROPERTY() float VisitRadius=250;
    UPROPERTY() bool Visit=false;
};
USTRUCT()
struct FLDRegionRoute
{
    GENERATED_BODY()
    UPROPERTY() uint8 From=0;
    UPROPERTY() uint8 To=0;
    UPROPERTY() bool Covered=false;
    UPROPERTY() TArray<FVector> Points;
};
USTRUCT()
struct FLDRegionGeometry
{
    GENERATED_BODY()
    UPROPERTY() FString Name;
    UPROPERTY() FString Mesh;
    UPROPERTY() FVector Position=FVector::ZeroVector;
    UPROPERTY() FVector Size=FVector(100);
    UPROPERTY() float Pitch=0;
    UPROPERTY() bool Floor=false;
    UPROPERTY() bool High=false;
};
UCLASS()
class LOCKDOWN_API ULDRegionDefinition : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY() FString SourceJson;
    UPROPERTY() FString Signature;
    UPROPERTY() FString Name;
    UPROPERTY() uint8 CityRegion=0;
    UPROPERTY() FLDVisionGrid Grid;
    UPROPERTY() FVector Guild=FVector::ZeroVector;
    UPROPERTY() FVector Heart=FVector::ZeroVector;
    UPROPERTY() FVector Tower=FVector::ZeroVector;
    UPROPERTY() FVector Control=FVector::ZeroVector;
    UPROPERTY() FVector Sentinel=FVector::ZeroVector;
    UPROPERTY() TArray<FLDRegionNode> Nodes;
    UPROPERTY() TArray<FLDRegionRoute> Routes;
    UPROPERTY() TArray<FLDRegionGeometry> Geometry;
    UPROPERTY() TArray<FVector> Spawns;
    UPROPERTY() TArray<FVector> Residents;
    UPROPERTY() uint8 TrainedResident=7;
    bool Import(const FString& Json,FString& Error);
    bool Validate(FString& Error) const;
    const FLDRegionNode* Node(uint8 Id) const;
    TArray<uint8> Towns() const;
    FVector SitePosition(uint8 Site) const;
    uint8 NearestRegion(const FVector& Point) const;
};
// Cooked public map configuration, present on every machine; contains no private state.
UCLASS()
class LOCKDOWN_API ALDRegionAnchor : public AActor
{
    GENERATED_BODY()
public:
    ALDRegionAnchor();
    UPROPERTY(EditAnywhere) TObjectPtr<ULDRegionDefinition> Definition;
};
namespace LDRegion
{
    LOCKDOWN_API const ULDRegionDefinition* Definition(const UWorld* World);
    LOCKDOWN_API FVector Site(const UWorld* World,uint8 Address);
    LOCKDOWN_API FString SiteName(const UWorld* World,uint8 Address);
    LOCKDOWN_API uint8 District(const UWorld* World,const FVector& Position);
    LOCKDOWN_API FString DistrictName(const UWorld* World,uint8 Id);
}
