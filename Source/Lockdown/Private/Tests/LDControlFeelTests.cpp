#include "Navigation/LDOrderRules.h"
#include "Combat/LDCombatPreInput.h"
#include "Framework/LDPlayerController.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDControlFeelShopFrontTest, "Lockdown.Rules.DemoControlFeel.ShopFront",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDControlFeelShopFrontTest::RunTest(const FString& Parameters)
{
    // A 160 x 100 cm stall around (778, 778).
    const FBox2D Stall(FVector2D(698, 728), FVector2D(858, 828));
    FVector Front;
    TestTrue(TEXT("From straight east the stop is the east face plus the body radius, level with the body"),
        LDOrderRules::ShopFrontPoint(Stall, FVector(1128, 790, 15), 40, Front) && Front.Equals(FVector(898, 790, 15), .01));
    TestTrue(TEXT("From a diagonal the stop is beside the nearest corner, not on a wider circle"),
        LDOrderRules::ShopFrontPoint(Stall, FVector(1100, 1100, 0), 40, Front) && Front.Equals(FVector(898, 868, 0), .01));
    TestFalse(TEXT("A body already beside the stall stays where it is"),
        LDOrderRules::ShopFrontPoint(Stall, FVector(778, 700, 0), 40, Front));
    TestFalse(TEXT("No footprint, no stop"), LDOrderRules::ShopFrontPoint(FBox2D(ForceInit), FVector(1128, 790, 0), 40, Front));
    const TArray<FVector> Candidates = LDOrderRules::ShopStopCandidates(FVector(898, 790, 0), FVector(1128, 790, 0), 50);
    TestTrue(TEXT("Stop candidates go from the stall back to the radius endpoint"),
        Candidates.Num() == 6 && Candidates[0].Equals(FVector(898, 790, 0)) && Candidates[1].Equals(FVector(948, 790, 0))
        && Candidates.Last().Equals(FVector(1128, 790, 0)));
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
    TestTrue(TEXT("Q, W and the innate D dash are Direct entries"),
        IsDirectEntry(ELDCombatEntry::Q) && IsDirectEntry(ELDCombatEntry::W) && IsDirectEntry(ELDCombatEntry::Dash)
        && !IsDirectEntry(ELDCombatEntry::Basic) && !IsDirectEntry(ELDCombatEntry::None));
    TestTrue(TEXT("The D dash fires toward the pointer on the press; Q/W still arm"),
        FiresOnPress(ELDCombatEntry::Dash) && !FiresOnPress(ELDCombatEntry::Q) && !FiresOnPress(ELDCombatEntry::W)
        && !FiresOnPress(ELDCombatEntry::Basic));
    TestTrue(TEXT("A held press lives at most a second"), HoldSeconds == 1.0);
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
        ELDOrderFailure::KnockedBack, ELDOrderFailure::ShopUnavailable, ELDOrderFailure::Superseded})
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
    for (const ELDCombatReject Refused : {ELDCombatReject::Cooldown, ELDCombatReject::InsufficientMana,
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

#endif