#pragma once
#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "LDFlightTrailComponent.generated.h"

class UStaticMeshComponent;

// A chalk-dust trail behind a visible flight: puffs strung out backward along the host actor's facing, as long as its
// recent speed, and gone while it stands still. It reads only its own host's motion.
UCLASS()
class LOCKDOWN_API ULDFlightTrailComponent : public USceneComponent
{
    GENERATED_BODY()
public:
    ULDFlightTrailComponent();
    virtual void OnRegister() override;
    virtual void TickComponent(float Delta, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
private:
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Puffs;
    FVector LastWorld = FVector::ZeroVector;
    float SpeedCmPerSec = 0;
    bool bHasLast = false, bTinted = false;
};
