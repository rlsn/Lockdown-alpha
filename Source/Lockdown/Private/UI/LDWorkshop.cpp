#include "UI/LDWorkshop.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
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
constexpr float WorkshopTop=116.f;
constexpr float WorkshopGap=20.f;
constexpr float WorkshopDockGap=16.f;
constexpr float WorkshopCharacterWidth=352.f;
constexpr float WorkshopRightWidth=456.f;
constexpr float WorkshopCapacityHeight=60.f;
constexpr float WorkshopColumnGap=10.f;
// A column's head (the skill art at 28, its name and pills) above the first socket at 206, as in the design.
constexpr float WorkshopHeadTop=28.f;
constexpr float WorkshopSocketTop=206.f;
// A pill's room before its number (the dot and its gap) and after it.
constexpr float WorkshopPillLead=21.f;
constexpr float WorkshopPillTail=8.f;
constexpr float WorkshopDockHeight=260.f;
constexpr float WorkshopFieldShare=.46f;
constexpr float WorkshopFieldDockWidth=340.f;
constexpr float WorkshopArtisanBand=150.f;
constexpr float WorkshopDockRight=270.f;
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
// A dock column that scales down (never up) to fit its place instead of spilling out of the dock.
UScaleBox* WorkshopFit(UWidgetTree* Tree,UWidget* Content)
{
    UScaleBox* Fit=Tree->ConstructWidget<UScaleBox>();Fit->SetStretch(EStretch::ScaleToFit);Fit->SetStretchDirection(EStretchDirection::DownOnly);
    Fit->SetContent(Content);
    if(UScaleBoxSlot* FitSlot=Cast<UScaleBoxSlot>(Content->Slot)){FitSlot->SetHorizontalAlignment(HAlign_Left);FitSlot->SetVerticalAlignment(VAlign_Top);}
    return Fit;
}
bool WorkshopHasArea(const FBox2D& Box){return Box.bIsValid&&Box.GetSize().X>1.f&&Box.GetSize().Y>1.f;}
FBox2D WorkshopBox(float X0,float Y0,float X1,float Y1){return FBox2D(FVector2D(X0,Y0),FVector2D(X1,Y1));}
FLinearColor WorkshopElementColor(ELDDemoElement Element)
{
    switch(Element)
    {
    case ELDDemoElement::Fire: return LDUIPaint::Hex(0xFF7A3D);
    case ELDDemoElement::Water: return LDUIPaint::Hex(0x4DB5FF);
    case ELDDemoElement::Electric: return LDUIPaint::Hex(0xFFE45C);
    default: return LDHUDStyle::TextSecondary;
    }
}
FString WorkshopStripParens(const FString& Text)
{
    FString Out=Text;Out.RemoveFromStart(TEXT("（"));Out.RemoveFromEnd(TEXT("）"));Out.RemoveFromStart(TEXT("装在："));
    return Out;
}
// A filter tag: the dark chip, or the yellow one while it filters.
void WorkshopTint(ULDCommandButton* Button,bool bSelected){LDWidgets::SetLook(Button,bSelected?LDUIPaint::EButton::TagOn:LDUIPaint::EButton::Tag);}
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
        // The surfaces themselves are painted by the backdrop layer (LDUIPaint::Panel); the borders only hold the content.
        auto Surface=[this](UWidget* Child,FMargin Inset)
        {UBorder* Panel=LDWidgets::Panel(WidgetTree,Child,Inset);Panel->SetBrush(FSlateNoResource());return Panel;};
        // The world behind the full workshop darkens and blurs (only darkens at low quality); it is never paused.
        Blur=WidgetTree->ConstructWidget<UBackgroundBlur>();Blur->SetBlurStrength(0.f);Blur->SetApplyAlphaToBlur(true);
        Shade=WidgetTree->ConstructWidget<UBorder>();Shade->SetBrushColor(LDHUDStyle::UIBase.CopyWithNewOpacity(.6f));
        Blur->SetContent(Shade);Add(Blur,0,ESlateVisibility::Visible);
        Backdrop=WidgetTree->ConstructWidget<ULDPaintLayer>();
        Backdrop->Painter=[this](FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry){PaintBackdrop(Elements,Layer,Geometry);};
        Add(Backdrop,1,ESlateVisibility::HitTestInvisible);
        {
            // The phase bar: the step's remaining time inside its ring, the rest and step, and the step dots (painted).
            auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();
            PhaseClock=Label(TEXT(""),13,LDHUDStyle::UIText,LDUIFonts::ERole::Number);PhaseClock->SetJustification(ETextJustify::Center);
            auto* ClockBox=WidgetTree->ConstructWidget<USizeBox>();ClockBox->SetWidthOverride(44.f);ClockBox->SetHeightOverride(44.f);
            ClockBox->SetContent(PhaseClock);
            if(USizeBoxSlot* ClockSlot=Cast<USizeBoxSlot>(ClockBox->GetContentSlot())){ClockSlot->SetHorizontalAlignment(HAlign_Center);ClockSlot->SetVerticalAlignment(VAlign_Center);}
            Row->AddChildToHorizontalBox(ClockBox)->SetVerticalAlignment(VAlign_Center);
            auto* Names=WidgetTree->ConstructWidget<UVerticalBox>();
            PhaseRest=Label(TEXT(""),13,LDHUDStyle::TextSecondary);Names->AddChildToVerticalBox(PhaseRest);
            {FSlateFontInfo Font=PhaseRest->GetFont();Font.LetterSpacing=120;PhaseRest->SetFont(Font);}
            PhaseStep=Label(TEXT(""),22,LDHUDStyle::UIText,LDUIFonts::ERole::Title);Names->AddChildToVerticalBox(PhaseStep);
            Row->AddChildToHorizontalBox(Names)->SetPadding(FMargin(14,0,0,0));
            PhasePanel=Surface(Row,FMargin(14,8));Add(PhasePanel,2);
            CloseButton=LDWidgets::Button(WidgetTree,LDTerms::WorkshopClose,[this](){if(RequestClose){RequestClose();}},true,TEXT("WorkshopClose"));
            if(UTextBlock* Caption=Cast<UTextBlock>(CloseButton->GetContent()))
            {
                // The close button carries its key as a keycap (.btn > .key).
                Caption->SetFont(LDUIFonts::Font(LDUIFonts::ERole::Number,14));Caption->SetColorAndOpacity(LDHUDStyle::TextSecondary);
                UBorder* Key=WidgetTree->ConstructWidget<UBorder>();Key->SetBrush(FSlateRoundedBoxBrush(FLinearColor(0,0,0,.35f),4.f,FLinearColor(1,1,1,.3f),1.f));
                Key->SetPadding(FMargin(7,2));Key->SetHorizontalAlignment(HAlign_Center);Key->SetVerticalAlignment(VAlign_Center);
                CloseButton->SetContent(Key);Key->SetContent(Caption);
                auto Style=CloseButton->GetStyle();Style.NormalPadding=FMargin(16,0);Style.PressedPadding=FMargin(16,0);CloseButton->SetStyle(Style);
            }
            Add(CloseButton,3,ESlateVisibility::Visible);
        }
        {
            // The character column: the holder, the unspent points, one row per stat with its "+", the derived attributes and
            // the three element reactions.
            auto* Column=WidgetTree->ConstructWidget<UVerticalBox>();
            auto* Head=WidgetTree->ConstructWidget<UHorizontalBox>();
            auto* PortraitStack=WidgetTree->ConstructWidget<UOverlay>();
            PortraitGlyph=Label(TEXT(""),34,FLinearColor::White,LDUIFonts::ERole::Title);PortraitGlyph->SetJustification(ETextJustify::Center);
            // The face, its experience ring and the level badge are painted (PaintBackdrop); the border only centres the glyph.
            Portrait=WidgetTree->ConstructWidget<UBorder>();Portrait->SetBrush(FSlateNoResource());
            Portrait->SetHorizontalAlignment(HAlign_Center);Portrait->SetVerticalAlignment(VAlign_Center);Portrait->SetContent(PortraitGlyph);
            auto* PortraitFill=PortraitStack->AddChildToOverlay(Portrait);PortraitFill->SetHorizontalAlignment(HAlign_Fill);PortraitFill->SetVerticalAlignment(VAlign_Fill);
            LevelBadge=Label(TEXT(""),17,LDUIPaint::Hex(0xFFD98A),LDUIFonts::ERole::Number);LevelBadge->SetJustification(ETextJustify::Center);LevelBadge->SetMinDesiredWidth(22.f);
            auto* Badge=WidgetTree->ConstructWidget<UBorder>();Badge->SetBrush(FSlateRoundedBoxBrush(LDUIPaint::Hex(0x10141D),5.f,LDUIPaint::Hex(0xE0A53A),1.5f));
            Badge->SetPadding(FMargin(6,1));Badge->SetContent(LevelBadge);Badge->SetRenderTranslation(FVector2D(6,4));
            auto* BadgeSlot=PortraitStack->AddChildToOverlay(Badge);BadgeSlot->SetHorizontalAlignment(HAlign_Right);BadgeSlot->SetVerticalAlignment(VAlign_Bottom);
            auto* PortraitBox=WidgetTree->ConstructWidget<USizeBox>();PortraitBox->SetWidthOverride(86.f);PortraitBox->SetHeightOverride(86.f);PortraitBox->SetContent(PortraitStack);
            PortraitFrame=PortraitBox;
            Head->AddChildToHorizontalBox(PortraitBox);
            auto* Names=WidgetTree->ConstructWidget<UVerticalBox>();
            HolderName=Label(TEXT(""),22,LDHUDStyle::UIText,LDUIFonts::ERole::Title);HolderName->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
            Names->AddChildToVerticalBox(HolderName);
            LevelLine=Label(TEXT(""),13,LDHUDStyle::TextSecondary,LDUIFonts::ERole::Number);Names->AddChildToVerticalBox(LevelLine)->SetPadding(FMargin(0,3,0,0));
            auto* NamesSlot=Head->AddChildToHorizontalBox(Names);NamesSlot->SetVerticalAlignment(VAlign_Center);NamesSlot->SetPadding(FMargin(16,0,0,0));
            NamesSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            Column->AddChildToVerticalBox(Head);
            // .pts: the unspent count large and glowing, its label beside it; the gradient box is painted.
            PointsText=Label(TEXT(""),32,LDHUDStyle::UIAction,LDUIFonts::ERole::Number);PointsText->SetRenderTranslation(FVector2D(0,-2));
            PointsText->SetShadowColorAndOpacity(LDHUDStyle::UIAction.CopyWithNewOpacity(.45f));PointsText->SetShadowOffset(FVector2D(0,0));
            UTextBlock* PointsLabel=Label(LDTerms::WorkshopPointsLabel,15,LDUIPaint::Hex(0xF5E9B3));
            auto* PointsRow=WidgetTree->ConstructWidget<UHorizontalBox>();
            PointsRow->AddChildToHorizontalBox(PointsText)->SetVerticalAlignment(VAlign_Center);
            PointsRow->AddChildToHorizontalBox(PointsLabel)->SetVerticalAlignment(VAlign_Center);
            if(UHorizontalBoxSlot* LabelSlot=Cast<UHorizontalBoxSlot>(PointsLabel->Slot)){LabelSlot->SetPadding(FMargin(12,4,0,0));}
            PointsBox=WidgetTree->ConstructWidget<UBorder>();PointsBox->SetBrush(FSlateNoResource());PointsBox->SetPadding(FMargin(22,2,16,2));PointsBox->SetContent(PointsRow);
            auto* PointsHeight=WidgetTree->ConstructWidget<USizeBox>();PointsHeight->SetHeightOverride(54.f);PointsHeight->SetContent(PointsBox);
            PointsBox->SetVerticalAlignment(VAlign_Center);
            Column->AddChildToVerticalBox(PointsHeight)->SetPadding(FMargin(0,4,0,12));
            StatList=WidgetTree->ConstructWidget<UVerticalBox>();Column->AddChildToVerticalBox(StatList);
            AttributeList=WidgetTree->ConstructWidget<UVerticalBox>();Column->AddChildToVerticalBox(AttributeList)->SetPadding(FMargin(0,26,0,0));
            UTextBlock* ReactionTitle=Label(LDTerms::WorkshopReactions,13,LDUIPaint::Hex(0x667086));
            {FSlateFontInfo Font=ReactionTitle->GetFont();Font.LetterSpacing=200;ReactionTitle->SetFont(Font);}
            Column->AddChildToVerticalBox(ReactionTitle)->SetPadding(FMargin(0,26,0,10));
            for(int32 Index=0;Index<3;++Index)
            {
                FReaction& Reaction=Reactions.AddDefaulted_GetRef();
                auto* Line=WidgetTree->ConstructWidget<UHorizontalBox>();
                // .rx3 .r: two element discs (painted under their characters), a plus, and the reaction's name on the right.
                const auto Disc=[&](UTextBlock*& Out)
                {
                    Out=Label(TEXT(""),13,LDUIPaint::Hex(0x10141D),LDUIFonts::ERole::Title);Out->SetJustification(ETextJustify::Center);
                    auto* Box=WidgetTree->ConstructWidget<USizeBox>();Box->SetWidthOverride(24.f);Box->SetHeightOverride(24.f);Box->SetContent(Out);
                    if(USizeBoxSlot* DiscSlot=Cast<USizeBoxSlot>(Out->Slot)){DiscSlot->SetHorizontalAlignment(HAlign_Center);DiscSlot->SetVerticalAlignment(VAlign_Center);}
                    Line->AddChildToHorizontalBox(Box)->SetVerticalAlignment(VAlign_Center);Built.Add(Box);
                };
                Disc(Reaction.FirstText);
                UTextBlock* Plus=Label(TEXT("+"),15,LDUIPaint::Hex(0x667086));Reaction.Pair=Plus;
                Line->AddChildToHorizontalBox(Plus)->SetPadding(FMargin(8,0));
                if(UHorizontalBoxSlot* PlusSlot=Cast<UHorizontalBoxSlot>(Plus->Slot)){PlusSlot->SetVerticalAlignment(VAlign_Center);}
                Disc(Reaction.SecondText);
                Line->AddChildToHorizontalBox(WidgetTree->ConstructWidget<USpacer>())->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
                Reaction.Name=Label(TEXT(""),15,LDUIPaint::Hex(0x667086),LDUIFonts::ERole::Strong);
                Line->AddChildToHorizontalBox(Reaction.Name)->SetVerticalAlignment(VAlign_Center);
                Reaction.Row=WidgetTree->ConstructWidget<UBorder>();Reaction.Row->SetPadding(FMargin(12,0));Reaction.Row->SetContent(Line);
                Reaction.Row->SetBrush(FSlateNoResource());Reaction.Row->SetVerticalAlignment(VAlign_Center);
                auto* RowHeight=WidgetTree->ConstructWidget<USizeBox>();RowHeight->SetHeightOverride(40.f);RowHeight->SetContent(Reaction.Row);
                Column->AddChildToVerticalBox(RowHeight)->SetPadding(FMargin(0,Index?8.f:0.f,0,0));
                Built.Add(Plus);Built.Add(Line);Built.Add(RowHeight);
            }
            RespecSpace=WidgetTree->ConstructWidget<USizeBox>();RespecSpace->SetHeightOverride(0.f);
            Column->AddChildToVerticalBox(RespecSpace)->SetHorizontalAlignment(HAlign_Fill);
            // The column scales down (never up) when larger text would push it past the panel.
            // The column keeps the panel's inner width and scales down (never up) when larger text would push it past the
            // panel's height.
            auto* ColumnWidth=WidgetTree->ConstructWidget<USizeBox>();ColumnWidth->SetWidthOverride(WorkshopCharacterWidth-48.f);ColumnWidth->SetContent(Column);
            CharacterPanel=Surface(WorkshopFit(WidgetTree,ColumnWidth),FMargin(24,22));Add(CharacterPanel,2);
        }
        {
            // The board: its surface, the capacity row (cells painted) and one head per column; the storage lays the
            // installed components out under the heads.
            BoardPanel=Surface(nullptr,FMargin(0));Add(BoardPanel,1);
            CapacityTitle=Label(LDTerms::WorkshopCapacityTitle,15,LDHUDStyle::TextSecondary);Add(CapacityTitle,3);
            if(UTextBlock* Spaced=Cast<UTextBlock>(CapacityTitle.Get())){FSlateFontInfo Font=Spaced->GetFont();Font.LetterSpacing=140;Spaced->SetFont(Font);}
            CapacityCount=Label(TEXT(""),26,LDHUDStyle::UIText,LDUIFonts::ERole::Number);Add(CapacityCount,3);
            CapacityUnlock=Label(TEXT(""),13,LDUIPaint::Hex(0x667086));CapacityUnlock->SetJustification(ETextJustify::Right);Add(CapacityUnlock,3);
            LockBadge=Label(LDTerms::WorkshopLocked,LDHUDStyle::TypeSmall,LDHUDStyle::UIReject,LDUIFonts::ERole::Strong);LockBadge->SetJustification(ETextJustify::Right);
            LockBadge->SetVisibility(ESlateVisibility::Collapsed);Add(LockBadge,3);
            for(int32 Index=0;Index<LDHUD::StorageColumnCount;++Index)
            {
                // The column surface and its rail are painted (PaintBackdrop); the border only marks the column's area.
                UBorder* Back=WidgetTree->ConstructWidget<UBorder>();Back->SetBrush(FSlateNoResource());
                Back->SetVisibility(ESlateVisibility::HitTestInvisible);Add(Back,2);ColumnBacks.Add(Back);
                FHead& Head=Heads.AddDefaulted_GetRef();
                auto* Box=WidgetTree->ConstructWidget<UVerticalBox>();
                auto* IconStack=WidgetTree->ConstructWidget<UOverlay>();
                Head.Picture=WidgetTree->ConstructWidget<UImage>();Head.Picture->SetVisibility(ESlateVisibility::Collapsed);
                Head.Glyph=Label(TEXT(""),LDHUDStyle::TypeSection,FLinearColor::White,LDUIFonts::ERole::Title);Head.Glyph->SetJustification(ETextJustify::Center);
                auto* Inner=WidgetTree->ConstructWidget<UOverlay>();
                auto* PictureSlot=Inner->AddChildToOverlay(Head.Picture);PictureSlot->SetHorizontalAlignment(HAlign_Fill);PictureSlot->SetVerticalAlignment(VAlign_Fill);
                auto* GlyphSlot=Inner->AddChildToOverlay(Head.Glyph);GlyphSlot->SetHorizontalAlignment(HAlign_Center);GlyphSlot->SetVerticalAlignment(VAlign_Center);
                // The skill art (frame, face and icon) is painted under the key and element chips (LDUIPaint::SkillArt).
                Head.IconFace=WidgetTree->ConstructWidget<UBorder>();Head.IconFace->SetBrush(FSlateNoResource());
                Head.IconFace->SetPadding(FMargin(0));Head.IconFace->SetContent(Inner);
                Head.IconFace->SetHorizontalAlignment(HAlign_Fill);Head.IconFace->SetVerticalAlignment(VAlign_Fill);
                auto* FaceSlot=IconStack->AddChildToOverlay(Head.IconFace);FaceSlot->SetHorizontalAlignment(HAlign_Fill);FaceSlot->SetVerticalAlignment(VAlign_Fill);
                // kbd: the key on a dark chip with a yellow edge, hanging off the art's corner.
                Head.Key=Label(TEXT(""),18,LDHUDStyle::UIAction,LDUIFonts::ERole::Number);Head.Key->SetJustification(ETextJustify::Center);
                auto* KeyChip=WidgetTree->ConstructWidget<UBorder>();KeyChip->SetBrush(FSlateRoundedBoxBrush(LDUIPaint::Hex(0x10141D),5.f,LDHUDStyle::UIAction,1.5f));
                KeyChip->SetPadding(FMargin(6,1));KeyChip->SetContent(Head.Key);KeyChip->SetRenderTranslation(FVector2D(-9,-9));Head.Key->SetMinDesiredWidth(18.f);
                auto* KeySlot=IconStack->AddChildToOverlay(KeyChip);KeySlot->SetHorizontalAlignment(HAlign_Left);KeySlot->SetVerticalAlignment(VAlign_Top);
                // .el: a 28 disc in the element's colour with a dark ring.
                Head.Element=Label(TEXT(""),14,LDUIPaint::Hex(0x10141D),LDUIFonts::ERole::Title);Head.Element->SetJustification(ETextJustify::Center);
                auto* ElementChip=WidgetTree->ConstructWidget<UBorder>();ElementChip->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White,14.f,LDUIPaint::Hex(0x10141D),2.f));
                ElementChip->SetPadding(FMargin(0));ElementChip->SetContent(Head.Element);ElementChip->SetRenderTranslation(FVector2D(8,8));
                ElementChip->SetHorizontalAlignment(HAlign_Center);ElementChip->SetVerticalAlignment(VAlign_Center);Head.Element->SetMinDesiredWidth(28.f);Head.Element->SetLineHeightPercentage(.95f);
                ElementChip->SetVisibility(ESlateVisibility::Collapsed);Built.Add(ElementChip);
                auto* ElementSlot=IconStack->AddChildToOverlay(ElementChip);ElementSlot->SetHorizontalAlignment(HAlign_Right);ElementSlot->SetVerticalAlignment(VAlign_Bottom);
                Head.Final=WidgetTree->ConstructWidget<UImage>();Head.Final->SetVisibility(ESlateVisibility::Collapsed);Head.Final->SetDesiredSizeOverride(FVector2D(34,34));
                Head.Final->SetRenderTranslation(FVector2D(10,-10));
                auto* FinalSlot=IconStack->AddChildToOverlay(Head.Final);FinalSlot->SetHorizontalAlignment(HAlign_Right);FinalSlot->SetVerticalAlignment(VAlign_Top);
                Head.IconBox=WidgetTree->ConstructWidget<USizeBox>();Head.IconBox->SetWidthOverride(100.f);Head.IconBox->SetHeightOverride(100.f);Head.IconBox->SetContent(IconStack);
                Box->AddChildToVerticalBox(Head.IconBox)->SetHorizontalAlignment(HAlign_Center);
                Head.Name=Label(TEXT(""),17,LDHUDStyle::UIText,LDUIFonts::ERole::Strong);Head.Name->SetJustification(ETextJustify::Center);
                Head.Name->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
                auto* NameSlot=Box->AddChildToVerticalBox(Head.Name);NameSlot->SetHorizontalAlignment(HAlign_Fill);NameSlot->SetPadding(FMargin(0,8,0,0));
                auto* Chips=WidgetTree->ConstructWidget<UHorizontalBox>();
                // .pill: the numbers sit inside painted pills, each led by its mana dot or cooldown diamond.
                Head.Mana=Label(TEXT(""),14,LDHUDStyle::UIText,LDUIFonts::ERole::Number);
                Head.Cooldown=Label(TEXT(""),14,LDHUDStyle::UIText,LDUIFonts::ERole::Number);
                Chips->AddChildToHorizontalBox(Head.Mana)->SetPadding(FMargin(WorkshopPillLead,3,WorkshopPillTail,3));
                Chips->AddChildToHorizontalBox(Head.Cooldown)->SetPadding(FMargin(6.f+WorkshopPillLead,3,WorkshopPillTail,3));
                auto* ChipsSlot=Box->AddChildToVerticalBox(Chips);ChipsSlot->SetHorizontalAlignment(HAlign_Center);ChipsSlot->SetPadding(FMargin(0,8,0,0));
                auto* HeadFrame=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),FName(*FString::Printf(TEXT("WorkshopHead%d"),Index)));
                HeadFrame->SetBrushColor(FLinearColor::Transparent);HeadFrame->SetPadding(FMargin(8,0,8,0));HeadFrame->SetContent(Box);
                HeadFrame->SetHorizontalAlignment(HAlign_Fill);HeadFrame->SetVisibility(ESlateVisibility::Visible);
                Head.Box=HeadFrame;Add(HeadFrame,3,ESlateVisibility::Visible);
                Built.Add(Box);Built.Add(IconStack);Built.Add(Inner);Built.Add(KeyChip);Built.Add(Chips);
            }
        }
        {
            // The library surface: its title, count and use-tag filter; the storage places the components under them.
            LibraryPanel=Surface(nullptr,FMargin(0));Add(LibraryPanel,1);
            auto* Title=WidgetTree->ConstructWidget<UHorizontalBox>();
            Title->AddChildToHorizontalBox(Label(LDTerms::Storage,22,LDHUDStyle::UIText,LDUIFonts::ERole::Title));
            LibraryCount=Label(TEXT(""),20,LDHUDStyle::TextSecondary,LDUIFonts::ERole::Number);
            Title->AddChildToHorizontalBox(LibraryCount)->SetPadding(FMargin(12,4,0,0));
            LibraryTitle=Title;Add(Title,3);
            FilterRow=WidgetTree->ConstructWidget<UWrapBox>();FilterRow->SetInnerSlotPadding(FVector2D(6,6));
            for(int32 Tag=0;Tag<int32(ELDDemoComponentTag::Count);++Tag)
            {
                ULDCommandButton* Chip=LDWidgets::Button(WidgetTree,LDTerms::ComponentTagName(uint8(Tag)),[this,Tag](){TagFilter=TagFilter==Tag?INDEX_NONE:Tag;
                    for(int32 Each=0;Each<FilterButtons.Num();++Each){WorkshopTint(FilterButtons[Each],Each==TagFilter);}},true,FName(*FString::Printf(TEXT("WorkshopTag%d"),Tag)));
                {auto Style=Chip->GetStyle();Style.NormalPadding=FMargin(9,3);Style.PressedPadding=FMargin(9,3);Chip->SetStyle(Style);}
                if(UTextBlock* Caption=Cast<UTextBlock>(Chip->GetContent())){Caption->SetFont(LDUIFonts::Font(LDUIFonts::ERole::Body,13));}
                WorkshopTint(Chip,false);
                FilterRow->AddChildToWrapBox(Chip);FilterButtons.Add(Chip);
            }
            Add(FilterRow,3,ESlateVisibility::Visible);
            PendingPanel=Surface(nullptr,FMargin(0));Add(PendingPanel,1);
            PendingTitle=Label(TEXT(""),13,LDHUDStyle::TextSecondary);Add(PendingTitle,3);
            {FSlateFontInfo Font=PendingTitle->GetFont();Font.LetterSpacing=160;PendingTitle->SetFont(Font);}
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
            DockIconFace=WidgetTree->ConstructWidget<UBorder>();DockIconFace->SetBrush(FSlateNoResource());
            DockIconFace->SetPadding(FMargin(0));DockIconFace->SetHorizontalAlignment(HAlign_Fill);DockIconFace->SetVerticalAlignment(VAlign_Fill);DockIconFace->SetContent(IconInner);
            DockIconBox=WidgetTree->ConstructWidget<USizeBox>();DockIconBox->SetWidthOverride(112.f);DockIconBox->SetHeightOverride(112.f);DockIconBox->SetContent(DockIconFace);
            Left->AddChildToVerticalBox(DockIconBox)->SetHorizontalAlignment(HAlign_Center);
            DockKind=Label(TEXT(""),LDHUDStyle::TypeTiny,LDHUDStyle::UIText,LDUIFonts::ERole::Strong);DockKind->SetJustification(ETextJustify::Center);
            Left->AddChildToVerticalBox(DockKind)->SetPadding(FMargin(0,12,0,0));
            DockTags=Label(TEXT(""),LDHUDStyle::TypeTiny,LDHUDStyle::TextSecondary);DockTags->SetJustification(ETextJustify::Center);DockTags->SetAutoWrapText(true);
            Left->AddChildToVerticalBox(DockTags)->SetPadding(FMargin(0,4,0,0));
            DockLeft=Left;Add(Left,3);
            auto* Centre=WidgetTree->ConstructWidget<UVerticalBox>();
            DockName=Label(TEXT(""),32,LDHUDStyle::UIText,LDUIFonts::ERole::Title);DockName->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
            Centre->AddChildToVerticalBox(DockName);
            DockSummary=Label(TEXT(""),14,LDHUDStyle::TextSecondary);DockSummary->SetAutoWrapText(true);
            Centre->AddChildToVerticalBox(DockSummary)->SetPadding(FMargin(0,2,0,0));
            DockBody=LDWidgets::RichText(WidgetTree,17,TEXT("WorkshopDockBody"));DockBody->SetAutoWrapText(true);
            Centre->AddChildToVerticalBox(DockBody)->SetPadding(FMargin(0,8,0,0));
            DockChips=Label(TEXT(""),LDHUDStyle::TypeSmall,LDHUDStyle::ComponentLink,LDUIFonts::ERole::Strong);DockChips->SetAutoWrapText(true);
            Centre->AddChildToVerticalBox(DockChips)->SetPadding(FMargin(0,10,0,0));
            DockCentre=WorkshopFit(WidgetTree,Centre);Add(DockCentre,3);
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
            DockRight=WorkshopFit(WidgetTree,Right);Add(DockRight,3);
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
        const float PendingHeight=(Tray>0.f?Tray+52.f:72.f)+(Latest.bServices?58.f:0.f);
        // The pending area is the library panel's lower section (.inbox), so the two share one surface.
        Next.Pending=WorkshopBox(RightX,Bottom-PendingHeight,W-WorkshopMargin,Bottom);
        Next.Library=WorkshopBox(RightX,WorkshopTop,W-WorkshopMargin,Next.Pending.Min.Y);
        Next.Board=WorkshopBox(Next.Character.Max.X+WorkshopGap,WorkshopTop,RightX-WorkshopGap,Bottom-WorkshopDockHeight-WorkshopDockGap);
        Next.Dock=WorkshopBox(Next.Board.Min.X,Next.Board.Max.Y+WorkshopDockGap,Next.Board.Max.X,Bottom);
        Next.HeadIcon=100.f;
        ActionsAt=FVector2D(W-WorkshopMargin-74.f-20.f,34.f);
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
        Next.Library=WorkshopBox(RightX,Inner,W-Margin,PendingHeight>0.f?Next.Pending.Min.Y:Bottom);
        Next.Dock=WorkshopBox(Margin,Inner,Margin+WorkshopFieldDockWidth,Bottom);
        Next.Board=WorkshopBox(Next.Dock.Max.X+16.f,Inner,RightX-16.f,Bottom);
        Next.HeadIcon=64.f;
        ActionsAt=FVector2D(W-WorkshopMargin-86.f,30.f);
    }
    if(Mode==EMode::Full&&Latest.bStage)
    {
        // A build moment takes the centre: the board, the library, the pending area and the character column step aside; the
        // offer keeps a centred dock, the final mod page puts its own description there.
        const float Bottom=H-WorkshopMargin;
        Next.Dock=WorkshopBox(W*.5f-548.f,Bottom-300.f,W*.5f+548.f,Bottom);
        Next.Stage=WorkshopBox(W*.5f-820.f,96.f,W*.5f+820.f,float(Next.Dock.Min.Y)-16.f);
        Next.Character=Next.Board=Next.Library=Next.Pending=FBox2D(ForceInit);
    }
    // .cap: the capacity strip across the board's top; .cols under it, five columns with the design's gaps.
    Next.Capacity=WorkshopBox(Next.Board.Min.X,Next.Board.Min.Y,Next.Board.Max.X,Next.Board.Min.Y+(Mode==EMode::Full?WorkshopCapacityHeight:48.f));
    Next.Columns=WorkshopBox(Next.Board.Min.X+16.f,Next.Capacity.Max.Y,Next.Board.Max.X-16.f,Next.Board.Max.Y-12.f);
    Next.HeadTop=Mode==EMode::Full?WorkshopHeadTop:10.f;
    Next.HeadHeight=Mode==EMode::Full?WorkshopSocketTop:Next.HeadTop+Next.HeadIcon+64.f;
    const float ColumnGap=WorkshopColumnGap;
    const float ColumnWidth=(float(Next.Columns.GetSize().X)-ColumnGap*(LDHUD::StorageColumnCount-1))/LDHUD::StorageColumnCount;
    for(int32 Index=0;Index<LDHUD::StorageColumnCount;++Index)
    {
        const float X=float(Next.Columns.Min.X)+Index*(ColumnWidth+ColumnGap);
        Next.Column[Index]=WorkshopBox(X,Next.Columns.Min.Y,X+ColumnWidth,Next.Columns.Max.Y);
    }
    // The library body sits under its title and filter rows (the filter wraps to as many lines as it needs), and those under
    // the artisan's band when the artisan serves.
    LibraryBand=Latest.bLibraryServices&&Mode==EMode::Full?WorkshopArtisanBand:0.f;
    const float FilterHeight=FMath::Max(56.f,FilterRow?float(FilterRow->GetDesiredSize().Y):0.f);
    Next.LibraryBody=WorkshopBox(Next.Library.Min.X+24.f,Next.Library.Min.Y+LibraryBand+70.f+FilterHeight,Next.Library.Max.X-20.f,Next.Library.Max.Y-14.f);
    if(!WorkshopHasArea(Next.Board))
    {
        Next.Capacity=Next.Columns=FBox2D(ForceInit);
        for(FBox2D& Column:Next.Column){Column=FBox2D(ForceInit);}
    }
    if(!WorkshopHasArea(Next.Library)){Next.LibraryBody=FBox2D(ForceInit);}
    Rects=Next;
    TrayRight=WorkshopHasArea(Rects.Pending)?W-float(Rects.Pending.Max.X)+12.f:W-float(Rects.Library.Max.X);
    TrayBottom=WorkshopHasArea(Rects.Pending)?H-float(Rects.Pending.Max.Y)+12.f:H-float(Rects.Library.Max.Y);
    TrayWrap=WorkshopHasArea(Rects.Pending)?FMath::Max(0.f,float(Rects.Pending.GetSize().X)-24.f):0.f;
    const bool bFull=Mode==EMode::Full;
    Place(Blur,Rects.Region);
    Place(Backdrop,WorkshopBox(0.f,0.f,W,H));
    Place(PhasePanel,bFull?Rects.Phase:FBox2D(ForceInit));
    Place(CloseButton,WorkshopBox(W-WorkshopMargin-74.f,bFull?34.f:float(Rects.Region.Min.Y)-60.f,W-WorkshopMargin,bFull?86.f:float(Rects.Region.Min.Y)-8.f));
    Place(CharacterPanel,bFull?Rects.Character:FBox2D(ForceInit));
    Place(BoardPanel,Rects.Board);
    Place(DockPanel,Rects.Dock);
    Place(LibraryPanel,Rects.Library);
    Place(PendingPanel,Rects.Pending);
    if(!WorkshopHasArea(Rects.Board))
    {
        for(UWidget* Hidden:{CapacityTitle.Get(),static_cast<UWidget*>(CapacityCount.Get()),static_cast<UWidget*>(CapacityUnlock.Get()),static_cast<UWidget*>(LockBadge.Get())}){Place(Hidden,FBox2D(ForceInit));}
    }
    else
    {
        // The strip reads left to right: its label, the cells (painted from CapacityCellsAt), the count, and the next
        // unlock flush right.
        const float Mid=float(Rects.Capacity.GetCenter().Y);
        const float Left=float(Rects.Capacity.Min.X)+24.f;
        Place(CapacityTitle,WorkshopBox(Left,Mid-12.f,Left+FMath::Max(90.f,float(CapacityTitle->GetDesiredSize().X)+4.f),Mid+13.f));
        CapacityCellsAt=Left+FMath::Max(72.f,float(CapacityTitle->GetDesiredSize().X))+16.f;
        const int32 Cells=Storage.IsValid()?LDWorkshop::Capacity(View,Storage->Model(),Latest.Content).Capacity+1:1;
        const float CountX=CapacityCellsAt+float(Cells)*45.f+10.f;
        Place(CapacityCount,WorkshopBox(CountX,Mid-18.f,CountX+90.f,Mid+18.f));
        Place(CapacityUnlock,WorkshopBox(float(Rects.Capacity.Max.X)-224.f,Mid-9.f,float(Rects.Capacity.Max.X)-24.f,Mid+10.f));
        Place(LockBadge,WorkshopBox(float(Rects.Capacity.Max.X)-224.f,Mid-28.f,float(Rects.Capacity.Max.X)-24.f,Mid-10.f));
    }
    for(int32 Index=0;Index<Heads.Num();++Index)
    {
        const FBox2D& Column=Rects.Column[Index];
        Place(ColumnBacks[Index],Column);
        Place(Heads[Index].Box,WorkshopBox(Column.Min.X,Column.Min.Y+Rects.HeadTop,Column.Max.X,Column.Min.Y+Rects.HeadHeight));
        if(!FMath::IsNearlyEqual(Heads[Index].IconBox->GetWidthOverride(),Rects.HeadIcon)){Heads[Index].IconBox->SetWidthOverride(Rects.HeadIcon);Heads[Index].IconBox->SetHeightOverride(Rects.HeadIcon);}
        // The smaller field-mode art keeps its key chip and the empty slot's "+" in proportion, so neither covers the art.
        const float HeadScale=FMath::Clamp(Rects.HeadIcon/100.f,.6f,1.f);
        if(UWidget* Chip=Heads[Index].Key?Heads[Index].Key->GetParent():nullptr)
        {
            Chip->SetRenderTransformPivot(FVector2D::ZeroVector);Chip->SetRenderScale(FVector2D(FMath::Max(HeadScale,.78f)));
            Chip->SetRenderTranslation(FVector2D(-9.f,-9.f)*HeadScale);
        }
        if(Heads[Index].Glyph){Heads[Index].Glyph->SetRenderScale(FVector2D(HeadScale));}
    }
    Place(LibraryTitle,WorkshopBox(Rects.Library.Min.X+24.f,Rects.Library.Min.Y+LibraryBand+18.f,Rects.Library.Max.X-20.f,Rects.Library.Min.Y+LibraryBand+56.f));
    const bool bOwnDock=!(Mode==EMode::Full&&Latest.bFinalStage);
    if(!bOwnDock){for(UWidget* Hidden:{DockLeft.Get(),DockCentre.Get(),DockRight.Get()}){Place(Hidden,FBox2D(ForceInit));}}
    else if(Mode==EMode::Full)
    {
        // .dock: left the 150 icon column, the rule in the middle, and (when there is anything to say) the 270 column on
        // the right behind its hairline.
        const bool bRight=DockHasRight();
        const float CentreEnd=bRight?float(Rects.Dock.Max.X)-WorkshopDockRight-28.f-50.f:float(Rects.Dock.Max.X)-28.f;
        Place(DockLeft,WorkshopBox(Rects.Dock.Min.X+28.f,Rects.Dock.Min.Y+24.f,Rects.Dock.Min.X+178.f,Rects.Dock.Max.Y-16.f));
        Place(DockCentre,WorkshopBox(Rects.Dock.Min.X+204.f,Rects.Dock.Min.Y+22.f,CentreEnd,Rects.Dock.Max.Y-16.f));
        Place(DockRight,bRight?WorkshopBox(Rects.Dock.Max.X-28.f-WorkshopDockRight,Rects.Dock.Min.Y+24.f,Rects.Dock.Max.X-28.f,Rects.Dock.Max.Y-16.f):FBox2D(ForceInit));
    }
    else
    {
        // The field dock stacks the name and rule under the icon and puts where it goes beside the icon.
        Place(DockLeft,WorkshopBox(Rects.Dock.Min.X+20.f,Rects.Dock.Min.Y+16.f,Rects.Dock.Min.X+140.f,Rects.Dock.Min.Y+146.f));
        Place(DockRight,WorkshopBox(Rects.Dock.Min.X+150.f,Rects.Dock.Min.Y+16.f,Rects.Dock.Max.X-20.f,Rects.Dock.Min.Y+146.f));
        Place(DockCentre,WorkshopBox(Rects.Dock.Min.X+20.f,Rects.Dock.Min.Y+150.f,Rects.Dock.Max.X-20.f,Rects.Dock.Max.Y-12.f));
    }
    Place(FilterRow,WorkshopBox(Rects.Library.Min.X+24.f,Rects.Library.Min.Y+LibraryBand+62.f,Rects.Library.Max.X-20.f,float(Rects.LibraryBody.Min.Y)-6.f));
    Place(PendingTitle,WorkshopHasArea(Rects.Pending)?WorkshopBox(Rects.Pending.Min.X+46.f,Rects.Pending.Min.Y+16.f,Rects.Pending.Min.X+200.f,Rects.Pending.Min.Y+38.f):FBox2D(ForceInit));
    {
        // The dock columns scale to fit, so every wrapping line wraps at its column's width rather than at what it is given.
        const float CentreWrap=Mode==EMode::Full?float(Rects.Dock.GetSize().X)-204.f-28.f-(DockHasRight()?WorkshopDockRight+50.f:0.f):WorkshopFieldDockWidth-40.f;
        const float RightWrap=Mode==EMode::Full?WorkshopDockRight:WorkshopFieldDockWidth-170.f;
        if(DockBody){DockBody->SetWrapTextAt(CentreWrap);}
        for(UTextBlock* Line:{DockSummary.Get(),DockChips.Get()}){if(Line){Line->SetAutoWrapText(false);Line->SetWrapTextAt(CentreWrap);}}
        for(UTextBlock* Line:{DockWhere.Get(),DockLinks.Get(),DockNotes.Get()}){if(Line){Line->SetAutoWrapText(false);Line->SetWrapTextAt(RightWrap);}}
    }
    const float DockArtSize=Mode==EMode::Full?112.f:80.f;
    if(DockIconBox&&!FMath::IsNearlyEqual(DockIconBox->GetWidthOverride(),DockArtSize)){DockIconBox->SetWidthOverride(DockArtSize);DockIconBox->SetHeightOverride(DockArtSize);}
}

ULDChoicesPanel::FWorkshopPlaces ULDWorkshop::Places() const
{
    ULDChoicesPanel::FWorkshopPlaces Out;
    if(Mode!=EMode::Full||!Canvas){return Out;}
    const FGeometry& Geometry=GetCachedGeometry();
    if(Geometry.GetLocalSize().X<=0.f){return Out;}
    const auto Absolute=[&Geometry](const FBox2D& Box)
    {
        if(!WorkshopHasArea(Box)){return FSlateRect();}
        const FVector2D A=Geometry.LocalToAbsolute(Box.Min),B=Geometry.LocalToAbsolute(Box.Max);
        return FSlateRect(A.X,A.Y,B.X,B.Y);
    };
    Out.bActive=true;
    Out.Stage=Absolute(Rects.Stage);
    Out.Dock=Absolute(Rects.Dock);
    for(int32 Index=0;Index<LDHUD::StorageColumnCount;++Index)
    {
        const FBox2D& Column=Rects.Column[Index];
        Out.Heads[Index]=WorkshopHasArea(Column)?Absolute(WorkshopBox(Column.Min.X,Column.Min.Y,Column.Max.X,Column.Min.Y+Rects.HeadHeight)):FSlateRect();
    }
    if(WorkshopHasArea(Rects.Character)&&RespecSpace&&Latest.bCharacterServices)
    {
        // The trainer's respec spans the foot of the character column (below the reactions).
        const FGeometry& Foot=RespecSpace->GetCachedGeometry();
        const FVector2D A=Foot.GetAbsolutePosition(),B=A+Foot.GetAbsoluteSize();
        Out.CharacterFoot=FSlateRect(A.X,A.Y+18.f*Foot.Scale,B.X,B.Y);
    }
    if(WorkshopHasArea(Rects.Library)&&LibraryBand>0.f)
    {Out.LibraryTop=Absolute(WorkshopBox(Rects.Library.Min.X+24.f,Rects.Library.Min.Y+14.f,Rects.Library.Max.X-20.f,Rects.Library.Min.Y+LibraryBand));}
    for(int32 Index=0;Index<Heads.Num()&&Index<LDHUD::StorageColumnCount;++Index)
    {
        if(!WorkshopHasArea(Rects.Column[Index])||!Heads[Index].IconFace){continue;}
        const FGeometry& Icon=Heads[Index].IconFace->GetCachedGeometry();
        const FVector2D A=Icon.GetAbsolutePosition(),B=A+Icon.GetAbsoluteSize();
        Out.HeadIcons[Index]=FSlateRect(A.X,A.Y,B.X,B.Y);
    }
    if(WorkshopHasArea(Rects.Pending))
    {Out.PendingTop=Absolute(WorkshopBox(Rects.Pending.Min.X+24.f,Rects.Pending.Min.Y+44.f,Rects.Pending.Max.X-20.f,Rects.Pending.Min.Y+96.f));}
    return Out;
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
        if(Previous!=EMode::Closed){LDWidgets::PlayCue(this,ELDAudioCue::UIWorkshopClose);}
        WorkshopShown(this,false);OpenedAt=-1.;
        return;
    }
    if(Previous==EMode::Closed||OpenedAt<0.){OpenedAt=UiNow();DockKey.Reset();LDWidgets::PlayCue(this,ELDAudioCue::UIWorkshopOpen);}
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
    // The final mod page turns the dark blue to a deep magenta: this step is not like the others.
    const FLinearColor Tint=Mode==EMode::Full&&Input.bFinalStage?FLinearColor(FColor(0x2A,0x06,0x1C)):LDHUDStyle::UIBase;
    if(Shade){Shade->SetBrushColor(Tint.CopyWithNewOpacity((Mode==EMode::Full?(Input.bFinalStage?.82f:.62f):.88f)*Open));}
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
    // Beside an NPC its name joins the rest line: its services stand on the board.
    const FString Rest=Latest.bRest?FString::Printf(TEXT("休息 R%u"),uint32(Latest.RestIndex)):FString();
    WorkshopText(PhaseRest,Latest.NpcName.IsEmpty()?Rest:Rest+TEXT("  ·  ")+Latest.NpcName);
    PhaseRest->SetColorAndOpacity(Latest.NpcName.IsEmpty()?LDHUDStyle::TextSecondary:LDHUDStyle::UIAction);
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
    if(RespecSpace){const float Foot=Latest.bCharacterServices?96.f:0.f;if(RespecSpace->GetHeightOverride()!=Foot){RespecSpace->SetHeightOverride(Foot);}}
    WorkshopText(PointsText,FString::FromInt(Unspent));
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
            // .stat: the 44 icon, the name, the sixteen-cell meter filling the middle, the value and the yellow "+".
            auto* IconBox=WidgetTree->ConstructWidget<USizeBox>();IconBox->SetWidthOverride(44.f);IconBox->SetHeightOverride(44.f);IconBox->SetContent(IconStack);
            Line->AddChildToHorizontalBox(IconBox)->SetVerticalAlignment(VAlign_Center);Row.Icon=IconBox;
            Row.Name=LDWidgets::Text(WidgetTree,TEXT(""),18,NAME_None,LDUIFonts::ERole::Strong);Row.Name->SetAutoWrapText(false);
            Row.Name->SetColorAndOpacity(LDHUDStyle::UIText);
            auto* NameBox=WidgetTree->ConstructWidget<USizeBox>();NameBox->SetWidthOverride(52.f);NameBox->SetContent(Row.Name);
            auto* NameSlot=Line->AddChildToHorizontalBox(NameBox);NameSlot->SetVerticalAlignment(VAlign_Center);NameSlot->SetPadding(FMargin(12,0,0,0));
            auto* Meter=WidgetTree->ConstructWidget<USizeBox>();Meter->SetHeightOverride(10.f);Row.Meter=Meter;
            auto* MeterSlot=Line->AddChildToHorizontalBox(Meter);MeterSlot->SetVerticalAlignment(VAlign_Center);MeterSlot->SetPadding(FMargin(12,0,0,0));
            MeterSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            Row.Points=LDWidgets::Text(WidgetTree,TEXT(""),24,NAME_None,LDUIFonts::ERole::Number);Row.Points->SetAutoWrapText(false);
            Row.Points->SetColorAndOpacity(LDHUDStyle::UIText);Row.Points->SetJustification(ETextJustify::Right);
            auto* PointsBox34=WidgetTree->ConstructWidget<USizeBox>();PointsBox34->SetWidthOverride(34.f);PointsBox34->SetContent(Row.Points);
            Line->AddChildToHorizontalBox(PointsBox34)->SetVerticalAlignment(VAlign_Center);
            if(UHorizontalBoxSlot* PointsSlot=Cast<UHorizontalBoxSlot>(PointsBox34->Slot)){PointsSlot->SetPadding(FMargin(12,0,0,0));}
            Built.Add(NameBox);Built.Add(PointsBox34);
            const int32 StatIndex=Index;
            Row.Plus=LDWidgets::Button(WidgetTree,TEXT("+"),[this,StatIndex](){if(StatBar.IsValid()&&StatBar->Allocate(StatIndex)){LDWidgets::PlayCue(this,ELDAudioCue::UIAddPoint);StatRows[StatIndex].AddedAt=UiNow();}},true,
                FName(*FString::Printf(TEXT("WorkshopStatPlus%d"),StatIndex)));
            LDWidgets::SetLook(Row.Plus,LDUIPaint::EButton::Plus);
            if(UTextBlock* PlusText=Cast<UTextBlock>(Row.Plus->GetContent()))
            {PlusText->SetFont(LDUIFonts::Font(LDUIFonts::ERole::Strong,24));PlusText->SetJustification(ETextJustify::Center);PlusText->SetRenderTranslation(FVector2D(0,-3));}
            {auto Style=Row.Plus->GetStyle();Style.NormalPadding=FMargin(0);Style.PressedPadding=FMargin(0);Row.Plus->SetStyle(Style);}
            auto* PlusBox=WidgetTree->ConstructWidget<USizeBox>();PlusBox->SetWidthOverride(36.f);PlusBox->SetHeightOverride(36.f);PlusBox->SetContent(Row.Plus);
            auto* PlusSlot=Line->AddChildToHorizontalBox(PlusBox);PlusSlot->SetPadding(FMargin(12,0,0,0));PlusSlot->SetVerticalAlignment(VAlign_Center);
            auto* Frame=WidgetTree->ConstructWidget<UBorder>();Frame->SetBrushColor(FLinearColor::Transparent);Frame->SetPadding(FMargin(0));Frame->SetContent(Line);
            Frame->SetVerticalAlignment(VAlign_Center);Frame->SetVisibility(ESlateVisibility::Visible);Row.Row=Frame;
            auto* RowHeight=WidgetTree->ConstructWidget<USizeBox>();RowHeight->SetHeightOverride(66.f);RowHeight->SetContent(Frame);Built.Add(RowHeight);
            StatList->AddChildToVerticalBox(RowHeight);
            for(UTextBlock* Fresh:{Row.Glyph,Row.Name,Row.Points,Cast<UTextBlock>(Row.Plus->GetContent())}){ScaleFresh(Fresh);}
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
        UTextBlock* Name=LDWidgets::Text(WidgetTree,TEXT(""),14);Name->SetAutoWrapText(false);Name->SetColorAndOpacity(LDHUDStyle::TextSecondary);
        Name->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
        UTextBlock* Value=LDWidgets::Text(WidgetTree,TEXT(""),16,NAME_None,LDUIFonts::ERole::Number);Value->SetAutoWrapText(false);
        Value->SetColorAndOpacity(LDHUDStyle::UIText);Value->SetJustification(ETextJustify::Right);
        auto* Half=WidgetTree->ConstructWidget<UHorizontalBox>();Built.Add(Half);
        auto* NameSlot=Half->AddChildToHorizontalBox(Name);NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));NameSlot->SetVerticalAlignment(VAlign_Center);
        Half->AddChildToHorizontalBox(Value)->SetPadding(FMargin(12,0,0,0));
        auto* HalfSlot=Line->AddChildToHorizontalBox(Half);HalfSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));HalfSlot->SetPadding(FMargin(Index%2?18.f:0.f,0,0,5));
        AttributeHalves.Add(Half);
        AttributeNames.Add(Name);AttributeValues.Add(Value);ScaleFresh(Name);ScaleFresh(Value);
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
        WorkshopText(Reaction.FirstText,LDHUD::ElementName(Rows[Index].First));
        WorkshopText(Reaction.SecondText,LDHUD::ElementName(Rows[Index].Second));
        Reaction.bLit=Rows[Index].bLit;
        // The rows and discs are painted; a lit row's text turns to the paper colour.
        Reaction.Name->SetColorAndOpacity(Reaction.bLit?LDHUDStyle::UIText:LDUIPaint::Hex(0x667086));
        Reaction.Pair->SetColorAndOpacity(Reaction.bLit?LDHUDStyle::UIText:LDUIPaint::Hex(0x667086));
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
            // The art and its icon are painted (LDUIPaint::SkillArt); the glyph stands in when there is no picture.
            Head.AppliedIcon=Shown.IconId;
            Head.Icon=Shown.IconId.IsEmpty()?nullptr:LDUIIcons::Find(Shown.IconId);
            WorkshopShown(Head.Glyph,!Head.Icon.IsValid(),ESlateVisibility::HitTestInvisible);
            WorkshopShown(Head.Picture,false);
        }
        Head.Glyph->SetFont(LDUIFonts::Font(LDUIFonts::ERole::Title,Column==LDHUD::EStorageColumn::Body?40:28));
        WorkshopText(Head.Mana,Shown.Mana);WorkshopShown(Head.Mana,!Shown.Mana.IsEmpty(),ESlateVisibility::HitTestInvisible);
        WorkshopText(Head.Cooldown,Shown.Cooldown);WorkshopShown(Head.Cooldown,!Shown.Cooldown.IsEmpty(),ESlateVisibility::HitTestInvisible);
        {
            // A locked final mod shows its icon over the head of what it binds (本体 when it binds the holder).
            const FLDFinalModSelection& Final=View.FinalMod;
            const bool bHere=!Final.IsEmpty()&&((Final.TargetKind==ELDComponentTargetKind::Active&&Final.TargetId==Active&&!Active.IsNone())
                ||(Final.TargetKind==ELDComponentTargetKind::Dash&&Column==LDHUD::EStorageColumn::Dash)
                ||(Final.TargetKind==ELDComponentTargetKind::None&&Column==LDHUD::EStorageColumn::Body));
            if(!bHere||!LDUIIcons::Apply(Head.Final,LDHUD::ContentIconId(Latest.Content,Final.DefinitionId.ToString()),17.f)){WorkshopShown(Head.Final,false);}
        }
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
    // The icon is painted (a skill's gold art); the glyph stands in when there is no picture.
    DockIcon=IconId.IsEmpty()?nullptr:LDUIIcons::Find(IconId);
    WorkshopShown(DockPicture,false);
    WorkshopShown(DockGlyph,!DockIcon.IsValid(),ESlateVisibility::HitTestInvisible);
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
    // The icon is painted as the component's own tile (LDUIPaint::Tile); the glyph stands in when there is no picture.
    DockIcon=LDUIIcons::Find(Card.IconId);DockScarce=Facts.bScarce;
    WorkshopShown(DockPicture,false);
    WorkshopShown(DockGlyph,!DockIcon.IsValid(),ESlateVisibility::HitTestInvisible);
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
FReply ULDWorkshop::NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event)
{
    if(Event.GetEffectingButton()==EKeys::RightMouseButton&&IsOpen()&&Choices.IsValid())
    {
        const FVector2D At=Event.GetScreenSpacePosition();
        for(int32 Index=0;Index<Heads.Num();++Index)
        {
            const UWidget* Head=Heads[Index].Box;
            if(!Head||!Head->IsVisible()){continue;}
            const FGeometry& HeadGeometry=Head->GetCachedGeometry();
            if(HeadGeometry.IsUnderLocation(At)&&Choices->ForgetAtColumn(Index)){return FReply::Handled();}
        }
    }
    return Super::NativeOnMouseButtonDown(Geometry,Event);
}

bool ULDWorkshop::DockHasRight() const
{
    for(const UTextBlock* Section:{DockWhere.Get(),DockLinks.Get(),DockNotes.Get()}){if(Section&&Section->GetVisibility()!=ESlateVisibility::Collapsed&&!Section->GetText().IsEmpty()){return true;}}
    return false;
}

void ULDWorkshop::PaintBackdrop(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry) const
{
    if(!IsOpen()){return;}
    using LDUIPaint::FFill;using LDUIPaint::Hex;using LDUIShapes::EShape;
    const double Now=UiNow();
    const auto Box=[&Geometry](const UWidget* Widget){return Widget?LDUIShapes::RectIn(Geometry,Widget->GetCachedGeometry()):FBox2D(ForceInit);};
    const auto Shown=[](const UWidget* Widget){return Widget&&Widget->IsVisible()&&Widget->GetCachedGeometry().GetLocalSize().X>1.f;};
    const int32 Base=Layer,Ink=Layer+3;
    const FLinearColor Line=FLinearColor(1,1,1,.075f);
    const bool bFull=Mode==EMode::Full;
    // ---- surfaces ----
    if(bFull&&WorkshopHasArea(Rects.Phase))
    {
        // .phase: a horizontal fade from the solid surface, its hairline and highlight.
        LDUIPaint::Shape(Elements,Base,Geometry,EShape::Rounded,Rects.Phase,6.f,FFill::Linear(90.f,{{0.f,Hex(0x121723,.95f)},{1.f,Hex(0x121723,.7f)}}));
        LDUIPaint::Edge(Elements,Base,Geometry,EShape::Rounded,Rects.Phase,6.f,Line,1.f);
        LDUIPaint::Line(Elements,Base,Geometry,FVector2D(Rects.Phase.Min.X+6.f,Rects.Phase.Min.Y+1.5f),FVector2D(Rects.Phase.Max.X-6.f,Rects.Phase.Min.Y+1.5f),FLinearColor(1,1,1,.08f),1.f);
    }
    if(bFull&&WorkshopHasArea(Rects.Character)){LDUIPaint::Panel(Elements,Base,Geometry,Rects.Character);}
    if(WorkshopHasArea(Rects.Board)){LDUIPaint::Panel(Elements,Base,Geometry,Rects.Board);}
    if(WorkshopHasArea(Rects.Dock))
    {
        LDUIPaint::Panel(Elements,Base,Geometry,Rects.Dock);
        if(bFull&&DockRight&&DockRight->IsVisible()&&!(Latest.bFinalStage))
        {
            const float X=float(Rects.Dock.Max.X)-28.f-WorkshopDockRight-24.f;
            LDUIPaint::Line(Elements,Base+3,Geometry,FVector2D(X,Rects.Dock.Min.Y+24.f),FVector2D(X,Rects.Dock.Max.Y-24.f),Line,1.f);
        }
    }
    if(WorkshopHasArea(Rects.Library))
    {
        FBox2D Right=Rects.Library;
        if(WorkshopHasArea(Rects.Pending)){Right+=Rects.Pending;}
        LDUIPaint::Panel(Elements,Base,Geometry,Right);
    }
    if(WorkshopHasArea(Rects.Pending))
    {
        // .inbox: the library's lower section under a hairline, warmed with a faint yellow, its square-led title and rule.
        const FBox2D& P=Rects.Pending;
        LDUIPaint::Shape(Elements,Base+3,Geometry,EShape::Rounded,WorkshopBox(P.Min.X+1.f,P.Min.Y,P.Max.X-1.f,P.Max.Y-1.f),5.f,
            FFill::Linear(180.f,{{0.f,LDHUDStyle::UIAction.CopyWithNewOpacity(.05f)},{.6f,LDHUDStyle::UIAction.CopyWithNewOpacity(0.f)}}));
        LDUIPaint::Line(Elements,Base+3,Geometry,FVector2D(P.Min.X+1.f,P.Min.Y),FVector2D(P.Max.X-1.f,P.Min.Y),Line,1.f);
        const float Y=float(P.Min.Y)+27.f;
        LDUIPaint::Shape(Elements,Ink,Geometry,EShape::Rounded,WorkshopBox(P.Min.X+24.f,Y-6.f,P.Min.X+36.f,Y+6.f),2.f,FFill::Solid(LDHUDStyle::UIAction));
        if(Shown(PendingTitle))
        {
            const FBox2D Title=Box(PendingTitle);
            LDUIPaint::Line(Elements,Ink,Geometry,FVector2D(Title.Min.X+PendingTitle->GetDesiredSize().X+10.f,Y),FVector2D(P.Max.X-24.f,Y),Line,1.f);
        }
    }
    if(Shown(DockIconFace)&&!DockKey.IsEmpty())
    {
        // .dock .l: the hovered item at 112, a component in its own tile, a skill in its gold art.
        const FBox2D Art=Box(DockIconFace);
        if(DockKey.StartsWith(TEXT("C|"))){LDUIPaint::Tile(Elements,Ink,Geometry,Art,DockKindValue,DockScarce,LDUIPaint::ETileState::Normal,DockIcon.Get(),1.f,Now);}
        else{LDUIPaint::SkillArt(Elements,Ink,Geometry,Art,false,!DockIcon.IsValid()&&DockGlyph&&DockGlyph->GetText().IsEmpty(),DockIcon.Get());}
    }
    // ---- phase ring and steps ----
    if(bFull&&PhaseClock&&WorkshopHasArea(Rects.Phase))
    {
        const UWidget* Clock=PhaseClock->GetParent()?static_cast<const UWidget*>(PhaseClock->GetParent()):PhaseClock.Get();
        const FVector2D Centre=Box(Clock).GetCenter();
        float Fraction=0.f;
        if(Latest.bRest&&Latest.bClock&&Latest.StepEndsAtMs>Latest.StepStartedAtMs)
        {Fraction=FMath::Clamp(float(double(Latest.StepEndsAtMs-Latest.NowMs)/double(Latest.StepEndsAtMs-Latest.StepStartedAtMs)),0.f,1.f);}
        LDUIShapes::Arc(Elements,Ink,Geometry,Centre,18.f,1.f,FLinearColor(1,1,1,.14f),4.f);
        if(Fraction>0.f){LDUIShapes::Arc(Elements,Ink+1,Geometry,Centre,18.f,Fraction,Latest.bDead?LDHUDStyle::UIReject:LDHUDStyle::UIAction,4.f);}
        if(Latest.bRest&&!Latest.bDead)
        {
            const int32 Current=LDWorkshop::RestStepDot(Latest.Step);
            const float Width=34.f,Gap=6.f;
            const float X0=float(Rects.Phase.Max.X)-22.f-LDWorkshop::RestStepDots*Width-(LDWorkshop::RestStepDots-1)*Gap,Y=float(Rects.Phase.GetCenter().Y);
            for(int32 Dot=0;Dot<LDWorkshop::RestStepDots;++Dot)
            {
                const FBox2D Bar=WorkshopBox(X0+Dot*(Width+Gap),Y-2.5f,X0+Dot*(Width+Gap)+Width,Y+2.5f);
                if(Dot==Current){LDUIPaint::Halo(Elements,Ink,Geometry,EShape::Rounded,Bar,3.f,LDHUDStyle::UIAction.CopyWithNewOpacity(.6f),10.f);}
                const FLinearColor Color=Dot==Current?LDHUDStyle::UIAction:Dot<Current?LDHUDStyle::UIAction.CopyWithNewOpacity(.45f):FLinearColor(1,1,1,.14f);
                LDUIPaint::Shape(Elements,Ink+1,Geometry,EShape::Rounded,Bar,3.f,FFill::Solid(Color));
            }
        }
    }
    // ---- the character column ----
    if(bFull&&WorkshopHasArea(Rects.Character))
    {
        if(Shown(PortraitFrame))
        {
            // .ava: the experience ring around the holder's face.
            const FBox2D Ava=Box(PortraitFrame);
            const FVector2D Centre=Ava.GetCenter();
            LDUIShapes::Arc(Elements,Ink,Geometry,Centre,40.f,1.f,FLinearColor(1,1,1,.12f),5.f);
            const float Xp=Latest.Level.bMax?1.f:FMath::Clamp(Latest.Level.Fraction,0.f,1.f);
            if(Xp>0.f){LDUIShapes::Arc(Elements,Ink+1,Geometry,Centre,40.f,Xp,Hex(0xE0A53A),5.f);}
            const FBox2D Face=LDUIPaint::Grow(Ava,-7.f);
            const FLinearColor Holder=Latest.HolderColor;
            LDUIPaint::Shape(Elements,Ink,Geometry,EShape::Circle,Face,36.f,FFill::Linear(160.f,{{0.f,FLinearColor::LerpUsingHSV(Holder,FLinearColor::White,.18f)},{1.f,Holder*.42f}}));
            LDUIPaint::Edge(Elements,Ink+1,Geometry,EShape::Circle,Face,36.f,FLinearColor(1,1,1,.25f),2.f);
        }
        if(Shown(PointsBox))
        {
            const FBox2D Pts=Box(PointsBox);
            LDUIPaint::Shape(Elements,Ink,Geometry,EShape::Rounded,Pts,5.f,FFill::Linear(90.f,{{0.f,LDHUDStyle::UIAction.CopyWithNewOpacity(.2f)},{1.f,LDHUDStyle::UIAction.CopyWithNewOpacity(.03f)}}));
            LDUIPaint::Edge(Elements,Ink,Geometry,EShape::Rounded,Pts,5.f,LDHUDStyle::UIAction.CopyWithNewOpacity(.5f),1.f);
        }
        for(const FStatRow& Row:StatRows)
        {
            if(Shown(Row.Icon)){LDUIPaint::Edge(Elements,Ink+2,Geometry,EShape::Rounded,Box(Row.Icon),8.f,FLinearColor(1,1,1,.14f),1.f);}
            if(!Shown(Row.Meter)){continue;}
            // .bar: sixteen cells, the allocated ones a warm gradient; the cell a click just asked for flashes.
            const FBox2D Meter=Box(Row.Meter);
            const float Gap=2.f,Cell=(float(Meter.GetSize().X)-Gap*(WorkshopMeterCells-1))/WorkshopMeterCells;
            const float Y=float(Meter.GetCenter().Y);
            for(int32 Index=0;Index<WorkshopMeterCells;++Index)
            {
                const FBox2D Piece=WorkshopBox(Meter.Min.X+Index*(Cell+Gap),Y-4.f,Meter.Min.X+Index*(Cell+Gap)+Cell,Y+4.f);
                const FFill Fill=Index<Row.Value?FFill::Linear(180.f,{{0.f,Hex(0xFFF2B0)},{1.f,Hex(0xFFB52E)}}):FFill::Solid(FLinearColor(1,1,1,.08f));
                LDUIPaint::Shape(Elements,Ink,Geometry,EShape::Rounded,Piece,1.f,Fill);
            }
            const double Age=Now-Row.AddedAt;
            if(Age>=0.&&Age<LDHUDStyle::MotionAddPoint*3.&&Row.Value<WorkshopMeterCells)
            {
                const FBox2D Next=WorkshopBox(Meter.Min.X+Row.Value*(Cell+Gap),Y-5.f,Meter.Min.X+Row.Value*(Cell+Gap)+Cell,Y+5.f);
                LDUIPaint::Shape(Elements,Ink+1,Geometry,EShape::Rounded,Next,1.f,FFill::Solid(FLinearColor(1,1,1,1.f-float(Age/(LDHUDStyle::MotionAddPoint*3.)))));
            }
        }
        for(const TObjectPtr<UWidget>& Half:AttributeHalves)
        {
            if(!Shown(Half)){continue;}
            const FBox2D Cell=Box(Half);
            LDUIPaint::Line(Elements,Ink,Geometry,FVector2D(Cell.Min.X,Cell.Max.Y+5.f),FVector2D(Cell.Max.X,Cell.Max.Y+5.f),FLinearColor(1,1,1,.05f),1.f);
        }
        if(Shown(AttributeList))
        {
            // .rule: the fading hairline over the derived attributes.
            const FBox2D List=Box(AttributeList);
            LDUIPaint::Line(Elements,Ink,Geometry,FVector2D(List.Min.X,List.Min.Y-14.f),FVector2D(List.Max.X,List.Min.Y-14.f),FLinearColor(1,1,1,.14f),1.f,FLinearColor(1,1,1,0.f));
        }
        for(const FReaction& Reaction:Reactions)
        {
            if(!Shown(Reaction.Row)){continue;}
            const FBox2D Row=Box(Reaction.Row);
            if(Reaction.bLit)
            {
                LDUIPaint::Halo(Elements,Ink,Geometry,EShape::Rounded,Row,5.f,Hex(0xFF7A3D,.2f),18.f);
                LDUIPaint::Shape(Elements,Ink+1,Geometry,EShape::Rounded,Row,5.f,FFill::Linear(90.f,{{0.f,Hex(0xFF7A3D,.2f)},{1.f,Hex(0xFFE45C,.06f)}}));
                LDUIPaint::Edge(Elements,Ink+1,Geometry,EShape::Rounded,Row,5.f,Hex(0xFF7A3D,.65f),1.f);
            }
            else
            {
                LDUIPaint::Shape(Elements,Ink+1,Geometry,EShape::Rounded,Row,5.f,FFill::Solid(FLinearColor(0,0,0,.25f)));
                LDUIPaint::Edge(Elements,Ink+1,Geometry,EShape::Rounded,Row,5.f,Line,1.f);
            }
            const float Disc=Reaction.bLit?1.f:.45f;
            if(Reaction.FirstText&&Reaction.FirstText->GetParent()){LDUIPaint::ElementDisc(Elements,Ink+2,Geometry,Box(Reaction.FirstText->GetParent()),WorkshopElementColor(Reaction.First),Disc);}
            if(Reaction.SecondText&&Reaction.SecondText->GetParent()){LDUIPaint::ElementDisc(Elements,Ink+2,Geometry,Box(Reaction.SecondText->GetParent()),WorkshopElementColor(Reaction.Second),Disc);}
        }
    }
    // ---- the board ----
    if(WorkshopHasArea(Rects.Capacity))
    {
        LDUIPaint::Line(Elements,Ink,Geometry,FVector2D(Rects.Board.Min.X+1.f,Rects.Capacity.Max.Y),FVector2D(Rects.Board.Max.X-1.f,Rects.Capacity.Max.Y),Line,1.f);
        if(Storage.IsValid())
        {
            // .segs: one skewed cell per workshop slot in the kind colour of what fills it; the next level's cell dashed.
            // Hovering or holding a stored component previews where it lands: the next free cell pulses green, cells a
            // replacement frees turn orange.
            const LDWorkshop::FCapacityView Capacity=LDWorkshop::Capacity(View,Storage->Model(),Latest.Content);
            const ULDComponentStorage::FFocus& Focus=Storage->Focus();
            TArray<FName> Freed;
            if(Focus.bOverDrop&&!Focus.Drop.Replacing.IsNone()){Freed.Add(Focus.Drop.Replacing);Freed.Append(Focus.Drop.Lost);}
            const bool bAdds=!Focus.Component.IsNone()&&!Focus.bInstalled&&Focus.Drops.ContainsByPredicate([](const LDHUD::FComponentDrop& Drop){return Drop.Replacing.IsNone();});
            const float Pulse=.55f+.45f*LDTween::PingPong(Now,.8);
            const int32 Cells=Capacity.Capacity+(Capacity.UnlockLevel>0?1:0);
            const float Y=float(Rects.Capacity.GetCenter().Y),Skew=7.f*FMath::Tan(FMath::DegreesToRadians(18.f));
            for(int32 Index=0;Index<Cells;++Index)
            {
                const float X=CapacityCellsAt+Index*45.f;
                const TArray<FVector2D> Cell{FVector2D(X+Skew,Y-7.f),FVector2D(X+40.f+Skew,Y-7.f),FVector2D(X+40.f-Skew,Y+7.f),FVector2D(X-Skew,Y+7.f)};
                if(Index>=Capacity.Capacity)
                {
                    TArray<FVector2D> Loop=Cell;Loop.Add(FVector2D(Cell[0]));
                    for(int32 Edge=0;Edge<4;++Edge)
                    {
                        const FVector2D From=Loop[Edge],To=Loop[Edge+1];
                        for(float At=0.f;At<1.f;At+=.25f){LDUIPaint::Line(Elements,Ink,Geometry,FMath::Lerp(From,To,double(At)),FMath::Lerp(From,To,double(At+.14f)),FLinearColor(1,1,1,.25f),1.f);}
                    }
                    continue;
                }
                FLinearColor Color=FLinearColor(1,1,1,.09f);float Glow=0.f;
                if(Capacity.Used.IsValidIndex(Index))
                {
                    const uint8 Kind=uint8(Capacity.Used[Index]);
                    Color=LDUIShapes::KindColor(Kind);Glow=Kind==0?0.f:.5f;
                    if(Freed.Contains(Capacity.Components[Index])){Color=LDHUDStyle::UIReplace;Glow=.6f;}
                    else if(Focus.bHeld&&Focus.bInstalled&&Capacity.Components[Index]==Focus.Component){Color=LDHUDStyle::UIAction;Glow=.6f;}
                }
                else if(bAdds&&Index==Capacity.Used.Num()){Color=LDHUDStyle::UILegal.CopyWithNewOpacity(Pulse);Glow=.7f*Pulse;}
                if(Glow>0.f){LDUIPaint::Halo(Elements,Ink,Geometry,EShape::Rounded,WorkshopBox(X,Y-7.f,X+40.f,Y+7.f),2.f,Color.CopyWithNewOpacity(Glow*.6f),8.f);}
                LDUIPaint::Polygon(Elements,Ink+1,Geometry,Cell,FFill::Solid(Color));
                if(!Capacity.Used.IsValidIndex(Index))
                {
                    TArray<FVector2D> Loop=Cell;Loop.Add(FVector2D(Cell[0]));
                    LDUIShapes::Stroke(Elements,Ink+1,Geometry,Loop,FLinearColor(1,1,1,.1f),1.f);
                }
            }
        }
    }
    const ULDComponentStorage::FFocus* Focus=Storage.IsValid()?&Storage->Focus():nullptr;
    for(int32 Index=0;Index<Heads.Num()&&Index<LDHUD::StorageColumnCount;++Index)
    {
        const FBox2D& Column=Rects.Column[Index];
        if(!WorkshopHasArea(Column)){continue;}
        // .col .bgc: the column's faint surface under its head; a column that takes the hovered or held component lights
        // green (a replacement orange). .rail: the line the sockets hang on.
        const LDHUD::EStorageColumn Id=static_cast<LDHUD::EStorageColumn>(Index);
        const bool bTakes=Focus&&Focus->Drops.ContainsByPredicate([Id](const LDHUD::FComponentDrop& Drop){return Drop.Column==Id&&Drop.Replacing.IsNone();});
        const bool bSwaps=Focus&&!bTakes&&Focus->Drops.ContainsByPredicate([Id](const LDHUD::FComponentDrop& Drop){return Drop.Column==Id;});
        const FBox2D Back=WorkshopBox(Column.Min.X,Column.Min.Y+(bFull?44.f:Rects.HeadTop+Rects.HeadIcon*.5f),Column.Max.X,Column.Max.Y);
        if(bTakes||bSwaps)
        {
            const FLinearColor Tone=bTakes?LDHUDStyle::UILegal:LDHUDStyle::UIReplace;
            LDUIPaint::Halo(Elements,Ink,Geometry,EShape::Rounded,Back,6.f,Tone.CopyWithNewOpacity(.22f),30.f);
            LDUIPaint::Shape(Elements,Ink+1,Geometry,EShape::Rounded,Back,6.f,FFill::Linear(180.f,{{0.f,Tone.CopyWithNewOpacity(.16f)},{1.f,Tone.CopyWithNewOpacity(.03f)}}));
            LDUIPaint::InnerHalo(Elements,Ink+1,Geometry,EShape::Rounded,Back,6.f,Tone.CopyWithNewOpacity(.12f),30.f);
            LDUIPaint::Edge(Elements,Ink+1,Geometry,EShape::Rounded,Back,6.f,Tone.CopyWithNewOpacity(.75f),1.f);
        }
        else
        {
            LDUIPaint::Shape(Elements,Ink+1,Geometry,EShape::Rounded,Back,6.f,FFill::Linear(180.f,{{0.f,FLinearColor(1,1,1,.035f)},{1.f,FLinearColor(1,1,1,.01f)}}));
            LDUIPaint::Edge(Elements,Ink+1,Geometry,EShape::Rounded,Back,6.f,FLinearColor(1,1,1,.045f),1.f);
        }
        if(bFull)
        {
            LDUIPaint::Line(Elements,Ink+1,Geometry,FVector2D(Column.Min.X+37.f,Column.Min.Y+WorkshopSocketTop-2.f),FVector2D(Column.Min.X+37.f,Column.Max.Y-26.f),
                FLinearColor(1,1,1,.2f),2.f,FLinearColor(1,1,1,.04f));
        }
        // The head: the skill art (gold for an active, silver for the body and the dash), the body's face in the holder's
        // colour, and the numbers' pills.
        const FHead& Head=Heads[Index];
        if(!Shown(Head.Box)||!Head.IconFace){continue;}
        const FBox2D Art=Box(Head.IconFace);
        const bool bSilver=Id==LDHUD::EStorageColumn::Body||Id==LDHUD::EStorageColumn::Dash;
        LDUIPaint::SkillArt(Elements,Ink+2,Geometry,Art,bSilver,Head.bEmpty,Head.Icon.Get());
        if(Id==LDHUD::EStorageColumn::Body)
        {
            const FLinearColor Holder=Latest.HolderColor;
            LDUIPaint::Shape(Elements,Ink+6,Geometry,EShape::Rounded,LDUIPaint::Grow(Art,-3.f),7.f,FFill::Linear(160.f,{{0.f,FLinearColor::LerpUsingHSV(Holder,FLinearColor::White,.18f)},{1.f,Holder*.42f}}));
        }
        for(const UTextBlock* Number:{Head.Mana,Head.Cooldown})
        {
            if(!Shown(Number)){continue;}
            const FBox2D Text=Box(Number);
            const float Mid=float(Text.GetCenter().Y);
            const FBox2D Pill=WorkshopBox(Text.Min.X-WorkshopPillLead,Mid-11.f,Text.Max.X+WorkshopPillTail,Mid+11.f);
            LDUIPaint::Pill(Elements,Ink+2,Geometry,Pill);
            const FVector2D Dot(Pill.Min.X+12.f,Mid);
            if(Number==Head.Mana){LDUIPaint::Shape(Elements,Ink+3,Geometry,EShape::Circle,WorkshopBox(Dot.X-4.f,Mid-4.f,Dot.X+4.f,Mid+4.f),4.f,FFill::Solid(Hex(0x3D8BFF)));}
            else{LDUIPaint::Polygon(Elements,Ink+3,Geometry,{FVector2D(Dot.X,Mid-5.f),FVector2D(Dot.X+5.f,Mid),FVector2D(Dot.X,Mid+5.f),FVector2D(Dot.X-5.f,Mid)},FFill::Solid(Hex(0xC9D2E0)));}
        }
    }
}

int32 ULDWorkshop::NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Culling,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool bEnabled) const
{
    const int32 Base=Super::NativePaint(Args,Geometry,Culling,Elements,Layer,Style,bEnabled);
    if(!IsOpen()){return Base;}
    const int32 Top=Base+1;
    const double Now=UiNow();
    // A column that takes nothing of the hovered or held component dims its head (.col.off); the ones that take it light up
    // in the backdrop.
    if(Storage.IsValid()&&!Storage->Focus().Drops.IsEmpty())
    {
        const ULDComponentStorage::FFocus& Focus=Storage->Focus();
        for(int32 Index=0;Index<Heads.Num();++Index)
        {
            const LDHUD::EStorageColumn Id=static_cast<LDHUD::EStorageColumn>(Index);
            if(Focus.Drops.ContainsByPredicate([Id](const LDHUD::FComponentDrop& Drop){return Drop.Column==Id;})||!WorkshopHasArea(Rects.Column[Index])){continue;}
            const FBox2D& Column=Rects.Column[Index];
            LDUIPaint::Shape(Elements,Top,Geometry,LDUIShapes::EShape::Rounded,WorkshopBox(Column.Min.X,Column.Min.Y,Column.Max.X,Column.Min.Y+Rects.HeadHeight),6.f,
                LDUIPaint::FFill::Solid(LDUIPaint::Hex(0x0C1019,.7f)));
        }
    }
    // Link lines: from each installed link rule to what its 联动 list names (the learned actives it works with, in other
    // columns, and the installed components it works with), drawn as a right-angled line with a dot at each end; a line
    // between two actives whose elements react carries the reaction's name. Lines of the hovered or held component light up.
    if(Storage.IsValid()&&Latest.Content&&WorkshopHasArea(Rects.Columns))
    {
        const FLDDemoContentDefinitions& Content=*Latest.Content;
        const ULDComponentStorage::FFocus& Focus=Storage->Focus();
        const FSlateFontInfo Font=LDUIFonts::Font(LDUIFonts::ERole::Strong,LDHUDStyle::TypeTiny);
        const LDHUD::FStorageView& Model=Storage->Model();
        for(int32 ColumnIndex=0;ColumnIndex<Model.Columns.Num();++ColumnIndex)
        {
            for(const LDHUD::FEquippedView& Entry:Model.Columns[ColumnIndex].Entries)
            {
                const FString Self=Entry.Component.ToString();
                const FLDDemoComponentDefinition* Definition=Content.Components.FindByPredicate([&Self](const FLDDemoComponentDefinition& D){return D.Id==Self;});
                FSlateRect From;
                if(!Definition||Definition->Kind!=ELDDemoComponentKind::Connector||!Storage->TileBox(Entry.Component,From)){continue;}
                const FBox2D Start=LDUIShapes::AbsoluteIn(Geometry,From);
                const FName HostActive=Model.Columns[ColumnIndex].Active;
                TArray<TPair<FVector2D,FString>> Ends;
                for(int32 Other=0;Other<Heads.Num()&&Other<Model.Columns.Num();++Other)
                {
                    const FName Active=Model.Columns[Other].Active;
                    if(Other==ColumnIndex||Active.IsNone()||!Definition->TargetActives.Contains(Active.ToString())||!Heads[Other].IconFace){continue;}
                    const FBox2D Icon=LDUIShapes::RectIn(Geometry,Heads[Other].IconFace->GetCachedGeometry());
                    FString Reaction;
                    for(const ELDDemoElement A:LDWorkshop::ActiveElements(HostActive,Latest.Content))
                    {for(const ELDDemoElement B:LDWorkshop::ActiveElements(Active,Latest.Content)){if(Reaction.IsEmpty()){Reaction=LDWorkshop::ReactionName(A,B);}}}
                    Ends.Add(TPair<FVector2D,FString>(FVector2D(Icon.GetCenter().X,Icon.Max.Y+4.f),Reaction));
                }
                for(const LDHUD::FStorageColumnView& Column:Model.Columns)
                {
                    for(const LDHUD::FEquippedView& OtherEntry:Column.Entries)
                    {
                        const FString OtherId=OtherEntry.Component.ToString();
                        if(OtherId==Self||OtherEntry.Component==Entry.Host||OtherEntry.Host==Entry.Component){continue;}
                        if(!Definition->TargetComponents.Contains(OtherId)&&!Definition->ExtraBases.Contains(OtherId)){continue;}
                        FSlateRect To;if(!Storage->TileBox(OtherEntry.Component,To)){continue;}
                        Ends.Add(TPair<FVector2D,FString>(LDUIShapes::AbsoluteIn(Geometry,To).GetCenter(),FString()));
                    }
                }
                const bool bLit=Focus.Component==Entry.Component;
                const FLinearColor Ink=LDHUDStyle::ComponentLink.CopyWithNewOpacity(bLit?1.f:.55f);
                const FVector2D A(Start.Max.X+3.f,Start.GetCenter().Y);
                for(const TPair<FVector2D,FString>& End:Ends)
                {
                    const FVector2D B=End.Key;
                    const float Mid=float(A.X+B.X)*.5f;
                    LDUIShapes::Stroke(Elements,Top,Geometry,{A,FVector2D(Mid,A.Y),FVector2D(Mid,B.Y),B},Ink,bLit?3.f:2.f);
                    for(const FVector2D& Dot:{A,B}){LDUIShapes::Fill(Elements,Top+1,Geometry,FBox2D(Dot-FVector2D(4,4),Dot+FVector2D(4,4)),Ink,4.f);}
                    if(!End.Value.IsEmpty())
                    {
                        const FVector2D Label(Mid-30.f,float(FMath::Min(A.Y,B.Y)+FMath::Abs(B.Y-A.Y)*.5f)-11.f);
                        LDUIShapes::Fill(Elements,Top+1,Geometry,FBox2D(Label,Label+FVector2D(60.f,22.f)),LDHUDStyle::UIBase.CopyWithNewOpacity(.92f),11.f);
                        LDUIShapes::Frame(Elements,Top+1,Geometry,LDUIShapes::EShape::Rounded,FBox2D(Label,Label+FVector2D(60.f,22.f)),LDHUDStyle::UIReplace,1.5f,11.f);
                        FSlateDrawElement::MakeText(Elements,Top+2,Geometry.ToPaintGeometry(FVector2D(60.f,22.f),FSlateLayoutTransform(Label+FVector2D(14.f,2.f))),End.Value,Font,ESlateDrawEffect::None,LDHUDStyle::UIText);
                    }
                }
            }
        }
    }
    return Top+1;
}

void ULDWorkshop::ApplyTextScale(float Scale)
{
    if(Scale<=0.f||!WidgetTree||FMath::IsNearlyEqual(Scale,TextScale)){return;}
    const float Ratio=Scale/TextScale;TextScale=Scale;
    WidgetTree->ForEachWidget([Ratio](UWidget* Widget)
    {
        if(auto* Text=Cast<UTextBlock>(Widget)){FSlateFontInfo Font=Text->GetFont();Font.Size=FMath::Max(1.f,FMath::RoundToFloat(float(Font.Size)*Ratio));Text->SetFont(Font);}
        else if(auto* Rich=Cast<URichTextBlock>(Widget)){LDWidgets::SetRichTextSize(Rich,FMath::Max(1,FMath::RoundToInt(float(LDWidgets::RichTextSize(Rich))*Ratio)));}
    });
}
void ULDWorkshop::ScaleFresh(UTextBlock* Text) const
{
    if(!Text||FMath::IsNearlyEqual(TextScale,1.f)){return;}
    FSlateFontInfo Font=Text->GetFont();Font.Size=FMath::Max(1.f,FMath::RoundToFloat(float(Font.Size)*TextScale));Text->SetFont(Font);
}
