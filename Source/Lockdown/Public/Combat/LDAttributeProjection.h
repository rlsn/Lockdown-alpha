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

// #198 B: one installed (enabled) component of a build as Combat executes it. BoundActive/Element are the install
// parameters of an ActiveElement binding (B18) and empty/None otherwise.
struct FLDInstalledComponentValue
{
    FName ComponentId;
    FName BoundActive;
    ELDDemoElement Element = ELDDemoElement::None;
    // #198 F: a mod/connector's install target (an active, a component, the innate D or the holder itself) and C12's
    // repeated base component.
    ELDComponentTargetKind TargetKind = ELDComponentTargetKind::None;
    FName TargetId;
    FName BoundComponent;

    bool operator==(const FLDInstalledComponentValue& Other) const
    {
        return ComponentId == Other.ComponentId && BoundActive == Other.BoundActive && Element == Other.Element
            && TargetKind == Other.TargetKind && TargetId == Other.TargetId && BoundComponent == Other.BoundComponent;
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
    // #198 B: installed components, each definition at most once, sorted by id. Their stat_bonus effects form the
    // Components term; low_hp_stat_bonus/control_damage_amp become conditional terms; every component is listed in
    // FLDAttributeProjection::InstalledComponents for Combat's trigger handlers.
    TArray<FLDInstalledComponentValue> Components;
    // DEV test layer (#78): one extra additive term per stat on top of base, growth and the allocated points, reported
    // as its own contribution row. Empty outside a DEV test session. Each stat at most once, nonnegative, never
    // ADBonusRatio; points in P2, ratios in basis points, range in Cm (Amount 0). A final value past its catalog
    // maximum refuses the projection.
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
    // Always-on rates.
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
    // #198: the allocated stat points times their per-point bonuses.
    FLDAttributeTerms PermanentChoices;
    // #198 B: the always-on stat_bonus effects of installed components (grown fixed amounts).
    FLDAttributeTerms Components;
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

// A held low-HP trigger (low_hp_stat_bonus, B15): its stats apply only while current HP is strictly below ThresholdBp of
// max HP.
struct FLDLowHPBonusTerm
{
    int64 ThresholdBp = 0;
    TArray<FLDDemoStatBonus> Stats;
};

// #198 B07: the holder's damage against a body that, before the hit, is move-slowed, hard-controlled or knocked back
// gains RatioBp (one term of the unified damage bonus pool).
struct FLDControlDamageAmpTerm
{
    FName ComponentId;
    int64 RatioBp = 0;
};

// #198 B: one installed component as Combat reads it (CollectPassiveEffects): its effects grown for Level.
struct FLDInstalledComponentTerm
{
    FName ComponentId;
    FName BoundActive;
    ELDDemoElement Element = ELDDemoElement::None;
    // #198 F install target and C12's repeated base (FLDInstalledComponentValue).
    ELDComponentTargetKind TargetKind = ELDComponentTargetKind::None;
    FName TargetId;
    FName BoundComponent;
    // The holder's level when projected; a level-up reprojects the build.
    uint8 Level = 0;
};

struct FLDAttributeProjection
{
    FLDProjectedAttributes Final;
    // One row per ELDProjectedAttribute, in enum order.
    TArray<FLDAttributeContributions> Contributions;
    // #198 B conditional component terms; Combat applies them from body state, the build projection never adds them.
    TOptional<FLDLowHPBonusTerm> LowHPBonus;
    TOptional<FLDControlDamageAmpTerm> ControlAmp;
    // Every installed component, sorted by id.
    TArray<FLDInstalledComponentTerm> InstalledComponents;
};

struct FLDAttributeProjectionResult
{
    TOptional<FLDAttributeProjection> Value;
    FLDAttributeProjectionFailure Error;

    bool IsSuccess() const { return Value.IsSet() && Error.Code == ELDAttributeProjectionError::None; }
};

// Temporary terms of one body at one instant: active timed bonuses, an active conditional low-HP bonus and the strongest
// slow (move speed).
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
    // Pure projection over the borrowed catalog: no world, clock, randomness, resource store or commit. The allocated
    // points are summed as flat and additive-percent terms (never multiplied per point); bounding them by the points
    // earned stays the growth domain's build validation. #198 B: each installed component is a catalog base component
    // listed once (an unknown, duplicate, non-base or misbound one refuses); its stat_bonus effects (grown with
    // LDSkillGrowth::GrowEffect for Level, which grows no stat_bonus amount) form the Components term, low_hp_stat_bonus and
    // control_damage_amp become LowHPBonus/ControlAmp (at most one of each) and every component is listed, sorted by id.
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
