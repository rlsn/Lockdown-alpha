#include "UI/LDWorldLabels.h"
#include "UI/LDHUDModel.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Development/LDDemoContentIds.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "Vision/LDViewTypes.h"
#include "Internationalization/StringTableCore.h"
#include "Internationalization/StringTableRegistry.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace
{
// A private table id keeps the fixture away from the real DemoContent display table and the HUD rules fixture.
const FName WorldLabelFixtureTable(TEXT("LDWorldLabelsDisplayFixture"));
FLDDemoContentDefinitions WorldLabelFixtureContent()
{
    FLDDemoContentDefinitions Content;
    for(const TCHAR* Id:{TEXT("root"),TEXT("shield"),TEXT("respawn_protection"),TEXT("camp_returning"),TEXT("monster_scavenger"),TEXT("monster_guardian"),TEXT("item_iron_plate")})
    {FLDDemoDisplayDefinition Display;Display.Id=Id;Display.Namespace=WorldLabelFixtureTable.ToString();Display.NameKey=FString(Id)+TEXT(".name");Display.DescriptionKey=FString(Id)+TEXT(".description");Content.Display.Add(Display);}
    FLDDemoMonsterDefinition Scavenger;Scavenger.Id=TEXT("monster_scavenger");Scavenger.NativeId=uint8(ELDDemoMonsterId::MonsterScavenger);Content.Monsters.Add(Scavenger);
    FLDDemoMonsterDefinition Guardian;Guardian.Id=TEXT("monster_guardian");Guardian.NativeId=uint8(ELDDemoMonsterId::MonsterGuardian);Content.Monsters.Add(Guardian);
    FLDDemoEquipmentDefinition Item;Item.Id=TEXT("item_iron_plate");Item.NativeId=uint8(ELDDemoEquipmentId::ItemIronPlate);Content.Equipment.Add(Item);
    return Content;
}
void WorldLabelRegisterFixtureTable()
{
    FStringTableRef Table=FStringTable::NewStringTable();Table->SetNamespace(WorldLabelFixtureTable.ToString());
    const TPair<const TCHAR*,const TCHAR*> Rows[]={{TEXT("root.name"),TEXT("定身")},{TEXT("shield.name"),TEXT("淡定")},
        {TEXT("respawn_protection.name"),TEXT("复活保护")},{TEXT("camp_returning.name"),TEXT("回岗中")},
        {TEXT("monster_scavenger.name"),TEXT("纪律委员")},{TEXT("monster_guardian.name"),TEXT("体育老师")},
        {TEXT("item_iron_plate.name"),TEXT("《五年挨打三年模拟》")}};
    for(const TPair<const TCHAR*,const TCHAR*>& Row:Rows)
    {
        // Development game targets also compile these tests; only editor builds have the dev-notes overload.
#if WITH_EDITORONLY_DATA
        Table->SetSourceString(Row.Key,Row.Value,FString());
#else
        Table->SetSourceString(Row.Key,Row.Value);
#endif
    }
    FStringTableRegistry::Get().RegisterStringTable(WorldLabelFixtureTable,Table);
}
FLDObservedStatus WorldLabelStatus(ELDDemoStatusId Id,int64 ExpiresAtServerMs,uint8 Stacks=1)
{FLDObservedStatus Status;Status.StatusId=uint8(Id);Status.ExpiresAtServerMs=ExpiresAtServerMs;Status.Stacks=Stacks;return Status;}
// A visible rival with every target-card field set, so any leak into the label would show.
FLDObservedUnitView WorldLabelRival()
{
    FLDObservedUnitView Unit;Unit.Handle=FGuid::NewGuid();Unit.ObservationEpoch=3;Unit.StateRevision=1;Unit.Kind=ELDObservedUnitKind::Contender;
    Unit.AuthorizedFieldMask=LDObservation::ContenderFields;Unit.Presentation=2;Unit.ContenderId=FGuid::NewGuid();Unit.Level=7;
    Unit.Health.CurrentP2=45001;Unit.Health.MaxP2=60000;Unit.Mana.CurrentP2=12099;Unit.Mana.MaxP2=20000;
    Unit.Equipment.Slots[0]=uint8(ELDDemoEquipmentId::ItemIronPlate);
    return Unit;
}
FLDObservedUnitView WorldLabelCampMember(ELDDemoMonsterId Creature)
{
    FLDObservedUnitView Unit;Unit.Handle=FGuid::NewGuid();Unit.ObservationEpoch=4;Unit.StateRevision=1;Unit.Kind=ELDObservedUnitKind::CampMember;
    Unit.AuthorizedFieldMask=LDObservation::CampFields;Unit.CreatureId=uint8(Creature);Unit.Health.CurrentP2=100;Unit.Health.MaxP2=200;
    return Unit;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDWorldLabelsContenderNameTest,"Lockdown.Rules.WorldLabels.ContenderName",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDWorldLabelsContenderNameTest::RunTest(const FString& Parameters)
{
    WorldLabelRegisterFixtureTable();
    ON_SCOPE_EXIT{FStringTableRegistry::Get().UnregisterStringTable(WorldLabelFixtureTable);};
    const FLDDemoContentDefinitions Content=WorldLabelFixtureContent();
    const FLDObservedUnitView Rival=WorldLabelRival();
    FLDLobbyView Lobby;FLDLobbyMember Member;Member.ContenderId=Rival.ContenderId;Member.Name=TEXT("同学甲");Lobby.Members.Add(Member);
    const LDWorldLabels::FLabel Listed=LDWorldLabels::Build(Rival,LDHUD::RosterName(Lobby,Rival.ContenderId),&Content,10.);
    TestEqualSensitive(TEXT("A visible contender is labelled with its public roster name"),Listed.Name,FString(TEXT("同学甲")));
    TestTrue(TEXT("A contender label keeps its presentation key for the identity tint and no creature"),Listed.bContender&&Listed.Presentation==Rival.Presentation&&Listed.CreatureId==0);
    TestTrue(TEXT("Without public statuses there is no status line, not a filler"),Listed.Status.IsEmpty());
    FLDObservedUnitView Unlisted=WorldLabelRival();Unlisted.Statuses.Add(WorldLabelStatus(ELDDemoStatusId::RespawnProtection,12000));
    const FString Roster=LDHUD::RosterName(Lobby,Unlisted.ContenderId);
    const LDWorldLabels::FLabel Missing=LDWorldLabels::Build(Unlisted,Roster,&Content,10.);
    TestTrue(TEXT("A contender the public roster does not list gets an empty name, never an invented or placeholder one"),Roster.IsEmpty()&&Missing.Name.IsEmpty()&&Missing.bContender);
    TestEqualSensitive(TEXT("The unlisted contender's public statuses are still labelled"),Missing.Status,FString(TEXT("复活保护 2.0s")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDWorldLabelsCampMemberNameTest,"Lockdown.Rules.WorldLabels.CampMemberName",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDWorldLabelsCampMemberNameTest::RunTest(const FString& Parameters)
{
    WorldLabelRegisterFixtureTable();
    ON_SCOPE_EXIT{FStringTableRegistry::Get().UnregisterStringTable(WorldLabelFixtureTable);};
    const FLDDemoContentDefinitions Content=WorldLabelFixtureContent();
    const LDWorldLabels::FLabel Teacher=LDWorldLabels::Build(WorldLabelCampMember(ELDDemoMonsterId::MonsterGuardian),TEXT("同学甲"),&Content,0.);
    TestEqualSensitive(TEXT("A camp member is labelled with its approved display name, never a roster name"),Teacher.Name,FString(TEXT("体育老师")));
    TestTrue(TEXT("A camp member label carries its creature and no contender tint"),!Teacher.bContender&&Teacher.CreatureId==uint8(ELDDemoMonsterId::MonsterGuardian)&&Teacher.Presentation==0);
    TestEqualSensitive(TEXT("The low tier has its own approved display name"),LDWorldLabels::Build(WorldLabelCampMember(ELDDemoMonsterId::MonsterScavenger),FString(),&Content,0.).Name,FString(TEXT("纪律委员")));
    const uint8 Unapproved=9;
    FLDObservedUnitView Unknown=WorldLabelCampMember(ELDDemoMonsterId::MonsterGuardian);Unknown.CreatureId=Unapproved;
    const FString UnknownName=LDWorldLabels::Build(Unknown,FString(),&Content,0.).Name;
    TestTrue(TEXT("An unapproved creature reads as the HUD reports it, never as a guessed approved name"),
        UnknownName==LDHUD::CreatureName(&Content,Unapproved)&&!UnknownName.IsEmpty()&&UnknownName!=TEXT("体育老师")&&UnknownName!=TEXT("纪律委员"));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDWorldLabelsStatusLineTest,"Lockdown.Rules.WorldLabels.StatusLine",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDWorldLabelsStatusLineTest::RunTest(const FString& Parameters)
{
    WorldLabelRegisterFixtureTable();
    ON_SCOPE_EXIT{FStringTableRegistry::Get().UnregisterStringTable(WorldLabelFixtureTable);};
    const FLDDemoContentDefinitions Content=WorldLabelFixtureContent();
    auto Status=[&Content](const FLDObservedUnitView& Unit,double SampledAtServerSec){return LDWorldLabels::Build(Unit,TEXT("同学甲"),&Content,SampledAtServerSec).Status;};
    FLDObservedUnitView Rival=WorldLabelRival();Rival.Statuses.Add(WorldLabelStatus(ELDDemoStatusId::RespawnProtection,11401));
    TestEqualSensitive(TEXT("Respawn protection shows its remaining time rounded up to a tenth"),Status(Rival,10.),FString(TEXT("复活保护 1.5s")));
    Rival.Statuses[0].ExpiresAtServerMs=11400;
    TestEqualSensitive(TEXT("An exact tenth is not rounded further"),Status(Rival,10.),FString(TEXT("复活保护 1.4s")));
    Rival.Statuses[0].ExpiresAtServerMs=10001;
    TestEqualSensitive(TEXT("A live protection never reads zero"),Status(Rival,10.),FString(TEXT("复活保护 0.1s")));
    Rival.Statuses[0].ExpiresAtServerMs=11500;
    TestEqualSensitive(TEXT("The remainder counts from the frame's own authority sample"),Status(Rival,10.25),FString(TEXT("复活保护 1.3s")));
    FLDObservedUnitView Mixed=WorldLabelRival();
    Mixed.Statuses.Add(WorldLabelStatus(ELDDemoStatusId::Root,12500));
    Mixed.Statuses.Add(WorldLabelStatus(ELDDemoStatusId::Shield,11000,2));
    Mixed.Statuses.Add(WorldLabelStatus(ELDDemoStatusId::RespawnProtection,11401));
    const FString MixedLine=Status(Mixed,10.);
    TestEqualSensitive(TEXT("Public statuses keep frame order with approved names, stacks and remaining time"),MixedLine,FString(TEXT("定身 2.5s · 淡定 ×2 1.0s · 复活保护 1.5s")));
    TestEqualSensitive(TEXT("The label reads exactly like the target card's status line"),TEXT("状态：")+MixedLine,LDHUD::ObservedCard(Mixed,TEXT("同学甲"),&Content,10.).Statuses);
    FLDObservedUnitView Camp=WorldLabelCampMember(ELDDemoMonsterId::MonsterScavenger);Camp.Statuses.Add(WorldLabelStatus(ELDDemoStatusId::CampReturning,0));
    TestEqualSensitive(TEXT("An untimed status shows just its approved name"),Status(Camp,10.),FString(TEXT("回岗中")));
    Camp.Statuses.Add(WorldLabelStatus(ELDDemoStatusId::Root,10800));
    TestEqualSensitive(TEXT("Untimed and timed statuses share one line in frame order"),Status(Camp,10.),FString(TEXT("回岗中 · 定身 0.8s")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDWorldLabelsWhitelistTest,"Lockdown.Rules.WorldLabels.Whitelist",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDWorldLabelsWhitelistTest::RunTest(const FString& Parameters)
{
    WorldLabelRegisterFixtureTable();
    ON_SCOPE_EXIT{FStringTableRegistry::Get().UnregisterStringTable(WorldLabelFixtureTable);};
    const FLDDemoContentDefinitions Content=WorldLabelFixtureContent();
    FLDObservedUnitView Rival=WorldLabelRival();Rival.Statuses.Add(WorldLabelStatus(ELDDemoStatusId::Root,12500));
    const LDWorldLabels::FLabel Label=LDWorldLabels::Build(Rival,TEXT("同学甲"),&Content,10.);
    const FString Shown=Label.Name+TEXT("\n")+Label.Status;
    TestEqualSensitive(TEXT("A label is the name and the public statuses only"),Shown,FString(TEXT("同学甲\n定身 2.5s")));
    for(const TCHAR* Private:{TEXT("等级"),TEXT("面子"),TEXT("胆量"),TEXT("装备"),TEXT("空槽"),TEXT("《五年挨打三年模拟》"),TEXT("451"),TEXT("600"),TEXT("120"),TEXT("200"),TEXT("零花钱"),TEXT("经验"),TEXT("冷却"),TEXT("技能")})
    {TestFalse(FString::Printf(TEXT("A world label shows no non-whitelisted field: %s"),Private),Shown.Contains(Private,ESearchCase::CaseSensitive));}
    // Everything outside the label's subset changes: level, resources, equipment, pose, observation identity and a
    // stray creature id on a contender. None of it may reach the label.
    FLDObservedUnitView Changed=Rival;
    Changed.Handle=FGuid::NewGuid();Changed.ObservationEpoch=9;Changed.StateRevision=77;Changed.Level=10;
    Changed.Health.CurrentP2=1;Changed.Health.MaxP2=99999;Changed.Mana.CurrentP2=0;Changed.Mana.MaxP2=1;
    Changed.Equipment.Slots[0]=0;Changed.Equipment.Slots[1]=uint8(ELDDemoEquipmentId::ItemIronPlate);
    Changed.FeetCm=FVector(1234,-567,89);Changed.Facing=4321;Changed.CreatureId=uint8(ELDDemoMonsterId::MonsterGuardian);
    const LDWorldLabels::FLabel Same=LDWorldLabels::Build(Changed,TEXT("同学甲"),&Content,10.);
    TestTrue(TEXT("Fields outside the label never change it"),
        Same.Name==Label.Name&&Same.Status==Label.Status&&Same.bContender==Label.bContender&&Same.Presentation==Label.Presentation&&Same.CreatureId==0);
    FLDObservedUnitView Camp=WorldLabelCampMember(ELDDemoMonsterId::MonsterGuardian);Camp.Presentation=3;Camp.ContenderId=Rival.ContenderId;
    const LDWorldLabels::FLabel CampLabel=LDWorldLabels::Build(Camp,TEXT("同学甲"),&Content,10.);
    TestTrue(TEXT("A camp member never takes a contender's name or tint"),CampLabel.Name==TEXT("体育老师")&&CampLabel.Presentation==0&&!CampLabel.bContender);
    return true;
}
#endif
