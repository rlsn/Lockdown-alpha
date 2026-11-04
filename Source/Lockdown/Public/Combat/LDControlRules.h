#pragma once
#include "CoreMinimal.h"
#include "Combat/LDCombatEvaluationTypes.h"
#include "Combat/LDAttributeProjection.h"
#include "Combat/LDCombatControlRules.h"

// Taunt and stealth rules (CONTENT_AND_BALANCE 15.4/15.5). Pure: Combat, Vision and Navigation apply them to their
// own state. Every status is half-open at its ExpiresAtMs, like Root and Shield.
namespace LDControl
{
    LOCKDOWN_API bool IsTaunted(const TOptional<FLDCombatTaunt>& Taunt, int64 AtMs);
    // A taunted body may only start or release a basic attack at its taunter's exact body; no ability or item.
    LOCKDOWN_API bool TauntAllows(const TOptional<FLDCombatTaunt>& Taunt, int64 AtMs, bool bBasicAttack,
        const FLDCombatBodyRef& Target);
    // The taunt's part of the control matrix: abilities (Cast, the D dash included), items (Gear) and displacement are
    // blocked; movement is the server's forced chase and a basic attack is judged against the taunter (TauntAllows).
    LOCKDOWN_API bool TauntBlocks(const TOptional<FLDCombatTaunt>& Taunt, int64 AtMs, ELDCombatControlAction Action);
    // The newest taunt replaces the current one: the latest taunter commands, for its own duration.
    LOCKDOWN_API bool ApplyTaunt(TOptional<FLDCombatTaunt>& Taunt, const FLDCombatTaunt& Candidate, int64 AtMs);
    // A taunt that lands is a control: like any other control it also reveals a stealthed victim (15.4).
    LOCKDOWN_API bool TauntVictim(TOptional<FLDCombatTaunt>& Taunt, TOptional<FLDCombatStealth>& Stealth,
        const FLDCombatTaunt& Candidate, int64 AtMs, bool& bRevealed);
    // A taunt ends early when its taunter's life is no longer alive (death, leave) or the victim no longer observes
    // that body (out of sight, blocked or hidden by stealth).
    LOCKDOWN_API bool TauntHolds(const FLDCombatTaunt& Taunt, int64 AtMs, bool bTaunterAlive, bool bVictimObservesTaunter);
    // Release-time selection: other contenders whose foot is within RadiusCm (inclusive, planar) of the caster's,
    // alive, unprotected and observing the caster. Indices into Candidates, by entity id.
    LOCKDOWN_API TArray<int32> SelectTauntTargets(const FGuid& Caster, const FVector& CasterFootCm, double RadiusCm,
        const TArray<FLDTauntCandidate>& Candidates);

    LOCKDOWN_API bool IsStealthed(const TOptional<FLDCombatStealth>& Stealth, int64 AtMs);
    // The planar distance beyond which enemies cannot observe the body now; 0 when it is not stealthed.
    LOCKDOWN_API double ConcealBeyondCm(const TOptional<FLDCombatStealth>& Stealth, int64 AtMs);
    // A new stealth replaces any current one (re-entering the box restarts its duration).
    LOCKDOWN_API bool ApplyStealth(TOptional<FLDCombatStealth>& Stealth, const FLDCombatStealth& Candidate, int64 AtMs);
    // Revealing triggers: an accepted basic attack, an accepted ability or item activation, or a positive hit
    // (damage, including fully absorbed damage, or a control). Returns whether a stealth record was removed.
    LOCKDOWN_API bool Reveal(TOptional<FLDCombatStealth>& Stealth);
    LOCKDOWN_API FLDStatusExpiry ExpireControlStatuses(TOptional<FLDCombatTaunt>& Taunt,
        TOptional<FLDCombatStealth>& Stealth, int64 AtMs);
    // FORMULA.MoveSpeed with the stealth penalty as one more additive ratio term, clamped to the approved bounds.
    LOCKDOWN_API bool MoveSpeedCmPerSec(const FLDAttributeContributions& Row, int64 PenaltyBp,
        const FLDDemoRationalBounds& Bounds, double& OutCmPerSec);
}
