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
    const TCHAR* const AudioMusicClassPackage = TEXT("/Game/Lockdown/Audio/SC_DemoMusic");
    const TCHAR* const AudioSfxClassPackage = TEXT("/Game/Lockdown/Audio/SC_DemoSfx");
    const TCHAR* const AudioUIClassPackage = TEXT("/Game/Lockdown/Audio/SC_DemoUI");
    const TCHAR* const AudioMusicPackageRoot = TEXT("/Game/Lockdown/Audio/Music/SW_");
    const TCHAR* const AudioCatalogPackage = TEXT("/Game/Lockdown/Audio/DA_DemoAudio");
    const TCHAR* const AudioCuePackageRoot = TEXT("/Game/Lockdown/Audio/Cues/SW_");
    const TCHAR* const AudioSkillPackageRoot = TEXT("/Game/Lockdown/Audio/Skills/SW_");

    // One sound with its takes: the first source imports as SW_<Name>, every further take as SW_<Name>_<n>.
    struct FAudioManifestSound
    {
        FString Name;
        TArray<FString> Sources;
        // Loudness-normalized playback volume (Scripts/MeasureDemoAudioLoudness.py for CC0 files, the generator for
        // originals); the source bytes stay unchanged. Every take of a sound plays at it.
        float Volume = 1.f;
    };
    struct FAudioManifestCue : FAudioManifestSound { ELDAudioCue Cue = ELDAudioCue::None; };
    struct FAudioManifestSkill : FAudioManifestSound { uint8 Ability = 0; };

    // An owner-supplied original music track (#94): no third-party pack, so no CC0 license file; its provenance is recorded.
    struct FAudioManifestTrack
    {
        ELDMusicTrack Track = ELDMusicTrack::None;
        FString Source;
    };

    struct FAudioManifest
    {
        TArray<FAudioManifestCue> Cues;
        TArray<FAudioManifestSkill> Skills;
        TArray<FAudioManifestSkill> SkillHits;
        TArray<FAudioManifestTrack> Tracks;
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

    // The ability numbers of SourceData/Demo/native_ids.json by content id.
    bool AudioAbilityNumbers(TMap<FString, int32>& Out, FString& Error)
    {
        FString Text;
        TSharedPtr<FJsonObject> Json;
        const TSharedPtr<FJsonObject>* Abilities = nullptr;
        if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectDir(), TEXT("SourceData"), TEXT("Demo"), TEXT("native_ids.json")))
            || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid()
            || !Json->TryGetObjectField(TEXT("abilities"), Abilities))
        { return AudioFail(Error, TEXT("SourceData/Demo/native_ids.json with its abilities is required for the skill sounds.")); }
        for (const auto& Pair : (*Abilities)->Values)
        {
            double Number = 0;
            if (Pair.Value->TryGetNumber(Number)) { Out.Add(FString(Pair.Key), int32(Number)); }
        }
        return true;
    }

    // The manifest is the only audio source list: CC0 packs proven by their own license text, original generated sounds
    // proven by their hashed generator, one or more hashed takes per cue and skill (a take may reuse a file at its own
    // volume; each still imports its own SoundWave), and one hashed original file per music track with its provenance.
    bool AudioLoadManifest(FAudioManifest& Out, FString& Error)
    {
        Out = FAudioManifest();
        const FString Root = AudioSourceRoot();
        FString Text;
        if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(Root, TEXT("audio_cues.json"))))
        { return AudioFail(Error, TEXT("SourceData/Audio/audio_cues.json is missing.")); }
        TSharedPtr<FJsonObject> Json;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid())
        { return AudioFail(Error, TEXT("audio_cues.json is not valid JSON.")); }
        int32 Schema = 0;
        const TArray<TSharedPtr<FJsonValue>>* Packs = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* Originals = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* Cues = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* Skills = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* SkillHits = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* Music = nullptr;
        if (!Json->TryGetNumberField(TEXT("schema"), Schema) || Schema != 3 || !Json->TryGetArrayField(TEXT("packs"), Packs)
            || !Json->TryGetArrayField(TEXT("originals"), Originals) || !Json->TryGetArrayField(TEXT("cues"), Cues)
            || !Json->TryGetArrayField(TEXT("skills"), Skills) || !Json->TryGetArrayField(TEXT("skill_hits"), SkillHits)
            || !Json->TryGetArrayField(TEXT("music"), Music) || Packs->IsEmpty())
        { return AudioFail(Error, TEXT("audio_cues.json needs schema 3, packs, originals, cues, skills, skill_hits and music.")); }
        TSet<FString> Listed, PackIds, Reserved;
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
            Reserved.Add(LicenseFile);
        }
        // Original sounds made by the project's own generator script (no third-party content beyond the CC0 packs above).
        for (const TSharedPtr<FJsonValue>& Value : *Originals)
        {
            const TSharedPtr<FJsonObject>* Pack = nullptr;
            FString Id, Generator, GeneratorHash, License;
            if (!Value->TryGetObject(Pack) || !(*Pack)->TryGetStringField(TEXT("id"), Id) || Id.IsEmpty() || PackIds.Contains(Id)
                || !(*Pack)->TryGetStringField(TEXT("generator"), Generator) || !(*Pack)->TryGetStringField(TEXT("generator_sha256"), GeneratorHash)
                || !(*Pack)->TryGetStringField(TEXT("license"), License) || License.TrimStartAndEnd().IsEmpty() || !AudioSafeRelative(Generator))
            { return AudioFail(Error, TEXT("Every original sound set needs a unique id, its generator, the generator's sha256 and a license statement.")); }
            if (!AudioCheckHash(Generator, GeneratorHash, Error)) { return false; }
            PackIds.Add(Id);
            Listed.Add(Generator);
            Reserved.Add(Generator);
        }
        // One sound: a pack, one or more hashed .ogg/.wav takes ("takes", or a single "source" with its "sha256") and a volume.
        const auto ReadSound = [&](const TSharedPtr<FJsonObject>& Entry, FAudioManifestSound& Sound, const FString& What) -> bool
        {
            FString Pack;
            double Volume = 0;
            if (!Entry->TryGetStringField(TEXT("pack"), Pack) || !PackIds.Contains(Pack)
                || !Entry->TryGetNumberField(TEXT("volume"), Volume) || !FMath::IsFinite(Volume) || Volume < 0.1 || Volume > 2.0)
            { return AudioFail(Error, What + TEXT(" needs a declared pack and a 0.1..2 volume.")); }
            Sound.Volume = float(Volume);
            TArray<TPair<FString, FString>> Takes;
            const TArray<TSharedPtr<FJsonValue>>* TakeValues = nullptr;
            FString Source, Hash;
            if (Entry->TryGetArrayField(TEXT("takes"), TakeValues))
            {
                for (const TSharedPtr<FJsonValue>& TakeValue : *TakeValues)
                {
                    const TSharedPtr<FJsonObject>* Take = nullptr;
                    if (!TakeValue->TryGetObject(Take) || !(*Take)->TryGetStringField(TEXT("source"), Source) || !(*Take)->TryGetStringField(TEXT("sha256"), Hash))
                    { return AudioFail(Error, What + TEXT(" has a take without source and sha256.")); }
                    Takes.Emplace(Source, Hash);
                }
            }
            else if (Entry->TryGetStringField(TEXT("source"), Source) && Entry->TryGetStringField(TEXT("sha256"), Hash)) { Takes.Emplace(Source, Hash); }
            if (Takes.IsEmpty() || Takes.Num() > 8) { return AudioFail(Error, What + TEXT(" needs one to eight takes.")); }
            for (const TPair<FString, FString>& Take : Takes)
            {
                if (!AudioSafeRelative(Take.Key) || Reserved.Contains(Take.Key) || Take.Key.StartsWith(TEXT("Music/"))
                    || !(Take.Key.EndsWith(TEXT(".ogg")) || Take.Key.EndsWith(TEXT(".wav"))))
                { return AudioFail(Error, What + TEXT(" has a take that is not an .ogg/.wav sound file of its own: ") + Take.Key); }
                if (!AudioCheckHash(Take.Key, Take.Value, Error)) { return false; }
                Listed.Add(Take.Key);
                Sound.Sources.Add(Take.Key);
            }
            return true;
        };
        if (Cues->Num() != LDAudio::CueCount)
        { return AudioFail(Error, FString::Printf(TEXT("audio_cues.json lists %d cues; the catalog needs %d."), Cues->Num(), LDAudio::CueCount)); }
        for (int32 Index = 0; Index < Cues->Num(); ++Index)
        {
            const TSharedPtr<FJsonObject>* Entry = nullptr;
            FAudioManifestCue Cue;
            FString Id;
            const ELDAudioCue Expected = LDAudio::PlayableCue(Index);
            if (!(*Cues)[Index]->TryGetObject(Entry) || !(*Entry)->TryGetStringField(TEXT("cue"), Id) || !LDAudio::ParseCueId(Id, Cue.Cue)
                || Cue.Cue != Expected)
            { return AudioFail(Error, FString::Printf(TEXT("Cue entry %d must be %s."), Index, *LDAudio::CueId(Expected))); }
            Cue.Name = LDAudio::CueId(Cue.Cue);
            if (!ReadSound(*Entry, Cue, TEXT("Cue ") + Cue.Name)) { return false; }
            Out.Cues.Add(MoveTemp(Cue));
        }
        TMap<FString, int32> Abilities;
        if ((!Skills->IsEmpty() || !SkillHits->IsEmpty()) && !AudioAbilityNumbers(Abilities, Error)) { return false; }
        // A table of skill sounds by ability: releases are SW_<ability>[_n], hits SW_<ability>_hit[_n].
        const auto ReadSkills = [&](const TArray<TSharedPtr<FJsonValue>>& Table, const TCHAR* Suffix, TArray<FAudioManifestSkill>& Into) -> bool
        {
            int32 PreviousAbility = -1;
            for (const TSharedPtr<FJsonValue>& Value : Table)
            {
                const TSharedPtr<FJsonObject>* Entry = nullptr;
                FAudioManifestSkill Skill;
                FString Id;
                double Number = -1;
                const int32* Known = nullptr;
                if (!Value->TryGetObject(Entry) || !(*Entry)->TryGetStringField(TEXT("ability"), Id) || !(*Entry)->TryGetNumberField(TEXT("native_id"), Number)
                    || !(Known = Abilities.Find(Id)) || *Known != int32(Number) || *Known <= PreviousAbility || *Known > 255)
                { return AudioFail(Error, TEXT("Every skill sound needs an ability id with its native_id from native_ids.json, in ascending native_id order without repeats.")); }
                PreviousAbility = *Known;
                Skill.Ability = uint8(*Known);
                Skill.Name = Id + Suffix;
                if (!ReadSound(*Entry, Skill, TEXT("Skill ") + Skill.Name)) { return false; }
                Into.Add(MoveTemp(Skill));
            }
            return true;
        };
        if (!ReadSkills(*Skills, TEXT(""), Out.Skills) || !ReadSkills(*SkillHits, TEXT("_hit"), Out.SkillHits)) { return false; }
        if (Music->Num() != LDAudio::MusicTrackCount)
        { return AudioFail(Error, FString::Printf(TEXT("audio_cues.json lists %d music tracks; the catalog needs %d."), Music->Num(), LDAudio::MusicTrackCount)); }
        for (int32 Index = 0; Index < Music->Num(); ++Index)
        {
            const TSharedPtr<FJsonObject>* Entry = nullptr;
            FAudioManifestTrack Track;
            FString Id, Hash, Provenance;
            bool bLoop = false;
            const ELDMusicTrack Expected = ELDMusicTrack(Index + 1);
            if (!(*Music)[Index]->TryGetObject(Entry) || !(*Entry)->TryGetStringField(TEXT("track"), Id)
                || !LDAudio::ParseMusicTrackId(Id, Track.Track) || Track.Track != Expected
                || !(*Entry)->TryGetStringField(TEXT("source"), Track.Source) || !(*Entry)->TryGetStringField(TEXT("sha256"), Hash)
                || !(*Entry)->TryGetBoolField(TEXT("loop"), bLoop) || bLoop != LDAudio::MusicTrackLoops(Expected)
                || !(*Entry)->TryGetStringField(TEXT("provenance"), Provenance) || Provenance.TrimStartAndEnd().IsEmpty()
                || !AudioSafeRelative(Track.Source) || !Track.Source.StartsWith(TEXT("Music/")) || Listed.Contains(Track.Source)
                || !(Track.Source.EndsWith(TEXT(".mp3")) || Track.Source.EndsWith(TEXT(".ogg")) || Track.Source.EndsWith(TEXT(".wav"))))
            {
                return AudioFail(Error, FString::Printf(TEXT("Music entry %d must be %s with a Music/ .mp3/.ogg/.wav source of its own, its sha256, loop %s and a provenance."),
                    Index, *LDAudio::MusicTrackId(Expected), LDAudio::MusicTrackLoops(Expected) ? TEXT("true") : TEXT("false")));
            }
            if (!AudioCheckHash(Track.Source, Hash, Error)) { return false; }
            Listed.Add(Track.Source);
            Out.Tracks.Add(MoveTemp(Track));
        }
        // Only selected files ship: anything else under SourceData\Audio is an unlisted file.
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

    // Imports (or re-imports in place) one SoundWave from a manifest source.
    USoundWave* AudioImportWave(USoundFactory& Factory, const FString& PackageName, const FString& Source)
    {
        UPackage* Package = CreatePackage(*PackageName);
        Package->FullyLoad();
        Factory.bAutoCreateCue = false;
        // Re-imports keep the existing asset and replace its audio without an overwrite prompt.
        Factory.SuppressImportDialogs();
        bool bCancelled = false;
        UObject* Imported = UFactory::StaticImportObject(USoundWave::StaticClass(), Package, FName(*FPackageName::GetShortName(PackageName)),
            RF_Public | RF_Standalone, bCancelled, *FPaths::Combine(AudioSourceRoot(), Source), nullptr, &Factory);
        return bCancelled ? nullptr : Cast<USoundWave>(Imported);
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

    // The package of take Index of a sound: SW_<Name> for the first, SW_<Name>_<Index> for the rest.
    FString AudioTakePackage(const TCHAR* Root, const FString& Name, int32 Index)
    {
        return FString(Root) + Name + (Index == 0 ? FString() : FString::Printf(TEXT("_%d"), Index));
    }

    // The voice priority of a sound when the engine runs out of voices (AUDIO_AND_FEEL §3): the own confirmation and
    // hurt first, then match events, blows and skills, element states, the interface last.
    float AudioPriorityOf(const FString& Name)
    {
        if (Name.StartsWith(TEXT("SelfDealt")) || Name.StartsWith(TEXT("SelfHit")) || Name.StartsWith(TEXT("SelfShield")) || Name.StartsWith(TEXT("SelfDeath"))) { return 3.f; }
        if (Name.StartsWith(TEXT("Match")) || Name.StartsWith(TEXT("Kill")) || Name.StartsWith(TEXT("BodyFall")) || Name.StartsWith(TEXT("Capture")) || Name.StartsWith(TEXT("LevelUp"))) { return 2.5f; }
        if (Name.StartsWith(TEXT("State"))) { return 1.f; }
        if (Name.StartsWith(TEXT("UI"))) { return .8f; }
        return 2.f;
    }

    // Imports every take of Sound onto Class at its volume and priority.
    bool AudioImportTakes(USoundFactory& Factory, const TCHAR* Root, const FAudioManifestSound& Sound, USoundClass* Class,
        TArray<USoundWave*>& OutTakes, TArray<UObject*>& Assets, FString& Error)
    {
        for (int32 Index = 0; Index < Sound.Sources.Num(); ++Index)
        {
            auto* Wave = AudioImportWave(Factory, AudioTakePackage(Root, Sound.Name, Index), Sound.Sources[Index]);
            if (!Wave) { return AudioFail(Error, TEXT("Sound import failed: ") + Sound.Sources[Index]); }
            Wave->SoundClassObject = Class;
            Wave->bLooping = false;
            Wave->AttenuationSettings = nullptr;
            Wave->Volume = Sound.Volume;
            Wave->Priority = AudioPriorityOf(Sound.Name);
            Wave->MarkPackageDirty();
            OutTakes.Add(Wave);
            Assets.Add(Wave);
        }
        return true;
    }

    bool AudioValidateTakes(const TCHAR* Root, const FAudioManifestSound& Sound, TConstArrayView<USoundWave*> Takes, FString& Error)
    {
        if (Takes.Num() != Sound.Sources.Num())
        { return AudioFail(Error, FString::Printf(TEXT("%s has %d takes, the manifest lists %d; run ImportDemoAudio.ps1 -Mode Import."), *Sound.Name, Takes.Num(), Sound.Sources.Num())); }
        for (int32 Index = 0; Index < Takes.Num(); ++Index)
        {
            const USoundWave* Wave = Takes[Index];
            const FString Expected = AudioTakePackage(Root, Sound.Name, Index);
            if (Wave->GetOutermost()->GetName() != Expected)
            { return AudioFail(Error, FString::Printf(TEXT("%s uses %s instead of %s."), *Sound.Name, *Wave->GetPathName(), *Expected)); }
            if (!AudioImportedFromCurrentBytes(*Wave, Sound.Sources[Index], Error)) { return false; }
            if (!FMath::IsNearlyEqual(Wave->Volume, Sound.Volume, 1e-4f))
            { return AudioFail(Error, FString::Printf(TEXT("%s plays at volume %.3f, the manifest says %.3f; run ImportDemoAudio.ps1 -Mode Import."), *Wave->GetPathName(), Wave->Volume, Sound.Volume)); }
            if (!FMath::IsNearlyEqual(Wave->Priority, AudioPriorityOf(Sound.Name), 1e-4f))
            { return AudioFail(Error, Wave->GetPathName() + TEXT(" has a stale voice priority; run ImportDemoAudio.ps1 -Mode Import.")); }
        }
        return true;
    }
}

bool LDDemoAudioImport::Import(FString& Error)
{
    FAudioManifest Loaded;
    if (!AudioLoadManifest(Loaded, Error)) { return false; }
    auto* Master = AudioLoadOrCreate<USoundClass>(AudioMasterPackage, true);
    auto* MusicClass = AudioLoadOrCreate<USoundClass>(AudioMusicClassPackage, true);
    auto* SfxClass = AudioLoadOrCreate<USoundClass>(AudioSfxClassPackage, true);
    auto* UIClass = AudioLoadOrCreate<USoundClass>(AudioUIClassPackage, true);
    auto* Mix = AudioLoadOrCreate<USoundMix>(AudioMixPackage, true);
    auto* Catalog = AudioLoadOrCreate<ULDAudioCatalog>(AudioCatalogPackage, true);
    if (!Master || !MusicClass || !SfxClass || !UIClass || !Mix || !Catalog)
    { return AudioFail(Error, TEXT("An existing audio class, the user mix or the catalog cannot be loaded.")); }
    // The three buses are the master class's only children: the master override reaches them, each bus override scales
    // its own class alone.
    Master->ChildClasses.Reset();
    for (USoundClass* Bus : { MusicClass, SfxClass, UIClass })
    {
        Bus->SetParentClass(Master);
        Bus->ChildClasses.Reset();
        if (!Master->ChildClasses.Contains(Bus)) { Master->ChildClasses.Add(Bus); }
        Bus->MarkPackageDirty();
    }
    // The user volumes are applied at runtime as class overrides; the asset itself carries no adjuster or EQ.
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
    Catalog->MusicClass = MusicClass;
    Catalog->SfxClass = SfxClass;
    Catalog->UIClass = UIClass;
    Catalog->UserMix = Mix;
    Catalog->Cues.Reset();
    Catalog->SkillReleases.Reset();
    Catalog->SkillHits.Reset();
    Catalog->Music.Reset();
    TArray<UObject*> Assets = { Master, MusicClass, SfxClass, UIClass, Mix };
    bool bImported = true;
    for (const FAudioManifestCue& Cue : Loaded.Cues)
    {
        TArray<USoundWave*> Takes;
        USoundClass* Class = ULDAudioCatalog::BusOf(Cue.Cue) == ELDAudioBus::UI ? UIClass : SfxClass;
        if (!(bImported = AudioImportTakes(*Factory, AudioCuePackageRoot, Cue, Class, Takes, Assets, Error))) { break; }
        FLDAudioCueEntry& Entry = Catalog->Cues.AddDefaulted_GetRef();
        Entry.Cue = Cue.Cue;
        Entry.Sound = Takes[0];
        for (int32 Index = 1; Index < Takes.Num(); ++Index) { Entry.Variants.Add(Takes[Index]); }
    }
    const auto ImportSkills = [&](const TArray<FAudioManifestSkill>& Table, TArray<FLDAudioKeyedEntry>& Into)
    {
        for (const FAudioManifestSkill& Skill : Table)
        {
            if (!bImported) { return; }
            TArray<USoundWave*> Takes;
            if (!(bImported = AudioImportTakes(*Factory, AudioSkillPackageRoot, Skill, SfxClass, Takes, Assets, Error))) { return; }
            FLDAudioKeyedEntry& Entry = Into.AddDefaulted_GetRef();
            Entry.Key = Skill.Ability;
            for (USoundWave* Take : Takes) { Entry.Sounds.Add(Take); }
        }
    };
    ImportSkills(Loaded.Skills, Catalog->SkillReleases);
    ImportSkills(Loaded.SkillHits, Catalog->SkillHits);
    for (const FAudioManifestTrack& Track : Loaded.Tracks)
    {
        if (!bImported) { break; }
        auto* Wave = AudioImportWave(*Factory, FString(AudioMusicPackageRoot) + LDAudio::MusicTrackId(Track.Track), Track.Source);
        if (!Wave) { bImported = AudioFail(Error, TEXT("Music import failed: ") + Track.Source); break; }
        // Played at the source level; the player's music volume is the only gain.
        Wave->SoundClassObject = MusicClass;
        Wave->bLooping = LDAudio::MusicTrackLoops(Track.Track);
        Wave->AttenuationSettings = nullptr;
        Wave->Volume = 1.f;
        Wave->MarkPackageDirty();
        FLDMusicTrackEntry& Entry = Catalog->Music.AddDefaulted_GetRef();
        Entry.Track = Track.Track;
        Entry.Sound = Wave;
        Assets.Add(Wave);
    }
    Factory->RemoveFromRoot();
    if (!bImported) { return false; }
    ULDAudioCatalog::FResolved Resolved;
    if (!Catalog->Resolve(Resolved, Error)) { return AudioFail(Error, TEXT("Imported audio catalog is invalid: ") + Error); }
    Catalog->MarkPackageDirty();
    Assets.Add(Catalog);
    for (UObject* Asset : Assets) { if (!AudioSaveAsset(*Asset, Error)) { return false; } }
    Error.Reset();
    return true;
}

bool LDDemoAudioImport::Validate(FString& Error)
{
    FAudioManifest Loaded;
    if (!AudioLoadManifest(Loaded, Error)) { return false; }
    const auto* Catalog = AudioLoadOrCreate<ULDAudioCatalog>(AudioCatalogPackage, false);
    if (!Catalog) { return AudioFail(Error, TEXT("Audio catalog DA_DemoAudio is missing; run ImportDemoAudio.ps1 -Mode Import.")); }
    ULDAudioCatalog::FResolved Resolved;
    if (!Catalog->Resolve(Resolved, Error)) { return AudioFail(Error, TEXT("Audio catalog is invalid: ") + Error); }
    if (Catalog->MasterClass->GetPathName() != ULDAudioCatalog::MasterClassPath || Catalog->UserMix->GetPathName() != ULDAudioCatalog::UserMixPath
        || Catalog->MusicClass->GetPathName() != ULDAudioCatalog::MusicClassPath || Catalog->SfxClass->GetPathName() != ULDAudioCatalog::SfxClassPath
        || Catalog->UIClass->GetPathName() != ULDAudioCatalog::UIClassPath)
    { return AudioFail(Error, TEXT("Audio catalog does not use the single SC_DemoMaster / SC_DemoMusic / SC_DemoSfx / SC_DemoUI / SMix_DemoUser chain.")); }
    if (Catalog->MasterClass->ChildClasses.Num() != 3 || !Catalog->MusicClass->ChildClasses.IsEmpty()
        || !Catalog->SfxClass->ChildClasses.IsEmpty() || !Catalog->UIClass->ChildClasses.IsEmpty())
    { return AudioFail(Error, TEXT("SC_DemoMaster must have exactly the three bus classes as children, each without children; run ImportDemoAudio.ps1 -Mode Import.")); }
    for (const FAudioManifestTrack& Track : Loaded.Tracks)
    {
        const USoundWave* Wave = Resolved.Music[int32(Track.Track)];
        const FString Expected = FString(AudioMusicPackageRoot) + LDAudio::MusicTrackId(Track.Track);
        if (Wave->GetOutermost()->GetName() != Expected)
        { return AudioFail(Error, FString::Printf(TEXT("Music track %s uses %s instead of %s."), *LDAudio::MusicTrackId(Track.Track), *Wave->GetPathName(), *Expected)); }
        if (!AudioImportedFromCurrentBytes(*Wave, Track.Source, Error)) { return false; }
        if (!FMath::IsNearlyEqual(Wave->Volume, 1.f, 1e-4f))
        { return AudioFail(Error, FString::Printf(TEXT("Music track %s plays at volume %.3f instead of its source level; run ImportDemoAudio.ps1 -Mode Import."), *LDAudio::MusicTrackId(Track.Track), Wave->Volume)); }
    }
    for (const FAudioManifestCue& Cue : Loaded.Cues)
    {
        if (!AudioValidateTakes(AudioCuePackageRoot, Cue, Resolved.Cues[int32(Cue.Cue)], Error)) { return false; }
    }
    if (Catalog->SkillReleases.Num() != Loaded.Skills.Num() || Catalog->SkillHits.Num() != Loaded.SkillHits.Num())
    { return AudioFail(Error, TEXT("The catalog's skill releases or hits differ from the manifest; run ImportDemoAudio.ps1 -Mode Import.")); }
    for (const FAudioManifestSkill& Skill : Loaded.Skills)
    {
        if (!AudioValidateTakes(AudioSkillPackageRoot, Skill, Resolved.Skills[Skill.Ability], Error)) { return false; }
    }
    for (const FAudioManifestSkill& Skill : Loaded.SkillHits)
    {
        if (!AudioValidateTakes(AudioSkillPackageRoot, Skill, Resolved.SkillHits[Skill.Ability], Error)) { return false; }
    }
    Error.Reset();
    return true;
}
