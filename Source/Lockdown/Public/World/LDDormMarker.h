#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDDormMarker.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;

// Placeholder presentation of one dorm (宿舍, D21 #79): a flat painted ring on the registered ground at the approved
// DORM.RadiusCm around the dorm anchor. Each machine builds it from its own world data; it is never replicated and has
// no collision, navigation, sight or height effect. Final art belongs to the presentation ticket.
UCLASS(NotPlaceable, Transient)
class LOCKDOWN_API ALDDormMarker : public AActor
{
    GENERATED_BODY()
public:
    ALDDormMarker();
    // Lays the ring for one match generation; false with a reason when the cooked mesh or material is missing.
    bool Build(FName InDormId, const FVector& CenterCm, double RadiusCm, uint32 InGeneration, FString& Error);
    static FLinearColor Color();
    FName GetDormId() const { return DormId; }
    uint32 GetGeneration() const { return Generation; }
    const FVector& GetCenter() const { return Center; }
    double GetRadius() const { return Radius; }
    int32 GetSegmentCount() const;
    bool IsPresentationOnly() const;
private:
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Ring;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Paint;
    FName DormId;
    FVector Center = FVector::ZeroVector;
    double Radius = 0;
    uint32 Generation = 0;
};
