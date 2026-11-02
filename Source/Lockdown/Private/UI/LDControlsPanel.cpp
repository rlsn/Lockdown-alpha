#include "UI/LDControlsPanel.h"
#include "Components/ButtonSlot.h"
#include "UI/LDControlSettings.h"
#include "UI/LDWidgets.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameInstance.h"
#include "UI/LDUIRoot.h"
#include "Navigation/LDNavigationComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/WrapBox.h"
#include "Components/TextBlock.h"
#include "GameFramework/GameUserSettings.h"
#include "Engine/Engine.h"
#include "Styling/CoreStyle.h"

TSharedRef<SWidget> ULDControlsPanel::RebuildWidget()
{
    SetIsFocusable(true);
    if(!Rows)
    {
        auto* Canvas=WidgetTree->ConstructWidget<UCanvasPanel>();WidgetTree->RootWidget=Canvas;
        auto* Border=LDWidgets::Panel(WidgetTree,nullptr);
        auto* Frame=Canvas->AddChildToCanvas(Border);Frame->SetAnchors(FAnchors(.06f,.16f,.94f,.96f));Frame->SetOffsets(FMargin(0));
        auto* Main=WidgetTree->ConstructWidget<UVerticalBox>();Border->SetContent(Main);
        Main->AddChildToVerticalBox(LDWidgets::Text(WidgetTree,TEXT("CONTROLS & SETTINGS"),24));
        Main->AddChildToVerticalBox(LDWidgets::Text(WidgetTree,TEXT("The match does not pause. Local controls apply when confirmed below. Display changes need Apply; resolution and window mode must also be kept explicitly.")))->SetPadding(FMargin(0,8));
        Feedback=LDWidgets::Text(WidgetTree,TEXT("Choose a binding, then press a key to apply it. Escape cancels capture."));Main->AddChildToVerticalBox(Feedback);
        RecoveryRows=WidgetTree->ConstructWidget<UVerticalBox>();Main->AddChildToVerticalBox(RecoveryRows);
        DisplayFeedback=LDWidgets::Text(WidgetTree,TEXT(""));Main->AddChildToVerticalBox(DisplayFeedback);
        TrialRows=WidgetTree->ConstructWidget<UVerticalBox>();Main->AddChildToVerticalBox(TrialRows);
        CloseRows=WidgetTree->ConstructWidget<UVerticalBox>();Main->AddChildToVerticalBox(CloseRows);
        Preferences=LDWidgets::Text(WidgetTree,TEXT(""));Main->AddChildToVerticalBox(Preferences)->SetPadding(FMargin(0,6));
        auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>();Main->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        auto* Content=WidgetTree->ConstructWidget<UVerticalBox>();Scroll->AddChild(Content);
        DisplayRows=WidgetTree->ConstructWidget<UVerticalBox>();Content->AddChildToVerticalBox(DisplayRows);
        Content->AddChildToVerticalBox(LDWidgets::Text(WidgetTree,TEXT("LOCAL KEYS, CAMERA & TEXT"),20))->SetPadding(FMargin(0,16,0,6));
        auto* PreferencesButtons=WidgetTree->ConstructWidget<UWrapBox>();PreferencesButtons->SetInnerSlotPadding(FVector2D(8));
        Content->AddChildToVerticalBox(PreferencesButtons);
        PreferencesButtons->AddChildToWrapBox(LDWidgets::Button(WidgetTree,TEXT("Apply default bindings"),[this](){ChangePreference([](auto& S){S.ResetBindings();});}));
        PreferencesButtons->AddChildToWrapBox(LDWidgets::Button(WidgetTree,TEXT("Apply edge pan on/off"),[this](){ChangePreference([](auto& S){S.bEdgePan=!S.bEdgePan;});}));
        PreferencesButtons->AddChildToWrapBox(LDWidgets::Button(WidgetTree,TEXT("Apply start-follow on/off"),[this](){ChangePreference([](auto& S){S.bStartFollowing=!S.bStartFollowing;});}));
        PreferencesButtons->AddChildToWrapBox(LDWidgets::Button(WidgetTree,TEXT("Apply text 100 / 125%"),[this](){ChangePreference([](auto& S){S.TextScale=S.TextScale>1?1:1.25;});}));
        PreferencesButtons->AddChildToWrapBox(LDWidgets::Button(WidgetTree,TEXT("Reset keys, camera and text"),[this]()
        {
            CloseRows->ClearChildren();bCloseAfterConfirm=false;
            RecoveryRows->ClearChildren();
            RecoveryRows->AddChildToVerticalBox(ScaledText(TEXT("Reset only local keys, camera and text to the current defaults? Display settings and this match are unchanged.")));
            RecoveryRows->AddChildToVerticalBox(ScaledButton(TEXT("Confirm local reset"),[this](){ChangePreference([](auto& S){S.ResetLocalDefaults();});RecoveryRows->ClearChildren();}));
            RecoveryRows->AddChildToVerticalBox(ScaledButton(TEXT("Keep current local preferences"),[this](){RecoveryRows->ClearChildren();}));
        }));
        Rows=WidgetTree->ConstructWidget<UVerticalBox>();Content->AddChildToVerticalBox(Rows);
        auto* Footer=WidgetTree->ConstructWidget<UWrapBox>();Footer->SetInnerSlotPadding(FVector2D(8));Main->AddChildToVerticalBox(Footer)->SetPadding(FMargin(0,12,0,0));
        Footer->AddChildToWrapBox(LDWidgets::Button(WidgetTree,TEXT("Retry local save"),[this]()
        {if(auto* Instance=SettingsInstance.Get()){Feedback->SetText(FText::FromString(Instance->FlushLocalSettings(true).Message));Populate();}}));
        Footer->AddChildToWrapBox(LDWidgets::Button(WidgetTree,TEXT("Recover unreadable local settings"),[this](){ShowRecoveryConfirmation();}));
        Footer->AddChildToWrapBox(LDWidgets::Button(WidgetTree,TEXT("Close"),[this](){Close();}));
    }
    Populate();return Super::RebuildWidget();
}
UTextBlock* ULDControlsPanel::ScaledText(const FString& Text,int32 Size)
{
    const auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());
    return LDWidgets::Text(WidgetTree,Text,FMath::RoundToInt(Size*PC->Controls->TextScale));
}
ULDCommandButton* ULDControlsPanel::ScaledButton(const FString& Text,TFunction<void()> Action)
{
    auto* Button=LDWidgets::Button(WidgetTree,Text,MoveTemp(Action));
    auto* Caption=CastChecked<UTextBlock>(Button->GetContent());
    const auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());
    Caption->SetFont(FCoreStyle::GetDefaultFontStyle("Regular",FMath::RoundToInt(16*PC->Controls->TextScale)));
    Caption->SetAutoWrapText(true);
    Caption->SetJustification(ETextJustify::Center);
    CastChecked<UButtonSlot>(Caption->Slot)->SetHorizontalAlignment(HAlign_Fill);
    return Button;
}
void ULDControlsPanel::Populate()
{
    if(!Rows){return;}auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());Rows->ClearChildren();
    const float Scale=PC->Controls->TextScale;const int32 Size=FMath::RoundToInt(16*Scale);TArray<UWidget*> All;WidgetTree->GetAllWidgets(All);for(auto* W:All){if(auto* T=Cast<UTextBlock>(W)){auto Font=T->GetFont();Font.Size=FMath::RoundToInt(Font.Size*Scale/AppliedTextScale);T->SetFont(Font);}}AppliedTextScale=Scale;
    Preferences->SetText(FText::FromString(FString::Printf(TEXT("Applied preferences: edge pan %s   start-follow %s   text %.0f%%   zoom %.0f cm%s"),PC->Controls->bEdgePan?TEXT("ON"):TEXT("OFF"),PC->Controls->bStartFollowing?TEXT("ON"):TEXT("OFF"),100*PC->Controls->TextScale,PC->Controls->Zoom,PC->Controls->IsDirty()?TEXT("   NOT SAVED"):TEXT(""))));
    for(const auto& B:PC->Controls->Definitions())
    {
        const FName Id=B.Action;auto* Button=LDWidgets::Button(WidgetTree,B.Label+TEXT("  [")+PC->Controls->Label(Id)+TEXT("]"),[this,Id](){Capture=Id;Feedback->SetText(FText::FromString(TEXT("Press a key or mouse button to apply this binding. Escape cancels.")));});
        CastChecked<UTextBlock>(Button->GetContent())->SetFont(FCoreStyle::GetDefaultFontStyle("Regular",Size));Button->SetIsEnabled(Id!=TEXT("Back"));Rows->AddChildToVerticalBox(Button)->SetPadding(FMargin(0,3));
    }
}
void ULDControlsPanel::ChangePreference(TFunction<void(ULDControlSettings&)> Change)
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());
    if(auto* Instance=SettingsInstance.Get())
    {
        auto* Draft=PC->Controls->CreateDraft();
        Change(*Draft);
        Feedback->SetText(FText::FromString(Instance->ApplyLocalSettings(*Draft).Message));
        Populate();
    }
}

void ULDControlsPanel::ShowRecoveryConfirmation()
{
    CloseRows->ClearChildren();bCloseAfterConfirm=false;
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());
    RecoveryRows->ClearChildren();
    if(!PC->Controls->NeedsRecovery())
    {Feedback->SetText(FText::FromString(TEXT("No unreadable local settings need replacement. Use Retry local save for a write failure.")));return;}
    RecoveryRows->AddChildToVerticalBox(ScaledText(TEXT("Replace the unreadable file with the applied keys, camera and text preferences shown above? The old file remains unchanged until you confirm.")));
    RecoveryRows->AddChildToVerticalBox(ScaledButton(TEXT("Confirm replacement with applied preferences"),[this]()
    {if(auto* Instance=SettingsInstance.Get()){Feedback->SetText(FText::FromString(Instance->ConfirmLocalSettingsRecovery().Message));}RecoveryRows->ClearChildren();Populate();}));
    RecoveryRows->AddChildToVerticalBox(ScaledButton(TEXT("Keep file unchanged"),[this](){RecoveryRows->ClearChildren();}));
}

void ULDControlsPanel::RefreshDisplayDraft()
{
    auto* Instance=SettingsInstance.Get();
    bDisplayAvailable=false;bDisplayDraftDirty=false;
    if(!Instance){return;}
    const auto Result=Instance->ReadDisplaySettings(DisplayDraft);
    bDisplayAvailable=Result.Status==ELDDisplaySettingsStatus::Loaded;
    DisplayFeedback->SetText(FText::FromString(Result.Message));
    Resolutions.Reset();
    FString Error;
    if(bDisplayAvailable&&!Instance->GetDisplayResolutions(Resolutions,Error))
    {DisplayFeedback->SetText(FText::FromString(Error));}
    PopulateDisplay();
}

void ULDControlsPanel::PopulateDisplay()
{
    if(!DisplayRows){return;}
    DisplayRows->ClearChildren();TrialRows->ClearChildren();
    DisplayRows->AddChildToVerticalBox(ScaledText(TEXT("DISPLAY"),20))->SetPadding(FMargin(0,12,0,6));
    auto* Instance=SettingsInstance.Get();
    if(!bDisplayAvailable||!Instance){return;}
    const bool Trial=TrialId!=0&&Instance->GetDisplayTrialId()==TrialId;
    const bool Editing=!Trial&&!Instance->IsDisplayRestorePending();
    auto Row=[&](const FString& Label,TFunction<void()> Action,bool Enabled=true)
    {
        auto* Button=ScaledButton(Label,MoveTemp(Action));
        Button->SetIsEnabled(Enabled);
        DisplayRows->AddChildToVerticalBox(Button)->SetPadding(FMargin(0,3));
    };
    const TCHAR* Mode=DisplayDraft.WindowMode==EWindowMode::Fullscreen?TEXT("Fullscreen"):DisplayDraft.WindowMode==EWindowMode::WindowedFullscreen?TEXT("Borderless"):TEXT("Windowed");
    Row(FString::Printf(TEXT("Window mode: %s (change draft)"),Mode),[this]()
    {
        DisplayDraft.WindowMode=DisplayDraft.WindowMode==EWindowMode::Windowed?EWindowMode::WindowedFullscreen
            :DisplayDraft.WindowMode==EWindowMode::WindowedFullscreen?EWindowMode::Fullscreen:EWindowMode::Windowed;
        if(DisplayDraft.WindowMode==EWindowMode::WindowedFullscreen){DisplayDraft.Resolution=GEngine->GetGameUserSettings()->GetDesktopResolution();}
        bDisplayDraftDirty=true;PopulateDisplay();
    },Editing);
    Row(FString::Printf(TEXT("Resolution: %d x %d (next supported size)"),DisplayDraft.Resolution.X,DisplayDraft.Resolution.Y),[this]()
    {
        const int32 Current=Resolutions.IndexOfByKey(DisplayDraft.Resolution);
        DisplayDraft.Resolution=Resolutions[(Current+1)%Resolutions.Num()];
        bDisplayDraftDirty=true;PopulateDisplay();
    },Editing&&!Resolutions.IsEmpty()&&DisplayDraft.WindowMode!=EWindowMode::WindowedFullscreen);
    Row(FString(TEXT("VSync draft: "))+ (DisplayDraft.bVSync?TEXT("On"):TEXT("Off")),[this](){DisplayDraft.bVSync=!DisplayDraft.bVSync;bDisplayDraftDirty=true;PopulateDisplay();},Editing);
    const int32 Preset=DisplayDraft.Quality.GetSingleQualityLevel();
    const TCHAR* Names[]={TEXT("Low"),TEXT("Medium"),TEXT("High"),TEXT("Epic"),TEXT("Cinematic")};
    Row(FString(TEXT("Quality draft: "))+(Preset>=0&&Preset<5?Names[Preset]:TEXT("Custom"))+TEXT(" (next preset)"),[this]()
    {DisplayDraft.Quality.SetFromSingleQualityLevel((DisplayDraft.Quality.GetSingleQualityLevel()+1)%5);bDisplayDraftDirty=true;PopulateDisplay();},Editing);
    Row(TEXT("Apply display draft"),[this](){ApplyDisplay();},Editing&&bDisplayDraftDirty);
    if(Instance->AreDisplaySettingsDirty())
    {Row(TEXT("Retry display save"),[this](){if(auto* Owner=SettingsInstance.Get()){DisplayFeedback->SetText(FText::FromString(Owner->RetryDisplaySave().Message));PopulateDisplay();}},Editing);}
    if(Instance->IsDisplayRestorePending())
    {Row(TEXT("Retry previous display mode"),[this](){if(auto* Owner=SettingsInstance.Get()){DisplayFeedback->SetText(FText::FromString(Owner->RetryDisplayRestore().Message));PopulateDisplay();}});}
    if(Trial)
    {
        const uint64 Id=TrialId;
        TrialRows->AddChildToVerticalBox(ScaledButton(TEXT("Keep this display mode"),[this,Id](){ConfirmDisplay(Id);}));
        TrialRows->AddChildToVerticalBox(ScaledButton(TEXT("Revert display mode"),[this,Id](){CancelDisplay(Id);}));
    }
}

void ULDControlsPanel::ApplyDisplay()
{
    if(auto* Instance=SettingsInstance.Get())
    {
        const auto Result=Instance->ApplyDisplaySettings(DisplayDraft,this);
        DisplayFeedback->SetText(FText::FromString(Result.Message));
        if(Result.Status==ELDDisplaySettingsStatus::TrialStarted){TrialId=Result.TrialId;}
        else if(Result.Status==ELDDisplaySettingsStatus::PersistenceVerified||Result.Status==ELDDisplaySettingsStatus::PersistenceFailed)
        {bDisplayDraftDirty=false;if(bCloseAfterConfirm){ForceClose();return;}}
        PopulateDisplay();
    }
}
void ULDControlsPanel::ConfirmDisplay(uint64 Id)
{
    if(auto* Instance=SettingsInstance.Get())
    {
        const auto Result=Instance->ConfirmDisplayTrial(Id);
        DisplayFeedback->SetText(FText::FromString(Result.Message));
        if(!Instance->GetDisplayTrialId()&&TrialId==Id)
        {
            TrialId=0;bDisplayDraftDirty=Result.Status==ELDDisplaySettingsStatus::ApplyFailed;
            if(Result.Status==ELDDisplaySettingsStatus::Cancelled||Result.Status==ELDDisplaySettingsStatus::RestorePending||Result.Status==ELDDisplaySettingsStatus::RestoreFailed)
            {bCloseAfterConfirm=false;RefreshDisplayDraft();DisplayFeedback->SetText(FText::FromString(Result.Message));}
            if(bCloseAfterConfirm&&(Result.Status==ELDDisplaySettingsStatus::PersistenceVerified||Result.Status==ELDDisplaySettingsStatus::PersistenceFailed))
            {ForceClose();return;}
        }
        PopulateDisplay();
    }
}
void ULDControlsPanel::CancelDisplay(uint64 Id)
{
    if(auto* Instance=SettingsInstance.Get())
    {
        const auto Result=Instance->CancelDisplayTrial(Id);
        if(Result.Status==ELDDisplaySettingsStatus::StaleTrial){return;}
        TrialId=0;bCloseAfterConfirm=false;
        RefreshDisplayDraft();
        DisplayFeedback->SetText(FText::FromString(Result.Message));
    }
}

void ULDControlsPanel::Open()
{
    bOpen=true;Capture=NAME_None;bCloseAfterConfirm=false;
    SettingsInstance=GetGameInstance<ULDGameInstance>();
    SetVisibility(ESlateVisibility::Visible);
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());PC->SetMenuOpen(true);
    RecoveryRows->ClearChildren();CloseRows->ClearChildren();
    if(auto* Instance=SettingsInstance.Get()){Feedback->SetText(FText::FromString(Instance->GetLocalSettingsMessage()));}
    SetKeyboardFocus();Populate();RefreshDisplayDraft();
}
void ULDControlsPanel::ShowCloseConfirmation()
{
    RecoveryRows->ClearChildren();
    CloseRows->ClearChildren();
    CloseRows->AddChildToVerticalBox(ScaledText(TEXT("Display edits have not been applied. Applied local keys and camera preferences will be kept.")));
    CloseRows->AddChildToVerticalBox(ScaledButton(TEXT("Apply display and return"),[this](){bCloseAfterConfirm=true;CloseRows->ClearChildren();ApplyDisplay();}));
    CloseRows->AddChildToVerticalBox(ScaledButton(TEXT("Discard display draft and return"),[this](){ForceClose();}));
    CloseRows->AddChildToVerticalBox(ScaledButton(TEXT("Continue editing"),[this](){bCloseAfterConfirm=false;CloseRows->ClearChildren();}));
}
void ULDControlsPanel::Close()
{
    if(!Capture.IsNone()){Capture=NAME_None;Feedback->SetText(FText::FromString(TEXT("Binding unchanged.")));return;}
    if(TrialId){CancelDisplay(TrialId);return;}
    if(RecoveryRows&&RecoveryRows->GetChildrenCount()){RecoveryRows->ClearChildren();return;}
    if(CloseRows&&CloseRows->GetChildrenCount()){CloseRows->ClearChildren();bCloseAfterConfirm=false;return;}
    if(bDisplayDraftDirty){ShowCloseConfirmation();return;}
    ForceClose();
}
void ULDControlsPanel::ForceClose()
{
    if(auto* Instance=SettingsInstance.Get()){Instance->CancelDisplayTrial(TrialId);Instance->FlushLocalSettings();}
    TrialId=0;Capture=NAME_None;bOpen=false;bDisplayDraftDirty=false;bCloseAfterConfirm=false;
    SetVisibility(ESlateVisibility::Collapsed);
    if(auto* PC=Cast<ALDPlayerController>(GetOwningPlayer()))
    {
        PC->SetMenuOpen(false);
        if(PC->IsFrontend()&&PC->RootUI&&!PC->RootUI->IsMenuOpen()){PC->RootUI->ToggleMenu();}
    }
}
void ULDControlsPanel::NativeDestruct()
{
    if(auto* Instance=SettingsInstance.Get()){Instance->CancelDisplayTrial(TrialId);Instance->FlushLocalSettings();}
    TrialId=0;bOpen=false;
    Super::NativeDestruct();
}
void ULDControlsPanel::NativeTick(const FGeometry& Geometry,float Delta)
{
    Super::NativeTick(Geometry,Delta);
    auto* Instance=SettingsInstance.Get();
    if(!bOpen||!Instance){return;}
    const auto& Result=Instance->GetDisplaySettingsResult();
    if(TrialId&&Instance->GetDisplayTrialId()!=TrialId)
    {TrialId=0;bCloseAfterConfirm=false;RefreshDisplayDraft();DisplayFeedback->SetText(FText::FromString(Result.Message));}
    if(TrialId)
    {DisplayFeedback->SetText(FText::FromString(FString::Printf(TEXT("%s\nReverting in %.0f seconds unless kept."),*Result.Message,FMath::CeilToDouble(Instance->GetDisplayTrialRemaining()))));}
    else if(Result.Status!=SeenDisplayStatus)
    {DisplayFeedback->SetText(FText::FromString(Result.Message));PopulateDisplay();}
    SeenDisplayStatus=Result.Status;
}
FReply ULDControlsPanel::CaptureKey(FKey Key)
{
    if(Key==EKeys::Escape){Capture=NAME_None;Feedback->SetText(FText::FromString(TEXT("Binding unchanged.")));return FReply::Handled();}
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());FString Error;auto* Draft=PC->Controls->CreateDraft();
    if(Draft->Rebind(Capture,Key,Error))
    {Capture=NAME_None;if(auto* Instance=SettingsInstance.Get()){Feedback->SetText(FText::FromString(Instance->ApplyLocalSettings(*Draft).Message));}Populate();}
    else{Feedback->SetText(FText::FromString(Error));}
    return FReply::Handled();
}
FReply ULDControlsPanel::NativeOnKeyDown(const FGeometry& G,const FKeyEvent& E)
{if(!Capture.IsNone()&&!E.IsRepeat()){return CaptureKey(E.GetKey());}if(E.GetKey()==EKeys::Escape){if(!E.IsRepeat()){Close();}return FReply::Handled();}return Super::NativeOnKeyDown(G,E);}
FReply ULDControlsPanel::NativeOnPreviewKeyDown(const FGeometry& G,const FKeyEvent& E)
{if(!Capture.IsNone()){return E.IsRepeat()?FReply::Handled():CaptureKey(E.GetKey());}if(E.GetKey()==EKeys::Escape){if(!E.IsRepeat()){Close();}return FReply::Handled();}return Super::NativeOnPreviewKeyDown(G,E);}
FReply ULDControlsPanel::NativeOnPreviewMouseButtonDown(const FGeometry& G,const FPointerEvent& E)
{if(!Capture.IsNone()){return CaptureKey(E.GetEffectingButton());}return Super::NativeOnPreviewMouseButtonDown(G,E);}
FReply ULDControlsPanel::NativeOnMouseButtonDown(const FGeometry& G,const FPointerEvent& E)
{if(!Capture.IsNone()){return CaptureKey(E.GetEffectingButton());}return FReply::Handled();}
FReply ULDControlsPanel::NativeOnMouseWheel(const FGeometry& G,const FPointerEvent& E)
{if(!Capture.IsNone()){return CaptureKey(EKeys::MouseWheelAxis);}return Super::NativeOnMouseWheel(G,E);}
