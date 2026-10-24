#pragma once
#include "CoreMinimal.h"
#include "LDTacticalRequest.generated.h"

// Bounded new-profile RPC. Legacy action/build serializers retain their path.
USTRUCT()
struct FLDTacticalRequest
{
    GENERATED_BODY()
    UPROPERTY() uint8 Schema=7;
    UPROPERTY() uint32 Match=0;
    UPROPERTY() uint32 Life=0;
    UPROPERTY() uint32 Token=0;
    UPROPERTY() uint32 BuildRevision=0;
    UPROPERTY() uint8 Slot=0;
    UPROPERTY() FString CatalogDigest;
    UPROPERTY() FGuid Observed;
    UPROPERTY() FVector_NetQuantizeNormal Direction=FVector::ForwardVector;
    bool NetSerialize(FArchive& Ar,class UPackageMap* Map,bool& Success);
};
template<> struct TStructOpsTypeTraits<FLDTacticalRequest>:TStructOpsTypeTraitsBase2<FLDTacticalRequest>{enum{WithNetSerializer=true};};
