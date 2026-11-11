#include "World/LDCharms.h"
#include "Framework/LDCaptureTypes.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace LDCharmsTests
{
// Test-only schedule with the approved shape (distinct kinds per wave, lifetime inside one interval, more points than one
// wave); the values are not the approved ones, so the rules never lean on a particular number.
FLDCharmState MakeState()
{
    FLDCharmState State;
    FLDCharmSchedule& S = State.Schedule;
    S.FirstWaveMs = 20000;
    S.WaveIntervalMs = 60000;
    S.WaveCount = 3;
    S.PerWave = 2;
    S.LifetimeMs = 30000;
    S.PickupRadiusCm = 80.;
    S.Seed = 0xC4A2u;
    S.Points = {{TEXT("airdrop_e"), FVector(2700, -200, 0)}, {TEXT("airdrop_nw"), FVector(-1800, 2100, 0)},
        {TEXT("airdrop_se"), FVector(2000, -1800, 0)}, {TEXT("airdrop_w"), FVector(-2300, 400, 0)}};
    return State;
}

LDAirdrop::FCandidate Candidate(uint32 Id, const FVector& Foot, bool bProtected = false)
{
    return {FGuid(Id, Id, Id, Id), Foot, bProtected};
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCharmsRulesTest, "Lockdown.Rules.DemoCharms.Rules",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCharmsRulesTest::RunTest(const FString& Parameters)
{
    using namespace LDCharmsTests;
    FLDCharmState State = MakeState();
    if (!TestTrue(TEXT("The test schedule is valid"), State.IsActive())) { return false; }
    {
        FLDCharmSchedule Bad = State.Schedule;
        Bad.PerWave = LDCharms::KindCount + 1;
        TestFalse(TEXT("A wave cannot draw more distinct kinds than exist"), Bad.IsValid());
        Bad = State.Schedule;
        Bad.LifetimeMs = Bad.WaveIntervalMs + 1;
        TestFalse(TEXT("A charm lives inside one wave interval"), Bad.IsValid());
        Bad = State.Schedule;
        Bad.Points.SetNum(2);
        TestFalse(TEXT("There are more points than one wave places"), Bad.IsValid());
    }
    int64 Wave0 = 0, Wave2 = 0, Beyond = 0;
    TestTrue(TEXT("Wave n lands at first + n * interval of field progress, and only n < WaveCount exists"),
        State.Schedule.WaveAtMs(0, Wave0) && State.Schedule.WaveAtMs(2, Wave2) && !State.Schedule.WaveAtMs(3, Beyond)
        && Wave0 == 20000 && Wave2 == 140000);

    FLDCharmAdvance Step;
    State.Advance(19999, INDEX_NONE, Step);
    TestTrue(TEXT("Nothing lies before the first wave"), State.Active.IsEmpty() && !Step.Changed());
    TestEqual(TEXT("The first wave is the next due instant"), State.NextDueFieldMs(19999), int64(20000));
    // The airdrop holds point 1: the wave avoids it.
    State.Advance(20000, 1, Step);
    TestEqual(TEXT("A wave places PerWave charms at its inclusive instant"), State.Active.Num(), 2);
    if (State.Active.Num() == 2)
    {
        const FLDCharmInstance& A = State.Active[0];
        const FLDCharmInstance& B = State.Active[1];
        TestTrue(TEXT("Serials count from 1 in order"), A.Serial == 1 && B.Serial == 2);
        TestTrue(TEXT("One wave's kinds are distinct and known"), A.Kind != B.Kind && A.Kind != ELDCharmKind::None
            && B.Kind != ELDCharmKind::None);
        TestTrue(TEXT("One wave's points are distinct and never the airdrop's"), A.PointIndex != B.PointIndex
            && A.PointIndex != 1 && B.PointIndex != 1);
        TestTrue(TEXT("A charm expires one lifetime after its wave instant"), A.ExpireFieldMs == 50000 && B.ExpireFieldMs == 50000);
    }
    TestEqual(TEXT("The expiry is the next due instant"), State.NextDueFieldMs(20000), int64(50000));
    // The same seed draws the same wave; the public view lists what lies, in serial order.
    {
        FLDCharmState Again = MakeState();
        FLDCharmAdvance Ignored;
        Again.Advance(20000, 1, Ignored);
        bool bSame = Again.Active.Num() == State.Active.Num();
        for (int32 Index = 0; bSame && Index < Again.Active.Num(); ++Index)
        {
            bSame = Again.Active[Index].Kind == State.Active[Index].Kind && Again.Active[Index].PointIndex == State.Active[Index].PointIndex;
        }
        TestTrue(TEXT("A wave is a function of the server seed"), bSame);
        TArray<FLDCharmView> View;
        State.View(View);
        TestTrue(TEXT("The public view lists each lying charm's kind and point"), View.Num() == 2 && View[0].Serial == 1
            && View[0].PointId == State.Schedule.Points[State.Active[0].PointIndex].Id && View[0].ExpireAtFieldMs == 50000);
    }
    // Claim: only an eligible contender inside the closed disk; several at once are decided by the fair key, not by
    // distance or order.
    {
        const FVector Centre(0, 0, 0);
        const uint64 Seed = State.TieSeed(1);
        FGuid Out;
        TestFalse(TEXT("A protected contender or a foot outside the disk claims nothing"), LDCharms::PickClaimer(Centre, 80.,
            {Candidate(1, FVector(10, 0, 0), true), Candidate(2, FVector(80.5, 0, 0))}, Seed, Out));
        TestTrue(TEXT("The disk is closed"), LDCharms::PickClaimer(Centre, 80., {Candidate(3, FVector(80, 0, 0))}, Seed, Out)
            && Out == FGuid(3, 3, 3, 3));
        FGuid Forward, Backward;
        const TArray<LDAirdrop::FCandidate> Both = {Candidate(4, FVector(1, 0, 0)), Candidate(5, FVector(70, 0, 0))};
        const TArray<LDAirdrop::FCandidate> Reversed = {Both[1], Both[0]};
        LDCharms::PickClaimer(Centre, 80., Both, Seed, Forward);
        LDCharms::PickClaimer(Centre, 80., Reversed, Seed, Backward);
        TestTrue(TEXT("Input order never decides a simultaneous claim"), Forward.IsValid() && Forward == Backward);
        bool bFarWins = false;
        for (uint64 Serial = 1; Serial <= 64 && !bFarWins; ++Serial)
        {
            FGuid Winner;
            bFarWins = LDCharms::PickClaimer(Centre, 80., Both, State.TieSeed(Serial), Winner) && Winner == FGuid(5, 5, 5, 5);
        }
        TestTrue(TEXT("Being nearer does not win a simultaneous claim"), bFarWins);
    }
    FLDCharmInstance Taken;
    TestTrue(TEXT("A claimed charm leaves; a second removal is refused"), State.Remove(1, Taken) && Taken.Serial == 1
        && !State.Remove(1, Taken) && State.Active.Num() == 1);
    State.Advance(50000, INDEX_NONE, Step);
    TestTrue(TEXT("An unclaimed charm expires at its inclusive lifetime end"), State.Active.IsEmpty() && Step.Expired.Num() == 1);
    // All four points held (two charms of a later wave cannot fit when two points are blocked): the missing instance is
    // skipped with a reason, never granted.
    {
        FLDCharmState Crowded = MakeState();
        Crowded.Schedule.Points.SetNum(3);
        FLDCharmAdvance Crowd;
        Crowded.Advance(20000, 0, Crowd);
        TestTrue(TEXT("The first wave fits the two free points"), Crowded.Active.Num() == 2 && Crowd.Skipped.IsEmpty());
        FLDCharmInstance Keep = Crowded.Active[0];
        Crowded.Active[0].ExpireFieldMs = 200000;
        Crowded.Active.RemoveAt(1);
        Crowded.Advance(80000, Keep.PointIndex == 1 ? 2 : 1, Crowd);
        TestTrue(TEXT("Too few free points place what fits and skip the rest with a reason"), Crowd.Spawned.Num() == 1
            && Crowd.Skipped.Num() == 1 && Crowd.Skipped[0].Missing == 1 && FString(Crowd.Skipped[0].Reason) == TEXT("NoFreePoint"));
    }
    // A wave whose whole lifetime passed before field progress reached it is skipped, not placed already expired.
    {
        FLDCharmState Late = MakeState();
        FLDCharmAdvance Jump;
        Late.Advance(50000, INDEX_NONE, Jump);
        TestTrue(TEXT("An overdue wave is skipped"), Late.Active.IsEmpty() && Jump.Skipped.Num() == 1
            && FString(Jump.Skipped[0].Reason) == TEXT("Overdue") && Late.WavesDone == 1);
    }
    // Rest entry removes every unclaimed charm; the waves are finite.
    State.Advance(80000, INDEX_NONE, Step);
    TArray<FLDCharmInstance> Left;
    State.Clear(Left);
    TestTrue(TEXT("Rest entry removes what lies"), Left.Num() == 2 && State.Active.IsEmpty());
    State.Advance(140000, INDEX_NONE, Step);
    State.Advance(1000000, INDEX_NONE, Step);
    TestTrue(TEXT("After the last wave nothing more lands"), State.WavesDone == 3 && State.LastSerial == 6
        && State.NextDueFieldMs(1000000) == MAX_int64);
    TestEqual(TEXT("Max-share is floor(max * bp / 10000) without overflow"), LDCharms::ShareP2(MAX_int64, 2000),
        MAX_int64 / 10000 * 2000 + MAX_int64 % 10000 * 2000 / 10000);
    TestEqual(TEXT("Twenty percent of 620.00 HP is 124.00"), LDCharms::ShareP2(62000, 2000), int64(12400));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCharmsDrawTest, "Lockdown.Rules.DemoCharms.Draw",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCharmsDrawTest::RunTest(const FString& Parameters)
{
    // Two of three kinds without replacement, uniformly: every ordered pair appears, none repeats a kind.
    TMap<int32, int32> Pairs;
    bool bDistinct = true;
    for (uint64 Seed = 1; Seed <= 6000; ++Seed)
    {
        TArray<int32> Out;
        LDCharms::DrawDistinct(LDAirdrop::Mix(Seed), LDCharms::KindCount, 2, Out);
        bDistinct &= Out.Num() == 2 && Out[0] != Out[1] && Out[0] >= 0 && Out[0] < 3 && Out[1] >= 0 && Out[1] < 3;
        if (!bDistinct) { break; }
        ++Pairs.FindOrAdd(Out[0] * 3 + Out[1]);
    }
    TestTrue(TEXT("A draw never repeats a kind"), bDistinct);
    bool bEven = Pairs.Num() == 6;
    for (const TPair<int32, int32>& Pair : Pairs) { bEven &= Pair.Value > 800 && Pair.Value < 1200; }
    TestTrue(TEXT("All six ordered pairs appear about equally often"), bDistinct && bEven);
    TArray<int32> Out;
    LDCharms::DrawDistinct(7, 2, 5, Out);
    TestTrue(TEXT("A draw takes at most N values"), Out.Num() == 2 && Out[0] != Out[1]);
    LDCharms::DrawDistinct(7, 0, 2, Out);
    TestTrue(TEXT("No values from an empty pool"), Out.IsEmpty());
    return true;
}
#endif
