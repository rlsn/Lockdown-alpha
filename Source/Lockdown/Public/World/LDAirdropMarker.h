#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Framework/LDCaptureTypes.h"
#include "LDAirdropMarker.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;

// Presentation of the current airdrop (天降外卖, D26 #79): a flat painted ring at the approved AIRDROP.PickupRadiusCm
// around the public drop point while it is announced, plus the art catalog's crate once it has landed (#97). It only
// mirrors the accepted public airdrop view; it is never replicated and has no collision, navigation, sight or height
// effect.
UCLASS(NotPlaceable, Transient)
class LOCKDOWN_API ALDAirdropMarker : public AActor
{
    GENERATED_BODY()
public:
    ALDAirdropMarker();
    // Lays the ring and crate for one drop of one match generation; false with a reason when the cooked mesh or
    // material or the art crate is missing or the view names no drop.
    bool Build(const FLDAirdropView& View, double RadiusCm, uint32 InGeneration, FString& Error);
    // Mirrors the phase of the same drop: the crate appears on landing; a claimed drop shows nothing.
    void ShowPhase(ELDAirdropPhase Phase);
    static FLinearColor Color();
    uint32 GetGeneration() const { return Generation; }
    uint64 GetSerial() const { return Serial; }
    ELDAirdropPhase GetPhase() const { return Shown; }
    const FVector& GetCenter() const { return Center; }
    double GetRadius() const { return Radius; }
    bool IsCrateVisible() const;
    bool IsPresentationOnly() const;
private:
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Ring;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Crate;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> RingPaint;
    FVector Center = FVector::ZeroVector;
    double Radius = 0;
    uint32 Generation = 0;
    uint64 Serial = 0;
    ELDAirdropPhase Shown = ELDAirdropPhase::None;
};
