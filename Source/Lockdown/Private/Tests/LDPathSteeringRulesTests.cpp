#include "Navigation/LDPathSteering.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDPathSteeringHeldGoalTest, "Lockdown.Rules.DemoPathSteering.HeldGoal",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// #97: a ground walk whose clicked spot another body already stands on stops beside it instead of pushing.
bool FLDPathSteeringHeldGoalTest::RunTest(const FString& Parameters)
{
    using namespace LDPathSteering;
    const double R = 40;
    const FVector2D Goal(0, 0);
    const double Contact = 2 * R + WaypointToleranceCm;
    const FBodyDisc OnSpot{FVector2D(0, 0), R};
    TestTrue(TEXT("Touching the body that stands on the spot is an arrival"),
        BlockedAtHeldGoal(FVector2D(-Contact, 0), R, Goal, {OnSpot}));
    TestFalse(TEXT("Short of contact with that body the walk goes on"),
        BlockedAtHeldGoal(FVector2D(-Contact - 1, 0), R, Goal, {OnSpot}));
    TestFalse(TEXT("Nobody near the spot: no early arrival"), BlockedAtHeldGoal(FVector2D(-Contact, 0), R, Goal, {}));

    const FBodyDisc Beside{FVector2D(0, 2 * R + 1), R};
    TestFalse(TEXT("A body the walker can still stand next to on the spot does not hold it"),
        BlockedAtHeldGoal(FVector2D(0, 2 * R + 1 + Contact), R, Goal, {Beside}));

    const FBodyDisc Crowd{FVector2D(-2 * R, 0), R};
    TestTrue(TEXT("Touching a body that touches the holder is an arrival (a crowd at the spot)"),
        BlockedAtHeldGoal(FVector2D(-2 * R - Contact, 0), R, Goal, {Crowd, OnSpot}));
    const FBodyDisc Stray{FVector2D(-600, 0), R};
    TestFalse(TEXT("Touching a body away from the holding group is not an arrival"),
        BlockedAtHeldGoal(FVector2D(-600 - Contact, 0), R, Goal, {Stray, OnSpot}));
    return true;
}
#endif
