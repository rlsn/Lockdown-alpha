#pragma once
#include "CoreMinimal.h"
#include "LDLoadoutRequest.generated.h"
USTRUCT()
struct FLDLoadoutRequest
{
    GENERATED_BODY()
    UPROPERTY() uint8 Version=1;
    UPROPERTY() uint32 Match=0;
    UPROPERTY() uint32 Life=0;
    UPROPERTY() uint32 Token=0;
    UPROPERTY() uint32 InventoryRevision=0;
    UPROPERTY() uint32 BuildRevision=0;
    UPROPERTY() uint32 ItemRevision=0;
    UPROPERTY() uint8 Command=0;
    UPROPERTY() uint8 Binding=0;
    UPROPERTY() FGuid Item;
    UPROPERTY() FString EquipmentDigest;
    bool NetSerialize(FArchive& Ar,class UPackageMap* Map,bool& Success);
};
template<> struct TStructOpsTypeTraits<FLDLoadoutRequest>:TStructOpsTypeTraitsBase2<FLDLoadoutRequest>{enum{WithNetSerializer=true};};
