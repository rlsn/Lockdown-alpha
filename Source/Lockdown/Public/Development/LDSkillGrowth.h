#pragma once

#include "CoreMinimal.h"

struct FLDDemoEffectDefinition;
struct FLDDemoRankDefinition;

// #147 skill growth: a learned skill's fixed amounts follow its owner's current level,
// G(L) = 1 + SKILL.GrowthPerLevel x (L - 1). The advanced rank's 1.5 is authored in its rank row. Mana, ratios,
// durations, cooldowns, ranges and stack counts never grow; equipment never passes through here.
namespace LDSkillGrowth
{
    // G(L) in basis points; 0 for a level below 1 or a negative step.
    LOCKDOWN_API int64 FactorBp(int64 PerLevelBp, uint8 Level);
    // Floor of AmountP2 x G(L); false on overflow or a negative amount.
    LOCKDOWN_API bool GrowP2(int64 AmountP2, int64 FactorBp, int64& Out);
    // The fixed amounts of a skill effect that grow: its base amount (damage, shield, reflection, empowered or periodic
    // damage), except a mana grant; and the point stats (AD, AP, armor, magic resist) of a timed bonus.
    LOCKDOWN_API bool GrowEffect(const FLDDemoEffectDefinition& Authored, int64 FactorBp, FLDDemoEffectDefinition& Out);
    // The authored rank row as an owner at Level uses it; false on overflow.
    LOCKDOWN_API bool GrowRank(const FLDDemoRankDefinition& Authored, int64 PerLevelBp, uint8 Level, FLDDemoRankDefinition& Out);
}
