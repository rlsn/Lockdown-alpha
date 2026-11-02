#pragma once

#include "CoreMinimal.h"
#include "Development/LDDemoContentDefinitions.h"

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
    FLDDamageDefinitionValues(int64 InBaseP2, int64 InADRatioBp, int64 InAPRatioBp, ELDDemoDamageType InDamageType)
        : BaseP2(InBaseP2), ADRatioBp(InADRatioBp), APRatioBp(InAPRatioBp), DamageType(InDamageType) {}

    int64 BaseP2;
    int64 ADRatioBp;
    int64 APRatioBp;
    ELDDemoDamageType DamageType;
};

struct FLDDamageEvaluationInput
{
    TOptional<FLDDamageDefinitionValues> Definition;
    ELDCombatHitClass HitClass = ELDCombatHitClass::None;
    TOptional<bool> bCritical;
    // A source attribute may be absent only when its resolved coefficient is zero.
    TOptional<int64> SourceADP2;
    TOptional<int64> SourceAPP2;
    // Resistance is the matching armor or magic-resist point value, not a reduction ratio.
    TOptional<int64> TargetResistanceP2;
    TOptional<int64> TargetHPP2;
    TOptional<int64> TargetShieldP2;
    TOptional<bool> bTargetProtected;
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

struct FLDStatusExpiry
{
    bool bRootsChanged = false;
    bool bShieldChanged = false;
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
