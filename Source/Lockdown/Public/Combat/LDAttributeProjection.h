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
    // 112 §7: empty (nothing upgraded) or one flag per EquippedItems entry; an upgraded entry projects its upgraded stats.
    TArray<bool> EquippedUpgraded;
    // DEV test layer (#78): one extra additive term per stat on top of base, growth, passives and equipment, reported
    // as its own contribution row. Empty outside a DEV test session. Each stat at most once, nonnegative, never
    // ADBonusRatio; points in P2, ratios in basis points, range in Cm (Amount 0). A final value outside its catalog
    // bound refuses the projection exactly like any other source.
    TArray<FLDDemoStatBonus> DevBonuses;
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
    // The DEV test layer (FLDAttributeBuildValues::DevBonuses); zero outside a DEV test session.
    FLDAttributeTerms Dev;
    // Explicit wild-card deductions (positive magnitudes, #112 §4): (Base + Flats - Penalties.Flat) x (1 + Ratios -
    // Penalties.AdditiveRatio); the final value never drops below the attribute's catalog minimum.
    FLDAttributeTerms Penalties;
    // Timed self bonuses, active conditional passives and the strongest slow (a negative ratio); zero in a build projection.
    FLDAttributeTerms Temporary;
    // (Base + every Flat - Penalties) x (1 + every AdditiveRatio - Penalties), exact, clamped to the catalog bounds.
    // With temporary terms, point/basis-point attributes floor to their storage unit and every attribute clamps.
    FLDDemoRational Final;
    bool bClamped = false;
};

struct FLDOutOfCombatRegenTerm
{
    FString AbilityId;
    uint8 Rank = 0;
    int64 DelayMs = 0;
    // Share of the current max HP regenerated per second out of combat (basis points; a whole 0.001 share).
    int64 MaxHPBpPerSec = 0;
    // After an ability hit the always-on HP/MP rates are multiplied for SurgeMs; 0 = none.
    int64 SurgeMs = 0;
    int64 SurgeMultiplierBp = 0;
    // Share of the out-of-combat rate kept during combat time; 0 = none.
    int64 InCombatRatioBp = 0;
};

// Passive whose stats apply only while current HP is strictly below ThresholdBp of max HP.
struct FLDLowHPBonusTerm
{
    FString AbilityId;
    uint8 Rank = 0;
    int64 ThresholdBp = 0;
    TArray<FLDDemoStatBonus> Stats;
    // Omnivamp of every damage hit the owner deals while the bonus is active; 0 = none.
    int64 OmnivampBp = 0;
};

// Passive extra damage of the first damaging action committed after DelayMs out of combat.
struct FLDAmbushDamageTerm
{
    FString AbilityId;
    uint8 Rank = 0;
    int64 DelayMs = 0;
    int64 BaseP2 = 0;
    int64 ADRatioBp = 0;
    int64 APRatioBp = 0;
    ELDDemoDamageType DamageType = ELDDemoDamageType::Magical;
    // Readiness credit of each dash or jump (D dash included); 0 = none.
    int64 DashCreditMs = 0;
    // Both resistances of the struck body shredded by this ratio for this long; 0 = none.
    int64 ShredRatioBp = 0;
    int64 ShredDurationMs = 0;
};

// Passive amplification of the owner's damage against a body under any control or slow.
struct FLDControlDamageAmpTerm
{
    FString AbilityId;
    uint8 Rank = 0;
    int64 RatioBp = 0;
};

struct FLDEquipmentPassiveFamily
{
    FString Family;
    FString EquipmentId;
    // Ascending EquippedItems indices holding this family; the family still applies once.
    TArray<int32> EquippedIndices;
    // 112 §7: an upgraded holder takes precedence, so the family applies its upgraded terms once.
    bool bUpgraded = false;
};

// A learned passive rank whose effects are combat triggers (thorns, cooldown refunds, crits, stacks, reach, mana,
// shields) rather than attribute terms; Combat reads the rank's effects through ALDCombatDirector::CollectPassiveEffects.
struct FLDLearnedTriggerTerm
{
    FString AbilityId;
    uint8 Rank = 0;
    // The owner's level when projected; Combat reads the rank grown for it (#147, a level-up reprojects the build).
    uint8 Level = 0;
};

struct FLDAttributeProjection
{
    FLDProjectedAttributes Final;
    // One row per ELDProjectedAttribute, in enum order.
    TArray<FLDAttributeContributions> Contributions;
    TOptional<FLDOutOfCombatRegenTerm> OutOfCombatHPRegen;
    // Conditional passive terms; Combat applies them from body state, the build projection never adds them.
    TOptional<FLDLowHPBonusTerm> LowHPBonus;
    TOptional<FLDAmbushDamageTerm> Ambush;
    TOptional<FLDControlDamageAmpTerm> ControlAmp;
    // One entry per family, sorted by family.
    TArray<FLDEquipmentPassiveFamily> EquipmentPassives;
    // Learned combat-trigger passives at their learned rank, sorted by ability.
    TArray<FLDLearnedTriggerTerm> LearnedTriggers;
};

struct FLDAttributeProjectionResult
{
    TOptional<FLDAttributeProjection> Value;
    FLDAttributeProjectionFailure Error;

    bool IsSuccess() const { return Value.IsSet() && Error.Code == ELDAttributeProjectionError::None; }
};

// Temporary terms of one body at one instant: active timed bonuses, an active conditional passive and the strongest
// slow (move and attack speed).
struct FLDTemporaryAttributeTerms
{
    TArray<FLDDemoStatBonus> Bonuses;
    int64 MoveSlowBp = 0;
    int64 AttackSlowBp = 0;

    bool IsEmpty() const { return Bonuses.IsEmpty() && MoveSlowBp == 0 && AttackSlowBp == 0; }
    bool operator==(const FLDTemporaryAttributeTerms& Other) const;
    bool operator!=(const FLDTemporaryAttributeTerms& Other) const { return !(*this == Other); }
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
    // Passive ranks plus card counts must fit min(Level, MaxChoices) (the opening choice plus one per level-up); this bounds only the
    // sources visible here and does not replace the growth domain's complete build validation.
    LOCKDOWN_API FLDAttributeProjectionResult Project(
        const FLDAttributeBuildValues& Build, const FLDDemoContentSnapshot& Catalog);
    // The same formula over a build projection plus temporary terms: (Base + Flats + TemporaryFlat) x (1 + Ratios +
    // TemporaryRatio - MoveSlow). Point and basis-point results floor; every attribute clamps to its catalog bounds
    // (a temporary term never refuses). A projection without contribution rows (camp members) uses Final as its base.
    // Resource maxima never take temporary terms.
    LOCKDOWN_API FLDAttributeProjectionResult ApplyTemporary(const FLDAttributeProjection& Build,
        const FLDTemporaryAttributeTerms& Terms, const FLDDemoContentSnapshot& Catalog);
    // One final-maximum conversion, floor(OldCurrent x NewMax / OldMax); life eligibility stays with the caller.
    LOCKDOWN_API FLDResourceMaxChangeResult ResourceMaxChange(
        const FLDResourceMaxChangeInput& Input, const FLDDemoContentSnapshot& Catalog);
}
