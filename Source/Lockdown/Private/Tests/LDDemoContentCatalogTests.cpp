#include "Development/LDDemoContentCatalog.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "../Development/LDDemoContentJson.h"
#include "../Development/LDDemoContentMath.h"
#include "Algo/AllOf.h"
#include "Development/LDDemoCardText.h"
#include "Development/LDDemoContentText.h"
#include "Framework/LDDemoEntry.h"
#include "HAL/FileManager.h"
#include "Internationalization/StringTableCore.h"
#include "Internationalization/StringTableRegistry.h"
#include "Region/LDMapDefinition.h"
#include "Misc/AutomationTest.h"
#include "UI/LDTerms.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/ObjectReader.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace
{
using namespace LDDemoContent;

struct FCase
{
    FString Name;
    FLDDemoContentDigest Digest;
    TArray<FString> ChangedPaths;
    FString MutatedQuantity;
    FString ErrorContains;
};

struct FFixtures
{
    FString Root;
    TArray<FCase> Valid;
    TArray<FCase> Invalid;
};

bool FixtureIndex(FAutomationTestBase& Test, FFixtures& Out)
{
    FString Error;
    FCheck Check(Error);
    if (!FParse::Value(FCommandLine::Get(), TEXT("LDDemoContentFixtures="), Out.Root) || Out.Root.IsEmpty())
    {
        Test.AddError(TEXT("Missing -LDDemoContentFixtures=<new Artifacts directory>; run ExportDemoNativeFixtures.py first. No live data fallback."));
        return false;
    }
    if (!Test.TestTrue(TEXT("Fixtures are isolated below this project's Artifacts"),
        FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Out.Root),
            FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), TEXT("Artifacts")))))
    {
        return false;
    }
    TArray<uint8> Bytes;
    FValue Value;
    if (!ReadFile(FPaths::Combine(Out.Root, TEXT("index.json")), Bytes, Error)
        || !Parse(Bytes, Value, Check, TEXT("fixture index")))
    {
        Test.AddError(Error);
        return false;
    }
    FFields Index(Check, Value, TEXT("fixture index"));
    bool TestOnly;
    if (!Index.Literal(TEXT("format"), TEXT("demo-native-fixtures-1")) || !Index.Flag(TEXT("test_only"), TestOnly)
        || !Check.Require(TestOnly, Index.Path, TEXT("test-only marker is required")))
    {
        Test.AddError(Error);
        return false;
    }
    for (const auto& Pair : {TPair<const TCHAR*, TArray<FCase>*>(TEXT("valid"), &Out.Valid), {TEXT("invalid"), &Out.Invalid}})
    {
        const TArray<FValue>* Rows;
        if (!Array(Index.Take(Pair.Key), Rows, Check, Index.Path, 1, 1024))
        {
            Test.AddError(Error);
            return false;
        }
        TSet<FString> Names;
        for (const auto& RowValue : *Rows)
        {
            FFields Row(Check, RowValue, TEXT("fixture case"));
            FCase Case;
            if (!Row.Identifier(TEXT("name"), Case.Name) || !Row.Digest(TEXT("catalog_sha256"), Case.Digest)
                || !Check.Require(!Names.Contains(Case.Name), Row.Path, TEXT("duplicate case")))
            {
                Test.AddError(Error);
                return false;
            }
            Names.Add(Case.Name);
            if (Pair.Value == &Out.Valid)
            {
                const TArray<FValue>* Changes;
                if (!Array(Row.Take(TEXT("changed_paths")), Changes, Check, Row.Path, 0, MaxFileBytes)
                    || !Row.Text(TEXT("mutated_quantity"), Case.MutatedQuantity, 0))
                {
                    Test.AddError(Error);
                    return false;
                }
                for (const auto& Change : *Changes)
                {
                    FString Path;
                    if (!String(Change, Path, Check, Row.Path))
                    {
                        Test.AddError(Error);
                        return false;
                    }
                    Case.ChangedPaths.Add(MoveTemp(Path));
                }
            }
            else if (!Row.Text(TEXT("error_contains"), Case.ErrorContains))
            {
                Test.AddError(Error);
                return false;
            }
            if (!Row.Done())
            {
                Test.AddError(Error);
                return false;
            }
            Pair.Value->Add(MoveTemp(Case));
        }
    }
    if (!Index.Done())
    {
        Test.AddError(Error);
        return false;
    }
    return Test.TestTrue(TEXT("Baseline fixture is explicit"), Out.Valid[0].Name == TEXT("base"));
}

// DemoContent.TypedProjection fully imports and asserts every valid case. DemoWorld.TypedProjection, later in the same
// run, takes the catalog snapshot of the same fixture bytes from it instead of importing that content a second time; run
// on its own it imports the content itself.
TMap<FString, TSharedPtr<const FLDDemoContentSnapshot>>& SharedSnapshots()
{
    static TMap<FString, TSharedPtr<const FLDDemoContentSnapshot>> Snapshots;
    return Snapshots;
}

FString SnapshotKey(const FString& Root, const FCase& Case)
{
    return FPaths::ConvertRelativePathToFull(Root) + TEXT("|") + Case.Digest.ToHex();
}

void ShareSnapshot(const FString& Root, const FCase& Case, const TSharedPtr<const FLDDemoContentSnapshot>& Snapshot)
{
    SharedSnapshots().Add(SnapshotKey(Root, Case), Snapshot);
}

bool FlatStruct(const UStruct* Type, const void* Data, const FString& Prefix, TMap<FString, FString>& Out, FAutomationTestBase& Test);

bool FlatProperty(const FProperty* Property, const void* Data, const FString& Path, TMap<FString, FString>& Out, FAutomationTestBase& Test)
{
    if (const auto* Struct = CastField<FStructProperty>(Property))
    {
        return FlatStruct(Struct->Struct, Data, Path, Out, Test);
    }
    if (const auto* ArrayProperty = CastField<FArrayProperty>(Property))
    {
        FScriptArrayHelper Values(ArrayProperty, Data);
        Out.Add(Path + TEXT(".#"), LexToString(Values.Num()));
        for (int32 I = 0; I < Values.Num(); ++I)
        {
            if (!FlatProperty(ArrayProperty->Inner, Values.GetRawPtr(I), FString::Printf(TEXT("%s[%d]"), *Path, I), Out, Test))
            {
                return false;
            }
        }
        return true;
    }
    if (const auto* Enum = CastField<FEnumProperty>(Property))
    {
        Out.Add(Path, Enum->GetUnderlyingProperty()->GetNumericPropertyValueToString(Data));
        return true;
    }
    if (const auto* Bool = CastField<FBoolProperty>(Property))
    {
        Out.Add(Path, Bool->GetPropertyValue(Data) ? TEXT("true") : TEXT("false"));
        return true;
    }
    if (const auto* Text = CastField<FStrProperty>(Property))
    {
        Out.Add(Path, Text->GetPropertyValue(Data));
        return true;
    }
    if (const auto* Number = CastField<FNumericProperty>(Property); Number && Number->IsInteger())
    {
        Out.Add(Path, Number->GetNumericPropertyValueToString(Data));
        return true;
    }
    Test.AddError(TEXT("Unasserted native property type: ") + Path);
    return false;
}

bool FlatStruct(const UStruct* Type, const void* Data, const FString& Prefix, TMap<FString, FString>& Out, FAutomationTestBase& Test)
{
    for (TFieldIterator<FProperty> It(Type); It; ++It)
    {
        const FProperty* Property = *It;
        const FString Base = Prefix.IsEmpty() ? Property->GetName() : Prefix + TEXT(".") + Property->GetName();
        if (Property->ArrayDim > 1)
        {
            Out.Add(Base + TEXT(".#"), LexToString(Property->ArrayDim));
        }
        for (int32 I = 0; I < Property->ArrayDim; ++I)
        {
            const FString Path = Property->ArrayDim > 1 ? FString::Printf(TEXT("%s[%d]"), *Base, I) : Base;
            if (!FlatProperty(Property, Property->ContainerPtrToValuePtr<void>(Data, I), Path, Out, Test))
            {
                return false;
            }
        }
    }
    return true;
}

bool AssertFields(FAutomationTestBase& Test, const UStruct* Type, const void* Data, const FString& File,
    TMap<FString, FString>& Actual)
{
    FString Error;
    TArray<uint8> Bytes;
    FValue Expected;
    FCheck Check(Error);
    if (!ReadFile(File, Bytes, Error)
        || !Parse(Bytes, Expected, Check, TEXT("native expectations"))
        || !Check.Require(Expected->Type == EJson::Object, TEXT("native expectations"), TEXT("expected object")))
    {
        Test.AddError(Error);
        return false;
    }
    if (!FlatStruct(Type, Data, TEXT(""), Actual, Test))
    {
        return false;
    }
    const auto& Fields = Expected->AsObject()->Values;
    bool Good = Test.TestEqual(TEXT("Every reflected field and array count has an independent assertion"), Actual.Num(), Fields.Num());
    for (const auto& Pair : Fields)
    {
        const FString Path(Pair.Key.ToView());
        FString ExpectedText;
        if (!String(Pair.Value, ExpectedText, Check, Path, 0, 1024))
        {
            Test.AddError(Error);
            return false;
        }
        const FString* Found = Actual.Find(Path);
        Good &= Test.TestTrue(*FString::Printf(TEXT("Native member exists: %s"), *Path), Found != nullptr);
        if (Found)
        {
            Good &= Test.TestTrue(*FString::Printf(TEXT("%s matches exact native value"), *Path),
                Found->Equals(ExpectedText, ESearchCase::CaseSensitive));
        }
    }
    return Good;
}

bool AssertProjection(FAutomationTestBase& Test, const FLDDemoContentSnapshot& Snapshot, const FString& Root,
    TMap<FString, FString>& Actual)
{
    return AssertFields(Test, FLDDemoContentDefinitions::StaticStruct(), &Snapshot.Get(),
        FPaths::Combine(Root, TEXT("native_expected.json")), Actual);
}

void QuantityPaths(const FValue& Value, const FString& Path, TSet<FString>& Out)
{
    if (Value->Type == EJson::Object)
    {
        const auto& Fields = Value->AsObject()->Values;
        if (Value->AsObject()->HasField(TEXT("unit")))
        {
            Out.Add(Path);
            return;
        }
        for (const auto& Pair : Fields)
        {
            QuantityPaths(Pair.Value, Path + TEXT(".") + FString(Pair.Key.ToView()), Out);
        }
    }
    else if (Value->Type == EJson::Array)
    {
        const auto& Values = Value->AsArray();
        for (int32 I = 0; I < Values.Num(); ++I)
        {
            QuantityPaths(Values[I], FString::Printf(TEXT("%s[%d]"), *Path, I), Out);
        }
    }
}

// #198: the quantities of every active's single value set (cost, cooldown and every effect term).
bool ValueQuantityPaths(const FLDDemoContentBundle& Bundle, TSet<FString>& Out, FString& Error)
{
    FValue Root;
    FCheck Check(Error);
    if (!Parse(Bundle.Content, Root, Check, TEXT("baseline content")))
    {
        return false;
    }
    const auto& Abilities = Root->AsObject()->GetArrayField(TEXT("abilities"));
    for (int32 I = 0; I < Abilities.Num(); ++I)
    {
        for (const TCHAR* Field : {TEXT("mana_cost"), TEXT("cooldown"), TEXT("effects")})
        {
            QuantityPaths(Abilities[I]->AsObject()->TryGetField(Field), FString::Printf(TEXT("content.abilities[%d].%s"), I, Field), Out);
        }
    }
    return true;
}

using FValidateProjection = TFunctionRef<bool(FString&)>;

bool MutateStruct(const UStruct* Type, void* Data, const FString& Prefix, FValidateProjection Validate,
    FAutomationTestBase& Test, int32& Count);

bool MutateProperty(FProperty* Property, void* Data, const FString& Path, FValidateProjection Validate,
    FAutomationTestBase& Test, int32& Count)
{
    if (auto* Struct = CastField<FStructProperty>(Property))
    {
        return MutateStruct(Struct->Struct, Data, Path, Validate, Test, Count);
    }
    if (auto* ArrayProperty = CastField<FArrayProperty>(Property))
    {
        FScriptArrayHelper Values(ArrayProperty, Data);
        for (int32 I = 0; I < Values.Num(); ++I)
        {
            if (!MutateProperty(ArrayProperty->Inner, Values.GetRawPtr(I), FString::Printf(TEXT("%s[%d]"), *Path, I),
                Validate, Test, Count))
            {
                return false;
            }
        }
        return true;
    }
    FString Error;
    bool bRejected = false;
    if (auto* Enum = CastField<FEnumProperty>(Property))
    {
        Property = Enum->GetUnderlyingProperty();
    }
    if (auto* Bool = CastField<FBoolProperty>(Property))
    {
        const bool Old = Bool->GetPropertyValue(Data);
        Bool->SetPropertyValue(Data, !Old);
        bRejected = !Validate(Error);
        Bool->SetPropertyValue(Data, Old);
    }
    else if (auto* Text = CastField<FStrProperty>(Property))
    {
        const FString Old = Text->GetPropertyValue(Data);
        FString CaseOnly = Old.ToUpper();
        if (CaseOnly.Equals(Old, ESearchCase::CaseSensitive)) { CaseOnly = Old.ToLower(); }
        if (!CaseOnly.Equals(Old, ESearchCase::CaseSensitive))
        {
            Text->SetPropertyValue(Data, CaseOnly);
            const bool CaseRejected = !Validate(Error);
            Text->SetPropertyValue(Data, Old);
            if (!Test.TestTrue(*FString::Printf(TEXT("Case-only persisted mutation rejects %s"), *Path),
                CaseRejected && !Error.IsEmpty()))
            {
                return false;
            }
        }
        Text->SetPropertyValue(Data, Old + TEXT("_corrupt"));
        bRejected = !Validate(Error);
        Text->SetPropertyValue(Data, Old);
    }
    else if (auto* Number = CastField<FNumericProperty>(Property); Number && Number->IsInteger())
    {
        const uint64 Old = Number->GetUnsignedIntPropertyValue(Data);
        Number->SetIntPropertyValue(Data, Old ^ uint64(1));
        bRejected = !Validate(Error);
        Number->SetIntPropertyValue(Data, Old);
    }
    else
    {
        Test.AddError(TEXT("Unmutated native property: ") + Path);
        return false;
    }
    ++Count;
    return Test.TestTrue(*FString::Printf(TEXT("Whole projection rejects changed %s"), *Path), bRejected && !Error.IsEmpty());
}

bool MutateStruct(const UStruct* Type, void* Data, const FString& Prefix, FValidateProjection Validate,
    FAutomationTestBase& Test, int32& Count)
{
    for (TFieldIterator<FProperty> It(Type); It; ++It)
    {
        FProperty* Property = *It;
        const FString Base = Prefix.IsEmpty() ? Property->GetName() : Prefix + TEXT(".") + Property->GetName();
        for (int32 I = 0; I < Property->ArrayDim; ++I)
        {
            const FString Path = Property->ArrayDim > 1 ? FString::Printf(TEXT("%s[%d]"), *Base, I) : Base;
            if (!MutateProperty(Property, Property->ContainerPtrToValuePtr<void>(Data, I), Path, Validate, Test, Count))
            {
                return false;
            }
        }
    }
    return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoContentProjectionTest, "Lockdown.Rules.DemoContent.TypedProjection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoContentProjectionTest::RunTest(const FString& Parameters)
{
    FFixtures Fixtures;
    if (!FixtureIndex(*this, Fixtures)) { return false; }
    TMap<FString, FString> Baseline;
    TSet<FString> UnchangedValueQuantities;
    FString Error;
    for (const auto& Case : Fixtures.Valid)
    {
        const FString Root = FPaths::Combine(Fixtures.Root, Case.Name);
        FLDDemoContentBundle Bundle;
        TUniquePtr<FLDDemoPreparedContent> Prepared;
        auto* Asset = NewObject<ULDDemoContentCatalog>(GetTransientPackage());
        if (!FLDDemoContentBundle::ReadImportFiles(Root, Bundle, Error)
            || !ULDDemoContentCatalog::PrepareImport(*Asset, Bundle, Case.Digest, Prepared, Error))
        {
            AddError(Case.Name + TEXT(": ") + Error);
            return false;
        }
        TSharedPtr<const FLDDemoContentSnapshot> Snapshot;
        if (!Asset->CommitImport(Prepared, Case.Digest, Error) || !Asset->Read(Case.Digest, Snapshot, Error))
        {
            AddError(Case.Name + TEXT(": ") + Error);
            return false;
        }
        TMap<FString, FString> Actual;
        if (!AssertProjection(*this, *Snapshot, Root, Actual)) { return false; }
        if (Case.MutatedQuantity.IsEmpty()) { ShareSnapshot(Root, Case, Snapshot); }
        if (Case.Name == TEXT("base"))
        {
            Baseline = Actual;
            if (!ValueQuantityPaths(Bundle, UnchangedValueQuantities, Error))
            {
                AddError(Error);
                return false;
            }
            const FUTF8ToTCHAR Decoded(reinterpret_cast<const ANSICHAR*>(Bundle.Content.GetData()), Bundle.Content.Num());
            const FString Canonical(Decoded.Length(), Decoded.Get());
            const int32 MP = Canonical.Find(TEXT("\"STAT.MPRegen.Base\""), ESearchCase::CaseSensitive);
            const int32 Magic = Canonical.Find(TEXT("\"STAT.MagicResist.Base\""), ESearchCase::CaseSensitive);
            TestTrue(TEXT("Compiler case-sensitive MPRegen-before-MagicResist key order survives native canonical validation"),
                MP >= 0 && Magic > MP);
        }
        for (const FString& Path : Case.ChangedPaths)
        {
            const FString* Original = Baseline.Find(Path);
            const FString* Changed = Actual.Find(Path);
            TestTrue(*FString::Printf(TEXT("%s changes native %s, not merely a digest"), *Case.Name, *Path),
                Original && Changed && *Original != *Changed && !Path.StartsWith(TEXT("Identity.")));
        }
        if (!Case.MutatedQuantity.IsEmpty())
        {
            TestTrue(TEXT("Each active value quantity mutation is unique and has a native change assertion"),
                UnchangedValueQuantities.Remove(Case.MutatedQuantity) == 1 && !Case.ChangedPaths.IsEmpty());
        }
        const FLDDemoAbilityDefinition* Ability = nullptr;
        const FLDDemoStatDefinition* Stat = nullptr;
        const FLDDemoMonsterDefinition* Monster = nullptr;
        const FLDDemoDisplayDefinition* Display = nullptr;
        TestTrue(TEXT("Typed lookups consume enabled definitions"),
            Snapshot->FindAbility(TEXT("active_snark_bolt"), Ability, Error)
            && Snapshot->FindStat(TEXT("stat_brains"), Stat, Error)
            && Snapshot->FindMonster(TEXT("monster_guardian"), Monster, Error)
            && Snapshot->FindDisplay(TEXT("camp_returning"), Display, Error));
        const auto* OldAbility = Ability;
        TestFalse(TEXT("Unknown definition is explicit failure"), Snapshot->FindAbility(TEXT("active_unknown"), Ability, Error));
        TestTrue(TEXT("Lookup failure is not a default definition"), !Error.IsEmpty() && Ability == OldAbility);
        // Without case-preserving names a packaged build returns the first registered spelling; lookups fold to the
        // canonical identity instead of refusing it. Author input stays strictly lowercase at import.
        TestTrue(TEXT("A case alias resolves to the canonical definition"),
            Snapshot->FindAbility(TEXT("Active_snark_bolt"), Ability, Error) && Ability == OldAbility);
    }
    TestEqual(TEXT("Every active value quantity has a compiler-backed native change case"), UnchangedValueQuantities.Num(), 0);

    FCheck Check(Error);
    const ANSICHAR* BoundaryText = "{\n  \"max\": 9223372036854775807,\n  \"min\": -9223372036854775808\n}\n";
    TArray<uint8> BoundaryBytes;
    BoundaryBytes.Append(reinterpret_cast<const uint8*>(BoundaryText), FCStringAnsi::Strlen(BoundaryText));
    FValue Boundary;
    int64 Minimum = 0, Maximum = 0;
    if (TestTrue(TEXT("Exact signed boundary tokens parse"), Parse(BoundaryBytes, Boundary, Check, TEXT("integer boundary"))))
    {
        FFields Fields(Check, Boundary, TEXT("integer boundary"));
        TestTrue(TEXT("Boundary reads never traverse double"), Fields.Int(TEXT("min"), Minimum, MIN_int64)
            && Fields.Int(TEXT("max"), Maximum) && Fields.Done());
        TestEqual(TEXT("INT64_MIN is exact"), Minimum, MIN_int64);
        TestEqual(TEXT("INT64_MAX is exact"), Maximum, MAX_int64);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoContentRefusalTest, "Lockdown.Rules.DemoContent.StrictRefusal",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoContentRefusalTest::RunTest(const FString& Parameters)
{
    FFixtures Fixtures;
    if (!FixtureIndex(*this, Fixtures)) { return false; }
    FString Error;
    FLDDemoContentBundle Baseline;
    TUniquePtr<FLDDemoPreparedContent> Prepared;
    auto* Asset = NewObject<ULDDemoContentCatalog>(GetTransientPackage());
    const FCase& Base = Fixtures.Valid[0];
    if (!FLDDemoContentBundle::ReadImportFiles(FPaths::Combine(Fixtures.Root, Base.Name), Baseline, Error)
        || !ULDDemoContentCatalog::PrepareImport(*Asset, Baseline, Base.Digest, Prepared, Error))
    {
        AddError(Error);
        return false;
    }
    if (!Asset->CommitImport(Prepared, Base.Digest, Error)
        || !ULDDemoContentCatalog::PrepareImport(*Asset, Baseline, Base.Digest, Prepared, Error))
    {
        AddError(Error);
        return false;
    }
    const FLDDemoPreparedContent* Original = Prepared.Get();
    for (const auto& Case : Fixtures.Invalid)
    {
        FLDDemoContentBundle Input = Baseline;
        Error.Reset();
        const bool Read = FLDDemoContentBundle::ReadImportFiles(FPaths::Combine(Fixtures.Root, Case.Name), Input, Error);
        const bool Accepted = Read && ULDDemoContentCatalog::PrepareImport(*Asset, Input, Case.Digest, Prepared, Error);
        TestFalse(*FString::Printf(TEXT("Reject %s"), *Case.Name), Accepted);
        TestTrue(*FString::Printf(TEXT("%s explains the actual refusal: %s"), *Case.Name, *Error),
            !Error.IsEmpty() && Error.Contains(Case.ErrorContains));
        TestTrue(TEXT("Failure preserves the previous complete prepared definition"), Prepared.Get() == Original);
        if (!Read)
        {
            TestTrue(TEXT("Missing file leaves the entire caller bundle unchanged"),
                FLDDemoContentBundle::StaticStruct()->CompareScriptStruct(&Input, &Baseline, 0));
        }
        if (!Asset->ValidatePrepared(*Prepared, Error))
        {
            AddError(TEXT("Failed import altered last valid asset: ") + Error);
            return false;
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoContentAssetTest, "Lockdown.Rules.DemoContent.AtomicAssetRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoContentAssetTest::RunTest(const FString& Parameters)
{
    FFixtures Fixtures;
    if (!FixtureIndex(*this, Fixtures)) { return false; }
    const FCase& Base = Fixtures.Valid[0];
    FString Error;
    auto* Asset = NewObject<ULDDemoContentCatalog>(GetTransientPackage());
    TSharedPtr<const FLDDemoContentSnapshot> First;
    TestFalse(TEXT("An empty asset cannot validate itself"), Asset->Validate(Base.Digest, Error));
    TestTrue(TEXT("Empty asset gives an explicit reason"), !Error.IsEmpty());
    TUniquePtr<FLDDemoPreparedContent> Prepared, Expected;
    const FString BaseRoot = FPaths::Combine(Fixtures.Root, Base.Name);
    if (!ULDDemoContentCatalog::PrepareImportFromFiles(*Asset, BaseRoot, Base.Digest, Prepared, Error)
        || !Asset->CommitImport(Prepared, Base.Digest, Error)
        || !ULDDemoContentCatalog::PrepareImportFromFiles(*Asset, BaseRoot, Base.Digest, Expected, Error)
        || !Asset->ReadRuntimeFiles(FPaths::Combine(BaseRoot, TEXT("Content\\Lockdown\\Data\\Demo")), Base.Digest, First, Error))
    {
        AddError(Error);
        return false;
    }
    TestFalse(TEXT("A consumed prepared import cannot be committed twice"), Asset->CommitImport(Prepared, Base.Digest, Error));
    TArray<uint8> Serialized;
    FObjectWriter Writer(Asset, Serialized, true, true, false);
    if (!TestFalse(TEXT("DataAsset serializes successfully"), Writer.IsError())) { return false; }
    auto* Restored = NewObject<ULDDemoContentCatalog>(GetTransientPackage());
    FObjectReader Reader(Restored, Serialized, true, true);
    if (!TestFalse(TEXT("DataAsset deserializes successfully"), Reader.IsError())
        || !TestTrue(TEXT("All saved bytes and typed fields survive roundtrip"), Restored->ValidatePrepared(*Expected, Error)))
    {
        AddError(Error);
        return false;
    }
    TSharedPtr<const FLDDemoContentSnapshot> RestoredSnapshot;
    if (!Restored->Read(Base.Digest, RestoredSnapshot, Error))
    {
        AddError(Error);
        return false;
    }
    TMap<FString, FString> Assertions;
    if (!AssertProjection(*this, *RestoredSnapshot, BaseRoot, Assertions)) { return false; }
    auto* ProjectionProperty = FindFProperty<FStructProperty>(ULDDemoContentCatalog::StaticClass(), TEXT("Definitions"));
    if (!TestNotNull(TEXT("Private persisted projection exists"), ProjectionProperty)) { return false; }
    auto* Stored = ProjectionProperty->ContainerPtrToValuePtr<FLDDemoContentDefinitions>(Restored);
    int32 MutationCount = 0;
    if (!MutateStruct(FLDDemoContentDefinitions::StaticStruct(), Stored, TEXT(""),
        [&](FString& Why) { return Restored->ValidatePrepared(*Expected, Why); }, *this, MutationCount))
    {
        return false;
    }
    int32 ArrayCounts = 0;
    for (const auto& Pair : Assertions)
    {
        ArrayCounts += Pair.Key.EndsWith(TEXT(".#")) ? 1 : 0;
    }
    TestEqual(TEXT("Every stored scalar, including inactive variant members, rejects tampering"),
        MutationCount, Assertions.Num() - ArrayCounts);
    const FLDDemoContentDefinitions Original = *Stored;
    Stored->Abilities.RemoveAt(0);
    TestFalse(TEXT("A dropped definition is not a valid partial asset"), Restored->ValidatePrepared(*Expected, Error));
    *Stored = Original;
    Stored->Abilities[0].Values.Effects.AddDefaulted();
    TestFalse(TEXT("An extra stored effect is rejected"), Restored->ValidatePrepared(*Expected, Error));
    *Stored = Original;
    TestTrue(TEXT("Restoring test mutation recovers exact original projection"), Restored->ValidatePrepared(*Expected, Error));
    Stored->Abilities[0].Id = Stored->Abilities[0].Id.ToUpper();
    TArray<uint8> BadSerialized;
    FObjectWriter BadWriter(Restored, BadSerialized, true, true, false);
    auto* BadRestored = NewObject<ULDDemoContentCatalog>(GetTransientPackage());
    FObjectReader BadReader(BadRestored, BadSerialized, true, true);
    TestTrue(TEXT("Case-corrupted projection can be serialized but remains invalid after roundtrip"),
        !BadWriter.IsError() && !BadReader.IsError() && !BadRestored->Validate(Base.Digest, Error));
    *Stored = Original;

    const FCase* NewCase = Fixtures.Valid.FindByPredicate([](const FCase& Case) { return Case.Name == TEXT("exact_integer"); });
    if (!TestNotNull(TEXT("Changed-root fixture exists"), NewCase)) { return false; }
    TSharedPtr<const FLDDemoContentSnapshot> Current = First;
    TestFalse(TEXT("Saved asset is stale against a new externally expected root"), Asset->Read(NewCase->Digest, Current, Error));
    TestTrue(TEXT("Failure does not replace the previous immutable snapshot"), Current == First && !Error.IsEmpty());
    const FString NewRoot = FPaths::Combine(Fixtures.Root, NewCase->Name);
    TestFalse(TEXT("New staged root cannot authorize an old saved projection"),
        Asset->ReadRuntimeFiles(FPaths::Combine(NewRoot, TEXT("Content\\Lockdown\\Data\\Demo")), NewCase->Digest, Current, Error));
    TestFalse(TEXT("Absent staged files never use the stored bytes as fallback"),
        Asset->ReadRuntimeFiles(FPaths::Combine(Fixtures.Root, TEXT("absent")), Base.Digest, Current, Error));
    if (!ULDDemoContentCatalog::PrepareImportFromFiles(*Asset, NewRoot, NewCase->Digest, Prepared, Error)
        || !Asset->CommitImport(Prepared, NewCase->Digest, Error)
        || !Asset->ReadRuntimeFiles(FPaths::Combine(NewRoot, TEXT("Content\\Lockdown\\Data\\Demo")), NewCase->Digest, Current, Error))
    {
        AddError(Error);
        return false;
    }
    TestEqual(TEXT("New snapshot preserves an above-2^53 integer"), Current->Get().Parameters.AirdropRewardXP, int64(9007199254740993LL));
    TestEqual(TEXT("Previous snapshot keeps its original definitions"), First->Get().Parameters.AirdropRewardXP, Original.Parameters.AirdropRewardXP);
    TestFalse(TEXT("New asset refuses the prior root identity"), Asset->Validate(Base.Digest, Error));

    auto* RaceAsset = NewObject<ULDDemoContentCatalog>(GetTransientPackage());
    TUniquePtr<FLDDemoPreparedContent> Older, Newer;
    if (!ULDDemoContentCatalog::PrepareImportFromFiles(*RaceAsset, BaseRoot, Base.Digest, Older, Error)
        || !ULDDemoContentCatalog::PrepareImportFromFiles(*RaceAsset, NewRoot, NewCase->Digest, Newer, Error)
        || !RaceAsset->CommitImport(Newer, NewCase->Digest, Error))
    {
        AddError(Error);
        return false;
    }
    TestFalse(TEXT("A-prepare, B-commit, A-commit cannot roll the target back"), RaceAsset->CommitImport(Older, Base.Digest, Error));
    TestTrue(TEXT("Stale generation is reported"), Error.Contains(TEXT("stale import generation")));
    TestTrue(TEXT("B remains valid after stale A rejection"), RaceAsset->Validate(NewCase->Digest, Error));
    // The exported fixtures are reused read-only by later runs, so the source-change check edits a private copy.
    const FString ScratchRoot = FPaths::CreateTempFilename(*FPaths::ProjectIntermediateDir(), TEXT("LDFixtureScratch"));
    ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*ScratchRoot, false, true); };
    TArray<FString> BaseFiles;
    IFileManager::Get().FindFilesRecursive(BaseFiles, *BaseRoot, TEXT("*"), true, false);
    FString BasePrefix = BaseRoot;
    FPaths::NormalizeDirectoryName(BasePrefix);
    BasePrefix += TEXT("/");
    bool bCopied = !BaseFiles.IsEmpty();
    for (const FString& File : BaseFiles)
    {
        FString Normal = File;
        FPaths::NormalizeFilename(Normal);
        bCopied &= Normal.StartsWith(BasePrefix, ESearchCase::CaseSensitive)
            && IFileManager::Get().Copy(*FPaths::Combine(ScratchRoot, Normal.RightChop(BasePrefix.Len())), *File) == COPY_OK;
    }
    if (!TestTrue(TEXT("The base case is copied for the source-change check"), bCopied)) { return false; }
    if (!ULDDemoContentCatalog::PrepareImportFromFiles(*RaceAsset, ScratchRoot, Base.Digest, Prepared, Error))
    {
        AddError(Error);
        return false;
    }
    TestFalse(TEXT("Prepared import cannot commit to a different object"), Asset->CommitImport(Prepared, Base.Digest, Error));
    TestFalse(TEXT("Fresh import still checks the current independently expected root"), RaceAsset->CommitImport(Prepared, NewCase->Digest, Error));
    TestTrue(TEXT("Current-root change is reported"), Error.Contains(TEXT("current expected root changed")));

    const FString SourceFile = FPaths::Combine(ScratchRoot, TEXT("Content\\Lockdown\\Data\\Demo\\demo_content.json"));
    TArray<uint8> OriginalSource;
    if (!ReadFile(SourceFile, OriginalSource, Error))
    {
        AddError(Error);
        return false;
    }
    TArray<uint8> ChangedSource = OriginalSource;
    ChangedSource.Add(uint8('\n'));
    const bool WroteChange = FFileHelper::SaveArrayToFile(ChangedSource, *SourceFile);
    const bool CommittedChangedSource = WroteChange && RaceAsset->CommitImport(Prepared, Base.Digest, Error);
    const FString ChangedError = Error;
    const bool RestoredSource = FFileHelper::SaveArrayToFile(OriginalSource, *SourceFile);
    TestTrue(TEXT("Only the private copy of the fixture source is changed and restored"), WroteChange && RestoredSource);
    TestFalse(TEXT("Changed source between prepare and commit rejects the token"), CommittedChangedSource);
    TestTrue(TEXT("Source-snapshot conflict is reported"), ChangedError.Contains(TEXT("source bytes changed")));
    if (!RestoredSource) { return false; }
    TestTrue(TEXT("B still survives a changed-file import"), RaceAsset->Validate(NewCase->Digest, Error));
    if (!ULDDemoContentCatalog::PrepareImportFromFiles(*RaceAsset, ScratchRoot, Base.Digest, Prepared, Error)
        || !RaceAsset->CommitImport(Prepared, Base.Digest, Error))
    {
        AddError(Error);
        return false;
    }
    TestTrue(TEXT("Explicit fresh preparation permits intentional reimport"), RaceAsset->Validate(Base.Digest, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoWorldProjectionTest, "Lockdown.Rules.DemoWorld.TypedProjection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoWorldProjectionTest::RunTest(const FString& Parameters)
{
    FFixtures Fixtures;
    if (!FixtureIndex(*this, Fixtures)) { return false; }
    FString Error;
    TArray<uint8> BaseWorld, BaseExpected;
    int32 Imported = 0, SharedBase = 0;
    for (const auto& Case : Fixtures.Valid)
    {
        const FString Root = FPaths::Combine(Fixtures.Root, Case.Name);
        const FString ExpectedFile = FPaths::Combine(Root, TEXT("native_world_expected.json"));
        FLDDemoContentBundle Bundle;
        TArray<uint8> Expected;
        if (!FLDDemoContentBundle::ReadImportFiles(Root, Bundle, Error) || !ReadFile(ExpectedFile, Expected, Error))
        {
            AddError(Case.Name + TEXT(": ") + Error);
            return false;
        }
        if (!Case.MutatedQuantity.IsEmpty())
        {
            // An active value mutation only changes an active's value set. Its world payload and expected world
            // projection must match base byte for byte, so importing it again would only repeat base's assertions.
            if (!TestTrue(*FString::Printf(TEXT("%s keeps the base world payload and expected world projection"), *Case.Name),
                !BaseWorld.IsEmpty() && Bundle.World == BaseWorld && Expected == BaseExpected)) { return false; }
            ++SharedBase;
            continue;
        }
        TSharedPtr<const FLDDemoContentSnapshot> Catalog = SharedSnapshots().FindRef(SnapshotKey(Root, Case));
        if (!Catalog)
        {
            auto* Content = NewObject<ULDDemoContentCatalog>(GetTransientPackage());
            TUniquePtr<FLDDemoPreparedContent> Prepared;
            if (!ULDDemoContentCatalog::PrepareImport(*Content, Bundle, Case.Digest, Prepared, Error)
                || !Content->CommitImport(Prepared, Case.Digest, Error) || !Content->Read(Case.Digest, Catalog, Error))
            {
                AddError(Case.Name + TEXT(": ") + Error);
                return false;
            }
        }
        auto* World = NewObject<ULDMapDefinition>(GetTransientPackage());
        TSharedPtr<const FLDDemoWorldGeometry> Geometry;
        if (!World->Import(Bundle.World, *Catalog, Error) || !World->Read(*Catalog, Geometry, Error))
        {
            AddError(Case.Name + TEXT(": ") + Error);
            return false;
        }
        TMap<FString, FString> Actual;
        if (!AssertFields(*this, FLDDemoWorldGeometry::StaticStruct(), Geometry.Get(), ExpectedFile, Actual)) { return false; }
        // The fields above follow the exported payload; only layout-independent structure is asserted here.
        TestTrue(TEXT("World keeps at least its four boundary walls"), Geometry->Blockers.Num() >= 4);
        if (Case.Name == TEXT("base"))
        {
            BaseWorld = Bundle.World;
            BaseExpected = Expected;
        }
        ++Imported;
    }
    TestTrue(TEXT("Every non-value-mutation case is fully imported and every case is covered"),
        !BaseWorld.IsEmpty() && Imported > 1 && Imported + SharedBase == Fixtures.Valid.Num());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoWorldSignedGeometryTest, "Lockdown.Rules.DemoWorld.ExactSignedGeometry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoWorldSignedGeometryTest::RunTest(const FString& Parameters)
{
    FString Error;
    LDDemoContent::FCheck Check(Error);
    LDDemoContent::FNumbers Numbers(Check);
    const int64 Values[] = {MIN_int64, -5000, -251, -1, 0, 1, 251, 5000, MAX_int64};
    for (int64 A : Values)
    {
        for (int64 B : Values)
        {
            const auto Left = Numbers.Value(A), Right = Numbers.Value(B);
            TestEqual(TEXT("Signed exact <= matches native int64 order"), Numbers.LE(Left, Right), A <= B);
            TestEqual(TEXT("Signed exact < matches native int64 order"), Numbers.LT(Left, Right), A < B);
            TestEqual(TEXT("Signed exact equality matches native int64"), Numbers.EQ(Left, Right), A == B);
        }
    }
    TestTrue(TEXT("Negative fractional geometry compares by exact magnitude"),
        Numbers.LT(Numbers.Value(-3, 2), Numbers.Value(-1))
        && Numbers.LT(Numbers.Value(-1), Numbers.Value(-1, 2)));
    TestTrue(TEXT("Signed subtraction and reduction preserve negative coordinates"),
        Numbers.EQ(Numbers.Sub(Numbers.Value(1, 2), Numbers.Value(3)), Numbers.Value(-5, 2)));
    TestTrue(TEXT("Small signed products use the same exact result as scratch arithmetic"),
        Numbers.EQ(Numbers.Mul(Numbers.Value(int64(1) << 30), Numbers.Value(-(int64(1) << 30))),
            Numbers.Value(-(int64(1) << 60))));
    TestTrue(TEXT("Products outside the native fast path retain the wide exact result"),
        Numbers.EQ(Numbers.Mul(Numbers.Value(MAX_int64), Numbers.Value(2)),
            Numbers.Add(Numbers.Value(MAX_int64), Numbers.Value(MAX_int64))));
    TestTrue(TEXT("Non-unit denominators still reduce exactly"),
        Numbers.EQ(Numbers.Add(Numbers.Value(1, 3), Numbers.Value(1, 6)), Numbers.Value(1, 2)));
    TestFalse(TEXT("A negative result cannot pass the nonnegative native projection bound"),
        Numbers.NativePair(Numbers.Value(-1), TEXT("negative test result")));
    TestTrue(TEXT("Negative native bound refusal is explicit"), !Error.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoWorldAssetTest, "Lockdown.Rules.DemoWorld.AtomicAssetRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoWorldAssetTest::RunTest(const FString& Parameters)
{
    FFixtures Fixtures;
    if (!FixtureIndex(*this, Fixtures)) { return false; }
    const FCase& Base = Fixtures.Valid[0];
    const FString Root = FPaths::Combine(Fixtures.Root, Base.Name);
    FString Error;
    FLDDemoContentBundle Bundle;
    auto* Content = NewObject<ULDDemoContentCatalog>(GetTransientPackage());
    TUniquePtr<FLDDemoPreparedContent> Prepared;
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!FLDDemoContentBundle::ReadImportFiles(Root, Bundle, Error)
        || !ULDDemoContentCatalog::PrepareImport(*Content, Bundle, Base.Digest, Prepared, Error)
        || !Content->CommitImport(Prepared, Base.Digest, Error) || !Content->Read(Base.Digest, Catalog, Error))
    {
        AddError(Error);
        return false;
    }
    auto* Asset = NewObject<ULDMapDefinition>(GetTransientPackage());
    TSharedPtr<const FLDDemoWorldGeometry> First;
    TestFalse(TEXT("An empty world asset has no success-shaped projection"), Asset->Read(*Catalog, First, Error));
    if (!Asset->Import(Bundle.World, *Catalog, Error) || !Asset->Read(*Catalog, First, Error))
    {
        AddError(Error);
        return false;
    }
    TestTrue(TEXT("Repeat complete import replaces instead of appending"), Asset->Import(Bundle.World, *Catalog, Error));
    TArray<uint8> Changed = Bundle.World;
    Changed.Add(uint8('\n'));
    TestFalse(TEXT("Import refuses bytes outside the trusted world digest"), Asset->Import(Changed, *Catalog, Error));
    TSharedPtr<const FLDDemoWorldGeometry> AfterFailure;
    TestTrue(TEXT("Failed import preserves the prior complete projection"), Asset->Read(*Catalog, AfterFailure, Error));
    TArray<uint8> Serialized;
    FObjectWriter Writer(Asset, Serialized, true, true, false);
    auto* Restored = NewObject<ULDMapDefinition>(GetTransientPackage());
    FObjectReader Reader(Restored, Serialized, true, true);
    TSharedPtr<const FLDDemoWorldGeometry> RestoredGeometry;
    if (!TestTrue(TEXT("Typed world asset roundtrips"), !Writer.IsError() && !Reader.IsError()
        && Restored->Read(*Catalog, RestoredGeometry, Error)))
    {
        AddError(Error);
        return false;
    }
    TMap<FString, FString> Assertions;
    if (!AssertFields(*this, FLDDemoWorldGeometry::StaticStruct(), RestoredGeometry.Get(),
        FPaths::Combine(Root, TEXT("native_world_expected.json")), Assertions)) { return false; }
    auto* Property = FindFProperty<FStructProperty>(ULDMapDefinition::StaticClass(), TEXT("DemoGeometry"));
    if (!TestNotNull(TEXT("World's persisted typed geometry exists"), Property)) { return false; }
    auto* Stored = Property->ContainerPtrToValuePtr<FLDDemoWorldGeometry>(Restored);
    int32 Count = 0, ArrayCounts = 0;
    TUniquePtr<FLDDemoPreparedWorld> Expected;
    if (!ULDMapDefinition::PrepareProjection(Bundle.World, *Catalog, Expected, Error))
    {
        AddError(Error);
        return false;
    }
    const auto Validate = [&](FString& Why)
    {
        return Restored->ValidatePrepared(*Expected, Why);
    };
    if (!MutateStruct(FLDDemoWorldGeometry::StaticStruct(), Stored, TEXT("world"), Validate, *this, Count)) { return false; }
    for (const auto& Pair : Assertions) { ArrayCounts += Pair.Key.EndsWith(TEXT(".#")) ? 1 : 0; }
    TestEqual(TEXT("Every stored world scalar rejects independent mutation"), Count, Assertions.Num() - ArrayCounts);
    const FLDDemoWorldGeometry Original = *Stored;
    Stored->Blockers[0].PolygonCm.Pop();
    TestFalse(TEXT("Truncated stored polygon cannot use a valid source checksum"), Validate(Error));
    *Stored = Original;
    Stored->Anchors[0].Id = Stored->Anchors[0].Id.ToUpper();
    TArray<uint8> Corrupted;
    FObjectWriter BadWriter(Restored, Corrupted, true, true, false);
    auto* BadRestored = NewObject<ULDMapDefinition>(GetTransientPackage());
    FObjectReader BadReader(BadRestored, Corrupted, true, true);
    TestTrue(TEXT("Case-corrupted world remains invalid after serialization"), !BadWriter.IsError()
        && !BadReader.IsError() && !BadRestored->Read(*Catalog, AfterFailure, Error));
    *Stored = Original;
    TestTrue(TEXT("Restoring the isolated mutations recovers the original asset"), Validate(Error));
    TestEqual(TEXT("Earlier immutable world snapshot remains unchanged"), First->Anchors.Num(), Original.Anchors.Num());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoContentDisplayStringTableTest, "Lockdown.Rules.DemoContent.DisplayStringTable",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoContentDisplayStringTableTest::RunTest(const FString& Parameters)
{
    FFixtures Fixtures;
    if (!FixtureIndex(*this, Fixtures)) { return false; }
    const FCase* Approved = Fixtures.Valid.FindByPredicate([](const FCase& Case) { return Case.Name == TEXT("approved_base"); });
    if (!TestNotNull(TEXT("approved_base fixture is exported"), Approved)) { return false; }
    FString Error;
    FLDDemoContentBundle Bundle;
    auto* Asset = NewObject<ULDDemoContentCatalog>(GetTransientPackage());
    TUniquePtr<FLDDemoPreparedContent> Prepared;
    TSharedPtr<const FLDDemoContentSnapshot> Snapshot;
    if (!FLDDemoContentBundle::ReadImportFiles(FPaths::Combine(Fixtures.Root, Approved->Name), Bundle, Error)
        || !ULDDemoContentCatalog::PrepareImport(*Asset, Bundle, Approved->Digest, Prepared, Error)
        || !Asset->CommitImport(Prepared, Approved->Digest, Error) || !Asset->Read(Approved->Digest, Snapshot, Error))
    {
        AddError(Error);
        return false;
    }
    const FLDDemoContentDefinitions& Definitions = Snapshot->Get();
    FStringTablePtr Table;
    const bool bBuilt = LDDemoContentText::Build(Definitions, Table, Error);
    if (!TestTrue(TEXT("Approved display rows build one complete table: ") + Error, bBuilt))
    {
        return false;
    }
    TestEqual(TEXT("Table namespace is DemoContent"), Table->GetNamespace(), FString(TEXT("DemoContent")));
    int32 Entries = 0;
    const auto Count = [&Entries](const FString&, const FString&) { ++Entries; return true; };
    Table->EnumerateSourceStrings(TFunctionRef<bool(const FString&, const FString&)>(Count));
    TestEqual(TEXT("Every display row contributes exactly a name and a description"), Entries, Definitions.Display.Num() * 2);
    // One row per definition (character, actives, stats, components, monsters) and per native status: statuses 1..6, 9, 10
    // and 12..14 (7, 8 and 11 are retired), the #112 C2 knockback (a breach shows as exposed), the two #147 statuses
    // (silence, burn), attack_slowed (#198 retires cowering and contact_guard) and the #198 wet, electrified,
    // braced and soured.
    const int32 Statuses = 6 + 5 + 1 + 2 + 1 + 4;
    TestEqual(TEXT("Every approved content and status identity is displayed"), Definitions.Display.Num(),
        1 + Definitions.Abilities.Num() + Definitions.Stats.Num() + Definitions.Components.Num() + Definitions.Monsters.Num()
        + Statuses);
    for (const FLDDemoDisplayDefinition& Row : Definitions.Display)
    {
        FString Name, Description;
        TestTrue(TEXT("Name resolves from the display key: ") + Row.Id,
            Table->GetSourceString(FTextKey(Row.NameKey), Name) && Name.Equals(Row.NameText, ESearchCase::CaseSensitive));
        TestTrue(TEXT("Description resolves from the display key: ") + Row.Id,
            Table->GetSourceString(FTextKey(Row.DescriptionKey), Description)
            && Description.Equals(Row.DescriptionText, ESearchCase::CaseSensitive));
    }
    // The statuses system text names directly read the same in the table as in the glossary (#111).
    for (const TPair<const TCHAR*, const TCHAR*>& Expected : {
        TPair<const TCHAR*, const TCHAR*>(TEXT("root.name"), LDTerms::Root),
        TPair<const TCHAR*, const TCHAR*>(TEXT("shield.name"), LDTerms::Shield),
        TPair<const TCHAR*, const TCHAR*>(TEXT("respawn_protection.name"), LDTerms::RespawnProtection)})
    {
        FString Text;
        TestTrue(FString::Printf(TEXT("Approved glossary name for %s"), Expected.Key),
            Table->GetSourceString(FTextKey(Expected.Key), Text) && Text.Equals(Expected.Value, ESearchCase::CaseSensitive));
    }

    FLDDemoContentDefinitions Missing = Definitions;
    Missing.Display[0].DescriptionText.Reset();
    FStringTablePtr Refused;
    TestFalse(TEXT("A row without description text is refused"), LDDemoContentText::Build(Missing, Refused, Error));
    TestTrue(TEXT("Missing text refusal is explicit and builds nothing"), !Error.IsEmpty() && !Refused.IsValid());
    FLDDemoContentDefinitions Duplicate = Definitions;
    Duplicate.Display[1].NameKey = Duplicate.Display[0].NameKey;
    TestFalse(TEXT("A duplicate key is refused instead of overwriting"), LDDemoContentText::Build(Duplicate, Refused, Error));
    FLDDemoContentDefinitions Empty = Definitions;
    Empty.Display.Reset();
    TestFalse(TEXT("A catalog without display rows is refused"), LDDemoContentText::Build(Empty, Refused, Error));

    TSharedPtr<const FLDDemoContentSnapshot> Runtime;
    const bool bLoaded = LDDemoEntry::LoadRuntimeContent(Runtime, Error);
    if (!TestTrue(TEXT("The client and Host content path loads and registers display text: ") + Error, bLoaded))
    {
        return false;
    }
    FStringTableRegistry& Registry = FStringTableRegistry::Get();
    const FStringTablePtr Registered = Registry.FindMutableStringTable(LDDemoContentText::TableId());
    if (!TestTrue(TEXT("DemoContent string table is registered"), Registered.IsValid())) { return false; }
    for (const FLDDemoDisplayDefinition& Row : Runtime->Get().Display)
    {
        FString Name;
        TestTrue(TEXT("Registered table resolves runtime display name: ") + Row.Id,
            Registered->GetSourceString(FTextKey(Row.NameKey), Name) && Name.Equals(Row.NameText, ESearchCase::CaseSensitive));
    }
    const bool bRepeated = LDDemoContentText::Register(*Runtime, Error);
    TestTrue(TEXT("Registering the same verified text again is a no-op: ") + Error, bRepeated);

    FLDDemoContentDefinitions Conflicting = Runtime->Get();
    Conflicting.Display[0].NameText += TEXT("_conflict");
    FStringTablePtr Foreign;
    const bool bBuiltForeign = LDDemoContentText::Build(Conflicting, Foreign, Error);
    // The registry allows one table per ID, so the foreign table replaces the verified one only for this check.
    const bool bUnregistered = Registry.UnregisterStringTable(LDDemoContentText::TableId());
    if (bBuiltForeign)
    {
        Registry.RegisterStringTable(LDDemoContentText::TableId(), Foreign.ToSharedRef());
    }
    const bool bConflictRegistered = LDDemoContentText::Register(*Runtime, Error);
    const FString ConflictError = Error;
    FString EntryError;
    const bool bEntryVerified = LDDemoEntry::VerifyRuntimeFiles(EntryError);
    Registry.UnregisterStringTable(LDDemoContentText::TableId());
    Registry.RegisterStringTable(LDDemoContentText::TableId(), Registered.ToSharedRef());
    TestTrue(TEXT("A conflicting table can be prepared for the refusal check"), bBuiltForeign && bUnregistered);
    TestFalse(TEXT("Different text under the DemoContent ID is refused, not mixed"), bConflictRegistered);
    TestTrue(TEXT("Conflict refusal is explicit: ") + ConflictError, ConflictError.Contains(TEXT("already registered")));
    TestFalse(TEXT("Entry verification revalidates even when runtime content was cached"), bEntryVerified);
    TestTrue(TEXT("Entry verification reports the conflicting table: ") + EntryError, EntryError.Contains(TEXT("already registered")));
    const bool bRestored = LDDemoContentText::Register(*Runtime, Error);
    TestTrue(TEXT("The verified table is restored and accepted again: ") + Error, bRestored);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoContentCaseFoldedLookupTest, "Lockdown.Rules.DemoContent.CaseFoldedLookup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoContentCaseFoldedLookupTest::RunTest(const FString& Parameters)
{
    FFixtures Fixtures;
    if (!FixtureIndex(*this, Fixtures)) { return false; }
    const FCase* Approved = Fixtures.Valid.FindByPredicate([](const FCase& Case) { return Case.Name == TEXT("approved_base"); });
    if (!TestNotNull(TEXT("approved_base fixture is exported"), Approved)) { return false; }
    FString Error;
    FLDDemoContentBundle Bundle;
    auto* Asset = NewObject<ULDDemoContentCatalog>(GetTransientPackage());
    TUniquePtr<FLDDemoPreparedContent> Prepared;
    TSharedPtr<const FLDDemoContentSnapshot> Snapshot;
    if (!FLDDemoContentBundle::ReadImportFiles(FPaths::Combine(Fixtures.Root, Approved->Name), Bundle, Error)
        || !ULDDemoContentCatalog::PrepareImport(*Asset, Bundle, Approved->Digest, Prepared, Error)
        || !Asset->CommitImport(Prepared, Approved->Digest, Error) || !Asset->Read(Approved->Digest, Snapshot, Error))
    {
        AddError(Error);
        return false;
    }
    const FLDDemoContentDefinitions& Definitions = Snapshot->Get();
    // A packaged build hands back the first registered casing of a name; the editor keeps each spelling,
    // so these explicit variants reproduce that input here.
    const auto Variants = [](const FString& Id)
    {
        FString Capitalized = Id;
        Capitalized[0] = FChar::ToUpper(Capitalized[0]);
        return TArray<FString>{Id.ToUpper(), Capitalized, FName(*Capitalized).ToString()};
    };
    int32 Identities = 0;
    const auto Expect = [&](const TCHAR* Kind, const FString& Id, auto Lookup)
    {
        ++Identities;
        const auto* Canonical = Lookup(Id);
        TestNotNull(FString::Printf(TEXT("%s %s resolves canonically: %s"), Kind, *Id, *Error), Canonical);
        for (const FString& Variant : Variants(Id))
        {
            const auto* Folded = Lookup(Variant);
            TestTrue(FString::Printf(TEXT("%s %s resolves from %s: %s"), Kind, *Id, *Variant, *Error),
                Canonical != nullptr && Folded == Canonical);
        }
    };
    for (const FLDDemoAbilityDefinition& Row : Definitions.Abilities)
    {
        Expect(TEXT("Ability"), Row.Id, [&](const FString& Id)
        { const FLDDemoAbilityDefinition* Out = nullptr; return Snapshot->FindAbility(Id, Out, Error) ? Out : nullptr; });
    }
    for (const FLDDemoStatDefinition& Row : Definitions.Stats)
    {
        Expect(TEXT("Stat"), Row.Id, [&](const FString& Id)
        { const FLDDemoStatDefinition* Out = nullptr; return Snapshot->FindStat(Id, Out, Error) ? Out : nullptr; });
    }
    for (const FLDDemoComponentDefinition& Row : Definitions.Components)
    {
        Expect(TEXT("Component"), Row.Id, [&](const FString& Id)
        { const FLDDemoComponentDefinition* Out = nullptr; return Snapshot->FindComponent(Id, Out, Error) ? Out : nullptr; });
    }
    for (const FLDDemoMonsterDefinition& Row : Definitions.Monsters)
    {
        Expect(TEXT("Monster"), Row.Id, [&](const FString& Id)
        { const FLDDemoMonsterDefinition* Out = nullptr; return Snapshot->FindMonster(Id, Out, Error) ? Out : nullptr; });
    }
    for (const FLDDemoDisplayDefinition& Row : Definitions.Display)
    {
        Expect(TEXT("Display"), Row.Id, [&](const FString& Id)
        { const FLDDemoDisplayDefinition* Out = nullptr; return Snapshot->FindDisplay(Id, Out, Error) ? Out : nullptr; });
    }
    TestTrue(TEXT("Every catalog kind contributed identities to the folded lookup check"),
        !Definitions.Abilities.IsEmpty() && !Definitions.Stats.IsEmpty() && !Definitions.Components.IsEmpty() && !Definitions.Monsters.IsEmpty()
        && Identities == Definitions.Abilities.Num() + Definitions.Stats.Num() + Definitions.Components.Num() + Definitions.Monsters.Num()
            + Definitions.Display.Num());
    for (const TCHAR* Common : {TEXT("Root"), TEXT("Shield")})
    {
        const FLDDemoDisplayDefinition* Out = nullptr;
        TestTrue(FString::Printf(TEXT("Display %s resolves from an engine-style registered spelling"), Common),
            Snapshot->FindDisplay(FName(Common).ToString(), Out, Error) && Out
            && Out->Id.Equals(FString(Common).ToLower(), ESearchCase::CaseSensitive));
    }
    for (const TCHAR* Bad : {TEXT(""), TEXT("Root!"), TEXT("1root"), TEXT("root shield"), TEXT("\uFF32oot")})
    {
        const FLDDemoDisplayDefinition* Out = nullptr;
        TestFalse(FString::Printf(TEXT("Malformed lookup '%s' is refused, not folded into a match"), Bad),
            Snapshot->FindDisplay(Bad, Out, Error));
        TestTrue(FString::Printf(TEXT("Malformed lookup '%s' is explicit"), Bad), Out == nullptr && !Error.IsEmpty());
    }
    const FLDDemoStatDefinition* Missing = nullptr;
    TestFalse(TEXT("A well-formed but absent identity stays absent in any casing"),
        Snapshot->FindStat(TEXT("Stat_Absent"), Missing, Error));
    TestTrue(TEXT("The absent identity reports the not-present reason"), Missing == nullptr && Error.Contains(TEXT("not present")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoContentGrowthCatalogTest, "Lockdown.Rules.DemoContent.GrowthCatalog",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// #198: the approved catalog carries five point-bought stats, single-level actives with an opening pool, the active offer
// levels and the growth parameters; an active's value set grows with its owner's level and stat cards read one point.
bool FLDDemoContentGrowthCatalogTest::RunTest(const FString& Parameters)
{
    FFixtures Fixtures;
    if (!FixtureIndex(*this, Fixtures)) { return false; }
    const FCase* Approved = Fixtures.Valid.FindByPredicate([](const FCase& Case) { return Case.Name == TEXT("approved_base"); });
    if (!TestNotNull(TEXT("approved_base fixture is exported"), Approved)) { return false; }
    FString Error;
    FLDDemoContentBundle Bundle;
    auto* Asset = NewObject<ULDDemoContentCatalog>(GetTransientPackage());
    TUniquePtr<FLDDemoPreparedContent> Prepared;
    TSharedPtr<const FLDDemoContentSnapshot> Snapshot;
    if (!FLDDemoContentBundle::ReadImportFiles(FPaths::Combine(Fixtures.Root, Approved->Name), Bundle, Error)
        || !ULDDemoContentCatalog::PrepareImport(*Asset, Bundle, Approved->Digest, Prepared, Error)
        || !Asset->CommitImport(Prepared, Approved->Digest, Error) || !Asset->Read(Approved->Digest, Snapshot, Error))
    {
        AddError(Error);
        return false;
    }
    const FLDDemoContentDefinitions& Definitions = Snapshot->Get();
    using S = ELDDemoBonusStat;
    struct FExpectedStat { const TCHAR* Id; ELDDemoStatId NativeId; TArray<S> PerPoint; };
    const FExpectedStat ExpectedStats[] = {
        {TEXT("stat_physique"), ELDDemoStatId::StatPhysique, {S::MaxHPFlat, S::HPRegenFlat}},
        {TEXT("stat_thick_skin"), ELDDemoStatId::StatThickSkin, {S::ArmorFlat, S::MagicResistFlat}},
        {TEXT("stat_strength"), ELDDemoStatId::StatStrength, {S::ADFlat, S::CritChanceBonusRatio}},
        {TEXT("stat_brains"), ELDDemoStatId::StatBrains, {S::APFlat, S::CooldownHasteFlat, S::MPRegenFlat}},
        {TEXT("stat_agility"), ELDDemoStatId::StatAgility, {S::AttackSpeedBonusRatio, S::MoveSpeedBonusRatio}}};
    TestEqual(TEXT("Exactly five point-bought stats"), Definitions.Stats.Num(), 5);
    // Per-stat arrays (points, AllocatePoints counts, owner rows, UI rows, AI ties) follow the approved design order 体格,
    // 脸皮, 力道, 脑力, 身手 (ascending native id), never the id-sorted catalog list.
    const TArray<const FLDDemoStatDefinition*> DesignOrder = Definitions.StatsInDesignOrder();
    bool bDesignOrder = DesignOrder.Num() == UE_ARRAY_COUNT(ExpectedStats);
    for (int32 Index = 0; bDesignOrder && Index < DesignOrder.Num(); ++Index)
    {
        bDesignOrder = DesignOrder[Index]->Id == ExpectedStats[Index].Id && DesignOrder[Index]->GetNativeId() == ExpectedStats[Index].NativeId
            && Definitions.StatDesignIndex(ExpectedStats[Index].Id) == Index;
    }
    TestTrue(TEXT("The stats' design order is physique, thick skin, strength, brains, agility"), bDesignOrder);
    for (const FExpectedStat& Expected : ExpectedStats)
    {
        const FLDDemoStatDefinition* Stat = nullptr;
        if (!TestTrue(FString::Printf(TEXT("%s resolves: %s"), Expected.Id, *Error), Snapshot->FindStat(Expected.Id, Stat, Error)))
        {
            continue;
        }
        TestTrue(FString::Printf(TEXT("%s keeps its native number"), Expected.Id), Stat->GetNativeId() == Expected.NativeId);
        TSet<S> Actual;
        for (const FLDDemoStatBonus& Bonus : Stat->PerPoint)
        {
            Actual.Add(Bonus.Stat);
        }
        TestTrue(FString::Printf(TEXT("%s adds exactly its per-point bonuses"), Expected.Id),
            Actual.Num() == Expected.PerPoint.Num() && Algo::AllOf(Expected.PerPoint, [&Actual](S Bonus) { return Actual.Contains(Bonus); }));
    }
    int32 OpeningPool = 0;
    for (const FLDDemoAbilityDefinition& Ability : Definitions.Abilities)
    {
        OpeningPool += Ability.Opening ? 1 : 0;
    }
    TestEqual(TEXT("Twenty-two single-level actives"), Definitions.Abilities.Num(), 22);
    TestEqual(TEXT("Eighteen actives form the opening pool (AI spec §4.3)"), OpeningPool, 18);
    TestTrue(TEXT("Levels 3 and 6 each enqueue one more active opportunity"), Definitions.Progression.ActiveOfferLevels == TArray<int32>{3, 6});
    const FLDDemoContentParameters& P = Definitions.Parameters;
    TestTrue(TEXT("Growth points and the build safe window are the approved values"),
        P.OpeningPoints == 3 && P.PointsPerLevel == 3 && P.MaxLevelPointsXP == 400 && P.MaxLevelPoints == 3 && P.BuildSafeWindowMs == 4000);
    TestTrue(TEXT("Skill growth steps are 0.15 through level 10 and 0.075 after"),
        P.SkillGrowthPerLevelBp == 1500 && P.SkillGrowthPerLevelAfter10Bp == 750);

    const FLDDemoAbilityDefinition* Elbow = nullptr;
    if (!TestTrue(TEXT("active_elbow resolves: ") + Error, Snapshot->FindAbility(TEXT("active_elbow"), Elbow, Error))) { return false; }
    const FLDDemoAbilityValues* First = Snapshot->FindValues(*Elbow, 1);
    const FLDDemoAbilityValues* Top = Snapshot->FindValues(*Elbow, 16);
    if (!TestTrue(TEXT("The value set resolves for levels 1 and 16"), First && Top)) { return false; }
    TestTrue(TEXT("Level 1 uses the authored G=1 values"),
        First->Effects[0].BaseP2 == 7500 && First->Effects[0].ADRatioBp == 6500 && First->ManaCostP2 == 5000 && First->CooldownMs == 10000);
    TestTrue(TEXT("Level 16 grows the base by G(16)=2.8 and keeps ratios, cost and cooldown"),
        Top->Effects[0].BaseP2 == 21000 && Top->Effects[0].ADRatioBp == 6500 && Top->ManaCostP2 == 5000 && Top->CooldownMs == 10000);
    const FLDDemoAbilityDefinition Detached = *Elbow;
    TestTrue(TEXT("Levels outside 1..16 and abilities outside the snapshot have no value set"),
        !Snapshot->FindValues(*Elbow, 0) && !Snapshot->FindValues(*Elbow, 17) && !Snapshot->FindValues(Detached, 1));

    TArray<LDDemoCardText::FToken> Tokens;
    const FLDDemoStatDefinition* Brains = nullptr;
    const FLDDemoStatDefinition* Strength = nullptr;
    if (!TestTrue(TEXT("A stat card placeholder parses: ") + Error, LDDemoCardText::Parse(TEXT("每点 +[per_point.APFlat]"), Tokens, Error))
        || !TestTrue(TEXT("Stats resolve"), Snapshot->FindStat(TEXT("stat_brains"), Brains, Error) && Snapshot->FindStat(TEXT("stat_strength"), Strength, Error))
        || !TestEqual(TEXT("A literal and one value token"), Tokens.Num(), 2))
    {
        return false;
    }
    const LDDemoCardText::FToken& PerPoint = Tokens[1];
    TestTrue(TEXT("The token reads one stat of a single point"),
        PerPoint.bValue && PerPoint.bPerPoint && PerPoint.Field == LDDemoCardText::EField::Stat && PerPoint.Stat == S::APFlat);
    const FLDDemoStatBonus* Bonus = LDDemoCardText::FindPerPoint(*Brains, PerPoint);
    TestTrue(TEXT("stat_brains answers its own per-point AP"), Bonus && Bonus->Stat == S::APFlat && Bonus->Amount == 500);
    TestNull(TEXT("Another stat does not answer it"), LDDemoCardText::FindPerPoint(*Strength, PerPoint));
    TestNull(TEXT("An active's value set does not answer a per-point token"), LDDemoCardText::FindEffect(*First, PerPoint));
    for (const TCHAR* Bad : {TEXT("[per_point.amount]"), TEXT("[dash_boost.ratio]"), TEXT("[damage.cooldown_cut]"), TEXT("[cower.ratio]")})
    {
        TestFalse(FString::Printf(TEXT("Retired or malformed placeholder %s is refused"), Bad), LDDemoCardText::Parse(Bad, Tokens, Error));
    }
    const FLDDemoAbilityDefinition* Retired = nullptr;
    const FLDDemoStatDefinition* Card = nullptr;
    TestFalse(TEXT("The retired cower is absent"), Snapshot->FindAbility(TEXT("active_cower"), Retired, Error));
    TestFalse(TEXT("Retired passives are absent"), Snapshot->FindAbility(TEXT("passive_lucky_star"), Retired, Error));
    TestFalse(TEXT("Retired attribute cards are absent"), Snapshot->FindStat(TEXT("attr_protein"), Card, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoContentComponentCatalogTest, "Lockdown.Rules.DemoContent.ComponentCatalog",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// #198 B: the approved catalog carries B01-B18 in native ID order with their kind, pool, opening condition, the B18
// active/element binding, the component-form effect fields and the level-up category bands; FindComponent resolves them.
bool FLDDemoContentComponentCatalogTest::RunTest(const FString& Parameters)
{
    FFixtures Fixtures;
    if (!FixtureIndex(*this, Fixtures)) { return false; }
    const FCase* Approved = Fixtures.Valid.FindByPredicate([](const FCase& Case) { return Case.Name == TEXT("approved_base"); });
    if (!TestNotNull(TEXT("approved_base fixture is exported"), Approved)) { return false; }
    FString Error;
    FLDDemoContentBundle Bundle;
    auto* Asset = NewObject<ULDDemoContentCatalog>(GetTransientPackage());
    TUniquePtr<FLDDemoPreparedContent> Prepared;
    TSharedPtr<const FLDDemoContentSnapshot> Snapshot;
    if (!FLDDemoContentBundle::ReadImportFiles(FPaths::Combine(Fixtures.Root, Approved->Name), Bundle, Error)
        || !ULDDemoContentCatalog::PrepareImport(*Asset, Bundle, Approved->Digest, Prepared, Error)
        || !Asset->CommitImport(Prepared, Approved->Digest, Error) || !Asset->Read(Approved->Digest, Snapshot, Error))
    {
        AddError(Error);
        return false;
    }
    const FLDDemoContentDefinitions& Definitions = Snapshot->Get();
    // #198 B bases B01-B18 (1-18), #198 F mods M01-M12 (19-30) and connectors C01-C12 (31-42); M11, M12 and C12 are scarce.
    if (!TestEqual(TEXT("Eighteen bases, twelve mods and twelve connectors"), Definitions.Components.Num(), 42)) { return false; }
    TSet<FString> AlwaysOpening;
    for (int32 Index = 0; Index < Definitions.Components.Num(); ++Index)
    {
        const FLDDemoComponentDefinition& Row = Definitions.Components[Index];
        const ELDDemoComponentKind Kind = Index < 18 ? ELDDemoComponentKind::Base
            : Index < 30 ? ELDDemoComponentKind::Mod : ELDDemoComponentKind::Connector;
        const bool bScarce = Row.Id == TEXT("component_pick_you") || Row.Id == TEXT("component_owe_it") || Row.Id == TEXT("component_dirty_fist");
        TestTrue(FString::Printf(TEXT("%s is numbered %d with its kind and pool"), *Row.Id, Index + 1),
            int32(Row.GetNativeId()) == Index + 1 && Row.Kind == Kind
            && Row.Pool == (bScarce ? ELDDemoComponentPool::Scarce : ELDDemoComponentPool::Common));
        TestTrue(FString::Printf(TEXT("%s names install targets exactly when it is a mod or connector"), *Row.Id),
            (Kind == ELDDemoComponentKind::Base) == (Row.TargetActives.IsEmpty() && Row.TargetComponents.IsEmpty() && !Row.bTargetDash
                && !Row.bTargetSelf));
        const FLDDemoComponentDefinition* Found = nullptr;
        TestTrue(FString::Printf(TEXT("FindComponent resolves %s to its own row: %s"), *Row.Id, *Error),
            Snapshot->FindComponent(Row.Id, Found, Error) && Found == &Row);
        const FLDDemoDisplayDefinition* Display = nullptr;
        TestTrue(FString::Printf(TEXT("%s has a display row with its own icon"), *Row.Id),
            Snapshot->FindDisplay(Row.Id, Display, Error) && Display->IconId == TEXT("icon_") + Row.Id);
        TestTrue(FString::Printf(TEXT("%s binds only through active_element"), *Row.Id),
            (Row.Binding == ELDDemoComponentBinding::ActiveElement) == (Row.Id == TEXT("component_dirty_trick")));
        if (Row.bOpeningAlways)
        {
            TestTrue(FString::Printf(TEXT("%s is always offered without an active condition"), *Row.Id), Row.OpeningActives.IsEmpty());
            AlwaysOpening.Add(Row.Id);
        }
    }
    TestTrue(TEXT("B01-B06, B09, B10 and B12 are the always-offered opening bases"), AlwaysOpening.Num() == 9
        && Algo::AllOf(TArray<FString>{TEXT("component_fist_fire"), TEXT("component_tab_ledger"), TEXT("component_spiky_hide"),
            TEXT("component_punch_heal"), TEXT("component_dash_bump"), TEXT("component_third_punch"), TEXT("component_hard_skin"),
            TEXT("component_fist_water"), TEXT("component_dash_punch")}, [&AlwaysOpening](const FString& Id) { return AlwaysOpening.Contains(Id); }));

    const auto Component = [&Snapshot, &Error](const TCHAR* Id)
    {
        const FLDDemoComponentDefinition* Out = nullptr;
        return Snapshot->FindComponent(Id, Out, Error) ? Out : nullptr;
    };
    const FLDDemoComponentDefinition* Fire = Component(TEXT("component_fist_fire"));
    const FLDDemoComponentDefinition* Ledger = Component(TEXT("component_tab_ledger"));
    const FLDDemoComponentDefinition* Spiky = Component(TEXT("component_spiky_hide"));
    const FLDDemoComponentDefinition* Third = Component(TEXT("component_third_punch"));
    const FLDDemoComponentDefinition* Kick = Component(TEXT("component_kick_down"));
    const FLDDemoComponentDefinition* Breach = Component(TEXT("component_breach_fist"));
    const FLDDemoComponentDefinition* Water = Component(TEXT("component_fist_water"));
    const FLDDemoComponentDefinition* Shock = Component(TEXT("component_fist_shock"));
    const FLDDemoComponentDefinition* DashPunch = Component(TEXT("component_dash_punch"));
    const FLDDemoComponentDefinition* LongArm = Component(TEXT("component_long_arm"));
    const FLDDemoComponentDefinition* HeldShot = Component(TEXT("component_held_shot"));
    const FLDDemoComponentDefinition* Trick = Component(TEXT("component_dirty_trick"));
    if (!TestTrue(TEXT("Named components resolve: ") + Error, Fire && Ledger && Spiky && Third && Kick && Breach && Water && Shock
        && DashPunch && LongArm && HeldShot && Trick))
    {
        return false;
    }
    using EOp = ELDDemoEffectOp;
    const auto Only = [this](const FLDDemoComponentDefinition& Row, EOp Op) -> const FLDDemoEffectDefinition*
    {
        const FLDDemoEffectDefinition* Effect = Row.Effects.FindByPredicate([Op](const FLDDemoEffectDefinition& Candidate) { return Candidate.GetOp() == Op; });
        TestNotNull(*FString::Printf(TEXT("%s carries operation %d"), *Row.Id, int32(Op)), Effect);
        return Effect;
    };
    if (const auto* Burn = Only(*Fire, EOp::Burn))
    {
        TestTrue(TEXT("B01 applies one burn stack per primary basic and none per active hit"), Burn->BasicCount == 1 && Burn->Count == 0);
    }
    if (const auto* Entry = Only(*Ledger, EOp::Ledger))
    {
        TestTrue(TEXT("B02 records 10 + 0.08 AP magic entries, cashes 5 and clears after 6 s"),
            Entry->BaseP2 == 1000 && Entry->ADRatioBp == 0 && Entry->APRatioBp == 800 && Entry->DamageType == ELDDemoDamageType::Magical
            && Entry->Count == 5 && Entry->DurationMs == 6000);
    }
    if (const auto* Thorns = Only(*Spiky, EOp::Thorns))
    {
        TestTrue(TEXT("B03 reflects 10 + 1% max HP inside 600 cm with no repeat window"),
            Thorns->BaseP2 == 1000 && Thorns->MaxHPRatioBp == 100 && Thorns->IntervalMs == 0 && Thorns->CriticalMultiplierBp == 10000
            && Thorns->RadiusCm.Numerator == 600 && Thorns->RadiusCm.Denominator == 1);
    }
    if (const auto* Push = Only(*Third, EOp::NthBasicBonus))
    {
        TestTrue(TEXT("B06 pushes every third primary basic target 120 cm in 0.12 s without damage"),
            Push->Count == 3 && Push->DistanceCm.Numerator == 120 && Push->DistanceCm.Denominator == 1 && Push->DurationMs == 120
            && Push->BaseP2 == 0);
    }
    if (const auto* Empower = Only(*DashPunch, EOp::EmpowerBasic))
    {
        TestTrue(TEXT("B12 arms one 25 + 0.25 AD package for 3 s when a dash completes"),
            Empower->Trigger == ELDDemoEffectTrigger::DashComplete && Empower->Count == 1 && Empower->DurationMs == 3000
            && Empower->BaseP2 == 2500 && Empower->ADRatioBp == 2500);
    }
    const FLDDemoEffectDefinition* WaterFist = Only(*Water, EOp::BasicElement);
    const FLDDemoEffectDefinition* ShockFist = Only(*Shock, EOp::BasicElement);
    TestTrue(TEXT("B10 and B11 name water and electric"), WaterFist && ShockFist
        && WaterFist->Element == ELDDemoElement::Water && ShockFist->Element == ELDDemoElement::Electric);
    const FLDDemoEffectDefinition* Scale = Only(*LongArm, EOp::BasicDamageScale);
    TestTrue(TEXT("B14 adds range and keeps 90% of the primary basic package"),
        Scale && Scale->RatioBp == 9000 && Only(*LongArm, EOp::StatBonus) != nullptr && LongArm->Effects.Num() == 2);
    const FLDDemoEffectDefinition* Idle = Only(*HeldShot, EOp::IdleReadyAmp);
    TestTrue(TEXT("B17 waits 2 s for +20%"), Idle && Idle->DelayMs == 2000 && Idle->RatioBp == 2000);
    for (const FLDDemoComponentDefinition& Row : Definitions.Components)
    {
        for (const FLDDemoEffectDefinition& Effect : Row.Effects)
        {
            TestTrue(FString::Printf(TEXT("%s names an element only on basic_element"), *Row.Id),
                (Effect.Element != ELDDemoElement::None) == (Effect.GetOp() == EOp::BasicElement));
        }
    }

    TestTrue(TEXT("B07 opens with an active that slows, hard-controls or knocks back (S01-S06, S08, S09, S12, S20, S22)"),
        !Kick->bOpeningAlways && Kick->OpeningActives == TArray<FString>{TEXT("active_elbow"), TEXT("active_pants_down"),
            TEXT("active_crotch_grab"), TEXT("active_account_banned"), TEXT("active_unplug"), TEXT("active_snark_bolt"),
            TEXT("active_quick_meeting"), TEXT("active_apology_slide"), TEXT("active_open_mic"), TEXT("active_hose_down"),
            TEXT("active_dirty_puddle")});
    TestTrue(TEXT("B11 opens with a fire or water active (S07, S08, S09, S20)"),
        Shock->OpeningActives == TArray<FString>{TEXT("active_seethe"), TEXT("active_quick_meeting"), TEXT("active_hose_down"),
            TEXT("active_dirty_puddle")});
    TestTrue(TEXT("B08 is never an opening option"), !Breach->bOpeningAlways && Breach->OpeningActives.IsEmpty());
    TestTrue(TEXT("B18 binds one of eighteen damaging actives and fire, water or electric"),
        Trick->Binding == ELDDemoComponentBinding::ActiveElement && Trick->BindActives.Num() == 18
        && !Trick->BindActives.Contains(TEXT("active_bail_out")) && Trick->BindActives.Contains(TEXT("active_flyer_trap"))
        && Trick->BindActives.Contains(TEXT("active_brace"))
        && Trick->BindElements == TArray<ELDDemoElement>{ELDDemoElement::Fire, ELDDemoElement::Water, ELDDemoElement::Electric}
        && Trick->Effects.Num() == 1 && Trick->Effects[0].GetOp() == EOp::ActiveElement);

    const TArray<FLDDemoComponentCategoryWeights>& Bands = Definitions.Progression.ComponentCategoryWeights;
    const auto Band = [&Bands](int32 Index, int32 Min, int32 Max, int32 Base, int32 Mod, int32 Connector)
    {
        return Bands.IsValidIndex(Index) && Bands[Index].MinLevel == Min && Bands[Index].MaxLevel == Max && Bands[Index].BasePercent == Base
            && Bands[Index].ModPercent == Mod && Bands[Index].ConnectorPercent == Connector;
    };
    TestTrue(TEXT("Level-up category bands are 2-6 55/25/20, 7-11 40/30/30 and 12-16 25/35/40"),
        Bands.Num() == 3 && Band(0, 2, 6, 55, 25, 20) && Band(1, 7, 11, 40, 30, 30) && Band(2, 12, 16, 25, 35, 40));
    TestEqual(TEXT("An empty common pool grants 3 points"), Definitions.Parameters.EmptyComponentPoolPoints, int64(3));

    TArray<LDDemoCardText::FToken> Tokens;
    if (TestTrue(TEXT("A component card placeholder parses: ") + Error, LDDemoCardText::Parse(TEXT("满 [ledger.count] 笔"), Tokens, Error))
        && TestEqual(TEXT("A literal, one value token and a literal"), Tokens.Num(), 3))
    {
        const FLDDemoEffectDefinition* Read = LDDemoCardText::FindEffect(Ledger->Effects, Tokens[1]);
        TestTrue(TEXT("B02's effects answer [ledger.count]"), Read && Read->GetOp() == EOp::Ledger);
        TestNull(TEXT("Another component does not answer it"), LDDemoCardText::FindEffect(Fire->Effects, Tokens[1]));
    }
    const FLDDemoComponentDefinition* Missing = nullptr;
    TestFalse(TEXT("An absent component stays absent"), Snapshot->FindComponent(TEXT("component_missing"), Missing, Error));
    TestTrue(TEXT("The absent component reports the not-present reason"), Missing == nullptr && Error.Contains(TEXT("not present")));
    TestFalse(TEXT("An active is not a component"), Snapshot->FindComponent(TEXT("active_elbow"), Missing, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDDemoContentDevReloadTest, "Lockdown.Rules.DemoContent.DevReload",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLDDemoContentDevReloadTest::RunTest(const FString& Parameters)
{
    FFixtures Fixtures;
    if (!FixtureIndex(*this, Fixtures)) { return false; }
    const auto Find = [&Fixtures](const TCHAR* Name) { return Fixtures.Valid.FindByPredicate([Name](const FCase& Case) { return Case.Name == Name; }); };
    const FCase* Base = Find(TEXT("base"));
    const FCase* Exact = Find(TEXT("exact_integer"));
    const FCase* World = Find(TEXT("world_spawn_shift"));
    if (!TestNotNull(TEXT("Base fixture"), Base) || !TestNotNull(TEXT("Content-only fixture"), Exact)
        || !TestNotNull(TEXT("World fixture"), World)) { return false; }
    // The asset plays the compiled build: imported from the base fixture.
    FString Error;
    auto* Asset = NewObject<ULDDemoContentCatalog>(GetTransientPackage());
    TUniquePtr<FLDDemoPreparedContent> Prepared;
    const FString BaseRoot = FPaths::Combine(Fixtures.Root, Base->Name);
    if (!ULDDemoContentCatalog::PrepareImportFromFiles(*Asset, BaseRoot, Base->Digest, Prepared, Error)
        || !Asset->CommitImport(Prepared, Base->Digest, Error))
    {
        AddError(Error);
        return false;
    }
    TSharedPtr<const FLDDemoContentSnapshot> Same, Reloaded, Refused;
    TestTrue(TEXT("Unchanged Sync outputs reload to the compiled identity"),
        Asset->ReadDevReloadFiles(BaseRoot, Base->Digest, Base->Digest, Same, Error) && Same
        && Same->Get().Identity.Root == Base->Digest);
    TestTrue(TEXT("A re-synced content payload reloads without a rebuild"),
        Asset->ReadDevReloadFiles(FPaths::Combine(Fixtures.Root, Exact->Name), Base->Digest, Exact->Digest, Reloaded, Error));
    if (!TestNotNull(TEXT("Reloaded snapshot"), Reloaded.Get()) || !Same) { return false; }
    TestTrue(TEXT("The reloaded snapshot carries the new number and root; the earlier snapshot keeps its own"),
        Reloaded->Get().Parameters.AirdropRewardXP == int64(9007199254740993LL) && Reloaded->Get().Identity.Root == Exact->Digest
        && Same->Get().Parameters.AirdropRewardXP != Reloaded->Get().Parameters.AirdropRewardXP);
    TestFalse(TEXT("A changed world needs Bootstrap and a rebuild, not a reload"),
        Asset->ReadDevReloadFiles(FPaths::Combine(Fixtures.Root, World->Name), Base->Digest, World->Digest, Refused, Error));
    TestTrue(TEXT("The world refusal names what changed"), !Refused && Error.Contains(TEXT("world")));
    TestFalse(TEXT("Files that do not hash to the read manifest root are refused"),
        Asset->ReadDevReloadFiles(FPaths::Combine(Fixtures.Root, Exact->Name), Base->Digest, Base->Digest, Refused, Error));
    TestFalse(TEXT("An asset that does not match the compiled root cannot anchor a reload"),
        Asset->ReadDevReloadFiles(BaseRoot, Exact->Digest, Base->Digest, Refused, Error));
    TestFalse(TEXT("Absent Sync outputs are refused, never replaced by the stored bytes"),
        Asset->ReadDevReloadFiles(FPaths::Combine(Fixtures.Root, TEXT("absent")), Base->Digest, Base->Digest, Refused, Error));
    TestTrue(TEXT("No refused reload produced a snapshot"), !Refused);

    // Only the content payload, its display text and the root may differ from the compiled identity.
    const FLDDemoContentIdentity Compiled = Same->Get().Identity;
    FLDDemoContentIdentity Changed = Compiled;
    Changed.Root = Exact->Digest;
    Changed.Content.Bytes[0] ^= 1;
    Changed.Display.Bytes[0] ^= 1;
    Changed.ApprovalStatus += TEXT("x");
    TestTrue(TEXT("Content, display, root and approval may change"), ULDDemoContentCatalog::SameDevReloadBase(Compiled, Changed, Error));
    const TFunction<void(FLDDemoContentIdentity&)> Guarded[] = {
        [](FLDDemoContentIdentity& I) { I.World.Bytes[0] ^= 1; },
        [](FLDDemoContentIdentity& I) { I.AuthorSchema.Bytes[0] ^= 1; },
        [](FLDDemoContentIdentity& I) { I.NativeIds.Bytes[0] ^= 1; },
        [](FLDDemoContentIdentity& I) { I.NativeContract.Bytes[0] ^= 1; },
        [](FLDDemoContentIdentity& I) { I.WorldContract.Bytes[0] ^= 1; },
        [](FLDDemoContentIdentity& I) { ++I.SchemaVersion; },
        [](FLDDemoContentIdentity& I) { ++I.HandlerRevision; },
        [](FLDDemoContentIdentity& I) { ++I.ProjectionRevision; },
        [](FLDDemoContentIdentity& I) { ++I.WorldSchemaVersion; },
        [](FLDDemoContentIdentity& I) { ++I.WorldProjectionRevision; }};
    int32 Refusals = 0;
    for (const auto& Mutate : Guarded)
    {
        FLDDemoContentIdentity Candidate = Compiled;
        Mutate(Candidate);
        Refusals += !ULDDemoContentCatalog::SameDevReloadBase(Compiled, Candidate, Error) && Error.Contains(TEXT("rebuild")) ? 1 : 0;
    }
    TestEqual(TEXT("Every world, schema, native id, contract and version change is refused with its reason"), Refusals, 10);
    TestFalse(TEXT("Without -LDDevContentReload the normal load stays bound to the compiled build"),
        LDDemoEntry::IsDevContentReload());
    return true;
}
#endif
