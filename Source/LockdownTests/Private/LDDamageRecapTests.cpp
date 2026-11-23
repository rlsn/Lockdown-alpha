#include "Combat/LDDamageRecap.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Framework/LDCaptureTypes.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Internationalization/StringTableCore.h"
#include "Internationalization/StringTableRegistry.h"
#include "Misc/AutomationTest.h"
#include "UI/LDHUDModel.h"
#include "UI/LDTerms.h"
#include "UI/LDUIIcons.h"
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
    const FLDCombatSourceRef Thorns = Source(ELDCombatSourceKind::Card, TEXT("card_spiky_hide"));
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

FLDDamageLine Chain(const FGuid& Contender, const FLDCombatSourceRef& Ref, int64 DamageP2, const FGuid& Setter, uint8 Consumed, uint8 Reaction)
{
    FLDDamageLine Out = Line(Contender, Ref, DamageP2);
    Out.Setter = Setter;
    Out.Consumed = Consumed;
    Out.Reaction = Reaction;
    return Out;
}

// Reaction damage keeps its rule chain (who set the consumed element, which element, which reaction) as its own line.
// Receivers refuse a malformed chain or reaction prompt.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDamageRecapChainTest, "Lockdown.Rules.DamageRecap.Chain",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDamageRecapChainTest::RunTest(const FString& Parameters)
{
    const FLDCombatSourceRef Zap = Source(ELDCombatSourceKind::Learned, TEXT("zap"));
    TArray<FLDDamageSample> Log;
    LDDamageRecap::Record(Log, Line(Killer, Zap, 2000), 1000, 30000);
    LDDamageRecap::Record(Log, Chain(Killer, Zap, 5000, Helper, 2, 2), 1000, 30000);
    LDDamageRecap::Record(Log, Chain(Killer, Zap, 1000, Helper, 2, 2), 8000, 30000);
    LDDamageRecap::Record(Log, Chain(Killer, Zap, 500, Third, 2, 2), 9000, 30000);
    TArray<FLDDamageLine> Out;
    LDDamageRecap::Summarize(Log, 10000, 30000, Out);
    TestEqual(TEXT("One line per source and rule chain"), Out.Num(), 3);
    TestTrue(TEXT("Reaction damage with one setter adds up"), Out.Num() == 3
        && Out[0].Setter == Helper && Out[0].Consumed == 2 && Out[0].Reaction == 2 && Out[0].DamageP2 == 6000);
    TestTrue(TEXT("Plain damage of the same source is its own line"), Out.Num() == 3 && !Out[1].Setter.IsValid() && Out[1].Reaction == 0
        && Out[1].DamageP2 == 2000);
    TestTrue(TEXT("Another setter is another line"), Out.Num() == 3 && Out[2].Setter == Third && Out[2].DamageP2 == 500);

    FString Error;
    TestTrue(TEXT("A summarized chain is a valid list"), LDDamageRecap::ValidateLines(Out, Error));
    TArray<FLDDamageLine> Bad = Out;
    Bad[0].Reaction = 0;
    TestFalse(TEXT("A consumed element without a reaction is refused"), LDDamageRecap::ValidateLines(Bad, Error));
    Bad = Out;
    Bad[1].Setter = Helper;
    TestFalse(TEXT("A setter on plain damage is refused"), LDDamageRecap::ValidateLines(Bad, Error));
    Bad = Out;
    Bad[0].Reaction = 16;
    TestFalse(TEXT("An unknown reaction is refused"), LDDamageRecap::ValidateLines(Bad, Error));
    TArray<FLDDamageLine> Splash = {Chain(Killer, Zap, 700, FGuid(), 0, 3)};
    TestTrue(TEXT("An Overload splash (a reaction that consumed nothing on this body) is a valid line"), LDDamageRecap::ValidateLines(Splash, Error));
    Splash[0].Setter = Helper;
    TestFalse(TEXT("A setter without a consumed element is refused"), LDDamageRecap::ValidateLines(Splash, Error));

    FLDReactionAssistView Assist;
    Assist.Match = 3;
    Assist.Life = 1;
    Assist.Trigger = Killer;
    Assist.Consumed = 2;
    Assist.Reaction = 2;
    Assist.AtMs = 5000;
    TestTrue(TEXT("A complete assist prompt is accepted"), LDDamageRecap::ValidateReactionAssist(Assist, Error));
    FLDReactionAssistView NoTrigger = Assist;
    NoTrigger.Trigger.Invalidate();
    TestTrue(TEXT("An assist prompt with an unseen (unnamed) trigger is accepted"), LDDamageRecap::ValidateReactionAssist(NoTrigger, Error));
    FLDReactionAssistView NoLife = Assist;
    NoLife.Life = 0;
    TestFalse(TEXT("An assist prompt without the setter's life is refused"), LDDamageRecap::ValidateReactionAssist(NoLife, Error));
    FLDReactionAssistView NoElement = Assist;
    NoElement.Consumed = 0;
    TestFalse(TEXT("An assist prompt without an element is refused"), LDDamageRecap::ValidateReactionAssist(NoElement, Error));
    FLDReactionAssistView NoMatch = Assist;
    NoMatch.Match = 0;
    TestFalse(TEXT("An assist prompt without a match is refused"), LDDamageRecap::ValidateReactionAssist(NoMatch, Error));
    return true;
}

// The death banner names the rule chain of reaction damage, as text and as icon chain nodes (who triggered → what it
// consumed → what it produced), and tags the killer and the credited assisters. The setter of a reaction hears it once as
// an event feed line and a prompt; stale or other-match prompts are skipped.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDamageRecapChainDisplayTest, "Lockdown.Rules.DamageRecap.ChainDisplay",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDamageRecapChainDisplayTest::RunTest(const FString& Parameters)
{
    const FLDCombatSourceRef Zap = Source(ELDCombatSourceKind::Learned, TEXT("zap"));
    const FLDCombatSourceRef Basic = Source(ELDCombatSourceKind::Basic);
    FLDLobbyView Lobby;
    const auto Member = [&Lobby](const FGuid& Id, const TCHAR* Name)
    {
        FLDLobbyMember& Row = Lobby.Members.AddDefaulted_GetRef();
        Row.ContenderId = Id;
        Row.Name = Name;
    };
    const FGuid Self(0xD4, 0, 0, 0x99);
    Member(Self, TEXT("我"));
    Member(Killer, TEXT("老王"));
    Member(Helper, TEXT("阿强"));
    Member(Third, TEXT("小李"));
    FLDOwnerState Own;
    Own.Match = 3;
    Own.Life = 2;
    Own.ContenderId = Self;
    Own.LifePhase = ELDLifePhase::Dead;
    FLDDeathRecapView Recap;
    Recap.Match = 3;
    Recap.Life = 2;
    Recap.WindowMs = 15000;
    Recap.Killer = Killer;
    Recap.Assisters = {Helper};
    FLDDamageLine Fatal = Chain(Killer, Zap, 30000, Helper, 2, 2);
    FLDDamageLine Own2 = Chain(Killer, Basic, 10000, Self, 3, 3);
    FLDDamageLine Help = Line(Helper, Basic, 20000);
    FLDDamageLine Other = Line(Third, Basic, 8000);
    Recap.Lines = {Fatal, Own2, Help, Other};
    const LDHUD::FDamageListView View = LDHUD::DeathRecap(Recap, Own, Lobby, nullptr);
    TestTrue(TEXT("The recap covers the assist window"), View.bAvailable && View.Heading == TEXT("近 15 秒承受伤害 680") && View.Rows.Num() == 3);
    TestTrue(TEXT("Reaction damage names its chain: someone else's setup, your own setup; the killer row is tagged"), View.Rows.Num() == 3
        && View.Rows[0] == FString::Printf(TEXT("老王 400 · zap → 阿强的wet → %s 300 · %s → 你的electrified → %s 100 · 击杀"),
            LDTerms::ReactionConduct, LDTerms::BasicAttack, LDTerms::ReactionOverload));
    TestTrue(TEXT("A credited assister is tagged"), View.Rows.Num() == 3
        && View.Rows[1] == FString::Printf(TEXT("阿强 200 · %s 200 · %s"), LDTerms::BasicAttack, LDTerms::Assist));
    TestTrue(TEXT("An attacker neither killer nor assister has no tag"), View.Rows.Num() == 3
        && View.Rows[2] == FString::Printf(TEXT("小李 80 · %s 80"), LDTerms::BasicAttack));

    using LDHUD::ERecapNode;
    using LDHUD::ERecapLink;
    const auto Is = [](const LDHUD::FRecapNode& Node, ERecapNode Kind, ERecapLink Link, const FString& Text)
    { return Node.Kind == Kind && Node.Link == Link && Node.Text == Text; };
    TestEqual(TEXT("Every recap row has its icon chain"), View.Chains.Num(), View.Rows.Num());
    const TArray<LDHUD::FRecapNode> Nodes = View.Chains.IsValidIndex(0) ? View.Chains[0].Nodes : TArray<LDHUD::FRecapNode>();
    TestTrue(TEXT("The chain starts with the attacker and its total"), Nodes.Num() == 13
        && Is(Nodes[0], ERecapNode::Attacker, ERecapLink::None, TEXT("老王")) && Nodes[0].Glyph == TEXT("老")
        && Is(Nodes[1], ERecapNode::Amount, ERecapLink::None, TEXT("400")));
    TestTrue(TEXT("Someone else's setup reads source → setter's state → reaction"), Nodes.Num() == 13
        && Nodes[2].Kind == ERecapNode::Source && Nodes[2].Link == ERecapLink::Gap && Nodes[2].Name == TEXT("zap")
        && Is(Nodes[3], ERecapNode::Setter, ERecapLink::Arrow, TEXT("阿强"))
        && Nodes[4].Kind == ERecapNode::State && Nodes[4].Link == ERecapLink::None && Nodes[4].Name == TEXT("wet")
        && Is(Nodes[5], ERecapNode::Reaction, ERecapLink::Arrow, LDTerms::ReactionConduct)
        && Is(Nodes[6], ERecapNode::Amount, ERecapLink::None, TEXT("300")));
    TestTrue(TEXT("The own setup reads 你, a basic attack shows its glyph, and the killer is tagged last"), Nodes.Num() == 13
        && Nodes[7].Kind == ERecapNode::Source && Nodes[7].IconId.IsEmpty() && Nodes[7].Glyph == LDHUD::IconGlyph(LDTerms::BasicAttack)
        && Is(Nodes[8], ERecapNode::Setter, ERecapLink::Arrow, TEXT("你"))
        && Nodes[9].Name == TEXT("electrified") && Is(Nodes[10], ERecapNode::Reaction, ERecapLink::Arrow, LDTerms::ReactionOverload)
        && Is(Nodes[12], ERecapNode::Tag, ERecapLink::Gap, TEXT("击杀")));
    TestTrue(TEXT("An assister's chain ends with its tag"), View.Chains.Num() == 3 && View.Chains[1].Nodes.Num() == 5
        && Is(View.Chains[1].Nodes.Last(), ERecapNode::Tag, ERecapLink::Gap, LDTerms::Assist) && View.Chains[2].Nodes.Num() == 4);

    // A private string table names the fixture rows, so no display text is reported missing.
    const FName Table(TEXT("LDDamageRecapChainFixture"));
    FStringTableRef Names = FStringTable::NewStringTable();
    Names->SetNamespace(Table.ToString());
    FLDDemoContentDefinitions Content;
    for (const TCHAR* Id : {TEXT("zap"), TEXT("wet")})
    {
        FLDDemoDisplayDefinition Display;
        Display.Id = Id;
        Display.Namespace = Table.ToString();
        Display.NameKey = FString(Id) + TEXT(".name");
        Display.IconId = FString(TEXT("icon_")) + Id;
        Content.Display.Add(Display);
#if WITH_EDITORONLY_DATA
        Names->SetSourceString(FTextKey(*Display.NameKey), Id, FString());
#else
        Names->SetSourceString(FTextKey(*Display.NameKey), Id);
#endif
    }
    FStringTableRegistry::Get().RegisterStringTable(Table, Names);
    TArray<LDHUD::FRecapNode> Icons;
    LDHUD::ReactionChainNodes(&Content, Fatal, Self, TEXT("阿强"), Icons);
    TestTrue(TEXT("The source and the consumed state show their content icons"), Icons.Num() == 4
        && Icons[0].IconId == TEXT("icon_zap") && Icons[2].IconId == TEXT("icon_wet") && Icons[3].IconId.IsEmpty());
    // A creature has a display row for its name but no icon picture: its attack shows a glyph and asks for no texture.
    FLDDemoMonsterDefinition Creature;
    Creature.Id = TEXT("zap");
    Content.Monsters.Add(Creature);
    Icons.Reset();
    LDHUD::ReactionChainNodes(&Content, Fatal, Self, TEXT("阿强"), Icons);
    TestTrue(TEXT("A creature source shows its glyph instead of an icon"), Icons.Num() == 4
        && Icons[0].IconId.IsEmpty() && !Icons[0].Glyph.IsEmpty() && Icons[2].IconId == TEXT("icon_wet"));
    TestTrue(TEXT("A creature id has no content icon"), LDHUD::ContentIconId(&Content, TEXT("zap")).IsEmpty()
        && LDHUD::ContentIconId(&Content, TEXT("wet")) == TEXT("icon_wet"));
    Content.Monsters.Reset();
    Icons.Reset();
    LDHUD::ReactionChainNodes(&Content, Line(Killer, Source(ELDCombatSourceKind::Innate), 100), Self, FString(), Icons);
    TestTrue(TEXT("The innate dash shows the dash icon and no chain"), Icons.Num() == 1 && Icons[0].IconId == LDUIIcons::Dash);
    Icons.Reset();
    LDHUD::ReactionChainNodes(&Content, Chain(Killer, Zap, 700, FGuid(), 0, 3), Self, FString(), Icons);
    TestTrue(TEXT("An Overload splash links the source straight to the reaction"), Icons.Num() == 2
        && Is(Icons[1], ERecapNode::Reaction, ERecapLink::Arrow, LDTerms::ReactionOverload));
    Icons.Reset();
    LDHUD::ReactionChainNodes(&Content, Chain(Killer, Zap, 100, FGuid(), 2, 2), Self, FString(), Icons);
    TestTrue(TEXT("An unnamed setter leaves the state alone after the arrow"), Icons.Num() == 3
        && Icons[1].Kind == ERecapNode::State && Icons[1].Link == ERecapLink::Arrow);
    FStringTableRegistry::Get().UnregisterStringTable(Table);
    FLDDamageLine SelfSet = Chain(Killer, Zap, 100, Killer, 1, 1);
    TestEqual(TEXT("The attacker's own setup names nobody"), LDHUD::ReactionChainText(nullptr, SelfSet, Self, TEXT("老王")),
        FString::Printf(TEXT("zap → burn → %s"), LDTerms::ReactionFog));
    FLDDeathRecapView ByCamp = Recap;
    ByCamp.Killer.Invalidate();
    ByCamp.KillerMonsterId = FName(TEXT("monster_low"));
    ByCamp.Lines = {Line(FGuid(), Basic, 4000, TEXT("monster_low"))};
    TestTrue(TEXT("A camp member that landed the fatal hit is tagged as the killer"),
        LDHUD::DeathRecap(ByCamp, Own, Lobby, nullptr).Rows == TArray<FString>{FString::Printf(TEXT("monster_low 40 · %s 40 · 击杀"), LDTerms::BasicAttack)});

    FLDReactionAssistView Assist;
    Assist.Match = 3;
    Assist.Life = 2;
    Assist.Trigger = Killer;
    Assist.Consumed = 2;
    Assist.Reaction = 2;
    Assist.AtMs = 19000;
    const FString Told = FString::Printf(TEXT("你的wet被老王引爆（%s）"), LDTerms::ReactionConduct);
    TestEqual(TEXT("The assist prompt names the setter's state, the trigger and the reaction"), LDHUD::ReactionAssistText(nullptr, Assist, TEXT("老王")), Told);
    FLDPublicMatchView Public;
    Public.Generation = 3;
    Public.Phase = ELDMatchPhase::Development;
    FLDKillFeedView Kills;
    Kills.Generation = 3;
    TArray<FLDReactionAssistView> Assists;
    LDHUD::FEventFeedInput In;
    In.Public = &Public;
    In.Kills = &Kills;
    In.Lobby = &Lobby;
    In.Self = Self;
    In.OwnReactionAssists = &Assists;
    In.OwnLife = 2;
    In.bClock = true;
    In.NowMs = 20000;
    In.LocalSeconds = 100;
    LDHUD::FEventFeed Feed;
    Feed.Observe(In);
    FLDReactionAssistView Stale = Assist;
    Stale.AtMs = 1000;
    Stale.Serial = 1;
    FLDReactionAssistView OtherMatch = Assist;
    OtherMatch.Match = 2;
    OtherMatch.Serial = 2;
    FLDReactionAssistView Fresh = Assist;
    Fresh.Serial = 3;
    Assists = {Stale, OtherMatch, Fresh};
    In.LocalSeconds = 101;
    Feed.Observe(In);
    TestTrue(TEXT("A fresh own assist is told once as an own line and a prompt; stale and other-match ones are not"),
        Feed.Lines().Num() == 1 && Feed.Lines()[0].Text == Told && Feed.Lines()[0].bSelf && Feed.AssistPrompt() == Told);
    In.LocalSeconds = 102;
    Feed.Observe(In);
    TestTrue(TEXT("An assist already told is not told again"), Feed.Lines().Num() == 1 && Feed.AssistPrompt().IsEmpty());
    return true;
}

// The rule chain names only what the reader may know. A setter the dead victim never met as an attacker in its recap window
// nor among its credited assisters stays unnamed (and its rows merge); an Overload splash says the reaction without a
// consumed state; a prompt whose trigger the setter did not see names nobody; a prompt kept before the feed's first
// observation is still told once, but not before the clock dates it; prompts of an earlier life are never told; one that
// arrives before the own life is known waits for it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDamageRecapChainSourcesTest, "Lockdown.Rules.DamageRecap.ChainSources",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDamageRecapChainSourcesTest::RunTest(const FString& Parameters)
{
    const FLDCombatSourceRef Zap = Source(ELDCombatSourceKind::Learned, TEXT("zap"));
    const FLDCombatSourceRef Basic = Source(ELDCombatSourceKind::Basic);
    const FGuid Victim(0xD4, 0, 0, 0x99);
    TArray<FLDDamageSample> Log;
    LDDamageRecap::Record(Log, Line(Fifth, Basic, 100), 1000, 30000);
    LDDamageRecap::Record(Log, Line(Fourth, Basic, 100), 2000, 30000);
    LDDamageRecap::Record(Log, Chain(Killer, Zap, 3000, Helper, 2, 2), 9000, 30000);
    LDDamageRecap::Record(Log, Chain(Killer, Zap, 1000, Third, 2, 2), 9000, 30000);
    LDDamageRecap::Record(Log, Chain(Killer, Zap, 400, Fifth, 2, 2), 9100, 30000);
    LDDamageRecap::Record(Log, Chain(Killer, Zap, 200, Fourth, 1, 1), 9200, 30000);
    LDDamageRecap::Record(Log, Chain(Killer, Zap, 300, Victim, 3, 3), 9300, 30000);
    // Death at 10000 with an 8500 ms window: Fifth's own hit at 1000 lies outside it.
    LDDamageRecap::HideUnreadableSetters(Log, 10000, 8500, Victim, {Helper});
    TArray<FLDDamageLine> Out;
    LDDamageRecap::Summarize(Log, 10000, 8500, Out);
    const FLDDamageLine* Assisted = Out.FindByPredicate([](const FLDDamageLine& Row) { return Row.Setter == Helper; });
    const FLDDamageLine* Attacker = Out.FindByPredicate([](const FLDDamageLine& Row) { return Row.Setter == Fourth; });
    const FLDDamageLine* Own = Out.FindByPredicate([&Victim](const FLDDamageLine& Row) { return Row.Setter == Victim; });
    const FLDDamageLine* Unnamed = Out.FindByPredicate([](const FLDDamageLine& Row) { return Row.Reaction == 2 && !Row.Setter.IsValid(); });
    TestTrue(TEXT("A credited assister, an attacker inside the window and the victim itself stay named"),
        Assisted && Assisted->DamageP2 == 3000 && Attacker && Attacker->DamageP2 == 200 && Own && Own->DamageP2 == 300);
    TestTrue(TEXT("Setters the victim never met inside the window are hidden and their rows merge"),
        Unnamed && Unnamed->DamageP2 == 1400 && !Out.ContainsByPredicate([](const FLDDamageLine& Row) { return Row.Setter == Third || Row.Setter == Fifth; }));
    FString Error;
    TestTrue(TEXT("The hidden recap is a valid list"), LDDamageRecap::ValidateLines(Out, Error));
    TestEqual(TEXT("An unnamed setter shows the state alone"), Unnamed ? LDHUD::ReactionChainText(nullptr, *Unnamed, Victim, FString()) : FString(),
        FString::Printf(TEXT("zap → wet → %s"), LDTerms::ReactionConduct));
    TestEqual(TEXT("An Overload splash shows the reaction without a consumed state"),
        LDHUD::ReactionChainText(nullptr, Chain(Killer, Zap, 700, FGuid(), 0, 3), Victim, FString()),
        FString::Printf(TEXT("zap → %s"), LDTerms::ReactionOverload));

    FLDReactionAssistView Assist;
    Assist.Match = 3;
    Assist.Life = 2;
    Assist.Consumed = 1;
    Assist.Reaction = 1;
    Assist.AtMs = 19000;
    const FString Anonymous = FString::Printf(TEXT("你的burn被引爆（%s）"), LDTerms::ReactionFog);
    TestEqual(TEXT("A trigger the setter did not see is never named"), LDHUD::ReactionAssistText(nullptr, Assist, TEXT("老王")), Anonymous);

    FLDPublicMatchView Public;
    Public.Generation = 3;
    Public.Phase = ELDMatchPhase::Development;
    FLDKillFeedView Kills;
    Kills.Generation = 3;
    FLDReactionAssistView Kept = Assist;
    Kept.Serial = 1;
    TArray<FLDReactionAssistView> Assists = {Kept};
    LDHUD::FEventFeedInput In;
    In.Public = &Public;
    In.Kills = &Kills;
    In.Self = Victim;
    In.OwnReactionAssists = &Assists;
    In.OwnLife = 2;
    In.bClock = false;
    In.NowMs = 0;
    In.LocalSeconds = 99;
    LDHUD::FEventFeed Feed;
    Feed.Observe(In);
    TestTrue(TEXT("A kept prompt waits while the clock is unknown"), Feed.Lines().IsEmpty() && Feed.AssistPrompt().IsEmpty());
    In.bClock = true;
    In.NowMs = 20000;
    In.LocalSeconds = 100;
    Feed.Observe(In);
    TestTrue(TEXT("A fresh prompt kept before the feed's first dated observation is told once"),
        Feed.Lines().Num() == 1 && Feed.AssistPrompt() == Anonymous);
    FLDReactionAssistView OldLife = Assist;
    OldLife.Life = 1;
    OldLife.Serial = 2;
    Assists.Add(OldLife);
    In.LocalSeconds = 101;
    Feed.Observe(In);
    TestTrue(TEXT("A prompt of an earlier life is not told"), Feed.Lines().Num() == 1 && Feed.AssistPrompt().IsEmpty());
    FLDReactionAssistView Fresh = Assist;
    Fresh.Serial = 3;
    Assists.Add(Fresh);
    In.OwnLife = 0;
    In.LocalSeconds = 102;
    Feed.Observe(In);
    TestTrue(TEXT("Without the own current life nothing is told yet"), Feed.Lines().Num() == 1 && Feed.AssistPrompt().IsEmpty());
    FLDReactionAssistView Next = Assist;
    Next.Serial = 4;
    Assists.Add(Next);
    In.OwnLife = 2;
    In.LocalSeconds = 103;
    Feed.Observe(In);
    TestTrue(TEXT("The waiting prompt and the next one of the own life are told in order"),
        Feed.Lines().Num() == 3 && Feed.AssistPrompt() == Anonymous);
    return true;
}

// A prompt is a reliable owner RPC that can overtake the replicated match and own life it belongs to: the feed waits for
// them instead of dropping it, also across a match change. The owner keeps prompts only of the newest (match, life): a late
// prompt of an earlier one is stale and never wipes newer ones.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDamageRecapAssistOrderingTest, "Lockdown.Rules.DamageRecap.AssistOrdering",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDamageRecapAssistOrderingTest::RunTest(const FString& Parameters)
{
    FLDReactionAssistView Base;
    Base.Match = 3;
    Base.Life = 2;
    Base.Consumed = 2;
    Base.Reaction = 2;
    Base.AtMs = 19000;
    TArray<FLDReactionAssistView> Kept;
    uint64 Serial = 0;
    FString Error;
    using LDDamageRecap::EAssistKeep;
    TestTrue(TEXT("A prompt ahead of the known own state is kept"),
        LDDamageRecap::KeepReactionAssist(Kept, Serial, Base, 3, 1, 6, Error) == EAssistKeep::Kept && Kept.Num() == 1 && Kept[0].Serial == 1);
    FLDReactionAssistView Earlier = Base;
    Earlier.Life = 1;
    TestTrue(TEXT("A late prompt of an earlier life is stale and wipes nothing"),
        LDDamageRecap::KeepReactionAssist(Kept, Serial, Earlier, 0, 0, 6, Error) == EAssistKeep::Stale && Kept.Num() == 1 && Serial == 1);
    FLDReactionAssistView NextMatch = Base;
    NextMatch.Match = 4;
    NextMatch.Life = 1;
    TestTrue(TEXT("A prompt older than the known own state is stale"),
        LDDamageRecap::KeepReactionAssist(Kept, Serial, Base, 4, 1, 6, Error) == EAssistKeep::Stale && Kept.Num() == 1);
    TestTrue(TEXT("A newer match replaces the kept prompts of the older one"),
        LDDamageRecap::KeepReactionAssist(Kept, Serial, NextMatch, 3, 2, 6, Error) == EAssistKeep::Kept
        && Kept.Num() == 1 && Kept[0].Match == 4 && Kept[0].Serial == 2);
    FLDReactionAssistView Broken = NextMatch;
    Broken.Reaction = 0;
    TestTrue(TEXT("A malformed prompt is invalid"),
        LDDamageRecap::KeepReactionAssist(Kept, Serial, Broken, 4, 1, 6, Error) == EAssistKeep::Invalid && Kept.Num() == 1);
    for (int32 Index = 0; Index < 6; ++Index) { LDDamageRecap::KeepReactionAssist(Kept, Serial, NextMatch, 4, 1, 6, Error); }
    TestTrue(TEXT("Only the newest prompts up to the cap are kept"), Kept.Num() == 6 && Kept[0].Serial == 3 && Kept.Last().Serial == 8);

    FLDPublicMatchView Public;
    Public.Generation = 3;
    Public.Phase = ELDMatchPhase::Development;
    TArray<FLDReactionAssistView> Assists;
    LDHUD::FEventFeedInput In;
    In.Public = &Public;
    In.OwnReactionAssists = &Assists;
    In.OwnLife = 2;
    In.bClock = true;
    In.NowMs = 20000;
    In.LocalSeconds = 100;
    LDHUD::FEventFeed Feed;
    Feed.Observe(In);
    FLDReactionAssistView Ahead = Base;
    Ahead.Life = 3;
    Ahead.Serial = 1;
    Assists.Add(Ahead);
    In.LocalSeconds = 101;
    Feed.Observe(In);
    TestTrue(TEXT("A prompt of a life not known yet waits"), Feed.Lines().IsEmpty());
    In.OwnLife = 3;
    In.LocalSeconds = 102;
    Feed.Observe(In);
    TestTrue(TEXT("It is told once the own life catches up"), Feed.Lines().Num() == 1 && !Feed.AssistPrompt().IsEmpty());
    FLDReactionAssistView NewMatch = NextMatch;
    NewMatch.Serial = 2;
    Assists = {NewMatch};
    In.LocalSeconds = 103;
    Feed.Observe(In);
    TestTrue(TEXT("A prompt of a match not known yet waits"), Feed.Lines().Num() == 1 && Feed.AssistPrompt().IsEmpty());
    Public.Generation = 4;
    In.OwnLife = 1;
    In.LocalSeconds = 104;
    Feed.Observe(In);
    TestTrue(TEXT("It is told on the first observation of its match"), Feed.Lines().Num() == 1 && !Feed.AssistPrompt().IsEmpty());
    In.LocalSeconds = 105;
    Feed.Observe(In);
    TestTrue(TEXT("A told prompt is never told again"), Feed.AssistPrompt().IsEmpty());
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
    const FLDCombatSourceRef Thorns = Source(ELDCombatSourceKind::Card, TEXT("card_spiky_hide"));
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
    { Many.Lines.Add(Line(FGuid(), Source(ELDCombatSourceKind::Card, *FString::Printf(TEXT("card_%02d"), Index)), 100 * (20 - Index))); }
    View = LDHUD::DamageSummary(Many, Result, nullptr);
    TestTrue(TEXT("More sources than cells: the last cell sums the rest"), View.Rows.Num() == LDHUD::DamageSummarySourcesMax
        && View.Rows.Last() == TEXT("其他 36") && View.Heading == TEXT("你造成的伤害 155"));
    return true;
}
}

#endif
