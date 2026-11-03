#pragma once
#include "CoreMinimal.h"
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
}
