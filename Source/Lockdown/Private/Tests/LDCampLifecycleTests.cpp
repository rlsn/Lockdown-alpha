#include "World/LDCampLifecycle.h"
#include "Development/LDDemoContentIds.h"
#include "Region/LDMapGeometry.h"
#include "Vision/LDViewTypes.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace LDCampLifecycleTests
{
FLDDemoRational Cm(int64 Value) { FLDDemoRational Out; Out.Numerator = Value; Out.Denominator = 1; return Out; }

FLDDemoWorldPoint Point(int64 X, int64 Y)
{
    FLDDemoWorldPoint Out;
    Out.X = Cm(X); Out.Y = Cm(Y); Out.Z = Cm(0);
    return Out;
}

struct FFixture
{
    FLDDemoContentDefinitions Content;
    FLDDemoWorldGeometry World;
};

FFixture MakeFixture()
{
    FFixture F;
    FLDDemoMonsterDefinition Low;
    Low.Id = TEXT("monster_scavenger"); Low.NativeId = uint8(ELDDemoMonsterId::MonsterScavenger);
    Low.Tier = ELDDemoCampTier::Low; Low.CountPerCamp = 2; Low.RespawnMs = 40000;
    FLDDemoMonsterDefinition High;
    High.Id = TEXT("monster_guardian"); High.NativeId = uint8(ELDDemoMonsterId::MonsterGuardian);
    High.Tier = ELDDemoCampTier::High; High.CountPerCamp = 1; High.RespawnMs = 65000;
    F.Content.Monsters = {Low, High};
    const TCHAR* Ids[] = {TEXT("l1"), TEXT("h1"), TEXT("l2"), TEXT("h2"), TEXT("l3")};
    const int64 Xy[5][2] = {{2473, 803}, {0, 2600}, {-2473, 803}, {-1528, -2103}, {1528, -2103}};
    for (int32 I = 0; I < 5; ++I)
    {
        const bool bHigh = Ids[I][0] == TEXT('h');
        F.Content.World.CampIds.Add(Ids[I]);
        FLDDemoCampPlacement Placement;
        Placement.AnchorId = Ids[I];
        Placement.MonsterId = bHigh ? High.Id : Low.Id;
        F.Content.Camps.Add(Placement);
        FLDDemoWorldAnchor Anchor;
        Anchor.Id = Ids[I]; Anchor.Kind = ELDDemoWorldAnchorKind::Camp; Anchor.SurfaceId = TEXT("ground");
        Anchor.PositionCm = Point(Xy[I][0], Xy[I][1]);
        for (int32 Slot = 0; Slot < (bHigh ? 1 : 2); ++Slot)
        {
            FLDDemoWorldMemberHome Home;
            Home.Id = FString::Printf(TEXT("%s_%d"), Ids[I], Slot);
            Home.PositionCm = Point(Xy[I][0] + 75 * Slot, Xy[I][1]);
            Anchor.Members.Add(Home);
        }
        F.World.Anchors.Add(Anchor);
    }
    return F;
}

TSharedRef<const FLDCampRoster> Roster(FAutomationTestBase& Test)
{
    const FFixture F = MakeFixture();
    FLDCampRoster Out;
    FString Error;
    Test.TestTrue(TEXT("Approved fixture roster builds"), LDCamp::BuildRoster(F.Content, F.World, Out, Error));
    return MakeShared<const FLDCampRoster>(MoveTemp(Out));
}

FLDCombatBodyRef Body(const FGuid& Entity, uint32 Life, uint32 Salt = 0)
{
    return {{ELDVictimKind::CampMember, Entity, Life}, FGuid(Life, Salt, Entity.A, Entity.B)};
}

FLDCombatBodyRef Contender(uint32 Id, uint32 Life = 1)
{
    return {{ELDVictimKind::Contender, FGuid(Id, 0, 0, 0), Life}, FGuid(Id, Life, 7, 7)};
}

TArray<FLDCombatBodyRef> Spawned(const FLDCampMatchState& State, int32 Camp, uint32 Salt = 0)
{
    TArray<FLDCombatBodyRef> Out;
    for (const auto& Member : State.FindCamp(Camp)->Members) { Out.Add(Body(Member.EntityId, Member.Life + 1, Salt)); }
    return Out;
}

FLDCampDecision Brain(ELDCampBrainPhase Phase)
{
    FLDCampDecision Out;
    Out.NextBrain.Phase = Phase;
    return Out;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCampRosterTest, "Lockdown.Rules.DemoCamp.Roster",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCampRosterTest::RunTest(const FString& Parameters)
{
    using namespace LDCampLifecycleTests;
    FString Error;
    FLDCampRoster Out;
    const FFixture Good = MakeFixture();
    if (!TestTrue(TEXT("Five approved camps compile"), LDCamp::BuildRoster(Good.Content, Good.World, Out, Error))) { return false; }
    TestEqual(TEXT("Five camps in content order"), Out.Camps.Num(), 5);
    TestEqual(TEXT("Low camp keeps both member homes"), Out.Camps[0].Members.Num(), 2);
    TestEqual(TEXT("High camp keeps its single member home"), Out.Camps[1].Members.Num(), 1);
    TestEqual(TEXT("Native creature identity is the approved monster"), Out.Camps[1].CreatureId, uint8(ELDDemoMonsterId::MonsterGuardian));
    TestEqual(TEXT("Refresh delay comes from the camp tier definition"), Out.Camps[0].RespawnMs, int64(40000));
    TestEqual(TEXT("Anchor coordinate is exact"), Out.Camps[3].AnchorCm, FVector(-1528, -2103, 0));
    TestEqual(TEXT("Lookup by stable camp id"), Out.FindCamp(TEXT("h2")), 3);
    const FLDCampRoster Before = Out;
    auto Reject = [&](const TCHAR* Name, TFunctionRef<void(FFixture&)> Mutate)
    {
        FFixture Bad = MakeFixture();
        Mutate(Bad);
        Error.Reset();
        TestFalse(Name, LDCamp::BuildRoster(Bad.Content, Bad.World, Out, Error));
        TestTrue(TEXT("Rejection is explicit and leaves the previous roster"), !Error.IsEmpty() && Out.Camps.Num() == Before.Camps.Num());
    };
    Reject(TEXT("Missing placement refused"), [](FFixture& F) { F.Content.Camps.RemoveAt(2); });
    Reject(TEXT("Unknown monster refused"), [](FFixture& F) { F.Content.Camps[0].MonsterId = TEXT("monster_unknown"); });
    Reject(TEXT("Missing world anchor refused"), [](FFixture& F) { F.World.Anchors.RemoveAt(4); });
    Reject(TEXT("Wrong anchor kind refused"), [](FFixture& F) { F.World.Anchors[0].Kind = ELDDemoWorldAnchorKind::Spawn; });
    Reject(TEXT("Member count must match the definition"), [](FFixture& F) { F.World.Anchors[0].Members.RemoveAt(1); });
    Reject(TEXT("Repeated member home refused"), [](FFixture& F) { F.World.Anchors[2].Members[0].Id = TEXT("l1_0"); });
    Reject(TEXT("Repeated camp refused"), [](FFixture& F) { F.Content.World.CampIds[4] = TEXT("l1"); });
    Reject(TEXT("Both tiers are required"), [](FFixture& F)
    {
        for (auto& Camp : F.Content.Camps) { Camp.MonsterId = TEXT("monster_scavenger"); }
        for (auto& Anchor : F.World.Anchors)
        {
            if (Anchor.Members.Num() != 1) { continue; }
            FLDDemoWorldMemberHome Extra = Anchor.Members[0];
            Extra.Id += TEXT("x");
            Anchor.Members.Add(MoveTemp(Extra));
        }
    });
    Reject(TEXT("Unplaced extra placement refused"), [](FFixture& F)
    {
        FLDDemoCampPlacement Extra = F.Content.Camps[0];
        Extra.AnchorId = TEXT("x9");
        F.Content.Camps.Add(MoveTemp(Extra));
    });
    Reject(TEXT("Nonpositive refresh delay refused"), [](FFixture& F) { F.Content.Monsters[1].RespawnMs = 0; });
    Reject(TEXT("Invalid coordinate denominator refused"), [](FFixture& F) { F.World.Anchors[1].PositionCm.X.Denominator = 0; });
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCampLifecycleTest, "Lockdown.Rules.DemoCamp.Lifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCampLifecycleTest::RunTest(const FString& Parameters)
{
    using namespace LDCampLifecycleTests;
    FString Error;
    FLDCampMatchState State;
    const FLDMatchContext Match{3, FGuid(9, 9, 9, 9)};
    if (!TestTrue(TEXT("Match begins"), State.Begin(Match, Roster(*this), Error))) { return false; }
    TestFalse(TEXT("An active match cannot be silently replaced"), State.Begin({4, FGuid(8, 8, 8, 8)}, Roster(*this), Error));
    TSet<FGuid> Entities;
    for (int32 Camp = 0; Camp < State.NumCamps(); ++Camp)
    {
        for (const auto& Member : State.FindCamp(Camp)->Members) { Entities.Add(Member.EntityId); }
    }
    TestEqual(TEXT("Eight fixed members get distinct match-local entity ids"), Entities.Num(), 8);
    const FGuid FirstEntity = State.FindCamp(0)->Members[0].EntityId;

    TestTrue(TEXT("Unspawned camp can start"), State.CanSpawnInitial(0, Error));
    TArray<FLDCombatBodyRef> Bodies = Spawned(State, 0);
    Bodies[1].Victim.Life = 2;
    TestFalse(TEXT("A skipped core life is refused"), State.CommitSpawn(0, Bodies, 0, Error));
    Bodies = Spawned(State, 0);
    Bodies[1].BodyIdentity = Bodies[0].BodyIdentity;
    TestFalse(TEXT("One physical body cannot serve two slots"), State.CommitSpawn(0, Bodies, 0, Error));
    TestFalse(TEXT("Half a group cannot spawn"), State.CommitSpawn(0, MakeArrayView(Spawned(State, 0).GetData(), 1), 0, Error));
    TestTrue(TEXT("The whole group commits at once"), State.CommitSpawn(0, Spawned(State, 0), 0, Error));
    TestFalse(TEXT("An occupied camp cannot spawn again"), State.CanSpawnInitial(0, Error));
    TestFalse(TEXT("A living camp cannot respawn"), State.CommitSpawn(0, Spawned(State, 0), 1, Error));
    const FLDCombatBodyRef A = State.FindCamp(0)->Members[0].Body, B = State.FindCamp(0)->Members[1].Body;
    TestTrue(TEXT("Both members alive on life one"), A.Victim.Life == 1 && State.FindLiving(A) && State.FindLiving(B));
    TestEqual(TEXT("No refresh deadline exists before any camp clears"), State.NextRespawnDueMs(), MAX_int64);

    TestEqual(TEXT("Death in another match is refused"),
        State.RecordDeath({FGuid(1, 1, 1, 1), A.Victim}, 5000, Error), ELDCampDeathResult::Rejected);
    TestEqual(TEXT("An unknown future life is refused"),
        State.RecordDeath({Match.LedgerMatch, {ELDVictimKind::CampMember, A.Victim.EntityId, 2}}, 5000, Error), ELDCampDeathResult::Rejected);
    TestEqual(TEXT("A negative death time is refused"),
        State.RecordDeath({Match.LedgerMatch, A.Victim}, -1, Error), ELDCampDeathResult::Rejected);
    TestEqual(TEXT("First low member death keeps the camp occupied"),
        State.RecordDeath({Match.LedgerMatch, A.Victim}, 5000, Error), ELDCampDeathResult::MemberDead);
    TestEqual(TEXT("Repeated death of the same life is a no-op"),
        State.RecordDeath({Match.LedgerMatch, A.Victim}, 6000, Error), ELDCampDeathResult::Duplicate);
    TestFalse(TEXT("No refresh while a member lives"), State.IsRespawnDue(0, 1000000));
    TestEqual(TEXT("A partly dead camp sets no refresh deadline"), State.NextRespawnDueMs(), MAX_int64);
    TestTrue(TEXT("Dead member no longer resolves"), !State.FindLiving(A) && State.FindMember(FirstEntity)->Life == 1);

    FLDCampDecision Returning = Brain(ELDCampBrainPhase::Returning);
    bool bBegan = false;
    FLDCampResetKey Key;
    TestTrue(TEXT("Survivor starts returning"), State.ApplyDecision(B, Returning, bBegan, Error) && bBegan);
    TestTrue(TEXT("Survivor reset key"), State.ReadResetKey(B, Key, Error));
    TestTrue(TEXT("Survivor resets at home"), State.CompleteReset(Key, 8, Error));
    TestTrue(TEXT("Reset keeps the same life and body; it is not a new life"),
        State.FindLiving(B) && State.FindLiving(B)->Life == 1 && !State.FindCamp(0)->bRespawnPending);
    TestFalse(TEXT("Reset cannot revive the dead member"), State.FindLiving(A) != nullptr);

    TestEqual(TEXT("Last member death clears the camp"),
        State.RecordDeath({Match.LedgerMatch, B.Victim}, 10000, Error), ELDCampDeathResult::CampCleared);
    const FLDCampGroupState* Group = State.FindCamp(0);
    TestTrue(TEXT("Refresh is timed from the last death, not the spawn"),
        Group->bRespawnPending && Group->ClearedAtMs == 10000 && Group->RespawnDueMs == 50000);
    TestFalse(TEXT("Refresh waits for its deadline"), State.IsRespawnDue(0, 49999));
    TestTrue(TEXT("Refresh is due at its deadline"), State.IsRespawnDue(0, 50000));
    TestEqual(TEXT("The driver's next refresh boundary is the cleared camp's deadline"), State.NextRespawnDueMs(), int64(50000));
    TestEqual(TEXT("A cleared camp does not rearm on repeated deaths"),
        State.RecordDeath({Match.LedgerMatch, B.Victim}, 11000, Error), ELDCampDeathResult::Duplicate);

    TestTrue(TEXT("High camp spawns"), State.CommitSpawn(1, Spawned(State, 1), 0, Error));
    TestEqual(TEXT("High camp clears with its single member"),
        State.RecordDeath({Match.LedgerMatch, State.FindCamp(1)->Members[0].Body.Victim}, 1001, Error), ELDCampDeathResult::CampCleared);
    TestTrue(TEXT("Clear and due times are the committed death millisecond plus the tier delay"),
        State.FindCamp(1)->ClearedAtMs == 1001 && State.FindCamp(1)->RespawnDueMs == 66001);
    TestEqual(TEXT("The earliest of several pending deadlines bounds the next step"), State.NextRespawnDueMs(), int64(50000));

    TestTrue(TEXT("Group refresh commits the next life for every slot"), State.CommitSpawn(0, Spawned(State, 0, 1), 50, Error));
    TestEqual(TEXT("A committed refresh leaves only the other pending deadline"), State.NextRespawnDueMs(), int64(66001));
    Group = State.FindCamp(0);
    TestTrue(TEXT("Second generation lives"), Group->Generation == 2 && Group->Members[0].Life == 2 && Group->Members[1].Life == 2
        && !Group->bRespawnPending && Group->Members[0].EntityId == FirstEntity);
    TestEqual(TEXT("An old life's death cannot touch the refreshed member"),
        State.RecordDeath({Match.LedgerMatch, A.Victim}, 51000, Error), ELDCampDeathResult::Duplicate);
    TestTrue(TEXT("Refreshed member still alive"), State.FindLiving(Group->Members[0].Body) != nullptr);

    const auto Views = State.BuildPublicViews();
    TestTrue(TEXT("Public camp views carry only fixed facts"), Views.Num() == 5 && Views[1].MonsterId == TEXT("monster_guardian")
        && Views[1].Tier == ELDDemoCampTier::High && Views[1].AnchorCm == FVector2D(0, 2600));
    State.End();
    TestTrue(TEXT("New match after End allocates new identities"), State.Begin({4, FGuid(8, 8, 8, 8)}, Roster(*this), Error)
        && State.FindMember(FirstEntity) == nullptr && State.FindCamp(0)->Generation == 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCampContactTest, "Lockdown.Rules.DemoCamp.Retaliation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCampContactTest::RunTest(const FString& Parameters)
{
    using namespace LDCampLifecycleTests;
    FString Error;
    FLDCampMatchState State;
    const FLDMatchContext Match{3, FGuid(9, 9, 9, 9)};
    if (!State.Begin(Match, Roster(*this), Error) || !State.CommitSpawn(0, Spawned(State, 0), 1, Error)
        || !State.CommitSpawn(2, Spawned(State, 2), 1, Error))
    {
        AddError(Error);
        return false;
    }
    const FLDCombatBodyRef A = State.FindCamp(0)->Members[0].Body, B = State.FindCamp(0)->Members[1].Body;
    const FLDCombatBodyRef Neighbor = State.FindCamp(2)->Members[0].Body;
    auto Phase = [&](const FLDCombatBodyRef& Member) { return State.FindLiving(Member)->Brain.Phase; };
    auto Sources = [&](const FLDCombatBodyRef& Member) { return State.FindLiving(Member)->HostileSources; };

    TestEqual(TEXT("A contact on a contender is not camp business"),
        State.ApplyContact({Contender(5), Contender(1), 2}, Error), ELDCampContactResult::NotCampMember);
    TestEqual(TEXT("A contact without a contender source cannot assign blame"),
        State.ApplyContact({A, FLDCombatBodyRef(), 2}, Error), ELDCampContactResult::NoContenderSource);
    TestEqual(TEXT("A camp member cannot be blamed"), State.ApplyContact({A, B, 2}, Error), ELDCampContactResult::NoContenderSource);
    TestEqual(TEXT("A stale victim body is refused"),
        State.ApplyContact({Body(A.Victim.EntityId, 1, 99), Contender(1), 2}, Error), ELDCampContactResult::StaleVictim);
    TestTrue(TEXT("Untouched camp stays idle"), Phase(A) == ELDCampBrainPhase::Idle && Phase(B) == ELDCampBrainPhase::Idle);

    TestEqual(TEXT("A confirmed hit wakes the whole camp"),
        State.ApplyContact({A, Contender(1), 2}, Error), ELDCampContactResult::Retaliating);
    TestTrue(TEXT("Both members engage the same recorded attacker body"), Phase(A) == ELDCampBrainPhase::Engaged
        && Phase(B) == ELDCampBrainPhase::Engaged && Sources(B).Num() == 1 && Sources(B)[0] == Contender(1));
    TestTrue(TEXT("The neighboring camp is not chained"), Phase(Neighbor) == ELDCampBrainPhase::Idle && Sources(Neighbor).IsEmpty());
    TestEqual(TEXT("Duplicate contact is idempotent"),
        State.ApplyContact({A, Contender(1), 3}, Error), ELDCampContactResult::Unchanged);
    FLDCombatBodyRef Twin = Contender(1);
    Twin.BodyIdentity = FGuid(42, 0, 0, 0);
    TestEqual(TEXT("Two bodies for one contender life are refused"),
        State.ApplyContact({B, Twin, 3}, Error), ELDCampContactResult::Rejected);
    TestEqual(TEXT("A second attacker is recorded by both"),
        State.ApplyContact({B, Contender(2), 3}, Error), ELDCampContactResult::Retaliating);
    TestEqual(TEXT("Attacker's later life replaces its dead earlier life"),
        State.ApplyContact({A, Contender(1, 2), 4}, Error), ELDCampContactResult::Retaliating);
    TestTrue(TEXT("Only the current life remains blamed"), Sources(A).Num() == 2 && Sources(A).Contains(Contender(1, 2))
        && !Sources(A).Contains(Contender(1)));
    TestEqual(TEXT("A late effect from the dead earlier life is not re-added"),
        State.ApplyContact({A, Contender(1), 5}, Error), ELDCampContactResult::Unchanged);
    for (uint32 Id = 3; Id <= 6; ++Id) { State.ApplyContact({A, Contender(Id), 5}, Error); }
    TestEqual(TEXT("The bound covers the full contender roster"), Sources(A).Num(), 6);
    TestEqual(TEXT("A seventh contender identity exceeds the roster bound"),
        State.ApplyContact({A, Contender(7), 6}, Error), ELDCampContactResult::Rejected);
    TestTrue(TEXT("Refused contact is all-or-nothing"), Sources(A).Num() == 6 && Sources(B).Num() == 6 && !Error.IsEmpty());

    bool bBegan = false;
    TestTrue(TEXT("Member A starts returning"), State.ApplyDecision(A, Brain(ELDCampBrainPhase::Returning), bBegan, Error) && bBegan);
    TestEqual(TEXT("Returning member ignores hits while its partner still retaliates"),
        State.ApplyContact({A, Contender(1, 2), 7}, Error), ELDCampContactResult::Unchanged);
    TestTrue(TEXT("Returning never turns back"), Phase(A) == ELDCampBrainPhase::Returning);
    FLDCampResetKey Key;
    TestTrue(TEXT("A resets at home"), State.ReadResetKey(A, Key, Error) && State.CompleteReset(Key, 9, Error));
    TestTrue(TEXT("Reset clears aggro"), Phase(A) == ELDCampBrainPhase::Idle && Sources(A).IsEmpty());
    TestEqual(TEXT("Contacts from before the reset cannot reopen aggro"),
        State.ApplyContact({A, Contender(4), 9}, Error), ELDCampContactResult::Unchanged);
    TestTrue(TEXT("Reset member stays idle"), Phase(A) == ELDCampBrainPhase::Idle && Sources(A).IsEmpty());
    TestEqual(TEXT("A later contact wakes the reset member again"),
        State.ApplyContact({B, Contender(4), 10}, Error), ELDCampContactResult::Retaliating);
    TestTrue(TEXT("Group retaliation after reset"), Phase(A) == ELDCampBrainPhase::Engaged && Sources(A) == TArray<FLDCombatBodyRef>{Contender(4)});

    State.RecordDeath({Match.LedgerMatch, B.Victim}, 11000, Error);
    TestEqual(TEXT("A dead victim body no longer accepts contacts"),
        State.ApplyContact({B, Contender(4), 12}, Error), ELDCampContactResult::StaleVictim);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCampResetTest, "Lockdown.Rules.DemoCamp.ReturnReset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCampResetTest::RunTest(const FString& Parameters)
{
    using namespace LDCampLifecycleTests;
    FString Error;
    FLDCampMatchState State;
    const FLDMatchContext Match{3, FGuid(9, 9, 9, 9)};
    if (!State.Begin(Match, Roster(*this), Error) || !State.CommitSpawn(1, Spawned(State, 1), 1, Error))
    {
        AddError(Error);
        return false;
    }
    const FLDCombatBodyRef G = State.FindCamp(1)->Members[0].Body;
    bool bBegan = false;
    FLDCampResetKey Key;
    TestFalse(TEXT("Idle cannot jump to Engaged by decision"), State.ApplyDecision(G, Brain(ELDCampBrainPhase::Engaged), bBegan, Error));
    TestFalse(TEXT("Idle has no reset key"), State.ReadResetKey(G, Key, Error));
    State.ApplyContact({G, Contender(1), 2}, Error);
    FLDCampDecision Lost = Brain(ELDCampBrainPhase::Engaged);
    Lost.NextBrain.LostTargetSinceSeconds = 2.5;
    TestTrue(TEXT("Engaged may record loss time"), State.ApplyDecision(G, Lost, bBegan, Error) && !bBegan);
    TestFalse(TEXT("Engaged cannot drop to Idle without a reset"), State.ApplyDecision(G, Brain(ELDCampBrainPhase::Idle), bBegan, Error));
    FLDCampDecision BadReturn = Brain(ELDCampBrainPhase::Returning);
    BadReturn.NextBrain.LostTargetSinceSeconds = 2.5;
    TestFalse(TEXT("Returning carries no loss history"), State.ApplyDecision(G, BadReturn, bBegan, Error));
    TestTrue(TEXT("Leash or grace begins return epoch one"), State.ApplyDecision(G, Brain(ELDCampBrainPhase::Returning), bBegan, Error)
        && bBegan && State.FindLiving(G)->ReturnEpoch == 1);
    TestTrue(TEXT("Continued return keeps the epoch"), State.ApplyDecision(G, Brain(ELDCampBrainPhase::Returning), bBegan, Error)
        && !bBegan && State.FindLiving(G)->ReturnEpoch == 1);
    TestFalse(TEXT("Returning cannot turn back to Engaged"), State.ApplyDecision(G, Brain(ELDCampBrainPhase::Engaged), bBegan, Error));
    TestTrue(TEXT("Reset key binds match, full life and epoch"), State.ReadResetKey(G, Key, Error)
        && Key.LedgerMatch == Match.LedgerMatch && Key.Victim == G.Victim && Key.ReturnEpoch == 1);
    FLDCampResetKey Wrong = Key;
    Wrong.ReturnEpoch = 2;
    TestFalse(TEXT("Future epoch cannot reset"), State.CompleteReset(Wrong, 4, Error));
    Wrong = Key;
    Wrong.Victim.Life = 2;
    TestFalse(TEXT("Another life cannot reset"), State.CompleteReset(Wrong, 4, Error));
    TestFalse(TEXT("Reset cannot predate the current spawn"), State.CompleteReset(Key, .5, Error));
    TestTrue(TEXT("Arrived member resets"), State.CompleteReset(Key, 4, Error));
    const FLDCampMemberState* Member = State.FindLiving(G);
    TestTrue(TEXT("Reset returns to idle without a new life or body"), Member && Member->Brain.Phase == ELDCampBrainPhase::Idle
        && Member->Life == 1 && Member->Body == G && Member->ResetThroughEpoch == 1 && Member->HostileSources.IsEmpty());
    TestFalse(TEXT("A replayed reset key cannot apply twice"), State.CompleteReset(Key, 5, Error));
    State.ApplyContact({G, Contender(2), 6}, Error);
    TestTrue(TEXT("Next encounter returns with a new epoch"), State.ApplyDecision(G, Brain(ELDCampBrainPhase::Returning), bBegan, Error)
        && bBegan && State.FindLiving(G)->ReturnEpoch == 2);
    TestFalse(TEXT("An old key cannot reset the new return"), State.CompleteReset(Key, 7, Error));
    TestEqual(TEXT("Death while returning is an ordinary single death"),
        State.RecordDeath({Match.LedgerMatch, G.Victim}, 8000, Error), ELDCampDeathResult::CampCleared);
    FLDCampResetKey Current;
    Current = {Match.LedgerMatch, G.Victim, 2};
    TestFalse(TEXT("A dead member cannot be reset"), State.CompleteReset(Current, 9, Error));
    uint64 Sequence = 0;
    TestFalse(TEXT("A dead member issues no action sequence"), State.NextDirectSequence(G, Sequence, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCampObservedFactsTest, "Lockdown.Rules.DemoCamp.ObservedFacts",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDCampObservedFactsTest::RunTest(const FString& Parameters)
{
    using namespace LDCampLifecycleTests;
    FString Error;
    FLDCampMatchState State;
    if (!State.Begin({3, FGuid(9, 9, 9, 9)}, Roster(*this), Error) || !State.CommitSpawn(0, Spawned(State, 0), 1, Error))
    {
        AddError(Error);
        return false;
    }
    const FLDCombatBodyRef A = State.FindCamp(0)->Members[0].Body;
    FLDObservedPublicFacts Facts;
    Facts.Statuses.Add({uint8(ELDDemoStatusId::Root), 1, 2500});
    TestTrue(TEXT("Idle member publishes identity only"), State.AppendObservedFacts(A, Facts, Error)
        && Facts.CreatureId == uint8(ELDDemoMonsterId::MonsterScavenger) && Facts.Statuses.Num() == 1);
    bool bBegan = false;
    State.ApplyContact({A, Contender(1), 2}, Error);
    State.ApplyDecision(A, Brain(ELDCampBrainPhase::Returning), bBegan, Error);
    FLDObservedPublicFacts Returning;
    Returning.Statuses.Add({uint8(ELDDemoStatusId::Root), 1, 2500});
    TestTrue(TEXT("Returning adds the untimed single-stack cue and keeps control state"), State.AppendObservedFacts(A, Returning, Error)
        && Returning.Statuses.Num() == 2 && Returning.Statuses[1].StatusId == uint8(ELDDemoStatusId::CampReturning)
        && Returning.Statuses[1].Stacks == 1 && Returning.Statuses[1].ExpiresAtServerMs == 0);
    TestFalse(TEXT("A second producer of the cue is refused"), State.AppendObservedFacts(A, Returning, Error));
    FLDObservedPublicFacts Conflict;
    Conflict.CreatureId = uint8(ELDDemoMonsterId::MonsterGuardian);
    TestFalse(TEXT("Conflicting creature identity is refused"), State.AppendObservedFacts(A, Conflict, Error));
    FLDObservedPublicFacts Full;
    for (int32 I = 0; I < LDObservation::MaxStatuses; ++I) { Full.Statuses.Add({uint8(ELDDemoStatusId::Shield), 1, 0}); }
    TestFalse(TEXT("The cue never overflows the status bound"), State.AppendObservedFacts(A, Full, Error));
    FLDCampResetKey Key;
    State.ReadResetKey(A, Key, Error);
    State.CompleteReset(Key, 3, Error);
    FLDObservedPublicFacts After;
    TestTrue(TEXT("Leaving Returning withdraws the cue"), State.AppendObservedFacts(A, After, Error) && After.Statuses.IsEmpty());
    State.RecordDeath({FGuid(9, 9, 9, 9), A.Victim}, 4000, Error);
    FLDObservedPublicFacts Dead;
    TestFalse(TEXT("A dead member has no observed facts"), State.AppendObservedFacts(A, Dead, Error));
    return true;
}
#endif
