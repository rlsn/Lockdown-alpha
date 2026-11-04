#pragma once

#include "CoreMinimal.h"
#include "Combat/LDCombatTypes.h"
#include "Combat/LDCombatEvaluationTypes.h"

struct FLDCombatEntityRecord;
struct FLDDemoPolicies;
class FLDDemoContentSnapshot;

// The executable terms of the innate D dash (D19) for one set of held equipment passives (D20).
struct FLDInnateDashTerms
{
    FLDDemoRational DistanceCm;
    int64 DurationMs=0;
    // Whole cooldown issued at release; cooldown haste does not apply to the innate dash.
    int64 CooldownMs=0;
    int64 ManaCostP2=0;
    bool bBoosted=false;
};

// Execution limits shared by admission, release and the owner view, checked in this fixed order.
struct FLDActiveGateInput
{
    int64 NowMs=0;
    int64 ReadyAtMs=0;           // the source's persistent track end
    int64 MpP2=0;
    int64 ManaCostP2=0;
    bool bDash=false;            // the active moves the body (POLICY.RootBlocks Dash applies)
    const TArray<FLDCombatRoot>* Roots=nullptr;
    // The control matrix (Director::IsActionBlocked): Stun or taunt blocks this entry's own action (Cast/Gear);
    // Root, Stun or taunt blocks displacement. A taunted body may only basic-attack its taunter: no Q/W/D/1/2.
    bool bControlBlocked=false;
    bool bDisplacementBlocked=false;
    bool bCheckBusy=false;       // admission only: another ability is winding up
    bool bAbilityInProgress=false;
};

namespace LDCombatSourceRules
{
    // Persistent source and cooldown identity of the innate dash on every contender record.
    LOCKDOWN_API const FName& InnateDashId();
    // D19/D20: approved CHARACTER dash terms; each held passive family with a DashBoost effect applies once,
    // multiplying the cooldown by CooldownRatioBp (ceil to whole ms) and the distance by (1 + DistanceBonusBp).
    // The duration is unchanged, so a boosted dash covers its longer distance in the same time.
    LOCKDOWN_API bool InnateDashTerms(const FLDDemoContentSnapshot& Content, const TArray<FLDEquipmentPassiveFamily>& Passives,
        FLDInnateDashTerms& Out, FString& Error);
    // ControlBlocked for the entry's own action, Busy (admission only), Cooldown, InsufficientMana, then ControlBlocked
    // for a moving active under a Root or displacement block; None when usable.
    LOCKDOWN_API ELDCombatReject ActiveGate(const FLDActiveGateInput& Input, const FLDDemoPolicies& Policies);

    // COMBAT 5.1 persistent cooldown binding for one complete post-change build. A learned definition keeps
    // its single track and end time. Each held active gear copy keeps the track bound to its instance, else
    // takes the latest unexpired unbound track of its definition (debt first, stable id on ties), else a
    // ready one, else a new track up to Progression.EquipmentSlots per definition. Pure; Apply installs it.
    LOCKDOWN_API bool PlanTrackBindings(const FLDCombatEntityRecord& Record, const FLDBuildProjectionInput& Input,
        int64 NowMs, const FLDDemoContentSnapshot& Content, TArray<FLDCombatBindingPlan>& Out,
        ELDCombatPrepareError& Code, FString& Error);
}
