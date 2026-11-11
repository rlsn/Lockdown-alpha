#include "UI/LDChoicesPanel.h"
#include "Core/LDLog.h"
#include "UI/LDHUDModel.h"
#include "UI/LDHUDStyle.h"
#include "UI/LDTerms.h"
#include "UI/LDUIIcons.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDBuildBlueprintComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/ScaleBox.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/ScrollBox.h"
#include "Components/WrapBox.h"
#include "Components/TextBlock.h"
#include "Components/RichTextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"

namespace
{
const FLinearColor GrowthCardIdle(.045f,.12f,.15f);
// Card faces stay compact; complete rules live in the independent inspection column.
constexpr float GrowthCardWidth=540.f,GrowthCardHeight=72.f,GrowthIconSize=44.f,GrowthDetailWidth=540.f,GrowthIconRadius=7.f;
constexpr float GrowthPanelWidth=540.f;
// #198 F target step: place buttons per line.
constexpr int32 GrowthTargetsPerLine=4;
// #198 G dismantle selection buttons per line (their labels may carry the installed warning).
constexpr int32 GrowthDismantlePerLine=1;
void GrowthTint(ULDCommandButton* Button,const FLinearColor& Color)
{
    auto Style=Button->GetStyle();Style.Normal.TintColor=FSlateColor(Color);Style.Hovered.TintColor=FSlateColor(Color*1.6f);Button->SetStyle(Style);
}
void GrowthText(UTextBlock* Text,const FString& Value)
{
    if(Text&&!Text->GetText().ToString().Equals(Value,ESearchCase::CaseSensitive)){Text->SetText(FText::FromString(Value));}
}
void GrowthShown(UWidget* Widget,bool bShown,ESlateVisibility Visible=ESlateVisibility::SelfHitTestInvisible)
{
    const ESlateVisibility Wanted=bShown?Visible:ESlateVisibility::Collapsed;
    if(Widget&&Widget->GetVisibility()!=Wanted){Widget->SetVisibility(Wanted);}
}
void GrowthEnabled(UWidget* Widget,bool bEnabled)
{
    if(Widget&&Widget->GetIsEnabled()!=bEnabled){Widget->SetIsEnabled(bEnabled);}
}
}

TSharedRef<SWidget> ULDChoicesPanel::RebuildWidget()
{
    if(!MainPanel)
    {
        auto Text=[this](const FString& Value,int32 Size){return LDWidgets::Text(WidgetTree,Value,Size);};
        auto OneLine=[&Text](int32 Size){UTextBlock* Line=Text(TEXT(""),Size);Line->SetAutoWrapText(false);Line->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);return Line;};
        auto Wrapped=[&Text](int32 Size,float Width){UTextBlock* Line=Text(TEXT(""),Size);Line->SetAutoWrapText(false);Line->SetWrapTextAt(Width);return Line;};
        auto* Root=WidgetTree->ConstructWidget<UVerticalBox>();TaskContent=Root;
        PointsDock=WidgetTree->ConstructWidget<UVerticalBox>();
        EmptyNote=Wrapped(15,GrowthPanelWidth);
        Root->AddChildToVerticalBox(EmptyNote);
        {
            // Each offer is a full-width task row; a click on a row is the choice.
            auto* Section=WidgetTree->ConstructWidget<UVerticalBox>();
            OfferHeading=OneLine(16);OfferHeading->SetColorAndOpacity(LDHUDStyle::TextAccent);Section->AddChildToVerticalBox(OfferHeading);
            CardRow=WidgetTree->ConstructWidget<UVerticalBox>();Section->AddChildToVerticalBox(CardRow)->SetPadding(FMargin(0,6,0,0));
            for(int32 Index=0;Index<3;++Index)
            {
                FCardWidgets& Card=Cards.AddDefaulted_GetRef();
                auto* Face=WidgetTree->ConstructWidget<UHorizontalBox>();
                auto* IconBox=WidgetTree->ConstructWidget<UOverlay>();
                Card.IconShape=WidgetTree->ConstructWidget<UBorder>();Card.IconShape->SetVisibility(ESlateVisibility::HitTestInvisible);
                Card.IconShape->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White,GrowthIconRadius,FLinearColor(1,1,1,.55f),1.5f));
                Card.IconShape->SetBrushColor(FLinearColor(.12f,.13f,.15f));
                Card.IconSize=WidgetTree->ConstructWidget<USizeBox>();Card.IconSize->SetWidthOverride(GrowthIconSize);Card.IconSize->SetHeightOverride(GrowthIconSize);
                Card.IconSize->SetContent(Card.IconShape);
                auto* ShapeSlot=IconBox->AddChildToOverlay(Card.IconSize);ShapeSlot->SetHorizontalAlignment(HAlign_Center);ShapeSlot->SetVerticalAlignment(VAlign_Center);
                Card.Picture=WidgetTree->ConstructWidget<UImage>();Card.Picture->SetVisibility(ESlateVisibility::Collapsed);
                auto* PictureSlot=IconBox->AddChildToOverlay(Card.Picture);PictureSlot->SetHorizontalAlignment(HAlign_Fill);PictureSlot->SetVerticalAlignment(VAlign_Fill);
                Card.Glyph=Text(TEXT(""),18);Card.Glyph->SetAutoWrapText(false);Card.Glyph->SetJustification(ETextJustify::Center);
                auto* GlyphSlot=IconBox->AddChildToOverlay(Card.Glyph);GlyphSlot->SetHorizontalAlignment(HAlign_Center);GlyphSlot->SetVerticalAlignment(VAlign_Center);
                auto* IconFrame=WidgetTree->ConstructWidget<USizeBox>();IconFrame->SetWidthOverride(GrowthIconSize);IconFrame->SetHeightOverride(GrowthIconSize);IconFrame->SetContent(IconBox);
                auto* IconSlot=Face->AddChildToHorizontalBox(IconFrame);IconSlot->SetVerticalAlignment(VAlign_Top);IconSlot->SetPadding(FMargin(0,0,10,0));
                auto* Lines=WidgetTree->ConstructWidget<UVerticalBox>();
                Card.Name=OneLine(17);Lines->AddChildToVerticalBox(Card.Name);
                Card.Summary=Wrapped(14,GrowthCardWidth-GrowthIconSize-40.f);Card.Summary->SetColorAndOpacity(LDHUDStyle::TextSecondary);
                Lines->AddChildToVerticalBox(Card.Summary)->SetPadding(FMargin(0,2,0,0));
                Card.Action=Wrapped(14,GrowthCardWidth-GrowthIconSize-40.f);Card.Action->SetColorAndOpacity(LDHUDStyle::TextAccent);
                Lines->AddChildToVerticalBox(Card.Action)->SetPadding(FMargin(0,4,0,0));
                auto* LinesSlot=Face->AddChildToHorizontalBox(Lines);LinesSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));LinesSlot->SetVerticalAlignment(VAlign_Center);
                Card.Button=LDWidgets::Button(WidgetTree,TEXT(""),[this,Index](){ChooseOffer(Index);},false,FName(*FString::Printf(TEXT("GrowthOffer%d"),Index)));
                Card.Button->SetContent(Face);GrowthTint(Card.Button,GrowthCardIdle);
                // After SetContent: replacing the caption makes a new, centred slot, which would shrink the icon and lines to
                // their own width in the middle of the card; they start at the card's left edge instead.
                if(UButtonSlot* FaceSlot=Cast<UButtonSlot>(Card.Button->GetContentSlot())){FaceSlot->SetHorizontalAlignment(HAlign_Fill);FaceSlot->SetVerticalAlignment(VAlign_Center);}
                auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetWidthOverride(GrowthCardWidth);Size->SetMinDesiredHeight(GrowthCardHeight);Size->SetContent(Card.Button);
                CardRow->AddChildToVerticalBox(Size)->SetPadding(FMargin(0,0,0,6));
            }
            CardViews.SetNum(Cards.Num());
            OfferNote=Wrapped(16,GrowthPanelWidth);OfferNote->SetColorAndOpacity(LDHUDStyle::TextSecondary);Section->AddChildToVerticalBox(OfferNote)->SetPadding(FMargin(0,4,0,0));
            OfferSection=Section;Root->AddChildToVerticalBox(Section);
        }
        {
            // #198 B opening base pick: one card per current option, three per line, built as needed.
            auto* Section=WidgetTree->ConstructWidget<UVerticalBox>();
            UTextBlock* Heading=OneLine(16);Heading->SetColorAndOpacity(LDHUDStyle::TextAccent);Heading->SetText(FText::FromString(LDTerms::OpeningBase));
            Section->AddChildToVerticalBox(Heading);
            BaseCardList=WidgetTree->ConstructWidget<UVerticalBox>();Section->AddChildToVerticalBox(BaseCardList)->SetPadding(FMargin(0,6,0,0));
            OpeningBaseSection=Section;Root->AddChildToVerticalBox(Section)->SetPadding(FMargin(0,10,0,0));
        }
        {
            // #198 G pending elite camp component choices, oldest first: one line each, built as needed.
            auto* Section=WidgetTree->ConstructWidget<UVerticalBox>();
            ComponentChoiceList=WidgetTree->ConstructWidget<UVerticalBox>();Section->AddChildToVerticalBox(ComponentChoiceList);
            ComponentChoiceSection=Section;Root->AddChildToVerticalBox(Section)->SetPadding(FMargin(0,10,0,0));
        }
        {
            // The binding step of B18: a learned active (one button each), then an element.
            auto* Section=WidgetTree->ConstructWidget<UVerticalBox>();
            BindingHeading=Wrapped(15,GrowthPanelWidth);BindingHeading->SetColorAndOpacity(LDHUDStyle::TextAccent);Section->AddChildToVerticalBox(BindingHeading);
            auto* ActiveLine=WidgetTree->ConstructWidget<UHorizontalBox>();
            UTextBlock* ActiveLabel=OneLine(13);ActiveLabel->SetText(FText::FromString(LDTerms::PickBoundActive));
            ActiveLine->AddChildToHorizontalBox(ActiveLabel)->SetVerticalAlignment(VAlign_Center);
            for(int32 Index=0;Index<3;++Index)
            {
                ULDCommandButton* Pick=LDWidgets::Button(WidgetTree,TEXT(""),[this,Index](){PickBindActive(Index);},false,FName(*FString::Printf(TEXT("GrowthBindActive%d"),Index)));
                ActiveLine->AddChildToHorizontalBox(Pick)->SetPadding(FMargin(6,0,0,0));
                BindActiveButtons.Add(Pick);
            }
            Section->AddChildToVerticalBox(ActiveLine)->SetPadding(FMargin(0,4,0,0));
            auto* ElementLine=WidgetTree->ConstructWidget<UHorizontalBox>();
            UTextBlock* ElementLabel=OneLine(13);ElementLabel->SetText(FText::FromString(LDTerms::PickElement));
            ElementLine->AddChildToHorizontalBox(ElementLabel)->SetVerticalAlignment(VAlign_Center);
            for(const ELDDemoElement Element:{ELDDemoElement::Fire,ELDDemoElement::Water,ELDDemoElement::Electric})
            {
                ULDCommandButton* Pick=LDWidgets::Button(WidgetTree,LDHUD::ElementName(Element),[this,Element](){PickBindElement(Element);},false,
                    FName(*FString::Printf(TEXT("GrowthBindElement%d"),int32(Element))));
                ElementLine->AddChildToHorizontalBox(Pick)->SetPadding(FMargin(6,0,0,0));
                BindElementButtons.Add(Pick);
            }
            BindElementLine=ElementLine;
            Section->AddChildToVerticalBox(ElementLine)->SetPadding(FMargin(0,4,0,0));
            auto* CancelLine=WidgetTree->ConstructWidget<UHorizontalBox>();
            CancelLine->AddChildToHorizontalBox(LDWidgets::Button(WidgetTree,LDTerms::CancelBinding,[this](){CancelBinding();},false,TEXT("GrowthBindCancel")));
            Section->AddChildToVerticalBox(CancelLine)->SetPadding(FMargin(0,4,0,0));
            BindingSection=Section;Root->AddChildToVerticalBox(Section)->SetPadding(FMargin(0,10,0,0));
        }
        {
            // #198 F target step of a mod or connector: one button per place it takes now, then C12's repeated base.
            auto* Section=WidgetTree->ConstructWidget<UVerticalBox>();
            TargetHeading=Wrapped(15,GrowthPanelWidth);TargetHeading->SetColorAndOpacity(LDHUDStyle::TextAccent);Section->AddChildToVerticalBox(TargetHeading);
            UTextBlock* PlaceLabel=OneLine(13);PlaceLabel->SetText(FText::FromString(LDTerms::PickInstallTarget));
            Section->AddChildToVerticalBox(PlaceLabel)->SetPadding(FMargin(0,4,0,0));
            TargetList=WidgetTree->ConstructWidget<UVerticalBox>();Section->AddChildToVerticalBox(TargetList)->SetPadding(FMargin(0,2,0,0));
            RepeatLine=WidgetTree->ConstructWidget<UHorizontalBox>();
            UTextBlock* BaseLabel=OneLine(13);BaseLabel->SetText(FText::FromString(LDTerms::PickRepeatedBase));
            RepeatLine->AddChildToHorizontalBox(BaseLabel)->SetVerticalAlignment(VAlign_Center);
            Section->AddChildToVerticalBox(RepeatLine)->SetPadding(FMargin(0,4,0,0));
            auto* CancelLine=WidgetTree->ConstructWidget<UHorizontalBox>();
            CancelLine->AddChildToHorizontalBox(LDWidgets::Button(WidgetTree,LDTerms::CancelBinding,[this](){CancelTarget();},false,TEXT("GrowthTargetCancel")));
            Section->AddChildToVerticalBox(CancelLine)->SetPadding(FMargin(0,4,0,0));
            TargetSection=Section;Root->AddChildToVerticalBox(Section)->SetPadding(FMargin(0,10,0,0));
        }
        {
            // Pending blueprints: one row each, built as needed, with a "学到 Q/W/E" button per empty slot.
            auto* Section=WidgetTree->ConstructWidget<UVerticalBox>();
            UTextBlock* Heading=OneLine(16);Heading->SetColorAndOpacity(LDHUDStyle::TextAccent);
            Heading->SetText(FText::FromString(TEXT("待学主动图纸 · 学到 Q / W / E 后才能使用")));
            Section->AddChildToVerticalBox(Heading);
            BlueprintList=WidgetTree->ConstructWidget<UVerticalBox>();Section->AddChildToVerticalBox(BlueprintList)->SetPadding(FMargin(0,4,0,0));
            BlueprintSection=Section;Root->AddChildToVerticalBox(Section)->SetPadding(FMargin(0,10,0,0));
        }
        {
            // #198 B owned components: one row each, built as needed.
            auto* Section=WidgetTree->ConstructWidget<UVerticalBox>();
            ComponentHeading=OneLine(16);ComponentHeading->SetColorAndOpacity(LDHUDStyle::TextAccent);
            Section->AddChildToVerticalBox(ComponentHeading);
            InstalledComponentHeading=OneLine(16);InstalledComponentHeading->SetColorAndOpacity(LDHUDStyle::TextAccent);
            InstalledComponentHeading->SetText(FText::FromString(TEXT("已安装")));
            Section->AddChildToVerticalBox(InstalledComponentHeading)->SetPadding(FMargin(0,8,0,2));
            InstalledComponentList=WidgetTree->ConstructWidget<UVerticalBox>();
            Section->AddChildToVerticalBox(InstalledComponentList);
            UninstalledComponentHeading=OneLine(16);UninstalledComponentHeading->SetColorAndOpacity(LDHUDStyle::TextAccent);
            UninstalledComponentHeading->SetText(FText::FromString(TEXT("未安装")));
            Section->AddChildToVerticalBox(UninstalledComponentHeading)->SetPadding(FMargin(0,12,0,2));
            UninstalledComponentList=WidgetTree->ConstructWidget<UVerticalBox>();
            Section->AddChildToVerticalBox(UninstalledComponentList);
            ComponentSection=Section;Root->AddChildToVerticalBox(Section)->SetPadding(FMargin(0,10,0,0));
        }
        {
            // Unspent points: one row per stat in design order (built from the first catalog seen), +/− stage locally, one confirm sends.
            auto* Section=WidgetTree->ConstructWidget<UVerticalBox>();
            PointsHeading=OneLine(16);PointsHeading->SetColorAndOpacity(LDHUDStyle::TextAccent);Section->AddChildToVerticalBox(PointsHeading);
            StatList=WidgetTree->ConstructWidget<UVerticalBox>();Section->AddChildToVerticalBox(StatList)->SetPadding(FMargin(0,4,0,0));
            auto* ConfirmLine=WidgetTree->ConstructWidget<UVerticalBox>();
            ConfirmButton=LDWidgets::Button(WidgetTree,LDTerms::ConfirmPoints,[this](){ConfirmPoints();},false,TEXT("GrowthConfirmPoints"));
            ConfirmLine->AddChildToVerticalBox(ConfirmButton);
            PointsBlocked=Wrapped(13,220);PointsBlocked->SetColorAndOpacity(LDHUDStyle::TextSecondary);
            ConfirmLine->AddChildToVerticalBox(PointsBlocked)->SetPadding(FMargin(0,6,0,0));
            Section->AddChildToVerticalBox(ConfirmLine)->SetPadding(FMargin(0,4,0,0));
            PointsSection=Section;PointsDock->AddChildToVerticalBox(Section)->SetPadding(FMargin(0,10,0,0));
        }
        {
            // #198 rest services: shown in a rest, enabled only in its build windows, each with its own reason when refused.
            auto* Section=WidgetTree->ConstructWidget<UVerticalBox>();
            UTextBlock* Heading=OneLine(16);Heading->SetColorAndOpacity(LDHUDStyle::TextAccent);Heading->SetText(FText::FromString(FString(LDTerms::RestServices)));
            Section->AddChildToVerticalBox(Heading);
            auto ServiceLine=[&](UWidget* Lead,UTextBlock*& OutBlocked)
            {
                auto* Line=WidgetTree->ConstructWidget<UVerticalBox>();
                Line->AddChildToVerticalBox(Lead)->SetHorizontalAlignment(HAlign_Left);
                OutBlocked=Wrapped(15,GrowthPanelWidth-20.f);OutBlocked->SetColorAndOpacity(LDHUDStyle::TextSecondary);
                Line->AddChildToVerticalBox(OutBlocked)->SetPadding(FMargin(0,4,0,0));
                Section->AddChildToVerticalBox(Line)->SetPadding(FMargin(0,4,0,0));
            };
            UTextBlock* Blocked=nullptr;
            NpcButton=LDWidgets::Button(WidgetTree,TEXT(""),[this](){BuyNpcOffer();},false,TEXT("GrowthRestNpc"));
            ServiceLine(NpcButton.Get(),Blocked);NpcBlocked=Blocked;
            RespecButton=LDWidgets::Button(WidgetTree,LDTerms::Respec,[this](){Respec();},false,TEXT("GrowthRestRespec"));
            ServiceLine(RespecButton.Get(),Blocked);RespecBlocked=Blocked;
            auto* ForgetRow=WidgetTree->ConstructWidget<UHorizontalBox>();
            for(uint8 Key=0;Key<3;++Key)
            {
                ULDCommandButton* Forget=LDWidgets::Button(WidgetTree,TEXT(""),[this,Key](){AskForget(Key);},false,
                    FName(*FString::Printf(TEXT("GrowthRestForget%s"),*LDHUD::SlotKey(Key))));
                auto* Caption=CastChecked<UTextBlock>(Forget->GetContent());Caption->SetAutoWrapText(false);Caption->SetWrapTextAt(130);
                ForgetRow->AddChildToHorizontalBox(Forget)->SetPadding(FMargin(Key?6.f:0.f,0,0,0));
                ForgetButtons.Add(Forget);
            }
            ServiceLine(ForgetRow,Blocked);ForgetBlocked=Blocked;
            // #198 G FillGap: the service button, then the gap targets and the picked target's fillers (buttons built as needed).
            FillGapButton=LDWidgets::Button(WidgetTree,TEXT(""),[this](){ToggleFillGap();},false,TEXT("GrowthRestFillGap"));
            ServiceLine(FillGapButton.Get(),Blocked);FillGapBlocked=Blocked;
            auto PickLine=[&](const TCHAR* Label)
            {
                auto* Line=WidgetTree->ConstructWidget<UWrapBox>();Line->SetExplicitWrapSize(true);Line->SetWrapSize(GrowthPanelWidth-24);
                Line->SetInnerSlotPadding(FVector2D(6,4));
                UTextBlock* Lead=OneLine(13);Lead->SetText(FText::FromString(Label));
                Line->AddChildToWrapBox(Lead);
                Line->SetVisibility(ESlateVisibility::Collapsed);
                Section->AddChildToVerticalBox(Line)->SetPadding(FMargin(12,4,0,0));
                return Line;
            };
            GapTargetLine=PickLine(LDTerms::PickGapTarget);
            GapFillerLine=PickLine(LDTerms::PickGapFiller);
            // #198 G Dismantle: the service button, then the owned common components to select (a few per line).
            DismantleButton=LDWidgets::Button(WidgetTree,TEXT(""),[this](){ToggleDismantle();},false,TEXT("GrowthRestDismantle"));
            ServiceLine(DismantleButton.Get(),Blocked);DismantleBlocked=Blocked;
            {
                auto* Picker=WidgetTree->ConstructWidget<UVerticalBox>();
                DismantleHeading=OneLine(13);Picker->AddChildToVerticalBox(DismantleHeading);
                DismantleList=WidgetTree->ConstructWidget<UVerticalBox>();Picker->AddChildToVerticalBox(DismantleList)->SetPadding(FMargin(0,2,0,0));
                Picker->SetVisibility(ESlateVisibility::Collapsed);
                DismantleSection=Picker;
                Section->AddChildToVerticalBox(Picker)->SetPadding(FMargin(12,4,0,0));
            }
            // The confirm line of a replace or forget names the active that will be forgotten.
            auto* ConfirmRow=WidgetTree->ConstructWidget<UHorizontalBox>();
            RestConfirmText=Wrapped(16,GrowthPanelWidth-220.f);RestConfirmText->SetColorAndOpacity(LDHUDStyle::TextWarning);
            auto* ConfirmTextSlot=ConfirmRow->AddChildToHorizontalBox(RestConfirmText);ConfirmTextSlot->SetVerticalAlignment(VAlign_Center);
            ConfirmTextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            RestConfirmButton=LDWidgets::Button(WidgetTree,LDTerms::ConfirmAction,[this](){ConfirmRestAction();},false,TEXT("GrowthRestConfirm"));
            ConfirmRow->AddChildToHorizontalBox(RestConfirmButton)->SetPadding(FMargin(8,0,0,0));
            ConfirmRow->AddChildToHorizontalBox(LDWidgets::Button(WidgetTree,LDTerms::CancelAction,[this](){CancelRestAction();},false,TEXT("GrowthRestCancel")))
                ->SetPadding(FMargin(6,0,0,0));
            ConfirmRow->SetVisibility(ESlateVisibility::Collapsed);
            RestConfirmLine=ConfirmRow;
            Section->SetVisibility(ESlateVisibility::Collapsed);
            RestSection=Section;RestConfirmLine=ConfirmRow;
        }
        // Only a result or a refusal shows at the bottom (no standing instructions); it collapses while empty.
        auto* Workspace=WidgetTree->ConstructWidget<UVerticalBox>();
        auto* Header=WidgetTree->ConstructWidget<UHorizontalBox>();
        Header->AddChildToHorizontalBox(Text(TEXT("构筑"),24))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        Header->AddChildToHorizontalBox(LDWidgets::Button(WidgetTree,TEXT("关闭"),[this](){ClosePanel();},false,TEXT("GrowthClose")));
        Workspace->AddChildToVerticalBox(Header)->SetPadding(FMargin(0,0,0,8));
        auto* Tabs=WidgetTree->ConstructWidget<UHorizontalBox>();
        const TCHAR* Labels[]={TEXT("技能与图纸"),TEXT("构件"),TEXT("加点")};
        for(int32 Tab=0;Tab<3;++Tab)
        {
            auto* Button=LDWidgets::Button(WidgetTree,Labels[Tab],[this,Tab](){ShowTab(Tab);},false,FName(*FString::Printf(TEXT("GrowthTab%d"),Tab)));
            TabButtons.Add(Button);Tabs->AddChildToHorizontalBox(Button)->SetPadding(FMargin(0,0,8,0));
        }
        Workspace->AddChildToVerticalBox(Tabs)->SetPadding(FMargin(0,0,0,8));
        auto* CurrentBuild=WidgetTree->ConstructWidget<UVerticalBox>();
        CurrentBuild->AddChildToVerticalBox(Text(TEXT("当前技能槽"),16))->SetPadding(FMargin(0,0,0,4));
        AttributeSummary=Wrapped(15,220);
        for(uint8 Key=0;Key<3;++Key)
        {
            auto* SkillRow=WidgetTree->ConstructWidget<UHorizontalBox>();
            SkillRow->AddChildToHorizontalBox(Text(LDHUD::SlotKey(Key),15))->SetPadding(FMargin(0,0,8,0));
            auto* Icon=WidgetTree->ConstructWidget<UImage>();auto* IconSize=WidgetTree->ConstructWidget<USizeBox>();
            IconSize->SetWidthOverride(32);IconSize->SetHeightOverride(32);IconSize->SetContent(Icon);
            SkillRow->AddChildToHorizontalBox(IconSize)->SetPadding(FMargin(0,0,8,0));EquippedIcons.Add(Icon);
            auto* Name=Wrapped(14,150);SkillRow->AddChildToHorizontalBox(Name);EquippedNames.Add(Name);
            CurrentBuild->AddChildToVerticalBox(SkillRow)->SetPadding(FMargin(0,0,0,4));
        }
        CurrentBuildSection=CurrentBuild;Root->AddChildToVerticalBox(CurrentBuildSection)->SetPadding(FMargin(0,0,0,8));
        PointsDock->AddChildToVerticalBox(AttributeSummary)->SetPadding(FMargin(0,12,0,0));
        Root->AddChildToVerticalBox(PointsDock);
        auto* DetailLines=WidgetTree->ConstructWidget<UVerticalBox>();
        auto FixedWrap=[](UTextBlock* Line){Line->SetAutoWrapText(false);Line->SetWrapTextAt(GrowthDetailWidth-28.f);return Line;};
        DetailName=FixedWrap(Text(TEXT(""),17));DetailLines->AddChildToVerticalBox(DetailName);
        DetailSummary=FixedWrap(Text(TEXT(""),15));DetailSummary->SetColorAndOpacity(LDHUDStyle::TextAccent);DetailLines->AddChildToVerticalBox(DetailSummary)->SetPadding(FMargin(0,2,0,0));
        DetailBody=LDWidgets::RichText(WidgetTree,16,TEXT("GrowthDetailBody"));DetailBody->SetAutoWrapText(false);DetailBody->SetWrapTextAt(GrowthDetailWidth-28.f);
        DetailLines->AddChildToVerticalBox(DetailBody)->SetPadding(FMargin(0,8,0,0));
        InspectState=Wrapped(16,GrowthDetailWidth-28);InspectState->SetColorAndOpacity(LDHUDStyle::TextWarning);
        DetailLines->AddChildToVerticalBox(InspectState)->SetPadding(FMargin(0,12,0,0));
        InspectInstall=LDWidgets::Button(WidgetTree,TEXT("安装构件"),[this]()
        {const int32 Index=ShownComponents.IndexOfByKey(InspectedComponent);if(Index!=INDEX_NONE){EnableComponent(Index);}},false,TEXT("GrowthInspectInstall"));
        DetailLines->AddChildToVerticalBox(InspectInstall)->SetPadding(FMargin(0,12,0,0));
        auto* DetailScroll=WidgetTree->ConstructWidget<UScrollBox>();DetailScroll->SetClipping(EWidgetClipping::ClipToBounds);DetailScroll->AddChild(DetailLines);
        auto* DetailWidth=WidgetTree->ConstructWidget<USizeBox>();DetailWidth->SetWidthOverride(GrowthDetailWidth);DetailWidth->SetContent(DetailScroll);
        Detail=WidgetTree->ConstructWidget<UBorder>();Detail->SetBrushColor(FLinearColor::Transparent);Detail->SetPadding(FMargin(0));Detail->SetContent(DetailWidth);
        auto* BodyContent=WidgetTree->ConstructWidget<UVerticalBox>();
        TaskBody=BodyContent;
        BodyContent->AddChildToVerticalBox(Root);
        BodyContent->AddChildToVerticalBox(Detail)->SetPadding(FMargin(0,12,0,0));
        Feedback=Wrapped(16,GrowthPanelWidth);Feedback->SetVisibility(ESlateVisibility::Collapsed);BodyContent->AddChildToVerticalBox(Feedback)->SetPadding(FMargin(0,12,0,0));
        auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),TEXT("GrowthTaskScroll"));Scroll->SetClipping(EWidgetClipping::ClipToBounds);Scroll->AddChild(BodyContent);
        auto* BodySize=WidgetTree->ConstructWidget<USizeBox>();BodySize->SetWidthOverride(GrowthPanelWidth);BodySize->SetHeightOverride(500);BodySize->SetContent(Scroll);
        Workspace->AddChildToVerticalBox(BodySize);
        MainPanel=LDWidgets::Panel(WidgetTree,Workspace,FMargin(20));MainPanel->SetBrushColor(LDHUDStyle::PanelFill.CopyWithNewOpacity(1.f));
        auto* ServiceContent=WidgetTree->ConstructWidget<UVerticalBox>();
        auto* ServiceHeader=WidgetTree->ConstructWidget<UHorizontalBox>();
        ServiceHeader->AddChildToHorizontalBox(Text(TEXT("NPC 服务"),22))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        ServiceHeader->AddChildToHorizontalBox(LDWidgets::Button(WidgetTree,TEXT("关闭"),[this](){ClosePanel();},false,TEXT("GrowthServiceClose")));
        ServiceContent->AddChildToVerticalBox(ServiceHeader)->SetPadding(FMargin(0,0,0,8));
        auto* ServiceScroll=WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),TEXT("GrowthServiceScroll"));ServiceScroll->SetClipping(EWidgetClipping::ClipToBounds);
        ServiceBody=WidgetTree->ConstructWidget<UVerticalBox>();ServiceBody->AddChildToVerticalBox(RestSection);ServiceBody->AddChildToVerticalBox(RestConfirmLine);
        ServiceScroll->AddChild(ServiceBody);
        auto* ServiceSize=WidgetTree->ConstructWidget<USizeBox>();ServiceSize->SetWidthOverride(GrowthPanelWidth);ServiceSize->SetHeightOverride(480);ServiceSize->SetContent(ServiceScroll);
        ServiceContent->AddChildToVerticalBox(ServiceSize);
        ServicePanel=LDWidgets::Panel(WidgetTree,ServiceContent,FMargin(20));ServicePanel->SetBrushColor(LDHUDStyle::PanelFill.CopyWithNewOpacity(1.f));
        ServicePanel->SetVisibility(ESlateVisibility::Collapsed);
        auto* Layers=WidgetTree->ConstructWidget<UOverlay>();
        Layers->AddChildToOverlay(MainPanel);
        Layers->AddChildToOverlay(ServicePanel);
        auto* Scale=WidgetTree->ConstructWidget<UScaleBox>();Scale->SetStretch(EStretch::ScaleToFit);Scale->SetStretchDirection(EStretchDirection::DownOnly);Scale->SetContent(Layers);
        auto* Frame=WidgetTree->ConstructWidget<UBorder>();Frame->SetBrushColor(FLinearColor::Transparent);Frame->SetPadding(FMargin(0,86,20,118));
        Frame->SetHorizontalAlignment(HAlign_Right);Frame->SetVerticalAlignment(VAlign_Top);Frame->SetVisibility(ESlateVisibility::SelfHitTestInvisible);Frame->SetContent(Scale);
        WidgetTree->RootWidget=Frame;
        SetVisibility(ESlateVisibility::Collapsed);
    }
    return Super::RebuildWidget();
}
void ULDChoicesPanel::NativeTick(const FGeometry& Geometry,float Delta)
{
    Super::NativeTick(Geometry,Delta);
    if(Feedback){GrowthShown(Feedback,!Feedback->GetText().IsEmpty(),ESlateVisibility::HitTestInvisible);}
    UpdateDetail();
}
void ULDChoicesPanel::SetFeedback(const FString& Text){GrowthText(Feedback,Text);}
void ULDChoicesPanel::UpdateDetail()
{
    if(!Detail){return;}
    int32 Next=INDEX_NONE;
    if(bOpen&&ActiveTab==0&&ShownOffer.IsValid())
    {
        for(int32 Index=0;Index<Cards.Num()&&Next==INDEX_NONE;++Index)
        {if(Cards[Index].Button->IsVisible()&&(Cards[Index].Button->IsHovered()||Cards[Index].Button->HasKeyboardFocus())){Next=Index;}}
        if(Next==INDEX_NONE&&Cards.IsValidIndex(PinnedPreview)){Next=PinnedPreview;}
    }
    FString Name=TEXT("构筑详情"),Summary=TEXT("悬停查看 · 点击构件图标固定"),Body=TEXT("在技能与图纸查看技能，在加点查看属性与每点收益；选择背包构件后安装。所有改动由服务器确认。");
    FString State;
    DetailCard=Next;InspectedComponent=NAME_None;
    if(Next!=INDEX_NONE&&CardViews.IsValidIndex(Next))
    {
        const LDHUD::FActiveCardView& View=CardViews[Next];
        Name=View.Name;Summary=View.Target+TEXT(" · ")+View.Cost;
        Body=View.Sentence+TEXT("\n\n")+FString::Join(View.Keywords,TEXT("\n"));
    }
    FName Component=ActiveTab==1?PinnedComponent:NAME_None;
    if(ActiveTab==1)
    {
        for(const FComponentRow& Row:ComponentRows)
        {if(Row.Inspect&&Row.Inspect->IsVisible()&&(Row.Inspect->IsHovered()||Row.Inspect->HasKeyboardFocus())){Component=Row.ShownId;break;}}
    }
    const FLDComponentInstanceView* Owned=Latest.Components.FindByPredicate([Component](const FLDComponentInstanceView& Row){return Row.DefinitionId==Component;});
    bool bInstall=false;
    if(Owned)
    {
        InspectedComponent=Component;
        const LDHUD::FComponentCardView Card=LDHUD::ComponentCard(Component,LatestContent,LatestStats);
        const LDHUD::FComponentRowView Row=LDHUD::ComponentRow(*Owned,Latest,LatestContent,Gate,LatestSafeAtMs,bLatestClock,LatestNowMs);
        Name=Card.Name;Summary=LDHUD::InstalledTargetText(*Owned,LatestContent);
        Body=Card.Sentence;State=Row.bInstalled?TEXT("已安装"):Row.bCanEnable?TEXT("可安装"):TEXT("不可安装：")+Row.Blocked;
        bInstall=!Row.bInstalled;
        GrowthEnabled(InspectInstall,Row.bCanEnable&&!PendingRequest);
    }
    if(ActiveTab==0&&!Owned)
    {
        auto PreviewBase=[&](const FBaseCard& Card)
        {
            if(!Card.Button||!Card.Button->IsVisible()||(!Card.Button->IsHovered()&&!Card.Button->HasKeyboardFocus())){return;}
            const LDHUD::FComponentCardView View=LDHUD::ComponentCard(Card.ShownId,LatestContent,LatestStats);
            Name=View.Name;Summary=TEXT("候选构件");Body=View.Sentence;
        };
        if(ActiveTab==0&&Latest.bOpeningBasePending){for(const FBaseCard& Card:BaseCards){PreviewBase(Card);}}
        for(int32 Index=0;ActiveTab==0&&Index<ShownChoices.Num()&&Index<ChoiceLines.Num();++Index)
        {for(const FBaseCard& Card:ChoiceLines[Index].Cards){PreviewBase(Card);}}
        for(int32 Index=0;Index<BlueprintRows.Num()&&Index<ShownBlueprints.Num();++Index)
        {
            const FBlueprintRow& Row=BlueprintRows[Index];
            if(!Row.Row->IsHovered()){continue;}
            const LDHUD::FActiveCardView View=LDHUD::ActiveCard(ShownBlueprints[Index],LatestContent,LatestStats);
            Name=View.Name;Summary=TEXT("待学主动图纸 · 不是整套构筑蓝图");Body=View.Sentence;
        }
    }
    GrowthText(DetailName,Name);GrowthText(DetailSummary,Summary);GrowthText(InspectState,State);
    const FText Text=FText::FromString(Body);if(!DetailBody->GetText().EqualTo(Text)){DetailBody->SetText(Text);}
    GrowthShown(InspectInstall,bInstall,ESlateVisibility::Visible);
}
void ULDChoicesPanel::ShowTab(int32 Tab)
{
    ActiveTab=FMath::Clamp(Tab,0,2);RefreshAll();UpdateDetail();
}
void ULDChoicesPanel::SetServiceContext(ELDRestService Service)
{
    if(ServiceContext==Service){return;}
    const ELDRestService Previous=ServiceContext;
    ServiceContext=Service;
    CancelRestAction();CloseMaterialPickers();
    if(bServicePanelOpen)
    {
        ClosePanel();
        const FString Message=Service==ELDRestService::None?TEXT("已离开 NPC，服务已收起。"):TEXT("服务对象已变化，请重新与 NPC 交互。");
        if(Notify){Notify(Message);}
    }
    else if(Previous!=ELDRestService::None&&Service==ELDRestService::None&&Notify)
    {Notify(TEXT("已离开 NPC，服务已收起。"));}
    RefreshAll();
}
bool ULDChoicesPanel::PreviewComponent(int32 Index)
{
    if(!ShownComponents.IsValidIndex(Index)){return false;}
    PinnedComponent=ShownComponents[Index];ActiveTab=1;RefreshAll();UpdateDetail();return true;
}
void ULDChoicesPanel::PresentAttributes(const FLDCombatOwnerView* Combat)
{
    if(!AttributeSummary){return;}
    TArray<FString> Lines;
    if(Combat){for(const FLDCombatAttributeRowView& Row:Combat->Attributes)
    {
        const LDHUD::FAttributeRowView View=LDHUD::AttributeRow(Row);
        Lines.Add(View.Name+TEXT("  ")+View.Final);
    }}
    GrowthText(AttributeSummary,Lines.IsEmpty()?TEXT("属性同步中"):FString::Join(Lines,TEXT("\n")));
}
void ULDChoicesPanel::PresentRestActions(bool bRest,bool bCanReset,bool bCanReady,const FString& ReadyLabel)
{
    GrowthShown(RestTrainingButton,bRest,ESlateVisibility::Visible);
    GrowthShown(RestReadyButton,bRest,ESlateVisibility::Visible);
    GrowthEnabled(RestTrainingButton,bCanReset);
    GrowthEnabled(RestReadyButton,bCanReady);
    if(RestReadyButton){GrowthText(Cast<UTextBlock>(RestReadyButton->GetContent()),ReadyLabel);}
}
UWidget* ULDChoicesPanel::FocusScope() const{return bServicePanelOpen?static_cast<UWidget*>(ServicePanel.Get()):static_cast<UWidget*>(MainPanel.Get());}
UWidget* ULDChoicesPanel::HitArea() const{return bServicePanelOpen?static_cast<UWidget*>(ServicePanel.Get()):static_cast<UWidget*>(MainPanel.Get());}
void ULDChoicesPanel::OpenPanel()
{
    if(TaskBody&&ServiceBody&&Feedback&&Feedback->GetParent()==ServiceBody)
    {ServiceBody->RemoveChild(Feedback);TaskBody->AddChildToVerticalBox(Feedback)->SetPadding(FMargin(0,12,0,0));}
    if(const auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());PC&&PC->BuildBlueprints&&PC->BuildBlueprints->IsViewerOpen())
    {PC->BuildBlueprints->ToggleViewer();}
    if(ServiceBody&&BlueprintSection&&BlueprintSection->GetParent()==ServiceBody)
    {ServiceBody->RemoveChild(BlueprintSection);TaskContent->InsertChildAt(6,BlueprintSection);}
    bServicePanelOpen=false;GrowthShown(ServicePanel,false);GrowthShown(MainPanel,true);
    bOpen=true;ResultNotice.Reset();SetVisibility(ESlateVisibility::SelfHitTestInvisible);Fold.Opened(false,UiNow());
    if(!PendingRequest){SetFeedback(FString());}
}
bool ULDChoicesPanel::OpenServicePanel()
{
    if(ServiceContext==ELDRestService::None){return false;}
    if(TaskBody&&ServiceBody&&Feedback&&Feedback->GetParent()==TaskBody)
    {TaskBody->RemoveChild(Feedback);ServiceBody->AddChildToVerticalBox(Feedback)->SetPadding(FMargin(0,12,0,0));}
    if(ServiceContext==ELDRestService::Trainer&&TaskContent&&ServiceBody&&BlueprintSection
        &&BlueprintSection->GetParent()==TaskContent)
    {TaskContent->RemoveChild(BlueprintSection);ServiceBody->AddChildToVerticalBox(BlueprintSection);}
    bOpen=false;Fold.Closed();bServicePanelOpen=true;ResultNotice.Reset();
    GrowthShown(MainPanel,false);GrowthShown(ServicePanel,true);
    SetVisibility(ESlateVisibility::SelfHitTestInvisible);RefreshAll();return true;
}
bool ULDChoicesPanel::ClosePanel()
{
    if(TaskBody&&ServiceBody&&Feedback&&Feedback->GetParent()==ServiceBody)
    {ServiceBody->RemoveChild(Feedback);TaskBody->AddChildToVerticalBox(Feedback)->SetPadding(FMargin(0,12,0,0));}
    // Closing only hides the panel: the offer, the blueprints, the staged points and any submitted request keep their state;
    // an unconfirmed replace, forget, fill gap or dismantle is withdrawn with its pickers.
    RestConfirm=FRestConfirm();GrowthShown(RestConfirmLine,false);CloseMaterialPickers();
    if(ServiceBody&&BlueprintSection&&BlueprintSection->GetParent()==ServiceBody)
    {ServiceBody->RemoveChild(BlueprintSection);TaskContent->InsertChildAt(6,BlueprintSection);}
    PinnedPreview=INDEX_NONE;PinnedComponent=NAME_None;ResultNotice.Reset();bOpen=false;bServicePanelOpen=false;
    GrowthShown(MainPanel,false);GrowthShown(ServicePanel,false);SetVisibility(ESlateVisibility::Collapsed);Fold.Closed();UpdateDetail();return true;
}
double ULDChoicesPanel::UiNow() const{const UWorld* World=GetWorld();return World?World->GetRealTimeSeconds():0.;}
void ULDChoicesPanel::MarkOpenedAutomatically()
{
    if(!bOpen){return;}
    Fold.Opened(true,UiNow());
    UE_LOG(LogLDUI,Log,TEXT("LD_HUD_GROWTH_AUTO_OPEN The growth panel opened for a new active offer, new points, the opening base pick or a new component."));
}
void ULDChoicesPanel::UpdateFold(bool bOwnHit,bool bCombatActive)
{
    if(bOpen&&Fold.ShouldFold(UiNow(),IsEngaged()||(MainPanel&&MainPanel->IsHovered()),bOwnHit,bCombatActive))
    {
        UE_LOG(LogLDUI,Log,TEXT("LD_HUD_CHOICES_FOLDED %s"),bCombatActive?TEXT("combat priority"):bOwnHit?TEXT("own hit"):TEXT("automatic open timed out"));
        ClosePanel();
    }
}
bool ULDChoicesPanel::Usable() const
{
    return Latest.bValid&&LatestMatch!=0&&Latest.Generation==LatestMatch&&Latest.Life==LatestLife;
}
int32 ULDChoicesPanel::StagedTotal() const
{
    int32 Total=0;for(const int32 Count:Staged){Total+=Count;}return Total;
}
void ULDChoicesPanel::Present(const FLDProgressionView& View,uint32 Match,uint32 Life,ELDLifePhase LifePhase,LDProgression::EMatchStage Stage,int64 BuildSafeAtMs,
    bool bClock,int64 NowMs,const FLDDemoContentDefinitions* Content,const LDHUD::FCardStats& Stats)
{
    const bool bStatsChanged=Stats.Level!=LatestStats.Level||Stats.ADP2!=LatestStats.ADP2||Stats.APP2!=LatestStats.APP2
        ||Stats.MaxHPP2!=LatestStats.MaxHPP2||Stats.ArmorP2!=LatestStats.ArmorP2||Content!=LatestContent;
    Latest=View;LatestMatch=Match;LatestLife=Life;LatestLifePhase=LifePhase;LatestStage=Stage;LatestSafeAtMs=BuildSafeAtMs;
    bLatestClock=bClock;LatestNowMs=NowMs;LatestContent=Content;LatestStats=Stats;
    if(!MainPanel){return;}
    if(StagedMatch!=Match){StagedMatch=Match;Staged.Reset();}
    Gate=Usable()?LDHUD::BuildGate(Stage,LifePhase,BuildSafeAtMs,bClock,NowMs):LDHUD::FBuildGateView();
    if(Content&&StatRows.IsEmpty()){BuildStatRows();}
    if(bStatsChanged){bCardsStale=true;bComponentTextStale=true;}
    RefreshAll();
}
void ULDChoicesPanel::RefreshOffer()
{
    const bool bUsable=Usable();
    const bool bOffer=bUsable&&Latest.bHasActiveOffer&&!Latest.ActiveOffer.Options.IsEmpty();
    const int32 Queued=bUsable?int32(Latest.QueuedActiveOpportunities):0;
    GrowthShown(OfferSection,!bUsable||bOffer||Queued>0);
    GrowthText(OfferHeading,bUsable?(Queued>0?FString::Printf(TEXT("选一个技能 · %s"),*LDTerms::OffersQueued(Queued)):FString(TEXT("选一个技能")))
        :FString(TEXT("等待成长数据同步。")));
    const bool bNewOffer=!bOffer||Latest.ActiveOffer.OfferId!=ShownOffer;
    if(bNewOffer||bCardsStale||Latest.ActiveOffer.OfferRevision!=ShownOfferRevision)
    {
        // A new offer (or new numbers) replaces the cards; ids and revisions travel with the click, never a row index.
        if(bNewOffer){PinnedPreview=INDEX_NONE;}
        ShownOffer=bOffer?Latest.ActiveOffer.OfferId:FGuid();ShownOfferRevision=bOffer?Latest.ActiveOffer.OfferRevision:0;
        bCardsStale=false;
        for(int32 Index=0;Index<Cards.Num();++Index)
        {
            FCardWidgets& Card=Cards[Index];
            const bool bShown=bOffer&&Latest.ActiveOffer.Options.IsValidIndex(Index);
            GrowthShown(Card.Button,bShown,ESlateVisibility::Visible);
            if(!bShown){CardViews[Index]=LDHUD::FActiveCardView();continue;}
            const LDHUD::FActiveCardView Shown=LDHUD::ActiveCard(Latest.ActiveOffer.Options[Index],LatestContent,LatestStats);
            CardViews[Index]=Shown;
            GrowthText(Card.Name,Shown.Name);GrowthText(Card.Glyph,Shown.Glyph);
            GrowthText(Card.Summary,Shown.bKnown?Shown.Target+TEXT(" · ")+Shown.Cost:FString());
            const bool bPicture=LDUIIcons::Apply(Card.Picture,Shown.IconId,GrowthIconRadius);
            Card.IconSize->SetVisibility(bPicture?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
            Card.Glyph->SetVisibility(bPicture?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
        }
    }
    // What a click does now: learn into the lowest empty slot, or keep it as a blueprint and say why (the server decides).
    uint8 FirstEmpty=3;
    for(uint8 Key=0;Key<3&&FirstEmpty==3;++Key){if(LDHUD::ActiveInSlot(Latest,Key).IsNone()){FirstEmpty=Key;}}
    const FString StoreWhy=FirstEmpty==3?FString(LDTerms::SlotsFull):Gate.LearnBlocked;
    const FString Action=!Gate.bCanChoose?Gate.ChooseBlocked
        :Gate.bCanLearn&&FirstEmpty<3?FString(LDTerms::LearnThis)+TEXT(" → ")+LDHUD::SlotKey(FirstEmpty)
        :FString(LDTerms::LearnThis)+TEXT("（先存为")+LDTerms::Blueprint+TEXT("：")+StoreWhy+TEXT("）");
    for(int32 Index=0;Index<Cards.Num();++Index)
    {
        GrowthText(Cards[Index].Action,Action);
        GrowthEnabled(Cards[Index].Button,Gate.bCanChoose&&!PendingRequest);
    }
    GrowthText(OfferNote,bUsable&&!bOffer&&Queued>0?FString(LDTerms::OfferWaitingEligible):FString());
    GrowthShown(OfferNote,!OfferNote->GetText().IsEmpty(),ESlateVisibility::HitTestInvisible);
}
ULDChoicesPanel::FBlueprintRow& ULDChoicesPanel::BlueprintRowAt(int32 Index)
{
    while(BlueprintRows.Num()<=Index)
    {
        const int32 RowIndex=BlueprintRows.Num();
        FBlueprintRow& Row=BlueprintRows.AddDefaulted_GetRef();
        auto MakeText=[this](int32 Size,const FLinearColor& Ink)
        {UTextBlock* Line=LDWidgets::Text(WidgetTree,TEXT(""),Size);Line->SetAutoWrapText(false);Line->SetColorAndOpacity(Ink);ScaleNewText(Line);RowWidgets.Add(Line);return Line;};
        auto* Group=WidgetTree->ConstructWidget<UVerticalBox>();RowWidgets.Add(Group);
        auto* Line=WidgetTree->ConstructWidget<UHorizontalBox>();RowWidgets.Add(Line);
        Group->AddChildToVerticalBox(Line);
        Row.Picture=WidgetTree->ConstructWidget<UImage>();RowWidgets.Add(Row.Picture);
        auto* IconSize=WidgetTree->ConstructWidget<USizeBox>();IconSize->SetWidthOverride(36);IconSize->SetHeightOverride(36);IconSize->SetContent(Row.Picture);
        RowWidgets.Add(IconSize);Line->AddChildToHorizontalBox(IconSize)->SetPadding(FMargin(0,0,8,0));
        auto* Lines=WidgetTree->ConstructWidget<UVerticalBox>();RowWidgets.Add(Lines);
        Row.Name=MakeText(15,LDHUDStyle::TextPrimary);Row.Name->SetWrapTextAt(GrowthPanelWidth-70.f);Lines->AddChildToVerticalBox(Row.Name);
        Row.Summary=MakeText(15,LDHUDStyle::TextSecondary);Row.Summary->SetWrapTextAt(GrowthPanelWidth-70.f);Lines->AddChildToVerticalBox(Row.Summary);
        auto* LinesSlot=Line->AddChildToHorizontalBox(Lines);LinesSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));LinesSlot->SetVerticalAlignment(VAlign_Center);
        auto* Actions=WidgetTree->ConstructWidget<UWrapBox>();RowWidgets.Add(Actions);
        Actions->SetExplicitWrapSize(true);Actions->SetWrapSize(GrowthPanelWidth-24.f);
        Actions->SetInnerSlotPadding(FVector2D(6,4));
        Group->AddChildToVerticalBox(Actions)->SetPadding(FMargin(0,4,0,0));
        for(uint8 Key=0;Key<3;++Key)
        {
            ULDCommandButton* Learn=LDWidgets::Button(WidgetTree,LDTerms::LearnInto(LDHUD::SlotKey(Key)),[this,RowIndex,Key](){LearnBlueprint(RowIndex,Key);},false,
                FName(*FString::Printf(TEXT("GrowthLearn%d%s"),RowIndex,*LDHUD::SlotKey(Key))));
            ScaleNewText(Cast<UTextBlock>(Learn->GetContent()));RowWidgets.Add(Learn);
            Actions->AddChildToWrapBox(Learn);
            Row.Learn.Add(Learn);
        }
        for(uint8 Key=0;Key<3;++Key)
        {
            // #198 rest only: the blueprint replaces the active in this occupied slot, after the confirm line.
            ULDCommandButton* Replace=LDWidgets::Button(WidgetTree,LDTerms::ReplaceInto(LDHUD::SlotKey(Key)),[this,RowIndex,Key](){AskReplace(RowIndex,Key);},false,
                FName(*FString::Printf(TEXT("GrowthReplace%d%s"),RowIndex,*LDHUD::SlotKey(Key))));
            ScaleNewText(Cast<UTextBlock>(Replace->GetContent()));RowWidgets.Add(Replace);
            Actions->AddChildToWrapBox(Replace);
            Row.Replace.Add(Replace);
        }
        Row.Blocked=MakeText(13,LDHUDStyle::TextSecondary);Row.Blocked->SetWrapTextAt(GrowthPanelWidth-24.f);
        Group->AddChildToVerticalBox(Row.Blocked)->SetPadding(FMargin(0,4,0,0));
        Row.Row=Group;
        BlueprintList->AddChildToVerticalBox(Group)->SetPadding(FMargin(0,0,0,8));
    }
    return BlueprintRows[Index];
}
void ULDChoicesPanel::RefreshBlueprints()
{
    const bool bUsable=Usable();
    ShownBlueprints=bUsable?Latest.PendingBlueprints:TArray<FName>();
    GrowthShown(BlueprintSection,!ShownBlueprints.IsEmpty());
    bool bEmpty[3]={false,false,false};
    for(uint8 Key=0;Key<3;++Key){bEmpty[Key]=LDHUD::ActiveInSlot(Latest,Key).IsNone();}
    const bool bAnyEmpty=bEmpty[0]||bEmpty[1]||bEmpty[2];
    // A full Q/W/E: in a rest the blueprint replaces an occupied slot (after a confirm), otherwise it waits for a rest.
    const FString ReplaceWhy=Gate.bInRest?ReplaceRefusal():FString();
    const FString Blocked=!bAnyEmpty?(Gate.bInRest?ReplaceWhy:FString(LDTerms::ReplaceAtRest)):Gate.bCanLearn?FString():Gate.LearnBlocked;
    for(int32 Index=0;Index<FMath::Max(ShownBlueprints.Num(),BlueprintRows.Num());++Index)
    {
        if(!ShownBlueprints.IsValidIndex(Index)){GrowthShown(BlueprintRows[Index].Row,false);continue;}
        FBlueprintRow& Row=BlueprintRowAt(Index);
        GrowthShown(Row.Row,true,ESlateVisibility::Visible);
        const LDHUD::FActiveCardView View=LDHUD::ActiveCard(ShownBlueprints[Index],LatestContent,LatestStats);
        LDUIIcons::Apply(Row.Picture,View.IconId,0);
        GrowthText(Row.Name,View.Name);
        GrowthText(Row.Summary,View.bKnown?View.Target+TEXT(" · ")+View.Cost:View.SentencePlain);
        if(!Row.Row->GetToolTipText().ToString().Equals(View.SentencePlain,ESearchCase::CaseSensitive)){Row.Row->SetToolTipText(FText::FromString(View.SentencePlain));}
        for(uint8 Key=0;Key<3;++Key)
        {
            GrowthShown(Row.Learn[Key],bEmpty[Key],ESlateVisibility::Visible);
            GrowthEnabled(Row.Learn[Key],Gate.bCanLearn&&!PendingRequest);
            GrowthShown(Row.Replace[Key],bServicePanelOpen&&Gate.bInRest&&ServiceContext==ELDRestService::Trainer&&!bEmpty[Key],ESlateVisibility::Visible);
            GrowthEnabled(Row.Replace[Key],ReplaceWhy.IsEmpty()&&!PendingRequest);
        }
        GrowthText(Row.Blocked,Blocked);
        GrowthShown(Row.Blocked,!Blocked.IsEmpty(),ESlateVisibility::HitTestInvisible);
    }
}
void ULDChoicesPanel::BuildStatRows()
{
    // Rows are built once from the catalog this process plays; a hot reload changes numbers only, never the ids.
    if(!LatestContent||!StatList){return;}
    auto MakeText=[this](int32 Size,const FLinearColor& Ink,bool bWrap)
    {
        UTextBlock* Line=LDWidgets::Text(WidgetTree,TEXT(""),Size);Line->SetAutoWrapText(false);Line->SetColorAndOpacity(Ink);
        if(bWrap){Line->SetWrapTextAt(GrowthPanelWidth-300.f);}
        ScaleNewText(Line);RowWidgets.Add(Line);return Line;
    };
    const TArray<const FLDDemoStatDefinition*> StatOrder=LatestContent->StatsInDesignOrder();
    for(int32 Index=0;Index<StatOrder.Num();++Index)
    {
        FStatRow& Row=StatRows.AddDefaulted_GetRef();
        auto* Line=WidgetTree->ConstructWidget<UVerticalBox>();RowWidgets.Add(Line);
        auto* Controls=WidgetTree->ConstructWidget<UHorizontalBox>();RowWidgets.Add(Controls);
        Row.Name=MakeText(15,LDHUDStyle::TextPrimary,false);
        auto* NameBox=WidgetTree->ConstructWidget<USizeBox>();NameBox->SetWidthOverride(90.f);NameBox->SetContent(Row.Name);RowWidgets.Add(NameBox);
        Controls->AddChildToHorizontalBox(NameBox)->SetVerticalAlignment(VAlign_Center);
        Row.Points=MakeText(15,LDHUDStyle::TextAccent,false);
        auto* PointsBox=WidgetTree->ConstructWidget<USizeBox>();PointsBox->SetWidthOverride(110.f);PointsBox->SetContent(Row.Points);RowWidgets.Add(PointsBox);
        Controls->AddChildToHorizontalBox(PointsBox)->SetVerticalAlignment(VAlign_Center);
        const FString Id=StatOrder[Index]->Id;
        Row.Minus=LDWidgets::Button(WidgetTree,TEXT("−"),[this,Index](){StagePoint(Index,-1);},false,FName(*(TEXT("GrowthPointMinus_")+Id)));
        Row.Plus=LDWidgets::Button(WidgetTree,TEXT("+"),[this,Index](){StagePoint(Index,1);},false,FName(*(TEXT("GrowthPointPlus_")+Id)));
        for(ULDCommandButton* Button:{Row.Minus.Get(),Row.Plus.Get()})
        {ScaleNewText(Cast<UTextBlock>(Button->GetContent()));RowWidgets.Add(Button);Controls->AddChildToHorizontalBox(Button)->SetPadding(FMargin(6,0,0,0));}
        Line->AddChildToVerticalBox(Controls);
        Row.PerPoint=MakeText(16,LDHUDStyle::TextSecondary,true);Row.PerPoint->SetWrapTextAt(GrowthPanelWidth-20.f);
        Line->AddChildToVerticalBox(Row.PerPoint)->SetPadding(FMargin(0,2,0,0));
        Row.Row=Line;
        StatList->AddChildToVerticalBox(Line)->SetPadding(FMargin(0,0,0,3));
    }
}
void ULDChoicesPanel::RefreshPoints()
{
    const bool bUsable=Usable();
    const int64 Unspent=bUsable?FMath::Max<int64>(0,Latest.UnspentPoints):0;
    const int32 StatCount=LatestContent?LatestContent->Stats.Num():0;
    Staged.SetNumZeroed(StatCount);
    // Staged points never exceed what the latest view leaves unspent: the excess comes off the last staged stats.
    for(int32 Index=Staged.Num()-1;Index>=0&&StagedTotal()>Unspent;--Index)
    {Staged[Index]=FMath::Max(0,Staged[Index]-int32(StagedTotal()-Unspent));}
    GrowthShown(PointsSection,bUsable&&(Unspent>0||StagedTotal()>0));
    GrowthText(PointsHeading,LDTerms::UnspentPoints(Unspent-StagedTotal()));
    const TArray<const FLDDemoStatDefinition*> StatOrder=LatestContent?LatestContent->StatsInDesignOrder():TArray<const FLDDemoStatDefinition*>();
    for(int32 Index=0;Index<StatRows.Num()&&Index<StatCount;++Index)
    {
        FStatRow& Row=StatRows[Index];
        const FLDDemoStatDefinition& Stat=*StatOrder[Index];
        const LDHUD::FStatRowView View=LDHUD::StatRow(Stat,LDHUD::StatPointsOf(Latest,FName(*Stat.Id)),LatestContent);
        GrowthText(Row.Name,View.Name);GrowthText(Row.PerPoint,View.PerPoint);Row.Name->SetToolTipText(FText::FromString(View.PerPoint));
        GrowthText(Row.Points,Staged[Index]>0?FString::Printf(TEXT("%d 点 +%d"),View.Points,Staged[Index]):FString::Printf(TEXT("%d 点"),View.Points));
        GrowthEnabled(Row.Minus,Staged[Index]>0);
        GrowthEnabled(Row.Plus,StagedTotal()<Unspent&&Staged[Index]<MAX_uint8);
    }
    const FString Blocked=Gate.bCanAllocate?FString():Gate.AllocateBlocked;
    GrowthText(PointsBlocked,Blocked);
    GrowthEnabled(ConfirmButton,Gate.bCanAllocate&&StagedTotal()>0&&!PendingRequest);
}
void ULDChoicesPanel::RefreshAll()
{
    RefreshOffer();RefreshOpeningBase();RefreshComponentChoices();RefreshBinding();RefreshTarget();RefreshBlueprints();RefreshComponents();RefreshPoints();RefreshRest();
    GrowthShown(CurrentBuildSection,ActiveTab==0);
    GrowthShown(OfferSection,ActiveTab==0&&(!Usable()||Latest.bHasActiveOffer||Latest.QueuedActiveOpportunities>0));
    GrowthShown(OpeningBaseSection,ActiveTab==0&&Usable()&&Latest.bOpeningBasePending&&!ShownBaseOptions.IsEmpty());
    GrowthShown(ComponentChoiceSection,ActiveTab==1&&!ShownChoices.IsEmpty());
    GrowthShown(BlueprintSection,bServicePanelOpen?ServiceContext==ELDRestService::Trainer&&!ShownBlueprints.IsEmpty()
        :ActiveTab==0&&!ShownBlueprints.IsEmpty());
    GrowthShown(ComponentSection,ActiveTab==1);
    GrowthShown(PointsDock,ActiveTab==2);
    GrowthText(EmptyNote,!Usable()?TEXT("构筑数据同步中…"):TEXT("目前没有待选技能或图纸。"));
    GrowthShown(EmptyNote,ActiveTab==0&&!Latest.bHasActiveOffer&&!Latest.bOpeningBasePending&&ShownChoices.IsEmpty()&&ShownBlueprints.IsEmpty());
    for(int32 Index=0;Index<TabButtons.Num();++Index)
    {GrowthTint(TabButtons[Index],Index==ActiveTab?FLinearColor(.14f,.25f,.28f):GrowthCardIdle);}
    for(uint8 Key=0;Key<3&&Key<EquippedNames.Num();++Key)
    {
        const FName Id=LDHUD::ActiveInSlot(Latest,Key);
        const LDHUD::FActiveCardView Card=Id.IsNone()?LDHUD::FActiveCardView():LDHUD::ActiveCard(Id,LatestContent,LatestStats);
        GrowthText(EquippedNames[Key],Id.IsNone()?TEXT("空槽"):Card.Name);
        LDUIIcons::Apply(EquippedIcons[Key],Card.IconId,0);
    }
    bComponentTextStale=false;
}
ULDChoicesPanel::FBaseCard& ULDChoicesPanel::BaseCardAt(int32 Index)
{
    while(BaseCards.Num()<=Index)
    {
        const int32 CardIndex=BaseCards.Num();
        if(CardIndex%3==0)
        {
            auto* Line=WidgetTree->ConstructWidget<UHorizontalBox>();RowWidgets.Add(Line);BaseCardLines.Add(Line);
            BaseCardList->AddChildToVerticalBox(Line)->SetPadding(FMargin(0,CardIndex?6.f:0.f,0,0));
        }
        FBaseCard& Card=BaseCards.AddDefaulted_GetRef();
        BuildComponentCard(Card,FName(*FString::Printf(TEXT("GrowthBase%d"),CardIndex)),[this,CardIndex](){PickOpeningBase(CardIndex);});
        BaseCardLines.Last()->AddChildToHorizontalBox(Card.Frame)->SetPadding(FMargin(CardIndex%3?8.f:0.f,0,0,0));
    }
    return BaseCards[Index];
}
void ULDChoicesPanel::BuildComponentCard(FBaseCard& Card,FName ButtonName,TFunction<void()> Click)
{
    auto MakeText=[this](int32 Size,const FLinearColor& Ink)
    {UTextBlock* Line=LDWidgets::Text(WidgetTree,TEXT(""),Size);Line->SetAutoWrapText(false);Line->SetColorAndOpacity(Ink);ScaleNewText(Line);RowWidgets.Add(Line);return Line;};
    auto* Face=WidgetTree->ConstructWidget<UHorizontalBox>();RowWidgets.Add(Face);
    auto* IconBox=WidgetTree->ConstructWidget<UOverlay>();RowWidgets.Add(IconBox);
    Card.Picture=WidgetTree->ConstructWidget<UImage>();Card.Picture->SetVisibility(ESlateVisibility::Collapsed);RowWidgets.Add(Card.Picture);
    auto* PictureSlot=IconBox->AddChildToOverlay(Card.Picture);PictureSlot->SetHorizontalAlignment(HAlign_Fill);PictureSlot->SetVerticalAlignment(VAlign_Fill);
    Card.Glyph=MakeText(18,LDHUDStyle::TextPrimary);Card.Glyph->SetJustification(ETextJustify::Center);
    auto* GlyphSlot=IconBox->AddChildToOverlay(Card.Glyph);GlyphSlot->SetHorizontalAlignment(HAlign_Center);GlyphSlot->SetVerticalAlignment(VAlign_Center);
    auto* IconFrame=WidgetTree->ConstructWidget<USizeBox>();IconFrame->SetWidthOverride(GrowthIconSize);IconFrame->SetHeightOverride(GrowthIconSize);
    IconFrame->SetContent(IconBox);RowWidgets.Add(IconFrame);
    auto* IconSlot=Face->AddChildToHorizontalBox(IconFrame);IconSlot->SetVerticalAlignment(VAlign_Top);IconSlot->SetPadding(FMargin(0,0,10,0));
    auto* Lines=WidgetTree->ConstructWidget<UVerticalBox>();RowWidgets.Add(Lines);
    Card.Name=MakeText(17,LDHUDStyle::TextPrimary);Card.Name->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);Lines->AddChildToVerticalBox(Card.Name);
    Card.Action=MakeText(16,LDHUDStyle::TextAccent);Card.Action->SetWrapTextAt(GrowthCardWidth-GrowthIconSize-40.f);
    Lines->AddChildToVerticalBox(Card.Action)->SetPadding(FMargin(0,4,0,0));
    auto* LinesSlot=Face->AddChildToHorizontalBox(Lines);LinesSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));LinesSlot->SetVerticalAlignment(VAlign_Center);
    Card.Button=LDWidgets::Button(WidgetTree,TEXT(""),MoveTemp(Click),false,ButtonName);
    RowWidgets.Add(Card.Button);
    Card.Button->SetContent(Face);GrowthTint(Card.Button,GrowthCardIdle);
    if(UButtonSlot* FaceSlot=Cast<UButtonSlot>(Card.Button->GetContentSlot())){FaceSlot->SetHorizontalAlignment(HAlign_Fill);FaceSlot->SetVerticalAlignment(VAlign_Center);}
    auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetWidthOverride(GrowthCardWidth);Size->SetMinDesiredHeight(GrowthCardHeight);Size->SetContent(Card.Button);
    RowWidgets.Add(Size);Card.Frame=Size;
}
void ULDChoicesPanel::ShowComponentCard(FBaseCard& Card,FName Id)
{
    // The face is rendered again only for another option or new numbers (level, stats or catalog).
    if(Card.ShownId==Id&&!bComponentTextStale){return;}
    const LDHUD::FComponentCardView View=LDHUD::ComponentCard(Id,LatestContent,LatestStats);
    Card.ShownId=Id;Card.bBinding=View.bBinding;
    GrowthText(Card.Name,View.Name);GrowthText(Card.Glyph,View.Glyph);
    if(Card.AppliedIcon!=View.IconId||View.IconId.IsEmpty())
    {
        Card.AppliedIcon=View.IconId;
        GrowthShown(Card.Glyph,!LDUIIcons::Apply(Card.Picture,View.IconId,GrowthIconRadius),ESlateVisibility::HitTestInvisible);
    }
}
void ULDChoicesPanel::RefreshOpeningBase()
{
    const bool bShow=Usable()&&Latest.bOpeningBasePending&&!Latest.OpeningBaseOptions.IsEmpty();
    ShownBaseOptions=bShow?Latest.OpeningBaseOptions:TArray<FName>();
    GrowthShown(OpeningBaseSection,bShow);
    for(int32 Index=0;Index<FMath::Max(ShownBaseOptions.Num(),BaseCards.Num());++Index)
    {
        if(!ShownBaseOptions.IsValidIndex(Index)){GrowthShown(BaseCards[Index].Frame,false);continue;}
        FBaseCard& Card=BaseCardAt(Index);
        GrowthShown(Card.Frame,true,ESlateVisibility::SelfHitTestInvisible);
        const FName Id=ShownBaseOptions[Index];
        ShowComponentCard(Card,Id);
        // What a click does now: take it and enable it at once, or keep it in the inventory and say why (the server decides).
        FLDComponentInstanceView Fresh;Fresh.DefinitionId=Id;
        const ELDGrowthReason Reason=LDHUD::ComponentEnableReason(Fresh,Latest,LatestContent);
        const FString StoreWhy=Reason==ELDGrowthReason::InvalidTarget&&Card.bBinding?FString(LDTerms::NoBindableActive)
            :Reason!=ELDGrowthReason::None?LDHUD::BuildBlockedText(Reason,LatestSafeAtMs,bLatestClock,LatestNowMs)
            :Gate.bCanInstall?FString():Gate.InstallBlocked;
        const FString Action=!Gate.bCanChoose?Gate.ChooseBlocked
            :StoreWhy.IsEmpty()?FString(LDTerms::TakeComponent)+TEXT(" → ")+LDTerms::EnableComponent
            :FString(LDTerms::TakeComponent)+TEXT("（先存着：")+StoreWhy+TEXT("）");
        GrowthText(Card.Action,Action);
        GrowthEnabled(Card.Button,Gate.bCanChoose&&!PendingRequest);
    }
}
ULDChoicesPanel::FChoiceLine& ULDChoicesPanel::ChoiceLineAt(int32 Index)
{
    while(ChoiceLines.Num()<=Index)
    {
        const int32 LineIndex=ChoiceLines.Num();
        FChoiceLine& Line=ChoiceLines.AddDefaulted_GetRef();
        auto* Box=WidgetTree->ConstructWidget<UVerticalBox>();RowWidgets.Add(Box);
        UTextBlock* Title=LDWidgets::Text(WidgetTree,TEXT(""),16);Title->SetAutoWrapText(false);Title->SetColorAndOpacity(LDHUDStyle::TextAccent);
        ScaleNewText(Title);RowWidgets.Add(Title);Line.Title=Title;
        Box->AddChildToVerticalBox(Title);
        auto* CardLine=WidgetTree->ConstructWidget<UHorizontalBox>();RowWidgets.Add(CardLine);
        for(int32 Option=0;Option<2;++Option)
        {
            // The reference is used before the next card is added (adding may move the array).
            FBaseCard& Card=Line.Cards.AddDefaulted_GetRef();
            BuildComponentCard(Card,FName(*FString::Printf(TEXT("GrowthComponentChoice%d_%d"),LineIndex,Option)),[this,LineIndex,Option](){PickComponentChoice(LineIndex,Option);});
            CardLine->AddChildToHorizontalBox(Card.Frame)->SetPadding(FMargin(Option?8.f:0.f,0,0,0));
        }
        ULDCommandButton* Points=LDWidgets::Button(WidgetTree,TEXT(""),[this,LineIndex](){TakeComponentChoicePoints(LineIndex);},false,
            FName(*FString::Printf(TEXT("GrowthComponentChoicePoints%d"),LineIndex)));
        ScaleNewText(Cast<UTextBlock>(Points->GetContent()));RowWidgets.Add(Points);Line.Points=Points;
        auto* PointsSlot=CardLine->AddChildToHorizontalBox(Points);PointsSlot->SetVerticalAlignment(VAlign_Center);PointsSlot->SetPadding(FMargin(8,0,0,0));
        Box->AddChildToVerticalBox(CardLine)->SetPadding(FMargin(0,6,0,0));
        Line.Row=Box;
        ComponentChoiceList->AddChildToVerticalBox(Box)->SetPadding(FMargin(0,LineIndex?8.f:0.f,0,0));
    }
    return ChoiceLines[Index];
}
void ULDChoicesPanel::RefreshComponentChoices()
{
    ShownChoices.Reset();
    if(Usable()){for(const FLDComponentChoiceView& Choice:Latest.ComponentChoices){ShownChoices.Add(Choice.ChoiceId);}}
    GrowthShown(ComponentChoiceSection,!ShownChoices.IsEmpty());
    const int64 PoolPoints=LatestContent?LatestContent->Parameters.EmptyComponentPoolPoints:0;
    for(int32 Index=0;Index<FMath::Max(ShownChoices.Num(),ChoiceLines.Num());++Index)
    {
        if(!ShownChoices.IsValidIndex(Index)){GrowthShown(ChoiceLines[Index].Row,false);continue;}
        FChoiceLine& Line=ChoiceLineAt(Index);
        GrowthShown(Line.Row,true,ESlateVisibility::SelfHitTestInvisible);
        const FLDComponentChoiceView& Choice=Latest.ComponentChoices[Index];
        // Only one known material left is shown as it is (spec §4.2), never padded with a made-up second card.
        GrowthText(Line.Title,Choice.Options.Num()==1?FString(LDTerms::ComponentChoiceSingle):FString(LDTerms::ComponentChoiceTitle));
        for(int32 Option=0;Option<Line.Cards.Num();++Option)
        {
            FBaseCard& Card=Line.Cards[Option];
            const bool bCard=Choice.Options.IsValidIndex(Option);
            GrowthShown(Card.Frame,bCard,ESlateVisibility::SelfHitTestInvisible);
            if(!bCard){continue;}
            ShowComponentCard(Card,Choice.Options[Option]);
            const bool bOwned=LDHUD::OwnsComponent(Latest,Choice.Options[Option]);
            GrowthText(Card.Action,!Gate.bCanComponentChoice?Gate.ComponentChoiceBlocked:bOwned?FString(LDTerms::ComponentChoiceOwned):FString(LDTerms::TakeComponent));
            GrowthEnabled(Card.Button,Gate.bCanComponentChoice&&!bOwned&&!PendingRequest);
        }
        // Every option already owned: only the empty-pool points remain.
        const bool bAllOwned=LDHUD::ComponentChoiceAllOwned(Choice,Latest);
        GrowthShown(Line.Points,bAllOwned,ESlateVisibility::Visible);
        GrowthText(Cast<UTextBlock>(Line.Points->GetContent()),PoolPoints>0?LDTerms::ComponentChoicePoints(PoolPoints):FString(TEXT("改领点数：数据缺失")));
        GrowthEnabled(Line.Points,bAllOwned&&PoolPoints>0&&Gate.bCanComponentChoice&&!PendingRequest);
    }
}
bool ULDChoicesPanel::PickComponentChoice(int32 ChoiceIndex,int32 OptionIndex)
{
    if(!Usable()||!ShownChoices.IsValidIndex(ChoiceIndex)){return false;}
    const FGuid Id=ShownChoices[ChoiceIndex];
    const FLDComponentChoiceView* Choice=Latest.ComponentChoices.FindByPredicate([&Id](const FLDComponentChoiceView& C){return C.ChoiceId==Id;});
    if(!Choice||!Choice->Options.IsValidIndex(OptionIndex)){return false;}
    if(PendingRequest){SetFeedback(TEXT("上一个请求仍在处理中。"));return false;}
    if(!Gate.bCanComponentChoice){SetFeedback(TEXT("没有提交：")+Gate.ComponentChoiceBlocked);return false;}
    const FName Option=Choice->Options[OptionIndex];
    if(LDHUD::OwnsComponent(Latest,Option)){SetFeedback(TEXT("没有提交：")+LDHUD::GrowthReasonText(ELDGrowthReason::ComponentOwned));return false;}
    FLDBuildCommand Command;
    Command.Kind=ELDBuildCommandKind::ComponentChoice;
    Command.OfferId=Id;
    if(!FLDDefinitionWire::Encode(Option,Command.Component)){SetFeedback(TEXT("没有提交：这个构件的标识无效。"));return false;}
    return Send(Command);
}
bool ULDChoicesPanel::TakeComponentChoicePoints(int32 ChoiceIndex)
{
    if(!Usable()||!ShownChoices.IsValidIndex(ChoiceIndex)){return false;}
    const FGuid Id=ShownChoices[ChoiceIndex];
    const FLDComponentChoiceView* Choice=Latest.ComponentChoices.FindByPredicate([&Id](const FLDComponentChoiceView& C){return C.ChoiceId==Id;});
    if(!Choice){return false;}
    if(PendingRequest){SetFeedback(TEXT("上一个请求仍在处理中。"));return false;}
    if(!Gate.bCanComponentChoice){SetFeedback(TEXT("没有提交：")+Gate.ComponentChoiceBlocked);return false;}
    if(!LDHUD::ComponentChoiceAllOwned(*Choice,Latest)){SetFeedback(TEXT("没有提交：")+LDHUD::GrowthReasonText(ELDGrowthReason::ComponentOwned));return false;}
    if(!LatestContent||LatestContent->Parameters.EmptyComponentPoolPoints<=0){SetFeedback(TEXT("没有提交：改领点数的数据缺失。"));return false;}
    // An all-zero Component takes the empty-pool points.
    FLDBuildCommand Command;
    Command.Kind=ELDBuildCommandKind::ComponentChoice;
    Command.OfferId=Id;
    return Send(Command);
}
ULDChoicesPanel::FComponentRow& ULDChoicesPanel::ComponentRowAt(int32 Index)
{
    while(ComponentRows.Num()<=Index)
    {
        const int32 RowIndex=ComponentRows.Num();
        FComponentRow& Row=ComponentRows.AddDefaulted_GetRef();
        auto MakeText=[this](int32 Size,const FLinearColor& Ink)
        {UTextBlock* Line=LDWidgets::Text(WidgetTree,TEXT(""),Size);Line->SetAutoWrapText(false);Line->SetColorAndOpacity(Ink);ScaleNewText(Line);RowWidgets.Add(Line);return Line;};
        auto* Lines=WidgetTree->ConstructWidget<UVerticalBox>();RowWidgets.Add(Lines);
        Row.Picture=WidgetTree->ConstructWidget<UImage>();RowWidgets.Add(Row.Picture);
        auto* Icon=WidgetTree->ConstructWidget<USizeBox>();Icon->SetWidthOverride(52);Icon->SetHeightOverride(52);Icon->SetContent(Row.Picture);
        RowWidgets.Add(Icon);Lines->AddChildToVerticalBox(Icon)->SetHorizontalAlignment(HAlign_Center);
        Row.Name=MakeText(16,LDHUDStyle::TextPrimary);Row.Name->SetWrapTextAt(GrowthPanelWidth-92);
        Lines->AddChildToVerticalBox(Row.Name)->SetPadding(FMargin(0,6,0,4));
        Row.State=MakeText(16,LDHUDStyle::TextAccent);Row.State->SetWrapTextAt(GrowthPanelWidth-92);Lines->AddChildToVerticalBox(Row.State);
        Row.Inspect=LDWidgets::Button(WidgetTree,TEXT(""),[this,RowIndex](){PreviewComponent(RowIndex);},false,
            FName(*FString::Printf(TEXT("GrowthInspectComponent%d"),RowIndex)));
        RowWidgets.Add(Row.Inspect);Row.Inspect->SetContent(Lines);
        auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetWidthOverride(GrowthPanelWidth);Size->SetMinDesiredHeight(84);Size->SetContent(Row.Inspect);
        RowWidgets.Add(Size);Row.Row=Size;
    }
    return ComponentRows[Index];
}
void ULDChoicesPanel::RefreshComponents()
{
    ShownComponents.Reset();
    if(Usable()){for(const FLDComponentInstanceView& Owned:Latest.Components){ShownComponents.Add(Owned.DefinitionId);}}
    int32 Installed=0;for(const FLDComponentInstanceView& Owned:Latest.Components){Installed+=Owned.bInstalled?1:0;}
    GrowthText(ComponentHeading,ShownComponents.IsEmpty()?TEXT("构件背包 · 暂无构件"):FString::Printf(TEXT("构件背包 · 已安装 %d / 持有 %d"),Installed,ShownComponents.Num()));
    GrowthShown(ComponentSection,!ShownComponents.IsEmpty());
    TArray<UWidget*> InstalledRows,UninstalledRows;
    for(int32 Index=0;Index<FMath::Max(ShownComponents.Num(),ComponentRows.Num());++Index)
    {
        if(!ShownComponents.IsValidIndex(Index)){GrowthShown(ComponentRows[Index].Row,false);continue;}
        FComponentRow& Row=ComponentRowAt(Index);
        GrowthShown(Row.Row,true,ESlateVisibility::Visible);
        const FLDComponentInstanceView& Owned=Latest.Components[Index];
        (Owned.bInstalled?InstalledRows:UninstalledRows).Add(Row.Row);
        if(Row.ShownId!=Owned.DefinitionId||bComponentTextStale)
        {
            const LDHUD::FComponentCardView Card=LDHUD::ComponentCard(Owned.DefinitionId,LatestContent,LatestStats);
            Row.ShownId=Owned.DefinitionId;
            LDUIIcons::Apply(Row.Picture,Card.IconId,0);
        }
        const LDHUD::FComponentRowView View=LDHUD::ComponentRow(Owned,Latest,LatestContent,Gate,LatestSafeAtMs,bLatestClock,LatestNowMs);
        FString Name=LDHUD::ContentName(LatestContent,Owned.DefinitionId.ToString());
        GrowthText(Row.Name,Name);
        const FString State=View.bInstalled?LDHUD::InstalledTargetText(Owned,LatestContent)
            :View.bCanEnable?TEXT("未安装 · 可安装"):FString(TEXT("未安装 · 不可安装："))+View.Blocked;
        GrowthText(Row.State,State);
        Row.Inspect->SetToolTipText(FText::FromString(View.Blocked));
        GrowthTint(Row.Inspect,PinnedComponent==Owned.DefinitionId?FLinearColor(.14f,.25f,.28f):GrowthCardIdle);
    }
    const auto Reconcile=[](UVerticalBox* List,const TArray<UWidget*>& Rows)
    {
        bool bChanged=List->GetChildrenCount()!=Rows.Num();
        for(int32 Index=0;!bChanged&&Index<Rows.Num();++Index){bChanged=List->GetChildAt(Index)!=Rows[Index];}
        if(!bChanged){return;}
        List->ClearChildren();
        for(UWidget* Row:Rows){List->AddChildToVerticalBox(Row);}
    };
    Reconcile(InstalledComponentList,InstalledRows);
    Reconcile(UninstalledComponentList,UninstalledRows);
    GrowthShown(InstalledComponentHeading,Installed>0,ESlateVisibility::HitTestInvisible);
    GrowthShown(UninstalledComponentHeading,ShownComponents.Num()>Installed,ESlateVisibility::HitTestInvisible);
}
void ULDChoicesPanel::RefreshBinding()
{
    // A binding in progress lasts while its command still applies to the latest view; otherwise it is dropped silently.
    const FLDDemoComponentDefinition* Definition=nullptr;
    if(!BindingComponent.IsNone()&&Usable()&&LatestContent)
    {
        const FString Id=BindingComponent.ToString();
        Definition=LatestContent->Components.FindByPredicate([&Id](const FLDDemoComponentDefinition& D){return D.Id==Id;});
        const FLDComponentInstanceView* Owned=Latest.Components.FindByPredicate([this](const FLDComponentInstanceView& C){return C.DefinitionId==BindingComponent;});
        const bool bStill=BindingKind==ELDBuildCommandKind::OpeningBase?Latest.bOpeningBasePending&&Latest.OpeningBaseOptions.Contains(BindingComponent)
            :BindingKind==ELDBuildCommandKind::InstallComponent&&Owned&&!Owned->bInstalled;
        BindingActives=Definition?LDHUD::BindableActives(*Definition,Latest):TArray<FName>();
        if(!bStill||!Definition||BindingActives.IsEmpty()){Definition=nullptr;}
        else if(!BindingActives.Contains(BindingActive)){BindingActive=BindingActives.Num()==1?BindingActives[0]:NAME_None;}
    }
    if(!Definition){BindingComponent=NAME_None;BindingKind=ELDBuildCommandKind::None;BindingActives.Reset();BindingActive=NAME_None;}
    GrowthShown(BindingSection,Definition!=nullptr);
    if(!Definition){return;}
    GrowthText(BindingHeading,LDHUD::ContentName(LatestContent,BindingComponent.ToString()));
    for(int32 Index=0;Index<BindActiveButtons.Num();++Index)
    {
        ULDCommandButton* Pick=BindActiveButtons[Index];
        const bool bShown=BindingActives.IsValidIndex(Index);
        GrowthShown(Pick,bShown,ESlateVisibility::Visible);
        if(!bShown){continue;}
        FString Label=LDHUD::ContentName(LatestContent,BindingActives[Index].ToString());
        if(BindingActives[Index]==BindingActive){Label=TEXT("【")+Label+TEXT("】");}
        GrowthText(Cast<UTextBlock>(Pick->GetContent()),Label);
        GrowthEnabled(Pick,!PendingRequest);
    }
    GrowthShown(BindElementLine,!BindingActive.IsNone(),ESlateVisibility::SelfHitTestInvisible);
    const ELDDemoElement Elements[]={ELDDemoElement::Fire,ELDDemoElement::Water,ELDDemoElement::Electric};
    for(int32 Index=0;Index<BindElementButtons.Num()&&Index<int32(UE_ARRAY_COUNT(Elements));++Index)
    {
        const bool bOffered=Definition->BindElements.Contains(Elements[Index]);
        GrowthShown(BindElementButtons[Index],bOffered,ESlateVisibility::Visible);
        GrowthEnabled(BindElementButtons[Index],bOffered&&!PendingRequest);
    }
}
bool ULDChoicesPanel::PickOpeningBase(int32 Index)
{
    if(!Usable()||!Latest.bOpeningBasePending||!ShownBaseOptions.IsValidIndex(Index)||!Latest.OpeningBaseOptions.Contains(ShownBaseOptions[Index])){return false;}
    if(PendingRequest){SetFeedback(TEXT("上一个请求仍在处理中。"));return false;}
    if(!Gate.bCanChoose){SetFeedback(TEXT("没有提交：")+Gate.ChooseBlocked);return false;}
    return BeginComponent(ELDBuildCommandKind::OpeningBase,ShownBaseOptions[Index]);
}
bool ULDChoicesPanel::EnableComponent(int32 Index)
{
    if(!Usable()||!ShownComponents.IsValidIndex(Index)){return false;}
    const FLDComponentInstanceView* Owned=Latest.Components.FindByPredicate([this,Index](const FLDComponentInstanceView& C){return C.DefinitionId==ShownComponents[Index];});
    if(!Owned){return false;}
    if(PendingRequest){SetFeedback(TEXT("上一个请求仍在处理中。"));return false;}
    const LDHUD::FComponentRowView Row=LDHUD::ComponentRow(*Owned,Latest,LatestContent,Gate,LatestSafeAtMs,bLatestClock,LatestNowMs);
    if(Row.bInstalled){SetFeedback(TEXT("没有提交：")+LDHUD::GrowthReasonText(ELDGrowthReason::AlreadyInstalled));return false;}
    if(!Row.bCanEnable){SetFeedback(TEXT("没有提交：")+Row.Blocked);return false;}
    return BeginComponent(ELDBuildCommandKind::InstallComponent,Owned->DefinitionId);
}
bool ULDChoicesPanel::BeginComponent(ELDBuildCommandKind Kind,FName Component)
{
    const FString Id=Component.ToString();
    const FLDDemoComponentDefinition* Definition=LatestContent?LatestContent->Components.FindByPredicate([&Id](const FLDDemoComponentDefinition& D){return D.Id==Id;}):nullptr;
    if(!Definition){SetFeedback(TEXT("没有提交：内容目录中找不到该构件。"));return false;}
    if(Definition->Binding==ELDDemoComponentBinding::ActiveElement)
    {
        const TArray<FName> Actives=LDHUD::BindableActives(*Definition,Latest);
        if(!Actives.IsEmpty())
        {
            BindingComponent=Component;BindingKind=Kind;BindingActives=Actives;BindingActive=Actives.Num()==1?Actives[0]:NAME_None;
            ClearTarget();RefreshTarget();
            SetFeedback(FString());RefreshBinding();
            return true;
        }
        // An opening pick may still take it into the inventory (the server stores it with the reason); an install cannot.
        if(Kind!=ELDBuildCommandKind::OpeningBase){SetFeedback(TEXT("没有提交：")+FString(LDTerms::NoBindableActive));return false;}
    }
    // #198 F: a mod or connector asks where it goes first (an opening pick is always a base).
    if(Definition->Kind!=ELDDemoComponentKind::Base&&Kind==ELDBuildCommandKind::InstallComponent)
    {
        ClearTarget();
        TargetComponent=Component;
        RefreshTarget();
        if(TargetComponent.IsNone()){SetFeedback(TEXT("没有提交：")+FString(LDTerms::NoInstallTarget));return false;}
        BindingComponent=NAME_None;BindingKind=ELDBuildCommandKind::None;BindingActives.Reset();BindingActive=NAME_None;RefreshBinding();
        SetFeedback(FString());
        return true;
    }
    return SendComponent(Kind,Component,NAME_None,ELDDemoElement::None);
}
ULDCommandButton* ULDChoicesPanel::TargetButtonAt(int32 Index)
{
    while(TargetButtons.Num()<=Index)
    {
        const int32 ButtonIndex=TargetButtons.Num();
        if(ButtonIndex%GrowthTargetsPerLine==0)
        {
            auto* Line=WidgetTree->ConstructWidget<UHorizontalBox>();RowWidgets.Add(Line);TargetLines.Add(Line);
            TargetList->AddChildToVerticalBox(Line)->SetPadding(FMargin(0,ButtonIndex?4.f:0.f,0,0));
        }
        ULDCommandButton* Pick=LDWidgets::Button(WidgetTree,TEXT(""),[this,ButtonIndex](){PickTarget(ButtonIndex);},false,
            FName(*FString::Printf(TEXT("GrowthTarget%d"),ButtonIndex)));
        ScaleNewText(Cast<UTextBlock>(Pick->GetContent()));RowWidgets.Add(Pick);
        TargetLines.Last()->AddChildToHorizontalBox(Pick)->SetPadding(FMargin(ButtonIndex%GrowthTargetsPerLine?6.f:0.f,0,0,0));
        TargetButtons.Add(Pick);
    }
    return TargetButtons[Index];
}
ULDCommandButton* ULDChoicesPanel::RepeatButtonAt(int32 Index)
{
    while(RepeatButtons.Num()<=Index)
    {
        const int32 ButtonIndex=RepeatButtons.Num();
        ULDCommandButton* Pick=LDWidgets::Button(WidgetTree,TEXT(""),[this,ButtonIndex](){PickRepeatedBase(ButtonIndex);},false,
            FName(*FString::Printf(TEXT("GrowthRepeatBase%d"),ButtonIndex)));
        ScaleNewText(Cast<UTextBlock>(Pick->GetContent()));RowWidgets.Add(Pick);
        RepeatLine->AddChildToHorizontalBox(Pick)->SetPadding(FMargin(6,0,0,0));
        RepeatButtons.Add(Pick);
    }
    return RepeatButtons[Index];
}
void ULDChoicesPanel::ClearTarget()
{
    TargetComponent=NAME_None;TargetChoices.Reset();TargetPicked=INDEX_NONE;TargetBases.Reset();
}
void ULDChoicesPanel::RefreshTarget()
{
    // A target step lasts while the component is still owned, not installed and has a place; otherwise it is dropped silently.
    const FLDDemoComponentDefinition* Definition=nullptr;
    if(!TargetComponent.IsNone()&&Usable()&&LatestContent)
    {
        const FString Id=TargetComponent.ToString();
        Definition=LatestContent->Components.FindByPredicate([&Id](const FLDDemoComponentDefinition& D){return D.Id==Id;});
        const FLDComponentInstanceView* Owned=Latest.Components.FindByPredicate([this](const FLDComponentInstanceView& C){return C.DefinitionId==TargetComponent;});
        if(!Owned||Owned->bInstalled||!Definition||Definition->Kind==ELDDemoComponentKind::Base){Definition=nullptr;}
    }
    if(Definition)
    {
        // The picked place survives a refresh only while it is still offered.
        const LDHUD::FWorkshopTargetView Picked=TargetChoices.IsValidIndex(TargetPicked)?TargetChoices[TargetPicked]:LDHUD::FWorkshopTargetView();
        const bool bHadPick=TargetChoices.IsValidIndex(TargetPicked);
        TargetChoices=LDHUD::WorkshopTargets(*Definition,Latest,LatestContent);
        TargetBases=LDHUD::RepeatableBases(*Definition,Latest,LatestContent);
        TargetPicked=bHadPick?TargetChoices.IndexOfByPredicate([&Picked](const LDHUD::FWorkshopTargetView& T){return T.Kind==Picked.Kind&&T.TargetId==Picked.TargetId;}):INDEX_NONE;
        if(TargetChoices.IsEmpty()||(!Definition->ExtraBases.IsEmpty()&&TargetBases.IsEmpty())){Definition=nullptr;}
    }
    if(!Definition){ClearTarget();}
    GrowthShown(TargetSection,Definition!=nullptr);
    if(!Definition){return;}
    GrowthText(TargetHeading,LDHUD::ContentName(LatestContent,TargetComponent.ToString()));
    for(int32 Index=0;Index<FMath::Max(TargetChoices.Num(),TargetButtons.Num());++Index)
    {
        const bool bShown=TargetChoices.IsValidIndex(Index);
        ULDCommandButton* Pick=bShown?TargetButtonAt(Index):TargetButtons[Index].Get();
        GrowthShown(Pick,bShown,ESlateVisibility::Visible);
        if(!bShown){continue;}
        const FString& Label=TargetChoices[Index].Label;
        GrowthText(Cast<UTextBlock>(Pick->GetContent()),Index==TargetPicked?TEXT("【")+Label+TEXT("】"):Label);
        GrowthEnabled(Pick,!PendingRequest);
    }
    // C12 asks for the repeated base once its place is picked.
    const bool bRepeat=!Definition->ExtraBases.IsEmpty()&&TargetPicked!=INDEX_NONE;
    GrowthShown(RepeatLine,bRepeat,ESlateVisibility::SelfHitTestInvisible);
    for(int32 Index=0;Index<FMath::Max(TargetBases.Num(),RepeatButtons.Num());++Index)
    {
        const bool bShown=bRepeat&&TargetBases.IsValidIndex(Index);
        ULDCommandButton* Pick=bShown?RepeatButtonAt(Index):RepeatButtons.IsValidIndex(Index)?RepeatButtons[Index].Get():nullptr;
        GrowthShown(Pick,bShown,ESlateVisibility::Visible);
        if(!bShown){continue;}
        GrowthText(Cast<UTextBlock>(Pick->GetContent()),LDHUD::ContentName(LatestContent,TargetBases[Index].ToString()));
        GrowthEnabled(Pick,!PendingRequest);
    }
}
bool ULDChoicesPanel::PickTarget(int32 Index)
{
    RefreshTarget();
    if(TargetComponent.IsNone()||!TargetChoices.IsValidIndex(Index)){return false;}
    const FString Id=TargetComponent.ToString();
    const FLDDemoComponentDefinition* Definition=LatestContent?LatestContent->Components.FindByPredicate([&Id](const FLDDemoComponentDefinition& D){return D.Id==Id;}):nullptr;
    if(!Definition){return false;}
    if(!Definition->ExtraBases.IsEmpty()){TargetPicked=Index;RefreshTarget();return true;}
    if(PendingRequest){SetFeedback(TEXT("上一个请求仍在处理中。"));return false;}
    const FLDComponentInstanceView* Owned=Latest.Components.FindByPredicate([this](const FLDComponentInstanceView& C){return C.DefinitionId==TargetComponent;});
    const LDHUD::FComponentRowView Row=Owned?LDHUD::ComponentRow(*Owned,Latest,LatestContent,Gate,LatestSafeAtMs,bLatestClock,LatestNowMs):LDHUD::FComponentRowView();
    if(!Row.bCanEnable){SetFeedback(TEXT("没有提交：")+(Row.Blocked.IsEmpty()?LDHUD::GrowthReasonText(ELDGrowthReason::NotOwned):Row.Blocked));return false;}
    const LDHUD::FWorkshopTargetView Target=TargetChoices[Index];
    return SendComponent(ELDBuildCommandKind::InstallComponent,TargetComponent,NAME_None,ELDDemoElement::None,Target.Kind,Target.TargetId);
}
bool ULDChoicesPanel::PickRepeatedBase(int32 Index)
{
    RefreshTarget();
    if(TargetComponent.IsNone()||!TargetChoices.IsValidIndex(TargetPicked)||!TargetBases.IsValidIndex(Index)){return false;}
    if(PendingRequest){SetFeedback(TEXT("上一个请求仍在处理中。"));return false;}
    const FLDComponentInstanceView* Owned=Latest.Components.FindByPredicate([this](const FLDComponentInstanceView& C){return C.DefinitionId==TargetComponent;});
    const LDHUD::FComponentRowView Row=Owned?LDHUD::ComponentRow(*Owned,Latest,LatestContent,Gate,LatestSafeAtMs,bLatestClock,LatestNowMs):LDHUD::FComponentRowView();
    if(!Row.bCanEnable){SetFeedback(TEXT("没有提交：")+(Row.Blocked.IsEmpty()?LDHUD::GrowthReasonText(ELDGrowthReason::NotOwned):Row.Blocked));return false;}
    const LDHUD::FWorkshopTargetView Target=TargetChoices[TargetPicked];
    return SendComponent(ELDBuildCommandKind::InstallComponent,TargetComponent,NAME_None,ELDDemoElement::None,Target.Kind,Target.TargetId,TargetBases[Index]);
}
void ULDChoicesPanel::CancelTarget()
{
    ClearTarget();
    RefreshTarget();
}
bool ULDChoicesPanel::PickBindActive(int32 Index)
{
    if(BindingComponent.IsNone()||!BindingActives.IsValidIndex(Index)){return false;}
    BindingActive=BindingActives[Index];
    RefreshBinding();
    return true;
}
bool ULDChoicesPanel::PickBindElement(ELDDemoElement Element)
{
    RefreshBinding();
    if(BindingComponent.IsNone()||BindingActive.IsNone()){return false;}
    if(PendingRequest){SetFeedback(TEXT("上一个请求仍在处理中。"));return false;}
    const FString Id=BindingComponent.ToString();
    const FLDDemoComponentDefinition* Definition=LatestContent?LatestContent->Components.FindByPredicate([&Id](const FLDDemoComponentDefinition& D){return D.Id==Id;}):nullptr;
    if(!Definition||!Definition->BindElements.Contains(Element)){SetFeedback(TEXT("没有提交：")+LDHUD::GrowthReasonText(ELDGrowthReason::InvalidTarget));return false;}
    if(BindingKind==ELDBuildCommandKind::OpeningBase&&!Gate.bCanChoose){SetFeedback(TEXT("没有提交：")+Gate.ChooseBlocked);return false;}
    if(BindingKind==ELDBuildCommandKind::InstallComponent)
    {
        const FLDComponentInstanceView* Owned=Latest.Components.FindByPredicate([this](const FLDComponentInstanceView& C){return C.DefinitionId==BindingComponent;});
        const LDHUD::FComponentRowView Row=Owned?LDHUD::ComponentRow(*Owned,Latest,LatestContent,Gate,LatestSafeAtMs,bLatestClock,LatestNowMs):LDHUD::FComponentRowView();
        if(!Row.bCanEnable){SetFeedback(TEXT("没有提交：")+(Row.Blocked.IsEmpty()?LDHUD::GrowthReasonText(ELDGrowthReason::NotOwned):Row.Blocked));return false;}
    }
    return SendComponent(BindingKind,BindingComponent,BindingActive,Element);
}
void ULDChoicesPanel::CancelBinding()
{
    BindingComponent=NAME_None;BindingKind=ELDBuildCommandKind::None;BindingActives.Reset();BindingActive=NAME_None;
    RefreshBinding();
}
bool ULDChoicesPanel::SendComponent(ELDBuildCommandKind Kind,FName Component,FName BoundActive,ELDDemoElement Element,
    ELDComponentTargetKind TargetKind,FName TargetId,FName BoundComponent)
{
    FLDBuildCommand Command;
    Command.Kind=Kind;
    if(!FLDDefinitionWire::Encode(Component,Command.Component)){SetFeedback(TEXT("没有提交：这个构件的标识无效。"));return false;}
    // An opening pick always asks to enable it now; the server keeps it in the inventory with the reason when it may not.
    Command.bInstallIfPossible=Kind==ELDBuildCommandKind::OpeningBase;
    // Base components take no target; B18 carries its binding; a mod or connector its place (#198 F, C12 also its base).
    Command.TargetKind=TargetKind;
    if(!TargetId.IsNone()&&!FLDDefinitionWire::Encode(TargetId,Command.TargetDefinition)){SetFeedback(TEXT("没有提交：安装位置的标识无效。"));return false;}
    if(!BoundComponent.IsNone()&&!FLDDefinitionWire::Encode(BoundComponent,Command.BoundComponent)){SetFeedback(TEXT("没有提交：重复的基础构件标识无效。"));return false;}
    if(!BoundActive.IsNone())
    {
        if(!FLDDefinitionWire::Encode(BoundActive,Command.BoundActive)){SetFeedback(TEXT("没有提交：绑定的技能标识无效。"));return false;}
        Command.Element=Element;
    }
    BindingComponent=NAME_None;BindingKind=ELDBuildCommandKind::None;BindingActives.Reset();BindingActive=NAME_None;
    ClearTarget();
    return Send(Command);
}
bool ULDChoicesPanel::ChooseOffer(int32 Index)
{
    if(!Usable()||!Latest.bHasActiveOffer||!Latest.ActiveOffer.Options.IsValidIndex(Index)||Latest.ActiveOffer.OfferId!=ShownOffer){return false;}
    // One click is one choice: a click while a request is in flight never queues a second one.
    if(PendingRequest){SetFeedback(TEXT("上一个请求仍在处理中。"));return false;}
    if(!Gate.bCanChoose){SetFeedback(TEXT("没有提交：")+Gate.ChooseBlocked);return false;}
    FLDBuildCommand Command;
    Command.Kind=ELDBuildCommandKind::ActiveChoice;
    Command.OfferId=Latest.ActiveOffer.OfferId;Command.OfferRevision=Latest.ActiveOffer.OfferRevision;
    if(!FLDDefinitionWire::Encode(Latest.ActiveOffer.Options[Index],Command.Option)){SetFeedback(TEXT("没有提交：这个技能的标识无效。"));return false;}
    // Always ask to learn now: the server learns it into the lowest empty slot when it may, otherwise keeps it as a blueprint.
    Command.bLearnIfPossible=true;Command.Slot=FLDBuildCommand::AnySlot;
    PinnedPreview=INDEX_NONE;
    return Send(Command);
}
bool ULDChoicesPanel::PreviewOffer(int32 Index)
{
    if(!Latest.bHasActiveOffer||!Latest.ActiveOffer.Options.IsValidIndex(Index)||Latest.ActiveOffer.OfferId!=ShownOffer){return false;}
    PinnedPreview=Index;UpdateDetail();return true;
}
bool ULDChoicesPanel::LearnBlueprint(int32 Index,uint8 Key)
{
    if(!Usable()||!ShownBlueprints.IsValidIndex(Index)||Key>2||!Latest.PendingBlueprints.Contains(ShownBlueprints[Index])){return false;}
    if(PendingRequest){SetFeedback(TEXT("上一个请求仍在处理中。"));return false;}
    if(!LDHUD::ActiveInSlot(Latest,Key).IsNone()){SetFeedback(TEXT("没有提交：")+FString(LDTerms::ReplaceAtRest));return false;}
    if(!Gate.bCanLearn){SetFeedback(TEXT("没有提交：")+Gate.LearnBlocked);return false;}
    FLDBuildCommand Command;
    Command.Kind=ELDBuildCommandKind::LearnBlueprint;
    if(!FLDDefinitionWire::Encode(ShownBlueprints[Index],Command.Blueprint)){SetFeedback(TEXT("没有提交：这份图纸的标识无效。"));return false;}
    Command.Slot=Key;
    return Send(Command);
}
bool ULDChoicesPanel::StagePoint(int32 Index,int32 Delta)
{
    if(!Usable()||!Staged.IsValidIndex(Index)||(Delta!=1&&Delta!=-1)){return false;}
    if(Delta<0&&Staged[Index]<=0){return false;}
    if(Delta>0&&(StagedTotal()>=Latest.UnspentPoints||Staged[Index]>=MAX_uint8)){SetFeedback(TEXT("未分配点数不够了。"));return false;}
    Staged[Index]+=Delta;
    RefreshPoints();
    return true;
}
bool ULDChoicesPanel::ConfirmPoints()
{
    if(!Usable()){return false;}
    if(PendingRequest){SetFeedback(TEXT("上一个请求仍在处理中。"));return false;}
    if(StagedTotal()<=0){SetFeedback(TEXT("先用 + 选好要加的点。"));return false;}
    if(!Gate.bCanAllocate){SetFeedback(TEXT("没有提交：")+Gate.AllocateBlocked);return false;}
    // One count per stat in the stats' design order, exactly as the view lists them; a mismatch is a data error, not a guess.
    if(!LatestContent||Latest.StatPoints.Num()!=LatestContent->Stats.Num()||Staged.Num()!=LatestContent->Stats.Num())
    {SetFeedback(TEXT("没有提交：属性数据与内容目录不一致。"));return false;}
    FLDBuildCommand Command;
    Command.Kind=ELDBuildCommandKind::AllocatePoints;
    const TArray<const FLDDemoStatDefinition*> StatOrder=LatestContent->StatsInDesignOrder();
    for(int32 Index=0;Index<Staged.Num();++Index)
    {
        if(Latest.StatPoints[Index].StatId!=FName(*StatOrder[Index]->Id)){SetFeedback(TEXT("没有提交：属性数据与内容目录不一致。"));return false;}
        Command.PointCounts.Add(uint8(Staged[Index]));
    }
    return Send(Command);
}
FString ULDChoicesPanel::NpcRefusal() const
{
    if(!Gate.bCanBuyNpc){return Gate.NpcBlocked;}
    if(ServiceContext!=ELDRestService::SkillMerchant){return TEXT("请先靠近技能商人");}
    if(!LatestContent){return TEXT("内容数据同步中");}
    const int64 Price=LatestContent->Parameters.NpcActivePoints;
    if(Price<=0){return TEXT("NPC 价格数据缺失");}
    if(Latest.bHasActiveOffer||Latest.QueuedActiveOpportunities>0){return LDHUD::GrowthReasonText(ELDGrowthReason::OfferOutstanding);}
    if(Latest.UnspentPoints<Price){return FString::Printf(TEXT("未分配点数不够（要 %lld 点）"),Price);}
    return FString();
}
FString ULDChoicesPanel::RespecRefusal() const
{
    if(!Gate.bCanRespec){return Gate.RespecBlocked;}
    if(ServiceContext!=ELDRestService::Trainer){return TEXT("请先靠近训练师");}
    int64 Allocated=0;for(const FLDStatPointsView& Row:Latest.StatPoints){Allocated+=FMath::Max(0,Row.Points);}
    return Allocated>0?FString():LDHUD::GrowthReasonText(ELDGrowthReason::NothingToRespec);
}
FString ULDChoicesPanel::ReplaceRefusal() const
{
    if(!Gate.bCanReplace){return Gate.ReplaceBlocked;}
    if(ServiceContext!=ELDRestService::Trainer){return TEXT("请先靠近训练师");}
    if(!bServicePanelOpen){return TEXT("请先与训练师交互");}
    return Latest.bHasActiveOffer?LDHUD::GrowthReasonText(ELDGrowthReason::OfferOutstanding):FString();
}
FString ULDChoicesPanel::ForgetRefusal() const
{
    if(!Gate.bCanForget){return Gate.ForgetBlocked;}
    if(ServiceContext!=ELDRestService::Trainer){return TEXT("请先靠近训练师");}
    return Latest.bHasActiveOffer?LDHUD::GrowthReasonText(ELDGrowthReason::OfferOutstanding):FString();
}
bool ULDChoicesPanel::RestConfirmHolds() const
{
    if(RestConfirm.Kind==ELDBuildCommandKind::None||!Usable()){return false;}
    if(RestConfirm.Kind==ELDBuildCommandKind::FillGap)
    {
        // The target is still a gap and the base still one that fills it.
        const LDHUD::FGapTargetView* Gap=Materials.Gaps.FindByPredicate([this](const LDHUD::FGapTargetView& G){return G.Target==RestConfirm.GapTarget;});
        return Gap&&Gap->Fillers.Contains(RestConfirm.GapFiller);
    }
    if(RestConfirm.Kind==ELDBuildCommandKind::Dismantle)
    {
        for(const FName& Id:RestConfirm.Surrendered){if(!Materials.DismantleCandidates.Contains(Id)){return false;}}
        return int64(RestConfirm.Surrendered.Num())==Materials.DismantleCount;
    }
    if(LDHUD::ActiveInSlot(Latest,RestConfirm.Slot)!=RestConfirm.Active){return false;}
    return RestConfirm.Kind!=ELDBuildCommandKind::ReplaceActive||Latest.PendingBlueprints.Contains(RestConfirm.Blueprint);
}
FString ULDChoicesPanel::RestConfirmRefusal() const
{
    switch(RestConfirm.Kind)
    {
    case ELDBuildCommandKind::ReplaceActive: return ReplaceRefusal();
    case ELDBuildCommandKind::ForgetActive: return ForgetRefusal();
    case ELDBuildCommandKind::FillGap: return Materials.FillGapBlocked;
    case ELDBuildCommandKind::Dismantle:
        return !Materials.DismantleBlocked.IsEmpty()?Materials.DismantleBlocked:LDHUD::DismantleSelectionRefusal(Latest,LatestContent,RestConfirm.Surrendered);
    default: return LDHUD::GrowthReasonText(ELDGrowthReason::InvalidRequest);
    }
}
void ULDChoicesPanel::RefreshRest()
{
    if(!RestSection){return;}
    const bool bShown=Usable()&&Gate.bInRest;
    Materials=bShown?LDHUD::MaterialServices(Latest,LatestContent,Gate):LDHUD::FMaterialServicesView();
    if(ServiceContext!=ELDRestService::ComponentArtisan)
    {
        if(Materials.FillGapBlocked.IsEmpty()){Materials.FillGapBlocked=TEXT("请先靠近构件工匠");}
        if(Materials.DismantleBlocked.IsEmpty()){Materials.DismantleBlocked=TEXT("请先靠近构件工匠");}
    }
    GrowthShown(RestSection,bShown&&bServicePanelOpen&&ServiceContext!=ELDRestService::None);
    // A confirm line whose terms changed (another slot content, a used blueprint, changed components, the rest over) is
    // withdrawn, never resent.
    if(RestConfirm.Kind!=ELDBuildCommandKind::None&&(!bShown||!RestConfirmHolds()))
    {
        const bool bMaterial=RestConfirm.Kind==ELDBuildCommandKind::FillGap||RestConfirm.Kind==ELDBuildCommandKind::Dismantle;
        RestConfirm=FRestConfirm();
        if(bShown){SetFeedback(bMaterial?TEXT("构件已变化，请重新选择。"):TEXT("技能槽或图纸已变化，请重新选择。"));}
    }
    GrowthShown(RestConfirmLine,RestConfirm.Kind!=ELDBuildCommandKind::None,ESlateVisibility::SelfHitTestInvisible);
    if(!bShown){CloseMaterialPickers();return;}
    const int64 Price=LatestContent?LatestContent->Parameters.NpcActivePoints:0;
    GrowthText(Cast<UTextBlock>(NpcButton->GetContent()),Price>0?LDTerms::NpcOffer(Price):FString(TEXT("NPC：价格数据缺失")));
    const auto Service=[this](ULDCommandButton* Button,UTextBlock* Why,const FString& Refusal)
    {
        GrowthEnabled(Button,Refusal.IsEmpty()&&!PendingRequest);
        GrowthText(Why,Refusal);
        GrowthShown(Why,!Refusal.IsEmpty(),ESlateVisibility::HitTestInvisible);
    };
    Service(NpcButton,NpcBlocked,NpcRefusal());
    Service(RespecButton,RespecBlocked,RespecRefusal());
    const FString Forget=ForgetRefusal();
    bool bAnyLearned=false;
    for(uint8 Key=0;Key<3&&ForgetButtons.IsValidIndex(Key);++Key)
    {
        const FName Active=LDHUD::ActiveInSlot(Latest,Key);
        GrowthShown(ForgetButtons[Key],!Active.IsNone(),ESlateVisibility::Visible);
        if(Active.IsNone()){continue;}
        bAnyLearned=true;
        GrowthText(Cast<UTextBlock>(ForgetButtons[Key]->GetContent()),LDTerms::ForgetSlot(LDHUD::SlotKey(Key),LDHUD::ContentName(LatestContent,Active.ToString())));
        GrowthEnabled(ForgetButtons[Key],Forget.IsEmpty()&&!PendingRequest);
    }
    const FString ForgetWhy=bAnyLearned?Forget:FString(TEXT("还没有学会的技能可遗忘"));
    GrowthText(ForgetBlocked,ForgetWhy);
    GrowthShown(ForgetBlocked,!ForgetWhy.IsEmpty(),ESlateVisibility::HitTestInvisible);
    RefreshMaterials();
    GrowthShown(NpcButton->GetParent(),ServiceContext==ELDRestService::SkillMerchant);
    GrowthShown(RespecButton->GetParent(),ServiceContext==ELDRestService::Trainer);
    GrowthShown(ForgetBlocked->GetParent(),ServiceContext==ELDRestService::Trainer);
    GrowthShown(FillGapButton->GetParent(),ServiceContext==ELDRestService::ComponentArtisan);
    GrowthShown(DismantleButton->GetParent(),ServiceContext==ELDRestService::ComponentArtisan);
    if(ServiceContext!=ELDRestService::ComponentArtisan)
    {GrowthShown(GapTargetLine,false);GrowthShown(GapFillerLine,false);GrowthShown(DismantleSection,false);}
    if(RestConfirm.Kind!=ELDBuildCommandKind::None)
    {
        const auto Name=[this](FName Id){return LDHUD::ContentName(LatestContent,Id.ToString());};
        const auto Names=[&Name](const TArray<FName>& Ids){TArray<FString> Out;for(const FName& Id:Ids){Out.Add(Name(Id));}return FString::Join(Out,TEXT("、"));};
        const FString Refusal=RestConfirmRefusal();
        FString Line;
        switch(RestConfirm.Kind)
        {
        case ELDBuildCommandKind::ReplaceActive:
            Line=LDTerms::ReplaceConfirm(Name(RestConfirm.Blueprint),LDHUD::SlotKey(RestConfirm.Slot),Name(RestConfirm.Active));
            break;
        case ELDBuildCommandKind::ForgetActive:
            Line=LDTerms::ForgetConfirm(LDHUD::SlotKey(RestConfirm.Slot),Name(RestConfirm.Active));
            break;
        case ELDBuildCommandKind::FillGap:
            Line=LDTerms::FillGapConfirm(Materials.FillGapPoints,Name(RestConfirm.GapTarget),Name(RestConfirm.GapFiller));
            break;
        case ELDBuildCommandKind::Dismantle:
            // What is deleted and which installs lose their host and go back to the inventory (spec §4.5).
            Line=LDTerms::DismantleConfirm(Names(RestConfirm.Surrendered),Names(LDHUD::DismantleDetached(Latest,RestConfirm.Surrendered)));
            break;
        default: break;
        }
        if(!Refusal.IsEmpty()){Line+=TEXT("（")+Refusal+TEXT("）");}
        GrowthText(RestConfirmText,Line);
        GrowthEnabled(RestConfirmButton,Refusal.IsEmpty()&&!PendingRequest);
    }
}
void ULDChoicesPanel::CloseMaterialPickers()
{
    bFillGapOpen=false;GapTarget=NAME_None;bDismantleOpen=false;DismantlePicked.Reset();
    if(RestConfirm.Kind==ELDBuildCommandKind::FillGap||RestConfirm.Kind==ELDBuildCommandKind::Dismantle)
    {RestConfirm=FRestConfirm();GrowthShown(RestConfirmLine,false);}
}
ULDCommandButton* ULDChoicesPanel::GapTargetButtonAt(int32 Index)
{
    while(GapTargetButtons.Num()<=Index)
    {
        const int32 ButtonIndex=GapTargetButtons.Num();
        ULDCommandButton* Pick=LDWidgets::Button(WidgetTree,TEXT(""),[this,ButtonIndex](){PickGapTarget(ButtonIndex);},false,
            FName(*FString::Printf(TEXT("GrowthRestGapTarget%d"),ButtonIndex)));
        ScaleNewText(Cast<UTextBlock>(Pick->GetContent()));RowWidgets.Add(Pick);
        auto* Caption=CastChecked<UTextBlock>(Pick->GetContent());Caption->SetAutoWrapText(false);Caption->SetWrapTextAt(180);
        GapTargetLine->AddChildToWrapBox(Pick);
        GapTargetButtons.Add(Pick);
    }
    return GapTargetButtons[Index];
}
ULDCommandButton* ULDChoicesPanel::GapFillerButtonAt(int32 Index)
{
    while(GapFillerButtons.Num()<=Index)
    {
        const int32 ButtonIndex=GapFillerButtons.Num();
        ULDCommandButton* Pick=LDWidgets::Button(WidgetTree,TEXT(""),[this,ButtonIndex](){PickGapFiller(ButtonIndex);},false,
            FName(*FString::Printf(TEXT("GrowthRestGapFiller%d"),ButtonIndex)));
        ScaleNewText(Cast<UTextBlock>(Pick->GetContent()));RowWidgets.Add(Pick);
        auto* Caption=CastChecked<UTextBlock>(Pick->GetContent());Caption->SetAutoWrapText(false);Caption->SetWrapTextAt(180);
        GapFillerLine->AddChildToWrapBox(Pick);
        GapFillerButtons.Add(Pick);
    }
    return GapFillerButtons[Index];
}
ULDCommandButton* ULDChoicesPanel::DismantleButtonAt(int32 Index)
{
    while(DismantleButtons.Num()<=Index)
    {
        const int32 ButtonIndex=DismantleButtons.Num();
        if(ButtonIndex%GrowthDismantlePerLine==0)
        {
            auto* Line=WidgetTree->ConstructWidget<UHorizontalBox>();RowWidgets.Add(Line);DismantleLines.Add(Line);
            DismantleList->AddChildToVerticalBox(Line)->SetPadding(FMargin(0,ButtonIndex?4.f:0.f,0,0));
        }
        ULDCommandButton* Pick=LDWidgets::Button(WidgetTree,TEXT(""),[this,ButtonIndex](){PickDismantle(ButtonIndex);},false,
            FName(*FString::Printf(TEXT("GrowthRestDismantlePick%d"),ButtonIndex)));
        ScaleNewText(Cast<UTextBlock>(Pick->GetContent()));RowWidgets.Add(Pick);
        auto* Caption=CastChecked<UTextBlock>(Pick->GetContent());Caption->SetAutoWrapText(false);Caption->SetWrapTextAt(GrowthPanelWidth-44.f);
        DismantleLines.Last()->AddChildToHorizontalBox(Pick)->SetPadding(FMargin(ButtonIndex%GrowthDismantlePerLine?6.f:0.f,0,0,0));
        DismantleButtons.Add(Pick);
    }
    return DismantleButtons[Index];
}
void ULDChoicesPanel::RefreshMaterials()
{
    const auto Name=[this](FName Id){return LDHUD::ContentName(LatestContent,Id.ToString());};
    const auto Marked=[](const FString& Label,bool bPicked){return bPicked?TEXT("【")+Label+TEXT("】"):Label;};
    const auto Service=[this](ULDCommandButton* Button,UTextBlock* Why,const FString& Refusal)
    {
        GrowthEnabled(Button,Refusal.IsEmpty()&&!PendingRequest);
        GrowthText(Why,Refusal);
        GrowthShown(Why,!Refusal.IsEmpty(),ESlateVisibility::HitTestInvisible);
    };
    // FillGap: the service, then (while open) the gap targets and the picked target's fillers.
    GrowthText(Cast<UTextBlock>(FillGapButton->GetContent()),Materials.FillGapPoints>0?LDTerms::FillGapService(Materials.FillGapPoints):FString(TEXT("补缺：价格数据缺失")));
    Service(FillGapButton,FillGapBlocked,Materials.FillGapBlocked);
    if(!Materials.FillGapBlocked.IsEmpty()){bFillGapOpen=false;}
    const int32 Picked=bFillGapOpen?Materials.Gaps.IndexOfByPredicate([this](const LDHUD::FGapTargetView& Gap){return Gap.Target==GapTarget;}):INDEX_NONE;
    if(Picked==INDEX_NONE){GapTarget=NAME_None;}
    GrowthShown(GapTargetLine,bFillGapOpen,ESlateVisibility::SelfHitTestInvisible);
    const int32 TargetCount=bFillGapOpen?Materials.Gaps.Num():0;
    for(int32 Index=0;Index<FMath::Max(TargetCount,GapTargetButtons.Num());++Index)
    {
        const bool bButtonShown=Index<TargetCount;
        ULDCommandButton* Pick=bButtonShown?GapTargetButtonAt(Index):GapTargetButtons[Index].Get();
        GrowthShown(Pick,bButtonShown,ESlateVisibility::Visible);
        if(!bButtonShown){continue;}
        GrowthText(Cast<UTextBlock>(Pick->GetContent()),Marked(Name(Materials.Gaps[Index].Target),Index==Picked));
        GrowthEnabled(Pick,!PendingRequest);
    }
    const int32 FillerCount=Picked!=INDEX_NONE?Materials.Gaps[Picked].Fillers.Num():0;
    GrowthShown(GapFillerLine,Picked!=INDEX_NONE,ESlateVisibility::SelfHitTestInvisible);
    for(int32 Index=0;Index<FMath::Max(FillerCount,GapFillerButtons.Num());++Index)
    {
        const bool bButtonShown=Index<FillerCount;
        ULDCommandButton* Pick=bButtonShown?GapFillerButtonAt(Index):GapFillerButtons[Index].Get();
        GrowthShown(Pick,bButtonShown,ESlateVisibility::Visible);
        if(!bButtonShown){continue;}
        const FName Filler=Materials.Gaps[Picked].Fillers[Index];
        GrowthText(Cast<UTextBlock>(Pick->GetContent()),Marked(Name(Filler),RestConfirm.Kind==ELDBuildCommandKind::FillGap&&RestConfirm.GapFiller==Filler));
        GrowthEnabled(Pick,!PendingRequest);
    }
    // Dismantle: the service, then (while open) every owned common component; installed ones warn about their installs.
    const int64 Count=Materials.DismantleCount;
    GrowthText(Cast<UTextBlock>(DismantleButton->GetContent()),Count>0?LDTerms::DismantleService(Count):FString(TEXT("拆解：数量数据缺失")));
    Service(DismantleButton,DismantleBlocked,Materials.DismantleBlocked);
    if(!Materials.DismantleBlocked.IsEmpty()){bDismantleOpen=false;}
    if(!bDismantleOpen){DismantlePicked.Reset();}
    DismantlePicked.RemoveAll([this](const FName& Id){return !Materials.DismantleCandidates.Contains(Id);});
    GrowthShown(DismantleSection,bDismantleOpen,ESlateVisibility::SelfHitTestInvisible);
    GrowthText(DismantleHeading,LDTerms::PickDismantle(Count)+TEXT(" ")+LDTerms::DismantlePicked(DismantlePicked.Num(),Count));
    const int32 CandidateCount=bDismantleOpen?Materials.DismantleCandidates.Num():0;
    for(int32 Index=0;Index<FMath::Max(CandidateCount,DismantleButtons.Num());++Index)
    {
        const bool bButtonShown=Index<CandidateCount;
        ULDCommandButton* Pick=bButtonShown?DismantleButtonAt(Index):DismantleButtons[Index].Get();
        GrowthShown(Pick,bButtonShown,ESlateVisibility::Visible);
        if(!bButtonShown){continue;}
        const FName Id=Materials.DismantleCandidates[Index];
        const bool bInstalled=Latest.Components.ContainsByPredicate([Id](const FLDComponentInstanceView& C){return C.DefinitionId==Id&&C.bInstalled;});
        const bool bPicked=DismantlePicked.Contains(Id);
        GrowthText(Cast<UTextBlock>(Pick->GetContent()),Marked(Name(Id)+(bInstalled?LDTerms::DismantleInstalledWarning:TEXT("")),bPicked));
        GrowthEnabled(Pick,!PendingRequest&&(bPicked||int64(DismantlePicked.Num())<Count));
    }
}
bool ULDChoicesPanel::ToggleFillGap()
{
    if(!Usable()){return false;}
    if(bFillGapOpen){CloseMaterialPickers();RefreshRest();return true;}
    if(PendingRequest){SetFeedback(TEXT("上一个请求仍在处理中。"));return false;}
    if(!Materials.FillGapBlocked.IsEmpty()){SetFeedback(TEXT("没有提交：")+Materials.FillGapBlocked);return false;}
    CloseMaterialPickers();
    bFillGapOpen=true;
    // A single gap is picked straight away.
    if(Materials.Gaps.Num()==1){GapTarget=Materials.Gaps[0].Target;}
    SetFeedback(FString());
    RefreshRest();
    return true;
}
bool ULDChoicesPanel::PickGapTarget(int32 Index)
{
    if(!Usable()||!bFillGapOpen||!Materials.Gaps.IsValidIndex(Index)){return false;}
    GapTarget=Materials.Gaps[Index].Target;
    if(RestConfirm.Kind==ELDBuildCommandKind::FillGap&&RestConfirm.GapTarget!=GapTarget){CancelRestAction();}
    // A single filler goes straight to the confirm line.
    if(Materials.Gaps[Index].Fillers.Num()==1){return PickGapFiller(0);}
    RefreshRest();
    return true;
}
bool ULDChoicesPanel::PickGapFiller(int32 Index)
{
    const LDHUD::FGapTargetView* Gap=bFillGapOpen?Materials.Gaps.FindByPredicate([this](const LDHUD::FGapTargetView& G){return G.Target==GapTarget;}):nullptr;
    if(!Usable()||!Gap||!Gap->Fillers.IsValidIndex(Index)){return false;}
    if(!Materials.FillGapBlocked.IsEmpty()){SetFeedback(TEXT("没有提交：")+Materials.FillGapBlocked);return false;}
    const FName Filler=Gap->Fillers[Index];
    RestConfirm=FRestConfirm();
    RestConfirm.Kind=ELDBuildCommandKind::FillGap;RestConfirm.GapTarget=GapTarget;RestConfirm.GapFiller=Filler;
    SetFeedback(FString());
    RefreshRest();
    return true;
}
bool ULDChoicesPanel::ToggleDismantle()
{
    if(!Usable()){return false;}
    if(bDismantleOpen){CloseMaterialPickers();RefreshRest();return true;}
    if(PendingRequest){SetFeedback(TEXT("上一个请求仍在处理中。"));return false;}
    if(!Materials.DismantleBlocked.IsEmpty()){SetFeedback(TEXT("没有提交：")+Materials.DismantleBlocked);return false;}
    CloseMaterialPickers();
    bDismantleOpen=true;
    SetFeedback(FString());
    RefreshRest();
    return true;
}
bool ULDChoicesPanel::PickDismantle(int32 Index)
{
    if(!Usable()||!bDismantleOpen||!Materials.DismantleCandidates.IsValidIndex(Index)){return false;}
    const FName Id=Materials.DismantleCandidates[Index];
    if(DismantlePicked.Contains(Id))
    {
        DismantlePicked.Remove(Id);
        if(RestConfirm.Kind==ELDBuildCommandKind::Dismantle){RestConfirm=FRestConfirm();}
    }
    else
    {
        if(int64(DismantlePicked.Num())>=Materials.DismantleCount){SetFeedback(LDTerms::DismantlePicked(DismantlePicked.Num(),Materials.DismantleCount));return false;}
        DismantlePicked.Add(Id);
        // The approved count picked: the confirm line names what is deleted and what goes back to the inventory.
        if(int64(DismantlePicked.Num())==Materials.DismantleCount)
        {
            RestConfirm=FRestConfirm();
            RestConfirm.Kind=ELDBuildCommandKind::Dismantle;RestConfirm.Surrendered=DismantlePicked;
        }
    }
    SetFeedback(FString());
    RefreshRest();
    return true;
}
bool ULDChoicesPanel::BuyNpcOffer()
{
    if(!Usable()){return false;}
    if(PendingRequest){SetFeedback(TEXT("上一个请求仍在处理中。"));return false;}
    const FString Refusal=NpcRefusal();
    if(!Refusal.IsEmpty()){SetFeedback(TEXT("没有提交：")+Refusal);return false;}
    FLDBuildCommand Command;
    Command.Kind=ELDBuildCommandKind::BuyNpcOffer;
    return Send(Command);
}
bool ULDChoicesPanel::Respec()
{
    if(!Usable()){return false;}
    if(PendingRequest){SetFeedback(TEXT("上一个请求仍在处理中。"));return false;}
    const FString Refusal=RespecRefusal();
    if(!Refusal.IsEmpty()){SetFeedback(TEXT("没有提交：")+Refusal);return false;}
    FLDBuildCommand Command;
    Command.Kind=ELDBuildCommandKind::Respec;
    return Send(Command);
}
bool ULDChoicesPanel::AskReplace(int32 Index,uint8 Key)
{
    if(!Usable()||!ShownBlueprints.IsValidIndex(Index)||Key>2||!Latest.PendingBlueprints.Contains(ShownBlueprints[Index])){return false;}
    const FName Active=LDHUD::ActiveInSlot(Latest,Key);
    if(Active.IsNone()){SetFeedback(TEXT("没有提交：")+LDHUD::GrowthReasonText(ELDGrowthReason::SlotEmpty));return false;}
    const FString Refusal=ReplaceRefusal();
    if(!Refusal.IsEmpty()){SetFeedback(TEXT("没有提交：")+Refusal);return false;}
    RestConfirm.Kind=ELDBuildCommandKind::ReplaceActive;RestConfirm.Blueprint=ShownBlueprints[Index];RestConfirm.Slot=Key;RestConfirm.Active=Active;
    SetFeedback(FString());
    RefreshRest();
    return true;
}
bool ULDChoicesPanel::AskForget(uint8 Key)
{
    if(!Usable()||Key>2){return false;}
    const FName Active=LDHUD::ActiveInSlot(Latest,Key);
    if(Active.IsNone()){SetFeedback(TEXT("没有提交：")+LDHUD::GrowthReasonText(ELDGrowthReason::SlotEmpty));return false;}
    const FString Refusal=ForgetRefusal();
    if(!Refusal.IsEmpty()){SetFeedback(TEXT("没有提交：")+Refusal);return false;}
    RestConfirm=FRestConfirm();
    RestConfirm.Kind=ELDBuildCommandKind::ForgetActive;RestConfirm.Slot=Key;RestConfirm.Active=Active;
    SetFeedback(FString());
    RefreshRest();
    return true;
}
bool ULDChoicesPanel::ConfirmRestAction()
{
    if(RestConfirm.Kind==ELDBuildCommandKind::None){return false;}
    if(PendingRequest){SetFeedback(TEXT("上一个请求仍在处理中。"));return false;}
    const bool bMaterial=RestConfirm.Kind==ELDBuildCommandKind::FillGap||RestConfirm.Kind==ELDBuildCommandKind::Dismantle;
    if(!RestConfirmHolds()){CancelRestAction();SetFeedback(bMaterial?TEXT("构件已变化，请重新选择。"):TEXT("技能槽或图纸已变化，请重新选择。"));return false;}
    const FString Refusal=RestConfirmRefusal();
    if(!Refusal.IsEmpty()){SetFeedback(TEXT("没有提交：")+Refusal);return false;}
    FLDBuildCommand Command;
    Command.Kind=RestConfirm.Kind;
    if(RestConfirm.Kind==ELDBuildCommandKind::FillGap)
    {
        // Component: the gap target; Filler: the unowned common base it receives.
        if(!FLDDefinitionWire::Encode(RestConfirm.GapTarget,Command.Component)||!FLDDefinitionWire::Encode(RestConfirm.GapFiller,Command.Filler))
        {SetFeedback(TEXT("没有提交：构件标识无效。"));return false;}
    }
    else if(RestConfirm.Kind==ELDBuildCommandKind::Dismantle)
    {
        for(const FName& Id:RestConfirm.Surrendered)
        {
            if(!FLDDefinitionWire::Encode(Id,Command.Surrendered.AddDefaulted_GetRef())){SetFeedback(TEXT("没有提交：构件标识无效。"));return false;}
        }
    }
    else
    {
        Command.Slot=RestConfirm.Slot;
        if(RestConfirm.Kind==ELDBuildCommandKind::ReplaceActive&&!FLDDefinitionWire::Encode(RestConfirm.Blueprint,Command.Blueprint))
        {SetFeedback(TEXT("没有提交：这份图纸的标识无效。"));return false;}
    }
    // The line is consumed by the attempt; the receipt or the refusal reason takes its place.
    RestConfirm=FRestConfirm();
    GrowthShown(RestConfirmLine,false);
    return Send(Command);
}
void ULDChoicesPanel::CancelRestAction()
{
    // A cancelled dismantle drops its selection, so the next pick starts over.
    if(RestConfirm.Kind==ELDBuildCommandKind::Dismantle){DismantlePicked.Reset();}
    RestConfirm=FRestConfirm();
    GrowthShown(RestConfirmLine,false);
}
bool ULDChoicesPanel::CloseChildConfirmation()
{
    if(RestConfirm.Kind==ELDBuildCommandKind::None){return false;}
    CancelRestAction();
    return true;
}
bool ULDChoicesPanel::Send(FLDBuildCommand Command)
{
    if(!SubmitBuild)
    {
        UE_LOG(LogLDUI,Error,TEXT("LD_GROWTH_SUBMIT_UNAVAILABLE The growth build entry is not connected to this panel."));
        SetFeedback(TEXT("成长入口不可用，本次没有提交。"));return false;
    }
    Command.ExpectedBuildRevision=Latest.BuildRevision;Command.CatalogDigest=Latest.CatalogDigest;
    // A Listen Host answers inside the submit call, so the pending entry is keyed to the id it will receive first.
    const uint64 Expected=PeekRequestId?PeekRequestId():0;
    const uint32 Before=Answers;PendingRequest=Expected;PendingKind=Command.Kind;
    ELDGrowthReason Reason=ELDGrowthReason::None;
    const uint64 Request=SubmitBuild(Command,Reason);
    if(!Request)
    {
        if(PendingRequest==Expected){PendingRequest=0;}
        const FString Why=LDHUD::GrowthReasonText(Reason);
        SetFeedback(TEXT("没有提交：")+(Why.IsEmpty()?FString(TEXT("原因未知")):Why));
        RefreshAll();
        return false;
    }
    if(Answers==Before){PendingRequest=Request;SetFeedback(FString());}
    RefreshAll();
    return true;
}
void ULDChoicesPanel::FinishRequest()
{
    PendingRequest=0;
    RefreshAll();
    // A result that arrives after the panel was closed still reaches the player, without reopening the panel; while it
    // is open the result is kept for the toast in case the panel closes itself with nothing left to do.
    if(!bOpen&&Notify&&Feedback){Notify(FString(LDTerms::Choice)+TEXT("：")+Feedback->GetText().ToString());}
    else if(Feedback){ResultNotice=Feedback->GetText().ToString();}
}
void ULDChoicesPanel::ShowFinal(const FLDBuildReceipt& Receipt)
{
    SetFeedback(LDHUD::BuildReceiptText(Receipt,LatestContent,LatestSafeAtMs,bLatestClock,LatestNowMs));
    // Applied points leave the staging; the rows show them once the owner view carries the receipt's revision.
    if(Receipt.Header.Outcome==ELDReceiptOutcome::Applied&&Receipt.Kind==ELDBuildCommandKind::AllocatePoints){for(int32& Count:Staged){Count=0;}}
    // #198 G: a done fill gap or dismantle closes its picker; the receipt line names what was received and removed.
    if(Receipt.Header.Outcome==ELDReceiptOutcome::Applied&&(Receipt.Kind==ELDBuildCommandKind::FillGap||Receipt.Kind==ELDBuildCommandKind::Dismantle))
    {CloseMaterialPickers();}
}
// The pending entry clears exactly when the controller releases its in-flight request: a final submit answer, an
// unreserved rejection, or a final/expired receipt. Other answers only explain the wait.
void ULDChoicesPanel::HandleSubmit(const FLDSubmitResult& Result)
{
    if(Result.Family!=ELDGrowthFamily::Build||!PendingRequest||Result.RequestId!=PendingRequest||Result.Generation!=LatestMatch){return;}
    ++Answers;
    switch(Result.Status)
    {
    case ELDAdmissionStatus::AcceptedPending: return;
    case ELDAdmissionStatus::AdmissionRejected:
        SetFeedback(TEXT("没有受理：")+LDHUD::GrowthReasonText(LDGrowthWire::Reason(Result.Reason)));
        if(Result.bSequenceReserved){return;}
        break;
    case ELDAdmissionStatus::Final:
        if(Result.bHasBuildReceipt){ShowFinal(Result.BuildReceipt);}else{SetFeedback(TEXT("请求已结束，请以当前状态为准。"));}
        break;
    default: SetFeedback(TEXT("无法识别服务器的答复，请以当前状态为准。"));return;
    }
    FinishRequest();
}
void ULDChoicesPanel::HandleReceipt(const FLDBuildReceiptQueryResult& Result)
{
    if(Result.Query.Family!=ELDGrowthFamily::Build||!PendingRequest||Result.Query.RequestId!=PendingRequest||Result.Query.Generation!=LatestMatch){return;}
    ++Answers;
    switch(Result.Lookup)
    {
    case ELDReceiptLookup::Pending: return;
    case ELDReceiptLookup::Final:
        if(Result.bHasReceipt){ShowFinal(Result.Receipt);}else{SetFeedback(TEXT("回执缺少结果，请以当前状态为准。"));}
        break;
    case ELDReceiptLookup::ReceiptExpired: SetFeedback(TEXT("回执已过期，请以当前状态为准。"));break;
    case ELDReceiptLookup::NotFound: SetFeedback(TEXT("服务器没有找到这次请求，请以当前状态为准。"));return;
    default: SetFeedback(TEXT("回执查询无效，请以当前状态为准。"));return;
    }
    FinishRequest();
}
