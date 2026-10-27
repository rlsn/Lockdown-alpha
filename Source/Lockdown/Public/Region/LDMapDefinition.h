#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameFramework/Actor.h"
#include "LDMapDefinition.generated.h"

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
    UPROPERTY() int32 Seed = 9001;
    UPROPERTY() int32 LayoutRevision = 1;
    UPROPERTY() int32 FitterRevision = 2;
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

    bool Import(const FString& Json, FString& Error);
    bool Validate(FString& Error) const;
    const FLDMapPoint* Find(FName Id) const;
    FVector NavigationTarget(FName Id) const;
    double Height(const FVector2D& XY) const;
    bool RiverAt(const FVector& Surface) const;
    static bool InBounds(const FVector& Point);
    static const ULDMapDefinition* Get(const UWorld* World);
};

UCLASS()
class LOCKDOWN_API ALDMapAnchor : public AActor
{
    GENERATED_BODY()
public:
    ALDMapAnchor();
    UPROPERTY(EditAnywhere) TObjectPtr<ULDMapDefinition> Definition;
};
