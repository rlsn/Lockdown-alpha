#pragma once

#include "CoreMinimal.h"

class ALDPlayerController;

#if WITH_DEV_AUTOMATION_TESTS
#include "Evaluation/LDEvaluationPolicy.h"

// DEV build evaluation driver (docs/systems/BUILD_EVALUATION.md). Scripts/BuildEvaluationLockdown.py writes this input;
// the Host probe runs every Case as one ordinary match of the Demo room and writes one result and one trace per Case.

// One AI duelist of a Case.
struct FLDEvalContenderPlan
{
    // "subject" or "opponent".
    FString Role;
    // AI roster slot 0..1; its contender identity comes from the Case seed.
    int32 Slot = 0;
    // XP granted once at T=0 through the reward composite (a supply assumption of the plan, logged cause=dev_fixture).
    int64 SupplyXP = 0;
    // One rule per active opportunity ordinal from 1 (the opening offer).
    TArray<FLDEvaluationChoice> Choices;
    // Conditional preparation: one-to-three-active offers queued (DevQueueOffer) for the next opportunities from ordinal 2,
    // oldest first.
    TArray<TArray<FName>> Presets;
    // #198 workshop plan (empty: the production follow-up rule builds), in order.
    TArray<FLDEvaluationBuildStep> Build;
};

struct FLDEvalCase
{
    FString CaseId;
    uint64 Seed = 0;
    // The public place both duelists walk to.
    FName Site;
    // Authority time after the first field begins at which an undecided duel ends as censored.
    int64 DuelTimeoutMs = 0;
    // Wall-clock budget of the whole Case; exceeding it is BudgetExceeded, never a result.
    int32 WallTimeoutSeconds = 0;
    TArray<FLDEvalContenderPlan> Contenders;
};

struct FLDEvalRunInput
{
    FString RunId;
    // The planned runtime catalog root; a different loaded catalog refuses the whole run.
    FString ContentRoot;
    TArray<FLDEvalCase> Cases;
};

namespace LDBuildEvaluation
{
    inline constexpr TCHAR InputSchema[] = TEXT("lockdown.build_evaluation.native_input");
    inline constexpr int32 InputSchemaVersion = 3;
    inline constexpr TCHAR ResultSchema[] = TEXT("lockdown.build_evaluation.native_result");
    inline constexpr TCHAR RunSchema[] = TEXT("lockdown.build_evaluation.native_run");
    inline constexpr int32 ResultSchemaVersion = 4;
    inline constexpr int32 MaxCases = 500;
    inline constexpr int32 MaxChoices = 16;
    inline constexpr int32 MaxBuildSteps = 32;
    // The duel needs exactly two AI slots; the evaluation Host is the only human and never fights.
    inline constexpr int32 DuelAISlots = 2;
    inline bool KillAtOrBeforeHorizon(int64 StartedAtMs, int64 DuelTimeoutMs, int64 OccurredAtMs)
    {
        return OccurredAtMs >= StartedAtMs && OccurredAtMs <= StartedAtMs + DuelTimeoutMs;
    }
    // Strict: unknown or missing fields, out-of-range numbers and malformed rules are refused with the field named.
    bool ParseInput(const FString& Json, FLDEvalRunInput& Out, FString& Error);
}

namespace LDDemoTests
{
    // -LDBuildEvaluation -LDBuildEvalInput=<json> -LDBuildEvalOutput=<dir under Artifacts>, Listen Host of L_Demo only.
    void StartBuildEvaluation(ALDPlayerController& Controller);
}
#endif
