#include "Development/LDProgressionRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Development/LDDemoContentCatalog.h"
#include "Development/LDGrowthProtocol.h"
#include "Misc/AutomationTest.h"
#include "LDTestApprovedFixture.h"

// Field rewards (spec §4.2/§4.4, outer parameters §5): pure LDProgression rules over the approved catalog. A build part is an
// active blueprint or one card three-choice (spec §6.4).
namespace
{
bool FieldTestReason(FAutomationTestBase& Test, const FString& What, ELDGrowthReason Actual, ELDGrowthReason Expected)
{
    return Test.TestEqual(What, LDGrowthWire::ReasonName(Actual), LDGrowthWire::ReasonName(Expected));
}

bool FieldTestRunDraw(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDTestApprovedFixture::Read(Test, Catalog)) { return false; }
    const FLDDemoContentDefinitions& Content = Catalog->Get();
    bool bGood = true;

    FLDGrowthBuild Build = LDProgression::StartBuild(Content);
    Build.Level = 5;
    Build.Actives.Add({FName(*Content.Abilities[0].Id), 0});
    Build.PendingBlueprints.Add(FName(*Content.Abilities[1].Id));
    LDProgression::NormalizeBuild(Build);

    // BuildDraw: both classes appear across seeds, never a blueprint already held, and the same seed draws the same reward.
    int32 Blueprints = 0, CardOffers = 0;
    for (uint64 Seed = 1; Seed <= 64; ++Seed)
    {
        LDProgression::FFieldGrant Grant, Again;
        bGood &= FieldTestReason(Test, TEXT("BuildDraw draws"), LDProgression::DrawFieldReward(Content, Build,
            ELDFieldRewardKind::BuildDraw, Seed, Grant), ELDGrowthReason::None);
        LDProgression::DrawFieldReward(Content, Build, ELDFieldRewardKind::BuildDraw, Seed, Again);
        bGood &= Test.TestTrue(TEXT("BuildDraw is fixed by its seed"), Grant.Result == Again.Result && Grant.Definition == Again.Definition);
        if (Grant.Result == ELDFieldRewardResult::Blueprint)
        {
            ++Blueprints;
            bGood &= Test.TestTrue(TEXT("A drawn blueprint is neither learned nor pending"),
                !Build.FindActive(Grant.Definition) && !Build.HasBlueprint(Grant.Definition));
        }
        else
        {
            ++CardOffers;
            bGood &= Test.TestTrue(TEXT("Otherwise one card three-choice"), Grant.Result == ELDFieldRewardResult::CardOffer
                && Grant.Definition.IsNone());
        }
    }
    bGood &= Test.TestTrue(TEXT("Both halves of the 50/50 class roll occur over 64 seeds"), Blueprints > 8 && CardOffers > 8);

    // No eligible active: the build reward is always the card offer and the high-tier blueprint is held.
    FLDGrowthBuild Exhausted = Build;
    for (const FLDDemoAbilityDefinition& Ability : Content.Abilities)
    {
        const FName Id(*Ability.Id);
        if (!Exhausted.FindActive(Id) && !Exhausted.HasBlueprint(Id)) { Exhausted.PendingBlueprints.Add(Id); }
    }
    LDProgression::NormalizeBuild(Exhausted);
    bool bOnlyCards = true;
    for (uint64 Seed = 1; Seed <= 16; ++Seed)
    {
        LDProgression::FFieldGrant Grant;
        LDProgression::DrawFieldReward(Content, Exhausted, ELDFieldRewardKind::BuildDraw, Seed, Grant);
        bOnlyCards &= Grant.Result == ELDFieldRewardResult::CardOffer;
    }
    bGood &= Test.TestTrue(TEXT("With no eligible active the build reward is always a card offer"), bOnlyCards);
    LDProgression::FFieldGrant Held;
    LDProgression::DrawFieldReward(Content, Exhausted, ELDFieldRewardKind::Blueprint, 7, Held);
    bGood &= Test.TestTrue(TEXT("A high-tier blueprint with no eligible active is held"), Held.Result == ELDFieldRewardResult::BlueprintHeld);
    LDProgression::FFieldGrant Elite;
    LDProgression::DrawFieldReward(Content, Exhausted, ELDFieldRewardKind::CardOffer, 7, Elite);
    bGood &= Test.TestTrue(TEXT("The elite camp marker always gives a card offer"), Elite.Result == ELDFieldRewardResult::CardOffer);

    // A held blueprint is granted once an active becomes eligible again.
    FLDGrowthBuild Freed = Exhausted;
    FLDProgressionState Progression;
    LDProgression::FFieldGrant HeldGrant;
    HeldGrant.Result = ELDFieldRewardResult::BlueprintHeld;
    bGood &= FieldTestReason(Test, TEXT("A held blueprint installs"), LDProgression::ApplyFieldGrant(Content, HeldGrant,
        TEXT("camp_marker_high"), 1, 0, Freed, Progression), ELDGrowthReason::None);
    bGood &= Test.TestTrue(TEXT("Nothing is released while no active is eligible"),
        !LDProgression::ReleaseHeldBlueprints(Content, FGuid(1, 2, 3, 4), FGuid(5, 6, 7, 8), Freed, Progression)
        && Progression.HeldBlueprints == 1 && Progression.FieldRewards.Num() == 1);
    const FName Released = Freed.Actives[0].Id;
    Freed.Actives.Reset();
    bGood &= Test.TestTrue(TEXT("Forgetting an active releases the held blueprint as that active"),
        LDProgression::ReleaseHeldBlueprints(Content, FGuid(1, 2, 3, 4), FGuid(5, 6, 7, 8), Freed, Progression)
        && Progression.HeldBlueprints == 0 && Freed.HasBlueprint(Released));
    return bGood;
}

bool FieldTestRunCardOffer(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDTestApprovedFixture::Read(Test, Catalog)) { return false; }
    const FLDDemoContentDefinitions& Content = Catalog->Get();
    FLDGrowthBuild Build = LDProgression::StartBuild(Content);
    Build.Level = 3;
    FLDProgressionState Progression;
    LDProgression::FFieldGrant Grant;
    Grant.Result = ELDFieldRewardResult::CardOffer;
    const int32 QueuedBefore = Progression.CardQueue.Num();
    bool bGood = FieldTestReason(Test, TEXT("The elite card offer is granted"), LDProgression::ApplyFieldGrant(Content, Grant,
        TEXT("camp_marker_elite"), 2, 0, Build, Progression), ELDGrowthReason::None);
    bGood &= Test.TestTrue(TEXT("One field-reward card offer is queued and the reward recorded"),
        Progression.CardQueue.Num() == QueuedBefore + 1 && Progression.CardQueue.Last() == ELDCardOfferSource::FieldReward
        && Progression.CardOffersEarned == 1 && Progression.FieldRewards.Num() == 1
        && Progression.FieldRewards[0].Result == ELDFieldRewardResult::CardOffer && Build.Cards.IsEmpty());
    LDProgression::FFieldGrant Empty;
    bGood &= FieldTestReason(Test, TEXT("A grant without a result is refused"), LDProgression::ApplyFieldGrant(Content, Empty,
        TEXT("airdrop"), 3, 0, Build, Progression), ELDGrowthReason::DataInvalid);
    const TPair<ELDGrowthReason, uint16> Numbers[] = {{ELDGrowthReason::FieldRewardDuplicate, 167}, {ELDGrowthReason::FinalLocked, 170},
        {ELDGrowthReason::NoFinalMod, 171}, {ELDGrowthReason::FinalModUnavailable, 172}, {ELDGrowthReason::NoCardOffer, 190}};
    for (const TPair<ELDGrowthReason, uint16>& Row : Numbers)
    {
        bGood &= Test.TestTrue(FString::Printf(TEXT("%s travels as %u"), LDGrowthWire::ReasonName(Row.Key), uint32(Row.Value)),
            LDGrowthWire::Raw(Row.Key) == Row.Value && FString(LDGrowthWire::ReasonName(Row.Key)) != TEXT("Unknown"));
    }
    return bGood;
}
}

// §4.2: the Boss/airdrop build reward's 50/50 class roll and its fallback, the high-tier blueprint (held when the pool is
// empty) and the elite card offer.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoFieldRewardDrawTest, "Lockdown.Rules.DemoFieldReward.Draw",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoFieldRewardDrawTest::RunTest(const FString& Parameters)
{
    return FieldTestRunDraw(*this);
}

// Spec §6.4: a field card offer joins the card offer queue and is recorded.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoFieldRewardCardOfferTest, "Lockdown.Rules.DemoFieldReward.CardOffer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoFieldRewardCardOfferTest::RunTest(const FString& Parameters)
{
    return FieldTestRunCardOffer(*this);
}

#endif
