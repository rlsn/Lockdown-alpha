#include "UI/LDHUDModel.h"
#include "Development/LDDemoContentIds.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDHUDMaskTest,"Lockdown.Rules.HUDObservationBoundary",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDHUDMaskTest::RunTest(const FString& Parameters)
{
    FLDOwnerState Own;Own.Gold=98765;Own.Hp=12.34;Own.MaxHp=100;Own.Mp=7;Own.MaxMp=30;
    FLDObservedUnitView Rival;Rival.Kind=ELDObservedUnitKind::Contender;Rival.AuthorizedFieldMask=LDObservation::ContenderFields;Rival.ContenderId=FGuid::NewGuid();Rival.Level=3;
    Rival.Health.CurrentP2=45000;Rival.Health.MaxP2=60000;Rival.Mana.CurrentP2=12000;Rival.Mana.MaxP2=20000;Rival.Equipment.Slots[0]=uint8(ELDDemoEquipmentId::ItemIronPlate);
    FLDObservedStatus Root;Root.StatusId=uint8(ELDDemoStatusId::Root);Root.ExpiresAtServerMs=12500;Rival.Statuses.Add(Root);
    FLDObservedStatus Protection;Protection.StatusId=uint8(ELDDemoStatusId::RespawnProtection);Protection.ExpiresAtServerMs=11500;Rival.Statuses.Add(Protection);
    const FString Shown=LDHUD::ObservedUnit(Rival,TEXT("Rival"),nullptr,10.);
    // Labels are exact HUD spellings; case-insensitive substrings collide (e.g. "camp" contains "mp").
    auto Has=[](const FString& Text,const TCHAR* Label){return Text.Contains(Label,ESearchCase::CaseSensitive);};
    TestTrue(TEXT("Observed inspector shows the SC-07 whitelist"),Has(Shown,TEXT("Rival · Level 3"))&&Has(Shown,TEXT("HP 450/600"))&&Has(Shown,TEXT("MP 120/200"))
        &&Has(Shown,TEXT("Rooted 2.5s"))&&Has(Shown,TEXT("Respawn protection 1.5s"))&&Has(Shown,TEXT("Equipment #99"))&&Has(Shown,TEXT("Empty slot")));
    TestEqualSensitive(TEXT("Observed inspector renders exactly the SC-07 whitelist"),Shown,FString(TEXT("Rival · Level 3\nHP 450/600 · MP 120/200\nRooted 2.5s · Respawn protection 1.5s\nEquipment Equipment #99 · Empty slot")));
    for(const TCHAR* Private:{TEXT("Gold"),TEXT("XP"),TEXT("Cooldown"),TEXT("Skill"),TEXT("STR"),TEXT("Armor"),TEXT("Attribute")})
    {TestFalse(FString::Printf(TEXT("Observed inspector has no private category: %s"),Private),Has(Shown,Private));}
    TestFalse(TEXT("Observed inspector cannot display own wallet as if it belonged to the rival"),Has(Shown,TEXT("98765")));
    TestTrue(TEXT("A contender missing from the public roster is not given an invented name"),LDHUD::ObservedUnit(Rival,FString(),nullptr,10.).StartsWith(TEXT("Unlisted contender"),ESearchCase::CaseSensitive));
    FLDObservedUnitView Camp;Camp.Kind=ELDObservedUnitKind::CampMember;Camp.AuthorizedFieldMask=LDObservation::CampFields;Camp.CreatureId=uint8(ELDDemoMonsterId::MonsterGuardian);
    Camp.Health.CurrentP2=100;Camp.Health.MaxP2=200;FLDObservedStatus Returning;Returning.StatusId=uint8(ELDDemoStatusId::CampReturning);Camp.Statuses.Add(Returning);
    const FString CampShown=LDHUD::ObservedUnit(Camp,TEXT("Ignored"),nullptr,0.);
    TestTrue(TEXT("Camp member shows identity, HP and public status only"),CampShown.StartsWith(TEXT("Creature #2"),ESearchCase::CaseSensitive)&&Has(CampShown,TEXT("HP 1/2"))&&Has(CampShown,TEXT("Returning to camp"))
        &&!Has(CampShown,TEXT("Level"))&&!Has(CampShown,TEXT("MP"))&&!Has(CampShown,TEXT("Equipment"))&&!Has(CampShown,TEXT("Ignored")));
    TestEqualSensitive(TEXT("Camp inspector renders exactly name, HP and public status"),CampShown,FString(TEXT("Creature #2\nHP 1/2\nReturning to camp")));
    TestEqualSensitive(TEXT("Self inspector shows only own resources; the old class and attribute view is retired"),LDHUD::Inspector(Own),FString(TEXT("YOUR CHARACTER\nHP 12/100 · MP 7/30")));
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
    TestEqualSensitive(TEXT("Protection counts down from the matching authority clock"),LDHUD::LifeStatus(Own,true,3000),FString(TEXT("Protected 2.0s")));
    TestEqualSensitive(TEXT("A live protection never reads zero before the clock reaches its deadline"),LDHUD::LifeStatus(Own,true,4999),FString(TEXT("Protected 0.1s")));
    TestEqualSensitive(TEXT("Protection ends once the authority clock reaches the deadline, before the field is cleared"),LDHUD::LifeStatus(Own,true,5000),FString(TEXT("Alive")));
    TestEqualSensitive(TEXT("Without the matching clock protection time is unavailable, never local time or zero"),LDHUD::LifeStatus(Own,false,0),FString(TEXT("Alive · protection time unavailable")));
    TestEqualSensitive(TEXT("A time value is ignored while the clock is unavailable"),LDHUD::LifeStatus(Own,false,999999),FString(TEXT("Alive · protection time unavailable")));
    Own.ProtectedUntilMs=0;
    TestEqualSensitive(TEXT("No protection deadline needs no clock"),LDHUD::LifeStatus(Own,false,0),FString(TEXT("Alive")));
    Own.LifePhase=ELDLifePhase::Dead;Own.RespawnAtMs=12000;
    const FString Waiting=LDHUD::LifeStatus(Own,true,2350);
    TestEqualSensitive(TEXT("Respawn waits on the Lives deadline"),Waiting,FString(TEXT("RETURN 9.7s")));
    TestFalse(TEXT("Death shows no gold loss: growth property is retained"),Waiting.Contains(TEXT("gold"))||Waiting.Contains(TEXT("lost")));
    TestEqualSensitive(TEXT("The respawn countdown reads zero only once the authority clock reaches it"),LDHUD::LifeStatus(Own,true,12000),FString(TEXT("RETURN 0.0s")));
    TestEqualSensitive(TEXT("Respawn time without the matching clock is unavailable"),LDHUD::LifeStatus(Own,false,0),FString(TEXT("RETURN · time unavailable")));
    Own.bActive=true;
    TestTrue(TEXT("The Lives phase, not the legacy active flag, decides the line"),LDHUD::LifeStatus(Own,true,0).StartsWith(TEXT("RETURN"),ESearchCase::CaseSensitive));
    Own.LifePhase=ELDLifePhase::Unspawned;TestEqualSensitive(TEXT("An unspawned contender is not shown alive"),LDHUD::LifeStatus(Own,true,0),FString(TEXT("Not spawned")));
    Own.LifePhase=ELDLifePhase::Left;TestEqualSensitive(TEXT("A departed contender is not shown alive"),LDHUD::LifeStatus(Own,true,0),FString(TEXT("Left the match")));
    Own.LifePhase=static_cast<ELDLifePhase>(200);TestEqualSensitive(TEXT("An unknown phase is reported, not shown alive"),LDHUD::LifeStatus(Own,true,0),FString(TEXT("Unknown life state")));
    return true;
}
#endif
