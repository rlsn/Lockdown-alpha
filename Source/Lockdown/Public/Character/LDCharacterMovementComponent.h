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
    virtual void OnClientCorrectionReceived(FNetworkPredictionData_Client_Character& ClientData,float TimeStamp,FVector NewLocation,FVector NewVelocity,FMovementBaseInterfaceData* NewBase,FName NewBaseBoneName,bool bHasBase,bool bBaseRelativePosition,uint8 ServerMovementMode,FVector ServerGravityDirection) override;
    uint32 CorrectionCount=0;
    double MaxCorrectionCm=0;
};
