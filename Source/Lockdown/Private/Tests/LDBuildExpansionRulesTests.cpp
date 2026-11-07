#include "Combat/LDTacticalRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

namespace LDBuildExpansionRulesTestDetail
{
    FLDDemoStatBonus Bonus(ELDDemoBonusStat Stat, int64 Amount)
    {
        FLDDemoStatBonus Value;
        Value.Stat = Stat;
        Value.Amount = Amount;
        return Value;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildExpansionSlowTest, "Lockdown.Rules.DemoBuildExpansion.Slow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDBuildExpansionSlowTest::RunTest(const FString& Parameters)
{
    using namespace LDTactical;
    TArray<FLDCombatSlow> Slows;
    const FGuid Shoe = FGuid::NewGuid(), Fart = FGuid::NewGuid(), Source = FGuid::NewGuid();
    TestTrue(TEXT("A new slow instance applies"), ApplySlow(Slows, {Shoe, Source, 2500, 2500}, 1000));
    TestFalse(TEXT("Re-applying the same instance never renews it"), ApplySlow(Slows, {Shoe, Source, 2500, 4000}, 1200));
    TestFalse(TEXT("A slow that would stop the body is refused"), ApplySlow(Slows, {FGuid::NewGuid(), Source, 10000, 4000}, 1200));
    TestFalse(TEXT("A zero slow is refused"), ApplySlow(Slows, {FGuid::NewGuid(), Source, 0, 4000}, 1200));
    TestFalse(TEXT("A slow ending at application time is refused"), ApplySlow(Slows, {FGuid::NewGuid(), Source, 3000, 1200}, 1200));
    TestTrue(TEXT("Another instance keeps its own deadline"), ApplySlow(Slows, {Fart, FGuid::NewGuid(), 3000, 2000}, 1200));
    TestEqual(TEXT("Only the strongest active slow counts (no stacking)"), StrongestSlowBp(Slows, 1500), int64(3000));
    TestEqual(TEXT("The visible deadline belongs to the strongest slow"), StrongestSlowUntilMs(Slows, 1500), int64(2000));
    TestEqual(TEXT("After the strongest ends the weaker one applies"), StrongestSlowBp(Slows, 2000), int64(2500));
    TestEqual(TEXT("A slow ends exactly at its deadline"), StrongestSlowBp(Slows, 2500), int64(0));
    TArray<FLDCombatTimedBonus> NoBonuses;
    TestTrue(TEXT("Lapsed slows are removed"), ExpireTemporaryStatuses(Slows, NoBonuses, 2000) && Slows.Num() == 1);
    TestFalse(TEXT("Nothing more lapses before the next deadline"), ExpireTemporaryStatuses(Slows, NoBonuses, 2100));
    const FLDTemporaryAttributeTerms Terms = TemporaryTerms(NoBonuses, Slows, {}, 100, 100, 2100);
    TestTrue(TEXT("Temporary terms carry the strongest slow only"), Terms.MoveSlowBp == 2500 && Terms.Bonuses.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildExpansionTimedBonusTest, "Lockdown.Rules.DemoBuildExpansion.TimedBonus",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDBuildExpansionTimedBonusTest::RunTest(const FString& Parameters)
{
    using namespace LDTactical;
    using LDBuildExpansionRulesTestDetail::Bonus;
    TArray<FLDCombatTimedBonus> Bonuses;
    const FName Flurry(TEXT("active_combo_flurry")), Moves(TEXT("active_slick_moves"));
    ApplyTimedBonus(Bonuses, {FGuid::NewGuid(), Flurry, {Bonus(ELDDemoBonusStat::AttackSpeedBonusRatio, 3000)}, 5000});
    ApplyTimedBonus(Bonuses, {FGuid::NewGuid(), Moves, {Bonus(ELDDemoBonusStat::MoveSpeedBonusRatio, 5000)}, 3000});
    ApplyTimedBonus(Bonuses, {FGuid::NewGuid(), Flurry, {Bonus(ELDDemoBonusStat::AttackSpeedBonusRatio, 6000)}, 6000});
    TestEqual(TEXT("A recast of the same source replaces its bonus; other sources aggregate"), Bonuses.Num(), 2);
    FLDTemporaryAttributeTerms Terms = TemporaryTerms(Bonuses, {}, {}, 100, 100, 2000);
    TestTrue(TEXT("Both active sources project their stats"), Terms.Bonuses.Num() == 2 && Terms.MoveSlowBp == 0
        && Terms.Bonuses.ContainsByPredicate([](const FLDDemoStatBonus& Stat) { return Stat.Amount == 6000; })
        && !Terms.Bonuses.ContainsByPredicate([](const FLDDemoStatBonus& Stat) { return Stat.Amount == 3000; }));
    Terms = TemporaryTerms(Bonuses, {}, {}, 100, 100, 3000);
    TestTrue(TEXT("A bonus ends exactly at its deadline"), Terms.Bonuses.Num() == 1
        && Terms.Bonuses[0].Stat == ELDDemoBonusStat::AttackSpeedBonusRatio);
    TArray<FLDCombatSlow> NoSlows;
    TestTrue(TEXT("Lapsed bonuses are removed"), ExpireTemporaryStatuses(NoSlows, Bonuses, 3000) && Bonuses.Num() == 1);
    TestTrue(TEXT("Equal terms compare equal"), TemporaryTerms(Bonuses, {}, {}, 100, 100, 3500) == TemporaryTerms(Bonuses, {}, {}, 100, 100, 4000));
    TestTrue(TEXT("Expired terms differ from active terms"), TemporaryTerms(Bonuses, {}, {}, 100, 100, 3500) != TemporaryTerms(Bonuses, {}, {}, 100, 100, 6000));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildExpansionConditionalTest, "Lockdown.Rules.DemoBuildExpansion.ConditionalTriggers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDBuildExpansionConditionalTest::RunTest(const FString& Parameters)
{
    using namespace LDTactical;
    using LDBuildExpansionRulesTestDetail::Bonus;
    // Low HP: strictly below the threshold, never for an empty or dead pool.
    TestTrue(TEXT("Below half HP"), BelowHPThreshold(49999, 100000, 5000));
    TestFalse(TEXT("Exactly half HP is not below"), BelowHPThreshold(50000, 100000, 5000));
    TestFalse(TEXT("Zero HP never counts"), BelowHPThreshold(0, 100000, 5000));
    TestFalse(TEXT("A threshold outside the pool is refused"), BelowHPThreshold(1, 100000, 10000));
    TOptional<FLDLowHPBonusTerm> LowHP;
    LowHP.Emplace();
    LowHP->ThresholdBp = 5000;
    LowHP->Stats = {Bonus(ELDDemoBonusStat::ADBonusRatio, 2200)};
    TestTrue(TEXT("The conditional bonus joins the terms below the threshold"),
        TemporaryTerms({}, {}, LowHP, 40000, 100000, 0).Bonuses.Num() == 1);
    TestTrue(TEXT("The conditional bonus resets when HP recovers"), TemporaryTerms({}, {}, LowHP, 60000, 100000, 0).IsEmpty());
    return true;
}

#endif
