#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "LDBasketballMotionSet.generated.h"

class UAnimSequence;
class USkeletalMesh;
class UPoseSearchDatabase;

USTRUCT(BlueprintType)
struct FLDBasketballActionClip
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere) TObjectPtr<UAnimSequence> Animation;
    UPROPERTY(EditAnywhere) float Start = 0;
    UPROPERTY(EditAnywhere) float Release = .2f;
    UPROPERTY(EditAnywhere) float End = .6f;
    UPROPERTY(EditAnywhere) bool bLeftHand = false;
};

// Authored assets for the editor basketball test. No production combat state lives here.
UCLASS(BlueprintType)
class ULDBasketballMotionSet : public UDataAsset
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Art Test") static bool SetPalmSocket(USkeletalMesh* Mesh, FName Name, FVector Location);
    UFUNCTION(BlueprintCallable, Category="Art Test") static bool SetDatabaseAnimations(UPoseSearchDatabase* Database, const TArray<UAnimSequence*>& Animations);
    UFUNCTION(BlueprintPure, Category="Art Test") static int32 DatabasePoseCount(UPoseSearchDatabase* Database);
    UPROPERTY(EditAnywhere) TObjectPtr<UPoseSearchDatabase> Dribble;
    UPROPERTY(EditAnywhere) TObjectPtr<UPoseSearchDatabase> Idle;
    UPROPERTY(EditAnywhere) TObjectPtr<UPoseSearchDatabase> Handling;
    UPROPERTY(EditAnywhere) TObjectPtr<UPoseSearchDatabase> Carry;
    UPROPERTY(EditAnywhere) TArray<FLDBasketballActionClip> Passes;
    UPROPERTY(EditAnywhere) TArray<FLDBasketballActionClip> StandingShots;
    UPROPERTY(EditAnywhere) TArray<FLDBasketballActionClip> MovingShots;
    UPROPERTY(EditAnywhere) TArray<FLDBasketballActionClip> Crossovers;
    UPROPERTY(EditAnywhere) TArray<FLDBasketballActionClip> Catches;
    UPROPERTY(EditAnywhere) TArray<FLDBasketballActionClip> Stops;
    UPROPERTY(EditAnywhere) TArray<FLDBasketballActionClip> Turns;
};
