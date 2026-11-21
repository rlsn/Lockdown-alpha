#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Character/LDBodyMotion.h"
#include "LDBasketballMotion.generated.h"

class UAnimSequence;
class USkeletalMesh;
class UStaticMesh;
class UMaterialInterface;

USTRUCT()
struct LOCKDOWN_API FLDBasketballActionClip
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere) TObjectPtr<UAnimSequence> Animation;
    UPROPERTY(VisibleAnywhere) float Start = 0;
    UPROPERTY(VisibleAnywhere) float Release = 0;
    UPROPERTY(VisibleAnywhere) float End = 0;
    UPROPERTY(VisibleAnywhere) bool bLeftHand = false;
};

// Presentation assets only. Capsules, input, attack cadence and hit/skill resolution have their existing owners.
UCLASS()
class LOCKDOWN_API ULDBasketballMotionSet : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(VisibleAnywhere) FString SourceSha256;
    UPROPERTY(VisibleAnywhere) TArray<FLDBasketballActionClip> Idle;
    UPROPERTY(VisibleAnywhere) TArray<FLDBasketballActionClip> Run;
    UPROPERTY(VisibleAnywhere) TArray<FLDBasketballActionClip> Handling;
    UPROPERTY(VisibleAnywhere) TArray<FLDBasketballActionClip> Passes;
    UPROPERTY(VisibleAnywhere) TArray<FLDBasketballActionClip> StandingShots;
    UPROPERTY(VisibleAnywhere) TArray<FLDBasketballActionClip> MovingShots;
    UPROPERTY(VisibleAnywhere) TArray<FLDBasketballActionClip> Turns;
    UPROPERTY(VisibleAnywhere) TArray<FLDBasketballActionClip> Crossovers;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMesh> BallMesh;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UMaterialInterface> BallMaterial;
    UPROPERTY(VisibleAnywhere) FVector RightPalm = FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere) FVector LeftPalm = FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere) FVector RightPalmNormal = FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere) FVector LeftPalmNormal = FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere) float BallRadiusCm = 0;
    UPROPERTY(VisibleAnywhere) float RunSpeedCmPerSec = 0;
    bool Validate(const USkeletalMesh& Body, FString& Error) const;
};

namespace LDBasketballMotion
{
    // Match the authored release exactly to the existing authority/prediction action's impact delay.
    // Recovery uses the same scale; no new attack interval is introduced by the animation.
    LOCKDOWN_API float SampleTime(const FLDBasketballActionClip& Clip, float Age, float ImpactSeconds);
    LOCKDOWN_API float Duration(const FLDBasketballActionClip& Clip, float ImpactSeconds);
}
