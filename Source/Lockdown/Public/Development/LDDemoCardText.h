#pragma once

#include "CoreMinimal.h"
#include "Development/LDDemoContentDefinitions.h"

// #147 card text (v7 §1.2): a skill's main sentence (display description_text) and advanced line (advanced_text) are
// authored with placeholders [op.field], e.g. "定身目标 [root.duration] 秒，造成 [damage.amount] 物理伤害。". A placeholder
// names one effect op of the rank (snake_case, as in content.json) and one typed field, so every number on a card comes from
// the rank row the owner actually uses. The importer refuses a template whose placeholder names no effect of its ranks.
namespace LDDemoCardText
{
    enum class EField : uint8
    {
        // BaseP2 plus the coefficients times the owner's AD, AP, max HP and armor, grown for the owner's level.
        Amount,
        Duration, Ratio, Count, Radius, Distance, Interval, Delay, SecondaryRatio, SecondaryDuration,
        // One stat of the effect's Stats.
        Stat
    };

    struct FToken
    {
        // Literal text when !bValue.
        FString Literal;
        bool bValue = false;
        ELDDemoEffectOp Op = ELDDemoEffectOp::Damage;
        EField Field = EField::Amount;
        ELDDemoBonusStat Stat = ELDDemoBonusStat::ADFlat;
    };

    // Splits Template into literals and placeholders; false with Error on an unknown op, field or stat, or a stray bracket.
    LOCKDOWN_API bool Parse(const FString& Template, TArray<FToken>& Out, FString& Error);
    // The effect of Rank a value token reads, or null.
    LOCKDOWN_API const FLDDemoEffectDefinition* FindEffect(const FLDDemoRankDefinition& Rank, const FToken& Token);
}
