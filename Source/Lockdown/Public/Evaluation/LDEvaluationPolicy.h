#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AI/LDOpponentPolicy.h"
#include "Combat/LDCombatTypes.h"

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
// Every card offer is skipped before a planned step (a case's cards come from its level cards, never a random offer).
enum class ELDEvaluationStepKind : uint8
{
    AllocatePoints, InstallCard, UninstallCard, LearnBlueprint, ReplaceActive, Respec
};

// Engagement stance. Every stance reads only the authorized frame (own state and sources, the visible rival's foot,
// public places); none sees projectiles, so none dodges them. Pursue is the production engagement
// (LDOpponentPolicy::EngageUnit).
// - Kite keeps StandoffCm from the rival: it takes only the non-dash activations the production picker offers, never walks
//   in for a basic attack, and while the rival is closer it clicks legal ground away from it (wider turns when refused).
// - Burst pursues while any own Q/W/E source is ready and kites at StandoffCm while all of them recover.
// - Hold defends the meeting place with radius StandoffCm: it pursues the rival only while the rival stands inside the
//   radius; otherwise it walks back to the place and takes only the non-dash activations the picker offers from there.
// - Guard pursues until its health falls below ThresholdBp of its maximum, then kites at StandoffCm until it recovers
//   GuardRecoverBp above the threshold.
// A positive LeashCm refuses every retreat step that would leave that distance from the meeting place and lead farther.
// Operation knobs (any stance; all 0: exact play). They are test policies of imperfect play, not claims about human
// skill, and gate only what the stance already chose, from the same authorized frame:
// - ReactionMs: an activation from a source slot goes only once that slot has been seen ready continuously for ReactionMs;
//   a retreat (Kite, Burst, Guard) starts only once the rival has stayed inside the standoff for ReactionMs.
// - AimErrorCm: an admitted ground-point activation lands up to AimErrorCm off its point (within its reach); a projectile,
//   line or cone turns by atan2(AimErrorCm * s, max(200, gap)), s in [-1, 1]. Unit, self and dash activations are exact.
// - SkipBp: when a slot's gate first opens in a readiness window, a roll below SkipBp of 10000 holds it another
//   max(ReactionMs, 500) ms, then rolls again.
// - WarningReactMs (-1: off, the strategy never dodges): a visible enemy warning counts only once it has been in sight for
//   WarningReactMs; the production dodge (LDOpponentPolicy::DodgeWarning, without its AI.DodgeChance roll) then dashes out
//   of it, before the stance fights.
// - LeadAim: a ground-point, projectile, line or cone activation aims at where the rival will be when it lands: its foot
//   plus its observed velocity (two consecutive sightings, capped at MaxLeadSpeedCmPerSec) times the windup, a ground
//   effect's delay and a projectile's flight, up to MaxLeadMs. Applied before AimErrorCm.
// Rolls are CityHash64 of the Case seed's "eval.operation" stream, the own contender identity and a per-profile counter.
enum class ELDEvaluationStance : uint8 { Pursue, Kite, Burst, Hold, Guard };

namespace LDEvaluationPolicy
{
    inline constexpr int32 MinStandoffCm = 200;
    inline constexpr int32 MaxStandoffCm = 2000;
    inline constexpr int32 MinThresholdBp = 1000;
    inline constexpr int32 MaxThresholdBp = 9000;
    inline constexpr int32 GuardRecoverBp = 1500;
    inline constexpr int32 MinLeashCm = 500;
    inline constexpr int32 MaxLeashCm = 5000;
    inline constexpr int32 MaxReactionMs = 1500;
    inline constexpr int32 MaxAimErrorCm = 400;
    inline constexpr int32 MaxSkipBp = 9000;
    inline constexpr int32 MaxWarningReactMs = 1500;
    // Lead aim: the fastest observed rival velocity it extrapolates, the longest lead and the oldest sighting it pairs.
    inline constexpr double MaxLeadSpeedCmPerSec = 1000.;
    inline constexpr int64 MaxLeadMs = 1500;
    inline constexpr int64 MaxLeadSampleGapMs = 1000;
    // A skipped slot is held at least this long before it rolls again.
    inline constexpr int32 MinSkipHoldMs = 500;
    // The shortest lever an aim error turns a direction by.
    inline constexpr double MinAimLeverCm = 200.;
    // One kite step: the length of the ground click away from the rival.
    inline constexpr double KiteStepCm = 350.;
    // The approach to the meeting place: a body that gained less than ApproachProgressCm in ApproachStuckMs stands behind
    // another body (a camp member on the planned route does not step aside, and a walk does not go round bodies), so it
    // takes one ApproachSidestepCm step to the side and walks on from there.
    inline constexpr double ApproachProgressCm = 100.;
    inline constexpr int64 ApproachStuckMs = 2500;
    inline constexpr double ApproachSidestepCm = 400.;
    // The source slots an operation gate tracks (Q, W, E, Dash).
    inline constexpr int32 OperationSlots = 4;
}

struct FLDEvaluationBuildStep
{
    ELDEvaluationStepKind Kind = ELDEvaluationStepKind::AllocatePoints;
    ELDEvaluationStepWindow Window = ELDEvaluationStepWindow::OpeningRest;
    // InstallCard / UninstallCard: the card definition (a stored copy installs, an installed copy comes off).
    FString Card;
    // InstallCard: its place (a learned skill names TargetId) and Y25's element.
    ELDCardTargetKind TargetKind = ELDCardTargetKind::None;
    FString TargetId;
    ELDDemoElement Element = ELDDemoElement::None;
    // LearnBlueprint / ReplaceActive: the pending blueprint and the slot 0..2 (Q/W/E).
    FString Blueprint;
    uint8 Slot = 0;
    // AllocatePoints: exact points per stat id (each stat at most once, 1..255).
    TArray<FLDOpponentStatPoints> Points;
};

struct FLDEvaluationProfile
{
    // How the strategy fights once its rival is visible (empty manifest field: Pursue).
    ELDEvaluationStance Stance = ELDEvaluationStance::Pursue;
    // Kite/Burst/Guard: the distance kept from the rival while retreating (cm); Hold: the defended radius. 0 for Pursue.
    int32 StandoffCm = 0;
    // Guard: the health share (basis points) below which it retreats. 0 otherwise.
    int32 ThresholdBp = 0;
    // Any stance but Pursue: the optional retreat leash around the meeting place (cm, 0: none).
    int32 LeashCm = 0;
    // Operation knobs (see ELDEvaluationStance): 0..MaxReactionMs, 0..MaxAimErrorCm, 0..MaxSkipBp; 0 is exact play.
    int32 ReactionMs = 0;
    int32 AimErrorCm = 0;
    int32 SkipBp = 0;
    // -1 (no dodge) or 0..MaxWarningReactMs, and lead aim (see ELDEvaluationStance).
    int32 WarningReactMs = -1;
    bool bLeadAim = false;
    // Auto-empower runs: the planned host the evaluation probe submits (None: the AI's own pick or -LDEmpowerHost).
    ELDCombatEntry EmpowerHost = ELDCombatEntry::None;
    // The Case root seed the operation rolls derive from.
    uint64 OperationSeed = 0;
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
    // The opening choice and all R0 build work have settled; planned field work may remain.
    bool bOpeningReady = false;
    // Guard: retreating until health recovers above the threshold plus GuardRecoverBp.
    bool bGuardRetreat = false;
    // Operation gates, per source slot (Q, W, E, Dash): since when the slot is seen ready continuously (-1: not ready), until
    // when a skip roll holds it, and whether this readiness window already passed its roll.
    int64 ReadySinceMs[LDEvaluationPolicy::OperationSlots] = {-1, -1, -1, -1};
    int64 SkipUntilMs[LDEvaluationPolicy::OperationSlots] = {0, 0, 0, 0};
    bool bSkipPassed[LDEvaluationPolicy::OperationSlots] = {false, false, false, false};
    // Since when the rival stands inside the standoff continuously (-1: not inside or not visible).
    int64 CloseSinceMs = -1;
    // The life the gates were observed in; another life starts them afresh.
    uint32 OperationLife = 0;
    // Rolls drawn so far (the roll counter).
    uint64 Rolls = 0;
    // WarningReactMs: since when each visible enemy warning (by handle) is in sight.
    TMap<FGuid, int64> WarningSeenAtMs;
    // LeadAim: the rival's last sighting (handle, epoch, foot, time; -1: none) and the velocity two sightings show.
    FGuid LeadHandle;
    uint32 LeadEpoch = 0;
    FVector LeadFootCm = FVector::ZeroVector;
    int64 LeadAtMs = -1;
    FVector RivalVelocity = FVector::ZeroVector;
    // The approach: the foot of the last progress and when it was made (-1: not approaching yet), and the side steps taken.
    FVector ApproachFoot = FVector::ZeroVector;
    int64 ApproachSinceMs = -1;
    int32 Sidesteps = 0;
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
    // The Kite stance's body given the production engagement's choice Engaged: Engaged when it is a non-dash activation,
    // otherwise a ground click away from Rival on the Site's surface while closer than the standoff (within the leash),
    // else no advance. Site is the meeting place (nullptr: no ground click). With State, a positive ReactionMs holds the
    // first step away until the rival has stood inside the standoff that long (State.CloseSinceMs).
    LOCKDOWN_API FLDOpponentBodyIntent Kite(const FLDOpponentDecisionFrame& Frame, const FLDEvaluationProfile& Profile,
        const FLDOpponentPolicyState& Policy, const FLDOpponentUnit& Rival, const FLDOpponentPOI* Site, const FLDOpponentBodyIntent& Engaged,
        const FLDEvaluationProfileState* State = nullptr);
    // Updates the operation gates from the authorized frame: per slot since when it is ready, since when the rival stands
    // inside the standoff; another life starts them afresh. Decide calls it on every fighting frame.
    LOCKDOWN_API void ObserveOperation(const FLDOpponentDecisionFrame& Frame, const FLDEvaluationProfile& Profile,
        FLDEvaluationProfileState& State);
    // The stance's body for the visible Rival given the production engagement's choice Engaged (Pursue: Engaged).
    LOCKDOWN_API FLDOpponentBodyIntent Stance(const FLDOpponentDecisionFrame& Frame, const FLDDemoContentSnapshot& Catalog,
        const FLDEvaluationProfile& Profile, FLDOpponentPolicyState& Policy, FLDEvaluationProfileState& State,
        const FLDOpponentUnit& Rival, const FLDOpponentBodyIntent& Engaged);
    // LeadAim: Body (a ground-point or direction activation at Rival) re-aimed at Rival's foot plus State.RivalVelocity times
    // the active's windup, ground delay and projectile flight (capped at MaxLeadMs, within the source's reach). Other
    // activations and a still rival are unchanged.
    LOCKDOWN_API void Lead(const FLDOpponentDecisionFrame& Frame, const FLDDemoContentSnapshot& Catalog,
        const FLDEvaluationProfileState& State, const FLDOpponentUnit& Rival, FLDOpponentBodyIntent& Body);
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
