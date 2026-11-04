#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Framework/LDCaptureTypes.h"
#include "LDCaptureMarker.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;

// Local presentation of the single spray zone: a flat painted ring on the registered ground at the approved radius
// around the flagpole anchor, and the graffiti board on the flagpole painted in its owner's public identity tint.
// Each machine builds it from its own world data and only mirrors the accepted public spray state; it is never
// replicated and has no collision, navigation, sight or height effect.
UCLASS(NotPlaceable, Transient)
class LOCKDOWN_API ALDCaptureMarker : public AActor
{
    GENERATED_BODY()
public:
    ALDCaptureMarker();
    // Lays the ring for one match generation; false with a reason when the cooked mesh or material is missing.
    bool Build(const FVector& CenterCm, double RadiusCm, uint32 InGeneration, FString& Error);
    void ShowState(ELDCircleState State);
    // The flagpole graffiti: shown in the owner's identity tint (his roster slot) while one stands, hidden when bare.
    void ShowGraffiti(bool bOwned, uint8 PresentationSlot);
    static FLinearColor ColorFor(ELDCircleState State);
    uint32 GetGeneration() const { return Generation; }
    const FVector& GetCenter() const { return Center; }
    double GetRadius() const { return Radius; }
    ELDCircleState GetShownState() const { return Shown; }
    bool IsGraffitiShown() const;
    uint8 GetGraffitiSlot() const { return GraffitiSlot; }
    int32 GetSegmentCount() const;
    // True while the ring is pure presentation: no collision, overlap or navigation influence.
    bool IsPresentationOnly() const;
private:
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Ring;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Paint;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Graffiti;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> GraffitiPaint;
    uint8 GraffitiSlot = 0;
    FVector Center = FVector::ZeroVector;
    double Radius = 0;
    uint32 Generation = 0;
    ELDCircleState Shown = ELDCircleState::NotOpen;
};
