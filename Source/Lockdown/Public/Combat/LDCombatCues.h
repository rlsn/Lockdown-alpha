#pragma once

#include "CoreMinimal.h"
#include "Combat/LDCombatTypes.h"
#include "Vision/LDViewTypes.h"

struct FLDCombatHit;
struct FLDDamageEvaluation;

// The one visible action-cue rule of a body life, used for the observed facts (server) and the own body (owner).
namespace LDCombatCues
{
    // A released attack or cast stays visible this long; a landed hit's flash and spark stay visible for HitHoldMs.
    // Both exceed the 150 ms that a 100 ms observation cadence needs to sample every window.
    constexpr int64 ReleaseHoldMs=200;
    constexpr int64 HitHoldMs=LDObservation::RecentHitWindowMs;

    struct FCue
    {
        ELDObservedActionCue Cue=ELDObservedActionCue::None;
        int64 AtMs=0;
        int64 ImpactAtMs=0;
    };

    // Cast > Attack. A landed hit is never an action cue: ordinary damage neither poses, moves nor delays the body it
    // strikes (only its flash and spark, from the LastHit facts). Attack and Cast run from the action's start through
    // its release plus ReleaseHoldMs (a dash to its end); a winding-up action shows until it releases. Within a kind
    // the latest start wins.
    LOCKDOWN_API FCue Select(const FLDCombatCueFacts& Facts, int64 NowMs);
    // Whether a committed hit confirms a landed blow to its source's owner (the owner's hit receipt): real damage
    // from a direct delivery, never a damage-over-time tick, settlement, reflection or ledger payment.
    LOCKDOWN_API bool ConfirmsDealt(const FLDCombatHit& Hit, int64 DamageP2);
    LOCKDOWN_API ELDObservedHitLook ClassifyHit(const FLDCombatHit& Hit);
    LOCKDOWN_API uint8 ClassifyImpact(bool bCritical, int64 ShieldBeforeP2, const FLDDamageEvaluation& Damage);
    // Owner receipt only (FLDCombatCueFacts::LastDealtFlags, above LDObservation::HitFlagsMask): the confirmed blow ended
    // the target's life. Never part of an observed hit.
    constexpr uint8 DealtLethal=8;
    static_assert((DealtLethal & LDObservation::HitFlagsMask)==0, "The lethal receipt bit is outside the observed hit flags.");
    LOCKDOWN_API uint8 ClassifyDealt(bool bCritical, int64 ShieldBeforeP2, const FLDDamageEvaluation& Damage, bool bLethal);
}
