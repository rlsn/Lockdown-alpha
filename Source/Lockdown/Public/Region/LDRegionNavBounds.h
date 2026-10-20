#pragma once
#include "CoreMinimal.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "LDRegionNavBounds.generated.h"
class UBoxComponent;
// A BSP brush can lose its authoring bounds during cooking. This noncolliding
// runtime component preserves the public navigation envelope in cooked worlds.
UCLASS()
class LOCKDOWN_API ALDRegionNavBounds : public ANavMeshBoundsVolume
{
    GENERATED_BODY()
public:
    ALDRegionNavBounds();
    void Configure(const FVector& Size);
    UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> CookedBounds;
};
