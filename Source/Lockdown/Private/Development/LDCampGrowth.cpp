#include "Development/LDCampGrowth.h"

#include "Development/LDDemoContentDefinitions.h"

namespace
{
    // Non-negative operands only; true when the result fits.
    bool CheckedMul(int64 A, int64 B, int64& Out)
    {
        if (A < 0 || B < 0 || (B != 0 && A > MAX_int64 / B)) { return false; }
        Out = A * B;
        return true;
    }

    bool CheckedAdd(int64 A, int64 B, int64& Out)
    {
        if (A < 0 || B < 0 || A > MAX_int64 - B) { return false; }
        Out = A + B;
        return true;
    }

    // Value * (10000 + PerStageBp * Stage) / 10000, floored; false on overflow or a negative input.
    bool Grow(int64 Value, int64 PerStageBp, uint8 Stage, int64& Out)
    {
        Out = 0;
        int64 StepBp = 0, FactorBp = 0, Product = 0;
        if (!CheckedMul(PerStageBp, int64(Stage), StepBp) || !CheckedAdd(10000, StepBp, FactorBp)
            || !CheckedMul(Value, FactorBp, Product))
        {
            return false;
        }
        Out = Product / 10000;
        return true;
    }
}

uint8 LDCampGrowth::Stage(const FLDDemoContentParameters& Parameters, int64 SinceMatchStartMs)
{
    if (SinceMatchStartMs <= 0 || Parameters.CampGrowthStageMs <= 0 || Parameters.CampGrowthMaxStage <= 0)
    {
        return 0;
    }
    const int64 Reached = SinceMatchStartMs / Parameters.CampGrowthStageMs;
    return uint8(FMath::Min<int64>(Reached, FMath::Min<int64>(Parameters.CampGrowthMaxStage, MAX_uint8)));
}

bool LDCampGrowth::ScaleBody(const FLDDemoContentParameters& Parameters, const FLDDemoMonsterDefinition& Base, uint8 Stage,
    FLDDemoMonsterDefinition& Out, FString& Error)
{
    Out = Base;
    int64 ResistBonusP2 = 0;
    if (Stage > Parameters.CampGrowthMaxStage
        || !Grow(Base.HPP2, Parameters.CampGrowthHPPerStageBp, Stage, Out.HPP2)
        || !Grow(Base.AttackP2, Parameters.CampGrowthAttackPerStageBp, Stage, Out.AttackP2)
        || !CheckedMul(Parameters.CampGrowthResistPerStageP2, int64(Stage), ResistBonusP2)
        || !CheckedAdd(Base.ArmorP2, ResistBonusP2, Out.ArmorP2)
        || !CheckedAdd(Base.MagicResistP2, ResistBonusP2, Out.MagicResistP2)
        || Out.HPP2 <= 0)
    {
        Out = Base;
        Error = FString::Printf(TEXT("Camp growth stage %u of %s is outside the approved curve."), Stage, *Base.Id);
        return false;
    }
    Error.Reset();
    return true;
}

bool LDCampGrowth::RewardXP(const FLDDemoContentParameters& Parameters, const FLDDemoMonsterDefinition& Base, uint8 Stage,
    int64& Out)
{
    Out = 0;
    return Stage <= Parameters.CampGrowthMaxStage && Grow(Base.RewardXP, Parameters.CampGrowthRewardXPPerStageBp, Stage, Out);
}

bool LDCampGrowth::ApplySpawnTerms(const FLDDemoContentParameters& Parameters, ELDCampAffix Affix, bool bPeddler,
    FLDDemoMonsterDefinition& InOut, FString& Error)
{
    const auto Scale = [&Error](int64 Value, int64 Bp, int64& Out)
    {
        if (Value < 0 || Bp < 0 || (Value > 0 && Bp > MAX_int64 / Value))
        {
            Error = TEXT("A camp spawn term overflowed.");
            return false;
        }
        Out = Value * Bp / 10000;
        return true;
    };
    if (Affix == ELDCampAffix::Bonus && !Scale(InOut.HPP2, 10000 + Parameters.Affix.BonusHPBp, InOut.HPP2)) { return false; }
    if (Affix == ELDCampAffix::Overtime)
    {
        // Attack speed is a rational: one attack per AttackIntervalMs; +30% shortens the interval to interval / 1.3.
        const int64 Bp = 10000 + Parameters.Affix.OvertimeAttackSpeedBp;
        InOut.AttackSpeed.Numerator *= Bp;
        InOut.AttackSpeed.Denominator *= 10000;
        const int64 Divisor = FMath::GreatestCommonDivisor(InOut.AttackSpeed.Numerator, InOut.AttackSpeed.Denominator);
        if (Divisor > 0) { InOut.AttackSpeed.Numerator /= Divisor; InOut.AttackSpeed.Denominator /= Divisor; }
        InOut.AttackIntervalMs = FMath::Max<int64>(InOut.AttackWindupMs + 1, (InOut.AttackIntervalMs * 10000 + Bp - 1) / Bp);
    }
    if (bPeddler)
    {
        InOut.HPP2 = Parameters.Explore.PeddlerHPP2;
        InOut.MoveSpeedCmPerSec = Parameters.Explore.PeddlerMoveSpeedCmPerSec;
        InOut.AttackP2 = 0;
    }
    Error.Reset();
    return true;
}
