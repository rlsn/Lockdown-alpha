#pragma once
#include "CoreMinimal.h"
#include "Combat/LDCombatTypes.h"
#include "LDDamageRecap.generated.h"

// Owner-only recap of one field death: what the dead life took from other bodies within WindowMs before the fatal hit
// (REWARD.AssistWindowSec, the assist window), grouped by attacker and source. Lines keep LDDamageRecap::Summarize's order.
USTRUCT()
struct FLDDeathRecapView
{
    GENERATED_BODY()
    UPROPERTY() uint32 Match = 0;
    UPROPERTY() uint32 Life = 0;
    UPROPERTY() int64 WindowMs = 0;
    UPROPERTY() TArray<FLDDamageLine> Lines;
};

// Owner-only match total of the damage credited to one contender, per source, frozen with the result.
USTRUCT()
struct FLDDamageSummaryView
{
    GENERATED_BODY()
    UPROPERTY() uint32 Match = 0;
    UPROPERTY() FGuid ResultId;
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

    // The samples at most WindowMs before DeathAtMs (inclusive, as LDAssist::SelectAssisters), summed per attacker and
    // source. Attackers come by their total, largest first; within one attacker its sources come largest first; ties
    // keep a fixed order (contender id, monster id, source kind, source id).
    LOCKDOWN_API void Summarize(const TArray<FLDDamageSample>& Log, int64 DeathAtMs, int64 WindowMs, TArray<FLDDamageLine>& Out);

    // Adds DamageP2 to Source's row of one contender's match totals. Non-positive amounts add nothing.
    LOCKDOWN_API bool AddTotal(TArray<FLDDamageLine>& Totals, const FLDCombatSourceRef& Source, int64 DamageP2);

    // Largest total first; ties by source kind, then source id.
    LOCKDOWN_API void SortTotals(TArray<FLDDamageLine>& Totals);

    // Receiver-side shape check: bounded, every amount positive, no repeated attacker-and-source key.
    LOCKDOWN_API bool ValidateLines(const TArray<FLDDamageLine>& Lines, FString& Error);
}
