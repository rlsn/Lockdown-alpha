#include "Combat/LDCombatDirector.h"
#include "Algo/Compare.h"
#include "Algo/Find.h"
#include "Core/LDLog.h"
#include "Combat/LDCombatControlRules.h"
#include "Combat/LDCombatElements.h"
#include "Combat/LDCombatKeywords.h"
#include "Combat/LDCombatSourceRules.h"
#include "Combat/LDControlRules.h"
#include "Combat/LDMomentum.h"
#include "Combat/LDTacticalRules.h"
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDSkillGrowth.h"
#include "Framework/LDLifeAuthority.h"

// Build cards on Combat's records (issue #287 spec §6-§7; docs/systems/COMBAT_AND_GROWTH.md, "构筑牌"). A card copy names the
// exit it sits on (the primary basic, a learned skill, the innate D) or the body; FLDPassiveEffectRef::Targets matches that
// exit against a hit, a control or a release. Copies of one card add their numbers into one damage factor (its Key) and each
// copy's trigger fires once.
namespace LDCombatCardsDetail
{
    // floor(Value x Bp / 10000) for nonnegative inputs, without overflow for any P2 or ms value.
    int64 Share(int64 Value, int64 Bp)
    {
        if (Value<=0 || Bp<=0) { return 0; }
        return Value/10000*Bp+Value%10000*Bp/10000;
    }

    double Cm(const FLDDemoRational& Value) { return Value.Denominator>0 ? double(Value.Numerator)/double(Value.Denominator) : 0.; }

    FLDDemoRational Reduce(int64 Numerator, int64 Denominator)
    {
        int64 A=FMath::Abs(Numerator), B=FMath::Abs(Denominator);
        while (B!=0) { const int64 T=A%B; A=B; B=T; }
        return A>1 ? FLDDemoRational{Numerator/A,Denominator/A} : FLDDemoRational{Numerator,Denominator};
    }

    FLDDemoRational ScaleRational(const FLDDemoRational& Value, int64 Bp) { return Reduce(Value.Numerator*Bp,Value.Denominator*10000); }

    // One skill-class hit of a body's card with the body's current AD/AP as its source values.
    FLDCombatHit MakeHit(const FLDBodyCombatState& Source, const FLDCombatSourceRef& Ref, const FLDCombatBodyRef& Target,
        const FGuid& EffectId)
    {
        FLDCombatHit Hit;
        Hit.HitId=FGuid::NewGuid();
        Hit.EffectId=EffectId.IsValid() ? EffectId : FGuid::NewGuid();
        Hit.Target=Target;
        Hit.SourceBody={Source.Victim,Source.Identity};
        Hit.SourceEntity=Source.Victim.EntityId;
        Hit.CreditOwner=Source.Victim.Kind==ELDVictimKind::Contender ? Source.Victim.EntityId : FGuid();
        Hit.Source=Ref;
        Hit.HitClass=ELDCombatHitClass::Ability;
        Hit.SourceADP2=Source.AttributeProjection.Final.ADP2;
        Hit.SourceAPP2=Source.AttributeProjection.Final.APP2;
        return Hit;
    }

    ELDCombatElement CombatElement(ELDDemoElement Element) { return LDCombatElements::FromDemo(Element); }

    // The mod operations a skill branch variant carries itself (DemoContent.BRANCH_MOD_OPS), in their compile order.
    constexpr ELDDemoEffectOp BranchModOps[]={ELDDemoEffectOp::FinalKnockback,ELDDemoEffectOp::Pull,ELDDemoEffectOp::ProjectileReturn,
        ELDDemoEffectOp::ProjectilePierce,ELDDemoEffectOp::EchoImpact,ELDDemoEffectOp::ZoneTether,ELDDemoEffectOp::EmpowerReach};

    // Copies of one card add (their deltas from x1 sum); different cards multiply (spec §6.1, the rule of §3.7). A sum below
    // -1 leaves nothing, so the multiplier never goes negative.
    int64 CardMultiplierBp(const TArray<FLDPassiveEffectRef>& Refs, int64 FLDDemoEffectDefinition::* Field)
    {
        TMap<FName,int64> Deltas;
        for (const FLDPassiveEffectRef& Ref : Refs) { Deltas.FindOrAdd(Ref.Key)+=Ref.Effect.*Field-10000; }
        int64 Product=10000;
        for (const TPair<FName,int64>& Pair : Deltas) { Product=Product*FMath::Max<int64>(10000+Pair.Value,0)/10000; }
        return Product;
    }
}

// ---------------------------------------------------------------- resolution

bool ALDCombatDirector::CollectPassiveEffects(const FLDAttributeProjection& Projection, ELDDemoEffectOp Op,
    TArray<FLDPassiveEffectRef>& Out, FString& Error) const
{
    return LDCombatSourceRules::CollectCardEffects(Projection,Op,Catalog(),Out,Error);
}

bool LDCombatSourceRules::CardHasteBp(const FLDAttributeProjection& Build, int64 HpP2, int64 MaxHpP2, int32 FocusHits,
    const FLDDemoContentSnapshot* Content, int64& Out, FString& Error)
{
    Out=0;
    Error.Reset();
    if (Build.MissingHPAttackSpeedBp>0 && MaxHpP2>0 && HpP2<MaxHpP2)
    {
        const int64 MissingPercent=(MaxHpP2-FMath::Max<int64>(HpP2,0))*100/MaxHpP2;
        Out+=MissingPercent*Build.MissingHPAttackSpeedBp;
    }
    if (FocusHits>0)
    {
        TArray<FLDPassiveEffectRef> Focus;
        if (!CollectCardEffects(Build,ELDDemoEffectOp::FocusAttackSpeed,Content,Focus,Error)) { return false; }
        for (const FLDPassiveEffectRef& Ref : Focus) { Out+=int64(FocusHits)*Ref.Effect.RatioBp; }
    }
    return true;
}

bool LDCombatSourceRules::CollectCardEffects(const FLDAttributeProjection& Projection, ELDDemoEffectOp Op,
    const FLDDemoContentSnapshot* Content, TArray<FLDPassiveEffectRef>& Out, FString& Error)
{
    Out.Reset();
    Error.Reset();
    if (Projection.InstalledCards.IsEmpty()) { return true; }
    if (!Content) { Error=TEXT("Card effects require the approved Demo content."); return false; }
    const FLDDemoContentParameters& Parameters=Content->Get().Parameters;
    for (const FLDInstalledCardTerm& Term : Projection.InstalledCards)
    {
        const FLDDemoCardDefinition* Card=nullptr;
        FString LookupError;
        if (!Content->FindCardByName(Term.CardId,Card,LookupError) || !Card)
        {
            Out.Reset();
            Error=FString::Printf(TEXT("Installed card %s is not in the approved catalog: %s"),*Term.CardId.ToString(),*LookupError);
            return false;
        }
        // The same growth the projection validated: fixed amounts grow with the holder's level, ratios and times do not.
        const int64 Factor=LDSkillGrowth::FactorBp(Parameters.SkillGrowthPerLevelBp,Term.Level);
        for (const FLDDemoEffectDefinition& Authored : Card->Effects)
        {
            if (Authored.GetOp()!=Op) { continue; }
            FLDPassiveEffectRef& Ref=Out.AddDefaulted_GetRef();
            Ref.Key=Term.CardId;
            Ref.Serial=Term.Serial;
            Ref.Source.Kind=ELDCombatSourceKind::Card;
            Ref.Source.DefinitionId=Term.CardId;
            Ref.TargetKind=Term.TargetKind;
            Ref.Active=Term.Active;
            Ref.Element=Term.Element;
            Ref.Tier=Term.Tier;
            if (Factor<=0 || !LDSkillGrowth::GrowEffect(Authored,Factor,Ref.Effect))
            {
                Out.Reset();
                Error=FString::Printf(TEXT("Installed card %s does not grow for level %u."),*Term.CardId.ToString(),uint32(Term.Level));
                return false;
            }
        }
    }
    return true;
}

bool ALDCombatDirector::CardsOn(const FLDAttributeProjection& Projection, ELDDemoEffectOp Op, const FLDCombatSourceRef& Exit,
    TArray<FLDPassiveEffectRef>& Out, FString& Error) const
{
    if (!CollectPassiveEffects(Projection,Op,Out,Error)) { return false; }
    Out.RemoveAll([&Exit](const FLDPassiveEffectRef& Ref) { return !Ref.Targets(Exit); });
    return true;
}

bool ALDCombatDirector::SumCards(const FLDAttributeProjection& Projection, ELDDemoEffectOp Op, int64 FLDDemoEffectDefinition::* Field,
    int64& Out, FString& Error) const
{
    Out=0;
    TArray<FLDPassiveEffectRef> Held;
    if (!CollectPassiveEffects(Projection,Op,Held,Error)) { return false; }
    for (const FLDPassiveEffectRef& Ref : Held) { Out+=Ref.Effect.*Field; }
    return true;
}

bool ALDCombatDirector::ChainAllows(const FLDCombatHit& Hit, const TCHAR* What) const
{
    const FLDDemoContentSnapshot* Content=Catalog();
    const int32 MaxDepth=Content ? Content->Get().Parameters.ChainMaxDepth : 0;
    if (int32(Hit.ChainDepth)+1<=MaxDepth) { return true; }
    UE_LOG(LogLDCombat,Warning,TEXT("LD_CHAIN_GUARD reason=Depth depth=%u what=%s src=%s effect=%s"),uint32(Hit.ChainDepth),What,
        *LDLog::Id(Hit.SourceEntity),*LDLog::Source(Hit.Source));
    return false;
}

// ---------------------------------------------------------------- release terms

bool ALDCombatDirector::CompileActive(const FLDAttributeProjection& Projection, const FLDCombatSourceRef& Source,
    FLDCombatActiveTerms& Terms, FString& Error) const
{
    return LDCombatSourceRules::CompileCardActive(Projection,Source,Catalog(),Terms,Error);
}

bool LDCombatSourceRules::CompileCardActive(const FLDAttributeProjection& Projection, const FLDCombatSourceRef& Source,
    const FLDDemoContentSnapshot* Content, FLDCombatActiveTerms& Terms, FString& Error)
{
    using namespace LDCombatCardsDetail;
    if (Source.Kind!=ELDCombatSourceKind::Learned && Source.Kind!=ELDCombatSourceKind::Innate) { return true; }
    FLDDemoAbilityValues& Values=Terms.Values;
    const auto Has=[&Values](ELDDemoEffectOp Op)
    { return Values.Effects.ContainsByPredicate([Op](const FLDDemoEffectDefinition& Effect) { return Effect.GetOp()==Op; }); };
    // Skill branches (issue #287 spec §5.1): a variant's own pull, return, pierce, echo, tether, finisher or reach leaves the
    // value set and runs through its shared handler.
    for (const ELDDemoEffectOp Op : BranchModOps)
    {
        const int32 Index=Values.Effects.IndexOfByPredicate([Op](const FLDDemoEffectDefinition& Effect) { return Effect.GetOp()==Op; });
        if (Index==INDEX_NONE) { continue; }
        FLDPassiveEffectRef Branch;
        Branch.Source.Kind=Source.Kind;
        Branch.Source.DefinitionId=Source.DefinitionId;
        Branch.Effect=Values.Effects[Index];
        Values.Effects.RemoveAt(Index);
        const FLDDemoEffectDefinition& Effect=Branch.Effect;
        switch (Op)
        {
        case ELDDemoEffectOp::FinalKnockback:
            // S17's last package or S22's final wave pushes; S17 pays with its attack-speed bonus.
            if (Has(ELDDemoEffectOp::EmpowerBasic) || Has(ELDDemoEffectOp::ChannelCone))
            {
                Terms.FinalKnockback=Branch;
                Terms.FinalKnockbackTerms=Effect;
                for (FLDDemoEffectDefinition& Package : Values.Effects)
                {
                    if (Package.GetOp()!=ELDDemoEffectOp::EmpowerBasic) { continue; }
                    for (FLDDemoStatBonus& Stat : Package.Stats)
                    {
                        if (Stat.Stat==ELDDemoBonusStat::AttackSpeedBonusRatio) { Stat.Amount=Share(Stat.Amount,Effect.CostRatioBp); }
                    }
                }
            }
            break;
        case ELDDemoEffectOp::Pull:
            if (Has(ELDDemoEffectOp::Knockback) || Has(ELDDemoEffectOp::Taunt)) { Terms.Pull=Branch; Terms.PullTerms=Effect; }
            break;
        case ELDDemoEffectOp::ProjectileReturn:
            if (Terms.Target.Mode==ELDDemoTargetMode::DirectionProjectile && Has(ELDDemoEffectOp::Damage)) { Terms.ProjectileReturn=Branch; }
            break;
        case ELDDemoEffectOp::ProjectilePierce:
            if (Terms.Target.Mode==ELDDemoTargetMode::DirectionProjectile && Has(ELDDemoEffectOp::Damage)) { Terms.ProjectilePierce=Branch; }
            break;
        case ELDDemoEffectOp::EchoImpact:
            if (Has(ELDDemoEffectOp::Damage) && (Has(ELDDemoEffectOp::SelfArea) || Has(ELDDemoEffectOp::GroundImpact)
                || Has(ELDDemoEffectOp::ContactTrap)))
            { Terms.EchoImpact=Branch; }
            break;
        case ELDDemoEffectOp::ZoneTether:
            for (FLDDemoEffectDefinition& Zone : Values.Effects)
            {
                if (Zone.GetOp()!=ELDDemoEffectOp::Zone) { continue; }
                Zone.RadiusCm=ScaleRational(Zone.RadiusCm,Effect.RatioBp);
                Terms.ZoneTether=Branch;
            }
            break;
        case ELDDemoEffectOp::EmpowerReach:
            if (Has(ELDDemoEffectOp::EmpowerBasic)) { Terms.EmpowerReach=Branch; }
            break;
        default:
            break;
        }
    }
    // K08/K09 on a skill: copies add their deltas, different cards multiply; the cost rounds up to the next 0.01 momentum.
    TArray<FLDPassiveEffectRef> Costs, Repeats, HPCosts, Splits;
    if (!CollectCardEffects(Projection,ELDDemoEffectOp::CooldownCost,Content,Costs,Error)
        || !CollectCardEffects(Projection,ELDDemoEffectOp::AutoRecast,Content,Repeats,Error)
        || !CollectCardEffects(Projection,ELDDemoEffectOp::HpCost,Content,HPCosts,Error)
        || !CollectCardEffects(Projection,ELDDemoEffectOp::DashSplit,Content,Splits,Error))
    { return false; }
    for (TArray<FLDPassiveEffectRef>* Refs : {&Costs,&Repeats,&HPCosts,&Splits})
    { Refs->RemoveAll([&Source](const FLDPassiveEffectRef& Ref) { return !Ref.Targets(Source); }); }
    if (!Costs.IsEmpty())
    {
        const int64 CooldownBp=CardMultiplierBp(Costs,&FLDDemoEffectDefinition::SecondaryRatioBp);
        const int64 CostBp=CardMultiplierBp(Costs,&FLDDemoEffectDefinition::CostRatioBp);
        if (Source.Kind==ELDCombatSourceKind::Innate) { Terms.CooldownScaleBp=Share(Terms.CooldownScaleBp,CooldownBp); }
        else { Values.CooldownMs=Share(Values.CooldownMs,CooldownBp); }
        const int64 Cost=Values.MomentumCostP2;
        Values.MomentumCostP2=Cost/10000*CostBp+(Cost%10000*CostBp+9999)/10000;
    }
    // K12: health instead of momentum (copies add their health per momentum).
    for (const FLDPassiveEffectRef& Ref : HPCosts) { Terms.HPCostBpPerMomentum+=Ref.Effect.RatioBp; }
    // K06: one repeat per copy, each at the card's share and after its delay.
    for (const FLDPassiveEffectRef& Ref : Repeats)
    {
        ++Terms.AutoRecasts;
        Terms.AutoRecastDelayMs=FMath::Max(Terms.AutoRecastDelayMs,Ref.Effect.DelayMs);
        Terms.AutoRecastScaleBp=FMath::Max(Terms.AutoRecastScaleBp,Ref.Effect.MultiplierBp);
    }
    // D05: the innate dash splits into two segments (copies add a segment each).
    if (Source.Kind==ELDCombatSourceKind::Innate && !Splits.IsEmpty())
    {
        Terms.SplitSegments=1+Splits.Num();
        Terms.SplitDistanceCm=Splits[0].Effect.DistanceCm;
        Terms.SplitWindowMs=Splits[0].Effect.DurationMs;
    }
    // D08: the D dash pays a flat momentum cost (copies add).
    if (Source.Kind==ELDCombatSourceKind::Innate)
    {
        for (const FLDPassiveEffectRef& Ref : Costs) { Terms.DashMomentumCostP2+=Ref.Effect.LimitP2; }
    }
    // Issue #292 P5: K07 grows the host's areas, flight radius and line width; K10 its cast range; K11 its control times (still
    // diminished); D07 lengthens the D dash. Copies add their deltas into one multiplier each.
    TArray<FLDPassiveEffectRef> Reshapes, Rams;
    if (!CollectCardEffects(Projection,ELDDemoEffectOp::Reshape,Content,Reshapes,Error)
        || !CollectCardEffects(Projection,ELDDemoEffectOp::DashRam,Content,Rams,Error))
    { return false; }
    Reshapes.RemoveAll([&Source](const FLDPassiveEffectRef& Ref) { return !Ref.Targets(Source); });
    Rams.RemoveAll([&Source](const FLDPassiveEffectRef& Ref) { return !Ref.Targets(Source); });
    int64 AreaDeltaBp=0, RangeBp=0, ControlBp=0;
    for (const FLDPassiveEffectRef& Ref : Reshapes)
    {
        AreaDeltaBp+=Ref.Effect.MultiplierBp>0 ? Ref.Effect.MultiplierBp-10000 : 0;
        RangeBp+=Ref.Effect.RatioBp;
        ControlBp+=Ref.Effect.CostRatioBp;
    }
    if (AreaDeltaBp>0)
    {
        const int64 AreaBp=10000+AreaDeltaBp;
        Terms.AreaBp=AreaBp;
        FLDDemoTargetDefinition& Target=Terms.Target;
        if (Target.ProjectileRadiusCm.Numerator>0) { Target.ProjectileRadiusCm=ScaleRational(Target.ProjectileRadiusCm,AreaBp); }
        if (Target.LineHalfWidthCm.Numerator>0) { Target.LineHalfWidthCm=ScaleRational(Target.LineHalfWidthCm,AreaBp); }
        // A cone's area is its reach.
        if (Target.Mode==ELDDemoTargetMode::DirectionCone) { Target.RangeCm=ScaleRational(Target.RangeCm,AreaBp); }
        for (FLDDemoEffectDefinition& Effect : Values.Effects)
        {
            const ELDDemoEffectOp Op=Effect.GetOp();
            const bool bArea=Op==ELDDemoEffectOp::SelfArea || Op==ELDDemoEffectOp::Zone || Op==ELDDemoEffectOp::GroundImpact
                || Op==ELDDemoEffectOp::ContactTrap || Op==ELDDemoEffectOp::GroundState;
            if (!bArea) { continue; }
            if (Effect.RadiusCm.Numerator>0) { Effect.RadiusCm=ScaleRational(Effect.RadiusCm,AreaBp); }
            // An impact's inner ring and a trap's trigger ring grow with it.
            if ((Op==ELDDemoEffectOp::GroundImpact || Op==ELDDemoEffectOp::ContactTrap) && Effect.DistanceCm.Numerator>0)
            { Effect.DistanceCm=ScaleRational(Effect.DistanceCm,AreaBp); }
        }
    }
    if (RangeBp>0 && (Terms.Target.Mode==ELDDemoTargetMode::Unit || Terms.Target.Mode==ELDDemoTargetMode::DirectionProjectile))
    {
        Terms.Target.RangeCm=ScaleRational(Terms.Target.RangeCm,10000+RangeBp);
    }
    if (ControlBp>0)
    {
        for (FLDDemoEffectDefinition& Effect : Values.Effects)
        {
            if (Effect.IsControl()) { Effect.DurationMs=Share(Effect.DurationMs,10000+ControlBp); }
            // K11 also lengthens a control another delivery carries.
            else if (int64* Composed=Effect.ComposedControlMs()) { *Composed=Share(*Composed,10000+ControlBp); }
        }
    }
    if (Source.Kind==ELDCombatSourceKind::Innate && !Rams.IsEmpty())
    {
        int64 RamBp=0;
        for (const FLDPassiveEffectRef& Ref : Rams) { RamBp+=Ref.Effect.RatioBp; }
        Terms.DashScaleBp=Share(Terms.DashScaleBp,10000+RamBp);
    }
    if (Values.CooldownMs<0 || Terms.PackageScaleBp<=0 || Terms.DashScaleBp<=0 || Terms.CooldownScaleBp<0)
    {
        Error=FString::Printf(TEXT("The cards on %s compile to a negative cooldown or a non-positive multiplier."),*LDLog::Source(Source));
        return false;
    }
    return true;
}

void LDCombatSourceRules::ScaleInnateDash(FLDInnateDashTerms& Dash, const FLDCombatActiveTerms& Terms)
{
    Dash.DistanceCm.Numerator*=Terms.DashScaleBp;
    Dash.DistanceCm.Denominator*=10000;
    Dash.CooldownMs=Dash.CooldownMs/10000*Terms.CooldownScaleBp+Dash.CooldownMs%10000*Terms.CooldownScaleBp/10000;
    // D05: each segment runs the card's distance.
    if (Terms.SplitSegments>1 && Terms.SplitDistanceCm.Denominator>0) { Dash.DistanceCm=Terms.SplitDistanceCm; }
}

bool ALDCombatDirector::ReleaseFactors(const FLDBodyCombatState& Body, const FLDCombatEntityRecord& Record,
    const FLDCombatSourceRef& Source, const FLDCombatTrack* Track, int64 NowMs, TArray<FLDDamageFactor>& Out, FString& Error) const
{
    Out.Reset();
    if (Source.Kind!=ELDCombatSourceKind::Learned) { return true; }
    TArray<FLDPassiveEffectRef> Flat, Idle, Growth;
    if (!CardsOn(Body.AttributeProjection,ELDDemoEffectOp::SkillDamageFactor,Source,Flat,Error)
        || !CardsOn(Body.AttributeProjection,ELDDemoEffectOp::IdleReadyAmp,Source,Idle,Error)
        || !CardsOn(Body.AttributeProjection,ELDDemoEffectOp::SkillGrowth,Source,Growth,Error))
    { return false; }
    // K09/K12: a fixed factor of the skill.
    for (const FLDPassiveEffectRef& Ref : Flat) { Out.Add({Ref.Key,Ref.Effect.RatioBp}); }
    // K16: every full interval the skill stood ready and idle (from its real ReadyAt), without a cap.
    for (const FLDPassiveEffectRef& Ref : Idle)
    {
        if (!Track || Track->ReadyAtMs>NowMs || Ref.Effect.IntervalMs<=0) { continue; }
        const int64 From=FMath::Max3<int64>(Track->ReadyAtMs,0,IdleCountFloorMs(Body,NowMs));
        const int64 Steps=(NowMs-From)/Ref.Effect.IntervalMs;
        if (Steps>0) { Out.Add({Ref.Key,Steps*Ref.Effect.RatioBp}); }
    }
    // K17: every contender hit of the host this match.
    for (const FLDPassiveEffectRef& Ref : Growth)
    {
        const int32* Hits=Record.CardGrowth.SkillHits.Find(Ref.Serial);
        if (Hits && *Hits>0) { Out.Add({Ref.Key,int64(*Hits)*Ref.Effect.RatioBp}); }
    }
    // K18: every release of another skill within the card's window adds its share.
    TArray<FLDPassiveEffectRef> Combos;
    if (!CardsOn(Body.AttributeProjection,ELDDemoEffectOp::RecentCastAmp,Source,Combos,Error)) { return false; }
    for (const FLDPassiveEffectRef& Ref : Combos)
    {
        int64 Others=0;
        for (const TPair<FName,int64>& Cast : Body.RecentCasts)
        {
            Others+=Cast.Key!=Source.DefinitionId && Cast.Value<=NowMs && NowMs-Cast.Value<=Ref.Effect.IntervalMs ? 1 : 0;
        }
        if (Others>0) { Out.Add({Ref.Key,Others*Ref.Effect.RatioBp}); }
    }
    // K05: a control of a host armed the next skill's bonus (ReleaseAbility spends it).
    if (!Body.ControlAmpCard.IsNone() && Body.ControlAmpBp>0 && Body.ControlAmpUntilMs>NowMs)
    {
        Out.Add({Body.ControlAmpCard,Body.ControlAmpBp});
    }
    // card_martial_scholar: basic hit while holding >=40 BonusAD and >=60 AP armed next skill for +skill_bonus.
    if (Body.ScholarSkillUntilMs>NowMs)
    {
        TArray<FLDPassiveEffectRef> Scholars;
        if (!CollectPassiveEffects(Body.AttributeProjection,ELDDemoEffectOp::MartialScholar,Scholars,Error)) { return false; }
        int64 ScholarBp=0;
        for (const FLDPassiveEffectRef& Ref : Scholars) { ScholarBp+=Ref.Effect.RatioBp; }
        if (ScholarBp>0) { Out.Add({Scholars[0].Key,ScholarBp}); }
    }
    return true;
}

bool ALDCombatDirector::HitFactors(FLDBodyCombatState* Source, FLDBodyCombatState& Target, const FLDCombatHit& Hit, int64 NowMs,
    TArray<FLDDamageFactor>& Out, TArray<FLDCombatSetupMark>& OutTaken, FString& Error)
{
    Out=Hit.Factors;
    OutTaken.Reset();
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!Content) { Error=TEXT("Damage factors require the approved Demo content."); return false; }
    const bool bHostile=Hit.SourceEntity!=Target.Victim.EntityId;
    if (!bHostile) { return true; }
    // A release's locked factors ride on every native hit of it.
    if (Hit.CastLedger && IsNativeDirectHit(Hit)) { Out.Append(Hit.CastLedger->Factors); }
    // The strength keystone on a slowed, attack-slowed, pushed or controlled target.
    const bool bControlled=LDTactical::StrongestSlowBp(Target.Slows,NowMs)>0 || LDTactical::HasActiveRoot(Target.Roots,NowMs)
        || LDTactical::StrongestSlowBp(Target.AttackSlows,NowMs)>0 || LDTactical::HasActiveRoot(Target.Knockbacks,NowMs)
        || LDTactical::HasActiveRoot(Target.Stuns,NowMs) || LDTactical::HasActiveRoot(Target.Disarms,NowMs)
        || LDControl::IsTaunted(Target.Taunt,NowMs) || LDTactical::HasActiveRoot(Target.Silences,NowMs)
        || LDTactical::HasActiveRoot(Target.Knockbacks,NowMs);
    if (Source && Source->AttributeProjection.Keystones.bStrength && bControlled)
    { Out.Add({FName(TEXT("keystone_strength")),Content->Get().Parameters.KeystoneStrengthDamageBp}); }
    // Skill branches: the target's vulnerabilities to this hit and the next-basic bonus a primary basic took.
    if (const int64 Vulnerable=BranchVulnerableBp(Target,Hit,NowMs); Vulnerable>0) { Out.Add({FName(TEXT("branch_vulnerable")),Vulnerable}); }
    if (Hit.BranchAmpBp>0) { Out.Add({FName(TEXT("branch_next_basic")),Hit.BranchAmpBp}); }
    if (Target.Victim.Kind==ELDVictimKind::CampMember && Hit.MonsterMultiplierBp>10000)
    {
        Out.Add({FName(TEXT("monster_multiplier")),Hit.MonsterMultiplierBp-10000});
    }
    // Shatter (shock + chill): the next damaging native skill hit on this life within its window takes more, then the
    // shatter is spent. Zero-damage blows (fans, pushes), riders, reflections, settles and ticks neither take nor spend it.
    const bool bDamaging=Hit.BaseP2>0 || Hit.ADRatioBp>0 || Hit.APRatioBp>0 || Hit.AdaptiveRatioBp>0 || Hit.MaxHPRatioBp>0;
    if (Target.ShatterUntilMs>NowMs && Target.ShatterBp>0 && IsNativeDirectHit(Hit) && bDamaging && !Hit.bPeriodic)
    {
        Out.Add({FName(TEXT("reaction_shatter")),Target.ShatterBp});
        Target.ShatterBp=0;
        Target.ShatterUntilMs=0;
    }
    if (!Source) { return true; }
    // Ground states: N1 A1's ice vulnerability and Y23's standing growth.
    GroundFactors(*Source,Target,Out);
    // Issue #292 P5 speed factors: A10 on a primary basic below its attack-speed threshold, Y18 on any damage per step of move
    // speed above the base; K10's far hit of its host.
    {
        TArray<FLDPassiveEffectRef> Speeds;
        if (!CollectPassiveEffects(Source->AttributeProjection,ELDDemoEffectOp::SpeedFactor,Speeds,Error)) { return false; }
        const auto Value=[](const FLDDemoRational& V) { return V.Denominator>0 ? double(V.Numerator)/double(V.Denominator) : 0.; };
        const FLDProjectedAttributes& Final=Source->AttributeProjection.Final;
        const bool bPrimary=Hit.HitClass==ELDCombatHitClass::BasicPrimary && !Hit.bRider;
        TMap<FName,int64> SpeedBp;
        for (const FLDPassiveEffectRef& Ref : Speeds)
        {
            int64 Steps=0;
            if (Ref.Effect.Trigger==ELDDemoEffectTrigger::AttackSpeedBelow)
            {
                const double Step=Value(Ref.Effect.StepRate), Below=Value(Ref.Effect.ExtraDistanceCm)-Value(Final.AttackSpeed);
                if (!bPrimary || Step<=0 || Below<=0) { continue; }
                Steps=int64(FMath::FloorToDouble(Below/Step+1e-9));
            }
            else if (Ref.Effect.Trigger==ELDDemoEffectTrigger::MoveSpeedAbove)
            {
                const double Base=Value(Content->Get().Parameters.MoveSpeedCmPerSec.Base), Step=double(Ref.Effect.SecondaryRatioBp)/10000.;
                if (Base<=0 || Step<=0 || Hit.bBurnTick || Hit.bPeriodic) { continue; }
                const double Above=Value(Final.MoveSpeedCmPerSec)/Base-1.;
                if (Above<=0) { continue; }
                Steps=int64(FMath::FloorToDouble(Above/Step+1e-9));
            }
            if (Steps>0) { SpeedBp.FindOrAdd(Ref.Key)+=Steps*Ref.Effect.RatioBp; }
        }
        for (const TPair<FName,int64>& Pair : SpeedBp) { Out.Add({Pair.Key,Pair.Value}); }
        if (IsNativeDirectHit(Hit))
        {
            TArray<FLDPassiveEffectRef> Reaches;
            if (!CardsOn(Source->AttributeProjection,ELDDemoEffectOp::Reshape,Hit.Source,Reaches,Error)) { return false; }
            const ALDCharacter* SourceActor=ActorOf(*Source);
            const ALDCharacter* TargetActor=ActorOf(Target);
            int64 FarBp=0;
            for (const FLDPassiveEffectRef& Ref : Reaches)
            {
                if (Ref.Effect.SecondaryRatioBp<=0 || !SourceActor || !TargetActor) { continue; }
                if (FVector::Dist2D(FootOf(*SourceActor),FootOf(*TargetActor))>=LDCombatCardsDetail::Cm(Ref.Effect.DistanceCm))
                { FarBp+=Ref.Effect.SecondaryRatioBp; }
            }
            if (FarBp>0) { Out.Add({Reaches[0].Key,FarBp}); }
        }
    }
    // K04: a different skill's native hit takes the holder's marks on this life (and consumes them).
    if (IsNativeDirectHit(Hit) && !Source->SetupMarks.IsEmpty())
    {
        TArray<FLDPassiveEffectRef> Setups;
        if (!CollectPassiveEffects(Source->AttributeProjection,ELDDemoEffectOp::SetupMark,Setups,Error)) { return false; }
        for (const FLDCombatSetupMark& Mark : Source->SetupMarks)
        {
            if (Mark.Target!=Hit.Target || Mark.ExpiresAtMs<=NowMs || Mark.Host==Hit.Source.DefinitionId) { continue; }
            const FLDPassiveEffectRef* Installed=Setups.FindByPredicate([&Mark](const FLDPassiveEffectRef& Ref)
            { return Ref.Serial==Mark.Serial && Ref.Active==Mark.Host; });
            if (!Installed) { continue; }
            Out.Add({Installed->Key,Installed->Effect.RatioBp});
            OutTaken.Add(Mark);
        }
    }
    // Y27: the reactions its holder triggers.
    if (Hit.ReactionKind!=ELDCombatReaction::None)
    {
        TArray<FLDPassiveEffectRef> Louder;
        if (!CollectPassiveEffects(Source->AttributeProjection,ELDDemoEffectOp::ReactionFactor,Louder,Error)) { return false; }
        for (const FLDPassiveEffectRef& Ref : Louder) { Out.Add({Ref.Key,Ref.Effect.RatioBp}); }
    }
    // Y22: every full interval its holder dealt no damage charges the next damage (damage over time neither takes nor resets it).
    if (!Hit.bBurnTick && Source->IdleSinceMs>0)
    {
        TArray<FLDPassiveEffectRef> Charges;
        if (!CollectPassiveEffects(Source->AttributeProjection,ELDDemoEffectOp::IdleCharge,Charges,Error)) { return false; }
        for (const FLDPassiveEffectRef& Ref : Charges)
        {
            const int64 From=FMath::Max<int64>(Source->IdleSinceMs,IdleCountFloorMs(*Source,NowMs));
            const int64 Steps=Ref.Effect.IntervalMs>0 ? (NowMs-From)/Ref.Effect.IntervalMs : 0;
            if (Steps>0) { Out.Add({Ref.Key,Steps*Ref.Effect.RatioBp}); }
        }
    }
    // #345 card_heavy_mallet: excess AS above cap (0.75) converts each 1% into +convert_ratio basic damage.
    if (Hit.HitClass==ELDCombatHitClass::BasicPrimary && !Hit.bRider)
    {
        TArray<FLDPassiveEffectRef> Mallets;
        if (!CollectPassiveEffects(Source->AttributeProjection,ELDDemoEffectOp::HeavyMallet,Mallets,Error)) { return false; }
        if (!Mallets.IsEmpty())
        {
            const FLDAttributeContributions* ASRow=Source->AttributeProjection.Contributions.FindByPredicate(
                [](const FLDAttributeContributions& Item) { return Item.Attribute==ELDProjectedAttribute::AttackSpeed; });
            if (ASRow)
            {
                const auto R=[](const FLDDemoRational& V) { return V.Denominator>0 ? double(V.Numerator)/double(V.Denominator) : 0.; };
                const double Flat=R(ASRow->Base)+R(ASRow->LevelGrowth.Flat)+R(ASRow->PermanentChoices.Flat)+R(ASRow->Cards.Flat)+R(ASRow->Dev.Flat)+R(ASRow->Temporary.Flat);
                const double Ratio=1.+R(ASRow->LevelGrowth.AdditiveRatio)+R(ASRow->PermanentChoices.AdditiveRatio)+R(ASRow->Cards.AdditiveRatio)+R(ASRow->Dev.AdditiveRatio)+R(ASRow->Temporary.AdditiveRatio);
                const double Uncapped=Flat*Ratio;
                const double Cap=LDCombatCardsDetail::Cm(Mallets[0].Effect.ExtraDistanceCm);
                if (Cap>0. && Uncapped>Cap)
                {
                    const int64 ExcessPercent=int64(FMath::FloorToDouble((Uncapped-Cap)/Cap*100.+1e-9));
                    int64 MalletBp=0;
                    for (const FLDPassiveEffectRef& Ref : Mallets) { MalletBp+=ExcessPercent*Ref.Effect.RatioBp; }
                    if (MalletBp>0) { Out.Add({Mallets[0].Key,MalletBp}); }
                }
            }
        }
    }
    // #345 card_let_bullets_fly: flying skills and basic_projectile gain +ratio per distance traveled up to max_ratio.
    if (Hit.FlightDistanceCm>0. && !Hit.bRider)
    {
        TArray<FLDPassiveEffectRef> Flights;
        if (!CollectPassiveEffects(Source->AttributeProjection,ELDDemoEffectOp::ProjectileFlightAmp,Flights,Error)) { return false; }
        int64 FlightBp=0;
        for (const FLDPassiveEffectRef& Ref : Flights)
        {
            const double StepCm=LDCombatCardsDetail::Cm(Ref.Effect.DistanceCm);
            if (StepCm<=0.) { continue; }
            const int64 Steps=int64(FMath::FloorToDouble(Hit.FlightDistanceCm/StepCm+1e-9));
            if (Steps>0) { FlightBp+=FMath::Min(Steps*Ref.Effect.RatioBp,Ref.Effect.SecondaryRatioBp); }
        }
        if (FlightBp>0) { Out.Add({Flights[0].Key,FlightBp}); }
    }
    // #345 card_desperado: while HP < hp_below, each missing_step of MaxHP adds +bonus_ratio to all damage dealt.
    if (Source->Resources.MaxHpP2>0 && !Hit.bStaggerBleed)
    {
        TArray<FLDPassiveEffectRef> Desp;
        if (!CollectPassiveEffects(Source->AttributeProjection,ELDDemoEffectOp::Desperado,Desp,Error)) { return false; }
        int64 DespBp=0;
        for (const FLDPassiveEffectRef& Ref : Desp)
        {
            if (Source->Resources.HpP2*10000<Source->Resources.MaxHpP2*Ref.Effect.RatioBp && Ref.Effect.CostRatioBp>0)
            {
                const int64 MissingBp=(Source->Resources.MaxHpP2-Source->Resources.HpP2)*10000/Source->Resources.MaxHpP2;
                DespBp+=(MissingBp/Ref.Effect.CostRatioBp)*Ref.Effect.SecondaryRatioBp;
            }
        }
        if (DespBp>0) { Out.Add({Desp[0].Key,DespBp}); }
    }
    return true;
}

void ALDCombatDirector::MarkEngaged(FLDBodyCombatState& Body, int64 NowMs) const
{
    const FLDDemoContentSnapshot* Content=Catalog();
    const int64 WindowMs=Content ? Content->Get().Parameters.Momentum.IdleDelayMs : 0;
    if (!Body.MomentumEngagedAtMs.IsSet() || NowMs-Body.MomentumEngagedAtMs.GetValue()>WindowMs || Body.FightStartedAtMs<=0)
    {
        Body.FightStartedAtMs=NowMs;
    }
    Body.LastCombatAtMs=NowMs;
    Body.MomentumEngagedAtMs=NowMs;
}

int64 ALDCombatDirector::IdleCountFloorMs(const FLDBodyCombatState& Body, int64 NowMs) const
{
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!Content) { return NowMs; }
    const auto& P=Content->Get().Parameters;
    const bool bInFight=Body.MomentumEngagedAtMs.IsSet() && Body.FightStartedAtMs>0
        && NowMs-Body.MomentumEngagedAtMs.GetValue()<=P.Momentum.IdleDelayMs;
    // Out of a fight only the carry counts; inside one, the whole fight plus the carry before it.
    return (bInFight ? Body.FightStartedAtMs : NowMs)-P.Cards.IdleCarryMs;
}

// ---------------------------------------------------------------- basics

bool ALDCombatDirector::OnBasicLanded(FLDBodyCombatState& Body, FLDCombatHit& Primary, TArray<FLDCombatHit>& OutExtra, FString& Error)
{
    using namespace LDCombatCardsDetail;
    OutExtra.Reset();
    const ALDGameMode* Mode=Core();
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!Mode || !Content) { Error=TEXT("A landed basic requires the current core and the approved Demo content."); return false; }
    const FLDAttributeProjection& Projection=Body.AttributeProjection;
    const FLDCombatSourceRef Basic{ELDCombatSourceKind::Basic,NAME_None};
    const bool bThird=Primary.ComboStep==3;
    // A04: the third step pushes its primary target away from the holder (a knockback of the basic; Y08 reads it).
    if (bThird)
    {
        TArray<FLDPassiveEffectRef> Pushes;
        if (!CardsOn(Projection,ELDDemoEffectOp::ComboKnockback,Basic,Pushes,Error)) { return false; }
        if (!Pushes.IsEmpty())
        {
            Primary.KnockbackCm=0;
            for (const FLDPassiveEffectRef& Ref : Pushes) { Primary.KnockbackCm+=Cm(Ref.Effect.DistanceCm); }
            Primary.KnockbackMs=Pushes[0].Effect.DurationMs;
            Primary.bKnockbackFromAnchor=false;
            Primary.KnockbackSource=Basic;
            LedgerCardTriggered(Body,Pushes[0].Key);
        }
    }
    // A11: a magic rider of the primary at its AP ratio (copies add), sharing its crit and factors.
    TArray<FLDPassiveEffectRef> Riders;
    if (!CardsOn(Projection,ELDDemoEffectOp::BasicApRider,Basic,Riders,Error)) { return false; }
    int64 RiderAPBp=0;
    for (const FLDPassiveEffectRef& Ref : Riders) { RiderAPBp+=Ref.Effect.APRatioBp; }
    const auto Rider=[&Primary,&Body,RiderAPBp](const FLDCombatHit& Parent)
    {
        FLDCombatHit Hit=LDCombatCardsDetail::MakeHit(Body,{ELDCombatSourceKind::Card,FName(TEXT("card_scholar_punch"))},Parent.Target,Parent.EffectId);
        Hit.APRatioBp=RiderAPBp;
        Hit.DamageType=ELDDemoDamageType::Magical;
        Hit.bCritical=Parent.bCritical;
        Hit.Factors=Parent.Factors;
        Hit.bRider=true;
        Hit.OnHitScaleBp=Parent.OnHitScaleBp;
        Hit.bLifesteal=Parent.bLifesteal;
        Hit.LifestealRatioBp=Parent.LifestealRatioBp;
        Hit.CreditOwner=Parent.CreditOwner;
        return Hit;
    };
    // A06: the third step becomes N+1 strikes at the card's share each (one per copy more); all are basic hits.
    if (bThird)
    {
        TArray<FLDPassiveEffectRef> Doubles;
        if (!CardsOn(Projection,ELDDemoEffectOp::ComboDouble,Basic,Doubles,Error)) { return false; }
        if (!Doubles.IsEmpty())
        {
            const int64 ShareBp=Doubles[0].Effect.MultiplierBp;
            Primary.DeliveryScaleBp=Share(Primary.DeliveryScaleBp,ShareBp);
            for (int32 Copy=0; Copy<Doubles.Num(); ++Copy)
            {
                FLDCombatHit Strike=Primary;
                Strike.HitId=FGuid::NewGuid();
                Strike.bExtraStrike=true;
                Strike.KnockbackCm=0;
                Strike.KnockbackMs=0;
                OutExtra.Add(MoveTemp(Strike));
            }
            LedgerCardTriggered(Body,Doubles[0].Key);
        }
    }
    // A09: attack speed past the card threshold turns into extra-strike chance (every full 100% is a certain strike).
    TArray<FLDPassiveEffectRef> Overcaps;
    if (!CardsOn(Projection,ELDDemoEffectOp::OvercapExtraHit,Basic,Overcaps,Error)) { return false; }
    if (!Overcaps.IsEmpty())
    {
        const FLDAttributeContributions* Row=Projection.Contributions.FindByPredicate([](const FLDAttributeContributions& Item)
        { return Item.Attribute==ELDProjectedAttribute::AttackSpeed; });
        // The uncapped rate: (base + flats) x (1 + ratios) of the build and the current temporary terms.
        double Uncapped=0;
        if (Row)
        {
            const auto R=[](const FLDDemoRational& V) { return V.Denominator>0 ? double(V.Numerator)/double(V.Denominator) : 0.; };
            const double Flat=R(Row->Base)+R(Row->LevelGrowth.Flat)+R(Row->PermanentChoices.Flat)+R(Row->Cards.Flat)+R(Row->Dev.Flat)+R(Row->Temporary.Flat);
            const double Ratio=1.+R(Row->LevelGrowth.AdditiveRatio)+R(Row->PermanentChoices.AdditiveRatio)+R(Row->Cards.AdditiveRatio)
                +R(Row->Dev.AdditiveRatio)+R(Row->Temporary.AdditiveRatio);
            Uncapped=Flat*Ratio;
        }
        int64 ChanceBp=0;
        for (const FLDPassiveEffectRef& Ref : Overcaps)
        {
            // #305: past the card's own threshold (ExtraDistanceCm attacks/s), not the attack-speed maximum.
            const double Step=Cm(Ref.Effect.StepRate), Threshold=Cm(Ref.Effect.ExtraDistanceCm);
            if (Step<=0 || Threshold<=0 || Uncapped<=Threshold) { continue; }
            // Bounded so the chance never leaves int64; anything near the bound is past the pending-hit budget anyway.
            const double Steps=FMath::Min(FMath::FloorToDouble((Uncapped-Threshold)/Step+1e-9),double(TechPendingHitsCap)*10000.);
            ChanceBp+=int64(Steps)*Ref.Effect.RatioBp;
        }
        int32 Strikes=int32(FMath::Min<int64>(ChanceBp/10000,TechPendingHitsCap));
        if (ChanceBp%10000>0 && CritRandom.RandRange(0,9999)<ChanceBp%10000) { ++Strikes; }
        if (Strikes>TechPendingHitsCap)
        {
            UE_LOG(LogLDCombat,Error,TEXT("LD_CHAIN_GUARD reason=Overflow what=overcap_extra_hit src=%s requested=%lld kept=%d"),
                *LDLog::Id(Body.Victim.EntityId),ChanceBp/10000,TechPendingHitsCap);
            Strikes=TechPendingHitsCap;
        }
        for (int32 Index=0; Index<Strikes; ++Index)
        {
            FLDCombatHit Strike=Primary;
            Strike.HitId=FGuid::NewGuid();
            Strike.bExtraStrike=true;
            Strike.bAttackSpeedStrike=true;
            Strike.KnockbackCm=0;
            Strike.KnockbackMs=0;
            OutExtra.Add(MoveTemp(Strike));
        }
        if (Strikes>0) { LedgerCardTriggered(Body,Overcaps[0].Key); }
    }
    // A07: the third step also sweeps a cone in front of the holder: every other enemy in its reach takes the card's share
    // (copies add their share), each a basic hit outside the combo.
    if (bThird)
    {
        TArray<FLDPassiveEffectRef> Sweeps;
        if (!CardsOn(Projection,ELDDemoEffectOp::ComboSweep,Basic,Sweeps,Error)) { return false; }
        const ALDCharacter* HolderActor=ActorOf(Body);
        ALDCharacter* TargetActor=nullptr;
        FindBody(Primary.Target,&TargetActor);
        if (!Sweeps.IsEmpty() && HolderActor && TargetActor)
        {
            int64 ShareBp=0;
            for (const FLDPassiveEffectRef& Ref : Sweeps) { ShareBp+=Ref.Effect.RatioBp; }
            const FVector Anchor=FootOf(*HolderActor);
            const FVector Facing=(FootOf(*TargetActor)-Anchor).GetSafeNormal2D();
            const double HalfAngle=FMath::DegreesToRadians(Cm(Sweeps[0].Effect.AngleDegrees)*.5);
            const double Reach=Cm(Projection.Final.AttackRangeCm);
            TArray<FLDCombatBodyRef> Around;
            if (!CollectAreaTargets(Anchor,Reach,&Body,Body.Victim.EntityId,Around,Error)) { return false; }
            bool bSwept=false;
            for (const FLDCombatBodyRef& Other : Around)
            {
                ALDCharacter* OtherActor=nullptr;
                if (Other==Primary.Target || !FindBody(Other,&OtherActor) || !OtherActor) { continue; }
                const FVector Toward=(FootOf(*OtherActor)-Anchor).GetSafeNormal2D();
                if (!Facing.IsNearlyZero() && !Toward.IsNearlyZero() && FVector::DotProduct(Facing,Toward)<FMath::Cos(HalfAngle)) { continue; }
                FLDCombatHit Strike=Primary;
                Strike.HitId=FGuid::NewGuid();
                Strike.Target=Other;
                Strike.bExtraStrike=true;
                Strike.KnockbackCm=0;
                Strike.KnockbackMs=0;
                Strike.DeliveryScaleBp=Share(Primary.DeliveryScaleBp,FMath::Min<int64>(ShareBp,10000));
                OutExtra.Add(MoveTemp(Strike));
                bSwept=true;
            }
            if (bSwept) { LedgerCardTriggered(Body,Sweeps[0].Key); }
        }
    }
    if (RiderAPBp>0)
    {
        TArray<FLDCombatHit> Carried;
        Carried.Add(Rider(Primary));
        for (const FLDCombatHit& Strike : OutExtra) { Carried.Add(Rider(Strike)); }
        OutExtra.Append(MoveTemp(Carried));
        LedgerCardTriggered(Body,FName(TEXT("card_scholar_punch")));
    }
    // #345 card_brain_speed: every full haste_step of CooldownHaste adds flat Magical rider to primary basics.
    {
        TArray<FLDPassiveEffectRef> Brains;
        if (!CollectPassiveEffects(Projection,ELDDemoEffectOp::BrainSpeed,Brains,Error)) { return false; }
        int64 BrainFlatP2=0;
        const int64 HasteWhole=FMath::Max<int64>(Projection.Final.CooldownHasteP2,0)/100;
        for (const FLDPassiveEffectRef& Ref : Brains)
        {
            if (Ref.Effect.Count>0 && HasteWhole>=Ref.Effect.Count)
            {
                BrainFlatP2+=(HasteWhole/int64(Ref.Effect.Count))*Ref.Effect.BaseP2;
            }
        }
        if (BrainFlatP2>0)
        {
            FLDCombatHit BrainHit=MakeHit(Body,Brains[0].Source,Primary.Target,Primary.EffectId);
            BrainHit.BaseP2=BrainFlatP2;
            BrainHit.DamageType=ELDDemoDamageType::Magical;
            BrainHit.bCritical=Primary.bCritical;
            BrainHit.Factors=Primary.Factors;
            BrainHit.bRider=true;
            OutExtra.Add(MoveTemp(BrainHit));
            LedgerCardTriggered(Body,Brains[0].Key);
        }
    }
    // #345 card_heavy_mallet: Combo Step 3 is guaranteed crit, and excess AS boosts knockback distance on basics.
    {
        TArray<FLDPassiveEffectRef> Mallets;
        if (!CollectPassiveEffects(Projection,ELDDemoEffectOp::HeavyMallet,Mallets,Error)) { return false; }
        if (!Mallets.IsEmpty())
        {
            if (bThird)
            {
                Primary.bCritical=true;
                for (FLDCombatHit& Ex : OutExtra) { Ex.bCritical=true; }
                LedgerCardTriggered(Body,Mallets[0].Key);
            }
            if (Primary.KnockbackCm>0.)
            {
                const FLDAttributeContributions* ASRow=Projection.Contributions.FindByPredicate(
                    [](const FLDAttributeContributions& Item) { return Item.Attribute==ELDProjectedAttribute::AttackSpeed; });
                if (ASRow)
                {
                    const auto R=[](const FLDDemoRational& V) { return V.Denominator>0 ? double(V.Numerator)/double(V.Denominator) : 0.; };
                    const double Flat=R(ASRow->Base)+R(ASRow->LevelGrowth.Flat)+R(ASRow->PermanentChoices.Flat)+R(ASRow->Cards.Flat)+R(ASRow->Dev.Flat)+R(ASRow->Temporary.Flat);
                    const double Ratio=1.+R(ASRow->LevelGrowth.AdditiveRatio)+R(ASRow->PermanentChoices.AdditiveRatio)+R(ASRow->Cards.AdditiveRatio)+R(ASRow->Dev.AdditiveRatio)+R(ASRow->Temporary.AdditiveRatio);
                    const double Uncapped=Flat*Ratio, Cap=Cm(Mallets[0].Effect.ExtraDistanceCm);
                    if (Cap>0. && Uncapped>Cap)
                    {
                        const int64 ExcessPct=int64(FMath::FloorToDouble((Uncapped-Cap)/Cap*100.+1e-9));
                        int64 ScaleBp=0;
                        for (const FLDPassiveEffectRef& Ref : Mallets) { ScaleBp+=ExcessPct*Ref.Effect.RatioBp; }
                        if (ScaleBp>0) { Primary.KnockbackCm*=1.+double(ScaleBp)/10000.; }
                    }
                }
            }
        }
    }
    // #345 card_tonnage_crush: Combo Step 3 adds 3.5% MaxHP Physical rider (7% vs camp monsters).
    if (bThird)
    {
        TArray<FLDPassiveEffectRef> Tonnage;
        if (!CollectPassiveEffects(Projection,ELDDemoEffectOp::TonnageCrush,Tonnage,Error)) { return false; }
        if (!Tonnage.IsEmpty() && BonusMaxHpP2Of(Body)>0)
        {
            const bool bVsMonster=Primary.Target.Victim.Kind==ELDVictimKind::CampMember;
            int64 RatioBp=0;
            for (const FLDPassiveEffectRef& Ref : Tonnage) { RatioBp+=bVsMonster ? Ref.Effect.SecondaryRatioBp : Ref.Effect.RatioBp; }
            const int64 CrushP2=Share(BonusMaxHpP2Of(Body),RatioBp);
            if (CrushP2>0)
            {
                FLDCombatHit Crush=MakeHit(Body,Tonnage[0].Source,Primary.Target,Primary.EffectId);
                Crush.BaseP2=CrushP2;
                Crush.DamageType=ELDDemoDamageType::Physical;
                Crush.bRider=true;
                OutExtra.Add(MoveTemp(Crush));
                LedgerCardTriggered(Body,Tonnage[0].Key);
            }
        }
    }
    MarkViewChanged(Body.Victim.EntityId);
    return true;
}

bool ALDCombatDirector::ApplyCardElement(FLDCombatHit& Hit, ELDDemoElement Element, int32 FireStacks, const FLDCombatSourceRef& Card,
    FString& Error, uint8 MedalTier) const
{
    const uint8 NormalizedTier=FMath::Max<uint8>(MedalTier,1);
    switch (Element)
    {
    case ELDDemoElement::Fire:
        if (Hit.CardBurnStacks>0 && Hit.CardBurnSource!=Card)
        {
            // Fire of a second card joins the first's layers (one applier of the hit's fire); keep the highest fire medal tier.
            Hit.CardBurnStacks+=FMath::Max(FireStacks,1);
            Hit.CardBurnTier=FMath::Max<uint8>(Hit.CardBurnTier,NormalizedTier);
            return true;
        }
        Hit.CardBurnStacks+=FMath::Max(FireStacks,1);
        Hit.CardBurnSource=Card;
        Hit.CardBurnTier=NormalizedTier;
        return true;
    case ELDDemoElement::Water:
    case ELDDemoElement::Electric:
    case ELDDemoElement::Chill:
    case ELDDemoElement::Rot:
    {
        const ELDCombatElement Kind=LDCombatCardsDetail::CombatElement(Element);
        if (!Hit.CardElements.ContainsByPredicate([Kind](const FLDCombatHit::FCardElementEntry& Row) { return Row.Element==Kind; }))
        {
            Hit.CardElements.Add({Kind,Card,NormalizedTier});
        }
        return true;
    }
    default:
        Error=FString::Printf(TEXT("%s names no element."),*LDLog::Source(Card));
        return false;
    }
}

bool ALDCombatDirector::MakeEmpower(const FLDBodyCombatState& Body, const FLDCombatSourceRef& Source,
    const FLDDemoEffectDefinition& Effect, const FGuid& EffectId, int64 NowMs, FLDCombatEmpower& Out, FString& Error) const
{
    using namespace LDCombatCardsDetail;
    Out=FLDCombatEmpower();
    // Every package merges into the one physical primary hit (spec §10.2).
    if (Effect.DamageType!=ELDDemoDamageType::Physical || Effect.DurationMs<=0)
    {
        Error=FString::Printf(TEXT("empower_basic of %s must be a physical package with a positive window."),*LDLog::Source(Source));
        return false;
    }
    Out.EffectId=EffectId;
    Out.Source=Source;
    Out.Charges=FMath::Max(Effect.Count,1);
    Out.MomentumP2=Effect.LimitP2;
    Out.BreachStacks=Effect.BasicCount;
    Out.LastStunMs=Effect.ExtraDurationMs;
    Out.bAlwaysCrit=Effect.bAlwaysCrit;
    const FLDProjectedAttributes& Final=Body.AttributeProjection.Final;
    const int64 AD=Share(Final.ADP2,Effect.ADRatioBp), AP=Share(Final.APP2,Effect.APRatioBp);
    if (Effect.BaseP2<0 || AD>MAX_int64-Effect.BaseP2 || AP>MAX_int64-Effect.BaseP2-AD)
    {
        Error=FString::Printf(TEXT("empower_basic of %s is outside the checked numeric domain."),*LDLog::Source(Source));
        return false;
    }
    Out.AmountP2=Effect.BaseP2+AD+AP;
    return LDMatchLifecycle::AddDurationMs(NowMs,Effect.DurationMs,Out.ExpiresAtMs,Error);
}

bool ALDCombatDirector::ResolveDashCompletions(int64 NowMs, FString& Error)
{
    for (auto& Pair : Bodies)
    {
        FLDBodyCombatState& Body=Pair.Value;
        if (!Body.VoluntaryDash.IsSet() || NowMs<Body.VoluntaryDash->EndMs) { continue; }
        const FLDCombatVoluntaryDash Dash=Body.VoluntaryDash.GetValue();
        Body.VoluntaryDash.Reset();
        // Spec §6.5: the completion's cards and grounds are steps of the dash's own chain (while it is open).
        TGuardValue<FGuid> ChainScope(CurrentChain,EffectChains.FindRef(Dash.EffectId));
        const ALDCharacter* Actor=Pair.Key.Get();
        // A dash that went nowhere (blocked at once) is no completed dash.
        if (!Actor || Body.bPendingDeath || !LDCombatControl::DashCompleted(Dash,NowMs,FootOf(*Actor))) { continue; }
        // D10: the hosted D leaves an ice band along its path; D11: it moves the holder's newest ground state, zone or trap.
        TArray<FLDPassiveEffectRef> Trails, Movers;
        if (!CardsOn(Body.AttributeProjection,ELDDemoEffectOp::DashIce,Dash.Source,Trails,Error)
            || !CardsOn(Body.AttributeProjection,ELDDemoEffectOp::GroundRelocate,Dash.Source,Movers,Error)) { return false; }
        const FVector EndFoot=FootOf(*Actor);
        // D11 moves what was placed before this dash, never the band D10 lays along it (both read the same completion).
        if (!Movers.IsEmpty())
        {
            if (!RelocateNewestGround(Body,EndFoot,NowMs,Error)) { return false; }
            LedgerCardTriggered(Body,Movers[0].Key);
        }
        // Y12: every copy keeps a trail one more of its lifetimes, so that many more bands stand at once.
        TArray<FLDPassiveEffectRef> Stalls;
        if (!Trails.IsEmpty() && !CollectPassiveEffects(Body.AttributeProjection,ELDDemoEffectOp::DoubleStall,Stalls,Error)) { return false; }
        for (const FLDPassiveEffectRef& Trail : Trails)
        {
            const FVector Path=FVector(EndFoot.X-Dash.StartFootCm.X,EndFoot.Y-Dash.StartFootCm.Y,0);
            FLDElementApplier TrailOwner;
            TrailOwner.SourceEntity=Body.Victim.EntityId;
            TrailOwner.SourceBody={Body.Victim,Body.Identity};
            TrailOwner.CreditOwner=Body.Victim.Kind==ELDVictimKind::Contender ? Body.Victim.EntityId : FGuid();
            TrailOwner.Source=Trail.Source;
            TrailOwner.Level=GrowthLevelOf(Body);
            TrailOwner.SourceAPP2=FMath::Max<int64>(Body.AttributeProjection.Final.APP2,0);
            const double Width=double(Trail.Effect.RadiusCm.Numerator)/double(FMath::Max<int64>(Trail.Effect.RadiusCm.Denominator,1));
            if (!SpawnGround(ELDGroundStateKind::Ice,TrailOwner,(Dash.StartFootCm+EndFoot)*.5,NowMs,Error,Width*.5,Trail.Effect.DurationMs*(1+Stalls.Num()),
                FVector2D(Path.X,Path.Y),Path.Size()*.5)) { return false; }
            LedgerCardTriggered(Body,Trail.Key);
        }
        // D03: the hosted D's completion arms one package per copy (a newer one replaces that copy's unused package).
        TArray<FLDPassiveEffectRef> Held;
        if (!CardsOn(Body.AttributeProjection,ELDDemoEffectOp::EmpowerBasic,Dash.Source,Held,Error)) { return false; }
        bool bArmed=false;
        for (const FLDPassiveEffectRef& Ref : Held)
        {
            if (Ref.Effect.Trigger!=ELDDemoEffectTrigger::DashComplete) { continue; }
            FLDCombatEmpower Package;
            FLDCombatSourceRef Copy=Ref.Source;
            Copy.DefinitionId=FName(*FString::Printf(TEXT("%s#%u"),*Ref.Key.ToString(),uint32(Ref.Serial)));
            if (!MakeEmpower(Body,Copy,Ref.Effect,FGuid::NewGuid(),NowMs,Package,Error)) { return false; }
            Package.Source=Ref.Source;
            LDCombatControl::ArmEmpower(Body.Empowers,Package);
            bArmed=true;
        }
        if (Body.AttributeProjection.Keystones.bAgility)
        {
            const FLDDemoContentSnapshot* Content=Catalog();
            if (!Content) { Error=TEXT("The agility keystone requires the approved Demo content."); return false; }
            const FLDDemoContentParameters& Parameters=Content->Get().Parameters;
            const int64 ADP2=Body.AttributeProjection.Final.ADP2;
            const int64 AddedP2=LDCombatCardsDetail::Share(ADP2,Parameters.KeystoneAgilityADRatioBp);
            if (AddedP2>MAX_int64-Parameters.KeystoneAgilityDamageBaseP2)
            { Error=TEXT("The agility keystone empower is outside the checked numeric domain."); return false; }
            FLDCombatEmpower Package;
            Package.EffectId=FGuid::NewGuid();
            Package.AmountP2=Parameters.KeystoneAgilityDamageBaseP2+AddedP2;
            if (!LDMatchLifecycle::AddDurationMs(NowMs,Parameters.KeystoneAgilityEmpowerDurationMs,Package.ExpiresAtMs,Error))
            { return false; }
            LDCombatControl::ArmEmpower(Body.Empowers,Package);
            // The same window lends the dasher move speed.
            FLDCombatTimedBonus Speed;
            Speed.EffectId=FGuid::NewGuid();
            Speed.SourceDefinition=FName(TEXT("keystone_agility_speed"));
            FLDDemoStatBonus& Stat=Speed.Stats.AddDefaulted_GetRef();
            Stat.Stat=ELDDemoBonusStat::MoveSpeedBonusRatio;
            Stat.Amount=Parameters.KeystoneAgilityMoveSpeedBp;
            Speed.ExpiresAtMs=Package.ExpiresAtMs;
            Body.TimedBonuses.RemoveAll([](const FLDCombatTimedBonus& Row)
                { return Row.SourceDefinition==FName(TEXT("keystone_agility_speed")); });
            if (Stat.Amount>0) { LDTactical::ApplyTimedBonus(Body.TimedBonuses,Speed); }
            bArmed=true;
        }
        if (bArmed)
        {
            ++Body.StateRevision;
            MarkViewChanged(Body.Victim.EntityId);
        }
    }
    return true;
}

// ---------------------------------------------------------------- triggers

bool ALDCombatDirector::HasteOtherActives(FLDBodyCombatState& Holder, const FLDCombatSourceRef& Host, const FGuid& EffectId,
    int64 NowMs, FString& Error)
{
    if (Holder.bPendingDeath || !EffectId.IsValid()) { return true; }
    TArray<FLDPassiveEffectRef> Hastes;
    if (!CardsOn(Holder.AttributeProjection,ELDDemoEffectOp::HitHasteOther,Host,Hastes,Error)) { return false; }
    FLDCombatEntityRecord* Record=Hastes.IsEmpty() ? nullptr : FindRecord(Holder.Victim.EntityId);
    for (const FLDPassiveEffectRef& Haste : Hastes)
    {
        if (!Record) { break; }
        // Once per release EffectId and copy (K01 at the host's first hit, D02 at the dash's first contact).
        if (Holder.EffectBudgets.ContainsByPredicate([&](const FLDCombatEffectBudget& Row)
            { return Row.EffectId==EffectId && Row.Card==Haste.Key && Row.Serial==Haste.Serial; }))
        { continue; }
        Holder.EffectBudgets.Add({EffectId,Haste.Key,Haste.Serial,NowMs});
        int32 Cut=0;
        for (FLDCombatTrack& Track : Record->Tracks)
        {
            if (Track.Kind!=ELDCombatSourceKind::Learned || Track.ReadyAtMs<=NowMs) { continue; }
            if (Host.Kind==ELDCombatSourceKind::Learned && Track.DefinitionId==Host.DefinitionId) { continue; }
            Track.ReadyAtMs=LDCombatSourceRules::ReduceReadyAt(Track.ReadyAtMs,NowMs,Haste.Effect.AmountMs);
            ++Cut;
        }
        if (Cut==0) { continue; }
        MarkViewChanged(Holder.Victim.EntityId);
        LedgerCardTriggered(Holder,Haste.Key);
        UE_LOG(LogLDCombat,Log,TEXT("LD_COMBAT_HIT_HASTE t=%lld holder=%s host=%s cut=%d amount_ms=%lld"),NowMs,
            *LDLog::Id(Holder.Victim.EntityId),*LDLog::Source(Host),Cut,Haste.Effect.AmountMs);
    }
    Holder.EffectBudgets.RemoveAll([NowMs](const FLDCombatEffectBudget& Row) { return NowMs-Row.AtMs>30000; });
    return true;
}

bool ALDCombatDirector::OnHostDirectHit(FLDBodyCombatState& Holder, const FLDCombatHit& Hit, const FLDCombatSourceRef& Host,
    int64 NowMs, FString& Error)
{
    if (Holder.bPendingDeath) { return true; }
    const bool bFreeAutoRecast=Hit.CastLedger && Hit.CastLedger->bAutoRecast;
    // K01: the host's hit cuts the other skills (once per paid release and copy; free auto-recasts do not refund cooldown).
    if (!bFreeAutoRecast && !HasteOtherActives(Holder,Host,Hit.EffectId,NowMs,Error)) { return false; }
    // K04: the host's hit sets (or refreshes) its mark on the struck life.
    Holder.SetupMarks.RemoveAll([NowMs](const FLDCombatSetupMark& Mark) { return Mark.ExpiresAtMs<=NowMs; });
    TArray<FLDPassiveEffectRef> Setups, Grants, Growths;
    if (!CardsOn(Holder.AttributeProjection,ELDDemoEffectOp::SetupMark,Host,Setups,Error)
        || !CardsOn(Holder.AttributeProjection,ELDDemoEffectOp::SkillHitMomentum,Host,Grants,Error)
        || !CardsOn(Holder.AttributeProjection,ELDDemoEffectOp::SkillGrowth,Host,Growths,Error))
    { return false; }
    for (const FLDPassiveEffectRef& Setup : Setups)
    {
        FLDCombatSetupMark* Mark=Holder.SetupMarks.FindByPredicate([&](const FLDCombatSetupMark& Row)
        { return Row.Target==Hit.Target && Row.Serial==Setup.Serial; });
        if (!Mark) { Mark=&Holder.SetupMarks.AddDefaulted_GetRef(); }
        Mark->Target=Hit.Target;
        Mark->Card=Setup.Key;
        Mark->Serial=Setup.Serial;
        Mark->Host=Setup.Active;
        if (!LDMatchLifecycle::AddDurationMs(NowMs,Setup.Effect.DurationMs,Mark->ExpiresAtMs,Error)) { return false; }
    }
    // K02: every paid hit of the host gives momentum (each copy); free auto-recasts paid nothing and do not refund momentum.
    if (!bFreeAutoRecast)
    {
        for (const FLDPassiveEffectRef& Granted : Grants)
        {
            if (GrantMomentum(Holder,Granted.Effect.LimitP2,ELDMomentumBook::Skill)>0) { LedgerCardTriggered(Holder,Granted.Key); }
        }
    }
    // K14: the host's hit spends a share of the holder's shield as damage of its own type (copies add their share).
    TArray<FLDPassiveEffectRef> Spends;
    if (!CardsOn(Holder.AttributeProjection,ELDDemoEffectOp::ShieldSpend,Host,Spends,Error)) { return false; }
    if (!Spends.IsEmpty() && Holder.Shield.IsSet() && Holder.Shield->ExpiresAtMs>NowMs && Holder.Resources.ShieldP2>0
        && ChainAllows(Hit,TEXT("shield_spend")))
    {
        int64 ShareBp=0;
        for (const FLDPassiveEffectRef& Ref : Spends) { ShareBp+=Ref.Effect.RatioBp; }
        const int64 Spent=LDCombatCardsDetail::Share(Holder.Resources.ShieldP2,FMath::Min<int64>(ShareBp,10000));
        if (Spent>0)
        {
            Holder.Resources.ShieldP2-=Spent;
            SpendShieldLayers(Holder,Spent);
            if (Holder.Resources.ShieldP2<=0) { Holder.Shield.Reset(); Holder.Resources.ShieldP2=0; Holder.ShieldLayers.Reset(); }
            ++Holder.Resources.Revision;
            ++Holder.StateRevision;
            MarkViewChanged(Holder.Victim.EntityId);
            FLDCombatHit Burst=LDCombatCardsDetail::MakeHit(Holder,Spends[0].Source,Hit.Target,Hit.EffectId);
            Burst.BaseP2=Spent;
            Burst.DamageType=Hit.DamageType;
            Burst.bRider=true;
            PendingHits.Add(MoveTemp(Burst));
            LedgerCardTriggered(Holder,Spends[0].Key);
        }
    }
    // K17: every hit of the host on a contender grows that copy for the match.
    if (!Growths.IsEmpty() && Hit.Target.Victim.Kind==ELDVictimKind::Contender)
    {
        if (FLDCombatEntityRecord* Record=FindRecord(Holder.Victim.EntityId))
        {
            for (const FLDPassiveEffectRef& Growth : Growths) { ++Record->CardGrowth.SkillHits.FindOrAdd(Growth.Serial); }
            LedgerCardTriggered(Holder,Growths[0].Key);
        }
    }
    return true;
}

bool ALDCombatDirector::OnCardHit(const FLDCombatHit& Hit, FLDBodyCombatState& Target, FLDBodyCombatState* Source,
    const FLDDamageEvaluation& Damage, int64 NowMs, FString& Error)
{
    using namespace LDCombatCardsDetail;
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!Content) { Error=TEXT("Card hits require the approved Demo content."); return false; }
    const FLDDemoContentParameters& Parameters=Content->Get().Parameters;
    const bool bHostile=Hit.SourceEntity!=Target.Victim.EntityId;
    const bool bEffective=Damage.HPLostP2>0 || Damage.ShieldAbsorbedP2>0;
    const FLDCombatBodyRef TargetRef{Target.Victim,Target.Identity};
    // ---- The struck body's cards.
    if (bHostile && !Target.bPendingDeath && Target.Resources.HpP2>0)
    {
        // Y03: below its threshold the stand starts (once per life).
        if (!LastStandActive(Target,NowMs,Error)) { return false; }
        // Y01: a direct hit (its shield-absorbed part included) is reflected as magic damage to the attacking life.
        const bool bDirect=!Hit.bReflect && !Hit.bBurnTick && !Hit.bPeriodic && !Hit.bStatusSettle && !Hit.bRider;
        int64 ReflectBp=0;
        if (bDirect && Damage.DamageP2>0 && !SumCards(Target.AttributeProjection,ELDDemoEffectOp::Thorns,&FLDDemoEffectDefinition::RatioBp,ReflectBp,Error))
        { return false; }
        ALDCharacter* AttackerActor=nullptr;
        const FLDBodyCombatState* Attacker=FindBody(Hit.SourceBody,&AttackerActor);
        if (ReflectBp>0 && Attacker && !Attacker->bPendingDeath && Attacker!=&Target && ChainAllows(Hit,TEXT("thorns")))
        {
            // #345 card_iron_hedgehog: thorns gains +(Armor+MR)*resist_ratio Magical damage, and +stance_bonus ratio while taunting/bracing/parrying.
            TArray<FLDPassiveEffectRef> Hedgehogs;
            if (!CollectPassiveEffects(Target.AttributeProjection,ELDDemoEffectOp::IronHedgehog,Hedgehogs,Error)) { return false; }
            int64 EffectiveReflectBp=ReflectBp;
            int64 ResistBonusP2=0;
            if (!Hedgehogs.IsEmpty())
            {
                bool bTauntingAny=false;
                for (const TPair<TWeakObjectPtr<ALDCharacter>,FLDBodyCombatState>& Row : Bodies)
                {
                    if (Row.Value.Taunt.IsSet() && Row.Value.Taunt->ExpiresAtMs>NowMs && Row.Value.Taunt->SourceEntity==Target.Victim.EntityId)
                    {
                        bTauntingAny=true;
                        break;
                    }
                }
                const bool bInStance=LDCombatElements::BraceActive(Target.Brace,NowMs)
                    || (Target.ActiveParry.IsSet() && NowMs<Target.ParryUntilMs)
                    || bTauntingAny;
                int64 StanceMultBp=0, ResistRatioBp=0;
                for (const FLDPassiveEffectRef& Ref : Hedgehogs)
                {
                    ResistRatioBp+=Ref.Effect.RatioBp;
                    if (bInStance) { StanceMultBp+=Ref.Effect.SecondaryRatioBp; }
                }
                if (StanceMultBp>0) { EffectiveReflectBp=Share(EffectiveReflectBp,10000+StanceMultBp); }
                const int64 ResistSum=Target.AttributeProjection.Final.ArmorP2+Target.AttributeProjection.Final.MagicResistP2;
                ResistBonusP2=Share(ResistSum,ResistRatioBp);
                LedgerCardTriggered(Target,Hedgehogs[0].Key);
            }
            FLDCombatHit Reflect=MakeHit(Target,{ELDCombatSourceKind::Card,FName(TEXT("card_spiky_hide"))},Hit.SourceBody,FGuid());
            Reflect.BaseP2=Share(Damage.DamageP2,EffectiveReflectBp)+ResistBonusP2;
            Reflect.DamageType=ELDDemoDamageType::Magical;
            Reflect.bReflect=true;
            // Spec §4.4: a reflection never heals (no lifesteal snapshot).
            if (!SnapshotOnHit(Target,0,Reflect,Error)) { return false; }
            PendingHits.Add(MoveTemp(Reflect));
            LedgerCardTriggered(Target,FName(TEXT("card_spiky_hide")));
        }
        // Y25: an attacker within the aura's radius takes its element (applied by the holder).
        TArray<FLDPassiveEffectRef> Auras;
        if (bDirect && Attacker && AttackerActor && !Attacker->bPendingDeath
            && !CollectPassiveEffects(Target.AttributeProjection,ELDDemoEffectOp::ElementAura,Auras,Error)) { return false; }
        ALDCharacter* TargetActor=nullptr;
        FindBody(TargetRef,&TargetActor);
        for (const FLDPassiveEffectRef& Aura : Auras)
        {
            if (!TargetActor || FVector::Dist2D(FootOf(*TargetActor),FootOf(*AttackerActor))>Cm(Aura.Effect.RadiusCm)) { continue; }
            const ELDCombatElement Kind=CombatElement(Aura.Element);
            FLDBodyCombatState* AttackerState=FindBody(Hit.SourceBody);
            if (Kind==ELDCombatElement::None || !AttackerState) { continue; }
            FLDElementApplier Applier;
            Applier.SourceEntity=Target.Victim.EntityId;
            Applier.SourceBody=TargetRef;
            Applier.CreditOwner=Target.Victim.Kind==ELDVictimKind::Contender ? Target.Victim.EntityId : FGuid();
            Applier.Source=Aura.Source;
            Applier.Level=GrowthLevelOf(Target);
            Applier.SourceAPP2=FMath::Max<int64>(Target.AttributeProjection.Final.APP2,0);
            if (!ApplyElement(*AttackerState,Kind,Applier,Kind==ELDCombatElement::Fire ? 1 : 0,FGuid::NewGuid(),NowMs,Error)) { return false; }
            LedgerCardTriggered(Target,Aura.Key);
        }
    }
    if (!Source || !bHostile || Source->bPendingDeath) { return true; }
    // ---- The source's cards.
    // Y22: positive damage (not over time) ends the charge.
    if (Damage.DamageP2>0 && !Hit.bBurnTick) { Source->IdleSinceMs=NowMs; }
    if (!bEffective) { return true; }
    const bool bBasic=(Hit.HitClass==ELDCombatHitClass::BasicPrimary || Hit.bTriggersBasicOnHit) && !Hit.bRider;
    const bool bNative=IsNativeDirectHit(Hit);
    const FLDCombatSourceRef Basic{ELDCombatSourceKind::Basic,NAME_None};
    // Lifesteal heals: the basic's own (SnapshotOnHit) heals in ApplyHit; Y10 extends the holder's lifesteal to skills and
    // K15 heals a share of the host skill's damage.
    if (bNative && Damage.HPLostP2>0)
    {
        int64 HealBp=0;
        TArray<FLDPassiveEffectRef> Hurts;
        if (!CardsOn(Source->AttributeProjection,ELDDemoEffectOp::SkillLifesteal,Hit.Source,Hurts,Error)) { return false; }
        for (const FLDPassiveEffectRef& Ref : Hurts) { HealBp+=Ref.Effect.RatioBp; }
        TArray<FLDPassiveEffectRef> Drains;
        if (!CollectPassiveEffects(Source->AttributeProjection,ELDDemoEffectOp::LifestealSkills,Drains,Error)) { return false; }
        if (!Drains.IsEmpty())
        {
            int64 Lifesteal=0;
            if (!SumCards(Source->AttributeProjection,ELDDemoEffectOp::Lifesteal,&FLDDemoEffectDefinition::RatioBp,Lifesteal,Error)) { return false; }
            HealBp+=Lifesteal;
        }
        if (HealBp>0)
        {
            FString HealError;
            if (HealBody(*Source,Share(Damage.HPLostP2,HealBp),Hit.Source,nullptr,FGuid(),&HealError) && !Hurts.IsEmpty())
            { LedgerCardTriggered(*Source,Hurts[0].Key); }
            if (!HealError.IsEmpty()) { Error=MoveTemp(HealError); return false; }
        }
    }
    // Skill-hit triggers of a native hit: the host's cards and Y16.
    const auto SkillHit=[&](const FLDCombatSourceRef& Host)
    {
        return OnHostDirectHit(*Source,Hit,Host,NowMs,Error);
    };
    const auto BrainMomentum=[&]() -> bool
    {
        TArray<FLDPassiveEffectRef> Brains;
        if (!CollectPassiveEffects(Source->AttributeProjection,ELDDemoEffectOp::HasteMomentum,Brains,Error)) { return false; }
        for (const FLDPassiveEffectRef& Ref : Brains)
        {
            if (Ref.Effect.BaseP2<=0) { continue; }
            const int64 Steps=FMath::Max<int64>(Source->AttributeProjection.Final.CooldownHasteP2,0)/Ref.Effect.BaseP2;
            if (Steps>0 && GrantMomentum(*Source,Steps*Ref.Effect.LimitP2,ELDMomentumBook::Skill)>0) { LedgerCardTriggered(*Source,Ref.Key); }
        }
        return true;
    };
    if (bNative)
    {
        if (!SkillHit(Hit.Source) || !BrainMomentum()) { return false; }
        // #345 card_fuel_the_fire: hitting a max-burn or rotten target settles settle_duration of active DoT without consuming stacks.
        if (ChainAllows(Hit,TEXT("fuel_the_fire")))
        {
            TArray<FLDPassiveEffectRef> Fuels;
            if (!CardsOn(Source->AttributeProjection,ELDDemoEffectOp::FuelTheFire,Hit.Source,Fuels,Error)) { return false; }
            if (!Fuels.IsEmpty())
            {
                FLDKeywordTerms KwTerms;
                FLDElementTerms ElTerms;
                if (!KeywordTerms(KwTerms,Error) || !ElementTerms(ElTerms,Error)) { return false; }
                int32 MaxBurnCap=KwTerms.BurnMaxStacks;
                TArray<FLDPassiveEffectRef> Caps;
                if (!CollectPassiveEffects(Source->AttributeProjection,ELDDemoEffectOp::BurnCap,Caps,Error)) { return false; }
                for (const FLDPassiveEffectRef& CapRef : Caps) { MaxBurnCap+=FMath::Max(CapRef.Effect.Count-KwTerms.BurnMaxStacks,0); }
                const bool bFullBurn=LDCombatKeywords::BurnActive(Target.Burn,NowMs) && Target.Burn.Layers.Num()>=MaxBurnCap;
                const bool bRotten=Target.ElementMark.Kind==ELDCombatElement::Rot && Target.ElementMark.ExpiresAtMs>NowMs;
                if (bFullBurn || bRotten)
                {
                    int64 SettleMs=0;
                    for (const FLDPassiveEffectRef& Ref : Fuels) { SettleMs+=Ref.Effect.DurationMs; }
                    int64 SettleP2=0;
                    if (bFullBurn)
                    {
                        for (const FLDCombatBurnLayer& Layer : Target.Burn.Layers)
                        {
                            if (Layer.ExpiresAtMs<=NowMs || LDCombatKeywords::BurnLayerTicksLeft(Layer,KwTerms)<=0) { continue; }
                            const int64 PerSecP2=LDCombatKeywords::BurnLayerPerSecondP2(Layer.Level,Layer.SourceAPP2,KwTerms);
                            SettleP2+=PerSecP2*SettleMs/1000;
                        }
                    }
                    if (bRotten)
                    {
                        const int64 RotPerSecP2=ElTerms.RotPerSecP2+Share(Target.ElementMark.Applier.SourceAPP2,ElTerms.RotAPRatioPerSecBp);
                        SettleP2+=RotPerSecP2*SettleMs/1000;
                    }
                    if (SettleP2>0)
                    {
                        FLDCombatHit Burst=MakeHit(*Source,Fuels[0].Source,Hit.Target,Hit.EffectId);
                        Burst.BaseP2=SettleP2;
                        Burst.DamageType=ELDDemoDamageType::Magical;
                        Burst.bStatusSettle=true;
                        Burst.bRider=true;
                        PendingHits.Add(MoveTemp(Burst));
                        LedgerCardTriggered(*Source,Fuels[0].Key);
                    }
                }
            }
        }
        // #345 card_tonnage_crush: own self-area skills also add 3.5% MaxHP Physical rider (7% vs camp monsters).
        if (ChainAllows(Hit,TEXT("tonnage_crush")) && Catalog())
        {
            const FLDDemoAbilityDefinition* AbilDef=nullptr;
            FString LookupErr;
            if (Catalog()->FindAbilityByName(Hit.Source.DefinitionId,AbilDef,LookupErr) && AbilDef
                && AbilDef->Target.Mode==ELDDemoTargetMode::Self && AbilDef->HitType==ELDDemoHitType::Area)
            {
                TArray<FLDPassiveEffectRef> Tonnage;
                if (!CollectPassiveEffects(Source->AttributeProjection,ELDDemoEffectOp::TonnageCrush,Tonnage,Error)) { return false; }
                if (!Tonnage.IsEmpty() && BonusMaxHpP2Of(*Source)>0)
                {
                    const bool bVsMonster=Target.Victim.Kind==ELDVictimKind::CampMember;
                    int64 RatioBp=0;
                    for (const FLDPassiveEffectRef& Ref : Tonnage) { RatioBp+=bVsMonster ? Ref.Effect.SecondaryRatioBp : Ref.Effect.RatioBp; }
                    const int64 CrushP2=Share(BonusMaxHpP2Of(*Source),RatioBp);
                    if (CrushP2>0)
                    {
                        FLDCombatHit Crush=MakeHit(*Source,Tonnage[0].Source,Hit.Target,Hit.EffectId);
                        Crush.BaseP2=CrushP2;
                        Crush.DamageType=ELDDemoDamageType::Physical;
                        Crush.bRider=true;
                        PendingHits.Add(MoveTemp(Crush));
                        LedgerCardTriggered(*Source,Tonnage[0].Key);
                    }
                }
            }
        }
    }
    // #345 card_martial_scholar: with >=ad_threshold BonusAD and >=ap_threshold AP, skill hits arm next basic (steal 8 momentum + 2x breach) and basic hits arm next skill (+22%).
    if (bNative || bBasic)
    {
        TArray<FLDPassiveEffectRef> Scholars;
        if (!CollectPassiveEffects(Source->AttributeProjection,ELDDemoEffectOp::MartialScholar,Scholars,Error)) { return false; }
        if (!Scholars.IsEmpty())
        {
            const FLDDemoEffectDefinition& Sch=Scholars[0].Effect;
            const int64 BonusADP2=LDTactical::BonusADOf(Parameters,GrowthLevelOf(*Source),Source->AttributeProjection.Final.ADP2);
            const int64 APP2=Source->AttributeProjection.Final.APP2;
            if (BonusADP2>=Sch.LimitP2 && APP2>=int64(Sch.Count)*100)
            {
                if (bBasic && Source->ScholarBasicUntilMs>NowMs && !Hit.bExtraStrike)
                {
                    Source->ScholarBasicUntilMs=0;
                    int64 StealTotalP2=0;
                    for (const FLDPassiveEffectRef& Ref : Scholars) { StealTotalP2+=Ref.Effect.BaseP2; }
                    if (Target.Victim.Kind==ELDVictimKind::Contender)
                    {
                        const int64 Taken=FMath::Min(FMath::Max<int64>(Target.Resources.MomentumP2,0),StealTotalP2);
                        if (Taken>0) { GrantMomentum(Target,-Taken,ELDMomentumBook::Spent); }
                        GrantMomentum(*Source,StealTotalP2,ELDMomentumBook::Basic);
                    }
                    else
                    {
                        GrantMomentum(*Source,StealTotalP2,ELDMomentumBook::Basic);
                    }
                    LedgerCardTriggered(*Source,Scholars[0].Key);
                }
                if (bNative)
                {
                    if (!LDMatchLifecycle::AddDurationMs(NowMs,Sch.DurationMs,Source->ScholarBasicUntilMs,Error)) { return false; }
                }
                if (bBasic && !Hit.bExtraStrike)
                {
                    if (!LDMatchLifecycle::AddDurationMs(NowMs,Sch.DurationMs,Source->ScholarSkillUntilMs,Error)) { return false; }
                    LedgerCardTriggered(*Source,Scholars[0].Key);
                }
            }
        }
    }
    if (bBasic)
    {
        const bool bThird=Hit.ComboStep==3;
        TArray<FLDPassiveEffectRef> Breaths, Thinks, Solids;
        if (!CardsOn(Source->AttributeProjection,ELDDemoEffectOp::ComboMomentum,Basic,Breaths,Error)
            || !CardsOn(Source->AttributeProjection,ELDDemoEffectOp::BasicCooldownCut,Basic,Thinks,Error)
            || !CardsOn(Source->AttributeProjection,ELDDemoEffectOp::ComboSkillHit,Basic,Solids,Error))
        { return false; }
        // A01: the third step gives momentum (each copy).
        for (const FLDPassiveEffectRef& Ref : Breaths)
        {
            if (bThird && GrantMomentum(*Source,Ref.Effect.LimitP2,ELDMomentumBook::Basic)>0) { LedgerCardTriggered(*Source,Ref.Key); }
        }
        // A02: one random skill still cooling down is cut (each copy).
        if (!Thinks.IsEmpty())
        {
            if (FLDCombatEntityRecord* Record=FindRecord(Source->Victim.EntityId))
            {
                for (const FLDPassiveEffectRef& Ref : Thinks)
                {
                    TArray<FLDCombatTrack*> Cooling;
                    for (FLDCombatTrack& Track : Record->Tracks)
                    {
                        if (Track.Kind==ELDCombatSourceKind::Learned && Track.ReadyAtMs>NowMs) { Cooling.Add(&Track); }
                    }
                    if (Cooling.IsEmpty()) { continue; }
                    FLDCombatTrack& Picked=*Cooling[CritRandom.RandRange(0,Cooling.Num()-1)];
                    Picked.ReadyAtMs=LDCombatSourceRules::ReduceReadyAt(Picked.ReadyAtMs,NowMs,Ref.Effect.AmountMs);
                    MarkViewChanged(Source->Victim.EntityId);
                    LedgerCardTriggered(*Source,Ref.Key);
                }
            }
        }
        // A03: the third step is one skill hit of every learned skill for their cards, plus the skill-hit momentum and Y16.
        if (bThird && !Hit.bExtraStrike)
        {
            for (const FLDPassiveEffectRef& Ref : Solids)
            {
                GrantMomentum(*Source,Parameters.Momentum.SkillHitP2,ELDMomentumBook::Skill);
                if (const FLDCombatEntityRecord* Record=FindRecord(Source->Victim.EntityId))
                {
                    for (const FLDLearnedActiveValue& Learned : Record->Build.Actives)
                    {
                        if (!SkillHit({ELDCombatSourceKind::Learned,Learned.AbilityId})) { return false; }
                    }
                }
                if (!BrainMomentum()) { return false; }
                LedgerCardTriggered(*Source,Ref.Key);
            }
        }
        // Strength 18-pt keystone: basic crit or Combo 3 on an enemy contender deals 4% of target current HP Physical damage
        // (1.5 s per-target internal cooldown).
        if (!Hit.bExtraStrike && Source->AttributeProjection.Keystones.bStrength
            && Hit.Target.Victim.Kind==ELDVictimKind::Contender && !Target.bTrainingDummy && (Hit.bCritical || bThird))
        {
            const int64 ReadyAt=Source->KeystoneStrengthTargetReadyAtMs.FindRef(Hit.Target.Victim.EntityId);
            if (NowMs>=ReadyAt)
            {
                Source->KeystoneStrengthTargetReadyAtMs.Add(Hit.Target.Victim.EntityId,NowMs+1500);
                const int64 CurrentHPP2=Target.Resources.HpP2+Damage.HPLostP2;
                const int64 BonusP2=CurrentHPP2/10000*400+CurrentHPP2%10000*400/10000;
                if (BonusP2>0)
                {
                    FLDCombatHit Heavy=MakeHit(*Source,{ELDCombatSourceKind::Basic,NAME_None},Hit.Target,Hit.EffectId);
                    Heavy.BaseP2=BonusP2;
                    Heavy.DamageType=ELDDemoDamageType::Physical;
                    Heavy.bRider=true;
                    PendingHits.Add(MoveTemp(Heavy));
                }
            }
        }
        // A13: primary basic hits grant 2 progress on contenders and 1 progress on monsters toward match growth (an A09
        // strike is not a new attack of that growth).
        if (!Target.bTrainingDummy && !Hit.bAttackSpeedStrike
            && (Hit.Target.Victim.Kind==ELDVictimKind::Contender || Hit.Target.Victim.Kind==ELDVictimKind::CampMember))
        {
            TArray<FLDPassiveEffectRef> Practice;
            if (!CollectPassiveEffects(Source->AttributeProjection,ELDDemoEffectOp::HitGrowth,Practice,Error)) { return false; }
            if (Practice.ContainsByPredicate([](const FLDPassiveEffectRef& Ref) { return Ref.Effect.Trigger==ELDDemoEffectTrigger::BasicHit; }))
            {
                if (FLDCombatEntityRecord* Record=FindRecord(Source->Victim.EntityId))
                {
                    Record->CardGrowth.BasicHits+=(Hit.Target.Victim.Kind==ELDVictimKind::Contender) ? 2 : 1;
                    if (!RefreshCardGrowth(Source->Victim.EntityId,NowMs,Error)) { return false; }
                }
            }
        }
        // A12: consecutive hits on the same life stack attack speed; another target or a pause restarts it. An A09 strike
        // neither stacks nor keeps it: it is converted from that attack speed.
        TArray<FLDPassiveEffectRef> Focus;
        if (!CardsOn(Source->AttributeProjection,ELDDemoEffectOp::FocusAttackSpeed,Basic,Focus,Error)) { return false; }
        if (!Focus.IsEmpty() && !Hit.bAttackSpeedStrike)
        {
            const bool bSame=Source->FocusTarget==Hit.Target && NowMs<=Source->FocusUntilMs;
            Source->FocusTarget=Hit.Target;
            Source->FocusHits=bSame ? Source->FocusHits+1 : 1;
            if (!LDMatchLifecycle::AddDurationMs(NowMs,Focus[0].Effect.DurationMs,Source->FocusUntilMs,Error)) { return false; }
            if (!RefreshTemporary(*Source,ActorOf(*Source),NowMs,Error)) { return false; }
        }
    }
    // Y24: every direct hit records a mark toward the tally.
    if (!Hit.bReflect && !Hit.bBurnTick && !Hit.bPeriodic && !Hit.bStatusSettle && !Hit.bRider && !Hit.bFromShieldTrigger
        && !OnTallyHit(*Source,Hit,NowMs,Error)) { return false; }
    // K13: a physical rider of the host skill's native hit (copies add their AD ratio), sharing its crit and factors.
    if (bNative && ChainAllows(Hit,TEXT("skill_rider")))
    {
        TArray<FLDPassiveEffectRef> Lends;
        if (!CardsOn(Source->AttributeProjection,ELDDemoEffectOp::SkillAdRider,Hit.Source,Lends,Error)) { return false; }
        int64 ADBp=0;
        for (const FLDPassiveEffectRef& Ref : Lends) { ADBp+=Ref.Effect.ADRatioBp; }
        if (ADBp>0)
        {
            FLDCombatHit Rider=MakeHit(*Source,Lends[0].Source,Hit.Target,Hit.EffectId);
            Rider.ADRatioBp=ADBp;
            Rider.DamageType=ELDDemoDamageType::Physical;
            Rider.bCritical=Hit.bCritical;
            Rider.Factors=Hit.Factors;
            if (Hit.CastLedger) { Rider.Factors.Append(Hit.CastLedger->Factors); }
            Rider.bRider=true;
            PendingHits.Add(MoveTemp(Rider));
            LedgerCardTriggered(*Source,Lends[0].Key);
        }
    }
    return true;
}

bool ALDCombatDirector::OnCardReaction(const FLDCombatReactionFact& Fact, int64 NowMs, FString& Error)
{
    using namespace LDCombatCardsDetail;
    FLDBodyCombatState* Trigger=FindBody(Fact.TriggerBody);
    if (!Trigger || Trigger->bPendingDeath) { return true; }
    // Y04: the trigger gains momentum (each copy).
    TArray<FLDPassiveEffectRef> Refunds, Waves, Reloads;
    if (!CollectPassiveEffects(Trigger->AttributeProjection,ELDDemoEffectOp::ReactionMomentum,Refunds,Error)
        || !CollectPassiveEffects(Trigger->AttributeProjection,ELDDemoEffectOp::ReactionElement,Waves,Error)
        || !CardsOn(Trigger->AttributeProjection,ELDDemoEffectOp::ReactionHaste,Fact.TriggerSource,Reloads,Error))
    { return false; }
    for (const FLDPassiveEffectRef& Ref : Refunds)
    {
        if (GrantMomentum(*Trigger,Ref.Effect.LimitP2,ELDMomentumBook::Reaction)>0) { LedgerCardTriggered(*Trigger,Ref.Key); }
    }
    // K25: the host skill that triggered it cuts its own remaining cooldown (once per paid release and copy).
    if (!(Fact.CastLedger && Fact.CastLedger->bAutoRecast)
        && Fact.TriggerSource.Kind==ELDCombatSourceKind::Learned && Fact.TriggerEffectId.IsValid() && !Reloads.IsEmpty())
    {
        if (FLDCombatEntityRecord* Record=FindRecord(Trigger->Victim.EntityId))
        {
            for (const FLDPassiveEffectRef& Haste : Reloads)
            {
                if (Trigger->EffectBudgets.ContainsByPredicate([&](const FLDCombatEffectBudget& Row)
                    { return Row.EffectId==Fact.TriggerEffectId && Row.Card==Haste.Key && Row.Serial==Haste.Serial; }))
                { continue; }
                Trigger->EffectBudgets.Add({Fact.TriggerEffectId,Haste.Key,Haste.Serial,NowMs});
                FLDCombatTrack* Track=Record->Tracks.FindByPredicate([&Fact](const FLDCombatTrack& Row)
                { return Row.Kind==ELDCombatSourceKind::Learned && Row.DefinitionId==Fact.TriggerSource.DefinitionId; });
                if (!Track || Track->ReadyAtMs<=NowMs) { continue; }
                Track->ReadyAtMs=LDCombatSourceRules::ReduceReadyAt(Track->ReadyAtMs,NowMs,Haste.Effect.AmountMs);
                MarkViewChanged(Trigger->Victim.EntityId);
                LedgerCardTriggered(*Trigger,Haste.Key);
            }
        }
    }
    // Y05: the primary target takes a random element other than the one just consumed (each copy one more). Its secondary
    // reaction in the same rule chain is exempted once from the mother reaction's 0.5 s lock (#330 §5.2.8).
    FLDBodyCombatState* Primary=FindBody(Fact.Primary);
    if (Waves.IsEmpty() || !Primary || Primary->bPendingDeath) { return true; }
    const FGuid ChainKey=CurrentChain.IsValid() ? CurrentChain
        : Fact.TriggerEffectId.IsValid() ? Fact.TriggerEffectId
        : Fact.HitId.IsValid() ? Fact.HitId : FGuid::NewGuid();
    TGuardValue<FGuid> ChainScope(CurrentChain,ChainKey);
    for (const FLDPassiveEffectRef& Wave : Waves)
    {
        const bool bExemptAvailable=ChainKey.IsValid() && !WaveAfterExemptedChains.Contains(ChainKey);
        Primary->WaveAfterExemptChainId=bExemptAvailable ? ChainKey : FGuid();
        TArray<ELDCombatElement> Choices{ELDCombatElement::Fire,ELDCombatElement::Water,ELDCombatElement::Shock,
            ELDCombatElement::Chill,ELDCombatElement::Rot};
        Choices.Remove(Fact.Consumed);
        TArray<ELDCombatElement> Pool=Choices;
        if (!bExemptAvailable)
        {
            const ELDCombatElement Still=ElementOfBody(*Primary,NowMs);
            TArray<ELDCombatElement> Lands=Choices.FilterByPredicate([Still](ELDCombatElement Candidate)
                { return LDCombatElements::ReactionOf(Still,Candidate)==ELDCombatReaction::None; });
            if (!Lands.IsEmpty()) { Pool=MoveTemp(Lands); }
        }
        const ELDCombatElement Kind=Pool[CritRandom.RandRange(0,Pool.Num()-1)];
        FLDElementApplier Applier;
        Applier.SourceEntity=Trigger->Victim.EntityId;
        Applier.SourceBody=Fact.TriggerBody;
        Applier.CreditOwner=Trigger->Victim.Kind==ELDVictimKind::Contender ? Trigger->Victim.EntityId : FGuid();
        Applier.Source=Wave.Source;
        Applier.Level=GrowthLevelOf(*Trigger);
        Applier.SourceAPP2=FMath::Max<int64>(Trigger->AttributeProjection.Final.APP2,0);
        Applier.CastLedger=Fact.CastLedger;
        if (!ApplyElement(*Primary,Kind,Applier,Kind==ELDCombatElement::Fire ? 1 : 0,FGuid::NewGuid(),NowMs,Error)) { return false; }
        if (Primary->WaveAfterExemptChainId==ChainKey) { Primary->WaveAfterExemptChainId.Invalidate(); }
        LedgerCardTriggered(*Trigger,Wave.Key);
    }
    return true;
}

bool ALDCombatDirector::OnCardTakedown(const FGuid& Entity, bool bKill, const FLDBodyCombatState& Victim, int64 WindowMs, int64 NowMs,
    FString& Error)
{
    FLDCombatEntityRecord* Record=FindRecord(Entity);
    if (!Record) { return true; }
    FLDBodyCombatState* Body=nullptr;
    for (auto& Pair : Bodies)
    {
        if (Pair.Value.Victim.EntityId==Entity && !Pair.Value.bPendingDeath) { Body=&Pair.Value; }
    }
    // Y20 counts every kill and assist of the match, alive or not.
    TArray<FLDPassiveEffectRef> Streaks, Resets;
    if (!CollectPassiveEffects(Record->Projection,ELDDemoEffectOp::KillGrowth,Streaks,Error)
        || !CollectPassiveEffects(Record->Projection,ELDDemoEffectOp::TakedownReset,Resets,Error))
    { return false; }
    if (!Streaks.IsEmpty())
    {
        ++(bKill ? Record->CardGrowth.Kills : Record->CardGrowth.Assists);
        if (!RefreshCardGrowth(Entity,NowMs,Error)) { return false; }
    }
    // Y02: every skill cooldown refreshed and the bar filled (a living body only).
    if (!Resets.IsEmpty() && Body)
    {
        for (FLDCombatTrack& Track : Record->Tracks)
        {
            if (Track.Kind==ELDCombatSourceKind::Learned && Track.ReadyAtMs>NowMs) { Track.ReadyAtMs=NowMs; }
        }
        const FLDDemoContentSnapshot* Content=Catalog();
        if (Content) { GrantMomentum(*Body,Content->Get().Parameters.Momentum.MaxP2,ELDMomentumBook::Other); }
        MarkViewChanged(Entity);
        LedgerCardTriggered(*Body,Resets[0].Key);
        UE_LOG(LogLDCombat,Log,TEXT("LD_COMBAT_TAKEDOWN_RESET t=%lld holder=%s kill=%d"),NowMs,*LDLog::Id(Entity),int32(bKill));
    }
    // K03 (a skill card's takedown_reset): its host is refreshed when its native hit reached the victim within the window.
    if (Body)
    {
        TArray<FLDPassiveEffectRef> Hosts;
        if (!CollectPassiveEffects(Record->Projection,ELDDemoEffectOp::TakedownReset,Hosts,Error)) { return false; }
        for (const FLDPassiveEffectRef& Ref : Hosts)
        {
            if (Ref.TargetKind!=ELDCardTargetKind::Skill) { continue; }
            const bool bHostHit=Victim.SkillContributions.ContainsByPredicate([&](const FLDSkillContribution& Row)
            { return Row.Contributor==Entity && Row.Skill==Ref.Active && Row.AtMs<=NowMs && NowMs-Row.AtMs<=WindowMs; });
            FLDCombatTrack* Track=Record->Tracks.FindByPredicate([&Ref](const FLDCombatTrack& Row)
            { return Row.Kind==ELDCombatSourceKind::Learned && Row.DefinitionId==Ref.Active; });
            if (!bHostHit || !Track || Track->ReadyAtMs<=NowMs) { continue; }
            Track->ReadyAtMs=NowMs;
            MarkViewChanged(Entity);
            LedgerCardTriggered(*Body,Ref.Key);
            UE_LOG(LogLDCombat,Log,TEXT("LD_COMBAT_HOST_REFRESH t=%lld holder=%s host=%s kill=%d"),NowMs,*LDLog::Id(Entity),
                *Ref.Active.ToString(),int32(bKill));
        }
        // #345 active_flurry_rush 3B2: contender takedown extends active frenzy and refills momentum.
        if (Body->ActiveFrenzy.IsSet() && NowMs<Body->FrenzyUntilMs)
        {
            const FLDDemoEffectDefinition& Frenzy=Body->ActiveFrenzy.GetValue();
            if (Frenzy.ExtraDurationMs>0)
            {
                if (!LDMatchLifecycle::AddDurationMs(Body->FrenzyUntilMs,Frenzy.ExtraDurationMs,Body->FrenzyUntilMs,Error)) { return false; }
                for (FLDCombatTimedBonus& Row : Body->TimedBonuses)
                {
                    if (Row.EffectId==Body->FrenzyEffectId) { Row.ExpiresAtMs=Body->FrenzyUntilMs; }
                }
            }
            if (Frenzy.LimitP2>0) { GrantMomentum(*Body,Frenzy.LimitP2,ELDMomentumBook::Skill); }
        }
    }
    return true;
}

bool ALDCombatDirector::OnHardControl(FLDBodyCombatState& Victim, const FLDControlApplication& App, int64 NowMs, FString& Error)
{
    // K05: a hard control its host landed on another body arms the holder's next skill (copies add their bonus).
    if (App.SourceBody.Victim.EntityId!=Victim.Victim.EntityId && App.Source.Kind==ELDCombatSourceKind::Learned)
    {
        if (FLDBodyCombatState* Holder=FindBody(App.SourceBody); Holder && !Holder->bPendingDeath)
        {
            TArray<FLDPassiveEffectRef> Amps;
            if (!CardsOn(Holder->AttributeProjection,ELDDemoEffectOp::ControlAmp,App.Source,Amps,Error)) { return false; }
            if (!Amps.IsEmpty())
            {
                int64 Bp=0;
                for (const FLDPassiveEffectRef& Ref : Amps) { Bp+=Ref.Effect.RatioBp; }
                Holder->ControlAmpCard=Amps[0].Key;
                Holder->ControlAmpBp=Bp;
                if (!LDMatchLifecycle::AddDurationMs(NowMs,Amps[0].Effect.DurationMs,Holder->ControlAmpUntilMs,Error)) { return false; }
                LedgerCardTriggered(*Holder,Amps[0].Key);
            }
        }
    }
    // Y21: a hard control from another body that actually landed on a contender grows its resistances for the match.
    if (Victim.Victim.Kind!=ELDVictimKind::Contender || App.SourceBody.Victim.EntityId==Victim.Victim.EntityId) { return true; }
    FLDCombatEntityRecord* Record=FindRecord(Victim.Victim.EntityId);
    if (!Record) { return true; }
    TArray<FLDPassiveEffectRef> Tough;
    if (!CollectPassiveEffects(Record->Projection,ELDDemoEffectOp::ControlGrowth,Tough,Error)) { return false; }
    if (Tough.IsEmpty()) { return true; }
    ++Record->CardGrowth.ControlsTaken;
    LedgerCardTriggered(Victim,Tough[0].Key);
    return RefreshCardGrowth(Victim.Victim.EntityId,NowMs,Error);
}

bool ALDCombatDirector::CardGrowthTerms(const FLDAttributeProjection& Projection, const FLDCardGrowth& Counters,
    TArray<FLDDemoStatBonus>& Growth, FString& Error) const
{
    Growth.Reset();
    TArray<FLDPassiveEffectRef> Fat, Streak, Tough, Counted;
    if (!CollectPassiveEffects(Projection,ELDDemoEffectOp::HealGrowth,Fat,Error)
        || !CollectPassiveEffects(Projection,ELDDemoEffectOp::KillGrowth,Streak,Error)
        || !CollectPassiveEffects(Projection,ELDDemoEffectOp::ControlGrowth,Tough,Error)
        || !CollectPassiveEffects(Projection,ELDDemoEffectOp::HitGrowth,Counted,Error))
    { return false; }
    TMap<ELDDemoBonusStat,int64> Totals;
    // Y19: every full heal threshold of the match adds its max HP (each copy).
    for (const FLDPassiveEffectRef& Ref : Fat)
    {
        if (Ref.Effect.BaseP2>0) { Totals.FindOrAdd(ELDDemoBonusStat::MaxHPFlat)+=Counters.HealedP2/Ref.Effect.BaseP2*Ref.Effect.LimitP2; }
    }
    // Y20: kills add the full stats, assists the card's share (each copy).
    for (const FLDPassiveEffectRef& Ref : Streak)
    {
        for (const FLDDemoStatBonus& Stat : Ref.Effect.Stats)
        {
            const int64 Assist=LDCombatCardsDetail::Share(Stat.Amount,Ref.Effect.RatioBp);
            Totals.FindOrAdd(Stat.Stat)+=Stat.Amount*Counters.Kills+Assist*Counters.Assists;
        }
    }
    // Y21: every hard control taken adds the resistances (each copy).
    for (const FLDPassiveEffectRef& Ref : Tough)
    {
        for (const FLDDemoStatBonus& Stat : Ref.Effect.Stats) { Totals.FindOrAdd(Stat.Stat)+=Stat.Amount*Counters.ControlsTaken; }
    }
    // A13: every full count of basic hits on contenders adds the card's stats (each copy).
    for (const FLDPassiveEffectRef& Ref : Counted)
    {
        const int32 Events=Ref.Effect.Trigger==ELDDemoEffectTrigger::BasicHit ? Counters.BasicHits : 0;
        const int32 Steps=Ref.Effect.Count>0 ? Events/Ref.Effect.Count : 0;
        for (const FLDDemoStatBonus& Stat : Ref.Effect.Stats) { Totals.FindOrAdd(Stat.Stat)+=Stat.Amount*Steps; }
    }
    for (const TPair<ELDDemoBonusStat,int64>& Pair : Totals)
    {
        if (Pair.Value<=0) { continue; }
        FLDDemoStatBonus& Stat=Growth.AddDefaulted_GetRef();
        Stat.Stat=Pair.Key;
        Stat.Amount=Pair.Value;
    }
    Growth.Sort([](const FLDDemoStatBonus& A, const FLDDemoStatBonus& B) { return uint8(A.Stat)<uint8(B.Stat); });
    return true;
}

bool ALDCombatDirector::RefreshCardGrowth(const FGuid& Entity, int64 NowMs, FString& Error)
{
    FLDCombatEntityRecord* Record=FindRecord(Entity);
    if (!Record) { return true; }
    TArray<FLDDemoStatBonus> Growth;
    if (!CardGrowthTerms(Record->Projection,Record->CardGrowth,Growth,Error)) { return false; }
    const bool bSame=Growth.Num()==Record->Build.CardGrowth.Num() && Algo::Compare(Growth,Record->Build.CardGrowth,
        [](const FLDDemoStatBonus& A, const FLDDemoStatBonus& B) { return A.Stat==B.Stat && A.Amount==B.Amount; });
    if (bSame) { return true; }
    return InstallCardGrowth(Entity,MoveTemp(Growth),NowMs,Error);
}

bool ALDCombatDirector::InstallCardGrowth(const FGuid& Entity, TArray<FLDDemoStatBonus>&& Growth, int64 NowMs, FString& Error)
{
    FLDCombatEntityRecord* Record=FindRecord(Entity);
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!Record || !Content) { Error=TEXT("Card growth needs the contender record and the approved Demo content."); return false; }
    FLDBuildProjectionInput Input=Record->Build;
    Input.CardGrowth=MoveTemp(Growth);
    FLDAttributeProjection Projection;
    if (!ProjectBuild(Input,Projection,Error)) { return false; }
    ALDCharacter* Actor=nullptr;
    FLDBodyCombatState* Body=nullptr;
    for (auto& Pair : Bodies)
    {
        if (Pair.Value.Victim.EntityId==Entity && !Pair.Value.bPendingDeath) { Body=&Pair.Value; Actor=Pair.Key.Get(); }
    }
    if (Body)
    {
        // One final-maximum conversion of HP, like a build change.
        FLDResourceMaxChangeInput HpChange;
        HpChange.Pool=ELDResourcePool::HP;
        HpChange.OldCurrentP2=Body->Resources.HpP2;
        HpChange.OldMaxP2=Body->Resources.MaxHpP2;
        HpChange.NewMaxP2=Projection.Final.MaxHPP2;
        const FLDResourceMaxChangeResult Hp=LDAttributeProjection::ResourceMaxChange(HpChange,*Content);
        if (!Hp.IsSuccess()) { Error=FString::Printf(TEXT("Card growth HP change refused: %s"),*Hp.Error.Message); return false; }
        Body->Resources.MaxHpP2=Projection.Final.MaxHPP2;
        Body->Resources.HpP2=Hp.Value->NewCurrentP2;
        ++Body->Resources.Revision;
        if (!InstallBodyProjection(*Body,Actor,Projection,NowMs,Error)) { return false; }
    }
    Record->Build=MoveTemp(Input);
    Record->Projection=MoveTemp(Projection);
    MarkViewChanged(Entity);
    UE_LOG(LogLDCombat,Log,TEXT("LD_COMBAT_CARD_GROWTH t=%lld holder=%s healed=%s kills=%d assists=%d controls=%d"),NowMs,*LDLog::Id(Entity),
        *LDLog::P2(Record->CardGrowth.HealedP2),Record->CardGrowth.Kills,Record->CardGrowth.Assists,Record->CardGrowth.ControlsTaken);
    return true;
}

bool ALDCombatDirector::LastStandActive(FLDBodyCombatState& Body, int64 NowMs, FString& Error)
{
    if (Body.bLastStandUsed || Body.bPendingDeath || Body.Resources.MaxHpP2<=0) { return true; }
    TArray<FLDPassiveEffectRef> Stands;
    if (!CollectPassiveEffects(Body.AttributeProjection,ELDDemoEffectOp::LastStand,Stands,Error)) { return false; }
    if (Stands.IsEmpty()) { return true; }
    const FLDDemoEffectDefinition& Stand=Stands[0].Effect;
    if (Body.Resources.HpP2*10000>=Body.Resources.MaxHpP2*Stand.RatioBp) { return true; }
    // Copies add their stand time.
    int64 DurationMs=0;
    for (const FLDPassiveEffectRef& Ref : Stands) { DurationMs+=Ref.Effect.DurationMs; }
    Body.bLastStandUsed=true;
    if (!LDMatchLifecycle::AddDurationMs(NowMs,DurationMs,Body.LastStandUntilMs,Error)) { return false; }
    LedgerCardTriggered(Body,Stands[0].Key);
    ++Body.StateRevision;
    MarkViewChanged(Body.Victim.EntityId);
    UE_LOG(LogLDCombat,Log,TEXT("LD_COMBAT_LAST_STAND t=%lld body=%s until=%lld"),NowMs,*LDLog::Id(Body.Victim.EntityId),Body.LastStandUntilMs);
    return true;
}

bool ALDCombatDirector::OverhealShield(FLDBodyCombatState& Body, int64 OverflowP2, int64 NowMs, FString& Error)
{
    if (OverflowP2<=0 || Body.bPendingDeath) { return true; }
    TArray<FLDPassiveEffectRef> Fed;
    if (!CollectPassiveEffects(Body.AttributeProjection,ELDDemoEffectOp::OverhealShield,Fed,Error)) { return false; }
    // Y29 shares the op with a burn share and no cap; only the heal caps shield overflowing heals.
    Fed.RemoveAll([](const FLDPassiveEffectRef& Ref) { return Ref.Effect.MultiplierBp<=0; });
    if (Fed.IsEmpty()) { return true; }
    // Copies add their cap; total active shield from this source is strictly capped at CapBp of MaxHP (refreshing expiry).
    int64 CapBp=0;
    for (const FLDPassiveEffectRef& Ref : Fed) { CapBp+=Ref.Effect.MultiplierBp; }
    const int64 CapP2=LDCombatCardsDetail::Share(Body.Resources.MaxHpP2,CapBp);
    if (CapP2<=0) { return true; }
    if (Body.Shield.IsSet() && Body.Shield->ExpiresAtMs<=NowMs)
    {
        Body.Shield.Reset();
        Body.Resources.ShieldP2=0;
        Body.ShieldLayers.Reset();
    }
    else
    {
        ExpireShieldLayers(Body,NowMs);
    }
    const FLDCombatSourceRef& GrantSource=Fed[0].Source;
    const int64 DurationMs=Fed[0].Effect.DurationMs;
    int64 NewExpires=0;
    if (!LDMatchLifecycle::AddDurationMs(NowMs,DurationMs,NewExpires,Error)) { return false; }
    int64 ExistingSameSourceP2=0;
    FLDBodyCombatState::FShieldLayer* PrimaryLayer=nullptr;
    for (int32 Idx=Body.ShieldLayers.Num()-1; Idx>=0; --Idx)
    {
        FLDBodyCombatState::FShieldLayer& Layer=Body.ShieldLayers[Idx];
        if (Layer.ExpiresAtMs<=NowMs || Layer.AmountP2<=0 || Layer.Source!=GrantSource) { continue; }
        ExistingSameSourceP2+=Layer.AmountP2;
        if (!PrimaryLayer) { PrimaryLayer=&Layer; }
    }
    const int64 RoomP2=FMath::Max<int64>(0,CapP2-ExistingSameSourceP2);
    const int64 RawAddP2=FMath::Min(OverflowP2,RoomP2);
    if (!PrimaryLayer)
    {
        if (RawAddP2<=0) { return true; }
        if (!GrantShieldTo(Body,RawAddP2,DurationMs,FGuid::NewGuid(),Body.Victim.EntityId,NowMs,Error,GrantSource)) { return false; }
        LedgerCardTriggered(Body,Fed[0].Key);
        return true;
    }
    // Refresh all active layers from this source and top up only the remaining difference under CapP2.
    for (FLDBodyCombatState::FShieldLayer& Layer : Body.ShieldLayers)
    {
        if (Layer.ExpiresAtMs>NowMs && Layer.AmountP2>0 && Layer.Source==GrantSource)
        {
            Layer.ExpiresAtMs=FMath::Max(Layer.ExpiresAtMs,NewExpires);
        }
    }
    if (Body.Shield.IsSet())
    {
        Body.Shield->ExpiresAtMs=FMath::Max(Body.Shield->ExpiresAtMs,NewExpires);
    }
    const int64 EffectiveAddP2=RawAddP2>0 ? LDCombatElements::AfterRotP2(RawAddP2,Body.ElementMark,NowMs) : 0;
    if (EffectiveAddP2>0)
    {
        PrimaryLayer->AmountP2+=EffectiveAddP2;
        Body.Resources.ShieldP2+=EffectiveAddP2;
        Body.ShieldGrantedP2=Body.Resources.ShieldP2;
        Body.LastShieldGrantP2=EffectiveAddP2;
        LedgerShieldGranted(Body,EffectiveAddP2,GrantSource);
    }
    ++Body.Resources.Revision;
    ++Body.StateRevision;
    MarkViewChanged(Body.Victim.EntityId);
    LedgerCardTriggered(Body,Fed[0].Key);
    return true;
}

bool ALDCombatDirector::MakeBasicStrike(const FLDBodyCombatState& Body, const FLDCombatBodyRef& Target, FLDCombatHit& Out, FString& Error)
{
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!Content || Body.Victim.Kind!=ELDVictimKind::Contender) { Error=TEXT("A card's basic strike requires a contender and the Demo content."); return false; }
    const auto& Final=Body.AttributeProjection.Final;
    Out=FLDCombatHit();
    Out.HitId=FGuid::NewGuid();
    Out.EffectId=FGuid::NewGuid();
    Out.Target=Target;
    Out.SourceBody={Body.Victim,Body.Identity};
    Out.SourceEntity=Body.Victim.EntityId;
    Out.CreditOwner=Body.Victim.EntityId;
    Out.Source.Kind=ELDCombatSourceKind::Basic;
    Out.HitClass=ELDCombatHitClass::BasicPrimary;
    Out.DamageType=ELDDemoDamageType::Physical;
    Out.ADRatioBp=Content->Get().Character.BasicADRatioBp;
    Out.SourceADP2=Final.ADP2;
    Out.SourceAPP2=Final.APP2;
    Out.bExtraStrike=true;
    if (!StampSourceTerms(Body,Out,Error) || !SnapshotOnHit(Body,10000,Out,Error)) { return false; }
    Out.bCritical=Final.CritChanceBp>0 && CritRandom.RandRange(0,9999)<Final.CritChanceBp;
    return true;
}

// ---------------------------------------------------------------- K06 repeats

bool ALDCombatDirector::ScheduleAutoRecasts(const FLDBodyCombatState& Body, const FLDCombatSourceRef& Source,
    const FLDCombatActiveTerms& Terms, const FLDCombatAction& Action, int64 NowMs, FString& Error)
{
    if (Terms.AutoRecasts<=0 || Action.bAutoRecast || Source.Kind!=ELDCombatSourceKind::Learned) { return true; }
    for (int32 Index=1; Index<=Terms.AutoRecasts; ++Index)
    {
        FLDCombatAutoRecast& Repeat=AutoRecasts.AddDefaulted_GetRef();
        Repeat.Body={Body.Victim,Body.Identity};
        Repeat.Action=Action;
        Repeat.Action.bAutoRecast=true;
        // A repeat is derived: never empowered (EMPOWER rule E2).
        Repeat.Action.bEmpowered=false;
        Repeat.Action.EmpowerProcessor=ELDEmpowerProcessor::None;
        Repeat.Action.AutoRecastScaleBp=Terms.AutoRecastScaleBp;
        Repeat.Card=FName(TEXT("card_encore"));
        Repeat.Action.ChainId=CurrentChain;
        if (!LDMatchLifecycle::AddDurationMs(NowMs,Terms.AutoRecastDelayMs*Index,Repeat.DueMs,Error)) { return false; }
    }
    return true;
}

bool ALDCombatDirector::RunAutoRecasts(int64 NowMs, FString& Error)
{
    for (int32 Index=0; Index<AutoRecasts.Num();)
    {
        if (AutoRecasts[Index].DueMs>NowMs) { ++Index; continue; }
        const FLDCombatAutoRecast Repeat=AutoRecasts[Index];
        AutoRecasts.RemoveAt(Index);
        ALDCharacter* Actor=nullptr;
        FLDBodyCombatState* Body=FindBody(Repeat.Body,&Actor);
        if (!Body || !Actor || Body->bPendingDeath) { continue; }
        ELDCombatReject Reject=ELDCombatReject::None;
        LastReleaseChain=FGuid();
        if (!ReleaseAbility(*Actor,*Body,Repeat.Action,Reject,Error)) { return false; }
        UE_LOG(LogLDCombat,Log,TEXT("LD_COMBAT_AUTO_RECAST t=%lld src=%s active=%s share_bp=%lld result=%u"),NowMs,
            *LDLog::Id(Body->Victim.EntityId),*Repeat.Action.Source.DefinitionId.ToString(),Repeat.Action.AutoRecastScaleBp,uint32(Reject));
        if (Reject==ELDCombatReject::None)
        {
            // The repeat's card is a step of the chain its release joined (or opened).
            TGuardValue<FGuid> ChainScope(CurrentChain,LastReleaseChain);
            LedgerCardTriggered(*Body,Repeat.Card.IsNone() ? FName(TEXT("card_encore")) : Repeat.Card);
        }
    }
    return true;
}

// ---------------------------------------------------------------- issue #292 P5 cards

bool ALDCombatDirector::OnShieldBrokenCards(FLDBodyCombatState& Holder, int64 NowMs, FString& Error, int32 EmpowerSerial)
{
    using namespace LDCombatCardsDetail;
    if (Holder.bPendingDeath) { return true; }
    TArray<FLDPassiveEffectRef> Bursts;
    if (!CollectPassiveEffects(Holder.AttributeProjection,ELDDemoEffectOp::ShieldBurstBreak,Bursts,Error)) { return false; }
    const ALDCharacter* Actor=ActorOf(Holder);
    if (Bursts.IsEmpty() || !Actor) { return true; }
    // Y06: copies add their damage terms; the first copy's radius.
    int64 BaseP2=0, APBp=0, MaxHPBp=0;
    for (const FLDPassiveEffectRef& Ref : Bursts)
    {
        BaseP2+=Ref.Effect.BaseP2;
        APBp+=Ref.Effect.APRatioBp;
        MaxHPBp+=Ref.Effect.MaxHPRatioBp;
    }
    TArray<FLDCombatBodyRef> Around;
    if (!CollectAreaTargets(FootOf(*Actor),Cm(Bursts[0].Effect.RadiusCm),&Holder,Holder.Victim.EntityId,Around,Error)) { return false; }
    const FGuid BurstId=FGuid::NewGuid();
    for (const FLDCombatBodyRef& Other : Around)
    {
        FLDCombatHit Blast=MakeHit(Holder,Bursts[0].Source,Other,BurstId);
        Blast.BaseP2=BaseP2;
        Blast.APRatioBp=APBp;
        Blast.MaxHPRatioBp=MaxHPBp;
        Blast.SourceMaxHPP2=BonusMaxHpP2Of(Holder);
        Blast.DamageType=ELDDemoDamageType::Magical;
        Blast.bFromShieldTrigger=true;
        Blast.EmpowerSerial=EmpowerSerial;
        if (!StampSourceTerms(Holder,Blast,Error)) { return false; }
        PendingHits.Add(MoveTemp(Blast));
    }
    if (!Around.IsEmpty()) { LedgerCardTriggered(Holder,Bursts[0].Key); }
    return true;
}

bool ALDCombatDirector::OnBurnDealtCards(FLDBodyCombatState& Source, int64 BurnDamageP2, int64 NowMs, FString& Error)
{
    if (BurnDamageP2<=0 || Source.bPendingDeath) { return true; }
    TArray<FLDPassiveEffectRef> Guards;
    if (!CollectPassiveEffects(Source.AttributeProjection,ELDDemoEffectOp::OverhealShield,Guards,Error)) { return false; }
    Guards.RemoveAll([](const FLDPassiveEffectRef& Ref) { return Ref.Effect.RatioBp<=0; });
    if (Guards.IsEmpty()) { return true; }
    // Y29: copies add their share; the live shield of this card stays within its balance cap (copies add their cap).
    int64 ShareBp=0, CapBp=0;
    for (const FLDPassiveEffectRef& Ref : Guards) { ShareBp+=Ref.Effect.RatioBp; CapBp+=Ref.Effect.SecondaryRatioBp; }
    int64 Amount=LDCombatCardsDetail::Share(BurnDamageP2,ShareBp);
    ExpireShieldLayers(Source,NowMs);
    int64 HeldP2=0;
    for (const FLDBodyCombatState::FShieldLayer& Layer : Source.ShieldLayers)
    {
        if (Layer.ExpiresAtMs>NowMs && Layer.AmountP2>0 && Layer.Source==Guards[0].Source) { HeldP2+=Layer.AmountP2; }
    }
    Amount=FMath::Min(Amount,LDCombatCardsDetail::Share(Source.Resources.MaxHpP2,CapBp)-HeldP2);
    if (Amount<=0) { return true; }
    if (!GrantShieldTo(Source,Amount,Guards[0].Effect.DurationMs,FGuid::NewGuid(),Source.Victim.EntityId,NowMs,Error,Guards[0].Source))
    { return false; }
    LedgerCardTriggered(Source,Guards[0].Key);
    return true;
}

bool ALDCombatDirector::OnTallyHit(FLDBodyCombatState& Source, const FLDCombatHit& Hit, int64 NowMs, FString& Error)
{
    // A card's own hits (a cashed tally among them) never mark.
    if (Hit.Source.Kind==ELDCombatSourceKind::Card || Source.bPendingDeath) { return true; }
    TArray<FLDPassiveEffectRef> Tallies;
    if (!CollectPassiveEffects(Source.AttributeProjection,ELDDemoEffectOp::Tally,Tallies,Error)) { return false; }
    if (Tallies.IsEmpty()) { return true; }
    const FLDDemoEffectDefinition& First=Tallies[0].Effect;
    // Y24: a pause longer than the card's window clears the open marks; the mark that fills the count cashes every mark.
    if (Source.TallyMarks>0 && NowMs-Source.TallyAtMs>First.DurationMs) { Source.TallyMarks=0; }
    ++Source.TallyMarks;
    Source.TallyAtMs=NowMs;
    if (Source.TallyMarks<FMath::Max(First.Count,1)) { return true; }
    int64 BaseP2=0, APBp=0;
    for (const FLDPassiveEffectRef& Ref : Tallies)
    {
        BaseP2+=Ref.Effect.BaseP2;
        APBp+=Ref.Effect.APRatioBp;
    }
    const int64 Marks=Source.TallyMarks;
    Source.TallyMarks=0;
    if (!ChainAllows(Hit,TEXT("tally"))) { return true; }
    FLDCombatHit Cash=LDCombatCardsDetail::MakeHit(Source,Tallies[0].Source,Hit.Target,FGuid::NewGuid());
    Cash.BaseP2=BaseP2*Marks;
    Cash.APRatioBp=APBp*Marks;
    Cash.DamageType=ELDDemoDamageType::Magical;
    if (!StampSourceTerms(Source,Cash,Error)) { return false; }
    PendingHits.Add(MoveTemp(Cash));
    LedgerCardTriggered(Source,Tallies[0].Key);
    UE_LOG(LogLDCombat,Log,TEXT("LD_COMBAT_TALLY t=%lld src=%s marks=%lld target=%s"),NowMs,*LDLog::Id(Source.Victim.EntityId),Marks,
        *LDLog::Id(Hit.Target.Victim.EntityId));
    return true;
}

bool ALDCombatDirector::DashRamHit(FLDBodyCombatState& Body, const FLDCombatDashContact& Dash, const FLDCombatBodyRef& Target,
    FString& Error)
{
    using namespace LDCombatCardsDetail;
    // #345 card_runaway_train: all dash contacts add MoveSpeed * speed_ratio + DashDistance * distance_ratio Physical damage.
    {
        TArray<FLDPassiveEffectRef> Trains;
        if (!CollectPassiveEffects(Body.AttributeProjection,ELDDemoEffectOp::RunawayTrain,Trains,Error)) { return false; }
        if (!Trains.IsEmpty())
        {
            const double MoveSpeed=Cm(Body.AttributeProjection.Final.MoveSpeedCmPerSec);
            double DashDist=Dash.NativeHit.IsSet() ? Dash.NativeHit->SourceDashDistanceCm : 0.;
            if (DashDist<=0. && Catalog())
            {
                DashDist=Cm(Catalog()->Get().Character.Dash.Values.Effects.IsEmpty()
                    ? FLDDemoRational{450,1} : Catalog()->Get().Character.Dash.Values.Effects[0].DistanceCm);
            }
            int64 SpeedBp=0, DistBp=0;
            for (const FLDPassiveEffectRef& Ref : Trains) { SpeedBp+=Ref.Effect.RatioBp; DistBp+=Ref.Effect.SecondaryRatioBp; }
            const int64 TrainP2=FMath::RoundToInt64((MoveSpeed*double(SpeedBp)/10000.+DashDist*double(DistBp)/10000.)*100.);
            if (TrainP2>0)
            {
                FLDCombatHit TrainHit=MakeHit(Body,Trains[0].Source,Target,Dash.EffectId);
                TrainHit.BaseP2=TrainP2;
                TrainHit.DamageType=ELDDemoDamageType::Physical;
                TrainHit.bAddedDamage=true;
                if (!StampSourceTerms(Body,TrainHit,Error)) { return false; }
                PendingHits.Add(MoveTemp(TrainHit));
                LedgerCardTriggered(Body,Trains[0].Key);
            }
        }
    }
    if (Dash.Source.Kind!=ELDCombatSourceKind::Innate) { return true; }
    TArray<FLDPassiveEffectRef> Rams;
    if (!CardsOn(Body.AttributeProjection,ELDDemoEffectOp::DashRam,Dash.Source,Rams,Error)) { return false; }
    if (Rams.IsEmpty()) { return true; }
    // D07: copies add their damage terms; the first copy's push.
    FLDCombatHit Ram=MakeHit(Body,Rams[0].Source,Target,Dash.EffectId);
    for (const FLDPassiveEffectRef& Ref : Rams)
    {
        Ram.BaseP2+=Ref.Effect.BaseP2;
        Ram.ADRatioBp+=Ref.Effect.ADRatioBp;
    }
    Ram.DamageType=ELDDemoDamageType::Physical;
    Ram.bAddedDamage=true;
    Ram.KnockbackCm=Cm(Rams[0].Effect.DistanceCm);
    Ram.KnockbackMs=Rams[0].Effect.DurationMs;
    Ram.bKnockbackFromAnchor=false;
    Ram.KnockbackSource=Dash.Source;
    if (!StampSourceTerms(Body,Ram,Error)) { return false; }
    PendingHits.Add(MoveTemp(Ram));
    LedgerCardTriggered(Body,Rams[0].Key);
    return true;
}

bool ALDCombatDirector::QueueComboCast(FLDBodyCombatState& Body, const FLDCombatAction& Basic, const FLDCombatHit& Primary, int64 NowMs,
    FString& Error)
{
    if (Primary.ComboStep!=3 || Primary.bExtraStrike || Body.Victim.Kind!=ELDVictimKind::Contender) { return true; }
    TArray<FLDPassiveEffectRef> Casts;
    if (!CardsOn(Body.AttributeProjection,ELDDemoEffectOp::ComboCast,{ELDCombatSourceKind::Basic,NAME_None},Casts,Error)) { return false; }
    if (Casts.IsEmpty()) { return true; }
    const FLDCombatEntityRecord* Record=FindRecord(Body.Victim.EntityId);
    const FLDLearnedActiveValue* Q=Record ? Record->Build.Actives.FindByPredicate([](const FLDLearnedActiveValue& Row)
    { return Row.Entry==ELDCombatEntry::Q; }) : nullptr;
    const FLDDemoContentSnapshot* Content=Catalog();
    const FLDDemoAbilityDefinition* Ability=nullptr;
    FString LookupError;
    ALDCharacter* HolderActor=ActorOf(Body);
    ALDCharacter* TargetActor=nullptr;
    FindBody(Primary.Target,&TargetActor);
    // Without a Q skill or a body to aim at, the third step casts nothing.
    if (!Q || !Content || !Content->FindAbilityByName(Q->AbilityId,Ability,LookupError) || !Ability || !HolderActor || !TargetActor)
    { return true; }
    // A unit Q follows the basic's own target only: a flying basic that struck another body casts no unit Q.
    if (Ability->Target.Mode==ELDDemoTargetMode::Unit && Primary.Target!=Basic.TargetBody) { return true; }
    // A08: the Q skill releases at once toward the struck body at the card's share (copies add), costing nothing, issuing no
    // cooldown and never repeated (a K06-style free release).
    FLDCombatAction Cast;
    Cast.Kind=ELDCombatActionKind::Ability;
    Cast.Key=Basic.Key;
    Cast.Token=Basic.Token;
    Cast.Entry=ELDCombatEntry::Q;
    Cast.Source={ELDCombatSourceKind::Learned,Q->AbilityId};
    Cast.Target=Basic.Target;
    Cast.TargetBody=Primary.Target;
    const FVector HolderFoot=FootOf(*HolderActor), TargetFoot=FootOf(*TargetActor);
    Cast.Direction=(TargetFoot-HolderFoot).GetSafeNormal2D();
    Cast.TargetPoint=TargetFoot;
    switch (Ability->Target.Mode)
    {
    case ELDDemoTargetMode::Unit: break;
    case ELDDemoTargetMode::Self: Cast.Target.Kind=ELDCombatTargetKind::Self; break;
    case ELDDemoTargetMode::GroundPoint: Cast.Target.Kind=ELDCombatTargetKind::Point; Cast.Target.Point=TargetFoot; break;
    default: Cast.Target.Kind=ELDCombatTargetKind::Direction; Cast.Target.Direction=Cast.Direction; break;
    }
    Cast.StartedAtMs=NowMs;
    Cast.ReleaseAtMs=NowMs;
    Cast.SourceRevision=Record->SourceRevision;
    Cast.bAutoRecast=true;
    int64 ShareBp=0;
    for (const FLDPassiveEffectRef& Ref : Casts) { ShareBp+=Ref.Effect.MultiplierBp; }
    Cast.AutoRecastScaleBp=ShareBp;
    Cast.bWholeEffectShare=true;
    Cast.ChainId=CurrentChain;
    FLDCombatAutoRecast& Repeat=AutoRecasts.AddDefaulted_GetRef();
    Repeat.Body={Body.Victim,Body.Identity};
    Repeat.Action=MoveTemp(Cast);
    Repeat.DueMs=NowMs;
    Repeat.Card=Casts[0].Key;
    return true;
}

// ---------------------------------------------------------------- branch mechanisms shared with the old mods

bool ALDCombatDirector::ScheduleEcho(const FLDCombatBodyRef& OwnerRef, const FLDPassiveEffectRef& Echo,
    const FLDCombatGroundEffect& Blast, int64 NowMs, FString& Error)
{
    const FLDBodyCombatState* Holder=FindBody(OwnerRef);
    if (!Holder || Holder->bPendingDeath) { return true; }
    // A branch echo: the same release's hit at the same place and outline, once, at its share; it never echoes again.
    FLDCombatGroundEffect Repeat;
    Repeat.Payload=Blast.Payload;
    Repeat.Payload.DeliveryScaleBp=LDCombatCardsDetail::Share(Blast.Payload.DeliveryScaleBp,Echo.Effect.MultiplierBp);
    Repeat.CentreCm=Blast.CentreCm;
    Repeat.RadiusCm=Blast.RadiusCm;
    Repeat.InnerRadiusCm=Blast.InnerRadiusCm;
    Repeat.OuterRatioBp=Blast.OuterRatioBp;
    Repeat.Origin=ELDCombatGroundOrigin::Echo;
    Repeat.ObservedKey=FGuid::NewGuid();
    // Skill branches (S18 A1/A2): the echo's center hits harder and it shocks.
    if (Echo.Effect.RatioBp>0)
    {
        Repeat.Payload.DeliveryScaleBp=LDCombatCardsDetail::Share(Repeat.Payload.DeliveryScaleBp,Echo.Effect.RatioBp);
        Repeat.OuterRatioBp=FMath::Clamp<int64>(Repeat.OuterRatioBp*10000/Echo.Effect.RatioBp,1,10000);
    }
    if (Echo.Effect.Element!=ELDDemoElement::None && Echo.Effect.Element!=ELDDemoElement::Fire)
    { Repeat.Payload.NativeElements|=LDCombatElements::Bit(LDCombatCardsDetail::CombatElement(Echo.Effect.Element)); }
    if (Repeat.Payload.DeliveryScaleBp<=0 || !LDMatchLifecycle::AddDurationMs(NowMs,Echo.Effect.DelayMs,Repeat.ReadyAtMs,Error))
    {
        if (Error.IsEmpty()) { Error=TEXT("An echo needs a positive share."); }
        return false;
    }
    Repeat.ExpiresAtMs=Repeat.ReadyAtMs;
    GroundEffects.Add(MoveTemp(Repeat));
    UE_LOG(LogLDCombat,Log,TEXT("LD_COMBAT_ECHO t=%lld owner=%s exit=%s at=%lld share_bp=%lld radius=%.0f"),NowMs,
        *LDLog::Id(OwnerRef.Victim.EntityId),*LDLog::Source(Blast.Payload.Source),NowMs+Echo.Effect.DelayMs,Echo.Effect.MultiplierBp,
        Blast.RadiusCm);
    return true;
}

bool ALDCombatDirector::LaunchReturn(const FLDCombatProjectile& Outbound, const FVector& EndCm, const FGuid& EndBody, FString& Error)
{
    ALDCharacter* ThrowerActor=nullptr;
    const FLDBodyCombatState* Thrower=FindBody(Outbound.SourceBody,&ThrowerActor);
    if (!Thrower || !ThrowerActor || Thrower->bPendingDeath) { return true; }
    const FVector Foot=FootOf(*ThrowerActor);
    const FVector2D Toward(Foot.X-EndCm.X,Foot.Y-EndCm.Y);
    const double Length=Toward.Size();
    if (!FMath::IsFinite(Length)) { Error=TEXT("A return flight has no finite length."); return false; }
    if (Length<=UE_KINDA_SMALL_NUMBER) { return true; }
    // A branch return: straight toward the thrower's foot now, as long as that distance, at the same speed and radius; it
    // carries the payload's damage share, elements, burn and breach but no slow or control.
    FLDCombatProjectile Return=Outbound;
    const int64 ShareBp=Outbound.ReturnScaleBp;
    Return.FlightId=FGuid::NewGuid();
    Return.AdvancedThroughMs=Authority() ? Authority()->GetAuthorityNowMs() : Outbound.AdvancedThroughMs;
    Return.OriginCm=FVector(EndCm.X,EndCm.Y,Outbound.OriginCm.Z);
    Return.Direction=FVector(Toward.X/Length,Toward.Y/Length,0);
    Return.RangeCm=Length;
    Return.TravelledCm=0;
    Return.SlowBp=Return.SlowDurationMs=0;
    Return.PierceRemaining=0;
    Return.PierceScaleBp=Return.ReturnScaleBp=0;
    Return.bReturn=true;
    Return.Struck.Reset();
    Return.StartContact=EndBody;
    FLDCombatHit& Payload=Return.Payload;
    Payload.DeliveryScaleBp=LDCombatCardsDetail::Share(Payload.DeliveryScaleBp,Outbound.ReturnScaleBp);
    Payload.RootDurationMs=Payload.SlowBp=Payload.SlowDurationMs=Payload.DisarmDurationMs=Payload.StunDurationMs=0;
    Payload.SilenceDurationMs=Payload.AttackSlowBp=Payload.AttackSlowMs=Payload.KnockbackMs=0;
    Payload.KnockbackCm=0;
    Payload.KnockbackSource=FLDCombatSourceRef();
    // Skill branches (S06 B1, S21 B2): the return shocks or wets.
    if (Outbound.ReturnElement!=ELDDemoElement::None && Outbound.ReturnElement!=ELDDemoElement::Fire)
    { Payload.NativeElements|=LDCombatElements::Bit(LDCombatCardsDetail::CombatElement(Outbound.ReturnElement)); }
    Return.LandingGround.Reset();
    if (Payload.DeliveryScaleBp<=0) { Error=TEXT("A return flight needs a positive share."); return false; }
    UE_LOG(LogLDCombat,Log,TEXT("LD_COMBAT_PROJECTILE_RETURN owner=%s exit=%s length=%.0f share_bp=%lld"),
        *LDLog::Id(Return.SourceEntity),*LDLog::Source(Return.Source),Length,ShareBp);
    Projectiles.Add(MoveTemp(Return));
    return true;
}

// ---------------------------------------------------------------- Y08 and timed card work

bool ALDCombatDirector::WatchKnockbackImpact(ALDCharacter& Target, FLDBodyCombatState& Body, const FLDCombatHit& Hit,
    const FVector& Direction, int64 NowMs, FString& Error)
{
    Body.PendingImpact.Reset();
    auto* Movement=Cast<ULDCharacterMovementComponent>(Target.GetCharacterMovement());
    const FLDBodyCombatState* Holder=FindBody(Hit.SourceBody);
    if (!Movement || !Holder || Holder->bPendingDeath || Hit.KnockbackSource.Kind==ELDCombatSourceKind::None) { return true; }
    // Y08 answers every knockback its holder causes (and card_runaway_train / active_open_mic 2B watch wall impacts too).
    TArray<FLDPassiveEffectRef> Impacts, Trains;
    if (!CollectPassiveEffects(Holder->AttributeProjection,ELDDemoEffectOp::KnockbackImpact,Impacts,Error)
        || !CollectPassiveEffects(Holder->AttributeProjection,ELDDemoEffectOp::RunawayTrain,Trains,Error))
    { return false; }
    bool bWarcryWallVuln=false;
    if (Hit.KnockbackSource.Kind==ELDCombatSourceKind::Learned && Catalog())
    {
        const FLDDemoAbilityDefinition* AbilDef=nullptr;
        FString LookupErr;
        if (Catalog()->FindAbilityByName(Hit.KnockbackSource.DefinitionId,AbilDef,LookupErr) && AbilDef)
        {
            if (const FLDCombatEntityRecord* Rec=FindRecord(Holder->Victim.EntityId))
            {
                for (const FLDLearnedActiveValue& L : Rec->Build.Actives)
                {
                    if (L.AbilityId==Hit.KnockbackSource.DefinitionId)
                    {
                        const FLDDemoAbilityValues& Val=AbilDef->FindBranch(L.Branch) ? AbilDef->FindBranch(L.Branch)->Values : AbilDef->Values;
                        bWarcryWallVuln=Val.Effects.ContainsByPredicate([](const FLDDemoEffectDefinition& E)
                        { return E.GetOp()==ELDDemoEffectOp::WarcryRing && E.MultiplierBp>0 && E.ExtraDurationMs>0; });
                    }
                }
            }
        }
    }
    if (Impacts.IsEmpty() && Trains.IsEmpty() && !bWarcryWallVuln && Hit.KnockbackWallStunMs<=0) { return true; }
    FLDCombatKnockbackImpact& Impact=Body.PendingImpact.Emplace();
    Impact.WallStunMs=Hit.KnockbackWallStunMs;
    Impact.EffectId=Hit.EffectId;
    Impact.Holder=Hit.SourceBody;
    Impact.CreditOwner=Hit.CreditOwner;
    Impact.bCardImpact=!Impacts.IsEmpty();
    if (Impact.bCardImpact) { Impact.Impact=Impacts[0]; }
    Impact.Direction=Direction;
    Impact.PushSource=Hit.KnockbackSource;
    Impact.HolderADP2=Holder->AttributeProjection.Final.ADP2;
    Impact.HolderAPP2=Holder->AttributeProjection.Final.APP2;
    if (!LDMatchLifecycle::AddDurationMs(NowMs,Hit.KnockbackMs,Impact.EndMs,Error)) { return false; }
    Movement->WatchKnockbackBlock();
    return true;
}

bool ALDCombatDirector::ResolveKnockbackImpact(FLDBodyCombatState& Body, const FLDCombatKnockbackImpact& Impact, bool bBody,
    const ALDCharacter* BlockerActor, int64 NowMs, FString& Error)
{
    const ILDLifeAuthority* Mode=Authority();
    if (!Mode) { Error=TEXT("A knockback impact needs the current core."); return false; }
    const FLDCombatBodyRef SelfRef{Body.Victim,Body.Identity};
    const auto Package=[&](const FLDCombatBodyRef& To, bool bStun)
    {
        FLDCombatHit Hit;
        Hit.HitId=FGuid::NewGuid();
        Hit.EffectId=Impact.EffectId;
        Hit.Target=To;
        Hit.SourceBody=Impact.Holder;
        Hit.SourceEntity=Impact.Holder.Victim.EntityId;
        Hit.CreditOwner=Impact.CreditOwner;
        Hit.Source=Impact.Impact.Source;
        Hit.HitClass=ELDCombatHitClass::Ability;
        Hit.BaseP2=Impact.Impact.Effect.BaseP2;
        Hit.ADRatioBp=Impact.Impact.Effect.ADRatioBp;
        Hit.APRatioBp=Impact.Impact.Effect.APRatioBp;
        Hit.DamageType=Impact.Impact.Effect.DamageType;
        Hit.SourceADP2=Impact.HolderADP2;
        Hit.SourceAPP2=Impact.HolderAPP2;
        Hit.StunDurationMs=bStun ? Impact.Impact.Effect.SecondaryDurationMs : 0;
        Hit.bAddedDamage=true;
        PendingHits.Add(MoveTemp(Hit));
    };
    const FLDBodyCombatState* Other=bBody && BlockerActor ? StateFor(BlockerActor) : nullptr;
    const bool bLegalBody=Other && !Other->bPendingDeath && Other!=&Body && !IsIsolated(Impact.Holder.Victim.EntityId,*Other)
        && Other->Victim.EntityId!=Impact.Holder.Victim.EntityId
        && !(Other->Victim.Kind==ELDVictimKind::Contender && Mode->IsContenderProtected(Other->Victim.EntityId));
    if (!bBody) { Package(SelfRef,true); }
    else if (bLegalBody)
    {
        Package(SelfRef,false);
        Package({Other->Victim,Other->Identity},false);
    }
    if (!bBody || bLegalBody)
    {
        if (const FLDBodyCombatState* Holder=FindBody(Impact.Holder)) { LedgerCardTriggered(*Holder,Impact.Impact.Key); }
    }
    UE_LOG(LogLDCombat,Log,TEXT("LD_COMBAT_KNOCKBACK_IMPACT t=%lld victim=%s holder=%s kind=%s"),NowMs,*LDLog::Id(Body.Victim.EntityId),
        *LDLog::Id(Impact.Holder.Victim.EntityId),!bBody ? TEXT("wall") : bLegalBody ? TEXT("body") : TEXT("blocked_no_answer"));
    return true;
}

bool ALDCombatDirector::AdvanceCards(int64 NowMs, FString& Error)
{
    for (auto& Pair : Bodies)
    {
        FLDBodyCombatState& Body=Pair.Value;
        ALDCharacter* Actor=Pair.Key.Get();
        if (!Actor || Body.bPendingDeath) { continue; }
        const FLDCombatBodyRef SelfRef{Body.Victim,Body.Identity};
        // A12: a pause past the window ends the focus stacks.
        if (Body.FocusHits>0 && NowMs>Body.FocusUntilMs)
        {
            {
                Body.FocusHits=0;
                Body.FocusTarget=FLDCombatBodyRef();
                if (!RefreshTemporary(Body,Actor,NowMs,Error)) { return false; }
            }
        }
        // D05: the second segment's window closes.
        if (Body.SplitDashLeft>0 && NowMs>Body.SplitDashUntilMs)
        {
            Body.SplitDashLeft=0;
            MarkViewChanged(Body.Victim.EntityId);
        }
        // Y08 (and a branch wall stun, the final bowling): the push's first real block answers once.
        if (Body.PendingImpact.IsSet())
        {
            auto* Movement=Cast<ULDCharacterMovementComponent>(Actor->GetCharacterMovement());
            TOptional<ULDCharacterMovementComponent::FKnockbackBlock> Block;
            if (Movement) { Block=Movement->TakeKnockbackBlock(); }
            const FLDCombatKnockbackImpact Impact=Body.PendingImpact.GetValue();
            if (Block.IsSet() || NowMs>=Impact.EndMs || !Movement)
            {
                Body.PendingImpact.Reset();
                if (Movement) { Movement->StopKnockbackWatch(); }
            }
            if (Block.IsSet())
            {
                const ALDCharacter* Blocker=Cast<ALDCharacter>(Block->Blocker.Get());
                if (Impact.bCardImpact && !ResolveKnockbackImpact(Body,Impact,Block->bBody,Blocker,NowMs,Error)) { return false; }
                if (!Block->bBody)
                {
                    if (FLDBodyCombatState* Holder=FindBody(Impact.Holder); Holder && !Holder->bPendingDeath)
                    {
                        // card_runaway_train: pushing an enemy into a wall refunds wall_refund of innate dash cooldown.
                        TArray<FLDPassiveEffectRef> Trains;
                        if (!CollectPassiveEffects(Holder->AttributeProjection,ELDDemoEffectOp::RunawayTrain,Trains,Error)) { return false; }
                        if (!Trains.IsEmpty())
                        {
                            int64 RefundBp=0;
                            for (const FLDPassiveEffectRef& Ref : Trains) { RefundBp+=Ref.Effect.CostRatioBp; }
                            RefundBp=FMath::Min<int64>(RefundBp,9500);
                            if (FLDCombatEntityRecord* HolderRec=FindRecord(Holder->Victim.EntityId))
                            {
                                const FName InnateId=LDCombatSourceRules::InnateDashId();
                                for (FLDCombatTrack& Track : HolderRec->Tracks)
                                {
                                    if (Track.Kind==ELDCombatSourceKind::Innate && Track.DefinitionId==InnateId && Track.ReadyAtMs>NowMs)
                                    {
                                        const int64 Rem=Track.ReadyAtMs-NowMs;
                                        Track.ReadyAtMs=LDCombatSourceRules::ReduceReadyAt(Track.ReadyAtMs,NowMs,LDCombatCardsDetail::Share(Rem,RefundBp));
                                    }
                                }
                                MarkViewChanged(Holder->Victim.EntityId);
                                LedgerCardTriggered(*Holder,Trains[0].Key);
                            }
                        }
                        // active_open_mic 2B: wall impact applies warcry_ring wall_vulnerable.
                        if (Impact.PushSource.Kind==ELDCombatSourceKind::Learned && Catalog())
                        {
                            const FLDDemoAbilityDefinition* AbilDef=nullptr;
                            FString LookupErr;
                            if (Catalog()->FindAbilityByName(Impact.PushSource.DefinitionId,AbilDef,LookupErr) && AbilDef)
                            {
                                if (const FLDCombatEntityRecord* Rec=FindRecord(Holder->Victim.EntityId))
                                {
                                    for (const FLDLearnedActiveValue& L : Rec->Build.Actives)
                                    {
                                        if (L.AbilityId==Impact.PushSource.DefinitionId)
                                        {
                                            const FLDDemoAbilityValues& Val=AbilDef->FindBranch(L.Branch) ? AbilDef->FindBranch(L.Branch)->Values : AbilDef->Values;
                                            if (const FLDDemoEffectDefinition* Wc=Val.Effects.FindByPredicate(
                                                [](const FLDDemoEffectDefinition& E) { return E.GetOp()==ELDDemoEffectOp::WarcryRing; }))
                                            {
                                                if (Wc->MultiplierBp>0 && Wc->ExtraDurationMs>0)
                                                {
                                                    FLDCombatVulnerability& Row=Body.Branch.Vulnerabilities.AddDefaulted_GetRef();
                                                    Row.Scope=ELDDemoDamageScope::All;
                                                    Row.RatioBp=Wc->MultiplierBp;
                                                    if (!LDMatchLifecycle::AddDurationMs(NowMs,Wc->ExtraDurationMs,Row.ExpiresAtMs,Error)) { return false; }
                                                    ++Body.StateRevision;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                if (!Block->bBody && Impact.WallStunMs>0)
                {
                    FLDCombatHit Stun;
                    Stun.HitId=FGuid::NewGuid();
                    Stun.EffectId=Impact.EffectId;
                    Stun.Target=SelfRef;
                    Stun.SourceBody=Impact.Holder;
                    Stun.SourceEntity=Impact.Holder.Victim.EntityId;
                    Stun.CreditOwner=Impact.CreditOwner;
                    Stun.Source=Impact.PushSource;
                    Stun.HitClass=ELDCombatHitClass::Ability;
                    Stun.StunDurationMs=Impact.WallStunMs;
                    PendingHits.Add(MoveTemp(Stun));
                    UE_LOG(LogLDCombat,Log,TEXT("LD_COMBAT_WALL_STUN t=%lld target=%s stun_ms=%lld"),NowMs,
                        *LDLog::Id(Body.Victim.EntityId),Impact.WallStunMs);
                }
            }
        }
    }
    return true;
}

int64 ALDCombatDirector::NextCardsDueMs(int64 NowMs) const
{
    int64 Due=MAX_int64;
    const auto Consider=[&Due,NowMs](int64 At) { if (At>NowMs && At<Due) { Due=At; } };
    for (const auto& Pair : Bodies)
    {
        const FLDBodyCombatState& Body=Pair.Value;
        if (Body.bPendingDeath) { continue; }
        if (Body.PendingImpact.IsSet()) { Consider(Body.PendingImpact->EndMs); }
        if (Body.SplitDashLeft>0) { Consider(Body.SplitDashUntilMs+1); }
        if (Body.FocusHits>0) { Consider(Body.FocusUntilMs+1); }
        if (Body.LastStandUntilMs>NowMs) { Consider(Body.LastStandUntilMs); }
    }
    for (const FLDCombatAutoRecast& Repeat : AutoRecasts) { Consider(Repeat.DueMs); }
    return Due;
}
