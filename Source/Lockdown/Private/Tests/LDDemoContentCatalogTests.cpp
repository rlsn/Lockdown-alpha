#include "Development/LDDemoContentCatalog.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "../Development/LDDemoContentJson.h"
#include "../Development/LDDemoContentMath.h"
#include "Region/LDMapDefinition.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
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
        if (!String(Pair.Value, ExpectedText, Check, Path, 0))
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

bool RankQuantityPaths(const FLDDemoContentBundle& Bundle, TSet<FString>& Out, FString& Error)
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
        QuantityPaths(Abilities[I]->AsObject()->GetField<EJson::Array>(TEXT("ranks")), FString::Printf(TEXT("content.abilities[%d].ranks"), I), Out);
    }
    const auto& Equipment = Root->AsObject()->GetArrayField(TEXT("equipment"));
    for (int32 I = 0; I < Equipment.Num(); ++I)
    {
        const FValue Active = Equipment[I]->AsObject()->TryGetField(TEXT("active"));
        if (Active->Type == EJson::Object)
        {
            QuantityPaths(Active->AsObject()->GetField<EJson::Object>(TEXT("rank")), FString::Printf(TEXT("content.equipment[%d].active.rank"), I), Out);
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
    TSet<FString> UnchangedRankQuantities;
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
        if (Case.Name == TEXT("base"))
        {
            Baseline = Actual;
            if (!RankQuantityPaths(Bundle, UnchangedRankQuantities, Error))
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
            TestTrue(TEXT("Each rank quantity mutation is unique and has a native change assertion"),
                UnchangedRankQuantities.Remove(Case.MutatedQuantity) == 1 && !Case.ChangedPaths.IsEmpty());
        }
        const FLDDemoAbilityDefinition* Ability = nullptr;
        const FLDDemoAttributeDefinition* Attribute = nullptr;
        const FLDDemoEquipmentDefinition* Item = nullptr;
        const FLDDemoMonsterDefinition* Monster = nullptr;
        const FLDDemoDisplayDefinition* Display = nullptr;
        TestTrue(TEXT("Typed lookups consume enabled definitions"),
            Snapshot->FindAbility(TEXT("active_arc_bolt"), Ability, Error)
            && Snapshot->FindAttribute(TEXT("attr_focus"), Attribute, Error)
            && Snapshot->FindEquipment(TEXT("item_arcane_battery"), Item, Error)
            && Snapshot->FindMonster(TEXT("monster_guardian"), Monster, Error)
            && Snapshot->FindDisplay(TEXT("camp_returning"), Display, Error));
        const auto* OldAbility = Ability;
        TestFalse(TEXT("Unknown definition is explicit failure"), Snapshot->FindAbility(TEXT("active_unknown"), Ability, Error));
        TestTrue(TEXT("Lookup failure is not a default definition"), !Error.IsEmpty() && Ability == OldAbility);
        TestFalse(TEXT("Case aliases are rejected before native name construction"), Snapshot->FindAbility(TEXT("Active_arc_bolt"), Ability, Error));
    }
    TestEqual(TEXT("Every rank's typed quantity has a compiler-backed native change case"), UnchangedRankQuantities.Num(), 0);

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
    Stored->Abilities[0].Ranks[0].Effects.AddDefaulted();
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

    const FCase* NewCase = Fixtures.Valid.FindByPredicate([](const FCase& Case) { return Case.Name == TEXT("exact_gold"); });
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
    TestEqual(TEXT("New snapshot preserves above-2^53 integer gold"), Current->Get().Parameters.StartingGold, int64(9007199254740993LL));
    TestEqual(TEXT("Previous snapshot keeps its original definitions"), First->Get().Parameters.StartingGold, Original.Parameters.StartingGold);
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
    if (!ULDDemoContentCatalog::PrepareImportFromFiles(*RaceAsset, BaseRoot, Base.Digest, Prepared, Error))
    {
        AddError(Error);
        return false;
    }
    TestFalse(TEXT("Prepared import cannot commit to a different object"), Asset->CommitImport(Prepared, Base.Digest, Error));
    TestFalse(TEXT("Fresh import still checks the current independently expected root"), RaceAsset->CommitImport(Prepared, NewCase->Digest, Error));
    TestTrue(TEXT("Current-root change is reported"), Error.Contains(TEXT("current expected root changed")));

    const FString SourceFile = FPaths::Combine(BaseRoot, TEXT("Content\\Lockdown\\Data\\Demo\\demo_content.json"));
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
    TestTrue(TEXT("Only isolated fixture source is changed and restored"), WroteChange && RestoredSource);
    TestFalse(TEXT("Changed source between prepare and commit rejects the token"), CommittedChangedSource);
    TestTrue(TEXT("Source-snapshot conflict is reported"), ChangedError.Contains(TEXT("source bytes changed")));
    if (!RestoredSource) { return false; }
    TestTrue(TEXT("B still survives a changed-file import"), RaceAsset->Validate(NewCase->Digest, Error));
    if (!ULDDemoContentCatalog::PrepareImportFromFiles(*RaceAsset, BaseRoot, Base.Digest, Prepared, Error)
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
    for (const auto& Case : Fixtures.Valid)
    {
        const FString Root = FPaths::Combine(Fixtures.Root, Case.Name);
        FLDDemoContentBundle Bundle;
        auto* Content = NewObject<ULDDemoContentCatalog>(GetTransientPackage());
        TUniquePtr<FLDDemoPreparedContent> Prepared;
        TSharedPtr<const FLDDemoContentSnapshot> Catalog;
        auto* World = NewObject<ULDMapDefinition>(GetTransientPackage());
        TSharedPtr<const FLDDemoWorldGeometry> Geometry;
        if (!FLDDemoContentBundle::ReadImportFiles(Root, Bundle, Error)
            || !ULDDemoContentCatalog::PrepareImport(*Content, Bundle, Case.Digest, Prepared, Error)
            || !Content->CommitImport(Prepared, Case.Digest, Error) || !Content->Read(Case.Digest, Catalog, Error)
            || !World->Import(Bundle.World, *Catalog, Error) || !World->Read(*Catalog, Geometry, Error))
        {
            AddError(Case.Name + TEXT(": ") + Error);
            return false;
        }
        TMap<FString, FString> Actual;
        if (!AssertFields(*this, FLDDemoWorldGeometry::StaticStruct(), Geometry.Get(),
            FPaths::Combine(Root, TEXT("native_world_expected.json")), Actual)) { return false; }
        TestEqual(TEXT("World keeps ten actual polygon blockers"), Geometry->Blockers.Num(), 10);
        TestEqual(TEXT("World keeps seventeen authored anchors"), Geometry->Anchors.Num(), 17);
        TestEqual(TEXT("World keeps seven local placements, not seventy spawn IDs"), Geometry->SpawnOffsetsCm.Num(), 7);
    }
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
    Restored->Seed = 9001;
    TestFalse(TEXT("Retired full-map fields cannot accompany a valid Demo projection"), Validate(Error));
    Restored->Seed = 0;
    TestTrue(TEXT("Restoring the isolated mutations recovers the original asset"), Validate(Error));
    TestEqual(TEXT("Earlier immutable world snapshot remains unchanged"), First->Anchors.Num(), 17);
    return true;
}
#endif
