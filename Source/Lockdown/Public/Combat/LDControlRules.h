#pragma once
#include "CoreMinimal.h"
#include "Combat/LDCombatEvaluationTypes.h"
#include "Combat/LDAttributeProjection.h"
#include "Combat/LDCombatControlRules.h"

// Taunt rules (CONTENT_AND_BALANCE 15.5). Pure: Combat, Vision and Navigation apply them to their
// own state. Every status is half-open at its ExpiresAtMs, like Root and Shield.
namespace LDControl
{
    LOCKDOWN_API bool IsTaunted(const TOptional<FLDCombatTaunt>& Taunt, int64 AtMs);
    // A taunted body may only start or release a basic attack at its taunter's exact body; no ability.
    LOCKDOWN_API bool TauntAllows(const TOptional<FLDCombatTaunt>& Taunt, int64 AtMs, bool bBasicAttack,
        const FLDCombatBodyRef& Target);
    // The taunt's part of the control matrix: abilities (Cast, the D dash included) and displacement are blocked;
    // movement is the server's forced chase and a basic attack is judged against the taunter (TauntAllows).
    LOCKDOWN_API bool TauntBlocks(const TOptional<FLDCombatTaunt>& Taunt, int64 AtMs, ELDCombatControlAction Action);
    // The newest taunt replaces the current one: the latest taunter commands, for its own duration.
    LOCKDOWN_API bool ApplyTaunt(TOptional<FLDCombatTaunt>& Taunt, const FLDCombatTaunt& Candidate, int64 AtMs);
    // A taunt ends early when its taunter's life is no longer alive (death, leave) or the victim no longer observes
    // that body (out of sight or blocked).
    LOCKDOWN_API bool TauntHolds(const FLDCombatTaunt& Taunt, int64 AtMs, bool bTaunterAlive, bool bVictimObservesTaunter);
    // Release-time selection: other contenders whose foot is within RadiusCm (inclusive, planar) of the caster's,
    // alive, unprotected and observing the caster. Indices into Candidates, by entity id.
    LOCKDOWN_API TArray<int32> SelectTauntTargets(const FGuid& Caster, const FVector& CasterFootCm, double RadiusCm,
        const TArray<FLDTauntCandidate>& Candidates);

    LOCKDOWN_API FLDStatusExpiry ExpireControlStatuses(TOptional<FLDCombatTaunt>& Taunt, int64 AtMs);
}
