#pragma once

#include "CoreMinimal.h"
#include "Combat/LDCombatTypes.h"
#include "Development/LDDemoContentDefinitions.h"

// Auto-empower (自动强化, decision M-12) as pure rules: the experiment switch, the host and its processor, readiness and the
// processors' changes to a release's values (docs/systems/COMBAT_AND_GROWTH.md "自动强化实验"). Combat owns the bar, the
// host choice and every release; nothing here reads the world, a clock or randomness.
enum class ELDEmpowerProcessor : uint8 { None, Fist, Strike, Guard, Move, Ward };

// The behaviour switches of one empowered release that are not release values (AUTO_EMPOWER_SPEC §2.3): Combat's delivery
// reads them where the existing mechanism lives (grab line, projectile pierce, ground states, cordon touches).
struct FLDEmpowerShape
{
    // S02: the grab takes every body on its line instead of the first.
    bool bGrabAll=false;
    // S06, N3: the flight goes on through PierceCount more bodies, each taking PierceScaleBp of its package.
    int32 PierceCount=0;
    int64 PierceScaleBp=0;
    // S07, S09: the ground state (ELDGroundStateKind) the release leaves for GroundLifeMs: under the caster (a self area,
    // GroundRadiusCm wide) or where a line ends (the kind's own radius).
    uint8 GroundKind=0;
    int64 GroundLifeMs=0;
    FLDDemoRational GroundRadiusCm;
    // N6: the cordon stuns every enemy at its first touch for TouchStunMs.
    int64 TouchStunMs=0;
};

namespace LDAutoEmpower
{
    // Whether the experiment runs: EMPOWER.Enabled, or the authority's -LDEmpower switch.
    LOCKDOWN_API bool Enabled(const FLDDemoEmpowerParameters& P);
    // The host -LDEmpowerHost=Basic|Q|W|E|D gives every contender that has not chosen one (None without the switch).
    LOCKDOWN_API ELDCombatEntry SwitchHost();
    // A host by its name (Basic, Q, W, E or D, any case); None for anything else.
    LOCKDOWN_API ELDCombatEntry ParseHost(const FString& Name);

    // An action started on a bar holding the threshold is empowered; it pays the cost when it releases on a bar holding it
    // (the caller has the running terms: ALDCombatDirector::EmpowerTerms).
    LOCKDOWN_API bool Ready(const FLDDemoEmpowerParameters& P, int64 MomentumP2);
    LOCKDOWN_API bool CanPay(const FLDDemoEmpowerParameters& P, int64 MomentumP2);

    // The entries a host may be: the basic (the body), Q, W, E and D.
    LOCKDOWN_API bool IsHostEntry(ELDCombatEntry Entry);
    LOCKDOWN_API const TCHAR* EntryName(ELDCombatEntry Entry);
    LOCKDOWN_API const TCHAR* ProcessorName(ELDEmpowerProcessor Processor);

    // The processor of a host: the basic is a fist, D a move; a learned active by what it does (an empowered-basic or frenzy
    // skill is a fist, a dash a move, a self shield, parry, brace or taunt a guard, a placed zone, trap, fan, bag or cordon a
    // ward, anything else a strike). Target and Values are the active's release terms (null for the basic and D).
    LOCKDOWN_API ELDEmpowerProcessor ProcessorOf(ELDCombatEntry Entry, const FLDDemoTargetDefinition* Target,
        const FLDDemoAbilityValues* Values);

    // One automatic host candidate (an AI contender's): its entry, whether it holds an action, and its investment (cards on it,
    // ranks past the first, medal tier).
    struct FHostCandidate
    {
        ELDCombatEntry Entry=ELDCombatEntry::None;
        bool bHeld=false;
        int32 Score=0;
    };
    // The held candidate with the highest score; ties go to the earlier of Q, W, E, the basic, D (the basic without any).
    LOCKDOWN_API ELDCombatEntry AutoHost(const TArray<FHostCandidate>& Candidates);

    // Value * Bp / 10000 without overflow for non-negative factors.
    LOCKDOWN_API int64 Share(int64 Value, int64 Bp);
    LOCKDOWN_API FLDDemoRational ScaleCm(const FLDDemoRational& Value, int64 Bp);

    // Strike shapes: areas, lines and pushes reach ShapeScaleBp further, a cone opens ConeAngleScaleBp wider; a chain bounces
    // ExtraCount more times.
    LOCKDOWN_API void ApplyStrike(const FLDDemoEmpowerParameters& P, FLDDemoTargetDefinition& Target, FLDDemoAbilityValues& Values);
    // The form changes on top of the processor's shape (one rule per mechanism a SPEC §2.3 row reuses): a strike's push onto
    // a single target stuns on a wall (S01), its grab takes the whole line (S02), its flight pierces every body (S06, N3) and
    // its native element leaves a ground state (S07 fire under the caster, S09 a puddle where the line ends, N1 ice over the
    // cone); a parry stuns its attacker (S05); a cordon stuns at the first touch (N6). Element is the active's native element.
    LOCKDOWN_API void ApplyForms(const FLDDemoEmpowerParameters& P, ELDEmpowerProcessor Processor, ELDDemoSkillElement Element,
        FLDDemoTargetDefinition& Target, FLDDemoAbilityValues& Values, FLDEmpowerShape& Out);
    // Guard: the parry window, the brace's record cap and answer radius and the taunt radius scale by ShapeScaleBp (a shield
    // bursts instead, Combat's part; control times never scale).
    LOCKDOWN_API void ApplyGuard(const FLDDemoEmpowerParameters& P, FLDDemoAbilityValues& Values);
    // Ward: zones (their pulses too), traps, fans, bags (their life too) and cordons last and reach ShapeScaleBp further; a
    // trap's fuse is WardFuseMs at most.
    LOCKDOWN_API void ApplyWard(const FLDDemoEmpowerParameters& P, FLDDemoAbilityValues& Values);
    // Fist skills: a charged empowered-basic package gains ExtraCount charges; frenzy waves fly ShapeScaleBp wider. A
    // single-package fist skill (S11) instead makes its next basic an empowered punch (Combat's part).
    LOCKDOWN_API void ApplyFistSkill(const FLDDemoEmpowerParameters& P, FLDDemoAbilityValues& Values);
    // The element layer on a release's values: fire adds burn layers (spread over a zone's pulses), water raises the slow,
    // shock adds ShockReactionBp of the caster's reaction base ReactionP2 to its damage, rot lasts RotDurationBp longer
    // (ApprovedRotMs without its own) and chill roots. bCanDeliverHostile: a self or dash action without an area or contact
    // carries no added element.
    LOCKDOWN_API void ApplyElement(const FLDDemoEmpowerParameters& P, ELDDemoSkillElement Element, bool bCanDeliverHostile,
        int64 ReactionP2, int64 ApprovedRotMs, FLDDemoAbilityValues& Values);
}
