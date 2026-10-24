#pragma once
#include "CoreMinimal.h"
#include "Inventory/LDInventoryTypes.h"
#include "LDSupplyRequest.generated.h"
USTRUCT()
struct FLDSupplyRequest
{
    GENERATED_BODY()
    uint32 Match=0,Life=0,Token=0,InventoryRevision=0;
    uint8 Site=2;
    ELDItemKind Kind=ELDItemKind::Book;
    ELDSkill Skill=ELDSkill::None;
    FString Digest;
    bool NetSerialize(FArchive& Ar,UPackageMap* Map,bool& Success);
};
template<>struct TStructOpsTypeTraits<FLDSupplyRequest>:TStructOpsTypeTraitsBase2<FLDSupplyRequest>{enum{WithNetSerializer=true};};
