#pragma once

#include "CoreMinimal.h"
#include "Development/LDDemoContentDefinitions.h"

// Momentum (劲), the basic three-step combo, overdrive and the perfect dodge as pure rules (CONTENT_AND_BALANCE §3.3-§3.5,
// docs/systems/COMBAT_AND_GROWTH.md "劲、连击、爆劲与完美闪避"). Combat owns the P2 bar on the body's resources and calls
// these; nothing here reads the world, a clock or randomness.
namespace LDMomentum
{
    // The per-life momentum books one LD_MOMENTUM line reports (P2 amounts; Overdrives counts releases).
    struct FLedger
    {
        int64 BasicP2 = 0, CombatP2 = 0, SkillP2 = 0, ReactionP2 = 0, DodgeP2 = 0, OtherP2 = 0, IdleP2 = 0, SpentP2 = 0;
        int32 Overdrives = 0;

        bool IsEmpty() const
        {
            return BasicP2 == 0 && CombatP2 == 0 && SkillP2 == 0 && ReactionP2 == 0 && DodgeP2 == 0 && OtherP2 == 0
                && IdleP2 == 0 && SpentP2 == 0 && Overdrives == 0;
        }
    };

    // Whether EngagedAtMs (the last real combat event of this life) keeps the body in combat at NowMs: within
    // P.IdleDelayMs, half-open. A life with no combat event yet is idle.
    LOCKDOWN_API bool InCombat(const FLDDemoMomentumParameters& P, const TOptional<int64>& EngagedAtMs, int64 NowMs);

    // Settles the bar over [FromMs, NowMs). In combat it gains CombatPerSec (MOMENTUM.CombatPerSec plus the brains bonus,
    // FLDAttributeFinal::MomentumRegenPerSec) up to P.MaxP2; once idle it drifts toward P.StartP2 at P.IdleReturnPerSec and
    // stops there. InOutRemainderMicroP2 carries the sub-P2 progress (1e-6 P2) of the current direction: positive while
    // rising, negative while falling; a direction change drops it. OutCombatP2/OutIdleP2 report the signed change of each
    // part. Fails for a reversed interval or an unrepresentable rate.
    LOCKDOWN_API bool Settle(const FLDDemoMomentumParameters& P, const FLDDemoRational& CombatPerSec,
        const TOptional<int64>& EngagedAtMs, int64 FromMs, int64 NowMs, int64& InOutP2, int64& InOutRemainderMicroP2,
        int64& OutCombatP2, int64& OutIdleP2, FString& Error);

    // The momentum one primary basic hit gains: P.BasicPerRefP2 x IntervalMs / P.BasicRefIntervalMs, floored to P2, so the
    // gain per second does not depend on attack speed. 0 for a non-positive interval.
    LOCKDOWN_API int64 BasicGainP2(const FLDDemoMomentumParameters& P, int64 IntervalMs);

    // CurrentP2 + DeltaP2 clamped to [0, P.MaxP2]; the applied change is returned in OutAppliedP2.
    LOCKDOWN_API int64 Apply(const FLDDemoMomentumParameters& P, int64 CurrentP2, int64 DeltaP2, int64& OutAppliedP2);

    // The combo step a successful AttackStart at NowMs takes: 1 after more than ResetMs since the previous start (or with no
    // previous start, Step 0), otherwise the next of 1, 2, 3, 1, ...
    LOCKDOWN_API uint8 NextComboStep(uint8 Step, int64 PreviousStartMs, int64 NowMs, int64 ResetMs);
    // COMBO.StepRatios of Step 1..3 in basis points; 10000 for any other step (no combo).
    LOCKDOWN_API int64 ComboRatioBp(const FLDDemoComboParameters& P, uint8 Step);

    // Whether a release that asked for overdrive is one: the bar holds at least OVERDRIVE.Threshold at the release.
    LOCKDOWN_API bool OverdriveReady(const FLDDemoOverdriveParameters& P, int64 MomentumP2);

    // A perfect dodge: the D started within WindowMs before the telegraphed effect resolved at ResolveMs (inclusive),
    // the foot at the dash start was inside the effect's hit area and the body at resolution is not.
    LOCKDOWN_API bool IsPerfectDodge(int64 WindowMs, int64 DashStartMs, int64 ResolveMs, bool bStartInside, bool bResolvedInside);
}
