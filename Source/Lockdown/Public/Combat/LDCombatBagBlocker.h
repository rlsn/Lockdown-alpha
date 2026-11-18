#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDCombatBagBlocker.generated.h"

class UBoxComponent;

// Spec §5 N4: the server's collision of a placed school bag or of the wall between two bags. It blocks bodies (the Pawn
// channel) and nothing else: no sight, camera, navigation or rendering. It never replicates; owning clients collide with
// the observation proxy of the same bag while they see it (ALDObservedProjectileProxy, Bag and BagWall looks).
UCLASS(NotBlueprintable)
class LOCKDOWN_API ALDCombatBagBlocker : public AActor
{
    GENERATED_BODY()
public:
    ALDCombatBagBlocker();
    // A box of HalfLengthCm along Heading, HalfWidthCm across it and Height tall, standing on Foot.
    void Shape(const FVector& Foot, const FVector2D& Heading, double HalfLengthCm, double HalfWidthCm, double Height);
    static constexpr double HeightCm = 120.;

private:
    UPROPERTY() TObjectPtr<UBoxComponent> Box;
};
