#include "Character/LDBodyMotion.h"
#include "Character/LDCharacter.h"
#include "Character/LDHumanoidVisualComponent.h"
#include "Development/LDDemoContentIds.h"

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
    TestTrue(TEXT("The impact spark peaks at the strike and follows faster attack timing"),
        FMath::IsNearlyEqual(Evaluate(EAction::Attack, StrikeAt).Burst, 1.f)
        && FMath::IsNearlyEqual(Evaluate(EAction::Attack, .15f, .15f).Burst, 1.f));
    TestTrue(TEXT("After the swing the body stands where it started"), AtRest(Settled));

    const FChannels Charging = Evaluate(EAction::Cast, .15f), Released = Evaluate(EAction::Cast, .3f);
    TestTrue(TEXT("A cast grows an orb at the hand over its windup"), Charging.Charge > .9f && Charging.Burst == 0.f);
    TestTrue(TEXT("The release spends the orb in a burst"), Released.Charge == 0.f && Released.Burst > 0.f);
    TestTrue(TEXT("The cast burst peaks at the server release age"),
        FMath::IsNearlyEqual(Evaluate(EAction::Cast, .3f, .3f).Burst, 1.f));
    TestTrue(TEXT("After the cast the body stands where it started"), AtRest(Evaluate(EAction::Cast, .6f)));

    const FChannels Impact = Evaluate(EAction::Hit, 0.f), Knocked = Evaluate(EAction::Hit, .05f), Recovered = Evaluate(EAction::Hit, .3f);
    TestTrue(TEXT("A hit flashes at full strength the moment it lands"), Impact.Flash == 1.f);
    TestTrue(TEXT("A hit knocks the body back"), Knocked.Lunge < -15.f);
    TestTrue(TEXT("The flash is short and the body recovers within the 300 ms cue"),
        Evaluate(EAction::Hit, .18f).Flash == 0.f && AtRest(Recovered));
    const FChannels CastingHit=WithHit(EAction::Cast,.15f,.03f);
    TestTrue(TEXT("Being hit during a cast preserves charging and adds a chest impact and flash"),
        CastingHit.Charge>0.f && CastingHit.Flash>0.f && CastingHit.HitBurst>0.f);
    const FChannels PlainCast=WithHit(EAction::Cast,.15f,-1.f);
    TestTrue(TEXT("No observed hit adds no feedback"),PlainCast.Flash==0.f && PlainCast.HitBurst==0.f);
    TestTrue(TEXT("A hit cue is never doubled"),WithHit(EAction::Hit,.05f,.05f).Lunge==Knocked.Lunge);
    TestTrue(TEXT("A direct hit cue uses the same typed impact spark as a hit during another action"),
        WithHit(EAction::Hit,.05f,.05f).Burst==0.f && WithHit(EAction::Hit,.05f,.05f).HitBurst==Knocked.Burst);
    TestTrue(TEXT("An interrupted cast's impact holds only its presentation pose"),
        FMath::IsNearlyEqual(ImpactPoseAge(EAction::Cast,.15f,.02f),.13f,1e-5f)
        && FMath::IsNearlyEqual(ImpactPoseAge(EAction::Cast,.17f,.04f),.13f,1e-5f));
    TestEqual(TEXT("After the tiny hold the pose catches up without extending any action"),ImpactPoseAge(EAction::Cast,.20f,.07f),.20f);
    TestEqual(TEXT("A hit reaction itself is never delayed"),ImpactPoseAge(EAction::Hit,.03f,.03f),.03f);

    TestTrue(TEXT("An attack clip reaches its impact frame at the server release age"),
        FMath::IsNearlyEqual(ClipTime(EAction::Attack,.3f,.565f,.3f,1.f),.565f));
    TestTrue(TEXT("An attack clip tracks faster attack speed"), FMath::IsNearlyEqual(ClipTime(EAction::Attack,.15f,.565f,.15f,1.f),.565f));
    TestTrue(TEXT("A cast clip reaches its release frame at the server release age"),
        FMath::IsNearlyEqual(ClipTime(EAction::Cast,.2f,.635f,.2f,1.f),.635f));
    TestTrue(TEXT("An instant release presents its impact frame immediately"),
        FMath::IsNearlyEqual(ClipTime(EAction::Cast,.01f,.635f,0.f,1.f),.635f));
    TestTrue(TEXT("A hit clip plays at its authored rate"), FMath::IsNearlyEqual(ClipTime(EAction::Hit,.1f,0.f,-1.f,1.9f),.19f));
    TestTrue(TEXT("A clip without an impact frame plays at its authored rate"), FMath::IsNearlyEqual(ClipTime(EAction::Attack,.1f,0.f,.2f,2.f),.2f));
    TestTrue(TEXT("No action, a negative age or a non-finite input poses the first frame"),
        ClipTime(EAction::None,.5f,.5f,.2f,1.f)==0.f && ClipTime(EAction::Attack,-.1f,.5f,.2f,1.f)==0.f
        && ClipTime(EAction::Hit,NaN,0.f,-1.f,1.f)==0.f && ClipTime(EAction::Hit,.1f,0.f,-1.f,NaN)==0.f);

    TestTrue(TEXT("A different action kind is a new instance"), IsNewInstance(EAction::Attack, 1.0, EAction::Hit, 1.0, true));
    TestTrue(TEXT("A later start of the same kind restarts it"), IsNewInstance(EAction::Hit, 1.0, EAction::Hit, 1.2, true));
    TestFalse(TEXT("Report jitter of one instance does not restart it"), IsNewInstance(EAction::Hit, 1.0, EAction::Hit, 1.05, true));
    TestFalse(TEXT("Without a known age a continuing kind is never restarted"), IsNewInstance(EAction::Attack, 1.0, EAction::Attack, 9.0, false));
    TestFalse(TEXT("An ended action starts nothing"), IsNewInstance(EAction::Attack, 1.0, EAction::None, 2.0, true));

    ULDHumanoidVisualComponent* Visual=NewObject<ULDHumanoidVisualComponent>();
    Visual->Clock=1.0;
    Visual->SetHitReaction(0.f,ELDObservedHitLook::None,LDObservation::ShieldAbsorbed);
    Visual->Clock=1.04;
    Visual->SetHitReaction(0.f,ELDObservedHitLook::None,LDObservation::ShieldAbsorbed|LDObservation::ShieldBroken);
    TestTrue(TEXT("A shield break following an absorbed hit within the jitter window starts its own reaction"),
        Visual->HitFlags==(LDObservation::ShieldAbsorbed|LDObservation::ShieldBroken)
        && FMath::IsNearlyEqual(Visual->HitOnset,1.04));
    Visual->Clock=1.06;
    Visual->SetHitReaction(.02f,ELDObservedHitLook::None,LDObservation::ShieldAbsorbed|LDObservation::ShieldBroken);
    TestTrue(TEXT("A repeated report of the break does not restart its reaction"),FMath::IsNearlyEqual(Visual->HitOnset,1.04));

    const FRotator ForwardPush=KnockbackLean(FVector(300,0,0));
    const FRotator SidePush=KnockbackLean(FVector(0,-300,0));
    TestTrue(TEXT("The push leans opposite its direction in the body's local space"),
        ForwardPush.Quaternion().RotateVector(FVector::UpVector).X<0
        && SidePush.Quaternion().RotateVector(FVector::UpVector).Y>0);
    const FQuat MeshFacing=FRotator(0,90,0).Quaternion();
    const FVector Velocity(300,0,0);
    const FRotator MeshLean=KnockbackLean(MeshFacing.Inverse().RotateVector(Velocity));
    const FVector WorldLean=MeshFacing.RotateVector(MeshLean.Quaternion().RotateVector(FVector::UpVector));
    TestTrue(TEXT("An asset's corrective facing does not rotate the push response away from its direction"),
        FVector::DotProduct(WorldLean,Velocity)<0);
    const FQuat TurningFacing=MeshFacing*FRotator(0,45,0).Quaternion();
    const FRotator TurningLean=KnockbackLean(TurningFacing.Inverse().RotateVector(Velocity));
    const FVector TurningUp=(MeshFacing*FRotator(TurningLean.Pitch,45,TurningLean.Roll).Quaternion()).RotateVector(FVector::UpVector);
    TestTrue(TEXT("A body still turning leans against the push in world space"),
        FVector::DotProduct(TurningUp,Velocity)<0);
    TestTrue(TEXT("Push lean is bounded and ignores a stationary or non-finite velocity"),
        KnockbackLean(FVector(3000,0,0)).Pitch==ForwardPush.Pitch
        && KnockbackLean(FVector::ZeroVector).IsZero()
        && KnockbackLean(FVector(NaN,0,0)).IsZero());
    TArray<FLDObservedStatus> Statuses;
    TestFalse(TEXT("No public knockback status produces no pushed pose"),ALDCharacter::PresentationKnockback(Statuses));
    Statuses.Add({uint8(ELDDemoStatusId::Knockback),1,2000});
    TestTrue(TEXT("The authorized knockback status selects the pushed pose"),ALDCharacter::PresentationKnockback(Statuses));
    Visual->Clock=2.0;
    Visual->SetCue(0,-1.f,-1.f,ELDObservedHitLook::None,true);
    Visual->Clock=2.2;
    Visual->SetCue(0,-1.f,-1.f,ELDObservedHitLook::None,true);
    TestTrue(TEXT("Repeated knockback samples keep the same hit animation onset"),
        Visual->bKnockback && Visual->KnockbackAt==2.0);
    Visual->SetCue(0);
    TestFalse(TEXT("Losing the status ends the pushed pose"),Visual->bKnockback);

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
