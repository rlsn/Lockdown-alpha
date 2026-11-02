#include "Region/LDMapApproaches.h"
#include "Region/LDMapRegistry.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDB90ApproachPaths,"Lockdown.Rules.B90ApproachPaths",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDB90ApproachPaths::RunTest(const FString& Parameters)
{
    const TArray<FVector> Path={FVector(0,0,0),FVector(300,0,400),FVector(300,0,400),FVector(300,600,400)};
    const auto Tail=LDMapApproaches::Tail(Path,850);
    TestTrue(TEXT("Tail preserves bends, grades and direction"),Tail.Num()==3&&Tail[0].Equals(FVector(150,0,200))&&Tail.Last()==Path.Last());
    TestTrue(TEXT("Tail measures in native 3D units"),FMath::IsNearlyEqual(LDMapApproaches::Length(Tail),850.));
    TestTrue(TEXT("Remaining distance follows corner before slope"),LDMapApproaches::AtRemaining(Path,850).Equals(FVector(150,0,200)));
    TestTrue(TEXT("Zero-distance tail is endpoint only"),LDMapApproaches::Tail(Path,0).Num()==1);
    TestTrue(TEXT("Overlong remaining distance clamps to start"),LDMapApproaches::AtRemaining(Path,2000)==Path[0]);
    TestTrue(TEXT("Overlong tail retains whole traversable length"),FMath::IsNearlyEqual(LDMapApproaches::Length(LDMapApproaches::Tail(Path,2000)),1100.));
    TestTrue(TEXT("Empty and invalid requests have no path"),LDMapApproaches::Tail({},100).IsEmpty()&&LDMapApproaches::Tail(Path,-1).IsEmpty());
    TestTrue(TEXT("A static ray without a world never authorizes"),LDMapApproaches::StaticRay(nullptr,{},{},1800).Result==LDMapApproaches::ERay::Invalid);
    const FBox Wall(FVector(-100,-100,0),FVector(100,100,600));
    TestTrue(TEXT("Road footprint detects wall crossing"),LDMapFitting::RoadOverlaps(FTransform(FRotator(0,45,0),FVector(0,0,2),FVector(4,8,.02)),Wall));
    TestFalse(TEXT("Road beside wall remains clear"),LDMapFitting::RoadOverlaps(FTransform(FRotator::ZeroRotator,FVector(600,0,2),FVector(2,2,.02)),Wall));
    TestFalse(TEXT("Different floor does not clip lower road"),LDMapFitting::RoadOverlaps(FTransform(FRotator::ZeroRotator,FVector(0,0,-50),FVector(2,2,.02)),Wall));
    return true;
}
#endif
