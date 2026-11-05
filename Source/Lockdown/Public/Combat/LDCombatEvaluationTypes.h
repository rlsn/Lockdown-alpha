#pragma once

#include "CoreMinimal.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Framework/LDMatchLifecycleTypes.h"

enum class ELDCombatHitClass : uint8 { None, BasicPrimary, BasicSecondary, Ability };
enum class ELDCombatEvaluationError : uint8 { None, Definition, Numeric, Domain, Overflow };

struct FLDCombatEvaluationFailure
{
    ELDCombatEvaluationError Code = ELDCombatEvaluationError::None;
    FString Field;
    FString Message;
};

struct FLDDamageDefinitionValues
{
    // P2 points and R4 coefficients from an already-resolved definition/rank.
    FLDDamageDefinitionValues(int64 InBaseP2, int64 InADRatioBp, int64 InAPRatioBp, ELDDemoDamageType InDamageType,
        int64 InMaxHPRatioBp = 0)
        : BaseP2(InBaseP2), ADRatioBp(InADRatioBp), APRatioBp(InAPRatioBp), DamageType(InDamageType), MaxHPRatioBp(InMaxHPRatioBp) {}

    int64 BaseP2;
    int64 ADRatioBp;
    int64 APRatioBp;
    ELDDemoDamageType DamageType;
    // R4 coefficient of the source's own max HP (P2), alongside AD and AP (active_body_slam).
    int64 MaxHPRatioBp;
};

struct FLDDamageEvaluationInput
{
    TOptional<FLDDamageDefinitionValues> Definition;
    ELDCombatHitClass HitClass = ELDCombatHitClass::None;
    TOptional<bool> bCritical;
    // A source attribute may be absent only when its resolved coefficient is zero.
    TOptional<int64> SourceADP2;
    TOptional<int64> SourceAPP2;
    // The source's max HP snapshotted at release; required only when MaxHPRatioBp is nonzero.
    TOptional<int64> SourceMaxHPP2;
    // Resistance is the matching armor or magic-resist point value, not a reduction ratio.
    TOptional<int64> TargetResistanceP2;
    TOptional<int64> TargetHPP2;
    TOptional<int64> TargetShieldP2;
    TOptional<bool> bTargetProtected;
    // Conditional multiplier of the raw amount before resistance: the product of every conditional amplification
    // (passive_public_shame, rear strike, passive_eagle_eye distance, a critical reduction of the target); 10000 = none.
    int64 AmplifyBp = 10000;
    // Damage reduction of the target applied after resistance and before the shield (active_porcelain_flop); 0 = none.
    int64 MitigateBp = 0;
};

struct FLDDamageEvaluation
{
    // Post-resistance, floored P2; protection makes the effective amount zero.
    int64 DamageP2 = 0;
    int64 ShieldAbsorbedP2 = 0;
    int64 HPLostP2 = 0;
    int64 OverkillP2 = 0;
    int64 RemainingHPP2 = 0;
    int64 RemainingShieldP2 = 0;
    // The post-resistance amount before MitigateBp (what a damage guard counts toward its trigger).
    int64 PreMitigationP2 = 0;
};

struct FLDDamageEvaluationResult
{
    TOptional<FLDDamageEvaluation> Value;
    FLDCombatEvaluationFailure Error;

    bool IsSuccess() const { return Value.IsSet() && Error.Code == ELDCombatEvaluationError::None; }
};

// One Root instance (EFFECT.Root), keyed by its effect instance on one target life.
struct FLDCombatRoot
{
    FGuid EffectId;
    FGuid SourceEntity;
    int64 ExpiresAtMs = 0;
};

// The single barrier record (EFFECT.ShieldPolicy); the remaining amount is the resource ShieldP2.
struct FLDCombatShield
{
    FGuid EffectId;
    FGuid SourceEntity;
    int64 ExpiresAtMs = 0;
};

// One slow instance (EFFECT.Slow) on one target life; the strongest active instance projects into move speed.
// AttackRatioBp is an attack-speed cut carried by the same instance (passive_iron_crotch Lv3); the strongest one projects too.
struct FLDCombatSlow
{
    FGuid EffectId;
    FGuid SourceEntity;
    int64 RatioBp = 0;
    int64 ExpiresAtMs = 0;
    int64 AttackRatioBp = 0;
    // The authored duration, so a refresh by the same source (active_barrage_rant) restarts it whole.
    int64 DurationMs = 0;
};

// One timed self bonus (EFFECT.TimedStatBonus); a recast of the same source definition replaces it.
struct FLDCombatTimedBonus
{
    FGuid EffectId;
    FName SourceDefinition;
    TArray<FLDDemoStatBonus> Stats;
    int64 ExpiresAtMs = 0;
};

// One resistance shred (breach) on a target life. Flat terms count once per stack; ratio terms do not stack.
// The same Key and source refresh it (adding Stacks up to MaxStacks); different keys or sources coexist.
// Effective resistance = max(0, Resistance - sum of flats) x (1 - min(1, sum of ratios)).
struct FLDCombatBreach
{
    FName Key;
    FGuid SourceEntity;
    int64 ArmorFlatP2 = 0;
    int64 MagicResistFlatP2 = 0;
    int64 ArmorRatioBp = 0;
    int64 MagicResistRatioBp = 0;
    int32 Stacks = 1;
    int32 MaxStacks = 1;
    int64 ExpiresAtMs = 0;
};

// A source's vamp against one target life while the slow that made it lasts (active_slipper_throw Lv3). The mark is
// bound to that slow instance (SlowEffectId): it only counts while the slow is active, so a cleanse or removal of the
// slow ends it, and a refresh of the slow carries ExpiresAtMs along.
struct FLDCombatVampMark
{
    FGuid SourceEntity;
    int64 RatioBp = 0;
    int64 ExpiresAtMs = 0;
    FGuid SlowEffectId;
};

struct FLDStatusExpiry
{
    bool bRootsChanged = false;
    bool bShieldChanged = false;
    bool bTauntChanged = false;
    bool bStealthChanged = false;
};

// One taunt on a contender life (EFFECT taunt): until it ends the victim may only basic-attack the taunter's body.
struct FLDCombatTaunt
{
    FGuid EffectId;
    FGuid SourceEntity;
    FLDCombatBodyRef SourceBody;
    int64 ExpiresAtMs = 0;
    // A decoy lure (item_decoy_wig): the victim walks to DecoyPointCm and may neither attack, cast nor use items.
    bool bDecoy = false;
    FVector DecoyPointCm = FVector::ZeroVector;
};

// Self stealth (EFFECT stealth): enemies observe the body only within RevealRadiusCm (inclusive) while it lasts.
struct FLDCombatStealth
{
    FGuid EffectId;
    int64 ExpiresAtMs = 0;
    double RevealRadiusCm = 0;
    int64 MoveSpeedPenaltyBp = 0;
};

// A hostile body inside a taunt's radius at the release moment, as Combat read it.
struct FLDTauntCandidate
{
    FLDCombatBodyRef Body;
    FVector FootCm = FVector::ZeroVector;
    bool bPendingDeath = false;
    bool bProtected = false;
    // Observation of the caster from this candidate (geometry plus stealth); only Visible may be taunted.
    bool bSeesCaster = false;
};

struct FLDBasicTimingInput
{
    TOptional<FLDDemoRational> BaseAttackSpeed;
    TOptional<FLDDemoRational> CurrentAttackSpeed;
    TOptional<int64> BaseWindupMs;
};

struct FLDBasicTiming
{
    // Durations only; the authority owns deadline construction.
    int64 IntervalMs = 0;
    int64 WindupMs = 0;
};

struct FLDBasicTimingResult
{
    TOptional<FLDBasicTiming> Value;
    FLDCombatEvaluationFailure Error;

    bool IsSuccess() const { return Value.IsSet() && Error.Code == ELDCombatEvaluationError::None; }
};
