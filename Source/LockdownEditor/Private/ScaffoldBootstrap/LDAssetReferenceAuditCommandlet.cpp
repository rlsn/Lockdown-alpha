#include "LDAssetReferenceAuditCommandlet.h"
#include "LDEditorLog.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDevice.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/LinkerLoad.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"

namespace
{
    TArray<TSharedPtr<FJsonValue>> Strings(const TArray<FString>& Values)
    {
        TArray<TSharedPtr<FJsonValue>> Result;
        for (const FString& Value : Values) { Result.Add(MakeShared<FJsonValueString>(Value)); }
        return Result;
    }

    class FLoadErrors final : public FOutputDevice
    {
    public:
        TArray<FString> Errors;
        FCriticalSection Mutex;
        virtual void Serialize(const TCHAR* Text, ELogVerbosity::Type Verbosity, const FName& Category) override
        {
            if (Verbosity <= ELogVerbosity::Error)
            {
                FScopeLock Lock(&Mutex);
                Errors.Add(Category.ToString() + TEXT(": ") + Text);
            }
        }
    };
}

ULDAssetReferenceAuditCommandlet::ULDAssetReferenceAuditCommandlet()
{
    IsClient = false;
    IsEditor = true;
    LogToConsole = true;
}

int32 ULDAssetReferenceAuditCommandlet::Main(const FString& Params)
{
    FString RequestPath, ReportPath, RequestText;
    TSharedPtr<FJsonObject> Request;
    if (!FParse::Value(*Params, TEXT("Request="), RequestPath)
        || !FParse::Value(*Params, TEXT("Report="), ReportPath)
        || !FFileHelper::LoadFileToString(RequestText, *RequestPath)
        || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(RequestText), Request))
    {
        UE_LOG(LogLDEditor, Error, TEXT("Asset audit requires readable -Request=<json> and -Report=<json>."));
        return 1;
    }
    ReportPath = FPaths::ConvertRelativePathToFull(ReportPath);
    if (!FPaths::IsUnderDirectory(ReportPath, FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Artifacts"))))
    {
        UE_LOG(LogLDEditor, Error, TEXT("Asset audit reports must be under this worktree's Artifacts directory."));
        return 1;
    }
    TArray<FString> Roots;
    const TArray<TSharedPtr<FJsonValue>>* Members = nullptr;
    if (!Request->TryGetStringArrayField(TEXT("roots"), Roots) || Roots.IsEmpty()
        || !Request->TryGetArrayField(TEXT("members"), Members))
    {
        UE_LOG(LogLDEditor, Error, TEXT("Asset audit request requires nonempty roots and a members array."));
        return 1;
    }
    TSet<FName> Names;
    TArray<TSharedPtr<FJsonValue>> NativeRoots, NativeMembers;
    TArray<FString> MissingRoots, Failures;
    for (const FString& Root : Roots)
    {
        auto Row = MakeShared<FJsonObject>();
        UObject* Object = LoadObject<UObject>(nullptr, *Root, nullptr, LOAD_NoWarn);
        Row->SetStringField(TEXT("path"), Root);
        Row->SetBoolField(TEXT("present"), Object != nullptr);
        if (Object) { Row->SetStringField(TEXT("class"), Object->GetClass()->GetPathName()); }
        else { MissingRoots.Add(Root); }
        FString Package, Name;
        if (!Root.Split(TEXT("."), &Package, &Name, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
        {
            UE_LOG(LogLDEditor, Error, TEXT("Asset audit root must be an object path: %s"), *Root);
            return 1;
        }
        Names.Add(FName(*Name));
        NativeRoots.Add(MakeShared<FJsonValueObject>(Row));
    }
    for (const auto& Value : *Members)
    {
        const TSharedPtr<FJsonObject>* Member = nullptr;
        FString Owner, Name;
        if (!Value->TryGetObject(Member) || !(*Member)->TryGetStringField(TEXT("owner"), Owner)
            || !(*Member)->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
        {
            UE_LOG(LogLDEditor, Error, TEXT("Asset audit members require owner and name."));
            return 1;
        }
        Names.Add(FName(*Name));
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("owner"), Owner);
        Row->SetStringField(TEXT("name"), Name);
        UStruct* Struct = LoadObject<UStruct>(nullptr, *Owner);
        UClass* Class = Cast<UClass>(Struct);
        const FProperty* Property = Struct ? FindFProperty<FProperty>(Struct, *Name) : nullptr;
        const UFunction* Function = Class ? Class->FindFunctionByName(FName(*Name)) : nullptr;
        Row->SetBoolField(TEXT("present"), Property || Function);
        if (Property) { Row->SetStringField(TEXT("nativePropertyType"), Property->GetCPPType()); }
        if (Function) { Row->SetStringField(TEXT("function"), Function->GetPathName()); }
        if (!Property && !Function) { MissingRoots.Add(Owner + TEXT(":") + Name); }
        NativeMembers.Add(MakeShared<FJsonValueObject>(Row));
    }

    auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    Registry.SearchAllAssets(true);
    TArray<FAssetData> Assets;
    Registry.GetAssetsByPath(FName(TEXT("/Game")), Assets, true, true);
    TSet<FString> PackageSet;
    for (const FAssetData& Asset : Assets) { PackageSet.Add(Asset.PackageName.ToString()); }
    TArray<FString> Files, MapFiles;
    IFileManager::Get().FindFilesRecursive(Files, *FPaths::ProjectContentDir(), TEXT("*.uasset"), true, false);
    IFileManager::Get().FindFilesRecursive(MapFiles, *FPaths::ProjectContentDir(), TEXT("*.umap"), true, false);
    Files.Append(MapFiles);
    for (const FString& File : Files)
    {
        FString Package;
        if (!FPackageName::TryConvertFilenameToLongPackageName(File, Package)) { Failures.Add(TEXT("Unmapped package file: ") + File); }
        else { PackageSet.Add(Package); }
    }
    TArray<FString> Packages = PackageSet.Array();
    Packages.Sort();
    TArray<TSharedPtr<FJsonValue>> PackageRows;
    int32 HitCount = 0;
    FLoadErrors LoadErrors;
    GLog->AddOutputDevice(&LoadErrors);
    for (const FString& PackageName : Packages)
    {
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("package"), PackageName);
        TArray<FName> Dependencies, Referencers;
        Registry.GetDependencies(FName(*PackageName), Dependencies, UE::AssetRegistry::EDependencyCategory::All);
        Registry.GetReferencers(FName(*PackageName), Referencers, UE::AssetRegistry::EDependencyCategory::All);
        TArray<FString> DependencyNames, ReferencerNames;
        for (FName Name : Dependencies) { DependencyNames.Add(Name.ToString()); }
        for (FName Name : Referencers) { ReferencerNames.Add(Name.ToString()); }
        DependencyNames.Sort(); ReferencerNames.Sort();
        Row->SetArrayField(TEXT("registryDependencies"), Strings(DependencyNames));
        Row->SetArrayField(TEXT("registryReferencers"), Strings(ReferencerNames));

        UPackage* Package = LoadPackage(nullptr, *PackageName, LOAD_None);
        Row->SetBoolField(TEXT("loaded"), Package != nullptr);
        if (!Package)
        {
            Failures.Add(TEXT("LoadPackage failed: ") + PackageName);
            PackageRows.Add(MakeShared<FJsonValueObject>(Row));
            continue;
        }
        Package->FullyLoad();
        FLinkerLoad* Linker = Package->GetLinker();
        Row->SetBoolField(TEXT("linkerAvailable"), Linker != nullptr);
        TArray<FString> Imports, SoftPaths, NameHits, ReferenceHits, ObjectClasses;
        const auto CheckPath = [&Roots, &ReferenceHits](const FString& Source, const FString& Target)
        {
            for (const FString& Root : Roots)
            {
                if (Target.Contains(Root)) { ReferenceHits.AddUnique(Source + TEXT(" -> ") + Target); }
            }
        };
        if (!Linker) { Failures.Add(TEXT("Missing linker/import evidence: ") + PackageName); }
        else
        {
            for (int32 Index = 0; Index < Linker->ImportMap.Num(); ++Index)
            {
                const FString Path = Linker->GetImportPathName(Index);
                Imports.Add(Path);
                CheckPath(TEXT("serialized import"), Path);
            }
            for (const FSoftObjectPath& Path : Linker->SoftObjectPathList)
            {
                SoftPaths.Add(Path.ToString());
                CheckPath(TEXT("serialized soft path"), Path.ToString());
            }
            for (FName Name : Linker->SoftPackageReferenceList) { SoftPaths.AddUnique(Name.ToString()); }
            for (FNameEntryId Id : Linker->NameMap)
            {
                const FName Name = FName::CreateFromDisplayId(Id, 0);
                if (Names.Contains(Name)) { NameHits.AddUnique(Name.ToString()); }
            }
        }
        TArray<TSharedPtr<FJsonValue>> Blueprints, Worlds;
        int32 ObjectCount = 0;
        ForEachObjectWithPackage(Package, [&](UObject* Object)
        {
            ++ObjectCount;
            const FString Path = Object->GetPathName();
            const FString ClassPath = Object->GetClass()->GetPathName();
            ObjectClasses.AddUnique(ClassPath);
            CheckPath(Path + TEXT(" class"), ClassPath);
            TArray<UObject*> References;
            FReferenceFinder Finder(References, nullptr, false, false, false, false);
            Finder.FindReferences(Object);
            for (UObject* Reference : References)
            {
                if (Reference) { CheckPath(Path, Reference->GetPathName()); }
            }
            for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
            {
                const FProperty* Property = *It;
                if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_SkipSerialization)) { continue; }
                for (int32 Index = 0; Index < Property->ArrayDim; ++Index)
                {
                    FString Text;
                    Property->ExportTextItem_Direct(Text, Property->ContainerPtrToValuePtr<void>(Object, Index), nullptr, Object, PPF_None);
                    CheckPath(Path + TEXT(".") + Property->GetName(), Text);
                    if (Property->GetFName() == FName(TEXT("MemberName")) && Names.Contains(FName(*Text)))
                    {
                        ReferenceHits.AddUnique(Path + TEXT(" member reference -> ") + Text);
                    }
                }
            }
            if (UBlueprint* Blueprint = Cast<UBlueprint>(Object))
            {
                auto BlueprintRow = MakeShared<FJsonObject>();
                BlueprintRow->SetStringField(TEXT("object"), Path);
                BlueprintRow->SetStringField(TEXT("parent"), GetPathNameSafe(Blueprint->ParentClass));
                BlueprintRow->SetStringField(TEXT("generatedClass"), GetPathNameSafe(Blueprint->GeneratedClass));
                BlueprintRow->SetStringField(TEXT("skeletonClass"), GetPathNameSafe(Blueprint->SkeletonGeneratedClass));
                BlueprintRow->SetNumberField(TEXT("status"), int32(Blueprint->Status));
                const UObject* CDO = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject() : nullptr;
                BlueprintRow->SetStringField(TEXT("cdo"), GetPathNameSafe(CDO));
                if (!Blueprint->GeneratedClass || !CDO || Blueprint->Status == BS_Error)
                {
                    Failures.Add(TEXT("Invalid Blueprint/generated class/CDO: ") + Path);
                }
                Blueprints.Add(MakeShared<FJsonValueObject>(BlueprintRow));
            }
            if (UWorld* World = Cast<UWorld>(Object))
            {
                auto WorldRow = MakeShared<FJsonObject>();
                const AWorldSettings* Settings = World->GetWorldSettings();
                WorldRow->SetStringField(TEXT("world"), Path);
                WorldRow->SetStringField(TEXT("worldSettings"), GetPathNameSafe(Settings));
                WorldRow->SetStringField(TEXT("defaultGameMode"), Settings ? GetPathNameSafe(Settings->DefaultGameMode.Get()) : TEXT("None"));
                if (!Settings) { Failures.Add(TEXT("Missing WorldSettings: ") + Path); }
                Worlds.Add(MakeShared<FJsonValueObject>(WorldRow));
            }
            return true;
        }, EGetObjectsFlags::IncludeNestedObjects);
        HitCount += ReferenceHits.Num() + NameHits.Num();
        Row->SetNumberField(TEXT("objectCount"), ObjectCount);
        Row->SetArrayField(TEXT("serializedImports"), Strings(Imports));
        Row->SetArrayField(TEXT("serializedSoftPaths"), Strings(SoftPaths));
        Row->SetArrayField(TEXT("serializedSymbolNames"), Strings(NameHits));
        Row->SetArrayField(TEXT("loadedReferenceHits"), Strings(ReferenceHits));
        Row->SetArrayField(TEXT("loadedObjectClasses"), Strings(ObjectClasses));
        Row->SetArrayField(TEXT("blueprints"), Blueprints);
        Row->SetArrayField(TEXT("worlds"), Worlds);
        PackageRows.Add(MakeShared<FJsonValueObject>(Row));
    }
    GLog->RemoveOutputDevice(&LoadErrors);
    Failures.Append(LoadErrors.Errors);
    auto Report = MakeShared<FJsonObject>();
    Report->SetStringField(TEXT("project"), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()));
    Report->SetStringField(TEXT("scope"), TEXT("All on-disk /Game assets and project Content package files; read-only registry/import/soft/name/object/property/CDO/WorldSettings inspection. Native declarations are not serialized asset uses. Dynamic source/config paths require separate source review."));
    Report->SetArrayField(TEXT("nativeRoots"), NativeRoots);
    Report->SetArrayField(TEXT("nativeMembers"), NativeMembers);
    Report->SetArrayField(TEXT("missingNativeRoots"), Strings(MissingRoots));
    Report->SetArrayField(TEXT("failures"), Strings(Failures));
    Report->SetNumberField(TEXT("assetRegistryAssetCount"), Assets.Num());
    Report->SetNumberField(TEXT("packageFileCount"), Files.Num());
    Report->SetNumberField(TEXT("packageCount"), Packages.Num());
    Report->SetNumberField(TEXT("hitsRequiringReview"), HitCount);
    Report->SetArrayField(TEXT("packages"), PackageRows);
    auto Config = MakeShared<FJsonObject>();
    for (const TCHAR* Key : {TEXT("GameDefaultMap"), TEXT("EditorStartupMap"), TEXT("GlobalDefaultGameMode"), TEXT("GameInstanceClass")})
    {
        FString Value;
        GConfig->GetString(TEXT("/Script/EngineSettings.GameMapsSettings"), Key, Value, GEngineIni);
        Config->SetStringField(Key, Value);
    }
    Report->SetObjectField(TEXT("effectiveMapsSettings"), Config);
    const bool Complete = !Packages.IsEmpty() && Failures.IsEmpty()
        && (MissingRoots.IsEmpty() || FParse::Param(*Params, TEXT("AllowMissingRoots")));
    Report->SetBoolField(TEXT("coverageComplete"), Complete);
    FString Output;
    if (!FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Output))
        || !FFileHelper::SaveStringToFile(Output, *ReportPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        UE_LOG(LogLDEditor, Error, TEXT("Could not write asset audit report: %s"), *ReportPath);
        return 2;
    }
    UE_LOG(LogLDEditor, Display, TEXT("LD_ASSET_AUDIT coverage=%s packages=%d hits=%d failures=%d report=%s"),
        Complete ? TEXT("complete") : TEXT("incomplete"), Packages.Num(), HitCount, Failures.Num(), *ReportPath);
    return Complete ? 0 : 3;
}
