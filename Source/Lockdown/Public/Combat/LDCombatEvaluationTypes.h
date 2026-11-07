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
    // R4 coefficient of the source's own max HP (P2), alongside AD and AP (active_seethe).
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
    // #147 D21 damage bonus pool: every "damage +X%" of this hit (control/low-HP amplification, execute, the affliction
    // amplification of burn) summed into one factor (1 + pool) applied once to the raw amount before resistance. An ability critical adds STAT.AbilityCritMultiplier - 1 to this pool here; a basic critical
    // multiplies by STAT.CritMultiplier before it.
    int64 DamageBonusBp = 0;
    // Remaining multiplier of the raw amount outside the pool (the target's critical damage reduction); 10000 = none.
    int64 AmplifyBp = 10000;
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
