#include "Combat/LDCombatDirector.h"
#include "Combat/LDCombatControlRules.h"
#include "Combat/LDCombatSourceRules.h"
#include "Combat/LDControlRules.h"
#include "Combat/LDTacticalRules.h"
#include "Framework/LDGameMode.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

// #112 §5 shared combat primitives: hard-control diminishing returns, remaining-cooldown cuts, slow cleanses and the
// single control path.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatHardControlDiminishTest, "Lockdown.Rules.DemoCombatPrimitives.HardControlDiminish",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatHardControlDiminishTest::RunTest(const FString& Parameters)
{
    using namespace LDCombatControl;
    for (const ELDControlKind Kind : {ELDControlKind::Root, ELDControlKind::Stun, ELDControlKind::Disarm, ELDControlKind::Taunt})
    { TestTrue(TEXT("Root, stun, disarm and taunt are hard controls"), IsHardControl(Kind)); }
    TestFalse(TEXT("A slow never diminishes"), IsHardControl(ELDControlKind::Slow));
    TestFalse(TEXT("A knockback never diminishes"), IsHardControl(ELDControlKind::Knockback));
    FLDHardControlChain Chain;
    TestEqual(TEXT("The first hard control lasts its full time"), DiminishHardControl(Chain, 1000, 1500), int64(1500));
    TestEqual(TEXT("A second one while the first is active lasts 60%"), DiminishHardControl(Chain, 2000, 1500), int64(900));
    TestEqual(TEXT("A third one lasts 30%"), DiminishHardControl(Chain, 2500, 1500), int64(450));
    TestEqual(TEXT("Later ones stay at 30%"), DiminishHardControl(Chain, 2600, 1000), int64(300));
    TestEqual(TEXT("A diminished control never falls below 0.2 s"), DiminishHardControl(Chain, 2700, 400), int64(200));
    TestEqual(TEXT("A control shorter than the floor keeps its own length"), DiminishHardControl(Chain, 2800, 100), int64(100));
    // The window ends 2 s after the latest diminished control ended (2500 + 450 + 2000 = 4950 is the latest).
    TestEqual(TEXT("Inside the 2 s window after the end the chain continues"), DiminishHardControl(Chain, 4800, 1000), int64(300));
    FLDHardControlChain Fresh;
    DiminishHardControl(Fresh, 0, 1000);
    TestEqual(TEXT("Exactly 2 s after the end the chain has cleared"), DiminishHardControl(Fresh, 3000, 1000), int64(1000));
    FLDHardControlChain Within;
    DiminishHardControl(Within, 0, 1000);
    TestEqual(TEXT("Just before the window closes it still diminishes"), DiminishHardControl(Within, 2999, 1000), int64(600));
    FLDHardControlChain Untouched;
    TestEqual(TEXT("A non-positive duration is returned unchanged"), DiminishHardControl(Untouched, 0, 0), int64(0));
    TestTrue(TEXT("and leaves the chain untouched"), Untouched.Count == 0 && Untouched.WindowEndMs == 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatReduceReadyAtTest, "Lockdown.Rules.DemoCombatPrimitives.ReduceReadyAt",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatReduceReadyAtTest::RunTest(const FString& Parameters)
{
    using LDCombatSourceRules::ReduceReadyAt;
    TestEqual(TEXT("A cooling track ends earlier by the cut"), ReduceReadyAt(10000, 4000, 3000), int64(7000));
    TestEqual(TEXT("A cut never moves the end before now"), ReduceReadyAt(5000, 4000, 3000), int64(4000));
    TestEqual(TEXT("MAX_int64 makes the track ready now"), ReduceReadyAt(10000, 4000, MAX_int64), int64(4000));
    TestEqual(TEXT("A ready track is unchanged"), ReduceReadyAt(3000, 4000, 1000), int64(3000));
    TestEqual(TEXT("A non-positive cut changes nothing"), ReduceReadyAt(10000, 4000, 0), int64(10000));
    TestEqual(TEXT("A negative cut changes nothing"), ReduceReadyAt(10000, 4000, -500), int64(10000));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatSlowCleanseTest, "Lockdown.Rules.DemoCombatPrimitives.SlowCleanse",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatSlowCleanseTest::RunTest(const FString& Parameters)
{
    using namespace LDTactical;
    TArray<FLDCombatSlow> Slows;
    FLDCombatSlow Short{FGuid::NewGuid(), FGuid::NewGuid(), 1000, 1500};
    FLDCombatSlow Bottle{FGuid::NewGuid(), FGuid::NewGuid(), 3500, 2500};
    TestTrue(TEXT("A slow applies"), ApplySlow(Slows, Short, 0));
    TestTrue(TEXT("Another slow applies"), ApplySlow(Slows, Bottle, 0));
    const FLDTemporaryAttributeTerms Terms = TemporaryTerms({}, Slows, {}, 100, 100, 1000);
    TestEqual(TEXT("The strongest move cut projects"), Terms.MoveSlowBp, int64(3500));
    TestEqual(TEXT("A cleanse counts only active slows"), DispelSlows(Slows, 2000), 1);
    TestTrue(TEXT("and removes every instance"), Slows.IsEmpty());
    TestEqual(TEXT("Nothing to cleanse counts zero"), DispelSlows(Slows, 2000), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatCommitControlTest, "Lockdown.Rules.DemoCombatPrimitives.CommitControl",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatCommitControlTest::RunTest(const FString& Parameters)
{
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    if (!TestNotNull(TEXT("An isolated control world"), World)) { return false; }
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    ALDCombatDirector* Director = World->SpawnActor<ALDCombatDirector>();
    ALDGameMode* Mode = World->SpawnActor<ALDGameMode>();
    if (!TestNotNull(TEXT("A combat director"), Director) || !TestNotNull(TEXT("A core"), Mode)) { return false; }
    // The control path reaches the spray zone through the core; with no active zone nothing is interrupted.
    Mode->Combat = Director;
    World->CopyGameState(Mode, nullptr);
    FLDBodyCombatState Victim;
    Victim.Victim.Kind = ELDVictimKind::Contender;
    Victim.Victim.EntityId = FGuid::NewGuid();
    const FGuid Source = FGuid::NewGuid();
    FLDCombatBodyRef SourceBody;
    SourceBody.Victim.Kind = ELDVictimKind::Contender;
    SourceBody.Victim.EntityId = Source;
    SourceBody.Victim.Life = 1;
    SourceBody.BodyIdentity = FGuid::NewGuid();
    const auto Commit = [&](ELDControlKind Kind, int64 NowMs, int64 DurationMs, bool& bApplied)
    {
        FLDControlApplication App;
        App.Kind = Kind;
        App.EffectId = FGuid::NewGuid();
        App.SourceEntity = Source;
        App.SourceBody = SourceBody;
        App.DurationMs = DurationMs;
        App.RatioBp = 3000;
        FString Error;
        const bool bOk = Director->CommitControl(Victim, App, NowMs, bApplied, Error);
        if (!bOk) { AddInfo(Error); }
        return bOk;
    };
    bool bApplied = false;
    TestTrue(TEXT("A root commits"), Commit(ELDControlKind::Root, 1000, 1000, bApplied) && bApplied);
    TestEqual(TEXT("The first root lasts its full time"), Victim.Roots.Last().ExpiresAtMs, int64(2000));
    TestTrue(TEXT("The body is impaired"), Director->IsImpaired(Victim, 1500));
    TestTrue(TEXT("A stun from any source joins the same chain"), Commit(ELDControlKind::Stun, 1500, 1000, bApplied) && bApplied);
    TestEqual(TEXT("The second hard control lasts 60%"), Victim.Stuns.Last().ExpiresAtMs, int64(2100));
    TestTrue(TEXT("A slow commits"), Commit(ELDControlKind::Slow, 1600, 1000, bApplied) && bApplied);
    TestEqual(TEXT("A slow never diminishes"), Victim.Slows.Last().ExpiresAtMs, int64(2600));
    TestTrue(TEXT("A disarm commits"), Commit(ELDControlKind::Disarm, 1700, 1000, bApplied) && bApplied);
    TestEqual(TEXT("The third hard control lasts 30%"), Victim.Disarms.Last().ExpiresAtMs, int64(2000));
    TestTrue(TEXT("A taunt commits on a contender"), Commit(ELDControlKind::Taunt, 1800, 1000, bApplied) && bApplied);
    TestTrue(TEXT("The taunt is diminished too"), Victim.Taunt.IsSet() && Victim.Taunt->ExpiresAtMs == 2100);
    TestTrue(TEXT("A zero duration is no control"), Commit(ELDControlKind::Root, 1900, 0, bApplied) && !bApplied);
    FString Error;
    FLDControlApplication Push;
    Push.Kind = ELDControlKind::Knockback;
    Push.EffectId = FGuid::NewGuid();
    Push.SourceEntity = Source;
    Push.SourceBody = SourceBody;
    Push.DurationMs = 250;
    // #112 C2: a knockback commits its forced-displacement window, undiminished, and bumps the order-drop serial.
    const FLDHardControlChain ChainBefore = Victim.HardControl;
    const uint64 SerialBefore = Victim.KnockbackSerial;
    TestTrue(TEXT("A knockback commits through the single control path"), Director->CommitControl(Victim, Push, 1900, bApplied, Error) && bApplied);
    TestTrue(TEXT("Its window lasts the full undiminished time"),
        Victim.Knockbacks.Num() == 1 && Victim.Knockbacks[0].ExpiresAtMs == 2150 && Victim.KnockbackSerial == SerialBefore + 1);
    TestTrue(TEXT("A knockback never advances the hard-control chain"),
        ChainBefore.Count == Victim.HardControl.Count && ChainBefore.WindowEndMs == Victim.HardControl.WindowEndMs);
    FLDBodyCombatState Camp;
    Camp.Victim.Kind = ELDVictimKind::CampMember;
    Camp.Victim.EntityId = FGuid::NewGuid();
    FLDControlApplication Taunt;
    Taunt.Kind = ELDControlKind::Taunt;
    Taunt.EffectId = FGuid::NewGuid();
    Taunt.DurationMs = 1000;
    TestTrue(TEXT("Taunting a camp member is not a fault"), Director->CommitControl(Camp, Taunt, 0, bApplied, Error));
    TestTrue(TEXT("Camp members are never taunted and keep a clean chain"), !bApplied && !Camp.Taunt.IsSet() && Camp.HardControl.Count == 0);
    TestFalse(TEXT("After every control ends the body is no longer impaired"), Director->IsImpaired(Victim, 5000));
    return true;
}

#endif
