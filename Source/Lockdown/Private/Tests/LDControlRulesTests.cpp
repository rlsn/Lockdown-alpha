#include "Combat/LDControlRules.h"
#include "Core/LDLog.h"
#include "Combat/LDAttributeProjection.h"
#include "Combat/LDCombatTypes.h"
#include "AI/LDOpponentRequests.h"
#include "Navigation/LDOrderRules.h"
#include "Vision/LDVisionDirector.h"
#include "Vision/LDViewTypes.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDemoContentIds.h"
#include "Misc/AutomationTest.h"
#include "Combat/LDCombatDirector.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDCaptureReducer.h"
#include "Character/LDCharacter.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

// Taunt rules (CONTENT_AND_BALANCE 15.5, issue #83).
namespace LDControlRulesTests
{
bool ReadApprovedFixture(FAutomationTestBase& Test, TSharedPtr<const FLDDemoContentSnapshot>& Out)
{
    FString Root, Error, Text;
    if (!FParse::Value(FCommandLine::Get(), TEXT("LDDemoContentFixtures="), Root) || Root.IsEmpty()
        || !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Root),
            FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), TEXT("Artifacts"))))
    {
        Test.AddError(TEXT("Control rule tests require isolated LDDemoContentFixtures with approved_base; no live-data fallback."));
        return false;
    }
    TSharedPtr<FJsonObject> Index;
    if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(Root, TEXT("index.json")))
        || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Index) || !Index)
    {
        Test.AddError(TEXT("Unreadable content fixture index."));
        return false;
    }
    FString Format;
    bool bTestOnly = false;
    const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
    if (!Index->TryGetStringField(TEXT("format"), Format) || Format != TEXT("demo-native-fixtures-1")
        || !Index->TryGetBoolField(TEXT("test_only"), bTestOnly) || !bTestOnly || !Index->TryGetArrayField(TEXT("valid"), Rows))
    {
        Test.AddError(TEXT("Invalid content fixture index."));
        return false;
    }
    bool bFound = false;
    FLDDemoContentDigest Digest;
    for (const auto& Value : *Rows)
    {
        const TSharedPtr<FJsonObject>* Row = nullptr;
        FString Name, Hex;
        if (!Value->TryGetObject(Row) || !(*Row)->TryGetStringField(TEXT("name"), Name))
        {
            Test.AddError(TEXT("Invalid fixture case."));
            return false;
        }
        if (Name != TEXT("approved_base")) { continue; }
        if (bFound || !(*Row)->TryGetStringField(TEXT("catalog_sha256"), Hex) || !FLDDemoContentDigest::ParseHex(Hex, Digest, Error))
        {
            Test.AddError(TEXT("Duplicate or invalid approved_base fixture."));
            return false;
        }
        bFound = true;
    }
    if (!bFound)
    {
        Test.AddError(TEXT("Missing approved_base fixture."));
        return false;
    }
    FLDDemoContentBundle Bundle;
    TUniquePtr<FLDDemoPreparedContent> Prepared;
    auto* Asset = NewObject<ULDDemoContentCatalog>(GetTransientPackage());
    if (!FLDDemoContentBundle::ReadImportFiles(FPaths::Combine(Root, TEXT("approved_base")), Bundle, Error)
        || !ULDDemoContentCatalog::PrepareImport(*Asset, Bundle, Digest, Prepared, Error)
        || !Asset->CommitImport(Prepared, Digest, Error) || !Asset->Read(Digest, Out, Error))
    {
        Test.AddError(Error);
        return false;
    }
    return true;
}

FLDCombatBodyRef Contender(uint32 Id, uint32 Life = 1)
{ return {{ELDVictimKind::Contender, FGuid(Id, 0, 0, 0), Life}, FGuid(Id, Life, 0, 0)}; }

FLDCombatBodyRef CampMember(uint32 Slot)
{ return {{ELDVictimKind::CampMember, FGuid(9, 0, 0, Slot), 1}, FGuid(9, 1, 0, Slot)}; }

// Rule-test taunt terms, the advanced (rank 2) taunt of active_stare_down: 300 cm for 1 s.
constexpr double TauntRadiusCm = 300;
constexpr int64 TauntMs = 1000;

FLDCombatTaunt TauntBy(const FLDCombatBodyRef& Source, int64 AtMs, int64 DurationMs = TauntMs)
{
    FLDCombatTaunt Taunt;
    Taunt.EffectId = FGuid::NewGuid();
    Taunt.SourceEntity = Source.Victim.EntityId;
    Taunt.SourceBody = Source;
    Taunt.ExpiresAtMs = AtMs + DurationMs;
    return Taunt;
}

FLDTauntCandidate Candidate(const FLDCombatBodyRef& Body, const FVector& Foot)
{
    FLDTauntCandidate Out;
    Out.Body = Body;
    Out.FootCm = Foot;
    Out.bSeesCaster = true;
    return Out;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDControlTauntForcedTargetTest, "Lockdown.Rules.DemoControl.TauntForcedTarget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDControlTauntForcedTargetTest::RunTest(const FString& Parameters)
{
    using namespace LDControlRulesTests;
    using EForced = LDOrderRules::EForcedOrder;
    const FLDCombatBodyRef Caster = Contender(1), Victim = Contender(2), Other = Contender(3), Camp = CampMember(1);
    constexpr int64 Now = 10000;

    // Selection at release: only other living, unprotected contenders inside the radius that see the caster.
    TArray<FLDTauntCandidate> Candidates;
    Candidates.Add(Candidate(Other, FVector(TauntRadiusCm, 0, 0)));              // boundary: inclusive
    Candidates.Add(Candidate(Victim, FVector(100, 50, 0)));
    Candidates.Add(Candidate(Contender(4), FVector(TauntRadiusCm + 0.5, 0, 0))); // outside
    Candidates.Add(Candidate(Camp, FVector(50, 0, 0)));                          // camp members are not taunted
    Candidates.Add(Candidate(Caster, FVector::ZeroVector));                      // never the caster
    FLDTauntCandidate Hidden = Candidate(Contender(5), FVector(80, 0, 0));
    Hidden.bSeesCaster = false;                                                  // blocked: cannot be taunted
    Candidates.Add(Hidden);
    FLDTauntCandidate Protected = Candidate(Contender(6), FVector(80, 0, 0));
    Protected.bProtected = true;
    Candidates.Add(Protected);
    FLDTauntCandidate Dying = Candidate(Contender(7), FVector(80, 0, 0));
    Dying.bPendingDeath = true;
    Candidates.Add(Dying);
    const TArray<int32> Selected = LDControl::SelectTauntTargets(Caster.Victim.EntityId, FVector(0, 0, 50), TauntRadiusCm, Candidates);
    TestTrue(TEXT("Exactly the eligible contenders inside the inclusive planar radius are taunted, in entity order"),
        Selected.Num() == 2 && Candidates[Selected[0]].Body == Victim && Candidates[Selected[1]].Body == Other);
    TestTrue(TEXT("A nonpositive radius taunts nobody"),
        LDControl::SelectTauntTargets(Caster.Victim.EntityId, FVector::ZeroVector, 0, Candidates).IsEmpty());

    // Forced target: a taunted body may only basic-attack the taunter's exact body.
    TOptional<FLDCombatTaunt> Taunt;
    TestTrue(TEXT("Untaunted: every action and target is allowed"),
        LDControl::TauntAllows(Taunt, Now, true, Other) && LDControl::TauntAllows(Taunt, Now, false, FLDCombatBodyRef()));
    TestTrue(TEXT("The taunt applies"), LDControl::ApplyTaunt(Taunt, TauntBy(Caster, Now), Now) && LDControl::IsTaunted(Taunt, Now));
    TestTrue(TEXT("A basic attack at the taunter is allowed"), LDControl::TauntAllows(Taunt, Now, true, Caster));
    TestFalse(TEXT("A basic attack at anyone else is refused"), LDControl::TauntAllows(Taunt, Now, true, Other));
    TestFalse(TEXT("A later life of the taunter is not the taunter"), LDControl::TauntAllows(Taunt, Now, true, Contender(1, 2)));
    TestFalse(TEXT("No ability, not even aimed at the taunter"), LDControl::TauntAllows(Taunt, Now, false, Caster)
        || LDControl::TauntAllows(Taunt, Now, false, FLDCombatBodyRef()));
    const FLDCombatTaunt First = Taunt.GetValue();
    TestFalse(TEXT("Re-applying the same taunt effect is not a new taunt"), LDControl::ApplyTaunt(Taunt, First, Now + 10));
    TestTrue(TEXT("A newer taunter takes over"), LDControl::ApplyTaunt(Taunt, TauntBy(Other, Now + 200), Now + 200)
        && LDControl::TauntAllows(Taunt, Now + 200, true, Other) && !LDControl::TauntAllows(Taunt, Now + 200, true, Caster));
    TestFalse(TEXT("An already expired taunt never applies"), LDControl::ApplyTaunt(Taunt, TauntBy(Caster, Now, 0), Now));

    // Navigation: the forced chase replaces the own order, follows a new taunter and ends with the taunt.
    TestTrue(TEXT("Taunt begins a forced chase over any own order"), LDOrderRules::ForcedOrderStep(true, false, false) == EForced::Begin);
    TestTrue(TEXT("The chase of the same taunter continues"), LDOrderRules::ForcedOrderStep(true, true, true) == EForced::Keep);
    TestTrue(TEXT("A new taunter restarts the chase"), LDOrderRules::ForcedOrderStep(true, true, false) == EForced::Begin);
    TestTrue(TEXT("Untaunted bodies keep their ordinary orders"), LDOrderRules::ForcedOrderStep(false, false, false) == EForced::None);

    // The opponent AI obeys the same rule: it holds body intents and an interrupted order teaches it nothing.
    FLDCombatOwnerView Own;
    Own.bHasBody = true;
    FLDCombatStatusView Status;
    Status.StatusId = uint8(ELDDemoStatusId::Taunt);
    Status.ExpiresAtMs = Now + TauntMs;
    Own.Statuses.Add(Status);
    TestTrue(TEXT("A taunted AI holds its body intents"), LDOpponentRequests::BodyIntentsHeld(Own, Now));
    TestFalse(TEXT("At the taunt's end the AI decides again"), LDOpponentRequests::BodyIntentsHeld(Own, Now + TauntMs));
    Own.bHasBody = false;
    TestFalse(TEXT("Without a body there is nothing to hold"), LDOpponentRequests::BodyIntentsHeld(Own, Now));
    FLDOrderReceipt Interrupted;
    Interrupted.Status = ELDOrderStatus::Cancelled;
    Interrupted.Failure = ELDOrderFailure::Taunted;
    TestTrue(TEXT("An order ended by a taunt is dropped, not a failed attempt"),
        LDOpponentRequests::SettleOrder(Interrupted) == ELDOpponentSettle::Dropped);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDControlTauntRestoreTest, "Lockdown.Rules.DemoControl.TauntRestore",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDControlTauntRestoreTest::RunTest(const FString& Parameters)
{
    using namespace LDControlRulesTests;
    const FLDCombatBodyRef Caster = Contender(1), Other = Contender(3);
    constexpr int64 Now = 20000;
    const FLDCombatTaunt Taunt = TauntBy(Caster, Now);
    TestTrue(TEXT("A taunt holds while its taunter lives and is observed"), LDControl::TauntHolds(Taunt, Now + TauntMs - 1, true, true));
    TestFalse(TEXT("It ends exactly at its expiry (half-open)"), LDControl::TauntHolds(Taunt, Now + TauntMs, true, true));
    TestFalse(TEXT("The taunter's death or leave ends it"), LDControl::TauntHolds(Taunt, Now, false, true));
    TestFalse(TEXT("Losing observation of the taunter (sight or range) ends it"), LDControl::TauntHolds(Taunt, Now, true, false));

    TOptional<FLDCombatTaunt> Held = Taunt;
    FLDStatusExpiry Expired = LDControl::ExpireControlStatuses(Held, Now + TauntMs - 1);
    TestTrue(TEXT("Before expiry nothing is removed"), Held.IsSet() && !Expired.bTauntChanged);
    Expired = LDControl::ExpireControlStatuses(Held, Now + TauntMs);
    TestTrue(TEXT("At expiry the taunt record is removed"), !Held.IsSet() && Expired.bTauntChanged);
    TestTrue(TEXT("Restored: every action and target is allowed again"),
        LDControl::TauntAllows(Held, Now + TauntMs, true, Other) && LDControl::TauntAllows(Held, Now + TauntMs, false, FLDCombatBodyRef()));
    TestFalse(TEXT("An expired record that was not yet swept no longer taunts"),
        LDControl::IsTaunted(TOptional<FLDCombatTaunt>(Taunt), Now + TauntMs));
    TestTrue(TEXT("Control returns: the forced chase ends and the replaced order is not resumed"),
        LDOrderRules::ForcedOrderStep(false, true, true) == LDOrderRules::EForcedOrder::End);
    // The forced-chase retry is stamped on the match-relative clock; a new match (clock back at 0) must not inherit it.
    LDOrderRules::FForcedRetry Retry;
    Retry.Hold(150., .25);
    TestFalse(TEXT("A chase that stopped by itself waits one query interval"), Retry.IsDue(150.1));
    TestTrue(TEXT("It is retried after the interval"), Retry.IsDue(150.25));
    Retry.Hold(150., .25);
    TestFalse(TEXT("Without a match reset the next match's early seconds would still wait"), Retry.IsDue(1.));
    Retry.ResetMatch();
    TestTrue(TEXT("After the match reset a taunt chases from the new match's first second"), Retry.IsDue(0.));
    return true;
}

// #112 §9 + §8: a real combat hit through ApplyHit -> CommitControl reaches the spray zone. Damage, slow and disarm leave the
// sprayer's progress alone; a stun (and #147 a silence) clears it at its instant, and the control alone (no damage) is its owner's
// assist contribution.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCaptureCombatInterruptTest, "Lockdown.Rules.DemoCapture.CombatHitInterrupt",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCaptureCombatInterruptTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDControlRulesTests::ReadApprovedFixture(*this, Catalog)) { return false; }
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    if (!TestNotNull(TEXT("Fixture world"), World)) { return false; }
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Mode = World->SpawnActor<ALDGameMode>();
    auto* Public = World->SpawnActor<ALDGameState>();
    auto* Driver = World->SpawnActor<ALDCombatDirector>();
    auto* RivalActor = World->SpawnActor<ALDCharacter>();
    if (!Mode || !Public || !Driver || !RivalActor) { AddError(TEXT("Fixture actors")); return false; }
    Mode->bDemoWorld = Mode->bEntryAccepted = Mode->bAuthorityInitialized = true;
    Mode->GameState = Public;
    World->CopyGameState(Mode, Public);
    Mode->MatchContext = {1, FGuid::NewGuid()};
    Mode->MatchPhase = ELDMatchPhase::Development;
    Mode->RosterRevision = 1;
    Mode->Combat = Driver;
    Mode->DemoContent = Catalog;
    Mode->AuthorityStep = {Mode->MatchContext, 1, 0, 0};
    const FGuid Attacker = FGuid::NewGuid(), Controller = FGuid::NewGuid(), Rival = FGuid::NewGuid();
    for (const FGuid& Id : {Attacker, Controller, Rival})
    {
        FLDCombatEntityRecord& Record = Driver->Records.Add(Id);
        Record.Kind = ELDVictimKind::Contender;
        Record.Entity = Id;
        Record.Build.Level = 1;
    }
    const FLDCombatBodyRef RivalBody{{ELDVictimKind::Contender, Rival, 1}, FGuid::NewGuid()};
    const FLDCombatBodyRef AttackerBody{{ELDVictimKind::Contender, Attacker, 1}, FGuid::NewGuid()};
    const FLDCombatBodyRef ControllerBody{{ELDVictimKind::Contender, Controller, 1}, FGuid::NewGuid()};
    {
        FLDBodyCombatState State;
        State.Identity = RivalBody.BodyIdentity;
        State.Victim = RivalBody.Victim;
        State.Resources.MaxHpP2 = State.Resources.HpP2 = 100000;
        Driver->Bodies.Add(RivalActor, MoveTemp(State));
    }
    FString Error;
    const auto Fact = [&](ELDCaptureEventKind Kind, const FLDCombatBodyRef& Body, double Seconds, ELDCommitPhase Phase, bool Inside)
    {
        FLDCaptureEvent Event;
        Event.Kind = Kind;
        Event.Body = Body;
        Event.Phase = Phase;
        Event.bInside = Inside;
        return FLDCaptureEventTime::FromSeconds(Seconds, Event.Time, Error) && Mode->CaptureReducer.Enqueue(Event, Error);
    };
    // Open at 0 with a 100 ms spray; the rival sprays alone from 1 ms, the others stay outside.
    if (!Mode->CaptureReducer.Begin(0, 100, 5000, Error)
        || !Fact(ELDCaptureEventKind::Joined, RivalBody, .001, ELDCommitPhase::RespawnsAndWorld, true)
        || !Fact(ELDCaptureEventKind::Joined, AttackerBody, .001, ELDCommitPhase::RespawnsAndWorld, false)
        || !Fact(ELDCaptureEventKind::Joined, ControllerBody, .001, ELDCommitPhase::RespawnsAndWorld, false))
    { AddError(Error); return false; }
    World->Tick(LEVELTICK_TimeOnly, .025f);
    Mode->CaptureCircle.CaptureId = TEXT("capture_fixture");
    Mode->CaptureCircle.RadiusCm = 300;
    Mode->CaptureCircle.Ground = NewObject<UBoxComponent>(GetTransientPackage());
    const auto Hit = [&](const FLDCombatBodyRef& From, int64 BaseP2)
    {
        FLDCombatHit Out;
        Out.HitId = Out.EffectId = FGuid::NewGuid();
        Out.Target = RivalBody;
        Out.SourceBody = From;
        Out.SourceEntity = Out.CreditOwner = From.Victim.EntityId;
        Out.HitClass = ELDCombatHitClass::Ability;
        Out.BaseP2 = BaseP2;
        return Out;
    };
    // One authority step to AtMs whose Effects phase applies Hits through the real combat path.
    const auto Step = [&](int64 AtMs, const TArray<FLDCombatHit>& Hits, TFunction<bool()> Extra = nullptr,
        TFunction<bool()> Motion = nullptr)
    {
        const auto Pass = [](const FLDGameplayWritePermit&) { return true; };
        const ELDCommitReject Begin = Mode->BeginAuthorityStep(*Driver, Mode->MatchContext, AtMs);
        if (Begin != ELDCommitReject::None)
        {
            Error = FString::Printf(TEXT("the step to %lld ms was refused (%u)"), AtMs, uint32(Begin));
            return false;
        }
        return Mode->ExecuteAuthorityPhase(*Driver, ELDCommitPhase::Motion,
                [&](const FLDGameplayWritePermit&) { return !Motion || Motion(); }, Error)
            && Mode->ExecuteAuthorityPhase(*Driver, ELDCommitPhase::Actions, Pass, Error)
            && Mode->ExecuteAuthorityPhase(*Driver, ELDCommitPhase::Effects, [&](const FLDGameplayWritePermit&)
            {
                for (const FLDCombatHit& Each : Hits) { if (!Driver->ApplyHit(Each, Error)) { return false; } }
                return !Extra || Extra();
            }, Error)
            && Mode->ExecuteAuthorityPhase(*Driver, ELDCommitPhase::Capture,
                [&](const FLDGameplayWritePermit& Permit) { return Mode->ExecuteCapturePhase(Permit, Error); }, Error)
            && Mode->ExecuteAuthorityPhase(*Driver, ELDCommitPhase::Close,
                [&](const FLDGameplayWritePermit& Permit) { return Mode->ExecuteClosePhase(Permit, Error); }, Error)
            && Mode->FinishAuthorityStep(*Driver, Error);
    };
    const FLDBodyCombatState& Sprayer = Driver->Bodies.FindChecked(RivalActor);

    FLDCombatHit Damage = Hit(AttackerBody, 1000);
    FLDCombatHit Slow = Hit(AttackerBody, 0);
    Slow.SlowBp = 3000;
    Slow.SlowDurationMs = 1000;
    FLDCombatHit Disarm = Hit(AttackerBody, 0);
    Disarm.DisarmDurationMs = 500;
    if (!TestTrue(FString::Printf(TEXT("Damage, slow and disarm step: %s"), *Error), Step(10, {Damage, Slow, Disarm}))) { return false; }
    TestTrue(TEXT("Ordinary damage, a slow and a disarm leave the spray running"),
        Sprayer.Resources.HpP2 < 100000 && Mode->CaptureReducer.GetState() == ELDCircleState::Solo
        && Mode->CaptureReducer.GetOccupier() == Rival && Mode->CaptureReducer.GetSoloSinceMs() == 1
        && Mode->CaptureReducer.GetDueAtMs() == 101);
    TestTrue(TEXT("The damage is the attacker's assist contribution"), Sprayer.AssistContributions.ContainsByPredicate(
        [&](const FLDAssistContribution& Each) { return Each.Contributor == Attacker && Each.LastAtMs == 10; }));

    FLDCombatHit Stun = Hit(ControllerBody, 0);
    Stun.StunDurationMs = 300;
    if (!TestTrue(FString::Printf(TEXT("Stun step: %s"), *Error), Step(20, {Stun}))) { return false; }
    TestTrue(TEXT("A stun from a real hit clears the spray at its instant; still alone, the rival restarts from zero"),
        Mode->CaptureReducer.GetState() == ELDCircleState::Solo && Mode->CaptureReducer.GetOccupier() == Rival
        && Mode->CaptureReducer.GetSoloSinceMs() == 20 && Mode->CaptureReducer.GetDueAtMs() == 120);
    TestTrue(TEXT("The damage-free stun is its owner's assist contribution, recorded once by the control path"),
        Sprayer.AssistContributions.FilterByPredicate(
            [&](const FLDAssistContribution& Each) { return Each.Contributor == Controller; }).Num() == 1);
    const FLDCaptureDevTally::FHolder* Holder = Mode->CaptureReducer.DevTally().Holders.Find(Rival);

    TestTrue(TEXT("The tally counts exactly one interruption"), Holder && Holder->Interrupted == 1);

    // #147 D10: a silence clears the spray at its instant like the other hard controls.
    FLDCombatHit Silence = Hit(ControllerBody, 0);
    Silence.SilenceDurationMs = 300;
    if (!TestTrue(FString::Printf(TEXT("Silence step: %s"), *Error), Step(25, {Silence}))) { return false; }
    TestTrue(TEXT("A silence from a real hit clears the spray at its instant"),
        Mode->CaptureReducer.GetState() == ELDCircleState::Solo && Mode->CaptureReducer.GetSoloSinceMs() == 25
        && Mode->CaptureReducer.GetDueAtMs() == 125 && Sprayer.Silences.Num() == 1);
    Holder = Mode->CaptureReducer.DevTally().Holders.Find(Rival);
    TestTrue(TEXT("The tally counts the silence"), Holder && Holder->Interrupted == 2);

    // A knockback commits through the same control path (its displacement needs the Demo map's movement; a push out of the
    // zone then reaches the reducer as an ordinary presence change, i.e. leaving the zone).
    World->Tick(LEVELTICK_TimeOnly, .025f);
    bool bPushed = false;
    const auto Push = [&]()
    {
        FLDControlApplication App;
        App.Kind = ELDControlKind::Knockback;
        App.EffectId = FGuid::NewGuid();
        App.SourceEntity = App.CreditOwner = Attacker;
        App.SourceBody = AttackerBody;
        App.DurationMs = 200;
        return Driver->CommitControl(Driver->Bodies.FindChecked(RivalActor), App, Mode->GetAuthorityNowMs(), bPushed, Error);
    };
    if (!TestTrue(FString::Printf(TEXT("Knockback step: %s"), *Error), Step(40, {}, Push))) { return false; }
    TestTrue(TEXT("A knockback clears the spray at its instant and never diminishes"), bPushed
        && Mode->CaptureReducer.GetState() == ELDCircleState::Solo && Mode->CaptureReducer.GetSoloSinceMs() == 40
        && Mode->CaptureReducer.GetDueAtMs() == 140 && Sprayer.Knockbacks.Num() == 1 && Sprayer.Knockbacks[0].ExpiresAtMs == 240);
    Holder = Mode->CaptureReducer.DevTally().Holders.Find(Rival);
    TestTrue(TEXT("The tally counts the third interruption"), Holder && Holder->Interrupted == 3);
    // Pushed out of the zone: the leave clears the progress like any other exit.
    const auto Leave = [&]() { return Fact(ELDCaptureEventKind::Presence, RivalBody, .045, ELDCommitPhase::Motion, false); };
    if (!TestTrue(FString::Printf(TEXT("Leave step: %s"), *Error), Step(50, {}, nullptr, Leave))) { return false; }
    TestTrue(TEXT("Leaving the zone empties the flagpole and nothing of the spray is kept"),
        Mode->CaptureReducer.GetState() == ELDCircleState::Empty && !Mode->CaptureReducer.GetOccupier().IsValid());
    return true;
}
// #134 review: the Host's LD_HIT line records the vamp the source actually regained, after the lifesteal cap and the max-HP clamp.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDPlaytestLogHitVampTest, "Lockdown.Rules.PlaytestLog.HitVamp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDPlaytestLogHitVampTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!LDControlRulesTests::ReadApprovedFixture(*this, Catalog)) { return false; }
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    if (!TestNotNull(TEXT("Fixture world"), World)) { return false; }
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Mode = World->SpawnActor<ALDGameMode>();
    auto* Driver = World->SpawnActor<ALDCombatDirector>();
    auto* SourceActor = World->SpawnActor<ALDCharacter>();
    auto* VictimActor = World->SpawnActor<ALDCharacter>();
    if (!Mode || !Driver || !SourceActor || !VictimActor) { AddError(TEXT("Fixture actors")); return false; }
    Mode->Combat = Driver;
    Mode->DemoContent = Catalog;
    Mode->AuthorityNowMs = 1000;
    World->CopyGameState(Mode, nullptr);
    const FLDCombatBodyRef SourceRef{{ELDVictimKind::Contender, FGuid::NewGuid(), 1}, FGuid::NewGuid()};
    const FLDCombatBodyRef VictimRef{{ELDVictimKind::Contender, FGuid::NewGuid(), 1}, FGuid::NewGuid()};
    for (const auto& Pair : {TPair<ALDCharacter*, FLDCombatBodyRef>(SourceActor, SourceRef), TPair<ALDCharacter*, FLDCombatBodyRef>(VictimActor, VictimRef)})
    {
        FLDBodyCombatState State;
        State.Identity = Pair.Value.BodyIdentity;
        State.Victim = Pair.Value.Victim;
        State.Resources.MaxHpP2 = State.Resources.HpP2 = 100000;
        Driver->Bodies.Add(Pair.Key, MoveTemp(State));
    }
    FLDBodyCombatState& Source = Driver->Bodies.FindChecked(SourceActor);
    const FLDBodyCombatState& Victim = Driver->Bodies.FindChecked(VictimActor);

    // The LD_HIT lines of this test, read back from the log.
    struct FHitLines final : FOutputDevice
    {
        FCriticalSection Lock;
        TArray<FString> Lines;
        virtual void Serialize(const TCHAR* Text, ELogVerbosity::Type, const FName& Category) override
        {
            if (Category == LogLDCombat.GetCategoryName() && FCString::Strstr(Text, TEXT("LD_HIT ")))
            {
                FScopeLock Guard(&Lock);
                Lines.Add(Text);
            }
        }
        virtual bool CanBeUsedOnAnyThread() const override { return true; }
        virtual bool CanBeUsedOnMultipleThreads() const override { return true; }
    } Capture;
    const ELogVerbosity::Type Verbosity = LogLDCombat.GetVerbosity();
    LogLDCombat.SetVerbosity(ELogVerbosity::Log);
    GLog->AddOutputDevice(&Capture);
    ON_SCOPE_EXIT { GLog->RemoveOutputDevice(&Capture); LogLDCombat.SetVerbosity(Verbosity); };
    const auto Field = [](const FString& Line, const TCHAR* Key)
    {
        const FString Token = FString::Printf(TEXT(" %s="), Key);
        const int32 At = Line.Find(Token, ESearchCase::CaseSensitive);
        if (At == INDEX_NONE) { return FString(); }
        const FString Rest = Line.Mid(At + Token.Len());
        int32 End = INDEX_NONE;
        return Rest.FindChar(TEXT(' '), End) ? Rest.Left(End) : Rest;
    };
    // A 300-point hit with a full on-hit lifesteal; #147 D22 caps every lifesteal of one hit at COMBAT.LifestealCap.
    const auto Strike = [&](int64& OutHPLost, int64& OutHealed, FString& OutVamp)
    {
        FLDCombatHit Hit;
        Hit.HitId = Hit.EffectId = FGuid::NewGuid();
        Hit.Target = VictimRef;
        Hit.SourceBody = SourceRef;
        Hit.SourceEntity = Hit.CreditOwner = SourceRef.Victim.EntityId;
        Hit.HitClass = ELDCombatHitClass::Ability;
        Hit.BaseP2 = 30000;
        Hit.OnHitScaleBp = 10000;
        Hit.bLifesteal = true;
        Hit.LifestealRatioBp = 10000;
        const int64 VictimBefore = Victim.Resources.HpP2, SourceBefore = Source.Resources.HpP2;
        const int32 Before = Capture.Lines.Num();
        FString Error;
        if (!TestTrue(FString::Printf(TEXT("The hit applies: %s"), *Error), Driver->ApplyHit(Hit, Error))) { return false; }
        GLog->Flush();
        FScopeLock Guard(&Capture.Lock);
        if (!TestEqual(TEXT("One LD_HIT line per hit"), Capture.Lines.Num(), Before + 1)) { return false; }
        OutHPLost = VictimBefore - Victim.Resources.HpP2;
        OutHealed = Source.Resources.HpP2 - SourceBefore;
        OutVamp = Field(Capture.Lines.Last(), TEXT("vamp"));
        return true;
    };

    // Room to heal: the line records the capped lifesteal the source regained.
    Source.Resources.HpP2 = 50000;
    int64 HPLost = 0, Healed = 0;
    FString Vamp;
    if (!Strike(HPLost, Healed, Vamp)) { return false; }
    TestTrue(TEXT("The hit took HP for the source's lifesteal"), HPLost > 0);
    const int64 CapBp = Catalog->Get().Parameters.LifestealCapBp;
    TestEqual(TEXT("The source regains the capped lifesteal"), Healed, HPLost * CapBp / 10000);
    TestEqual(TEXT("vamp is the source's actual HP gain"), Vamp, LDLog::P2(Healed));

    // The source is near full HP: the line records the clamped gain.
    Source.Resources.HpP2 = 89900;
    if (!Strike(HPLost, Healed, Vamp)) { return false; }
    TestTrue(TEXT("This lifesteal would overfill the source"), HPLost * CapBp / 10000 > Source.Resources.MaxHpP2 - 89900);
    TestTrue(TEXT("The source stops at its max HP"), Source.Resources.HpP2 == Source.Resources.MaxHpP2 && Healed == 10100);
    TestEqual(TEXT("vamp is the source's actual HP gain at the max-HP clamp"), Vamp, LDLog::P2(Healed));
    return true;
}
#endif
