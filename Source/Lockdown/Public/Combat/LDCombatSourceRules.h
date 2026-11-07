#pragma once

#include "CoreMinimal.h"
#include "Combat/LDCombatTypes.h"
#include "Combat/LDCombatEvaluationTypes.h"

struct FLDDemoRankDefinition;

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
    // The control matrix (Director::IsActionBlocked): Stun or taunt blocks the entry's cast;
    // Root, Stun or taunt blocks displacement. A taunted body may only basic-attack its taunter: no Q/W/D.
    bool bControlBlocked=false;
    bool bDisplacementBlocked=false;
    bool bCheckBusy=false;       // admission only: another ability is winding up
    bool bAbilityInProgress=false;
};

// Release of a basic attack whose windup legally started (target in range, observed and in clear sight at the start).
// 2026-11-07 user decision ("once the windup starts the hit is certain, even if the target ran far") -- the swing lands on
// its committed target life wherever that body went: distance, visibility and line of sight at the release are
// deliberately not inputs.
struct FLDBasicReleaseInput
{
    // The attacker's own state: a Disarm/Stun/knockback that landed during the windup, or a taunt toward another body.
    bool bAttackBlocked=false;
    bool bTauntAllows=true;
    // The life the windup committed to, and that entity's registered body now (invalid when it has none).
    FLDCombatBodyRef Committed;
    FLDCombatBodyRef Current;
    bool bCurrentPendingDeath=false;  // that body already took its lethal hit
    bool bTargetPresent=false;        // its contender is still a present member (camp members: true)
    bool bSelf=false;
};

// How the Direct receipt window answers one owner sequence.
enum class ELDDirectSequenceVerdict : uint8
{
    Fresh,      // judge it now
    Replay,     // a recorded request with the same terms: answer its receipt
    Conflict,   // a recorded sequence with different terms
    Expired     // at or below the high-water mark and no longer recorded: never judged again
};

namespace LDCombatSourceRules
{
    // The receipt window is consulted first, so a refusal recorded above the high-water mark (Navigation refused it
    // before Combat judged it) replays. A shift-queued step was checked unseen when it was queued and runs once from
    // the queue, so a newer sequence judged meanwhile does not expire it.
    LOCKDOWN_API ELDDirectSequenceVerdict DirectSequenceVerdict(bool bRecorded, bool bSameTerms, uint64 Sequence,
        uint64 HighWater, bool bQueuedStep);
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
    // ControlBlocked when the attacker's own control ends the swing; TargetUnavailable only when the committed target life
    // is gone (dead or dying, left the match, respawned as a new life or body); None: the swing lands.
    LOCKDOWN_API ELDCombatReject BasicReleaseGate(const FLDBasicReleaseInput& Input);

    // COMBAT 5.1 persistent cooldown binding for one complete post-change build. A learned definition keeps
    // its single track and end time, also when it is relearned. Pure; Apply installs it.
    LOCKDOWN_API bool PlanTrackBindings(const FLDCombatEntityRecord& Record, const FLDBuildProjectionInput& Input,
        int64 NowMs, const FLDDemoContentSnapshot& Content, TArray<FLDCombatBindingPlan>& Out,
        ELDCombatPrepareError& Code, FString& Error);

    // The remaining-cooldown cut shared by every refund (active_combo_flurry/active_lunge Lv3, passive_precision, C2 passive_mental_gymnastics):
    // a cooling track ends AmountMs earlier but never before NowMs; MAX_int64 makes it ready now. A ready track
    // (ReadyAtMs <= NowMs) is unchanged and a non-positive amount changes nothing.
    LOCKDOWN_API int64 ReduceReadyAt(int64 ReadyAtMs, int64 NowMs, int64 AmountMs);

    // Whether a rank displaces its caster (dash or jump): such a release is a dash for the root matrix and
    // takes the item_phase_anchor skill boost.
    LOCKDOWN_API bool DisplacesCaster(const FLDDemoRankDefinition& Rank);
    // item_phase_anchor: the boost of every character active with a dash or jump, fixed at its release. Each held
    // family with DashBoost skill terms applies once: cooldown x SkillCooldownRatioBp, distance x (1 + SkillDistanceBonusBp).
    LOCKDOWN_API bool SkillDisplacementBoost(const FLDDemoContentSnapshot& Content, const TArray<FLDEquipmentPassiveFamily>& Passives,
        int64& OutCooldownRatioBp, int64& OutDistanceBonusBp, FString& Error);
}
