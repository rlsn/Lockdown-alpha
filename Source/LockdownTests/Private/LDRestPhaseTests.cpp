#include "Framework/LDMatchTimeline.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Combat/LDTacticalRules.h"
#include "Development/LDDemoContentCatalog.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/LDGameMode.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "LDTestApprovedFixture.h"

// #198 rest phases (approved outer parameters, issue #198 comment 6752601175 §1/§8/§9): the pure timeline reducer and the
// Combat side of the phase-boundary resource policy and the private rest space.
namespace
{
bool RestTimelineParameters(FAutomationTestBase& Test, FLDTimelineParameters& Out)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    FString Error;
    if (!LDTestApprovedFixture::Read(Test, Catalog)) { return false; }
    return Test.TestTrue(TEXT("The approved content yields the timeline: ") + Error,
        FLDTimelineParameters::FromContent(Catalog->Get().Parameters, Out, Error));
}

const FLDTimelineEvent* FindEvent(const TArray<FLDTimelineEvent>& Events, ELDTimelineEventKind Kind)
{
    return Events.FindByPredicate([Kind](const FLDTimelineEvent& Event) { return Event.Kind == Kind; });
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDRestTimelineTest, "Lockdown.Rules.DemoRest.Timeline",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDRestTimelineTest::RunTest(const FString& Parameters)
{
    FLDTimelineParameters P;
    if (!RestTimelineParameters(*this, P)) { return false; }
    bool bGood = TestTrue(TEXT("The approved durations (s): field 160, R0 build 45, rest build 40, return 5"),
        P.FieldSegmentMs == 160000 && P.OpeningBuildMs == 45000 && P.BuildMs == 40000 && P.ReturnMs == 5000);
    FString Error;
    {
        FLDMatchTimeline Skip;
        TArray<FLDTimelineEvent> SkipEvents;
        bGood &= TestTrue(TEXT("DEV preparation fixture begins"), Skip.Begin(P, 0, Error));
        bGood &= TestTrue(TEXT("Skip locks the build and requests a real field return immediately"),
            Skip.DevFinishPreparation(1000, SkipEvents, Error) && Skip.GetStep() == ELDRestStep::Return
            && Skip.IsEntryDue(1000) && FindEvent(SkipEvents, ELDTimelineEventKind::LockBuild)
            && !FindEvent(SkipEvents, ELDTimelineEventKind::EnterField));
        SkipEvents.Reset();
        bGood &= TestTrue(TEXT("An unready skipped entry waits; T stays frozen"),
            Skip.Advance(1000, false, false, SkipEvents, Error) && SkipEvents.IsEmpty() && Skip.IsRest());
        bGood &= TestTrue(TEXT("The ready entry starts T at its actual commit"),
            Skip.Advance(1050, false, true, SkipEvents, Error) && Skip.IsFieldRunning()
            && Skip.FieldMsAt(1050) == 0 && FindEvent(SkipEvents, ELDTimelineEventKind::EnterField));
        SkipEvents.Reset();
        bGood &= TestFalse(TEXT("A repeated skip cannot advance a running field"), Skip.DevFinishPreparation(1051, SkipEvents, Error));
        bGood &= TestTrue(TEXT("Rejected skip leaves the stage and T unchanged"), Skip.IsFieldRunning() && Skip.FieldMsAt(1051) == 1);
    }
    {
        FLDTimelineParameters Bad = P;
        Bad.ReturnMs = 0;
        bGood &= TestFalse(TEXT("A zero step is refused"), Bad.Validate(Error));
        Bad = P;
        Bad.FieldSegmentMs = 0;
        bGood &= TestFalse(TEXT("A zero field segment is refused"), Bad.Validate(Error));
    }

    // Every instant below follows from the approved durations checked above.
    const int64 Seg = P.FieldSegmentMs;
    FLDMatchTimeline Line;
    const int64 Start = 1000;
    TArray<FLDTimelineEvent> Events;
    if (!TestTrue(TEXT("The match begins in R0 build"), Line.Begin(P, Start, Error))) { AddError(Error); return false; }
    bGood &= TestTrue(TEXT("R0: rest 0, build step, T frozen at 0, build open"),
        Line.IsRest() && Line.GetRestIndex() == 0 && Line.GetStep() == ELDRestStep::Build && Line.FieldMsAt(Start + 20000) == 0
        && Line.IsRestBuildOpen() && !Line.IsFieldRunning() && Line.NextDueMs() == Start + P.OpeningBuildMs);
    bGood &= TestTrue(TEXT("Without the lock permission the due build lock waits"),
        Line.Advance(Start + P.OpeningBuildMs, false, true, Events, Error) && Events.IsEmpty() && Line.IsBuildLockDue(Start + P.OpeningBuildMs));
    bGood &= TestTrue(TEXT("The build lock goes straight to the return countdown"),
        Line.Advance(Start + P.OpeningBuildMs, true, true, Events, Error)
        && FindEvent(Events, ELDTimelineEventKind::LockBuild) && Line.GetStep() == ELDRestStep::Return && !Line.IsRestBuildOpen()
        && Line.NextDueMs() == Start + P.OpeningBuildMs + P.ReturnMs);
    Events.Reset();
    const int64 Field1 = Start + P.OpeningBuildMs + P.ReturnMs;
    bGood &= TestTrue(TEXT("The return countdown ends in field 1 at T=0"), Line.Advance(Field1, true, true, Events, Error)
        && FindEvent(Events, ELDTimelineEventKind::EnterField) && Line.GetStage() == ELDMatchStage::Field
        && Line.GetFieldSegment() == 0 && Line.FieldMsAt(Field1) == 0 && Line.IsFieldRunning());
    bGood &= TestTrue(TEXT("T runs with the authority clock in the field"), Line.FieldMsAt(Field1 + 12345) == 12345);
    bGood &= TestTrue(TEXT("Field timers are converted to authority ms inside the segment only"),
        Line.FieldDueAuthorityMs(Field1, 45000) == Field1 + 45000 && Line.FieldDueAuthorityMs(Field1, Seg + 1) == MAX_int64
        && Line.FieldDueAuthorityMs(Field1 + 50000, 45000) == MAX_int64);
    bGood &= TestTrue(TEXT("Field 1 ends after one segment"), Line.NextDueMs() == Field1 + Seg);
    Events.Reset();
    const int64 Rest1 = Field1 + Seg;
    bGood &= TestTrue(TEXT("The segment end enters R1 with T frozen"), Line.Advance(Rest1, false, true, Events, Error)
        && FindEvent(Events, ELDTimelineEventKind::EnterRest) && Line.GetRestIndex() == 1 && Line.FieldMsAt(Rest1 + 99999) == Seg
        && Line.NextDueMs() == Rest1 + P.BuildMs && Line.FieldDueAuthorityMs(Rest1, Seg + 20000) == MAX_int64);
    bGood &= TestTrue(TEXT("A whole mid rest lasts its build and return"),
        Line.Advance(Rest1 + P.BuildMs, true, true, Events, Error) && Line.GetStep() == ELDRestStep::Return
        && Line.NextDueMs() == Rest1 + P.BuildMs + P.ReturnMs);
    while (Line.GetStage() == ELDMatchStage::Rest) { Line.Advance(Line.NextDueMs(), true, true, Events, Error); }
    bGood &= TestTrue(TEXT("Field 2 resumes at the T where segment 1 ended"), Line.GetFieldSegment() == 1
        && Line.FieldMsAt(Rest1 + P.BuildMs + P.ReturnMs) == Seg);
    while (Line.GetStage() != ELDMatchStage::Field || Line.GetFieldSegment() != 2) { Line.Advance(Line.NextDueMs(), true, true, Events, Error); }
    const int64 Field3 = Line.GetStepEndsAtMs() - P.FieldSegmentMs;
    bGood &= TestTrue(TEXT("Field 3 runs from the end of segment 2"), Line.FieldMsAt(Field3) == 2 * Seg);
    Line.Advance(Line.NextDueMs(), false, true, Events, Error);
    bGood &= TestTrue(TEXT("R3 at the end of segment 3"), Line.GetRestIndex() == 3 && Line.FieldMsAt(Line.NextDueMs()) == 3 * Seg);
    Line.Advance(Line.NextDueMs(), true, true, Events, Error);
    bGood &= TestTrue(TEXT("The final build deadline returns everyone to the showdown"), Line.GetStep() == ELDRestStep::Return);
    Events.Reset();
    const int64 Final = Line.NextDueMs();
    bGood &= TestTrue(TEXT("R3 returns to final field; the showdown owner, not the growth timeline, schedules its end"),
        Line.Advance(Final, true, true, Events, Error) && Line.GetStage() == ELDMatchStage::FinalField
        && FindEvent(Events, ELDTimelineEventKind::EnterField)->FieldSegment == 3 && Line.NextDueMs() == MAX_int64
        && Line.FieldMsAt(Final + 60000) == 3 * Seg + 60000);
    const FLDTimelineView View = Line.MakeView(3, 7);
    bGood &= TestTrue(TEXT("Clients derive the same T from the public view"),
        LDMatchTimeline::FieldNowFromView(View, Final + 60000) == 3 * Seg + 60000 && View.FieldEndT == -1);
    // Start to the flagpole: R0 (45 + 5 s), three rests (40 + 5 s each) and three 160 s segments, 665 s.
    bGood &= TestEqual(TEXT("The flagpole opens 665 s after the start"), Final - Start, int64(665000));
    return bGood;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDRestReadyEarlyTest, "Lockdown.Rules.DemoRest.ReadyEarly",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDRestReadyEarlyTest::RunTest(const FString& Parameters)
{
    FLDTimelineParameters P;
    if (!RestTimelineParameters(*this, P)) { return false; }
    FLDMatchTimeline Line;
    FString Error;
    bool bChanged = false;
    bool bGood = TestTrue(TEXT("Begin the approved opening rest"), Line.Begin(P, 1000, Error));
    TArray<FLDTimelineEvent> Events;
    while (Line.GetRestIndex() != 1 || !Line.IsRest()) { Line.Advance(Line.NextDueMs(), true, true, Events, Error); }
    const int64 Ready = Line.GetStepEndsAtMs() - P.BuildMs + 4000;
    bGood &= TestTrue(TEXT("R1 opens on its build window with T frozen at segment 1's end"),
        Line.GetStep() == ELDRestStep::Build && Line.FieldMsAt(Ready) == P.FieldSegmentMs);
    bGood &= TestTrue(TEXT("All ready ends the build step at once"),
        Line.FinishBuildEarly(Ready, bChanged, Error) && bChanged && Line.GetStepEndsAtMs() == Ready && Line.IsBuildLockDue(Ready));
    bGood &= TestFalse(TEXT("A repeated ready at the ended step changes nothing"), Line.FinishBuildEarly(Ready, bChanged, Error));
    Events.Reset();
    bGood &= TestTrue(TEXT("The lock then runs the whole return countdown"),
        Line.Advance(Ready, true, true, Events, Error) && Line.GetStep() == ELDRestStep::Return
        && Line.GetStepEndsAtMs() == Ready + P.ReturnMs);
    bGood &= TestFalse(TEXT("Ready cannot skip the return countdown"), Line.FinishBuildEarly(Ready + 1, bChanged, Error));
    bGood &= TestTrue(TEXT("A new match restores the full opening window"), Line.Begin(P, 20000, Error)
        && Line.GetStepEndsAtMs() == 20000 + P.OpeningBuildMs);
    return bGood;
}

// The Combat side of the phase boundary (approved §8) and of the shared rest space: a frozen world body takes nothing and
// regenerates nothing, rest groups never touch each other or the field, the phase refill readies every track (a forgotten
// active's too) and the phase cleanup ends every control, mark, shield and in-flight effect without inventing a hit.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDRestPhaseTest, "Lockdown.Rules.DemoRest.PhasePolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDRestPhaseTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDTestApprovedFixture::Read(*this, Catalog)) { return false; }
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    if (!TestNotNull(TEXT("An isolated rest world"), World)) { return false; }
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Mode = World->SpawnActor<ALDGameMode>();
    auto* Public = World->SpawnActor<ALDGameState>();
    auto* Driver = World->SpawnActor<ALDCombatDirector>();
    ALDCharacter* OwnerA = World->SpawnActor<ALDCharacter>();
    ALDCharacter* DummyA = World->SpawnActor<ALDCharacter>();
    ALDCharacter* OwnerB = World->SpawnActor<ALDCharacter>();
    ALDCharacter* Monster = World->SpawnActor<ALDCharacter>();
    if (!Mode || !Public || !Driver || !OwnerA || !DummyA || !OwnerB || !Monster) { AddError(TEXT("Fixture actors failed to spawn")); return false; }
    Mode->bDemoWorld = Mode->bEntryAccepted = Mode->bAuthorityInitialized = true;
    Mode->GameState = Public;
    World->CopyGameState(Mode, Public);
    Mode->MatchContext = {9, FGuid::NewGuid()};
    Mode->MatchPhase = ELDMatchPhase::Rest;
    Mode->Combat = Driver;
    Mode->DemoContent = Catalog;
    Mode->AuthorityStep = {Mode->MatchContext, 1, 0, 0};
    const FGuid A(0x198C, 1, 0, 1), Dummy(0x198C, 1, 0, 2), B(0x198C, 1, 0, 3), Camp(0x198C, 1, 0, 4);
    const auto State = [](ELDVictimKind Kind, const FGuid& Id, const FGuid& Group)
    {
        FLDBodyCombatState S;
        S.Identity = FGuid::NewGuid();
        S.Victim = {Kind, Id, 1};
        S.Resources.HpP2 = 50000;
        S.Resources.MaxHpP2 = 100000;
        S.Resources.Revision = 1;
        S.IsolationGroup = Group;
        return S;
    };
    Driver->Bodies.Add(OwnerA, State(ELDVictimKind::Contender, A, ALDGameMode::RestIsolationGroup()));
    Driver->Bodies.Add(DummyA, State(ELDVictimKind::Contender, Dummy, ALDGameMode::RestIsolationGroup()));
    Driver->Bodies.FindChecked(DummyA).bTrainingDummy = true;
    Driver->Bodies.Add(OwnerB, State(ELDVictimKind::Contender, B, ALDGameMode::RestIsolationGroup()));
    Driver->Bodies.Add(Monster, State(ELDVictimKind::CampMember, Camp, FGuid()));
    FLDBodyCombatState& SA = Driver->Bodies.FindChecked(OwnerA);
    FLDBodyCombatState& SD = Driver->Bodies.FindChecked(DummyA);
    FLDBodyCombatState& SB = Driver->Bodies.FindChecked(OwnerB);
    FLDBodyCombatState& SM = Driver->Bodies.FindChecked(Monster);
    {
        SA.IsolationGroup = SB.IsolationGroup = FGuid();
        TestFalse(TEXT("Field contenders can normally attack each other"), Driver->IsIsolated(B, SA));
        Mode->DevPeaceful.Add(A);
        TestTrue(TEXT("Peaceful mode blocks hostile contender and monster targeting"),
            Driver->IsIsolated(B, SA) && Driver->IsIsolated(Camp, SA));
        for (const FGuid& Source : {B, Camp})
        {
            FLDCombatHit Hit;
            Hit.HitId = FGuid::NewGuid();
            Hit.SourceEntity = Source;
            Hit.Target = {SA.Victim, SA.Identity};
            const int64 Hp = SA.Resources.HpP2;
            FString HitError;
            TestTrue(TEXT("An incoming hit is discarded without changing resources"),
                Driver->ApplyHit(Hit, HitError) && SA.Resources.HpP2 == Hp);
        }
        TestFalse(TEXT("Peaceful mode preserves visibility"), Driver->IsHiddenFrom(B, {SA.Victim, SA.Identity}));
        FLDControlApplication Stun;
        Stun.Kind = ELDControlKind::Stun;
        Stun.SourceEntity = B;
        Stun.DurationMs = 1000;
        Stun.EffectId = FGuid::NewGuid();
        bool Applied = true;
        FString Error;
        TestTrue(TEXT("Peaceful mode blocks direct hostile control"),
            Driver->CommitControl(SA, Stun, 0, Applied, Error) && !Applied && SA.Stuns.IsEmpty());
        TestFalse(TEXT("Peaceful mode leaves other contenders attackable"), Driver->IsIsolated(A, SB));
        Mode->DevPeaceful.Reset();
        TestFalse(TEXT("Turning peaceful mode off restores hostile targeting"), Driver->IsIsolated(B, SA));
        SA.IsolationGroup = SB.IsolationGroup = ALDGameMode::RestIsolationGroup();
    }
    bool bGood = TestTrue(TEXT("Rest isolation admits the training-dummy target, not other rest players"),
        !Driver->IsIsolated(A, SD) && !Driver->IsIsolated(B, SD));
    bGood &= TestTrue(TEXT("Rest players are visible, but cannot damage or control one another"),
        Driver->IsIsolated(A, SB) && Driver->IsIsolated(B, SA)
        && !Driver->IsHiddenFrom(A, {SB.Victim, SB.Identity}) && !Driver->IsHiddenFrom(B, {SA.Victim, SA.Identity}));
    {
        FLDControlApplication Control;
        Control.Kind = ELDControlKind::Stun;
        Control.SourceEntity = A;
        Control.DurationMs = 1000;
        Control.EffectId = FGuid::NewGuid();
        bool bApplied = true;
        FString Error;
        bGood &= TestTrue(TEXT("Direct control paths also reject a rest player without advancing control state"),
            Driver->CommitControl(SB, Control, 0, bApplied, Error) && !bApplied && SB.Stuns.IsEmpty());
    }
    bGood &= TestTrue(TEXT("The field is out of reach of a rest group and back"), Driver->IsIsolated(A, SM) && Driver->IsIsolated(Camp, SA));
    SM.bSuspended = true;
    SA.IsolationGroup = SD.IsolationGroup = FGuid();
    bGood &= TestTrue(TEXT("A frozen world body is out of reach even from the field, and reaches nobody"),
        Driver->IsIsolated(A, SM) && Driver->IsIsolated(Camp, SA));
    // Frozen bodies consume their interval without recovery.
    {
        LDTactical::FOutOfCombatRecovery Recovery;
        Recovery.DelayMs = 1;
        Recovery.MaxHPBpPerSec = 10000;
        FString Error;
        const int64 HpBefore = SM.Resources.HpP2;
        bGood &= TestTrue(TEXT("Regeneration settles"), Driver->RegenerateBodies(Recovery, 5000, Error));
        bGood &= TestTrue(TEXT("A frozen body keeps its HP and its regeneration clock moves on"),
            SM.Resources.HpP2 == HpBefore && SM.RegenThroughMs == 5000);
    }
    // Phase cleanup and refill under a RespawnsAndWorld permit.
    FLDCombatEntityRecord& Record = Driver->Records.Add(A);
    Record.Kind = ELDVictimKind::Contender;
    Record.Entity = A;
    FLDCombatTrack& Bound = Record.Tracks.AddDefaulted_GetRef();
    Bound.IssuedCooldownId = FGuid::NewGuid();
    Bound.DefinitionId = TEXT("active_elbow");
    Bound.ReadyAtMs = 90000;
    FLDCombatTrack& Forgotten = Record.Tracks.AddDefaulted_GetRef();
    Forgotten.IssuedCooldownId = FGuid::NewGuid();
    Forgotten.DefinitionId = TEXT("active_seethe");
    Forgotten.ReadyAtMs = 120000;
    Record.NextAttackAtMs = 70000;
    SA.Roots.AddDefaulted();
    SA.Roots[0].ExpiresAtMs = 99999;
    SA.Shield.Emplace();
    SA.Resources.ShieldP2 = 3000;
    SA.Burn.Layers.AddDefaulted();
    Driver->PendingHits.AddDefaulted();
    Driver->Projectiles.AddDefaulted();
    for (int32 Tick = 0; Tick < 100; ++Tick) { World->Tick(LEVELTICK_TimeOnly, .1f); }
    const int64 HpBeforePhase = SA.Resources.HpP2;
    FString Error;
    int32 Cancelled = INDEX_NONE;
    bool bReady = false;
    int64 HpAfterCleanup = -1;
    bool bReset = false;
    // A's second body becomes its personal training dummy: a fatal hit refills it instead of locking a death.
    SD.bTrainingDummy = true;
    SD.Resources.MaxHpP2 = 100000;
    SD.Resources.HpP2 = 0;
    {
        FLDCombatHit Fatal;
        Fatal.SourceEntity = A;
        bGood &= TestTrue(TEXT("A training dummy never dies: a fatal commit refills it"),
            Driver->LockDeath(*DummyA, SD, Fatal, Error) && !SD.bPendingDeath && SD.Resources.HpP2 == SD.Resources.MaxHpP2
            && Driver->PendingDeaths.IsEmpty());
    }
    SD.Resources.HpP2 = 1234;
    SD.Stuns.AddDefaulted();
    SD.Stuns[0].ExpiresAtMs = 99999;
    const bool bRan = Mode->BeginAuthorityStep(*Driver, Mode->MatchContext, 6000) == ELDCommitReject::None
        && Mode->ExecuteAuthorityPhase(*Driver, ELDCommitPhase::Motion, [](const auto&) { return true; }, Error)
        && Mode->ExecuteAuthorityPhase(*Driver, ELDCommitPhase::RespawnsAndWorld, [&](const FLDGameplayWritePermit& Permit)
        {
            Cancelled = Driver->CancelAllForPhase(Permit);
            bReady = Driver->ReadyAllForPhase(A, Permit, Error);
            HpAfterCleanup = SA.Resources.HpP2;
            // The owner's training reset refills both training bodies and clears their statuses.
            SD.Stuns.AddDefaulted();
            SD.Stuns[0].ExpiresAtMs = 99999;
            bReset = Driver->ResetTraining(A, Dummy, Permit, Error);
            return Cancelled != INDEX_NONE && bReady && bReset;
        }, Error)
        && Mode->FinishAuthorityStep(*Driver, Error);
    if (!TestTrue(TEXT("The phase boundary step runs: ") + Error, bRan)) { return false; }
    bGood &= TestEqual(TEXT("Both in-flight effects are cancelled by the phase"), Cancelled, 2);
    bGood &= TestTrue(TEXT("Nothing in flight survives and no hit was invented"), Driver->PendingHits.IsEmpty() && Driver->Projectiles.IsEmpty());
    bGood &= TestTrue(TEXT("Controls, shields and burns end on every body"),
        SA.Roots.IsEmpty() && !SA.Shield.IsSet() && SA.Resources.ShieldP2 == 0 && SA.Burn.Layers.IsEmpty());
    bGood &= TestTrue(TEXT("The phase leaves HP alone (new lives start full by construction)"), HpAfterCleanup == HpBeforePhase);
    bGood &= TestTrue(TEXT("A training reset refills the owner and its dummy and clears the dummy's statuses"),
        SA.Resources.HpP2 == SA.Resources.MaxHpP2 && SD.Resources.HpP2 == SD.Resources.MaxHpP2 && SD.Stuns.IsEmpty());
    bGood &= TestTrue(TEXT("Every cooldown, the forgotten active's held track included, and the basic cadence are ready"),
        Record.Tracks[0].ReadyAtMs == 6000 && Record.Tracks[1].ReadyAtMs == 6000 && Record.NextAttackAtMs == 6000
        && Record.Tracks[1].IssuedCooldownId == Forgotten.IssuedCooldownId);
    return bGood;
}

#endif
