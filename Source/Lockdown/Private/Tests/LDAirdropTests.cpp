#include "World/LDAirdrop.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDCaptureTypes.h"
#include "Misc/AutomationTest.h"
#include "Tests/LDTestApprovedFixture.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
namespace LDAirdropTests
{
// Test-only schedule with the approved shape (lead <= first landing, each drop gone before the next announcement); the
// values are not the approved ones, so the rules never lean on a particular number.
FLDAirdropState MakeState()
{
    FLDAirdropState State;
    FLDAirdropSchedule& S = State.Schedule;
    S.StartedAtMs = 1000;
    S.LandMs = {30000, 90000, 150000};
    S.AnnounceLeadMs = 10000;
    S.ExpireMs = 20000;
    S.PickupRadiusCm = 120.;
    S.Seed = 0x5EEDu;
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
    // #198: world schedules are field progress T; a field running since T=0 at authority 0 makes T equal NowMs.
    View.Timeline.Generation = View.Generation;
    View.Timeline.Stage = ELDMatchStage::FinalField;
    View.Timeline.FieldEndT = -1;
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
    int64 Land0 = 0, Land1 = 0, Announce0 = 0, Announce1 = 0, Announce2 = 0, Expire0 = 0, Expire1 = 0, Expire2 = 0;
    TestTrue(TEXT("Drop n lands at start + its listed instant, is announced one lead earlier and leaves one expiry later"),
        State.Schedule.LandAtMs(0, Land0) && State.Schedule.LandAtMs(1, Land1) && State.Schedule.AnnounceAtMs(0, Announce0)
        && State.Schedule.AnnounceAtMs(1, Announce1) && State.Schedule.AnnounceAtMs(2, Announce2)
        && State.Schedule.ExpireAtMs(0, Expire0) && State.Schedule.ExpireAtMs(1, Expire1) && State.Schedule.ExpireAtMs(2, Expire2)
        && Land0 == 31000 && Land1 == 91000 && Announce0 == 21000 && Announce1 == 81000 && Announce2 == 141000
        && Expire0 == 51000 && Expire1 == 111000 && Expire2 == 171000);
    int64 Overflow = 0;
    TestFalse(TEXT("There is no drop after the list"), State.Schedule.LandAtMs(3, Overflow) || State.Schedule.AnnounceAtMs(3, Overflow));
    TestFalse(TEXT("An index far beyond the list is refused"), State.Schedule.LandAtMs(MAX_uint64 / 2, Overflow));
    // The point draw: never the previous point, every point in use, and a function of the server seed alone.
    {
        const int32 Count = State.Schedule.Points.Num();
        int32 Previous = INDEX_NONE;
        TArray<int32> Uses;
        Uses.Init(0, Count);
        bool bNeverRepeats = true, bInRange = true;
        TArray<int32> Drawn;
        for (uint64 Serial = 1; Serial <= 300; ++Serial)
        {
            const int32 Next = LDAirdrop::NextPoint(State.Schedule.Seed, Serial, Previous, Count);
            bInRange &= Next >= 0 && Next < Count;
            if (!bInRange) { break; }
            bNeverRepeats &= Next != Previous;
            ++Uses[Next];
            Drawn.Add(Next);
            Previous = Next;
        }
        TestTrue(TEXT("Every drawn point is one of the airdrop points"), bInRange);
        TestTrue(TEXT("A drop never lands on the previous drop's point"), bNeverRepeats);
        TestTrue(TEXT("Over many drops every point is used, roughly evenly"), bInRange
            && !Uses.ContainsByPredicate([](int32 N) { return N < 70; }));
        bool bSame = true, bOtherSeedDiffers = false;
        Previous = INDEX_NONE;
        int32 OtherPrevious = INDEX_NONE;
        for (uint64 Serial = 1; bInRange && Serial <= 300; ++Serial)
        {
            Previous = LDAirdrop::NextPoint(State.Schedule.Seed, Serial, Previous, Count);
            OtherPrevious = LDAirdrop::NextPoint(State.Schedule.Seed + 1, Serial, OtherPrevious, Count);
            bSame &= Previous == Drawn[int32(Serial - 1)];
            bOtherSeedDiffers |= OtherPrevious != Previous;
        }
        TestTrue(TEXT("The same seed draws the same sequence"), bSame);
        TestTrue(TEXT("Another seed draws another sequence"), bOtherSeedDiffers);
        TestEqual(TEXT("A single point is always that point"), LDAirdrop::NextPoint(7, 3, 0, 1), 0);
        TestEqual(TEXT("No points draw nothing"), LDAirdrop::NextPoint(7, 3, INDEX_NONE, 0), int32(INDEX_NONE));
    }
    // The expected point of each drop follows the same draw.
    const auto& Points = State.Schedule.Points;
    const int32 P1 = LDAirdrop::NextPoint(State.Schedule.Seed, 1, INDEX_NONE, Points.Num());
    const int32 P2 = LDAirdrop::NextPoint(State.Schedule.Seed, 2, P1, Points.Num());
    const int32 P3 = LDAirdrop::NextPoint(State.Schedule.Seed, 3, P2, Points.Num());

    // Before the first announcement nothing is public and the next boundary is that announcement.
    TestFalse(TEXT("Nothing changes before the first announcement"), State.Advance(Announce0 - 1));
    TestTrue(TEXT("No drop before the first announcement"), State.Serial == 0 && State.Phase == ELDAirdropPhase::None
        && State.NextDueMs(Announce0 - 1) == Announce0 && !State.CurrentPoint());
    TestFalse(TEXT("A drop that has not landed cannot be claimed"), State.Claim(FGuid(1, 0, 0, 0), Announce0 - 1));
    TestTrue(TEXT("The announcement boundary is inclusive"), State.Advance(Announce0)
        && State.Serial == 1 && State.Phase == ELDAirdropPhase::Announced && State.CurrentPoint()->Id == Points[P1].Id);
    TestTrue(TEXT("While announced the next boundary is the landing"), State.NextDueMs(Announce0) == Land0);
    TestFalse(TEXT("An announced drop cannot be claimed"), State.Claim(FGuid(1, 0, 0, 0), Land0 - 1));
    TestFalse(TEXT("Repeating the same instant changes nothing"), State.Advance(Announce0));
    TestTrue(TEXT("The landing boundary is inclusive"), State.Advance(Land0) && State.Phase == ELDAirdropPhase::Landed);
    TestTrue(TEXT("Once landed the next boundary is its expiry"), State.NextDueMs(Land0) == Expire0);
    FString Error;
    TestTrue(TEXT("A landed drop is a valid public view: ") + Error,
        LDCapture::ValidatePublicMatchView(ViewAt(State, Land0), Error));
    TestTrue(TEXT("The first claimer takes a landed drop once"), State.Claim(FGuid(2, 0, 0, 0), Land0 + 500)
        && State.Phase == ELDAirdropPhase::Claimed && State.ClaimedBy == FGuid(2, 0, 0, 0) && State.ClaimedAtMs == Land0 + 500);
    TestFalse(TEXT("A claimed drop cannot be claimed again"), State.Claim(FGuid(3, 0, 0, 0), Land0 + 600));
    TestTrue(TEXT("A claimed drop is a valid public view: ") + Error,
        LDCapture::ValidatePublicMatchView(ViewAt(State, Land0 + 600), Error));
    TestTrue(TEXT("A claimed drop waits for the next announcement only"), State.NextDueMs(Land0 + 600) == Announce1);
    TestFalse(TEXT("A claimed drop stays until the next announcement"), State.Advance(Announce1 - 1));
    TestTrue(TEXT("The next announcement replaces a claimed drop"), State.Advance(Announce1)
        && State.Serial == 2 && State.Phase == ELDAirdropPhase::Announced && !State.ClaimedBy.IsValid()
        && State.CurrentPoint()->Id == Points[P2].Id);
    State.Advance(Land1);
    TestFalse(TEXT("An unclaimed drop lies until its expiry"), State.Advance(Expire1 - 1));
    TestTrue(TEXT("At its expiry an unclaimed drop is gone"), State.Advance(Expire1) && State.Serial == 2
        && State.Phase == ELDAirdropPhase::Expired && !State.HoldsPoint() && State.NextDueMs(Expire1) == Announce2);
    TestFalse(TEXT("An expired drop cannot be claimed"), State.Claim(FGuid(4, 0, 0, 0), Expire1));
    TestTrue(TEXT("An expired drop is a valid public view: ") + Error, LDCapture::ValidatePublicMatchView(ViewAt(State, Expire1), Error));
    {
        FLDPublicMatchView Early = ViewAt(State, Expire1 - 1);
        TestFalse(TEXT("A drop cannot expire before its expiry"), LDCapture::ValidatePublicMatchView(Early, Error));
    }
    TestFalse(TEXT("An expired drop stays gone until the next announcement"), State.Advance(Announce2 - 1));
    TestTrue(TEXT("The next announcement replaces it"), State.Advance(Announce2) && State.Serial == 3
        && State.Phase == ELDAirdropPhase::Announced && State.CurrentPoint()->Id == Points[P3].Id);
    TestTrue(TEXT("After the last drop's expiry nothing more is due and no drop follows"),
        State.Advance(Expire2) && State.Phase == ELDAirdropPhase::Expired && State.NextDueMs(Expire2) == MAX_int64
        && !State.Advance(Expire2 + 10000000) && State.Serial == 3);
    FLDAirdropState Late = MakeState();
    TestTrue(TEXT("A boundary after a landing starts on the ground, never behind it"),
        Late.Advance(Land0 + 5) && Late.Serial == 1 && Late.Phase == ELDAirdropPhase::Landed);
    FLDAirdropState Skip = MakeState();
    TestTrue(TEXT("Skipping drops still draws each point from the one before it"),
        Skip.Advance(Announce2) && Skip.Serial == 3 && Skip.CurrentPoint()->Id == Points[P3].Id);
    FLDAirdropState Unseeded = MakeState();
    Unseeded.Schedule.Seed = 0;
    TestFalse(TEXT("A schedule without its server seed is not valid"), Unseeded.IsActive());

    // The claimer is the nearest living, unprotected contender inside the closed disk; exact ties are a seeded draw.
    const FVector Centre(0, -3300, 0);
    const double Radius = State.Schedule.PickupRadiusCm;
    const uint64 Tie = State.TieSeed();
    using FCandidates = TArray<LDAirdrop::FCandidate>;
    FGuid Claimer;
    TestFalse(TEXT("Nobody inside the disk claims nothing"), LDAirdrop::PickClaimer(Centre, Radius,
        FCandidates{{FGuid(1, 0, 0, 0), Centre + FVector(Radius + .01, 0, 0)}}, Tie, Claimer) || Claimer.IsValid());
    TestTrue(TEXT("The pickup disk boundary is inside"), LDAirdrop::PickClaimer(Centre, Radius,
        FCandidates{{FGuid(1, 0, 0, 0), Centre + FVector(0, Radius, 0)}}, Tie, Claimer) && Claimer == FGuid(1, 0, 0, 0));
    TestTrue(TEXT("The nearest contender inside claims"), LDAirdrop::PickClaimer(Centre, Radius,
        FCandidates{{FGuid(1, 0, 0, 0), Centre + FVector(Radius * .8, 0, 0)}, {FGuid(9, 0, 0, 0), Centre + FVector(0, Radius * .2, 0)}},
        Tie, Claimer) && Claimer == FGuid(9, 0, 0, 0));
    TestTrue(TEXT("Height does not matter, only the foot's ground position"), LDAirdrop::PickClaimer(Centre, Radius,
        FCandidates{{FGuid(5, 0, 0, 0), Centre + FVector(10, 0, 500)}}, Tie, Claimer) && Claimer == FGuid(5, 0, 0, 0));
    TestFalse(TEXT("An invalid contender or position never claims"), LDAirdrop::PickClaimer(Centre, Radius,
        FCandidates{{FGuid(), Centre}, {FGuid(6, 0, 0, 0), FVector(std::numeric_limits<double>::quiet_NaN(), 0, 0)}}, Tie, Claimer));
    // Respawn protection: a protected contender never claims, however near; the next unprotected one does.
    LDAirdrop::FCandidate Protected{FGuid(2, 0, 0, 0), Centre, true};
    TestFalse(TEXT("A protected contender alone cannot claim"), LDAirdrop::PickClaimer(Centre, Radius,
        FCandidates{Protected}, Tie, Claimer));
    TestTrue(TEXT("A protected contender on the centre leaves the drop to an unprotected one inside the disk"),
        LDAirdrop::PickClaimer(Centre, Radius, FCandidates{Protected, {FGuid(3, 0, 0, 0), Centre + FVector(Radius * .9, 0, 0)}},
            Tie, Claimer) && Claimer == FGuid(3, 0, 0, 0));
    // An exact tie: the same draw whatever the input order, and the draw (not the id) decides across drops.
    const LDAirdrop::FCandidate Low{FGuid(4, 0, 0, 0), Centre - FVector(50, 0, 0)}, High{FGuid(7, 0, 0, 0), Centre + FVector(50, 0, 0)};
    FGuid Forward, Backward;
    TestTrue(TEXT("An exact tie is decided the same way in any input order"),
        LDAirdrop::PickClaimer(Centre, Radius, FCandidates{Low, High}, Tie, Forward)
        && LDAirdrop::PickClaimer(Centre, Radius, FCandidates{High, Low}, Tie, Backward) && Forward == Backward);
    bool bLowWon = false, bHighWon = false;
    for (uint64 Draw = 1; Draw <= 64; ++Draw)
    {
        FGuid Winner;
        LDAirdrop::PickClaimer(Centre, Radius, FCandidates{Low, High}, LDAirdrop::Mix(Draw), Winner);
        bLowWon |= Winner == Low.Contender;
        bHighWon |= Winner == High.Contender;
    }
    TestTrue(TEXT("Across draws either tied contender can win: the contender id does not decide"), bLowWon && bHighWon);

    // The public view must match its phase.
    FLDAirdropState Public = MakeState();
    Public.Advance(Announce0);
    FLDPublicMatchView View = ViewAt(Public, Announce0);
    TestTrue(TEXT("An announced drop is a valid public view: ") + Error, LDCapture::ValidatePublicMatchView(View, Error));
    View = ViewAt(Public, Announce0);
    View.Airdrop.Phase = ELDAirdropPhase::Landed;
    TestFalse(TEXT("A drop cannot be on the ground before its landing"), LDCapture::ValidatePublicMatchView(View, Error));
    View = ViewAt(Public, Announce0);
    View.Airdrop.Phase = ELDAirdropPhase::Claimed;
    TestFalse(TEXT("A drop cannot be claimed before its landing"), LDCapture::ValidatePublicMatchView(View, Error));
    View = ViewAt(Public, Announce0);
    View.Airdrop.PointId = NAME_None;
    TestFalse(TEXT("A drop must name its public point"), LDCapture::ValidatePublicMatchView(View, Error));
    View = ViewAt(Public, Announce0);
    View.Phase = ELDMatchPhase::Lobby;
    View.ServerNowMs = View.StartedAtMs = View.OpenAtMs = 0;
    TestFalse(TEXT("A room outside a match publishes no drop"), LDCapture::ValidatePublicMatchView(View, Error));
    FLDAirdropState Invalid = MakeState();
    Invalid.Schedule.ExpireMs = 60000;
    TestFalse(TEXT("A drop still lying at the next announcement is not a valid schedule"), Invalid.IsActive());
    TestFalse(TEXT("An invalid schedule never announces"), Invalid.Advance(Land0) || Invalid.Serial != 0);
    Invalid = MakeState();
    Invalid.Schedule.LandMs = {30000, 30000};
    TestFalse(TEXT("Landings must be strictly increasing"), Invalid.IsActive());
    Invalid = MakeState();
    Invalid.Schedule.LandMs = {5000};
    TestFalse(TEXT("A drop must be announced after the start"), Invalid.IsActive());
    return true;
}

// #198 outer §5 (approved https://github.com/rlsn/Lockdown-alpha/issues/198#issuecomment-6752601175, comment 6752628806:
// "批准整套方案，按票内参数实施"): six drops at field T=45/125/225/305/405/485 s, each announced 15 s earlier and gone
// 40 s after landing unless claimed (200 XP plus one build reward draw); never a seventh, and none in the final field.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDAirdropApprovedScheduleTest, "Lockdown.Rules.DemoAirdropApprovedSchedule",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDAirdropApprovedScheduleTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDTestApprovedFixture::Read(*this, Catalog)) { return false; }
    const auto& P = Catalog->Get().Parameters;
    bool bGood = TestTrue(TEXT("The approved landings are T=45/125/225/305/405/485 s"),
        P.AirdropLandMs == TArray<int64>({45000, 125000, 225000, 305000, 405000, 485000}));
    bGood &= TestTrue(TEXT("Announced 15 s early, gone 40 s after landing, 200 XP"),
        P.AirdropAnnounceLeadMs == 15000 && P.AirdropExpireMs == 40000 && P.AirdropRewardXP == 200);
    FLDAirdropState State = LDAirdropTests::MakeState();
    State.Schedule.StartedAtMs = 0;
    State.Schedule.LandMs = P.AirdropLandMs;
    State.Schedule.AnnounceLeadMs = P.AirdropAnnounceLeadMs;
    State.Schedule.ExpireMs = P.AirdropExpireMs;
    if (!TestTrue(TEXT("The approved schedule is valid"), State.IsActive())) { return false; }
    // Walk the field clock from boundary to boundary, as the authority does.
    TArray<int64> Announced, Landed, Expired;
    int64 Now = 0;
    for (int32 Guard = 0; Guard < 64; ++Guard)
    {
        const int64 Due = State.NextDueMs(Now);
        if (Due == MAX_int64) { break; }
        Now = Due;
        if (!State.Advance(Now)) { continue; }
        (State.Phase == ELDAirdropPhase::Announced ? Announced : State.Phase == ELDAirdropPhase::Landed ? Landed : Expired).Add(Now);
    }
    bGood &= TestTrue(TEXT("Six announcements, each 15 s before its landing"),
        Announced == TArray<int64>({30000, 110000, 210000, 290000, 390000, 470000}));
    bGood &= TestTrue(TEXT("Six landings at the approved instants"), Landed == P.AirdropLandMs);
    bGood &= TestTrue(TEXT("Unclaimed, each is gone 40 s after landing, the last before the final field"),
        Expired == TArray<int64>({85000, 165000, 265000, 345000, 445000, 525000})
        && Expired.Last() <= P.FieldSegmentMs * FLDTimelineParameters::TimedSegments);
    bGood &= TestTrue(TEXT("There is no seventh drop"), State.Serial == 6 && !State.Advance(10000000) && State.Serial == 6);
    return bGood;
}
#endif
