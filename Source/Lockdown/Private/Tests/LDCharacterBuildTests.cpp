#include "Development/LDCharacterBuild.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCharacterStartTest,"Lockdown.Rules.CharacterStarts",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDCharacterStartTest::RunTest(const FString& Parameters)
{
    for (uint8 Class=0; Class<3; ++Class)
    {
        const auto B=LDDevelopment::StartingBuild(Class); int32 Total=0, Known=0;
        for (int32 A:B.Attributes) { Total+=A; }
        for (uint8 R:B.Ranks) { Known+=R>0; }
        TestTrue(TEXT("Valid equal-budget class start"),LDDevelopment::Valid(B)&&Total==60&&Known==2);
        TestTrue(TEXT("Exactly the two starting skills are equipped at rank one"),LDDevelopment::Rank(B,B.Slots[0])==1&&LDDevelopment::Rank(B,B.Slots[1])==1&&B.Slots[2]==ELDSkill::None&&B.Slots[3]==ELDSkill::None);
        TestTrue(TEXT("No initial points, specialization or cooldown"),B.Level==1&&!B.AttributePoints&&!B.SkillPoints&&!B.Subclass&&!B.Cooldowns.ContainsByPredicate([](double V){return V!=0;}));
    }
    auto B=LDDevelopment::StartingBuild(0); B.Attributes.Init(10,6);
    TestEqual(TEXT("All-ten HP"),LDDevelopment::MaxHP(B),100.);
    TestEqual(TEXT("All-ten damage"),LDDevelopment::BasicDamage(B),20.);
    B.Attributes[LDDevelopment::STR]=12; B.Attributes[LDDevelopment::CON]=12; B.Attributes[LDDevelopment::DEX]=12;
    TestEqual(TEXT("Fractional basic damage is preserved"),LDDevelopment::BasicDamage(B),21.5);
    TestEqual(TEXT("CON derived maximum"),LDDevelopment::MaxHP(B),110.);
    TestTrue(TEXT("DEX scales native walk and sprint"),FMath::IsNearlyEqual(180*LDDevelopment::SpeedFactor(B),183.6)&&FMath::IsNearlyEqual(300*LDDevelopment::SpeedFactor(B),306.));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCharacterGrowthTest,"Lockdown.Rules.CharacterGrowth",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDCharacterGrowthTest::RunTest(const FString& Parameters)
{
    auto B=LDDevelopment::StartingBuild(1); int64 XP=0;
    LDDevelopment::AwardXP(B,XP,59); TestTrue(TEXT("No early level reward"),B.Level==1&&!B.AttributePoints&&!B.SkillPoints);
    LDDevelopment::AwardXP(B,XP,1); TestTrue(TEXT("Exact first threshold grants once"),B.Level==2&&B.AttributePoints==2&&B.SkillPoints==1);
    LDDevelopment::AwardXP(B,XP,MAX_int64); TestTrue(TEXT("Multiple thresholds and huge award cap safely"),XP==420&&B.Level==5&&B.AttributePoints==8&&B.SkillPoints==4);
    const uint32 Revision=B.Revision;
    LDDevelopment::AwardXP(B,XP,100); LDDevelopment::AwardXP(B,XP,-10);
    TestTrue(TEXT("At cap and negative award cannot print points"),XP==420&&B.AttributePoints==8&&B.SkillPoints==4&&B.Revision==Revision);
    TestEqual(TEXT("Opening return delay"),LDDevelopment::DeathDelay(1,0),10.);
    TestEqual(TEXT("Late level-five return delay"),LDDevelopment::DeathDelay(5,1200),46.);
    TestEqual(TEXT("Return delay cap"),LDDevelopment::DeathDelay(5,5400),60.);
    TestEqual(TEXT("Integer ceiling gold loss"),LDDevelopment::DeathLoss(101),int64(16));
    TestTrue(TEXT("Gold loss avoids overflow"),LDDevelopment::DeathLoss(MAX_int64)>0&&LDDevelopment::DeathLoss(MAX_int64)<MAX_int64);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildPlanTest,"Lockdown.Rules.CharacterPreparationPlan",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDBuildPlanTest::RunTest(const FString& Parameters)
{
    auto B=LDDevelopment::StartingBuild(1); int64 XP=0; LDDevelopment::AwardXP(B,XP,420);
    B.Cooldowns[int32(ELDSkill::Step)]=99;
    auto P=LDDevelopment::PlanFrom(B); P.Attributes[LDDevelopment::STR]+=3; P.Ranks[int32(ELDSkill::Step)]=3; P.Slots[2]=ELDSkill::Step; P.Subclass=3;
    FString Reason; FLDCharacterBuild Result;
    TestTrue(TEXT("Cumulative class learn and rank1-to3 pays every cost"),LDDevelopment::Prepare(B,P,Result,Reason)&&Result.SkillPoints==0&&Result.AttributePoints==5&&Result.Cooldowns==B.Cooldowns);
    TestTrue(TEXT("Permanent subclass and rank retain four slots"),Result.Subclass==3&&Result.Slots.Num()==4&&LDDevelopment::Rank(Result,ELDSkill::Step)==3);
    const auto Sentinel=Result;
    P.ExpectedRevision--;
    TestFalse(TEXT("Stale revision rejected"),LDDevelopment::Prepare(B,P,Result,Reason));
    TestTrue(TEXT("Rejected plan leaves output unchanged"),Result.Revision==Sentinel.Revision&&Result.Attributes==Sentinel.Attributes);
    P=LDDevelopment::PlanFrom(B); P.Attributes[0]=17;
    TestFalse(TEXT("Attribute cap rejected"),LDDevelopment::Prepare(B,P,Result,Reason));
    P=LDDevelopment::PlanFrom(B); P.Ranks[int32(ELDSkill::Spark)]=1;
    TestFalse(TEXT("No direct cross-class learning without a book"),LDDevelopment::Prepare(B,P,Result,Reason));
    P=LDDevelopment::PlanFrom(B); P.Slots[1]=P.Slots[0];
    TestFalse(TEXT("Duplicate slot rejected"),LDDevelopment::Prepare(B,P,Result,Reason));
    P=LDDevelopment::PlanFrom(B); P.Slots.Add(ELDSkill::None);
    TestFalse(TEXT("Extra utility slot rejected"),LDDevelopment::Prepare(B,P,Result,Reason));
    return true;
}
#endif
