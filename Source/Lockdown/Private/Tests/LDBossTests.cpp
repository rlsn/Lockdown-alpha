#include "World/LDBoss.h"
#include "Character/LDCharacter.h"
#include "Combat/LDAttributeProjection.h"
#include "Combat/LDCombatDirector.h"
#include "Combat/LDTacticalRules.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDProgressionRules.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/LDCaptureTypes.h"
#include "Framework/LDGameMode.h"
#include "Misc/ScopeExit.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
namespace LDBossTests
{
// Test-only rules with the approved shape (aggro beyond reach, four waypoints); the values are not the approved ones,
// so the rules never lean on a particular number.
FLDBossState MakeState()
{
    FLDBossState State;
    FLDBossRules& R = State.Rules;
    R.StartedAtMs = 1000;
    R.AppearAtMs = 20000;
    R.AggroRadiusCm = 300.;
    R.AttackRangeCm = 200.;
    R.AttackerMemoryMs = 4000;
    R.MonsterId = TEXT("monster_dean");
    R.CreatureId = 3;
    R.SurfaceId = TEXT("demo_ground");
    R.Waypoints = {FVector(2400, 2400, 0), FVector(-2400, 2400, 0), FVector(-2400, -2400, 0), FVector(2400, -2400, 0)};
    State.Phase = ELDBossPhase::Pending;
    State.EntityId = FGuid(0xB0, 0, 0, 1);
    return State;
}

FLDCombatBodyRef BodyOf(uint32 Id, ELDVictimKind Kind = ELDVictimKind::Contender)
{
    FLDCombatBodyRef Ref;
    Ref.Victim.Kind = Kind;
    Ref.Victim.EntityId = FGuid(Id, 0, 0, 0);
    Ref.Victim.Life = 1;
    Ref.BodyIdentity = FGuid(Id, 1, 0, 0);
    return Ref;
}

LDBoss::FCandidate Seen(uint32 Id, const FVector& Foot, bool bVisible = true)
{
    LDBoss::FCandidate Out;
    Out.Body = BodyOf(Id);
    Out.FootCm = Foot;
    Out.bVisible = bVisible;
    return Out;
}

FLDCombatBodyRef BossBody(const FLDBossState& State)
{
    FLDCombatBodyRef Ref;
    Ref.Victim.Kind = ELDVictimKind::CampMember;
    Ref.Victim.EntityId = State.EntityId;
    Ref.Victim.Life = 1;
    Ref.BodyIdentity = FGuid(0xB0, 1, 0, 1);
    return Ref;
}

FLDPublicMatchView ViewAt(const FLDBossState& State, int64 NowMs)
{
    FLDPublicMatchView View;
    View.Generation = 1;
    View.PublicRevision = 1;
    View.Phase = ELDMatchPhase::Development;
    View.ServerNowMs = NowMs;
    View.StartedAtMs = State.Rules.StartedAtMs;
    View.OpenAtMs = 1000000;
    View.Boss = State.View();
    return View;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBossRulesTest, "Lockdown.Rules.DemoBoss.Rules",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDBossRulesTest::RunTest(const FString& Parameters)
{
    using namespace LDBossTests;
    FLDBossState State = MakeState();
    if (!TestTrue(TEXT("The test rules are valid"), State.IsActive())) { return false; }
    const int64 Appear = 21000;
    TestEqual(TEXT("The Boss appears one appear_at after the match start"), State.NextDueMs(), Appear);
    TestFalse(TEXT("It is not due before its time"), State.IsSpawnDue(Appear - 1));
    const FLDCombatBodyRef Self = BossBody(State);
    TestFalse(TEXT("An early spawn is refused"), State.Spawned(Self, Appear - 1));
    TestFalse(TEXT("A spawn of another entity is refused"), State.Spawned(BodyOf(9, ELDVictimKind::CampMember), Appear));
    TestTrue(TEXT("At its time the Boss spawns"), State.Spawned(Self, Appear) && State.Phase == ELDBossPhase::Alive);
    TestEqual(TEXT("Once alive nothing more is due"), State.NextDueMs(), MAX_int64);
    TestFalse(TEXT("It never spawns twice"), State.Spawned(Self, Appear + 1));

    // Patrol: the four waypoints in order, looping.
    const FVector Home = State.Rules.Waypoints[0];
    FLDBossDecision Decision = State.Decide(Appear, Home, {}, true, true);
    TestTrue(TEXT("Alone it patrols toward the first waypoint"), Decision.Intent == ELDBossIntent::Patrol && Decision.GoalFootCm == Home);
    for (int32 Step = 0; Step < 4; ++Step) { State.ReachedWaypoint(); }
    TestTrue(TEXT("After the fourth waypoint it loops back to the first"), State.PatrolGoal() == Home);
    State.ReachedWaypoint();
    TestTrue(TEXT("Then the second"), State.PatrolGoal() == State.Rules.Waypoints[1]);
    TestTrue(TEXT("Controlled, it does not walk"), State.Decide(Appear, Home, {}, false, true).Intent == ELDBossIntent::None);

    // Target: the nearest visible contender within aggro, never one outside it or unseen.
    const FVector Near = Home + FVector(250, 0, 0), Close = Home + FVector(150, 0, 0), Far = Home + FVector(600, 0, 0);
    Decision = State.Decide(Appear, Home, {Seen(1, Far)}, true, true);
    TestTrue(TEXT("A contender beyond aggro is ignored"), Decision.Intent == ELDBossIntent::Patrol);
    Decision = State.Decide(Appear, Home, {Seen(1, Close, false)}, true, true);
    TestTrue(TEXT("An unseen contender is ignored"), Decision.Intent == ELDBossIntent::Patrol);
    Decision = State.Decide(Appear, Home, {Seen(1, Near)}, true, true);
    TestTrue(TEXT("Inside aggro but out of reach it chases"), Decision.Intent == ELDBossIntent::Chase && Decision.Target == BodyOf(1)
        && Decision.GoalFootCm == Near);
    Decision = State.Decide(Appear, Home, {Seen(1, Near), Seen(2, Close)}, true, true);
    TestTrue(TEXT("The nearest contender in reach is attacked"), Decision.Intent == ELDBossIntent::Attack && Decision.Target == BodyOf(2));
    Decision = State.Decide(Appear, Home, {Seen(2, Close)}, true, false);
    TestTrue(TEXT("In reach while cooling down it holds"), Decision.Intent == ELDBossIntent::None && Decision.Target == BodyOf(2));
    Decision = State.Decide(Appear, Home, {Seen(5, Close), Seen(4, Home + FVector(0, 150, 0))}, true, true);
    TestTrue(TEXT("Equal distance is decided by contender id, not order"), Decision.Target == BodyOf(4));

    // Attackers come first, from anywhere they can be seen, until the memory runs out.
    State.NoteAttack(BodyOf(7), Appear + 1000);
    State.NoteAttack(BodyOf(8, ELDVictimKind::CampMember), Appear + 1000);
    Decision = State.Decide(Appear + 2000, Home, {Seen(2, Close), Seen(7, Far)}, true, true);
    TestTrue(TEXT("A recent attacker beyond aggro is chased before a nearer contender"), Decision.Intent == ELDBossIntent::Chase
        && Decision.Target == BodyOf(7));
    TestEqual(TEXT("Only a contender's contact is remembered"), State.Attackers.Num(), 1);
    State.NoteAttack(BodyOf(7), Appear + 3000);
    Decision = State.Decide(Appear + 6500, Home, {Seen(2, Close), Seen(7, Far)}, true, true);
    TestTrue(TEXT("A later hit refreshes the memory"), Decision.Target == BodyOf(7));
    Decision = State.Decide(Appear + 7001, Home, {Seen(2, Close), Seen(7, Far)}, true, true);
    TestTrue(TEXT("Once the memory runs out the nearest contender in aggro is the target again"), Decision.Target == BodyOf(2)
        && State.Attackers.IsEmpty());

    // The public view follows the phase; the death happens once.
    FString Error;
    FLDBossState Pending = MakeState();
    FLDPublicMatchView View = ViewAt(Pending, Appear - 5000);
    TestTrue(TEXT("A pending Boss is a valid public view: ") + Error, LDCapture::ValidatePublicMatchView(View, Error));
    View = ViewAt(State, Appear + 100);
    TestTrue(TEXT("A living Boss is a valid public view: ") + Error, LDCapture::ValidatePublicMatchView(View, Error));
    View.Boss.KilledBy = BodyOf(1).Victim.EntityId;
    TestFalse(TEXT("A living Boss names no killer"), LDCapture::ValidatePublicMatchView(View, Error));
    TestFalse(TEXT("Another body's death does not count"), State.Died(BodyOf(9, ELDVictimKind::CampMember), FGuid(1, 0, 0, 0), Appear + 9000));
    TestTrue(TEXT("The Boss dies once"), State.Died(Self, FGuid(1, 0, 0, 0), Appear + 9000) && State.Phase == ELDBossPhase::Down
        && State.KilledBy == FGuid(1, 0, 0, 0) && State.DownAtMs == Appear + 9000);
    TestFalse(TEXT("A second death is refused"), State.Died(Self, FGuid(2, 0, 0, 0), Appear + 9100));
    TestFalse(TEXT("Once down it never spawns again"), State.IsSpawnDue(Appear + 100000) || State.Spawned(Self, Appear + 100000));
    TestTrue(TEXT("A down Boss decides nothing"), State.Decide(Appear + 9500, Home, {Seen(2, Close)}, true, true).Intent == ELDBossIntent::None);
    View = ViewAt(State, Appear + 9000);
    TestTrue(TEXT("A down Boss is a valid public view: ") + Error, LDCapture::ValidatePublicMatchView(View, Error));
    View = ViewAt(State, Appear + 8999);
    TestFalse(TEXT("A death cannot be from the future"), LDCapture::ValidatePublicMatchView(View, Error));
    View = ViewAt(State, Appear + 9000);
    View.Phase = ELDMatchPhase::Lobby;
    View.ServerNowMs = View.StartedAtMs = View.OpenAtMs = 0;
    TestFalse(TEXT("A room outside a match publishes no Boss"), LDCapture::ValidatePublicMatchView(View, Error));

    FLDBossState Invalid = MakeState();
    Invalid.Rules.AggroRadiusCm = Invalid.Rules.AttackRangeCm;
    TestFalse(TEXT("An aggro radius inside its reach is not valid"), Invalid.IsActive());
    Invalid = MakeState();
    Invalid.Rules.Waypoints.Pop();
    TestFalse(TEXT("Three waypoints are not valid"), Invalid.IsActive() || Invalid.IsSpawnDue(Appear));
    Invalid = MakeState();
    Invalid.Rules.Waypoints[2].X = std::numeric_limits<double>::quiet_NaN();
    TestFalse(TEXT("A waypoint must be a number"), Invalid.IsActive());
    return true;
}
// D26 (#79) 画饼老板 reward: the last hit earns the approved monster_dean DeathReward and the 吃到大饼 buff. The buff is
// granted only in DeathsAndRewards and only to a living contender body; it raises AD, AP and move speed by the
// approved ratios and expires after the approved duration.
namespace LDBossRewardTests
{
bool ReadApprovedFixture(FAutomationTestBase& Test, TSharedPtr<const FLDDemoContentSnapshot>& Out)
{
    FString Root, Error, Text;
    if (!FParse::Value(FCommandLine::Get(), TEXT("LDDemoContentFixtures="), Root) || Root.IsEmpty()
        || !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Root),
            FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), TEXT("Artifacts"))))
    {
        Test.AddError(TEXT("The Boss reward test requires isolated LDDemoContentFixtures with approved_base; no live-data fallback."));
        return false;
    }
    TSharedPtr<FJsonObject> Index;
    const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
    if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(Root, TEXT("index.json")))
        || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Index) || !Index
        || !Index->TryGetArrayField(TEXT("valid"), Rows))
    {
        Test.AddError(TEXT("Unreadable content fixture index."));
        return false;
    }
    FLDDemoContentDigest Digest;
    bool bFound = false;
    for (const TSharedPtr<FJsonValue>& Value : *Rows)
    {
        const TSharedPtr<FJsonObject>* Row = nullptr;
        FString Name, Hex;
        if (!Value->TryGetObject(Row) || !(*Row)->TryGetStringField(TEXT("name"), Name) || Name != TEXT("approved_base")) { continue; }
        if (bFound || !(*Row)->TryGetStringField(TEXT("catalog_sha256"), Hex) || !FLDDemoContentDigest::ParseHex(Hex, Digest, Error))
        {
            Test.AddError(TEXT("Duplicate or invalid approved_base fixture."));
            return false;
        }
        bFound = true;
    }
    FLDDemoContentBundle Bundle;
    TUniquePtr<FLDDemoPreparedContent> Prepared;
    auto* Asset = NewObject<ULDDemoContentCatalog>(GetTransientPackage());
    if (!bFound || !FLDDemoContentBundle::ReadImportFiles(FPaths::Combine(Root, TEXT("approved_base")), Bundle, Error)
        || !ULDDemoContentCatalog::PrepareImport(*Asset, Bundle, Digest, Prepared, Error)
        || !Asset->CommitImport(Prepared, Digest, Error) || !Asset->Read(Digest, Out, Error))
    {
        Test.AddError(bFound ? Error : FString(TEXT("Missing approved_base fixture.")));
        return false;
    }
    return true;
}

bool RatioIs(const FLDDemoRational& Value, int64 Numerator, int64 Denominator)
{
    return Value.Denominator > 0 && Value.Numerator * Denominator == Numerator * Value.Denominator;
}

int64 StatOf(const TArray<FLDDemoStatBonus>& Stats, ELDDemoBonusStat Stat)
{
    const FLDDemoStatBonus* Found = Stats.FindByPredicate([Stat](const FLDDemoStatBonus& Bonus) { return Bonus.Stat == Stat; });
    return Found ? Found->Amount : -1;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBossRewardTest, "Lockdown.Rules.DemoBoss.Reward",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDBossRewardTest::RunTest(const FString& Parameters)
{
    using namespace LDBossRewardTests;
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!ReadApprovedFixture(*this, Catalog)) { return false; }
    const FLDDemoContentDefinitions& D = Catalog->Get();
    const FLDDemoBossDefinition& Rules = D.Boss;

    // 1. The approved values (#79 "已批准决定"; gold removed in #144): 300 流量 through the monster_dean DeathReward, and 吃到大饼 =
    //    AD +25%, AP +25%, move speed +10% for 90 s.
    const FLDDemoMonsterDefinition* Row = D.Monsters.FindByPredicate(
        [&Rules](const FLDDemoMonsterDefinition& M) { return M.Id == Rules.Id; });
    bool bGood = TestTrue(TEXT("The Boss is the monster_dean row of the Boss tier"),
        Rules.Id == TEXT("monster_dean") && Row && Row->Tier == ELDDemoCampTier::Boss);
    LDProgression::FRewardAmounts Amounts;
    bGood &= TestTrue(TEXT("The last hit's DeathReward is the approved 300 XP"),
        LDProgression::MonsterKillReward(*Catalog, FName(*Rules.Id), 0, Amounts) == ELDGrowthReason::None
        && Amounts.XP == 300);
    bGood &= TestTrue(TEXT("The Boss is not a camp and never takes a camp growth stage"),
        LDProgression::MonsterKillReward(*Catalog, FName(*Rules.Id), 1, Amounts) == ELDGrowthReason::DataInvalid
        && Amounts.XP == 0);
    bGood &= TestTrue(TEXT("The approved buff is AD +25%, AP +25% and move speed +10% for 90 s"),
        Rules.ApprovalDurationMs == 90000 && Rules.ApprovalStats.Num() == 3
        && StatOf(Rules.ApprovalStats, ELDDemoBonusStat::ADBonusRatio) == 2500
        && StatOf(Rules.ApprovalStats, ELDDemoBonusStat::APBonusRatio) == 2500
        && StatOf(Rules.ApprovalStats, ELDDemoBonusStat::MoveSpeedBonusRatio) == 1000);

    // 2. The authority grant, in a real DeathsAndRewards boundary.
    const FLDAttributeProjectionResult Projected = LDAttributeProjection::Project(FLDAttributeBuildValues(1, {}, {}), *Catalog);
    if (!TestTrue(TEXT("A level-1 contender projects"), Projected.IsSuccess())) { return false; }
    const FLDAttributeProjection& Base = Projected.Value.GetValue();
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    if (!TestNotNull(TEXT("An isolated reward world"), World)) { return false; }
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Mode = World->SpawnActor<ALDGameMode>();
    auto* Public = World->SpawnActor<ALDGameState>();
    auto* Driver = World->SpawnActor<ALDCombatDirector>();
    ALDCharacter* Killer = World->SpawnActor<ALDCharacter>();
    ALDCharacter* Fallen = World->SpawnActor<ALDCharacter>();
    ALDCharacter* Guard = World->SpawnActor<ALDCharacter>();
    if (!Mode || !Public || !Driver || !Killer || !Fallen || !Guard) { AddError(TEXT("Fixture actors failed to spawn")); return false; }
    // Combat reaches its authority through UWorld::GetAuthGameMode; a bare test world has none, so the spawned mode is
    // registered through the reflected engine property instead of a full GameInstance boot.
    FObjectPropertyBase* AuthorityProperty = FindFProperty<FObjectPropertyBase>(UWorld::StaticClass(), TEXT("AuthorityGameMode"));
    if (!TestNotNull(TEXT("The engine exposes the world's authority game mode"), AuthorityProperty)) { return false; }
    AuthorityProperty->SetObjectPropertyValue_InContainer(World, Mode);
    if (!TestTrue(TEXT("Combat sees the test authority"), World->GetAuthGameMode() == Mode)) { return false; }
    Mode->bDemoWorld = Mode->bEntryAccepted = Mode->bAuthorityInitialized = true;
    Mode->GameState = Public;
    Mode->MatchContext = {1, FGuid::NewGuid()};
    Mode->MatchPhase = ELDMatchPhase::Development;
    Mode->Combat = Driver;
    Mode->DemoContent = Catalog;
    Mode->AuthorityStep = {Mode->MatchContext, 1, 0, 0};
    const auto Install = [&](ALDCharacter* Actor, ELDVictimKind Kind, bool bDead)
    {
        FLDBodyCombatState State;
        State.Identity = FGuid::NewGuid();
        State.Victim = {Kind, FGuid::NewGuid(), 1};
        State.Resources.MaxHpP2 = State.Resources.HpP2 = 10000;
        State.Resources.Revision = 1;
        State.BuildProjection = State.AttributeProjection = Base;
        State.bPendingDeath = bDead;
        const FLDCombatBodyRef Ref{State.Victim, State.Identity};
        Driver->Bodies.Add(Actor, MoveTemp(State));
        return Ref;
    };
    const FLDCombatBodyRef KillerRef = Install(Killer, ELDVictimKind::Contender, false);
    const FLDCombatBodyRef FallenRef = Install(Fallen, ELDVictimKind::Contender, true);
    const FLDCombatBodyRef GuardRef = Install(Guard, ELDVictimKind::CampMember, false);
    World->Tick(LEVELTICK_TimeOnly, .025f);

    const FName Source(*Rules.Id);
    FString Error, GrantError;
    bool bKiller = false, bFallen = true, bGuard = true, bLate = true, bLateOk = true, bRan = true;
    // Inside the 25 ms the isolated world has ticked, like the other authority-boundary rule tests.
    const int64 GrantAtMs = 10;
    bRan = Mode->BeginAuthorityStep(*Driver, Mode->MatchContext, GrantAtMs) == ELDCommitReject::None
        && Mode->ExecuteAuthorityPhase(*Driver, ELDCommitPhase::Motion, [](const auto&) { return true; }, Error)
        && Mode->ExecuteAuthorityPhase(*Driver, ELDCommitPhase::DeathsAndRewards, [&](const FLDGameplayWritePermit& Permit)
            {
                return Driver->GrantTimedBonus(KillerRef, Source, Rules.ApprovalStats, Rules.ApprovalDurationMs, Permit, bKiller, Error)
                    && Driver->GrantTimedBonus(FallenRef, Source, Rules.ApprovalStats, Rules.ApprovalDurationMs, Permit, bFallen, Error)
                    && Driver->GrantTimedBonus(GuardRef, Source, Rules.ApprovalStats, Rules.ApprovalDurationMs, Permit, bGuard, Error);
            }, Error)
        && Mode->ExecuteAuthorityPhase(*Driver, ELDCommitPhase::Transactions, [&](const FLDGameplayWritePermit& Permit)
            {
                // Outside DeathsAndRewards the grant is refused as an authority fault, and nothing changes.
                bLateOk = Driver->GrantTimedBonus(KillerRef, Source, Rules.ApprovalStats, Rules.ApprovalDurationMs, Permit, bLate,
                    GrantError);
                return true;
            }, Error)
        && Mode->FinishAuthorityStep(*Driver, Error);
    if (!TestTrue(FString::Printf(TEXT("The authority boundary ran (%s)"), *Error), bRan)) { return false; }
    bGood &= TestTrue(TEXT("The grant phase itself reported no fault"), Error.IsEmpty());
    const FLDBodyCombatState& K = Driver->Bodies.FindChecked(Killer);
    const FLDBodyCombatState& F = Driver->Bodies.FindChecked(Fallen);
    const FLDBodyCombatState& G = Driver->Bodies.FindChecked(Guard);
    bGood &= TestTrue(TEXT("The living last hitter receives the buff once, from the Boss, for exactly 90 s"),
        bKiller && K.TimedBonuses.Num() == 1 && K.TimedBonuses[0].SourceDefinition == Source
        && K.TimedBonuses[0].ExpiresAtMs == GrantAtMs + Rules.ApprovalDurationMs);
    const auto& C = K.AttributeProjection.Contributions;
    bGood &= TestTrue(TEXT("It raises AD and AP by 25% and move speed by 10% through the temporary terms"),
        RatioIs(C[int32(ELDProjectedAttribute::AD)].Temporary.AdditiveRatio, 1, 4)
        && RatioIs(C[int32(ELDProjectedAttribute::AP)].Temporary.AdditiveRatio, 1, 4)
        && RatioIs(C[int32(ELDProjectedAttribute::MoveSpeed)].Temporary.AdditiveRatio, 1, 10)
        && K.AttributeProjection.Final.ADP2 == Base.Final.ADP2 * 12500 / 10000);
    bGood &= TestTrue(TEXT("A last hitter already dead gets nothing"), !bFallen && F.TimedBonuses.IsEmpty()
        && F.AttributeProjection.Final.ADP2 == Base.Final.ADP2);
    bGood &= TestTrue(TEXT("A camp body is never approved"), !bGuard && G.TimedBonuses.IsEmpty());
    bGood &= TestTrue(TEXT("Outside DeathsAndRewards the grant is refused and adds nothing"),
        !bLateOk && !bLate && !GrantError.IsEmpty() && K.TimedBonuses.Num() == 1);

    // 3. Expiry: present until its end, gone at it.
    const FLDTemporaryAttributeTerms Before = LDTactical::TemporaryTerms(K.TimedBonuses, {}, {}, K.Resources.HpP2,
        K.Resources.MaxHpP2, GrantAtMs + Rules.ApprovalDurationMs - 1);
    const FLDTemporaryAttributeTerms After = LDTactical::TemporaryTerms(K.TimedBonuses, {}, {}, K.Resources.HpP2,
        K.Resources.MaxHpP2, GrantAtMs + Rules.ApprovalDurationMs);
    bGood &= TestTrue(TEXT("The buff holds until its 90 s run out and is gone at the end"),
        Before.Bonuses.Num() == Rules.ApprovalStats.Num() && After.Bonuses.IsEmpty());
    return bGood;
}
#endif
