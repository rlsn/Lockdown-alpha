#include "Evaluation/LDEvaluationPolicy.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Evaluation/LDEvaluationSeeds.h"

namespace LDEvaluationPolicyDetail
{
TMap<FGuid, TSharedPtr<FLDEvaluationRun>> StagedRuns;

FString OfferText(const TArray<FLDOpponentOption>& Options)
{
    TArray<FString> Ids;
    for (const FLDOpponentOption& Option : Options) { Ids.Add(Option.DefinitionId); }
    return FString::Join(Ids, TEXT(","));
}
}

bool LDEvaluationPolicy::Choose(const FLDOpponentSelf& Self, const FLDDemoContentSnapshot& Catalog, const FLDEvaluationChoice& Rule,
    FLDOpponentChoicePick& Out, FString& Error)
{
    using namespace LDEvaluationPolicyDetail;
    Out = {};
    if (!Self.Offer.IsSet() || Self.Offer->Options.IsEmpty())
    {
        Error = TEXT("No current offer.");
        return false;
    }
    const TArray<FLDOpponentOption>& Options = Self.Offer->Options;
    const FLDOpponentOption* Option = nullptr;
    switch (Rule.Rule)
    {
    case ELDEvaluationChoiceRule::AISelect:
        return LDOpponentPolicy::SelectChoice(Self, Catalog, Out, Error);
    case ELDEvaluationChoiceRule::OptionIndex:
        if (!Options.IsValidIndex(Rule.Index))
        {
            Error = FString::Printf(TEXT("the actual offer (%s) has no option %d"), *OfferText(Options), Rule.Index);
            return false;
        }
        Option = &Options[Rule.Index];
        break;
    case ELDEvaluationChoiceRule::Definition:
        Option = Options.FindByPredicate([&Rule](const FLDOpponentOption& Row)
            { return Row.DefinitionId.Equals(Rule.DefinitionId, ESearchCase::CaseSensitive); });
        if (!Option)
        {
            Error = FString::Printf(TEXT("planned card %s is not in the actual offer (%s)"), *Rule.DefinitionId, *OfferText(Options));
            return false;
        }
        break;
    default:
        Error = TEXT("unknown choice rule");
        return false;
    }
    Out.OptionId = Option->OptionId;
    if (Option->Kind != ELDOpponentOptionKind::LearnActive) { return true; }
    // A new active takes a free Q/W/E; with all three taken only the planned replacement is confirmed.
    const FLDOpponentSkill* Held[3] = {nullptr, nullptr, nullptr};
    for (const FLDOpponentSkill& Skill : Self.Skills)
    {
        if (Skill.Slot != ELDOpponentSkillSlot::Passive) { Held[uint8(Skill.Slot)] = &Skill; }
    }
    if (!Held[0] || !Held[1] || !Held[2]) { return true; }
    if (!Rule.Replace.IsSet() || Rule.Replace.GetValue() == ELDOpponentSkillSlot::Passive)
    {
        Error = FString::Printf(TEXT("new active %s needs a planned Q/W/E replacement"), *Option->DefinitionId);
        return false;
    }
    const FLDOpponentSkill& Old = *Held[uint8(Rule.Replace.GetValue())];
    Out.bReplace = true;
    Out.ReplaceSlot = Rule.Replace.GetValue();
    Out.ReplacedSkillId = Old.DefinitionId;
    Out.ReplacedRank = Old.Rank;
    return true;
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
    const int32 Ordinal = int32(Self.ChoicesConsumed) + 1;
    if (Self.Offer.IsSet() && !Frame.bBuildChangeInFlight && Ordinal <= Planned)
    {
        FLDOpponentRetryKey Key;
        Key.Category = ELDOpponentFailure::Choice;
        Key.Target = Self.Offer->OfferId;
        if (Policy.Failures.CanAttempt(Key, LDOpponentPolicy::Fingerprint(ELDOpponentFailure::Choice, Self), Frame.Public.NowMs))
        {
            FLDOpponentChoicePick Pick;
            FString Error;
            if (!Choose(Self, Catalog, Profile.Choices[Ordinal - 1], Pick, Error))
            {
                State.Phase = ELDEvaluationPhase::Failed;
                State.Failure = FString::Printf(TEXT("ordinal %d: %s"), Ordinal, *Error);
                return Finish(ELDOpponentMode::Stopped, ELDOpponentReason::Stopped);
            }
            Out.Build.Kind = ELDOpponentBuildKind::Choose;
            Out.Build.OfferId = Self.Offer->OfferId;
            Out.Build.OfferRevision = Self.Offer->OfferRevision;
            Out.Build.Choice = Pick;
            Out.Build.Key = Key;
        }
    }
    const bool bChose = Out.Build.Kind == ELDOpponentBuildKind::Choose;
    // The body waits in the countdown, while dead and until every planned choice is consumed.
    if (int32(Self.ChoicesConsumed) < Planned)
    {
        State.Phase = ELDEvaluationPhase::Building;
        return Finish(bChose ? ELDOpponentMode::Choose : ELDOpponentMode::Stopped,
            bChose ? ELDOpponentReason::ChooseOffer : ELDOpponentReason::Stopped);
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
        if (bActionLive || (Self.bCowering && (Out.Body.Kind == EBody::AttackUnit || Out.Body.Kind == EBody::Activate))) { Hold(); }
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
