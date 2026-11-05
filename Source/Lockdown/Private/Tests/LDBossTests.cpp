#include "World/LDBoss.h"
#include "Framework/LDCaptureTypes.h"
#include "Misc/AutomationTest.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
namespace LDBossTests
{
// Test-only rules with the approved shape (aggro beyond reach, four waypoints); the values are not the approved ones,
// so the rules never lean on a particular number.
FLDBossState MakeState()
{
    FLDBossState State;
    FLDBossRules& R = State.Rules;
    R.StartedAtMs = 1000;
    R.AppearAtMs = 20000;
    R.AggroRadiusCm = 300.;
    R.AttackRangeCm = 200.;
    R.AttackerMemoryMs = 4000;
    R.MonsterId = TEXT("monster_dean");
    R.CreatureId = 3;
    R.SurfaceId = TEXT("demo_ground");
    R.Waypoints = {FVector(2400, 2400, 0), FVector(-2400, 2400, 0), FVector(-2400, -2400, 0), FVector(2400, -2400, 0)};
    State.Phase = ELDBossPhase::Pending;
    State.EntityId = FGuid(0xB0, 0, 0, 1);
    return State;
}

FLDCombatBodyRef BodyOf(uint32 Id, ELDVictimKind Kind = ELDVictimKind::Contender)
{
    FLDCombatBodyRef Ref;
    Ref.Victim.Kind = Kind;
    Ref.Victim.EntityId = FGuid(Id, 0, 0, 0);
    Ref.Victim.Life = 1;
    Ref.BodyIdentity = FGuid(Id, 1, 0, 0);
    return Ref;
}

LDBoss::FCandidate Seen(uint32 Id, const FVector& Foot, bool bVisible = true)
{
    LDBoss::FCandidate Out;
    Out.Body = BodyOf(Id);
    Out.FootCm = Foot;
    Out.bVisible = bVisible;
    return Out;
}

FLDCombatBodyRef BossBody(const FLDBossState& State)
{
    FLDCombatBodyRef Ref;
    Ref.Victim.Kind = ELDVictimKind::CampMember;
    Ref.Victim.EntityId = State.EntityId;
    Ref.Victim.Life = 1;
    Ref.BodyIdentity = FGuid(0xB0, 1, 0, 1);
    return Ref;
}

FLDPublicMatchView ViewAt(const FLDBossState& State, int64 NowMs)
{
    FLDPublicMatchView View;
    View.Generation = 1;
    View.PublicRevision = 1;
    View.Phase = ELDMatchPhase::Development;
    View.ServerNowMs = NowMs;
    View.StartedAtMs = State.Rules.StartedAtMs;
    View.OpenAtMs = 1000000;
    View.Boss = State.View();
    return View;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBossRulesTest, "Lockdown.Rules.DemoBoss.Rules",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDBossRulesTest::RunTest(const FString& Parameters)
{
    using namespace LDBossTests;
    FLDBossState State = MakeState();
    if (!TestTrue(TEXT("The test rules are valid"), State.IsActive())) { return false; }
    const int64 Appear = 21000;
    TestEqual(TEXT("The Boss appears one appear_at after the match start"), State.NextDueMs(), Appear);
    TestFalse(TEXT("It is not due before its time"), State.IsSpawnDue(Appear - 1));
    const FLDCombatBodyRef Self = BossBody(State);
    TestFalse(TEXT("An early spawn is refused"), State.Spawned(Self, Appear - 1));
    TestFalse(TEXT("A spawn of another entity is refused"), State.Spawned(BodyOf(9, ELDVictimKind::CampMember), Appear));
    TestTrue(TEXT("At its time the Boss spawns"), State.Spawned(Self, Appear) && State.Phase == ELDBossPhase::Alive);
    TestEqual(TEXT("Once alive nothing more is due"), State.NextDueMs(), MAX_int64);
    TestFalse(TEXT("It never spawns twice"), State.Spawned(Self, Appear + 1));

    // Patrol: the four waypoints in order, looping.
    const FVector Home = State.Rules.Waypoints[0];
    FLDBossDecision Decision = State.Decide(Appear, Home, {}, true, true);
    TestTrue(TEXT("Alone it patrols toward the first waypoint"), Decision.Intent == ELDBossIntent::Patrol && Decision.GoalFootCm == Home);
    for (int32 Step = 0; Step < 4; ++Step) { State.ReachedWaypoint(); }
    TestTrue(TEXT("After the fourth waypoint it loops back to the first"), State.PatrolGoal() == Home);
    State.ReachedWaypoint();
    TestTrue(TEXT("Then the second"), State.PatrolGoal() == State.Rules.Waypoints[1]);
    TestTrue(TEXT("Controlled, it does not walk"), State.Decide(Appear, Home, {}, false, true).Intent == ELDBossIntent::None);

    // Target: the nearest visible contender within aggro, never one outside it or unseen.
    const FVector Near = Home + FVector(250, 0, 0), Close = Home + FVector(150, 0, 0), Far = Home + FVector(600, 0, 0);
    Decision = State.Decide(Appear, Home, {Seen(1, Far)}, true, true);
    TestTrue(TEXT("A contender beyond aggro is ignored"), Decision.Intent == ELDBossIntent::Patrol);
    Decision = State.Decide(Appear, Home, {Seen(1, Close, false)}, true, true);
    TestTrue(TEXT("An unseen contender is ignored"), Decision.Intent == ELDBossIntent::Patrol);
    Decision = State.Decide(Appear, Home, {Seen(1, Near)}, true, true);
    TestTrue(TEXT("Inside aggro but out of reach it chases"), Decision.Intent == ELDBossIntent::Chase && Decision.Target == BodyOf(1)
        && Decision.GoalFootCm == Near);
    Decision = State.Decide(Appear, Home, {Seen(1, Near), Seen(2, Close)}, true, true);
    TestTrue(TEXT("The nearest contender in reach is attacked"), Decision.Intent == ELDBossIntent::Attack && Decision.Target == BodyOf(2));
    Decision = State.Decide(Appear, Home, {Seen(2, Close)}, true, false);
    TestTrue(TEXT("In reach while cooling down it holds"), Decision.Intent == ELDBossIntent::None && Decision.Target == BodyOf(2));
    Decision = State.Decide(Appear, Home, {Seen(5, Close), Seen(4, Home + FVector(0, 150, 0))}, true, true);
    TestTrue(TEXT("Equal distance is decided by contender id, not order"), Decision.Target == BodyOf(4));

    // Attackers come first, from anywhere they can be seen, until the memory runs out.
    State.NoteAttack(BodyOf(7), Appear + 1000);
    State.NoteAttack(BodyOf(8, ELDVictimKind::CampMember), Appear + 1000);
    Decision = State.Decide(Appear + 2000, Home, {Seen(2, Close), Seen(7, Far)}, true, true);
    TestTrue(TEXT("A recent attacker beyond aggro is chased before a nearer contender"), Decision.Intent == ELDBossIntent::Chase
        && Decision.Target == BodyOf(7));
    TestEqual(TEXT("Only a contender's contact is remembered"), State.Attackers.Num(), 1);
    State.NoteAttack(BodyOf(7), Appear + 3000);
    Decision = State.Decide(Appear + 6500, Home, {Seen(2, Close), Seen(7, Far)}, true, true);
    TestTrue(TEXT("A later hit refreshes the memory"), Decision.Target == BodyOf(7));
    Decision = State.Decide(Appear + 7001, Home, {Seen(2, Close), Seen(7, Far)}, true, true);
    TestTrue(TEXT("Once the memory runs out the nearest contender in aggro is the target again"), Decision.Target == BodyOf(2)
        && State.Attackers.IsEmpty());

    // The public view follows the phase; the death happens once.
    FString Error;
    FLDBossState Pending = MakeState();
    FLDPublicMatchView View = ViewAt(Pending, Appear - 5000);
    TestTrue(TEXT("A pending Boss is a valid public view: ") + Error, LDCapture::ValidatePublicMatchView(View, Error));
    View = ViewAt(State, Appear + 100);
    TestTrue(TEXT("A living Boss is a valid public view: ") + Error, LDCapture::ValidatePublicMatchView(View, Error));
    View.Boss.KilledBy = BodyOf(1).Victim.EntityId;
    TestFalse(TEXT("A living Boss names no killer"), LDCapture::ValidatePublicMatchView(View, Error));
    TestFalse(TEXT("Another body's death does not count"), State.Died(BodyOf(9, ELDVictimKind::CampMember), FGuid(1, 0, 0, 0), Appear + 9000));
    TestTrue(TEXT("The Boss dies once"), State.Died(Self, FGuid(1, 0, 0, 0), Appear + 9000) && State.Phase == ELDBossPhase::Down
        && State.KilledBy == FGuid(1, 0, 0, 0) && State.DownAtMs == Appear + 9000);
    TestFalse(TEXT("A second death is refused"), State.Died(Self, FGuid(2, 0, 0, 0), Appear + 9100));
    TestFalse(TEXT("Once down it never spawns again"), State.IsSpawnDue(Appear + 100000) || State.Spawned(Self, Appear + 100000));
    TestTrue(TEXT("A down Boss decides nothing"), State.Decide(Appear + 9500, Home, {Seen(2, Close)}, true, true).Intent == ELDBossIntent::None);
    View = ViewAt(State, Appear + 9000);
    TestTrue(TEXT("A down Boss is a valid public view: ") + Error, LDCapture::ValidatePublicMatchView(View, Error));
    View = ViewAt(State, Appear + 8999);
    TestFalse(TEXT("A death cannot be from the future"), LDCapture::ValidatePublicMatchView(View, Error));
    View = ViewAt(State, Appear + 9000);
    View.Phase = ELDMatchPhase::Lobby;
    View.ServerNowMs = View.StartedAtMs = View.OpenAtMs = 0;
    TestFalse(TEXT("A room outside a match publishes no Boss"), LDCapture::ValidatePublicMatchView(View, Error));

    FLDBossState Invalid = MakeState();
    Invalid.Rules.AggroRadiusCm = Invalid.Rules.AttackRangeCm;
    TestFalse(TEXT("An aggro radius inside its reach is not valid"), Invalid.IsActive());
    Invalid = MakeState();
    Invalid.Rules.Waypoints.Pop();
    TestFalse(TEXT("Three waypoints are not valid"), Invalid.IsActive() || Invalid.IsSpawnDue(Appear));
    Invalid = MakeState();
    Invalid.Rules.Waypoints[2].X = std::numeric_limits<double>::quiet_NaN();
    TestFalse(TEXT("A waypoint must be a number"), Invalid.IsActive());
    return true;
}
#endif
