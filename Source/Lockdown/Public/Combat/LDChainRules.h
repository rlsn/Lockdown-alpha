#pragma once

#include "CoreMinimal.h"
#include "LDChainRules.generated.h"

// Spec §6.5 chains: one contender action (a basic start, a skill release, a dash) opens a chain; every effect it derives
// directly or indirectly (card triggers, reactions, ground interactions, contender kills) is one step of it. The chain ends
// CHAIN.EndSec after its last step; its key is the kinds of its steps in order, each kind once.

// One ended chain the owner receives (only its owner: chains are never public). Key holds the step kinds: a card id, a
// reaction (reaction_<name>), a ground state (ground_<kind>) or kill.
USTRUCT()
struct LOCKDOWN_API FLDChainEndView
{
    GENERATED_BODY()

    // Per owner, from 1; a client shows each serial once.
    UPROPERTY() uint32 Serial = 0;
    UPROPERTY() TArray<FName> Key;
    UPROPERTY() int32 Steps = 0;
    UPROPERTY() int64 DamageP2 = 0;
};

// Spec §6.5 结算“本局最离谱”: one contender's longest chain of the match and its single strongest hit on a contender.
USTRUCT()
struct LOCKDOWN_API FLDResultHighlight
{
    GENERATED_BODY()

    UPROPERTY() FGuid Contender;
    UPROPERTY() TArray<FName> LongestKey;
    UPROPERTY() int32 LongestSteps = 0;
    // The strongest hit: its source (a card, a skill, the basic or a reaction) by kind and definition.
    UPROPERTY() uint8 BestSourceKind = 0;
    UPROPERTY() FName BestSource;
    UPROPERTY() int64 BestDamageP2 = 0;
    UPROPERTY() int64 BestTargetMaxHpP2 = 0;
};

namespace LDChain
{
    // An ended chain's owner view keeps the last this many ends; the server logs each owner's longest this many (LD_CHAIN).
    inline constexpr int32 OwnerRecentEnds = 4;
    inline constexpr int32 LoggedPerOwner = 5;
    // The local codex keeps this many keys (the oldest first seen leaves first).
    inline constexpr int32 CodexCapacity = 300;

    // The key of Steps: each kind once, in the order it first appeared.
    LOCKDOWN_API void Key(const TArray<FName>& Steps, TArray<FName>& Out);
    // Whether a key this long counts (CHAIN.MinKeySteps).
    LOCKDOWN_API bool Counts(const TArray<FName>& Key, int32 MinSteps);
    // The key as one string joined by Separator (">" in logs and the codex, " → " in the HUD).
    LOCKDOWN_API FString Text(const TArray<FName>& Key, const TCHAR* Separator);
    // The step kind of a reaction or a ground state.
    LOCKDOWN_API FName ReactionStep(const TCHAR* ReactionName);
    LOCKDOWN_API FName GroundStep(const TCHAR* GroundName);
    // The kill step.
    LOCKDOWN_API FName KillStep();
    // Inserts an ended chain of Steps into a longest-first list capped at Capacity; true when it entered the list.
    LOCKDOWN_API bool KeepLongest(TArray<int32>& Longest, int32 Steps, int32 Capacity);
}
