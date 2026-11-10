#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS
// DEV-only randomness of one build evaluation Case (docs/systems/BUILD_EVALUATION.md). It exists only in builds with
// automation tests and acts only in a process started with -LDBuildEvaluation, after the Host evaluation probe armed a
// Case root seed in the Lobby. Every match start then draws the named streams below instead of fresh entropy, so the
// same Case replays the same offers, critical rolls, spawn anchors, AI opening plans, airdrop points and contender
// identities. Without the switch, or while nothing is armed, every hook keeps its production entropy unchanged.
struct FLDEvaluationSeeds
{
    static constexpr int32 MaxAISlots = 3;
    uint64 Root = 0;
    // Growth draws (LDProgression::OfferSeed material).
    FGuid MatchSecret;
    // The core's server stream: spawn anchors and the AI opening plans, in draw order.
    int32 SpawnSeed = 0;
    // Combat's ability and basic critical rolls.
    int32 CritSeed = 0;
    // The airdrop schedule's point sequence.
    uint64 AirdropSeed = 1;
    // Contender identity of each AI roster slot; identities order bodies, intents and deaths at one boundary.
    FGuid AIContenders[MaxAISlots];
};

namespace LDEvaluationSeeds
{
    // Version of the derivation below; a change makes earlier Case seeds incomparable.
    inline constexpr uint32 DerivationVersion = 1;
    // The process switch -LDBuildEvaluation, read once.
    LOCKDOWN_API bool IsSwitchPresent();
    // A named 64-bit stream of Root: CityHash64 of "LDEval/<version>/<Name>" followed by Root (little endian). Pure.
    LOCKDOWN_API uint64 Stream(uint64 Root, const TCHAR* Name);
    // A valid GUID from the two streams <Name>.hi and <Name>.lo. Pure.
    LOCKDOWN_API FGuid StreamGuid(uint64 Root, const TCHAR* Name);
    // Every stream of one Case. Pure.
    LOCKDOWN_API FLDEvaluationSeeds Derive(uint64 Root);
    // Host probe, Lobby only: the next match starts draw from Root's streams until Disarm. Refused without the switch.
    LOCKDOWN_API bool Arm(uint64 Root, FString& Error);
    LOCKDOWN_API void Disarm();
    // The armed streams, or nullptr: always nullptr without the switch.
    LOCKDOWN_API const FLDEvaluationSeeds* Armed();
    // Under the switch, room members get process-stable session identities in join order (the evaluation Host is the
    // first), so a replay in a new process keeps every identity ordering; without it, a fresh GUID as in production.
    LOCKDOWN_API FGuid NextSessionId();
}
#endif
