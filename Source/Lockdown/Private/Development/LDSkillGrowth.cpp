#include "Development/LDSkillGrowth.h"

#include "Development/LDDemoContentDefinitions.h"

int64 LDSkillGrowth::FactorBp(int64 PerLevelBp, uint8 Level)
{
    if (Level < 1 || PerLevelBp < 0 || PerLevelBp > MAX_int32)
    {
        return 0;
    }
    return 10000 + PerLevelBp * int64(Level - 1);
}

bool LDSkillGrowth::GrowP2(int64 AmountP2, int64 Factor, int64& Out)
{
    Out = 0;
    if (AmountP2 < 0 || Factor <= 0 || (AmountP2 > 0 && Factor > MAX_int64 / AmountP2))
    {
        return false;
    }
    Out = AmountP2 * Factor / 10000;
    return true;
}

bool LDSkillGrowth::GrowEffect(const FLDDemoEffectDefinition& Authored, int64 Factor, FLDDemoEffectDefinition& Out)
{
    Out = Authored;
    if (Authored.GetOp() != ELDDemoEffectOp::ManaOnHit && !GrowP2(Authored.BaseP2, Factor, Out.BaseP2))
    {
        return false;
    }
    if (Authored.GetOp() == ELDDemoEffectOp::TimedStatBonus)
    {
        for (FLDDemoStatBonus& Stat : Out.Stats)
        {
            const bool bPoints = Stat.Stat == ELDDemoBonusStat::ADFlat || Stat.Stat == ELDDemoBonusStat::APFlat
                || Stat.Stat == ELDDemoBonusStat::ArmorFlat || Stat.Stat == ELDDemoBonusStat::MagicResistFlat;
            if (bPoints && !GrowP2(Stat.Amount, Factor, Stat.Amount))
            {
                return false;
            }
        }
    }
    return true;
}

bool LDSkillGrowth::GrowRank(const FLDDemoRankDefinition& Authored, int64 PerLevelBp, uint8 Level, FLDDemoRankDefinition& Out)
{
    Out = Authored;
    const int64 Factor = FactorBp(PerLevelBp, Level);
    if (Factor <= 0)
    {
        return false;
    }
    for (int32 Index = 0; Index < Authored.Effects.Num(); ++Index)
    {
        if (!GrowEffect(Authored.Effects[Index], Factor, Out.Effects[Index]))
        {
            return false;
        }
    }
    return true;
}
