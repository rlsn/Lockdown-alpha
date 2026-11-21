#include "Navigation/LDOrderRules.h"
#include "Navigation/LDNavigationComponent.h"
#include "Combat/LDCombatControlRules.h"
#include "Combat/LDCombatPreInput.h"
#include "Framework/LDPlayerController.h"
#include "UI/LDControlSettings.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

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
    View.ActionEntry = ELDCombatEntry::W;
    View.bHasBody = false;
    TestFalse(TEXT("No body holds nothing"), IsWindingUp(View, 1000));
    TestTrue(TEXT("The server holds a press that met the own ability windup within 450 ms, through its release moment"),
        ServerHolds(ELDCombatEntry::W, true, 1200, 1000) && ServerHolds(ELDCombatEntry::Dash, true, 1200, 1200)
        && ServerHolds(ELDCombatEntry::E, true, 1450, 1000));
    TestFalse(TEXT("The server holds nothing without an own ability windup, for a basic, or past the 450 ms hold"),
        ServerHolds(ELDCombatEntry::W, false, 0, 1000) || ServerHolds(ELDCombatEntry::Basic, true, 1200, 1000)
        || ServerHolds(ELDCombatEntry::E, true, 1451, 1000));
    TestTrue(TEXT("Q, W, E and the innate D dash are Direct entries"),
        IsDirectEntry(ELDCombatEntry::Q) && IsDirectEntry(ELDCombatEntry::W)
        && IsDirectEntry(ELDCombatEntry::E) && IsDirectEntry(ELDCombatEntry::Dash)
        && !IsDirectEntry(ELDCombatEntry::Basic) && !IsDirectEntry(ELDCombatEntry::None));
    TestTrue(TEXT("The D dash fires toward the pointer on the press; Q/W/E still arm"),
        FiresOnPress(ELDCombatEntry::Dash) && !FiresOnPress(ELDCombatEntry::Q) && !FiresOnPress(ELDCombatEntry::W)
        && !FiresOnPress(ELDCombatEntry::E)
        && !FiresOnPress(ELDCombatEntry::Basic));
    TestTrue(TEXT("A held press lives inside the 450 ms 2-step FIFO window"),
        HoldWindowMs == 450 && FMath::IsNearlyEqual(HoldSeconds, 0.45) && MaxBufferedSteps == 2);

    // 2-step FIFO covering Direct, GroundPoint and Unit casts: 3rd buffered press evicts the oldest and keeps newest 2 in order.
    TArray<FPendingCombatIntent> Queue;
    TOptional<FPendingCombatIntent> Evicted;
    FPendingCombatIntent Step1{ELDCombatEntry::W, ELDCombatPreInputMode::GroundPoint};
    FPendingCombatIntent Step2{ELDCombatEntry::E, ELDCombatPreInputMode::Unit};
    FPendingCombatIntent Step3{ELDCombatEntry::Dash, ELDCombatPreInputMode::Direct};
    EnqueueFIFO(Queue, MoveTemp(Step1), Evicted);
    TestTrue(TEXT("First buffered step enters without eviction"), !Evicted.IsSet() && Queue.Num() == 1);
    EnqueueFIFO(Queue, MoveTemp(Step2), Evicted);
    TestTrue(TEXT("GroundPoint -> Unit both fit in the 2-step FIFO behind an active Direct windup"),
        !Evicted.IsSet() && Queue.Num() == 2
        && Queue[0].CastMode == ELDCombatPreInputMode::GroundPoint && Queue[1].CastMode == ELDCombatPreInputMode::Unit);
    EnqueueFIFO(Queue, MoveTemp(Step3), Evicted);
    TestTrue(TEXT("A third buffered key evicts the oldest and retains the latest 2 steps in FIFO order"),
        Evicted.IsSet() && Evicted->Entry == ELDCombatEntry::W && Queue.Num() == 2
        && Queue[0].Entry == ELDCombatEntry::E && Queue[1].Entry == ELDCombatEntry::Dash);
    // Out-of-order RPC arrival with DirectSequence preserves true press order (N before N+1).
    Queue.Reset();
    FPendingCombatIntent Later{ELDCombatEntry::W, ELDCombatPreInputMode::GroundPoint};
    Later.Action.DirectSequence = 11;
    FPendingCombatIntent Earlier{ELDCombatEntry::Q, ELDCombatPreInputMode::Direct};
    Earlier.Action.DirectSequence = 10;
    EnqueueFIFO(Queue, MoveTemp(Later), Evicted);
    EnqueueFIFO(Queue, MoveTemp(Earlier), Evicted);
    TestTrue(TEXT("DirectSequence ordering keeps press 10 ahead of press 11 across Direct and GroundPoint paths"),
        Queue.Num() == 2 && Queue[0].Action.DirectSequence == 10 && Queue[1].Action.DirectSequence == 11);
    TestTrue(TEXT("Hard CC outlasting the remaining windup by > 450ms clears the buffer"),
        !ShouldClearForHardControl(1650, 1200) && ShouldClearForHardControl(1651, 1200));

    // Close-range innate dash clamp: stops at 135cm from an engaged enemy in a 220cm +-30deg forward cone unless card_pass_through is installed.
    const double Clamped = ClampInnateDashDistance(450.0, FVector::ZeroVector, FVector(1, 0, 0), FVector(200, 0, 0), false);
    TestTrue(TEXT("Dashing straight at an enemy 200cm ahead clamps 450cm dash to 65cm (stopping at 135cm)"),
        FMath::IsNearlyEqual(Clamped, 65.0, 1e-4));
    TestTrue(TEXT("Dashing at an enemy already at or inside 135cm clamps to 0cm in-place stop instead of pushing closer"),
        FMath::IsNearlyEqual(ClampInnateDashDistance(450.0, FVector::ZeroVector, FVector(1, 0, 0), FVector(135, 0, 0), false), 0.0, 1e-6)
        && FMath::IsNearlyEqual(ClampInnateDashDistance(450.0, FVector::ZeroVector, FVector(1, 0, 0), FVector(100, 0, 0), false), 0.0, 1e-6));
    FLDCombatVoluntaryDash ClampedZeroDash;
    ClampedZeroDash.StartFootCm = FVector::ZeroVector;
    ClampedZeroDash.EndMs = 1001;
    ClampedZeroDash.bClampedStop = true;
    TestTrue(TEXT("A zero-displacement clamped stop dash still completes naturally at EndMs"),
        LDCombatControl::DashCompleted(ClampedZeroDash, 1001, FVector::ZeroVector));
    TestTrue(TEXT("card_pass_through preserves the full 450cm dash through the enemy"),
        FMath::IsNearlyEqual(ClampInnateDashDistance(450.0, FVector::ZeroVector, FVector(1, 0, 0), FVector(200, 0, 0), true), 450.0, 1e-4));
    TestTrue(TEXT("Dashing away into open ground preserves the full 450cm dash"),
        FMath::IsNearlyEqual(ClampInnateDashDistance(450.0, FVector::ZeroVector, FVector(-1, 0, 0), FVector(200, 0, 0), false), 450.0, 1e-4));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDControlFeelSourceBufferTest, "Lockdown.Rules.DemoControlFeel.SourceBuffer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDControlFeelSourceBufferTest::RunTest(const FString& Parameters)
{
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    if (!TestNotNull(TEXT("Source buffer fixture world"),World)) { return false; }
    ON_SCOPE_EXIT { World->DestroyWorld(false); };
    auto* PC=World->SpawnActor<ALDPlayerController>();
    if (!TestNotNull(TEXT("Source buffer controller"),PC)) { return false; }
    PC->OwnerState.Match=1;
    PC->OwnerState.Life=1;
    PC->LatestCombatView.Generation=PC->CombatView.Generation=1;
    PC->LatestCombatView.Life=PC->CombatView.Life=1;
    PC->LatestCombatView.SourceRevision=PC->CombatView.SourceRevision=5;
    PC->LatestCombatView.Revision=1;
    PC->CombatView.Revision=2;
    PC->QueuedCombat.Entry=ELDCombatEntry::Q;
    PC->QueuedCombat.SourceRevision=5;
    PC->OnRep_CombatView();
    TestEqual(TEXT("An ordinary action update preserves the held intent"),PC->QueuedCombat.Entry,ELDCombatEntry::Q);
    PC->CombatView.Revision=3;
    PC->CombatView.SourceRevision=6;
    PC->OnRep_CombatView();
    TestEqual(TEXT("Changing skills drops the old source's buffered aim"),PC->QueuedCombat.Entry,ELDCombatEntry::None);
    PC->QueuedCombat.Entry=ELDCombatEntry::W;
    PC->CombatView.Generation=2;
    PC->CombatView.Revision=1;
    PC->OnRep_CombatView();
    TestEqual(TEXT("A new match cannot inherit a buffered action"),PC->QueuedCombat.Entry,ELDCombatEntry::None);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDControlFeelShiftQueueTest, "Lockdown.Rules.DemoControlFeel.ShiftQueue",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDControlFeelShiftQueueTest::RunTest(const FString& Parameters)
{
    using namespace LDOrderRules;
    TestEqual(TEXT("At most eight steps wait in the shift queue (2026-11-06 decision)"), MaxQueuedSteps, 8);
    TestTrue(TEXT("The head starts once nothing holds the body"), QueueHeadDue(false, false, false));
    TestFalse(TEXT("A pending request goes first"), QueueHeadDue(true, false, false));
    TestFalse(TEXT("The current order runs to its end first"), QueueHeadDue(false, true, false));
    TestFalse(TEXT("A queued action still in Combat holds the queue"), QueueHeadDue(false, false, true));

    for (const ELDOrderFailure Next : {ELDOrderFailure::None, ELDOrderFailure::TargetDied, ELDOrderFailure::TargetUnavailable,
        ELDOrderFailure::NoCompletePath, ELDOrderFailure::InvalidGround, ELDOrderFailure::PathBlocked,
        ELDOrderFailure::KnockedBack, ELDOrderFailure::Superseded})
    {
        TestTrue(FString::Printf(TEXT("Failure %u of one step moves on to the next step"), uint32(Next)),
            QueueAfter(Next) == EQueueAfter::Next);
    }
    for (const ELDOrderFailure Clear : {ELDOrderFailure::Stopped, ELDOrderFailure::Dead, ELDOrderFailure::MatchEnded,
        ELDOrderFailure::StaleMatch, ELDOrderFailure::StaleLife, ELDOrderFailure::NotOwner, ELDOrderFailure::Expired,
        ELDOrderFailure::Taunted})
    {
        TestTrue(FString::Printf(TEXT("Failure %u drops the whole queue"), uint32(Clear)), QueueAfter(Clear) == EQueueAfter::Clear);
    }

    TestFalse(TEXT("A queued action Combat holds is not settled"), QueuedActionSettled(ELDCombatPhase::Queued));
    TestFalse(TEXT("A winding-up queued action is not settled"), QueuedActionSettled(ELDCombatPhase::Accepted));
    TestTrue(TEXT("A released, finished or refused queued action settles"), QueuedActionSettled(ELDCombatPhase::Released)
        && QueuedActionSettled(ELDCombatPhase::Finished) && QueuedActionSettled(ELDCombatPhase::Rejected));

    FVector Direction;
    TestTrue(TEXT("A queued direction re-aims from the body's new feet"),
        QueuedAimDirection(FVector(100, 100, 0), FVector(100, 400, 90), Direction) && Direction.Equals(FVector(0, 1, 0), 1.e-6));
    TestFalse(TEXT("A point under the body keeps the sent direction"),
        QueuedAimDirection(FVector(100, 100, 0), FVector(100.5, 100, 300), Direction));
    const double NaN = std::numeric_limits<double>::quiet_NaN();
    TestFalse(TEXT("A non-finite point aims nothing"), QueuedAimDirection(FVector(NaN, 0, 0), FVector(1, 2, 3), Direction)
        || QueuedAimDirection(FVector::ZeroVector, FVector(0, NaN, 0), Direction));

    FLDSpatialOrderRequest Plain;
    Plain.Identity = {1, 1, 3, 1};
    FLDSpatialOrderRequest Queued = Plain;
    Queued.bQueue = true;
    TestTrue(TEXT("A queued order keeps a valid shape"), Queued.HasValidShape());
    TestFalse(TEXT("The same identity with and without Shift is a conflict, not a replay"), Plain.SameTerms(Queued));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDControlFeelAttackClickTest, "Lockdown.Rules.DemoControlFeel.AttackClick",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDControlFeelAttackClickTest::RunTest(const FString& Parameters)
{
    using namespace LDOrderRules;
    // 2026-11-07 decision: A then a left click on an attackable visible unit is the right-click attack on that unit.
    TestTrue(TEXT("A-click on a visible unit attacks that unit, never the nearest one"),
        AttackConfirmKind(true) == ELDSpatialOrderKind::AttackUnit);
    TestTrue(TEXT("A-click on the ground stays an attack-move"), AttackConfirmKind(false) == ELDSpatialOrderKind::AttackMove);
    FLDSpatialOrderRequest Click;
    Click.Identity = {1, 1, 4, 1};
    Click.Kind = AttackConfirmKind(true);
    TestTrue(TEXT("The unit attack from an A-click is an aggressive intent like a right click"), Click.IsAggressive());
    Click.Kind = AttackConfirmKind(false);
    TestTrue(TEXT("So is the ground attack-move"), Click.IsAggressive());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDControlFeelRefusedSkillKeepsOrderTest, "Lockdown.Rules.DemoControlFeel.RefusedSkillKeepsOrder",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDControlFeelRefusedSkillKeepsOrderTest::RunTest(const FString& Parameters)
{
    using namespace LDOrderRules;
    // 2026-11-07 decision: a skill or D request that cannot start is refused at once and leaves the current move or
    // attack order, the pending request and the shift queue as they were; only a ready one goes on to replace them.
    TestTrue(TEXT("A ready entry goes on"), SkillRequestReady(ELDCombatReject::None));
    TestTrue(TEXT("The own ability windup only delays the request"), SkillRequestReady(ELDCombatReject::Busy));
    for (const ELDCombatReject Refused : {ELDCombatReject::Cooldown, ELDCombatReject::MomentumShort,
        ELDCombatReject::ControlBlocked, ELDCombatReject::SourceUnavailable,
        ELDCombatReject::Dead, ELDCombatReject::MatchClosed, ELDCombatReject::AuthorityDataUnavailable})
    {
        TestFalse(FString::Printf(TEXT("Unavailability %u is refused without touching the current order"), uint32(Refused)),
            SkillRequestReady(Refused));
    }
    TestTrue(TEXT("A refused cast step moves on to the next queued step instead of dropping the queue"),
        QueueAfter(ELDOrderFailure::CastRefused) == EQueueAfter::Next);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDControlFeelAutoAttackAfterCastTest, "Lockdown.Rules.DemoControlFeel.AutoAttackAfterCast",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDControlFeelAutoAttackAfterCastTest::RunTest(const FString& Parameters)
{
    using namespace LDOrderRules;
    // #167 user decisions: a unit skill continues with an attack on its target; a ground/direction skill resumes the unit
    // the body was attacking before the cast while it is still visible, otherwise the body stands; the setting is on by
    // default and off restores the plain stand-after-cast behaviour.
    const FGuid SkillTarget = FGuid::NewGuid(), Prior = FGuid::NewGuid();
    auto Input = [&](ELDSpatialOrderKind Kind, bool bEnabled, bool bQueued, bool bPriorCurrent)
    {
        FCastFollowUpInput In;
        In.bEnabled = bEnabled;
        In.bQueued = bQueued;
        In.CastKind = Kind;
        In.CastTarget = Kind == ELDSpatialOrderKind::CastUnit ? SkillTarget : FGuid();
        In.CastEpoch = Kind == ELDSpatialOrderKind::CastUnit ? 7 : 0;
        In.bPriorAttackCurrent = bPriorCurrent;
        In.PriorTarget = Prior;
        In.PriorEpoch = 3;
        return In;
    };
    FGuid Target;
    uint32 Epoch = 0;
    TestTrue(TEXT("On: a unit skill continues with an attack on its own target"),
        CastFollowUpTarget(Input(ELDSpatialOrderKind::CastUnit, true, false, true), Target, Epoch) && Target == SkillTarget && Epoch == 7);
    TestTrue(TEXT("On: a ground/direction skill resumes the unit attacked before the cast"),
        CastFollowUpTarget(Input(ELDSpatialOrderKind::CastPoint, true, false, true), Target, Epoch) && Target == Prior && Epoch == 3);
    TestFalse(TEXT("On: a ground/direction skill without a current attack stands"),
        CastFollowUpTarget(Input(ELDSpatialOrderKind::CastPoint, true, false, false), Target, Epoch));
    TestTrue(TEXT("No follow-up leaves no target"), !Target.IsValid() && Epoch == 0);
    for (const ELDSpatialOrderKind Kind : {ELDSpatialOrderKind::CastUnit, ELDSpatialOrderKind::CastPoint})
    {
        TestFalse(TEXT("Off: no cast continues with an attack"), CastFollowUpTarget(Input(Kind, false, false, true), Target, Epoch));
        TestFalse(TEXT("A Shift-queued cast never continues with an attack: the queue defines what follows"),
            CastFollowUpTarget(Input(Kind, true, true, true), Target, Epoch));
    }
    FCastFollowUpInput Missing = Input(ELDSpatialOrderKind::CastUnit, true, false, true);
    Missing.CastTarget.Invalidate();
    TestFalse(TEXT("A unit skill without a resolved target has nothing to attack"), CastFollowUpTarget(Missing, Target, Epoch));
    TestFalse(TEXT("Only cast orders carry a follow-up"),
        CastFollowUpTarget(Input(ELDSpatialOrderKind::AttackMove, true, false, true), Target, Epoch));

    for (const ELDOrderStatus Running : {ELDOrderStatus::Queued, ELDOrderStatus::Activated, ELDOrderStatus::Progress})
    {
        TestTrue(TEXT("The follow-up waits while the cast order is queued or running"),
            CastFollowUpStep(Running, ELDOrderFailure::None, true, true) == ECastFollowUp::Wait);
    }
    TestTrue(TEXT("A completed cast with a visible target attacks"),
        CastFollowUpStep(ELDOrderStatus::Completed, ELDOrderFailure::None, true, true) == ECastFollowUp::Attack);
    TestTrue(TEXT("A target no longer visible is not attacked: the body stands"),
        CastFollowUpStep(ELDOrderStatus::Completed, ELDOrderFailure::None, true, false) == ECastFollowUp::Drop);
    TestTrue(TEXT("A newer own order or Stop replaces the follow-up"),
        CastFollowUpStep(ELDOrderStatus::Completed, ELDOrderFailure::None, false, true) == ECastFollowUp::Drop);
    TestTrue(TEXT("A refused cast never continues with an attack"),
        CastFollowUpStep(ELDOrderStatus::Rejected, ELDOrderFailure::CastRefused, true, true) == ECastFollowUp::Drop);
    for (const ELDOrderFailure Failure : {ELDOrderFailure::CastRefused, ELDOrderFailure::Superseded, ELDOrderFailure::Stopped,
        ELDOrderFailure::TargetDied})
    {
        TestTrue(FString::Printf(TEXT("A cast order cancelled by failure %u drops its follow-up"), uint32(Failure)),
            CastFollowUpStep(ELDOrderStatus::Cancelled, Failure, true, true) == ECastFollowUp::Drop);
        TestTrue(TEXT("A completion carrying a failure is no successful cast"),
            CastFollowUpStep(ELDOrderStatus::Completed, Failure, true, true) == ECastFollowUp::Drop);
    }

    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    if (!TestNotNull(TEXT("Follow-up receipt fixture world"),World)) { return false; }
    ON_SCOPE_EXIT { World->DestroyWorld(false); };
    auto* PC=World->SpawnActor<ALDPlayerController>();
    if (!TestNotNull(TEXT("Follow-up receipt controller"),PC)) { return false; }
    PC->Player=NewObject<ULocalPlayer>(GEngine);
    PC->Controls=NewObject<ULDControlSettings>(PC);
    PC->OwnerState.Match=1;
    PC->OwnerState.Life=1;
    if (!TestTrue(TEXT("Receipts are applied on the owning controller"),PC->IsLocalController() && PC->GetLocalPlayer())) { return false; }
    ULDNavigationComponent& Nav=*PC->Navigation;
    uint64 ReceiptSerial=0;
    const auto Receive=[&](const FLDSpatialOrderRequest& Request, ELDOrderStatus Status, ELDOrderFailure Failure)
    {
        FLDOrderReceipt Receipt;
        Receipt.Identity=Request.Identity;
        Receipt.Kind=Request.Kind;
        Receipt.Status=Status;
        Receipt.Failure=Failure;
        Receipt.ReceiptSerial=++ReceiptSerial;
        Nav.ClientOrderReceipt_Implementation(Receipt);
    };
    const auto Remember=[&](const FLDSpatialOrderRequest& Request, const FGuid& FollowUp=FGuid(), uint32 FollowUpEpoch=0,
        bool bAutomatic=false)
    {
        Nav.Local=Request.Identity;
        Nav.RememberSentOrder(Request,FollowUp,FollowUpEpoch,bAutomatic);
    };
    FLDSpatialOrderRequest AttackA;
    AttackA.Identity={1,1,1,1};
    AttackA.Kind=ELDSpatialOrderKind::AttackUnit;
    AttackA.Observed=SkillTarget;
    AttackA.ObservationEpoch=7;
    Remember(AttackA);
    FLDCombatActionRequest PointCast;
    const auto PreparePoint=[&]()
    { Nav.PrepareCastFollowUp(ELDSpatialOrderKind::CastPoint,PointCast,false,Target,Epoch); };
    PreparePoint();
    TestFalse(TEXT("An attack without an activation receipt is not yet the current attack"),Target.IsValid());
    Receive(AttackA,ELDOrderStatus::Activated,ELDOrderFailure::None);

    FLDSpatialOrderRequest AttackB=AttackA;
    AttackB.Identity.Order=2;
    AttackB.Observed=Prior;
    AttackB.ObservationEpoch=3;
    AttackB.bQueue=true;
    Remember(AttackB);
    Receive(AttackB,ELDOrderStatus::Queued,ELDOrderFailure::None);
    PreparePoint();
    TestTrue(TEXT("A queued attack on B does not replace the accepted attack on A"),
        Target==SkillTarget && Epoch==7);

    FLDSpatialOrderRequest RefusedCast;
    RefusedCast.Identity={1,1,3,1};
    RefusedCast.Kind=ELDSpatialOrderKind::CastUnit;
    Remember(RefusedCast,Prior,3);
    Receive(RefusedCast,ELDOrderStatus::Rejected,ELDOrderFailure::CastRefused);
    PreparePoint();
    TestTrue(TEXT("A synchronous or remote cast refusal clears only that cast and retains the accepted attack on A"),
        !Nav.CastFollowUp.Identity.Order && Nav.ClientActive==AttackA.Identity && Target==SkillTarget && Epoch==7);
    Receive(AttackB,ELDOrderStatus::Rejected,ELDOrderFailure::QueueFull);
    PreparePoint();
    TestTrue(TEXT("A rejected queued attack cannot erase the accepted target"),Target==SkillTarget && Epoch==7);

    AttackB.Identity.Order=4;
    Remember(AttackB);
    Receive(AttackB,ELDOrderStatus::Queued,ELDOrderFailure::None);
    Receive(AttackA,ELDOrderStatus::Completed,ELDOrderFailure::TargetDied);
    Receive(AttackB,ELDOrderStatus::Activated,ELDOrderFailure::None);
    PreparePoint();
    TestTrue(TEXT("Once the queued attack activates, B becomes the accepted target"),Target==Prior && Epoch==3);
    Nav.Local={1,1,5,2};
    Nav.bLocalStop=true;
    PreparePoint();
    TestFalse(TEXT("A newer Stop epoch cannot revive the previous accepted attack"),Target.IsValid());
    Nav.ResetLife();
    TestTrue(TEXT("A new life discards every remembered target and follow-up"),
        Nav.SentAttackTargets.IsEmpty() && !Nav.CastFollowUp.Identity.Order && !Nav.AutoAttackIdentity.Order);

    FLDSpatialOrderRequest AutoAttack=AttackA;
    AutoAttack.Identity={1,1,6,2};
    const uint32 NoticeBefore=PC->FeedbackSerial();
    Remember(AutoAttack,FGuid(),0,true);
    Receive(AutoAttack,ELDOrderStatus::Rejected,ELDOrderFailure::TargetUnavailable);
    TestEqual(TEXT("An immediate automatic-attack refusal does not post an extra notice"),PC->FeedbackSerial(),NoticeBefore);
    AutoAttack.Identity.Order=7;
    Remember(AutoAttack);
    Receive(AutoAttack,ELDOrderStatus::Rejected,ELDOrderFailure::TargetUnavailable);
    TestEqual(TEXT("An ordinary attack refusal still posts its notice"),PC->FeedbackSerial(),NoticeBefore+1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDControlFeelElevationAimAndClipTest, "Lockdown.Rules.DemoControlFeel.ElevationAimAndClip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDControlFeelElevationAimAndClipTest::RunTest(const FString& Parameters)
{
    // #335 §1: Camera ray aimed toward (1000, 300, 0) from top-down camera (0, -900, 1200) passes right through a 600cm
    // wall top along the way; IntersectElevationPlane(Z=0) recovers the exact planar direction with 0.0 deg deflection.
    const FVector CasterFeet(200., 100., 0.);
    const FVector DesiredGround(1000., 300., 0.);
    const FVector CamOrigin(200., -800., 1200.);
    const FVector RayDir = (DesiredGround - CamOrigin).GetSafeNormal();
    // Simulate a 600cm tall wall intercepting the ray halfway at Z=600 (which used to deflect GroundUnderPointer):
    const double WallT = (600. - CamOrigin.Z) / RayDir.Z;
    const FVector WallTopHit = CamOrigin + RayDir * WallT;
    const FVector DeflectedDir = FVector(WallTopHit.X - CasterFeet.X, WallTopHit.Y - CasterFeet.Y, 0.).GetSafeNormal();
    const FVector ExpectedDir = FVector(DesiredGround.X - CasterFeet.X, DesiredGround.Y - CasterFeet.Y, 0.).GetSafeNormal();
    TestTrue(TEXT("Wall-top hit would have severely deflected direction"), FVector::DotProduct(DeflectedDir, ExpectedDir) < .85);

    FVector PlaneHit = FVector::ZeroVector;
    TestTrue(TEXT("IntersectElevationPlane resolves exact caster foot plane"),
        LDOrderRules::IntersectElevationPlane(CamOrigin, RayDir, CasterFeet.Z, FVector(1., 0., 0.), CasterFeet, PlaneHit));
    TestNearlyEqual(TEXT("Plane Z matches caster foot elevation"), PlaneHit.Z, CasterFeet.Z, 1e-4);
    const FVector ResolvedDir = LDOrderRules::ResolveElevationAimDirection(CasterFeet, PlaneHit, FVector(0., 1., 0.));
    const double AngleDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(ResolvedDir, ExpectedDir), -1., 1.)));
    TestNearlyEqual(TEXT("Zero-degree angular error despite 600cm wall along camera ray (#335)"), AngleDeg, 0., 1e-4);

    // Deadzone < 8cm falls back to CasterForward instead of dropping the cast.
    const FVector NearFoot = CasterFeet + FVector(3., -4., 0.);
    const FVector FallbackFwd = FVector(0., 1., 0.);
    TestTrue(TEXT("Aim within 8cm deadzone falls back to CasterForward"),
        LDOrderRules::ResolveElevationAimDirection(CasterFeet, NearFoot, FallbackFwd).Equals(FallbackFwd, 1e-4));

    // #335 §2: Off-screen corridor segment clips cleanly to viewport instead of disappearing.
    FVector2D SegA(960., 540.), SegB(960., 2400.);
    TestTrue(TEXT("Half off-screen corridor segment remains visible"),
        LDOrderRules::ClipViewportSegment(SegA, SegB, FVector2D(0., 0.), FVector2D(1920., 1080.)));
    TestNearlyEqual(TEXT("Start inside viewport unchanged"), SegA.Y, 540., 1e-3);
    TestNearlyEqual(TEXT("Far endpoint clipped cleanly to bottom edge"), SegB.Y, 1080., 1e-3);

    FVector2D OutsideA(-200., -100.), OutsideB(-50., -20.);
    TestFalse(TEXT("Wholly off-screen segment is culled"),
        LDOrderRules::ClipViewportSegment(OutsideA, OutsideB, FVector2D(0., 0.), FVector2D(1920., 1080.)));

    // #337 §3.2: 50ms / <15cm ClickMove coalescing.
    TestTrue(TEXT("Rapid click within 50ms and <15cm coalesces"),
        LDOrderRules::ShouldCoalesceClickMove(10.03, 10.00, FVector(100., 200., 0.), FVector(108., 205., 0.)));
    TestFalse(TEXT("Click >=15cm away sends immediately even within 50ms"),
        LDOrderRules::ShouldCoalesceClickMove(10.02, 10.00, FVector(100., 200., 0.), FVector(120., 200., 0.)));
    TestFalse(TEXT("Click after 50ms window sends"),
        LDOrderRules::ShouldCoalesceClickMove(10.06, 10.00, FVector(100., 200., 0.), FVector(102., 200., 0.)));
    return true;
}

#endif