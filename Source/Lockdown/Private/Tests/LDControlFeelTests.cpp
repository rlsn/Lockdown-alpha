#include "Navigation/LDOrderRules.h"
#include "Combat/LDCombatPreInput.h"

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDControlFeelPreInputTest, "Lockdown.Rules.DemoControlFeel.PreInput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDControlFeelPreInputTest::RunTest(const FString& Parameters)
{
    using namespace LDCombatPreInput;
    FLDCombatOwnerView View;
    View.bHasBody = true;
    View.ActionEntry = ELDCombatEntry::Q;
    View.ActionPhase = ELDCombatPhase::Accepted;
    View.ActionReleaseAtMs = 1200;
    TestTrue(TEXT("An own ability still winding up holds the next press"), IsWindingUp(View, 1000));
    TestFalse(TEXT("At its release the held press goes out"), IsWindingUp(View, 1200));
    TestTrue(TEXT("Without a clock the accepted phase alone holds it"), IsWindingUp(View, 0));
    View.ActionPhase = ELDCombatPhase::Finished;
    TestFalse(TEXT("A finished action holds nothing"), IsWindingUp(View, 1000));
    View.ActionPhase = ELDCombatPhase::Accepted;
    View.ActionEntry = ELDCombatEntry::Basic;
    TestFalse(TEXT("A basic swing is interrupted by an ability, so it holds nothing"), IsWindingUp(View, 1000));
    View.ActionEntry = ELDCombatEntry::Item2;
    View.bHasBody = false;
    TestFalse(TEXT("No body holds nothing"), IsWindingUp(View, 1000));
    TestTrue(TEXT("Q, W and both gear actives are Direct entries"),
        IsDirectEntry(ELDCombatEntry::Q) && IsDirectEntry(ELDCombatEntry::W) && IsDirectEntry(ELDCombatEntry::Item1)
        && IsDirectEntry(ELDCombatEntry::Item2) && !IsDirectEntry(ELDCombatEntry::Basic) && !IsDirectEntry(ELDCombatEntry::None));
    TestTrue(TEXT("A held press lives at most a second"), HoldSeconds == 1.0);
    return true;
}

#endif