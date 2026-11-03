#include "Framework/LDCaptureReducer.h"
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
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCaptureContinuityTest, "Lockdown.Rules.DemoCapture.Continuity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCaptureContinuityTest::RunTest(const FString& Parameters)
{
    FCaptureReducerProbe Probe{*this};
    const FGuid A = FGuid::NewGuid(), B = FGuid::NewGuid(), C = FGuid::NewGuid();
    const FLDCombatBodyRef BodyA = CaptureTestBody(A, 1), BodyB = CaptureTestBody(B, 1), BodyC = CaptureTestBody(C, 1);
    TestTrue(TEXT("Approved opening and hold start the reducer"), Probe.Reducer.Begin(1000, 500, Probe.Error));
    Probe.Add(CaptureJoined(BodyA, true), TEXT("A joins inside"));
    Probe.Add(CaptureJoined(BodyB, false), TEXT("B joins outside"));
    Probe.Add(CaptureJoined(BodyC, false), TEXT("C joins outside"));
    Probe.Close(999, false, TEXT("Development never completes a capture"));
    TestTrue(TEXT("Closed circle waits for its exact opening boundary"),
        Probe.Reducer.GetState() == ELDCircleState::NotOpen && Probe.Reducer.NextDueMs() == 1000);
    Probe.Close(1000, false, TEXT("Opening starts the first segment"));
    Probe.ExpectSolo(A, 1000, 1500, TEXT("Occupancy before opening is not credited"));
    const uint64 FirstEpoch = Probe.Reducer.GetEpoch();

    Probe.Add(CapturePresence(BodyB, CaptureSeconds(1.2003), true), TEXT("B enters"));
    Probe.Close(1201, false, TEXT("Contest has no completion"));
    TestTrue(TEXT("A second eligible body contests and pauses the holder's progress"),
        Probe.Reducer.GetState() == ELDCircleState::Contested && Probe.Reducer.GetOccupier() == A
        && Probe.Reducer.GetPausedProgressMs() == 201 && Probe.Reducer.GetSoloSinceMs() == 0
        && Probe.Reducer.GetDueAtMs() == 0 && Probe.Reducer.NextDueMs() == MAX_int64);
    Probe.Add(CapturePresence(BodyB, CaptureMs(1300), false), TEXT("B leaves"));
    Probe.Close(1300, false, TEXT("Re-solo does not complete"));
    Probe.ExpectSolo(A, 1099, 1599, TEXT("The holder resumes from his paused progress"));

    Probe.Add(CapturePresence(BodyB, CaptureSeconds(1.4001), true), TEXT("B touches in"));
    Probe.Add(CapturePresence(BodyB, CaptureSeconds(1.4002), false), TEXT("B leaves within the same boundary"));
    Probe.Close(1401, false, TEXT("Intermediate contest inside one boundary"));
    Probe.ExpectSolo(A, 1099, 1599, TEXT("An enter-then-exit inside one boundary pauses and resumes without loss"));
    TestTrue(TEXT("Every pause and resume advances the public epoch"), Probe.Reducer.GetEpoch() == FirstEpoch + 4);

    Probe.Add(CapturePresence(BodyC, CaptureSeconds(1.5004), true), TEXT("C enters"));
    Probe.Add(CaptureFact(ELDCaptureEventKind::Died, BodyC, CaptureMs(1501), ELDCommitPhase::DeathsAndRewards), TEXT("C dies"));
    Probe.Close(1501, false, TEXT("Enter then die in one boundary"));
    Probe.ExpectSolo(A, 1099, 1599, TEXT("A contender who entered before dying paused the segment without clearing it"));
    TestTrue(TEXT("The intermediate contest was reduced, not collapsed"), Probe.Reducer.GetEpoch() == FirstEpoch + 6);

    Probe.Close(1598, false, TEXT("Quiet boundary before completion"));
    Probe.Add(CapturePresence(BodyB, CaptureSeconds(1.5985), true), TEXT("B enters inside the completion bucket"));
    Probe.Close(1599, false, TEXT("A contest in the completion bucket prevents the win"));
    TestTrue(TEXT("Due-bucket entry pauses the full progress"), Probe.Reducer.GetState() == ELDCircleState::Contested
        && Probe.Reducer.GetOccupier() == A && Probe.Reducer.GetPausedProgressMs() == 500
        && Probe.Reducer.NextDueMs() == MAX_int64);
    Probe.Add(CapturePresence(BodyB, CaptureMs(1600), false), TEXT("B leaves again"));
    Probe.Add(CapturePresence(BodyB, CaptureSeconds(1.6005), true), TEXT("B enters after the completion instant"));
    TOptional<FLDCaptureProposal> Proposal;
    TestTrue(TEXT("Completion boundary closes"), Probe.Reducer.CloseBoundary(1600, Proposal, Probe.Error));
    TestTrue(TEXT("A fully paused holder completes the instant he is sole again; later facts cannot backfill it"),
        Proposal.IsSet() && Proposal->Occupier == A && Proposal->DueAtMs == 1600 && Proposal->SoloSinceMs == 1100
        && Proposal->Epoch == Probe.Reducer.GetEpoch());

    FCaptureReducerProbe Sub{*this};
    TestTrue(TEXT("Sub-millisecond reducer"), Sub.Reducer.Begin(0, 500, Sub.Error));
    Sub.Add(CaptureJoined(BodyA, false), TEXT("A joins outside"));
    Sub.Close(0, false, TEXT("Empty open circle"));
    TestTrue(TEXT("No eligible occupant is an empty circle"), Sub.Reducer.GetState() == ELDCircleState::Empty);
    Sub.Add(CapturePresence(BodyA, CaptureSeconds(1.0003), true), TEXT("A enters after a millisecond edge"));
    Sub.Close(1001, false, TEXT("Ceil bucket"));
    Sub.ExpectSolo(A, 1001, 1501, TEXT("A late sub-millisecond entry is never floored into an earlier start"));
    Sub.Add(CaptureFact(ELDCaptureEventKind::Left, BodyA, CaptureMs(1200), ELDCommitPhase::Session), TEXT("Occupier leaves"));
    Sub.Close(1200, false, TEXT("Leaving does not complete or award"));
    TestTrue(TEXT("A contender who left neither holds nor wins"), Sub.Reducer.GetState() == ELDCircleState::Empty
        && Sub.Reducer.NextDueMs() == MAX_int64);
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
    TestTrue(TEXT("Reducer begins"), Probe.Reducer.Begin(0, 500, Probe.Error));
    Probe.Add(CaptureJoined(BodyA, true), TEXT("A joins inside"));
    Probe.Add(CaptureJoined(BodyB, false), TEXT("B joins outside"));
    Probe.Add(CaptureJoined(BodyC, false), TEXT("C joins outside"));
    Probe.Close(0, false, TEXT("Open"));
    Probe.Add(CapturePresence(BodyB, CaptureMs(100), true), TEXT("B enters"));
    Probe.Close(100, false, TEXT("Contest"));
    ExpectPaused(Probe, A, 100, TEXT("The holder's progress is paused"));
    Probe.Close(300, false, TEXT("A long contest"));
    ExpectPaused(Probe, A, 100, TEXT("Nobody accumulates while contested"));
    Probe.Add(CapturePresence(BodyA, CaptureMs(350), false), TEXT("The paused holder leaves the circle"));
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
    Probe.ExpectSolo(A, 500, 1000, TEXT("The next sole contender starts from zero"));

    Probe.Add(CapturePresence(BodyC, CaptureMs(600), true), TEXT("C enters"));
    Probe.Add(CaptureFact(ELDCaptureEventKind::Left, BodyA, CaptureMs(650), ELDCommitPhase::Session),
        TEXT("The holder leaves the match during the contest"));
    Probe.Close(650, false, TEXT("Departure"));
    Probe.ExpectSolo(C, 650, 1150, TEXT("A departed holder forfeits his paused progress"));
    Probe.Add(CapturePresence(BodyC, CaptureMs(700), false), TEXT("C exits while sole"));
    Probe.Add(CapturePresence(BodyC, CaptureMs(800), true), TEXT("C returns"));
    Probe.Close(800, false, TEXT("Empty then sole"));
    Probe.ExpectSolo(C, 800, 1300, TEXT("An empty circle clears progress, even for the same contender"));

    FCaptureReducerProbe Same{*this};
    TestTrue(TEXT("Reducer begins"), Same.Reducer.Begin(0, 500, Same.Error));
    Same.Add(CaptureJoined(BodyA, true), TEXT("A joins inside"));
    Same.Add(CaptureJoined(BodyB, false), TEXT("B joins outside"));
    Same.Close(0, false, TEXT("Open"));
    Same.Add(CapturePresence(BodyB, CaptureSeconds(.2002), true), TEXT("B enters"));
    Same.Add(CapturePresence(BodyA, CaptureSeconds(.2004), false), TEXT("A leaves at a later instant of the same boundary"));
    Same.Close(201, false, TEXT("Same-boundary handoff"));
    Same.ExpectSolo(B, 201, 701, TEXT("Facts of one boundary are reduced in order: the holder's exit hands nothing over"));

    FCaptureReducerProbe Cap{*this};
    TestTrue(TEXT("Reducer begins"), Cap.Reducer.Begin(0, 500, Cap.Error));
    Cap.Add(CaptureJoined(BodyA, true), TEXT("A joins inside"));
    Cap.Add(CaptureJoined(BodyB, false), TEXT("B joins outside"));
    Cap.Close(0, false, TEXT("Open"));
    Cap.Add(CapturePresence(BodyB, CaptureMs(450), true), TEXT("B enters late in the segment"));
    Cap.Close(450, false, TEXT("Late contest"));
    ExpectPaused(Cap, A, 450, TEXT("A late contest adds no driver stop of its own"));
    FCaptureReducerProbe Skipped = Cap;
    Skipped.Add(CapturePresence(BodyB, CaptureMs(460), false), TEXT("B leaves"));
    TestEqual(TEXT("The motion prefix shows the resumed completion inside the step"),
        Skipped.Reducer.CompletionInsideMs(520), int64(510));
    TOptional<FLDCaptureProposal> Proposal;
    TestFalse(TEXT("A resumed completion closed as an ordinary boundary fails loudly"),
        Skipped.Reducer.CloseBoundary(520, Proposal, Skipped.Error) || Proposal.IsSet());
    FCaptureReducerProbe Hidden = Cap;
    Hidden.Add(CapturePresence(BodyB, CaptureMs(460), false), TEXT("B leaves"));
    Hidden.Add(CapturePresence(BodyB, CaptureMs(515), true), TEXT("B re-enters after the resumed completion"));
    TestFalse(TEXT("A later contest cannot hide a skipped resumed completion"),
        Hidden.Reducer.CloseBoundary(520, Proposal, Hidden.Error) || Proposal.IsSet());
    FCaptureReducerProbe Inside = Cap;
    Inside.Add(CapturePresence(BodyB, CaptureMs(460), false), TEXT("B leaves"));
    Inside.Add(CapturePresence(BodyB, CaptureMs(515), true), TEXT("B re-enters after the resumed completion"));
    TestTrue(TEXT("The step closes through the resumed completion"),
        Inside.Reducer.CloseThroughCompletion(520, Proposal, Inside.Error));
    TestTrue(TEXT("The holder wins at his exact resumed due instant; a later entry inside the step cannot undo it"),
        Proposal.IsSet() && Proposal->Occupier == A && Proposal->SoloSinceMs == 10 && Proposal->DueAtMs == 510
        && Proposal->Epoch == Inside.Reducer.GetEpoch() && Inside.Reducer.GetClosedThroughMs() == 520);
    FCaptureReducerProbe Interrupted = Cap;
    Interrupted.Add(CapturePresence(BodyB, CaptureMs(460), false), TEXT("B leaves"));
    Interrupted.Add(CapturePresence(BodyB, CaptureSeconds(.5095), true), TEXT("B re-enters in the due bucket"));
    TestEqual(TEXT("A contest in the due bucket still prevents the resumed win"),
        Interrupted.Reducer.CompletionInsideMs(520), MAX_int64);
    TestFalse(TEXT("Closing through a completion that did not happen fails loudly"),
        Interrupted.Reducer.CloseThroughCompletion(520, Proposal, Interrupted.Error) || Proposal.IsSet());
    Cap.Add(CapturePresence(BodyB, CaptureMs(460), false), TEXT("B leaves"));
    Cap.Close(500, false, TEXT("The capped boundary"));
    Cap.ExpectSolo(A, 10, 510, TEXT("The resumed segment keeps its paused progress"));
    Cap.Close(510, true, TEXT("The resumed segment completes at its real due instant"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCapturePausedContestStepsTest, "Lockdown.Rules.DemoCapture.PausedContestSteps",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCapturePausedContestStepsTest::RunTest(const FString& Parameters)
{
    // The native driver's longest step (CMC MaxSimulationTimeStep) and the approved hold.
    constexpr int64 MaxStepMs = 50, HoldMs = 25000, ContestMs = 2000;
    const FGuid A = FGuid::NewGuid(), B = FGuid::NewGuid();
    const FLDCombatBodyRef BodyA = CaptureTestBody(A, 1), BodyB = CaptureTestBody(B, 1);
    FCaptureReducerProbe Probe{*this};
    TestTrue(TEXT("Reducer begins"), Probe.Reducer.Begin(0, HoldMs, Probe.Error));
    Probe.Add(CaptureJoined(BodyA, true), TEXT("A joins inside"));
    Probe.Add(CaptureJoined(BodyB, false), TEXT("B joins outside"));
    Probe.Close(0, false, TEXT("Open"));
    Probe.Add(CapturePresence(BodyB, CaptureMs(HoldMs - 1), true), TEXT("B enters one millisecond before completion"));
    Probe.Close(HoldMs - 1, false, TEXT("Contest freezes nearly the full hold"));
    TestTrue(TEXT("The holder is paused one millisecond short"), Probe.Reducer.GetState() == ELDCircleState::Contested
        && Probe.Reducer.GetOccupier() == A && Probe.Reducer.GetPausedProgressMs() == HoldMs - 1);
    // Step the contest exactly as the authority driver does: the native step cap or the reducer's next stop.
    int64 Now = HoldMs - 1;
    int32 Steps = 0;
    while (Now < HoldMs - 1 + ContestMs && Steps <= ContestMs)
    {
        const int64 End = FMath::Min(Now + MaxStepMs, Probe.Reducer.NextDueMs());
        if (!Probe.Close(End, false, TEXT("A quiet contest boundary"))) { return false; }
        Now = End;
        ++Steps;
    }
    TestTrue(FString::Printf(TEXT("A quiet paused contest runs native-length steps (%d steps for %lld ms)"), Steps, ContestMs),
        Steps <= ContestMs / MaxStepMs);
    // The step in which the holder becomes sole again closes at his exact resumed due instant.
    const int64 End = Now + MaxStepMs;
    Probe.Add(CapturePresence(BodyB, CaptureSeconds((double(Now) + 6.5) / 1000.), false), TEXT("B leaves inside the step"));
    TestEqual(TEXT("Only this step's motion prefix reveals the inside completion"), Probe.Reducer.CompletionInsideMs(End), Now + 8);
    TOptional<FLDCaptureProposal> Proposal;
    TestTrue(TEXT("The resumed step closes through its completion"),
        Probe.Reducer.CloseThroughCompletion(End, Proposal, Probe.Error));
    TestTrue(TEXT("The resumed hold completes exactly one millisecond after the resume"),
        Proposal.IsSet() && Proposal->Occupier == A && Proposal->DueAtMs == Now + 8
        && Proposal->DueAtMs - Proposal->SoloSinceMs == HoldMs);
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
        TestTrue(TEXT("Reducer begins"), Probe.Reducer.Begin(0, 500, Probe.Error));
        Probe.Add(CaptureJoined(BodyA, true), TEXT("A joins inside"));
        Probe.Add(CaptureJoined(FirstB, false), TEXT("B joins outside"));
        Probe.Close(0, false, TEXT("Open"));
        Probe.Add(CaptureFact(ELDCaptureEventKind::Died, FirstB, CaptureMs(100), ELDCommitPhase::DeathsAndRewards), TEXT("B dies"));
        Probe.Close(100, false, TEXT("Death"));
        Probe.Add(CaptureFact(ELDCaptureEventKind::Respawned, SecondB, CaptureMs(200), ELDCommitPhase::RespawnsAndWorld,
            true, ProtectedUntilMs), TEXT("B respawns protected inside the circle"));
        Probe.Close(200, false, TEXT("Protected respawn"));
        Probe.ExpectSolo(A, 0, 500, TEXT("A protected body neither advances nor interrupts"));
    };

    FCaptureReducerProbe AtDue{*this};
    Prepare(AtDue, 500);
    AtDue.Add(CaptureFact(ELDCaptureEventKind::ProtectionEnded, SecondB, CaptureMs(500), ELDCommitPhase::Session),
        TEXT("Protection expires at the completion instant"));
    AtDue.Close(500, false, TEXT("Half-open protection end contests at the same instant"));
    TestTrue(TEXT("Expiry at the completion instant pauses the full progress"),
        AtDue.Reducer.GetState() == ELDCircleState::Contested && AtDue.Reducer.GetOccupier() == A
        && AtDue.Reducer.GetPausedProgressMs() == 500 && AtDue.Reducer.NextDueMs() == MAX_int64);
    AtDue.Add(CapturePresence(SecondB, CaptureMs(501), false), TEXT("B leaves"));
    AtDue.Close(501, true, TEXT("Sole again with the full paused progress completes at that instant"));

    FCaptureReducerProbe Later{*this};
    Prepare(Later, 600);
    Later.Close(500, true, TEXT("Protection ending after completion cannot rewrite it"));

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
    TestTrue(TEXT("Reducer begins"), Skip.Reducer.Begin(0, 500, Skip.Error));
    Skip.Add(CaptureJoined(BodyA, true), TEXT("A joins inside"));
    Skip.Close(0, false, TEXT("Open"));
    TestFalse(TEXT("A driver that skips the completion boundary fails loudly"),
        Skip.Reducer.CloseBoundary(501, Proposal, Skip.Error) || Proposal.IsSet());

    FCaptureReducerProbe Refuse{*this};
    TestTrue(TEXT("Reducer begins"), Refuse.Reducer.Begin(1000, 500, Refuse.Error));
    Refuse.Add(CaptureJoined(BodyA, false), TEXT("A joins"));
    TestFalse(TEXT("A contender cannot join twice"), Refuse.Reducer.Enqueue(CaptureJoined(BodyA, false), Refuse.Error));
    TestFalse(TEXT("Presence must name the current body"),
        Refuse.Reducer.Enqueue(CapturePresence(CaptureTestBody(A, 1), CaptureMs(10), true), Refuse.Error));
    TestFalse(TEXT("Unknown contenders cannot publish presence"),
        Refuse.Reducer.Enqueue(CapturePresence(FirstB, CaptureMs(10), true), Refuse.Error));
    TestFalse(TEXT("Respawn requires a committed death"), Refuse.Reducer.Enqueue(CaptureFact(ELDCaptureEventKind::Respawned,
        CaptureTestBody(A, 2), CaptureMs(10), ELDCommitPhase::RespawnsAndWorld, false, 100), Refuse.Error));
    TestFalse(TEXT("An unprotected life cannot end protection"), Refuse.Reducer.Enqueue(CaptureFact(
        ELDCaptureEventKind::ProtectionEnded, BodyA, CaptureMs(10), ELDCommitPhase::Session), Refuse.Error));
    Refuse.Add(CaptureFact(ELDCaptureEventKind::Died, BodyA, CaptureMs(10), ELDCommitPhase::DeathsAndRewards), TEXT("A dies"));
    TestFalse(TEXT("One life dies once"), Refuse.Reducer.Enqueue(
        CaptureFact(ELDCaptureEventKind::Died, BodyA, CaptureMs(11), ELDCommitPhase::DeathsAndRewards), Refuse.Error));
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
    Open.ServerNowMs = 190000;
    Open.OpenAtMs = 180000;
    Open.CircleState = ELDCircleState::Solo;
    Open.Occupier = FGuid::NewGuid();
    Open.CaptureEpoch = 3;
    Open.SoloSinceMs = 185000;
    Open.DueAtMs = 210000;
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
    Malformed([](FLDPublicMatchView& View) { View.Phase = ELDMatchPhase::Development; },
        TEXT("Development cannot publish an open circle"));
    Malformed([](FLDPublicMatchView& View) { View.Phase = ELDMatchPhase::Lobby; }, TEXT("Lobby publishes no capture state"));
    Malformed([](FLDPublicMatchView& View) { View.Phase = ELDMatchPhase::Results; }, TEXT("Results need a result identity"));

    FLDResultView Result;
    Result.Generation = 2;
    Result.ResultId = FGuid::NewGuid();
    Result.ResultSerial = 1;
    Result.Reason = ELDMatchEndReason::CaptureCompleted;
    Result.Winner = Open.Occupier;
    Result.StartedAtMs = 0;
    Result.EndedAtMs = 210000;
    Result.DurationMs = 210000;
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
    BadResult([](FLDResultView& View) { View.Rows[2].FinalLevel = 11; }, TEXT("Final level stays within the approved range"));
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
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCaptureProviderGateTest, "Lockdown.Rules.DemoCapture.ProviderGate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCaptureProviderGateTest::RunTest(const FString& Parameters)
{
    {
        LDMatchProviders::FScopedTestTable Empty({});
        TestEqual(TEXT("Normal start names every missing core provider"),
            LDMatchProviders::MissingForStart(false), FString(TEXT("Combat, Progression, Shop, Camps")));
    }
    FLDMatchProviderHooks Plain;
    FLDMatchProviderHooks Levels;
    Levels.ReadFinalLevel = [](const ALDGameMode&, const FGuid&, uint8& Level, FString&) { Level = 1; return true; };
    FLDMatchProviderHooks AI;
    AI.Prepare = [](ALDGameMode&, const FLDMatchStartContext&, FString&) { return true; };
    {
        LDMatchProviders::FScopedTestTable Partial({{ELDMatchProvider::Combat, Plain}, {ELDMatchProvider::Progression, Plain},
            {ELDMatchProvider::Shop, Plain}, {ELDMatchProvider::Camps, Plain}});
        TestEqual(TEXT("Progression must provide committed final levels"),
            LDMatchProviders::MissingForStart(false), FString(TEXT("Progression")));
    }
    {
        LDMatchProviders::FScopedTestTable Wired({{ELDMatchProvider::Combat, Plain}, {ELDMatchProvider::Progression, Levels},
            {ELDMatchProvider::Shop, Plain}, {ELDMatchProvider::Camps, Plain}});
        TestTrue(TEXT("Wired core providers open a human-only start"), LDMatchProviders::MissingForStart(false).IsEmpty());
        TestEqual(TEXT("AI slots require the opponent AI provider"),
            LDMatchProviders::MissingForStart(true), FString(TEXT("OpponentAI")));
    }
    {
        LDMatchProviders::FScopedTestTable WithAI({{ELDMatchProvider::Combat, Plain}, {ELDMatchProvider::Progression, Levels},
            {ELDMatchProvider::Shop, Plain}, {ELDMatchProvider::Camps, Plain}, {ELDMatchProvider::OpponentAI, AI}});
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
    Progression.ReadFinalLevel = [](const ALDGameMode&, const FGuid&, uint8& Level, FString&) { Level = 3; return true; };
    Progression.MatchClosed = [&Closed](ALDGameMode&, const FLDMatchClosed& Info)
    { Closed += Info.Reason == ELDMatchEndReason::HostEnded && !Info.Winner.IsValid() ? 1 : 100; };
    Progression.MatchReset = [&Resets, &LastReset](ALDGameMode&, const FLDMatchReset& Reset, FString&)
    { ++Resets; LastReset = Reset; return true; };
    LDMatchProviders::FScopedTestTable Providers({{ELDMatchProvider::Progression, Progression}});

    // A running match without native bodies: only the core gate, clock and reducer matter here.
    Mode->MatchPhase = ELDMatchPhase::Development;
    TestTrue(TEXT("The approved timing starts the reducer"), Mode->CaptureReducer.Begin(180000, 25000, Error));
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
        && Result.Rows.Num() == 2 && Result.Rows[0].FinalLevel == 3 && Result.DurationMs == Result.EndedAtMs - Result.StartedAtMs);
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
