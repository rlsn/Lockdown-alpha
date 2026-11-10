#include "Combat/LDCombatElements.h"
#include "Combat/LDCombatDirector.h"
#include "Development/LDDemoContentDefinitions.h"

namespace LDCombatElementsDetail
{
    double Cm(const FLDDemoRational& Value)
    { return Value.Denominator>0 ? double(Value.Numerator)/double(Value.Denominator) : 0.; }

    int64 Share(int64 ValueP2, int64 Bp)
    {
        if (ValueP2<=0 || Bp<=0) { return 0; }
        return ValueP2/10000*Bp+ValueP2%10000*Bp/10000;
    }
}

FLDElementTerms FLDElementTerms::From(const FLDDemoContentParameters& Parameters)
{
    using namespace LDCombatElementsDetail;
    FLDElementTerms Terms;
    Terms.WetMs=Parameters.WetMs;
    Terms.WetSlowBp=Parameters.WetSlowBp;
    Terms.ElectrifiedMs=Parameters.ElectrifiedMs;
    Terms.DischargeRadiusCm=Cm(Parameters.DischargeRadiusCm);
    Terms.DischargeIntervalMs=Parameters.DischargeIntervalMs;
    Terms.ReactionBaseP2=Parameters.ReactionBaseP2;
    Terms.ReactionPerLevelP2=Parameters.ReactionPerLevelP2;
    Terms.ReactionAPRatioBp=Parameters.ReactionAPRatioBp;
    Terms.FogRadiusCm=Cm(Parameters.FogRadiusCm);
    Terms.FogMs=Parameters.FogMs;
    Terms.FogIntervalMs=Parameters.FogIntervalMs;
    Terms.ConductRadiusCm=Cm(Parameters.ConductRadiusCm);
    Terms.ConductSlowBp=Parameters.ConductSlowBp;
    Terms.ConductSlowMs=Parameters.ConductSlowMs;
    Terms.OverloadRadiusCm=Cm(Parameters.OverloadRadiusCm);
    Terms.OverloadBreachStacks=int32(FMath::Clamp<int64>(Parameters.OverloadBreachStacks,0,MAX_int32));
    return Terms;
}

bool FLDElementTerms::IsUsable() const
{
    return WetMs>0 && WetSlowBp>0 && WetSlowBp<10000 && ElectrifiedMs>0 && DischargeRadiusCm>0 && DischargeIntervalMs>0
        && ReactionBaseP2>=0 && ReactionPerLevelP2>=0 && ReactionAPRatioBp>=0 && FogRadiusCm>0 && FogMs>0
        && FogIntervalMs>0 && FogIntervalMs<=FogMs && ConductRadiusCm>0 && ConductSlowBp>0 && ConductSlowBp<10000
        && ConductSlowMs>0 && OverloadRadiusCm>0 && OverloadBreachStacks>0;
}

namespace LDCombatElements
{
    ELDCombatReaction ReactionOf(ELDCombatElement Held, ELDCombatElement Incoming)
    {
        if (Held==ELDCombatElement::None || Incoming==ELDCombatElement::None || Held==Incoming) { return ELDCombatReaction::None; }
        const auto Has=[Held,Incoming](ELDCombatElement Element) { return Held==Element || Incoming==Element; };
        if (Has(ELDCombatElement::Fire) && Has(ELDCombatElement::Water)) { return ELDCombatReaction::Fog; }
        if (Has(ELDCombatElement::Water) && Has(ELDCombatElement::Electric)) { return ELDCombatReaction::Conduct; }
        return ELDCombatReaction::Overload;
    }

    int64 ReactionDamageP2(int32 Level, int64 SourceAPP2, const FLDElementTerms& Terms)
    {
        const int64 Steps=FMath::Max<int64>(Level-1,0);
        const int64 PerLevel=FMath::Max<int64>(Terms.ReactionPerLevelP2,0);
        const int64 Growth=PerLevel>0 && Steps>MAX_int64/PerLevel ? MAX_int64 : PerLevel*Steps;
        const int64 FromAP=LDCombatElementsDetail::Share(FMath::Max<int64>(SourceAPP2,0),FMath::Max<int64>(Terms.ReactionAPRatioBp,0));
        const int64 Base=FMath::Max<int64>(Terms.ReactionBaseP2,0);
        if (Growth>MAX_int64-Base || FromAP>MAX_int64-Base-Growth) { return MAX_int64; }
        return Base+Growth+FromAP;
    }

    bool MarkActive(const FLDCombatElementMark& Mark, int64 AtMs)
    {
        return Mark.Kind!=ELDCombatElement::None && Mark.ExpiresAtMs>AtMs;
    }

    ELDCombatElement ElementOf(bool bBurning, const FLDCombatElementMark& Mark, int64 AtMs)
    {
        if (bBurning) { return ELDCombatElement::Fire; }
        return MarkActive(Mark,AtMs) ? Mark.Kind : ELDCombatElement::None;
    }

    int64 BraceRecordP2(int64 RecordedP2, int64 CutP2, int64 CapP2)
    {
        const int64 Recorded=FMath::Max<int64>(RecordedP2,0), Cut=FMath::Max<int64>(CutP2,0), Cap=FMath::Max<int64>(CapP2,0);
        return Cut>=Cap-FMath::Min(Recorded,Cap) ? Cap : Recorded+Cut;
    }

    int64 BraceAnswerP2(const FLDCombatBrace& Brace)
    {
        const int64 Base=FMath::Max<int64>(Brace.BaseP2,0);
        const int64 FromRecord=LDCombatElementsDetail::Share(Brace.RecordedP2,Brace.RecordRatioBp);
        return FromRecord>MAX_int64-Base ? MAX_int64 : Base+FromRecord;
    }

    bool BraceActive(const FLDCombatBrace& Brace, int64 AtMs)
    {
        return Brace.EffectId.IsValid() && AtMs<Brace.EndsAtMs;
    }

    int64 AfterGrievousP2(int64 AmountP2, const FLDCombatGrievous& Grievous, int64 AtMs)
    {
        if (AmountP2<=0) { return 0; }
        if (Grievous.ExpiresAtMs<=AtMs || Grievous.RatioBp<=0) { return AmountP2; }
        return AmountP2-LDCombatElementsDetail::Share(AmountP2,FMath::Min<int64>(Grievous.RatioBp,10000));
    }

    bool ApplyGrievous(FLDCombatGrievous& Grievous, int64 RatioBp, int64 ExpiresAtMs, int64 AtMs)
    {
        if (RatioBp<=0 || RatioBp>=10000 || ExpiresAtMs<=AtMs) { return false; }
        if (Grievous.ExpiresAtMs<=AtMs) { Grievous=FLDCombatGrievous(); }
        Grievous.RatioBp=FMath::Max(Grievous.RatioBp,RatioBp);
        Grievous.ExpiresAtMs=FMath::Max(Grievous.ExpiresAtMs,ExpiresAtMs);
        return true;
    }

    ELDCombatHitCategory CategoryOf(const FLDCombatHit& Hit)
    {
        if (Hit.bStatusSettle || Hit.bBurnTick) { return ELDCombatHitCategory::StatusSettle; }
        if (Hit.HitClass==ELDCombatHitClass::BasicPrimary) { return ELDCombatHitCategory::BasicPrimary; }
        if (Hit.bDirectAbilityDamage) { return ELDCombatHitCategory::SkillDirect; }
        return ELDCombatHitCategory::SkillHit;
    }

    const TCHAR* ReactionName(ELDCombatReaction Reaction)
    {
        switch (Reaction)
        {
        case ELDCombatReaction::Fog: return TEXT("fog");
        case ELDCombatReaction::Conduct: return TEXT("conduct");
        case ELDCombatReaction::Overload: return TEXT("overload");
        default: return TEXT("none");
        }
    }
}
