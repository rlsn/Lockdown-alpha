#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "LDCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class ULDHumanoidVisualComponent;
class ULDPrototypeTuning;
class UPointLightComponent;

UCLASS()
class LOCKDOWN_API ALDCharacter : public ACharacter
{
    GENERATED_BODY()
public:
    explicit ALDCharacter(const FObjectInitializer& ObjectInitializer);
    virtual void BeginPlay() override;
    virtual void Tick(float Delta) override;
    void SetSprint(bool bSprint);
    void SetFacing(float Yaw);
    void Zoom(float Axis);
    UPROPERTY(VisibleAnywhere) TObjectPtr<ULDHumanoidVisualComponent> Body;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USpringArmComponent> CameraArm;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> PortableGlow;
    void SetPortableLight(uint8 Mode);
    UPROPERTY() TObjectPtr<ULDPrototypeTuning> Tuning;
    UPROPERTY(ReplicatedUsing=OnRep_Presentation) uint8 PresentationKey = 0;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
protected:
    UFUNCTION() void OnRep_Presentation();
};

UCLASS()
class LOCKDOWN_API ALDResident : public ALDCharacter
{
    GENERATED_BODY()
public:
    explicit ALDResident(const FObjectInitializer& ObjectInitializer);
    virtual void Tick(float Delta) override;
private:
    float NextDecision = 0;
};
