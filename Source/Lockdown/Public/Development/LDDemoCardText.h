#pragma once

#include "CoreMinimal.h"
#include "Development/LDDemoContentDefinitions.h"

// #147/#198 card text: an active's description_text is authored with placeholders [op.field], e.g. "定身目标 [root.duration]
// 秒，造成 [damage.amount] 物理伤害。". A placeholder names one effect op of the active's single value set (snake_case, as in
// content.json) and one typed field, so every number on a card comes from the value set the owner actually uses. A stat's
// description_text reads its per-point bonuses as [per_point.<Stat>]. The importer refuses a template whose placeholder
// names no effect of its active or no per-point bonus of its stat. #198 B: a component's description_text reads its effects
// the same way.
namespace LDDemoCardText
{
    enum class EField : uint8
    {
        // BaseP2 plus the coefficients times the owner's AD, AP, max HP and armor, grown for the owner's level.
        Amount,
        Duration, Ratio, Count, Radius, Distance, Interval, Delay, SecondaryRatio, SecondaryDuration,
        // BasicCount (stacks per primary basic hit); a cooldown refund's seconds (AmountMs); Amount x SecondaryRatioBp (a
        // shield's burst).
        BasicCount, Refund, Burst,
        // One stat of the effect's Stats, or of a stat's PerPoint.
        Stat, Limit,
        // #198 F: MultiplierBp and CostRatioBp as percents, AngleDegrees in degrees.
        Multiplier, Angle, CostRatio
    };

    struct FToken
    {
        // Literal text when !bValue.
        FString Literal;
        bool bValue = false;
        // [per_point.<Stat>]: Field is Stat and Op is unused.
        bool bPerPoint = false;
        ELDDemoEffectOp Op = ELDDemoEffectOp::Damage;
        EField Field = EField::Amount;
        ELDDemoBonusStat Stat = ELDDemoBonusStat::ADFlat;
    };

    // Splits Template into literals and placeholders; false with Error on an unknown op, field or stat, or a stray bracket.
    LOCKDOWN_API bool Parse(const FString& Template, TArray<FToken>& Out, FString& Error);
    // The effect of Values an effect token reads, or null (also for a per-point token).
    LOCKDOWN_API const FLDDemoEffectDefinition* FindEffect(const FLDDemoAbilityValues& Values, const FToken& Token);
    // #198 B: the same over a component's effects.
    LOCKDOWN_API const FLDDemoEffectDefinition* FindEffect(const TArray<FLDDemoEffectDefinition>& Effects, const FToken& Token);
    // The per-point bonus of Stat a per-point token reads, or null.
    LOCKDOWN_API const FLDDemoStatBonus* FindPerPoint(const FLDDemoStatDefinition& Stat, const FToken& Token);
}
