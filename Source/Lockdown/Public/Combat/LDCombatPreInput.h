#pragma once
#include "CoreMinimal.h"
#include "Combat/LDCombatTypes.h"

// Pre-input (control feel): a Q/W/D press during the owner's own ability windup is held on the owning client and
// sent as an ordinary Direct request once that windup ends, instead of reaching the server as a Busy refusal. One
// press is held at a time (the newest replaces it); S, a new life or match, suppressed input or HoldSeconds drop it.
// A basic attack needs no buffer: an attack order already waits on the server until the body is free.
namespace LDCombatPreInput
{
    constexpr double HoldSeconds = 1.0;
    LOCKDOWN_API bool IsDirectEntry(ELDCombatEntry Entry);
    // The innate D dash fires toward the pointer on the press itself (2026-11-04 decision): it never waits armed for a
    // left-click confirmation, whatever the quick-cast setting. Other aimed entries arm (or quick-cast) as before.
    LOCKDOWN_API bool FiresOnPress(ELDCombatEntry Entry);
    // The current owner view shows an own Direct ability still winding up at NowMs (0 when the clock is unknown: the
    // view's phase alone decides).
    LOCKDOWN_API bool IsWindingUp(const FLDCombatOwnerView& View, int64 NowMs);
}
