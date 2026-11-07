#pragma once
#include "CoreMinimal.h"
#include "Combat/LDCombatEvaluationTypes.h"

struct FLDDemoPolicies;
struct FLDDemoEffectDefinition;

// Voluntary actions the control matrix judges; the same rule serves human, opponent AI and camp callers.
// Cast is any Q/W/D activation; Skill is a learned Q/W (#147 silence blocks it, the innate dash is no skill), so a Q/W
// caller checks both.
enum class ELDCombatControlAction : uint8 { Move, Displacement, Basic, Cast, Skill };

// Every control or slow a body can receive; all of them are committed through ALDCombatDirector::CommitControl.
// Hard controls (Root, Stun, Disarm, Taunt, Silence) diminish; Knockback and Slow never do.
enum class ELDControlKind : uint8 { Root, Stun, Disarm, Taunt, Knockback, Slow, Silence };

// One control or slow about to be committed on a body life.
struct FLDControlApplication
{
    ELDControlKind Kind = ELDControlKind::Root;
    FGuid EffectId;
    FGuid SourceEntity;
    FLDCombatBodyRef SourceBody;
    // Contender credited with the control (#112 assists); invalid for unowned or camp sources.
    FGuid CreditOwner;
    // Undiminished duration; CommitControl applies the hard-control chain.
    int64 DurationMs = 0;
    // Slow: move-speed ratio.
    int64 RatioBp = 0;
};

// Hard-control diminishing returns of one body (#112 cross-session contract 6): a hard control landing while a previous one is active
// or within 2 s after the latest one ended lasts 60%, then 30% (third and later), never below 0.2 s; the chain is
// shared by every source and clears after 2 s without a new hard control.
struct FLDHardControlChain
{
    int32 Count = 0;
    int64 WindowEndMs = 0;
};

// The next basic attacks' added hit, armed by one release and consumed by the basics that land (Charges of them, #147).
struct FLDCombatEmpower
{
    FGuid EffectId;
    FName SourceDefinitionId;
    int64 BaseP2 = 0;
    int64 ADRatioBp = 0;
    int64 APRatioBp = 0;
    ELDDemoDamageType DamageType = ELDDemoDamageType::Physical;
    int64 ExpiresAtMs = 0;
    // #147: the landed basics this release empowers, and the cleave they carry (ratio and radius).
    int32 Charges = 1;
    int64 CleaveRatioBp = 0;
    double CleaveRadiusCm = 0;
    // The timed bonus (keyed by EffectId) held while charges remain; ended with the last charge.
    bool bChargeBonus = false;
};

// The last reflection one thorns source of a body dealt to one attacker.
struct FLDThornsMark
{
    FString SourceKey;
    FGuid Attacker;
    int64 AtMs = 0;
};

// What one committed hit offers a thorns source of its victim.
struct FLDThornsContact
{
    ELDCombatHitClass HitClass = ELDCombatHitClass::None;
    bool bReflected = false;
    bool bCritical = false;
    int64 DamageP2 = 0;
    double DistanceCm = 0;
};

namespace LDCombatControl
{
    // Matrix: Root blocks the POLICY.RootBlocks actions (Move, Dash as Displacement); Disarm blocks Basic; Silence
    // (#147) blocks Skill; Stun blocks every voluntary action. Expiry is half-open at ExpiresAtMs.
    // Disarm and stun instances reuse FLDCombatRoot: keyed by effect, carrying their source and expiry, so another rule
    // (for example a bonus against bodies this source controls) can read who applied them.
    LOCKDOWN_API bool Blocks(const TArray<FLDCombatRoot>& Roots, const TArray<FLDCombatRoot>& Disarms,
        const TArray<FLDCombatRoot>& Stuns, int64 AtMs, const FLDDemoPolicies& Policies, ELDCombatControlAction Action,
        const TArray<FLDCombatRoot>* Silences = nullptr);
    // ELDCombatControlBlock bits of one status kind, for display only.
    LOCKDOWN_API uint8 DisarmBlockBits();
    LOCKDOWN_API uint8 StunBlockBits();
    LOCKDOWN_API uint8 SilenceBlockBits();
    // Adds a new instance (never renews the same effect); returns whether it was added.
    LOCKDOWN_API bool Apply(TArray<FLDCombatRoot>& Instances, const FLDCombatRoot& Instance, int64 AtMs);
    // Drops expired disarm and stun instances; roots keep their LDTactical rule. Returns whether anything ended.
    LOCKDOWN_API bool ExpireDisarmsAndStuns(TArray<FLDCombatRoot>& Disarms, TArray<FLDCombatRoot>& Stuns, int64 AtMs,
        TArray<FLDCombatRoot>* Silences = nullptr);

    // Takes one charge of the armed empowerment when it is still active at AtMs (the record ends with its last
    // charge); an expired one is dropped and nothing is taken. The returned copy's Charges is what remains.
    LOCKDOWN_API TOptional<FLDCombatEmpower> ConsumeEmpower(TOptional<FLDCombatEmpower>& Armed, int64 AtMs);

    // Whether this contact may trigger the thorns effect: positive damage, not itself a reflection, a primary basic
    // hit within reach, and at least IntervalMs after this source's last reflection to the same attacker.
    LOCKDOWN_API bool ThornsTriggers(const FLDDemoEffectDefinition& Thorns, const FLDThornsContact& Contact,
        const TArray<FLDThornsMark>& Marks, const FString& SourceKey, const FGuid& Attacker, int64 AtMs);
    // Reflection base in P2: (Base + Armor x ArmorRatio + MaxHP x MaxHPRatio), multiplied by CriticalMultiplier when the
    // hit was critical.
    LOCKDOWN_API bool ThornsBaseP2(const FLDDemoEffectDefinition& Thorns, int64 OwnerArmorP2, int64 OwnerMaxHPP2, bool bCritical,
        int64& OutP2, FString& Error);
    // Records a reflection and forgets marks whose window has passed.
    LOCKDOWN_API void MarkThorns(TArray<FLDThornsMark>& Marks, const FString& SourceKey, const FGuid& Attacker,
        int64 AtMs, int64 IntervalMs);

    LOCKDOWN_API bool IsHardControl(ELDControlKind Kind);
    // Duration of a hard control landing at NowMs after the chain's diminishing returns; advances the chain.
    // Non-positive durations are returned unchanged and leave the chain untouched.
    LOCKDOWN_API int64 DiminishHardControl(FLDHardControlChain& Chain, int64 NowMs, int64 DurationMs);
}
