#include "Combat/LDCombatMechanics.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/LDControlRules.h"
#include "Combat/LDTacticalRules.h"
#include "Misc/AutomationTest.h"

// #112 §5 mechanics (C2): pure rules of heal/shield reduction, shield events, knockback, breach layers, the negative-kind count,
// periodic schedules, zones, the shield burst and the damage guard, plus the decoy lure's control matrix.
namespace LDCombatMechanicsTests
{
FGuid Id(uint32 Seed) { return FGuid(Seed, Seed + 1, Seed + 2, Seed + 3); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoMechanicsHealCutTest, "Lockdown.Rules.DemoMechanics.HealCut",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoMechanicsHealCutTest::RunTest(const FString& Parameters)
{
    using namespace LDCombatMechanicsTests;
    TArray<FLDCombatHealCut> Cuts;
    TestTrue(TEXT("A 40% cut lands"), LDCombatMechanics::ApplyHealCut(Cuts, {Id(1), Id(10), 4000, 3000}, 0));
    TestTrue(TEXT("A 35% cut of another source lands beside it"), LDCombatMechanics::ApplyHealCut(Cuts, {Id(2), Id(20), 3500, 5000}, 0));
    TestEqual(TEXT("Same kinds never stack: the strongest applies"), LDCombatMechanics::StrongestHealCutBp(Cuts, 1000), int64(4000));
    TestTrue(TEXT("The same source replaces its own cut"), LDCombatMechanics::ApplyHealCut(Cuts, {Id(3), Id(10), 2000, 4000}, 1000));
    TestEqual(TEXT("One record per source"), Cuts.Num(), 2);
    TestEqual(TEXT("The weaker replacement leaves the other source strongest"), LDCombatMechanics::StrongestHealCutBp(Cuts, 1000), int64(3500));
    TestEqual(TEXT("Half-open expiry"), LDCombatMechanics::StrongestHealCutBp(Cuts, 5000), int64(0));
    TestTrue(TEXT("Lapsed cuts are dropped"), LDCombatMechanics::ExpireHealCuts(Cuts, 5000) && Cuts.IsEmpty());
    TestFalse(TEXT("A cut without a source is refused"), LDCombatMechanics::ApplyHealCut(Cuts, {Id(4), FGuid(), 1000, 100}, 0));
    TestFalse(TEXT("A cut past 100% is refused"), LDCombatMechanics::ApplyHealCut(Cuts, {Id(5), Id(30), 10001, 100}, 0));
    TestFalse(TEXT("An already lapsed cut is refused"), LDCombatMechanics::ApplyHealCut(Cuts, {Id(6), Id(30), 1000, 100}, 100));
    TestEqual(TEXT("A 35% cut of 150.00 floors to 97.50"), LDCombatMechanics::ReduceByBp(15000, 3500), int64(9750));
    TestEqual(TEXT("A cut of an odd P2 amount floors"), LDCombatMechanics::ReduceByBp(333, 3333), int64(222));
    TestEqual(TEXT("No cut keeps the amount"), LDCombatMechanics::ReduceByBp(12345, 0), int64(12345));
    TestEqual(TEXT("A full cut removes it"), LDCombatMechanics::ReduceByBp(12345, 10000), int64(0));
    TestEqual(TEXT("Huge amounts stay exact"), LDCombatMechanics::ReduceByBp(MAX_int64 / 2, 5000), MAX_int64 / 2 / 10000 * 5000 + (MAX_int64 / 2) % 10000 * 5000 / 10000);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoMechanicsShieldEventsTest, "Lockdown.Rules.DemoMechanics.ShieldEvents",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoMechanicsShieldEventsTest::RunTest(const FString& Parameters)
{
    using namespace LDCombatMechanicsTests;
    TOptional<FLDCombatShield> Shield;
    int64 ShieldP2 = 0;
    TestTrue(TEXT("An empty record gains (R1: from nothing is no refresh)"),
        LDCombatMechanics::GrantShield(Shield, ShieldP2, {Id(1), Id(9), 2500}, 9000, 0) == ELDShieldGrant::Gained);
    TestEqual(TEXT("Still one shield record"), ShieldP2, int64(9000));
    TestTrue(TEXT("A weaker shield is refused and fires nothing"),
        LDCombatMechanics::GrantShield(Shield, ShieldP2, {Id(2), Id(9), 3000}, 8999, 100) == ELDShieldGrant::Rejected);
    TestTrue(TEXT("An equal shield replaces a held one: a refresh"),
        LDCombatMechanics::GrantShield(Shield, ShieldP2, {Id(3), Id(9), 3000}, 9000, 100) == ELDShieldGrant::Refreshed);
    TestTrue(TEXT("The record now belongs to the newest effect"), Shield.IsSet() && Shield->EffectId == Id(3));
    TestTrue(TEXT("After lapsing, a new shield only gains"),
        LDCombatMechanics::GrantShield(Shield, ShieldP2, {Id(4), Id(9), 9000}, 100, 3000) == ELDShieldGrant::Gained);
    ShieldP2 = 0;
    TestTrue(TEXT("A record emptied by damage holds nothing: the next shield gains"),
        LDCombatMechanics::GrantShield(Shield, ShieldP2, {Id(5), Id(9), 9500}, 100, 4000) == ELDShieldGrant::Gained);
    TestTrue(TEXT("Damage emptying a positive shield breaks it"), LDCombatMechanics::ShieldBroken(5000, 0, 5000));
    TestFalse(TEXT("Damage leaving some shield is no break"), LDCombatMechanics::ShieldBroken(5000, 1, 4999));
    TestFalse(TEXT("A consumed or lapsed shield (no absorption) is no break"), LDCombatMechanics::ShieldBroken(5000, 0, 0));
    TestFalse(TEXT("No shield, no break"), LDCombatMechanics::ShieldBroken(0, 0, 0));
    TestEqual(TEXT("Shield burst counts the shield up to its cap"), LDCombatMechanics::ShieldBurstBaseP2(5000, 7480, 12000, 30000), int64(5000 + 8976));
    TestEqual(TEXT("Shield burst caps a large shield at 300"), LDCombatMechanics::ShieldBurstBaseP2(7000, 59000, 12000, 30000), int64(7000 + 36000));
    TestEqual(TEXT("No shield, only the base"), LDCombatMechanics::ShieldBurstBaseP2(5000, 0, 12000, 30000), int64(5000));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoMechanicsKnockbackTest, "Lockdown.Rules.DemoMechanics.Knockback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoMechanicsKnockbackTest::RunTest(const FString& Parameters)
{
    FVector Direction;
    TestTrue(TEXT("A push leaves the source foot toward the target"),
        LDCombatMechanics::KnockbackDirection(FVector(0, 0, 10), FVector(30, 40, 0), 0, Direction)
        && Direction.Equals(FVector(0.6, 0.8, 0), 1e-9));
    TestTrue(TEXT("Height never tilts the push off the ground"),
        LDCombatMechanics::KnockbackDirection(FVector(0, 0, 0), FVector(-10, 0, 500), 0, Direction)
        && Direction.Equals(FVector(-1, 0, 0), 1e-9));
    TestTrue(TEXT("Coincident feet push along the source's facing"),
        LDCombatMechanics::KnockbackDirection(FVector(5, 5, 0), FVector(5, 5, 0), 90, Direction)
        && Direction.Equals(FVector(0, 1, 0), 1e-9));
    TestFalse(TEXT("Non-finite input is refused"),
        LDCombatMechanics::KnockbackDirection(FVector(std::numeric_limits<double>::quiet_NaN(), 0, 0), FVector(1, 0, 0), 0, Direction));
    // A decoy lure (taunt with bDecoy) also forbids every basic attack; an ordinary taunt allows swings at its taunter.
    FLDCombatTaunt Lure;
    Lure.EffectId = LDCombatMechanicsTests::Id(7);
    Lure.SourceEntity = LDCombatMechanicsTests::Id(8);
    Lure.ExpiresAtMs = 1000;
    Lure.bDecoy = true;
    const TOptional<FLDCombatTaunt> Lured(Lure);
    TestTrue(TEXT("A lured body cannot attack"), LDControl::TauntBlocks(Lured, 0, ELDCombatControlAction::Basic));
    TestTrue(TEXT("A lured body cannot cast"), LDControl::TauntBlocks(Lured, 0, ELDCombatControlAction::Cast));
    TestTrue(TEXT("A lured body cannot use items"), LDControl::TauntBlocks(Lured, 0, ELDCombatControlAction::Gear));
    TestFalse(TEXT("No basic swing is allowed at anyone while lured"), LDControl::TauntAllows(Lured, 0, true, Lure.SourceBody));
    TestFalse(TEXT("The lure ends at its half-open expiry"), LDControl::TauntBlocks(Lured, 1000, ELDCombatControlAction::Basic));
    FLDCombatTaunt Taunt = Lure;
    Taunt.bDecoy = false;
    const TOptional<FLDCombatTaunt> Taunted(Taunt);
    TestFalse(TEXT("An ordinary taunt still allows basic attacks"), LDControl::TauntBlocks(Taunted, 0, ELDCombatControlAction::Basic));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoMechanicsBreachTest, "Lockdown.Rules.DemoMechanics.Breach",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoMechanicsBreachTest::RunTest(const FString& Parameters)
{
    // The #112 C2 layers on C1's single breach record: active_expose_post adds one 6-point layer per tick (R2, at most
    // three), passive_dox_master one layer per hit (R5, per-layer shred, at most three); flat terms count per layer.
    using namespace LDCombatMechanicsTests;
    TArray<FLDCombatBreach> Breaches;
    FLDCombatBreach Post;
    Post.Key = TEXT("active_expose_post");
    Post.SourceEntity = Id(1);
    Post.ArmorFlatP2 = Post.MagicResistFlatP2 = 600;
    Post.Stacks = 1;
    Post.MaxStacks = 3;
    Post.ExpiresAtMs = 3001;
    for (int32 Tick = 0; Tick < 4; ++Tick) { LDTactical::ApplyBreach(Breaches, Post, 1000 * Tick); }
    TestEqual(TEXT("The same key and source keeps one record"), Breaches.Num(), 1);
    TestEqual(TEXT("Expose post deepens to its cap: 60 - 18"), LDTactical::BreachedResistanceP2(Breaches, 6000, true, 2000), int64(4200));
    FLDCombatBreach Dox;
    Dox.Key = TEXT("passive_dox_master");
    Dox.SourceEntity = Id(2);
    Dox.ArmorFlatP2 = Dox.MagicResistFlatP2 = 400;
    Dox.Stacks = 1;
    Dox.MaxStacks = 3;
    Dox.ExpiresAtMs = 2500;
    LDTactical::ApplyBreach(Breaches, Dox, 2000);
    LDTactical::ApplyBreach(Breaches, Dox, 2000);
    TestEqual(TEXT("Different sources add: 60 - 18 - 8"), LDTactical::BreachedResistanceP2(Breaches, 6000, false, 2000), int64(3400));
    Dox.ExpiresAtMs = 6000;
    LDTactical::ApplyBreach(Breaches, Dox, 3000);
    TestEqual(TEXT("A lapsed dox record restarts from one layer"), LDTactical::BreachedResistanceP2(Breaches, 6000, true, 3001), int64(5600));
    TestTrue(TEXT("Any active record is a breach"), LDTactical::HasBreach(Breaches, 5999));
    TestFalse(TEXT("None after the last ends"), LDTactical::HasBreach(Breaches, 6000));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoMechanicsNegativeKindsTest, "Lockdown.Rules.DemoMechanics.NegativeKinds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoMechanicsNegativeKindsTest::RunTest(const FString& Parameters)
{
    FLDNegativeKinds Kinds;
    TestEqual(TEXT("A clean body has no negative kind"), LDCombatMechanics::CountNegativeKinds(Kinds), 0);
    TestEqual(TEXT("No kind, no amplification"), LDCombatMechanics::NegativeKindsAmplifyBp(0, 2500), int64(10000));
    Kinds.bRoot = Kinds.bSlow = true;
    TestEqual(TEXT("Root and slow are two kinds"), LDCombatMechanics::CountNegativeKinds(Kinds), 2);
    TestEqual(TEXT("+25% per kind"), LDCombatMechanics::NegativeKindsAmplifyBp(2, 2500), int64(15000));
    Kinds.bDisarm = Kinds.bStun = Kinds.bTaunt = Kinds.bBreach = true;
    TestEqual(TEXT("All six kinds"), LDCombatMechanics::CountNegativeKinds(Kinds), 6);
    TestEqual(TEXT("At most six kinds amplify"), LDCombatMechanics::NegativeKindsAmplifyBp(9, 2500), int64(25000));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoMechanicsPeriodicTest, "Lockdown.Rules.DemoMechanics.Periodic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoMechanicsPeriodicTest::RunTest(const FString& Parameters)
{
    // active_fart_dash Lv3 gas: three pulses at 0.5/1.5/2.5 s; active_pie_chart: three at 0/1/2 s.
    TestEqual(TEXT("The first gas pulse"), LDCombatMechanics::NextPulseAtMs(1000, 500, 1000, 3, 1000), int64(1500));
    TestEqual(TEXT("A pulse due exactly now is still due"), LDCombatMechanics::NextPulseAtMs(1000, 500, 1000, 3, 2500), int64(2500));
    TestEqual(TEXT("Between pulses, the next one"), LDCombatMechanics::NextPulseAtMs(1000, 500, 1000, 3, 2501), int64(3500));
    TestEqual(TEXT("After the last pulse nothing is left"), LDCombatMechanics::NextPulseAtMs(1000, 500, 1000, 3, 3501), int64(0));
    TestEqual(TEXT("A zone pulses at its start"), LDCombatMechanics::NextPulseAtMs(1000, 0, 1000, 3, 1000), int64(1000));
    TestEqual(TEXT("The third pie pulse"), LDCombatMechanics::NextPulseAtMs(1000, 0, 1000, 3, 2500), int64(3000));
    TestEqual(TEXT("No fourth pie pulse"), LDCombatMechanics::NextPulseAtMs(1000, 0, 1000, 3, 3001), int64(0));
    TestEqual(TEXT("An invalid schedule never pulses"), LDCombatMechanics::NextPulseAtMs(1000, 0, 0, 3, 1000), int64(0));
    TestTrue(TEXT("The zone boundary counts as inside"), LDCombatMechanics::InsideZone(FVector(0, 0, 0), 300, FVector(300, 0, 90)));
    TestFalse(TEXT("Just outside is outside"), LDCombatMechanics::InsideZone(FVector(0, 0, 0), 300, FVector(300.01, 0, 0)));
    // active_porcelain_flop: the guard recoils once the damage counted before its reduction exceeds 80.
    int64 Taken = 0;
    TestFalse(TEXT("59.3 does not exceed 80"), LDCombatMechanics::GuardTriggers(0, 5930, 8000, Taken));
    TestEqual(TEXT("The guard keeps counting"), Taken, int64(5930));
    TestFalse(TEXT("Exactly 80 does not exceed 80"), LDCombatMechanics::GuardTriggers(5930, 2070, 8000, Taken));
    TestTrue(TEXT("Crossing 80 recoils"), LDCombatMechanics::GuardTriggers(8000, 1, 8000, Taken));
    TestFalse(TEXT("A guard past its threshold never recoils again"), LDCombatMechanics::GuardTriggers(8001, 5000, 8000, Taken));
    return true;
}

#endif
