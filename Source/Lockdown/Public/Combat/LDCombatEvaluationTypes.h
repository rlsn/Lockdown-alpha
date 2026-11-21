#pragma once

#include "CoreMinimal.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Framework/LDMatchLifecycleTypes.h"

// #198: cleave (the only basic secondary) is retired, so a basic hit is always the primary one.
enum class ELDCombatHitClass : uint8 { None, BasicPrimary, Ability };
enum class ELDCombatEvaluationError : uint8 { None, Definition, Numeric, Domain, Overflow };

struct FLDCombatEvaluationFailure
{
    ELDCombatEvaluationError Code = ELDCombatEvaluationError::None;
    FString Field;
    FString Message;
};

// Spec §3.7: one damage factor source of a hit. Every source is its own multiplier (1 + Bp/10000); entries with the same
// Key (copies of one card, or one fixed source) add into one multiplier first (LDTactical::DamageFactorProduct).
struct FLDDamageFactor
{
    FName Key;
    int64 Bp = 0;
};

struct FLDDamageDefinitionValues
{
    // P2 points and R4 coefficients from an already-resolved definition/rank.
    FLDDamageDefinitionValues(int64 InBaseP2, int64 InADRatioBp, int64 InAPRatioBp, ELDDemoDamageType InDamageType,
        int64 InMaxHPRatioBp = 0, int64 InAdaptiveRatioBp = 0)
        : BaseP2(InBaseP2), ADRatioBp(InADRatioBp), APRatioBp(InAPRatioBp), DamageType(InDamageType),
          MaxHPRatioBp(InMaxHPRatioBp), AdaptiveRatioBp(InAdaptiveRatioBp) {}

    int64 BaseP2;
    int64 ADRatioBp;
    int64 APRatioBp;
    ELDDemoDamageType DamageType;
    // R4 coefficient of the source's own max HP (P2), alongside AD and AP (active_seethe).
    int64 MaxHPRatioBp;
    // R4 adaptive coefficient: BonusAD * 1.4 >= AP -> BonusAD * AdaptiveRatioBp (Physical), else AP * AdaptiveRatioBp (Magical).
    int64 AdaptiveRatioBp;
};

struct FLDDamageEvaluationInput
{
    TOptional<FLDDamageDefinitionValues> Definition;
    ELDCombatHitClass HitClass = ELDCombatHitClass::None;
    TOptional<bool> bCritical;
    // A source attribute may be absent only when its resolved coefficient is zero.
    TOptional<int64> SourceADP2;
    TOptional<int64> SourceAPP2;
    TOptional<int64> SourceBonusADP2;
    // The source's max HP snapshotted at release; required only when MaxHPRatioBp is nonzero.
    TOptional<int64> SourceMaxHPP2;
    // Resistance is the matching armor or magic-resist point value, not a reduction ratio.
    TOptional<int64> TargetResistanceP2;
    TOptional<int64> TargetHPP2;
    TOptional<int64> TargetShieldP2;
    TOptional<bool> bTargetProtected;
    // Spec §3.7: the product of the hit's damage factors (LDTactical::DamageFactorProduct), 10000 = x1, applied once to the
    // raw amount after the critical multiplier and before resistance. A primary basic crits; an ability only under Y09.
    int64 DamageFactorBp = 10000;
    // Delivery share of the raw amount (a ground impact's outer ring); 10000 = all of it.
    int64 DeliveryScaleBp = 10000;
    // A package multiplier of the raw amount (a skill branch's share), applied with the delivery share; 1..100000.
    int64 PackageScaleBp = 10000;
    // The activated thick-skin keystone cuts after the ordinary primary guard.
    int64 KeystoneGuardP2 = 0;
    int64 KeystoneGuardCapBp = 0;
    // #198 S16: the target's stance cuts this share of the hit after resistance and delivery scaling, before the guard.
    int64 BraceCutBp = 0;
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
    // The amount BraceCutBp actually took off (floored P2; zero for a protected target).
    int64 BraceCutP2 = 0;
    int64 KeystoneGuardCutP2 = 0;
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

// One independent shield entry in a body's multi-source shield stack (shortest remaining duration absorbs first).
struct FLDActiveShield
{
    FGuid EffectId;
    FGuid SourceEntity;
    int64 AmountP2 = 0;
    int64 GrantedP2 = 0;
    int64 ExpiresAtMs = 0;
};

// Legacy single-slot shield struct kept for pure status-rule compatibility.
struct FLDCombatShield
{
    FGuid EffectId;
    FGuid SourceEntity;
    int64 ExpiresAtMs = 0;
};

// One slow instance (EFFECT.Slow) on one target life; the strongest active instance projects into move speed.
struct FLDCombatSlow
{
    FGuid EffectId;
    FGuid SourceEntity;
    int64 RatioBp = 0;
    int64 ExpiresAtMs = 0;
};

// One timed self bonus (EFFECT.TimedStatBonus); a recast of the same source definition replaces it.
struct FLDCombatTimedBonus
{
    FGuid EffectId;
    FName SourceDefinition;
    TArray<FLDDemoStatBonus> Stats;
    int64 ExpiresAtMs = 0;
};

struct FLDStatusExpiry
{
    bool bRootsChanged = false;
    bool bShieldChanged = false;
    bool bTauntChanged = false;
};

// One taunt on a contender life (EFFECT taunt): until it ends the victim may only basic-attack the taunter's body.
struct FLDCombatTaunt
{
    FGuid EffectId;
    FGuid SourceEntity;
    FLDCombatBodyRef SourceBody;
    int64 ExpiresAtMs = 0;
};

// A hostile body inside a taunt's radius at the release moment, as Combat read it.
struct FLDTauntCandidate
{
    FLDCombatBodyRef Body;
    FVector FootCm = FVector::ZeroVector;
    bool bPendingDeath = false;
    bool bProtected = false;
    // Observation of the caster from this candidate; only Visible may be taunted.
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
