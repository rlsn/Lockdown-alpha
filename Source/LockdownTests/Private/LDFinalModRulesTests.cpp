#include "Framework/LDCaptureReducer.h"
#include "World/LDCaptureCircle.h"
#include "Components/BoxComponent.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS
// The win and flagpole rules of the eight universal final mods (App Spec §2.2, content §4.7) on the real capture reducer.
namespace
{
    FLDCombatBodyRef FinalBody(const FGuid& Id, uint32 Life)
    {
        return {{ELDVictimKind::Contender, Id, Life}, FGuid::NewGuid()};
    }

    FLDCaptureEventTime FinalMs(int64 Value)
    {
        FLDCaptureEventTime Time;
        FString Error;
        verify(FLDCaptureEventTime::FromMs(Value, Time, Error));
        return Time;
    }

    FLDCaptureEvent FinalFact(ELDCaptureEventKind Kind, const FLDCombatBodyRef& Body, int64 Ms, ELDCommitPhase Phase,
        bool bInside = false, int64 ProtectedUntilMs = 0)
    {
        FLDCaptureEvent Event;
        Event.Kind = Kind;
        Event.Body = Body;
        Event.Time = FinalMs(Ms);
        Event.Phase = Phase;
        Event.bInside = bInside;
        Event.ProtectedUntilMs = ProtectedUntilMs;
        return Event;
    }

    FLDCaptureEvent FinalJoined(const FLDCombatBodyRef& Body, bool bInside)
    { return FinalFact(ELDCaptureEventKind::Joined, Body, 0, ELDCommitPhase::RespawnsAndWorld, bInside); }

    FLDCaptureEvent FinalPresence(const FLDCombatBodyRef& Body, int64 Ms, bool bInside)
    { return FinalFact(ELDCaptureEventKind::Presence, Body, Ms, ELDCommitPhase::Motion, bInside); }

    FLDCaptureEvent FinalDied(const FLDCombatBodyRef& Body, int64 Ms, const FGuid& Killer = FGuid())
    {
        FLDCaptureEvent Event = FinalFact(ELDCaptureEventKind::Died, Body, Ms, ELDCommitPhase::DeathsAndRewards);
        Event.Killer = Killer;
        return Event;
    }

    struct FFinalProbe
    {
        FAutomationTestBase& Test;
        FLDCaptureReducer Reducer;
        FString Error;

        bool Add(const FLDCaptureEvent& Event, const TCHAR* What) { return Test.TestTrue(What, Reducer.Enqueue(Event, Error)); }

        TOptional<FLDCaptureProposal> Close(int64 AtMs, const TCHAR* What)
        {
            TOptional<FLDCaptureProposal> Proposal;
            if (!Reducer.CloseBoundary(AtMs, Proposal, Error)) { Test.AddError(FString::Printf(TEXT("%s: %s"), What, *Error)); }
            return Proposal;
        }

        void ExpectNoWin(int64 AtMs, const TCHAR* What) { Test.TestFalse(What, Close(AtMs, What).IsSet()); }

        void ExpectWin(int64 AtMs, ELDCaptureWin Kind, const FGuid& Winner, int64 DueAtMs, const TCHAR* What)
        {
            const TOptional<FLDCaptureProposal> Proposal = Close(AtMs, What);
            Test.TestTrue(What, Proposal.IsSet() && Proposal->Kind == Kind && Proposal->Winner == Winner && Proposal->DueAtMs == DueAtMs);
        }
    };

    FLDCaptureFinalRules FinalRules(const FGuid& Holder, ELDDemoFinalModId Mod)
    {
        FLDCaptureFinalRules Rules;
        Rules.Mods.Add(Holder, Mod);
        Rules.HitAndRunSprays = 2;
        Rules.HitAndRunFadeMs = 400;
        Rules.KillGuardMs = 400;
        return Rules;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDFinalModHitAndRunTest, "Lockdown.Rules.DemoFinalMod.HitAndRun",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDFinalModHitAndRunTest::RunTest(const FString& Parameters)
{
    const FGuid H = FGuid::NewGuid(), B = FGuid::NewGuid();
    const FLDCombatBodyRef BodyH = FinalBody(H, 1), BodyB = FinalBody(B, 1);
    {
        FFinalProbe Probe{*this};
        TestTrue(TEXT("The reducer starts"), Probe.Reducer.Begin(1000, 350, 2000, Probe.Error)
            && Probe.Reducer.SetFinalRules(FinalRules(H, ELDDemoFinalModId::FinalHitAndRun), Probe.Error));
        Probe.Add(FinalJoined(BodyH, true), TEXT("H joins inside"));
        Probe.Add(FinalJoined(BodyB, false), TEXT("B joins outside"));
        Probe.ExpectNoWin(1000, TEXT("The opening starts H's spray"));
        Probe.ExpectNoWin(1350, TEXT("The first spray is no win"));
        TestTrue(TEXT("A hit-and-run graffiti has no guard and fades at its approved time"),
            Probe.Reducer.GetGraffitiOwner() == H && Probe.Reducer.GetFadeAtMs() == 1750 && Probe.Reducer.GetGuardDueAtMs() == 0
            && Probe.Reducer.GetGuardKeptMs() == 0 && Probe.Reducer.NextDueMs() == 1750 && Probe.Reducer.GetCompletedSprays(H) == 1);
        Probe.ExpectNoWin(1750, TEXT("The graffiti fades"));
        TestTrue(TEXT("The faded flagpole is bare and H, still inside, cannot spray again"),
            !Probe.Reducer.GetGraffitiOwner().IsValid() && Probe.Reducer.GetFadeAtMs() == 0
            && Probe.Reducer.GetState() != ELDCircleState::Solo && !Probe.Reducer.GetOccupier().IsValid());
        Probe.Add(FinalPresence(BodyH, 1800, false), TEXT("H leaves the zone"));
        Probe.ExpectNoWin(1800, TEXT("Leaving rearms H"));
        Probe.Add(FinalPresence(BodyH, 1900, true), TEXT("H comes back"));
        Probe.ExpectNoWin(1900, TEXT("H sprays again"));
        TestTrue(TEXT("The second spray runs from zero"), Probe.Reducer.GetState() == ELDCircleState::Solo
            && Probe.Reducer.GetOccupier() == H && Probe.Reducer.GetDueAtMs() == 2250);
        Probe.ExpectWin(2250, ELDCaptureWin::Sprays, H, 2250, TEXT("The approved spray count wins at the spray instant"));
    }
    {
        FFinalProbe Probe{*this};
        TestTrue(TEXT("A second reducer starts"), Probe.Reducer.Begin(1000, 350, 2000, Probe.Error)
            && Probe.Reducer.SetFinalRules(FinalRules(H, ELDDemoFinalModId::FinalHitAndRun), Probe.Error));
        Probe.Add(FinalJoined(BodyH, true), TEXT("H joins inside"));
        Probe.Add(FinalJoined(BodyB, false), TEXT("B joins outside"));
        Probe.ExpectNoWin(1000, TEXT("Opening"));
        Probe.ExpectNoWin(1350, TEXT("H sprays"));
        Probe.Add(FinalPresence(BodyH, 1400, false), TEXT("H runs"));
        Probe.Add(FinalPresence(BodyB, 1400, true), TEXT("B comes in"));
        Probe.ExpectNoWin(1400, TEXT("B sprays over the fading graffiti"));
        Probe.ExpectNoWin(1750, TEXT("B's spray lands at H's fade instant"));
        TestTrue(TEXT("A fading graffiti is covered normally and B's guard runs"), Probe.Reducer.GetGraffitiOwner() == B
            && Probe.Reducer.GetFadeAtMs() == 0 && Probe.Reducer.GetGuardDueAtMs() == 3750);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDFinalModClearOutTest, "Lockdown.Rules.DemoFinalMod.ClearOut",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDFinalModClearOutTest::RunTest(const FString& Parameters)
{
    const FGuid H = FGuid::NewGuid(), B = FGuid::NewGuid(), C = FGuid::NewGuid();
    const FLDCombatBodyRef BodyH = FinalBody(H, 1), BodyB = FinalBody(B, 1), BodyC = FinalBody(C, 1), BodyB2 = FinalBody(B, 2);
    {
        FFinalProbe Probe{*this};
        TestTrue(TEXT("The reducer starts"), Probe.Reducer.Begin(1000, 350, 2000, Probe.Error)
            && Probe.Reducer.SetFinalRules(FinalRules(H, ELDDemoFinalModId::FinalClearOut), Probe.Error));
        for (const FLDCombatBodyRef& Body : {BodyH, BodyB, BodyC}) { Probe.Add(FinalJoined(Body, false), TEXT("Joins outside")); }
        Probe.Add(FinalDied(BodyB, 500), TEXT("B dies before the unlock"));
        Probe.Add(FinalDied(BodyC, 600), TEXT("C dies before the unlock"));
        Probe.ExpectNoWin(999, TEXT("Nobody clears the field before the flagpole unlocks"));
        Probe.ExpectWin(1000, ELDCaptureWin::ClearOut, H, 1000, TEXT("Everyone else dead at the unlock is a clear-out"));
    }
    {
        FFinalProbe Probe{*this};
        TestTrue(TEXT("A second reducer starts"), Probe.Reducer.Begin(1000, 350, 2000, Probe.Error)
            && Probe.Reducer.SetFinalRules(FinalRules(H, ELDDemoFinalModId::FinalClearOut), Probe.Error));
        for (const FLDCombatBodyRef& Body : {BodyH, BodyB, BodyC}) { Probe.Add(FinalJoined(Body, false), TEXT("Joins outside")); }
        Probe.ExpectNoWin(1000, TEXT("Opening"));
        Probe.Add(FinalDied(BodyB, 1100), TEXT("B dies"));
        Probe.ExpectNoWin(1100, TEXT("One rival standing is no clear-out"));
        // C dies at the very instant B comes back under protection: a protected body is alive.
        Probe.Add(FinalDied(BodyC, 1200), TEXT("C dies"));
        Probe.Add(FinalFact(ELDCaptureEventKind::Respawned, BodyB2, 1200, ELDCommitPhase::RespawnsAndWorld, false, 3000),
            TEXT("B respawns protected at the same instant"));
        Probe.ExpectNoWin(1200, TEXT("A protected respawn counts as alive"));
        Probe.Add(FinalDied(BodyB2, 1300), TEXT("B dies again"));
        Probe.ExpectWin(1300, ELDCaptureWin::ClearOut, H, 1300, TEXT("Every other contender dead at once wins for the living holder"));
    }
    {
        FFinalProbe Probe{*this};
        TestTrue(TEXT("A third reducer starts"), Probe.Reducer.Begin(1000, 350, 2000, Probe.Error)
            && Probe.Reducer.SetFinalRules(FinalRules(H, ELDDemoFinalModId::FinalClearOut), Probe.Error));
        for (const FLDCombatBodyRef& Body : {BodyH, BodyB, BodyC}) { Probe.Add(FinalJoined(Body, false), TEXT("Joins outside")); }
        Probe.ExpectNoWin(1000, TEXT("Opening"));
        Probe.Add(FinalDied(BodyH, 1100), TEXT("The holder dies first"));
        Probe.Add(FinalDied(BodyB, 1200), TEXT("B dies"));
        Probe.Add(FinalDied(BodyC, 1300), TEXT("C dies"));
        Probe.ExpectNoWin(1300, TEXT("A dead holder clears nothing"));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCaptureGuardCheckpointTest, "Lockdown.Rules.DemoCapture.GuardCheckpoint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCaptureGuardCheckpointTest::RunTest(const FString& Parameters)
{
    // An owner painted over after guarding past the checkpoint starts his next own graffiti from the resume guard, once.
    const FGuid A = FGuid::NewGuid(), B = FGuid::NewGuid();
    const FLDCombatBodyRef BodyA = FinalBody(A, 1), BodyB = FinalBody(B, 1);
    FFinalProbe Probe{*this};
    FLDCaptureFinalRules Rules;
    Rules.GuardCheckpointMs = 500;
    Rules.GuardResumeMs = 400;
    TestTrue(TEXT("The reducer starts with a checkpoint"), Probe.Reducer.Begin(1000, 350, 1000, Probe.Error)
        && Probe.Reducer.SetFinalRules(Rules, Probe.Error));
    Probe.Add(FinalJoined(BodyA, true), TEXT("A joins inside"));
    Probe.Add(FinalJoined(BodyB, false), TEXT("B joins outside"));
    Probe.ExpectNoWin(1000, TEXT("Opening"));
    Probe.ExpectNoWin(1350, TEXT("A's graffiti lands"));
    Probe.Add(FinalPresence(BodyB, 1900, true), TEXT("B enters after A guarded 550"));
    Probe.ExpectNoWin(1900, TEXT("B freezes A's guard"));
    Probe.Add(FinalPresence(BodyA, 1950, false), TEXT("A leaves"));
    Probe.ExpectNoWin(1950, TEXT("B sprays alone"));
    Probe.ExpectNoWin(2300, TEXT("B paints over A's graffiti"));
    Probe.Add(FinalPresence(BodyB, 2400, false), TEXT("B leaves"));
    Probe.Add(FinalPresence(BodyA, 2400, true), TEXT("A comes back"));
    Probe.ExpectNoWin(2400, TEXT("A sprays again"));
    Probe.ExpectNoWin(2750, TEXT("A's graffiti lands again"));
    TestEqual(TEXT("A's guard starts from the checkpoint's resume guard"), Probe.Reducer.GetGuardDueAtMs(), int64(2750 + 1000 - 400));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDFinalModKillSecondsTest, "Lockdown.Rules.DemoFinalMod.KillSeconds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDFinalModKillSecondsTest::RunTest(const FString& Parameters)
{
    const FGuid K = FGuid::NewGuid(), B = FGuid::NewGuid(), C = FGuid::NewGuid(), D = FGuid::NewGuid();
    const FLDCombatBodyRef BodyK = FinalBody(K, 1), BodyB = FinalBody(B, 1), BodyC = FinalBody(C, 1), BodyD = FinalBody(D, 1);
    FFinalProbe Probe{*this};
    TestTrue(TEXT("The reducer starts"), Probe.Reducer.Begin(1000, 350, 1000, Probe.Error)
        && Probe.Reducer.SetFinalRules(FinalRules(K, ELDDemoFinalModId::FinalKillSeconds), Probe.Error));
    Probe.Add(FinalJoined(BodyK, false), TEXT("K joins outside"));
    for (const FLDCombatBodyRef& Body : {BodyB, BodyC, BodyD}) { Probe.Add(FinalJoined(Body, false), TEXT("Joins outside")); }
    Probe.ExpectNoWin(1000, TEXT("Opening"));
    Probe.Add(FinalDied(BodyD, 1100, K), TEXT("K kills D without graffiti"));
    Probe.ExpectNoWin(1100, TEXT("A kill without graffiti only banks"));
    TestEqual(TEXT("A kill banks its guard in the historical contribution"), Probe.Reducer.GetBankedGuardMs(K), int64(400));
    Probe.Add(FinalPresence(BodyK, 1200, true), TEXT("K enters"));
    Probe.ExpectNoWin(1200, TEXT("K sprays"));
    Probe.ExpectNoWin(1550, TEXT("K's graffiti lands"));
    TestEqual(TEXT("K's guard runs to its full duration"), Probe.Reducer.GetGuardDueAtMs(), int64(2550));
    Probe.Add(FinalDied(BodyB, 1700, K), TEXT("K kills B while guarding"));
    Probe.ExpectNoWin(1700, TEXT("The kill moves the running guard"));
    TestTrue(TEXT("The kill adds its guard to the current and the historical guard"),
        Probe.Reducer.GetGuardDueAtMs() == 2150 && Probe.Reducer.GetBankedGuardMs(K) == 400 + 150 + 400);
    Probe.Add(FinalPresence(BodyK, 1800, false), TEXT("K leaves"));
    Probe.ExpectNoWin(1800, TEXT("Leaving pauses the guard"));
    TestEqual(TEXT("The paused guard keeps its progress with the kill"), Probe.Reducer.GetGuardKeptMs(), int64(1800 - (2150 - 1000)));
    Probe.Add(FinalDied(BodyC, 1900, K), TEXT("K kills C away from the zone"));
    Probe.ExpectWin(1900, ELDCaptureWin::Guard, K, 1900, TEXT("A kill that fills the guard wins at the kill instant"));
    // A killer who dies at the very instant of his filling kill wins nothing, in either order of the two deaths.
    for (const bool bKillerFirst : {false, true})
    {
        FFinalProbe Trade{*this};
        const FLDCombatBodyRef Holder = FinalBody(K, 1), Victim = FinalBody(B, 1), Other = FinalBody(C, 1);
        TestTrue(TEXT("A trade reducer starts"), Trade.Reducer.Begin(1000, 350, 1000, Trade.Error)
            && Trade.Reducer.SetFinalRules(FinalRules(K, ELDDemoFinalModId::FinalKillSeconds), Trade.Error));
        Trade.Add(FinalJoined(Holder, true), TEXT("K joins inside"));
        Trade.Add(FinalJoined(Victim, false), TEXT("B joins outside"));
        Trade.Add(FinalJoined(Other, false), TEXT("C joins outside"));
        Trade.ExpectNoWin(1000, TEXT("Opening"));
        Trade.ExpectNoWin(1350, TEXT("K's graffiti lands; its guard is due at 2350"));
        Trade.ExpectNoWin(2000, TEXT("K guards to 650"));
        if (bKillerFirst) { Trade.Add(FinalDied(Holder, 2100, B), TEXT("B kills K")); }
        Trade.Add(FinalDied(Victim, 2100, K), TEXT("K's kill fills his guard"));
        if (!bKillerFirst) { Trade.Add(FinalDied(Holder, 2100, B), TEXT("B kills K at the same instant")); }
        Trade.ExpectNoWin(2100, bKillerFirst ? TEXT("A killer dead first at the instant wins nothing")
            : TEXT("A killer dead later at the same instant wins nothing"));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDFinalModUsurpTest, "Lockdown.Rules.DemoFinalMod.Usurp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDFinalModUsurpTest::RunTest(const FString& Parameters)
{
    const FGuid A = FGuid::NewGuid(), U = FGuid::NewGuid(), B = FGuid::NewGuid();
    const FLDCombatBodyRef BodyA = FinalBody(A, 1), BodyU = FinalBody(U, 1), BodyB = FinalBody(B, 1);
    FFinalProbe Probe{*this};
    TestTrue(TEXT("The reducer starts"), Probe.Reducer.Begin(1000, 350, 2000, Probe.Error)
        && Probe.Reducer.SetFinalRules(FinalRules(U, ELDDemoFinalModId::FinalUsurp), Probe.Error));
    Probe.Add(FinalJoined(BodyA, true), TEXT("A joins inside"));
    Probe.Add(FinalJoined(BodyU, false), TEXT("U joins outside"));
    Probe.Add(FinalJoined(BodyB, false), TEXT("B joins outside"));
    Probe.ExpectNoWin(1000, TEXT("Opening"));
    Probe.ExpectNoWin(1350, TEXT("A sprays"));
    Probe.Add(FinalDied(BodyB, 1400, U), TEXT("U kills a non-owner"));
    Probe.ExpectNoWin(1400, TEXT("Killing someone else takes nothing"));
    TestTrue(TEXT("The graffiti stays A's, sprayed"), Probe.Reducer.GetGraffitiOwner() == A
        && Probe.Reducer.GetGraffitiCause() == ELDGraffitiCause::Sprayed);
    Probe.Add(FinalDied(BodyA, 1600, U), TEXT("U kills the owner"));
    Probe.ExpectNoWin(1600, TEXT("The owner's death hands the graffiti over"));
    TestTrue(TEXT("The usurper owns it with the inherited progress, paused while he is outside"),
        Probe.Reducer.GetGraffitiOwner() == U && Probe.Reducer.GetGraffitiCause() == ELDGraffitiCause::Usurped
        && Probe.Reducer.GetState() == ELDCircleState::Empty
        && Probe.Reducer.GetGuardKeptMs() == 250 && Probe.Reducer.GetBankedGuardMs(A) == 250);
    Probe.Add(FinalPresence(BodyU, 1700, true), TEXT("U enters alone"));
    Probe.ExpectNoWin(1700, TEXT("U guards"));
    TestEqual(TEXT("The inherited guard continues"), Probe.Reducer.GetGuardDueAtMs(), int64(1700 + 2000 - 250));
    Probe.ExpectWin(3450, ELDCaptureWin::Guard, U, 3450, TEXT("The usurped guard wins at its full duration"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDFinalModPrivatePoleTest, "Lockdown.Rules.DemoFinalMod.PrivatePole",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDFinalModPrivatePoleTest::RunTest(const FString& Parameters)
{
    const FGuid H = FGuid::NewGuid(), O = FGuid::NewGuid();
    const FLDCombatBodyRef BodyH = FinalBody(H, 1), BodyO = FinalBody(O, 1);
    FFinalProbe Main{*this};
    TestTrue(TEXT("The main reducer starts unscheduled"), Main.Reducer.Begin(MAX_int64, 350, 2000, Main.Error));
    Main.Add(FinalJoined(BodyH, false), TEXT("H joins"));
    Main.Add(FinalJoined(BodyO, false), TEXT("O joins"));
    TestTrue(TEXT("The final field schedules the opening"), Main.Reducer.ScheduleOpen(1000, Main.Error));
    FFinalProbe Private{*this};
    TestTrue(TEXT("The private pole starts from the main roster"), Private.Reducer.BeginPrivate(Main.Reducer, H, 300, 1000, Private.Error)
        && Private.Reducer.GetExclusiveSprayer() == H && Private.Reducer.GetOpenAtMs() == 1000);
    FLDCaptureReducer Stranger;
    TestFalse(TEXT("Only a member of the roster can hold a private pole"),
        Stranger.BeginPrivate(Main.Reducer, FGuid::NewGuid(), 300, 1000, Private.Error));
    Private.Add(FinalPresence(BodyO, 500, true), TEXT("O waits inside H's pole"));
    Private.ExpectNoWin(1000, TEXT("Opening"));
    TestTrue(TEXT("Nobody but the holder sprays it"), Private.Reducer.GetState() != ELDCircleState::Solo
        && !Private.Reducer.GetOccupier().IsValid());
    Private.Add(FinalPresence(BodyO, 1050, false), TEXT("O leaves"));
    Private.Add(FinalPresence(BodyH, 1050, true), TEXT("H enters his pole"));
    Private.ExpectNoWin(1050, TEXT("H sprays"));
    TestTrue(TEXT("The holder sprays with the private duration"), Private.Reducer.GetState() == ELDCircleState::Solo
        && Private.Reducer.GetOccupier() == H && Private.Reducer.GetDueAtMs() == 1350);
    Private.ExpectNoWin(1350, TEXT("H's graffiti lands"));
    TestEqual(TEXT("The private guard runs"), Private.Reducer.GetGuardDueAtMs(), int64(2350));
    Private.Add(FinalPresence(BodyO, 1500, true), TEXT("O walks in"));
    Private.ExpectNoWin(1500, TEXT("Anyone else inside freezes it"));
    TestTrue(TEXT("The guard is frozen with its progress"), Private.Reducer.GetGuardDueAtMs() == 0 && Private.Reducer.GetGuardKeptMs() == 150);
    Private.Add(FinalPresence(BodyO, 1600, false), TEXT("O leaves"));
    Private.ExpectNoWin(1600, TEXT("Alone again, H's guard resumes"));
    Private.ExpectWin(2450, ELDCaptureWin::Guard, H, 2450, TEXT("A full private guard wins"));
    TestTrue(TEXT("The private pole never touches the main flagpole's contribution"),
        Main.Reducer.GetBankedGuardMs(H) == 0 && Main.Reducer.GetCompletedSprays(H) == 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDFinalModSettlementTest, "Lockdown.Rules.DemoFinalMod.SameInstantSettlement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDFinalModSettlementTest::RunTest(const FString& Parameters)
{
    const FGuid A = FGuid::NewGuid(), B = FGuid::NewGuid();
    const auto Win = [](ELDCaptureWin Kind, const FGuid& Winner, int64 AtMs)
    {
        FLDCaptureProposal Row;
        Row.Kind = Kind;
        Row.Winner = Winner;
        Row.DueAtMs = AtMs;
        return Row;
    };
    TOptional<FLDCaptureProposal> Decided = FLDCaptureReducer::Settle({Win(ELDCaptureWin::ClearOut, A, 900), Win(ELDCaptureWin::Guard, B, 1000)});
    TestTrue(TEXT("The earliest win settles first, whatever its kind"), Decided.IsSet() && Decided->Winner == A && Decided->Kind == ELDCaptureWin::ClearOut);
    Decided = FLDCaptureReducer::Settle({Win(ELDCaptureWin::Sprays, A, 1000), Win(ELDCaptureWin::Guard, B, 1000)});
    TestTrue(TEXT("At one instant a full guard beats a spray count"), Decided.IsSet() && Decided->Winner == B);
    Decided = FLDCaptureReducer::Settle({Win(ELDCaptureWin::ClearOut, A, 1000), Win(ELDCaptureWin::Sprays, B, 1000)});
    TestTrue(TEXT("At one instant a spray count beats a clear-out"), Decided.IsSet() && Decided->Winner == B);
    Decided = FLDCaptureReducer::Settle({Win(ELDCaptureWin::Guard, A, 1000), Win(ELDCaptureWin::Guard, B, 1000),
        Win(ELDCaptureWin::Sprays, B, 1000)});
    TestTrue(TEXT("Two contenders tied in the best condition leave no winner"), Decided.IsSet() && !Decided->Winner.IsValid()
        && Decided->DueAtMs == 1000);
    Decided = FLDCaptureReducer::Settle({Win(ELDCaptureWin::Guard, A, 1000), Win(ELDCaptureWin::Guard, A, 1000)});
    TestTrue(TEXT("The same contender reaching one condition twice still wins"), Decided.IsSet() && Decided->Winner == A);
    TestFalse(TEXT("No candidate decides nothing"), FLDCaptureReducer::Settle({}).IsSet());

    // A kill that fills one guard and a guard due at the same instant: both are Guard wins of different contenders.
    const FGuid K = FGuid::NewGuid(), V = FGuid::NewGuid();
    const FLDCombatBodyRef BodyK = FinalBody(K, 1), BodyV = FinalBody(V, 1), BodyA = FinalBody(A, 1);
    FFinalProbe Probe{*this};
    TestTrue(TEXT("The reducer starts"), Probe.Reducer.Begin(1000, 350, 1000, Probe.Error)
        && Probe.Reducer.SetFinalRules(FinalRules(K, ELDDemoFinalModId::FinalKillSeconds), Probe.Error));
    Probe.Add(FinalJoined(BodyA, true), TEXT("A joins inside"));
    Probe.Add(FinalJoined(BodyK, false), TEXT("K joins outside"));
    Probe.Add(FinalJoined(BodyV, false), TEXT("V joins outside"));
    Probe.ExpectNoWin(1000, TEXT("Opening"));
    Probe.ExpectNoWin(1350, TEXT("A sprays; its guard is due at 2350"));
    Probe.Add(FinalDied(BodyV, 2350, K), TEXT("K's kill lands at A's guard instant"));
    Probe.ExpectWin(2350, ELDCaptureWin::Guard, A, 2350, TEXT("The owner's guard is due at the boundary; K holds no graffiti"));

    // Two flagpoles whose guards both resume inside one step and complete at the same instant inside it: each closes
    // through its own completion and the shared instant ties (no winner) instead of faulting.
    const FGuid P = FGuid::NewGuid(), O = FGuid::NewGuid();
    const FLDCombatBodyRef BodyP = FinalBody(P, 1), BodyO = FinalBody(O, 1), BodyM = FinalBody(A, 1);
    FFinalProbe Main{*this};
    TestTrue(TEXT("The main flagpole starts"), Main.Reducer.Begin(MAX_int64, 300, 1000, Main.Error));
    Main.Add(FinalJoined(BodyM, false), TEXT("M joins"));
    Main.Add(FinalJoined(BodyP, false), TEXT("P joins"));
    Main.Add(FinalJoined(BodyO, false), TEXT("O joins"));
    TestTrue(TEXT("Opening scheduled"), Main.Reducer.ScheduleOpen(1000, Main.Error));
    FFinalProbe Private{*this};
    TestTrue(TEXT("P's private pole starts"), Private.Reducer.BeginPrivate(Main.Reducer, P, 300, 1000, Private.Error));
    Main.Add(FinalPresence(BodyM, 1000, true), TEXT("M enters the main zone"));
    Private.Add(FinalPresence(BodyP, 1000, true), TEXT("P enters his pole"));
    Main.ExpectNoWin(1000, TEXT("M sprays"));
    Private.ExpectNoWin(1000, TEXT("P sprays"));
    Main.ExpectNoWin(1300, TEXT("M's graffiti"));
    Private.ExpectNoWin(1300, TEXT("P's graffiti"));
    Main.Add(FinalPresence(BodyO, 1400, true), TEXT("O freezes M"));
    Private.Add(FinalPresence(BodyO, 1400, true), TEXT("O is also in P's pole"));
    Main.ExpectNoWin(1400, TEXT("Frozen"));
    Private.ExpectNoWin(1400, TEXT("Frozen"));
    Main.Add(FinalPresence(BodyO, 2000, false), TEXT("O leaves both"));
    Private.Add(FinalPresence(BodyO, 2000, false), TEXT("O leaves both"));
    const int64 Inside = 2000 + 1000 - 100;
    TestTrue(TEXT("Both guards complete at the same instant inside the step"),
        Main.Reducer.CompletionInsideMs(3000) == Inside && Private.Reducer.CompletionInsideMs(3000) == Inside);
    TArray<FLDCaptureProposal> Candidates;
    for (FFinalProbe* Pole : {&Main, &Private})
    {
        TOptional<FLDCaptureProposal> Proposal;
        TestTrue(TEXT("Each closes through its own completion"), Pole->Reducer.CloseThroughCompletion(3000, Proposal, Pole->Error));
        if (Proposal) { Candidates.Add(Proposal.GetValue()); }
    }
    const TOptional<FLDCaptureProposal> Shared = FLDCaptureReducer::Settle(Candidates);
    TestTrue(TEXT("Simultaneous full guards on two flagpoles tie"), Candidates.Num() == 2 && Shared.IsSet()
        && Shared->DueAtMs == Inside && !Shared->Winner.IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDFinalModZoneTest, "Lockdown.Rules.DemoFinalMod.ZoneGeometry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDFinalModZoneTest::RunTest(const FString& Parameters)
{
    FLDFinalZoneInput In;
    In.CurrentCm = FVector2D(100, 100);
    In.BaseRadiusCm = 300;
    In.DoubleYellowRadiusCm = 180;
    FVector2D Centre;
    double Radius = 0;
    LDCaptureCircle::FinalZone(In, Centre, Radius);
    TestTrue(TEXT("Without a card the zone stays"), Centre == In.CurrentCm && Radius == 300);
    In.OwnerMod = uint8(ELDDemoFinalModId::FinalCarryFlag);
    In.OwnerFootCm = FVector2D(400, -50);
    LDCaptureCircle::FinalZone(In, Centre, Radius);
    TestTrue(TEXT("A living carrier takes the flagpole to his foot"), Centre == FVector2D(400, -50) && Radius == 300);
    In.bCageStanding = true;
    LDCaptureCircle::FinalZone(In, Centre, Radius);
    TestTrue(TEXT("A standing fence holds the carried flagpole"), Centre == In.CurrentCm);
    In.bCageStanding = false;
    In.OwnerFootCm.Reset();
    LDCaptureCircle::FinalZone(In, Centre, Radius);
    TestTrue(TEXT("A dead carrier leaves the flagpole where it stands"), Centre == In.CurrentCm);
    In.OwnerMod = uint8(ELDDemoFinalModId::FinalDoubleYellow);
    In.OwnerFootCm = FVector2D(400, -50);
    LDCaptureCircle::FinalZone(In, Centre, Radius);
    TestTrue(TEXT("A double-yellow owner shrinks the zone without moving it"), Centre == In.CurrentCm && Radius == 180);
    In.OwnerMod = 0;
    LDCaptureCircle::FinalZone(In, Centre, Radius);
    TestEqual(TEXT("Another owner restores the approved radius"), Radius, 300.);

    // A zone away from its anchor counts any natively grounded floor; at the anchor only the registered capture ground.
    FLDCaptureCircle Circle;
    Circle.CaptureId = TEXT("final_capture");
    Circle.RadiusCm = 300;
    UBoxComponent* Floor = NewObject<UBoxComponent>(GetTransientPackage());
    UBoxComponent* Other = NewObject<UBoxComponent>(GetTransientPackage());
    Circle.Ground = Floor;
    FLDNativeSupportSample OnOther;
    OnOther.Grounded = true;
    OnOther.Floor = Other;
    TestFalse(TEXT("The anchored zone needs its registered ground"), Circle.Contains(FVector(10, 0, 0), OnOther));
    Circle.bAnyGround = true;
    TestTrue(TEXT("A moved or private zone accepts any grounded floor"), Circle.Contains(FVector(10, 0, 0), OnOther));
    OnOther.Grounded = false;
    TestFalse(TEXT("An airborne foot is never inside"), Circle.Contains(FVector(10, 0, 0), OnOther));
    return true;
}
#endif
