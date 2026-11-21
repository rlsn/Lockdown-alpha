#include "Vision/LDVisionDirector.h"
#include "LDVisionTestParameters.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDRelativeHeightRange, "Lockdown.Rules.DemoVision.RelativeHeightRange",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDRelativeHeightRange::RunTest(const FString& Parameters)
{
    const auto Range=LDVisionTestRange();
    struct FCase {double Difference,Expected;};
    for(const auto& Row:{FCase{0,1},FCase{35,1},FCase{-35,1},FCase{77.5,.875},FCase{-77.5,1.125},
        FCase{120,.75},FCase{-120,1.25},FCase{2000,.75},FCase{-2000,1.25}})
    {
        TestTrue(*FString::Printf(TEXT("Approved radius scale at relative height %.1f"),Row.Difference),
            FMath::IsNearlyEqual(LDObservation::HeightRadiusScale(Range,0,Row.Difference),Row.Expected,1.e-12));
        TestTrue(TEXT("Negative world origin never changes the result"),FMath::IsNearlyEqual(
            LDObservation::HeightRadiusScale(Range,-5000,-5000+Row.Difference),Row.Expected,1.e-12));
    }
    double Previous=1;
    for(int32 I=0;I<=160;++I)
    {
        const double Value=LDObservation::HeightRadiusScale(Range,0,I);
        TestTrue(TEXT("Uphill transition is continuous, monotone and bounded"),Value<=Previous&&Value>=.75);
        Previous=Value;
    }
    TestTrue(TEXT("Thresholds have no jump"),FMath::Abs(LDObservation::HeightRadiusScale(Range,0,35.001)-1)<1.e-8
        &&FMath::Abs(LDObservation::HeightRadiusScale(Range,0,119.999)-.75)<1.e-8);
    TestTrue(TEXT("High versus low is intentionally asymmetric"),
        LDObservation::HeightRadiusScale(Range,1000,0)>LDObservation::HeightRadiusScale(Range,0,1000));
    FLDDemoWorldGeometry Geometry;
    Geometry.LevelStepCm={50,1};Geometry.SightSampleHeightCm={120,1};
    auto& Surface=Geometry.Surfaces.AddDefaulted_GetRef();Surface.Id=TEXT("range_fixture");
    Surface.MinCm.X={-5000,1};Surface.MinCm.Y={-5000,1};Surface.MaxCm.X={5000,1};Surface.MaxCm.Y={5000,1};
    FString Error;FLDSightGeometry Sight;
    auto Invalid=Range;Invalid.FullHeightCm=Invalid.NeutralHeightCm;
    TestFalse(TEXT("Degenerate smooth transition is refused"),Sight.Build(Geometry,{TEXT("range_fixture"),1,1},{1400,1},Invalid,Error));
    Invalid=Range;Invalid.UphillRadiusBp=0;
    TestFalse(TEXT("Zero uphill vision is refused"),Sight.Build(Geometry,{TEXT("range_fixture"),1,1},{1400,1},Invalid,Error));
    Invalid=Range;Invalid.DownhillRadiusBp=9999;
    TestFalse(TEXT("A shrinking downhill scale is refused"),Sight.Build(Geometry,{TEXT("range_fixture"),1,1},{1400,1},Invalid,Error));
    return true;
}
#endif
