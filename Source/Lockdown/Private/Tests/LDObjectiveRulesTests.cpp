#include "Objectives/LDObjectiveRules.h"
#include "Misc/AutomationTest.h"
#include "Core/LDRules.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDObjectiveTimingTest,"Lockdown.Rules.ObjectiveTiming",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDObjectiveTimingTest::RunTest(const FString& Parameters)
{
    double Due=0;int32 Samples=0;for(int32 Frame=0;Frame<620;++Frame){const double Now=Frame/62.;if(Now>=Due){++Samples;Due=LDRules::NextSample(Now,Due,.05);}}
    TestTrue(TEXT("62FPS frame quantization retains twenty authority samples per second"),Samples>=199&&Samples<=201);
    TestEqual(TEXT("A stalled tick skips old intervals without an immediate replay"),LDRules::NextSample(5.,1.,.05),5.05);
    TestTrue(TEXT("A late ordinary frame retains the scheduled phase"),FMath::IsNearlyEqual(LDRules::NextSample(.064,.05,.05),.1));
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
