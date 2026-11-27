#include "Misc/AutomationTest.h"
#include "World/LDAirdrop.h"
#include "World/LDFieldEcology.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDFieldEcologyDeploymentTest, "Lockdown.Rules.FieldEcology.JointDeployment",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDFieldEcologyDeploymentTest::RunTest(const FString& Parameters)
{
    TArray<FVector> Candidates;
    for (int32 X = 0; X < 5; ++X)
    {
        for (int32 Y = 0; Y < 5; ++Y) { Candidates.Emplace(X * 1350., Y * 1350., 0.); }
    }
    TArray<TOptional<FVector>> Previous;
    Previous.SetNum(6);
    FRandomStream Match(334), Return(335), OtherMatch(354);
    TArray<int32> Opening, NextSegment, Other;
    if (!TestTrue(TEXT("The opening places all six"), LDFieldEcology::ChooseFieldSpawns(Candidates, Previous, 2400., Match, Opening)))
    {
        return false;
    }
    TestTrue(TEXT("A different match can pick a different deployment"),
        LDFieldEcology::ChooseFieldSpawns(Candidates, Previous, 2400., OtherMatch, Other) && Other != Opening);
    for (int32 Seat = 0; Seat < Previous.Num(); ++Seat) { Previous[Seat] = Candidates[Opening[Seat]]; }
    if (!TestTrue(TEXT("The next segment places all six again"),
        LDFieldEcology::ChooseFieldSpawns(Candidates, Previous, 2400., Return, NextSegment))) { return false; }

    for (const TArray<int32>* Deployment : { &Opening, &NextSegment })
    {
        for (int32 A = 0; A < Deployment->Num(); ++A)
        {
            for (int32 B = A + 1; B < Deployment->Num(); ++B)
            {
                TestTrue(TEXT("The initial feet are at least 2400 cm apart"),
                    FVector::Dist2D(Candidates[(*Deployment)[A]], Candidates[(*Deployment)[B]]) >= 2400.);
            }
        }
    }
    for (int32 Seat = 0; Seat < Previous.Num(); ++Seat)
    {
        TestNotEqual(TEXT("A seat gets a new point after rest"), NextSegment[Seat], Opening[Seat]);
    }
    TestFalse(TEXT("An impossible deployment yields no partial assignments"),
        LDFieldEcology::ChooseFieldSpawns(MakeArrayView(Candidates.GetData(), 5), Previous, 2400., Return, NextSegment));
    TestTrue(TEXT("The failed plan is empty"), NextSegment.IsEmpty());

    // A random draw that misses narrow ground falls back to a fixed lattice over the same field bounds.
    TArray<FVector2D> Grid, Again;
    const FBox2D Field(FVector2D(-1000., 0.), FVector2D(1700., 1800.));
    LDFieldEcology::GridPoints(Field, 900., Grid);
    LDFieldEcology::GridPoints(Field, 900., Again);
    TestEqual(TEXT("The sweep covers the field at one point per 9 m cell"), Grid.Num(), 6);
    TestTrue(TEXT("The sweep is deterministic"), Grid == Again);
    TestTrue(TEXT("The sweep starts half a cell inside the field"), Grid.Num() > 0 && Grid[0].Equals(FVector2D(-550., 450.)));
    TestFalse(TEXT("Every sweep point is inside the field and 9 m from its neighbours"),
        Grid.ContainsByPredicate([&Field](const FVector2D& Point) { return !Field.IsInsideOrOn(Point); })
        || (Grid.Num() > 1 && !FMath::IsNearlyEqual(FVector2D::Distance(Grid[0], Grid[1]), 900.)));
    LDFieldEcology::GridPoints(Field, 0., Grid);
    TestTrue(TEXT("A bad spacing sweeps nothing"), Grid.IsEmpty());
    LDFieldEcology::GridPoints(FBox2D(ForceInit), 900., Grid);
    TestTrue(TEXT("An empty field sweeps nothing"), Grid.IsEmpty());

    // Farthest-point picking strands at five from every start of this layout, yet six of its points keep 2400 cm apart.
    const TArray<FVector> Irregular{{2280., 2890., 0.}, {780., 1560., 0.}, {4050., 3190., 0.}, {5190., 3100., 0.},
        {70., 3420., 0.}, {950., 4670., 0.}, {2610., 1860., 0.}, {2410., 4360., 0.}, {4810., 610., 0.}, {1940., 640., 0.},
        {10., 710., 0.}, {5990., 1270., 0.}};
    TArray<TOptional<FVector>> Fresh;
    Fresh.SetNum(6);
    for (int32 Seed : {1, 334, 356})
    {
        FRandomStream Stream(Seed);
        TArray<int32> Found;
        if (!TestTrue(TEXT("An irregular layout with a legal deployment places all six"),
            LDFieldEcology::ChooseFieldSpawns(Irregular, Fresh, 2400., Stream, Found) && Found.Num() == 6)) { return false; }
        for (int32 A = 0; A < Found.Num(); ++A)
        {
            for (int32 B = A + 1; B < Found.Num(); ++B)
            {
                TestTrue(TEXT("The irregular deployment keeps 2400 cm apart"),
                    FVector::Dist2D(Irregular[Found[A]], Irregular[Found[B]]) >= 2400.);
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDFieldEcologyAirdropTest, "Lockdown.Rules.FieldEcology.AnnouncedPlacement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDFieldEcologyAirdropTest::RunTest(const FString& Parameters)
{
    FLDAirdropState State;
    State.Schedule.LandMs = {40000, 110000};
    State.Schedule.AnnounceLeadMs = 15000;
    State.Schedule.ExpireMs = 30000;
    State.Schedule.Seed = 334;
    State.Schedule.PickupRadiusCm = 150.;
    int32 Samples = 0;
    const FLDAirdropState::FSelectPoint Sample = [&](uint64 Serial, const LDAirdrop::FPoint* Previous, LDAirdrop::FPoint& Out)
    {
        ++Samples;
        if (Previous) { TestEqual(TEXT("The next sample receives the last landed position"), Previous->PositionCm, FVector(2000., 3000., 0.)); }
        Out = {TEXT("sampled_field_point"), FVector(2000. * Serial, 3000., 0.)};
        return true;
    };
    TestFalse(TEXT("No point is sampled before its announcement"), State.Advance(24999, Sample));
    TestTrue(TEXT("The first announcement works with an empty fallback pool"), State.Advance(25000, Sample));
    TestEqual(TEXT("The sampled point is announced"), State.View().PositionCm, FVector(2000., 3000., 0.));
    TestTrue(TEXT("Landing preserves the announced position"), State.Advance(40000, Sample));
    TestEqual(TEXT("Landing never redraws"), Samples, 1);
    TestTrue(TEXT("The next announcement updates the point"), State.Advance(95000, Sample));
    TestEqual(TEXT("The second sample is published"), State.View().PositionCm, FVector(4000., 3000., 0.));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDFieldEcologyRecoveryTest, "Lockdown.Rules.FieldEcology.RecoveryEligibility",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDFieldEcologyRecoveryTest::RunTest(const FString& Parameters)
{
    using LDFieldEcology::NeedsRecovery;
    TestFalse(TEXT("Full health and momentum leave the pack on the ground"), NeedsRecovery(100000, 100000, 10000, 10000));
    TestTrue(TEXT("A health deficit can take it"), NeedsRecovery(95000, 100000, 10000, 10000));
    TestTrue(TEXT("A momentum deficit can take it even with full health"), NeedsRecovery(100000, 100000, 9500, 10000));
    TestFalse(TEXT("A dead body cannot take it"), NeedsRecovery(0, 100000, 9500, 10000));
    return true;
}
