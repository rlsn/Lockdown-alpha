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

    TOptional<FLDCombatEmpower> ConsumeEmpower(TOptional<FLDCombatEmpower>& Armed, int64 AtMs)
    {
        TOptional<FLDCombatEmpower> Taken;
        if (Armed.IsSet() && Armed->ExpiresAtMs > AtMs)
        {
            Taken = Armed;
            Taken->Charges = FMath::Max(Armed->Charges, 1) - 1;
            if (Taken->Charges > 0)
            {
                Armed->Charges = Taken->Charges;
                return Taken;
            }
        }
        Armed.Reset();
        return Taken;
    }

    bool ThornsTriggers(const FLDDemoEffectDefinition& Thorns, const FLDThornsContact& Contact,
        const TArray<FLDThornsMark>& Marks, const FString& SourceKey, const FGuid& Attacker, int64 AtMs)
    {
        if (Thorns.GetOp() != ELDDemoEffectOp::Thorns || Contact.bReflected || Contact.DamageP2 <= 0 || !Attacker.IsValid()) { return false; }
        const double Reach = Thorns.RadiusCm.Denominator > 0 ? double(Thorns.RadiusCm.Numerator) / double(Thorns.RadiusCm.Denominator) : 0.;
        if (Contact.HitClass != ELDCombatHitClass::BasicPrimary || !FMath::IsFinite(Contact.DistanceCm) || Contact.DistanceCm > Reach)
        {
            return false;
        }
        const FLDThornsMark* Last = Marks.FindByPredicate([&SourceKey, &Attacker](const FLDThornsMark& Mark)
        { return Mark.Attacker == Attacker && Mark.SourceKey == SourceKey; });
        return !Last || Thorns.IntervalMs <= 0 || AtMs - Last->AtMs >= Thorns.IntervalMs;
    }

    bool ThornsBaseP2(const FLDDemoEffectDefinition& Thorns, int64 OwnerArmorP2, int64 OwnerMaxHPP2, bool bCritical,
        int64& OutP2, FString& Error)
    {
        OutP2 = 0;
        const int64 Critical = bCritical ? Thorns.CriticalMultiplierBp : 10000;
        if (Thorns.BaseP2 < 0 || Thorns.ArmorRatioBp < 0 || Thorns.MaxHPRatioBp < 0 || OwnerArmorP2 < 0 || OwnerMaxHPP2 < 0
            || Critical < 10000 || Thorns.BaseP2 > MAX_int64 / 10000
            || (Thorns.ArmorRatioBp > 0 && OwnerArmorP2 > MAX_int64 / Thorns.ArmorRatioBp)
            || (Thorns.MaxHPRatioBp > 0 && OwnerMaxHPP2 > MAX_int64 / Thorns.MaxHPRatioBp))
        {
            Error = TEXT("Thorns scaling is outside the checked numeric domain.");
            return false;
        }
        const int64 ScaledBase = Thorns.BaseP2 * 10000;
        const int64 ScaledArmor = OwnerArmorP2 * Thorns.ArmorRatioBp;
        const int64 ScaledHealth = OwnerMaxHPP2 * Thorns.MaxHPRatioBp;
        if (ScaledBase > MAX_int64 - ScaledArmor || ScaledBase + ScaledArmor > MAX_int64 - ScaledHealth)
        {
            Error = TEXT("Thorns scaling overflowed.");
            return false;
        }
        const int64 Plain = (ScaledBase + ScaledArmor + ScaledHealth) / 10000;
        if (Plain > MAX_int64 / Critical)
        {
            Error = TEXT("Thorns critical scaling overflowed.");
            return false;
        }
        OutP2 = Plain * Critical / 10000;
        Error.Reset();
        return true;
    }

    void MarkThorns(TArray<FLDThornsMark>& Marks, const FString& SourceKey, const FGuid& Attacker, int64 AtMs, int64 IntervalMs)
    {
        Marks.RemoveAll([AtMs, IntervalMs, &SourceKey, &Attacker](const FLDThornsMark& Mark)
        { return (Mark.SourceKey == SourceKey && Mark.Attacker == Attacker) || AtMs - Mark.AtMs >= IntervalMs; });
        Marks.Add({SourceKey, Attacker, AtMs});
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
