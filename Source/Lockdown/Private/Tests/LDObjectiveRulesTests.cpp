#include "Objectives/LDObjectiveRules.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDObjectiveTimingTest,"Lockdown.Rules.ObjectiveTiming",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDObjectiveTimingTest::RunTest(const FString& Parameters)
{
    TestTrue(TEXT("Exact 250ms hold boundary"),LDObjective::FreshHold(10.25,10));
    TestFalse(TEXT("Expired hold"),LDObjective::FreshHold(10.2501,10));
    TestFalse(TEXT("Future refresh rejected"),LDObjective::FreshHold(10,11));
    TestFalse(TEXT("No early completion"),LDObjective::DueBeforeDeadline(9.999,0,10,720));
    TestTrue(TEXT("Exact ten second completion"),LDObjective::DueBeforeDeadline(10,0,10,720));
    TestTrue(TEXT("Late sample permits deadline-equal scheduled claim"),LDObjective::DueBeforeDeadline(721,710,10,720));
    TestFalse(TEXT("Late scheduled claim cannot win"),LDObjective::DueBeforeDeadline(721,710.001,10,720));
    TestEqual(TEXT("Converted Guild"),LDObjective::Position(LDObjective::Guild),FVector(-400,100,0));
    TestEqual(TEXT("Converted single West route"),LDObjective::Position(LDObjective::Station),FVector(-1280,800,0));
    TestEqual(TEXT("Retained South district boundary"),LDObjective::District(FVector(0,-400.01,0)),uint8(2));
    TestEqual(TEXT("District boundary is East"),LDObjective::District(FVector(0,-400,0)),uint8(1));
    return true;
}
#endif
