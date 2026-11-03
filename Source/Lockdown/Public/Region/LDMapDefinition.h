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

UCLASS()
class LOCKDOWN_API ULDMapDefinition : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY() FString SourceJson;
    UPROPERTY() FString Checksum;

    bool Import(const TArray<uint8>& Json, const FLDDemoContentSnapshot& Content, FString& Error);
    static bool PrepareProjection(const TArray<uint8>& Json, const FLDDemoContentSnapshot& Content,
        TUniquePtr<FLDDemoPreparedWorld>& Out, FString& Error);
    bool ValidatePrepared(const FLDDemoPreparedWorld& Expected, FString& Error) const;
    bool Read(const FLDDemoContentSnapshot& Content, TSharedPtr<const FLDDemoWorldGeometry>& Out, FString& Error) const;
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
