#include "AI/LDOpponentRequests.h"
#include "Combat/LDCombatDirector.h"
#include "Development/LDGrowthProtocol.h"
#include "Framework/LDCaptureTypes.h"
#include "Framework/LDGameInstance.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDMatchProviders.h"
#include "Framework/LDPlayerController.h"
#include "Navigation/LDOrderRules.h"
#include "Navigation/LDOrderTypes.h"
#include "UI/LDHUDModel.h"
#include "UI/LDTerms.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
// #153: the opening countdown between the initial spawn and the match's T=0.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoOpeningCountdownTest, "Lockdown.Rules.DemoOpeningCountdown",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDemoOpeningCountdownTest::RunTest(const FString& Parameters)
{
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    if (!TestNotNull(TEXT("Isolated countdown fixture world"), World)) { return false; }
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Instance = NewObject<ULDGameInstance>(GEngine);
    World->SetGameInstance(Instance);
    auto* Mode = World->SpawnActor<ALDGameMode>();
    auto* Public = World->SpawnActor<ALDGameState>();
    auto* Host = World->SpawnActor<ALDPlayerController>();
    auto* Guest = World->SpawnActor<ALDPlayerController>();
    auto* Driver = World->SpawnActor<ALDCombatDirector>();
    if (!Mode || !Public || !Host || !Guest || !Driver) { AddError(TEXT("Fixture actors failed to spawn")); return false; }
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
    if (!TestTrue(TEXT("The Host and its guest join the room"), Mode->AdmitRoomMember(Host, Error) && Mode->AdmitRoomMember(Guest, Error)))
    { AddError(Error); return false; }
    FLDMatchProviderHooks Progression;
    Progression.ReadFinalLevel = [](const ALDGameMode&, const FGuid&, uint8& Level, FString&) { Level = 1; return true; };
    LDMatchProviders::FScopedTestTable Providers({{ELDMatchProvider::Progression, Progression}});

    // A started match: the bodies stand from authority time 0 and T=0 is the countdown's end.
    constexpr int64 CountdownMs = 5000, DevelopmentMs = 180000;
    Mode->MatchPhase = ELDMatchPhase::Countdown;
    Mode->MatchStartedAtMs = CountdownMs;
    Mode->Combat = Driver;
    Mode->bAuthorityInitialized = true;
    Mode->AuthorityStep = {Mode->MatchContext, 1, 0, 0};
    Mode->AuthorityBoundary = {Mode->MatchContext, 1, 0};
    // The native clock already has the whole countdown available; the fixture does not tick the world for seconds.
    Mode->AuthorityOriginWorldSeconds = World->GetTimeSeconds() - 10.;
    Mode->LastWorldClockSample = World->GetTimeSeconds();
    if (!TestTrue(TEXT("Development counts from T=0"), Mode->CaptureReducer.Begin(CountdownMs + DevelopmentMs, 3500, 20000, Error)))
    { AddError(Error); return false; }

    TestTrue(TEXT("The countdown keeps the write gate open for the clock, the opening choice and leaves"),
        Mode->IsGameplayWriteOpen(Mode->MatchContext));
    TestTrue(TEXT("The countdown is reported to the action, order and trade gates"), Mode->IsOpeningCountdown(Mode->MatchContext));
    TestFalse(TEXT("Another ledger match is never in this countdown"), Mode->IsOpeningCountdown({Generation, FGuid::NewGuid()}));
    TestEqual(TEXT("T=0 is an exact authority boundary"), Mode->NextCountdownDueMs(), CountdownMs);

    FString PublicError;
    FLDPublicMatchView View = Mode->ReadPublicMatch();
    // Unpublished reads carry revision 0; publication assigns the next one.
    View.PublicRevision = 1;
    TestTrue(TEXT("The countdown publishes T=0 and the development deadline from it"),
        View.Phase == ELDMatchPhase::Countdown && View.ServerNowMs == 0 && View.StartedAtMs == CountdownMs
        && View.OpenAtMs == CountdownMs + DevelopmentMs && LDCapture::ValidatePublicMatchView(View, PublicError));
    {
        FLDPublicMatchView Late = View;
        Late.ServerNowMs = CountdownMs + 1;
        TestFalse(TEXT("A countdown view past T=0 is malformed"), LDCapture::ValidatePublicMatchView(Late, PublicError));
        FLDPublicMatchView Open = View;
        Open.CircleState = ELDCircleState::Empty;
        TestFalse(TEXT("A countdown view with an open circle is malformed"), LDCapture::ValidatePublicMatchView(Open, PublicError));
    }

    // The Host cannot end a match that has no elapsed time yet.
    FLDSessionRequest End;
    End.Key = {Generation, 1};
    End.Command = ELDSessionCommand::EndMatch;
    End.ExpectedRosterRevision = Mode->RosterRevision;
    const FLDSessionReceipt Refused = Mode->ProcessSessionCommand(Host, End);
    TestTrue(TEXT("Ending during the countdown is refused with its reason"),
        Refused.Outcome == ELDSessionOutcome::Rejected && Refused.Reason == ELDSessionReject::WrongPhase
        && Refused.Detail.Contains(TEXT("倒计时")) && Mode->MatchPhase == ELDMatchPhase::Countdown && !Mode->bResultLocked);

    // One authority step: Motion, the phases a fixture without bodies needs, then Capture, which ends the countdown.
    const auto Step = [&](int64 EndAtMs)
    {
        FString StepError;
        const bool bRan = Mode->BeginAuthorityStep(*Driver, Mode->MatchContext, EndAtMs) == ELDCommitReject::None
            && Mode->ExecuteAuthorityPhase(*Driver, ELDCommitPhase::Motion, [](const auto&) { return true; }, StepError)
            && Mode->ExecuteAuthorityPhase(*Driver, ELDCommitPhase::Actions, [](const auto&) { return true; }, StepError)
            && Mode->ExecuteAuthorityPhase(*Driver, ELDCommitPhase::Capture,
                [&](const FLDGameplayWritePermit& Permit) { return Mode->ExecuteCapturePhase(Permit, StepError); }, StepError)
            && Mode->FinishAuthorityStep(*Driver, StepError);
        if (!bRan) { AddError(StepError); }
        return bRan;
    };
    TestTrue(TEXT("A step just before T=0 runs"), Step(CountdownMs - 1));
    TestTrue(TEXT("The boundary just before T=0 still belongs to the countdown"),
        Mode->MatchPhase == ELDMatchPhase::Countdown && Mode->IsOpeningCountdown(Mode->MatchContext)
        && Mode->GetAuthorityNowMs() == CountdownMs - 1);
    TestTrue(TEXT("The step that reaches T=0 runs"), Step(CountdownMs));
    TestTrue(TEXT("The match enters development exactly at T=0"),
        Mode->MatchPhase == ELDMatchPhase::Development && !Mode->IsOpeningCountdown(Mode->MatchContext)
        && Mode->IsGameplayWriteOpen(Mode->MatchContext) && Mode->GetAuthorityNowMs() == CountdownMs);
    TestEqual(TEXT("No countdown boundary remains after T=0"), Mode->NextCountdownDueMs(), MAX_int64);
    View = Mode->ReadPublicMatch();
    View.PublicRevision = 2;
    TestTrue(TEXT("Development is published from T=0 with its unchanged deadline"),
        View.Phase == ELDMatchPhase::Development && View.ServerNowMs == CountdownMs && View.StartedAtMs == CountdownMs
        && View.OpenAtMs == CountdownMs + DevelopmentMs && LDCapture::ValidatePublicMatchView(View, PublicError));

    End.Key.RequestSequence = 2;
    const FLDSessionReceipt Ended = Mode->ProcessSessionCommand(Host, End);
    const FLDResultView& Result = Public->MatchResult;
    TestTrue(TEXT("After T=0 the Host can end the match; its duration counts from T=0"),
        Ended.Outcome == ELDSessionOutcome::Committed && Result.StartedAtMs == CountdownMs && Result.EndedAtMs == CountdownMs
        && Result.DurationMs == 0);

    // Presentation: the centre countdown and the refusal text of every gated command.
    FLDPublicMatchView Shown;
    Shown.Phase = ELDMatchPhase::Countdown;
    Shown.StartedAtMs = CountdownMs;
    TestEqual(TEXT("The whole countdown shows 5"), LDHUD::OpeningCountdown(Shown, true, 0), FString(TEXT("5")));
    TestEqual(TEXT("Seconds round up"), LDHUD::OpeningCountdown(Shown, true, 1), FString(TEXT("5")));
    TestEqual(TEXT("The last second shows 1"), LDHUD::OpeningCountdown(Shown, true, CountdownMs - 1), FString(TEXT("1")));
    TestEqual(TEXT("A local clock at T=0 waits for the authority with 1"), LDHUD::OpeningCountdown(Shown, true, CountdownMs),
        FString(TEXT("1")));
    TestTrue(TEXT("No clock, no countdown"), LDHUD::OpeningCountdown(Shown, false, 0).IsEmpty());
    Shown.Phase = ELDMatchPhase::Development;
    TestEqual(TEXT("Development starts with 开淫！"), LDHUD::OpeningCountdown(Shown, true, CountdownMs), FString(LDTerms::CountdownGo));
    TestTrue(TEXT("开淫！ shows only briefly"), LDHUD::OpeningCountdown(Shown, true, CountdownMs + LDHUD::CountdownGoShowMs).IsEmpty());
    Shown.Phase = ELDMatchPhase::CaptureOpen;
    TestTrue(TEXT("No countdown once the flagpole is open"), LDHUD::OpeningCountdown(Shown, true, CountdownMs).IsEmpty());
    TestEqual(TEXT("The phase line names the countdown"), LDHUD::PhaseLine(ELDMatchPhase::Countdown), FString(LDTerms::OpeningCountdown));
    TestEqual(TEXT("A refused attack, ability or dash says it has not started"), LDHUD::CombatRejectText(ELDCombatReject::NotStarted),
        FString(LDTerms::NotStartedYet));

    // 2026-11-07 user decision: "开局五秒允许移动和选技能,但不允许攻击和购买".
    {
        FLDSpatialOrderRequest Move;
        Move.Kind = ELDSpatialOrderKind::MoveGround;
        TestTrue(TEXT("A ground move is admitted before T=0"), LDOrderRules::CountdownAdmitsOrder(Move));
        Move.bQueue = true;
        TestTrue(TEXT("Also as a Shift step"), LDOrderRules::CountdownAdmitsOrder(Move));
    }
    for (const ELDSpatialOrderKind Kind : {ELDSpatialOrderKind::AttackUnit, ELDSpatialOrderKind::AttackMove,
        ELDSpatialOrderKind::CastUnit, ELDSpatialOrderKind::CastPoint})
    {
        FLDSpatialOrderRequest Attack;
        Attack.Kind = Kind;
        TestFalse(TEXT("An attack or cast order is refused before T=0"), LDOrderRules::CountdownAdmitsOrder(Attack));
    }
    TestTrue(TEXT("The D dash is movement and is admitted"), LDOrderRules::CountdownAdmitsAction(ELDCombatEntry::Dash));
    for (const ELDCombatEntry Entry : {ELDCombatEntry::Basic, ELDCombatEntry::Q, ELDCombatEntry::W, ELDCombatEntry::E})
    {
        TestFalse(TEXT("A basic attack or Q/W/E is refused before T=0"), LDOrderRules::CountdownAdmitsAction(Entry));
    }

    // An AI request refused by the countdown teaches the failure memory nothing.
    FLDOrderReceipt Order;
    Order.Status = ELDOrderStatus::Rejected;
    Order.Failure = ELDOrderFailure::NotStarted;
    FLDCombatActionReceipt Action;
    Action.Phase = ELDCombatPhase::Rejected;
    Action.Reject = ELDCombatReject::NotStarted;
    TestTrue(TEXT("Countdown refusals are dropped, not failed"),
        LDOpponentRequests::SettleOrder(Order) == ELDOpponentSettle::Dropped
        && LDOpponentRequests::SettleAction(Action) == ELDOpponentSettle::Dropped);
    return true;
}
#endif
