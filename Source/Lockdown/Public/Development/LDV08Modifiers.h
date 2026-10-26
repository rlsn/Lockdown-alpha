#pragma once
#include "CoreMinimal.h"
namespace LDV08Modifiers
{
    inline uint32 Bit(const FString& Id)
    {static const TCHAR* Names[]={TEXT("mist_steps"),TEXT("storm_resistance"),TEXT("survey_overlay"),TEXT("insulation"),TEXT("river_wading"),TEXT("construct_durability"),TEXT("frost_extent"),TEXT("isolation_strike"),TEXT("frost_radius"),TEXT("conductive_hit"),TEXT("ember_hit"),TEXT("front_guard"),TEXT("field_repair"),TEXT("ice_grip"),TEXT("burn_resistance"),TEXT("consented_armor_aura"),TEXT("reaction_duration"),TEXT("cover_concealment")};for(uint32 I=0;I<18;++I){if(Id==Names[I]){return 1u<<I;}}return 0;}
}
