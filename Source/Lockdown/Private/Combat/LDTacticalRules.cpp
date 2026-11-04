#include "Combat/LDTacticalRules.h"
#include "../Development/LDDemoContentMath.h"

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
        if (Input.HitClass != ELDCombatHitClass::BasicPrimary && Input.HitClass != ELDCombatHitClass::BasicSecondary
            && Input.HitClass != ELDCombatHitClass::Ability)
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
        if (Input.bCritical.GetValue() && Input.HitClass != ELDCombatHitClass::BasicPrimary)
        {
            return Fail(EError::Domain, TEXT("bCritical"), TEXT("Only a primary basic hit can be critical"));
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
        if (Input.bCritical.GetValue())
        {
            Damage = Numbers.Mul(Damage, Numbers.Value(Parameters.CritMultiplierBp, 10000));
        }
        if (Input.AmplifyBp < 0)
        {
            return Fail(EError::Domain, TEXT("AmplifyBp"), TEXT("A damage amplification cannot be negative"));
        }
        if (Input.AmplifyBp != 10000)
        {
            Damage = Numbers.Mul(Damage, Numbers.Value(Input.AmplifyBp, 10000));
        }
        int64 ResistanceDenominatorP2;
        if (!Numbers.Add64(Formulas.ResistanceScaleP2, Input.TargetResistanceP2.GetValue(),
            ResistanceDenominatorP2, TEXT("TargetResistanceP2")))
        {
            return Fail(EError::Overflow, TEXT("TargetResistanceP2"), MathError);
        }
        Damage = Numbers.Mul(Damage, Numbers.Value(Formulas.ResistanceScaleP2, ResistanceDenominatorP2));
        FLDDamageEvaluation Value;
        if (!QuantizeEvaluation(Damage, false, Value.DamageP2, Check, TEXT("DamageP2")))
        {
            return Fail(EError::Overflow, TEXT("DamageP2"), MathError);
        }
        // Protection cannot hide missing input, an invalid domain or arithmetic overflow.
        if (Input.bTargetProtected.GetValue()) { Value.DamageP2 = 0; }
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
        const int64 Remaining = Shield.IsSet() && Shield->ExpiresAtMs > AtMs ? FMath::Max<int64>(ShieldP2, 0) : 0;
        if (AmountP2 < Remaining) { return false; }
        Shield = Candidate;
        ShieldP2 = AmountP2;
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

    void ApplyExposure(TArray<FLDCombatExposure>& Exposures, const FLDCombatExposure& Exposure)
    {
        Exposures.RemoveAll([&Exposure](const FLDCombatExposure& Existing) { return Existing.SourceEntity == Exposure.SourceEntity; });
        Exposures.Add(Exposure);
    }

    int64 ExposureBp(const TArray<FLDCombatExposure>& Exposures, const FGuid& Source, int64 AtMs)
    {
        for (const auto& Exposure : Exposures)
        {
            if (Source.IsValid() && Exposure.SourceEntity == Source && Exposure.ExpiresAtMs > AtMs) { return Exposure.AmplifyBp; }
        }
        return 0;
    }

    bool ExpireTemporaryStatuses(TArray<FLDCombatSlow>& Slows, TArray<FLDCombatTimedBonus>& Bonuses,
        TArray<FLDCombatExposure>& Exposures, int64 AtMs)
    {
        const int32 Removed = Slows.RemoveAll([AtMs](const FLDCombatSlow& Slow) { return Slow.ExpiresAtMs <= AtMs; })
            + Bonuses.RemoveAll([AtMs](const FLDCombatTimedBonus& Bonus) { return Bonus.ExpiresAtMs <= AtMs; })
            + Exposures.RemoveAll([AtMs](const FLDCombatExposure& Exposure) { return Exposure.ExpiresAtMs <= AtMs; });
        return Removed > 0;
    }

    bool BelowHPThreshold(int64 HPP2, int64 MaxHPP2, int64 ThresholdBp)
    {
        if (HPP2 <= 0 || MaxHPP2 <= 0 || ThresholdBp <= 0 || ThresholdBp >= 10000
            || HPP2 > MAX_int64 / 10000 || MaxHPP2 > MAX_int64 / ThresholdBp) { return false; }
        return HPP2 * 10000 < MaxHPP2 * ThresholdBp;
    }

    bool AmbushReady(int64 LastCombatAtMs, int64 DelayMs, int64 AtMs)
    {
        return DelayMs > 0 && LastCombatAtMs <= MAX_int64 - DelayMs && AtMs >= LastCombatAtMs + DelayMs;
    }

    FLDTemporaryAttributeTerms TemporaryTerms(const TArray<FLDCombatTimedBonus>& Bonuses, const TArray<FLDCombatSlow>& Slows,
        const TOptional<FLDLowHPBonusTerm>& LowHP, int64 HPP2, int64 MaxHPP2, int64 AtMs)
    {
        FLDTemporaryAttributeTerms Terms;
        for (const auto& Bonus : Bonuses)
        {
            if (Bonus.ExpiresAtMs > AtMs) { Terms.Bonuses.Append(Bonus.Stats); }
        }
        if (LowHP.IsSet() && BelowHPThreshold(HPP2, MaxHPP2, LowHP->ThresholdBp)) { Terms.Bonuses.Append(LowHP->Stats); }
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

    bool SettleRegen(const FLDAttributeProjection& Attributes, const FOutOfCombatRecovery* Recovery,
        int64 LastCombatAtMs, int64 FromMs, int64 NowMs, FRegenPools& InOut, FString& Error)
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
        if (!IntegrateRegen(Attributes.Final.HPRegenPerSec, Elapsed, InOut.MaxHpP2, InOut.HpP2, InOut.HpMicroP2, Error)
            || !IntegrateRegen(Attributes.Final.MPRegenPerSec, Elapsed, InOut.MaxMpP2, InOut.MpP2, InOut.MpMicroP2, Error))
        {
            return false;
        }
        if (Attributes.OutOfCombatHPRegen.IsSet())
        {
            int64 StartMs = NowMs;
            if (!TermStart(Attributes.OutOfCombatHPRegen->DelayMs, StartMs)
                || !IntegrateRegen(Attributes.OutOfCombatHPRegen->HPPerSec, NowMs - StartMs, InOut.MaxHpP2, InOut.HpP2,
                    InOut.HpMicroP2, Error))
            {
                return false;
            }
        }
        if (Recovery)
        {
            int64 StartMs = NowMs;
            FLDDemoRational HPRate, MPRate;
            if (!TermStart(Recovery->DelayMs, StartMs) || !Share(InOut.MaxHpP2, Recovery->MaxHPBpPerSec, HPRate)
                || !Share(InOut.MaxMpP2, Recovery->MaxMPBpPerSec, MPRate)
                || !IntegrateRegen(HPRate, NowMs - StartMs, InOut.MaxHpP2, InOut.HpP2, InOut.HpMicroP2, Error)
                || !IntegrateRegen(MPRate, NowMs - StartMs, InOut.MaxMpP2, InOut.MpP2, InOut.MpMicroP2, Error))
            {
                return false;
            }
        }
        Error.Reset();
        return true;
    }
}
