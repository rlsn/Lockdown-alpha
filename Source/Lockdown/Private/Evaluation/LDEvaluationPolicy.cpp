#include "Evaluation/LDEvaluationPolicy.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Development/LDDemoContentCatalog.h"
#include "Evaluation/LDEvaluationSeeds.h"

namespace LDEvaluationPolicyDetail
{
TMap<FGuid, TSharedPtr<FLDEvaluationRun>> StagedRuns;

enum class EStepResult : uint8 { Wait, Send, Failed };
enum class EStepEffect : uint8 { Pending, Committed, Diverged };

bool SameId(const FString& A, const FString& B) { return A.Equals(B, ESearchCase::CaseSensitive); }

const TCHAR* SlotName(uint8 Slot) { return Slot == 0 ? TEXT("Q") : Slot == 1 ? TEXT("W") : Slot == 2 ? TEXT("E") : TEXT("?"); }

const FLDOpponentComponent* FindOwned(const FLDOpponentSelf& Self, const FString& Id)
{
    return Self.Components.FindByPredicate([&Id](const FLDOpponentComponent& Owned) { return SameId(Owned.DefinitionId, Id); });
}

bool IsInstalled(const FLDOpponentSelf& Self, const FString& Id)
{
    const FLDOpponentComponent* Owned = FindOwned(Self, Id);
    return Owned && Owned->bInstalled;
}

bool IsLearned(const FLDOpponentSelf& Self, const FString& Id)
{
    return Self.Actives.ContainsByPredicate([&Id](const FLDOpponentActive& Active) { return SameId(Active.DefinitionId, Id); });
}

const FLDOpponentActive* InSlot(const FLDOpponentSelf& Self, uint8 Slot)
{
    return Self.Actives.FindByPredicate([Slot](const FLDOpponentActive& Active) { return Active.Slot == Slot; });
}

bool IsBlueprint(const FLDOpponentSelf& Self, const FString& Id)
{
    return Self.Blueprints.ContainsByPredicate([&Id](const FString& Blueprint) { return SameId(Blueprint, Id); });
}

ELDOpponentBuildKind IntentKind(ELDEvaluationStepKind Kind)
{
    switch (Kind)
    {
    case ELDEvaluationStepKind::OpeningBase: return ELDOpponentBuildKind::OpeningBase;
    case ELDEvaluationStepKind::AllocatePoints: return ELDOpponentBuildKind::AllocatePoints;
    case ELDEvaluationStepKind::InstallComponent: return ELDOpponentBuildKind::InstallComponent;
    case ELDEvaluationStepKind::UninstallComponent: return ELDOpponentBuildKind::UninstallComponent;
    case ELDEvaluationStepKind::LearnBlueprint: return ELDOpponentBuildKind::LearnBlueprint;
    case ELDEvaluationStepKind::ReplaceActive: return ELDOpponentBuildKind::ReplaceActive;
    case ELDEvaluationStepKind::Respec: return ELDOpponentBuildKind::Respec;
    default: return ELDOpponentBuildKind::None;
    }
}

ELDBuildCommandKind CommandKind(ELDEvaluationStepKind Kind)
{
    switch (Kind)
    {
    case ELDEvaluationStepKind::OpeningBase: return ELDBuildCommandKind::OpeningBase;
    case ELDEvaluationStepKind::AllocatePoints: return ELDBuildCommandKind::AllocatePoints;
    case ELDEvaluationStepKind::InstallComponent: return ELDBuildCommandKind::InstallComponent;
    case ELDEvaluationStepKind::UninstallComponent: return ELDBuildCommandKind::UninstallComponent;
    case ELDEvaluationStepKind::LearnBlueprint: return ELDBuildCommandKind::LearnBlueprint;
    case ELDEvaluationStepKind::ReplaceActive: return ELDBuildCommandKind::ReplaceActive;
    case ELDEvaluationStepKind::Respec: return ELDBuildCommandKind::Respec;
    default: return ELDBuildCommandKind::None;
    }
}

// Failure memory of one planned step: its own key, so a later step on the same component or slot starts afresh.
FLDOpponentRetryKey StepKey(const FLDEvaluationBuildStep& Step, int32 Index)
{
    FLDOpponentRetryKey Key;
    Key.Category = ELDOpponentFailure::Build;
    Key.Slot = uint8(IntentKind(Step.Kind));
    Key.Target = FGuid(0x45564C50u, uint32(Index + 1), 0u, 0u);
    return Key;
}

FString Describe(const FLDEvaluationBuildStep& Step, int32 Index)
{
    FString Subject = !Step.Component.IsEmpty() ? Step.Component
        : !Step.Blueprint.IsEmpty() ? FString::Printf(TEXT("%s %s"), *Step.Blueprint, SlotName(Step.Slot)) : FString();
    return FString::Printf(TEXT("build step %d (%s%s%s)"), Index + 1, LDEvaluationPolicy::StepName(Step.Kind),
        Subject.IsEmpty() ? TEXT("") : TEXT(" "), *Subject);
}

// The planned points as one count per stat in the stats' design order (the AllocatePoints command).
bool PointCounts(const FLDEvaluationBuildStep& Step, const FLDDemoContentSnapshot& Catalog, TArray<uint8>& Out, FString& Error)
{
    const int32 Stats = Catalog.Get().StatsInDesignOrder().Num();
    Out.Init(0, Stats);
    for (const FLDOpponentStatPoints& Row : Step.Points)
    {
        const int32 Index = Catalog.Get().StatDesignIndex(Row.StatId);
        if (Index == INDEX_NONE || Row.Points <= 0 || Row.Points > MAX_uint8 || Out[Index] != 0)
        {
            Error = FString::Printf(TEXT("planned points %s=%d are not one catalog stat with 1..255 points"), *Row.StatId, Row.Points);
            return false;
        }
        Out[Index] = uint8(Row.Points);
    }
    if (Step.Points.IsEmpty())
    {
        Error = TEXT("planned points name no stat");
        return false;
    }
    return true;
}

// What the own build shows of an issued step. A component that ends up somewhere else than planned diverged.
EStepEffect Effect(const FLDEvaluationBuildStep& Step, const FLDOpponentSelf& Self, const TArray<FLDOpponentStatPoints>& Baseline,
    const TArray<uint8>& Counts, FString& Detail)
{
    switch (Step.Kind)
    {
    case ELDEvaluationStepKind::OpeningBase:
    case ELDEvaluationStepKind::InstallComponent:
    {
        const FLDOpponentComponent* Owned = FindOwned(Self, Step.Component);
        if (!Owned || (Step.Kind == ELDEvaluationStepKind::InstallComponent && !Owned->bInstalled)) { return EStepEffect::Pending; }
        if (!Owned->bInstalled)
        {
            Detail = TEXT("the opening base was kept in the inventory: its install was refused");
            return EStepEffect::Diverged;
        }
        if (Owned->TargetKind != Step.TargetKind || !SameId(Owned->TargetId, Step.TargetId) || Owned->Element != Step.Element
            || !SameId(Owned->BoundActive, Step.BoundActive) || !SameId(Owned->BoundComponent, Step.BoundComponent))
        {
            Detail = FString::Printf(TEXT("installed on %u:%s bound %s element %u base %s, not as planned"), uint8(Owned->TargetKind),
                *Owned->TargetId, *Owned->BoundActive, uint8(Owned->Element), *Owned->BoundComponent);
            return EStepEffect::Diverged;
        }
        return EStepEffect::Committed;
    }
    case ELDEvaluationStepKind::UninstallComponent:
    {
        const FLDOpponentComponent* Owned = FindOwned(Self, Step.Component);
        return Owned && !Owned->bInstalled ? EStepEffect::Committed : EStepEffect::Pending;
    }
    case ELDEvaluationStepKind::AllocatePoints:
    {
        if (Self.StatPoints.Num() != Baseline.Num() || Baseline.Num() != Counts.Num()) { return EStepEffect::Pending; }
        for (int32 Index = 0; Index < Counts.Num(); ++Index)
        {
            if (!SameId(Self.StatPoints[Index].StatId, Baseline[Index].StatId)
                || Self.StatPoints[Index].Points != Baseline[Index].Points + Counts[Index])
            {
                return EStepEffect::Pending;
            }
        }
        return EStepEffect::Committed;
    }
    case ELDEvaluationStepKind::LearnBlueprint:
    case ELDEvaluationStepKind::ReplaceActive:
    {
        const FLDOpponentActive* Active = InSlot(Self, Step.Slot);
        return Active && SameId(Active->DefinitionId, Step.Blueprint) ? EStepEffect::Committed : EStepEffect::Pending;
    }
    case ELDEvaluationStepKind::Respec:
        return Self.StatPoints.ContainsByPredicate([](const FLDOpponentStatPoints& Row) { return Row.Points != 0; })
            ? EStepEffect::Pending : EStepEffect::Committed;
    default:
        Detail = TEXT("unknown step kind");
        return EStepEffect::Diverged;
    }
}

// Why the own build cannot take the step now; empty when it can. Nothing else builds for a planned side, so a missing
// subject in the step's window never appears later: the seeded supply did not give it or an earlier step did not leave it.
FString Precondition(const FLDEvaluationBuildStep& Step, const FLDOpponentSelf& Self, const TArray<uint8>& Counts)
{
    const FLDOpponentComponent* Owned = FindOwned(Self, Step.Component);
    switch (Step.Kind)
    {
    case ELDEvaluationStepKind::OpeningBase:
        if (!Self.bOpeningBasePending) { return TEXT("the opening base pick is not open"); }
        if (!Self.OpeningBaseOptions.ContainsByPredicate([&Step](const FString& Option) { return SameId(Option, Step.Component); }))
        {
            return FString::Printf(TEXT("not an option of the opening base pick (%s)"), *FString::Join(Self.OpeningBaseOptions, TEXT(",")));
        }
        break;
    case ELDEvaluationStepKind::InstallComponent:
        if (!Owned) { return TEXT("the component is not owned (the seeded supply did not grant it)"); }
        if (Owned->bInstalled) { return TEXT("the component is already installed"); }
        if (Step.TargetKind == ELDComponentTargetKind::Active && !IsLearned(Self, Step.TargetId))
        {
            return FString::Printf(TEXT("the target active %s is not learned"), *Step.TargetId);
        }
        if (Step.TargetKind == ELDComponentTargetKind::Component && !IsInstalled(Self, Step.TargetId))
        {
            return FString::Printf(TEXT("the host component %s is not installed"), *Step.TargetId);
        }
        if (!Step.BoundComponent.IsEmpty() && !IsInstalled(Self, Step.BoundComponent))
        {
            return FString::Printf(TEXT("the repeated base %s is not installed"), *Step.BoundComponent);
        }
        break;
    case ELDEvaluationStepKind::UninstallComponent:
        if (!Owned || !Owned->bInstalled) { return TEXT("the component is not installed"); }
        break;
    case ELDEvaluationStepKind::AllocatePoints:
    {
        int64 Total = 0;
        for (const uint8 Count : Counts) { Total += Count; }
        if (Self.StatPoints.Num() != Counts.Num()) { return TEXT("the own stat rows do not match the catalog stats"); }
        if (Total > Self.UnspentPoints)
        {
            return FString::Printf(TEXT("it needs %lld points, %lld are unspent"), Total, Self.UnspentPoints);
        }
        break;
    }
    case ELDEvaluationStepKind::LearnBlueprint:
    case ELDEvaluationStepKind::ReplaceActive:
    {
        if (Step.Slot > 2) { return TEXT("the slot is not Q, W or E"); }
        if (!IsBlueprint(Self, Step.Blueprint)) { return FString::Printf(TEXT("%s is not a pending blueprint"), *Step.Blueprint); }
        const FLDOpponentActive* Active = InSlot(Self, Step.Slot);
        if (Step.Kind == ELDEvaluationStepKind::LearnBlueprint && Active)
        {
            return FString::Printf(TEXT("slot %s holds %s"), SlotName(Step.Slot), *Active->DefinitionId);
        }
        if (Step.Kind == ELDEvaluationStepKind::ReplaceActive && !Active)
        {
            return FString::Printf(TEXT("slot %s is empty"), SlotName(Step.Slot));
        }
        break;
    }
    case ELDEvaluationStepKind::Respec:
        if (!Self.StatPoints.ContainsByPredicate([](const FLDOpponentStatPoints& Row) { return Row.Points > 0; }))
        {
            return TEXT("no allocated point to return");
        }
        break;
    default:
        return TEXT("unknown step kind");
    }
    if ((Step.Kind == ELDEvaluationStepKind::OpeningBase || Step.Kind == ELDEvaluationStepKind::InstallComponent)
        && !Step.BoundActive.IsEmpty() && !IsLearned(Self, Step.BoundActive))
    {
        return FString::Printf(TEXT("the bound active %s is not learned"), *Step.BoundActive);
    }
    return FString();
}

FLDOpponentBuildIntent StepIntent(const FLDEvaluationBuildStep& Step, const TArray<uint8>& Counts, const FLDOpponentRetryKey& Key)
{
    FLDOpponentBuildIntent Out;
    Out.Kind = IntentKind(Step.Kind);
    Out.Key = Key;
    switch (Step.Kind)
    {
    case ELDEvaluationStepKind::OpeningBase:
    case ELDEvaluationStepKind::InstallComponent:
        Out.Component = Step.Component;
        Out.BoundActive = Step.BoundActive;
        Out.Element = Step.Element;
        Out.TargetKind = Step.TargetKind;
        Out.TargetId = Step.TargetId;
        Out.BoundComponent = Step.BoundComponent;
        break;
    case ELDEvaluationStepKind::UninstallComponent:
        Out.Component = Step.Component;
        break;
    case ELDEvaluationStepKind::AllocatePoints:
        Out.PointCounts = Counts;
        break;
    case ELDEvaluationStepKind::LearnBlueprint:
    case ELDEvaluationStepKind::ReplaceActive:
        Out.Blueprint = Step.Blueprint;
        Out.Slot = Step.Slot;
        break;
    default:
        break;
    }
    return Out;
}

// The next planned build command: Send fills Out; Wait while the step's window, gate, an in-flight request or the retry
// back-off holds it; Failed (State names why) when the step cannot run as planned. Never another command.
EStepResult AdvanceBuild(const FLDOpponentDecisionFrame& Frame, const FLDDemoContentSnapshot& Catalog,
    const FLDEvaluationProfile& Profile, FLDOpponentPolicyState& Policy, FLDEvaluationProfileState& State,
    FLDOpponentBuildIntent& Out)
{
    const FLDOpponentSelf& Self = Frame.Self;
    const FLDOpponentPublicMatch& Public = Frame.Public;
    const auto Fail = [&State](const FString& Why)
    {
        State.Phase = ELDEvaluationPhase::Failed;
        State.Failure = Why;
        return EStepResult::Failed;
    };
    while (State.StepsCommitted < Profile.Build.Num())
    {
        if (Frame.bBuildChangeInFlight) { return EStepResult::Wait; }
        const int32 Index = State.StepsCommitted;
        const FLDEvaluationBuildStep& Step = Profile.Build[Index];
        const FString What = LDEvaluationPolicyDetail::Describe(Step, Index);
        TArray<uint8> Counts;
        FString Error;
        if (Step.Kind == ELDEvaluationStepKind::AllocatePoints && !PointCounts(Step, Catalog, Counts, Error))
        {
            return Fail(What + TEXT(": ") + Error);
        }
        if (State.bStepIssued)
        {
            switch (Effect(Step, Self, State.StepBaseline, Counts, Error))
            {
            case EStepEffect::Committed:
                ++State.StepsCommitted;
                State.bStepIssued = false;
                State.StepBaseline.Reset();
                continue;
            case EStepEffect::Diverged:
                return Fail(What + TEXT(": ") + Error);
            default:
                break;
            }
        }
        const FLDOpponentRetryKey Key = StepKey(Step, Index);
        const uint64 Print = LDOpponentPolicy::Fingerprint(ELDOpponentFailure::Build, Self);
        if (Policy.Failures.IsSuppressed(Key, Print))
        {
            return Fail(What + TEXT(": growth refused it until the retry policy gave up"));
        }
        const bool bField = Public.Phase == ELDMatchPhase::Development || Public.Phase == ELDMatchPhase::CaptureOpen;
        const bool bOpeningRest = Public.Phase == ELDMatchPhase::Rest && Public.Timeline.Stage == ELDMatchStage::Rest
            && Public.Timeline.RestIndex == 0;
        if (Step.Window == ELDEvaluationStepWindow::OpeningRest)
        {
            const bool bPassed = bField || (Public.Phase == ELDMatchPhase::Rest && Public.Timeline.Stage == ELDMatchStage::Rest
                && Public.Timeline.RestIndex > 0);
            if (bPassed) { return Fail(What + TEXT(": the opening rest ended before it was committed")); }
            if (!bOpeningRest || Self.OffersConsumed < 1) { return EStepResult::Wait; }
        }
        else if (!bField || Self.Level < Profile.SupplyLevel || int32(Self.OffersConsumed) < Profile.Choices.Num())
        {
            return EStepResult::Wait;
        }
        LDProgression::FBuildContext Context;
        Context.Stage = Public.BuildStage();
        Context.LifePhase = Self.Phase;
        Context.bSafe = Self.BuildSafeAtMs <= Public.NowMs;
        const FLDOpponentComponent* Owned = FindOwned(Self, Step.Component);
        const ELDGrowthReason Gate = Step.Kind == ELDEvaluationStepKind::InstallComponent
            ? LDProgression::InstallGate(Context, Owned && Owned->bEverInstalled)
            : LDProgression::CommandGate(CommandKind(Step.Kind), Context);
        if (Gate != ELDGrowthReason::None)
        {
            // Spec §5: moves, uninstalls, replacements and respecs never open on the field.
            if (Context.Stage == LDProgression::EMatchStage::Live && (Gate == ELDGrowthReason::RestOnly
                || Gate == ELDGrowthReason::NotRestWindow || Gate == ELDGrowthReason::ReplacementNotAllowed))
            {
                return Fail(FString::Printf(TEXT("%s: %s on the field"), *What, LDGrowthWire::ReasonName(Gate)));
            }
            return EStepResult::Wait;
        }
        const FString Missing = Precondition(Step, Self, Counts);
        if (!Missing.IsEmpty()) { return Fail(What + TEXT(": ") + Missing); }
        if (!Policy.Failures.CanAttempt(Key, Print, Public.NowMs)) { return EStepResult::Wait; }
        if (!State.bStepIssued)
        {
            State.StepBaseline = Self.StatPoints;
            State.bStepIssued = true;
        }
        Out = StepIntent(Step, Counts, Key);
        return EStepResult::Send;
    }
    return EStepResult::Wait;
}
}

const TCHAR* LDEvaluationPolicy::StepName(ELDEvaluationStepKind Kind)
{
    switch (Kind)
    {
    case ELDEvaluationStepKind::OpeningBase: return TEXT("OpeningBase");
    case ELDEvaluationStepKind::AllocatePoints: return TEXT("AllocatePoints");
    case ELDEvaluationStepKind::InstallComponent: return TEXT("InstallComponent");
    case ELDEvaluationStepKind::UninstallComponent: return TEXT("UninstallComponent");
    case ELDEvaluationStepKind::LearnBlueprint: return TEXT("LearnBlueprint");
    case ELDEvaluationStepKind::ReplaceActive: return TEXT("ReplaceActive");
    case ELDEvaluationStepKind::Respec: return TEXT("Respec");
    default: return TEXT("Unknown");
    }
}

bool LDEvaluationPolicy::Choose(const FLDOpponentSelf& Self, const FLDDemoContentSnapshot& Catalog, const FLDEvaluationChoice& Rule,
    FString& OutOption, FString& Error)
{
    OutOption.Reset();
    if (!Self.Offer.IsSet() || Self.Offer->Options.IsEmpty())
    {
        Error = TEXT("No current offer.");
        return false;
    }
    const TArray<FString>& Options = Self.Offer->Options;
    switch (Rule.Rule)
    {
    case ELDEvaluationChoiceRule::AISelect:
        return LDOpponentPolicy::SelectActive(Self, Catalog, OutOption, Error);
    case ELDEvaluationChoiceRule::OptionIndex:
        if (!Options.IsValidIndex(Rule.Index))
        {
            Error = FString::Printf(TEXT("the actual offer (%s) has no option %d"), *FString::Join(Options, TEXT(",")), Rule.Index);
            return false;
        }
        OutOption = Options[Rule.Index];
        return true;
    case ELDEvaluationChoiceRule::Definition:
        if (!Options.ContainsByPredicate([&Rule](const FString& Option) { return Option.Equals(Rule.DefinitionId, ESearchCase::CaseSensitive); }))
        {
            Error = FString::Printf(TEXT("planned active %s is not in the actual offer (%s)"), *Rule.DefinitionId,
                *FString::Join(Options, TEXT(",")));
            return false;
        }
        OutOption = Rule.DefinitionId;
        return true;
    default:
        Error = TEXT("unknown choice rule");
        return false;
    }
}

FLDOpponentDecision LDEvaluationPolicy::Decide(const FLDOpponentDecisionFrame& Frame, const FLDDemoContentSnapshot& Catalog,
    const FLDEvaluationProfile& Profile, FLDOpponentPolicyState& Policy, FLDEvaluationProfileState& State)
{
    using EBody = ELDOpponentBodyKind;
    const FLDOpponentSelf& Self = Frame.Self;
    if (Policy.Generation != Frame.Generation)
    {
        Policy = FLDOpponentPolicyState();
        Policy.Generation = Frame.Generation;
    }
    if (Policy.Life != Self.Life)
    {
        Policy.Life = Self.Life;
        Policy.TargetHandle.Invalidate();
        Policy.TargetEpoch = 0;
        Policy.TargetEngagedAtMs = 0;
    }
    FLDOpponentDecision Out;
    const auto Finish = [&Out, &Policy](ELDOpponentMode Mode, ELDOpponentReason Reason)
    {
        Out.Mode = Mode;
        Out.Reason = Reason;
        Policy.Mode = Mode;
        return Out;
    };
    if (State.Phase == ELDEvaluationPhase::Failed) { return Finish(ELDOpponentMode::Stopped, ELDOpponentReason::Stopped); }
    const bool bRunning = Frame.Public.Phase == ELDMatchPhase::Development || Frame.Public.Phase == ELDMatchPhase::CaptureOpen;
    // #198: a rest keeps the body in its private rest space; builds go only while the rest build window is open.
    const bool bRest = Frame.Public.Phase == ELDMatchPhase::Rest;
    if (!Frame.Generation || !(bRunning || bRest) || !Self.ContenderId.IsValid()
        || (Self.Phase != ELDLifePhase::Alive && Self.Phase != ELDLifePhase::Dead))
    {
        return Finish(ELDOpponentMode::Stopped, ELDOpponentReason::Stopped);
    }
    const int32 Planned = Profile.Choices.Num();
    const bool bBuildOpen = !bRest || Frame.Public.BuildStage() == LDProgression::EMatchStage::Rest;
    if (bBuildOpen && Self.Offer.IsSet() && !Frame.bBuildChangeInFlight && int32(Self.Offer->Ordinal) <= Planned)
    {
        const int32 Ordinal = Self.Offer->Ordinal;
        FLDOpponentRetryKey Key;
        Key.Category = ELDOpponentFailure::Build;
        Key.Slot = uint8(ELDOpponentBuildKind::ChooseActive);
        Key.Target = Self.Offer->OfferId;
        if (Ordinal >= 1 && Policy.Failures.CanAttempt(Key, LDOpponentPolicy::Fingerprint(ELDOpponentFailure::Build, Self), Frame.Public.NowMs))
        {
            FString Option;
            FString Error;
            if (!Choose(Self, Catalog, Profile.Choices[Ordinal - 1], Option, Error))
            {
                State.Phase = ELDEvaluationPhase::Failed;
                State.Failure = FString::Printf(TEXT("ordinal %d: %s"), Ordinal, *Error);
                return Finish(ELDOpponentMode::Stopped, ELDOpponentReason::Stopped);
            }
            Out.Build.Kind = ELDOpponentBuildKind::ChooseActive;
            Out.Build.OfferId = Self.Offer->OfferId;
            Out.Build.OfferRevision = Self.Offer->OfferRevision;
            Out.Build.Option = Option;
            Out.Build.Key = Key;
        }
    }
    if (Out.Build.Kind == ELDOpponentBuildKind::None)
    {
        // A planned build owns every build command but the active choices; only an unplanned profile takes the production
        // follow-up rule, whatever kinds that rule decides.
        if (Profile.Build.IsEmpty())
        {
            if (bBuildOpen) { LDOpponentPolicy::DecideFollowUpBuild(Frame, Catalog, Policy, Out.Build); }
        }
        else if (LDEvaluationPolicyDetail::AdvanceBuild(Frame, Catalog, Profile, Policy, State, Out.Build)
            == LDEvaluationPolicyDetail::EStepResult::Failed)
        {
            Out.Build = FLDOpponentBuildIntent();
            return Finish(ELDOpponentMode::Stopped, ELDOpponentReason::Stopped);
        }
    }
    const bool bBuilds = Out.Build.Kind != ELDOpponentBuildKind::None;
    // The body waits in a rest, while dead, and until every planned choice is consumed, every planned build step is
    // committed and the build steps an unplanned profile's production rule decides are settled.
    const bool bSettling = State.Phase == ELDEvaluationPhase::Waiting || State.Phase == ELDEvaluationPhase::Building;
    if (int32(Self.OffersConsumed) < Planned || State.StepsCommitted < Profile.Build.Num()
        || (bSettling && (bBuilds || Frame.bBuildChangeInFlight)))
    {
        State.Phase = ELDEvaluationPhase::Building;
        return Finish(bBuilds ? ELDOpponentMode::Choose : ELDOpponentMode::Stopped,
            bBuilds ? ELDOpponentReason::ChooseOffer : ELDOpponentReason::Stopped);
    }
    if (bRest || Self.Phase == ELDLifePhase::Dead)
    {
        return Finish(ELDOpponentMode::Stopped, Self.Phase == ELDLifePhase::Dead ? ELDOpponentReason::DeadWaiting : ELDOpponentReason::Stopped);
    }
    // While an accepted action owns the body, nothing new is sent that could cancel its windup.
    const bool bActionLive = Self.bChanneling || (Frame.Order.IsLive() && Frame.Order.Kind == ELDOpponentOrderKind::Action);
    const auto Hold = [&Out]() { Out.Body = FLDOpponentBodyIntent(); Out.Body.Kind = EBody::Keep; };
    const FLDOpponentUnit* Rival = Frame.Units.FindByPredicate([&Profile](const FLDOpponentUnit& Unit)
        { return Unit.Kind == ELDOpponentUnitKind::Contender && Unit.ContenderId == Profile.Opponent; });
    if (Rival)
    {
        State.Phase = ELDEvaluationPhase::Engaging;
        Out.Body = LDOpponentPolicy::EngageUnit(Frame, Catalog, Policy, *Rival);
        if (bActionLive) { Hold(); }
        return Finish(ELDOpponentMode::Engage, ELDOpponentReason::EngageTarget);
    }
    const FLDOpponentPOI* Site = Frame.POIs.FindByPredicate([&Profile](const FLDOpponentPOI& POI) { return POI.Id == Profile.Site; });
    if (!Site)
    {
        State.Phase = ELDEvaluationPhase::Failed;
        State.Failure = FString::Printf(TEXT("meeting place %s is not a public place of this world"), *Profile.Site.ToString());
        return Finish(ELDOpponentMode::Stopped, ELDOpponentReason::ProviderUnavailable);
    }
    State.Phase = ELDEvaluationPhase::Approaching;
    if (FVector::Dist2D(Self.FootCm, Site->FootCm) <= SiteArrivalCm)
    {
        if (Frame.Order.IsLive()) { Hold(); }
        return Finish(ELDOpponentMode::DefendOrHold, ELDOpponentReason::Explore);
    }
    Out.Body = LDOpponentPolicy::WalkTo(Frame, Catalog, Policy, Profile.Site);
    if (bActionLive) { Hold(); }
    return Finish(ELDOpponentMode::Explore, ELDOpponentReason::Explore);
}

bool LDEvaluationProfiles::Stage(const FGuid& Contender, const FLDEvaluationProfile& Profile, FString& Error)
{
    if (!LDEvaluationSeeds::IsSwitchPresent())
    {
        Error = TEXT("Evaluation profiles need a process started with -LDBuildEvaluation.");
        return false;
    }
    if (!Contender.IsValid() || !Profile.Opponent.IsValid() || Profile.Opponent == Contender || Profile.Site.IsNone())
    {
        Error = TEXT("An evaluation profile names its own contender, another rival and a meeting place.");
        return false;
    }
    auto Run = MakeShared<FLDEvaluationRun>();
    Run->Profile = Profile;
    LDEvaluationPolicyDetail::StagedRuns.Add(Contender, Run);
    return true;
}

TSharedPtr<FLDEvaluationRun> LDEvaluationProfiles::Find(const FGuid& Contender)
{
    return LDEvaluationSeeds::IsSwitchPresent() ? LDEvaluationPolicyDetail::StagedRuns.FindRef(Contender) : nullptr;
}

void LDEvaluationProfiles::Clear()
{
    LDEvaluationPolicyDetail::StagedRuns.Reset();
}
#endif
