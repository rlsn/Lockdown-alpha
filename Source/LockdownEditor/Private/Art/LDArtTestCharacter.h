#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "Art/LDBasketballMotionSet.h"
#include "LDArtTestCharacter.generated.h"

class UAnimSequence;
class UCameraComponent;
class USpringArmComponent;
class ALDBasketballPreviewBall;

UENUM(BlueprintType)
enum class ELDBasketballAction : uint8 { None, Pass, Shot, Crossover, Catch, Stop, Turn };

// Asset inspection in L_ArtTest only. These classes live in the Editor module and are not game modes for shipping.
UCLASS()
class ALDArtTestCharacter : public ACharacter
{
    GENERATED_BODY()
public:
    ALDArtTestCharacter();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void OnConstruction(const FTransform& Transform) override;
    UFUNCTION(BlueprintCallable) bool ThrowBasketball(FVector AimPoint);
    UFUNCTION(BlueprintCallable) bool ShootBasketball(FVector AimPoint);
    UFUNCTION(BlueprintCallable) bool DashBasketball(FVector Direction);
    UFUNCTION(BlueprintCallable) void SetPreviewAttackSpeed(float AttacksPerSecond);
    UFUNCTION(BlueprintCallable) void SetPreviewMoveSpeed(float CentimetersPerSecond);
    FVector GetBasketballHand(bool bLeft) const;
    FVector KeepBallOutsideHands(FVector Position) const;
    void ReceiveBasketball();
    float GetActionSampleTime() const;

    UPROPERTY(EditAnywhere, Category="Art Test") TObjectPtr<USkeletalMesh> PreviewMesh;
    UPROPERTY(EditAnywhere, Category="Art Test") TObjectPtr<UAnimSequence> IdleAnimation;
    UPROPERTY(EditAnywhere, Category="Art Test") TObjectPtr<UAnimSequence> RunAnimation;
    UPROPERTY(EditAnywhere, Category="Art Test|Basketball") TObjectPtr<UAnimSequence> DribbleIdleAnimation;
    UPROPERTY(EditAnywhere, Category="Art Test|Basketball") TObjectPtr<UAnimSequence> DribbleRunAnimation;
    UPROPERTY(EditAnywhere, Category="Art Test|Basketball") TObjectPtr<UAnimSequence> ThrowAnimation;
    UPROPERTY(EditAnywhere, Category="Art Test|Basketball") TObjectPtr<UStaticMesh> BasketballMesh;
    UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly) TObjectPtr<ALDBasketballPreviewBall> Basketball;
    UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly) float DribblePhase = 0;
    UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly) float AttackTime = -1;
    UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly) float UpperBodyAimYaw = 0;
    UPROPERTY(EditAnywhere, Category="Art Test|Basketball") TObjectPtr<ULDBasketballMotionSet> MotionSet;
    // Animation preview rate, attacks/second. One captured value drives pose, release, flight and return.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Art Test|Basketball", meta=(ClampMin="0.2",ClampMax="5")) float PreviewAttacksPerSecond = 2.f;
    UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly) ELDBasketballAction Action = ELDBasketballAction::None;
    UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly) float ActionDuration = 0;
    UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly) float AttackCycle = .5f;
    UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly) float BallReleaseTime = .06f;
    UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly) float ActionWeight = 0;
    UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly) float DashTime = -1;
    UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly) int32 DashCount = 0;
    UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly) int32 SkillCount = 0;
    UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly) FString MatchedAnimation;
    UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly) float MatchedTime = 0;
    UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly) float MatchCost = 0;
    UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly) float LocomotionPlayRate = 1.f;
    UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly) int32 TurnCount = 0;
    UPROPERTY(Transient) bool bMoving = false;
    UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly) int32 MatchChanges = 0;
    UPROPERTY(Transient) FLDBasketballActionClip ActiveClip;
    UPROPERTY(Transient) bool bHandlingVariation = false;
    UPROPERTY(Transient) bool bCarryPose = false;
    UPROPERTY(Transient) bool bLeftHand = false;
    UPROPERTY(EditAnywhere, Category="Art Test", meta=(ClampMin="1")) float PreviewHeightCm = 180.f;
    UPROPERTY(EditAnywhere, Category="Art Test") FRotator MeshOrientation = FRotator(0,-90,0);
    // The source art catalog's measured run speed, used only to match animation cadence in the preview.
    UPROPERTY(EditAnywhere, Category="Art Test", meta=(ClampMin="1")) float AnimationRunSpeed = 284.3f;
    UPROPERTY(VisibleAnywhere, Category="Art Test") TObjectPtr<USpringArmComponent> PreviewCameraArm;
    UPROPERTY(VisibleAnywhere, Category="Art Test") TObjectPtr<UCameraComponent> PreviewCamera;
private:
    void UpdatePreviewMesh();
    bool BeginBallAction(FVector AimPoint, bool bSkill);
    void BeginClip(const FLDBasketballActionClip& Clip, ELDBasketballAction Kind, float Duration);
    bool bMotionReady = false;
    bool bMotionFailed = false;
    float MotionLoadStarted = 0;
    float AttackYaw = 0;
    float NextAttackAt = 0;
    float NextDashAt = 0;
    float IdleTime = 0;
    float TurnStartYaw = 0;
    float TurnYawDelta = 0;
    float NextTurnAt = 0;
    float DashDuration = .38f;
    float DashSpeed = 0;
    FVector DashDirection = FVector::ZeroVector;
    FVector PreviousClothVelocity = FVector::ZeroVector;
    FVector ClothOffset = FVector::ZeroVector;
    FVector ClothOffsetVelocity = FVector::ZeroVector;
    int32 Variation = 0;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> PlayingAnimation;
};

UCLASS()
class ALDArtTestController : public APlayerController
{
    GENERATED_BODY()
public:
    ALDArtTestController();
    virtual void BeginPlay() override;
    virtual void PlayerTick(float DeltaSeconds) override;
private:
    void MoveToCursor();
    void StopPreviewMovement();
    void AttackCursor(bool bNearestTarget, bool bSkill = false);
};

UCLASS()
class ALDArtTestGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    ALDArtTestGameMode();
};
