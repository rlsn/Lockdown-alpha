#include "Development/LDSkillGrowth.h"

#include "Development/LDDemoContentDefinitions.h"

int64 LDSkillGrowth::FactorBp(int64 ThroughTenBp, int64 AfterTenBp, uint8 Level)
{
    if (Level < 1 || ThroughTenBp < 0 || ThroughTenBp > MAX_int32 || AfterTenBp < 0 || AfterTenBp > MAX_int32)
    {
        return 0;
    }
    const int64 ThroughTenSteps = FMath::Min<int64>(Level - 1, 9);
    const int64 AfterTenSteps = FMath::Max<int64>(Level - 10, 0);
    return 10000 + ThroughTenBp * ThroughTenSteps + AfterTenBp * AfterTenSteps;
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
    // #198 S16: the record cap is a fixed amount (100G) like the answer's base.
    if (Authored.GetOp() == ELDDemoEffectOp::Brace && !GrowP2(Authored.LimitP2, Factor, Out.LimitP2))
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

bool LDSkillGrowth::GrowValues(const FLDDemoAbilityValues& Authored, int64 ThroughTenBp, int64 AfterTenBp,
    uint8 Level, FLDDemoAbilityValues& Out)
{
    Out = Authored;
    const int64 Factor = FactorBp(ThroughTenBp, AfterTenBp, Level);
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
