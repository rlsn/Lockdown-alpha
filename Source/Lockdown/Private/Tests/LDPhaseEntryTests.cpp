#include "Framework/LDMatchTimeline.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/LDNativeMotionTypes.h"
#include "Development/LDDemoContentCatalog.h"
#include "Misc/AutomationTest.h"
#include "PK/LDPKRules.h"
#include "Tests/LDTestApprovedFixture.h"
#include "World/LDSpawnPlacement.h"

// #198 R02/R14/R16 (issue #198 review findings; approved outer parameters, issue #198 comment 6752601175 §1/§2/§8): a held
// body entry, the spawn request identity of a switched or retried placement, and the ring's real outside time.
namespace LDPhaseEntryTesting
{
const FLDTimelineEvent* FindEvent(const TArray<FLDTimelineEvent>& Events, ELDTimelineEventKind Kind)
{
    return Events.FindByPredicate([Kind](const FLDTimelineEvent& Event) { return Event.Kind == Kind; });
}

FLDPKParameters RingParameters()
{
    FLDPKParameters P;
    P.ArenaRadiusCm = 1800;
    P.MidRadiusCm = 600;
    P.FinalRadiusCm = 150;
    P.ShrinkStartMs = 10000;
    P.MidMs = 40000;
    P.FinalMs = 55000;
    P.PreviewMs = 10000;
    P.MaxMs = 60000;
    P.TickMs = 250;
    P.EarlyDamageBp = 500;
    P.MidDamageBp = 1200;
    P.FinalDamageBp = 2500;
    return P;
}

FLDNativeMotionRecord Move(uint64 Sequence, const FVector& From, const FVector& To, double StartSeconds, double EndSeconds)
{
    FLDNativeMotionRecord Record;
    Record.Sequence = Sequence;
    Record.Kind = ELDNativeMotionRecordKind::Movement;
    Record.FromFootCm = From;
    Record.ToFootCm = To;
    Record.StartServerSeconds = StartSeconds;
    Record.EndServerSeconds = EndSeconds;
    return Record;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDRestEntryHoldTest, "Lockdown.Rules.DemoRest.EntryHold",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDRestEntryHoldTest::RunTest(const FString& Parameters)
{
    using namespace LDPhaseEntryTesting;
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDTestApprovedFixture::Read(*this, Catalog)) { return false; }
    FLDTimelineParameters P;
    FString Error;
    if (!TestTrue(TEXT("The approved content yields the timeline"), FLDTimelineParameters::FromContent(Catalog->Get().Parameters, P, Error)))
    {
        return false;
    }
    bool bGood = TestEqual(TEXT("The approved entry wait is 30 s (outer parameters §1)"), Catalog->Get().Parameters.RestEntryReadyTimeoutMs, int64(30000));
    FLDMatchTimeline Line;
    TArray<FLDTimelineEvent> Events;
    if (!TestTrue(TEXT("The match begins in R0"), Line.Begin(P, 0, Error))) { return false; }
    // R0 with two entrants runs to the PK ready countdown.
    Line.Advance(P.OpeningBuildMs, 2, true, true, Events, Error);
    while (Line.GetStep() != ELDRestStep::PKReady) { Line.Advance(Line.NextDueMs(), 2, true, true, Events, Error); }
    const int64 PKDue = Line.NextDueMs();
    // A PK body that cannot stand freezes the entry: the PK neither starts nor uses up its time.
    Events.Reset();
    bGood &= TestTrue(TEXT("An unready PK entry holds at its boundary"), Line.Advance(PKDue, 2, true, false, Events, Error)
        && Events.IsEmpty() && Line.GetStep() == ELDRestStep::PKReady && Line.IsEntryHeld() && Line.IsEntryDue(PKDue + 7000));
    bGood &= TestTrue(TEXT("Still unready 7 s later: still held"), Line.Advance(PKDue + 7000, 2, true, false, Events, Error)
        && Events.IsEmpty() && Line.GetStep() == ELDRestStep::PKReady);
    bGood &= TestTrue(TEXT("Once every body stands the PK starts then, with its whole length"),
        Line.Advance(PKDue + 7250, 2, true, true, Events, Error) && Line.GetStep() == ELDRestStep::PK && !Line.IsEntryHeld()
        && FindEvent(Events, ELDTimelineEventKind::EnterStep) && FindEvent(Events, ELDTimelineEventKind::EnterStep)->AtMs == PKDue + 7250
        && Line.GetStepEndsAtMs() == PKDue + 7250 + P.PKMaxMs);
    while (Line.GetStep() != ELDRestStep::Return) { Line.Advance(Line.NextDueMs(), 2, true, true, Events, Error); }
    const int64 FieldDue = Line.NextDueMs();
    // A partially unready return: nobody stands, T stays frozen and the field (and in R3 the flagpole) does not start.
    Events.Reset();
    bGood &= TestTrue(TEXT("An unready field return holds: no field entry, T frozen"), Line.Advance(FieldDue, 0, true, false, Events, Error)
        && !FindEvent(Events, ELDTimelineEventKind::EnterField) && Line.IsRest() && !Line.IsFieldRunning()
        && Line.FieldMsAt(FieldDue + 2000) == 0 && Line.IsEntryHeld());
    bGood &= TestTrue(TEXT("The held return enters the field when every body stands; T starts there"),
        Line.Advance(FieldDue + 2000, 0, true, true, Events, Error) && FindEvent(Events, ELDTimelineEventKind::EnterField)
        && FindEvent(Events, ELDTimelineEventKind::EnterField)->AtMs == FieldDue + 2000 && Line.IsFieldRunning()
        && Line.FieldMsAt(FieldDue + 3000) == 1000 && Line.NextDueMs() == FieldDue + 2000 + P.FieldSegmentMs);
    // An unheld entry keeps its exact scheduled instant.
    while (Line.GetStage() != ELDMatchStage::Rest || Line.GetStep() != ELDRestStep::Return) { Line.Advance(Line.NextDueMs(), 0, true, true, Events, Error); }
    const int64 Field2Due = Line.NextDueMs();
    Events.Reset();
    bGood &= TestTrue(TEXT("A ready return enters at its scheduled boundary"), Line.Advance(Field2Due, 0, true, true, Events, Error)
        && FindEvent(Events, ELDTimelineEventKind::EnterField)->AtMs == Field2Due);
    return bGood;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDRestSpawnIdentityTest, "Lockdown.Rules.DemoRest.SpawnIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDRestSpawnIdentityTest::RunTest(const FString& Parameters)
{
    using EAdmission = LDSpawnPlacement::EAdmission;
    const FLDMatchContext Match{3, FGuid::NewGuid()};
    const FGuid Contender = FGuid::NewGuid();
    const auto Key = [&](uint32 Life, uint64 Sequence) { return FLDSpawnRequestKey{Match, Contender, Life, Sequence}; };
    bool bGood = TestTrue(TEXT("A first request is new"), LDSpawnPlacement::Admit(nullptr, Key(2, 4)) == EAdmission::New);
    // The PK entry selected its PK anchor for life 2 under sequence 4 and was blocked (released, not finalized).
    LDSpawnPlacement::FPriorRequest PK;
    PK.Request = Key(2, 4);
    bGood &= TestTrue(TEXT("The same blocked request replays its PK anchor (a retry of the same entry)"),
        LDSpawnPlacement::Admit(&PK, Key(2, 4)) == EAdmission::Replay);
    bGood &= TestTrue(TEXT("Other terms under the same identity conflict"), LDSpawnPlacement::Admit(&PK, Key(3, 4)) == EAdmission::Conflict);
    bGood &= TestTrue(TEXT("R02: a still-pending PK request cannot be redrawn at the rest anchor under any identity"),
        LDSpawnPlacement::Admit(&PK, Key(2, 5)) == EAdmission::Refused);
    // Withdrawn when the joint PK entry failed: the switch to the rest anchor takes a newer identity for the same life.
    PK.bWithdrawn = true;
    bGood &= TestTrue(TEXT("A withdrawn request never replays"), LDSpawnPlacement::Admit(&PK, Key(2, 4)) == EAdmission::Refused);
    bGood &= TestTrue(TEXT("The same life stands elsewhere under a newer sequence"), LDSpawnPlacement::Admit(&PK, Key(2, 5)) == EAdmission::New);
    bGood &= TestTrue(TEXT("No older life or sequence comes back"), LDSpawnPlacement::Admit(&PK, Key(1, 6)) == EAdmission::Refused
        && LDSpawnPlacement::Admit(&PK, Key(2, 3)) == EAdmission::Refused);
    PK.bHeld = true;
    bGood &= TestTrue(TEXT("A held reservation must be released first"), LDSpawnPlacement::Admit(&PK, Key(2, 5)) == EAdmission::Refused);
    LDSpawnPlacement::FPriorRequest Done;
    Done.Request = Key(2, 5);
    Done.bFinalized = true;
    bGood &= TestTrue(TEXT("After a committed life only the next life is new"),
        LDSpawnPlacement::Admit(&Done, Key(3, 6)) == EAdmission::New && LDSpawnPlacement::Admit(&Done, Key(2, 6)) == EAdmission::Refused);
    return bGood;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDPKRingOutsideTimeTest, "Lockdown.Rules.DemoPK.RingOutsideTime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDPKRingOutsideTimeTest::RunTest(const FString& Parameters)
{
    using namespace LDPhaseEntryTesting;
    using FInterval = TPair<int64, int64>;
    const FLDPKParameters P = RingParameters();
    const FVector Centre(0, 0, 0);
    // The fight started at authority 100000 ms; record instants are authority seconds.
    const int64 Fight = 100000;
    const auto Seconds = [Fight](double ElapsedMs) { return (double(Fight) + ElapsedMs) / 1000.; };
    const auto Run = [&](const FLDNativeMotionBatch& Batch, int64 From, int64 To, TArray<FInterval>& Out)
    {
        TArray<LDPKRules::FFootSegment> Path;
        return LDPKRules::FootPathFromMotion(Batch, Fight, From, To, Path) && LDPKRules::OutsideRingIntervals(P, Centre, Path, Out);
    };
    TArray<FInterval> Out;
    // Walking out across the 1800 cm edge in the middle of a 50 ms step: only the half after the crossing counts (the end
    // foot alone would charge all 50 ms).
    FLDNativeMotionBatch Out1;
    Out1.StartFootCm = FVector(1790, 0, 0);
    Out1.EndFootCm = FVector(1810, 0, 0);
    Out1.Records = {Move(1, FVector(1790, 0, 0), FVector(1810, 0, 0), Seconds(1000), Seconds(1050))};
    bool bGood = TestTrue(TEXT("Leaving mid-step counts from the crossing"), Run(Out1, 1000, 1050, Out)
        && Out.Num() == 1 && Out[0] == FInterval(1025, 1050));
    // Walking back in mid-step: the part before the crossing counts (the end foot alone would charge nothing).
    FLDNativeMotionBatch In1;
    In1.StartFootCm = FVector(1830, 0, 0);
    In1.EndFootCm = FVector(1790, 0, 0);
    In1.Records = {Move(1, FVector(1830, 0, 0), FVector(1790, 0, 0), Seconds(2000), Seconds(2040))};
    bGood &= TestTrue(TEXT("Entering mid-step counts until the crossing"), Run(In1, 2000, 2050, Out)
        && Out.Num() == 1 && Out[0] == FInterval(2000, 2030));
    // Standing still while the ring shrinks over the foot: R = 1000 cm at 30000 ms.
    FLDNativeMotionBatch Still;
    Still.StartFootCm = Still.EndFootCm = FVector(0, 1000, 0);
    bGood &= TestTrue(TEXT("The shrinking edge passing a standing foot splits the step"), Run(Still, 29950, 30050, Out)
        && Out.Num() == 1 && Out[0] == FInterval(30000, 30050));
    // A crossing between whole ms falls into its event ms (rounded up); the boundary itself is inside.
    FLDNativeMotionBatch Edge;
    Edge.StartFootCm = FVector(1799.9, 0, 0);
    Edge.EndFootCm = FVector(1800.1, 0, 0);
    Edge.Records = {Move(1, FVector(1799.9, 0, 0), FVector(1800.1, 0, 0), Seconds(3000), Seconds(3001))};
    bGood &= TestTrue(TEXT("A crossing inside a ms counts from the next whole ms"), Run(Edge, 3000, 3010, Out)
        && Out.Num() == 1 && Out[0] == FInterval(3001, 3010));
    FLDNativeMotionBatch OnEdge;
    OnEdge.StartFootCm = OnEdge.EndFootCm = FVector(1800, 0, 0);
    bGood &= TestTrue(TEXT("Standing exactly on the edge is inside"), Run(OnEdge, 4000, 4050, Out) && Out.IsEmpty());
    // Damage of the split interval: 25 ms at 5 % of 1000 HP per second, in P2 x 1e7.
    int64 Accumulator = 0;
    TArray<FInterval> Left;
    Run(Out1, 1000, 1050, Left);
    for (const FInterval& Interval : Left) { LDPKRules::AccumulateRingDamage(P, 100000, Interval.Key, Interval.Value, Accumulator); }
    bGood &= TestEqual(TEXT("Only the real outside time burns (25 ms, not the whole 50 ms step)"), Accumulator, int64(100000) * 500 * 25);
    // A record that does not continue the foot is refused, never guessed.
    FLDNativeMotionBatch Broken = Out1;
    Broken.Records[0].FromFootCm = FVector(0, 0, 0);
    TArray<LDPKRules::FFootSegment> Path;
    bGood &= TestFalse(TEXT("A discontinuous motion record is refused"), LDPKRules::FootPathFromMotion(Broken, Fight, 1000, 1050, Path));
    return bGood;
}

#endif
