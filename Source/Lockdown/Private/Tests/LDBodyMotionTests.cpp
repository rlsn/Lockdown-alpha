#include "Character/LDBodyMotion.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBodyMotionTimelinesTest, "Lockdown.Rules.DemoBodyMotion.Timelines",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDBodyMotionTimelinesTest::RunTest(const FString& Parameters)
{
    using namespace LDBodyMotion;
    const auto AtRest = [](const FChannels& C) { return C.Lunge == 0.f && C.Charge == 0.f && C.Burst == 0.f && C.Flash == 0.f; };
    const float NaN = std::numeric_limits<float>::quiet_NaN();
    TestTrue(TEXT("No action, a negative age or a non-finite age is the rest pose"),
        AtRest(Evaluate(EAction::None, .2f)) && AtRest(Evaluate(EAction::Attack, -.1f)) && AtRest(Evaluate(EAction::Hit, NaN)));

    const FChannels Struck = Evaluate(EAction::Attack, StrikeAt + .02f), Settled = Evaluate(EAction::Attack, .6f);
    TestTrue(TEXT("The strike steps the body into the blow"), Struck.Lunge > 13.f);
    TestTrue(TEXT("The impact spark peaks at the strike and is gone by the swing's end"),
        FMath::IsNearlyEqual(Evaluate(EAction::Attack, StrikeAt + .03f).Burst, 1.f) && Evaluate(EAction::Attack, StrikeAt + .15f).Burst == 0.f);
    TestTrue(TEXT("After the swing the body stands where it started"), AtRest(Settled));

    const FChannels Charging = Evaluate(EAction::Cast, .15f), Released = Evaluate(EAction::Cast, .3f);
    TestTrue(TEXT("A cast grows an orb at the hand over its windup"), Charging.Charge > .9f && Charging.Burst == 0.f);
    TestTrue(TEXT("The release spends the orb in a burst"), Released.Charge == 0.f && Released.Burst > 0.f);
    TestTrue(TEXT("After the cast the body stands where it started"), AtRest(Evaluate(EAction::Cast, .6f)));

    const FChannels Impact = Evaluate(EAction::Hit, 0.f), Knocked = Evaluate(EAction::Hit, .05f), Recovered = Evaluate(EAction::Hit, .3f);
    TestTrue(TEXT("A hit flashes at full strength the moment it lands"), Impact.Flash == 1.f);
    TestTrue(TEXT("A hit knocks the body back"), Knocked.Lunge < -15.f);
    TestTrue(TEXT("The flash is short and the body recovers within the 300 ms cue"),
        Evaluate(EAction::Hit, .18f).Flash == 0.f && AtRest(Recovered));

    TestTrue(TEXT("An attack clip reaches its impact frame at the strike"), FMath::IsNearlyEqual(ClipTime(EAction::Attack, StrikeAt, .565f, 1.f), .565f));
    TestTrue(TEXT("A cast clip reaches its release frame at the strike"), FMath::IsNearlyEqual(ClipTime(EAction::Cast, StrikeAt, .635f, 1.f), .635f));
    TestTrue(TEXT("A hit clip plays at its authored rate"), FMath::IsNearlyEqual(ClipTime(EAction::Hit, .1f, 0.f, 1.9f), .19f));
    TestTrue(TEXT("A clip without an impact frame plays at its authored rate"), FMath::IsNearlyEqual(ClipTime(EAction::Attack, .1f, 0.f, 2.f), .2f));
    TestTrue(TEXT("No action, a negative age or a non-finite input poses the first frame"),
        ClipTime(EAction::None, .5f, .5f, 1.f) == 0.f && ClipTime(EAction::Attack, -.1f, .5f, 1.f) == 0.f
        && ClipTime(EAction::Hit, NaN, 0.f, 1.f) == 0.f && ClipTime(EAction::Hit, .1f, 0.f, NaN) == 0.f);

    TestTrue(TEXT("A different action kind is a new instance"), IsNewInstance(EAction::Attack, 1.0, EAction::Hit, 1.0, true));
    TestTrue(TEXT("A later start of the same kind restarts it"), IsNewInstance(EAction::Hit, 1.0, EAction::Hit, 1.2, true));
    TestFalse(TEXT("Report jitter of one instance does not restart it"), IsNewInstance(EAction::Hit, 1.0, EAction::Hit, 1.05, true));
    TestFalse(TEXT("Without a known age a continuing kind is never restarted"), IsNewInstance(EAction::Attack, 1.0, EAction::Attack, 9.0, false));
    TestFalse(TEXT("An ended action starts nothing"), IsNewInstance(EAction::Attack, 1.0, EAction::None, 2.0, true));

    const FLife Born = Appear(0.f), Grown = Appear(AppearSeconds);
    TestTrue(TEXT("A respawned body grows in from small inside a bright column"), Born.Scale < .3f && Born.Column == 1.f && Born.Glow == 1.f);
    TestTrue(TEXT("The appear effect ends at full size"), Grown.Scale == 1.f && Grown.Column == 0.f && Grown.Glow == 0.f);
    const float Lie = 1.067f;
    const FLife Lying = Fall(Lie, Lie), Gone = Fall(Lie + FadeSeconds, Lie);
    TestTrue(TEXT("A dead body lies down through its death clip at full size"), Lying.Scale == 1.f && Lying.Sink == 0.f && !Lying.bGone);
    TestTrue(TEXT("Then it sinks and shrinks away"), Gone.bGone && Gone.Scale == 0.f && Gone.Sink > 0.f);
    TestTrue(TEXT("A non-finite fall age has already vanished"), Fall(NaN, Lie).bGone);
    return true;
}

#endif
