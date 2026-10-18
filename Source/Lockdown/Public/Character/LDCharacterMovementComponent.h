#pragma once
#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "LDCharacterMovementComponent.generated.h"

UCLASS()
class LOCKDOWN_API ULDCharacterMovementComponent : public UCharacterMovementComponent
{
    GENERATED_BODY()
public:
    bool bWantsSprint = false;
    float SprintSpeed = 300.0f;
    bool bCombatWalkOnly = false;
    bool StartAuthorityDash(const FVector& Direction);
    virtual float GetMaxSpeed() const override;
    virtual void UpdateFromCompressedFlags(uint8 Flags) override;
    virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
};
