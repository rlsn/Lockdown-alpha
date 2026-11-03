#include "LDDemoAudioImport.h"
#include "Audio/LDAudioCatalog.h"
#include "Audio/LDAudioTypes.h"
#include "Framework/LDDemoEntry.h"
#include "Dom/JsonObject.h"
#include "EditorFramework/AssetImportData.h"
#include "Factories/SoundFactory.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "Sound/SoundWave.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
    const TCHAR* const AudioMasterPackage = TEXT("/Game/Lockdown/Audio/SC_DemoMaster");
    const TCHAR* const AudioMixPackage = TEXT("/Game/Lockdown/Audio/SMix_DemoUser");
    const TCHAR* const AudioCatalogPackage = TEXT("/Game/Lockdown/Audio/DA_DemoAudio");
    const TCHAR* const AudioCuePackageRoot = TEXT("/Game/Lockdown/Audio/Cues/SW_");

    struct FAudioManifestCue
    {
        ELDAudioCue Cue = ELDAudioCue::None;
        FString Source;
        // Loudness-normalized playback volume measured by Scripts/MeasureDemoAudioLoudness.py; the source bytes stay unchanged.
        float Volume = 1.f;
    };

    bool AudioFail(FString& Error, const FString& Message) { Error = Message; return false; }

    FString AudioSourceRoot() { return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("SourceData"), TEXT("Audio"))); }

    bool AudioSafeRelative(const FString& Path)
    {
        return !Path.IsEmpty() && !Path.Contains(TEXT("\\")) && !Path.Contains(TEXT("..")) && !Path.StartsWith(TEXT("/"))
            && !Path.Contains(TEXT(":"));
    }

    bool AudioCheckHash(const FString& Relative, const FString& Sha256, FString& Error)
    {
        FString HashError;
        return LDDemoEntry::VerifyFileHash(FPaths::Combine(AudioSourceRoot(), Relative), Sha256, HashError)
            || AudioFail(Error, FString::Printf(TEXT("%s is missing or differs from its manifest SHA-256."), *Relative));
    }

    // The manifest is the only audio source list: CC0 packs proven by their own license text, one hashed file per cue.
    bool AudioLoadManifest(TArray<FAudioManifestCue>& Out, FString& Error)
    {
        Out.Reset();
        const FString Root = AudioSourceRoot();
        FString Text;
        if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(Root, TEXT("audio_cues.json"))))
        { return AudioFail(Error, TEXT("SourceData/Audio/audio_cues.json is missing.")); }
        TSharedPtr<FJsonObject> Json;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid())
        { return AudioFail(Error, TEXT("audio_cues.json is not valid JSON.")); }
        int32 Schema = 0;
        const TArray<TSharedPtr<FJsonValue>>* Packs = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* Cues = nullptr;
        if (!Json->TryGetNumberField(TEXT("schema"), Schema) || Schema != 1 || !Json->TryGetArrayField(TEXT("packs"), Packs)
            || !Json->TryGetArrayField(TEXT("cues"), Cues) || Packs->IsEmpty())
        { return AudioFail(Error, TEXT("audio_cues.json needs schema 1, packs and cues.")); }
        TSet<FString> Listed, PackIds;
        for (const TSharedPtr<FJsonValue>& Value : *Packs)
        {
            const TSharedPtr<FJsonObject>* Pack = nullptr;
            FString Id, License, LicenseFile, LicenseHash, Url;
            if (!Value->TryGetObject(Pack) || !(*Pack)->TryGetStringField(TEXT("id"), Id) || Id.IsEmpty() || PackIds.Contains(Id)
                || !(*Pack)->TryGetStringField(TEXT("license"), License) || !(*Pack)->TryGetStringField(TEXT("download_url"), Url)
                || !(*Pack)->TryGetStringField(TEXT("license_file"), LicenseFile) || !(*Pack)->TryGetStringField(TEXT("license_sha256"), LicenseHash)
                || !AudioSafeRelative(LicenseFile) || !Url.StartsWith(TEXT("https://")))
            { return AudioFail(Error, TEXT("Every audio pack needs a unique id, https download_url, license, license_file and license_sha256.")); }
            if (!License.Contains(TEXT("CC0"))) { return AudioFail(Error, FString::Printf(TEXT("Pack %s is not declared CC0."), *Id)); }
            if (!AudioCheckHash(LicenseFile, LicenseHash, Error)) { return false; }
            FString LicenseText;
            FFileHelper::LoadFileToString(LicenseText, *FPaths::Combine(Root, LicenseFile));
            if (!LicenseText.Contains(TEXT("CC0")) && !LicenseText.Contains(TEXT("Creative Commons Zero")))
            { return AudioFail(Error, FString::Printf(TEXT("The license file of pack %s does not state CC0."), *Id)); }
            PackIds.Add(Id);
            Listed.Add(LicenseFile);
        }
        if (Cues->Num() != LDAudio::CueCount)
        { return AudioFail(Error, FString::Printf(TEXT("audio_cues.json lists %d cues; the catalog needs %d."), Cues->Num(), LDAudio::CueCount)); }
        for (int32 Index = 0; Index < Cues->Num(); ++Index)
        {
            const TSharedPtr<FJsonObject>* Entry = nullptr;
            FAudioManifestCue Cue;
            FString Id, Pack, Hash;
            double Volume = 0;
            const ELDAudioCue Expected = ELDAudioCue(Index + 1);
            if (!(*Cues)[Index]->TryGetObject(Entry) || !(*Entry)->TryGetStringField(TEXT("cue"), Id) || !LDAudio::ParseCueId(Id, Cue.Cue)
                || Cue.Cue != Expected || !(*Entry)->TryGetStringField(TEXT("pack"), Pack) || !PackIds.Contains(Pack)
                || !(*Entry)->TryGetStringField(TEXT("source"), Cue.Source) || !(*Entry)->TryGetStringField(TEXT("sha256"), Hash)
                || !AudioSafeRelative(Cue.Source) || Listed.Contains(Cue.Source)
                || !(Cue.Source.EndsWith(TEXT(".ogg")) || Cue.Source.EndsWith(TEXT(".wav")))
                || !(*Entry)->TryGetNumberField(TEXT("volume"), Volume) || !FMath::IsFinite(Volume) || Volume < 0.1 || Volume > 2.0)
            { return AudioFail(Error, FString::Printf(TEXT("Cue entry %d must be %s with a declared pack, a unique .ogg/.wav source and a 0.1..2 volume."), Index, *LDAudio::CueId(Expected))); }
            Cue.Volume = float(Volume);
            if (!AudioCheckHash(Cue.Source, Hash, Error)) { return false; }
            Listed.Add(Cue.Source);
            Out.Add(MoveTemp(Cue));
        }
        // Only selected files ship: anything else under SourceData\Audio is an unlisted third-party file.
        TArray<FString> Files;
        IFileManager::Get().FindFilesRecursive(Files, *Root, TEXT("*"), true, false);
        for (const FString& File : Files)
        {
            FString Relative = File;
            FPaths::MakePathRelativeTo(Relative, *(Root + TEXT("/")));
            Relative.ReplaceInline(TEXT("\\"), TEXT("/"));
            if (Relative != TEXT("audio_cues.json") && !Listed.Contains(Relative))
            { return AudioFail(Error, TEXT("Unlisted file under SourceData/Audio: ") + Relative); }
        }
        Error.Reset();
        return true;
    }

    template<typename T>
    T* AudioLoadOrCreate(const TCHAR* PackageName, bool bCreate)
    {
        const FString Name = FPackageName::GetShortName(PackageName);
        if (FPackageName::DoesPackageExist(PackageName)) { return LoadObject<T>(nullptr, *(FString(PackageName) + TEXT(".") + Name)); }
        return bCreate ? NewObject<T>(CreatePackage(PackageName), *Name, RF_Public | RF_Standalone) : nullptr;
    }

    bool AudioSaveAsset(UObject& Asset, FString& Error)
    {
        const FString File = FPackageName::LongPackageNameToFilename(Asset.GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
        if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true)) { return AudioFail(Error, TEXT("Cannot create audio asset directory: ") + File); }
        FSavePackageArgs Args;
        Args.TopLevelFlags = RF_Public | RF_Standalone;
        return UPackage::SavePackage(Asset.GetOutermost(), &Asset, *File, Args) || AudioFail(Error, TEXT("Audio asset could not be saved: ") + File);
    }

    bool AudioImportedFromCurrentBytes(const USoundWave& Wave, const FString& Source, FString& Error)
    {
        const FString Absolute = FPaths::Combine(AudioSourceRoot(), Source);
        const UAssetImportData* ImportData = Wave.AssetImportData;
        if (!ImportData || ImportData->SourceData.SourceFiles.Num() != 1)
        { return AudioFail(Error, Wave.GetPathName() + TEXT(" has no single import source; run ImportDemoAudio.ps1 -Mode Import.")); }
        const FMD5Hash Current = FMD5Hash::HashFile(*Absolute);
        const auto& Imported = ImportData->SourceData.SourceFiles[0];
        if (!Current.IsValid() || Imported.FileHash != Current
            || !FPaths::GetCleanFilename(Imported.RelativeFilename).Equals(FPaths::GetCleanFilename(Source), ESearchCase::CaseSensitive))
        { return AudioFail(Error, Wave.GetPathName() + TEXT(" was not imported from the current bytes of ") + Source + TEXT("; run ImportDemoAudio.ps1 -Mode Import.")); }
        return true;
    }
}

bool LDDemoAudioImport::Import(FString& Error)
{
    TArray<FAudioManifestCue> Cues;
    if (!AudioLoadManifest(Cues, Error)) { return false; }
    auto* Master = AudioLoadOrCreate<USoundClass>(AudioMasterPackage, true);
    auto* Mix = AudioLoadOrCreate<USoundMix>(AudioMixPackage, true);
    auto* Catalog = AudioLoadOrCreate<ULDAudioCatalog>(AudioCatalogPackage, true);
    if (!Master || !Mix || !Catalog) { return AudioFail(Error, TEXT("An existing audio master class, user mix or catalog cannot be loaded.")); }
    // The user volume is applied at runtime as a class override; the asset itself carries no adjuster or EQ.
    Mix->bApplyEQ = false;
    Mix->SoundClassEffects.Reset();
    Mix->InitialDelay = 0.f;
    Mix->FadeInTime = 0.f;
    Mix->FadeOutTime = 0.f;
    Mix->Duration = -1.f;
    Master->MarkPackageDirty();
    Mix->MarkPackageDirty();
    auto* Factory = NewObject<USoundFactory>(GetTransientPackage());
    Factory->AddToRoot();
    Catalog->MasterClass = Master;
    Catalog->UserMix = Mix;
    Catalog->Cues.Reset();
    TArray<UObject*> Assets = { Master, Mix };
    for (const FAudioManifestCue& Cue : Cues)
    {
        const FString Name = TEXT("SW_") + LDAudio::CueId(Cue.Cue);
        UPackage* Package = CreatePackage(*(FString(AudioCuePackageRoot) + LDAudio::CueId(Cue.Cue)));
        Package->FullyLoad();
        Factory->bAutoCreateCue = false;
        // Re-imports keep the existing asset and replace its audio without an overwrite prompt.
        Factory->SuppressImportDialogs();
        bool bCancelled = false;
        UObject* Imported = UFactory::StaticImportObject(USoundWave::StaticClass(), Package, FName(*Name), RF_Public | RF_Standalone,
            bCancelled, *FPaths::Combine(AudioSourceRoot(), Cue.Source), nullptr, Factory);
        auto* Wave = Cast<USoundWave>(Imported);
        if (!Wave || bCancelled)
        {
            Factory->RemoveFromRoot();
            return AudioFail(Error, TEXT("Sound import failed: ") + Cue.Source);
        }
        Wave->SoundClassObject = Master;
        Wave->bLooping = false;
        Wave->AttenuationSettings = nullptr;
        Wave->Volume = Cue.Volume;
        Wave->MarkPackageDirty();
        FLDAudioCueEntry& Entry = Catalog->Cues.AddDefaulted_GetRef();
        Entry.Cue = Cue.Cue;
        Entry.Sound = Wave;
        Assets.Add(Wave);
    }
    Factory->RemoveFromRoot();
    TArray<USoundWave*> Resolved;
    if (!Catalog->Resolve(Resolved, Error)) { return AudioFail(Error, TEXT("Imported audio catalog is invalid: ") + Error); }
    Catalog->MarkPackageDirty();
    Assets.Add(Catalog);
    for (UObject* Asset : Assets) { if (!AudioSaveAsset(*Asset, Error)) { return false; } }
    Error.Reset();
    return true;
}

bool LDDemoAudioImport::Validate(FString& Error)
{
    TArray<FAudioManifestCue> Cues;
    if (!AudioLoadManifest(Cues, Error)) { return false; }
    const auto* Catalog = AudioLoadOrCreate<ULDAudioCatalog>(AudioCatalogPackage, false);
    if (!Catalog) { return AudioFail(Error, TEXT("Audio catalog DA_DemoAudio is missing; run ImportDemoAudio.ps1 -Mode Import.")); }
    TArray<USoundWave*> Resolved;
    if (!Catalog->Resolve(Resolved, Error)) { return AudioFail(Error, TEXT("Audio catalog is invalid: ") + Error); }
    if (Catalog->MasterClass->GetPathName() != ULDAudioCatalog::MasterClassPath || Catalog->UserMix->GetPathName() != ULDAudioCatalog::UserMixPath)
    { return AudioFail(Error, TEXT("Audio catalog does not use the single SC_DemoMaster / SMix_DemoUser chain.")); }
    for (const FAudioManifestCue& Cue : Cues)
    {
        const USoundWave* Wave = Resolved[int32(Cue.Cue)];
        const FString Expected = FString(AudioCuePackageRoot) + LDAudio::CueId(Cue.Cue);
        if (Wave->GetOutermost()->GetName() != Expected)
        { return AudioFail(Error, FString::Printf(TEXT("Cue %s uses %s instead of %s."), *LDAudio::CueId(Cue.Cue), *Wave->GetPathName(), *Expected)); }
        if (!AudioImportedFromCurrentBytes(*Wave, Cue.Source, Error)) { return false; }
        if (!FMath::IsNearlyEqual(Wave->Volume, Cue.Volume, 1e-4f))
        { return AudioFail(Error, FString::Printf(TEXT("Cue %s plays at volume %.3f, the manifest says %.3f; run ImportDemoAudio.ps1 -Mode Import."), *LDAudio::CueId(Cue.Cue), Wave->Volume, Cue.Volume)); }
    }
    Error.Reset();
    return true;
}
