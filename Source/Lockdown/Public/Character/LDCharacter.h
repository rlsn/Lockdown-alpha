#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "LDCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class ULDHumanoidVisualComponent;
class FLDDemoContentSnapshot;

UCLASS()
class LOCKDOWN_API ALDCharacter : public ACharacter
{
    GENERATED_BODY()
public:
    explicit ALDCharacter(const FObjectInitializer& ObjectInitializer);
    virtual void BeginPlay() override;
    virtual void Tick(float Delta) override;
    bool ConfigureDemoMovement(const FLDDemoContentSnapshot& Content, FString& Error);
    void SetFacing(float Yaw);
    void Zoom(float Axis);
    // The one zoom rule for the body's camera arm and for the dead owner's static-map camera.
    static float ZoomedArmLength(float ArmLength, float Axis);
    // World pose of a camera on this class's default arm (fixed angle and world offset) centred on Origin.
    static FTransform DefaultArmCameraPose(const FVector& Origin, float ArmLength);
    UPROPERTY(VisibleAnywhere) TObjectPtr<ULDHumanoidVisualComponent> Body;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USpringArmComponent> CameraArm;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;
    UPROPERTY(ReplicatedUsing=OnRep_Presentation) uint8 PresentationKey = 0;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
protected:
    UFUNCTION() void OnRep_Presentation();
};
