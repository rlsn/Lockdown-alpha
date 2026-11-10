#pragma once
#include "CoreMinimal.h"
#include "Combat/LDCombatEvaluationTypes.h"
#include "Combat/LDCombatTypes.h"

// #112 §5 mechanics still used by the catalog: shield breaks, knockback direction and the zone pulse schedule. Pure rules
// over primitive values; ALDCombatDirector applies them to its body records (LDCombatMechanicsDirector.cpp holds the
// director side). Every timed record is half-open at its ExpiresAtMs, like Root and Shield.

namespace LDCombatMechanics
{
    // A shield is broken when damage takes a positive amount to zero; lapsing or being consumed is not a break.
    LOCKDOWN_API bool ShieldBroken(int64 BeforeP2, int64 AfterP2, int64 AbsorbedP2);

    // Planar unit direction a knockback pushes the target: from the source foot to the target foot; coincident feet
    // fall back to the source's facing. False only for non-finite input.
    LOCKDOWN_API bool KnockbackDirection(const FVector& SourceFoot, const FVector& TargetFoot, double SourceYawDeg,
        FVector& OutDirection);

    // Periodic schedule: the Count pulse instants StartMs + DelayMs + k x IntervalMs (0 <= k < Count). NextPulseAtMs gives
    // the first such instant at or after FromMs, or 0 when none is left (or the schedule is invalid).
    LOCKDOWN_API int64 NextPulseAtMs(int64 StartMs, int64 DelayMs, int64 IntervalMs, int32 Count, int64 FromMs);

    // #198 C01 (spec §13.3): fixed windows (Anchor + (k-1)I, Anchor + kI] for k >= 1 from its first install. The end of the
    // window a fact at AtMs belongs to (a fact on a boundary closes that window). False for an invalid interval, a fact
    // before the anchor or an end past the clock domain.
    LOCKDOWN_API bool FixedWindowEndMs(int64 AnchorMs, int64 IntervalMs, int64 AtMs, int64& OutEndMs);
}
