#include "UI/LDHUDModel.h"
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
    for(const TCHAR* Id:{TEXT("root"),TEXT("respawn_protection"),TEXT("camp_returning"),TEXT("monster_guardian"),TEXT("item_iron_plate")})
    {FLDDemoDisplayDefinition Display;Display.Id=Id;Display.Namespace=HudFixtureTable.ToString();Display.NameKey=FString(Id)+TEXT(".name");Display.DescriptionKey=FString(Id)+TEXT(".description");Content.Display.Add(Display);}
    FLDDemoMonsterDefinition Monster;Monster.Id=TEXT("monster_guardian");Monster.NativeId=uint8(ELDDemoMonsterId::MonsterGuardian);Content.Monsters.Add(Monster);
    FLDDemoEquipmentDefinition Item;Item.Id=TEXT("item_iron_plate");Item.NativeId=uint8(ELDDemoEquipmentId::ItemIronPlate);Content.Equipment.Add(Item);
    return Content;
}
void HudRegisterFixtureTable()
{
    FStringTableRef Table=FStringTable::NewStringTable();Table->SetNamespace(HudFixtureTable.ToString());
    Table->SetSourceString(TEXT("root.name"),TEXT("定身"),FString());Table->SetSourceString(TEXT("respawn_protection.name"),TEXT("复活保护"),FString());
    Table->SetSourceString(TEXT("camp_returning.name"),TEXT("回岗中"),FString());Table->SetSourceString(TEXT("monster_guardian.name"),TEXT("体育老师"),FString());
    Table->SetSourceString(TEXT("item_iron_plate.name"),TEXT("《五年挨打三年模拟》"),FString());
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
    const FVector World(-1700,-1000,200);const auto Fraction=LDHUD::MapPoint(World,Ground);const auto Back=LDHUD::MapWorld(Fraction,Ground);
    TestTrue(TEXT("Map conversion round trips XY and deliberately grants no height or dynamic target"),FVector::Dist2D(World,Back)<.001&&Back.Z==0);
    const FVector Edge=LDHUD::MapWorld(FVector2D(5,-3),Ground);
    TestTrue(TEXT("Map edge clamp stays inside the registered ground"),Edge.X>=Ground.Min.X&&Edge.X<Ground.Max.X&&Edge.Y>Ground.Min.Y&&Edge.Y<=Ground.Max.Y);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDLifeStatusTest,"Lockdown.Rules.HUDLifeStatus",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDLifeStatusTest::RunTest(const FString& Parameters)
{
    FLDOwnerState Own;Own.LifePhase=ELDLifePhase::Alive;Own.ProtectedUntilMs=5000;
    TestEqualSensitive(TEXT("Protection counts down from the matching authority clock"),LDHUD::LifeStatus(Own,true,3000),FString(TEXT("复活保护 2.0s · 发起进攻即解除")));
    TestEqualSensitive(TEXT("A live protection never reads zero before the clock reaches its deadline"),LDHUD::LifeStatus(Own,true,4999),FString(TEXT("复活保护 0.1s · 发起进攻即解除")));
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
    FLDPublicMatchView Public;Public.Generation=4;Public.Phase=ELDMatchPhase::Development;Public.OpenAtMs=180000;
    TestEqualSensitive(TEXT("Development counts down to the public opening in whole seconds rounded up"),LDHUD::Circle(Public,FString(),true,60500).Line,FString(TEXT("发育期 · 升旗台 2:00 后开放")));
    TestEqualSensitive(TEXT("A running countdown never reads 0:00 before the opening"),LDHUD::Clock(1),FString(TEXT("0:01")));
    TestEqualSensitive(TEXT("Without the authority clock the opening time is not guessed"),LDHUD::Circle(Public,FString(),false,0).Line,FString(TEXT("发育期 · 升旗台尚未开放")));
    Public.Phase=ELDMatchPhase::CaptureOpen;Public.CircleState=ELDCircleState::Empty;
    TestEqualSensitive(TEXT("An open empty circle has no occupier"),LDHUD::Circle(Public,FString(),true,0).Line,FString(TEXT("升旗台已开放 · 无人占领")));
    Public.CircleState=ELDCircleState::Contested;
    const LDHUD::FCircleView Contested=LDHUD::Circle(Public,TEXT("同学甲"),true,0);
    TestTrue(TEXT("Contesting shows the reset without naming anyone or showing progress"),Contested.Line==TEXT("升旗台争夺中 · 占领进度清零")&&!Contested.bProgress);
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
    FLDDemoEffectDefinition Damage;Damage.Opcode=uint8(ELDDemoEffectOp::Damage);Damage.BaseP2=7000;Damage.APRatioBp=6500;Damage.DamageType=ELDDemoDamageType::Magical;
    TestEqualSensitive(TEXT("Magic damage uses the approved term and its AP scaling"),LDHUD::EffectLine(Damage),FString(TEXT("社死伤害 70 + 65%缺德值")));
    FLDDemoEffectDefinition Root;Root.Opcode=uint8(ELDDemoEffectOp::Root);Root.DurationMs=1300;
    TestEqualSensitive(TEXT("Control duration is shown in seconds"),LDHUD::EffectLine(Root),FString(TEXT("定身 1.3s")));
    FLDDemoEffectDefinition Unknown;Unknown.Opcode=200;
    TestEqualSensitive(TEXT("An unknown effect is reported, never described"),LDHUD::EffectLine(Unknown),FString(TEXT("未知效果 #200")));
    FLDDemoRankDefinition Rank;Rank.Rank=1;Rank.ManaCostP2=4500;Rank.CooldownMs=8000;Rank.Effects.Add(Damage);
    const TArray<FString> Lines=LDHUD::RankLines(Rank,true);
    TestTrue(TEXT("Active rank lines carry the effect then mana and cooldown"),Lines.Num()==2&&Lines[1]==TEXT("胆量 45 · 冷却 8s"));
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
#endif
