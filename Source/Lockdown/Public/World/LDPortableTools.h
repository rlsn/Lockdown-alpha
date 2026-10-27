#pragma once
#include "CoreMinimal.h"
#include "Inventory/LDInventoryTypes.h"
struct FLDEquipmentDefinition;
class ALDPlayerController;
namespace LDPortableTools
{
    LOCKDOWN_API bool IsTool(ELDItemKind Kind);
    LOCKDOWN_API bool Load(FString& Json,FString& Error);
    LOCKDOWN_API const FLDEquipmentDefinition* Definition(ELDItemKind Kind);
    LOCKDOWN_API int32 InitialFuel(ELDItemKind Kind);
    // Fuel is a continuous clock, not a structural inventory mutation each frame.
    LOCKDOWN_API int32 Remaining(const ALDItem* Item,double Now);
    LOCKDOWN_API uint8 LightFor(const ALDPlayerController* PC);
}
