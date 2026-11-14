#include "Framework/LDMatchTimeline.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/LDOpponentPolicy.h"
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
    bool bGood = TestTrue(TEXT("The approved durations (s): field 180, R0 180, rests 120, sign-up 10, 5/8/3/60/3/12/5"),
        P.FieldSegmentMs == 180000 && P.OpeningBuildMs == 180000 && P.BuildMs == 120000 && P.SignupMs == 10000
        && P.DrawMs == 5000 && P.BettingMs == 8000 && P.PKReadyMs == 3000 && P.PKMaxMs == 60000 && P.SettlementMs == 3000
        && P.PayoutMs == 12000 && P.ReturnMs == 5000);
    bGood &= TestTrue(TEXT("Growth-field respawn waits by death T: 8/10/12 s"),
        P.RespawnMs[0] == 8000 && P.RespawnMs[1] == 10000 && P.RespawnMs[2] == 12000);
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
        && Line.IsRestBuildOpen() && !Line.IsFieldRunning() && Line.NextDueMs() == Start + P.OpeningBuildMs);
    bGood &= TestTrue(TEXT("Sign-up opens for the last 10 s of the build, its deadline included"),
        !Line.IsSignupOpen(Start + 169999) && Line.IsSignupOpen(Start + 170000) && Line.IsSignupOpen(Start + 180000));
    bGood &= TestEqual(TEXT("R0 publishes the sign-up opening"), Line.MakeView(1, 1).SignupOpensAtMs,
        Start + P.OpeningBuildMs - P.SignupMs);
    FLDOpponentPublicMatch Public;
    Public.Phase = ELDMatchPhase::Rest;
    Public.Timeline = Line.MakeView(1, 1);
    Public.NowMs = Public.Timeline.SignupOpensAtMs;
    const FGuid Opponent(1, 2, 3, 4);
    bGood &= TestTrue(TEXT("The AI signs up using the R0 public timeline"), LDOpponentPolicy::WantsPKSignUp(Public, Opponent));
    bGood &= TestTrue(TEXT("Without the lock permission the due build lock waits"),
        Line.Advance(Start + P.OpeningBuildMs, 0, false, true, Events, Error) && Events.IsEmpty() && Line.IsBuildLockDue(Start + P.OpeningBuildMs));
    // One entrant: the PK is skipped and the 5 s return follows the lock at once.
    bGood &= TestTrue(TEXT("Lock with one sign-up skips the PK"), Line.Advance(Start + P.OpeningBuildMs, 1, true, true, Events, Error)
        && FindEvent(Events, ELDTimelineEventKind::LockBuild) && FindEvent(Events, ELDTimelineEventKind::LockBuild)->Value == 1
        && Line.GetStep() == ELDRestStep::Return && Line.MakeView(1, 1).bPKSkipped && !Line.IsRestBuildOpen());
    Events.Reset();
    const int64 Field1 = Start + P.OpeningBuildMs + P.ReturnMs;
    bGood &= TestTrue(TEXT("The return countdown ends in field 1 at T=0"), Line.Advance(Field1, 0, true, true, Events, Error)
        && FindEvent(Events, ELDTimelineEventKind::EnterField) && Line.GetStage() == ELDMatchStage::Field
        && Line.GetFieldSegment() == 0 && Line.FieldMsAt(Field1) == 0 && Line.IsFieldRunning());
    bGood &= TestTrue(TEXT("T runs with the authority clock in the field"), Line.FieldMsAt(Field1 + 12345) == 12345);
    bGood &= TestTrue(TEXT("Field timers are converted to authority ms inside the segment only"),
        Line.FieldDueAuthorityMs(Field1, 45000) == Field1 + 45000 && Line.FieldDueAuthorityMs(Field1, 180001) == MAX_int64
        && Line.FieldDueAuthorityMs(Field1 + 50000, 45000) == MAX_int64);
    bGood &= TestTrue(TEXT("Field 1 ends after 180 s"), Line.NextDueMs() == Field1 + 180000);
    Events.Reset();
    const int64 Rest1 = Field1 + 180000;
    bGood &= TestTrue(TEXT("T=180 s enters R1 with T frozen"), Line.Advance(Rest1, 0, false, true, Events, Error)
        && FindEvent(Events, ELDTimelineEventKind::EnterRest) && Line.GetRestIndex() == 1 && Line.FieldMsAt(Rest1 + 99999) == 180000
        && Line.NextDueMs() == Rest1 + P.BuildMs && Line.FieldDueAuthorityMs(Rest1, 200000) == MAX_int64);
    bGood &= TestEqual(TEXT("R1 publishes the sign-up opening"), Line.MakeView(1, 1).SignupOpensAtMs,
        Rest1 + P.BuildMs - P.SignupMs);
    // Two entrants run every PK step; a PK that runs to its limit makes the approved 126 s rest.
    Events.Reset();
    bGood &= TestTrue(TEXT("Two entrants: draw follows the lock"), Line.Advance(Rest1 + P.BuildMs, 2, true, true, Events, Error)
        && Line.GetStep() == ELDRestStep::Draw && Line.MakeView(1, 1).LockedEntrants == 2);
    const ELDRestStep Chain[] = {ELDRestStep::Betting, ELDRestStep::PKReady, ELDRestStep::PK, ELDRestStep::Settlement,
        ELDRestStep::Payout, ELDRestStep::Return};
    for (const ELDRestStep Next : Chain)
    {
        const int64 Due = Line.NextDueMs();
        bGood &= TestTrue(FString::Printf(TEXT("Step %s at %lld"), LDMatchTimeline::StepName(Next), Due),
            Line.Advance(Due, 2, true, true, Events, Error) && Line.GetStep() == Next);
        if (Next == ELDRestStep::Payout) { bGood &= TestTrue(TEXT("The payout reopens the build"), Line.IsRestBuildOpen()); }
    }
    bGood &= TestEqual(TEXT("A full R1 with a 60 s PK lasts 216 s"), Line.NextDueMs() - Rest1, int64(216000));
    Events.Reset();
    const int64 Field2 = Line.NextDueMs();
    bGood &= TestTrue(TEXT("R1 returns to field 2 at T=180 s"), Line.Advance(Field2, 0, true, true, Events, Error)
        && Line.GetFieldSegment() == 1 && Line.FieldMsAt(Field2 + 1) == 180001);
    // R2 with a PK that ends early: the PK step ends at the reported instant.
    const int64 Rest2 = Field2 + 180000;
    bGood &= TestTrue(TEXT("R2 begins at T=360 s"), Line.Advance(Rest2, 0, false, true, Events, Error) && Line.GetRestIndex() == 2);
    bGood &= TestEqual(TEXT("R2 publishes the sign-up opening"), Line.MakeView(1, 1).SignupOpensAtMs,
        Rest2 + P.BuildMs - P.SignupMs);
    bGood &= TestTrue(TEXT("R2 locks with three entrants"), Line.Advance(Rest2 + P.BuildMs, 3, true, true, Events, Error));
    while (Line.GetStep() != ELDRestStep::PK) { Line.Advance(Line.NextDueMs(), 3, true, true, Events, Error); }
    const int64 PKStart = Line.GetStepEndsAtMs() - P.PKMaxMs;
    bGood &= TestFalse(TEXT("A PK cannot end after its limit"), Line.FinishPK(PKStart + P.PKMaxMs + 1, Error));
    bGood &= TestTrue(TEXT("A PK decided after 15 s ends then"), Line.FinishPK(PKStart + 15000, Error)
        && Line.NextDueMs() == PKStart + 15000 && Line.Advance(PKStart + 15000, 3, true, true, Events, Error)
        && Line.GetStep() == ELDRestStep::Settlement);
    bGood &= TestFalse(TEXT("Only a running PK ends early"), Line.FinishPK(PKStart + 16000, Error));
    while (Line.GetStage() == ELDMatchStage::Rest) { Line.Advance(Line.NextDueMs(), 3, true, true, Events, Error); }
    const int64 Field3 = Line.GetStepEndsAtMs() - P.FieldSegmentMs;
    bGood &= TestTrue(TEXT("Field 3 runs from T=360 s"), Line.GetFieldSegment() == 2 && Line.FieldMsAt(Field3) == 360000);
    Line.Advance(Line.NextDueMs(), 0, false, true, Events, Error);
    bGood &= TestTrue(TEXT("R3 at T=540 s"), Line.GetRestIndex() == 3 && Line.FieldMsAt(Line.NextDueMs()) == 540000);
    bGood &= TestFalse(TEXT("The final rest has no PK signup"), Line.IsSignupOpen(Line.NextDueMs()));
    bGood &= TestEqual(TEXT("The final rest publishes no PK signup window"), Line.MakeView(1, 1).SignupOpensAtMs, int64(0));
    Public.Timeline = Line.MakeView(1, 1);
    Public.NowMs = Line.NextDueMs() - 1;
    bGood &= TestFalse(TEXT("The AI does not sign up using the final rest public timeline"),
        LDOpponentPolicy::WantsPKSignUp(Public, Opponent));
    Line.Advance(Line.NextDueMs(), 6, true, true, Events, Error);
    bGood &= TestTrue(TEXT("The final build deadline returns all six directly to showdown without another PK"),
        Line.GetStep() == ELDRestStep::Return && Line.MakeView(1,1).LockedEntrants == 0);
    Events.Reset();
    const int64 Final = Line.NextDueMs();
    bGood &= TestTrue(TEXT("R3 returns to final field; the showdown owner, not the growth timeline, schedules its end"),
        Line.Advance(Final, 0, true, true, Events, Error) && Line.GetStage() == ELDMatchStage::FinalField
        && FindEvent(Events, ELDTimelineEventKind::EnterField)->FieldSegment == 3 && Line.NextDueMs() == MAX_int64
        && Line.FieldMsAt(Final + 60000) == 600000);
    bGood &= TestTrue(TEXT("Respawn waits by death T, boundaries included"),
        Line.RespawnWaitMs(0) == 8000 && Line.RespawnWaitMs(179999) == 8000 && Line.RespawnWaitMs(180000) == 10000
        && Line.RespawnWaitMs(360000) == 12000 && Line.RespawnWaitMs(540000) == 0 && Line.RespawnWaitMs(9999999) == 0);
    const FLDTimelineView View = Line.MakeView(3, 7);
    bGood &= TestTrue(TEXT("Clients derive the same T from the public view"),
        LDMatchTimeline::FieldNowFromView(View, Final + 60000) == 600000 && View.FieldEndT == -1);
    return bGood;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDRestReadySignupTest, "Lockdown.Rules.DemoRest.ReadySignup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDRestReadySignupTest::RunTest(const FString& Parameters)
{
    FLDTimelineParameters P;
    if (!RestTimelineParameters(*this, P)) { return false; }
    FLDMatchTimeline Line;
    FString Error;
    bool bChanged = false;
    bool bGood = TestTrue(TEXT("Begin the approved opening rest"), Line.Begin(P, 1000, Error));
    bGood &= TestTrue(TEXT("All ready preserves a full ten seconds of signup"),
        Line.FinishBuildEarly(5000, bChanged, Error) && bChanged && Line.GetStepEndsAtMs() == 15000
        && Line.IsSignupOpen(5000) && Line.IsSignupOpen(15000) && Line.FieldMsAt(14999) == 0);
    bGood &= TestTrue(TEXT("A repeated ready cannot restart or extend signup"),
        Line.FinishBuildEarly(7000, bChanged, Error) && !bChanged && Line.GetStepEndsAtMs() == 15000);
    bGood &= TestTrue(TEXT("Ready in the last millisecond keeps the original deadline"),
        Line.FinishBuildEarly(14999, bChanged, Error) && !bChanged && Line.GetStepEndsAtMs() == 15000);
    bGood &= TestFalse(TEXT("Ready at the expired deadline cannot change the window"),
        Line.FinishBuildEarly(15000, bChanged, Error));
    TArray<FLDTimelineEvent> Events;
    bGood &= TestTrue(TEXT("Standing signup still locks into a two-player PK"),
        Line.Advance(15000, 2, true, true, Events, Error) && Line.GetStep() == ELDRestStep::Draw
        && Line.MakeView(1, 1).LockedEntrants == 2);
    bGood &= TestFalse(TEXT("Ready cannot skip a locked step"), Line.FinishBuildEarly(15001, bChanged, Error));
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
        FLDControlApplication Throw;
        Throw.Kind = ELDControlKind::Slow;
        Throw.EffectId = FGuid::NewGuid();
        Throw.DurationMs = 1000;
        Throw.RatioBp = 3000;
        Throw.bSpectatorThrow = true;
        bGood &= TestTrue(TEXT("A spectator throw never slows a rest player"),
            Driver->CommitControl(SB, Throw, 0, bApplied, Error) && !bApplied);
        FLDBodyCombatState Arena = State(ELDVictimKind::Contender, FGuid(0x198C, 1, 0, 5), FGuid(0x198C, 2, 0, 1));
        Arena.PKSide = 1;
        FLDControlApplication Unowned = Throw;
        Unowned.bSpectatorThrow = false;
        bGood &= TestTrue(TEXT("An unowned control without the throw flag stays isolated from an arena body"),
            Driver->CommitControl(Arena, Unowned, 0, bApplied, Error) && !bApplied);
        bGood &= TestTrue(TEXT("A spectator throw slows a PK arena body"),
            Driver->CommitControl(Arena, Throw, 0, bApplied, Error) && bApplied);
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
