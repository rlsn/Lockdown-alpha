#include "UI/LDControlsPanel.h"
#include "Audio/LDAudioSubsystem.h"
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
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/WrapBox.h"
#include "Components/TextBlock.h"
#include "GameFramework/GameUserSettings.h"
#include "Engine/Engine.h"
#include "Styling/CoreStyle.h"

namespace
{
    // Updates a control RebuildWidget constructed once under a stable name; a missing name is a construction bug.
    void ControlsSetButton(UWidgetTree* Tree,FName Name,const FString& Label,bool Enabled,bool bShown=true)
    {
        auto* Button=CastChecked<UButton>(Tree->FindWidget(Name));
        CastChecked<UTextBlock>(Button->GetContent())->SetText(FText::FromString(Label));
        Button->SetIsEnabled(Enabled);Button->SetVisibility(bShown?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    }
    const TCHAR* ControlsOnOff(bool Value){return Value?TEXT("开"):TEXT("关");}
    // One press of the camera pan speed buttons; the legal domain itself belongs to ULDControlSettings.
    constexpr float ControlsPanStepCmPerSec=300;
    constexpr ELDMinimapOrientation ControlsOrientations[]={ELDMinimapOrientation::Camera,ELDMinimapOrientation::Facing,ELDMinimapOrientation::North};
    FName ControlsOrientationButton(ELDMinimapOrientation Value)
    {return Value==ELDMinimapOrientation::Facing?FName(TEXT("MinimapFacing")):Value==ELDMinimapOrientation::North?FName(TEXT("MinimapNorth")):FName(TEXT("MinimapCamera"));}
    // The applied orientation keeps an amber fill; the row label also names it, so the choice never relies on colour alone.
    void ControlsMarkChoice(UWidgetTree* Tree,FName Name,bool bChosen,const FSlateColor& RestTint)
    {
        auto* Button=CastChecked<UButton>(Tree->FindWidget(Name));FButtonStyle Style=Button->GetStyle();
        Style.Normal.TintColor=bChosen?FSlateColor(FLinearColor(.36f,.27f,.06f)):RestTint;Button->SetStyle(Style);
    }
}

TSharedRef<SWidget> ULDControlsPanel::RebuildWidget()
{
    SetIsFocusable(true);
    if(!Rows)
    {
        // Every interactive control is built once under a stable name (automation and Back routing find it by name);
        // later refreshes only change text, enablement and visibility. Rebuilt binding rows stay unnamed because a
        // detached widget keeps its name until garbage collection, so reusing a name would collide.
        auto WrapButton=[this](const TCHAR* Label,TFunction<void()> Action,FName Name)
        {
            auto* Button=LDWidgets::Button(WidgetTree,Label,MoveTemp(Action),false,Name);
            auto* Caption=CastChecked<UTextBlock>(Button->GetContent());
            Caption->SetAutoWrapText(true);Caption->SetJustification(ETextJustify::Center);
            CastChecked<UButtonSlot>(Caption->Slot)->SetHorizontalAlignment(HAlign_Fill);
            return Button;
        };
        auto ChildRows=[this](const TCHAR* Prompt)
        {
            auto* Box=WidgetTree->ConstructWidget<UVerticalBox>();Box->SetVisibility(ESlateVisibility::Collapsed);
            Box->AddChildToVerticalBox(LDWidgets::Text(WidgetTree,Prompt));return Box;
        };
        auto Choice=[&WrapButton](UVerticalBox* Box,const TCHAR* Label,FName Name,TFunction<void()> Action)
        {Box->AddChildToVerticalBox(WrapButton(Label,MoveTemp(Action),Name))->SetPadding(FMargin(0,3));};
        auto Section=[this](UVerticalBox* Parent,const TCHAR* Title)
        {Parent->AddChildToVerticalBox(LDWidgets::Text(WidgetTree,Title,20))->SetPadding(FMargin(0,16,0,6));};
        auto* Canvas=WidgetTree->ConstructWidget<UCanvasPanel>();WidgetTree->RootWidget=Canvas;
        auto* Border=LDWidgets::Panel(WidgetTree,nullptr);
        auto* Frame=Canvas->AddChildToCanvas(Border);Frame->SetAnchors(FAnchors(.06f,.16f,.94f,.96f));Frame->SetOffsets(FMargin(0));
        auto* Main=WidgetTree->ConstructWidget<UVerticalBox>();Border->SetContent(Main);
        Main->AddChildToVerticalBox(LDWidgets::Text(WidgetTree,TEXT("设置"),24));
        Main->AddChildToVerticalBox(LDWidgets::Text(WidgetTree,TEXT("对局不会暂停。按键、镜头、文字、小地图与音量改动确认后立即应用；显示改动需要“应用显示草稿”，分辨率与窗口模式还要在倒计时内选择保留。")))->SetPadding(FMargin(0,8));
        Feedback=LDWidgets::Text(WidgetTree,TEXT("选择一项按键后按下新键即可应用。Esc 取消。"));Main->AddChildToVerticalBox(Feedback);
        ResetRows=ChildRows(TEXT("恢复默认值？将恢复：显示/画质（分辨率、窗口模式、垂直同步、整体画质）、音量、按键、镜头/文字（含镜头平移速度与小地图朝向）。名称与本局进度不变。分辨率或窗口模式改变时仍需在倒计时内选择保留，否则只恢复原显示模式。"));Main->AddChildToVerticalBox(ResetRows);
        Choice(ResetRows,TEXT("确认恢复默认"),TEXT("ConfirmLocalReset"),[this](){ConfirmResetDefaults();});
        Choice(ResetRows,TEXT("保持当前设置"),TEXT("KeepLocalPreferences"),[this](){ShowChild(EChildConfirmation::None);});
        RecoveryRows=ChildRows(TEXT("用当前已应用的名称、按键、镜头、文字、小地图与音量偏好替换无法读取或不兼容的设置文件？确认前旧文件保持不变。"));Main->AddChildToVerticalBox(RecoveryRows);
        Choice(RecoveryRows,TEXT("确认替换"),TEXT("ConfirmRecovery"),[this]()
        {ShowChild(EChildConfirmation::None);if(auto* Instance=SettingsInstance.Get()){Feedback->SetText(FText::FromString(Instance->ConfirmLocalSettingsRecovery().Message));}Populate();});
        Choice(RecoveryRows,TEXT("保持文件不变"),TEXT("KeepSettingsFile"),[this](){ShowChild(EChildConfirmation::None);});
        DisplayFeedback=LDWidgets::Text(WidgetTree,TEXT(""));Main->AddChildToVerticalBox(DisplayFeedback);
        TrialRows=WidgetTree->ConstructWidget<UVerticalBox>();TrialRows->SetVisibility(ESlateVisibility::Collapsed);Main->AddChildToVerticalBox(TrialRows);
        TrialRows->AddChildToVerticalBox(WrapButton(TEXT("保留此显示模式"),[this](){if(TrialId){ConfirmDisplay(TrialId);}},TEXT("KeepDisplayMode")))->SetPadding(FMargin(0,3));
        TrialRows->AddChildToVerticalBox(WrapButton(TEXT("恢复原显示模式"),[this](){if(TrialId){CancelDisplay(TrialId);}},TEXT("RevertDisplayMode")))->SetPadding(FMargin(0,3));
        CloseRows=ChildRows(TEXT("显示改动尚未应用。已应用的按键与镜头偏好会保留。"));Main->AddChildToVerticalBox(CloseRows);
        Choice(CloseRows,TEXT("应用显示并返回"),TEXT("ApplyDisplayAndReturn"),[this](){ShowChild(EChildConfirmation::None);bCloseAfterConfirm=true;ApplyDisplay();});
        Choice(CloseRows,TEXT("放弃显示草稿并返回"),TEXT("DiscardDisplayDraft"),[this](){ForceClose();});
        Choice(CloseRows,TEXT("继续编辑"),TEXT("ContinueEditing"),[this](){ShowChild(EChildConfirmation::None);});
        Preferences=LDWidgets::Text(WidgetTree,TEXT(""));Main->AddChildToVerticalBox(Preferences)->SetPadding(FMargin(0,6));
        auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>();Main->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        auto* Content=WidgetTree->ConstructWidget<UVerticalBox>();Scroll->AddChild(Content);
        Section(Content,TEXT("显示 / 画质"));
        DisplayRows=WidgetTree->ConstructWidget<UVerticalBox>();DisplayRows->SetVisibility(ESlateVisibility::Collapsed);Content->AddChildToVerticalBox(DisplayRows);
        auto DisplayRow=[&](FName Name,TFunction<void()> Action){DisplayRows->AddChildToVerticalBox(WrapButton(TEXT(""),MoveTemp(Action),Name))->SetPadding(FMargin(0,3));};
        DisplayRow(TEXT("WindowModeDraft"),[this]()
        {
            DisplayDraft.WindowMode=DisplayDraft.WindowMode==EWindowMode::Windowed?EWindowMode::WindowedFullscreen
                :DisplayDraft.WindowMode==EWindowMode::WindowedFullscreen?EWindowMode::Fullscreen:EWindowMode::Windowed;
            if(DisplayDraft.WindowMode==EWindowMode::WindowedFullscreen){DisplayDraft.Resolution=GEngine->GetGameUserSettings()->GetDesktopResolution();}
            bDisplayDraftDirty=true;PopulateDisplay();
        });
        DisplayRow(TEXT("ResolutionDraft"),[this]()
        {
            if(Resolutions.IsEmpty()){DisplayFeedback->SetText(FText::FromString(TEXT("显卡驱动没有提供支持的分辨率，无法切换分辨率。")));return;}
            const int32 Current=Resolutions.IndexOfByKey(DisplayDraft.Resolution);
            DisplayDraft.Resolution=Resolutions[(Current+1)%Resolutions.Num()];
            bDisplayDraftDirty=true;PopulateDisplay();
        });
        DisplayRow(TEXT("VSyncDraft"),[this](){DisplayDraft.bVSync=!DisplayDraft.bVSync;bDisplayDraftDirty=true;PopulateDisplay();});
        DisplayRow(TEXT("QualityDraft"),[this]()
        {DisplayDraft.Quality.SetFromSingleQualityLevel((DisplayDraft.Quality.GetSingleQualityLevel()+1)%5);bDisplayDraftDirty=true;PopulateDisplay();});
        DisplayRow(TEXT("ApplyDisplayDraft"),[this](){ApplyDisplay();});
        DisplayRow(TEXT("RetryDisplaySave"),[this](){if(auto* Owner=SettingsInstance.Get()){DisplayFeedback->SetText(FText::FromString(Owner->RetryDisplaySave().Message));PopulateDisplay();}});
        DisplayRow(TEXT("RetryDisplayRestore"),[this](){if(auto* Owner=SettingsInstance.Get()){DisplayFeedback->SetText(FText::FromString(Owner->RetryDisplayRestore().Message));PopulateDisplay();}});
        Section(Content,TEXT("音量"));
        VolumeLabel=LDWidgets::Text(WidgetTree,TEXT("主音量 100%"));Content->AddChildToVerticalBox(VolumeLabel);
        VolumeSlider=WidgetTree->ConstructWidget<USlider>(USlider::StaticClass(),TEXT("MasterVolume"));
        VolumeSlider->SetMinValue(0);VolumeSlider->SetMaxValue(1);VolumeSlider->SetStepSize(.05f);VolumeSlider->IsFocusable=true;
        VolumeSlider->OnValueChanged.AddDynamic(this,&ULDControlsPanel::HandleMasterVolumeChanged);
        VolumeSlider->OnMouseCaptureBegin.AddDynamic(this,&ULDControlsPanel::HandleVolumeMouseCaptureBegin);VolumeSlider->OnMouseCaptureEnd.AddDynamic(this,&ULDControlsPanel::HandleVolumeMouseCaptureEnd);
        VolumeSlider->OnControllerCaptureBegin.AddDynamic(this,&ULDControlsPanel::HandleVolumeControllerCaptureBegin);VolumeSlider->OnControllerCaptureEnd.AddDynamic(this,&ULDControlsPanel::HandleVolumeControllerCaptureEnd);
        // The vertical box would stretch the size box to the full width; a left-aligned row keeps the slider at its override.
        auto* VolumeBox=WidgetTree->ConstructWidget<USizeBox>();VolumeBox->SetWidthOverride(360);VolumeBox->SetHeightOverride(32);VolumeBox->SetContent(VolumeSlider);
        auto* VolumeRow=Content->AddChildToVerticalBox(VolumeBox);VolumeRow->SetHorizontalAlignment(HAlign_Left);VolumeRow->SetPadding(FMargin(0,6));
        Section(Content,TEXT("按键"));
        auto* KeyButtons=WidgetTree->ConstructWidget<UWrapBox>();KeyButtons->SetInnerSlotPadding(FVector2D(8));Content->AddChildToVerticalBox(KeyButtons);
        KeyButtons->AddChildToWrapBox(LDWidgets::Button(WidgetTree,TEXT("恢复默认按键"),[this](){ChangePreference([](auto& S){S.ResetBindings();});},false,TEXT("ResetBindings")));
        Rows=WidgetTree->ConstructWidget<UVerticalBox>();Content->AddChildToVerticalBox(Rows);
        Section(Content,TEXT("镜头 / 文字"));
        auto* CameraButtons=WidgetTree->ConstructWidget<UWrapBox>();CameraButtons->SetInnerSlotPadding(FVector2D(8));Content->AddChildToVerticalBox(CameraButtons);
        CameraButtons->AddChildToWrapBox(LDWidgets::Button(WidgetTree,TEXT("边缘平移 开/关"),[this](){ChangePreference([](auto& S){S.bEdgePan=!S.bEdgePan;});},false,TEXT("ToggleEdgePan")));
        CameraButtons->AddChildToWrapBox(LDWidgets::Button(WidgetTree,TEXT("开局镜头跟随 开/关"),[this](){ChangePreference([](auto& S){S.bStartFollowing=!S.bStartFollowing;});},false,TEXT("ToggleStartFollow")));
        CameraButtons->AddChildToWrapBox(LDWidgets::Button(WidgetTree,TEXT("文字大小 100% / 125%"),[this](){ChangePreference([](auto& S){S.TextScale=S.TextScale>1?1:1.25;});},false,TEXT("ApplyTextScale")));
        PanSpeedLabel=LDWidgets::Text(WidgetTree,TEXT(""));Content->AddChildToVerticalBox(PanSpeedLabel)->SetPadding(FMargin(0,12,0,4));
        auto* PanButtons=WidgetTree->ConstructWidget<UWrapBox>();PanButtons->SetInnerSlotPadding(FVector2D(8));Content->AddChildToVerticalBox(PanButtons);
        PanButtons->AddChildToWrapBox(LDWidgets::Button(WidgetTree,TEXT(""),[this]()
        {ChangePreference([](auto& S){S.CameraPanCmPerSec=FMath::Max(S.CameraPanCmPerSec-ControlsPanStepCmPerSec,ULDControlSettings::MinCameraPanCmPerSec);});},false,TEXT("SlowerCameraPan")));
        PanButtons->AddChildToWrapBox(LDWidgets::Button(WidgetTree,TEXT(""),[this]()
        {ChangePreference([](auto& S){S.CameraPanCmPerSec=FMath::Min(S.CameraPanCmPerSec+ControlsPanStepCmPerSec,ULDControlSettings::MaxCameraPanCmPerSec);});},false,TEXT("FasterCameraPan")));
        OrientationLabel=LDWidgets::Text(WidgetTree,TEXT(""));Content->AddChildToVerticalBox(OrientationLabel)->SetPadding(FMargin(0,12,0,4));
        auto* OrientationButtons=WidgetTree->ConstructWidget<UWrapBox>();OrientationButtons->SetInnerSlotPadding(FVector2D(8));Content->AddChildToVerticalBox(OrientationButtons);
        for(const ELDMinimapOrientation Option:ControlsOrientations)
        {
            OrientationButtons->AddChildToWrapBox(LDWidgets::Button(WidgetTree,ULDControlSettings::MinimapOrientationLabel(Option),[this,Option]()
            {ChangePreference([Option](auto& S){S.MinimapOrientation=Option;});},false,ControlsOrientationButton(Option)));
        }
        ChoiceRestTint=CastChecked<UButton>(WidgetTree->FindWidget(ControlsOrientationButton(ELDMinimapOrientation::Camera)))->GetStyle().Normal.TintColor;
        auto* Footer=WidgetTree->ConstructWidget<UWrapBox>();Footer->SetInnerSlotPadding(FVector2D(8));Main->AddChildToVerticalBox(Footer)->SetPadding(FMargin(0,12,0,0));
        Footer->AddChildToWrapBox(LDWidgets::Button(WidgetTree,TEXT("恢复显示、画质、音量、按键、镜头、文字与小地图默认值"),[this](){ShowChild(EChildConfirmation::LocalReset);},false,TEXT("ResetLocalDefaults")));
        Footer->AddChildToWrapBox(LDWidgets::Button(WidgetTree,TEXT("重试本地保存"),[this]()
        {if(auto* Instance=SettingsInstance.Get()){Feedback->SetText(FText::FromString(Instance->FlushLocalSettings(true).Message));Populate();}},false,TEXT("RetryLocalSave")));
        Footer->AddChildToWrapBox(LDWidgets::Button(WidgetTree,TEXT("恢复无法读取或不兼容的设置"),[this](){ShowRecoveryConfirmation();},false,TEXT("RecoverLocalSettings")));
        Footer->AddChildToWrapBox(LDWidgets::Button(WidgetTree,TEXT("返回"),[this](){Close();},false,TEXT("Close")));
    }
    Populate();return Super::RebuildWidget();
}
void ULDControlsPanel::Populate()
{
    if(!Rows){return;}auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());Rows->ClearChildren();
    const float Scale=PC->Controls->TextScale;const int32 Size=FMath::RoundToInt(16*Scale);TArray<UWidget*> All;WidgetTree->GetAllWidgets(All);for(auto* W:All){if(auto* T=Cast<UTextBlock>(W)){auto Font=T->GetFont();Font.Size=FMath::RoundToInt(Font.Size*Scale/AppliedTextScale);T->SetFont(Font);}}AppliedTextScale=Scale;
    const auto& Applied=*PC->Controls;
    PopulatePreferences();
    bSyncingVolume=true;VolumeSlider->SetValue(Applied.MasterVolume);bSyncingVolume=false;
    for(const auto& B:Applied.Definitions())
    {
        const FName Id=B.Action;auto* Button=LDWidgets::Button(WidgetTree,B.Label+TEXT("  [")+Applied.Label(Id)+TEXT("]"),[this,Id](){Capture=Id;Feedback->SetText(FText::FromString(TEXT("按下新的键盘按键或鼠标按键以应用此绑定。Esc 取消。")));});
        CastChecked<UTextBlock>(Button->GetContent())->SetFont(FCoreStyle::GetDefaultFontStyle("Regular",Size));Button->SetIsEnabled(Id!=TEXT("Back"));Rows->AddChildToVerticalBox(Button)->SetPadding(FMargin(0,3));
    }
}
void ULDControlsPanel::PopulatePreferences()
{
    const auto& Applied=*CastChecked<ALDPlayerController>(GetOwningPlayer())->Controls;const int32 Volume=FMath::RoundToInt(100*Applied.MasterVolume);
    const float Pan=Applied.GetCameraPanCmPerSec();
    const ELDMinimapOrientation Orientation=Applied.GetMinimapOrientation();const FString OrientationName=ULDControlSettings::MinimapOrientationLabel(Orientation);
    Preferences->SetText(FText::FromString(FString::Printf(TEXT("已应用：边缘平移 %s · 开局镜头跟随 %s · 镜头平移速度 %.0f cm/s · 小地图朝向 %s · 文字 %.0f%% · 镜头距离 %.0f 厘米 · 主音量 %d%%%s"),ControlsOnOff(Applied.bEdgePan),ControlsOnOff(Applied.bStartFollowing),Pan,*OrientationName,100*Applied.TextScale,Applied.Zoom,Volume,Applied.IsDirty()?TEXT(" · 尚未保存"):TEXT(""))));
    VolumeLabel->SetText(FText::FromString(FString::Printf(TEXT("主音量 %d%%"),Volume)));
    PanSpeedLabel->SetText(FText::FromString(FString::Printf(TEXT("镜头平移速度 %.0f cm/s"),Pan)));
    ControlsSetButton(WidgetTree,TEXT("SlowerCameraPan"),FString::Printf(TEXT("减慢 %.0f cm/s"),ControlsPanStepCmPerSec),Pan>ULDControlSettings::MinCameraPanCmPerSec);
    ControlsSetButton(WidgetTree,TEXT("FasterCameraPan"),FString::Printf(TEXT("加快 %.0f cm/s"),ControlsPanStepCmPerSec),Pan<ULDControlSettings::MaxCameraPanCmPerSec);
    OrientationLabel->SetText(FText::FromString(TEXT("小地图朝向：")+OrientationName));
    for(const ELDMinimapOrientation Option:ControlsOrientations){ControlsMarkChoice(WidgetTree,ControlsOrientationButton(Option),Option==Orientation,ChoiceRestTint);}
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

void ULDControlsPanel::HandleMasterVolumeChanged(float Value)
{
    auto* Instance=SettingsInstance.Get();auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    if(bSyncingVolume||!Instance||!PC||!PC->Controls){return;}
    // A drag reports many sub-percent values; only a changed whole percent is applied, and the instance saves it later.
    const float Linear=FMath::RoundToFloat(Value*100)/100;
    if(Linear==PC->Controls->MasterVolume){return;}
    const auto Result=Instance->SetAppliedMasterVolume(Linear);
    Feedback->SetText(FText::FromString(Result.Message));PopulatePreferences();
    if(Result.Status==ELDLocalSettingsSaveStatus::Invalid){return;}
    bVolumeSavePending=true;
    if(!bVolumeMouseCapture&&!bVolumeControllerCapture){PreviewMasterVolume();}
}
void ULDControlsPanel::HandleVolumeMouseCaptureBegin(){bVolumeMouseCapture=true;}
void ULDControlsPanel::HandleVolumeMouseCaptureEnd(){EndVolumeCapture(bVolumeMouseCapture);}
void ULDControlsPanel::HandleVolumeControllerCaptureBegin(){bVolumeControllerCapture=true;}
void ULDControlsPanel::HandleVolumeControllerCaptureEnd(){EndVolumeCapture(bVolumeControllerCapture);}
void ULDControlsPanel::EndVolumeCapture(bool& bCaptured)
{
    // A capture can report its end twice (touch release, lost capture); the preview plays once, after the last capture ends.
    if(!bCaptured){return;}bCaptured=false;
    if(!bVolumeMouseCapture&&!bVolumeControllerCapture){PreviewMasterVolume();}
}
void ULDControlsPanel::PreviewMasterVolume()
{if(auto* Instance=SettingsInstance.Get()){if(auto* AudioOutput=Instance->GetSubsystem<ULDAudioSubsystem>()){AudioOutput->Play(ELDAudioCue::UIClick);}}}

void ULDControlsPanel::ShowChild(EChildConfirmation Which)
{
    // Showing or dismissing a child confirmation never applies or discards a value; it only drops a pending "apply and return".
    Child=Which;bCloseAfterConfirm=false;
    if(!CloseRows){return;}
    auto Show=[Which](UWidget* Box,EChildConfirmation Kind){Box->SetVisibility(Which==Kind?ESlateVisibility::Visible:ESlateVisibility::Collapsed);};
    Show(ResetRows,EChildConfirmation::LocalReset);Show(RecoveryRows,EChildConfirmation::Recovery);Show(CloseRows,EChildConfirmation::CloseDraft);
}

void ULDControlsPanel::ShowRecoveryConfirmation()
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());
    if(!PC->Controls->NeedsRecovery())
    {ShowChild(EChildConfirmation::None);Feedback->SetText(FText::FromString(TEXT("没有需要替换的无法读取或不兼容的设置文件。写入失败请使用“重试本地保存”。")));return;}
    ShowChild(EChildConfirmation::Recovery);
}

void ULDControlsPanel::RefreshDisplayDraft()
{
    auto* Instance=SettingsInstance.Get();
    bDisplayAvailable=false;bDisplayDraftDirty=false;
    Resolutions.Reset();
    if(!Instance){DisplayFeedback->SetText(FText::FromString(TEXT("显示设置不可用：找不到本地设置的所有者。")));PopulateDisplay();return;}
    const auto Result=Instance->ReadDisplaySettings(DisplayDraft);
    bDisplayAvailable=Result.Status==ELDDisplaySettingsStatus::Loaded;
    DisplayFeedback->SetText(FText::FromString(Result.Message));
    FString Error;
    if(bDisplayAvailable&&!Instance->GetDisplayResolutions(Resolutions,Error))
    {DisplayFeedback->SetText(FText::FromString(Error));}
    PopulateDisplay();
}

void ULDControlsPanel::PopulateDisplay()
{
    if(!DisplayRows){return;}
    auto* Instance=SettingsInstance.Get();
    const bool Available=bDisplayAvailable&&Instance;
    const bool Trial=Available&&TrialId!=0&&Instance->GetDisplayTrialId()==TrialId;
    DisplayRows->SetVisibility(Available?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    TrialRows->SetVisibility(Trial?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    if(!Available){return;}
    const bool Editing=!Trial&&!Instance->IsDisplayRestorePending();
    const TCHAR* Mode=DisplayDraft.WindowMode==EWindowMode::Fullscreen?TEXT("全屏"):DisplayDraft.WindowMode==EWindowMode::WindowedFullscreen?TEXT("无边框"):TEXT("窗口");
    ControlsSetButton(WidgetTree,TEXT("WindowModeDraft"),FString::Printf(TEXT("窗口模式草稿：%s（点击切换）"),Mode),Editing);
    ControlsSetButton(WidgetTree,TEXT("ResolutionDraft"),FString::Printf(TEXT("分辨率草稿：%d × %d（下一档支持尺寸）"),DisplayDraft.Resolution.X,DisplayDraft.Resolution.Y),
        Editing&&!Resolutions.IsEmpty()&&DisplayDraft.WindowMode!=EWindowMode::WindowedFullscreen);
    ControlsSetButton(WidgetTree,TEXT("VSyncDraft"),FString(TEXT("垂直同步草稿："))+ControlsOnOff(DisplayDraft.bVSync),Editing);
    const int32 Preset=DisplayDraft.Quality.GetSingleQualityLevel();
    const TCHAR* Names[]={TEXT("低"),TEXT("中"),TEXT("高"),TEXT("极高"),TEXT("电影级")};
    ControlsSetButton(WidgetTree,TEXT("QualityDraft"),FString(TEXT("画质草稿："))+(Preset>=0&&Preset<5?Names[Preset]:TEXT("自定义"))+TEXT("（下一档预设）"),Editing);
    ControlsSetButton(WidgetTree,TEXT("ApplyDisplayDraft"),TEXT("应用显示草稿"),Editing&&bDisplayDraftDirty);
    ControlsSetButton(WidgetTree,TEXT("RetryDisplaySave"),TEXT("重试保存显示设置"),Editing,Instance->AreDisplaySettingsDirty());
    ControlsSetButton(WidgetTree,TEXT("RetryDisplayRestore"),TEXT("重试恢复原显示模式"),true,Instance->IsDisplayRestorePending());
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

void ULDControlsPanel::ConfirmResetDefaults()
{
    ShowChild(EChildConfirmation::None);
    auto* Instance=SettingsInstance.Get();
    if(!Instance){Feedback->SetText(FText::FromString(TEXT("本地设置的所有者不可用，没有恢复任何默认值。")));return;}
    // The display part needs the trial path; while another trial or restore runs, nothing is reset rather than half of it.
    if(TrialId||Instance->GetDisplayTrialId()||Instance->IsDisplayRestorePending())
    {Feedback->SetText(FText::FromString(TEXT("显示试用或恢复仍在进行：请先保留或恢复显示模式，再恢复默认。没有恢复任何设置。")));return;}
    auto* Draft=CastChecked<ALDPlayerController>(GetOwningPlayer())->Controls->CreateDraft();
    Draft->ResetLocalDefaults();
    Feedback->SetText(FText::FromString(TEXT("恢复按键、镜头、文字、小地图与音量默认值：")+Instance->ApplyLocalSettings(*Draft).Message));
    Populate();
    RestoreDisplayDefaults(*Instance);
}

void ULDControlsPanel::RestoreDisplayDefaults(ULDGameInstance& Instance)
{
    auto* LiveSettings=GEngine?GEngine->GetGameUserSettings():nullptr;
    FLDDisplaySettings Current,Defaults;FString Error;
    const auto Read=Instance.ReadDisplaySettings(Current);
    if(Read.Status!=ELDDisplaySettingsStatus::Loaded||!LiveSettings)
    {DisplayFeedback->SetText(FText::FromString(TEXT("显示与画质未恢复默认：")+Read.Message));return;}
    if(!FLDDisplaySettings::ProductDefaults(*LiveSettings,LiveSettings->GetDesktopResolution(),Defaults,Error))
    {DisplayFeedback->SetText(FText::FromString(Error));return;}
    // Availability and the supported list come from the live display; the confirmed reset replaces any unapplied draft.
    RefreshDisplayDraft();
    if(Defaults==Current)
    {DisplayFeedback->SetText(FText::FromString(TEXT("显示与画质已是默认值，没有开始显示试用。")));return;}
    // A resolution or window mode change starts the keep/revert trial; VSync and quality apply with that display apply.
    DisplayDraft=Defaults;bDisplayDraftDirty=true;
    ApplyDisplay();
}

void ULDControlsPanel::Open()
{
    // Reopening an open panel only refocuses it; it must not silently discard an unapplied display draft.
    if(bOpen){SetKeyboardFocus();return;}
    bOpen=true;Capture=NAME_None;
    SettingsInstance=GetGameInstance<ULDGameInstance>();
    SetVisibility(ESlateVisibility::Visible);
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());PC->SetMenuOpen(true);
    ShowChild(EChildConfirmation::None);
    if(auto* Instance=SettingsInstance.Get()){Feedback->SetText(FText::FromString(Instance->GetLocalSettingsMessage()));}
    SetKeyboardFocus();Populate();RefreshDisplayDraft();
}
void ULDControlsPanel::EndCapture()
{Capture=NAME_None;Feedback->SetText(FText::FromString(TEXT("按键未更改。")));}
bool ULDControlsPanel::Close()
{
    if(!bOpen){return true;}
    if(!Capture.IsNone()){EndCapture();return false;}
    if(TrialId){CancelDisplay(TrialId);return false;}
    if(Child!=EChildConfirmation::None){ShowChild(EChildConfirmation::None);return false;}
    if(bDisplayDraftDirty){ShowChild(EChildConfirmation::CloseDraft);return false;}
    ForceClose();return true;
}
bool ULDControlsPanel::HasChildConfirmation() const
{return bOpen&&(!Capture.IsNone()||TrialId!=0||Child!=EChildConfirmation::None);}
bool ULDControlsPanel::CloseChildConfirmation()
{
    if(!bOpen){return false;}
    if(!Capture.IsNone()){EndCapture();return true;}
    // Same as the visible "continue editing" / "keep unchanged" choices: nothing is applied or discarded.
    if(Child!=EChildConfirmation::None){ShowChild(EChildConfirmation::None);return true;}
    // A display trial prompt is not dismissible here: it must be kept, reverted or allowed to time out explicitly.
    return false;
}
void ULDControlsPanel::ForceClose()
{
    if(auto* Instance=SettingsInstance.Get()){Instance->CancelDisplayTrial(TrialId);Instance->FlushLocalSettings();}
    TrialId=0;Capture=NAME_None;bOpen=false;bDisplayDraftDirty=false;ShowChild(EChildConfirmation::None);
    // The flush above already settled any pending volume save; a capture still open on the hidden slider ends silently.
    bVolumeSavePending=false;bVolumeMouseCapture=false;bVolumeControllerCapture=false;
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
    {DisplayFeedback->SetText(FText::FromString(FString::Printf(TEXT("%s\n%d 秒后自动恢复，除非选择保留。"),*Result.Message,FMath::CeilToInt32(Instance->GetDisplayTrialRemaining()))));}
    else if(Result.Status!=SeenDisplayStatus)
    {DisplayFeedback->SetText(FText::FromString(Result.Message));PopulateDisplay();}
    SeenDisplayStatus=Result.Status;
    if(bVolumeSavePending&&!Instance->IsLocalSettingsFlushPending())
    {bVolumeSavePending=false;Feedback->SetText(FText::FromString(Instance->GetLocalSettingsMessage()));PopulatePreferences();}
}
FReply ULDControlsPanel::CaptureKey(FKey Key)
{
    if(Key==EKeys::Escape){EndCapture();return FReply::Handled();}
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
