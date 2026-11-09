#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "GameFramework/Actor.h"
#include "GameFramework/HUD.h"
#include "LDBasketballPreview.generated.h"

class ALDArtTestCharacter;
class UStaticMeshComponent;
class UCapsuleComponent;

// Editor art preview only: these actors never participate in the production combat ledger.
UCLASS(Transient)
class ULDBasketballPreviewAnimInstance : public UAnimInstance
{
    GENERATED_BODY()
protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
};

UENUM()
enum class ELDBasketballPreviewState : uint8 { Dribble, Gather, Outbound, ToBounce, Returning };

UCLASS()
class ALDBasketballPreviewBall : public AActor
{
    GENERATED_BODY()
public:
    ALDBasketballPreviewBall();
    virtual void Tick(float DeltaSeconds) override;
    void Initialize(ALDArtTestCharacter* InHero);
    bool ThrowAt(const FVector& Point);
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> BallMesh;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) ELDBasketballPreviewState State = ELDBasketballPreviewState::Dribble;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 ThrowCount = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 HitCount = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 ReturnCount = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 BounceCount = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float LastReleaseDelay = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float LastCycleDuration = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FVector LastBouncePoint = FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FVector ImpactHeroPosition = FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FVector LastImpactPoint = FVector::ZeroVector;
private:
    FVector HandPosition() const;
    void StartBounce();
    void AdvanceFlight(float DeltaSeconds);
    float GroundHeight(const FVector& Position) const;
    void UpdateDribble(float DeltaSeconds, const FVector& Hand);
    UPROPERTY() TObjectPtr<ALDArtTestCharacter> Hero;
    FVector Start, Goal, GatherStart, BouncePoint, ReturnStart;
    float FlightTime = 0;
    float FlightDuration = 0;
    float Cycle = .5f;
    float ReleaseDelay = .06f;
    float Age = 0;
    float CatchBlend = 1;
    FVector CatchOffset = FVector::ZeroVector;
    bool bSkill = false;
    bool bThrowLeft = false;
};

UCLASS()
class ALDArtTestMonster : public AActor
{
    GENERATED_BODY()
public:
    ALDArtTestMonster();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual float TakeDamage(float Damage, const FDamageEvent& Event, AController* Instigator, AActor* Causer) override;
    UFUNCTION(BlueprintCallable) void ResetTarget();
    UPROPERTY(VisibleAnywhere) TObjectPtr<UCapsuleComponent> HitBody;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Appearance;
    UPROPERTY(EditAnywhere, Category="Art Test") float PreviewMaxHealth = 100;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float Health = 100;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 TotalHits = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 DefeatCount = 0;
private:
    float HitTime = -10;
    float DefeatTime = -10;
};

UCLASS()
class ALDBasketballPreviewHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
};
