#pragma once
#include "CoreMinimal.h"
#include "Inventory/LDInventoryTypes.h"

namespace LDEconomy
{
    struct FMarketGood { ELDItemKind Kind=ELDItemKind::Fiber; int32 Target=0,Cap=0,Base=0; };
    struct FMarketDefinition { FName Id; int64 Gold=0; TArray<FMarketGood> Goods; };
    struct FInstitutionRule {uint8 Service=0;ELDItemKind Kind=ELDItemKind::Fiber;int32 Cost=0;bool Producer=false;};
    struct LOCKDOWN_API FCatalog
    {
        int32 Version=0;
        FString Checksum;
        TArray<FMarketDefinition> Markets;
        int32 Nodes=0,Charges=0;
        int32 FiberPrice=0,OrePrice=0;
        int32 GatherSeconds=0,Range=0,ToolkitFee=0,ToolkitSeconds=0,CoreFee=0,CoreSeconds=0;
        int32 SurveyScore=0,AnalyzeScore=0,CouponDiscount=0,CouponSeconds=0;
        uint32 RecipeRevision=1;
        TArray<int32> InstitutionTimes;
        TArray<FInstitutionRule> Institutions; // funded producers, then consumers
        int32 ShortageStart=0,ShortageEnd=0,ShortageNumerator=0,ShortageDenominator=0;
        int32 ShortageGold=0,ShortageXp=0,ShortageSeconds=0;
        // Decode to a candidate; a failed reload leaves the existing catalog intact.
        bool Parse(const FString& Json,FString& Error);
        bool Load(FString& Error);
    };
    LOCKDOWN_API int32 Ask(const FMarketGood& Good,int32 Stock,int32 Numerator=1,int32 Denominator=1);
    LOCKDOWN_API int32 Bid(const FMarketGood& Good,int32 Stock,int32 Numerator=1,int32 Denominator=1);
}
