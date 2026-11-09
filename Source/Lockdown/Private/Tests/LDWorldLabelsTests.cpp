#include "UI/LDWorldLabels.h"
#include "UI/LDHUDModel.h"
#include "UI/LDTerms.h"
#include "Combat/LDCombatTypes.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Development/LDDemoContentIds.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "Vision/LDViewTypes.h"
#include "Internationalization/StringTableCore.h"
#include "Internationalization/StringTableRegistry.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Tests/LDTestApprovedDisplay.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace
{
// A private table id keeps the fixture away from the real DemoContent display table and the HUD rules fixture. Its names
// are the approved catalog's (#111), so expectations read them through WorldLabelName instead of repeating them.
const FName WorldLabelFixtureTable(TEXT("LDWorldLabelsDisplayFixture"));
const TCHAR* const WorldLabelFixtureIds[]={TEXT("root"),TEXT("shield"),TEXT("respawn_protection"),TEXT("camp_returning"),TEXT("monster_scavenger"),TEXT("monster_guardian")};
TMap<FString,FString> WorldLabelFixtureNames;
FString WorldLabelName(const TCHAR* Id)
{
    const FString* Name=WorldLabelFixtureNames.Find(Id);
    return Name?*Name:FString(Id);
}
FLDDemoContentDefinitions WorldLabelFixtureContent()
{
    FLDDemoContentDefinitions Content;
    for(const TCHAR* Id:WorldLabelFixtureIds)
    {FLDDemoDisplayDefinition Display;Display.Id=Id;Display.Namespace=WorldLabelFixtureTable.ToString();Display.NameKey=FString(Id)+TEXT(".name");Display.DescriptionKey=FString(Id)+TEXT(".description");Content.Display.Add(Display);}
    FLDDemoMonsterDefinition Scavenger;Scavenger.Id=TEXT("monster_scavenger");Scavenger.NativeId=uint8(ELDDemoMonsterId::MonsterScavenger);Content.Monsters.Add(Scavenger);
    FLDDemoMonsterDefinition Guardian;Guardian.Id=TEXT("monster_guardian");Guardian.NativeId=uint8(ELDDemoMonsterId::MonsterGuardian);Content.Monsters.Add(Guardian);
    return Content;
}
bool WorldLabelRegisterFixtureTable(FAutomationTestBase& Test)
{
    if(!LDTestApprovedDisplay::Names(Test,WorldLabelFixtureNames)){return false;}
    FStringTableRef Table=FStringTable::NewStringTable();Table->SetNamespace(WorldLabelFixtureTable.ToString());
    for(const TCHAR* Id:WorldLabelFixtureIds)
    {
        const FString* Name=WorldLabelFixtureNames.Find(Id);
        if(!Name){Test.AddError(FString::Printf(TEXT("The approved catalog has no display name for %s."),Id));return false;}
        const FString Key=FString(Id)+TEXT(".name");
        // Development game targets also compile these tests; only editor builds have the dev-notes overload.
#if WITH_EDITORONLY_DATA
        Table->SetSourceString(FTextKey(*Key),*Name,FString());
#else
        Table->SetSourceString(FTextKey(*Key),*Name);
#endif
    }
    FStringTableRegistry::Get().RegisterStringTable(WorldLabelFixtureTable,Table);
    return true;
}
FLDObservedStatus WorldLabelStatus(ELDDemoStatusId Id,int64 ExpiresAtServerMs,uint8 Stacks=1)
{FLDObservedStatus Status;Status.StatusId=uint8(Id);Status.ExpiresAtServerMs=ExpiresAtServerMs;Status.Stacks=Stacks;return Status;}
// A visible rival with every target-card field set, so any leak into the label would show.
FLDObservedUnitView WorldLabelRival()
{
    FLDObservedUnitView Unit;Unit.Handle=FGuid::NewGuid();Unit.ObservationEpoch=3;Unit.StateRevision=1;Unit.Kind=ELDObservedUnitKind::Contender;
    Unit.AuthorizedFieldMask=LDObservation::ContenderFields;Unit.Presentation=2;Unit.ContenderId=FGuid::NewGuid();Unit.Level=7;
    Unit.Health.CurrentP2=45001;Unit.Health.MaxP2=60000;Unit.Mana.CurrentP2=12099;Unit.Mana.MaxP2=20000;
    return Unit;
}
FLDObservedUnitView WorldLabelCampMember(ELDDemoMonsterId Creature)
{
    FLDObservedUnitView Unit;Unit.Handle=FGuid::NewGuid();Unit.ObservationEpoch=4;Unit.StateRevision=1;Unit.Kind=ELDObservedUnitKind::CampMember;
    Unit.AuthorizedFieldMask=LDObservation::CampFields;Unit.CreatureId=uint8(Creature);Unit.Health.CurrentP2=100;Unit.Health.MaxP2=200;
    return Unit;
}
// Every label field, so a test tells an unchanged label from one that changed.
bool WorldLabelSame(const LDWorldLabels::FLabel& A,const LDWorldLabels::FLabel& B)
{
    return A.Name.Equals(B.Name,ESearchCase::CaseSensitive)&&A.Status.Equals(B.Status,ESearchCase::CaseSensitive)&&A.bContender==B.bContender
        &&A.Presentation==B.Presentation&&A.CreatureId==B.CreatureId&&A.Level==B.Level&&A.bHealth==B.bHealth&&A.HealthFraction==B.HealthFraction&&A.bShield==B.bShield;
}
// The local owner alive in public match 5, life 2, and its own combat projection of that life with a body.
FLDOwnerState WorldLabelOwner()
{FLDOwnerState Own;Own.Match=5;Own.Life=2;Own.LifePhase=ELDLifePhase::Alive;return Own;}
FLDCombatOwnerView WorldLabelOwnCombat()
{
    FLDCombatOwnerView Combat;Combat.Generation=5;Combat.Life=2;Combat.bHasBody=true;
    Combat.HPP2=30000;Combat.MaxHPP2=60000;Combat.MPP2=5000;Combat.MaxMPP2=20000;
    return Combat;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDWorldLabelsContenderNameTest,"Lockdown.Rules.WorldLabels.ContenderName",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDWorldLabelsContenderNameTest::RunTest(const FString& Parameters)
{
    if(!WorldLabelRegisterFixtureTable(*this)){return false;}
    ON_SCOPE_EXIT{FStringTableRegistry::Get().UnregisterStringTable(WorldLabelFixtureTable);};
    const FLDDemoContentDefinitions Content=WorldLabelFixtureContent();
    const FLDObservedUnitView Rival=WorldLabelRival();
    FLDLobbyView Lobby;FLDLobbyMember Member;Member.ContenderId=Rival.ContenderId;Member.Name=TEXT("玩家甲");Lobby.Members.Add(Member);
    const LDWorldLabels::FLabel Listed=LDWorldLabels::Build(Rival,LDHUD::RosterName(Lobby,Rival.ContenderId),&Content,10.);
    TestEqualSensitive(TEXT("A visible contender is labelled with its public roster name"),Listed.Name,FString(TEXT("玩家甲")));
    TestTrue(TEXT("A contender label keeps its presentation key for the identity tint and no creature"),Listed.bContender&&Listed.Presentation==Rival.Presentation&&Listed.CreatureId==0);
    TestTrue(TEXT("Without public statuses there is no status line, not a filler"),Listed.Status.IsEmpty());
    FLDObservedUnitView Unlisted=WorldLabelRival();Unlisted.Statuses.Add(WorldLabelStatus(ELDDemoStatusId::RespawnProtection,12000));
    const FString Roster=LDHUD::RosterName(Lobby,Unlisted.ContenderId);
    const LDWorldLabels::FLabel Missing=LDWorldLabels::Build(Unlisted,Roster,&Content,10.);
    TestTrue(TEXT("A contender the public roster does not list gets an empty name, never an invented or placeholder one"),Roster.IsEmpty()&&Missing.Name.IsEmpty()&&Missing.bContender);
    TestEqualSensitive(TEXT("The unlisted contender's public statuses are still labelled"),Missing.Status,WorldLabelName(TEXT("respawn_protection"))+TEXT(" 2.0s"));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDWorldLabelsCampMemberNameTest,"Lockdown.Rules.WorldLabels.CampMemberName",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDWorldLabelsCampMemberNameTest::RunTest(const FString& Parameters)
{
    if(!WorldLabelRegisterFixtureTable(*this)){return false;}
    ON_SCOPE_EXIT{FStringTableRegistry::Get().UnregisterStringTable(WorldLabelFixtureTable);};
    const FLDDemoContentDefinitions Content=WorldLabelFixtureContent();
    const LDWorldLabels::FLabel Teacher=LDWorldLabels::Build(WorldLabelCampMember(ELDDemoMonsterId::MonsterGuardian),TEXT("玩家甲"),&Content,0.);
    TestEqualSensitive(TEXT("A camp member is labelled with its approved display name, never a roster name"),Teacher.Name,WorldLabelName(TEXT("monster_guardian")));
    TestTrue(TEXT("A camp member label carries its creature and no contender tint"),!Teacher.bContender&&Teacher.CreatureId==uint8(ELDDemoMonsterId::MonsterGuardian)&&Teacher.Presentation==0);
    TestEqualSensitive(TEXT("The low tier has its own approved display name"),LDWorldLabels::Build(WorldLabelCampMember(ELDDemoMonsterId::MonsterScavenger),FString(),&Content,0.).Name,WorldLabelName(TEXT("monster_scavenger")));
    const uint8 Unapproved=9;
    FLDObservedUnitView Unknown=WorldLabelCampMember(ELDDemoMonsterId::MonsterGuardian);Unknown.CreatureId=Unapproved;
    const FString UnknownName=LDWorldLabels::Build(Unknown,FString(),&Content,0.).Name;
    TestTrue(TEXT("An unapproved creature reads as the HUD reports it, never as a guessed approved name"),
        UnknownName==LDHUD::CreatureName(&Content,Unapproved)&&!UnknownName.IsEmpty()&&UnknownName!=WorldLabelName(TEXT("monster_guardian"))&&UnknownName!=WorldLabelName(TEXT("monster_scavenger")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDWorldLabelsStatusLineTest,"Lockdown.Rules.WorldLabels.StatusLine",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDWorldLabelsStatusLineTest::RunTest(const FString& Parameters)
{
    if(!WorldLabelRegisterFixtureTable(*this)){return false;}
    ON_SCOPE_EXIT{FStringTableRegistry::Get().UnregisterStringTable(WorldLabelFixtureTable);};
    const FLDDemoContentDefinitions Content=WorldLabelFixtureContent();
    auto Status=[&Content](const FLDObservedUnitView& Unit,double SampledAtServerSec){return LDWorldLabels::Build(Unit,TEXT("玩家甲"),&Content,SampledAtServerSec).Status;};
    FLDObservedUnitView Rival=WorldLabelRival();Rival.Statuses.Add(WorldLabelStatus(ELDDemoStatusId::RespawnProtection,11401));
    TestEqualSensitive(TEXT("Respawn protection shows its remaining time rounded up to a tenth"),Status(Rival,10.),WorldLabelName(TEXT("respawn_protection"))+TEXT(" 1.5s"));
    Rival.Statuses[0].ExpiresAtServerMs=11400;
    TestEqualSensitive(TEXT("An exact tenth is not rounded further"),Status(Rival,10.),WorldLabelName(TEXT("respawn_protection"))+TEXT(" 1.4s"));
    Rival.Statuses[0].ExpiresAtServerMs=10001;
    TestEqualSensitive(TEXT("A live protection never reads zero"),Status(Rival,10.),WorldLabelName(TEXT("respawn_protection"))+TEXT(" 0.1s"));
    Rival.Statuses[0].ExpiresAtServerMs=11500;
    TestEqualSensitive(TEXT("The remainder counts from the frame's own authority sample"),Status(Rival,10.25),WorldLabelName(TEXT("respawn_protection"))+TEXT(" 1.3s"));
    FLDObservedUnitView Mixed=WorldLabelRival();
    Mixed.Statuses.Add(WorldLabelStatus(ELDDemoStatusId::Root,12500));
    Mixed.Statuses.Add(WorldLabelStatus(ELDDemoStatusId::Shield,11000,2));
    Mixed.Statuses.Add(WorldLabelStatus(ELDDemoStatusId::RespawnProtection,11401));
    const FString MixedLine=Status(Mixed,10.);
    TestEqualSensitive(TEXT("Public statuses keep frame order with approved names, stacks and remaining time"),MixedLine,WorldLabelName(TEXT("root"))+TEXT(" 2.5s · ")+WorldLabelName(TEXT("shield"))+TEXT(" ×2 1.0s · ")+WorldLabelName(TEXT("respawn_protection"))+TEXT(" 1.5s"));
    TestEqualSensitive(TEXT("The label reads exactly like the target card's status line"),TEXT("状态：")+MixedLine,LDHUD::ObservedCard(Mixed,TEXT("玩家甲"),&Content,10.).Statuses);
    const LDWorldLabels::FLabel MixedLabel=LDWorldLabels::Build(Mixed,TEXT("玩家甲"),&Content,10.);
    FString Joined;for(const LDWorldLabels::FLabel::FStatusPart& Part:MixedLabel.StatusParts){Joined+=(Joined.IsEmpty()?TEXT(""):TEXT(" · "))+Part.Text;}
    TestTrue(TEXT("Coloured status parts are the line's own parts, each with its status, in frame order"),Joined==MixedLine&&MixedLabel.StatusParts.Num()==3
        &&MixedLabel.StatusParts[0].StatusId==uint8(ELDDemoStatusId::Root)&&MixedLabel.StatusParts[1].StatusId==uint8(ELDDemoStatusId::Shield)
        &&MixedLabel.StatusParts[2].StatusId==uint8(ELDDemoStatusId::RespawnProtection));
    FLDObservedUnitView Camp=WorldLabelCampMember(ELDDemoMonsterId::MonsterScavenger);Camp.Statuses.Add(WorldLabelStatus(ELDDemoStatusId::CampReturning,0));
    TestEqualSensitive(TEXT("An untimed status shows just its approved name"),Status(Camp,10.),WorldLabelName(TEXT("camp_returning")));
    Camp.Statuses.Add(WorldLabelStatus(ELDDemoStatusId::Root,10800));
    TestEqualSensitive(TEXT("Untimed and timed statuses share one line in frame order"),Status(Camp,10.),WorldLabelName(TEXT("camp_returning"))+TEXT(" · ")+WorldLabelName(TEXT("root"))+TEXT(" 0.8s"));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDWorldLabelsWhitelistTest,"Lockdown.Rules.WorldLabels.Whitelist",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDWorldLabelsWhitelistTest::RunTest(const FString& Parameters)
{
    if(!WorldLabelRegisterFixtureTable(*this)){return false;}
    ON_SCOPE_EXIT{FStringTableRegistry::Get().UnregisterStringTable(WorldLabelFixtureTable);};
    const FLDDemoContentDefinitions Content=WorldLabelFixtureContent();
    FLDObservedUnitView Rival=WorldLabelRival();Rival.Statuses.Add(WorldLabelStatus(ELDDemoStatusId::Root,12500));
    const LDWorldLabels::FLabel Label=LDWorldLabels::Build(Rival,TEXT("玩家甲"),&Content,10.);
    const FString Shown=Label.Name+TEXT("\n")+Label.Status;
    TestEqualSensitive(TEXT("A label's text is the name and the public statuses only"),Shown,TEXT("玩家甲\n")+WorldLabelName(TEXT("root"))+TEXT(" 2.5s"));
    for(const TCHAR* Private:{TEXT("等级"),LDTerms::Health,LDTerms::Mana,TEXT("空槽"),TEXT("451"),TEXT("600"),TEXT("120"),TEXT("200"),LDTerms::Experience,TEXT("冷却"),TEXT("技能")})
    {TestFalse(FString::Printf(TEXT("A world label shows no non-whitelisted field: %s"),Private),Shown.Contains(Private,ESearchCase::CaseSensitive));}
    TestTrue(TEXT("Beside its text a label carries only the level, the health fraction and shield presence"),
        Label.Level==7&&Label.bHealth&&Label.HealthFraction==float(45001./60000.)&&!Label.bShield);
    // Everything outside the label's subset changes: mana, pose, observation identity and a stray creature id
    // on a contender. None of it may reach the label.
    FLDObservedUnitView Changed=Rival;
    Changed.Handle=FGuid::NewGuid();Changed.ObservationEpoch=9;Changed.StateRevision=77;
    Changed.Mana.CurrentP2=0;Changed.Mana.MaxP2=1;
    Changed.FeetCm=FVector(1234,-567,89);Changed.Facing=4321;Changed.CreatureId=uint8(ELDDemoMonsterId::MonsterGuardian);
    TestTrue(TEXT("Fields outside the label never change it"),WorldLabelSame(LDWorldLabels::Build(Changed,TEXT("玩家甲"),&Content,10.),Label)&&Label.CreatureId==0);
    // Level and health are in the subset: each changes exactly its own label field and nothing else.
    FLDObservedUnitView Grown=Rival;Grown.Level=10;
    LDWorldLabels::FLabel Expected=Label;Expected.Level=10;
    TestTrue(TEXT("A new level changes only the label's level"),WorldLabelSame(LDWorldLabels::Build(Grown,TEXT("玩家甲"),&Content,10.),Expected));
    FLDObservedUnitView Hurt=Rival;Hurt.Health.CurrentP2=1;Hurt.Health.MaxP2=99999;
    Expected=Label;Expected.HealthFraction=float(1./99999.);
    TestTrue(TEXT("New health changes only the label's health fraction"),WorldLabelSame(LDWorldLabels::Build(Hurt,TEXT("玩家甲"),&Content,10.),Expected));
    FLDObservedUnitView Camp=WorldLabelCampMember(ELDDemoMonsterId::MonsterGuardian);Camp.Presentation=3;Camp.ContenderId=Rival.ContenderId;Camp.Level=5;
    const LDWorldLabels::FLabel CampLabel=LDWorldLabels::Build(Camp,TEXT("玩家甲"),&Content,10.);
    TestTrue(TEXT("A camp member never takes a contender's name, tint or level"),CampLabel.Name==WorldLabelName(TEXT("monster_guardian"))&&CampLabel.Presentation==0&&CampLabel.Level==0&&!CampLabel.bContender);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDWorldLabelsHealthBarTest,"Lockdown.Rules.WorldLabels.HealthBar",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDWorldLabelsHealthBarTest::RunTest(const FString& Parameters)
{
    if(!WorldLabelRegisterFixtureTable(*this)){return false;}
    ON_SCOPE_EXIT{FStringTableRegistry::Get().UnregisterStringTable(WorldLabelFixtureTable);};
    const FLDDemoContentDefinitions Content=WorldLabelFixtureContent();
    auto Labelled=[&Content](const FLDObservedUnitView& Unit){return LDWorldLabels::Build(Unit,TEXT("玩家甲"),&Content,10.);};
    FLDObservedUnitView Rival=WorldLabelRival();Rival.Health.CurrentP2=15000;Rival.Health.MaxP2=60000;
    const LDWorldLabels::FLabel Quarter=Labelled(Rival);
    TestTrue(TEXT("A visible contender has a health bar and its level"),Quarter.bHealth&&Quarter.Level==7);
    TestEqual(TEXT("The bar fills current over maximum health"),Quarter.HealthFraction,.25f);
    Rival.Health.CurrentP2=0;
    const LDWorldLabels::FLabel Drained=Labelled(Rival);
    TestTrue(TEXT("Known zero health is an empty bar, not a missing one"),Drained.bHealth&&Drained.HealthFraction==0.f);
    Rival.Health.CurrentP2=70000;
    TestEqual(TEXT("Health above its maximum fills the bar and never overflows it"),Labelled(Rival).HealthFraction,1.f);
    for(const int64 MaxP2:{int64(0),int64(-60000)})
    {
        Rival.Health.CurrentP2=0;Rival.Health.MaxP2=MaxP2;
        const LDWorldLabels::FLabel Unknown=Labelled(Rival);
        TestTrue(FString::Printf(TEXT("A health maximum of %lld draws no bar rather than an empty one"),MaxP2),!Unknown.bHealth&&Unknown.HealthFraction==0.f);
    }
    Rival.Health.CurrentP2=-1;Rival.Health.MaxP2=60000;
    TestFalse(TEXT("A malformed current never reads as an empty bar"),Labelled(Rival).bHealth);
    FLDObservedUnitView Camp=WorldLabelCampMember(ELDDemoMonsterId::MonsterScavenger);Camp.Level=3;
    const LDWorldLabels::FLabel CampLabel=Labelled(Camp);
    TestTrue(TEXT("A camp member gets a health bar but never a level, even a stray one"),CampLabel.bHealth&&CampLabel.Level==0);
    TestEqual(TEXT("A camp member's bar fills current over maximum health"),CampLabel.HealthFraction,.5f);
    FLDObservedUnitView Shielded=WorldLabelRival();Shielded.Statuses.Add(WorldLabelStatus(ELDDemoStatusId::Root,12500));
    TestFalse(TEXT("Another public status is no shield"),Labelled(Shielded).bShield);
    Shielded.Statuses.Add(WorldLabelStatus(ELDDemoStatusId::Shield,11000,2));
    const LDWorldLabels::FLabel Calm=Labelled(Shielded);
    TestTrue(TEXT("The public shield status marks the bar and is still named in the status line"),Calm.bShield&&Calm.Status.Contains(WorldLabelName(TEXT("shield")),ESearchCase::CaseSensitive));
    Camp.Statuses.Add(WorldLabelStatus(ELDDemoStatusId::Shield,0));
    TestTrue(TEXT("A camp member's shield status marks its bar too"),Labelled(Camp).bShield);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDWorldLabelsOwnBarsTest,"Lockdown.Rules.WorldLabels.OwnBars",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDWorldLabelsOwnBarsTest::RunTest(const FString& Parameters)
{
    const FLDOwnerState Own=WorldLabelOwner();const FLDCombatOwnerView Combat=WorldLabelOwnCombat();
    const LDWorldLabels::FOwnBars Bars=LDWorldLabels::OwnBars(Own,5,Combat);
    TestTrue(TEXT("The own body's bars show for its own current match, life and body"),Bars.bVisible&&Bars.bMana);
    TestEqual(TEXT("Health fills its share of the bar"),Bars.Health,.5f);
    TestEqual(TEXT("Without a shield nothing follows the health fill"),Bars.Shield,0.f);
    TestEqual(TEXT("Mana fills its own bar"),Bars.Mana,.25f);
    auto Hidden=[](const LDWorldLabels::FOwnBars& Result){return !Result.bVisible&&!Result.bMana&&Result.Health==0.f&&Result.Shield==0.f&&Result.Mana==0.f;};
    FLDCombatOwnerView Stale=Combat;Stale.Life=1;
    TestTrue(TEXT("A projection of an earlier life hides the bars"),Hidden(LDWorldLabels::OwnBars(Own,5,Stale)));
    Stale=Combat;Stale.Generation=4;
    TestTrue(TEXT("A projection of another match hides the bars"),Hidden(LDWorldLabels::OwnBars(Own,5,Stale)));
    TestTrue(TEXT("An owner state of another public match hides the bars"),Hidden(LDWorldLabels::OwnBars(Own,6,Combat)));
    FLDOwnerState Other=Own;Other.Match=0;Stale=Combat;Stale.Generation=0;
    TestTrue(TEXT("Without a public match nothing is current"),Hidden(LDWorldLabels::OwnBars(Other,0,Stale)));
    Other=Own;Other.Life=0;Stale=Combat;Stale.Life=0;
    TestTrue(TEXT("Without a life nothing is current"),Hidden(LDWorldLabels::OwnBars(Other,5,Stale)));
    for(const ELDLifePhase Phase:{ELDLifePhase::Unspawned,ELDLifePhase::Dead,ELDLifePhase::Left})
    {
        Other=Own;Other.LifePhase=Phase;
        TestTrue(FString::Printf(TEXT("Life phase %d hides the bars"),int32(Phase)),Hidden(LDWorldLabels::OwnBars(Other,5,Combat)));
    }
    Stale=Combat;Stale.bHasBody=false;
    TestTrue(TEXT("A life without a body has no bars"),Hidden(LDWorldLabels::OwnBars(Own,5,Stale)));
    for(const int64 MaxHPP2:{int64(0),int64(-1)})
    {
        Stale=Combat;Stale.HPP2=0;Stale.MaxHPP2=MaxHPP2;
        TestTrue(FString::Printf(TEXT("A health maximum of %lld hides the bars instead of drawing them empty"),MaxHPP2),Hidden(LDWorldLabels::OwnBars(Own,5,Stale)));
    }
    Stale=Combat;Stale.HPP2=-1;
    TestTrue(TEXT("A malformed health never reads as an empty bar"),Hidden(LDWorldLabels::OwnBars(Own,5,Stale)));
    FLDCombatOwnerView Shielded=Combat;Shielded.ShieldP2=12000;
    const LDWorldLabels::FOwnBars Appended=LDWorldLabels::OwnBars(Own,5,Shielded);
    TestEqual(TEXT("The shield leaves the health fill as it is"),Appended.Health,.5f);
    TestEqual(TEXT("The shield follows the health fill at its share of the health maximum"),Appended.Shield,.2f);
    Shielded.HPP2=45000;Shielded.ShieldP2=30000;
    const LDWorldLabels::FOwnBars Scaled=LDWorldLabels::OwnBars(Own,5,Shielded);
    TestEqual(TEXT("Health and shield beyond the bar share it in proportion: health"),Scaled.Health,.6f);
    TestEqual(TEXT("Health and shield beyond the bar share it in proportion: shield"),Scaled.Shield,.4f);
    TestEqual(TEXT("Together they fill the bar exactly"),Scaled.Health+Scaled.Shield,1.f);
    Shielded.HPP2=60000;Shielded.ShieldP2=60000;
    const LDWorldLabels::FOwnBars Even=LDWorldLabels::OwnBars(Own,5,Shielded);
    TestTrue(TEXT("Full health with as much shield splits the bar evenly"),Even.Health==.5f&&Even.Shield==.5f);
    FLDCombatOwnerView Manaless=Combat;Manaless.MPP2=0;Manaless.MaxMPP2=0;
    const LDWorldLabels::FOwnBars HealthOnly=LDWorldLabels::OwnBars(Own,5,Manaless);
    TestTrue(TEXT("Without a known mana maximum the mana bar is omitted and the health bar stays"),HealthOnly.bVisible&&!HealthOnly.bMana&&HealthOnly.Mana==0.f&&HealthOnly.Health==.5f);
    Manaless.MaxMPP2=20000;
    const LDWorldLabels::FOwnBars Spent=LDWorldLabels::OwnBars(Own,5,Manaless);
    TestTrue(TEXT("Known zero mana is an empty mana bar, not a missing one"),Spent.bMana&&Spent.Mana==0.f);
    return true;
}
#endif
