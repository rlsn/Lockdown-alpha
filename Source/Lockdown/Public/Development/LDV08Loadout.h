#pragma once
#include "CoreMinimal.h"
#include "Inventory/LDInventoryTypes.h"

namespace LDV08
{
    constexpr int32 Actions=6, Bindings=11, AccessoryStart=7;
    LOCKDOWN_API const TCHAR* TraitName(const FLDCharacterBuild& B);
    LOCKDOWN_API bool PresetEnabled(uint8 Preset);
    LOCKDOWN_API int32 TraitValue(const FLDCharacterBuild& B,FName Rule,int32 Base);
    LOCKDOWN_API bool Validate(const FLDInventory& I,const FLDCharacterBuild& B,FString& Why);
    LOCKDOWN_API bool Project(const FLDInventory& I,const FLDCharacterBuild& B,const FLDBuildPlan& P,FLDInventory& Out,FLDCharacterBuild& Build,FString& Why);
    LOCKDOWN_API bool InvalidateSources(FLDCharacterBuild& B,const FLDInventory& I);
    LOCKDOWN_API bool ItemSource(const FLDInventory& I,const FLDCharacterBuild& B,FGuid Item,uint8 SourceSlot,bool Accessory);
    LOCKDOWN_API bool PreparationPermissions(const FLDCharacterBuild& B,const FLDBuildPlan& P,bool SkillAllowed,bool ItemAllowed);
}
