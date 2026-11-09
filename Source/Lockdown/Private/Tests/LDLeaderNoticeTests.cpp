#include "World/LDLeaderNotice.h"
#include "Framework/LDCaptureTypes.h"
#include "Misc/AutomationTest.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
namespace LDLeaderNoticeTests
{
// Test-only rules with the approved shape (offset <= radius, cap >= one step); the values are not the approved ones,
// so the rules never lean on a particular number.
FLDLeaderState MakeState()
{
    FLDLeaderState State;
    FLDLeaderRules& R = State.Rules;
    R.StartedAtMs = 1000;
    R.RefreshMs = 8000;
    R.MinLevel = 2;
    R.CircleRadiusCm = 1200.;
    R.CircleMaxOffsetCm = 700.;
    R.Seed = 0x5eed;
    return State;
}

LDLeaderNotice::FCandidate Candidate(uint32 Id, uint8 Level, uint64 Kills, bool bAlive = true,
    const FVector& FootCm = FVector(300, -200, 0))
{
    LDLeaderNotice::FCandidate Out;
    Out.Contender = FGuid(Id, 0, 0, 0);
    Out.Level = Level;
    Out.Kills = Kills;
    Out.bAlive = bAlive;
    Out.FootCm = FootCm;
    return Out;
}

FLDPublicMatchView ViewAt(const FLDLeaderState& State, int64 NowMs)
{
    FLDPublicMatchView View;
    View.Generation = 1;
    View.PublicRevision = 1;
    View.Phase = ELDMatchPhase::Development;
    View.ServerNowMs = NowMs;
    View.StartedAtMs = State.Rules.StartedAtMs;
    View.OpenAtMs = 1000000;
    View.Leader = State.View();
    return View;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDLeaderNoticeRulesTest, "Lockdown.Rules.DemoLeader.Rules",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDLeaderNoticeRulesTest::RunTest(const FString& Parameters)
{
    using namespace LDLeaderNoticeTests;
    using LDLeaderNotice::PickLeader;
    const FGuid A(1, 0, 0, 0), B(2, 0, 0, 0);

    // Who is named: the unique highest level at or above the minimum, ties by kills, otherwise nobody.
    TestTrue(TEXT("The unique highest level is named"), PickLeader(2, {Candidate(1, 4, 0), Candidate(2, 3, 9)}) == A);
    TestTrue(TEXT("A level tie is broken by kills"), PickLeader(2, {Candidate(1, 4, 1), Candidate(2, 4, 3)}) == B);
    TestFalse(TEXT("A tie on level and kills names nobody"), PickLeader(2, {Candidate(1, 4, 2), Candidate(2, 4, 2)}).IsValid());
    TestFalse(TEXT("A tie at the top names nobody even after a lower candidate"),
        PickLeader(2, {Candidate(1, 5, 2), Candidate(3, 2, 0), Candidate(2, 5, 2)}).IsValid());
    TestFalse(TEXT("Nobody below the minimum level is named"), PickLeader(2, {Candidate(1, 1, 9)}).IsValid());
    TestTrue(TEXT("Candidates below the minimum never tie the leader"), PickLeader(3, {Candidate(1, 3, 0), Candidate(2, 2, 0)}) == A);
    TestFalse(TEXT("An invalid contender is never named"), PickLeader(1, {Candidate(0, 9, 9)}).IsValid());

    FLDLeaderState State = MakeState();
    if (!TestTrue(TEXT("The test rules are valid"), State.IsActive())) { return false; }
    const int64 R1 = 9000, R2 = 17000, R3 = 25000, R4 = 33000;
    TestEqual(TEXT("The first refresh is one interval after the start"), State.NextDueMs(1000), R1);
    TestEqual(TEXT("The boundary itself plans the next refresh"), State.NextDueMs(R1), R2);
    TestFalse(TEXT("Nothing is due before the first refresh"), State.IsRefreshDue(R1 - 1));
    TestFalse(TEXT("A refresh is never applied early"), State.Refresh(R1 - 1, {Candidate(1, 4, 0)}));

    // The leader is named inside a circle no farther than the maximum offset.
    const FVector Foot(300, -200, 0);
    TestTrue(TEXT("The first refresh names the leader"), State.Refresh(R1, {Candidate(1, 4, 0), Candidate(2, 3, 0)}));
    TestTrue(TEXT("The highest level is the leader"), State.Leader == A && State.RefreshIndex == 1);
    TestTrue(TEXT("The centre is within the maximum offset of the foot, so the leader is inside the circle"),
        FVector::Dist2D(State.CircleCentreCm, Foot) <= State.Rules.CircleMaxOffsetCm + 1e-6 && State.RefreshedAtMs == R1);
    const FVector FirstCentre = State.CircleCentreCm;
    TestFalse(TEXT("The same boundary is not applied twice"), State.Refresh(R1, {Candidate(1, 4, 0)}));

    FLDLeaderState Replay = MakeState();
    Replay.Refresh(R1, {Candidate(1, 4, 0), Candidate(2, 3, 0)});
    TestTrue(TEXT("The offset depends only on the match seed and refresh index"), Replay.CircleCentreCm == FirstCentre);

    // Staying leader keeps the notice; the offset moves with the refresh.
    TestTrue(TEXT("The second refresh keeps the leader"), State.Refresh(R2, {Candidate(1, 5, 0), Candidate(2, 3, 0)}));
    TestTrue(TEXT("The kept leader is still named"), State.Leader == A && State.View().Leader == A);
    State.Refresh(R3, {Candidate(1, 5, 0)});

    // A skipped boundary is applied once, at the boundary the authority reached.
    FLDLeaderState Late = MakeState();
    TestTrue(TEXT("A late step applies only the latest boundary"), Late.Refresh(R2 + 5, {Candidate(1, 4, 0)})
        && Late.RefreshIndex == 2 && Late.Leader == A);

    // Another contender overtaking replaces the leader.
    TestTrue(TEXT("Another contender overtaking is named"), State.Refresh(R4, {Candidate(1, 5, 0), Candidate(2, 6, 0)}));
    TestTrue(TEXT("The new leader replaces the old one"), State.Leader == B && State.View().Leader == B);

    // The leader's death ends the notice; the next refresh starts over.
    TestFalse(TEXT("Another contender's death leaves the notice"), State.LeaderDied(A, R4 + 100));
    TestTrue(TEXT("The leader's death ends the notice"), State.LeaderDied(B, R4 + 200));
    TestTrue(TEXT("The death keeps who fell and when"), State.LastDownLeader == B && State.LastDownAtMs == R4 + 200);
    TestTrue(TEXT("Nothing is shown after the leader's death"), !State.View().Leader.IsValid() && !State.Leader.IsValid());
    State.Refresh(R4 + 8000, {Candidate(2, 6, 0)});
    TestTrue(TEXT("A leader can be named again after a death"), State.Leader == B);

    // A leader dead at the refresh, or nobody unique, shows nothing.
    TestTrue(TEXT("A leader dead at the refresh is not shown"),
        State.Refresh(R4 + 16000, {Candidate(2, 6, 0, false)}) && !State.Leader.IsValid());
    TestFalse(TEXT("Still nobody: the view does not change"), State.Refresh(R4 + 24000, {Candidate(1, 3, 1), Candidate(2, 3, 1)}));

    // The public view must match its leader and the match clock.
    FLDLeaderState Public = MakeState();
    Public.Refresh(R1, {Candidate(1, 4, 0)});
    FString Error;
    FLDPublicMatchView View = ViewAt(Public, R1);
    TestTrue(TEXT("A named leader is a valid public view: ") + Error, LDCapture::ValidatePublicMatchView(View, Error));
    View = ViewAt(Public, R1 - 1);
    TestFalse(TEXT("A notice cannot be from the future"), LDCapture::ValidatePublicMatchView(View, Error));
    View = ViewAt(MakeState(), R1);
    View.Leader.RefreshedAtMs = R1;
    TestFalse(TEXT("Nobody named carries no refresh time"), LDCapture::ValidatePublicMatchView(View, Error));
    View = ViewAt(Public, R1);
    View.Leader.CircleCentreCm.X = std::numeric_limits<double>::quiet_NaN();
    TestFalse(TEXT("A circle centre must be a number"), LDCapture::ValidatePublicMatchView(View, Error));
    View = ViewAt(Public, R1);
    View.Phase = ELDMatchPhase::Lobby;
    View.ServerNowMs = View.StartedAtMs = View.OpenAtMs = 0;
    TestFalse(TEXT("A room outside a match names no leader"), LDCapture::ValidatePublicMatchView(View, Error));

    FLDLeaderState Invalid = MakeState();
    Invalid.Rules.CircleMaxOffsetCm = Invalid.Rules.CircleRadiusCm + 1.;
    TestFalse(TEXT("An offset beyond the radius could leave the leader outside: invalid rules"), Invalid.IsActive());
    TestFalse(TEXT("Invalid rules never name a leader"), Invalid.Refresh(R1, {Candidate(1, 4, 0)}) || Invalid.Leader.IsValid());
    return true;
}
#endif
