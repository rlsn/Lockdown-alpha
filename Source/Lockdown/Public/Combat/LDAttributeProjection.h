#pragma once

#include "CoreMinimal.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Development/LDGrowthProtocol.h"

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

// #198: the points a build allocated to one catalog stat; each point adds the stat's PerPoint bonuses.
struct FLDStatPointsValue
{
    FLDStatPointsValue(FString InStatId, int32 InPoints)
        : StatId(MoveTemp(InStatId)), Points(InPoints) {}

    FString StatId;
    int32 Points;
};

// One installed card of a build as Combat executes it (spec §6.1): its definition, serial and install target.
struct FLDInstalledCardValue
{
    FName CardId;
    uint16 Serial = 0;
    ELDCardTargetKind TargetKind = ELDCardTargetKind::None;
    // Skill: the learned active it sits on.
    FName Active;
    // Y25: the chosen element.
    ELDDemoElement Element = ELDDemoElement::None;

    bool operator==(const FLDInstalledCardValue& Other) const
    {
        return CardId == Other.CardId && Serial == Other.Serial && TargetKind == Other.TargetKind && Active == Other.Active
            && Element == Other.Element;
    }
};

struct FLDAttributeBuildValues
{
    // Every part is explicit; an empty array states that no point was allocated.
    FLDAttributeBuildValues(uint8 InLevel, TArray<FLDStatPointsValue> InStatPoints)
        : Level(InLevel), StatPoints(MoveTemp(InStatPoints)) {}

    uint8 Level;
    // Each catalog stat at most once; a positive count of points.
    TArray<FLDStatPointsValue> StatPoints;
    // Installed cards, distinct serials sorted ascending (copies of one definition are separate rows). Y13 converts the
    // projected armor into the Cards term of max HP; every card is listed in FLDAttributeProjection::InstalledCards for
    // Combat's handlers.
    TArray<FLDInstalledCardValue> Cards;
    // The match growth of grow cards (Y19 max HP, Y20 attack and power, Y21 resistances), owned by Combat like DevBonuses:
    // one nonnegative flat term per stat, added to the Cards term.
    TArray<FLDDemoStatBonus> CardGrowth;
    // DEV test layer (#78): one extra additive term per stat on top of base, growth and the allocated points, reported
    // as its own contribution row. Empty outside a DEV test session. Each stat at most once, nonnegative, never
    // ADBonusRatio; points in P2, ratios in basis points, range in Cm (Amount 0). A final value past its catalog
    // maximum refuses the projection.
    TArray<FLDDemoStatBonus> DevBonuses;
};

enum class ELDProjectedAttribute : uint8
{
    MaxHP, AD, AP, Armor, MagicResist, AttackSpeed, MoveSpeed, AttackRange,
    HPRegen, MomentumRegen, CritChance, CooldownHaste, Count
};

struct FLDProjectedAttributes
{
    int64 MaxHPP2 = 0;
    int64 ADP2 = 0;
    int64 APP2 = 0;
    int64 ArmorP2 = 0;
    int64 MagicResistP2 = 0;
    // Attacks per second; the value for FLDBasicTimingInput::CurrentAttackSpeed.
    FLDDemoRational AttackSpeed;
    FLDDemoRational MoveSpeedCmPerSec;
    // Basic-attack footpoint XY centre distance; not an ability range or cleave radius.
    FLDDemoRational AttackRangeCm;
    // Always-on rates.
    FLDDemoRational HPRegenPerSec;
    // In-combat momentum gain per second (MOMENTUM.CombatPerSec plus the brains points).
    FLDDemoRational MomentumRegenPerSec;
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
    // #198: the allocated stat points times their per-point bonuses.
    FLDAttributeTerms PermanentChoices;
    // Installed cards: Y13's armor conversion and the grow cards' match growth.
    FLDAttributeTerms Cards;
    // The DEV test layer (FLDAttributeBuildValues::DevBonuses); zero outside a DEV test session.
    FLDAttributeTerms Dev;
    // Timed self bonuses, an active conditional low-HP bonus and the strongest slow (a negative ratio); zero in a build
    // projection.
    FLDAttributeTerms Temporary;
    // (Base + every Flat) x (1 + every AdditiveRatio), exact, clamped to the catalog bounds. With temporary terms,
    // point/basis-point attributes floor to their storage unit and every attribute clamps.
    FLDDemoRational Final;
    bool bClamped = false;
};

struct FLDKeystoneActivation
{
    bool bPhysique = false;
    bool bThickSkin = false;
    bool bStrength = false;
    bool bBrains = false;
    bool bAgility = false;
};

// One installed card as Combat reads it (CollectPassiveEffects): its definition, serial, target and the holder's level.
struct FLDInstalledCardTerm
{
    FName CardId;
    uint16 Serial = 0;
    ELDCardTargetKind TargetKind = ELDCardTargetKind::None;
    FName Active;
    ELDDemoElement Element = ELDDemoElement::None;
    // The holder's level when projected; a level-up reprojects the build.
    uint8 Level = 0;
};

struct FLDAttributeProjection
{
    FLDProjectedAttributes Final;
    // Activated from allocated point counts, independently of stat caps and unspent points.
    FLDKeystoneActivation Keystones;
    // One row per ELDProjectedAttribute, in enum order.
    TArray<FLDAttributeContributions> Contributions;
    // Y15: attack-speed ratio per missing HP ratio (copies add); Combat applies it from current HP as a temporary term.
    int64 MissingHPAttackSpeedBp = 0;
    // Every installed card, sorted by serial.
    TArray<FLDInstalledCardTerm> InstalledCards;
};

struct FLDAttributeProjectionResult
{
    TOptional<FLDAttributeProjection> Value;
    FLDAttributeProjectionFailure Error;

    bool IsSuccess() const { return Value.IsSet() && Error.Code == ELDAttributeProjectionError::None; }
};

// Temporary terms of one body at one instant: active timed bonuses, an active conditional low-HP bonus and the strongest
// slow (move speed).
struct LOCKDOWN_API FLDTemporaryAttributeTerms
{
    TArray<FLDDemoStatBonus> Bonuses;
    int64 MoveSlowBp = 0;
    int64 AttackSlowBp = 0;

    bool IsEmpty() const { return Bonuses.IsEmpty() && MoveSlowBp == 0 && AttackSlowBp == 0; }
    bool operator==(const FLDTemporaryAttributeTerms& Other) const;
    bool operator!=(const FLDTemporaryAttributeTerms& Other) const { return !(*this == Other); }
};

// Selects the quantization policy: HP refuses positive-to-zero. Momentum has a fixed maximum and never converts.
enum class ELDResourcePool : uint8 { None, HP };

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
    // Pure projection over the borrowed catalog: no world, clock, randomness, resource store or commit. The allocated
    // points are summed as flat and additive-percent terms (never multiplied per point); bounding them by the points
    // earned stays the growth domain's build validation. Each installed card is a catalog card on a target of its form, listed
    // once by serial (an unknown, repeated or misplaced one refuses); the match growth and Y13's armor conversion form the
    // Cards term, Y15 becomes MissingHPAttackSpeedBp and every card is listed, sorted by serial.
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
