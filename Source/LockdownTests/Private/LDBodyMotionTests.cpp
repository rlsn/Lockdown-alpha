#include "Character/LDBodyMotion.h"
#include "Character/LDCharacter.h"
#include "Character/LDHitFeel.h"
#include "Character/LDHumanoidVisualComponent.h"
#include "Character/LDHitVfx.h"
#include "Combat/LDCombatElements.h"
#include "Framework/LDDemoEntry.h"
#include "Development/LDDemoContentCatalog.h"
#include "Character/LDImpactFx.h"
#include "Combat/LDCombatCues.h"
#include "Combat/LDCombatEvaluationTypes.h"
#include "Development/LDDemoContentIds.h"
#include "UI/LDBroadcastStyle.h"
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
    TestTrue(TEXT("The authority keeps the strongest receipt of one instant: kill, critical, broken shield, then any hit"),
        LDCombatCues::DealtRank(LDCombatCues::DealtLethal)>LDCombatCues::DealtRank(LDObservation::Critical|LDObservation::ShieldBroken)
        && LDCombatCues::DealtRank(LDObservation::Critical)>LDCombatCues::DealtRank(LDObservation::ShieldBroken)
        && LDCombatCues::DealtRank(LDObservation::ShieldBroken)>LDCombatCues::DealtRank(LDObservation::ShieldAbsorbed)
        && LDCombatCues::DealtRank(LDObservation::ShieldAbsorbed)==LDCombatCues::DealtRank(0));
    // A confirmed prediction hands over to the authority's start even within the jitter window of a repeated report.
    ULDHumanoidVisualComponent* Handover=NewObject<ULDHumanoidVisualComponent>();
    Handover->Clock=2.1;
    Handover->SetCue(1,0.f,.3f);
    Handover->SetCue(1,.1f,.3f);
    TestTrue(TEXT("An earlier report of the same swing is jitter"),FMath::IsNearlyEqual(Handover->ActionOnset,2.1));
    Handover->SetCue(1,.1f,.3f,false,true);
    TestTrue(TEXT("The handover adopts the authority's earlier start"),FMath::IsNearlyEqual(Handover->ActionOnset,2.0));
    TestTrue(TEXT("Each receipt names its own onomatopoeia"),
        LDHitFeel::Word(0)==TEXT("啪！") && LDHitFeel::Word(LDObservation::ShieldAbsorbed)==TEXT("铛！")
        && LDHitFeel::Word(LDObservation::Critical|LDObservation::ShieldAbsorbed)==TEXT("暴击！")
        && LDHitFeel::Word(LDObservation::ShieldBroken)==TEXT("破盾！")
        && LDHitFeel::Word(LDCombatCues::DealtLethal)==TEXT("终结！")
        && LDHitFeel::Word(LDCombatCues::DealtOverdrive)==TEXT("爆劲！")
        && LDHitFeel::Word(LDCombatCues::DealtOverdrive|LDCombatCues::DealtLethal)==TEXT("终结！"));
    TestTrue(TEXT("An overdrive blow weighs like a critical hit and outranks one in the same instant"),
        LDHitFeel::TierOf(LDCombatCues::DealtOverdrive)==LDHitFeel::ETier::Critical
        && LDCombatCues::DealtRank(LDCombatCues::DealtOverdrive)>LDCombatCues::DealtRank(LDObservation::Critical)
        && LDCombatCues::DealtRank(LDCombatCues::DealtLethal)>LDCombatCues::DealtRank(LDCombatCues::DealtOverdrive));
    FLDDamageEvaluation Dealt, Absorbed;
    Dealt.DamageP2=100;
    TestTrue(TEXT("The lethal receipt bit only marks a blow that dealt damage"),
        (LDCombatCues::ClassifyDealt(false,0,Dealt,true) & LDCombatCues::DealtLethal)!=0
        && (LDCombatCues::ClassifyDealt(false,0,Absorbed,true) & LDCombatCues::DealtLethal)==0
        && (LDCombatCues::ClassifyDealt(false,0,Dealt,false) & LDCombatCues::DealtLethal)==0);
    TestTrue(TEXT("The overdrive receipt bit only marks an overdrive blow that dealt damage"),
        (LDCombatCues::ClassifyDealt(false,0,Dealt,false,true) & LDCombatCues::DealtOverdrive)!=0
        && (LDCombatCues::ClassifyDealt(false,0,Absorbed,false,true) & LDCombatCues::DealtOverdrive)==0
        && (LDCombatCues::ClassifyDealt(false,0,Dealt,false) & LDCombatCues::DealtOverdrive)==0);
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

    TestTrue(TEXT("A withdrawn swing rewinds to rest and then ends"),
        FMath::IsNearlyEqual(LDBodyMotion::RetractAge(.3f,0.f),.3f) && LDBodyMotion::RetractAge(.3f,LDBodyMotion::RetractSeconds*.5f)<.3f
        && LDBodyMotion::RetractAge(.3f,LDBodyMotion::RetractSeconds)<0.f && LDBodyMotion::RetractAge(.3f,-.01f)<0.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBodyMotionSliceLayersTest, "Lockdown.Rules.DemoBodyMotion.SliceLayers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDBodyMotionSliceLayersTest::RunTest(const FString& Parameters)
{
    using namespace LDSliceVfx;
    const float NaN=std::numeric_limits<float>::quiet_NaN();
    const FVector Chest(0,0,120), Feet(0,0,0), Dir(1,0,0), From(-300,0,120);
    FLook Own; Own.bOwn=Own.bFocus=true;
    FLook Other;
    const auto Find=[](const FRecipe& Recipe, ETex Tex, EPlace Place)
    { return Recipe.Layers.FindByPredicate([Tex,Place](const FLayer& Layer){ return Layer.Tex==Tex && Layer.Place==Place; }); };
    TestTrue(TEXT("The slice shows unflagged plain, water and electric hits; any outcome flag or other look keeps the hit layers"),
        ShowsHit(ELDObservedHitLook::None,0) && ShowsHit(ELDObservedHitLook::Water,0) && ShowsHit(ELDObservedHitLook::Electric,0)
        && !ShowsHit(ELDObservedHitLook::Fire,0) && !ShowsHit(ELDObservedHitLook::Magic,0) && !ShowsHit(ELDObservedHitLook::None,LDObservation::Critical)
        && !ShowsHit(ELDObservedHitLook::Water,LDObservation::ShieldAbsorbed) && !ShowsHit(ELDObservedHitLook::Electric,LDObservation::Dodged));
    // Tiers differ by silhouette (their own sheet), not only by size: basic < finisher < skill < reaction.
    const FRecipe Basic=Impact(EStrike::Basic,Chest,Feet,Dir,From,false,Own), Finisher=Impact(EStrike::Finisher,Chest,Feet,Dir,From,false,Own);
    const FRecipe Skill=Impact(EStrike::Skill,Chest,Feet,Dir,From,false,Own), Reaction=Conduct(Chest,Feet,450.f,Own);
    const FLayer* BasicHit=Find(Basic,ETex::HitBasic,EPlace::Facing);
    const FLayer* FinisherHit=Find(Finisher,ETex::HitFinisher,EPlace::Facing);
    const FLayer* SkillHit=Find(Skill,ETex::HitSkill,EPlace::Facing);
    const FLayer* ReactionHit=Find(Reaction,ETex::HitSkill,EPlace::Facing);
    TestTrue(TEXT("Each hit tier has its own burst sheet and grows basic < finisher <= skill < reaction"),
        BasicHit && FinisherHit && SkillHit && ReactionHit && !Find(Basic,ETex::HitFinisher,EPlace::Facing)
        && BasicHit->SizeCm<FinisherHit->SizeCm && FinisherHit->SizeCm<=SkillHit->SizeCm && SkillHit->SizeCm<ReactionHit->SizeCm);
    TestTrue(TEXT("Bursts point along the strike with their hit point at the sheet's pivot"),
        BasicHit && BasicHit->Dir.Equals(Dir) && FMath::IsNearlyEqual(BasicHit->Pivot,.3f) && BasicHit->bTop);
    const FRecipe Hose=Impact(EStrike::Hose,Chest,Feet,Dir,From,false,Own);
    const FLayer* Wet=Find(Hose,ETex::WetDecal,EPlace::Flat);
    TestTrue(TEXT("A water hit bursts in the water ink, splashes along the jet and wets the ground for over a second"),
        Find(Hose,ETex::HitSkill,EPlace::Facing) && Find(Hose,ETex::WaterSplash,EPlace::Facing) && Wet && Wet->Seconds>1.f
        && Find(Hose,ETex::HitSkill,EPlace::Facing)->Glow.B>Find(Hose,ETex::HitSkill,EPlace::Facing)->Glow.R);
    const FRecipe Zap=Impact(EStrike::Zap,Chest,Feet,Dir,From,false,Own), Bounce=Impact(EStrike::Zap,Chest,Feet,Dir,From,true,Own);
    const FLayer* Arc=Find(Zap,ETex::ShockArcs,EPlace::Strip);
    TestTrue(TEXT("A zap hit arcs from its source to the body and sparks; a bounce arcs from the last body without the skill burst"),
        Arc && Arc->At.Equals(From) && Arc->End.Equals(Chest) && Arc->Variants==8 && Find(Zap,ETex::ShockSpark,EPlace::Facing)
        && Find(Zap,ETex::HitSkill,EPlace::Facing) && Find(Bounce,ETex::ShockArcs,EPlace::Strip) && !Find(Bounce,ETex::HitSkill,EPlace::Facing));
    const FLayer* Field=Find(Reaction,ETex::ShockField,EPlace::Flat);
    TestTrue(TEXT("Conduct arcs along the ground out to its splash radius over a field whose edge is exactly that radius"),
        Field && FMath::IsNearlyEqual(Field->SizeCm*FieldEdge*.5f,450.f,1.f) && FMath::IsNearlyEqual(FieldSizeCm(220.f)*FieldEdge*.5f,220.f,.1f) && Reaction.Layers.ContainsByPredicate([](const FLayer& L){ return L.Place==EPlace::GroundStrip && FVector::Dist(L.At,L.End)>400.f; })
        && Reaction.Layers.FilterByPredicate([](const FLayer& L){ return L.Tex==ETex::ShockArcs && L.Place==EPlace::GroundStrip; }).Num()==3
        && ReactionHit->Ink.B>ReactionHit->Ink.R);
    // Own strikes: full size with the ball orange; others' are white, smaller and fainter; reduced effects drop the optional layers.
    const FLayer* OtherHit=Find(Impact(EStrike::Basic,Chest,Feet,Dir,From,false,Other),ETex::HitBasic,EPlace::Facing);
    TestTrue(TEXT("Others' fights are smaller, fainter and white; the local player's strikes carry the ball orange"),
        OtherHit && OtherHit->SizeCm<BasicHit->SizeCm && OtherHit->Alpha<BasicHit->Alpha && BasicHit->Glow.R>2.f*BasicHit->Glow.B
        && FMath::IsNearlyEqual(OtherHit->Glow.R,OtherHit->Glow.B,.01f));
    FLook Reduced=Own; Reduced.EffectScale=.45f;
    TestTrue(TEXT("Reduced effects drop the optional arcs and dim the rest"), Conduct(Chest,Feet,450.f,Reduced).Layers.Num()==Reaction.Layers.Num()-2
        && Find(Conduct(Chest,Feet,450.f,Reduced),ETex::HitSkill,EPlace::Facing)->Alpha<ReactionHit->Alpha);
    FLook Off=Own; Off.Size=0.f;
    FLook Broken=Own; Broken.Seconds=NaN;
    TestTrue(TEXT("A zero or invalid tuning or effect scale shows nothing"), Impact(EStrike::Basic,Chest,Feet,Dir,From,false,Off).Layers.IsEmpty()
        && SwingRelease(1,Chest,Dir,Broken).Layers.IsEmpty() && Impact(EStrike::None,Chest,Feet,Dir,From,false,Own).Layers.IsEmpty());
    // The swing: chalk telegraph on the ground through the windup, then the step's slash at the chest.
    const FRecipe Chalk=SwingTelegraph(2,Feet,Dir,.3f,Own);
    TestTrue(TEXT("A swing's telegraph is the coming slash in white chalk on the ground, gone after the windup"),
        Chalk.Layers.Num()==1 && Chalk.Layers[0].Mesh==EMesh::Slash2 && Chalk.Layers[0].Place==EPlace::GroundSlash && Chalk.Layers[0].bTelegraph
        && Chalk.Layers[0].Glow.R==Chalk.Layers[0].Glow.B && Evaluate(Chalk.Layers[0],.15f).bVisible && !Evaluate(Chalk.Layers[0],.36f).bVisible
        && SwingTelegraph(1,Feet,Dir,.05f,Own).Layers.IsEmpty());
    const FRecipe Slash1=SwingRelease(1,Chest,Dir,Own), Slash3=SwingRelease(3,Chest,Dir,Own);
    TestTrue(TEXT("Each combo step has its own slash; the finisher lasts longer"),
        Slash1.Layers.Num()==1 && Slash1.Layers[0].Mesh==EMesh::Slash1 && Slash3.Layers[0].Mesh==EMesh::Slash3
        && Slash3.Layers[0].Seconds>Slash1.Layers[0].Seconds && Slash3.Layers[0].SizeCm<Slash1.Layers[0].SizeCm && SwingRelease(2,Chest,Dir,Own).Layers[0].Mesh==EMesh::Slash2);
    const FLayer& Cut=Slash1.Layers[0];
    TestTrue(TEXT("A slash reveals from its tail to its head, then erodes"),
        Evaluate(Cut,0.f).WindowLo>.9f && Evaluate(Cut,Cut.RevealSeconds).WindowLo<0.f && Evaluate(Cut,Cut.ErodeFrom).Dissolve==0.f
        && Evaluate(Cut,Cut.ErodeFrom+Cut.ErodeSeconds*.5f).Dissolve>.4f);
    const FLayer& Spark=*Find(Zap,ETex::ShockSpark,EPlace::Facing);
    TestTrue(TEXT("Flipbooks step at their authored rate and hold their last frame; a delayed layer waits"),
        Evaluate(Spark,0.f).Frame==0.f && Evaluate(Spark,.1f).Frame==3.f && Evaluate(Spark,Spark.Seconds-.01f).Frame==15.f
        && !Evaluate(*Find(Hose,ETex::WaterSplash,EPlace::Facing),.01f).bVisible && IsOver(Spark,Spark.Seconds) && !IsOver(Spark,0.f));
    const FLayer Loop=GroundField(Feet,220.f,Own);
    TestTrue(TEXT("The ground field loops its frames and never ends on its own"),
        Loop.bLoop && !IsOver(Loop,100.f) && Evaluate(Loop,1.f+1.f/16.f).Frame==1.f && Evaluate(Loop,0.f).Fade<Evaluate(Loop,1.f).Fade);
    const FRecipe Lane=SkillTelegraph(Feet,Dir,600.f,160.f,.2f,Own);
    TestTrue(TEXT("A skill's chalk lane covers its footprint along the facing"), Lane.Layers.Num()==1 && Lane.Layers[0].Tex==ETex::ChalkLane
        && Lane.Layers[0].End.Equals(FVector(600,0,0)) && FMath::IsNearlyEqual(Lane.Layers[0].SizeCm,160.f) && Lane.Layers[0].Place==EPlace::GroundStrip);
    TestTrue(TEXT("The shock element is the broadcast shock ink"), LDHitVfx::ElementTint(uint8(ELDCombatElement::Shock))==LDBroadcast::Shock
        && LDHitVfx::ElementTint(uint8(ELDCombatElement::Water))==LDBroadcast::Water);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBodyMotionHitLayersTest, "Lockdown.Rules.DemoBodyMotion.HitLayers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDBodyMotionHitLayersTest::RunTest(const FString& Parameters)
{
    using namespace LDHitVfx;
    auto Has=[](const FRecipe& Recipe, EMask Mask, EPlace Place)
    { return Recipe.Layers.ContainsByPredicate([Mask,Place](const FLayer& Layer){ return Layer.Mask==Mask && Layer.Place==Place; }); };
    // Unflagged plain, water and electric hits are the slice's (LDSliceVfx::Impact): no hit layers here.
    TestTrue(TEXT("The slice's hits have no hit layers; every other hit keeps them"),
        ForHit(ELDObservedHitLook::None,0,1.f).Layers.IsEmpty() && ForHit(ELDObservedHitLook::Water,0,1.f).Layers.IsEmpty()
        && ForHit(ELDObservedHitLook::Electric,0,1.f).Layers.IsEmpty() && !ForHit(ELDObservedHitLook::None,LDObservation::Critical,1.f).Layers.IsEmpty()
        && !ForHit(ELDObservedHitLook::Fire,0,1.f).Layers.IsEmpty());
    const FRecipe Plain=ForHit(ELDObservedHitLook::Fire,0,1.f);
    const FRecipe Crit=ForHit(ELDObservedHitLook::None,LDObservation::Critical,1.f);
    TestTrue(TEXT("A fire hit flashes a star and burns the ground"), Has(Plain,EMask::Star,EPlace::Sprite) && Has(Plain,EMask::Burn,EPlace::Ground));
    const FLayer* PlainStar=Plain.Layers.FindByPredicate([](const FLayer& L){ return L.Place==EPlace::Sprite; });
    const FLayer* CritStar=Crit.Layers.FindByPredicate([](const FLayer& L){ return L.Place==EPlace::Sprite; });
    TestTrue(TEXT("A critical hit changes shape, not only colour: a larger cross star and a second ring"),
        CritStar && PlainStar && CritStar->Mask==EMask::Cross && CritStar->SizeCm>=1.5f*PlainStar->SizeCm
        && Crit.Layers.FilterByPredicate([](const FLayer& L){ return L.Mask==EMask::Ring; }).Num()==2);
    const FRecipe Shield=ForHit(ELDObservedHitLook::Fire,LDObservation::ShieldAbsorbed,1.f);
    const FRecipe Broken=ForHit(ELDObservedHitLook::None,LDObservation::ShieldAbsorbed|LDObservation::ShieldBroken,1.f);
    const FLayer* Shell=Shield.Layers.FindByPredicate([](const FLayer& L){ return L.Place==EPlace::Shell; });
    const FLayer* Burst=Broken.Layers.FindByPredicate([](const FLayer& L){ return L.Place==EPlace::Shell; });
    TestTrue(TEXT("A shield shows its hex shell and a broken one bursts outwards over a wide ring"),
        Shell && Shell->Mask==EMask::Hex && Burst && Burst->ToScale>Shell->ToScale && Has(Broken,EMask::Ring,EPlace::Ground));
    TestTrue(TEXT("Each look has its own signature"),
        Has(ForHit(ELDObservedHitLook::Fire,0,1.f),EMask::Burn,EPlace::Ground) && Has(ForHit(ELDObservedHitLook::Water,LDObservation::Critical,1.f),EMask::Ripple,EPlace::Ground)
        && Has(ForHit(ELDObservedHitLook::Electric,LDObservation::Critical,1.f),EMask::Bolt,EPlace::Upright) && Has(ForHit(ELDObservedHitLook::Magic,0,1.f),EMask::Sigil,EPlace::Ground)
        && Has(Shield,EMask::Burn,EPlace::Ground));
    const FRecipe Reduced=ForHit(ELDObservedHitLook::None,LDObservation::Critical,.45f);
    TestTrue(TEXT("Reduced effects drop only the repeat layers and dim the rest; the tier's shape stays"),
        Reduced.Layers.Num()==Crit.Layers.Num()-1 && Has(Reduced,EMask::Cross,EPlace::Sprite) && Has(Reduced,EMask::Ring,EPlace::Ground)
        && Reduced.Layers[0].Glow.R<Crit.Layers[0].Glow.R && Has(ForHit(ELDObservedHitLook::Electric,LDObservation::Critical,.45f),EMask::Bolt,EPlace::Upright));
    const float NaN=std::numeric_limits<float>::quiet_NaN();
    TestTrue(TEXT("A zero or invalid effect scale shows nothing"),
        ForHit(ELDObservedHitLook::Fire,0,0.f).Layers.IsEmpty() && ForHit(ELDObservedHitLook::Fire,0,NaN).Layers.IsEmpty() && ForKill(NaN).Layers.IsEmpty());
    const FRecipe Kill=ForKill(1.f);
    const FLayer* Crack=Kill.Layers.FindByPredicate([](const FLayer& L){ return L.Mask==EMask::Crack; });
    TestTrue(TEXT("A fall cracks the ground for two seconds under a shock ring"), Crack && Crack->Place==EPlace::Ground
        && FMath::IsNearlyEqual(Crack->Seconds,2.f) && Has(Kill,EMask::Ring,EPlace::Ground) && ForKill(.45f).Layers.Num()==Kill.Layers.Num()-1);
    for (int32 Look=0; Look<=int32(ELDObservedHitLook::Electric); ++Look)
    {
        for (uint8 Flags=0; Flags<=LDObservation::HitFlagsMask; ++Flags)
        {
            TestTrue(TEXT("Every hit fits the fixed layer budget"), ForHit(ELDObservedHitLook(Look),Flags,1.f).Layers.Num()<=MaxLayers);
        }
    }
    const FLayer& Star=*PlainStar;
    TestTrue(TEXT("Brightness rises to its peak, fades and ends at its duration; scale grows towards its end"),
        Brightness(Star,-.01f)==0.f && FMath::IsNearlyEqual(Brightness(Star,Star.PeakAt*Star.Seconds),1.f,.01f)
        && Brightness(Star,.8f*Star.Seconds)<Brightness(Star,.4f*Star.Seconds) && Brightness(Star,Star.Seconds)==0.f
        && ScaleAt(Star,0.f)<ScaleAt(Star,Star.Seconds) && FMath::IsNearlyEqual(ScaleAt(Star,Star.Seconds),Star.ToScale));
    const FLayer* Echo=Crit.Layers.FindByPredicate([](const FLayer& L){ return L.bOptional; });
    TestTrue(TEXT("A delayed layer stays dark until its delay"), Echo && Brightness(*Echo,Echo->DelaySeconds*.5f)==0.f && Brightness(*Echo,Echo->DelaySeconds+.02f)>0.f);
    // Distance compensation is one factor for every sprite: far away the ordinary star holds 60 px of 1080 lines and a
    // critical cross stays 1.5x it; up close nothing grows.
    const float Far=SpriteCompensation(2000.f,90.f);
    const float PlainFar=PlainStar->SizeCm*PlainStar->ToScale*Far, CritFar=CritStar->SizeCm*CritStar->ToScale*Far;
    const float ViewCm=2.f*2000.f*FMath::Tan(FMath::DegreesToRadians(45.f));
    TestTrue(TEXT("Far away every sprite grows by one factor, so the tiers keep their size ratio"),
        Far>1.f && PlainFar/ViewCm*1080.f>=MinSpritePx-.5f && FMath::IsNearlyEqual(CritFar/PlainFar,CritStar->SizeCm/PlainStar->SizeCm,.001f)
        && CritFar>=1.5f*PlainFar);
    TestTrue(TEXT("Up close or with an invalid view nothing is compensated"), SpriteCompensation(100.f,90.f)==1.f
        && SpriteCompensation(NaN,90.f)==1.f && SpriteCompensation(2000.f,0.f)==1.f);
    // Saved and live tuning reach the layers; an impact size or duration of zero turns them off.
    LDHitFeel::ResetAll();
    const LDHitFeel::FTuning Start=LDHitFeel::Read();
    const FLayerTuning Neutral=TuningOf(Start,1.f,1.f,1.f);
    TestTrue(TEXT("At the starting controls and neutral asset values the layers are unchanged"), FMath::IsNearlyEqual(Neutral.Size,1.f)
        && FMath::IsNearlyEqual(Neutral.Seconds,1.f) && FMath::IsNearlyEqual(Neutral.Glow,1.f)
        && FMath::IsNearlyEqual(ForHit(ELDObservedHitLook::Fire,0,1.f,Neutral).Layers[0].SizeCm,PlainStar->SizeCm));
    LDHitFeel::FTuning Bigger=Start; Bigger.ImpactScale*=2.f; Bigger.ImpactSeconds*=2.f;
    const FRecipe Tuned=ForHit(ELDObservedHitLook::None,LDObservation::Critical,1.f,TuningOf(Bigger,1.f,1.f,.5f));
    TestTrue(TEXT("Live size and duration and the saved glow scale every layer, delays included"),
        FMath::IsNearlyEqual(Tuned.Layers[0].SizeCm,2.f*Crit.Layers[0].SizeCm) && FMath::IsNearlyEqual(Tuned.Layers[0].Seconds,2.f*Crit.Layers[0].Seconds)
        && FMath::IsNearlyEqual(Tuned.Layers[2].DelaySeconds,2.f*Crit.Layers[2].DelaySeconds) && Tuned.Layers[0].Glow.R<Crit.Layers[0].Glow.R);
    LDHitFeel::FTuning NoSize=Start; NoSize.ImpactScale=0.f;
    LDHitFeel::FTuning NoTime=Start; NoTime.ImpactSeconds=0.f;
    TestTrue(TEXT("An impact size or duration of zero, or a zero asset value, shows no layers"),
        ForHit(ELDObservedHitLook::Fire,0,1.f,TuningOf(NoSize,1.f,1.f,1.f)).Layers.IsEmpty()
        && ForKill(1.f,TuningOf(NoTime,1.f,1.f,1.f)).Layers.IsEmpty() && ForHit(ELDObservedHitLook::Fire,0,1.f,TuningOf(Start,0.f,1.f,1.f)).Layers.IsEmpty());
    const FScreenFlash KillFlash=ScreenFlashOf(LDCombatCues::DealtLethal|LDObservation::Critical,false);
    const FScreenFlash CritFlash=ScreenFlashOf(LDObservation::Critical,false);
    TestTrue(TEXT("A kill flashes red over a critical hit's gold, a broken shield cyan, an ordinary hit nothing"),
        KillFlash.Colour.R>KillFlash.Colour.G*4.f && CritFlash.Colour.G>CritFlash.Colour.B && ScreenFlashOf(LDObservation::ShieldBroken,false).Colour.B>.5f
        && ScreenFlashOf(0,false).Seconds==0.f && KillFlash.Fringe>CritFlash.Fringe);
    TestTrue(TEXT("Reduced effects turn the screen flash off; the flash fades out over its duration"),
        ScreenFlashOf(LDCombatCues::DealtLethal,true).Seconds==0.f && ScreenFlashAlpha(KillFlash,0.f)==KillFlash.Colour.A
        && ScreenFlashAlpha(KillFlash,KillFlash.Seconds*.5f)<KillFlash.Colour.A && ScreenFlashAlpha(KillFlash,KillFlash.Seconds)==0.f);
    // The particle kit: every skill has its own look, element hits and effects have presets, dashes are detected.
    TSet<uint8> Abilities;
    TSet<FName> AbilityIds;
    for (const FSkillLook& Skill : SkillLooks())
    {
        Abilities.Add(Skill.Ability);
        AbilityIds.Add(FName(Skill.Id));
        TestTrue(FString::Printf(TEXT("%s has its own look"), Skill.Id), SkillLookOf(Skill.Ability) == &Skill && SkillLookOf(FName(Skill.Id)) == &Skill
            && !BurstOf(Skill.Release, Skill.Tint, 1.f).IsEmpty());
    }
    TestTrue(TEXT("The skill table names 24 distinct abilities"), SkillLooks().Num() == 24 && Abilities.Num() == 24 && AbilityIds.Num() == 24
        && SkillLookOf(uint8(0)) == nullptr && SkillLookOf(FName()) == nullptr);
    for (int32 Kind = 0; Kind < int32(EBurst::Count); ++Kind)
    {
        const FBurst Full = BurstOf(EBurst(Kind), FLinearColor::White, 1.f);
        const FBurst Thin = BurstOf(EBurst(Kind), FLinearColor::White, .45f);
        TestTrue(TEXT("Every burst preset shows particles and reduced effects only thin them"), !Full.IsEmpty()
            && Thin.Sparks <= Full.Sparks && Thin.Smoke <= Full.Smoke && Thin.Sparks + Thin.Smoke < Full.Sparks + Full.Smoke
            && Thin.Scale == Full.Scale && Thin.SparkColour == Full.SparkColour);
    }
    TestTrue(TEXT("Embers rise, droplets fall, arcs float"), BurstOf(EBurst::Flame, FLinearColor::White, 1.f).Gravity.Z > 0.
        && BurstOf(EBurst::Splash, FLinearColor::White, 1.f).Gravity.Z < -980. && BurstOf(EBurst::Zap, FLinearColor::White, 1.f).Gravity.IsZero()
        && BurstOf(EBurst::Strike, FLinearColor::White, NaN).IsEmpty());
    EBurst Element;
    FLinearColor ElementTint;
    TestTrue(TEXT("Element hits add their particles, an ordinary hit none"), HitBurstOf(ELDObservedHitLook::Fire, Element, ElementTint)
        && Element == EBurst::Flame && HitBurstOf(ELDObservedHitLook::Electric, Element, ElementTint) && Element == EBurst::Zap
        && !HitBurstOf(ELDObservedHitLook::None, Element, ElementTint));
    const FZone Small = ZoneOf(ELDObservedProjectileLook::AreaFire, 150.f, 1.f);
    const FZone Wide = ZoneOf(ELDObservedProjectileLook::AreaFire, 300.f, 1.f);
    TestTrue(TEXT("A ground zone loops particles at a density that follows its area; a flight has a trail instead"),
        Small.bShow && Wide.Rate > 3.f * Small.Rate && ZoneOf(ELDObservedProjectileLook::AreaFire, 150.f, .45f).Rate < Small.Rate
        && !ZoneOf(ELDObservedProjectileLook::Chalk, 150.f, 1.f).bShow && TrailOf(ELDObservedProjectileLook::Chalk, 1.f).bShow
        && !TrailOf(ELDObservedProjectileLook::AreaFire, 1.f).bShow && !ZoneOf(ELDObservedProjectileLook::AreaFire, NaN, 1.f).bShow);
    TestTrue(TEXT("Launches, landings and flight ends have bursts"), AppearBurstOf(ELDObservedProjectileLook::Chalk, Element, ElementTint)
        && Element == EBurst::Muzzle && EndBurstOf(ELDObservedProjectileLook::BookShadow, Element, ElementTint) && Element == EBurst::Slam
        && !EndBurstOf(ELDObservedProjectileLook::AreaFire, Element, ElementTint));
    TestTrue(TEXT("A dash streaks; a run, a knock-back or a fall does not"), IsDashing(1500.f, false, false) && !IsDashing(675.f, false, false)
        && !IsDashing(1500.f, true, false) && !IsDashing(1500.f, false, true) && DashTrailOf(FLinearColor::White, 1.f).bShow
        && DashTrailOf(FLinearColor::White, .45f).WidthCm < DashTrailOf(FLinearColor::White, 1.f).WidthCm);
    TestTrue(TEXT("Taking a hit tints the edge faintly; reduced effects turn it off"), HurtFlashOf(false).Seconds > 0.f
        && HurtFlashOf(false).Colour.A < ScreenFlashOf(LDObservation::Critical, false).Colour.A && HurtFlashOf(true).Seconds == 0.f);
    {
        const FScreenFlash CritEdge = ScreenFlashOf(LDObservation::Critical, false), KillEdge = ScreenFlashOf(LDCombatCues::DealtLethal, false);
        TestTrue(TEXT("Screen flashes keep a gap: inside it only a stronger flash replaces the last one"),
            ScreenFlashReplaces(FScreenFlash(), 0.f, CritEdge) && !ScreenFlashReplaces(CritEdge, .1f, CritEdge) && !ScreenFlashReplaces(CritEdge, .1f, HurtFlashOf(false))
            && ScreenFlashReplaces(CritEdge, .1f, KillEdge) && !ScreenFlashReplaces(KillEdge, .3f, CritEdge)
            && ScreenFlashReplaces(CritEdge, ScreenFlashGapSeconds, CritEdge) && !ScreenFlashReplaces(FScreenFlash(), 0.f, HurtFlashOf(true)));
        TestEqual(TEXT("A critical hit's edge has no lens fringe"), CritEdge.Fringe, 0.f);
    }
    // Overdrive (#305): a public hit flag with its own shapes, the heaviest flash short of a kill, a charge and a release.
    const FRecipe Over=ForHit(ELDObservedHitLook::None,LDObservation::Overdriven,1.f);
    const FLayer* OverStar=Over.Layers.FindByPredicate([](const FLayer& L){ return L.Place==EPlace::Sprite; });
    TestTrue(TEXT("An overdrive hit shows a cross larger than a critical one, its own wide ring and a sigil"),
        OverStar && OverStar->Mask==EMask::Cross && OverStar->SizeCm>CritStar->SizeCm && Has(Over,EMask::Sigil,EPlace::Ground)
        && Over.Layers.FilterByPredicate([](const FLayer& L){ return L.Mask==EMask::Ring; }).Num()==1 && OverStar->Glow.B>OverStar->Glow.G);
    TestTrue(TEXT("The overdrive bit is public and is the owner receipt's overdrive bit"),
        (LDObservation::HitFlagsMask & LDObservation::Overdriven)!=0 && LDCombatCues::DealtOverdrive==LDObservation::Overdriven
        && (LDCombatCues::DealtLethal & LDObservation::HitFlagsMask)==0);
    const FScreenFlash OverFlash=ScreenFlashOf(LDCombatCues::DealtOverdrive|LDObservation::Critical,false);
    TestTrue(TEXT("A kill still outranks overdrive, which outranks a critical hit on the screen edge"),
        OverFlash.Seconds>0.f && OverFlash.Colour.B>OverFlash.Colour.G && OverFlash.Fringe>CritFlash.Fringe
        && ScreenFlashOf(LDCombatCues::DealtOverdrive|LDCombatCues::DealtLethal,false).Colour==KillFlash.Colour
        && ScreenFlashOf(LDCombatCues::DealtOverdrive,true).Seconds==0.f);
    const FZone ChargeStart=ChargeZoneOf(0.f,1.f), ChargeFull=ChargeZoneOf(1.f,1.f);
    TestTrue(TEXT("The charge column grows with the hold and thins with reduced effects"), ChargeStart.bShow && ChargeFull.Rate>ChargeStart.Rate
        && ChargeFull.Velocity.Z>ChargeStart.Velocity.Z && ChargeZoneOf(5.f,1.f).Rate==ChargeFull.Rate && ChargeZoneOf(1.f,.45f).Rate<ChargeFull.Rate
        && !ChargeZoneOf(NaN,1.f).bShow && !ForChargeReady(1.f).Layers.IsEmpty() && ForChargeReady(.45f).Layers.Num()<ForChargeReady(1.f).Layers.Num());
    // The overdrive release is public only on a Cast cue and only with the momentum authorization.
    FLDObservedUnitView Caster;
    Caster.Handle=FGuid::NewGuid(); Caster.ObservationEpoch=1; Caster.StateRevision=1;
    Caster.Kind=ELDObservedUnitKind::Contender; Caster.AuthorizedFieldMask=LDObservation::ContenderFields;
    Caster.Presentation=2; Caster.FeetCm=FVector(100,-200,2); Caster.Facing=1234;
    Caster.ContenderId=FGuid::NewGuid(); Caster.Level=3; Caster.Health.CurrentP2=45000; Caster.Health.MaxP2=60000;
    Caster.ActionCue=ELDObservedActionCue::Cast; Caster.bOverdriveCast=true;
    FString CasterError;
    const bool bCastOk=LDObservation::ValidateUnit(Caster,CasterError);
    FLDObservedUnitView NoCast=Caster; NoCast.ActionCue=ELDObservedActionCue::None;
    FLDObservedUnitView Hidden=Caster; Hidden.AuthorizedFieldMask&=~LDObservation::Momentum;
    FLDObservedUnitView StrayAge=Caster; StrayAge.ChargeAgeMs=120;
    FLDObservedUnitView Held=Caster; Held.bCharging=true; Held.ChargeAgeMs=120;
    TestTrue(TEXT("A charge age rides only a held charge"), !LDObservation::ValidateUnit(StrayAge,CasterError) && LDObservation::ValidateUnit(Held,CasterError));
    TestTrue(*FString::Printf(TEXT("An overdrive release rides a Cast cue under the momentum authorization (%s)"),*CasterError),
        bCastOk && !LDObservation::ValidateUnit(NoCast,CasterError) && !LDObservation::ValidateUnit(Hidden,CasterError));
    const FRecipe Release=ForOverdriveRelease(1.f);
    TestTrue(TEXT("The release is a shockwave: cross, rings, sigil and crack"), Has(Release,EMask::Cross,EPlace::Sprite)
        && Has(Release,EMask::Ring,EPlace::Ground) && Has(Release,EMask::Sigil,EPlace::Ground) && Has(Release,EMask::Crack,EPlace::Ground)
        && Release.Layers.Num()<=MaxLayers && ForOverdriveRelease(.45f).Layers.Num()==Release.Layers.Num()-1);
    const FBurst OverBurst=BurstOf(EBurst::Overdrive,FLinearColor::White,1.f);
    TestTrue(TEXT("The overdrive burst is the heaviest particle preset short of a kill and keeps its own colour"),
        OverBurst.Sparks>BurstOf(EBurst::Strike,FLinearColor::White,1.f).Sparks && OverBurst.SparkColour.B>OverBurst.SparkColour.G);
    // Element reactions and public ground states use the particle kit in their own colours.
    const FRecipe React=ForReaction(uint8(ELDCombatElement::Fire),1.f);
    TestTrue(TEXT("A reaction flashes a cross and rings in its element's colour; reduced effects drop the echo"),
        Has(React,EMask::Cross,EPlace::Sprite) && Has(React,EMask::Ring,EPlace::Ground) && React.Layers[0].Glow.R>React.Layers[0].Glow.B
        && ForReaction(uint8(ELDCombatElement::Water),1.f).Layers[0].Glow.B>ForReaction(uint8(ELDCombatElement::Water),1.f).Layers[0].Glow.R
        && ForReaction(uint8(ELDCombatElement::Fire),.45f).Layers.Num()==React.Layers.Num()-1 && ForReaction(1,NaN).Layers.IsEmpty());
    for (uint8 ElementId=1; ElementId<LDCombatElementCount; ++ElementId)
    {
        TestTrue(TEXT("Every element has its own burst and tint"), ElementBurst(ElementId)!=EBurst::Strike
            && !BurstOf(ElementBurst(ElementId),LDHitVfx::ElementTint(ElementId),1.f).IsEmpty());
    }
    for (uint8 GroundKind=1; GroundKind<LDGroundStateKindCount; ++GroundKind)
    {
        EBurst Appear; FLinearColor AppearTint;
        const FZone Patch=GroundZoneOf(GroundKind,150.f,1.f);
        if (GroundKind==uint8(ELDGroundStateKind::Field))
        {
            TestTrue(TEXT("The electric field has no particles or burst: its crackle is the slice's ground field"),
                !Patch.bShow && !GroundAppearBurstOf(GroundKind,Appear,AppearTint));
            continue;
        }
        TestTrue(TEXT("Every ground state loops particles by area, thins with reduced effects and appears with a burst"),
            Patch.bShow && GroundZoneOf(GroundKind,300.f,1.f).Rate>3.f*Patch.Rate && GroundZoneOf(GroundKind,150.f,.45f).Rate<Patch.Rate
            && GroundAppearBurstOf(GroundKind,Appear,AppearTint));
    }
    TestTrue(TEXT("No ground state, no particles"), !GroundZoneOf(0,150.f,1.f).bShow && !GroundZoneOf(1,NaN,1.f).bShow);
    // Afterimages.
    const FGhosts Ghosts=GhostsOf(FLinearColor::White,1.f), FewGhosts=GhostsOf(FLinearColor::White,.45f);
    TestTrue(TEXT("A dash leaves afterimages that fade out; reduced effects leave half as many"), Ghosts.bShow
        && FewGhosts.IntervalSeconds>=2.f*Ghosts.IntervalSeconds-.001f && GhostFade(Ghosts,0.f)>GhostFade(Ghosts,Ghosts.Seconds*.5f)
        && GhostFade(Ghosts,Ghosts.Seconds)==0.f && GhostFade(Ghosts,-.1f)==0.f && !GhostsOf(FLinearColor::White,0.f).bShow);
    // Every active ability of the approved content has its own look.
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    FString ContentError;
    if (TestTrue(TEXT("The approved content loads"), LDDemoEntry::LoadRuntimeContent(Content,ContentError) && Content.IsValid()))
    {
        int32 Actives=0;
        for (const FLDDemoAbilityDefinition& Ability : Content->Get().Abilities)
        {
            if (!Ability.Id.StartsWith(TEXT("active_"))) { continue; }
            ++Actives;
            const FSkillLook* Skill=SkillLookOf(FName(*Ability.Id));
            TestTrue(FString::Printf(TEXT("%s has a skill look with its native id"),*Ability.Id), Skill && Skill->Ability==Ability.NativeId);
        }
        TestEqual(TEXT("The skill table covers exactly the approved actives"), SkillLooks().Num(), Actives);
    }
    TestTrue(TEXT("Every mask has a manifest id and a texture package"), [](){
        for (int32 Mask=0; Mask<int32(EMask::Count); ++Mask)
        { if (FCString::Strlen(MaskId(EMask(Mask)))==0 || !TexturePackage(MaskId(EMask(Mask))).StartsWith(Folder)) { return false; } }
        return true; }());
    return true;
}

#endif
