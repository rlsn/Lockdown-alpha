#include "Combat/LDTacticalRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "../Development/LDDemoContentJson.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

namespace
{
bool ReadApprovedFixture(FAutomationTestBase& Test, TSharedPtr<const FLDDemoContentSnapshot>& Out)
{
    using namespace LDDemoContent;
    FString Root, Error;
    if (!FParse::Value(FCommandLine::Get(), TEXT("LDDemoContentFixtures="), Root) || Root.IsEmpty())
    {
        Test.AddError(TEXT("Missing -LDDemoContentFixtures=<isolated Artifacts directory>; export approved_base first. No live-data fallback."));
        return false;
    }
    if (!Test.TestTrue(TEXT("Evaluator fixtures are isolated under this project's Artifacts"),
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
    const auto& P = Out->Get().Parameters;
    return Test.TestTrue(TEXT("Fixture uses the approved first-play damage and timing parameters"),
        P.AD.BaseP2 == 7000 && P.AP.BaseP2 == 0 && P.MaxHP.BaseP2 == 62000
        && P.Armor.BaseP2 == 1800 && P.MagicResist.BaseP2 == 1800
        && P.Armor.MaxP2 == 20000 && P.MagicResist.MaxP2 == 20000
        && P.CritMultiplierBp == 20000 && Out->Get().Formulas.ResistanceScaleP2 == 10000
        && P.AttackSpeed.Base.Numerator == 9 && P.AttackSpeed.Base.Denominator == 10
        && P.BasicAttackWindupMs == 250);
}

FLDDamageEvaluationInput PrimaryInput(const FLDDemoContentSnapshot& Catalog)
{
    const auto& Content = Catalog.Get();
    FLDDamageEvaluationInput Input;
    Input.Definition.Emplace(0, Content.Character.BasicADRatioBp, 0, Content.Character.BasicDamageType);
    Input.HitClass = ELDCombatHitClass::BasicPrimary;
    Input.bCritical = false;
    Input.SourceADP2 = Content.Parameters.AD.BaseP2;
    Input.TargetResistanceP2 = Content.Parameters.Armor.BaseP2;
    Input.TargetHPP2 = Content.Parameters.MaxHP.BaseP2;
    Input.TargetShieldP2 = int64(0);
    Input.bTargetProtected = false;
    return Input;
}

FLDDemoRational Rate(int64 Numerator, int64 Denominator = 1)
{
    FLDDemoRational Result;
    Result.Numerator = Numerator;
    Result.Denominator = Denominator;
    return Result;
}

FLDBasicTimingInput TimingInput(const FLDDemoContentSnapshot& Catalog)
{
    FLDBasicTimingInput Input;
    Input.BaseAttackSpeed = Catalog.Get().Parameters.AttackSpeed.Base;
    Input.CurrentAttackSpeed = Input.BaseAttackSpeed;
    Input.BaseWindupMs = Catalog.Get().Parameters.BasicAttackWindupMs;
    return Input;
}

bool CheckDamage(FAutomationTestBase& Test, const FString& Label, const FLDDamageEvaluationInput& Input,
    const FLDDemoContentSnapshot& Catalog, const FLDDamageEvaluation& Expected)
{
    const auto Result = LDTactical::EvaluateDamage(Input, Catalog);
    if (!Test.TestTrue(Label + TEXT(": success"), Result.IsSuccess()))
    {
        Test.AddError(Result.Error.Field + TEXT(": ") + Result.Error.Message);
        return false;
    }
    const auto& Value = Result.Value.GetValue();
    bool Good = Test.TestEqual(Label + TEXT(": damage P2"), Value.DamageP2, Expected.DamageP2);
    Good &= Test.TestEqual(Label + TEXT(": shield absorbed P2"), Value.ShieldAbsorbedP2, Expected.ShieldAbsorbedP2);
    Good &= Test.TestEqual(Label + TEXT(": HP lost P2"), Value.HPLostP2, Expected.HPLostP2);
    Good &= Test.TestEqual(Label + TEXT(": overkill P2"), Value.OverkillP2, Expected.OverkillP2);
    Good &= Test.TestEqual(Label + TEXT(": remaining HP P2"), Value.RemainingHPP2, Expected.RemainingHPP2);
    Good &= Test.TestEqual(Label + TEXT(": remaining shield P2"), Value.RemainingShieldP2, Expected.RemainingShieldP2);
    Good &= Test.TestTrue(Label + TEXT(": damage partition conserves P2 without an overflowing resource sum"),
        Value.DamageP2 >= 0 && Value.ShieldAbsorbedP2 >= 0 && Value.ShieldAbsorbedP2 <= Value.DamageP2
        && Value.HPLostP2 >= 0 && Value.HPLostP2 <= Value.DamageP2 - Value.ShieldAbsorbedP2
        && Value.OverkillP2 == Value.DamageP2 - Value.ShieldAbsorbedP2 - Value.HPLostP2);
    Good &= Test.TestTrue(Label + TEXT(": resources conserve P2"),
        Value.HPLostP2 <= Input.TargetHPP2.GetValue() && Value.ShieldAbsorbedP2 <= Input.TargetShieldP2.GetValue()
        && Value.RemainingHPP2 == Input.TargetHPP2.GetValue() - Value.HPLostP2
        && Value.RemainingShieldP2 == Input.TargetShieldP2.GetValue() - Value.ShieldAbsorbedP2);
    Good &= Test.TestTrue(Label + TEXT(": success carries no error"), Result.Error.Field.IsEmpty() && Result.Error.Message.IsEmpty());
    return Good;
}

template <typename TResult>
bool CheckFailure(FAutomationTestBase& Test, const FString& Label, const TResult& Result, ELDCombatEvaluationError Code)
{
    return Test.TestTrue(Label + TEXT(": typed error, diagnostic and no success-shaped value"),
        !Result.IsSuccess() && !Result.Value.IsSet() && Result.Error.Code == Code
        && !Result.Error.Field.IsEmpty() && !Result.Error.Message.IsEmpty());
}

bool CheckTiming(FAutomationTestBase& Test, const FString& Label, const FLDBasicTimingInput& Input,
    const FLDDemoContentSnapshot& Catalog, int64 IntervalMs, int64 WindupMs)
{
    const auto Result = LDTactical::EvaluateBasicTiming(Input, Catalog);
    if (!Test.TestTrue(Label + TEXT(": success"), Result.IsSuccess()))
    {
        Test.AddError(Result.Error.Field + TEXT(": ") + Result.Error.Message);
        return false;
    }
    bool Good = Test.TestEqual(Label + TEXT(": interval ms"), Result.Value->IntervalMs, IntervalMs);
    Good &= Test.TestEqual(Label + TEXT(": windup ms"), Result.Value->WindupMs, WindupMs);
    Good &= Test.TestTrue(Label + TEXT(": positive durations and strict windup ordering"),
        Result.Value->WindupMs > 0 && Result.Value->WindupMs < Result.Value->IntervalMs);
    Good &= Test.TestTrue(Label + TEXT(": success carries no error"), Result.Error.Field.IsEmpty() && Result.Error.Message.IsEmpty());
    return Good;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoDamageEvaluationTest, "Lockdown.Rules.DemoCombatEvaluation.Damage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoDamageEvaluationTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!ReadApprovedFixture(*this, Catalog)) { return false; }
    auto Input = PrimaryInput(*Catalog);
    if (!CheckDamage(*this, TEXT("70 AD against 18 armor"), Input, *Catalog, {5932, 0, 5932, 0, 56068, 0})) { return false; }
    Input.bCritical = true;
    if (!CheckDamage(*this, TEXT("Critical multiplier precedes resistance"), Input, *Catalog, {11864, 0, 11864, 0, 50136, 0})) { return false; }
    Input.bCritical = false;
    Input.TargetResistanceP2 = int64(0);
    if (!CheckDamage(*this, TEXT("Zero armor is valid"), Input, *Catalog, {7000, 0, 7000, 0, 55000, 0})) { return false; }
    Input.TargetResistanceP2 = int64(10000);
    if (!CheckDamage(*this, TEXT("100 resistance points halve damage"), Input, *Catalog, {3500, 0, 3500, 0, 58500, 0})) { return false; }
    Input.TargetResistanceP2 = int64(20000);
    if (!CheckDamage(*this, TEXT("200 armor has no legacy sixty-percent mitigation cap"), Input, *Catalog, {2333, 0, 2333, 0, 59667, 0})) { return false; }

    FString Error;
    const FLDDemoAbilityDefinition* Arc = nullptr;
    if (!Catalog->FindAbility(TEXT("active_arc_bolt"), Arc, Error))
    {
        AddError(Error);
        return false;
    }
    if (!TestEqual(TEXT("Approved arc bolt has three ranks"), Arc->Ranks.Num(), 3)) { return false; }
    const int64 ExpectedDamage[] = {14322, 17033, 19745};
    for (int32 I = 0; I < Arc->Ranks.Num(); ++I)
    {
        const auto* Effect = Arc->Ranks[I].Effects.FindByPredicate(
            [](const auto& Value) { return Value.GetOp() == ELDDemoEffectOp::Damage; });
        if (!TestNotNull(TEXT("Rank has an actual compiled damage effect"), Effect)) { return false; }
        Input = PrimaryInput(*Catalog);
        Input.Definition.Emplace(Effect->BaseP2, Effect->ADRatioBp, Effect->APRatioBp, Effect->DamageType);
        Input.HitClass = ELDCombatHitClass::Ability;
        Input.SourceAPP2 = int64(2000);
        Input.TargetResistanceP2 = Catalog->Get().Parameters.MagicResist.BaseP2;
        if (!CheckDamage(*this, FString::Printf(TEXT("Compiled arc rank %d with 70 AD and 20 AP"), I + 1), Input, *Catalog,
            {ExpectedDamage[I], 0, ExpectedDamage[I], 0, 62000 - ExpectedDamage[I], 0})) { return false; }
        if (I == 0)
        {
            Input.SourceAPP2 = Catalog->Get().Parameters.AP.BaseP2;
            if (!CheckDamage(*this, TEXT("156 magic damage against 18 points, not a ratio"), Input, *Catalog,
                {13220, 0, 13220, 0, 48780, 0})) { return false; }
            Input.TargetResistanceP2 = int64(20000);
            if (!CheckDamage(*this, TEXT("Magic resistance uses the same points formula"), Input, *Catalog,
                {5200, 0, 5200, 0, 56800, 0})) { return false; }
        }
    }
    const FLDDemoEquipmentDefinition* Maul = nullptr;
    if (!Catalog->FindEquipment(TEXT("item_echo_maul"), Maul, Error))
    {
        AddError(Error);
        return false;
    }
    const auto* Cleave = Maul->Passive.Effects.FindByPredicate(
        [](const auto& Value) { return Value.GetOp() == ELDDemoEffectOp::Cleave; });
    if (!TestNotNull(TEXT("Compiled maul supplies the secondary coefficient"), Cleave)) { return false; }
    Input = PrimaryInput(*Catalog);
    Input.Definition.Emplace(0, Cleave->RatioBp, 0, ELDDemoDamageType::Physical);
    Input.HitClass = ELDCombatHitClass::BasicSecondary;
    Input.SourceADP2 = int64(8000);
    Input.TargetResistanceP2 = int64(800);
    Input.TargetHPP2 = int64(9000);
    return CheckDamage(*this, TEXT("80 AD cleave against low-camp armor"), Input, *Catalog, {2592, 0, 2592, 0, 6408, 0});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoDamageDomainTest, "Lockdown.Rules.DemoCombatEvaluation.DamageDomains",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoDamageDomainTest::RunTest(const FString& Parameters)
{
    using EError = ELDCombatEvaluationError;
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!ReadApprovedFixture(*this, Catalog)) { return false; }
    auto Input = PrimaryInput(*Catalog);
    Input.TargetHPP2 = int64(3000);
    Input.TargetShieldP2 = int64(1000);
    if (!CheckDamage(*this, TEXT("Shield then HP then overkill"), Input, *Catalog, {5932, 1000, 3000, 1932, 0, 0})) { return false; }
    Input.TargetShieldP2 = int64(6000);
    if (!CheckDamage(*this, TEXT("A full shield absorption is not HP loss"), Input, *Catalog, {5932, 5932, 0, 0, 3000, 68})) { return false; }
    Input.bTargetProtected = true;
    if (!CheckDamage(*this, TEXT("Protection preserves HP and shield"), Input, *Catalog, {0, 0, 0, 0, 3000, 6000})) { return false; }
    Input.bTargetProtected = false;
    Input.SourceADP2 = int64(0);
    if (!CheckDamage(*this, TEXT("Explicit zero AD does not imply a minimum hit"), Input, *Catalog, {0, 0, 0, 0, 3000, 6000})) { return false; }
    Input = PrimaryInput(*Catalog);
    Input.TargetHPP2 = int64(0);
    if (!CheckDamage(*this, TEXT("Zero HP is a numeric value, not a second death event"), Input, *Catalog, {5932, 0, 0, 5932, 0, 0})) { return false; }

    CheckFailure(*this, TEXT("Unresolved definition"), LDTactical::EvaluateDamage({}, *Catalog), EError::Definition);
    for (ELDCombatHitClass HitClass : {ELDCombatHitClass::None, static_cast<ELDCombatHitClass>(255)})
    {
        Input = PrimaryInput(*Catalog);
        Input.HitClass = HitClass;
        CheckFailure(*this, TEXT("Unresolved or unknown hit class"), LDTactical::EvaluateDamage(Input, *Catalog), EError::Definition);
    }
    Input = PrimaryInput(*Catalog);
    Input.Definition.Emplace(0, 10000, 0, static_cast<ELDDemoDamageType>(255));
    CheckFailure(*this, TEXT("Unknown damage type"), LDTactical::EvaluateDamage(Input, *Catalog), EError::Definition);
    for (ELDCombatHitClass HitClass : {ELDCombatHitClass::BasicPrimary, ELDCombatHitClass::BasicSecondary})
    {
        Input = PrimaryInput(*Catalog);
        Input.HitClass = HitClass;
        Input.Definition.Emplace(0, 10000, 1, ELDDemoDamageType::Physical);
        Input.SourceAPP2 = int64(0);
        CheckFailure(*this, TEXT("Even zero current AP cannot authorize an AP basic coefficient"),
            LDTactical::EvaluateDamage(Input, *Catalog), EError::Definition);
        Input.Definition.Emplace(0, 10000, 0, ELDDemoDamageType::Magical);
        CheckFailure(*this, TEXT("Basic hits cannot become magical"), LDTactical::EvaluateDamage(Input, *Catalog), EError::Definition);
    }
    for (ELDCombatHitClass HitClass : {ELDCombatHitClass::BasicSecondary, ELDCombatHitClass::Ability})
    {
        Input = PrimaryInput(*Catalog);
        Input.HitClass = HitClass;
        Input.bCritical = true;
        CheckFailure(*this, TEXT("Non-primary hits cannot borrow a critical result"),
            LDTactical::EvaluateDamage(Input, *Catalog), EError::Domain);
    }
    struct FRequiredP2 { const TCHAR* Name; TOptional<int64> FLDDamageEvaluationInput::* Member; };
    for (const auto& Field : {
        FRequiredP2{TEXT("AD"), &FLDDamageEvaluationInput::SourceADP2},
        FRequiredP2{TEXT("resistance"), &FLDDamageEvaluationInput::TargetResistanceP2},
        FRequiredP2{TEXT("HP"), &FLDDamageEvaluationInput::TargetHPP2},
        FRequiredP2{TEXT("shield"), &FLDDamageEvaluationInput::TargetShieldP2}})
    {
        Input = PrimaryInput(*Catalog);
        (Input.*Field.Member).Reset();
        CheckFailure(*this, FString(TEXT("Missing ")) + Field.Name, LDTactical::EvaluateDamage(Input, *Catalog), EError::Numeric);
        (Input.*Field.Member) = int64(-1);
        CheckFailure(*this, FString(TEXT("Negative ")) + Field.Name, LDTactical::EvaluateDamage(Input, *Catalog), EError::Domain);
    }
    Input = PrimaryInput(*Catalog);
    Input.bCritical.Reset();
    CheckFailure(*this, TEXT("No implicit non-critical assumption"), LDTactical::EvaluateDamage(Input, *Catalog), EError::Numeric);
    Input = PrimaryInput(*Catalog);
    Input.bTargetProtected.Reset();
    CheckFailure(*this, TEXT("No implicit unprotected assumption"), LDTactical::EvaluateDamage(Input, *Catalog), EError::Numeric);
    Input = PrimaryInput(*Catalog);
    Input.Definition.Emplace(0, 0, 10000, ELDDemoDamageType::Magical);
    Input.HitClass = ELDCombatHitClass::Ability;
    Input.SourceADP2.Reset();
    CheckFailure(*this, TEXT("An AP coefficient requires resolved AP"), LDTactical::EvaluateDamage(Input, *Catalog), EError::Numeric);
    Input.SourceAPP2 = int64(-1);
    CheckFailure(*this, TEXT("Negative AP rejects"), LDTactical::EvaluateDamage(Input, *Catalog), EError::Domain);
    Input.SourceAPP2 = Catalog->Get().Parameters.AP.MaxP2 + 1;
    CheckFailure(*this, TEXT("AP beyond the compiled domain rejects"), LDTactical::EvaluateDamage(Input, *Catalog), EError::Domain);
    for (const auto& Definition : {
        FLDDamageDefinitionValues(-1, 0, 0, ELDDemoDamageType::Physical),
        FLDDamageDefinitionValues(0, -1, 0, ELDDemoDamageType::Physical),
        FLDDamageDefinitionValues(0, 0, -1, ELDDemoDamageType::Physical)})
    {
        Input = PrimaryInput(*Catalog);
        Input.Definition = Definition;
        CheckFailure(*this, TEXT("Negative definition quantity rejects"), LDTactical::EvaluateDamage(Input, *Catalog), EError::Domain);
    }
    Input = PrimaryInput(*Catalog);
    Input.SourceADP2 = Catalog->Get().Parameters.AD.MaxP2 + 1;
    CheckFailure(*this, TEXT("AD beyond the compiled domain rejects"), LDTactical::EvaluateDamage(Input, *Catalog), EError::Domain);
    Input = PrimaryInput(*Catalog);
    Input.TargetResistanceP2 = Catalog->Get().Parameters.Armor.MaxP2 + 1;
    CheckFailure(*this, TEXT("Resistance is rejected rather than silently clamped"), LDTactical::EvaluateDamage(Input, *Catalog), EError::Domain);
    Input = PrimaryInput(*Catalog);
    Input.SourceAPP2 = int64(-1);
    CheckFailure(*this, TEXT("Supplied unused attributes must still be valid"), LDTactical::EvaluateDamage(Input, *Catalog), EError::Domain);
    Input = PrimaryInput(*Catalog);
    Input.bTargetProtected = true;
    Input.TargetHPP2 = int64(-1);
    CheckFailure(*this, TEXT("Protection cannot mask invalid resources"), LDTactical::EvaluateDamage(Input, *Catalog), EError::Domain);
    Input = PrimaryInput(*Catalog);
    Input.bTargetProtected = true;
    Input.SourceADP2.Reset();
    CheckFailure(*this, TEXT("Protection cannot mask missing source values"), LDTactical::EvaluateDamage(Input, *Catalog), EError::Numeric);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoDamageExactTest, "Lockdown.Rules.DemoCombatEvaluation.ExactDamage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoDamageExactTest::RunTest(const FString& Parameters)
{
    using EError = ELDCombatEvaluationError;
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!ReadApprovedFixture(*this, Catalog)) { return false; }
    auto Input = PrimaryInput(*Catalog);
    Input.HitClass = ELDCombatHitClass::Ability;
    Input.SourceADP2 = int64(1);
    Input.TargetResistanceP2 = int64(0);
    Input.Definition.Emplace(0, 0, 0, ELDDemoDamageType::Physical);
    Input.SourceADP2.Reset();
    if (!CheckDamage(*this, TEXT("An explicit zero definition needs no unused source attributes"), Input, *Catalog,
        {0, 0, 0, 0, 62000, 0})) { return false; }
    Input.SourceADP2 = int64(1);
    for (const int64 Ratio : {int64(9000), int64(9999), int64(10000), int64(10001)})
    {
        Input.Definition.Emplace(0, Ratio, 0, ELDDemoDamageType::Physical);
        const int64 Expected = Ratio / 10000;
        if (!CheckDamage(*this, FString::Printf(TEXT("Sub-P2 boundary at R4=%lld"), Ratio), Input, *Catalog,
            {Expected, 0, Expected, 0, 62000 - Expected, 0})) { return false; }
    }
    Input.Definition.Emplace(0, 6000, 6000, ELDDemoDamageType::Magical);
    Input.SourceAPP2 = int64(1);
    if (!CheckDamage(*this, TEXT("Add fractional source terms before flooring"), Input, *Catalog, {1, 0, 1, 0, 61999, 0})) { return false; }
    Input.Definition.Emplace(0, 19000, 0, ELDDemoDamageType::Physical);
    Input.SourceAPP2.Reset();
    Input.TargetResistanceP2 = int64(1800);
    if (!CheckDamage(*this, TEXT("Do not floor before resistance"), Input, *Catalog, {1, 0, 1, 0, 61999, 0})) { return false; }
    Input = PrimaryInput(*Catalog);
    Input.SourceADP2 = int64(1);
    Input.bCritical = true;
    if (!CheckDamage(*this, TEXT("Do not floor before the critical multiplier"), Input, *Catalog, {1, 0, 1, 0, 61999, 0})) { return false; }

    Input = PrimaryInput(*Catalog);
    Input.HitClass = ELDCombatHitClass::Ability;
    Input.SourceADP2.Reset();
    Input.TargetResistanceP2 = int64(0);
    Input.TargetHPP2 = MAX_int64;
    const int64 AboveDoubleInteger = 9007199254740993LL;
    Input.Definition.Emplace(AboveDoubleInteger, 0, 0, ELDDemoDamageType::Physical);
    if (!CheckDamage(*this, TEXT("P2 above double's exact integer range"), Input, *Catalog,
        {AboveDoubleInteger, 0, AboveDoubleInteger, 0, MAX_int64 - AboveDoubleInteger, 0})) { return false; }
    Input.Definition.Emplace(1, 0, 0, ELDDemoDamageType::Physical);
    Input.TargetShieldP2 = MAX_int64;
    if (!CheckDamage(*this, TEXT("HP plus shield need not fit in one int64"), Input, *Catalog,
        {1, 1, 0, 0, MAX_int64, MAX_int64 - 1})) { return false; }
    Input.TargetShieldP2 = int64(0);
    Input.Definition.Emplace(0, MAX_int64, 0, ELDDemoDamageType::Physical);
    Input.SourceADP2 = int64(10000);
    if (!CheckDamage(*this, TEXT("Wide coefficient product reduces to exact int64 max"), Input, *Catalog,
        {MAX_int64, 0, MAX_int64, 0, 0, 0})) { return false; }
    Input.SourceADP2 = int64(10001);
    CheckFailure(*this, TEXT("Unrepresentable quantized damage"), LDTactical::EvaluateDamage(Input, *Catalog), EError::Overflow);
    Input.Definition.Emplace(MAX_int64, 5000, 0, ELDDemoDamageType::Physical);
    Input.SourceADP2 = int64(1);
    if (!CheckDamage(*this, TEXT("Floor at int64 max retains a representable result"), Input, *Catalog,
        {MAX_int64, 0, MAX_int64, 0, 0, 0})) { return false; }
    Input.Definition.Emplace(MAX_int64, 10000, 0, ELDDemoDamageType::Physical);
    CheckFailure(*this, TEXT("One P2 beyond int64 max rejects"), LDTactical::EvaluateDamage(Input, *Catalog), EError::Overflow);
    Input.bTargetProtected = true;
    CheckFailure(*this, TEXT("Protection cannot mask overflow"), LDTactical::EvaluateDamage(Input, *Catalog), EError::Overflow);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoBasicTimingTest, "Lockdown.Rules.DemoCombatEvaluation.BasicTiming",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoBasicTimingTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!ReadApprovedFixture(*this, Catalog)) { return false; }
    auto Input = TimingInput(*Catalog);
    if (!CheckTiming(*this, TEXT("Compiled character base"), Input, *Catalog, 1112, 250)) { return false; }
    Input.CurrentAttackSpeed = Rate(27, 25);
    if (!CheckTiming(*this, TEXT("Changed AS changes both durations"), Input, *Catalog, 926, 209)) { return false; }
    Input.CurrentAttackSpeed = Rate(531, 500);
    if (!CheckTiming(*this, TEXT("Level-ten base AS"), Input, *Catalog, 942, 212)) { return false; }
    Input.CurrentAttackSpeed = Rate(711, 500);
    if (!CheckTiming(*this, TEXT("Approved AS envelope"), Input, *Catalog, 704, 159)) { return false; }
    Input.CurrentAttackSpeed = Catalog->Get().Parameters.AttackSpeed.Min;
    if (!CheckTiming(*this, TEXT("AS lower bound"), Input, *Catalog, 3334, 750)) { return false; }
    Input.CurrentAttackSpeed = Catalog->Get().Parameters.AttackSpeed.Max;
    if (!CheckTiming(*this, TEXT("AS upper bound"), Input, *Catalog, 400, 90)) { return false; }

    struct FMonsterCase { const TCHAR* Id; int64 Interval; int64 Windup; int64 FasterInterval; int64 FasterWindup; int64 Damage; };
    for (const auto& Case : {
        FMonsterCase{TEXT("monster_scavenger"), 1250, 350, 625, 175, 847},
        FMonsterCase{TEXT("monster_guardian"), 1539, 500, 770, 250, 4067}})
    {
        FString Error;
        const FLDDemoMonsterDefinition* Monster = nullptr;
        if (!Catalog->FindMonster(Case.Id, Monster, Error))
        {
            AddError(Error);
            return false;
        }
        Input.BaseAttackSpeed = Monster->AttackSpeed;
        Input.CurrentAttackSpeed = Monster->AttackSpeed;
        Input.BaseWindupMs = Monster->AttackWindupMs;
        if (!CheckTiming(*this, Case.Id, Input, *Catalog, Case.Interval, Case.Windup)) { return false; }
        TestEqual(TEXT("Compiled monster interval agrees with the common evaluator"), Monster->AttackIntervalMs, Case.Interval);
        Input.CurrentAttackSpeed = Rate(Monster->AttackSpeed.Numerator * 2, Monster->AttackSpeed.Denominator);
        if (!CheckTiming(*this, FString(Case.Id) + TEXT(" changed AS"), Input, *Catalog, Case.FasterInterval, Case.FasterWindup)) { return false; }
        auto Damage = PrimaryInput(*Catalog);
        Damage.SourceADP2 = Monster->AttackP2;
        if (!CheckDamage(*this, FString(Case.Id) + TEXT(" same physical evaluator"), Damage, *Catalog,
            {Case.Damage, 0, Case.Damage, 0, 62000 - Case.Damage, 0})) { return false; }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoTimingDomainTest, "Lockdown.Rules.DemoCombatEvaluation.TimingDomains",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoTimingDomainTest::RunTest(const FString& Parameters)
{
    using EError = ELDCombatEvaluationError;
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!ReadApprovedFixture(*this, Catalog)) { return false; }
    auto Input = TimingInput(*Catalog);
    Input.CurrentAttackSpeed = Rate(36028797018963967LL, 18014398509481984LL);
    if (!CheckTiming(*this, TEXT("Reciprocal just above an integer must ceil"), Input, *Catalog, 501, 113)) { return false; }
    Input.CurrentAttackSpeed = Rate(2);
    if (!CheckTiming(*this, TEXT("Exact reciprocal integer does not gain a millisecond"), Input, *Catalog, 500, 113)) { return false; }
    Input.CurrentAttackSpeed = Rate(899999999999999999LL, 1000000000000000000LL);
    if (!CheckTiming(*this, TEXT("Windup just above 250 is 251, not a double-rounded 250"), Input, *Catalog, 1112, 251)) { return false; }
    Input.CurrentAttackSpeed = Rate(900000000000000001LL, 1000000000000000000LL);
    if (!CheckTiming(*this, TEXT("Windup just below 250 ceils to 250"), Input, *Catalog, 1112, 250)) { return false; }

    CheckFailure(*this, TEXT("Missing compiled timing"), LDTactical::EvaluateBasicTiming({}, *Catalog), EError::Definition);
    Input = TimingInput(*Catalog);
    Input.BaseWindupMs.Reset();
    CheckFailure(*this, TEXT("Missing base windup"), LDTactical::EvaluateBasicTiming(Input, *Catalog), EError::Definition);
    Input = TimingInput(*Catalog);
    Input.CurrentAttackSpeed.Reset();
    CheckFailure(*this, TEXT("Missing current AS"), LDTactical::EvaluateBasicTiming(Input, *Catalog), EError::Numeric);
    for (int64 Denominator : {int64(0), int64(-1), MIN_int64})
    {
        Input = TimingInput(*Catalog);
        Input.CurrentAttackSpeed = Rate(1, Denominator);
        CheckFailure(*this, TEXT("Malformed current-AS denominator"), LDTactical::EvaluateBasicTiming(Input, *Catalog), EError::Numeric);
        Input = TimingInput(*Catalog);
        Input.BaseAttackSpeed = Rate(1, Denominator);
        CheckFailure(*this, TEXT("Malformed base-AS denominator"), LDTactical::EvaluateBasicTiming(Input, *Catalog), EError::Numeric);
    }
    for (int64 Nonpositive : {int64(0), int64(-1), MIN_int64})
    {
        Input = TimingInput(*Catalog);
        Input.CurrentAttackSpeed = Rate(Nonpositive);
        CheckFailure(*this, TEXT("Nonpositive current AS"), LDTactical::EvaluateBasicTiming(Input, *Catalog), EError::Domain);
        Input = TimingInput(*Catalog);
        Input.BaseAttackSpeed = Rate(Nonpositive);
        CheckFailure(*this, TEXT("Nonpositive base AS"), LDTactical::EvaluateBasicTiming(Input, *Catalog), EError::Domain);
        Input = TimingInput(*Catalog);
        Input.BaseWindupMs = Nonpositive;
        CheckFailure(*this, TEXT("Nonpositive base windup"), LDTactical::EvaluateBasicTiming(Input, *Catalog), EError::Domain);
    }
    for (const auto& Outside : {Rate(299999999999999999LL, 1000000000000000000LL),
        Rate(2500000000000000001LL, 1000000000000000000LL)})
    {
        Input = TimingInput(*Catalog);
        Input.CurrentAttackSpeed = Outside;
        CheckFailure(*this, TEXT("No silent AS clamp or floating tolerance"), LDTactical::EvaluateBasicTiming(Input, *Catalog), EError::Domain);
    }
    Input.BaseAttackSpeed = Rate(1);
    Input.CurrentAttackSpeed = Rate(2);
    Input.BaseWindupMs = int64(999);
    CheckFailure(*this, TEXT("Rounding must not make windup equal interval"), LDTactical::EvaluateBasicTiming(Input, *Catalog), EError::Domain);
    Input.BaseWindupMs = int64(998);
    if (!CheckTiming(*this, TEXT("Adjacent strict-duration boundary"), Input, *Catalog, 500, 499)) { return false; }
    Input = TimingInput(*Catalog);
    Input.CurrentAttackSpeed = Catalog->Get().Parameters.AttackSpeed.Min;
    Input.BaseWindupMs = MAX_int64;
    CheckFailure(*this, TEXT("Scaled windup overflow does not wrap or become ready"), LDTactical::EvaluateBasicTiming(Input, *Catalog), EError::Overflow);
    return true;
}

#endif
