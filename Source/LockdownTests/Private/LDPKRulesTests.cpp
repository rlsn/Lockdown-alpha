#include "PK/LDPKRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

// #198 session D: the pure PK rules (approved outer parameters, issue #198 comment 6752601175 §2-§4 and §6). The
// parameters below are the approved values, built here so the rules stay fast to check without the content fixture.
// Named (not anonymous) so unity builds cannot collide with another test file's helpers.
namespace LDPKRulesTesting
{
FLDPKParameters ApprovedPKParameters()
{
    FLDPKParameters P;
    P.ArenaRadiusCm = 1800;
    P.MidRadiusCm = 600;
    P.FinalRadiusCm = 150;
    P.ShrinkStartMs = 10000;
    P.MidMs = 40000;
    P.FinalMs = 55000;
    P.PreviewMs = 10000;
    P.MaxMs = 60000;
    P.TickMs = 250;
    P.EarlyDamageBp = 500;
    P.MidDamageBp = 1200;
    P.FinalDamageBp = 2500;
    P.TeamFormatBp = 5000;
    P.RewardMultiplierBp = 15000;
    P.ParticipationPoints = 1;
    P.WinnerPointsMax = 3;
    P.BettingPoolMaxPoints = 2;
    P.ThrowCooldownMs = 10000;
    P.ThrowTelegraphMs = 800;
    P.ThrowSlowBp = 2000;
    P.ThrowSlowMs = 1000;
    P.ThrowExposeMs = 2000;
    P.ThrowRadiusCm = 120;
    P.ThrowRangeCm = 4000;
    return P;
}

uint32 AnchorMask(const TArray<int32>& Anchors)
{
    uint32 Mask = 0;
    for (const int32 Anchor : Anchors) { Mask |= 1u << Anchor; }
    return Mask;
}

uint32 RotatedMask(int32 Rotation, int32 First, int32 Count)
{
    uint32 Mask = 0;
    for (int32 Index = 0; Index < Count; ++Index) { Mask |= 1u << ((Rotation + First + Index) % LDPKRules::SpawnAnchors); }
    return Mask;
}

// Anchors distinct and in range; teams on the two contiguous halves of one rotation; a duel opposite; a free-for-all on
// the approved spread of one rotation.
bool ValidSpawns(const FLDPKDraw& Draw)
{
    const int32 N = Draw.SideOfSeat.Num();
    if (Draw.AnchorOfSeat.Num() != N) { return false; }
    for (const int32 Anchor : Draw.AnchorOfSeat)
    {
        if (Anchor < 0 || Anchor >= LDPKRules::SpawnAnchors) { return false; }
    }
    if (FMath::CountBits(AnchorMask(Draw.AnchorOfSeat)) != N) { return false; }
    if (Draw.Format == ELDPKFormat::Teams)
    {
        TArray<int32> Side1;
        TArray<int32> Side2;
        for (int32 Seat = 0; Seat < N; ++Seat) { (Draw.SideOfSeat[Seat] == 1 ? Side1 : Side2).Add(Draw.AnchorOfSeat[Seat]); }
        for (int32 Rotation = 0; Rotation < LDPKRules::SpawnAnchors; ++Rotation)
        {
            if (AnchorMask(Side1) == RotatedMask(Rotation, 0, N / 2) && AnchorMask(Side2) == RotatedMask(Rotation, 3, N / 2))
            {
                return true;
            }
        }
        return false;
    }
    TArray<int32> Offsets;
    switch (N)
    {
    case 2: Offsets = {0, 3}; break;
    case 3: Offsets = {0, 2, 4}; break;
    case 4: Offsets = {0, 1, 3, 4}; break;
    default: for (int32 Index = 0; Index < N; ++Index) { Offsets.Add(Index); } break;
    }
    for (int32 Seat = 0; Seat < N; ++Seat)
    {
        if (Draw.SideOfSeat[Seat] != Seat + 1) { return false; }
    }
    for (int32 Rotation = 0; Rotation < LDPKRules::SpawnAnchors; ++Rotation)
    {
        uint32 Expected = 0;
        for (const int32 Offset : Offsets) { Expected |= 1u << ((Rotation + Offset) % LDPKRules::SpawnAnchors); }
        if (AnchorMask(Draw.AnchorOfSeat) == Expected) { return true; }
    }
    return false;
}

int64 TotalPoints(const TArray<FLDPKAward>& Awards)
{
    int64 Total = 0;
    for (const FLDPKAward& Award : Awards) { Total += Award.Participation + Award.Winner + Award.Bet; }
    return Total;
}

const FLDPKAward* SeatAward(const TArray<FLDPKAward>& Awards, int32 Seat)
{
    return Awards.FindByPredicate([Seat](const FLDPKAward& A) { return A.Seat == Seat && A.Bettor == INDEX_NONE; });
}

int64 SeatWinner(const TArray<FLDPKAward>& Awards, int32 Seat)
{
    const FLDPKAward* Award = SeatAward(Awards, Seat);
    return Award ? Award->Winner : -1;
}

const FLDPKAward* BettorAward(const TArray<FLDPKAward>& Awards, int32 Bettor)
{
    return Awards.FindByPredicate([Bettor](const FLDPKAward& A) { return A.Bettor == Bettor && A.Seat == INDEX_NONE; });
}

// A six-seat room with N entrants: every entrant eligible, every spectator present and betting on side 1, side 1 wins.
int64 BestCaseTotal(FAutomationTestBase& Test, const FLDPKParameters& P, int32 N, bool bTeams, uint32 Serial)
{
    TArray<uint8> Sides;
    for (int32 Seat = 0; Seat < N; ++Seat) { Sides.Add(bTeams ? uint8(Seat % 2 + 1) : uint8(Seat + 1)); }
    TArray<bool> Eligible;
    Eligible.Init(true, N);
    TArray<uint8> Bets;
    Bets.Init(uint8(1), 6 - N);
    TArray<bool> Present;
    Present.Init(true, 6 - N);
    TArray<FLDPKAward> Awards;
    FString Error;
    Test.TestTrue(*FString::Printf(TEXT("N=%d awards compute: %s"), N, *Error),
        LDPKRules::ComputeAwards(P, ELDPKOutcome::Won, 1, Serial, Sides, Eligible, Bets, Present, Awards, Error));
    return TotalPoints(Awards);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDPKFormatDrawTest, "Lockdown.Rules.DemoPK.FormatDraw",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDPKFormatDrawTest::RunTest(const FString& Parameters)
{
    using namespace LDPKRulesTesting;
    const FLDPKParameters P = ApprovedPKParameters();
    FString Error;
    TestTrue(TEXT("The approved PK parameters validate: ") + Error, P.Validate(Error));

    FRandomStream Random(198);
    ELDPKFormat Format = ELDPKFormat::Duel;
    const ELDPKFormat Fixed[] = {ELDPKFormat::None, ELDPKFormat::None, ELDPKFormat::Duel, ELDPKFormat::FreeForAll,
        ELDPKFormat::None, ELDPKFormat::FreeForAll};
    for (int32 N = 0; N <= 6; ++N)
    {
        const bool bDrawn = LDPKRules::DrawFormat(N, P.TeamFormatBp, Random, Format);
        TestTrue(*FString::Printf(TEXT("N=%d draws a format"), N), bDrawn);
        if (N == 4 || N == 6)
        {
            TestTrue(*FString::Printf(TEXT("N=%d is free-for-all or teams"), N),
                Format == ELDPKFormat::FreeForAll || Format == ELDPKFormat::Teams);
        }
        else
        {
            TestTrue(*FString::Printf(TEXT("N=%d has its fixed format"), N), Format == Fixed[N]);
        }
    }
    TestFalse(TEXT("Seven entrants are refused"), LDPKRules::DrawFormat(7, P.TeamFormatBp, Random, Format));
    TestFalse(TEXT("A probability above 100% is refused"), LDPKRules::DrawFormat(4, 10001, Random, Format));
    TestTrue(TEXT("0 bp never draws teams"), LDPKRules::DrawFormat(6, 0, Random, Format) && Format == ELDPKFormat::FreeForAll);
    TestTrue(TEXT("10000 bp always draws teams"), LDPKRules::DrawFormat(4, 10000, Random, Format) && Format == ELDPKFormat::Teams);

    FLDPKDraw Draw;
    TestFalse(TEXT("A PK draw needs two entrants"), LDPKRules::Draw(1, P, Random, Draw, Error));

    TArray<TArray<uint8>> Partitions;
    LDPKRules::EqualPartitions(4, Partitions);
    TestEqual(TEXT("4 entrants have 3 equal partitions"), Partitions.Num(), 3);
    LDPKRules::EqualPartitions(6, Partitions);
    TestEqual(TEXT("6 entrants have 10 equal partitions"), Partitions.Num(), 10);
    LDPKRules::EqualPartitions(5, Partitions);
    TestEqual(TEXT("Odd counts have no equal partition"), Partitions.Num(), 0);

    constexpr int32 Draws = 4000;
    for (const int32 N : {4, 6})
    {
        LDPKRules::EqualPartitions(N, Partitions);
        TArray<int32> PartitionCounts;
        PartitionCounts.Init(0, Partitions.Num());
        int32 Teams = 0;
        bool bAllValid = true;
        for (int32 Index = 0; Index < Draws; ++Index)
        {
            if (!LDPKRules::Draw(N, P, Random, Draw, Error) || !ValidSpawns(Draw)) { bAllValid = false; continue; }
            if (Draw.Format != ELDPKFormat::Teams) { bAllValid &= Draw.SideCount == N; continue; }
            ++Teams;
            bAllValid &= Draw.SideCount == 2 && Draw.SideOfSeat[0] == 1;
            const int32 Partition = Partitions.IndexOfByKey(Draw.SideOfSeat);
            if (Partition == INDEX_NONE) { bAllValid = false; continue; }
            ++PartitionCounts[Partition];
        }
        TestTrue(*FString::Printf(TEXT("N=%d draws are well formed with valid spawns: %s"), N, *Error), bAllValid);
        const double Share = double(Teams) / Draws;
        TestTrue(*FString::Printf(TEXT("N=%d team share %.3f is near 50%%"), N, Share), Share > 0.45 && Share < 0.55);
        const double Mean = double(Teams) / Partitions.Num();
        for (int32 Partition = 0; Partition < Partitions.Num(); ++Partition)
        {
            TestTrue(*FString::Printf(TEXT("N=%d partition %d drawn %d times near %.0f"), N, Partition, PartitionCounts[Partition], Mean),
                FMath::Abs(PartitionCounts[Partition] - Mean) < Mean * 0.3);
        }
    }
    for (const int32 N : {2, 3, 5})
    {
        bool bAllValid = true;
        for (int32 Index = 0; Index < 200; ++Index)
        {
            bAllValid &= LDPKRules::Draw(N, P, Random, Draw, Error) && Draw.SideCount == N && ValidSpawns(Draw);
        }
        TestTrue(*FString::Printf(TEXT("N=%d draws keep every entrant its own side on the approved spread"), N), bAllValid);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDPKOutcomeTest, "Lockdown.Rules.DemoPK.Outcome",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDPKOutcomeTest::RunTest(const FString& Parameters)
{
    using namespace LDPKRulesTesting;
    uint8 Winner = 0;
    const TArray<uint8> FreeForAll = {1, 2, 3};
    TestTrue(TEXT("Two sides alive keep running"),
        LDPKRules::Judge(FreeForAll, {true, true, false}, false, Winner) == ELDPKOutcome::Running && Winner == 0);
    TestTrue(TEXT("The last side alive wins"),
        LDPKRules::Judge(FreeForAll, {false, true, false}, false, Winner) == ELDPKOutcome::Won && Winner == 2);
    TestTrue(TEXT("The last side alive wins at the deadline too"),
        LDPKRules::Judge(FreeForAll, {false, false, true}, true, Winner) == ELDPKOutcome::Won && Winner == 3);
    TestTrue(TEXT("A simultaneous wipe is a draw"),
        LDPKRules::Judge(FreeForAll, {false, false, false}, false, Winner) == ELDPKOutcome::Draw && Winner == 0);
    TestTrue(TEXT("The deadline with two sides alive is a draw"),
        LDPKRules::Judge(FreeForAll, {true, false, true}, true, Winner) == ELDPKOutcome::Draw && Winner == 0);

    const TArray<uint8> Teams = {1, 2, 1, 2};
    TestTrue(TEXT("A team with one survivor is still alive"),
        LDPKRules::Judge(Teams, {false, true, true, false}, false, Winner) == ELDPKOutcome::Running);
    TestTrue(TEXT("A team with one survivor wins when the other is gone"),
        LDPKRules::Judge(Teams, {false, true, false, false}, false, Winner) == ELDPKOutcome::Won && Winner == 2);
    TestTrue(TEXT("Malformed input is no outcome"),
        LDPKRules::Judge(Teams, {true, true}, false, Winner) == ELDPKOutcome::None);
    TestTrue(TEXT("Side 0 is no outcome"),
        LDPKRules::Judge({0, 1}, {true, true}, false, Winner) == ELDPKOutcome::None);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDPKRingTest, "Lockdown.Rules.DemoPK.Ring",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDPKRingTest::RunTest(const FString& Parameters)
{
    using namespace LDPKRulesTesting;
    const FLDPKParameters P = ApprovedPKParameters();
    const int64 Times[] = {0, 10000, 25000, 40000, 47500, 55000, 60000};
    const double Radii[] = {1800, 1800, 1200, 600, 375, 150, 150};
    for (int32 Index = 0; Index < int32(UE_ARRAY_COUNT(Times)); ++Index)
    {
        TestNearlyEqual(*FString::Printf(TEXT("Radius at %lld ms"), Times[Index]), LDPKRules::RingRadiusCm(P, Times[Index]),
            Radii[Index], 1e-6);
    }

    const FVector Centre(100, -200, 50);
    TestTrue(TEXT("The boundary is inside"), LDPKRules::IsInsideRing(P, Centre, Centre + FVector(1800, 0, 300), 0));
    TestFalse(TEXT("Past the boundary is outside"), LDPKRules::IsInsideRing(P, Centre, Centre + FVector(1800.01, 0, 0), 0));
    TestTrue(TEXT("The shrunk boundary is inside"), LDPKRules::IsInsideRing(P, Centre, Centre + FVector(0, -1200, 0), 25000));
    TestFalse(TEXT("The shrunk ring excludes the old edge"), LDPKRules::IsInsideRing(P, Centre, Centre + FVector(0, 1300, 0), 25000));

    double Target = 0;
    int64 TargetAt = 0;
    TestTrue(TEXT("The first shrink is previewed ahead"), LDPKRules::RingPreview(P, 0, Target, TargetAt) && Target == 600 && TargetAt == 40000);
    TestTrue(TEXT("The second shrink is shown while it runs"), LDPKRules::RingPreview(P, 45000, Target, TargetAt) && Target == 150 && TargetAt == 55000);
    TestFalse(TEXT("The final ring has no preview"), LDPKRules::RingPreview(P, 56000, Target, TargetAt));

    TestEqual(TEXT("Early rate"), LDPKRules::RingDamageBp(P, 39999), int64(500));
    TestEqual(TEXT("Mid rate from 40 s"), LDPKRules::RingDamageBp(P, 40000), int64(1200));
    TestEqual(TEXT("Mid rate before 55 s"), LDPKRules::RingDamageBp(P, 54999), int64(1200));
    TestEqual(TEXT("Final rate from 55 s"), LDPKRules::RingDamageBp(P, 55000), int64(2500));

    TestEqual(TEXT("Next tick after 0"), LDPKRules::NextRingTickMs(P, 0), int64(250));
    TestEqual(TEXT("Next tick after 249"), LDPKRules::NextRingTickMs(P, 249), int64(250));
    TestEqual(TEXT("Next tick after 250"), LDPKRules::NextRingTickMs(P, 250), int64(500));

    // A full PK outside, settled every tick: 100000 * (0.05 * 40 + 0.12 * 15 + 0.25 * 5) = 505000 P2.
    constexpr int64 MaxHpP2 = 100000;
    int64 Accumulator = 0;
    int64 Dealt = 0;
    bool bAccumulated = true;
    for (int64 At = 0; At < P.MaxMs; At = LDPKRules::NextRingTickMs(P, At))
    {
        bAccumulated &= LDPKRules::AccumulateRingDamage(P, MaxHpP2, At, LDPKRules::NextRingTickMs(P, At), Accumulator);
        Dealt += LDPKRules::TakeRingDamageP2(Accumulator);
    }
    TestTrue(TEXT("Every tick accumulates"), bAccumulated);
    TestEqual(TEXT("A full PK outside deals the approved total"), Dealt, int64(505000));
    TestEqual(TEXT("Nothing is left over"), Accumulator, int64(0));

    // Partial outside time across the 40 s boundary: 1 * (500 * 100 + 1200 * 50) = 110000 E7, kept below one P2.
    Accumulator = 0;
    TestTrue(TEXT("A boundary-crossing interval accumulates"), LDPKRules::AccumulateRingDamage(P, 1, 39900, 40050, Accumulator));
    TestEqual(TEXT("The interval is split at the rate boundary"), Accumulator, int64(110000));
    TestEqual(TEXT("Less than one P2 pays nothing yet"), LDPKRules::TakeRingDamageP2(Accumulator), int64(0));
    TestEqual(TEXT("The remainder is kept"), Accumulator, int64(110000));
    Accumulator = 0;
    TestTrue(TEXT("A partial tick accumulates"), LDPKRules::AccumulateRingDamage(P, 100001, 0, 100, Accumulator));
    TestEqual(TEXT("The whole P2 part is taken"), LDPKRules::TakeRingDamageP2(Accumulator), int64(500));
    TestEqual(TEXT("The fraction stays for the next settlement"), Accumulator, int64(50000));

    int64 Untouched = 7;
    TestFalse(TEXT("A reversed interval is refused"), LDPKRules::AccumulateRingDamage(P, 1, 500, 400, Untouched));
    TestFalse(TEXT("An overflowing product is refused"), LDPKRules::AccumulateRingDamage(P, MAX_int64 / 2, 0, 1000, Untouched));
    TestEqual(TEXT("A refused accumulation changes nothing"), Untouched, int64(7));

    FLDPKParameters Bad = P;
    Bad.MidRadiusCm = 2000;
    FString Error;
    TestFalse(TEXT("A growing ring is refused"), Bad.Validate(Error));
    Bad = P;
    Bad.FinalMs = P.MaxMs;
    TestFalse(TEXT("Ring nodes must end before the PK maximum"), Bad.Validate(Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDPKAwardsTest, "Lockdown.Rules.DemoPK.Awards",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDPKAwardsTest::RunTest(const FString& Parameters)
{
    using namespace LDPKRulesTesting;
    const FLDPKParameters P = ApprovedPKParameters();
    FString Error;

    const int64 FreeForAllMax[] = {0, 0, 8, 11, 12, 15, 14};
    int64 RoomMax = 0;
    for (int32 N = 2; N <= 6; ++N)
    {
        const int64 Total = BestCaseTotal(*this, P, N, false, 1);
        TestEqual(*FString::Printf(TEXT("N=%d best case total"), N), Total, FreeForAllMax[N]);
        RoomMax = FMath::Max(RoomMax, Total);
    }
    TestEqual(TEXT("2v2 best case total"), BestCaseTotal(*this, P, 4, true, 1), int64(12));
    TestEqual(TEXT("3v3 best case total"), BestCaseTotal(*this, P, 6, true, 1), int64(14));
    TestTrue(TEXT("Four PKs of a six-seat room never exceed 60 points"), RoomMax * 4 <= 60);
    TestEqual(TEXT("Four five-entrant PKs reach exactly 60"), BestCaseTotal(*this, P, 5, false, 1) + BestCaseTotal(*this, P, 5, false, 2)
        + BestCaseTotal(*this, P, 5, false, 3) + BestCaseTotal(*this, P, 5, false, 4), int64(60));

    TArray<FLDPKAward> Awards;
    // Free-for-all of six: the three-point base pool scales to five after ceiling.
    TArray<uint8> Six = {1, 2, 3, 4, 5, 6};
    TArray<bool> SixEligible;
    SixEligible.Init(true, 6);
    TestTrue(TEXT("Six-way free-for-all awards"), LDPKRules::ComputeAwards(P, ELDPKOutcome::Won, 4, 1, Six, SixEligible, {}, {}, Awards, Error));
    const FLDPKAward* Winner = SeatAward(Awards, 3);
    TestTrue(TEXT("The single winner gets the five-point scaled pool"), Winner && Winner->Winner == 5 && Winner->Participation == 1);
    FLDPKParameters Capped = P;
    Capped.WinnerPointsMax = 2;
    LDPKRules::ComputeAwards(Capped, ELDPKOutcome::Won, 4, 1, Six, SixEligible, {}, {}, Awards, Error);
    Winner = SeatAward(Awards, 3);
    TestTrue(TEXT("The scaled winner pool follows the content base cap"), Winner && Winner->Winner == 3);

    // 3v3: a five-point winner pool rotates the remainder among present winners.
    TestTrue(TEXT("3v3 awards"), LDPKRules::ComputeAwards(P, ELDPKOutcome::Won, 2, 1, {1, 2, 1, 2, 1, 2}, SixEligible, {}, {}, Awards, Error));
    TestTrue(TEXT("3v3 winner remainder rotates by seat"), SeatWinner(Awards, 1) == 2 && SeatWinner(Awards, 3) == 2 && SeatWinner(Awards, 5) == 1
        && SeatWinner(Awards, 0) == 0);
    // 2v2: a three-point winner pool rotates; a forfeited winner gets nothing.
    TestTrue(TEXT("2v2 awards"), LDPKRules::ComputeAwards(P, ELDPKOutcome::Won, 1, 1, {1, 2, 2, 1}, {true, true, true, true}, {}, {}, Awards, Error));
    TestTrue(TEXT("2v2 winner pool remainder rotates"), SeatWinner(Awards, 0) == 2 && SeatWinner(Awards, 3) == 1);
    TestTrue(TEXT("2v2 with a forfeited winner"), LDPKRules::ComputeAwards(P, ELDPKOutcome::Won, 1, 1, {1, 2, 2, 1}, {true, true, true, false}, {}, {}, Awards, Error));
    TestTrue(TEXT("The forfeited winner gets nothing"), SeatAward(Awards, 3) == nullptr);
    TestTrue(TEXT("The present teammate takes the scaled winner pool"), SeatWinner(Awards, 0) == 3);
    TestTrue(TEXT("Duel with a forfeited loser"), LDPKRules::ComputeAwards(P, ELDPKOutcome::Won, 1, 1, {1, 2}, {true, false}, {}, {}, Awards, Error));
    TestTrue(TEXT("The forfeited loser gets no participation"), SeatAward(Awards, 1) == nullptr && TotalPoints(Awards) == 4);

    // A three-point betting pool over four correct bettors rotates the remainder with the PK serial.
    TArray<int64> Split;
    const int64 Expected[4][4] = {{1, 1, 1, 0}, {0, 1, 1, 1}, {1, 0, 1, 1}, {1, 1, 0, 1}};
    for (uint32 Serial = 1; Serial <= 4; ++Serial)
    {
        const TArray<uint8> Bets = {2, 2, 2, 2};
        TestTrue(TEXT("Three-way PK with four bettors"), LDPKRules::ComputeAwards(P, ELDPKOutcome::Won, 2, Serial, {1, 2, 3},
            {true, true, true}, Bets, {true, true, true, true}, Awards, Error));
        for (int32 Bettor = 0; Bettor < 4; ++Bettor)
        {
            const FLDPKAward* Row = BettorAward(Awards, Bettor);
            TestEqual(*FString::Printf(TEXT("Serial %u bettor %d"), Serial, Bettor), Row ? Row->Bet : int64(0), Expected[(Serial - 1) % 4][Bettor]);
        }
        LDPKRules::SplitWithRotation(3, 4, Serial, Split);
        TestTrue(*FString::Printf(TEXT("Serial %u split"), Serial), Split.Num() == 4 && Split[0] == Expected[(Serial - 1) % 4][0]
            && Split[1] == Expected[(Serial - 1) % 4][1] && Split[2] == Expected[(Serial - 1) % 4][2]
            && Split[3] == Expected[(Serial - 1) % 4][3]);
    }
    TestTrue(TEXT("Wrong, absent and abstaining bettors"), LDPKRules::ComputeAwards(P, ELDPKOutcome::Won, 1, 1, {1, 2},
        {true, true}, {1, 2, 0, 1}, {true, true, true, false}, Awards, Error));
    TestTrue(TEXT("Only the present correct bettor takes the scaled pool of 3"), BettorAward(Awards, 0) && BettorAward(Awards, 0)->Bet == 3
        && !BettorAward(Awards, 1) && !BettorAward(Awards, 2) && !BettorAward(Awards, 3));

    TestTrue(TEXT("Draw awards"), LDPKRules::ComputeAwards(P, ELDPKOutcome::Draw, 0, 1, {1, 2, 3}, {true, true, false}, {1, 2, 3},
        {true, true, true}, Awards, Error));
    const FLDPKAward* DrawSeat0 = SeatAward(Awards, 0);
    const FLDPKAward* DrawSeat1 = SeatAward(Awards, 1);
    TestTrue(TEXT("A draw rounds and rotates the eligible participation pool"), Awards.Num() == 2 && TotalPoints(Awards) == 3
        && DrawSeat0 && DrawSeat0->Participation == 2 && DrawSeat1 && DrawSeat1->Participation == 1 && SeatAward(Awards, 2) == nullptr);
    TestTrue(TEXT("Participation remainder rotates on the next serial"), LDPKRules::ComputeAwards(P, ELDPKOutcome::Draw, 0, 2,
        {1, 2, 3}, {true, true, false}, {}, {}, Awards, Error));
    DrawSeat0 = SeatAward(Awards, 0);
    DrawSeat1 = SeatAward(Awards, 1);
    TestTrue(TEXT("Participation remainder rotates on the next serial"), DrawSeat0 && DrawSeat0->Participation == 1
        && DrawSeat1 && DrawSeat1->Participation == 2);
    TestTrue(TEXT("No-contest awards"), LDPKRules::ComputeAwards(P, ELDPKOutcome::NoContest, 0, 1, {1, 2}, {true, true}, {1}, {true}, Awards, Error));
    TestTrue(TEXT("A no-contest pays nothing"), Awards.IsEmpty());

    TestFalse(TEXT("A running PK has no awards"), LDPKRules::ComputeAwards(P, ELDPKOutcome::Running, 0, 1, {1, 2}, {true, true}, {}, {}, Awards, Error));
    TestFalse(TEXT("A bet on a missing side is refused"), LDPKRules::ComputeAwards(P, ELDPKOutcome::Won, 1, 1, {1, 2}, {true, true}, {3}, {true}, Awards, Error));
    TestFalse(TEXT("A winner side that does not exist is refused"), LDPKRules::ComputeAwards(P, ELDPKOutcome::Won, 3, 1, {1, 2}, {true, true}, {}, {}, Awards, Error));
    FLDPKParameters InvalidMultiplier = P;
    InvalidMultiplier.RewardMultiplierBp = 9999;
    TestFalse(TEXT("A sub-100% reward multiplier is refused"),
        LDPKRules::ComputeAwards(InvalidMultiplier, ELDPKOutcome::Draw, 0, 1, {1, 2}, {true, true}, {}, {}, Awards, Error));
    FLDPKParameters Boundary = P;
    Boundary.ParticipationPoints = 64;
    Boundary.RewardMultiplierBp = 40000;
    TestFalse(TEXT("A 256-point participation award is refused instead of clamped to 255"),
        LDPKRules::ComputeAwards(Boundary, ELDPKOutcome::Draw, 0, 1, {1, 2}, {true, true}, {}, {}, Awards, Error));
    TestTrue(TEXT("Overflow exposes no partially payable rows and explains the refusal"), Awards.IsEmpty() && !Error.IsEmpty());
    Boundary.RewardMultiplierBp = 39843;
    TestTrue(TEXT("The representable 255-point participation boundary is preserved"),
        LDPKRules::ComputeAwards(Boundary, ELDPKOutcome::Draw, 0, 1, {1, 2}, {true, true}, {}, {}, Awards, Error));
    TestTrue(TEXT("Both participants receive the full representable award"),
        Awards.Num() == 2 && Awards[0].Participation == 255 && Awards[1].Participation == 255);
    Boundary.ParticipationPoints = 0;
    Boundary.RewardMultiplierBp = 2560000;
    TestFalse(TEXT("A 256-point winner pool cannot silently truncate"),
        LDPKRules::ComputeAwards(Boundary, ELDPKOutcome::Won, 1, 1, {1, 2}, {true, true}, {}, {}, Awards, Error));
    TestTrue(TEXT("Winner overflow returns no payable rows"), Awards.IsEmpty() && !Error.IsEmpty());
    Boundary.WinnerPointsMax = 0;
    Boundary.ParticipationPoints = 1;
    Boundary.RewardMultiplierBp = 1280000;
    TestFalse(TEXT("A late 256-point betting overflow with valid participant rows is atomic"),
        LDPKRules::ComputeAwards(Boundary, ELDPKOutcome::Won, 1, 1, {1, 2}, {true, true}, {1, 2}, {true, true}, Awards, Error));
    TestTrue(TEXT("Betting overflow returns no partially payable participants"), Awards.IsEmpty() && !Error.IsEmpty());
    return true;
}

#endif
