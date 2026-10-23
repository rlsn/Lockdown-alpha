#pragma once
#include "CoreMinimal.h"
#include "Inventory/LDInventoryTypes.h"

namespace LDEconomy
{
    struct FMarketGood { ELDItemKind Kind=ELDItemKind::Fiber; int32 Target=0,Cap=0,Base=0; };
    struct FMarketDefinition { FName Id; int64 Gold=0; TArray<FMarketGood> Goods; };
    struct LOCKDOWN_API FCatalog
    {
        int32 Version=0;
        FString Checksum;
        TArray<FMarketDefinition> Markets;
        int32 Nodes=0,Charges=0;
        // Decode to a candidate; a failed reload leaves the existing catalog intact.
        bool Parse(const FString& Json,FString& Error);
        bool Load(FString& Error);
    };
    LOCKDOWN_API int32 Ask(const FMarketGood& Good,int32 Stock,int32 Numerator=1,int32 Denominator=1);
    LOCKDOWN_API int32 Bid(const FMarketGood& Good,int32 Stock,int32 Numerator=1,int32 Denominator=1);
}
