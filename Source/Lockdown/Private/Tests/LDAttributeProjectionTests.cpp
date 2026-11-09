#include "Combat/LDAttributeProjection.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/LDTacticalRules.h"
#include "../Development/LDDemoContentJson.h"
#include "../Development/LDDemoContentMath.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

namespace LDAttributeProjectionTests
{
namespace
{
using EAttribute = ELDProjectedAttribute;
using EError = ELDAttributeProjectionError;

// Reads the producer-exported approved_base only; there is no live-data fallback.
bool ReadAttributeFixture(FAutomationTestBase& Test, TSharedPtr<const FLDDemoContentSnapshot>& Out)
{
    using namespace LDDemoContent;
    FString Root, Error;
    if (!FParse::Value(FCommandLine::Get(), TEXT("LDDemoContentFixtures="), Root) || Root.IsEmpty())
    {
        Test.AddError(TEXT("Missing -LDDemoContentFixtures=<isolated Artifacts directory>; export approved_base first. No live-data fallback."));
        return false;
    }
    if (!Test.TestTrue(TEXT("Attribute fixtures are isolated under this project's Artifacts"),
        FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Root),
            FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), TEXT("Artifacts")))))
    {
        return false;
    }
    FCheck Check(Error);
    TArray<uint8> Bytes;
    FValue IndexValue;
    if (!LDDemoContent::ReadFile(FPaths::Combine(Root, TEXT("index.json")), Bytes, Error)
        || !LDDemoContent::Parse(Bytes, IndexValue, Check, TEXT("fixture index")))
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
        || !LDDemoContent::Array(Index.Take(TEXT("valid")), Valid, Check, Index.Path, 1, 1024)
        || !LDDemoContent::Array(Index.Take(TEXT("invalid")), Invalid, Check, Index.Path, 0, 1024) || !Index.Done())
    {
        Test.AddError(Error);
        return false;
    }
    bool bFound = false;
    FLDDemoContentDigest Digest;
    for (const FValue& RowValue : *Valid)
    {
        FFields Row(Check, RowValue, TEXT("fixture case"));
        FString Name;
        if (!Row.Identifier(TEXT("name"), Name))
        {
            Test.AddError(Error);
            return false;
        }
        if (!Name.Equals(TEXT("approved_base"), ESearchCase::CaseSensitive))
        {
            continue;
        }
        const TArray<FValue>* ChangedPaths = nullptr;
        FString MutatedQuantity;
        if (!Check.Require(!bFound, Row.Path, TEXT("duplicate approved_base fixture"))
            || !Row.Digest(TEXT("catalog_sha256"), Digest)
            || !LDDemoContent::Array(Row.Take(TEXT("changed_paths")), ChangedPaths, Check, Row.Path, 0, MaxFileBytes)
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
    ULDDemoContentCatalog* Asset = NewObject<ULDDemoContentCatalog>(GetTransientPackage());
    TUniquePtr<FLDDemoPreparedContent> Prepared;
    if (!FLDDemoContentBundle::ReadImportFiles(FPaths::Combine(Root, TEXT("approved_base")), Bundle, Error)
        || !ULDDemoContentCatalog::PrepareImport(*Asset, Bundle, Digest, Prepared, Error)
        || !Asset->CommitImport(Prepared, Digest, Error) || !Asset->Read(Digest, Out, Error))
    {
        Test.AddError(Error);
        return false;
    }
    const FLDDemoContentParameters& P = Out->Get().Parameters;
    const FLDDemoProgressionDefinition& Progression = Out->Get().Progression;
    return Test.TestTrue(TEXT("Fixture carries the approved first-play attribute parameters"),
        P.MaxHP.BaseP2 == 62000 && P.MaxHP.PerLevelP2 == 5800 && P.MaxMP.BaseP2 == 28000 && P.MaxMP.PerLevelP2 == 2200
        && P.AD.BaseP2 == 7000 && P.AD.PerLevelP2 == 320 && P.Armor.BaseP2 == 1800 && P.MagicResist.BaseP2 == 1800
        && P.AttackSpeed.Base.Numerator == 9 && P.AttackSpeed.Base.Denominator == 10 && P.AttackSpeedPerLevelBp == 200
        && P.MoveSpeedCmPerSec.Base.Numerator == 300 && P.MoveSpeedCmPerSec.Base.Denominator == 1
        && P.AttackRangeCm.Base.Numerator == 220 && P.AttackRangeCm.Base.Denominator == 1
        && Progression.MaxLevel == 16 && Progression.MaxChoices == 16 && Progression.MaxSkillRank == 2
        && Progression.ActiveSlots == 3);
}

FLDDemoRational MakeRational(int64 Numerator, int64 Denominator = 1)
{
    FLDDemoRational Result;
    Result.Numerator = Numerator;
    Result.Denominator = Denominator;
    return Result;
}

FLDAttributeBuildValues MakeBuild(uint8 Level, TArray<FLDLearnedPassiveValue> Passives = {},
    TArray<FLDAttributeCardValue> Cards = {})
{
    return FLDAttributeBuildValues(Level, MoveTemp(Passives), MoveTemp(Cards));
}

struct FExpectedAttributes
{
    int64 MaxHPP2 = 0;
    int64 MaxMPP2 = 0;
    int64 ADP2 = 0;
    int64 APP2 = 0;
    int64 ArmorP2 = 0;
    int64 MagicResistP2 = 0;
    FLDDemoRational AttackSpeed;
    FLDDemoRational MoveSpeedCmPerSec;
    FLDDemoRational AttackRangeCm;
    FLDDemoRational HPRegenPerSec;
    FLDDemoRational MPRegenPerSec;
    int64 CritChanceBp = 0;
    int64 CooldownHasteP2 = 0;
};

// Approved CONTENT_AND_BALANCE section 3.2 values for an unequipped character without choices.
FExpectedAttributes ApprovedLevel(int32 Level)
{
    FExpectedAttributes Expected;
    Expected.MoveSpeedCmPerSec = MakeRational(300);
    Expected.AttackRangeCm = MakeRational(220);
    switch (Level)
    {
    case 1:
        Expected.MaxHPP2 = 62000; Expected.MaxMPP2 = 28000; Expected.ADP2 = 7000;
        Expected.ArmorP2 = 1800; Expected.MagicResistP2 = 1800; Expected.AttackSpeed = MakeRational(9, 10);
        Expected.HPRegenPerSec = MakeRational(8, 5); Expected.MPRegenPerSec = MakeRational(6, 5);
        break;
    case 2:
        Expected.MaxHPP2 = 67800; Expected.MaxMPP2 = 30200; Expected.ADP2 = 7320;
        Expected.ArmorP2 = 2000; Expected.MagicResistP2 = 2000; Expected.AttackSpeed = MakeRational(459, 500);
        Expected.HPRegenPerSec = MakeRational(43, 25); Expected.MPRegenPerSec = MakeRational(32, 25);
        break;
    case 4:
        Expected.MaxHPP2 = 79400; Expected.MaxMPP2 = 34600; Expected.ADP2 = 7960;
        Expected.ArmorP2 = 2400; Expected.MagicResistP2 = 2400; Expected.AttackSpeed = MakeRational(477, 500);
        Expected.HPRegenPerSec = MakeRational(49, 25); Expected.MPRegenPerSec = MakeRational(36, 25);
        break;
    case 5:
        Expected.MaxHPP2 = 85200; Expected.MaxMPP2 = 36800; Expected.ADP2 = 8280;
        Expected.ArmorP2 = 2600; Expected.MagicResistP2 = 2600; Expected.AttackSpeed = MakeRational(243, 250);
        Expected.HPRegenPerSec = MakeRational(52, 25); Expected.MPRegenPerSec = MakeRational(38, 25);
        break;
    case 6:
        Expected.MaxHPP2 = 91000; Expected.MaxMPP2 = 39000; Expected.ADP2 = 8600;
        Expected.ArmorP2 = 2800; Expected.MagicResistP2 = 2800; Expected.AttackSpeed = MakeRational(99, 100);
        Expected.HPRegenPerSec = MakeRational(11, 5); Expected.MPRegenPerSec = MakeRational(8, 5);
        break;
    case 10:
        Expected.MaxHPP2 = 114200; Expected.MaxMPP2 = 47800; Expected.ADP2 = 9880;
        Expected.ArmorP2 = 3600; Expected.MagicResistP2 = 3600; Expected.AttackSpeed = MakeRational(531, 500);
        Expected.HPRegenPerSec = MakeRational(67, 25); Expected.MPRegenPerSec = MakeRational(48, 25);
        break;
    default:
        break;
    }
    return Expected;
}

bool ExpectRational(FAutomationTestBase& Test, const FString& What, const FLDDemoRational& Actual,
    const FLDDemoRational& Expected)
{
    bool bGood = Test.TestEqual(What + TEXT(" numerator"), Actual.Numerator, Expected.Numerator);
    bGood &= Test.TestEqual(What + TEXT(" denominator"), Actual.Denominator, Expected.Denominator);
    return bGood;
}

bool ExpectTerms(FAutomationTestBase& Test, const FString& What, const FLDAttributeTerms& Terms,
    const FLDDemoRational& Flat, const FLDDemoRational& AdditiveRatio)
{
    bool bGood = ExpectRational(Test, What + TEXT(" flat"), Terms.Flat, Flat);
    bGood &= ExpectRational(Test, What + TEXT(" additive ratio"), Terms.AdditiveRatio, AdditiveRatio);
    return bGood;
}

bool ExpectAttributes(FAutomationTestBase& Test, const FString& Label, const FLDProjectedAttributes& Actual,
    const FExpectedAttributes& Expected)
{
    bool bGood = Test.TestEqual(Label + TEXT(": MaxHP P2"), Actual.MaxHPP2, Expected.MaxHPP2);
    bGood &= Test.TestEqual(Label + TEXT(": MaxMP P2"), Actual.MaxMPP2, Expected.MaxMPP2);
    bGood &= Test.TestEqual(Label + TEXT(": AD P2"), Actual.ADP2, Expected.ADP2);
    bGood &= Test.TestEqual(Label + TEXT(": AP P2"), Actual.APP2, Expected.APP2);
    bGood &= Test.TestEqual(Label + TEXT(": armor P2"), Actual.ArmorP2, Expected.ArmorP2);
    bGood &= Test.TestEqual(Label + TEXT(": magic resist P2"), Actual.MagicResistP2, Expected.MagicResistP2);
    bGood &= ExpectRational(Test, Label + TEXT(": attack speed"), Actual.AttackSpeed, Expected.AttackSpeed);
    bGood &= ExpectRational(Test, Label + TEXT(": move speed"), Actual.MoveSpeedCmPerSec, Expected.MoveSpeedCmPerSec);
    bGood &= ExpectRational(Test, Label + TEXT(": attack range"), Actual.AttackRangeCm, Expected.AttackRangeCm);
    bGood &= ExpectRational(Test, Label + TEXT(": HP regeneration"), Actual.HPRegenPerSec, Expected.HPRegenPerSec);
    bGood &= ExpectRational(Test, Label + TEXT(": MP regeneration"), Actual.MPRegenPerSec, Expected.MPRegenPerSec);
    bGood &= Test.TestEqual(Label + TEXT(": crit chance bp"), Actual.CritChanceBp, Expected.CritChanceBp);
    bGood &= Test.TestEqual(Label + TEXT(": cooldown haste P2"), Actual.CooldownHasteP2, Expected.CooldownHasteP2);
    return bGood;
}

// Every row must explain its final exactly, match the typed final and stay inside the compiled bounds.
bool ExpectExplainedFinals(FAutomationTestBase& Test, const FString& Label, const FLDAttributeProjection& Projection,
    const FLDDemoContentSnapshot& Catalog)
{
    const TArray<FLDAttributeContributions>& Rows = Projection.Contributions;
    if (!Test.TestEqual(Label + TEXT(": one contribution row per attribute"), Rows.Num(), int32(EAttribute::Count)))
    {
        return false;
    }
    FString MathError;
    LDDemoContent::FCheck Check(MathError);
    LDDemoContent::FNumbers N(Check);
    bool bGood = true;
    for (int32 Index = 0; Index < Rows.Num(); ++Index)
    {
        const FLDAttributeContributions& Row = Rows[Index];
        const FString What = FString::Printf(TEXT("%s: contribution row %d"), *Label, Index);
        const LDDemoContent::FExact Flat = N.Sub(N.Add(N.Add(N.Value(Row.LevelGrowth.Flat), N.Value(Row.PermanentChoices.Flat)),
            N.Value(Row.Passives.Flat)), N.Value(Row.Penalties.Flat));
        const LDDemoContent::FExact Ratio = N.Sub(N.Add(
            N.Add(N.Value(Row.LevelGrowth.AdditiveRatio), N.Value(Row.PermanentChoices.AdditiveRatio)),
            N.Value(Row.Passives.AdditiveRatio)), N.Value(Row.Penalties.AdditiveRatio));
        // #112 §4: every attribute clamps to its catalog bounds; a clamped row reports it and its final differs from the sum.
        const bool bExplained = N.EQ(N.Mul(N.Add(N.Value(Row.Base), Flat), N.Add(N.Value(1), Ratio)), N.Value(Row.Final));
        bGood &= Test.TestTrue(What + TEXT(" keeps attribute order"), Row.Attribute == static_cast<EAttribute>(Index));
        bGood &= Test.TestTrue(What + TEXT(" final is (base + flat - penalty) x (1 + ratio - penalty), or clamped and marked"),
            bExplained != Row.bClamped);
    }
    const FLDProjectedAttributes& Final = Projection.Final;
    const auto Explained = [&N, &Rows](EAttribute Attribute, int64 Stored, int64 Scale)
    {
        return N.EQ(N.Value(Rows[int32(Attribute)].Final), N.Value(Stored, Scale));
    };
    const auto Same = [&Rows](EAttribute Attribute, const FLDDemoRational& Typed)
    {
        const FLDDemoRational& Row = Rows[int32(Attribute)].Final;
        return Row.Numerator == Typed.Numerator && Row.Denominator == Typed.Denominator;
    };
    bGood &= Test.TestTrue(Label + TEXT(": typed finals equal the explained finals"),
        Explained(EAttribute::MaxHP, Final.MaxHPP2, 100) && Explained(EAttribute::MaxMP, Final.MaxMPP2, 100)
        && Explained(EAttribute::AD, Final.ADP2, 100) && Explained(EAttribute::AP, Final.APP2, 100)
        && Explained(EAttribute::Armor, Final.ArmorP2, 100) && Explained(EAttribute::MagicResist, Final.MagicResistP2, 100)
        && Same(EAttribute::AttackSpeed, Final.AttackSpeed) && Same(EAttribute::MoveSpeed, Final.MoveSpeedCmPerSec)
        && Same(EAttribute::AttackRange, Final.AttackRangeCm) && Same(EAttribute::HPRegen, Final.HPRegenPerSec)
        && Same(EAttribute::MPRegen, Final.MPRegenPerSec) && Explained(EAttribute::CritChance, Final.CritChanceBp, 10000)
        && Explained(EAttribute::CooldownHaste, Final.CooldownHasteP2, 100));
    const FLDDemoContentParameters& P = Catalog.Get().Parameters;
    const auto InPoints = [](int64 Value, const FLDDemoP2Bounds& Bounds)
    {
        return Value >= Bounds.MinP2 && Value <= Bounds.MaxP2;
    };
    const auto InRange = [&N](const FLDDemoRational& Value, const FLDDemoRational& Low, const FLDDemoRational& High)
    {
        return N.LE(N.Value(Low), N.Value(Value)) && N.LE(N.Value(Value), N.Value(High));
    };
    bGood &= Test.TestTrue(Label + TEXT(": every final stays inside the compiled catalog bounds"),
        InPoints(Final.MaxHPP2, P.MaxHP) && InPoints(Final.MaxMPP2, P.MaxMP) && InPoints(Final.ADP2, P.AD)
        && InPoints(Final.APP2, P.AP) && InPoints(Final.ArmorP2, P.Armor) && InPoints(Final.MagicResistP2, P.MagicResist)
        && InRange(Final.AttackSpeed, P.AttackSpeed.Min, P.AttackSpeed.Max)
        && InRange(Final.MoveSpeedCmPerSec, P.MoveSpeedCmPerSec.Min, P.MoveSpeedCmPerSec.Max)
        && InRange(Final.AttackRangeCm, P.AttackRangeCm.Min, P.AttackRangeCm.Max)
        && InRange(Final.HPRegenPerSec, P.HPRegen.Min, P.HPRegen.Max)
        && InRange(Final.MPRegenPerSec, P.MPRegen.Min, P.MPRegen.Max)
        && Final.CritChanceBp >= 0 && Final.CritChanceBp <= 10000
        && Final.CooldownHasteP2 >= 0 && Final.CooldownHasteP2 <= P.CooldownHasteMaxP2);
    bGood &= Test.TestTrue(Label + TEXT(": exact test arithmetic stays representable"), Check.Good);
    return bGood;
}

bool ProjectBuild(FAutomationTestBase& Test, const FString& Label, const FLDAttributeBuildValues& Build,
    const FLDDemoContentSnapshot& Catalog, FLDAttributeProjection& Out)
{
    const FLDAttributeProjectionResult Result = LDAttributeProjection::Project(Build, Catalog);
    if (!Test.TestTrue(Label + TEXT(": projection succeeds"), Result.IsSuccess()))
    {
        Test.AddError(Label + TEXT(": ") + Result.Error.Field + TEXT(": ") + Result.Error.Message);
        return false;
    }
    Out = Result.Value.GetValue();
    bool bGood = Test.TestTrue(Label + TEXT(": success carries no error"),
        Result.Error.Code == EError::None && Result.Error.Field.IsEmpty() && Result.Error.Message.IsEmpty());
    bGood &= ExpectExplainedFinals(Test, Label, Out, Catalog);
    return bGood;
}

template <typename TResult>
bool ExpectRefusal(FAutomationTestBase& Test, const FString& Label, const TResult& Result, EError Code,
    const TCHAR* Field)
{
    const bool bGood = Test.TestTrue(Label + TEXT(": typed refusal without a success-shaped value"),
        !Result.IsSuccess() && !Result.Value.IsSet() && Result.Error.Code == Code
        && !Result.Error.Message.IsEmpty() && Result.Error.Field.Equals(Field, ESearchCase::CaseSensitive));
    if (!bGood)
    {
        Test.AddInfo(FString::Printf(TEXT("%s: code %d, field '%s', message '%s'"), *Label,
            int32(Result.Error.Code), *Result.Error.Field, *Result.Error.Message));
    }
    return bGood;
}

bool ExpectBasicTiming(FAutomationTestBase& Test, const FString& Label, const FLDDemoContentSnapshot& Catalog,
    const FLDDemoRational& AttackSpeed, int64 IntervalMs, int64 WindupMs)
{
    FLDBasicTimingInput Input;
    Input.BaseAttackSpeed = Catalog.Get().Parameters.AttackSpeed.Base;
    Input.CurrentAttackSpeed = AttackSpeed;
    Input.BaseWindupMs = Catalog.Get().Parameters.BasicAttackWindupMs;
    const FLDBasicTimingResult Result = LDTactical::EvaluateBasicTiming(Input, Catalog);
    if (!Test.TestTrue(Label + TEXT(": projected attack speed feeds the basic timing evaluator"), Result.IsSuccess()))
    {
        Test.AddError(Result.Error.Field + TEXT(": ") + Result.Error.Message);
        return false;
    }
    bool bGood = Test.TestEqual(Label + TEXT(": interval ms"), Result.Value->IntervalMs, IntervalMs);
    bGood &= Test.TestEqual(Label + TEXT(": windup ms"), Result.Value->WindupMs, WindupMs);
    return bGood;
}

bool SameRational(const FLDDemoRational& A, const FLDDemoRational& B)
{
    return A.Numerator == B.Numerator && A.Denominator == B.Denominator;
}

bool SameTerms(const FLDAttributeTerms& A, const FLDAttributeTerms& B)
{
    return SameRational(A.Flat, B.Flat) && SameRational(A.AdditiveRatio, B.AdditiveRatio);
}

// Attributes and explanations are source-order independent.
bool SameAttributes(const FLDAttributeProjection& A, const FLDAttributeProjection& B)
{
    const FLDProjectedAttributes& L = A.Final;
    const FLDProjectedAttributes& R = B.Final;
    bool bSame = L.MaxHPP2 == R.MaxHPP2 && L.MaxMPP2 == R.MaxMPP2 && L.ADP2 == R.ADP2 && L.APP2 == R.APP2
        && L.ArmorP2 == R.ArmorP2 && L.MagicResistP2 == R.MagicResistP2 && SameRational(L.AttackSpeed, R.AttackSpeed)
        && SameRational(L.MoveSpeedCmPerSec, R.MoveSpeedCmPerSec) && SameRational(L.AttackRangeCm, R.AttackRangeCm)
        && SameRational(L.HPRegenPerSec, R.HPRegenPerSec) && SameRational(L.MPRegenPerSec, R.MPRegenPerSec)
        && L.CritChanceBp == R.CritChanceBp && L.CooldownHasteP2 == R.CooldownHasteP2
        && A.Contributions.Num() == B.Contributions.Num();
    for (int32 Index = 0; bSame && Index < A.Contributions.Num(); ++Index)
    {
        const FLDAttributeContributions& X = A.Contributions[Index];
        const FLDAttributeContributions& Y = B.Contributions[Index];
        bSame = X.Attribute == Y.Attribute && SameRational(X.Base, Y.Base) && SameTerms(X.LevelGrowth, Y.LevelGrowth)
            && SameTerms(X.PermanentChoices, Y.PermanentChoices) && SameTerms(X.Passives, Y.Passives)
            && SameRational(X.Final, Y.Final) && X.bClamped == Y.bClamped;
    }
    return bSame;
}

FLDResourceMaxChangeInput ResourceInput(ELDResourcePool Pool, int64 OldCurrentP2, int64 OldMaxP2, int64 NewMaxP2)
{
    FLDResourceMaxChangeInput Input;
    Input.Pool = Pool;
    Input.OldCurrentP2 = OldCurrentP2;
    Input.OldMaxP2 = OldMaxP2;
    Input.NewMaxP2 = NewMaxP2;
    return Input;
}

bool ExpectResource(FAutomationTestBase& Test, const FString& Label, const FLDDemoContentSnapshot& Catalog,
    ELDResourcePool Pool, int64 OldCurrentP2, int64 OldMaxP2, int64 NewMaxP2, int64 ExpectedP2)
{
    const FLDResourceMaxChangeResult Result = LDAttributeProjection::ResourceMaxChange(
        ResourceInput(Pool, OldCurrentP2, OldMaxP2, NewMaxP2), Catalog);
    if (!Test.TestTrue(Label + TEXT(": conversion succeeds"), Result.IsSuccess()))
    {
        Test.AddError(Label + TEXT(": ") + Result.Error.Field + TEXT(": ") + Result.Error.Message);
        return false;
    }
    bool bGood = Test.TestEqual(Label + TEXT(": new current P2"), Result.Value->NewCurrentP2, ExpectedP2);
    bGood &= Test.TestTrue(Label + TEXT(": success carries no error"),
        Result.Error.Code == EError::None && Result.Error.Field.IsEmpty() && Result.Error.Message.IsEmpty());
    return bGood;
}

bool RunApprovedLevels(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!ReadAttributeFixture(Test, Catalog))
    {
        return false;
    }
    FLDAttributeProjection Projection;
    if (!ProjectBuild(Test, TEXT("Level 1 character"), MakeBuild(1), *Catalog, Projection)
        || !ExpectAttributes(Test, TEXT("Level 1 character"), Projection.Final, ApprovedLevel(1)))
    {
        return false;
    }
    const TArray<FLDAttributeContributions>& Rows = Projection.Contributions;
    bool bGood = ExpectRational(Test, TEXT("Level 1 AD base"), Rows[int32(EAttribute::AD)].Base, MakeRational(70));
    bGood &= ExpectTerms(Test, TEXT("Level 1 has no AD growth"), Rows[int32(EAttribute::AD)].LevelGrowth,
        MakeRational(0), MakeRational(0));
    bGood &= ExpectBasicTiming(Test, TEXT("Level 1 attack speed"), *Catalog, Projection.Final.AttackSpeed, 1112, 250);

    if (!ProjectBuild(Test, TEXT("Level 2 character"), MakeBuild(2), *Catalog, Projection))
    {
        return false;
    }
    bGood &= Test.TestEqual(TEXT("Level 2 maximum HP is 678"), Projection.Final.MaxHPP2, int64(67800));
    for (const int32 Level : {4, 5, 6})
    {
        const FString Label = FString::Printf(TEXT("Level %d character"), Level);
        if (!ProjectBuild(Test, Label, MakeBuild(uint8(Level)), *Catalog, Projection))
        {
            return false;
        }
        bGood &= ExpectAttributes(Test, Label, Projection.Final, ApprovedLevel(Level));
    }

    if (!ProjectBuild(Test, TEXT("Level 10 character"), MakeBuild(10), *Catalog, Projection)
        || !ExpectAttributes(Test, TEXT("Level 10 character"), Projection.Final, ApprovedLevel(10)))
    {
        return false;
    }
    bGood &= ExpectRational(Test, TEXT("Level 10 maximum HP base"), Rows[int32(EAttribute::MaxHP)].Base, MakeRational(620));
    bGood &= ExpectTerms(Test, TEXT("Level 10 maximum HP growth"), Rows[int32(EAttribute::MaxHP)].LevelGrowth,
        MakeRational(522), MakeRational(0));
    bGood &= ExpectTerms(Test, TEXT("Level 10 AD growth"), Rows[int32(EAttribute::AD)].LevelGrowth,
        MakeRational(144, 5), MakeRational(0));
    bGood &= ExpectRational(Test, TEXT("Level 10 AD final"), Rows[int32(EAttribute::AD)].Final, MakeRational(494, 5));
    bGood &= ExpectTerms(Test, TEXT("Level 10 attack-speed growth is one additive ratio"),
        Rows[int32(EAttribute::AttackSpeed)].LevelGrowth, MakeRational(0), MakeRational(9, 50));
    bGood &= ExpectTerms(Test, TEXT("Level 10 HP regeneration growth"), Rows[int32(EAttribute::HPRegen)].LevelGrowth,
        MakeRational(27, 25), MakeRational(0));
    bGood &= ExpectTerms(Test, TEXT("Level 10 MP regeneration growth"), Rows[int32(EAttribute::MPRegen)].LevelGrowth,
        MakeRational(18, 25), MakeRational(0));
    for (const EAttribute Flat : {EAttribute::MoveSpeed, EAttribute::AttackRange, EAttribute::CritChance, EAttribute::CooldownHaste})
    {
        bGood &= ExpectTerms(Test, TEXT("Attributes without a level key do not grow"), Rows[int32(Flat)].LevelGrowth,
            MakeRational(0), MakeRational(0));
    }
    bGood &= ExpectBasicTiming(Test, TEXT("Level 10 attack speed"), *Catalog, Projection.Final.AttackSpeed, 942, 212);
    return bGood;
}

bool RunChoicesAndPassives(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!ReadAttributeFixture(Test, Catalog))
    {
        return false;
    }
    FLDAttributeProjection Projection;
    bool bGood = true;
    // #112 §4: nine copies of each composite card at level 10 (base AD 98.8, max HP 1142, max MP 478, resistances 36,
    // attack speed 0.9 x 1.18, HP regeneration 2.68); every attribute clamps to its catalog bound and says so.
    struct FCardCase
    {
        const TCHAR* Id;
        TFunction<void(FExpectedAttributes&)> Expect;
        EAttribute Attribute;
        FLDDemoRational ChoiceFlat;
        FLDDemoRational ChoiceRatio;
        FLDDemoRational PenaltyFlat;
        FLDDemoRational PenaltyRatio;
        TArray<EAttribute> Clamped;
    };
    const FLDDemoRational Zero = MakeRational(0);
    const TArray<FCardCase> Cards = {
        {TEXT("attr_protein"), [](FExpectedAttributes& E) { E.ADP2 = 15280; E.MaxHPP2 = 168200; },
            EAttribute::AD, MakeRational(54), Zero, Zero, Zero, {}},
        {TEXT("attr_fast_hands"), [](FExpectedAttributes& E) { E.AttackSpeed = MakeRational(1629, 1000); E.ADP2 = 13480; },
            EAttribute::AttackSpeed, Zero, MakeRational(63, 100), Zero, Zero, {}},
        {TEXT("attr_all_nighter"), [](FExpectedAttributes& E) { E.APP2 = 21600; E.CooldownHasteP2 = 6000; },
            EAttribute::AP, MakeRational(216), Zero, Zero, Zero, {EAttribute::CooldownHaste}},
        {TEXT("attr_canteen"), [](FExpectedAttributes& E) { E.MaxHPP2 = 186200; E.ArmorP2 = 8100; E.MagicResistP2 = 8100; },
            EAttribute::MagicResist, MakeRational(45), Zero, Zero, Zero, {}},
        {TEXT("attr_big_talk"), [](FExpectedAttributes& E) { E.APP2 = 18000; E.MaxHPP2 = 168200; },
            EAttribute::MaxHP, MakeRational(540), Zero, Zero, Zero, {}},
        {TEXT("attr_morning_rush"), [](FExpectedAttributes& E) { E.MoveSpeedCmPerSec = MakeRational(408); E.MaxHPP2 = 177200; },
            EAttribute::MoveSpeed, Zero, MakeRational(9, 25), Zero, Zero, {}},
        {TEXT("attr_hand_out"), [](FExpectedAttributes& E) { E.AttackRangeCm = MakeRational(670); E.AttackSpeed = MakeRational(1467, 1000); },
            EAttribute::AttackRange, MakeRational(450), Zero, Zero, Zero, {}},
        {TEXT("attr_wild_frenzy"), [](FExpectedAttributes& E)
            { E.ADP2 = 17980; E.AttackSpeed = MakeRational(1629, 1000); E.MaxHPP2 = 24200; },
            EAttribute::MaxHP, Zero, Zero, MakeRational(900), Zero, {}},
    };
    for (const FCardCase& Case : Cards)
    {
        const FString Label = FString::Printf(TEXT("Nine %s cards at level 10"), Case.Id);
        FExpectedAttributes Stacked = ApprovedLevel(10);
        Case.Expect(Stacked);
        if (!ProjectBuild(Test, Label, MakeBuild(10, {}, {FLDAttributeCardValue(Case.Id, 9)}), *Catalog, Projection))
        {
            return false;
        }
        bGood &= ExpectAttributes(Test, Label + TEXT(" stack every card"), Projection.Final, Stacked);
        const FLDAttributeContributions& Row = Projection.Contributions[int32(Case.Attribute)];
        bGood &= ExpectTerms(Test, Label + TEXT(" permanent-choice terms"), Row.PermanentChoices, Case.ChoiceFlat, Case.ChoiceRatio);
        bGood &= ExpectTerms(Test, Label + TEXT(" explicit deduction terms"), Row.Penalties, Case.PenaltyFlat, Case.PenaltyRatio);
        for (int32 Index = 0; Index < Projection.Contributions.Num(); ++Index)
        {
            bGood &= Test.TestEqual(FString::Printf(TEXT("%s: row %d clamp flag"), *Label, Index),
                Projection.Contributions[Index].bClamped, Case.Clamped.Contains(static_cast<EAttribute>(Index)));
        }
    }
    FExpectedAttributes Mixed = ApprovedLevel(6);
    Mixed.ADP2 = 9800;
    Mixed.MaxHPP2 = 111000;
    Mixed.ArmorP2 = 3300;
    Mixed.MagicResistP2 = 3300;
    Mixed.AttackRangeCm = MakeRational(320);
    Mixed.AttackSpeed = MakeRational(27, 25);
    if (!ProjectBuild(Test, TEXT("Level 6 mixed cards"), MakeBuild(6, {}, {FLDAttributeCardValue(TEXT("attr_protein"), 2),
        FLDAttributeCardValue(TEXT("attr_canteen"), 1), FLDAttributeCardValue(TEXT("attr_hand_out"), 2)}), *Catalog, Projection))
    {
        return false;
    }
    bGood &= ExpectAttributes(Test, TEXT("Level 6 mixed cards use all five choices"), Projection.Final, Mixed);
    FExpectedAttributes Quality = ApprovedLevel(2);
    Quality.ADP2 = 8580;
    Quality.MaxHPP2 = 80400;
    if (!ProjectBuild(Test, TEXT("Attribute quality scales positive terms by card instance"),
        MakeBuild(2, {}, {FLDAttributeCardValue(TEXT("attr_protein"), 1, ELDAttributeQuality::Green),
            FLDAttributeCardValue(TEXT("attr_protein"), 1, ELDAttributeQuality::Gold)}), *Catalog, Projection))
    {
        return false;
    }
    bGood &= ExpectAttributes(Test, TEXT("Green and gold protein use 0.9x and 1.2x"), Projection.Final, Quality);
    FExpectedAttributes WildQuality = ApprovedLevel(2);
    WildQuality.ADP2 = 9210;
    WildQuality.AttackSpeed = MakeRational(10503, 10000);
    WildQuality.MaxHPP2 = 47800;
    if (!ProjectBuild(Test, TEXT("Attribute quality never scales a negative cost"),
        MakeBuild(2, {}, {FLDAttributeCardValue(TEXT("attr_wild_frenzy"), 1, ELDAttributeQuality::Green),
            FLDAttributeCardValue(TEXT("attr_wild_frenzy"), 1, ELDAttributeQuality::Gold)}), *Catalog, Projection))
    {
        return false;
    }
    bGood &= ExpectAttributes(Test, TEXT("Two wild frenzy costs remain two full penalties"), Projection.Final, WildQuality);
    // Rank values replace lower ranks and never grow with level: passive_lucky_star grants 0.12/0.18 crit,
    // passive_iced_americano 20/30 cooldown haste beside a flat 2 MP per second.
    const int64 CritNumerators[] = {3, 9};
    const int64 CritDenominators[] = {25, 50};
    const int64 HasteFlat[] = {20, 30};
    for (uint8 Rank = 1; Rank <= 2; ++Rank)
    {
        const int32 Slot = int32(Rank) - 1;
        const FString HasteLabel = FString::Printf(TEXT("Iced americano rank %d at level 4"), int32(Rank));
        FExpectedAttributes Haste = ApprovedLevel(4);
        Haste.CooldownHasteP2 = HasteFlat[Slot] * 100;
        Haste.MPRegenPerSec = MakeRational(86, 25);
        if (!ProjectBuild(Test, HasteLabel, MakeBuild(4, {FLDLearnedPassiveValue(TEXT("passive_iced_americano"), Rank)}),
            *Catalog, Projection))
        {
            return false;
        }
        bGood &= ExpectAttributes(Test, HasteLabel, Projection.Final, Haste);
        bGood &= ExpectTerms(Test, HasteLabel + TEXT(" passive haste terms"),
            Projection.Contributions[int32(EAttribute::CooldownHaste)].Passives, MakeRational(HasteFlat[Slot]), MakeRational(0));
        bGood &= ExpectTerms(Test, HasteLabel + TEXT(" passive MP regeneration terms"),
            Projection.Contributions[int32(EAttribute::MPRegen)].Passives, MakeRational(2), MakeRational(0));

        const FString CritLabel = FString::Printf(TEXT("Lucky star rank %d at level 4"), int32(Rank));
        FExpectedAttributes Crit = ApprovedLevel(4);
        Crit.CritChanceBp = Slot == 0 ? 1200 : 1800;
        if (!ProjectBuild(Test, CritLabel, MakeBuild(4, {FLDLearnedPassiveValue(TEXT("passive_lucky_star"), Rank)}),
            *Catalog, Projection))
        {
            return false;
        }
        bGood &= ExpectAttributes(Test, CritLabel, Projection.Final, Crit);
        bGood &= ExpectTerms(Test, CritLabel + TEXT(" adds probability points"),
            Projection.Contributions[int32(EAttribute::CritChance)].Passives,
            MakeRational(CritNumerators[Slot], CritDenominators[Slot]), MakeRational(0));
    }

    FExpectedAttributes AllPassives = ApprovedLevel(10);
    AllPassives.CooldownHasteP2 = 3000;
    AllPassives.MPRegenPerSec = MakeRational(98, 25);
    AllPassives.CritChanceBp = 1800;
    FLDAttributeProjection Reversed;
    if (!ProjectBuild(Test, TEXT("Five rank-two passives"), MakeBuild(10, {
            FLDLearnedPassiveValue(TEXT("passive_iced_americano"), 2), FLDLearnedPassiveValue(TEXT("passive_lucky_star"), 2),
            FLDLearnedPassiveValue(TEXT("passive_freeloader"), 2), FLDLearnedPassiveValue(TEXT("passive_pressed"), 2),
            FLDLearnedPassiveValue(TEXT("passive_kick_down"), 2)}), *Catalog, Projection)
        || !ProjectBuild(Test, TEXT("The same passives reversed"), MakeBuild(10, {
            FLDLearnedPassiveValue(TEXT("passive_kick_down"), 2), FLDLearnedPassiveValue(TEXT("passive_pressed"), 2),
            FLDLearnedPassiveValue(TEXT("passive_freeloader"), 2), FLDLearnedPassiveValue(TEXT("passive_lucky_star"), 2),
            FLDLearnedPassiveValue(TEXT("passive_iced_americano"), 2)}), *Catalog, Reversed))
    {
        return false;
    }
    bGood &= ExpectAttributes(Test, TEXT("Five rank-two passives use all ten choices"), Projection.Final, AllPassives);
    bGood &= Test.TestTrue(TEXT("Passive order changes neither attributes nor explanations"), SameAttributes(Projection, Reversed));

    FExpectedAttributes Combined = ApprovedLevel(10);
    // 98.8 + 7 x 6 AD, 1142 + 7 x 60 HP, 0.9 x 1.18 attacks/s, 0.18 crit.
    Combined.ADP2 = 14080;
    Combined.MaxHPP2 = 156200;
    Combined.AttackSpeed = MakeRational(531, 500);
    Combined.CritChanceBp = 1800;
    if (!ProjectBuild(Test, TEXT("Lucky star and seven protein cards"),
        MakeBuild(10, {FLDLearnedPassiveValue(TEXT("passive_lucky_star"), 2)}, {FLDAttributeCardValue(TEXT("attr_protein"), 7)}),
        *Catalog, Projection))
    {
        return false;
    }
    bGood &= ExpectAttributes(Test, TEXT("Choices and a passive each count once"), Projection.Final, Combined);
    const FLDAttributeContributions& ADRow = Projection.Contributions[int32(EAttribute::AD)];
    bGood &= ExpectTerms(Test, TEXT("Combined AD permanent choices"), ADRow.PermanentChoices, MakeRational(42), MakeRational(0));
    const FLDAttributeContributions& SpeedRow = Projection.Contributions[int32(EAttribute::AttackSpeed)];
    bGood &= ExpectTerms(Test, TEXT("Combined attack-speed growth"), SpeedRow.LevelGrowth, MakeRational(0), MakeRational(9, 50));
    return bGood;
}

bool RunEnvelope(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!ReadAttributeFixture(Test, Catalog))
    {
        return false;
    }
    FLDAttributeProjection Projection;
    const auto Envelope = [&Test, &Catalog, &Projection](const TCHAR* Label, const FLDAttributeBuildValues& Build)
    {
        return ProjectBuild(Test, Label, Build, *Catalog, Projection);
    };
    // All ten choices (the opening one and nine level-ups) toward one coordinate at level 10.
    if (!Envelope(TEXT("Canteen envelope"), MakeBuild(10, {}, {FLDAttributeCardValue(TEXT("attr_canteen"), 10)})))
    {
        return false;
    }
    bool bGood = Test.TestEqual(TEXT("HP envelope is 1142 + 10 x 80"), Projection.Final.MaxHPP2, int64(194200));
    bGood &= Test.TestEqual(TEXT("Armor envelope is 36 + 10 x 5"), Projection.Final.ArmorP2, int64(8600));
    bGood &= Test.TestEqual(TEXT("Magic resist envelope is 36 + 10 x 5"), Projection.Final.MagicResistP2, int64(8600));
    if (!Envelope(TEXT("MP envelope"), MakeBuild(10, {}, {FLDAttributeCardValue(TEXT("attr_padded_thesis"), 10)})))
    {
        return false;
    }
    bGood &= Test.TestEqual(TEXT("MP envelope clamps 1478 to its cap 800"), Projection.Final.MaxMPP2, int64(80000));
    bGood &= Test.TestTrue(TEXT("The clamped MP row says so"), Projection.Contributions[int32(EAttribute::MaxMP)].bClamped);
    if (!Envelope(TEXT("AD envelope"), MakeBuild(10, {}, {FLDAttributeCardValue(TEXT("attr_wild_frenzy"), 10)})))
    {
        return false;
    }
    bGood &= Test.TestEqual(TEXT("AD envelope is 98.8 + 10 x 9"), Projection.Final.ADP2, int64(18880));
    bGood &= Test.TestFalse(TEXT("The AD row is below its cap"), Projection.Contributions[int32(EAttribute::AD)].bClamped);
    bGood &= Test.TestEqual(TEXT("The AD envelope pays its explicit max-HP deduction: 1142 - 1000"),
        Projection.Final.MaxHPP2, int64(14200));
    bGood &= ExpectRational(Test, TEXT("Attack-speed envelope is 0.9 x 1.88"), Projection.Final.AttackSpeed, MakeRational(423, 250));
    bGood &= ExpectBasicTiming(Test, TEXT("Attack-speed envelope"), *Catalog, Projection.Final.AttackSpeed, 592, 133);
    if (!Envelope(TEXT("AP envelope"), MakeBuild(10, {}, {FLDAttributeCardValue(TEXT("attr_all_nighter"), 10)})))
    {
        return false;
    }
    bGood &= Test.TestEqual(TEXT("AP envelope is 10 x 24"), Projection.Final.APP2, int64(24000));
    bGood &= Test.TestFalse(TEXT("The AP row is below its cap"), Projection.Contributions[int32(EAttribute::AP)].bClamped);
    bGood &= Test.TestEqual(TEXT("Haste envelope clamps 100 to its cap 60"), Projection.Final.CooldownHasteP2, int64(6000));
    bGood &= Test.TestTrue(TEXT("The clamped haste row says so"), Projection.Contributions[int32(EAttribute::CooldownHaste)].bClamped);
    if (!Envelope(TEXT("Move-speed envelope"), MakeBuild(10, {}, {FLDAttributeCardValue(TEXT("attr_morning_rush"), 10)})))
    {
        return false;
    }
    bGood &= ExpectRational(Test, TEXT("Move-speed envelope is 300 x 1.4"), Projection.Final.MoveSpeedCmPerSec, MakeRational(420));
    if (!Envelope(TEXT("Range envelope"), MakeBuild(10, {}, {FLDAttributeCardValue(TEXT("attr_hand_out"), 10)})))
    {
        return false;
    }
    bGood &= ExpectRational(Test, TEXT("Range envelope is 220 + 10 x 50"), Projection.Final.AttackRangeCm, MakeRational(720));
    if (!Envelope(TEXT("Crit envelope"), MakeBuild(10, {FLDLearnedPassiveValue(TEXT("passive_lucky_star"), 2)})))
    {
        return false;
    }
    bGood &= Test.TestEqual(TEXT("Crit envelope is 0.18"), Projection.Final.CritChanceBp, int64(1800));

    return bGood;
}

bool RunRefusals(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!ReadAttributeFixture(Test, Catalog))
    {
        return false;
    }
    const auto Refused = [&Test, &Catalog](const TCHAR* Label, const FLDAttributeBuildValues& Build, EError Code,
        const TCHAR* Field)
    {
        return ExpectRefusal(Test, Label, LDAttributeProjection::Project(Build, *Catalog), Code, Field);
    };
    const auto Passive = [](const TCHAR* Id, uint8 Rank) { return FLDLearnedPassiveValue(Id, Rank); };
    const auto Cards = [](const TCHAR* Id, uint8 Count) { return FLDAttributeCardValue(Id, Count); };
    bool bGood = Refused(TEXT("Level zero"), MakeBuild(0), EError::InputDomain, TEXT("Level"));
    bGood &= Refused(TEXT("Level seventeen"), MakeBuild(17), EError::InputDomain, TEXT("Level"));
    bGood &= Refused(TEXT("Level 255"), MakeBuild(255), EError::InputDomain, TEXT("Level"));
    bGood &= Refused(TEXT("Unknown passive"), MakeBuild(10, {Passive(TEXT("passive_missing"), 1)}),
        EError::UnknownDefinition, TEXT("LearnedPassives[0]"));
    bGood &= Refused(TEXT("A learned active carries no passive attributes"), MakeBuild(10, {Passive(TEXT("active_snark_bolt"), 1)}),
        EError::UnknownDefinition, TEXT("LearnedPassives[0]"));
    bGood &= Refused(TEXT("Duplicate learned passive"), MakeBuild(10, {Passive(TEXT("passive_lucky_star"), 1),
        Passive(TEXT("passive_lucky_star"), 1)}), EError::DuplicateSource, TEXT("LearnedPassives[1]"));
    bGood &= Refused(TEXT("Rank zero"), MakeBuild(10, {Passive(TEXT("passive_lucky_star"), 0)}),
        EError::InputDomain, TEXT("LearnedPassives[0]"));
    bGood &= Refused(TEXT("Rank three is above the two approved ranks"), MakeBuild(10, {Passive(TEXT("passive_lucky_star"), 3)}),
        EError::InputDomain, TEXT("LearnedPassives[0]"));
    bGood &= Refused(TEXT("Unknown attribute card"), MakeBuild(10, {}, {Cards(TEXT("attr_missing"), 1)}),
        EError::UnknownDefinition, TEXT("AttributeCards[0]"));
    bGood &= Refused(TEXT("Duplicate attribute card entry"), MakeBuild(10, {}, {Cards(TEXT("attr_protein"), 1),
        Cards(TEXT("attr_protein"), 1)}), EError::DuplicateSource, TEXT("AttributeCards[1]"));
    bGood &= Refused(TEXT("Case-folded IDs cannot duplicate the same attribute and quality"),
        MakeBuild(10, {}, {Cards(TEXT("attr_protein"), 1), Cards(TEXT("Attr_Protein"), 1)}),
        EError::DuplicateSource, TEXT("AttributeCards[1]"));
    bGood &= Refused(TEXT("Zero-count attribute card entry"), MakeBuild(10, {}, {Cards(TEXT("attr_protein"), 0)}),
        EError::InputDomain, TEXT("AttributeCards[0]"));
    for (const uint8 Quality : {uint8(0), uint8(5), uint8(255)})
    {
        const FString Label = FString::Printf(TEXT("Undefined attribute quality %u"), Quality);
        bGood &= Refused(*Label, MakeBuild(10, {}, {
            FLDAttributeCardValue(TEXT("attr_protein"), 1, static_cast<ELDAttributeQuality>(Quality))}),
            EError::InputDomain, TEXT("AttributeCards[0]"));
    }
    bGood &= Refused(TEXT("Level 1 holds only its opening choice: two cards exceed it"), MakeBuild(1, {}, {Cards(TEXT("attr_protein"), 2)}),
        EError::InputDomain, TEXT("ChoiceBudget"));
    bGood &= Refused(TEXT("Eleven cards exceed ten choices"), MakeBuild(10, {}, {Cards(TEXT("attr_protein"), 11)}),
        EError::InputDomain, TEXT("ChoiceBudget"));
    bGood &= Refused(TEXT("A rank-two passive plus nine cards exceed ten choices"),
        MakeBuild(10, {Passive(TEXT("passive_lucky_star"), 2)}, {Cards(TEXT("attr_protein"), 9)}), EError::InputDomain, TEXT("ChoiceBudget"));
    bGood &= Refused(TEXT("A rank-two passive at level 1 exceeds one choice"),
        MakeBuild(1, {Passive(TEXT("passive_iced_americano"), 2)}), EError::InputDomain, TEXT("ChoiceBudget"));
    bGood &= Refused(TEXT("Twenty-one AD cards are refused before any attribute bound"),
        MakeBuild(10, {}, {Cards(TEXT("attr_protein"), 21)}), EError::InputDomain, TEXT("ChoiceBudget"));
    FLDAttributeProjection Projection;
    for (const ELDAttributeQuality Quality : {ELDAttributeQuality::Green, ELDAttributeQuality::Blue,
        ELDAttributeQuality::Purple, ELDAttributeQuality::Gold})
    {
        const FString Label = FString::Printf(TEXT("Defined attribute quality %u"), uint8(Quality));
        bGood &= ProjectBuild(Test, Label, MakeBuild(1, {}, {
            FLDAttributeCardValue(TEXT("attr_protein"), 1, Quality)}), *Catalog, Projection);
    }
    bGood &= ProjectBuild(Test, TEXT("One attribute family may have distinct qualities, even with case-folded IDs"),
        MakeBuild(2, {}, {FLDAttributeCardValue(TEXT("attr_protein"), 1, ELDAttributeQuality::Green),
            FLDAttributeCardValue(TEXT("Attr_Protein"), 1, ELDAttributeQuality::Gold)}), *Catalog, Projection);
    bGood &= ProjectBuild(Test, TEXT("A level-1 build with its opening card"),
        MakeBuild(1, {}, {Cards(TEXT("attr_protein"), 1)}), *Catalog, Projection);
    bGood &= ProjectBuild(Test, TEXT("A build exactly at the visible choice bound"),
        MakeBuild(10, {Passive(TEXT("passive_lucky_star"), 2)}, {Cards(TEXT("attr_protein"), 8)}), *Catalog, Projection);
    bGood &= ProjectBuild(Test, TEXT("A rank-two passive at level 2"),
        MakeBuild(2, {Passive(TEXT("passive_iced_americano"), 2)}), *Catalog, Projection);
    return bGood;
}

bool RunResourceMaxChange(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!ReadAttributeFixture(Test, Catalog))
    {
        return false;
    }
    const auto Change = [&Catalog](const FLDResourceMaxChangeInput& Input)
    {
        return LDAttributeProjection::ResourceMaxChange(Input, *Catalog);
    };
    bool bGood = true;
    for (const ELDResourcePool Pool : {ELDResourcePool::HP, ELDResourcePool::MP})
    {
        const FString Name = Pool == ELDResourcePool::HP ? FString(TEXT("HP")) : FString(TEXT("MP"));
        // CONTENT_AND_BALANCE section 10.3: 40/100 -> 150 -> 30 -> 150 -> 100 returns to 40.
        bGood &= ExpectResource(Test, Name + TEXT(" 40/100 to max 150"), *Catalog, Pool, 4000, 10000, 15000, 6000);
        bGood &= ExpectResource(Test, Name + TEXT(" 60/150 to max 30"), *Catalog, Pool, 6000, 15000, 3000, 1200);
        bGood &= ExpectResource(Test, Name + TEXT(" 12/30 to max 150"), *Catalog, Pool, 1200, 3000, 15000, 6000);
        bGood &= ExpectResource(Test, Name + TEXT(" 60/150 back to max 100"), *Catalog, Pool, 6000, 15000, 10000, 4000);
        bGood &= ExpectResource(Test, Name + TEXT(" unchanged maximum"), *Catalog, Pool, 1234, 10000, 10000, 1234);
        bGood &= ExpectResource(Test, Name + TEXT(" full resource grows to the new maximum"), *Catalog, Pool, 62000, 62000, 67800, 67800);
        bGood &= ExpectResource(Test, Name + TEXT(" full resource shrinks to the new maximum"), *Catalog, Pool, 10000, 10000, 3000, 3000);
        bGood &= ExpectResource(Test, Name + TEXT(" zero current survives a shrink"), *Catalog, Pool, 0, 10000, 5000, 0);
        bGood &= ExpectResource(Test, Name + TEXT(" 0.02 of 100 halves to 0.01"), *Catalog, Pool, 2, 10000, 5000, 1);
        bGood &= ExpectResource(Test, Name + TEXT(" zero maximum, zero current and zero new maximum stay zero"),
            *Catalog, Pool, 0, 0, 0, 0);
        bGood &= ExpectRefusal(Test, Name + TEXT(" zero old maximum has no ratio for a positive new maximum"),
            Change(ResourceInput(Pool, 0, 0, 28000)), EError::NumericDomain, TEXT("OldMaxP2"));
        bGood &= ExpectRefusal(Test, Name + TEXT(" current above the old maximum"),
            Change(ResourceInput(Pool, 10001, 10000, 20000)), EError::InputDomain, TEXT("OldCurrentP2"));
        for (const FLDResourceMaxChangeInput& Negative : {ResourceInput(Pool, -1, 10000, 10000),
            ResourceInput(Pool, 0, -1, 10000), ResourceInput(Pool, 0, 10000, -1)})
        {
            bGood &= ExpectRefusal(Test, Name + TEXT(" negative P2 value"), Change(Negative), EError::InputDomain, TEXT("Resources"));
        }
        FLDResourceMaxChangeInput Missing = ResourceInput(Pool, 4000, 10000, 15000);
        Missing.OldCurrentP2.Reset();
        bGood &= ExpectRefusal(Test, Name + TEXT(" missing current"), Change(Missing), EError::MissingValue, TEXT("OldCurrentP2"));
        Missing = ResourceInput(Pool, 4000, 10000, 15000);
        Missing.OldMaxP2.Reset();
        bGood &= ExpectRefusal(Test, Name + TEXT(" missing old maximum"), Change(Missing), EError::MissingValue, TEXT("OldMaxP2"));
        Missing = ResourceInput(Pool, 4000, 10000, 15000);
        Missing.NewMaxP2.Reset();
        bGood &= ExpectRefusal(Test, Name + TEXT(" missing new maximum"), Change(Missing), EError::MissingValue, TEXT("NewMaxP2"));
    }
    bGood &= ExpectRefusal(Test, TEXT("Unset pool"), Change(FLDResourceMaxChangeInput()), EError::MissingValue, TEXT("Pool"));
    for (const uint8 Unknown : {uint8(3), uint8(255)})
    {
        bGood &= ExpectRefusal(Test, TEXT("Unknown pool"), Change(ResourceInput(static_cast<ELDResourcePool>(Unknown), 4000, 10000, 15000)),
            EError::InputDomain, TEXT("Pool"));
    }

    // HP refuses a positive value quantized to zero; MP accepts zero and is never lethal.
    bGood &= ExpectRefusal(Test, TEXT("HP 0.01/100 to max 50 underflows"),
        Change(ResourceInput(ELDResourcePool::HP, 1, 10000, 5000)), EError::NumericUnderflow, TEXT("NewMaxP2"));
    bGood &= ExpectRefusal(Test, TEXT("HP 0.01/800 to max 620 underflows"),
        Change(ResourceInput(ELDResourcePool::HP, 1, 80000, 62000)), EError::NumericUnderflow, TEXT("NewMaxP2"));
    bGood &= ExpectRefusal(Test, TEXT("Positive HP to a zero maximum underflows"),
        Change(ResourceInput(ELDResourcePool::HP, 5000, 10000, 0)), EError::NumericUnderflow, TEXT("NewMaxP2"));
    bGood &= ExpectResource(Test, TEXT("MP 0.01/100 to max 50 becomes zero"), *Catalog, ELDResourcePool::MP, 1, 10000, 5000, 0);
    bGood &= ExpectResource(Test, TEXT("MP 0.01/800 to max 620 becomes zero"), *Catalog, ELDResourcePool::MP, 1, 80000, 62000, 0);
    bGood &= ExpectResource(Test, TEXT("MP to a zero maximum becomes zero"), *Catalog, ELDResourcePool::MP, 5000, 10000, 0, 0);
    bGood &= ExpectResource(Test, TEXT("Zero HP to a zero maximum is not an underflow"), *Catalog, ELDResourcePool::HP, 0, 10000, 0, 0);

    FLDAttributeProjection LevelOne;
    FLDAttributeProjection LevelTwo;
    if (!ProjectBuild(Test, TEXT("Level 1 maximum HP"), MakeBuild(1), *Catalog, LevelOne)
        || !ProjectBuild(Test, TEXT("Level 2 maximum HP"), MakeBuild(2), *Catalog, LevelTwo))
    {
        return false;
    }
    bGood &= ExpectResource(Test, TEXT("Low-camp 583.94/620 HP after levelling becomes 638.56/678"), *Catalog,
        ELDResourcePool::HP, 58394, LevelOne.Final.MaxHPP2, LevelTwo.Final.MaxHPP2, 63856);

    // Native fast path and exact wide-product path, on both sides of the int64 product boundary.
    bGood &= ExpectResource(Test, TEXT("Largest native product floors exactly"), *Catalog, ELDResourcePool::HP,
        3037000499LL, 3037000500LL, 3037000499LL, 3037000498LL);
    bGood &= ExpectResource(Test, TEXT("First wide product floors exactly"), *Catalog, ELDResourcePool::HP,
        3037000500LL, 3037000501LL, 3037000500LL, 3037000499LL);
    bGood &= ExpectResource(Test, TEXT("Wide product with an exact quotient"), *Catalog, ELDResourcePool::MP,
        3000000000000LL, 4000000000000LL, 5000000000000LL, 3750000000000LL);
    bGood &= ExpectResource(Test, TEXT("Products near int64 max floor without rounding"), *Catalog, ELDResourcePool::MP,
        MAX_int64 - 1, MAX_int64, MAX_int64 - 2, MAX_int64 - 3);

    struct FMaximumPair { int64 From; int64 To; };
    const FMaximumPair Pairs[] = {{62000, 80000}, {80000, 62000}, {10000, 15000}, {15000, 3000}, {28000, 40000},
        {114200, 240200}, {240200, 114200}};
    for (const ELDResourcePool Pool : {ELDResourcePool::HP, ELDResourcePool::MP})
    {
        for (const FMaximumPair& Pair : Pairs)
        {
            const FString Label = FString::Printf(TEXT("%s round trip %lld -> %lld -> %lld"),
                Pool == ELDResourcePool::HP ? TEXT("HP") : TEXT("MP"), Pair.From, Pair.To, Pair.From);
            TArray<int64> Samples;
            for (int64 Value = 0; Value < FMath::Min<int64>(2000, Pair.From); ++Value)
            {
                Samples.Add(Value);
            }
            for (int64 Value = 2000; Value < Pair.From; Value += 997)
            {
                Samples.Add(Value);
            }
            Samples.Add(Pair.From);
            int64 Violations = 0;
            int64 Underflows = 0;
            FString FirstViolation;
            for (const int64 Current : Samples)
            {
                const FLDResourceMaxChangeResult Out = Change(ResourceInput(Pool, Current, Pair.From, Pair.To));
                const bool bForwardUnderflow = Pool == ELDResourcePool::HP && Current > 0 && Current * Pair.To / Pair.From == 0;
                if (!Out.IsSuccess())
                {
                    const bool bExpected = bForwardUnderflow && Out.Error.Code == EError::NumericUnderflow && !Out.Value.IsSet();
                    Underflows += bExpected ? 1 : 0;
                    Violations += bExpected ? 0 : 1;
                    continue;
                }
                const int64 Converted = Out.Value->NewCurrentP2;
                bool bViolation = bForwardUnderflow || Converted * Pair.From > Current * Pair.To;
                // A refused return trip is not a gain; only an HP underflow may refuse it.
                const FLDResourceMaxChangeResult Back = Change(ResourceInput(Pool, Converted, Pair.To, Pair.From));
                const bool bBackUnderflow = Pool == ELDResourcePool::HP && Converted > 0 && Converted * Pair.From / Pair.To == 0;
                if (Back.IsSuccess())
                {
                    const int64 Returned = Back.Value->NewCurrentP2;
                    bViolation = bViolation || bBackUnderflow || Returned > Current || Returned * Pair.To > Converted * Pair.From;
                }
                else
                {
                    bViolation = bViolation || !bBackUnderflow || Back.Error.Code != EError::NumericUnderflow;
                }
                if (bViolation && FirstViolation.IsEmpty())
                {
                    FirstViolation = FString::Printf(TEXT("current %lld"), Current);
                }
                Violations += bViolation ? 1 : 0;
            }
            bGood &= Test.TestTrue(Label + TEXT(": no step raises the ratio and no round trip gains P2"), Violations == 0);
            if (Violations != 0)
            {
                Test.AddInfo(Label + TEXT(": first violation at ") + FirstViolation);
            }
            const bool bShrinksHP = Pool == ELDResourcePool::HP && Pair.To < Pair.From;
            bGood &= Test.TestTrue(Label + TEXT(": underflow refusals occur only for a shrinking positive HP"),
                bShrinksHP ? Underflows > 0 : Underflows == 0);
        }
    }
    return bGood;
}

bool SameRational(const FLDDemoRational& Value, int64 Numerator, int64 Denominator)
{
    return Value.Denominator > 0 && Denominator > 0 && Value.Numerator * Denominator == Numerator * Value.Denominator;
}

FLDTemporaryAttributeTerms Terms(TArray<FLDDemoStatBonus> Bonuses, int64 SlowBp = 0)
{
    FLDTemporaryAttributeTerms Result;
    Result.Bonuses = MoveTemp(Bonuses);
    Result.MoveSlowBp = SlowBp;
    return Result;
}

FLDDemoStatBonus TempBonus(ELDDemoBonusStat Stat, int64 Amount)
{
    FLDDemoStatBonus Bonus;
    Bonus.Stat = Stat;
    Bonus.Amount = Amount;
    return Bonus;
}

bool Apply(FAutomationTestBase& Test, const FString& Label, const FLDAttributeProjection& Build,
    const FLDTemporaryAttributeTerms& Temporary, const FLDDemoContentSnapshot& Catalog, FLDAttributeProjection& Out)
{
    const FLDAttributeProjectionResult Result = LDAttributeProjection::ApplyTemporary(Build, Temporary, Catalog);
    if (!Test.TestTrue(Label + TEXT(": temporary projection succeeds"), Result.IsSuccess()))
    {
        Test.AddError(Label + TEXT(": ") + Result.Error.Field + TEXT(": ") + Result.Error.Message);
        return false;
    }
    Out = Result.Value.GetValue();
    return true;
}

bool RunTemporaryTerms(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!ReadAttributeFixture(Test, Catalog))
    {
        return false;
    }
    const FLDDemoContentParameters& P = Catalog->Get().Parameters;
    FLDAttributeProjection Build, Out;
    if (!ProjectBuild(Test, TEXT("Level 1 character"), MakeBuild(1), *Catalog, Build))
    {
        return false;
    }
    bool bGood = true;
    const FLDDemoRational& BaseSpeed = P.MoveSpeedCmPerSec.Base;
    const int32 MoveRow = int32(EAttribute::MoveSpeed);
    if (!Apply(Test, TEXT("No terms"), Build, FLDTemporaryAttributeTerms(), *Catalog, Out)) { return false; }
    bGood &= Test.TestTrue(TEXT("Empty terms keep the build projection"),
        Out.Final.ADP2 == Build.Final.ADP2 && SameRational(Out.Final.MoveSpeedCmPerSec, BaseSpeed.Numerator, BaseSpeed.Denominator));
    if (!Apply(Test, TEXT("Slow 30%"), Build, Terms({}, 3000), *Catalog, Out)) { return false; }
    bGood &= Test.TestTrue(TEXT("A 30% slow is a negative term of the same move-speed formula"),
        SameRational(Out.Final.MoveSpeedCmPerSec, BaseSpeed.Numerator * 7, BaseSpeed.Denominator * 10)
        && SameRational(Out.Contributions[MoveRow].Temporary.AdditiveRatio, -3, 10)
        && SameRational(Out.Contributions[MoveRow].Final, BaseSpeed.Numerator * 7, BaseSpeed.Denominator * 10));
    bGood &= Test.TestTrue(TEXT("Other attributes are untouched by a slow"), Out.Final.ADP2 == Build.Final.ADP2
        && SameRational(Out.Final.AttackSpeed, Build.Final.AttackSpeed.Numerator, Build.Final.AttackSpeed.Denominator));
    if (!Apply(Test, TEXT("Haste 50% and slow 30%"), Build,
        Terms({TempBonus(ELDDemoBonusStat::MoveSpeedBonusRatio, 5000)}, 3000), *Catalog, Out)) { return false; }
    bGood &= Test.TestTrue(TEXT("Timed move speed and a slow add in one ratio"),
        SameRational(Out.Final.MoveSpeedCmPerSec, BaseSpeed.Numerator * 12, BaseSpeed.Denominator * 10));
    if (!Apply(Test, TEXT("Slow 99.99%"), Build, Terms({}, 9999), *Catalog, Out)) { return false; }
    bGood &= Test.TestTrue(TEXT("A slow never takes move speed below its catalog minimum"),
        SameRational(Out.Final.MoveSpeedCmPerSec, P.MoveSpeedCmPerSec.Min.Numerator, P.MoveSpeedCmPerSec.Min.Denominator)
        && Out.Contributions[MoveRow].bClamped);
    if (!Apply(Test, TEXT("Attack speed 60%"), Build,
        Terms({TempBonus(ELDDemoBonusStat::AttackSpeedBonusRatio, 6000)}), *Catalog, Out)) { return false; }
    const FLDDemoRational& BaseRate = P.AttackSpeed.Base;
    bGood &= Test.TestTrue(TEXT("Timed attack speed adds to the build ratio"),
        SameRational(Out.Final.AttackSpeed, BaseRate.Numerator * (10000 + 6000), BaseRate.Denominator * 10000));
    if (!Apply(Test, TEXT("AD 22%"), Build, Terms({TempBonus(ELDDemoBonusStat::ADBonusRatio, 2200)}), *Catalog, Out)) { return false; }
    bGood &= Test.TestEqual(TEXT("A percent AD bonus floors to P2"), Out.Final.ADP2, Build.Final.ADP2 * 12200 / 10000);
    if (!Apply(Test, TEXT("Armor and resistance +20"), Build, Terms({TempBonus(ELDDemoBonusStat::ArmorFlat, 2000),
        TempBonus(ELDDemoBonusStat::MagicResistFlat, 2000)}), *Catalog, Out)) { return false; }
    bGood &= Test.TestTrue(TEXT("Timed flat resistances add once"),
        Out.Final.ArmorP2 == Build.Final.ArmorP2 + 2000 && Out.Final.MagicResistP2 == Build.Final.MagicResistP2 + 2000);
    if (!Apply(Test, TEXT("Armor beyond the cap"), Build, Terms({TempBonus(ELDDemoBonusStat::ArmorFlat, P.Armor.MaxP2)}), *Catalog, Out))
    {
        return false;
    }
    bGood &= Test.TestTrue(TEXT("A temporary term clamps to the catalog bound instead of refusing"),
        Out.Final.ArmorP2 == P.Armor.MaxP2 && Out.Contributions[int32(EAttribute::Armor)].bClamped);
    const auto Refused = [&Test, &Build, &Catalog](const FString& Label, const FLDTemporaryAttributeTerms& Bad)
    {
        const FLDAttributeProjectionResult Result = LDAttributeProjection::ApplyTemporary(Build, Bad, *Catalog);
        return Test.TestTrue(Label, !Result.IsSuccess() && !Result.Value.IsSet() && Result.Error.Code == EError::InputDomain);
    };
    bGood &= Refused(TEXT("A temporary resource maximum is refused"), Terms({TempBonus(ELDDemoBonusStat::MaxHPFlat, 100)}));
    bGood &= Refused(TEXT("A negative temporary bonus is refused"), Terms({TempBonus(ELDDemoBonusStat::ADFlat, -1)}));
    bGood &= Refused(TEXT("A full slow is refused"), Terms({}, 10000));
    FLDAttributeProjection Camp;
    Camp.Final.MoveSpeedCmPerSec = MakeRational(400);
    Camp.Final.ADP2 = 1000;
    if (!Apply(Test, TEXT("Camp member slow"), Camp, Terms({}, 3000), *Catalog, Out)) { return false; }
    bGood &= Test.TestTrue(TEXT("A body without contribution rows slows from its final speed"),
        SameRational(Out.Final.MoveSpeedCmPerSec, 280, 1) && Out.Final.ADP2 == 1000 && Out.Contributions.IsEmpty());
    return bGood;
}

bool RunExpansionPassives(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!ReadAttributeFixture(Test, Catalog))
    {
        return false;
    }
    FString Error;
    // #112: a rank may carry further effects (advanced additions); each passive has exactly one effect of its family.
    const auto RankEffect = [&Catalog, &Error](const TCHAR* Id, int32 Rank, ELDDemoEffectOp Op) -> const FLDDemoEffectDefinition*
    {
        const FLDDemoAbilityDefinition* Ability = nullptr;
        if (!Catalog->FindAbility(Id, Ability, Error)) { return nullptr; }
        const FLDDemoRankDefinition* Row = Ability->Ranks.FindByPredicate([Rank](const FLDDemoRankDefinition& R) { return R.Rank == Rank; });
        if (!Row) { return nullptr; }
        const FLDDemoEffectDefinition* Found = nullptr;
        for (const FLDDemoEffectDefinition& Effect : Row->Effects)
        {
            if (Effect.GetOp() != Op) { continue; }
            if (Found) { return nullptr; }
            Found = &Effect;
        }
        return Found;
    };
    const FLDDemoEffectDefinition* Pressed = RankEffect(TEXT("passive_pressed"), 2, ELDDemoEffectOp::LowHpStatBonus);
    const FLDDemoEffectDefinition* KickDown = RankEffect(TEXT("passive_kick_down"), 1, ELDDemoEffectOp::ControlDamageAmp);
    if (!Test.TestTrue(TEXT("Approved conditional passives have one effect of their family per rank: ") + Error,
        Pressed && KickDown))
    {
        return false;
    }
    const FLDDemoStatBonus* PressedSpeed = Pressed->Stats.FindByPredicate([](const FLDDemoStatBonus& Stat)
        { return Stat.Stat == ELDDemoBonusStat::AttackSpeedBonusRatio; });
    const FLDDemoStatBonus* PressedMove = Pressed->Stats.FindByPredicate([](const FLDDemoStatBonus& Stat)
        { return Stat.Stat == ELDDemoBonusStat::MoveSpeedBonusRatio; });
    if (!Test.TestTrue(TEXT("passive_pressed grants attack and move speed below its threshold"),
        PressedSpeed && PressedMove && Pressed->Stats.Num() == 2))
    {
        return false;
    }
    FLDAttributeProjection Plain, Projection;
    if (!ProjectBuild(Test, TEXT("Level 6"), MakeBuild(6), *Catalog, Plain)
        || !ProjectBuild(Test, TEXT("Level 6 conditional passives"), MakeBuild(6, {FLDLearnedPassiveValue(TEXT("passive_pressed"), 2),
            FLDLearnedPassiveValue(TEXT("passive_kick_down"), 1)}),
            *Catalog, Projection))
    {
        return false;
    }
    bool bGood = Test.TestTrue(TEXT("A conditional passive never changes the build attack or move speed"),
        SameRational(Projection.Final.AttackSpeed, Plain.Final.AttackSpeed.Numerator, Plain.Final.AttackSpeed.Denominator)
        && SameRational(Projection.Final.MoveSpeedCmPerSec, Plain.Final.MoveSpeedCmPerSec.Numerator, Plain.Final.MoveSpeedCmPerSec.Denominator));
    bGood &= Test.TestTrue(TEXT("The low-HP term carries its threshold, rank stats and advanced lifesteal"), Projection.LowHPBonus.IsSet()
        && Projection.LowHPBonus->ThresholdBp == Pressed->RatioBp && Projection.LowHPBonus->Rank == 2
        && Projection.LowHPBonus->Stats.Num() == 2 && Projection.LowHPBonus->BasicLifestealBp == Pressed->SecondaryRatioBp
        && Projection.LowHPBonus->BasicLifestealBp > 0);
    bGood &= Test.TestTrue(TEXT("The control amplification term carries its ratio"), Projection.ControlAmp.IsSet()
        && Projection.ControlAmp->RatioBp == KickDown->RatioBp);
    bGood &= Test.TestTrue(TEXT("A plain build carries no conditional terms"),
        !Plain.LowHPBonus.IsSet() && !Plain.ControlAmp.IsSet());
    FLDAttributeProjection Pushed;
    const FLDTemporaryAttributeTerms Low = LDTactical::TemporaryTerms({}, {}, Projection.LowHPBonus, 1, 100, 0);
    if (!Apply(Test, TEXT("Below the threshold"), Projection, Low, *Catalog, Pushed)) { return false; }
    const FLDDemoContentParameters& P = Catalog->Get().Parameters;
    const int64 LevelSpeedBp = 5 * P.AttackSpeedPerLevelBp;
    bGood &= Test.TestTrue(TEXT("Below the threshold attack speed adds the rank ratio to the level ratio"),
        SameRational(Pushed.Final.AttackSpeed, P.AttackSpeed.Base.Numerator * (10000 + LevelSpeedBp + PressedSpeed->Amount),
            P.AttackSpeed.Base.Denominator * 10000));
    bGood &= Test.TestTrue(TEXT("Below the threshold move speed rises by the rank ratio"),
        SameRational(Pushed.Final.MoveSpeedCmPerSec, P.MoveSpeedCmPerSec.Base.Numerator * (10000 + PressedMove->Amount),
            P.MoveSpeedCmPerSec.Base.Denominator * 10000));
    const FLDTemporaryAttributeTerms High = LDTactical::TemporaryTerms({}, {}, Projection.LowHPBonus, 100, 100, 0);
    bGood &= Test.TestTrue(TEXT("At full HP the low-HP term adds nothing"), High.Bonuses.IsEmpty());
    return bGood;
}
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoAttributeProjectionLevelsTest, "Lockdown.Rules.DemoAttributeProjection.ApprovedLevels",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoAttributeProjectionLevelsTest::RunTest(const FString& Parameters)
{
    return LDAttributeProjectionTests::RunApprovedLevels(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoAttributeProjectionChoicesTest, "Lockdown.Rules.DemoAttributeProjection.ChoicesAndPassives",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoAttributeProjectionChoicesTest::RunTest(const FString& Parameters)
{
    return LDAttributeProjectionTests::RunChoicesAndPassives(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoAttributeProjectionEnvelopeTest, "Lockdown.Rules.DemoAttributeProjection.Envelope",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoAttributeProjectionEnvelopeTest::RunTest(const FString& Parameters)
{
    return LDAttributeProjectionTests::RunEnvelope(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoAttributeProjectionRefusalsTest, "Lockdown.Rules.DemoAttributeProjection.Refusals",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoAttributeProjectionRefusalsTest::RunTest(const FString& Parameters)
{
    return LDAttributeProjectionTests::RunRefusals(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoAttributeProjectionResourceMaxChangeTest,
    "Lockdown.Rules.DemoAttributeProjection.ResourceMaxChange",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoAttributeProjectionResourceMaxChangeTest::RunTest(const FString& Parameters)
{
    return LDAttributeProjectionTests::RunResourceMaxChange(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoAttributeProjectionTemporaryTermsTest,
    "Lockdown.Rules.DemoAttributeProjection.TemporaryTerms",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoAttributeProjectionTemporaryTermsTest::RunTest(const FString& Parameters)
{
    return LDAttributeProjectionTests::RunTemporaryTerms(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoAttributeProjectionExpansionPassivesTest,
    "Lockdown.Rules.DemoAttributeProjection.ExpansionPassives",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoAttributeProjectionExpansionPassivesTest::RunTest(const FString& Parameters)
{
    return LDAttributeProjectionTests::RunExpansionPassives(*this);
}

#endif
