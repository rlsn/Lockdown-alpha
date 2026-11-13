#include "Evaluation/LDEvaluationPolicy.h"
#include "AI/LDOpponentRequests.h"
#include "Navigation/LDOrderTypes.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Combat/LDAttributeProjection.h"
#include "Development/LDDemoContentCatalog.h"
#include "Evaluation/LDEvaluationSeeds.h"
#include "Misc/AutomationTest.h"
#include "Tests/LDBuildEvaluationProbe.h"
#include "Tests/LDTestApprovedFixture.h"

// DEV build evaluation (docs/systems/BUILD_EVALUATION.md): seed streams and their production default, the fixed duel
// strategy and the strict native input. The end-to-end Cases run in RunBuildEvaluation.ps1, not here.

namespace LDBuildEvaluationTests
{
// Golden values of derivation version 1 (see RunSeeds).
constexpr uint64 GoldenStreamRoot0 = 5665713329989635553ull;
constexpr int32 GoldenSpawnSeed = -2082328359;

FLDOpponentActive Active(const TCHAR* Definition, uint8 Slot)
{
    FLDOpponentActive Out;
    Out.DefinitionId = Definition;
    Out.Slot = Slot;
    return Out;
}

FLDOpponentOffer Offer(TArray<FString> Options, uint8 Ordinal = 1)
{
    FLDOpponentOffer Out;
    Out.OfferId = FGuid(0xEB, 0, 0, 1);
    Out.OfferRevision = 3;
    Out.Ordinal = Ordinal;
    Out.Options = MoveTemp(Options);
    return Out;
}

FLDOpponentPOI Place(const TCHAR* Id, ELDOpponentPOIKind Kind, double X, double Y)
{
    FLDOpponentPOI Out;
    Out.Id = Id;
    Out.Kind = Kind;
    Out.SurfaceId = TEXT("ground");
    Out.FootCm = FVector(X, Y, 0);
    return Out;
}

FLDOpponentUnit Rival(uint32 Handle, const FGuid& Contender, double X, double Y)
{
    FLDOpponentUnit Out;
    Out.Handle = FGuid(0xEC, 0, 0, Handle);
    Out.Epoch = 1;
    Out.Kind = ELDOpponentUnitKind::Contender;
    Out.ContenderId = Contender;
    Out.FootCm = FVector(X, Y, 0);
    Out.HpP2 = 60000;
    Out.MaxHpP2 = 62000;
    return Out;
}

// A level-1 living body at the origin in Development, with the public places the production context needs.
bool Frame(FAutomationTestBase& Test, const FLDDemoContentSnapshot& Catalog, FLDOpponentDecisionFrame& Out)
{
    Out = {};
    Out.Generation = 9;
    Out.ViewRevision = 1;
    FLDOpponentSelf& Self = Out.Self;
    Self.ContenderId = FGuid(0xED, 0, 0, 1);
    Self.Phase = ELDLifePhase::Alive;
    Self.Life = 1;
    const auto Projected = LDAttributeProjection::Project(FLDAttributeBuildValues(1, {}), Catalog);
    if (!Test.TestTrue(TEXT("Level 1 bare projection succeeds"), Projected.IsSuccess())) { return false; }
    Self.Attributes = Projected.Value->Final;
    Self.HpP2 = Self.MaxHpP2 = Self.Attributes.MaxHPP2;
    Self.MpP2 = Self.MaxMpP2 = Self.Attributes.MaxMPP2;
    Self.BuildRevision = 1;
    Out.Public.Phase = ELDMatchPhase::Development;
    Out.Public.NowMs = 20000;
    Out.POIs = {Place(TEXT("capture"), ELDOpponentPOIKind::Capture, 3000, 0), Place(TEXT("s00"), ELDOpponentPOIKind::Spawn, -4000, 0)};
    return true;
}

bool RunSeeds(FAutomationTestBase& Test)
{
    using namespace LDEvaluationSeeds;
    // The rules run carries no -LDBuildEvaluation: every hook keeps its production entropy.
    bool bGood = Test.TestFalse(TEXT("The rules run carries no -LDBuildEvaluation switch"), IsSwitchPresent());
    FString Error;
    bGood &= Test.TestTrue(TEXT("Arming without the switch is refused and names it"),
        !Arm(7, Error) && Error.Contains(TEXT("-LDBuildEvaluation")) && Armed() == nullptr);
    const FGuid First = NextSessionId(), Second = NextSessionId();
    bGood &= Test.TestTrue(TEXT("Without the switch session identities stay fresh GUIDs"), First.IsValid() && Second.IsValid() && First != Second);
    FLDEvaluationProfile Profile;
    Profile.Opponent = FGuid(1, 2, 3, 4);
    Profile.Site = TEXT("capture");
    bGood &= Test.TestTrue(TEXT("Profiles are refused without the switch and none is found"),
        !LDEvaluationProfiles::Stage(FGuid(5, 6, 7, 8), Profile, Error) && !LDEvaluationProfiles::Find(FGuid(5, 6, 7, 8)).IsValid());

    const FLDEvaluationSeeds A = Derive(20261109), Again = Derive(20261109), B = Derive(20261110);
    bGood &= Test.TestTrue(TEXT("One root derives the same streams every time"), A.MatchSecret == Again.MatchSecret
        && A.SpawnSeed == Again.SpawnSeed && A.CritSeed == Again.CritSeed && A.AirdropSeed == Again.AirdropSeed
        && A.AIContenders[0] == Again.AIContenders[0] && A.AIContenders[1] == Again.AIContenders[1] && A.AIContenders[2] == Again.AIContenders[2]);
    bGood &= Test.TestTrue(TEXT("Another root changes every stream"), A.MatchSecret != B.MatchSecret && A.SpawnSeed != B.SpawnSeed
        && A.CritSeed != B.CritSeed && A.AirdropSeed != B.AirdropSeed && A.AIContenders[0] != B.AIContenders[0]);
    bGood &= Test.TestTrue(TEXT("Streams of one root are independent and every identity is valid and distinct"),
        A.MatchSecret.IsValid() && A.AIContenders[0].IsValid() && A.AIContenders[1].IsValid() && A.AIContenders[2].IsValid()
        && A.AIContenders[0] != A.AIContenders[1] && A.AIContenders[1] != A.AIContenders[2] && A.AIContenders[0] != A.MatchSecret
        && A.SpawnSeed != A.CritSeed && A.AirdropSeed != 0);
    bGood &= Test.TestTrue(TEXT("A stream depends on its name and its root"),
        Stream(1, TEXT("a")) != Stream(1, TEXT("b")) && Stream(1, TEXT("a")) != Stream(2, TEXT("a")));
    // Golden values: a change of the derivation must bump DerivationVersion, because recorded Case seeds would replay
    // other offers, rolls and identities.
    bGood &= Test.TestEqual(TEXT("Derivation version"), DerivationVersion, uint32(1));
    bGood &= Test.TestEqual(TEXT("Golden stream of root 0"), Stream(0, TEXT("growth.match_secret.hi")), uint64(GoldenStreamRoot0));
    bGood &= Test.TestEqual(TEXT("Golden spawn seed of root 20261109"), A.SpawnSeed, int32(GoldenSpawnSeed));
    return bGood;
}

bool Pick(FAutomationTestBase& Test, const FString& Label, const FLDDemoContentSnapshot& Catalog, const FLDOpponentSelf& Self,
    const FLDEvaluationChoice& Rule, bool bAccepted, const TCHAR* Expected = TEXT(""), const TCHAR* ErrorPart = TEXT(""))
{
    FString Out;
    FString Error;
    const bool bChosen = LDEvaluationPolicy::Choose(Self, Catalog, Rule, Out, Error);
    if (!bAccepted)
    {
        return Test.TestTrue(Label + TEXT(": ") + Error, !bChosen && Error.Contains(ErrorPart));
    }
    return Test.TestTrue(Label + TEXT(": ") + Error, bChosen && Out == Expected);
}

bool RunPolicy(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDTestApprovedFixture::Read(Test, Catalog)) { return false; }
    const FLDDemoContentSnapshot& C = *Catalog;
    using ERule = ELDEvaluationChoiceRule;
    FLDOpponentSelf Self;
    Self.Offer = Offer({TEXT("active_elbow"), TEXT("active_seethe"), TEXT("active_not_mad")});
    FLDEvaluationChoice Rule;
    Rule.Rule = ERule::OptionIndex;
    Rule.Index = 1;
    bool bGood = Pick(Test, TEXT("option_index takes that option of the actual offer"), C, Self, Rule, true, TEXT("active_seethe"));
    Rule.Index = 2;
    bGood &= Pick(Test, TEXT("option_index 2 is the third option"), C, Self, Rule, true, TEXT("active_not_mad"));
    Rule = {};
    Rule.Rule = ERule::Definition;
    Rule.DefinitionId = TEXT("active_seethe");
    bGood &= Pick(Test, TEXT("definition takes the offered active"), C, Self, Rule, true, TEXT("active_seethe"));
    Rule.DefinitionId = TEXT("active_pants_down");
    bGood &= Pick(Test, TEXT("A planned active that is not offered is refused, never replaced"), C, Self, Rule, false, TEXT(""),
        TEXT("is not in the actual offer"));
    FLDOpponentSelf Short = Self;
    Short.Offer->Options.SetNum(2);
    Rule = {};
    Rule.Rule = ERule::OptionIndex;
    Rule.Index = 2;
    bGood &= Pick(Test, TEXT("An index past the actual offer is refused"), C, Short, Rule, false, TEXT(""), TEXT("has no option 2"));
    FLDOpponentSelf Offerless;
    Rule = {};
    bGood &= Pick(Test, TEXT("Without an offer nothing is chosen"), C, Offerless, Rule, false, TEXT(""), TEXT("offer"));
    FString Production, Evaluated, Error;
    bGood &= Test.TestTrue(TEXT("ai_select is the production choice"), LDOpponentPolicy::SelectActive(Self, C, Production, Error)
        && LDEvaluationPolicy::Choose(Self, C, Rule, Evaluated, Error) && Production == Evaluated);

    // ---- The fixed duel strategy over one authorized frame ----
    FLDOpponentDecisionFrame Base;
    if (!Frame(Test, C, Base)) { return false; }
    FLDEvaluationProfile Profile;
    Profile.Opponent = FGuid(0xED, 0, 0, 2);
    Profile.Site = TEXT("capture");
    Profile.Choices = {FLDEvaluationChoice()};
    const auto Decide = [&C](const FLDOpponentDecisionFrame& At, const FLDEvaluationProfile& Plan, FLDEvaluationProfileState& State)
    {
        FLDOpponentPolicyState Policy;
        return LDEvaluationPolicy::Decide(At, C, Plan, Policy, State);
    };
    // The opening rest R0: the planned opening active is chosen at once (asking to learn it), the body does not move.
    auto Countdown = Base;
    Countdown.Public.Phase = ELDMatchPhase::Rest;
    Countdown.Public.Timeline.Stage = ELDMatchStage::Rest;
    Countdown.Public.Timeline.Step = ELDRestStep::Build;
    Countdown.Self.Offer = Offer({TEXT("active_seethe"), TEXT("active_snark_bolt"), TEXT("active_bail_out")});
    FLDEvaluationProfileState State;
    FLDOpponentDecision Decision = Decide(Countdown, Profile, State);
    bGood &= Test.TestTrue(TEXT("Countdown: the planned opening (the production preference) and no body intent"),
        Decision.Build.Kind == ELDOpponentBuildKind::ChooseActive && Decision.Build.Option == TEXT("active_snark_bolt")
        && Decision.Build.OfferId == FGuid(0xEB, 0, 0, 1) && Decision.Body.Kind == ELDOpponentBodyKind::None
        && State.Phase == ELDEvaluationPhase::Building && !State.bOpeningReady);
    auto InFlight = Countdown;
    InFlight.bBuildChangeInFlight = true;
    State = {};
    bGood &= Test.TestTrue(TEXT("No second choice while one is in flight"),
        Decide(InFlight, Profile, State).Build.Kind == ELDOpponentBuildKind::None && !State.bOpeningReady);
    auto OpeningDone = Countdown;
    OpeningDone.Self.Offer.Reset();
    OpeningDone.Self.OffersConsumed = 1;
    State = {};
    Decide(OpeningDone, Profile, State);
    bGood &= Test.TestTrue(TEXT("The opening is ready once the unplanned production build is settled"), State.bOpeningReady);
    FLDEvaluationProfile FieldPlan = Profile;
    FieldPlan.Choices.Add(FLDEvaluationChoice());
    FLDEvaluationBuildStep FieldStep;
    FieldStep.Kind = ELDEvaluationStepKind::AllocatePoints;
    FieldStep.Window = ELDEvaluationStepWindow::Field;
    FieldStep.Points = {{TEXT("stat_strength"), 3}};
    FieldPlan.Build = {FieldStep};
    OpeningDone.Self.UnspentPoints = 3;
    State = {};
    Decide(OpeningDone, FieldPlan, State);
    bGood &= Test.TestTrue(TEXT("A planned field step and reserved opening points do not hold R0"),
        State.bOpeningReady && State.StepsCommitted == 0);
    OpeningDone.bBuildChangeInFlight = true;
    State = {};
    Decide(OpeningDone, FieldPlan, State);
    bGood &= Test.TestFalse(TEXT("An in-flight build never permits early ready"), State.bOpeningReady);
    // Until every planned choice is consumed the body holds, even with the rival in sight.
    FLDEvaluationProfile Two = Profile;
    Two.Choices.Add(FLDEvaluationChoice());
    auto Building = Base;
    Building.Self.OffersConsumed = 1;
    Building.Units = {Rival(1, Profile.Opponent, 300, 0)};
    State = {};
    Decision = Decide(Building, Two, State);
    bGood &= Test.TestTrue(TEXT("Building: no body intent before the planned choices are consumed"),
        Decision.Body.Kind == ELDOpponentBodyKind::None && State.Phase == ELDEvaluationPhase::Building);
    // Plan done and the rival in sight: the production engagement of the rival, not of a nearer other contender.
    auto Engage = Base;
    Engage.Self.OffersConsumed = 1;
    Engage.Units = {Rival(7, FGuid(0xED, 0, 0, 3), 100, 0), Rival(8, Profile.Opponent, 150, 0)};
    State = {};
    Decision = Decide(Engage, Profile, State);
    bGood &= Test.TestTrue(FString::Printf(TEXT("Engaging: only the designated rival is attacked (body %u)"), uint8(Decision.Body.Kind)),
        Decision.Body.Kind == ELDOpponentBodyKind::AttackUnit && Decision.Body.Handle == FGuid(0xEC, 0, 0, 8)
        && Decision.Mode == ELDOpponentMode::Engage && State.Phase == ELDEvaluationPhase::Engaging);
    // Unspent points are settled by the production allocation before the first fight.
    auto Points = Engage;
    Points.Self.UnspentPoints = 3;
    for (const FLDDemoStatDefinition* Stat : C.Get().StatsInDesignOrder())
    {
        FLDOpponentStatPoints Row;
        Row.StatId = Stat->Id;
        Points.Self.StatPoints.Add(Row);
    }
    State = {};
    Decision = Decide(Points, Profile, State);
    bGood &= Test.TestTrue(TEXT("Building: the unspent points are allocated before the body moves"),
        Decision.Build.Kind == ELDOpponentBuildKind::AllocatePoints && Decision.Body.Kind == ELDOpponentBodyKind::None
        && State.Phase == ELDEvaluationPhase::Building);
    // A blueprint with Q/W/E full waits for the rest-only replacement and never holds the duel.
    auto Full = Engage;
    Full.Self.Actives = {Active(TEXT("active_not_mad"), 0), Active(TEXT("active_snark_bolt"), 1), Active(TEXT("active_elbow"), 2)};
    Full.Self.Blueprints = {TEXT("active_seethe")};
    State = {};
    Decision = Decide(Full, Profile, State);
    bGood &= Test.TestTrue(TEXT("A blueprint without an empty slot does not hold the duel"),
        Decision.Build.Kind == ELDOpponentBuildKind::None && State.Phase == ELDEvaluationPhase::Engaging);
    auto Low = Engage;
    Low.Self.HpP2 = Low.Self.MaxHpP2 / 10;
    State = {};
    Decision = Decide(Low, Profile, State);
    bGood &= Test.TestTrue(TEXT("A low body keeps fighting: the fixed duel strategy never retreats"),
        Decision.Body.Kind == ELDOpponentBodyKind::AttackUnit && Decision.Body.Handle == FGuid(0xEC, 0, 0, 8));
    // The rival out of sight: walk to the meeting place; at it, hold.
    auto Approach = Base;
    Approach.Self.OffersConsumed = 1;
    Approach.Units = {Rival(7, FGuid(0xED, 0, 0, 3), 100, 0)};
    State = {};
    Decision = Decide(Approach, Profile, State);
    bGood &= Test.TestTrue(TEXT("Approaching: a walk to the meeting place"), Decision.Body.Kind == ELDOpponentBodyKind::MoveGround
        && Decision.Body.POI == FName(TEXT("capture")) && State.Phase == ELDEvaluationPhase::Approaching);
    auto Arrived = Approach;
    Arrived.Self.FootCm = FVector(2900, 0, 0);
    State = {};
    Decision = Decide(Arrived, Profile, State);
    bGood &= Test.TestTrue(TEXT("At the meeting place no new walk is ordered"), Decision.Body.Kind == ELDOpponentBodyKind::None);
    // Refusals stop the strategy for good and name the reason.
    FLDEvaluationProfile Nowhere = Profile;
    Nowhere.Site = TEXT("no_such_place");
    State = {};
    Decide(Approach, Nowhere, State);
    bGood &= Test.TestTrue(TEXT("An unknown meeting place fails the plan"), State.Phase == ELDEvaluationPhase::Failed
        && State.Failure.Contains(TEXT("no_such_place")));
    Decision = Decide(Engage, Nowhere, State);
    bGood &= Test.TestTrue(TEXT("A failed plan proposes nothing more"), Decision.Body.Kind == ELDOpponentBodyKind::None
        && Decision.Build.Kind == ELDOpponentBuildKind::None && Decision.Mode == ELDOpponentMode::Stopped);
    FLDEvaluationProfile Missing = Two;
    Missing.Choices[1].Rule = ERule::Definition;
    Missing.Choices[1].DefinitionId = TEXT("active_pants_down");
    auto Offered = Building;
    Offered.Self.Offer = Offer({TEXT("active_elbow"), TEXT("active_seethe")}, 2);
    State = {};
    Decision = Decide(Offered, Missing, State);
    bGood &= Test.TestTrue(TEXT("An unoffered planned active fails the plan at its ordinal"), State.Phase == ELDEvaluationPhase::Failed
        && State.Failure.Contains(TEXT("ordinal 2")) && Decision.Build.Kind == ELDOpponentBuildKind::None);

    // ---- Kite stance: never walks in for a basic, steps away on legal ground inside the standoff ----
    FLDEvaluationProfile Kiter = Profile;
    Kiter.Stance = ELDEvaluationStance::Kite;
    Kiter.StandoffCm = 600;
    auto Close = Base;
    Close.Self.OffersConsumed = 1;
    Close.Units = {Rival(8, Profile.Opponent, 200, 0)};
    State = {};
    Decision = Decide(Close, Kiter, State);
    bGood &= Test.TestTrue(TEXT("Kite: inside the standoff it clicks ground straight away from the rival on the site surface"),
        Decision.Body.Kind == ELDOpponentBodyKind::MoveGround && Decision.Body.POI.IsNone()
        && Decision.Body.Surface == FName(TEXT("ground")) && Decision.Body.Point.Equals(FVector(-LDEvaluationPolicy::KiteStepCm, 0, 0), 1.)
        && Decision.Mode == ELDOpponentMode::Engage);
    {
        FLDSpatialOrderRequest Request;
        FString Why;
        FLDMoveIdentity Identity;
        Identity.Match = 7;
        Identity.Life = 1;
        Identity.Order = 4;
        bGood &= Test.TestTrue(TEXT("Kite: the ground click becomes a MoveGround order at that point on that surface"),
            LDOpponentRequests::BuildOrder(Decision.Body, Close.POIs, Identity, Request, Why)
            && Request.Kind == ELDSpatialOrderKind::MoveGround && FVector(Request.GroundCm).Equals(Decision.Body.Point, 1.)
            && Request.SurfaceHint == FName(TEXT("ground")));
    }
    auto Walking = Close;
    Walking.Order.Kind = ELDOpponentOrderKind::MoveGround;
    Walking.Order.Status = ELDOpponentOrderStatus::Active;
    State = {};
    bGood &= Test.TestTrue(TEXT("Kite: a live step is kept, not re-sent"), Decide(Walking, Kiter, State).Body.Kind == ELDOpponentBodyKind::Keep);
    auto Held = Close;
    Held.Units = {Rival(8, Profile.Opponent, 650, 0)};
    Held.Order.Kind = ELDOpponentOrderKind::AttackUnit;
    Held.Order.Status = ELDOpponentOrderStatus::Active;
    State = {};
    bGood &= Test.TestTrue(TEXT("Kite: at the standoff a basic-attack walk is stopped"),
        Decide(Held, Kiter, State).Body.Kind == ELDOpponentBodyKind::Stop);
    auto Far = Close;
    Far.Units = {Rival(8, Profile.Opponent, 2000, 0)};
    State = {};
    Decision = Decide(Far, Kiter, State);
    bGood &= Test.TestTrue(TEXT("Kite: far away it walks back up to the standoff, not into basic range"),
        Decision.Body.Kind == ELDOpponentBodyKind::MoveGround && Decision.Body.Point.Equals(FVector(1400, 0, 0), 1.));
    // The leash: straight away and the shallow turns lead beyond it and farther from the site, so a turn back toward it is taken.
    FLDEvaluationProfile Leashed = Kiter;
    Leashed.LeashCm = 1000;
    State = {};
    Decision = Decide(Close, Leashed, State);
    bGood &= Test.TestTrue(TEXT("Kite: a leashed retreat never steps farther from the site beyond the leash"),
        Decision.Body.Kind == ELDOpponentBodyKind::MoveGround && FVector::Dist2D(Decision.Body.Point, FVector(3000, 0, 0)) < 3000.);
    const auto Pursued = [&](const FLDOpponentDecisionFrame& At)
    {
        FLDEvaluationProfileState Fresh;
        return Decide(At, Profile, Fresh).Body;
    };
    const auto Same = [](const FLDOpponentBodyIntent& A, const FLDOpponentBodyIntent& B)
    {
        return A.Kind == B.Kind && A.Handle == B.Handle && A.POI == B.POI && A.Point.Equals(B.Point, 1.);
    };
    const auto KiteStep = [](const FLDOpponentBodyIntent& Body)
    {
        return Body.Kind == ELDOpponentBodyKind::MoveGround && Body.POI.IsNone() && Body.Point.Equals(FVector(-LDEvaluationPolicy::KiteStepCm, 0, 0), 1.);
    };

    // ---- Burst: pursues while any Q/W/E source is ready, kites while all of them recover ----
    FLDEvaluationProfile Burster = Kiter;
    Burster.Stance = ELDEvaluationStance::Burst;
    State = {};
    bGood &= Test.TestTrue(TEXT("Burst: with every active recovering it kites"), KiteStep(Decide(Close, Burster, State).Body));
    auto DashOnly = Close;
    FLDOpponentSource Dash;
    Dash.Slot = ELDOpponentSourceSlot::Dash;
    Dash.bReady = true;
    DashOnly.Self.Sources = {Dash};
    State = {};
    bGood &= Test.TestTrue(TEXT("Burst: a ready dash alone is not a burst window"), KiteStep(Decide(DashOnly, Burster, State).Body));
    auto Ready = DashOnly;
    FLDOpponentSource Q;
    Q.Slot = ELDOpponentSourceSlot::Q;
    Q.DefinitionId = TEXT("active_seethe");
    Q.bReady = true;
    Ready.Self.Sources.Add(Q);
    State = {};
    bGood &= Test.TestTrue(TEXT("Burst: a ready Q/W/E source takes the production engagement"),
        Same(Decide(Ready, Burster, State).Body, Pursued(Ready)));

    // ---- Guard: pursues until health falls below the threshold, then kites until it recovers past the hysteresis ----
    FLDEvaluationProfile Guard = Kiter;
    Guard.Stance = ELDEvaluationStance::Guard;
    Guard.ThresholdBp = 3000;
    State = {};
    bGood &= Test.TestTrue(TEXT("Guard: healthy it takes the production engagement"),
        Same(Decide(Close, Guard, State).Body, Pursued(Close)) && !State.bGuardRetreat);
    auto Hurt = Close;
    Hurt.Self.HpP2 = Hurt.Self.MaxHpP2 / 5;
    bGood &= Test.TestTrue(TEXT("Guard: below the threshold it retreats"), KiteStep(Decide(Hurt, Guard, State).Body) && State.bGuardRetreat);
    auto Mending = Close;
    Mending.Self.HpP2 = Mending.Self.MaxHpP2 * 2 / 5;
    bGood &= Test.TestTrue(TEXT("Guard: above the threshold but inside the hysteresis it keeps retreating"),
        KiteStep(Decide(Mending, Guard, State).Body) && State.bGuardRetreat);
    auto Mended = Close;
    Mended.Self.HpP2 = Mended.Self.MaxHpP2 / 2;
    bGood &= Test.TestTrue(TEXT("Guard: recovered past the hysteresis it engages again"),
        Same(Decide(Mended, Guard, State).Body, Pursued(Mended)) && !State.bGuardRetreat);

    // ---- Hold: pursues only inside the defended radius of the site; otherwise returns to it and holds ----
    FLDEvaluationProfile Holder = Kiter;
    Holder.Stance = ELDEvaluationStance::Hold;
    auto Inside = Close;
    Inside.Self.FootCm = FVector(2900, 0, 0);
    Inside.Units = {Rival(8, Profile.Opponent, 3300, 0)};
    State = {};
    bGood &= Test.TestTrue(TEXT("Hold: a rival inside the radius is pursued"), Same(Decide(Inside, Holder, State).Body, Pursued(Inside)));
    auto Outside = Close;
    Outside.Units = {Rival(8, Profile.Opponent, 400, 0)};
    State = {};
    Decision = Decide(Outside, Holder, State);
    bGood &= Test.TestTrue(TEXT("Hold: away from the site with the rival outside the radius it walks back to the site"),
        Decision.Body.Kind == ELDOpponentBodyKind::MoveGround && Decision.Body.POI == FName(TEXT("capture")));
    auto Posted = Inside;
    Posted.Self.FootCm = FVector(2950, 0, 0);
    Posted.Units = {Rival(8, Profile.Opponent, 2000, 0)};
    Posted.Order.Kind = ELDOpponentOrderKind::AttackUnit;
    Posted.Order.Status = ELDOpponentOrderStatus::Active;
    State = {};
    bGood &= Test.TestTrue(TEXT("Hold: at the site a chase of a rival outside the radius is stopped"),
        Decide(Posted, Holder, State).Body.Kind == ELDOpponentBodyKind::Stop);
    return bGood;
}

FLDOpponentComponent Owned(const TCHAR* Id, bool bInstalled, bool bEver, ELDDemoElement Element = ELDDemoElement::None,
    ELDComponentTargetKind Kind = ELDComponentTargetKind::None, const TCHAR* TargetId = TEXT(""))
{
    FLDOpponentComponent Out;
    Out.DefinitionId = Id;
    Out.bInstalled = bInstalled;
    Out.bEverInstalled = bEver;
    Out.Element = Element;
    // The tests bind B18 to the opening active.
    Out.BoundActive = bInstalled && Element != ELDDemoElement::None ? TEXT("active_hose_down") : TEXT("");
    Out.TargetKind = Kind;
    Out.TargetId = TargetId;
    return Out;
}

FLDEvaluationBuildStep Step(ELDEvaluationStepKind Kind, ELDEvaluationStepWindow Window, const TCHAR* Component = TEXT(""))
{
    FLDEvaluationBuildStep Out;
    Out.Kind = Kind;
    Out.Window = Window;
    Out.Component = Component;
    return Out;
}

// #198 workshop plan (review §4, spec §19): the planned build steps go out in order through the same build intents, only
// when growth's gate admits them and their subject is in the own build; the production follow-up rule never builds for a
// planned side; anything the plan cannot do as written fails it with the reason.
bool RunWorkshopPlan(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDTestApprovedFixture::Read(Test, Catalog)) { return false; }
    const FLDDemoContentSnapshot& C = *Catalog;
    using EKind = ELDEvaluationStepKind;
    using EWindow = ELDEvaluationStepWindow;
    using EBuild = ELDOpponentBuildKind;
    const TCHAR* Trick = TEXT("component_dirty_trick");
    const TCHAR* Kick = TEXT("component_long_kick");
    const TCHAR* Hose = TEXT("active_hose_down");
    const FGuid Opponent(0xED, 0, 0, 2);

    // The field after the supply, the opening active learned in Q and the rival in sight.
    FLDOpponentDecisionFrame Field;
    if (!Frame(Test, C, Field)) { return false; }
    for (const FLDDemoStatDefinition* Stat : C.Get().StatsInDesignOrder())
    {
        FLDOpponentStatPoints Row;
        Row.StatId = Stat->Id;
        Field.Self.StatPoints.Add(Row);
    }
    Field.Self.Actives = {Active(Hose, 0)};
    Field.Self.OffersConsumed = 1;
    Field.Units = {Rival(8, Opponent, 150, 0)};
    // The opening rest R0 build window: the opening pick open and the three opening points unspent.
    auto Rest = Field;
    Rest.Public.Phase = ELDMatchPhase::Rest;
    Rest.Public.Timeline.Stage = ELDMatchStage::Rest;
    Rest.Public.Timeline.RestIndex = 0;
    Rest.Public.Timeline.Step = ELDRestStep::Build;
    Rest.Self.UnspentPoints = 3;
    Rest.Self.bOpeningBasePending = true;
    Rest.Self.OpeningBaseOptions = {TEXT("component_fist_fire"), Trick};

    FLDEvaluationProfile Plan;
    Plan.Opponent = Opponent;
    Plan.Site = TEXT("capture");
    Plan.Choices = {FLDEvaluationChoice()};
    Plan.SupplyLevel = 3;
    FLDEvaluationBuildStep Pick = Step(EKind::OpeningBase, EWindow::OpeningRest, Trick);
    Pick.BoundActive = Hose;
    Pick.Element = ELDDemoElement::Water;
    FLDEvaluationBuildStep Points = Step(EKind::AllocatePoints, EWindow::OpeningRest);
    Points.Points = {{TEXT("stat_strength"), 2}, {TEXT("stat_physique"), 1}};
    FLDEvaluationBuildStep Rebind = Step(EKind::InstallComponent, EWindow::OpeningRest, Trick);
    Rebind.BoundActive = Hose;
    Rebind.Element = ELDDemoElement::Fire;
    FLDEvaluationBuildStep Mod = Step(EKind::InstallComponent, EWindow::Field, Kick);
    Mod.TargetKind = ELDComponentTargetKind::Active;
    Mod.TargetId = Hose;
    Plan.Build = {Pick, Points, Step(EKind::UninstallComponent, EWindow::OpeningRest, Trick), Rebind, Mod};

    FLDOpponentPolicyState Policy;
    FLDEvaluationProfileState State;
    FLDOpponentDecision Decision = LDEvaluationPolicy::Decide(Rest, C, Plan, Policy, State);
    const FLDOpponentRetryKey PickKey = Decision.Build.Key;
    bool bGood = Test.TestTrue(TEXT("R0: the planned opening base with its B18 binding, no body intent"),
        Decision.Build.Kind == EBuild::OpeningBase && Decision.Build.Component == Trick && Decision.Build.BoundActive == Hose
        && Decision.Build.Element == ELDDemoElement::Water && Decision.Build.TargetKind == ELDComponentTargetKind::None
        && Decision.Body.Kind == ELDOpponentBodyKind::None && State.Phase == ELDEvaluationPhase::Building && State.bStepIssued);
    auto Flight = Rest;
    Flight.bBuildChangeInFlight = true;
    Decision = LDEvaluationPolicy::Decide(Flight, C, Plan, Policy, State);
    bGood &= Test.TestTrue(TEXT("Nothing more while the step is in flight"), Decision.Build.Kind == EBuild::None && State.StepsCommitted == 0);
    // The pick visible: the opening points go next, exactly as planned, in the stats' design order.
    auto Picked = Rest;
    Picked.Self.BuildRevision = 2;
    Picked.Self.bOpeningBasePending = false;
    Picked.Self.Components = {Owned(Trick, true, true, ELDDemoElement::Water)};
    Decision = LDEvaluationPolicy::Decide(Picked, C, Plan, Policy, State);
    bGood &= Test.TestTrue(TEXT("The planned points per stat (physique 1, strength 2)"), State.StepsCommitted == 1
        && Decision.Build.Kind == EBuild::AllocatePoints && Decision.Build.PointCounts == TArray<uint8>({1, 0, 2, 0, 0})
        && !(Decision.Build.Key == PickKey));
    auto Spent = Picked;
    Spent.Self.BuildRevision = 3;
    Spent.Self.StatPoints[0].Points = 1;
    Spent.Self.StatPoints[2].Points = 2;
    Spent.Self.UnspentPoints = 0;
    Decision = LDEvaluationPolicy::Decide(Spent, C, Plan, Policy, State);
    bGood &= Test.TestTrue(TEXT("A rebuild in the rest window: the base comes off"), State.StepsCommitted == 2
        && Decision.Build.Kind == EBuild::UninstallComponent && Decision.Build.Component == Trick);
    auto Off = Spent;
    Off.Self.BuildRevision = 4;
    Off.Self.Components = {Owned(Trick, false, true)};
    Decision = LDEvaluationPolicy::Decide(Off, C, Plan, Policy, State);
    bGood &= Test.TestTrue(TEXT("...and goes back with the other element (a rest-window move)"), State.StepsCommitted == 3
        && Decision.Build.Kind == EBuild::InstallComponent && Decision.Build.Component == Trick && Decision.Build.BoundActive == Hose
        && Decision.Build.Element == ELDDemoElement::Fire);
    auto Rebound = Off;
    Rebound.Self.BuildRevision = 5;
    Rebound.Self.Components = {Owned(Trick, true, true, ELDDemoElement::Fire)};
    Decision = LDEvaluationPolicy::Decide(Rebound, C, Plan, Policy, State);
    bGood &= Test.TestTrue(TEXT("The field step waits through the rest"), State.StepsCommitted == 4 && Decision.Build.Kind == EBuild::None
        && Decision.Body.Kind == ELDOpponentBodyKind::None && State.Phase == ELDEvaluationPhase::Building);
    // On the field below the supply's level: the planned side still holds, and the production rule never spends its points.
    auto Early = Field;
    Early.Self.BuildRevision = 6;
    Early.Self.Level = 2;
    Early.Self.UnspentPoints = 3;
    Early.Self.StatPoints = Spent.Self.StatPoints;
    Early.Self.Components = Rebound.Self.Components;
    Decision = LDEvaluationPolicy::Decide(Early, C, Plan, Policy, State);
    bGood &= Test.TestTrue(TEXT("Below the supply level: no build of the production rule, no body intent"),
        Decision.Build.Kind == EBuild::None && Decision.Body.Kind == ELDOpponentBodyKind::None && State.Phase == ELDEvaluationPhase::Building);
    auto Drawn = Early;
    Drawn.Self.Level = 3;
    Drawn.Self.Components.Add(Owned(Kick, false, false));
    Decision = LDEvaluationPolicy::Decide(Drawn, C, Plan, Policy, State);
    bGood &= Test.TestTrue(TEXT("The drawn mod goes onto the planned active"), Decision.Build.Kind == EBuild::InstallComponent
        && Decision.Build.Component == Kick && Decision.Build.TargetKind == ELDComponentTargetKind::Active && Decision.Build.TargetId == Hose);
    auto Done = Drawn;
    Done.Self.BuildRevision = 7;
    Done.Self.Components[1] = Owned(Kick, true, true, ELDDemoElement::None, ELDComponentTargetKind::Active, Hose);
    Decision = LDEvaluationPolicy::Decide(Done, C, Plan, Policy, State);
    bGood &= Test.TestTrue(TEXT("Every step committed: the duel starts, the unplanned points stay unspent"), State.StepsCommitted == 5
        && Decision.Build.Kind == EBuild::None && State.Phase == ELDEvaluationPhase::Engaging && Decision.Mode == ELDOpponentMode::Engage);

    // ---- A plan that cannot run as written fails with its reason and never sends another command ----
    const auto Fails = [&](const FString& Label, const FLDOpponentDecisionFrame& At, const FLDEvaluationProfile& With,
        FLDEvaluationProfileState Start, const TCHAR* Reason)
    {
        FLDOpponentPolicyState Fresh;
        const FLDOpponentDecision Out = LDEvaluationPolicy::Decide(At, C, With, Fresh, Start);
        return Test.TestTrue(Label + TEXT(": ") + Start.Failure, Start.Phase == ELDEvaluationPhase::Failed
            && Start.Failure.Contains(Reason) && Out.Build.Kind == EBuild::None && Out.Body.Kind == ELDOpponentBodyKind::None);
    };
    FLDEvaluationProfileState AtMod;
    AtMod.StepsCommitted = 4;
    auto Undrawn = Early;
    Undrawn.Self.Level = 3;
    bGood &= Fails(TEXT("The seeded supply did not grant the planned mod"), Undrawn, Plan, AtMod, TEXT("not owned"));
    bGood &= Fails(TEXT("An opening-rest step still open on the field"), Early, Plan, FLDEvaluationProfileState(), TEXT("opening rest ended"));
    FLDEvaluationProfile Missing = Plan;
    Missing.Build[0].Component = TEXT("component_kick_down");
    bGood &= Fails(TEXT("A base that the opening pick does not offer"), Rest, Missing, FLDEvaluationProfileState(),
        TEXT("not an option of the opening base pick"));
    FLDEvaluationProfile Move = Plan;
    Move.SupplyLevel = 1;
    Move.Build = {Rebind};
    Move.Build[0].Window = EWindow::Field;
    auto Moved = Early;
    Moved.Self.Components = {Owned(Trick, false, true)};
    {
        // #222: a once-installed component is installed again on the field as well.
        FLDOpponentPolicyState Fresh;
        FLDEvaluationProfileState Start;
        const FLDOpponentDecision Out = LDEvaluationPolicy::Decide(Moved, C, Move, Fresh, Start);
        bGood &= Test.TestTrue(TEXT("A once-installed component is installed again on the field: ") + Start.Failure,
            Start.Phase != ELDEvaluationPhase::Failed && Out.Build.Kind == EBuild::InstallComponent);
    }
    FLDEvaluationProfileState Issued;
    Issued.bStepIssued = true;
    auto Kept = Rest;
    Kept.Self.bOpeningBasePending = false;
    Kept.Self.Components = {Owned(Trick, false, false)};
    bGood &= Fails(TEXT("An opening base kept in the inventory"), Kept, Plan, Issued, TEXT("kept in the inventory"));
    // A refused step backs off on its own key, and fails the plan once the retry policy gives up.
    FLDOpponentPolicyState Refusing;
    FLDEvaluationProfileState Refused;
    Decision = LDEvaluationPolicy::Decide(Rest, C, Plan, Refusing, Refused);
    const FLDOpponentRetryKey Key = Decision.Build.Key;
    const FLDDemoAIParameters& AI = C.Get().Parameters.AI;
    LDOpponentPolicy::RecordOutcome(Refusing, Key, ELDOpponentOutcome::Failed, Rest.Self, Rest.Public.NowMs, AI);
    Decision = LDEvaluationPolicy::Decide(Rest, C, Plan, Refusing, Refused);
    bGood &= Test.TestTrue(TEXT("A refused step waits for its retry"), Decision.Build.Kind == EBuild::None
        && Refused.Phase == ELDEvaluationPhase::Building);
    for (int64 Attempt = 1; Attempt < AI.RetryMaxAttempts; ++Attempt)
    {
        LDOpponentPolicy::RecordOutcome(Refusing, Key, ELDOpponentOutcome::Failed, Rest.Self, Rest.Public.NowMs, AI);
    }
    Decision = LDEvaluationPolicy::Decide(Rest, C, Plan, Refusing, Refused);
    bGood &= Test.TestTrue(TEXT("Growth refusing the step until the retry policy gives up fails the plan: ") + Refused.Failure,
        Refused.Phase == ELDEvaluationPhase::Failed && Refused.Failure.Contains(TEXT("build step 1 (OpeningBase"))
        && Refused.Failure.Contains(TEXT("retry policy")) && Decision.Build.Kind == EBuild::None);

    // ---- Learning, replacement and respec go out as planned when their subject is there ----
    FLDEvaluationProfile Learn = Plan;
    Learn.SupplyLevel = 1;
    FLDEvaluationBuildStep LearnStep = Step(EKind::LearnBlueprint, EWindow::Field);
    LearnStep.Blueprint = TEXT("active_seethe");
    LearnStep.Slot = 1;
    Learn.Build = {LearnStep};
    auto Pending = Field;
    Pending.Self.Blueprints = {TEXT("active_seethe")};
    FLDEvaluationProfileState Fresh;
    FLDOpponentPolicyState FreshPolicy;
    Decision = LDEvaluationPolicy::Decide(Pending, C, Learn, FreshPolicy, Fresh);
    bGood &= Test.TestTrue(TEXT("A pending blueprint into the planned empty slot"), Decision.Build.Kind == EBuild::LearnBlueprint
        && Decision.Build.Blueprint == TEXT("active_seethe") && Decision.Build.Slot == 1);
    FLDEvaluationProfile Replace = Learn;
    Replace.Build[0].Kind = EKind::ReplaceActive;
    Replace.Build[0].Window = EWindow::OpeningRest;
    Replace.Build[0].Slot = 0;
    auto RestPending = Rest;
    RestPending.Self.Blueprints = {TEXT("active_seethe")};
    for (uint8 Index = 1; Index <= 3; ++Index)
    {
        FLDOpponentPOI Service = Place(*FString::Printf(TEXT("rest_service_%d"), Index), ELDOpponentPOIKind::RestService, 6000 + 800 * Index, 700);
        Service.RestService = static_cast<ELDRestService>(Index);
        RestPending.RestServicePOIs.Add(Service);
    }
    Fresh = {};
    FreshPolicy = {};
    Decision = LDEvaluationPolicy::Decide(RestPending, C, Replace, FreshPolicy, Fresh);
    bGood &= Test.TestTrue(TEXT("Away from the trainer, a planned replacement walks there and sends nothing"),
        Decision.Build.Kind == EBuild::None && Decision.Body.Kind == ELDOpponentBodyKind::MoveGround
        && Decision.Body.POI == TEXT("rest_service_3") && Decision.Reason == ELDOpponentReason::RestService
        && Fresh.Phase == ELDEvaluationPhase::Building && !Fresh.bOpeningReady);
    RestPending.Self.NearbyRestService = ELDRestService::Trainer;
    Decision = LDEvaluationPolicy::Decide(RestPending, C, Replace, FreshPolicy, Fresh);
    bGood &= Test.TestTrue(TEXT("A replacement of the occupied slot beside the trainer"), Decision.Build.Kind == EBuild::ReplaceActive
        && Decision.Build.Blueprint == TEXT("active_seethe") && Decision.Build.Slot == 0);
    FLDEvaluationProfile Occupied = Learn;
    Occupied.Build[0].Slot = 0;
    bGood &= Fails(TEXT("A learn into an occupied slot"), Pending, Occupied, FLDEvaluationProfileState(), TEXT("slot Q holds"));
    FLDEvaluationProfile Respec = Plan;
    Respec.Build = {Step(EKind::Respec, EWindow::OpeningRest)};
    auto Allocated = Rest;
    Allocated.RestServicePOIs = RestPending.RestServicePOIs;
    Allocated.Self.NearbyRestService = ELDRestService::Trainer;
    Allocated.Self.StatPoints[2].Points = 3;
    Fresh = {};
    FreshPolicy = {};
    Decision = LDEvaluationPolicy::Decide(Allocated, C, Respec, FreshPolicy, Fresh);
    bGood &= Test.TestTrue(TEXT("A respec in the rest window"), Decision.Build.Kind == EBuild::Respec);
    bGood &= Fails(TEXT("A respec with nothing allocated"), Rest, Respec, FLDEvaluationProfileState(), TEXT("no allocated point"));
    // Allocate -> respec -> allocate: the plain allocations go anywhere; only the respec walks to the trainer first.
    FLDEvaluationBuildStep One = Step(EKind::AllocatePoints, EWindow::OpeningRest);
    One.Points = {{TEXT("stat_strength"), 3}};
    FLDEvaluationBuildStep Again = Step(EKind::AllocatePoints, EWindow::OpeningRest);
    Again.Points = {{TEXT("stat_physique"), 3}};
    FLDEvaluationProfile Cycle = Plan;
    Cycle.Build = {One, Step(EKind::Respec, EWindow::OpeningRest), Again};
    auto Away = Rest;
    Away.RestServicePOIs = RestPending.RestServicePOIs;
    Away.Self.bOpeningBasePending = false;
    Fresh = {};
    FreshPolicy = {};
    Decision = LDEvaluationPolicy::Decide(Away, C, Cycle, FreshPolicy, Fresh);
    bGood &= Test.TestTrue(TEXT("Cycle: the first allocation goes out away from any NPC"),
        Decision.Build.Kind == EBuild::AllocatePoints && Decision.Body.Kind == ELDOpponentBodyKind::None);
    auto Spent3 = Away;
    Spent3.Self.BuildRevision = 2;
    Spent3.Self.UnspentPoints = 0;
    Spent3.Self.StatPoints[2].Points = 3;
    Decision = LDEvaluationPolicy::Decide(Spent3, C, Cycle, FreshPolicy, Fresh);
    bGood &= Test.TestTrue(TEXT("Cycle: the respec walks to the trainer without sending"), Fresh.StepsCommitted == 1
        && Decision.Build.Kind == EBuild::None && Decision.Body.Kind == ELDOpponentBodyKind::MoveGround
        && Decision.Body.POI == TEXT("rest_service_3") && Fresh.Phase == ELDEvaluationPhase::Building);
    auto AtTrainer = Spent3;
    AtTrainer.Self.NearbyRestService = ELDRestService::Trainer;
    Decision = LDEvaluationPolicy::Decide(AtTrainer, C, Cycle, FreshPolicy, Fresh);
    bGood &= Test.TestTrue(TEXT("Cycle: beside the trainer the respec goes out"), Decision.Build.Kind == EBuild::Respec);
    auto Refunded = AtTrainer;
    Refunded.Self.BuildRevision = 3;
    Refunded.Self.UnspentPoints = 3;
    Refunded.Self.StatPoints[2].Points = 0;
    Refunded.Self.NearbyRestService = ELDRestService::None;
    Decision = LDEvaluationPolicy::Decide(Refunded, C, Cycle, FreshPolicy, Fresh);
    bGood &= Test.TestTrue(TEXT("Cycle: the second allocation goes out wherever the body stands"), Fresh.StepsCommitted == 2
        && Decision.Build.Kind == EBuild::AllocatePoints && Decision.Build.PointCounts == TArray<uint8>({3, 0, 0, 0, 0}));
    return bGood;
}

FString Input(const FString& Cases, const FString& Top = FString())
{
    return FString::Printf(TEXT("{\"schema\":\"lockdown.build_evaluation.native_input\",\"schema_version\":5,\"run_id\":\"run-1\",")
        TEXT("\"content_root\":\"%s\"%s,\"cases\":[%s]}"), *FString::ChrN(64, TEXT('a')), *Top, *Cases);
}

FString CaseJson(const FString& Subject, const FString& Opponent, const FString& Seed = TEXT("\"18446744073709551615\""),
    const FString& Extra = FString())
{
    return FString::Printf(TEXT("{\"case_id\":\"c1\",\"seed\":%s,\"site\":\"capture\",\"duel_timeout_ms\":40000,\"wall_timeout_s\":120%s,")
        TEXT("\"contenders\":[%s,%s]}"), *Seed, *Extra, *Subject, *Opponent);
}

FString Side(const TCHAR* Role, int32 Slot, const FString& Choices = TEXT("[{\"rule\":\"ai_select\"}]"),
    const FString& Presets = TEXT("[]"), const FString& Build = TEXT("[]"), const FString& Opening = TEXT("[]"),
    const FString& Components = TEXT("[]"))
{
    return FString::Printf(TEXT("{\"role\":\"%s\",\"slot\":%d,\"supply_xp\":440,\"choices\":%s,\"opening_preset\":%s,")
        TEXT("\"presets\":%s,\"level_components\":%s,\"build\":%s}"), Role, Slot, *Choices, *Opening, *Presets, *Components, *Build);
}

bool RunInput(FAutomationTestBase& Test)
{
    using namespace LDBuildEvaluation;
    const FString Subject = Side(TEXT("subject"), 0, TEXT("[{\"rule\":\"ai_select\"},{\"rule\":\"option_index\",\"index\":2},")
        TEXT("{\"rule\":\"definition\",\"definition\":\"active_elbow\"}]"),
        TEXT("[[\"active_elbow\",\"active_seethe\",\"active_not_mad\"]]"), TEXT("[]"), TEXT("[\"active_snark_bolt\"]"),
        TEXT("[\"component_punch_heal\",\"component_breath_strike\"]"));
    const FString Opponent = Side(TEXT("opponent"), 1);
    FLDEvalRunInput Parsed;
    FString Error;
    bool bGood = Test.TestTrue(TEXT("A complete input parses: ") + Error, ParseInput(Input(CaseJson(Subject, Opponent)), Parsed, Error));
    if (bGood && Parsed.Cases.Num() == 1)
    {
        const FLDEvalCase& Case = Parsed.Cases[0];
        bGood &= Test.TestTrue(TEXT("Every field is kept exactly, the seed as an exact uint64"), Case.CaseId == TEXT("c1")
            && Case.Seed == MAX_uint64 && Case.Site == FName(TEXT("capture")) && Case.DuelTimeoutMs == 40000
            && Case.WallTimeoutSeconds == 120 && Case.Contenders.Num() == 2 && Case.Contenders[0].Role == TEXT("subject")
            && Case.Contenders[0].SupplyXP == 440 && Case.Contenders[0].Choices.Num() == 3
            && Case.Contenders[0].Choices[1].Rule == ELDEvaluationChoiceRule::OptionIndex && Case.Contenders[0].Choices[1].Index == 2
            && Case.Contenders[0].Choices[2].Rule == ELDEvaluationChoiceRule::Definition
            && Case.Contenders[0].Choices[2].DefinitionId == TEXT("active_elbow")
            && Case.Contenders[0].OpeningPreset == TArray<FName>({FName(TEXT("active_snark_bolt"))})
            && Case.Contenders[0].LevelComponents == TArray<FName>({FName(TEXT("component_punch_heal")), FName(TEXT("component_breath_strike"))})
            && Case.Contenders[0].Presets.Num() == 1 && Case.Contenders[0].Presets[0][2] == FName(TEXT("active_not_mad"))
            && Case.Contenders[0].Stance == ELDEvaluationStance::Pursue && Case.Contenders[0].StandoffCm == 0);
    }
    {
        // The optional kite stance and its standoff reach the contender plan; its absence is pursue.
        const FString Kiter = Side(TEXT("opponent"), 1).LeftChop(1) + TEXT(",\"stance\":{\"kind\":\"kite\",\"standoff_cm\":650}}");
        FLDEvalRunInput Kited;
        FString Why;
        bGood &= Test.TestTrue(TEXT("A kite stance parses: ") + Why, ParseInput(Input(CaseJson(Subject, Kiter)), Kited, Why)
            && Kited.Cases.Num() == 1 && Kited.Cases[0].Contenders[1].Stance == ELDEvaluationStance::Kite
            && Kited.Cases[0].Contenders[1].StandoffCm == 650);
        const FString Guarded = Side(TEXT("opponent"), 1).LeftChop(1)
            + TEXT(",\"stance\":{\"kind\":\"guard\",\"standoff_cm\":600,\"threshold_bp\":3500,\"leash_cm\":1200}}");
        FLDEvalRunInput Guard;
        bGood &= Test.TestTrue(TEXT("A guard stance with its threshold and leash parses: ") + Why,
            ParseInput(Input(CaseJson(Subject, Guarded)), Guard, Why) && Guard.Cases.Num() == 1
            && Guard.Cases[0].Contenders[1].Stance == ELDEvaluationStance::Guard && Guard.Cases[0].Contenders[1].StandoffCm == 600
            && Guard.Cases[0].Contenders[1].ThresholdBp == 3500 && Guard.Cases[0].Contenders[1].LeashCm == 1200);
        const FString Operated = Side(TEXT("opponent"), 1).LeftChop(1)
            + TEXT(",\"stance\":{\"kind\":\"kite\",\"standoff_cm\":450,\"reaction_ms\":800,\"aim_error_cm\":200,\"skip_bp\":3500}}");
        FLDEvalRunInput Operation;
        bGood &= Test.TestTrue(TEXT("Operation knobs on a stance parse: ") + Why, ParseInput(Input(CaseJson(Subject, Operated)), Operation, Why)
            && Operation.Cases.Num() == 1 && Operation.Cases[0].Contenders[1].ReactionMs == 800
            && Operation.Cases[0].Contenders[1].AimErrorCm == 200 && Operation.Cases[0].Contenders[1].SkipBp == 3500
            && Operation.Cases[0].Contenders[0].ReactionMs == 0);
        const FString Hasty = Side(TEXT("opponent"), 1).LeftChop(1) + TEXT(",\"stance\":{\"kind\":\"pursue\",\"reaction_ms\":150}}");
        FLDEvalRunInput Pursuer;
        bGood &= Test.TestTrue(TEXT("An explicit pursue carrying a knob parses: ") + Why, ParseInput(Input(CaseJson(Subject, Hasty)), Pursuer, Why)
            && Pursuer.Cases.Num() == 1 && Pursuer.Cases[0].Contenders[1].Stance == ELDEvaluationStance::Pursue
            && Pursuer.Cases[0].Contenders[1].ReactionMs == 150 && Pursuer.Cases[0].Contenders[1].StandoffCm == 0);
        const auto RefuseStance = [&](const FString& Label, const TCHAR* Stance, const TCHAR* Field)
        {
            FLDEvalRunInput Ignored;
            FString Reason;
            const FString Json = Input(CaseJson(Subject, Side(TEXT("opponent"), 1).LeftChop(1) + TEXT(",\"stance\":") + Stance + TEXT("}")));
            return Test.TestTrue(Label + TEXT(": ") + Reason, !ParseInput(Json, Ignored, Reason) && Reason.Contains(Field));
        };
        bGood &= RefuseStance(TEXT("A reaction past its limit"), TEXT("{\"kind\":\"kite\",\"standoff_cm\":450,\"reaction_ms\":1501}"),
            TEXT("reaction_ms"));
        bGood &= RefuseStance(TEXT("A negative aim error"), TEXT("{\"kind\":\"kite\",\"standoff_cm\":450,\"aim_error_cm\":-1}"),
            TEXT("aim_error_cm"));
        bGood &= RefuseStance(TEXT("A skip past its limit"), TEXT("{\"kind\":\"kite\",\"standoff_cm\":450,\"skip_bp\":9001}"), TEXT("skip_bp"));
        bGood &= RefuseStance(TEXT("A plain pursue (the absent stance)"), TEXT("{\"kind\":\"pursue\"}"), TEXT("kind"));
        bGood &= RefuseStance(TEXT("A pursue with a standoff"), TEXT("{\"kind\":\"pursue\",\"standoff_cm\":450,\"skip_bp\":10}"),
            TEXT("standoff_cm"));
        bGood &= RefuseStance(TEXT("An unknown stance field"), TEXT("{\"kind\":\"kite\",\"standoff_cm\":450,\"dodge\":1}"), TEXT("dodge"));
    }
    const auto Refuse = [&Test](const FString& Label, const FString& Json, const TCHAR* Field)
    {
        FLDEvalRunInput Ignored;
        FString Why;
        return Test.TestTrue(Label + TEXT(": ") + Why, !ParseInput(Json, Ignored, Why) && Why.Contains(Field));
    };
    bGood &= Refuse(TEXT("Not JSON"), TEXT("{"), TEXT("input"));
    bGood &= Refuse(TEXT("The retired schema version 4 (no level components)"), Input(CaseJson(Subject, Opponent)).Replace(
        TEXT("\"schema_version\":5"), TEXT("\"schema_version\":4")), TEXT("schema"));
    bGood &= Refuse(TEXT("An unknown top-level field"), Input(CaseJson(Subject, Opponent), TEXT(",\"budget\":1")), TEXT("budget"));
    bGood &= Refuse(TEXT("An unknown Case field"), Input(CaseJson(Subject, Opponent, TEXT("\"1\""), TEXT(",\"strategy\":\"x\""))),
        TEXT("strategy"));
    bGood &= Refuse(TEXT("A numeric seed loses precision and is refused"), Input(CaseJson(Subject, Opponent, TEXT("12"))), TEXT("seed"));
    bGood &= Refuse(TEXT("A seed past uint64"), Input(CaseJson(Subject, Opponent, TEXT("\"18446744073709551616\""))), TEXT("seed"));
    bGood &= Refuse(TEXT("A signed seed"), Input(CaseJson(Subject, Opponent, TEXT("\"+5\""))), TEXT("seed"));
    bGood &= Refuse(TEXT("Duplicate Case ids"), Input(CaseJson(Subject, Opponent) + TEXT(",") + CaseJson(Subject, Opponent)), TEXT("case_id"));
    bGood &= Refuse(TEXT("One contender"), Input(FString::Printf(TEXT("{\"case_id\":\"c1\",\"seed\":\"1\",\"site\":\"capture\",")
        TEXT("\"duel_timeout_ms\":40000,\"wall_timeout_s\":120,\"contenders\":[%s]}"), *Subject)), TEXT("contenders"));
    bGood &= Refuse(TEXT("Two subjects"), Input(CaseJson(Subject, Side(TEXT("subject"), 1))), TEXT("contenders"));
    bGood &= Refuse(TEXT("One slot twice"), Input(CaseJson(Subject, Side(TEXT("opponent"), 0))), TEXT("contenders"));
    bGood &= Refuse(TEXT("A third AI slot"), Input(CaseJson(Subject, Side(TEXT("opponent"), 2))), TEXT("slot"));
    bGood &= Refuse(TEXT("No choices"), Input(CaseJson(Subject, Side(TEXT("opponent"), 1, TEXT("[]")))), TEXT("choices"));
    bGood &= Refuse(TEXT("An unknown rule"), Input(CaseJson(Subject, Side(TEXT("opponent"), 1, TEXT("[{\"rule\":\"best\"}]")))), TEXT("rule"));
    bGood &= Refuse(TEXT("An index past three options"),
        Input(CaseJson(Subject, Side(TEXT("opponent"), 1, TEXT("[{\"rule\":\"option_index\",\"index\":3}]")))), TEXT("index"));
    bGood &= Refuse(TEXT("The retired replacement field is unsupported"), Input(CaseJson(Subject, Side(TEXT("opponent"), 1,
        TEXT("[{\"rule\":\"definition\",\"definition\":\"active_elbow\",\"replace\":\"W\"}]")))), TEXT("replace"));
    bGood &= Refuse(TEXT("A preset of four actives"), Input(CaseJson(Subject, Side(TEXT("opponent"), 1, TEXT("[{\"rule\":\"ai_select\"}]"),
        TEXT("[[\"active_elbow\",\"active_seethe\",\"active_not_mad\",\"active_unplug\"]]")))), TEXT("presets"));
    bGood &= Refuse(TEXT("A preset naming one active twice"), Input(CaseJson(Subject, Side(TEXT("opponent"), 1,
        TEXT("[{\"rule\":\"ai_select\"}]"), TEXT("[[\"active_elbow\",\"active_elbow\"]]")))), TEXT("presets"));
    bGood &= Refuse(TEXT("An opening preset naming one active twice"), Input(CaseJson(Subject, Side(TEXT("opponent"), 1,
        TEXT("[{\"rule\":\"ai_select\"}]"), TEXT("[]"), TEXT("[]"), TEXT("[\"active_elbow\",\"active_elbow\"]")))),
        TEXT("opening_preset"));
    bGood &= Refuse(TEXT("An opening preset with four actives"), Input(CaseJson(Subject, Side(TEXT("opponent"), 1,
        TEXT("[{\"rule\":\"ai_select\"}]"), TEXT("[]"), TEXT("[]"),
        TEXT("[\"active_elbow\",\"active_seethe\",\"active_not_mad\",\"active_unplug\"]")))), TEXT("opening_preset"));
    bGood &= Refuse(TEXT("A level preset naming one component twice"), Input(CaseJson(Subject, Side(TEXT("opponent"), 1,
        TEXT("[{\"rule\":\"ai_select\"}]"), TEXT("[]"), TEXT("[]"), TEXT("[]"),
        TEXT("[\"component_punch_heal\",\"component_punch_heal\"]")))), TEXT("level_components"));
    bGood &= Refuse(TEXT("A pursue stance is spelled by omission"), Input(CaseJson(Subject, Side(TEXT("opponent"), 1).LeftChop(1)
        + TEXT(",\"stance\":{\"kind\":\"pursue\",\"standoff_cm\":650}}"))), TEXT("stance.kind"));
    bGood &= Refuse(TEXT("A kite standoff below the floor"), Input(CaseJson(Subject, Side(TEXT("opponent"), 1).LeftChop(1)
        + TEXT(",\"stance\":{\"kind\":\"kite\",\"standoff_cm\":100}}"))), TEXT("standoff_cm"));
    bGood &= Refuse(TEXT("A guard stance without its threshold"), Input(CaseJson(Subject, Side(TEXT("opponent"), 1).LeftChop(1)
        + TEXT(",\"stance\":{\"kind\":\"guard\",\"standoff_cm\":600}}"))), TEXT("threshold_bp"));
    bGood &= Refuse(TEXT("A threshold on a stance other than guard"), Input(CaseJson(Subject, Side(TEXT("opponent"), 1).LeftChop(1)
        + TEXT(",\"stance\":{\"kind\":\"hold\",\"standoff_cm\":600,\"threshold_bp\":3500}}"))), TEXT("threshold_bp"));
    bGood &= Refuse(TEXT("A leash below the floor"), Input(CaseJson(Subject, Side(TEXT("opponent"), 1).LeftChop(1)
        + TEXT(",\"stance\":{\"kind\":\"burst\",\"standoff_cm\":600,\"leash_cm\":100}}"))), TEXT("leash_cm"));
    bGood &= Refuse(TEXT("A duel timeout below one second"),
        Input(CaseJson(Subject, Opponent).Replace(TEXT("\"duel_timeout_ms\":40000"), TEXT("\"duel_timeout_ms\":999"))), TEXT("duel_timeout_ms"));
    bGood &= Refuse(TEXT("A content root that is not a SHA-256 digest"),
        Input(CaseJson(Subject, Opponent)).Replace(*FString::ChrN(64, TEXT('a')), TEXT("abc")), TEXT("content_root"));
    bGood &= Refuse(TEXT("A contender without its build plan"), Input(CaseJson(Subject, Opponent.Replace(TEXT(",\"build\":[]"), TEXT("")))),
        TEXT("build"));

    // ---- #198 workshop plan steps ----
    const FString Plan = TEXT("[{\"step\":\"opening_base\",\"at\":\"opening_rest\",\"component\":\"component_dirty_trick\",")
        TEXT("\"bind\":{\"active\":\"active_hose_down\",\"element\":\"water\"}},")
        TEXT("{\"step\":\"allocate_points\",\"at\":\"opening_rest\",\"points\":{\"stat_strength\":2,\"stat_physique\":1}},")
        TEXT("{\"step\":\"uninstall\",\"at\":\"opening_rest\",\"component\":\"component_dirty_trick\"},")
        TEXT("{\"step\":\"install\",\"at\":\"opening_rest\",\"component\":\"component_dirty_trick\",")
        TEXT("\"bind\":{\"active\":\"active_hose_down\",\"element\":\"fire\"}},")
        TEXT("{\"step\":\"respec\",\"at\":\"opening_rest\"},")
        TEXT("{\"step\":\"replace\",\"at\":\"opening_rest\",\"blueprint\":\"active_seethe\",\"slot\":\"Q\"},")
        TEXT("{\"step\":\"install\",\"at\":\"field\",\"component\":\"component_long_kick\",\"target\":{\"kind\":\"active\",\"id\":\"active_hose_down\"}},")
        TEXT("{\"step\":\"install\",\"at\":\"field\",\"component\":\"component_ram_through\",\"target\":{\"kind\":\"dash\"}},")
        TEXT("{\"step\":\"install\",\"at\":\"field\",\"component\":\"component_dirty_fist\",\"target\":{\"kind\":\"component\",")
        TEXT("\"id\":\"component_spiky_hide\"},\"base\":\"component_fist_fire\"},")
        TEXT("{\"step\":\"learn\",\"at\":\"field\",\"blueprint\":\"active_seethe\",\"slot\":\"E\"}]");
    const auto Planned = [](const FString& Build) { return Side(TEXT("subject"), 0, TEXT("[{\"rule\":\"ai_select\"}]"), TEXT("[]"), Build); };
    Error.Reset();
    Parsed = {};
    const bool bPlanned = ParseInput(Input(CaseJson(Planned(Plan), Opponent)), Parsed, Error);
    bGood &= Test.TestTrue(TEXT("A workshop plan parses: ") + Error, bPlanned);
    if (bPlanned && Parsed.Cases.Num() == 1 && Parsed.Cases[0].Contenders[0].Build.Num() == 10)
    {
        using EKind = ELDEvaluationStepKind;
        const TArray<FLDEvaluationBuildStep>& Steps = Parsed.Cases[0].Contenders[0].Build;
        bGood &= Test.TestTrue(TEXT("The opening base keeps its B18 binding"), Steps[0].Kind == EKind::OpeningBase
            && Steps[0].Window == ELDEvaluationStepWindow::OpeningRest && Steps[0].Component == TEXT("component_dirty_trick")
            && Steps[0].BoundActive == TEXT("active_hose_down") && Steps[0].Element == ELDDemoElement::Water);
        bGood &= Test.TestTrue(TEXT("Points keep exact counts per stat id"), Steps[1].Kind == EKind::AllocatePoints
            && Steps[1].Points.Num() == 2 && Steps[1].Points[0].StatId == TEXT("stat_physique") && Steps[1].Points[0].Points == 1
            && Steps[1].Points[1].StatId == TEXT("stat_strength") && Steps[1].Points[1].Points == 2);
        bGood &= Test.TestTrue(TEXT("A rebuild: uninstall, then install the same base with another element"),
            Steps[2].Kind == EKind::UninstallComponent && Steps[3].Kind == EKind::InstallComponent && Steps[3].Element == ELDDemoElement::Fire);
        bGood &= Test.TestTrue(TEXT("Respec and replacement in the opening rest"), Steps[4].Kind == EKind::Respec
            && Steps[5].Kind == EKind::ReplaceActive && Steps[5].Blueprint == TEXT("active_seethe") && Steps[5].Slot == 0);
        bGood &= Test.TestTrue(TEXT("Mod and connector targets"), Steps[6].Window == ELDEvaluationStepWindow::Field
            && Steps[6].TargetKind == ELDComponentTargetKind::Active && Steps[6].TargetId == TEXT("active_hose_down")
            && Steps[7].TargetKind == ELDComponentTargetKind::Dash && Steps[7].TargetId.IsEmpty()
            && Steps[8].TargetKind == ELDComponentTargetKind::Component && Steps[8].TargetId == TEXT("component_spiky_hide")
            && Steps[8].BoundComponent == TEXT("component_fist_fire"));
        bGood &= Test.TestTrue(TEXT("A field learn into a named slot"), Steps[9].Kind == EKind::LearnBlueprint && Steps[9].Slot == 2);
    }
    const auto RefuseStep = [&](const FString& Label, const FString& Step, const TCHAR* Field)
    {
        return Refuse(Label, Input(CaseJson(Planned(TEXT("[") + Step + TEXT("]")), Opponent)), Field);
    };
    bGood &= RefuseStep(TEXT("An unknown step (NPC purchases are not plannable)"), TEXT("{\"step\":\"buy_npc_offer\",\"at\":\"opening_rest\"}"),
        TEXT("step"));
    bGood &= RefuseStep(TEXT("An unknown window"), TEXT("{\"step\":\"respec\",\"at\":\"rest_1\"}"), TEXT("at"));
    bGood &= RefuseStep(TEXT("A field uninstall (rest-only)"), TEXT("{\"step\":\"uninstall\",\"at\":\"field\",\"component\":\"component_fist_fire\"}"),
        TEXT("rest-window"));
    bGood &= RefuseStep(TEXT("A field replacement (rest-only)"),
        TEXT("{\"step\":\"replace\",\"at\":\"field\",\"blueprint\":\"active_seethe\",\"slot\":\"Q\"}"), TEXT("rest-window"));
    bGood &= RefuseStep(TEXT("A field after an opening step order"), TEXT("{\"step\":\"install\",\"at\":\"field\",\"component\":\"component_fist_fire\"},")
        TEXT("{\"step\":\"respec\",\"at\":\"opening_rest\"}"), TEXT("after a field step"));
    bGood &= RefuseStep(TEXT("A field that the step does not take"),
        TEXT("{\"step\":\"uninstall\",\"at\":\"opening_rest\",\"component\":\"component_fist_fire\",\"target\":{\"kind\":\"dash\"}}"), TEXT("target"));
    bGood &= RefuseStep(TEXT("An unknown element"), TEXT("{\"step\":\"opening_base\",\"at\":\"opening_rest\",\"component\":\"component_dirty_trick\",")
        TEXT("\"bind\":{\"active\":\"active_hose_down\",\"element\":\"ice\"}}"), TEXT("element"));
    bGood &= RefuseStep(TEXT("An active target without its id"), TEXT("{\"step\":\"install\",\"at\":\"field\",\"component\":\"component_long_kick\",")
        TEXT("\"target\":{\"kind\":\"active\"}}"), TEXT("id"));
    bGood &= RefuseStep(TEXT("A dash target naming a definition"), TEXT("{\"step\":\"install\",\"at\":\"field\",\"component\":\"component_ram_through\",")
        TEXT("\"target\":{\"kind\":\"dash\",\"id\":\"active_elbow\"}}"), TEXT("id"));
    bGood &= RefuseStep(TEXT("An unknown target kind"), TEXT("{\"step\":\"install\",\"at\":\"field\",\"component\":\"component_long_kick\",")
        TEXT("\"target\":{\"kind\":\"segment\",\"id\":\"active_elbow\"}}"), TEXT("kind"));
    bGood &= RefuseStep(TEXT("Zero points"), TEXT("{\"step\":\"allocate_points\",\"at\":\"field\",\"points\":{\"stat_strength\":0}}"), TEXT("points"));
    bGood &= RefuseStep(TEXT("No stat"), TEXT("{\"step\":\"allocate_points\",\"at\":\"field\",\"points\":{}}"), TEXT("points"));
    bGood &= RefuseStep(TEXT("A slot other than Q/W/E"), TEXT("{\"step\":\"learn\",\"at\":\"field\",\"blueprint\":\"active_seethe\",\"slot\":\"D\"}"),
        TEXT("slot"));
    bGood &= RefuseStep(TEXT("A component id outside the definition subset"),
        TEXT("{\"step\":\"install\",\"at\":\"field\",\"component\":\"Component-Long\"}"), TEXT("component"));
    FString TooMany = TEXT("[");
    for (int32 Index = 0; Index <= MaxBuildSteps; ++Index)
    {
        TooMany += (Index ? TEXT(",") : TEXT("")) + FString(TEXT("{\"step\":\"respec\",\"at\":\"opening_rest\"}"));
    }
    bGood &= Refuse(TEXT("More steps than the native limit"), Input(CaseJson(Planned(TooMany + TEXT("]")), Opponent)), TEXT("build"));
    return bGood;
}

bool RunHorizon(FAutomationTestBase& Test)
{
    using LDBuildEvaluation::KillAtOrBeforeHorizon;
    bool bGood = Test.TestFalse(TEXT("A kill before the field begins never counts"),
        KillAtOrBeforeHorizon(5000, 45001, 4999));
    bGood &= Test.TestTrue(TEXT("A kill at the field start counts"),
        KillAtOrBeforeHorizon(5000, 45001, 5000));
    bGood &= Test.TestTrue(TEXT("A kill before the non-frame-aligned horizon counts"),
        KillAtOrBeforeHorizon(5000, 45001, 50000));
    bGood &= Test.TestTrue(TEXT("A kill exactly at the horizon counts"),
        KillAtOrBeforeHorizon(5000, 45001, 50001));
    bGood &= Test.TestFalse(TEXT("A kill one millisecond after the horizon is censored"),
        KillAtOrBeforeHorizon(5000, 45001, 50002));
    bGood &= Test.TestFalse(TEXT("A kill after a frame-aligned horizon is censored"),
        KillAtOrBeforeHorizon(5000, 45000, 50001));
    const FGuid Subject(1, 0, 0, 1), Opponent(2, 0, 0, 1), Other(3, 0, 0, 1);
    const auto Notice = [](uint64 Serial, const FGuid& Victim, const FGuid& Killer, int64 At)
    {
        FLDKillNotice Row;
        Row.Serial = Serial;
        Row.Victim = Victim;
        Row.Killer = Killer;
        Row.OccurredAtMs = At;
        return Row;
    };
    const FLDKillNotice First = Notice(1, Subject, Opponent, 10000);
    const FLDKillNotice Reciprocal = Notice(2, Opponent, Subject, 10000);
    uint64 Last = 0;
    FLDEvalOutcome Outcome;
    bGood &= Test.TestTrue(TEXT("Reciprocal same-time deaths are an observed trade, not whichever notice came first"),
        LDBuildEvaluation::SelectDuelOutcome({First, Reciprocal}, Last, Subject, Opponent, 5000, 45000, Outcome)
        && Outcome.bTrade && Outcome.OccurredAtMs == 10000 && Outcome.VictimSide == INDEX_NONE && !Outcome.Killer.IsValid()
        && Last == 2);
    Last = 0;
    bGood &= Test.TestTrue(TEXT("A trade is independent of notice order"),
        LDBuildEvaluation::SelectDuelOutcome({Reciprocal, First}, Last, Subject, Opponent, 5000, 45000, Outcome)
        && Outcome.bTrade && Outcome.OccurredAtMs == 10000);
    Last = 0;
    bGood &= Test.TestTrue(TEXT("A later second death does not turn the first into a trade"),
        LDBuildEvaluation::SelectDuelOutcome({Notice(1, Other, Subject, 9000), First, Notice(3, Opponent, Subject, 10001)},
            Last, Subject, Opponent, 5000, 45000, Outcome)
        && !Outcome.bTrade && Outcome.VictimSide == 0 && Outcome.Killer == Opponent && Outcome.OccurredAtMs == 10000);
    bGood &= Test.TestFalse(TEXT("Already observed notices are not returned again"),
        LDBuildEvaluation::SelectDuelOutcome({First, Reciprocal}, Last, Subject, Opponent, 5000, 45000, Outcome));
    return bGood;
}

// The operation knobs filter what the stance chose, from the authorized frame only, with deterministic rolls.
bool RunOperationGates(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDTestApprovedFixture::Read(Test, Catalog)) { return false; }
    const FLDDemoContentSnapshot& C = *Catalog;
    using EBody = ELDOpponentBodyKind;
    FLDOpponentDecisionFrame Base;
    if (!Frame(Test, C, Base)) { return false; }
    Base.Self.OffersConsumed = 1;
    const double Attack = double(Base.Self.Attributes.AttackRangeCm.Numerator) / double(Base.Self.Attributes.AttackRangeCm.Denominator);
    FLDEvaluationProfile Exact;
    Exact.Opponent = FGuid(0xED, 0, 0, 2);
    Exact.Site = TEXT("capture");
    Exact.Choices = {FLDEvaluationChoice()};
    Exact.OperationSeed = 20261113;
    const auto With = [&](const TCHAR* Id, double X, double Y, int64 NowMs, bool bReady = true)
    {
        auto Out = Base;
        Out.Public.NowMs = NowMs;
        Out.Self.Actives = {Active(Id, 0)};
        FLDOpponentSource Source;
        Source.Slot = ELDOpponentSourceSlot::Q;
        Source.DefinitionId = Id;
        Source.bReady = bReady;
        const FLDDemoAbilityDefinition* Ability = nullptr;
        FString Error;
        if (C.FindAbility(Id, Ability, Error) && Ability)
        {
            Source.RangeCm = int32(Ability->Target.RangeCm.Numerator / FMath::Max<int64>(1, Ability->Target.RangeCm.Denominator));
        }
        Out.Self.Sources = {Source};
        Out.Units = {Rival(8, Exact.Opponent, X, Y)};
        return Out;
    };
    const auto Decide = [&C](const FLDOpponentDecisionFrame& At, const FLDEvaluationProfile& Plan, FLDEvaluationProfileState& State)
    {
        FLDOpponentPolicyState Policy;
        return LDEvaluationPolicy::Decide(At, C, Plan, Policy, State).Body;
    };
    const auto IsCast = [](const FLDOpponentBodyIntent& Body) { return Body.Kind == EBody::Activate && Body.Source == ELDOpponentSourceSlot::Q; };
    const FVector Target(Attack - 10, 120, 0);
    const TCHAR* Zone = TEXT("active_quick_meeting");
    bool bGood = true;

    // ---- All knobs at zero: the stance's activation exactly, no roll drawn ----
    FLDEvaluationProfileState State;
    const FLDOpponentBodyIntent Plain = Decide(With(Zone, Target.X, Target.Y, 20000), Exact, State);
    bGood &= Test.TestTrue(TEXT("Zero knobs: the zone lands on the rival's foot at once and nothing is rolled"),
        IsCast(Plain) && Plain.TargetMode == ELDDemoTargetMode::GroundPoint && Plain.Point.Equals(Target, 1.e-3) && State.Rolls == 0);

    // ---- Reaction: a ready source is held until it has been seen ready that long; not ready starts it afresh ----
    FLDEvaluationProfile Slow = Exact;
    Slow.ReactionMs = 400;
    State = {};
    bGood &= Test.TestFalse(TEXT("Reaction: the first frame the slot is seen ready it does not cast"),
        IsCast(Decide(With(Zone, Target.X, Target.Y, 20000), Slow, State)));
    bGood &= Test.TestTrue(TEXT("Reaction: the slot's readiness is observed from the frame"), State.ReadySinceMs[0] == 20000);
    bGood &= Test.TestFalse(TEXT("Reaction: 399 ms later it still waits"), IsCast(Decide(With(Zone, Target.X, Target.Y, 20399), Slow, State)));
    const FLDOpponentBodyIntent Reacted = Decide(With(Zone, Target.X, Target.Y, 20400), Slow, State);
    bGood &= Test.TestTrue(TEXT("Reaction: after the reaction it casts the same zone exactly"),
        IsCast(Reacted) && Reacted.Point.Equals(Target, 1.e-3) && State.Rolls == 0);
    Decide(With(Zone, Target.X, Target.Y, 20500, false), Slow, State);
    bGood &= Test.TestTrue(TEXT("Reaction: a slot seen not ready forgets its readiness"), State.ReadySinceMs[0] == -1);
    bGood &= Test.TestFalse(TEXT("Reaction: ready again, the reaction starts afresh"),
        IsCast(Decide(With(Zone, Target.X, Target.Y, 20600), Slow, State)));
    auto Reborn = With(Zone, Target.X, Target.Y, 21100);
    Reborn.Self.Life = 2;
    bGood &= Test.TestFalse(TEXT("Reaction: another life starts the gate afresh"), IsCast(Decide(Reborn, Slow, State))
        && State.ReadySinceMs[0] == 21100);

    // ---- Aim: a ground point lands within the error and the source's reach; deterministic per seed ----
    FLDEvaluationProfile Shaky = Exact;
    Shaky.AimErrorCm = 200;
    State = {};
    const FLDOpponentBodyIntent Aimed = Decide(With(Zone, Target.X, Target.Y, 20000), Shaky, State);
    FLDEvaluationProfileState Again;
    const FLDOpponentBodyIntent Replayed = Decide(With(Zone, Target.X, Target.Y, 20000), Shaky, Again);
    const double Reach = FMath::Max(double(With(Zone, 0, 0, 0).Self.Sources[0].RangeCm), Target.Size2D());
    bGood &= Test.TestTrue(FString::Printf(TEXT("Aim: the zone lands off the foot by at most the error (%.1f cm), within reach, at its height"),
            FVector::Dist2D(Aimed.Point, Target)),
        IsCast(Aimed) && FVector::Dist2D(Aimed.Point, Target) <= 200. + 1.e-3 && !Aimed.Point.Equals(Target, 1.e-3)
        && Aimed.Point.Size2D() <= Reach + 1.e-3 && Aimed.Point.Z == Target.Z && State.Rolls == 1);
    bGood &= Test.TestTrue(TEXT("Aim: the same seed and frame aim the same way"), Replayed.Point.Equals(Aimed.Point, 1.e-6));
    FLDEvaluationProfile Reseeded = Shaky;
    Reseeded.OperationSeed = Shaky.OperationSeed + 1;
    State = {};
    bGood &= Test.TestFalse(TEXT("Aim: another Case seed rolls another way"),
        Decide(With(Zone, Target.X, Target.Y, 20000), Reseeded, State).Point.Equals(Aimed.Point, 1.e-6));
    {
        // A direction turns by at most atan2(error, max(200, gap)), keeping its length.
        const FVector Near(150, 0, 0);
        State = {};
        const FLDOpponentBodyIntent Bolt = Decide(With(TEXT("active_snark_bolt"), Near.X, Near.Y, 20000), Exact, State);
        FLDEvaluationProfileState Turned;
        const FLDOpponentBodyIntent Off = Decide(With(TEXT("active_snark_bolt"), Near.X, Near.Y, 20000), Shaky, Turned);
        const bool bDirection = IsCast(Bolt) && (Bolt.TargetMode == ELDDemoTargetMode::DirectionProjectile
            || Bolt.TargetMode == ELDDemoTargetMode::DirectionCone || Bolt.TargetMode == ELDDemoTargetMode::DirectionLine);
        const double Turn = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Bolt.Direction.GetSafeNormal(),
            Off.Direction.GetSafeNormal()), -1., 1.)));
        bGood &= Test.TestTrue(FString::Printf(TEXT("Aim: a direction cast turns by at most the error over the lever (%.2f deg)"), Turn),
            bDirection && IsCast(Off) && Off.TargetMode == Bolt.TargetMode && Turn > 0.
            && Turn <= FMath::RadiansToDegrees(FMath::Atan2(200., LDEvaluationPolicy::MinAimLeverCm)) + 1.e-6
            && FMath::IsNearlyEqual(Off.Direction.Size(), Bolt.Direction.Size(), 1.e-6) && Turned.Rolls == 1);
    }

    // ---- Skip: a failed roll holds the slot max(reaction, 500) ms, then rolls again; nothing rolls in between ----
    FLDEvaluationProfile Hesitant = Exact;
    Hesitant.SkipBp = LDEvaluationPolicy::MaxSkipBp;
    int32 Skipped = 0;
    bool bHeldRight = true, bAllCast = true;
    for (uint64 Seed = 1; Seed <= 20; ++Seed)
    {
        Hesitant.OperationSeed = Seed;
        State = {};
        bool bCast = false;
        for (int64 Now = 20000; Now <= 80000 && !bCast; Now += 100)
        {
            const uint64 Before = State.Rolls;
            const int64 Until = State.SkipUntilMs[0];
            bCast = IsCast(Decide(With(Zone, Target.X, Target.Y, Now), Hesitant, State));
            if (Now < Until) { bHeldRight &= !bCast && State.Rolls == Before; }
            if (!bCast && State.SkipUntilMs[0] > Until)
            {
                ++Skipped;
                bHeldRight &= State.SkipUntilMs[0] == Now + LDEvaluationPolicy::MinSkipHoldMs && State.Rolls == Before + 1;
            }
        }
        bAllCast &= bCast && State.bSkipPassed[0];
    }
    bGood &= Test.TestTrue(TEXT("Skip: a 90% skip holds some casts"), Skipped > 0);
    bGood &= Test.TestTrue(TEXT("Skip: a skipped slot is held exactly the hold and rolls only when it reopens"), bHeldRight);
    bGood &= Test.TestTrue(TEXT("Skip: every seed casts once a roll passes"), bAllCast);

    // ---- Kite reaction: the first step away waits until the rival has stood inside the standoff that long ----
    FLDEvaluationProfile Kiter = Exact;
    Kiter.Stance = ELDEvaluationStance::Kite;
    Kiter.StandoffCm = 600;
    Kiter.ReactionMs = 300;
    auto Close = Base;
    Close.Units = {Rival(8, Exact.Opponent, 200, 0)};
    State = {};
    const FLDOpponentBodyIntent Startled = Decide(Close, Kiter, State);
    bGood &= Test.TestTrue(TEXT("Kite reaction: the rival just came inside the standoff, no step away yet"),
        Startled.Kind != EBody::MoveGround && State.CloseSinceMs == Close.Public.NowMs);
    Close.Public.NowMs += 300;
    const FLDOpponentBodyIntent Stepped = Decide(Close, Kiter, State);
    bGood &= Test.TestTrue(TEXT("Kite reaction: after the reaction it steps away"),
        Stepped.Kind == EBody::MoveGround && Stepped.Point.Equals(FVector(-LDEvaluationPolicy::KiteStepCm, 0, 0), 1.));
    auto Away = Close;
    Away.Units = {Rival(8, Exact.Opponent, 700, 0)};
    Decide(Away, Kiter, State);
    bGood &= Test.TestTrue(TEXT("Kite reaction: a rival back outside the standoff resets it"), State.CloseSinceMs == -1);
    return bGood;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildEvaluationOperationGatesTest, "Lockdown.Rules.BuildEvaluation.OperationGates",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDBuildEvaluationOperationGatesTest::RunTest(const FString& Parameters) { return LDBuildEvaluationTests::RunOperationGates(*this); }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildEvaluationSeedsTest, "Lockdown.Rules.BuildEvaluation.Seeds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDBuildEvaluationSeedsTest::RunTest(const FString& Parameters) { return LDBuildEvaluationTests::RunSeeds(*this); }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildEvaluationPolicyTest, "Lockdown.Rules.BuildEvaluation.Policy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDBuildEvaluationPolicyTest::RunTest(const FString& Parameters) { return LDBuildEvaluationTests::RunPolicy(*this); }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildEvaluationWorkshopPlanTest, "Lockdown.Rules.BuildEvaluation.WorkshopPlan",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDBuildEvaluationWorkshopPlanTest::RunTest(const FString& Parameters) { return LDBuildEvaluationTests::RunWorkshopPlan(*this); }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildEvaluationInputTest, "Lockdown.Rules.BuildEvaluation.Input",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDBuildEvaluationInputTest::RunTest(const FString& Parameters) { return LDBuildEvaluationTests::RunInput(*this); }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildEvaluationHorizonTest, "Lockdown.Rules.BuildEvaluation.Horizon",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDBuildEvaluationHorizonTest::RunTest(const FString& Parameters) { return LDBuildEvaluationTests::RunHorizon(*this); }
#endif
