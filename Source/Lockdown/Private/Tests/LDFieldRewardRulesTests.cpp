#include "Development/LDProgressionRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Development/LDDemoContentCatalog.h"
#include "Development/LDGrowthProtocol.h"
#include "Misc/AutomationTest.h"
#include "Tests/LDTestApprovedFixture.h"

// #198 G field rewards (spec §4.2/§4.4, outer parameters §5) and the rest material services (spec §4.5): pure LDProgression
// rules over the approved catalog.
namespace
{
bool FieldTestReason(FAutomationTestBase& Test, const FString& What, ELDGrowthReason Actual, ELDGrowthReason Expected)
{
    return Test.TestEqual(What, LDGrowthWire::ReasonName(Actual), LDGrowthWire::ReasonName(Expected));
}

LDProgression::FBuildContext FieldTestContext(LDProgression::EMatchStage Stage)
{
    LDProgression::FBuildContext Context;
    Context.Stage = Stage;
    Context.LifePhase = ELDLifePhase::Alive;
    return Context;
}

void FieldTestOwn(FLDGrowthBuild& Build, FName Id)
{
    FLDComponentInstance& Instance = Build.Components.AddDefaulted_GetRef();
    Instance.Id = Id;
    Instance.Source = ELDComponentSource::BuildReward;
    Instance.SourceLevel = 1;
    LDProgression::NormalizeBuild(Build);
}

void FieldTestIds(const FLDDemoContentDefinitions& Content, TFunctionRef<bool(const FLDDemoComponentDefinition&)> Filter,
    TArray<FName>& Out)
{
    for (const FLDDemoComponentDefinition& Row : Content.Components)
    {
        if (Filter(Row)) { Out.Add(FName(*Row.Id)); }
    }
}

bool FieldTestRunDraw(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDTestApprovedFixture::Read(Test, Catalog)) { return false; }
    const FLDDemoContentDefinitions& Content = Catalog->Get();
    bool bGood = true;
    TArray<FName> Scarce, Common;
    FieldTestIds(Content, [](const FLDDemoComponentDefinition& Row) { return Row.Pool == ELDDemoComponentPool::Scarce; }, Scarce);
    FieldTestIds(Content, [](const FLDDemoComponentDefinition& Row) { return Row.Pool == ELDDemoComponentPool::Common; }, Common);
    bGood &= Test.TestTrue(TEXT("The approved scarce pool is M11, M12 and C12"), Scarce.Num() == 3);

    FLDGrowthBuild Build = LDProgression::StartBuild(Content);
    Build.Level = 5;
    Build.Actives.Add({FName(*Content.Abilities[0].Id), 0});
    Build.PendingBlueprints.Add(FName(*Content.Abilities[1].Id));
    FieldTestOwn(Build, Scarce[0]);
    LDProgression::NormalizeBuild(Build);

    // BuildDraw: both classes appear across seeds, never anything already held, and the same seed draws the same reward.
    int32 Blueprints = 0, ScarceGrants = 0;
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
            ++ScarceGrants;
            bGood &= Test.TestTrue(TEXT("Otherwise a scarce component not owned"), Grant.Result == ELDFieldRewardResult::ScarceComponent
                && Scarce.Contains(Grant.Definition) && !Build.FindComponent(Grant.Definition));
        }
    }
    bGood &= Test.TestTrue(TEXT("Both halves of the 50/50 class roll occur over 64 seeds"), Blueprints > 8 && ScarceGrants > 8);

    // One class empty: the other one always pays.
    FLDGrowthBuild AllScarce = Build;
    for (const FName& Id : Scarce) { if (!AllScarce.FindComponent(Id)) { FieldTestOwn(AllScarce, Id); } }
    bool bOnlyBlueprints = true;
    for (uint64 Seed = 1; Seed <= 16; ++Seed)
    {
        LDProgression::FFieldGrant Grant;
        LDProgression::DrawFieldReward(Content, AllScarce, ELDFieldRewardKind::BuildDraw, Seed, Grant);
        bOnlyBlueprints &= Grant.Result == ELDFieldRewardResult::Blueprint;
    }
    bGood &= Test.TestTrue(TEXT("With every scarce component owned the build reward is always a blueprint"), bOnlyBlueprints);

    // Both classes empty: one ordinary material, or the empty-pool points when the common pool is exhausted too.
    FLDGrowthBuild Exhausted = AllScarce;
    for (const FLDDemoAbilityDefinition& Ability : Content.Abilities)
    {
        const FName Id(*Ability.Id);
        if (!Exhausted.FindActive(Id) && !Exhausted.HasBlueprint(Id)) { Exhausted.PendingBlueprints.Add(Id); }
    }
    LDProgression::NormalizeBuild(Exhausted);
    LDProgression::FFieldGrant Fallback;
    LDProgression::DrawFieldReward(Content, Exhausted, ELDFieldRewardKind::BuildDraw, 7, Fallback);
    bGood &= Test.TestTrue(TEXT("Both classes empty: one common component"), Fallback.Result == ELDFieldRewardResult::CommonComponent
        && Common.Contains(Fallback.Definition));
    LDProgression::FFieldGrant Held;
    LDProgression::DrawFieldReward(Content, Exhausted, ELDFieldRewardKind::Blueprint, 7, Held);
    bGood &= Test.TestTrue(TEXT("A high-tier blueprint with no eligible active is held"), Held.Result == ELDFieldRewardResult::BlueprintHeld);
    for (const FName& Id : Common) { FieldTestOwn(Exhausted, Id); }
    LDProgression::DrawFieldReward(Content, Exhausted, ELDFieldRewardKind::BuildDraw, 7, Fallback);
    bGood &= Test.TestTrue(TEXT("Every pool empty: the empty-pool points"), Fallback.Result == ELDFieldRewardResult::PoolPoints);
    LDProgression::FFieldGrant NoChoice;
    LDProgression::DrawFieldReward(Content, Exhausted, ELDFieldRewardKind::ComponentChoice, 7, NoChoice);
    bGood &= Test.TestTrue(TEXT("An elite choice with no common component left pays the points"),
        NoChoice.Result == ELDFieldRewardResult::PoolPoints);

    // Elite choice: two distinct unowned commons; one when only one is left.
    LDProgression::FFieldGrant Choice;
    LDProgression::DrawFieldReward(Content, Build, ELDFieldRewardKind::ComponentChoice, 11, Choice);
    bGood &= Test.TestTrue(TEXT("The elite two-choice shows two distinct unowned common components"),
        Choice.Result == ELDFieldRewardResult::ComponentChoice && Choice.ChoiceOptions.Num() == 2
        && Choice.ChoiceOptions[0] != Choice.ChoiceOptions[1] && Common.Contains(Choice.ChoiceOptions[0])
        && Common.Contains(Choice.ChoiceOptions[1]));
    FLDGrowthBuild OneLeft = Build;
    for (int32 Index = 1; Index < Common.Num(); ++Index) { FieldTestOwn(OneLeft, Common[Index]); }
    LDProgression::DrawFieldReward(Content, OneLeft, ELDFieldRewardKind::ComponentChoice, 11, Choice);
    bGood &= Test.TestTrue(TEXT("Only one common left: the choice honestly shows one"),
        Choice.ChoiceOptions.Num() == 1 && Choice.ChoiceOptions[0] == Common[0]);

    // A held blueprint is granted once an active becomes eligible again.
    FLDGrowthBuild Freed = AllScarce;
    for (const FLDDemoAbilityDefinition& Ability : Content.Abilities)
    {
        const FName Id(*Ability.Id);
        if (!Freed.FindActive(Id) && !Freed.HasBlueprint(Id)) { Freed.PendingBlueprints.Add(Id); }
    }
    FLDProgressionState Progression;
    LDProgression::FFieldGrant HeldGrant;
    HeldGrant.Result = ELDFieldRewardResult::BlueprintHeld;
    bGood &= FieldTestReason(Test, TEXT("A held blueprint installs"), LDProgression::ApplyFieldGrant(Content, HeldGrant,
        TEXT("camp_marker_high"), 1, 0, FGuid(), Freed, Progression), ELDGrowthReason::None);
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

bool FieldTestRunServices(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDTestApprovedFixture::Read(Test, Catalog)) { return false; }
    const FLDDemoContentDefinitions& Content = Catalog->Get();
    bool bGood = Test.TestTrue(TEXT("Approved FillGap price 4 and dismantle input 2"),
        Content.Parameters.FillGapPoints == 4 && Content.Parameters.DismantleInputCount == 2);
    const LDProgression::FBuildContext Rest = FieldTestContext(LDProgression::EMatchStage::Rest);
    const LDProgression::FBuildContext Field = FieldTestContext(LDProgression::EMatchStage::Live);
    const FName FireSkin(TEXT("component_fire_skin")), HoldDown(TEXT("component_hold_down")), FistFire(TEXT("component_fist_fire"));
    const FName DirtyTrick(TEXT("component_dirty_trick")), TabLedger(TEXT("component_tab_ledger")), BigTab(TEXT("component_big_tab"));
    const FName SpikyHide(TEXT("component_spiky_hide")), DirtyFist(TEXT("component_dirty_fist"));

    // XP 600 is level 6: 3 opening + 5 * 3 = 18 points earned.
    FLDGrowthBuild Build = LDProgression::StartBuild(Content);
    Build.Level = 6;
    FieldTestOwn(Build, FireSkin);
    FieldTestOwn(Build, HoldDown);
    TArray<FName> Gaps, Fillers;
    LDProgression::GapTargets(Content, Build, Gaps);
    bGood &= Test.TestTrue(TEXT("C01 without a fire source and M04 without a hard control are gaps"),
        Gaps.Contains(FireSkin) && Gaps.Contains(HoldDown));
    LDProgression::GapFillers(Content, Build, FireSkin, Fillers);
    bGood &= Test.TestTrue(TEXT("C01's fillers are its unowned common bases (B01, B18), never the scarce C12"),
        Fillers.Contains(FistFire) && Fillers.Contains(DirtyTrick) && !Fillers.Contains(DirtyFist));
    LDProgression::GapFillers(Content, Build, HoldDown, Fillers);
    bGood &= Test.TestTrue(TEXT("M04 has no base filler: its entry is an active source"), Fillers.IsEmpty());

    FLDGrowthBuild Out;
    int64 Paid = 0;
    bGood &= FieldTestReason(Test, TEXT("FillGap outside the rest window"), LDProgression::ApplyFillGap(Content, Build, 600, 0,
        FireSkin, FistFire, Field, Out, Paid), ELDGrowthReason::NotRestWindow);
    bGood &= FieldTestReason(Test, TEXT("FillGap with a base that is no filler"), LDProgression::ApplyFillGap(Content, Build, 600, 0,
        FireSkin, TabLedger, Rest, Out, Paid), ELDGrowthReason::NoGapFiller);
    FLDGrowthBuild Poor = Build;
    Poor.PermanentSpentPoints = 15;
    bGood &= FieldTestReason(Test, TEXT("FillGap without the points"), LDProgression::ApplyFillGap(Content, Poor, 600, 0,
        FireSkin, FistFire, Rest, Out, Paid), ELDGrowthReason::NotEnoughPoints);
    bGood &= FieldTestReason(Test, TEXT("FillGap of C01 with B01"), LDProgression::ApplyFillGap(Content, Build, 600, 0,
        FireSkin, FistFire, Rest, Out, Paid), ELDGrowthReason::None);
    const FLDComponentInstance* Filled = Out.FindComponent(FistFire);
    bGood &= Test.TestTrue(TEXT("Payment, filler and the filled mark commit together"), Paid == 4 && Out.PermanentSpentPoints == 4
        && Filled && Filled->Source == ELDComponentSource::FillGap && Out.FindComponent(FireSkin)->bGapFilled);
    FLDGrowthBuild Again = Out;
    Again.Components.RemoveAll([&](const FLDComponentInstance& Row) { return Row.Id == FistFire; });
    bGood &= FieldTestReason(Test, TEXT("A filled target never fills again, even without its entry"), LDProgression::ApplyFillGap(
        Content, Again, 600, 0, FireSkin, FistFire, Rest, Out, Paid), ELDGrowthReason::GapAlreadyFilled);

    // Dismantle: two distinct owned commons for one unowned common; installs on a surrendered host come off.
    FLDGrowthBuild Shop = LDProgression::StartBuild(Content);
    Shop.Level = 6;
    for (const FName& Id : {TabLedger, BigTab, SpikyHide, FistFire, DirtyFist}) { FieldTestOwn(Shop, Id); }
    FLDComponentInstance* Host = Shop.FindComponent(TabLedger);
    Host->bInstalled = Host->bEverInstalled = true;
    FLDComponentInstance* Mod = Shop.FindComponent(BigTab);
    Mod->bInstalled = Mod->bEverInstalled = true;
    Mod->Target.Kind = ELDComponentTargetKind::Component;
    Mod->Target.Definition = TabLedger;
    bGood &= FieldTestReason(Test, TEXT("The fixture build is valid"), LDProgression::ValidateBuild(Content, Shop), ELDGrowthReason::None);
    LDProgression::FDismantleOutcome Outcome;
    bGood &= FieldTestReason(Test, TEXT("Dismantle outside the rest window"), LDProgression::ApplyDismantle(Content, Shop,
        {TabLedger, SpikyHide}, 3, Field, Out, Outcome), ELDGrowthReason::NotRestWindow);
    bGood &= FieldTestReason(Test, TEXT("A scarce component is not accepted"), LDProgression::ApplyDismantle(Content, Shop,
        {TabLedger, DirtyFist}, 3, Rest, Out, Outcome), ELDGrowthReason::DismantleInvalid);
    bGood &= FieldTestReason(Test, TEXT("The same component twice is not two"), LDProgression::ApplyDismantle(Content, Shop,
        {TabLedger, TabLedger}, 3, Rest, Out, Outcome), ELDGrowthReason::DismantleInvalid);
    bGood &= FieldTestReason(Test, TEXT("Three components are not the approved two"), LDProgression::ApplyDismantle(Content, Shop,
        {TabLedger, SpikyHide, FistFire}, 3, Rest, Out, Outcome), ELDGrowthReason::DismantleInvalid);
    bGood &= FieldTestReason(Test, TEXT("Dismantle two commons"), LDProgression::ApplyDismantle(Content, Shop,
        {TabLedger, SpikyHide}, 3, Rest, Out, Outcome), ELDGrowthReason::None);
    const FLDDemoComponentDefinition* Received = Content.Components.FindByPredicate(
        [&](const FLDDemoComponentDefinition& Row) { return FName(*Row.Id) == Outcome.Received; });
    bGood &= Test.TestTrue(TEXT("One unowned common component received, outside the surrendered ones"), Received
        && Received->Pool == ELDDemoComponentPool::Common && !Shop.FindComponent(Outcome.Received)
        && Out.FindComponent(Outcome.Received)->Source == ELDComponentSource::Dismantle);
    bGood &= Test.TestTrue(TEXT("The surrendered ones are gone and the mod on the host returned to the inventory"),
        !Out.FindComponent(TabLedger) && !Out.FindComponent(SpikyHide) && Out.FindComponent(FistFire)
        && Out.FindComponent(BigTab) && !Out.FindComponent(BigTab)->bInstalled && Outcome.Uninstalled == TArray<FName>{BigTab});
    bGood &= FieldTestReason(Test, TEXT("The result is a valid build"), LDProgression::ValidateBuild(Content, Out), ELDGrowthReason::None);
    LDProgression::FDismantleOutcome Same;
    FLDGrowthBuild SameOut;
    LDProgression::ApplyDismantle(Content, Shop, {TabLedger, SpikyHide}, 3, Rest, SameOut, Same);
    bGood &= Test.TestTrue(TEXT("The random result is fixed by its seed"), Same.Received == Outcome.Received);

    FLDGrowthBuild Full = LDProgression::StartBuild(Content);
    Full.Level = 6;
    for (const FLDDemoComponentDefinition& Row : Content.Components)
    {
        if (Row.Pool == ELDDemoComponentPool::Common) { FieldTestOwn(Full, FName(*Row.Id)); }
    }
    bGood &= FieldTestReason(Test, TEXT("An exhausted common pool refuses without removing anything"), LDProgression::ApplyDismantle(
        Content, Full, {TabLedger, SpikyHide}, 3, Rest, Out, Outcome), ELDGrowthReason::DismantlePoolEmpty);
    bGood &= Test.TestTrue(TEXT("Nothing changed"), Out.Components.Num() == Full.Components.Num());
    return bGood;
}

bool FieldTestRunReactionGap(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDTestApprovedFixture::Read(Test, Catalog)) { return false; }
    const FLDDemoContentDefinitions& Content = Catalog->Get();
    const FName BlastChase(TEXT("component_blast_chase"));
    const FName FistFire(TEXT("component_fist_fire")), FistWater(TEXT("component_fist_water")), FistShock(TEXT("component_fist_shock"));
    const FName DirtyTrick(TEXT("component_dirty_trick"));
    // seethe burns and quick_meeting burns in its zone (fire + fire); hose_down wets (water).
    const FName Seethe(TEXT("active_seethe")), QuickMeeting(TEXT("active_quick_meeting")), HoseDown(TEXT("active_hose_down"));
    FLDGrowthBuild FireFire = LDProgression::StartBuild(Content);
    FireFire.Level = 6;
    FireFire.Actives.Add({Seethe, 0});
    FireFire.PendingBlueprints.Add(QuickMeeting);
    FieldTestOwn(FireFire, BlastChase);
    FieldTestOwn(FireFire, FistFire);
    TArray<FName> Gaps, Fillers;
    LDProgression::GapTargets(Content, FireFire, Gaps);
    bool bGood = Test.TestTrue(TEXT("C09 with three fire entries (seethe, quick_meeting blueprint, B01) is still a gap: fire cannot react with fire"),
        Gaps.Contains(BlastChase));
    bGood &= FieldTestReason(Test, TEXT("C09 fire+fire fillers"), LDProgression::GapFillers(Content, FireFire, BlastChase, Fillers),
        ELDGrowthReason::None);
    bGood &= Test.TestTrue(TEXT("Its fillers are the bases adding a missing kind (B10 water, B11 electric, unbound B18), never fire"),
        Fillers.Contains(FistWater) && Fillers.Contains(FistShock) && Fillers.Contains(DirtyTrick) && !Fillers.Contains(FistFire));

    FLDGrowthBuild FireWater = LDProgression::StartBuild(Content);
    FireWater.Level = 6;
    FireWater.Actives.Add({Seethe, 0});
    FireWater.Actives.Add({HoseDown, 1});
    FieldTestOwn(FireWater, BlastChase);
    LDProgression::GapTargets(Content, FireWater, Gaps);
    bGood &= Test.TestFalse(TEXT("C09 with fire (seethe) and water (hose_down) can react: not a gap"), Gaps.Contains(BlastChase));
    bGood &= FieldTestReason(Test, TEXT("C09 fire+water is refused as a gap"), LDProgression::GapFillers(Content, FireWater, BlastChase,
        Fillers), ELDGrowthReason::NotAGap);
    return bGood;
}

bool FieldTestRunChoice(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDTestApprovedFixture::Read(Test, Catalog)) { return false; }
    const FLDDemoContentDefinitions& Content = Catalog->Get();
    const LDProgression::FBuildContext Field = FieldTestContext(LDProgression::EMatchStage::Live);
    const FName A(TEXT("component_fist_fire")), B(TEXT("component_tab_ledger"));
    FLDGrowthBuild Build = LDProgression::StartBuild(Content);
    Build.Level = 3;
    FLDProgressionState Progression;
    LDProgression::FFieldGrant Grant;
    Grant.Result = ELDFieldRewardResult::ComponentChoice;
    Grant.ChoiceOptions = {A, B};
    const FGuid ChoiceId(0x198, 0x6, 0x1, 0x2);
    bool bGood = FieldTestReason(Test, TEXT("The elite choice is granted pending"), LDProgression::ApplyFieldGrant(Content, Grant,
        TEXT("camp_marker_elite"), 2, 0, ChoiceId, Build, Progression), ELDGrowthReason::None);
    bGood &= Test.TestTrue(TEXT("Pending with its fixed options and recorded"), Progression.ComponentChoices.Num() == 1
        && Progression.ComponentChoices[0].Options == TArray<FName>{A, B} && Progression.FieldRewards.Num() == 1);
    FLDGrowthBuild Out;
    FLDProgressionState OutProgression;
    bool bPoints = false;
    bGood &= FieldTestReason(Test, TEXT("An unknown choice"), LDProgression::ApplyComponentChoice(Content, Build, Progression,
        FGuid(9, 9, 9, 9), A, Field, Out, OutProgression, bPoints), ELDGrowthReason::NoComponentChoice);
    bGood &= FieldTestReason(Test, TEXT("A component that is no option"), LDProgression::ApplyComponentChoice(Content, Build, Progression,
        ChoiceId, TEXT("component_spiky_hide"), Field, Out, OutProgression, bPoints), ELDGrowthReason::InvalidOption);
    bGood &= FieldTestReason(Test, TEXT("The points while an option is free"), LDProgression::ApplyComponentChoice(Content, Build,
        Progression, ChoiceId, NAME_None, Field, Out, OutProgression, bPoints), ELDGrowthReason::ComponentOwned);
    bGood &= FieldTestReason(Test, TEXT("Choosing B on the field"), LDProgression::ApplyComponentChoice(Content, Build, Progression,
        ChoiceId, B, Field, Out, OutProgression, bPoints), ELDGrowthReason::None);
    bGood &= Test.TestTrue(TEXT("B is owned from the elite choice and the choice is resolved"),
        Out.FindComponent(B) && Out.FindComponent(B)->Source == ELDComponentSource::EliteChoice && OutProgression.ComponentChoices.IsEmpty());
    FLDGrowthBuild Both = Build;
    FieldTestOwn(Both, A);
    FieldTestOwn(Both, B);
    bGood &= FieldTestReason(Test, TEXT("An owned option"), LDProgression::ApplyComponentChoice(Content, Both, Progression, ChoiceId, A,
        Field, Out, OutProgression, bPoints), ELDGrowthReason::ComponentOwned);
    bGood &= FieldTestReason(Test, TEXT("Every option owned: the points"), LDProgression::ApplyComponentChoice(Content, Both, Progression,
        ChoiceId, NAME_None, Field, Out, OutProgression, bPoints), ELDGrowthReason::None);
    bGood &= Test.TestTrue(TEXT("The empty-pool points are awarded"), bPoints
        && Out.AwardedPoints == Content.Parameters.EmptyComponentPoolPoints && OutProgression.ComponentChoices.IsEmpty());
    const TPair<ELDGrowthReason, uint16> Numbers[] = {{ELDGrowthReason::NoComponentChoice, 160}, {ELDGrowthReason::ComponentOwned, 161},
        {ELDGrowthReason::NotAGap, 162}, {ELDGrowthReason::NoGapFiller, 163}, {ELDGrowthReason::GapAlreadyFilled, 164},
        {ELDGrowthReason::DismantleInvalid, 165}, {ELDGrowthReason::DismantlePoolEmpty, 166},
        {ELDGrowthReason::FieldRewardDuplicate, 167}};
    for (const TPair<ELDGrowthReason, uint16>& Row : Numbers)
    {
        bGood &= Test.TestTrue(FString::Printf(TEXT("%s travels as %u"), LDGrowthWire::ReasonName(Row.Key), uint32(Row.Value)),
            LDGrowthWire::Raw(Row.Key) == Row.Value && FString(LDGrowthWire::ReasonName(Row.Key)) != TEXT("Unknown"));
    }
    bGood &= Test.TestTrue(TEXT("The new command kinds travel as 13, 24 and 25"),
        uint8(ELDBuildCommandKind::ComponentChoice) == 13 && uint8(ELDBuildCommandKind::FillGap) == 24
        && uint8(ELDBuildCommandKind::Dismantle) == 25);
    return bGood;
}
}

// #198 G §4.2: the Boss/airdrop build reward's 50/50 class roll and fallbacks, the high-tier blueprint (held when the pool
// is empty) and the elite two-choice draw.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoFieldRewardDrawTest, "Lockdown.Rules.DemoFieldReward.Draw",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoFieldRewardDrawTest::RunTest(const FString& Parameters)
{
    return FieldTestRunDraw(*this);
}

// #198 G §4.5: FillGap eligibility, price and once-only mark; Dismantle's three-for-one with dangling installs and refusals.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoFieldRewardServicesTest, "Lockdown.Rules.DemoFieldReward.Services",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoFieldRewardServicesTest::RunTest(const FString& Parameters)
{
    return FieldTestRunServices(*this);
}

// #198 G2 §4.5: C09 counts distinct element kinds among its held entries; fire+fire is a gap, fire+water is not.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoFieldRewardReactionGapTest, "Lockdown.Rules.DemoFieldReward.ReactionGap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoFieldRewardReactionGapTest::RunTest(const FString& Parameters)
{
    return FieldTestRunReactionGap(*this);
}

// #198 G: resolving an elite component choice, its refusals and its points fallback; the wire numbers of the new kinds.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoFieldRewardChoiceTest, "Lockdown.Rules.DemoFieldReward.Choice",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoFieldRewardChoiceTest::RunTest(const FString& Parameters)
{
    return FieldTestRunChoice(*this);
}

#endif
