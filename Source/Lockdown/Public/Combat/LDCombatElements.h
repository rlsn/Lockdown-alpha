#pragma once
#include "CoreMinimal.h"
#include "Combat/LDCombatTypes.h"

struct FLDDemoContentParameters;
struct FLDCombatHit;
enum class ELDDemoElement : uint8;
struct FLDCombatCastLedger;

// Spec §8 five elements (docs/systems/COMBAT_AND_GROWTH.md "元素、反应与地面状态", CONTENT_AND_BALANCE §4.4). Each target
// life holds at most one element: fire is its burn layers (LDCombatKeywords); water, shock, chill and rot are one mark. A hit's
// elements apply as one batch in the order fire, water, shock, chill, rot; a different element already held reacts with the
// new one through the 5 x 5 table (ReactionOf). Pure rules over primitive values; ALDCombatDirector applies them to its body
// records (LDCombatElementsDirector.cpp). Shock keeps the number of the former electric element.
enum class ELDCombatElement : uint8 { None = 0, Fire = 1, Water = 2, Shock = 3, Chill = 4, Rot = 5 };
constexpr int32 LDCombatElementCount = 6;
// Spec §8.2: the ten reactions of the five elements. Numbers 6, 9 and 12 are retired and never reused.
enum class ELDCombatReaction : uint8
{
    None = 0, Fog = 1, Conduct = 2, Overload = 3, Melt = 4, Methane = 5, Freeze = 7, Ferment = 8, Shatter = 10, Corrode = 11,
    Frostbite = 13
};
// One past the highest reaction number.
constexpr int32 LDCombatReactionCount = 14;

// Spec §8.3 ground states; 0 is none. The interface draws each with its own pattern.
enum class ELDGroundStateKind : uint8 { None = 0, Puddle = 1, Ice = 2, Fire = 3, Miasma = 4, Field = 5 };
constexpr int32 LDGroundStateKindCount = 6;

// AI spec §8 event categories of one committed hit. BasicPrimary: an own basic primary hit (empowerments merged in).
// SkillDirect: an active's own damage (its native hits, zone pulses, the S16 answer). SkillHit: other skill hits that are
// not status damage (connector answers, added contact damage). StatusSettle: burn and rot ticks, early fire settles, shock
// strikes, ground ticks and reaction damage; these never feed skill-hit readers.
enum class ELDCombatHitCategory : uint8 { BasicPrimary, SkillDirect, SkillHit, StatusSettle };

// The approved element, reaction and ground-state terms (COMBAT.* and GROUND.* element parameters).
struct LOCKDOWN_API FLDElementTerms
{
    int64 WetMs = 0;
    int64 WetSlowBp = 0;
    int64 ShockMs = 0;
    int32 ShockEveryNth = 0;
    int64 ShockRatioBp = 0;
    double ShockSplashRadiusCm = 0;
    int64 ChillMs = 0;
    int64 ChillAttackSlowBp = 0;
    int64 ChillMoveSlowBp = 0;
    int64 RotMs = 0;
    int64 RotHealCutBp = 0;
    int64 RotPerSecP2 = 0;
    int64 RotAPRatioPerSecBp = 0;
    int64 RotTickMs = 0;
    // The short shove of a fan blow or a typhoon (COMBAT.PushSec).
    int64 PushMs = 0;
    int64 ReactionBaseP2 = 0;
    int64 ReactionPerLevelP2 = 0;
    int64 ReactionAPRatioBp = 0;
    int64 ReactionLockMs = 0;
    double FogRadiusCm = 0;
    int64 FogMs = 0;
    int64 FogIntervalMs = 0;
    // Spec §11.1 heat day: a fog reaction makes no fog area.
    bool bFogArea = true;
    int64 ConductRatioBp = 0;
    double ConductRadiusCm = 0;
    int64 ConductSlowBp = 0;
    int64 ConductSlowMs = 0;
    int64 OverloadRatioBp = 0;
    double OverloadRadiusCm = 0;
    int32 OverloadBreachStacks = 0;
    int64 MeltRatioBp = 0;
    int64 MethaneRatioBp = 0;
    int32 MethaneBurnStacks = 0;
    double MethaneRadiusCm = 0;
    int64 FreezeRootMs = 0;
    int64 FermentRotMs = 0;
    int64 FermentHealCutBp = 0;
    int64 ShatterSkillDamageBp = 0;
    int64 ShatterMs = 0;
    int32 CorrodeBreachStacks = 0;
    int64 CorrodeBreachMs = 0;
    int64 FrostbiteAttackSlowBp = 0;
    int64 FrostbiteMs = 0;

    static FLDElementTerms From(const FLDDemoContentParameters& Parameters);
    // Every term the rules divide or step by is positive.
    bool IsUsable() const;
};

// One ground-state kind's approved terms (GROUND.*): radius, life and tick (every TickMs from its creation while it lasts).
struct FLDGroundKindTerms
{
    double RadiusCm = 0;
    int64 LifeMs = 0;
    int64 TickMs = 0;
};

struct LOCKDOWN_API FLDGroundTerms
{
    FLDGroundKindTerms Kinds[LDGroundStateKindCount];
    // Ice: an enemy's movement acceleration scale (inertia) and its knockback and dash distance scale.
    int64 IceAccelScaleBp = 0;
    int64 IceDisplacementScaleBp = 0;
    int32 FireBurnStacks = 0;
    int64 FieldRatioBp = 0;
    int32 MaxPerOwner = 0;
    int32 MaxTotal = 0;

    static FLDGroundTerms From(const FLDDemoContentParameters& Parameters);
    bool IsUsable() const;
    const FLDGroundKindTerms& Of(ELDGroundStateKind Kind) const { return Kinds[FMath::Clamp(int32(Kind),0,LDGroundStateKindCount-1)]; }
};

// Who applied an element and the snapshot its reaction damage, shock strikes and rot ticks read (the applier's level and AP
// when the element was applied). CastLedger is carried only for once-per-release keystone refunds.
struct FLDElementApplier
{
    FGuid SourceEntity;
    FLDCombatBodyRef SourceBody;
    FGuid CreditOwner;
    FLDCombatSourceRef Source;
    int32 Level = 1;
    int64 SourceAPP2 = 0;
    // The release whose hit applied this element (K25 keys its once-per-release cut on it); unset otherwise.
    FGuid EffectId;
    TSharedPtr<FLDCombatCastLedger> CastLedger;
};

// The held mark of a target life (water, shock, chill or rot). Wet and chill hold their slow instances (SlowEffectId,
// AttackSlowEffectId) that end with the mark. Shock counts the direct hits taken since it landed (every ShockEveryNth-th one
// strikes). Rot cuts new heals and shields by HealCutBp and ticks its damage every RotTickMs from NextTickMs.
struct FLDCombatElementMark
{
    ELDCombatElement Kind = ELDCombatElement::None;
    FLDElementApplier Applier;
    FGuid SlowEffectId;
    FGuid AttackSlowEffectId;
    int64 ExpiresAtMs = 0;
    int32 ShockHits = 0;
    int64 HealCutBp = 0;
    int64 NextTickMs = 0;
};

// A completed reaction (AI spec §8 "反应完成"): the trigger is the applier of the new element and TriggerSource the source
// of the hit that applied it; connectors that read "a reaction this source triggered" key on it. Consumed is the element the
// primary target held.
struct FLDCombatReactionFact
{
    ELDCombatReaction Kind = ELDCombatReaction::None;
    ELDCombatElement Consumed = ELDCombatElement::None;
    FLDCombatBodyRef TriggerBody;
    FGuid TriggerEntity;
    FLDCombatSourceRef TriggerSource;
    FGuid TriggerEffectId;
    FLDCombatBodyRef Primary;
    FGuid HitId;
    int64 AtMs = 0;
};

// The fog of one owner (fire + water, or fire / water on a puddle or fire ground): a fixed zone that gives element-free
// enemies inside wet at its start and every FogIntervalMs until it ends. One per owner; a newer fog replaces it.
struct FLDCombatFog
{
    FGuid EffectId;
    FLDElementApplier Owner;
    FVector CentreCm = FVector::ZeroVector;
    double RadiusCm = 0;
    int64 StartMs = 0, EndMs = 0, NextPulseMs = 0;
};

// Spec §8.3 one ground state: a circle (RadiusCm around CentreCm); for D10's ice path a band of half width RadiusCm along
// HalfLengthCm either side of CentreCm in Heading; for N1 A's cone a sector of RadiusCm and AngleDegrees from CentreCm in Heading. It harms only enemies of its owner, never blocks, never hides and is
// public (observed with its owner's seat). Every TickMs from its creation it lays its standing effect on the enemies whose
// foot is inside; an element landing inside converts it (LDCombatElements::GroundConversion).
struct FLDGroundState
{
    uint32 Id = 0;
    ELDGroundStateKind Kind = ELDGroundStateKind::None;
    FLDElementApplier Owner;
    FVector CentreCm = FVector::ZeroVector;
    double RadiusCm = 0;
    FVector2D Heading = FVector2D(1,0);
    double HalfLengthCm = 0;
    double AngleDegrees = 0;
    int64 StartMs = 0, ExpiresAtMs = 0, NextTickMs = 0;
    uint32 Version = 0;
    // The skill release (or card) that made it: a skill keeps one live instance per release source (Y12 copies add one).
    FLDCombatSourceRef Source;
    // N1 A1: the owner's damage factor on enemies standing on it; N3 A1: their attack slow while inside.
    int64 VulnerableBp = 0;
    int64 AttackSlowBp = 0;
    // D11: moved once already (never again).
    bool bRelocated = false;
    uint64 CreatedSerial = 0;
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
    // The share of the record the answer, heal and shield use (an A08 combo cast's, summed over its copies; 10000 otherwise).
    int64 ShareBp = 10000;
    // The release's shared ledger: the answer is that release's native direct damage (B13-B16).
    TSharedPtr<struct FLDCombatCastLedger> CastLedger;
};

// How one element application lands beyond the approved terms: a rot's own cut and duration.
struct FLDElementExtras
{
    int64 RotCutBp = 0, RotMs = 0;
};

// What a reaction does on its primary target (spec §8.2): damage as a multiple of R (0: none), controls and statuses, the
// splash around it and the ground state it leaves at its foot. Pure table data; ApplyElement commits it.
struct FLDReactionRule
{
    int64 PrimaryRatioBp = 0;
    // The splash: bodies within SplashRadiusCm of the primary (wet ones only for conduct) take SplashRatioBp x R.
    double SplashRadiusCm = 0;
    int64 SplashRatioBp = 0;
    bool bSplashWetOnly = false;
    ELDGroundStateKind Ground = ELDGroundStateKind::None;
};

namespace LDCombatElements
{
    // The reaction of a held element meeting an incoming one (spec §8.2 5 x 5 table); None when either is None or both are
    // the same.
    LOCKDOWN_API ELDCombatReaction ReactionOf(ELDCombatElement Held, ELDCombatElement Incoming);
    // The table terms of one reaction (ratios, splash, ground).
    LOCKDOWN_API FLDReactionRule RuleOf(ELDCombatReaction Reaction, const FLDElementTerms& Terms);
    // R = ReactionBase + ReactionPerLevel x (Level - 1) + ReactionAPRatio x AP, floored to P2 (not scaled by G).
    LOCKDOWN_API int64 ReactionDamageP2(int32 Level, int64 SourceAPP2, const FLDElementTerms& Terms);
    // Share x R (floored to P2).
    LOCKDOWN_API int64 ScaledP2(int64 ValueP2, int64 RatioBp);
    LOCKDOWN_API bool MarkActive(const FLDCombatElementMark& Mark, int64 AtMs);
    // The element a target life holds now: fire while any burn layer is live, otherwise its live mark.
    LOCKDOWN_API ELDCombatElement ElementOf(bool bBurning, const FLDCombatElementMark& Mark, int64 AtMs);
    // Shock: counts one more direct hit on a live shock mark; true when it is the ShockEveryNth-th since the mark landed (the
    // count starts again). Overcharge (S10 B1) strikes on every hit and leaves the count alone.
    LOCKDOWN_API bool CountShockHit(FLDCombatElementMark& Mark, int32 EveryNth, bool bOvercharged, int64 AtMs);
    // Rot: one tick of (RotPerSec + RotAPRatioPerSec x AP) x RotTick / 1 s, floored to P2.
    LOCKDOWN_API int64 RotTickP2(int64 SourceAPP2, const FLDElementTerms& Terms);
    // A new heal or shield of AmountP2 under a live rot mark: floor(Amount x (1 - HealCut)).
    LOCKDOWN_API int64 AfterRotP2(int64 AmountP2, const FLDCombatElementMark& Mark, int64 AtMs);
    // Spec §8.3 "再加元素": what a ground state of Kind becomes when Incoming lands inside it. Same kind = None (no change);
    // bVanishes = it ends (after its burst, if any); bFog = a fog rises at its centre; bBurst = methane (miasma + fire:
    // everyone inside takes MethaneRatio x R).
    struct FGroundConversion
    {
        ELDGroundStateKind Becomes = ELDGroundStateKind::None;
        bool bVanishes = false, bFog = false, bBurst = false;
        bool Any() const { return Becomes != ELDGroundStateKind::None || bVanishes || bFog || bBurst; }
    };
    LOCKDOWN_API FGroundConversion GroundConversion(ELDGroundStateKind Kind, ELDCombatElement Incoming);
    // The element a ground state lays on an enemy standing in it at each tick (None for ice).
    LOCKDOWN_API ELDCombatElement GroundElement(ELDGroundStateKind Kind);
    // Whether Foot is inside the ground state (circle, band or sector, planar).
    LOCKDOWN_API bool GroundContains(const FLDGroundState& Ground, const FVector& Foot);
    // Spec §8.3 caps: Owner's live states beyond MaxPerOwner - 1 and all beyond MaxTotal - 1 go first, oldest first, so one
    // more fits. Returns the indices to remove (descending).
    LOCKDOWN_API void GroundEvictions(const TArray<FLDGroundState>& Live, const FGuid& Owner, int32 MaxPerOwner, int32 MaxTotal,
        TArray<int32>& OutDescending);
    // The record after one cut: Recorded + Cut, at most Cap.
    LOCKDOWN_API int64 BraceRecordP2(int64 RecordedP2, int64 CutP2, int64 CapP2);
    // The answer's raw magic damage: Base + RecordRatio x Recorded, floored to P2.
    LOCKDOWN_API int64 BraceAnswerP2(const FLDCombatBrace& Brace);
    LOCKDOWN_API bool BraceActive(const FLDCombatBrace& Brace, int64 AtMs);
    // The AI spec §8 category of one hit.
    LOCKDOWN_API ELDCombatHitCategory CategoryOf(const FLDCombatHit& Hit);
    LOCKDOWN_API const TCHAR* ReactionName(ELDCombatReaction Reaction);
    LOCKDOWN_API const TCHAR* ElementName(ELDCombatElement Element);
    LOCKDOWN_API const TCHAR* GroundName(ELDGroundStateKind Kind);
    // The combat element a content element names (content "electric" is shock).
    LOCKDOWN_API ELDCombatElement FromDemo(ELDDemoElement Element);
    // The bit of Element in a hit's native element mask (FLDCombatHit::NativeElements).
    constexpr uint8 Bit(ELDCombatElement Element) { return uint8(1u << uint8(Element)); }
}
