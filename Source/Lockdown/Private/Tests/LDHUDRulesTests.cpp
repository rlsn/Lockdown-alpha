#include "UI/LDHUDModel.h"
#include "UI/LDBlueprintPanel.h"
#include "Development/LDDemoCardText.h"
#include "UI/LDTerms.h"
#include "UI/LDUIIcons.h"
#include "UI/LDDevTestPanel.h"
#include "Development/LDDevTestProtocol.h"
#include "Development/LDGrowthProtocol.h"
#include "Combat/LDCombatTypes.h"
#include "UI/LDMinimap.h"
#include "UI/LDWorldFeedback.h"
#include "Character/LDHumanoidVisualComponent.h"
#include "Combat/LDAttributeProjection.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Development/LDDemoContentIds.h"
#include "Internationalization/StringTableCore.h"
#include "Internationalization/StringTableRegistry.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Tests/LDTestApprovedDisplay.h"
#include "Tests/LDTestApprovedFixture.h"
#include "Development/LDDemoContentCatalog.h"
#include "PK/LDPKRules.h"
#include "Engine/Texture2D.h"
#include <limits>
#if WITH_DEV_AUTOMATION_TESTS
namespace
{
// A private table id keeps the fixture away from the real DemoContent display table. Its names are the approved
// catalog's (#111), so expectations read them through HudName instead of repeating them.
const FName HudFixtureTable(TEXT("LDHUDRulesDisplayFixture"));
const TCHAR* const HudFixtureIds[]={TEXT("root"),TEXT("shield"),TEXT("respawn_protection"),TEXT("camp_returning"),TEXT("monster_guardian")};
TMap<FString,FString> HudFixtureNames;
FString HudName(const TCHAR* Id)
{
    const FString* Name=HudFixtureNames.Find(Id);
    return Name?*Name:FString(Id);
}
FLDDemoContentDefinitions HudFixtureContent()
{
    FLDDemoContentDefinitions Content;
    for(const TCHAR* Id:HudFixtureIds)
    {FLDDemoDisplayDefinition Display;Display.Id=Id;Display.Namespace=HudFixtureTable.ToString();Display.NameKey=FString(Id)+TEXT(".name");Display.DescriptionKey=FString(Id)+TEXT(".description");Display.IconId=FString(TEXT("icon_"))+Id;Content.Display.Add(Display);}
    FLDDemoMonsterDefinition Monster;Monster.Id=TEXT("monster_guardian");Monster.NativeId=uint8(ELDDemoMonsterId::MonsterGuardian);Content.Monsters.Add(Monster);
    return Content;
}
bool HudRegisterFixtureTable(FAutomationTestBase& Test)
{
    if(!LDTestApprovedDisplay::Names(Test,HudFixtureNames)){return false;}
    FStringTableRef Table=FStringTable::NewStringTable();Table->SetNamespace(HudFixtureTable.ToString());
    for(const TCHAR* Id:HudFixtureIds)
    {
        const FString* Name=HudFixtureNames.Find(Id);
        if(!Name){Test.AddError(FString::Printf(TEXT("The approved catalog has no display name for %s."),Id));return false;}
        const FString Key=FString(Id)+TEXT(".name");
        // Development game targets also compile these tests; only editor builds have the dev-notes overload.
#if WITH_EDITORONLY_DATA
        Table->SetSourceString(FTextKey(*Key),*Name,FString());
#else
        Table->SetSourceString(FTextKey(*Key),*Name);
#endif
    }
    FStringTableRegistry::Get().RegisterStringTable(HudFixtureTable,Table);
    return true;
}
// The game registers the approved display table at startup; when this editor session has not, the approved names are
// registered for the rendered faces (OutTable stays None when the existing table is used and nothing must be unregistered).
bool HudRegisterApprovedNames(FAutomationTestBase& Test,const FLDDemoContentDefinitions& Content,FName& OutTable)
{
    OutTable=NAME_None;
    TMap<FString,FString> Names;
    if(!LDTestApprovedDisplay::Names(Test,Names)||Content.Display.IsEmpty()){return false;}
    const FName Namespace(*Content.Display[0].Namespace);
    if(FStringTableRegistry::Get().FindStringTable(Namespace).IsValid()){return true;}
    OutTable=Namespace;
    FStringTableRef Table=FStringTable::NewStringTable();Table->SetNamespace(OutTable.ToString());
    for(const FLDDemoDisplayDefinition& Row:Content.Display)
    {
        const FString* Name=Names.Find(Row.Id);
        if(!Name||Row.Namespace!=OutTable.ToString()){continue;}
#if WITH_EDITORONLY_DATA
        Table->SetSourceString(FTextKey(*Row.NameKey),*Name,FString());
#else
        Table->SetSourceString(FTextKey(*Row.NameKey),*Name);
#endif
    }
    FStringTableRegistry::Get().RegisterStringTable(OutTable,Table);
    return true;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDMaskTest,"Lockdown.Rules.HUDObservationBoundary",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDMaskTest::RunTest(const FString& Parameters)
{
    if(!HudRegisterFixtureTable(*this)){return false;}
    ON_SCOPE_EXIT{FStringTableRegistry::Get().UnregisterStringTable(HudFixtureTable);};
    const FLDDemoContentDefinitions Content=HudFixtureContent();
    FLDObservedUnitView Rival;Rival.Kind=ELDObservedUnitKind::Contender;Rival.AuthorizedFieldMask=LDObservation::ContenderFields;Rival.ContenderId=FGuid::NewGuid();Rival.Level=3;
    Rival.Health.CurrentP2=45001;Rival.Health.MaxP2=60000;Rival.Mana.CurrentP2=12099;Rival.Mana.MaxP2=20000;
    FLDObservedStatus Root;Root.StatusId=uint8(ELDDemoStatusId::Root);Root.ExpiresAtServerMs=12500;Rival.Statuses.Add(Root);
    FLDObservedStatus Protection;Protection.StatusId=uint8(ELDDemoStatusId::RespawnProtection);Protection.ExpiresAtServerMs=11500;Rival.Statuses.Add(Protection);
    const FString Shown=LDHUD::ObservedUnit(Rival,TEXT("玩家甲"),&Content,10.);
    auto Has=[](const FString& Text,const TCHAR* Label){return Text.Contains(Label,ESearchCase::CaseSensitive);};
    TestEqualSensitive(TEXT("Observed card renders exactly the SC-07 whitelist with Q/W/E and approved display names"),Shown,
        FString::Printf(TEXT("玩家甲 · 等级 3\n%s 451/600 · %s 120/200\n状态：%s 2.5s · %s 1.5s\n技能：Q 空槽 · W 空槽 · E 空槽"),LDTerms::Health,LDTerms::Mana,
            *HudName(TEXT("root")),*HudName(TEXT("respawn_protection"))));
    for(const TCHAR* Private:{LDTerms::Experience,TEXT("冷却"),TEXT("被动"),LDTerms::AttackDamage,LDTerms::Armor,TEXT("属性"),TEXT("98765")})
    {TestFalse(FString::Printf(TEXT("Observed card has no private category: %s"),Private),Has(Shown,Private));}
    TestTrue(TEXT("A contender missing from the public roster is not given an invented name"),LDHUD::ObservedUnit(Rival,FString(),&Content,10.).StartsWith(TEXT("未列名参赛者"),ESearchCase::CaseSensitive));
    FLDObservedUnitView Camp;Camp.Kind=ELDObservedUnitKind::CampMember;Camp.AuthorizedFieldMask=LDObservation::CampFields;Camp.CreatureId=uint8(ELDDemoMonsterId::MonsterGuardian);
    Camp.Health.CurrentP2=100;Camp.Health.MaxP2=200;FLDObservedStatus Returning;Returning.StatusId=uint8(ELDDemoStatusId::CampReturning);Camp.Statuses.Add(Returning);
    const FString CampShown=LDHUD::ObservedUnit(Camp,TEXT("Ignored"),&Content,0.);
    TestEqualSensitive(TEXT("Camp card renders exactly name, HP and public status"),CampShown,HudName(TEXT("monster_guardian"))+TEXT("\n")+LDTerms::Health+TEXT(" 1/2\n状态：")+HudName(TEXT("camp_returning")));
    TestFalse(TEXT("Camp card has no level, mana or a contender name"),Has(CampShown,TEXT("等级"))||Has(CampShown,LDTerms::Mana)||Has(CampShown,TEXT("Ignored")));
    const LDHUD::FObservedCard CampCard=LDHUD::ObservedCard(Camp,FString(),&Content,0.);
    TestTrue(TEXT("Card structure follows the authorized field mask"),!CampCard.bMana&&!CampCard.bActives&&CampCard.Health.bKnown);
    FString Text;
    // The missing-text path is a content error by design: it logs once per id and shows the stable id.
    AddExpectedMessage(TEXT("LD_HUD_DISPLAY_TEXT_MISSING hud_rules_fixture_without_text"),ELogVerbosity::Error,EAutomationExpectedMessageFlags::Contains,1,false);
    TestFalse(TEXT("A definition without display text is a content error"),LDHUD::ContentText(&Content,TEXT("hud_rules_fixture_without_text"),false,Text));
    TestEqualSensitive(TEXT("A missing display text shows the stable id, never a guessed name"),Text,FString(TEXT("hud_rules_fixture_without_text")));
    TestTrue(TEXT("A registered display text resolves through the DemoContent table"),LDHUD::ContentText(&Content,TEXT("root"),false,Text)&&Text==HudName(TEXT("root")));
    TestEqualSensitive(TEXT("Without approved content an observed creature id is reported, not named"),LDHUD::CreatureName(nullptr,2),FString(TEXT("未知野怪 #2")));
    TestEqualSensitive(TEXT("An unknown status id is reported, not named"),LDHUD::StatusName(&Content,200),FString(TEXT("未知状态 #200")));
    FLDLobbyView Lobby;FLDLobbyMember Member;Member.ContenderId=Rival.ContenderId;Member.Name=TEXT("玩家甲");Lobby.Members.Add(Member);
    TestEqualSensitive(TEXT("Roster names come from the public roster"),LDHUD::RosterName(Lobby,Rival.ContenderId),FString(TEXT("玩家甲")));
    TestTrue(TEXT("An unlisted contender has no invented roster name"),LDHUD::RosterName(Lobby,FGuid::NewGuid()).IsEmpty()&&LDHUD::RosterName(Lobby,FGuid()).IsEmpty());
    const FBox2D Ground(FVector2D(-5000,-5000),FVector2D(5000,5000));
    const LDMinimap::FView MapView=LDMinimap::MakeView(ELDMinimapOrientation::North,Ground,FVector2D(240,240),0.,false,0.);
    const FVector World(-1700,-1000,200);const FVector Back=LDMinimap::ToWorld(MapView,LDMinimap::ToFrame(MapView,World));
    TestTrue(TEXT("Map conversion round trips XY and deliberately grants no height or dynamic target"),MapView.IsValid()&&FVector::Dist2D(World,Back)<.001&&Back.Z==0);
    const FVector Edge=LDMinimap::ClampToGround(LDMinimap::ToWorld(MapView,FVector2D(5,-3)*MapView.FrameSize),Ground);
    TestTrue(TEXT("Map edge clamp stays inside the registered ground"),Edge.X>=Ground.Min.X&&Edge.X<Ground.Max.X&&Edge.Y>=Ground.Min.Y&&Edge.Y<Ground.Max.Y&&Edge.Z==0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDLifeStatusTest,"Lockdown.Rules.HUDLifeStatus",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDLifeStatusTest::RunTest(const FString& Parameters)
{
    FLDOwnerState Own;Own.LifePhase=ELDLifePhase::Alive;Own.ProtectedUntilMs=5000;
    TestEqualSensitive(TEXT("Protection counts down from the matching authority clock"),LDHUD::LifeStatus(Own,true,3000),FString(LDTerms::RespawnProtection)+TEXT(" 2.0s · ")+LDTerms::ProtectionRule);
    TestEqualSensitive(TEXT("A live protection never reads zero before the clock reaches its deadline"),LDHUD::LifeStatus(Own,true,4999),FString(LDTerms::RespawnProtection)+TEXT(" 0.1s · ")+LDTerms::ProtectionRule);
    TestEqualSensitive(TEXT("Protection ends once the authority clock reaches the deadline, before the field is cleared"),LDHUD::LifeStatus(Own,true,5000),FString(TEXT("存活")));
    TestEqualSensitive(TEXT("Without the matching clock protection time is unavailable, never local time or zero"),LDHUD::LifeStatus(Own,false,0),FString(LDTerms::RespawnProtection)+TEXT(" · 时间同步中"));
    TestEqualSensitive(TEXT("A time value is ignored while the clock is unavailable"),LDHUD::LifeStatus(Own,false,999999),FString(LDTerms::RespawnProtection)+TEXT(" · 时间同步中"));
    Own.ProtectedUntilMs=0;
    TestEqualSensitive(TEXT("No protection deadline needs no clock"),LDHUD::LifeStatus(Own,false,0),FString(TEXT("存活")));
    TestFalse(TEXT("A living owner has no death banner"),LDHUD::Death(Own,true,0).bVisible);
    Own.LifePhase=ELDLifePhase::Dead;Own.RespawnAtMs=12000;
    const FString Waiting=LDHUD::LifeStatus(Own,true,2350);
    TestEqualSensitive(TEXT("Respawn waits on the Lives deadline"),Waiting,FString(LDTerms::Dead)+TEXT(" · ")+LDTerms::RespawnWait+TEXT(" 9.7s"));
    TestFalse(TEXT("Death shows no growth loss"),Waiting.Contains(TEXT("损失")));
    TestEqualSensitive(TEXT("The respawn countdown reads zero only once the authority clock reaches it"),LDHUD::LifeStatus(Own,true,12000),FString(LDTerms::Dead)+TEXT(" · ")+LDTerms::RespawnWait+TEXT(" 0.0s"));
    TestEqualSensitive(TEXT("Respawn time without the matching clock is unavailable"),LDHUD::LifeStatus(Own,false,0),FString(LDTerms::Dead)+TEXT(" · ")+LDTerms::RespawnWait+TEXT("（时间同步中）"));
    const LDHUD::FDeathView Death=LDHUD::Death(Own,true,2350);
    TestTrue(TEXT("The death banner names the state and the countdown and keeps the build"),Death.bVisible&&Death.Title==LDTerms::DeathTitle&&Death.Countdown==FString(LDTerms::RespawnWait)+TEXT(" 9.7s")&&Death.Note==LDTerms::DeathNote());
    TestEqualSensitive(TEXT("The banner never invents a respawn time"),LDHUD::Death(Own,false,0).Countdown,FString(LDTerms::RespawnWait)+TEXT(" · 时间同步中"));
    Own.RespawnAtMs=0;
    TestEqualSensitive(TEXT("An eliminated finalist is never promised another respawn"),LDHUD::Death(Own,true,0,true).Countdown,FString(TEXT("终局已出局")));
    Own.bActive=true;
    TestTrue(TEXT("The Lives phase, not the legacy active flag, decides the line"),LDHUD::LifeStatus(Own,true,0).StartsWith(LDTerms::Dead,ESearchCase::CaseSensitive));
    Own.LifePhase=ELDLifePhase::Unspawned;TestEqualSensitive(TEXT("An unspawned contender is not shown alive"),LDHUD::LifeStatus(Own,true,0),FString(TEXT("等待出生")));
    Own.LifePhase=ELDLifePhase::Left;TestEqualSensitive(TEXT("A departed contender is not shown alive"),LDHUD::LifeStatus(Own,true,0),FString(TEXT("已离开本局")));
    Own.LifePhase=static_cast<ELDLifePhase>(200);TestEqualSensitive(TEXT("An unknown phase is reported, not shown alive"),LDHUD::LifeStatus(Own,true,0),FString(TEXT("生命状态未知")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDRegenDisplayTest,"Lockdown.Rules.HUDRegenDisplay",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDRegenDisplayTest::RunTest(const FString& Parameters)
{
    FLDOwnerState Own;Own.Match=2;Own.Life=3;Own.LifePhase=ELDLifePhase::Alive;
    FLDCombatOwnerView View;View.Generation=2;View.Life=3;View.bHasBody=true;
    View.HPP2=View.MaxHPP2=62000;View.MPP2=View.MaxMPP2=30000;
    View.HPRegenPerSec=2.5;View.MPRegenPerSec=1.2;
    TestEqualSensitive(TEXT("Full HP still displays regen capacity"),LDHUD::RegenText(Own,&View,true),FString(TEXT("+2.5/s")));
    TestEqualSensitive(TEXT("MP has its own rate"),LDHUD::RegenText(Own,&View,false),FString(TEXT("+1.2/s")));
    View.HPP2=100;View.HPRegenPerSec=12.345;
    TestEqualSensitive(TEXT("Rate changes render to one decimal independent of HP deltas"),LDHUD::RegenText(Own,&View,true),FString(TEXT("+12.3/s")));
    View.MPRegenPerSec=0;
    TestEqualSensitive(TEXT("A known zero is explicit"),LDHUD::RegenText(Own,&View,false),FString(TEXT("+0.0/s")));
    TestTrue(TEXT("Missing projection does not invent zero"),LDHUD::RegenText(Own,nullptr,true).IsEmpty());
    View.Generation=1;TestTrue(TEXT("Old match is hidden"),LDHUD::RegenText(Own,&View,true).IsEmpty());View.Generation=2;
    View.Life=2;TestTrue(TEXT("Old life is hidden"),LDHUD::RegenText(Own,&View,true).IsEmpty());View.Life=3;
    Own.LifePhase=ELDLifePhase::Dead;TestTrue(TEXT("Death hides regen even with a lingering live view"),LDHUD::RegenText(Own,&View,true).IsEmpty());Own.LifePhase=ELDLifePhase::Alive;
    View.bHasBody=false;TestTrue(TEXT("Absent body hides regen"),LDHUD::RegenText(Own,&View,true).IsEmpty());View.bHasBody=true;
    View.HPP2=0;TestTrue(TEXT("Pending death hides regen"),LDHUD::RegenText(Own,&View,true).IsEmpty());View.HPP2=100;
    View.MPP2=-1;TestTrue(TEXT("Invalid MP resource hides regen"),LDHUD::RegenText(Own,&View,false).IsEmpty());View.MPP2=0;
    View.MaxMPP2=0;TestTrue(TEXT("Unknown MP maximum hides regen"),LDHUD::RegenText(Own,&View,false).IsEmpty());
    View.HPRegenPerSec=std::numeric_limits<double>::quiet_NaN();TestTrue(TEXT("Nonfinite rate is hidden"),LDHUD::RegenText(Own,&View,true).IsEmpty());
    View.HPRegenPerSec=-1;TestTrue(TEXT("Malformed negative rate is hidden"),LDHUD::RegenText(Own,&View,true).IsEmpty());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDOwnResourcesTest,"Lockdown.Rules.HUDOwnResources",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDOwnResourcesTest::RunTest(const FString& Parameters)
{
    const FString Health(LDTerms::Health),Mana(LDTerms::Mana),Experience(LDTerms::Experience);
    const LDHUD::FResourceView Shown=LDHUD::Resource(Health,12.34,100,true);
    TestTrue(TEXT("Live health rounds up so a living body never reads zero"),Shown.bKnown&&Shown.Text==Health+TEXT(" 13/100")&&FMath::IsNearlyEqual(Shown.Fraction,.1234f,.0001f));
    TestEqualSensitive(TEXT("Mana rounds down so a cost never looks affordable early"),LDHUD::Resource(Mana,7.9,30,false).Text,Mana+TEXT(" 7/30"));
    const LDHUD::FResourceView Unset=LDHUD::Resource(Health,0,0,true);
    TestTrue(TEXT("An unprojected maximum is syncing, never 0/0"),!Unset.bKnown&&Unset.Text==Health+TEXT(" 同步中")&&Unset.Fraction==0.f);
    TestFalse(TEXT("A non-finite value is not shown as a resource"),LDHUD::Resource(Health,std::numeric_limits<double>::quiet_NaN(),100,true).bKnown);
    TestFalse(TEXT("A negative P2 value is not shown as a resource"),LDHUD::ResourceP2(Mana,-1,100,false).bKnown);
    TestEqualSensitive(TEXT("P2 encodings are hundredths of a point"),LDHUD::ResourceP2(Health,59950,60000,true).Text,Health+TEXT(" 600/600"));
    const LDHUD::FLevelView Mid=LDHUD::Level(3,10,200,180,true,300);
    TestTrue(TEXT("Experience shows progress inside the current level"),Mid.Level==TEXT("等级 3")&&Mid.Experience==Experience+TEXT(" 20/120")&&FMath::IsNearlyEqual(Mid.Fraction,1.f/6.f,.0001f)&&!Mid.bMax);
    const LDHUD::FLevelView Cap=LDHUD::Level(10,10,1500,1440,false,0);
    TestTrue(TEXT("The level cap shows MAX instead of an eleventh threshold"),Cap.bMax&&Cap.Level==TEXT("等级 10")&&Cap.Experience==Experience+TEXT(" MAX"));
    TestEqualSensitive(TEXT("Experience below the level threshold is a data error, not progress"),LDHUD::Level(3,10,100,180,true,300).Experience,Experience+TEXT("数据异常"));
    TestEqualSensitive(TEXT("An invalid level is not shown as level one"),LDHUD::Level(0,10,0,0,true,80).Level,FString(TEXT("等级 —")));
    TestEqualSensitive(TEXT("Zero remainder reads zero"),LDHUD::TenthsUp(0),FString(TEXT("0.0s")));
    TestEqualSensitive(TEXT("Any live remainder reads at least a tenth"),LDHUD::TenthsUp(1),FString(TEXT("0.1s")));
    TestEqualSensitive(TEXT("Remainders round up to the next tenth"),LDHUD::TenthsUp(101),FString(TEXT("0.2s")));
    TestTrue(TEXT("Development names the closed capture point"),LDHUD::PhaseLine(ELDMatchPhase::Development).Contains(LDTerms::CapturePoint));
    TestEqualSensitive(TEXT("#198: a rest has its own phase line"),LDHUD::PhaseLine(ELDMatchPhase::Rest),FString(LDTerms::RestPhase));
    TestTrue(TEXT("An authority fault is stated explicitly"),LDHUD::PhaseLine(ELDMatchPhase::Faulted).Contains(TEXT("故障")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDActionViewTest,"Lockdown.Rules.HUDActionView",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDActionViewTest::RunTest(const FString& Parameters)
{
    LDHUD::FActionContext Context;Context.KeyLabel=TEXT("Q");Context.bOwnContext=true;Context.bPlaying=true;Context.bAlive=true;Context.bClock=true;Context.NowMs=4800;Context.bManaKnown=true;Context.ManaP2=10000;
    LDHUD::FActionInput Bolt;Bolt.Slot=LDHUD::EActionSlot::Q;Bolt.bProjected=true;Bolt.bHasSource=true;Bolt.Name=TEXT("技能甲");Bolt.ManaCostP2=5000;
    LDHUD::FActionView View=LDHUD::Action(Bolt,Context);
    TestTrue(TEXT("A ready learned active can be sent and shows its name without a skill level"),View.bCanSubmitIntent&&View.ExecutionBlocks==0&&View.State==TEXT("就绪")&&View.Name==TEXT("技能甲")&&View.Kind==TEXT("角色主动"));
    Bolt.ReadyAtMs=8000;Bolt.CooldownMs=8000;View=LDHUD::Action(Bolt,Context);
    TestTrue(TEXT("Cooldown is an execution limit with its own track remainder"),(View.ExecutionBlocks&LDHUD::ActionCooldown)&&View.State==TEXT("冷却 3.2s")&&View.CooldownRemainingMs==3200&&FMath::IsNearlyEqual(View.CooldownFraction,.4f,.0001f));
    TestTrue(TEXT("Cooldown does not suppress the explicit intent"),View.bCanSubmitIntent);
    Context.ManaP2=4000;View=LDHUD::Action(Bolt,Context);
    TestTrue(TEXT("Several limits are all reported while the state shows the first"),(View.ExecutionBlocks&LDHUD::ActionCooldown)&&(View.ExecutionBlocks&LDHUD::ActionMana)&&View.State.StartsWith(TEXT("冷却")));
    Bolt.ReadyAtMs=0;View=LDHUD::Action(Bolt,Context);
    TestTrue(TEXT("Insufficient mana greys and explains without blocking the intent"),View.State==LDTerms::NotEnoughMana()&&View.bCanSubmitIntent);
    Context.ManaP2=10000;
    LDHUD::FActionInput Empty;Empty.Slot=LDHUD::EActionSlot::W;Empty.bProjected=true;View=LDHUD::Action(Empty,Context);
    TestTrue(TEXT("An empty learned slot is an explicit EmptySource attempt, still sent"),View.Name==TEXT("空槽")&&(View.ExecutionBlocks&LDHUD::ActionEmpty)&&View.bCanSubmitIntent);
    Empty.Slot=LDHUD::EActionSlot::Dash;View=LDHUD::Action(Empty,Context);
    TestTrue(TEXT("An unbound dash says it is unavailable and is still sent"),View.Name==FString(LDTerms::Dash)+TEXT("不可用")&&View.Kind==TEXT("通用冲刺")
        &&View.State==View.Name&&(View.ExecutionBlocks&LDHUD::ActionEmpty)&&View.bCanSubmitIntent);
    Bolt.bControlled=true;View=LDHUD::Action(Bolt,Context);
    TestTrue(TEXT("A control limit is explained"),(View.ExecutionBlocks&LDHUD::ActionControlled)&&View.State==TEXT("受控制无法使用"));
    Bolt.bControlled=false;Bolt.ServerBlock=TEXT("目标不可用");View=LDHUD::Action(Bolt,Context);
    TestTrue(TEXT("A published server reason is shown as display only"),(View.ExecutionBlocks&LDHUD::ActionServer)&&View.State==TEXT("目标不可用")&&View.bCanSubmitIntent);
    Bolt.ServerBlock.Reset();Bolt.ReadyAtMs=8000;Context.bClock=false;View=LDHUD::Action(Bolt,Context);
    TestTrue(TEXT("Without the authority clock a cooldown is syncing, never ready"),(View.ExecutionBlocks&LDHUD::ActionUnsynced)&&View.State==TEXT("冷却时间同步中"));
    Context.bClock=true;Bolt.ReadyAtMs=0;
    LDHUD::FActionInput Unprojected=Bolt;Unprojected.bProjected=false;View=LDHUD::Action(Unprojected,Context);
    TestTrue(TEXT("Without the owner combat projection nothing is guessed or sent"),!View.bCanSubmitIntent&&View.State==TEXT("未同步"));
    Context.bAlive=false;View=LDHUD::Action(Bolt,Context);
    TestTrue(TEXT("A dead owner cannot send combat intents"),!View.bCanSubmitIntent&&View.State==LDTerms::Dead);
    Context.bAlive=true;Context.bInputCaptured=true;View=LDHUD::Action(Bolt,Context);
    TestTrue(TEXT("A higher UI layer owning input suppresses sending, not the readiness display"),!View.bCanSubmitIntent&&View.State==TEXT("就绪"));
    Context.bInputCaptured=false;Context.bOwnContext=false;View=LDHUD::Action(Bolt,Context);
    TestTrue(TEXT("A stale match or life context never sends"),!View.bCanSubmitIntent&&(View.ExecutionBlocks&LDHUD::ActionUnsynced));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDMatchTimerTest,"Lockdown.Rules.HUDMatchTimer",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDMatchTimerTest::RunTest(const FString& Parameters)
{
    FLDPublicMatchView Public;Public.Generation=4;Public.Phase=ELDMatchPhase::Rest;Public.StartedAtMs=50000;
    TestEqualSensitive(TEXT("A clock behind the match start reads 0:00"),LDHUD::MatchTimer(Public,true,46000),FString(TEXT("0:00")));
    TestEqualSensitive(TEXT("The match start (the opening rest) reads 0:00"),LDHUD::MatchTimer(Public,true,50000),FString(TEXT("0:00")));
    Public.Phase=ELDMatchPhase::Development;
    TestEqualSensitive(TEXT("Elapsed seconds round down"),LDHUD::MatchTimer(Public,true,50999),FString(TEXT("0:00")));
    TestEqualSensitive(TEXT("Minutes carry no leading zero"),LDHUD::MatchTimer(Public,true,62000),FString(TEXT("0:12")));
    TestEqualSensitive(TEXT("Seconds keep two digits"),LDHUD::MatchTimer(Public,true,115000),FString(TEXT("1:05")));
    TestEqualSensitive(TEXT("Ten minutes and more show their own digits"),LDHUD::MatchTimer(Public,true,50000+725000),FString(TEXT("12:05")));
    TestTrue(TEXT("Without the authority clock no time is guessed"),LDHUD::MatchTimer(Public,false,62000).IsEmpty());
    Public.StartedAtMs=0;
    TestTrue(TEXT("Without a published match start no time is shown"),LDHUD::MatchTimer(Public,true,62000).IsEmpty());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDRestTimelineTest,"Lockdown.Rules.HUDRestTimeline",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDRestTimelineTest::RunTest(const FString& Parameters)
{
    // #198: the timeline banner, the PK sign-up window and the rest return countdown come from the public timeline only.
    const FGuid Self=FGuid::NewGuid();
    FLDPublicMatchView Public;Public.Generation=4;Public.Phase=ELDMatchPhase::Rest;
    FLDTimelineView& Timeline=Public.Timeline;
    Timeline.Generation=4;Timeline.Stage=ELDMatchStage::Rest;Timeline.RestIndex=1;Timeline.FieldSegment=1;Timeline.Step=ELDRestStep::Build;
    Timeline.StepStartedAtMs=100000;Timeline.StepEndsAtMs=130000;Timeline.SignupOpensAtMs=120000;
    LDHUD::FTimelineView View=LDHUD::TimelineBanner(Public,Self,true,105000);
    TestEqualSensitive(TEXT("A rest names its index, step and whole seconds left"),View.Headline,
        LDTerms::RestLabel(1)+TEXT(" · ")+LDTerms::RestBuild+TEXT(" · 剩 25 秒"));
    TestTrue(TEXT("Before the sign-up window it says when it opens and where the disk is"),
        View.Detail==LDTerms::SignupOpensIn(15)+TEXT(" · ")+LDTerms::SignupCircleHint&&!View.bSignupOpen&&View.bSignupStep);
    View=LDHUD::TimelineBanner(Public,Self,true,121500);
    TestTrue(TEXT("Inside the window the line counts who stands in a disk and asks Self to step in"),View.bSignupOpen&&!View.bSignedUp
        &&View.Detail==LDTerms::SignupOpenFor(9)+TEXT(" · ")+LDTerms::SignedUpCount(0)+TEXT(" · ")+LDTerms::NotSignedUpSelf);
    Timeline.Signups={Self};
    View=LDHUD::TimelineBanner(Public,Self,true,130000);
    TestTrue(TEXT("The deadline instant is still inside the window; standing inside reads as signed up"),View.bSignupOpen&&View.bSignedUp
        &&View.Detail.Contains(LDTerms::SignedUpCount(1))&&View.Detail.Contains(LDTerms::SignedUpSelf));
    TestFalse(TEXT("Another contender's sign-up is not the own one"),LDHUD::TimelineBanner(Public,FGuid::NewGuid(),true,125000).bSignedUp);
    View=LDHUD::TimelineBanner(Public,Self,false,0);
    TestTrue(TEXT("Without the authority clock nothing is guessed and the window is not open"),!View.bSignupOpen&&View.Headline.EndsWith(TEXT("时间同步中"))
        &&View.Detail.StartsWith(TEXT("PK 报名时间同步中")));
    Timeline.Signups.Reset();
    Timeline.Step=ELDRestStep::PK;Timeline.LockedEntrants=3;Timeline.StepStartedAtMs=140000;Timeline.StepEndsAtMs=200000;
    View=LDHUD::TimelineBanner(Public,Self,true,150000);
    TestTrue(TEXT("A PK names its latest end and its entrants; #222: no build lock to announce"),View.Headline.EndsWith(TEXT("最长剩 50 秒"))
        &&View.Detail==LDTerms::PKEntrants(3)&&!View.bSignupOpen);
    Timeline.Step=ELDRestStep::Payout;
    TestEqualSensitive(TEXT("The payout names the entrants the same way"),LDHUD::TimelineBanner(Public,Self,true,150000).Detail,LDTerms::PKEntrants(3));
    TestTrue(TEXT("No return countdown outside the return step"),LDHUD::RestReturnCountdown(Public,true,150000).IsEmpty());
    Timeline.Step=ELDRestStep::Return;Timeline.bPKSkipped=true;Timeline.LockedEntrants=1;Timeline.StepStartedAtMs=130000;Timeline.StepEndsAtMs=135000;
    View=LDHUD::TimelineBanner(Public,Self,true,131200);
    TestTrue(TEXT("Fewer than two sign-ups skip the PK"),View.Headline==LDTerms::RestLabel(1)+TEXT(" · ")+LDTerms::RestReturn+TEXT(" · 剩 4 秒")
        &&View.Detail==FString(LDTerms::PKSkipped));
    TestEqualSensitive(TEXT("The return countdown shows whole seconds rounded up"),LDHUD::RestReturnCountdown(Public,true,131200),FString(TEXT("4")));
    TestEqualSensitive(TEXT("The last millisecond still reads 1"),LDHUD::RestReturnCountdown(Public,true,134999),FString(TEXT("1")));
    TestEqualSensitive(TEXT("Reaching the end holds 1 until the authority enters the field"),LDHUD::RestReturnCountdown(Public,true,136000),FString(TEXT("1")));
    TestTrue(TEXT("Without the authority clock no countdown is guessed"),LDHUD::RestReturnCountdown(Public,false,131200).IsEmpty());
    // The field: T runs from the segment start; "开淫！" greets each field segment.
    Public.Phase=ELDMatchPhase::Development;
    Timeline.Stage=ELDMatchStage::Field;Timeline.Step=ELDRestStep::None;Timeline.FieldBaseMs=180000;Timeline.FieldStartedAtMs=200000;Timeline.FieldEndT=360000;
    Timeline.StepStartedAtMs=200000;Timeline.StepEndsAtMs=380000;
    View=LDHUD::TimelineBanner(Public,Self,true,200500);
    TestEqualSensitive(TEXT("A field segment counts down to the next rest in field time"),View.Headline,LDTerms::FieldSegmentLine(2,3,TEXT("3:00")));
    TestTrue(TEXT("The field has no rest detail and no toggle"),View.Detail.IsEmpty()&&!View.bSignupOpen);
    TestEqualSensitive(TEXT("A field segment starts with the go line"),LDHUD::RestReturnCountdown(Public,true,200500),FString(LDTerms::CountdownGo));
    TestTrue(TEXT("The go line leaves after its moment"),LDHUD::RestReturnCountdown(Public,true,200000+LDHUD::CountdownGoShowMs).IsEmpty());
    Public.Phase=ELDMatchPhase::CaptureOpen;Timeline.Stage=ELDMatchStage::FinalField;Timeline.FieldSegment=3;Timeline.FieldBaseMs=540000;Timeline.FieldEndT=-1;
    TestEqualSensitive(TEXT("The final field names the open flagpole"),LDHUD::TimelineBanner(Public,Self,true,250000).Headline,FString(LDTerms::FinalField));
    Timeline.Generation=3;
    TestTrue(TEXT("Another generation's timeline shows nothing"),LDHUD::TimelineBanner(Public,Self,true,250000).Headline.IsEmpty()
        &&LDHUD::RestReturnCountdown(Public,true,200500).IsEmpty());
    // The PK sign-up has no answer to show: standing in the own disk is the sign-up, read from the public timeline above.
    // The personal training reset: a queued reset says what it does; every refusal is explicit, never a success.
    TestEqualSensitive(TEXT("A queued reset refills both and readies the cooldowns"),LDHUD::TrainingResetText(ELDTrainingResetResult::Queued),
        FString(TEXT("训练已重置：你和假人回满，冷却就绪")));
    TestEqualSensitive(TEXT("No dummy says the rest spot is taken"),LDHUD::TrainingResetText(ELDTrainingResetResult::NoDummy),
        FString(TEXT("没有"))+LDTerms::TrainingDummy+TEXT("（休息位被占）"));
    for(const ELDTrainingResetResult Refused:{ELDTrainingResetResult::NotInRest,ELDTrainingResetResult::NotContender,ELDTrainingResetResult::StaleMatch,ELDTrainingResetResult::None})
    {
        const FString Text=LDHUD::TrainingResetText(Refused);
        TestTrue(FString::Printf(TEXT("Training reset refusal %d is explicit"),int32(Refused)),!Text.IsEmpty()&&!Text.Contains(TEXT("已重置")));
    }
    // An unlisted contender-kind unit is the own training dummy only in a rest; a listed one keeps its roster name.
    FLDLobbyView Lobby;FLDLobbyMember& Member=Lobby.Members.AddDefaulted_GetRef();Member.ContenderId=Self;Member.Name=TEXT("玩家甲");
    TestEqualSensitive(TEXT("A listed contender keeps its roster name in a rest"),LDHUD::ObservedContenderName(Lobby,Self,true),FString(TEXT("玩家甲")));
    TestEqualSensitive(TEXT("An unlisted contender in a rest is the training dummy"),LDHUD::ObservedContenderName(Lobby,FGuid::NewGuid(),true),FString(LDTerms::TrainingDummy));
    TestTrue(TEXT("Outside a rest an unlisted contender is not given a name"),LDHUD::ObservedContenderName(Lobby,FGuid::NewGuid(),false).IsEmpty());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDPKViewTest,"Lockdown.Rules.HUDPKView",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDPKViewTest::RunTest(const FString& Parameters)
{
    // #198 session D: the PK panel, the spectator camera predicate and every bet/throw answer come from the public PK view.
    TestEqualSensitive(TEXT("Two entrants duel"),LDHUD::PKFormatText(ELDPKFormat::Duel,2,2),FString(LDTerms::PKDuel));
    TestEqualSensitive(TEXT("A free-for-all reads 混战"),LDHUD::PKFormatText(ELDPKFormat::FreeForAll,5,5),FString(LDTerms::PKFreeForAll));
    TestEqualSensitive(TEXT("Four entrants in two teams are 2v2"),LDHUD::PKFormatText(ELDPKFormat::Teams,2,4),FString(TEXT("2v2")));
    TestEqualSensitive(TEXT("Six entrants in two teams are 3v3"),LDHUD::PKFormatText(ELDPKFormat::Teams,2,6),FString(TEXT("3v3")));
    TestTrue(TEXT("No format or an uneven team split names nothing"),LDHUD::PKFormatText(ELDPKFormat::None,0,0).IsEmpty()
        &&LDHUD::PKFormatText(ELDPKFormat::Teams,2,5).IsEmpty());

    const FGuid A=FGuid::NewGuid(),B=FGuid::NewGuid(),C=FGuid::NewGuid(),D=FGuid::NewGuid(),Watcher=FGuid::NewGuid(),Late=FGuid::NewGuid();
    FLDLobbyView Lobby;
    const TPair<FGuid,const TCHAR*> Listed[]={{A,TEXT("甲")},{B,TEXT("乙")},{C,TEXT("丙")},{D,TEXT("丁")},{Watcher,TEXT("戊")}};
    for(const TPair<FGuid,const TCHAR*>& Entry:Listed){FLDLobbyMember& Member=Lobby.Members.AddDefaulted_GetRef();Member.ContenderId=Entry.Key;Member.Name=Entry.Value;}
    {
        FLDLobbyView DuplicateNames;
        const FGuid First=FGuid::NewGuid(),Second=FGuid::NewGuid();
        FLDLobbyMember& One=DuplicateNames.Members.AddDefaulted_GetRef();One.ContenderId=First;One.Name=TEXT("玩家");
        FLDLobbyMember& Two=DuplicateNames.Members.AddDefaulted_GetRef();Two.ContenderId=Second;Two.Name=TEXT("玩家");
        TestEqualSensitive(TEXT("Duplicate default roster names are disambiguated in published order"),
            LDHUD::RosterName(DuplicateNames,First),FString(TEXT("玩家 1")));
        TestEqualSensitive(TEXT("The second duplicate default roster name has a stable suffix"),
            LDHUD::RosterName(DuplicateNames,Second),FString(TEXT("玩家 2")));
    }
    FLDPublicMatchView Public;Public.Generation=7;Public.Phase=ELDMatchPhase::Rest;
    FLDTimelineView& Timeline=Public.Timeline;
    Timeline.Generation=7;Timeline.Stage=ELDMatchStage::Rest;Timeline.RestIndex=2;Timeline.Step=ELDRestStep::Betting;
    Timeline.StepStartedAtMs=100000;Timeline.StepEndsAtMs=108000;
    FLDPKView& PK=Public.PK;
    PK.Generation=7;PK.Serial=2;PK.RestIndex=2;PK.Format=ELDPKFormat::Teams;PK.SideCount=2;PK.Outcome=ELDPKOutcome::Pending;
    PK.ArenaCentreCm=FVector(100,200,0);
    const TPair<FGuid,uint8> Seats[]={TPair<FGuid,uint8>(A,uint8(1)),TPair<FGuid,uint8>(B,uint8(2)),TPair<FGuid,uint8>(C,uint8(1)),TPair<FGuid,uint8>(D,uint8(2))};
    for(const TPair<FGuid,uint8>& Seat:Seats)
    {FLDPKEntrantView& Entrant=PK.Entrants.AddDefaulted_GetRef();Entrant.Contender=Seat.Key;Entrant.Side=Seat.Value;}
    PK.Bettors={Watcher};
    TestEqualSensitive(TEXT("The public team PK labels a same-side observed entrant as a teammate"),
        LDHUD::PKRelationText(Public,A,C),FString(LDTerms::PKTeammate));
    TestEqualSensitive(TEXT("The public team PK labels an opposing observed entrant as an enemy"),
        LDHUD::PKRelationText(Public,A,B),FString(LDTerms::PKEnemy));
    TestEqualSensitive(TEXT("A spectator sees a side label, not a fabricated friendly/enemy relation"),
        LDHUD::PKRelationText(Public,Watcher,A),LDTerms::PKSide(1));
    FLDPublicMatchView FreeForAll=Public;FreeForAll.PK.Format=ELDPKFormat::FreeForAll;
    TestTrue(TEXT("Team labels do not change free-for-all semantics"),LDHUD::PKRelationText(FreeForAll,A,B).IsEmpty());

    // Betting: an eligible spectator gets one button per side; an entrant or a contender outside the locked bettors gets none.
    LDHUD::FPKPanelInput In;In.Self=Watcher;In.bClock=true;In.NowMs=103000;
    LDHUD::FPKPanelView View=LDHUD::PKPanel(Public,Lobby,In);
    TestTrue(TEXT("The betting step shows the format and one bet per side to an eligible bettor"),View.bVisible&&View.bCanBet&&!View.bCanThrow
        &&View.Title==LDTerms::PKTitle(2,TEXT("2v2"),4)&&View.Sides.Num()==2&&View.Sides[0].BetAction==LDTerms::PKBetOn(TEXT("甲、丙"))
        &&View.Sides[1].BetAction==LDTerms::PKBetOn(TEXT("乙、丁"))&&View.Role==LDTerms::PKRoleBet&&View.OwnBet==0);
    FLDPKBetView& OwnBet=PK.Bets.AddDefaulted_GetRef();OwnBet.Spectator=Watcher;OwnBet.Side=2;
    View=LDHUD::PKPanel(Public,Lobby,In);
    TestTrue(TEXT("The own bet comes from the public bets and is marked on its side"),View.OwnBet==2&&View.Sides[1].bOwnBet&&!View.Sides[0].bOwnBet
        &&View.Sides[1].Line.Contains(LDTerms::PKBetCount(1))&&View.Sides[1].Line.Contains(LDTerms::PKOwnBet));
    In.Self=A;View=LDHUD::PKPanel(Public,Lobby,In);
    TestTrue(TEXT("An entrant sees its side but no bet"),View.bVisible&&!View.bCanBet&&View.Role==LDTerms::PKRoleEntrant&&View.Sides[0].bOwnSide);
    In.Self=Late;
    TestFalse(TEXT("A contender outside the locked bettors cannot bet"),LDHUD::PKPanel(Public,Lobby,In).bCanBet);
    TestFalse(TEXT("Betting is not spectating yet"),LDHUD::IsSpectatingPK(Public,Watcher));

    // PKReady: the entrants stand in the arena; everyone else watches it.
    Timeline.Step=ELDRestStep::PKReady;Timeline.StepStartedAtMs=108000;Timeline.StepEndsAtMs=111000;
    for(FLDPKEntrantView& Entrant:PK.Entrants){Entrant.bAlive=true;}
    TestTrue(TEXT("Spectators watch the arena from PKReady"),LDHUD::IsSpectatingPK(Public,Watcher)&&LDHUD::IsSpectatingPK(Public,Late));
    TestFalse(TEXT("A living entrant keeps its own camera"),LDHUD::IsSpectatingPK(Public,A));
    TestFalse(TEXT("No contender id never spectates"),LDHUD::IsSpectatingPK(Public,FGuid()));
    In.Self=A;In.NowMs=108100;View=LDHUD::PKPanel(Public,Lobby,In);
    TestTrue(TEXT("PKReady counts whole seconds to the fight"),View.Countdown==LDTerms::PKStartsIn(3)&&View.Role==LDTerms::PKRoleFighter&&!View.bCanBet);
    In.NowMs=111500;
    TestEqualSensitive(TEXT("A local clock past the end holds 1 until the authority starts the fight"),LDHUD::PKPanel(Public,Lobby,In).Countdown,LDTerms::PKStartsIn(1));
    In.bClock=false;
    TestEqualSensitive(TEXT("Without the authority clock no countdown is guessed"),LDHUD::PKPanel(Public,Lobby,In).Countdown,FString(LDTerms::PKClockSync));
    In.bClock=true;

    // The fight: the ring, the throw and the eliminated entrant.
    Timeline.Step=ELDRestStep::PK;Timeline.StepStartedAtMs=111000;Timeline.StepEndsAtMs=171000;
    PK.FightStartedAtMs=111000;PK.Outcome=ELDPKOutcome::Running;PK.Entrants[1].bAlive=false;
    TestTrue(TEXT("An eliminated entrant watches the arena"),LDHUD::IsSpectatingPK(Public,B)&&!LDHUD::IsSpectatingPK(Public,C));
    TestEqualSensitive(TEXT("An eliminated entrant sees its former teammate's side, not a teammate relation"),
        LDHUD::PKRelationText(Public,B,D),LDTerms::PKSide(2));
    TestEqualSensitive(TEXT("An eliminated entrant sees the other side, not an enemy relation"),
        LDHUD::PKRelationText(Public,B,A),LDTerms::PKSide(1));
    TestTrue(TEXT("Living PK fighters retain teammate and enemy relations"),
        LDHUD::PKRelationText(Public,A,C)==LDTerms::PKTeammate&&LDHUD::PKRelationText(Public,A,D)==LDTerms::PKEnemy);
    In.Self=B;In.NowMs=116000;View=LDHUD::PKPanel(Public,Lobby,In);
    TestTrue(TEXT("The eliminated entrant is read-only and told so"),View.bSpectating&&!View.bCanBet&&!View.bCanThrow&&View.Role==LDTerms::PKRoleOut);
    TestTrue(TEXT("Its name carries the out mark once the fight started"),View.Sides[1].Line.Contains(FString(TEXT("乙（"))+LDTerms::PKOut+TEXT("）"))
        &&!View.Sides[1].bOut);
    FLDPKParameters Ring;
    Ring.ArenaRadiusCm=1800;Ring.MidRadiusCm=600;Ring.FinalRadiusCm=150;
    Ring.ShrinkStartMs=10000;Ring.MidMs=40000;Ring.FinalMs=55000;Ring.PreviewMs=10000;Ring.MaxMs=60000;Ring.TickMs=1000;
    In.Self=Watcher;In.Params=&Ring;View=LDHUD::PKPanel(Public,Lobby,In);
    TestEqualSensitive(TEXT("The PK counts down to its latest end"),View.Countdown,LDTerms::PKTimeLeft(55));
    TestEqualSensitive(TEXT("Before the shrink the ring names the next radius and when it starts"),View.Ring,
        LDTerms::PKRingNow(TEXT("18.0"))+TEXT(" · ")+LDTerms::PKRingSoon(TEXT("6.0"),5));
    TestTrue(TEXT("A ready eligible spectator may throw"),View.bCanThrow&&View.bThrowReady&&View.ThrowAction==LDTerms::PKThrow
        &&View.Role==LDTerms::PKRoleThrow&&View.bSpectating);
    In.NowMs=136000;
    TestEqualSensitive(TEXT("While shrinking the ring names where it is heading"),LDHUD::PKPanel(Public,Lobby,In).Ring,
        LDTerms::PKRingNow(TEXT("12.0"))+TEXT(" · ")+LDTerms::PKRingShrinking(TEXT("6.0")));
    In.NowMs=116000;In.bThrowArmed=true;
    TestEqualSensitive(TEXT("An armed throw asks for the ground"),LDHUD::PKPanel(Public,Lobby,In).ThrowAction,FString(LDTerms::PKThrowAim));
    In.bThrowArmed=false;In.ThrowReadyAtMs=119800;View=LDHUD::PKPanel(Public,Lobby,In);
    TestTrue(TEXT("A cooling throw shows its remainder and cannot be pressed"),View.bCanThrow&&!View.bThrowReady&&View.ThrowAction==LDTerms::PKThrowCooling(TEXT("3.8")));
    TestEqualSensitive(TEXT("The throw cooldown reads whole seconds from ten"),LDHUD::PKThrowCooldown(126000,true,116000),LDTerms::PKThrowCooling(TEXT("10")));
    TestTrue(TEXT("A reached or unknown cooldown is ready"),LDHUD::PKThrowCooldown(119800,true,119800).IsEmpty()&&LDHUD::PKThrowCooldown(0,true,116000).IsEmpty());
    TestEqualSensitive(TEXT("Without the authority clock the cooldown is not guessed"),LDHUD::PKThrowCooldown(119800,false,0),FString(LDTerms::PKThrowCoolingSync));
    In.ThrowReadyAtMs=0;In.bThrowInFlight=true;View=LDHUD::PKPanel(Public,Lobby,In);
    TestTrue(TEXT("A throw waiting for its answer cannot be pressed again"),!View.bThrowReady&&View.ThrowAction==LDTerms::PKThrowSending);
    In.bThrowInFlight=false;

    // Answers: only a placed bet and a thrown throw read as done; every refusal is explicit.
    TestTrue(TEXT("A placed bet says so"),LDHUD::PKBetText(ELDPKBetResult::Placed).StartsWith(TEXT("已押注")));
    TSet<FString> BetTexts;
    for(const ELDPKBetResult Refused:{ELDPKBetResult::NotSpectator,ELDPKBetResult::WindowClosed,ELDPKBetResult::UnknownSide,ELDPKBetResult::StaleMatch,
        ELDPKBetResult::NotContender,ELDPKBetResult::NoPK,ELDPKBetResult::None})
    {
        const FString Text=LDHUD::PKBetText(Refused);BetTexts.Add(Text);
        TestTrue(FString::Printf(TEXT("Bet refusal %d is explicit, never a success"),int32(Refused)),!Text.IsEmpty()&&!Text.Contains(TEXT("已押注")));
    }
    TestEqual(TEXT("Every bet refusal has its own text"),BetTexts.Num(),7);
    TestTrue(TEXT("A thrown throw says so"),LDHUD::PKThrowText(ELDPKThrowResult::Thrown).StartsWith(TEXT("已投掷")));
    TSet<FString> ThrowTexts;
    for(const ELDPKThrowResult Refused:{ELDPKThrowResult::NotSpectator,ELDPKThrowResult::NotRunning,ELDPKThrowResult::CoolingDown,ELDPKThrowResult::OutsideArena,
        ELDPKThrowResult::OutOfRange,ELDPKThrowResult::NotGround,ELDPKThrowResult::StaleMatch,ELDPKThrowResult::NotContender,ELDPKThrowResult::NoPK,
        ELDPKThrowResult::None})
    {
        const FString Text=LDHUD::PKThrowText(Refused);ThrowTexts.Add(Text);
        TestTrue(FString::Printf(TEXT("Throw refusal %d is explicit, never a success"),int32(Refused)),!Text.IsEmpty()&&!Text.Contains(TEXT("已投掷")));
    }
    TestEqual(TEXT("Every throw refusal has its own text"),ThrowTexts.Num(),10);

    // Settlement: the frozen result and the awards, the own row highlighted.
    Timeline.Step=ELDRestStep::Settlement;Timeline.StepStartedAtMs=140000;Timeline.StepEndsAtMs=143000;
    PK.Outcome=ELDPKOutcome::Won;PK.WinnerSide=1;PK.DecidedAtMs=140000;PK.Bets[0].Side=1;
    const auto Award=[&PK](const FGuid& Id,uint8 Participation,uint8 Winner,uint8 Bet)
    {FLDPKAwardView& Row=PK.Awards.AddDefaulted_GetRef();Row.Contender=Id;Row.Participation=Participation;Row.Winner=Winner;Row.Bet=Bet;};
    Award(A,1,2,0);Award(C,1,1,0);Award(B,1,0,0);Award(D,0,0,0);Award(Watcher,0,0,1);
    In.NowMs=141000;View=LDHUD::PKPanel(Public,Lobby,In);
    TestTrue(TEXT("The settlement freezes the winning side"),View.bResult&&View.Result==LDTerms::PKWinners(TEXT("甲、丙"))&&View.Sides[0].bWinner
        &&!View.Sides[1].bWinner&&!View.bCanThrow&&!View.bCanBet&&View.Countdown.IsEmpty()&&!View.bOwnWin);
    TestTrue(TEXT("Awards list every paid contender and mark the own row"),View.Awards.Num()==4&&View.Awards[3].bSelf&&!View.Awards[0].bSelf
        &&View.Awards[3].Name==TEXT("戊")&&View.Awards[3].Points==FString(LDTerms::PKAwardBet)+TEXT(" +1 · ")+LDTerms::PKAwardTotal(1));
    TestEqualSensitive(TEXT("An award row names each part and the total"),View.Awards[0].Points,
        FString::Printf(TEXT("%s +1 · %s +2 · %s"),LDTerms::PKAwardParticipation,LDTerms::PKAwardWinner,*LDTerms::PKAwardTotal(3)));
    In.Self=A;
    TestTrue(TEXT("A winning entrant is told it won"),LDHUD::PKPanel(Public,Lobby,In).bOwnWin);
    Timeline.Step=ELDRestStep::Payout;
    TestTrue(TEXT("The payout keeps the result"),LDHUD::PKPanel(Public,Lobby,In).bResult);
    PK.Outcome=ELDPKOutcome::Draw;
    TestEqualSensitive(TEXT("A draw reads 平局"),LDHUD::PKResultText(PK,Lobby),FString(LDTerms::PKDraw));
    PK.Outcome=ELDPKOutcome::NoContest;
    TestTrue(TEXT("No contest names no winner"),LDHUD::PKResultText(PK,Lobby)==LDTerms::PKNoContest&&!LDHUD::PKResultText(PK,Lobby).Contains(TEXT("胜方")));
    PK.Outcome=ELDPKOutcome::Running;
    TestTrue(TEXT("An undecided PK has no result"),LDHUD::PKResultText(PK,Lobby).IsEmpty());
    Timeline.Step=ELDRestStep::PK;PK.Outcome=ELDPKOutcome::NoContest;
    TestFalse(TEXT("A PK without an arena is not watched"),LDHUD::IsSpectatingPK(Public,Watcher));

    // Outside the PK steps, for another generation or without a PK nothing shows.
    PK.Outcome=ELDPKOutcome::Running;Timeline.Step=ELDRestStep::Return;
    TestFalse(TEXT("The return step has no PK panel"),LDHUD::PKPanel(Public,Lobby,In).bVisible);
    Timeline.Step=ELDRestStep::PK;PK.Generation=6;
    TestTrue(TEXT("Another generation's PK shows nothing"),!LDHUD::PKPanel(Public,Lobby,In).bVisible&&!LDHUD::IsSpectatingPK(Public,Watcher));
    PK.Generation=7;PK.Serial=0;
    TestTrue(TEXT("A rest without a PK shows nothing"),!LDHUD::HasPK(Public)&&!LDHUD::PKPanel(Public,Lobby,In).bVisible);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDPublicMatchTest,"Lockdown.Rules.HUDPublicMatch",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDPublicMatchTest::RunTest(const FString& Parameters)
{
    // #198: the flagpole opens when the final field begins; until then its opening instant is unscheduled (MAX_int64).
    FLDPublicMatchView Public;Public.Generation=4;Public.Phase=ELDMatchPhase::Development;Public.OpenAtMs=MAX_int64;
    const FString Closed=FString(LDTerms::Development)+TEXT(" · ")+LDTerms::CaptureAfterLastRest;
    TestEqualSensitive(TEXT("Development says the flagpole opens after the last rest and prints no countdown"),LDHUD::Circle(Public,FString(),FString(),true,500).Line,Closed);
    TestEqualSensitive(TEXT("A running countdown never reads 0:00 before the opening"),LDHUD::Clock(1),FString(TEXT("0:01")));
    TestEqualSensitive(TEXT("Without the authority clock the line is the same"),LDHUD::Circle(Public,FString(),FString(),false,0).Line,Closed);
    TestTrue(TEXT("The closed line names the last rest R3"),Closed.Contains(TEXT("R3"))&&Closed.Contains(LDTerms::CapturePoint));
    Public.Phase=ELDMatchPhase::CaptureOpen;Public.CircleState=ELDCircleState::Empty;
    TestEqualSensitive(TEXT("An open empty zone names no one"),LDHUD::Circle(Public,FString(),FString(),true,0).Line,FString(LDTerms::CircleEmpty));
    Public.CircleState=ELDCircleState::Contested;
    const LDHUD::FCircleView Contested=LDHUD::Circle(Public,TEXT("玩家甲"),FString(),true,0);
    TestTrue(TEXT("A contest without a holder names no one and shows no progress"),Contested.Line==FString(LDTerms::CircleContested)+TEXT(" · ")+LDTerms::NoProgressKept&&!Contested.bProgress);
    {
        FLDPublicMatchView Paused=Public;Paused.Occupier=FGuid::NewGuid();Paused.PausedProgressMs=1201;
        const FString Contest=FString(LDTerms::CircleContested)+TEXT(" · ")+LDTerms::ProgressKept(TEXT("玩家甲"));
        const LDHUD::FCircleView Held=LDHUD::Circle(Paused,TEXT("玩家甲"),FString(),true,0,3500,20000);
        TestTrue(TEXT("A paused sprayer's progress shows against the approved spray in tenths"),
            Held.Line==Contest+TEXT(" 1.2/3.5 s")&&Held.bProgress&&FMath::IsNearlyEqual(Held.Progress,1201.f/3500.f));
        TestTrue(TEXT("Without the approved spray the paused seconds still show, without a bar"),
            LDHUD::Circle(Paused,TEXT("玩家甲"),FString(),true,0).Line==Contest+TEXT("（1.2 s）")&&!LDHUD::Circle(Paused,TEXT("玩家甲"),FString(),true,0).bProgress);
    }
    Public.CircleState=ELDCircleState::Solo;Public.SoloSinceMs=100000;Public.DueAtMs=103500;
    const LDHUD::FCircleView Solo=LDHUD::Circle(Public,TEXT("玩家甲"),FString(),true,101400);
    TestTrue(TEXT("Spray progress runs from the public spray start to its due time"),Solo.bProgress&&FMath::IsNearlyEqual(Solo.Progress,.4f,.0001f)
        &&Solo.Line==FString(TEXT("玩家甲 "))+LDTerms::Holding+TEXT(" · ")+LDTerms::HoldLeft+TEXT(" 2.1s"));
    const LDHUD::FCircleView Due=LDHUD::Circle(Public,TEXT("玩家甲"),FString(),true,104000);
    TestTrue(TEXT("Reaching the due time waits for the authority and never claims a graffiti or a winner"),Due.Progress==1.f&&Due.Line.EndsWith(TEXT("0.0s"))&&!Due.Line.Contains(LDTerms::Winner));
    TestEqualSensitive(TEXT("An unlisted sprayer is not given an invented name"),LDHUD::Circle(Public,FString(),FString(),false,0).Line,FString(TEXT("未列名参赛者 "))+LDTerms::Holding+TEXT(" · 进度同步中"));
    {
        // The graffiti owner and his guard are public in every open state.
        FLDPublicMatchView Owned=Public;Owned.GraffitiOwner=FGuid::NewGuid();Owned.GuardKeptMs=5000;
        const LDHUD::FCircleView Repaint=LDHUD::Circle(Owned,TEXT("玩家甲"),TEXT("玩家乙"),true,101400,3500,20000);
        TestTrue(TEXT("A sole sprayer's bar shows his spray; the line names the paused guard of the graffiti owner"),
            Repaint.bProgress&&FMath::IsNearlyEqual(Repaint.Progress,.4f,.0001f)
            &&Repaint.Line==FString(TEXT("玩家甲 "))+LDTerms::Holding+TEXT(" · ")+LDTerms::HoldLeft+TEXT(" 2.1s · ")+LDTerms::GraffitiOf(TEXT("玩家乙"))
                +TEXT(" · ")+LDTerms::GuardPaused+TEXT("（")+LDTerms::GuardLeft+TEXT(" 15.0s）"));
        FLDPublicMatchView Guarded=Owned;Guarded.CircleState=ELDCircleState::Guarded;Guarded.Occupier.Invalidate();
        Guarded.SoloSinceMs=Guarded.DueAtMs=Guarded.GuardKeptMs=0;Guarded.GuardDueAtMs=120000;
        const LDHUD::FCircleView Guard=LDHUD::Circle(Guarded,FString(),TEXT("玩家乙"),true,105000,3500,20000);
        TestTrue(TEXT("A running guard counts down to its public win instant"),Guard.bProgress&&FMath::IsNearlyEqual(Guard.Progress,.25f,.0001f)
            &&Guard.Line==LDTerms::GraffitiOf(TEXT("玩家乙"))+TEXT(" · ")+LDTerms::GuardLeft+TEXT(" 15.0s"));
        Guarded.CircleState=ELDCircleState::Empty;Guarded.GuardDueAtMs=0;Guarded.GuardKeptMs=5000;
        const LDHUD::FCircleView Away=LDHUD::Circle(Guarded,FString(),TEXT("玩家乙"),true,105000,3500,20000);
        TestTrue(TEXT("2026-11-07: an owner away from the empty zone pauses his guard at its kept value"),
            Away.Line==LDTerms::GraffitiOf(TEXT("玩家乙"))+TEXT(" · ")+LDTerms::GuardAway+TEXT("（")+LDTerms::GuardLeft+TEXT(" 15.0s）")
            &&Away.bProgress&&FMath::IsNearlyEqual(Away.Progress,.25f,.0001f));
        Guarded.CircleState=ELDCircleState::Contested;
        const LDHUD::FCircleView Frozen=LDHUD::Circle(Guarded,FString(),TEXT("玩家乙"),true,105000,3500,20000);
        TestTrue(TEXT("A contest beside the owner names the frozen guard"),Frozen.Line==FString(LDTerms::CircleContested)+TEXT(" · ")+LDTerms::NoProgressKept
            +TEXT(" · ")+LDTerms::GraffitiOf(TEXT("玩家乙"))+TEXT(" · ")+LDTerms::GuardPaused+TEXT("（")+LDTerms::GuardLeft+TEXT(" 15.0s）")
            &&Frozen.bProgress&&FMath::IsNearlyEqual(Frozen.Progress,.25f,.0001f));
        Guarded.CircleState=ELDCircleState::Guarded;Guarded.GuardDueAtMs=120000;Guarded.GuardKeptMs=0;
        TestFalse(TEXT("A running guard never claims a winner"),LDHUD::Circle(Guarded,FString(),TEXT("玩家乙"),true,125000,3500,20000).Line.Contains(LDTerms::Winner));
    }
    FLDKillNotice Notice;Notice.Cause=ELDKillCause::Contender;
    TestEqualSensitive(TEXT("A contender kill names both public roster names"),LDHUD::KillLine(Notice,TEXT("乙"),TEXT("甲"),LDTerms::CampLow,LDTerms::CampHigh,LDTerms::CampElite,LDTerms::Boss),LDTerms::KillLine(TEXT("甲"),TEXT("乙")));
    Notice.Cause=ELDKillCause::CampHigh;
    TestEqualSensitive(TEXT("A camp death names the tier creature, not a hidden attacker"),LDHUD::KillLine(Notice,TEXT("乙"),TEXT("甲"),LDTerms::CampLow,LDTerms::CampHigh,LDTerms::CampElite,LDTerms::Boss),LDTerms::KilledByLine(TEXT("乙"),LDTerms::CampHigh));
    Notice.Cause=ELDKillCause::Unowned;
    TestEqualSensitive(TEXT("A death without a contender owner says so"),LDHUD::KillLine(Notice,TEXT("乙"),FString(),LDTerms::CampLow,LDTerms::CampHigh,LDTerms::CampElite,LDTerms::Boss),FString(TEXT("乙 "))+LDTerms::Dead+TEXT("（无参赛者击杀）"));
    Notice.Cause=ELDKillCause::Boss;
    TestEqualSensitive(TEXT("A Boss death names the 画饼老板"),LDHUD::KillLine(Notice,TEXT("乙"),TEXT("甲"),LDTerms::CampLow,LDTerms::CampHigh,LDTerms::CampElite,LDTerms::Boss),
        LDTerms::KilledByLine(TEXT("乙"),LDTerms::Boss));
    FLDResultView Result;Result.Reason=ELDMatchEndReason::GuardSucceeded;Result.Winner=FGuid::NewGuid();
    TestEqualSensitive(TEXT("A guard win names its end reason and crowns the one winner"),LDHUD::ResultHeadline(Result,TEXT("甲")),FString(TEXT("守护成功 · "))+LDTerms::Crowned(TEXT("甲")));
    TestTrue(TEXT("The crowning names the winner and the winner title"),LDTerms::Crowned(TEXT("甲")).StartsWith(TEXT("甲 "))&&LDTerms::Crowned(TEXT("甲")).Contains(LDTerms::Winner));
    Result.Winner.Invalidate();
    TestEqualSensitive(TEXT("A capture result without a winner says so"),LDHUD::ResultHeadline(Result,FString()),FString(LDTerms::NoWinner));
    Result.Reason=ELDMatchEndReason::HostEnded;
    TestEqualSensitive(TEXT("A Host-ended match has no winner"),LDHUD::ResultHeadline(Result,FString()),FString(TEXT("房主结束了本局 · "))+LDTerms::NoCapture);
    FLDResultRow Row;Row.ContenderId=FGuid::NewGuid();Row.Name=TEXT("机器人");Row.Kind=ELDContenderKind::OpponentAI;Row.bLeft=true;Row.Kills=3;Row.Deaths=1;Row.FinalLevel=7;
    const LDHUD::FResultRowView Shown=LDHUD::ResultRow(Row,Row.ContenderId,FGuid());
    TestTrue(TEXT("A frozen row keeps the departed AI and its statistics"),Shown.Name==TEXT("机器人（AI）（已离开）")&&Shown.Kills==TEXT("3")&&Shown.Deaths==TEXT("1")&&Shown.Level==TEXT("7")&&Shown.bWinner);
    TestFalse(TEXT("Statistics never mark a winner on their own"),LDHUD::ResultRow(Row,FGuid(),FGuid()).bWinner);
    const FGuid Me=FGuid::NewGuid();
    // D26: the event feed names the Boss's last hit.
    FLDBossView Dean;
    Dean.Phase=ELDBossPhase::Down;Dean.KilledBy=Me;Dean.DownAtMs=90000;
    TestEqualSensitive(TEXT("The last hit is told of its build reward"),LDHUD::BossDownLine(Dean,Me,FString(),LDTerms::Boss),
        LDTerms::BossDownSelf(LDTerms::Boss));
    TestTrue(TEXT("No 吃到大饼 buff is promised any more"),LDHUD::BossDownLine(Dean,Me,FString(),LDTerms::Boss).Contains(LDTerms::BossReward)
        &&!LDHUD::BossDownLine(Dean,Me,FString(),LDTerms::Boss).Contains(TEXT("吃到大饼")));
    TestEqualSensitive(TEXT("Others see the public roster name"),LDHUD::BossDownLine(Dean,FGuid::NewGuid(),TEXT("甲"),LDTerms::Boss),
        LDTerms::BossDownBy(TEXT("甲"),LDTerms::Boss));
    // D27: the 上热搜 line names the leader (#144: no bounty); the leader is told directly.
    FLDLeaderView Named;
    TestTrue(TEXT("Nobody named shows no leader line"),LDHUD::LeaderLine(Named,Me,FString()).IsEmpty());
    Named.Leader=FGuid::NewGuid();Named.RefreshedAtMs=20000;
    TestEqualSensitive(TEXT("Another contender's notice names the public roster name"),LDHUD::LeaderLine(Named,Me,TEXT("甲")),
        LDTerms::LeaderNamed(TEXT("甲")));
    TestTrue(TEXT("The notice is called 上热搜 and carries no bounty"),LDHUD::LeaderLine(Named,Me,TEXT("甲")).StartsWith(LDTerms::Leader)
        &&!LDHUD::LeaderLine(Named,Me,TEXT("甲")).Contains(TEXT("赏金")));
    TestEqualSensitive(TEXT("An unlisted leader is not given an invented name"),LDHUD::LeaderLine(Named,Me,FString()),LDTerms::LeaderNamed(TEXT("未列名参赛者")));
    Named.Leader=Me;
    TestEqualSensitive(TEXT("The leader is told its place is public"),LDHUD::LeaderLine(Named,Me,TEXT("我")),LDTerms::LeaderSelf());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDCardTextTest,"Lockdown.Rules.HUDCardText",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDCardTextTest::RunTest(const FString& Parameters)
{
    // #147/#198 card templates: every number reads a typed field of the active's single value set grown for the owner's level.
    FLDDemoEffectDefinition Root;Root.Opcode=uint8(ELDDemoEffectOp::Root);Root.DurationMs=1000;
    FLDDemoEffectDefinition Damage;Damage.Opcode=uint8(ELDDemoEffectOp::Damage);Damage.BaseP2=6000;Damage.ADRatioBp=5000;
    FLDDemoEffectDefinition Slow;Slow.Opcode=uint8(ELDDemoEffectOp::Slow);Slow.RatioBp=2500;Slow.DurationMs=1500;
    FLDDemoEffectDefinition Timed;Timed.Opcode=uint8(ELDDemoEffectOp::TimedStatBonus);Timed.DurationMs=3000;
    Timed.Stats={{ELDDemoBonusStat::ArmorFlat,1200,{}},{ELDDemoBonusStat::AttackSpeedBonusRatio,5000,{}}};
    FLDDemoAbilityValues Values;Values.ManaCostP2=5000;Values.CooldownMs=10000;Values.Effects={Root,Damage,Slow,Timed};
    LDHUD::FCardStats L1;L1.ADP2=7000;
    const LDHUD::FCardText Face=LDHUD::CardText(TEXT("定身目标 [root.duration] 秒，造成 [damage.amount] 物理伤害。"),Values,L1,1000);
    TestEqualSensitive(TEXT("Level 1 numbers: base plus the coefficient times the owner's AD"),Face.Plain,FString(TEXT("定身目标 1 秒，造成 95 物理伤害。")));
    TestEqualSensitive(TEXT("Keywords are bold"),Face.Rich,FString(TEXT("<b>定身</>目标 1 秒，造成 95 物理伤害。")));
    TestTrue(TEXT("The keywords used are listed in glossary order"),Face.bComplete&&Face.Keywords==TArray<FString>{TEXT("定身")});
    TestTrue(TEXT("and the card lists each placeholder's value"),Face.Values==TArray<FString>({TEXT("1"),TEXT("95")}));
    LDHUD::FCardStats L5=L1;L5.Level=5;
    TestEqualSensitive(TEXT("Level 5 grows the base (60 x 1.4 + 35) but not the duration"),
        LDHUD::CardText(TEXT("定身目标 [root.duration] 秒，造成 [damage.amount] 物理伤害。"),Values,L5,1000).Plain,FString(TEXT("定身目标 1 秒，造成 119 物理伤害。")));
    LDHUD::FCardStats BlueprintStats=L5;BlueprintStats.bShowScalingFormula=true;
    const LDHUD::FCardText BlueprintFace=LDHUD::CardText(
        TEXT("定身目标 [root.duration] 秒，造成 [damage.amount] 物理伤害。"),Values,BlueprintStats,1000);
    TestTrue(TEXT("A blueprint renders fixed values and attribute scaling without assuming the recorded player's attributes"),
        BlueprintFace.bComplete&&BlueprintFace.Plain==TEXT("定身目标 1 秒，造成 84 + 50%攻击力 物理伤害。"));
    FLDDemoEffectDefinition Shield;Shield.Opcode=uint8(ELDDemoEffectOp::Shield);Shield.BaseP2=2500;
    Shield.ArmorRatioBp=3500;Shield.SecondaryRatioBp=2000;
    FLDDemoAbilityValues ShieldValues;ShieldValues.Effects.Add(Shield);
    BlueprintStats.Level=1;
    TestEqualSensitive(TEXT("A blueprint burst keeps the armor coefficient and multiplier"),
        LDHUD::CardText(TEXT("造成 [shield.burst] 伤害。"),ShieldValues,BlueprintStats,1000).Plain,
        FString(TEXT("造成 (25 + 35%护甲) × 20% 伤害。")));
    LDHUD::FCardStats L12=L1;L12.Level=12;
    TestEqualSensitive(TEXT("After level 10 the slower growth applies (60 x (1 + 0.9 + 0.1) + 35)"),
        LDHUD::CardText(TEXT("造成 [damage.amount] 物理伤害。"),Values,L12,1000,500).Plain,FString(TEXT("造成 155 物理伤害。")));
    TestEqualSensitive(TEXT("Timed stats: points grow, ratios do not"),
        LDHUD::CardText(TEXT("护甲 +[timed_stat_bonus.ArmorFlat]，攻速 +[timed_stat_bonus.AttackSpeedBonusRatio]%。"),Values,L5,1000).Plain,
        FString(TEXT("护甲 +17，攻速 +50%。")));
    TestFalse(TEXT("An active's template never reads a per-point bonus"),
        LDHUD::CardText(TEXT("每点 +[per_point.ADFlat]。"),Values,L1,1000).bComplete);
    // #198 stat rows: [per_point.<Stat>] reads the per-point bonus exactly as authored, decimals kept and never grown.
    FLDDemoStatDefinition Brains;Brains.Id=TEXT("hud_rules_fixture_brains");
    FLDDemoStatBonus AP;AP.Stat=ELDDemoBonusStat::APFlat;AP.Amount=500;
    FLDDemoStatBonus Haste;Haste.Stat=ELDDemoBonusStat::CooldownHasteFlat;Haste.Amount=100;
    FLDDemoStatBonus Regen;Regen.Stat=ELDDemoBonusStat::MPRegenFlat;Regen.Cm.Numerator=3;Regen.Cm.Denominator=100;
    FLDDemoStatBonus Crit;Crit.Stat=ELDDemoBonusStat::CritChanceBonusRatio;Crit.Amount=40;
    Brains.PerPoint={AP,Haste,Regen,Crit};
    const FString StatTemplate=TEXT("每点法术强度 +[per_point.APFlat]、技能急速 +[per_point.CooldownHasteFlat]、法力回复 +[per_point.MPRegenFlat]/秒、暴击率 +[per_point.CritChanceBonusRatio]%。");
    const LDHUD::FCardText StatFace=LDHUD::StatCardText(StatTemplate,Brains);
    TestEqualSensitive(TEXT("A stat row shows its per-point bonuses; mana regeneration reads its Cm fraction"),StatFace.Plain,
        FString(TEXT("每点法术强度 +5、技能急速 +1、法力回复 +0.03/秒、暴击率 +0.4%。")));
    TestTrue(TEXT("Every per-point placeholder reads a number"),StatFace.bComplete&&StatFace.Values.Num()==4);
    TestFalse(TEXT("A stat's template never reads an active's effect"),LDHUD::StatCardText(TEXT("造成 [damage.amount] 伤害。"),Brains).bComplete);
    TestFalse(TEXT("A per-point bonus the stat lacks is never shown as a number"),LDHUD::StatCardText(TEXT("+[per_point.ArmorFlat]"),Brains).bComplete);
    const LDHUD::FCardText Both=LDHUD::CardText(TEXT("被减速或被控制的敌人。"),Values,L1,1000);
    TestTrue(TEXT("Slow and control are separate keywords"),Both.Keywords==TArray<FString>({TEXT("减速"),TEXT("控制")})
        &&Both.Rich==TEXT("被<b>减速</>或被<b>控制</>的敌人。"));
    const LDHUD::FCardText Absent=LDHUD::CardText(TEXT("造成 [stun.duration] 秒。"),Values,L1,1000);
    TestTrue(TEXT("A placeholder of an absent effect is never shown as a number"),!Absent.bComplete&&Absent.Plain.Contains(TEXT("?")));
    TestFalse(TEXT("A broken template is refused"),LDHUD::CardText(TEXT("造成 [damage] 伤害。"),Values,L1,1000).bComplete);
    TArray<LDDemoCardText::FToken> Tokens;FString Error;
    TestFalse(TEXT("An unknown op is refused"),LDDemoCardText::Parse(TEXT("[teleport.amount]"),Tokens,Error));
    TestFalse(TEXT("A stray bracket is refused"),LDDemoCardText::Parse(TEXT("造成 ] 伤害"),Tokens,Error));
    FLDBlueprintRecord BlueprintRecord;BlueprintRecord.FinalBuild.bComponentsCollected=true;
    FLDBlueprintComponent& Installed=BlueprintRecord.FinalBuild.Components.AddDefaulted_GetRef();
    Installed.Component=TEXT("component_quick_hop");
    Installed.Parameters={{TEXT("bound_component"),TEXT("component_fist_water")},
        {TEXT("element"),TEXT("fire")},{TEXT("target_kind"),TEXT("dash")}};
    const FString BlueprintHistory=ULDBlueprintPanel::Describe(BlueprintRecord,nullptr);
    TestTrue(TEXT("Blueprint install details use readable targets, elements and reused components"),
        BlueprintHistory.Contains(TEXT("冲刺（D）"))&&BlueprintHistory.Contains(TEXT("绑定元素：火"))
        &&BlueprintHistory.Contains(TEXT("复用构件：component_fist_water"))&&!BlueprintHistory.Contains(TEXT("target_kind")));
    TestTrue(TEXT("Keyword lines explain each keyword in bold"),LDHUD::KeywordLines({TEXT("定身")}).Num()==1
        &&LDHUD::KeywordLines({TEXT("定身")})[0].StartsWith(TEXT("<b>定身</>：")));
    for(const LDTerms::FCardKeyword& Keyword:LDTerms::CardKeywords)
    {
        const FString Explain=Keyword.Explain;
        TestFalse(FString(Keyword.Name)+TEXT(" never mentions spray or guard"),Explain.Contains(TEXT("喷漆"))||Explain.Contains(TEXT("守护")));
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDContentParametersTest,"Lockdown.Rules.HUDContentParameters",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDContentParametersTest::RunTest(const FString& Parameters)
{
    FLDDemoStatBonus Stat;Stat.Stat=ELDDemoBonusStat::ADFlat;Stat.Amount=2000;
    TestEqualSensitive(TEXT("Flat stats are P2 points"),LDHUD::StatLine(Stat),FString(LDTerms::AttackDamage)+TEXT(" +20"));
    Stat.Stat=ELDDemoBonusStat::AttackSpeedBonusRatio;Stat.Amount=1000;
    TestEqualSensitive(TEXT("Ratio stats are basis points"),LDHUD::StatLine(Stat),FString(LDTerms::AttackSpeed)+TEXT(" +10%"));
    Stat.Stat=ELDDemoBonusStat::AttackRangeCmFlat;Stat.Amount=0;Stat.Cm.Numerator=40;Stat.Cm.Denominator=1;
    TestEqualSensitive(TEXT("Range bonuses are centimetres"),LDHUD::StatLine(Stat),FString(TEXT("攻击距离 +40cm")));
    FLDDemoEffectDefinition Damage;Damage.Opcode=uint8(ELDDemoEffectOp::Damage);Damage.BaseP2=10000;Damage.ADRatioBp=8000;Damage.APRatioBp=6500;Damage.DamageType=ELDDemoDamageType::Magical;
    TestEqualSensitive(TEXT("Magic damage uses the approved term and its AD and AP scaling"),LDHUD::EffectLine(Damage),FString::Printf(TEXT("%s 100 + 80%%%s + 65%%%s"),LDTerms::MagicalDamage,LDTerms::AttackDamage,LDTerms::AbilityPower));
    FLDDemoEffectDefinition Root;Root.Opcode=uint8(ELDDemoEffectOp::Root);Root.DurationMs=1300;
    TestEqualSensitive(TEXT("Control duration is shown in seconds"),LDHUD::EffectLine(Root),FString(LDTerms::Root)+TEXT(" 1.3s"));
    FLDDemoEffectDefinition Unknown;Unknown.Opcode=200;
    TestEqualSensitive(TEXT("An unknown effect is reported, never described"),LDHUD::EffectLine(Unknown),FString(TEXT("未知效果 #200")));
    FLDDemoAbilityValues Cost;Cost.ManaCostP2=4500;Cost.CooldownMs=6400;Cost.Effects.Add(Damage);
    TestEqualSensitive(TEXT("An active's cost line carries mana and cooldown"),LDHUD::CostLine(Cost),FString(LDTerms::Mana)+TEXT(" 45 · 冷却 6.4s"));
    FLDDemoContentDefinitions Content;FLDDemoAbilityDefinition Bolt;Bolt.Id=TEXT("hud_rules_fixture_bolt");
    Bolt.Target.Mode=ELDDemoTargetMode::DirectionProjectile;Bolt.Target.RangeCm.Numerator=650;Bolt.Target.RangeCm.Denominator=1;
    Bolt.Values=Cost;Content.Abilities.Add(Bolt);
    Content.Parameters.SkillGrowthPerLevelBp=1000;
    LDHUD::FCardStats Stats;
    AddExpectedMessage(TEXT("LD_HUD_DISPLAY_TEXT_MISSING hud_rules_fixture_bolt"),ELogVerbosity::Error,EAutomationExpectedMessageFlags::Contains,0,false);
    const LDHUD::FActiveCardView Missing=LDHUD::ActiveCard(TEXT("hud_rules_fixture_bolt"),&Content,Stats);
    TestTrue(TEXT("An active without its display row says so instead of inventing a face"),
        !Missing.bKnown&&Missing.SentencePlain==TEXT("内容目录中找不到该技能"));
    AddExpectedMessage(TEXT("LD_HUD_DISPLAY_TEXT_MISSING hud_rules_fixture_unknown"),ELogVerbosity::Error,EAutomationExpectedMessageFlags::Contains,0,false);
    TestTrue(TEXT("An active the catalog lacks says so too"),!LDHUD::ActiveCard(TEXT("hud_rules_fixture_unknown"),&Content,Stats).bKnown);
    FLDDemoDisplayDefinition BoltDisplay;BoltDisplay.Id=TEXT("hud_rules_fixture_bolt");BoltDisplay.IconId=TEXT("icon_hud_rules_fixture_bolt");
    BoltDisplay.NameText=TEXT("测试飞弹");BoltDisplay.DescriptionText=TEXT("命中造成 [damage.amount] 魔法伤害并减速。");
    Content.Display.Add(BoltDisplay);
    const LDHUD::FActiveCardView Card=LDHUD::ActiveCard(TEXT("hud_rules_fixture_bolt"),&Content,Stats);
    TestTrue(TEXT("The card face is the sentence with this level's numbers and its keywords"),Card.bKnown
        &&Card.SentencePlain==TEXT("命中造成 100 魔法伤害并减速。")&&Card.Sentence==TEXT("命中造成 100 魔法伤害并<b>减速</>。")
        &&Card.Keywords.Num()==1&&Card.Keywords[0].StartsWith(TEXT("<b>减速</>：")));
    TestTrue(TEXT("The card shows the target form and the cost"),Card.Target==TEXT("方向弹道 · 射程 650cm")&&Card.Cost==LDHUD::CostLine(Cost));
    TestTrue(TEXT("An active card draws its display icon, with the name's first character only as the missing-icon stand-in"),
        Card.Glyph==Card.Name.Left(1)&&Card.IconId==TEXT("icon_hud_rules_fixture_bolt"));
    LDHUD::FCardStats L4;L4.Level=4;
    TestTrue(TEXT("A higher owner level grows the number on the same card (one skill level, #198)"),
        LDHUD::ActiveCard(TEXT("hud_rules_fixture_bolt"),&Content,L4).SentencePlain==TEXT("命中造成 130 魔法伤害并减速。"));
    // #198 stat rows: name, per-point text and the allocated points; a stat without its display row says so.
    FLDDemoStatDefinition Skin;Skin.Id=TEXT("hud_rules_fixture_skin");
    FLDDemoStatBonus Armor;Armor.Stat=ELDDemoBonusStat::ArmorFlat;Armor.Amount=250;
    FLDDemoStatBonus Resist;Resist.Stat=ELDDemoBonusStat::MagicResistFlat;Resist.Amount=250;
    Skin.PerPoint={Armor,Resist};
    AddExpectedMessage(TEXT("LD_HUD_DISPLAY_TEXT_MISSING hud_rules_fixture_skin"),ELogVerbosity::Error,EAutomationExpectedMessageFlags::Contains,0,false);
    const LDHUD::FStatRowView NoRow=LDHUD::StatRow(Skin,2,&Content);
    TestTrue(TEXT("A stat without its display row says so"),!NoRow.bKnown&&NoRow.Points==2&&NoRow.PerPoint==TEXT("内容目录中找不到该属性"));
    FLDDemoDisplayDefinition SkinDisplay;SkinDisplay.Id=Skin.Id;SkinDisplay.IconId=TEXT("icon_hud_rules_fixture_skin");
    SkinDisplay.DescriptionText=TEXT("每点护甲和魔抗各 +[per_point.ArmorFlat]。");
    Content.Display.Add(SkinDisplay);
    const LDHUD::FStatRowView SkinRow=LDHUD::StatRow(Skin,3,&Content);
    TestTrue(TEXT("A stat row shows its per-point text, its icon and the allocated points"),SkinRow.bKnown&&SkinRow.Points==3
        &&SkinRow.PerPoint==TEXT("每点护甲和魔抗各 +2.5。")&&SkinRow.IconId==TEXT("icon_hud_rules_fixture_skin")&&SkinRow.StatId==FName(*Skin.Id));
    {
        // The stat icon's popup: each raised attribute's current value and the gain of one point; a missing row reads "?".
        FLDCombatAttributeRowView ArmorRow;ArmorRow.Attribute=uint8(ELDProjectedAttribute::Armor);ArmorRow.Final.Numerator=105;ArmorRow.Final.Denominator=2;
        FLDDemoStatDefinition Strong;Strong.Id=TEXT("hud_rules_fixture_strong");
        FLDDemoStatBonus Crit;Crit.Stat=ELDDemoBonusStat::CritChanceBonusRatio;Crit.Amount=40;
        FLDDemoStatBonus Regen;Regen.Stat=ELDDemoBonusStat::HPRegenFlat;Regen.Cm.Numerator=2;Regen.Cm.Denominator=25;
        Strong.PerPoint={Armor,Crit,Regen};
        const TArray<LDHUD::FStatEffectView> Effects=LDHUD::StatEffects(Strong,{ArmorRow});
        TestTrue(TEXT("A stat's popup lists each raised attribute with its current value and per-point gain"),Effects.Num()==3
            &&Effects[0].Attribute==LDTerms::Armor&&Effects[0].Current==TEXT("52.5")&&Effects[0].PerPoint==TEXT("+2.5")
            &&Effects[1].Attribute==LDTerms::CritChance()&&Effects[1].Current==TEXT("?")&&Effects[1].PerPoint==TEXT("+0.4%")
            &&Effects[2].Attribute==LDTerms::HealthRegen()&&Effects[2].PerPoint==TEXT("+0.08/s"));
    }
    FLDCombatAttributeRowView Crit;Crit.Attribute=uint8(ELDProjectedAttribute::CritChance);Crit.Final.Numerator=1;Crit.Final.Denominator=5;Crit.Base.Denominator=1;
    Crit.Level.Denominator=1;Crit.Choices.Numerator=1;Crit.Choices.Denominator=50;
    const LDHUD::FAttributeRowView Row=LDHUD::AttributeRow(Crit);
    TestTrue(TEXT("Attribute rows use the approved name and show ratios as percent"),Row.Name==LDTerms::CritChance()&&Row.Final==TEXT("20%"));
    TestTrue(TEXT("The breakdown names the allocated points' term, never a card or an opening"),Row.Breakdown.Contains(TEXT("属性点 2%"))
        &&!Row.Breakdown.Contains(TEXT("卡"))&&!Row.Breakdown.Contains(TEXT("开局")));
    TestEqualSensitive(TEXT("A dead owner's growth refusal names the state, not a trade"),LDHUD::GrowthReasonText(ELDGrowthReason::Dead),FString(LDTerms::Dead)+TEXT("，暂时不能操作"));
    TestEqualSensitive(TEXT("Combat rejections are explained"),LDHUD::CombatRejectText(ELDCombatReject::OutOfRange),FString(TEXT("超出射程")));
    FLDCombatEntryView Entry;Entry.DefinitionId=NAME_None;Entry.Unavailable=ELDCombatReject::SourceUnavailable;
    const LDHUD::FActionInput Empty=LDHUD::ActionInput(Entry,LDHUD::EActionSlot::Q,&Content);
    TestTrue(TEXT("An empty combat entry is an explicit empty source with the server reason kept for display"),Empty.bProjected&&!Empty.bHasSource&&Empty.ServerBlock==TEXT("没有可用来源"));
    TestTrue(TEXT("An empty slot has no icon"),Empty.IconId.IsEmpty()&&LDHUD::Action(Empty,LDHUD::FActionContext()).IconId.IsEmpty());
    FLDCombatEntryView BoltEntry;BoltEntry.DefinitionId=TEXT("hud_rules_fixture_bolt");
    const LDHUD::FActionInput BoltInput=LDHUD::ActionInput(BoltEntry,LDHUD::EActionSlot::Q,&Content);
    TestTrue(TEXT("A bound slot draws its definition's display icon"),BoltInput.IconId==TEXT("icon_hud_rules_fixture_bolt")
        &&LDHUD::Action(BoltInput,LDHUD::FActionContext()).IconId==TEXT("icon_hud_rules_fixture_bolt"));
    FLDCombatEntryView DashEntry;DashEntry.DefinitionId=TEXT("dash");
    TestEqualSensitive(TEXT("The dash has its own icon without a display row"),LDHUD::ActionInput(DashEntry,LDHUD::EActionSlot::Dash,&Content).IconId,
        FString(LDUIIcons::Dash));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDCardFacesTest,"Lockdown.Rules.HUDCardFaces",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDCardFacesTest::RunTest(const FString& Parameters)
{
    // #198 rendered faces over the approved catalog: every active and component card at levels 1 and 10 with the bare build's
    // stats, and every stat row (as many as the catalog has), names its entry, reads every placeholder as a number, has an
    // icon and never shows spray/guard wording,
    // an unknown effect or a missing-content line.
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if(!LDTestApprovedFixture::Read(*this,Catalog)){return false;}
    const FLDDemoContentDefinitions& Content=Catalog->Get();
    FName Table;
    if(!HudRegisterApprovedNames(*this,Content,Table)){return false;}
    ON_SCOPE_EXIT{if(!Table.IsNone()){FStringTableRegistry::Get().UnregisterStringTable(Table);}};
    const FLDAttributeProjectionResult Base=LDAttributeProjection::Project(FLDAttributeBuildValues(1,{}),*Catalog);
    if(!TestTrue(TEXT("The bare level-1 build projects"),Base.IsSuccess())){return false;}
    LDHUD::FCardStats L1;L1.ADP2=Base.Value->Final.ADP2;L1.APP2=Base.Value->Final.APP2;L1.MaxHPP2=Base.Value->Final.MaxHPP2;L1.ArmorP2=Base.Value->Final.ArmorP2;
    LDHUD::FCardStats L10=L1;L10.Level=10;
    LDHUD::FCardStats Blueprint=L10;Blueprint.bShowScalingFormula=true;
    const auto Bad=[](const FString& Text){return Text.IsEmpty()||Text.Contains(TEXT("喷漆"))||Text.Contains(TEXT("守护"))||Text.Contains(TEXT("未知"))
        ||Text.Contains(TEXT("?"))||Text.Contains(TEXT("内容目录中找不到"))||Text.Contains(TEXT("同步中"));};
    TestTrue(TEXT("The approved catalog has actives, stats and components"),
        !Content.Abilities.IsEmpty()&&!Content.Stats.IsEmpty()&&!Content.Components.IsEmpty());
    int32 ActiveFaces=0,StatFaces=0,ComponentFaces=0;
    for(const FLDDemoAbilityDefinition& Ability:Content.Abilities)
    {
        ++ActiveFaces;
        for(const LDHUD::FCardStats& Stats:{L1,L10})
        {
            const FString At=FString::Printf(TEXT("%s L%d"),*Ability.Id,int32(Stats.Level));
            const LDHUD::FActiveCardView Card=LDHUD::ActiveCard(FName(*Ability.Id),&Content,Stats);
            TestTrue(At+TEXT(": the card reads every placeholder as a number"),Card.bKnown);
            TestFalse(At+TEXT(": name ")+Card.Name,Bad(Card.Name)||Card.Name==Ability.Id);
            TestFalse(At+TEXT(": sentence ")+Card.SentencePlain,Bad(Card.SentencePlain));
            TestFalse(At+TEXT(": target ")+Card.Target,Bad(Card.Target));
            TestFalse(At+TEXT(": cost ")+Card.Cost,Bad(Card.Cost));
            for(const FString& Line:Card.Keywords){TestFalse(At+TEXT(": keyword ")+Line,Bad(Line));}
            TestFalse(At+TEXT(": the card has an icon"),Card.IconId.IsEmpty());
        }
        const LDHUD::FActiveCardView BlueprintCard=LDHUD::ActiveCard(FName(*Ability.Id),&Content,Blueprint);
        TestTrue(Ability.Id+TEXT(": the blueprint resolves every active placeholder"),
            BlueprintCard.bKnown&&!BlueprintCard.SentencePlain.Contains(TEXT("["))&&!BlueprintCard.SentencePlain.Contains(TEXT("]")));
    }
    for(const FLDDemoStatDefinition& Stat:Content.Stats)
    {
        ++StatFaces;
        const LDHUD::FStatRowView Row=LDHUD::StatRow(Stat,2,&Content);
        TestTrue(Stat.Id+TEXT(": the row reads every per-point placeholder as a number"),Row.bKnown&&Row.Points==2);
        TestFalse(Stat.Id+TEXT(": name ")+Row.Name,Bad(Row.Name)||Row.Name==Stat.Id);
        TestFalse(Stat.Id+TEXT(": per point ")+Row.PerPoint,Bad(Row.PerPoint));
        TestFalse(Stat.Id+TEXT(": the row has an icon"),Row.IconId.IsEmpty());
    }
    for(const FLDDemoComponentDefinition& Component:Content.Components)
    {
        ++ComponentFaces;
        for(const LDHUD::FCardStats& Stats:{L1,L10})
        {
            const FString At=FString::Printf(TEXT("%s L%d"),*Component.Id,int32(Stats.Level));
            const LDHUD::FComponentCardView Card=LDHUD::ComponentCard(FName(*Component.Id),&Content,Stats);
            TestTrue(At+TEXT(": the card reads every placeholder as a number"),Card.bKnown);
            TestFalse(At+TEXT(": name ")+Card.Name,Bad(Card.Name)||Card.Name==Component.Id);
            TestFalse(At+TEXT(": sentence ")+Card.SentencePlain,Bad(Card.SentencePlain));
            for(const FString& Line:Card.Keywords){TestFalse(At+TEXT(": keyword ")+Line,Bad(Line));}
            TestFalse(At+TEXT(": the card has an icon"),Card.IconId.IsEmpty());
        }
        const LDHUD::FComponentCardView BlueprintCard=LDHUD::ComponentCard(FName(*Component.Id),&Content,Blueprint);
        TestTrue(Component.Id+TEXT(": the blueprint resolves every component placeholder"),
            BlueprintCard.bKnown&&!BlueprintCard.SentencePlain.Contains(TEXT("["))&&!BlueprintCard.SentencePlain.Contains(TEXT("]")));
    }
    TestTrue(TEXT("Every active, stat and component of the approved catalog has its face checked"),
        ActiveFaces==Content.Abilities.Num()&&StatFaces==Content.Stats.Num()&&ComponentFaces==Content.Components.Num());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDSelfIconsTest,"Lockdown.Rules.HUDSelfIcons",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDSelfIconsTest::RunTest(const FString& Parameters)
{
    TestEqualSensitive(TEXT("A Latin name's icon is its upper-cased first letter"),LDHUD::IconGlyph(TEXT(" bolt")),FString(TEXT("B")));
    TestEqualSensitive(TEXT("A Chinese name's icon is its first character"),LDHUD::IconGlyph(TEXT("眩晕")),FString(TEXT("眩")));
    TestEqualSensitive(TEXT("An empty name is never a blank square"),LDHUD::IconGlyph(TEXT("  ")),FString(TEXT("?")));
    // More temporary statuses and recoveries than one strip row: every one shows, control statuses first.
    if(!HudRegisterFixtureTable(*this)){return false;}
    ON_SCOPE_EXIT{FStringTableRegistry::Get().UnregisterStringTable(HudFixtureTable);};
    const FLDDemoContentDefinitions Content=HudFixtureContent();
    FLDCombatOwnerView Combat;
    FLDCombatStatusView Root;Root.StatusId=uint8(ELDDemoStatusId::Root);Root.ExpiresAtMs=5000;Root.ControlBlocks=uint8(ELDCombatControlBlock::Move);
    FLDCombatStatusView Shield;Shield.StatusId=uint8(ELDDemoStatusId::Shield);Shield.ExpiresAtMs=4000;Shield.AmountP2=5000;
    Combat.Statuses={Shield,Root};
    while(Combat.Statuses.Num()<14){Combat.Statuses.Add(Shield);}
    Combat.bOutOfCombatRegenActive=true;
    const LDHUD::FSelfIconsView Full=LDHUD::SelfIcons(&Combat,true,0,&Content);
    TestEqual(TEXT("The strip has no cap: 14 statuses and a recovery all show"),Full.Items.Num(),15);
    TestTrue(TEXT("Control statuses come first, the recovery last"),Full.Items[0].Kind==LDHUD::ESelfIconKind::Control
        &&Full.Items.Last().Source==LDHUD::ESelfIconSource::OutOfCombatRegen);
    TestTrue(TEXT("Without the own projection the grid is syncing"),LDHUD::SelfIcons(nullptr,true,0,&Content).bSyncing);
    // The hover popup names the square and its kind; the description comes from the content catalog.
    const LDHUD::FActionDetailView RootDetail=LDHUD::SelfIconDetail(Full.Items[0],nullptr);
    TestTrue(TEXT("A control status popup heads with its line, kind and tone"),RootDetail.Name==Full.Items[0].Tip&&RootDetail.Summary==TEXT("控制状态 · 负面"));
    // Status tone: what helps the bearer is positive, every control and affliction negative, an unknown id negative.
    for(const ELDDemoStatusId Id:{ELDDemoStatusId::Shield,ELDDemoStatusId::StatBoost,ELDDemoStatusId::Empowered,ELDDemoStatusId::RespawnProtection,ELDDemoStatusId::CampReturning})
    {TestTrue(FString::Printf(TEXT("Status %d is positive"),int32(Id)),LDHUD::StatusIsPositive(uint8(Id)));}
    for(const ELDDemoStatusId Id:{ELDDemoStatusId::Root,ELDDemoStatusId::Slow,ELDDemoStatusId::Exposed,ELDDemoStatusId::Taunt,ELDDemoStatusId::Disarm,
        ELDDemoStatusId::Stun,ELDDemoStatusId::Knockback,ELDDemoStatusId::Silence,ELDDemoStatusId::Burn})
    {TestFalse(FString::Printf(TEXT("Status %d is negative"),int32(Id)),LDHUD::StatusIsPositive(uint8(Id)));}
    TestFalse(TEXT("An unknown status reads as negative"),LDHUD::StatusIsPositive(200));
    TestTrue(TEXT("A shield square is positive"),Full.Items[1].bPositive&&!Full.Items[0].bPositive);
    TestEqualSensitive(TEXT("A shield popup says 临时状态 · 正面"),LDHUD::SelfIconDetail(Full.Items[1],nullptr).Summary,FString(TEXT("临时状态 · 正面")));
    // Remaining-time rings: the first sight of an end instant is the full ring.
    LDHUD::FStatusRings Rings;const FName Key(TEXT("Status.1"));
    TestEqual(TEXT("A newly seen status starts with a full ring"),Rings.Fraction(Key,4000,1000),1.f);
    TestEqual(TEXT("Half the span left is half a ring"),Rings.Fraction(Key,4000,2500),.5f);
    TestEqual(TEXT("A refresh (a later end) restarts the ring full"),Rings.Fraction(Key,8000,2500),1.f);
    TestEqual(TEXT("An earlier end keeps the span and shortens the ring"),Rings.Fraction(Key,5250,2500),.5f);
    TestEqual(TEXT("A passed end is an empty ring"),Rings.Fraction(Key,5250,9000),0.f);
    Rings.Retain({});
    TestEqual(TEXT("A status gone and re-applied starts a fresh full ring"),Rings.Fraction(Key,3000,2000),1.f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDCombatOwnerViewTest,"Lockdown.Rules.HUDCombatOwnerView",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDCombatOwnerViewTest::RunTest(const FString& Parameters)
{
    if(!HudRegisterFixtureTable(*this)){return false;}
    ON_SCOPE_EXIT{FStringTableRegistry::Get().UnregisterStringTable(HudFixtureTable);};
    FLDDemoContentDefinitions Content=HudFixtureContent();
    FLDCombatOwnerView Combat;
    FLDCombatStatusView Root;Root.StatusId=uint8(ELDDemoStatusId::Root);Root.ExpiresAtMs=11201;Root.ControlBlocks=uint8(ELDCombatControlBlock::Move);
    FLDCombatStatusView Aura;Aura.StatusId=uint8(ELDDemoStatusId::Shield);Aura.bIsPassive=true;
    FLDCombatStatusView Protection;Protection.StatusId=uint8(ELDDemoStatusId::RespawnProtection);Protection.ExpiresAtMs=20000;
    FLDCombatStatusView Shield;Shield.StatusId=uint8(ELDDemoStatusId::Shield);Shield.AmountP2=8050;Shield.ExpiresAtMs=12000;
    // The shield is listed before the root: the grid still puts the control status first.
    Combat.Statuses={Shield,Aura,Protection,Root};
    auto Tips=[](const LDHUD::FSelfIconsView& View){TArray<FString> Out;for(const LDHUD::FSelfIconView& Icon:View.Items){Out.Add(Icon.Tip);}return FString::Join(Out,TEXT(" | "));};
    LDHUD::FSelfIconsView Icons=LDHUD::SelfIcons(&Combat,true,10000,&Content);
    TestEqualSensitive(TEXT("Control first; time and shield round up; passive auras and protection stay off the grid"),
        Tips(Icons),HudName(TEXT("root"))+TEXT(" 1.3s | ")+HudName(TEXT("shield"))+TEXT(" 81 2.0s"));
    TestTrue(TEXT("Timed statuses carry their end instant and kind"),Icons.Items.Num()==2&&Icons.Items[0].Kind==LDHUD::ESelfIconKind::Control
        &&Icons.Items[0].ExpiresAtMs==11201&&Icons.Items[1].Kind==LDHUD::ESelfIconKind::Temporary&&Icons.Items[1].ExpiresAtMs==12000&&!Icons.bSyncing);
    TestTrue(TEXT("The root square is negative and the shield square positive"),!Icons.Items[0].bPositive&&Icons.Items[1].bPositive
        &&LDHUD::SelfIconDetail(Icons.Items[1],nullptr).Summary==TEXT("临时状态 · 正面"));
    TestEqualSensitive(TEXT("The missing-icon stand-in is the name's first character"),Icons.Items[0].Glyph,LDHUD::IconGlyph(HudName(TEXT("root"))));
    TestTrue(TEXT("Status squares draw their status's display icon"),Icons.Items[0].IconId==TEXT("icon_root")&&Icons.Items[1].IconId==TEXT("icon_shield"));
    TestEqualSensitive(TEXT("Without the matching authority clock no remainder is shown"),Tips(LDHUD::SelfIcons(&Combat,false,0,&Content)),
        HudName(TEXT("root"))+TEXT(" | ")+HudName(TEXT("shield"))+TEXT(" 81"));
    const LDHUD::FSelfIconsView Missing=LDHUD::SelfIcons(nullptr,true,0,&Content);
    TestTrue(TEXT("A missing projection reads as syncing, never as none"),Missing.bSyncing&&Missing.Items.IsEmpty());
    Combat.Statuses.Reset();
    // D21 recoveries are temporary squares while they heal, with no remaining-time ring.
    Combat.bOutOfCombatRegenActive=true;Combat.bDormRegenActive=true;
    Icons=LDHUD::SelfIcons(&Combat,true,10000,&Content);
    TestEqualSensitive(TEXT("Active out-of-combat and dorm recoveries show"),Tips(Icons),
        FString(LDTerms::OutOfCombatRegen)+TEXT(" | ")+LDTerms::DormRegenActive());
    TestTrue(TEXT("Recoveries are untimed temporary squares"),Icons.Items.Num()==2&&Icons.Items[0].Kind==LDHUD::ESelfIconKind::Temporary
        &&Icons.Items[0].ExpiresAtMs==0&&Icons.Items[1].Kind==LDHUD::ESelfIconKind::Temporary&&Icons.Items[1].ExpiresAtMs==0);
    TestTrue(TEXT("Recoveries draw their own icons without a display row"),
        Icons.Items[0].IconId==LDUIIcons::OutOfCombatRegen&&Icons.Items[1].IconId==LDUIIcons::DormRegen);
    TestTrue(TEXT("Recoveries are positive"),Icons.Items.FilterByPredicate([](const LDHUD::FSelfIconView& Icon){return Icon.bPositive;}).Num()==2);
    Content.Parameters.OutOfCombatRegenDelayMs=5000;Content.Parameters.OutOfCombatMaxHPRegenBp=150;Content.Parameters.OutOfCombatMaxMPRegenBp=150;
    Content.Parameters.DormRegenDelayMs=3000;Content.Parameters.DormMaxHPRegenBp=1000;Content.Parameters.DormMaxMPRegenBp=1000;
    const LDHUD::FActionDetailView Slow=LDHUD::SelfIconDetail(Icons.Items[0],&Content);
    TestTrue(TEXT("The out-of-combat popup states the REGEN delay and share"),Slow.Name==LDTerms::OutOfCombatRegen&&Slow.Summary==TEXT("临时状态 · 正面")
        &&Slow.Body==TEXT("脱战 5s 后，每秒恢复 1.5% 生命上限和法力上限\n出手、释放伤害技能或受到伤害立即停止"));
    const LDHUD::FActionDetailView Dorm=LDHUD::SelfIconDetail(Icons.Items[1],&Content);
    TestTrue(TEXT("The dorm popup states the DORM delay and share"),Dorm.Body.StartsWith(FString(TEXT("站在"))+LDTerms::Dorm+TEXT("内且脱战 3s 后，每秒恢复 10% 生命上限和法力上限")));
    Combat.bOutOfCombatRegenActive=false;Combat.bDormRegenActive=false;
    Icons=LDHUD::SelfIcons(&Combat,true,10000,&Content);
    TestTrue(TEXT("A known projection without statuses or recoveries shows an empty strip"),Icons.Items.IsEmpty()&&!Icons.bSyncing);
    FLDCombatActionReceipt Receipt;Receipt.Origin=ELDCombatRequestOrigin::Direct;Receipt.Entry=ELDCombatEntry::W;
    Receipt.Phase=ELDCombatPhase::Rejected;Receipt.Reject=ELDCombatReject::Cooldown;Receipt.bProtectionRemoved=true;
    TestEqualSensitive(TEXT("A failed own request and the lost protection are separate facts"),LDHUD::CombatReceiptNotice(Receipt),TEXT("角色技能 2 未执行：冷却中 · ")+LDTerms::ProtectionRemoved());
    Receipt.bProtectionRemoved=false;Receipt.Reject=ELDCombatReject::Cancelled;
    TestTrue(TEXT("A cancel tells nothing"),LDHUD::CombatReceiptNotice(Receipt).IsEmpty());
    Receipt.Origin=ELDCombatRequestOrigin::Nav;Receipt.Reject=ELDCombatReject::OutOfRange;
    TestTrue(TEXT("Navigation-issued attacks report through their order, not as an action failure"),LDHUD::CombatReceiptNotice(Receipt).IsEmpty());
    Receipt.Phase=ELDCombatPhase::Accepted;Receipt.Reject=ELDCombatReject::None;Receipt.bProtectionRemoved=true;
    TestEqualSensitive(TEXT("An accepted attack still reports the lost protection"),LDHUD::CombatReceiptNotice(Receipt),LDTerms::ProtectionRemoved());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDBasicAttackTermTest,"Lockdown.Rules.HUDBasicAttackTerm",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDBasicAttackTermTest::RunTest(const FString& Parameters)
{
    // APP_SPEC 1.2 (#148): the player reads the basic attack by its generic glossary term; retired memes never return in system text.
    const FString NotReady=LDHUD::CombatRejectText(ELDCombatReject::AttackNotReady);
    TestTrue(TEXT("The attack-not-ready reason names the basic attack by its player term"),
        NotReady.Contains(LDTerms::BasicAttack,ESearchCase::CaseSensitive));
    FLDDemoEffectDefinition Lifesteal;Lifesteal.Opcode=uint8(ELDDemoEffectOp::Lifesteal);Lifesteal.RatioBp=1500;
    TestEqualSensitive(TEXT("The lifesteal line names the basic attack by its player term"),LDHUD::EffectLine(Lifesteal),FString(LDTerms::BasicAttack)+TEXT("打掉的")+LDTerms::Health+TEXT("的 15% 回复自身"));
    TestEqualSensitive(TEXT("The basic entry label uses the player term"),LDHUD::EntryLabel(ELDCombatEntry::Basic),FString(LDTerms::BasicAttack));
    const TCHAR* const Retired[]={TEXT("大比兜"),TEXT("精神状态"),TEXT("电量"),TEXT("力大砖飞"),TEXT("阴阳怪气"),TEXT("嘴硬"),TEXT("心大"),TEXT("手速"),
        TEXT("润"),TEXT("整活频率"),TEXT("物理超度"),TEXT("精神攻击"),TEXT("心理防线"),TEXT("提裤子"),TEXT("腿软"),TEXT("捂裆"),TEXT("免死金牌")};
    FString Technical;
    for(int32 Value=0;Value<=MAX_uint8;++Value)
    {
        FLDDemoEffectDefinition Effect;Effect.Opcode=uint8(Value);
        for(const FString& Line:{LDHUD::CombatRejectText(static_cast<ELDCombatReject>(Value)),LDHUD::EffectLine(Effect),LDHUD::EntryLabel(static_cast<ELDCombatEntry>(Value))})
        {for(const TCHAR* Term:Retired){if(Line.Contains(Term,ESearchCase::CaseSensitive)){Technical+=Line+TEXT(" | ");break;}}}
    }
    TestTrue(*FString::Printf(TEXT("No combat reason, effect line or entry label shows a retired meme term (found: %s)"),*Technical),Technical.IsEmpty());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDIdentityPaletteTest,"Lockdown.Rules.HUDIdentityPalette",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDIdentityPaletteTest::RunTest(const FString& Parameters)
{
    // Identity colours follow the locked roster slot 0..5: a portrait shows the same tint as that contender's body.
    auto Spread=[](const FLinearColor& A,const FLinearColor& B){return FMath::Max3(FMath::Abs(A.R-B.R),FMath::Abs(A.G-B.G),FMath::Abs(A.B-B.B));};
    for(int32 Slot=0;Slot<6;++Slot)
    {
        const FLinearColor Tint=LDHUD::PaletteColor(uint8(Slot));
        TestTrue(*FString::Printf(TEXT("Roster slot %d portrait uses its body tint"),Slot),Tint==ULDHumanoidVisualComponent::ContenderTint(uint8(Slot)));
        for(int32 Other=0;Other<Slot;++Other)
        {TestTrue(*FString::Printf(TEXT("Roster slots %d and %d have distinct identity tints"),Other,Slot),Spread(Tint,LDHUD::PaletteColor(uint8(Other)))>=.1f);}
    }
    // A camp member's portrait uses its NPC clothing, never a contender's identity tint.
    for(const ELDDemoMonsterId Monster:{ELDDemoMonsterId::MonsterScavenger,ELDDemoMonsterId::MonsterGuardian})
    {
        const FLinearColor Camp=ULDHumanoidVisualComponent::MonsterTint(uint8(Monster));
        for(int32 Slot=0;Slot<6;++Slot)
        {TestTrue(*FString::Printf(TEXT("Creature %d is not drawn in roster slot %d's tint"),int32(Monster),Slot),Spread(Camp,LDHUD::PaletteColor(uint8(Slot)))>=.1f);}
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDCooldownTextTest,"Lockdown.Rules.HUDCooldownText",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDCooldownTextTest::RunTest(const FString& Parameters)
{
    TestTrue(TEXT("A ready track draws no remainder"),LDHUD::CooldownText(0).IsEmpty()&&LDHUD::CooldownText(-250).IsEmpty());
    TestEqualSensitive(TEXT("From ten seconds the remainder reads in whole seconds"),LDHUD::CooldownText(12000),FString(TEXT("12")));
    TestEqualSensitive(TEXT("Whole seconds round up"),LDHUD::CooldownText(12001),FString(TEXT("13")));
    TestEqualSensitive(TEXT("Exactly ten seconds reads whole"),LDHUD::CooldownText(10000),FString(TEXT("10")));
    TestEqualSensitive(TEXT("A remainder that rounds up to ten seconds reads whole, never 10.0"),LDHUD::CooldownText(9950),FString(TEXT("10")));
    TestEqualSensitive(TEXT("Below ten seconds the remainder reads in tenths"),LDHUD::CooldownText(3200),FString(TEXT("3.2")));
    TestEqualSensitive(TEXT("Tenths round up"),LDHUD::CooldownText(3201),FString(TEXT("3.3")));
    TestEqualSensitive(TEXT("The last tenth below ten seconds"),LDHUD::CooldownText(9900),FString(TEXT("9.9")));
    TestEqualSensitive(TEXT("Any live remainder reads at least a tenth, never 0.0"),LDHUD::CooldownText(1),FString(TEXT("0.1")));
    LDHUD::FActionContext Context;Context.KeyLabel=TEXT("Q");Context.bOwnContext=true;Context.bPlaying=true;Context.bAlive=true;Context.bClock=true;Context.NowMs=4800;
    LDHUD::FActionInput Bolt;Bolt.Slot=LDHUD::EActionSlot::Q;Bolt.bProjected=true;Bolt.bHasSource=true;Bolt.Name=TEXT("技能甲");Bolt.ReadyAtMs=8000;Bolt.CooldownMs=8000;
    TestEqualSensitive(TEXT("The slot view carries the remainder drawn over the mask"),LDHUD::Action(Bolt,Context).Countdown,FString(TEXT("3.2")));
    Context.NowMs=8000;
    TestTrue(TEXT("The remainder disappears once the authority clock reaches the track end"),LDHUD::Action(Bolt,Context).Countdown.IsEmpty());
    Context.NowMs=4800;Context.bClock=false;
    TestTrue(TEXT("Without the authority clock no remainder is drawn"),LDHUD::Action(Bolt,Context).Countdown.IsEmpty());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDActionStatesTest,"Lockdown.Rules.HUDActionStates",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDActionStatesTest::RunTest(const FString& Parameters)
{
    LDHUD::FActionContext Context;Context.KeyLabel=TEXT("Q");Context.bOwnContext=true;Context.bPlaying=true;Context.bAlive=true;Context.bClock=true;Context.NowMs=4800;Context.bManaKnown=true;Context.ManaP2=10000;
    LDHUD::FActionInput Bolt;Bolt.Slot=LDHUD::EActionSlot::Q;Bolt.bProjected=true;Bolt.bHasSource=true;Bolt.Name=TEXT("技能甲");Bolt.ManaCostP2=3050;
    LDHUD::FActionView View=LDHUD::Action(Bolt,Context);
    LDHUD::EActionLook Shown=LDHUD::ActionLook(View,false);
    TestEqualSensitive(TEXT("The cost chip shows only the mana cost, rounded down like the shown mana"),View.Cost,FString(TEXT("30")));
    TestTrue(TEXT("A learned active shows its name's first character as the icon"),View.Glyph==TEXT("技"));
    TestTrue(TEXT("A ready, affordable active looks ready"),Shown==LDHUD::EActionLook::Ready);
    Bolt.ReadyAtMs=8000;Bolt.CooldownMs=8000;View=LDHUD::Action(Bolt,Context);Shown=LDHUD::ActionLook(View,false);
    TestTrue(TEXT("A cooling active looks cooling and carries its remainder"),Shown==LDHUD::EActionLook::Cooling&&View.Countdown==TEXT("3.2"));
    Context.ManaP2=1000;View=LDHUD::Action(Bolt,Context);
    TestTrue(TEXT("A cooldown outranks missing mana"),LDHUD::ActionLook(View,false)==LDHUD::EActionLook::Cooling);
    Bolt.ReadyAtMs=0;View=LDHUD::Action(Bolt,Context);Shown=LDHUD::ActionLook(View,false);
    TestTrue(TEXT("Missing mana without a cooldown has its own look and still sends"),Shown==LDHUD::EActionLook::NoMana&&View.State==LDTerms::NotEnoughMana()&&View.bCanSubmitIntent);
    Bolt.bControlled=true;View=LDHUD::Action(Bolt,Context);Shown=LDHUD::ActionLook(View,false);
    TestTrue(TEXT("A control status outranks missing mana"),Shown==LDHUD::EActionLook::Controlled);
    Bolt.ReadyAtMs=8000;View=LDHUD::Action(Bolt,Context);
    TestTrue(TEXT("A control status outranks the cooldown, whose remainder still shows"),LDHUD::ActionLook(View,false)==LDHUD::EActionLook::Controlled&&View.Countdown==TEXT("3.2"));
    Shown=LDHUD::ActionLook(View,true);
    TestTrue(TEXT("The armed entry outranks its remaining limits"),Shown==LDHUD::EActionLook::Armed);
    Bolt.bControlled=false;Bolt.ReadyAtMs=0;Context.ManaP2=10000;
    LDHUD::FActionInput Empty;Empty.Slot=LDHUD::EActionSlot::W;Empty.bProjected=true;View=LDHUD::Action(Empty,Context);Shown=LDHUD::ActionLook(View,true);
    TestTrue(TEXT("An empty slot looks empty even when armed and has no cost or icon glyph"),Shown==LDHUD::EActionLook::Empty&&View.Cost.IsEmpty()&&View.Glyph.IsEmpty()&&View.Name==TEXT("空槽"));
    Context.bAlive=false;View=LDHUD::Action(Bolt,Context);Shown=LDHUD::ActionLook(View,true);
    TestTrue(TEXT("A dead owner's slot is blocked, never armed"),Shown==LDHUD::EActionLook::Blocked&&View.State==LDTerms::Dead);
    Context.bAlive=true;Bolt.ServerBlock=TEXT("目标不可用");View=LDHUD::Action(Bolt,Context);Shown=LDHUD::ActionLook(View,false);
    TestTrue(TEXT("A published server reason blocks the look and its tooltip names the reason"),Shown==LDHUD::EActionLook::Blocked&&View.State==TEXT("目标不可用"));
    Bolt.ServerBlock.Reset();Bolt.ManaCostP2=0;
    TestTrue(TEXT("An active without a cost shows no chip"),LDHUD::Action(Bolt,Context).Cost.IsEmpty());
    LDHUD::FActionInput Unprojected=Bolt;Unprojected.bProjected=false;Unprojected.ManaCostP2=5000;View=LDHUD::Action(Unprojected,Context);
    TestTrue(TEXT("Without a projection there is no cost and the slot is unsynced"),View.Cost.IsEmpty()&&LDHUD::ActionLook(View,false)==LDHUD::EActionLook::Blocked);
    FLDCombatActionReceipt Receipt;Receipt.Origin=ELDCombatRequestOrigin::Direct;Receipt.Entry=ELDCombatEntry::W;Receipt.Phase=ELDCombatPhase::Rejected;Receipt.Reject=ELDCombatReject::OutOfRange;
    TestEqualSensitive(TEXT("The pressed slot shows the reason of the player's failed request"),LDHUD::CombatReceiptSlotNotice(Receipt),FString(TEXT("超出射程")));
    Receipt.Entry=ELDCombatEntry::Basic;
    TestTrue(TEXT("The basic attack has no slot to tell"),LDHUD::CombatReceiptSlotNotice(Receipt).IsEmpty());
    Receipt.Entry=ELDCombatEntry::Q;Receipt.Reject=ELDCombatReject::Cancelled;
    TestTrue(TEXT("A cancel tells the slot nothing"),LDHUD::CombatReceiptSlotNotice(Receipt).IsEmpty());
    Receipt.Reject=ELDCombatReject::Cooldown;Receipt.Origin=ELDCombatRequestOrigin::Nav;
    TestTrue(TEXT("A navigation-issued request tells the slot nothing"),LDHUD::CombatReceiptSlotNotice(Receipt).IsEmpty());
    Receipt.Origin=ELDCombatRequestOrigin::Direct;Receipt.Phase=ELDCombatPhase::Accepted;
    TestTrue(TEXT("An accepted request tells the slot nothing"),LDHUD::CombatReceiptSlotNotice(Receipt).IsEmpty());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDDeathCauseTest,"Lockdown.Rules.HUDDeathCause",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDDeathCauseTest::RunTest(const FString& Parameters)
{
    const FGuid Self=FGuid::NewGuid(),Rival=FGuid::NewGuid(),Other=FGuid::NewGuid();
    FLDLobbyView Lobby;
    FLDLobbyMember Me;Me.ContenderId=Self;Me.Name=TEXT("我");Lobby.Members.Add(Me);
    FLDLobbyMember Killer;Killer.ContenderId=Rival;Killer.Name=TEXT("玩家甲");Lobby.Members.Add(Killer);
    FLDKillFeedView Feed;Feed.Generation=7;
    auto AddNotice=[&Feed](uint64 Serial,const FGuid& Victim,const FGuid& By,ELDKillCause Kind,int64 AtMs)
    {FLDKillNotice Entry;Entry.Serial=Serial;Entry.Victim=Victim;Entry.Killer=By;Entry.Cause=Kind;Entry.OccurredAtMs=AtMs;Feed.Notices.Add(Entry);};
    auto CauseAt=[&Feed,&Lobby,&Self](uint32 Generation,int64 NotBeforeMs){return LDHUD::DeathCause(Feed,Generation,Self,NotBeforeMs,Lobby,LDTerms::CampLow,LDTerms::CampHigh,LDTerms::CampElite,LDTerms::Boss);};
    AddNotice(1,Self,FGuid(),ELDKillCause::CampHigh,10000);
    AddNotice(2,Other,Rival,ELDKillCause::Contender,30000);
    AddNotice(3,Self,Rival,ELDKillCause::Contender,50000);
    TestEqualSensitive(TEXT("The newest own death names its killer from the public roster"),CauseAt(7,40000),LDTerms::KilledBy(TEXT("玩家甲")));
    TestTrue(TEXT("A feed of another generation is not this match"),CauseAt(8,0).IsEmpty()&&CauseAt(0,0).IsEmpty());
    TestTrue(TEXT("Before this life's notice arrives an older death never stands in"),CauseAt(7,60000).IsEmpty());
    TestTrue(TEXT("Without an own identity there is no cause"),LDHUD::DeathCause(Feed,7,FGuid(),0,Lobby,LDTerms::CampLow,LDTerms::CampHigh,LDTerms::CampElite,LDTerms::Boss).IsEmpty());
    Feed.Notices.RemoveAt(2);
    TestEqualSensitive(TEXT("A camp death names the approved creature of its tier"),CauseAt(7,0),LDTerms::KilledBy(LDTerms::CampHigh));
    TestTrue(TEXT("Another contender's death is never the player's cause"),CauseAt(7,20000).IsEmpty());
    AddNotice(4,Self,FGuid(),ELDKillCause::CampLow,70000);
    TestEqualSensitive(TEXT("The low camp tier has its own creature"),CauseAt(7,60000),LDTerms::KilledBy(LDTerms::CampLow));
    AddNotice(5,Self,FGuid::NewGuid(),ELDKillCause::Contender,80000);
    TestEqualSensitive(TEXT("A killer missing from the roster is not given an invented name"),CauseAt(7,60000),LDTerms::KilledBy(TEXT("未列名参赛者")));
    AddNotice(6,Self,FGuid(),ELDKillCause::Unowned,90000);
    TestEqualSensitive(TEXT("A death without a contender owner says so"),CauseAt(7,60000),FString(TEXT("无参赛者击杀")));
    FLDOwnerState Own;Own.LifePhase=ELDLifePhase::Dead;Own.RespawnAtMs=12000;
    const FString Note=LDHUD::Death(Own,true,0).Note;
    TestTrue(TEXT("The death note is the one short glossary line that keeps the build"),Note.Len()<=24&&!Note.Contains(TEXT("；"))&&!Note.Contains(TEXT("\n"))
        &&Note==LDTerms::DeathNote());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDKillFeedSelfTest,"Lockdown.Rules.HUDKillFeedSelf",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDKillFeedSelfTest::RunTest(const FString& Parameters)
{
    const FGuid Self=FGuid::NewGuid(),Rival=FGuid::NewGuid(),Other=FGuid::NewGuid();
    auto Line=[](const FLDKillNotice& Source,const FGuid& Viewer,const TCHAR* VictimName,const TCHAR* KillerName)
    {return LDHUD::KillFeedLine(Source,Viewer,VictimName,KillerName,LDTerms::CampLow,LDTerms::CampHigh,LDTerms::CampElite,LDTerms::Boss);};
    FLDKillNotice Notice;Notice.Cause=ELDKillCause::Contender;Notice.Victim=Rival;Notice.Killer=Self;
    LDHUD::FKillFeedLineView Shown=Line(Notice,Self,TEXT("乙"),TEXT("我"));
    TestTrue(TEXT("The player's own kill says 你 and is highlighted"),Shown.bSelf&&Shown.Text==LDTerms::KillLine(TEXT("你"),TEXT("乙")));
    Notice.Victim=Self;Notice.Killer=Rival;Shown=Line(Notice,Self,TEXT("我"),TEXT("甲"));
    TestTrue(TEXT("The player's own death says 你 and is highlighted"),Shown.bSelf&&Shown.Text==LDTerms::KillLine(TEXT("甲"),TEXT("你")));
    Notice.Cause=ELDKillCause::CampLow;Notice.Killer.Invalidate();Shown=Line(Notice,Self,TEXT("我"),TEXT(""));
    TestTrue(TEXT("A camp death of the player says 你"),Shown.bSelf&&Shown.Text==LDTerms::KilledByLine(TEXT("你"),LDTerms::CampLow));
    Notice.Cause=ELDKillCause::Contender;Notice.Victim=Other;Notice.Killer=Rival;Shown=Line(Notice,Self,TEXT("丙"),TEXT("甲"));
    TestTrue(TEXT("Other contenders keep their roster names and are not highlighted"),!Shown.bSelf&&Shown.Text==LDTerms::KillLine(TEXT("甲"),TEXT("丙")));
    Notice.Victim=Self;
    TestFalse(TEXT("Without an own identity nothing is marked or renamed"),Line(Notice,FGuid(),TEXT("我"),TEXT("甲")).bSelf||Line(Notice,FGuid(),TEXT("我"),TEXT("甲")).Text.Contains(TEXT("你")));
    Notice.Cause=ELDKillCause::CampHigh;Notice.Victim=Other;Notice.Killer=Self;Shown=Line(Notice,Self,TEXT("丙"),TEXT("我"));
    TestTrue(TEXT("Only a contender kill can credit the player"),!Shown.bSelf&&Shown.Text==LDTerms::KilledByLine(TEXT("丙"),LDTerms::CampHigh));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDScoreboardTest,"Lockdown.Rules.HUDScoreboard",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDScoreboardTest::RunTest(const FString& Parameters)
{
    const FGuid Ana=FGuid::NewGuid(),Bo=FGuid::NewGuid(),Cy=FGuid::NewGuid(),Di=FGuid::NewGuid(),Gone=FGuid::NewGuid();
    FLDLobbyView Lobby;Lobby.Generation=3;
    for(const TPair<FGuid,const TCHAR*>& Member:TArray<TPair<FGuid,const TCHAR*>>{{Ana,TEXT("阿娜")},{Bo,TEXT("阿博")},{Cy,TEXT("阿西")},{Di,TEXT("阿迪")}})
    {FLDLobbyMember& Row=Lobby.Members.AddDefaulted_GetRef();Row.ContenderId=Member.Key;Row.Name=Member.Value;}
    Lobby.Members[3].Kind=ELDContenderKind::OpponentAI;
    FLDKillFeedView Feed;Feed.Generation=3;
    Feed.Scores={{Ana,1,2,5},{Bo,3,4,0},{Cy,3,1,1},{Di,1,2,0},{Gone,0,0,0}};
    const TArray<LDHUD::FScoreLine> Lines=LDHUD::Scoreboard(Feed,3,Lobby,Ana);
    if(!TestEqual(TEXT("Every contender of the match has a row"),Lines.Num(),5)){return false;}
    TestEqual(TEXT("Most kills first; the tie goes to fewer deaths"),Lines[0].Name,FString(TEXT("阿西")));
    TestEqual(TEXT("Then the other three-kill contender"),Lines[1].Name,FString(TEXT("阿博")));
    TestEqual(TEXT("Equal kills and deaths keep roster order; the own row says so"),Lines[2].Name,FString(TEXT("阿娜（你）")));
    TestTrue(TEXT("Only the own row is marked as the player"),Lines[2].bSelf&&!Lines[0].bSelf&&!Lines[3].bSelf);
    TestEqual(TEXT("An AI opponent is named as one"),Lines[3].Name,FString(TEXT("阿迪（AI）")));
    TestEqual(TEXT("A contender no longer in the room is shown as having left"),Lines[4].Name,FString(TEXT("（已离开）")));
    TestTrue(TEXT("The row keeps its own kills, deaths and assists"),Lines[2].Kills==1&&Lines[2].Deaths==2&&Lines[2].Assists==5);
    TestTrue(TEXT("Another match's feed shows no scoreboard"),LDHUD::Scoreboard(Feed,4,Lobby,Ana).IsEmpty());
    TestTrue(TEXT("No match shows no scoreboard"),LDHUD::Scoreboard(Feed,0,Lobby,Ana).IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDEventFeedTest,"Lockdown.Rules.HUDEventFeed",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDEventFeedTest::RunTest(const FString& Parameters)
{
    const FGuid Self=FGuid::NewGuid(),Rival=FGuid::NewGuid();
    FLDLobbyView Lobby;
    {FLDLobbyMember Member;Member.ContenderId=Self;Member.Name=TEXT("我");Lobby.Members.Add(Member);}
    {FLDLobbyMember Member;Member.ContenderId=Rival;Member.Name=TEXT("甲");Lobby.Members.Add(Member);}
    FLDPublicMatchView Public;Public.Generation=3;Public.Phase=ELDMatchPhase::Development;
    Public.Airdrop.Serial=1;Public.Airdrop.Phase=ELDAirdropPhase::Claimed;Public.Boss.Phase=ELDBossPhase::Pending;
    Public.Leader.Leader=Rival;
    FLDKillFeedView Kills;Kills.Generation=3;
    FLDKillNotice Old;Old.Serial=1;Old.Cause=ELDKillCause::Contender;Old.Victim=Rival;Old.Killer=Self;Old.OccurredAtMs=1000;Kills.Notices.Add(Old);
    LDHUD::FEventFeedInput In;In.Public=&Public;In.Kills=&Kills;In.Lobby=&Lobby;In.Self=Self;In.bClock=true;In.NowMs=20000;In.LocalSeconds=100;
    In.LowCampName=LDTerms::CampLow;In.HighCampName=LDTerms::CampHigh;In.EliteCampName=LDTerms::CampElite;In.BossName=LDTerms::Boss;
    LDHUD::FEventFeed Feed;
    auto Texts=[&Feed](){TArray<FString> Out;for(const LDHUD::FEventLine& Line:Feed.Lines()){Out.Add(Line.Text);}return Out;};
    Feed.Observe(In);
    TestTrue(TEXT("Joining mid-match replays neither old kills nor the state already in place"),Feed.Lines().IsEmpty());
    // A fresh kill, the flagpole opening, a new drop landing, the Boss appearing, a new leader and a new graffiti owner.
    FLDKillNotice Fresh;Fresh.Serial=2;Fresh.Cause=ELDKillCause::Contender;Fresh.Victim=Self;Fresh.Killer=Rival;Fresh.OccurredAtMs=19500;Kills.Notices.Add(Fresh);
    Public.Phase=ELDMatchPhase::CaptureOpen;Public.Airdrop.Serial=2;Public.Airdrop.Phase=ELDAirdropPhase::Landed;Public.Boss.Phase=ELDBossPhase::Alive;
    Public.Leader.Leader=Self;Public.GraffitiOwner=Rival;
    In.LocalSeconds=101;Feed.Observe(In);
    const TArray<FString> Told={LDTerms::KillLine(TEXT("甲"),TEXT("你")),FString(LDTerms::CaptureOpened),FString(LDTerms::AirdropLanded),LDTerms::BossAppeared(LDTerms::Boss),
        LDTerms::LeaderSelf(),LDTerms::GraffitiSprayed(TEXT("甲"))};
    TestTrue(TEXT("Each change is told once, oldest first so the newest sits at the bottom"),Texts()==Told);
    TestTrue(TEXT("Lines about the player are highlighted"),Feed.Lines()[0].bSelf&&!Feed.Lines()[1].bSelf&&Feed.Lines()[4].bSelf&&!Feed.Lines()[5].bSelf);
    In.LocalSeconds=102;Feed.Observe(In);
    TestEqual(TEXT("An unchanged state tells nothing again"),Feed.Lines().Num(),Told.Num());
    // The drop is claimed: everyone hears it is gone until the own claim serial says this screen took it.
    Public.Airdrop.Phase=ELDAirdropPhase::Claimed;In.LocalSeconds=103;Feed.Observe(In);
    TestEqual(TEXT("Beyond the row limit the oldest line leaves early"),Feed.Lines().Num(),LDHUD::FEventFeed::MaxLines);
    TestEqualSensitive(TEXT("A claim without the own serial only says the drop is gone"),Feed.Lines().Last().Text,LDTerms::AirdropTaken());
    In.OwnAirdropClaimSerial=2;In.LocalSeconds=103.5;Feed.Observe(In);
    TestTrue(TEXT("The own claim, arriving later, relabels the shown line"),Feed.Lines().Last().Text==LDTerms::AirdropOwnClaim()&&Feed.Lines().Last().bSelf);
    Public.Boss.Phase=ELDBossPhase::Down;Public.Boss.KilledBy=Rival;In.LocalSeconds=104;Feed.Observe(In);
    TestEqualSensitive(TEXT("A downed Boss names its last hit"),Feed.Lines().Last().Text,LDTerms::BossDownBy(TEXT("甲"),LDTerms::Boss));
    In.LocalSeconds=106.2;Feed.Observe(In);
    TestTrue(TEXT("Each line leaves after its five seconds"),Feed.Lines().Num()==2&&Feed.Lines()[0].Text==LDTerms::AirdropOwnClaim());
    In.LocalSeconds=110;Feed.Observe(In);
    TestTrue(TEXT("All lines are gone once their time is up"),Feed.Lines().IsEmpty());
    // The owner's guard frozen by a challenger drops to zero when he leaves the zone; only that drop is told.
    Public.CircleState=ELDCircleState::Contested;Public.GuardKeptMs=4000;In.LocalSeconds=111;Feed.Observe(In);
    TestTrue(TEXT("A guard freezing is no event line"),Feed.Lines().IsEmpty());
    Public.CircleState=ELDCircleState::Solo;Public.GuardKeptMs=0;In.LocalSeconds=112;Feed.Observe(In);
    TestTrue(TEXT("A frozen guard dropping to zero tells that its owner left the zone"),
        Feed.Lines().Num()==1&&Feed.Lines()[0].Text==LDTerms::GuardReset&&!Feed.Lines()[0].bSelf);
    In.LocalSeconds=113;Feed.Observe(In);
    TestEqual(TEXT("A guard reset is told once"),Feed.Lines().Num(),1);
    Public.CircleState=ELDCircleState::Guarded;Public.GuardDueAtMs=50000;In.LocalSeconds=114;Feed.Observe(In);
    TestEqual(TEXT("A guard that runs again tells nothing"),Feed.Lines().Num(),1);
    Public.CircleState=ELDCircleState::Empty;Public.GuardDueAtMs=0;Public.GuardKeptMs=1500;In.LocalSeconds=114.5;Feed.Observe(In);
    TestEqual(TEXT("A guard paused by its owner leaving the empty zone is no reset line"),Feed.Lines().Num(),1);
    // A new match starts empty and does not tell the state it opens with.
    Public.Generation=4;Kills.Generation=4;Kills.Notices.Reset();Public.GraffitiOwner.Invalidate();Public.GuardDueAtMs=0;Public.GuardKeptMs=0;In.LocalSeconds=120;Feed.Observe(In);
    TestTrue(TEXT("A new match starts with an empty feed"),Feed.Lines().IsEmpty());
    // #198 outer §5: a drop expiring unclaimed, the Boss leaving, and camp markers revealed, taken and expiring.
    Public.Airdrop.Serial=3;Public.Airdrop.Phase=ELDAirdropPhase::Expired;Public.Boss.Phase=ELDBossPhase::Left;Public.Boss.KilledBy.Invalidate();
    In.LocalSeconds=121;Feed.Observe(In);
    TestTrue(TEXT("An expired drop and a Boss that left are told"),
        Texts()==TArray<FString>({LDTerms::AirdropExpired(),LDTerms::BossLeft(LDTerms::Boss)}));
    In.LocalSeconds=130;Feed.Observe(In);
    FLDFieldMarkerView HighMark;HighMark.Segment=1;HighMark.Kind=ELDFieldMarkerKind::High;HighMark.CampId=TEXT("h03");
    HighMark.Phase=ELDFieldMarkerPhase::Revealed;HighMark.RevealAtMs=15000;HighMark.EndAtMs=180000;
    FLDFieldMarkerView EliteMark=HighMark;EliteMark.Kind=ELDFieldMarkerKind::Elite;EliteMark.CampId=TEXT("e03");
    Public.Markers={HighMark,EliteMark};In.LocalSeconds=131;Feed.Observe(In);
    TestTrue(TEXT("A segment's reveal tells both markers"),Texts()==TArray<FString>({FString(LDTerms::CampMarkHigh),FString(LDTerms::CampMarkElite)}));
    Public.Markers[0].Phase=ELDFieldMarkerPhase::Claimed;In.LocalSeconds=132;Feed.Observe(In);
    TestTrue(TEXT("A taken marker never names who took it"),Feed.Lines().Last().Text==LDTerms::CampMarkTaken(true)&&!Feed.Lines().Last().bSelf);
    TArray<FLDFieldRewardView> OwnRewards;
    {FLDFieldRewardView Mine;Mine.Event=TEXT("camp_marker_high");Mine.Serial=1;OwnRewards.Add(Mine);}
    In.OwnFieldRewards=&OwnRewards;In.LocalSeconds=132.5;Feed.Observe(In);
    TestTrue(TEXT("The own marker reward, arriving later, relabels the shown line"),
        Feed.Lines().Last().Text==LDTerms::CampMarkOwnClaim(true)&&Feed.Lines().Last().bSelf);
    Public.Markers[1].Phase=ELDFieldMarkerPhase::Expired;In.LocalSeconds=133;Feed.Observe(In);
    TestEqualSensitive(TEXT("An unclaimed marker expiring is told"),Feed.Lines().Last().Text,LDTerms::CampMarkExpired());
    const int32 Shown=Feed.Lines().Num();
    In.LocalSeconds=133.5;Feed.Observe(In);
    TestEqual(TEXT("Marker changes are told once"),Feed.Lines().Num(),Shown);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDChoicesFoldTest,"Lockdown.Rules.HUDChoicesFold",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDChoicesFoldTest::RunTest(const FString& Parameters)
{
    const double Wait=LDHUD::FChoicesFold::AutoOpenSeconds;
    {
        LDHUD::FChoicesFold Fold;
        TestFalse(TEXT("A closed panel never folds"),Fold.ShouldFold(100.,false,true));
        Fold.Opened(true,10.);
        TestTrue(TEXT("The opening's automatic open is marked automatic"),Fold.IsAutomatic());
        TestFalse(TEXT("The automatic open stays while its few seconds run"),Fold.ShouldFold(10.+Wait-.01,false,false));
        TestTrue(TEXT("The automatic open folds back to the pending pill after its few seconds"),Fold.ShouldFold(10.+Wait,false,false));
        Fold.Closed();
        TestFalse(TEXT("A folded panel is closed"),Fold.IsAutomatic()||Fold.ShouldFold(10.+Wait+1.,false,true));
    }
    {
        LDHUD::FChoicesFold Fold;Fold.Opened(true,0.);
        TestTrue(TEXT("An own hit folds the automatic opening at once"),Fold.ShouldFold(1.,false,true));
    }
    {
        LDHUD::FChoicesFold Fold;Fold.Opened(true,0.);
        TestFalse(TEXT("A previewed card or a request in flight keeps the panel open"),Fold.ShouldFold(Wait+1.,true,true));
        TestFalse(TEXT("Engaging makes the automatic open the player's own: neither time nor hits fold it"),
            Fold.IsAutomatic()||Fold.ShouldFold(Wait+60.,false,false)||Fold.ShouldFold(Wait+61.,false,true));
    }
    {
        // #222: the player builds in combat and in a PK through the real UI, so a panel the player opened stays open.
        LDHUD::FChoicesFold Fold;Fold.Opened(false,0.);
        TestFalse(TEXT("A panel the player opened never times out and never folds on a hit"),Fold.IsAutomatic()||Fold.ShouldFold(600.,false,false)
            ||Fold.ShouldFold(601.,false,true));
    }
    {
        // The HUD reads hits from the same own-body changes the world feedback floats: lost health or an absorbed shield.
        LDWorldFeedback::FTracker Hits;FLDCombatOwnerView Combat;Combat.Generation=4;Combat.Life=2;Combat.bHasBody=true;Combat.Revision=1;Combat.HPP2=50000;Combat.MaxHPP2=60000;
        FLDCombatStatusView Shield;Shield.StatusId=uint8(ELDDemoStatusId::Shield);Shield.ExpiresAtMs=90000;Shield.AmountP2=3000;Combat.Statuses.Add(Shield);Combat.ShieldP2=3000;Combat.AsOfMs=1000;
        LDWorldFeedback::FSample Body;Body.Match=4;Body.Life=2;Body.LifePhase=ELDLifePhase::Alive;Body.OwnCombat=&Combat;
        auto Observe=[&Hits,&Body](){TArray<LDWorldFeedback::FEvent> Events;Hits.Observe(Body,Events);return Events;};
        auto IsHit=[](const TArray<LDWorldFeedback::FEvent>& Events){return Events.ContainsByPredicate([](const LDWorldFeedback::FEvent& Event)
            {return Event.bOwn&&(Event.Kind==LDWorldFeedback::EKind::DamageTaken||Event.Kind==LDWorldFeedback::EKind::ShieldAbsorbed);});};
        TestFalse(TEXT("The first own-body sample only primes"),IsHit(Observe()));
        Combat.Revision=2;Combat.ShieldP2=1000;Combat.Statuses[0].AmountP2=1000;Combat.AsOfMs=1500;
        TestTrue(TEXT("A hit fully absorbed by the shield is a hit"),IsHit(Observe()));
        Combat.Revision=3;Combat.ShieldP2=0;Combat.Statuses.Reset();Combat.HPP2=48000;Combat.AsOfMs=2000;
        TestTrue(TEXT("Lost health is a hit"),IsHit(Observe()));
        Combat.Revision=4;Combat.HPP2=48100;Combat.AsOfMs=3000;
        TestFalse(TEXT("Regeneration is not a hit"),IsHit(Observe()));
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDBuildGateTest,"Lockdown.Rules.HUDBuildGate",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDBuildGateTest::RunTest(const FString& Parameters)
{
    // #198 growth panel: the disabled reasons follow the server's command gate (a hint only; the server decides each command).
    using EStage=LDProgression::EMatchStage;
    LDHUD::FBuildGateView Gate=LDHUD::BuildGate(EStage::Rest,ELDLifePhase::Alive);
    TestTrue(TEXT("A rest build window allows every build step and rest service"),Gate.bCanChoose&&Gate.bCanLearn&&Gate.bCanAllocate&&Gate.bCanInstall
        &&Gate.bCanBuyNpc&&Gate.bCanRespec&&Gate.bCanReplace&&Gate.bCanForget&&Gate.bInRest
        &&Gate.ChooseBlocked.IsEmpty()&&Gate.LearnBlocked.IsEmpty()&&Gate.AllocateBlocked.IsEmpty()&&Gate.NpcBlocked.IsEmpty());
    Gate=LDHUD::BuildGate(EStage::RestLocked,ELDLifePhase::Alive);
    TestTrue(TEXT("#222: a PK or return step keeps the own build open and locks only the rest services"),Gate.bCanChoose&&Gate.bCanLearn&&Gate.bCanAllocate
        &&Gate.bCanInstall&&!Gate.bCanBuyNpc&&!Gate.bCanRespec&&!Gate.bCanReplace&&!Gate.bCanForget&&Gate.bInRest);
    TestEqualSensitive(TEXT("The locked reason names the lock"),Gate.NpcBlocked,LDHUD::GrowthReasonText(ELDGrowthReason::BuildLocked));
    Gate=LDHUD::BuildGate(EStage::Rest,ELDLifePhase::Dead);
    TestTrue(TEXT("#222: a dead contender in a rest chooses, learns, allocates and installs; the services wait for a living body"),
        Gate.bCanChoose&&Gate.bCanAllocate&&Gate.bCanLearn&&Gate.bCanInstall&&!Gate.bCanBuyNpc&&!Gate.bCanForget);
    Gate=LDHUD::BuildGate(EStage::Live,ELDLifePhase::Alive);
    TestTrue(TEXT("#222: on the field every own build step is open, in combat too"),Gate.bCanChoose&&Gate.bCanLearn&&Gate.bCanAllocate&&Gate.bCanInstall
        &&Gate.LearnBlocked.IsEmpty()&&Gate.AllocateBlocked.IsEmpty());
    TestTrue(TEXT("The field hides the rest services and names why they are refused"),!Gate.bInRest&&!Gate.bCanBuyNpc&&!Gate.bCanRespec&&!Gate.bCanReplace&&!Gate.bCanForget
        &&Gate.NpcBlocked==LDHUD::GrowthReasonText(ELDGrowthReason::NotRestWindow)&&Gate.ReplaceBlocked==LDHUD::GrowthReasonText(ELDGrowthReason::ReplacementNotAllowed));
    Gate=LDHUD::BuildGate(EStage::Live,ELDLifePhase::Dead);
    TestTrue(TEXT("#222: a dead contender on the field builds as well"),Gate.bCanChoose&&Gate.bCanAllocate&&Gate.bCanLearn&&Gate.bCanInstall);
    Gate=LDHUD::BuildGate(EStage::Closed,ELDLifePhase::Alive);
    TestTrue(TEXT("Results close every build step with a reason"),!Gate.bCanChoose&&!Gate.bCanLearn&&!Gate.bCanAllocate&&!Gate.bCanInstall
        &&Gate.ChooseBlocked==TEXT("现在不是成长时段"));
    TestFalse(TEXT("An unspawned contender sends nothing"),LDHUD::BuildGate(EStage::Rest,ELDLifePhase::Unspawned).bCanChoose);
    {
        // The stage comes from the public match the same way the server reads it.
        FLDTimelineView Timeline;Timeline.Stage=ELDMatchStage::Rest;Timeline.Step=ELDRestStep::Build;
        TestTrue(TEXT("A rest's build step is the rest build window"),LDProgression::StageForPublic(ELDMatchPhase::Rest,Timeline)==EStage::Rest);
        Timeline.Step=ELDRestStep::Payout;
        TestTrue(TEXT("The payout reopens the build"),LDProgression::StageForPublic(ELDMatchPhase::Rest,Timeline)==EStage::Rest);
        Timeline.Step=ELDRestStep::Return;
        TestTrue(TEXT("The return countdown locks the build"),LDProgression::StageForPublic(ELDMatchPhase::Rest,Timeline)==EStage::RestLocked);
        TestTrue(TEXT("A field segment is live"),LDProgression::StageForPublic(ELDMatchPhase::Development,FLDTimelineView())==EStage::Live);
        FLDTimelineView FinalField;
        FinalField.Stage=ELDMatchStage::FinalField;
        TestTrue(TEXT("The final field closes ordinary growth commands"),
            LDProgression::StageForPublic(ELDMatchPhase::Development,FinalField)==EStage::Closed
            && LDProgression::StageForPublic(ELDMatchPhase::CaptureOpen,FinalField)==EStage::Closed);
        TestTrue(TEXT("Results are closed"),LDProgression::StageForPublic(ELDMatchPhase::Results,FLDTimelineView())==EStage::Closed);
    }
    TestEqualSensitive(TEXT("No empty slot names the rest-only replacement"),LDHUD::BuildBlockedText(ELDGrowthReason::SlotOccupied),FString(LDTerms::ReplaceAtRest));
    TestEqualSensitive(TEXT("A full Q/W/E says so"),LDHUD::BuildBlockedText(ELDGrowthReason::NoEmptySlot),FString(LDTerms::SlotsFull));

    // Receipt lines: learned into a slot, stored as a blueprint with the reason, or the points added and left.
    FLDBuildReceipt Receipt;Receipt.Header.Outcome=ELDReceiptOutcome::Applied;Receipt.Kind=ELDBuildCommandKind::ActiveChoice;
    Receipt.DefinitionId=TEXT("hud_rules_fixture_bolt");Receipt.ChoiceResult=ELDBuildChoiceResult::Learned;Receipt.LearnedSlot=2;
    TestEqualSensitive(TEXT("A learned choice names its slot"),LDHUD::BuildReceiptText(Receipt,nullptr),FString(TEXT("已学到 E：hud_rules_fixture_bolt")));
    {
        // The slot reads as the key the HUD shows for it (rebound keys, dragged slot order), never the server's letter.
        LDHUD::FSlotKeys Shown;Shown.Keys[0]=TEXT("E");Shown.Keys[1]=TEXT("Z");Shown.Keys[2]=TEXT("W");
        TestEqualSensitive(TEXT("A learned choice names the slot's shown key"),LDHUD::BuildReceiptText(Receipt,nullptr,Shown),FString(TEXT("已学到 W：hud_rules_fixture_bolt")));
        TestEqualSensitive(TEXT("Each server slot reads its shown key"),Shown.Of(0)+Shown.Of(1)+Shown.Of(2),FString(TEXT("EZW")));
        TestEqualSensitive(TEXT("An unset key reads as the server slot"),LDHUD::FSlotKeys().Of(1),FString(TEXT("W")));
    }
    Receipt.ChoiceResult=ELDBuildChoiceResult::StoredAsBlueprint;Receipt.StoredReason=uint16(ELDGrowthReason::NoEmptySlot);
    TestEqualSensitive(TEXT("A full Q/W/E stores the choice"),LDHUD::BuildReceiptText(Receipt,nullptr),FString(TEXT("已存为图纸：hud_rules_fixture_bolt（Q/W/E 已满）")));
    Receipt.ChoiceResult=ELDBuildChoiceResult::None;
    TestEqualSensitive(TEXT("A receipt without a result never claims one"),LDHUD::BuildReceiptText(Receipt,nullptr),FString(TEXT("回执缺少结果，请以当前状态为准。")));
    Receipt.Kind=ELDBuildCommandKind::AllocatePoints;Receipt.PointsSpent={2,0,1,0,0};Receipt.UnspentPointsAfter=3;
    TestEqualSensitive(TEXT("An allocation names the points added and left"),LDHUD::BuildReceiptText(Receipt,nullptr),FString(TEXT("已加 3 点，还剩 3 点")));
    Receipt.Kind=ELDBuildCommandKind::SkipActiveOffer;Receipt.PointsAwarded=5;
    TestEqualSensitive(TEXT("A skipped offer names the awarded points"),LDHUD::BuildReceiptText(Receipt,nullptr),FString(TEXT("已放弃本组技能，获得 5 属性点。")));
    Receipt.PointsAwarded=0;
    TestEqualSensitive(TEXT("A skip receipt without its award claims nothing"),LDHUD::BuildReceiptText(Receipt,nullptr),FString(TEXT("回执缺少结果，请以当前状态为准。")));
    Receipt.Header.Outcome=ELDReceiptOutcome::Rejected;Receipt.Header.Reason=uint16(ELDGrowthReason::NotEnoughPoints);
    TestEqualSensitive(TEXT("A refused command names its reason"),LDHUD::BuildReceiptText(Receipt,nullptr),FString(TEXT("没有生效：未分配点数不够")));
    {
        // #198 rest services: each applied receipt names what it did; a missing field never reads as a success.
        FLDBuildReceipt Rest;Rest.Header.Outcome=ELDReceiptOutcome::Applied;
        Rest.Kind=ELDBuildCommandKind::BuyNpcOffer;Rest.NpcPointsPaid=4;
        TestEqualSensitive(TEXT("An NPC purchase names the points paid"),LDHUD::BuildReceiptText(Rest,nullptr),LDTerms::NpcBought(4));
        Rest.NpcPointsPaid=0;
        TestEqualSensitive(TEXT("An NPC receipt without its payment claims nothing"),LDHUD::BuildReceiptText(Rest,nullptr),FString(TEXT("回执缺少结果，请以当前状态为准。")));
        Rest.Kind=ELDBuildCommandKind::Respec;Rest.PointsRefunded=5;Rest.UnspentPointsAfter=7;
        TestEqualSensitive(TEXT("A respec names the refund and the unspent points"),LDHUD::BuildReceiptText(Rest,nullptr),FString(TEXT("已洗点：返还 5 点，现有 7 点未分配")));
        Rest.Kind=ELDBuildCommandKind::ReplaceActive;Rest.ChoiceResult=ELDBuildChoiceResult::Learned;Rest.LearnedSlot=1;
        Rest.DefinitionId=TEXT("hud_rules_fixture_bolt");Rest.ForgottenId=TEXT("hud_rules_fixture_old");
        TestEqualSensitive(TEXT("A replacement names the slot, the new and the forgotten active"),LDHUD::BuildReceiptText(Rest,nullptr),
            FString(TEXT("已在 W 用 hud_rules_fixture_bolt 替换 hud_rules_fixture_old")));
        Rest.ForgottenId=NAME_None;
        TestEqualSensitive(TEXT("A replacement without the forgotten active claims nothing"),LDHUD::BuildReceiptText(Rest,nullptr),FString(TEXT("回执缺少结果，请以当前状态为准。")));
        Rest.Kind=ELDBuildCommandKind::ForgetActive;Rest.ForgottenId=TEXT("hud_rules_fixture_old");
        TestEqualSensitive(TEXT("A forget names the forgotten active"),LDHUD::BuildReceiptText(Rest,nullptr),LDTerms::Forgotten(TEXT("hud_rules_fixture_old")));
        Rest.Header.Outcome=ELDReceiptOutcome::Rejected;Rest.Header.Reason=uint16(ELDGrowthReason::NpcPoolTooSmall);
        TestEqualSensitive(TEXT("A refused rest service names its reason"),LDHUD::BuildReceiptText(Rest,nullptr),
            FString(TEXT("没有生效："))+LDHUD::GrowthReasonText(ELDGrowthReason::NpcPoolTooSmall));
        const ELDGrowthReason RestReasons[]={ELDGrowthReason::ReplacementNotAllowed,ELDGrowthReason::BuildLocked,ELDGrowthReason::NpcPoolTooSmall,
            ELDGrowthReason::OfferOutstanding,ELDGrowthReason::NothingToRespec,ELDGrowthReason::SlotEmpty,ELDGrowthReason::NotRestWindow};
        TSet<FString> Seen;
        for(const ELDGrowthReason Reason:RestReasons)
        {
            const FString Text=LDHUD::GrowthReasonText(Reason);bool bDuplicate=false;Seen.Add(Text,&bDuplicate);
            TestTrue(FString::Printf(TEXT("Rest reason %d has its own player text"),int32(Reason)),!Text.IsEmpty()&&!Text.StartsWith(TEXT("未知原因"))&&!bDuplicate);
        }
    }

    // The 构筑 button is lit by what the tray has cards for: the shown offer and learnable blueprints of the current match's
    // own view; unspent points wait on the HUD's stat icons instead.
    FLDProgressionView View;View.bValid=true;View.Generation=5;View.QueuedActiveOpportunities=2;View.UnspentPoints=3;
    LDHUD::FGrowthPending Pending=LDHUD::GrowthPending(&View,5,nullptr);
    TestTrue(TEXT("Queued opportunities without a shown offer have no card to light the button"),!Pending.Any()&&Pending.Offers==2);
    View.bHasActiveOffer=true;View.ActiveOffer.Options={FName(TEXT("active_elbow")),FName(TEXT("active_bolt"))};
    Pending=LDHUD::GrowthPending(&View,5,nullptr);
    TestTrue(TEXT("A shown offer lights the button and keeps the queue count for the pulse"),Pending.Any()&&Pending.bOffer&&Pending.Offers==2);
    View.bHasActiveOffer=false;View.ActiveOffer.Options.Reset();View.QueuedActiveOpportunities=0;
    TestFalse(TEXT("Unspent points alone do not light the button"),LDHUD::GrowthPending(&View,5,nullptr).Any());
    View.bHasActiveOffer=true;View.ActiveOffer.Options={FName(TEXT("active_elbow"))};
    TestFalse(TEXT("Another match's view lights nothing"),LDHUD::GrowthPending(&View,4,nullptr).Any()||LDHUD::GrowthPending(nullptr,5,nullptr).Any());
    View.bHasActiveOffer=false;View.ActiveOffer.Options.Reset();View.UnspentPoints=0;View.PendingBlueprints={FName(TEXT("active_elbow"))};
    Pending=LDHUD::GrowthPending(&View,5,nullptr);
    TestTrue(TEXT("A blueprint with an empty slot lights it alone"),Pending.Any()&&Pending.Blueprints==1);
    for(uint8 Key=0;Key<3;++Key){FLDActiveSlotView& Held=View.Actives.AddDefaulted_GetRef();Held.Slot=Key;Held.DefinitionId=FName(*FString::Printf(TEXT("held_%d"),Key));}
    TestFalse(TEXT("A blueprint waiting for the rest-only replacement does not light it"),LDHUD::GrowthPending(&View,5,nullptr).Any());
    TestTrue(TEXT("The slot reads come from the view"),LDHUD::ActiveInSlot(View,1)==FName(TEXT("held_1"))&&LDHUD::SlotKey(1)==TEXT("W"));
    FLDStatPointsView& Physique=View.StatPoints.AddDefaulted_GetRef();Physique.StatId=TEXT("stat_physique");Physique.Points=4;
    TestTrue(TEXT("Stat points come from the view, 0 for an unlisted stat"),LDHUD::StatPointsOf(View,TEXT("stat_physique"))==4&&LDHUD::StatPointsOf(View,TEXT("stat_brains"))==0);

    // D10 auto-open: once per match, for the first view of the opening offer (opportunity 1); later offers and points never.
    LDHUD::FGrowthAutoOpen AutoOpen;FLDProgressionView Growth;Growth.bValid=true;Growth.Generation=7;Growth.UnspentPoints=3;
    TestFalse(TEXT("Points without the opening offer open nothing"),AutoOpen.Observe(Growth,7));
    Growth.bHasActiveOffer=true;Growth.ActiveOffer.OfferId=FGuid::NewGuid();Growth.ActiveOffer.OpportunityOrdinal=1;
    TestTrue(TEXT("The first view with the opening offer opens the panel"),AutoOpen.Observe(Growth,7));
    TestFalse(TEXT("The same view does not open it again"),AutoOpen.Observe(Growth,7));
    Growth.bHasActiveOffer=false;Growth.UnspentPoints=6;
    TestFalse(TEXT("Newly earned points open nothing"),AutoOpen.Observe(Growth,7));
    Growth.bHasActiveOffer=true;Growth.ActiveOffer.OfferId=FGuid::NewGuid();Growth.ActiveOffer.OpportunityOrdinal=2;
    TestFalse(TEXT("A later offer opens nothing; it only lights the button"),AutoOpen.Observe(Growth,7));
    TestFalse(TEXT("A view of another match opens nothing"),AutoOpen.Observe(Growth,8));
    Growth.Generation=8;Growth.ActiveOffer.OpportunityOrdinal=1;Growth.ActiveOffer.OfferId=FGuid::NewGuid();
    TestTrue(TEXT("A new match's opening offer opens it again"),AutoOpen.Observe(Growth,8));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDComponentsTest,"Lockdown.Rules.HUDComponents",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDComponentsTest::RunTest(const FString& Parameters)
{
    // #198 B growth panel: the 启用 button of each owned component and its disabled reason, as growth would judge it (a hint
    // only; the server decides each command). A private fixture catalog keeps the rule apart from the approved content.
    FLDDemoContentDefinitions Content;
    // A workshop of five slots at level 1, one more every two levels (every install takes one).
    Content.Progression.WorkshopCapacityBase=5;Content.Progression.WorkshopCapacityLevelsPerSlot=2;
    const auto AddComponent=[&Content](const TCHAR* Id,ELDDemoComponentKind Kind,ELDDemoElement Element)->FLDDemoComponentDefinition&
    {
        FLDDemoComponentDefinition& Row=Content.Components.AddDefaulted_GetRef();
        Row.Id=Id;Row.NativeId=uint8(Content.Components.Num());Row.Kind=Kind;
        FLDDemoEffectDefinition& Effect=Row.Effects.AddDefaulted_GetRef();Effect.Element=Element;
        return Row;
    };
    AddComponent(TEXT("hud_fixture_fist"),ELDDemoComponentKind::Base,ELDDemoElement::None);
    AddComponent(TEXT("hud_fixture_wet"),ELDDemoComponentKind::Base,ELDDemoElement::Water);
    AddComponent(TEXT("hud_fixture_shock"),ELDDemoComponentKind::Base,ELDDemoElement::Electric);
    // The reference is used before the next row is added (adding may move the array).
    FLDDemoComponentDefinition& Trick=AddComponent(TEXT("hud_fixture_trick"),ELDDemoComponentKind::Base,ELDDemoElement::None);
    Trick.Binding=ELDDemoComponentBinding::ActiveElement;Trick.BindActives={TEXT("hud_fixture_bolt")};
    Trick.BindElements={ELDDemoElement::Fire,ELDDemoElement::Water,ELDDemoElement::Electric};
    AddComponent(TEXT("hud_fixture_mod"),ELDDemoComponentKind::Mod,ELDDemoElement::None);
    const auto Owned=[](const TCHAR* Id,bool bInstalled=false,bool bEver=false)
    {FLDComponentInstanceView View;View.DefinitionId=Id;View.Source=ELDComponentSource::LevelUp;View.SourceLevel=2;View.bInstalled=bInstalled;View.bEverInstalled=bEver||bInstalled;return View;};
    FLDProgressionView View;View.bValid=true;View.Generation=9;View.Level=1;
    using EColumn=LDHUD::EStorageColumn;
    {
        // #222: installs, moves and uninstalls follow the learn gate: open on the field, in combat, in a PK and while dead.
        using EStage=LDProgression::EMatchStage;
        TestTrue(TEXT("Components install on the field and in every rest step, alive or dead"),
            LDHUD::BuildGate(EStage::Live,ELDLifePhase::Alive).bCanInstall&&LDHUD::BuildGate(EStage::Live,ELDLifePhase::Dead).bCanInstall
            &&LDHUD::BuildGate(EStage::Rest,ELDLifePhase::Alive).bCanInstall&&LDHUD::BuildGate(EStage::RestLocked,ELDLifePhase::Dead).bCanInstall);
        TestFalse(TEXT("Results install nothing"),LDHUD::BuildGate(EStage::Closed,ELDLifePhase::Alive).bCanInstall);
    }
    // #227 storage drops: a plain base goes on 本体 only; B18 on the column of each bindable learned active, with its elements.
    View.Components={Owned(TEXT("hud_fixture_fist")),Owned(TEXT("hud_fixture_wet")),Owned(TEXT("hud_fixture_trick")),Owned(TEXT("hud_fixture_mod"))};
    TArray<LDHUD::FComponentDrop> Drops=LDHUD::ComponentDrops(TEXT("hud_fixture_fist"),View,&Content);
    TestTrue(TEXT("A plain base drops on 本体 only"),Drops.Num()==1&&Drops[0].Column==EColumn::Body&&Drops[0].Kind==ELDComponentTargetKind::None
        &&Drops[0].Host.IsNone()&&Drops[0].Elements.IsEmpty()&&Drops[0].Bases.IsEmpty());
    TestTrue(TEXT("A water base drops on 本体 like any base"),LDHUD::ComponentDrops(TEXT("hud_fixture_wet"),View,&Content).Num()==1);
    TestTrue(TEXT("B18 without a bindable learned active has nowhere to go"),LDHUD::ComponentDrops(TEXT("hud_fixture_trick"),View,&Content).IsEmpty());
    FLDActiveSlotView& Bolt=View.Actives.AddDefaulted_GetRef();Bolt.DefinitionId=TEXT("hud_fixture_bolt");Bolt.Slot=1;
    FLDActiveSlotView& Other=View.Actives.AddDefaulted_GetRef();Other.DefinitionId=TEXT("hud_fixture_other");Other.Slot=0;
    Drops=LDHUD::ComponentDrops(TEXT("hud_fixture_trick"),View,&Content);
    TestTrue(TEXT("B18 drops on the W column of its bindable active and asks for one of its elements"),Drops.Num()==1&&Drops[0].Column==EColumn::SlotW
        &&Drops[0].BindActive==FName(TEXT("hud_fixture_bolt"))&&Drops[0].Kind==ELDComponentTargetKind::None&&Drops[0].Elements.Num()==3);
    TestTrue(TEXT("Only the learned actives its binding lists are offered"),LDHUD::BindableActives(Content.Components[3],View)==TArray<FName>{FName(TEXT("hud_fixture_bolt"))});
    TestTrue(TEXT("Over Q nothing takes B18; over W its drop"),LDHUD::FindDrop(Drops,EColumn::SlotQ,NAME_None)==nullptr
        &&LDHUD::FindDrop(Drops,EColumn::SlotW,NAME_None)==&Drops[0]&&LDHUD::FindDrop(Drops,EColumn::SlotW,TEXT("hud_fixture_fist"))==&Drops[0]);
    TestTrue(TEXT("A mod without any place has no drop"),LDHUD::ComponentDrops(TEXT("hud_fixture_mod"),View,&Content).IsEmpty());
    TestTrue(TEXT("An unknown or unowned component has no drop"),LDHUD::ComponentDrops(TEXT("hud_fixture_unknown"),View,&Content).IsEmpty()
        &&LDHUD::ComponentDrops(TEXT("hud_fixture_shock"),View,&Content).IsEmpty());
    {
        // The storage view: the stored ones in the view's order with the never-installed count; each installed one in its column.
        FLDProgressionView Built=View;
        Built.Components[0].bInstalled=Built.Components[0].bEverInstalled=true;
        FLDComponentInstanceView& Bound=Built.Components[2];Bound.bInstalled=Bound.bEverInstalled=true;
        Bound.Target.BoundActive=TEXT("hud_fixture_bolt");Bound.Target.Element=ELDDemoElement::Water;
        Built.Components[3].bEverInstalled=true;
        const LDHUD::FStorageView Storage=LDHUD::ComponentStorage(Built,&Content);
        TestTrue(TEXT("Stored components in order; one was installed before, one never"),
            Storage.Stored==TArray<FName>({FName(TEXT("hud_fixture_wet")),FName(TEXT("hud_fixture_mod"))})&&Storage.Fresh==1);
        const TSet<FName> Seen{FName(TEXT("hud_fixture_wet"))};
        TestEqual(TEXT("A previewed component no longer contributes to the fresh count"),
            LDHUD::ComponentStorage(Built,&Content,&Seen).Fresh,0);
        TestTrue(TEXT("Five columns: 本体, Q, W, E and D, each slot with its learned active"),Storage.Columns.Num()==LDHUD::StorageColumnCount
            &&Storage.Columns[0].Title==LDTerms::ColumnBody&&Storage.Columns[4].Title==LDTerms::Dash
            &&Storage.Columns[2].Title.IsEmpty()&&Storage.Columns[3].Title==LDTerms::ColumnEmptySlot
            &&Storage.Columns[1].Active==FName(TEXT("hud_fixture_other"))&&Storage.Columns[2].Active==FName(TEXT("hud_fixture_bolt"))
            &&Storage.Columns[3].Active.IsNone());
        TestTrue(TEXT("The base sits on 本体 and B18 on W with its element"),Storage.Columns[0].Entries.Num()==1
            &&Storage.Columns[0].Entries[0].Component==FName(TEXT("hud_fixture_fist"))&&Storage.Columns[2].Entries.Num()==1
            &&Storage.Columns[2].Entries[0].Note==LDTerms::ElementWater&&Storage.Columns[1].Entries.IsEmpty());
        const TArray<LDHUD::FComponentDrop> Moves=LDHUD::ComponentDrops(TEXT("hud_fixture_trick"),Built,&Content);
        TestTrue(TEXT("An installed B18 still drops (a move) on its bindable column"),Moves.Num()==1&&Moves[0].Column==EColumn::SlotW);
        TestTrue(TEXT("Nothing comes off with a component nothing sits on, or with a stored one"),LDHUD::DetachedWith(TEXT("hud_fixture_trick"),Built).IsEmpty()
            &&LDHUD::DetachedWith(TEXT("hud_fixture_wet"),Built).IsEmpty());
        TestTrue(TEXT("An invalid view shows the columns and nothing in them"),LDHUD::ComponentStorage(FLDProgressionView(),&Content).Stored.IsEmpty()
            &&LDHUD::ComponentStorage(FLDProgressionView(),&Content).Columns.Num()==LDHUD::StorageColumnCount);
        TestTrue(TEXT("The storage counts the installs against the level's workshop capacity"),Storage.Installed==2&&Storage.Capacity==5);
        // Every slot taken: a stored component has no drop and its hover says why; an installed one still moves.
        FLDProgressionView Full=Built;
        for(const TCHAR* Id:{TEXT("hud_fixture_full_a"),TEXT("hud_fixture_full_b"),TEXT("hud_fixture_full_c")}){Full.Components.Add(Owned(Id,true));}
        TestTrue(TEXT("A full workshop leaves a stored base nowhere to go"),LDHUD::WorkshopFull(Full,&Content)
            &&LDHUD::ComponentDrops(TEXT("hud_fixture_wet"),Full,&Content).IsEmpty()
            &&LDHUD::StoredPlaceText(TEXT("hud_fixture_wet"),Full,&Content)==LDTerms::WorkshopFullHint(5));
        TestTrue(TEXT("A full workshop does not promise a replacement when the card has no valid host"),
            LDHUD::ReplaceDrops(TEXT("hud_fixture_mod"),Full,&Content).IsEmpty()
            &&LDHUD::StoredPlaceText(TEXT("hud_fixture_mod"),Full,&Content)==LDTerms::WorkshopFullNoPlace(5));
        TestTrue(TEXT("An installed component still moves in a full workshop"),LDHUD::ComponentDrops(TEXT("hud_fixture_trick"),Full,&Content).Num()==1);
        Full.Level=3;
        TestTrue(TEXT("Level 3 adds a slot"),!LDHUD::WorkshopFull(Full,&Content)&&LDHUD::ComponentDrops(TEXT("hud_fixture_wet"),Full,&Content).Num()==1);
        Full.Level=1;
        // A full workshop offers a replace on each installed tile whose removal makes room in that column; never on the move of
        // an installed component, and never where the plain drop already lands.
        const TArray<LDHUD::FComponentDrop> Replaces=LDHUD::ReplaceDrops(TEXT("hud_fixture_wet"),Full,&Content);
        const LDHUD::FComponentDrop* OverFist=Replaces.FindByPredicate([](const LDHUD::FComponentDrop& D){return D.Host==FName(TEXT("hud_fixture_fist"));});
        TestTrue(TEXT("Over the installed base a full workshop replaces it on 本体"),OverFist&&OverFist->Column==EColumn::Body
            &&OverFist->Replacing==FName(TEXT("hud_fixture_fist"))&&OverFist->Lost==TArray<FName>{FName(TEXT("hud_fixture_fist"))}
            &&OverFist->Kind==ELDComponentTargetKind::None);
        TestFalse(TEXT("No replace lands a base on B18's W column"),Replaces.ContainsByPredicate([](const LDHUD::FComponentDrop& D){return D.Column==EColumn::SlotW;}));
        TestTrue(TEXT("An installed component has no replace drops; a free slot needs none on its column"),
            LDHUD::ReplaceDrops(TEXT("hud_fixture_trick"),Full,&Content).IsEmpty()&&LDHUD::ReplaceDrops(TEXT("hud_fixture_wet"),Built,&Content).IsEmpty());
        TestEqualSensitive(TEXT("A plain drop previews its column"),LDHUD::DropPreviewText(TEXT("hud_fixture_wet"),LDHUD::ComponentDrops(TEXT("hud_fixture_wet"),Built,&Content)[0],Built,&Content),
            LDTerms::DropInstallsOn(LDTerms::ColumnBody));
        // A form: two bases of one form never share 本体, so the stored one has no plain drop and replaces the installed one.
        AddExpectedMessage(TEXT("LD_HUD_DISPLAY_TEXT_MISSING hud_fixture_fist"),ELogVerbosity::Error,EAutomationExpectedMessageFlags::Contains,0,false);
        FLDDemoContentDefinitions Formed=Content;Formed.Components[0].Form=TEXT("fist");Formed.Components[1].Form=TEXT("fist");
        TestTrue(TEXT("An installed form-mate takes the base's only place"),LDHUD::ComponentDrops(TEXT("hud_fixture_wet"),Built,&Formed).IsEmpty()
            &&LDHUD::FormMateAt(TEXT("hud_fixture_wet"),ELDComponentTargetKind::None,NAME_None,Built,&Formed)==FName(TEXT("hud_fixture_fist")));
        const TArray<LDHUD::FComponentDrop> Swaps=LDHUD::ReplaceDrops(TEXT("hud_fixture_wet"),Built,&Formed);
        TestTrue(TEXT("It replaces its form-mate without a full workshop"),Swaps.Num()==1&&Swaps[0].Replacing==FName(TEXT("hud_fixture_fist"))&&Swaps[0].Column==EColumn::Body);
        TestTrue(TEXT("The stored card explains the available form replacement"),
            LDHUD::StoredPlaceText(TEXT("hud_fixture_wet"),Built,&Formed)==FString(LDTerms::ReplaceAvailable));
        TestTrue(TEXT("The popup names the owned form-mate"),LDHUD::ConnectionsText(TEXT("hud_fixture_wet"),Built,&Formed).Contains(TEXT("同形态")));
        TestTrue(TEXT("Without a form, nothing is named"),LDHUD::ConnectionsText(TEXT("hud_fixture_wet"),Built,&Content).IsEmpty());
    }
    {
        // The popup's category line: the kind, then the use tags in their authored order.
        AddExpectedMessage(TEXT("LD_HUD_DISPLAY_TEXT_MISSING hud_fixture_mod"),ELogVerbosity::Error,EAutomationExpectedMessageFlags::Contains,0,false);
        FLDDemoContentDefinitions Tagged=Content;
        Tagged.Components[4].Tags={ELDDemoComponentTag::Projectile,ELDDemoComponentTag::Spell};
        FLDDemoDisplayDefinition& Shown=Tagged.Display.AddDefaulted_GetRef();Shown.Id=TEXT("hud_fixture_mod");Shown.DescriptionText=TEXT("改一下");
        const LDHUD::FComponentCardView Card=LDHUD::ComponentCard(TEXT("hud_fixture_mod"),&Tagged,LDHUD::FCardStats());
        TestEqualSensitive(TEXT("A mod's kind and tags"),Card.KindLine,FString(TEXT("技能改装 · 弹道 · 法术")));
        TestEqualSensitive(TEXT("The choice popup summary is that line"),LDHUD::ComponentChoiceDetail(Card,FString()).Summary,Card.KindLine);
    }
    TestTrue(TEXT("Element names"),LDHUD::ElementName(ELDDemoElement::Fire)==LDTerms::ElementFire&&LDHUD::ElementName(ELDDemoElement::Water)==LDTerms::ElementWater
        &&LDHUD::ElementName(ELDDemoElement::Electric)==LDTerms::ElementElectric);

    // Every #198 B reason names itself; none falls through to the unknown line.
    for(const ELDGrowthReason Reason:{ELDGrowthReason::NotOwned,ELDGrowthReason::AlreadyInstalled,ELDGrowthReason::NotInstalled,
        ELDGrowthReason::InvalidTarget,ELDGrowthReason::OpeningBaseUnavailable,ELDGrowthReason::OpeningBaseTaken,
        ELDGrowthReason::OpeningActiveFirst})
    {
        const FString Text=LDHUD::GrowthReasonText(Reason);
        TestTrue(FString::Printf(TEXT("Reason %d has its own text"),int32(Reason)),!Text.IsEmpty()&&!Text.StartsWith(TEXT("未知原因")));
    }

    // Receipt lines: an opening pick enabled or kept with the reason, an install, an uninstall and a refusal.
    FLDBuildReceipt Receipt;Receipt.Header.Outcome=ELDReceiptOutcome::Applied;Receipt.Kind=ELDBuildCommandKind::OpeningBase;
    Receipt.ComponentId=TEXT("hud_fixture_fist");Receipt.bComponentInstalled=true;
    TestEqualSensitive(TEXT("An opening pick enabled at once"),LDHUD::BuildReceiptText(Receipt,nullptr),LDTerms::ComponentInstalled(TEXT("hud_fixture_fist")));
    Receipt.bComponentInstalled=false;Receipt.InstallStoredReason=uint16(ELDGrowthReason::InvalidTarget);
    TestEqualSensitive(TEXT("An opening pick kept in the storage names why"),LDHUD::BuildReceiptText(Receipt,nullptr),
        LDTerms::ComponentStored(TEXT("hud_fixture_fist"),LDHUD::BuildBlockedText(ELDGrowthReason::InvalidTarget)));
    Receipt.Kind=ELDBuildCommandKind::InstallComponent;Receipt.bComponentInstalled=true;Receipt.InstallStoredReason=0;
    TestEqualSensitive(TEXT("An install names the component"),LDHUD::BuildReceiptText(Receipt,nullptr),LDTerms::ComponentInstalled(TEXT("hud_fixture_fist")));
    Receipt.bComponentInstalled=false;
    TestEqualSensitive(TEXT("An applied install that installed nothing never claims success"),LDHUD::BuildReceiptText(Receipt,nullptr),
        FString(TEXT("回执缺少结果，请以当前状态为准。")));
    Receipt.Kind=ELDBuildCommandKind::UninstallComponent;
    TestEqualSensitive(TEXT("An uninstall names the component"),LDHUD::BuildReceiptText(Receipt,nullptr),LDTerms::ComponentRemoved(TEXT("hud_fixture_fist")));
    Receipt.Header.Outcome=ELDReceiptOutcome::Rejected;Receipt.Header.Reason=uint16(ELDGrowthReason::OpeningActiveFirst);
    TestEqualSensitive(TEXT("A refused component command names its reason"),LDHUD::BuildReceiptText(Receipt,nullptr),
        TEXT("没有生效：")+LDHUD::GrowthReasonText(ELDGrowthReason::OpeningActiveFirst));

    // The 构筑 button counts the opening pick; #227: owned components wait in the storage stack instead.
    FLDProgressionView Pending;Pending.bValid=true;Pending.Generation=9;
    Pending.bOpeningBasePending=true;Pending.OpeningBaseOptions={FName(TEXT("hud_fixture_fist"))};
    Pending.Components={Owned(TEXT("hud_fixture_mod")),Owned(TEXT("hud_fixture_wet"))};
    const LDHUD::FGrowthPending Counted=LDHUD::GrowthPending(&Pending,9,&Content);
    TestTrue(TEXT("Only the opening pick lights the button"),Counted.Any()&&Counted.bOpeningBase);
    Pending.bOpeningBasePending=false;Pending.OpeningBaseOptions.Reset();
    TestFalse(TEXT("Stored components never light the button"),LDHUD::GrowthPending(&Pending,9,&Content).Any());

    // Auto-open: the opening base pick once; a new component from a level-up only lights the button (no fight cover).
    LDHUD::FGrowthAutoOpen AutoOpen;FLDProgressionView Growth;Growth.bValid=true;Growth.Generation=11;
    TestFalse(TEXT("An empty first view opens nothing"),AutoOpen.Observe(Growth,11));
    Growth.bOpeningBasePending=true;Growth.OpeningBaseOptions={FName(TEXT("hud_fixture_fist"))};
    TestTrue(TEXT("The opening base pick opens the panel"),AutoOpen.Observe(Growth,11));
    TestFalse(TEXT("The same pick does not open it again"),AutoOpen.Observe(Growth,11));
    Growth.bOpeningBasePending=false;Growth.OpeningBaseOptions.Reset();Growth.Components={Owned(TEXT("hud_fixture_fist"),true)};
    TestFalse(TEXT("A pick enabled at once opens nothing"),AutoOpen.Observe(Growth,11));
    Growth.Components.Add(Owned(TEXT("hud_fixture_trick")));
    TestFalse(TEXT("A new component from a level-up opens nothing"),AutoOpen.Observe(Growth,11));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDWorkshopTargetsTest,"Lockdown.Rules.HUDWorkshopTargets",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDWorkshopTargetsTest::RunTest(const FString& Parameters)
{
    // #198 F growth panel: the places an owned mod or connector may go now (learned actives, installed components, D, self,
    // in catalog list order), C12's repeatable bases, what an installed one sits on, and the conflict reason's text.
    // The private fixture ids have no registered display text; their labels fall back to the stable ids (logged).
    AddExpectedError(TEXT("LD_HUD_DISPLAY_TEXT_MISSING"),EAutomationExpectedErrorFlags::Contains,0);
    FLDDemoContentDefinitions Content;
    // A workshop of five slots at level 1, one more every two levels (every install takes one).
    Content.Progression.WorkshopCapacityBase=5;Content.Progression.WorkshopCapacityLevelsPerSlot=2;
    const auto AddComponent=[&Content](const TCHAR* Id,ELDDemoComponentKind Kind,ELDDemoElement Element)->FLDDemoComponentDefinition&
    {
        FLDDemoComponentDefinition& Row=Content.Components.AddDefaulted_GetRef();
        Row.Id=Id;Row.NativeId=uint8(Content.Components.Num());Row.Kind=Kind;
        FLDDemoEffectDefinition& Effect=Row.Effects.AddDefaulted_GetRef();Effect.Element=Element;
        return Row;
    };
    AddComponent(TEXT("hud_fixture_fist"),ELDDemoComponentKind::Base,ELDDemoElement::None);
    AddComponent(TEXT("hud_fixture_wet"),ELDDemoComponentKind::Base,ELDDemoElement::Water);
    AddComponent(TEXT("hud_fixture_ledger"),ELDDemoComponentKind::Base,ELDDemoElement::None);
    // Each reference is used before the next row is added (adding may move the array).
    {
        FLDDemoComponentDefinition& Reach=AddComponent(TEXT("hud_fixture_reach"),ELDDemoComponentKind::Mod,ELDDemoElement::None);
        Reach.TargetActives={TEXT("hud_fixture_kick"),TEXT("hud_fixture_bolt")};Reach.TargetComponents={TEXT("hud_fixture_wet"),TEXT("hud_fixture_fist")};
        Reach.bTargetDash=true;
    }
    AddComponent(TEXT("hud_fixture_owe"),ELDDemoComponentKind::Mod,ELDDemoElement::None).bTargetSelf=true;
    {
        FLDDemoComponentDefinition& Repeat=AddComponent(TEXT("hud_fixture_repeat"),ELDDemoComponentKind::Connector,ELDDemoElement::None);
        Repeat.TargetActives={TEXT("hud_fixture_bolt")};Repeat.ExtraBases={TEXT("hud_fixture_wet"),TEXT("hud_fixture_ledger")};
    }
    {
        FLDDemoComponentDefinition& Reaction=AddComponent(TEXT("hud_fixture_reaction"),ELDDemoComponentKind::Connector,ELDDemoElement::None);
        Reaction.TargetActives={TEXT("hud_fixture_bolt")};Reaction.Effects[0].Opcode=uint8(ELDDemoEffectOp::ReactionEmpower);
    }
    const auto Owned=[](const TCHAR* Id,bool bInstalled)
    {FLDComponentInstanceView View;View.DefinitionId=Id;View.Source=ELDComponentSource::LevelUp;View.SourceLevel=2;View.bInstalled=View.bEverInstalled=bInstalled;return View;};
    FLDProgressionView View;View.bValid=true;View.Generation=9;View.Level=1;
    const FLDDemoComponentDefinition& Reach=Content.Components[3];
    const FLDDemoComponentDefinition& Repeat=Content.Components[5];
    const auto Places=[&](const FLDDemoComponentDefinition& Component)
    {
        TArray<FString> Out;
        for(const LDHUD::FWorkshopTargetView& Target:LDHUD::WorkshopTargets(Component,View,&Content))
        {Out.Add(FString::Printf(TEXT("%d:%s:%s"),int32(Target.Kind),*Target.TargetId.ToString(),*Target.Label));}
        return FString::Join(Out,TEXT("|"));
    };
    const auto Place=[&](ELDComponentTargetKind Kind,const TCHAR* Id,const FString& Label)
    {return FString::Printf(TEXT("%d:%s:%s"),int32(Kind),Id,*Label);};
    const FString Dash=Place(ELDComponentTargetKind::Dash,TEXT("None"),LDTerms::TargetDash);
    TestEqualSensitive(TEXT("Without a learned active or installed host only D is offered"),Places(Reach),Dash);
    View.Components={Owned(TEXT("hud_fixture_fist"),true),Owned(TEXT("hud_fixture_wet"),false),Owned(TEXT("hud_fixture_reach"),false)};
    FLDActiveSlotView& Bolt=View.Actives.AddDefaulted_GetRef();Bolt.DefinitionId=TEXT("hud_fixture_bolt");Bolt.Slot=2;
    TestEqualSensitive(TEXT("Learned actives, installed hosts, then D, in catalog list order"),Places(Reach),
        Place(ELDComponentTargetKind::Active,TEXT("hud_fixture_bolt"),LDHUD::ContentName(&Content,TEXT("hud_fixture_bolt")))+TEXT("|")
        +Place(ELDComponentTargetKind::Component,TEXT("hud_fixture_fist"),LDHUD::ContentName(&Content,TEXT("hud_fixture_fist")))+TEXT("|")+Dash);
    TestEqualSensitive(TEXT("A self-installed mod goes on the holder"),Places(Content.Components[4]),
        Place(ELDComponentTargetKind::None,TEXT("None"),LDTerms::TargetSelf));
    {
        // #227: each place is a drop: the active's column (E), the host in its column (本体) and D.
        const TArray<LDHUD::FComponentDrop> Drops=LDHUD::ComponentDrops(TEXT("hud_fixture_reach"),View,&Content);
        TestTrue(TEXT("A mod drops on each of its places"),Drops.Num()==3&&Drops[0].Column==LDHUD::EStorageColumn::SlotE
            &&Drops[0].Kind==ELDComponentTargetKind::Active&&Drops[0].TargetId==FName(TEXT("hud_fixture_bolt"))
            &&Drops[1].Column==LDHUD::EStorageColumn::Body&&Drops[1].Host==FName(TEXT("hud_fixture_fist"))
            &&Drops[1].Kind==ELDComponentTargetKind::Component&&Drops[2].Column==LDHUD::EStorageColumn::Dash);
        TestTrue(TEXT("Over its host the host's drop; over 本体 alone nothing (the mod is not a self mod)"),
            LDHUD::FindDrop(Drops,LDHUD::EStorageColumn::Body,TEXT("hud_fixture_fist"))==&Drops[1]
            &&LDHUD::FindDrop(Drops,LDHUD::EStorageColumn::Body,NAME_None)==nullptr);
        TestEqualSensitive(TEXT("The hover line names each reachable column once, in column order"),LDHUD::DropColumnsText(Drops),
            LDTerms::CanGoOn(FString(LDTerms::ColumnBody)+TEXT("、E、D")));
        TestEqualSensitive(TEXT("No drop says there is no place now"),LDHUD::DropColumnsText({}),FString(LDTerms::NoPlaceNow));
    }

    // C12: a repeatable base must be installed (water and electric included since #198 A).
    View.Components.Add(Owned(TEXT("hud_fixture_repeat"),false));
    TestTrue(TEXT("C12 without an installed base has no drop"),LDHUD::ComponentDrops(TEXT("hud_fixture_repeat"),View,&Content).IsEmpty());
    View.Components[1].bInstalled=View.Components[1].bEverInstalled=true;
    TestTrue(TEXT("An installed water base is repeatable"),LDHUD::RepeatableBases(Repeat,View,&Content)==TArray<FName>{FName(TEXT("hud_fixture_wet"))});
    View.Components.Add(Owned(TEXT("hud_fixture_ledger"),true));
    TestTrue(TEXT("Installed bases are offered in list order"),LDHUD::RepeatableBases(Repeat,View,&Content)
        ==TArray<FName>({FName(TEXT("hud_fixture_wet")),FName(TEXT("hud_fixture_ledger"))}));
    {
        const TArray<LDHUD::FComponentDrop> Drops=LDHUD::ComponentDrops(TEXT("hud_fixture_repeat"),View,&Content);
        TestTrue(TEXT("Then C12 drops on its active and asks for one of the bases"),Drops.Num()==1&&Drops[0].Column==LDHUD::EStorageColumn::SlotE
            &&Drops[0].Bases==TArray<FName>({FName(TEXT("hud_fixture_wet")),FName(TEXT("hud_fixture_ledger"))}));
    }
    View.Components.Add(Owned(TEXT("hud_fixture_reaction"),false));
    TestFalse(TEXT("C09 with a place drops there"),LDHUD::ComponentDrops(TEXT("hud_fixture_reaction"),View,&Content).IsEmpty());
    {
        // A mod on a host nests under it in the host's column; storing or moving the host takes the mod along.
        FLDProgressionView Nest=View;
        FLDComponentInstanceView& OnFist=*Nest.Components.FindByPredicate([](const FLDComponentInstanceView& C){return C.DefinitionId==FName(TEXT("hud_fixture_reach"));});
        OnFist.bInstalled=OnFist.bEverInstalled=true;OnFist.Target.Kind=ELDComponentTargetKind::Component;OnFist.Target.Definition=TEXT("hud_fixture_fist");
        const LDHUD::FStorageView Shown=LDHUD::ComponentStorage(Nest,&Content);
        const TArray<LDHUD::FEquippedView>& Body=Shown.Columns[0].Entries;
        TestTrue(TEXT("The mod follows its host, one level deeper"),Body.Num()==4&&Body[0].Component==FName(TEXT("hud_fixture_fist"))
            &&Body[1].Component==FName(TEXT("hud_fixture_reach"))&&Body[1].Host==FName(TEXT("hud_fixture_fist"))&&Body[1].Depth==1&&Body[2].Depth==0);
        TestTrue(TEXT("Taking the host off takes the mod with it"),LDHUD::DetachedWith(TEXT("hud_fixture_fist"),Nest)==TArray<FName>{FName(TEXT("hud_fixture_reach"))});
        const TArray<LDHUD::FComponentDrop> Moves=LDHUD::ComponentDrops(TEXT("hud_fixture_reach"),Nest,&Content);
        TestTrue(TEXT("The installed mod may move to its other places"),Moves.Num()==4&&LDHUD::FindDrop(Moves,LDHUD::EStorageColumn::Body,TEXT("hud_fixture_wet"))!=nullptr);
        const TArray<LDHUD::FComponentDrop> HostMoves=LDHUD::ComponentDrops(TEXT("hud_fixture_wet"),Nest,&Content);
        TestTrue(TEXT("A base never drops on itself"),HostMoves.Num()==1&&HostMoves[0].Host.IsNone());
    }

    // Installed rows name their place.
    FLDComponentInstanceView Shown=Owned(TEXT("hud_fixture_reach"),true);
    Shown.Target.Kind=ELDComponentTargetKind::Component;Shown.Target.Definition=TEXT("hud_fixture_fist");
    TestEqualSensitive(TEXT("A mod names its host"),LDHUD::InstalledTargetText(Shown,&Content),LDTerms::InstalledOn(LDHUD::ContentName(&Content,TEXT("hud_fixture_fist"))));
    Shown.Target.Kind=ELDComponentTargetKind::Dash;Shown.Target.Definition=NAME_None;
    TestEqualSensitive(TEXT("A mod on D names D"),LDHUD::InstalledTargetText(Shown,&Content),LDTerms::InstalledOn(LDTerms::TargetDash));
    Shown=Owned(TEXT("hud_fixture_owe"),true);
    TestEqualSensitive(TEXT("A self mod names the holder"),LDHUD::InstalledTargetText(Shown,&Content),LDTerms::InstalledOn(LDTerms::TargetSelf));
    Shown=Owned(TEXT("hud_fixture_repeat"),true);
    Shown.Target.Kind=ELDComponentTargetKind::Active;Shown.Target.Definition=TEXT("hud_fixture_bolt");Shown.Target.BoundComponent=TEXT("hud_fixture_ledger");
    TestEqualSensitive(TEXT("C12 names its place and its repeated base"),LDHUD::InstalledTargetText(Shown,&Content),
        LDTerms::InstalledRepeating(LDHUD::ContentName(&Content,TEXT("hud_fixture_bolt")),LDHUD::ContentName(&Content,TEXT("hud_fixture_ledger"))));
    TestTrue(TEXT("A plain base or an inventory item names nothing"),LDHUD::InstalledTargetText(Owned(TEXT("hud_fixture_fist"),true),&Content).IsEmpty()
        &&LDHUD::InstalledTargetText(Owned(TEXT("hud_fixture_reach"),false),&Content).IsEmpty());

    // The refusal of M08 and M09 on one place reads as itself.
    TestEqualSensitive(TEXT("TargetConflict has its own text"),LDHUD::GrowthReasonText(ELDGrowthReason::TargetConflict),FString(LDTerms::TargetConflict));
    FLDBuildReceipt Receipt;Receipt.Kind=ELDBuildCommandKind::InstallComponent;
    Receipt.Header.Outcome=ELDReceiptOutcome::Rejected;Receipt.Header.Reason=uint16(ELDGrowthReason::TargetConflict);
    TestEqualSensitive(TEXT("A refused install names the conflict"),LDHUD::BuildReceiptText(Receipt,nullptr),
        TEXT("没有生效：")+FString(LDTerms::TargetConflict));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDFieldRewardNoticesTest,"Lockdown.Rules.HUDFieldRewardNotices",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDFieldRewardNoticesTest::RunTest(const FString& Parameters)
{
    // #198 G: each field reward names its event, XP and what its build part gave; the watch tells each new entry once.
    const auto Reward=[](const TCHAR* Event,uint64 Serial,int64 XP,ELDFieldRewardResult Result,const TCHAR* Definition)
    {FLDFieldRewardView View;View.Event=Event;View.Serial=Serial;View.XP=XP;View.Result=Result;View.Definition=Definition;return View;};
    const FString XP=FString::Printf(TEXT("+200 %s"),LDTerms::Experience);
    TestEqualSensitive(TEXT("An airdrop blueprint names the event, the XP and the blueprint"),
        LDHUD::FieldRewardNotice(Reward(TEXT("airdrop"),1,200,ELDFieldRewardResult::Blueprint,TEXT("hud_fixture_bolt")),nullptr),
        FString(LDTerms::Airdrop)+TEXT("：")+XP+TEXT("，")+LDTerms::FieldRewardBlueprint(TEXT("hud_fixture_bolt")));
    TestEqualSensitive(TEXT("A Boss scarce component without XP names only the component"),
        LDHUD::FieldRewardNotice(Reward(TEXT("boss"),1,0,ELDFieldRewardResult::ScarceComponent,TEXT("hud_fixture_rare")),nullptr),
        FString(LDTerms::Boss)+TEXT("：")+LDTerms::FieldRewardScarce(TEXT("hud_fixture_rare")));
    TestEqualSensitive(TEXT("The common fallback names a plain component"),
        LDHUD::FieldRewardNotice(Reward(TEXT("airdrop"),2,0,ELDFieldRewardResult::CommonComponent,TEXT("hud_fixture_fist")),nullptr),
        FString(LDTerms::Airdrop)+TEXT("：")+LDTerms::FieldRewardCommon(TEXT("hud_fixture_fist")));
    TestEqualSensitive(TEXT("A high camp marker blueprint"),
        LDHUD::FieldRewardNotice(Reward(TEXT("camp_marker_high"),3,0,ELDFieldRewardResult::Blueprint,TEXT("hud_fixture_bolt")),nullptr),
        FString(LDTerms::FieldRewardHighCamp)+TEXT("：")+LDTerms::FieldRewardBlueprint(TEXT("hud_fixture_bolt")));
    TestEqualSensitive(TEXT("An elite camp marker readies its two-choice"),
        LDHUD::FieldRewardNotice(Reward(TEXT("camp_marker_elite"),4,0,ELDFieldRewardResult::ComponentChoice,TEXT("")),nullptr),
        FString(LDTerms::FieldRewardEliteCamp)+TEXT("：")+LDTerms::FieldRewardChoiceReady());
    TestEqualSensitive(TEXT("An empty active pool keeps the blueprint for later"),
        LDHUD::FieldRewardNotice(Reward(TEXT("camp_marker_high"),5,0,ELDFieldRewardResult::BlueprintHeld,TEXT("")),nullptr),
        FString(LDTerms::FieldRewardHighCamp)+TEXT("：")+LDTerms::FieldRewardBlueprintHeld());
    {
        FLDDemoContentDefinitions Content;Content.Parameters.EmptyComponentPoolPoints=2;
        TestEqualSensitive(TEXT("An empty material pool names the approved points"),
            LDHUD::FieldRewardNotice(Reward(TEXT("boss"),6,0,ELDFieldRewardResult::PoolPoints,TEXT("")),&Content),
            FString(LDTerms::Boss)+TEXT("：")+LDTerms::FieldRewardPoolPoints(2));
    }
    TestEqualSensitive(TEXT("XP alone is still told"),LDHUD::FieldRewardNotice(Reward(TEXT("airdrop"),7,200,ELDFieldRewardResult::None,TEXT("")),nullptr),
        FString(LDTerms::Airdrop)+TEXT("：")+XP);
    TestTrue(TEXT("A blueprint result without its definition never claims a name"),
        !LDHUD::FieldRewardNotice(Reward(TEXT("airdrop"),8,0,ELDFieldRewardResult::Blueprint,TEXT("")),nullptr).Contains(TEXT("「")));
    TestTrue(TEXT("An unknown event is named as unknown, never as another event"),LDHUD::FieldRewardEventName(TEXT("hud_fixture_event")).StartsWith(TEXT("未知事件")));
    TestTrue(TEXT("Nothing to tell is empty"),LDHUD::FieldRewardNotice(Reward(TEXT("airdrop"),9,0,ELDFieldRewardResult::None,TEXT("")),nullptr).IsEmpty());

    // New entries since a known count, oldest first.
    FLDProgressionView View;View.bValid=true;View.Generation=3;
    View.FieldRewards={Reward(TEXT("airdrop"),1,200,ELDFieldRewardResult::Blueprint,TEXT("a")),Reward(TEXT("boss"),1,0,ELDFieldRewardResult::ScarceComponent,TEXT("b"))};
    TArray<FLDFieldRewardView> Fresh=LDHUD::NewFieldRewards(View,1);
    TestTrue(TEXT("Entries past the known count"),Fresh.Num()==1&&Fresh[0].Event==FName(TEXT("boss")));
    TestTrue(TEXT("Nothing past the end"),LDHUD::NewFieldRewards(View,2).IsEmpty()&&LDHUD::NewFieldRewards(View,5).IsEmpty());
    TestEqual(TEXT("A negative count reads from the start"),LDHUD::NewFieldRewards(View,-1).Num(),2);

    // The watch: the first view of a match primes, later entries are told once, another match's view tells nothing.
    LDHUD::FFieldRewardWatch Watch;
    TestTrue(TEXT("The first view primes and replays nothing"),Watch.Observe(&View,3).IsEmpty());
    View.FieldRewards.Add(Reward(TEXT("camp_marker_elite"),2,0,ELDFieldRewardResult::ComponentChoice,TEXT("")));
    Fresh=Watch.Observe(&View,3);
    TestTrue(TEXT("A new entry is told"),Fresh.Num()==1&&Fresh[0].Event==FName(TEXT("camp_marker_elite"))&&Fresh[0].Serial==2);
    TestTrue(TEXT("The same view tells it only once"),Watch.Observe(&View,3).IsEmpty());
    View.FieldRewards.Add(Reward(TEXT("airdrop"),3,200,ELDFieldRewardResult::Blueprint,TEXT("c")));
    TestTrue(TEXT("A view of another match tells nothing"),Watch.Observe(&View,4).IsEmpty()&&Watch.Observe(nullptr,3).IsEmpty());
    TestEqual(TEXT("The entry is still told for its own match"),Watch.Observe(&View,3).Num(),1);
    View.Generation=4;
    TestTrue(TEXT("A new match primes again"),Watch.Observe(&View,4).IsEmpty());

    // The 构筑 button is lit by pending component choices.
    FLDProgressionView Pending;Pending.bValid=true;Pending.Generation=5;
    FLDComponentChoiceView& Choice=Pending.ComponentChoices.AddDefaulted_GetRef();Choice.ChoiceId=FGuid(1,2,3,4);
    Choice.Options={FName(TEXT("hud_fixture_a")),FName(TEXT("hud_fixture_b"))};
    const LDHUD::FGrowthPending Counts=LDHUD::GrowthPending(&Pending,5,nullptr);
    TestTrue(TEXT("A pending component choice lights the button"),Counts.Any()&&Counts.ComponentChoices==1);
    TestFalse(TEXT("Options not owned leave the points closed"),LDHUD::ComponentChoiceAllOwned(Choice,Pending));
    FLDComponentInstanceView& HeldA=Pending.Components.AddDefaulted_GetRef();HeldA.DefinitionId=TEXT("hud_fixture_a");
    TestFalse(TEXT("One free option still leaves the points closed"),LDHUD::ComponentChoiceAllOwned(Choice,Pending));
    FLDComponentInstanceView& HeldB=Pending.Components.AddDefaulted_GetRef();HeldB.DefinitionId=TEXT("hud_fixture_b");
    TestTrue(TEXT("Every option owned opens the points"),LDHUD::ComponentChoiceAllOwned(Choice,Pending));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDMaterialServicesTest,"Lockdown.Rules.HUDMaterialServices",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDMaterialServicesTest::RunTest(const FString& Parameters)
{
    // #198 G panel hints: the component choice follows the active choice's gate, FillGap and Dismantle the rest window.
    using EStage=LDProgression::EMatchStage;
    LDHUD::FBuildGateView Gate=LDHUD::BuildGate(EStage::Rest,ELDLifePhase::Alive);
    TestTrue(TEXT("A rest build window opens the choice and both material services"),Gate.bCanComponentChoice&&Gate.bCanFillGap&&Gate.bCanDismantle);
    const LDHUD::FBuildGateView Field=LDHUD::BuildGate(EStage::Live,ELDLifePhase::Alive);
    TestTrue(TEXT("In combat the choice is still taken; the material services wait for a rest"),Field.bCanComponentChoice&&!Field.bCanFillGap&&!Field.bCanDismantle
        &&Field.FillGapBlocked==LDHUD::GrowthReasonText(ELDGrowthReason::NotRestWindow)&&Field.DismantleBlocked==Field.FillGapBlocked);
    TestTrue(TEXT("A dead contender still takes the choice"),LDHUD::BuildGate(EStage::Live,ELDLifePhase::Dead).bCanComponentChoice);
    const LDHUD::FBuildGateView Locked=LDHUD::BuildGate(EStage::RestLocked,ELDLifePhase::Alive);
    TestTrue(TEXT("#222: a locked rest step still takes the choice and refuses both services with the lock"),Locked.bCanComponentChoice
        &&!Locked.bCanFillGap&&!Locked.bCanDismantle&&Locked.FillGapBlocked==LDHUD::GrowthReasonText(ELDGrowthReason::BuildLocked));

    // A private fixture: one mod whose only entry is an unowned common base, three common bases and a scarce one.
    FLDDemoContentDefinitions Content;
    Content.Parameters.FillGapPoints=4;Content.Parameters.DismantleInputCount=3;Content.Parameters.EmptyComponentPoolPoints=2;
    const auto AddComponent=[&Content](const TCHAR* Id,ELDDemoComponentKind Kind,ELDDemoComponentPool Pool)->FLDDemoComponentDefinition&
    {
        FLDDemoComponentDefinition& Row=Content.Components.AddDefaulted_GetRef();
        Row.Id=Id;Row.NativeId=uint8(Content.Components.Num());Row.Kind=Kind;Row.Pool=Pool;
        return Row;
    };
    AddComponent(TEXT("hud_gap_base_a"),ELDDemoComponentKind::Base,ELDDemoComponentPool::Common);
    AddComponent(TEXT("hud_gap_base_b"),ELDDemoComponentKind::Base,ELDDemoComponentPool::Common);
    AddComponent(TEXT("hud_gap_base_c"),ELDDemoComponentKind::Base,ELDDemoComponentPool::Common);
    AddComponent(TEXT("hud_gap_rare"),ELDDemoComponentKind::Base,ELDDemoComponentPool::Scarce);
    AddComponent(TEXT("hud_gap_mod"),ELDDemoComponentKind::Mod,ELDDemoComponentPool::Common).TargetComponents={TEXT("hud_gap_base_a")};
    const auto Owned=[](const TCHAR* Id){FLDComponentInstanceView View;View.DefinitionId=Id;View.Source=ELDComponentSource::LevelUp;View.SourceLevel=2;return View;};
    FLDProgressionView View;View.bValid=true;View.Generation=2;View.Level=3;View.UnspentPoints=5;
    View.Components={Owned(TEXT("hud_gap_base_b")),Owned(TEXT("hud_gap_mod")),Owned(TEXT("hud_gap_rare"))};
    LDHUD::FMaterialServicesView Services=LDHUD::MaterialServices(View,&Content,Gate);
    TestTrue(TEXT("The never-enabled mod without an entry is the gap, filled by its unowned base"),Services.Gaps.Num()==1
        &&Services.Gaps[0].Target==FName(TEXT("hud_gap_mod"))&&Services.Gaps[0].Fillers==TArray<FName>{FName(TEXT("hud_gap_base_a"))});
    TestTrue(TEXT("FillGap may be sent with the price in unspent points"),Services.FillGapPoints==4&&Services.FillGapBlocked.IsEmpty());
    TestTrue(TEXT("Only owned common components are dismantle candidates"),Services.DismantleCandidates==TArray<FName>{FName(TEXT("hud_gap_base_b")),FName(TEXT("hud_gap_mod"))});
    TestEqualSensitive(TEXT("Fewer than the approved count refuses the dismantle"),Services.DismantleBlocked,LDTerms::DismantleTooFew(3));
    View.UnspentPoints=3;
    TestEqualSensitive(TEXT("Not enough unspent points names the price"),LDHUD::MaterialServices(View,&Content,Gate).FillGapBlocked,
        FString(TEXT("未分配点数不够（要 4 点）")));
    TestEqualSensitive(TEXT("The field names the rest window first"),LDHUD::MaterialServices(View,&Content,Field).FillGapBlocked,Field.FillGapBlocked);
    TestFalse(TEXT("Without the catalog nothing is offered"),LDHUD::MaterialServices(View,nullptr,Gate).FillGapBlocked.IsEmpty());
    View.UnspentPoints=5;
    View.Components.Add(Owned(TEXT("hud_gap_base_c")));
    Services=LDHUD::MaterialServices(View,&Content,Gate);
    TestTrue(TEXT("Three owned common components open the dismantle"),Services.DismantleCandidates.Num()==3&&Services.DismantleBlocked.IsEmpty());
    TestTrue(TEXT("Three distinct owned common components are a legal input"),
        LDHUD::DismantleSelectionRefusal(View,&Content,{FName(TEXT("hud_gap_base_b")),FName(TEXT("hud_gap_base_c")),FName(TEXT("hud_gap_mod"))}).IsEmpty());
    TestFalse(TEXT("A scarce component is not accepted"),
        LDHUD::DismantleSelectionRefusal(View,&Content,{FName(TEXT("hud_gap_base_b")),FName(TEXT("hud_gap_base_c")),FName(TEXT("hud_gap_rare"))}).IsEmpty());
    TestFalse(TEXT("A repeated component is not accepted"),
        LDHUD::DismantleSelectionRefusal(View,&Content,{FName(TEXT("hud_gap_base_b")),FName(TEXT("hud_gap_base_b")),FName(TEXT("hud_gap_mod"))}).IsEmpty());

    // The dismantle preview: installs on a surrendered host (or on one thereby sent back) return to the inventory.
    {
        FLDProgressionView Built;Built.bValid=true;Built.Generation=2;
        const auto Installed=[](const TCHAR* Id,const TCHAR* Host,const TCHAR* Base)
        {
            FLDComponentInstanceView Row;Row.DefinitionId=Id;Row.bInstalled=Row.bEverInstalled=true;
            if(Host[0]){Row.Target.Kind=ELDComponentTargetKind::Component;Row.Target.Definition=Host;}
            if(Base[0]){Row.Target.BoundComponent=Base;}
            return Row;
        };
        Built.Components={Installed(TEXT("host"),TEXT(""),TEXT("")),Installed(TEXT("on_host"),TEXT("host"),TEXT("")),
            Installed(TEXT("on_on_host"),TEXT("on_host"),TEXT("")),Installed(TEXT("repeats_host"),TEXT(""),TEXT("host")),
            Installed(TEXT("elsewhere"),TEXT("other"),TEXT(""))};
        TestTrue(TEXT("Every install depending on the surrendered host returns, in the view's order"),
            LDHUD::DismantleDetached(Built,{FName(TEXT("host"))})==TArray<FName>{FName(TEXT("on_host")),FName(TEXT("on_on_host")),FName(TEXT("repeats_host"))});
        TestTrue(TEXT("Surrendering a leaf sends nothing back"),LDHUD::DismantleDetached(Built,{FName(TEXT("on_on_host"))}).IsEmpty());
    }

    // Receipts name what was received, paid and removed; a missing field never reads as success.
    FLDBuildReceipt Receipt;Receipt.Header.Outcome=ELDReceiptOutcome::Applied;
    Receipt.Kind=ELDBuildCommandKind::ComponentChoice;Receipt.ComponentId=TEXT("hud_gap_base_a");
    TestEqualSensitive(TEXT("A component choice names the component taken"),LDHUD::BuildReceiptText(Receipt,nullptr),LDTerms::ComponentTaken(TEXT("hud_gap_base_a")));
    Receipt.ComponentId=NAME_None;
    TestEqualSensitive(TEXT("A component choice without a component took the points"),LDHUD::BuildReceiptText(Receipt,nullptr),
        FString(LDTerms::ComponentChoiceTookPoints));
    Receipt.Kind=ELDBuildCommandKind::FillGap;Receipt.ComponentId=TEXT("hud_gap_base_a");Receipt.ServicePointsPaid=4;
    TestEqualSensitive(TEXT("A gap fill names the price and the base"),LDHUD::BuildReceiptText(Receipt,nullptr),LDTerms::GapFilled(4,TEXT("hud_gap_base_a")));
    Receipt.ServicePointsPaid=0;
    TestEqualSensitive(TEXT("A gap fill without its payment claims nothing"),LDHUD::BuildReceiptText(Receipt,nullptr),FString(TEXT("回执缺少结果，请以当前状态为准。")));
    Receipt.Kind=ELDBuildCommandKind::Dismantle;Receipt.ComponentId=TEXT("new");
    Receipt.RemovedComponents={FName(TEXT("a")),FName(TEXT("b")),FName(TEXT("c"))};Receipt.UninstalledComponents={FName(TEXT("d"))};
    TestEqualSensitive(TEXT("A dismantle names the removed, the received and the uninstalled"),LDHUD::BuildReceiptText(Receipt,nullptr),
        LDTerms::Dismantled(TEXT("a、b、c"),TEXT("new"),TEXT("d")));
    Receipt.RemovedComponents.Reset();
    TestEqualSensitive(TEXT("A dismantle without its removed list claims nothing"),LDHUD::BuildReceiptText(Receipt,nullptr),
        FString(TEXT("回执缺少结果，请以当前状态为准。")));
    // Every #198 G reason names itself; none falls through to the unknown line or repeats another.
    TSet<FString> Seen;
    for(const ELDGrowthReason Reason:{ELDGrowthReason::NoComponentChoice,ELDGrowthReason::ComponentOwned,ELDGrowthReason::NotAGap,ELDGrowthReason::NoGapFiller,
        ELDGrowthReason::GapAlreadyFilled,ELDGrowthReason::DismantleInvalid,ELDGrowthReason::DismantlePoolEmpty,ELDGrowthReason::FieldRewardDuplicate,
        ELDGrowthReason::FinalLocked,ELDGrowthReason::NoFinalMod,ELDGrowthReason::FinalModUnavailable})
    {
        const FString Text=LDHUD::GrowthReasonText(Reason);bool bDuplicate=false;Seen.Add(Text,&bDuplicate);
        TestTrue(FString::Printf(TEXT("Reason %d has its own text"),int32(Reason)),!Text.IsEmpty()&&!Text.StartsWith(TEXT("未知原因"))&&!bDuplicate);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDChoiceTrayTest,"Lockdown.Rules.HUDChoiceTray",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDChoiceTrayTest::RunTest(const FString& Parameters)
{
    // A card shows only its icon and name; the popup carries the kind, the full rule, the keywords (as plain text) and what
    // a click does when that is not the plain take.
    LDHUD::FActiveCardView Card;Card.Name=TEXT("肘击");Card.Target=TEXT("单体");Card.Cost=TEXT("法力 30");Card.bKnown=true;
    Card.SentencePlain=TEXT("造成 10 点伤害并减速");Card.Keywords={TEXT("<b>减速</>：移速降低 &lt;30%&gt;")};
    const LDHUD::FActionDetailView Offer=LDHUD::ActiveChoiceDetail(Card,false,LDTerms::LearnInto(TEXT("Q")));
    TestEqualSensitive(TEXT("An offer popup is named after the active"),Offer.Name,FString(TEXT("肘击")));
    TestEqualSensitive(TEXT("An offer popup names the kind, the targeting and the cost"),Offer.Summary,FString(TEXT("技能 · 单体 · 法力 30")));
    TestEqualSensitive(TEXT("The rule, its plain keyword lines and the outcome follow"),Offer.Body,
        FString(TEXT("造成 10 点伤害并减速\n\n减速：移速降低 <30%>\n\n"))+LDTerms::LearnInto(TEXT("Q")));
    TestTrue(TEXT("A blueprint card says it is a blueprint"),LDHUD::ActiveChoiceDetail(Card,true,FString()).Summary.StartsWith(LDTerms::Blueprint));
    Card.bKnown=false;
    TestEqualSensitive(TEXT("An unknown active invents no targeting or cost"),LDHUD::ActiveChoiceDetail(Card,false,FString()).Summary,FString(TEXT("技能")));
    LDHUD::FComponentCardView Part;Part.Name=TEXT("铁拳");Part.SentencePlain=TEXT("普攻多打 5 点");
    const LDHUD::FActionDetailView Taken=LDHUD::ComponentChoiceDetail(Part,FString());
    TestTrue(TEXT("A plain take adds no outcome line"),Taken.Summary==LDTerms::Component&&Taken.Body==TEXT("普攻多打 5 点"));
    TestTrue(TEXT("A component that cannot be taken says why"),LDHUD::ComponentChoiceDetail(Part,LDTerms::ComponentChoiceOwned).Body.EndsWith(LDTerms::ComponentChoiceOwned));

    // Component rows turn into icon tiles once they hold more than ChoiceTrayCardLimit cards together.
    FLDProgressionView View;View.bValid=true;View.Generation=3;
    for(int32 Index=0;Index<LDHUD::ChoiceTrayCardLimit;++Index){View.OpeningBaseOptions.Add(FName(*FString::Printf(TEXT("base_%d"),Index)));}
    View.bOpeningBasePending=true;
    TestFalse(TEXT("Up to the limit the cards keep their names"),LDHUD::ChoiceTrayTiles(View));
    FLDComponentChoiceView& Choice=View.ComponentChoices.AddDefaulted_GetRef();Choice.ChoiceId=FGuid(1,2,3,4);Choice.Options={FName(TEXT("mod_a"))};
    TestTrue(TEXT("One card past the limit makes a grid"),LDHUD::ChoiceTrayTiles(View));
    View.bOpeningBasePending=false;
    TestFalse(TEXT("A taken opening pick no longer counts"),LDHUD::ChoiceTrayTiles(View));
    View.Components.AddDefaulted_GetRef().DefinitionId=TEXT("mod_a");
    const FLDComponentChoiceView Owned=View.ComponentChoices[0];
    for(int32 Index=0;Index<3;++Index){View.ComponentChoices.Add(Owned);}
    TestTrue(TEXT("An all-owned choice counts its points card too"),LDHUD::ChoiceTrayTiles(View));
    TestEqualSensitive(TEXT("Service admission refusal gives the exact proximity instruction"),
        LDHUD::GrowthReasonText(ELDGrowthReason::RestServiceUnavailable),FString(TEXT("请靠近对应的服务NPC")));
    TestTrue(TEXT("Window and lock refusals remain distinct from NPC proximity"),
        LDHUD::GrowthReasonText(ELDGrowthReason::NotRestWindow)!=LDHUD::GrowthReasonText(ELDGrowthReason::RestServiceUnavailable)
        &&LDHUD::GrowthReasonText(ELDGrowthReason::BuildLocked)!=LDHUD::GrowthReasonText(ELDGrowthReason::RestServiceUnavailable));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDDevTestPanelTest,"Lockdown.Rules.HUDDevTestPanel",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDDevTestPanelTest::RunTest(const FString& Parameters)
{
    if(!HudRegisterFixtureTable(*this)){return false;}
    ON_SCOPE_EXIT{FStringTableRegistry::Get().UnregisterStringTable(HudFixtureTable);};
    // Entries are exact: a malformed amount or value is refused locally and sends nothing.
    int64 Value=0;
    TestTrue(TEXT("A positive amount parses exactly"),LDDevTest::ParseAmount(TEXT(" 120 "),Value)&&Value==120);
    int32 AmountRefusals=0;
    for(const TCHAR* Bad:{TEXT(""),TEXT("0"),TEXT("-5"),TEXT("1.5"),TEXT("12a"),TEXT("1234567890123456789")}){AmountRefusals+=LDDevTest::ParseAmount(Bad,Value)?0:1;}
    TestEqual(TEXT("Empty, zero, signed, fractional, non-digit and over-long amounts are refused"),AmountRefusals,6);
    TestTrue(TEXT("Two decimals scale to P2 / basis points"),LDDevTest::ParseScaled(TEXT("12.5"),2,Value)&&Value==1250&&LDDevTest::ParseScaled(TEXT("7"),2,Value)&&Value==700);
    TestTrue(TEXT("Whole centimetres scale by one"),LDDevTest::ParseScaled(TEXT("40"),0,Value)&&Value==40&&LDDevTest::ParseScaled(TEXT("0"),2,Value)&&Value==0);
    int32 ValueRefusals=0;
    for(const TPair<const TCHAR*,int32>& Bad:{TPair<const TCHAR*,int32>(TEXT("1.234"),2),{TEXT("1.5"),0},{TEXT("-1"),2},{TEXT(".5"),2},{TEXT("5."),2},{TEXT("1e3"),2}})
    {ValueRefusals+=LDDevTest::ParseScaled(Bad.Key,Bad.Value,Value)?0:1;}
    TestEqual(TEXT("Extra decimals, signs, missing digits and exponents are refused, never rounded"),ValueRefusals,6);
    // One settable field per permanent build stat, each shown in its projected attribute row.
    const TConstArrayView<LDDevTest::FStatField> Fields=LDDevTest::StatFields();
    TSet<uint8> Stats;TSet<FString> Names;bool bRowsValid=true;
    for(const LDDevTest::FStatField& Field:Fields)
    {
        Stats.Add(Field.Stat);Names.Add(Field.ConsoleName);
        bRowsValid&=Field.Attribute<uint8(ELDProjectedAttribute::Count)&&Field.Stat!=uint8(ELDDemoBonusStat::ADBonusRatio);
    }
    TestTrue(TEXT("Eleven distinct permanent stats, never the conditional AD ratio"),Fields.Num()==11&&Stats.Num()==11&&Names.Num()==11&&bRowsValid);
    const LDDevTest::FStatField* Crit=Fields.FindByPredicate([](const LDDevTest::FStatField& F){return F.Stat==uint8(ELDDemoBonusStat::CritChanceBonusRatio);});
    const LDDevTest::FStatField* Range=Fields.FindByPredicate([](const LDDevTest::FStatField& F){return F.Stat==uint8(ELDDemoBonusStat::AttackRangeCmFlat);});
    TestTrue(TEXT("Fields name the stat in player terms with the entry unit"),Crit&&Range&&LDDevPanel::StatLabel(*Crit)==LDTerms::CritChance()+TEXT("（百分点）")
        &&LDDevPanel::StatLabel(*Range).EndsWith(TEXT("（厘米）")));

    // Catalog rows: every active, with what the own progression holds of it (#198).
    FLDDemoContentDefinitions Content=HudFixtureContent();
    for(const TCHAR* Id:{TEXT("root"),TEXT("shield"),TEXT("camp_returning")}){FLDDemoAbilityDefinition Active;Active.Id=Id;Content.Abilities.Add(Active);}
    FLDProgressionView Progression;Progression.bValid=true;Progression.Level=3;Progression.CumulativeXP=450;Progression.UnspentPoints=4;
    Progression.QueuedActiveOpportunities=1;
    FLDActiveSlotView Learned;Learned.DefinitionId=TEXT("root");Learned.Slot=1;Progression.Actives.Add(Learned);
    Progression.PendingBlueprints={FName(TEXT("shield"))};
    const TArray<LDDevPanel::FActiveRow> Rows=LDDevPanel::ActiveRows(Content,&Progression);
    TestTrue(TEXT("Every active of the catalog is listed in order"),Rows.Num()==3&&Rows[0].Name==HudName(TEXT("root"))&&Rows[1].Id==FName(TEXT("shield")));
    TestTrue(TEXT("What is held comes from the own progression: a learned slot, a blueprint or nothing"),LDDevPanel::HeldText(Rows[0])==TEXT("已学 W")
        &&LDDevPanel::HeldText(Rows[1])==LDTerms::Blueprint&&LDDevPanel::HeldText(Rows[2])==TEXT("未拥有"));
    TestTrue(TEXT("Without a projection nothing is held"),LDDevPanel::ActiveRows(Content,nullptr)[0].Held==LDDevPanel::EActiveHeld::None);
    TestFalse(TEXT("One held active does not need a replacement"),LDDevPanel::AllActiveSlotsHeld(&Progression));
    Learned.Slot=0;Learned.DefinitionId=TEXT("other_active");Progression.Actives.Add(Learned);
    TestFalse(TEXT("Q and W leave E free"),LDDevPanel::AllActiveSlotsHeld(&Progression));
    Learned.Slot=2;Learned.DefinitionId=TEXT("third_active");Progression.Actives.Add(Learned);
    TestTrue(TEXT("Q, W and E held offer the DEV replacement"),LDDevPanel::AllActiveSlotsHeld(&Progression));
    TestFalse(TEXT("An unprojected progression offers nothing"),LDDevPanel::AllActiveSlotsHeld(nullptr));

    // Own state lines: an unprojected view reads as syncing, never as 0.
    TestEqualSensitive(TEXT("Nothing projected is syncing"),LDDevPanel::StatusLine(nullptr),FString(TEXT("等级 同步中")));
    TestEqualSensitive(TEXT("Level, XP, unspent points and queued active opportunities"),LDDevPanel::StatusLine(&Progression),
        FString::Printf(TEXT("等级 3 · %s 450 · 未分配点数 4 · 待选技能 1 次"),LDTerms::Experience));
    TestEqualSensitive(TEXT("An attribute without a projected row is syncing"),LDDevPanel::AttributeLine(nullptr,uint8(ELDProjectedAttribute::AD)),FString(TEXT("同步中")));
    FLDCombatOwnerView Combat;FLDCombatAttributeRowView AD;AD.Attribute=uint8(ELDProjectedAttribute::AD);
    for(FLDDemoRational* Term:{&AD.Base,&AD.Level,&AD.Choices,&AD.PercentBonus,&AD.Dev,&AD.DevPercent,&AD.Temporary,&AD.TemporaryPercent,&AD.Final})
    {Term->Numerator=0;Term->Denominator=1;}
    AD.Base.Numerator=50;AD.Dev.Numerator=12;AD.Final.Numerator=62;Combat.Attributes.Add(AD);
    const FString ADLine=LDDevPanel::AttributeLine(&Combat,uint8(ELDProjectedAttribute::AD));
    TestTrue(TEXT("The committed final value shows with the test layer among its sources"),ADLine.StartsWith(TEXT("当前 62"))&&ADLine.Contains(TEXT("测试")));

    // The authority's answer, never a guessed success.
    FLDDevTestResult Result;Result.Status=ELDAdmissionStatus::AdmissionRejected;Result.Reason=uint16(ELDGrowthReason::NotAuthority);
    TestTrue(TEXT("An admission refusal names its reason"),LDDevPanel::ResultLine(Result).StartsWith(TEXT("请求被拒绝（")));
    Result.Status=ELDAdmissionStatus::Final;Result.bHasReceipt=true;Result.Receipt.Op=ELDDevTestOp::GrantExperience;Result.Receipt.Header.Outcome=ELDReceiptOutcome::Applied;
    TestEqualSensitive(TEXT("An applied receipt says so"),LDDevPanel::ResultLine(Result),FString(TEXT("加"))+LDTerms::Experience+TEXT(" 已生效"));
    Result.Receipt.Header.Outcome=ELDReceiptOutcome::Rejected;Result.Receipt.Header.Reason=uint16(ELDGrowthReason::DataNotReady);
    TestTrue(TEXT("A refused receipt names the op and its reason"),LDDevPanel::ResultLine(Result).StartsWith(FString(TEXT("加"))+LDTerms::Experience+TEXT(" 被拒绝（")));
    Result.Status=ELDAdmissionStatus::AcceptedPending;Result.bHasReceipt=false;
    TestTrue(TEXT("An admitted command waits for its commit"),LDDevPanel::ResultLine(Result).StartsWith(TEXT("已受理")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDIconsTest,"Lockdown.Rules.HUDIcons",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDIconsTest::RunTest(const FString& Parameters)
{
    // #85: every icon the HUD and the growth panel draw is an imported texture: each active, stat and status of the approved
    // catalog (only the character and the creatures have no icon square), plus the dash and the two recoveries, which have no
    // display row.
    TestTrue(TEXT("Icon packages live in the cooked UI icon folder"),LDUIIcons::PackageFor(TEXT("icon_root"))==TEXT("/Game/Lockdown/Art/UI/Icons/T_icon_root"));
    TestTrue(TEXT("A malformed icon id maps to no package"),LDUIIcons::PackageFor(TEXT("root")).IsEmpty()&&LDUIIcons::PackageFor(TEXT("icon_../x")).IsEmpty()
        &&LDUIIcons::PackageFor(TEXT("icon_Root")).IsEmpty());
    TSharedPtr<const FLDDemoContentSnapshot> Snapshot;
    if(!LDTestApprovedFixture::Read(*this,Snapshot)){return false;}
    const FLDDemoContentDefinitions& Content=Snapshot->Get();
    TSet<FString> NoSquare{Content.Character.Id};
    for(const FLDDemoMonsterDefinition& Monster:Content.Monsters){NoSquare.Add(Monster.Id);}
    TArray<FString> Needed{LDUIIcons::Dash,LDUIIcons::OutOfCombatRegen,LDUIIcons::DormRegen};
    for(const FLDDemoDisplayDefinition& Display:Content.Display){if(!NoSquare.Contains(Display.Id)){Needed.Add(Display.IconId);}}
    TestTrue(TEXT("The catalog lists actives, stats and statuses"),Needed.Num()>3+Content.Abilities.Num()+Content.Stats.Num());
    for(const FString& IconId:Needed)
    {
        const UTexture2D* Texture=LDUIIcons::Find(IconId);
        TestTrue(FString::Printf(TEXT("%s is an imported icon texture"),*IconId),Texture!=nullptr);
    }
    // A missing one is a visible, logged content error rather than a blank square.
    // A fresh id per run: each id is reported once per process.
    const FString Unknown=TEXT("icon_hud_rules_fixture_missing_")+FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
    AddExpectedMessage(TEXT("LD_UI_ICON_MISSING '")+Unknown+TEXT("'"),ELogVerbosity::Error,EAutomationExpectedMessageFlags::Contains,1,false);
    TestNull(TEXT("An id without an imported texture finds nothing"),LDUIIcons::Find(Unknown));
    TestNull(TEXT("A second lookup still finds nothing and logs no more"),LDUIIcons::Find(Unknown));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDTermsGlossaryTest,"Lockdown.Rules.HUDTermsGlossary",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDTermsGlossaryTest::RunTest(const FString& Parameters)
{
    // #111: system text names a few statuses directly; those glossary copies must be the approved catalog's display names.
    TMap<FString,FString> Names;
    if(!LDTestApprovedDisplay::Names(*this,Names)){return false;}
    const TPair<const TCHAR*,const TCHAR*> Shared[]={{TEXT("shield"),LDTerms::Shield},{TEXT("root"),LDTerms::Root},{TEXT("slow"),LDTerms::Slow},
        {TEXT("taunt"),LDTerms::Taunt},{TEXT("disarm"),LDTerms::Disarm},{TEXT("stun"),LDTerms::Stun},
        {TEXT("respawn_protection"),LDTerms::RespawnProtection},
        // #147 keywords: silence and burn are statuses of their own; breach is the shared resistance shred (exposed).
        {TEXT("silence"),LDTerms::Silence},{TEXT("burn"),LDTerms::Burn},{TEXT("exposed"),LDTerms::Breach}};
    for(const TPair<const TCHAR*,const TCHAR*>& Pair:Shared)
    {
        const FString* Name=Names.Find(Pair.Key);
        TestTrue(FString::Printf(TEXT("The glossary's %s is the approved display name"),Pair.Key),Name&&Name->Equals(Pair.Value,ESearchCase::CaseSensitive));
    }
    // Every system term is filled, distinct from the others and never an entry name, so it cannot read as a skill or creature.
    const TCHAR* const Terms[]={LDTerms::Health,LDTerms::Mana,LDTerms::AttackDamage,LDTerms::AbilityPower,LDTerms::Armor,LDTerms::MagicResist,
        LDTerms::AttackSpeed,LDTerms::MoveSpeed,LDTerms::Crit,LDTerms::Haste,LDTerms::PhysicalDamage,LDTerms::MagicalDamage,
        LDTerms::Experience,LDTerms::BasicAttack,LDTerms::Dash,LDTerms::CapturePoint,LDTerms::CampLow,LDTerms::CampHigh,
        LDTerms::Dead,LDTerms::RespawnWait,LDTerms::Choice,LDTerms::Winner,LDTerms::UnknownVictim};
    TSet<FString> Seen;TArray<FString> EntryNames;Names.GenerateValueArray(EntryNames);
    for(const TCHAR* Term:Terms)
    {
        const FString Value(Term);
        bool bDuplicate=false;Seen.Add(Value,&bDuplicate);
        TestTrue(FString::Printf(TEXT("System term %s is filled, unique and no entry name"),Term),!Value.IsEmpty()&&!bDuplicate&&!EntryNames.Contains(Value));
    }
    // The front page and the crowning tell one story: the capture point decides the winner title.
    TestTrue(TEXT("The title names the winner title"),FString(LDTerms::Title).Contains(LDTerms::Winner));
    TestTrue(TEXT("The tagline names the capture point and the winner title"),FString(LDTerms::Tagline).Contains(LDTerms::CapturePoint)&&FString(LDTerms::Tagline).Contains(LDTerms::Winner));
    TestTrue(TEXT("The slang note is shown text, not a placeholder"),FString(LDTerms::SlangNote).Len()>4);
    TestTrue(TEXT("A kill line keeps both names in order"),LDTerms::KillLine(TEXT("甲"),TEXT("乙")).StartsWith(TEXT("甲 "))&&LDTerms::KillLine(TEXT("甲"),TEXT("乙")).Contains(TEXT(" 乙 ")));
    return true;
}
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDAttributeViewTest,"Lockdown.Rules.HUDAttributeView",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDAttributeViewTest::RunTest(const FString& Parameters)
{
    // The HUD attribute view lists each projected attribute once, except what the health and mana bars already show.
    using A=ELDProjectedAttribute;
    const TArray<uint8> Order=LDHUD::AttributeViewOrder();
    for(const A Shown:{A::MaxHP,A::MaxMP,A::HPRegen,A::MPRegen})
    {TestFalse(TEXT("The bars' maxima and regenerations are not repeated"),Order.Contains(uint8(Shown)));}
    TestEqual(TEXT("Every other projected attribute is listed"),Order.Num(),int32(A::Count)-4);
    TSet<FString> Glyphs;
    for(const uint8 Attribute:Order)
    {
        FLDCombatAttributeRowView Probe;Probe.Attribute=Attribute;
        Glyphs.Add(LDHUD::AttributeRow(Probe).Glyph);
    }
    TestEqual(TEXT("Each listed attribute has its own icon glyph"),Glyphs.Num(),Order.Num());
    // The entry shows the final value alone; the popup keeps the unit, the clamp note and the sources.
    FLDCombatAttributeRowView Speed;Speed.Attribute=uint8(A::AttackSpeed);Speed.Final.Numerator=5;Speed.Final.Denominator=4;Speed.bClamped=true;
    const LDHUD::FAttributeRowView Row=LDHUD::AttributeRow(Speed);
    TestEqualSensitive(TEXT("Attack speed reads as whole attacks per minute, without a unit in the entry"),Row.Value,FString(TEXT("75")));
    TestTrue(TEXT("The popup line keeps the unit and the clamp"),Row.Final.StartsWith(TEXT("75 次/分钟"))&&Row.Final.Contains(TEXT("上下限")));
    FLDCombatAttributeRowView SlowSpeed;SlowSpeed.Attribute=uint8(A::AttackSpeed);SlowSpeed.Final.Numerator=9;SlowSpeed.Final.Denominator=10;
    TestEqualSensitive(TEXT("0.9 attacks per second reads as 54"),LDHUD::AttributeRow(SlowSpeed).Value,FString(TEXT("54")));
    FLDCombatAttributeRowView Move;Move.Attribute=uint8(A::MoveSpeed);Move.Final.Numerator=300;Move.Final.Denominator=1;
    TestEqualSensitive(TEXT("Move speed has no unit anywhere"),LDHUD::AttributeRow(Move).Final,FString(TEXT("300")));
    FLDCombatAttributeRowView Haste;Haste.Attribute=uint8(A::CooldownHaste);Haste.Final.Numerator=30;Haste.Final.Denominator=1;
    const LDHUD::FAttributeRowView HasteRow=LDHUD::AttributeRow(Haste);
    TestEqualSensitive(TEXT("Haste 30 reads as a 23% cooldown reduction"),HasteRow.Value,FString(TEXT("23%")));
    TestTrue(TEXT("The haste popup keeps the haste points"),HasteRow.Final.Contains(TEXT("23%"))&&HasteRow.Final.Contains(TEXT("30")));
    FLDCombatAttributeRowView NoHaste;NoHaste.Attribute=uint8(A::CooldownHaste);NoHaste.Final.Denominator=1;
    TestEqualSensitive(TEXT("No haste reads as 0%"),LDHUD::AttributeRow(NoHaste).Value,FString(TEXT("0%")));
    FLDCombatAttributeRowView Crit;Crit.Attribute=uint8(A::CritChance);Crit.Final.Numerator=1;Crit.Final.Denominator=4;
    TestEqualSensitive(TEXT("A ratio reads as a percentage"),LDHUD::AttributeRow(Crit).Value,FString(TEXT("25%")));
    return true;
}
