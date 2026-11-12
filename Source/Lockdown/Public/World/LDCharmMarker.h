#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Framework/LDCaptureTypes.h"
#include "LDCharmMarker.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;

// Presentation of one lying field charm (符咒, #198 outer §7, #226): a flat painted ring at the approved
// CHARM.PickupRadiusCm around its public airdrop point and a floating diamond token above it, both in the kind's map
// colour. It only mirrors one charm of the accepted public match view; it is never replicated and has no collision,
// navigation, sight or height effect.
UCLASS(NotPlaceable, Transient)
class LOCKDOWN_API ALDCharmMarker : public AActor
{
    GENERATED_BODY()
public:
    ALDCharmMarker();
    // Lays the ring and token for one charm of one match generation; false with a reason when the cooked mesh or
    // material is missing or the view names no lying charm.
    bool Build(const FLDCharmView& View, double RadiusCm, const FLinearColor& Tint, uint32 InGeneration, FString& Error);
    uint32 GetGeneration() const { return Generation; }
    uint64 GetSerial() const { return Serial; }
    ELDCharmKind GetKind() const { return Kind; }
    const FVector& GetCenter() const { return Center; }
    double GetRadius() const { return Radius; }
    const FLinearColor& GetTint() const { return Tint; }
    int32 GetSegmentCount() const;
    bool IsTokenVisible() const;
    bool IsPresentationOnly() const;
private:
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Ring;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Token;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Paint;
    FVector Center = FVector::ZeroVector;
    double Radius = 0;
    FLinearColor Tint = FLinearColor::White;
    uint32 Generation = 0;
    uint64 Serial = 0;
    ELDCharmKind Kind = ELDCharmKind::None;
};
