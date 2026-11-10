#include "Evaluation/LDEvaluationPolicy.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Evaluation/LDEvaluationSeeds.h"

namespace LDEvaluationPolicyDetail
{
TMap<FGuid, TSharedPtr<FLDEvaluationRun>> StagedRuns;
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
    const bool bCountdown = Frame.Public.Phase == ELDMatchPhase::Countdown;
    if (!Frame.Generation || !(bRunning || bCountdown) || !Self.ContenderId.IsValid()
        || (Self.Phase != ELDLifePhase::Alive && Self.Phase != ELDLifePhase::Dead))
    {
        return Finish(ELDOpponentMode::Stopped, ELDOpponentReason::Stopped);
    }
    const int32 Planned = Profile.Choices.Num();
    if (Self.Offer.IsSet() && !Frame.bBuildChangeInFlight && int32(Self.Offer->Ordinal) <= Planned)
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
    if (Out.Build.Kind == ELDOpponentBuildKind::None) { LDOpponentPolicy::DecideLearnOrAllocate(Frame, Catalog, Policy, Out.Build); }
    const bool bBuilds = Out.Build.Kind != ELDOpponentBuildKind::None;
    // The body waits in the countdown, while dead, and until every planned choice is consumed and the build steps it
    // unlocked (learning blueprints, allocating points) are settled.
    const bool bSettling = State.Phase == ELDEvaluationPhase::Waiting || State.Phase == ELDEvaluationPhase::Building;
    if (int32(Self.OffersConsumed) < Planned || (bSettling && (bBuilds || Frame.bBuildChangeInFlight)))
    {
        State.Phase = ELDEvaluationPhase::Building;
        return Finish(bBuilds ? ELDOpponentMode::Choose : ELDOpponentMode::Stopped,
            bBuilds ? ELDOpponentReason::ChooseOffer : ELDOpponentReason::Stopped);
    }
    if (bCountdown || Self.Phase == ELDLifePhase::Dead)
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
