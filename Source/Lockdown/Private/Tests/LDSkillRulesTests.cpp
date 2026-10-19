#include "Development/LDCharacterBuild.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDSkillCatalogTest,"Lockdown.Rules.SkillCatalog",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDSkillCatalogTest::RunTest(const FString& Parameters)
{
    auto Build=LDDevelopment::StartingBuild(0);Build.Attributes.Init(10,6);Build.Ranks.Init(1,LDDevelopment::SkillCount);Build.Ranks[0]=0;
    TestEqual(TEXT("Step base distance"),LDDevelopment::SkillValue(Build,ELDSkill::Step),360.);
    Build.Attributes[LDDevelopment::DEX]=8;TestEqual(TEXT("Step low DEX"),LDDevelopment::SkillValue(Build,ELDSkill::Step),340.);
    Build.Attributes[LDDevelopment::DEX]=16;Build.Ranks[1]=3;TestEqual(TEXT("Step bounded distance"),LDDevelopment::SkillValue(Build,ELDSkill::Step),420.);
    Build.Attributes.Init(16,6);Build.Ranks.Init(3,LDDevelopment::SkillCount);Build.Ranks[0]=0;
    TestEqual(TEXT("Spark rank three at INT16"),LDDevelopment::SkillValue(Build,ELDSkill::Spark),39.5);
    TestEqual(TEXT("Ambush rank/attribute formula"),LDDevelopment::SkillValue(Build,ELDSkill::Ambush),74.);
    TestEqual(TEXT("Patch rank/attribute formula"),LDDevelopment::SkillValue(Build,ELDSkill::Patch),39.);
    Build.Level=4;
    const ELDSkill Affected[]={ELDSkill::Heave,ELDSkill::Patch,ELDSkill::Pickpocket,ELDSkill::Survey,ELDSkill::Spark,ELDSkill::Parley};
    for(uint8 Subclass=1;Subclass<=6;++Subclass)
    {
        if(Subclass==3)
        {TestTrue(TEXT("Cutpurse adds one at the theft boundary"),!LDDevelopment::TheftSucceeds(10,10,1,false,10,false)&&LDDevelopment::TheftSucceeds(10,10,1,true,10,false));continue;}
        Build.Subclass=0;const double Before=LDDevelopment::SkillValue(Build,Affected[Subclass-1]);Build.Subclass=Subclass;
        const double Expected=Subclass==2?40:Before+(Subclass==5?3:2);
        TestEqual(FString::Printf(TEXT("Subclass %s applies before cap"),LDDevelopment::SubclassName(Subclass)),LDDevelopment::SkillValue(Build,Affected[Subclass-1]),Expected);
    }
    Build.Subclass=0;const double Reference=LDDevelopment::SkillValue(Build,ELDSkill::Spark);
    for(uint8 Class=0;Class<3;++Class){Build.ClassId=Class;TestEqual(TEXT("Same attributes/rank produce same cross-class Spark"),LDDevelopment::SkillValue(Build,ELDSkill::Spark),Reference);}
    auto* Catalog=ULDCharacterCatalog::Load();FString Error;
    if(!TestNotNull(TEXT("Saved catalog exists"),Catalog)){return false;}
    TestTrue(TEXT("Saved schema matches nine native skills"),Catalog->Validate(Error));
    auto* Changed=NewObject<ULDCharacterCatalog>();
    TestFalse(TEXT("Rehashed changed skill cooldown is rejected"),Changed->Import(Catalog->SourceJson.Replace(TEXT("\"cooldownSeconds\": 7"),TEXT("\"cooldownSeconds\": 5")),Error));
    TestFalse(TEXT("Rehashed changed Spark formula is rejected"),Changed->Import(Catalog->SourceJson.Replace(TEXT("1.25 * (INT - 10)"),TEXT("2 * (INT - 10)")),Error));
    TestFalse(TEXT("Rehashed changed held duration is rejected"),Changed->Import(Catalog->SourceJson.Replace(TEXT("\"channelSeconds\": 1"),TEXT("\"channelSeconds\": 0.5")),Error));
    return true;
}
#endif
