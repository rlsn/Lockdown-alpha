#include "UI/LDControlSettings.h"
#include "Audio/LDAudioSubsystem.h"
#include "Framework/LDGameInstance.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"
#include "Templates/UnrealTemplate.h"
#include <limits>
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    struct FLocalSettingsFixture
    {
        FAutomationTestBase& Test;
        FGuid Id = FGuid::NewGuid();
        FLDLocalSettingsStorage Storage = FLDLocalSettingsStorage::Automation(Id);
        FString DisplayFile = Storage.Filename() + TEXT(".ini");
        FString QualityFile = Storage.Filename() + TEXT(".quality.ini");

        explicit FLocalSettingsFixture(FAutomationTestBase& InTest) : Test(InTest)
        {
            Test.TestTrue(TEXT("Create isolated fixture directory"),
                IFileManager::Get().MakeDirectory(*FPaths::GetPath(Storage.Filename()), true));
        }
        ~FLocalSettingsFixture()
        {
            for (const FString& Filename : { Storage.Filename(), DisplayFile, QualityFile })
            {
                auto& Files = IFileManager::Get();
                if (Files.FileExists(*Filename)) { Test.TestTrue(TEXT("Remove exact fixture file"), Files.Delete(*Filename, false, false, true)); }
                if (Files.DirectoryExists(*Filename)) { Test.TestTrue(TEXT("Remove exact empty failure fixture"), Files.DeleteDirectory(*Filename, false, false)); }
            }
        }
    };

    // Schema 5 is the migratable layout without PlayerName, MasterVolume, CameraPanCmPerSec and MinimapOrientation, schema 6
    // the one without MasterVolume and the later fields, schema 7 the one without CameraPanCmPerSec and MinimapOrientation,
    // schema 8 the one without MinimapOrientation, schema 9 the one without QuickCast; every other schema writes the current
    // whitelist order.
    constexpr uint32 NamelessFixtureSchema = 5;
    constexpr uint32 VolumelessFixtureSchema = 6;
    constexpr uint32 PanlessFixtureSchema = 7;
    constexpr uint32 OrientationlessFixtureSchema = 8;
    constexpr uint32 QuickCastlessFixtureSchema = 9;

    TArray<uint8> FixtureBytes(const ULDControlSettings& Values, uint32 Schema = ULDControlSettings::CurrentSchema, uint8 Edge = 1, bool Extra = false)
    {
        FBufferArchive Payload;
        uint8 Follow = Values.bStartFollowing ? 1 : 0;
        float Text = Values.TextScale, Zoom = Values.Zoom;
        int32 Count = Values.Bindings.Num();
        Payload << Edge << Follow << Text << Zoom;
        if (Schema != NamelessFixtureSchema) { FString Name = Values.PlayerName; Payload << Name; }
        if (Schema != NamelessFixtureSchema && Schema != VolumelessFixtureSchema) { float Volume = Values.MasterVolume; Payload << Volume; }
        if (Schema != NamelessFixtureSchema && Schema != VolumelessFixtureSchema && Schema != PanlessFixtureSchema)
        { float PanSpeed = Values.CameraPanCmPerSec; Payload << PanSpeed; }
        if (Schema != NamelessFixtureSchema && Schema != VolumelessFixtureSchema && Schema != PanlessFixtureSchema
            && Schema != OrientationlessFixtureSchema)
        { uint8 Orientation = uint8(Values.MinimapOrientation); Payload << Orientation; }
        if (Schema != NamelessFixtureSchema && Schema != VolumelessFixtureSchema && Schema != PanlessFixtureSchema
            && Schema != OrientationlessFixtureSchema && Schema != QuickCastlessFixtureSchema)
        { uint8 Quick = Values.bQuickCast ? 1 : 0; Payload << Quick; }
        Payload << Count;
        for (const auto& Pair : Values.Bindings)
        {
            FString Action = Pair.Key.ToString(), Key = Pair.Value.GetFName().ToString();
            Payload << Action << Key;
        }
        if (Extra) { uint32 ForbiddenPayload = 123; Payload << ForbiddenPayload; }
        FBufferArchive Bytes;
        uint32 Magic = 0x4C444353, Checksum = FCrc::MemCrc32(Payload.GetData(), Payload.Num());
        Bytes << Magic << Schema << Checksum;
        Bytes.Append(Payload);
        return Bytes;
    }

    // Reads the schema field of the actual file, so a migration rewrite is proven on disk rather than in memory.
    bool FixtureFileSchema(const FString& Filename, uint32& Schema)
    {
        TArray<uint8> Bytes;
        if (!FFileHelper::LoadFileToArray(Bytes, *Filename) || Bytes.Num() < 12) { return false; }
        FMemoryReader Reader(Bytes);
        uint32 Magic = 0;
        Reader << Magic << Schema;
        return !Reader.IsError() && Magic == 0x4C444353;
    }

    // Reproduces a file written by the retired schema-4 multi-registry layout, including its old action names.
    TArray<uint8> RetiredRegistryBytes(const TMap<FName, FKey>& Bindings, float TextScale, float Zoom, bool EdgePan)
    {
        FBufferArchive Payload;
        uint8 RetiredSelector = 6, Edge = EdgePan ? 1 : 0, Follow = 1;
        float Text = TextScale, SavedZoom = Zoom;
        int32 Count = Bindings.Num();
        Payload << RetiredSelector << Edge << Follow << Text << SavedZoom << Count;
        for (const auto& Pair : Bindings)
        {
            FString Action = Pair.Key.ToString(), Key = Pair.Value.GetFName().ToString();
            Payload << Action << Key;
        }
        FBufferArchive Bytes;
        uint32 Magic = 0x4C444353, Schema = 4, Checksum = FCrc::MemCrc32(Payload.GetData(), Payload.Num());
        Bytes << Magic << Schema << Checksum;
        Bytes.Append(Payload);
        return Bytes;
    }

    TMap<FName, FKey> RetiredLayoutBindings()
    {
        struct FRetired { const TCHAR* Action; FKey Key; };
        const FRetired Values[] = {
            { TEXT("Select"), EKeys::LeftMouseButton }, { TEXT("Navigate"), EKeys::RightMouseButton }, { TEXT("Attack"), EKeys::A },
            { TEXT("Skill1"), EKeys::F6 }, { TEXT("Skill2"), EKeys::W }, { TEXT("Skill3"), EKeys::E }, { TEXT("Skill4"), EKeys::R },
            { TEXT("Item1"), EKeys::One }, { TEXT("Item2"), EKeys::Two }, { TEXT("Item3"), EKeys::Three }, { TEXT("Item4"), EKeys::Four },
            { TEXT("Stop"), EKeys::S }, { TEXT("Sprint"), EKeys::LeftShift }, { TEXT("Inventory"), EKeys::I }, { TEXT("Development"), EKeys::P },
            { TEXT("PanUp"), EKeys::Up }, { TEXT("PanDown"), EKeys::Down }, { TEXT("PanLeft"), EKeys::Left }, { TEXT("PanRight"), EKeys::Right },
            { TEXT("Recenter"), EKeys::SpaceBar }, { TEXT("Follow"), EKeys::Home }, { TEXT("Zoom"), EKeys::MouseWheelAxis },
            { TEXT("Help"), EKeys::F1 }, { TEXT("Back"), EKeys::Escape } };
        TMap<FName, FKey> Bindings;
        for (const auto& Entry : Values) { Bindings.Add(Entry.Action, Entry.Key); }
        return Bindings;
    }

    const TCHAR* const RetiredActions[] = { TEXT("Attack"), TEXT("Development"), TEXT("Help"), TEXT("Inventory"), TEXT("Sprint"),
        TEXT("Skill3"), TEXT("Skill4"), TEXT("Skill5"), TEXT("Skill6"), TEXT("Item3"), TEXT("Item4"), TEXT("Context"), TEXT("Trade") };

    // Checks the stored map itself: Key() never resolves unregistered names, so it cannot prove a retired entry is absent.
    bool HasExactDemoDefaults(const ULDControlSettings& Settings)
    {
        const auto& Registry = ULDControlSettings::Definitions();
        if (Settings.Bindings.Num() != Registry.Num()) { return false; }
        for (const auto& Binding : Registry)
        {
            const FKey* Bound = Settings.Bindings.Find(Binding.Action);
            if (!Bound || *Bound != Binding.Key) { return false; }
        }
        return true;
    }

    FLDDisplaySettings DisplayFixture()
    {
        FLDDisplaySettings Result;
        Result.Resolution = FIntPoint(1280, 720);
        Result.WindowMode = EWindowMode::Windowed;
        Result.bVSync = false;
        Result.Quality.SetFromSingleQualityLevel(1);
        return Result;
    }

    FString DisplayIni(const FLDDisplaySettings& Value, bool Confirmed = true)
    {
        return FString::Printf(TEXT("[/Script/Engine.GameUserSettings]\nResolutionSizeX=%d\nResolutionSizeY=%d\n")
            TEXT("LastUserConfirmedResolutionSizeX=%d\nLastUserConfirmedResolutionSizeY=%d\n")
            TEXT("FullscreenMode=%d\nLastConfirmedFullscreenMode=%d\nPreferredFullscreenMode=%d\nbUseVSync=%s\n"),
            Value.Resolution.X, Value.Resolution.Y, Confirmed ? Value.Resolution.X : 1, Value.Resolution.Y,
            int32(Value.WindowMode), int32(Value.WindowMode), int32(Value.PreferredFullscreenMode), Value.bVSync ? TEXT("True") : TEXT("False"));
    }

    FString QualityIni(const FLDDisplaySettings& Value)
    {
        const auto& Q = Value.Quality;
        return FString::Printf(TEXT("[ScalabilityGroups]\nsg.ResolutionQuality=%.6f\nsg.ViewDistanceQuality=%d\n")
            TEXT("sg.AntiAliasingQuality=%d\nsg.ShadowQuality=%d\nsg.GlobalIlluminationQuality=%d\n")
            TEXT("sg.ReflectionQuality=%d\nsg.PostProcessQuality=%d\nsg.TextureQuality=%d\n")
            TEXT("sg.EffectsQuality=%d\nsg.FoliageQuality=%d\nsg.ShadingQuality=%d\nsg.LandscapeQuality=%d\n"),
            Q.ResolutionQuality, Q.ViewDistanceQuality, Q.AntiAliasingQuality, Q.ShadowQuality,
            Q.GlobalIlluminationQuality, Q.ReflectionQuality, Q.PostProcessQuality, Q.TextureQuality,
            Q.EffectsQuality, Q.FoliageQuality, Q.ShadingQuality, Q.LandscapeQuality);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDLocalSettingsPersistenceTest, "Lockdown.Rules.LocalSettingsPersistence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDLocalSettingsPersistenceTest::RunTest(const FString& Parameters)
{
    FLocalSettingsFixture Fixture(*this);
    const auto Invalid = FLDLocalSettingsStorage::Automation(FGuid());
    TestTrue(TEXT("Invalid fixture never resolves a real user path"), !Invalid.IsValid() && Invalid.IsAutomation());
    TestTrue(TEXT("Invalid fixture has a typed refusal"), ULDControlSettings::LoadLocalSettings(Invalid).Status == ELDLocalSettingsLoadStatus::InvalidStorage);
    const auto UserStorage = FLDLocalSettingsStorage::User();
    TestTrue(TEXT("The single user file is explicit and is not an automation fixture"), UserStorage.IsValid() && !UserStorage.IsAutomation());
    TestNotEqual(TEXT("Automation storage is distinct from the user file"), Fixture.Storage.Filename(), UserStorage.Filename());

    auto Loaded = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
    TestTrue(TEXT("Missing and Loaded are distinct"), Loaded.Status == ELDLocalSettingsLoadStatus::Missing);
    auto* Applied = Loaded.Settings;
    if (!TestNotNull(TEXT("Missing file supplies a usable current registry"), Applied)) { return false; }
    TestTrue(TEXT("Missing defaults are not called persistence verified"), Applied->GetPhase() == ELDLocalSettingsPhase::ConfirmedApplied);
    TestTrue(TEXT("A missing file never invents a player name"), Applied->PlayerName.IsEmpty());
    auto* Draft = Applied->CreateDraft();
    Draft->TextScale = 1.25f;
    Draft->bEdgePan = false;
    Draft->MinimapOrientation = ELDMinimapOrientation::Facing;
    FString Error;
    TestTrue(TEXT("Draft binding change is legal"), Draft->Rebind(TEXT("Skill1"), EKeys::F6, Error));
    TestTrue(TEXT("EditingDraft cannot save"), Draft->SaveLocalSettings().Status == ELDLocalSettingsSaveStatus::Invalid);
    TestFalse(TEXT("A draft cannot set the applied player name"), Draft->SetAppliedPlayerName(TEXT("阴人王"), Error));
    TestTrue(TEXT("A refused draft name leaves the draft unset"), Draft->PlayerName.IsEmpty());
    TestFalse(TEXT("Draft did not create a file"), IFileManager::Get().FileExists(*Fixture.Storage.Filename()));
    TestEqual(TEXT("Draft does not mutate applied preferences"), Applied->TextScale, 1.f);
    TestTrue(TEXT("Explicit Apply accepts a valid local draft"), Applied->ApplyLocalSettings(*Draft, Error));
    TestTrue(TEXT("ConfirmedApplied is dirty"), Applied->IsDirty());
    TestTrue(TEXT("Save is verified from the actual target file"), Applied->SaveLocalSettings().IsSaved());
    TestTrue(TEXT("Only verified save clears dirty"), Applied->GetPhase() == ELDLocalSettingsPhase::PersistenceVerified);
    Loaded = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
    TestTrue(TEXT("Restart reads confirmed preferences"), Loaded.Status == ELDLocalSettingsLoadStatus::Loaded && Loaded.Settings
        && Loaded.Settings->TextScale == 1.25f && !Loaded.Settings->bEdgePan && Loaded.Settings->Key(TEXT("Skill1")) == EKeys::F6
        && Loaded.Settings->GetMinimapOrientation() == ELDMinimapOrientation::Facing);
    TestTrue(TEXT("An unset (empty) name is valid, saves and reloads as unset"), Loaded.Settings && Loaded.Settings->PlayerName.IsEmpty());
    const FString ChineseName = TEXT("阴人王 小明");
    TestTrue(TEXT("The applied owner accepts a Chinese name with an inner space"), Applied->SetAppliedPlayerName(ChineseName, Error));
    TestTrue(TEXT("A chosen name is applied but not yet saved"), Applied->IsDirty());
    TestTrue(TEXT("The current schema saves the player name"), Applied->SaveLocalSettings().IsSaved());
    Loaded = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
    TestTrue(TEXT("The current schema round trip keeps the exact Chinese name with every other preference"),
        Loaded.Status == ELDLocalSettingsLoadStatus::Loaded && Loaded.Settings
        && Loaded.Settings->PlayerName.Equals(ChineseName, ESearchCase::CaseSensitive)
        && Loaded.Settings->TextScale == 1.25f && Loaded.Settings->Key(TEXT("Skill1")) == EKeys::F6);
    uint32 SavedSchema = 0;
    TestTrue(TEXT("The verified file declares the current schema"),
        FixtureFileSchema(Fixture.Storage.Filename(), SavedSchema) && SavedSchema == uint32(ULDControlSettings::CurrentSchema));
    TestFalse(TEXT("An invalid name is refused on the applied owner"), Applied->SetAppliedPlayerName(TEXT("阴人王?"), Error));
    TestTrue(TEXT("A refused name keeps the applied name and explains why"),
        Applied->PlayerName.Equals(ChineseName, ESearchCase::CaseSensitive) && !Error.IsEmpty());

    TArray<uint8> Before, After;
    TestTrue(TEXT("Read verified bytes"), FFileHelper::LoadFileToArray(Before, *Fixture.Storage.Filename()));
    Draft->Zoom = std::numeric_limits<float>::quiet_NaN();
    TestFalse(TEXT("Apply rejects NaN before mutating owner"), Applied->ApplyLocalSettings(*Draft, Error));
    TestEqual(TEXT("Rejected draft keeps applied zoom"), Applied->Zoom, 1400.f);
    Applied->Bindings.Add(TEXT("RetiredAction"), EKeys::F8);
    TestFalse(TEXT("Raw unknown binding cannot resolve"), Applied->Key(TEXT("RetiredAction")).IsValid());
    TestTrue(TEXT("Unknown binding refuses save"), Applied->SaveLocalSettings().Status == ELDLocalSettingsSaveStatus::Invalid);
    TestTrue(TEXT("Invalid save preserves confirmed disk bytes"), FFileHelper::LoadFileToArray(After, *Fixture.Storage.Filename()) && Before == After);
    Applied->ResetBindings();
    TestTrue(TEXT("Binding recovery does not reset other preferences or the name"), Applied->TextScale == 1.25f && !Applied->bEdgePan
        && Applied->PlayerName.Equals(ChineseName, ESearchCase::CaseSensitive));
    Applied->PlayerName = TEXT(" 阴人王");
    const auto RefusedName = Applied->SaveLocalSettings();
    TestTrue(TEXT("A raw invalid non-empty name refuses save with an explicit name error"),
        RefusedName.Status == ELDLocalSettingsSaveStatus::Invalid && RefusedName.Message.Contains(TEXT("名称")));
    TestTrue(TEXT("Invalid-name save preserves confirmed disk bytes"), FFileHelper::LoadFileToArray(After, *Fixture.Storage.Filename()) && Before == After);
    Applied->PlayerName = ChineseName;

    Applied->RequireRecovery();
    TestTrue(TEXT("Recovery gate prevents automatic replacement"), Applied->SaveLocalSettings().Status == ELDLocalSettingsSaveStatus::RecoveryRequired);
    Applied->AllowRecoverySave();
    TestTrue(TEXT("Explicit recovery can persist valid applied values"), Applied->SaveLocalSettings().IsSaved());
    TestTrue(TEXT("Remove only the fixture's saved file"), IFileManager::Get().Delete(*Fixture.Storage.Filename(), false, false, true));
    TestTrue(TEXT("Load consults disk, not the still-live settings object"),
        ULDControlSettings::LoadLocalSettings(Fixture.Storage).Status == ELDLocalSettingsLoadStatus::Missing);
    TestTrue(TEXT("Create a directory at the exact fixture filename"), IFileManager::Get().MakeDirectory(*Fixture.Storage.Filename(), false));
    TestTrue(TEXT("Unreadable path is ReadFailed, not Missing or defaults"),
        ULDControlSettings::LoadLocalSettings(Fixture.Storage).Status == ELDLocalSettingsLoadStatus::ReadFailed);
    TestTrue(TEXT("Write failure is typed and never marked saved"), Applied->SaveLocalSettings().Status == ELDLocalSettingsSaveStatus::WriteFailed);
    TestTrue(TEXT("Write failure retains applied dirty preferences"), Applied->IsDirty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDLocalSettingsRejectionTest, "Lockdown.Rules.LocalSettingsRejectionAndRecovery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDLocalSettingsRejectionTest::RunTest(const FString& Parameters)
{
    FLocalSettingsFixture Fixture(*this);
    auto* Values = NewObject<ULDControlSettings>();
    Values->ResetBindings();
    Values->TextScale = 1.25f;
    Values->Zoom = 1600;
    auto Write = [&](const TArray<uint8>& Bytes) { return TestTrue(TEXT("Write explicit malformed-data fixture"), FFileHelper::SaveArrayToFile(Bytes, *Fixture.Storage.Filename())); };
    auto ExpectRejected = [&](ELDLocalSettingsLoadStatus Expected)
    {
        const auto Result = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
        TestTrue(TEXT("Load reports the exact rejection family"), Result.Status == Expected);
        TestNull(TEXT("Rejected data is not returned as an applied settings object"), Result.Settings);
    };
    Write(FixtureBytes(*Values, ULDControlSettings::CurrentSchema + 1));
    ExpectRejected(ELDLocalSettingsLoadStatus::UnsupportedSchema);
    Write(FixtureBytes(*Values, 3));
    ExpectRejected(ELDLocalSettingsLoadStatus::UnsupportedSchema);
    Write(FixtureBytes(*Values, ULDControlSettings::CurrentSchema, 2));
    ExpectRejected(ELDLocalSettingsLoadStatus::Corrupt);
    Write(FixtureBytes(*Values, ULDControlSettings::CurrentSchema, 1, true));
    ExpectRejected(ELDLocalSettingsLoadStatus::Corrupt);
    auto Corrupt = FixtureBytes(*Values);
    Corrupt.Last() ^= 1;
    Write(Corrupt);
    ExpectRejected(ELDLocalSettingsLoadStatus::Corrupt);
    TArray<uint8> Readback;
    TestTrue(TEXT("Corrupt load leaves the user's bytes untouched"),
        FFileHelper::LoadFileToArray(Readback, *Fixture.Storage.Filename()) && Readback == Corrupt);
    TArray<uint8> Oversized;
    Oversized.Init(0, 65537);
    Write(Oversized);
    ExpectRejected(ELDLocalSettingsLoadStatus::Corrupt);
    for (float BadZoom : { 1099.f, 1701.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() })
    {
        Values->Zoom = BadZoom;
        Write(FixtureBytes(*Values));
        ExpectRejected(ELDLocalSettingsLoadStatus::Corrupt);
    }
    Values->Zoom = 1600;
    Values->TextScale = .99f;
    Write(FixtureBytes(*Values));
    ExpectRejected(ELDLocalSettingsLoadStatus::Corrupt);
    Values->TextScale = 1.25f;
    for (FKey InvalidKey : { EKeys::Gamepad_FaceButton_Bottom, EKeys::MouseScrollDown, EKeys::Escape, EKeys::W })
    {
        Values->Bindings[TEXT("Skill1")] = InvalidKey;
        Write(FixtureBytes(*Values));
        ExpectRejected(ELDLocalSettingsLoadStatus::Corrupt);
    }
    Values->ResetBindings();
    // Local name rule: Chinese, letters, digits and an inner space up to the limit; nothing that can break the ?Name= option.
    FString NameError, Smile, LoneSurrogate = TEXT("王");
    Smile.AppendChar(TCHAR(0xD83D));
    Smile.AppendChar(TCHAR(0xDE00));
    LoneSurrogate.AppendChar(TCHAR(0xD83D));
    for (const FString& ValidName : { FString(TEXT("阴人王")), FString(TEXT("Player 1")),
        FString::ChrN(ULDControlSettings::MaxPlayerNameChars, TEXT('王')), FString::ChrN(ULDControlSettings::MaxPlayerNameChars - 1, TEXT('王')) + Smile })
    { TestTrue(TEXT("A valid name passes the local rule; a surrogate pair counts as one character"), ULDControlSettings::ValidatePlayerName(ValidName, NameError) && NameError.IsEmpty()); }
    for (const FString& InvalidName : { FString(), FString(TEXT(" 阴人王")), FString(TEXT("阴人王 ")), FString::ChrN(ULDControlSettings::MaxPlayerNameChars + 1, TEXT('王')),
        FString(TEXT("a?b")), FString(TEXT("a=b")), FString(TEXT("a#b")), FString(TEXT("a/b")), FString(TEXT("a\\b")), FString(TEXT("a:b")), FString(TEXT("a%b")),
        FString(TEXT("a&b")), FString(TEXT("a\"b")), FString(TEXT("a'b")), FString(TEXT("a<b")), FString(TEXT("a>b")), FString(TEXT("a|b")), FString(TEXT("a\tb")), LoneSurrogate })
    {
        TestFalse(TEXT("An empty, padded, overlong, reserved-character, control or broken name fails the local rule"), ULDControlSettings::ValidatePlayerName(InvalidName, NameError));
        TestFalse(TEXT("Every name rejection explains itself"), NameError.IsEmpty());
    }
    for (const TCHAR* BadName : { TEXT(" 阴人王"), TEXT("阴人王?"), TEXT("a\tb") })
    {
        Values->PlayerName = BadName;
        Write(FixtureBytes(*Values));
        const auto NameRejected = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
        TestTrue(TEXT("A saved invalid name rejects the whole file explicitly, without recovery data"),
            NameRejected.Status == ELDLocalSettingsLoadStatus::Corrupt && !NameRejected.Settings && !NameRejected.RecoverablePreferences
            && NameRejected.Message.Contains(TEXT("名称")));
    }
    FString NulName = TEXT("ab");
    NulName[1] = TCHAR(0);
    Values->PlayerName = NulName;
    Write(FixtureBytes(*Values));
    ExpectRejected(ELDLocalSettingsLoadStatus::Corrupt);
    Values->PlayerName = TEXT("阴人王");
    Values->Bindings.Remove(TEXT("Attributes"));
    Values->Bindings.Add(TEXT("Development"), EKeys::P);
    const auto UnknownBindings = FixtureBytes(*Values, ULDControlSettings::CurrentSchema, 0);
    Write(UnknownBindings);
    const auto Rejected = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
    TestTrue(TEXT("Retired binding names have recovery data, never success"),
        Rejected.Status == ELDLocalSettingsLoadStatus::Corrupt && !Rejected.Settings && Rejected.RecoverablePreferences);
    TestTrue(TEXT("The rejection names the retired and missing actions"),
        Rejected.Message.Contains(TEXT("Development")) && Rejected.Message.Contains(TEXT("Attributes")));
    TestTrue(TEXT("Recovery data never carries a rejected binding name"),
        Rejected.RecoverablePreferences && !Rejected.RecoverablePreferences->Bindings.Contains(TEXT("Development")));
    TestTrue(TEXT("Recovery data keeps the validated player name"),
        Rejected.RecoverablePreferences && Rejected.RecoverablePreferences->PlayerName.Equals(TEXT("阴人王"), ESearchCase::CaseSensitive));
    auto* Instance = NewObject<ULDGameInstance>();
    auto* Recovered = Instance->UseIsolatedLocalSettings(Fixture.Id);
    TestTrue(TEXT("Recovery preserves validated unrelated preferences"),
        Recovered->TextScale == 1.25f && Recovered->Zoom == 1600 && !Recovered->bEdgePan);
    TestTrue(TEXT("Recovery keeps the validated player name in the session settings"),
        Recovered->PlayerName.Equals(TEXT("阴人王"), ESearchCase::CaseSensitive));
    TestTrue(TEXT("Runtime bindings require explicit replacement approval"), Recovered->NeedsRecovery() && Recovered->Valid());
    TestFalse(TEXT("Retired action cannot reappear at runtime"), Recovered->Bindings.Contains(TEXT("Development")));
    TestTrue(TEXT("Session bindings are exactly the Demo defaults, including the missing action"), HasExactDemoDefaults(*Recovered));
    TestTrue(TEXT("Travel does not silently replace a corrupt file"),
        Instance->FlushLocalSettings().Status == ELDLocalSettingsSaveStatus::RecoveryRequired);
    TestTrue(TEXT("Rejected auto-flush kept exact bad bytes"),
        FFileHelper::LoadFileToArray(Readback, *Fixture.Storage.Filename()) && Readback == UnknownBindings);
    TestTrue(TEXT("Explicit recovery succeeds"), Instance->ConfirmLocalSettingsRecovery().IsSaved());
    const auto Confirmed = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
    TestTrue(TEXT("Recovered file keeps camera/text and current registry"),
        Confirmed.Status == ELDLocalSettingsLoadStatus::Loaded && Confirmed.Settings && Confirmed.Settings->Zoom == 1600
        && Confirmed.Settings->TextScale == 1.25f && !Confirmed.Settings->bEdgePan && Confirmed.Settings->Valid());
    TestTrue(TEXT("Confirmed recovery writes the validated player name back"),
        Confirmed.Settings && Confirmed.Settings->PlayerName.Equals(TEXT("阴人王"), ESearchCase::CaseSensitive));

    // A schema-4 file from the retired multi-registry layout is incompatible: nothing is migrated, prefs use recovery.
    auto RetiredBindings = RetiredLayoutBindings();
    const auto RetiredFile = RetiredRegistryBytes(RetiredBindings, 1.25f, 1650, false);
    Write(RetiredFile);
    const auto Retired = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
    TestTrue(TEXT("Retired registry schema is an explicit incompatibility, never Loaded"),
        Retired.Status == ELDLocalSettingsLoadStatus::UnsupportedSchema && !Retired.Settings);
    for (const TCHAR* Name : { TEXT("Attack"), TEXT("Development"), TEXT("Help"), TEXT("Inventory"), TEXT("Item3"),
        TEXT("Item4"), TEXT("Skill3"), TEXT("Skill4"), TEXT("Sprint") })
    { TestTrue(TEXT("The incompatibility names every retired action"), Retired.Message.Contains(Name)); }
    TestTrue(TEXT("Retired bindings are not migrated into recovery data"), Retired.RecoverablePreferences
        && Retired.RecoverablePreferences->Valid() && HasExactDemoDefaults(*Retired.RecoverablePreferences));
    auto* RetiredInstance = NewObject<ULDGameInstance>();
    auto* RetiredSession = RetiredInstance->UseIsolatedLocalSettings(Fixture.Id);
    TestTrue(TEXT("Valid camera and text preferences from the retired file are kept for the session"),
        RetiredSession->TextScale == 1.25f && RetiredSession->Zoom == 1650 && !RetiredSession->bEdgePan);
    TestTrue(TEXT("Retired layout requires explicit replacement"), RetiredSession->NeedsRecovery() && RetiredSession->Valid());
    TestTrue(TEXT("No surviving action name is partially migrated"), HasExactDemoDefaults(*RetiredSession));
    for (const TCHAR* Name : RetiredActions)
    { TestFalse(TEXT("Retired actions stay absent from the binding map after recovery"), RetiredSession->Bindings.Contains(Name)); }
    TestTrue(TEXT("Automatic flush keeps the retired file byte-identical"),
        RetiredInstance->FlushLocalSettings().Status == ELDLocalSettingsSaveStatus::RecoveryRequired
        && FFileHelper::LoadFileToArray(Readback, *Fixture.Storage.Filename()) && Readback == RetiredFile);
    TestTrue(TEXT("Confirmed recovery replaces it with the current schema"), RetiredInstance->ConfirmLocalSettingsRecovery().IsSaved());
    const auto Replaced = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
    TestTrue(TEXT("Replacement loads with kept preferences and Demo bindings"),
        Replaced.Status == ELDLocalSettingsLoadStatus::Loaded && Replaced.Settings && Replaced.Settings->Zoom == 1650
        && Replaced.Settings->Key(TEXT("Skill1")) == EKeys::Q && !Replaced.Settings->Bindings.Contains(TEXT("Sprint")));
    Write(RetiredRegistryBytes(RetiredBindings, 1.25f, 9999, false));
    const auto RetiredInvalid = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
    TestTrue(TEXT("Invalid preferences in a retired file are not offered for recovery"),
        RetiredInvalid.Status == ELDLocalSettingsLoadStatus::UnsupportedSchema && !RetiredInvalid.Settings && !RetiredInvalid.RecoverablePreferences);
    auto DamagedRetired = RetiredRegistryBytes(RetiredBindings, 1.25f, 1650, false);
    DamagedRetired.Last() ^= 1;
    Write(DamagedRetired);
    ExpectRejected(ELDLocalSettingsLoadStatus::Corrupt);

    // Schema 5 predates PlayerName: explicit migration keeps its valid bindings and preferences and leaves the name unset.
    auto* Previous = NewObject<ULDControlSettings>();
    Previous->ResetBindings();
    Previous->Bindings[TEXT("Skill1")] = EKeys::F6;
    Previous->TextScale = 1.25f;
    Previous->Zoom = 1550;
    Previous->bStartFollowing = false;
    const auto PreviousFile = FixtureBytes(*Previous, NamelessFixtureSchema, 0);
    Write(PreviousFile);
    const auto Migrated = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
    TestTrue(TEXT("A schema 5 file is an explicit successful migration, never plain Loaded"),
        Migrated.Status == ELDLocalSettingsLoadStatus::Migrated && Migrated.Settings && !Migrated.RecoverablePreferences);
    TestTrue(TEXT("Migration keeps every valid binding and preference"), Migrated.Settings && Migrated.Settings->Valid()
        && Migrated.Settings->Key(TEXT("Skill1")) == EKeys::F6 && Migrated.Settings->TextScale == 1.25f && Migrated.Settings->Zoom == 1550
        && !Migrated.Settings->bEdgePan && !Migrated.Settings->bStartFollowing);
    TestTrue(TEXT("Migration never invents a player name"), Migrated.Settings && Migrated.Settings->PlayerName.IsEmpty());
    TestTrue(TEXT("Migrated values are applied and dirty, not persistence verified or recovery gated"), Migrated.Settings
        && Migrated.Settings->GetPhase() == ELDLocalSettingsPhase::ConfirmedApplied && !Migrated.Settings->NeedsRecovery());
    TestTrue(TEXT("The migration message asks for a name before Host or Join"),
        Migrated.Message.Contains(TEXT("迁移")) && Migrated.Message.Contains(TEXT("名称")));
    TestTrue(TEXT("Loading never rewrites the schema 5 file"),
        FFileHelper::LoadFileToArray(Readback, *Fixture.Storage.Filename()) && Readback == PreviousFile);
    auto* MigratedInstance = NewObject<ULDGameInstance>();
    auto* MigratedSession = MigratedInstance->UseIsolatedLocalSettings(Fixture.Id);
    TestTrue(TEXT("The game instance activates the migrated values without a recovery gate"), MigratedSession
        && !MigratedSession->NeedsRecovery() && MigratedSession->IsDirty() && MigratedSession->Key(TEXT("Skill1")) == EKeys::F6
        && MigratedSession->Zoom == 1550 && MigratedSession->PlayerName.IsEmpty());
    TestEqual(TEXT("The game instance reports the migration message"), MigratedInstance->GetLocalSettingsMessage(), Migrated.Message);
    TestTrue(TEXT("The next flush writes the migrated values"), MigratedInstance->FlushLocalSettings().IsSaved());
    uint32 UpgradedSchema = 0;
    TestTrue(TEXT("The flushed file uses the current schema"),
        FixtureFileSchema(Fixture.Storage.Filename(), UpgradedSchema) && UpgradedSchema == uint32(ULDControlSettings::CurrentSchema));
    const auto Upgraded = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
    TestTrue(TEXT("The rewritten file loads as current with the migrated values and an unset name"),
        Upgraded.Status == ELDLocalSettingsLoadStatus::Loaded && Upgraded.Settings && Upgraded.Settings->Key(TEXT("Skill1")) == EKeys::F6
        && Upgraded.Settings->Zoom == 1550 && !Upgraded.Settings->bStartFollowing && Upgraded.Settings->PlayerName.IsEmpty());
    Previous->Bindings.Add(TEXT("Development"), EKeys::F9);
    Write(FixtureBytes(*Previous, NamelessFixtureSchema, 0));
    const auto PreviousRejected = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
    TestTrue(TEXT("A schema 5 file with a retired action keeps the existing rejection and recovery path"),
        PreviousRejected.Status == ELDLocalSettingsLoadStatus::Corrupt && !PreviousRejected.Settings && PreviousRejected.RecoverablePreferences
        && PreviousRejected.RecoverablePreferences->PlayerName.IsEmpty() && PreviousRejected.Message.Contains(TEXT("Development")));
    Previous->Bindings.Remove(TEXT("Development"));
    Previous->Zoom = 9999;
    Write(FixtureBytes(*Previous, NamelessFixtureSchema, 0));
    const auto PreviousInvalid = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
    TestTrue(TEXT("Invalid schema 5 preferences are rejected, never migrated or offered for recovery"),
        PreviousInvalid.Status == ELDLocalSettingsLoadStatus::Corrupt && !PreviousInvalid.Settings && !PreviousInvalid.RecoverablePreferences);
    auto DamagedPrevious = PreviousFile;
    DamagedPrevious.Last() ^= 1;
    Write(DamagedPrevious);
    ExpectRejected(ELDLocalSettingsLoadStatus::Corrupt);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDLocalSettingsMasterVolumeTest, "Lockdown.Rules.LocalSettingsMasterVolume",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDLocalSettingsMasterVolumeTest::RunTest(const FString& Parameters)
{
    FLocalSettingsFixture Fixture(*this);
    auto Write = [&](const TArray<uint8>& Bytes) { return TestTrue(TEXT("Write explicit volume fixture"), FFileHelper::SaveArrayToFile(Bytes, *Fixture.Storage.Filename())); };
    auto OnDisk = [&](const TArray<uint8>& Expected) { TArray<uint8> Actual; return FFileHelper::LoadFileToArray(Actual, *Fixture.Storage.Filename()) && Actual == Expected; };
    uint32 SavedSchema = 0;
    FString Error;

    // The current schema stores the volume itself: save, then restore it exactly.
    auto* Applied = ULDControlSettings::CreateSessionSettings(Fixture.Storage);
    TestEqual(TEXT("The default master volume is full, without attenuation"), Applied->MasterVolume, 1.f);
    TestTrue(TEXT("The applied owner accepts a non-default volume and marks it unsaved"),
        Applied->SetAppliedMasterVolume(.35f, Error) && Error.IsEmpty() && Applied->IsDirty());
    TestTrue(TEXT("The volume is saved and verified from disk"), Applied->SaveLocalSettings().IsSaved());
    TestTrue(TEXT("The verified file declares the current schema"),
        FixtureFileSchema(Fixture.Storage.Filename(), SavedSchema) && SavedSchema == uint32(ULDControlSettings::CurrentSchema));
    const auto Loaded = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
    TestTrue(TEXT("A current-schema round trip restores the exact volume as persistence verified"),
        Loaded.Status == ELDLocalSettingsLoadStatus::Loaded && Loaded.Settings && Loaded.Settings->MasterVolume == .35f
        && Loaded.Settings->GetPhase() == ELDLocalSettingsPhase::PersistenceVerified);

    // Only finite 0..1 values reach the applied owner; a draft never takes one.
    auto* Draft = Applied->CreateDraft();
    TestFalse(TEXT("A draft cannot set the applied volume"), Draft->SetAppliedMasterVolume(.5f, Error));
    TestTrue(TEXT("A refused draft volume is explained and leaves the draft unchanged"), !Error.IsEmpty() && Draft->MasterVolume == .35f);
    for (float Invalid : { std::numeric_limits<float>::quiet_NaN(), -.01f, 1.01f, std::numeric_limits<float>::infinity() })
    {
        TestFalse(TEXT("A non-finite or out-of-range volume is refused"), Applied->SetAppliedMasterVolume(Invalid, Error));
        TestTrue(TEXT("A refused volume is explained and keeps the applied value"), !Error.IsEmpty() && Applied->MasterVolume == .35f);
    }
    TestTrue(TEXT("Refused volumes never mark the verified owner dirty"), Applied->GetPhase() == ELDLocalSettingsPhase::PersistenceVerified);
    TestTrue(TEXT("Silence is a supported volume"), Applied->SetAppliedMasterVolume(0.f, Error) && Applied->MasterVolume == 0.f);
    TestTrue(TEXT("Full volume is a supported volume"), Applied->SetAppliedMasterVolume(1.f, Error) && Applied->MasterVolume == 1.f);

    // Restoring local defaults covers the volume but never the player name.
    TestTrue(TEXT("Prepare a chosen name"), Applied->SetAppliedPlayerName(TEXT("阴人王"), Error));
    TestTrue(TEXT("Prepare a non-default volume"), Applied->SetAppliedMasterVolume(.2f, Error));
    Applied->ResetLocalDefaults();
    TestTrue(TEXT("Restoring local defaults returns the volume to full and keeps the player name"),
        Applied->MasterVolume == GetDefault<ULDControlSettings>()->MasterVolume && Applied->MasterVolume == 1.f
        && Applied->PlayerName.Equals(TEXT("阴人王"), ESearchCase::CaseSensitive) && Applied->Valid());

    // A saved volume outside 0..1 rejects the whole file like an out-of-range zoom, and the file is left alone.
    auto* Values = NewObject<ULDControlSettings>();
    Values->ResetBindings();
    Values->PlayerName = TEXT("阴人王");
    for (float BadVolume : { 1.5f, -.01f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() })
    {
        Values->MasterVolume = BadVolume;
        const auto BadVolumeFile = FixtureBytes(*Values);
        Write(BadVolumeFile);
        const auto BadVolumeLoad = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
        TestTrue(TEXT("An invalid saved volume makes the file Corrupt, without settings or recovery data"),
            BadVolumeLoad.Status == ELDLocalSettingsLoadStatus::Corrupt && !BadVolumeLoad.Settings && !BadVolumeLoad.RecoverablePreferences
            && BadVolumeLoad.Message.Contains(TEXT("主音量")));
        TestTrue(TEXT("Rejecting a saved volume leaves the file byte-identical"), OnDisk(BadVolumeFile));
    }

    // Schema 6 has the name but no volume: an explicit migration keeps what it stored and uses the default volume.
    Values->Bindings[TEXT("Skill1")] = EKeys::F6;
    Values->Zoom = 1550;
    // Never written by the schema 6 layout; the migration must not pick it up from anywhere.
    Values->MasterVolume = .2f;
    const auto VolumelessFile = FixtureBytes(*Values, VolumelessFixtureSchema, 0);
    Write(VolumelessFile);
    const auto Migrated = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
    TestTrue(TEXT("A schema 6 file is an explicit successful migration, never plain Loaded"),
        Migrated.Status == ELDLocalSettingsLoadStatus::Migrated && Migrated.Settings && !Migrated.RecoverablePreferences);
    TestTrue(TEXT("The migration keeps the name, bindings and camera preferences and uses the full default volume"), Migrated.Settings
        && Migrated.Settings->MasterVolume == 1.f && Migrated.Settings->PlayerName.Equals(TEXT("阴人王"), ESearchCase::CaseSensitive)
        && Migrated.Settings->Key(TEXT("Skill1")) == EKeys::F6 && Migrated.Settings->Zoom == 1550 && !Migrated.Settings->bEdgePan
        && Migrated.Settings->Valid());
    TestTrue(TEXT("Migrated values are applied and dirty, not persistence verified or recovery gated"),
        Migrated.Settings && Migrated.Settings->IsDirty() && !Migrated.Settings->NeedsRecovery());
    TestTrue(TEXT("The schema 6 migration message names the default volume"),
        Migrated.Message.Contains(TEXT("迁移")) && Migrated.Message.Contains(TEXT("主音量使用默认值 100%")));
    TestTrue(TEXT("Loading never rewrites the schema 6 file"), OnDisk(VolumelessFile));
    TestTrue(TEXT("The next verified save writes the migrated values"), Migrated.Settings && Migrated.Settings->SaveLocalSettings().IsSaved());
    TestTrue(TEXT("The rewritten file declares the current schema"),
        FixtureFileSchema(Fixture.Storage.Filename(), SavedSchema) && SavedSchema == uint32(ULDControlSettings::CurrentSchema));
    const auto Upgraded = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
    TestTrue(TEXT("The rewritten file loads as current with the kept name and the default volume"),
        Upgraded.Status == ELDLocalSettingsLoadStatus::Loaded && Upgraded.Settings && Upgraded.Settings->MasterVolume == 1.f
        && Upgraded.Settings->PlayerName.Equals(TEXT("阴人王"), ESearchCase::CaseSensitive)
        && Upgraded.Settings->Key(TEXT("Skill1")) == EKeys::F6 && Upgraded.Settings->Zoom == 1550);
    Values->Bindings.Add(TEXT("Development"), EKeys::F9);
    Write(FixtureBytes(*Values, VolumelessFixtureSchema, 0));
    const auto VolumelessRejected = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
    TestTrue(TEXT("A schema 6 file with a retired action keeps the rejection and recovery path, with the default volume"),
        VolumelessRejected.Status == ELDLocalSettingsLoadStatus::Corrupt && !VolumelessRejected.Settings && VolumelessRejected.RecoverablePreferences
        && VolumelessRejected.RecoverablePreferences->MasterVolume == 1.f && VolumelessRejected.Message.Contains(TEXT("Development"))
        && VolumelessRejected.RecoverablePreferences->PlayerName.Equals(TEXT("阴人王"), ESearchCase::CaseSensitive));
    Values->Bindings.Remove(TEXT("Development"));
    auto DamagedVolumeless = VolumelessFile;
    DamagedVolumeless.Last() ^= 1;
    Write(DamagedVolumeless);
    TestTrue(TEXT("A damaged schema 6 file is Corrupt"),
        ULDControlSettings::LoadLocalSettings(Fixture.Storage).Status == ELDLocalSettingsLoadStatus::Corrupt);

    // Schema 5 (no name, no volume) still migrates explicitly.
    const auto NamelessFile = FixtureBytes(*Values, NamelessFixtureSchema, 0);
    Write(NamelessFile);
    const auto Nameless = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
    TestTrue(TEXT("A schema 5 file still migrates with the default volume and an unset name"),
        Nameless.Status == ELDLocalSettingsLoadStatus::Migrated && Nameless.Settings && Nameless.Settings->MasterVolume == 1.f
        && Nameless.Settings->PlayerName.IsEmpty() && Nameless.Settings->Key(TEXT("Skill1")) == EKeys::F6 && Nameless.Settings->IsDirty());
    TestTrue(TEXT("The schema 5 migration message names the default volume and still asks for a name"),
        Nameless.Message.Contains(TEXT("主音量使用默认值 100%")) && Nameless.Message.Contains(TEXT("名称")));
    TestTrue(TEXT("Loading never rewrites the schema 5 file"), OnDisk(NamelessFile));

    // The game instance applies a slider value to the live owner at once and leaves the write to its debounced flush.
    auto* Instance = NewObject<ULDGameInstance>();
    TestNull(TEXT("A game instance that never ran Init has no audio subsystem"), Instance->GetSubsystem<ULDAudioSubsystem>());
    auto* Session = Instance->UseIsolatedLocalSettings(Fixture.Id);
    if (!TestNotNull(TEXT("The isolated fixture supplies the applied settings"), Session)) { return false; }
    TestTrue(TEXT("The migrated values flush as the current schema and leave the owner clean"), Instance->FlushLocalSettings().IsSaved() && !Session->IsDirty());
    TArray<uint8> Verified;
    TestTrue(TEXT("Read the verified bytes"), FFileHelper::LoadFileToArray(Verified, *Fixture.Storage.Filename()));
    const auto Changed = Instance->SetAppliedMasterVolume(.6f);
    TestTrue(TEXT("Without an audio subsystem the volume still applies to the live owner and marks it unsaved"),
        Changed.Status == ELDLocalSettingsSaveStatus::NoChanges && Changed.Message.Contains(TEXT("60%"))
        && Session->MasterVolume == .6f && Session->IsDirty());
    TestEqual(TEXT("The game instance reports the volume message"), Instance->GetLocalSettingsMessage(), Changed.Message);
    TestTrue(TEXT("A slider value waits for the debounced flush instead of writing the file"),
        Instance->IsLocalSettingsFlushPending() && OnDisk(Verified));
    const auto Refused = Instance->SetAppliedMasterVolume(std::numeric_limits<float>::quiet_NaN());
    TestTrue(TEXT("A non-finite volume is refused explicitly and keeps the applied value"),
        Refused.Status == ELDLocalSettingsSaveStatus::Invalid && !Refused.Message.IsEmpty() && Session->MasterVolume == .6f);
    TestTrue(TEXT("The flush writes the applied volume"), Instance->FlushLocalSettings().IsSaved() && !Session->IsDirty());
    const auto Flushed = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
    TestTrue(TEXT("Restart restores the flushed volume"),
        Flushed.Status == ELDLocalSettingsLoadStatus::Loaded && Flushed.Settings && Flushed.Settings->MasterVolume == .6f);
    auto* ResetDraft = Session->CreateDraft();
    ResetDraft->ResetLocalDefaults();
    TestTrue(TEXT("Restoring defaults through the game instance applies and saves the default volume"),
        Instance->ApplyLocalSettings(*ResetDraft).IsSaved() && Session->MasterVolume == 1.f);

    // Recovery data from a rejected file carries its validated volume into the session; the gated file stays unchanged.
    Values->MasterVolume = .4f;
    Values->Bindings.Add(TEXT("Development"), EKeys::F9);
    const auto RejectedFile = FixtureBytes(*Values, ULDControlSettings::CurrentSchema, 0);
    Write(RejectedFile);
    auto* RecoveryInstance = NewObject<ULDGameInstance>();
    auto* Recovered = RecoveryInstance->UseIsolatedLocalSettings(Fixture.Id);
    if (!TestNotNull(TEXT("The rejected fixture still supplies session settings"), Recovered)) { return false; }
    TestTrue(TEXT("Recovery keeps the validated volume for the session and requires explicit replacement"),
        Recovered->MasterVolume == .4f && Recovered->NeedsRecovery());
    const auto Gated = RecoveryInstance->SetAppliedMasterVolume(.5f);
    TestTrue(TEXT("A recovery-gated volume change applies without promising an automatic save"),
        Gated.Status == ELDLocalSettingsSaveStatus::NoChanges && Recovered->MasterVolume == .5f && !Gated.Message.Contains(TEXT("稍后自动保存")));
    TestTrue(TEXT("The gated flush keeps the rejected file byte-identical"),
        RecoveryInstance->FlushLocalSettings().Status == ELDLocalSettingsSaveStatus::RecoveryRequired && OnDisk(RejectedFile));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDLocalSettingsCameraPanSpeedTest, "Lockdown.Rules.LocalSettingsCameraPanSpeed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDLocalSettingsCameraPanSpeedTest::RunTest(const FString& Parameters)
{
    FLocalSettingsFixture Fixture(*this);
    auto Write = [&](const TArray<uint8>& Bytes) { return TestTrue(TEXT("Write explicit camera pan fixture"), FFileHelper::SaveArrayToFile(Bytes, *Fixture.Storage.Filename())); };
    auto OnDisk = [&](const TArray<uint8>& Expected) { TArray<uint8> Actual; return FFileHelper::LoadFileToArray(Actual, *Fixture.Storage.Filename()) && Actual == Expected; };
    // The coordinator's default; the domain belongs to the UI settings module, not to the content catalog.
    const float DefaultPan = 1800;
    uint32 SavedSchema = 0;
    FString Error;

    // The current schema stores the camera-only pan speed: save, then restore it exactly.
    TestTrue(TEXT("The camera pan domain is 900..3600 cm/s"),
        ULDControlSettings::MinCameraPanCmPerSec == 900.f && ULDControlSettings::MaxCameraPanCmPerSec == 3600.f);
    auto* Applied = ULDControlSettings::CreateSessionSettings(Fixture.Storage);
    TestTrue(TEXT("The default camera pan speed is 1800 cm/s"),
        Applied->GetCameraPanCmPerSec() == DefaultPan && GetDefault<ULDControlSettings>()->CameraPanCmPerSec == DefaultPan);
    TestTrue(TEXT("The applied owner accepts a legal pan speed and marks it unsaved"),
        Applied->SetAppliedCameraPanSpeed(2700, Error) && Error.IsEmpty() && Applied->IsDirty() && Applied->GetCameraPanCmPerSec() == 2700.f);
    TestTrue(TEXT("The pan speed is saved and verified from disk"), Applied->SaveLocalSettings().IsSaved());
    TestTrue(TEXT("The verified file declares the current schema"),
        FixtureFileSchema(Fixture.Storage.Filename(), SavedSchema) && SavedSchema == uint32(ULDControlSettings::CurrentSchema));
    const auto Loaded = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
    TestTrue(TEXT("A current-schema round trip restores the exact pan speed as persistence verified"),
        Loaded.Status == ELDLocalSettingsLoadStatus::Loaded && Loaded.Settings && Loaded.Settings->GetCameraPanCmPerSec() == 2700.f
        && Loaded.Settings->GetPhase() == ELDLocalSettingsPhase::PersistenceVerified);
    for (float Bound : { ULDControlSettings::MinCameraPanCmPerSec, ULDControlSettings::MaxCameraPanCmPerSec })
    {
        TestTrue(TEXT("Both domain bounds are legal pan speeds"), Applied->SetAppliedCameraPanSpeed(Bound, Error) && Applied->GetCameraPanCmPerSec() == Bound);
        TestTrue(TEXT("A domain bound is saved and verified"), Applied->SaveLocalSettings().IsSaved());
        const auto BoundLoad = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
        TestTrue(TEXT("A domain bound reloads exactly"),
            BoundLoad.Status == ELDLocalSettingsLoadStatus::Loaded && BoundLoad.Settings && BoundLoad.Settings->GetCameraPanCmPerSec() == Bound);
    }

    // Only finite values inside the domain reach the applied owner; a draft never takes one.
    auto* Draft = Applied->CreateDraft();
    TestFalse(TEXT("A draft cannot set the applied pan speed"), Draft->SetAppliedCameraPanSpeed(1200, Error));
    TestTrue(TEXT("A refused draft pan speed is explained and leaves the draft unchanged"), !Error.IsEmpty() && Draft->CameraPanCmPerSec == 3600.f);
    for (float Invalid : { 899.f, 3601.f, 0.f, -1800.f, std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() })
    {
        TestFalse(TEXT("A non-finite or out-of-domain pan speed is refused"), Applied->SetAppliedCameraPanSpeed(Invalid, Error));
        TestTrue(TEXT("A refused pan speed is explained and keeps the applied value"), !Error.IsEmpty() && Applied->CameraPanCmPerSec == 3600.f);
    }
    TestTrue(TEXT("Refused pan speeds never mark the verified owner dirty"), Applied->GetPhase() == ELDLocalSettingsPhase::PersistenceVerified);
    for (float Invalid : { std::numeric_limits<float>::quiet_NaN(), 3900.f })
    {
        Draft->CameraPanCmPerSec = Invalid;
        TestFalse(TEXT("Apply refuses a draft carrying a non-finite or out-of-domain pan speed"), Applied->ApplyLocalSettings(*Draft, Error));
        TestTrue(TEXT("The refusal names the pan speed and keeps the verified applied value"), Error.Contains(TEXT("镜头平移速度"))
            && Applied->CameraPanCmPerSec == 3600.f && Applied->GetPhase() == ELDLocalSettingsPhase::PersistenceVerified);
    }

    // Restoring local defaults covers the pan speed but never the player name.
    TestTrue(TEXT("Prepare a chosen name"), Applied->SetAppliedPlayerName(TEXT("阴人王"), Error));
    Applied->ResetLocalDefaults();
    TestTrue(TEXT("Restoring local defaults returns the pan speed to 1800 cm/s and keeps the player name"),
        Applied->GetCameraPanCmPerSec() == DefaultPan && Applied->PlayerName.Equals(TEXT("阴人王"), ESearchCase::CaseSensitive) && Applied->Valid());

    // A saved pan speed outside the domain rejects the whole file like an out-of-range zoom or volume; the file is left alone.
    auto* Values = NewObject<ULDControlSettings>();
    Values->ResetBindings();
    Values->PlayerName = TEXT("阴人王");
    for (float BadPan : { 899.f, 3601.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() })
    {
        Values->CameraPanCmPerSec = BadPan;
        const auto BadPanFile = FixtureBytes(*Values);
        Write(BadPanFile);
        const auto BadPanLoad = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
        TestTrue(TEXT("An invalid saved pan speed makes the file Corrupt, without settings or recovery data"),
            BadPanLoad.Status == ELDLocalSettingsLoadStatus::Corrupt && !BadPanLoad.Settings && !BadPanLoad.RecoverablePreferences
            && BadPanLoad.Message.Contains(TEXT("镜头平移速度")));
        TestTrue(TEXT("Rejecting a saved pan speed leaves the file byte-identical"), OnDisk(BadPanFile));
    }

    // Schema 7 has the volume but no pan speed: an explicit migration keeps everything it stored and uses the default.
    Values->Bindings[TEXT("Skill1")] = EKeys::F6;
    Values->Zoom = 1550;
    Values->MasterVolume = .4f;
    // Never written by the schema 7 layout; the migration must not pick it up from anywhere.
    Values->CameraPanCmPerSec = 3000;
    const auto PanlessFile = FixtureBytes(*Values, PanlessFixtureSchema, 0);
    Write(PanlessFile);
    const auto Migrated = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
    TestTrue(TEXT("A schema 7 file is an explicit successful migration, never plain Loaded"),
        Migrated.Status == ELDLocalSettingsLoadStatus::Migrated && Migrated.Settings && !Migrated.RecoverablePreferences);
    TestTrue(TEXT("The migration keeps the name, volume, bindings and camera preferences and uses the default pan speed"), Migrated.Settings
        && Migrated.Settings->GetCameraPanCmPerSec() == DefaultPan && Migrated.Settings->MasterVolume == .4f
        && Migrated.Settings->PlayerName.Equals(TEXT("阴人王"), ESearchCase::CaseSensitive)
        && Migrated.Settings->Key(TEXT("Skill1")) == EKeys::F6 && Migrated.Settings->Zoom == 1550 && !Migrated.Settings->bEdgePan
        && Migrated.Settings->Valid());
    TestTrue(TEXT("Migrated values are applied and dirty, not persistence verified or recovery gated"), Migrated.Settings
        && Migrated.Settings->GetPhase() == ELDLocalSettingsPhase::ConfirmedApplied && !Migrated.Settings->NeedsRecovery());
    TestTrue(TEXT("The schema 7 migration message names the default pan speed"),
        Migrated.Message.Contains(TEXT("迁移")) && Migrated.Message.Contains(TEXT("镜头平移速度使用默认值 1800 cm/s")));
    TestTrue(TEXT("Loading never rewrites the schema 7 file"), OnDisk(PanlessFile));
    TestTrue(TEXT("The next verified save writes the migrated values"), Migrated.Settings && Migrated.Settings->SaveLocalSettings().IsSaved());
    TestTrue(TEXT("The rewritten file declares the current schema"),
        FixtureFileSchema(Fixture.Storage.Filename(), SavedSchema) && SavedSchema == uint32(ULDControlSettings::CurrentSchema));
    const auto Upgraded = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
    TestTrue(TEXT("The rewritten file loads as current with the kept values and the default pan speed"),
        Upgraded.Status == ELDLocalSettingsLoadStatus::Loaded && Upgraded.Settings && Upgraded.Settings->GetCameraPanCmPerSec() == DefaultPan
        && Upgraded.Settings->MasterVolume == .4f && Upgraded.Settings->PlayerName.Equals(TEXT("阴人王"), ESearchCase::CaseSensitive)
        && Upgraded.Settings->Key(TEXT("Skill1")) == EKeys::F6 && Upgraded.Settings->Zoom == 1550);
    Values->Bindings.Add(TEXT("Development"), EKeys::F9);
    Write(FixtureBytes(*Values, PanlessFixtureSchema, 0));
    const auto PanlessRejected = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
    TestTrue(TEXT("A schema 7 file with a retired action keeps the rejection and recovery path, with the default pan speed"),
        PanlessRejected.Status == ELDLocalSettingsLoadStatus::Corrupt && !PanlessRejected.Settings && PanlessRejected.RecoverablePreferences
        && PanlessRejected.RecoverablePreferences->GetCameraPanCmPerSec() == DefaultPan
        && PanlessRejected.RecoverablePreferences->MasterVolume == .4f && PanlessRejected.Message.Contains(TEXT("Development")));
    Values->Bindings.Remove(TEXT("Development"));
    auto DamagedPanless = PanlessFile;
    DamagedPanless.Last() ^= 1;
    Write(DamagedPanless);
    TestTrue(TEXT("A damaged schema 7 file is Corrupt"),
        ULDControlSettings::LoadLocalSettings(Fixture.Storage).Status == ELDLocalSettingsLoadStatus::Corrupt);

    // Schemas 5 and 6 still migrate explicitly, with the default pan speed as well as the default volume.
    for (const uint32 Older : { NamelessFixtureSchema, VolumelessFixtureSchema })
    {
        const auto OlderFile = FixtureBytes(*Values, Older, 0);
        Write(OlderFile);
        const auto OlderLoad = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
        TestTrue(TEXT("An older schema still migrates with the default pan speed and the default volume"),
            OlderLoad.Status == ELDLocalSettingsLoadStatus::Migrated && OlderLoad.Settings && OlderLoad.Settings->GetCameraPanCmPerSec() == DefaultPan
            && OlderLoad.Settings->MasterVolume == 1.f && OlderLoad.Settings->Key(TEXT("Skill1")) == EKeys::F6 && OlderLoad.Settings->IsDirty());
        TestTrue(TEXT("The older migration message names the default pan speed"),
            OlderLoad.Message.Contains(TEXT("迁移")) && OlderLoad.Message.Contains(TEXT("镜头平移速度使用默认值 1800 cm/s")));
        TestTrue(TEXT("Loading never rewrites an older file"), OnDisk(OlderFile));
    }

    // Recovery data from a rejected current file carries its validated pan speed into the session; the gated file stays unchanged.
    Values->CameraPanCmPerSec = 2400;
    Values->Bindings.Add(TEXT("Development"), EKeys::F9);
    const auto RejectedFile = FixtureBytes(*Values, ULDControlSettings::CurrentSchema, 0);
    Write(RejectedFile);
    auto* RecoveryInstance = NewObject<ULDGameInstance>();
    auto* Recovered = RecoveryInstance->UseIsolatedLocalSettings(Fixture.Id);
    if (!TestNotNull(TEXT("The rejected fixture still supplies session settings"), Recovered)) { return false; }
    TestTrue(TEXT("Recovery keeps the validated pan speed for the session and requires explicit replacement"),
        Recovered->GetCameraPanCmPerSec() == 2400.f && Recovered->NeedsRecovery());
    TestTrue(TEXT("The gated flush keeps the rejected file byte-identical"),
        RecoveryInstance->FlushLocalSettings().Status == ELDLocalSettingsSaveStatus::RecoveryRequired && OnDisk(RejectedFile));
    TestTrue(TEXT("Confirmed recovery writes the kept pan speed in the current schema"), RecoveryInstance->ConfirmLocalSettingsRecovery().IsSaved());
    const auto Replaced = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
    TestTrue(TEXT("The replacement restores the kept pan speed"),
        Replaced.Status == ELDLocalSettingsLoadStatus::Loaded && Replaced.Settings && Replaced.Settings->GetCameraPanCmPerSec() == 2400.f);
    auto* ResetDraft = Recovered->CreateDraft();
    ResetDraft->ResetLocalDefaults();
    TestTrue(TEXT("Restoring defaults through the game instance applies and saves the default pan speed"),
        RecoveryInstance->ApplyLocalSettings(*ResetDraft).IsSaved() && Recovered->GetCameraPanCmPerSec() == DefaultPan);
    const auto Restored = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
    TestTrue(TEXT("Restart restores the default pan speed and keeps the player name"),
        Restored.Status == ELDLocalSettingsLoadStatus::Loaded && Restored.Settings && Restored.Settings->GetCameraPanCmPerSec() == DefaultPan
        && Restored.Settings->PlayerName.Equals(TEXT("阴人王"), ESearchCase::CaseSensitive));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDLocalSettingsMinimapOrientationTest, "Lockdown.Rules.LocalSettingsMinimapOrientation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDLocalSettingsMinimapOrientationTest::RunTest(const FString& Parameters)
{
    FLocalSettingsFixture Fixture(*this);
    auto Write = [&](const TArray<uint8>& Bytes) { return TestTrue(TEXT("Write explicit minimap orientation fixture"), FFileHelper::SaveArrayToFile(Bytes, *Fixture.Storage.Filename())); };
    auto OnDisk = [&](const TArray<uint8>& Expected) { TArray<uint8> Actual; return FFileHelper::LoadFileToArray(Actual, *Fixture.Storage.Filename()) && Actual == Expected; };
    const ELDMinimapOrientation Camera = ELDMinimapOrientation::Camera, Facing = ELDMinimapOrientation::Facing, North = ELDMinimapOrientation::North;
    uint32 SavedSchema = 0;
    FString Error;

    // Schema 9 added the minimap orientation; 跟随镜头 is the user's default.
    TestTrue(TEXT("Every orientation has its player-facing name and no other value has one"),
        ULDControlSettings::MinimapOrientationLabel(Camera).Equals(TEXT("跟随镜头"), ESearchCase::CaseSensitive)
        && ULDControlSettings::MinimapOrientationLabel(Facing).Equals(TEXT("跟随角色朝向"), ESearchCase::CaseSensitive)
        && ULDControlSettings::MinimapOrientationLabel(North).Equals(TEXT("固定正北"), ESearchCase::CaseSensitive)
        && ULDControlSettings::MinimapOrientationLabel(static_cast<ELDMinimapOrientation>(3)).IsEmpty());
    auto* Applied = ULDControlSettings::CreateSessionSettings(Fixture.Storage);
    TestTrue(TEXT("The default minimap orientation follows the camera"),
        Applied->GetMinimapOrientation() == Camera && GetDefault<ULDControlSettings>()->MinimapOrientation == Camera);
    for (const ELDMinimapOrientation Value : { Facing, North, Camera })
    {
        TestTrue(TEXT("The applied owner accepts every orientation and marks it unsaved"),
            Applied->SetAppliedMinimapOrientation(Value, Error) && Error.IsEmpty() && Applied->IsDirty() && Applied->GetMinimapOrientation() == Value);
        TestTrue(TEXT("The orientation is saved and verified from disk"), Applied->SaveLocalSettings().IsSaved());
        TestTrue(TEXT("The verified file declares the current schema"), FixtureFileSchema(Fixture.Storage.Filename(), SavedSchema) && SavedSchema == uint32(ULDControlSettings::CurrentSchema));
        const auto Loaded = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
        TestTrue(TEXT("A current-schema round trip restores the exact orientation as persistence verified"),
            Loaded.Status == ELDLocalSettingsLoadStatus::Loaded && Loaded.Settings && Loaded.Settings->GetMinimapOrientation() == Value
            && Loaded.Settings->GetPhase() == ELDLocalSettingsPhase::PersistenceVerified);
    }

    // Only the three orientations reach the applied owner; a draft never takes one.
    TestTrue(TEXT("Prepare a verified 固定正北"), Applied->SetAppliedMinimapOrientation(North, Error) && Applied->SaveLocalSettings().IsSaved());
    auto* Draft = Applied->CreateDraft();
    TestFalse(TEXT("A draft cannot set the applied orientation"), Draft->SetAppliedMinimapOrientation(Facing, Error));
    TestTrue(TEXT("A refused draft orientation is explained and leaves the draft unchanged"), !Error.IsEmpty() && Draft->MinimapOrientation == North);
    for (const uint8 Invalid : { uint8(3), uint8(255) })
    {
        TestFalse(TEXT("An unknown orientation is refused"), Applied->SetAppliedMinimapOrientation(static_cast<ELDMinimapOrientation>(Invalid), Error));
        TestTrue(TEXT("A refused orientation is explained and keeps the applied value"), !Error.IsEmpty() && Applied->MinimapOrientation == North);
        Draft->MinimapOrientation = static_cast<ELDMinimapOrientation>(Invalid);
        TestFalse(TEXT("Apply refuses a draft carrying an unknown orientation"), Applied->ApplyLocalSettings(*Draft, Error));
        TestTrue(TEXT("The refusal names the minimap orientation and keeps the verified applied value"), Error.Contains(TEXT("小地图朝向"))
            && Applied->MinimapOrientation == North && Applied->GetPhase() == ELDLocalSettingsPhase::PersistenceVerified);
    }

    // Restoring local defaults returns to 跟随镜头 but never clears the player name.
    TestTrue(TEXT("Prepare a chosen name"), Applied->SetAppliedPlayerName(TEXT("阴人王"), Error));
    Applied->ResetLocalDefaults();
    TestTrue(TEXT("Restoring local defaults returns the orientation to Camera and keeps the player name"),
        Applied->GetMinimapOrientation() == Camera && Applied->PlayerName.Equals(TEXT("阴人王"), ESearchCase::CaseSensitive) && Applied->Valid());

    // A stored orientation outside the enumeration rejects the whole file like an out-of-range zoom; the file is left alone.
    auto* Values = NewObject<ULDControlSettings>();
    Values->ResetBindings();
    Values->PlayerName = TEXT("阴人王");
    for (const uint8 Invalid : { uint8(3), uint8(255) })
    {
        Values->MinimapOrientation = static_cast<ELDMinimapOrientation>(Invalid);
        const auto BadFile = FixtureBytes(*Values);
        Write(BadFile);
        const auto BadLoad = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
        TestTrue(TEXT("An invalid saved orientation makes the file Corrupt, without settings or recovery data"),
            BadLoad.Status == ELDLocalSettingsLoadStatus::Corrupt && !BadLoad.Settings && !BadLoad.RecoverablePreferences
            && BadLoad.Message.Contains(TEXT("小地图朝向")));
        TestTrue(TEXT("Rejecting a saved orientation leaves the file byte-identical"), OnDisk(BadFile));
    }

    // Schema 8 has the pan speed but no orientation: an explicit migration keeps everything it stored and uses 跟随镜头.
    Values->Bindings[TEXT("Skill1")] = EKeys::F6;
    Values->Zoom = 1550;
    Values->MasterVolume = .4f;
    Values->CameraPanCmPerSec = 2700;
    // Never written by the schema 8 layout; the migration must not pick it up from anywhere.
    Values->MinimapOrientation = North;
    const auto OrientationlessFile = FixtureBytes(*Values, OrientationlessFixtureSchema, 0);
    Write(OrientationlessFile);
    const auto Migrated = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
    TestTrue(TEXT("A schema 8 file is an explicit successful migration, never plain Loaded"),
        Migrated.Status == ELDLocalSettingsLoadStatus::Migrated && Migrated.Settings && !Migrated.RecoverablePreferences);
    TestTrue(TEXT("The migration keeps the name, volume, pan speed, bindings and camera preferences and uses Camera"), Migrated.Settings
        && Migrated.Settings->GetMinimapOrientation() == Camera && Migrated.Settings->GetCameraPanCmPerSec() == 2700.f
        && Migrated.Settings->MasterVolume == .4f && Migrated.Settings->PlayerName.Equals(TEXT("阴人王"), ESearchCase::CaseSensitive)
        && Migrated.Settings->Key(TEXT("Skill1")) == EKeys::F6 && Migrated.Settings->Zoom == 1550 && !Migrated.Settings->bEdgePan
        && Migrated.Settings->Valid());
    TestTrue(TEXT("Migrated values are applied and dirty, not persistence verified or recovery gated"), Migrated.Settings
        && Migrated.Settings->GetPhase() == ELDLocalSettingsPhase::ConfirmedApplied && !Migrated.Settings->NeedsRecovery());
    TestTrue(TEXT("The schema 8 migration message names the default orientation"),
        Migrated.Message.Contains(TEXT("迁移")) && Migrated.Message.Contains(TEXT("小地图朝向使用默认值“跟随镜头”")));
    TestTrue(TEXT("Loading never rewrites the schema 8 file"), OnDisk(OrientationlessFile));
    TestTrue(TEXT("The next verified save writes the migrated values"), Migrated.Settings && Migrated.Settings->SaveLocalSettings().IsSaved());
    TestTrue(TEXT("The rewritten file declares the current schema"), FixtureFileSchema(Fixture.Storage.Filename(), SavedSchema) && SavedSchema == uint32(ULDControlSettings::CurrentSchema));
    const auto Upgraded = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
    TestTrue(TEXT("The rewritten file loads as current with the kept values and Camera"),
        Upgraded.Status == ELDLocalSettingsLoadStatus::Loaded && Upgraded.Settings && Upgraded.Settings->GetMinimapOrientation() == Camera
        && Upgraded.Settings->GetCameraPanCmPerSec() == 2700.f && Upgraded.Settings->Key(TEXT("Skill1")) == EKeys::F6);
    Values->Bindings.Add(TEXT("Development"), EKeys::F9);
    Write(FixtureBytes(*Values, OrientationlessFixtureSchema, 0));
    const auto OrientationlessRejected = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
    TestTrue(TEXT("A schema 8 file with a retired action keeps the rejection and recovery path, with Camera"),
        OrientationlessRejected.Status == ELDLocalSettingsLoadStatus::Corrupt && !OrientationlessRejected.Settings
        && OrientationlessRejected.RecoverablePreferences && OrientationlessRejected.RecoverablePreferences->GetMinimapOrientation() == Camera
        && OrientationlessRejected.RecoverablePreferences->GetCameraPanCmPerSec() == 2700.f
        && OrientationlessRejected.Message.Contains(TEXT("Development")));
    Values->Bindings.Remove(TEXT("Development"));
    auto DamagedOrientationless = OrientationlessFile;
    DamagedOrientationless.Last() ^= 1;
    Write(DamagedOrientationless);
    TestTrue(TEXT("A damaged schema 8 file is Corrupt"),
        ULDControlSettings::LoadLocalSettings(Fixture.Storage).Status == ELDLocalSettingsLoadStatus::Corrupt);

    // Schemas 5, 6 and 7 still migrate explicitly, with Camera as well as their older defaults.
    for (const uint32 Older : { NamelessFixtureSchema, VolumelessFixtureSchema, PanlessFixtureSchema })
    {
        const auto OlderFile = FixtureBytes(*Values, Older, 0);
        Write(OlderFile);
        const auto OlderLoad = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
        TestTrue(TEXT("An older schema still migrates with Camera and the default pan speed"),
            OlderLoad.Status == ELDLocalSettingsLoadStatus::Migrated && OlderLoad.Settings && OlderLoad.Settings->GetMinimapOrientation() == Camera
            && OlderLoad.Settings->GetCameraPanCmPerSec() == 1800.f && OlderLoad.Settings->Key(TEXT("Skill1")) == EKeys::F6 && OlderLoad.Settings->IsDirty());
        TestTrue(TEXT("The older migration message names the default orientation"),
            OlderLoad.Message.Contains(TEXT("迁移")) && OlderLoad.Message.Contains(TEXT("小地图朝向使用默认值“跟随镜头”")));
        TestTrue(TEXT("Loading never rewrites an older file"), OnDisk(OlderFile));
    }

    // Recovery data from a rejected current file carries its validated orientation into the session; the gated file stays unchanged.
    Values->MinimapOrientation = Facing;
    Values->Bindings.Add(TEXT("Development"), EKeys::F9);
    const auto RejectedFile = FixtureBytes(*Values, ULDControlSettings::CurrentSchema, 0);
    Write(RejectedFile);
    auto* RecoveryInstance = NewObject<ULDGameInstance>();
    auto* Recovered = RecoveryInstance->UseIsolatedLocalSettings(Fixture.Id);
    if (!TestNotNull(TEXT("The rejected fixture still supplies session settings"), Recovered)) { return false; }
    TestTrue(TEXT("Recovery keeps the validated orientation for the session and requires explicit replacement"),
        Recovered->GetMinimapOrientation() == Facing && Recovered->NeedsRecovery());
    TestTrue(TEXT("The gated flush keeps the rejected file byte-identical"),
        RecoveryInstance->FlushLocalSettings().Status == ELDLocalSettingsSaveStatus::RecoveryRequired && OnDisk(RejectedFile));
    TestTrue(TEXT("Confirmed recovery writes the kept orientation in the current schema"), RecoveryInstance->ConfirmLocalSettingsRecovery().IsSaved());
    const auto Replaced = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
    TestTrue(TEXT("The replacement restores the kept orientation"),
        Replaced.Status == ELDLocalSettingsLoadStatus::Loaded && Replaced.Settings && Replaced.Settings->GetMinimapOrientation() == Facing);
    auto* ResetDraft = Recovered->CreateDraft();
    ResetDraft->ResetLocalDefaults();
    TestTrue(TEXT("Restoring defaults through the game instance applies and saves Camera"),
        RecoveryInstance->ApplyLocalSettings(*ResetDraft).IsSaved() && Recovered->GetMinimapOrientation() == Camera);
    const auto Restored = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
    TestTrue(TEXT("Restart restores Camera and keeps the player name"),
        Restored.Status == ELDLocalSettingsLoadStatus::Loaded && Restored.Settings && Restored.Settings->GetMinimapOrientation() == Camera
        && Restored.Settings->PlayerName.Equals(TEXT("阴人王"), ESearchCase::CaseSensitive));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDLocalControlRegistryTest, "Lockdown.Rules.LocalControlRegistryIsolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDLocalControlRegistryTest::RunTest(const FString& Parameters)
{
    const TCHAR* const Expected[] = { TEXT("Select"), TEXT("Navigate"), TEXT("AttackMove"), TEXT("Skill1"), TEXT("Skill2"),
        TEXT("Item1"), TEXT("Item2"), TEXT("Stop"), TEXT("Attributes"), TEXT("PanUp"), TEXT("PanDown"), TEXT("PanLeft"),
        TEXT("PanRight"), TEXT("Recenter"), TEXT("Follow"), TEXT("Zoom"), TEXT("Back") };
    const auto& Registry = ULDControlSettings::Definitions();
    if (!TestEqual(TEXT("One Demo registry holds exactly the Spec action set"), Registry.Num(), int32(UE_ARRAY_COUNT(Expected)))) { return false; }
    TSet<FKey> DefaultKeys;
    for (int32 Index = 0; Index < Registry.Num(); ++Index)
    {
        const auto& Binding = Registry[Index];
        TestTrue(TEXT("Registry order and action names are fixed"), Binding.Action == FName(Expected[Index]));
        TestFalse(TEXT("Every action has a visible label"), Binding.Label.IsEmpty());
        TestFalse(TEXT("Default keys never collide"), DefaultKeys.Contains(Binding.Key));
        TestTrue(TEXT("Only Zoom is an axis"), Binding.Axis == (Binding.Action == TEXT("Zoom")));
        DefaultKeys.Add(Binding.Key);
    }
    auto* Settings = ULDControlSettings::CreateSessionSettings(FLDLocalSettingsStorage::Automation(FGuid::NewGuid()));
    TestTrue(TEXT("Default registry is complete and conflict free"), Settings->Valid());
    TestTrue(TEXT("Spec defaults: Q/W abilities, 1/2 equipment, A attack-move, P attributes, S stop, Esc back"),
        Settings->Key(TEXT("Skill1")) == EKeys::Q && Settings->Key(TEXT("Skill2")) == EKeys::W
        && Settings->Key(TEXT("Item1")) == EKeys::One && Settings->Key(TEXT("Item2")) == EKeys::Two
        && Settings->Key(TEXT("AttackMove")) == EKeys::A && Settings->Key(TEXT("Attributes")) == EKeys::P
        && Settings->Key(TEXT("Stop")) == EKeys::S && Settings->Key(TEXT("Back")) == EKeys::Escape);
    FString Error;
    for (const TCHAR* Name : RetiredActions)
    {
        TestFalse(TEXT("Default bindings never contain a retired action"), Settings->Bindings.Contains(Name));
        TestFalse(TEXT("Retired action cannot be rebound"), Settings->Rebind(Name, EKeys::F9, Error));
        Settings->Bindings.Add(Name, EKeys::F9);
        TestFalse(TEXT("A retired name injected into the raw map still resolves to no key"), Settings->Key(Name).IsValid());
        TestFalse(TEXT("A raw map carrying a retired name is invalid"), Settings->Valid());
        Settings->Bindings.Remove(Name);
    }
    TestTrue(TEXT("Raw-map probes leave exactly the Demo defaults"), HasExactDemoDefaults(*Settings));
    const auto Before = Settings->Bindings;
    TestFalse(TEXT("Conflicting key is rejected, never relocated"), Settings->Rebind(TEXT("PanUp"), EKeys::Q, Error));
    TestTrue(TEXT("Rejected rebind preserves every binding"), Settings->Bindings.OrderIndependentCompareEqual(Before));
    TestTrue(TEXT("The applied owner accepts a chosen name"), Settings->SetAppliedPlayerName(TEXT("阴人王"), Error));
    Settings->TextScale = 1.25f;
    Settings->Zoom = 1600;
    Settings->MinimapOrientation = ELDMinimapOrientation::North;
    Settings->ResetBindings();
    TestTrue(TEXT("Resetting bindings preserves unrelated preferences and the name"), Settings->TextScale == 1.25f && Settings->Zoom == 1600
        && Settings->MinimapOrientation == ELDMinimapOrientation::North && Settings->PlayerName.Equals(TEXT("阴人王"), ESearchCase::CaseSensitive));
    Settings->ResetLocalDefaults();
    const auto* Defaults = GetDefault<ULDControlSettings>();
    TestTrue(TEXT("Restoring local defaults resets keys, camera, text and the minimap orientation but never deletes the player name"),
        Settings->TextScale == Defaults->TextScale && Settings->Zoom == Defaults->Zoom && HasExactDemoDefaults(*Settings)
        && Settings->MinimapOrientation == Defaults->MinimapOrientation
        && Settings->PlayerName.Equals(TEXT("阴人王"), ESearchCase::CaseSensitive) && Settings->Valid());
    auto* Draft = Settings->CreateDraft();
    Draft->Bindings.Add(TEXT("Sprint"), EKeys::LeftShift);
    TestFalse(TEXT("A draft carrying a retired action cannot be applied"), Settings->ApplyLocalSettings(*Draft, Error));
    TestTrue(TEXT("The rejection names the retired action"), Error.Contains(TEXT("Sprint")));
    TestFalse(TEXT("Rejected apply leaves the owner unchanged"), Settings->Bindings.Contains(TEXT("Sprint")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDLocalDisplayTrialTest, "Lockdown.Rules.LocalDisplayTrialLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDLocalDisplayTrialTest::RunTest(const FString& Parameters)
{
    FLocalSettingsFixture Fixture(*this);
    auto* Instance = NewObject<ULDGameInstance>();
    auto* Applied = Instance->UseIsolatedLocalSettings(Fixture.Id);
    auto Before = DisplayFixture(), Draft = Before;
    Draft.Resolution = FIntPoint(1920, 1080);
    Draft.bVSync = true;
    Draft.Quality.SetFromSingleQualityLevel(3);
    FLDDisplayTrialState Trial;
    const uint64 First = Trial.Begin(Before, Draft, 10., 15.);
    TestTrue(TEXT("A valid trial has an identity"), First != 0);
    TestEqual(TEXT("A second trial cannot replace an active one"), Trial.Begin(Before, Draft, 11., 15.), uint64(0));
    TestFalse(TEXT("No early timeout"), Trial.IsExpired(24.999));
    TestTrue(TEXT("Timeout includes the exact deadline"), Trial.IsExpired(25.));
    TestTrue(TEXT("Original VSync and every quality field are retained separately"),
        Trial.Baseline() == Before && Trial.Candidate() == Draft);
    FString Error;
    TestTrue(TEXT("A legal applied zoom during trial is retained"), Applied->SetAppliedZoom(1650, Error));
    Applied->SetAppliedFollowing(false);
    TestTrue(TEXT("Cancel invalidates the trial once"), Trial.Invalidate(First));
    TestFalse(TEXT("Repeated cancel is inert"), Trial.Invalidate(First));
    const uint64 Second = Trial.Begin(Before, Draft, 30., 15.);
    TestTrue(TEXT("Trial IDs never repeat"), Second > First);
    TestFalse(TEXT("Old confirmation cannot target a newer trial"), Trial.IsCurrent(First));
    TestFalse(TEXT("Old cancellation cannot cancel a newer trial"), Trial.Invalidate(First));
    TestTrue(TEXT("New trial remains current after stale callbacks"), Trial.IsCurrent(Second));
    TestFalse(TEXT("Old timeout does not expire a newer trial"), Trial.IsExpired(25.));
    TestTrue(TEXT("Nonfinite clock is fail-closed"), Trial.IsExpired(std::numeric_limits<double>::quiet_NaN()));
    Trial.Invalidate(Second);
    TestEqual(TEXT("Nonfinite timeout cannot begin"), Trial.Begin(Before, Draft, 50., std::numeric_limits<double>::infinity()), uint64(0));
    TestEqual(TEXT("Zero timeout cannot begin"), Trial.Begin(Before, Draft, 50., 0.), uint64(0));
    auto* LocalDraft = Applied->CreateDraft();
    LocalDraft->Zoom = 1100;
    Instance->RequestLocalSettingsFlush();
    Instance->PrepareLocalSettingsForTravel();
    const auto Restarted = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
    TestTrue(TEXT("Travel flushes latest applied zoom/follow without waiting for a world or widget timer"),
        Restarted.Status == ELDLocalSettingsLoadStatus::Loaded && Restarted.Settings
        && Restarted.Settings->Zoom == 1650 && !Restarted.Settings->bStartFollowing);
    TestTrue(TEXT("An uncommitted draft remains unsavable after travel"),
        LocalDraft->SaveLocalSettings().Status == ELDLocalSettingsSaveStatus::Invalid);
    TestTrue(TEXT("Isolated automation never claims a real display trial"),
        Instance->BeginDisplayTrial(Draft, Instance).Status == ELDDisplaySettingsStatus::Unavailable);
    TestTrue(TEXT("No trial cannot be confirmed as applied"),
        Instance->ConfirmDisplayTrial(1).Status == ELDDisplaySettingsStatus::StaleTrial);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDLocalDisplayProductDefaultsTest, "Lockdown.Rules.LocalDisplayProductDefaults",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDLocalDisplayProductDefaultsTest::RunTest(const FString& Parameters)
{
    auto* VSync = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync"));
    int32 EngineVSync = 0;
    if (!TestNotNull(TEXT("The engine VSync console variable exists"), VSync)
        || !TestTrue(TEXT("The engine VSync default is a number"), LexTryParseString(EngineVSync, *VSync->GetDefaultValue()))) { return false; }
    // A detached live owner whose every restorable value differs from the product defaults; no real viewport is touched.
    auto* Live = NewObject<UGameUserSettings>();
    Live->SetScreenResolution(FIntPoint(1280, 720));
    Live->SetFullscreenMode(EWindowMode::Windowed);
    Live->ConfirmVideoMode();
    Live->SetVSyncEnabled(EngineVSync == 0);
    Live->ScalabilityQuality.SetFromSingleQualityLevel(0);
    const auto Before = FLDDisplaySettings::Read(*Live);
    const FIntPoint Desktop(2560, 1440);
    FLDDisplaySettings Defaults;
    FString Error;
    if (!TestTrue(TEXT("Product defaults resolve for a known desktop size"),
        FLDDisplaySettings::ProductDefaults(*Live, Desktop, Defaults, Error) && Error.IsEmpty())) { return false; }
    TestTrue(TEXT("Computing the defaults never modifies the live settings owner"),
        FLDDisplaySettings::Read(*Live) == Before && Live->GetLastConfirmedScreenResolution() == FIntPoint(1280, 720));
    TestTrue(TEXT("The window mode is the engine default"), Defaults.WindowMode == UGameUserSettings::GetDefaultWindowMode());
    TestTrue(TEXT("The unset default resolution in borderless mode is the desktop size"), Defaults.Resolution == Desktop);
    TestTrue(TEXT("VSync is the engine's r.VSync default, not the live value"),
        Defaults.bVSync == (EngineVSync != 0) && Defaults.bVSync != Before.bVSync);
    Scalability::FQualityLevels EngineQuality;
    EngineQuality.SetDefaults();
    const auto& Q = Defaults.Quality;
    TestTrue(TEXT("Every quality group is the engine default level"),
        Q.ViewDistanceQuality == EngineQuality.ViewDistanceQuality && Q.AntiAliasingQuality == EngineQuality.AntiAliasingQuality
        && Q.ShadowQuality == EngineQuality.ShadowQuality && Q.GlobalIlluminationQuality == EngineQuality.GlobalIlluminationQuality
        && Q.ReflectionQuality == EngineQuality.ReflectionQuality && Q.PostProcessQuality == EngineQuality.PostProcessQuality
        && Q.TextureQuality == EngineQuality.TextureQuality && Q.EffectsQuality == EngineQuality.EffectsQuality
        && Q.FoliageQuality == EngineQuality.FoliageQuality && Q.ShadingQuality == EngineQuality.ShadingQuality
        && Q.LandscapeQuality == EngineQuality.LandscapeQuality);
    TestTrue(TEXT("The default quality is not the live Low preset"), Q != Before.Quality && Q.GetSingleQualityLevel() != 0);
    TestTrue(TEXT("The fullscreen shortcut preference stays the live one"), Defaults.PreferredFullscreenMode == Before.PreferredFullscreenMode);
    TestTrue(TEXT("The product defaults are valid display settings"), Defaults.IsValid());
    TestFalse(TEXT("Product defaults are not the current values that ResetToCurrentSettings keeps"), Defaults == Before);
    TestFalse(TEXT("Restoring this mode needs the keep/revert display trial"), Defaults.SameMode(Before));
    auto Unchanged = Before;
    TestFalse(TEXT("An unknown desktop size is refused instead of inventing a resolution"),
        FLDDisplaySettings::ProductDefaults(*Live, FIntPoint::ZeroValue, Unchanged, Error));
    TestTrue(TEXT("The refusal is explained and leaves the output untouched"), !Error.IsEmpty() && Unchanged == Before);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDLocalDisplayRenderScaleTest, "Lockdown.Rules.LocalDisplayRenderScale",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDLocalDisplayRenderScaleTest::RunTest(const FString& Parameters)
{
    // Issue #18. A private scalability variable stands in for r.ScreenPercentage, so other tests never see a changed scale.
    auto& Console = IConsoleManager::Get();
    const TCHAR* Name = TEXT("ld.Test.RenderScale");
    if (IConsoleVariable* Stale = Console.FindConsoleVariable(Name)) { Console.UnregisterConsoleObject(Stale, false); }
    IConsoleVariable* Scale = Console.RegisterConsoleVariable(Name, 0.f, TEXT("Lockdown render scale test variable."), ECVF_Scalability);
    if (!TestNotNull(TEXT("The test render scale registers"), Scale)) { return false; }
    ON_SCOPE_EXIT { Console.UnregisterConsoleObject(Scale, false); };
    Scale->Set(50.f, ECVF_SetByScalability);
    TestTrue(TEXT("A non-default quality keeps its scale: the engine applies it"),
        LDLocalSettings::ApplyRenderScale(*Scale, 50.f) && Scale->GetFloat() == 50.f);
    TestTrue(TEXT("The default (0) drops the earlier scalability scale at once"),
        LDLocalSettings::ApplyRenderScale(*Scale, 0.f) && Scale->GetFloat() == 0.f
        && (Scale->GetFlags() & ECVF_SetByMask) == ECVF_SetByConstructor);
    TestTrue(TEXT("Applying the default again is harmless"), LDLocalSettings::ApplyRenderScale(*Scale, 0.f) && Scale->GetFloat() == 0.f);
    Scale->Set(50.f, ECVF_SetByScalability);
    Scale->Set(80.f, ECVF_SetByProjectSetting);
    TestTrue(TEXT("A project screen percentage outranks scalability and is what the default returns to"),
        Scale->GetFloat() == 80.f && LDLocalSettings::ApplyRenderScale(*Scale, 0.f) && Scale->GetFloat() == 80.f
        && (Scale->GetFlags() & ECVF_SetByMask) == ECVF_SetByProjectSetting);

    // The real engine path: sg.ResolutionQuality 0 never touches r.ScreenPercentage, which is the defect being fixed.
    IConsoleVariable* Screen = Console.FindConsoleVariable(TEXT("r.ScreenPercentage"));
    if (!TestNotNull(TEXT("The engine screen percentage exists"), Screen)) { return false; }
    if ((Screen->GetFlags() & ECVF_SetByMask) > ECVF_SetByScalability)
    {
        AddInfo(TEXT("r.ScreenPercentage is set above scalability in this process; the engine-path reproduction is skipped."));
        return true;
    }
    const Scalability::FQualityLevels Saved = Scalability::GetQualityLevels();
    const float Baseline = Screen->GetFloat();
    ON_SCOPE_EXIT
    {
        Scalability::SetQualityLevels(Saved);
        if (Saved.ResolutionQuality == 0.f) { LDLocalSettings::ApplyRenderScale(*Screen, 0.f); }
    };
    if (Saved.ResolutionQuality != 0.f) { Scalability::FQualityLevels Reset = Saved; Reset.ResolutionQuality = 0.f; Scalability::SetQualityLevels(Reset); }
    LDLocalSettings::ApplyRenderScale(*Screen, 0.f);
    const float ProjectDefault = Screen->GetFloat();
    Scalability::FQualityLevels Low = Saved;
    Low.ResolutionQuality = ProjectDefault == 50.f ? 60.f : 50.f;
    Scalability::SetQualityLevels(Low);
    TestEqual(TEXT("A quality level's render scale reaches r.ScreenPercentage"), Screen->GetFloat(), Low.ResolutionQuality);
    Scalability::FQualityLevels Defaults = Low;
    Defaults.ResolutionQuality = 0.f;
    Scalability::SetQualityLevels(Defaults);
    TestEqual(TEXT("The engine leaves the earlier render scale in force for the default (0)"), Screen->GetFloat(), Low.ResolutionQuality);
    TestTrue(TEXT("ApplyRenderScale returns the project default immediately"),
        LDLocalSettings::ApplyRenderScale(*Screen, Defaults.ResolutionQuality) && Screen->GetFloat() == ProjectDefault);
    AddInfo(FString::Printf(TEXT("Baseline r.ScreenPercentage %.2f, project default %.2f."), Baseline, ProjectDefault));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDLocalDisplayPersistenceTest, "Lockdown.Rules.LocalDisplayPersistenceReadback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDLocalDisplayPersistenceTest::RunTest(const FString& Parameters)
{
    FLocalSettingsFixture Fixture(*this);
    auto Expected = DisplayFixture();
    auto Verify = [&]() { return LDLocalSettings::VerifyDisplaySettingsFile(Fixture.DisplayFile, Fixture.QualityFile,
        TEXT("/Script/Engine.GameUserSettings"), Expected); };
    TestTrue(TEXT("Missing display file is not successful void SaveSettings"),
        Verify().Status == ELDLocalSettingsSaveStatus::ReadbackFailed);
    TestTrue(TEXT("Write actual display fixture"), FFileHelper::SaveStringToFile(DisplayIni(Expected), *Fixture.DisplayFile));
    TestTrue(TEXT("Missing separate quality file cannot be masked by display success"),
        Verify().Status == ELDLocalSettingsSaveStatus::ReadbackFailed);
    TestTrue(TEXT("Write actual quality fixture"), FFileHelper::SaveStringToFile(QualityIni(Expected), *Fixture.QualityFile));
    TestTrue(TEXT("Verified requires both actual files"), Verify().IsSaved());
    auto Changed = Expected;
    Changed.bVSync = true;
    TestTrue(TEXT("Modify disk without modifying the expected in-memory state"),
        FFileHelper::SaveStringToFile(DisplayIni(Changed), *Fixture.DisplayFile));
    TestTrue(TEXT("Readback does not trust stale cached VSync"), Verify().Status == ELDLocalSettingsSaveStatus::ReadbackFailed);
    TestTrue(TEXT("Write an unconfirmed mode fixture"), FFileHelper::SaveStringToFile(DisplayIni(Expected, false), *Fixture.DisplayFile));
    TestTrue(TEXT("An unconfirmed mode is not persistence verified"), Verify().Status == ELDLocalSettingsSaveStatus::ReadbackFailed);
    TestTrue(TEXT("Restore confirmed display fixture"), FFileHelper::SaveStringToFile(DisplayIni(Expected), *Fixture.DisplayFile));
    Changed = Expected;
    Changed.Quality.TextureQuality = 4;
    TestTrue(TEXT("Change one quality field on disk"), FFileHelper::SaveStringToFile(QualityIni(Changed), *Fixture.QualityFile));
    TestTrue(TEXT("Every relevant quality field is verified"), Verify().Status == ELDLocalSettingsSaveStatus::ReadbackFailed);
    TestTrue(TEXT("Write invalid numeric disk value"), FFileHelper::SaveStringToFile(
        QualityIni(Expected).Replace(TEXT("sg.ViewDistanceQuality=1"), TEXT("sg.ViewDistanceQuality=1garbage")), *Fixture.QualityFile));
    TestTrue(TEXT("Malformed numbers do not parse as expected defaults"), Verify().Status == ELDLocalSettingsSaveStatus::ReadbackFailed);
    for (const auto& Invalid : {TPair<const TCHAR*, float>(TEXT("71.13junk"), 71.13f),
        TPair<const TCHAR*, float>(TEXT("0junk"), 0.f), TPair<const TCHAR*, float>(TEXT("NaN"), 0.f),
        TPair<const TCHAR*, float>(TEXT("Inf"), 0.f), TPair<const TCHAR*, float>(TEXT("."), 0.f),
        TPair<const TCHAR*, float>(TEXT("1e"), 1.f), TPair<const TCHAR*, float>(TEXT("1e9999"), 0.f)})
    {
        Expected.Quality.ResolutionQuality = Invalid.Value;
        const FString Prefix = FString::Printf(TEXT("sg.ResolutionQuality=%.6f"), Expected.Quality.ResolutionQuality);
        TestTrue(TEXT("Write malformed float with an otherwise matching numeric prefix"), FFileHelper::SaveStringToFile(
            QualityIni(Expected).Replace(*Prefix, *(FString(TEXT("sg.ResolutionQuality=")) + Invalid.Key)), *Fixture.QualityFile));
        TestTrue(TEXT("Malformed scale is never persistence verified"), Verify().Status == ELDLocalSettingsSaveStatus::ReadbackFailed);
    }
    Expected.Quality.ResolutionQuality = 0.00001f;
    FConfigFile Scientific;
    Scientific.ProcessInputFileContents(QualityIni(Expected), Fixture.QualityFile);
    Scientific.SetFloat(TEXT("ScalabilityGroups"), TEXT("sg.ResolutionQuality"), Expected.Quality.ResolutionQuality);
    TestTrue(TEXT("Native SetFloat producer writes scientific notation fixture"), Scientific.Write(Fixture.QualityFile));
    TestTrue(TEXT("Valid native scientific notation is persistence verified"), Verify().IsSaved());
    TestTrue(TEXT("Remove actual display fixture"), IFileManager::Get().Delete(*Fixture.DisplayFile, false, false, true));
    TestTrue(TEXT("A removed actual file cannot be verified from memory"), Verify().Status == ELDLocalSettingsSaveStatus::ReadbackFailed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDLocalDisplayAutosaveGuardTest, "Lockdown.Rules.LocalDisplayAutosaveGuard",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDLocalDisplayAutosaveGuardTest::RunTest(const FString& Parameters)
{
    FLocalSettingsFixture Fixture(*this);
    const auto Expected = DisplayFixture();
    const FString OriginalDisplay = DisplayIni(Expected), OriginalQuality = QualityIni(Expected);
    TestTrue(TEXT("Write confirmed display bytes"), FFileHelper::SaveStringToFile(OriginalDisplay, *Fixture.DisplayFile));
    TestTrue(TEXT("Write confirmed quality bytes"), FFileHelper::SaveStringToFile(OriginalQuality, *Fixture.QualityFile));
    FConfigCacheIni Cache(EConfigCacheType::DiskBacked);
    FConfigFile Display, Quality;
    Display.ProcessInputFileContents(OriginalDisplay, Fixture.DisplayFile);
    Quality.ProcessInputFileContents(OriginalQuality, Fixture.QualityFile);
    Cache.Add(Fixture.DisplayFile, Display);
    Cache.Add(Fixture.QualityFile, Quality);
    FLDDisplayConfigWriteGuard Guard;
    FString Error;
    TestTrue(TEXT("Acquire guard for the two actual engine-style configuration targets"),
        Guard.Begin(Cache, Fixture.DisplayFile, Fixture.QualityFile, TEXT("/Script/Engine.GameUserSettings"), Error));
    TestFalse(TEXT("Cannot replace an active guard"),
        Guard.Begin(Cache, Fixture.DisplayFile, Fixture.QualityFile, TEXT("/Script/Engine.GameUserSettings"), Error));
    Cache.SetString(TEXT("/Script/Engine.GameUserSettings"), TEXT("ResolutionSizeX"), TEXT("1920"), Fixture.DisplayFile);
    Cache.SetString(TEXT("/Script/Engine.GameUserSettings"), TEXT("LastUserConfirmedResolutionSizeX"), TEXT("1920"), Fixture.DisplayFile);
    Cache.SetString(TEXT("/Script/Engine.GameUserSettings"), TEXT("PreferredFullscreenMode"), TEXT("0"), Fixture.DisplayFile);
    Cache.SetString(TEXT("ScalabilityGroups"), TEXT("sg.TextureQuality"), TEXT("4"), Fixture.QualityFile);
    Cache.SetString(TEXT("Unrelated"), TEXT("Marker"), TEXT("keep"), Fixture.DisplayFile);
    Cache.Flush(false, Fixture.DisplayFile);
    Cache.Flush(false, Fixture.QualityFile);
    FString Readback;
    TestTrue(TEXT("Automatic engine-style flush cannot persist trial display values"),
        FFileHelper::LoadFileToString(Readback, *Fixture.DisplayFile) && Readback == OriginalDisplay);
    TestTrue(TEXT("Automatic flush cannot persist trial quality values"),
        FFileHelper::LoadFileToString(Readback, *Fixture.QualityFile) && Readback == OriginalQuality);
    TestTrue(TEXT("Release restores pre-trial tracked values before allowing writes"), Guard.Release(Cache, Error));
    TestFalse(TEXT("Original display write policy restored"), Cache.FindConfigFile(Fixture.DisplayFile)->NoSave);
    TestFalse(TEXT("Original quality write policy restored"), Cache.FindConfigFile(Fixture.QualityFile)->NoSave);
    TestTrue(TEXT("Unrelated settings are not rolled back"),
        Cache.GetString(TEXT("Unrelated"), TEXT("Marker"), Readback, Fixture.DisplayFile) && Readback == TEXT("keep"));
    TestTrue(TEXT("Cached confirmed resolution restored"),
        Cache.GetString(TEXT("/Script/Engine.GameUserSettings"), TEXT("ResolutionSizeX"), Readback, Fixture.DisplayFile) && Readback == TEXT("1280"));
    TestTrue(TEXT("Cached confirmed quality restored"),
        Cache.GetString(TEXT("ScalabilityGroups"), TEXT("sg.TextureQuality"), Readback, Fixture.QualityFile) && Readback == TEXT("1"));
    TestTrue(TEXT("Cached fullscreen shortcut preference restored"),
        Cache.GetString(TEXT("/Script/Engine.GameUserSettings"), TEXT("PreferredFullscreenMode"), Readback, Fixture.DisplayFile) && Readback == TEXT("1"));
    Cache.Flush(false, Fixture.DisplayFile);
    Cache.Flush(false, Fixture.QualityFile);
    FConfigFile ActualDisplay, ActualQuality;
    TestTrue(TEXT("Read post-release display from disk"), FFileHelper::LoadFileToString(Readback, *Fixture.DisplayFile));
    ActualDisplay.ProcessInputFileContents(Readback, Fixture.DisplayFile);
    TestTrue(TEXT("Later flush cannot revive the cached trial resolution"),
        ActualDisplay.GetString(TEXT("/Script/Engine.GameUserSettings"), TEXT("ResolutionSizeX"), Readback) && Readback == TEXT("1280"));
    TestTrue(TEXT("Read post-release quality from disk"), FFileHelper::LoadFileToString(Readback, *Fixture.QualityFile));
    ActualQuality.ProcessInputFileContents(Readback, Fixture.QualityFile);
    TestTrue(TEXT("Later flush cannot revive cached trial quality"),
        ActualQuality.GetString(TEXT("ScalabilityGroups"), TEXT("sg.TextureQuality"), Readback) && Readback == TEXT("1"));
    Cache.SetString(TEXT("/Script/Engine.GameUserSettings"), TEXT("ResolutionSizeX"), TEXT("1440"), Fixture.DisplayFile);
    Cache.Flush(false, Fixture.DisplayFile);
    TestTrue(TEXT("Read a subsequent permitted write"), FFileHelper::LoadFileToString(Readback, *Fixture.DisplayFile));
    FConfigFile Subsequent;
    Subsequent.ProcessInputFileContents(Readback, Fixture.DisplayFile);
    TestTrue(TEXT("The guard did not leave normal future saves disabled"),
        Subsequent.GetString(TEXT("/Script/Engine.GameUserSettings"), TEXT("ResolutionSizeX"), Readback) && Readback == TEXT("1440"));
    Cache.FindConfigFile(Fixture.DisplayFile)->NoSave = true;
    TestTrue(TEXT("Guard may coexist with a pre-existing write prohibition"),
        Guard.Begin(Cache, Fixture.DisplayFile, Fixture.DisplayFile, TEXT("/Script/Engine.GameUserSettings"), Error));
    TestTrue(TEXT("Repeated filename is guarded once"), Guard.Release(Cache, Error));
    TestTrue(TEXT("Release never removes somebody else's NoSave flag"), Cache.FindConfigFile(Fixture.DisplayFile)->NoSave);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDLocalSettingsAtomicReplaceFailureTest, "Lockdown.Rules.LocalSettingsAtomicReplaceFailure",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDLocalSettingsAtomicReplaceFailureTest::RunTest(const FString& Parameters)
{
#if PLATFORM_WINDOWS
    FLocalSettingsFixture Fixture(*this);
    auto* Settings = ULDControlSettings::CreateSessionSettings(Fixture.Storage);
    if (!TestTrue(TEXT("Create an existing verified save"), Settings->SaveLocalSettings().IsSaved())) { return false; }
    TArray<uint8> Before, After;
    if (!TestTrue(TEXT("Read original verified bytes"), FFileHelper::LoadFileToArray(Before, *Fixture.Storage.Filename()))) { return false; }
    FString Path = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*Fixture.Storage.Filename());
    Path.ReplaceInline(TEXT("/"), TEXT("\\"));
    HANDLE Lock = ::CreateFileW(*Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (!TestTrue(TEXT("Hold the existing destination without delete sharing"), Lock != INVALID_HANDLE_VALUE)) { return false; }
    FString Error;
    TestTrue(TEXT("New applied preference is valid"), Settings->SetAppliedZoom(1600, Error));
    const auto Failed = Settings->SaveLocalSettings();
    TestTrue(TEXT("Release the exact destination handle"), ::CloseHandle(Lock) != 0);
    TestTrue(TEXT("Atomic replacement failure is explicit"), Failed.Status == ELDLocalSettingsSaveStatus::WriteFailed);
    TestTrue(TEXT("The destination still contains every original verified byte"),
        FFileHelper::LoadFileToArray(After, *Fixture.Storage.Filename()) && Before == After);
    const auto Original = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
    TestTrue(TEXT("The last known-good save remains loadable"),
        Original.Status == ELDLocalSettingsLoadStatus::Loaded && Original.Settings && Original.Settings->Zoom == 1400);
    TestTrue(TEXT("Failed replacement retains the newly applied dirty value"), Settings->IsDirty() && Settings->Zoom == 1600);
    TArray<FString> TemporaryFiles;
    IFileManager::Get().FindFiles(TemporaryFiles, *(Fixture.Storage.Filename() + TEXT(".tmp-*")), true, false);
    TestTrue(TEXT("Only the failed temporary candidate was removed"), TemporaryFiles.IsEmpty());
    TestTrue(TEXT("Retry replaces the existing file after its lock is released"), Settings->SaveLocalSettings().IsSaved());
    const auto Updated = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
    TestTrue(TEXT("Retry reads back the new value"), Updated.Status == ELDLocalSettingsLoadStatus::Loaded
        && Updated.Settings && Updated.Settings->Zoom == 1600);
    return true;
#else
    AddError(TEXT("This storage failure test requires the supported Windows platform."));
    return false;
#endif
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDLocalSettingsMetadataReadFailureTest, "Lockdown.Rules.LocalSettingsMetadataReadFailure",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDLocalSettingsMetadataReadFailureTest::RunTest(const FString& Parameters)
{
#if PLATFORM_WINDOWS
    FLocalSettingsFixture Fixture(*this);
    auto* Settings = ULDControlSettings::CreateSessionSettings(Fixture.Storage);
    FString Error;
    TestTrue(TEXT("Prepare an existing nondefault preference"), Settings->SetAppliedZoom(1550, Error));
    if (!TestTrue(TEXT("Persist the original existing save"), Settings->SaveLocalSettings().IsSaved())) { return false; }
    TArray<uint8> Before, After;
    TestTrue(TEXT("Read last good bytes"), FFileHelper::LoadFileToArray(Before, *Fixture.Storage.Filename()));
    FString Path = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*Fixture.Storage.Filename());
    Path.ReplaceInline(TEXT("/"), TEXT("\\"));
    HANDLE File = ::CreateFileW(*Path, DELETE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (!TestTrue(TEXT("Open only the existing fixture for a reversible metadata failure"), File != INVALID_HANDLE_VALUE)) { return false; }
    FILE_DISPOSITION_INFO Pending{1};
    if (!TestTrue(TEXT("Temporarily mark the open fixture delete-pending"),
        ::SetFileInformationByHandle(File, FileDispositionInfo, &Pending, sizeof(Pending)) != 0))
    {
        ::CloseHandle(File);
        return false;
    }
    const DWORD Attributes = ::GetFileAttributesW(*Path);
    const DWORD MetadataError = Attributes == INVALID_FILE_ATTRIBUTES ? ::GetLastError() : ERROR_SUCCESS;
    const auto Failed = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
    FILE_DISPOSITION_INFO Keep{};
    const bool Restored = ::SetFileInformationByHandle(File, FileDispositionInfo, &Keep, sizeof(Keep)) != 0;
    const bool Closed = ::CloseHandle(File) != 0;
    if (!TestTrue(TEXT("Restore the fixture's metadata access before closing it"), Restored && Closed)) { return false; }
    TestTrue(TEXT("The real Windows metadata failure is not a definitive missing path"),
        Attributes == INVALID_FILE_ATTRIBUTES && MetadataError != ERROR_SUCCESS
        && MetadataError != ERROR_FILE_NOT_FOUND && MetadataError != ERROR_PATH_NOT_FOUND);
    TestTrue(TEXT("Metadata failure on an existing file is ReadFailed, never Missing"),
        Failed.Status == ELDLocalSettingsLoadStatus::ReadFailed);
    TestNull(TEXT("Metadata failure never supplies dirty defaults for automatic flush"), Failed.Settings);
    // Reproduce the old delayed overwrite if a failed probe ever supplies flushable defaults again.
    if (Failed.Settings) { Failed.Settings->SaveLocalSettings(); }
    TestTrue(TEXT("Restored access did not allow defaults to overwrite the existing file"),
        FFileHelper::LoadFileToArray(After, *Fixture.Storage.Filename()) && Before == After);
    const auto Accessible = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
    TestTrue(TEXT("Restored access reloads the original preference rather than defaults"),
        Accessible.Status == ELDLocalSettingsLoadStatus::Loaded && Accessible.Settings && Accessible.Settings->Zoom == 1550);
    return true;
#else
    AddError(TEXT("This metadata failure test requires the supported Windows platform."));
    return false;
#endif
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDLocalDisplayNativeSaveFlushTest, "Lockdown.Rules.LocalDisplayNativeSaveFlush",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDLocalDisplayNativeSaveFlushTest::RunTest(const FString& Parameters)
{
    FLocalSettingsFixture Fixture(*this);
    FConfigCacheIni Cache(EConfigCacheType::DiskBacked);
    FConfigFile Display, Quality;
    Cache.Add(Fixture.DisplayFile, Display);
    Cache.Add(Fixture.QualityFile, Quality);
    auto* Settings = NewObject<UGameUserSettings>();
    Settings->SetScreenResolution(FIntPoint(1280, 720));
    Settings->SetFullscreenMode(EWindowMode::Windowed);
    Settings->ConfirmVideoMode();
    Settings->SetVSyncEnabled(false);
    Settings->ScalabilityQuality = Scalability::GetQualityLevels();
    auto Expected = FLDDisplaySettings::Read(*Settings);
    const FString Section = Settings->GetClass()->GetPathName();
    TestFalse(TEXT("No display INI fixture is prewritten"), IFileManager::Get().FileExists(*Fixture.DisplayFile));
    TestFalse(TEXT("No quality INI fixture is prewritten"), IFileManager::Get().FileExists(*Fixture.QualityFile));
    // Exercise SaveSettings' native writers with explicit isolated targets, without its global path reload.
    {
        TGuardValue<FConfigCacheIni*> IsolatedCache(GConfig, &Cache);
        Scalability::SaveState(Fixture.QualityFile);
        Settings->SaveConfig(CPF_Config, *Fixture.DisplayFile, &Cache, false);
    }
    TestTrue(TEXT("Native UObject config writer produced the display file"), IFileManager::Get().FileExists(*Fixture.DisplayFile));
    TestFalse(TEXT("Scalability SaveState stages, but does not flush, the separate quality file"), IFileManager::Get().FileExists(*Fixture.QualityFile));
    TestTrue(TEXT("Readback cannot pretend staged editor quality is persisted"),
        LDLocalSettings::VerifyDisplaySettingsFile(Fixture.DisplayFile, Fixture.QualityFile, Section, Expected).Status
            == ELDLocalSettingsSaveStatus::ReadbackFailed);
    TestTrue(TEXT("The real post-save path flushes and verifies the separate quality target"),
        LDLocalSettings::FlushAndVerifyDisplaySettingsFiles(Cache, Fixture.DisplayFile, Fixture.QualityFile, Section, Expected).IsSaved());
    TArray<uint8> Before, After;
    TestTrue(TEXT("Read verified native quality bytes"), FFileHelper::LoadFileToArray(Before, *Fixture.QualityFile));
    Expected.Quality.TextureQuality = (Expected.Quality.TextureQuality + 1) % 5;
    Cache.SetInt(TEXT("ScalabilityGroups"), TEXT("sg.TextureQuality"), Expected.Quality.TextureQuality, Fixture.QualityFile);
    Cache.FindConfigFile(Fixture.QualityFile)->NoSave = true;
    TestTrue(TEXT("A pre-existing editor quality write prohibition is surfaced"),
        LDLocalSettings::FlushAndVerifyDisplaySettingsFiles(Cache, Fixture.DisplayFile, Fixture.QualityFile, Section, Expected).Status
            == ELDLocalSettingsSaveStatus::WriteFailed);
    TestTrue(TEXT("Saving never clears somebody else's NoSave policy"), Cache.FindConfigFile(Fixture.QualityFile)->NoSave);
    TestTrue(TEXT("Blocked save preserved the previous quality file"),
        FFileHelper::LoadFileToArray(After, *Fixture.QualityFile) && Before == After);
    Cache.FindConfigFile(Fixture.QualityFile)->NoSave = false;
    TestTrue(TEXT("Retry flushes pending quality after its owner restores write access"),
        LDLocalSettings::FlushAndVerifyDisplaySettingsFiles(Cache, Fixture.DisplayFile, Fixture.QualityFile, Section, Expected).IsSaved());
    FConfigCacheIni NamedCache(EConfigCacheType::DiskBacked);
    auto* DisplayBranch = NamedCache.FindBranch(FName(TEXT("GameUserSettings")), TEXT("GameUserSettings"));
    auto* QualityBranch = NamedCache.FindBranch(FName(TEXT("Engine")), TEXT("Engine"));
    if (!TestNotNull(TEXT("Known display config identifier exists"), DisplayBranch)
        || !TestNotNull(TEXT("Independent known config identifier exists"), QualityBranch)) { return false; }
    DisplayBranch->IniPath = Fixture.DisplayFile;
    QualityBranch->IniPath = Fixture.QualityFile;
    FString Physical, ResolutionError;
    TestTrue(TEXT("Known config identifier resolves to its real destination, not the working directory"),
        LDLocalSettings::ResolveConfigDestination(NamedCache, TEXT("GameUserSettings"), Physical, ResolutionError)
        && FPaths::IsSamePath(Physical, IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*Fixture.DisplayFile)));
    TestTrue(TEXT("Separate quality identifier resolves independently"),
        LDLocalSettings::ResolveConfigDestination(NamedCache, TEXT("Engine"), Physical, ResolutionError)
        && FPaths::IsSamePath(Physical, IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*Fixture.QualityFile)));
    QualityBranch->IniPath.Reset();
    TestFalse(TEXT("Missing destination cannot fall back to a relative config identifier"),
        LDLocalSettings::ResolveConfigDestination(NamedCache, TEXT("Engine"), Physical, ResolutionError));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDLocalSettingsQuickCastTest, "Lockdown.Rules.LocalSettingsQuickCast",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDLocalSettingsQuickCastTest::RunTest(const FString& Parameters)
{
    FLocalSettingsFixture Fixture(*this);
    auto Write = [&](const TArray<uint8>& Bytes) { return TestTrue(TEXT("Write explicit quick cast fixture"), FFileHelper::SaveArrayToFile(Bytes, *Fixture.Storage.Filename())); };
    FString Error;
    auto* Applied = ULDControlSettings::CreateSessionSettings(Fixture.Storage);
    TestTrue(TEXT("Quick cast is off by default: a skill key arms for a left-click confirmation"),
        !Applied->GetQuickCast() && !GetDefault<ULDControlSettings>()->bQuickCast);
    auto* Draft = Applied->CreateDraft();
    Draft->bQuickCast = true;
    TestTrue(TEXT("Applying a draft turns quick cast on"), Applied->ApplyLocalSettings(*Draft, Error) && Applied->GetQuickCast());
    TestTrue(TEXT("Quick cast is saved and verified from disk"), Applied->SaveLocalSettings().IsSaved());
    const auto Loaded = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
    TestTrue(TEXT("A current-schema round trip restores quick cast"),
        Loaded.Status == ELDLocalSettingsLoadStatus::Loaded && Loaded.Settings && Loaded.Settings->GetQuickCast());

    // A schema 9 file has no quick cast field: it migrates with quick cast off, whatever the writer held.
    auto* Values = NewObject<ULDControlSettings>();
    Values->ResetBindings();
    Values->PlayerName = TEXT("阴人王");
    Values->bQuickCast = true;
    Write(FixtureBytes(*Values, QuickCastlessFixtureSchema, 0));
    const auto Migrated = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
    TestTrue(TEXT("A schema 9 file migrates explicitly with quick cast off and its other preferences kept"),
        Migrated.Status == ELDLocalSettingsLoadStatus::Migrated && Migrated.Settings && !Migrated.Settings->GetQuickCast()
        && Migrated.Settings->PlayerName.Equals(TEXT("阴人王"), ESearchCase::CaseSensitive) && Migrated.Message.Contains(TEXT("快速施法默认关闭")));

    // A stored flag other than 0 or 1 is malformed, never read as on.
    TArray<uint8> Bad = FixtureBytes(*Values, ULDControlSettings::CurrentSchema, 0);
    Values->bQuickCast = false;
    const TArray<uint8> Good = FixtureBytes(*Values, ULDControlSettings::CurrentSchema, 0);
    int32 FlagAt = INDEX_NONE;
    for (int32 Index = 12; Index < Good.Num() && Index < Bad.Num(); ++Index) { if (Good[Index] != Bad[Index]) { FlagAt = Index; break; } }
    if (TestTrue(TEXT("The quick cast byte is located in the fixture"), FlagAt != INDEX_NONE))
    {
        Bad[FlagAt] = 2;
        const uint32 Checksum = FCrc::MemCrc32(Bad.GetData() + 12, Bad.Num() - 12);
        FMemory::Memcpy(Bad.GetData() + 8, &Checksum, sizeof(Checksum));
        Write(Bad);
        TestTrue(TEXT("A malformed quick cast flag is corrupt"), ULDControlSettings::LoadLocalSettings(Fixture.Storage).Status == ELDLocalSettingsLoadStatus::Corrupt);
    }
    return true;
}
#endif
