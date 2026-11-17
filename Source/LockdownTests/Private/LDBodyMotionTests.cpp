#include "Character/LDBodyMotion.h"
#include "Character/LDCharacter.h"
#include "Character/LDHitFeel.h"
#include "Character/LDHumanoidVisualComponent.h"
#include "Character/LDImpactFx.h"
#include "Combat/LDCombatCues.h"
#include "Combat/LDCombatEvaluationTypes.h"
#include "Development/LDDemoContentIds.h"
#include "Vision/LDViewTypes.h"

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
        AtRest(Evaluate(EAction::None, .2f)) && AtRest(Evaluate(EAction::Attack, -.1f)) && AtRest(Evaluate(EAction::Cast, NaN)));

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

    const FChannels Impact = HitOverlay(0.f), Flash = HitOverlay(.03f);
    TestTrue(TEXT("A hit flashes at full strength the moment it lands and never moves the body"),
        Impact.Flash == 1.f && Impact.Lunge == 0.f && Flash.Lunge == 0.f && Flash.Flash > 0.f);
    TestTrue(TEXT("The flash is short and leaves nothing behind"),
        HitOverlay(HitOverlaySeconds).Flash == 0.f && AtRest(HitOverlay(.3f)));
    const FChannels CastingHit=WithHit(EAction::Cast,.15f,.03f);
    TestTrue(TEXT("Being hit during a cast preserves charging and adds a flash"),
        CastingHit.Charge>0.f && CastingHit.Flash>0.f);
    const FChannels PlainCast=WithHit(EAction::Cast,.15f,-1.f);
    TestTrue(TEXT("No observed hit adds no flash"),PlainCast.Flash==0.f);
    TestTrue(TEXT("Being hit never interrupts or bends the own swing"),
        WithHit(EAction::Attack,StrikeAt,.02f).Lunge==Evaluate(EAction::Attack,StrikeAt).Lunge);

    TestEqual(TEXT("Without a confirmation the pose runs at action age"),HitStopPoseAge(.2f,-1.f,.06f),.2f);
    TestTrue(TEXT("A confirmation holds the striking pose for its stop"),
        FMath::IsNearlyEqual(HitStopPoseAge(.15f,.0f,.06f),.15f,1e-5f)
        && FMath::IsNearlyEqual(HitStopPoseAge(.19f,.04f,.06f),.15f,1e-5f));
    TestTrue(TEXT("Then the pose catches up over twice the stop and never extends the action"),
        FMath::IsNearlyEqual(HitStopPoseAge(.27f,.12f,.06f),.24f,1e-5f) && HitStopPoseAge(.40f,.25f,.06f)==.40f);
    TestTrue(TEXT("A zero or non-finite stop never holds the pose"),
        HitStopPoseAge(.2f,.01f,0.f)==.2f && HitStopPoseAge(.2f,NaN,.06f)==.2f);
    TestTrue(TEXT("The camera kick rises fast, ends at its duration and scales with its distance"),
        FMath::IsNearlyEqual(KickOffset(.03f,6.f,.12f),6.f,1e-4f) && KickOffset(.12f,6.f,.12f)==0.f
        && KickOffset(-.01f,6.f,.12f)==0.f && KickOffset(.03f,0.f,.12f)==0.f);
    TestTrue(TEXT("The hand flare of a confirmation is short"),ConfirmSpark(.025f)>.99f && ConfirmSpark(ConfirmSparkSeconds)==0.f);

    TestTrue(TEXT("An attack clip reaches its impact frame at the server release age"),
        FMath::IsNearlyEqual(ClipTime(EAction::Attack,.3f,.565f,.3f,1.f),.565f));
    TestTrue(TEXT("An attack clip tracks faster attack speed"), FMath::IsNearlyEqual(ClipTime(EAction::Attack,.15f,.565f,.15f,1.f),.565f));
    TestTrue(TEXT("A cast clip reaches its release frame at the server release age"),
        FMath::IsNearlyEqual(ClipTime(EAction::Cast,.2f,.635f,.2f,1.f),.635f));
    TestTrue(TEXT("An instant release presents its impact frame immediately"),
        FMath::IsNearlyEqual(ClipTime(EAction::Cast,.01f,.635f,0.f,1.f),.635f));
    TestTrue(TEXT("A clip without an impact frame plays at its authored rate"), FMath::IsNearlyEqual(ClipTime(EAction::Attack,.1f,0.f,.2f,2.f),.2f));
    TestTrue(TEXT("No action, a negative age or a non-finite input poses the first frame"),
        ClipTime(EAction::None,.5f,.5f,.2f,1.f)==0.f && ClipTime(EAction::Attack,-.1f,.5f,.2f,1.f)==0.f
        && ClipTime(EAction::Cast,NaN,0.f,-1.f,1.f)==0.f && ClipTime(EAction::Cast,.1f,0.f,-1.f,NaN)==0.f);

    TestTrue(TEXT("A different action kind is a new instance"), IsNewInstance(EAction::Attack, 1.0, EAction::Cast, 1.0, true));
    TestTrue(TEXT("A later start of the same kind restarts it"), IsNewInstance(EAction::Cast, 1.0, EAction::Cast, 1.2, true));
    TestFalse(TEXT("Report jitter of one instance does not restart it"), IsNewInstance(EAction::Cast, 1.0, EAction::Cast, 1.05, true));
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
    const FGuid FirstHit=FGuid::NewGuid(), SecondHit=FGuid::NewGuid();
    Visual->Clock=2.;
    Visual->PendingImpacts.Reset();
    Visual->SetHitReaction(0.f,ELDObservedHitLook::None,0,FirstHit,2000);
    Visual->SetHitReaction(.01f,ELDObservedHitLook::None,0,FirstHit,2000);
    TestEqual(TEXT("The delayed observation of an immediate hit does not burst twice"),Visual->PendingImpacts.Num(),1);
    Visual->SetHitReaction(0.f,ELDObservedHitLook::None,0,SecondHit,2000);
    TestEqual(TEXT("Distinct hits at the same timestamp each queue a reaction"),Visual->PendingImpacts.Num(),2);
    Visual->Clock=2.1;
    Visual->SetHitReaction(0.f,ELDObservedHitLook::Fire,0,FGuid::NewGuid(),1960);
    TestEqual(TEXT("An older out-of-order hit does not replace the newer reaction"),Visual->PendingImpacts.Num(),2);
    Visual->SetHitReaction(.01f,ELDObservedHitLook::None,0,FirstHit,2000);
    TestEqual(TEXT("An out-of-order duplicate cannot restart the first hit"),Visual->PendingImpacts.Num(),2);

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
    Visual->SetCue(0,-1.f,-1.f,true);
    Visual->Clock=2.2;
    Visual->SetCue(0,-1.f,-1.f,true);
    TestTrue(TEXT("Repeated knockback samples keep the same hit animation onset"),
        Visual->bKnockback && Visual->KnockbackAt==2.0);
    Visual->SetCue(0);
    TestFalse(TEXT("Losing the status ends the pushed pose"),Visual->bKnockback);
    Visual->Clock=3.0;
    Visual->SetConfirm(.02f,.06f,1.4f);
    TestTrue(TEXT("A confirmation dates its onset back by the receipt's age"),
        FMath::IsNearlyEqual(Visual->ConfirmOnset,2.98) && Visual->ConfirmStopSeconds==.06f && Visual->ConfirmSparkScale==1.4f);

    const FLife Born = Appear(0.f), Grown = Appear(AppearSeconds);
    TestTrue(TEXT("A respawned body grows in from small inside a bright column"), Born.Scale < .3f && Born.Column == 1.f && Born.Glow == 1.f);
    TestTrue(TEXT("The appear effect ends at full size"), Grown.Scale == 1.f && Grown.Column == 0.f && Grown.Glow == 0.f);
    const float Lie = 1.067f;
    const FLife Lying = Fall(Lie, Lie), Gone = Fall(Lie + FadeSeconds, Lie);
    TestTrue(TEXT("A dead body lies down through its death clip at full size"), Lying.Scale == 1.f && Lying.Sink == 0.f && !Lying.bGone);
    TestTrue(TEXT("Then it sinks and shrinks away"), Gone.bGone && Gone.Scale == 0.f && Gone.Sink > 0.f);
    TestTrue(TEXT("A non-finite fall age has already vanished"), Fall(NaN, Lie).bGone);

    int64 SeenReceipt=0;
    TestTrue(TEXT("A new receipt confirms once while fresh"), LDHitFeel::TakeFreshReceipt(SeenReceipt,1000,1100));
    TestFalse(TEXT("The same or an older receipt cannot replay"), LDHitFeel::TakeFreshReceipt(SeenReceipt,1000,1100)
        || LDHitFeel::TakeFreshReceipt(SeenReceipt,900,1100));
    TestTrue(TEXT("An older receipt cannot lower the watermark"), SeenReceipt==1000);
    TestFalse(TEXT("A future receipt waits for the authority clock"), LDHitFeel::TakeFreshReceipt(SeenReceipt,1200,1100));
    TestTrue(TEXT("The future receipt can confirm once the clock catches up"), LDHitFeel::TakeFreshReceipt(SeenReceipt,1200,1200));
    TestFalse(TEXT("An old first-seen receipt is primed without playback"), LDHitFeel::TakeFreshReceipt(SeenReceipt,1400,1400+LDHitFeel::FreshMs));
    TestTrue(TEXT("A stale receipt still advances its watermark"), SeenReceipt==1400);
    LDHitFeel::FTuning Tuning;
    Tuning.StopSeconds=.06f;Tuning.CritStopSeconds=.1f;Tuning.KillStopSeconds=.16f;Tuning.BreakStopSeconds=.09f;
    Tuning.KickCm=6.f;Tuning.KillKickScale=2.f;Tuning.SparkScale=1.4f;Tuning.CritSparkScale=2.2f;
    const LDHitFeel::FConfirm Plain=LDHitFeel::ConfirmOf(Tuning,0), Crit=LDHitFeel::ConfirmOf(Tuning,LDObservation::Critical),
        Break=LDHitFeel::ConfirmOf(Tuning,LDObservation::ShieldBroken),
        Kill=LDHitFeel::ConfirmOf(Tuning,LDObservation::Critical|LDCombatCues::DealtLethal);
    TestTrue(TEXT("A critical hit holds, flares and kicks harder"),
        Plain.Tier==LDHitFeel::ETier::Normal && Plain.StopSeconds==.06f && Plain.KickCm==6.f
        && Crit.Tier==LDHitFeel::ETier::Critical && Crit.StopSeconds==.1f && Crit.SparkScale==2.2f && Crit.KickCm==9.f);
    TestTrue(TEXT("A kill outweighs a critical hit and a broken shield sits between"),
        Kill.Tier==LDHitFeel::ETier::Kill && Kill.StopSeconds==.16f && Kill.KickCm==12.f
        && Break.Tier==LDHitFeel::ETier::ShieldBreak && Break.StopSeconds==.09f);
    TestTrue(TEXT("Each receipt names its own onomatopoeia"),
        LDHitFeel::Word(0)==TEXT("啪！") && LDHitFeel::Word(LDObservation::ShieldAbsorbed)==TEXT("铛！")
        && LDHitFeel::Word(LDObservation::Critical|LDObservation::ShieldAbsorbed)==TEXT("暴击！")
        && LDHitFeel::Word(LDObservation::ShieldBroken)==TEXT("破盾！")
        && LDHitFeel::Word(LDCombatCues::DealtLethal)==TEXT("终结！"));
    FLDDamageEvaluation Dealt, Absorbed;
    Dealt.DamageP2=100;
    TestTrue(TEXT("The lethal receipt bit only marks a blow that dealt damage"),
        (LDCombatCues::ClassifyDealt(false,0,Dealt,true) & LDCombatCues::DealtLethal)!=0
        && (LDCombatCues::ClassifyDealt(false,0,Absorbed,true) & LDCombatCues::DealtLethal)==0
        && (LDCombatCues::ClassifyDealt(false,0,Dealt,false) & LDCombatCues::DealtLethal)==0);
    LDHitFeel::ResetAll();
    TestTrue(TEXT("Stepping moves one step and stops at the ends of the range"),
        LDHitFeel::Step(0,1) && FMath::IsNearlyEqual(LDHitFeel::Read().StopSeconds,.07f) && !LDHitFeel::IsAllDefault());
    int32 Guard=0;
    while(LDHitFeel::Step(0,-1) && ++Guard<100){}
    TestTrue(TEXT("The bottom of the range is zero and refuses another step"), LDHitFeel::Value(0)==0.f && !LDHitFeel::Step(0,-1));
    const TConstArrayView<LDHitFeel::FField> FeelFields=LDHitFeel::Fields();
    const int32 Words=FeelFields.IndexOfByPredicate([](const LDHitFeel::FField& F){ return FCString::Strcmp(F.ConsoleName,TEXT("ld.Feel.Words"))==0; });
    TestTrue(TEXT("A switch flips between on and off"),
        Words!=INDEX_NONE && LDHitFeel::Step(Words,-1) && !LDHitFeel::Read().bWords && LDHitFeel::Current(Words)==TEXT("关"));
    const int32 CastScale=FeelFields.IndexOfByPredicate([](const LDHitFeel::FField& F){ return FCString::Strcmp(F.ConsoleName,TEXT("ld.Feel.CastScale"))==0; });
    const float BeforeCastScale=LDHitFeel::Read().CastScale;
    TestTrue(TEXT("Stepping cast charge size changes its live visual setting"),
        CastScale!=INDEX_NONE && LDHitFeel::Step(CastScale,1) && LDHitFeel::Read().CastScale>BeforeCastScale);
    const int32 ConfirmVolume=FeelFields.IndexOfByPredicate([](const LDHitFeel::FField& F){ return FCString::Strcmp(F.ConsoleName,TEXT("ld.Feel.ConfirmVolume"))==0; });
    const float BeforeConfirmVolume=LDHitFeel::Read().ConfirmVolume;
    TestTrue(TEXT("Stepping the hit confirmation volume changes the live audio setting"),
        ConfirmVolume!=INDEX_NONE && LDHitFeel::Step(ConfirmVolume,-1) && LDHitFeel::Read().ConfirmVolume<BeforeConfirmVolume);
    LDHitFeel::ResetAll();
    TestTrue(TEXT("Reset returns every field to its starting value"), LDHitFeel::IsAllDefault() && LDHitFeel::Read().bWords
        && LDHitFeel::Read().bPredictSwing && LDHitFeel::Read().PredictGraceMs==150);

    const LDHitFeel::FTuning Feel=LDHitFeel::Read();
    const auto BurstPlain=LDImpactFx::Plan(ELDObservedHitLook::None,0,Feel,1.f);
    const auto BurstCrit=LDImpactFx::Plan(ELDObservedHitLook::None,LDObservation::Critical,Feel,1.f);
    const auto BurstShield=LDImpactFx::Plan(ELDObservedHitLook::None,LDObservation::ShieldAbsorbed|LDObservation::ShieldBroken,Feel,1.f);
    const auto BurstReduced=LDImpactFx::Plan(ELDObservedHitLook::None,0,Feel,.45f);
    LDHitFeel::FTuning Off=Feel;
    Off.ImpactScale=0.f;
    TestTrue(TEXT("A critical hit bursts more sparks and a broken shield adds shards"),
        BurstPlain.bEnabled && BurstCrit.Sparks>BurstPlain.Sparks && BurstCrit.SpeedScale>BurstPlain.SpeedScale
        && BurstShield.Shards>0 && BurstPlain.Shards==0);
    TestTrue(TEXT("Reduced effects thin the layers and zero or invalid settings disable them"),
        BurstReduced.Sparks<BurstPlain.Sparks && BurstReduced.Smoke<BurstPlain.Smoke && BurstReduced.Scale<BurstPlain.Scale
        && !LDImpactFx::Plan(ELDObservedHitLook::None,0,Off,1.f).bEnabled
        && !LDImpactFx::Plan(ELDObservedHitLook::None,0,Feel,NaN).bEnabled);
    LDHitFeel::FTuning Adjusted=Feel;
    Adjusted.ImpactSpeed*=2.f; Adjusted.ImpactSeconds*=2.f; Adjusted.ImpactGlow*=2.f;
    const auto BurstAdjusted=LDImpactFx::Plan(ELDObservedHitLook::None,0,Adjusted,1.f);
    TestTrue(TEXT("Live speed, duration and brightness changes reach the Niagara parameters"),
        FMath::IsNearlyEqual(BurstAdjusted.SpeedScale,BurstPlain.SpeedScale*2.f)
        && FMath::IsNearlyEqual(BurstAdjusted.LifeScale,BurstPlain.LifeScale*2.f)
        && FMath::IsNearlyEqual(BurstAdjusted.SparkColour.R,BurstPlain.SparkColour.R*2.f));
    TestTrue(TEXT("Shields burst cyan whatever the hit's look"),
        LDImpactFx::Colour(ELDObservedHitLook::Fire,LDObservation::ShieldAbsorbed,LDImpactFx::EKind::Spark)
            ==LDImpactFx::Colour(ELDObservedHitLook::None,0,LDImpactFx::EKind::Shard)
        && LDImpactFx::Colour(ELDObservedHitLook::Fire,0,LDImpactFx::EKind::Spark)!=LDImpactFx::Colour(ELDObservedHitLook::None,0,LDImpactFx::EKind::Spark));

    // The own swing prediction: starts when due and engaged, confirms on the authority's start, else withdraws.
    using LDHitFeel::ESwingStep;
    LDHitFeel::FSwingFacts Due;
    Due.bEnabled=Due.bCurrent=Due.bEngaged=true;
    Due.NowMs=1000; Due.NextAttackAtMs=1000; Due.GraceMs=200;
    LDHitFeel::FSwingPrediction Swing;
    LDHitFeel::FSwingFacts NotDue=Due; NotDue.NowMs=999;
    TestTrue(TEXT("No swing is predicted before the cadence is due"), LDHitFeel::AdvanceSwing(Swing,NotDue)==ESwingStep::None && !Swing.bActive);
    LDHitFeel::FSwingFacts SwingOff=Due; SwingOff.bEnabled=false;
    TestTrue(TEXT("The switch off predicts nothing"), LDHitFeel::AdvanceSwing(Swing,SwingOff)==ESwingStep::None && !Swing.bActive);
    TestTrue(TEXT("A due, engaged swing starts at once"),
        LDHitFeel::AdvanceSwing(Swing,Due)==ESwingStep::Started && Swing.bActive && Swing.KeyAtMs==1000 && Swing.StartedAtMs==1000);
    LDHitFeel::FSwingFacts Holding=Due; Holding.NowMs=1150;
    TestTrue(TEXT("A started swing waits within its grace"), LDHitFeel::AdvanceSwing(Swing,Holding)==ESwingStep::None && Swing.bActive);
    LDHitFeel::FSwingFacts Started=Holding; Started.LastBasicStartMs=1040; Started.NextAttackAtMs=1840;
    TestTrue(TEXT("The authority's start at or after the key confirms it"),
        LDHitFeel::AdvanceSwing(Swing,Started)==ESwingStep::Confirmed && !Swing.bActive && Swing.SpentKeyAtMs==1000);
    LDHitFeel::FSwingPrediction Lapsed;
    LDHitFeel::AdvanceSwing(Lapsed,Due);
    LDHitFeel::FSwingFacts Timeout=Due; Timeout.NowMs=1201;
    TestTrue(TEXT("No authority start within the grace withdraws it and never predicts that key again while engaged"),
        LDHitFeel::AdvanceSwing(Lapsed,Timeout)==ESwingStep::Withdrawn && !Lapsed.bActive
        && LDHitFeel::AdvanceSwing(Lapsed,Timeout)==ESwingStep::None && !Lapsed.bActive);
    LDHitFeel::FSwingFacts Apart=Timeout; Apart.bEngaged=false;
    LDHitFeel::AdvanceSwing(Lapsed,Apart);
    TestTrue(TEXT("Engaging again may predict the same due swing again"), LDHitFeel::AdvanceSwing(Lapsed,Timeout)==ESwingStep::Started);
    for (int32 Case=0; Case<3; ++Case)
    {
        LDHitFeel::FSwingPrediction Cut;
        LDHitFeel::AdvanceSwing(Cut,Due);
        LDHitFeel::FSwingFacts Next=Due;
        Next.NowMs=1050;
        if (Case==0) { Next.bBusy=true; } else if (Case==1) { Next.bBlocked=true; } else { Next.bEngaged=false; }
        TestTrue(FString::Printf(TEXT("A cast or dash, a basic block or a disengaged order withdraws it (case %d)"),Case),
            LDHitFeel::AdvanceSwing(Cut,Next)==ESwingStep::Withdrawn && !Cut.bActive);
    }
    LDHitFeel::FSwingPrediction First;
    LDHitFeel::FSwingFacts Fresh=Due; Fresh.NextAttackAtMs=0;
    TestTrue(TEXT("A life's first swing (no cadence, no start yet) is predicted"),
        LDHitFeel::AdvanceSwing(First,Fresh)==ESwingStep::Started && First.StartedAtMs==1000);
    TestTrue(TEXT("The predicted pose holds short of the release flash"),
        FMath::IsNearlyEqual(LDHitFeel::SwingHoldSeconds(400),.3f) && LDHitFeel::SwingHoldSeconds(400)<.4f*.8f && LDHitFeel::SwingHoldSeconds(-5)==0.f);
    TestTrue(TEXT("A swing trail follows only a current attack's moving window"),
        !LDHitFeel::SwingTrailWindow(.1f,.4f) && LDHitFeel::SwingTrailWindow(.3f,.4f)
        && !LDHitFeel::SwingTrailWindow(.5f,.4f) && !LDHitFeel::SwingTrailWindow(NaN,.4f)
        && !LDHitFeel::SwingTrailWindow(.1f,0.f));
    TestTrue(TEXT("A withdrawn swing rewinds to rest and then ends"),
        FMath::IsNearlyEqual(LDBodyMotion::RetractAge(.3f,0.f),.3f) && LDBodyMotion::RetractAge(.3f,LDBodyMotion::RetractSeconds*.5f)<.3f
        && LDBodyMotion::RetractAge(.3f,LDBodyMotion::RetractSeconds)<0.f && LDBodyMotion::RetractAge(.3f,-.01f)<0.f);
    return true;
}

#endif
