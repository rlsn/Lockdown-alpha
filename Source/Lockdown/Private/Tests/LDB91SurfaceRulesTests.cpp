#include "Combat/LDEnvironmentRules.h"
#include "Combat/LDRiverRules.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDB91NarrowBeam,"Lockdown.Rules.B91NarrowBeamFootprint",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDB91NarrowBeam::RunTest(const FString& Parameters)
{
    const FVector Start(3300,1000,492.15),Aim(3600,1000,400);
    const auto Full=LDEnvironment::Mask(Start,Aim,900,90,false,32,false,true);
    TestTrue(TEXT("A beam between cell centres still paints the hit floor"),Full.Contains(LDRiver::Cell(Aim)));
    TestTrue(TEXT("Footprint admission remains bounded"),Full.Num()>0&&Full.Num()<=32);
    TestTrue(TEXT("Inherited small-map centre selection stays unchanged"),LDEnvironment::Mask(Start,Aim,900,90,false,32,false,false).IsEmpty());
    return true;
}
#endif
