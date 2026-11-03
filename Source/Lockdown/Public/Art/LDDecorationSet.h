#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDDecorationSet.generated.h"

class UInstancedStaticMeshComponent;
struct FLDArtResolved;

// Campus decoration props of the Demo map. Spawned locally on each rendering machine, never replicated or saved.
// Every part is display only: it cannot collide, overlap, block the LDVision sight traces or affect navigation, so
// the approved D02 walls, sight lines and navmesh stay exactly as generated.
UCLASS(NotPlaceable, Transient)
class LOCKDOWN_API ALDDecorationSet : public AActor
{
    GENERATED_BODY()
public:
    ALDDecorationSet();
    // One instanced component per prop holding all of its placements. Fails without partial placement.
    bool Build(const FLDArtResolved& Art, FString& Error);
    // True when every primitive part ignores all channels, generates no overlaps and is irrelevant to navigation.
    bool IsPresentationOnly() const;
    int32 GetInstanceCount() const;
    const TArray<TObjectPtr<UInstancedStaticMeshComponent>>& GetParts() const { return Parts; }
private:
    UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> Parts;
};
