#include "Navigation/LDOrderRules.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDControlFeelHeldMoveTest, "Lockdown.Rules.DemoControlFeel.HeldMove",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDControlFeelHeldMoveTest::RunTest(const FString& Parameters)
{
    using namespace LDOrderRules;
    const FVector Sent(100, 200, 0);
    const FVector Far = Sent + FVector(HeldMoveShiftCm, 0, 0);
    TestTrue(TEXT("A held button repeats once the interval passed and the pointer's ground moved"),
        ShouldRepeatHeldMove(10.0 + HeldMoveRepeatSeconds, 10.0, Sent, Far));
    TestFalse(TEXT("Never more than eight orders a second"),
        ShouldRepeatHeldMove(10.0 + HeldMoveRepeatSeconds - .001, 10.0, Sent, Far));
    TestTrue(TEXT("The interval caps the rate at eight a second"), FMath::IsNearlyEqual(1.0 / HeldMoveRepeatSeconds, 8.0));
    TestFalse(TEXT("A still pointer sends nothing more"),
        ShouldRepeatHeldMove(20.0, 10.0, Sent, Sent + FVector(HeldMoveShiftCm - 1.0, 0, 0)));
    TestFalse(TEXT("Height alone is no new ground point"),
        ShouldRepeatHeldMove(20.0, 10.0, Sent, Sent + FVector(0, 0, 500)));
    const double NaN = std::numeric_limits<double>::quiet_NaN();
    TestFalse(TEXT("A non-finite clock or point repeats nothing"),
        ShouldRepeatHeldMove(NaN, 10.0, Sent, Far) || ShouldRepeatHeldMove(20.0, NaN, Sent, Far)
        || ShouldRepeatHeldMove(20.0, 10.0, Sent, FVector(NaN, 0, 0)));
    return true;
}

#endif
