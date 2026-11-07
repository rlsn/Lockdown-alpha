#pragma once
#include "CoreMinimal.h"
#include "Combat/LDAttributeProjection.h"
#include "Combat/LDCombatEvaluationTypes.h"
#include "Combat/LDAttributeProjection.h"

class FLDDemoContentSnapshot;
struct FLDDemoPolicies;

namespace LDTactical
{
    LOCKDOWN_API FLDDamageEvaluationResult EvaluateDamage(
        const FLDDamageEvaluationInput& Input, const FLDDemoContentSnapshot& Catalog);
    LOCKDOWN_API FLDBasicTimingResult EvaluateBasicTiming(
        const FLDBasicTimingInput& Input, const FLDDemoContentSnapshot& Catalog);

    // FORMULA.CooldownMs: ceil(BaseMs x Scale / (Scale + Haste)) with Scale=HasteScaleP2 and Haste in P2.
    LOCKDOWN_API bool CooldownDurationMs(int64 BaseMs, int64 HasteP2, int64 HasteScaleP2, int64& OutMs, FString& Error);

    // Status and resource rules extracted from the d70211a LDTactical Allows/Apply/Dispel/Barrier/Regenerate
    // helpers and retyped to P2 integers and authority milliseconds. Expiry is half-open at ExpiresAtMs.
    LOCKDOWN_API bool HasActiveRoot(const TArray<FLDCombatRoot>& Roots, int64 AtMs);
    // Largest remaining Root deadline (the visible expiry), or 0 when no instance is active.
    LOCKDOWN_API int64 ActiveRootUntilMs(const TArray<FLDCombatRoot>& Roots, int64 AtMs);
    // POLICY.RootBlocks: an active Root blocks only the listed voluntary actions (Move, Dash).
    LOCKDOWN_API bool RootBlocks(const TArray<FLDCombatRoot>& Roots, int64 AtMs,
        const FLDDemoPolicies& Policies, ELDDemoRootBlock Action);
    // Instances are kept per effect; re-applying the same instance never renews it. Returns whether it was added.
    LOCKDOWN_API bool ApplyRoot(TArray<FLDCombatRoot>& Roots, const FLDCombatRoot& Instance, int64 AtMs);
    // Root-only cleanse: removes every current Root instance and nothing else. Returns the removed count.
    LOCKDOWN_API int32 DispelRoots(TArray<FLDCombatRoot>& Roots);
    // EFFECT.ShieldPolicy=ReplaceIfNotWeaker against the current remaining amount; equal amounts refresh.
    // No legacy barrier cap or strict comparison (COMBAT 6.4). Returns whether the candidate replaced the record.
    LOCKDOWN_API bool ApplyShield(TOptional<FLDCombatShield>& Shield, int64& ShieldP2,
        const FLDCombatShield& Candidate, int64 AmountP2, int64 AtMs);
    // Drops expired Root instances and an expired or depleted barrier.
    LOCKDOWN_API FLDStatusExpiry ExpireTimedStatuses(TArray<FLDCombatRoot>& Roots,
        TOptional<FLDCombatShield>& Shield, int64& ShieldP2, int64 AtMs);
    // Slows are kept per effect instance like Root (no renewal of the same instance); only the strongest active one counts.
    LOCKDOWN_API bool ApplySlow(TArray<FLDCombatSlow>& Slows, const FLDCombatSlow& Instance, int64 AtMs);
    LOCKDOWN_API int64 StrongestSlowBp(const TArray<FLDCombatSlow>& Slows, int64 AtMs);
    // Strongest active attack-speed cut carried by a slow instance (0 when none).
    LOCKDOWN_API int64 StrongestAttackSlowBp(const TArray<FLDCombatSlow>& Slows, int64 AtMs);
    // Latest expiry among the active slows of the strongest ratio (the visible deadline), or 0.
    LOCKDOWN_API int64 StrongestSlowUntilMs(const TArray<FLDCombatSlow>& Slows, int64 AtMs);
    // Removes every slow instance; returns how many were active at AtMs (active_guard_pulse, active_slick_moves Lv3).
    LOCKDOWN_API int32 DispelSlows(TArray<FLDCombatSlow>& Slows, int64 AtMs);
    // A recast of the same source definition replaces its bonus; different sources aggregate additively.
    LOCKDOWN_API void ApplyTimedBonus(TArray<FLDCombatTimedBonus>& Bonuses, const FLDCombatTimedBonus& Bonus);
    // The same Key and source refresh the record (stacks add up to MaxStacks, terms and deadline take the newest);
    // anything else is a separate record.
    LOCKDOWN_API void ApplyBreach(TArray<FLDCombatBreach>& Breaches, const FLDCombatBreach& Breach, int64 AtMs);
    LOCKDOWN_API bool HasBreach(const TArray<FLDCombatBreach>& Breaches, int64 AtMs);
    // The resistance a hit meets: flats first (per stack, plus ExtraFlatP2 of the shared #147 breach), then the summed
    // ratio (capped at 100%), floored to P2, >= 0.
    LOCKDOWN_API int64 BreachedResistanceP2(const TArray<FLDCombatBreach>& Breaches, int64 ResistanceP2, bool bArmor, int64 AtMs,
        int64 ExtraFlatP2 = 0);
    // Latest active breach deadline (the visible expiry), or 0.
    LOCKDOWN_API int64 BreachUntilMs(const TArray<FLDCombatBreach>& Breaches, int64 AtMs);
    // The same source refreshes its mark.
    LOCKDOWN_API void ApplyVampMark(TArray<FLDCombatVampMark>& Marks, const FLDCombatVampMark& Mark);
    // The mark's ratio while its bound slow is still active on the same body; 0 otherwise.
    LOCKDOWN_API int64 VampMarkBp(const TArray<FLDCombatVampMark>& Marks, const TArray<FLDCombatSlow>& Slows, const FGuid& Source,
        int64 AtMs);
    // Drops vamp marks whose bound slow is gone or lapsed and carries a refreshed slow's deadline onto its mark; true when
    // anything changed.
    LOCKDOWN_API bool SyncVampMarks(TArray<FLDCombatVampMark>& Marks, const TArray<FLDCombatSlow>& Slows, int64 AtMs);
    // Drops every slow, timed bonus, breach and vamp mark whose half-open deadline has passed (and every mark whose slow
    // ended); true when anything ended.
    LOCKDOWN_API bool ExpireTemporaryStatuses(TArray<FLDCombatSlow>& Slows, TArray<FLDCombatTimedBonus>& Bonuses,
        TArray<FLDCombatBreach>& Breaches, TArray<FLDCombatVampMark>& VampMarks, int64 AtMs);
    // HP strictly below ThresholdBp of MaxHP (a dead or empty pool never counts).
    LOCKDOWN_API bool BelowHPThreshold(int64 HPP2, int64 MaxHPP2, int64 ThresholdBp);
    // The ambush window opens DelayMs after the last combat instant; CreditMs (dashes, jumps) brings it closer.
    LOCKDOWN_API bool AmbushReady(int64 LastCombatAtMs, int64 DelayMs, int64 AtMs, int64 CreditMs = 0);
    // Active timed bonuses (in apply order), the conditional low-HP bonus and the strongest slow at AtMs.
    LOCKDOWN_API FLDTemporaryAttributeTerms TemporaryTerms(const TArray<FLDCombatTimedBonus>& Bonuses,
        const TArray<FLDCombatSlow>& Slows, const TOptional<FLDLowHPBonusTerm>& LowHP, int64 HPP2, int64 MaxHPP2, int64 AtMs);
    // FORMULA.Regen with POLICY.RegenIntegration=CarrySubP2Remainder: integrates RatePerSec over ElapsedMs plus the
    // carried 1e-6 P2 remainder, commits whole P2 and discards overflow at MaxP2. Fails for unrepresentable rates.
    LOCKDOWN_API bool IntegrateRegen(const FLDDemoRational& RatePerSec, int64 ElapsedMs, int64 MaxP2,
        int64& InOutP2, int64& InOutRemainderMicroP2, FString& Error);

    // D21 recovery of every contender: once DelayMs has passed since its last combat event, a per-second share
    // (basis points) of its current maximum HP and MP. It stacks with every other regeneration term.
    struct FOutOfCombatRecovery
    {
        int64 DelayMs = 0;
        int64 MaxHPBpPerSec = 0;
        int64 MaxMPBpPerSec = 0;
    };

    // The resource pools and sub-P2 remainders of one living body that regeneration settles.
    struct FRegenPools
    {
        int64 HpP2 = 0, MaxHpP2 = 0, MpP2 = 0, MaxMpP2 = 0;
        int64 HpMicroP2 = 0, MpMicroP2 = 0;
    };

    // FORMULA.Regen over [FromMs, NowMs) of one living body: the always-on rates of Attributes, its passive
    // out-of-combat bonus and, when given, the contender Recovery and the dorm's fast Dorm recovery (D21, #79).
    // Each out-of-combat term runs exactly from LastCombatAtMs + its own delay and they all stack; the maxima are
    // the ones held over the whole interval (a change settles first).
    // passive_second_wind: before SurgeUntilMs the always-on rates are multiplied by the passive's surge multiplier, and during combat
    // time the passive keeps its in-combat share of its out-of-combat rate.
    LOCKDOWN_API bool SettleRegen(const FLDAttributeProjection& Attributes, const FOutOfCombatRecovery* Recovery,
        int64 LastCombatAtMs, int64 FromMs, int64 NowMs, FRegenPools& InOut, FString& Error, int64 SurgeUntilMs = 0,
        const FOutOfCombatRecovery* Dorm = nullptr);
    struct FRegenRates { double HP=0, MP=0; };
    // Read-only instantaneous rates from validated authority inputs, before full-pool clipping.
    // Uses the same terms (including the D21 躺平角 term when given) and activation boundaries as SettleRegen; no resource integration or mutation.
    LOCKDOWN_API FRegenRates RegenRates(const FLDAttributeProjection& Attributes, const FOutOfCombatRecovery* Recovery,
        int64 MaxHpP2, int64 MaxMpP2, int64 LastCombatAtMs, int64 NowMs, int64 SurgeUntilMs = 0,
        const FOutOfCombatRecovery* Dorm = nullptr);
    // Whether an out-of-combat term with DelayMs is running at NowMs.
    LOCKDOWN_API bool OutOfCombatAt(int64 LastCombatAtMs, int64 DelayMs, int64 NowMs);
}
