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
        if (ValueP2/10000>MAX_int64/Bp) { return MAX_int64; }
        return ValueP2/10000*Bp+ValueP2%10000*Bp/10000;
    }

    int32 Small(int64 Value) { return int32(FMath::Clamp<int64>(Value,0,MAX_int32)); }
}

FLDElementTerms FLDElementTerms::From(const FLDDemoContentParameters& Parameters)
{
    using namespace LDCombatElementsDetail;
    const FLDDemoElementParameters& E=Parameters.Elements;
    FLDElementTerms Terms;
    Terms.WetMs=Parameters.WetMs;
    Terms.WetSlowBp=Parameters.WetSlowBp;
    Terms.ShockMs=E.ShockMs;
    Terms.ShockEveryNth=Small(E.ShockEveryNthHit);
    Terms.ShockRatioBp=E.ShockRatioBp;
    Terms.ShockSplashRadiusCm=Cm(E.ShockSplashRadiusCm);
    Terms.ChillMs=E.ChillMs;
    Terms.ChillAttackSlowBp=E.ChillAttackSlowBp;
    Terms.ChillMoveSlowBp=E.ChillMoveSlowBp;
    Terms.RotMs=E.RotMs;
    Terms.RotHealCutBp=E.RotHealCutBp;
    Terms.RotPerSecP2=E.RotPerSecP2;
    Terms.RotAPRatioPerSecBp=E.RotAPRatioPerSecBp;
    Terms.RotTickMs=E.RotTickMs;
    Terms.GustPushCm=Cm(E.GustPushCm);
    Terms.GustPushMs=E.GustPushMs;
    Terms.ReactionBaseP2=Parameters.ReactionBaseP2;
    Terms.ReactionPerLevelP2=Parameters.ReactionPerLevelP2;
    Terms.ReactionAPRatioBp=Parameters.ReactionAPRatioBp;
    Terms.ReactionLockMs=Parameters.ReactionLockMs;
    Terms.FogRadiusCm=Cm(Parameters.FogRadiusCm);
    Terms.FogMs=Parameters.FogMs;
    Terms.FogIntervalMs=Parameters.FogIntervalMs;
    Terms.ConductRatioBp=E.ConductRatioBp;
    Terms.ConductRadiusCm=Cm(Parameters.ConductRadiusCm);
    Terms.ConductSlowBp=Parameters.ConductSlowBp;
    Terms.ConductSlowMs=Parameters.ConductSlowMs;
    Terms.OverloadRatioBp=E.OverloadRatioBp;
    Terms.OverloadRadiusCm=Cm(Parameters.OverloadRadiusCm);
    Terms.OverloadBreachStacks=Small(Parameters.OverloadBreachStacks);
    Terms.MeltRatioBp=E.MeltRatioBp;
    Terms.MethaneRatioBp=E.MethaneRatioBp;
    Terms.MethaneBurnStacks=Small(E.MethaneBurnStacks);
    Terms.MethaneRadiusCm=Cm(E.MethaneRadiusCm);
    Terms.FirestormBurnScale=Small(E.FirestormBurnScale);
    Terms.FreezeRootMs=E.FreezeRootMs;
    Terms.FermentRotMs=E.FermentRotMs;
    Terms.FermentHealCutBp=E.FermentHealCutBp;
    Terms.SplashPushCm=Cm(E.SplashPushCm);
    Terms.ShatterSkillDamageBp=E.ShatterSkillDamageBp;
    Terms.ShatterMs=E.ShatterMs;
    Terms.CorrodeBreachStacks=Small(E.CorrodeBreachStacks);
    Terms.CorrodeBreachMs=E.CorrodeBreachMs;
    Terms.ThunderstormKnockbackCm=Cm(E.ThunderstormKnockbackCm);
    Terms.FrostbiteAttackSlowBp=E.FrostbiteAttackSlowBp;
    Terms.FrostbiteMs=E.FrostbiteMs;
    Terms.BlizzardRatioBp=E.BlizzardRatioBp;
    Terms.SmokeDisarmMs=E.SmokeDisarmMs;
    return Terms;
}

bool FLDElementTerms::IsUsable() const
{
    const auto Ratio=[](int64 Bp) { return Bp>0 && Bp<10000; };
    return WetMs>0 && Ratio(WetSlowBp) && ShockMs>0 && ShockEveryNth>0 && ShockRatioBp>0 && ShockSplashRadiusCm>0
        && ChillMs>0 && Ratio(ChillAttackSlowBp) && Ratio(ChillMoveSlowBp) && RotMs>0 && Ratio(RotHealCutBp)
        && RotPerSecP2>=0 && RotAPRatioPerSecBp>=0 && RotTickMs>0 && GustPushCm>0 && GustPushMs>0
        && ReactionBaseP2>=0 && ReactionPerLevelP2>=0 && ReactionAPRatioBp>=0 && FogRadiusCm>0 && FogMs>0
        && FogIntervalMs>0 && FogIntervalMs<=FogMs && ConductRatioBp>0 && ConductRadiusCm>0 && Ratio(ConductSlowBp)
        && ConductSlowMs>0 && OverloadRatioBp>0 && OverloadRadiusCm>0 && OverloadBreachStacks>0 && MeltRatioBp>0
        && MethaneRatioBp>0 && MethaneBurnStacks>0 && MethaneRadiusCm>0 && FirestormBurnScale>1 && FreezeRootMs>0
        && FermentRotMs>0 && Ratio(FermentHealCutBp) && SplashPushCm>0 && ShatterSkillDamageBp>0 && ShatterMs>0
        && CorrodeBreachStacks>0 && CorrodeBreachMs>0 && ThunderstormKnockbackCm>0 && Ratio(FrostbiteAttackSlowBp)
        && FrostbiteMs>0 && BlizzardRatioBp>0 && SmokeDisarmMs>0;
}

FLDGroundTerms FLDGroundTerms::From(const FLDDemoContentParameters& Parameters)
{
    using namespace LDCombatElementsDetail;
    const FLDDemoGroundParameters& G=Parameters.Ground;
    FLDGroundTerms Terms;
    const auto Kind=[&Terms](ELDGroundStateKind At, const FLDDemoRational& Radius, int64 LifeMs, int64 TickMs)
    { Terms.Kinds[uint8(At)]={Cm(Radius),LifeMs,TickMs}; };
    Kind(ELDGroundStateKind::Puddle,G.PuddleRadiusCm,G.PuddleMs,G.PuddleTickMs);
    // Ice lays nothing on a tick (its inertia is read by every authority step from the bodies standing on it).
    Kind(ELDGroundStateKind::Ice,G.IceRadiusCm,G.IceMs,G.IceMs);
    Kind(ELDGroundStateKind::Fire,G.FireRadiusCm,G.FireMs,G.FireTickMs);
    Kind(ELDGroundStateKind::Miasma,G.MiasmaRadiusCm,G.MiasmaMs,G.MiasmaTickMs);
    Kind(ELDGroundStateKind::Field,G.FieldRadiusCm,G.FieldMs,G.FieldTickMs);
    Kind(ELDGroundStateKind::Blizzard,G.BlizzardRadiusCm,G.BlizzardMs,G.BlizzardTickMs);
    Terms.IceAccelScaleBp=G.IceAccelScaleBp;
    Terms.IceDisplacementScaleBp=G.IceDisplacementScaleBp;
    Terms.FireBurnStacks=Small(G.FireBurnStacks);
    Terms.FieldRatioBp=G.FieldRatioBp;
    Terms.FieldDischargeRadiusCm=Cm(G.FieldDischargeRadiusCm);
    Terms.BlizzardSlowBp=G.BlizzardSlowBp;
    Terms.GustMoveCm=Cm(G.GustMoveCm);
    Terms.MaxPerOwner=Small(G.MaxPerOwner);
    Terms.MaxTotal=Small(G.MaxTotal);
    return Terms;
}

bool FLDGroundTerms::IsUsable() const
{
    for (int32 Index=1; Index<LDGroundStateKindCount; ++Index)
    {
        const FLDGroundKindTerms& Kind=Kinds[Index];
        if (Kind.RadiusCm<=0 || Kind.LifeMs<=0 || Kind.TickMs<=0 || Kind.TickMs>Kind.LifeMs) { return false; }
    }
    return IceAccelScaleBp>0 && IceAccelScaleBp<10000 && IceDisplacementScaleBp>10000 && FireBurnStacks>0 && FieldRatioBp>0
        && FieldDischargeRadiusCm>0 && BlizzardSlowBp>0 && BlizzardSlowBp<10000 && GustMoveCm>0 && MaxPerOwner>0
        && MaxTotal>=MaxPerOwner;
}

namespace LDCombatElements
{
    ELDCombatReaction ReactionOf(ELDCombatElement Held, ELDCombatElement Incoming)
    {
        using E=ELDCombatElement;
        using R=ELDCombatReaction;
        if (Held==E::None || Incoming==E::None || Held==Incoming || Held==E::Gust) { return R::None; }
        const uint8 A=FMath::Min(uint8(Held),uint8(Incoming)), B=FMath::Max(uint8(Held),uint8(Incoming));
        // Rows by the lower element (fire 1, water 2, shock 3, chill 4, rot 5), columns by the higher (.. gust 6).
        static const R Table[LDCombatElementCount][LDCombatElementCount]=
        {
            {R::None,R::None,R::None,R::None,R::None,R::None,R::None},
            {R::None,R::None,R::Fog,R::Overload,R::Melt,R::Methane,R::Firestorm},
            {R::None,R::None,R::None,R::Conduct,R::Freeze,R::Ferment,R::Splash},
            {R::None,R::None,R::None,R::None,R::Shatter,R::Corrode,R::Thunderstorm},
            {R::None,R::None,R::None,R::None,R::None,R::Frostbite,R::Blizzard},
            {R::None,R::None,R::None,R::None,R::None,R::None,R::Smoke},
            {R::None,R::None,R::None,R::None,R::None,R::None,R::None},
        };
        return A<LDCombatElementCount && B<LDCombatElementCount ? Table[A][B] : R::None;
    }

    FLDReactionRule RuleOf(ELDCombatReaction Reaction, const FLDElementTerms& Terms)
    {
        FLDReactionRule Rule;
        switch (Reaction)
        {
        case ELDCombatReaction::Fog: Rule.PrimaryRatioBp=10000; break;
        case ELDCombatReaction::Overload:
            Rule.PrimaryRatioBp=Terms.OverloadRatioBp;
            Rule.SplashRadiusCm=Terms.OverloadRadiusCm;
            Rule.SplashRatioBp=10000;
            break;
        case ELDCombatReaction::Melt:
            Rule.PrimaryRatioBp=Terms.MeltRatioBp;
            Rule.Ground=ELDGroundStateKind::Puddle;
            break;
        case ELDCombatReaction::Methane:
            Rule.PrimaryRatioBp=Terms.MethaneRatioBp;
            Rule.SplashRadiusCm=Terms.MethaneRadiusCm;
            Rule.SplashRatioBp=10000;
            break;
        case ELDCombatReaction::Firestorm:
            Rule.bKeepsHeldFire=true;
            Rule.Ground=ELDGroundStateKind::Fire;
            break;
        case ELDCombatReaction::Conduct:
            Rule.PrimaryRatioBp=Terms.ConductRatioBp;
            Rule.SplashRadiusCm=Terms.ConductRadiusCm;
            Rule.SplashRatioBp=10000;
            Rule.bSplashWetOnly=true;
            break;
        case ELDCombatReaction::Splash: Rule.Ground=ELDGroundStateKind::Puddle; break;
        case ELDCombatReaction::Thunderstorm:
            Rule.PrimaryRatioBp=10000;
            Rule.bReappliesShock=true;
            Rule.Ground=ELDGroundStateKind::Field;
            break;
        case ELDCombatReaction::Blizzard:
            Rule.PrimaryRatioBp=Terms.BlizzardRatioBp;
            Rule.Ground=ELDGroundStateKind::Blizzard;
            break;
        case ELDCombatReaction::Smoke: Rule.Ground=ELDGroundStateKind::Miasma; break;
        default: break;
        }
        return Rule;
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

    int64 ScaledP2(int64 ValueP2, int64 RatioBp) { return LDCombatElementsDetail::Share(ValueP2,RatioBp); }

    bool MarkActive(const FLDCombatElementMark& Mark, int64 AtMs)
    {
        return Mark.Kind!=ELDCombatElement::None && Mark.ExpiresAtMs>AtMs;
    }

    ELDCombatElement ElementOf(bool bBurning, const FLDCombatElementMark& Mark, int64 AtMs)
    {
        if (bBurning) { return ELDCombatElement::Fire; }
        return MarkActive(Mark,AtMs) ? Mark.Kind : ELDCombatElement::None;
    }

    bool CountShockHit(FLDCombatElementMark& Mark, int32 EveryNth, bool bOvercharged, int64 AtMs)
    {
        if (Mark.Kind!=ELDCombatElement::Shock || !MarkActive(Mark,AtMs) || EveryNth<=0) { return false; }
        if (bOvercharged) { return true; }
        Mark.ShockHits=FMath::Min(Mark.ShockHits+1,EveryNth);
        if (Mark.ShockHits<EveryNth) { return false; }
        Mark.ShockHits=0;
        return true;
    }

    int64 RotTickP2(int64 SourceAPP2, const FLDElementTerms& Terms)
    {
        using namespace LDCombatElementsDetail;
        const int64 PerSec=FMath::Max<int64>(Terms.RotPerSecP2,0);
        const int64 FromAP=Share(FMath::Max<int64>(SourceAPP2,0),Terms.RotAPRatioPerSecBp);
        if (FromAP>MAX_int64-PerSec || Terms.RotTickMs<=0) { return 0; }
        const int64 Sum=PerSec+FromAP;
        if (Sum>MAX_int64/Terms.RotTickMs) { return MAX_int64/1000; }
        return Sum*Terms.RotTickMs/1000;
    }

    int64 AfterRotP2(int64 AmountP2, const FLDCombatElementMark& Mark, int64 AtMs)
    {
        if (AmountP2<=0) { return 0; }
        if (Mark.Kind!=ELDCombatElement::Rot || !MarkActive(Mark,AtMs) || Mark.HealCutBp<=0) { return AmountP2; }
        return AmountP2-LDCombatElementsDetail::Share(AmountP2,FMath::Min<int64>(Mark.HealCutBp,10000));
    }

    FGroundConversion GroundConversion(ELDGroundStateKind Kind, ELDCombatElement Incoming)
    {
        using E=ELDCombatElement;
        using G=ELDGroundStateKind;
        FGroundConversion Out;
        switch (Kind)
        {
        case G::Puddle:
            if (Incoming==E::Shock) { Out.Becomes=G::Field; }
            else if (Incoming==E::Chill) { Out.Becomes=G::Ice; }
            else if (Incoming==E::Fire) { Out.bFog=true; Out.bVanishes=true; }
            break;
        case G::Ice:
            if (Incoming==E::Fire) { Out.Becomes=G::Puddle; }
            break;
        case G::Fire:
            if (Incoming==E::Gust) { Out.bMove=true; }
            else if (Incoming==E::Water) { Out.bFog=true; Out.bVanishes=true; }
            break;
        case G::Miasma:
            if (Incoming==E::Fire) { Out.bBurst=true; Out.bVanishes=true; }
            else if (Incoming==E::Gust) { Out.bMove=true; }
            break;
        case G::Field:
            if (Incoming==E::Gust) { Out.bBurst=true; Out.bVanishes=true; }
            break;
        case G::Blizzard:
            if (Incoming==E::Fire) { Out.bVanishes=true; }
            break;
        default: break;
        }
        return Out;
    }

    ELDCombatElement GroundElement(ELDGroundStateKind Kind)
    {
        switch (Kind)
        {
        case ELDGroundStateKind::Puddle: return ELDCombatElement::Water;
        case ELDGroundStateKind::Fire: return ELDCombatElement::Fire;
        case ELDGroundStateKind::Miasma: return ELDCombatElement::Rot;
        case ELDGroundStateKind::Field: return ELDCombatElement::Shock;
        case ELDGroundStateKind::Blizzard: return ELDCombatElement::Chill;
        default: return ELDCombatElement::None;
        }
    }

    ELDGroundStateKind GroundForElement(ELDCombatElement Element)
    {
        switch (Element)
        {
        case ELDCombatElement::Fire: return ELDGroundStateKind::Fire;
        case ELDCombatElement::Water: return ELDGroundStateKind::Puddle;
        case ELDCombatElement::Shock: return ELDGroundStateKind::Field;
        case ELDCombatElement::Chill: return ELDGroundStateKind::Ice;
        case ELDCombatElement::Rot: return ELDGroundStateKind::Miasma;
        default: return ELDGroundStateKind::None;
        }
    }

    bool GroundContains(const FLDGroundState& Ground, const FVector& Foot)
    {
        const FVector2D Offset(Foot.X-Ground.CentreCm.X,Foot.Y-Ground.CentreCm.Y);
        if (Ground.AngleDegrees>0)
        {
            if (Offset.SizeSquared()>Ground.RadiusCm*Ground.RadiusCm) { return false; }
            if (Offset.IsNearlyZero()) { return true; }
            const double Cos=FVector2D::DotProduct(Offset.GetSafeNormal(),Ground.Heading.GetSafeNormal());
            return Cos>=FMath::Cos(FMath::DegreesToRadians(Ground.AngleDegrees*.5))-1e-9;
        }
        if (Ground.HalfLengthCm<=0) { return Offset.SizeSquared()<=Ground.RadiusCm*Ground.RadiusCm; }
        const FVector2D Along=Ground.Heading.GetSafeNormal();
        const double T=FMath::Clamp(FVector2D::DotProduct(Offset,Along),-Ground.HalfLengthCm,Ground.HalfLengthCm);
        return (Offset-Along*T).SizeSquared()<=Ground.RadiusCm*Ground.RadiusCm;
    }

    void GroundEvictions(const TArray<FLDGroundState>& Live, const FGuid& Owner, int32 MaxPerOwner, int32 MaxTotal,
        TArray<int32>& OutDescending)
    {
        OutDescending.Reset();
        TArray<int32> Order;
        for (int32 Index=0; Index<Live.Num(); ++Index) { Order.Add(Index); }
        Order.Sort([&Live](int32 Left, int32 Right) { return Live[Left].CreatedSerial<Live[Right].CreatedSerial; });
        int32 OwnerCount=0;
        for (int32 Index : Order) { if (Live[Index].Owner.SourceEntity==Owner) { ++OwnerCount; } }
        int32 Total=Live.Num();
        for (int32 Index : Order)
        {
            const bool bOwners=Live[Index].Owner.SourceEntity==Owner;
            if ((bOwners && OwnerCount>=MaxPerOwner) || Total>=MaxTotal)
            {
                OutDescending.Add(Index);
                --Total;
                if (bOwners) { --OwnerCount; }
            }
        }
        OutDescending.Sort([](int32 Left, int32 Right) { return Left>Right; });
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
        case ELDCombatReaction::Melt: return TEXT("melt");
        case ELDCombatReaction::Methane: return TEXT("methane");
        case ELDCombatReaction::Firestorm: return TEXT("firestorm");
        case ELDCombatReaction::Freeze: return TEXT("freeze");
        case ELDCombatReaction::Ferment: return TEXT("ferment");
        case ELDCombatReaction::Splash: return TEXT("splash");
        case ELDCombatReaction::Shatter: return TEXT("shatter");
        case ELDCombatReaction::Corrode: return TEXT("corrode");
        case ELDCombatReaction::Thunderstorm: return TEXT("thunderstorm");
        case ELDCombatReaction::Frostbite: return TEXT("frostbite");
        case ELDCombatReaction::Blizzard: return TEXT("blizzard");
        case ELDCombatReaction::Smoke: return TEXT("smoke");
        default: return TEXT("none");
        }
    }

    const TCHAR* ElementName(ELDCombatElement Element)
    {
        switch (Element)
        {
        case ELDCombatElement::Fire: return TEXT("fire");
        case ELDCombatElement::Water: return TEXT("water");
        case ELDCombatElement::Shock: return TEXT("shock");
        case ELDCombatElement::Chill: return TEXT("chill");
        case ELDCombatElement::Rot: return TEXT("rot");
        case ELDCombatElement::Gust: return TEXT("gust");
        default: return TEXT("none");
        }
    }

    ELDCombatElement FromDemo(ELDDemoElement Element)
    {
        switch (Element)
        {
        case ELDDemoElement::Fire: return ELDCombatElement::Fire;
        case ELDDemoElement::Water: return ELDCombatElement::Water;
        case ELDDemoElement::Electric: return ELDCombatElement::Shock;
        case ELDDemoElement::Chill: return ELDCombatElement::Chill;
        case ELDDemoElement::Rot: return ELDCombatElement::Rot;
        case ELDDemoElement::Gust: return ELDCombatElement::Gust;
        default: return ELDCombatElement::None;
        }
    }

    const TCHAR* GroundName(ELDGroundStateKind Kind)
    {
        switch (Kind)
        {
        case ELDGroundStateKind::Puddle: return TEXT("puddle");
        case ELDGroundStateKind::Ice: return TEXT("ice");
        case ELDGroundStateKind::Fire: return TEXT("fire");
        case ELDGroundStateKind::Miasma: return TEXT("miasma");
        case ELDGroundStateKind::Field: return TEXT("field");
        case ELDGroundStateKind::Blizzard: return TEXT("blizzard");
        default: return TEXT("none");
        }
    }
}
