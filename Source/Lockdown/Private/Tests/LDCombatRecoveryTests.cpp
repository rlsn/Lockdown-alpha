#include "Combat/LDCombatDirector.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "../Development/LDDemoContentJson.h"
#include "Character/LDCharacter.h"
#include "Combat/LDTacticalRules.h"
#include "Development/LDDemoContentCatalog.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "World/LDDormZones.h"

namespace LDCombatRecoveryTestDetail
{
constexpr int64 MicroPerP2 = 1000000;

// Reads the producer-exported approved_base only; there is no live-data fallback.
bool ReadRecoveryCatalog(FAutomationTestBase& Test, TSharedPtr<const FLDDemoContentSnapshot>& Out)
{
    using namespace LDDemoContent;
    FString Root, Error;
    if (!FParse::Value(FCommandLine::Get(), TEXT("LDDemoContentFixtures="), Root) || Root.IsEmpty())
    {
        Test.AddError(TEXT("Missing -LDDemoContentFixtures=<isolated Artifacts directory>; export approved_base first. No live-data fallback."));
        return false;
    }
    if (!Test.TestTrue(TEXT("Recovery fixtures are isolated under this project's Artifacts"),
        FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Root),
            FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), TEXT("Artifacts")))))
    {
        return false;
    }
    FCheck Check(Error);
    TArray<uint8> Bytes;
    FValue IndexValue;
    if (!ReadFile(FPaths::Combine(Root, TEXT("index.json")), Bytes, Error)
        || !Parse(Bytes, IndexValue, Check, TEXT("fixture index")))
    {
        Test.AddError(Error);
        return false;
    }
    FFields Index(Check, IndexValue, TEXT("fixture index"));
    bool bTestOnly = false;
    const TArray<FValue>* Valid = nullptr;
    const TArray<FValue>* Invalid = nullptr;
    if (!Index.Literal(TEXT("format"), TEXT("demo-native-fixtures-1"))
        || !Index.Flag(TEXT("test_only"), bTestOnly) || !Check.Require(bTestOnly, Index.Path, TEXT("test-only marker required"))
        || !Array(Index.Take(TEXT("valid")), Valid, Check, Index.Path, 1, 1024)
        || !Array(Index.Take(TEXT("invalid")), Invalid, Check, Index.Path, 0, 1024) || !Index.Done())
    {
        Test.AddError(Error);
        return false;
    }
    bool bFound = false;
    FLDDemoContentDigest Digest;
    for (const auto& RowValue : *Valid)
    {
        FFields Row(Check, RowValue, TEXT("fixture case"));
        FString Name;
        if (!Row.Identifier(TEXT("name"), Name))
        {
            Test.AddError(Error);
            return false;
        }
        if (Name != TEXT("approved_base")) { continue; }
        const TArray<FValue>* ChangedPaths = nullptr;
        FString MutatedQuantity;
        if (!Check.Require(!bFound, Row.Path, TEXT("duplicate approved_base fixture"))
            || !Row.Digest(TEXT("catalog_sha256"), Digest)
            || !Array(Row.Take(TEXT("changed_paths")), ChangedPaths, Check, Row.Path, 0, MaxFileBytes)
            || !Row.Text(TEXT("mutated_quantity"), MutatedQuantity, 0) || !Row.Done())
        {
            Test.AddError(Error);
            return false;
        }
        bFound = true;
    }
    if (!Test.TestTrue(TEXT("approved_base is explicit; synthetic base is not an approved balance fixture"), bFound))
    {
        return false;
    }
    FLDDemoContentBundle Bundle;
    auto* Asset = NewObject<ULDDemoContentCatalog>(GetTransientPackage());
    TUniquePtr<FLDDemoPreparedContent> Prepared;
    if (!FLDDemoContentBundle::ReadImportFiles(FPaths::Combine(Root, TEXT("approved_base")), Bundle, Error)
        || !ULDDemoContentCatalog::PrepareImport(*Asset, Bundle, Digest, Prepared, Error)
        || !Asset->CommitImport(Prepared, Digest, Error) || !Asset->Read(Digest, Out, Error))
    {
        Test.AddError(Error);
        return false;
    }
    return true;
}

// Exact 1e-6 P2 per millisecond of a points-per-second rate (the FORMULA.Regen step), or -1.
int64 MicroPerMs(const FLDDemoRational& Rate)
{
    if (Rate.Denominator <= 0 || Rate.Numerator < 0 || Rate.Numerator > MAX_int64 / 100000
        || (Rate.Numerator * 100000) % Rate.Denominator != 0)
    {
        return -1;
    }
    return Rate.Numerator * 100000 / Rate.Denominator;
}

FLDBodyCombatState Body(ELDVictimKind Kind, int64 MaxHpP2, int64 MaxMpP2, int64 AtMs)
{
    FLDBodyCombatState State;
    State.Identity = FGuid::NewGuid();
    State.Victim = {Kind, FGuid::NewGuid(), 1};
    State.Resources.MaxHpP2 = MaxHpP2;
    State.Resources.HpP2 = MaxHpP2 / 2;
    State.Resources.MaxMpP2 = MaxMpP2;
    State.Resources.MpP2 = MaxMpP2 / 2;
    State.Resources.Revision = 1;
    // Zero always-on rates isolate the out-of-combat terms.
    State.AttributeProjection.Final.HPRegenPerSec = FLDDemoRational();
    State.AttributeProjection.Final.MPRegenPerSec = FLDDemoRational();
    // A life starts out of combat at its spawn moment, exactly as LDCombatLife installs it.
    State.LastCombatAtMs = State.RegenThroughMs = AtMs;
    return State;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatRecoveryTest, "Lockdown.Rules.DemoCombatStatus.OutOfCombatRecovery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// D21: after REGEN.OutOfCombatDelaySec without a combat event every contender recovers a per-second share of its
// current maximum HP and MP; combat stops it at once, it stacks with the passive bonus, and dead or camp bodies do not get it.
bool FLDCombatRecoveryTest::RunTest(const FString& Parameters)
{
    using namespace LDCombatRecoveryTestDetail;
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!ReadRecoveryCatalog(*this, Catalog))
    {
        return false;
    }
    const FLDDemoContentDefinitions& Content = Catalog->Get();
    const FLDDemoContentParameters& P = Content.Parameters;
    bool bGood = TestTrue(TEXT("The approved D21 recovery is 1.5% of maximum HP and MP per second after 5 s"),
        P.OutOfCombatRegenDelayMs == 5000 && P.OutOfCombatMaxHPRegenBp == 150 && P.OutOfCombatMaxMPRegenBp == 150);
    LDTactical::FOutOfCombatRecovery Recovery;
    Recovery.DelayMs = P.OutOfCombatRegenDelayMs;
    Recovery.MaxHPBpPerSec = P.OutOfCombatMaxHPRegenBp;
    Recovery.MaxMPBpPerSec = P.OutOfCombatMaxMPRegenBp;
    const int64 Delay = Recovery.DelayMs;
    const int64 MaxHp = P.MaxHP.BaseP2;
    const int64 MaxMp = P.MaxMP.BaseP2 > 0 ? P.MaxMP.BaseP2 : 10000;
    // The passive bonus of the catalog (whatever passive holds out_of_combat_regen), at its highest rank.
    const FLDDemoEffectDefinition* Bonus = nullptr;
    for (const FLDDemoAbilityDefinition& Ability : Content.Abilities)
    {
        if (Ability.Kind != ELDDemoAbilityKind::Passive || Ability.Ranks.IsEmpty()) { continue; }
        for (const FLDDemoEffectDefinition& Effect : Ability.Ranks.Last().Effects)
        {
            if (!Bonus && Effect.GetOp() == ELDDemoEffectOp::OutOfCombatRegen) { Bonus = &Effect; }
        }
    }
    if (!TestNotNull(TEXT("The catalog holds a passive out-of-combat bonus to stack with"), Bonus)) { return false; }
    const int64 HpMicro = MaxHp * Recovery.MaxHPBpPerSec / 10;
    const int64 MpMicro = MaxMp * Recovery.MaxMPBpPerSec / 10;
    const int64 BonusMicro = MicroPerMs(Bonus->HPPerSec);
    if (!TestTrue(TEXT("Recovery and bonus rates are exact per millisecond"),
        HpMicro * 10 == MaxHp * Recovery.MaxHPBpPerSec && MpMicro * 10 == MaxMp * Recovery.MaxMPBpPerSec && BonusMicro >= 0))
    {
        return false;
    }

    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    if (!TestNotNull(TEXT("An isolated recovery world"), World)) { return false; }
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    ALDCombatDirector* Driver = World->SpawnActor<ALDCombatDirector>();
    ALDCharacter* Plain = World->SpawnActor<ALDCharacter>();
    ALDCharacter* Stacked = World->SpawnActor<ALDCharacter>();
    ALDCharacter* Dead = World->SpawnActor<ALDCharacter>();
    ALDCharacter* Camp = World->SpawnActor<ALDCharacter>();
    ALDCharacter* Respawned = World->SpawnActor<ALDCharacter>();
    if (!Driver || !Plain || !Stacked || !Dead || !Camp || !Respawned)
    {
        AddError(TEXT("Recovery fixture actors failed to spawn"));
        return false;
    }
    Driver->Bodies.Add(Plain, Body(ELDVictimKind::Contender, MaxHp, MaxMp, 0));
    FLDBodyCombatState StackedState = Body(ELDVictimKind::Contender, MaxHp, MaxMp, 0);
    FLDOutOfCombatRegenTerm Term;
    Term.DelayMs = Bonus->DelayMs;
    Term.HPPerSec = Bonus->HPPerSec;
    StackedState.AttributeProjection.OutOfCombatHPRegen = Term;
    Driver->Bodies.Add(Stacked, MoveTemp(StackedState));
    FLDBodyCombatState DeadState = Body(ELDVictimKind::Contender, MaxHp, MaxMp, 0);
    DeadState.bPendingDeath = true;
    Driver->Bodies.Add(Dead, MoveTemp(DeadState));
    Driver->Bodies.Add(Camp, Body(ELDVictimKind::CampMember, MaxHp, MaxMp, 0));
    FLDBodyCombatState& A = Driver->Bodies.FindChecked(Plain);
    FLDBodyCombatState& B = Driver->Bodies.FindChecked(Stacked);
    FLDBodyCombatState& D = Driver->Bodies.FindChecked(Dead);
    FLDBodyCombatState& C = Driver->Bodies.FindChecked(Camp);
    const int64 Hp0 = A.Resources.HpP2, Mp0 = A.Resources.MpP2;
    FString Error;

    // 1. Nothing before the delay has passed.
    bGood &= TestTrue(FString::Printf(TEXT("Regeneration through %lld ms (%s)"), Delay - 1, *Error),
        Driver->RegenerateBodies(Recovery, Delay - 1, Error));
    bGood &= TestTrue(TEXT("No recovery one millisecond before the delay"), A.Resources.HpP2 == Hp0 && A.Resources.MpP2 == Mp0
        && A.HpRegenMicroP2 == 0 && A.MpRegenMicroP2 == 0);

    // 2. It starts exactly at LastCombat + Delay and restores the share of both maxima.
    // Past both delays, so the passive bonus also runs for a while.
    const int64 T1 = FMath::Max(Delay, Bonus->DelayMs) + 1000;
    const int64 Run = T1 - Delay;
    bGood &= TestTrue(TEXT("Regeneration past both delays"), Driver->RegenerateBodies(Recovery, T1, Error));
    bGood &= TestTrue(TEXT("Recovery restores the HP share of the maximum from the delay on"),
        A.Resources.HpP2 == Hp0 + HpMicro * Run / MicroPerP2 && A.HpRegenMicroP2 == HpMicro * Run % MicroPerP2);
    bGood &= TestTrue(TEXT("Recovery restores the MP share of the maximum from the delay on"),
        A.Resources.MpP2 == Mp0 + MpMicro * Run / MicroPerP2 && A.MpRegenMicroP2 == MpMicro * Run % MicroPerP2);

    // 3. It stacks with the passive bonus, each term from its own delay.
    const int64 BonusFrom = FMath::Clamp(Bonus->DelayMs, int64(0), T1);
    const int64 StackedMicro = HpMicro * (T1 - Delay) + BonusMicro * (T1 - BonusFrom);
    bGood &= TestTrue(TEXT("Recovery and the passive bonus add up exactly"),
        B.Resources.HpP2 == Hp0 + StackedMicro / MicroPerP2 && B.HpRegenMicroP2 == StackedMicro % MicroPerP2);
    bGood &= TestTrue(TEXT("The passive bonus adds to recovery"), BonusMicro > 0 && B.Resources.HpP2 > A.Resources.HpP2);

    // 4. Dead and camp bodies never receive it.
    bGood &= TestTrue(TEXT("A dead body does not recover"),
        D.Resources.HpP2 == Hp0 && D.Resources.MpP2 == Mp0 && D.RegenThroughMs == 0);
    bGood &= TestTrue(TEXT("A camp member has no contender recovery"), C.Resources.HpP2 == Hp0 && C.Resources.MpP2 == Mp0);

    // 5. A combat event stops it immediately; it resumes only a full delay later.
    A.LastCombatAtMs = T1;
    const int64 HpCombat = A.Resources.HpP2, MpCombat = A.Resources.MpP2;
    const int64 HpRemainder = A.HpRegenMicroP2, MpRemainder = A.MpRegenMicroP2;
    Driver->Bodies.Add(Respawned, Body(ELDVictimKind::Contender, MaxHp, MaxMp, T1));
    // Adding a body may move the map's storage: read every body again.
    FLDBodyCombatState& R = Driver->Bodies.FindChecked(Respawned);
    FLDBodyCombatState& AA = Driver->Bodies.FindChecked(Plain);
    bGood &= TestTrue(TEXT("Regeneration through the combat delay"), Driver->RegenerateBodies(Recovery, T1 + Delay - 1, Error));
    bGood &= TestTrue(TEXT("Recovery stops at the combat event"), AA.Resources.HpP2 == HpCombat && AA.Resources.MpP2 == MpCombat
        && AA.HpRegenMicroP2 == HpRemainder && AA.MpRegenMicroP2 == MpRemainder);
    bGood &= TestTrue(TEXT("A new life's clock starts at its respawn moment"), R.Resources.HpP2 == Hp0 && R.Resources.MpP2 == Mp0);
    const int64 T2 = T1 + Delay + 2000;
    bGood &= TestTrue(TEXT("Regeneration after the combat delay"), Driver->RegenerateBodies(Recovery, T2, Error));
    bGood &= TestTrue(TEXT("Recovery resumes a full delay after the combat event"),
        AA.Resources.HpP2 == HpCombat + (HpRemainder + HpMicro * 2000) / MicroPerP2
        && AA.Resources.MpP2 == MpCombat + (MpRemainder + MpMicro * 2000) / MicroPerP2);
    bGood &= TestTrue(TEXT("The respawned life recovers from its own delay"),
        R.Resources.HpP2 == Hp0 + HpMicro * 2000 / MicroPerP2);

    // 6. The share follows the current maximum.
    const int64 HpBefore = AA.Resources.HpP2, RemainderBefore = AA.HpRegenMicroP2;
    AA.Resources.MaxHpP2 = MaxHp + 10000;
    const int64 RaisedMicro = AA.Resources.MaxHpP2 * Recovery.MaxHPBpPerSec / 10;
    bGood &= TestTrue(TEXT("Regeneration under a raised maximum"), Driver->RegenerateBodies(Recovery, T2 + 1000, Error));
    bGood &= TestTrue(TEXT("Recovery uses the current maximum"),
        AA.Resources.HpP2 == HpBefore + (RemainderBefore + RaisedMicro * 1000) / MicroPerP2);

    // 7. A full pool stays full; an inexact share is refused explicitly.
    AA.Resources.HpP2 = AA.Resources.MaxHpP2;
    AA.Resources.MpP2 = AA.Resources.MaxMpP2;
    bGood &= TestTrue(TEXT("Regeneration of a full pool"), Driver->RegenerateBodies(Recovery, T2 + 3000, Error));
    bGood &= TestTrue(TEXT("A full pool discards recovery"), AA.Resources.HpP2 == AA.Resources.MaxHpP2 && AA.HpRegenMicroP2 == 0
        && AA.Resources.MpP2 == AA.Resources.MaxMpP2);
    LDTactical::FOutOfCombatRecovery Inexact = Recovery;
    Inexact.MaxHPBpPerSec = 155;
    LDTactical::FRegenPools Pools;
    Pools.MaxHpP2 = 62001;
    Pools.HpP2 = 1;
    bGood &= TestFalse(TEXT("A share without an exact per-millisecond step is refused"),
        LDTactical::SettleRegen(FLDAttributeProjection(), &Inexact, 0, 0, Delay + 1, Pools, Error));
    bGood &= TestTrue(TEXT("The refusal is explicit"), !Error.IsEmpty());
    return bGood;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatDormRecoveryTest, "Lockdown.Rules.DemoCombatStatus.DormRecovery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// D21 (#79): a contender whose foot stands inside a dorm disk recovers DORM.Max*RatioPerSec of its current maxima once
// DORM.OutOfCombatDelaySec has passed since its last combat event. It stacks with the slow recovery, stops at a combat
// event, leaving or death, and camp members never get it. The dorm is not a safe zone: nothing else changes.
bool FLDCombatDormRecoveryTest::RunTest(const FString& Parameters)
{
    using namespace LDCombatRecoveryTestDetail;
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!ReadRecoveryCatalog(*this, Catalog))
    {
        return false;
    }
    const FLDDemoContentParameters& P = Catalog->Get().Parameters;
    bool bGood = TestTrue(TEXT("The approved dorm is a 400 cm disk recovering 10% of maximum HP and MP per second after 3 s"),
        P.DormRadiusCm.Numerator == 400 * P.DormRadiusCm.Denominator && P.DormRegenDelayMs == 3000
        && P.DormMaxHPRegenBp == 1000 && P.DormMaxMPRegenBp == 1000);
    LDTactical::FOutOfCombatRecovery Slow;
    Slow.DelayMs = P.OutOfCombatRegenDelayMs;
    Slow.MaxHPBpPerSec = P.OutOfCombatMaxHPRegenBp;
    Slow.MaxMPBpPerSec = P.OutOfCombatMaxMPRegenBp;
    LDTactical::FOutOfCombatRecovery Dorm;
    Dorm.DelayMs = P.DormRegenDelayMs;
    Dorm.MaxHPBpPerSec = P.DormMaxHPRegenBp;
    Dorm.MaxMPBpPerSec = P.DormMaxMPRegenBp;
    if (!TestTrue(TEXT("The dorm starts before the slow recovery"), Dorm.DelayMs < Slow.DelayMs)) { return false; }
    const double Radius = double(P.DormRadiusCm.Numerator) / double(P.DormRadiusCm.Denominator);
    FLDDormZones Zones;
    Zones.RadiusCm = Radius;
    Zones.Dorms.Add({TEXT("dorm_test"), FVector2D(1000., -2000.)});
    const int64 MaxHp = P.MaxHP.BaseP2;
    const int64 MaxMp = P.MaxMP.BaseP2 > 0 ? P.MaxMP.BaseP2 : 10000;
    const int64 DormHp = MaxHp * Dorm.MaxHPBpPerSec / 10, DormMp = MaxMp * Dorm.MaxMPBpPerSec / 10;
    const int64 SlowHp = MaxHp * Slow.MaxHPBpPerSec / 10;
    if (!TestTrue(TEXT("Dorm rates are exact per millisecond"), DormHp * 10 == MaxHp * Dorm.MaxHPBpPerSec
        && DormMp * 10 == MaxMp * Dorm.MaxMPBpPerSec && SlowHp * 10 == MaxHp * Slow.MaxHPBpPerSec))
    {
        return false;
    }

    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    if (!TestNotNull(TEXT("An isolated dorm world"), World)) { return false; }
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    ALDCombatDirector* Driver = World->SpawnActor<ALDCombatDirector>();
    ALDCharacter* Resting = World->SpawnActor<ALDCharacter>();
    ALDCharacter* Edge = World->SpawnActor<ALDCharacter>();
    ALDCharacter* Outside = World->SpawnActor<ALDCharacter>();
    ALDCharacter* Camp = World->SpawnActor<ALDCharacter>();
    ALDCharacter* Dead = World->SpawnActor<ALDCharacter>();
    if (!Driver || !Resting || !Edge || !Outside || !Camp || !Dead)
    {
        AddError(TEXT("Dorm fixture actors failed to spawn"));
        return false;
    }
    const FVector2D Center = Zones.Dorms[0].CenterCm;
    const auto Place = [](ALDCharacter* Actor, const FVector2D& XY) { Actor->SetActorLocation(FVector(XY.X, XY.Y, 200.)); };
    Place(Resting, Center + FVector2D(100., 50.));
    // The boundary counts as inside, like the capture circle.
    Place(Edge, Center + FVector2D(Radius, 0.));
    Place(Outside, Center + FVector2D(Radius + 1., 0.));
    Place(Camp, Center);
    Place(Dead, Center);
    Driver->Bodies.Add(Resting, Body(ELDVictimKind::Contender, MaxHp, MaxMp, 0));
    Driver->Bodies.Add(Edge, Body(ELDVictimKind::Contender, MaxHp, MaxMp, 0));
    Driver->Bodies.Add(Outside, Body(ELDVictimKind::Contender, MaxHp, MaxMp, 0));
    Driver->Bodies.Add(Camp, Body(ELDVictimKind::CampMember, MaxHp, MaxMp, 0));
    FLDBodyCombatState DeadState = Body(ELDVictimKind::Contender, MaxHp, MaxMp, 0);
    DeadState.bPendingDeath = true;
    Driver->Bodies.Add(Dead, MoveTemp(DeadState));
    FLDBodyCombatState& R = Driver->Bodies.FindChecked(Resting);
    FLDBodyCombatState& E = Driver->Bodies.FindChecked(Edge);
    FLDBodyCombatState& O = Driver->Bodies.FindChecked(Outside);
    FLDBodyCombatState& C = Driver->Bodies.FindChecked(Camp);
    FLDBodyCombatState& D = Driver->Bodies.FindChecked(Dead);
    const int64 Hp0 = R.Resources.HpP2, Mp0 = R.Resources.MpP2;
    FString Error;
    const auto Settle = [&](int64 AtMs)
    {
        return TestTrue(FString::Printf(TEXT("Regeneration through %lld ms (%s)"), AtMs, *Error),
            Driver->RegenerateBodies(Slow, AtMs, Error, &Dorm, &Zones));
    };

    // 1. Nothing before the dorm delay; standing inside is already known.
    bGood &= Settle(Dorm.DelayMs - 1);
    bGood &= TestTrue(TEXT("No dorm recovery one millisecond before its delay"), R.Resources.HpP2 == Hp0 && R.Resources.MpP2 == Mp0);
    bGood &= TestTrue(TEXT("Inside and boundary bodies are in the dorm; the outside one is not"), R.bInDorm && E.bInDorm && !O.bInDorm);

    // 2. From the dorm delay on, the dorm share of both maxima; outside nothing until the slow delay.
    const int64 T1 = Dorm.DelayMs + 1000;
    bGood &= Settle(T1);
    bGood &= TestTrue(TEXT("The dorm restores its HP share from its delay on"),
        R.Resources.HpP2 == Hp0 + DormHp * 1000 / MicroPerP2 && R.HpRegenMicroP2 == DormHp * 1000 % MicroPerP2);
    bGood &= TestTrue(TEXT("The dorm restores its MP share from its delay on"), R.Resources.MpP2 == Mp0 + DormMp * 1000 / MicroPerP2);
    bGood &= TestTrue(TEXT("A foot on the boundary recovers like one inside"), E.Resources.HpP2 == R.Resources.HpP2);
    bGood &= TestTrue(TEXT("A foot one centimetre outside gets no dorm recovery"), O.Resources.HpP2 == Hp0 && O.Resources.MpP2 == Mp0);

    // 3. Past the slow delay both run and add up exactly, each from its own delay.
    const int64 T2 = Slow.DelayMs + 1000;
    bGood &= Settle(T2);
    const int64 Stacked = DormHp * (T2 - Dorm.DelayMs) + SlowHp * (T2 - Slow.DelayMs);
    bGood &= TestTrue(TEXT("Dorm and slow recovery stack exactly"),
        R.Resources.HpP2 == Hp0 + Stacked / MicroPerP2 && R.HpRegenMicroP2 == Stacked % MicroPerP2);
    bGood &= TestTrue(TEXT("Outside only the slow recovery runs"), O.Resources.HpP2 == Hp0 + SlowHp * 1000 / MicroPerP2);

    // 4. Camp members and dead bodies never receive it.
    bGood &= TestTrue(TEXT("A camp member in a dorm gets nothing"), C.Resources.HpP2 == Hp0 && C.Resources.MpP2 == Mp0 && !C.bInDorm);
    bGood &= TestTrue(TEXT("A dead body in a dorm gets nothing"), D.Resources.HpP2 == Hp0 && D.RegenThroughMs == 0);

    // 5. A combat event stops it at once; it resumes a full dorm delay later.
    R.LastCombatAtMs = T2;
    const int64 HpCombat = R.Resources.HpP2, RemainderCombat = R.HpRegenMicroP2;
    bGood &= Settle(T2 + Dorm.DelayMs);
    bGood &= TestTrue(TEXT("Combat stops dorm recovery until its delay has passed again"),
        R.Resources.HpP2 == HpCombat && R.HpRegenMicroP2 == RemainderCombat);
    const int64 T3 = T2 + Dorm.DelayMs + 500;
    bGood &= Settle(T3);
    bGood &= TestTrue(TEXT("Dorm recovery resumes a full delay after the combat event"),
        R.Resources.HpP2 == HpCombat + (RemainderCombat + DormHp * 500) / MicroPerP2);

    // 6. Leaving ends it at the next settle, and the slow recovery carries on alone.
    Place(Resting, Center + FVector2D(Radius + 200., 0.));
    const int64 HpLeft = R.Resources.HpP2, RemainderLeft = R.HpRegenMicroP2;
    // Past the slow delay of the combat event, so the slow term is the only one running from its start.
    const int64 T4 = T2 + Slow.DelayMs + 1000;
    bGood &= Settle(T4);
    const int64 SlowOnly = SlowHp * (T4 - FMath::Max(T3, T2 + Slow.DelayMs));
    bGood &= TestTrue(TEXT("Outside the dorm only the slow recovery remains"), !R.bInDorm
        && R.Resources.HpP2 == HpLeft + (RemainderLeft + SlowOnly) / MicroPerP2);

    // 7. About ten seconds in a dorm fill a pool from zero; a full pool discards the rest.
    E.Resources.HpP2 = 0;
    E.HpRegenMicroP2 = 0;
    E.LastCombatAtMs = T4;
    E.RegenThroughMs = T4;
    bGood &= Settle(T4 + Dorm.DelayMs + 10000);
    bGood &= TestTrue(TEXT("Ten seconds of dorm recovery fill an empty pool"), E.Resources.HpP2 == E.Resources.MaxHpP2);
    return bGood;
}

#endif
