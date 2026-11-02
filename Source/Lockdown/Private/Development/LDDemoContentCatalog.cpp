#include "Development/LDDemoContentCatalog.h"

#include "LDDemoContentJson.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace
{
template <typename T>
bool Find(const TArray<T>& Rows, const FString& Id, const T*& Out, FString& Error)
{
    LDDemoContent::FCheck Check(Error);
    if (!LDDemoContent::Id(Id, Check, TEXT("definition lookup")))
    {
        return false;
    }
    const T* Row = Rows.FindByPredicate([&](const T& Candidate) { return Candidate.Id == Id; });
    if (!Check.Require(Row != nullptr, Id, TEXT("definition is not present in this validated catalog")))
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

bool FLDDemoContentSnapshot::FindAbility(const FString& Id, const FLDDemoAbilityDefinition*& Out, FString& Error) const
{
    return Find(Definitions.Abilities, Id, Out, Error);
}

bool FLDDemoContentSnapshot::FindAttribute(const FString& Id, const FLDDemoAttributeDefinition*& Out, FString& Error) const
{
    return Find(Definitions.Attributes, Id, Out, Error);
}

bool FLDDemoContentSnapshot::FindEquipment(const FString& Id, const FLDDemoEquipmentDefinition*& Out, FString& Error) const
{
    return Find(Definitions.Equipment, Id, Out, Error);
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
