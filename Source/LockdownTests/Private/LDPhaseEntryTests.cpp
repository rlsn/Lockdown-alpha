#include "Framework/LDMatchTimeline.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Development/LDDemoContentCatalog.h"
#include "Misc/AutomationTest.h"
#include "LDTestApprovedFixture.h"
#include "World/LDSpawnPlacement.h"

// #198 R14 (issue #198 review findings; approved outer parameters, issue #198 comment 6752601175 §1/§8): a held body entry
// and the spawn request identity of a switched or retried placement.
namespace LDPhaseEntryTesting
{
const FLDTimelineEvent* FindEvent(const TArray<FLDTimelineEvent>& Events, ELDTimelineEventKind Kind)
{
    return Events.FindByPredicate([Kind](const FLDTimelineEvent& Event) { return Event.Kind == Kind; });
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
    // R0 runs its build and return into field segment 0, which runs into R1's return countdown.
    while (Line.GetStage() != ELDMatchStage::Rest || Line.GetRestIndex() != 1 || Line.GetStep() != ELDRestStep::Return)
    {
        Line.Advance(Line.NextDueMs(), true, true, Events, Error);
    }
    const int64 FieldDue = Line.NextDueMs();
    // A partially unready return: nobody stands, T stays frozen at segment 1's end and segment 2 (and in R3 the flagpole)
    // does not start.
    Events.Reset();
    bGood &= TestTrue(TEXT("An unready field return holds: no field entry, T frozen"), Line.Advance(FieldDue, true, false, Events, Error)
        && !FindEvent(Events, ELDTimelineEventKind::EnterField) && Line.IsRest() && !Line.IsFieldRunning()
        && Line.FieldMsAt(FieldDue + 2000) == P.FieldSegmentMs && Line.IsEntryHeld());
    bGood &= TestTrue(TEXT("The held return enters the field when every body stands; T starts there"),
        Line.Advance(FieldDue + 2000, true, true, Events, Error) && FindEvent(Events, ELDTimelineEventKind::EnterField)
        && FindEvent(Events, ELDTimelineEventKind::EnterField)->AtMs == FieldDue + 2000 && Line.IsFieldRunning()
        && Line.FieldMsAt(FieldDue + 3000) == P.FieldSegmentMs + 1000 && Line.NextDueMs() == FieldDue + 2000 + P.FieldSegmentMs);
    // An unheld entry keeps its exact scheduled instant.
    while (Line.GetStage() != ELDMatchStage::Rest || Line.GetStep() != ELDRestStep::Return) { Line.Advance(Line.NextDueMs(), true, true, Events, Error); }
    const int64 Field2Due = Line.NextDueMs();
    Events.Reset();
    bGood &= TestTrue(TEXT("A ready return enters at its scheduled boundary"), Line.Advance(Field2Due, true, true, Events, Error)
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
    // A return entry selected its anchor for life 2 under sequence 4 and was blocked (released, not finalized).
    LDSpawnPlacement::FPriorRequest Prior;
    Prior.Request = Key(2, 4);
    bGood &= TestTrue(TEXT("The same blocked request replays its anchor (a retry of the same entry)"),
        LDSpawnPlacement::Admit(&Prior, Key(2, 4)) == EAdmission::Replay);
    bGood &= TestTrue(TEXT("Other terms under the same identity conflict"), LDSpawnPlacement::Admit(&Prior, Key(3, 4)) == EAdmission::Conflict);
    bGood &= TestTrue(TEXT("A still-pending request cannot be redrawn at another anchor under any identity"),
        LDSpawnPlacement::Admit(&Prior, Key(2, 5)) == EAdmission::Refused);
    // Withdrawn when the joint entry failed: the switch to another anchor takes a newer identity for the same life.
    Prior.bWithdrawn = true;
    bGood &= TestTrue(TEXT("A withdrawn request never replays"), LDSpawnPlacement::Admit(&Prior, Key(2, 4)) == EAdmission::Refused);
    bGood &= TestTrue(TEXT("The same life stands elsewhere under a newer sequence"), LDSpawnPlacement::Admit(&Prior, Key(2, 5)) == EAdmission::New);
    bGood &= TestTrue(TEXT("No older life or sequence comes back"), LDSpawnPlacement::Admit(&Prior, Key(1, 6)) == EAdmission::Refused
        && LDSpawnPlacement::Admit(&Prior, Key(2, 3)) == EAdmission::Refused);
    Prior.bHeld = true;
    bGood &= TestTrue(TEXT("A held reservation must be released first"), LDSpawnPlacement::Admit(&Prior, Key(2, 5)) == EAdmission::Refused);
    LDSpawnPlacement::FPriorRequest Done;
    Done.Request = Key(2, 5);
    Done.bFinalized = true;
    bGood &= TestTrue(TEXT("After a committed life only the next life is new"),
        LDSpawnPlacement::Admit(&Done, Key(3, 6)) == EAdmission::New && LDSpawnPlacement::Admit(&Done, Key(2, 6)) == EAdmission::Refused);
    return bGood;
}

#endif
