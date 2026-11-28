#pragma once

#include "CoreMinimal.h"
#include "Combat/LDCombatTypes.h"
#include "Development/LDDemoContentDefinitions.h"

// Auto-empower (自动强化, decision M-12) as pure rules: the experiment switch, the host and its processor, readiness and the
// processors' changes to a release's values (docs/systems/COMBAT_AND_GROWTH.md "自动强化实验"). Combat owns the bar, the
// host choice and every release; nothing here reads the world, a clock or randomness.
enum class ELDEmpowerProcessor : uint8 { None, Fist, Strike, Guard, Move, Ward };

namespace LDAutoEmpower
{
    // Whether the experiment runs: EMPOWER.Enabled, or the authority's -LDEmpower switch.
    LOCKDOWN_API bool Enabled(const FLDDemoEmpowerParameters& P);
    // The host -LDEmpowerHost=Basic|Q|W|E|D gives every contender that has not chosen one (None without the switch).
    LOCKDOWN_API ELDCombatEntry SwitchHost();

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

    // Strike shapes: areas, lines, cones and pushes reach ShapeScaleBp further; a chain bounces ExtraCount more times.
    LOCKDOWN_API void ApplyStrike(const FLDDemoEmpowerParameters& P, FLDDemoTargetDefinition& Target, FLDDemoAbilityValues& Values);
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
