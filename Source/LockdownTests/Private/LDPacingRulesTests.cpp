#include "World/LDFieldPacing.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Development/LDCampGrowth.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDProgressionRules.h"
#include "Framework/LDPacingTypes.h"
#include "Misc/AutomationTest.h"
#include "World/LDLeaderNotice.h"
#include "LDTestApprovedFixture.h"

// Issue #287 spec §10-§11 (P6): the pure pacing rules on the approved content: mid-field objectives, the death screen's
// spawn choice, the 上热搜 bounty, daily modifiers, camp affixes, lockers and the second-hand dealer.
// Named (not anonymous) so unity builds cannot collide with another test file's helpers.
namespace LDPacingRulesTesting
{
LDFieldPacing::FPresence At(uint32 Id, const FVector& Foot, bool bControlled = false, bool bDamaged = false)
{
    LDFieldPacing::FPresence Out;
    Out.Contender = FGuid(0x293, 0, 0, Id);
    Out.FootCm = Foot;
    Out.bControlled = bControlled;
    Out.bDamaged = bDamaged;
    return Out;
}

TArray<LDFieldPacing::FPoint> Points(const TCHAR* Prefix, int32 Count)
{
    TArray<LDFieldPacing::FPoint> Out;
    for (int32 Index = 0; Index < Count; ++Index)
    {
        Out.Add({FName(*FString::Printf(TEXT("%s_%d"), Prefix, Index)), FVector(1000.0 * Index, -500.0 * Index, 0)});
    }
    return Out;
}

bool Approved(FAutomationTestBase& Test, TSharedPtr<const FLDDemoContentSnapshot>& Catalog)
{
    return LDTestApprovedFixture::Read(Test, Catalog) && Catalog.IsValid();
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDPacingFlagTest, "Lockdown.Rules.DemoRest.Objective.Flag",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDPacingFlagTest::RunTest(const FString& Parameters)
{
    using namespace LDPacingRulesTesting;
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!Approved(*this, Catalog)) { return false; }
    const FLDDemoContentParameters& P = Catalog->Get().Parameters;
    bool bGood = TestTrue(TEXT("Approved: at 100 s, announced 10 s ahead, a 30 s window, 200 cm, 3 s of spray"),
        P.Objective.StartMs == 100000 && P.Objective.AnnounceLeadMs == 10000 && P.Objective.FlagWindowMs == 30000
        && P.Objective.FlagSprayMs == 3000 && P.Objective.FlagRadiusCm.Numerator == 200 * P.Objective.FlagRadiusCm.Denominator);
    FLDObjectiveState State;
    FString Error;
    const TArray<LDFieldPacing::FPoint> Flags = Points(TEXT("mini_flag"), 3);
    if (!TestTrue(TEXT("The objectives begin"), State.Begin(P, Flags, FVector(5000, 5000, 0), 77, Error))) { AddError(Error); return false; }
    bGood &= TestTrue(TEXT("Segments 0 and 2 hold a mini flag, segment 1 the arena"),
        LDFieldPacing::KindOfSegment(0) == ELDObjectiveKind::MiniFlag && LDFieldPacing::KindOfSegment(1) == ELDObjectiveKind::Arena
        && LDFieldPacing::KindOfSegment(2) == ELDObjectiveKind::MiniFlag);
    bGood &= TestEqual(TEXT("The first boundary is the announcement"), State.NextDueT(0), int64(90000));
    bGood &= TestFalse(TEXT("Nothing before the announcement"), State.Advance(89999));
    bGood &= TestTrue(TEXT("The announcement draws one mini flag point"), State.Advance(90000)
        && State.View.Phase == ELDObjectivePhase::Announced && State.View.Kind == ELDObjectiveKind::MiniFlag && State.View.Serial == 1
        && Flags.ContainsByPredicate([&State](const LDFieldPacing::FPoint& Point) { return Point.Id == State.View.AnchorId; }));
    FLDObjectiveState Same;
    Same.Begin(P, Flags, FVector(5000, 5000, 0), 77, Error);
    Same.Advance(90000);
    bGood &= TestTrue(TEXT("The draw is fixed by the seed"), Same.View.AnchorId == State.View.AnchorId);
    bGood &= TestTrue(TEXT("It opens at 100 s, closing 30 s later"), State.Advance(100000) && State.View.Phase == ELDObjectivePhase::Open
        && State.View.ClosesAtT == 130000 && State.View.SprayNeedMs == 3000);
    const FVector Centre = State.View.CentreCm;
    const FVector Away = Centre + FVector(500, 0, 0);
    bool bChanged = false;
    bGood &= TestFalse(TEXT("Alone in the circle starts a spray"), State.StepFlag(100000, {At(1, Centre), At(2, Away)}, bChanged));
    bGood &= TestTrue(TEXT("The sole sprayer is public"), bChanged && State.View.Sprayer == At(1, Centre).Contender && State.View.SprayMs == 0);
    State.StepFlag(101000, {At(1, Centre), At(2, Centre)}, bChanged);
    bGood &= TestTrue(TEXT("A second contender inside contests and pauses it"), State.View.bContested && State.View.SprayMs == 0);
    State.StepFlag(102000, {At(1, Centre), At(2, Away)}, bChanged);
    bGood &= TestTrue(TEXT("Alone again it runs on"), !State.View.bContested && State.View.SprayMs == 1000);
    State.StepFlag(103000, {At(1, Centre, true), At(2, Away)}, bChanged);
    bGood &= TestTrue(TEXT("A hard control clears the spray"), !State.View.Sprayer.IsValid() && State.View.SprayMs == 0);
    State.StepFlag(104000, {At(1, Away), At(2, Centre)}, bChanged);
    bGood &= TestTrue(TEXT("Leaving hands the circle to the next sole sprayer from zero"),
        State.View.Sprayer == At(2, Centre).Contender && State.View.SprayMs == 0);
    bGood &= TestFalse(TEXT("2 s are not enough"), State.StepFlag(106000, {At(2, Centre)}, bChanged));
    bGood &= TestTrue(TEXT("3 s alone win the flag"), State.StepFlag(107000, {At(2, Centre)}, bChanged)
        && State.View.Phase == ELDObjectivePhase::Won && State.View.Winner == At(2, Centre).Contender);

    // An unsprayed flag expires with its window.
    FLDObjectiveState Idle;
    Idle.Begin(P, Flags, FVector(5000, 5000, 0), 9, Error);
    Idle.Advance(100000);
    bGood &= TestTrue(TEXT("A flag nobody sprayed expires at its close"), Idle.Advance(130000) && Idle.View.Phase == ELDObjectivePhase::Expired);
    bGood &= TestTrue(TEXT("Segment 2 draws its own flag"), Idle.Advance(2 * P.FieldSegmentMs + 90000) && Idle.View.Serial == 3
        && Idle.View.Kind == ELDObjectiveKind::MiniFlag);
    FLDObjectiveState Bad;
    bGood &= TestFalse(TEXT("No mini flag point refuses the objectives"), Bad.Begin(P, {}, FVector::ZeroVector, 1, Error) || Error.IsEmpty());
    return bGood;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDPacingArenaTest, "Lockdown.Rules.DemoRest.Objective.Arena",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDPacingArenaTest::RunTest(const FString& Parameters)
{
    using namespace LDPacingRulesTesting;
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!Approved(*this, Catalog)) { return false; }
    const FLDDemoContentParameters& P = Catalog->Get().Parameters;
    bool bGood = TestTrue(TEXT("Approved: a 40 s arena of 900 cm, its winner offered five cards"),
        P.Objective.ArenaMs == 40000 && P.Objective.ArenaRadiusCm.Numerator == 900 * P.Objective.ArenaRadiusCm.Denominator
        && LDProgression::CardOfferSize(P, ELDCardOfferSource::Arena) == 5
        && LDProgression::CardOfferSize(P, ELDCardOfferSource::MiniFlag) == 3);
    FLDObjectiveState State;
    FString Error;
    const FVector Flagpole(4000, 2000, 0);
    if (!TestTrue(TEXT("The objectives begin"), State.Begin(P, Points(TEXT("mini_flag"), 3), Flagpole, 5, Error))) { return false; }
    const int64 Open = P.FieldSegmentMs + P.Objective.StartMs;
    State.Advance(Open);
    bGood &= TestTrue(TEXT("Segment 1 opens the arena around the flagpole"), State.View.Kind == ELDObjectiveKind::Arena
        && State.View.Phase == ELDObjectivePhase::Open && State.View.CentreCm.Equals(Flagpole) && State.View.ClosesAtT == Open + 40000);
    bGood &= TestTrue(TEXT("The circle is the approved radius"), State.IsInside(Flagpole + FVector(900, 0, 0))
        && !State.IsInside(Flagpole + FVector(901, 0, 0)));
    State.NoteArenaDamage(At(1, Flagpole).Contender, 500, Open + 1000);
    State.NoteArenaDamage(At(2, Flagpole).Contender, 300, Open + 2000);
    State.NoteArenaDamage(At(2, Flagpole).Contender, 200, Open + 3000);
    State.NoteArenaDamage(At(3, Flagpole).Contender, 900, Open + 4000);
    const FVector Inside = Flagpole + FVector(100, 0, 0), Outside = Flagpole + FVector(2000, 0, 0);
    bGood &= TestEqual(TEXT("The most damage inside wins"), State.ArenaWinner({At(1, Inside), At(2, Inside), At(3, Inside)}),
        At(3, Inside).Contender);
    bGood &= TestEqual(TEXT("A tie goes to the earlier last hit"), State.ArenaWinner({At(1, Inside), At(2, Inside), At(3, Outside)}),
        At(1, Inside).Contender);
    bGood &= TestFalse(TEXT("Nobody alive inside: no winner"), State.ArenaWinner({At(1, Outside)}).IsValid());
    bGood &= TestFalse(TEXT("Damage alone without being inside never wins"), State.ArenaWinner({At(4, Inside)}).IsValid());
    return bGood;
}

// Every field step advances the objectives: a later step of the same segment keeps its objective (the arena's damage, the
// flag's spray) instead of replaying the earlier segments and drawing it anew.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDPacingObjectiveKeptTest, "Lockdown.Rules.DemoRest.Objective.KeptAcrossSteps",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDPacingObjectiveKeptTest::RunTest(const FString& Parameters)
{
    using namespace LDPacingRulesTesting;
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!Approved(*this, Catalog)) { return false; }
    const FLDDemoContentParameters& P = Catalog->Get().Parameters;
    FLDObjectiveState State;
    FString Error;
    const FVector Flagpole(4000, 2000, 0);
    if (!TestTrue(TEXT("The objectives begin"), State.Begin(P, Points(TEXT("mini_flag"), 3), Flagpole, 5, Error))) { return false; }
    bool bGood = true;
    int32 Changes = 0;
    for (int64 T = 0; T <= P.FieldSegmentMs + P.Objective.StartMs + 1000; T += 50) { Changes += State.Advance(T) ? 1 : 0; }
    const int64 Open = P.FieldSegmentMs + P.Objective.StartMs;
    // Segment 0: announced, opened, expired; segment 1: announced, opened.
    bGood &= TestEqual(TEXT("Each objective boundary changes the view once"), Changes, 5);
    bGood &= TestTrue(TEXT("The arena is open"), State.View.Serial == 2 && State.View.Phase == ELDObjectivePhase::Open);
    State.NoteArenaDamage(At(1, Flagpole).Contender, 500, Open + 1000);
    for (int64 T = Open + 1050; T < Open + P.Objective.ArenaMs; T += 50)
    {
        bGood &= TestFalse(TEXT("A later step of the arena changes nothing"), State.Advance(T));
    }
    bGood &= TestEqual(TEXT("The arena's damage is kept to its close"), State.ArenaWinner({At(1, Flagpole)}), At(1, Flagpole).Contender);
    const int64 FlagOpen = 2 * P.FieldSegmentMs + P.Objective.StartMs;
    State.Advance(FlagOpen);
    const FVector Centre = State.View.CentreCm;
    bool bChanged = false;
    bool bWon = false;
    for (int64 T = FlagOpen; T <= FlagOpen + P.Objective.FlagSprayMs && !bWon; T += 50)
    {
        bGood &= TestFalse(TEXT("A later step of the flag changes nothing"), T > FlagOpen && State.Advance(T));
        bWon = State.StepFlag(T, {At(2, Centre)}, bChanged);
    }
    bGood &= TestTrue(TEXT("A sole sprayer stepped every frame wins the flag"), bWon && State.View.Phase == ELDObjectivePhase::Won
        && State.View.Winner == At(2, Centre).Contender && State.View.Serial == 3);
    return bGood;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDPacingRespawnChoiceTest, "Lockdown.Rules.DemoRest.Respawn.Choice",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDPacingRespawnChoiceTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDPacingRulesTesting::Approved(*this, Catalog)) { return false; }
    const FLDDemoContentParameters& P = Catalog->Get().Parameters;
    bool bGood = TestTrue(TEXT("Approved: 6 s + 0.5 s per level, three candidates, 6 s to choose"),
        P.RespawnBaseMs == 6000 && P.RespawnPerLevelMs == 500 && P.SpawnChoiceCount == 3 && P.SpawnChoiceMs == 6000);
    bGood &= TestEqual(TEXT("Level 1 waits 6.5 s"), LDPacing::FieldRespawnWaitMs(P, 1, false), int64(6500));
    bGood &= TestEqual(TEXT("Level 10 waits 11 s"), LDPacing::FieldRespawnWaitMs(P, 10, false), int64(11000));
    bGood &= TestEqual(TEXT("Exam week adds its approved bonus"), LDPacing::FieldRespawnWaitMs(P, 10, true),
        int64(11000) + P.Daily.ExamRespawnBonusMs);
    const TArray<FName> Anchors = {TEXT("spawn_0"), TEXT("spawn_1"), TEXT("spawn_2"), TEXT("spawn_3"), TEXT("spawn_4"), TEXT("spawn_5")};
    TArray<FName> Drawn, Again;
    LDFieldPacing::DrawCandidates(42, 7, Anchors, P.SpawnChoiceCount, Drawn);
    LDFieldPacing::DrawCandidates(42, 7, Anchors, P.SpawnChoiceCount, Again);
    bGood &= TestTrue(TEXT("Three distinct spawn anchors, fixed by the seed"),
        Drawn.Num() == 3 && Drawn[0] != Drawn[1] && Drawn[0] != Drawn[2] && Drawn[1] != Drawn[2] && Drawn == Again
        && Anchors.Contains(Drawn[0]) && Anchors.Contains(Drawn[1]) && Anchors.Contains(Drawn[2]));
    TSet<FName> Seen;
    for (uint64 Seed = 1; Seed <= 200; ++Seed)
    {
        LDFieldPacing::DrawCandidates(Seed, 3, Anchors, 2, Drawn);
        Seen.Append(Drawn);
    }
    bGood &= TestEqual(TEXT("Every spawn anchor can be offered"), Seen.Num(), Anchors.Num());
    FLDSpawnChoice Choice;
    Choice.Candidates = {TEXT("spawn_2"), TEXT("spawn_4")};
    bGood &= TestEqual(TEXT("Without a choice the first candidate is used"), Choice.Resolved(), FName(TEXT("spawn_2")));
    Choice.Chosen = 1;
    bGood &= TestEqual(TEXT("The chosen candidate is used"), Choice.Resolved(), FName(TEXT("spawn_4")));
    bGood &= TestEqual(TEXT("The AI takes the candidate nearer a public goal"),
        LDFieldPacing::NearestCandidate({FVector(0, 0, 0), FVector(5000, 0, 0)}, {FVector(4000, 0, 0)}), 1);
    bGood &= TestEqual(TEXT("Without a goal the AI takes the first"), LDFieldPacing::NearestCandidate({FVector(0, 0, 0), FVector(5000, 0, 0)}, {}), 0);
    return bGood;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDPacingBountyTest, "Lockdown.Rules.DemoRest.Bounty",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDPacingBountyTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDPacingRulesTesting::Approved(*this, Catalog)) { return false; }
    const FLDDemoContentParameters& P = Catalog->Get().Parameters;
    bool bGood = TestTrue(TEXT("Approved: the rumor day names two"), P.Daily.RumorLeaders == 2);
    using FCandidate = LDLeaderNotice::FCandidate;
    const auto Candidate = [](uint32 Id, uint8 Level, uint64 Kills)
    {
        FCandidate Out;
        Out.Contender = FGuid(0x293, 1, 0, Id);
        Out.Level = Level;
        Out.Kills = Kills;
        Out.bAlive = true;
        return Out;
    };
    const TArray<FCandidate> Room = {Candidate(1, 6, 0), Candidate(2, 8, 1), Candidate(3, 7, 4), Candidate(4, 7, 2), Candidate(5, 3, 9)};
    TArray<FGuid> Top;
    LDLeaderNotice::PickLeaders(5, 2, Room, Top);
    bGood &= TestTrue(TEXT("Two leaders by level, then kills"), Top.Num() == 2 && Top[0] == Room[1].Contender && Top[1] == Room[2].Contender);
    const TArray<FCandidate> Tied = {Candidate(1, 8, 1), Candidate(2, 7, 3), Candidate(3, 7, 3)};
    LDLeaderNotice::PickLeaders(5, 2, Tied, Top);
    bGood &= TestTrue(TEXT("A tie across the cut admits none of the tied group"), Top.Num() == 1 && Top[0] == Tied[0].Contender);
    LDLeaderNotice::PickLeaders(9, 2, Room, Top);
    bGood &= TestTrue(TEXT("Nobody at the minimum level names nobody"), Top.IsEmpty());

    FLDLeaderState Notice;
    Notice.Rules.StartedAtMs = 0;
    Notice.Rules.RefreshMs = P.LeaderRefreshMs;
    Notice.Rules.MinLevel = 5;
    Notice.Rules.CircleRadiusCm = 1200;
    Notice.Rules.CircleMaxOffsetCm = 800;
    Notice.Rules.Seed = 3;
    Notice.Rules.Leaders = 2;
    bGood &= TestTrue(TEXT("The rumor-day notice is valid"), Notice.Rules.IsValid());
    Notice.Refresh(P.LeaderRefreshMs, Room);
    bGood &= TestTrue(TEXT("Both named contenders carry the bounty"), Notice.IsTrending(Room[1].Contender) && Notice.IsTrending(Room[2].Contender)
        && !Notice.IsTrending(Room[3].Contender) && !Notice.IsTrending(FGuid()));
    Notice.LeaderDied(Room[1].Contender, P.LeaderRefreshMs + 100);
    bGood &= TestFalse(TEXT("A trending contender's death ends its bounty until the next refresh"), Notice.IsTrending(Room[1].Contender));
    return bGood;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDPacingDailyTest, "Lockdown.Rules.DemoRest.DailyModifiers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDPacingDailyTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDPacingRulesTesting::Approved(*this, Catalog)) { return false; }
    const FLDDemoContentParameters& P = Catalog->Get().Parameters;
    bool bGood = TestEqual(TEXT("Approved: two hotspots a match"), P.Daily.Count, 2);
    TArray<ELDDailyModifier> Day, Again;
    LDPacing::DrawDailyModifiers(11, P.Daily.Count, Day);
    LDPacing::DrawDailyModifiers(11, P.Daily.Count, Again);
    bGood &= TestTrue(TEXT("Two distinct sorted hotspots, fixed by the seed"), Day.Num() == 2 && Day[0] < Day[1] && Day == Again);
    TSet<ELDDailyModifier> Seen;
    for (uint64 Seed = 1; Seed <= 400; ++Seed)
    {
        LDPacing::DrawDailyModifiers(Seed, P.Daily.Count, Day);
        Seen.Append(Day);
    }
    bGood &= TestEqual(TEXT("Every one of the twelve can be drawn"), Seen.Num(), LDPacing::DailyModifiers);
    for (int32 Id = 1; Id <= LDPacing::DailyModifiers; ++Id)
    {
        const ELDDailyModifier Modifier = ELDDailyModifier(Id);
        bGood &= TestEqual(*FString::Printf(TEXT("Hotspot %d round-trips its id"), Id), LDPacing::DailyFromId(LDPacing::DailyId(Modifier)), Modifier);
    }
    bGood &= TestEqual(TEXT("An unknown id is none"), LDPacing::DailyFromId(TEXT("dm_unknown")), ELDDailyModifier::None);
    bGood &= TestTrue(TEXT("The sale halves prices, rounded up, never below one"), LDPacing::SalePrice(P, 25, true) == 13
        && LDPacing::SalePrice(P, 6, true) == 3 && LDPacing::SalePrice(P, 1, true) == 1 && LDPacing::SalePrice(P, 25, false) == 25);
    bGood &= TestTrue(TEXT("Approved delivery day: twelve drops, each lying 15 s"),
        P.Daily.DeliveryLandMs.Num() == 12 && P.Daily.DeliveryExpireMs == 15000);
    bGood &= TestTrue(TEXT("Approved patrol day: the Boss appears at 120 s"), P.Daily.PatrolBossAppearMs == 120000);
    return bGood;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDPacingAffixTest, "Lockdown.Rules.DemoRest.CampAffixes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDPacingAffixTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDPacingRulesTesting::Approved(*this, Catalog)) { return false; }
    const FLDDemoContentParameters& P = Catalog->Get().Parameters;
    bool bGood = TestEqual(TEXT("Approved: 30 % of eligible camp spawns carry an affix"), P.Affix.ChanceBp, int64(3000));
    bGood &= TestEqual(TEXT("A Low camp never carries one"), LDPacing::RollAffix(P, true, false, 0, 0), ELDCampAffix::None);
    bGood &= TestEqual(TEXT("Overtime day puts Overtime on every camp, Low ones too"), LDPacing::RollAffix(P, true, true, 9999, 5),
        ELDCampAffix::Overtime);
    bGood &= TestEqual(TEXT("A roll at the chance carries none"), LDPacing::RollAffix(P, false, false, P.Affix.ChanceBp, 0), ELDCampAffix::None);
    for (int32 Pick = 0; Pick < LDPacing::CampAffixes; ++Pick)
    {
        bGood &= TestEqual(*FString::Printf(TEXT("A roll under the chance takes affix %d"), Pick + 1),
            LDPacing::RollAffix(P, false, false, P.Affix.ChanceBp - 1, Pick), ELDCampAffix(Pick + 1));
    }
    bGood &= TestTrue(TEXT("Approved affix rewards: fridge offers three, bonus five, audit at most one point"),
        P.Affix.FridgeCardOffer == 3 && P.Affix.BonusCardOffer == 5 && P.Affix.AuditSkillPointsMax == 1
        && LDProgression::CardOfferSize(P, ELDCardOfferSource::CampFridge) == 3
        && LDProgression::CardOfferSize(P, ELDCardOfferSource::CampBonus) == 5);
    // Overtime scales an exact attack speed; one with a huge denominator must not overflow when the reduced result fits.
    FLDDemoMonsterDefinition Fast;
    Fast.AttackSpeed.Numerator = 1000000000000001;
    Fast.AttackSpeed.Denominator = 1000000000000000;
    Fast.AttackIntervalMs = 1000;
    Fast.AttackWindupMs = 100;
    FString Error;
    const int64 Bp = 10000 + P.Affix.OvertimeAttackSpeedBp;
    bGood &= TestTrue(TEXT("Overtime on an exact huge-denominator speed applies: ") + Error,
        LDCampGrowth::ApplySpawnTerms(P, ELDCampAffix::Overtime, false, Fast, Error));
    int64 G = Bp;
    for (int64 B = 10000; B != 0;) { const int64 T = B; B = G % B; G = T; }
    bGood &= TestTrue(TEXT("Overtime speed is the exact reduced product"),
        Fast.AttackSpeed.Numerator == 1000000000000001 * (Bp / G)
        && Fast.AttackSpeed.Denominator == 1000000000000000 * (10000 / G));
    bGood &= TestEqual(TEXT("Overtime interval is the rounded-up quotient"), Fast.AttackIntervalMs, (int64(1000) * 10000 + Bp - 1) / Bp);
    return bGood;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDPacingLockerTest, "Lockdown.Rules.DemoRest.Explore.Locker",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDPacingLockerTest::RunTest(const FString& Parameters)
{
    using namespace LDPacingRulesTesting;
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!Approved(*this, Catalog)) { return false; }
    const FLDDemoContentParameters& P = Catalog->Get().Parameters;
    bool bGood = TestTrue(TEXT("Approved: four lockers, 150 cm, a 3 s channel, a three-card offer, out 45 s within 30..420 s"),
        P.Explore.LockerCount == 4 && P.Explore.LockerChannelMs == 3000 && P.Explore.LockerCardOffer == 3
        && P.Explore.LockerRadiusCm.Numerator == 150 * P.Explore.LockerRadiusCm.Denominator
        && P.Explore.LockerEarliestMs == 30000 && P.Explore.LockerLatestMs == 420000 && P.Explore.LockerStayMs == 45000);
    FLDLockerState State;
    FString Error;
    const TArray<LDFieldPacing::FPoint> All = Points(TEXT("locker"), 10);
    if (!TestTrue(TEXT("Lockers stock"), State.Begin(P, All, 21, Error))) { AddError(Error); return false; }
    bGood &= TestEqual(TEXT("Four of the ten points are stocked"), State.Lockers.Num(), 4);
    for (const FLDLockerState::FLocker& Locker : State.Lockers)
    {
        bGood &= TestTrue(TEXT("Each comes out inside the window and stays its time"), Locker.AppearT >= P.Explore.LockerEarliestMs
            && Locker.AppearT <= P.Explore.LockerLatestMs && Locker.LeaveT == Locker.AppearT + P.Explore.LockerStayMs);
    }
    FLDLockerState Few;
    bGood &= TestFalse(TEXT("Fewer points than stocked lockers is refused"), Few.Begin(P, Points(TEXT("locker"), 3), 21, Error));
    const FLDLockerState::FLocker& First = State.Lockers[0];
    const int64 Out = First.AppearT;
    const FVector Spot = First.Point.PositionCm;
    TArray<TPair<int32, FGuid>> Opened;
    bool bChanged = false;
    TArray<FLDLockerView> View;
    State.Step(Out - 1, {At(1, Spot)}, Opened, bChanged);
    State.View(Out - 1, View);
    bGood &= TestTrue(TEXT("Before it comes out nobody channels it and it is not public"), !State.Lockers[0].Opener.IsValid()
        && !View.ContainsByPredicate([&](const FLDLockerView& Row) { return Row.AnchorId == First.Point.Id; }));
    bGood &= TestTrue(TEXT("Its coming out is a due instant"), State.NextDueT(Out - 1) <= Out);
    State.Step(Out, {At(2, Spot), At(1, Spot)}, Opened, bChanged);
    State.View(Out, View);
    bGood &= TestTrue(TEXT("Out, it is public and the smallest id inside starts the channel"), bChanged
        && State.Lockers[0].Opener == At(1, Spot).Contender
        && View.ContainsByPredicate([&](const FLDLockerView& Row) { return Row.AnchorId == First.Point.Id && Row.bChanneling; })
        && State.NextDueT(Out) == Out + 3000);
    State.Step(Out + 1000, {At(1, Spot, false, true), At(2, Spot)}, Opened, bChanged);
    bGood &= TestTrue(TEXT("Damage interrupts it and the next one inside starts over"), State.Lockers[0].Opener == At(2, Spot).Contender
        && State.Lockers[0].ChannelSinceT == Out + 1000);
    State.Step(Out + 3999, {At(2, Spot)}, Opened, bChanged);
    bGood &= TestTrue(TEXT("Not yet open before 3 s"), Opened.IsEmpty() && !State.Lockers[0].bOpened);
    State.Step(Out + 4000, {At(2, Spot)}, Opened, bChanged);
    bGood &= TestTrue(TEXT("3 s of channel open it for its opener"), Opened.Num() == 1 && Opened[0].Key == 0
        && Opened[0].Value == At(2, Spot).Contender && State.Lockers[0].bOpened);
    State.Step(Out + 8000, {At(1, Spot)}, Opened, bChanged);
    bGood &= TestTrue(TEXT("An opened locker never opens again"), Opened.IsEmpty() && !State.Lockers[0].Opener.IsValid());

    FLDLockerState Late;
    Late.Begin(P, All, 21, Error);
    const int64 Leave = Late.Lockers[0].LeaveT;
    Late.Step(Leave - 1000, {At(1, Late.Lockers[0].Point.PositionCm)}, Opened, bChanged);
    Late.Step(Leave, {At(1, Late.Lockers[0].Point.PositionCm)}, Opened, bChanged);
    Late.View(Leave, View);
    bGood &= TestTrue(TEXT("A locker nobody finished leaves at its time and drops the channel"), bChanged && Opened.IsEmpty()
        && !Late.Lockers[0].Opener.IsValid() && !Late.Lockers[0].bOpened
        && !View.ContainsByPredicate([&](const FLDLockerView& Row) { return Row.AnchorId == Late.Lockers[0].Point.Id; }));
    return bGood;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDPacingPeddlerTest, "Lockdown.Rules.DemoRest.Explore.Peddler",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDPacingPeddlerTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDPacingRulesTesting::Approved(*this, Catalog)) { return false; }
    const FLDDemoContentParameters& P = Catalog->Get().Parameters;
    bool bGood = TestTrue(TEXT("Approved: appears between 120 s and 400 s, stays 60 s, offers five cards"),
        P.Explore.PeddlerEarliestMs == 120000 && P.Explore.PeddlerLatestMs == 400000 && P.Explore.PeddlerStayMs == 60000
        && LDProgression::CardOfferSize(P, ELDCardOfferSource::Peddler) == 5);
    const TArray<FName> Camps = {TEXT("camp_a"), TEXT("camp_b"), TEXT("camp_c")};
    FString Error;
    TSet<FName> Where;
    int64 Earliest = MAX_int64, Latest = 0;
    for (uint64 Seed = 1; Seed <= 300; ++Seed)
    {
        FLDPeddlerPlan Plan;
        if (!Plan.Plan(P, Camps, Seed, Error)) { AddError(Error); return false; }
        bGood &= Plan.AppearT >= P.Explore.PeddlerEarliestMs && Plan.AppearT <= P.Explore.PeddlerLatestMs
            && Plan.LeaveT == Plan.AppearT + P.Explore.PeddlerStayMs && Camps.Contains(Plan.CampAnchor) && !Plan.bSpawned && !Plan.bGone;
        Where.Add(Plan.CampAnchor);
        Earliest = FMath::Min(Earliest, Plan.AppearT);
        Latest = FMath::Max(Latest, Plan.AppearT);
    }
    bGood &= TestTrue(TEXT("Every plan lies in the window, stays its time and stands beside a camp"), bGood);
    bGood &= TestTrue(TEXT("The time and camp are spread"), Where.Num() == Camps.Num() && Latest - Earliest > 200000);
    FLDPeddlerPlan A, B;
    A.Plan(P, Camps, 8, Error);
    B.Plan(P, Camps, 8, Error);
    bGood &= TestTrue(TEXT("The plan is fixed by the seed"), A.AppearT == B.AppearT && A.CampAnchor == B.CampAnchor);
    FLDPeddlerPlan None;
    bGood &= TestFalse(TEXT("Without camps there is no dealer"), None.Plan(P, {}, 8, Error));
    return bGood;
}

#endif
