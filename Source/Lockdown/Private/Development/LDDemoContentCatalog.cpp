#include "Development/LDDemoContentCatalog.h"

#include "LDDemoContentJson.h"
#include "Algo/StableSort.h"
#include "Development/LDSkillGrowth.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

TArray<const FLDDemoStatDefinition*> FLDDemoContentDefinitions::StatsInDesignOrder() const
{
    TArray<const FLDDemoStatDefinition*> Out;
    Out.Reserve(Stats.Num());
    for (const FLDDemoStatDefinition& Stat : Stats) { Out.Add(&Stat); }
    Algo::StableSortBy(Out, [](const FLDDemoStatDefinition* Stat) { return Stat->NativeId; });
    return Out;
}

int32 FLDDemoContentDefinitions::StatDesignIndex(const FString& StatId) const
{
    // Catalog ids are lowercase; an FName-printed id may come back in another casing.
    return StatsInDesignOrder().IndexOfByPredicate([&StatId](const FLDDemoStatDefinition* Stat)
        { return Stat->Id.Equals(StatId, ESearchCase::IgnoreCase); });
}

namespace
{
template <typename T>
bool Find(const TArray<T>& Rows, const FString& Id, const T*& Out, FString& Error)
{
    LDDemoContent::FCheck Check(Error);
    // Callers often pass FName::ToString(). Without case-preserving names (every non-editor build) a common word comes
    // back in the casing it was first registered with, such as "Root" or "Shield". Catalog identities are canonical
    // lowercase, so a lookup folds first; author input is still validated strictly at import.
    const FString Canonical = Id.ToLower();
    if (!LDDemoContent::Id(Canonical, Check, TEXT("definition lookup")))
    {
        return false;
    }
    const T* Row = Rows.FindByPredicate([&](const T& Candidate) { return Candidate.Id.Equals(Canonical, ESearchCase::CaseSensitive); });
    if (!Check.Require(Row != nullptr, Canonical, TEXT("definition is not present in this validated catalog")))
    {
        return false;
    }
    Out = Row;
    Error.Reset();
    return true;
}

bool ExactStruct(const UStruct* Type, const void* A, const void* B);

bool ExactProperty(const FProperty* Property, const void* A, const void* B)
{
    if (const auto* Text = CastField<FStrProperty>(Property))
    {
        const FString& Left = Text->GetPropertyValue(A);
        const FString& Right = Text->GetPropertyValue(B);
        return Left.Len() == Right.Len() && FMemory::Memcmp(*Left, *Right, Left.Len() * sizeof(TCHAR)) == 0;
    }
    if (const auto* Struct = CastField<FStructProperty>(Property))
    {
        return ExactStruct(Struct->Struct, A, B);
    }
    if (const auto* Array = CastField<FArrayProperty>(Property))
    {
        FScriptArrayHelper Left(Array, A), Right(Array, B);
        if (Left.Num() != Right.Num()) { return false; }
        for (int32 I = 0; I < Left.Num(); ++I)
        {
            if (!ExactProperty(Array->Inner, Left.GetRawPtr(I), Right.GetRawPtr(I))) { return false; }
        }
        return true;
    }
    return Property->Identical(A, B, 0);
}

bool ExactStruct(const UStruct* Type, const void* A, const void* B)
{
    for (TFieldIterator<FProperty> It(Type); It; ++It)
    {
        const FProperty* Property = *It;
        for (int32 I = 0; I < Property->ArrayDim; ++I)
        {
            if (!ExactProperty(Property, Property->ContainerPtrToValuePtr<void>(A, I),
                Property->ContainerPtrToValuePtr<void>(B, I)))
            {
                return false;
            }
        }
    }
    return true;
}

bool SameProjection(const FLDDemoContentDefinitions& A, const FLDDemoContentDefinitions& B, FString& Error)
{
    LDDemoContent::FCheck Check(Error);
    // FStrProperty::Identical uses FString's case-insensitive equality in UE 5.8.
    return Check.Require(ExactStruct(FLDDemoContentDefinitions::StaticStruct(), &A, &B),
        TEXT("asset"), TEXT("serialized typed projection differs from compiler bytes"));
}

bool ReadBundleFiles(const FString& ProjectRoot, FLDDemoContentBundle& Out,
    TArray<TUniquePtr<IFileHandle>>& HeldFiles, FString& Error)
{
    LDDemoContent::FCheck Check(Error);
    FLDDemoContentBundle Candidate;
    TArray<TUniquePtr<IFileHandle>> Handles;
    struct FFile { const TCHAR* Path; TArray<uint8>* Bytes; };
    for (const auto& File : {
        FFile{TEXT("Content\\Lockdown\\Data\\Demo\\manifest.json"), &Candidate.Manifest},
        {TEXT("Content\\Lockdown\\Data\\Demo\\demo_content.json"), &Candidate.Content},
        {TEXT("Content\\Lockdown\\Data\\Demo\\world.json"), &Candidate.World},
        {TEXT("Source\\Lockdown\\Private\\Development\\LDDemoContentContract.h"), &Candidate.ContentContract},
        {TEXT("Source\\Lockdown\\Private\\World\\LDDemoWorldContract.h"), &Candidate.WorldContract},
        {TEXT("SourceData\\Demo\\content.schema.json"), &Candidate.AuthorSchema},
        {TEXT("SourceData\\Demo\\native_ids.json"), &Candidate.NativeIds},
        {TEXT("Source\\Lockdown\\Public\\Development\\LDDemoContentIds.h"), &Candidate.NativeContract},
        {TEXT("SourceData\\World\\demo_world.json"), &Candidate.WorldSource},
        {TEXT("SourceData\\World\\demo_world.schema.json"), &Candidate.WorldAuthorSchema}})
    {
        const FString Path = FPaths::Combine(ProjectRoot, File.Path);
        auto Opened = FPlatformFileManager::Get().GetPlatformFile().OpenRead(*Path, IPlatformFile::EOpenReadFlags::None);
        if (!Check.Require(!Opened.HasError(), Path, TEXT("missing, locked or unreadable input")))
        {
            return false;
        }
        TUniquePtr<IFileHandle> Handle = Opened.StealValue();
        const int64 Size = Handle->Size();
        if (!Check.Require(Size > 0 && Size <= LDDemoContent::MaxFileBytes, Path, TEXT("empty or oversized input")))
        {
            return false;
        }
        File.Bytes->SetNumUninitialized(static_cast<int32>(Size));
        if (!Check.Require(Handle->Read(File.Bytes->GetData(), Size), Path, TEXT("source read failed")))
        {
            return false;
        }
        Handles.Add(MoveTemp(Handle));
    }
    Out = MoveTemp(Candidate);
    HeldFiles = MoveTemp(Handles);
    Error.Reset();
    return true;
}
}

bool FLDDemoContentBundle::ReadImportFiles(const FString& ProjectRoot, FLDDemoContentBundle& Out, FString& Error)
{
    TArray<TUniquePtr<IFileHandle>> HeldFiles;
    return ReadBundleFiles(ProjectRoot, Out, HeldFiles, Error);
}

bool LDDemoContent::ExactStoredStruct(const UStruct* Type, const void* A, const void* B)
{
    return ExactStruct(Type, A, B);
}

FLDDemoContentSnapshot::FLDDemoContentSnapshot(FLDDemoContentDefinitions&& In) : Definitions(MoveTemp(In))
{
    const int32 MaxLevel = FMath::Clamp(Definitions.Progression.MaxLevel, 0, int32(MAX_uint8));
    GrownValues.SetNum(Definitions.Abilities.Num());
    for (int32 Index = 0; Index < Definitions.Abilities.Num(); ++Index)
    {
        const FLDDemoAbilityValues& Authored = Definitions.Abilities[Index].Values;
        TArray<TOptional<FLDDemoAbilityValues>>& Grown = GrownValues[Index];
        Grown.SetNum(MaxLevel);
        for (int32 Level = 1; Level <= MaxLevel; ++Level)
        {
            // Content validation proves every grown amount fits; a level that does not stays unset so FindValues refuses
            // it instead of guessing.
            FLDDemoAbilityValues Row;
            if (LDSkillGrowth::GrowValues(Authored, Definitions.Parameters.SkillGrowthPerLevelBp,
                Definitions.Parameters.SkillGrowthPerLevelAfter10Bp, uint8(Level), Row))
            {
                Grown[Level - 1] = MoveTemp(Row);
            }
        }
    }
}

const FLDDemoAbilityValues* FLDDemoContentSnapshot::FindValues(const FLDDemoAbilityDefinition& Ability, uint8 Level) const
{
    // Membership by address first: Ability may belong to another snapshot or be a free-standing object.
    const int32 Index = Definitions.Abilities.IndexOfByPredicate([&Ability](const FLDDemoAbilityDefinition& Row) { return &Row == &Ability; });
    if (Index == INDEX_NONE || Level < 1 || int32(Level) > GrownValues[Index].Num())
    {
        return nullptr;
    }
    const TOptional<FLDDemoAbilityValues>& Row = GrownValues[Index][Level - 1];
    return Row.IsSet() ? &Row.GetValue() : nullptr;
}

bool FLDDemoContentSnapshot::FindAbility(const FString& Id, const FLDDemoAbilityDefinition*& Out, FString& Error) const
{
    return Find(Definitions.Abilities, Id, Out, Error);
}

bool FLDDemoContentSnapshot::FindStat(const FString& Id, const FLDDemoStatDefinition*& Out, FString& Error) const
{
    return Find(Definitions.Stats, Id, Out, Error);
}

bool FLDDemoContentSnapshot::FindComponent(const FString& Id, const FLDDemoComponentDefinition*& Out, FString& Error) const
{
    return Find(Definitions.Components, Id, Out, Error);
}

bool FLDDemoContentSnapshot::FindMonster(const FString& Id, const FLDDemoMonsterDefinition*& Out, FString& Error) const
{
    return Find(Definitions.Monsters, Id, Out, Error);
}

bool FLDDemoContentSnapshot::FindDisplay(const FString& Id, const FLDDemoDisplayDefinition*& Out, FString& Error) const
{
    return Find(Definitions.Display, Id, Out, Error);
}

const TCHAR* ULDDemoContentCatalog::AssetPath()
{
    return TEXT("/Game/Lockdown/Generated/Data/DA_DemoContent.DA_DemoContent");
}

bool ULDDemoContentCatalog::PrepareImport(const ULDDemoContentCatalog& Target, const FLDDemoContentBundle& Bundle, const FLDDemoContentDigest& ExpectedRoot,
    TUniquePtr<FLDDemoPreparedContent>& Out, FString& Error)
{
    LDDemoContent::FCheck Check(Error);
    if (!Check.Require(IsInGameThread(), TEXT("import"), TEXT("target-bound preparation requires the game thread")))
    {
        return false;
    }
    TUniquePtr<FLDDemoPreparedContent> Candidate(new FLDDemoPreparedContent());
    if (!LDDemoContent::Project(Bundle, ExpectedRoot, Candidate->Definitions, Error))
    {
        return false;
    }
    Candidate->Target = FObjectKey(&Target);
    Candidate->TargetGeneration = Target.ImportGeneration;
    Candidate->Bundle = Bundle;
    Out = MoveTemp(Candidate);
    return true;
}

bool ULDDemoContentCatalog::PrepareImportFromFiles(const ULDDemoContentCatalog& Target, const FString& ProjectRoot, const FLDDemoContentDigest& ExpectedRoot,
    TUniquePtr<FLDDemoPreparedContent>& Out, FString& Error)
{
    FLDDemoContentBundle Bundle;
    TUniquePtr<FLDDemoPreparedContent> Candidate;
    if (!FLDDemoContentBundle::ReadImportFiles(ProjectRoot, Bundle, Error) || !PrepareImport(Target, Bundle, ExpectedRoot, Candidate, Error))
    {
        return false;
    }
    Candidate->ImportRoot = FPaths::ConvertRelativePathToFull(ProjectRoot);
    Out = MoveTemp(Candidate);
    return true;
}

bool ULDDemoContentCatalog::CommitImport(TUniquePtr<FLDDemoPreparedContent>& Prepared, const FLDDemoContentDigest& CurrentExpectedRoot, FString& Error)
{
    LDDemoContent::FCheck Check(Error);
    if (!Check.Require(IsInGameThread(), TEXT("import"), TEXT("asset commits require the game thread"))
        || !Check.Require(Prepared.IsValid(), TEXT("import"), TEXT("missing or already consumed prepared import"))
        || !Check.Require(Prepared->Target == FObjectKey(this) && Prepared->TargetGeneration == ImportGeneration,
            TEXT("import"), TEXT("wrong target or stale import generation"))
        || !Check.Require(Prepared->Definitions.Identity.Root == CurrentExpectedRoot, TEXT("import"), TEXT("current expected root changed"))
        || !Check.Require(ImportGeneration < MAX_uint64, TEXT("import"), TEXT("import generation exhausted")))
    {
        return false;
    }
    TArray<TUniquePtr<IFileHandle>> HeldFiles;
    if (!Prepared->ImportRoot.IsEmpty())
    {
        FLDDemoContentBundle Current;
        if (!ReadBundleFiles(Prepared->ImportRoot, Current, HeldFiles, Error)
            || !Check.Require(FLDDemoContentBundle::StaticStruct()->CompareScriptStruct(&Current, &Prepared->Bundle, 0),
                TEXT("import"), TEXT("source bytes changed after preparation")))
        {
            return false;
        }
    }
    // All failure-prone validation/allocation precedes this game-thread commit.
    ImportedBytes = MoveTemp(Prepared->Bundle);
    Definitions = MoveTemp(Prepared->Definitions);
    ++ImportGeneration;
    Prepared.Reset();
    MarkPackageDirty();
    Error.Reset();
    return true;
}

bool ULDDemoContentCatalog::Validate(const FLDDemoContentDigest& ExpectedRoot, FString& Error) const
{
    TUniquePtr<FLDDemoPreparedContent> Expected;
    return PrepareImport(*this, ImportedBytes, ExpectedRoot, Expected, Error) && ValidatePrepared(*Expected, Error);
}

bool ULDDemoContentCatalog::ValidatePrepared(const FLDDemoPreparedContent& Expected, FString& Error) const
{
    LDDemoContent::FCheck Check(Error);
    if (!Check.Require(IsInGameThread(), TEXT("asset"), TEXT("asset validation requires the game thread"))
        || !Check.Require(FLDDemoContentBundle::StaticStruct()->CompareScriptStruct(&ImportedBytes, &Expected.Bundle, 0),
            TEXT("asset"), TEXT("stored bytes differ from the current prepared import"))
        || !SameProjection(Definitions, Expected.Definitions, Error))
    {
        return false;
    }
    Error.Reset();
    return true;
}

bool ULDDemoContentCatalog::Read(const FLDDemoContentDigest& ExpectedRoot,
    TSharedPtr<const FLDDemoContentSnapshot>& Out, FString& Error) const
{
    LDDemoContent::FCheck Check(Error);
    if (!Check.Require(IsInGameThread(), TEXT("asset"), TEXT("asset reads require the game thread")))
    {
        return false;
    }
    FLDDemoContentDefinitions Rebuilt;
    if (!LDDemoContent::Project(ImportedBytes, ExpectedRoot, Rebuilt, Error) || !SameProjection(Definitions, Rebuilt, Error))
    {
        return false;
    }
    Out = TSharedPtr<const FLDDemoContentSnapshot>(new FLDDemoContentSnapshot(MoveTemp(Rebuilt)));
    Error.Reset();
    return true;
}

bool ULDDemoContentCatalog::Load(const FLDDemoContentDigest& ExpectedRoot,
    TSharedPtr<const FLDDemoContentSnapshot>& Out, FString& Error)
{
    LDDemoContent::FCheck Check(Error);
    if (!Check.Require(IsInGameThread(), TEXT("load"), TEXT("asset loading requires the game thread")))
    {
        return false;
    }
    const ULDDemoContentCatalog* Asset = LoadObject<ULDDemoContentCatalog>(nullptr, AssetPath());
    if (!Check.Require(Asset != nullptr, AssetPath(), TEXT("required native content asset is absent; no legacy fallback")))
    {
        return false;
    }
    return Asset->ReadRuntimeFiles(FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Lockdown\\Data\\Demo")),
        ExpectedRoot, Out, Error);
}

#if WITH_DEV_AUTOMATION_TESTS
bool ULDDemoContentCatalog::SameDevReloadBase(const FLDDemoContentIdentity& Compiled, const FLDDemoContentIdentity& Reloaded,
    FString& Error)
{
    struct FField { const TCHAR* Name; bool bSame; };
    for (const FField& Field : {
        FField{TEXT("world.json"), Compiled.World == Reloaded.World},
        {TEXT("content schema"), Compiled.AuthorSchema == Reloaded.AuthorSchema},
        {TEXT("native ids"), Compiled.NativeIds == Reloaded.NativeIds},
        {TEXT("native contract"), Compiled.NativeContract == Reloaded.NativeContract},
        {TEXT("world contract"), Compiled.WorldContract == Reloaded.WorldContract},
        {TEXT("content schema version"), Compiled.SchemaVersion == Reloaded.SchemaVersion},
        {TEXT("handler revision"), Compiled.HandlerRevision == Reloaded.HandlerRevision},
        {TEXT("projection revision"), Compiled.ProjectionRevision == Reloaded.ProjectionRevision},
        {TEXT("world schema version"), Compiled.WorldSchemaVersion == Reloaded.WorldSchemaVersion},
        {TEXT("world projection revision"), Compiled.WorldProjectionRevision == Reloaded.WorldProjectionRevision}})
    {
        if (!Field.bSame)
        {
            Error = FString::Printf(TEXT("%s differs from the compiled build; only demo_content.json numbers reload, this change needs Bootstrap and a rebuild."), Field.Name);
            return false;
        }
    }
    Error.Reset();
    return true;
}

bool ULDDemoContentCatalog::ReadDevReloadFiles(const FString& ProjectRoot, const FLDDemoContentDigest& CompiledRoot,
    const FLDDemoContentDigest& DiskRoot, TSharedPtr<const FLDDemoContentSnapshot>& Out, FString& Error) const
{
    LDDemoContent::FCheck Check(Error);
    if (!Check.Require(IsInGameThread(), TEXT("asset"), TEXT("asset reads require the game thread")) || !Validate(CompiledRoot, Error))
    {
        return false;
    }
    FLDDemoContentBundle Disk;
    FLDDemoContentDefinitions Rebuilt;
    if (!FLDDemoContentBundle::ReadImportFiles(ProjectRoot, Disk, Error) || !LDDemoContent::Project(Disk, DiskRoot, Rebuilt, Error)
        || !SameDevReloadBase(Definitions.Identity, Rebuilt.Identity, Error))
    {
        return false;
    }
    if (!Check.Require(Disk.WorldSource == ImportedBytes.WorldSource && Disk.WorldAuthorSchema == ImportedBytes.WorldAuthorSchema,
        TEXT("world source"), TEXT("differs from the compiled build; only demo_content.json numbers reload")))
    {
        return false;
    }
    Out = TSharedPtr<const FLDDemoContentSnapshot>(new FLDDemoContentSnapshot(MoveTemp(Rebuilt)));
    Error.Reset();
    return true;
}

bool ULDDemoContentCatalog::LoadDevReload(const FLDDemoContentDigest& CompiledRoot, const FLDDemoContentDigest& DiskRoot,
    TSharedPtr<const FLDDemoContentSnapshot>& Out, FString& Error)
{
    LDDemoContent::FCheck Check(Error);
    if (!Check.Require(IsInGameThread(), TEXT("load"), TEXT("asset loading requires the game thread")))
    {
        return false;
    }
    const ULDDemoContentCatalog* Asset = LoadObject<ULDDemoContentCatalog>(nullptr, AssetPath());
    if (!Check.Require(Asset != nullptr, AssetPath(), TEXT("required native content asset is absent; no legacy fallback")))
    {
        return false;
    }
    return Asset->ReadDevReloadFiles(FPaths::ProjectDir(), CompiledRoot, DiskRoot, Out, Error);
}
#endif

bool ULDDemoContentCatalog::ReadRuntimeFiles(const FString& DataDirectory, const FLDDemoContentDigest& ExpectedRoot,
    TSharedPtr<const FLDDemoContentSnapshot>& Out, FString& Error) const
{
    LDDemoContent::FCheck Check(Error);
    if (!Check.Require(IsInGameThread(), TEXT("asset"), TEXT("asset reads require the game thread")))
    {
        return false;
    }
    FLDDemoContentBundle Current = ImportedBytes;
    if (!LDDemoContent::ReadFile(FPaths::Combine(DataDirectory, TEXT("manifest.json")), Current.Manifest, Error)
        || !LDDemoContent::ReadFile(FPaths::Combine(DataDirectory, TEXT("demo_content.json")), Current.Content, Error)
        || !LDDemoContent::ReadFile(FPaths::Combine(DataDirectory, TEXT("world.json")), Current.World, Error))
    {
        return false;
    }
    FLDDemoContentDefinitions Rebuilt;
    if (!LDDemoContent::Project(Current, ExpectedRoot, Rebuilt, Error)
        || !SameProjection(Definitions, Rebuilt, Error) || !Validate(ExpectedRoot, Error))
    {
        return false;
    }
    Out = TSharedPtr<const FLDDemoContentSnapshot>(new FLDDemoContentSnapshot(MoveTemp(Rebuilt)));
    Error.Reset();
    return true;
}
