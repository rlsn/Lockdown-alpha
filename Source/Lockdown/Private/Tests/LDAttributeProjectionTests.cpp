#include "Combat/LDAttributeProjection.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/LDTacticalRules.h"
#include "../Development/LDDemoContentJson.h"
#include "../Development/LDDemoContentMath.h"
#include "Misc/AutomationTest.h"
#include "Tests/LDTestApprovedFixture.h"

namespace LDAttributeProjectionTests
{
namespace
{
using EAttribute = ELDProjectedAttribute;
using EError = ELDAttributeProjectionError;

// Reads the producer-exported approved_base only; there is no live-data fallback.
bool ReadAttributeFixture(FAutomationTestBase& Test, TSharedPtr<const FLDDemoContentSnapshot>& Out)
{
    if (!LDTestApprovedFixture::Read(Test, Out))
    {
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
        && Progression.MaxLevel == 16 && Progression.ActiveSlots == 3);
}

FLDDemoRational MakeRational(int64 Numerator, int64 Denominator = 1)
{
    FLDDemoRational Result;
    Result.Numerator = Numerator;
    Result.Denominator = Denominator;
    return Result;
}

FLDAttributeBuildValues MakeBuild(uint8 Level, TArray<FLDStatPointsValue> Points = {})
{
    return FLDAttributeBuildValues(Level, MoveTemp(Points));
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
        const LDDemoContent::FExact Flat = N.Add(N.Add(N.Value(Row.LevelGrowth.Flat), N.Value(Row.PermanentChoices.Flat)),
            N.Value(Row.Dev.Flat));
        const LDDemoContent::FExact Ratio = N.Add(N.Add(N.Value(Row.LevelGrowth.AdditiveRatio),
            N.Value(Row.PermanentChoices.AdditiveRatio)), N.Value(Row.Dev.AdditiveRatio));
        // #112 §4: every attribute clamps to its catalog bounds; a clamped row reports it and its final differs from the sum.
        const bool bExplained = N.EQ(N.Mul(N.Add(N.Value(Row.Base), Flat), N.Add(N.Value(1), Ratio)), N.Value(Row.Final));
        bGood &= Test.TestTrue(What + TEXT(" keeps attribute order"), Row.Attribute == static_cast<EAttribute>(Index));
        bGood &= Test.TestTrue(What + TEXT(" final is (base + flat) x (1 + ratio), or clamped and marked"),
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
            && SameTerms(X.PermanentChoices, Y.PermanentChoices) && SameTerms(X.Dev, Y.Dev)
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

bool RunStatPoints(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!ReadAttributeFixture(Test, Catalog))
    {
        return false;
    }
    FLDAttributeProjection Projection;
    bool bGood = true;
    // #198: ten points into each stat at level 10 (base AD 98.8, max HP 1142, MP regeneration 1.92, resistances 36,
    // attack speed 0.9 x 1.18, HP regeneration 2.68); each point adds its stat's PerPoint bonuses once, never compounding.
    struct FStatCase
    {
        const TCHAR* Id;
        TFunction<void(FExpectedAttributes&)> Expect;
        EAttribute Attribute;
        FLDDemoRational ChoiceFlat;
        FLDDemoRational ChoiceRatio;
    };
    const FLDDemoRational Zero = MakeRational(0);
    const TArray<FStatCase> Stats = {
        {TEXT("stat_physique"), [](FExpectedAttributes& E) { E.MaxHPP2 = 144200; E.HPRegenPerSec = MakeRational(87, 25); },
            EAttribute::MaxHP, MakeRational(300), Zero},
        {TEXT("stat_thick_skin"), [](FExpectedAttributes& E) { E.ArmorP2 = 6100; E.MagicResistP2 = 6100; },
            EAttribute::Armor, MakeRational(25), Zero},
        {TEXT("stat_strength"), [](FExpectedAttributes& E) { E.ADP2 = 12380; E.CritChanceBp = 400; },
            EAttribute::AD, MakeRational(25), Zero},
        {TEXT("stat_brains"), [](FExpectedAttributes& E) { E.APP2 = 5000; E.CooldownHasteP2 = 1000; E.MPRegenPerSec = MakeRational(111, 50); },
            EAttribute::AP, MakeRational(50), Zero},
        {TEXT("stat_agility"), [](FExpectedAttributes& E) { E.AttackSpeed = MakeRational(621, 500); E.MoveSpeedCmPerSec = MakeRational(315); },
            EAttribute::AttackSpeed, Zero, MakeRational(1, 5)},
    };
    if (!Test.TestEqual(TEXT("The approved catalog holds the five point-bought stats"), Catalog->Get().Stats.Num(), Stats.Num()))
    {
        return false;
    }
    for (const FStatCase& Case : Stats)
    {
        const FString Label = FString::Printf(TEXT("Ten %s points at level 10"), Case.Id);
        FExpectedAttributes Stacked = ApprovedLevel(10);
        Case.Expect(Stacked);
        if (!ProjectBuild(Test, Label, MakeBuild(10, {FLDStatPointsValue(Case.Id, 10)}), *Catalog, Projection))
        {
            return false;
        }
        bGood &= ExpectAttributes(Test, Label + TEXT(" add every per-point bonus"), Projection.Final, Stacked);
        const FLDAttributeContributions& Row = Projection.Contributions[int32(Case.Attribute)];
        bGood &= ExpectTerms(Test, Label + TEXT(" permanent-choice terms"), Row.PermanentChoices, Case.ChoiceFlat, Case.ChoiceRatio);
        bGood &= ExpectTerms(Test, Label + TEXT(" leave the DEV row empty"), Row.Dev, Zero, Zero);
        for (int32 Index = 0; Index < Projection.Contributions.Num(); ++Index)
        {
            bGood &= Test.TestFalse(FString::Printf(TEXT("%s: row %d is inside its bound"), *Label, Index),
                Projection.Contributions[Index].bClamped);
        }
    }
    bGood &= ExpectTerms(Test, TEXT("Ten agility points add one move-speed ratio"),
        Projection.Contributions[int32(EAttribute::MoveSpeed)].PermanentChoices, Zero, MakeRational(1, 20));

    // Points of several stats at level 6 (max HP 910, AD 86, attack speed 0.9 x 1.1, HP regeneration 2.2).
    FExpectedAttributes Mixed = ApprovedLevel(6);
    Mixed.MaxHPP2 = 100000;
    Mixed.HPRegenPerSec = MakeRational(61, 25);
    Mixed.ADP2 = 9100;
    Mixed.CritChanceBp = 80;
    Mixed.AttackSpeed = MakeRational(126, 125);
    Mixed.MoveSpeedCmPerSec = MakeRational(603, 2);
    FLDAttributeProjection Reversed;
    if (!ProjectBuild(Test, TEXT("Level 6 mixed points"), MakeBuild(6, {FLDStatPointsValue(TEXT("stat_physique"), 3),
            FLDStatPointsValue(TEXT("stat_strength"), 2), FLDStatPointsValue(TEXT("stat_agility"), 1)}), *Catalog, Projection)
        || !ProjectBuild(Test, TEXT("The same points listed in reverse"), MakeBuild(6, {FLDStatPointsValue(TEXT("stat_agility"), 1),
            FLDStatPointsValue(TEXT("stat_strength"), 2), FLDStatPointsValue(TEXT("stat_physique"), 3)}), *Catalog, Reversed))
    {
        return false;
    }
    bGood &= ExpectAttributes(Test, TEXT("Level 6 mixed points"), Projection.Final, Mixed);
    bGood &= Test.TestTrue(TEXT("Point order changes neither attributes nor explanations"), SameAttributes(Projection, Reversed));
    bGood &= ExpectTerms(Test, TEXT("Mixed AD permanent choices"), Projection.Contributions[int32(EAttribute::AD)].PermanentChoices,
        MakeRational(5), Zero);
    bGood &= ExpectTerms(Test, TEXT("Mixed attack-speed growth stays its own term"),
        Projection.Contributions[int32(EAttribute::AttackSpeed)].LevelGrowth, Zero, MakeRational(1, 10));
    bGood &= ExpectTerms(Test, TEXT("Mixed attack-speed points"), Projection.Contributions[int32(EAttribute::AttackSpeed)].PermanentChoices,
        Zero, MakeRational(1, 50));
    FLDAttributeProjection Plain;
    if (!ProjectBuild(Test, TEXT("Level 6 without points"), MakeBuild(6), *Catalog, Plain))
    {
        return false;
    }
    bGood &= ExpectAttributes(Test, TEXT("No allocated point adds nothing"), Plain.Final, ApprovedLevel(6));
    for (const FLDAttributeContributions& Row : Plain.Contributions)
    {
        bGood &= ExpectTerms(Test, TEXT("An empty allocation has no permanent-choice term"), Row.PermanentChoices, Zero, Zero);
    }
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
    const auto Envelope = [&Test, &Catalog, &Projection](const TCHAR* Label, const TCHAR* Stat, int32 Points)
    {
        return ProjectBuild(Test, Label, MakeBuild(16, {FLDStatPointsValue(Stat, Points)}), *Catalog, Projection);
    };
    // Level 16 holds 3 + 15 x 3 = 48 points; every stat stays inside its bound with all of them (base max HP 1490, AD 118,
    // resistances 48, HP regeneration 3.4, MP regeneration 2.4, attack speed 0.9 x 1.3).
    if (!Envelope(TEXT("Physique envelope"), TEXT("stat_physique"), 48)) { return false; }
    bool bGood = Test.TestEqual(TEXT("HP envelope is 1490 + 48 x 30"), Projection.Final.MaxHPP2, int64(293000));
    bGood &= ExpectRational(Test, TEXT("HP regeneration envelope is 3.4 + 48 x 0.08"), Projection.Final.HPRegenPerSec, MakeRational(181, 25));
    if (!Envelope(TEXT("Thick skin envelope"), TEXT("stat_thick_skin"), 48)) { return false; }
    bGood &= Test.TestTrue(TEXT("Resistance envelope is 48 + 48 x 2.5"), Projection.Final.ArmorP2 == 16800 && Projection.Final.MagicResistP2 == 16800);
    if (!Envelope(TEXT("Strength envelope"), TEXT("stat_strength"), 48)) { return false; }
    bGood &= Test.TestTrue(TEXT("AD envelope is 118 + 48 x 2.5 with 48 x 0.4% crit"),
        Projection.Final.ADP2 == 23800 && Projection.Final.CritChanceBp == 1920);
    if (!Envelope(TEXT("Brains envelope"), TEXT("stat_brains"), 48)) { return false; }
    bGood &= Test.TestTrue(TEXT("AP envelope is 48 x 5 with 48 haste"), Projection.Final.APP2 == 24000 && Projection.Final.CooldownHasteP2 == 4800);
    bGood &= ExpectRational(Test, TEXT("MP regeneration envelope is 2.4 + 48 x 0.03"), Projection.Final.MPRegenPerSec, MakeRational(96, 25));
    if (!Envelope(TEXT("Agility envelope"), TEXT("stat_agility"), 48)) { return false; }
    bGood &= ExpectRational(Test, TEXT("Attack-speed envelope is 0.9 x 2.26"), Projection.Final.AttackSpeed, MakeRational(1017, 500));
    bGood &= ExpectRational(Test, TEXT("Move-speed envelope is 300 x 1.24"), Projection.Final.MoveSpeedCmPerSec, MakeRational(372));
    bGood &= ExpectBasicTiming(Test, TEXT("Attack-speed envelope"), *Catalog, Projection.Final.AttackSpeed, 492, 111);

    // Past level 16 every full 400 XP adds 3 points; the attributes then clamp to their catalog bounds and say so.
    if (!Envelope(TEXT("Brains past the cap"), TEXT("stat_brains"), 78)) { return false; }
    bGood &= Test.TestTrue(TEXT("AP, haste and MP regeneration clamp to 250, 60 and 4"), Projection.Final.APP2 == 25000
        && Projection.Final.CooldownHasteP2 == 6000 && Projection.Final.MPRegenPerSec.Numerator == 4 * Projection.Final.MPRegenPerSec.Denominator);
    bGood &= Test.TestTrue(TEXT("The clamped rows say so"), Projection.Contributions[int32(EAttribute::AP)].bClamped
        && Projection.Contributions[int32(EAttribute::CooldownHaste)].bClamped && Projection.Contributions[int32(EAttribute::MPRegen)].bClamped);
    if (!Envelope(TEXT("Physique past the cap"), TEXT("stat_physique"), 78)) { return false; }
    bGood &= Test.TestTrue(TEXT("Max HP clamps to 3000"), Projection.Final.MaxHPP2 == 300000
        && Projection.Contributions[int32(EAttribute::MaxHP)].bClamped);
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
    const auto Points = [](const TCHAR* Id, int32 Count) { return FLDStatPointsValue(Id, Count); };
    bool bGood = Refused(TEXT("Level zero"), MakeBuild(0), EError::InputDomain, TEXT("Level"));
    bGood &= Refused(TEXT("Level seventeen"), MakeBuild(17), EError::InputDomain, TEXT("Level"));
    bGood &= Refused(TEXT("Level 255"), MakeBuild(255), EError::InputDomain, TEXT("Level"));
    bGood &= Refused(TEXT("Unknown stat"), MakeBuild(10, {Points(TEXT("stat_missing"), 1)}), EError::UnknownDefinition,
        TEXT("StatPoints[0]"));
    bGood &= Refused(TEXT("An active is no stat"), MakeBuild(10, {Points(TEXT("active_snark_bolt"), 1)}), EError::UnknownDefinition,
        TEXT("StatPoints[0]"));
    bGood &= Refused(TEXT("A stat listed twice"), MakeBuild(10, {Points(TEXT("stat_strength"), 1), Points(TEXT("stat_strength"), 2)}),
        EError::DuplicateSource, TEXT("StatPoints[1]"));
    bGood &= Refused(TEXT("Case-folded ids cannot list one stat twice"),
        MakeBuild(10, {Points(TEXT("stat_strength"), 1), Points(TEXT("Stat_Strength"), 1)}), EError::DuplicateSource, TEXT("StatPoints[1]"));
    bGood &= Refused(TEXT("A stat row of no point"), MakeBuild(10, {Points(TEXT("stat_strength"), 0)}), EError::InputDomain,
        TEXT("StatPoints[0]"));
    bGood &= Refused(TEXT("A negative stat row"), MakeBuild(10, {Points(TEXT("stat_physique"), 2), Points(TEXT("stat_strength"), -1)}),
        EError::InputDomain, TEXT("StatPoints[1]"));
    FLDAttributeProjection Projection;
    bGood &= ProjectBuild(Test, TEXT("Every stat once"), MakeBuild(16, {Points(TEXT("stat_physique"), 10), Points(TEXT("stat_thick_skin"), 10),
        Points(TEXT("stat_strength"), 10), Points(TEXT("stat_brains"), 9), Points(TEXT("stat_agility"), 9)}), *Catalog, Projection);
    bGood &= ProjectBuild(Test, TEXT("A case-folded stat id names the same stat"), MakeBuild(2, {Points(TEXT("Stat_Strength"), 1)}),
        *Catalog, Projection);
    bGood &= Test.TestEqual(TEXT("The case-folded stat projects like its canonical id"), Projection.Final.ADP2,
        ApprovedLevel(2).ADP2 + 250);
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

// The conditional low-HP bonus (low_hp_stat_bonus, kept for the #198 PR3 base components): its stats join the temporary
// terms only strictly below the threshold, and never the build projection itself.
bool RunLowHPTerm(FAutomationTestBase& Test)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!ReadAttributeFixture(Test, Catalog))
    {
        return false;
    }
    FLDAttributeProjection Plain;
    if (!ProjectBuild(Test, TEXT("Level 6"), MakeBuild(6), *Catalog, Plain))
    {
        return false;
    }
    FLDLowHPBonusTerm Term;
    Term.ThresholdBp = 5000;
    Term.Stats = {TempBonus(ELDDemoBonusStat::AttackSpeedBonusRatio, 2000), TempBonus(ELDDemoBonusStat::MoveSpeedBonusRatio, 1000)};
    Term.BasicLifestealBp = 800;
    const TOptional<FLDLowHPBonusTerm> Held(Term);
    FLDAttributeProjection Pushed;
    const FLDTemporaryAttributeTerms Low = LDTactical::TemporaryTerms({}, {}, Held, 49, 100, 0);
    if (!Apply(Test, TEXT("Below the threshold"), Plain, Low, *Catalog, Pushed)) { return false; }
    const FLDDemoContentParameters& P = Catalog->Get().Parameters;
    const int64 LevelSpeedBp = 5 * P.AttackSpeedPerLevelBp;
    bool bGood = Test.TestTrue(TEXT("Below the threshold attack speed adds the term's ratio to the level ratio"),
        SameRational(Pushed.Final.AttackSpeed, P.AttackSpeed.Base.Numerator * (10000 + LevelSpeedBp + 2000),
            P.AttackSpeed.Base.Denominator * 10000));
    bGood &= Test.TestTrue(TEXT("Below the threshold move speed rises by the term's ratio"),
        SameRational(Pushed.Final.MoveSpeedCmPerSec, P.MoveSpeedCmPerSec.Base.Numerator * 11000, P.MoveSpeedCmPerSec.Base.Denominator * 10000));
    bGood &= Test.TestTrue(TEXT("Exactly at the threshold the term adds nothing"),
        LDTactical::TemporaryTerms({}, {}, Held, 50, 100, 0).Bonuses.IsEmpty());
    bGood &= Test.TestTrue(TEXT("At full HP the term adds nothing"), LDTactical::TemporaryTerms({}, {}, Held, 100, 100, 0).Bonuses.IsEmpty());
    bGood &= Test.TestTrue(TEXT("Without a held term nothing is added"),
        LDTactical::TemporaryTerms({}, {}, TOptional<FLDLowHPBonusTerm>(), 1, 100, 0).Bonuses.IsEmpty());
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoAttributeProjectionStatPointsTest, "Lockdown.Rules.DemoAttributeProjection.StatPoints",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoAttributeProjectionStatPointsTest::RunTest(const FString& Parameters)
{
    return LDAttributeProjectionTests::RunStatPoints(*this);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoAttributeProjectionLowHPTermTest,
    "Lockdown.Rules.DemoAttributeProjection.LowHPTerm",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoAttributeProjectionLowHPTermTest::RunTest(const FString& Parameters)
{
    return LDAttributeProjectionTests::RunLowHPTerm(*this);
}

#endif