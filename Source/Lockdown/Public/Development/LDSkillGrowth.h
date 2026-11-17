#pragma once

#include "CoreMinimal.h"

struct FLDDemoEffectDefinition;
struct FLDDemoAbilityValues;
struct FLDDemoSkillRankParameters;

// Skill growth and ranks (CONTENT_AND_BALANCE §3.6): the fixed base amounts of a skill's value set grow with
// G(L) = 1 + SKILL.GrowthPerLevel x (L - 1) and scale with SKILL.RankBaseScale[rank]; its AD/AP/max-HP ratios scale with
// SKILL.RankRatioScale[rank]. Momentum costs and grants, durations, cooldowns, ranges and stack counts never grow.
namespace LDSkillGrowth
{
    // G(L) in basis points; 0 for a level below 1 or a negative step.
    LOCKDOWN_API int64 FactorBp(int64 PerLevelBp, uint8 Level);
    // Floor of AmountP2 x FactorBp / 10000; false on overflow or a negative amount.
    LOCKDOWN_API bool GrowP2(int64 AmountP2, int64 FactorBp, int64& Out);
    // The fixed amounts of a skill effect that grow: its base amount (damage, shield, reflection, empowered or periodic
    // damage), except a momentum grant; the brace record cap; and the point stats (AD, AP, armor, magic resist) of a timed
    // bonus.
    LOCKDOWN_API bool GrowEffect(const FLDDemoEffectDefinition& Authored, int64 FactorBp, FLDDemoEffectDefinition& Out);
    // The AD/AP/max-HP coefficients of an effect times RatioBp; false on overflow.
    LOCKDOWN_API bool ScaleRatios(FLDDemoEffectDefinition& InOut, int64 RatioBp);
    // Every effect of Values: its growing amounts times BaseBp and its ratios times RatioBp (rank and overdrive scaling).
    LOCKDOWN_API bool ScaleValues(FLDDemoAbilityValues& InOut, int64 BaseBp, int64 RatioBp);
    // The rank scales of Rank 1..3 (basis points); false for another rank or a malformed table.
    LOCKDOWN_API bool RankScales(const FLDDemoSkillRankParameters& Ranks, uint8 Rank, int64& OutBaseBp, int64& OutRatioBp);
    // The value set an owner at Level uses at Rank: the (already patched) authored set grown by G(L) and scaled by the
    // rank; false on overflow or an unknown rank.
    LOCKDOWN_API bool GrowValues(const FLDDemoAbilityValues& Authored, const FLDDemoSkillRankParameters& Ranks,
        int64 PerLevelBp, uint8 Level, uint8 Rank, FLDDemoAbilityValues& Out);
}
