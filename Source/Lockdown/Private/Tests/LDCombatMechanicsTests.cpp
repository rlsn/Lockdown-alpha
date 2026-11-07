#include "Combat/LDCombatMechanics.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

// #112 §5 mechanics (C2) still used by the catalog: pure rules of shield breaks, knockback and periodic zone schedules.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoMechanicsShieldEventsTest, "Lockdown.Rules.DemoMechanics.ShieldEvents",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoMechanicsShieldEventsTest::RunTest(const FString& Parameters)
{
    // A break (item_privacy_screen's retort, the damage-triggered shield's burst) needs damage to empty a positive shield.
    TestTrue(TEXT("Damage emptying a positive shield breaks it"), LDCombatMechanics::ShieldBroken(5000, 0, 5000));
    TestFalse(TEXT("Damage leaving some shield is no break"), LDCombatMechanics::ShieldBroken(5000, 1, 4999));
    TestFalse(TEXT("A consumed or lapsed shield (no absorption) is no break"), LDCombatMechanics::ShieldBroken(5000, 0, 0));
    TestFalse(TEXT("No shield, no break"), LDCombatMechanics::ShieldBroken(0, 0, 0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoMechanicsKnockbackTest, "Lockdown.Rules.DemoMechanics.Knockback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoMechanicsKnockbackTest::RunTest(const FString& Parameters)
{
    FVector Direction;
    TestTrue(TEXT("A push leaves the source foot toward the target"),
        LDCombatMechanics::KnockbackDirection(FVector(0, 0, 10), FVector(30, 40, 0), 0, Direction)
        && Direction.Equals(FVector(0.6, 0.8, 0), 1e-9));
    TestTrue(TEXT("Height never tilts the push off the ground"),
        LDCombatMechanics::KnockbackDirection(FVector(0, 0, 0), FVector(-10, 0, 500), 0, Direction)
        && Direction.Equals(FVector(-1, 0, 0), 1e-9));
    TestTrue(TEXT("Coincident feet push along the source's facing"),
        LDCombatMechanics::KnockbackDirection(FVector(5, 5, 0), FVector(5, 5, 0), 90, Direction)
        && Direction.Equals(FVector(0, 1, 0), 1e-9));
    TestFalse(TEXT("Non-finite input is refused"),
        LDCombatMechanics::KnockbackDirection(FVector(std::numeric_limits<double>::quiet_NaN(), 0, 0), FVector(1, 0, 0), 0, Direction));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoMechanicsPeriodicTest, "Lockdown.Rules.DemoMechanics.Periodic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoMechanicsPeriodicTest::RunTest(const FString& Parameters)
{
    // A delayed zone: three pulses at 0.5/1.5/2.5 s; an immediate one: three at 0/1/2 s.
    TestEqual(TEXT("The first gas pulse"), LDCombatMechanics::NextPulseAtMs(1000, 500, 1000, 3, 1000), int64(1500));
    TestEqual(TEXT("A pulse due exactly now is still due"), LDCombatMechanics::NextPulseAtMs(1000, 500, 1000, 3, 2500), int64(2500));
    TestEqual(TEXT("Between pulses, the next one"), LDCombatMechanics::NextPulseAtMs(1000, 500, 1000, 3, 2501), int64(3500));
    TestEqual(TEXT("After the last pulse nothing is left"), LDCombatMechanics::NextPulseAtMs(1000, 500, 1000, 3, 3501), int64(0));
    TestEqual(TEXT("A zone pulses at its start"), LDCombatMechanics::NextPulseAtMs(1000, 0, 1000, 3, 1000), int64(1000));
    TestEqual(TEXT("The third immediate pulse"), LDCombatMechanics::NextPulseAtMs(1000, 0, 1000, 3, 2500), int64(3000));
    TestEqual(TEXT("No fourth immediate pulse"), LDCombatMechanics::NextPulseAtMs(1000, 0, 1000, 3, 3001), int64(0));
    TestEqual(TEXT("An invalid schedule never pulses"), LDCombatMechanics::NextPulseAtMs(1000, 0, 0, 3, 1000), int64(0));
    return true;
}

#endif
