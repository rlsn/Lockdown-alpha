#pragma once

#include "CoreMinimal.h"

class ALDPlayerController;

#if WITH_DEV_AUTOMATION_TESTS
#include "Evaluation/LDEvaluationPolicy.h"
#include "Framework/LDCaptureTypes.h"

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
    // Diagnostic-only: exactly one common component for each level crossed by the XP supply.
    TArray<FName> LevelComponents;
    // One rule per active opportunity ordinal from 1 (the opening offer).
    TArray<FLDEvaluationChoice> Choices;
    // Diagnostic-only opening offer, staged for the armed AI slot before match start; empty means ordinary draw.
    TArray<FName> OpeningPreset;
    // Conditional preparation: one-to-three-active offers queued (DevQueueOffer) for the next opportunities from ordinal 2,
    // oldest first.
    TArray<TArray<FName>> Presets;
    // #198 workshop plan (empty: the production follow-up rule builds), in order.
    TArray<FLDEvaluationBuildStep> Build;
    // Optional "stance" object (absent: pursue): {"kind": "kite"|"burst"|"hold"|"guard", "standoff_cm": N,
    // "threshold_bp": N (guard only, required), "leash_cm": N (optional)}, with the optional operation knobs "reaction_ms",
    // "aim_error_cm" and "skip_bp" on any stance; {"kind": "pursue"} is written only to carry knobs.
    ELDEvaluationStance Stance = ELDEvaluationStance::Pursue;
    int32 StandoffCm = 0;
    int32 ThresholdBp = 0;
    int32 LeashCm = 0;
    int32 ReactionMs = 0;
    int32 AimErrorCm = 0;
    int32 SkipBp = 0;
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

struct FLDEvalOutcome
{
    int32 VictimSide = INDEX_NONE;
    FGuid Killer;
    int64 OccurredAtMs = -1;
    bool bTrade = false;
};

namespace LDBuildEvaluation
{
    inline constexpr TCHAR InputSchema[] = TEXT("lockdown.build_evaluation.native_input");
    inline constexpr int32 InputSchemaVersion = 5;
    inline constexpr TCHAR ResultSchema[] = TEXT("lockdown.build_evaluation.native_result");
    inline constexpr TCHAR RunSchema[] = TEXT("lockdown.build_evaluation.native_run");
    inline constexpr int32 ResultSchemaVersion = 6;
    inline constexpr int32 MaxCases = 500;
    inline constexpr int32 MaxChoices = 16;
    inline constexpr int32 MaxBuildSteps = 32;
    // The duel needs exactly two AI slots; the evaluation Host is the only human and never fights.
    inline constexpr int32 DuelAISlots = 2;
    inline bool KillAtOrBeforeHorizon(int64 StartedAtMs, int64 DuelTimeoutMs, int64 OccurredAtMs)
    {
        return OccurredAtMs >= StartedAtMs && OccurredAtMs <= StartedAtMs + DuelTimeoutMs;
    }
    // Collect all duel deaths at the earliest authority time before deciding whether it was a single kill or a trade.
    bool SelectDuelOutcome(const TArray<FLDKillNotice>& Notices, uint64& LastSerial, const FGuid& Subject, const FGuid& Opponent,
        int64 StartedAtMs, int64 DuelTimeoutMs, FLDEvalOutcome& Out);
    // Strict: unknown or missing fields, out-of-range numbers and malformed rules are refused with the field named.
    bool ParseInput(const FString& Json, FLDEvalRunInput& Out, FString& Error);
}

namespace LDDemoTests
{
    // -LDBuildEvaluation -LDBuildEvalInput=<json> -LDBuildEvalOutput=<dir under Artifacts>, Listen Host of L_Demo only.
    void StartBuildEvaluation(ALDPlayerController& Controller);
}
#endif
