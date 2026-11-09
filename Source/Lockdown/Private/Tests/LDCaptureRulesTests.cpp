#include "Framework/LDCaptureReducer.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Framework/LDGameInstance.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDMatchProviders.h"
#include "Framework/LDPlayerController.h"
#include "World/LDCaptureCircle.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    FLDCombatBodyRef CaptureTestBody(const FGuid& Id, uint32 Life)
    {
        return {{ELDVictimKind::Contender, Id, Life}, FGuid::NewGuid()};
    }

    FLDCaptureEventTime CaptureMs(int64 Value)
    {
        FLDCaptureEventTime Time;
        FString Error;
        verify(FLDCaptureEventTime::FromMs(Value, Time, Error));
        return Time;
    }

    FLDCaptureEventTime CaptureSeconds(double Value)
    {
        FLDCaptureEventTime Time;
        FString Error;
        verify(FLDCaptureEventTime::FromSeconds(Value, Time, Error));
        return Time;
    }

    FLDCaptureEvent CaptureFact(ELDCaptureEventKind Kind, const FLDCombatBodyRef& Body, const FLDCaptureEventTime& Time,
        ELDCommitPhase Phase, bool Inside = false, int64 ProtectedUntilMs = 0)
    {
        FLDCaptureEvent Event;
        Event.Kind = Kind;
        Event.Body = Body;
        Event.Time = Time;
        Event.Phase = Phase;
        Event.bInside = Inside;
        Event.ProtectedUntilMs = ProtectedUntilMs;
        return Event;
    }

    FLDCaptureEvent CapturePresence(const FLDCombatBodyRef& Body, const FLDCaptureEventTime& Time, bool Inside)
    { return CaptureFact(ELDCaptureEventKind::Presence, Body, Time, ELDCommitPhase::Motion, Inside); }

    FLDCaptureEvent CaptureJoined(const FLDCombatBodyRef& Body, bool Inside)
    { return CaptureFact(ELDCaptureEventKind::Joined, Body, CaptureMs(0), ELDCommitPhase::RespawnsAndWorld, Inside); }

    // A root, stun, taunt or knockback committed by Combat's control path at an Effects boundary.
    FLDCaptureEvent CaptureInterrupt(const FLDCombatBodyRef& Body, int64 Ms)
    { return CaptureFact(ELDCaptureEventKind::Interrupted, Body, CaptureMs(Ms), ELDCommitPhase::Effects); }

    struct FCaptureReducerProbe
    {
        FAutomationTestBase& Test;
        FLDCaptureReducer Reducer;
        FString Error;

        bool Add(const FLDCaptureEvent& Event, const TCHAR* What)
        { return Test.TestTrue(What, Reducer.Enqueue(Event, Error)); }

        bool Close(int64 AtMs, bool ExpectProposal, const TCHAR* What)
        {
            TOptional<FLDCaptureProposal> Proposal;
            const bool Closed = Reducer.CloseBoundary(AtMs, Proposal, Error);
            if (!Closed) { Test.AddError(FString::Printf(TEXT("%s: %s"), What, *Error)); return false; }
            return Test.TestEqual(What, Proposal.IsSet(), ExpectProposal);
        }

        void ExpectSolo(const FGuid& Occupier, int64 SoloSinceMs, int64 DueAtMs, const TCHAR* What)
        {
            Test.TestTrue(What, Reducer.GetState() == ELDCircleState::Solo && Reducer.GetOccupier() == Occupier
                && Reducer.GetSoloSinceMs() == SoloSinceMs && Reducer.GetDueAtMs() == DueAtMs
                && Reducer.NextDueMs() == DueAtMs);
        }

        void ExpectGuard(const FGuid& Owner, ELDCircleState State, int64 GuardDueAtMs, int64 GuardKeptMs, const TCHAR* What)
        {
            Test.TestTrue(What, Reducer.GetGraffitiOwner() == Owner && Reducer.GetState() == State
                && Reducer.GetGuardDueAtMs() == GuardDueAtMs && Reducer.GetGuardKeptMs() == GuardKeptMs);
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCaptureSprayGuardTest, "Lockdown.Rules.DemoCapture.SprayAndGuard",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCaptureSprayGuardTest::RunTest(const FString& Parameters)
{
    FCaptureReducerProbe Probe{*this};
    const FGuid A = FGuid::NewGuid(), B = FGuid::NewGuid(), C = FGuid::NewGuid();
    const FLDCombatBodyRef BodyA = CaptureTestBody(A, 1), BodyB = CaptureTestBody(B, 1), BodyC = CaptureTestBody(C, 1);
    TestTrue(TEXT("Approved opening, spray and guard start the reducer"), Probe.Reducer.Begin(1000, 350, 2000, Probe.Error));
    Probe.Add(CaptureJoined(BodyA, true), TEXT("A joins inside"));
    Probe.Add(CaptureJoined(BodyB, false), TEXT("B joins outside"));
    Probe.Add(CaptureJoined(BodyC, false), TEXT("C joins outside"));
    Probe.Close(999, false, TEXT("Development never sprays"));
    TestTrue(TEXT("The closed flagpole waits for its exact opening boundary"),
        Probe.Reducer.GetState() == ELDCircleState::NotOpen && Probe.Reducer.NextDueMs() == 1000);
    Probe.Close(1000, false, TEXT("The opening starts the first spray"));
    Probe.ExpectSolo(A, 1000, 1350, TEXT("A sole effective contender who is not the owner sprays; presence before the opening is not credited"));
    TestFalse(TEXT("No graffiti yet"), Probe.Reducer.GetGraffitiOwner().IsValid() || Probe.Reducer.GetGuardDueAtMs() != 0);

    Probe.Close(1350, false, TEXT("A spray is no win"));
    Probe.ExpectGuard(A, ELDCircleState::Guarded, 3350, 0, TEXT("The spray lands at its full duration and the guard starts from zero"));
    TestTrue(TEXT("The owner no longer sprays and the driver stops at the guard win"),
        !Probe.Reducer.GetOccupier().IsValid() && Probe.Reducer.NextDueMs() == 3350);
    {
        const FLDCaptureDevTally& Tally = Probe.Reducer.DevTally();
        const FLDCaptureDevTally::FHolder* Holder = Tally.Holders.Find(A);
        TestTrue(TEXT("The telemetry tally counts the spray"), Tally.Sprays == 1 && Tally.Repaints == 0
            && Tally.GraffitiSinceMs == 1350 && Holder && Holder->Sprays == 1 && Holder->Attempts == 1);
    }

    Probe.Add(CapturePresence(BodyA, CaptureMs(1500), false), TEXT("The owner walks away"));
    Probe.Close(1500, false, TEXT("Owner away"));
    Probe.ExpectGuard(A, ELDCircleState::Empty, 0, 150, TEXT("The owner away from the empty zone pauses his guard at its value"));
    TestEqual(TEXT("A paused guard adds no driver stop"), Probe.Reducer.NextDueMs(), MAX_int64);

    Probe.Add(CapturePresence(BodyB, CaptureMs(1600), true), TEXT("B enters alone"));
    Probe.Close(1600, false, TEXT("A repaint starts"));
    Probe.ExpectSolo(B, 1600, 1950, TEXT("A sole challenger sprays from zero"));
    Probe.ExpectGuard(A, ELDCircleState::Solo, 0, 150, TEXT("A sole sprayer keeps the paused guard frozen at its value"));
    Probe.Add(CapturePresence(BodyB, CaptureMs(1700), false), TEXT("B gives up"));
    Probe.Close(1700, false, TEXT("A failed repaint"));
    Probe.ExpectGuard(A, ELDCircleState::Empty, 0, 150, TEXT("After a failed repaint the guard stays paused while the owner is away"));

    Probe.Add(CapturePresence(BodyA, CaptureMs(1800), true), TEXT("The owner returns"));
    Probe.Close(1800, false, TEXT("Owner inside"));
    Probe.ExpectGuard(A, ELDCircleState::Guarded, 3650, 0, TEXT("The owner alone in the zone does not spray and his guard resumes from its kept value"));
    Probe.Add(CapturePresence(BodyB, CaptureMs(1900), true), TEXT("B joins the owner"));
    Probe.Close(1900, false, TEXT("Owner and challenger"));
    Probe.ExpectGuard(A, ELDCircleState::Contested, 0, 250, TEXT("A challenger beside the owner freezes the guard"));
    TestTrue(TEXT("A challenger beside the owner holds no progress and the frozen guard adds no stop"),
        !Probe.Reducer.GetOccupier().IsValid() && Probe.Reducer.NextDueMs() == MAX_int64);
    Probe.Add(CapturePresence(BodyA, CaptureMs(2000), false), TEXT("The owner leaves the challenger alone"));
    Probe.Close(2000, false, TEXT("Challenger alone"));
    Probe.ExpectSolo(B, 2000, 2350, TEXT("The challenger sprays as soon as he is alone"));
    Probe.ExpectGuard(A, ELDCircleState::Solo, 0, 0, TEXT("The owner leaving under a challenger resets his frozen guard; the graffiti stays"));

    Probe.Close(2350, false, TEXT("The repaint lands"));
    Probe.ExpectGuard(B, ELDCircleState::Guarded, 4350, 0, TEXT("A repaint covers the old graffiti and starts the new owner's guard from zero"));
    TestTrue(TEXT("The tally counts the repaint"), Probe.Reducer.DevTally().Sprays == 2 && Probe.Reducer.DevTally().Repaints == 1
        && Probe.Reducer.DevTally().GraffitiSinceMs == 2350);

    FCaptureReducerProbe Dies = Probe;
    Dies.Add(CaptureFact(ELDCaptureEventKind::Died, BodyB, CaptureMs(4350), ELDCommitPhase::DeathsAndRewards),
        TEXT("The owner dies at the win instant"));
    Dies.Close(4350, false, TEXT("A death at the same instant is settled before the guard"));
    Dies.ExpectGuard(FGuid(), ELDCircleState::Empty, 0, 0, TEXT("The dead owner's graffiti is cleared and nobody wins"));
    TestEqual(TEXT("The tally names the clearing cause"), Dies.Reducer.DevTally().ClearedByDeath, uint32(1));
    FCaptureReducerProbe Skipped = Probe;
    TOptional<FLDCaptureProposal> Proposal;
    TestFalse(TEXT("A driver that skips the guard win fails loudly"),
        Skipped.Reducer.CloseBoundary(4351, Proposal, Skipped.Error) || Proposal.IsSet());

    Probe.Close(4349, false, TEXT("Quiet boundary before the win"));
    TestTrue(TEXT("The guard win boundary closes"), Probe.Reducer.CloseBoundary(4350, Proposal, Probe.Error));
    TestTrue(TEXT("A guard that reached its full duration proposes its owner"),
        Proposal.IsSet() && Proposal->Winner == B && Proposal->GuardSinceMs == 2350 && Proposal->DueAtMs == 4350
        && Proposal->Epoch == Probe.Reducer.GetEpoch());
    (void)BodyC;
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCaptureGuardFreezeTest, "Lockdown.Rules.DemoCapture.GuardFreeze",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCaptureGuardFreezeTest::RunTest(const FString& Parameters)
{
    // 2026-11-07 user decision: anyone but the owner in the zone freezes the guard; it continues once the owner is alone
    // in the zone again, and the owner leaving the zone while a challenger stays in it resets it to zero without
    // clearing the graffiti. An empty zone only pauses it.
    const FGuid A = FGuid::NewGuid(), B = FGuid::NewGuid(), C = FGuid::NewGuid();
    const FLDCombatBodyRef BodyA = CaptureTestBody(A, 1), BodyB = CaptureTestBody(B, 1), BodyC = CaptureTestBody(C, 1);
    FCaptureReducerProbe Probe{*this};
    TestTrue(TEXT("Reducer begins"), Probe.Reducer.Begin(0, 500, 2000, Probe.Error));
    Probe.Add(CaptureJoined(BodyA, true), TEXT("A joins inside"));
    Probe.Add(CaptureJoined(BodyB, false), TEXT("B joins outside"));
    Probe.Add(CaptureJoined(BodyC, false), TEXT("C joins outside"));
    Probe.Close(0, false, TEXT("Open"));
    Probe.Close(500, false, TEXT("A sprays"));
    Probe.ExpectGuard(A, ELDCircleState::Guarded, 2500, 0, TEXT("A guards from zero"));

    Probe.Add(CapturePresence(BodyB, CaptureMs(600), true), TEXT("B enters beside the owner"));
    Probe.Close(600, false, TEXT("Owner and challenger"));
    Probe.ExpectGuard(A, ELDCircleState::Contested, 0, 100, TEXT("A contest with the owner inside freezes the guard"));
    Probe.Close(900, false, TEXT("A long contest"));
    Probe.ExpectGuard(A, ELDCircleState::Contested, 0, 100, TEXT("The frozen guard does not advance"));
    TestEqual(TEXT("A frozen guard adds no driver stop"), Probe.Reducer.NextDueMs(), MAX_int64);
    Probe.Add(CapturePresence(BodyB, CaptureMs(1000), false), TEXT("B leaves"));
    Probe.Close(1000, false, TEXT("Challenger gone"));
    Probe.ExpectGuard(A, ELDCircleState::Guarded, 2900, 0, TEXT("The guard continues from its frozen value once the challenger leaves"));

    FCaptureReducerProbe Reset = Probe;
    Reset.Add(CapturePresence(BodyB, CaptureMs(1100), true), TEXT("B enters again"));
    Reset.Close(1100, false, TEXT("Frozen again"));
    Reset.ExpectGuard(A, ELDCircleState::Contested, 0, 200, TEXT("The guard freezes again"));
    Reset.Add(CapturePresence(BodyA, CaptureMs(1200), false), TEXT("The owner leaves while B is inside"));
    Reset.Close(1200, false, TEXT("Owner leaves under a challenger"));
    Reset.ExpectGuard(A, ELDCircleState::Solo, 0, 0, TEXT("The owner leaving under a challenger resets the guard and keeps his graffiti"));
    Reset.ExpectSolo(B, 1200, 1700, TEXT("The challenger left alone sprays from zero"));
    TestEqual(TEXT("The tally counts the guard reset"), Reset.Reducer.DevTally().GuardResets, uint32(1));
    Reset.Add(CapturePresence(BodyB, CaptureMs(1300), false), TEXT("B gives up"));
    Reset.Close(1300, false, TEXT("Zone empty"));
    Reset.ExpectGuard(A, ELDCircleState::Empty, 0, 0, TEXT("The reset guard stays at zero while the empty zone pauses it"));

    FCaptureReducerProbe Away = Probe;
    Away.Add(CapturePresence(BodyA, CaptureMs(1100), false), TEXT("The owner walks away"));
    Away.Close(1100, false, TEXT("Owner away"));
    Away.ExpectGuard(A, ELDCircleState::Empty, 0, 200, TEXT("Without a challenger the owner walking away pauses his guard"));
    Away.Add(CapturePresence(BodyB, CaptureMs(1200), true), TEXT("B enters with the owner away"));
    Away.Close(1200, false, TEXT("Sole sprayer"));
    Away.ExpectGuard(A, ELDCircleState::Solo, 0, 200, TEXT("An owner already outside only has his guard frozen"));
    Away.Add(CapturePresence(BodyC, CaptureMs(1300), true), TEXT("C joins B"));
    Away.Close(1300, false, TEXT("Two challengers"));
    Away.ExpectGuard(A, ELDCircleState::Contested, 0, 200, TEXT("Two challengers keep the guard frozen"));
    Away.Add(CapturePresence(BodyB, CaptureMs(1400), false), TEXT("B leaves"));
    Away.Add(CapturePresence(BodyC, CaptureMs(1500), false), TEXT("C leaves"));
    Away.Close(1500, false, TEXT("Challengers gone"));
    Away.ExpectGuard(A, ELDCircleState::Empty, 0, 200, TEXT("The guard stays paused once the last challenger leaves with the owner away"));
    TestEqual(TEXT("No reset while the owner stayed away"), Away.Reducer.DevTally().GuardResets, uint32(0));
    Away.Add(CapturePresence(BodyC, CaptureMs(1600), true), TEXT("C enters alone"));
    Away.Add(CapturePresence(BodyA, CaptureMs(1700), true), TEXT("The owner comes back under the challenger"));
    Away.Close(1700, false, TEXT("Owner returns"));
    Away.ExpectGuard(A, ELDCircleState::Contested, 0, 200, TEXT("The returning owner leaves the guard frozen"));
    Away.Add(CapturePresence(BodyA, CaptureMs(1800), false), TEXT("The owner leaves again"));
    Away.Close(1800, false, TEXT("Owner leaves under a challenger"));
    Away.ExpectGuard(A, ELDCircleState::Solo, 0, 0, TEXT("An owner who came back and left again resets the frozen guard"));
    Away.ExpectSolo(C, 1700, 2200, TEXT("The challenger resumes his paused spray"));

    FCaptureReducerProbe Together = Probe;
    Together.Add(CapturePresence(BodyB, CaptureMs(1100), true), TEXT("B enters"));
    Together.Close(1100, false, TEXT("Frozen"));
    Together.Add(CapturePresence(BodyA, CaptureMs(1200), false), TEXT("The owner leaves"));
    Together.Add(CapturePresence(BodyB, CaptureMs(1200), false), TEXT("The challenger leaves at the same instant"));
    Together.Close(1200, false, TEXT("Simultaneous exits"));
    Together.ExpectGuard(A, ELDCircleState::Empty, 0, 200,
        TEXT("An owner and the last challenger leaving at one instant reset nothing; the empty zone pauses the guard"));
    TestEqual(TEXT("No reset is counted"), Together.Reducer.DevTally().GuardResets, uint32(0));

    FCaptureReducerProbe Swap = Probe;
    Swap.Add(CapturePresence(BodyB, CaptureMs(1100), true), TEXT("B enters"));
    Swap.Add(CapturePresence(BodyA, CaptureMs(1100), false), TEXT("The owner leaves at the same instant"));
    Swap.Close(1100, false, TEXT("Simultaneous entry and exit"));
    Swap.ExpectGuard(A, ELDCircleState::Solo, 0, 200, TEXT("An owner leaving as a challenger enters only freezes the guard"));
    Swap.ExpectSolo(B, 1100, 1600, TEXT("The challenger sprays alone"));

    FCaptureReducerProbe Shielded = Probe;
    const FLDCombatBodyRef SecondC = CaptureTestBody(C, 2);
    Shielded.Add(CaptureFact(ELDCaptureEventKind::Died, BodyC, CaptureMs(1100), ELDCommitPhase::DeathsAndRewards), TEXT("C dies"));
    Shielded.Add(CaptureFact(ELDCaptureEventKind::Respawned, SecondC, CaptureMs(1200), ELDCommitPhase::RespawnsAndWorld, true, 1500),
        TEXT("C respawns protected inside the zone"));
    Shielded.Close(1200, false, TEXT("Protected challenger"));
    Shielded.ExpectGuard(A, ELDCircleState::Guarded, 2900, 0, TEXT("A protected body in the zone freezes nothing"));
    Shielded.Add(CaptureFact(ELDCaptureEventKind::ProtectionEnded, SecondC, CaptureMs(1500), ELDCommitPhase::Session), TEXT("Protection ends"));
    Shielded.Close(1500, false, TEXT("Protection ends inside"));
    Shielded.ExpectGuard(A, ELDCircleState::Contested, 0, 600, TEXT("The challenger counts, and freezes the guard, once protection ends"));

    FCaptureReducerProbe Step = Probe;
    Step.Close(2890, false, TEXT("Quiet boundary before the win"));
    Step.Add(CapturePresence(BodyB, CaptureMs(2895), true), TEXT("B enters just before the win"));
    Step.Close(2895, false, TEXT("Frozen near the end"));
    Step.ExpectGuard(A, ELDCircleState::Contested, 0, 1995, TEXT("The guard freezes five milliseconds short"));
    Step.Add(CapturePresence(BodyB, CaptureSeconds(2.8965), false), TEXT("B leaves inside the step"));
    TestEqual(TEXT("The unfrozen guard completes inside the step"), Step.Reducer.CompletionInsideMs(2950), int64(2902));
    TOptional<FLDCaptureProposal> Proposal;
    TestTrue(FString::Printf(TEXT("The step closes through the completion: %s"), *Step.Error),
        Step.Reducer.CloseThroughCompletion(2950, Proposal, Step.Error));
    TestTrue(TEXT("The owner wins at his exact unfrozen guard instant"),
        Proposal.IsSet() && Proposal->Winner == A && Proposal->DueAtMs == 2902 && Proposal->GuardSinceMs == 902);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCaptureGuardNeedsOwnerTest, "Lockdown.Rules.DemoCapture.GuardNeedsOwner",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCaptureGuardNeedsOwnerTest::RunTest(const FString& Parameters)
{
    // 2026-11-07 user decision: the guard counts only while its owner stands alone in the zone; with the flagpole owned
    // and nobody in the zone the time stops. An empty zone pauses it at its value and the owner back alone resumes it.
    const FGuid A = FGuid::NewGuid(), B = FGuid::NewGuid();
    const FLDCombatBodyRef BodyA = CaptureTestBody(A, 1), BodyB = CaptureTestBody(B, 1);
    FCaptureReducerProbe Probe{*this};
    TestTrue(TEXT("Reducer begins"), Probe.Reducer.Begin(0, 500, 2000, Probe.Error));
    Probe.Add(CaptureJoined(BodyA, true), TEXT("A joins inside"));
    Probe.Add(CaptureJoined(BodyB, false), TEXT("B joins outside"));
    Probe.Close(0, false, TEXT("Open"));
    Probe.Close(500, false, TEXT("A sprays"));
    Probe.ExpectGuard(A, ELDCircleState::Guarded, 2500, 0, TEXT("The painter alone in the zone guards from zero"));

    Probe.Add(CapturePresence(BodyA, CaptureMs(800), false), TEXT("The owner leaves the zone alone"));
    Probe.Close(800, false, TEXT("Owner away"));
    Probe.ExpectGuard(A, ELDCircleState::Empty, 0, 300, TEXT("The empty zone pauses the guard at its value"));
    TestEqual(TEXT("A paused guard adds no driver stop"), Probe.Reducer.NextDueMs(), MAX_int64);
    Probe.Close(2600, false, TEXT("The old win instant passes with the owner away"));
    Probe.ExpectGuard(A, ELDCircleState::Empty, 0, 300, TEXT("Nothing accrues while the zone is empty"));

    Probe.Add(CapturePresence(BodyB, CaptureMs(2700), true), TEXT("A challenger enters with the owner away"));
    Probe.Close(2700, false, TEXT("Sole sprayer"));
    Probe.ExpectGuard(A, ELDCircleState::Solo, 0, 300, TEXT("A challenger entering while the owner is away freezes the kept value"));
    Probe.ExpectSolo(B, 2700, 3200, TEXT("The challenger sprays from zero"));
    Probe.Add(CapturePresence(BodyB, CaptureMs(2800), false), TEXT("The challenger leaves"));
    Probe.Close(2800, false, TEXT("Zone empty again"));
    Probe.ExpectGuard(A, ELDCircleState::Empty, 0, 300, TEXT("The guard stays paused at the same value"));
    TestEqual(TEXT("An owner already outside resets nothing"), Probe.Reducer.DevTally().GuardResets, uint32(0));

    Probe.Add(CapturePresence(BodyA, CaptureMs(3000), true), TEXT("The owner returns alone"));
    Probe.Close(3000, false, TEXT("Owner back"));
    Probe.ExpectGuard(A, ELDCircleState::Guarded, 4700, 0, TEXT("The owner back alone resumes from the kept value"));
    TestEqual(TEXT("The driver stops at the resumed win"), Probe.Reducer.NextDueMs(), int64(4700));
    Probe.Add(CapturePresence(BodyA, CaptureMs(4600), false), TEXT("The owner leaves just before the win"));
    Probe.Close(4600, false, TEXT("Owner away near the end"));
    Probe.ExpectGuard(A, ELDCircleState::Empty, 0, 1900, TEXT("Leaving near the end pauses short of the win"));
    Probe.Close(4800, false, TEXT("No win while the owner is away"));
    Probe.Add(CapturePresence(BodyA, CaptureMs(5000), true), TEXT("The owner returns again"));
    Probe.Close(5000, false, TEXT("Owner back again"));
    Probe.ExpectGuard(A, ELDCircleState::Guarded, 5100, 0, TEXT("Only the remaining guard is left to stand"));
    Probe.Add(CapturePresence(BodyA, CaptureMs(5050), false), TEXT("The owner steps out once more"));
    Probe.Close(5050, false, TEXT("Paused again"));
    Probe.ExpectGuard(A, ELDCircleState::Empty, 0, 1950, TEXT("The guard pauses with fifty milliseconds left"));

    Probe.Add(CapturePresence(BodyA, CaptureSeconds(5.0605), true), TEXT("The owner steps back in inside a step"));
    TestEqual(TEXT("The resumed guard completes inside the step after the full remaining time"),
        Probe.Reducer.CompletionInsideMs(5150), int64(5111));
    TOptional<FLDCaptureProposal> Proposal;
    TestTrue(FString::Printf(TEXT("The step closes through the completion: %s"), *Probe.Error),
        Probe.Reducer.CloseThroughCompletion(5150, Proposal, Probe.Error));
    TestTrue(TEXT("The owner wins after standing alone in the zone for the whole remaining guard"),
        Proposal.IsSet() && Proposal->Winner == A && Proposal->DueAtMs == 5111 && Proposal->GuardSinceMs == 3111);
    TestEqual(TEXT("Pauses never count as resets"), Probe.Reducer.DevTally().GuardResets, uint32(0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCaptureContestPauseTest, "Lockdown.Rules.DemoCapture.ContestPause",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCaptureContestPauseTest::RunTest(const FString& Parameters)
{
    const FGuid A = FGuid::NewGuid(), B = FGuid::NewGuid(), C = FGuid::NewGuid();
    const FLDCombatBodyRef BodyA = CaptureTestBody(A, 1), BodyB = CaptureTestBody(B, 1), BodyC = CaptureTestBody(C, 1);
    auto ExpectPaused = [this](FCaptureReducerProbe& Probe, const FGuid& Holder, int64 PausedMs, const TCHAR* What)
    {
        TestTrue(What, Probe.Reducer.GetState() == ELDCircleState::Contested && Probe.Reducer.GetOccupier() == Holder
            && Probe.Reducer.GetPausedProgressMs() == PausedMs && Probe.Reducer.GetSoloSinceMs() == 0
            && Probe.Reducer.GetDueAtMs() == 0 && Probe.Reducer.NextDueMs() == MAX_int64);
    };

    FCaptureReducerProbe Probe{*this};
    TestTrue(TEXT("Reducer begins"), Probe.Reducer.Begin(0, 500, 5000, Probe.Error));
    Probe.Add(CaptureJoined(BodyA, true), TEXT("A joins inside"));
    Probe.Add(CaptureJoined(BodyB, false), TEXT("B joins outside"));
    Probe.Add(CaptureJoined(BodyC, false), TEXT("C joins outside"));
    Probe.Close(0, false, TEXT("Open"));
    Probe.Add(CapturePresence(BodyB, CaptureMs(100), true), TEXT("B enters"));
    Probe.Close(100, false, TEXT("Contest"));
    ExpectPaused(Probe, A, 100, TEXT("Two effective contenders pause the sprayer's progress"));
    Probe.Close(300, false, TEXT("A long contest"));
    ExpectPaused(Probe, A, 100, TEXT("Nobody accumulates while contested"));
    Probe.Add(CapturePresence(BodyA, CaptureMs(350), false), TEXT("The paused holder leaves the zone"));
    Probe.Close(350, false, TEXT("Holder exit"));
    Probe.ExpectSolo(B, 350, 850, TEXT("The holder who leaves loses his progress and nobody inherits it"));

    Probe.Add(CapturePresence(BodyA, CaptureMs(400), true), TEXT("A returns"));
    Probe.Close(400, false, TEXT("Second contest"));
    ExpectPaused(Probe, B, 50, TEXT("The new holder's own progress is paused"));
    const uint64 PausedEpoch = Probe.Reducer.GetEpoch();
    Probe.Add(CapturePresence(BodyC, CaptureMs(410), true), TEXT("C joins the contest"));
    Probe.Close(410, false, TEXT("Contest grows"));
    ExpectPaused(Probe, B, 50, TEXT("A larger contest keeps the paused holder"));
    TestEqual(TEXT("An unchanged contest publishes nothing new"), Probe.Reducer.GetEpoch(), PausedEpoch);
    Probe.Add(CaptureFact(ELDCaptureEventKind::Died, BodyB, CaptureMs(450), ELDCommitPhase::DeathsAndRewards),
        TEXT("The paused holder dies"));
    Probe.Close(450, false, TEXT("Holder death"));
    ExpectPaused(Probe, FGuid(), 0, TEXT("A dead holder's progress is cleared even while the contest continues"));
    TestEqual(TEXT("Clearing the holder is a public change"), Probe.Reducer.GetEpoch(), PausedEpoch + 1);
    Probe.Add(CapturePresence(BodyC, CaptureMs(500), false), TEXT("C leaves"));
    Probe.Close(500, false, TEXT("Contest ends without a holder"));
    Probe.ExpectSolo(A, 500, 1000, TEXT("The next sole sprayer starts from zero"));

    Probe.Add(CapturePresence(BodyC, CaptureMs(600), true), TEXT("C enters"));
    Probe.Add(CaptureFact(ELDCaptureEventKind::Left, BodyA, CaptureMs(650), ELDCommitPhase::Session),
        TEXT("The holder leaves the match during the contest"));
    Probe.Close(650, false, TEXT("Departure"));
    Probe.ExpectSolo(C, 650, 1150, TEXT("A departed holder forfeits his paused progress"));
    Probe.Add(CapturePresence(BodyC, CaptureMs(700), false), TEXT("C exits while sole"));
    Probe.Add(CapturePresence(BodyC, CaptureMs(800), true), TEXT("C returns"));
    Probe.Close(800, false, TEXT("Empty then sole"));
    Probe.ExpectSolo(C, 800, 1300, TEXT("An empty zone clears progress, even for the same contender"));

    FCaptureReducerProbe Same{*this};
    TestTrue(TEXT("Reducer begins"), Same.Reducer.Begin(0, 500, 5000, Same.Error));
    Same.Add(CaptureJoined(BodyA, true), TEXT("A joins inside"));
    Same.Add(CaptureJoined(BodyB, false), TEXT("B joins outside"));
    Same.Close(0, false, TEXT("Open"));
    Same.Add(CapturePresence(BodyB, CaptureSeconds(.2002), true), TEXT("B enters"));
    Same.Add(CapturePresence(BodyA, CaptureSeconds(.2004), false), TEXT("A leaves at a later instant of the same boundary"));
    Same.Close(201, false, TEXT("Same-boundary handoff"));
    Same.ExpectSolo(B, 201, 701, TEXT("Facts of one boundary are reduced in order: the holder's exit hands nothing over"));

    FCaptureReducerProbe Cap{*this};
    TestTrue(TEXT("Reducer begins"), Cap.Reducer.Begin(0, 500, 5000, Cap.Error));
    Cap.Add(CaptureJoined(BodyA, true), TEXT("A joins inside"));
    Cap.Add(CaptureJoined(BodyB, false), TEXT("B joins outside"));
    Cap.Close(0, false, TEXT("Open"));
    Cap.Add(CapturePresence(BodyB, CaptureMs(450), true), TEXT("B enters late in the spray"));
    Cap.Close(450, false, TEXT("Late contest"));
    ExpectPaused(Cap, A, 450, TEXT("A late contest adds no driver stop of its own"));
    FCaptureReducerProbe Inside = Cap;
    Inside.Add(CapturePresence(BodyB, CaptureMs(460), false), TEXT("B leaves"));
    Inside.Add(CapturePresence(BodyB, CaptureMs(515), true), TEXT("B re-enters after the resumed spray"));
    TestEqual(TEXT("A spray inside a step never ends the match"), Inside.Reducer.CompletionInsideMs(520), MAX_int64);
    Inside.Close(520, false, TEXT("The step closes through the resumed spray"));
    Inside.ExpectGuard(A, ELDCircleState::Contested, 0, 5,
        TEXT("The resumed spray lands at its real instant inside the step; the later entry meets the owner and freezes his guard"));
    TestEqual(TEXT("The graffiti dates from the resumed spray instant"), Inside.Reducer.DevTally().GraffitiSinceMs, int64(510));
    FCaptureReducerProbe Interrupted = Cap;
    Interrupted.Add(CapturePresence(BodyB, CaptureMs(460), false), TEXT("B leaves"));
    Interrupted.Add(CapturePresence(BodyB, CaptureSeconds(.5095), true), TEXT("B re-enters in the due bucket"));
    Interrupted.Close(520, false, TEXT("Contest in the due bucket"));
    ExpectPaused(Interrupted, A, 500, TEXT("A contest in the due bucket still prevents the resumed spray"));
    TestFalse(TEXT("No graffiti"), Interrupted.Reducer.GetGraffitiOwner().IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCapturePausedContestStepsTest, "Lockdown.Rules.DemoCapture.PausedContestSteps",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCapturePausedContestStepsTest::RunTest(const FString& Parameters)
{
    // The native driver's longest step (CMC MaxSimulationTimeStep) and the approved spray and guard.
    constexpr int64 MaxStepMs = 50, SprayMs = 3500, GuardMs = 20000, ContestMs = 2000;
    const FGuid A = FGuid::NewGuid(), B = FGuid::NewGuid();
    const FLDCombatBodyRef BodyA = CaptureTestBody(A, 1), BodyB = CaptureTestBody(B, 1);
    FCaptureReducerProbe Probe{*this};
    TestTrue(TEXT("Reducer begins"), Probe.Reducer.Begin(0, SprayMs, GuardMs, Probe.Error));
    Probe.Add(CaptureJoined(BodyA, true), TEXT("A joins inside"));
    Probe.Add(CaptureJoined(BodyB, false), TEXT("B joins outside"));
    Probe.Close(0, false, TEXT("Open"));
    Probe.Add(CapturePresence(BodyB, CaptureMs(SprayMs - 1), true), TEXT("B enters one millisecond before the spray"));
    Probe.Close(SprayMs - 1, false, TEXT("Contest freezes nearly the full spray"));
    TestTrue(TEXT("The sprayer is paused one millisecond short"), Probe.Reducer.GetState() == ELDCircleState::Contested
        && Probe.Reducer.GetOccupier() == A && Probe.Reducer.GetPausedProgressMs() == SprayMs - 1);
    // Step the contest exactly as the authority driver does: the native step cap or the reducer's next stop.
    int64 Now = SprayMs - 1;
    int32 Steps = 0;
    while (Now < SprayMs - 1 + ContestMs && Steps <= ContestMs)
    {
        const int64 End = FMath::Min(Now + MaxStepMs, Probe.Reducer.NextDueMs());
        if (!Probe.Close(End, false, TEXT("A quiet contest boundary"))) { return false; }
        Now = End;
        ++Steps;
    }
    TestTrue(FString::Printf(TEXT("A quiet paused contest runs native-length steps (%d steps for %lld ms)"), Steps, ContestMs),
        Steps <= ContestMs / MaxStepMs);
    // The step in which the holder becomes sole again sprays at his exact resumed instant and goes on.
    const int64 End = Now + MaxStepMs;
    Probe.Add(CapturePresence(BodyB, CaptureSeconds((double(Now) + 6.5) / 1000.), false), TEXT("B leaves inside the step"));
    TestEqual(TEXT("A resumed spray inside the step is no completion of the match"), Probe.Reducer.CompletionInsideMs(End), MAX_int64);
    Probe.Close(End, false, TEXT("The resumed step closes as an ordinary boundary"));
    Probe.ExpectGuard(A, ELDCircleState::Guarded, Now + 8 + GuardMs, 0,
        TEXT("The resumed spray lands one millisecond after the resume and the guard starts there"));
    TestEqual(TEXT("The driver next stops at the guard win"), Probe.Reducer.NextDueMs(), Now + 8 + GuardMs);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCaptureSprayInterruptTest, "Lockdown.Rules.DemoCapture.SprayInterrupt",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCaptureSprayInterruptTest::RunTest(const FString& Parameters)
{
    // Root, stun, taunt and knockback reach the reducer as one Interrupted fact from Combat's control path; damage,
    // slow and disarm publish none, so they never touch the spray.
    const FGuid A = FGuid::NewGuid(), B = FGuid::NewGuid();
    const FLDCombatBodyRef BodyA = CaptureTestBody(A, 1), BodyB = CaptureTestBody(B, 1);
    FCaptureReducerProbe Probe{*this};
    TestTrue(TEXT("Reducer begins"), Probe.Reducer.Begin(0, 500, 5000, Probe.Error));
    Probe.Add(CaptureJoined(BodyA, true), TEXT("A joins inside"));
    Probe.Add(CaptureJoined(BodyB, false), TEXT("B joins outside"));
    Probe.Close(0, false, TEXT("Open"));
    Probe.ExpectSolo(A, 0, 500, TEXT("A sprays"));
    Probe.Add(CaptureInterrupt(BodyA, 200), TEXT("A hard control lands on the sole sprayer"));
    Probe.Close(200, false, TEXT("Interrupted sprayer"));
    Probe.ExpectSolo(A, 200, 700, TEXT("The control clears his progress at its instant; still alone, he starts again from zero"));
    Probe.Add(CapturePresence(BodyB, CaptureMs(300), true), TEXT("B enters"));
    Probe.Close(300, false, TEXT("Contest"));
    TestTrue(TEXT("The contest pauses the restarted progress"), Probe.Reducer.GetState() == ELDCircleState::Contested
        && Probe.Reducer.GetOccupier() == A && Probe.Reducer.GetPausedProgressMs() == 100);
    Probe.Add(CaptureInterrupt(BodyA, 350), TEXT("A hard control lands on the paused holder"));
    Probe.Close(350, false, TEXT("Interrupted paused holder"));
    TestTrue(TEXT("A control clears paused progress too"), Probe.Reducer.GetState() == ELDCircleState::Contested
        && !Probe.Reducer.GetOccupier().IsValid() && Probe.Reducer.GetPausedProgressMs() == 0);
    Probe.Add(CapturePresence(BodyB, CaptureMs(400), false), TEXT("B leaves"));
    Probe.Close(400, false, TEXT("A alone again"));
    Probe.ExpectSolo(A, 400, 900, TEXT("Nothing of the cleared progress resumes"));
    Probe.Add(CapturePresence(BodyB, CaptureMs(450), true), TEXT("B enters"));
    Probe.Close(450, false, TEXT("Contest"));
    const uint64 Epoch = Probe.Reducer.GetEpoch();
    Probe.Add(CaptureInterrupt(BodyB, 460), TEXT("A hard control lands on the challenger without progress"));
    Probe.Close(460, false, TEXT("Interrupted bystander"));
    TestTrue(TEXT("Controlling someone without progress changes nothing"), Probe.Reducer.GetEpoch() == Epoch
        && Probe.Reducer.GetOccupier() == A && Probe.Reducer.GetPausedProgressMs() == 50);
    Probe.Add(CapturePresence(BodyB, CaptureMs(470), false), TEXT("B leaves"));
    Probe.Close(470, false, TEXT("Resume"));
    Probe.ExpectSolo(A, 420, 920, TEXT("The holder resumes his paused progress"));
    Probe.Close(919, false, TEXT("Quiet boundary"));
    Probe.Add(CaptureInterrupt(BodyA, 920), TEXT("A hard control at the spray instant"));
    Probe.Close(920, false, TEXT("Control and spray at one instant"));
    Probe.ExpectSolo(A, 920, 1420, TEXT("A control at the spray instant is settled first and the spray never lands"));
    TestFalse(TEXT("No graffiti"), Probe.Reducer.GetGraffitiOwner().IsValid());
    {
        const FLDCaptureDevTally::FHolder* Holder = Probe.Reducer.DevTally().Holders.Find(A);
        TestTrue(TEXT("The telemetry tally counts every interruption as a loss"), Holder && Holder->Attempts == 4
            && Holder->Resumes == 1 && Holder->Paused == 2 && Holder->Lost == 3 && Holder->Interrupted == 3);
    }
    TestFalse(TEXT("An interruption must name the current living body"),
        Probe.Reducer.Enqueue(CaptureInterrupt(CaptureTestBody(A, 2), 1000), Probe.Error));
    Probe.Close(1420, false, TEXT("The spray lands"));
    Probe.ExpectGuard(A, ELDCircleState::Guarded, 6420, 0, TEXT("A graffiti"));
    const uint64 Owned = Probe.Reducer.GetEpoch();
    Probe.Add(CaptureInterrupt(BodyA, 1500), TEXT("A hard control lands on the owner"));
    Probe.Close(1500, false, TEXT("Controlled owner"));
    Probe.ExpectGuard(A, ELDCircleState::Guarded, 6420, 0, TEXT("Controls on the owner never touch the guard"));
    TestEqual(TEXT("Nothing public changed"), Probe.Reducer.GetEpoch(), Owned);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCaptureGraffitiClearedTest, "Lockdown.Rules.DemoCapture.GraffitiCleared",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCaptureGraffitiClearedTest::RunTest(const FString& Parameters)
{
    const FGuid A = FGuid::NewGuid(), B = FGuid::NewGuid();
    const FLDCombatBodyRef BodyA = CaptureTestBody(A, 1), BodyB = CaptureTestBody(B, 1);
    FCaptureReducerProbe Probe{*this};
    TestTrue(TEXT("Reducer begins"), Probe.Reducer.Begin(0, 100, 5000, Probe.Error));
    Probe.Add(CaptureJoined(BodyA, true), TEXT("A joins inside"));
    Probe.Add(CaptureJoined(BodyB, false), TEXT("B joins outside"));
    Probe.Close(0, false, TEXT("Open"));
    Probe.Close(100, false, TEXT("A sprays"));
    Probe.ExpectGuard(A, ELDCircleState::Guarded, 5100, 0, TEXT("A owns the flagpole"));

    FCaptureReducerProbe Death = Probe;
    Death.Add(CaptureFact(ELDCaptureEventKind::Died, BodyA, CaptureMs(200), ELDCommitPhase::DeathsAndRewards), TEXT("The owner dies"));
    Death.Close(200, false, TEXT("Owner death"));
    Death.ExpectGuard(FGuid(), ELDCircleState::Empty, 0, 0, TEXT("The owner's death clears the graffiti at once"));
    TestTrue(TEXT("The bare flagpole adds no stop and the tally names the death"), Death.Reducer.NextDueMs() == MAX_int64
        && Death.Reducer.DevTally().ClearedByDeath == 1 && Death.Reducer.DevTally().GraffitiSinceMs == -1);
    FCaptureReducerProbe Leave = Probe;
    Leave.Add(CaptureFact(ELDCaptureEventKind::Left, BodyA, CaptureMs(200), ELDCommitPhase::Session), TEXT("The owner leaves the match"));
    Leave.Close(200, false, TEXT("Owner departure"));
    Leave.ExpectGuard(FGuid(), ELDCircleState::Empty, 0, 0, TEXT("The owner's departure clears the graffiti at once"));
    TestEqual(TEXT("The tally names the departure"), Leave.Reducer.DevTally().ClearedByLeave, uint32(1));

    Probe.Add(CapturePresence(BodyA, CaptureMs(150), false), TEXT("The owner walks away"));
    Probe.Add(CapturePresence(BodyB, CaptureMs(160), true), TEXT("B sprays alone"));
    Probe.Close(160, false, TEXT("Repaint under way"));
    Probe.ExpectGuard(A, ELDCircleState::Solo, 0, 50, TEXT("The owner walking away pauses the guard and the repaint keeps it frozen"));
    Probe.Add(CaptureFact(ELDCaptureEventKind::Died, BodyA, CaptureMs(170), ELDCommitPhase::DeathsAndRewards), TEXT("The owner dies"));
    Probe.Close(170, false, TEXT("Owner death during a repaint"));
    Probe.ExpectSolo(B, 160, 260, TEXT("The sprayer keeps his progress when the graffiti is cleared"));
    TestFalse(TEXT("No graffiti"), Probe.Reducer.GetGraffitiOwner().IsValid());
    Probe.Close(260, false, TEXT("B sprays"));
    Probe.ExpectGuard(B, ELDCircleState::Guarded, 5260, 0, TEXT("A spray on the bare flagpole"));
    TestTrue(TEXT("It is no repaint"), Probe.Reducer.DevTally().Sprays == 2 && Probe.Reducer.DevTally().Repaints == 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCaptureProtectionTest, "Lockdown.Rules.DemoCapture.ProtectionAndContracts",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCaptureProtectionTest::RunTest(const FString& Parameters)
{
    const FGuid A = FGuid::NewGuid(), B = FGuid::NewGuid();
    const FLDCombatBodyRef BodyA = CaptureTestBody(A, 1), FirstB = CaptureTestBody(B, 1), SecondB = CaptureTestBody(B, 2);
    auto Prepare = [&](FCaptureReducerProbe& Probe, int64 ProtectedUntilMs)
    {
        TestTrue(TEXT("Reducer begins"), Probe.Reducer.Begin(0, 500, 5000, Probe.Error));
        Probe.Add(CaptureJoined(BodyA, true), TEXT("A joins inside"));
        Probe.Add(CaptureJoined(FirstB, false), TEXT("B joins outside"));
        Probe.Close(0, false, TEXT("Open"));
        Probe.Add(CaptureFact(ELDCaptureEventKind::Died, FirstB, CaptureMs(100), ELDCommitPhase::DeathsAndRewards), TEXT("B dies"));
        Probe.Add(CaptureFact(ELDCaptureEventKind::Respawned, SecondB, CaptureMs(200), ELDCommitPhase::RespawnsAndWorld,
            true, ProtectedUntilMs), TEXT("B respawns protected inside the zone"));
        Probe.Close(200, false, TEXT("Protected respawn"));
        Probe.ExpectSolo(A, 0, 500, TEXT("A protected body neither sprays nor interrupts"));
    };

    FCaptureReducerProbe AtDue{*this};
    Prepare(AtDue, 500);
    AtDue.Add(CaptureFact(ELDCaptureEventKind::ProtectionEnded, SecondB, CaptureMs(500), ELDCommitPhase::Session),
        TEXT("Protection expires at the spray instant"));
    AtDue.Close(500, false, TEXT("Half-open protection end contests at the same instant"));
    TestTrue(TEXT("Expiry at the spray instant pauses the full progress"),
        AtDue.Reducer.GetState() == ELDCircleState::Contested && AtDue.Reducer.GetOccupier() == A
        && AtDue.Reducer.GetPausedProgressMs() == 500 && AtDue.Reducer.NextDueMs() == MAX_int64);
    AtDue.Add(CapturePresence(SecondB, CaptureMs(501), false), TEXT("B leaves"));
    AtDue.Close(501, false, TEXT("Sole again with the full paused progress"));
    AtDue.ExpectGuard(A, ELDCircleState::Guarded, 5501, 0, TEXT("The full paused progress sprays the instant he is sole again"));

    FCaptureReducerProbe Later{*this};
    Prepare(Later, 600);
    Later.Close(500, false, TEXT("Protection ending after the spray cannot rewrite it"));
    Later.ExpectGuard(A, ELDCircleState::Guarded, 5500, 0, TEXT("The protected body never counted"));

    FCaptureReducerProbe Exit{*this};
    Prepare(Exit, 300);
    Exit.Add(CapturePresence(SecondB, CaptureSeconds(.2998), false), TEXT("Protected B leaves just before expiry"));
    Exit.Add(CaptureFact(ELDCaptureEventKind::ProtectionEnded, SecondB, CaptureMs(300), ELDCommitPhase::Session), TEXT("Expiry"));
    Exit.Close(300, false, TEXT("Same bucket ordered by real instant, not by phase"));
    Exit.ExpectSolo(A, 0, 500, TEXT("Leaving before the expiry instant creates no phantom contest"));

    FCaptureReducerProbe Missing{*this};
    Prepare(Missing, 300);
    TOptional<FLDCaptureProposal> Proposal;
    TestFalse(TEXT("A protection deadline without its life fact is an explicit contract failure"),
        Missing.Reducer.CloseBoundary(300, Proposal, Missing.Error));
    TestFalse(TEXT("Contract failure is explained"), Missing.Error.IsEmpty());

    FCaptureReducerProbe Skip{*this};
    TestTrue(TEXT("Reducer begins"), Skip.Reducer.Begin(0, 100, 400, Skip.Error));
    Skip.Add(CaptureJoined(BodyA, true), TEXT("A joins inside"));
    Skip.Close(0, false, TEXT("Open"));
    Skip.Close(100, false, TEXT("Spray"));
    TestFalse(TEXT("A driver that skips the guard win fails loudly"),
        Skip.Reducer.CloseBoundary(501, Proposal, Skip.Error) || Proposal.IsSet());

    FCaptureReducerProbe Refuse{*this};
    TestFalse(TEXT("A spray or guard without a positive duration is refused"),
        Refuse.Reducer.Begin(1000, 0, 500, Refuse.Error) || Refuse.Reducer.Begin(1000, 500, 0, Refuse.Error));
    TestTrue(TEXT("Reducer begins"), Refuse.Reducer.Begin(1000, 500, 5000, Refuse.Error));
    Refuse.Add(CaptureJoined(BodyA, false), TEXT("A joins"));
    TestFalse(TEXT("A contender cannot join twice"), Refuse.Reducer.Enqueue(CaptureJoined(BodyA, false), Refuse.Error));
    TestFalse(TEXT("Presence must name the current body"),
        Refuse.Reducer.Enqueue(CapturePresence(CaptureTestBody(A, 1), CaptureMs(10), true), Refuse.Error));
    TestFalse(TEXT("Unknown contenders cannot publish presence"),
        Refuse.Reducer.Enqueue(CapturePresence(FirstB, CaptureMs(10), true), Refuse.Error));
    TestFalse(TEXT("Unknown contenders cannot be interrupted"), Refuse.Reducer.Enqueue(CaptureInterrupt(FirstB, 10), Refuse.Error));
    TestFalse(TEXT("Respawn requires a committed death"), Refuse.Reducer.Enqueue(CaptureFact(ELDCaptureEventKind::Respawned,
        CaptureTestBody(A, 2), CaptureMs(10), ELDCommitPhase::RespawnsAndWorld, false, 100), Refuse.Error));
    TestFalse(TEXT("An unprotected life cannot end protection"), Refuse.Reducer.Enqueue(CaptureFact(
        ELDCaptureEventKind::ProtectionEnded, BodyA, CaptureMs(10), ELDCommitPhase::Session), Refuse.Error));
    Refuse.Add(CaptureFact(ELDCaptureEventKind::Died, BodyA, CaptureMs(10), ELDCommitPhase::DeathsAndRewards), TEXT("A dies"));
    TestFalse(TEXT("One life dies once"), Refuse.Reducer.Enqueue(
        CaptureFact(ELDCaptureEventKind::Died, BodyA, CaptureMs(11), ELDCommitPhase::DeathsAndRewards), Refuse.Error));
    TestFalse(TEXT("A dead body cannot be interrupted"), Refuse.Reducer.Enqueue(CaptureInterrupt(BodyA, 11), Refuse.Error));
    Refuse.Close(20, false, TEXT("Close"));
    TestFalse(TEXT("Closed boundaries cannot be backfilled"), Refuse.Reducer.Enqueue(CaptureFact(ELDCaptureEventKind::Respawned,
        CaptureTestBody(A, 2), CaptureMs(20), ELDCommitPhase::RespawnsAndWorld, false, 100), Refuse.Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCaptureGeometryTest, "Lockdown.Rules.DemoCapture.Geometry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCaptureGeometryTest::RunTest(const FString& Parameters)
{
    UBoxComponent* Ground = NewObject<UBoxComponent>(GetTransientPackage());
    UBoxComponent* Other = NewObject<UBoxComponent>(GetTransientPackage());
    FLDCaptureCircle Circle;
    Circle.CaptureId = TEXT("capture_fixture");
    Circle.RadiusCm = 600;
    Circle.Ground = Ground;
    const FLDNativeSupportSample OnGround{true, Ground};
    const FLDNativeSupportSample Airborne{false, nullptr};
    const FLDNativeSupportSample OnOther{true, Other};
    TestTrue(TEXT("The disk edge is inside"), Circle.Contains(FVector(600, 0, 0), OnGround));
    TestFalse(TEXT("Outside the closed disk"), Circle.Contains(FVector(600.0001, 0, 0), OnGround));
    TestFalse(TEXT("Airborne feet never occupy"), Circle.Contains(FVector::ZeroVector, Airborne));
    TestFalse(TEXT("Other support never occupies"), Circle.Contains(FVector::ZeroVector, OnOther));

    auto Batch = [&](const FVector& Start, const FLDNativeSupportSample& Support)
    {
        FLDNativeMotionBatch Result;
        Result.StartFootCm = Result.EndFootCm = Start;
        Result.InitialSupport = Result.FinalSupport = Support;
        return Result;
    };
    auto Append = [](FLDNativeMotionBatch& Into, ELDNativeMotionRecordKind Kind, const FVector& To, double Start,
        double End, const FLDNativeSupportSample& Support = {})
    {
        FLDNativeMotionRecord& Record = Into.Records.AddDefaulted_GetRef();
        Record.Sequence = uint64(Into.Records.Num());
        Record.Kind = Kind;
        Record.FromFootCm = Into.EndFootCm;
        Record.ToFootCm = To;
        Record.StartServerSeconds = Start;
        Record.EndServerSeconds = End;
        Record.Support = Support;
        Into.EndFootCm = To;
        if (Kind == ELDNativeMotionRecordKind::Support) { Into.FinalSupport = Support; }
    };
    FString Error;
    TArray<FLDCaptureCrossing> Crossings;

    FLDNativeMotionBatch Chord = Batch(FVector(-1000, 0, 0), OnGround);
    Append(Chord, ELDNativeMotionRecordKind::Movement, FVector(1000, 0, 0), 1., 2.);
    bool Inside = false;
    TestTrue(TEXT("A chord through the disk is extracted"), LDCaptureCircle::Extract(Circle, Chord, Inside, Crossings, Error));
    TestTrue(TEXT("Start and end outside still report the enter and the exit at their real times"),
        Crossings.Num() == 2 && Crossings[0].bInside && !Crossings[1].bInside
        && FMath::IsNearlyEqual(Crossings[0].Seconds, 1.2, 1e-9) && FMath::IsNearlyEqual(Crossings[1].Seconds, 1.8, 1e-9)
        && !Inside);

    FLDNativeMotionBatch Tangent = Batch(FVector(-100, 600, 0), OnGround);
    Append(Tangent, ELDNativeMotionRecordKind::Movement, FVector(100, 600, 0), 3., 4.);
    Inside = false;
    TestTrue(TEXT("A tangent touch is extracted"), LDCaptureCircle::Extract(Circle, Tangent, Inside, Crossings, Error));
    TestTrue(TEXT("A tangent keeps both simultaneous transitions"), Crossings.Num() == 2 && Crossings[0].bInside
        && !Crossings[1].bInside && Crossings[0].Seconds == Crossings[1].Seconds);

    FLDNativeMotionBatch Jump = Batch(FVector(-1000, 0, 0), OnGround);
    Append(Jump, ELDNativeMotionRecordKind::Adjustment, FVector(1000, 0, 0), 5., 5.);
    Inside = false;
    TestTrue(TEXT("Discontinuous placement is extracted"), LDCaptureCircle::Extract(Circle, Jump, Inside, Crossings, Error));
    TestEqual(TEXT("A discontinuous placement does not invent a path through the circle"), Crossings.Num(), 0);

    FLDNativeMotionBatch Flight = Batch(FVector(-1000, 0, 0), Airborne);
    Append(Flight, ELDNativeMotionRecordKind::Movement, FVector(0, 0, 0), 6., 7.);
    Append(Flight, ELDNativeMotionRecordKind::Support, FVector(0, 0, 0), 7., 7., OnGround);
    Inside = false;
    TestTrue(TEXT("Airborne travel then landing is extracted"), LDCaptureCircle::Extract(Circle, Flight, Inside, Crossings, Error));
    TestTrue(TEXT("Only the landing on registered ground occupies"),
        Crossings.Num() == 1 && Crossings[0].bInside && Crossings[0].Seconds == 7. && Inside);

    FLDNativeMotionBatch Lift = Batch(FVector(10, 10, 0), OnGround);
    Append(Lift, ELDNativeMotionRecordKind::Support, FVector(10, 10, 0), 8., 8., Airborne);
    Append(Lift, ELDNativeMotionRecordKind::Support, FVector(10, 10, 0), 8.5, 8.5, OnGround);
    Inside = true;
    TestTrue(TEXT("Support changes at a fixed XY are extracted"), LDCaptureCircle::Extract(Circle, Lift, Inside, Crossings, Error));
    TestTrue(TEXT("Losing and regaining support at the same XY are both reported"),
        Crossings.Num() == 2 && !Crossings[0].bInside && Crossings[1].bInside && Inside);

    FLDNativeMotionBatch Mismatch = Batch(FVector(0, 0, 0), OnGround);
    Inside = false;
    TestFalse(TEXT("Evidence that does not continue the published presence is refused"),
        LDCaptureCircle::Extract(Circle, Mismatch, Inside, Crossings, Error));
    FLDNativeMotionBatch Broken = Batch(FVector(-1000, 0, 0), OnGround);
    Append(Broken, ELDNativeMotionRecordKind::Movement, FVector(-900, 0, 0), 9., 9.5);
    Broken.Records[0].FromFootCm = FVector(-950, 0, 0);
    Inside = false;
    TestFalse(TEXT("Discontinuous records are refused"), LDCaptureCircle::Extract(Circle, Broken, Inside, Crossings, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCaptureViewsTest, "Lockdown.Rules.DemoCapture.PublicViews",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCaptureViewsTest::RunTest(const FString& Parameters)
{
    FString Error;
    FLDCaptureEventTime Time;
    TestTrue(TEXT("Integer instants stay exact"), FLDCaptureEventTime::FromMs(1001, Time, Error)
        && Time.BucketMs == 1001 && Time.Seconds == double(1001) / 1000.);
    TestTrue(TEXT("Sub-millisecond instants use the ceiling bucket"), FLDCaptureEventTime::FromSeconds(1.0002, Time, Error)
        && Time.BucketMs == 1001);
    TestFalse(TEXT("Negative instants are refused"), FLDCaptureEventTime::FromMs(-1, Time, Error));

    FLDPublicMatchView Latest;
    FLDPublicMatchView Open;
    Open.Generation = 2;
    Open.PublicRevision = 5;
    Open.Phase = ELDMatchPhase::CaptureOpen;
    Open.ServerNowMs = 130000;
    Open.OpenAtMs = 120000;
    Open.CircleState = ELDCircleState::Solo;
    Open.Occupier = FGuid::NewGuid();
    Open.CaptureEpoch = 3;
    Open.SoloSinceMs = 125000;
    Open.DueAtMs = 150000;
    TestEqual(TEXT("A coherent public circle is accepted"),
        LDCapture::AcceptPublicMatchView(Open, Latest, Error), ELDLobbyProjectionResult::Accepted);
    TestEqual(TEXT("Repeated public revisions are stale"),
        LDCapture::AcceptPublicMatchView(Open, Latest, Error), ELDLobbyProjectionResult::Stale);
    auto Malformed = [&](TFunctionRef<void(FLDPublicMatchView&)> Mutate, const TCHAR* What)
    {
        FLDPublicMatchView Candidate = Open;
        ++Candidate.PublicRevision;
        Mutate(Candidate);
        FLDPublicMatchView Scratch = Latest;
        TestEqual(What, LDCapture::AcceptPublicMatchView(Candidate, Scratch, Error), ELDLobbyProjectionResult::Malformed);
    };
    Malformed([](FLDPublicMatchView& View) { View.Occupier.Invalidate(); }, TEXT("Solo without occupier"));
    Malformed([](FLDPublicMatchView& View) { View.CircleState = ELDCircleState::Contested; },
        TEXT("A contest exposes no running segment"));
    Malformed([](FLDPublicMatchView& View) { View.PausedProgressMs = 1000; }, TEXT("A running segment has no paused progress"));
    FLDPublicMatchView Paused = Open;
    ++Paused.PublicRevision;
    Paused.CircleState = ELDCircleState::Contested;
    Paused.SoloSinceMs = 0;
    Paused.DueAtMs = 0;
    Paused.PausedProgressMs = 5000;
    FLDPublicMatchView PausedLatest = Latest;
    TestEqual(TEXT("A contest names its paused holder and frozen progress"),
        LDCapture::AcceptPublicMatchView(Paused, PausedLatest, Error), ELDLobbyProjectionResult::Accepted);
    auto MalformedPause = [&](TFunctionRef<void(FLDPublicMatchView&)> Mutate, const TCHAR* What)
    {
        FLDPublicMatchView Candidate = Paused;
        Mutate(Candidate);
        FLDPublicMatchView Scratch = Latest;
        TestEqual(What, LDCapture::AcceptPublicMatchView(Candidate, Scratch, Error), ELDLobbyProjectionResult::Malformed);
    };
    MalformedPause([](FLDPublicMatchView& View) { View.PausedProgressMs = 0; }, TEXT("A paused holder has progress"));
    MalformedPause([](FLDPublicMatchView& View) { View.Occupier.Invalidate(); }, TEXT("Paused progress names its holder"));
    MalformedPause([](FLDPublicMatchView& View) { View.PausedProgressMs = 10001; },
        TEXT("Paused progress cannot exceed the open time"));
    MalformedPause([](FLDPublicMatchView& View) { View.CircleState = ELDCircleState::Empty; },
        TEXT("An empty circle holds no progress"));
    Malformed([](FLDPublicMatchView& View) { View.GuardDueAtMs = 140000; }, TEXT("A bare flagpole has no guard"));
    FLDPublicMatchView Owned = Open;
    Owned.PublicRevision = Open.PublicRevision + 2;
    Owned.GraffitiOwner = FGuid::NewGuid();
    Owned.GraffitiPresentation = 2;
    Owned.GuardKeptMs = 4000;
    FLDPublicMatchView OwnedLatest = Latest;
    TestEqual(TEXT("A sole sprayer under a graffiti shows the paused guard"),
        LDCapture::AcceptPublicMatchView(Owned, OwnedLatest, Error), ELDLobbyProjectionResult::Accepted);
    auto MalformedOwned = [&](const FLDPublicMatchView& Base, TFunctionRef<void(FLDPublicMatchView&)> Mutate, const TCHAR* What)
    {
        FLDPublicMatchView Candidate = Base;
        Mutate(Candidate);
        FLDPublicMatchView Scratch = Latest;
        TestEqual(What, LDCapture::AcceptPublicMatchView(Candidate, Scratch, Error), ELDLobbyProjectionResult::Malformed);
    };
    MalformedOwned(Owned, [](FLDPublicMatchView& View) { View.GuardDueAtMs = 140000; }, TEXT("A paused guard has no win instant"));
    MalformedOwned(Owned, [](FLDPublicMatchView& View) { View.GuardKeptMs = -1; }, TEXT("Kept guard progress is not negative"));
    MalformedOwned(Owned, [](FLDPublicMatchView& View) { View.Occupier = View.GraffitiOwner; }, TEXT("The owner never sprays"));
    MalformedOwned(Owned, [](FLDPublicMatchView& View) { View.GraffitiPresentation = 6; }, TEXT("The owner's tint is a roster slot"));
    FLDPublicMatchView Guarded = Owned;
    Guarded.CircleState = ELDCircleState::Guarded;
    Guarded.Occupier.Invalidate();
    Guarded.SoloSinceMs = Guarded.DueAtMs = Guarded.GuardKeptMs = 0;
    Guarded.GuardDueAtMs = 140000;
    FLDPublicMatchView GuardedLatest = Latest;
    TestEqual(TEXT("The owner alone in the zone shows his running guard"),
        LDCapture::AcceptPublicMatchView(Guarded, GuardedLatest, Error), ELDLobbyProjectionResult::Accepted);
    MalformedOwned(Guarded, [](FLDPublicMatchView& View) { View.GuardDueAtMs = 0; }, TEXT("A running guard names its win instant"));
    MalformedOwned(Guarded, [](FLDPublicMatchView& View) { View.GraffitiOwner.Invalidate(); View.GraffitiPresentation = 0; View.GuardDueAtMs = 0; },
        TEXT("A guarded zone names its owner"));
    FLDPublicMatchView Frozen = Guarded;
    Frozen.CircleState = ELDCircleState::Contested;
    Frozen.GuardDueAtMs = 0;
    Frozen.GuardKeptMs = 4000;
    FLDPublicMatchView FrozenLatest = Latest;
    TestEqual(TEXT("A contest under a graffiti shows the frozen guard"),
        LDCapture::AcceptPublicMatchView(Frozen, FrozenLatest, Error), ELDLobbyProjectionResult::Accepted);
    MalformedOwned(Frozen, [](FLDPublicMatchView& View) { View.GuardDueAtMs = 140000; View.GuardKeptMs = 0; },
        TEXT("A contest never runs the owner's guard"));
    // 2026-11-07 user decision: the owner away from an empty zone pauses his guard at its kept value.
    FLDPublicMatchView Away = Frozen;
    Away.CircleState = ELDCircleState::Empty;
    FLDPublicMatchView AwayLatest = Latest;
    TestEqual(TEXT("An empty zone under a graffiti shows the paused guard"),
        LDCapture::AcceptPublicMatchView(Away, AwayLatest, Error), ELDLobbyProjectionResult::Accepted);
    MalformedOwned(Away, [](FLDPublicMatchView& View) { View.GuardDueAtMs = 140000; View.GuardKeptMs = 0; },
        TEXT("An empty zone never runs the owner's guard"));
    MalformedOwned(Away, [](FLDPublicMatchView& View) { View.GuardKeptMs = 10001; },
        TEXT("A paused guard cannot exceed the open time"));
    Malformed([](FLDPublicMatchView& View) { View.Phase = ELDMatchPhase::Development; },
        TEXT("Development cannot publish an open circle"));
    Malformed([](FLDPublicMatchView& View) { View.Phase = ELDMatchPhase::Lobby; }, TEXT("Lobby publishes no capture state"));
    Malformed([](FLDPublicMatchView& View) { View.Phase = ELDMatchPhase::Results; }, TEXT("Results need a result identity"));

    FLDResultView Result;
    Result.Generation = 2;
    Result.ResultId = FGuid::NewGuid();
    Result.ResultSerial = 1;
    Result.Reason = ELDMatchEndReason::GuardSucceeded;
    Result.Winner = Open.Occupier;
    Result.StartedAtMs = 0;
    Result.EndedAtMs = 150000;
    Result.DurationMs = 150000;
    Result.Rows.Add({Open.Occupier, TEXT("Host"), ELDContenderKind::Human, false, 2, 1, 6});
    Result.Rows.Add({FGuid::NewGuid(), TEXT("Guest"), ELDContenderKind::Human, true, 0, 3, 3});
    Result.Rows.Add({FGuid::NewGuid(), TEXT("AI 对手 1"), ELDContenderKind::OpponentAI, false, 1, 0, 5});
    FLDResultView LatestResult;
    TestEqual(TEXT("A frozen result with a listed winner is accepted"),
        LDCapture::AcceptResultView(Result, LatestResult, Error), ELDLobbyProjectionResult::Accepted);
    TestEqual(TEXT("The result is published once"),
        LDCapture::AcceptResultView(Result, LatestResult, Error), ELDLobbyProjectionResult::Stale);
    auto BadResult = [&](TFunctionRef<void(FLDResultView&)> Mutate, const TCHAR* What)
    {
        FLDResultView Candidate = Result;
        Candidate.Generation = 3;
        Mutate(Candidate);
        FLDResultView Scratch = LatestResult;
        TestEqual(What, LDCapture::AcceptResultView(Candidate, Scratch, Error), ELDLobbyProjectionResult::Malformed);
    };
    BadResult([](FLDResultView& View) { View.Reason = ELDMatchEndReason::HostEnded; }, TEXT("Host end has no winner"));
    BadResult([](FLDResultView& View) { View.Winner = FGuid::NewGuid(); }, TEXT("The winner must be a listed contender"));
    BadResult([](FLDResultView& View) { View.Rows[0].bLeft = true; }, TEXT("A contender who left cannot win"));
    BadResult([](FLDResultView& View) { View.DurationMs = 1; }, TEXT("Duration comes from the real boundaries"));
    BadResult([](FLDResultView& View) { View.Rows[2].FinalLevel = uint8(LDDemoProgression::MaxLevel + 1); },
        TEXT("Final level stays within the approved range"));
    BadResult([](FLDResultView& View) { View.Rows[2].FinalLevel = 0; }, TEXT("A final level is at least 1"));
    {
        // #184: a guard win with contenders above the old level 10 locked a result its own validation refused.
        FLDResultView Top = Result;
        Top.Generation = 3;
        Top.Rows[0].FinalLevel = uint8(LDDemoProgression::MaxLevel);
        Top.Rows[2].FinalLevel = 11;
        FLDResultView Scratch = LatestResult;
        TestEqual(TEXT("A result with a winner at the approved level cap is accepted"),
            LDCapture::AcceptResultView(Top, Scratch, Error), ELDLobbyProjectionResult::Accepted);
    }
    FLDResultView HostEnded = Result;
    HostEnded.Generation = 3;
    HostEnded.Reason = ELDMatchEndReason::HostEnded;
    HostEnded.Winner.Invalidate();
    TestEqual(TEXT("Host-ended match has no winner and still lists every contender"),
        LDCapture::AcceptResultView(HostEnded, LatestResult, Error), ELDLobbyProjectionResult::Accepted);

    FLDKillFeedView Feed;
    Feed.Generation = 2;
    Feed.Notices.Add({1, Result.Rows[1].ContenderId, Open.Occupier, ELDKillCause::Contender, 1000});
    Feed.Notices.Add({2, Open.Occupier, FGuid(), ELDKillCause::CampHigh, 2000});
    FLDKillFeedView LatestFeed;
    TestEqual(TEXT("Public kill notices are accepted"), LDCapture::AcceptKillFeed(Feed, LatestFeed, Error),
        ELDLobbyProjectionResult::Accepted);
    TestEqual(TEXT("A repeated feed is stale"), LDCapture::AcceptKillFeed(Feed, LatestFeed, Error),
        ELDLobbyProjectionResult::Stale);
    auto BadFeed = [&](TFunctionRef<void(FLDKillFeedView&)> Mutate, const TCHAR* What)
    {
        FLDKillFeedView Candidate = Feed;
        Candidate.Generation = 3;
        Mutate(Candidate);
        FLDKillFeedView Scratch = LatestFeed;
        TestEqual(What, LDCapture::AcceptKillFeed(Candidate, Scratch, Error), ELDLobbyProjectionResult::Malformed);
    };
    BadFeed([](FLDKillFeedView& View) { View.Notices[1].Killer = FGuid::NewGuid(); }, TEXT("Only contender kills name a killer"));
    BadFeed([](FLDKillFeedView& View) { View.Notices[0].Killer.Invalidate(); }, TEXT("A contender kill names its killer"));
    BadFeed([](FLDKillFeedView& View) { View.Notices[1].Serial = 1; }, TEXT("Notice serials strictly increase"));
    BadFeed([](FLDKillFeedView& View) { View.Notices[0].Killer = View.Notices[0].Victim; }, TEXT("A suicide names no killer"));
    FLDKillFeedView Scored = Feed;
    Scored.Generation = 4;
    Scored.Scores = {{Open.Occupier, 1, 1, 0}, {Result.Rows[1].ContenderId, 0, 1, 0}};
    TestEqual(TEXT("A feed with one score row per contender is accepted"), LDCapture::AcceptKillFeed(Scored, LatestFeed, Error),
        ELDLobbyProjectionResult::Accepted);
    BadFeed([](FLDKillFeedView& View) { View.Scores = {{FGuid(), 0, 0, 0}}; }, TEXT("A score row names a contender"));
    BadFeed([&Open](FLDKillFeedView& View) { View.Scores = {{Open.Occupier, 1, 0, 0}, {Open.Occupier, 0, 1, 0}}; },
        TEXT("A contender has one score row"));
    BadFeed([](FLDKillFeedView& View) { View.Scores.SetNum(LDCapture::MaxScoreRows + 1); for (FLDScoreRow& Row : View.Scores) { Row.ContenderId = FGuid::NewGuid(); } },
        TEXT("The scoreboard has at most one row per seat"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCaptureProviderGateTest, "Lockdown.Rules.DemoCapture.ProviderGate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCaptureProviderGateTest::RunTest(const FString& Parameters)
{
    {
        LDMatchProviders::FScopedTestTable Empty({});
        TestEqual(TEXT("Normal start names every missing core provider"),
            LDMatchProviders::MissingForStart(false), FString(TEXT("Combat, Progression, Camps")));
    }
    FLDMatchProviderHooks Plain;
    FLDMatchProviderHooks Levels;
    Levels.ReadFinalLevel = [](const ALDGameMode&, const FGuid&, uint8& Level, FString&) { Level = 1; return true; };
    FLDMatchProviderHooks AI;
    AI.Prepare = [](ALDGameMode&, const FLDMatchStartContext&, FString&) { return true; };
    {
        LDMatchProviders::FScopedTestTable Partial({{ELDMatchProvider::Combat, Plain}, {ELDMatchProvider::Progression, Plain},
            {ELDMatchProvider::Camps, Plain}});
        TestEqual(TEXT("Progression must provide committed final levels"),
            LDMatchProviders::MissingForStart(false), FString(TEXT("Progression")));
    }
    {
        LDMatchProviders::FScopedTestTable Wired({{ELDMatchProvider::Combat, Plain}, {ELDMatchProvider::Progression, Levels},
            {ELDMatchProvider::Camps, Plain}});
        TestTrue(TEXT("Wired core providers open a human-only start"), LDMatchProviders::MissingForStart(false).IsEmpty());
        TestEqual(TEXT("AI slots require the opponent AI provider"),
            LDMatchProviders::MissingForStart(true), FString(TEXT("OpponentAI")));
    }
    {
        LDMatchProviders::FScopedTestTable WithAI({{ELDMatchProvider::Combat, Plain}, {ELDMatchProvider::Progression, Levels},
            {ELDMatchProvider::Camps, Plain}, {ELDMatchProvider::OpponentAI, AI}});
        TestTrue(TEXT("An AI provider that binds slots satisfies the AI gate"), LDMatchProviders::MissingForStart(true).IsEmpty());
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCaptureSessionTest, "Lockdown.Rules.DemoCapture.SessionEndgame",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCaptureSessionTest::RunTest(const FString& Parameters)
{
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    if (!TestNotNull(TEXT("Isolated endgame fixture world"), World)) { return false; }
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Instance = NewObject<ULDGameInstance>(GEngine);
    World->SetGameInstance(Instance);
    auto* Mode = World->SpawnActor<ALDGameMode>();
    auto* Public = World->SpawnActor<ALDGameState>();
    auto* Host = World->SpawnActor<ALDPlayerController>();
    auto* Guest = World->SpawnActor<ALDPlayerController>();
    if (!Mode || !Public || !Host || !Guest) { AddError(TEXT("Fixture actors failed to spawn")); return false; }
    Host->SetAsLocalPlayerController();
    FString Error;
    uint32 Generation = 0;
    if (!TestTrue(TEXT("The room allocates its first match generation"), Instance->AllocateMatchGeneration(Generation, Error)))
    { return false; }
    Mode->GameState = Public;
    Mode->MatchContext = {Generation, FGuid::NewGuid()};
    Mode->MatchPhase = ELDMatchPhase::Lobby;
    Mode->RosterRevision = 1;
    Mode->bDemoWorld = Mode->bEntryAccepted = true;
    // The chosen name travels as ?LDName=; the engine's ?Name= (online subsystem nickname) never names a contender.
    FString Name;
    TestTrue(TEXT("An absent chosen name takes the explicit default"),
        LDMatchLifecycle::ResolveContenderName(TEXT("?listen?Name=Fak-B33A66484D3FB6B1"), Name, Error)
        && Name.Equals(LDMatchLifecycle::DefaultContenderName, ESearchCase::CaseSensitive));
    TestTrue(TEXT("The chosen name wins over the engine nickname and keeps its inner space"),
        LDMatchLifecycle::ResolveContenderName(TEXT("?Name=Fak-1?LDName=同学 乙?LDWire=4"), Name, Error)
        && Name.Equals(TEXT("同学 乙"), ESearchCase::CaseSensitive));
    for (const TCHAR* Bad : {TEXT("?LDName="), TEXT("?LDName= 同学"), TEXT("?LDName=a/b"), TEXT("?LDName=12345678901234567")})
    {
        TestTrue(TEXT("An empty, padded, reserved-character or overlong chosen name is refused explicitly"),
            !LDMatchLifecycle::ResolveContenderName(Bad, Name, Error) && Error.StartsWith(TEXT("InvalidName: ")) && Name.IsEmpty());
    }
    if (!TestTrue(TEXT("The Listen Host joins the room"), Mode->AdmitRoomMember(Host, Error))) { AddError(Error); return false; }
    auto* GuestName = World->SpawnActor<ALDPlayerState>();
    if (!TestNotNull(TEXT("Fixture player state"), GuestName)) { return false; }
    Guest->PlayerState = GuestName;
    GuestName->SetPlayerName(TEXT("Fak-B33A66484D3FB6B1D501039CE5B3D81C"));
    const uint64 RosterBefore = Mode->RosterRevision;
    TestTrue(TEXT("An online subsystem nickname is refused at admission, not shown, and the roster is unchanged"),
        !Mode->AdmitRoomMember(Guest, Error) && Error.StartsWith(TEXT("InvalidName: ")) && Mode->RosterRevision == RosterBefore
        && !Mode->Contenders.Contains(Guest));
    GuestName->SetPlayerName(TEXT("同学 乙"));
    if (!TestTrue(TEXT("The second human joins with the chosen name"), Mode->AdmitRoomMember(Guest, Error)))
    { AddError(Error); return false; }
    TestTrue(TEXT("Admission keeps the chosen name; a nameless fixture takes the explicit default"),
        Mode->Contenders.Find(Guest) && Mode->Contenders.Find(Guest)->Name.Equals(TEXT("同学 乙"), ESearchCase::CaseSensitive)
        && Mode->Contenders.Find(Host) && (Host->PlayerState
            ? Mode->Contenders.Find(Host)->Name.Equals(Host->PlayerState->GetPlayerName(), ESearchCase::CaseSensitive)
            : Mode->Contenders.Find(Host)->Name.Equals(LDMatchLifecycle::DefaultContenderName, ESearchCase::CaseSensitive)));
    const FGuid GuestId = Mode->ContenderId(Guest);

    int32 Closed = 0, Resets = 0;
    FLDMatchReset LastReset;
    FLDMatchProviderHooks Progression;
    // #184: every contender at the approved level cap, past the old level-10 bound that faulted the result lock.
    Progression.ReadFinalLevel = [](const ALDGameMode&, const FGuid&, uint8& Level, FString&) { Level = uint8(LDDemoProgression::MaxLevel); return true; };
    Progression.MatchClosed = [&Closed](ALDGameMode&, const FLDMatchClosed& Info)
    { Closed += Info.Reason == ELDMatchEndReason::HostEnded && !Info.Winner.IsValid() ? 1 : 100; };
    Progression.MatchReset = [&Resets, &LastReset](ALDGameMode&, const FLDMatchReset& Reset, FString&)
    { ++Resets; LastReset = Reset; return true; };
    LDMatchProviders::FScopedTestTable Providers({{ELDMatchProvider::Progression, Progression}});

    // A running match without native bodies: only the core gate, clock and reducer matter here.
    Mode->MatchPhase = ELDMatchPhase::Development;
    TestTrue(TEXT("The approved timing starts the reducer"), Mode->CaptureReducer.Begin(120000, 3500, 20000, Error));
    FLDSessionRequest Request;
    Request.Key = {Generation, 1};
    Request.Command = ELDSessionCommand::EndMatch;
    Request.ExpectedRosterRevision = Mode->RosterRevision;
    TestTrue(TEXT("Only the Host can end the match"), Mode->ProcessSessionCommand(Guest, Request).Reason == ELDSessionReject::NotHost);
    const auto Ended = Mode->ProcessSessionCommand(Host, Request);
    TestTrue(TEXT("The Host ends the running match"), Ended.Outcome == ELDSessionOutcome::Committed && Mode->MatchPhase == ELDMatchPhase::Results);
    TestFalse(TEXT("Ending closes every gameplay write"), Mode->IsGameplayWriteOpen(Mode->MatchContext));
    TestEqual(TEXT("Cleanup runs once after the gate closed"), Closed, 1);
    const FLDResultView& Result = Public->MatchResult;
    TestTrue(TEXT("A host-ended result has no winner and lists every contender"),
        Result.Generation == Generation && Result.Reason == ELDMatchEndReason::HostEnded && !Result.Winner.IsValid()
        && Result.Rows.Num() == 2 && Result.Rows[0].FinalLevel == LDDemoProgression::MaxLevel && Result.DurationMs == Result.EndedAtMs - Result.StartedAtMs);
    TestTrue(TEXT("The public circle names the locked result"),
        Public->PublicMatch.Phase == ELDMatchPhase::Results && Public->PublicMatch.ResultId == Result.ResultId);
    Request.Key.RequestSequence = 2;
    TestTrue(TEXT("A second end cannot replace the locked result"),
        Mode->ProcessSessionCommand(Host, Request).Reason == ELDSessionReject::MatchClosed && Closed == 1);

    Mode->FindContender(GuestId)->bLeft = true;
    Request.Key.RequestSequence = 3;
    Request.Command = ELDSessionCommand::ReturnToLobby;
    Request.ExpectedResult = FGuid::NewGuid();
    TestTrue(TEXT("Returning names the exact locked result"),
        Mode->ProcessSessionCommand(Host, Request).Reason == ELDSessionReject::StaleMatch && Mode->MatchPhase == ELDMatchPhase::Results);
    Request.Key.RequestSequence = 4;
    Request.ExpectedResult = Result.ResultId;
    const FGuid OldLedger = Mode->MatchContext.LedgerMatch;
    const auto Returned = Mode->ProcessSessionCommand(Host, Request);
    TestTrue(TEXT("The Host takes the room back to the Lobby"),
        Returned.Outcome == ELDSessionOutcome::Committed && Mode->MatchPhase == ELDMatchPhase::Lobby);
    TestTrue(TEXT("The next match has a new network and ledger identity"),
        Mode->MatchContext.Generation > Generation && Mode->MatchContext.LedgerMatch != OldLedger);
    TestTrue(TEXT("Every provider retired the old match once"), Resets == 1 && LastReset.Old.Generation == Generation
        && LastReset.New == Mode->MatchContext && !LastReset.bAbortedStart);
    TestTrue(TEXT("A contender who left is dropped and connected humans stay unready"),
        !Mode->FindContender(GuestId) && Mode->Contenders.Num() == 1 && !Mode->StateFor(Host)->bReady);
    TestTrue(TEXT("The result and reducer belong to the old match only"),
        !Mode->bResultLocked && !Mode->CaptureReducer.IsActive() && Public->PublicMatch.Generation == Mode->MatchContext.Generation
        && Public->PublicMatch.Phase == ELDMatchPhase::Lobby);
    Request.Key.RequestSequence = 5;
    Request.Command = ELDSessionCommand::SetReady;
    Request.ExpectedResult.Invalidate();
    Request.Ready = true;
    Request.ExpectedRosterRevision = Mode->RosterRevision;
    TestTrue(TEXT("Old-generation commands cannot touch the new Lobby"),
        Mode->ProcessSessionCommand(Host, Request).Reason == ELDSessionReject::StaleMatch);
    Request.Key = {Mode->MatchContext.Generation, 1};
    TestTrue(TEXT("The new generation starts a fresh request window"),
        Mode->ProcessSessionCommand(Host, Request).Outcome == ELDSessionOutcome::Committed && Mode->StateFor(Host)->bReady);
    return true;
}
#endif
