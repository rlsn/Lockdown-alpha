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
        if (Definition.BaseP2 < 0 || Definition.ADRatioBp < 0 || Definition.APRatioBp < 0)
        {
            return Fail(EError::Domain, TEXT("Definition"), TEXT("Damage points and R4 coefficients must be nonnegative"));
        }
        if (Input.HitClass != ELDCombatHitClass::Ability
            && (Definition.APRatioBp != 0 || Definition.DamageType != ELDDemoDamageType::Physical))
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
        if (Input.bCritical.GetValue())
        {
            Damage = Numbers.Mul(Damage, Numbers.Value(Parameters.CritMultiplierBp, 10000));
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

    int64 Milliseconds(double Seconds){return FMath::FloorToInt64(Seconds*1000.+.000001);}
    bool FState::Has(EStatus K,int64 At) const{return Statuses.ContainsByPredicate([&](const FStatus& S){return S.Kind==K&&S.Until>At;});}
    uint16 FState::Mask(int64 At) const{uint16 Result=0;for(const auto& S:Statuses){if(S.Until>At){Result|=1<<uint8(S.Kind);}}return Result;}
    double MaxMP(const FLDCharacterBuild& B){return 180.+12*(LDDevelopment::Effective(B)[3]-8)+6*(LDDevelopment::Effective(B)[4]-8)+5*(B.Level-1);}
    bool Allows(const FState& S,EAction A,int64 At,FString* Why)
    {
        const TCHAR* Reason=nullptr;
        if(S.Has(EStatus::Stun,At)){Reason=TEXT("Stunned: external strong dispel or expiry required");}
        else if(S.Has(EStatus::Hex,At)&&A!=EAction::Move){Reason=TEXT("Hexed: external strong dispel or expiry required");}
        else if(A==EAction::Skill&&S.Has(EStatus::Silence,At)){Reason=TEXT("Silenced: skills blocked; active items remain legal");}
        else if(A==EAction::Item&&S.Has(EStatus::Mute,At)){Reason=TEXT("Muted: active items blocked; skills remain legal");}
        else if((A==EAction::Move||A==EAction::Dash)&&S.Has(EStatus::Root,At)){Reason=TEXT("Rooted: voluntary movement blocked");}
        else if(A==EAction::Dash&&S.GroundedUntil>At){Reason=TEXT("Grounded: destroy emitter or walk out");}
        else if(A==EAction::Attack&&S.Has(EStatus::Disarm,At)){Reason=TEXT("Disarmed: basic attacks blocked");}
        if(Why){*Why=Reason?Reason:TEXT("");}return !Reason;
    }
    void Apply(FState& S,EStatus K,int64 Until,FGuid Source,int32 Magnitude)
    {if(auto* Old=S.Statuses.FindByPredicate([&](const FStatus& V){return V.Kind==K&&V.Source==Source;})){Old->Until=FMath::Max(Old->Until,Until);Old->Magnitude=FMath::Max(Old->Magnitude,Magnitude);}else{S.Statuses.Add({K,Until,Source,Magnitude});}}
    void Dispel(FState& S,bool Strong,bool Positive)
    {
        if(Positive){S.Barrier=0;S.BarrierUntil=0;S.AnchorUntil=0;return;}
        S.Statuses.RemoveAll([&](const FStatus& V){return Strong||(V.Kind!=EStatus::Stun&&V.Kind!=EStatus::Hex&&V.Kind!=EStatus::Break);});
    }
    void Barrier(FState& S,double Amount,double MaxHP,int64 Until,int64 At)
    {const double Pool=FMath::Clamp(Amount,0.,.4*MaxHP);if(At>=S.BarrierUntil||Pool>S.Barrier){S.Barrier=Pool;S.BarrierUntil=Until;S.Directional=false;}}
    bool ActivateGuard(FState& S,int64 At)
    {if(At<S.MajorReadyAt){return false;}S.MajorReadyAt=At+8000;S.GuardUntil=At+1500;S.ConcealedUntil=0;return true;}
    bool GuardControl(FState& S,EStatus K,int64 At)
    {if((K!=EStatus::Stun&&K!=EStatus::Hex)||S.GuardUntil<=At){return false;}S.GuardUntil=0;return true;}
    bool PassiveActive(const FState& S,bool Breakable,int64 At){return !Breakable||!S.Has(EStatus::Break,At);}
    float BodySight(const FState& S,int64 At){return S.Has(EStatus::Blind,At)?200.f:720.f;}
    bool Block(FState& S,int64 At){if(S.BlockUntil<=At){return false;}S.BlockUntil=0;return true;}
    double Damage(double& HP,FState& S,double Amount,EDamage Kind,double Armor,double MR,int64 At,bool UseBarrier)
    {
        if(!FMath::IsFinite(Amount)||Amount<=0){return 0;}
        const double A=FMath::Max(0.,Armor);
        const double Reduction=Kind==EDamage::Physical?FMath::Min(.6,A/(100.+A)):Kind==EDamage::Magical?FMath::Clamp(MR,0.,.6):0;
        double D=FMath::FloorToDouble(Amount*(1-Reduction)*100.+.000001)/100.;
        const double Contact=D;
        if(At>=S.BarrierUntil){S.Barrier=0;}
        const double Absorbed=UseBarrier?FMath::Min(D,S.Barrier):0.;S.Barrier-=Absorbed;D-=Absorbed;
        const double Lost=FMath::Min(HP,D);HP=FMath::Max(0.,HP-D);return FMath::Min(Contact,Absorbed+Lost);
    }
    void Regenerate(double& HP,FState& S,double MaxHP,double MPMax,int64 At,int32 MPRegen)
    {
        if(S.LastRegen<0){S.LastRegen=At;return;}if(At<=S.LastRegen){return;}
        const int64 Elapsed=At-S.LastRegen;S.LastRegen=At;
        const int64 H=100*Elapsed+S.HPRegenRemainder,P=int64(MPRegen)*Elapsed+S.MPRegenRemainder;S.HPRegenRemainder=H%1000;S.MPRegenRemainder=P%1000;
        if(HP>0){HP=FMath::Min(MaxHP,HP+(H/1000)/100.);S.MP=FMath::Min(MPMax,S.MP+(P/1000)/100.);}
        S.Statuses.RemoveAll([&](const FStatus& V){return V.Until<=At;});if(S.BarrierUntil<=At){S.Barrier=0;}
    }
    FString StatusText(const FState& S,int64 At)
    {
        static const TCHAR* Names[]={TEXT("Stun / strong dispel"),TEXT("Silence / basic dispel"),TEXT("Mute / basic dispel"),TEXT("Root / basic dispel"),TEXT("Disarm / basic dispel"),TEXT("Hex / strong dispel"),TEXT("Break / strong dispel"),TEXT("Blind / basic dispel"),TEXT("Slow / basic dispel"),TEXT("Ledger mark / basic dispel"),TEXT("Wet / basic dispel"),TEXT("Burning / Water or basic dispel")};
        FString Result;
        for(uint8 K=0;K<12;++K){int64 Until=At;for(const auto& V:S.Statuses){if(uint8(V.Kind)==K){Until=FMath::Max(Until,V.Until);}}if(Until>At){Result+=FString::Printf(TEXT("%s %.1fs · "),Names[K],(Until-At)/1000.);}}
        if(S.ConcealedUntil>At){Result+=TEXT("Concealed · ");}
        if(S.GroundedUntil>At){Result+=TEXT("Grounded / source-bound · ");}
        if(S.GuardUntil>At){Result+=TEXT("Control guard / one charge · ");}
        return Result;
    }
}
