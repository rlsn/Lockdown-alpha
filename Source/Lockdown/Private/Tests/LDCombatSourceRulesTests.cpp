#include "Combat/LDCombatSourceRules.h"
#include "Combat/LDCombatDirector.h"
#include "Navigation/LDOrderRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "../Development/LDDemoContentJson.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

namespace LDCombatSourceRulesTestDetail
{
    // Same isolated approved_base fixture as the evaluator tests; no live-data fallback.
    bool ReadApprovedContent(FAutomationTestBase& Test, TSharedPtr<const FLDDemoContentSnapshot>& Out)
    {
        using namespace LDDemoContent;
        FString Root, Error;
        if (!FParse::Value(FCommandLine::Get(), TEXT("LDDemoContentFixtures="), Root) || Root.IsEmpty())
        {
            Test.AddError(TEXT("Missing -LDDemoContentFixtures=<isolated Artifacts directory>; export approved_base first."));
            return false;
        }
        if (!Test.TestTrue(TEXT("Source-rule fixtures are isolated under this project's Artifacts"),
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
        if (!Test.TestTrue(TEXT("approved_base fixture is present"), bFound)) { return false; }
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

    const FName Anchor(TEXT("item_phase_anchor"));
    const FName Maul(TEXT("item_echo_maul"));
    const FName Bolt(TEXT("active_arc_bolt"));
    constexpr int64 NowMs = 60000;

    FLDCombatTrack GearTrack(const FGuid& Id, const FGuid& Bound, bool bBound, int64 ReadyAtMs)
    {
        FLDCombatTrack Track;
        Track.IssuedCooldownId = Id;
        Track.Kind = ELDCombatSourceKind::Gear;
        Track.DefinitionId = Anchor;
        Track.BoundInstance = Bound;
        Track.bBound = bBound;
        Track.ReadyAtMs = ReadyAtMs;
        return Track;
    }

    FLDEquippedItemValue Item(ELDCombatEntry Entry, const FGuid& Instance, FName Definition)
    {
        FLDEquippedItemValue Value;
        Value.Entry = Entry;
        Value.Instance = Instance;
        Value.DefinitionId = Definition;
        return Value;
    }

    bool Plan(const FLDCombatEntityRecord& Record, const FLDBuildProjectionInput& Input, const FLDDemoContentSnapshot& Content,
        TArray<FLDCombatBindingPlan>& Out, ELDCombatPrepareError& Code)
    {
        FString Error;
        return LDCombatSourceRules::PlanTrackBindings(Record, Input, NowMs, Content, Out, Code, Error);
    }

    FLDObservedUnitView Unit(const FGuid& Handle, const FVector& Feet, ELDObservedUnitKind Kind, const FGuid& Contender)
    {
        FLDObservedUnitView View;
        View.Handle = Handle;
        View.FeetCm = Feet;
        View.Kind = Kind;
        View.ContenderId = Contender;
        return View;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDCombatSourceTracksTest, "Lockdown.Rules.DemoCombatSources.CooldownTracks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDCombatSourceTracksTest::RunTest(const FString& Parameters)
{
    using namespace LDCombatSourceRulesTestDetail;
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    if (!ReadApprovedContent(*this, Content)) { return false; }
    const FGuid A = FGuid::NewGuid(), B = FGuid::NewGuid(), C = FGuid::NewGuid();
    TArray<FLDCombatBindingPlan> Out;
    ELDCombatPrepareError Code = ELDCombatPrepareError::None;

    FLDCombatEntityRecord Empty;
    FLDBuildProjectionInput First;
    First.Equipment.Add(Item(ELDCombatEntry::Item1, A, Anchor));
    TestTrue(TEXT("A first active copy issues a ready new track"),
        Plan(Empty, First, *Content, Out, Code) && Out.Num() == 1 && Out[0].bNewTrack && Out[0].ReadyAtMs == 0
        && Out[0].Entry == ELDCombatEntry::Item1 && Out[0].Source.ItemInstance == A && Out[0].IssuedCooldownId.IsValid());

    const FGuid Sold = FGuid::NewGuid();
    FLDCombatEntityRecord Debt;
    Debt.Tracks.Add(GearTrack(Sold, A, false, NowMs + 10000));
    FLDBuildProjectionInput Rebuy;
    Rebuy.Equipment.Add(Item(ELDCombatEntry::Item2, B, Anchor));
    TestTrue(TEXT("A rebought copy inherits the sold copy's unexpired debt"),
        Plan(Debt, Rebuy, *Content, Out, Code) && Out.Num() == 1 && !Out[0].bNewTrack
        && Out[0].IssuedCooldownId == Sold && Out[0].ReadyAtMs == NowMs + 10000);

    const FGuid Held = FGuid::NewGuid(), Ready = FGuid::NewGuid();
    FLDCombatEntityRecord Pair;
    Pair.Tracks.Add(GearTrack(Held, A, true, NowMs + 5000));
    Pair.Tracks.Add(GearTrack(Ready, C, false, NowMs - 1));
    FLDBuildProjectionInput Both;
    Both.Equipment.Add(Item(ELDCombatEntry::Item1, A, Anchor));
    Both.Equipment.Add(Item(ELDCombatEntry::Item2, B, Anchor));
    const bool bBoth = Plan(Pair, Both, *Content, Out, Code) && Out.Num() == 2;
    TestTrue(TEXT("A held copy keeps its own track and a second copy takes the free ready one"),
        bBoth && Out[0].Source.ItemInstance == A && Out[0].IssuedCooldownId == Held && Out[0].ReadyAtMs == NowMs + 5000
        && Out[1].Source.ItemInstance == B && Out[1].IssuedCooldownId == Ready && !Out[1].bNewTrack);

    const FGuid Early = FGuid::NewGuid(), Late = FGuid::NewGuid();
    FLDCombatEntityRecord TwoDebts;
    TwoDebts.Tracks.Add(GearTrack(Early, A, false, NowMs + 3000));
    TwoDebts.Tracks.Add(GearTrack(Late, B, false, NowMs + 8000));
    FLDBuildProjectionInput One;
    One.Equipment.Add(Item(ELDCombatEntry::Item1, C, Anchor));
    TestTrue(TEXT("Debt first: the latest unexpired unbound track is taken"),
        Plan(TwoDebts, One, *Content, Out, Code) && Out.Num() == 1 && Out[0].IssuedCooldownId == Late
        && Out[0].ReadyAtMs == NowMs + 8000);

    FLDCombatTrack Learned;
    Learned.IssuedCooldownId = FGuid::NewGuid();
    Learned.Kind = ELDCombatSourceKind::Learned;
    Learned.DefinitionId = Bolt;
    Learned.ReadyAtMs = NowMs + 4000;
    FLDCombatEntityRecord Relearn;
    Relearn.Tracks.Add(Learned);
    FLDBuildProjectionInput Active;
    FLDLearnedActiveValue Skill;
    Skill.AbilityId = Bolt;
    Skill.Rank = 1;
    Skill.Entry = ELDCombatEntry::W;
    Active.Actives.Add(Skill);
    TestTrue(TEXT("A learned definition returns to its single track and end time in any slot"),
        Plan(Relearn, Active, *Content, Out, Code) && Out.Num() == 1 && Out[0].Entry == ELDCombatEntry::W
        && Out[0].IssuedCooldownId == Learned.IssuedCooldownId && Out[0].ReadyAtMs == NowMs + 4000);

    FLDBuildProjectionInput Passive;
    Passive.Equipment.Add(Item(ELDCombatEntry::Item1, A, Maul));
    TestTrue(TEXT("Gear without an active binds no cooldown track"),
        Plan(Empty, Passive, *Content, Out, Code) && Out.IsEmpty());

    FLDBuildProjectionInput Duplicate;
    Duplicate.Equipment.Add(Item(ELDCombatEntry::Item1, A, Anchor));
    Duplicate.Equipment.Add(Item(ELDCombatEntry::Item2, A, Anchor));
    TestTrue(TEXT("One instance in two slots is refused"),
        !Plan(Empty, Duplicate, *Content, Out, Code) && Code == ELDCombatPrepareError::InvalidInput);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDOrderAttackMoveCandidateTest, "Lockdown.Rules.DemoOrders.AttackMoveCandidate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDOrderAttackMoveCandidateTest::RunTest(const FString& Parameters)
{
    using LDCombatSourceRulesTestDetail::Unit;
    const FGuid Self = FGuid::NewGuid(), Other = FGuid::NewGuid();
    const FGuid Low(1, 0, 0, 0), High(2, 0, 0, 0);
    TArray<FLDObservedUnitView> Units;
    Units.Add(Unit(FGuid(9, 0, 0, 0), FVector(0, 0, 0), ELDObservedUnitKind::Contender, Self));
    Units.Add(Unit(High, FVector(100, 0, 0), ELDObservedUnitKind::Contender, Other));
    Units.Add(Unit(Low, FVector(0, 100, 0), ELDObservedUnitKind::CampMember, FGuid()));
    Units.Add(Unit(FGuid(3, 0, 0, 0), FVector(500, 0, 0), ELDObservedUnitKind::Contender, FGuid::NewGuid()));
    TestEqual(TEXT("Equal planar distance takes the smaller observer handle, never the attacker"),
        LDOrderRules::NearestCandidate(Units, FVector(0, 0, 50), 440., Self), 2);
    Units.Add(Unit(FGuid(4, 0, 0, 0), FVector(60, 0, 0), ELDObservedUnitKind::Contender, Other));
    TestEqual(TEXT("The nearest visible unit wins"), LDOrderRules::NearestCandidate(Units, FVector::ZeroVector, 440., Self), 4);
    TestEqual(TEXT("Units beyond the acquisition radius are not candidates"),
        LDOrderRules::NearestCandidate(Units, FVector(1000, 0, 0), 440., Self), INDEX_NONE);
    TestEqual(TEXT("A nonpositive radius has no candidate"), LDOrderRules::NearestCandidate(Units, FVector::ZeroVector, 0., Self), INDEX_NONE);
    TestEqual(TEXT("No visible unit, no candidate"),
        LDOrderRules::NearestCandidate(TArray<FLDObservedUnitView>(), FVector::ZeroVector, 440., Self), INDEX_NONE);
    return true;
}

#endif
