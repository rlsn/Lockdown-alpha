#include "Framework/LDShowdownRing.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/LDNativeMotionTypes.h"
#include "Misc/AutomationTest.h"

// The final showdown's street closure (SHOWDOWN.Ring*, CONTENT_AND_BALANCE §3): a linear shrink, its two damage rates and
// the real outside time of an authoritative foot path. Named (not anonymous) so unity builds cannot collide with another
// test file's helpers.
namespace LDShowdownRingTesting
{
// The approved shape with a 60 s closure: 1800 cm to 600 cm, 5 % then 12 % max HP per second, 0.25 s ticks.
FLDShowdownRing Ring(int64 DurationMs = 60000)
{
    FLDShowdownRing R;
    R.InitialRadiusCm = 1800;
    R.FinalRadiusCm = 600;
    R.DurationMs = DurationMs;
    R.TickMs = 250;
    R.PreviewMs = 10000;
    R.EarlyDamageBp = 500;
    R.LateDamageBp = 1200;
    return R;
}

// A ring that stays at 1800 cm for the first seconds (its shrink is far below a double's resolution there), so a crossing
// lies exactly where the foot meets 1800 cm.
FLDShowdownRing StillRing()
{
    FLDShowdownRing R = Ring(1000000000);
    R.FinalRadiusCm = 1800 - 1e-9;
    return R;
}

FLDNativeMotionRecord Move(uint64 Sequence, const FVector& From, const FVector& To, double StartSeconds, double EndSeconds)
{
    FLDNativeMotionRecord Record;
    Record.Sequence = Sequence;
    Record.Kind = ELDNativeMotionRecordKind::Movement;
    Record.FromFootCm = From;
    Record.ToFootCm = To;
    Record.StartServerSeconds = StartSeconds;
    Record.EndServerSeconds = EndSeconds;
    return Record;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDShowdownRingTest, "Lockdown.Rules.DemoShowdown.Ring",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDShowdownRingTest::RunTest(const FString& Parameters)
{
    using namespace LDShowdownRingTesting;
    const FLDShowdownRing R = Ring();
    FString Error;
    bool bGood = TestTrue(TEXT("The approved shape is valid"), R.Validate(Error));
    const int64 Times[] = {-1, 0, 15000, 30000, 45000, 60000, 90000};
    const double Radii[] = {1800, 1800, 1500, 1200, 900, 600, 600};
    for (int32 Index = 0; Index < int32(UE_ARRAY_COUNT(Times)); ++Index)
    {
        bGood &= TestNearlyEqual(*FString::Printf(TEXT("Radius at %lld ms"), Times[Index]), LDShowdownRing::RadiusCm(R, Times[Index]),
            Radii[Index], 1e-6);
    }
    bGood &= TestEqual(TEXT("No damage before the closure"), LDShowdownRing::DamageBp(R, -1), int64(0));
    bGood &= TestEqual(TEXT("Early rate in the first half"), LDShowdownRing::DamageBp(R, 29999), int64(500));
    bGood &= TestEqual(TEXT("Late rate from the half"), LDShowdownRing::DamageBp(R, 30000), int64(1200));
    bGood &= TestEqual(TEXT("Next tick after 0"), LDShowdownRing::NextTickMs(R, 0), int64(250));
    bGood &= TestEqual(TEXT("Next tick after 249"), LDShowdownRing::NextTickMs(R, 249), int64(250));
    bGood &= TestEqual(TEXT("Next tick after 250"), LDShowdownRing::NextTickMs(R, 250), int64(500));

    // A whole closure outside, settled every tick: 100000 * (0.05 * 30 + 0.12 * 30) = 510000 P2.
    constexpr int64 MaxHpP2 = 100000;
    int64 Accumulator = 0;
    int64 Dealt = 0;
    bool bAccumulated = true;
    for (int64 At = 0; At < R.DurationMs; At = LDShowdownRing::NextTickMs(R, At))
    {
        bAccumulated &= LDShowdownRing::AccumulateDamage(R, MaxHpP2, At, LDShowdownRing::NextTickMs(R, At), Accumulator);
        Dealt += LDShowdownRing::TakeDamageP2(Accumulator);
    }
    bGood &= TestTrue(TEXT("Every tick accumulates"), bAccumulated);
    bGood &= TestEqual(TEXT("A whole closure outside deals the approved total"), Dealt, int64(510000));
    bGood &= TestEqual(TEXT("Nothing is left over"), Accumulator, int64(0));

    // Outside time across the half: 1 * (500 * 100 + 1200 * 50) = 110000 E7, kept below one P2.
    Accumulator = 0;
    bGood &= TestTrue(TEXT("A half-crossing interval accumulates"), LDShowdownRing::AccumulateDamage(R, 1, 29900, 30050, Accumulator));
    bGood &= TestEqual(TEXT("The interval is split at the rate boundary"), Accumulator, int64(110000));
    bGood &= TestEqual(TEXT("Less than one P2 pays nothing yet"), LDShowdownRing::TakeDamageP2(Accumulator), int64(0));
    bGood &= TestEqual(TEXT("The remainder is kept"), Accumulator, int64(110000));
    int64 Untouched = 7;
    bGood &= TestFalse(TEXT("A reversed interval is refused"), LDShowdownRing::AccumulateDamage(R, 1, 500, 400, Untouched));
    bGood &= TestFalse(TEXT("An overflowing product is refused"), LDShowdownRing::AccumulateDamage(R, MAX_int64 / 2, 0, 1000, Untouched));
    bGood &= TestEqual(TEXT("A refused accumulation changes nothing"), Untouched, int64(7));

    FLDShowdownRing Bad = R;
    Bad.FinalRadiusCm = 2000;
    bGood &= TestFalse(TEXT("A growing ring is refused"), Bad.Validate(Error));
    Bad = R;
    Bad.LateDamageBp = 0;
    bGood &= TestFalse(TEXT("A zero damage rate is refused"), Bad.Validate(Error));
    Bad = R;
    Bad.TickMs = 0;
    bGood &= TestFalse(TEXT("A zero tick is refused"), Bad.Validate(Error));
    return bGood;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDShowdownRingOutsideTimeTest, "Lockdown.Rules.DemoShowdown.RingOutsideTime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDShowdownRingOutsideTimeTest::RunTest(const FString& Parameters)
{
    using namespace LDShowdownRingTesting;
    using FInterval = TPair<int64, int64>;
    const FVector Centre(0, 0, 0);
    // The closure started at authority 100000 ms; record instants are authority seconds.
    const int64 Closure = 100000;
    const auto Seconds = [Closure](double ElapsedMs) { return (double(Closure) + ElapsedMs) / 1000.; };
    const auto Run = [&](const FLDShowdownRing& R, const FLDNativeMotionBatch& Batch, int64 From, int64 To, TArray<FInterval>& Out)
    {
        TArray<LDShowdownRing::FFootSegment> Path;
        return LDShowdownRing::FootPathFromMotion(Batch, Closure, From, To, Path) && LDShowdownRing::OutsideIntervals(R, Centre, Path, Out);
    };
    const FLDShowdownRing Still = StillRing();
    TArray<FInterval> Out;
    // Walking out across the 1800 cm edge in the middle of a 50 ms step: only the half after the crossing counts (the end
    // foot alone would charge all 50 ms).
    FLDNativeMotionBatch Out1;
    Out1.StartFootCm = FVector(1790, 0, 0);
    Out1.EndFootCm = FVector(1810, 0, 0);
    Out1.Records = {Move(1, FVector(1790, 0, 0), FVector(1810, 0, 0), Seconds(1000), Seconds(1050))};
    bool bGood = TestTrue(TEXT("Leaving mid-step counts from the crossing"), Run(Still, Out1, 1000, 1050, Out)
        && Out.Num() == 1 && Out[0] == FInterval(1025, 1050));
    // Walking back in mid-step: the part before the crossing counts (the end foot alone would charge nothing).
    FLDNativeMotionBatch In1;
    In1.StartFootCm = FVector(1830, 0, 0);
    In1.EndFootCm = FVector(1790, 0, 0);
    In1.Records = {Move(1, FVector(1830, 0, 0), FVector(1790, 0, 0), Seconds(2000), Seconds(2040))};
    bGood &= TestTrue(TEXT("Entering mid-step counts until the crossing"), Run(Still, In1, 2000, 2050, Out)
        && Out.Num() == 1 && Out[0] == FInterval(2000, 2030));
    // Standing still while the ring shrinks over the foot: a 45 s closure is 1000 cm at 30000 ms.
    FLDNativeMotionBatch Standing;
    Standing.StartFootCm = Standing.EndFootCm = FVector(0, 1000, 0);
    bGood &= TestTrue(TEXT("The shrinking edge passing a standing foot splits the step"), Run(Ring(45000), Standing, 29950, 30050, Out)
        && Out.Num() == 1 && Out[0] == FInterval(30000, 30050));
    // A crossing between whole ms falls into its event ms (rounded up); the boundary itself is inside.
    FLDNativeMotionBatch Edge;
    Edge.StartFootCm = FVector(1799.9, 0, 0);
    Edge.EndFootCm = FVector(1800.1, 0, 0);
    Edge.Records = {Move(1, FVector(1799.9, 0, 0), FVector(1800.1, 0, 0), Seconds(3000), Seconds(3001))};
    bGood &= TestTrue(TEXT("A crossing inside a ms counts from the next whole ms"), Run(Still, Edge, 3000, 3010, Out)
        && Out.Num() == 1 && Out[0] == FInterval(3001, 3010));
    FLDNativeMotionBatch OnEdge;
    OnEdge.StartFootCm = OnEdge.EndFootCm = FVector(1800, 0, 0);
    bGood &= TestTrue(TEXT("Standing exactly on the edge is inside"), Run(Still, OnEdge, 4000, 4050, Out) && Out.IsEmpty());
    // Damage of the split interval: 25 ms at 5 % of 1000 HP per second, in P2 x 1e7.
    int64 Accumulator = 0;
    TArray<FInterval> Left;
    Run(Still, Out1, 1000, 1050, Left);
    for (const FInterval& Interval : Left) { LDShowdownRing::AccumulateDamage(Still, 100000, Interval.Key, Interval.Value, Accumulator); }
    bGood &= TestEqual(TEXT("Only the real outside time burns (25 ms, not the whole 50 ms step)"), Accumulator, int64(100000) * 500 * 25);
    // A record that does not continue the foot is refused, never guessed.
    FLDNativeMotionBatch Broken = Out1;
    Broken.Records[0].FromFootCm = FVector(0, 0, 0);
    TArray<LDShowdownRing::FFootSegment> Path;
    bGood &= TestFalse(TEXT("A discontinuous motion record is refused"), LDShowdownRing::FootPathFromMotion(Broken, Closure, 1000, 1050, Path));
    return bGood;
}

#endif
