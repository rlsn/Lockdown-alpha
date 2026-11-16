#include "Combat/LDTacticalRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

namespace LDCombatStatusRulesTestDetail
{
    FLDDemoRational Rate(int64 Numerator, int64 Denominator)
    {
        FLDDemoRational Value;
        Value.Numerator = Numerator;
        Value.Denominator = Denominator;
        return Value;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatStatusRootsTest, "Lockdown.Rules.DemoCombatStatus.Roots",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatStatusRootsTest::RunTest(const FString& Parameters)
{
    using namespace LDTactical;
    TArray<FLDCombatRoot> Roots;
    const FGuid First = FGuid::NewGuid(), Second = FGuid::NewGuid(), Source = FGuid::NewGuid();
    TestTrue(TEXT("A new Root instance applies"), ApplyRoot(Roots, {First, Source, 1500}, 1000));
    TestFalse(TEXT("Re-applying the same instance never renews it"), ApplyRoot(Roots, {First, Source, 2500}, 1200));
    TestEqual(TEXT("The same instance keeps its original deadline"), ActiveRootUntilMs(Roots, 1200), int64(1500));
    TestTrue(TEXT("Another source keeps its own instance"), ApplyRoot(Roots, {Second, FGuid::NewGuid(), 1400}, 1200));
    TestEqual(TEXT("The visible deadline is the latest active instance"), ActiveRootUntilMs(Roots, 1200), int64(1500));
    TestFalse(TEXT("An instance ending at application time is refused"), ApplyRoot(Roots, {FGuid::NewGuid(), Source, 1200}, 1200));
    TOptional<FLDCombatShield> NoShield;
    int64 NoShieldP2 = 0;
    const auto Expiry = ExpireTimedStatuses(Roots, NoShield, NoShieldP2, 1400);
    TestTrue(TEXT("An earlier expiry removes only its own instance"),
        Expiry.bRootsChanged && !Expiry.bShieldChanged && Roots.Num() == 1 && Roots[0].EffectId == First);
    TestTrue(TEXT("Root is active before its deadline"), HasActiveRoot(Roots, 1499));
    TestFalse(TEXT("Root ends exactly at its deadline"), HasActiveRoot(Roots, 1500));
    FLDDemoPolicies Policies;
    Policies.RootBlocks = {ELDDemoRootBlock::Move, ELDDemoRootBlock::Dash};
    TestTrue(TEXT("Root blocks voluntary movement"), RootBlocks(Roots, 1450, Policies, ELDDemoRootBlock::Move));
    TestTrue(TEXT("Root blocks dashes"), RootBlocks(Roots, 1450, Policies, ELDDemoRootBlock::Dash));
    Policies.RootBlocks = {ELDDemoRootBlock::Move};
    TestFalse(TEXT("Only policy-listed actions are blocked"), RootBlocks(Roots, 1450, Policies, ELDDemoRootBlock::Dash));
    TestEqual(TEXT("The cleanse removes every current Root"), DispelRoots(Roots), 1);
    TestFalse(TEXT("No Root remains after the cleanse"), HasActiveRoot(Roots, 1450));
    TestTrue(TEXT("A later legal application roots again"), ApplyRoot(Roots, {FGuid::NewGuid(), Source, 2000}, 1450));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatStatusShieldTest, "Lockdown.Rules.DemoCombatStatus.Shield",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatStatusShieldTest::RunTest(const FString& Parameters)
{
    using namespace LDTactical;
    const FGuid Source = FGuid::NewGuid();
    TArray<FLDCombatRoot> NoRoots;
    TOptional<FLDCombatShield> Shield;
    int64 ShieldP2 = 0;
    const FLDCombatShield First{FGuid::NewGuid(), Source, 5000};
    TestTrue(TEXT("A first barrier applies"),
        ApplyShield(Shield, ShieldP2, First, 12000, 1000) && ShieldP2 == 12000 && Shield->EffectId == First.EffectId);
    ShieldP2 = 8000;
    TestFalse(TEXT("A weaker barrier is suppressed"), ApplyShield(Shield, ShieldP2, {FGuid::NewGuid(), Source, 9000}, 7999, 2000));
    TestTrue(TEXT("The stronger remaining record keeps its amount, source and deadline"),
        ShieldP2 == 8000 && Shield->EffectId == First.EffectId && Shield->ExpiresAtMs == 5000);
    const FLDCombatShield Equal{FGuid::NewGuid(), Source, 9000};
    TestTrue(TEXT("An equal barrier refreshes the whole record"),
        ApplyShield(Shield, ShieldP2, Equal, 8000, 2000) && ShieldP2 == 8000 && Shield->EffectId == Equal.EffectId
        && Shield->ExpiresAtMs == 9000);
    TestTrue(TEXT("No legacy cap limits a stronger barrier"),
        ApplyShield(Shield, ShieldP2, {FGuid::NewGuid(), Source, 9500}, 1000000, 2000) && ShieldP2 == 1000000);
    TestTrue(TEXT("An expired barrier counts as no remaining amount"),
        ApplyShield(Shield, ShieldP2, {FGuid::NewGuid(), Source, 20000}, 1, 9500) && ShieldP2 == 1);
    TestFalse(TEXT("A nonpositive amount never applies"), ApplyShield(Shield, ShieldP2, {FGuid::NewGuid(), Source, 30000}, 0, 9600));
    auto Expiry = ExpireTimedStatuses(NoRoots, Shield, ShieldP2, 20000);
    TestTrue(TEXT("A barrier ends exactly at its deadline"), Expiry.bShieldChanged && !Shield.IsSet() && ShieldP2 == 0);
    Shield = FLDCombatShield{FGuid::NewGuid(), Source, 30000};
    ShieldP2 = 0;
    Expiry = ExpireTimedStatuses(NoRoots, Shield, ShieldP2, 21000);
    TestTrue(TEXT("A depleted barrier is cleared"), Expiry.bShieldChanged && !Shield.IsSet());
    Expiry = ExpireTimedStatuses(NoRoots, Shield, ShieldP2, 22000);
    TestFalse(TEXT("Nothing changes without a barrier"), Expiry.bShieldChanged || Expiry.bRootsChanged);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatStatusRegenTest, "Lockdown.Rules.DemoCombatStatus.Regen",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatStatusRegenTest::RunTest(const FString& Parameters)
{
    using namespace LDTactical;
    using LDCombatStatusRulesTestDetail::Rate;
    FString Error;
    const auto Base = Rate(8, 5);
    constexpr int64 Max = 100000;
    int64 Hp = 0, Remainder = 0;
    bool bGood = true;
    for (int32 Step = 0; Step < 1000; ++Step) { bGood &= IntegrateRegen(Base, 1, Max, Hp, Remainder, Error); }
    TestTrue(TEXT("1.6/s over 1000 one-millisecond steps restores exactly 1.60"), bGood && Hp == 160 && Remainder == 0);
    Hp = Remainder = 0;
    TestTrue(TEXT("One whole second is the same integral"),
        IntegrateRegen(Base, 1000, Max, Hp, Remainder, Error) && Hp == 160 && Remainder == 0);
    Hp = Remainder = 0;
    TestTrue(TEXT("Sub-P2 progress is carried"), IntegrateRegen(Base, 3, Max, Hp, Remainder, Error) && Hp == 0 && Remainder == 480000);
    TestTrue(TEXT("The carried remainder completes the next whole P2"),
        IntegrateRegen(Base, 4, Max, Hp, Remainder, Error) && Hp == 1 && Remainder == 120000);
    Hp = Max - 1;
    Remainder = 999999;
    TestTrue(TEXT("A full pool discards the overflow and its remainder"),
        IntegrateRegen(Base, 1000, Max, Hp, Remainder, Error) && Hp == Max && Remainder == 0);
    TestTrue(TEXT("A full pool stays full"), IntegrateRegen(Base, 10, Max, Hp, Remainder, Error) && Hp == Max && Remainder == 0);
    Hp = Remainder = 0;
    TestTrue(TEXT("An unreduced equal rate integrates identically"),
        IntegrateRegen(Rate(160, 100), 1000, Max, Hp, Remainder, Error) && Hp == 160 && Remainder == 0);
    Hp = Remainder = 0;
    TestTrue(TEXT("A zero rate keeps the pool and remainder"),
        IntegrateRegen(Rate(0, 1), 1000, Max, Hp, Remainder, Error) && Hp == 0 && Remainder == 0);
    TestFalse(TEXT("A rate without an exact micro-P2 step is refused"), IntegrateRegen(Rate(1, 3), 1, Max, Hp, Remainder, Error));
    TestTrue(TEXT("The refusal is explicit and writes nothing"), !Error.IsEmpty() && Hp == 0 && Remainder == 0);
    Hp = Max + 1;
    TestFalse(TEXT("A pool above its maximum is a domain error"), IntegrateRegen(Base, 1, Max, Hp, Remainder, Error));
    Hp = 0;
    Remainder = 1000000;
    TestFalse(TEXT("An unbounded remainder is a domain error"), IntegrateRegen(Base, 1, Max, Hp, Remainder, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatStatusCooldownTest, "Lockdown.Rules.DemoCombatStatus.Cooldown",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatStatusCooldownTest::RunTest(const FString& Parameters)
{
    using namespace LDTactical;
    FString Error;
    int64 Duration = 0;
    TestTrue(TEXT("6 s with 30 haste is 4616 ms"), CooldownDurationMs(6000, 3000, 10000, Duration, Error) && Duration == 4616);
    TestTrue(TEXT("Zero haste keeps the base duration"), CooldownDurationMs(28000, 0, 10000, Duration, Error) && Duration == 28000);
    TestTrue(TEXT("Exact division does not round up"), CooldownDurationMs(6000, 10000, 10000, Duration, Error) && Duration == 3000);
    TestFalse(TEXT("Negative haste is refused"), CooldownDurationMs(6000, -1, 10000, Duration, Error));
    TestFalse(TEXT("An overflowing base is refused"), CooldownDurationMs(MAX_int64, 0, 10000, Duration, Error));
    TestFalse(TEXT("A missing haste scale is refused"), CooldownDurationMs(6000, 0, 0, Duration, Error));
    return true;
}

#endif
