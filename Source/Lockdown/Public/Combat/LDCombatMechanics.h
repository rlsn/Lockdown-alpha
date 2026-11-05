#pragma once
#include "CoreMinimal.h"
#include "Combat/LDCombatEvaluationTypes.h"
#include "Combat/LDCombatTypes.h"

// #112 §5 mechanics: heal/shield reduction, shield events, knockback, damage over time, armor/MR breach, ground zones,
// the damage guard and the negative-status count. Pure rules over primitive values; ALDCombatDirector applies them to its
// body records (LDCombatMechanicsDirector.cpp holds the director side). Every timed record is half-open at
// its ExpiresAtMs, like Root and Shield.

// One heal/shield reduction (EFFECT heal_cut) on a target life: healing and shields the life receives are multiplied by
// (1 - RatioBp). Several sources never stack: only the strongest active one applies.
struct FLDCombatHealCut
{
    FGuid EffectId;
    FGuid SourceEntity;
    int64 RatioBp = 0;
    int64 ExpiresAtMs = 0;
};

// Outcome of one shield application against the single shield record (EFFECT.ShieldPolicy=ReplaceIfNotWeaker).
// Refreshed: it replaced a shield that still had a remaining amount; Gained: the record was empty or lapsed.
enum class ELDShieldGrant : uint8 { Rejected, Gained, Refreshed };

// Armor/MR breach on a target life: one record per Key (the applying definition and its source). The damage evaluation
// subtracts the summed flat terms first, then scales by (1 - summed ratio terms); the result never drops below zero.
struct FLDCombatBreach
{
    FName Key;
    FGuid SourceEntity;
    int64 ArmorFlatP2 = 0;
    int64 MRFlatP2 = 0;
    int64 ArmorRatioBp = 0;
    int64 MRRatioBp = 0;
    int32 Stacks = 0;
    int64 ExpiresAtMs = 0;
};

// The six negative-status kinds active_cancel_blast counts (#112 §2): any breach or damage over time counts once.
struct FLDNegativeKinds
{
    bool bRoot = false;
    bool bSlow = false;
    bool bDisarm = false;
    bool bStun = false;
    bool bTaunt = false;
    bool bBreach = false;
};

// A damage over time on one target life (EFFECT dot, breach_stacks). Fixed-count dots tick TicksLeft more times every
// IntervalMs and add one shred step per tick; stacked dots tick until ExpiresAtMs, each stack adds its shred and damage.
// Damage terms and the source's AD/AP are snapshotted when the dot is applied.
struct FLDCombatDot
{
    FName Key;
    FGuid EffectId;
    FGuid SourceEntity;
    FLDCombatBodyRef SourceBody;
    FGuid CreditOwner;
    FLDCombatSourceRef Source;
    int64 BaseP2 = 0;
    int64 ADRatioBp = 0;
    int64 APRatioBp = 0;
    ELDDemoDamageType DamageType = ELDDemoDamageType::Magical;
    int64 SourceADP2 = 0;
    int64 SourceAPP2 = 0;
    // Source-side amplification of its damage over time (item_megaphone), snapshotted at application.
    int64 AmplifyBp = 10000;
    int64 IntervalMs = 0;
    int64 NextTickAtMs = 0;
    int64 ExpiresAtMs = 0;
    int32 TicksLeft = 0;
    int32 TicksTotal = 0;
    int32 Stacks = 1;
    int32 MaxStacks = 1;
    int64 ShredStepP2 = 0;
    int64 DurationMs = 0;
    int64 FullHealCutBp = 0;
    // active_expose_post Lv3: one extra hit the first time the target displaces itself while the dot lasts.
    bool bPunish = false;
    int64 PunishBaseP2 = 0;
    int64 PunishADRatioBp = 0;
    int64 PunishAPRatioBp = 0;
};

// Damage taken while it lasts is reduced by ReductionBp after resistance and before the shield; once the damage counted
// before that reduction exceeds ThresholdP2 the guard recoils once on the bodies within RadiusCm (active_porcelain_flop).
struct FLDCombatGuard
{
    FGuid EffectId;
    FLDCombatSourceRef Source;
    int64 ReductionBp = 0;
    int64 ThresholdP2 = 0;
    int64 TakenP2 = 0;
    int64 ExpiresAtMs = 0;
    bool bTriggered = false;
    double RadiusCm = 0;
    int64 SlowBp = 0;
    int64 SlowMs = 0;
    int64 StunMs = 0;
    int64 ShieldBaseP2 = 0;
    int64 ShieldAPRatioBp = 0;
    int64 ShieldMaxHPRatioBp = 0;
    int64 ShieldMs = 0;
};

// Extra damage a target life takes from damage over time and basic attacks while its root lasts (binding snare Lv3).
struct FLDCombatVulnerable
{
    FGuid EffectId;
    int64 RatioBp = 0;
    int64 ExpiresAtMs = 0;
};

namespace LDCombatMechanics
{
    // Strongest active reduction at AtMs (0 when none).
    LOCKDOWN_API int64 StrongestHealCutBp(const TArray<FLDCombatHealCut>& Cuts, int64 AtMs);
    // A new reduction from the same source replaces that source's record; other sources keep theirs. Lapsed records are
    // dropped. False for an invalid candidate (no source, ratio outside 1..10000, already expired).
    LOCKDOWN_API bool ApplyHealCut(TArray<FLDCombatHealCut>& Cuts, const FLDCombatHealCut& Candidate, int64 AtMs);
    // Drops lapsed reductions; true when any ended.
    LOCKDOWN_API bool ExpireHealCuts(TArray<FLDCombatHealCut>& Cuts, int64 AtMs);
    // floor(AmountP2 x (10000 - CutBp) / 10000) for a nonnegative amount; the cut is clamped to 0..10000.
    LOCKDOWN_API int64 ReduceByBp(int64 AmountP2, int64 CutBp);

    // ReplaceIfNotWeaker with the refresh/gain distinction (#112 R1): only replacing a record that still holds a
    // positive, unexpired amount is a refresh.
    LOCKDOWN_API ELDShieldGrant GrantShield(TOptional<FLDCombatShield>& Shield, int64& ShieldP2,
        const FLDCombatShield& Candidate, int64 AmountP2, int64 AtMs);
    // A shield is broken when damage takes a positive amount to zero; lapsing or being consumed is not a break.
    LOCKDOWN_API bool ShieldBroken(int64 BeforeP2, int64 AfterP2, int64 AbsorbedP2);

    // Planar unit direction a knockback pushes the target: from the source foot to the target foot; coincident feet
    // fall back to the source's facing. False only for non-finite input.
    LOCKDOWN_API bool KnockbackDirection(const FVector& SourceFoot, const FVector& TargetFoot, double SourceYawDeg,
        FVector& OutDirection);

    // Breach records: same Key replaces (with its stacks), others aggregate; lapsed ones are dropped by Expire.
    LOCKDOWN_API void ApplyBreach(TArray<FLDCombatBreach>& Breaches, const FLDCombatBreach& Record);
    LOCKDOWN_API bool ExpireBreaches(TArray<FLDCombatBreach>& Breaches, int64 AtMs);
    LOCKDOWN_API bool HasBreach(const TArray<FLDCombatBreach>& Breaches, int64 AtMs);
    // max(0, ResistanceP2 - flat) x (1 - min(ratio, 10000)) floored to P2, over the active records of one resistance.
    LOCKDOWN_API int64 BreachedResistanceP2(int64 ResistanceP2, const TArray<FLDCombatBreach>& Breaches, bool bArmor, int64 AtMs);

    // Number of negative kinds (0..6).
    LOCKDOWN_API int32 CountNegativeKinds(const FLDNegativeKinds& Kinds);
    // 10000 + PerKindBp x Kinds: the raw-damage multiplier of active_cancel_blast.
    LOCKDOWN_API int64 NegativeKindsAmplifyBp(int32 Kinds, int64 PerKindBp);

    // Periodic schedule: the Count pulse instants StartMs + DelayMs + k x IntervalMs (0 <= k < Count). NextPulseAtMs gives
    // the first such instant at or after FromMs, or 0 when none is left (or the schedule is invalid).
    LOCKDOWN_API int64 NextPulseAtMs(int64 StartMs, int64 DelayMs, int64 IntervalMs, int32 Count, int64 FromMs);
    // Inclusive planar foot distance test of a zone.
    LOCKDOWN_API bool InsideZone(const FVector& CentreCm, double RadiusCm, const FVector& FootCm);

    // active_table_flip: Base + RatioBp x min(ShieldP2, CapP2), floored to P2.
    LOCKDOWN_API int64 ShieldBurstBaseP2(int64 BaseP2, int64 ShieldP2, int64 RatioBp, int64 CapP2);
    // Damage that counts toward a damage guard's trigger: after resistance, before its own reduction and the shield.
    LOCKDOWN_API bool GuardTriggers(int64 TakenBeforeP2, int64 HitPreMitigationP2, int64 ThresholdP2, int64& OutTakenP2);
}
