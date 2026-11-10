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
#include "Tests/LDTestApprovedFixture.h"

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
    bool bGood = TestTrue(TEXT("The approved durations (s): field 180, R0 45, rests 30, sign-up 10, 5/8/3/60/3/12/5"),
        P.FieldSegmentMs == 180000 && P.OpeningBuildMs == 45000 && P.BuildMs == 30000 && P.SignupMs == 10000
        && P.DrawMs == 5000 && P.BettingMs == 8000 && P.PKReadyMs == 3000 && P.PKMaxMs == 60000 && P.SettlementMs == 3000
        && P.PayoutMs == 12000 && P.ReturnMs == 5000);
    bGood &= TestTrue(TEXT("Field respawn waits by death T: 8/10/12/15 s"),
        P.RespawnMs[0] == 8000 && P.RespawnMs[1] == 10000 && P.RespawnMs[2] == 12000 && P.RespawnMs[3] == 15000);
    FString Error;
    {
        FLDTimelineParameters Bad = P;
        Bad.SignupMs = Bad.BuildMs + 1;
        bGood &= TestFalse(TEXT("A sign-up longer than a build window is refused"), Bad.Validate(Error));
        Bad = P;
        Bad.ReturnMs = 0;
        bGood &= TestFalse(TEXT("A zero step is refused"), Bad.Validate(Error));
    }

    FLDMatchTimeline Line;
    const int64 Start = 1000;
    TArray<FLDTimelineEvent> Events;
    if (!TestTrue(TEXT("The match begins in R0 build"), Line.Begin(P, Start, Error))) { AddError(Error); return false; }
    bGood &= TestTrue(TEXT("R0: rest 0, build step, T frozen at 0, build open"),
        Line.IsRest() && Line.GetRestIndex() == 0 && Line.GetStep() == ELDRestStep::Build && Line.FieldMsAt(Start + 20000) == 0
        && Line.IsRestBuildOpen() && !Line.IsFieldRunning() && Line.NextDueMs() == Start + 45000);
    bGood &= TestTrue(TEXT("Sign-up opens for the last 10 s of the build, its deadline included"),
        !Line.IsSignupOpen(Start + 34999) && Line.IsSignupOpen(Start + 35000) && Line.IsSignupOpen(Start + 45000));
    bGood &= TestTrue(TEXT("Without the lock permission the due build lock waits"),
        Line.Advance(Start + 45000, 0, false, Events, Error) && Events.IsEmpty() && Line.IsBuildLockDue(Start + 45000));
    // One entrant: the PK is skipped and the 5 s return follows the lock at once.
    bGood &= TestTrue(TEXT("Lock with one sign-up skips the PK"), Line.Advance(Start + 45000, 1, true, Events, Error)
        && FindEvent(Events, ELDTimelineEventKind::LockBuild) && FindEvent(Events, ELDTimelineEventKind::LockBuild)->Value == 1
        && Line.GetStep() == ELDRestStep::Return && Line.MakeView(1, 1).bPKSkipped && !Line.IsRestBuildOpen());
    Events.Reset();
    const int64 Field1 = Start + 50000;
    bGood &= TestTrue(TEXT("The return countdown ends in field 1 at T=0"), Line.Advance(Field1, 0, true, Events, Error)
        && FindEvent(Events, ELDTimelineEventKind::EnterField) && Line.GetStage() == ELDMatchStage::Field
        && Line.GetFieldSegment() == 0 && Line.FieldMsAt(Field1) == 0 && Line.IsFieldRunning());
    bGood &= TestTrue(TEXT("T runs with the authority clock in the field"), Line.FieldMsAt(Field1 + 12345) == 12345);
    bGood &= TestTrue(TEXT("Field timers are converted to authority ms inside the segment only"),
        Line.FieldDueAuthorityMs(Field1, 45000) == Field1 + 45000 && Line.FieldDueAuthorityMs(Field1, 180001) == MAX_int64
        && Line.FieldDueAuthorityMs(Field1 + 50000, 45000) == MAX_int64);
    bGood &= TestTrue(TEXT("Field 1 ends after 180 s"), Line.NextDueMs() == Field1 + 180000);
    Events.Reset();
    const int64 Rest1 = Field1 + 180000;
    bGood &= TestTrue(TEXT("T=180 s enters R1 with T frozen"), Line.Advance(Rest1, 0, false, Events, Error)
        && FindEvent(Events, ELDTimelineEventKind::EnterRest) && Line.GetRestIndex() == 1 && Line.FieldMsAt(Rest1 + 99999) == 180000
        && Line.NextDueMs() == Rest1 + 30000 && Line.FieldDueAuthorityMs(Rest1, 200000) == MAX_int64);
    // Two entrants run every PK step; a PK that runs to its limit makes the approved 126 s rest.
    Events.Reset();
    bGood &= TestTrue(TEXT("Two entrants: draw follows the lock"), Line.Advance(Rest1 + 30000, 2, true, Events, Error)
        && Line.GetStep() == ELDRestStep::Draw && Line.MakeView(1, 1).LockedEntrants == 2);
    const ELDRestStep Chain[] = {ELDRestStep::Betting, ELDRestStep::PKReady, ELDRestStep::PK, ELDRestStep::Settlement,
        ELDRestStep::Payout, ELDRestStep::Return};
    for (const ELDRestStep Next : Chain)
    {
        const int64 Due = Line.NextDueMs();
        bGood &= TestTrue(FString::Printf(TEXT("Step %s at %lld"), LDMatchTimeline::StepName(Next), Due),
            Line.Advance(Due, 2, true, Events, Error) && Line.GetStep() == Next);
        if (Next == ELDRestStep::Payout) { bGood &= TestTrue(TEXT("The payout reopens the build"), Line.IsRestBuildOpen()); }
    }
    bGood &= TestEqual(TEXT("A full R1 with a 60 s PK lasts 126 s"), Line.NextDueMs() - Rest1, int64(126000));
    Events.Reset();
    const int64 Field2 = Line.NextDueMs();
    bGood &= TestTrue(TEXT("R1 returns to field 2 at T=180 s"), Line.Advance(Field2, 0, true, Events, Error)
        && Line.GetFieldSegment() == 1 && Line.FieldMsAt(Field2 + 1) == 180001);
    // R2 with a PK that ends early: the PK step ends at the reported instant.
    const int64 Rest2 = Field2 + 180000;
    bGood &= TestTrue(TEXT("R2 begins at T=360 s"), Line.Advance(Rest2, 0, false, Events, Error) && Line.GetRestIndex() == 2);
    bGood &= TestTrue(TEXT("R2 locks with three entrants"), Line.Advance(Rest2 + 30000, 3, true, Events, Error));
    while (Line.GetStep() != ELDRestStep::PK) { Line.Advance(Line.NextDueMs(), 3, true, Events, Error); }
    const int64 PKStart = Line.GetStepEndsAtMs() - P.PKMaxMs;
    bGood &= TestFalse(TEXT("A PK cannot end after its limit"), Line.FinishPK(PKStart + P.PKMaxMs + 1, Error));
    bGood &= TestTrue(TEXT("A PK decided after 15 s ends then"), Line.FinishPK(PKStart + 15000, Error)
        && Line.NextDueMs() == PKStart + 15000 && Line.Advance(PKStart + 15000, 3, true, Events, Error)
        && Line.GetStep() == ELDRestStep::Settlement);
    bGood &= TestFalse(TEXT("Only a running PK ends early"), Line.FinishPK(PKStart + 16000, Error));
    while (Line.GetStage() == ELDMatchStage::Rest) { Line.Advance(Line.NextDueMs(), 3, true, Events, Error); }
    const int64 Field3 = Line.GetStepEndsAtMs() - P.FieldSegmentMs;
    bGood &= TestTrue(TEXT("Field 3 runs from T=360 s"), Line.GetFieldSegment() == 2 && Line.FieldMsAt(Field3) == 360000);
    Line.Advance(Line.NextDueMs(), 0, false, Events, Error);
    bGood &= TestTrue(TEXT("R3 at T=540 s"), Line.GetRestIndex() == 3 && Line.FieldMsAt(Line.NextDueMs()) == 540000);
    Line.Advance(Line.NextDueMs(), 0, true, Events, Error);
    Events.Reset();
    const int64 Final = Line.NextDueMs();
    bGood &= TestTrue(TEXT("R3 returns to the final field, which never ends on its own"),
        Line.Advance(Final, 0, true, Events, Error) && Line.GetStage() == ELDMatchStage::FinalField
        && FindEvent(Events, ELDTimelineEventKind::EnterField)->FieldSegment == 3 && Line.NextDueMs() == MAX_int64
        && Line.FieldMsAt(Final + 60000) == 600000);
    bGood &= TestTrue(TEXT("Respawn waits by death T, boundaries included"),
        Line.RespawnWaitMs(0) == 8000 && Line.RespawnWaitMs(179999) == 8000 && Line.RespawnWaitMs(180000) == 10000
        && Line.RespawnWaitMs(360000) == 12000 && Line.RespawnWaitMs(540000) == 15000 && Line.RespawnWaitMs(9999999) == 15000);
    const FLDTimelineView View = Line.MakeView(3, 7);
    bGood &= TestTrue(TEXT("Clients derive the same T from the public view"),
        LDMatchTimeline::FieldNowFromView(View, Final + 60000) == 600000 && View.FieldEndT == -1);
    return bGood;
}

// The Combat side of the phase boundary (approved §8) and of the private rest space: a frozen world body takes nothing and
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
    Driver->Bodies.Add(OwnerA, State(ELDVictimKind::Contender, A, A));
    // A second body of A's rest group.
    Driver->Bodies.Add(DummyA, State(ELDVictimKind::CampMember, Dummy, A));
    Driver->Bodies.Add(OwnerB, State(ELDVictimKind::Contender, B, B));
    Driver->Bodies.Add(Monster, State(ELDVictimKind::CampMember, Camp, FGuid()));
    FLDBodyCombatState& SA = Driver->Bodies.FindChecked(OwnerA);
    FLDBodyCombatState& SD = Driver->Bodies.FindChecked(DummyA);
    FLDBodyCombatState& SB = Driver->Bodies.FindChecked(OwnerB);
    FLDBodyCombatState& SM = Driver->Bodies.FindChecked(Monster);
    bool bGood = TestTrue(TEXT("Bodies of the same rest group reach each other"),
        !Driver->IsIsolated(A, SD) && !Driver->IsIsolated(Dummy, SA));
    bGood &= TestTrue(TEXT("Another contender's rest group is out of reach both ways"),
        Driver->IsIsolated(A, SB) && Driver->IsIsolated(B, SA) && Driver->IsIsolated(B, SD));
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
        Recovery.MaxMPBpPerSec = 10000;
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
    const bool bRan = Mode->BeginAuthorityStep(*Driver, Mode->MatchContext, 6000) == ELDCommitReject::None
        && Mode->ExecuteAuthorityPhase(*Driver, ELDCommitPhase::Motion, [](const auto&) { return true; }, Error)
        && Mode->ExecuteAuthorityPhase(*Driver, ELDCommitPhase::RespawnsAndWorld, [&](const FLDGameplayWritePermit& Permit)
        {
            Cancelled = Driver->CancelAllForPhase(Permit);
            bReady = Driver->ReadyAllForPhase(A, Permit, Error);
            return Cancelled != INDEX_NONE && bReady;
        }, Error)
        && Mode->FinishAuthorityStep(*Driver, Error);
    if (!TestTrue(TEXT("The phase boundary step runs: ") + Error, bRan)) { return false; }
    bGood &= TestEqual(TEXT("Both in-flight effects are cancelled by the phase"), Cancelled, 2);
    bGood &= TestTrue(TEXT("Nothing in flight survives and no hit was invented"), Driver->PendingHits.IsEmpty() && Driver->Projectiles.IsEmpty());
    bGood &= TestTrue(TEXT("Controls, shields and burns end on every body"),
        SA.Roots.IsEmpty() && !SA.Shield.IsSet() && SA.Resources.ShieldP2 == 0 && SA.Burn.Layers.IsEmpty());
    bGood &= TestTrue(TEXT("The phase leaves HP alone (new lives start full by construction)"), SA.Resources.HpP2 == HpBeforePhase);
    bGood &= TestTrue(TEXT("Every cooldown, the forgotten active's held track included, and the basic cadence are ready"),
        Record.Tracks[0].ReadyAtMs == 6000 && Record.Tracks[1].ReadyAtMs == 6000 && Record.NextAttackAtMs == 6000
        && Record.Tracks[1].IssuedCooldownId == Forgotten.IssuedCooldownId);
    return bGood;
}

#endif
