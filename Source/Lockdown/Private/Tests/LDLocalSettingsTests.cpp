#include "UI/LDControlSettings.h"
#include "Framework/LDGameInstance.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
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

    // Schema 5 is the migratable layout without PlayerName; every other schema writes the current whitelist order.
    constexpr uint32 NamelessFixtureSchema = 5;

    TArray<uint8> FixtureBytes(const ULDControlSettings& Values, uint32 Schema = ULDControlSettings::CurrentSchema, uint8 Edge = 1, bool Extra = false)
    {
        FBufferArchive Payload;
        uint8 Follow = Values.bStartFollowing ? 1 : 0;
        float Text = Values.TextScale, Zoom = Values.Zoom;
        int32 Count = Values.Bindings.Num();
        Payload << Edge << Follow << Text << Zoom;
        if (Schema != NamelessFixtureSchema) { FString Name = Values.PlayerName; Payload << Name; }
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
        && Loaded.Settings->TextScale == 1.25f && !Loaded.Settings->bEdgePan && Loaded.Settings->Key(TEXT("Skill1")) == EKeys::F6);
    TestTrue(TEXT("An unset (empty) name is valid, saves and reloads as unset"), Loaded.Settings && Loaded.Settings->PlayerName.IsEmpty());
    const FString ChineseName = TEXT("阴人王 小明");
    TestTrue(TEXT("The applied owner accepts a Chinese name with an inner space"), Applied->SetAppliedPlayerName(ChineseName, Error));
    TestTrue(TEXT("A chosen name is applied but not yet saved"), Applied->IsDirty());
    TestTrue(TEXT("The current schema saves the player name"), Applied->SaveLocalSettings().IsSaved());
    Loaded = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
    TestTrue(TEXT("Schema 6 round trip keeps the exact Chinese name with every other preference"),
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
    Settings->ResetBindings();
    TestTrue(TEXT("Resetting bindings preserves unrelated preferences and the name"), Settings->TextScale == 1.25f && Settings->Zoom == 1600
        && Settings->PlayerName.Equals(TEXT("阴人王"), ESearchCase::CaseSensitive));
    Settings->ResetLocalDefaults();
    const auto* Defaults = GetDefault<ULDControlSettings>();
    TestTrue(TEXT("Restoring local defaults resets keys, camera and text but never deletes the player name"),
        Settings->TextScale == Defaults->TextScale && Settings->Zoom == Defaults->Zoom && HasExactDemoDefaults(*Settings)
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
#endif
