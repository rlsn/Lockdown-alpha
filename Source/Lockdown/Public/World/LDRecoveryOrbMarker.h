#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDRecoveryOrbMarker.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;

// Presentation of one camp clear recovery orb: a bright ring at AIRDROP.PickupRadiusCm around the public orb point. It only
// mirrors the accepted public view; it is never replicated and has no collision, navigation, sight or height effect.
UCLASS(NotPlaceable, Transient)
class LOCKDOWN_API ALDRecoveryOrbMarker : public AActor
{
    GENERATED_BODY()
public:
    ALDRecoveryOrbMarker();
    bool Build(const FVector& CenterCm, double RadiusCm, uint64 InSerial, uint32 InGeneration, FString& Error);
    uint64 GetSerial() const { return Serial; }
    uint32 GetGeneration() const { return Generation; }
private:
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Ring;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Core;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> RingPaint;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> CorePaint;
    uint64 Serial = 0;
    uint32 Generation = 0;
};
