#pragma once

#include "CoreMinimal.h"
#include "Development/LDDemoContentDefinitions.h"

class FLDDemoContentSnapshot;

enum class ELDAttributeProjectionError : uint8
{
    None, Catalog, UnknownDefinition, DuplicateSource, InputDomain, Aggregation,
    OutOfBounds, MissingValue, NumericDomain, NumericUnderflow, Overflow
};

struct FLDAttributeProjectionFailure
{
    ELDAttributeProjectionError Code = ELDAttributeProjectionError::None;
    FString Field;
    FString Message;
};

struct FLDLearnedPassiveValue
{
    FLDLearnedPassiveValue(FString InAbilityId, uint8 InRank)
        : AbilityId(MoveTemp(InAbilityId)), Rank(InRank) {}

    FString AbilityId;
    // Only this rank's values apply; they replace lower ranks rather than adding to them.
    uint8 Rank;
};

struct FLDAttributeCardValue
{
    FLDAttributeCardValue(FString InAttributeId, uint8 InCount)
        : AttributeId(MoveTemp(InAttributeId)), Count(InCount) {}

    FString AttributeId;
    uint8 Count;
};

struct FLDAttributeBuildValues
{
    // Every part is explicit; an empty array states that no such source is held.
    FLDAttributeBuildValues(uint8 InLevel, TArray<FLDLearnedPassiveValue> InLearnedPassives,
        TArray<FLDAttributeCardValue> InAttributeCards, TArray<FString> InEquippedItems)
        : Level(InLevel), LearnedPassives(MoveTemp(InLearnedPassives)),
          AttributeCards(MoveTemp(InAttributeCards)), EquippedItems(MoveTemp(InEquippedItems)) {}

    uint8 Level;
    // Passive definitions only, each listed once; learned actives carry no attributes.
    TArray<FLDLearnedPassiveValue> LearnedPassives;
    TArray<FLDAttributeCardValue> AttributeCards;
    // Occupied equipment slots in slot order; the same definition may appear twice.
    TArray<FString> EquippedItems;
};

enum class ELDProjectedAttribute : uint8
{
    MaxHP, MaxMP, AD, AP, Armor, MagicResist, AttackSpeed, MoveSpeed, AttackRange,
    HPRegen, MPRegen, CritChance, CooldownHaste, Count
};

struct FLDProjectedAttributes
{
    int64 MaxHPP2 = 0;
    int64 MaxMPP2 = 0;
    int64 ADP2 = 0;
    int64 APP2 = 0;
    int64 ArmorP2 = 0;
    int64 MagicResistP2 = 0;
    // Attacks per second; the value for FLDBasicTimingInput::CurrentAttackSpeed.
    FLDDemoRational AttackSpeed;
    FLDDemoRational MoveSpeedCmPerSec;
    // Basic-attack footpoint XY centre distance; not an ability range or cleave radius.
    FLDDemoRational AttackRangeCm;
    // Always-on rates; the conditional out-of-combat bonus is reported separately.
    FLDDemoRational HPRegenPerSec;
    FLDDemoRational MPRegenPerSec;
    int64 CritChanceBp = 0;
    int64 CooldownHasteP2 = 0;
};

struct FLDAttributeTerms
{
    // Flat is in the attribute's author unit; AdditiveRatio is dimensionless.
    FLDDemoRational Flat;
    FLDDemoRational AdditiveRatio;
};

struct FLDAttributeContributions
{
    ELDProjectedAttribute Attribute = ELDProjectedAttribute::Count;
    FLDDemoRational Base;
    FLDAttributeTerms LevelGrowth;
    FLDAttributeTerms PermanentChoices;
    FLDAttributeTerms Passives;
    FLDAttributeTerms Equipment;
    // (Base + every Flat) x (1 + every AdditiveRatio), exact; only attack and move speed clamp.
    FLDDemoRational Final;
    bool bClamped = false;
};

struct FLDOutOfCombatRegenTerm
{
    FString AbilityId;
    uint8 Rank = 0;
    int64 DelayMs = 0;
    FLDDemoRational HPPerSec;
};

struct FLDEquipmentPassiveFamily
{
    FString Family;
    FString EquipmentId;
    // Ascending EquippedItems indices holding this family; the family still applies once.
    TArray<int32> EquippedIndices;
};

struct FLDAttributeProjection
{
    FLDProjectedAttributes Final;
    // One row per ELDProjectedAttribute, in enum order.
    TArray<FLDAttributeContributions> Contributions;
    TOptional<FLDOutOfCombatRegenTerm> OutOfCombatHPRegen;
    // One entry per family, sorted by family.
    TArray<FLDEquipmentPassiveFamily> EquipmentPassives;
};

struct FLDAttributeProjectionResult
{
    TOptional<FLDAttributeProjection> Value;
    FLDAttributeProjectionFailure Error;

    bool IsSuccess() const { return Value.IsSet() && Error.Code == ELDAttributeProjectionError::None; }
};

// Selects only the quantization policy: HP refuses positive-to-zero, MP accepts it.
enum class ELDResourcePool : uint8 { None, HP, MP };

struct FLDResourceMaxChangeInput
{
    ELDResourcePool Pool = ELDResourcePool::None;
    TOptional<int64> OldCurrentP2;
    TOptional<int64> OldMaxP2;
    TOptional<int64> NewMaxP2;
};

struct FLDResourceMaxChange
{
    int64 NewCurrentP2 = 0;
};

struct FLDResourceMaxChangeResult
{
    TOptional<FLDResourceMaxChange> Value;
    FLDAttributeProjectionFailure Error;

    bool IsSuccess() const { return Value.IsSet() && Error.Code == ELDAttributeProjectionError::None; }
};

namespace LDAttributeProjection
{
    // Pure projection over the borrowed catalog: no world, clock, randomness, resource store or commit.
    // Passive ranks plus card counts must fit min(Level - 1, MaxChoices); this bounds only the
    // sources visible here and does not replace the growth domain's complete build validation.
    LOCKDOWN_API FLDAttributeProjectionResult Project(
        const FLDAttributeBuildValues& Build, const FLDDemoContentSnapshot& Catalog);
    // One final-maximum conversion, floor(OldCurrent x NewMax / OldMax); life eligibility stays with the caller.
    LOCKDOWN_API FLDResourceMaxChangeResult ResourceMaxChange(
        const FLDResourceMaxChangeInput& Input, const FLDDemoContentSnapshot& Catalog);
}
