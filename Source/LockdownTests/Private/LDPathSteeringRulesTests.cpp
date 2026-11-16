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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDPathSteeringRestCrowdTest, "Lockdown.Rules.DemoPathSteering.RestCrowd",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDPathSteeringRestCrowdTest::RunTest(const FString& Parameters)
{
    using namespace LDPathSteering;
    const FVector Start(6727,-841,0), Goal(8100,-200,0);
    const FBodyDisc Blocker{FVector2D(7300,-500),40};
    const TArray<FBodyDisc> Others{Blocker};
    const auto ReachesDisk = [&](bool bAvoid)
    {
        FVector Foot=Start;
        for (int32 Step=0; Step<200; ++Step)
        {
            if (FVector::Dist2D(Foot,Goal)<145) { return true; }
            FVector Input=(Goal-Foot).GetSafeNormal2D();
            if (bAvoid) { Input=AvoidVisibleBodies(Foot,Goal,Input,40,Others); }
            const FVector Next=Foot+Input*15;
            if (FVector2D::Distance(FVector2D(Next.X,Next.Y),Blocker.Foot)>=80) { Foot=Next; }
        }
        return false;
    };
    TestFalse(TEXT("The captured direct route is blocked by the idle player's capsule"),ReachesDisk(false));
    TestTrue(TEXT("A visible blocking player can be passed before the sign-up lock"),ReachesDisk(true));
    const FVector Direct(1,0,0);
    TestTrue(TEXT("A body behind the walker does not change the route"),
        AvoidVisibleBodies(FVector::ZeroVector,FVector(1000,0,0),Direct,40,{{FVector2D(-50,0),40}}).Equals(Direct));
    TestTrue(TEXT("A body beyond the waypoint does not change the route"),
        AvoidVisibleBodies(FVector::ZeroVector,FVector(100,0,0),Direct,40,{{FVector2D(150,0),40}}).Equals(Direct));
    return true;
}
#endif
