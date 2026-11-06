#include "LDDemoUIImageImport.h"
#include "LDEditorLog.h"
#include "UI/LDUIBackgrounds.h"
#include "Framework/LDDemoEntry.h"
#include "Dom/JsonObject.h"
#include "EditorFramework/AssetImportData.h"
#include "Engine/Texture2D.h"
#include "Factories/TextureFactory.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
    struct FUIImageSpec
    {
        FString Id;
        FString Path;
        FString Package;
        int32 Width = 0;
        int32 Height = 0;
    };

    bool UIFail(FString& Error, const FString& Message) { Error = Message; return false; }

    FString UISourceRoot() { return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("SourceData"), TEXT("UI"))); }

    FString UIAbsolute(const FString& Relative) { return FPaths::Combine(UISourceRoot(), Relative); }

    // The runtime knows exactly these images; the manifest may list nothing else.
    FString UIPackageFor(const FString& Id)
    {
        if (Id == LDUIBackgrounds::TitleId) { return LDUIBackgrounds::TitlePackage; }
        if (Id == LDUIBackgrounds::LobbyId) { return LDUIBackgrounds::LobbyPackage; }
        return FString();
    }

    bool UILoadManifest(TArray<FUIImageSpec>& Out, FString& Error)
    {
        Out.Reset();
        FString Text;
        if (!FFileHelper::LoadFileToString(Text, *UIAbsolute(TEXT("ui_images.json")))) { return UIFail(Error, TEXT("SourceData/UI/ui_images.json is missing.")); }
        TSharedPtr<FJsonObject> Json;
        int32 Schema = 0;
        const TArray<TSharedPtr<FJsonValue>>* Images = nullptr;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid()
            || !Json->TryGetNumberField(TEXT("schema"), Schema) || Schema != 1 || !Json->TryGetArrayField(TEXT("images"), Images))
        {
            return UIFail(Error, TEXT("ui_images.json must be valid JSON with schema 1 and an images list."));
        }
        TSet<FString> Ids, Listed;
        for (const TSharedPtr<FJsonValue>& Value : *Images)
        {
            const TSharedPtr<FJsonObject>* Object = nullptr;
            FUIImageSpec Spec;
            FString Hash, Purpose, Provenance;
            if (!Value->TryGetObject(Object) || !(*Object)->TryGetStringField(TEXT("id"), Spec.Id) || Ids.Contains(Spec.Id)
                || !(*Object)->TryGetStringField(TEXT("path"), Spec.Path) || Spec.Path.Contains(TEXT("/")) || Spec.Path.Contains(TEXT("\\"))
                || Spec.Path.Contains(TEXT("..")) || !Spec.Path.EndsWith(TEXT(".png")) || Listed.Contains(Spec.Path)
                || !(*Object)->TryGetStringField(TEXT("sha256"), Hash) || !(*Object)->TryGetNumberField(TEXT("width"), Spec.Width)
                || !(*Object)->TryGetNumberField(TEXT("height"), Spec.Height) || Spec.Width <= 0 || Spec.Height <= 0
                || !(*Object)->TryGetStringField(TEXT("purpose"), Purpose) || Purpose.IsEmpty()
                || !(*Object)->TryGetStringField(TEXT("provenance"), Provenance) || Provenance.IsEmpty())
            {
                return UIFail(Error, TEXT("Every UI image needs a unique id, a unique .png file name, sha256, width, height, purpose and provenance."));
            }
            Spec.Package = UIPackageFor(Spec.Id);
            if (Spec.Package.IsEmpty()) { return UIFail(Error, TEXT("ui_images.json lists an image the game does not use: ") + Spec.Id); }
            FString HashError;
            if (!LDDemoEntry::VerifyFileHash(UIAbsolute(Spec.Path), Hash, HashError))
            {
                return UIFail(Error, FString::Printf(TEXT("SourceData/UI/%s is missing or differs from its manifest SHA-256."), *Spec.Path));
            }
            Ids.Add(Spec.Id);
            Listed.Add(Spec.Path);
            Out.Add(MoveTemp(Spec));
        }
        if (!Ids.Contains(LDUIBackgrounds::TitleId) || !Ids.Contains(LDUIBackgrounds::LobbyId))
        {
            return UIFail(Error, TEXT("ui_images.json must list both the title and the lobby background."));
        }
        TArray<FString> Found;
        IFileManager::Get().FindFiles(Found, *(UISourceRoot() / TEXT("*")), true, false);
        for (const FString& File : Found)
        {
            if (File != TEXT("ui_images.json") && !Listed.Contains(File)) { return UIFail(Error, TEXT("Unlisted file under SourceData/UI: ") + File); }
        }
        TArray<FString> Folders;
        IFileManager::Get().FindFiles(Folders, *(UISourceRoot() / TEXT("*")), false, true);
        if (!Folders.IsEmpty()) { return UIFail(Error, TEXT("SourceData/UI holds no folders: ") + Folders[0]); }
        Error.Reset();
        return true;
    }

    // Full-screen pictures: sharp at any resolution (no mips, high-quality compression), never streamed out.
    void UIApplySettings(UTexture2D& Texture)
    {
        Texture.LODGroup = TEXTUREGROUP_UI;
        Texture.MipGenSettings = TMGS_NoMipmaps;
        Texture.CompressionSettings = TC_BC7;
        Texture.SRGB = true;
        Texture.NeverStream = true;
    }

    bool UISettingsMatch(const UTexture2D& Texture)
    {
        return Texture.LODGroup == TEXTUREGROUP_UI && Texture.MipGenSettings == TMGS_NoMipmaps && Texture.CompressionSettings == TC_BC7
            && Texture.SRGB && Texture.NeverStream;
    }

    UTexture2D* UILoad(const FUIImageSpec& Spec)
    {
        return FPackageName::DoesPackageExist(Spec.Package)
            ? LoadObject<UTexture2D>(nullptr, *(Spec.Package + TEXT(".") + FPackageName::GetShortName(Spec.Package))) : nullptr;
    }
}

bool LDDemoUIImageImport::Import(FString& Error)
{
    TArray<FUIImageSpec> Specs;
    if (!UILoadManifest(Specs, Error)) { return false; }
    for (const FUIImageSpec& Spec : Specs)
    {
        UPackage* Package = CreatePackage(*Spec.Package);
        Package->FullyLoad();
        auto* Factory = NewObject<UTextureFactory>(GetTransientPackage());
        Factory->AddToRoot();
        UTextureFactory::SuppressImportOverwriteDialog();
        bool bCancelled = false;
        UObject* Imported = UFactory::StaticImportObject(UTexture2D::StaticClass(), Package, FName(*FPackageName::GetShortName(Spec.Package)),
            RF_Public | RF_Standalone, bCancelled, *UIAbsolute(Spec.Path), nullptr, Factory);
        Factory->RemoveFromRoot();
        auto* Texture = Cast<UTexture2D>(Imported);
        if (!Texture || bCancelled) { return UIFail(Error, TEXT("UI texture import failed: ") + Spec.Path); }
        UIApplySettings(*Texture);
        Texture->PostEditChange();
        Texture->MarkPackageDirty();
        const FString File = FPackageName::LongPackageNameToFilename(Spec.Package, FPackageName::GetAssetPackageExtension());
        if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true)) { return UIFail(Error, TEXT("Cannot create the UI texture folder: ") + File); }
        FSavePackageArgs Args;
        Args.TopLevelFlags = RF_Public | RF_Standalone;
        if (!UPackage::SavePackage(Package, Texture, *File, Args)) { return UIFail(Error, TEXT("UI texture could not be saved: ") + File); }
        UE_LOG(LogLDEditor, Display, TEXT("LD_DEMO_UI_IMAGE_IMPORTED %s <- SourceData/UI/%s"), *Spec.Package, *Spec.Path);
    }
    Error.Reset();
    return true;
}

bool LDDemoUIImageImport::Validate(FString& Error)
{
    TArray<FUIImageSpec> Specs;
    if (!UILoadManifest(Specs, Error)) { return false; }
    for (const FUIImageSpec& Spec : Specs)
    {
        const UTexture2D* Texture = UILoad(Spec);
        if (!Texture) { return UIFail(Error, Spec.Package + TEXT(" is missing; run ImportDemoArt.ps1 -Mode ImportBackgrounds.")); }
        if (!UISettingsMatch(*Texture)) { return UIFail(Error, Spec.Package + TEXT(" lost its UI texture settings; run ImportDemoArt.ps1 -Mode ImportBackgrounds.")); }
        if (Texture->Source.GetSizeX() != Spec.Width || Texture->Source.GetSizeY() != Spec.Height)
        {
            return UIFail(Error, FString::Printf(TEXT("%s is %lldx%lld, the manifest says %dx%d."), *Spec.Package,
                Texture->Source.GetSizeX(), Texture->Source.GetSizeY(), Spec.Width, Spec.Height));
        }
        const UAssetImportData* Data = Texture->AssetImportData;
        const FMD5Hash Current = FMD5Hash::HashFile(*UIAbsolute(Spec.Path));
        if (!Data || Data->SourceData.SourceFiles.IsEmpty() || !Current.IsValid() || Data->SourceData.SourceFiles[0].FileHash != Current
            || !FPaths::GetCleanFilename(Data->SourceData.SourceFiles[0].RelativeFilename).Equals(Spec.Path, ESearchCase::CaseSensitive))
        {
            return UIFail(Error, Spec.Package + TEXT(" was not imported from the current bytes of SourceData/UI/") + Spec.Path
                + TEXT("; run ImportDemoArt.ps1 -Mode ImportBackgrounds."));
        }
    }
    Error.Reset();
    return true;
}
