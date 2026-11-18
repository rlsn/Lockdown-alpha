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

    bool ExpireEmpowers(TArray<FLDCombatEmpower>& Armed, int64 AtMs)
    {
        return Armed.RemoveAll([AtMs](const FLDCombatEmpower& Pending) { return Pending.ExpiresAtMs <= AtMs; }) > 0;
    }

    bool DashCompleted(const FLDCombatVoluntaryDash& Dash, int64 AtMs, const FVector& FootCm)
    {
        return AtMs >= Dash.EndMs && FVector::Dist2D(Dash.StartFootCm, FootCm) > DashDisplacementEpsilonCm;
    }

    bool IsHardControl(ELDControlKind Kind)
    {
        return Kind == ELDControlKind::Root || Kind == ELDControlKind::Stun || Kind == ELDControlKind::Disarm
            || Kind == ELDControlKind::Taunt || Kind == ELDControlKind::Silence;
    }

    int64 DiminishHardControl(FLDHardControlChain& Chain, int64 NowMs, int64 DurationMs)
    {
        constexpr int64 WindowMs = 2000;
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
}
