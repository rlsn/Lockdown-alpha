#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDCaptureFence.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;

// final_cage: the fence on the spray zone boundary while it stands. Every machine, the authority included, builds it from
// the accepted public match view, so the authoritative and the predicted movement collide with the same posts. It blocks
// bodies only (pawn movement, dashes and forced moves); sight, projectiles and area queries pass through. The navigation
// mesh is static, so it never changes paths: AI read the public fence instead.
UCLASS(NotPlaceable, Transient)
class LOCKDOWN_API ALDCaptureFence : public AActor
{
    GENERATED_BODY()
public:
    ALDCaptureFence();
    // Raises the posts of one fence; false with a reason when the cooked mesh is missing or the circle is invalid.
    bool Build(const FVector& CenterCm, double RadiusCm, int64 UntilMs, FString& Error);
    int64 GetUntilMs() const { return UntilMs; }
    const FVector& GetCenter() const { return Center; }
    double GetRadius() const { return Radius; }
    int32 GetPostCount() const;
private:
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Posts;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Paint;
    FVector Center = FVector::ZeroVector;
    double Radius = 0;
    int64 UntilMs = 0;
};
