#pragma once
#include "CoreMinimal.h"
#include "Combat/LDCombatTypes.h"

struct FLDDemoContentParameters;
struct FLDCombatHit;

// #198 water, fire and electric (docs/systems/COMBAT_AND_GROWTH.md "水火电与反应", CONTENT_AND_BALANCE §4.4). Each target
// life holds at most one element: fire is its burn layers (LDCombatKeywords), water and electric are one mark. A hit's
// elements apply as one batch in the order fire, water, electric; a different element already held reacts with the new
// one and both are consumed. Pure rules over primitive values; ALDCombatDirector applies them to its body records
// (LDCombatElementsDirector.cpp).
enum class ELDCombatElement : uint8 { None, Fire, Water, Electric };
enum class ELDCombatReaction : uint8 { None, Fog, Conduct, Overload };

// AI spec §8 event categories of one committed hit. BasicPrimary: an own basic primary hit (empowerments merged in).
// SkillDirect: an active's own damage (its native hits, zone pulses, the S16 answer). SkillHit: other skill hits that are
// not status damage (connector answers, added contact damage). StatusSettle: burn ticks, early fire settles, electric
// discharges and reaction damage; these never feed skill-hit readers.
enum class ELDCombatHitCategory : uint8 { BasicPrimary, SkillDirect, SkillHit, StatusSettle };

// The approved element terms (COMBAT.Wet*, COMBAT.Electrified*, COMBAT.Discharge*, COMBAT.Reaction*, COMBAT.Fog*,
// COMBAT.Conduct*, COMBAT.Overload*).
struct LOCKDOWN_API FLDElementTerms
{
    int64 WetMs = 0;
    int64 WetSlowBp = 0;
    int64 ElectrifiedMs = 0;
    double DischargeRadiusCm = 0;
    int64 DischargeIntervalMs = 0;
    int64 ReactionBaseP2 = 0;
    int64 ReactionPerLevelP2 = 0;
    int64 ReactionAPRatioBp = 0;
    double FogRadiusCm = 0;
    int64 FogMs = 0;
    int64 FogIntervalMs = 0;
    double ConductRadiusCm = 0;
    int64 ConductSlowBp = 0;
    int64 ConductSlowMs = 0;
    double OverloadRadiusCm = 0;
    int32 OverloadBreachStacks = 0;

    static FLDElementTerms From(const FLDDemoContentParameters& Parameters);
    // Every term the rules divide or step by is positive.
    bool IsUsable() const;
};

// Who applied an element and the snapshot its reaction damage and discharges read (the applier's level and AP when the
// element was applied).
struct FLDElementApplier
{
    FGuid SourceEntity;
    FLDCombatBodyRef SourceBody;
    FGuid CreditOwner;
    FLDCombatSourceRef Source;
    int32 Level = 1;
    int64 SourceAPP2 = 0;
    // The release whose hit applied this element (C15 keys its once-per-release cut on it); unset otherwise.
    FGuid EffectId;
};

// The water or electric mark of a target life. Wet also holds a slow instance (SlowEffectId) that ends with the mark.
struct FLDCombatElementMark
{
    ELDCombatElement Kind = ELDCombatElement::None;
    FLDElementApplier Applier;
    FGuid SlowEffectId;
    int64 ExpiresAtMs = 0;
};

// A completed reaction (AI spec §8 "反应完成"): the trigger is the applier of the new element and TriggerSource the source
// of the hit that applied it; connectors that read "a reaction this source triggered" key on it.
struct FLDCombatReactionFact
{
    ELDCombatReaction Kind = ELDCombatReaction::None;
    FLDCombatBodyRef TriggerBody;
    FGuid TriggerEntity;
    FLDCombatSourceRef TriggerSource;
    FGuid TriggerEffectId;
    FLDCombatBodyRef Primary;
    FGuid HitId;
    int64 AtMs = 0;
};

// The fog of one owner (fire + water): a fixed zone that gives element-free enemies inside wet at its start and every
// FogIntervalMs until it ends. One per owner; a newer fog replaces it.
struct FLDCombatFog
{
    FGuid EffectId;
    FLDElementApplier Owner;
    FVector CentreCm = FVector::ZeroVector;
    double RadiusCm = 0;
    int64 StartMs = 0, EndMs = 0, NextPulseMs = 0;
};

// S16 顶着揍 (AI spec §9.1): for its duration hostile damage after resistance and percentage cuts is reduced by RatioBp;
// the amount actually cut is recorded up to CapP2. At its natural end a living body answers every enemy within RadiusCm
// with BaseP2 + RecordRatioBp of the record as magic damage. Death, leaving and a newer cast end it without an answer.
struct FLDCombatBrace
{
    FGuid EffectId;
    FLDCombatSourceRef Source;
    uint64 Token = 0;
    int64 EndsAtMs = 0;
    int64 RatioBp = 0;
    int64 CapP2 = 0;
    int64 BaseP2 = 0;
    int64 RecordRatioBp = 0;
    double RadiusCm = 0;
    int64 RecordedP2 = 0;
    // The release's shared ledger: the answer is that release's native direct damage (B13-B16).
    TSharedPtr<struct FLDCombatCastLedger> CastLedger;
};

// S21 馊水糊脸: new heals and new shields of the holder are cut by RatioBp until ExpiresAtMs; existing shields stay.
struct FLDCombatGrievous
{
    int64 RatioBp = 0;
    int64 ExpiresAtMs = 0;
};

namespace LDCombatElements
{
    // The reaction of a held element meeting an incoming one; None when either is None or both are the same.
    LOCKDOWN_API ELDCombatReaction ReactionOf(ELDCombatElement Held, ELDCombatElement Incoming);
    // R = ReactionBase + ReactionPerLevel x (Level - 1) + ReactionAPRatio x AP, floored to P2 (not scaled by G).
    LOCKDOWN_API int64 ReactionDamageP2(int32 Level, int64 SourceAPP2, const FLDElementTerms& Terms);
    LOCKDOWN_API bool MarkActive(const FLDCombatElementMark& Mark, int64 AtMs);
    // The element a target life holds now: fire while any burn layer is live, otherwise its live mark.
    LOCKDOWN_API ELDCombatElement ElementOf(bool bBurning, const FLDCombatElementMark& Mark, int64 AtMs);
    // The record after one cut: Recorded + Cut, at most Cap.
    LOCKDOWN_API int64 BraceRecordP2(int64 RecordedP2, int64 CutP2, int64 CapP2);
    // The answer's raw magic damage: Base + RecordRatio x Recorded, floored to P2.
    LOCKDOWN_API int64 BraceAnswerP2(const FLDCombatBrace& Brace);
    LOCKDOWN_API bool BraceActive(const FLDCombatBrace& Brace, int64 AtMs);
    // A new heal or shield of AmountP2 after a live grievous cut: floor(Amount x (1 - Ratio)).
    LOCKDOWN_API int64 AfterGrievousP2(int64 AmountP2, const FLDCombatGrievous& Grievous, int64 AtMs);
    // Applies a grievous of RatioBp until ExpiresAtMs: the stronger ratio stays, the later end stays.
    LOCKDOWN_API bool ApplyGrievous(FLDCombatGrievous& Grievous, int64 RatioBp, int64 ExpiresAtMs, int64 AtMs);
    // The AI spec §8 category of one hit.
    LOCKDOWN_API ELDCombatHitCategory CategoryOf(const FLDCombatHit& Hit);
    LOCKDOWN_API const TCHAR* ReactionName(ELDCombatReaction Reaction);
}
