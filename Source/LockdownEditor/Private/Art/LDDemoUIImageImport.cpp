#include "LDDemoUIImageImport.h"
#include "LDEditorLog.h"
#include "UI/LDUIBackgrounds.h"
#include "UI/LDUIFonts.h"
#include "UI/LDUIIcons.h"
#include "Framework/LDDemoEntry.h"
#include "Dom/JsonObject.h"
#include "EditorFramework/AssetImportData.h"
#include "Engine/FontFace.h"
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
        // Relative to SourceData/UI: a background's file name, or Icons/<id>.png.
        FString Path;
        FString Package;
        int32 Width = 0;
        int32 Height = 0;
        bool bIcon = false;
    };

    constexpr TCHAR IconFolder[] = TEXT("Icons");
    constexpr TCHAR FontFolder[] = TEXT("Fonts");

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

    bool UILoadIcons(TArray<FUIImageSpec>& Out, FString& Error);

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
            if (File != TEXT("ui_images.json") && File != TEXT("ui_icons.json") && File != TEXT("ui_fonts.json") && !Listed.Contains(File))
            {
                return UIFail(Error, TEXT("Unlisted file under SourceData/UI: ") + File);
            }
        }
        TArray<FString> Folders;
        IFileManager::Get().FindFiles(Folders, *(UISourceRoot() / TEXT("*")), false, true);
        for (const FString& Folder : Folders)
        {
            if (Folder != IconFolder && Folder != FontFolder) { return UIFail(Error, TEXT("SourceData/UI holds only the Icons and Fonts folders: ") + Folder); }
        }
        return UILoadIcons(Out, Error);
    }

    // #85 icons: SourceData/UI/ui_icons.json lists every PNG of SourceData/UI/Icons by icon id, each imported to
    // LDUIIcons::PackageFor(id). Which ids the HUD needs is checked against the approved catalog by Lockdown.Rules.HUDIcons.
    bool UILoadIcons(TArray<FUIImageSpec>& Out, FString& Error)
    {
        FString Text;
        if (!FFileHelper::LoadFileToString(Text, *UIAbsolute(TEXT("ui_icons.json")))) { return UIFail(Error, TEXT("SourceData/UI/ui_icons.json is missing.")); }
        TSharedPtr<FJsonObject> Json;
        int32 Schema = 0;
        FString Folder, Provenance;
        const TArray<TSharedPtr<FJsonValue>>* Icons = nullptr;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid()
            || !Json->TryGetNumberField(TEXT("schema"), Schema) || Schema != 1 || !Json->TryGetStringField(TEXT("folder"), Folder)
            || Folder != IconFolder || !Json->TryGetStringField(TEXT("provenance"), Provenance) || Provenance.IsEmpty()
            || !Json->TryGetArrayField(TEXT("icons"), Icons) || Icons->IsEmpty())
        {
            return UIFail(Error, TEXT("ui_icons.json must be valid JSON with schema 1, folder Icons, a provenance and an icons list."));
        }
        TSet<FString> Listed;
        for (const TSharedPtr<FJsonValue>& Value : *Icons)
        {
            const TSharedPtr<FJsonObject>* Object = nullptr;
            FUIImageSpec Spec;
            FString Hash;
            Spec.bIcon = true;
            if (!Value->TryGetObject(Object) || !(*Object)->TryGetStringField(TEXT("id"), Spec.Id) || Listed.Contains(Spec.Id)
                || !(*Object)->TryGetStringField(TEXT("sha256"), Hash) || !(*Object)->TryGetNumberField(TEXT("width"), Spec.Width)
                || !(*Object)->TryGetNumberField(TEXT("height"), Spec.Height) || Spec.Width <= 0 || Spec.Height <= 0)
            {
                return UIFail(Error, TEXT("Every icon needs a unique id, sha256, width and height."));
            }
            Spec.Package = LDUIIcons::PackageFor(Spec.Id);
            if (Spec.Package.IsEmpty()) { return UIFail(Error, TEXT("ui_icons.json lists an id that is not icon_<lower-case identifier>: ") + Spec.Id); }
            Spec.Path = FString(IconFolder) / (Spec.Id + TEXT(".png"));
            FString HashError;
            if (!LDDemoEntry::VerifyFileHash(UIAbsolute(Spec.Path), Hash, HashError))
            {
                return UIFail(Error, FString::Printf(TEXT("SourceData/UI/%s is missing or differs from its manifest SHA-256."), *Spec.Path));
            }
            Listed.Add(Spec.Id);
            Out.Add(MoveTemp(Spec));
        }
        TArray<FString> Found;
        IFileManager::Get().FindFiles(Found, *(UISourceRoot() / IconFolder / TEXT("*")), true, false);
        for (const FString& File : Found)
        {
            if (!File.EndsWith(TEXT(".png"), ESearchCase::CaseSensitive) || !Listed.Contains(FPaths::GetBaseFilename(File)))
            {
                return UIFail(Error, TEXT("Unlisted file under SourceData/UI/Icons: ") + File);
            }
        }
        TArray<FString> Folders;
        IFileManager::Get().FindFiles(Folders, *(UISourceRoot() / IconFolder / TEXT("*")), false, true);
        if (!Folders.IsEmpty()) { return UIFail(Error, TEXT("SourceData/UI/Icons holds no folders: ") + Folders[0]); }
        Error.Reset();
        return true;
    }

    // Full-screen pictures: sharp at any resolution (no mips, high-quality compression), never streamed out. Icons are
    // small enough to stay uncompressed ("UserInterface2D (RGBA)"), so their painted edges keep every pixel.
    TextureCompressionSettings UICompression(const FUIImageSpec& Spec) { return Spec.bIcon ? TC_EditorIcon : TC_BC7; }

    void UIApplySettings(UTexture2D& Texture, const FUIImageSpec& Spec)
    {
        Texture.LODGroup = TEXTUREGROUP_UI;
        Texture.MipGenSettings = TMGS_NoMipmaps;
        Texture.CompressionSettings = UICompression(Spec);
        Texture.SRGB = true;
        Texture.NeverStream = true;
    }

    bool UISettingsMatch(const UTexture2D& Texture, const FUIImageSpec& Spec)
    {
        return Texture.LODGroup == TEXTUREGROUP_UI && Texture.MipGenSettings == TMGS_NoMipmaps
            && Texture.CompressionSettings == UICompression(Spec) && Texture.SRGB && Texture.NeverStream;
    }

    UTexture2D* UILoad(const FUIImageSpec& Spec)
    {
        return FPackageName::DoesPackageExist(Spec.Package)
            ? LoadObject<UTexture2D>(nullptr, *(Spec.Package + TEXT(".") + FPackageName::GetShortName(Spec.Package))) : nullptr;
    }

    // Saved icon texture packages that ui_icons.json no longer lists (an icon retired from the manifest).
    TArray<FString> UIStaleIconFiles(const TArray<FUIImageSpec>& Specs)
    {
        const FString Extension = FPackageName::GetAssetPackageExtension();
        const FString Directory = FPaths::GetPath(FPackageName::LongPackageNameToFilename(FString(LDUIIcons::Folder) / TEXT("T_icon"), Extension));
        TSet<FString> Listed;
        for (const FUIImageSpec& Spec : Specs) { if (Spec.bIcon) { Listed.Add(FPackageName::GetShortName(Spec.Package)); } }
        TArray<FString> Found;
        IFileManager::Get().FindFiles(Found, *(Directory / (TEXT("*") + Extension)), true, false);
        TArray<FString> Out;
        for (const FString& File : Found)
        {
            if (!Listed.Contains(FPaths::GetBaseFilename(File))) { Out.Add(Directory / File); }
        }
        Out.Sort();
        return Out;
    }
    struct FUIFontSpec
    {
        FString Typeface;
        // Relative to SourceData/UI: Fonts/<file>.otf.
        FString Path;
        FString Package;
    };

    // SourceData/UI/ui_fonts.json lists exactly the typefaces LDUIFonts composes, each one .otf of SourceData/UI/Fonts with
    // its SHA-256, plus the licence file the fonts ship with; the folder holds nothing else.
    bool UILoadFonts(TArray<FUIFontSpec>& Out, FString& Error)
    {
        Out.Reset();
        FString Text;
        if (!FFileHelper::LoadFileToString(Text, *UIAbsolute(TEXT("ui_fonts.json")))) { return UIFail(Error, TEXT("SourceData/UI/ui_fonts.json is missing.")); }
        TSharedPtr<FJsonObject> Json;
        int32 Schema = 0;
        FString Folder, Family, Provenance, License, LicenseFile, LicenseHash;
        const TArray<TSharedPtr<FJsonValue>>* Fonts = nullptr;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid()
            || !Json->TryGetNumberField(TEXT("schema"), Schema) || Schema != 1 || !Json->TryGetStringField(TEXT("folder"), Folder)
            || Folder != FontFolder || !Json->TryGetStringField(TEXT("family"), Family) || Family.IsEmpty()
            || !Json->TryGetStringField(TEXT("provenance"), Provenance) || Provenance.IsEmpty()
            || !Json->TryGetStringField(TEXT("license"), License) || License.IsEmpty()
            || !Json->TryGetStringField(TEXT("license_file"), LicenseFile) || LicenseFile.IsEmpty() || LicenseFile.Contains(TEXT("/"))
            || LicenseFile.Contains(TEXT("\\")) || LicenseFile.Contains(TEXT("..")) || !Json->TryGetStringField(TEXT("license_sha256"), LicenseHash)
            || !Json->TryGetArrayField(TEXT("fonts"), Fonts))
        {
            return UIFail(Error, TEXT("ui_fonts.json must be valid JSON with schema 1, folder Fonts, family, provenance, license, license_file, license_sha256 and a fonts list."));
        }
        FString HashError;
        if (!LDDemoEntry::VerifyFileHash(UIAbsolute(FString(FontFolder) / LicenseFile), LicenseHash, HashError))
        {
            return UIFail(Error, FString::Printf(TEXT("SourceData/UI/Fonts/%s is missing or differs from its manifest SHA-256."), *LicenseFile));
        }
        TSet<FString> Typefaces, Listed;
        Listed.Add(LicenseFile);
        for (const TSharedPtr<FJsonValue>& Value : *Fonts)
        {
            const TSharedPtr<FJsonObject>* Object = nullptr;
            FUIFontSpec Spec;
            FString File, Hash;
            if (!Value->TryGetObject(Object) || !(*Object)->TryGetStringField(TEXT("typeface"), Spec.Typeface) || Typefaces.Contains(Spec.Typeface)
                || !(*Object)->TryGetStringField(TEXT("file"), File) || File.Contains(TEXT("/")) || File.Contains(TEXT("\\")) || File.Contains(TEXT(".."))
                || !File.EndsWith(TEXT(".otf"), ESearchCase::CaseSensitive) || Listed.Contains(File) || !(*Object)->TryGetStringField(TEXT("sha256"), Hash))
            {
                return UIFail(Error, TEXT("Every font needs a unique typeface, a unique .otf file name and sha256."));
            }
            Spec.Package = LDUIFonts::PackageFor(Spec.Typeface);
            if (Spec.Package.IsEmpty()) { return UIFail(Error, TEXT("ui_fonts.json lists a typeface the interface font does not use: ") + Spec.Typeface); }
            Spec.Path = FString(FontFolder) / File;
            if (!LDDemoEntry::VerifyFileHash(UIAbsolute(Spec.Path), Hash, HashError))
            {
                return UIFail(Error, FString::Printf(TEXT("SourceData/UI/%s is missing or differs from its manifest SHA-256."), *Spec.Path));
            }
            Typefaces.Add(Spec.Typeface);
            Listed.Add(File);
            Out.Add(MoveTemp(Spec));
        }
        for (const TCHAR* Typeface : LDUIFonts::Typefaces)
        {
            if (!Typefaces.Contains(Typeface)) { return UIFail(Error, FString(TEXT("ui_fonts.json does not list the typeface ")) + Typeface); }
        }
        TArray<FString> Found;
        IFileManager::Get().FindFiles(Found, *(UISourceRoot() / FontFolder / TEXT("*")), true, false);
        for (const FString& File : Found)
        {
            if (!Listed.Contains(File)) { return UIFail(Error, TEXT("Unlisted file under SourceData/UI/Fonts: ") + File); }
        }
        TArray<FString> Folders;
        IFileManager::Get().FindFiles(Folders, *(UISourceRoot() / FontFolder / TEXT("*")), false, true);
        if (!Folders.IsEmpty()) { return UIFail(Error, TEXT("SourceData/UI/Fonts holds no folders: ") + Folders[0]); }
        Error.Reset();
        return true;
    }

    UFontFace* UILoadFace(const FUIFontSpec& Spec)
    {
        return FPackageName::DoesPackageExist(Spec.Package)
            ? LoadObject<UFontFace>(nullptr, *(Spec.Package + TEXT(".") + FPackageName::GetShortName(Spec.Package))) : nullptr;
    }

    // The face holds exactly the bytes of its source file.
    bool UIFaceMatches(const UFontFace& Face, const TArray<uint8>& Bytes)
    {
        return Face.GetFontFaceData()->GetData() == Bytes;
    }

    bool UIImportFonts(FString& Error)
    {
        TArray<FUIFontSpec> Specs;
        if (!UILoadFonts(Specs, Error)) { return false; }
        for (const FUIFontSpec& Spec : Specs)
        {
            TArray<uint8> Bytes;
            if (!FFileHelper::LoadFileToArray(Bytes, *UIAbsolute(Spec.Path))) { return UIFail(Error, TEXT("Cannot read SourceData/UI/") + Spec.Path); }
            if (const UFontFace* Existing = UILoadFace(Spec); Existing && UIFaceMatches(*Existing, Bytes)) { continue; }
            UPackage* Package = CreatePackage(*Spec.Package);
            Package->FullyLoad();
            const FName Name(*FPackageName::GetShortName(Spec.Package));
            UFontFace* Face = FindObject<UFontFace>(Package, *Name.ToString());
            if (!Face) { Face = NewObject<UFontFace>(Package, Name, RF_Public | RF_Standalone); }
            Face->InitializeFromBulkData(UIAbsolute(Spec.Path), EFontHinting::Default, Bytes.GetData(), Bytes.Num());
            // No PostEditChange: it flushes the Slate font cache, which a commandlet does not have.
            Face->SourceFilename = FPaths::GetCleanFilename(Spec.Path);
            Face->MarkPackageDirty();
            const FString File = FPackageName::LongPackageNameToFilename(Spec.Package, FPackageName::GetAssetPackageExtension());
            if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true)) { return UIFail(Error, TEXT("Cannot create the UI font folder: ") + File); }
            FSavePackageArgs Args;
            Args.TopLevelFlags = RF_Public | RF_Standalone;
            if (!UPackage::SavePackage(Package, Face, *File, Args)) { return UIFail(Error, TEXT("UI font face could not be saved: ") + File); }
            UE_LOG(LogLDEditor, Display, TEXT("LD_DEMO_UI_FONT_IMPORTED %s <- SourceData/UI/%s"), *Spec.Package, *Spec.Path);
        }
        Error.Reset();
        return true;
    }

    bool UIValidateFonts(FString& Error)
    {
        TArray<FUIFontSpec> Specs;
        if (!UILoadFonts(Specs, Error)) { return false; }
        for (const FUIFontSpec& Spec : Specs)
        {
            const UFontFace* Face = UILoadFace(Spec);
            if (!Face) { return UIFail(Error, Spec.Package + TEXT(" is missing; run ImportDemoArt.ps1 -Mode ImportUI.")); }
            TArray<uint8> Bytes;
            if (!FFileHelper::LoadFileToArray(Bytes, *UIAbsolute(Spec.Path)) || !UIFaceMatches(*Face, Bytes))
            {
                return UIFail(Error, Spec.Package + TEXT(" does not hold the current bytes of SourceData/UI/") + Spec.Path
                    + TEXT("; run ImportDemoArt.ps1 -Mode ImportUI."));
            }
        }
        Error.Reset();
        return true;
    }
}

bool LDDemoUIImageImport::Import(FString& Error)
{
    TArray<FUIImageSpec> Specs;
    if (!UILoadManifest(Specs, Error)) { return false; }
    for (const FUIImageSpec& Spec : Specs)
    {
        if(const UTexture2D* Existing=UILoad(Spec);Existing&&UISettingsMatch(*Existing,Spec)
            &&Existing->Source.GetSizeX()==Spec.Width&&Existing->Source.GetSizeY()==Spec.Height)
        {
            const UAssetImportData* Data=Existing->AssetImportData;
            const FMD5Hash Current=FMD5Hash::HashFile(*UIAbsolute(Spec.Path));
            if(Data&&!Data->SourceData.SourceFiles.IsEmpty()&&Current.IsValid()
                &&Data->SourceData.SourceFiles[0].FileHash==Current
                &&FPaths::GetCleanFilename(Data->SourceData.SourceFiles[0].RelativeFilename)
                    .Equals(FPaths::GetCleanFilename(Spec.Path),ESearchCase::CaseSensitive))
            {continue;}
        }
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
        UIApplySettings(*Texture, Spec);
        Texture->PostEditChange();
        Texture->MarkPackageDirty();
        const FString File = FPackageName::LongPackageNameToFilename(Spec.Package, FPackageName::GetAssetPackageExtension());
        if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true)) { return UIFail(Error, TEXT("Cannot create the UI texture folder: ") + File); }
        FSavePackageArgs Args;
        Args.TopLevelFlags = RF_Public | RF_Standalone;
        if (!UPackage::SavePackage(Package, Texture, *File, Args)) { return UIFail(Error, TEXT("UI texture could not be saved: ") + File); }
        UE_LOG(LogLDEditor, Display, TEXT("LD_DEMO_UI_IMAGE_IMPORTED %s <- SourceData/UI/%s"), *Spec.Package, *Spec.Path);
    }
    for (const FString& Stale : UIStaleIconFiles(Specs))
    {
        if (!IFileManager::Get().Delete(*Stale, true, true))
        {
            return UIFail(Error, TEXT("A UI icon texture no longer listed in ui_icons.json could not be deleted: ") + Stale);
        }
        UE_LOG(LogLDEditor, Display, TEXT("LD_DEMO_UI_IMAGE_DELETED %s (not listed in ui_icons.json)"), *Stale);
    }
    return UIImportFonts(Error);
}

bool LDDemoUIImageImport::Validate(FString& Error)
{
    TArray<FUIImageSpec> Specs;
    if (!UILoadManifest(Specs, Error)) { return false; }
    for (const FUIImageSpec& Spec : Specs)
    {
        const UTexture2D* Texture = UILoad(Spec);
        if (!Texture) { return UIFail(Error, Spec.Package + TEXT(" is missing; run ImportDemoArt.ps1 -Mode ImportUI.")); }
        if (!UISettingsMatch(*Texture, Spec)) { return UIFail(Error, Spec.Package + TEXT(" lost its UI texture settings; run ImportDemoArt.ps1 -Mode ImportUI.")); }
        if (Texture->Source.GetSizeX() != Spec.Width || Texture->Source.GetSizeY() != Spec.Height)
        {
            return UIFail(Error, FString::Printf(TEXT("%s is %lldx%lld, the manifest says %dx%d."), *Spec.Package,
                Texture->Source.GetSizeX(), Texture->Source.GetSizeY(), Spec.Width, Spec.Height));
        }
        const UAssetImportData* Data = Texture->AssetImportData;
        const FMD5Hash Current = FMD5Hash::HashFile(*UIAbsolute(Spec.Path));
        if (!Data || Data->SourceData.SourceFiles.IsEmpty() || !Current.IsValid() || Data->SourceData.SourceFiles[0].FileHash != Current
            || !FPaths::GetCleanFilename(Data->SourceData.SourceFiles[0].RelativeFilename)
                .Equals(FPaths::GetCleanFilename(Spec.Path), ESearchCase::CaseSensitive))
        {
            return UIFail(Error, Spec.Package + TEXT(" was not imported from the current bytes of SourceData/UI/") + Spec.Path
                + TEXT("; run ImportDemoArt.ps1 -Mode ImportUI."));
        }
    }
    const TArray<FString> Stale = UIStaleIconFiles(Specs);
    if (!Stale.IsEmpty())
    {
        return UIFail(Error, Stale[0] + TEXT(" is a UI icon texture ui_icons.json no longer lists; run ImportDemoArt.ps1 -Mode ImportUI."));
    }
    return UIValidateFonts(Error);
}
