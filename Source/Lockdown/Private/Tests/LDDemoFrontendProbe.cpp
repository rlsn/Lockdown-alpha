#include "Tests/LDDemoEntryProbe.h"
#include "Core/LDLog.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Framework/LDGameInstance.h"
#include "Framework/LDPlayerController.h"
#include "UI/LDUIRoot.h"
#include "UI/LDControlsPanel.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
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
    // Stable widget names survive copy changes; label text is player-facing and is never used to locate controls.
    UButton* FindButton(UUserWidget* Widget, FName Name)
    {
        return Widget && Widget->WidgetTree ? Widget->WidgetTree->FindWidget<UButton>(Name) : nullptr;
    }

    // UMG visibility is per widget, so a control inside a collapsed row or a hidden panel is not on screen.
    bool OnScreen(const UUserWidget* Owner, const UWidget* Widget)
    {
        if (!Owner || !Widget || !Owner->IsVisible()) { return false; }
        for (const UWidget* Current = Widget; Current; Current = Current->GetParent())
        {
            const ESlateVisibility Visibility = Current->GetVisibility();
            if (Visibility == ESlateVisibility::Collapsed || Visibility == ESlateVisibility::Hidden) { return false; }
        }
        return true;
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
            Report->SetStringField(TEXT("scope"), TEXT("Windowed RHI frontend/UMG synthetic delegate clicks, isolated control save and UserDir, text-scale apply/readback, borderless-only display settings under a -windowed launch, Back on the un-applied VSync draft prompt discards, and a VSync apply saves and reads back. No OS input or display-mode change."));
            Report->SetStringField(TEXT("rhi"), RHIName);
            Report->SetStringField(TEXT("control_file"), Storage.Filename());
            Report->SetStringField(TEXT("game_user_settings"), PhysicalSettings);
            Report->SetStringField(TEXT("quality_settings"), PhysicalQuality);
            Report->SetArrayField(TEXT("checks"), Checks);
            Report->SetArrayField(TEXT("screenshots"), Images);
            FString Json;
            const bool Written = FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json))
                && FFileHelper::SaveStringToFile(Json, *(Directory / TEXT("FrontendSettings.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
            UE_LOG(LogLDTest, Display, TEXT("LD_DEMO_FRONTEND_UI_PROBE passed=%d report=%s"), !Failed && Written, *(Directory / TEXT("FrontendSettings.json")));
            FPlatformMisc::RequestExitWithStatus(false, !Failed && Written ? 0 : 1);
            return false;
        }

        bool Click(UUserWidget* Widget, FName Name)
        {
            auto* Button = FindButton(Widget, Name);
            if (!Check(Button && Button->GetIsEnabled() && OnScreen(Widget, Button), TEXT("Action available: ") + Name.ToString())) { return false; }
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
                auto* Apply = FindButton(PC->ControlsUI, TEXT("ApplyDisplayDraft"));
                if (!Check(Apply && OnScreen(PC->ControlsUI, Apply) && !Apply->GetIsEnabled(), TEXT("Display Apply is unavailable before a draft changes"))) { return Finish(); }
                Check(!FindButton(PC->ControlsUI, TEXT("WindowModeDraft")) && !FindButton(PC->ControlsUI, TEXT("ResolutionDraft"))
                    && !FindButton(PC->ControlsUI, TEXT("KeepDisplayMode")), TEXT("Settings offer no window mode, resolution or display trial"));
                Check(GEngine->GetGameUserSettings()->GetFullscreenMode() == EWindowMode::WindowedFullscreen && !Viewport->Viewport->IsFullscreen(),
                    TEXT("The game holds borderless; this fixture's -windowed launch only overrides its own window"));
                Check(!PC->Controls->bShowNetworkDiagnostics && !PC->Controls->bShowFPS,TEXT("Diagnostics start disabled in a fresh user profile"));
                if (!Click(PC->ControlsUI,TEXT("ToggleNetworkDiagnostics")) || !Click(PC->ControlsUI,TEXT("ToggleFPS")))
                { return Finish(); }
                Check(PC->Controls->bShowNetworkDiagnostics && PC->Controls->bShowFPS,TEXT("Both diagnostics toggles apply immediately"));
                if (!Click(PC->ControlsUI,TEXT("ToggleFPS"))) { return Finish(); }
                Check(PC->Controls->bShowNetworkDiagnostics && !PC->Controls->bShowFPS,TEXT("FPS can be hidden without disabling network diagnostics"));
                if (!Click(PC->ControlsUI,TEXT("ToggleNetworkDiagnostics"))) { return Finish(); }
                Check(!PC->Controls->bShowNetworkDiagnostics && !PC->Controls->bShowFPS,TEXT("Both diagnostics toggles can be turned off"));
                if (!Click(PC->ControlsUI,TEXT("ToggleFPS"))) { return Finish(); }
                Check(!PC->Controls->bShowNetworkDiagnostics && PC->Controls->bShowFPS,TEXT("FPS works without network diagnostics"));
                if (!Click(PC->ControlsUI,TEXT("ToggleNetworkDiagnostics"))) { return Finish(); }
                Capture(TEXT("Settings-100"));
            }
            else if (Stage == 3) { Reveal(FindButton(PC->ControlsUI, TEXT("ApplyTextScale"))); }
            else if (Stage == 4) { if (!Click(PC->ControlsUI, TEXT("ApplyTextScale"))) { return Finish(); } }
            else if (Stage == 5)
            {
                const auto Loaded = ULDControlSettings::LoadLocalSettings(Storage);
                if (!Check(PC->Controls->TextScale == 1.25f && PC->Controls->GetPhase() == ELDLocalSettingsPhase::PersistenceVerified
                    && Loaded.Status == ELDLocalSettingsLoadStatus::Loaded && Loaded.Settings && Loaded.Settings->TextScale == 1.25f,
                    TEXT("Actual local-preference Apply is persisted and verified from the isolated file"))) { return Finish(); }
                Check(Loaded.Settings->bShowNetworkDiagnostics && Loaded.Settings->bShowFPS,
                    TEXT("Both diagnostics choices survive another preference apply and disk readback"));
                Capture(TEXT("Settings-125"));
            }
            else if (Stage == 6) { Reveal(FindButton(PC->ControlsUI, TEXT("VSyncDraft"))); }
            else if (Stage == 7)
            {
                if (!Click(PC->ControlsUI, TEXT("VSyncDraft"))) { return Finish(); }
                auto* Apply = FindButton(PC->ControlsUI, TEXT("ApplyDisplayDraft"));
                Check(Apply && OnScreen(PC->ControlsUI, Apply) && Apply->GetIsEnabled(), TEXT("A display draft enables its real Apply action"));
                Check(FLDDisplaySettings::Read(*GEngine->GetGameUserSettings()) == OriginalDisplay, TEXT("Unapplied draft does not change live display settings"));
            }
            else if (Stage == 8) { if (!Click(PC->ControlsUI, TEXT("Close"))) { return Finish(); } }
            else if (Stage == 9)
            {
                auto* Discard = FindButton(PC->ControlsUI, TEXT("DiscardDisplayDraft"));
                Check(PC->ControlsUI->IsOpen() && Discard && OnScreen(PC->ControlsUI, Discard), TEXT("Closing an unapplied draft exposes an explicit discard path"));
                Check(PC->ControlsUI->HasChildConfirmation(), TEXT("The unapplied-draft prompt is a child confirmation for Back routing"));
                Capture(TEXT("DiscardDisplayDraft"));
            }
            // Issue #99: Back again on the prompt leaves without the draft instead of toggling the prompt away and back.
            else if (Stage == 10) { if (!Click(PC->ControlsUI, TEXT("Close"))) { return Finish(); } }
            else if (Stage == 11)
            {
                Check(!PC->ControlsUI->IsOpen() && !PC->ControlsUI->HasChildConfirmation(), TEXT("Back on the unapplied-draft prompt discards and closes the panel"));
                Check(FLDDisplaySettings::Read(*GEngine->GetGameUserSettings()) == OriginalDisplay && !Viewport->Viewport->IsFullscreen(),
                    TEXT("The discarded draft leaves quality and VSync unchanged"));
                if (!Click(PC->RootUI, TEXT("Settings"))) { return Finish(); }
            }
            else if (Stage == 12) { Reveal(FindButton(PC->ControlsUI, TEXT("VSyncDraft"))); }
            else if (Stage == 13)
            {
                if (!Click(PC->ControlsUI, TEXT("VSyncDraft")) || !Click(PC->ControlsUI, TEXT("ApplyDisplayDraft"))) { return Finish(); }
                const auto Applied = FLDDisplaySettings::Read(*GEngine->GetGameUserSettings());
                Check(Instance->GetDisplaySettingsResult().Status == ELDDisplaySettingsStatus::PersistenceVerified && !Instance->AreDisplaySettingsDirty(),
                    TEXT("A VSync apply is saved and read back from disk even though the launch overrides the window"));
                Check(Applied.bVSync != OriginalDisplay.bVSync && Applied.Quality == OriginalDisplay.Quality, TEXT("Only the drafted VSync changed"));
                auto* Apply = FindButton(PC->ControlsUI, TEXT("ApplyDisplayDraft"));
                Check(Apply && !Apply->GetIsEnabled(), TEXT("An applied draft leaves nothing to apply"));
            }
            else if (Stage == 14) { if (!Click(PC->ControlsUI, TEXT("Close"))) { return Finish(); } }
            else
            {
                Check(!PC->ControlsUI->IsOpen(), TEXT("With the draft applied, Back closes the panel at once"));
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
        UE_LOG(LogLDTest, Error, TEXT("Frontend UI fixture requires a directory under Artifacts."));
        FPlatformMisc::RequestExitWithStatus(false, 1);
        return;
    }
    auto Probe = MakeShared<FFrontendProbe>();
    Probe->World = &World;
    Probe->Directory = FPaths::ConvertRelativePathToFull(Directory);
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Probe](float Delta) { return Probe->Tick(Delta); }));
}
#endif
