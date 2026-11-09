#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Art/LDArtTestCharacter.h"
#include "Art/LDBasketballPreview.h"
#include "LDChefArtTest.generated.h"

class UStaticMeshComponent;
UCLASS(Transient)
class ULDChefPreviewAnimInstance : public UAnimInstance
{
    GENERATED_BODY()
protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) override;
};

// Local art preview only. Uses CharacterMovement and the existing test target.
UCLASS()
class ALDChefArtTestCharacter : public ACharacter
{
    GENERATED_BODY()
public:
    ALDChefArtTestCharacter();
    virtual void BeginPlay() override;
    virtual void Tick(float Delta) override;
    UFUNCTION(BlueprintCallable) bool SlashAt(FVector Aim);
    UFUNCTION(BlueprintCallable) void SetPreviewAttackSpeed(float Rate);
    UFUNCTION(BlueprintCallable) void SetSprinting(bool Enabled);
    UFUNCTION(BlueprintCallable) void ResetCloth();
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<USkeletalMeshComponent> Cape;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Ladle;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USpringArmComponent> CameraArm;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;
    UPROPERTY(EditAnywhere) TObjectPtr<UAnimSequence> IdleAnimation;
    UPROPERTY(EditAnywhere) TObjectPtr<UAnimSequence> RunAnimation;
    UPROPERTY(EditAnywhere) TObjectPtr<UAnimSequence> SlashAnimation;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) float RunWeight=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) float ActionWeight=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) float ActionPhase=-1;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) float PoseClock=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) float RunClock=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) float ClothSpeed=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) float PreviewAttacksPerSecond=1;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 SlashCount=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 HitCount=0;
private:
    void ApplyPreviewHit();
    float ActionDuration=1;
    bool bHitApplied=false;
};

UCLASS()
class ALDChefArtTestController : public ALDArtTestController
{
    GENERATED_BODY()
public:
    virtual void PlayerTick(float Delta) override;
    UFUNCTION(BlueprintCallable) bool SwitchPreviewCharacter();
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 SwitchCount=0;
private:
    UPROPERTY(Transient) TObjectPtr<APawn> KobePawn;
    UPROPERTY(Transient) TObjectPtr<ALDChefArtTestCharacter> ChefPawn;
    void ChefAttack(bool Nearest);
};

UCLASS()
class ALDChefArtTestHUD : public ALDBasketballPreviewHUD
{
    GENERATED_BODY()
public: virtual void DrawHUD() override;
};

UCLASS()
class ALDChefArtTestGameMode : public ALDArtTestGameMode
{
    GENERATED_BODY()
public: ALDChefArtTestGameMode();
};
