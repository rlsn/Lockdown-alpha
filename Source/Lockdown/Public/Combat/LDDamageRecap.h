#pragma once
#include "CoreMinimal.h"
#include "Combat/LDCombatTypes.h"
#include "Combat/LDChainRules.h"
#include "LDDamageRecap.generated.h"

// Owner-only recap of one field death: what the dead life took from other bodies within WindowMs before the fatal hit
// (REWARD.AssistWindowSec, the assist window), grouped by attacker and source and rule chain. Lines keep
// LDDamageRecap::Summarize's order. Killer (a contender) or KillerMonsterId (a camp member) names who landed the fatal hit; Assisters are the contenders credited with the assist, as frozen in the death fact.
USTRUCT()
struct FLDDeathRecapView
{
    GENERATED_BODY()
    UPROPERTY() uint32 Match = 0;
    UPROPERTY() uint32 Life = 0;
    UPROPERTY() int64 WindowMs = 0;
    UPROPERTY() FGuid Killer;
    UPROPERTY() FName KillerMonsterId;
    UPROPERTY() TArray<FGuid> Assisters;
    UPROPERTY() TArray<FLDDamageLine> Lines;
};

// Owner-only reaction assist prompt: a reaction consumed an element this contender had applied (Consumed,
// ELDCombatElement) on a body it saw, as Reaction (ELDCombatReaction), at authority AtMs, during its life Life. Trigger is
// the contender that caused it, sent only when the setter also saw the trigger's body (otherwise unset: an unnamed trigger).
// Serial is client-local: the order of arrival on the owning connection, never sent.
USTRUCT()
struct FLDReactionAssistView
{
    GENERATED_BODY()
    UPROPERTY() uint32 Match = 0;
    UPROPERTY() uint32 Life = 0;
    UPROPERTY() FGuid Trigger;
    UPROPERTY() uint8 Consumed = 0;
    UPROPERTY() uint8 Reaction = 0;
    UPROPERTY() int64 AtMs = 0;
    uint64 Serial = 0;
};

// Owner-only match total of the damage credited to one contender, per source, frozen with the result.
USTRUCT()
struct FLDDamageSummaryView
{
    GENERATED_BODY()
    UPROPERTY() uint32 Match = 0;
    UPROPERTY() FGuid ResultId;
    UPROPERTY() TArray<FLDDamageLine> Lines;
    // Spec §6.5 结算“本局最离谱”: every contender's longest chain and strongest hit, by contender id.
    UPROPERTY() TArray<FLDResultHighlight> Highlights;
};

// How often one card of the build triggered (a card definition id or an installed card's key).
USTRUCT()
struct FLDCardTriggerRow
{
    GENERATED_BODY()
    UPROPERTY() FName Card;
    UPROPERTY() int32 Count = 0;
};

// Owner-only build statistics of the running match, sent when a rest begins (ALDGameMode::SendBuildStats): the own
// committed damage per source and the trigger count of each card, both without training. Serial grows with every send.
USTRUCT()
struct FLDBuildStatsView
{
    GENERATED_BODY()
    UPROPERTY() uint32 Match = 0;
    UPROPERTY() uint32 Serial = 0;
    UPROPERTY() TArray<FLDDamageLine> Lines;
    UPROPERTY() TArray<FLDCardTriggerRow> Triggers;
};

// Owner-only summary of one training combo on a dummy (ALDGameMode::SendTrainingCombos): the damage per source of the hits
// that followed each other within the quiet gap, sent once the gap passed. Serial grows with every send.
USTRUCT()
struct FLDTrainingComboView
{
    GENERATED_BODY()
    UPROPERTY() uint32 Match = 0;
    UPROPERTY() uint32 Serial = 0;
    UPROPERTY() TArray<FLDDamageLine> Lines;
};

// One committed hit on a victim life, kept only while it can still fall inside the recap window.
struct FLDDamageSample
{
    int64 AtMs = 0;
    FLDDamageLine Line;
};

// Pure rules of the death recap and the per-source damage totals; ALDCombatDirector feeds them from committed hits.
namespace LDDamageRecap
{
    // Transport bound of one view; a list that would exceed it is not sent.
    inline constexpr int32 MaxLines = 96;

    // Appends one hit taken at NowMs and drops samples older than WindowMs before it. A non-positive amount or window
    // records nothing. Returns true when the sample was added.
    LOCKDOWN_API bool Record(TArray<FLDDamageSample>& Log, const FLDDamageLine& Hit, int64 NowMs, int64 WindowMs);

    // The samples at most WindowMs before DeathAtMs (inclusive, as LDAssist::SelectAssisters), summed per attacker, source
    // and rule chain (setter, consumed element, reaction).
    // Attackers come by their total, largest first; within one attacker its lines come largest first; ties keep a fixed
    // order (contender id, monster id, source kind, source id, reaction, consumed element, setter).
    LOCKDOWN_API void Summarize(const TArray<FLDDamageSample>& Log, int64 DeathAtMs, int64 WindowMs, TArray<FLDDamageLine>& Out);

    // Before Summarize: clears every setter the victim may not read, so its chain shows the element unnamed. A setter stays
    // named when it is the victim itself, a contender attacker of a sample inside the window, or one of the frozen Assisters.
    LOCKDOWN_API void HideUnreadableSetters(TArray<FLDDamageSample>& Log, int64 DeathAtMs, int64 WindowMs, const FGuid& Victim,
        const TArray<FGuid>& Assisters);

    // Adds DamageP2 to Source's row of one contender's match totals. Non-positive amounts add nothing.
    LOCKDOWN_API bool AddTotal(TArray<FLDDamageLine>& Totals, const FLDCombatSourceRef& Source, int64 DamageP2);

    // Largest total first; ties by source kind, then source id.
    LOCKDOWN_API void SortTotals(TArray<FLDDamageLine>& Totals);

    // Receiver-side shape check: bounded, every amount positive, a known element and reaction, a consumed
    // element only on reaction damage (an Overload splash names none), a setter only with a consumed element, no repeated
    // attacker-source-chain key.
    LOCKDOWN_API bool ValidateLines(const TArray<FLDDamageLine>& Lines, FString& Error);

    // Receiver-side shape check of a reaction assist prompt: a match and life, a known element and reaction (the trigger is
    // optional).
    LOCKDOWN_API bool ValidateReactionAssist(const FLDReactionAssistView& View, FString& Error);

    enum class EAssistKeep : uint8 { Kept, Stale, Invalid };
    // Owner-side keep rule of a received assist prompt. Invalid shapes are refused; a prompt of an older (match, life) than
    // the known own state (OwnMatch/OwnLife; 0 when not yet known) or than any kept prompt is stale and changes nothing, so a
    // late older prompt never wipes newer ones. A kept prompt replaces those of older (match, life), takes the next local
    // serial and the oldest beyond Cap are dropped.
    LOCKDOWN_API EAssistKeep KeepReactionAssist(TArray<FLDReactionAssistView>& Kept, uint64& Serial, const FLDReactionAssistView& View,
        uint32 OwnMatch, uint32 OwnLife, int32 Cap, FString& Error);
}
