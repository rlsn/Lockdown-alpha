#include "UI/LDControlSettings.h"
#include "Internationalization/Regex.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

struct FLDDisplayConfigGuardData
{
    struct FValue
    {
        FString Section, Key, Value;
        bool bPresent = false;
    };
    struct FFile
    {
        FString Filename;
        bool bPreviousNoSave = false;
        TArray<FValue> Values;
    };
    TArray<FFile> Files;
};

bool FLDDisplayConfigWriteGuard::Begin(FConfigCacheIni& Cache, const FString& SettingsFile,
    const FString& QualityFile, const FString& SettingsSection, FString& Error)
{
    if (IsActive() || SettingsFile.IsEmpty() || QualityFile.IsEmpty() || SettingsSection.IsEmpty()
        || !Cache.FindConfigFile(SettingsFile) || !Cache.FindConfigFile(QualityFile))
    { Error = TEXT("The display configuration is unavailable or already guarded. No display trial was started."); return false; }
    auto Captured = MakeShared<FLDDisplayConfigGuardData>();
    auto Capture = [&](const FString& Filename, const FString& Section, const TCHAR* Key)
    {
        auto* File = Captured->Files.FindByPredicate([&](const auto& Entry) { return Entry.Filename == Filename; });
        if (!File)
        {
            FLDDisplayConfigGuardData::FFile Entry;
            Entry.Filename = Filename;
            Entry.bPreviousNoSave = Cache.FindConfigFile(Filename)->NoSave;
            Captured->Files.Add(MoveTemp(Entry));
            File = &Captured->Files.Last();
        }
        FLDDisplayConfigGuardData::FValue Value;
        Value.Section = Section;
        Value.Key = Key;
        Value.bPresent = Cache.GetString(*Section, Key, Value.Value, Filename);
        File->Values.Add(MoveTemp(Value));
    };
    for (const TCHAR* Key : { TEXT("ResolutionSizeX"), TEXT("ResolutionSizeY"),
        TEXT("LastUserConfirmedResolutionSizeX"), TEXT("LastUserConfirmedResolutionSizeY"),
        TEXT("FullscreenMode"), TEXT("LastConfirmedFullscreenMode"), TEXT("PreferredFullscreenMode"),
        TEXT("LastUserConfirmedDisplayID"), TEXT("LastUserConfirmedDisplayIndex"), TEXT("bUseVSync") })
    { Capture(SettingsFile, SettingsSection, Key); }
    for (const TCHAR* Key : { TEXT("sg.ResolutionQuality"), TEXT("sg.ViewDistanceQuality"),
        TEXT("sg.AntiAliasingQuality"), TEXT("sg.ShadowQuality"), TEXT("sg.GlobalIlluminationQuality"),
        TEXT("sg.ReflectionQuality"), TEXT("sg.PostProcessQuality"), TEXT("sg.TextureQuality"),
        TEXT("sg.EffectsQuality"), TEXT("sg.FoliageQuality"), TEXT("sg.ShadingQuality"), TEXT("sg.LandscapeQuality") })
    { Capture(QualityFile, TEXT("ScalabilityGroups"), Key); }
    for (const auto& File : Captured->Files) { Cache.FindConfigFile(File.Filename)->NoSave = true; }
    Data = MoveTemp(Captured);
    Error.Empty();
    return true;
}

bool FLDDisplayConfigWriteGuard::Release(FConfigCacheIni& Cache, FString& Error)
{
    if (!Data) { Error.Empty(); return true; }
    for (const auto& File : Data->Files)
    {
        if (!Cache.FindConfigFile(File.Filename))
        { Error = TEXT("The guarded display configuration disappeared. Display saving remains blocked."); return false; }
    }
    // Use cache mutators so UE's saved-layer change tracking also loses any automatic trial writes.
    for (const auto& File : Data->Files)
    {
        for (const auto& Value : File.Values)
        {
            if (Value.bPresent) { Cache.SetString(*Value.Section, *Value.Key, *Value.Value, File.Filename); }
            else { Cache.RemoveKey(*Value.Section, *Value.Key, File.Filename); }
        }
    }
    for (const auto& File : Data->Files) { Cache.FindConfigFile(File.Filename)->NoSave = File.bPreviousNoSave; }
    Data.Reset();
    Error.Empty();
    return true;
}

namespace
{
    constexpr uint32 LocalSettingsMagic = 0x4C444353;
    constexpr uint32 LegacySaveGameMagic = 0x53415647;
    constexpr int64 MaxSettingsBytes = 65536;
    constexpr int32 MaxBindingCount = 64;

    bool SupportedProfile(uint8 Profile) { return Profile == 6 || Profile == 7 || Profile == 8; }

    enum class ELocalFileLookup : uint8 { Present, Missing, Failed };

#if PLATFORM_WINDOWS
    FString WindowsSettingsPath(const FString& Filename, bool ForWrite)
    {
        FString Path = ForWrite
            ? IFileManager::Get().ConvertToAbsolutePathForExternalAppForWrite(*Filename)
            : IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*Filename);
        Path.ReplaceInline(TEXT("/"), TEXT("\\"));
        return Path;
    }
#endif

    ELocalFileLookup LookupSettingsFile(const FLDLocalSettingsStorage& Storage, FString& Error)
    {
#if PLATFORM_WINDOWS
        const DWORD Attributes = ::GetFileAttributesW(*WindowsSettingsPath(Storage.Filename(), false));
        const DWORD NativeError = Attributes == INVALID_FILE_ATTRIBUTES ? ::GetLastError() : ERROR_SUCCESS;
        if (Attributes == INVALID_FILE_ATTRIBUTES)
        {
            if (NativeError == ERROR_FILE_NOT_FOUND || NativeError == ERROR_PATH_NOT_FOUND)
            { return ELocalFileLookup::Missing; }
            Error = FString::Printf(TEXT("Local settings metadata could not be read (Windows error %u). The file has not been replaced."), uint32(NativeError));
            return ELocalFileLookup::Failed;
        }
        if (Attributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            Error = TEXT("The local settings file cannot be read: its path is a directory.");
            return ELocalFileLookup::Failed;
        }
        return ELocalFileLookup::Present;
#else
        Error = TEXT("Local settings storage requires the supported Windows file implementation.");
        return ELocalFileLookup::Failed;
#endif
    }

    bool AtomicReplaceSettingsFile(const FString& Filename, const FString& Temporary, FString& Error)
    {
#if PLATFORM_WINDOWS
        const FString Target = WindowsSettingsPath(Filename, true), Source = WindowsSettingsPath(Temporary, true);
        HANDLE File = ::CreateFileW(*Source, GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (File == INVALID_HANDLE_VALUE)
        {
            Error = FString::Printf(TEXT("Preferences are applied, but the replacement file could not be opened for a durable write (Windows error %u). The previous file was kept."), uint32(::GetLastError()));
            return false;
        }
        const bool Flushed = ::FlushFileBuffers(File) != 0;
        const DWORD FlushError = Flushed ? ERROR_SUCCESS : ::GetLastError();
        const bool Closed = ::CloseHandle(File) != 0;
        const DWORD CloseError = Closed ? ERROR_SUCCESS : ::GetLastError();
        if (!Flushed || !Closed)
        {
            Error = FString::Printf(TEXT("Preferences are applied, but the replacement file could not be flushed (Windows error %u). The previous file was kept."), uint32(Flushed ? CloseError : FlushError));
            return false;
        }
        // A sibling temporary file keeps this a same-volume rename. Never predelete or allow copy fallback.
        if (!::MoveFileExW(*Source, *Target, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            Error = FString::Printf(TEXT("Preferences are applied, but atomic replacement failed (Windows error %u). The previous file was kept; retry saving."), uint32(::GetLastError()));
            return false;
        }
        return true;
#else
        Error = TEXT("Atomic local settings replacement is unavailable on this platform. The previous file was kept.");
        return false;
#endif
    }

    bool ReadConfigFromDisk(const FString& Filename, FConfigFile& Out)
    {
        FString Contents;
        if (Filename.IsEmpty() || !FFileHelper::LoadFileToString(Contents, *Filename)) { return false; }
        Out.ProcessInputFileContents(Contents, Filename);
        return true;
    }

    bool IntMatches(const FConfigFile& File, const TCHAR* Section, const TCHAR* Key, int32 Expected)
    {
        FString Value;
        return File.GetString(Section, Key, Value) && Value.TrimStartAndEnd() == FString::FromInt(Expected);
    }
}

FLDLocalSettingsStorage FLDLocalSettingsStorage::User(uint8 RequestedProfile)
{
    FLDLocalSettingsStorage Result;
    if (!SupportedProfile(RequestedProfile)) { return Result; }
    const TCHAR* Slot = RequestedProfile == 8 ? TEXT("LockdownLocalControlsV1_V08.sav")
        : RequestedProfile == 7 ? TEXT("LockdownLocalControlsV07.sav") : TEXT("LockdownLocalControlsV1.sav");
    Result.Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"), Slot);
    return Result;
}

FLDLocalSettingsStorage FLDLocalSettingsStorage::Automation(const FGuid& Fixture)
{
    FLDLocalSettingsStorage Result;
    Result.bAutomation = true;
    if (Fixture.IsValid())
    {
        Result.Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("Artifacts"), TEXT("Automation"), TEXT("LocalSettings"),
            Fixture.ToString(EGuidFormats::Digits) + TEXT(".sav"));
    }
    return Result;
}

const TArray<FLDControlBinding>& ULDControlSettings::Defaults(bool Tactical)
{
    static const TArray<FLDControlBinding> Values={
        {TEXT("Select"),TEXT("Select / confirm target"),EKeys::LeftMouseButton},
        {TEXT("Navigate"),TEXT("Move / approach / cancel targeting"),EKeys::RightMouseButton},
        {TEXT("Attack"),TEXT("Arm basic attack"),EKeys::A},
        {TEXT("Skill1"),TEXT("Equipped skill 1"),EKeys::Q},{TEXT("Skill2"),TEXT("Equipped skill 2"),EKeys::W},
        {TEXT("Skill3"),TEXT("Equipped skill 3"),EKeys::E},{TEXT("Skill4"),TEXT("Equipped skill 4"),EKeys::R},
        {TEXT("Item1"),TEXT("Quick item 1"),EKeys::One},{TEXT("Item2"),TEXT("Quick item 2"),EKeys::Two},
        {TEXT("Item3"),TEXT("Quick item 3"),EKeys::Three},{TEXT("Item4"),TEXT("Quick item 4"),EKeys::Four},
        {TEXT("Stop"),TEXT("Stop movement / current action"),EKeys::S},
        {TEXT("Sprint"),TEXT("Hold sprint"),EKeys::LeftShift},{TEXT("Inventory"),TEXT("Inventory"),EKeys::I},
        {TEXT("Development"),TEXT("Character / preparation"),EKeys::P},
        {TEXT("PanUp"),TEXT("Pan camera up"),EKeys::Up},{TEXT("PanDown"),TEXT("Pan camera down"),EKeys::Down},
        {TEXT("PanLeft"),TEXT("Pan camera left"),EKeys::Left},{TEXT("PanRight"),TEXT("Pan camera right"),EKeys::Right},
        {TEXT("Recenter"),TEXT("Hold camera recenter"),EKeys::SpaceBar},{TEXT("Follow"),TEXT("Toggle camera follow"),EKeys::Home},
        {TEXT("Zoom"),TEXT("Camera zoom"),EKeys::MouseWheelAxis,true},
        {TEXT("Help"),TEXT("Help / controls / settings"),EKeys::F1},{TEXT("Back"),TEXT("Back / cancel (reserved)"),EKeys::Escape}
    };
    static const TArray<FLDControlBinding> TacticalValues=[](){auto V=Values;V.RemoveAll([](const auto& B){return B.Action==TEXT("Item4");});V.Insert({TEXT("Skill5"),TEXT("Equipped skill 5"),EKeys::D},7);return V;}();
    return Tactical?TacticalValues:Values;
}
const TArray<FLDControlBinding>& ULDControlSettings::HybridDefaults()
{static const TArray<FLDControlBinding> Values=[](){auto V=Defaults(true);V.Insert({TEXT("Skill6"),TEXT("Shared action 6"),EKeys::G},8);V.Insert({TEXT("Item4"),TEXT("Physical accessory 4"),EKeys::Four},12);for(auto& B:V){if(B.Action.ToString().StartsWith(TEXT("Item"))){B.Label=TEXT("Physical accessory ")+B.Action.ToString().Right(1);}}return V;}();return Values;}
FKey ULDControlSettings::Key(FName Action) const
{
    if (!Definitions().ContainsByPredicate([&](const FLDControlBinding& B) { return B.Action == Action; })) { return FKey(); }
    const FKey* Key = Bindings.Find(Action);
    return Key ? *Key : FKey();
}
FString ULDControlSettings::Label(FName Action) const {return Key(Action).GetDisplayName().ToString();}
void ULDControlSettings::ResetBindings()
{
    Bindings.Reset();
    for (const auto& B : Definitions()) { Bindings.Add(B.Action, B.Key); }
    if (Phase != ELDLocalSettingsPhase::EditingDraft) { Phase = ELDLocalSettingsPhase::ConfirmedApplied; }
}
bool ULDControlSettings::Rebind(FName Action,FKey NewKey,FString& Error)
{
    const auto* Definition=Definitions().FindByPredicate([&](const FLDControlBinding& B){return B.Action==Action;});
    if(!Definition){Error=TEXT("This action is not in the current control registry.");return false;}
    if(Action==TEXT("Back")||NewKey==EKeys::Escape){Error=TEXT("Escape is reserved for Back.");return false;}
    if(!NewKey.IsValid()||NewKey.IsGamepadKey()||NewKey.IsAxis2D()||NewKey.IsAxis3D()
        ||(Definition->Axis&&NewKey!=EKeys::MouseWheelAxis)||(!Definition->Axis&&NewKey.IsAxis1D()))
    {Error=Definition->Axis?TEXT("Zoom uses the mouse wheel axis."):TEXT("Choose a keyboard key or mouse button.");return false;}
    for(const auto& B:Definitions())
    {
        const FKey Other=Key(B.Action);
        const bool WheelOverlap=(NewKey==EKeys::MouseScrollUp||NewKey==EKeys::MouseScrollDown)&&Other==EKeys::MouseWheelAxis;
        if(B.Action!=Action&&(Other==NewKey||WheelOverlap)){Error=TEXT("Already used by ")+B.Label+TEXT(". Choose another key.");return false;}
    }
    Bindings.Add(Action,NewKey);
    if (Phase != ELDLocalSettingsPhase::EditingDraft) { Phase = ELDLocalSettingsPhase::ConfirmedApplied; }
    Error.Empty();return true;
}
bool ULDControlSettings::Valid() const
{
    FString Error;
    return ValidateLocalSettings(Error);
}
bool ULDControlSettings::ValidatePreferences(FString& Error) const
{
    if (!FMath::IsFinite(TextScale) || TextScale < 1 || TextScale > 1.25
        || !FMath::IsFinite(Zoom) || Zoom < 1100 || Zoom > 1700)
    { Error = TEXT("Camera zoom or text scale is outside the current supported range."); return false; }
    Error.Empty();
    return true;
}
bool ULDControlSettings::ValidateLocalSettings(FString& Error) const
{
    if (Schema != CurrentSchema || !SupportedProfile(Profile))
    { Error = TEXT("The local settings schema or control registry is unsupported."); return false; }
    if (!ValidatePreferences(Error)) { return false; }
    if (Bindings.Num() != Definitions().Num() || Key(TEXT("Back")) != EKeys::Escape)
    { Error = TEXT("Saved bindings do not match the current registry. Reset bindings to recover."); return false; }
    TSet<FKey> Seen;
    for(const auto& B:Definitions())
    {
        const FKey K=Key(B.Action);
        if(!K.IsValid()||K.IsGamepadKey()||K.IsAxis2D()||K.IsAxis3D()||Seen.Contains(K)
            ||(!B.Axis&&K.IsAxis1D())||(B.Axis&&K!=EKeys::MouseWheelAxis)
            ||K==EKeys::MouseScrollUp||K==EKeys::MouseScrollDown)
        { Error = TEXT("Saved bindings contain an invalid key, axis or conflict. Reset bindings to recover."); return false; }
        Seen.Add(K);
    }
    Error.Empty();
    return true;
}

void ULDControlSettings::CopyPreferences(const ULDControlSettings& Other)
{
    Schema = Other.Schema;
    Profile = Other.Profile;
    Bindings = Other.Bindings;
    bEdgePan = Other.bEdgePan;
    bStartFollowing = Other.bStartFollowing;
    TextScale = Other.TextScale;
    Zoom = Other.Zoom;
}

ULDControlSettings* ULDControlSettings::CreateSessionSettings(const FLDLocalSettingsStorage& InStorage, uint8 RequestedProfile)
{
    if (!SupportedProfile(RequestedProfile)) { return nullptr; }
    auto* Settings = NewObject<ULDControlSettings>();
    Settings->Storage = InStorage;
    Settings->Profile = RequestedProfile;
    Settings->ResetBindings();
    Settings->Phase = ELDLocalSettingsPhase::ConfirmedApplied;
    return Settings;
}

ULDControlSettings* ULDControlSettings::CreateDraft() const
{
    auto* Draft = NewObject<ULDControlSettings>();
    Draft->CopyPreferences(*this);
    return Draft;
}

bool ULDControlSettings::ApplyLocalSettings(const ULDControlSettings& Draft, FString& Error)
{
    if (Phase == ELDLocalSettingsPhase::EditingDraft || Draft.Profile != Profile)
    { Error = TEXT("Apply to the active local settings owner, using its current registry."); return false; }
    if (!Draft.ValidateLocalSettings(Error)) { return false; }
    CopyPreferences(Draft);
    Phase = ELDLocalSettingsPhase::ConfirmedApplied;
    return true;
}

bool ULDControlSettings::SetAppliedZoom(float Value, FString& Error)
{
    if (Phase == ELDLocalSettingsPhase::EditingDraft || !FMath::IsFinite(Value) || Value < 1100 || Value > 1700)
    { Error = TEXT("Zoom was not saved: the applied camera value is invalid."); return false; }
    Zoom = Value;
    Phase = ELDLocalSettingsPhase::ConfirmedApplied;
    Error.Empty();
    return true;
}

void ULDControlSettings::SetAppliedFollowing(bool Value)
{
    bStartFollowing = Value;
    if (Phase != ELDLocalSettingsPhase::EditingDraft) { Phase = ELDLocalSettingsPhase::ConfirmedApplied; }
}

void ULDControlSettings::ResetLocalDefaults()
{
    const auto* CurrentDefaults = GetDefault<ULDControlSettings>();
    Schema = CurrentSchema;
    bEdgePan = CurrentDefaults->bEdgePan;
    bStartFollowing = CurrentDefaults->bStartFollowing;
    TextScale = CurrentDefaults->TextScale;
    Zoom = CurrentDefaults->Zoom;
    ResetBindings();
}

FLDLocalSettingsLoadResult ULDControlSettings::LoadLocalSettings(const FLDLocalSettingsStorage& InStorage, uint8 RequestedProfile)
{
    if (!InStorage.IsValid() || !SupportedProfile(RequestedProfile))
    { return { ELDLocalSettingsLoadStatus::InvalidStorage, nullptr, nullptr, TEXT("Invalid local settings storage or registry. No user file was selected.") }; }

    auto& Files = IFileManager::Get();
    const FString& Filename = InStorage.Filename();
    FString LookupError;
    const ELocalFileLookup Lookup = LookupSettingsFile(InStorage, LookupError);
    if (Lookup == ELocalFileLookup::Failed)
    { return { ELDLocalSettingsLoadStatus::ReadFailed, nullptr, nullptr, LookupError }; }
    if (Lookup == ELocalFileLookup::Missing)
    { return { ELDLocalSettingsLoadStatus::Missing, CreateSessionSettings(InStorage, RequestedProfile), nullptr, TEXT("No saved local preferences. Current defaults are applied, not yet saved.") }; }
    TUniquePtr<FArchive> FileReader(Files.CreateFileReader(*Filename, FILEREAD_Silent));
    if (!FileReader)
    { return { ELDLocalSettingsLoadStatus::ReadFailed, nullptr, nullptr, TEXT("The local settings file cannot be read. Check file access and retry.") }; }
    const int64 Size = FileReader->TotalSize();
    if (Size < 0)
    { return { ELDLocalSettingsLoadStatus::ReadFailed, nullptr, nullptr, TEXT("The local settings file size could not be read. It has not been replaced.") }; }
    if (Size < 12 || Size > MaxSettingsBytes)
    { return { ELDLocalSettingsLoadStatus::Corrupt, nullptr, nullptr, TEXT("The local settings file has an invalid size. It has not been replaced.") }; }
    TArray<uint8> Bytes;
    Bytes.SetNumUninitialized(int32(Size));
    FileReader->Serialize(Bytes.GetData(), Size);
    const bool ReadError = FileReader->IsError();
    if (!FileReader->Close() || ReadError)
    { return { ELDLocalSettingsLoadStatus::ReadFailed, nullptr, nullptr, TEXT("The local settings file could not be read. It has not been replaced.") }; }

    FMemoryReader Reader(Bytes, true);
    Reader.ArMaxSerializeSize = MaxSettingsBytes;
    uint32 Magic = 0, SavedSchema = 0, Checksum = 0;
    Reader << Magic << SavedSchema << Checksum;
    if (Magic == LegacySaveGameMagic || (Magic == LocalSettingsMagic && SavedSchema != CurrentSchema))
    { return { ELDLocalSettingsLoadStatus::UnsupportedSchema, nullptr, nullptr, TEXT("This local settings format is unsupported. Use the recovery control to replace it explicitly.") }; }
    if (Magic != LocalSettingsMagic || Checksum != FCrc::MemCrc32(Bytes.GetData() + 12, Bytes.Num() - 12))
    { return { ELDLocalSettingsLoadStatus::Corrupt, nullptr, nullptr, TEXT("The local settings file is damaged. It has not been replaced.") }; }

    auto* Settings = NewObject<ULDControlSettings>();
    Settings->Storage = InStorage;
    uint8 Edge = 0, Follow = 0;
    int32 Count = 0;
    Reader << Settings->Profile << Edge << Follow << Settings->TextScale << Settings->Zoom << Count;
    if (Reader.IsError() || Edge > 1 || Follow > 1 || Count < 0 || Count > MaxBindingCount)
    { return { ELDLocalSettingsLoadStatus::Corrupt, nullptr, nullptr, TEXT("The local settings payload is malformed. It has not been replaced.") }; }
    Settings->bEdgePan = Edge != 0;
    Settings->bStartFollowing = Follow != 0;
    for (int32 Index = 0; Index < Count && !Reader.IsError(); ++Index)
    {
        FString Action, Key;
        Reader << Action << Key;
        if (Reader.IsError() || Action.IsEmpty() || Action.Len() > 64 || Key.IsEmpty() || Key.Len() > 64
            || Action.Len() != FCString::Strlen(*Action) || Key.Len() != FCString::Strlen(*Key)
            || Settings->Bindings.Contains(FName(*Action)))
        { return { ELDLocalSettingsLoadStatus::Corrupt, nullptr, nullptr, TEXT("The saved binding list is malformed. It has not been replaced.") }; }
        Settings->Bindings.Add(FName(*Action), FKey(FName(*Key)));
    }
    if (Reader.IsError() || !Reader.AtEnd())
    { return { ELDLocalSettingsLoadStatus::Corrupt, nullptr, nullptr, TEXT("The local settings payload is incomplete or contains extra data.") }; }
    FString Error;
    if (!Settings->ValidatePreferences(Error))
    { return { ELDLocalSettingsLoadStatus::Corrupt, nullptr, nullptr, Error + TEXT(" The file has not been replaced.") }; }
    if (Settings->Profile != RequestedProfile)
    { return { ELDLocalSettingsLoadStatus::UnsupportedSchema, nullptr, Settings, TEXT("Saved bindings belong to a different registry. No binding migration was performed.") }; }
    if (!Settings->ValidateLocalSettings(Error))
    { return { ELDLocalSettingsLoadStatus::Corrupt, nullptr, Settings, Error + TEXT(" Other valid local preferences can be kept.") }; }
    Settings->Phase = ELDLocalSettingsPhase::PersistenceVerified;
    return { ELDLocalSettingsLoadStatus::Loaded, Settings, nullptr, TEXT("Local preferences loaded.") };
}

FLDLocalSettingsSaveResult ULDControlSettings::SaveLocalSettings()
{
    FString Error;
    if (Phase == ELDLocalSettingsPhase::EditingDraft || !Storage.IsValid() || !ValidateLocalSettings(Error))
    { return { ELDLocalSettingsSaveStatus::Invalid, Error.IsEmpty() ? TEXT("Only confirmed, applied local preferences with explicit storage may be saved.") : Error }; }
    if (bRecoveryRequired)
    { return { ELDLocalSettingsSaveStatus::RecoveryRequired, TEXT("Applied for this session; the unreadable settings file was preserved. Confirm replacement in Settings before saving.") }; }

    // Deliberate field whitelist: UObject/SaveGame serialization would also write unrelated properties.
    FBufferArchive Payload;
    uint8 SavedProfile = Profile, Edge = bEdgePan ? 1 : 0, Follow = bStartFollowing ? 1 : 0;
    float SavedTextScale = TextScale, SavedZoom = Zoom;
    int32 Count = Definitions().Num();
    Payload << SavedProfile << Edge << Follow << SavedTextScale << SavedZoom << Count;
    for (const auto& Binding : Definitions())
    {
        FString Action = Binding.Action.ToString(), KeyName = Key(Binding.Action).GetFName().ToString();
        Payload << Action << KeyName;
    }
    FBufferArchive Bytes;
    uint32 Magic = LocalSettingsMagic, SavedSchema = CurrentSchema, Checksum = FCrc::MemCrc32(Payload.GetData(), Payload.Num());
    Bytes << Magic << SavedSchema << Checksum;
    Bytes.Append(Payload);
    if (Bytes.IsError() || Bytes.Num() > MaxSettingsBytes)
    { return { ELDLocalSettingsSaveStatus::Invalid, TEXT("The local settings payload cannot be serialized.") }; }
    Phase = ELDLocalSettingsPhase::ConfirmedApplied;
    auto& Files = IFileManager::Get();
    const FString& Filename = Storage.Filename();
    if (!Files.MakeDirectory(*FPaths::GetPath(Filename), true))
    { return { ELDLocalSettingsSaveStatus::WriteFailed, TEXT("Preferences are applied, but the local settings directory could not be created. Retry saving.") }; }
    const FString Temporary = Filename + TEXT(".tmp-") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    auto RemoveTemporary = [&]()
    {
        if (Files.FileExists(*Temporary) && !Files.Delete(*Temporary, false, false, true))
        { UE_LOG(LogTemp, Warning, TEXT("Could not remove local settings temporary file: %s"), *Temporary); }
    };
    if (!FFileHelper::SaveArrayToFile(Bytes, *Temporary))
    {
        RemoveTemporary();
        return { ELDLocalSettingsSaveStatus::WriteFailed, TEXT("Preferences are applied, but writing the local settings file failed. Retry saving.") };
    }
    TArray<uint8> Readback;
    if (!FFileHelper::LoadFileToArray(Readback, *Temporary) || Readback != Bytes)
    {
        RemoveTemporary();
        return { ELDLocalSettingsSaveStatus::ReadbackFailed, TEXT("Preferences are applied, but the new file could not be verified. The previous file was kept.") };
    }
    if (!AtomicReplaceSettingsFile(Filename, Temporary, Error))
    {
        RemoveTemporary();
        return { ELDLocalSettingsSaveStatus::WriteFailed, Error };
    }
    Readback.Reset();
    if (!FFileHelper::LoadFileToArray(Readback, *Filename) || Readback != Bytes)
    { return { ELDLocalSettingsSaveStatus::ReadbackFailed, TEXT("Preferences are applied, but reading back the saved file failed. Persistence is not verified; retry saving.") }; }
    Phase = ELDLocalSettingsPhase::PersistenceVerified;
    return { ELDLocalSettingsSaveStatus::Saved, TEXT("Local preferences saved and verified from disk.") };
}

FLDDisplaySettings FLDDisplaySettings::Read(const UGameUserSettings& Settings)
{
    FLDDisplaySettings Result;
    Result.Resolution = Settings.GetScreenResolution();
    Result.WindowMode = Settings.GetFullscreenMode();
    Result.PreferredFullscreenMode = Settings.GetPreferredFullscreenMode();
    Result.bVSync = Settings.IsVSyncEnabled();
    Result.Quality = Settings.ScalabilityQuality;
    return Result;
}
bool FLDDisplaySettings::SameMode(const FLDDisplaySettings& Other) const
{ return Resolution == Other.Resolution && WindowMode == Other.WindowMode; }
bool FLDDisplaySettings::operator==(const FLDDisplaySettings& Other) const
{ return SameMode(Other) && PreferredFullscreenMode == Other.PreferredFullscreenMode && bVSync == Other.bVSync && Quality == Other.Quality; }
bool FLDDisplaySettings::IsValid() const
{
    if (Resolution.X <= 0 || Resolution.Y <= 0
        || (WindowMode != EWindowMode::Windowed && WindowMode != EWindowMode::WindowedFullscreen && WindowMode != EWindowMode::Fullscreen)
        || (PreferredFullscreenMode != EWindowMode::Fullscreen && PreferredFullscreenMode != EWindowMode::WindowedFullscreen)
        || !FMath::IsFinite(Quality.ResolutionQuality) || Quality.ResolutionQuality < 0 || Quality.ResolutionQuality > 100) { return false; }
    for (int32 Value : { Quality.ViewDistanceQuality, Quality.AntiAliasingQuality, Quality.ShadowQuality,
        Quality.GlobalIlluminationQuality, Quality.ReflectionQuality, Quality.PostProcessQuality, Quality.TextureQuality,
        Quality.EffectsQuality, Quality.FoliageQuality, Quality.ShadingQuality, Quality.LandscapeQuality })
    { if (Value < 0 || Value > 4) { return false; } }
    return true;
}

uint64 FLDDisplayTrialState::Begin(const FLDDisplaySettings& Confirmed, const FLDDisplaySettings& Draft, double Now, double Duration)
{
    if (ActiveId || Sequence == MAX_uint64 || !Confirmed.IsValid() || !Draft.IsValid()
        || !FMath::IsFinite(Now) || !FMath::IsFinite(Duration) || Duration <= 0 || !FMath::IsFinite(Now + Duration)) { return 0; }
    Before = Confirmed;
    Pending = Draft;
    Deadline = Now + Duration;
    ActiveId = ++Sequence;
    return ActiveId;
}
bool FLDDisplayTrialState::IsExpired(double Now) const
{ return ActiveId != 0 && (!FMath::IsFinite(Now) || Now >= Deadline); }
bool FLDDisplayTrialState::Invalidate(uint64 Id)
{
    if (!IsCurrent(Id)) { return false; }
    ActiveId = 0;
    Deadline = 0;
    return true;
}
double FLDDisplayTrialState::Remaining(double Now) const
{ return ActiveId && FMath::IsFinite(Now) ? FMath::Max(0., Deadline - Now) : 0.; }

bool LDLocalSettings::ResolveConfigDestination(FConfigCacheIni& Cache, const FString& ConfigId, FString& Filename, FString& Error)
{
    Filename.Reset();
    Error.Reset();
    if (ConfigId.IsEmpty())
    {
        Error = TEXT("Configuration identifier is empty. No physical file can be verified.");
        return false;
    }
    const auto* Branch = Cache.FindBranch(FName(*ConfigId), ConfigId);
    if (!Branch || Branch->IniPath.IsEmpty())
    {
        Error = TEXT("Configuration destination is unavailable for ") + ConfigId + TEXT(". No physical file can be verified.");
        return false;
    }
    Filename = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*Branch->IniPath);
    if (Filename.IsEmpty())
    {
        Error = TEXT("Configuration destination could not be resolved for ") + ConfigId + TEXT(".");
        return false;
    }
    return true;
}

FLDLocalSettingsSaveResult LDLocalSettings::FlushAndVerifyDisplaySettingsFiles(
    FConfigCacheIni& Cache, const FString& SettingsFile, const FString& QualityFile,
    const FString& SettingsSection, const FLDDisplaySettings& Expected)
{
    if (SettingsFile.IsEmpty() || QualityFile.IsEmpty() || SettingsSection.IsEmpty() || !Expected.IsValid())
    { return { ELDLocalSettingsSaveStatus::Invalid, TEXT("Invalid display persistence target or expected values. No quality file was flushed.") }; }
    FString PhysicalSettings, PhysicalQuality, ResolutionError;
    if (!ResolveConfigDestination(Cache, SettingsFile, PhysicalSettings, ResolutionError)
        || !ResolveConfigDestination(Cache, QualityFile, PhysicalQuality, ResolutionError))
    { return { ELDLocalSettingsSaveStatus::WriteFailed, ResolutionError }; }
    if (SettingsFile != QualityFile)
    {
        const auto* Quality = Cache.FindConfigFile(QualityFile);
        if (!Quality)
        { return { ELDLocalSettingsSaveStatus::WriteFailed, TEXT("Display changes are applied, but the separate quality configuration is unavailable. Retry saving.") }; }
        if (Quality->NoSave)
        { return { ELDLocalSettingsSaveStatus::WriteFailed, TEXT("Display changes are applied, but the separate quality configuration prohibits writes. Persistence is not verified.") }; }
        if (!Cache.Flush(false, QualityFile))
        { return { ELDLocalSettingsSaveStatus::WriteFailed, TEXT("Display changes are applied, but flushing the separate quality configuration failed. Retry saving.") }; }
    }
    return VerifyDisplaySettingsFile(PhysicalSettings, PhysicalQuality, SettingsSection, Expected);
}

FLDLocalSettingsSaveResult LDLocalSettings::VerifyDisplaySettingsFile(
    const FString& SettingsFile, const FString& QualityFile, const FString& SettingsSection, const FLDDisplaySettings& Expected)
{
    if (!Expected.IsValid() || SettingsSection.IsEmpty())
    { return { ELDLocalSettingsSaveStatus::Invalid, TEXT("Display values cannot be verified because their expected state is invalid.") }; }
    FConfigFile Display, Quality;
    if (!ReadConfigFromDisk(SettingsFile, Display) || !ReadConfigFromDisk(QualityFile, Quality))
    { return { ELDLocalSettingsSaveStatus::ReadbackFailed, TEXT("Display changes are applied, but the actual configuration file could not be read. Retry saving.") }; }
    FString VSync;
    const TCHAR* Section = *SettingsSection;
    const bool DisplayMatches = IntMatches(Display, Section, TEXT("ResolutionSizeX"), Expected.Resolution.X)
        && IntMatches(Display, Section, TEXT("ResolutionSizeY"), Expected.Resolution.Y)
        && IntMatches(Display, Section, TEXT("LastUserConfirmedResolutionSizeX"), Expected.Resolution.X)
        && IntMatches(Display, Section, TEXT("LastUserConfirmedResolutionSizeY"), Expected.Resolution.Y)
        && IntMatches(Display, Section, TEXT("FullscreenMode"), int32(Expected.WindowMode))
        && IntMatches(Display, Section, TEXT("LastConfirmedFullscreenMode"), int32(Expected.WindowMode))
        && IntMatches(Display, Section, TEXT("PreferredFullscreenMode"), int32(Expected.PreferredFullscreenMode))
        && Display.GetString(Section, TEXT("bUseVSync"), VSync)
        && VSync.Equals(Expected.bVSync ? TEXT("True") : TEXT("False"), ESearchCase::IgnoreCase);
    const TCHAR* QualitySection = TEXT("ScalabilityGroups");
    const bool QualityMatches = IntMatches(Quality, QualitySection, TEXT("sg.ViewDistanceQuality"), Expected.Quality.ViewDistanceQuality)
        && IntMatches(Quality, QualitySection, TEXT("sg.AntiAliasingQuality"), Expected.Quality.AntiAliasingQuality)
        && IntMatches(Quality, QualitySection, TEXT("sg.ShadowQuality"), Expected.Quality.ShadowQuality)
        && IntMatches(Quality, QualitySection, TEXT("sg.GlobalIlluminationQuality"), Expected.Quality.GlobalIlluminationQuality)
        && IntMatches(Quality, QualitySection, TEXT("sg.ReflectionQuality"), Expected.Quality.ReflectionQuality)
        && IntMatches(Quality, QualitySection, TEXT("sg.PostProcessQuality"), Expected.Quality.PostProcessQuality)
        && IntMatches(Quality, QualitySection, TEXT("sg.TextureQuality"), Expected.Quality.TextureQuality)
        && IntMatches(Quality, QualitySection, TEXT("sg.EffectsQuality"), Expected.Quality.EffectsQuality)
        && IntMatches(Quality, QualitySection, TEXT("sg.FoliageQuality"), Expected.Quality.FoliageQuality)
        && IntMatches(Quality, QualitySection, TEXT("sg.ShadingQuality"), Expected.Quality.ShadingQuality)
        && IntMatches(Quality, QualitySection, TEXT("sg.LandscapeQuality"), Expected.Quality.LandscapeQuality);
    FString ScaleText;
    bool ScaleMatches = false;
    if (Quality.GetString(QualitySection, TEXT("sg.ResolutionQuality"), ScaleText))
    {
        double Scale = 0;
        const FRegexPattern Number(TEXT("^[+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?$"));
        FRegexMatcher Matcher(Number, ScaleText);
        ScaleMatches = Matcher.FindNext() && Matcher.GetMatchEnding() == ScaleText.Len()
            && ScaleText.Len() == FCString::Strlen(*ScaleText)
            && LexTryParseString(Scale, *ScaleText) && FMath::IsFinite(Scale)
            && FMath::IsNearlyEqual(Scale, double(Expected.Quality.ResolutionQuality), .0001);
    }
    if (!DisplayMatches || !QualityMatches || !ScaleMatches)
    { return { ELDLocalSettingsSaveStatus::ReadbackFailed, TEXT("Display changes are applied, but disk values do not match. Persistence is unverified; retry saving.") }; }
    return { ELDLocalSettingsSaveStatus::Saved, TEXT("Display settings saved and verified from disk.") };
}
