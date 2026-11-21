#include "Combat/LDCombatSourceRules.h"
#include "Combat/LDMomentum.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDemoContentJson.h"
#include "Development/LDProgressionRules.h"
#include "Development/LDSkillGrowth.h"
#include "LDTestApprovedFixture.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Issue #287 spec §16 P1 rules on the pure momentum, combo and skill-rank functions and the approved catalog.

namespace LDMomentumRulesTests
{
bool Approved(FAutomationTestBase& Test, TSharedPtr<const FLDDemoContentSnapshot>& Out)
{
    return LDTestApprovedFixture::Read(Test, Out) && Out.IsValid();
}

LDProgression::FBuildContext LiveContext()
{
    LDProgression::FBuildContext Context;
    Context.Stage = LDProgression::EMatchStage::Live;
    Context.LifePhase = ELDLifePhase::Alive;
    return Context;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDMomentumIncomeTest, "Lockdown.Rules.Combat.Momentum.Income",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDMomentumIncomeTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDMomentumRulesTests::Approved(*this, Catalog)) { return false; }
    const FLDDemoMomentumParameters& P = Catalog->Get().Parameters.Momentum;
    TestTrue(TEXT("The approved incomes: basic 10 per 1.334 s, skill hit 5, reaction 10, perfect dodge 15, combat 5/s"),
        P.BasicPerRefP2 == 1000 && P.BasicRefIntervalMs == 1334 && P.SkillHitP2 == 500 && P.ReactionP2 == 1000
        && P.PerfectDodgeP2 == 1500 && P.CombatPerSec.Numerator == 5 * P.CombatPerSec.Denominator);
    TestEqual(TEXT("A basic at the reference interval gains the reference amount"), LDMomentum::BasicGainP2(P, 1334), int64(1000));
    // Momentum per second from basics does not depend on attack speed: faster swings gain less each.
    const double Reference = double(LDMomentum::BasicGainP2(P, 1334)) * 1000. / 1334.;
    for (const int64 IntervalMs : {200, 435, 667, 1000, 2000, 2667})
    {
        const double PerSecond = double(LDMomentum::BasicGainP2(P, IntervalMs)) * 1000. / double(IntervalMs);
        TestTrue(FString::Printf(TEXT("Basics every %lld ms gain %.2f/s, the same as at the reference"), IntervalMs, PerSecond / 100.),
            FMath::Abs(PerSecond - Reference) <= 1000. / double(IntervalMs));
    }
    TestEqual(TEXT("A non-positive interval gains nothing"), LDMomentum::BasicGainP2(P, 0), int64(0));
    int64 Applied = 0;
    TestEqual(TEXT("Income stops at the maximum"), LDMomentum::Apply(P, 9800, 500, Applied), P.MaxP2);
    TestEqual(TEXT("and reports what it really added"), Applied, int64(200));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDMomentumSpendTest, "Lockdown.Rules.Combat.Momentum.Spend",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDMomentumSpendTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDMomentumRulesTests::Approved(*this, Catalog)) { return false; }
    const FLDDemoMomentumParameters& P = Catalog->Get().Parameters.Momentum;
    FLDActiveGateInput Gate;
    Gate.NowMs = 1000;
    Gate.ReadyAtMs = 0;
    Gate.MomentumCostP2 = 3000;
    Gate.MomentumP2 = 2999;
    TestEqual(TEXT("Below the cost the release is refused as MomentumShort"),
        LDCombatSourceRules::ActiveGate(Gate, Catalog->Get().Policies), ELDCombatReject::MomentumShort);
    Gate.MomentumP2 = 3000;
    TestEqual(TEXT("At the cost it passes"), LDCombatSourceRules::ActiveGate(Gate, Catalog->Get().Policies), ELDCombatReject::None);
    int64 Applied = 0;
    TestEqual(TEXT("A payment never leaves the bar below zero"), LDMomentum::Apply(P, 2000, -3000, Applied), int64(0));
    TestEqual(TEXT("and pays only what was there"), Applied, int64(-2000));
    for (const FLDDemoAbilityDefinition& Ability : Catalog->Get().Abilities)
    {
        TestTrue(FString::Printf(TEXT("%s costs momentum and the costs never grow with level"), *Ability.Id),
            Ability.Values.MomentumCostP2 > 0 && Catalog->FindValues(Ability, 16, ELDSkillBranch::None)
            && Catalog->FindValues(Ability, 16, ELDSkillBranch::None)->MomentumCostP2 == Ability.Values.MomentumCostP2);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDMomentumIdleTest, "Lockdown.Rules.Combat.Momentum.Idle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDMomentumIdleTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDMomentumRulesTests::Approved(*this, Catalog)) { return false; }
    const FLDDemoMomentumParameters& P = Catalog->Get().Parameters.Momentum;
    const FLDDemoRational Rate = P.CombatPerSec;
    FString Error;
    int64 Bar = 4000, Remainder = 0, Combat = 0, Idle = 0;
    TestTrue(TEXT("One second in combat gains 5"), LDMomentum::Settle(P, Rate, TOptional<int64>(0), 0, 1000, Bar, Remainder, Combat, Idle, Error)
        && Bar == 4500 && Combat == 500 && Idle == 0);
    TestTrue(TEXT("The combat window is half-open: idle from IdleDelaySec after the last combat event"),
        LDMomentum::InCombat(P, TOptional<int64>(0), P.IdleDelayMs - 1) && !LDMomentum::InCombat(P, TOptional<int64>(0), P.IdleDelayMs)
        && !LDMomentum::InCombat(P, TOptional<int64>(), 0));
    Bar = 8000; Remainder = 0;
    TestTrue(TEXT("Idle above the start drifts down 10/s"), LDMomentum::Settle(P, Rate, TOptional<int64>(0), 10000, 11000, Bar, Remainder,
        Combat, Idle, Error) && Bar == 7000 && Idle == -1000);
    Bar = 2000; Remainder = 0;
    TestTrue(TEXT("Idle below the start drifts up 10/s"), LDMomentum::Settle(P, Rate, TOptional<int64>(), 10000, 11000, Bar, Remainder,
        Combat, Idle, Error) && Bar == 3000 && Idle == 1000);
    Bar = 5500; Remainder = 0;
    TestTrue(TEXT("The drift stops at the start value"), LDMomentum::Settle(P, Rate, TOptional<int64>(), 10000, 12000, Bar, Remainder,
        Combat, Idle, Error) && Bar == P.StartP2);
    Bar = 9900; Remainder = 0;
    TestTrue(TEXT("Combat gain stops at the maximum"), LDMomentum::Settle(P, Rate, TOptional<int64>(0), 0, 2000, Bar, Remainder,
        Combat, Idle, Error) && Bar == P.MaxP2);
    TestFalse(TEXT("A reversed interval is refused"), LDMomentum::Settle(P, Rate, TOptional<int64>(0), 2000, 1000, Bar, Remainder,
        Combat, Idle, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDComboStepsTest, "Lockdown.Rules.Combat.Combo.Steps",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDComboStepsTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDMomentumRulesTests::Approved(*this, Catalog)) { return false; }
    const FLDDemoComboParameters& P = Catalog->Get().Parameters.Combo;
    TestTrue(TEXT("The approved steps are 0.9, 0.9, 1.3 and the reset 1.5 s"),
        LDMomentum::ComboRatioBp(P, 1) == 9000 && LDMomentum::ComboRatioBp(P, 2) == 9000 && LDMomentum::ComboRatioBp(P, 3) == 13000
        && LDMomentum::ComboRatioBp(P, 0) == 10000 && P.ResetMs == 1500);
    uint8 Step = LDMomentum::NextComboStep(0, 0, 1000, P.ResetMs);
    TestEqual(TEXT("The first swing is step 1"), int32(Step), 1);
    Step = LDMomentum::NextComboStep(Step, 1000, 2000, P.ResetMs);
    TestEqual(TEXT("then 2"), int32(Step), 2);
    Step = LDMomentum::NextComboStep(Step, 2000, 3000, P.ResetMs);
    TestEqual(TEXT("then 3"), int32(Step), 3);
    Step = LDMomentum::NextComboStep(Step, 3000, 4000, P.ResetMs);
    TestEqual(TEXT("and around to 1"), int32(Step), 1);
    TestEqual(TEXT("At exactly the reset the combo goes on"), int32(LDMomentum::NextComboStep(2, 1000, 2500, P.ResetMs)), 3);
    TestEqual(TEXT("More than the reset after the previous start starts over"), int32(LDMomentum::NextComboStep(2, 1000, 2501, P.ResetMs)), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDOverdriveReadyTest, "Lockdown.Rules.Combat.Overdrive.Threshold",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDOverdriveReadyTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDMomentumRulesTests::Approved(*this, Catalog)) { return false; }
    const FLDDemoOverdriveParameters& O = Catalog->Get().Parameters.Overdrive;
    TestTrue(TEXT("Overdrive needs 60 momentum"), O.ThresholdP2 == 6000 && !LDMomentum::OverdriveReady(O, 5999)
        && LDMomentum::OverdriveReady(O, 6000));
    TestTrue(TEXT("The approved overdrive terms"), O.HoldMs == 300 && O.ScaleBp == 16000 && O.FireBurnStacks == 3 && O.WaterSlowBp == 4500
        && O.WaterSlowMs == 1500 && O.ShockReactionBp == 5000 && O.RotDurationBp == 20000);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDPerfectDodgeWindowTest, "Lockdown.Rules.Combat.PerfectDodge.Window",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDPerfectDodgeWindowTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDMomentumRulesTests::Approved(*this, Catalog)) { return false; }
    const int64 Window = Catalog->Get().Parameters.Momentum.PerfectDodgeWindowMs;
    TestEqual(TEXT("The window is 0.3 s"), Window, int64(300));
    TestTrue(TEXT("A dash started inside within the window that left the area dodges perfectly"),
        LDMomentum::IsPerfectDodge(Window, 1000, 1300, true, false));
    TestFalse(TEXT("One millisecond too early is not"), LDMomentum::IsPerfectDodge(Window, 1000, 1301, true, false));
    TestFalse(TEXT("A dash that started outside the area is not"), LDMomentum::IsPerfectDodge(Window, 1000, 1200, false, false));
    TestFalse(TEXT("A body still inside at the resolution is not"), LDMomentum::IsPerfectDodge(Window, 1000, 1200, true, true));
    TestFalse(TEXT("A dash after the resolution is not"), LDMomentum::IsPerfectDodge(Window, 1300, 1200, true, false));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDSkillRankCardHostTest, "Lockdown.Rules.Growth.SkillRank.CardHosts",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDSkillRankCardHostTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDMomentumRulesTests::Approved(*this, Catalog)) { return false; }
    const FLDDemoContentDefinitions& Content = Catalog->Get();
    using B = ELDSkillBranch;
    const FName Banned(TEXT("active_account_banned")), Slide(TEXT("active_apology_slide"));
    FLDGrowthBuild Build = LDProgression::StartBuild(Content);
    Build.Level = 8;
    Build.Actives.Add({Banned, 0, B::None});
    Build.Actives.Add({Slide, 1, B::None});
    FLDCardTarget OnBanned, OnSlide;
    OnBanned.Kind = OnSlide.Kind = ELDCardTargetKind::Skill;
    OnBanned.Active = Banned;
    OnSlide.Active = Slide;
    FLDCardInstance Reach;
    Reach.Serial = 1;
    Reach.Id = TEXT("card_long_reach");
    Reach.Target = OnBanned;
    Build.Cards.Add(Reach);
    Build.NextCardSerial = 2;
    const auto Reason = [&Content](const FLDGrowthBuild& On, const TCHAR* Card, const FLDCardTarget& Target)
        { return LDProgression::CardTargetReason(Content, On, FName(Card), Target); };
    // S04 at rank 1 is a targeted single-target silence: K10 fits, K07 does not.
    TestTrue(TEXT("Rank-1 S04 takes K10 and refuses K07"), Reason(Build, TEXT("card_long_reach"), OnBanned) == ELDGrowthReason::None
        && Reason(Build, TEXT("card_crowd_hit"), OnBanned) == ELDGrowthReason::InvalidTarget);
    const LDProgression::FBuildContext Context = LDMomentumRulesTests::LiveContext();
    FLDGrowthBuild Out;
    TestEqual(TEXT("S04 ranks up to 2B"), LDProgression::ApplyRankUpSkill(Content, Build, 0, B::B, Context, Out), ELDGrowthReason::None);
    TestTrue(TEXT("2B silences around the caster: K10 returns to the library and K07 now fits"), !Out.Cards[0].IsInstalled()
        && Reason(Out, TEXT("card_long_reach"), OnBanned) == ELDGrowthReason::InvalidTarget
        && Reason(Out, TEXT("card_crowd_hit"), OnBanned) == ELDGrowthReason::None);
    // S12 only slows at rank 1; its 2A turns the slow into a root, which K05/K11 take.
    TestEqual(TEXT("Rank-1 S12 refuses K05"), Reason(Out, TEXT("card_pin_amp"), OnSlide), ELDGrowthReason::InvalidTarget);
    Build = Out;
    TestEqual(TEXT("S12 ranks up to 2A"), LDProgression::ApplyRankUpSkill(Content, Build, 1, B::A, Context, Out), ELDGrowthReason::None);
    TestTrue(TEXT("2A's root takes K05 and K11"), Reason(Out, TEXT("card_pin_amp"), OnSlide) == ELDGrowthReason::None
        && Reason(Out, TEXT("card_hold_longer"), OnSlide) == ELDGrowthReason::None);
    // S14 stays a self form; its 2B adds a hit around the caster, which K07 widens.
    const FName Lid(TEXT("active_not_mad"));
    FLDCardTarget OnLid;
    OnLid.Kind = ELDCardTargetKind::Skill;
    OnLid.Active = Lid;
    Build = Out;
    Build.Level = 12;
    Build.Actives.Add({Lid, 2, B::None});
    TestEqual(TEXT("Rank-1 S14 only shields its caster and refuses K07"), Reason(Build, TEXT("card_crowd_hit"), OnLid),
        ELDGrowthReason::InvalidTarget);
    TestEqual(TEXT("S14 ranks up to 2B"), LDProgression::ApplyRankUpSkill(Content, Build, 2, B::B, Context, Out), ELDGrowthReason::None);
    TestEqual(TEXT("2B's area around the caster takes K07"), Reason(Out, TEXT("card_crowd_hit"), OnLid), ELDGrowthReason::None);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDSkillRankBranchTest, "Lockdown.Rules.Growth.SkillRank.Branch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDSkillRankBranchTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDMomentumRulesTests::Approved(*this, Catalog)) { return false; }
    const FLDDemoContentDefinitions& Content = Catalog->Get();
    using B = ELDSkillBranch;
    FLDGrowthBuild Build = LDProgression::StartBuild(Content);
    Build.Actives.Add({FName(TEXT("active_elbow")), 0, B::None});
    const LDProgression::FBuildContext Context = LDMomentumRulesTests::LiveContext();
    FLDGrowthBuild Out;
    TestEqual(TEXT("Level 1 has no skill point"), LDProgression::SkillPoints(Content, Build), 0);
    TestEqual(TEXT("so a rank-up is refused"), LDProgression::ApplyRankUpSkill(Content, Build, 0, B::A, Context, Out), ELDGrowthReason::NoSkillPoint);
    Build.Level = 3;
    TestEqual(TEXT("Level 3 gives the first point"), LDProgression::SkillPoints(Content, Build), 1);
    TestEqual(TEXT("A rank-3 path cannot skip rank 2"), LDProgression::ApplyRankUpSkill(Content, Build, 0, B::A1, Context, Out),
        ELDGrowthReason::BranchUnavailable);
    TestEqual(TEXT("An empty slot has nothing to raise"), LDProgression::ApplyRankUpSkill(Content, Build, 1, B::A, Context, Out),
        ELDGrowthReason::SlotEmpty);
    TestTrue(TEXT("Rank 2 takes A and spends the point"), LDProgression::ApplyRankUpSkill(Content, Build, 0, B::A, Context, Out)
        == ELDGrowthReason::None && Out.Actives[0].Branch == B::A && LDProgression::SkillPoints(Content, Out) == 0);
    Build = Out;
    Build.Level = 6;
    TestEqual(TEXT("Under A, rank 3 is A1 or A2, never B1"), LDProgression::ApplyRankUpSkill(Content, Build, 0, B::B1, Context, Out),
        ELDGrowthReason::BranchUnavailable);
    TestTrue(TEXT("Level 6's point takes A2"), LDProgression::ApplyRankUpSkill(Content, Build, 0, B::A2, Context, Out)
        == ELDGrowthReason::None && Out.Actives[0].Branch == B::A2);
    Build = Out;
    Build.Level = 9;
    TestEqual(TEXT("Rank 3 is the last"), LDProgression::ApplyRankUpSkill(Content, Build, 0, B::A1, Context, Out),
        ELDGrowthReason::BranchUnavailable);
    TestEqual(TEXT("Level 9's point stays unspent"), LDProgression::SkillPoints(Content, Build), 1);
    LDProgression::FBuildContext Closed = Context;
    Closed.Stage = LDProgression::EMatchStage::Closed;
    TestEqual(TEXT("A closed match refuses it"), LDProgression::ApplyRankUpSkill(Content, Build, 0, B::A1, Closed, Out),
        ELDGrowthReason::PhaseClosed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDSkillRankRefundTest, "Lockdown.Rules.Growth.SkillRank.Refund",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDSkillRankRefundTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDMomentumRulesTests::Approved(*this, Catalog)) { return false; }
    const FLDDemoContentDefinitions& Content = Catalog->Get();
    FLDGrowthBuild Build = LDProgression::StartBuild(Content);
    Build.Level = 9;
    Build.Actives.Add({FName(TEXT("active_elbow")), 0, ELDSkillBranch::B2});
    Build.Actives.Add({FName(TEXT("active_pants_down")), 1, ELDSkillBranch::None});
    TestEqual(TEXT("A rank-3 skill holds two of the three points"), LDProgression::SkillPoints(Content, Build), 1);
    FLDProgressionState Progression;
    LDProgression::FBuildContext Rest;
    Rest.Stage = LDProgression::EMatchStage::Rest;
    Rest.LifePhase = ELDLifePhase::Alive;
    FLDGrowthBuild Out;
    FName Forgotten;
    TArray<uint16> Reclaimed;
    if (!TestEqual(TEXT("The trainer forgets it"), LDProgression::ApplyForgetActive(Content, Build, Progression, 0, Rest, Out, Forgotten, Reclaimed),
        ELDGrowthReason::None)) { return false; }
    TestEqual(TEXT("Forgetting returns every point it spent"), LDProgression::SkillPoints(Content, Out), 3);
    TestTrue(TEXT("Its rank and branch are gone with it"), !Out.FindActive(FName(TEXT("active_elbow"))));
    TestEqual(TEXT("A build that spent more points than it earned is refused"),
        LDProgression::SkillPoints(Content, [&]{ FLDGrowthBuild Bad = Build; Bad.Level = 4; return Bad; }()), -1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDSkillRankPatchesTest, "Lockdown.Rules.Combat.SkillRank.Patches",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDSkillRankPatchesTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDMomentumRulesTests::Approved(*this, Catalog)) { return false; }
    const FLDDemoContentDefinitions& Content = Catalog->Get();
    const FLDDemoSkillRankParameters& Ranks = Content.Parameters.SkillRanks;
    const int64 Step = Content.Parameters.SkillGrowthPerLevelBp;
    for (const FLDDemoAbilityDefinition& Ability : Content.Abilities)
    {
        TestEqual(FString::Printf(TEXT("%s has the six branch variants"), *Ability.Id), Ability.Branches.Num(), 6);
        for (const FLDDemoAbilityBranch& Branch : Ability.Branches)
        {
            const uint8 Rank = LDSkillBranch::RankOf(Branch.Path);
            const FString Label = FString::Printf(TEXT("%s %s"), *Ability.Id, LDSkillBranch::StepKey(Branch.Path));
            // Every branch patch changes the level-1 definition, and a rank-3 variant keeps its rank-2 parent's changes.
            TestFalse(Label + TEXT(" changes the level-1 definition"),
                LDDemoContent::ExactStoredStruct(FLDDemoAbilityValues::StaticStruct(), &Branch.Values, &Ability.Values)
                && LDDemoContent::ExactStoredStruct(FLDDemoTargetDefinition::StaticStruct(), &Branch.Target, &Ability.Target));
            if (const FLDDemoAbilityBranch* Parent = Ability.FindBranch(LDSkillBranch::ParentOf(Branch.Path)))
            {
                TestTrue(Label + TEXT(" differs from its rank-2 parent"),
                    !LDDemoContent::ExactStoredStruct(FLDDemoAbilityValues::StaticStruct(), &Branch.Values, &Parent->Values)
                    || !LDDemoContent::ExactStoredStruct(FLDDemoTargetDefinition::StaticStruct(), &Branch.Target, &Parent->Target));
            }
            // The release snapshot of a branch is its variant grown by level and by its rank's scales.
            for (const uint8 Level : {uint8(1), uint8(9), uint8(16)})
            {
                FLDDemoAbilityValues Expected;
                const FLDDemoAbilityValues* Snapshot = Catalog->FindValues(Ability, Level, Branch.Path);
                TestTrue(FString::Printf(TEXT("%s at level %u is the variant grown for rank %u"), *Label, Level, Rank),
                    Snapshot && LDSkillGrowth::GrowValues(Branch.Values, Ranks, Step, Level, Rank, Expected)
                    && LDDemoContent::ExactStoredStruct(FLDDemoAbilityValues::StaticStruct(), Snapshot, &Expected));
            }
        }
    }
    // A branch's momentum amount (momentum_gain, momentum_steal, zone_momentum) never grows with level, rank or overdrive.
    int32 MomentumEffects = 0;
    for (const FLDDemoAbilityDefinition& Ability : Content.Abilities)
    {
        for (const FLDDemoAbilityBranch& Branch : Ability.Branches)
        {
            const FLDDemoAbilityValues* Grown = Catalog->FindValues(Ability, 16, Branch.Path);
            for (int32 Index = 0; Grown && Index < Branch.Values.Effects.Num(); ++Index)
            {
                const ELDDemoEffectOp Op = Branch.Values.Effects[Index].GetOp();
                if (Op != ELDDemoEffectOp::MomentumGain && Op != ELDDemoEffectOp::MomentumSteal && Op != ELDDemoEffectOp::ZoneMomentum) { continue; }
                ++MomentumEffects;
                FLDDemoAbilityValues Overdriven = *Grown;
                TestTrue(FString::Printf(TEXT("%s %s keeps its authored momentum at level 16 and under overdrive"), *Ability.Id,
                    LDSkillBranch::StepKey(Branch.Path)), Grown->Effects[Index].BaseP2 == Branch.Values.Effects[Index].BaseP2
                    && LDSkillGrowth::ScaleValues(Overdriven, Content.Parameters.Overdrive.ScaleBp, Content.Parameters.Overdrive.ScaleBp)
                    && Overdriven.Effects[Index].BaseP2 == Branch.Values.Effects[Index].BaseP2);
            }
        }
    }
    TestTrue(TEXT("The approved branches carry momentum grants"), MomentumEffects > 0);
    // Two concrete patches: S01 2B kicks 500 cm; S02 2A reaches 650 cm.
    const FLDDemoAbilityDefinition* Elbow = nullptr;
    const FLDDemoAbilityDefinition* Pants = nullptr;
    FString Error;
    if (!Catalog->FindAbility(TEXT("active_elbow"), Elbow, Error) || !Catalog->FindAbility(TEXT("active_pants_down"), Pants, Error))
    { AddError(Error); return false; }
    const FLDDemoAbilityBranch* Kick = Elbow->FindBranch(ELDSkillBranch::B);
    const FLDDemoEffectDefinition* Push = Kick ? Kick->Values.Effects.FindByPredicate([](const FLDDemoEffectDefinition& E)
        { return E.GetOp() == ELDDemoEffectOp::Knockback; }) : nullptr;
    TestTrue(TEXT("S01 B kicks 500 cm"), Push && Push->DistanceCm.Numerator == 500 * Push->DistanceCm.Denominator);
    const FLDDemoAbilityBranch* Belt = Pants->FindBranch(ELDSkillBranch::A);
    TestTrue(TEXT("S02 A reaches 650 cm and keeps it under A1"), Belt && Belt->Target.RangeCm.Numerator == 650 * Belt->Target.RangeCm.Denominator
        && Pants->FindBranch(ELDSkillBranch::A1)->Target.RangeCm.Numerator == Belt->Target.RangeCm.Numerator);
    // Rank 3 at level 1: base x 2 and coefficients x 1.5 on S01 B1.
    const FLDDemoAbilityValues* Rank3 = Catalog->FindValues(*Elbow, 1, ELDSkillBranch::B1);
    const FLDDemoEffectDefinition* Damage = Rank3 ? Rank3->Effects.FindByPredicate([](const FLDDemoEffectDefinition& E)
        { return E.GetOp() == ELDDemoEffectOp::Damage; }) : nullptr;
    TestTrue(TEXT("S01 B1 at level 1: 55 x 2 + 0.6 x 1.5 AD"), Damage && Damage->BaseP2 == 11000 && Damage->ADRatioBp == 9000);
    TestTrue(TEXT("No variant exists for None outside rank 1, nor a rank-2 path at rank 3"), !Elbow->FindBranch(ELDSkillBranch::None)
        && Catalog->FindValues(*Elbow, 1, ELDSkillBranch::None) != nullptr);
    return true;
}

#endif
