#include "Combat/LDAssistRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Framework/LDCaptureTypes.h"
#include "Misc/AutomationTest.h"
#include "UI/LDHUDModel.h"
#include "UI/LDTerms.h"

// Named scope keeps these fixtures unique inside unity translation units.
namespace LDAssistRulesTests
{
const FGuid Killer(0xA5, 0, 0, 0x10), A1(0xA5, 0, 0, 0x20), A2(0xA5, 0, 0, 0x30), Victim(0xA5, 0, 0, 0x40), Gone(0xA5, 0, 0, 0x50);

// #112 §8 eligibility: a contribution within 15 s (inclusive) before the death, refreshed by every new one; never the
// killer or the victim; a contender who left earns none; a dead one still does; no killer, no assisters.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoAssistContributionsTest, "Lockdown.Rules.DemoAssist.Contributions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDemoAssistContributionsTest::RunTest(const FString& Parameters)
{
    TArray<FLDAssistContribution> Rows;
    TestTrue(TEXT("A first contribution is recorded"), LDAssist::RecordContribution(Rows, A1, Victim, 1000));
    TestFalse(TEXT("The victim never contributes to himself"), LDAssist::RecordContribution(Rows, Victim, Victim, 1000));
    TestFalse(TEXT("An invalid contributor is ignored"), LDAssist::RecordContribution(Rows, FGuid(), Victim, 1000));
    TestTrue(TEXT("A new contribution restarts the window"), LDAssist::RecordContribution(Rows, A1, Victim, 9000)
        && Rows.Num() == 1 && Rows[0].LastAtMs == 9000);
    TestFalse(TEXT("An older or same-instant contact never moves the window back"), LDAssist::RecordContribution(Rows, A1, Victim, 9000));
    LDAssist::RecordContribution(Rows, A2, Victim, 2000);
    LDAssist::RecordContribution(Rows, Killer, Victim, 20000);
    LDAssist::RecordContribution(Rows, Gone, Victim, 20000);

    const auto Present = [](const FGuid& Id) { return Id != Gone; };
    TArray<FGuid> Out;
    LDAssist::SelectAssisters(Rows, Killer, Victim, 24000, 15000, Present, Out);
    TestTrue(TEXT("Exactly 15 s after the refreshed contribution still counts; 22 s does not; killer and leaver never do"),
        Out.Num() == 1 && Out[0] == A1);
    LDAssist::SelectAssisters(Rows, Killer, Victim, 24001, 15000, Present, Out);
    TestTrue(TEXT("One millisecond past the window drops the assist"), Out.IsEmpty());
    LDAssist::SelectAssisters(Rows, Killer, Victim, 16000, 15000, Present, Out);
    TestTrue(TEXT("Assisters are sorted"), Out.Num() == 2 && Out[0] == A1 && Out[1] == A2);
    LDAssist::SelectAssisters(Rows, Killer, Victim, 16000, 15000, [](const FGuid&) { return true; }, Out);
    TestTrue(TEXT("Contributions recorded after the death never count"), !Out.Contains(Gone) && Out.Num() == 2);
    LDAssist::SelectAssisters(Rows, A2, Victim, 20000, 15000, Present, Out);
    TestTrue(TEXT("Another killer turns the former killer into an assister"), Out.Num() == 2 && Out.Contains(A1) && Out.Contains(Killer));
    LDAssist::SelectAssisters(Rows, FGuid(), Victim, 16000, 15000, Present, Out);
    TestTrue(TEXT("A death without a killer has no assisters"), Out.IsEmpty());
    return true;
}

// #112 §8 rewards: the kill XP is split evenly among the killer and the assisters; every share is floored.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoAssistRewardSplitTest, "Lockdown.Rules.DemoAssist.RewardSplit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDemoAssistRewardSplitTest::RunTest(const FString& Parameters)
{
    FLDAssistRewardShares Shares;
    TestTrue(TEXT("Without assisters the killer takes everything"), LDAssist::SplitKillReward(100, 0, Shares)
        && Shares.KillerXP == 100 && Shares.AssisterXP == 0);
    TestTrue(TEXT("Two assisters: 100 XP split 33 each"),
        LDAssist::SplitKillReward(100, 2, Shares) && Shares.KillerXP == 33 && Shares.AssisterXP == 33);
    TestTrue(TEXT("Three assisters: 120 XP -> 30 each"),
        LDAssist::SplitKillReward(120, 3, Shares) && Shares.KillerXP == 30 && Shares.AssisterXP == 30);
    TestTrue(TEXT("A small amount floors every share: 7 XP among five -> 1 each"),
        LDAssist::SplitKillReward(7, 4, Shares) && Shares.KillerXP == 1 && Shares.AssisterXP == 1);
    TestFalse(TEXT("A negative amount is refused"), LDAssist::SplitKillReward(-1, 1, Shares));
    TestFalse(TEXT("A negative assister count is refused"), LDAssist::SplitKillReward(10, -1, Shares));
    return true;
}

// The public kill notice lists the assisters; the feed line and the result row show them.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoAssistKillFeedTest, "Lockdown.Rules.DemoAssist.KillFeedAndResult",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDemoAssistKillFeedTest::RunTest(const FString& Parameters)
{
    FLDKillFeedView Feed;
    Feed.Generation = 1;
    FLDKillNotice& Notice = Feed.Notices.AddDefaulted_GetRef();
    Notice.Serial = 1;
    Notice.Victim = Victim;
    Notice.Killer = Killer;
    Notice.Cause = ELDKillCause::Contender;
    Notice.Assisters = {A1, A2};
    FString Error;
    TestTrue(TEXT("A contender kill with sorted assisters is a valid notice"), LDCapture::ValidateKillFeed(Feed, Error));
    const auto Refused = [&](const TCHAR* What, TFunctionRef<void(FLDKillNotice&)> Change)
    {
        FLDKillFeedView Bad = Feed;
        Change(Bad.Notices[0]);
        TestFalse(What, LDCapture::ValidateKillFeed(Bad, Error));
    };
    Refused(TEXT("Unsorted assisters are refused"), [](FLDKillNotice& N) { N.Assisters = {A2, A1}; });
    Refused(TEXT("The killer is never an assister"), [](FLDKillNotice& N) { N.Assisters = {Killer}; });
    Refused(TEXT("The victim is never an assister"), [](FLDKillNotice& N) { N.Assisters = {Victim}; });
    Refused(TEXT("A camp kill lists no assisters"), [](FLDKillNotice& N) { N.Cause = ELDKillCause::CampLow; N.Killer.Invalidate(); });
    Refused(TEXT("At most four assisters among six contenders"), [](FLDKillNotice& N)
    { N.Assisters = {FGuid(1, 0, 0, 1), FGuid(1, 0, 0, 2), FGuid(1, 0, 0, 3), FGuid(1, 0, 0, 4), FGuid(1, 0, 0, 5)}; });

    const TArray<FString> Names = {TEXT("阿强"), TEXT("阿珍")};
    TestEqual(TEXT("The kill line lists the assisters after it"), LDHUD::KillLine(Notice, TEXT("小明"), TEXT("小红"), TEXT("低"), TEXT("高"), TEXT("精英"), TEXT("老板"), Names),
        LDTerms::KillLine(TEXT("小红"), TEXT("小明")) + TEXT("（助攻：阿强、阿珍）"));
    FLDKillNotice Plain = Notice;
    Plain.Assisters.Reset();
    TestEqual(TEXT("A kill without assisters keeps the plain line"), LDHUD::KillLine(Plain, TEXT("小明"), TEXT("小红"), TEXT("低"), TEXT("高"), TEXT("精英"), TEXT("老板")),
        LDTerms::KillLine(TEXT("小红"), TEXT("小明")));
    const LDHUD::FKillFeedLineView Own = LDHUD::KillFeedLine(Notice, A2, TEXT("小明"), TEXT("小红"), TEXT("低"), TEXT("高"), TEXT("精英"), TEXT("老板"), Names);
    TestTrue(TEXT("The player's own assist says 你 and highlights the line"),
        Own.bSelf && Own.Text == LDTerms::KillLine(TEXT("小红"), TEXT("小明")) + TEXT("（助攻：阿强、你）"));
    const LDHUD::FKillFeedLineView Other = LDHUD::KillFeedLine(Notice, Gone, TEXT("小明"), TEXT("小红"), TEXT("低"), TEXT("高"), TEXT("精英"), TEXT("老板"), Names);
    TestFalse(TEXT("A bystander's feed line is not highlighted"), Other.bSelf);

    FLDResultRow Row;
    Row.ContenderId = A1;
    Row.Name = TEXT("阿强");
    Row.Kills = 2;
    Row.Deaths = 1;
    Row.Assists = 4;
    Row.FinalLevel = 6;
    const LDHUD::FResultRowView Shown = LDHUD::ResultRow(Row, FGuid(), FGuid());
    TestTrue(TEXT("The result row shows kills, deaths, assists and the final level"),
        Shown.Kills == TEXT("2") && Shown.Deaths == TEXT("1") && Shown.Assists == TEXT("4") && Shown.Level == TEXT("6"));
    return true;
}
} // namespace LDAssistRulesTests

#endif
