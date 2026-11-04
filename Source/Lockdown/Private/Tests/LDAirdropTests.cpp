#include "World/LDAirdrop.h"
#include "Framework/LDCaptureTypes.h"
#include "Misc/AutomationTest.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
namespace LDAirdropTests
{
// Test-only schedule with the approved shape (lead <= first landing, lead < interval); the values are not the
// approved ones, so the rules never lean on a particular number.
FLDAirdropState MakeState()
{
    FLDAirdropState State;
    FLDAirdropSchedule& S = State.Schedule;
    S.StartedAtMs = 1000;
    S.FirstLandMs = 30000;
    S.IntervalMs = 60000;
    S.AnnounceLeadMs = 10000;
    S.PickupRadiusCm = 120.;
    S.PointOffset = 1;
    S.Points = {{TEXT("airdrop_ne"), FVector(1940, 2670, 0)}, {TEXT("airdrop_nw"), FVector(-1940, 2670, 0)},
        {TEXT("airdrop_s"), FVector(0, -3300, 0)}};
    return State;
}

FLDPublicMatchView ViewAt(const FLDAirdropState& State, int64 NowMs)
{
    FLDPublicMatchView View;
    View.Generation = 1;
    View.PublicRevision = 1;
    View.Phase = ELDMatchPhase::Development;
    View.ServerNowMs = NowMs;
    View.StartedAtMs = State.Schedule.StartedAtMs;
    View.OpenAtMs = 1000000;
    View.Airdrop = State.View();
    return View;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDAirdropRulesTest, "Lockdown.Rules.DemoAirdrop.Rules",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDAirdropRulesTest::RunTest(const FString& Parameters)
{
    using namespace LDAirdropTests;
    FLDAirdropState State = MakeState();
    if (!TestTrue(TEXT("The test schedule is valid"), State.IsActive())) { return false; }
    int64 Land0 = 0, Land1 = 0, Announce0 = 0, Announce1 = 0;
    TestTrue(TEXT("Drop n lands at start + first + n * interval and is announced one lead earlier"),
        State.Schedule.LandAtMs(0, Land0) && State.Schedule.LandAtMs(1, Land1) && State.Schedule.AnnounceAtMs(0, Announce0)
        && State.Schedule.AnnounceAtMs(1, Announce1)
        && Land0 == 31000 && Land1 == 91000 && Announce0 == 21000 && Announce1 == 81000);
    int64 Overflow = 0;
    TestFalse(TEXT("An unrepresentable drop instant is refused"), State.Schedule.LandAtMs(MAX_uint64 / 2, Overflow));
    TestTrue(TEXT("The rotation starts at the match offset and visits every point"),
        State.Schedule.PointOf(0).Id == TEXT("airdrop_nw") && State.Schedule.PointOf(1).Id == TEXT("airdrop_s")
        && State.Schedule.PointOf(2).Id == TEXT("airdrop_ne") && State.Schedule.PointOf(3).Id == TEXT("airdrop_nw"));

    // Before the first announcement nothing is public and the next boundary is that announcement.
    TestFalse(TEXT("Nothing changes before the first announcement"), State.Advance(Announce0 - 1));
    TestTrue(TEXT("No drop before the first announcement"), State.Serial == 0 && State.Phase == ELDAirdropPhase::None
        && State.NextDueMs(Announce0 - 1) == Announce0 && !State.CurrentPoint());
    TestFalse(TEXT("A drop that has not landed cannot be claimed"), State.Claim(FGuid(1, 0, 0, 0), Announce0 - 1));
    TestTrue(TEXT("The announcement boundary is inclusive"), State.Advance(Announce0)
        && State.Serial == 1 && State.Phase == ELDAirdropPhase::Announced && State.CurrentPoint()->Id == TEXT("airdrop_nw"));
    TestTrue(TEXT("While announced the next boundary is the landing"), State.NextDueMs(Announce0) == Land0);
    TestFalse(TEXT("An announced drop cannot be claimed"), State.Claim(FGuid(1, 0, 0, 0), Land0 - 1));
    TestFalse(TEXT("Repeating the same instant changes nothing"), State.Advance(Announce0));
    TestTrue(TEXT("The landing boundary is inclusive"), State.Advance(Land0) && State.Phase == ELDAirdropPhase::Landed);
    TestTrue(TEXT("Once landed the next boundary is the next announcement"), State.NextDueMs(Land0) == Announce1);
    FString Error;
    TestTrue(TEXT("A landed drop is a valid public view: ") + Error,
        LDCapture::ValidatePublicMatchView(ViewAt(State, Land0), Error));
    TestTrue(TEXT("The first claimer takes a landed drop once"), State.Claim(FGuid(2, 0, 0, 0), Land0 + 500)
        && State.Phase == ELDAirdropPhase::Claimed && State.ClaimedBy == FGuid(2, 0, 0, 0) && State.ClaimedAtMs == Land0 + 500);
    TestFalse(TEXT("A claimed drop cannot be claimed again"), State.Claim(FGuid(3, 0, 0, 0), Land0 + 600));
    TestTrue(TEXT("A claimed drop is a valid public view: ") + Error,
        LDCapture::ValidatePublicMatchView(ViewAt(State, Land0 + 600), Error));
    TestFalse(TEXT("A claimed drop stays until the next announcement"), State.Advance(Announce1 - 1));
    TestTrue(TEXT("The next announcement replaces a claimed drop"), State.Advance(Announce1)
        && State.Serial == 2 && State.Phase == ELDAirdropPhase::Announced && !State.ClaimedBy.IsValid()
        && State.CurrentPoint()->Id == TEXT("airdrop_s"));
    State.Advance(Land1);
    TestTrue(TEXT("An unclaimed drop is replaced by the next announcement"),
        State.Advance(Land1 + State.Schedule.IntervalMs - State.Schedule.AnnounceLeadMs) && State.Serial == 3
        && State.Phase == ELDAirdropPhase::Announced && State.CurrentPoint()->Id == TEXT("airdrop_ne"));
    FLDAirdropState Late = MakeState();
    TestTrue(TEXT("A boundary after a landing starts on the ground, never behind it"),
        Late.Advance(Land0 + 5) && Late.Serial == 1 && Late.Phase == ELDAirdropPhase::Landed);

    // The claimer is the nearest living contender inside the closed disk, ties by contender id.
    const FVector Centre(0, -3300, 0);
    const double Radius = State.Schedule.PickupRadiusCm;
    FGuid Claimer;
    TestFalse(TEXT("Nobody inside the disk claims nothing"), LDAirdrop::PickClaimer(Centre, Radius,
        {{FGuid(1, 0, 0, 0), Centre + FVector(Radius + .01, 0, 0)}}, Claimer) || Claimer.IsValid());
    TestTrue(TEXT("The pickup disk boundary is inside"), LDAirdrop::PickClaimer(Centre, Radius,
        {{FGuid(1, 0, 0, 0), Centre + FVector(0, Radius, 0)}}, Claimer) && Claimer == FGuid(1, 0, 0, 0));
    TestTrue(TEXT("The nearest contender inside claims"), LDAirdrop::PickClaimer(Centre, Radius,
        {{FGuid(1, 0, 0, 0), Centre + FVector(Radius * .8, 0, 0)}, {FGuid(9, 0, 0, 0), Centre + FVector(0, Radius * .2, 0)}},
        Claimer) && Claimer == FGuid(9, 0, 0, 0));
    TestTrue(TEXT("Equal distance is decided by contender id, not order"), LDAirdrop::PickClaimer(Centre, Radius,
        {{FGuid(7, 0, 0, 0), Centre + FVector(50, 0, 0)}, {FGuid(4, 0, 0, 0), Centre - FVector(50, 0, 0)}}, Claimer)
        && Claimer == FGuid(4, 0, 0, 0));
    TestTrue(TEXT("Height does not matter, only the foot's ground position"), LDAirdrop::PickClaimer(Centre, Radius,
        {{FGuid(5, 0, 0, 0), Centre + FVector(10, 0, 500)}}, Claimer) && Claimer == FGuid(5, 0, 0, 0));
    TestFalse(TEXT("An invalid contender or position never claims"), LDAirdrop::PickClaimer(Centre, Radius,
        {{FGuid(), Centre}, {FGuid(6, 0, 0, 0), FVector(std::numeric_limits<double>::quiet_NaN(), 0, 0)}}, Claimer));

    // The public view must match its phase.
    FLDAirdropState Public = MakeState();
    Public.Advance(Announce0);
    FLDPublicMatchView View = ViewAt(Public, Announce0);
    TestTrue(TEXT("An announced drop is a valid public view: ") + Error, LDCapture::ValidatePublicMatchView(View, Error));
    View.Airdrop.ClaimedBy = FGuid(1, 0, 0, 0);
    TestFalse(TEXT("An unclaimed drop cannot name a claimer"), LDCapture::ValidatePublicMatchView(View, Error));
    View = ViewAt(Public, Announce0);
    View.Airdrop.Phase = ELDAirdropPhase::Landed;
    TestFalse(TEXT("A drop cannot be on the ground before its landing"), LDCapture::ValidatePublicMatchView(View, Error));
    View = ViewAt(Public, Announce0);
    View.Airdrop.PointId = NAME_None;
    TestFalse(TEXT("A drop must name its public point"), LDCapture::ValidatePublicMatchView(View, Error));
    View = ViewAt(Public, Announce0);
    View.Phase = ELDMatchPhase::Lobby;
    View.ServerNowMs = View.StartedAtMs = View.OpenAtMs = 0;
    TestFalse(TEXT("A room outside a match publishes no drop"), LDCapture::ValidatePublicMatchView(View, Error));
    FLDAirdropState Invalid = MakeState();
    Invalid.Schedule.AnnounceLeadMs = Invalid.Schedule.IntervalMs;
    TestFalse(TEXT("An announcement lead of a whole interval is not a valid schedule"), Invalid.IsActive());
    TestFalse(TEXT("An invalid schedule never announces"), Invalid.Advance(Land0) || Invalid.Serial != 0);
    return true;
}
#endif
