#include "Tests/LDDemoEntryProbe.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Framework/LDGameInstance.h"
#include "Framework/LDPlayerController.h"
#include "UI/LDUIRoot.h"
#include "UI/LDControlsPanel.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/ScrollBox.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"
#include "DynamicRHI.h"

namespace
{
    UButton* FindButton(UUserWidget* Widget, const FString& Label, bool Prefix = false)
    {
        if (!Widget || !Widget->WidgetTree) { return nullptr; }
        TArray<UWidget*> Children;
        Widget->WidgetTree->GetAllWidgets(Children);
        for (UWidget* Child : Children)
        {
            auto* Button = Cast<UButton>(Child);
            const auto* Text = Button ? Cast<UTextBlock>(Button->GetContent()) : nullptr;
            if (Text && (Prefix ? Text->GetText().ToString().StartsWith(Label) : Text->GetText().ToString() == Label)) { return Button; }
        }
        return nullptr;
    }

    void Reveal(UWidget* Widget)
    {
        for (auto* Parent = Widget ? Widget->GetParent() : nullptr; Parent; Parent = Parent->GetParent())
        {
            if (auto* Scroll = Cast<UScrollBox>(Parent))
            {
                Scroll->ScrollWidgetIntoView(Widget, false, EDescendantScrollDestination::IntoView);
                return;
            }
        }
    }

    struct FFrontendProbe
    {
        TWeakObjectPtr<UWorld> World;
        FString Directory, PendingImage, RHIName, PhysicalSettings, PhysicalQuality;
        FLDLocalSettingsStorage Storage;
        FLDDisplaySettings OriginalDisplay;
        TArray<TSharedPtr<FJsonValue>> Checks, Images;
        double StartedAt = FPlatformTime::Seconds(), NextAt = 0;
        int32 Stage = 0;
        bool Failed = false;

        bool Check(bool Passed, const FString& Name)
        {
            auto Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("name"), Name);
            Entry->SetBoolField(TEXT("passed"), Passed);
            Checks.Add(MakeShared<FJsonValueObject>(Entry));
            Failed |= !Passed;
            return Passed;
        }

        void Capture(const TCHAR* Name)
        {
            PendingImage = Directory / (FString(Name) + TEXT(".png"));
            Images.Add(MakeShared<FJsonValueString>(FPaths::GetCleanFilename(PendingImage)));
            FScreenshotRequest::RequestScreenshot(PendingImage, true, false);
        }

        bool Finish(const FString& Error = TEXT(""))
        {
            if (!Error.IsEmpty()) { Check(false, Error); }
            auto Report = MakeShared<FJsonObject>();
            Report->SetBoolField(TEXT("passed"), !Failed);
            Report->SetStringField(TEXT("scope"), TEXT("Windowed RHI frontend/UMG synthetic delegate clicks, isolated control save and UserDir, text-scale apply/readback and un-applied VSync draft discard. No OS input, resolution/fullscreen changes, or display trial acceptance claim."));
            Report->SetStringField(TEXT("rhi"), RHIName);
            Report->SetStringField(TEXT("control_file"), Storage.Filename());
            Report->SetStringField(TEXT("game_user_settings"), PhysicalSettings);
            Report->SetStringField(TEXT("quality_settings"), PhysicalQuality);
            Report->SetArrayField(TEXT("checks"), Checks);
            Report->SetArrayField(TEXT("screenshots"), Images);
            FString Json;
            const bool Written = FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json))
                && FFileHelper::SaveStringToFile(Json, *(Directory / TEXT("FrontendSettings.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
            UE_LOG(LogTemp, Display, TEXT("LD_DEMO_FRONTEND_UI_PROBE passed=%d report=%s"), !Failed && Written, *(Directory / TEXT("FrontendSettings.json")));
            FPlatformMisc::RequestExitWithStatus(false, !Failed && Written ? 0 : 1);
            return false;
        }

        bool Click(UUserWidget* Widget, const FString& Label, bool Prefix = false)
        {
            auto* Button = FindButton(Widget, Label, Prefix);
            if (!Check(Button && Button->GetIsEnabled() && Button->IsVisible(), TEXT("Action available: ") + Label)) { return false; }
            Button->OnClicked.Broadcast();
            return true;
        }

        bool Tick(float Delta)
        {
            const double Now = FPlatformTime::Seconds();
            if (Now - StartedAt > 150) { return Finish(TEXT("Rendered UI fixture timed out")); }
            if (!World.IsValid()) { return Finish(TEXT("Frontend world ended before UI checks completed")); }
            if (!PendingImage.IsEmpty())
            {
                if (IFileManager::Get().FileSize(*PendingImage) <= 0) { return true; }
                Check(true, TEXT("Rendered screenshot: ") + FPaths::GetCleanFilename(PendingImage));
                PendingImage.Reset();
                NextAt = Now + .5;
                return true;
            }
            if (Now < NextAt) { return true; }
            auto* PC = Cast<ALDPlayerController>(World->GetFirstPlayerController());
            auto* Instance = World->GetGameInstance<ULDGameInstance>();
            auto* Viewport = World->GetGameViewport();
            if (!PC || !PC->RootUI || !Instance || !Viewport || !Viewport->Viewport) { return true; }
            if (Failed) { return Finish(); }
            NextAt = Now + .5;
            if (Stage == 0)
            {
                RHIName = GDynamicRHI ? GDynamicRHI->GetName() : TEXT("None");
                FString ResolutionError;
                if (!Check(GDynamicRHI && RHIName != TEXT("Null") && !FParse::Param(FCommandLine::Get(), TEXT("nullrhi")), TEXT("A real RHI renders the fixture"))
                    || !Check(!Viewport->Viewport->IsFullscreen(), TEXT("Fixture is windowed, not an OS display-mode change"))
                    || !Check(GConfig && LDLocalSettings::ResolveConfigDestination(*GConfig, GGameUserSettingsIni, PhysicalSettings, ResolutionError)
                        && FPaths::IsUnderDirectory(PhysicalSettings, Directory),
                        TEXT("Engine settings target is isolated under this fixture UserDir"))
                    || !Check(LDLocalSettings::ResolveConfigDestination(*GConfig, GIsEditor ? GEditorSettingsIni : GGameUserSettingsIni, PhysicalQuality, ResolutionError)
                        && FPaths::IsUnderDirectory(PhysicalQuality, Directory),
                        TEXT("Quality settings target is isolated under this fixture UserDir"))) { return Finish(); }
                Storage = FLDLocalSettingsStorage::User();
                PC->Controls = Instance->GetLocalControlSettings();
                if (!Check(PC->Controls && FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Storage.Filename()), Directory),
                    TEXT("Control settings use the new isolated process UserDir, never the real user save"))) { return Finish(); }
                OriginalDisplay = FLDDisplaySettings::Read(*GEngine->GetGameUserSettings());
                Capture(TEXT("Frontend-100"));
            }
            else if (Stage == 1) { if (!Click(PC->RootUI, TEXT("Settings"))) { return Finish(); } }
            else if (Stage == 2)
            {
                if (!Check(PC->ControlsUI && PC->ControlsUI->IsOpen(), TEXT("The production Settings button opens the actual UMG panel"))) { return Finish(); }
                auto* Apply = FindButton(PC->ControlsUI, TEXT("Apply display draft"));
                if (!Check(Apply && !Apply->GetIsEnabled(), TEXT("Display Apply is unavailable before a draft changes"))) { return Finish(); }
                Capture(TEXT("Settings-100"));
            }
            else if (Stage == 3) { Reveal(FindButton(PC->ControlsUI, TEXT("Apply text 100 / 125%"))); }
            else if (Stage == 4) { if (!Click(PC->ControlsUI, TEXT("Apply text 100 / 125%"))) { return Finish(); } }
            else if (Stage == 5)
            {
                const auto Loaded = ULDControlSettings::LoadLocalSettings(Storage);
                if (!Check(PC->Controls->TextScale == 1.25f && PC->Controls->GetPhase() == ELDLocalSettingsPhase::PersistenceVerified
                    && Loaded.Status == ELDLocalSettingsLoadStatus::Loaded && Loaded.Settings && Loaded.Settings->TextScale == 1.25f,
                    TEXT("Actual local-preference Apply is persisted and verified from the isolated file"))) { return Finish(); }
                Capture(TEXT("Settings-125"));
            }
            else if (Stage == 6) { Reveal(FindButton(PC->ControlsUI, TEXT("VSync draft:"), true)); }
            else if (Stage == 7)
            {
                if (!Click(PC->ControlsUI, TEXT("VSync draft:"), true)) { return Finish(); }
                auto* Apply = FindButton(PC->ControlsUI, TEXT("Apply display draft"));
                Check(Apply && Apply->GetIsEnabled(), TEXT("A display draft enables its real Apply action"));
                Check(FLDDisplaySettings::Read(*GEngine->GetGameUserSettings()) == OriginalDisplay, TEXT("Unapplied draft does not change live display settings"));
            }
            else if (Stage == 8) { if (!Click(PC->ControlsUI, TEXT("Close"))) { return Finish(); } }
            else if (Stage == 9)
            {
                Check(FindButton(PC->ControlsUI, TEXT("Discard display draft and return")) != nullptr, TEXT("Closing an unapplied draft exposes an explicit discard path"));
                Capture(TEXT("DiscardDisplayDraft"));
            }
            else if (Stage == 10) { if (!Click(PC->ControlsUI, TEXT("Discard display draft and return"))) { return Finish(); } }
            else
            {
                Check(!PC->ControlsUI->IsOpen() && !Instance->GetDisplayTrialId() && !Instance->IsDisplayRestorePending(),
                    TEXT("Discard closes the panel without starting a display trial"));
                Check(FLDDisplaySettings::Read(*GEngine->GetGameUserSettings()) == OriginalDisplay && !Viewport->Viewport->IsFullscreen(),
                    TEXT("Display mode, resolution, quality and VSync remain unchanged"));
                return Finish();
            }
            ++Stage;
            return true;
        }
    };
}

void LDDemoTests::StartFrontendSettingsProbe(UWorld& World)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("LDDemoFrontendUIProbe"))) { return; }
    FString Directory;
    if (!FParse::Value(FCommandLine::Get(), TEXT("LDDemoFrontendUIOutput="), Directory)
        || !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Directory),
            FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Artifacts"))))
    {
        UE_LOG(LogTemp, Error, TEXT("Frontend UI fixture requires a directory under Artifacts."));
        FPlatformMisc::RequestExitWithStatus(false, 1);
        return;
    }
    auto Probe = MakeShared<FFrontendProbe>();
    Probe->World = &World;
    Probe->Directory = FPaths::ConvertRelativePathToFull(Directory);
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Probe](float Delta) { return Probe->Tick(Delta); }));
}
#endif
