#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "LDPrototypeTuning.generated.h"

UCLASS(BlueprintType)
class LOCKDOWN_API ULDPrototypeTuning : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(VisibleAnywhere, Category="Migration") int32 SchemaVersion = 1;
    UPROPERTY(VisibleAnywhere, Category="Migration") FString BaselineRevision = TEXT("74d3d3e");
    UPROPERTY(EditDefaultsOnly, Category="Movement") float WalkSpeed = 180.0f;
    UPROPERTY(EditDefaultsOnly, Category="Movement") float SprintSpeed = 300.0f;
    UPROPERTY(EditDefaultsOnly, Category="Movement") float CapsuleRadius = 28.0f;
    UPROPERTY(EditDefaultsOnly, Category="Movement") float CapsuleHalfHeight = 90.0f;
    UPROPERTY(EditDefaultsOnly, Category="Movement") float DashDistance = 360.0f;
    UPROPERTY(EditDefaultsOnly, Category="Movement") float DashSeconds = 0.2f;
    UPROPERTY(EditDefaultsOnly, Category="Movement") float DashCooldownSeconds = 7.0f;
    UPROPERTY(EditDefaultsOnly, Category="Combat") int32 MaxHp = 100;
    UPROPERTY(EditDefaultsOnly, Category="Combat") int32 ResidentHp = 40;
    UPROPERTY(EditDefaultsOnly, Category="Combat") int32 BasicDamage = 20;
    UPROPERTY(EditDefaultsOnly, Category="Combat") float BasicReach = 96.0f;
    UPROPERTY(EditDefaultsOnly, Category="Combat") float BasicWindupSeconds = 0.15f;
    UPROPERTY(EditDefaultsOnly, Category="Combat") float BasicCadenceSeconds = 0.75f;
    UPROPERTY(EditDefaultsOnly, Category="Combat") int32 AssassinationDamage = 60;
    UPROPERTY(EditDefaultsOnly, Category="Combat") float AssassinationReach = 112.0f;
    UPROPERTY(EditDefaultsOnly, Category="Combat") float AssassinationWarningSeconds = 0.5f;
    UPROPERTY(EditDefaultsOnly, Category="Combat") float AssassinationCooldownSeconds = 18.0f;
    UPROPERTY(EditDefaultsOnly, Category="Combat") float ProtectionSeconds = 4.0f;
    UPROPERTY(EditDefaultsOnly, Category="Vision") float BodySight = 720.0f;
    UPROPERTY(EditDefaultsOnly, Category="Vision") float WardSight = 560.0f;
    UPROPERTY(EditDefaultsOnly, Category="Vision") float TowerSight = 600.0f;
    UPROPERTY(EditDefaultsOnly, Category="Vision") float HeartSight = 360.0f;
    UPROPERTY(EditDefaultsOnly, Category="Vision") float CellSize = 80.0f;
    UPROPERTY(EditDefaultsOnly, Category="Vision") float WardLifetimeSeconds = 60.0f;
    UPROPERTY(EditDefaultsOnly, Category="Vision") int32 WardHp = 20;
    UPROPERTY(EditDefaultsOnly, Category="Vision") int32 WardCap = 2;
    UPROPERTY(EditDefaultsOnly, Category="Vision") float WardPlacementRange = 96.0f;
    UPROPERTY(EditDefaultsOnly, Category="Interaction") float InteractionRange = 120.0f;
    UPROPERTY(EditDefaultsOnly, Category="Interaction") float SealPickupRange = 80.0f;
    UPROPERTY(EditDefaultsOnly, Category="Interaction") float ContestRange = 240.0f;
    UPROPERTY(EditDefaultsOnly, Category="Interaction") float WorkSeconds = 3.0f;
    UPROPERTY(EditDefaultsOnly, Category="Interaction") float WorkCooldownSeconds = 20.0f;
    UPROPERTY(EditDefaultsOnly, Category="Interaction") int32 CourierGold = 25;
    UPROPERTY(EditDefaultsOnly, Category="Interaction") int32 CourierXp = 15;
    UPROPERTY(EditDefaultsOnly, Category="Interaction") float ClaimSeconds = 10.0f;

    bool Validate(FString& Error) const;
};
