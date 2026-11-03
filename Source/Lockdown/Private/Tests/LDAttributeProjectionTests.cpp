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
        && P.AD.BaseP2 == 5400 && P.AD.PerLevelP2 == 320 && P.Armor.BaseP2 == 1800 && P.MagicResist.BaseP2 == 1800
        && P.AttackSpeed.Base.Numerator == 7 && P.AttackSpeed.Base.Denominator == 10 && P.AttackSpeedPerLevelBp == 200
        && P.MoveSpeedCmPerSec.Base.Numerator == 600 && P.MoveSpeedCmPerSec.Base.Denominator == 1
        && P.AttackRangeCm.Base.Numerator == 220 && P.AttackRangeCm.Base.Denominator == 1
        && Progression.MaxLevel == 10 && Progression.MaxChoices == 10 && Progression.MaxSkillRank == 3
        && Progression.EquipmentSlots == 2);
}

FLDDemoRational MakeRational(int64 Numerator, int64 Denominator = 1)
{
    FLDDemoRational Result;
    Result.Numerator = Numerator;
    Result.Denominator = Denominator;
    return Result;
}

FLDAttributeBuildValues MakeBuild(uint8 Level, TArray<FLDLearnedPassiveValue> Passives = {},
    TArray<FLDAttributeCardValue> Cards = {}, TArray<FString> Items = {})
{
    return FLDAttributeBuildValues(Level, MoveTemp(Passives), MoveTemp(Cards), MoveTemp(Items));
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
    Expected.MoveSpeedCmPerSec = MakeRational(600);
    Expected.AttackRangeCm = MakeRational(220);
    switch (Level)
    {
    case 1:
        Expected.MaxHPP2 = 62000; Expected.MaxMPP2 = 28000; Expected.ADP2 = 5400;
        Expected.ArmorP2 = 1800; Expected.MagicResistP2 = 1800; Expected.AttackSpeed = MakeRational(7, 10);
        Expected.HPRegenPerSec = MakeRational(8, 5); Expected.MPRegenPerSec = MakeRational(6, 5);
        break;
    case 4:
        Expected.MaxHPP2 = 79400; Expected.MaxMPP2 = 34600; Expected.ADP2 = 6360;
        Expected.ArmorP2 = 2400; Expected.MagicResistP2 = 2400; Expected.AttackSpeed = MakeRational(371, 500);
        Expected.HPRegenPerSec = MakeRational(49, 25); Expected.MPRegenPerSec = MakeRational(36, 25);
        break;
    case 5:
        Expected.MaxHPP2 = 85200; Expected.MaxMPP2 = 36800; Expected.ADP2 = 6680;
        Expected.ArmorP2 = 2600; Expected.MagicResistP2 = 2600; Expected.AttackSpeed = MakeRational(189, 250);
        Expected.HPRegenPerSec = MakeRational(52, 25); Expected.MPRegenPerSec = MakeRational(38, 25);
        break;
    case 6:
        Expected.MaxHPP2 = 91000; Expected.MaxMPP2 = 39000; Expected.ADP2 = 7000;
        Expected.ArmorP2 = 2800; Expected.MagicResistP2 = 2800; Expected.AttackSpeed = MakeRational(77, 100);
        Expected.HPRegenPerSec = MakeRational(11, 5); Expected.MPRegenPerSec = MakeRational(8, 5);
        break;
    case 10:
        Expected.MaxHPP2 = 114200; Expected.MaxMPP2 = 47800; Expected.ADP2 = 8280;
        Expected.ArmorP2 = 3600; Expected.MagicResistP2 = 3600; Expected.AttackSpeed = MakeRational(413, 500);
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
        const LDDemoContent::FExact Flat = N.Add(N.Add(N.Value(Row.LevelGrowth.Flat), N.Value(Row.PermanentChoices.Flat)),
            N.Add(N.Value(Row.Passives.Flat), N.Value(Row.Equipment.Flat)));
        const LDDemoContent::FExact Ratio = N.Add(
            N.Add(N.Value(Row.LevelGrowth.AdditiveRatio), N.Value(Row.PermanentChoices.AdditiveRatio)),
            N.Add(N.Value(Row.Passives.AdditiveRatio), N.Value(Row.Equipment.AdditiveRatio)));
        const bool bExplained = N.EQ(N.Mul(N.Add(N.Value(Row.Base), Flat), N.Add(N.Value(1), Ratio)), N.Value(Row.Final));
        bGood &= Test.TestTrue(What + TEXT(" keeps attribute order"), Row.Attribute == static_cast<EAttribute>(Index));
        bGood &= Test.TestTrue(What + TEXT(" final equals (base + flat) x (1 + additive ratio)"), bExplained);
        bGood &= Test.TestFalse(What + TEXT(" stays inside the approved clamp-free envelope"), Row.bClamped);
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

struct FExpectedFamily
{
    const TCHAR* Family;
    const TCHAR* EquipmentId;
    TArray<int32> EquippedIndices;
};

bool ExpectFamilies(FAutomationTestBase& Test, const FString& Label, const FLDAttributeProjection& Projection,
    const TArray<FExpectedFamily>& Expected)
{
    if (!Test.TestEqual(Label + TEXT(": each equipment passive family registers once"),
        Projection.EquipmentPassives.Num(), Expected.Num()))
    {
        return false;
    }
    bool bGood = true;
    for (int32 Index = 0; Index < Expected.Num(); ++Index)
    {
        const FLDEquipmentPassiveFamily& Actual = Projection.EquipmentPassives[Index];
        const FString What = FString::Printf(TEXT("%s: passive family %d"), *Label, Index);
        bGood &= Test.TestEqual(What + TEXT(" id"), Actual.Family, FString(Expected[Index].Family));
        bGood &= Test.TestEqual(What + TEXT(" equipment definition"), Actual.EquipmentId, FString(Expected[Index].EquipmentId));
        bGood &= Test.TestTrue(What + TEXT(" lists every contributing equipped copy"),
            Actual.EquippedIndices == Expected[Index].EquippedIndices);
    }
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

// Attributes and explanations are slot-order independent; only family indices name slots.
bool SameAttributes(const FLDAttributeProjection& A, const FLDAttributeProjection& B)
{
    const FLDProjectedAttributes& L = A.Final;
    const FLDProjectedAttributes& R = B.Final;
    bool bSame = L.MaxHPP2 == R.MaxHPP2 && L.MaxMPP2 == R.MaxMPP2 && L.ADP2 == R.ADP2 && L.APP2 == R.APP2
        && L.ArmorP2 == R.ArmorP2 && L.MagicResistP2 == R.MagicResistP2 && SameRational(L.AttackSpeed, R.AttackSpeed)
        && SameRational(L.MoveSpeedCmPerSec, R.MoveSpeedCmPerSec) && SameRational(L.AttackRangeCm, R.AttackRangeCm)
        && SameRational(L.HPRegenPerSec, R.HPRegenPerSec) && SameRational(L.MPRegenPerSec, R.MPRegenPerSec)
        && L.CritChanceBp == R.CritChanceBp && L.CooldownHasteP2 == R.CooldownHasteP2
        && A.Contributions.Num() == B.Contributions.Num()
        && A.OutOfCombatHPRegen.IsSet() == B.OutOfCombatHPRegen.IsSet()
        && A.EquipmentPassives.Num() == B.EquipmentPassives.Num();
    for (int32 Index = 0; bSame && Index < A.Contributions.Num(); ++Index)
    {
        const FLDAttributeContributions& X = A.Contributions[Index];
        const FLDAttributeContributions& Y = B.Contributions[Index];
        bSame = X.Attribute == Y.Attribute && SameRational(X.Base, Y.Base) && SameTerms(X.LevelGrowth, Y.LevelGrowth)
            && SameTerms(X.PermanentChoices, Y.PermanentChoices) && SameTerms(X.Passives, Y.Passives)
            && SameTerms(X.Equipment, Y.Equipment) && SameRational(X.Final, Y.Final) && X.bClamped == Y.bClamped;
    }
    for (int32 Index = 0; bSame && Index < A.EquipmentPassives.Num(); ++Index)
    {
        bSame = A.EquipmentPassives[Index].Family.Equals(B.EquipmentPassives[Index].Family, ESearchCase::CaseSensitive)
            && A.EquipmentPassives[Index].EquipmentId.Equals(B.EquipmentPassives[Index].EquipmentId, ESearchCase::CaseSensitive)
            && A.EquipmentPassives[Index].EquippedIndices.Num() == B.EquipmentPassives[Index].EquippedIndices.Num();
    }
    if (bSame && A.OutOfCombatHPRegen.IsSet())
    {
        const FLDOutOfCombatRegenTerm& X = A.OutOfCombatHPRegen.GetValue();
        const FLDOutOfCombatRegenTerm& Y = B.OutOfCombatHPRegen.GetValue();
        bSame = X.AbilityId.Equals(Y.AbilityId, ESearchCase::CaseSensitive) && X.Rank == Y.Rank
            && X.DelayMs == Y.DelayMs && SameRational(X.HPPerSec, Y.HPPerSec);
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
    bool bGood = Test.TestFalse(TEXT("No conditional regeneration without a learned passive"),
        Projection.OutOfCombatHPRegen.IsSet());
    bGood &= Test.TestEqual(TEXT("No equipment passive family without equipment"), Projection.EquipmentPassives.Num(), 0);
    bGood &= ExpectRational(Test, TEXT("Level 1 AD base"), Rows[int32(EAttribute::AD)].Base, MakeRational(54));
    bGood &= ExpectTerms(Test, TEXT("Level 1 has no AD growth"), Rows[int32(EAttribute::AD)].LevelGrowth,
        MakeRational(0), MakeRational(0));
    bGood &= ExpectBasicTiming(Test, TEXT("Level 1 attack speed"), *Catalog, Projection.Final.AttackSpeed, 1429, 250);

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
    bGood &= ExpectRational(Test, TEXT("Level 10 AD final"), Rows[int32(EAttribute::AD)].Final, MakeRational(414, 5));
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
    bGood &= ExpectBasicTiming(Test, TEXT("Level 10 attack speed"), *Catalog, Projection.Final.AttackSpeed, 1211, 212);
    return bGood;
}

bool RunDuplicateEquipment(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!ReadAttributeFixture(Test, Catalog))
    {
        return false;
    }
    FLDAttributeProjection Projection;
    FExpectedAttributes Expected = ApprovedLevel(1);
    Expected.ADP2 = 8200;
    Expected.AttackRangeCm = MakeRational(300);
    if (!ProjectBuild(Test, TEXT("Two echo mauls"), MakeBuild(1, {}, {}, {TEXT("item_echo_maul"), TEXT("item_echo_maul")}),
            *Catalog, Projection)
        || !ExpectAttributes(Test, TEXT("Two echo mauls add both fixed stat blocks"), Projection.Final, Expected))
    {
        return false;
    }
    bool bGood = ExpectFamilies(Test, TEXT("Two echo mauls"), Projection,
        {{TEXT("passive_cleave"), TEXT("item_echo_maul"), {0, 1}}});
    bGood &= ExpectTerms(Test, TEXT("Two echo mauls AD"), Projection.Contributions[int32(EAttribute::AD)].Equipment,
        MakeRational(28), MakeRational(0));
    bGood &= ExpectTerms(Test, TEXT("Two echo mauls range"), Projection.Contributions[int32(EAttribute::AttackRange)].Equipment,
        MakeRational(80), MakeRational(0));
    bGood &= ExpectTerms(Test, TEXT("Equipment passives add no attribute terms"),
        Projection.Contributions[int32(EAttribute::AD)].Passives, MakeRational(0), MakeRational(0));

    Expected.ADP2 = 6800;
    Expected.AttackRangeCm = MakeRational(260);
    if (!ProjectBuild(Test, TEXT("One remaining echo maul"), MakeBuild(1, {}, {}, {TEXT("item_echo_maul")}), *Catalog, Projection))
    {
        return false;
    }
    bGood &= ExpectAttributes(Test, TEXT("Selling one maul removes only its stats"), Projection.Final, Expected);
    bGood &= ExpectFamilies(Test, TEXT("The remaining maul keeps the cleave family"), Projection,
        {{TEXT("passive_cleave"), TEXT("item_echo_maul"), {0}}});

    Expected = ApprovedLevel(1);
    Expected.ADP2 = 7800;
    Expected.MagicResistP2 = 4200;
    if (!ProjectBuild(Test, TEXT("Two siphon emblems"),
        MakeBuild(1, {}, {}, {TEXT("item_siphon_emblem"), TEXT("item_siphon_emblem")}), *Catalog, Projection))
    {
        return false;
    }
    bGood &= ExpectAttributes(Test, TEXT("Two siphon emblems"), Projection.Final, Expected);
    bGood &= ExpectFamilies(Test, TEXT("Two siphon emblems"), Projection,
        {{TEXT("passive_siphon"), TEXT("item_siphon_emblem"), {0, 1}}});

    Expected = ApprovedLevel(1);
    Expected.ADP2 = 8000;
    Expected.MagicResistP2 = 3000;
    Expected.AttackRangeCm = MakeRational(260);
    FLDAttributeProjection Swapped;
    if (!ProjectBuild(Test, TEXT("Maul and emblem"),
            MakeBuild(1, {}, {}, {TEXT("item_echo_maul"), TEXT("item_siphon_emblem")}), *Catalog, Projection)
        || !ProjectBuild(Test, TEXT("Emblem and maul"),
            MakeBuild(1, {}, {}, {TEXT("item_siphon_emblem"), TEXT("item_echo_maul")}), *Catalog, Swapped))
    {
        return false;
    }
    bGood &= ExpectAttributes(Test, TEXT("Maul and emblem reach the documented 80 AD"), Projection.Final, Expected);
    bGood &= ExpectFamilies(Test, TEXT("Maul and emblem"), Projection,
        {{TEXT("passive_cleave"), TEXT("item_echo_maul"), {0}}, {TEXT("passive_siphon"), TEXT("item_siphon_emblem"), {1}}});
    bGood &= ExpectFamilies(Test, TEXT("Emblem and maul"), Swapped,
        {{TEXT("passive_cleave"), TEXT("item_echo_maul"), {1}}, {TEXT("passive_siphon"), TEXT("item_siphon_emblem"), {0}}});
    bGood &= Test.TestTrue(TEXT("Slot order changes neither attributes nor explanations"), SameAttributes(Projection, Swapped));

    FString Error;
    const FLDDemoEquipmentDefinition* Maul = nullptr;
    if (!Catalog->FindEquipment(TEXT("item_echo_maul"), Maul, Error))
    {
        Test.AddError(Error);
        return false;
    }
    const FLDDemoEffectDefinition* Cleave = Maul->Passive.Effects.FindByPredicate(
        [](const FLDDemoEffectDefinition& Effect) { return Effect.GetOp() == ELDDemoEffectOp::Cleave; });
    if (!Test.TestNotNull(TEXT("The compiled maul supplies the cleave coefficient"), Cleave))
    {
        return false;
    }
    FLDDamageEvaluationInput Primary;
    Primary.Definition.Emplace(0, Catalog->Get().Character.BasicADRatioBp, 0, Catalog->Get().Character.BasicDamageType);
    Primary.HitClass = ELDCombatHitClass::BasicPrimary;
    Primary.bCritical = false;
    Primary.SourceADP2 = Projection.Final.ADP2;
    Primary.TargetResistanceP2 = int64(800);
    Primary.TargetHPP2 = int64(9000);
    Primary.TargetShieldP2 = int64(0);
    Primary.bTargetProtected = false;
    FLDDamageEvaluationInput Secondary = Primary;
    Secondary.Definition.Emplace(0, Cleave->RatioBp, 0, ELDDemoDamageType::Physical);
    Secondary.HitClass = ELDCombatHitClass::BasicSecondary;
    const FLDDamageEvaluationResult PrimaryHit = LDTactical::EvaluateDamage(Primary, *Catalog);
    const FLDDamageEvaluationResult SecondaryHit = LDTactical::EvaluateDamage(Secondary, *Catalog);
    if (!Test.TestTrue(TEXT("Projected AD feeds the shared damage evaluator"), PrimaryHit.IsSuccess() && SecondaryHit.IsSuccess()))
    {
        return false;
    }
    bGood &= Test.TestEqual(TEXT("80 AD primary hit against 8 armor is 74.07"), PrimaryHit.Value->DamageP2, int64(7407));
    bGood &= Test.TestEqual(TEXT("80 AD cleave against 8 armor is 25.92"), SecondaryHit.Value->DamageP2, int64(2592));

    struct FDuplicateCase
    {
        const TCHAR* Item;
        int64 MaxHPP2;
        int64 MaxMPP2;
        int64 ADP2;
        int64 APP2;
        int64 ArmorP2;
        int64 MagicResistP2;
        int64 AttackSpeedNumerator;
        int64 AttackSpeedDenominator;
        int64 MoveSpeed;
        int64 CooldownHasteP2;
    };
    for (const FDuplicateCase& Case : {
        FDuplicateCase{TEXT("item_iron_plate"), 98000, 28000, 5400, 0, 4800, 1800, 7, 10, 600, 0},
        FDuplicateCase{TEXT("item_edge_blade"), 62000, 28000, 9400, 0, 1800, 1800, 21, 25, 600, 0},
        FDuplicateCase{TEXT("item_runic_focus"), 62000, 28000, 5400, 6000, 1800, 1800, 7, 10, 600, 3000},
        FDuplicateCase{TEXT("item_phase_anchor"), 62000, 28000, 5400, 0, 1800, 1800, 7, 10, 744, 0},
        FDuplicateCase{TEXT("item_arcane_battery"), 62000, 52000, 5400, 0, 1800, 3800, 7, 10, 600, 0}})
    {
        const FString Label = FString::Printf(TEXT("Two %s"), Case.Item);
        Expected = ApprovedLevel(1);
        Expected.MaxHPP2 = Case.MaxHPP2;
        Expected.MaxMPP2 = Case.MaxMPP2;
        Expected.ADP2 = Case.ADP2;
        Expected.APP2 = Case.APP2;
        Expected.ArmorP2 = Case.ArmorP2;
        Expected.MagicResistP2 = Case.MagicResistP2;
        Expected.AttackSpeed = MakeRational(Case.AttackSpeedNumerator, Case.AttackSpeedDenominator);
        Expected.MoveSpeedCmPerSec = MakeRational(Case.MoveSpeed);
        Expected.CooldownHasteP2 = Case.CooldownHasteP2;
        if (!ProjectBuild(Test, Label, MakeBuild(1, {}, {}, {Case.Item, Case.Item}), *Catalog, Projection))
        {
            return false;
        }
        bGood &= ExpectAttributes(Test, Label + TEXT(" add every fixed stat per copy"), Projection.Final, Expected);
        bGood &= ExpectFamilies(Test, Label + TEXT(" without passives"), Projection, {});
    }
    if (!ProjectBuild(Test, TEXT("Two phase anchors"),
        MakeBuild(1, {}, {}, {TEXT("item_phase_anchor"), TEXT("item_phase_anchor")}), *Catalog, Projection))
    {
        return false;
    }
    bGood &= ExpectTerms(Test, TEXT("Two phase anchors move-speed terms"),
        Projection.Contributions[int32(EAttribute::MoveSpeed)].Equipment, MakeRational(0), MakeRational(6, 25));
    if (!ProjectBuild(Test, TEXT("Two edge blades"),
        MakeBuild(1, {}, {}, {TEXT("item_edge_blade"), TEXT("item_edge_blade")}), *Catalog, Projection))
    {
        return false;
    }
    bGood &= ExpectBasicTiming(Test, TEXT("Two edge blades"), *Catalog, Projection.Final.AttackSpeed, 1191, 209);
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
    struct FCardCase
    {
        const TCHAR* Id;
        int64 ADP2;
        int64 APP2;
        int64 MaxHPP2;
        int64 ResistP2;
        int64 RangeCm;
        EAttribute Attribute;
        int64 ChoiceFlat;
    };
    for (const FCardCase& Case : {
        FCardCase{TEXT("attr_might"), 15480, 0, 114200, 3600, 220, EAttribute::AD, 72},
        FCardCase{TEXT("attr_focus"), 8280, 14400, 114200, 3600, 220, EAttribute::AP, 144},
        FCardCase{TEXT("attr_vitality"), 8280, 0, 204200, 3600, 220, EAttribute::MaxHP, 900},
        FCardCase{TEXT("attr_resolve"), 8280, 0, 114200, 9000, 220, EAttribute::MagicResist, 54},
        FCardCase{TEXT("attr_reach"), 8280, 0, 114200, 3600, 580, EAttribute::AttackRange, 360}})
    {
        const FString Label = FString::Printf(TEXT("Nine %s cards at level 10"), Case.Id);
        FExpectedAttributes Stacked = ApprovedLevel(10);
        Stacked.ADP2 = Case.ADP2;
        Stacked.APP2 = Case.APP2;
        Stacked.MaxHPP2 = Case.MaxHPP2;
        Stacked.ArmorP2 = Case.ResistP2;
        Stacked.MagicResistP2 = Case.ResistP2;
        Stacked.AttackRangeCm = MakeRational(Case.RangeCm);
        if (!ProjectBuild(Test, Label, MakeBuild(10, {}, {FLDAttributeCardValue(Case.Id, 9)}), *Catalog, Projection))
        {
            return false;
        }
        bGood &= ExpectAttributes(Test, Label + TEXT(" stack every card"), Projection.Final, Stacked);
        bGood &= ExpectTerms(Test, Label + TEXT(" permanent-choice terms"),
            Projection.Contributions[int32(Case.Attribute)].PermanentChoices, MakeRational(Case.ChoiceFlat), MakeRational(0));
    }
    FExpectedAttributes Mixed = ApprovedLevel(6);
    Mixed.ADP2 = 8600;
    Mixed.ArmorP2 = 3400;
    Mixed.MagicResistP2 = 3400;
    Mixed.AttackRangeCm = MakeRational(300);
    if (!ProjectBuild(Test, TEXT("Level 6 mixed cards"), MakeBuild(6, {}, {FLDAttributeCardValue(TEXT("attr_might"), 2),
        FLDAttributeCardValue(TEXT("attr_resolve"), 1), FLDAttributeCardValue(TEXT("attr_reach"), 2)}), *Catalog, Projection))
    {
        return false;
    }
    bGood &= ExpectAttributes(Test, TEXT("Level 6 mixed cards use all five choices"), Projection.Final, Mixed);

    // Rank values replace lower ranks: 0.08/0.14/0.20 attack speed and crit, 3/5/7 HP per second.
    const int64 RankRatioNumerators[] = {2, 7, 1};
    const int64 RankRatioDenominators[] = {25, 50, 5};
    const int64 RhythmSpeedNumerators[] = {399, 21, 441};
    const int64 RhythmSpeedDenominators[] = {500, 25, 500};
    const int64 SecondWindRates[] = {3, 5, 7};
    for (uint8 Rank = 1; Rank <= 3; ++Rank)
    {
        const int32 Slot = int32(Rank) - 1;
        const FLDDemoRational RankRatio = MakeRational(RankRatioNumerators[Slot], RankRatioDenominators[Slot]);
        const FString RhythmLabel = FString::Printf(TEXT("Battle rhythm rank %d at level 4"), int32(Rank));
        FExpectedAttributes Rhythm = ApprovedLevel(4);
        Rhythm.AttackSpeed = MakeRational(RhythmSpeedNumerators[Slot], RhythmSpeedDenominators[Slot]);
        if (!ProjectBuild(Test, RhythmLabel, MakeBuild(4, {FLDLearnedPassiveValue(TEXT("passive_battle_rhythm"), Rank)}),
            *Catalog, Projection))
        {
            return false;
        }
        bGood &= ExpectAttributes(Test, RhythmLabel, Projection.Final, Rhythm);
        bGood &= ExpectTerms(Test, RhythmLabel + TEXT(" passive terms"),
            Projection.Contributions[int32(EAttribute::AttackSpeed)].Passives, MakeRational(0), RankRatio);

        const FString PrecisionLabel = FString::Printf(TEXT("Precision rank %d at level 4"), int32(Rank));
        FExpectedAttributes Precision = ApprovedLevel(4);
        Precision.CritChanceBp = int64(800 + 600 * Slot);
        if (!ProjectBuild(Test, PrecisionLabel, MakeBuild(4, {FLDLearnedPassiveValue(TEXT("passive_precision"), Rank)}),
            *Catalog, Projection))
        {
            return false;
        }
        bGood &= ExpectAttributes(Test, PrecisionLabel, Projection.Final, Precision);
        bGood &= ExpectTerms(Test, PrecisionLabel + TEXT(" adds probability points"),
            Projection.Contributions[int32(EAttribute::CritChance)].Passives, RankRatio, MakeRational(0));

        const FString WindLabel = FString::Printf(TEXT("Second wind rank %d at level 4"), int32(Rank));
        if (!ProjectBuild(Test, WindLabel, MakeBuild(4, {FLDLearnedPassiveValue(TEXT("passive_second_wind"), Rank)}),
            *Catalog, Projection))
        {
            return false;
        }
        bGood &= ExpectAttributes(Test, WindLabel + TEXT(" keeps the always-on attributes"), Projection.Final, ApprovedLevel(4));
        if (!Test.TestTrue(WindLabel + TEXT(" reports its conditional regeneration"), Projection.OutOfCombatHPRegen.IsSet()))
        {
            return false;
        }
        const FLDOutOfCombatRegenTerm& Wind = Projection.OutOfCombatHPRegen.GetValue();
        bGood &= Test.TestEqual(WindLabel + TEXT(" source"), Wind.AbilityId, FString(TEXT("passive_second_wind")));
        bGood &= Test.TestEqual(WindLabel + TEXT(" rank"), int32(Wind.Rank), int32(Rank));
        bGood &= Test.TestEqual(WindLabel + TEXT(" delay"), Wind.DelayMs, int64(6000));
        bGood &= ExpectRational(Test, WindLabel + TEXT(" bonus rate"), Wind.HPPerSec, MakeRational(SecondWindRates[Slot]));
        bGood &= ExpectTerms(Test, WindLabel + TEXT(" is not folded into always-on regeneration"),
            Projection.Contributions[int32(EAttribute::HPRegen)].Passives, MakeRational(0), MakeRational(0));
    }

    FExpectedAttributes AllPassives = ApprovedLevel(10);
    AllPassives.AttackSpeed = MakeRational(483, 500);
    AllPassives.CritChanceBp = 2000;
    FLDAttributeProjection Reversed;
    if (!ProjectBuild(Test, TEXT("Three rank-three passives"), MakeBuild(10, {
            FLDLearnedPassiveValue(TEXT("passive_battle_rhythm"), 3), FLDLearnedPassiveValue(TEXT("passive_precision"), 3),
            FLDLearnedPassiveValue(TEXT("passive_second_wind"), 3)}), *Catalog, Projection)
        || !ProjectBuild(Test, TEXT("The same passives reversed"), MakeBuild(10, {
            FLDLearnedPassiveValue(TEXT("passive_second_wind"), 3), FLDLearnedPassiveValue(TEXT("passive_precision"), 3),
            FLDLearnedPassiveValue(TEXT("passive_battle_rhythm"), 3)}), *Catalog, Reversed))
    {
        return false;
    }
    bGood &= ExpectAttributes(Test, TEXT("Three rank-three passives use nine of the ten choices"), Projection.Final, AllPassives);
    bGood &= Test.TestTrue(TEXT("Passive order changes neither attributes nor explanations"), SameAttributes(Projection, Reversed));

    FExpectedAttributes Combined = ApprovedLevel(10);
    Combined.ADP2 = 17880;
    Combined.AttackSpeed = MakeRational(483, 500);
    Combined.CritChanceBp = 1400;
    if (!ProjectBuild(Test, TEXT("Precision, seven might cards and two edge blades"),
        MakeBuild(10, {FLDLearnedPassiveValue(TEXT("passive_precision"), 2)}, {FLDAttributeCardValue(TEXT("attr_might"), 7)},
            {TEXT("item_edge_blade"), TEXT("item_edge_blade")}), *Catalog, Projection))
    {
        return false;
    }
    bGood &= ExpectAttributes(Test, TEXT("Choices, a passive and duplicate equipment each count once"), Projection.Final, Combined);
    const FLDAttributeContributions& ADRow = Projection.Contributions[int32(EAttribute::AD)];
    bGood &= ExpectTerms(Test, TEXT("Combined AD permanent choices"), ADRow.PermanentChoices, MakeRational(56), MakeRational(0));
    bGood &= ExpectTerms(Test, TEXT("Combined AD equipment"), ADRow.Equipment, MakeRational(40), MakeRational(0));
    const FLDAttributeContributions& SpeedRow = Projection.Contributions[int32(EAttribute::AttackSpeed)];
    bGood &= ExpectTerms(Test, TEXT("Combined attack-speed growth"), SpeedRow.LevelGrowth, MakeRational(0), MakeRational(9, 50));
    bGood &= ExpectTerms(Test, TEXT("Combined attack-speed equipment"), SpeedRow.Equipment, MakeRational(0), MakeRational(1, 5));
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
    // CONTENT_AND_BALANCE section 3.2: all ten choices (the opening one and nine level-ups) toward one coordinate plus its best two items.
    if (!Envelope(TEXT("HP envelope"), MakeBuild(10, {}, {FLDAttributeCardValue(TEXT("attr_vitality"), 10)},
        {TEXT("item_iron_plate"), TEXT("item_iron_plate")})))
    {
        return false;
    }
    bool bGood = Test.TestEqual(TEXT("HP envelope is 2502"), Projection.Final.MaxHPP2, int64(250200));
    if (!Envelope(TEXT("MP envelope"), MakeBuild(10, {}, {}, {TEXT("item_arcane_battery"), TEXT("item_arcane_battery")})))
    {
        return false;
    }
    bGood &= Test.TestEqual(TEXT("MP envelope is 718"), Projection.Final.MaxMPP2, int64(71800));
    if (!Envelope(TEXT("AD envelope"), MakeBuild(10, {}, {FLDAttributeCardValue(TEXT("attr_might"), 10)},
        {TEXT("item_edge_blade"), TEXT("item_edge_blade")})))
    {
        return false;
    }
    bGood &= Test.TestEqual(TEXT("AD envelope is 202.8"), Projection.Final.ADP2, int64(20280));
    if (!Envelope(TEXT("AP envelope"), MakeBuild(10, {}, {FLDAttributeCardValue(TEXT("attr_focus"), 10)},
        {TEXT("item_runic_focus"), TEXT("item_runic_focus")})))
    {
        return false;
    }
    bGood &= Test.TestEqual(TEXT("AP envelope is 220"), Projection.Final.APP2, int64(22000));
    bGood &= Test.TestEqual(TEXT("Haste envelope is 30"), Projection.Final.CooldownHasteP2, int64(3000));
    if (!Envelope(TEXT("Armor envelope"), MakeBuild(10, {}, {FLDAttributeCardValue(TEXT("attr_resolve"), 10)},
        {TEXT("item_iron_plate"), TEXT("item_iron_plate")})))
    {
        return false;
    }
    bGood &= Test.TestEqual(TEXT("Armor envelope is 126"), Projection.Final.ArmorP2, int64(12600));
    if (!Envelope(TEXT("Magic resist envelope"), MakeBuild(10, {}, {FLDAttributeCardValue(TEXT("attr_resolve"), 10)},
        {TEXT("item_siphon_emblem"), TEXT("item_siphon_emblem")})))
    {
        return false;
    }
    bGood &= Test.TestEqual(TEXT("Magic resist envelope is 120"), Projection.Final.MagicResistP2, int64(12000));
    bGood &= ExpectFamilies(Test, TEXT("Magic resist envelope"), Projection,
        {{TEXT("passive_siphon"), TEXT("item_siphon_emblem"), {0, 1}}});
    if (!Envelope(TEXT("Attack-speed envelope"), MakeBuild(10, {FLDLearnedPassiveValue(TEXT("passive_battle_rhythm"), 3)}, {},
        {TEXT("item_edge_blade"), TEXT("item_edge_blade")})))
    {
        return false;
    }
    bGood &= ExpectRational(Test, TEXT("Attack-speed envelope is 1.106"), Projection.Final.AttackSpeed, MakeRational(553, 500));
    bGood &= ExpectBasicTiming(Test, TEXT("Attack-speed envelope"), *Catalog, Projection.Final.AttackSpeed, 905, 159);
    if (!Envelope(TEXT("Move-speed envelope"), MakeBuild(10, {}, {}, {TEXT("item_phase_anchor"), TEXT("item_phase_anchor")})))
    {
        return false;
    }
    bGood &= ExpectRational(Test, TEXT("Move-speed envelope is 744"), Projection.Final.MoveSpeedCmPerSec, MakeRational(744));
    if (!Envelope(TEXT("Range envelope"), MakeBuild(10, {}, {FLDAttributeCardValue(TEXT("attr_reach"), 10)},
        {TEXT("item_echo_maul"), TEXT("item_echo_maul")})))
    {
        return false;
    }
    bGood &= ExpectRational(Test, TEXT("Range envelope is 700"), Projection.Final.AttackRangeCm, MakeRational(700));
    bGood &= ExpectFamilies(Test, TEXT("Range envelope"), Projection, {{TEXT("passive_cleave"), TEXT("item_echo_maul"), {0, 1}}});
    if (!Envelope(TEXT("Crit envelope"), MakeBuild(10, {FLDLearnedPassiveValue(TEXT("passive_precision"), 3)})))
    {
        return false;
    }
    bGood &= Test.TestEqual(TEXT("Crit envelope is 0.20"), Projection.Final.CritChanceBp, int64(2000));
    if (!Envelope(TEXT("Regeneration envelope"), MakeBuild(10, {FLDLearnedPassiveValue(TEXT("passive_second_wind"), 3)}))
        || !Test.TestTrue(TEXT("Regeneration envelope has a conditional term"), Projection.OutOfCombatHPRegen.IsSet()))
    {
        return false;
    }
    FString MathError;
    LDDemoContent::FCheck Check(MathError);
    LDDemoContent::FNumbers N(Check);
    const LDDemoContent::FExact Peak = N.Add(N.Value(Projection.Final.HPRegenPerSec),
        N.Value(Projection.OutOfCombatHPRegen->HPPerSec));
    bGood &= Test.TestTrue(TEXT("Always-on plus out-of-combat regeneration is 9.68 and within the HP regeneration bound"),
        N.EQ(Peak, N.Value(242, 25)) && N.LE(Peak, N.Value(Catalog->Get().Parameters.HPRegen.Max)));

    TArray<FString> ItemIds;
    for (const FLDDemoEquipmentDefinition& Item : Catalog->Get().Equipment)
    {
        ItemIds.Add(Item.Id);
    }
    TArray<TArray<FString>> Multisets;
    Multisets.Emplace();
    for (int32 First = 0; First < ItemIds.Num(); ++First)
    {
        Multisets.Add(TArray<FString>{ItemIds[First]});
        for (int32 Second = First; Second < ItemIds.Num(); ++Second)
        {
            Multisets.Add(TArray<FString>{ItemIds[First], ItemIds[Second]});
        }
    }
    bGood &= Test.TestEqual(TEXT("Empty, single and duplicate-allowed pairs give 36 equipment multisets"), Multisets.Num(), 36);
    for (const uint8 Level : {uint8(1), uint8(10)})
    {
        for (const TArray<FString>& Items : Multisets)
        {
            const FString Label = FString::Printf(TEXT("Level %d with [%s]"), int32(Level), *FString::Join(Items, TEXT(", ")));
            if (!ProjectBuild(Test, Label, MakeBuild(Level, {}, {}, Items), *Catalog, Projection))
            {
                return false;
            }
            if (Items.Num() == 2)
            {
                FLDAttributeProjection Swapped;
                if (!ProjectBuild(Test, Label + TEXT(" swapped"), MakeBuild(Level, {}, {}, {Items[1], Items[0]}), *Catalog, Swapped))
                {
                    return false;
                }
                bGood &= Test.TestTrue(Label + TEXT(": slot order changes neither attributes nor explanations"),
                    SameAttributes(Projection, Swapped));
            }
        }
    }
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
    bGood &= Refused(TEXT("Level eleven"), MakeBuild(11), EError::InputDomain, TEXT("Level"));
    bGood &= Refused(TEXT("Level 255"), MakeBuild(255), EError::InputDomain, TEXT("Level"));
    bGood &= Refused(TEXT("Three equipped items"), MakeBuild(10, {}, {},
        {TEXT("item_iron_plate"), TEXT("item_iron_plate"), TEXT("item_edge_blade")}), EError::InputDomain, TEXT("EquippedItems"));
    bGood &= Refused(TEXT("A third item is refused before its identity is resolved"), MakeBuild(10, {}, {},
        {TEXT("item_missing"), TEXT("item_missing"), TEXT("item_missing")}), EError::InputDomain, TEXT("EquippedItems"));
    bGood &= Refused(TEXT("Unknown equipment"), MakeBuild(1, {}, {}, {TEXT("item_iron_plate"), TEXT("item_missing")}),
        EError::UnknownDefinition, TEXT("EquippedItems[1]"));
    // A packaged build can return an FName in its first registered casing; equipment lookups fold to the canonical ID.
    FLDAttributeProjection Folded, Canonical;
    bGood &= ProjectBuild(Test, TEXT("Case-folded equipment ID"), MakeBuild(1, {}, {}, {TEXT("Item_Echo_Maul")}), *Catalog, Folded)
        && ProjectBuild(Test, TEXT("Canonical equipment ID"), MakeBuild(1, {}, {}, {TEXT("item_echo_maul")}), *Catalog, Canonical)
        && Test.TestTrue(TEXT("Case-folded equipment projects exactly like its canonical identity"),
            Folded.Final.MaxHPP2 == Canonical.Final.MaxHPP2 && Folded.Final.MaxMPP2 == Canonical.Final.MaxMPP2
            && Folded.Final.ADP2 == Canonical.Final.ADP2 && Folded.Final.APP2 == Canonical.Final.APP2
            && Folded.Final.ArmorP2 == Canonical.Final.ArmorP2 && Folded.Final.MagicResistP2 == Canonical.Final.MagicResistP2
            && Folded.Final.CritChanceBp == Canonical.Final.CritChanceBp && Folded.Final.CooldownHasteP2 == Canonical.Final.CooldownHasteP2
            && Folded.EquipmentPassives.Num() == Canonical.EquipmentPassives.Num()
            && Folded.Contributions.Num() == Canonical.Contributions.Num());
    bGood &= Refused(TEXT("An empty ID is not an empty slot"), MakeBuild(1, {}, {}, {FString()}),
        EError::UnknownDefinition, TEXT("EquippedItems[0]"));
    bGood &= Refused(TEXT("An attribute card is not equipment"), MakeBuild(1, {}, {}, {TEXT("attr_might")}),
        EError::UnknownDefinition, TEXT("EquippedItems[0]"));
    bGood &= Refused(TEXT("Unknown passive"), MakeBuild(10, {Passive(TEXT("passive_missing"), 1)}),
        EError::UnknownDefinition, TEXT("LearnedPassives[0]"));
    bGood &= Refused(TEXT("A learned active carries no passive attributes"), MakeBuild(10, {Passive(TEXT("active_arc_bolt"), 1)}),
        EError::UnknownDefinition, TEXT("LearnedPassives[0]"));
    bGood &= Refused(TEXT("An equipment passive family is not a learned passive"),
        MakeBuild(10, {Passive(TEXT("passive_cleave"), 1)}), EError::UnknownDefinition, TEXT("LearnedPassives[0]"));
    bGood &= Refused(TEXT("Duplicate learned passive"), MakeBuild(10, {Passive(TEXT("passive_precision"), 1),
        Passive(TEXT("passive_precision"), 1)}), EError::DuplicateSource, TEXT("LearnedPassives[1]"));
    bGood &= Refused(TEXT("Rank zero"), MakeBuild(10, {Passive(TEXT("passive_precision"), 0)}),
        EError::InputDomain, TEXT("LearnedPassives[0]"));
    bGood &= Refused(TEXT("Rank four"), MakeBuild(10, {Passive(TEXT("passive_precision"), 4)}),
        EError::InputDomain, TEXT("LearnedPassives[0]"));
    bGood &= Refused(TEXT("Unknown attribute card"), MakeBuild(10, {}, {Cards(TEXT("attr_missing"), 1)}),
        EError::UnknownDefinition, TEXT("AttributeCards[0]"));
    bGood &= Refused(TEXT("Equipment is not an attribute card"), MakeBuild(10, {}, {Cards(TEXT("item_edge_blade"), 1)}),
        EError::UnknownDefinition, TEXT("AttributeCards[0]"));
    bGood &= Refused(TEXT("Duplicate attribute card entry"), MakeBuild(10, {}, {Cards(TEXT("attr_might"), 1),
        Cards(TEXT("attr_might"), 1)}), EError::DuplicateSource, TEXT("AttributeCards[1]"));
    bGood &= Refused(TEXT("Zero-count attribute card entry"), MakeBuild(10, {}, {Cards(TEXT("attr_might"), 0)}),
        EError::InputDomain, TEXT("AttributeCards[0]"));
    bGood &= Refused(TEXT("Level 1 holds only its opening choice: two cards exceed it"), MakeBuild(1, {}, {Cards(TEXT("attr_might"), 2)}),
        EError::InputDomain, TEXT("ChoiceBudget"));
    bGood &= Refused(TEXT("Eleven cards exceed ten choices"), MakeBuild(10, {}, {Cards(TEXT("attr_might"), 11)}),
        EError::InputDomain, TEXT("ChoiceBudget"));
    bGood &= Refused(TEXT("A rank-three passive plus eight cards exceed ten choices"),
        MakeBuild(10, {Passive(TEXT("passive_precision"), 3)}, {Cards(TEXT("attr_might"), 8)}), EError::InputDomain, TEXT("ChoiceBudget"));
    bGood &= Refused(TEXT("A rank-three passive at level 2 exceeds two choices"),
        MakeBuild(2, {Passive(TEXT("passive_battle_rhythm"), 3)}), EError::InputDomain, TEXT("ChoiceBudget"));
    bGood &= Refused(TEXT("Twenty-one AD cards are refused before any attribute bound"),
        MakeBuild(10, {}, {Cards(TEXT("attr_might"), 21)}), EError::InputDomain, TEXT("ChoiceBudget"));
    FLDAttributeProjection Projection;
    bGood &= ProjectBuild(Test, TEXT("A level-1 build with its opening card"),
        MakeBuild(1, {}, {Cards(TEXT("attr_might"), 1)}), *Catalog, Projection);
    bGood &= ProjectBuild(Test, TEXT("A build exactly at the visible choice bound"),
        MakeBuild(10, {Passive(TEXT("passive_precision"), 3)}, {Cards(TEXT("attr_might"), 7)}), *Catalog, Projection);
    bGood &= ProjectBuild(Test, TEXT("A rank-three passive at level 3"),
        MakeBuild(3, {Passive(TEXT("passive_battle_rhythm"), 3)}), *Catalog, Projection);
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
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoAttributeProjectionLevelsTest, "Lockdown.Rules.DemoAttributeProjection.ApprovedLevels",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoAttributeProjectionLevelsTest::RunTest(const FString& Parameters)
{
    return LDAttributeProjectionTests::RunApprovedLevels(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoAttributeProjectionDuplicateEquipmentTest,
    "Lockdown.Rules.DemoAttributeProjection.DuplicateEquipment",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoAttributeProjectionDuplicateEquipmentTest::RunTest(const FString& Parameters)
{
    return LDAttributeProjectionTests::RunDuplicateEquipment(*this);
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

#endif
