#include "Framework/LDMatchLifecycleTypes.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Combat/LDCombatDirector.h"
#include "Character/LDCharacter.h"
#include "Character/LDNativeMotionTypes.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Components/BoxComponent.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "Misc/ScopeExit.h"
#include <limits>
#include <type_traits>

#if WITH_DEV_AUTOMATION_TESTS
static_assert(!std::is_copy_constructible_v<FLDGameplayWritePermit>);
static_assert(!std::is_move_constructible_v<FLDGameplayWritePermit>);
static_assert(!std::is_default_constructible_v<FLDGameplayWritePermit>);

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoBodyIdentityTest, "Lockdown.Rules.DemoCore.BodyIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDemoBodyIdentityTest::RunTest(const FString& Parameters)
{
    const FGuid Entity=FGuid::NewGuid();
    const FLDVictimKey First{ELDVictimKind::Contender,Entity,1};
    const FLDVictimKey Second{ELDVictimKind::Contender,Entity,2};
    const FLDVictimKey Monster{ELDVictimKind::CampMember,Entity,1};
    TestTrue(TEXT("Body, entity and life have separate complete identities"), First.IsValid() && First!=Second && First!=Monster);
    TestFalse(TEXT("Zero life is not an active victim"), (FLDVictimKey{ELDVictimKind::Contender,Entity,0}).IsValid());
    TestFalse(TEXT("Unknown victim kind is not a camp member"), (FLDVictimKey{static_cast<ELDVictimKind>(255),Entity,1}).IsValid());
    TestFalse(TEXT("Actor-less/default identity cannot become a combat body"), FLDCombatBodyRef{}.IsValid());
    const FLDCombatBodyRef Body{First,FGuid::NewGuid()};
    TestTrue(TEXT("Same logical life does not authorize another physical body"),
        Body.IsValid() && Body!=(FLDCombatBodyRef{First,FGuid::NewGuid()}));
    const FGuid Ledger=FGuid::NewGuid();
    TSet<FLDDeathKey> Deaths;
    Deaths.Add({Ledger,First});
    Deaths.Add({Ledger,First});
    Deaths.Add({Ledger,Second});
    Deaths.Add({Ledger,Monster});
    Deaths.Add({FGuid::NewGuid(),First});
    TestEqual(TEXT("Death identity is match plus full victim, not actor/time/killer"), Deaths.Num(), 4);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoAuthorityTimeTest, "Lockdown.Rules.DemoCore.CheckedTime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDemoAuthorityTimeTest::RunTest(const FString& Parameters)
{
    FString Error;
    int64 Now=0, Event=0, Deadline=0;
    TestTrue(TEXT("Availability and event bins share the same raw time"),
        LDMatchLifecycle::AuthoritySecondsToNowMs(1.0002,Now,Error)
        && LDMatchLifecycle::AuthoritySecondsToEventMs(1.0002,Event,Error));
    TestEqual(TEXT("Availability does not reach the next deadline early"), Now, int64(1000));
    TestEqual(TEXT("Actual late event cannot be backdated by floor"), Event, int64(1001));
    TestTrue(TEXT("Raw release plus integer duration stays conservative"),
        LDMatchLifecycle::DeadlineFromSeconds(1.0002,250,Deadline,Error));
    TestEqual(TEXT("Flooring the release cannot shorten cooldown"), Deadline, int64(1251));
    TestTrue(TEXT("Integer endpoint bypasses floating-point round trips"),
        LDMatchLifecycle::AddDurationMs(9007199254740993LL,2,Deadline,Error));
    TestEqual(TEXT("Large integer deadline remains exact"), Deadline, 9007199254740995LL);
    TestTrue(TEXT("Last int64 deadline is representable"),
        LDMatchLifecycle::AddDurationMs(MAX_int64-1,1,Deadline,Error) && Deadline==MAX_int64);
    TestFalse(TEXT("Deadline never wraps into the past"), LDMatchLifecycle::AddDurationMs(MAX_int64,1,Deadline,Error));
    TestTrue(TEXT("Deadline failure has explicit detail and no valid-looking result"), !Error.IsEmpty() && Deadline==0);
    for (double Invalid : {-1., std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN(), 9223372036854775808./1000.})
    {
        TestFalse(TEXT("Malformed or overflowing game time is refused"),
            LDMatchLifecycle::AuthoritySecondsToNowMs(Invalid,Now,Error));
        TestFalse(TEXT("Event conversion has the same finite/range gate"),
            LDMatchLifecycle::AuthoritySecondsToEventMs(Invalid,Event,Error));
        TestFalse(TEXT("Time rejection cannot be silent"), Error.IsEmpty());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoClockProjectionTest, "Lockdown.Rules.DemoCore.ClockProjection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDemoClockProjectionTest::RunTest(const FString& Parameters)
{
    FLDDemoClockView Latest;
    FString Error;
    TestFalse(TEXT("An unpublished clock is unavailable, not authority time zero"), Latest.IsValid());
    TestTrue(TEXT("A published initial match clock is valid"),
        LDMatchLifecycle::AcceptClockView({1,1,0},Latest,Error)==ELDClockProjectionResult::Accepted && Latest.IsValid());
    TestTrue(TEXT("Exact repeated clock publications are idempotent"),
        LDMatchLifecycle::AcceptClockView({1,1,0},Latest,Error)==ELDClockProjectionResult::Duplicate);
    TestTrue(TEXT("Only an actually published completed step advances display time"),
        LDMatchLifecycle::AcceptClockView({1,2,2999},Latest,Error)==ELDClockProjectionResult::Accepted && Latest.NowMs==2999);
    TestTrue(TEXT("Out of order steps cannot reverse a countdown"),
        LDMatchLifecycle::AcceptClockView({1,1,0},Latest,Error)==ELDClockProjectionResult::Stale && Latest.NowMs==2999);
    TestTrue(TEXT("Same step cannot claim a later expiry"),
        LDMatchLifecycle::AcceptClockView({1,2,3000},Latest,Error)==ELDClockProjectionResult::Malformed
        && Latest.NowMs==2999 && !Error.IsEmpty());
    TestTrue(TEXT("A later step cannot regress the clock"),
        LDMatchLifecycle::AcceptClockView({1,3,2998},Latest,Error)==ELDClockProjectionResult::Malformed && Latest.NowMs==2999);
    TestTrue(TEXT("Protection reaches zero only at a real published deadline"),
        LDMatchLifecycle::AcceptClockView({1,3,3000},Latest,Error)==ELDClockProjectionResult::Accepted && Latest.NowMs==3000);
    TestTrue(TEXT("A new match has its own zero, not the prior world's time origin"),
        LDMatchLifecycle::AcceptClockView({2,1,0},Latest,Error)==ELDClockProjectionResult::Accepted && Latest.NowMs==0);
    TestTrue(TEXT("Old match clocks cannot corrupt the replacement match"),
        LDMatchLifecycle::AcceptClockView({1,MAX_uint64,MAX_int64},Latest,Error)==ELDClockProjectionResult::Stale && Latest.Generation==2);
    for (const FLDDemoClockView Bad : {FLDDemoClockView{0,1,0},FLDDemoClockView{2,0,0},FLDDemoClockView{2,2,-1}})
    {
        TestTrue(TEXT("Malformed clock fields never replace the valid publication"),
            LDMatchLifecycle::AcceptClockView(Bad,Latest,Error)==ELDClockProjectionResult::Malformed
            && !Error.IsEmpty() && Latest.Generation==2 && Latest.StepSerial==1 && Latest.NowMs==0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoCoreTest, "Lockdown.Rules.DemoCore.ScopedAuthority",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDDemoCoreTest::RunTest(const FString& Parameters)
{
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    if (!TestNotNull(TEXT("Isolated core permission world"),World)) { return false; }
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Mode=World->SpawnActor<ALDGameMode>();
    auto* Public=World->SpawnActor<ALDGameState>();
    auto* Driver=World->SpawnActor<ALDCombatDirector>();
    auto* OtherDriver=World->SpawnActor<ALDCombatDirector>();
    if (!TestNotNull(TEXT("Core owner"),Mode) || !TestNotNull(TEXT("Core public read model"),Public)
        || !TestNotNull(TEXT("Sole driver fixture"),Driver)
        || !TestNotNull(TEXT("Unregistered driver counterexample"),OtherDriver)) { return false; }
    Mode->bDemoWorld=Mode->bEntryAccepted=Mode->bAuthorityInitialized=true;
    Mode->GameState=Public;
    Mode->MatchContext={1,FGuid::NewGuid()};
    Mode->MatchPhase=ELDMatchPhase::Development;
    Mode->Combat=Driver;
    Mode->AuthorityStep={Mode->MatchContext,1,0,0};
    World->Tick(LEVELTICK_TimeOnly,.025f);
    const double NativeTime=World->GetTimeSeconds();
    TestTrue(TEXT("Native world advances the available budget"), NativeTime>=.02);
    TestTrue(TEXT("Unregistered driver cannot obtain a step"),
        Mode->BeginAuthorityStep(*OtherDriver,Mode->MatchContext,10)==ELDCommitReject::NotAuthority);
    TestTrue(TEXT("Network generation cannot replace the ledger identity"),
        Mode->BeginAuthorityStep(*Driver,{1,FGuid::NewGuid()},10)==ELDCommitReject::StaleMatch);
    TestTrue(TEXT("Future native interval is refused"),
        Mode->BeginAuthorityStep(*Driver,Mode->MatchContext,1000)==ELDCommitReject::TimeUnavailable);
    TestTrue(TEXT("Current driver receives an actually available step"),
        Mode->BeginAuthorityStep(*Driver,Mode->MatchContext,10)==ELDCommitReject::None);
    FString Error;
    TestFalse(TEXT("Actions cannot run before native motion"),
        Mode->ExecuteAuthorityPhase(*Driver,ELDCommitPhase::Actions,[](const auto&) { return true; },Error));
    TestTrue(TEXT("Motion receives a borrowed, body-step-bound scope"),
        Mode->ExecuteAuthorityPhase(*Driver,ELDCommitPhase::Motion,[&](const FLDGameplayWritePermit& Permit)
        {
            TestTrue(TEXT("Current phase validates"), Mode->ValidateWritePermit(Permit,ELDCommitPhase::Motion)==ELDCommitReject::None);
            TestTrue(TEXT("A Motion permit cannot write transactions"),
                Mode->ValidateWritePermit(Permit,ELDCommitPhase::Transactions)==ELDCommitReject::WrongPhase);
            TestTrue(TEXT("Another native step cannot reenter the active scope"),
                Mode->BeginAuthorityStep(*Driver,Mode->MatchContext,11)==ELDCommitReject::Reentrant);
            FString NestedError;
            TestFalse(TEXT("A callback cannot mint a nested gameplay permit"),
                Mode->ExecuteAuthorityPhase(*Driver,ELDCommitPhase::Actions,[](const auto&) { return true; },NestedError));
            ++Mode->GateRevision;
            TestTrue(TEXT("Closing/invalidation is checked again at final commit"),
                Mode->ValidateWritePermit(Permit,ELDCommitPhase::Motion)==ELDCommitReject::ScopeExpired);
            --Mode->GateRevision;
            return true;
        },Error));
    TestNull(TEXT("Borrowed scope is not retained after the callback"), Mode->ActiveWritePermit);
    TestEqual(TEXT("Simulation reaches the exact executed endpoint"), Mode->GetAuthorityNowMs(), int64(10));
    TestEqual(TEXT("Advancing the match never rewrites native World time"), World->GetTimeSeconds(), NativeTime);
    TestFalse(TEXT("A Motion phase cannot integrate again"),
        Mode->ExecuteAuthorityPhase(*Driver,ELDCommitPhase::Motion,[](const auto&) { return true; },Error));
    TestFalse(TEXT("Core does not finish an incomplete action phase"), Mode->FinishAuthorityStep(*Driver,Error));
    TestTrue(TEXT("Actions follow the completed native position step"),
        Mode->ExecuteAuthorityPhase(*Driver,ELDCommitPhase::Actions,[](const auto&) { return true; },Error));
    TestTrue(TEXT("The completed primitive step can retire"), Mode->FinishAuthorityStep(*Driver,Error));
    TestTrue(TEXT("Display clock copies the completed core step without using native World time"),
        Public->DemoClock.Generation==Mode->MatchContext.Generation
        && Public->DemoClock.StepSerial==Mode->AuthorityStep.StepSerial && Public->DemoClock.NowMs==10);
    TestTrue(TEXT("Completed time cannot be integrated twice"),
        Mode->BeginAuthorityStep(*Driver,Mode->MatchContext,10)==ELDCommitReject::TimeRegression);
    Mode->AuthorityStep.StepSerial=MAX_uint64;
    TestTrue(TEXT("Step counter exhaustion does not wrap"),
        Mode->BeginAuthorityStep(*Driver,Mode->MatchContext,11)==ELDCommitReject::CounterExhausted);
    Mode->MatchPhase=ELDMatchPhase::Results;
    TestTrue(TEXT("Closed match cannot issue motion"),
        Mode->BeginAuthorityStep(*Driver,Mode->MatchContext,11)==ELDCommitReject::MatchClosed);

    auto* Body=World->SpawnActor<ALDCharacter>();
    if (!TestNotNull(TEXT("Body binding for bounded native-prefix tests"),Body)) { return false; }
    const FLDCombatBodyRef Ref{{ELDVictimKind::Contender,FGuid::NewGuid(),1},FGuid::NewGuid()};
    FLDBodyCombatState BodyState;
    BodyState.Identity=Ref.BodyIdentity;
    BodyState.Victim=Ref.Victim;
    Driver->Bodies.Add(Body,MoveTemp(BodyState));
    auto& Life=Mode->Lives.Add(Ref.Victim.EntityId);
    Life.Phase=ELDLifePhase::Alive;
    Life.Life=1;
    Life.Body=Life.MotionBody=Ref;
    Life.Actor=Body;
    Life.MotionStreamId=FGuid::NewGuid();
    Mode->MatchPhase=ELDMatchPhase::Development;
    FLDObservedPublicFacts PublicFacts;
    TestFalse(TEXT("A missing committed public build is not defaulted to level one"),
        Mode->ReadObservedPublicFacts(Mode->MatchContext,Ref,PublicFacts,Error));
    auto& Registered=Driver->Bodies.FindChecked(Body);
    Registered.Resources.Revision=Registered.AttributeRevision=1;
    FLDObservedPublicFacts Committed;
    Committed.Level=1;
    Committed.Equipment.Slots[0]=Committed.Equipment.Slots[1]=0;
    Registered.PublicFacts=Committed;
    Life.ProtectedUntilMs=100;
    Mode->AuthorityNowMs=99;
    TestTrue(TEXT("Current alive protection is derived once with its exact core deadline"),
        Mode->ReadObservedPublicFacts(Mode->MatchContext,Ref,PublicFacts,Error)
        && PublicFacts.Level==1 && PublicFacts.Statuses.Num()==1
        && PublicFacts.Statuses[0].StatusId==uint8(ELDDemoStatusId::RespawnProtection)
        && PublicFacts.Statuses[0].Stacks==1 && PublicFacts.Statuses[0].ExpiresAtServerMs==100);
    Mode->AuthorityNowMs=100;
    TestTrue(TEXT("The expiry boundary omits the cue without clearing or moving the deadline"),
        Mode->ReadObservedPublicFacts(Mode->MatchContext,Ref,PublicFacts,Error)
        && PublicFacts.Statuses.IsEmpty() && Life.ProtectedUntilMs==100);
    TestFalse(TEXT("An old match cannot acquire the current body's public facts"),
        Mode->ReadObservedPublicFacts({1,FGuid::NewGuid()},Ref,PublicFacts,Error));
    TestFalse(TEXT("A different physical body cannot reuse the logical life"),
        Mode->ReadObservedPublicFacts(Mode->MatchContext,{Ref.Victim,FGuid::NewGuid()},PublicFacts,Error));
    Registered.PublicFacts->Statuses.Add({uint8(ELDDemoStatusId::RespawnProtection),1,100});
    TestFalse(TEXT("A stored protection status is rejected even after its deadline"),
        Mode->ReadObservedPublicFacts(Mode->MatchContext,Ref,PublicFacts,Error));
    Registered.PublicFacts->Statuses.Reset();
    Life.Phase=ELDLifePhase::Dead;
    TestFalse(TEXT("A dead life cannot publish an old protection cue"),
        Mode->ReadObservedPublicFacts(Mode->MatchContext,Ref,PublicFacts,Error));
    Life.Phase=ELDLifePhase::Alive;
    Life.ProtectedUntilMs=0;
    Mode->AuthorityNowMs=10;
    Mode->bAuthorityStepOpen=true;
    // Empty stationary batches still occupy an unconsumed native prefix.
    for (uint64 Index=1; Index<=255; ++Index)
    {
        auto Batch=MakeShared<FLDNativeMotionBatch>();
        Batch->Step={Mode->MatchContext,Index,double(Index-1)/1000.,double(Index)/1000.};
        Batch->Body=Ref;
        Batch->StreamId=Life.MotionStreamId;
        Life.NativeMotion.Add(Batch);
    }
    Life.CompletedMotionStep=255;
    Mode->AuthorityStep={Mode->MatchContext,256,.255,.256};
    TestTrue(TEXT("Exactly 255 retained batches leave space for one native step"),
        Mode->ValidateMotionStep(Mode->AuthorityStep,Ref)==ELDCommitReject::None);
    auto LastBatch=MakeShared<FLDNativeMotionBatch>();
    LastBatch->Step=Mode->AuthorityStep;
    LastBatch->Body=Ref;
    LastBatch->StreamId=Life.MotionStreamId;
    Life.NativeMotion.Add(LastBatch);
    Life.CompletedMotionStep=256;
    Mode->AuthorityStep={Mode->MatchContext,257,.256,.257};
    TestTrue(TEXT("The actual 256-batch limit backpressures before another integration"),
        Mode->ValidateMotionStep(Mode->AuthorityStep,Ref)==ELDCommitReject::CapacityExceeded);
    TestTrue(TEXT("A real retained partial prefix may be acknowledged"),
        Mode->AcknowledgeNativeMotion(Mode->MatchContext,Ref,128,0,Error));
    TestEqual(TEXT("Acknowledgement releases only its exact prefix"),Life.NativeMotion.Num(),128);
    TestTrue(TEXT("Acknowledging the same prefix is idempotent"),
        Mode->AcknowledgeNativeMotion(Mode->MatchContext,Ref,128,0,Error));
    TestFalse(TEXT("Same step with a different sequence cannot discard more evidence"),
        Mode->AcknowledgeNativeMotion(Mode->MatchContext,Ref,128,1,Error));
    TestEqual(TEXT("Conflicting acknowledgement retains the suffix"),Life.NativeMotion.Num(),128);
    TestTrue(TEXT("Backpressure clears only after actual prefix consumption"),
        Mode->ValidateMotionStep(Mode->AuthorityStep,Ref)==ELDCommitReject::None);
    Mode->RemoveLifeBinding(Ref.Victim.EntityId);
    TestTrue(TEXT("A retired body keeps accepted evidence until consumed"),
        Life.Phase==ELDLifePhase::Left && Life.NativeMotion.Num()==128 && Life.MotionBody==Ref);
    TestTrue(TEXT("The exact retired stream can release its already accepted final prefix"),
        Mode->AcknowledgeNativeMotion(Mode->MatchContext,Ref,256,0,Error));
    TestTrue(TEXT("Retired native history is bounded and reclaimable"),Life.NativeMotion.IsEmpty());
    const uint64 OpenGate=Mode->GateRevision;
    TestFalse(TEXT("A stale reporter cannot fault the current match"),
        Mode->ReportAuthorityFault({1,FGuid::NewGuid()},TEXT("Obsolete world query")));
    TestTrue(TEXT("Stale fault report preserves the live gate"),
        Mode->MatchPhase==ELDMatchPhase::Development && Mode->GateRevision==OpenGate);
    Mode->MatchPhase=ELDMatchPhase::Results;
    TestFalse(TEXT("Late provider fault cannot overwrite a locked result"),
        Mode->ReportAuthorityFault(Mode->MatchContext,TEXT("Late world query")));
    TestTrue(TEXT("Locked result remains closed without another mutation"),
        Mode->MatchPhase==ELDMatchPhase::Results && Mode->GateRevision==OpenGate);
    Mode->MatchPhase=ELDMatchPhase::Development;
    AddExpectedError(TEXT("LD_DEMO_AUTHORITY_FAULT Controlled provider failure"),EAutomationExpectedErrorFlags::Contains,1);
    TestTrue(TEXT("A current provider failure closes the real core gate"),
        Mode->ReportAuthorityFault(Mode->MatchContext,TEXT("Controlled provider failure"))
        && Mode->MatchPhase==ELDMatchPhase::Faulted && Mode->GateRevision==OpenGate+1
        && !Mode->IsGameplayWriteOpen(Mode->MatchContext));
    TestTrue(TEXT("Repeated fault report is idempotent"),
        Mode->ReportAuthorityFault(Mode->MatchContext,TEXT("Controlled provider failure")) && Mode->GateRevision==OpenGate+1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDRewardSettlementTest, "Lockdown.Rules.DemoCore.RewardSettlementBeforeClose",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDRewardSettlementTest::RunTest(const FString& Parameters)
{
    FLDMatchProviderHooks Progression;
    Progression.ReadFinalLevel=[](const ALDGameMode&,const FGuid&,uint8& Level,FString&) { Level=2; return true; };
    LDMatchProviders::FScopedTestTable Providers({{ELDMatchProvider::Progression,Progression}});
    struct FMatch
    {
        ALDGameMode* Mode=nullptr;
        ALDGameState* Public=nullptr;
        ALDCombatDirector* Driver=nullptr;
        ALDPlayerController* Host=nullptr;
        FGuid Winner;
    };
    TArray<UWorld*> Worlds;
    ON_SCOPE_EXIT { for (UWorld* World : Worlds) { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); } };
    // An open match whose sole contender holds the circle from 1 ms; the hold completes at exactly 11 ms.
    const auto Open=[&](FMatch& Out)
    {
        const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
            .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
        UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
        if (!World) { return false; }
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        Worlds.Add(World);
        Out.Mode=World->SpawnActor<ALDGameMode>();
        Out.Public=World->SpawnActor<ALDGameState>();
        Out.Driver=World->SpawnActor<ALDCombatDirector>();
        Out.Host=World->SpawnActor<ALDPlayerController>();
        if (!Out.Mode || !Out.Public || !Out.Driver || !Out.Host) { return false; }
        Out.Host->SetAsLocalPlayerController();
        auto* Mode=Out.Mode;
        Mode->bDemoWorld=Mode->bEntryAccepted=Mode->bAuthorityInitialized=true;
        Mode->GameState=Out.Public;
        Mode->MatchContext={1,FGuid::NewGuid()};
        Mode->MatchPhase=ELDMatchPhase::Development;
        Mode->RosterRevision=1;
        Mode->Combat=Out.Driver;
        Mode->AuthorityStep={Mode->MatchContext,1,0,0};
        Out.Winner=FGuid::NewGuid();
        FLDContenderRecord Record;
        Record.Session=Out.Winner;
        Record.Name=TEXT("Holder");
        Mode->Contenders.Add(Out.Host,Record);
        Mode->RosterOrder={Out.Winner};
        Mode->LobbyHost=Out.Host;
        // Present, but without a live body: the fixture has no native motion to integrate.
        auto& Life=Mode->Lives.Add(Out.Winner);
        Life.Kind=ELDVictimKind::Contender;
        Life.Phase=ELDLifePhase::Unspawned;
        FString Error;
        FLDCaptureEvent Joined;
        Joined.Kind=ELDCaptureEventKind::Joined;
        Joined.Body={{ELDVictimKind::Contender,Out.Winner,1},FGuid::NewGuid()};
        Joined.Phase=ELDCommitPhase::RespawnsAndWorld;
        Joined.bInside=true;
        // Sole from 1 ms: the 4 ms spray lands at 5 ms and the 6 ms guard wins at 11 ms.
        if (!Mode->CaptureReducer.Begin(0,4,6,Error) || !FLDCaptureEventTime::FromMs(1,Joined.Time,Error)
            || !Mode->CaptureReducer.Enqueue(Joined,Error))
        {
            AddError(Error);
            return false;
        }
        World->Tick(LEVELTICK_TimeOnly,.025f);
        return true;
    };
    const auto RetryFact=[](const FMatch& Match)
    {
        FLDCommittedDeath Fact;
        Fact.Key={Match.Mode->MatchContext.LedgerMatch,{ELDVictimKind::CampMember,FGuid::NewGuid(),1}};
        Fact.CreditOwner=Match.Winner;
        return Fact;
    };
    // Boundary 11: a kill's reward is refused as retryable, then the guard completes at the same instant.
    const auto RunCaptureBoundary=[&](FMatch& Match)
    {
        FString Error;
        auto* Mode=Match.Mode;
        auto* Driver=Match.Driver;
        const bool Ran=Mode->BeginAuthorityStep(*Driver,Mode->MatchContext,11)==ELDCommitReject::None
            && Mode->ExecuteAuthorityPhase(*Driver,ELDCommitPhase::Motion,[](const auto&) { return true; },Error)
            && Mode->ExecuteAuthorityPhase(*Driver,ELDCommitPhase::DeathsAndRewards,
                [&](const auto&) { Driver->RetryRewards.Add(RetryFact(Match)); return true; },Error)
            && Mode->ExecuteAuthorityPhase(*Driver,ELDCommitPhase::Capture,
                [&](const FLDGameplayWritePermit& Permit) { return Mode->ExecuteCapturePhase(Permit,Error); },Error)
            && Mode->ExecuteAuthorityPhase(*Driver,ELDCommitPhase::Close,
                [&](const FLDGameplayWritePermit& Permit) { return Mode->ExecuteClosePhase(Permit,Error); },Error)
            && Mode->FinishAuthorityStep(*Driver,Error);
        if (!Ran) { AddError(FString::Printf(TEXT("Capture boundary failed: %s"),*Error)); }
        return Ran;
    };

    FMatch Held;
    if (!TestTrue(TEXT("Settlement fixture opens"),Open(Held))) { return false; }
    auto* Mode=Held.Mode;
    Held.Driver->RetryRewards.Add(RetryFact(Held));
    FLDSessionRequest End;
    End.Key={Mode->MatchContext.Generation,1};
    End.Command=ELDSessionCommand::EndMatch;
    End.ExpectedRosterRevision=Mode->RosterRevision;
    TestTrue(TEXT("The Host cannot end the match over a reward that awaits its retry"),
        Mode->ProcessSessionCommand(Held.Host,End).Reason==ELDSessionReject::RequestInFlight
        && !Mode->bResultLocked && Mode->IsGameplayWriteOpen(Mode->MatchContext));
    FString Error;
    TestFalse(TEXT("No result locks while a required reward awaits its retry"),
        Mode->TryCloseMatch(ELDMatchEndReason::HostEnded,FGuid(),Mode->GetAuthorityNowMs(),Error));
    TestTrue(TEXT("The refused close names the pending reward"),Error.Contains(TEXT("reward")));
    Held.Driver->RetryRewards.Reset();

    FMatch Captured;
    if (!TestTrue(TEXT("Capture fixture opens"),Open(Captured))) { return false; }
    Mode=Captured.Mode;
    if (!RunCaptureBoundary(Captured)) { return false; }
    TestTrue(TEXT("The completed guard waits for the pending reward instead of locking over it"),
        !Mode->bResultLocked && Mode->IsGameplayWriteOpen(Mode->MatchContext) && Mode->PendingCapture.IsSet()
        && Mode->PendingCapture->Winner==Captured.Winner && Mode->PendingCapture->DueAtMs==11
        && Mode->PendingCapture->GuardSinceMs==5);
    auto* Driver=Captured.Driver;
    TestTrue(TEXT("A held completion admits no later instant"),
        Mode->IsCloseAwaitingSettlement() && Mode->BeginAuthorityStep(*Driver,Mode->MatchContext,12)==ELDCommitReject::WrongPhase);
    const uint64 CaptureStep=Mode->AuthorityStep.StepSerial;
    TestTrue(TEXT("The settlement boundary repeats the completion instant with a fresh batch"),
        Mode->BeginSettlementStep(*Driver,Mode->MatchContext)==ELDCommitReject::None
        && Mode->AuthorityBoundary.AtMs==11 && Mode->AuthorityBoundary.BatchSerial==CaptureStep+1
        && Mode->AuthorityStep.StartServerSeconds==Mode->AuthorityStep.EndServerSeconds);
    TestFalse(TEXT("A settlement boundary integrates no motion"),
        Mode->ExecuteAuthorityPhase(*Driver,ELDCommitPhase::Motion,[](const auto&) { return true; },Error));
    TestFalse(TEXT("A settlement boundary runs no actions, transactions or capture"),
        Mode->ExecuteAuthorityPhase(*Driver,ELDCommitPhase::Actions,[](const auto&) { return true; },Error)
        || Mode->ExecuteAuthorityPhase(*Driver,ELDCommitPhase::Transactions,[](const auto&) { return true; },Error)
        || Mode->ExecuteAuthorityPhase(*Driver,ELDCommitPhase::Capture,[](const auto&) { return true; },Error));
    bool RetriedOnce=false;
    TestTrue(TEXT("The owed reward gets its single retry under the settlement permit"),
        Mode->ExecuteAuthorityPhase(*Driver,ELDCommitPhase::DeathsAndRewards,[&](const FLDGameplayWritePermit& Permit)
        {
            RetriedOnce=Mode->ValidateWritePermit(Permit,ELDCommitPhase::DeathsAndRewards)==ELDCommitReject::None
                && Permit.GetBoundary().AtMs==11 && Driver->RetryRewards.Num()==1 && !Mode->bResultLocked;
            Driver->RetryRewards.Reset();
            return true;
        },Error) && RetriedOnce);
    TestTrue(TEXT("Close then locks the winner at the exact completion instant"),
        Mode->ExecuteAuthorityPhase(*Driver,ELDCommitPhase::Close,
            [&](const FLDGameplayWritePermit& Permit) { return Mode->ExecuteClosePhase(Permit,Error); },Error)
        && Mode->bResultLocked && !Mode->IsGameplayWriteOpen(Mode->MatchContext) && !Mode->PendingCapture.IsSet()
        && Mode->ResultView.Reason==ELDMatchEndReason::GuardSucceeded && Mode->ResultView.Winner==Captured.Winner
        && Mode->ResultView.EndedAtMs==11);
    TestTrue(TEXT("The settlement boundary completes without moving the clock"),
        Mode->FinishAuthorityStep(*Driver,Error) && !Mode->bSettlementStep && Mode->GetAuthorityNowMs()==11
        && Captured.Public->DemoClock.StepSerial==CaptureStep+1 && Captured.Public->DemoClock.NowMs==11);
    TestTrue(TEXT("No step follows the locked result"),
        Mode->BeginAuthorityStep(*Driver,Mode->MatchContext,12)==ELDCommitReject::MatchClosed
        && Mode->BeginSettlementStep(*Driver,Mode->MatchContext)==ELDCommitReject::MatchClosed);

    // The retry did not pay: no normal winner is locked over it, and the fault leaves nothing for the next match.
    FMatch Unpaid;
    if (!TestTrue(TEXT("Unpaid fixture opens"),Open(Unpaid))) { return false; }
    Mode=Unpaid.Mode;
    Driver=Unpaid.Driver;
    if (!RunCaptureBoundary(Unpaid)) { return false; }
    TestTrue(TEXT("The unpaid fixture holds its completion"),Mode->IsCloseAwaitingSettlement()
        && Mode->BeginSettlementStep(*Driver,Mode->MatchContext)==ELDCommitReject::None);
    TestTrue(TEXT("Retries run before the settlement Close"),
        Mode->ExecuteAuthorityPhase(*Driver,ELDCommitPhase::DeathsAndRewards,[](const auto&) { return true; },Error));
    TestFalse(TEXT("A settlement Close refuses to lock over a reward still owed"),
        Mode->ExecuteAuthorityPhase(*Driver,ELDCommitPhase::Close,
            [&](const FLDGameplayWritePermit& Permit) { return Mode->ExecuteClosePhase(Permit,Error); },Error));
    TestTrue(TEXT("The refusal locks nothing"),!Mode->bResultLocked && !Mode->PendingCapture.IsSet());
    AddExpectedError(TEXT("LD_DEMO_AUTHORITY_FAULT The settlement boundary still owes a reward"),EAutomationExpectedErrorFlags::Contains,1);
    TestTrue(TEXT("The driver's failure faults the match without a winner"),
        Mode->ReportAuthorityFault(Mode->MatchContext,Error) && Mode->MatchPhase==ELDMatchPhase::Faulted
        && !Mode->bResultLocked && !Mode->bSettlementStep && !Mode->bAuthorityStepOpen
        && !Mode->IsGameplayWriteOpen(Mode->MatchContext));
    Mode->RetireMatchDomains();
    TestTrue(TEXT("Retiring the match drops its held completion and owed rewards with its Combat owner"),
        !Mode->PendingCapture.IsSet() && !Mode->Combat && !Mode->HasPendingRewardRetries() && !Mode->CaptureReducer.IsActive());

    // The driver's real failure path: the single retry is consumed and refused again before Close runs.
    FMatch Failed;
    if (!TestTrue(TEXT("Failed-retry fixture opens"),Open(Failed))) { return false; }
    Mode=Failed.Mode;
    Driver=Failed.Driver;
    if (!RunCaptureBoundary(Failed)) { return false; }
    TestTrue(TEXT("The failed-retry fixture holds its completion"),
        Mode->BeginSettlementStep(*Driver,Mode->MatchContext)==ELDCommitReject::None);
    TestFalse(TEXT("A retry refused again fails the settlement boundary"),
        Mode->ExecuteAuthorityPhase(*Driver,ELDCommitPhase::DeathsAndRewards,[&](const auto&)
        {
            const TArray<FLDCommittedDeath> Retries=MoveTemp(Driver->RetryRewards);
            Driver->RetryRewards.Reset();
            Error=FString::Printf(TEXT("Controlled second refusal of %d owed reward"),Retries.Num());
            return false;
        },Error));
    AddExpectedError(TEXT("LD_DEMO_AUTHORITY_FAULT Controlled second refusal of 1 owed reward"),EAutomationExpectedErrorFlags::Contains,1);
    TestTrue(TEXT("The fault abandons the held completion: no winner is ever locked"),
        Mode->ReportAuthorityFault(Mode->MatchContext,Error) && Mode->MatchPhase==ELDMatchPhase::Faulted
        && !Mode->bResultLocked && !Mode->PendingCapture.IsSet() && !Mode->IsCloseAwaitingSettlement()
        && !Mode->bSettlementStep && !Mode->IsGameplayWriteOpen(Mode->MatchContext));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCaptureInsideStepTest, "Lockdown.Rules.DemoCore.CaptureCompletesInsideStep",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCaptureInsideStepTest::RunTest(const FString& Parameters)
{
    FLDMatchProviderHooks Progression;
    Progression.ReadFinalLevel=[](const ALDGameMode&,const FGuid&,uint8& Level,FString&) { Level=2; return true; };
    LDMatchProviders::FScopedTestTable Providers({{ELDMatchProvider::Progression,Progression}});
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    if (!TestNotNull(TEXT("Fixture world"),World)) { return false; }
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Mode=World->SpawnActor<ALDGameMode>();
    auto* Public=World->SpawnActor<ALDGameState>();
    auto* Driver=World->SpawnActor<ALDCombatDirector>();
    auto* Host=World->SpawnActor<ALDPlayerController>();
    if (!Mode || !Public || !Driver || !Host) { AddError(TEXT("Fixture actors")); return false; }
    Host->SetAsLocalPlayerController();
    Mode->bDemoWorld=Mode->bEntryAccepted=Mode->bAuthorityInitialized=true;
    Mode->GameState=Public;
    Mode->MatchContext={1,FGuid::NewGuid()};
    Mode->MatchPhase=ELDMatchPhase::Development;
    Mode->RosterRevision=1;
    Mode->Combat=Driver;
    Mode->AuthorityStep={Mode->MatchContext,1,0,0};
    const FGuid Holder=FGuid::NewGuid(), Rival=FGuid::NewGuid();
    FLDContenderRecord Record;
    Record.Session=Holder;
    Record.Name=TEXT("Holder");
    Mode->Contenders.Add(Host,Record);
    Mode->RosterOrder={Holder};
    Mode->LobbyHost=Host;
    // Present without live bodies: the fixture integrates no native motion and injects its presence facts.
    auto& Life=Mode->Lives.Add(Holder);
    Life.Kind=ELDVictimKind::Contender;
    Life.Phase=ELDLifePhase::Unspawned;
    const FLDCombatBodyRef HolderBody{{ELDVictimKind::Contender,Holder,1},FGuid::NewGuid()};
    const FLDCombatBodyRef RivalBody{{ELDVictimKind::Contender,Rival,1},FGuid::NewGuid()};
    FString Error;
    const auto Fact=[&](ELDCaptureEventKind Kind,const FLDCombatBodyRef& Body,double Seconds,ELDCommitPhase Phase,bool Inside)
    {
        FLDCaptureEvent Event;
        Event.Kind=Kind;
        Event.Body=Body;
        Event.Phase=Phase;
        Event.bInside=Inside;
        return FLDCaptureEventTime::FromSeconds(Seconds,Event.Time,Error) && Mode->CaptureReducer.Enqueue(Event,Error);
    };
    // Open at 0 with a 2 ms spray and a 3 ms guard; the holder is sole from 1 ms and sprays at 3 ms.
    if (!Mode->CaptureReducer.Begin(0,2,3,Error)
        || !Fact(ELDCaptureEventKind::Joined,HolderBody,.001,ELDCommitPhase::RespawnsAndWorld,true)
        || !Fact(ELDCaptureEventKind::Joined,RivalBody,.001,ELDCommitPhase::RespawnsAndWorld,false))
    { AddError(Error); return false; }
    World->Tick(LEVELTICK_TimeOnly,.025f);
    // A bound zone (no native ground needed: the fixture injects presence) lets Combat's control path reach the reducer.
    Mode->CaptureCircle.CaptureId=TEXT("capture_fixture");
    Mode->CaptureCircle.RadiusCm=300;
    Mode->CaptureCircle.Ground=NewObject<UBoxComponent>(GetTransientPackage());
    const auto Phase=[&](ELDCommitPhase Which,TFunctionRef<bool()> Body)
    { return Mode->ExecuteAuthorityPhase(*Driver,Which,[&](const FLDGameplayWritePermit&) { return Body(); },Error); };
    const auto Capture=[&]()
    {
        return Mode->ExecuteAuthorityPhase(*Driver,ELDCommitPhase::Capture,
                [&](const FLDGameplayWritePermit& Permit) { return Mode->ExecuteCapturePhase(Permit,Error); },Error)
            && Mode->ExecuteAuthorityPhase(*Driver,ELDCommitPhase::Close,
                [&](const FLDGameplayWritePermit& Permit) { return Mode->ExecuteClosePhase(Permit,Error); },Error)
            && Mode->FinishAuthorityStep(*Driver,Error);
    };
    ELDCommitReject StaleInterrupt=ELDCommitReject::None, EffectsInterrupt=ELDCommitReject::InvalidRequest;

    // Step to 5 ms: the spray lands inside the step at 3 ms without ending the match; the owner walks out at 3.5 ms,
    // pausing his guard at 1 of 3 ms, and the rival entering at 4 ms sprays alone, keeping it frozen there.
    const bool Sprayed=Mode->BeginAuthorityStep(*Driver,Mode->MatchContext,5)==ELDCommitReject::None
        && !Mode->CaptureCompletesInsideStep()
        && Phase(ELDCommitPhase::Motion,[&]
        {
            return Fact(ELDCaptureEventKind::Presence,HolderBody,.0035,ELDCommitPhase::Motion,false)
                && Fact(ELDCaptureEventKind::Presence,RivalBody,.004,ELDCommitPhase::Motion,true);
        })
        && !Mode->CaptureCompletesInsideStep()
        && Mode->ExecuteAuthorityPhase(*Driver,ELDCommitPhase::Actions,[&](const FLDGameplayWritePermit& Permit)
            { StaleInterrupt=Mode->PublishCaptureInterrupt(Permit,{{ELDVictimKind::Contender,Rival,2},RivalBody.BodyIdentity}); return true; },Error)
        // A control committed on the rival at 5 ms (the Effects boundary) restarts his spray from zero.
        && Mode->ExecuteAuthorityPhase(*Driver,ELDCommitPhase::Effects,[&](const FLDGameplayWritePermit& Permit)
            { EffectsInterrupt=Mode->PublishCaptureInterrupt(Permit,RivalBody); return true; },Error)
        && Capture();
    if (!TestTrue(FString::Printf(TEXT("Spray step: %s"),*Error),Sprayed)) { return false; }
    TestTrue(TEXT("Only the current living body can be interrupted; the control path is accepted inside its permit"),
        StaleInterrupt==ELDCommitReject::UnknownBody && EffectsInterrupt==ELDCommitReject::None);
    TestTrue(TEXT("A spray inside a step is no win; the sole rival pauses the guard and, interrupted, sprays again from zero"),
        !Mode->bResultLocked && Mode->CaptureReducer.GetState()==ELDCircleState::Solo && Mode->CaptureReducer.GetOccupier()==Rival
        && Mode->CaptureReducer.GetSoloSinceMs()==5 && Mode->CaptureReducer.GetGraffitiOwner()==Holder
        && Mode->CaptureReducer.GetGuardKeptMs()==1 && Mode->NextCaptureDueMs()==7);

    // Step to 10 ms: the rival walks out at 5.5 ms, just before his spray, and the owner steps back in at 5.6 ms, so the
    // guard resumes and wins at 8 ms, inside the step; the rival's re-entry at 8.5 ms cannot undo it. The later boundary
    // instant's gameplay phases do not run.
    const bool Stepped=Mode->BeginAuthorityStep(*Driver,Mode->MatchContext,10)==ELDCommitReject::None
        && Phase(ELDCommitPhase::Motion,[&]
        {
            return Fact(ELDCaptureEventKind::Presence,RivalBody,.0055,ELDCommitPhase::Motion,false)
                && Fact(ELDCaptureEventKind::Presence,HolderBody,.0056,ELDCommitPhase::Motion,true)
                && Fact(ELDCaptureEventKind::Presence,RivalBody,.0085,ELDCommitPhase::Motion,true);
        });
    if (!TestTrue(FString::Printf(TEXT("Resume step motion: %s"),*Error),Stepped)) { return false; }
    TestTrue(TEXT("The motion prefix closes the guard inside the step"),Mode->CaptureCompletesInsideStep());
    TestTrue(FString::Printf(TEXT("Owed rewards, Capture and Close finish the step: %s"),*Error),
        Phase(ELDCommitPhase::DeathsAndRewards,[] { return true; }) && Capture());
    TestTrue(TEXT("The owner wins at his exact resumed guard instant, before the step's boundary"),
        Mode->bResultLocked && !Mode->IsGameplayWriteOpen(Mode->MatchContext) && !Mode->PendingCapture.IsSet()
        && Mode->ResultView.Reason==ELDMatchEndReason::GuardSucceeded && Mode->ResultView.Winner==Holder
        && Mode->ResultView.EndedAtMs==8 && Mode->GetAuthorityNowMs()==10);
    return true;
}
#endif
