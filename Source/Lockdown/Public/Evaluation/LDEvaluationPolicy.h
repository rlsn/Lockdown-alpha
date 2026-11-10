#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AI/LDOpponentPolicy.h"

class FLDDemoContentSnapshot;

// DEV build evaluation strategy (docs/systems/BUILD_EVALUATION.md). One fixed, symmetric duel strategy for the two AI
// contenders of an evaluation Case. It reads only the authorized decision frame the production policy reads and returns
// intents the opponent adapter submits through the same domain entries and receipts. What it fixes is whom to fight, where
// to meet and which offered active to take (always asking to learn it now); the opening base pick, learning blueprints,
// enabling components and allocating points are the production rule (LDOpponentPolicy::DecideFollowUpBuild) and how to
// fight is the production engagement (LDOpponentPolicy::EngageUnit).

enum class ELDEvaluationChoiceRule : uint8
{
    // The production active preference (LDOpponentPolicy::SelectActive).
    AISelect,
    // The option at Index (0..2) of the actual active offer.
    OptionIndex,
    // The actual active offer's option of DefinitionId.
    Definition,
};

struct FLDEvaluationChoice
{
    ELDEvaluationChoiceRule Rule = ELDEvaluationChoiceRule::AISelect;
    int32 Index = INDEX_NONE;
    FString DefinitionId;
};

struct FLDEvaluationProfile
{
    // The designated rival: the only contender this strategy engages.
    FGuid Opponent;
    // The public place both duelists walk to.
    FName Site;
    // One rule per active opportunity ordinal from 1 (the opening offer); the strategy fights once all of them are consumed
    // and the build steps they unlocked are settled.
    TArray<FLDEvaluationChoice> Choices;
};

enum class ELDEvaluationPhase : uint8 { Waiting, Building, Approaching, Engaging, Failed };

struct FLDEvaluationProfileState
{
    ELDEvaluationPhase Phase = ELDEvaluationPhase::Waiting;
    // The first refusal of the plan (a planned active absent from the actual offer, an option index beyond it, an unknown
    // site). The strategy then stops; the Case is invalid, never completed with another active.
    FString Failure;
};

// One profile and its evaluation state, shared by the Host probe (reads the state) and the adapter (runs the strategy).
struct FLDEvaluationRun
{
    FLDEvaluationProfile Profile;
    FLDEvaluationProfileState State;
    // Set by the opponent adapter that took this profile at its bind.
    bool bBound = false;
};

namespace LDEvaluationPolicy
{
    // Arrival distance to the meeting place; inside it the body holds instead of ordering another walk.
    inline constexpr double SiteArrivalCm = 200.;
    // The active of the current offer under Rule, or false with Error; never another active.
    LOCKDOWN_API bool Choose(const FLDOpponentSelf& Self, const FLDDemoContentSnapshot& Catalog, const FLDEvaluationChoice& Rule,
        FString& OutOption, FString& Error);
    LOCKDOWN_API FLDOpponentDecision Decide(const FLDOpponentDecisionFrame& Frame, const FLDDemoContentSnapshot& Catalog,
        const FLDEvaluationProfile& Profile, FLDOpponentPolicyState& Policy, FLDEvaluationProfileState& State);
}

// Profiles the Host probe staged for the next match, keyed by contender identity (the evaluation seeds fix the AI
// identities before the start). An adapter takes its profile when it binds; without one it runs the production policy.
namespace LDEvaluationProfiles
{
    // Refused without -LDBuildEvaluation.
    LOCKDOWN_API bool Stage(const FGuid& Contender, const FLDEvaluationProfile& Profile, FString& Error);
    LOCKDOWN_API TSharedPtr<FLDEvaluationRun> Find(const FGuid& Contender);
    LOCKDOWN_API void Clear();
}
#endif
