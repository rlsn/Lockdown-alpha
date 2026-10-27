#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LDMapRegistry.generated.h"
class ULDMapDefinition;
class UPrimitiveComponent;
class FJsonObject;

struct FLDMapBinding
{
    FName Id,Surface;
    FVector Position=FVector::ZeroVector;
    TWeakObjectPtr<UPrimitiveComponent> Floor;
    TArray<FVector> HubPath;
};

// B90 world-local spatial addresses. Declared service functions have no B92
// transaction handler here; aliases resolve to one address, never another source.
UCLASS()
class LOCKDOWN_API ULDMapRegistry : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void OnWorldBeginPlay(UWorld& World) override;
    bool Enabled() const { return Definition!=nullptr; }
    uint32 Generation=0;
    bool Rebuild(FString& Error);
    bool Ensure(FString& Error);
    const FLDMapBinding* Resolve(FName Id,uint32 ExpectedGeneration) const;
    const TArray<FLDMapBinding>& Sites() const { return Bindings; }
    const TArray<FLDMapBinding>& ServiceSeats() const { return ServiceBindings; }
    FString CanonicalBindings() const;
    TSharedRef<FJsonObject> Report() const;
private:
    const ULDMapDefinition* Definition=nullptr;
    TArray<FLDMapBinding> Bindings,ServiceBindings;
};

namespace LDMapFitting
{
    LOCKDOWN_API TArray<FBox> StructuralWalls(UWorld* World);
    LOCKDOWN_API bool RoadOverlaps(const FTransform& Tile,const FBox& Wall,double Padding=0);
}
