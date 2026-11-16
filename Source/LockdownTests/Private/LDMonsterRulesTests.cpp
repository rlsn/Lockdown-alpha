#include "Combat/LDMonsterRules.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDMonsterAreasTest,"Lockdown.Rules.DemoCamp.SkillGeometry",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDMonsterAreasTest::RunTest(const FString& Parameters)
{
    FLDMonsterArea Cone;Cone.Range=600;Cone.Angle=60;Cone.Direction=FVector::ForwardVector;
    TestTrue(TEXT("Cone contains a target along its locked aim"),LDMonsterRules::Contains(Cone,FVector(300,0,0)));
    TestFalse(TEXT("Sidestepping leaves the cone"),LDMonsterRules::Contains(Cone,FVector(100,100,0)));
    TestFalse(TEXT("Going behind the caster leaves the cone"),LDMonsterRules::Contains(Cone,FVector(-10,0,0)));
    TestFalse(TEXT("Cone range cannot extend beyond the warning"),LDMonsterRules::Contains(Cone,FVector(601,0,0)));
    FLDMonsterArea Circle;Circle.Shape=ELDMonsterShape::Circle;Circle.Origin=FVector(300,200,0);Circle.Radius=180;
    TestTrue(TEXT("Circle includes its edge"),LDMonsterRules::Contains(Circle,FVector(480,200,0)));
    TestFalse(TEXT("Circle excludes a point outside its edge"),LDMonsterRules::Contains(Circle,FVector(481,200,0)));
    FLDMonsterArea Line;Line.Shape=ELDMonsterShape::Line;Line.Range=400;Line.Radius=80;
    TestTrue(TEXT("Line contains its body width"),LDMonsterRules::Contains(Line,FVector(200,80,0)));
    TestFalse(TEXT("Line can be dodged sideways"),LDMonsterRules::Contains(Line,FVector(200,81,0)));
    TestFalse(TEXT("Line never extends behind its origin"),LDMonsterRules::Contains(Line,FVector(-1,0,0)));
    FLDDemoMonsterSkill Skill;Skill.Pattern=ELDMonsterPattern::EchoCircle;Skill.RatioBp=10000;Skill.Pulses=2;
    Skill.RangeCm={800,1};Skill.RadiusCm={180,1};Skill.AngleDegrees=60;Skill.PulseIntervalMs=800;
    Skill.EchoStage=6;Skill.EchoDelayMs=800;Skill.EchoRatioBp=5000;
    const FVector Origin(0,0,0),Target(100,200,0);
    const auto Before=LDMonsterRules::Areas(Skill,5,Origin,Target);
    const auto Evolved=LDMonsterRules::Areas(Skill,6,Origin,Target);
    TestEqual(TEXT("Stage five has only its two approved pulses"),Before.Num(),2);
    TestEqual(TEXT("Stage six gains one delayed evolution pulse"),Evolved.Num(),3);
    TestTrue(TEXT("Every echo stays on the committed target foot"),Evolved[0].Origin==Target && Evolved[1].Origin==Target && Evolved[2].Origin==Target);
    TestEqual(TEXT("The evolution pulse has a separate exact delay"),Evolved[2].DelayMs,int64(1600));
    TestEqual(TEXT("The evolution pulse is only its own damage budget"),Evolved[2].RatioBp,int64(5000));
    Skill.Pattern=ELDMonsterPattern::Fan;Skill.Pulses=3;Skill.RatioBp=7500;Skill.PulseAngleDegrees=60;Skill.PulseIntervalMs=500;
    const auto Fan=LDMonsterRules::Areas(Skill,0,Origin,FVector(400,0,0));
    TestTrue(TEXT("A three-lane fan rotates around the locked aim"),Fan[0].Direction.Y<0 && FMath::IsNearlyZero(Fan[1].Direction.Y) && Fan[2].Direction.Y>0);
    TestEqual(TEXT("Multi-pulse damage is budgeted for the whole release"),Fan[0].RatioBp+Fan[1].RatioBp+Fan[2].RatioBp,int64(22500));
    Skill.Pattern=ELDMonsterPattern::RotatingCone;Skill.PulseAngleDegrees=120;
    const auto Around=LDMonsterRules::Areas(Skill,0,Origin,FVector(400,0,0));
    TestTrue(TEXT("Rotating sectors cover rear-left, front and rear-right separately"),
        LDMonsterRules::Contains(Around[0],FVector(-150,-260,0)) && LDMonsterRules::Contains(Around[1],FVector(300,0,0))
        && LDMonsterRules::Contains(Around[2],FVector(-150,260,0)));
    TestFalse(TEXT("A rotating release keeps a safe sector between its warnings"),
        LDMonsterRules::Contains(Around[0],FVector(150,260,0)) || LDMonsterRules::Contains(Around[1],FVector(150,260,0))
        || LDMonsterRules::Contains(Around[2],FVector(150,260,0)));
    return true;
}
#endif
