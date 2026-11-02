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
    TestTrue(TEXT("A second eligible body contests and clears progress"),
        Probe.Reducer.GetState() == ELDCircleState::Contested && !Probe.Reducer.GetOccupier().IsValid()
        && Probe.Reducer.GetDueAtMs() == 0 && Probe.Reducer.NextDueMs() == MAX_int64);
    Probe.Add(CapturePresence(BodyB, CaptureMs(1300), false), TEXT("B leaves"));
    Probe.Close(1300, false, TEXT("Re-solo does not complete"));
    Probe.ExpectSolo(A, 1300, 1800, TEXT("The previous occupier restarts a complete segment"));

    Probe.Add(CapturePresence(BodyB, CaptureSeconds(1.4001), true), TEXT("B touches in"));
    Probe.Add(CapturePresence(BodyB, CaptureSeconds(1.4002), false), TEXT("B leaves within the same boundary"));
    Probe.Close(1401, false, TEXT("Intermediate contest inside one boundary"));
    Probe.ExpectSolo(A, 1401, 1901, TEXT("An enter-then-exit inside one boundary still breaks continuity"));
    TestTrue(TEXT("Every continuity break advances the public epoch"), Probe.Reducer.GetEpoch() == FirstEpoch + 4);

    Probe.Add(CapturePresence(BodyC, CaptureSeconds(1.5004), true), TEXT("C enters"));
    Probe.Add(CaptureFact(ELDCaptureEventKind::Died, BodyC, CaptureMs(1501), ELDCommitPhase::DeathsAndRewards), TEXT("C dies"));
    Probe.Close(1501, false, TEXT("Enter then die in one boundary"));
    Probe.ExpectSolo(A, 1501, 2001, TEXT("A contender who entered before dying broke the segment"));

    Probe.Close(2000, false, TEXT("Quiet boundary before completion"));
    Probe.Add(CapturePresence(BodyB, CaptureSeconds(2.0005), true), TEXT("B enters inside the completion bucket"));
    Probe.Close(2001, false, TEXT("A contest in the completion bucket prevents the win"));
    TestTrue(TEXT("Due-bucket entry contests"), Probe.Reducer.GetState() == ELDCircleState::Contested);
    Probe.Add(CapturePresence(BodyB, CaptureMs(2100), false), TEXT("B leaves again"));
    Probe.Close(2100, false, TEXT("A re-solo"));
    Probe.ExpectSolo(A, 2100, 2600, TEXT("Completion resets after the contest"));
    Probe.Add(CapturePresence(BodyB, CaptureSeconds(2.6005), true), TEXT("B enters after the completion instant"));
    TOptional<FLDCaptureProposal> Proposal;
    TestTrue(TEXT("Completion boundary closes"), Probe.Reducer.CloseBoundary(2600, Proposal, Probe.Error));
    TestTrue(TEXT("Later facts cannot backfill an earlier completion"),
        Proposal.IsSet() && Proposal->Occupier == A && Proposal->DueAtMs == 2600 && Proposal->SoloSinceMs == 2100
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
    TestTrue(TEXT("Expiry at the completion instant contests"), AtDue.Reducer.GetState() == ELDCircleState::Contested);

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
        TEXT("Contest cannot expose an occupier or progress"));
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
    Result.Rows.Add({FGuid::NewGuid(), TEXT("Opponent AI 1"), ELDContenderKind::OpponentAI, false, 1, 0, 5});
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
    if (!TestTrue(TEXT("Two humans join the room"), Mode->AdmitRoomMember(Host, Error) && Mode->AdmitRoomMember(Guest, Error)))
    { AddError(Error); return false; }
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
