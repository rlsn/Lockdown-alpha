#include "UI/LDHUDModel.h"
#include "Development/LDDemoCardText.h"
#include "UI/LDTerms.h"
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
#include <limits>
#if WITH_DEV_AUTOMATION_TESTS
namespace
{
// A private table id keeps the fixture away from the real DemoContent display table. Its names are the approved
// catalog's (#111), so expectations read them through HudName instead of repeating them.
const FName HudFixtureTable(TEXT("LDHUDRulesDisplayFixture"));
const TCHAR* const HudFixtureIds[]={TEXT("root"),TEXT("shield"),TEXT("respawn_protection"),TEXT("camp_returning"),TEXT("monster_guardian"),TEXT("item_iron_plate"),TEXT("passive_iced_americano")};
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
    {FLDDemoDisplayDefinition Display;Display.Id=Id;Display.Namespace=HudFixtureTable.ToString();Display.NameKey=FString(Id)+TEXT(".name");Display.DescriptionKey=FString(Id)+TEXT(".description");Content.Display.Add(Display);}
    FLDDemoMonsterDefinition Monster;Monster.Id=TEXT("monster_guardian");Monster.NativeId=uint8(ELDDemoMonsterId::MonsterGuardian);Content.Monsters.Add(Monster);
    FLDDemoEquipmentDefinition Item;Item.Id=TEXT("item_iron_plate");Item.NativeId=uint8(ELDDemoEquipmentId::ItemIronPlate);Content.Equipment.Add(Item);
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
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDMaskTest,"Lockdown.Rules.HUDObservationBoundary",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDMaskTest::RunTest(const FString& Parameters)
{
    if(!HudRegisterFixtureTable(*this)){return false;}
    ON_SCOPE_EXIT{FStringTableRegistry::Get().UnregisterStringTable(HudFixtureTable);};
    const FLDDemoContentDefinitions Content=HudFixtureContent();
    FLDObservedUnitView Rival;Rival.Kind=ELDObservedUnitKind::Contender;Rival.AuthorizedFieldMask=LDObservation::ContenderFields;Rival.ContenderId=FGuid::NewGuid();Rival.Level=3;
    Rival.Health.CurrentP2=45001;Rival.Health.MaxP2=60000;Rival.Mana.CurrentP2=12099;Rival.Mana.MaxP2=20000;Rival.Equipment.Slots[0]=uint8(ELDDemoEquipmentId::ItemIronPlate);
    FLDObservedStatus Root;Root.StatusId=uint8(ELDDemoStatusId::Root);Root.ExpiresAtServerMs=12500;Rival.Statuses.Add(Root);
    FLDObservedStatus Protection;Protection.StatusId=uint8(ELDDemoStatusId::RespawnProtection);Protection.ExpiresAtServerMs=11500;Rival.Statuses.Add(Protection);
    const FString Shown=LDHUD::ObservedUnit(Rival,TEXT("玩家甲"),&Content,10.);
    auto Has=[](const FString& Text,const TCHAR* Label){return Text.Contains(Label,ESearchCase::CaseSensitive);};
    TestEqualSensitive(TEXT("Observed card renders exactly the SC-07 whitelist (with the #112 Q/W actives) with approved display names"),Shown,
        FString::Printf(TEXT("玩家甲 · 等级 3\n%s 451/600 · %s 120/200\n状态：%s 2.5s · %s 1.5s\n装备：%s · 空槽\n技能：Q 空槽 · W 空槽"),LDTerms::Health,LDTerms::Mana,
            *HudName(TEXT("root")),*HudName(TEXT("respawn_protection")),*HudName(TEXT("item_iron_plate"))));
    for(const TCHAR* Private:{LDTerms::Gold,LDTerms::Experience,TEXT("冷却"),TEXT("被动"),LDTerms::AttackDamage,LDTerms::Armor,TEXT("属性"),TEXT("98765")})
    {TestFalse(FString::Printf(TEXT("Observed card has no private category: %s"),Private),Has(Shown,Private));}
    TestTrue(TEXT("A contender missing from the public roster is not given an invented name"),LDHUD::ObservedUnit(Rival,FString(),&Content,10.).StartsWith(TEXT("未列名参赛者"),ESearchCase::CaseSensitive));
    FLDObservedUnitView Camp;Camp.Kind=ELDObservedUnitKind::CampMember;Camp.AuthorizedFieldMask=LDObservation::CampFields;Camp.CreatureId=uint8(ELDDemoMonsterId::MonsterGuardian);
    Camp.Health.CurrentP2=100;Camp.Health.MaxP2=200;FLDObservedStatus Returning;Returning.StatusId=uint8(ELDDemoStatusId::CampReturning);Camp.Statuses.Add(Returning);
    const FString CampShown=LDHUD::ObservedUnit(Camp,TEXT("Ignored"),&Content,0.);
    TestEqualSensitive(TEXT("Camp card renders exactly name, HP and public status"),CampShown,HudName(TEXT("monster_guardian"))+TEXT("\n")+LDTerms::Health+TEXT(" 1/2\n状态：")+HudName(TEXT("camp_returning")));
    TestFalse(TEXT("Camp card has no level, mana, equipment or a contender name"),Has(CampShown,TEXT("等级"))||Has(CampShown,LDTerms::Mana)||Has(CampShown,TEXT("装备"))||Has(CampShown,TEXT("Ignored")));
    const LDHUD::FObservedCard CampCard=LDHUD::ObservedCard(Camp,FString(),&Content,0.);
    TestTrue(TEXT("Card structure follows the authorized field mask"),!CampCard.bMana&&!CampCard.bEquipment&&CampCard.Health.bKnown);
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
    TestFalse(TEXT("Death shows no gold or growth loss"),Waiting.Contains(LDTerms::Gold)||Waiting.Contains(TEXT("损失")));
    TestEqualSensitive(TEXT("The respawn countdown reads zero only once the authority clock reaches it"),LDHUD::LifeStatus(Own,true,12000),FString(LDTerms::Dead)+TEXT(" · ")+LDTerms::RespawnWait+TEXT(" 0.0s"));
    TestEqualSensitive(TEXT("Respawn time without the matching clock is unavailable"),LDHUD::LifeStatus(Own,false,0),FString(LDTerms::Dead)+TEXT(" · ")+LDTerms::RespawnWait+TEXT("（时间同步中）"));
    const LDHUD::FDeathView Death=LDHUD::Death(Own,true,2350);
    TestTrue(TEXT("The death banner names the state and the countdown and keeps the build"),Death.bVisible&&Death.Title==LDTerms::DeathTitle&&Death.Countdown==FString(LDTerms::RespawnWait)+TEXT(" 9.7s")&&Death.Note==LDTerms::DeathNote());
    TestEqualSensitive(TEXT("The banner never invents a respawn time"),LDHUD::Death(Own,false,0).Countdown,FString(LDTerms::RespawnWait)+TEXT(" · 时间同步中"));
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
    TestEqualSensitive(TEXT("Gold uses the approved player term"),LDHUD::Gold(150),FString(LDTerms::Gold)+TEXT(" 150"));
    TestEqualSensitive(TEXT("Zero remainder reads zero"),LDHUD::TenthsUp(0),FString(TEXT("0.0s")));
    TestEqualSensitive(TEXT("Any live remainder reads at least a tenth"),LDHUD::TenthsUp(1),FString(TEXT("0.1s")));
    TestEqualSensitive(TEXT("Remainders round up to the next tenth"),LDHUD::TenthsUp(101),FString(TEXT("0.2s")));
    TestTrue(TEXT("Development names the closed capture point"),LDHUD::PhaseLine(ELDMatchPhase::Development).Contains(LDTerms::CapturePoint));
    TestTrue(TEXT("An authority fault is stated explicitly"),LDHUD::PhaseLine(ELDMatchPhase::Faulted).Contains(TEXT("故障")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDActionViewTest,"Lockdown.Rules.HUDActionView",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDActionViewTest::RunTest(const FString& Parameters)
{
    LDHUD::FActionContext Context;Context.KeyLabel=TEXT("Q");Context.bOwnContext=true;Context.bPlaying=true;Context.bAlive=true;Context.bClock=true;Context.NowMs=4800;Context.bManaKnown=true;Context.ManaP2=10000;
    LDHUD::FActionInput Bolt;Bolt.Slot=LDHUD::EActionSlot::Q;Bolt.bProjected=true;Bolt.bHasSource=true;Bolt.Name=TEXT("技能甲");Bolt.Rank=2;Bolt.ManaCostP2=5000;
    LDHUD::FActionView View=LDHUD::Action(Bolt,Context);
    TestTrue(TEXT("A ready learned active can be sent and shows its rank"),View.bCanSubmitIntent&&View.ExecutionBlocks==0&&View.State==TEXT("就绪")&&View.Name==TEXT("技能甲 2级")&&View.Kind==TEXT("角色主动"));
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
    Empty.Slot=LDHUD::EActionSlot::Equipment1;View=LDHUD::Action(Empty,Context);
    TestTrue(TEXT("An empty equipment slot says so and sends nothing"),View.Name==TEXT("无装备")&&View.Kind==TEXT("装备")&&!View.bCanSubmitIntent);
    LDHUD::FActionInput Plate;Plate.Slot=LDHUD::EActionSlot::Equipment2;Plate.bProjected=true;Plate.bHasSource=true;Plate.Name=TEXT("装备甲");
    Plate.ReadyAtMs=40000;Plate.CooldownMs=40000;Plate.ManaCostP2=20000;View=LDHUD::Action(Plate,Context);
    TestTrue(TEXT("Held equipment is shown as worn, never as a skill to send"),View.State==TEXT("已装备")&&View.ExecutionBlocks==LDHUD::ActionPassive
        &&View.Name==TEXT("装备甲")&&!View.bCanSubmitIntent&&View.CooldownRemainingMs==0&&View.Cost.IsEmpty());
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
    FLDPublicMatchView Public;Public.Generation=4;Public.Phase=ELDMatchPhase::Countdown;Public.StartedAtMs=50000;
    TestEqualSensitive(TEXT("The opening countdown reads 0:00: the match time starts at T=0"),LDHUD::MatchTimer(Public,true,46000),FString(TEXT("0:00")));
    Public.Phase=ELDMatchPhase::Development;
    TestEqualSensitive(TEXT("T=0 reads 0:00"),LDHUD::MatchTimer(Public,true,50000),FString(TEXT("0:00")));
    TestEqualSensitive(TEXT("Elapsed seconds round down"),LDHUD::MatchTimer(Public,true,50999),FString(TEXT("0:00")));
    TestEqualSensitive(TEXT("Minutes carry no leading zero"),LDHUD::MatchTimer(Public,true,62000),FString(TEXT("0:12")));
    TestEqualSensitive(TEXT("Seconds keep two digits"),LDHUD::MatchTimer(Public,true,115000),FString(TEXT("1:05")));
    TestEqualSensitive(TEXT("Ten minutes and more show their own digits"),LDHUD::MatchTimer(Public,true,50000+725000),FString(TEXT("12:05")));
    TestTrue(TEXT("Without the authority clock no time is guessed"),LDHUD::MatchTimer(Public,false,62000).IsEmpty());
    Public.StartedAtMs=0;
    TestTrue(TEXT("Without a published T=0 no time is shown"),LDHUD::MatchTimer(Public,true,62000).IsEmpty());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDPublicMatchTest,"Lockdown.Rules.HUDPublicMatch",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDPublicMatchTest::RunTest(const FString& Parameters)
{
    FLDPublicMatchView Public;Public.Generation=4;Public.Phase=ELDMatchPhase::Development;Public.OpenAtMs=120000;
    TestEqualSensitive(TEXT("Development counts down to the public opening in whole seconds rounded up"),LDHUD::Circle(Public,FString(),FString(),true,500).Line,FString(LDTerms::Development)+TEXT(" · ")+LDTerms::CapturePoint+TEXT(" 2:00 后开放"));
    TestEqualSensitive(TEXT("A running countdown never reads 0:00 before the opening"),LDHUD::Clock(1),FString(TEXT("0:01")));
    TestEqualSensitive(TEXT("Without the authority clock the opening time is not guessed"),LDHUD::Circle(Public,FString(),FString(),false,0).Line,FString(LDTerms::Development)+TEXT(" · ")+LDTerms::CapturePoint+TEXT("尚未开放"));
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
    TestEqualSensitive(TEXT("A contender kill names both public roster names"),LDHUD::KillLine(Notice,TEXT("乙"),TEXT("甲"),LDTerms::CampLow,LDTerms::CampHigh,LDTerms::Boss),LDTerms::KillLine(TEXT("甲"),TEXT("乙")));
    Notice.Cause=ELDKillCause::CampHigh;
    TestEqualSensitive(TEXT("A camp death names the tier creature, not a hidden attacker"),LDHUD::KillLine(Notice,TEXT("乙"),TEXT("甲"),LDTerms::CampLow,LDTerms::CampHigh,LDTerms::Boss),LDTerms::KilledByLine(TEXT("乙"),LDTerms::CampHigh));
    Notice.Cause=ELDKillCause::Unowned;
    TestEqualSensitive(TEXT("A death without a contender owner says so"),LDHUD::KillLine(Notice,TEXT("乙"),FString(),LDTerms::CampLow,LDTerms::CampHigh,LDTerms::Boss),FString(TEXT("乙 "))+LDTerms::Dead+TEXT("（无参赛者击杀）"));
    Notice.Cause=ELDKillCause::Boss;
    TestEqualSensitive(TEXT("A Boss death names the 画饼老板"),LDHUD::KillLine(Notice,TEXT("乙"),TEXT("甲"),LDTerms::CampLow,LDTerms::CampHigh,LDTerms::Boss),
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
    // D26: the public airdrop line.
    FLDAirdropView Drop;
    TestTrue(TEXT("No drop shows no airdrop line"),LDHUD::AirdropLine(Drop,true,0).IsEmpty());
    Drop.Serial=1;Drop.Phase=ELDAirdropPhase::Announced;Drop.PointId=TEXT("airdrop_s");Drop.LandAtMs=45000;
    TestEqualSensitive(TEXT("An announced drop counts down to its landing"),LDHUD::AirdropLine(Drop,true,30001),
        LDTerms::AirdropCountdown(TEXT("0:15")));
    TestTrue(TEXT("The countdown names the 天降外卖"),LDHUD::AirdropLine(Drop,true,30001).StartsWith(LDTerms::Airdrop));
    TestEqualSensitive(TEXT("Without the authority clock the landing time is not guessed"),LDHUD::AirdropLine(Drop,false,0),
        LDTerms::AirdropSoon());
    Drop.Phase=ELDAirdropPhase::Landed;
    TestEqualSensitive(TEXT("A landed drop is first come, first served"),LDHUD::AirdropLine(Drop,true,46000),
        FString(LDTerms::AirdropLanded));
    Drop.Phase=ELDAirdropPhase::Claimed;
    TestTrue(TEXT("A claimed drop is an event of the feed, not a standing line"),LDHUD::AirdropLine(Drop,true,48000).IsEmpty());
    const FGuid Me=FGuid::NewGuid();
    // D26: the Boss line counts down shortly before it appears; the last hit is named by the event feed.
    FLDBossView Dean;
    TestTrue(TEXT("No Boss shows no Boss line"),LDHUD::BossLine(Dean,LDTerms::Boss,true,0).IsEmpty());
    Dean.Phase=ELDBossPhase::Pending;Dean.AppearAtMs=61000;
    TestTrue(TEXT("Long before it appears there is no countdown"),LDHUD::BossLine(Dean,LDTerms::Boss,true,30000).IsEmpty());
    TestEqualSensitive(TEXT("Shortly before it appears the Boss counts down"),LDHUD::BossLine(Dean,LDTerms::Boss,true,51000),
        LDTerms::BossCountdown(LDTerms::Boss,TEXT("0:10")));
    TestTrue(TEXT("The countdown names the 画饼老板"),LDHUD::BossLine(Dean,FString(),true,51000).StartsWith(LDTerms::Boss));
    TestTrue(TEXT("Without the authority clock no countdown is guessed"),LDHUD::BossLine(Dean,LDTerms::Boss,false,51000).IsEmpty());
    Dean.Phase=ELDBossPhase::Down;Dean.KilledBy=Me;Dean.DownAtMs=90000;
    TestTrue(TEXT("A downed Boss leaves the standing line"),LDHUD::BossLine(Dean,LDTerms::Boss,true,91000).IsEmpty());
    TestEqualSensitive(TEXT("The last hit is told it ate the pie"),LDHUD::BossDownLine(Dean,Me,FString(),LDTerms::Boss),
        LDTerms::BossDownSelf(LDTerms::Boss));
    TestTrue(TEXT("The buff is called 吃到大饼"),LDHUD::BossDownLine(Dean,Me,FString(),LDTerms::Boss).Contains(LDTerms::BossBuff));
    TestEqualSensitive(TEXT("Others see the public roster name"),LDHUD::BossDownLine(Dean,FGuid::NewGuid(),TEXT("甲"),LDTerms::Boss),
        LDTerms::BossDownBy(TEXT("甲"),LDTerms::Boss));
    // D27: the 上热搜 line names the leader and the bounty; the leader is told directly.
    FLDLeaderView Named;
    TestTrue(TEXT("Nobody named shows no leader line"),LDHUD::LeaderLine(Named,Me,FString()).IsEmpty());
    Named.Leader=FGuid::NewGuid();Named.BountyGold=100;Named.RefreshedAtMs=20000;
    TestEqualSensitive(TEXT("Another contender's notice names the public roster name and the bounty"),LDHUD::LeaderLine(Named,Me,TEXT("甲")),
        LDTerms::LeaderNamed(TEXT("甲"),100));
    TestTrue(TEXT("The notice is called 上热搜 and pays in 米"),LDHUD::LeaderLine(Named,Me,TEXT("甲")).StartsWith(LDTerms::Leader)
        &&LDHUD::LeaderLine(Named,Me,TEXT("甲")).Contains(LDTerms::Gold));
    Named.Leader=Me;
    TestEqualSensitive(TEXT("The leader is told its place is public"),LDHUD::LeaderLine(Named,Me,TEXT("我")),LDTerms::LeaderSelf(100));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDCardTextTest,"Lockdown.Rules.HUDCardText",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDCardTextTest::RunTest(const FString& Parameters)
{
    // #147 card templates: every number reads a typed field of the rank row the owner uses at its level.
    FLDDemoEffectDefinition Root;Root.Opcode=uint8(ELDDemoEffectOp::Root);Root.DurationMs=1000;
    FLDDemoEffectDefinition Damage;Damage.Opcode=uint8(ELDDemoEffectOp::Damage);Damage.BaseP2=6000;Damage.ADRatioBp=5000;
    FLDDemoEffectDefinition Slow;Slow.Opcode=uint8(ELDDemoEffectOp::Slow);Slow.RatioBp=2500;Slow.DurationMs=1500;
    FLDDemoEffectDefinition Timed;Timed.Opcode=uint8(ELDDemoEffectOp::TimedStatBonus);Timed.DurationMs=3000;
    Timed.Stats={{ELDDemoBonusStat::ArmorFlat,1200,{}},{ELDDemoBonusStat::AttackSpeedBonusRatio,5000,{}}};
    FLDDemoRankDefinition Basic;Basic.Rank=1;Basic.Effects={Root,Damage,Slow,Timed};
    FLDDemoRankDefinition Advanced=Basic;Advanced.Rank=2;Advanced.Effects[0].DurationMs=1250;Advanced.Effects[1].BaseP2=9000;
    LDHUD::FCardStats L1;L1.ADP2=7000;
    const LDHUD::FCardText Face=LDHUD::CardText(TEXT("定身目标 [root.duration] 秒，造成 [damage.amount] 物理伤害。"),Basic,nullptr,L1,1000);
    TestEqualSensitive(TEXT("Level 1 numbers: base plus the coefficient times the owner's AD"),Face.Plain,FString(TEXT("定身目标 1 秒，造成 95 物理伤害。")));
    TestEqualSensitive(TEXT("Keywords are bold"),Face.Rich,FString(TEXT("<b>定身</>目标 1 秒，造成 95 物理伤害。")));
    TestTrue(TEXT("The keywords used are listed in glossary order"),Face.bComplete&&Face.Keywords==TArray<FString>{TEXT("定身")});
    TestEqual(TEXT("The read length leaves out spaces"),LDHUD::CardLength(Face.Plain),16);
    LDHUD::FCardStats L5=L1;L5.Level=5;
    TestEqualSensitive(TEXT("Level 5 grows the base (60 x 1.4 + 35) but not the duration"),
        LDHUD::CardText(TEXT("定身目标 [root.duration] 秒，造成 [damage.amount] 物理伤害。"),Basic,nullptr,L5,1000).Plain,FString(TEXT("定身目标 1 秒，造成 119 物理伤害。")));
    TestEqualSensitive(TEXT("An upgrade card shows each changed value as A → B"),
        LDHUD::CardText(TEXT("定身目标 [root.duration] 秒，造成 [damage.amount] 物理伤害，减速 [slow.ratio]%。"),Advanced,&Basic,L1,1000).Plain,
        FString(TEXT("定身目标 1 → 1.25 秒，造成 95 → 125 物理伤害，减速 25%。")));
    TestEqualSensitive(TEXT("Timed stats: points grow, ratios do not"),
        LDHUD::CardText(TEXT("护甲 +[timed_stat_bonus.ArmorFlat]，攻速 +[timed_stat_bonus.AttackSpeedBonusRatio]%。"),Basic,nullptr,L5,1000).Plain,
        FString(TEXT("护甲 +17，攻速 +50%。")));
    // passive_iced_americano: MPRegenFlat (like HPRegenFlat and AttackRangeCmFlat) is an exact fraction in Cm, not Amount.
    FLDDemoEffectDefinition Iced;Iced.Opcode=uint8(ELDDemoEffectOp::StatBonus);
    FLDDemoStatBonus Regen;Regen.Stat=ELDDemoBonusStat::MPRegenFlat;Regen.Cm.Numerator=2;Regen.Cm.Denominator=1;
    Iced.Stats={{ELDDemoBonusStat::CooldownHasteFlat,2000,{}},Regen};
    FLDDemoRankDefinition IcedBasic;IcedBasic.Rank=1;IcedBasic.Effects={Iced};
    FLDDemoRankDefinition IcedAdvanced=IcedBasic;IcedAdvanced.Rank=2;IcedAdvanced.Effects[0].Stats[0].Amount=3000;
    const FString IcedTemplate=TEXT("技能急速 +[stat_bonus.CooldownHasteFlat]，法力回复 +[stat_bonus.MPRegenFlat]/秒。");
    const LDHUD::FCardText IcedFace=LDHUD::CardText(IcedTemplate,IcedBasic,nullptr,L5,1000);
    TestEqualSensitive(TEXT("Mana regeneration reads its Cm fraction (+2/秒), never +0"),IcedFace.Plain,FString(TEXT("技能急速 +20，法力回复 +2/秒。")));
    TestTrue(TEXT("and the card lists each placeholder's value"),IcedFace.Values==TArray<FString>({TEXT("20"),TEXT("2")}));
    TestEqualSensitive(TEXT("Its upgrade card shows the haste change and the unchanged regeneration once"),
        LDHUD::CardText(IcedTemplate,IcedAdvanced,&IcedBasic,L1,1000).Plain,FString(TEXT("技能急速 +20 → 30，法力回复 +2/秒。")));
    // The attributes panel's learned passive: the sentence at its rank, plus the advanced line once advanced.
    FLDDemoAbilityDefinition IcedSkill;IcedSkill.Id=TEXT("hud_rules_fixture_iced");IcedSkill.Kind=ELDDemoAbilityKind::Passive;IcedSkill.Ranks={IcedBasic,IcedAdvanced};
    TestTrue(TEXT("A basic learned passive lists its sentence only"),
        LDHUD::LearnedCardLines(IcedTemplate,TEXT("普攻命中也减冷却。"),IcedSkill,1,L1,1000)==TArray<FString>({TEXT("技能急速 +20，法力回复 +2/秒。")}));
    TestTrue(TEXT("An advanced learned passive also lists its advanced change"),
        LDHUD::LearnedCardLines(IcedTemplate,TEXT("普攻命中也减冷却。"),IcedSkill,2,L1,1000)
            ==TArray<FString>({TEXT("技能急速 +30，法力回复 +2/秒。"),TEXT("高级：普攻命中也减冷却。")}));
    TestTrue(TEXT("A rank the skill does not have lists nothing"),LDHUD::LearnedCardLines(IcedTemplate,TEXT(""),IcedSkill,3,L1,1000).IsEmpty());
    const LDHUD::FCardText Both=LDHUD::CardText(TEXT("被减速或被控制的敌人。"),Basic,nullptr,L1,1000);
    TestTrue(TEXT("Slow and control are separate keywords"),Both.Keywords==TArray<FString>({TEXT("减速"),TEXT("控制")})
        &&Both.Rich==TEXT("被<b>减速</>或被<b>控制</>的敌人。"));
    const LDHUD::FCardText Absent=LDHUD::CardText(TEXT("造成 [stun.duration] 秒。"),Basic,nullptr,L1,1000);
    TestTrue(TEXT("A placeholder of an absent effect is never shown as a number"),!Absent.bComplete&&Absent.Plain.Contains(TEXT("?")));
    TestFalse(TEXT("A broken template is refused"),LDHUD::CardText(TEXT("造成 [damage] 伤害。"),Basic,nullptr,L1,1000).bComplete);
    TArray<LDDemoCardText::FToken> Tokens;FString Error;
    TestFalse(TEXT("An unknown op is refused"),LDDemoCardText::Parse(TEXT("[teleport.amount]"),Tokens,Error));
    TestFalse(TEXT("A stray bracket is refused"),LDDemoCardText::Parse(TEXT("造成 ] 伤害"),Tokens,Error));
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
    FLDDemoRankDefinition Rank;Rank.Rank=1;Rank.ManaCostP2=4500;Rank.CooldownMs=6400;Rank.Effects.Add(Damage);
    TestEqualSensitive(TEXT("An active's cost line carries mana and cooldown"),LDHUD::CostLine(Rank),FString(LDTerms::Mana)+TEXT(" 45 · 冷却 6.4s"));
    FLDDemoContentDefinitions Content;FLDDemoAbilityDefinition Bolt;Bolt.Id=TEXT("hud_rules_fixture_bolt");Bolt.Kind=ELDDemoAbilityKind::Active;
    Bolt.Target.Mode=ELDDemoTargetMode::DirectionProjectile;Bolt.Target.RangeCm.Numerator=650;Bolt.Target.RangeCm.Denominator=1;
    FLDDemoRankDefinition Second=Rank;Second.Rank=2;Second.Effects[0].BaseP2=15000;Bolt.Ranks={Rank,Second};Content.Abilities.Add(Bolt);
    Content.Parameters.SkillGrowthPerLevelBp=1000;
    FLDProgressionView Progression;
    for(uint8 Slot=0;Slot<2;++Slot){FLDLearnedSkillView Skill;Skill.DefinitionId=Slot?FName(TEXT("active_not_mad")):FName(TEXT("active_pants_down"));Skill.bActive=true;Skill.Rank=1;Skill.Slot=Slot;Progression.Skills.Add(Skill);}
    FLDChoiceOption Learn;Learn.Kind=ELDChoiceOptionKind::LearnActive;Learn.DefinitionId=TEXT("hud_rules_fixture_bolt");Learn.ToRank=1;
    LDHUD::FCardStats Stats;
    AddExpectedMessage(TEXT("LD_HUD_DISPLAY_TEXT_MISSING hud_rules_fixture_bolt"),ELogVerbosity::Error,EAutomationExpectedMessageFlags::Contains,0,false);
    const LDHUD::FChoiceCardView Missing=LDHUD::ChoiceCard(Learn,Progression,&Content,Stats);
    TestTrue(TEXT("A skill without its display row says so instead of inventing a face"),Missing.SentencePlain==TEXT("内容目录中找不到该技能"));
    FLDDemoDisplayDefinition BoltDisplay;BoltDisplay.Id=TEXT("hud_rules_fixture_bolt");BoltDisplay.IconId=TEXT("icon_hud_rules_fixture_bolt");
    BoltDisplay.NameText=TEXT("测试飞弹");BoltDisplay.DescriptionText=TEXT("命中造成 [damage.amount] 魔法伤害。");BoltDisplay.AdvancedText=TEXT("穿透。");
    Content.Display.Add(BoltDisplay);
    const LDHUD::FChoiceCardView Card=LDHUD::ChoiceCard(Learn,Progression,&Content,Stats);
    TestTrue(TEXT("Learning a third active with Q/W full requires an explicit replacement"),Card.bNeedsReplacement&&Card.Kind==TEXT("新主动")&&Card.RankText==TEXT("初级"));
    TestTrue(TEXT("The card face is the sentence with this level's numbers and the advanced line"),
        Card.SentencePlain==TEXT("命中造成 100 魔法伤害。")&&Card.AdvancedPlain==TEXT("高级：穿透。"));
    TestTrue(TEXT("The details show the target form and the cost"),Card.Lines.Num()==2&&Card.Lines[0]==TEXT("方向弹道 · 射程 650cm")&&Card.Lines[1]==LDHUD::CostLine(Rank));
    TestTrue(TEXT("The compact card line is short and leaves the kind and the replacement to the UI"),Card.Summary==TEXT("新")&&Card.Detail==TEXT("新主动 · 初级"));
    TestTrue(TEXT("An active card draws the active placeholder icon with the name's first character"),
        Card.Icon==LDHUD::EChoiceIcon::Active&&Card.Glyph==Card.Name.Left(1)&&Card.IconId==TEXT("icon_hud_rules_fixture_bolt"));
    FLDChoiceOption Upgrade=Learn;Upgrade.Kind=ELDChoiceOptionKind::UpgradeActive;Upgrade.FromRank=1;Upgrade.ToRank=2;
    const LDHUD::FChoiceCardView Up=LDHUD::ChoiceCard(Upgrade,Progression,&Content,Stats);
    TestTrue(TEXT("An upgrade states both ranks, shows A → B and never needs a replacement"),!Up.bNeedsReplacement&&Up.RankText==TEXT("初级 → 高级")
        &&Up.SentencePlain==TEXT("命中造成 100 → 150 魔法伤害。")&&Up.Summary==TEXT("初→高"));
    FLDChoiceOption Passive=Learn;Passive.Kind=ELDChoiceOptionKind::LearnPassive;
    FLDChoiceOption Attribute=Learn;Attribute.Kind=ELDChoiceOptionKind::Attribute;
    TestTrue(TEXT("Passive and attribute cards draw their own placeholder families"),LDHUD::ChoiceCard(Passive,Progression,&Content,Stats).Icon==LDHUD::EChoiceIcon::Passive
        &&LDHUD::ChoiceCard(Attribute,Progression,&Content,Stats).Icon==LDHUD::EChoiceIcon::Attribute&&LDHUD::ChoiceCard(Attribute,Progression,&Content,Stats).Summary==TEXT("已选 0 次"));
    TestEqualSensitive(TEXT("The card carries the display table's IconId"),LDHUD::ChoiceCard(Upgrade,Progression,&Content,Stats).IconId,FString(TEXT("icon_hud_rules_fixture_bolt")));
    FLDCombatAttributeRowView Crit;Crit.Attribute=uint8(ELDProjectedAttribute::CritChance);Crit.Final.Numerator=1;Crit.Final.Denominator=5;Crit.Base.Denominator=1;
    Crit.Level.Denominator=1;Crit.Choices.Denominator=1;Crit.Passives.Denominator=1;Crit.Equipment.Denominator=1;
    const LDHUD::FAttributeRowView Row=LDHUD::AttributeRow(Crit);
    TestTrue(TEXT("Attribute rows use the approved name and show ratios as percent"),Row.Name==LDTerms::CritChance()&&Row.Final==TEXT("20%"));
    TestEqualSensitive(TEXT("Shop failures are explained in the approved terms"),LDHUD::GrowthReasonText(ELDGrowthReason::InsufficientGold),LDTerms::NotEnoughGold());
    TestEqualSensitive(TEXT("A full equipment bar says to sell first"),LDHUD::GrowthReasonText(ELDGrowthReason::EquipmentFull),FString(TEXT("装备槽已满，请先卖出一件")));
    TestEqualSensitive(TEXT("Combat rejections are explained"),LDHUD::CombatRejectText(ELDCombatReject::OutOfRange),FString(TEXT("超出射程")));
    FLDCombatEntryView Entry;Entry.DefinitionId=NAME_None;Entry.Unavailable=ELDCombatReject::SourceUnavailable;
    const LDHUD::FActionInput Empty=LDHUD::ActionInput(Entry,LDHUD::EActionSlot::Q,&Content);
    TestTrue(TEXT("An empty combat entry is an explicit empty source with the server reason kept for display"),Empty.bProjected&&!Empty.bHasSource&&Empty.ServerBlock==TEXT("没有可用来源"));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDSelfIconsTest,"Lockdown.Rules.HUDSelfIcons",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDSelfIconsTest::RunTest(const FString& Parameters)
{
    TestEqualSensitive(TEXT("A Latin name's icon is its upper-cased first letter"),LDHUD::IconGlyph(TEXT(" bolt")),FString(TEXT("B")));
    TestEqualSensitive(TEXT("A Chinese name's icon is its first character"),LDHUD::IconGlyph(TEXT("眩晕")),FString(TEXT("眩")));
    TestEqualSensitive(TEXT("An empty name is never a blank square"),LDHUD::IconGlyph(TEXT("  ")),FString(TEXT("?")));
    // More statuses and passives than the 6 x 2 grid: the first SelfIconMax show, the rest are counted for "…".
    if(!HudRegisterFixtureTable(*this)){return false;}
    ON_SCOPE_EXIT{FStringTableRegistry::Get().UnregisterStringTable(HudFixtureTable);};
    const FLDDemoContentDefinitions Content=HudFixtureContent();
    FLDProgressionView Progression;FLDCombatOwnerView Combat;
    FLDCombatStatusView Root;Root.StatusId=uint8(ELDDemoStatusId::Root);Root.ExpiresAtMs=5000;Root.ControlBlocks=uint8(ELDCombatControlBlock::Move);
    Combat.Statuses={Root};
    FLDLearnedSkillView Rhythm;Rhythm.DefinitionId=TEXT("passive_iced_americano");Rhythm.Rank=1;
    Progression.Skills.Init(Rhythm,LDHUD::SelfIconMax+2);
    const LDHUD::FSelfIconsView Full=LDHUD::SelfIcons(&Progression,&Combat,true,0,&Content);
    TestTrue(TEXT("A full grid keeps 12 squares and counts the rest"),LDHUD::SelfIconMax==12&&Full.Items.Num()==LDHUD::SelfIconMax&&Full.Overflow==3);
    TestTrue(TEXT("A full grid never drops the temporary status"),Full.Items[0].Kind==LDHUD::ESelfIconKind::Control);
    Progression.Skills.SetNum(LDHUD::SelfIconMax-1);
    TestEqual(TEXT("Exactly 12 items need no \"…\""),LDHUD::SelfIcons(&Progression,&Combat,true,0,&Content).Overflow,0);
    // The hover popup names the square and its kind; the description and effect lines come from the content catalog.
    const LDHUD::FActionDetailView RootDetail=LDHUD::SelfIconDetail(Full.Items[0],LDHUD::FCardStats(),nullptr);
    TestTrue(TEXT("A control status popup heads with its line, kind and tone"),RootDetail.Name==Full.Items[0].Tip&&RootDetail.Summary==TEXT("控制状态 · 负面"));
    // Status tone: what helps the bearer is positive, every control and affliction negative, an unknown id negative.
    for(const ELDDemoStatusId Id:{ELDDemoStatusId::Shield,ELDDemoStatusId::StatBoost,ELDDemoStatusId::Empowered,ELDDemoStatusId::RespawnProtection,ELDDemoStatusId::CampReturning})
    {TestTrue(FString::Printf(TEXT("Status %d is positive"),int32(Id)),LDHUD::StatusIsPositive(uint8(Id)));}
    for(const ELDDemoStatusId Id:{ELDDemoStatusId::Root,ELDDemoStatusId::Slow,ELDDemoStatusId::Exposed,ELDDemoStatusId::Taunt,ELDDemoStatusId::Disarm,
        ELDDemoStatusId::Stun,ELDDemoStatusId::Knockback,ELDDemoStatusId::Silence,ELDDemoStatusId::Burn})
    {TestFalse(FString::Printf(TEXT("Status %d is negative"),int32(Id)),LDHUD::StatusIsPositive(uint8(Id)));}
    TestFalse(TEXT("An unknown status reads as negative"),LDHUD::StatusIsPositive(31));
    TestTrue(TEXT("Learned passives are positive"),Full.Items[1].bPositive&&!Full.Items[0].bPositive);
    TestEqualSensitive(TEXT("A learned passive popup says 角色被动"),LDHUD::SelfIconDetail(Full.Items[1],LDHUD::FCardStats(),nullptr).Summary,FString(TEXT("角色被动")));
    LDHUD::FSelfIconView Gear;Gear.Source=LDHUD::ESelfIconSource::Equipment;Gear.Tip=TEXT("铁板");Gear.bUpgraded=true;
    TestEqualSensitive(TEXT("An upgraded equipment passive says so"),LDHUD::SelfIconDetail(Gear,LDHUD::FCardStats(),nullptr).Summary,FString(TEXT("装备被动（已进阶）")));
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
    FLDProgressionView Progression;
    auto Tips=[](const LDHUD::FSelfIconsView& View){TArray<FString> Out;for(const LDHUD::FSelfIconView& Icon:View.Items){Out.Add(Icon.Tip);}return FString::Join(Out,TEXT(" | "));};
    LDHUD::FSelfIconsView Icons=LDHUD::SelfIcons(&Progression,&Combat,true,10000,&Content);
    TestEqualSensitive(TEXT("Control first; time and shield round up; passive auras and protection stay off the grid"),
        Tips(Icons),HudName(TEXT("root"))+TEXT(" 1.3s | ")+HudName(TEXT("shield"))+TEXT(" 81 2.0s"));
    TestTrue(TEXT("Timed statuses carry their end instant and kind"),Icons.Items.Num()==2&&Icons.Items[0].Kind==LDHUD::ESelfIconKind::Control
        &&Icons.Items[0].ExpiresAtMs==11201&&Icons.Items[1].Kind==LDHUD::ESelfIconKind::Temporary&&Icons.Items[1].ExpiresAtMs==12000&&!Icons.bSyncing);
    TestTrue(TEXT("The root square is negative and the shield square positive"),!Icons.Items[0].bPositive&&Icons.Items[1].bPositive
        &&LDHUD::SelfIconDetail(Icons.Items[1],LDHUD::FCardStats(),nullptr).Summary==TEXT("临时状态 · 正面"));
    TestEqualSensitive(TEXT("The placeholder icon is the name's first character"),Icons.Items[0].Glyph,LDHUD::IconGlyph(HudName(TEXT("root"))));
    TestEqualSensitive(TEXT("Without the matching authority clock no remainder is shown"),Tips(LDHUD::SelfIcons(&Progression,&Combat,false,0,&Content)),
        HudName(TEXT("root"))+TEXT(" | ")+HudName(TEXT("shield"))+TEXT(" 81"));
    const LDHUD::FSelfIconsView Missing=LDHUD::SelfIcons(nullptr,nullptr,true,0,&Content);
    TestTrue(TEXT("Missing projections read as syncing, never as none"),Missing.bSyncing&&Missing.Items.IsEmpty());
    FLDLearnedSkillView Rhythm;Rhythm.DefinitionId=TEXT("passive_iced_americano");Rhythm.Rank=2;
    FLDLearnedSkillView Active;Active.DefinitionId=TEXT("active_not_mad");Active.bActive=true;Active.Rank=2;
    Progression.Skills={Rhythm,Active};
    Content.Equipment[0].HasPassive=true;Content.Equipment[0].Passive.Family=TEXT("passive_siphon");
    Combat.Statuses.Reset();Combat.EquipmentPassives={FName(TEXT("passive_siphon")),FName(TEXT("passive_siphon"))};
    Icons=LDHUD::SelfIcons(&Progression,&Combat,true,10000,&Content);
    TestEqualSensitive(TEXT("Learned passives keep their rank; an equipment passive appears once under its equipment"),
        Tips(Icons),HudName(TEXT("passive_iced_americano"))+TEXT("（")+LDHUD::SkillRankName(2)+TEXT("） | ")+HudName(TEXT("item_iron_plate")));
    TestTrue(TEXT("Passives carry no remaining time"),Icons.Items.Num()==2&&Icons.Items[0].Kind==LDHUD::ESelfIconKind::Passive&&Icons.Items[0].ExpiresAtMs==0);
    // D21 recoveries are temporary squares ahead of the passives while they heal, with no remaining-time ring.
    Combat.bOutOfCombatRegenActive=true;Combat.bDormRegenActive=true;
    Icons=LDHUD::SelfIcons(&Progression,&Combat,true,10000,&Content);
    TestEqualSensitive(TEXT("Active out-of-combat and dorm recoveries show before the passives"),Tips(Icons),
        FString(LDTerms::OutOfCombatRegen)+TEXT(" | ")+LDTerms::DormRegenActive()+TEXT(" | ")+HudName(TEXT("passive_iced_americano"))+TEXT("（")
        +LDHUD::SkillRankName(2)+TEXT("） | ")+HudName(TEXT("item_iron_plate")));
    TestTrue(TEXT("Recoveries are untimed temporary squares"),Icons.Items.Num()==4&&Icons.Items[0].Kind==LDHUD::ESelfIconKind::Temporary
        &&Icons.Items[0].ExpiresAtMs==0&&Icons.Items[1].Kind==LDHUD::ESelfIconKind::Temporary&&Icons.Items[1].ExpiresAtMs==0);
    TestTrue(TEXT("Recoveries and passives are all positive"),Icons.Items.FilterByPredicate([](const LDHUD::FSelfIconView& Icon){return Icon.bPositive;}).Num()==4);
    Content.Parameters.OutOfCombatRegenDelayMs=5000;Content.Parameters.OutOfCombatMaxHPRegenBp=150;Content.Parameters.OutOfCombatMaxMPRegenBp=150;
    Content.Parameters.DormRegenDelayMs=3000;Content.Parameters.DormMaxHPRegenBp=1000;Content.Parameters.DormMaxMPRegenBp=1000;
    const LDHUD::FActionDetailView Slow=LDHUD::SelfIconDetail(Icons.Items[0],LDHUD::FCardStats(),&Content);
    TestTrue(TEXT("The out-of-combat popup states the REGEN delay and share"),Slow.Name==LDTerms::OutOfCombatRegen&&Slow.Summary==TEXT("临时状态 · 正面")
        &&Slow.Body==TEXT("脱战 5s 后，每秒恢复 1.5% 生命上限和法力上限\n出手、释放伤害技能或受到伤害立即停止"));
    const LDHUD::FActionDetailView Dorm=LDHUD::SelfIconDetail(Icons.Items[1],LDHUD::FCardStats(),&Content);
    TestTrue(TEXT("The dorm popup states the DORM delay and share"),Dorm.Body.StartsWith(FString(TEXT("站在"))+LDTerms::Dorm+TEXT("内且脱战 3s 后，每秒恢复 10% 生命上限和法力上限")));
    Combat.bOutOfCombatRegenActive=false;Combat.bDormRegenActive=false;
    Progression.Skills={Active};Combat.EquipmentPassives.Reset();
    Icons=LDHUD::SelfIcons(&Progression,&Combat,true,10000,&Content);
    TestTrue(TEXT("Known projections without statuses or passives show an empty grid"),Icons.Items.IsEmpty()&&!Icons.bSyncing&&Icons.Overflow==0);
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
    LDHUD::FActionInput Bolt;Bolt.Slot=LDHUD::EActionSlot::Q;Bolt.bProjected=true;Bolt.bHasSource=true;Bolt.Name=TEXT("技能甲");Bolt.Rank=1;Bolt.ManaCostP2=3050;
    LDHUD::FActionView View=LDHUD::Action(Bolt,Context);
    LDHUD::EActionLook Shown=LDHUD::ActionLook(View,false);
    TestEqualSensitive(TEXT("The cost chip shows only the mana cost, rounded down like the shown mana"),View.Cost,FString(TEXT("30")));
    TestTrue(TEXT("A learned active shows its rank number and its name's first character as the icon"),View.Rank==TEXT("1")&&View.Glyph==TEXT("技"));
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
    TestTrue(TEXT("An empty slot looks empty even when armed and has no cost, rank or icon glyph"),Shown==LDHUD::EActionLook::Empty&&View.Cost.IsEmpty()&&View.Rank.IsEmpty()&&View.Glyph.IsEmpty()&&View.Name==TEXT("空槽"));
    LDHUD::FActionInput Plate;Plate.Slot=LDHUD::EActionSlot::Equipment1;Plate.bProjected=true;Plate.bHasSource=true;Plate.Name=TEXT("装备甲");Plate.ManaCostP2=2000;
    View=LDHUD::Action(Plate,Context);Shown=LDHUD::ActionLook(View,false);
    TestTrue(TEXT("A held item looks like a passive slot, its tooltip says it is worn and it shows no cost or rank"),Shown==LDHUD::EActionLook::Empty&&View.State==TEXT("已装备")&&View.Cost.IsEmpty()&&View.Rank.IsEmpty());
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
    auto CauseAt=[&Feed,&Lobby,&Self](uint32 Generation,int64 NotBeforeMs){return LDHUD::DeathCause(Feed,Generation,Self,NotBeforeMs,Lobby,LDTerms::CampLow,LDTerms::CampHigh,LDTerms::Boss);};
    AddNotice(1,Self,FGuid(),ELDKillCause::CampHigh,10000);
    AddNotice(2,Other,Rival,ELDKillCause::Contender,30000);
    AddNotice(3,Self,Rival,ELDKillCause::Contender,50000);
    TestEqualSensitive(TEXT("The newest own death names its killer from the public roster"),CauseAt(7,40000),LDTerms::KilledBy(TEXT("玩家甲")));
    TestTrue(TEXT("A feed of another generation is not this match"),CauseAt(8,0).IsEmpty()&&CauseAt(0,0).IsEmpty());
    TestTrue(TEXT("Before this life's notice arrives an older death never stands in"),CauseAt(7,60000).IsEmpty());
    TestTrue(TEXT("Without an own identity there is no cause"),LDHUD::DeathCause(Feed,7,FGuid(),0,Lobby,LDTerms::CampLow,LDTerms::CampHigh,LDTerms::Boss).IsEmpty());
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
        &&Note==LDTerms::DeathNote()&&Note.Contains(LDTerms::Gold));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDKillFeedSelfTest,"Lockdown.Rules.HUDKillFeedSelf",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDKillFeedSelfTest::RunTest(const FString& Parameters)
{
    const FGuid Self=FGuid::NewGuid(),Rival=FGuid::NewGuid(),Other=FGuid::NewGuid();
    auto Line=[](const FLDKillNotice& Source,const FGuid& Viewer,const TCHAR* VictimName,const TCHAR* KillerName)
    {return LDHUD::KillFeedLine(Source,Viewer,VictimName,KillerName,LDTerms::CampLow,LDTerms::CampHigh,LDTerms::Boss);};
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDShopPreorderTest,"Lockdown.Rules.HUDShopPreorder",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDShopPreorderTest::RunTest(const FString& Parameters)
{
    // The catalog lists every approved item at its approved prices, for browsing and pre-ordering away from the shop.
    FLDDemoContentDefinitions Content;
    {FLDDemoEquipmentDefinition Item;Item.Id=TEXT("item_a");Item.BuyPrice=300;Item.SellGold=150;Content.Equipment.Add(Item);}
    {FLDDemoEquipmentDefinition Item;Item.Id=TEXT("item_b");Item.BuyPrice=120;Item.SellGold=60;Content.Equipment.Add(Item);}
    const TArray<FLDShopItemView> Catalog=LDHUD::CatalogItems(&Content);
    TestTrue(TEXT("The catalog lists each approved item with its prices"),Catalog.Num()==2&&Catalog[0].DefinitionId==FName(TEXT("item_a"))&&Catalog[0].BuyPrice==300
        &&Catalog[0].SellGold==150&&Catalog[1].BuyPrice==120&&Catalog[1].SellGold==60);
    TestTrue(TEXT("Without content there is no catalog"),LDHUD::CatalogItems(nullptr).IsEmpty());
    const TArray<FName> Queue={FName(TEXT("item_a")),FName(TEXT("item_b"))};
    TestEqual(TEXT("Queue order first: the first pre-order the gold covers"),LDHUD::NextPreorder(Queue,Catalog,400,2),0);
    TestEqual(TEXT("An unaffordable first pre-order lets an affordable later one buy"),LDHUD::NextPreorder(Queue,Catalog,200,2),1);
    TestEqual(TEXT("Too little gold for either buys nothing"),LDHUD::NextPreorder(Queue,Catalog,100,2),int32(INDEX_NONE));
    TestEqual(TEXT("No empty equipment slot buys nothing"),LDHUD::NextPreorder(Queue,Catalog,1000,0),int32(INDEX_NONE));
    // The session quote decides the price: an item it does not list, or lists dearer, is not bought at the catalog price.
    TArray<FLDShopItemView> Quote=Catalog;Quote.RemoveAt(0);Quote[0].BuyPrice=250;
    TestEqual(TEXT("An item the quote does not list is never bought"),LDHUD::NextPreorder({FName(TEXT("item_a"))},Quote,1000,2),int32(INDEX_NONE));
    TestEqual(TEXT("The quote price, not the catalog price, must be covered"),LDHUD::NextPreorder({FName(TEXT("item_b"))},Quote,200,2),int32(INDEX_NONE));
    TestEqual(TEXT("An empty queue buys nothing"),LDHUD::NextPreorder(TArray<FName>(),Catalog,1000,2),int32(INDEX_NONE));
    // A held copy whose upgrade is offered is bought as that upgrade: at the upgrade price, without a free slot.
    const TArray<FName> Held={FName(TEXT("item_a"))};
    TestEqual(TEXT("An owned upgradable pre-order upgrades at the upgrade price without a free slot"),LDHUD::NextPreorder({FName(TEXT("item_a"))},Catalog,200,0,Held,150),0);
    TestEqual(TEXT("The upgrade price must be covered"),LDHUD::NextPreorder({FName(TEXT("item_a"))},Catalog,100,2,Held,150),int32(INDEX_NONE));
    TestEqual(TEXT("An owned upgradable item is never bought as a second copy"),LDHUD::NextPreorder({FName(TEXT("item_a"))},Catalog,1000,2,Held,5000),int32(INDEX_NONE));
    TestEqual(TEXT("Without an upgrade price the upgrade is never bought"),LDHUD::NextPreorder({FName(TEXT("item_a"))},Catalog,1000,2,Held,0),int32(INDEX_NONE));
    TestEqual(TEXT("A full inventory still lets a later pre-order upgrade"),LDHUD::NextPreorder(Queue,Catalog,1000,0,{FName(TEXT("item_b"))},150),1);
    TestEqual(TEXT("Two pre-orders at most"),LDHUD::MaxShopPreorders,2);
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
    Public.Leader.Leader=Rival;Public.Leader.BountyGold=100;
    FLDKillFeedView Kills;Kills.Generation=3;
    FLDKillNotice Old;Old.Serial=1;Old.Cause=ELDKillCause::Contender;Old.Victim=Rival;Old.Killer=Self;Old.OccurredAtMs=1000;Kills.Notices.Add(Old);
    LDHUD::FEventFeedInput In;In.Public=&Public;In.Kills=&Kills;In.Lobby=&Lobby;In.Self=Self;In.bClock=true;In.NowMs=20000;In.LocalSeconds=100;
    In.LowCampName=LDTerms::CampLow;In.HighCampName=LDTerms::CampHigh;In.BossName=LDTerms::Boss;
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
        LDTerms::LeaderSelf(100),LDTerms::GraffitiSprayed(TEXT("甲"))};
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
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDNextStepTest,"Lockdown.Rules.HUDNextStep",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDNextStepTest::RunTest(const FString& Parameters)
{
    if(!HudRegisterFixtureTable(*this)){return false;}
    ON_SCOPE_EXIT{FStringTableRegistry::Get().UnregisterStringTable(HudFixtureTable);};
    FLDDemoContentDefinitions Content=HudFixtureContent();
    for(FLDDemoEquipmentDefinition& Item:Content.Equipment){Item.BuyPrice=300;}
    const FGuid Self=FGuid::NewGuid();
    FLDPublicMatchView Public;Public.Generation=4;Public.Phase=ELDMatchPhase::Development;
    FLDProgressionView Growth;Growth.bValid=true;Growth.Generation=4;Growth.Level=1;
    FLDEquipmentView Wallet;Wallet.bValid=true;Wallet.Generation=4;Wallet.Gold=150;
    for(uint8 Index=0;Index<2;++Index){FLDEquipmentSlotView Slot;Slot.SlotIndex=Index;Wallet.Slots.Add(Slot);}
    auto Step=[&](){return LDHUD::NextStep(Public,Self,&Growth,&Wallet,&Content);};
    TestEqualSensitive(TEXT("A fresh contender is pointed at the camps"),Step(),FString::Printf(TEXT("下一步：去打附近的野怪搞%s、涨%s（右键攻击）"),LDTerms::Gold,LDTerms::Experience));
    Wallet.Gold=320;
    TestEqualSensitive(TEXT("Affordable gear for an empty slot names the approved item"),Step(),FString(TEXT("下一步："))+LDTerms::Gold+TEXT("够买「")+HudName(TEXT("item_iron_plate"))+TEXT("」了，去")+LDTerms::Shop);
    Wallet.Slots[0].bOccupied=true;Wallet.Slots[1].bOccupied=true;Growth.Level=3;
    TestTrue(TEXT("Full slots and a grown contender need no hint"),Step().IsEmpty());
    Growth.PendingChoiceCount=2;
    TestEqualSensitive(TEXT("A pending growth choice comes first"),Step(),FString::Printf(TEXT("下一步：%s ×2（点右下角的%s按钮）"),LDTerms::Choice,LDTerms::Choice));
    Growth.PendingChoiceCount=0;Public.Phase=ELDMatchPhase::CaptureOpen;
    TestEqualSensitive(TEXT("An open capture point is the next goal"),Step(),FString(TEXT("下一步："))+LDTerms::CaptureOpened+LDTerms::CaptureGo);
    Public.CircleState=ELDCircleState::Solo;Public.Occupier=Self;
    TestEqualSensitive(TEXT("The own spray is encouraged instead"),Step(),FString(TEXT("稳住：你"))+LDTerms::Holding);
    Public.CircleState=ELDCircleState::Guarded;Public.Occupier.Invalidate();Public.GraffitiOwner=Self;
    TestEqualSensitive(TEXT("The own graffiti asks to survive the guard"),Step(),FString(LDTerms::GuardOwn));
    Public.GraffitiOwner=FGuid::NewGuid();
    TestEqualSensitive(TEXT("A rival graffiti asks to besiege its owner or repaint"),LDHUD::NextStep(Public,Self,&Growth,&Wallet,&Content,TEXT("玩家乙")),LDTerms::Besiege(TEXT("玩家乙")));
    Public.GraffitiOwner.Invalidate();
    Public.Phase=ELDMatchPhase::Results;
    TestTrue(TEXT("Outside a running match there is no hint"),Step().IsEmpty());
    Public.Phase=ELDMatchPhase::Development;Growth.Generation=3;Wallet.Generation=3;Wallet.Slots[0].bOccupied=false;
    TestTrue(TEXT("Projections of another match never give a hint"),Step().IsEmpty());
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
        TestFalse(TEXT("A previewed card or a request in flight keeps the panel open"),Fold.ShouldFold(Wait+1.,true,true));
        TestFalse(TEXT("Engaging makes the automatic open the player's own: it no longer times out"),Fold.IsAutomatic()||Fold.ShouldFold(Wait+60.,false,false));
        TestTrue(TEXT("A hit still folds it once the player is no longer engaged"),Fold.ShouldFold(Wait+61.,false,true));
    }
    {
        LDHUD::FChoicesFold Fold;Fold.Opened(false,0.);
        TestFalse(TEXT("A panel the player opened never times out"),Fold.IsAutomatic()||Fold.ShouldFold(600.,false,false));
        TestTrue(TEXT("A hit on the own body folds the open panel"),Fold.ShouldFold(601.,false,true));
        Fold.Closed();Fold.Opened(false,602.);
        TestFalse(TEXT("The player's own reopening after a hit fold is respected while hits continue"),Fold.ShouldFold(603.,false,true));
        Fold.Closed();Fold.Opened(false,700.);
        TestTrue(TEXT("A later opening folds on a hit again"),Fold.ShouldFold(701.,false,true));
        Fold.Closed();Fold.Opened(true,800.);Fold.Closed();Fold.Opened(false,801.);
        TestTrue(TEXT("Only a hit fold, not a timed one, protects the next opening"),Fold.ShouldFold(802.,false,true));
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

    // Catalog rows: skills then attribute cards, with what the own progression holds.
    FLDDemoContentDefinitions Content=HudFixtureContent();
    FLDDemoAbilityDefinition Active;Active.Id=TEXT("root");Active.Kind=ELDDemoAbilityKind::Active;Content.Abilities.Add(Active);
    FLDDemoAbilityDefinition Passive;Passive.Id=TEXT("passive_iced_americano");Passive.Kind=ELDDemoAbilityKind::Passive;Content.Abilities.Add(Passive);
    FLDDemoAttributeDefinition Card;Card.Id=TEXT("shield");Content.Attributes.Add(Card);
    Content.Equipment[0].BuyPrice=300;
    FLDProgressionView Progression;Progression.bValid=true;Progression.Level=3;Progression.CumulativeXP=450;Progression.PendingChoiceCount=1;
    FLDLearnedSkillView Learned;Learned.DefinitionId=TEXT("root");Learned.bActive=true;Learned.Rank=2;Learned.Slot=0;Progression.Skills.Add(Learned);
    FLDAttributeCardView Cards;Cards.DefinitionId=TEXT("shield");Cards.Count=3;Progression.AttributeCards.Add(Cards);
    const TArray<LDDevPanel::FLearnRow> Rows=LDDevPanel::LearnRows(Content,&Progression);
    TestTrue(TEXT("Every skill and card of the catalog is listed in order with its kind"),Rows.Num()==3&&Rows[0].Name==HudName(TEXT("root"))&&Rows[0].bActiveSkill
        &&Rows[1].Kind==TEXT("被动技能")&&Rows[2].Kind==TEXT("属性卡"));
    TestTrue(TEXT("Held ranks and card counts come from the own progression"),LDDevPanel::HeldText(Rows[0])==TEXT("2 级")
        &&LDDevPanel::HeldText(Rows[1])==TEXT("未拥有")&&LDDevPanel::HeldText(Rows[2])==TEXT("已有 3 张"));
    TestFalse(TEXT("One held active does not need a replacement"),LDDevPanel::BothActiveSlotsHeld(&Progression));
    Learned.Slot=1;Learned.DefinitionId=TEXT("other_active");Progression.Skills.Add(Learned);
    TestTrue(TEXT("Q and W both held offer the confirmed replacement"),LDDevPanel::BothActiveSlotsHeld(&Progression));
    TestFalse(TEXT("An unprojected progression offers nothing"),LDDevPanel::BothActiveSlotsHeld(nullptr));
    const TArray<LDDevPanel::FItemRow> Items=LDDevPanel::ItemRows(Content);
    TestTrue(TEXT("Items list their catalog price"),Items.Num()==1&&Items[0].Name==HudName(TEXT("item_iron_plate"))&&Items[0].Price==300);

    // Own state lines: an unprojected view reads as syncing, never as 0.
    TestEqualSensitive(TEXT("Nothing projected is syncing"),LDDevPanel::StatusLine(nullptr,nullptr),FString(TEXT("等级 同步中 · "))+LDTerms::Gold+TEXT(" 同步中"));
    FLDEquipmentView Equipment;Equipment.bValid=true;Equipment.Gold=275;
    FLDEquipmentSlotView First;First.SlotIndex=0;First.bOccupied=true;First.DefinitionId=TEXT("item_iron_plate");Equipment.Slots.Add(First);
    FLDEquipmentSlotView Second;Second.SlotIndex=1;Equipment.Slots.Add(Second);
    TestEqualSensitive(TEXT("Level, XP, unused choices and gold"),LDDevPanel::StatusLine(&Progression,&Equipment),FString::Printf(TEXT("等级 3 · %s 450 · 未用选择机会 1 · %s 275"),LDTerms::Experience,LDTerms::Gold));
    TestTrue(TEXT("Slots show the held item or empty"),LDDevPanel::SlotLine(&Equipment,&Content,1)==TEXT("装备 1：")+HudName(TEXT("item_iron_plate"))
        &&LDDevPanel::SlotLine(&Equipment,&Content,2)==TEXT("装备 2：空")&&LDDevPanel::SlotLine(nullptr,&Content,1)==TEXT("装备 1：同步中"));
    TestEqualSensitive(TEXT("An attribute without a projected row is syncing"),LDDevPanel::AttributeLine(nullptr,uint8(ELDProjectedAttribute::AD)),FString(TEXT("同步中")));
    FLDCombatOwnerView Combat;FLDCombatAttributeRowView AD;AD.Attribute=uint8(ELDProjectedAttribute::AD);
    for(FLDDemoRational* Term:{&AD.Base,&AD.Level,&AD.Choices,&AD.Passives,&AD.Equipment,&AD.PercentBonus,&AD.Dev,&AD.DevPercent,&AD.Temporary,&AD.TemporaryPercent,&AD.Final})
    {Term->Numerator=0;Term->Denominator=1;}
    AD.Base.Numerator=50;AD.Dev.Numerator=12;AD.Final.Numerator=62;Combat.Attributes.Add(AD);
    const FString ADLine=LDDevPanel::AttributeLine(&Combat,uint8(ELDProjectedAttribute::AD));
    TestTrue(TEXT("The committed final value shows with the test layer among its sources"),ADLine.StartsWith(TEXT("当前 62"))&&ADLine.Contains(TEXT("测试")));

    // The authority's answer, never a guessed success.
    FLDDevTestResult Result;Result.Status=ELDAdmissionStatus::AdmissionRejected;Result.Reason=uint16(ELDGrowthReason::NotAuthority);
    TestTrue(TEXT("An admission refusal names its reason"),LDDevPanel::ResultLine(Result).StartsWith(TEXT("请求被拒绝（")));
    Result.Status=ELDAdmissionStatus::Final;Result.bHasReceipt=true;Result.Receipt.Op=ELDDevTestOp::GrantGold;Result.Receipt.Header.Outcome=ELDReceiptOutcome::Applied;
    TestEqualSensitive(TEXT("An applied receipt says so"),LDDevPanel::ResultLine(Result),FString(TEXT("加"))+LDTerms::Gold+TEXT(" 已生效"));
    Result.Receipt.Header.Outcome=ELDReceiptOutcome::Rejected;Result.Receipt.Header.Reason=uint16(ELDGrowthReason::EquipmentFull);
    TestTrue(TEXT("A refused receipt names the op and its reason"),LDDevPanel::ResultLine(Result).StartsWith(FString(TEXT("加"))+LDTerms::Gold+TEXT(" 被拒绝（")));
    Result.Status=ELDAdmissionStatus::AcceptedPending;Result.bHasReceipt=false;
    TestTrue(TEXT("An admitted command waits for its commit"),LDDevPanel::ResultLine(Result).StartsWith(TEXT("已受理")));
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
    // Every system term is filled, distinct from the others and never an entry name, so it cannot read as a skill, item or creature.
    const TCHAR* const Terms[]={LDTerms::Health,LDTerms::Mana,LDTerms::AttackDamage,LDTerms::AbilityPower,LDTerms::Armor,LDTerms::MagicResist,
        LDTerms::AttackSpeed,LDTerms::MoveSpeed,LDTerms::Crit,LDTerms::Haste,LDTerms::PhysicalDamage,LDTerms::MagicalDamage,LDTerms::Gold,
        LDTerms::Experience,LDTerms::BasicAttack,LDTerms::Dash,LDTerms::Shop,LDTerms::CapturePoint,LDTerms::CampLow,LDTerms::CampHigh,
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
