#pragma once

#include "CoreMinimal.h"
#include "Combat/LDCombatTypes.h"
#include "Vision/LDViewTypes.h"

// The one visible action-cue rule of a body life, used for the observed facts (server) and the own body (owner).
namespace LDCombatCues
{
    // A released attack or cast stays visible this long; a landed hit shows for HitHoldMs. Both exceed the 150 ms
    // that a 100 ms observation cadence needs to sample every window.
    constexpr int64 ReleaseHoldMs=200;
    constexpr int64 HitHoldMs=300;

    struct FCue
    {
        ELDObservedActionCue Cue=ELDObservedActionCue::None;
        int64 AtMs=0;
    };

    // Cast > Hit > Attack: a body hit in the middle of its own swing still shows the hit. Attack and Cast run from the
    // action's start through its release plus ReleaseHoldMs (a dash to its end); a winding-up action shows until it
    // releases. Within a kind the latest start wins.
    LOCKDOWN_API FCue Select(const FLDCombatCueFacts& Facts, int64 NowMs);
}
