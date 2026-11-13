#pragma once

#include "CoreMinimal.h"
#include "Framework/LDGameInstance.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "Vision/LDVisionDirector.h"
#include "FogMistLab.generated.h"

class UCameraComponent;
class ULDHumanoidVisualComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UTexture2D;
class APostProcessVolume;

UCLASS()
class FOGMISTLAB_API UFogMistLabGameInstance : public ULDGameInstance
{
    GENERATED_BODY()
public:
    virtual AGameModeBase* CreateGameModeForURL(FURL URL, UWorld* World) override;
    virtual void OnStart() override;
};

UCLASS()
class FOGMISTLAB_API AFogMistLabCharacter : public ACharacter
{
    GENERATED_BODY()
public:
    AFogMistLabCharacter();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;
    void Zoom(float Axis);
    UPROPERTY(VisibleAnywhere) TObjectPtr<ULDHumanoidVisualComponent> Visual;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;
    UPROPERTY(BlueprintReadOnly) uint8 CreatureId = 0;
    UPROPERTY(BlueprintReadOnly) float ArmLength = 2250;
};

UCLASS()
class FOGMISTLAB_API AFogMistLabController : public APlayerController
{
    GENERATED_BODY()
public:
    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;
    virtual void PlayerTick(float DeltaTime) override;
    UFUNCTION(BlueprintCallable) bool NavigateTo(FVector Destination);
    UFUNCTION(BlueprintCallable) void StopMoving();
    UPROPERTY(BlueprintReadOnly) bool bFollowingPath = false;
private:
    void NavigateUnderCursor();
    void Zoom(float Axis);
    TArray<FVector> Path;
    int32 PathIndex = 0;
};

UCLASS()
class FOGMISTLAB_API AFogMistLabGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AFogMistLabGameMode();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
    UPROPERTY(BlueprintReadOnly) bool bLabReady = false;
    UPROPERTY(BlueprintReadOnly) int32 MonsterCount = 0;
    UPROPERTY(BlueprintReadOnly) int32 VisibleMonsterCount = 0;
    UPROPERTY(BlueprintReadOnly) int32 FogUpdates = 0;
    UPROPERTY(BlueprintReadOnly) float LastFogBuildMs = 0;
    UPROPERTY(BlueprintReadOnly) FString StartupError;
    UPROPERTY(BlueprintReadOnly) TObjectPtr<UMaterialInstanceDynamic> LiveFog;
private:
    bool InitializeLab(FString& Error);
    bool BuildField(const FVector& Foot, FString& Error);
    void UploadField(float DeltaSeconds);
    void Fail(const FString& Error);
    static constexpr int32 FieldSide = 128;
    static constexpr float DistanceRangeCm = 900;
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    TSharedPtr<const FLDDemoWorldGeometry> Geometry;
    FLDSightGeometry Sight;
    FVector LastFoot = FVector::ZeroVector;
    float SampleElapsed = .1f;
    TArray<float> TargetField, ShownField;
    UPROPERTY() TObjectPtr<UTexture2D> FieldTexture;
    UPROPERTY() TObjectPtr<UMaterialInterface> TuningMaterial;
    UPROPERTY() TObjectPtr<APostProcessVolume> FogVolume;
    UPROPERTY() TArray<TObjectPtr<AFogMistLabCharacter>> Monsters;
};
