#include "Combat/LDTacticalRules.h"
#include "Development/LDDemoContentMath.h"

namespace
{
template <typename TResult>
TResult EvaluationFailure(ELDCombatEvaluationError Code, const TCHAR* Field, const FString& Message)
{
    TResult Result;
    Result.Error = {Code, Field, Message};
    return Result;
}

bool QuantizeEvaluation(const LDDemoContent::FExact& Value, bool bCeil, int64& Out,
    LDDemoContent::FCheck& Check, const TCHAR* Field)
{
    using namespace LDDemoContent;
    if (!Check.Good) { return false; }
    if (!Check.Require(!Value.N.IsNegative() && !Value.D.IsNegative() && !Value.D.IsZero(),
        Field, TEXT("quantization requires a nonnegative value and positive denominator")))
    {
        return false;
    }
    FBig Quotient = Value.N;
    FBig Remainder;
    Quotient.DivideWithRemainder(Value.D, Remainder);
    const bool bIncrement = bCeil && !Remainder.IsZero();
    if (!Check.Require(Quotient <= FBig(MAX_int64) && (!bIncrement || Quotient < FBig(MAX_int64)),
        Field, TEXT("quantized result exceeds int64")))
    {
        return false;
    }
    if (bIncrement) { Quotient += FBig(1); }
    Out = Quotient.ToInt();
    return true;
}

bool WithinP2Bounds(int64 Value, const FLDDemoP2Bounds& Bounds)
{
    return Value >= 0 && Value >= Bounds.MinP2 && Value <= Bounds.MaxP2;
}
}

namespace LDTactical
{
    FLDDamageEvaluationResult EvaluateDamage(const FLDDamageEvaluationInput& Input, const FLDDemoContentSnapshot& Catalog)
    {
        using EError = ELDCombatEvaluationError;
        const auto Fail = [](EError Code, const TCHAR* Field, const FString& Message)
        {
            return EvaluationFailure<FLDDamageEvaluationResult>(Code, Field, Message);
        };
        const auto& Content = Catalog.Get();
        const auto& Formulas = Content.Formulas;
        const auto& Parameters = Content.Parameters;
        if (Formulas.Damage != ELDDemoFormula::FloorP2ThenShieldThenHP
            || Formulas.Resistance != ELDDemoFormula::PointsOverScalePlusPoints
            || Formulas.Crit != ELDDemoFormula::OncePerPrimaryAttack
            || Content.Policies.CritSampling != ELDDemoPolicy::OncePerAttackId
            || Content.Policies.BasicSecondaryTriggers != ELDDemoPolicy::Disabled
            || Formulas.ResistanceScaleP2 <= 0 || Parameters.CritMultiplierBp < 10000)
        {
            return Fail(EError::Definition, TEXT("Catalog"), TEXT("Unsupported damage, resistance or critical-hit contract"));
        }
        if (!Input.Definition.IsSet())
        {
            return Fail(EError::Definition, TEXT("Definition"), TEXT("Resolved damage definition quantities are required"));
        }
        const auto& Definition = Input.Definition.GetValue();
        if (Definition.DamageType != ELDDemoDamageType::Physical && Definition.DamageType != ELDDemoDamageType::Magical)
        {
            return Fail(EError::Definition, TEXT("DamageType"), TEXT("Only physical and magical damage are defined"));
        }
        if (Input.HitClass != ELDCombatHitClass::BasicPrimary && Input.HitClass != ELDCombatHitClass::Ability)
        {
            return Fail(EError::Definition, TEXT("HitClass"), TEXT("A resolved hit classification is required"));
        }
        if (Definition.BaseP2 < 0 || Definition.ADRatioBp < 0 || Definition.APRatioBp < 0 || Definition.MaxHPRatioBp < 0)
        {
            return Fail(EError::Domain, TEXT("Definition"), TEXT("Damage points and R4 coefficients must be nonnegative"));
        }
        if (Input.HitClass != ELDCombatHitClass::Ability
            && (Definition.APRatioBp != 0 || Definition.MaxHPRatioBp != 0 || Definition.DamageType != ELDDemoDamageType::Physical))
        {
            return Fail(EError::Definition, TEXT("Definition"), TEXT("Basic hits must be physical and cannot scale with AP"));
        }
        if (!Input.bCritical.IsSet())
        {
            return Fail(EError::Numeric, TEXT("bCritical"), TEXT("The critical-hit result must be explicit"));
        }
        if (Definition.ADRatioBp != 0 && !Input.SourceADP2.IsSet())
        {
            return Fail(EError::Numeric, TEXT("SourceADP2"), TEXT("AD is required by the resolved coefficient"));
        }
        if (Definition.APRatioBp != 0 && !Input.SourceAPP2.IsSet())
        {
            return Fail(EError::Numeric, TEXT("SourceAPP2"), TEXT("AP is required by the resolved coefficient"));
        }
        if (Input.SourceADP2.IsSet() && !WithinP2Bounds(Input.SourceADP2.GetValue(), Parameters.AD))
        {
            return Fail(EError::Domain, TEXT("SourceADP2"), TEXT("AD is outside the catalog point domain"));
        }
        if (Input.SourceAPP2.IsSet() && !WithinP2Bounds(Input.SourceAPP2.GetValue(), Parameters.AP))
        {
            return Fail(EError::Domain, TEXT("SourceAPP2"), TEXT("AP is outside the catalog point domain"));
        }
        if (Definition.MaxHPRatioBp != 0 && !Input.SourceMaxHPP2.IsSet())
        {
            return Fail(EError::Numeric, TEXT("SourceMaxHPP2"), TEXT("Max HP is required by the resolved coefficient"));
        }
        if (Input.SourceMaxHPP2.IsSet() && !WithinP2Bounds(Input.SourceMaxHPP2.GetValue(), Parameters.MaxHP))
        {
            return Fail(EError::Domain, TEXT("SourceMaxHPP2"), TEXT("Max HP is outside the catalog point domain"));
        }
        if (!Input.TargetResistanceP2.IsSet())
        {
            return Fail(EError::Numeric, TEXT("TargetResistanceP2"), TEXT("The matching target resistance is required"));
        }
        const auto& ResistanceBounds = Definition.DamageType == ELDDemoDamageType::Physical
            ? Parameters.Armor : Parameters.MagicResist;
        if (!WithinP2Bounds(Input.TargetResistanceP2.GetValue(), ResistanceBounds))
        {
            return Fail(EError::Domain, TEXT("TargetResistanceP2"), TEXT("Resistance is outside the catalog point domain"));
        }
        if (!Input.TargetHPP2.IsSet())
        {
            return Fail(EError::Numeric, TEXT("TargetHPP2"), TEXT("Target HP must be resolved"));
        }
        if (!Input.TargetShieldP2.IsSet())
        {
            return Fail(EError::Numeric, TEXT("TargetShieldP2"), TEXT("The current target shield must be resolved"));
        }
        if (Input.TargetHPP2.GetValue() < 0 || Input.TargetShieldP2.GetValue() < 0)
        {
            return Fail(EError::Domain, TEXT("TargetResources"), TEXT("HP and shield must be nonnegative P2 values"));
        }
        if (!Input.bTargetProtected.IsSet())
        {
            return Fail(EError::Numeric, TEXT("bTargetProtected"), TEXT("Current target protection must be resolved"));
        }

        FString MathError;
        LDDemoContent::FCheck Check(MathError);
        LDDemoContent::FNumbers Numbers(Check);
        auto Damage = Numbers.Value(Definition.BaseP2);
        if (Definition.ADRatioBp != 0)
        {
            Damage = Numbers.Add(Damage, Numbers.Mul(Numbers.Value(Input.SourceADP2.GetValue()),
                Numbers.Value(Definition.ADRatioBp, 10000)));
        }
        if (Definition.APRatioBp != 0)
        {
            Damage = Numbers.Add(Damage, Numbers.Mul(Numbers.Value(Input.SourceAPP2.GetValue()),
                Numbers.Value(Definition.APRatioBp, 10000)));
        }
        if (Definition.MaxHPRatioBp != 0)
        {
            Damage = Numbers.Add(Damage, Numbers.Mul(Numbers.Value(Input.SourceMaxHPP2.GetValue()),
                Numbers.Value(Definition.MaxHPRatioBp, 10000)));
        }
        // A critical hit multiplies by STAT.CritMultiplier before the damage factors.
        if (Input.bCritical.GetValue())
        {
            Damage = Numbers.Mul(Damage, Numbers.Value(Parameters.CritMultiplierBp, 10000));
        }
        if (Input.DamageFactorBp <= 0 || Input.DeliveryScaleBp < 0 || Input.DeliveryScaleBp > 10000
            || Input.KeystoneGuardCapBp < 0 || Input.KeystoneGuardCapBp >= 10000
            || Input.PackageScaleBp <= 0 || Input.PackageScaleBp > 100000 || Input.BraceCutBp < 0 || Input.BraceCutBp >= 10000)
        {
            return Fail(EError::Domain, TEXT("DamageFactorBp"), TEXT("A damage factor, delivery share or guard is outside its domain"));
        }
        if (Input.DamageFactorBp != 10000)
        {
            Damage = Numbers.Mul(Damage, Numbers.Value(Input.DamageFactorBp, 10000));
        }
        int64 ResistanceDenominatorP2;
        if (!Numbers.Add64(Formulas.ResistanceScaleP2, Input.TargetResistanceP2.GetValue(),
            ResistanceDenominatorP2, TEXT("TargetResistanceP2")))
        {
            return Fail(EError::Overflow, TEXT("TargetResistanceP2"), MathError);
        }
        Damage = Numbers.Mul(Damage, Numbers.Value(Formulas.ResistanceScaleP2, ResistanceDenominatorP2));
        Damage = Numbers.Mul(Damage, Numbers.Value(Input.DeliveryScaleBp, 10000));
        if (Input.PackageScaleBp != 10000)
        {
            Damage = Numbers.Mul(Damage, Numbers.Value(Input.PackageScaleBp, 10000));
        }
        // #198 S16 (AI spec §9.1): the stance cut follows every resistance and percentage term and precedes the flat guard.
        int64 BraceCutP2 = 0;
        if (Input.BraceCutBp > 0)
        {
            const auto Cut = Numbers.Mul(Damage, Numbers.Value(Input.BraceCutBp, 10000));
            if (!QuantizeEvaluation(Cut, false, BraceCutP2, Check, TEXT("BraceCutP2")))
            {
                return Fail(EError::Overflow, TEXT("BraceCutP2"), MathError);
            }
            Damage = Numbers.Sub(Damage, Numbers.Value(BraceCutP2));
        }
        int64 BeforeKeystoneGuardP2 = 0;
        // The thick-skin keystone cuts its share of a direct hit (the caller arms it only for a ready, direct, hostile hit).
        const bool bKeystoneGuarded = Input.KeystoneGuardCapBp > 0;
        if (bKeystoneGuarded)
        {
            if (!QuantizeEvaluation(Damage, false, BeforeKeystoneGuardP2, Check, TEXT("BeforeKeystoneGuardP2")))
            {
                return Fail(EError::Overflow, TEXT("BeforeKeystoneGuardP2"), MathError);
            }
            Damage = Numbers.Sub(Damage, Numbers.Mul(Damage, Numbers.Value(Input.KeystoneGuardCapBp, 10000)));
        }
        FLDDamageEvaluation Value;
        if (!QuantizeEvaluation(Damage, false, Value.DamageP2, Check, TEXT("DamageP2")))
        {
            return Fail(EError::Overflow, TEXT("DamageP2"), MathError);
        }
        // Protection cannot hide missing input, an invalid domain or arithmetic overflow.
        const int64 KeystoneGuardCutP2 = bKeystoneGuarded
            ? FMath::Max<int64>(0, BeforeKeystoneGuardP2 - Value.DamageP2) : 0;
        if (Input.bTargetProtected.GetValue())
        { Value.DamageP2 = 0; BraceCutP2 = 0; }
        Value.BraceCutP2 = BraceCutP2;
        Value.KeystoneGuardCutP2 = Input.bTargetProtected.GetValue() ? 0 : KeystoneGuardCutP2;
        Value.ShieldAbsorbedP2 = FMath::Min(Value.DamageP2, Input.TargetShieldP2.GetValue());
        const int64 AfterShieldP2 = Value.DamageP2 - Value.ShieldAbsorbedP2;
        Value.HPLostP2 = FMath::Min(AfterShieldP2, Input.TargetHPP2.GetValue());
        Value.OverkillP2 = AfterShieldP2 - Value.HPLostP2;
        Value.RemainingHPP2 = Input.TargetHPP2.GetValue() - Value.HPLostP2;
        Value.RemainingShieldP2 = Input.TargetShieldP2.GetValue() - Value.ShieldAbsorbedP2;
        FLDDamageEvaluationResult Result;
        Result.Value = Value;
        return Result;
    }

    bool DamageFactorProduct(const TArray<FLDDamageFactor>& Factors, int64 CapBp, int64& OutBp, bool& bOutCapped, FString& Error)
    {
        OutBp = 10000;
        bOutCapped = false;
        if (CapBp <= 10000)
        {
            Error = TEXT("The damage factor cap must sit above x1.");
            return false;
        }
        // Copies of one source add into one multiplier; distinct sources multiply (spec §3.7).
        TArray<FLDDamageFactor, TInlineAllocator<8>> Groups;
        for (const FLDDamageFactor& Factor : Factors)
        {
            if (Factor.Bp < 0 || Factor.Key.IsNone())
            {
                Error = FString::Printf(TEXT("Damage factor %s is negative or unnamed."), *Factor.Key.ToString());
                return false;
            }
            FLDDamageFactor* Group = Groups.FindByPredicate([&Factor](const FLDDamageFactor& Row) { return Row.Key == Factor.Key; });
            if (!Group) { Group = &Groups.Add_GetRef({Factor.Key, 0}); }
            if (Group->Bp > MAX_int64 - 10000 - Factor.Bp)
            {
                Error = FString::Printf(TEXT("Damage factor %s overflows int64."), *Factor.Key.ToString());
                return false;
            }
            Group->Bp += Factor.Bp;
        }
        for (const FLDDamageFactor& Group : Groups)
        {
            const int64 Multiplier = 10000 + Group.Bp;
            // The int64 pre-check: an overflow fails explicitly, it never saturates.
            if (OutBp > MAX_int64 / Multiplier)
            {
                Error = FString::Printf(TEXT("Damage factor product overflows int64 at %s."), *Group.Key.ToString());
                return false;
            }
            OutBp = OutBp * Multiplier / 10000;
            if (OutBp > CapBp)
            {
                OutBp = CapBp;
                bOutCapped = true;
            }
        }
        return true;
    }

    FLDBasicTimingResult EvaluateBasicTiming(const FLDBasicTimingInput& Input, const FLDDemoContentSnapshot& Catalog)
    {
        using EError = ELDCombatEvaluationError;
        const auto Fail = [](EError Code, const TCHAR* Field, const FString& Message)
        {
            return EvaluationFailure<FLDBasicTimingResult>(Code, Field, Message);
        };
        const auto& Content = Catalog.Get();
        if (Content.Formulas.AttackIntervalMs != ELDDemoFormula::CeilReciprocalMs
            || Content.Formulas.AttackWindupMs != ELDDemoFormula::CeilBaseWindupTimesBaseOverCurrentAS)
        {
            return Fail(EError::Definition, TEXT("Catalog"), TEXT("Unsupported basic-attack timing contract"));
        }
        if (!Input.BaseAttackSpeed.IsSet() || !Input.BaseWindupMs.IsSet())
        {
            return Fail(EError::Definition, TEXT("BasicDefinition"), TEXT("Compiled base attack speed and windup are required"));
        }
        if (!Input.CurrentAttackSpeed.IsSet())
        {
            return Fail(EError::Numeric, TEXT("CurrentAttackSpeed"), TEXT("Current attack speed must be resolved"));
        }
        const auto& Base = Input.BaseAttackSpeed.GetValue();
        const auto& Current = Input.CurrentAttackSpeed.GetValue();
        if (Base.Denominator <= 0 || Current.Denominator <= 0)
        {
            return Fail(EError::Numeric, TEXT("AttackSpeed"), TEXT("Attack-speed rational denominators must be positive"));
        }
        if (Base.Numerator <= 0 || Current.Numerator <= 0 || Input.BaseWindupMs.GetValue() <= 0)
        {
            return Fail(EError::Domain, TEXT("BasicTiming"), TEXT("Attack speeds and base windup must be positive"));
        }

        FString MathError;
        LDDemoContent::FCheck Check(MathError);
        LDDemoContent::FNumbers Numbers(Check);
        const auto CurrentRate = Numbers.Value(Current);
        const auto& Bounds = Content.Parameters.AttackSpeed;
        const bool bInRange = Numbers.LE(Numbers.Value(Bounds.Min), CurrentRate)
            && Numbers.LE(CurrentRate, Numbers.Value(Bounds.Max));
        if (!Check.Good)
        {
            return Fail(EError::Overflow, TEXT("CurrentAttackSpeed"), MathError);
        }
        if (!bInRange)
        {
            return Fail(EError::Domain, TEXT("CurrentAttackSpeed"), TEXT("Current attack speed is outside the catalog domain; project it before evaluation"));
        }
        FLDBasicTiming Value;
        if (!Numbers.Interval(Current, Value.IntervalMs, TEXT("IntervalMs")))
        {
            return Fail(EError::Overflow, TEXT("IntervalMs"), MathError);
        }
        const auto Windup = Numbers.Mul(Numbers.Value(Input.BaseWindupMs.GetValue()),
            Numbers.Mul(Numbers.Value(Base), Numbers.Value(Current.Denominator, Current.Numerator)));
        if (!QuantizeEvaluation(Windup, true, Value.WindupMs, Check, TEXT("WindupMs")))
        {
            return Fail(EError::Overflow, TEXT("WindupMs"), MathError);
        }
        if (Value.WindupMs <= 0 || Value.WindupMs >= Value.IntervalMs)
        {
            return Fail(EError::Domain, TEXT("WindupMs"), TEXT("Quantized windup must be positive and shorter than the attack interval"));
        }
        FLDBasicTimingResult Result;
        Result.Value = Value;
        return Result;
    }

    bool CooldownDurationMs(int64 BaseMs, int64 HasteP2, int64 HasteScaleP2, int64& OutMs, FString& Error)
    {
        OutMs = 0;
        if (HasteScaleP2 <= 0 || HasteP2 < 0 || BaseMs < 0 || BaseMs > MAX_int64 / HasteScaleP2
            || HasteP2 > MAX_int64 - HasteScaleP2)
        {
            Error = TEXT("Cooldown inputs are outside the checked numeric domain.");
            return false;
        }
        const int64 Numerator = BaseMs * HasteScaleP2;
        const int64 Denominator = HasteScaleP2 + HasteP2;
        OutMs = Numerator / Denominator + (Numerator % Denominator != 0 ? 1 : 0);
        Error.Reset();
        return true;
    }

    bool HasActiveRoot(const TArray<FLDCombatRoot>& Roots, int64 AtMs)
    {
        return Roots.ContainsByPredicate([AtMs](const FLDCombatRoot& Root) { return Root.ExpiresAtMs > AtMs; });
    }

    int64 ActiveRootUntilMs(const TArray<FLDCombatRoot>& Roots, int64 AtMs)
    {
        int64 Until = 0;
        for (const auto& Root : Roots)
        {
            if (Root.ExpiresAtMs > AtMs) { Until = FMath::Max(Until, Root.ExpiresAtMs); }
        }
        return Until;
    }

    bool RootBlocks(const TArray<FLDCombatRoot>& Roots, int64 AtMs, const FLDDemoPolicies& Policies, ELDDemoRootBlock Action)
    {
        return Policies.RootBlocks.Contains(Action) && HasActiveRoot(Roots, AtMs);
    }

    bool ApplyRoot(TArray<FLDCombatRoot>& Roots, const FLDCombatRoot& Instance, int64 AtMs)
    {
        if (!Instance.EffectId.IsValid() || Instance.ExpiresAtMs <= AtMs
            || Roots.ContainsByPredicate([&Instance](const FLDCombatRoot& Root) { return Root.EffectId == Instance.EffectId; }))
        {
            return false;
        }
        Roots.Add(Instance);
        return true;
    }

    int32 DispelRoots(TArray<FLDCombatRoot>& Roots)
    {
        const int32 Removed = Roots.Num();
        Roots.Reset();
        return Removed;
    }

    bool ApplyShield(TOptional<FLDCombatShield>& Shield, int64& ShieldP2, const FLDCombatShield& Candidate, int64 AmountP2, int64 AtMs)
    {
        if (AmountP2 <= 0 || !Candidate.EffectId.IsValid() || Candidate.ExpiresAtMs <= AtMs) { return false; }
        const bool bLive = Shield.IsSet() && Shield->ExpiresAtMs > AtMs;
        const int64 Remaining = bLive ? FMath::Max<int64>(ShieldP2, 0) : 0;
        if (Remaining > MAX_int64 - AmountP2) { return false; }
        const int64 ExpiresAtMs = bLive ? FMath::Max(Shield->ExpiresAtMs, Candidate.ExpiresAtMs) : Candidate.ExpiresAtMs;
        Shield = Candidate;
        Shield->ExpiresAtMs = ExpiresAtMs;
        ShieldP2 = Remaining + AmountP2;
        return true;
    }

    FLDStatusExpiry ExpireTimedStatuses(TArray<FLDCombatRoot>& Roots, TOptional<FLDCombatShield>& Shield, int64& ShieldP2, int64 AtMs)
    {
        FLDStatusExpiry Result;
        Result.bRootsChanged = Roots.RemoveAll([AtMs](const FLDCombatRoot& Root) { return Root.ExpiresAtMs <= AtMs; }) > 0;
        if (Shield.IsSet() ? (Shield->ExpiresAtMs <= AtMs || ShieldP2 <= 0) : ShieldP2 != 0)
        {
            Shield.Reset();
            ShieldP2 = 0;
            Result.bShieldChanged = true;
        }
        return Result;
    }

    bool ApplySlow(TArray<FLDCombatSlow>& Slows, const FLDCombatSlow& Instance, int64 AtMs)
    {
        if (!Instance.EffectId.IsValid() || Instance.ExpiresAtMs <= AtMs || Instance.RatioBp <= 0 || Instance.RatioBp >= 10000
            || Slows.ContainsByPredicate([&Instance](const FLDCombatSlow& Slow) { return Slow.EffectId == Instance.EffectId; }))
        {
            return false;
        }
        Slows.Add(Instance);
        return true;
    }

    int64 StrongestSlowBp(const TArray<FLDCombatSlow>& Slows, int64 AtMs)
    {
        int64 Strongest = 0;
        for (const auto& Slow : Slows)
        {
            if (Slow.ExpiresAtMs > AtMs) { Strongest = FMath::Max(Strongest, Slow.RatioBp); }
        }
        return Strongest;
    }

    int32 DispelSlows(TArray<FLDCombatSlow>& Slows, int64 AtMs)
    {
        const int32 Active = Slows.FilterByPredicate([AtMs](const FLDCombatSlow& Slow) { return Slow.ExpiresAtMs > AtMs; }).Num();
        Slows.Reset();
        return Active;
    }

    int64 StrongestSlowUntilMs(const TArray<FLDCombatSlow>& Slows, int64 AtMs)
    {
        const int64 Strongest = StrongestSlowBp(Slows, AtMs);
        int64 Until = 0;
        for (const auto& Slow : Slows)
        {
            if (Strongest > 0 && Slow.ExpiresAtMs > AtMs && Slow.RatioBp == Strongest) { Until = FMath::Max(Until, Slow.ExpiresAtMs); }
        }
        return Until;
    }

    void ApplyTimedBonus(TArray<FLDCombatTimedBonus>& Bonuses, const FLDCombatTimedBonus& Bonus)
    {
        Bonuses.RemoveAll([&Bonus](const FLDCombatTimedBonus& Existing) { return Existing.SourceDefinition == Bonus.SourceDefinition; });
        Bonuses.Add(Bonus);
    }

    bool ExpireTemporaryStatuses(TArray<FLDCombatSlow>& Slows, TArray<FLDCombatTimedBonus>& Bonuses, int64 AtMs)
    {
        return Slows.RemoveAll([AtMs](const FLDCombatSlow& Slow) { return Slow.ExpiresAtMs <= AtMs; })
            + Bonuses.RemoveAll([AtMs](const FLDCombatTimedBonus& Bonus) { return Bonus.ExpiresAtMs <= AtMs; }) > 0;
    }

    bool BelowHPThreshold(int64 HPP2, int64 MaxHPP2, int64 ThresholdBp)
    {
        if (HPP2 <= 0 || MaxHPP2 <= 0 || ThresholdBp <= 0 || ThresholdBp >= 10000
            || HPP2 > MAX_int64 / 10000 || MaxHPP2 > MAX_int64 / ThresholdBp) { return false; }
        return HPP2 * 10000 < MaxHPP2 * ThresholdBp;
    }

    FLDTemporaryAttributeTerms TemporaryTerms(const TArray<FLDCombatTimedBonus>& Bonuses, const TArray<FLDCombatSlow>& Slows,
        int64 AtMs)
    {
        FLDTemporaryAttributeTerms Terms;
        for (const auto& Bonus : Bonuses)
        {
            if (Bonus.ExpiresAtMs > AtMs) { Terms.Bonuses.Append(Bonus.Stats); }
        }
        Terms.MoveSlowBp = StrongestSlowBp(Slows, AtMs);
        return Terms;
    }

    bool IntegrateRegen(const FLDDemoRational& RatePerSec, int64 ElapsedMs, int64 MaxP2,
        int64& InOutP2, int64& InOutRemainderMicroP2, FString& Error)
    {
        constexpr int64 MicroPerP2 = 1000000;
        // One point per second is 100 P2 per 1000 ms, i.e. 1e5 micro-P2 per millisecond.
        constexpr int64 MicroPerPointMs = 100000;
        int64 Numerator = RatePerSec.Numerator;
        int64 Denominator = RatePerSec.Denominator;
        if (Numerator < 0 || Denominator <= 0 || ElapsedMs < 0 || MaxP2 < 0 || InOutP2 < 0 || InOutP2 > MaxP2
            || InOutRemainderMicroP2 < 0 || InOutRemainderMicroP2 >= MicroPerP2)
        {
            Error = TEXT("Regeneration input is outside its numeric domain.");
            return false;
        }
        if (InOutP2 == MaxP2)
        {
            InOutRemainderMicroP2 = 0;
            Error.Reset();
            return true;
        }
        if (Numerator == 0 || ElapsedMs == 0)
        {
            Error.Reset();
            return true;
        }
        for (int64 A = Numerator, B = Denominator; ; )
        {
            const int64 Next = A % B;
            if (Next == 0) { Numerator /= B; Denominator /= B; break; }
            A = B;
            B = Next;
        }
        if (Numerator > MAX_int64 / MicroPerPointMs || (Numerator * MicroPerPointMs) % Denominator != 0)
        {
            Error = TEXT("Regeneration rate is not representable in 1e-6 P2 per millisecond.");
            return false;
        }
        const int64 PerMs = Numerator * MicroPerPointMs / Denominator;
        if (ElapsedMs > (MAX_int64 - InOutRemainderMicroP2) / PerMs)
        {
            Error = TEXT("Regeneration integral overflowed.");
            return false;
        }
        const int64 Total = PerMs * ElapsedMs + InOutRemainderMicroP2;
        const int64 Gain = Total / MicroPerP2;
        if (Gain >= MaxP2 - InOutP2)
        {
            // A full pool discards the overflow, including the sub-P2 part.
            InOutP2 = MaxP2;
            InOutRemainderMicroP2 = 0;
        }
        else
        {
            InOutP2 += Gain;
            InOutRemainderMicroP2 = Total % MicroPerP2;
        }
        Error.Reset();
        return true;
    }

    bool OutOfCombatAt(int64 LastCombatAtMs, int64 DelayMs, int64 NowMs)
    {
        return DelayMs >= 0 && LastCombatAtMs <= MAX_int64 - DelayMs && NowMs >= LastCombatAtMs + DelayMs;
    }

    FRegenRates RegenRates(const FLDAttributeProjection& Attributes, const FOutOfCombatRecovery* Recovery,
        int64 MaxHpP2, int64 LastCombatAtMs, int64 NowMs, const FOutOfCombatRecovery* Dorm)
    {
        const auto Value=[](const FLDDemoRational& Rate){return double(Rate.Numerator)/double(Rate.Denominator);};
        FRegenRates Rates{Value(Attributes.Final.HPRegenPerSec)};
        for (const FOutOfCombatRecovery* Term : {Recovery, Dorm})
        {
            if (!Term || !OutOfCombatAt(LastCombatAtMs,Term->DelayMs,NowMs)) { continue; }
            Rates.HP+=double(MaxHpP2)*double(Term->MaxHPBpPerSec)/1000000.;
        }
        return Rates;
    }

    bool SettleRegen(const FLDAttributeProjection& Attributes, const FOutOfCombatRecovery* Recovery,
        int64 LastCombatAtMs, int64 FromMs, int64 NowMs, FRegenPools& InOut, FString& Error, const FOutOfCombatRecovery* Dorm)
    {
        if (NowMs < FromMs)
        {
            Error = TEXT("Regeneration cannot settle a reversed interval.");
            return false;
        }
        // An out-of-combat term starts exactly at LastCombatAt + Delay; the part of the interval before it is combat time.
        const auto TermStart = [LastCombatAtMs, FromMs, NowMs, &Error](int64 DelayMs, int64& OutMs)
        {
            if (DelayMs < 0 || LastCombatAtMs > MAX_int64 - DelayMs)
            {
                Error = TEXT("An out-of-combat regeneration term has no representable start.");
                return false;
            }
            OutMs = FMath::Clamp(LastCombatAtMs + DelayMs, FromMs, NowMs);
            return true;
        };
        // MaxP2 x Bp / 1e6 points per second; a whole 0.001 share (Bp % 10 == 0) is always exact per millisecond.
        const auto Share = [&Error](int64 MaxP2, int64 Bp, FLDDemoRational& Out)
        {
            if (MaxP2 < 0 || Bp < 0 || Bp > 10000 || (Bp != 0 && MaxP2 > MAX_int64 / Bp))
            {
                Error = TEXT("Out-of-combat recovery share is outside its numeric domain.");
                return false;
            }
            Out.Numerator = MaxP2 * Bp;
            Out.Denominator = 1000000;
            return true;
        };
        const int64 Elapsed = NowMs - FromMs;
        if (!IntegrateRegen(Attributes.Final.HPRegenPerSec, Elapsed, InOut.MaxHpP2, InOut.HpP2, InOut.HpMicroP2, Error))
        {
            return false;
        }
        for (const FOutOfCombatRecovery* Term : {Recovery, Dorm})
        {
            if (!Term) { continue; }
            int64 StartMs = NowMs;
            FLDDemoRational HPRate;
            if (!TermStart(Term->DelayMs, StartMs) || !Share(InOut.MaxHpP2, Term->MaxHPBpPerSec, HPRate)
                || !IntegrateRegen(HPRate, NowMs - StartMs, InOut.MaxHpP2, InOut.HpP2, InOut.HpMicroP2, Error))
            {
                return false;
            }
        }
        Error.Reset();
        return true;
    }
}
