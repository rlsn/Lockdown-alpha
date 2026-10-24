#pragma once
#include "CoreMinimal.h"
#include "LDGearTypes.generated.h"
USTRUCT()
struct FLDGearStats
{
    GENERATED_BODY()
    UPROPERTY() TArray<int32> Primary={0,0,0,0,0,0};
    UPROPERTY() TArray<int32> Grants;
    UPROPERTY() double WeaponBase=42;
    UPROPERTY() int32 Reach=96;
    UPROPERTY() int32 Interval=1200;
    UPROPERTY() double Armor=0;
    UPROPERTY() double MagicResist=0;
    UPROPERTY() int32 Movement=0;
    UPROPERTY() int32 RoadMovement=0;
    UPROPERTY() int32 MPRegen=0;
    UPROPERTY() int32 Capacity=0;
    UPROPERTY() int32 SpellPercent=0;
    UPROPERTY() int32 CastRange=0;
    UPROPERTY() uint8 ControlPotency=0;
    UPROPERTY() int32 Detector=0;
};
