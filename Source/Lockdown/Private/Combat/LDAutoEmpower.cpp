#include "Combat/LDAutoEmpower.h"

#include "Combat/LDCombatElements.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace LDAutoEmpowerDetail
{
    // An empowered flight's pierce budget: more than the bodies one flight can meet.
    constexpr int32 MaxPierce=32;

    FLDDemoEffectDefinition* Find(FLDDemoAbilityValues& Values, ELDDemoEffectOp Op)
    {
        return Values.Effects.FindByPredicate([Op](const FLDDemoEffectDefinition& Effect) { return Effect.GetOp()==Op; });
    }

    bool Has(const FLDDemoAbilityValues& Values, ELDDemoEffectOp Op)
    {
        return Values.Effects.ContainsByPredicate([Op](const FLDDemoEffectDefinition& Effect) { return Effect.GetOp()==Op; });
    }

    int64 Ceil(int64 Value, int64 Bp)
    {
        return Value<=0 || Bp<=0 ? Value : Value/10000*Bp+(Value%10000*Bp+9999)/10000;
    }
}

bool LDAutoEmpower::Enabled(const FLDDemoEmpowerParameters& P)
{
    static const bool bSwitch=FParse::Param(FCommandLine::Get(),TEXT("LDEmpower"));
    return (P.bEnabled || bSwitch) && P.ThresholdP2>0 && P.CostP2>0 && P.CostP2<=P.ThresholdP2;
}

ELDCombatEntry LDAutoEmpower::ParseHost(const FString& Name)
{
    for (const ELDCombatEntry Entry : {ELDCombatEntry::Basic,ELDCombatEntry::Q,ELDCombatEntry::W,ELDCombatEntry::E,ELDCombatEntry::Dash})
    {
        if (Name.Equals(EntryName(Entry),ESearchCase::IgnoreCase)) { return Entry; }
    }
    return ELDCombatEntry::None;
}

ELDCombatEntry LDAutoEmpower::SwitchHost()
{
    FString Name;
    return FParse::Value(FCommandLine::Get(),TEXT("LDEmpowerHost="),Name) ? ParseHost(Name) : ELDCombatEntry::None;
}

bool LDAutoEmpower::Ready(const FLDDemoEmpowerParameters& P, int64 MomentumP2)
{
    return P.ThresholdP2>0 && MomentumP2>=P.ThresholdP2;
}

bool LDAutoEmpower::CanPay(const FLDDemoEmpowerParameters& P, int64 MomentumP2)
{
    return P.CostP2>0 && MomentumP2>=P.CostP2;
}

bool LDAutoEmpower::IsHostEntry(ELDCombatEntry Entry)
{
    return Entry==ELDCombatEntry::Basic || Entry==ELDCombatEntry::Q || Entry==ELDCombatEntry::W || Entry==ELDCombatEntry::E
        || Entry==ELDCombatEntry::Dash;
}

const TCHAR* LDAutoEmpower::EntryName(ELDCombatEntry Entry)
{
    switch (Entry)
    {
    case ELDCombatEntry::Basic: return TEXT("Basic");
    case ELDCombatEntry::Q: return TEXT("Q");
    case ELDCombatEntry::W: return TEXT("W");
    case ELDCombatEntry::E: return TEXT("E");
    case ELDCombatEntry::Dash: return TEXT("D");
    default: return TEXT("None");
    }
}

const TCHAR* LDAutoEmpower::ProcessorName(ELDEmpowerProcessor Processor)
{
    switch (Processor)
    {
    case ELDEmpowerProcessor::Fist: return TEXT("fist");
    case ELDEmpowerProcessor::Strike: return TEXT("strike");
    case ELDEmpowerProcessor::Guard: return TEXT("guard");
    case ELDEmpowerProcessor::Move: return TEXT("move");
    case ELDEmpowerProcessor::Ward: return TEXT("ward");
    default: return TEXT("none");
    }
}

ELDEmpowerProcessor LDAutoEmpower::ProcessorOf(ELDCombatEntry Entry, const FLDDemoTargetDefinition* Target,
    const FLDDemoAbilityValues* Values)
{
    using namespace LDAutoEmpowerDetail;
    using EOp=ELDDemoEffectOp;
    if (Entry==ELDCombatEntry::Basic) { return ELDEmpowerProcessor::Fist; }
    if (Entry==ELDCombatEntry::Dash) { return ELDEmpowerProcessor::Move; }
    if (!IsHostEntry(Entry) || !Target || !Values) { return ELDEmpowerProcessor::None; }
    if (Has(*Values,EOp::EmpowerBasic) || Has(*Values,EOp::FrenzyWaves)) { return ELDEmpowerProcessor::Fist; }
    if (Target->Mode==ELDDemoTargetMode::DirectionDash || Has(*Values,EOp::Dash)) { return ELDEmpowerProcessor::Move; }
    if (Target->Mode==ELDDemoTargetMode::Self && (Has(*Values,EOp::Shield) || Has(*Values,EOp::ParryStance)
        || Has(*Values,EOp::Brace) || Has(*Values,EOp::Taunt)))
    { return ELDEmpowerProcessor::Guard; }
    if (Has(*Values,EOp::Zone) || Has(*Values,EOp::ContactTrap) || Has(*Values,EOp::Fan) || Has(*Values,EOp::Bag)
        || Has(*Values,EOp::CordonLine))
    { return ELDEmpowerProcessor::Ward; }
    return ELDEmpowerProcessor::Strike;
}

ELDCombatEntry LDAutoEmpower::AutoHost(const TArray<FHostCandidate>& Candidates)
{
    static const ELDCombatEntry Order[]={ELDCombatEntry::Q,ELDCombatEntry::W,ELDCombatEntry::E,ELDCombatEntry::Basic,ELDCombatEntry::Dash};
    ELDCombatEntry Best=ELDCombatEntry::Basic;
    int32 BestScore=MIN_int32;
    for (const ELDCombatEntry Entry : Order)
    {
        const FHostCandidate* Candidate=Candidates.FindByPredicate([Entry](const FHostCandidate& Row) { return Row.Entry==Entry; });
        if (!Candidate || !Candidate->bHeld) { continue; }
        if (Candidate->Score>BestScore) { Best=Entry; BestScore=Candidate->Score; }
    }
    return Best;
}

int64 LDAutoEmpower::Share(int64 Value, int64 Bp)
{
    return Value/10000*Bp+Value%10000*Bp/10000;
}

FLDDemoRational LDAutoEmpower::ScaleCm(const FLDDemoRational& Value, int64 Bp)
{
    FLDDemoRational Out=Value;
    if (Value.Denominator<=0 || Bp<=0) { return Out; }
    Out.Numerator=Value.Numerator*Bp;
    Out.Denominator=Value.Denominator*10000;
    const int64 Divisor=FMath::GreatestCommonDivisor(FMath::Abs(Out.Numerator),Out.Denominator);
    if (Divisor>1) { Out.Numerator/=Divisor; Out.Denominator/=Divisor; }
    return Out;
}

void LDAutoEmpower::ApplyStrike(const FLDDemoEmpowerParameters& P, FLDDemoTargetDefinition& Target, FLDDemoAbilityValues& Values)
{
    using EOp=ELDDemoEffectOp;
    const int64 Bp=P.ShapeScaleBp;
    switch (Target.Mode)
    {
    case ELDDemoTargetMode::DirectionLine:
        Target.RangeCm=ScaleCm(Target.RangeCm,Bp);
        Target.LineHalfWidthCm=ScaleCm(Target.LineHalfWidthCm,Bp);
        break;
    case ELDDemoTargetMode::DirectionCone:
        Target.AngleDegrees=ScaleCm(Target.AngleDegrees,P.ConeAngleScaleBp);
        break;
    case ELDDemoTargetMode::DirectionProjectile:
        Target.RangeCm=ScaleCm(Target.RangeCm,Bp);
        Target.ProjectileRadiusCm=ScaleCm(Target.ProjectileRadiusCm,Bp);
        break;
    default:
        break;
    }
    for (FLDDemoEffectDefinition& Effect : Values.Effects)
    {
        switch (Effect.GetOp())
        {
        case EOp::SelfArea:
        case EOp::GroundImpact:
            Effect.RadiusCm=ScaleCm(Effect.RadiusCm,Bp);
            break;
        case EOp::Knockback:
            Effect.DistanceCm=ScaleCm(Effect.DistanceCm,Bp);
            break;
        case EOp::ChannelCone:
            Effect.AngleDegrees=ScaleCm(Effect.AngleDegrees,P.ConeAngleScaleBp);
            break;
        case EOp::ChainBounce:
            Effect.Count+=P.ExtraCount;
            break;
        default:
            break;
        }
    }
}

void LDAutoEmpower::ApplyForms(const FLDDemoEmpowerParameters& P, ELDEmpowerProcessor Processor, ELDDemoSkillElement Element,
    FLDDemoTargetDefinition& Target, FLDDemoAbilityValues& Values, FLDEmpowerShape& Out)
{
    using namespace LDAutoEmpowerDetail;
    using EOp=ELDDemoEffectOp;
    Out=FLDEmpowerShape();
    if (Processor==ELDEmpowerProcessor::Guard)
    {
        if (FLDDemoEffectDefinition* Parry=Find(Values,EOp::ParryStance)) { Parry->ExtraDurationMs=FMath::Max(Parry->ExtraDurationMs,P.CounterStunMs); }
        return;
    }
    if (Processor==ELDEmpowerProcessor::Ward)
    {
        if (Has(Values,EOp::CordonLine)) { Out.TouchStunMs=P.TouchStunMs; }
        return;
    }
    if (Processor!=ELDEmpowerProcessor::Strike) { return; }
    if (FLDDemoEffectDefinition* Push=Find(Values,EOp::Knockback); Push && Target.Mode==ELDDemoTargetMode::Unit)
    { Push->ExtraDurationMs=FMath::Max(Push->ExtraDurationMs,P.WallStunMs); }
    Out.bGrabAll=Has(Values,EOp::GrabLine);
    if (Target.Mode==ELDDemoTargetMode::DirectionProjectile)
    {
        // Every body on the way (each one once: a flight never strikes the same body twice).
        Out.PierceCount=MaxPierce;
        Out.PierceScaleBp=10000;
    }
    ELDGroundStateKind Kind=ELDGroundStateKind::None;
    switch (Element)
    {
    case ELDDemoSkillElement::Fire: Kind=ELDGroundStateKind::Fire; break;
    case ELDDemoSkillElement::Water: Kind=ELDGroundStateKind::Puddle; break;
    case ELDDemoSkillElement::Chill: Kind=ELDGroundStateKind::Ice; break;
    default: break;
    }
    if (Kind==ELDGroundStateKind::None || P.GroundLifeMs<=0) { return; }
    if (Has(Values,EOp::ChannelCone))
    {
        // The cone's own ground state (N1 2A's ice over the sector) lasts at least GroundLifeMs.
        FLDDemoEffectDefinition* Ground=Find(Values,EOp::GroundState);
        if (!Ground)
        {
            Ground=&Values.Effects.AddDefaulted_GetRef();
            Ground->Opcode=uint16(EOp::GroundState);
            Ground->GroundKind=uint8(Kind);
        }
        Ground->DurationMs=FMath::Max(Ground->DurationMs,P.GroundLifeMs);
        return;
    }
    const FLDDemoEffectDefinition* Area=Find(Values,EOp::SelfArea);
    if ((Target.Mode==ELDDemoTargetMode::Self && Area) || Target.Mode==ELDDemoTargetMode::DirectionLine)
    {
        Out.GroundKind=uint8(Kind);
        Out.GroundLifeMs=P.GroundLifeMs;
        if (Target.Mode==ELDDemoTargetMode::Self) { Out.GroundRadiusCm=Area->RadiusCm; }
    }
}

void LDAutoEmpower::ApplyGuard(const FLDDemoEmpowerParameters& P, FLDDemoAbilityValues& Values)
{
    using EOp=ELDDemoEffectOp;
    const int64 Bp=P.ShapeScaleBp;
    for (FLDDemoEffectDefinition& Effect : Values.Effects)
    {
        switch (Effect.GetOp())
        {
        case EOp::ParryStance:
            Effect.DurationMs=Share(Effect.DurationMs,Bp);
            break;
        case EOp::Brace:
            Effect.LimitP2=Share(Effect.LimitP2,Bp);
            Effect.RadiusCm=ScaleCm(Effect.RadiusCm,Bp);
            break;
        case EOp::Taunt:
            Effect.RadiusCm=ScaleCm(Effect.RadiusCm,Bp);
            break;
        default:
            break;
        }
    }
}

void LDAutoEmpower::ApplyWard(const FLDDemoEmpowerParameters& P, FLDDemoAbilityValues& Values)
{
    using namespace LDAutoEmpowerDetail;
    using EOp=ELDDemoEffectOp;
    const int64 Bp=P.ShapeScaleBp;
    for (FLDDemoEffectDefinition& Effect : Values.Effects)
    {
        switch (Effect.GetOp())
        {
        case EOp::Zone:
            Effect.DurationMs=Share(Effect.DurationMs,Bp);
            Effect.Count=int32(Ceil(Effect.Count,Bp));
            Effect.RadiusCm=ScaleCm(Effect.RadiusCm,Bp);
            break;
        case EOp::ContactTrap:
            Effect.RadiusCm=ScaleCm(Effect.RadiusCm,Bp);
            if (P.WardFuseMs>0) { Effect.DelayMs=FMath::Min(Effect.DelayMs,P.WardFuseMs); }
            break;
        case EOp::Fan:
            Effect.DurationMs=Share(Effect.DurationMs,Bp);
            break;
        case EOp::Bag:
            Effect.BaseP2=Share(Effect.BaseP2,Bp);
            Effect.LimitP2=Share(Effect.LimitP2,Bp);
            Effect.RadiusCm=ScaleCm(Effect.RadiusCm,Bp);
            break;
        case EOp::CordonLine:
            Effect.RadiusCm=ScaleCm(Effect.RadiusCm,Bp);
            break;
        default:
            break;
        }
    }
}

void LDAutoEmpower::ApplyFistSkill(const FLDDemoEmpowerParameters& P, FLDDemoAbilityValues& Values)
{
    using EOp=ELDDemoEffectOp;
    for (FLDDemoEffectDefinition& Effect : Values.Effects)
    {
        if (Effect.GetOp()==EOp::EmpowerBasic && Effect.Count>0) { Effect.Count+=P.ExtraCount; }
        if (Effect.GetOp()==EOp::FrenzyWaves) { Effect.RadiusCm=ScaleCm(Effect.RadiusCm,P.ShapeScaleBp); }
    }
}

void LDAutoEmpower::ApplyElement(const FLDDemoEmpowerParameters& P, ELDDemoSkillElement Element, bool bCanDeliverHostile,
    int64 ReactionP2, int64 ApprovedRotMs, FLDDemoAbilityValues& Values)
{
    using namespace LDAutoEmpowerDetail;
    using EOp=ELDDemoEffectOp;
    switch (Element)
    {
    case ELDDemoSkillElement::Fire:
        // More burn layers: on the release's hits, or spread over a zone's pulses.
        if (FLDDemoEffectDefinition* Burn=Find(Values,EOp::Burn)) { Burn->Count+=int32(P.FireBurnStacks); }
        else if (const FLDDemoEffectDefinition* Zone=Find(Values,EOp::Zone))
        {
            const int64 Pulses=FMath::Max<int64>(Zone->Count,1);
            const int64 IntervalMs=Zone->IntervalMs;
            FLDDemoEffectDefinition* ZoneBurn=Find(Values,EOp::ZoneBurn);
            if (!ZoneBurn)
            {
                ZoneBurn=&Values.Effects.AddDefaulted_GetRef();
                ZoneBurn->Opcode=uint16(EOp::ZoneBurn);
                ZoneBurn->IntervalMs=IntervalMs;
            }
            ZoneBurn->Count+=int32(FMath::Max<int64>(1,P.FireBurnStacks/Pulses));
        }
        else if (bCanDeliverHostile)
        {
            FLDDemoEffectDefinition& Added=Values.Effects.AddDefaulted_GetRef();
            Added.Opcode=uint16(EOp::Burn);
            Added.Count=int32(P.FireBurnStacks);
        }
        break;
    case ELDDemoSkillElement::Water:
        if (const FLDDemoEffectDefinition* Zone=Find(Values,EOp::Zone))
        {
            const int64 IntervalMs=Zone->IntervalMs;
            FLDDemoEffectDefinition* ZoneSlow=Find(Values,EOp::ZoneSlow);
            if (!ZoneSlow)
            {
                ZoneSlow=&Values.Effects.AddDefaulted_GetRef();
                ZoneSlow->Opcode=uint16(EOp::ZoneSlow);
                ZoneSlow->IntervalMs=IntervalMs;
            }
            ZoneSlow->RatioBp=FMath::Max(ZoneSlow->RatioBp,P.WaterSlowBp);
            ZoneSlow->DurationMs=FMath::Max(ZoneSlow->DurationMs,P.WaterSlowMs);
        }
        else if (bCanDeliverHostile)
        {
            FLDDemoEffectDefinition* Slow=Find(Values,EOp::Slow);
            if (!Slow) { Slow=&Values.Effects.AddDefaulted_GetRef(); Slow->Opcode=uint16(EOp::Slow); }
            Slow->RatioBp=FMath::Max(Slow->RatioBp,P.WaterSlowBp);
            Slow->DurationMs=FMath::Max(Slow->DurationMs,P.WaterSlowMs);
        }
        break;
    case ELDDemoSkillElement::Shock:
    {
        // One extra discharge on each body hit: ShockReactionBp of the caster's R, added to its damage (a zone shares it
        // over its pulses).
        FLDDemoEffectDefinition* Damage=Find(Values,EOp::Damage);
        if (!Damage) { Damage=Find(Values,EOp::GroundImpact); }
        if (!Damage) { Damage=Find(Values,EOp::DashContact); }
        if (!Damage) { Damage=Find(Values,EOp::ChannelCone); }
        int64 Pulses=1;
        if (!Damage)
        {
            Damage=Find(Values,EOp::ZoneDamage);
            if (const FLDDemoEffectDefinition* Zone=Find(Values,EOp::Zone); Damage && Zone && Zone->Count>0) { Pulses=Zone->Count; }
        }
        if (Damage && ReactionP2>0)
        {
            const int64 Extra=FMath::Max<int64>(1,Share(ReactionP2,P.ShockReactionBp)/Pulses);
            if (Extra<=MAX_int64-Damage->BaseP2) { Damage->BaseP2+=Extra; }
        }
        break;
    }
    case ELDDemoSkillElement::Rot:
        if (FLDDemoEffectDefinition* Rot=Find(Values,EOp::Rot))
        {
            Rot->DurationMs=Share(Rot->DurationMs>0 ? Rot->DurationMs : ApprovedRotMs,P.RotDurationBp);
        }
        else if (bCanDeliverHostile)
        {
            FLDDemoEffectDefinition& Added=Values.Effects.AddDefaulted_GetRef();
            Added.Opcode=uint16(EOp::Rot);
            Added.DurationMs=Share(ApprovedRotMs,P.RotDurationBp);
        }
        break;
    case ELDDemoSkillElement::Chill:
        // A root (hard control, diminishing returns).
        if (Has(Values,EOp::Chill) || bCanDeliverHostile)
        {
            FLDDemoEffectDefinition* Root=Find(Values,EOp::Root);
            if (!Root) { Root=&Values.Effects.AddDefaulted_GetRef(); Root->Opcode=uint16(EOp::Root); }
            Root->DurationMs=FMath::Max(Root->DurationMs,P.ChillRootMs);
        }
        break;
    default:
        break;
    }
}
