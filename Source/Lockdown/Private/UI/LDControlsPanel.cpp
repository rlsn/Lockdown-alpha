#include "UI/LDControlsPanel.h"
#include "Audio/LDAudioSubsystem.h"
#include "Character/LDCameraSettings.h"
#include "UI/LDControlSettings.h"
#include "UI/LDHUDStyle.h"
#include "UI/LDWidgets.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameInstance.h"
#include "UI/LDUIRoot.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/Slider.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "CoreGlobals.h"
#include "GameFramework/GameUserSettings.h"
#include "Engine/Engine.h"

namespace
{
    using ECategory=ULDControlsPanel::ECategory;

    // Local presentation of the settings page only; no rule or value lives here, and the formal reskin belongs to #93.
    const FLinearColor ScrimColor(0.f,0.f,0.f,.55f);
    const FLinearColor HairlineColor(.42f,.55f,.62f,.18f);
    const FLinearColor ChosenFill(.24f,.17f,.035f);
    const FLinearColor ChosenHover(.30f,.22f,.05f);
    const FLinearColor ChosenEdge(1.f,.75f,.30f);
    const FLinearColor TabChosen(.05f,.10f,.13f,.85f);
    const FLinearColor FlatHover(.09f,.24f,.27f,.45f);
    const FLinearColor SwitchOnTrack(.62f,.42f,.08f);
    const FLinearColor SwitchOffTrack(.035f,.06f,.08f);
    const FLinearColor SwitchOffEdge(.28f,.38f,.44f);
    const FLinearColor SwitchOffKnob(.50f,.58f,.64f);
    const FLinearColor BarFill(.035f,.06f,.085f);
    const FLinearColor BarPendingFill(.12f,.09f,.02f);
    const FLinearColor CardFill(.09f,.07f,.02f);
    constexpr int32 TitleSize=26,TabSize=19,LabelSize=18,NoteSize=15,ValueSize=17,SectionSize=16,ActionSize=16;
    // Slate units at the 1080p reference; the control column and value widths leave room for 125% text.
    constexpr float PanelWidth=1280,ControlColumn=520,ValueWidth=170,KeyWidth=210;
    // One press of the camera pan speed arrows; the legal domain itself belongs to ULDControlSettings.
    constexpr float PanStepCmPerSec=300;
    constexpr int32 QualityLevels=5;
    const TCHAR* const QualityNames[QualityLevels]={TEXT("低"),TEXT("中"),TEXT("高"),TEXT("极高"),TEXT("电影级")};
    constexpr ELDMinimapOrientation Orientations[]={ELDMinimapOrientation::Camera,ELDMinimapOrientation::Facing,ELDMinimapOrientation::North};
    const TCHAR* const VSyncHint=TEXT("与显示器刷新同步，减少画面撕裂。");
    const TCHAR* const QualityHint=TEXT("整体画质预设；越高越精细，对显卡要求也越高。");
    // The unapplied-draft prompt names what Back does next, so it never just toggles the prompt away and back.
    const TCHAR* const CloseDraftText=TEXT("画面更改（垂直同步、画质）尚未应用；其他页已生效的更改会保留。再按“返回”或 Esc 将放弃画面更改并返回。");

    // Tab names are stable automation handles; titles are player-facing copy only.
    struct FCategoryInfo {const TCHAR* Title;const TCHAR* Tab;};
    const FCategoryInfo Categories[ULDControlsPanel::CategoryCount]={
        {TEXT("画面"),TEXT("TabDisplay")},{TEXT("音频"),TEXT("TabAudio")},{TEXT("操作"),TEXT("TabControls")},
        {TEXT("镜头"),TEXT("TabCamera")},{TEXT("界面"),TEXT("TabInterface")}};
    const TCHAR* CategoryTitle(ECategory Category){return Categories[int32(Category)].Title;}

    FName Part(FName Base,const TCHAR* Suffix){return FName(*(Base.ToString()+Suffix));}
    const TCHAR* OnOff(bool Value){return Value?TEXT("开"):TEXT("关");}
    FString QualityName(const Scalability::FQualityLevels& Quality)
    {const int32 Level=Quality.GetSingleQualityLevel();return Level>=0&&Level<QualityLevels?QualityNames[Level]:TEXT("自定义");}
    FName OrientationButton(ELDMinimapOrientation Value)
    {return Value==ELDMinimapOrientation::Facing?FName(TEXT("MinimapFacing")):Value==ELDMinimapOrientation::North?FName(TEXT("MinimapNorth")):FName(TEXT("MinimapCamera"));}

    FSlateRoundedBoxBrush Face(const FLinearColor& Fill,const FLinearColor& Edge,float Radius=6.f,float EdgeWidth=1.f)
    {FSlateRoundedBoxBrush Brush(Fill,Radius,Edge,EdgeWidth);Brush.OutlineSettings.bUseBrushTransparency=true;return Brush;}
    UTextBlock* Text(UWidgetTree* Tree,const FString& Value,int32 Size,const FLinearColor& Color,FName Name=NAME_None)
    {auto* Label=LDWidgets::Text(Tree,Value,Size,Name);Label->SetColorAndOpacity(Color);return Label;}
    UButton* Find(UWidgetTree* Tree,FName Name){return CastChecked<UButton>(Tree->FindWidget(Name));}
    UTextBlock* Caption(UButton* Button){return CastChecked<UTextBlock>(Button->GetContent());}
    void Show(UWidget* Widget,bool bShown,ESlateVisibility Visible=ESlateVisibility::Visible)
    {Widget->SetVisibility(bShown?Visible:ESlateVisibility::Collapsed);}
    void SetNote(UTextBlock* Note,const FString& Value,const FLinearColor& Color)
    {Note->SetText(FText::FromString(Value));Note->SetColorAndOpacity(Color);Show(Note,!Value.IsEmpty(),ESlateVisibility::HitTestInvisible);}

    ULDCommandButton* MakeButton(UWidgetTree* Tree,const FString& Label,TFunction<void()> Action,FName Name,int32 Size)
    {
        auto* Result=LDWidgets::Button(Tree,Label,MoveTemp(Action),false,Name);auto* Words=Caption(Result);
        auto Font=Words->GetFont();Font.Size=Size;Words->SetFont(Font);Words->SetJustification(ETextJustify::Center);
        CastChecked<UButtonSlot>(Words->Slot)->SetHorizontalAlignment(HAlign_Center);
        return Result;
    }
    // Switches and tabs drop the button face at rest; hover and press still show where the pointer acts.
    void Flatten(UButton* Button,const FMargin& Spacing)
    {
        auto Style=Button->GetStyle();const auto None=Face(FLinearColor::Transparent,FLinearColor::Transparent);
        Style.Normal=None;Style.Disabled=None;Style.Hovered=Face(FlatHover,FLinearColor(.30f,.62f,.62f,.5f));
        Style.Pressed=Face(FLinearColor(.12f,.32f,.32f,.6f),FLinearColor(.40f,.75f,.72f,.7f));
        Style.NormalPadding=Spacing;Style.PressedPadding=Spacing;Button->SetStyle(Style);
    }
    // A chosen option, the capturing key and an available Apply take an amber face with a heavier edge, so the state reads
    // from the outline as well as the colour; the caption names the choice itself.
    void MarkChosen(UButton* Button,bool bChosen,const FButtonStyle& Rest)
    {
        FButtonStyle Style=Rest;
        if(bChosen){Style.Normal=Face(ChosenFill,ChosenEdge,6.f,2.f);Style.Hovered=Face(ChosenHover,ChosenEdge,6.f,2.f);Style.Pressed=Style.Hovered;}
        Button->SetStyle(Style);
    }
    // A flat strip: hairlines between rows and the underline of the shown tab.
    USizeBox* Strip(UWidgetTree* Tree,float Height,const FLinearColor& Color,FName Name=NAME_None)
    {
        auto* Line=Tree->ConstructWidget<UBorder>(UBorder::StaticClass(),Name);Line->SetBrush(FSlateColorBrush(FLinearColor::White));
        Line->SetBrushColor(Color);Line->SetPadding(FMargin(0));Line->SetVisibility(ESlateVisibility::HitTestInvisible);
        auto* Box=Tree->ConstructWidget<USizeBox>();Box->SetHeightOverride(Height);Box->SetContent(Line);return Box;
    }

    // A switch: track and knob plus the state word, so on and off never rely on colour alone.
    ULDCommandButton* Switch(UWidgetTree* Tree,FName Name,TFunction<void()> Action)
    {
        auto* Result=LDWidgets::Button(Tree,TEXT(""),MoveTemp(Action),false,Name);Flatten(Result,FMargin(6,6,12,6));
        auto* Row=Tree->ConstructWidget<UHorizontalBox>();Row->SetVisibility(ESlateVisibility::HitTestInvisible);
        auto* Track=Tree->ConstructWidget<UBorder>(UBorder::StaticClass(),Part(Name,TEXT("Track")));
        Track->SetPadding(FMargin(3));Track->SetVerticalAlignment(VAlign_Center);
        auto* Knob=Tree->ConstructWidget<UBorder>(UBorder::StaticClass(),Part(Name,TEXT("Knob")));
        auto* KnobBox=Tree->ConstructWidget<USizeBox>();KnobBox->SetWidthOverride(20);KnobBox->SetHeightOverride(20);KnobBox->SetContent(Knob);
        Track->SetContent(KnobBox);
        auto* TrackBox=Tree->ConstructWidget<USizeBox>();TrackBox->SetWidthOverride(50);TrackBox->SetHeightOverride(26);TrackBox->SetContent(Track);
        Row->AddChildToHorizontalBox(TrackBox)->SetVerticalAlignment(VAlign_Center);
        auto* State=Text(Tree,TEXT(""),ValueSize,LDHUDStyle::TextPrimary,Part(Name,TEXT("State")));State->SetAutoWrapText(false);
        auto* StateSlot=Row->AddChildToHorizontalBox(State);StateSlot->SetPadding(FMargin(12,0,0,0));StateSlot->SetVerticalAlignment(VAlign_Center);
        Result->SetContent(Row);
        return Result;
    }
    void SetSwitch(UWidgetTree* Tree,FName Name,bool bOn)
    {
        auto* Track=CastChecked<UBorder>(Tree->FindWidget(Part(Name,TEXT("Track"))));
        Track->SetBrush(Face(bOn?SwitchOnTrack:SwitchOffTrack,bOn?ChosenEdge:SwitchOffEdge,13.f));
        Track->SetHorizontalAlignment(bOn?HAlign_Right:HAlign_Left);
        CastChecked<UBorder>(Tree->FindWidget(Part(Name,TEXT("Knob"))))->SetBrush(Face(bOn?FLinearColor::White:SwitchOffKnob,FLinearColor::Transparent,10.f));
        auto* State=CastChecked<UTextBlock>(Tree->FindWidget(Part(Name,TEXT("State"))));
        State->SetText(FText::FromString(OnOff(bOn)));State->SetColorAndOpacity(bOn?LDHUDStyle::TextPrimary:LDHUDStyle::TextSecondary);
    }
    // An ordered value between two arrows; an arrow at the end of the range is disabled instead of wrapping around.
    UHorizontalBox* Stepper(UWidgetTree* Tree,FName Lower,FName Higher,FName Value,TFunction<void()> OnLower,TFunction<void()> OnHigher)
    {
        auto* Row=Tree->ConstructWidget<UHorizontalBox>();
        auto Arrow=[Tree,Row](FName Name,const TCHAR* Glyph,TFunction<void()> Action)
        {
            auto* Box=Tree->ConstructWidget<USizeBox>();Box->SetWidthOverride(48);Box->SetContent(MakeButton(Tree,Glyph,MoveTemp(Action),Name,LabelSize));
            Row->AddChildToHorizontalBox(Box)->SetVerticalAlignment(VAlign_Center);
        };
        Arrow(Lower,TEXT("‹"),MoveTemp(OnLower));
        auto* Shown=Text(Tree,TEXT(""),ValueSize,LDHUDStyle::TextPrimary,Value);Shown->SetAutoWrapText(false);Shown->SetJustification(ETextJustify::Center);
        auto* ValueBox=Tree->ConstructWidget<USizeBox>();ValueBox->SetWidthOverride(ValueWidth);
        CastChecked<USizeBoxSlot>(ValueBox->SetContent(Shown))->SetVerticalAlignment(VAlign_Center);
        Row->AddChildToHorizontalBox(ValueBox)->SetVerticalAlignment(VAlign_Center);
        Arrow(Higher,TEXT("›"),MoveTemp(OnHigher));
        return Row;
    }
    void SetStepper(UWidgetTree* Tree,FName Lower,FName Higher,FName Value,const FString& Shown,bool bCanLower,bool bCanRaise)
    {
        CastChecked<UTextBlock>(Tree->FindWidget(Value))->SetText(FText::FromString(Shown));
        Find(Tree,Lower)->SetIsEnabled(bCanLower);Find(Tree,Higher)->SetIsEnabled(bCanRaise);
    }
    void AddSegment(UWidgetTree* Tree,UHorizontalBox* Row,FName Name,const FString& Label,TFunction<void()> Action)
    {Row->AddChildToHorizontalBox(MakeButton(Tree,Label,MoveTemp(Action),Name,ValueSize))->SetPadding(FMargin(0,0,6,0));}

    // One setting: its name and a one-line note on the left, its control in a fixed right column so every control starts on
    // the same edge. Returns the note, which a row may later rewrite with its current state.
    UTextBlock* AddRow(UWidgetTree* Tree,UVerticalBox* Page,const FString& Name,const FString& Note,UWidget* Control,bool bCompact=false)
    {
        auto* Row=Tree->ConstructWidget<UHorizontalBox>();
        auto* Words=Tree->ConstructWidget<UVerticalBox>();
        Words->AddChildToVerticalBox(Text(Tree,Name,LabelSize,LDHUDStyle::TextPrimary));
        auto* NoteText=Text(Tree,Note,NoteSize,LDHUDStyle::TextSecondary);
        Words->AddChildToVerticalBox(NoteText)->SetPadding(FMargin(0,4,0,0));
        Show(NoteText,!Note.IsEmpty(),ESlateVisibility::HitTestInvisible);
        auto* WordsSlot=Row->AddChildToHorizontalBox(Words);WordsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        WordsSlot->SetVerticalAlignment(VAlign_Center);WordsSlot->SetPadding(FMargin(0,0,24,0));
        auto* Column=Tree->ConstructWidget<USizeBox>();Column->SetWidthOverride(ControlColumn);
        auto* ControlSlot=CastChecked<USizeBoxSlot>(Column->SetContent(Control));
        ControlSlot->SetHorizontalAlignment(HAlign_Left);ControlSlot->SetVerticalAlignment(VAlign_Center);
        Row->AddChildToHorizontalBox(Column)->SetVerticalAlignment(VAlign_Center);
        Page->AddChildToVerticalBox(Row)->SetPadding(bCompact?FMargin(6,7):FMargin(6,14));
        Page->AddChildToVerticalBox(Strip(Tree,1,HairlineColor));
        return NoteText;
    }
    void AddSection(UWidgetTree* Tree,UVerticalBox* Page,const TCHAR* Title)
    {Page->AddChildToVerticalBox(Text(Tree,Title,SectionSize,LDHUDStyle::TextSecondary))->SetPadding(FMargin(6,24,6,4));}

    // A confirmation: its question, then its answers in one row.
    UVerticalBox* ChildRows(UWidgetTree* Tree,const FString& Prompt,TObjectPtr<UTextBlock>* OutPrompt=nullptr)
    {
        auto* Box=Tree->ConstructWidget<UVerticalBox>();Box->SetVisibility(ESlateVisibility::Collapsed);
        auto* Words=Text(Tree,Prompt,LabelSize-1,LDHUDStyle::TextPrimary);Box->AddChildToVerticalBox(Words);
        Box->AddChildToVerticalBox(Tree->ConstructWidget<UHorizontalBox>())->SetPadding(FMargin(0,12,0,0));
        if(OutPrompt){*OutPrompt=Words;}
        return Box;
    }
    void AddChoice(UWidgetTree* Tree,UVerticalBox* Rows,const TCHAR* Label,FName Name,TFunction<void()> Action)
    {CastChecked<UHorizontalBox>(Rows->GetChildAt(1))->AddChildToHorizontalBox(MakeButton(Tree,Label,MoveTemp(Action),Name,ActionSize))->SetPadding(FMargin(0,0,10,0));}
    // Keeps a whole row (its control and its result line) in view, also at the bottom edge of a page.
    UWidget* RowOf(UWidget* Note){return Note&&Note->GetParent()?Note->GetParent()->GetParent():nullptr;}
    void RevealRow(UWidget* Row)
    {
        for(UWidget* Parent=Row?Row->GetParent():nullptr;Parent;Parent=Parent->GetParent())
        {if(auto* List=Cast<UScrollBox>(Parent)){List->ScrollWidgetIntoView(Row,false,EDescendantScrollDestination::IntoView,8);return;}}
    }
    int32 BindingIndex(FName Action)
    {return ULDControlSettings::Definitions().IndexOfByPredicate([Action](const FLDControlBinding& B){return B.Action==Action;});}
}

TSharedRef<SWidget> ULDControlsPanel::RebuildWidget()
{
    SetIsFocusable(true);
    if(!Feedback)
    {
        // Every interactive control is built once under a stable name (automation and Back routing find it by name, never by
        // copy); later refreshes only change text, faces, enablement and visibility.
        UWidgetTree* Tree=WidgetTree;
        auto* Canvas=Tree->ConstructWidget<UCanvasPanel>();Tree->RootWidget=Canvas;
        // The world keeps running under Settings (UI_AND_SETTINGS 4); the scrim only separates the page from it.
        auto* Scrim=Tree->ConstructWidget<UBorder>();Scrim->SetBrush(FSlateColorBrush(FLinearColor::White));Scrim->SetBrushColor(ScrimColor);
        Scrim->SetVisibility(ESlateVisibility::HitTestInvisible);
        auto* ScrimSlot=Canvas->AddChildToCanvas(Scrim);ScrimSlot->SetAnchors(FAnchors(0,0,1,1));ScrimSlot->SetOffsets(FMargin(0));
        auto* Main=Tree->ConstructWidget<UVerticalBox>();
        // A fixed reading width centred on any desktop aspect; the height follows the window and each page scrolls.
        auto* Frame=Canvas->AddChildToCanvas(LDWidgets::Panel(Tree,Main,FMargin(32,24,32,20)));
        Frame->SetAnchors(FAnchors(.5f,.05f,.5f,.95f));Frame->SetAlignment(FVector2D(.5f,0));Frame->SetOffsets(FMargin(0,0,PanelWidth,0));

        auto* Header=Tree->ConstructWidget<UHorizontalBox>();Main->AddChildToVerticalBox(Header);
        auto* Title=Text(Tree,TEXT("设置"),TitleSize,LDHUDStyle::TextPrimary);Title->SetAutoWrapText(false);
        Header->AddChildToHorizontalBox(Title)->SetVerticalAlignment(VAlign_Bottom);
        Header->AddChildToHorizontalBox(Tree->ConstructWidget<USpacer>())->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        MatchNote=Text(Tree,TEXT("对局不会暂停，角色仍在场上"),NoteSize,LDHUDStyle::TextWarning);MatchNote->SetAutoWrapText(false);
        Header->AddChildToHorizontalBox(MatchNote)->SetVerticalAlignment(VAlign_Bottom);

        auto* Tabs=Tree->ConstructWidget<UHorizontalBox>();Main->AddChildToVerticalBox(Tabs)->SetPadding(FMargin(0,18,0,0));
        for(int32 Index=0;Index<CategoryCount;++Index)
        {
            const ECategory Target=static_cast<ECategory>(Index);
            auto* Tab=MakeButton(Tree,Categories[Index].Title,[this,Target](){ShowCategory(Target);},Categories[Index].Tab,TabSize);
            Flatten(Tab,FMargin(24,9));
            auto* Column=Tree->ConstructWidget<UVerticalBox>();Column->AddChildToVerticalBox(Tab);
            Column->AddChildToVerticalBox(Strip(Tree,3,FLinearColor::Transparent,Part(Categories[Index].Tab,TEXT("Line"))));
            Tabs->AddChildToHorizontalBox(Column)->SetPadding(FMargin(0,0,4,0));
        }
        Main->AddChildToVerticalBox(Strip(Tree,1,HairlineColor));

        auto* Body=Tree->ConstructWidget<UVerticalBox>();Main->AddChildToVerticalBox(Body)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        auto AddPage=[this,Body](UWidget* Page){Pages.Add(Page);Body->AddChildToVerticalBox(Page)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));};
        auto NewPage=[Tree](UScrollBox*& Scroll)
        {
            Scroll=Tree->ConstructWidget<UScrollBox>();auto* Content=Tree->ConstructWidget<UVerticalBox>();
            CastChecked<UScrollBoxSlot>(Scroll->AddChild(Content))->SetPadding(FMargin(0,4,18,16));return Content;
        };

        // Display: the draft rows, and the apply bar that stays in view below the page's own scroll.
        {
            UScrollBox* Scroll=nullptr;UVerticalBox* Content=NewPage(Scroll);
            auto* Page=Tree->ConstructWidget<UVerticalBox>();Page->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            Content->AddChildToVerticalBox(Text(Tree,TEXT("游戏始终以无边框全屏运行。本页更改需要点击“应用”后才会生效。"),NoteSize,LDHUDStyle::TextSecondary))
                ->SetPadding(FMargin(6,16,6,4));
            DisplayUnavailable=Text(Tree,TEXT(""),LabelSize,LDHUDStyle::TextWarning);DisplayUnavailable->SetVisibility(ESlateVisibility::Collapsed);
            Content->AddChildToVerticalBox(DisplayUnavailable)->SetPadding(FMargin(6,12));
            DisplayRows=Tree->ConstructWidget<UVerticalBox>();Content->AddChildToVerticalBox(DisplayRows);
            // Every draft edit is a new request, so an apply the instance refused may be tried again.
            VSyncNote=AddRow(Tree,DisplayRows,TEXT("垂直同步"),VSyncHint,
                Switch(Tree,TEXT("VSyncDraft"),[this](){DisplayDraft.bVSync=!DisplayDraft.bVSync;EditDisplayDraft();}));
            QualityNote=AddRow(Tree,DisplayRows,TEXT("画质"),QualityHint,Stepper(Tree,TEXT("QualityDraftLower"),TEXT("QualityDraftHigher"),
                TEXT("QualityDraftValue"),[this](){StepQuality(-1);},[this](){StepQuality(1);}));
            DisplayBar=Tree->ConstructWidget<UBorder>();DisplayBar->SetPadding(FMargin(18,10,10,10));
            auto* Bar=Tree->ConstructWidget<UHorizontalBox>();DisplayBar->SetContent(Bar);
            DisplayFeedback=Text(Tree,TEXT(""),NoteSize+1,LDHUDStyle::TextSecondary);
            auto* FeedbackSlot=Bar->AddChildToHorizontalBox(DisplayFeedback);FeedbackSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            FeedbackSlot->SetVerticalAlignment(VAlign_Center);FeedbackSlot->SetPadding(FMargin(0,0,16,0));
            auto BarButton=[Tree,Bar](const TCHAR* Label,FName Name,TFunction<void()> Action)
            {auto* Placed=Bar->AddChildToHorizontalBox(MakeButton(Tree,Label,MoveTemp(Action),Name,ActionSize));Placed->SetVerticalAlignment(VAlign_Center);Placed->SetPadding(FMargin(8,0,0,0));};
            BarButton(TEXT("重试保存"),TEXT("RetryDisplaySave"),[this]()
            {if(auto* Owner=SettingsInstance.Get()){SetDisplayMessage(Owner->RetryDisplaySave());SeenDisplayStatus=Owner->GetDisplaySettingsResult().Status;PopulateDisplay();}});
            BarButton(TEXT("撤销更改"),TEXT("RevertDisplayDraft"),[this]()
            {
                // A save that still failed keeps its own message beside its retry; otherwise the bar confirms the revert.
                RefreshDisplayDraft();
                if(bDisplayAvailable&&DisplayMessage.IsEmpty()){DisplayMessage=TEXT("已撤销未应用的画面更改。");bDisplayMessageProblem=false;PopulateDisplay();}
            });
            BarButton(TEXT("应用"),TEXT("ApplyDisplayDraft"),[this](){ApplyDisplay();});
            Page->AddChildToVerticalBox(DisplayBar)->SetPadding(FMargin(0,8,0,0));
            AddPage(Page);
        }
        // Audio
        {
            UScrollBox* Scroll=nullptr;UVerticalBox* Content=NewPage(Scroll);
            VolumeSlider=Tree->ConstructWidget<USlider>(USlider::StaticClass(),TEXT("MasterVolume"));
            VolumeSlider->SetMinValue(0);VolumeSlider->SetMaxValue(1);VolumeSlider->SetStepSize(.05f);VolumeSlider->IsFocusable=true;
            VolumeSlider->SetSliderBarColor(FLinearColor(.28f,.38f,.44f));VolumeSlider->SetSliderHandleColor(LDHUDStyle::TextAccent);
            FSliderStyle SliderStyle=VolumeSlider->GetWidgetStyle();SliderStyle.BarThickness=4;VolumeSlider->SetWidgetStyle(SliderStyle);
            VolumeSlider->OnValueChanged.AddDynamic(this,&ULDControlsPanel::HandleMasterVolumeChanged);
            VolumeSlider->OnMouseCaptureBegin.AddDynamic(this,&ULDControlsPanel::HandleVolumeMouseCaptureBegin);VolumeSlider->OnMouseCaptureEnd.AddDynamic(this,&ULDControlsPanel::HandleVolumeMouseCaptureEnd);
            VolumeSlider->OnControllerCaptureBegin.AddDynamic(this,&ULDControlsPanel::HandleVolumeControllerCaptureBegin);VolumeSlider->OnControllerCaptureEnd.AddDynamic(this,&ULDControlsPanel::HandleVolumeControllerCaptureEnd);
            auto* Volume=Tree->ConstructWidget<UHorizontalBox>();
            auto* SliderBox=Tree->ConstructWidget<USizeBox>();SliderBox->SetWidthOverride(320);SliderBox->SetHeightOverride(32);SliderBox->SetContent(VolumeSlider);
            Volume->AddChildToHorizontalBox(SliderBox)->SetVerticalAlignment(VAlign_Center);
            VolumeValue=Text(Tree,TEXT(""),ValueSize,LDHUDStyle::TextPrimary);VolumeValue->SetAutoWrapText(false);
            auto* ValueSlot=Volume->AddChildToHorizontalBox(VolumeValue);ValueSlot->SetPadding(FMargin(16,0,0,0));ValueSlot->SetVerticalAlignment(VAlign_Center);
            AddRow(Tree,Content,TEXT("主音量"),TEXT("游戏全部声音的音量；松开滑条或用方向键调节后试听一次。"),Volume);
            AddPage(Scroll);
        }
        // Controls: quick cast, then one row per registered action with its key in the right column.
        {
            UScrollBox* Scroll=nullptr;UVerticalBox* Content=NewPage(Scroll);
            AddSection(Tree,Content,TEXT("施法"));
            // Quick cast (off by default): a skill key casts at the pointed unit or toward the pointer at once; Combat falls back
            // to arming when no unit is pointed.
            AddRow(Tree,Content,TEXT("快速施法"),TEXT("按技能键立即对指针下的单位或指针方向施放；未指向单位时照常瞄准。"),
                Switch(Tree,TEXT("ToggleQuickCast"),[this](){ChangePreference([](auto& S){S.bQuickCast=!S.bQuickCast;});}));
            AddSection(Tree,Content,TEXT("按键"));
            Content->AddChildToVerticalBox(Text(Tree,TEXT("点击右侧按键框，再按下新的键盘按键或鼠标按键即可更改；Esc 取消。"),NoteSize,LDHUDStyle::TextSecondary))
                ->SetPadding(FMargin(6,0,6,6));
            for(const FLDControlBinding& Binding:ULDControlSettings::Definitions())
            {
                const FName Id=Binding.Action;
                auto* Key=MakeButton(Tree,TEXT(""),[this,Id](){BeginCapture(Id);},Part(TEXT("Bind"),*Id.ToString()),ValueSize);
                auto* KeyBox=Tree->ConstructWidget<USizeBox>();KeyBox->SetMinDesiredWidth(KeyWidth);KeyBox->SetContent(Key);
                BindingKeys.Add(Key);
                BindingNotes.Add(AddRow(Tree,Content,Binding.Label,TEXT(""),KeyBox,true));
            }
            AddPage(Scroll);
        }
        // Camera
        {
            UScrollBox* Scroll=nullptr;UVerticalBox* Content=NewPage(Scroll);
            auto* Distance=Tree->ConstructWidget<UHorizontalBox>();
            ZoomValue=Text(Tree,TEXT(""),ValueSize,LDHUDStyle::TextPrimary);ZoomValue->SetAutoWrapText(false);
            auto* ZoomBox=Tree->ConstructWidget<USizeBox>();ZoomBox->SetWidthOverride(ValueWidth);
            CastChecked<USizeBoxSlot>(ZoomBox->SetContent(ZoomValue))->SetVerticalAlignment(VAlign_Center);
            Distance->AddChildToHorizontalBox(ZoomBox)->SetVerticalAlignment(VAlign_Center);
            Distance->AddChildToHorizontalBox(MakeButton(Tree,TEXT("采用推荐镜头"),[this](){ChangePreference([](auto& S){S.UseRecommendedCameraZoom();});},
                TEXT("UseRecommendedCameraZoom"),ActionSize))->SetVerticalAlignment(VAlign_Center);
            AddRow(Tree,Content,TEXT("镜头距离"),FString::Printf(TEXT("推荐 %.0f 厘米，只修改距离；对局中用鼠标滚轮随时缩放。"),LDCamera::DefaultArmLength),Distance);
            AddRow(Tree,Content,TEXT("边缘平移"),TEXT("鼠标移到屏幕边缘时平移镜头。"),
                Switch(Tree,TEXT("ToggleEdgePan"),[this](){ChangePreference([](auto& S){S.bEdgePan=!S.bEdgePan;});}));
            FollowNote=AddRow(Tree,Content,TEXT("开局镜头跟随"),TEXT(""),
                Switch(Tree,TEXT("ToggleStartFollow"),[this](){ChangePreference([](auto& S){S.bStartFollowing=!S.bStartFollowing;});}));
            AddRow(Tree,Content,TEXT("镜头平移速度"),TEXT("键盘与边缘平移时镜头移动的速度，只影响镜头。"),Stepper(Tree,TEXT("SlowerCameraPan"),TEXT("FasterCameraPan"),TEXT("CameraPanValue"),
                [this](){ChangePreference([](auto& S){S.CameraPanCmPerSec=FMath::Max(S.CameraPanCmPerSec-PanStepCmPerSec,ULDControlSettings::MinCameraPanCmPerSec);});},
                [this](){ChangePreference([](auto& S){S.CameraPanCmPerSec=FMath::Min(S.CameraPanCmPerSec+PanStepCmPerSec,ULDControlSettings::MaxCameraPanCmPerSec);});}));
            AddPage(Scroll);
        }
        // Interface: text size and minimap, then the two diagnostic overlays.
        {
            UScrollBox* Scroll=nullptr;UVerticalBox* Content=NewPage(Scroll);
            auto* Sizes=Tree->ConstructWidget<UHorizontalBox>();
            AddSegment(Tree,Sizes,TEXT("TextScale100"),TEXT("100%"),[this](){ChangePreference([](auto& S){S.TextScale=1;});});
            AddSegment(Tree,Sizes,TEXT("TextScale125"),TEXT("125%"),[this](){ChangePreference([](auto& S){S.TextScale=1.25f;});});
            AddRow(Tree,Content,TEXT("文字大小"),TEXT("设置、菜单与 HUD 的文字大小。"),Sizes);
            auto* Orientation=Tree->ConstructWidget<UHorizontalBox>();
            for(const ELDMinimapOrientation Option:Orientations)
            {
                AddSegment(Tree,Orientation,OrientationButton(Option),ULDControlSettings::MinimapOrientationLabel(Option),
                    [this,Option](){ChangePreference([Option](auto& S){S.MinimapOrientation=Option;});});
            }
            AddRow(Tree,Content,TEXT("小地图朝向"),TEXT("小地图的上方朝向；只旋转显示，不改变能看到的内容。"),Orientation);
            AddSection(Tree,Content,TEXT("诊断"));
            AddRow(Tree,Content,TEXT("网络诊断"),TEXT("在右上角显示延迟、抖动、丢包与移动校正。"),
                Switch(Tree,TEXT("ToggleNetworkDiagnostics"),[this](){ChangePreference([](auto& S){S.bShowNetworkDiagnostics=!S.bShowNetworkDiagnostics;});}));
            AddRow(Tree,Content,TEXT("FPS 显示"),TEXT("在右上角显示帧率与帧耗时。"),
                Switch(Tree,TEXT("ToggleFPS"),[this](){ChangePreference([](auto& S){S.bShowFPS=!S.bShowFPS;});}));
            AddPage(Scroll);
        }

        // Confirmations open right above the footer buttons that ask for them.
        auto* Card=Tree->ConstructWidget<UBorder>();Card->SetBrush(Face(CardFill,ChosenEdge));Card->SetPadding(FMargin(20,14));
        Card->SetVisibility(ESlateVisibility::Collapsed);ConfirmCard=Card;
        auto* CardRows=Tree->ConstructWidget<UVerticalBox>();Card->SetContent(CardRows);
        ResetRows=ChildRows(Tree,TEXT(""),&ResetPrompt);CardRows->AddChildToVerticalBox(ResetRows);
        AddChoice(Tree,ResetRows,TEXT("恢复默认"),TEXT("ConfirmLocalReset"),[this](){ConfirmResetDefaults();});
        AddChoice(Tree,ResetRows,TEXT("取消"),TEXT("KeepLocalPreferences"),[this](){ShowChild(EChildConfirmation::None);});
        RecoveryRows=ChildRows(Tree,TEXT("用当前生效的名称、按键、镜头、文字、小地图、诊断与音量偏好替换无法读取或不兼容的设置文件？确认前旧文件保持不变。"));
        CardRows->AddChildToVerticalBox(RecoveryRows);
        AddChoice(Tree,RecoveryRows,TEXT("替换设置文件"),TEXT("ConfirmRecovery"),[this]()
        {
            ShowChild(EChildConfirmation::None);
            if(auto* Instance=SettingsInstance.Get()){const auto Result=Instance->ConfirmLocalSettingsRecovery();SetFeedback(Result.Message,!Result.IsSaved());}
            Populate();
        });
        AddChoice(Tree,RecoveryRows,TEXT("保持文件不变"),TEXT("KeepSettingsFile"),[this](){ShowChild(EChildConfirmation::None);});
        CloseRows=ChildRows(Tree,CloseDraftText,&CloseDraftPrompt);CardRows->AddChildToVerticalBox(CloseRows);
        AddChoice(Tree,CloseRows,TEXT("应用并返回"),TEXT("ApplyDisplayAndReturn"),[this](){ApplyDisplayAndReturn();});
        AddChoice(Tree,CloseRows,TEXT("放弃更改并返回"),TEXT("DiscardDisplayDraft"),[this](){ForceClose();});
        AddChoice(Tree,CloseRows,TEXT("继续编辑"),TEXT("ContinueEditing"),[this](){ShowChild(EChildConfirmation::None);});
        Main->AddChildToVerticalBox(Card)->SetPadding(FMargin(0,12,0,0));

        Main->AddChildToVerticalBox(Strip(Tree,1,HairlineColor))->SetPadding(FMargin(0,12,0,0));
        auto* Footer=Tree->ConstructWidget<UHorizontalBox>();Main->AddChildToVerticalBox(Footer)->SetPadding(FMargin(0,14,0,0));
        auto FooterButton=[Tree,Footer](const TCHAR* Label,FName Name,TFunction<void()> Action,const FMargin& Spacing)
        {auto* Placed=Footer->AddChildToHorizontalBox(MakeButton(Tree,Label,MoveTemp(Action),Name,ActionSize));Placed->SetVerticalAlignment(VAlign_Center);Placed->SetPadding(Spacing);};
        FooterButton(TEXT("恢复本页默认"),TEXT("ResetCategoryDefaults"),[this](){AskReset();},FMargin(0));
        Feedback=Text(Tree,TEXT(""),NoteSize,LDHUDStyle::TextSecondary);
        auto* FeedbackSlot=Footer->AddChildToHorizontalBox(Feedback);FeedbackSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        FeedbackSlot->SetVerticalAlignment(VAlign_Center);FeedbackSlot->SetPadding(FMargin(18,0));
        FooterButton(TEXT("立即保存"),TEXT("RetryLocalSave"),[this]()
        {
            if(auto* Instance=SettingsInstance.Get()){const auto Result=Instance->FlushLocalSettings(true);SetFeedback(Result.Message,!Result.IsSaved());}
            Populate();
        },FMargin(0,0,8,0));
        FooterButton(TEXT("恢复设置文件"),TEXT("RecoverLocalSettings"),[this](){ShowRecoveryConfirmation();},FMargin(0,0,8,0));
        FooterButton(TEXT("返回（Esc）"),TEXT("Close"),[this](){Close();},FMargin(0));
        RestStyle=Find(Tree,TEXT("Close"))->GetStyle();
        ShowCategory(Category);
    }
    Populate();return Super::RebuildWidget();
}

void ULDControlsPanel::ShowCategory(ECategory Next)
{
    if(!Feedback){return;}
    // Switching pages answers an open confirmation with its "keep" choice and ends a key capture unchanged; edits, the
    // display draft and each page's scroll position stay.
    if(!Capture.IsNone()){EndCapture();}
    if(Child!=EChildConfirmation::None){ShowChild(EChildConfirmation::None);}
    Category=Next;
    for(int32 Index=0;Index<CategoryCount;++Index)
    {
        const bool bShown=Index==int32(Next);
        Show(Pages[Index],bShown);
        auto* Tab=Find(WidgetTree,Categories[Index].Tab);
        auto Style=Tab->GetStyle();Style.Normal=bShown?Face(TabChosen,FLinearColor::Transparent):Face(FLinearColor::Transparent,FLinearColor::Transparent);Tab->SetStyle(Style);
        Caption(Tab)->SetColorAndOpacity(bShown?LDHUDStyle::TextPrimary:LDHUDStyle::TextSecondary);
        CastChecked<UBorder>(WidgetTree->FindWidget(Part(Categories[Index].Tab,TEXT("Line"))))->SetBrushColor(bShown?LDHUDStyle::TextAccent:FLinearColor::Transparent);
    }
    if(!bFeedbackProblem){ShowPageHint();}
    PopulateFooter();
}

void ULDControlsPanel::ShowPageHint()
{SetFeedback(Category==ECategory::Display?TEXT("本页更改点击“应用”后生效。"):TEXT("本页更改立即生效并自动保存。"),false);}

void ULDControlsPanel::SetFeedback(const FString& Message,bool bProblem)
{
    if(!Feedback){return;}
    bFeedbackProblem=bProblem;bFeedbackFromDisplay=false;Feedback->SetText(FText::FromString(Message));
    Feedback->SetColorAndOpacity(bProblem?LDHUDStyle::TextWarning:LDHUDStyle::TextSecondary);
}

void ULDControlsPanel::Populate()
{
    if(!Feedback){return;}
    const auto& Applied=*CastChecked<ALDPlayerController>(GetOwningPlayer())->Controls;
    // Text size (UI 3.3) rescales every label once per change; the fixed control columns already leave room for 125%.
    ApplyTextScale(Applied.TextScale);
    PopulatePreferences();
    PopulateBindings();
    bSyncingVolume=true;VolumeSlider->SetValue(Applied.MasterVolume);bSyncingVolume=false;
    PopulateFooter();
}

void ULDControlsPanel::PopulatePreferences()
{
    const auto& Applied=*CastChecked<ALDPlayerController>(GetOwningPlayer())->Controls;
    SetSwitch(WidgetTree,TEXT("ToggleQuickCast"),Applied.GetQuickCast());
    SetSwitch(WidgetTree,TEXT("ToggleEdgePan"),Applied.bEdgePan);
    SetSwitch(WidgetTree,TEXT("ToggleStartFollow"),Applied.bStartFollowing);
    SetSwitch(WidgetTree,TEXT("ToggleNetworkDiagnostics"),Applied.bShowNetworkDiagnostics);
    SetSwitch(WidgetTree,TEXT("ToggleFPS"),Applied.bShowFPS);
    VolumeValue->SetText(FText::FromString(FString::Printf(TEXT("%d%%"),FMath::RoundToInt(100*Applied.MasterVolume))));
    ZoomValue->SetText(FText::FromString(FString::Printf(TEXT("%.0f 厘米"),Applied.Zoom)));
    Find(WidgetTree,TEXT("UseRecommendedCameraZoom"))->SetIsEnabled(Applied.Zoom!=LDCamera::DefaultArmLength);
    SetNote(FollowNote,FString::Printf(TEXT("进入对局时镜头跟随自己的角色；对局中按 %s 随时切换。"),*Applied.Label(TEXT("Follow"))),LDHUDStyle::TextSecondary);
    const float Pan=Applied.GetCameraPanCmPerSec();
    SetStepper(WidgetTree,TEXT("SlowerCameraPan"),TEXT("FasterCameraPan"),TEXT("CameraPanValue"),FString::Printf(TEXT("%.0f cm/s"),Pan),
        Pan>ULDControlSettings::MinCameraPanCmPerSec,Pan<ULDControlSettings::MaxCameraPanCmPerSec);
    MarkChosen(Find(WidgetTree,TEXT("TextScale100")),Applied.TextScale<=1,RestStyle);
    MarkChosen(Find(WidgetTree,TEXT("TextScale125")),Applied.TextScale>1,RestStyle);
    for(const ELDMinimapOrientation Option:Orientations)
    {MarkChosen(Find(WidgetTree,OrientationButton(Option)),Option==Applied.GetMinimapOrientation(),RestStyle);}
}

void ULDControlsPanel::PopulateBindings()
{
    const auto& Applied=*CastChecked<ALDPlayerController>(GetOwningPlayer())->Controls;
    const auto& Definitions=ULDControlSettings::Definitions();
    for(int32 Index=0;Index<Definitions.Num()&&Index<BindingKeys.Num();++Index)
    {
        const FName Id=Definitions[Index].Action;
        const bool bCapturing=Capture==Id,bReserved=Id==TEXT("Back");
        UButton* Key=BindingKeys[Index];
        Caption(Key)->SetText(FText::FromString(bCapturing?FString(TEXT("按下新键…")):Applied.Label(Id)));
        MarkChosen(Key,bCapturing,RestStyle);
        Key->SetIsEnabled(!bReserved);
        // The row being captured, or the one that just ended a capture, states the result in place.
        if(bCapturing)
        {
            SetNote(BindingNotes[Index],bCaptureProblem?CaptureResult:FString(TEXT("按下新的键盘按键或鼠标按键；Esc 取消。")),
                bCaptureProblem?LDHUDStyle::TextWarning:LDHUDStyle::TextAccent);
        }
        else if(Id==LastCapture&&!CaptureResult.IsEmpty())
        {SetNote(BindingNotes[Index],CaptureResult,bCaptureProblem?LDHUDStyle::TextWarning:LDHUDStyle::TextSecondary);}
        else{SetNote(BindingNotes[Index],bReserved?FString(TEXT("Esc 保留给返回与取消，不能更改。")):FString(),LDHUDStyle::TextSecondary);}
    }
}

void ULDControlsPanel::PopulateFooter()
{
    if(!Feedback){return;}
    const auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    const ULDControlSettings* Applied=PC?PC->Controls.Get():nullptr;
    const auto* Instance=SettingsInstance.Get();
    const bool bRecover=Applied&&Applied->NeedsRecovery();
    // Unconfirmed values offer an explicit save only once no automatic flush is still on its way; a file that needs
    // recovery offers the recovery instead, because saving waits for that confirmation.
    Show(Find(WidgetTree,TEXT("RetryLocalSave")),Instance&&Applied&&!bRecover&&Applied->IsDirty()&&!Instance->IsLocalSettingsFlushPending());
    Show(Find(WidgetTree,TEXT("RecoverLocalSettings")),bRecover);
    Find(WidgetTree,TEXT("ResetCategoryDefaults"))->SetIsEnabled(Instance&&(Category!=ECategory::Display||bDisplayAvailable));
}

void ULDControlsPanel::ChangePreference(TFunction<void(ULDControlSettings&)> Change)
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());
    auto* Instance=SettingsInstance.Get();
    if(!Instance){SetFeedback(TEXT("本地设置的所有者不可用，没有应用任何更改。"),true);return;}
    auto* Draft=PC->Controls->CreateDraft();
    Change(*Draft);
    const auto Result=Instance->ApplyLocalSettings(*Draft);
    SetFeedback(Result.Message,!Result.IsSaved()&&Result.Status!=ELDLocalSettingsSaveStatus::NoChanges);
    Populate();
}

void ULDControlsPanel::HandleMasterVolumeChanged(float Value)
{
    auto* Instance=SettingsInstance.Get();auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());
    if(bSyncingVolume||!Instance||!PC||!PC->Controls){return;}
    // A drag reports many sub-percent values; only a changed whole percent is applied, and the instance saves it later.
    const float Linear=FMath::RoundToFloat(Value*100)/100;
    if(Linear==PC->Controls->MasterVolume){return;}
    const auto Result=Instance->SetAppliedMasterVolume(Linear);
    SetFeedback(Result.Message,Result.Status==ELDLocalSettingsSaveStatus::Invalid);PopulatePreferences();PopulateFooter();
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
    // Showing or dismissing a child confirmation never applies or discards a value; the close prompt starts from its own text.
    if(!CloseRows){Child=Which;return;}
    if(Which==EChildConfirmation::None&&bOpen&&ConfirmCard->HasFocusedDescendants()){SetKeyboardFocus();}
    Child=Which;
    auto Toggle=[Which](UWidget* Box,EChildConfirmation Kind){Show(Box,Which==Kind);};
    CloseDraftPrompt->SetText(FText::FromString(CloseDraftText));
    Toggle(ResetRows,EChildConfirmation::LocalReset);Toggle(RecoveryRows,EChildConfirmation::Recovery);Toggle(CloseRows,EChildConfirmation::CloseDraft);
    Show(ConfirmCard,Which!=EChildConfirmation::None);
    // The safe answer takes keyboard focus once the card is arranged: keep for a reset or a recovery, apply for the close
    // prompt while the draft can still be applied.
    PendingFocus=Which==EChildConfirmation::LocalReset?FName(TEXT("KeepLocalPreferences"))
        :Which==EChildConfirmation::Recovery?FName(TEXT("KeepSettingsFile"))
        :Which==EChildConfirmation::CloseDraft?FName(Find(WidgetTree,TEXT("ApplyDisplayAndReturn"))->GetIsEnabled()?TEXT("ApplyDisplayAndReturn"):TEXT("DiscardDisplayDraft"))
        :FName();
    PendingFocusFrame=GFrameCounter;
}

void ULDControlsPanel::AskReset()
{
    if(!Capture.IsNone()){EndCapture();}
    ResetCategory=Category;
    const auto* Defaults=GetDefault<ULDControlSettings>();
    FString Scope;
    switch(Category)
    {
    case ECategory::Display:
    {
        FLDDisplaySettings Product;FString Error;const auto* Live=GEngine?GEngine->GetGameUserSettings():nullptr;
        Scope=Live&&FLDDisplaySettings::ProductDefaults(*Live,Product,Error)
            ?FString::Printf(TEXT("垂直同步 %s、画质 %s，确认后立即应用；未应用的画面更改会被替换。"),OnOff(Product.bVSync),*QualityName(Product.Quality))
            :FString(TEXT("垂直同步与画质恢复为产品默认值，确认后立即应用；未应用的画面更改会被替换。"));
        break;
    }
    case ECategory::Audio:
        Scope=FString::Printf(TEXT("主音量 %d%%。"),FMath::RoundToInt(100*Defaults->MasterVolume));break;
    case ECategory::Controls:
        Scope=FString::Printf(TEXT("全部按键恢复为默认键，快速施法 %s。"),OnOff(Defaults->bQuickCast));break;
    case ECategory::Camera:
        Scope=FString::Printf(TEXT("镜头距离 %.0f 厘米、边缘平移 %s、开局镜头跟随 %s、镜头平移速度 %.0f cm/s。"),
            Defaults->Zoom,OnOff(Defaults->bEdgePan),OnOff(Defaults->bStartFollowing),Defaults->CameraPanCmPerSec);break;
    case ECategory::Interface:
        Scope=FString::Printf(TEXT("文字大小 %.0f%%、小地图朝向 %s、网络诊断 %s、FPS 显示 %s。"),100*Defaults->TextScale,
            *ULDControlSettings::MinimapOrientationLabel(Defaults->MinimapOrientation),OnOff(Defaults->bShowNetworkDiagnostics),OnOff(Defaults->bShowFPS));break;
    }
    ResetPrompt->SetText(FText::FromString(FString::Printf(TEXT("恢复“%s”页的默认值？%s\n其他页、名称与本局进度不变。"),CategoryTitle(Category),*Scope)));
    ShowChild(EChildConfirmation::LocalReset);
}

void ULDControlsPanel::ShowRecoveryConfirmation()
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());
    if(!PC->Controls->NeedsRecovery())
    {ShowChild(EChildConfirmation::None);SetFeedback(TEXT("没有需要替换的无法读取或不兼容的设置文件。"),false);PopulateFooter();return;}
    ShowChild(EChildConfirmation::Recovery);
}

void ULDControlsPanel::SetDisplayMessage(const FLDDisplaySettingsResult& Result)
{
    DisplayMessage=Result.Message;
    bDisplayMessageProblem=Result.Status!=ELDDisplaySettingsStatus::Loaded&&Result.Status!=ELDDisplaySettingsStatus::PersistenceVerified;
}

void ULDControlsPanel::RefreshDisplayDraft()
{
    auto* Instance=SettingsInstance.Get();
    bDisplayAvailable=false;bDisplayDraftDirty=false;bDisplayDraftRejected=false;
    if(!Instance){DisplayMessage=TEXT("画面设置不可用：找不到本地设置的所有者。");bDisplayMessageProblem=true;PopulateDisplay();return;}
    const auto Result=Instance->ReadDisplaySettings(DisplayDraft);
    bDisplayAvailable=Result.Status==ELDDisplaySettingsStatus::Loaded;
    // A plain read is not news; an unavailable display explains itself, and an applied value whose save still failed keeps
    // that failure beside its retry.
    if(!bDisplayAvailable){SetDisplayMessage(Result);}
    else if(Instance->AreDisplaySettingsDirty()){SetDisplayMessage(Instance->GetDisplaySettingsResult());}
    else{DisplayMessage.Reset();bDisplayMessageProblem=false;}
    PopulateDisplay();
}

void ULDControlsPanel::EditDisplayDraft()
{
    // A draft edited back to the live values leaves nothing to apply or to confirm on Back.
    FLDDisplaySettings Live;auto* Instance=SettingsInstance.Get();
    bDisplayDraftDirty=!Instance||Instance->ReadDisplaySettings(Live).Status!=ELDDisplaySettingsStatus::Loaded||!(DisplayDraft==Live);
    bDisplayDraftRejected=false;DisplayMessage.Reset();bDisplayMessageProblem=false;
    PopulateDisplay();
}

void ULDControlsPanel::StepQuality(int32 Direction)
{
    const int32 Level=DisplayDraft.Quality.GetSingleQualityLevel();
    // A custom mix steps from its lowest group, so neither arrow skips past a preset the player has not seen.
    const int32 From=Level>=0?Level:DisplayDraft.Quality.GetMinQualityLevel();
    DisplayDraft.Quality.SetFromSingleQualityLevel(FMath::Clamp(From+Direction,0,QualityLevels-1));
    EditDisplayDraft();
}

void ULDControlsPanel::PopulateDisplay()
{
    if(!DisplayRows){return;}
    auto* Instance=SettingsInstance.Get();
    const bool Available=bDisplayAvailable&&Instance;
    // A refused draft is not offered again, from the page or from the close prompt, until it is edited.
    const bool CanApply=Available&&bDisplayDraftDirty&&!bDisplayDraftRejected;
    Find(WidgetTree,TEXT("ApplyDisplayAndReturn"))->SetIsEnabled(CanApply);
    // The display tab carries a marker while its page holds changes that are not applied yet.
    Caption(Find(WidgetTree,Categories[int32(ECategory::Display)].Tab))->SetText(FText::FromString(
        Available&&bDisplayDraftDirty?FString(TEXT("画面 ●")):FString(Categories[int32(ECategory::Display)].Title)));
    Show(DisplayRows,Available);Show(DisplayBar,Available);
    SetNote(DisplayUnavailable,Available?FString():DisplayMessage,LDHUDStyle::TextWarning);
    PopulateFooter();
    if(!Available){return;}
    FLDDisplaySettings Live;
    const bool bLive=Instance->ReadDisplaySettings(Live).Status==ELDDisplaySettingsStatus::Loaded;
    SetSwitch(WidgetTree,TEXT("VSyncDraft"),DisplayDraft.bVSync);
    const int32 Level=DisplayDraft.Quality.GetSingleQualityLevel();
    SetStepper(WidgetTree,TEXT("QualityDraftLower"),TEXT("QualityDraftHigher"),TEXT("QualityDraftValue"),QualityName(DisplayDraft.Quality),
        Level!=0,Level<QualityLevels-1);
    const bool bVSyncChanged=bLive&&Live.bVSync!=DisplayDraft.bVSync;
    const bool bQualityChanged=bLive&&!(Live.Quality==DisplayDraft.Quality);
    SetNote(VSyncNote,bVSyncChanged?FString::Printf(TEXT("已更改，尚未应用（当前生效：%s）"),OnOff(Live.bVSync)):FString(VSyncHint),
        bVSyncChanged?LDHUDStyle::TextAccent:LDHUDStyle::TextSecondary);
    SetNote(QualityNote,bQualityChanged?FString::Printf(TEXT("已更改，尚未应用（当前生效：%s）"),*QualityName(Live.Quality)):FString(QualityHint),
        bQualityChanged?LDHUDStyle::TextAccent:LDHUDStyle::TextSecondary);
    auto* Apply=Find(WidgetTree,TEXT("ApplyDisplayDraft"));Apply->SetIsEnabled(CanApply);MarkChosen(Apply,CanApply,RestStyle);
    Find(WidgetTree,TEXT("RevertDisplayDraft"))->SetIsEnabled(bDisplayDraftDirty);
    Show(Find(WidgetTree,TEXT("RetryDisplaySave")),Instance->AreDisplaySettingsDirty());
    FString Line;FLinearColor Color=LDHUDStyle::TextSecondary;
    if(bDisplayDraftRejected){Line=DisplayMessage+TEXT("\n修改后才能再次应用；也可以撤销更改。");Color=LDHUDStyle::TextWarning;}
    else if(bDisplayDraftDirty)
    {
        TArray<FString> Changes;
        if(bVSyncChanged){Changes.Add(FString::Printf(TEXT("垂直同步 %s → %s"),OnOff(Live.bVSync),OnOff(DisplayDraft.bVSync)));}
        if(bQualityChanged){Changes.Add(FString::Printf(TEXT("画质 %s → %s"),*QualityName(Live.Quality),*QualityName(DisplayDraft.Quality)));}
        Line=Changes.IsEmpty()?FString(TEXT("有未应用的画面更改。")):TEXT("未应用：")+FString::Join(Changes,TEXT(" · "));
        Color=LDHUDStyle::TextAccent;
    }
    else if(!DisplayMessage.IsEmpty()){Line=DisplayMessage;Color=bDisplayMessageProblem?LDHUDStyle::TextWarning:LDHUDStyle::TextSecondary;}
    else{Line=TEXT("没有未应用的更改。");}
    DisplayFeedback->SetText(FText::FromString(Line));DisplayFeedback->SetColorAndOpacity(Color);
    DisplayBar->SetBrush(Face(bDisplayDraftDirty?BarPendingFill:BarFill,bDisplayDraftDirty?ChosenEdge:HairlineColor));
    // A display reset's failure in the footer is answered once the bar no longer reports a display problem.
    if(bFeedbackFromDisplay&&bFeedbackProblem&&!bDisplayMessageProblem&&!bDisplayDraftRejected){ShowPageHint();}
}

bool ULDControlsPanel::ApplyDisplay(FString& Message)
{
    auto* Instance=SettingsInstance.Get();
    if(!Instance){Message=TEXT("画面设置不可用：找不到本地设置的所有者。");bDisplayDraftRejected=true;}
    else
    {
        const auto Result=Instance->ApplyDisplaySettings(DisplayDraft);
        SeenDisplayStatus=Result.Status;Message=Result.Message;
        // Applied means saved, or applied with only the save left to retry ("重试保存").
        if(Result.Status==ELDDisplaySettingsStatus::PersistenceVerified||Result.Status==ELDDisplaySettingsStatus::PersistenceFailed)
        {bDisplayDraftDirty=false;bDisplayDraftRejected=false;SetDisplayMessage(Result);PopulateDisplay();return true;}
        bDisplayDraftRejected=true;
    }
    DisplayMessage=Message;bDisplayMessageProblem=true;
    PopulateDisplay();
    return false;
}
void ULDControlsPanel::ApplyDisplay(){FString Message;ApplyDisplay(Message);}
void ULDControlsPanel::ApplyDisplayAndReturn()
{
    FString Message;
    if(ApplyDisplay(Message)){ForceClose();return;}
    // The failure is stated on the confirmation itself, whose default choice becomes leaving without the draft.
    ShowChild(EChildConfirmation::CloseDraft);
    CloseDraftPrompt->SetText(FText::FromString(FString::Printf(TEXT("画面更改没有应用：%s\n可以放弃更改并返回（再按“返回”或 Esc 也会放弃），或继续编辑。"),*Message)));
    PendingFocus=TEXT("DiscardDisplayDraft");
}

void ULDControlsPanel::ConfirmResetDefaults()
{
    ShowChild(EChildConfirmation::None);
    auto* Instance=SettingsInstance.Get();
    if(!Instance){SetFeedback(TEXT("本地设置的所有者不可用，没有恢复任何默认值。"),true);return;}
    const FString Prefix=FString::Printf(TEXT("恢复“%s”页默认值："),CategoryTitle(ResetCategory));
    if(ResetCategory==ECategory::Display)
    {
        RestoreDisplayDefaults(*Instance);
        SetFeedback(Prefix+DisplayMessage,bDisplayMessageProblem);
        bFeedbackFromDisplay=true;
        return;
    }
    const ELDPreferenceGroup Group=ResetCategory==ECategory::Audio?ELDPreferenceGroup::Audio
        :ResetCategory==ECategory::Controls?ELDPreferenceGroup::Controls
        :ResetCategory==ECategory::Camera?ELDPreferenceGroup::Camera:ELDPreferenceGroup::Interface;
    auto* Draft=CastChecked<ALDPlayerController>(GetOwningPlayer())->Controls->CreateDraft();
    Draft->ResetPreferenceGroup(Group);
    const auto Result=Instance->ApplyLocalSettings(*Draft);
    if(Group==ELDPreferenceGroup::Controls){LastCapture=NAME_None;CaptureResult.Reset();bCaptureProblem=false;}
    SetFeedback(Prefix+Result.Message,!Result.IsSaved()&&Result.Status!=ELDLocalSettingsSaveStatus::NoChanges);
    Populate();
}

void ULDControlsPanel::RestoreDisplayDefaults(ULDGameInstance& Instance)
{
    auto* LiveSettings=GEngine?GEngine->GetGameUserSettings():nullptr;
    FLDDisplaySettings Current,Defaults;FString Error;
    const auto Read=Instance.ReadDisplaySettings(Current);
    if(Read.Status!=ELDDisplaySettingsStatus::Loaded||!LiveSettings)
    {DisplayMessage=TEXT("画面没有恢复默认：")+Read.Message;bDisplayMessageProblem=true;PopulateDisplay();return;}
    if(!FLDDisplaySettings::ProductDefaults(*LiveSettings,Defaults,Error))
    {DisplayMessage=Error;bDisplayMessageProblem=true;PopulateDisplay();return;}
    // The confirmed reset replaces any unapplied draft.
    RefreshDisplayDraft();
    if(Defaults==Current){DisplayMessage=TEXT("垂直同步与画质已是默认值。");bDisplayMessageProblem=false;PopulateDisplay();return;}
    DisplayDraft=Defaults;bDisplayDraftDirty=true;
    ApplyDisplay();
}

void ULDControlsPanel::Open()
{
    // Reopening an open panel only refocuses it; it must not silently discard an unapplied display draft.
    if(bOpen){SetKeyboardFocus();return;}
    bOpen=true;Capture=NAME_None;LastCapture=NAME_None;CaptureResult.Reset();bCaptureProblem=false;bFeedbackProblem=false;
    SettingsInstance=GetGameInstance<ULDGameInstance>();
    SetVisibility(ESlateVisibility::Visible);
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());PC->SetMenuOpen(true);
    Show(MatchNote,!PC->IsFrontend(),ESlateVisibility::HitTestInvisible);
    ShowChild(EChildConfirmation::None);PendingFocus=NAME_None;
    auto* Instance=SettingsInstance.Get();
    // Only results that arrive while the panel is open are news; the last apply of an earlier visit is not shown again.
    if(Instance){SeenDisplayStatus=Instance->GetDisplaySettingsResult().Status;}
    SetKeyboardFocus();Populate();RefreshDisplayDraft();
    // The panel reopens on the page it was left on.
    ShowCategory(Category);
    // A recovery or not-yet-saved values (migration, a failed save) are stated in the footer beside their action.
    const ULDControlSettings* Applied=PC->Controls.Get();
    if(Instance&&Applied&&(Applied->NeedsRecovery()||Applied->IsDirty())){SetFeedback(Instance->GetLocalSettingsMessage(),Applied->NeedsRecovery());}
}
void ULDControlsPanel::BeginCapture(FName Action)
{
    if(Child!=EChildConfirmation::None){ShowChild(EChildConfirmation::None);}
    Capture=Action;LastCapture=Action;CaptureResult.Reset();bCaptureProblem=false;
    PopulateBindings();
    const int32 Index=BindingIndex(Action);
    if(BindingNotes.IsValidIndex(Index)){PendingRevealRow=RowOf(BindingNotes[Index]);PendingRevealFrame=GFrameCounter;RevealRow(PendingRevealRow);}
}
void ULDControlsPanel::EndCapture()
{
    LastCapture=Capture;Capture=NAME_None;CaptureResult=TEXT("按键未更改。");bCaptureProblem=false;
    PopulateBindings();
}
bool ULDControlsPanel::Close()
{
    if(!bOpen){return true;}
    if(!Capture.IsNone()){EndCapture();return false;}
    // Back while the unapplied-draft prompt shows leaves without the draft, as the prompt says.
    if(Child==EChildConfirmation::CloseDraft){ForceClose();return true;}
    if(Child!=EChildConfirmation::None){ShowChild(EChildConfirmation::None);return false;}
    // The prompt opens over the display page, so the pending changes it names are in view.
    if(bDisplayDraftDirty){ShowCategory(ECategory::Display);ShowChild(EChildConfirmation::CloseDraft);return false;}
    ForceClose();return true;
}
bool ULDControlsPanel::HasChildConfirmation() const
{return bOpen&&(!Capture.IsNone()||Child!=EChildConfirmation::None);}
bool ULDControlsPanel::CloseChildConfirmation()
{
    if(!bOpen){return false;}
    if(!Capture.IsNone()){EndCapture();return true;}
    // Back on the unapplied-draft prompt is the same as its "discard and return" choice, so it can never loop.
    if(Child==EChildConfirmation::CloseDraft){ForceClose();return true;}
    // Same as the visible "keep unchanged" choices: nothing is applied or discarded.
    if(Child!=EChildConfirmation::None){ShowChild(EChildConfirmation::None);return true;}
    return false;
}
void ULDControlsPanel::ForceClose()
{
    if(auto* Instance=SettingsInstance.Get()){Instance->FlushLocalSettings();}
    Capture=NAME_None;bOpen=false;bDisplayDraftDirty=false;bDisplayDraftRejected=false;ShowChild(EChildConfirmation::None);PendingFocus=NAME_None;PendingRevealRow=nullptr;
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
    if(auto* Instance=SettingsInstance.Get()){Instance->FlushLocalSettings();}
    bOpen=false;
    Super::NativeDestruct();
}
void ULDControlsPanel::NativeTick(const FGeometry& Geometry,float Delta)
{
    Super::NativeTick(Geometry,Delta);
    auto* Instance=SettingsInstance.Get();
    if(!bOpen||!Instance){return;}
    if(!PendingFocus.IsNone()&&GFrameCounter>PendingFocusFrame)
    {
        UWidget* Target=WidgetTree->FindWidget(PendingFocus);
        if(Target&&Target->IsVisible()&&Target->GetIsEnabled()){Target->SetKeyboardFocus();}
        PendingFocus=NAME_None;
    }
    // A wrapped result line changes the row height only after layout, so the row is revealed a frame later.
    if(PendingRevealRow&&GFrameCounter>PendingRevealFrame+1){RevealRow(PendingRevealRow);PendingRevealRow=nullptr;}
    // A save retried elsewhere (travel, exit) reports here; the panel's own applies already recorded their status.
    const auto& Result=Instance->GetDisplaySettingsResult();
    if(Result.Status!=SeenDisplayStatus){SetDisplayMessage(Result);PopulateDisplay();}
    SeenDisplayStatus=Result.Status;
    if(bVolumeSavePending&&!Instance->IsLocalSettingsFlushPending())
    {
        bVolumeSavePending=false;
        const ULDControlSettings* Applied=CastChecked<ALDPlayerController>(GetOwningPlayer())->Controls.Get();
        SetFeedback(Instance->GetLocalSettingsMessage(),Applied&&(Applied->IsDirty()||Applied->NeedsRecovery()));
        PopulatePreferences();PopulateFooter();
    }
}
FReply ULDControlsPanel::CaptureKey(FKey Key)
{
    if(Key==EKeys::Escape){EndCapture();return FReply::Handled();}
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());FString Error;auto* Draft=PC->Controls->CreateDraft();
    if(!Draft->Rebind(Capture,Key,Error))
    {
        // A refused key (reserved, wrong device or already used) is named on the row and the capture keeps waiting.
        CaptureResult=Error;bCaptureProblem=true;PopulateBindings();
        const int32 Index=BindingIndex(Capture);
        if(BindingNotes.IsValidIndex(Index)){PendingRevealRow=RowOf(BindingNotes[Index]);PendingRevealFrame=GFrameCounter;}
        return FReply::Handled();
    }
    const FName Action=Capture;
    LastCapture=Action;Capture=NAME_None;
    auto* Instance=SettingsInstance.Get();
    const FLDLocalSettingsSaveResult Result=Instance?Instance->ApplyLocalSettings(*Draft)
        :FLDLocalSettingsSaveResult{ELDLocalSettingsSaveStatus::Invalid,TEXT("本地设置的所有者不可用。")};
    const bool bApplied=PC->Controls->Key(Action)==Key;
    bCaptureProblem=!bApplied;
    CaptureResult=bApplied?FString::Printf(TEXT("已改为 %s。"),*PC->Controls->Label(Action)):TEXT("按键未更改：")+Result.Message;
    SetFeedback(Result.Message,!Result.IsSaved()&&Result.Status!=ELDLocalSettingsSaveStatus::NoChanges);
    Populate();
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
