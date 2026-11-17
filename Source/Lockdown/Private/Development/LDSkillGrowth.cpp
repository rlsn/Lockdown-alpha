#include "Development/LDSkillGrowth.h"

#include "Development/LDDemoContentDefinitions.h"

namespace LDSkillGrowthDetail
{
    // Momentum grants keep their authored amount (control_momentum, multi_hit_momentum).
    bool IsMomentumGrant(ELDDemoEffectOp Op)
    {
        return Op == ELDDemoEffectOp::ControlMomentum || Op == ELDDemoEffectOp::MultiHitMomentum;
    }

    bool ScaleBp(int64 Value, int64 Bp, int64& Out)
    {
        Out = 0;
        if (Value < 0 || Bp < 0 || (Value > 0 && Bp > MAX_int64 / Value)) { return false; }
        Out = Value * Bp / 10000;
        return true;
    }
}

int64 LDSkillGrowth::FactorBp(int64 PerLevelBp, uint8 Level)
{
    if (Level < 1 || PerLevelBp < 0 || PerLevelBp > MAX_int32)
    {
        return 0;
    }
    return 10000 + PerLevelBp * (int64(Level) - 1);
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
    if (!LDSkillGrowthDetail::IsMomentumGrant(Authored.GetOp()) && !GrowP2(Authored.BaseP2, Factor, Out.BaseP2))
    {
        return false;
    }
    // S16: the record cap is a fixed amount that grows like the answer's base.
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

bool LDSkillGrowth::ScaleRatios(FLDDemoEffectDefinition& InOut, int64 RatioBp)
{
    using LDSkillGrowthDetail::ScaleBp;
    return ScaleBp(InOut.ADRatioBp, RatioBp, InOut.ADRatioBp) && ScaleBp(InOut.APRatioBp, RatioBp, InOut.APRatioBp)
        && ScaleBp(InOut.MaxHPRatioBp, RatioBp, InOut.MaxHPRatioBp);
}

bool LDSkillGrowth::ScaleValues(FLDDemoAbilityValues& InOut, int64 BaseBp, int64 RatioBp)
{
    if (BaseBp <= 0 || RatioBp <= 0)
    {
        return false;
    }
    for (FLDDemoEffectDefinition& Effect : InOut.Effects)
    {
        FLDDemoEffectDefinition Scaled;
        if (!GrowEffect(Effect, BaseBp, Scaled) || !ScaleRatios(Scaled, RatioBp))
        {
            return false;
        }
        Effect = MoveTemp(Scaled);
    }
    return true;
}

bool LDSkillGrowth::RankScales(const FLDDemoSkillRankParameters& Ranks, uint8 Rank, int64& OutBaseBp, int64& OutRatioBp)
{
    OutBaseBp = OutRatioBp = 0;
    if (Rank < 1 || Rank > Ranks.RankBaseScaleBp.Num() || Rank > Ranks.RankRatioScaleBp.Num())
    {
        return false;
    }
    OutBaseBp = Ranks.RankBaseScaleBp[Rank - 1];
    OutRatioBp = Ranks.RankRatioScaleBp[Rank - 1];
    return OutBaseBp > 0 && OutRatioBp > 0;
}

bool LDSkillGrowth::GrowValues(const FLDDemoAbilityValues& Authored, const FLDDemoSkillRankParameters& Ranks,
    int64 PerLevelBp, uint8 Level, uint8 Rank, FLDDemoAbilityValues& Out)
{
    Out = Authored;
    const int64 Factor = FactorBp(PerLevelBp, Level);
    int64 BaseBp = 0, RatioBp = 0;
    if (Factor <= 0 || !RankScales(Ranks, Rank, BaseBp, RatioBp))
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
    return Rank == 1 || ScaleValues(Out, BaseBp, RatioBp);
}
