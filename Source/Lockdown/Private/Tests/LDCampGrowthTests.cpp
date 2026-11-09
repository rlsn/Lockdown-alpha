#include "Development/LDCampGrowth.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace LDCampGrowthTests
{
// Test-only curve with the approved shape; the numbers are chosen so floors are visible, not the approved ones.
FLDDemoContentParameters MakeParameters()
{
    FLDDemoContentParameters P;
    P.CampGrowthStageMs = 60000;
    P.CampGrowthMaxStage = 10;
    P.CampGrowthHPPerStageBp = 1200;
    P.CampGrowthAttackPerStageBp = 800;
    P.CampGrowthResistPerStageP2 = 200;
    P.CampGrowthRewardXPPerStageBp = 1000;
    return P;
}

FLDDemoMonsterDefinition MakeMonster()
{
    FLDDemoMonsterDefinition M;
    M.Id = TEXT("monster_test");
    M.Tier = ELDDemoCampTier::High;
    M.CountPerCamp = 1;
    M.HPP2 = 9001;
    M.AttackP2 = 4801;
    M.ArmorP2 = 2000;
    M.MagicResistP2 = 1500;
    M.AttackIntervalMs = 1539;
    M.AttackWindupMs = 500;
    M.RespawnMs = 65000;
    M.RewardXP = 21;
    return M;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCampGrowthTest, "Lockdown.Rules.DemoCamp.Growth",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCampGrowthTest::RunTest(const FString& Parameters)
{
    using namespace LDCampGrowthTests;
    const FLDDemoContentParameters P = MakeParameters();
    const FLDDemoMonsterDefinition Base = MakeMonster();

    // 1. The stage counts whole StageMs from T=0, starts at 0 during the countdown and stops at the cap.
    bool bGood = true;
    for (const auto& [SinceMs, Expected] : TArray<TTuple<int64, uint8>>{
        {-5000, 0}, {0, 0}, {59999, 0}, {60000, 1}, {119999, 1}, {599999, 9}, {600000, 10}, {MAX_int64, 10}})
    {
        bGood &= TestEqual(FString::Printf(TEXT("A life spawned %lld ms after T=0 takes stage %u"), SinceMs, Expected),
            LDCampGrowth::Stage(P, SinceMs), Expected);
    }
    FLDDemoContentParameters Unset;
    bGood &= TestEqual(TEXT("Without an approved curve every life stays at stage 0"), LDCampGrowth::Stage(Unset, 600000), uint8(0));

    // 2. Stage 0 is the base definition; each stage adds a floored share of the base HP and attack and a flat resist.
    FLDDemoMonsterDefinition Grown;
    FString Error;
    bGood &= TestTrue(TEXT("Stage 0 scales"), LDCampGrowth::ScaleBody(P, Base, 0, Grown, Error));
    bGood &= TestTrue(TEXT("Stage 0 keeps the base values"), Grown.HPP2 == Base.HPP2 && Grown.AttackP2 == Base.AttackP2
        && Grown.ArmorP2 == Base.ArmorP2 && Grown.MagicResistP2 == Base.MagicResistP2);
    bGood &= TestTrue(TEXT("Stage 3 scales"), LDCampGrowth::ScaleBody(P, Base, 3, Grown, Error));
    // 9001 * 1.36 = 12241.36 and 4801 * 1.24 = 5953.24, floored in P2; resists +6 points each.
    bGood &= TestEqual(TEXT("Stage 3 HP is +36% floored"), Grown.HPP2, int64(12241));
    bGood &= TestEqual(TEXT("Stage 3 attack is +24% floored"), Grown.AttackP2, int64(5953));
    bGood &= TestEqual(TEXT("Stage 3 armor is +6 points"), Grown.ArmorP2, int64(2600));
    bGood &= TestEqual(TEXT("Stage 3 magic resist is +6 points"), Grown.MagicResistP2, int64(2100));
    bGood &= TestTrue(TEXT("Growth leaves timing, respawn and rewards alone"), Grown.AttackIntervalMs == Base.AttackIntervalMs
        && Grown.AttackWindupMs == Base.AttackWindupMs && Grown.RespawnMs == Base.RespawnMs && Grown.RewardXP == Base.RewardXP
        && Grown.Id == Base.Id && Grown.Tier == Base.Tier);
    bGood &= TestTrue(TEXT("The cap stage scales"), LDCampGrowth::ScaleBody(P, Base, 10, Grown, Error));
    bGood &= TestEqual(TEXT("The cap stage HP is +120%"), Grown.HPP2, int64(19802));

    // 3. A stage past the cap is refused without a partial result.
    bGood &= TestFalse(TEXT("A stage past the cap is refused"), LDCampGrowth::ScaleBody(P, Base, 11, Grown, Error));
    bGood &= TestTrue(TEXT("The refusal names the stage and keeps the base values"), !Error.IsEmpty() && Grown.HPP2 == Base.HPP2);
    FLDDemoMonsterDefinition Huge = Base;
    Huge.HPP2 = MAX_int64 / 2;
    bGood &= TestFalse(TEXT("An overflowing HP is refused"), LDCampGrowth::ScaleBody(P, Huge, 10, Grown, Error));

    // 4. The XP reward grows by its own share, floored; gold is not part of the curve.
    int64 XP = 0;
    bGood &= TestTrue(TEXT("Stage 0 XP"), LDCampGrowth::RewardXP(P, Base, 0, XP) && XP == 21);
    bGood &= TestTrue(TEXT("Stage 3 XP is +30% floored"), LDCampGrowth::RewardXP(P, Base, 3, XP) && XP == 27);
    bGood &= TestTrue(TEXT("The cap stage XP doubles"), LDCampGrowth::RewardXP(P, Base, 10, XP) && XP == 42);
    bGood &= TestFalse(TEXT("XP past the cap is refused"), LDCampGrowth::RewardXP(P, Base, 11, XP));
    return bGood;
}
#endif
