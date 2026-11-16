#include "Combat/LDDamageRecap.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Framework/LDCaptureTypes.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "Misc/AutomationTest.h"
#include "UI/LDHUDModel.h"
#include "UI/LDTerms.h"
#include "Vision/LDViewTypes.h"

// Named scope keeps these fixtures unique inside unity translation units.
namespace LDDamageRecapTests
{
const FGuid Killer(0xD4, 0, 0, 0x10), Helper(0xD4, 0, 0, 0x20), Third(0xD4, 0, 0, 0x30), Fourth(0xD4, 0, 0, 0x40), Fifth(0xD4, 0, 0, 0x50);

FLDCombatSourceRef Source(ELDCombatSourceKind Kind, const TCHAR* Id = nullptr)
{
    FLDCombatSourceRef Ref;
    Ref.Kind = Kind;
    if (Id) { Ref.DefinitionId = FName(Id); }
    return Ref;
}

FLDDamageLine Line(const FGuid& Contender, const FLDCombatSourceRef& Ref, int64 DamageP2, const TCHAR* Monster = nullptr)
{
    FLDDamageLine Out;
    Out.Contender = Contender;
    if (Monster) { Out.MonsterId = FName(Monster); }
    Out.Source = Ref;
    Out.DamageP2 = DamageP2;
    return Out;
}

// The death recap sums what one life took inside the assist window (inclusive at its edge), per attacker and source:
// attackers by their total, each attacker's sources by amount, ties in a fixed order. Older samples are dropped.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDamageRecapWindowTest, "Lockdown.Rules.DamageRecap.Window",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDamageRecapWindowTest::RunTest(const FString& Parameters)
{
    const FLDCombatSourceRef Basic = Source(ELDCombatSourceKind::Basic);
    const FLDCombatSourceRef Bolt = Source(ELDCombatSourceKind::Learned, TEXT("ability_bolt"));
    const FLDCombatSourceRef Thorns = Source(ELDCombatSourceKind::Component, TEXT("component_thorns"));
    TArray<FLDDamageSample> Log;
    TestFalse(TEXT("A hit without positive damage is not a sample"), LDDamageRecap::Record(Log, Line(Killer, Basic, 0), 1000, 15000));
    TestFalse(TEXT("No window, no sample"), LDDamageRecap::Record(Log, Line(Killer, Basic, 500), 1000, 0));
    TestTrue(TEXT("A positive hit is recorded"), LDDamageRecap::Record(Log, Line(Helper, Basic, 4000), 1000, 15000));
    LDDamageRecap::Record(Log, Line(Helper, Bolt, 9000), 9000, 15000);
    LDDamageRecap::Record(Log, Line(Killer, Bolt, 3000), 9000, 15000);
    LDDamageRecap::Record(Log, Line(FGuid(), Basic, 2500, TEXT("monster_low")), 12000, 15000);
    LDDamageRecap::Record(Log, Line(Killer, Basic, 1000), 16000, 15000);
    TestEqual(TEXT("A sample exactly at the window edge is kept"), Log.Num(), 5);
    LDDamageRecap::Record(Log, Line(Killer, Basic, 2000), 16001, 15000);
    TestTrue(TEXT("A sample one millisecond past the window is dropped"), Log.Num() == 5 && Log[0].AtMs == 9000);
    LDDamageRecap::Record(Log, Line(Killer, Thorns, 2000), 20000, 15000);

    TArray<FLDDamageLine> Out;
    LDDamageRecap::Summarize(Log, 24000, 15000, Out);
    // Helper 9000 (bolt); Killer 3000 bolt + 3000 basic + 2000 thorns = 8000; monster 2500.
    TestEqual(TEXT("One line per attacker and source"), Out.Num(), 5);
    TestTrue(TEXT("The attacker with the largest total comes first"), Out.Num() == 5 && Out[0].Contender == Helper && Out[0].DamageP2 == 9000);
    TestTrue(TEXT("Equal sources of one attacker keep the source order (basic before learned)"),
        Out.Num() == 5 && Out[1].Contender == Killer && Out[1].Source == Basic && Out[1].DamageP2 == 3000
        && Out[2].Contender == Killer && Out[2].Source == Bolt && Out[2].DamageP2 == 3000
        && Out[3].Contender == Killer && Out[3].Source == Thorns && Out[3].DamageP2 == 2000);
    TestTrue(TEXT("A camp attacker is its own group"), Out.Num() == 5 && !Out[4].Contender.IsValid()
        && Out[4].MonsterId == FName(TEXT("monster_low")) && Out[4].DamageP2 == 2500);
    LDDamageRecap::Summarize(Log, 24001, 15000, Out);
    TestTrue(TEXT("The window is counted back from the fatal hit"), Out.Num() == 3 && Out[0].Contender == Killer && Out[0].Source == Basic && Out[0].DamageP2 == 3000);
    LDDamageRecap::Summarize(Log, 10000, 15000, Out);
    TestTrue(TEXT("Hits after the death never count"), Out.Num() == 2 && Out[0].Contender == Helper && Out[1].Contender == Killer);
    LDDamageRecap::Summarize(Log, 24000, 0, Out);
    TestTrue(TEXT("No window, no recap"), Out.IsEmpty());

    TArray<FLDDamageLine> Totals;
    TestFalse(TEXT("A total ignores a non-positive amount"), LDDamageRecap::AddTotal(Totals, Basic, 0));
    LDDamageRecap::AddTotal(Totals, Basic, 1500);
    LDDamageRecap::AddTotal(Totals, Bolt, 4000);
    LDDamageRecap::AddTotal(Totals, Basic, 2500);
    LDDamageRecap::AddTotal(Totals, Thorns, 4000);
    LDDamageRecap::SortTotals(Totals);
    TestTrue(TEXT("Totals add up per source and sort largest first, ties by source"), Totals.Num() == 3
        && Totals[0].Source == Basic && Totals[0].DamageP2 == 4000 && Totals[1].Source == Bolt && Totals[2].Source == Thorns);

    FString Error;
    TestTrue(TEXT("A summarized recap is a valid list"), LDDamageRecap::ValidateLines(Totals, Error));
    TArray<FLDDamageLine> Bad = Totals;
    Bad.Add(Totals[0]);
    TestFalse(TEXT("A repeated attacker and source is refused"), LDDamageRecap::ValidateLines(Bad, Error));
    Bad = Totals;
    Bad[1].DamageP2 = 0;
    TestFalse(TEXT("A line without a positive amount is refused"), LDDamageRecap::ValidateLines(Bad, Error));
    Bad.Reset();
    for (int32 Index = 0; Index <= LDDamageRecap::MaxLines; ++Index) { Bad.Add(Line(FGuid(0xD5, 0, 0, uint32(Index + 1)), Basic, 100)); }
    TestFalse(TEXT("A list above the transport bound is refused"), LDDamageRecap::ValidateLines(Bad, Error));
    return true;
}

// The death banner shows only the recap of exactly the dead life; the result page only the summary of exactly the result
// shown. Both stay inside their fixed row counts: what does not fit is summed into one 其他 entry.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDamageRecapDisplayTest, "Lockdown.Rules.DamageRecap.Display",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDamageRecapDisplayTest::RunTest(const FString& Parameters)
{
    const FLDCombatSourceRef Basic = Source(ELDCombatSourceKind::Basic);
    const FLDCombatSourceRef Dash = Source(ELDCombatSourceKind::Innate, TEXT("innate_dash"));
    const FLDCombatSourceRef Bolt = Source(ELDCombatSourceKind::Learned, TEXT("ability_bolt"));
    const FLDCombatSourceRef Wave = Source(ELDCombatSourceKind::Learned, TEXT("ability_wave"));
    const FLDCombatSourceRef Thorns = Source(ELDCombatSourceKind::Component, TEXT("component_thorns"));
    TestEqual(TEXT("A basic reads the basic attack term"), LDHUD::DamageSourceName(nullptr, Basic), FString(LDTerms::BasicAttack));
    TestEqual(TEXT("The innate dash reads the dash term"), LDHUD::DamageSourceName(nullptr, Dash), FString(LDTerms::Dash));
    TestEqual(TEXT("Without display text an ability reads its id, never another name"), LDHUD::DamageSourceName(nullptr, Bolt), FString(TEXT("ability_bolt")));
    TestEqual(TEXT("A source without identity is named as such"), LDHUD::DamageSourceName(nullptr, FLDCombatSourceRef()), FString(TEXT("其他来源")));

    FLDLobbyView Lobby;
    const auto Member = [&Lobby](const FGuid& Id, const TCHAR* Name)
    {
        FLDLobbyMember& Row = Lobby.Members.AddDefaulted_GetRef();
        Row.ContenderId = Id;
        Row.Name = Name;
    };
    Member(Killer, TEXT("小红"));
    Member(Helper, TEXT("阿强"));
    FLDOwnerState Own;
    Own.Match = 3;
    Own.Life = 2;
    Own.LifePhase = ELDLifePhase::Dead;
    FLDDeathRecapView Recap;
    Recap.Match = 3;
    Recap.Life = 2;
    Recap.WindowMs = 15000;
    Recap.Lines = {Line(Killer, Bolt, 30050), Line(Killer, Basic, 12000), Line(Killer, Wave, 5000), Line(Killer, Thorns, 2501),
        Line(Helper, Basic, 8000), Line(FGuid(), Basic, 4000, TEXT("monster_low"))};
    LDHUD::FDamageListView View = LDHUD::DeathRecap(Recap, Own, Lobby, nullptr);
    TestTrue(TEXT("The recap of the dead life is shown with the window and the total"),
        View.bAvailable && View.Heading == TEXT("近 15 秒承受伤害 616") && View.Rows.Num() == 3);
    TestTrue(TEXT("An attacker row names it, its total and its largest sources; the rest is one entry"), View.Rows.Num() == 3
        && View.Rows[0] == FString::Printf(TEXT("小红 496 · ability_bolt 301 · %s 120 · 其他 76"), LDTerms::BasicAttack));
    TestTrue(TEXT("A camp attacker reads its definition"), View.Rows.Num() == 3
        && View.Rows[1] == FString::Printf(TEXT("阿强 80 · %s 80"), LDTerms::BasicAttack)
        && View.Rows[2] == FString::Printf(TEXT("monster_low 40 · %s 40"), LDTerms::BasicAttack));

    FLDOwnerState Alive = Own;
    Alive.LifePhase = ELDLifePhase::Alive;
    TestFalse(TEXT("A living player has no recap"), LDHUD::DeathRecap(Recap, Alive, Lobby, nullptr).bAvailable);
    FLDOwnerState NextLife = Own;
    NextLife.Life = 3;
    TestFalse(TEXT("An earlier life's recap is never shown for a later death"), LDHUD::DeathRecap(Recap, NextLife, Lobby, nullptr).bAvailable);
    FLDOwnerState NextMatch = Own;
    NextMatch.Match = 4;
    TestFalse(TEXT("Another match's recap is never shown"), LDHUD::DeathRecap(Recap, NextMatch, Lobby, nullptr).bAvailable);
    TestFalse(TEXT("No recap delivered, nothing shown"), LDHUD::DeathRecap(FLDDeathRecapView(), Own, Lobby, nullptr).bAvailable);

    FLDDeathRecapView Quiet = Recap;
    Quiet.Lines.Reset();
    View = LDHUD::DeathRecap(Quiet, Own, Lobby, nullptr);
    TestTrue(TEXT("A death without hostile damage says so and lists nothing"),
        View.bAvailable && View.Rows.IsEmpty() && View.Heading == TEXT("近 15 秒没有受到其他单位的伤害"));

    FLDDeathRecapView Crowd = Recap;
    Crowd.Lines = {Line(Killer, Basic, 5000), Line(Helper, Basic, 4000), Line(Third, Basic, 3000), Line(Fourth, Basic, 2000), Line(Fifth, Basic, 1000)};
    View = LDHUD::DeathRecap(Crowd, Own, Lobby, nullptr);
    TestTrue(TEXT("More attackers than rows: the last row sums the rest"), View.Rows.Num() == LDHUD::DeathRecapAttackersMax
        && View.Rows.Last() == TEXT("其他 2 个攻击者 30") && View.Rows[2].StartsWith(TEXT("未列名参赛者 30")));

    FLDResultView Result;
    Result.Generation = 3;
    Result.ResultId = FGuid(0xD6, 0, 0, 1);
    FLDDamageSummaryView Summary;
    Summary.Match = 3;
    Summary.ResultId = Result.ResultId;
    Summary.Lines = {Line(FGuid(), Bolt, 120000), Line(FGuid(), Basic, 45001)};
    View = LDHUD::DamageSummary(Summary, Result, nullptr);
    TestTrue(TEXT("The own damage of the shown result lists each source"), View.bAvailable && View.Heading == TEXT("你造成的伤害 1651")
        && View.Rows.Num() == 2 && View.Rows[0] == TEXT("ability_bolt 1200") && View.Rows[1] == FString::Printf(TEXT("%s 451"), LDTerms::BasicAttack));
    FLDResultView Other = Result;
    Other.ResultId = FGuid(0xD6, 0, 0, 2);
    TestFalse(TEXT("Another result's summary is not available"), LDHUD::DamageSummary(Summary, Other, nullptr).bAvailable);
    TestFalse(TEXT("No summary delivered: not available, never zeros"), LDHUD::DamageSummary(FLDDamageSummaryView(), Result, nullptr).bAvailable);
    FLDDamageSummaryView None = Summary;
    None.Lines.Reset();
    View = LDHUD::DamageSummary(None, Result, nullptr);
    TestTrue(TEXT("A delivered empty summary says no damage was dealt"), View.bAvailable && View.Rows.IsEmpty() && View.Heading == TEXT("你本局没有造成伤害"));
    FLDDamageSummaryView Many = Summary;
    Many.Lines.Reset();
    for (int32 Index = 0; Index < LDHUD::DamageSummarySourcesMax + 2; ++Index)
    { Many.Lines.Add(Line(FGuid(), Source(ELDCombatSourceKind::Component, *FString::Printf(TEXT("component_%02d"), Index)), 100 * (20 - Index))); }
    View = LDHUD::DamageSummary(Many, Result, nullptr);
    TestTrue(TEXT("More sources than cells: the last cell sums the rest"), View.Rows.Num() == LDHUD::DamageSummarySourcesMax
        && View.Rows.Last() == TEXT("其他 36") && View.Heading == TEXT("你造成的伤害 155"));
    return true;
}
}

#endif
