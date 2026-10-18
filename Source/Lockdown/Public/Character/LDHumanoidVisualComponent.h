#pragma once
#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "LDHumanoidVisualComponent.generated.h"

class UStaticMeshComponent;
UCLASS()
class LOCKDOWN_API ULDHumanoidVisualComponent : public USceneComponent
{
    GENERATED_BODY()
public:
    ULDHumanoidVisualComponent();
    virtual void OnRegister() override;
    void ApplyPalette(uint8 Key);
    void Animate(const FVector& Velocity, float Delta);
private:
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Parts;
    float Phase = 0;
};
