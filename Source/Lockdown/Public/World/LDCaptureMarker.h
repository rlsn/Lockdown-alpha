#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Framework/LDCaptureTypes.h"
#include "LDCaptureMarker.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;

// A flat painted ring of chords on the registered ground, shared by the capture and dorm boundaries. Presentation only:
// no collision, overlap, navigation or shadow, and its dimensions never change an approved radius or rule.
namespace LDGroundRing
{
    // Constructor-time: the ring component on the cooked engine cube with every gameplay influence off.
    LOCKDOWN_API UInstancedStaticMeshComponent* Create(AActor& Owner, FName Name);
    // Lays the chords of a closed circle of RadiusCm around the owner's origin; false when it cannot be drawn.
    LOCKDOWN_API bool Lay(UInstancedStaticMeshComponent* Ring, double RadiusCm);
    // Tints the ring with the generated character material; false with a reason when it is missing.
    LOCKDOWN_API bool Paint(AActor& Owner, UInstancedStaticMeshComponent& Ring, UMaterialInstanceDynamic*& OutPaint, FString& Error);
    LOCKDOWN_API bool IsPresentationOnly(const UInstancedStaticMeshComponent* Ring);
}

// Local presentation of the single capture circle (旗杆): a flat painted ring on the registered ground at the
// approved radius around the capture anchor. Each machine builds it from its own world data and only mirrors the
// accepted public circle state; it is never replicated and has no collision, navigation, sight or height effect.
UCLASS(NotPlaceable, Transient)
class LOCKDOWN_API ALDCaptureMarker : public AActor
{
    GENERATED_BODY()
public:
    ALDCaptureMarker();
    // Lays the ring for one match generation; false with a reason when the cooked mesh or material is missing.
    bool Build(const FVector& CenterCm, double RadiusCm, uint32 InGeneration, FString& Error);
    void ShowState(ELDCircleState State);
    static FLinearColor ColorFor(ELDCircleState State);
    uint32 GetGeneration() const { return Generation; }
    const FVector& GetCenter() const { return Center; }
    double GetRadius() const { return Radius; }
    ELDCircleState GetShownState() const { return Shown; }
    int32 GetSegmentCount() const;
    // True while the ring is pure presentation: no collision, overlap or navigation influence.
    bool IsPresentationOnly() const;
private:
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Ring;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Paint;
    FVector Center = FVector::ZeroVector;
    double Radius = 0;
    uint32 Generation = 0;
    ELDCircleState Shown = ELDCircleState::NotOpen;
};
