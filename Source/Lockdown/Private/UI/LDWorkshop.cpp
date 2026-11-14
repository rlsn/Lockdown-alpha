#include "UI/LDWorkshop.h"
#include "Core/LDLog.h"
#include "UI/LDChoicesPanel.h"
#include "UI/LDHUDStyle.h"
#include "UI/LDStatBar.h"
#include "UI/LDTerms.h"
#include "UI/LDTween.h"
#include "UI/LDUIFonts.h"
#include "UI/LDUIIcons.h"
#include "UI/LDUIShapes.h"
#include "UI/LDWidgets.h"
#include "UI/LDWorkshopModel.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/BackgroundBlur.h"
#include "Components/Border.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/RichTextBlock.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"

namespace
{
// Sizes in Slate units at the 1080p reference.
constexpr float WorkshopMargin=40.f;
constexpr float WorkshopTop=118.f;
constexpr float WorkshopGap=20.f;
constexpr float WorkshopCharacterWidth=352.f;
constexpr float WorkshopRightWidth=480.f;
constexpr float WorkshopDockHeight=260.f;
constexpr float WorkshopFieldShare=.46f;
constexpr float WorkshopFieldDockWidth=340.f;
constexpr int32 WorkshopMeterCells=16;
// Icons never draw under this many pixels; below it the board hides the names beside them.
constexpr float WorkshopMinIconPixels=36.f;

FSlateRoundedBoxBrush WorkshopRound(float Radius,const FLinearColor& Edge,float Width)
{FSlateRoundedBoxBrush Brush(FLinearColor::White,Radius,Edge,Width);Brush.OutlineSettings.bUseBrushTransparency=true;return Brush;}
void WorkshopText(UTextBlock* Text,const FString& Value)
{if(Text&&!Text->GetText().ToString().Equals(Value,ESearchCase::CaseSensitive)){Text->SetText(FText::FromString(Value));}}
void WorkshopShown(UWidget* Widget,bool bShown,ESlateVisibility Shown=ESlateVisibility::SelfHitTestInvisible)
{
    if(!Widget){return;}
    const ESlateVisibility Next=bShown?Shown:ESlateVisibility::Collapsed;
    if(Widget->GetVisibility()!=Next){Widget->SetVisibility(Next);}
}
bool WorkshopHasArea(const FBox2D& Box){return Box.bIsValid&&Box.GetSize().X>1.f&&Box.GetSize().Y>1.f;}
FBox2D WorkshopBox(float X0,float Y0,float X1,float Y1){return FBox2D(FVector2D(X0,Y0),FVector2D(X1,Y1));}
FLinearColor WorkshopElementColor(ELDDemoElement Element)
{
    switch(Element)
    {
    case ELDDemoElement::Fire: return FLinearColor(FColor(0xFF,0x6A,0x3D));
    case ELDDemoElement::Water: return FLinearColor(FColor(0x3F,0x9C,0xF0));
    case ELDDemoElement::Electric: return FLinearColor(FColor(0xFF,0xE1,0x4A));
    default: return LDHUDStyle::TextSecondary;
    }
}
FString WorkshopStripParens(const FString& Text)
{
    FString Out=Text;Out.RemoveFromStart(TEXT("（"));Out.RemoveFromEnd(TEXT("）"));Out.RemoveFromStart(TEXT("装在："));
    return Out;
}
void WorkshopTint(ULDCommandButton* Button,bool bSelected)
{
    if(!Button){return;}
    FButtonStyle Style=Button->GetStyle();
    const FLinearColor Fill=bSelected?LDHUDStyle::UIAction:LDHUDStyle::UIRaised;
    const FLinearColor Edge=bSelected?LDHUDStyle::UIAction:LDHUDStyle::PanelRim;
    Style.Normal=WorkshopRound(LDHUDStyle::RadiusButton,Edge,1.f);Style.Normal.TintColor=FSlateColor(Fill);
    Style.Hovered=WorkshopRound(LDHUDStyle::RadiusButton,LDHUDStyle::UIAction,1.f);Style.Hovered.TintColor=FSlateColor(bSelected?Fill:LDHUDStyle::UIPanel);
    Style.Pressed=Style.Hovered;
    Button->SetStyle(Style);
    if(UTextBlock* Caption=Cast<UTextBlock>(Button->GetContent())){Caption->SetColorAndOpacity(bSelected?LDHUDStyle::UIBase:LDHUDStyle::UIText);}
}
}

double ULDWorkshop::UiNow() const
{
    return GetWorld()?GetWorld()->GetRealTimeSeconds():0.;
}

TSharedRef<SWidget> ULDWorkshop::RebuildWidget()
{
    if(!Canvas)
    {
        SetVisibility(ESlateVisibility::Collapsed);
        Canvas=WidgetTree->ConstructWidget<UCanvasPanel>();Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);WidgetTree->RootWidget=Canvas;
        auto Add=[this](UWidget* Widget,int32 Z,ESlateVisibility Shown=ESlateVisibility::SelfHitTestInvisible)
        {UCanvasPanelSlot* Slot=Canvas->AddChildToCanvas(Widget);Slot->SetZOrder(Z);Slot->SetAutoSize(false);Slots.Add(Widget,Slot);ShownAs.Add(Widget,Shown);Widget->SetVisibility(ESlateVisibility::Collapsed);return Slot;};
        auto Label=[this](const FString& Value,int32 Size,const FLinearColor& Ink,LDUIFonts::ERole Role=LDUIFonts::ERole::Body)
        {UTextBlock* Text=LDWidgets::Text(WidgetTree,Value,Size,NAME_None,Role);Text->SetAutoWrapText(false);Text->SetColorAndOpacity(Ink);return Text;};
        auto Surface=[this](UWidget* Child,FMargin Inset)
        {UBorder* Panel=LDWidgets::Panel(WidgetTree,Child,Inset);Panel->SetBrushColor(LDHUDStyle::UIPanel.CopyWithNewOpacity(.94f));return Panel;};
        // The world behind the full workshop darkens and blurs (only darkens at low quality); it is never paused.
        Blur=WidgetTree->ConstructWidget<UBackgroundBlur>();Blur->SetBlurStrength(0.f);Blur->SetApplyAlphaToBlur(true);
        Shade=WidgetTree->ConstructWidget<UBorder>();Shade->SetBrushColor(LDHUDStyle::UIBase.CopyWithNewOpacity(.6f));
        Blur->SetContent(Shade);Add(Blur,0,ESlateVisibility::Visible);
        {
            // The phase bar: the step's remaining time inside its ring, the rest and step, and the step dots (painted).
            auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();
            PhaseClock=Label(TEXT(""),LDHUDStyle::TypeCaption,LDHUDStyle::UIText,LDUIFonts::ERole::Number);PhaseClock->SetJustification(ETextJustify::Center);
            auto* ClockBox=WidgetTree->ConstructWidget<USizeBox>();ClockBox->SetWidthOverride(48.f);ClockBox->SetHeightOverride(48.f);
            ClockBox->SetContent(PhaseClock);
            if(USizeBoxSlot* ClockSlot=Cast<USizeBoxSlot>(ClockBox->GetContentSlot())){ClockSlot->SetHorizontalAlignment(HAlign_Center);ClockSlot->SetVerticalAlignment(VAlign_Center);}
            Row->AddChildToHorizontalBox(ClockBox)->SetVerticalAlignment(VAlign_Center);
            auto* Names=WidgetTree->ConstructWidget<UVerticalBox>();
            PhaseRest=Label(TEXT(""),LDHUDStyle::TypeTiny,LDHUDStyle::TextSecondary);Names->AddChildToVerticalBox(PhaseRest);
            PhaseStep=Label(TEXT(""),LDHUDStyle::TypeHeading,LDHUDStyle::UIText,LDUIFonts::ERole::Title);Names->AddChildToVerticalBox(PhaseStep);
            Row->AddChildToHorizontalBox(Names)->SetPadding(FMargin(14,0,0,0));
            PhasePanel=Surface(Row,FMargin(14,8));Add(PhasePanel,2);
            CloseButton=LDWidgets::Button(WidgetTree,LDTerms::WorkshopClose,[this](){if(RequestClose){RequestClose();}},true,TEXT("WorkshopClose"));
            Add(CloseButton,3,ESlateVisibility::Visible);
        }
        {
            // The character column: the holder, the unspent points, one row per stat with its "+", the derived attributes and
            // the three element reactions.
            auto* Column=WidgetTree->ConstructWidget<UVerticalBox>();
            auto* Head=WidgetTree->ConstructWidget<UHorizontalBox>();
            auto* PortraitStack=WidgetTree->ConstructWidget<UOverlay>();
            PortraitGlyph=Label(TEXT(""),LDHUDStyle::TypeSection,FLinearColor::White,LDUIFonts::ERole::Title);PortraitGlyph->SetJustification(ETextJustify::Center);
            Portrait=WidgetTree->ConstructWidget<UBorder>();Portrait->SetBrush(WorkshopRound(36.f,LDHUDStyle::UIAction,2.f));
            Portrait->SetHorizontalAlignment(HAlign_Center);Portrait->SetVerticalAlignment(VAlign_Center);Portrait->SetContent(PortraitGlyph);
            auto* PortraitFill=PortraitStack->AddChildToOverlay(Portrait);PortraitFill->SetHorizontalAlignment(HAlign_Fill);PortraitFill->SetVerticalAlignment(VAlign_Fill);
            LevelBadge=Label(TEXT(""),LDHUDStyle::TypeSmall,LDHUDStyle::UIBase,LDUIFonts::ERole::Number);
            auto* Badge=WidgetTree->ConstructWidget<UBorder>();Badge->SetBrush(WorkshopRound(4.f,LDHUDStyle::UIBase,1.f));Badge->SetBrushColor(LDHUDStyle::UIAction);
            Badge->SetPadding(FMargin(5,0));Badge->SetContent(LevelBadge);
            auto* BadgeSlot=PortraitStack->AddChildToOverlay(Badge);BadgeSlot->SetHorizontalAlignment(HAlign_Right);BadgeSlot->SetVerticalAlignment(VAlign_Bottom);
            auto* PortraitBox=WidgetTree->ConstructWidget<USizeBox>();PortraitBox->SetWidthOverride(72.f);PortraitBox->SetHeightOverride(72.f);PortraitBox->SetContent(PortraitStack);
            Head->AddChildToHorizontalBox(PortraitBox);
            auto* Names=WidgetTree->ConstructWidget<UVerticalBox>();
            HolderName=Label(TEXT(""),LDHUDStyle::TypeHeading,LDHUDStyle::UIText,LDUIFonts::ERole::Title);HolderName->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
            Names->AddChildToVerticalBox(HolderName);
            LevelLine=Label(TEXT(""),LDHUDStyle::TypeTiny,LDHUDStyle::TextSecondary);Names->AddChildToVerticalBox(LevelLine)->SetPadding(FMargin(0,2,0,0));
            auto* NamesSlot=Head->AddChildToHorizontalBox(Names);NamesSlot->SetVerticalAlignment(VAlign_Center);NamesSlot->SetPadding(FMargin(14,0,0,0));
            NamesSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            Column->AddChildToVerticalBox(Head);
            PointsText=Label(TEXT(""),LDHUDStyle::TypeLabel,LDHUDStyle::UIAction,LDUIFonts::ERole::Strong);
            PointsBox=WidgetTree->ConstructWidget<UBorder>();PointsBox->SetBrush(WorkshopRound(LDHUDStyle::RadiusButton,LDHUDStyle::UIAction.CopyWithNewOpacity(.7f),1.f));
            PointsBox->SetBrushColor(LDHUDStyle::UIAction.CopyWithNewOpacity(.10f));PointsBox->SetPadding(FMargin(16,10));PointsBox->SetContent(PointsText);
            Column->AddChildToVerticalBox(PointsBox)->SetPadding(FMargin(0,18,0,0));
            StatList=WidgetTree->ConstructWidget<UVerticalBox>();Column->AddChildToVerticalBox(StatList)->SetPadding(FMargin(0,14,0,0));
            AttributeList=WidgetTree->ConstructWidget<UVerticalBox>();Column->AddChildToVerticalBox(AttributeList)->SetPadding(FMargin(0,16,0,0));
            UTextBlock* ReactionTitle=Label(LDTerms::WorkshopReactions,LDHUDStyle::TypeTiny,LDHUDStyle::TextSecondary);
            Column->AddChildToVerticalBox(ReactionTitle)->SetPadding(FMargin(0,18,0,6));
            for(int32 Index=0;Index<3;++Index)
            {
                FReaction& Reaction=Reactions.AddDefaulted_GetRef();
                auto* Line=WidgetTree->ConstructWidget<UHorizontalBox>();
                auto* Pair=Label(TEXT(""),LDHUDStyle::TypeSmall,LDHUDStyle::UIText,LDUIFonts::ERole::Strong);Reaction.Pair=Pair;
                Line->AddChildToHorizontalBox(Pair)->SetVerticalAlignment(VAlign_Center);
                Line->AddChildToHorizontalBox(WidgetTree->ConstructWidget<USpacer>())->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
                Reaction.Name=Label(TEXT(""),LDHUDStyle::TypeSmall,LDHUDStyle::TextSecondary,LDUIFonts::ERole::Strong);
                Line->AddChildToHorizontalBox(Reaction.Name)->SetVerticalAlignment(VAlign_Center);
                Reaction.Row=WidgetTree->ConstructWidget<UBorder>();Reaction.Row->SetPadding(FMargin(12,7));Reaction.Row->SetContent(Line);
                Reaction.Row->SetBrush(WorkshopRound(LDHUDStyle::RadiusButton,LDHUDStyle::PanelRim,1.f));Reaction.Row->SetBrushColor(LDHUDStyle::UIRaised.CopyWithNewOpacity(.6f));
                Column->AddChildToVerticalBox(Reaction.Row)->SetPadding(FMargin(0,Index?6.f:0.f,0,0));
                Built.Add(Pair);Built.Add(Line);
            }
            CharacterPanel=Surface(Column,FMargin(22,20));Add(CharacterPanel,2);
        }
        {
            // The board: its surface, the capacity row (cells painted) and one head per column; the storage lays the
            // installed components out under the heads.
            BoardPanel=Surface(nullptr,FMargin(0));Add(BoardPanel,1);
            CapacityTitle=Label(LDTerms::WorkshopCapacityTitle,LDHUDStyle::TypeCaption,LDHUDStyle::TextSecondary);Add(CapacityTitle,3);
            CapacityCount=Label(TEXT(""),LDHUDStyle::TypeLead,LDHUDStyle::UIText,LDUIFonts::ERole::Number);Add(CapacityCount,3);
            CapacityUnlock=Label(TEXT(""),LDHUDStyle::TypeTiny,LDHUDStyle::TextSecondary);CapacityUnlock->SetJustification(ETextJustify::Right);Add(CapacityUnlock,3);
            LockBadge=Label(LDTerms::WorkshopLocked,LDHUDStyle::TypeSmall,LDHUDStyle::UIReject,LDUIFonts::ERole::Strong);LockBadge->SetJustification(ETextJustify::Right);
            LockBadge->SetVisibility(ESlateVisibility::Collapsed);Add(LockBadge,3);
            for(int32 Index=0;Index<LDHUD::StorageColumnCount;++Index)
            {
                UBorder* Back=WidgetTree->ConstructWidget<UBorder>();Back->SetBrush(WorkshopRound(8.f,LDHUDStyle::PanelRim.CopyWithNewOpacity(.25f),1.f));
                Back->SetBrushColor(LDHUDStyle::UIRaised.CopyWithNewOpacity(.45f));Back->SetVisibility(ESlateVisibility::HitTestInvisible);Add(Back,2);ColumnBacks.Add(Back);
                FHead& Head=Heads.AddDefaulted_GetRef();
                auto* Box=WidgetTree->ConstructWidget<UVerticalBox>();
                auto* IconStack=WidgetTree->ConstructWidget<UOverlay>();
                Head.Picture=WidgetTree->ConstructWidget<UImage>();Head.Picture->SetVisibility(ESlateVisibility::Collapsed);
                Head.Glyph=Label(TEXT(""),LDHUDStyle::TypeSection,FLinearColor::White,LDUIFonts::ERole::Title);Head.Glyph->SetJustification(ETextJustify::Center);
                auto* Inner=WidgetTree->ConstructWidget<UOverlay>();
                auto* PictureSlot=Inner->AddChildToOverlay(Head.Picture);PictureSlot->SetHorizontalAlignment(HAlign_Fill);PictureSlot->SetVerticalAlignment(VAlign_Fill);
                auto* GlyphSlot=Inner->AddChildToOverlay(Head.Glyph);GlyphSlot->SetHorizontalAlignment(HAlign_Center);GlyphSlot->SetVerticalAlignment(VAlign_Center);
                Head.IconFace=WidgetTree->ConstructWidget<UBorder>();Head.IconFace->SetBrush(WorkshopRound(10.f,FLinearColor::Transparent,0.f));
                Head.IconFace->SetBrushColor(LDHUDStyle::IconFill);Head.IconFace->SetPadding(FMargin(0));Head.IconFace->SetContent(Inner);
                Head.IconFace->SetHorizontalAlignment(HAlign_Fill);Head.IconFace->SetVerticalAlignment(VAlign_Fill);
                auto* FaceSlot=IconStack->AddChildToOverlay(Head.IconFace);FaceSlot->SetHorizontalAlignment(HAlign_Fill);FaceSlot->SetVerticalAlignment(VAlign_Fill);
                Head.Key=Label(TEXT(""),LDHUDStyle::TypeSmall,LDHUDStyle::UIBase,LDUIFonts::ERole::Strong);
                auto* KeyChip=WidgetTree->ConstructWidget<UBorder>();KeyChip->SetBrush(WorkshopRound(3.f,LDHUDStyle::UIBase,1.f));KeyChip->SetBrushColor(LDHUDStyle::UIAction);
                KeyChip->SetPadding(FMargin(6,0));KeyChip->SetContent(Head.Key);KeyChip->SetRenderTranslation(FVector2D(-8,-8));
                auto* KeySlot=IconStack->AddChildToOverlay(KeyChip);KeySlot->SetHorizontalAlignment(HAlign_Left);KeySlot->SetVerticalAlignment(VAlign_Top);
                Head.Element=Label(TEXT(""),LDHUDStyle::TypeTiny,LDHUDStyle::UIBase,LDUIFonts::ERole::Strong);
                auto* ElementChip=WidgetTree->ConstructWidget<UBorder>();ElementChip->SetBrush(WorkshopRound(9.f,LDHUDStyle::UIBase,1.f));
                ElementChip->SetPadding(FMargin(5,0));ElementChip->SetContent(Head.Element);ElementChip->SetRenderTranslation(FVector2D(6,6));
                ElementChip->SetVisibility(ESlateVisibility::Collapsed);Built.Add(ElementChip);
                auto* ElementSlot=IconStack->AddChildToOverlay(ElementChip);ElementSlot->SetHorizontalAlignment(HAlign_Right);ElementSlot->SetVerticalAlignment(VAlign_Bottom);
                Head.IconBox=WidgetTree->ConstructWidget<USizeBox>();Head.IconBox->SetWidthOverride(100.f);Head.IconBox->SetHeightOverride(100.f);Head.IconBox->SetContent(IconStack);
                Box->AddChildToVerticalBox(Head.IconBox)->SetHorizontalAlignment(HAlign_Center);
                Head.Name=Label(TEXT(""),LDHUDStyle::TypeLead,LDHUDStyle::UIText,LDUIFonts::ERole::Title);Head.Name->SetJustification(ETextJustify::Center);
                Head.Name->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
                auto* NameSlot=Box->AddChildToVerticalBox(Head.Name);NameSlot->SetHorizontalAlignment(HAlign_Fill);NameSlot->SetPadding(FMargin(0,8,0,0));
                auto* Chips=WidgetTree->ConstructWidget<UHorizontalBox>();
                Head.Mana=Label(TEXT(""),LDHUDStyle::TypeTiny,LDHUDStyle::UIText,LDUIFonts::ERole::Number);
                Head.Cooldown=Label(TEXT(""),LDHUDStyle::TypeTiny,LDHUDStyle::UIText,LDUIFonts::ERole::Number);
                Chips->AddChildToHorizontalBox(Head.Mana);
                Chips->AddChildToHorizontalBox(Head.Cooldown)->SetPadding(FMargin(10,0,0,0));
                Box->AddChildToVerticalBox(Chips)->SetHorizontalAlignment(HAlign_Center);
                auto* HeadFrame=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),FName(*FString::Printf(TEXT("WorkshopHead%d"),Index)));
                HeadFrame->SetBrushColor(FLinearColor::Transparent);HeadFrame->SetPadding(FMargin(8,10,8,0));HeadFrame->SetContent(Box);
                HeadFrame->SetHorizontalAlignment(HAlign_Fill);HeadFrame->SetVisibility(ESlateVisibility::Visible);
                Head.Box=HeadFrame;Add(HeadFrame,3,ESlateVisibility::Visible);
                Built.Add(Box);Built.Add(IconStack);Built.Add(Inner);Built.Add(KeyChip);Built.Add(Chips);
            }
        }
        {
            // The library surface: its title, count and use-tag filter; the storage places the components under them.
            LibraryPanel=Surface(nullptr,FMargin(0));Add(LibraryPanel,1);
            auto* Title=WidgetTree->ConstructWidget<UHorizontalBox>();
            Title->AddChildToHorizontalBox(Label(LDTerms::Storage,LDHUDStyle::TypeSection,LDHUDStyle::UIText,LDUIFonts::ERole::Title));
            LibraryCount=Label(TEXT(""),LDHUDStyle::TypeLead,LDHUDStyle::TextSecondary,LDUIFonts::ERole::Number);
            Title->AddChildToHorizontalBox(LibraryCount)->SetPadding(FMargin(12,4,0,0));
            LibraryTitle=Title;Add(Title,3);
            FilterRow=WidgetTree->ConstructWidget<UWrapBox>();FilterRow->SetInnerSlotPadding(FVector2D(6,6));
            for(int32 Tag=0;Tag<int32(ELDDemoComponentTag::Count);++Tag)
            {
                ULDCommandButton* Chip=LDWidgets::Button(WidgetTree,LDTerms::ComponentTagName(uint8(Tag)),[this,Tag](){TagFilter=TagFilter==Tag?INDEX_NONE:Tag;
                    for(int32 Each=0;Each<FilterButtons.Num();++Each){WorkshopTint(FilterButtons[Each],Each==TagFilter);}},true,FName(*FString::Printf(TEXT("WorkshopTag%d"),Tag)));
                {auto Style=Chip->GetStyle();Style.NormalPadding=FMargin(8,2);Style.PressedPadding=FMargin(8,2);Chip->SetStyle(Style);}
                if(UTextBlock* Caption=Cast<UTextBlock>(Chip->GetContent())){auto Font=Caption->GetFont();Font.Size=LDHUDStyle::TypeTiny;Caption->SetFont(Font);}
                WorkshopTint(Chip,false);
                FilterRow->AddChildToWrapBox(Chip);FilterButtons.Add(Chip);
            }
            Add(FilterRow,3,ESlateVisibility::Visible);
            PendingPanel=Surface(nullptr,FMargin(0));Add(PendingPanel,1);
            PendingTitle=Label(TEXT(""),LDHUDStyle::TypeCaption,LDHUDStyle::TextSecondary,LDUIFonts::ERole::Strong);Add(PendingTitle,3);
        }
        {
            // The info dock: left the big icon with its kind and tags, centre the name, rule and triggers, right where it sits
            // or goes, what it links with and what to watch.
            DockPanel=Surface(nullptr,FMargin(0));Add(DockPanel,1);
            auto* Left=WidgetTree->ConstructWidget<UVerticalBox>();
            auto* IconInner=WidgetTree->ConstructWidget<UOverlay>();
            DockPicture=WidgetTree->ConstructWidget<UImage>();DockPicture->SetVisibility(ESlateVisibility::Collapsed);
            DockGlyph=Label(TEXT(""),LDHUDStyle::TypeBanner,FLinearColor::White,LDUIFonts::ERole::Title);DockGlyph->SetJustification(ETextJustify::Center);
            auto* PictureSlot=IconInner->AddChildToOverlay(DockPicture);PictureSlot->SetHorizontalAlignment(HAlign_Fill);PictureSlot->SetVerticalAlignment(VAlign_Fill);
            auto* GlyphSlot=IconInner->AddChildToOverlay(DockGlyph);GlyphSlot->SetHorizontalAlignment(HAlign_Center);GlyphSlot->SetVerticalAlignment(VAlign_Center);
            DockIconFace=WidgetTree->ConstructWidget<UBorder>();DockIconFace->SetBrush(WorkshopRound(10.f,FLinearColor::Transparent,0.f));DockIconFace->SetBrushColor(LDHUDStyle::IconFill);
            DockIconFace->SetPadding(FMargin(0));DockIconFace->SetHorizontalAlignment(HAlign_Fill);DockIconFace->SetVerticalAlignment(VAlign_Fill);DockIconFace->SetContent(IconInner);
            DockIconBox=WidgetTree->ConstructWidget<USizeBox>();DockIconBox->SetWidthOverride(112.f);DockIconBox->SetHeightOverride(112.f);DockIconBox->SetContent(DockIconFace);
            Left->AddChildToVerticalBox(DockIconBox)->SetHorizontalAlignment(HAlign_Center);
            DockKind=Label(TEXT(""),LDHUDStyle::TypeTiny,LDHUDStyle::UIText,LDUIFonts::ERole::Strong);DockKind->SetJustification(ETextJustify::Center);
            Left->AddChildToVerticalBox(DockKind)->SetPadding(FMargin(0,12,0,0));
            DockTags=Label(TEXT(""),LDHUDStyle::TypeTiny,LDHUDStyle::TextSecondary);DockTags->SetJustification(ETextJustify::Center);DockTags->SetAutoWrapText(true);
            Left->AddChildToVerticalBox(DockTags)->SetPadding(FMargin(0,4,0,0));
            DockLeft=Left;Add(Left,3);
            auto* Centre=WidgetTree->ConstructWidget<UVerticalBox>();
            DockName=Label(TEXT(""),LDHUDStyle::TypePage,LDHUDStyle::UIText,LDUIFonts::ERole::Title);DockName->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
            Centre->AddChildToVerticalBox(DockName);
            DockSummary=Label(TEXT(""),LDHUDStyle::TypeSmall,LDHUDStyle::TextSecondary);DockSummary->SetAutoWrapText(true);
            Centre->AddChildToVerticalBox(DockSummary)->SetPadding(FMargin(0,2,0,0));
            DockBody=LDWidgets::RichText(WidgetTree,LDHUDStyle::TypeText,TEXT("WorkshopDockBody"));DockBody->SetAutoWrapText(true);
            Centre->AddChildToVerticalBox(DockBody)->SetPadding(FMargin(0,8,0,0));
            DockChips=Label(TEXT(""),LDHUDStyle::TypeSmall,LDHUDStyle::ComponentLink,LDUIFonts::ERole::Strong);DockChips->SetAutoWrapText(true);
            Centre->AddChildToVerticalBox(DockChips)->SetPadding(FMargin(0,10,0,0));
            DockCentre=Centre;Add(Centre,3);
            auto* Right=WidgetTree->ConstructWidget<UVerticalBox>();
            const auto Section=[&](TObjectPtr<UTextBlock>& Title,TObjectPtr<UTextBlock>& Body,const TCHAR* Caption,bool bFirst)
            {
                Title=Label(Caption,LDHUDStyle::TypeTiny,LDHUDStyle::TextSecondary);Right->AddChildToVerticalBox(Title)->SetPadding(FMargin(0,bFirst?0.f:12.f,0,0));
                Body=Label(TEXT(""),LDHUDStyle::TypeSmall,LDHUDStyle::UIText,LDUIFonts::ERole::Strong);Body->SetAutoWrapText(true);
                Right->AddChildToVerticalBox(Body)->SetPadding(FMargin(0,4,0,0));
            };
            Section(DockWhereTitle,DockWhere,LDTerms::WorkshopInstalledOn,true);
            Section(DockLinksTitle,DockLinks,LDTerms::WorkshopLinks,false);
            Section(DockNotesTitle,DockNotes,LDTerms::WorkshopNotes,false);
            DockRight=Right;Add(Right,3);
        }
    }
    return Super::RebuildWidget();
}

void ULDWorkshop::Place(UWidget* Widget,const FBox2D& Box)
{
    UCanvasPanelSlot* const* Found=Slots.Find(Widget);
    if(!Found||!*Found){return;}
    const bool bArea=WorkshopHasArea(Box);
    const ESlateVisibility* Shown=ShownAs.Find(Widget);
    WorkshopShown(Widget,bArea,Shown?*Shown:ESlateVisibility::SelfHitTestInvisible);
    if(!bArea){return;}
    const FVector2D Position(FMath::RoundToFloat(float(Box.Min.X)),FMath::RoundToFloat(float(Box.Min.Y)));
    const FVector2D Size(FMath::RoundToFloat(float(Box.GetSize().X)),FMath::RoundToFloat(float(Box.GetSize().Y)));
    if(!(*Found)->GetPosition().Equals(Position,.5f)){(*Found)->SetPosition(Position);}
    if(!(*Found)->GetSize().Equals(Size,.5f)){(*Found)->SetSize(Size);}
}

float ULDWorkshop::LibraryIconFor(const FBox2D& Box) const
{
    // The largest of the library's sizes whose groups all fit the rectangle; nothing scrolls.
    const ULDComponentStorage* Source=Storage.Get();
    if(!Source||!WorkshopHasArea(Box)){return 0.f;}
    int32 Counts[3]={0,0,0};
    for(const FName& Id:Source->Model().Stored){const uint8 Kind=uint8(LDWorkshop::ComponentFacts(Id,Latest.Content).Kind);if(Kind<3){++Counts[Kind];}}
    const float Tier=LDWorkshop::LibraryTier(Source->Model().Stored.Num()).Icon;
    const float Width=float(Box.GetSize().X),Height=float(Box.GetSize().Y);
    for(const float Size:{68.f,56.f,46.f,40.f})
    {
        if(Size>Tier){continue;}
        const float Cell=Size+14.f,Gap=8.f;
        const int32 PerRow=FMath::Max(1,FMath::FloorToInt((Width+Gap)/(Cell+Gap)));
        float Needed=0.f;int32 Groups=0;
        for(const int32 Count:Counts)
        {
            if(Count<=0){continue;}
            const int32 Rows=(Count+PerRow-1)/PerRow;
            Needed+=(Groups?14.f:0.f)+22.f+Rows*(Size+4.f+16.f)+(Rows-1)*Gap;++Groups;
        }
        if(Needed<=Height){return Size;}
    }
    return 36.f;
}

void ULDWorkshop::Layout(FVector2D Size)
{
    FRects Next;
    const float W=float(Size.X),H=float(Size.Y);
    if(Mode==EMode::Full)
    {
        const float Bottom=H-WorkshopMargin;
        Next.Region=WorkshopBox(0,0,W,H);
        Next.Phase=WorkshopBox(WorkshopMargin,28,WorkshopMargin+464,92);
        Next.Character=WorkshopBox(WorkshopMargin,WorkshopTop,WorkshopMargin+WorkshopCharacterWidth,Bottom);
        const float RightX=W-WorkshopMargin-WorkshopRightWidth;
        const float Tray=FMath::Clamp(float(Latest.TrayDesired.Y),0.f,H*.5f);
        const float PendingHeight=Tray>0.f?Tray+52.f:72.f;
        Next.Pending=WorkshopBox(RightX,Bottom-PendingHeight,W-WorkshopMargin,Bottom);
        Next.Library=WorkshopBox(RightX,WorkshopTop,W-WorkshopMargin,Next.Pending.Min.Y-WorkshopGap);
        Next.Board=WorkshopBox(Next.Character.Max.X+WorkshopGap,WorkshopTop,RightX-WorkshopGap,Bottom-WorkshopDockHeight-WorkshopGap);
        Next.Dock=WorkshopBox(Next.Board.Min.X,Next.Board.Max.Y+WorkshopGap,Next.Board.Max.X,Bottom);
        Next.HeadIcon=100.f;
        ActionsAt=FVector2D(W-WorkshopMargin-86.f,30.f);
    }
    else
    {
        const float Top=H*(1.f-WorkshopFieldShare);
        const float Bottom=H-16.f,Inner=Top+16.f,Margin=24.f;
        Next.Region=WorkshopBox(0,Top,W,H);
        const float RightX=W-Margin-WorkshopRightWidth;
        const float Tray=FMath::Clamp(float(Latest.TrayDesired.Y),0.f,(Bottom-Inner)*.6f);
        const float PendingHeight=Tray>0.f?Tray+52.f:0.f;
        Next.Pending=PendingHeight>0.f?WorkshopBox(RightX,Bottom-PendingHeight,W-Margin,Bottom):FBox2D(ForceInit);
        Next.Library=WorkshopBox(RightX,Inner,W-Margin,PendingHeight>0.f?Next.Pending.Min.Y-12.f:Bottom);
        Next.Dock=WorkshopBox(Margin,Inner,Margin+WorkshopFieldDockWidth,Bottom);
        Next.Board=WorkshopBox(Next.Dock.Max.X+16.f,Inner,RightX-16.f,Bottom);
        Next.HeadIcon=64.f;
        ActionsAt=FVector2D(W-WorkshopMargin-86.f,30.f);
    }
    Next.Capacity=WorkshopBox(Next.Board.Min.X+24.f,Next.Board.Min.Y+14.f,Next.Board.Max.X-24.f,Next.Board.Min.Y+50.f);
    Next.Columns=WorkshopBox(Next.Board.Min.X+16.f,Next.Capacity.Max.Y+12.f,Next.Board.Max.X-16.f,Next.Board.Max.Y-14.f);
    Next.HeadHeight=Next.HeadIcon+(Mode==EMode::Full?78.f:64.f);
    const float ColumnGap=12.f;
    const float ColumnWidth=(float(Next.Columns.GetSize().X)-ColumnGap*(LDHUD::StorageColumnCount-1))/LDHUD::StorageColumnCount;
    for(int32 Index=0;Index<LDHUD::StorageColumnCount;++Index)
    {
        const float X=float(Next.Columns.Min.X)+Index*(ColumnWidth+ColumnGap);
        Next.Column[Index]=WorkshopBox(X,Next.Columns.Min.Y,X+ColumnWidth,Next.Columns.Max.Y);
    }
    // The library body sits under its title and filter rows.
    Next.LibraryBody=WorkshopBox(Next.Library.Min.X+24.f,Next.Library.Min.Y+124.f,Next.Library.Max.X-20.f,Next.Library.Max.Y-14.f);
    Rects=Next;
    TrayRight=WorkshopHasArea(Rects.Pending)?W-float(Rects.Pending.Max.X)+12.f:W-float(Rects.Library.Max.X);
    TrayBottom=WorkshopHasArea(Rects.Pending)?H-float(Rects.Pending.Max.Y)+12.f:H-float(Rects.Library.Max.Y);
    const bool bFull=Mode==EMode::Full;
    Place(Blur,Rects.Region);
    Place(PhasePanel,bFull?Rects.Phase:FBox2D(ForceInit));
    Place(CloseButton,WorkshopBox(W-WorkshopMargin-74.f,bFull?34.f:float(Rects.Region.Min.Y)-50.f,W-WorkshopMargin,bFull?80.f:float(Rects.Region.Min.Y)-8.f));
    Place(CharacterPanel,bFull?Rects.Character:FBox2D(ForceInit));
    Place(BoardPanel,Rects.Board);
    Place(DockPanel,Rects.Dock);
    Place(LibraryPanel,Rects.Library);
    Place(PendingPanel,Rects.Pending);
    Place(CapacityTitle,WorkshopBox(Rects.Capacity.Min.X,Rects.Capacity.Min.Y+8.f,Rects.Capacity.Min.X+96.f,Rects.Capacity.Max.Y));
    Place(CapacityCount,WorkshopBox(Rects.Capacity.Max.X-260.f,Rects.Capacity.Min.Y+2.f,Rects.Capacity.Max.X-180.f,Rects.Capacity.Max.Y));
    Place(CapacityUnlock,WorkshopBox(Rects.Capacity.Max.X-176.f,Rects.Capacity.Min.Y+10.f,Rects.Capacity.Max.X,Rects.Capacity.Max.Y));
    Place(LockBadge,WorkshopBox(Rects.Capacity.Max.X-176.f,Rects.Capacity.Min.Y-10.f,Rects.Capacity.Max.X,Rects.Capacity.Min.Y+10.f));
    for(int32 Index=0;Index<Heads.Num();++Index)
    {
        const FBox2D& Column=Rects.Column[Index];
        Place(ColumnBacks[Index],Column);
        Place(Heads[Index].Box,WorkshopBox(Column.Min.X,Column.Min.Y,Column.Max.X,Column.Min.Y+Rects.HeadHeight));
        if(!FMath::IsNearlyEqual(Heads[Index].IconBox->GetWidthOverride(),Rects.HeadIcon)){Heads[Index].IconBox->SetWidthOverride(Rects.HeadIcon);Heads[Index].IconBox->SetHeightOverride(Rects.HeadIcon);}
    }
    Place(LibraryTitle,WorkshopBox(Rects.Library.Min.X+24.f,Rects.Library.Min.Y+16.f,Rects.Library.Max.X-20.f,Rects.Library.Min.Y+56.f));
    if(Mode==EMode::Full)
    {
        Place(DockLeft,WorkshopBox(Rects.Dock.Min.X+20.f,Rects.Dock.Min.Y+20.f,Rects.Dock.Min.X+176.f,Rects.Dock.Max.Y-12.f));
        Place(DockCentre,WorkshopBox(Rects.Dock.Min.X+196.f,Rects.Dock.Min.Y+18.f,Rects.Dock.Max.X-300.f,Rects.Dock.Max.Y-12.f));
        Place(DockRight,WorkshopBox(Rects.Dock.Max.X-280.f,Rects.Dock.Min.Y+20.f,Rects.Dock.Max.X-20.f,Rects.Dock.Max.Y-12.f));
    }
    else
    {
        // The field dock stacks the name and rule under the icon and puts where it goes beside the icon.
        Place(DockLeft,WorkshopBox(Rects.Dock.Min.X+20.f,Rects.Dock.Min.Y+16.f,Rects.Dock.Min.X+140.f,Rects.Dock.Min.Y+146.f));
        Place(DockRight,WorkshopBox(Rects.Dock.Min.X+150.f,Rects.Dock.Min.Y+16.f,Rects.Dock.Max.X-20.f,Rects.Dock.Min.Y+146.f));
        Place(DockCentre,WorkshopBox(Rects.Dock.Min.X+20.f,Rects.Dock.Min.Y+150.f,Rects.Dock.Max.X-20.f,Rects.Dock.Max.Y-12.f));
    }
    Place(FilterRow,WorkshopBox(Rects.Library.Min.X+24.f,Rects.Library.Min.Y+62.f,Rects.Library.Max.X-20.f,Rects.Library.Min.Y+118.f));
    Place(PendingTitle,WorkshopHasArea(Rects.Pending)?WorkshopBox(Rects.Pending.Min.X+24.f,Rects.Pending.Min.Y+14.f,Rects.Pending.Max.X-20.f,Rects.Pending.Min.Y+40.f):FBox2D(ForceInit));
    if(DockBody){DockBody->SetWrapTextAt(Mode==EMode::Full?float(Rects.Dock.GetSize().X)-520.f:WorkshopFieldDockWidth-40.f);}
    const float DockIcon=Mode==EMode::Full?112.f:80.f;
    if(DockIconBox&&!FMath::IsNearlyEqual(DockIconBox->GetWidthOverride(),DockIcon)){DockIconBox->SetWidthOverride(DockIcon);DockIconBox->SetHeightOverride(DockIcon);}
}

ULDComponentStorage::FWorkshopLayout ULDWorkshop::StorageLayout() const
{
    ULDComponentStorage::FWorkshopLayout Out;
    if(!IsOpen()||!Canvas){return Out;}
    const FGeometry& Geometry=GetCachedGeometry();
    if(Geometry.GetLocalSize().X<=0.f){return Out;}
    const auto Absolute=[&Geometry](const FBox2D& Box)
    {
        if(!WorkshopHasArea(Box)){return FSlateRect();}
        const FVector2D A=Geometry.LocalToAbsolute(Box.Min),B=Geometry.LocalToAbsolute(Box.Max);
        return FSlateRect(A.X,A.Y,B.X,B.Y);
    };
    Out.bActive=true;
    for(int32 Index=0;Index<LDHUD::StorageColumnCount;++Index){Out.Columns[Index]=Absolute(Rects.Column[Index]);}
    const float Scale=Geometry.Scale>0.f?Geometry.Scale:1.f;
    Out.HeadHeight=Rects.HeadHeight*Scale;
    Out.Library=Absolute(Rects.LibraryBody);
    Out.TileScale=Mode==EMode::Full?1.f:.78f;
    Out.bNames=true;
    // Icons stay at least WorkshopMinIconPixels on screen; the board then drops the names to keep the room.
    const float MinUnits=WorkshopMinIconPixels/Scale;
    if(46.f*Out.TileScale<MinUnits){Out.TileScale=MinUnits/46.f;Out.bNames=false;}
    Out.LibraryIcon=FMath::Max(LibraryIconFor(Rects.LibraryBody),MinUnits);
    Out.TagFilter=TagFilter;
    return Out;
}

bool ULDWorkshop::IsHit(FVector2D Absolute) const
{
    if(!IsOpen()){return false;}
    if(Mode==EMode::Full){return true;}
    const FVector2D Local=GetCachedGeometry().AbsoluteToLocal(Absolute);
    return WorkshopHasArea(Rects.Region)&&Rects.Region.IsInside(Local);
}

void ULDWorkshop::Present(const FInput& Input)
{
    const EMode Previous=Mode;
    Latest=Input;Mode=Input.Mode;
    View=Input.Progression?*Input.Progression:FLDProgressionView();
    if(Mode==EMode::Closed)
    {
        WorkshopShown(this,false);OpenedAt=-1.;
        return;
    }
    if(Previous==EMode::Closed||OpenedAt<0.){OpenedAt=UiNow();DockKey.Reset();}
    WorkshopShown(this,true);
    FVector2D Size=GetCachedGeometry().GetLocalSize();
    if(Size.X<=1.f||Size.Y<=1.f){Size=FVector2D(1920.f,1080.f);}
    LastSize=Size;
    Layout(Size);
    // The darkening fades in with the opening; at low quality it only darkens.
    const float Open=LDTween::Progress(UiNow()-OpenedAt,LDHUDStyle::MotionWorkshopOpen,LDTween::ECurve::EaseOut);
    if(Blur)
    {
        const float Strength=Mode==EMode::Full&&!Input.bLowQuality?8.f*Open:0.f;
        if(!FMath::IsNearlyEqual(Blur->GetBlurStrength(),Strength,.01f)){Blur->SetBlurStrength(Strength);}
    }
    if(Shade){Shade->SetBrushColor(LDHUDStyle::UIBase.CopyWithNewOpacity((Mode==EMode::Full?.62f:.88f)*Open));}
    RefreshPhase();
    RefreshCharacter();
    RefreshHeads();
    const LDHUD::FStorageView* Model=Storage.IsValid()?&Storage->Model():nullptr;
    WorkshopText(LibraryCount,Model?FString::FromInt(Model->Stored.Num()):FString());
    if(Model)
    {
        WorkshopText(CapacityCount,FString::Printf(TEXT("%d / %d"),Model->Installed,Model->Capacity));
        CapacityCount->SetColorAndOpacity(Model->Capacity>0&&Model->Installed>=Model->Capacity?LDHUDStyle::UIAction:LDHUDStyle::UIText);
        const LDWorkshop::FCapacityView Capacity=LDWorkshop::Capacity(View,*Model,Input.Content);
        WorkshopText(CapacityUnlock,Capacity.UnlockLevel>0?LDTerms::WorkshopUnlockAt(Capacity.UnlockLevel):FString());
    }
    const bool bLocked=Storage.IsValid()&&!Storage->BuildGate().bCanInstall;
    WorkshopShown(LockBadge,bLocked,ESlateVisibility::HitTestInvisible);
    if(bLocked){WorkshopText(LockBadge,Storage->BuildGate().InstallBlocked.IsEmpty()?FString(LDTerms::WorkshopLocked):Storage->BuildGate().InstallBlocked);}
    WorkshopText(PendingTitle,FString::Printf(TEXT("%s  %d"),LDTerms::WorkshopPending,Input.Pending));
}

void ULDWorkshop::RefreshPhase()
{
    if(Mode!=EMode::Full){return;}
    if(Latest.bDead)
    {
        WorkshopText(PhaseRest,FString());
        WorkshopText(PhaseStep,Latest.DeathTitle);PhaseStep->SetColorAndOpacity(LDHUDStyle::UIReject);
        WorkshopText(PhaseClock,Latest.DeathCountdown);
        return;
    }
    PhaseStep->SetColorAndOpacity(LDHUDStyle::UIText);
    WorkshopText(PhaseRest,Latest.bRest?FString::Printf(TEXT("休息 R%u"),uint32(Latest.RestIndex)):FString());
    WorkshopText(PhaseStep,Latest.bRest?LDHUD::RestStepText(Latest.Step):FString());
    const int64 Remaining=Latest.bClock&&Latest.StepEndsAtMs>0?FMath::Max<int64>(0,Latest.StepEndsAtMs-Latest.NowMs):-1;
    WorkshopText(PhaseClock,Remaining>=0?LDHUD::Clock(Remaining):FString());
}

void ULDWorkshop::RefreshCharacter()
{
    if(Mode!=EMode::Full){return;}
    const FLDDemoContentDefinitions* Content=Latest.Content;
    WorkshopText(PortraitGlyph,Latest.HolderName.IsEmpty()?FString(TEXT("?")):Latest.HolderName.Left(1));
    if(Portrait&&Portrait->GetBrushColor()!=Latest.HolderColor){Portrait->SetBrushColor(Latest.HolderColor);}
    WorkshopText(HolderName,Latest.HolderName);
    WorkshopText(LevelBadge,View.bValid?FString::FromInt(View.Level):FString(TEXT("—")));
    WorkshopText(LevelLine,Latest.Level.Experience);
    const int64 Unspent=View.bValid?FMath::Max<int64>(0,View.UnspentPoints):0;
    WorkshopShown(PointsBox,Unspent>0);
    WorkshopText(PointsText,LDTerms::WorkshopPointsToSpend(Unspent));
    // The stat rows follow the catalog's design order; each "+" spends one point through the band's stat bar.
    if(Content&&StatRows.IsEmpty())
    {
        int32 Index=0;
        for(const FLDDemoStatDefinition* Stat:Content->StatsInDesignOrder())
        {
            FStatRow& Row=StatRows.AddDefaulted_GetRef();Row.StatId=FName(*Stat->Id);
            auto* Line=WidgetTree->ConstructWidget<UHorizontalBox>();
            auto* IconStack=WidgetTree->ConstructWidget<UOverlay>();
            Row.Picture=WidgetTree->ConstructWidget<UImage>();Row.Picture->SetVisibility(ESlateVisibility::Collapsed);
            Row.Glyph=LDWidgets::Text(WidgetTree,TEXT(""),LDHUDStyle::TypeLabel,NAME_None,LDUIFonts::ERole::Strong);Row.Glyph->SetJustification(ETextJustify::Center);
            auto* PictureSlot=IconStack->AddChildToOverlay(Row.Picture);PictureSlot->SetHorizontalAlignment(HAlign_Fill);PictureSlot->SetVerticalAlignment(VAlign_Fill);
            auto* GlyphSlot=IconStack->AddChildToOverlay(Row.Glyph);GlyphSlot->SetHorizontalAlignment(HAlign_Center);GlyphSlot->SetVerticalAlignment(VAlign_Center);
            auto* IconBox=WidgetTree->ConstructWidget<USizeBox>();IconBox->SetWidthOverride(42.f);IconBox->SetHeightOverride(42.f);IconBox->SetContent(IconStack);
            Line->AddChildToHorizontalBox(IconBox)->SetVerticalAlignment(VAlign_Center);
            Row.Name=LDWidgets::Text(WidgetTree,TEXT(""),LDHUDStyle::TypeLabel,NAME_None,LDUIFonts::ERole::Strong);Row.Name->SetAutoWrapText(false);
            Row.Name->SetColorAndOpacity(LDHUDStyle::UIText);
            auto* NameSlot=Line->AddChildToHorizontalBox(Row.Name);NameSlot->SetVerticalAlignment(VAlign_Center);NameSlot->SetPadding(FMargin(12,0,0,0));
            auto* Meter=WidgetTree->ConstructWidget<USizeBox>();Meter->SetWidthOverride(92.f);Meter->SetHeightOverride(10.f);Row.Meter=Meter;
            auto* MeterSlot=Line->AddChildToHorizontalBox(Meter);MeterSlot->SetVerticalAlignment(VAlign_Center);MeterSlot->SetPadding(FMargin(14,0,0,0));
            Line->AddChildToHorizontalBox(WidgetTree->ConstructWidget<USpacer>())->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            Row.Points=LDWidgets::Text(WidgetTree,TEXT(""),LDHUDStyle::TypeHeading,NAME_None,LDUIFonts::ERole::Number);Row.Points->SetAutoWrapText(false);
            Row.Points->SetColorAndOpacity(LDHUDStyle::UIText);
            Line->AddChildToHorizontalBox(Row.Points)->SetVerticalAlignment(VAlign_Center);
            const int32 StatIndex=Index;
            Row.Plus=LDWidgets::Button(WidgetTree,TEXT("+"),[this,StatIndex](){if(StatBar.IsValid()){StatBar->Allocate(StatIndex);}},true,
                FName(*FString::Printf(TEXT("WorkshopStatPlus%d"),StatIndex)));
            WorkshopTint(Row.Plus,true);
            auto* PlusBox=WidgetTree->ConstructWidget<USizeBox>();PlusBox->SetWidthOverride(36.f);PlusBox->SetHeightOverride(36.f);PlusBox->SetContent(Row.Plus);
            Line->AddChildToHorizontalBox(PlusBox)->SetPadding(FMargin(12,0,0,0));
            auto* Frame=WidgetTree->ConstructWidget<UBorder>();Frame->SetBrushColor(FLinearColor::Transparent);Frame->SetPadding(FMargin(0,5));Frame->SetContent(Line);
            Frame->SetVisibility(ESlateVisibility::Visible);Row.Row=Frame;
            StatList->AddChildToVerticalBox(Frame);
            for(UWidget* Each:{static_cast<UWidget*>(Line),static_cast<UWidget*>(IconStack),static_cast<UWidget*>(IconBox),static_cast<UWidget*>(Meter),static_cast<UWidget*>(PlusBox)}){Built.Add(Each);}
            ++Index;
        }
    }
    const bool bCanAllocate=Storage.IsValid()&&Storage->BuildGate().bCanAllocate;
    if(Content)
    {
        const TArray<const FLDDemoStatDefinition*> Stats=Content->StatsInDesignOrder();
        for(int32 Index=0;Index<StatRows.Num()&&Stats.IsValidIndex(Index);++Index)
        {
            FStatRow& Row=StatRows[Index];
            const LDHUD::FStatRowView Shown=LDHUD::StatRow(*Stats[Index],LDHUD::StatPointsOf(View,Row.StatId),Content);
            WorkshopText(Row.Name,Shown.Name);WorkshopText(Row.Points,FString::FromInt(Shown.Points));WorkshopText(Row.Glyph,Shown.Glyph);
            Row.Value=Shown.Points;
            if(Row.AppliedIcon!=Shown.IconId){Row.AppliedIcon=Shown.IconId;WorkshopShown(Row.Glyph,!LDUIIcons::Apply(Row.Picture,Shown.IconId,8.f),ESlateVisibility::HitTestInvisible);}
            const bool bPlus=Unspent>0&&bCanAllocate;
            if(Row.Plus->GetIsEnabled()!=bPlus){Row.Plus->SetIsEnabled(bPlus);}
        }
    }
    // The derived attributes, two a line, in the attribute view's order.
    TArray<LDHUD::FAttributeRowView> Shown;
    for(const uint8 Attribute:LDHUD::AttributeViewOrder())
    {
        const FLDCombatAttributeRowView* Row=Latest.Attributes.FindByPredicate([Attribute](const FLDCombatAttributeRowView& R){return R.Attribute==Attribute;});
        if(Row){Shown.Add(LDHUD::AttributeRow(*Row));}
    }
    while(AttributeNames.Num()<Shown.Num())
    {
        const int32 Index=AttributeNames.Num();
        if(Index%2==0)
        {
            auto* Line=WidgetTree->ConstructWidget<UHorizontalBox>();Built.Add(Line);
            AttributeList->AddChildToVerticalBox(Line)->SetPadding(FMargin(0,Index?6.f:0.f,0,0));
        }
        auto* Line=Cast<UHorizontalBox>(AttributeList->GetChildAt(AttributeList->GetChildrenCount()-1));
        UTextBlock* Name=LDWidgets::Text(WidgetTree,TEXT(""),LDHUDStyle::TypeSmall);Name->SetAutoWrapText(false);Name->SetColorAndOpacity(LDHUDStyle::TextSecondary);
        UTextBlock* Value=LDWidgets::Text(WidgetTree,TEXT(""),LDHUDStyle::TypeSmall,NAME_None,LDUIFonts::ERole::Number);Value->SetAutoWrapText(false);
        Value->SetColorAndOpacity(LDHUDStyle::UIText);Value->SetJustification(ETextJustify::Right);
        auto* Half=WidgetTree->ConstructWidget<UHorizontalBox>();Built.Add(Half);
        auto* NameSlot=Half->AddChildToHorizontalBox(Name);NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        Half->AddChildToHorizontalBox(Value)->SetPadding(FMargin(6,0,0,0));
        auto* HalfSlot=Line->AddChildToHorizontalBox(Half);HalfSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));HalfSlot->SetPadding(FMargin(Index%2?16.f:0.f,0,0,0));
        AttributeNames.Add(Name);AttributeValues.Add(Value);
    }
    for(int32 Index=0;Index<AttributeNames.Num();++Index)
    {
        const bool bHas=Shown.IsValidIndex(Index);
        WorkshopText(AttributeNames[Index],bHas?Shown[Index].Name:FString());
        WorkshopText(AttributeValues[Index],bHas?Shown[Index].Value:FString());
    }
    // The element reactions: the one the current build can set off lights up.
    const TArray<LDWorkshop::FReactionRow> Rows=LDWorkshop::Reactions(LDWorkshop::BuildElements(View,Content));
    for(int32 Index=0;Index<Reactions.Num()&&Rows.IsValidIndex(Index);++Index)
    {
        FReaction& Reaction=Reactions[Index];
        Reaction.First=Rows[Index].First;Reaction.Second=Rows[Index].Second;
        WorkshopText(Reaction.Name,Rows[Index].Name);
        WorkshopText(Reaction.Pair,LDHUD::ElementName(Rows[Index].First)+TEXT(" + ")+LDHUD::ElementName(Rows[Index].Second));
        if(Reaction.bLit!=Rows[Index].bLit||Reaction.Row->GetBrushColor().A<.01f)
        {
            Reaction.bLit=Rows[Index].bLit;
            Reaction.Row->SetBrush(WorkshopRound(LDHUDStyle::RadiusButton,Reaction.bLit?LDHUDStyle::UIReplace:LDHUDStyle::PanelRim.CopyWithNewOpacity(.4f),Reaction.bLit?1.5f:1.f));
            Reaction.Row->SetBrushColor(Reaction.bLit?LDHUDStyle::UIReplace.CopyWithNewOpacity(.16f):LDHUDStyle::UIRaised.CopyWithNewOpacity(.6f));
            Reaction.Name->SetColorAndOpacity(Reaction.bLit?LDHUDStyle::UIText:LDHUDStyle::TextSecondary);
        }
    }
}

void ULDWorkshop::RefreshHeads()
{
    const LDHUD::FStorageView* Model=Storage.IsValid()?&Storage->Model():nullptr;
    for(int32 Index=0;Index<Heads.Num();++Index)
    {
        FHead& Head=Heads[Index];
        const LDHUD::EStorageColumn Column=static_cast<LDHUD::EStorageColumn>(Index);
        const FName Active=Model&&Model->Columns.IsValidIndex(Index)?Model->Columns[Index].Active:NAME_None;
        const LDWorkshop::FColumnHead Shown=LDWorkshop::ColumnHead(Column,Active,Latest.HolderName,Latest.Content);
        Head.Active=Active;Head.bEmpty=Shown.bEmpty;Head.Elements=Shown.Elements;
        FString Key;
        if(Column==LDHUD::EStorageColumn::Body){Key=LDTerms::ColumnBody;}
        else if(Column==LDHUD::EStorageColumn::Dash){Key=Latest.DashKey.IsEmpty()?FString(TEXT("D")):Latest.DashKey;}
        else{Key=Latest.Keys.Of(uint8(Index-int32(LDHUD::EStorageColumn::SlotQ)));}
        WorkshopText(Head.Key,Key);
        WorkshopText(Head.Name,Shown.Name);
        Head.Name->SetColorAndOpacity(Shown.bEmpty?LDHUDStyle::TextSecondary:LDHUDStyle::UIText);
        WorkshopText(Head.Glyph,Shown.bEmpty?FString(TEXT("+")):Shown.Glyph);
        if(Head.AppliedIcon!=Shown.IconId||Shown.IconId.IsEmpty())
        {
            Head.AppliedIcon=Shown.IconId;
            const bool bPicture=!Shown.IconId.IsEmpty()&&LDUIIcons::Apply(Head.Picture,Shown.IconId,10.f);
            WorkshopShown(Head.Glyph,!bPicture,ESlateVisibility::HitTestInvisible);
            if(!bPicture){WorkshopShown(Head.Picture,false);}
        }
        // The body column shows the holder's identity colour; an empty slot is only its dashed outline (painted).
        const FLinearColor Fill=Column==LDHUD::EStorageColumn::Body?Latest.HolderColor:Shown.bEmpty?FLinearColor::Transparent:LDHUDStyle::IconFill;
        if(Head.IconFace->GetBrushColor()!=Fill){Head.IconFace->SetBrushColor(Fill);}
        WorkshopText(Head.Mana,Shown.Mana.IsEmpty()?FString():TEXT("● ")+Shown.Mana);
        Head.Mana->SetColorAndOpacity(FLinearColor(FColor(0x6F,0x9B,0xFF)));
        WorkshopText(Head.Cooldown,Shown.Cooldown.IsEmpty()?FString():TEXT("◆ ")+Shown.Cooldown);
        if(UBorder* Chip=Cast<UBorder>(Head.Element->GetParent()))
        {
            WorkshopShown(Chip,!Shown.Elements.IsEmpty(),ESlateVisibility::HitTestInvisible);
            if(!Shown.Elements.IsEmpty())
            {
                WorkshopText(Head.Element,LDHUD::ElementName(Shown.Elements[0]));
                Chip->SetBrushColor(WorkshopElementColor(Shown.Elements[0]));
            }
        }
    }
}

void ULDWorkshop::ShowTextDock(const FString& Key,const FString& Name,const FString& Summary,const FString& Body,const FString& IconId,const FString& Glyph)
{
    if(DockKey==Key){return;}
    DockKey=Key;DockKindValue=0;
    WorkshopText(DockName,Name);
    WorkshopText(DockSummary,Summary);WorkshopShown(DockSummary,!Summary.IsEmpty(),ESlateVisibility::HitTestInvisible);
    if(DockBody){DockBody->SetText(FText::FromString(Body));}
    WorkshopText(DockChips,FString());
    WorkshopText(DockKind,FString());WorkshopText(DockTags,FString());
    WorkshopText(DockGlyph,Glyph);
    const bool bPicture=!IconId.IsEmpty()&&LDUIIcons::Apply(DockPicture,IconId,12.f);
    WorkshopShown(DockGlyph,!bPicture,ESlateVisibility::HitTestInvisible);
    DockIconFace->SetBrush(WorkshopRound(12.f,FLinearColor::Transparent,0.f));DockIconFace->SetBrushColor(LDHUDStyle::IconFill);
    for(UTextBlock* Hidden:{DockWhereTitle.Get(),DockWhere.Get(),DockLinksTitle.Get(),DockLinks.Get(),DockNotesTitle.Get(),DockNotes.Get()}){WorkshopShown(Hidden,false);}
}

void ULDWorkshop::ShowComponentDock(FName Component,const ULDComponentStorage::FFocus* Focus)
{
    const FLDDemoContentDefinitions* Content=Latest.Content;
    const FString DropText=Focus&&Focus->bOverDrop?LDHUD::DropPreviewText(Component,Focus->Drop,View,Content):FString();
    const FString Key=FString::Printf(TEXT("C|%s|%llu|%s"),*Component.ToString(),View.BuildRevision,*DropText);
    if(DockKey==Key){return;}
    DockKey=Key;
    const LDHUD::FComponentCardView Card=LDHUD::ComponentCard(Component,Content,Latest.Stats);
    const LDWorkshop::FComponentFacts Facts=LDWorkshop::ComponentFacts(Component,Content);
    DockKindValue=uint8(Facts.Kind);
    WorkshopText(DockName,Card.Name);
    WorkshopText(DockSummary,FString());WorkshopShown(DockSummary,false);
    const FString Body=Card.Keywords.IsEmpty()?Card.Sentence:Card.Sentence+TEXT("\n")+FString::Join(Card.Keywords,TEXT("\n"));
    if(DockBody){DockBody->SetText(FText::FromString(Body));}
    WorkshopText(DockKind,LDTerms::ComponentKindName(uint8(Facts.Kind)));
    DockKind->SetColorAndOpacity(LDUIShapes::KindColor(uint8(Facts.Kind)));
    TArray<FString> Tags;for(const ELDDemoComponentTag Tag:Facts.Tags){Tags.Add(LDTerms::ComponentTagName(uint8(Tag)));}
    WorkshopText(DockTags,FString::Join(Tags,TEXT(" · ")));
    const TArray<FString> Triggers=LDWorkshop::TriggerChips(Component,Content);
    WorkshopText(DockChips,FString::Join(Triggers,TEXT("  ·  ")));
    WorkshopText(DockGlyph,Card.Glyph);
    const float Radius=Facts.Kind==ELDDemoComponentKind::Connector?56.f:Facts.Kind==ELDDemoComponentKind::Mod?22.f:12.f;
    const bool bPicture=LDUIIcons::Apply(DockPicture,Card.IconId,Radius);
    WorkshopShown(DockGlyph,!bPicture,ESlateVisibility::HitTestInvisible);
    DockIconFace->SetBrush(WorkshopRound(Radius,FLinearColor::Transparent,0.f));DockIconFace->SetBrushColor(LDHUDStyle::IconFill);
    // Right: where it sits (or where letting go puts it), what it works with, and what to watch.
    const FLDComponentInstanceView* Owned=View.Components.FindByPredicate([Component](const FLDComponentInstanceView& C){return C.DefinitionId==Component;});
    const bool bInstalled=Owned&&Owned->bInstalled;
    WorkshopText(DockWhereTitle,bInstalled&&DropText.IsEmpty()?LDTerms::WorkshopInstalledOn:LDTerms::WorkshopGoesOn);
    FString Where;
    if(!DropText.IsEmpty()){Where=DropText;}
    else if(bInstalled)
    {
        const FString Target=WorkshopStripParens(LDHUD::InstalledTargetText(*Owned,Content));
        Where=Target.IsEmpty()?FString(LDTerms::ColumnBody):Target;
    }
    else
    {
        Where=LDHUD::StoredPlaceText(Component,View,Content);
        const FString Missing=LDWorkshop::MissingPlace(Component,View,Content);
        if(!Missing.IsEmpty()){Where+=TEXT("\n")+Missing;}
    }
    WorkshopText(DockWhere,Where);
    TArray<FString> Links,Notes;
    TArray<FString> Lines;LDHUD::ConnectionsText(Component,View,Content).ParseIntoArrayLines(Lines);
    for(FString& Line:Lines)
    {
        if(Line.StartsWith(TEXT("同形态"))){Notes.Add(Line);}
        else{Line.RemoveFromStart(TEXT("联动："));Links.Add(Line);}
    }
    if(Focus&&Focus->bOverDrop&&!Focus->Drop.Lost.IsEmpty())
    {
        TArray<FString> Names;for(const FName& Id:Focus->Drop.Lost){Names.Add(LDHUD::ContentName(Content,Id.ToString()));}
        Notes.Add(LDTerms::MovingDetaches(FString::Join(Names,TEXT("、"))));
    }
    WorkshopText(DockLinks,FString::Join(Links,TEXT("\n")));
    WorkshopText(DockNotes,FString::Join(Notes,TEXT("\n")));
    WorkshopShown(DockWhereTitle,true,ESlateVisibility::HitTestInvisible);WorkshopShown(DockWhere,true,ESlateVisibility::HitTestInvisible);
    WorkshopShown(DockLinksTitle,!Links.IsEmpty(),ESlateVisibility::HitTestInvisible);WorkshopShown(DockLinks,!Links.IsEmpty(),ESlateVisibility::HitTestInvisible);
    WorkshopShown(DockNotesTitle,!Notes.IsEmpty(),ESlateVisibility::HitTestInvisible);WorkshopShown(DockNotes,!Notes.IsEmpty(),ESlateVisibility::HitTestInvisible);
}

void ULDWorkshop::RefreshDock(FVector2D Pointer)
{
    // The dock follows the held or hovered component first, then a hovered card of the tray, a stat row or a column head;
    // with nothing under the pointer it keeps what it showed last.
    if(Storage.IsValid()&&!Storage->Focus().Component.IsNone())
    {
        const ULDComponentStorage::FFocus& Focus=Storage->Focus();
        ShowComponentDock(Focus.Component,&Focus);
        return;
    }
    if(Choices.IsValid())
    {
        LDHUD::FActionDetailView Detail;FName Component;FString IconId;
        if(Choices->Hovered(Pointer,Detail,&Component,&IconId))
        {
            if(!Component.IsNone()){ShowComponentDock(Component,nullptr);}
            else{ShowTextDock(TEXT("T|")+Detail.Name+Detail.Summary+Detail.Body,Detail.Name,Detail.Summary,Detail.Body,IconId,LDHUD::IconGlyph(Detail.Name));}
            return;
        }
    }
    const FLDDemoContentDefinitions* Content=Latest.Content;
    if(Content&&Mode==EMode::Full)
    {
        const TArray<const FLDDemoStatDefinition*> Stats=Content->StatsInDesignOrder();
        for(int32 Index=0;Index<StatRows.Num()&&Stats.IsValidIndex(Index);++Index)
        {
            if(!StatRows[Index].Row||!StatRows[Index].Row->IsHovered()){continue;}
            const LDHUD::FStatRowView Row=LDHUD::StatRow(*Stats[Index],StatRows[Index].Value,Content);
            TArray<FString> Lines;
            for(const LDHUD::FStatEffectView& Effect:LDHUD::StatEffects(*Stats[Index],Latest.Attributes))
            {Lines.Add(FString::Printf(TEXT("%s  %s  <b>%s</>"),*Effect.Attribute,*Effect.Current,*Effect.PerPoint));}
            ShowTextDock(TEXT("S|")+Row.Name+FString::FromInt(Row.Points)+FString::Join(Lines,TEXT("|")),Row.Name,Row.PerPoint,FString::Join(Lines,TEXT("\n")),Row.IconId,Row.Glyph);
            return;
        }
    }
    for(const FHead& Head:Heads)
    {
        if(!Head.Box||!Head.Box->IsHovered()||Head.Active.IsNone()){continue;}
        const LDHUD::FActiveCardView Card=LDHUD::ActiveCard(Head.Active,Content,Latest.Stats);
        const LDHUD::FActionDetailView Detail=LDHUD::ActiveChoiceDetail(Card,false,FString());
        ShowTextDock(TEXT("A|")+Head.Active.ToString()+Detail.Body,Detail.Name,Detail.Summary,Detail.Body,Card.IconId,Card.Glyph);
        return;
    }
}

void ULDWorkshop::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);
    if(!IsOpen()||!FSlateApplication::IsInitialized()){return;}
    RefreshDock(FSlateApplication::Get().GetCursorPos());
}

int32 ULDWorkshop::NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Culling,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool bEnabled) const
{
    const int32 Base=Super::NativePaint(Args,Geometry,Culling,Elements,Layer,Style,bEnabled);
    if(!IsOpen()){return Base;}
    const int32 Top=Base+1;
    const double Now=UiNow();
    // Every surface carries the same yellow corner fold on its top-left and a one-pixel highlight on its top edge.
    const FBox2D Surfaces[]={Rects.Phase,Rects.Character,Rects.Board,Rects.Dock,Rects.Library,Rects.Pending};
    for(int32 Index=0;Index<UE_ARRAY_COUNT(Surfaces);++Index)
    {
        const FBox2D& Box=Surfaces[Index];
        if(!WorkshopHasArea(Box)||(Mode!=EMode::Full&&Index<2)){continue;}
        LDUIShapes::Stroke(Elements,Top,Geometry,{FVector2D(Box.Min.X,Box.Min.Y+22.f),Box.Min,FVector2D(Box.Min.X+30.f,Box.Min.Y)},LDHUDStyle::UIAction,3.f);
        LDUIShapes::Stroke(Elements,Top,Geometry,{FVector2D(Box.Min.X+34.f,Box.Min.Y+1.f),FVector2D(Box.Max.X-8.f,Box.Min.Y+1.f)},LDHUDStyle::UIText.CopyWithNewOpacity(.07f),1.f);
    }
    // The phase ring: the step's remaining share; the six step dots (done, current, ahead).
    if(Mode==EMode::Full&&PhaseClock&&WorkshopHasArea(Rects.Phase))
    {
        const FBox2D Clock=LDUIShapes::RectIn(Geometry,PhaseClock->GetParent()?PhaseClock->GetParent()->GetCachedGeometry():PhaseClock->GetCachedGeometry());
        const FVector2D Center=Clock.GetCenter();
        float Fraction=0.f;
        if(Latest.bRest&&Latest.bClock&&Latest.StepEndsAtMs>Latest.StepStartedAtMs)
        {Fraction=FMath::Clamp(float(double(Latest.StepEndsAtMs-Latest.NowMs)/double(Latest.StepEndsAtMs-Latest.StepStartedAtMs)),0.f,1.f);}
        LDUIShapes::Arc(Elements,Top,Geometry,Center,22.f,1.f,LDHUDStyle::UIRaised,3.f);
        if(Fraction>0.f){LDUIShapes::Arc(Elements,Top+1,Geometry,Center,22.f,Fraction,Latest.bDead?LDHUDStyle::UIReject:LDHUDStyle::UIAction,3.f);}
        if(Latest.bRest&&!Latest.bDead)
        {
            const int32 Current=LDWorkshop::RestStepDot(Latest.Step);
            const float X0=float(Rects.Phase.Max.X)-LDWorkshop::RestStepDots*38.f-10.f,Y=float(Rects.Phase.GetCenter().Y);
            for(int32 Dot=0;Dot<LDWorkshop::RestStepDots;++Dot)
            {
                const FLinearColor Color=Dot==Current?LDHUDStyle::UIAction:Dot<Current?LDHUDStyle::UIText.CopyWithNewOpacity(.55f):LDHUDStyle::UIRaised;
                const float X=X0+Dot*38.f;
                LDUIShapes::Stroke(Elements,Top,Geometry,{FVector2D(X+4.f,Y),FVector2D(X+32.f,Y)},Color,Dot==Current?5.f:4.f);
            }
        }
    }
    // Stat meters: sixteen cells, the allocated ones lit.
    for(const FStatRow& Row:StatRows)
    {
        if(Mode!=EMode::Full||!Row.Meter||!Row.Meter->IsVisible()){continue;}
        const FBox2D Box=LDUIShapes::RectIn(Geometry,Row.Meter->GetCachedGeometry());
        const float Cell=float(Box.GetSize().X)/WorkshopMeterCells;
        for(int32 Index=0;Index<WorkshopMeterCells;++Index)
        {
            const FBox2D Piece(FVector2D(Box.Min.X+Index*Cell,Box.Min.Y),FVector2D(Box.Min.X+(Index+1)*Cell-1.5f,Box.Max.Y));
            LDUIShapes::Fill(Elements,Top,Geometry,Piece,Index<Row.Value?LDHUDStyle::UIAction:LDHUDStyle::UIRaised,1.f);
        }
    }
    // The capacity bar: one cell per workshop slot in the kind colour of what fills it, empty cells dark, the next level's
    // cell dashed. Hovering or holding a stored component previews where it lands: the next free cell pulses green, cells
    // a replacement frees turn orange.
    if(Storage.IsValid()&&WorkshopHasArea(Rects.Capacity))
    {
        const LDWorkshop::FCapacityView Capacity=LDWorkshop::Capacity(View,Storage->Model(),Latest.Content);
        const ULDComponentStorage::FFocus& Focus=Storage->Focus();
        const float Left=float(Rects.Capacity.Min.X)+100.f,Right=float(Rects.Capacity.Max.X)-270.f;
        const int32 Cells=FMath::Max(1,Capacity.Capacity+(Capacity.UnlockLevel>0?1:0));
        const float Step=FMath::Min(52.f,(Right-Left)/float(Cells));
        const float Y0=float(Rects.Capacity.Min.Y)+12.f,Y1=Y0+14.f;
        TArray<FName> Freed;
        if(Focus.bOverDrop&&!Focus.Drop.Replacing.IsNone()){Freed.Add(Focus.Drop.Replacing);Freed.Append(Focus.Drop.Lost);}
        const bool bAdds=!Focus.Component.IsNone()&&!Focus.bInstalled&&Focus.Drops.ContainsByPredicate([](const LDHUD::FComponentDrop& Drop){return Drop.Replacing.IsNone();});
        const float Pulse=.45f+.55f*LDTween::PingPong(Now,.8);
        for(int32 Index=0;Index<Cells;++Index)
        {
            const FBox2D Cell(FVector2D(Left+Index*Step,Y0),FVector2D(Left+(Index+1)*Step-6.f,Y1));
            if(Index>=Capacity.Capacity){LDUIShapes::Dashed(Elements,Top,Geometry,Cell,LDHUDStyle::TextSecondary.CopyWithNewOpacity(.5f),4.f,3.f,1.f);continue;}
            FLinearColor Color=LDHUDStyle::UIRaised;
            if(Capacity.Used.IsValidIndex(Index))
            {
                Color=LDUIShapes::KindColor(uint8(Capacity.Used[Index]));
                if(Freed.Contains(Capacity.Components[Index])){Color=LDHUDStyle::UIReplace;}
                else if(Focus.bHeld&&Focus.bInstalled&&Capacity.Components[Index]==Focus.Component){Color=LDHUDStyle::UIAction;}
            }
            else if(bAdds&&Index==Capacity.Used.Num()){Color=LDHUDStyle::UILegal.CopyWithNewOpacity(Pulse);}
            LDUIShapes::Fill(Elements,Top,Geometry,Cell,Color,2.f);
        }
    }
    // Column heads: a learned active wears the gold frame, an empty slot a dashed one. A column that takes the hovered or
    // held component lights green; one that takes nothing darkens.
    for(int32 Index=0;Index<Heads.Num();++Index)
    {
        const FHead& Head=Heads[Index];
        if(!Head.IconFace||!Head.Box||!Head.Box->IsVisible()){continue;}
        const FBox2D Icon=LDUIShapes::RectIn(Geometry,Head.IconFace->GetCachedGeometry());
        if(Head.bEmpty){LDUIShapes::Dashed(Elements,Top,Geometry,Icon,LDHUDStyle::TextSecondary.CopyWithNewOpacity(.6f));}
        else{LDUIShapes::Frame(Elements,Top,Geometry,LDUIShapes::EShape::Rounded,Icon,LDHUDStyle::UIAction.CopyWithNewOpacity(.85f),2.f,10.f);}
        if(!Storage.IsValid()){continue;}
        const ULDComponentStorage::FFocus& Focus=Storage->Focus();
        if(Focus.Drops.IsEmpty()){continue;}
        const LDHUD::EStorageColumn Id=static_cast<LDHUD::EStorageColumn>(Index);
        const bool bTakes=Focus.Drops.ContainsByPredicate([Id](const LDHUD::FComponentDrop& Drop){return Drop.Column==Id&&Drop.Replacing.IsNone();});
        const bool bReplaces=Focus.Drops.ContainsByPredicate([Id](const LDHUD::FComponentDrop& Drop){return Drop.Column==Id&&!Drop.Replacing.IsNone();});
        const FBox2D& Column=Rects.Column[Index];
        if(bTakes||bReplaces)
        {
            const FLinearColor Edge=bTakes?LDHUDStyle::UILegal:LDHUDStyle::UIReplace;
            LDUIShapes::Glow(Elements,Top,Geometry,LDUIShapes::EShape::Rounded,Column,Edge,8.f,8.f);
            LDUIShapes::Frame(Elements,Top,Geometry,LDUIShapes::EShape::Rounded,Column,Edge,2.f,8.f);
        }
        else{LDUIShapes::Fill(Elements,Top,Geometry,WorkshopBox(Column.Min.X,Column.Min.Y,Column.Max.X,Column.Min.Y+Rects.HeadHeight),LDHUDStyle::UIBase.CopyWithNewOpacity(.6f),8.f);}
    }
    // The dock's icon frame in its kind's shape.
    if(DockIconFace&&DockIconFace->IsVisible()&&!DockKey.IsEmpty())
    {
        const FBox2D Icon=LDUIShapes::RectIn(Geometry,DockIconFace->GetCachedGeometry());
        const bool bComponent=DockKey.StartsWith(TEXT("C|"));
        LDUIShapes::Frame(Elements,Top,Geometry,bComponent?LDUIShapes::KindShape(DockKindValue):LDUIShapes::EShape::Rounded,Icon,
            bComponent?LDUIShapes::KindColor(DockKindValue):LDHUDStyle::UIAction,2.f,12.f);
    }
    return Top+1;
}
