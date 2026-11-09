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

FLDOpponentOption Option(uint32 Id, ELDOpponentOptionKind Kind, const TCHAR* Definition, uint8 From = 0, uint8 To = 1)
{
    FLDOpponentOption Out;
    Out.OptionId = FGuid(0xEA, 0, 0, Id);
    Out.Kind = Kind;
    Out.DefinitionId = Definition;
    Out.FromRank = From;
    Out.ToRank = To;
    return Out;
}

FLDOpponentSkill Skill(const TCHAR* Definition, uint8 Rank, ELDOpponentSkillSlot Slot)
{
    FLDOpponentSkill Out;
    Out.DefinitionId = Definition;
    Out.Rank = Rank;
    Out.Slot = Slot;
    return Out;
}

FLDOpponentOffer Offer(TArray<FLDOpponentOption> Options)
{
    FLDOpponentOffer Out;
    Out.OfferId = FGuid(0xEB, 0, 0, 1);
    Out.OfferRevision = 3;
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
    const auto Projected = LDAttributeProjection::Project(FLDAttributeBuildValues(1, {}, {}), Catalog);
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
    const FLDEvaluationChoice& Rule, bool bAccepted, uint32 Expected = 0, const TCHAR* ErrorPart = TEXT(""))
{
    FLDOpponentChoicePick Out;
    FString Error;
    const bool bChosen = LDEvaluationPolicy::Choose(Self, Catalog, Rule, Out, Error);
    if (!bAccepted)
    {
        return Test.TestTrue(Label + TEXT(": ") + Error, !bChosen && Error.Contains(ErrorPart));
    }
    return Test.TestTrue(Label + TEXT(": ") + Error, bChosen && Out.OptionId == FGuid(0xEA, 0, 0, Expected));
}

bool RunPolicy(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDTestApprovedFixture::Read(Test, Catalog)) { return false; }
    const FLDDemoContentSnapshot& C = *Catalog;
    using EKind = ELDOpponentOptionKind;
    using ESlot = ELDOpponentSkillSlot;
    using ERule = ELDEvaluationChoiceRule;
    FLDOpponentSelf Self;
    Self.Offer = Offer({Option(1, EKind::Attribute, TEXT("attr_protein")), Option(2, EKind::LearnPassive, TEXT("passive_pressed")),
        Option(3, EKind::LearnActive, TEXT("active_elbow"))});
    FLDEvaluationChoice Rule;
    Rule.Rule = ERule::OptionIndex;
    Rule.Index = 1;
    bool bGood = Pick(Test, TEXT("option_index takes that option of the actual offer"), C, Self, Rule, true, 2);
    Rule.Index = 2;
    bGood &= Pick(Test, TEXT("A free Q takes a new active without a replacement"), C, Self, Rule, true, 3);
    Rule = {};
    Rule.Rule = ERule::Definition;
    Rule.DefinitionId = TEXT("passive_pressed");
    bGood &= Pick(Test, TEXT("definition takes the offered card"), C, Self, Rule, true, 2);
    Rule.DefinitionId = TEXT("passive_debt_collector");
    bGood &= Pick(Test, TEXT("A planned card that is not offered is refused, never replaced"), C, Self, Rule, false, 0,
        TEXT("is not in the actual offer"));
    FLDOpponentSelf Short = Self;
    Short.Offer->Options.SetNum(2);
    Rule = {};
    Rule.Rule = ERule::OptionIndex;
    Rule.Index = 2;
    bGood &= Pick(Test, TEXT("An index past the actual offer is refused"), C, Short, Rule, false, 0, TEXT("has no option 2"));
    FLDOpponentSelf Full = Self;
    Full.Skills = {Skill(TEXT("active_not_mad"), 1, ESlot::Q), Skill(TEXT("active_snark_bolt"), 2, ESlot::W),
        Skill(TEXT("active_pants_down"), 1, ESlot::E)};
    Rule = {};
    Rule.Rule = ERule::Definition;
    Rule.DefinitionId = TEXT("active_elbow");
    bGood &= Pick(Test, TEXT("A new active with Q/W/E taken needs a planned replacement"), C, Full, Rule, false, 0,
        TEXT("needs a planned Q/W/E replacement"));
    Rule.Replace = ESlot::W;
    FLDOpponentChoicePick Replaced;
    FString Error;
    bGood &= Test.TestTrue(TEXT("The planned replacement confirms exactly that slot, skill and rank"),
        LDEvaluationPolicy::Choose(Full, C, Rule, Replaced, Error) && Replaced.bReplace && Replaced.ReplaceSlot == ESlot::W
        && Replaced.ReplacedSkillId == TEXT("active_snark_bolt") && Replaced.ReplacedRank == 2 && Replaced.OptionId == FGuid(0xEA, 0, 0, 3));
    FLDOpponentChoicePick Production, Evaluated;
    Rule = {};
    bGood &= Test.TestTrue(TEXT("ai_select is the production choice"), LDOpponentPolicy::SelectChoice(Full, C, Production, Error)
        && LDEvaluationPolicy::Choose(Full, C, Rule, Evaluated, Error) && Production.OptionId == Evaluated.OptionId
        && Production.bReplace == Evaluated.bReplace && Production.bReroll == Evaluated.bReroll);

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
    // The opening countdown: the planned opening is chosen at once, the body does not move.
    auto Countdown = Base;
    Countdown.Public.Phase = ELDMatchPhase::Countdown;
    Countdown.Self.Offer = Offer({Option(1, EKind::Opening, TEXT("opening_athlete")), Option(2, EKind::Opening, TEXT("opening_grinder")),
        Option(3, EKind::Opening, TEXT("opening_rider"))});
    Countdown.Self.OpeningPickIndex = 1;
    FLDEvaluationProfileState State;
    FLDOpponentDecision Decision = Decide(Countdown, Profile, State);
    bGood &= Test.TestTrue(TEXT("Countdown: the planned opening (the production card index) and no body intent"),
        Decision.Build.Kind == ELDOpponentBuildKind::Choose && Decision.Build.Choice.OptionId == FGuid(0xEA, 0, 0, 2)
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
    Building.Self.ChoicesConsumed = 1;
    Building.Units = {Rival(1, Profile.Opponent, 300, 0)};
    State = {};
    Decision = Decide(Building, Two, State);
    bGood &= Test.TestTrue(TEXT("Building: no body intent before the planned choices are consumed"),
        Decision.Body.Kind == ELDOpponentBodyKind::None && State.Phase == ELDEvaluationPhase::Building);
    // Plan done and the rival in sight: the production engagement of the rival, not of a nearer other contender.
    auto Engage = Base;
    Engage.Self.ChoicesConsumed = 1;
    Engage.Units = {Rival(7, FGuid(0xED, 0, 0, 3), 100, 0), Rival(8, Profile.Opponent, 150, 0)};
    State = {};
    Decision = Decide(Engage, Profile, State);
    bGood &= Test.TestTrue(FString::Printf(TEXT("Engaging: only the designated rival is attacked (body %u)"), uint8(Decision.Body.Kind)),
        Decision.Body.Kind == ELDOpponentBodyKind::AttackUnit && Decision.Body.Handle == FGuid(0xEC, 0, 0, 8)
        && Decision.Mode == ELDOpponentMode::Engage && State.Phase == ELDEvaluationPhase::Engaging);
    auto Low = Engage;
    Low.Self.HpP2 = Low.Self.MaxHpP2 / 10;
    State = {};
    Decision = Decide(Low, Profile, State);
    bGood &= Test.TestTrue(TEXT("A low body keeps fighting: the fixed duel strategy never retreats"),
        Decision.Body.Kind == ELDOpponentBodyKind::AttackUnit && Decision.Body.Handle == FGuid(0xEC, 0, 0, 8));
    // The rival out of sight: walk to the meeting place; at it, hold.
    auto Approach = Base;
    Approach.Self.ChoicesConsumed = 1;
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
    Missing.Choices[1].DefinitionId = TEXT("passive_debt_collector");
    auto Offered = Building;
    Offered.Self.Offer = Self.Offer;
    State = {};
    Decision = Decide(Offered, Missing, State);
    bGood &= Test.TestTrue(TEXT("An unoffered planned card fails the plan at its ordinal"), State.Phase == ELDEvaluationPhase::Failed
        && State.Failure.Contains(TEXT("ordinal 2")) && Decision.Build.Kind == ELDOpponentBuildKind::None);
    return bGood;
}

FString Input(const FString& Cases, const FString& Top = FString())
{
    return FString::Printf(TEXT("{\"schema\":\"lockdown.build_evaluation.native_input\",\"schema_version\":1,\"run_id\":\"run-1\",")
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
        TEXT("{\"rule\":\"definition\",\"definition\":\"active_elbow\",\"replace\":\"W\"}]"),
        TEXT("[[\"attr_protein\",\"attr_canteen\",\"passive_pressed\"]]"));
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
            && Case.Contenders[0].Choices[2].Replace.Get(ELDOpponentSkillSlot::Passive) == ELDOpponentSkillSlot::W
            && Case.Contenders[0].Presets.Num() == 1 && Case.Contenders[0].Presets[0][2] == FName(TEXT("passive_pressed")));
    }
    const auto Refuse = [&Test](const FString& Label, const FString& Json, const TCHAR* Field)
    {
        FLDEvalRunInput Ignored;
        FString Why;
        return Test.TestTrue(Label + TEXT(": ") + Why, !ParseInput(Json, Ignored, Why) && Why.Contains(Field));
    };
    bGood &= Refuse(TEXT("Not JSON"), TEXT("{"), TEXT("input"));
    bGood &= Refuse(TEXT("Another schema version"), Input(CaseJson(Subject, Opponent)).Replace(TEXT("\"schema_version\":1"),
        TEXT("\"schema_version\":2")), TEXT("schema"));
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
    bGood &= Refuse(TEXT("ai_select with a replacement"),
        Input(CaseJson(Subject, Side(TEXT("opponent"), 1, TEXT("[{\"rule\":\"ai_select\",\"replace\":\"Q\"}]")))), TEXT("ai_select"));
    bGood &= Refuse(TEXT("A replacement outside Q/W/E"), Input(CaseJson(Subject, Side(TEXT("opponent"), 1,
        TEXT("[{\"rule\":\"definition\",\"definition\":\"active_elbow\",\"replace\":\"R\"}]")))), TEXT("replace"));
    bGood &= Refuse(TEXT("A preset of two cards"), Input(CaseJson(Subject, Side(TEXT("opponent"), 1, TEXT("[{\"rule\":\"ai_select\"}]"),
        TEXT("[[\"attr_protein\",\"attr_canteen\"]]")))), TEXT("presets"));
    bGood &= Refuse(TEXT("A preset naming one card twice"), Input(CaseJson(Subject, Side(TEXT("opponent"), 1,
        TEXT("[{\"rule\":\"ai_select\"}]"), TEXT("[[\"attr_protein\",\"attr_protein\",\"attr_canteen\"]]")))), TEXT("presets"));
    bGood &= Refuse(TEXT("A duel timeout below one second"),
        Input(CaseJson(Subject, Opponent).Replace(TEXT("\"duel_timeout_ms\":40000"), TEXT("\"duel_timeout_ms\":999"))), TEXT("duel_timeout_ms"));
    bGood &= Refuse(TEXT("A content root that is not a SHA-256 digest"),
        Input(CaseJson(Subject, Opponent)).Replace(*FString::ChrN(64, TEXT('a')), TEXT("abc")), TEXT("content_root"));
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
#endif
