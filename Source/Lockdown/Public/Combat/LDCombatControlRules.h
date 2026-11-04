#pragma once
#include "CoreMinimal.h"
#include "Combat/LDCombatEvaluationTypes.h"

struct FLDDemoPolicies;
struct FLDDemoEffectDefinition;

// Voluntary actions the control matrix judges; the same rule serves human, opponent AI and camp callers.
enum class ELDCombatControlAction : uint8 { Move, Displacement, Basic, Cast, Gear };

// The next basic attack's added hit, armed by one release and consumed by the first basic that lands.
struct FLDCombatEmpower
{
    FGuid EffectId;
    FName SourceDefinitionId;
    int64 BaseP2 = 0;
    int64 ADRatioBp = 0;
    int64 APRatioBp = 0;
    ELDDemoDamageType DamageType = ELDDemoDamageType::Physical;
    int64 ExpiresAtMs = 0;
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
    bool bFromBehind = false;
    int64 DamageP2 = 0;
    double DistanceCm = 0;
};

namespace LDCombatControl
{
    // Matrix: Root blocks the POLICY.RootBlocks actions (Move, Dash as Displacement); Disarm blocks Basic;
    // Stun blocks every voluntary action. Expiry is half-open at ExpiresAtMs.
    // Disarm and stun instances reuse FLDCombatRoot: keyed by effect, carrying their source and expiry, so another rule
    // (for example a bonus against bodies this source controls) can read who applied them.
    LOCKDOWN_API bool Blocks(const TArray<FLDCombatRoot>& Roots, const TArray<FLDCombatRoot>& Disarms,
        const TArray<FLDCombatRoot>& Stuns, int64 AtMs, const FLDDemoPolicies& Policies, ELDCombatControlAction Action);
    // ELDCombatControlBlock bits of one status kind, for display only.
    LOCKDOWN_API uint8 DisarmBlockBits();
    LOCKDOWN_API uint8 StunBlockBits();
    // Adds a new instance (never renews the same effect); returns whether it was added.
    LOCKDOWN_API bool Apply(TArray<FLDCombatRoot>& Instances, const FLDCombatRoot& Instance, int64 AtMs);
    // Drops expired disarm and stun instances; roots keep their LDTactical rule. Returns whether anything ended.
    LOCKDOWN_API bool ExpireDisarmsAndStuns(TArray<FLDCombatRoot>& Disarms, TArray<FLDCombatRoot>& Stuns, int64 AtMs);

    // Whether SourceFoot lies in the rear sector of a body standing at TargetFoot with authority facing TargetYawDeg:
    // the angle between the target's back direction and the planar direction to the source is at most RearArc/2.
    // Coincident feet are never "behind".
    LOCKDOWN_API bool IsBehind(const FVector& TargetFoot, double TargetYawDeg, const FVector& SourceFoot,
        const FLDDemoRational& RearArcDeg);

    // Takes the armed empowerment when it is still active at AtMs; an expired one is dropped and nothing is taken.
    LOCKDOWN_API TOptional<FLDCombatEmpower> ConsumeEmpower(TOptional<FLDCombatEmpower>& Armed, int64 AtMs);

    // Whether this contact may trigger the thorns effect: positive damage, not itself a reflection, a primary basic
    // hit (or an ability hit when the effect allows it), within reach, not from behind when rear hits are exempt,
    // and at least IntervalMs after this source's last reflection to the same attacker.
    LOCKDOWN_API bool ThornsTriggers(const FLDDemoEffectDefinition& Thorns, const FLDThornsContact& Contact,
        const TArray<FLDThornsMark>& Marks, const FString& SourceKey, const FGuid& Attacker, int64 AtMs);
    // Reflection base in P2: (Base + Armor x ArmorRatio), doubled by CriticalMultiplier when the hit was critical.
    LOCKDOWN_API bool ThornsBaseP2(const FLDDemoEffectDefinition& Thorns, int64 OwnerArmorP2, bool bCritical,
        int64& OutP2, FString& Error);
    // Records a reflection and forgets marks whose window has passed.
    LOCKDOWN_API void MarkThorns(TArray<FLDThornsMark>& Marks, const FString& SourceKey, const FGuid& Attacker,
        int64 AtMs, int64 IntervalMs);

    // Planar jump plan from Foot toward Point, clamped to RangeCm. Zero distance is a jump in place.
    LOCKDOWN_API bool PlanJump(const FVector& Foot, const FVector& Point, double RangeCm,
        FVector& OutDirection, double& OutDistanceCm);
}
