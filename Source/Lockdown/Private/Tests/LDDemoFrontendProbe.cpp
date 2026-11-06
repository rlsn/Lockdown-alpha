#include "Tests/LDDemoEntryProbe.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Framework/LDGameInstance.h"
#include "Framework/LDPlayerController.h"
#include "UI/LDUIRoot.h"
#include "UI/LDControlsPanel.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ScrollBox.h"
#include "Components/Slider.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
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
    using ECategory = ULDControlsPanel::ECategory;

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

    // Synthetic Slate key input along the real focus path (no OS input): the focused control and its panel see it exactly
    // as they see a key press.
    bool SendKey(UWidget* Focus, const FKey& Key)
    {
        if (!Focus || !FSlateApplication::IsInitialized()) { return false; }
        Focus->SetKeyboardFocus();
        const FKeyEvent Event(Key, FModifierKeysState(), FSlateApplication::Get().GetUserIndexForKeyboard(), false, 0, 0);
        FSlateApplication::Get().ProcessKeyDownEvent(Event);
        FSlateApplication::Get().ProcessKeyUpEvent(Event);
        return true;
    }

    FKey DefaultKey(FName Action)
    {
        const FLDControlBinding* Binding = ULDControlSettings::Definitions().FindByPredicate([Action](const FLDControlBinding& B) { return B.Action == Action; });
        return Binding ? Binding->Key : FKey();
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
            Report->SetStringField(TEXT("scope"), TEXT("Windowed RHI frontend/UMG synthetic delegate clicks and Slate key events, isolated control save and UserDir, every settings page through its named tab at 100% and 125% text, diagnostics toggles, text-scale apply/readback, borderless-only display settings under a -windowed launch, VSync draft revert, Back on the un-applied draft prompt discards, a VSync apply saves and reads back, in-row key rebinding with a refused conflict and an Esc cancel, and per-page restore defaults. No OS input or display-mode change."));
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

        bool Click(UUserWidget* Widget, FName Name)
        {
            auto* Button = FindButton(Widget, Name);
            if (!Check(Button && Button->GetIsEnabled() && OnScreen(Widget, Button), TEXT("Action available: ") + Name.ToString())) { return false; }
            Button->OnClicked.Broadcast();
            return true;
        }

        // A settings page is chosen only through its named production tab, focused as a pointer press would focus it; a
        // control of that page must then be on screen.
        bool ShowPage(ULDControlsPanel* Panel, const TCHAR* Tab, ECategory Category, const TCHAR* Control)
        {
            if (auto* Button = FindButton(Panel, Tab)) { Button->SetKeyboardFocus(); }
            if (!Click(Panel, Tab)) { return false; }
            const UWidget* Shown = Panel->WidgetTree->FindWidget(Control);
            return Check(Panel->GetCategory() == Category && Shown && OnScreen(Panel, Shown),
                FString::Printf(TEXT("Tab %s shows its page with %s"), Tab, Control));
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
                auto* Panel = PC->ControlsUI.Get();
                if (!Check(Panel && Panel->IsOpen(), TEXT("The production Settings button opens the actual UMG panel"))) { return Finish(); }
                Check(Panel->GetCategory() == ECategory::Display && OnScreen(Panel, FindButton(Panel, TEXT("VSyncDraft"))),
                    TEXT("Settings open on the display page with its draft controls in view"));
                auto* Apply = FindButton(Panel, TEXT("ApplyDisplayDraft"));
                auto* Revert = FindButton(Panel, TEXT("RevertDisplayDraft"));
                if (!Check(Apply && OnScreen(Panel, Apply) && !Apply->GetIsEnabled() && Revert && OnScreen(Panel, Revert) && !Revert->GetIsEnabled(),
                    TEXT("Display Apply and Revert are unavailable before a draft changes"))) { return Finish(); }
                Check(!FindButton(Panel, TEXT("WindowModeDraft")) && !FindButton(Panel, TEXT("ResolutionDraft"))
                    && !FindButton(Panel, TEXT("KeepDisplayMode")), TEXT("Settings offer no window mode, resolution or display trial"));
                Check(GEngine->GetGameUserSettings()->GetFullscreenMode() == EWindowMode::WindowedFullscreen && !Viewport->Viewport->IsFullscreen(),
                    TEXT("The game holds borderless; this fixture's -windowed launch only overrides its own window"));
                Check(!PC->Controls->bShowNetworkDiagnostics && !PC->Controls->bShowFPS,TEXT("Diagnostics start disabled in a fresh user profile"));
                // Hidden pages leave the keyboard order; Tab from the panel itself lands on the first category tab.
                const TArray<UWidget*> Targets = Panel->PanelFocusTargets();
                bool bTabs = true;
                for (const TCHAR* Tab : { TEXT("TabDisplay"), TEXT("TabAudio"), TEXT("TabControls"), TEXT("TabCamera"), TEXT("TabInterface") })
                { bTabs &= Targets.Contains(Panel->WidgetTree->FindWidget(Tab)); }
                Check(bTabs && Targets.Contains(FindButton(Panel, TEXT("VSyncDraft"))) && !Targets.Contains(FindButton(Panel, TEXT("ToggleFPS")))
                    && !Targets.Contains(Panel->WidgetTree->FindWidget(TEXT("MasterVolume"))),
                    TEXT("Keyboard focus walks every category tab and only the shown page"));
                SendKey(Panel, EKeys::Tab);
                Check(FindButton(Panel, TEXT("TabDisplay"))->HasKeyboardFocus(), TEXT("Tab from the panel focuses the first category tab"));
                Capture(TEXT("Settings-Display-100"));
            }
            else if (Stage == 3)
            {
                if (!ShowPage(PC->ControlsUI, TEXT("TabAudio"), ECategory::Audio, TEXT("MasterVolume"))) { return Finish(); }
                Check(!OnScreen(PC->ControlsUI, FindButton(PC->ControlsUI, TEXT("VSyncDraft"))), TEXT("Only one page shows at a time"));
                Capture(TEXT("Settings-Audio-100"));
            }
            else if (Stage == 4)
            {
                if (!ShowPage(PC->ControlsUI, TEXT("TabControls"), ECategory::Controls, TEXT("BindStop"))) { return Finish(); }
                Capture(TEXT("Settings-Controls-100"));
            }
            else if (Stage == 5)
            {
                if (!ShowPage(PC->ControlsUI, TEXT("TabCamera"), ECategory::Camera, TEXT("SlowerCameraPan"))) { return Finish(); }
                Capture(TEXT("Settings-Camera-100"));
            }
            else if (Stage == 6)
            {
                if (!ShowPage(PC->ControlsUI, TEXT("TabInterface"), ECategory::Interface, TEXT("ToggleFPS"))) { return Finish(); }
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
                Capture(TEXT("Settings-Interface-100"));
            }
            else if (Stage == 7) { if (!Click(PC->ControlsUI, TEXT("TextScale125"))) { return Finish(); } }
            else if (Stage == 8)
            {
                const auto Loaded = ULDControlSettings::LoadLocalSettings(Storage);
                if (!Check(PC->Controls->TextScale == 1.25f && PC->Controls->GetPhase() == ELDLocalSettingsPhase::PersistenceVerified
                    && Loaded.Status == ELDLocalSettingsLoadStatus::Loaded && Loaded.Settings && Loaded.Settings->TextScale == 1.25f,
                    TEXT("Actual local-preference Apply is persisted and verified from the isolated file"))) { return Finish(); }
                Check(Loaded.Settings->bShowNetworkDiagnostics && Loaded.Settings->bShowFPS,
                    TEXT("Both diagnostics choices survive another preference apply and disk readback"));
                Capture(TEXT("Settings-Interface-125"));
            }
            else if (Stage == 9)
            {
                if (!ShowPage(PC->ControlsUI, TEXT("TabControls"), ECategory::Controls, TEXT("BindStop"))) { return Finish(); }
                Capture(TEXT("Settings-Controls-125"));
            }
            else if (Stage == 10)
            {
                if (!ShowPage(PC->ControlsUI, TEXT("TabCamera"), ECategory::Camera, TEXT("SlowerCameraPan"))) { return Finish(); }
                Capture(TEXT("Settings-Camera-125"));
            }
            else if (Stage == 11)
            {
                if (!ShowPage(PC->ControlsUI, TEXT("TabAudio"), ECategory::Audio, TEXT("MasterVolume"))) { return Finish(); }
                Capture(TEXT("Settings-Audio-125"));
            }
            else if (Stage == 12)
            {
                if (!ShowPage(PC->ControlsUI, TEXT("TabDisplay"), ECategory::Display, TEXT("VSyncDraft"))
                    || !Click(PC->ControlsUI, TEXT("VSyncDraft"))) { return Finish(); }
                auto* Apply = FindButton(PC->ControlsUI, TEXT("ApplyDisplayDraft"));
                auto* Revert = FindButton(PC->ControlsUI, TEXT("RevertDisplayDraft"));
                Check(Apply && OnScreen(PC->ControlsUI, Apply) && Apply->GetIsEnabled() && Revert && Revert->GetIsEnabled(),
                    TEXT("A display draft enables its real Apply and Revert actions"));
                Check(FLDDisplaySettings::Read(*GEngine->GetGameUserSettings()) == OriginalDisplay, TEXT("Unapplied draft does not change live display settings"));
                Capture(TEXT("Settings-Display-125"));
            }
            else if (Stage == 13)
            {
                if (!Click(PC->ControlsUI, TEXT("RevertDisplayDraft"))) { return Finish(); }
                Check(!FindButton(PC->ControlsUI, TEXT("ApplyDisplayDraft"))->GetIsEnabled() && !FindButton(PC->ControlsUI, TEXT("RevertDisplayDraft"))->GetIsEnabled()
                    && FLDDisplaySettings::Read(*GEngine->GetGameUserSettings()) == OriginalDisplay,
                    TEXT("Revert drops the draft and leaves nothing to apply or to confirm"));
                if (!Click(PC->ControlsUI, TEXT("VSyncDraft"))) { return Finish(); }
                Check(FindButton(PC->ControlsUI, TEXT("ApplyDisplayDraft"))->GetIsEnabled(), TEXT("A new edit after Revert is a new draft"));
            }
            // The draft survives a page switch, and Back from another page brings the prompt up over the display page.
            else if (Stage == 14)
            {
                if (!ShowPage(PC->ControlsUI, TEXT("TabAudio"), ECategory::Audio, TEXT("MasterVolume")) || !Click(PC->ControlsUI, TEXT("Close"))) { return Finish(); }
            }
            else if (Stage == 15)
            {
                auto* Discard = FindButton(PC->ControlsUI, TEXT("DiscardDisplayDraft"));
                Check(PC->ControlsUI->IsOpen() && Discard && OnScreen(PC->ControlsUI, Discard), TEXT("Closing an unapplied draft exposes an explicit discard path"));
                Check(PC->ControlsUI->HasChildConfirmation(), TEXT("The unapplied-draft prompt is a child confirmation for Back routing"));
                Check(PC->ControlsUI->GetCategory() == ECategory::Display && OnScreen(PC->ControlsUI, FindButton(PC->ControlsUI, TEXT("VSyncDraft"))),
                    TEXT("The unapplied-draft prompt shows the display page it is about"));
                Capture(TEXT("DiscardDisplayDraft"));
            }
            // Issue #99: Back again on the prompt leaves without the draft instead of toggling the prompt away and back.
            else if (Stage == 16) { if (!Click(PC->ControlsUI, TEXT("Close"))) { return Finish(); } }
            else if (Stage == 17)
            {
                Check(!PC->ControlsUI->IsOpen() && !PC->ControlsUI->HasChildConfirmation(), TEXT("Back on the unapplied-draft prompt discards and closes the panel"));
                Check(FLDDisplaySettings::Read(*GEngine->GetGameUserSettings()) == OriginalDisplay && !Viewport->Viewport->IsFullscreen(),
                    TEXT("The discarded draft leaves quality and VSync unchanged"));
                if (!Click(PC->RootUI, TEXT("Settings"))) { return Finish(); }
            }
            else if (Stage == 18)
            {
                Check(PC->ControlsUI->GetCategory() == ECategory::Display, TEXT("Settings reopen on the page they were left on"));
                if (!Click(PC->ControlsUI, TEXT("VSyncDraft")) || !Click(PC->ControlsUI, TEXT("ApplyDisplayDraft"))) { return Finish(); }
                const auto Applied = FLDDisplaySettings::Read(*GEngine->GetGameUserSettings());
                Check(Instance->GetDisplaySettingsResult().Status == ELDDisplaySettingsStatus::PersistenceVerified && !Instance->AreDisplaySettingsDirty(),
                    TEXT("A VSync apply is saved and read back from disk even though the launch overrides the window"));
                Check(Applied.bVSync != OriginalDisplay.bVSync && Applied.Quality == OriginalDisplay.Quality, TEXT("Only the drafted VSync changed"));
                auto* Apply = FindButton(PC->ControlsUI, TEXT("ApplyDisplayDraft"));
                Check(Apply && !Apply->GetIsEnabled(), TEXT("An applied draft leaves nothing to apply"));
            }
            // Key rebinding in its row, through the real key path: a used key is refused while the capture keeps waiting.
            else if (Stage == 19)
            {
                if (!ShowPage(PC->ControlsUI, TEXT("TabControls"), ECategory::Controls, TEXT("BindStop"))) { return Finish(); }
                Reveal(FindButton(PC->ControlsUI, TEXT("BindStop")));
            }
            else if (Stage == 20)
            {
                auto* Key = FindButton(PC->ControlsUI, TEXT("BindStop"));
                if (Key) { Key->SetKeyboardFocus(); }
                if (!Click(PC->ControlsUI, TEXT("BindStop"))) { return Finish(); }
                Check(PC->ControlsUI->HasChildConfirmation(), TEXT("A key chip starts an in-row capture that Back routing treats as the top layer"));
                SendKey(Key, DefaultKey(TEXT("AttackMove")));
                Check(PC->Controls->Key(TEXT("Stop")) == DefaultKey(TEXT("Stop")) && PC->ControlsUI->HasChildConfirmation(),
                    TEXT("A key already used by another action is refused and the capture keeps waiting"));
            }
            // The refused row has scrolled itself fully into view by the next frame.
            else if (Stage == 21) { Capture(TEXT("Settings-RebindConflict")); }
            else if (Stage == 22)
            {
                auto* Key = FindButton(PC->ControlsUI, TEXT("BindStop"));
                SendKey(Key, EKeys::K);
                const auto Loaded = ULDControlSettings::LoadLocalSettings(Storage);
                Check(PC->Controls->Key(TEXT("Stop")) == EKeys::K && !PC->ControlsUI->HasChildConfirmation()
                    && Loaded.Settings && Loaded.Settings->Key(TEXT("Stop")) == EKeys::K,
                    TEXT("A free key rebinds the action at once and is saved and read back"));
                if (!Click(PC->ControlsUI, TEXT("BindStop"))) { return Finish(); }
                SendKey(Key, EKeys::Escape);
                Check(PC->ControlsUI->IsOpen() && !PC->ControlsUI->HasChildConfirmation() && PC->Controls->Key(TEXT("Stop")) == EKeys::K,
                    TEXT("Esc during a capture cancels only the capture and keeps the binding"));
            }
            // Restore defaults covers the shown page only, after an explicit confirmation.
            else if (Stage == 23)
            {
                if (!Click(PC->ControlsUI, TEXT("ResetCategoryDefaults"))) { return Finish(); }
                Check(PC->ControlsUI->HasChildConfirmation() && OnScreen(PC->ControlsUI, FindButton(PC->ControlsUI, TEXT("ConfirmLocalReset")))
                    && PC->Controls->Key(TEXT("Stop")) == EKeys::K, TEXT("Restore defaults asks before changing anything"));
                Capture(TEXT("Settings-ResetConfirm"));
            }
            else if (Stage == 24)
            {
                if (!Click(PC->ControlsUI, TEXT("ConfirmLocalReset"))) { return Finish(); }
                Check(PC->Controls->Key(TEXT("Stop")) == DefaultKey(TEXT("Stop")) && PC->Controls->TextScale == 1.25f
                    && PC->Controls->bShowNetworkDiagnostics && PC->Controls->bShowFPS && !PC->ControlsUI->HasChildConfirmation(),
                    TEXT("Restoring the controls page restores its keys only and leaves other pages unchanged"));
                if (!ShowPage(PC->ControlsUI, TEXT("TabInterface"), ECategory::Interface, TEXT("ToggleFPS"))
                    || !Click(PC->ControlsUI, TEXT("ResetCategoryDefaults")) || !Click(PC->ControlsUI, TEXT("ConfirmLocalReset"))) { return Finish(); }
                const auto Loaded = ULDControlSettings::LoadLocalSettings(Storage);
                Check(PC->Controls->TextScale == 1.f && !PC->Controls->bShowNetworkDiagnostics && !PC->Controls->bShowFPS
                    && Loaded.Settings && Loaded.Settings->TextScale == 1.f && !Loaded.Settings->bShowFPS
                    && Loaded.Settings->Key(TEXT("Stop")) == DefaultKey(TEXT("Stop")),
                    TEXT("Restoring the interface page restores text size and diagnostics and saves them"));
                Check(FLDDisplaySettings::Read(*GEngine->GetGameUserSettings()).bVSync != OriginalDisplay.bVSync,
                    TEXT("A preference page reset leaves the applied display values alone"));
            }
            else if (Stage == 25) { if (!Click(PC->ControlsUI, TEXT("Close"))) { return Finish(); } }
            // Closed from the interface page with nothing pending, Settings reopen on that page.
            else if (Stage == 26)
            {
                Check(!PC->ControlsUI->IsOpen(), TEXT("With the draft applied, Back closes the panel at once"));
                if (!Click(PC->RootUI, TEXT("Settings"))) { return Finish(); }
            }
            else if (Stage == 27)
            {
                Check(PC->ControlsUI->IsOpen() && PC->ControlsUI->GetCategory() == ECategory::Interface
                    && OnScreen(PC->ControlsUI, FindButton(PC->ControlsUI, TEXT("ToggleFPS")))
                    && !OnScreen(PC->ControlsUI, FindButton(PC->ControlsUI, TEXT("VSyncDraft"))),
                    TEXT("Settings reopen on the non-display page they were closed from"));
                if (!Click(PC->ControlsUI, TEXT("Close"))) { return Finish(); }
            }
            else
            {
                Check(!PC->ControlsUI->IsOpen(), TEXT("Back closes the reopened panel at once"));
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
