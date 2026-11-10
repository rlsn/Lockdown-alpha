#include "Evaluation/LDEvaluationPolicy.h"

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
        && State.Phase == ELDEvaluationPhase::Building);
    auto InFlight = Countdown;
    InFlight.bBuildChangeInFlight = true;
    State = {};
    bGood &= Test.TestTrue(TEXT("No second choice while one is in flight"),
        Decide(InFlight, Profile, State).Build.Kind == ELDOpponentBuildKind::None);
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
    for (const auto& Stat : C.Get().Stats)
    {
        FLDOpponentStatPoints Row;
        Row.StatId = Stat.Id;
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
    return bGood;
}

FString Input(const FString& Cases, const FString& Top = FString())
{
    return FString::Printf(TEXT("{\"schema\":\"lockdown.build_evaluation.native_input\",\"schema_version\":2,\"run_id\":\"run-1\",")
        TEXT("\"content_root\":\"%s\"%s,\"cases\":[%s]}"), *FString::ChrN(64, TEXT('a')), *Top, *Cases);
}

FString CaseJson(const FString& Subject, const FString& Opponent, const FString& Seed = TEXT("\"18446744073709551615\""),
    const FString& Extra = FString())
{
    return FString::Printf(TEXT("{\"case_id\":\"c1\",\"seed\":%s,\"site\":\"capture\",\"duel_timeout_ms\":40000,\"wall_timeout_s\":120%s,")
        TEXT("\"contenders\":[%s,%s]}"), *Seed, *Extra, *Subject, *Opponent);
}

FString Side(const TCHAR* Role, int32 Slot, const FString& Choices = TEXT("[{\"rule\":\"ai_select\"}]"),
    const FString& Presets = TEXT("[]"))
{
    return FString::Printf(TEXT("{\"role\":\"%s\",\"slot\":%d,\"supply_xp\":440,\"choices\":%s,\"presets\":%s}"), Role, Slot, *Choices, *Presets);
}

bool RunInput(FAutomationTestBase& Test)
{
    using namespace LDBuildEvaluation;
    const FString Subject = Side(TEXT("subject"), 0, TEXT("[{\"rule\":\"ai_select\"},{\"rule\":\"option_index\",\"index\":2},")
        TEXT("{\"rule\":\"definition\",\"definition\":\"active_elbow\"}]"),
        TEXT("[[\"active_elbow\",\"active_seethe\",\"active_not_mad\"]]"));
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
            && Case.Contenders[0].Presets.Num() == 1 && Case.Contenders[0].Presets[0][2] == FName(TEXT("active_not_mad")));
    }
    const auto Refuse = [&Test](const FString& Label, const FString& Json, const TCHAR* Field)
    {
        FLDEvalRunInput Ignored;
        FString Why;
        return Test.TestTrue(Label + TEXT(": ") + Why, !ParseInput(Json, Ignored, Why) && Why.Contains(Field));
    };
    bGood &= Refuse(TEXT("Not JSON"), TEXT("{"), TEXT("input"));
    bGood &= Refuse(TEXT("The retired schema version 1 (three-card choices)"), Input(CaseJson(Subject, Opponent)).Replace(
        TEXT("\"schema_version\":2"), TEXT("\"schema_version\":1")), TEXT("schema"));
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
    bGood &= Refuse(TEXT("A duel timeout below one second"),
        Input(CaseJson(Subject, Opponent).Replace(TEXT("\"duel_timeout_ms\":40000"), TEXT("\"duel_timeout_ms\":999"))), TEXT("duel_timeout_ms"));
    bGood &= Refuse(TEXT("A content root that is not a SHA-256 digest"),
        Input(CaseJson(Subject, Opponent)).Replace(*FString::ChrN(64, TEXT('a')), TEXT("abc")), TEXT("content_root"));
    return bGood;
}

bool RunHorizon(FAutomationTestBase& Test)
{
    using LDBuildEvaluation::KillAtOrBeforeHorizon;
    bool bGood = Test.TestTrue(TEXT("A kill before the non-frame-aligned horizon counts"),
        KillAtOrBeforeHorizon(5000, 45001, 50000));
    bGood &= Test.TestTrue(TEXT("A kill exactly at the horizon counts"),
        KillAtOrBeforeHorizon(5000, 45001, 50001));
    bGood &= Test.TestFalse(TEXT("A kill one millisecond after the horizon is censored"),
        KillAtOrBeforeHorizon(5000, 45001, 50002));
    bGood &= Test.TestFalse(TEXT("A kill after a frame-aligned horizon is censored"),
        KillAtOrBeforeHorizon(5000, 45000, 50001));
    return bGood;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildEvaluationSeedsTest, "Lockdown.Rules.BuildEvaluation.Seeds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDBuildEvaluationSeedsTest::RunTest(const FString& Parameters) { return LDBuildEvaluationTests::RunSeeds(*this); }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildEvaluationPolicyTest, "Lockdown.Rules.BuildEvaluation.Policy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDBuildEvaluationPolicyTest::RunTest(const FString& Parameters) { return LDBuildEvaluationTests::RunPolicy(*this); }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildEvaluationInputTest, "Lockdown.Rules.BuildEvaluation.Input",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDBuildEvaluationInputTest::RunTest(const FString& Parameters) { return LDBuildEvaluationTests::RunInput(*this); }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildEvaluationHorizonTest, "Lockdown.Rules.BuildEvaluation.Horizon",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDBuildEvaluationHorizonTest::RunTest(const FString& Parameters) { return LDBuildEvaluationTests::RunHorizon(*this); }
#endif
