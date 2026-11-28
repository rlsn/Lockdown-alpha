#include "Combat/LDCombatControlRules.h"
#include "Combat/LDCombatTypes.h"
#include "Combat/LDTacticalRules.h"
#include "Development/LDDemoContentDefinitions.h"

namespace LDCombatControl
{
    bool Blocks(const TArray<FLDCombatRoot>& Roots, const TArray<FLDCombatRoot>& Disarms, const TArray<FLDCombatRoot>& Stuns,
        int64 AtMs, const FLDDemoPolicies& Policies, ELDCombatControlAction Action, const TArray<FLDCombatRoot>* Silences)
    {
        if (LDTactical::HasActiveRoot(Stuns, AtMs)) { return true; }
        switch (Action)
        {
        case ELDCombatControlAction::Move: return LDTactical::RootBlocks(Roots, AtMs, Policies, ELDDemoRootBlock::Move);
        case ELDCombatControlAction::Displacement: return LDTactical::RootBlocks(Roots, AtMs, Policies, ELDDemoRootBlock::Dash);
        case ELDCombatControlAction::Basic: return LDTactical::HasActiveRoot(Disarms, AtMs);
        case ELDCombatControlAction::Skill: return Silences && LDTactical::HasActiveRoot(*Silences, AtMs);
        default: return false;
        }
    }

    uint8 DisarmBlockBits() { return uint8(ELDCombatControlBlock::Basic); }

    uint8 SilenceBlockBits() { return uint8(ELDCombatControlBlock::Cast); }

    uint8 StunBlockBits()
    {
        return uint8(uint8(ELDCombatControlBlock::Move) | uint8(ELDCombatControlBlock::Basic) | uint8(ELDCombatControlBlock::Cast)
            | uint8(ELDCombatControlBlock::Displacement));
    }

    bool Apply(TArray<FLDCombatRoot>& Instances, const FLDCombatRoot& Instance, int64 AtMs)
    { return LDTactical::ApplyRoot(Instances, Instance, AtMs); }

    bool ExpireDisarmsAndStuns(TArray<FLDCombatRoot>& Disarms, TArray<FLDCombatRoot>& Stuns, int64 AtMs,
        TArray<FLDCombatRoot>* Silences)
    {
        const auto Ended=[AtMs](const FLDCombatRoot& Instance) { return Instance.ExpiresAtMs <= AtMs; };
        const int32 Removed = Disarms.RemoveAll(Ended) + Stuns.RemoveAll(Ended) + (Silences ? Silences->RemoveAll(Ended) : 0);
        return Removed > 0;
    }

    void ArmEmpower(TArray<FLDCombatEmpower>& Armed, const FLDCombatEmpower& Package)
    {
        Armed.RemoveAll([&Package](const FLDCombatEmpower& Pending) { return Pending.Source == Package.Source; });
        Armed.Add(Package);
    }

    bool ConsumeEmpowers(TArray<FLDCombatEmpower>& Armed, int64 AtMs, TArray<FLDCombatEmpower>& OutTaken, int64& OutSumP2)
    {
        OutTaken.Reset();
        OutSumP2 = 0;
        ExpireEmpowers(Armed, AtMs);
        int64 Sum = 0;
        for (const FLDCombatEmpower& Pending : Armed)
        {
            if (Pending.AmountP2 < 0 || Pending.AmountP2 > MAX_int64 - Sum) { return false; }
            Sum += Pending.AmountP2;
        }
        for (int32 Index = Armed.Num() - 1; Index >= 0; --Index)
        {
            FLDCombatEmpower Taken = Armed[Index];
            Taken.Charges = FMath::Max(Armed[Index].Charges, 1) - 1;
            if (Taken.Charges > 0) { Armed[Index].Charges = Taken.Charges; }
            else { Armed.RemoveAt(Index); }
            OutTaken.Insert(Taken, 0);
        }
        OutSumP2 = Sum;
        return true;
    }

    int64 EmpoweredWindupScaleBp(const TArray<FLDCombatEmpower>& Armed, int64 AtMs)
    {
        int64 Scale = 10000;
        for (const FLDCombatEmpower& Pending : Armed)
        {
            if (Pending.ExpiresAtMs > AtMs) { Scale = FMath::Max(Scale, Pending.WindupScaleBp); }
        }
        return Scale;
    }

    bool ExpireEmpowers(TArray<FLDCombatEmpower>& Armed, int64 AtMs)
    {
        return Armed.RemoveAll([AtMs](const FLDCombatEmpower& Pending) { return Pending.ExpiresAtMs <= AtMs; }) > 0;
    }

    bool DashCompleted(const FLDCombatVoluntaryDash& Dash, int64 AtMs, const FVector& FootCm)
    {
        return AtMs >= Dash.EndMs && (Dash.bClampedStop || FVector::Dist2D(Dash.StartFootCm, FootCm) > DashDisplacementEpsilonCm);
    }

    bool IsHardControl(ELDControlKind Kind)
    {
        return Kind == ELDControlKind::Root || Kind == ELDControlKind::Stun || Kind == ELDControlKind::Disarm
            || Kind == ELDControlKind::Taunt || Kind == ELDControlKind::Silence;
    }

    int64 DiminishHardControl(FLDHardControlChain& Chain, int64 NowMs, int64 DurationMs)
    {
        constexpr int64 WindowMs = HardControlChainWindowMs;
        constexpr int64 FloorMs = 200;
        if (DurationMs <= 0) { return DurationMs; }
        Chain.Count = NowMs < Chain.WindowEndMs ? FMath::Min(Chain.Count + 1, 2) : 0;
        const int64 ScaleBp = Chain.Count == 0 ? 10000 : Chain.Count == 1 ? 6000 : 3000;
        const int64 Scaled = DurationMs / 10000 * ScaleBp + DurationMs % 10000 * ScaleBp / 10000;
        const int64 Result = Chain.Count == 0 ? DurationMs : FMath::Max(FMath::Min(FloorMs, DurationMs), Scaled);
        const int64 EndsAt = NowMs <= MAX_int64 - Result - WindowMs ? NowMs + Result + WindowMs : MAX_int64;
        Chain.WindowEndMs = FMath::Max(Chain.WindowEndMs, EndsAt);
        return Result;
    }

    int64 AgencyWeightBp(ELDControlKind Kind, const FLDAgencyParams& Params)
    {
        switch (Kind)
        {
        case ELDControlKind::Stun: case ELDControlKind::Knockback: return 10000;
        case ELDControlKind::Taunt: return Params.TauntWeightBp;
        case ELDControlKind::Root: return 5000;
        case ELDControlKind::Silence: case ELDControlKind::Disarm: return 2000;
        default: return 0;
        }
    }

    int64 AgencyLossBpMs(const TArray<FLDAgencyLoss>& Losses, int64 NowMs, int64 WindowMs)
    {
        const int64 From = NowMs - WindowMs;
        TArray<int64, TInlineAllocator<16>> Cuts;
        Cuts.Add(From);
        Cuts.Add(NowMs);
        for (const FLDAgencyLoss& Loss : Losses)
        {
            if (Loss.StartMs > From && Loss.StartMs < NowMs) { Cuts.Add(Loss.StartMs); }
            if (Loss.EndMs > From && Loss.EndMs < NowMs) { Cuts.Add(Loss.EndMs); }
        }
        Cuts.Sort();
        int64 Sum = 0;
        for (int32 Index = 1; Index < Cuts.Num(); ++Index)
        {
            const int64 A = Cuts[Index - 1], B = Cuts[Index];
            if (B <= A) { continue; }
            int64 Weight = 0;
            for (const FLDAgencyLoss& Loss : Losses)
            {
                if (Loss.StartMs <= A && Loss.EndMs >= B) { Weight = FMath::Max(Weight, Loss.WeightBp); }
            }
            Sum += (B - A) * Weight;
        }
        return Sum;
    }

    namespace
    {
        int64 AgencyOpenFromMs(const FLDAgencyMeter& Meter, const FLDAgencyParams& Params)
        {
            const int64 AfterCooldown = Meter.LastTriggerMs == MIN_int64 ? MIN_int64 : Meter.LastTriggerMs + Params.CooldownMs;
            return FMath::Max(Meter.GuardUntilMs, AfterCooldown);
        }
    }

    bool AgencyTriggers(const FLDAgencyMeter& Meter, int64 NowMs, const FLDAgencyParams& Params)
    {
        return NowMs >= AgencyOpenFromMs(Meter, Params)
            && AgencyLossBpMs(Meter.Losses, NowMs, Params.WindowMs) >= Params.ThresholdMs * 10000;
    }

    int64 AgencyDueMs(const FLDAgencyMeter& Meter, int64 NowMs, const FLDAgencyParams& Params)
    {
        const int64 Threshold = Params.ThresholdMs * 10000;
        // The loss is linear between these instants: a loss starts or ends, or its edge leaves the window.
        TArray<int64, TInlineAllocator<32>> Points;
        const int64 Open = FMath::Max(NowMs + 1, AgencyOpenFromMs(Meter, Params));
        Points.Add(Open);
        for (const FLDAgencyLoss& Loss : Meter.Losses)
        {
            for (const int64 At : {Loss.StartMs, Loss.EndMs, Loss.StartMs + Params.WindowMs, Loss.EndMs + Params.WindowMs})
            {
                if (At > Open) { Points.Add(At); }
            }
        }
        Points.Sort();
        int64 T0 = Points[0];
        int64 L0 = AgencyLossBpMs(Meter.Losses, T0, Params.WindowMs);
        if (L0 >= Threshold) { return T0; }
        for (int32 Index = 1; Index < Points.Num(); ++Index)
        {
            const int64 T1 = Points[Index];
            if (T1 <= T0) { continue; }
            const int64 L1 = AgencyLossBpMs(Meter.Losses, T1, Params.WindowMs);
            if (L1 >= Threshold)
            {
                // Smallest whole millisecond on the rising segment that reaches the threshold.
                const int64 Rise = L1 - L0, Span = T1 - T0, Need = Threshold - L0;
                return T0 + (Need * Span + Rise - 1) / Rise;
            }
            T0 = T1;
            L0 = L1;
        }
        return MAX_int64;
    }

    void PruneAgency(FLDAgencyMeter& Meter, int64 NowMs, int64 WindowMs)
    {
        Meter.Losses.RemoveAll([From = NowMs - WindowMs](const FLDAgencyLoss& Loss) { return Loss.EndMs <= From; });
    }

    bool AgencyKnockbackRolls(FLDAgencyMeter& Meter, int64 NowMs, int64 EndMs, const FLDAgencyParams& Params)
    {
        const bool bChained = Meter.LastKnockEndMs != MIN_int64 && NowMs < Meter.LastKnockEndMs + HardControlChainWindowMs;
        Meter.KnockChain = bChained ? Meter.KnockChain + 1 : 1;
        Meter.LastKnockEndMs = FMath::Max(Meter.LastKnockEndMs, EndMs);
        return Meter.KnockChain >= Params.RollFrom;
    }
}
