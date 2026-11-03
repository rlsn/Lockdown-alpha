#include "LDScaffoldBootstrapCommandlet.h"
#include "LDDemoWorldBuilder.h"
#include "Audio/LDDemoAudioImport.h"
#include "Framework/LDDemoEntry.h"
#include "Framework/LDGameMode.h"
#include "Development/LDDemoContentCatalog.h"
#include "Region/LDMapDefinition.h"
#include "UI/LDUIRoot.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"
#include "FileHelpers.h"

ULDScaffoldBootstrapCommandlet::ULDScaffoldBootstrapCommandlet()
{
    IsClient = false;
    IsEditor = true;
    LogToConsole = true;
}

int32 ULDScaffoldBootstrapCommandlet::Main(const FString& Params)
{
    for (const TCHAR* Option : {TEXT("RebuildRegionMaps"), TEXT("RebuildV08Region")})
    {
        if (FParse::Param(*Params, Option))
        {
            UE_LOG(LogTemp, Error, TEXT("LD_DEMO_BOOTSTRAP_REJECTED Retired bootstrap option: %s"), Option);
            return 1;
        }
    }
    const bool ValidateOnly = FParse::Param(*Params, TEXT("Validate"));
    const bool Regenerate = FParse::Param(*Params, TEXT("Regenerate"));
    const bool RefreshDefinitions = FParse::Param(*Params, TEXT("RefreshDefinitions"));
    FString Error, DigestHex, Output;
    if ((ValidateOnly && Regenerate) || (RefreshDefinitions && (ValidateOnly || Regenerate)))
    {
        UE_LOG(LogTemp, Error, TEXT("LD_DEMO_BOOTSTRAP_REJECTED Validate, Regenerate and RefreshDefinitions are exclusive."));
        return 1;
    }
    if (!FParse::Value(*Params, TEXT("Output="), Output))
    {
        Output = FPaths::Combine(FPaths::ProjectDir(), TEXT("Artifacts"), TEXT("DemoBootstrap"), FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S")));
    }
    if (!LDDemoEntry::VerifyPayloadFiles(Error, &DigestHex))
    {
        UE_LOG(LogTemp, Error, TEXT("LD_DEMO_BOOTSTRAP_REJECTED %s"), *Error);
        return 2;
    }
    FLDDemoContentDigest ExpectedRoot;
    TUniquePtr<FLDDemoPreparedContent> Prepared;
    const FString ContentPackage = FPackageName::ObjectPathToPackageName(FString(ULDDemoContentCatalog::AssetPath()));
    const bool HadContent = FPackageName::DoesPackageExist(ContentPackage);
    if (RefreshDefinitions && (!HadContent || !FPackageName::DoesPackageExist(LDDemoEntry::WorldDefinition)
        || !FPackageName::DoesPackageExist(LDDemoEntry::WorldMap)))
    {
        UE_LOG(LogTemp, Error, TEXT("LD_DEMO_BOOTSTRAP_REJECTED RefreshDefinitions requires existing saved Demo assets."));
        return 4;
    }
    auto* Content = HadContent ? LoadObject<ULDDemoContentCatalog>(nullptr, ULDDemoContentCatalog::AssetPath()) : nullptr;
    if (!Content && !HadContent && !ValidateOnly)
    {
        Content = NewObject<ULDDemoContentCatalog>(CreatePackage(*ContentPackage), *FPackageName::GetShortName(ContentPackage), RF_Public | RF_Standalone);
    }
    if (!FLDDemoContentDigest::ParseHex(DigestHex, ExpectedRoot, Error) || !Content
        || !ULDDemoContentCatalog::PrepareImportFromFiles(*Content, FPaths::ProjectDir(), ExpectedRoot, Prepared, Error))
    {
        UE_LOG(LogTemp, Error, TEXT("LD_DEMO_BOOTSTRAP_REJECTED Missing/invalid native content target or approved input: %s"), *Error);
        return 4;
    }
    if (!ValidateOnly && (!HadContent || Regenerate || RefreshDefinitions))
    {
        if (!Content->CommitImport(Prepared, ExpectedRoot, Error)
            || !LDDemoWorldBuilder::SaveAsset(*Content, ContentPackage, Error))
        {
            UE_LOG(LogTemp, Error, TEXT("LD_DEMO_BOOTSTRAP_REJECTED Content import/save failed: %s"), *Error);
            return 4;
        }
    }
    else if (!Content->ValidatePrepared(*Prepared, Error))
    {
        UE_LOG(LogTemp, Error, TEXT("LD_DEMO_BOOTSTRAP_REJECTED Saved content differs; regenerate explicitly: %s"), *Error);
        return 4;
    }
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if (!Content->Read(ExpectedRoot, Catalog, Error))
    {
        UE_LOG(LogTemp, Error, TEXT("LD_DEMO_BOOTSTRAP_REJECTED Saved content cannot be read: %s"), *Error);
        return 4;
    }
    const bool HadWorldDefinition = FPackageName::DoesPackageExist(LDDemoEntry::WorldDefinition);
    auto* WorldDefinition = HadWorldDefinition
        ? LoadObject<ULDMapDefinition>(nullptr, *(FString(LDDemoEntry::WorldDefinition) + TEXT(".DA_DemoWorld"))) : nullptr;
    if (!WorldDefinition && !HadWorldDefinition && !ValidateOnly)
    {
        WorldDefinition = NewObject<ULDMapDefinition>(CreatePackage(LDDemoEntry::WorldDefinition), TEXT("DA_DemoWorld"), RF_Public | RF_Standalone);
    }
    if (!WorldDefinition)
    {
        UE_LOG(LogTemp, Error, TEXT("LD_DEMO_BOOTSTRAP_REJECTED Native Demo world definition is absent or unreadable."));
        return 5;
    }
    if (!ValidateOnly && (!HadWorldDefinition || Regenerate || RefreshDefinitions))
    {
        TArray<uint8> WorldBytes;
        if (!FFileHelper::LoadFileToArray(WorldBytes, *FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Lockdown\\Data\\Demo\\world.json")))
            || !WorldDefinition->Import(WorldBytes, *Catalog, Error)
            || !LDDemoWorldBuilder::SaveAsset(*WorldDefinition, LDDemoEntry::WorldDefinition, Error))
        {
            UE_LOG(LogTemp, Error, TEXT("LD_DEMO_BOOTSTRAP_REJECTED World definition import/save failed: %s"), *Error);
            return 5;
        }
    }
    TSharedPtr<const FLDDemoWorldGeometry> Geometry;
    if (!WorldDefinition->Read(*Catalog, Geometry, Error))
    {
        UE_LOG(LogTemp, Error, TEXT("LD_DEMO_BOOTSTRAP_REJECTED World projection differs; regenerate explicitly: %s"), *Error);
        return 5;
    }
    for (const auto& Expected : {
        TPair<const TCHAR*, const TCHAR*>(TEXT("GameDefaultMap"), LDDemoEntry::FrontendMap),
        TPair<const TCHAR*, const TCHAR*>(TEXT("EditorStartupMap"), LDDemoEntry::FrontendMap),
        TPair<const TCHAR*, const TCHAR*>(TEXT("GlobalDefaultGameMode"), TEXT("/Script/Lockdown.LDGameMode")),
        TPair<const TCHAR*, const TCHAR*>(TEXT("GameInstanceClass"), TEXT("/Script/Lockdown.LDGameInstance"))})
    {
        FString Actual;
        if (!GConfig->GetString(TEXT("/Script/EngineSettings.GameMapsSettings"), Expected.Key, Actual, GEngineIni)
            || Actual != Expected.Value)
        {
            UE_LOG(LogTemp, Error, TEXT("LD_DEMO_BOOTSTRAP_REJECTED Effective %s is not the Demo entry contract."), Expected.Key);
            return 4;
        }
    }
    auto* Widget = LoadObject<UWidgetBlueprint>(nullptr, TEXT("/Game/Lockdown/Generated/UI/WBP_UIRoot.WBP_UIRoot"));
    if (!Widget || !Widget->GeneratedClass || !Widget->GeneratedClass->IsChildOf(ULDUIRoot::StaticClass())
        || !Widget->GeneratedClass->GetDefaultObject() || Widget->Status == BS_Error)
    {
        UE_LOG(LogTemp, Error, TEXT("LD_DEMO_BOOTSTRAP_REJECTED UI root, generated class or CDO is invalid."));
        return 4;
    }
    for (const TCHAR* PackagePath : {LDDemoEntry::FrontendMap, LDDemoEntry::WorldMap})
    {
        UWorld* World = nullptr;
        const bool IsDemoWorld = FString(PackagePath) == LDDemoEntry::WorldMap;
        if (IsDemoWorld && !ValidateOnly && (Regenerate || !FPackageName::DoesPackageExist(PackagePath)))
        {
            if (!LDDemoWorldBuilder::Generate(*WorldDefinition, *Catalog, World, Error))
            {
                UE_LOG(LogTemp, Error, TEXT("LD_DEMO_BOOTSTRAP_REJECTED World generation failed: %s"), *Error);
                return 6;
            }
        }
        else if (IsDemoWorld)
        {
            World = UEditorLoadingAndSavingUtils::LoadMap(FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetMapPackageExtension()));
        }
        else
        {
            UPackage* Package = LoadPackage(nullptr, PackagePath, LOAD_None);
            World = Package ? UWorld::FindWorldInPackage(Package) : nullptr;
        }
        AWorldSettings* Settings = World ? World->GetWorldSettings() : nullptr;
        if (!World || !World->PersistentLevel || !Settings
            || (Settings->DefaultGameMode && Settings->DefaultGameMode != ALDGameMode::StaticClass()))
        {
            UE_LOG(LogTemp, Error, TEXT("LD_DEMO_BOOTSTRAP_REJECTED Missing world or retired GameMode: %s"), PackagePath);
            return 5;
        }
        if (!IsDemoWorld) { continue; }
        if (!LDDemoWorldBuilder::Validate(*World, *WorldDefinition, *Catalog, Output, Error))
        {
            UE_LOG(LogTemp, Error, TEXT("LD_DEMO_BOOTSTRAP_REJECTED Native world geometry/navigation validation failed: %s"), *Error);
            return 6;
        }
    }
    // Audio: the cue catalog must resolve and match its CC0 sources before anything is packaged.
    if (!LDDemoAudioImport::Validate(Error))
    {
        UE_LOG(LogTemp, Error, TEXT("LD_DEMO_BOOTSTRAP_REJECTED Demo audio: %s"), *Error);
        return 7;
    }
    UE_LOG(LogTemp, Display, TEXT("LD_DEMO_BOOTSTRAP_SUCCESS Demo input bytes, native geometry/collision/navigation and root bindings validated; gameplay acceptance is separate."));
    return 0;
}
