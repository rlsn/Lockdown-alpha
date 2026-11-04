#pragma once
#include "CoreMinimal.h"
#include "Combat/LDAttributeProjection.h"
#include "Combat/LDCombatEvaluationTypes.h"

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
    // out-of-combat bonus and, when given, the contender Recovery. Each out-of-combat term runs exactly from
    // LastCombatAtMs + its own delay; the maxima are the ones held over the whole interval (a change settles first).
    LOCKDOWN_API bool SettleRegen(const FLDAttributeProjection& Attributes, const FOutOfCombatRecovery* Recovery,
        int64 LastCombatAtMs, int64 FromMs, int64 NowMs, FRegenPools& InOut, FString& Error);
    // Whether an out-of-combat term with DelayMs is running at NowMs.
    LOCKDOWN_API bool OutOfCombatAt(int64 LastCombatAtMs, int64 DelayMs, int64 NowMs);
}
