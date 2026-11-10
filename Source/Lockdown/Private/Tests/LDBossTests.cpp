#include "World/LDBoss.h"
#include "Character/LDCharacter.h"
#include "Combat/LDAttributeProjection.h"
#include "Combat/LDCombatDirector.h"
#include "Combat/LDTacticalRules.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDProgressionRules.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/LDCaptureTypes.h"
#include "Framework/LDGameMode.h"
#include "Misc/ScopeExit.h"
#include "Misc/AutomationTest.h"
#include "Tests/LDTestApprovedFixture.h"
#include "UObject/UnrealType.h"
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
    R.LeaveAtMs = 60000;
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
    // #198: world schedules are field progress T; a field running since T=0 at authority 0 makes T equal NowMs.
    View.Timeline.Generation = View.Generation;
    View.Timeline.Stage = ELDMatchStage::FinalField;
    View.Timeline.FieldEndT = -1;
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
    TestEqual(TEXT("Once alive only its departure is due"), State.NextDueMs(), int64(61000));
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
// #198 outer §5 Boss lifecycle (approved https://github.com/rlsn/Lockdown-alpha/issues/198#issuecomment-6752601175, comment
// 6752628806: "批准整套方案，按票内参数实施"): the approved Boss appears once at field T=240 s, leaves unkilled at T=540 s
// (the third rest, so never in the final field) and never respawns. Its last hit keeps the monster_dean DeathReward
// (300 XP) and adds one build reward draw; the old 90 s AD/AP +25%, move speed +10% approval buff is gone.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBossFieldLifecycleTest, "Lockdown.Rules.DemoBossFieldLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDBossFieldLifecycleTest::RunTest(const FString& Parameters)
{
    using namespace LDBossTests;
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDTestApprovedFixture::Read(*this, Catalog)) { return false; }
    const FLDDemoContentDefinitions& D = Catalog->Get();
    const FLDDemoBossDefinition& Rules = D.Boss;
    const FLDDemoMonsterDefinition* Row = D.Monsters.FindByPredicate(
        [&Rules](const FLDDemoMonsterDefinition& M) { return M.Id == Rules.Id; });
    bool bGood = TestTrue(TEXT("The Boss is the monster_dean row of the Boss tier"),
        Rules.Id == TEXT("monster_dean") && Row && Row->Tier == ELDDemoCampTier::Boss);
    bGood &= TestTrue(TEXT("It appears at field T=240 s and leaves at T=540 s, the third rest"),
        Rules.AppearAtMs == 240000 && Rules.LeaveAtMs == 540000
        && Rules.LeaveAtMs == D.Parameters.FieldSegmentMs * FLDTimelineParameters::TimedSegments);
    bGood &= TestTrue(TEXT("Its combat values stay as approved (4500 HP, 210 attack)"),
        Row && Row->HPP2 == 450000 && Row->AttackP2 == 21000);
    LDProgression::FRewardAmounts Amounts;
    bGood &= TestTrue(TEXT("The last hit's DeathReward is the approved 300 XP"),
        LDProgression::MonsterKillReward(*Catalog, FName(*Rules.Id), 0, Amounts) == ELDGrowthReason::None
        && Amounts.XP == 300);
    bGood &= TestTrue(TEXT("The Boss is not a camp and never takes a camp growth stage"),
        LDProgression::MonsterKillReward(*Catalog, FName(*Rules.Id), 1, Amounts) == ELDGrowthReason::DataInvalid
        && Amounts.XP == 0);
    bGood &= TestTrue(TEXT("The approval buff no longer exists in the Boss rules"),
        !FindFProperty<FProperty>(FLDDemoBossDefinition::StaticStruct(), TEXT("ApprovalStats"))
        && !FindFProperty<FProperty>(FLDDemoBossDefinition::StaticStruct(), TEXT("ApprovalDurationMs")));

    // The approved rules drive the lifecycle in field progress T (the field starts at T=0).
    FLDBossState State = MakeState();
    State.Rules.StartedAtMs = 0;
    State.Rules.AppearAtMs = Rules.AppearAtMs;
    State.Rules.LeaveAtMs = Rules.LeaveAtMs;
    if (!TestTrue(TEXT("The lifecycle rules are valid"), State.IsActive())) { return false; }
    const FLDCombatBodyRef Self = BossBody(State);
    bGood &= TestTrue(TEXT("Pending, the next instant is its appearance at T=240 s"),
        State.NextDueMs() == 240000 && !State.IsSpawnDue(239999) && State.IsSpawnDue(240000) && !State.IsLeaveDue(539999));
    bGood &= TestTrue(TEXT("It appears at T=240 s"), State.Spawned(Self, 240000) && State.Phase == ELDBossPhase::Alive);
    bGood &= TestTrue(TEXT("Alive, the next instant is its departure at T=540 s"),
        State.NextDueMs() == 540000 && !State.IsLeaveDue(539999) && State.IsLeaveDue(540000));
    FString Error;
    FLDPublicMatchView View = ViewAt(State, 300000);
    bGood &= TestTrue(TEXT("A living Boss publishes its leave instant: ") + Error,
        LDCapture::ValidatePublicMatchView(View, Error) && View.Boss.LeaveAtMs == 540000);
    bGood &= TestTrue(TEXT("It does not leave early"), !State.Leave(539999) && State.Phase == ELDBossPhase::Alive);
    bGood &= TestTrue(TEXT("At T=540 s it leaves without a killer"), State.Leave(540000) && State.Phase == ELDBossPhase::Left
        && !State.KilledBy.IsValid() && State.DownAtMs == 0 && !State.Body.IsValid());
    bGood &= TestTrue(TEXT("A Boss that left neither dies, respawns nor leaves again"),
        !State.Died(Self, FGuid(1, 0, 0, 0), 540000) && !State.IsSpawnDue(600000) && !State.Spawned(Self, 600000)
        && !State.Leave(600000) && State.NextDueMs() == MAX_int64);
    View = ViewAt(State, 540000);
    bGood &= TestTrue(TEXT("A Boss that left is a valid public view: ") + Error, LDCapture::ValidatePublicMatchView(View, Error));
    View = ViewAt(State, 539999);
    bGood &= TestFalse(TEXT("It cannot have left before its leave instant"), LDCapture::ValidatePublicMatchView(View, Error));
    View = ViewAt(State, 540000);
    View.Boss.KilledBy = FGuid(1, 0, 0, 0);
    bGood &= TestFalse(TEXT("A Boss that left names no killer"), LDCapture::ValidatePublicMatchView(View, Error));

    // An appearance still blocked at the leave instant never happens: the Boss leaves from Pending.
    FLDBossState Blocked = MakeState();
    Blocked.Rules.StartedAtMs = 0;
    Blocked.Rules.AppearAtMs = Rules.AppearAtMs;
    Blocked.Rules.LeaveAtMs = Rules.LeaveAtMs;
    bGood &= TestTrue(TEXT("A Boss still pending at T=540 s cannot appear any more and leaves"),
        !Blocked.IsSpawnDue(540000) && !Blocked.Spawned(BossBody(Blocked), 540000) && Blocked.IsLeaveDue(540000)
        && Blocked.Leave(540000) && Blocked.Phase == ELDBossPhase::Left);
    // A Boss killed first keeps its death; it never leaves.
    FLDBossState Killed = MakeState();
    Killed.Rules.StartedAtMs = 0;
    Killed.Rules.AppearAtMs = Rules.AppearAtMs;
    Killed.Rules.LeaveAtMs = Rules.LeaveAtMs;
    bGood &= TestTrue(TEXT("A Boss killed at T=540 s is down, not gone"),
        Killed.Spawned(BossBody(Killed), 240000) && Killed.Died(BossBody(Killed), FGuid(2, 0, 0, 0), 540000)
        && !Killed.IsLeaveDue(540000) && !Killed.Leave(540000) && Killed.Phase == ELDBossPhase::Down);
    FLDBossState Inverted = MakeState();
    Inverted.Rules.LeaveAtMs = Inverted.Rules.AppearAtMs;
    bGood &= TestFalse(TEXT("A leave instant not after the appearance is not valid"), Inverted.IsActive());
    return bGood;
}
#endif
