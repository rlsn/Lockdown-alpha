#include "Development/LDWorkshopRules.h"

bool LDWorkshop::Lists(const TArray<FString>& Ids, FName Id)
{
    return !Id.IsNone() && Ids.ContainsByPredicate([Id](const FString& Row) { return FName(*Row) == Id; });
}

namespace
{
    bool HostHas(const FLDDemoAbilityDefinition& Host, ELDDemoEffectOp Op)
    {
        return Host.Values.Effects.ContainsByPredicate([Op](const FLDDemoEffectDefinition& Effect) { return Effect.GetOp() == Op; });
    }

    // Whether a mod op changes this host, mirroring the gates of LDCombatSourceRules::CompileWorkshopActive. Unset for ops
    // without a compile gate (connectors resolve at runtime).
    TOptional<bool> ModChangesHost(ELDDemoEffectOp Op, const FLDDemoAbilityDefinition& Host)
    {
        using E = ELDDemoEffectOp;
        const auto Has = [&Host](E Want) { return HostHas(Host, Want); };
        const ELDDemoTargetMode Mode = Host.Target.Mode;
        switch (Op)
        {
        case E::KnockbackScale: return Has(E::Knockback);
        case E::ControlScale: return Has(E::Root) || Has(E::Stun) || Has(E::Silence) || Has(E::Disarm) || Has(E::Taunt);
        case E::RangeBonus: return Mode != ELDDemoTargetMode::Self;
        case E::CooldownCost: return true;
        case E::CompressCone: return Has(E::ChannelCone);
        case E::CompressZone: return Has(E::Zone) && Has(E::ZoneDamage);
        case E::DashScale: return Has(E::Dash);
        case E::SpreadUnit: return Mode == ELDDemoTargetMode::Unit && Has(E::Damage);
        case E::SpreadProjectile:
        case E::ProjectileReturn:
        case E::ProjectilePierce: return Mode == ELDDemoTargetMode::DirectionProjectile && Has(E::Damage);
        case E::ChainExtra:
        case E::CompressChain: return Has(E::ChainBounce);
        case E::EchoImpact: return Has(E::Damage) && (Has(E::SelfArea) || Has(E::GroundImpact) || Has(E::ContactTrap));
        case E::ZoneTether: return Has(E::Zone);
        case E::DefenseShove: return Has(E::Shield) || Has(E::Brace);
        case E::EmpowerReach: return Has(E::EmpowerBasic);
        case E::FinalKnockback: return Has(E::EmpowerBasic) || Has(E::ChannelCone);
        case E::Pull: return Has(E::Knockback) || Has(E::Taunt);
        default: return {};
        }
    }

    const FLDDemoAbilityDefinition* FindAbility(const FLDDemoContentDefinitions& Content, const FString& Id)
    {
        return Content.Abilities.FindByPredicate([&Id](const FLDDemoAbilityDefinition& Ability) { return Ability.Id == Id; });
    }
}

bool LDWorkshop::TargetsActive(const FLDDemoContentDefinitions& Content, const FLDDemoComponentDefinition& Definition, FName ActiveId)
{
    if (Definition.Kind == ELDDemoComponentKind::Base || ActiveId.IsNone())
    {
        return false;
    }
    if (Lists(Definition.TargetActives, ActiveId))
    {
        return true;
    }
    const FLDDemoAbilityDefinition* Target = FindAbility(Content, ActiveId.ToString());
    if (!Target)
    {
        return false;
    }
    TArray<const FLDDemoAbilityDefinition*> SameMode;
    for (const FString& Id : Definition.TargetActives)
    {
        const FLDDemoAbilityDefinition* Listed = FindAbility(Content, Id);
        if (Listed && Listed->Target.Mode == Target->Target.Mode) { SameMode.Add(Listed); }
    }
    if (SameMode.IsEmpty())
    {
        return false;
    }
    // Same delivery type is necessary but not enough: the component must actually change this host.
    bool bGated = false;
    for (const FLDDemoEffectDefinition& Effect : Definition.Effects)
    {
        const TOptional<bool> Changes = ModChangesHost(Effect.GetOp(), *Target);
        if (!Changes.IsSet()) { continue; }
        if (Changes.GetValue()) { return true; }
        bGated = true;
    }
    if (bGated)
    {
        return false;
    }
    // Runtime-triggered effects: the host must carry every effect of some listed host of its delivery type.
    return SameMode.ContainsByPredicate([Target](const FLDDemoAbilityDefinition* Listed)
    {
        return !Listed->Values.Effects.ContainsByPredicate([Target](const FLDDemoEffectDefinition& Effect)
            { return !HostHas(*Target, Effect.GetOp()); });
    });
}

const FLDDemoEffectDefinition* LDWorkshop::FindOp(const FLDDemoComponentDefinition& Definition, ELDDemoEffectOp Op)
{
    return Definition.Effects.FindByPredicate([Op](const FLDDemoEffectDefinition& Effect) { return Effect.GetOp() == Op; });
}

bool LDWorkshop::TargetShape(const FLDDemoContentDefinitions& Content, const FLDDemoComponentDefinition& Definition,
    ELDComponentTargetKind Kind, FName TargetId, FName BoundActive, ELDDemoElement Element, FName BoundComponent)
{
    if (Definition.Kind == ELDDemoComponentKind::Base || !BoundActive.IsNone() || Element != ELDDemoElement::None)
    {
        return false;
    }
    if (Definition.ExtraBases.IsEmpty() ? !BoundComponent.IsNone() : !Lists(Definition.ExtraBases, BoundComponent))
    {
        return false;
    }
    switch (Kind)
    {
    case ELDComponentTargetKind::Active: return TargetsActive(Content, Definition, TargetId);
    case ELDComponentTargetKind::Component: return Lists(Definition.TargetComponents, TargetId);
    case ELDComponentTargetKind::Dash: return Definition.bTargetDash && TargetId.IsNone();
    case ELDComponentTargetKind::None: return Definition.bTargetSelf && TargetId.IsNone();
    default: return false;
    }
}

bool LDWorkshop::Conflicts(const FLDDemoComponentDefinition& A, const FLDDemoComponentDefinition& B)
{
    return A.Id != B.Id && !A.Form.IsEmpty() && A.Form == B.Form;
}
