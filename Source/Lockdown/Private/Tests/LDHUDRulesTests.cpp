#include "UI/LDHUDModel.h"
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
#include <limits>
#if WITH_DEV_AUTOMATION_TESTS
namespace
{
// A private table id keeps the fixture away from the real DemoContent display table.
const FName HudFixtureTable(TEXT("LDHUDRulesDisplayFixture"));
FLDDemoContentDefinitions HudFixtureContent()
{
    FLDDemoContentDefinitions Content;
    for(const TCHAR* Id:{TEXT("root"),TEXT("shield"),TEXT("respawn_protection"),TEXT("camp_returning"),TEXT("monster_guardian"),TEXT("item_iron_plate"),TEXT("passive_battle_rhythm")})
    {FLDDemoDisplayDefinition Display;Display.Id=Id;Display.Namespace=HudFixtureTable.ToString();Display.NameKey=FString(Id)+TEXT(".name");Display.DescriptionKey=FString(Id)+TEXT(".description");Content.Display.Add(Display);}
    FLDDemoMonsterDefinition Monster;Monster.Id=TEXT("monster_guardian");Monster.NativeId=uint8(ELDDemoMonsterId::MonsterGuardian);Content.Monsters.Add(Monster);
    FLDDemoEquipmentDefinition Item;Item.Id=TEXT("item_iron_plate");Item.NativeId=uint8(ELDDemoEquipmentId::ItemIronPlate);Content.Equipment.Add(Item);
    return Content;
}
void HudRegisterFixtureTable()
{
    FStringTableRef Table=FStringTable::NewStringTable();Table->SetNamespace(HudFixtureTable.ToString());
    const TPair<const TCHAR*,const TCHAR*> Rows[]={{TEXT("root.name"),TEXT("定身")},{TEXT("respawn_protection.name"),TEXT("复活保护")},
        {TEXT("camp_returning.name"),TEXT("回岗中")},{TEXT("monster_guardian.name"),TEXT("体育老师")},
        {TEXT("item_iron_plate.name"),TEXT("《五年挨打三年模拟》")},{TEXT("shield.name"),TEXT("淡定")},
        {TEXT("passive_battle_rhythm.name"),TEXT("单身二十年的手速")}};
    for(const TPair<const TCHAR*,const TCHAR*>& Row:Rows)
    {
        // Development game targets also compile these tests; only editor builds have the dev-notes overload.
#if WITH_EDITORONLY_DATA
        Table->SetSourceString(Row.Key,Row.Value,FString());
#else
        Table->SetSourceString(Row.Key,Row.Value);
#endif
    }
    FStringTableRegistry::Get().RegisterStringTable(HudFixtureTable,Table);
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDMaskTest,"Lockdown.Rules.HUDObservationBoundary",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDMaskTest::RunTest(const FString& Parameters)
{
    HudRegisterFixtureTable();
    ON_SCOPE_EXIT{FStringTableRegistry::Get().UnregisterStringTable(HudFixtureTable);};
    const FLDDemoContentDefinitions Content=HudFixtureContent();
    FLDObservedUnitView Rival;Rival.Kind=ELDObservedUnitKind::Contender;Rival.AuthorizedFieldMask=LDObservation::ContenderFields;Rival.ContenderId=FGuid::NewGuid();Rival.Level=3;
    Rival.Health.CurrentP2=45001;Rival.Health.MaxP2=60000;Rival.Mana.CurrentP2=12099;Rival.Mana.MaxP2=20000;Rival.Equipment.Slots[0]=uint8(ELDDemoEquipmentId::ItemIronPlate);
    FLDObservedStatus Root;Root.StatusId=uint8(ELDDemoStatusId::Root);Root.ExpiresAtServerMs=12500;Rival.Statuses.Add(Root);
    FLDObservedStatus Protection;Protection.StatusId=uint8(ELDDemoStatusId::RespawnProtection);Protection.ExpiresAtServerMs=11500;Rival.Statuses.Add(Protection);
    const FString Shown=LDHUD::ObservedUnit(Rival,TEXT("同学甲"),&Content,10.);
    auto Has=[](const FString& Text,const TCHAR* Label){return Text.Contains(Label,ESearchCase::CaseSensitive);};
    TestEqualSensitive(TEXT("Observed card renders exactly the SC-07 whitelist with approved display names"),Shown,
        FString(TEXT("同学甲 · 等级 3\n面子 451/600 · 胆量 120/200\n状态：定身 2.5s · 复活保护 1.5s\n装备：《五年挨打三年模拟》 · 空槽")));
    for(const TCHAR* Private:{TEXT("零花钱"),TEXT("经验"),TEXT("冷却"),TEXT("技能"),TEXT("手劲"),TEXT("抗揍"),TEXT("属性"),TEXT("98765")})
    {TestFalse(FString::Printf(TEXT("Observed card has no private category: %s"),Private),Has(Shown,Private));}
    TestTrue(TEXT("A contender missing from the public roster is not given an invented name"),LDHUD::ObservedUnit(Rival,FString(),&Content,10.).StartsWith(TEXT("未列名参赛者"),ESearchCase::CaseSensitive));
    FLDObservedUnitView Camp;Camp.Kind=ELDObservedUnitKind::CampMember;Camp.AuthorizedFieldMask=LDObservation::CampFields;Camp.CreatureId=uint8(ELDDemoMonsterId::MonsterGuardian);
    Camp.Health.CurrentP2=100;Camp.Health.MaxP2=200;FLDObservedStatus Returning;Returning.StatusId=uint8(ELDDemoStatusId::CampReturning);Camp.Statuses.Add(Returning);
    const FString CampShown=LDHUD::ObservedUnit(Camp,TEXT("Ignored"),&Content,0.);
    TestEqualSensitive(TEXT("Camp card renders exactly name, HP and public status"),CampShown,FString(TEXT("体育老师\n面子 1/2\n状态：回岗中")));
    TestFalse(TEXT("Camp card has no level, mana, equipment or a contender name"),Has(CampShown,TEXT("等级"))||Has(CampShown,TEXT("胆量"))||Has(CampShown,TEXT("装备"))||Has(CampShown,TEXT("Ignored")));
    const LDHUD::FObservedCard CampCard=LDHUD::ObservedCard(Camp,FString(),&Content,0.);
    TestTrue(TEXT("Card structure follows the authorized field mask"),!CampCard.bMana&&!CampCard.bEquipment&&CampCard.Health.bKnown);
    FString Text;
    // The missing-text path is a content error by design: it logs once per id and shows the stable id.
    AddExpectedMessage(TEXT("LD_HUD_DISPLAY_TEXT_MISSING hud_rules_fixture_without_text"),ELogVerbosity::Error,EAutomationExpectedMessageFlags::Contains,1,false);
    TestFalse(TEXT("A definition without display text is a content error"),LDHUD::ContentText(&Content,TEXT("hud_rules_fixture_without_text"),false,Text));
    TestEqualSensitive(TEXT("A missing display text shows the stable id, never a guessed name"),Text,FString(TEXT("hud_rules_fixture_without_text")));
    TestTrue(TEXT("A registered display text resolves through the DemoContent table"),LDHUD::ContentText(&Content,TEXT("root"),false,Text)&&Text==TEXT("定身"));
    TestEqualSensitive(TEXT("Without approved content an observed creature id is reported, not named"),LDHUD::CreatureName(nullptr,2),FString(TEXT("未知野怪 #2")));
    TestEqualSensitive(TEXT("An unknown status id is reported, not named"),LDHUD::StatusName(&Content,200),FString(TEXT("未知状态 #200")));
    FLDLobbyView Lobby;FLDLobbyMember Member;Member.ContenderId=Rival.ContenderId;Member.Name=TEXT("同学甲");Lobby.Members.Add(Member);
    TestEqualSensitive(TEXT("Roster names come from the public roster"),LDHUD::RosterName(Lobby,Rival.ContenderId),FString(TEXT("同学甲")));
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
    TestEqualSensitive(TEXT("Protection counts down from the matching authority clock"),LDHUD::LifeStatus(Own,true,3000),FString(TEXT("复活保护 2.0s · 发起进攻即解除，失败也解除")));
    TestEqualSensitive(TEXT("A live protection never reads zero before the clock reaches its deadline"),LDHUD::LifeStatus(Own,true,4999),FString(TEXT("复活保护 0.1s · 发起进攻即解除，失败也解除")));
    TestEqualSensitive(TEXT("Protection ends once the authority clock reaches the deadline, before the field is cleared"),LDHUD::LifeStatus(Own,true,5000),FString(TEXT("存活")));
    TestEqualSensitive(TEXT("Without the matching clock protection time is unavailable, never local time or zero"),LDHUD::LifeStatus(Own,false,0),FString(TEXT("复活保护 · 时间同步中")));
    TestEqualSensitive(TEXT("A time value is ignored while the clock is unavailable"),LDHUD::LifeStatus(Own,false,999999),FString(TEXT("复活保护 · 时间同步中")));
    Own.ProtectedUntilMs=0;
    TestEqualSensitive(TEXT("No protection deadline needs no clock"),LDHUD::LifeStatus(Own,false,0),FString(TEXT("存活")));
    TestFalse(TEXT("A living owner has no death banner"),LDHUD::Death(Own,true,0).bVisible);
    Own.LifePhase=ELDLifePhase::Dead;Own.RespawnAtMs=12000;
    const FString Waiting=LDHUD::LifeStatus(Own,true,2350);
    TestEqualSensitive(TEXT("Respawn waits on the Lives deadline"),Waiting,FString(TEXT("社死 · 回宿舍冷静 9.7s")));
    TestFalse(TEXT("Death shows no gold or growth loss"),Waiting.Contains(TEXT("零花钱"))||Waiting.Contains(TEXT("损失")));
    TestEqualSensitive(TEXT("The respawn countdown reads zero only once the authority clock reaches it"),LDHUD::LifeStatus(Own,true,12000),FString(TEXT("社死 · 回宿舍冷静 0.0s")));
    TestEqualSensitive(TEXT("Respawn time without the matching clock is unavailable"),LDHUD::LifeStatus(Own,false,0),FString(TEXT("社死 · 回宿舍冷静（时间同步中）")));
    const LDHUD::FDeathView Death=LDHUD::Death(Own,true,2350);
    TestTrue(TEXT("The death banner names the state and the countdown and keeps the build"),Death.bVisible&&Death.Title==TEXT("社死")&&Death.Countdown==TEXT("回宿舍冷静 9.7s")&&Death.Note.Contains(TEXT("全部保留")));
    TestEqualSensitive(TEXT("The banner never invents a respawn time"),LDHUD::Death(Own,false,0).Countdown,FString(TEXT("回宿舍冷静 · 时间同步中")));
    Own.bActive=true;
    TestTrue(TEXT("The Lives phase, not the legacy active flag, decides the line"),LDHUD::LifeStatus(Own,true,0).StartsWith(TEXT("社死"),ESearchCase::CaseSensitive));
    Own.LifePhase=ELDLifePhase::Unspawned;TestEqualSensitive(TEXT("An unspawned contender is not shown alive"),LDHUD::LifeStatus(Own,true,0),FString(TEXT("等待出生")));
    Own.LifePhase=ELDLifePhase::Left;TestEqualSensitive(TEXT("A departed contender is not shown alive"),LDHUD::LifeStatus(Own,true,0),FString(TEXT("已离开本局")));
    Own.LifePhase=static_cast<ELDLifePhase>(200);TestEqualSensitive(TEXT("An unknown phase is reported, not shown alive"),LDHUD::LifeStatus(Own,true,0),FString(TEXT("生命状态未知")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDOwnResourcesTest,"Lockdown.Rules.HUDOwnResources",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDOwnResourcesTest::RunTest(const FString& Parameters)
{
    const LDHUD::FResourceView Health=LDHUD::Resource(TEXT("面子"),12.34,100,true);
    TestTrue(TEXT("Live health rounds up so a living body never reads zero"),Health.bKnown&&Health.Text==TEXT("面子 13/100")&&FMath::IsNearlyEqual(Health.Fraction,.1234f,.0001f));
    TestEqualSensitive(TEXT("Mana rounds down so a cost never looks affordable early"),LDHUD::Resource(TEXT("胆量"),7.9,30,false).Text,FString(TEXT("胆量 7/30")));
    const LDHUD::FResourceView Unset=LDHUD::Resource(TEXT("面子"),0,0,true);
    TestTrue(TEXT("An unprojected maximum is syncing, never 0/0"),!Unset.bKnown&&Unset.Text==TEXT("面子 同步中")&&Unset.Fraction==0.f);
    TestFalse(TEXT("A non-finite value is not shown as a resource"),LDHUD::Resource(TEXT("面子"),std::numeric_limits<double>::quiet_NaN(),100,true).bKnown);
    TestFalse(TEXT("A negative P2 value is not shown as a resource"),LDHUD::ResourceP2(TEXT("胆量"),-1,100,false).bKnown);
    TestEqualSensitive(TEXT("P2 encodings are hundredths of a point"),LDHUD::ResourceP2(TEXT("面子"),59950,60000,true).Text,FString(TEXT("面子 600/600")));
    const LDHUD::FLevelView Mid=LDHUD::Level(3,10,200,180,true,300);
    TestTrue(TEXT("Experience shows progress inside the current level"),Mid.Level==TEXT("等级 3")&&Mid.Experience==TEXT("经验 20/120")&&FMath::IsNearlyEqual(Mid.Fraction,1.f/6.f,.0001f)&&!Mid.bMax);
    const LDHUD::FLevelView Cap=LDHUD::Level(10,10,1500,1440,false,0);
    TestTrue(TEXT("The level cap shows MAX instead of an eleventh threshold"),Cap.bMax&&Cap.Level==TEXT("等级 10")&&Cap.Experience==TEXT("经验 MAX"));
    TestEqualSensitive(TEXT("Experience below the level threshold is a data error, not progress"),LDHUD::Level(3,10,100,180,true,300).Experience,FString(TEXT("经验数据异常")));
    TestEqualSensitive(TEXT("An invalid level is not shown as level one"),LDHUD::Level(0,10,0,0,true,80).Level,FString(TEXT("等级 —")));
    TestEqualSensitive(TEXT("Gold uses the approved player term"),LDHUD::Gold(150),FString(TEXT("零花钱 150")));
    TestEqualSensitive(TEXT("Zero remainder reads zero"),LDHUD::TenthsUp(0),FString(TEXT("0.0s")));
    TestEqualSensitive(TEXT("Any live remainder reads at least a tenth"),LDHUD::TenthsUp(1),FString(TEXT("0.1s")));
    TestEqualSensitive(TEXT("Remainders round up to the next tenth"),LDHUD::TenthsUp(101),FString(TEXT("0.2s")));
    TestTrue(TEXT("Development names the closed capture point"),LDHUD::PhaseLine(ELDMatchPhase::Development).Contains(TEXT("发育期")));
    TestTrue(TEXT("An authority fault is stated explicitly"),LDHUD::PhaseLine(ELDMatchPhase::Faulted).Contains(TEXT("故障")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDActionViewTest,"Lockdown.Rules.HUDActionView",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDActionViewTest::RunTest(const FString& Parameters)
{
    LDHUD::FActionContext Context;Context.KeyLabel=TEXT("Q");Context.bOwnContext=true;Context.bPlaying=true;Context.bAlive=true;Context.bClock=true;Context.NowMs=4800;Context.bManaKnown=true;Context.ManaP2=10000;
    LDHUD::FActionInput Bolt;Bolt.Slot=LDHUD::EActionSlot::Q;Bolt.bProjected=true;Bolt.bHasSource=true;Bolt.Name=TEXT("粉笔头");Bolt.Rank=2;Bolt.bHasActive=true;Bolt.ManaCostP2=5000;
    LDHUD::FActionView View=LDHUD::Action(Bolt,Context);
    TestTrue(TEXT("A ready learned active can be sent and shows its rank"),View.bCanSubmitIntent&&View.ExecutionBlocks==0&&View.State==TEXT("就绪")&&View.Name==TEXT("粉笔头 2级")&&View.Kind==TEXT("角色主动"));
    Bolt.ReadyAtMs=8000;Bolt.CooldownMs=8000;View=LDHUD::Action(Bolt,Context);
    TestTrue(TEXT("Cooldown is an execution limit with its own track remainder"),(View.ExecutionBlocks&LDHUD::ActionCooldown)&&View.State==TEXT("冷却 3.2s")&&View.CooldownRemainingMs==3200&&FMath::IsNearlyEqual(View.CooldownFraction,.4f,.0001f));
    TestTrue(TEXT("Cooldown does not suppress the explicit intent"),View.bCanSubmitIntent);
    Context.ManaP2=4000;View=LDHUD::Action(Bolt,Context);
    TestTrue(TEXT("Several limits are all reported while the state shows the first"),(View.ExecutionBlocks&LDHUD::ActionCooldown)&&(View.ExecutionBlocks&LDHUD::ActionMana)&&View.State.StartsWith(TEXT("冷却")));
    Bolt.ReadyAtMs=0;View=LDHUD::Action(Bolt,Context);
    TestTrue(TEXT("Insufficient mana greys and explains without blocking the intent"),View.State==TEXT("胆量不足")&&View.bCanSubmitIntent);
    Context.ManaP2=10000;
    LDHUD::FActionInput Empty;Empty.Slot=LDHUD::EActionSlot::W;Empty.bProjected=true;View=LDHUD::Action(Empty,Context);
    TestTrue(TEXT("An empty learned slot is an explicit EmptySource attempt, still sent"),View.Name==TEXT("空槽")&&(View.ExecutionBlocks&LDHUD::ActionEmpty)&&View.bCanSubmitIntent);
    Empty.Slot=LDHUD::EActionSlot::Item1;View=LDHUD::Action(Empty,Context);
    TestTrue(TEXT("An empty equipment slot says so"),View.Name==TEXT("无装备")&&View.Kind==TEXT("装备主动"));
    LDHUD::FActionInput Plate;Plate.Slot=LDHUD::EActionSlot::Item2;Plate.bProjected=true;Plate.bHasSource=true;Plate.Name=TEXT("《五年挨打三年模拟》");
    View=LDHUD::Action(Plate,Context);
    TestTrue(TEXT("Attribute-only equipment is marked without an active, never a pretend skill"),View.State==TEXT("无主动效果")&&(View.ExecutionBlocks&LDHUD::ActionNoActive)&&View.Name==TEXT("《五年挨打三年模拟》"));
    LDHUD::FActionInput First;First.Slot=LDHUD::EActionSlot::Item1;First.bProjected=true;First.bHasSource=true;First.Name=TEXT("保温杯");First.bHasActive=true;First.ReadyAtMs=40000;First.CooldownMs=40000;
    LDHUD::FActionInput Second=First;Second.Slot=LDHUD::EActionSlot::Item2;Second.ReadyAtMs=0;
    TestTrue(TEXT("Two copies of one item keep independent readiness"),LDHUD::Action(First,Context).State.StartsWith(TEXT("冷却"))&&LDHUD::Action(Second,Context).State==TEXT("就绪"));
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
    TestTrue(TEXT("A dead owner cannot send combat intents"),!View.bCanSubmitIntent&&View.State==TEXT("社死中"));
    Context.bAlive=true;Context.bInputCaptured=true;View=LDHUD::Action(Bolt,Context);
    TestTrue(TEXT("A higher UI layer owning input suppresses sending, not the readiness display"),!View.bCanSubmitIntent&&View.State==TEXT("就绪"));
    Context.bInputCaptured=false;Context.bOwnContext=false;View=LDHUD::Action(Bolt,Context);
    TestTrue(TEXT("A stale match or life context never sends"),!View.bCanSubmitIntent&&(View.ExecutionBlocks&LDHUD::ActionUnsynced));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDPublicMatchTest,"Lockdown.Rules.HUDPublicMatch",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDPublicMatchTest::RunTest(const FString& Parameters)
{
    FLDPublicMatchView Public;Public.Generation=4;Public.Phase=ELDMatchPhase::Development;Public.OpenAtMs=120000;
    TestEqualSensitive(TEXT("Development counts down to the public opening in whole seconds rounded up"),LDHUD::Circle(Public,FString(),true,500).Line,FString(TEXT("发育期 · 升旗台 2:00 后开放")));
    TestEqualSensitive(TEXT("A running countdown never reads 0:00 before the opening"),LDHUD::Clock(1),FString(TEXT("0:01")));
    TestEqualSensitive(TEXT("Without the authority clock the opening time is not guessed"),LDHUD::Circle(Public,FString(),false,0).Line,FString(TEXT("发育期 · 升旗台尚未开放")));
    Public.Phase=ELDMatchPhase::CaptureOpen;Public.CircleState=ELDCircleState::Empty;
    TestEqualSensitive(TEXT("An open empty circle has no occupier"),LDHUD::Circle(Public,FString(),true,0).Line,FString(TEXT("升旗台已开放 · 无人占领")));
    Public.CircleState=ELDCircleState::Contested;
    const LDHUD::FCircleView Contested=LDHUD::Circle(Public,TEXT("同学甲"),true,0);
    TestTrue(TEXT("A contest without a holder names no one and shows no progress"),Contested.Line==TEXT("升旗台争夺中 · 无人保有进度")&&!Contested.bProgress);
    {
        FLDPublicMatchView Paused=Public;Paused.Occupier=FGuid::NewGuid();Paused.PausedProgressMs=12001;
        const LDHUD::FCircleView Held=LDHUD::Circle(Paused,TEXT("同学甲"),true,0,25000);
        TestTrue(TEXT("A paused holder's progress shows against the approved hold, rounded up"),
            Held.Line==TEXT("升旗台争夺中 · 进度暂停 13/25 s（同学甲）")&&Held.bProgress&&FMath::IsNearlyEqual(Held.Progress,12001.f/25000.f));
        TestTrue(TEXT("Without the approved hold the paused seconds still show, without a bar"),
            LDHUD::Circle(Paused,TEXT("同学甲"),true,0).Line==TEXT("升旗台争夺中 · 同学甲 的进度暂停在 13 s")&&!LDHUD::Circle(Paused,TEXT("同学甲"),true,0).bProgress);
    }
    Public.CircleState=ELDCircleState::Solo;Public.SoloSinceMs=100000;Public.DueAtMs=125000;
    const LDHUD::FCircleView Solo=LDHUD::Circle(Public,TEXT("同学甲"),true,110000);
    TestTrue(TEXT("Solo progress runs from the public solo start to its due time"),Solo.bProgress&&FMath::IsNearlyEqual(Solo.Progress,.4f,.0001f)&&Solo.Line==TEXT("同学甲 正在独占升旗台 · 剩余 15.0s"));
    const LDHUD::FCircleView Due=LDHUD::Circle(Public,TEXT("同学甲"),true,126000);
    TestTrue(TEXT("Reaching the due time waits for the authority and never claims a winner"),Due.Progress==1.f&&Due.Line.EndsWith(TEXT("0.0s"))&&!Due.Line.Contains(TEXT("加冕")));
    TestEqualSensitive(TEXT("An unlisted occupier is not given an invented name"),LDHUD::Circle(Public,FString(),false,0).Line,FString(TEXT("未列名参赛者 正在独占升旗台 · 进度同步中")));
    FLDKillNotice Notice;Notice.Cause=ELDKillCause::Contender;
    TestEqualSensitive(TEXT("A contender kill names both public roster names"),LDHUD::KillLine(Notice,TEXT("乙"),TEXT("甲"),TEXT("纪律委员"),TEXT("体育老师")),FString(TEXT("甲 让 乙 社死了")));
    Notice.Cause=ELDKillCause::CampHigh;
    TestEqualSensitive(TEXT("A camp death names the tier creature, not a hidden attacker"),LDHUD::KillLine(Notice,TEXT("乙"),TEXT("甲"),TEXT("纪律委员"),TEXT("体育老师")),FString(TEXT("乙 被体育老师送去社死")));
    Notice.Cause=ELDKillCause::Unowned;
    TestEqualSensitive(TEXT("A death without a contender owner says so"),LDHUD::KillLine(Notice,TEXT("乙"),FString(),TEXT("纪律委员"),TEXT("体育老师")),FString(TEXT("乙 社死了（无参赛者击杀）")));
    FLDResultView Result;Result.Reason=ELDMatchEndReason::CaptureCompleted;Result.Winner=FGuid::NewGuid();
    TestEqualSensitive(TEXT("A capture result crowns the one winner"),LDHUD::ResultHeadline(Result,TEXT("甲")),FString(TEXT("甲 连续独占升旗台，加冕阴人王！")));
    Result.Reason=ELDMatchEndReason::HostEnded;Result.Winner.Invalidate();
    TestEqualSensitive(TEXT("A Host-ended match has no winner"),LDHUD::ResultHeadline(Result,FString()),FString(TEXT("房主结束了本局 · 无胜者")));
    FLDResultRow Row;Row.ContenderId=FGuid::NewGuid();Row.Name=TEXT("机器人");Row.Kind=ELDContenderKind::OpponentAI;Row.bLeft=true;Row.Kills=3;Row.Deaths=1;Row.FinalLevel=7;
    const LDHUD::FResultRowView Shown=LDHUD::ResultRow(Row,Row.ContenderId,FGuid());
    TestTrue(TEXT("A frozen row keeps the departed AI and its statistics"),Shown.Name==TEXT("机器人（AI）（已离开）")&&Shown.Kills==TEXT("3")&&Shown.Deaths==TEXT("1")&&Shown.Level==TEXT("7")&&Shown.bWinner);
    TestFalse(TEXT("Statistics never mark a winner on their own"),LDHUD::ResultRow(Row,FGuid(),FGuid()).bWinner);
    // D26: the public airdrop line.
    FLDAirdropView Drop;
    TestTrue(TEXT("No drop shows no airdrop line"),LDHUD::AirdropLine(Drop,FGuid(),FString(),true,0).IsEmpty());
    Drop.Serial=1;Drop.Phase=ELDAirdropPhase::Announced;Drop.PointId=TEXT("airdrop_s");Drop.LandAtMs=45000;
    TestEqualSensitive(TEXT("An announced drop counts down to its landing"),LDHUD::AirdropLine(Drop,FGuid(),FString(),true,30001),
        FString(TEXT("空投 0:15 后落地 · 小地图橙色标记")));
    TestEqualSensitive(TEXT("Without the authority clock the landing time is not guessed"),LDHUD::AirdropLine(Drop,FGuid(),FString(),false,0),
        FString(TEXT("空投即将落地 · 小地图橙色标记")));
    Drop.Phase=ELDAirdropPhase::Landed;
    TestEqualSensitive(TEXT("A landed drop is first come, first served"),LDHUD::AirdropLine(Drop,FGuid(),FString(),true,46000),
        FString(TEXT("空投已落地 · 先到先得")));
    const FGuid Me=FGuid::NewGuid();
    Drop.Phase=ELDAirdropPhase::Claimed;Drop.ClaimedBy=Me;Drop.ClaimedAtMs=47000;
    TestEqualSensitive(TEXT("The own claim is announced to its owner"),LDHUD::AirdropLine(Drop,Me,FString(),true,48000),FString(TEXT("你抢到了空投！")));
    TestEqualSensitive(TEXT("Another contender's claim names the public roster name"),LDHUD::AirdropLine(Drop,FGuid::NewGuid(),TEXT("甲"),true,48000),
        FString(TEXT("甲 抢到了空投")));
    TestTrue(TEXT("The claim notice leaves after a few seconds"),LDHUD::AirdropLine(Drop,FGuid::NewGuid(),TEXT("甲"),true,60000).IsEmpty());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDContentParametersTest,"Lockdown.Rules.HUDContentParameters",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDContentParametersTest::RunTest(const FString& Parameters)
{
    FLDDemoStatBonus Stat;Stat.Stat=ELDDemoBonusStat::ADFlat;Stat.Amount=2000;
    TestEqualSensitive(TEXT("Flat stats are P2 points"),LDHUD::StatLine(Stat),FString(TEXT("手劲 +20")));
    Stat.Stat=ELDDemoBonusStat::AttackSpeedBonusRatio;Stat.Amount=1000;
    TestEqualSensitive(TEXT("Ratio stats are basis points"),LDHUD::StatLine(Stat),FString(TEXT("手速 +10%")));
    Stat.Stat=ELDDemoBonusStat::AttackRangeCmFlat;Stat.Amount=0;Stat.Cm.Numerator=40;Stat.Cm.Denominator=1;
    TestEqualSensitive(TEXT("Range bonuses are centimetres"),LDHUD::StatLine(Stat),FString(TEXT("攻击距离 +40cm")));
    FLDDemoEffectDefinition Damage;Damage.Opcode=uint8(ELDDemoEffectOp::Damage);Damage.BaseP2=10000;Damage.ADRatioBp=8000;Damage.APRatioBp=6500;Damage.DamageType=ELDDemoDamageType::Magical;
    TestEqualSensitive(TEXT("Magic damage uses the approved term and its AD and AP scaling"),LDHUD::EffectLine(Damage),FString(TEXT("社死伤害 100 + 80%手劲 + 65%缺德值")));
    FLDDemoEffectDefinition Root;Root.Opcode=uint8(ELDDemoEffectOp::Root);Root.DurationMs=1300;
    TestEqualSensitive(TEXT("Control duration is shown in seconds"),LDHUD::EffectLine(Root),FString(TEXT("定身 1.3s")));
    FLDDemoEffectDefinition Unknown;Unknown.Opcode=200;
    TestEqualSensitive(TEXT("An unknown effect is reported, never described"),LDHUD::EffectLine(Unknown),FString(TEXT("未知效果 #200")));
    FLDDemoRankDefinition Rank;Rank.Rank=1;Rank.ManaCostP2=4500;Rank.CooldownMs=6400;Rank.Effects.Add(Damage);
    const TArray<FString> Lines=LDHUD::RankLines(Rank,true);
    TestTrue(TEXT("Active rank lines carry the effect then mana and cooldown"),Lines.Num()==2&&Lines[1]==TEXT("胆量 45 · 冷却 6.4s"));
    TestEqual(TEXT("Passive ranks have no cost line"),LDHUD::RankLines(Rank,false).Num(),1);
    FLDDemoContentDefinitions Content;FLDDemoAbilityDefinition Bolt;Bolt.Id=TEXT("hud_rules_fixture_bolt");Bolt.Kind=ELDDemoAbilityKind::Active;
    Bolt.Target.Mode=ELDDemoTargetMode::DirectionProjectile;Bolt.Target.RangeCm.Numerator=650;Bolt.Target.RangeCm.Denominator=1;
    FLDDemoRankDefinition Second=Rank;Second.Rank=2;Bolt.Ranks={Rank,Second};Content.Abilities.Add(Bolt);
    FLDProgressionView Progression;
    for(uint8 Slot=0;Slot<2;++Slot){FLDLearnedSkillView Skill;Skill.DefinitionId=Slot?FName(TEXT("active_guard_pulse")):FName(TEXT("active_binding_snare"));Skill.bActive=true;Skill.Rank=1;Skill.Slot=Slot;Progression.Skills.Add(Skill);}
    FLDChoiceOption Learn;Learn.Kind=ELDChoiceOptionKind::LearnActive;Learn.DefinitionId=TEXT("hud_rules_fixture_bolt");Learn.ToRank=1;
    AddExpectedMessage(TEXT("LD_HUD_DISPLAY_TEXT_MISSING hud_rules_fixture_bolt"),ELogVerbosity::Error,EAutomationExpectedMessageFlags::Contains,0,false);
    const LDHUD::FChoiceCardView Card=LDHUD::ChoiceCard(Learn,Progression,&Content);
    TestTrue(TEXT("Learning a third active with Q/W full requires an explicit replacement"),Card.bNeedsReplacement&&Card.Kind==TEXT("新主动技能")&&Card.RankText==TEXT("1 级"));
    TestTrue(TEXT("The card shows the target form and the rank's typed parameters"),Card.Lines.Num()==3&&Card.Lines[0]==TEXT("方向弹道 · 射程 650cm"));
    FLDChoiceOption Upgrade=Learn;Upgrade.Kind=ELDChoiceOptionKind::UpgradeActive;Upgrade.FromRank=1;Upgrade.ToRank=2;
    const LDHUD::FChoiceCardView Up=LDHUD::ChoiceCard(Upgrade,Progression,&Content);
    TestTrue(TEXT("An upgrade states both ranks and never needs a replacement"),!Up.bNeedsReplacement&&Up.RankText==TEXT("1 → 2 级"));
    FLDCombatAttributeRowView Crit;Crit.Attribute=uint8(ELDProjectedAttribute::CritChance);Crit.Final.Numerator=1;Crit.Final.Denominator=5;Crit.Base.Denominator=1;
    Crit.Level.Denominator=1;Crit.Choices.Denominator=1;Crit.Passives.Denominator=1;Crit.Equipment.Denominator=1;
    const LDHUD::FAttributeRowView Row=LDHUD::AttributeRow(Crit);
    TestTrue(TEXT("Attribute rows use the approved name and show ratios as percent"),Row.Name==TEXT("正中要害率")&&Row.Final==TEXT("20%"));
    TestEqualSensitive(TEXT("Shop failures are explained in the approved terms"),LDHUD::GrowthReasonText(ELDGrowthReason::InsufficientGold),FString(TEXT("零花钱不足")));
    TestEqualSensitive(TEXT("A full equipment bar says to sell first"),LDHUD::GrowthReasonText(ELDGrowthReason::EquipmentFull),FString(TEXT("装备槽已满，请先卖出一件")));
    TestEqualSensitive(TEXT("Combat rejections are explained"),LDHUD::CombatRejectText(ELDCombatReject::OutOfRange),FString(TEXT("超出射程")));
    FLDCombatEntryView Entry;Entry.DefinitionId=NAME_None;Entry.Unavailable=ELDCombatReject::SourceUnavailable;
    const LDHUD::FActionInput Empty=LDHUD::ActionInput(Entry,LDHUD::EActionSlot::Q,&Content);
    TestTrue(TEXT("An empty combat entry is an explicit empty source with the server reason kept for display"),Empty.bProjected&&!Empty.bHasSource&&Empty.ServerBlock==TEXT("没有可用来源"));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDCombatOwnerViewTest,"Lockdown.Rules.HUDCombatOwnerView",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDCombatOwnerViewTest::RunTest(const FString& Parameters)
{
    HudRegisterFixtureTable();
    ON_SCOPE_EXIT{FStringTableRegistry::Get().UnregisterStringTable(HudFixtureTable);};
    FLDDemoContentDefinitions Content=HudFixtureContent();
    FLDCombatOwnerView Combat;
    FLDCombatStatusView Root;Root.StatusId=uint8(ELDDemoStatusId::Root);Root.ExpiresAtMs=11201;Root.ControlBlocks=uint8(ELDCombatControlBlock::Move);
    FLDCombatStatusView Aura;Aura.StatusId=uint8(ELDDemoStatusId::Shield);Aura.bIsPassive=true;
    FLDCombatStatusView Protection;Protection.StatusId=uint8(ELDDemoStatusId::RespawnProtection);Protection.ExpiresAtMs=20000;
    FLDCombatStatusView Shield;Shield.StatusId=uint8(ELDDemoStatusId::Shield);Shield.AmountP2=8050;Shield.ExpiresAtMs=12000;
    Combat.Statuses={Root,Aura,Protection,Shield};
    TestEqualSensitive(TEXT("Temporary statuses round time and shield up; passive auras and protection stay off the line"),
        LDHUD::SelfStatusLine(Combat,true,10000,&Content),FString(TEXT("定身 1.3s · 淡定 81 2.0s")));
    TestEqualSensitive(TEXT("Without the matching authority clock no remainder is shown"),LDHUD::SelfStatusLine(Combat,false,0,&Content),FString(TEXT("定身 · 淡定 81")));
    TestEqualSensitive(TEXT("Missing projections read as syncing, never as none"),LDHUD::PassiveLine(nullptr,nullptr,&Content),FString(TEXT("被动：角色被动同步中、装备被动同步中")));
    FLDProgressionView Progression;
    FLDLearnedSkillView Rhythm;Rhythm.DefinitionId=TEXT("passive_battle_rhythm");Rhythm.Rank=2;
    FLDLearnedSkillView Active;Active.DefinitionId=TEXT("active_guard_pulse");Active.bActive=true;Active.Rank=3;
    Progression.Skills={Rhythm,Active};
    Content.Equipment[0].HasPassive=true;Content.Equipment[0].Passive.Family=TEXT("passive_siphon");
    Combat.EquipmentPassives={FName(TEXT("passive_siphon")),FName(TEXT("passive_siphon"))};
    TestEqualSensitive(TEXT("Learned passives keep their rank; an equipment passive appears once under its equipment"),
        LDHUD::PassiveLine(&Progression,&Combat,&Content),FString(TEXT("被动：单身二十年的手速 2级、《五年挨打三年模拟》")));
    Progression.Skills={Active};Combat.EquipmentPassives.Reset();
    TestEqualSensitive(TEXT("Known projections without passives say so"),LDHUD::PassiveLine(&Progression,&Combat,&Content),FString(TEXT("被动：无")));
    FLDCombatActionReceipt Receipt;Receipt.Origin=ELDCombatRequestOrigin::Direct;Receipt.Entry=ELDCombatEntry::Item1;
    Receipt.Phase=ELDCombatPhase::Rejected;Receipt.Reject=ELDCombatReject::Cooldown;Receipt.bProtectionRemoved=true;
    TestEqualSensitive(TEXT("A failed own request and the lost protection are separate facts"),LDHUD::CombatReceiptNotice(Receipt),FString(TEXT("装备 1 未执行：冷却中 · 复活保护已解除")));
    Receipt.bProtectionRemoved=false;Receipt.Reject=ELDCombatReject::Cancelled;
    TestTrue(TEXT("A cancel tells nothing"),LDHUD::CombatReceiptNotice(Receipt).IsEmpty());
    Receipt.Origin=ELDCombatRequestOrigin::Nav;Receipt.Reject=ELDCombatReject::OutOfRange;
    TestTrue(TEXT("Navigation-issued attacks report through their order, not as an action failure"),LDHUD::CombatReceiptNotice(Receipt).IsEmpty());
    Receipt.Phase=ELDCombatPhase::Accepted;Receipt.Reject=ELDCombatReject::None;Receipt.bProtectionRemoved=true;
    TestEqualSensitive(TEXT("An accepted attack still reports the lost protection"),LDHUD::CombatReceiptNotice(Receipt),FString(TEXT("复活保护已解除")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDBasicAttackTermTest,"Lockdown.Rules.HUDBasicAttackTerm",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDBasicAttackTermTest::RunTest(const FString& Parameters)
{
    // APP_SPEC 1.2: the player reads the basic attack as 弹脑瓜崩; 普攻 stays a technical term only.
    const FString NotReady=LDHUD::CombatRejectText(ELDCombatReject::AttackNotReady);
    TestTrue(TEXT("The attack-not-ready reason names the basic attack by its player term"),
        NotReady.Contains(TEXT("弹脑瓜崩"),ESearchCase::CaseSensitive)&&!NotReady.Contains(TEXT("普攻"),ESearchCase::CaseSensitive));
    FLDDemoEffectDefinition Lifesteal;Lifesteal.Opcode=uint8(ELDDemoEffectOp::Lifesteal);Lifesteal.RatioBp=1500;
    TestEqualSensitive(TEXT("The lifesteal line names the basic attack by its player term"),LDHUD::EffectLine(Lifesteal),FString(TEXT("弹脑瓜崩造成的面子损失的 15% 回复自身")));
    TestEqualSensitive(TEXT("The basic entry label uses the player term"),LDHUD::EntryLabel(ELDCombatEntry::Basic),FString(TEXT("弹脑瓜崩")));
    FString Technical;
    for(int32 Value=0;Value<=MAX_uint8;++Value)
    {
        FLDDemoEffectDefinition Effect;Effect.Opcode=uint8(Value);
        for(const FString& Line:{LDHUD::CombatRejectText(static_cast<ELDCombatReject>(Value)),LDHUD::EffectLine(Effect),LDHUD::EntryLabel(static_cast<ELDCombatEntry>(Value))})
        {if(Line.Contains(TEXT("普攻"),ESearchCase::CaseSensitive)){Technical+=Line+TEXT(" | ");}}
    }
    TestTrue(*FString::Printf(TEXT("No combat reason, effect line or entry label shows the technical term (found: %s)"),*Technical),Technical.IsEmpty());
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
    LDHUD::FActionInput Bolt;Bolt.Slot=LDHUD::EActionSlot::Q;Bolt.bProjected=true;Bolt.bHasSource=true;Bolt.Name=TEXT("粉笔头");Bolt.bHasActive=true;Bolt.ReadyAtMs=8000;Bolt.CooldownMs=8000;
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
    LDHUD::FActionInput Bolt;Bolt.Slot=LDHUD::EActionSlot::Q;Bolt.bProjected=true;Bolt.bHasSource=true;Bolt.Name=TEXT("粉笔头");Bolt.Rank=1;Bolt.bHasActive=true;Bolt.ManaCostP2=3050;
    LDHUD::FActionView View=LDHUD::Action(Bolt,Context);
    LDHUD::FActionLookView Shown=LDHUD::ActionLook(View,false);
    TestEqualSensitive(TEXT("The cost chip rounds the mana cost down, like the shown mana"),View.Cost,FString(TEXT("胆量 30")));
    TestTrue(TEXT("A ready, affordable active looks ready and says so"),Shown.Look==LDHUD::EActionLook::Ready&&Shown.Caption==TEXT("就绪"));
    Bolt.ReadyAtMs=8000;Bolt.CooldownMs=8000;View=LDHUD::Action(Bolt,Context);Shown=LDHUD::ActionLook(View,false);
    TestTrue(TEXT("A cooling active names the cooldown and carries its remainder"),Shown.Look==LDHUD::EActionLook::Cooling&&Shown.Caption==TEXT("冷却中")&&View.Countdown==TEXT("3.2"));
    Context.ManaP2=1000;View=LDHUD::Action(Bolt,Context);
    TestTrue(TEXT("A cooldown outranks missing mana"),LDHUD::ActionLook(View,false).Look==LDHUD::EActionLook::Cooling);
    Bolt.ReadyAtMs=0;View=LDHUD::Action(Bolt,Context);Shown=LDHUD::ActionLook(View,false);
    TestTrue(TEXT("Missing mana without a cooldown has its own look and caption and still sends"),Shown.Look==LDHUD::EActionLook::NoMana&&Shown.Caption==TEXT("胆量不足")&&View.bCanSubmitIntent);
    Bolt.bControlled=true;View=LDHUD::Action(Bolt,Context);Shown=LDHUD::ActionLook(View,false);
    TestTrue(TEXT("A control status outranks missing mana and is named"),Shown.Look==LDHUD::EActionLook::Controlled&&Shown.Caption==TEXT("受控"));
    Bolt.ReadyAtMs=8000;View=LDHUD::Action(Bolt,Context);
    TestTrue(TEXT("A control status outranks the cooldown, whose remainder still shows"),LDHUD::ActionLook(View,false).Look==LDHUD::EActionLook::Controlled&&View.Countdown==TEXT("3.2"));
    Shown=LDHUD::ActionLook(View,true);
    TestTrue(TEXT("The armed entry outranks its remaining limits"),Shown.Look==LDHUD::EActionLook::Armed&&Shown.Caption==TEXT("选择目标"));
    Bolt.bControlled=false;Bolt.ReadyAtMs=0;Context.ManaP2=10000;
    LDHUD::FActionInput Empty;Empty.Slot=LDHUD::EActionSlot::W;Empty.bProjected=true;View=LDHUD::Action(Empty,Context);Shown=LDHUD::ActionLook(View,true);
    TestTrue(TEXT("An empty slot looks empty even when armed; its name says so and it has no cost"),Shown.Look==LDHUD::EActionLook::Empty&&Shown.Caption.IsEmpty()&&View.Cost.IsEmpty()&&View.Name==TEXT("空槽"));
    LDHUD::FActionInput Plate;Plate.Slot=LDHUD::EActionSlot::Item1;Plate.bProjected=true;Plate.bHasSource=true;Plate.Name=TEXT("《五年挨打三年模拟》");Plate.ManaCostP2=2000;
    View=LDHUD::Action(Plate,Context);Shown=LDHUD::ActionLook(View,false);
    TestTrue(TEXT("A source without an active looks empty, says why and shows no cost"),Shown.Look==LDHUD::EActionLook::Empty&&Shown.Caption==TEXT("无主动效果")&&View.Cost.IsEmpty());
    Context.bAlive=false;View=LDHUD::Action(Bolt,Context);Shown=LDHUD::ActionLook(View,true);
    TestTrue(TEXT("A dead owner's slot is blocked and named, never armed"),Shown.Look==LDHUD::EActionLook::Blocked&&Shown.Caption==TEXT("社死中"));
    Context.bAlive=true;Bolt.ServerBlock=TEXT("目标不可用");View=LDHUD::Action(Bolt,Context);Shown=LDHUD::ActionLook(View,false);
    TestTrue(TEXT("A published server reason blocks the look with that reason"),Shown.Look==LDHUD::EActionLook::Blocked&&Shown.Caption==TEXT("目标不可用"));
    Bolt.ServerBlock.Reset();Bolt.ManaCostP2=0;
    TestTrue(TEXT("An active without a cost shows no chip"),LDHUD::Action(Bolt,Context).Cost.IsEmpty());
    LDHUD::FActionInput Unprojected=Bolt;Unprojected.bProjected=false;Unprojected.ManaCostP2=5000;View=LDHUD::Action(Unprojected,Context);
    TestTrue(TEXT("Without a projection there is no cost and the slot is unsynced"),View.Cost.IsEmpty()&&LDHUD::ActionLook(View,false).Look==LDHUD::EActionLook::Blocked);
    FLDCombatActionReceipt Receipt;Receipt.Origin=ELDCombatRequestOrigin::Direct;Receipt.Entry=ELDCombatEntry::W;Receipt.Phase=ELDCombatPhase::Rejected;Receipt.Reject=ELDCombatReject::OutOfRange;
    TestEqualSensitive(TEXT("The pressed slot shows the reason of the player's failed request"),LDHUD::CombatReceiptSlotNotice(Receipt),FString(TEXT("超出射程")));
    Receipt.Entry=ELDCombatEntry::Basic;
    TestTrue(TEXT("The basic attack has no slot to tell"),LDHUD::CombatReceiptSlotNotice(Receipt).IsEmpty());
    Receipt.Entry=ELDCombatEntry::Item2;Receipt.Reject=ELDCombatReject::Cancelled;
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
    FLDLobbyMember Killer;Killer.ContenderId=Rival;Killer.Name=TEXT("同学甲");Lobby.Members.Add(Killer);
    FLDKillFeedView Feed;Feed.Generation=7;
    auto AddNotice=[&Feed](uint64 Serial,const FGuid& Victim,const FGuid& By,ELDKillCause Kind,int64 AtMs)
    {FLDKillNotice Entry;Entry.Serial=Serial;Entry.Victim=Victim;Entry.Killer=By;Entry.Cause=Kind;Entry.OccurredAtMs=AtMs;Feed.Notices.Add(Entry);};
    auto CauseAt=[&Feed,&Lobby,&Self](uint32 Generation,int64 NotBeforeMs){return LDHUD::DeathCause(Feed,Generation,Self,NotBeforeMs,Lobby,TEXT("纪律委员"),TEXT("体育老师"));};
    AddNotice(1,Self,FGuid(),ELDKillCause::CampHigh,10000);
    AddNotice(2,Other,Rival,ELDKillCause::Contender,30000);
    AddNotice(3,Self,Rival,ELDKillCause::Contender,50000);
    TestEqualSensitive(TEXT("The newest own death names its killer from the public roster"),CauseAt(7,40000),FString(TEXT("被 同学甲 送去社死")));
    TestTrue(TEXT("A feed of another generation is not this match"),CauseAt(8,0).IsEmpty()&&CauseAt(0,0).IsEmpty());
    TestTrue(TEXT("Before this life's notice arrives an older death never stands in"),CauseAt(7,60000).IsEmpty());
    TestTrue(TEXT("Without an own identity there is no cause"),LDHUD::DeathCause(Feed,7,FGuid(),0,Lobby,TEXT("纪律委员"),TEXT("体育老师")).IsEmpty());
    Feed.Notices.RemoveAt(2);
    TestEqualSensitive(TEXT("A camp death names the approved creature of its tier"),CauseAt(7,0),FString(TEXT("被体育老师送去社死")));
    TestTrue(TEXT("Another contender's death is never the player's cause"),CauseAt(7,20000).IsEmpty());
    AddNotice(4,Self,FGuid(),ELDKillCause::CampLow,70000);
    TestEqualSensitive(TEXT("The low camp tier has its own creature"),CauseAt(7,60000),FString(TEXT("被纪律委员送去社死")));
    AddNotice(5,Self,FGuid::NewGuid(),ELDKillCause::Contender,80000);
    TestEqualSensitive(TEXT("A killer missing from the roster is not given an invented name"),CauseAt(7,60000),FString(TEXT("被 未列名参赛者 送去社死")));
    AddNotice(6,Self,FGuid(),ELDKillCause::Unowned,90000);
    TestEqualSensitive(TEXT("A death without a contender owner says so"),CauseAt(7,60000),FString(TEXT("无参赛者击杀")));
    FLDOwnerState Own;Own.LifePhase=ELDLifePhase::Dead;Own.RespawnAtMs=12000;
    const FString Note=LDHUD::Death(Own,true,0).Note;
    TestTrue(TEXT("The death note is one short line that keeps the build and the choices"),Note.Len()<=24&&!Note.Contains(TEXT("；"))&&!Note.Contains(TEXT("\n"))
        &&Note.Contains(TEXT("全部保留"))&&Note.Contains(TEXT("选卡不影响复活")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDKillFeedSelfTest,"Lockdown.Rules.HUDKillFeedSelf",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDKillFeedSelfTest::RunTest(const FString& Parameters)
{
    const FGuid Self=FGuid::NewGuid(),Rival=FGuid::NewGuid(),Other=FGuid::NewGuid();
    auto Line=[](const FLDKillNotice& Source,const FGuid& Viewer,const TCHAR* VictimName,const TCHAR* KillerName)
    {return LDHUD::KillFeedLine(Source,Viewer,VictimName,KillerName,TEXT("纪律委员"),TEXT("体育老师"));};
    FLDKillNotice Notice;Notice.Cause=ELDKillCause::Contender;Notice.Victim=Rival;Notice.Killer=Self;
    LDHUD::FKillFeedLineView Shown=Line(Notice,Self,TEXT("乙"),TEXT("我"));
    TestTrue(TEXT("The player's own kill says 你 and is highlighted"),Shown.bSelf&&Shown.Text==TEXT("你 让 乙 社死了"));
    Notice.Victim=Self;Notice.Killer=Rival;Shown=Line(Notice,Self,TEXT("我"),TEXT("甲"));
    TestTrue(TEXT("The player's own death says 你 and is highlighted"),Shown.bSelf&&Shown.Text==TEXT("甲 让 你 社死了"));
    Notice.Cause=ELDKillCause::CampLow;Notice.Killer.Invalidate();Shown=Line(Notice,Self,TEXT("我"),TEXT(""));
    TestTrue(TEXT("A camp death of the player says 你"),Shown.bSelf&&Shown.Text==TEXT("你 被纪律委员送去社死"));
    Notice.Cause=ELDKillCause::Contender;Notice.Victim=Other;Notice.Killer=Rival;Shown=Line(Notice,Self,TEXT("丙"),TEXT("甲"));
    TestTrue(TEXT("Other contenders keep their roster names and are not highlighted"),!Shown.bSelf&&Shown.Text==TEXT("甲 让 丙 社死了"));
    Notice.Victim=Self;
    TestFalse(TEXT("Without an own identity nothing is marked or renamed"),Line(Notice,FGuid(),TEXT("我"),TEXT("甲")).bSelf||Line(Notice,FGuid(),TEXT("我"),TEXT("甲")).Text.Contains(TEXT("你")));
    Notice.Cause=ELDKillCause::CampHigh;Notice.Victim=Other;Notice.Killer=Self;Shown=Line(Notice,Self,TEXT("丙"),TEXT("我"));
    TestTrue(TEXT("Only a contender kill can credit the player"),!Shown.bSelf&&Shown.Text==TEXT("丙 被体育老师送去社死"));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDNextStepTest,"Lockdown.Rules.HUDNextStep",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDNextStepTest::RunTest(const FString& Parameters)
{
    HudRegisterFixtureTable();
    ON_SCOPE_EXIT{FStringTableRegistry::Get().UnregisterStringTable(HudFixtureTable);};
    FLDDemoContentDefinitions Content=HudFixtureContent();
    for(FLDDemoEquipmentDefinition& Item:Content.Equipment){Item.BuyPrice=300;}
    const FGuid Self=FGuid::NewGuid();
    FLDPublicMatchView Public;Public.Generation=4;Public.Phase=ELDMatchPhase::Development;
    FLDProgressionView Growth;Growth.bValid=true;Growth.Generation=4;Growth.Level=1;
    FLDEquipmentView Wallet;Wallet.bValid=true;Wallet.Generation=4;Wallet.Gold=150;
    for(uint8 Index=0;Index<2;++Index){FLDEquipmentSlotView Slot;Slot.SlotIndex=Index;Wallet.Slots.Add(Slot);}
    auto Step=[&](){return LDHUD::NextStep(Public,Self,&Growth,&Wallet,&Content);};
    TestEqualSensitive(TEXT("A fresh contender is pointed at the camps"),Step(),FString(TEXT("下一步：去打附近的野怪拿经验和零花钱（右键攻击）")));
    Wallet.Gold=320;
    TestEqualSensitive(TEXT("Affordable gear for an empty slot names the approved item"),Step(),FString(TEXT("下一步：零花钱够买「《五年挨打三年模拟》」，去小卖部")));
    Wallet.Slots[0].bOccupied=true;Wallet.Slots[1].bOccupied=true;Growth.Level=3;
    TestTrue(TEXT("Full slots and a grown contender need no hint"),Step().IsEmpty());
    Growth.PendingChoiceCount=2;
    TestEqualSensitive(TEXT("A pending growth choice comes first"),Step(),FString(TEXT("下一步：选择成长 ×2（点技能栏上方的金色提示）")));
    Growth.PendingChoiceCount=0;Public.Phase=ELDMatchPhase::CaptureOpen;
    TestEqualSensitive(TEXT("An open capture point is the next goal"),Step(),FString(TEXT("下一步：升旗台已开放，去中心抢圈")));
    Public.CircleState=ELDCircleState::Solo;Public.Occupier=Self;
    TestEqualSensitive(TEXT("The own occupation is encouraged instead"),Step(),FString(TEXT("坚持住：你正在占领升旗台")));
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
        // The HUD reads hits from the same own-body changes the world feedback floats: lost 面子 or absorbed 淡定.
        LDWorldFeedback::FTracker Hits;FLDCombatOwnerView Combat;Combat.Generation=4;Combat.Life=2;Combat.bHasBody=true;Combat.Revision=1;Combat.HPP2=50000;Combat.MaxHPP2=60000;
        FLDCombatStatusView Shield;Shield.StatusId=uint8(ELDDemoStatusId::Shield);Shield.ExpiresAtMs=90000;Shield.AmountP2=3000;Combat.Statuses.Add(Shield);Combat.ShieldP2=3000;Combat.AsOfMs=1000;
        LDWorldFeedback::FSample Body;Body.Match=4;Body.Life=2;Body.LifePhase=ELDLifePhase::Alive;Body.OwnCombat=&Combat;
        auto Observe=[&Hits,&Body](){TArray<LDWorldFeedback::FEvent> Events;Hits.Observe(Body,Events);return Events;};
        auto IsHit=[](const TArray<LDWorldFeedback::FEvent>& Events){return Events.ContainsByPredicate([](const LDWorldFeedback::FEvent& Event)
            {return Event.bOwn&&(Event.Kind==LDWorldFeedback::EKind::DamageTaken||Event.Kind==LDWorldFeedback::EKind::ShieldAbsorbed);});};
        TestFalse(TEXT("The first own-body sample only primes"),IsHit(Observe()));
        Combat.Revision=2;Combat.ShieldP2=1000;Combat.Statuses[0].AmountP2=1000;Combat.AsOfMs=1500;
        TestTrue(TEXT("A hit fully absorbed by 淡定 is a hit"),IsHit(Observe()));
        Combat.Revision=3;Combat.ShieldP2=0;Combat.Statuses.Reset();Combat.HPP2=48000;Combat.AsOfMs=2000;
        TestTrue(TEXT("Lost 面子 is a hit"),IsHit(Observe()));
        Combat.Revision=4;Combat.HPP2=48100;Combat.AsOfMs=3000;
        TestFalse(TEXT("Regeneration is not a hit"),IsHit(Observe()));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDDevTestPanelTest,"Lockdown.Rules.HUDDevTestPanel",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDDevTestPanelTest::RunTest(const FString& Parameters)
{
    HudRegisterFixtureTable();
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
    TestTrue(TEXT("Fields name the stat in player terms with the entry unit"),Crit&&Range&&LDDevPanel::StatLabel(*Crit)==TEXT("正中要害率（百分点）")
        &&LDDevPanel::StatLabel(*Range).EndsWith(TEXT("（厘米）")));

    // Catalog rows: skills then attribute cards, with what the own progression holds.
    FLDDemoContentDefinitions Content=HudFixtureContent();
    FLDDemoAbilityDefinition Active;Active.Id=TEXT("root");Active.Kind=ELDDemoAbilityKind::Active;Content.Abilities.Add(Active);
    FLDDemoAbilityDefinition Passive;Passive.Id=TEXT("passive_battle_rhythm");Passive.Kind=ELDDemoAbilityKind::Passive;Content.Abilities.Add(Passive);
    FLDDemoAttributeDefinition Card;Card.Id=TEXT("shield");Content.Attributes.Add(Card);
    Content.Equipment[0].BuyPrice=300;
    FLDProgressionView Progression;Progression.bValid=true;Progression.Level=3;Progression.CumulativeXP=450;Progression.PendingChoiceCount=1;
    FLDLearnedSkillView Learned;Learned.DefinitionId=TEXT("root");Learned.bActive=true;Learned.Rank=2;Learned.Slot=0;Progression.Skills.Add(Learned);
    FLDAttributeCardView Cards;Cards.DefinitionId=TEXT("shield");Cards.Count=3;Progression.AttributeCards.Add(Cards);
    const TArray<LDDevPanel::FLearnRow> Rows=LDDevPanel::LearnRows(Content,&Progression);
    TestTrue(TEXT("Every skill and card of the catalog is listed in order with its kind"),Rows.Num()==3&&Rows[0].Name==TEXT("定身")&&Rows[0].bActiveSkill
        &&Rows[1].Kind==TEXT("被动技能")&&Rows[2].Kind==TEXT("属性卡"));
    TestTrue(TEXT("Held ranks and card counts come from the own progression"),LDDevPanel::HeldText(Rows[0])==TEXT("2 级")
        &&LDDevPanel::HeldText(Rows[1])==TEXT("未拥有")&&LDDevPanel::HeldText(Rows[2])==TEXT("已有 3 张"));
    TestFalse(TEXT("One held active does not need a replacement"),LDDevPanel::BothActiveSlotsHeld(&Progression));
    Learned.Slot=1;Learned.DefinitionId=TEXT("other_active");Progression.Skills.Add(Learned);
    TestTrue(TEXT("Q and W both held offer the confirmed replacement"),LDDevPanel::BothActiveSlotsHeld(&Progression));
    TestFalse(TEXT("An unprojected progression offers nothing"),LDDevPanel::BothActiveSlotsHeld(nullptr));
    const TArray<LDDevPanel::FItemRow> Items=LDDevPanel::ItemRows(Content);
    TestTrue(TEXT("Items list their catalog price"),Items.Num()==1&&Items[0].Name==TEXT("《五年挨打三年模拟》")&&Items[0].Price==300);

    // Own state lines: an unprojected view reads as syncing, never as 0.
    TestEqualSensitive(TEXT("Nothing projected is syncing"),LDDevPanel::StatusLine(nullptr,nullptr),FString(TEXT("等级 同步中 · 零花钱 同步中")));
    FLDEquipmentView Equipment;Equipment.bValid=true;Equipment.Gold=275;
    FLDEquipmentSlotView First;First.SlotIndex=0;First.bOccupied=true;First.DefinitionId=TEXT("item_iron_plate");Equipment.Slots.Add(First);
    FLDEquipmentSlotView Second;Second.SlotIndex=1;Equipment.Slots.Add(Second);
    TestEqualSensitive(TEXT("Level, XP, unused choices and gold"),LDDevPanel::StatusLine(&Progression,&Equipment),FString(TEXT("等级 3 · 经验 450 · 未用选择机会 1 · 零花钱 275")));
    TestTrue(TEXT("Slots show the held item or empty"),LDDevPanel::SlotLine(&Equipment,&Content,1)==TEXT("装备 1：《五年挨打三年模拟》")
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
    TestEqualSensitive(TEXT("An applied receipt says so"),LDDevPanel::ResultLine(Result),FString(TEXT("加零花钱 已生效")));
    Result.Receipt.Header.Outcome=ELDReceiptOutcome::Rejected;Result.Receipt.Header.Reason=uint16(ELDGrowthReason::EquipmentFull);
    TestTrue(TEXT("A refused receipt names the op and its reason"),LDDevPanel::ResultLine(Result).StartsWith(TEXT("加零花钱 被拒绝（")));
    Result.Status=ELDAdmissionStatus::AcceptedPending;Result.bHasReceipt=false;
    TestTrue(TEXT("An admitted command waits for its commit"),LDDevPanel::ResultLine(Result).StartsWith(TEXT("已受理")));
    return true;
}
#endif
