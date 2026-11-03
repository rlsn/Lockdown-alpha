#include "Character/LDBodyMotion.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBodyMotionTimelinesTest, "Lockdown.Rules.DemoBodyMotion.Timelines",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDBodyMotionTimelinesTest::RunTest(const FString& Parameters)
{
    using namespace LDBodyMotion;
    const auto AtRest = [](const FChannels& C)
    {
        return C.WindBack == 0.f && C.Strike == 0.f && C.Raise == 0.f && C.Thrust == 0.f && C.Flinch == 0.f
            && C.Lunge == 0.f && C.Lean == 0.f && C.Charge == 0.f && C.Burst == 0.f && C.Flash == 0.f;
    };
    TestTrue(TEXT("No action, a negative age or a non-finite age is the rest pose"),
        AtRest(Evaluate(EAction::None, .2f)) && AtRest(Evaluate(EAction::Attack, -.1f))
        && AtRest(Evaluate(EAction::Hit, std::numeric_limits<float>::quiet_NaN())));

    const FChannels WoundUp = Evaluate(EAction::Attack, .15f), Struck = Evaluate(EAction::Attack, .23f);
    const FChannels Spark = Evaluate(EAction::Attack, StrikeAt + .03f), Settled = Evaluate(EAction::Attack, .6f);
    TestTrue(TEXT("A swing is drawn back before it strikes"), WoundUp.WindBack > .9f && WoundUp.Strike < .05f && WoundUp.Lean < 0.f);
    TestTrue(TEXT("The strike flings the arm forward with a lunge"), Struck.Strike > .9f && Struck.Lunge > 25.f && Struck.Lean > 10.f);
    TestTrue(TEXT("The impact spark peaks at the strike and is gone by the swing's end"),
        FMath::IsNearlyEqual(Spark.Burst, 1.f) && Evaluate(EAction::Attack, StrikeAt + .15f).Burst == 0.f);
    TestTrue(TEXT("After the swing the body stands where it started"), Settled.Lunge == 0.f && Settled.Lean == 0.f && Settled.Burst == 0.f);

    const FChannels Charging = Evaluate(EAction::Cast, .15f), Released = Evaluate(EAction::Cast, .3f);
    TestTrue(TEXT("A cast raises both arms with a growing orb"), Charging.Raise > .8f && Charging.Charge > .8f);
    TestTrue(TEXT("The cast is pushed forward and its orb spent"), Released.Thrust > .9f && Released.Charge == 0.f);

    const FChannels Impact = Evaluate(EAction::Hit, 0.f), Knocked = Evaluate(EAction::Hit, .05f), Recovered = Evaluate(EAction::Hit, .3f);
    TestTrue(TEXT("A hit flashes at full strength the moment it lands"), Impact.Flash == 1.f);
    TestTrue(TEXT("A hit knocks the body back and throws its arms out"), Knocked.Lunge < -15.f && Knocked.Lean < -20.f && Knocked.Flinch > .9f);
    TestTrue(TEXT("The flash is short and the body recovers within the 300 ms cue"),
        Evaluate(EAction::Hit, .18f).Flash == 0.f && Recovered.Lunge == 0.f && Recovered.Flinch == 0.f && Recovered.Burst == 0.f);

    TestTrue(TEXT("A different action kind is a new instance"), IsNewInstance(EAction::Attack, 1.0, EAction::Hit, 1.0, true));
    TestTrue(TEXT("A later start of the same kind restarts it"), IsNewInstance(EAction::Hit, 1.0, EAction::Hit, 1.2, true));
    TestFalse(TEXT("Report jitter of one instance does not restart it"), IsNewInstance(EAction::Hit, 1.0, EAction::Hit, 1.05, true));
    TestFalse(TEXT("Without a known age a continuing kind is never restarted"), IsNewInstance(EAction::Attack, 1.0, EAction::Attack, 9.0, false));
    TestFalse(TEXT("An ended action starts nothing"), IsNewInstance(EAction::Attack, 1.0, EAction::None, 2.0, true));

    const FLife Born = Appear(0.f), Grown = Appear(AppearSeconds);
    TestTrue(TEXT("A respawned body grows in from small inside a bright column"), Born.Scale < .3f && Born.Column == 1.f && Born.Glow == 1.f);
    TestTrue(TEXT("The appear effect ends at full size"), Grown.Scale == 1.f && Grown.Column == 0.f && Grown.Glow == 0.f);
    const FLife Toppled = Fall(.35f), Gone = Fall(FallSeconds);
    TestTrue(TEXT("A dead body falls on its back first"), FMath::IsNearlyEqual(Toppled.Tilt, 90.f) && Toppled.Scale == 1.f && !Toppled.bGone);
    TestTrue(TEXT("Then it sinks and shrinks away"), Gone.bGone && Gone.Scale == 0.f && Gone.Sink > 0.f);
    return true;
}

#endif
