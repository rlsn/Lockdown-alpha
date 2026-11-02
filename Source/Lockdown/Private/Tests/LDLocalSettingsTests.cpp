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

    TArray<uint8> FixtureBytes(const ULDControlSettings& Values, uint32 Schema = ULDControlSettings::CurrentSchema, uint8 Edge = 1, bool Extra = false)
    {
        FBufferArchive Payload;
        uint8 Profile = Values.Profile, Follow = Values.bStartFollowing ? 1 : 0;
        float Text = Values.TextScale, Zoom = Values.Zoom;
        int32 Count = Values.Bindings.Num();
        Payload << Profile << Edge << Follow << Text << Zoom << Count;
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
    TestTrue(TEXT("User registry is explicitly whitelisted"), !FLDLocalSettingsStorage::User(99).IsValid());
    for (uint8 Registry : { uint8(6), uint8(7), uint8(8) })
    { TestNotEqual(TEXT("Automation storage is distinct from every user file"), Fixture.Storage.Filename(), FLDLocalSettingsStorage::User(Registry).Filename()); }

    auto Loaded = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
    TestTrue(TEXT("Missing and Loaded are distinct"), Loaded.Status == ELDLocalSettingsLoadStatus::Missing);
    auto* Applied = Loaded.Settings;
    if (!TestNotNull(TEXT("Missing file supplies a usable current registry"), Applied)) { return false; }
    TestTrue(TEXT("Missing defaults are not called persistence verified"), Applied->GetPhase() == ELDLocalSettingsPhase::ConfirmedApplied);
    auto* Draft = Applied->CreateDraft();
    Draft->TextScale = 1.25f;
    Draft->bEdgePan = false;
    FString Error;
    TestTrue(TEXT("Draft binding change is legal"), Draft->Rebind(TEXT("Skill1"), EKeys::F6, Error));
    TestTrue(TEXT("EditingDraft cannot save"), Draft->SaveLocalSettings().Status == ELDLocalSettingsSaveStatus::Invalid);
    TestFalse(TEXT("Draft did not create a file"), IFileManager::Get().FileExists(*Fixture.Storage.Filename()));
    TestEqual(TEXT("Draft does not mutate applied preferences"), Applied->TextScale, 1.f);
    TestTrue(TEXT("Explicit Apply accepts a valid local draft"), Applied->ApplyLocalSettings(*Draft, Error));
    TestTrue(TEXT("ConfirmedApplied is dirty"), Applied->IsDirty());
    TestTrue(TEXT("Save is verified from the actual target file"), Applied->SaveLocalSettings().IsSaved());
    TestTrue(TEXT("Only verified save clears dirty"), Applied->GetPhase() == ELDLocalSettingsPhase::PersistenceVerified);
    Loaded = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
    TestTrue(TEXT("Restart reads confirmed preferences"), Loaded.Status == ELDLocalSettingsLoadStatus::Loaded && Loaded.Settings
        && Loaded.Settings->TextScale == 1.25f && !Loaded.Settings->bEdgePan && Loaded.Settings->Key(TEXT("Skill1")) == EKeys::F6);

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
    TestTrue(TEXT("Binding recovery does not reset other preferences"), Applied->TextScale == 1.25f && !Applied->bEdgePan);

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
    Values->Bindings.Remove(TEXT("Help"));
    Values->Bindings.Add(TEXT("RetiredAction"), EKeys::F1);
    const auto UnknownBindings = FixtureBytes(*Values, ULDControlSettings::CurrentSchema, 0);
    Write(UnknownBindings);
    const auto Rejected = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
    TestTrue(TEXT("Unknown bindings have recovery data, never success"),
        Rejected.Status == ELDLocalSettingsLoadStatus::Corrupt && !Rejected.Settings && Rejected.RecoverablePreferences);
    auto* Instance = NewObject<ULDGameInstance>();
    auto* Recovered = Instance->UseIsolatedLocalSettings(Fixture.Id, 6);
    TestTrue(TEXT("Recovery preserves validated unrelated preferences"),
        Recovered->TextScale == 1.25f && Recovered->Zoom == 1600 && !Recovered->bEdgePan);
    TestTrue(TEXT("Runtime bindings require explicit replacement approval"), Recovered->NeedsRecovery() && Recovered->Valid());
    TestFalse(TEXT("Retired action cannot reappear at runtime"), Recovered->Key(TEXT("RetiredAction")).IsValid());
    TestTrue(TEXT("Travel does not silently replace a corrupt file"),
        Instance->FlushLocalSettings().Status == ELDLocalSettingsSaveStatus::RecoveryRequired);
    TestTrue(TEXT("Rejected auto-flush kept exact bad bytes"),
        FFileHelper::LoadFileToArray(Readback, *Fixture.Storage.Filename()) && Readback == UnknownBindings);
    TestTrue(TEXT("Explicit recovery succeeds"), Instance->ConfirmLocalSettingsRecovery().IsSaved());
    const auto Confirmed = ULDControlSettings::LoadLocalSettings(Fixture.Storage);
    TestTrue(TEXT("Recovered file keeps camera/text and current registry"),
        Confirmed.Status == ELDLocalSettingsLoadStatus::Loaded && Confirmed.Settings && Confirmed.Settings->Zoom == 1600
        && Confirmed.Settings->TextScale == 1.25f && !Confirmed.Settings->bEdgePan && Confirmed.Settings->Valid());
    TestTrue(TEXT("Another explicit registry cannot trigger fallback or migration"),
        ULDControlSettings::LoadLocalSettings(Fixture.Storage, 8).Status == ELDLocalSettingsLoadStatus::UnsupportedSchema);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDLocalDisplayTrialTest, "Lockdown.Rules.LocalDisplayTrialLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLDLocalDisplayTrialTest::RunTest(const FString& Parameters)
{
    FLocalSettingsFixture Fixture(*this);
    auto* Instance = NewObject<ULDGameInstance>();
    auto* Applied = Instance->UseIsolatedLocalSettings(Fixture.Id, 6);
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
