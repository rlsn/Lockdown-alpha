#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDCaptureFence.generated.h"

class UInstancedStaticMeshComponent;

// final_cage: the fence on the spray zone boundary while it stands. Every machine, the authority included, builds it from
// the accepted public match view, so the authoritative and the predicted movement collide with the same posts. It blocks
// bodies only (pawn movement, dashes and forced moves); sight, projectiles and area queries pass through. The navigation
// mesh is static, so it never changes paths: AI read the public fence instead.
// The collision is a ring of hidden blocks that stays whole until the fence breaks; what players see is the art catalog's
// cage fence panel repeated along the ring, and panels fall away in a fixed shuffled order as its public integrity drops.
UCLASS(NotPlaceable, Transient)
class LOCKDOWN_API ALDCaptureFence : public AActor
{
    GENERATED_BODY()
public:
    ALDCaptureFence();
    // Raises one fence; false with a reason when the cooked block, the art panel or the circle is missing.
    bool Build(const FVector& CenterCm, double RadiusCm, int64 UntilMs, FString& Error);
    // Shows the panels its integrity (0..10000) still holds up; the collision is untouched.
    void SetIntegrity(int32 IntegrityBp);
    int64 GetUntilMs() const { return UntilMs; }
    const FVector& GetCenter() const { return Center; }
    double GetRadius() const { return Radius; }
    int32 GetPostCount() const;
    int32 GetShownPanelCount() const;
private:
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Posts;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Panels;
    TArray<FTransform> PanelTransforms;
    FVector Center = FVector::ZeroVector;
    double Radius = 0;
    int64 UntilMs = 0;
    int32 ShownIntegrityBp = -1;
};
