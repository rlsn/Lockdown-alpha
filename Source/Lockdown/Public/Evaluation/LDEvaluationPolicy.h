#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AI/LDOpponentPolicy.h"

class FLDDemoContentSnapshot;

// DEV build evaluation strategy (docs/systems/BUILD_EVALUATION.md). One fixed, symmetric duel strategy for the two AI
// contenders of an evaluation Case. It reads only the authorized decision frame the production policy reads and returns
// intents the opponent adapter submits through the same domain entries and receipts. What it fixes is whom to fight, where
// to meet and which offered active to take (always asking to learn it now); how to fight is the production engagement
// (LDOpponentPolicy::EngageUnit).
// Build ownership: a profile without planned build steps leaves every other build command (the opening base pick, learning
// blueprints, installing components, allocating points and whatever else the production rule decides) to
// LDOpponentPolicy::DecideFollowUpBuild. A profile with planned build steps owns every build command except the active
// choices: it sends exactly its steps, in order, and the production follow-up rule never runs for it.

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

// #198 workshop plan: the build window a planned step runs in.
enum class ELDEvaluationStepWindow : uint8
{
    // The opening rest R0 while its build window is open, after the opening active choice (before the field supply).
    OpeningRest,
    // The field, once the supply's level is reached and every planned active choice is consumed.
    Field,
};

// One planned build command, sent as a player sends it (FLDOpponentBuildIntent) only when growth's gate admits it.
enum class ELDEvaluationStepKind : uint8
{
    OpeningBase, AllocatePoints, InstallComponent, UninstallComponent, LearnBlueprint, ReplaceActive, Respec
};

struct FLDEvaluationBuildStep
{
    ELDEvaluationStepKind Kind = ELDEvaluationStepKind::OpeningBase;
    ELDEvaluationStepWindow Window = ELDEvaluationStepWindow::OpeningRest;
    // OpeningBase / InstallComponent / UninstallComponent: the component.
    FString Component;
    // An ActiveElement binding (B18): the bound learned active and its element; empty / None otherwise.
    FString BoundActive;
    ELDDemoElement Element = ELDDemoElement::None;
    // InstallComponent of a mod or connector: a learned active or an installed host (Active / Component with TargetId), the
    // innate D (Dash) or the holder itself (None); C12 also names the installed base it repeats (BoundComponent).
    ELDComponentTargetKind TargetKind = ELDComponentTargetKind::None;
    FString TargetId;
    FString BoundComponent;
    // LearnBlueprint / ReplaceActive: the pending blueprint and the slot 0..2 (Q/W/E).
    FString Blueprint;
    uint8 Slot = 0;
    // AllocatePoints: exact points per stat id (each stat at most once, 1..255).
    TArray<FLDOpponentStatPoints> Points;
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
    // #198 workshop plan, in order (empty: the production follow-up rule builds). A step runs only in its window, after the
    // previous step's effect is visible in the own build; the strategy fights once every step is committed.
    TArray<FLDEvaluationBuildStep> Build;
    // The level the supply reaches (field steps wait for it).
    uint8 SupplyLevel = 1;
};

enum class ELDEvaluationPhase : uint8 { Waiting, Building, Approaching, Engaging, Failed };

struct FLDEvaluationProfileState
{
    ELDEvaluationPhase Phase = ELDEvaluationPhase::Waiting;
    // The first refusal of the plan (a planned active absent from the actual offer, an option index beyond it, an unknown
    // site, a planned build step that cannot run or that growth refused). The strategy then stops; the Case is invalid,
    // never completed with another active or build.
    FString Failure;
    // Planned build steps whose effect the own build shows, in order.
    int32 StepsCommitted = 0;
    // The current step was sent at least once; its effect is awaited.
    bool bStepIssued = false;
    // AllocatePoints: the own stat rows when the step was first sent.
    TArray<FLDOpponentStatPoints> StepBaseline;
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
    // The planned step's command name (the ELDBuildCommandKind it sends).
    LOCKDOWN_API const TCHAR* StepName(ELDEvaluationStepKind Kind);
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
