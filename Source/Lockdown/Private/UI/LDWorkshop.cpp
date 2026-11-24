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
#include "UI/LDRecapChain.h"
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
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/WrapBox.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"

namespace
{
// Sizes in Slate units at the 1080p reference.
constexpr float WorkshopMargin=40.f;
constexpr float WorkshopTop=116.f;
constexpr float WorkshopTitleWidth=118.f;
constexpr float WorkshopGap=20.f;
constexpr float WorkshopDockGap=16.f;
// The character column shows the ten pairs of supported elements as chips in rows of three.
constexpr int32 WorkshopReactionChips=10;
constexpr int32 WorkshopReactionColumns=3;
constexpr float WorkshopCapacityHeight=60.f;
constexpr float WorkshopColumnGap=10.f;
// A column's head (the skill art at 28, its name and pills) above the first socket at 206, as in the design.
constexpr float WorkshopHeadTop=28.f;
constexpr float WorkshopSocketTop=206.f;
// A pill's room before its number (the dot and its gap) and after it.
constexpr float WorkshopPillLead=21.f;
constexpr float WorkshopPillTail=8.f;
constexpr float WorkshopDockHeight=204.f;
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
bool WorkshopHasArea(const FBox2D& Box){return Box.bIsValid&&Box.GetSize().X>1.f&&Box.GetSize().Y>1.f;}
FBox2D WorkshopBox(float X0,float Y0,float X1,float Y1){return FBox2D(FVector2D(X0,Y0),FVector2D(X1,Y1));}
FLinearColor WorkshopElementColor(ELDDemoElement Element)
{
    switch(Element)
    {
    case ELDDemoElement::Fire: return LDUIPaint::Hex(0xFF7A3D);
    case ELDDemoElement::Water: return LDUIPaint::Hex(0x4DB5FF);
    // Spec §8.1 colours: shock violet, chill cyan, rot green.
    case ELDDemoElement::Electric: return LDUIPaint::Hex(0xB46BFF);
    case ELDDemoElement::Chill: return LDUIPaint::Hex(0x5CF2FF);
    case ELDDemoElement::Rot: return LDUIPaint::Hex(0x7AD93A);
    default: return LDHUDStyle::TextSecondary;
    }
}
FString WorkshopStripParens(const FString& Text)
{
    FString Out=Text;Out.RemoveFromStart(TEXT("（"));Out.RemoveFromEnd(TEXT("）"));Out.RemoveFromStart(TEXT("装在："));
    return Out;
}
FString WorkshopRichEscape(FString Text)
{
    Text.ReplaceInline(TEXT("&"),TEXT("&amp;"));Text.ReplaceInline(TEXT("<"),TEXT("&lt;"));Text.ReplaceInline(TEXT(">"),TEXT("&gt;"));
    return Text;
}
// A filter tag: the dark chip, or the yellow one while it filters.
void WorkshopTint(ULDCommandButton* Button,bool bSelected){LDWidgets::SetLook(Button,bSelected?LDUIPaint::EButton::TagOn:LDUIPaint::EButton::Tag);}
}

bool ULDWorkshop::IsOpening() const{return IsOpen()&&OpenedAt>=0.&&UiNow()-OpenedAt<LDHUDStyle::MotionWorkshopOpen;}
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
        Backdrop->Painter=[this](const LDUIPaint::FContext& Paint){PaintBackdrop(Paint);};
        Add(Backdrop,1,ESlateVisibility::HitTestInvisible);
        {
            // The phase bar: the step's remaining time inside its ring, the rest and step, and the step dots (painted).
            auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();
            PhaseClock=Label(TEXT(""),13,LDHUDStyle::UIText,LDUIFonts::ERole::Number);PhaseClock->SetJustification(ETextJustify::Center);
            auto* ClockBox=WidgetTree->ConstructWidget<USizeBox>();ClockBox->SetWidthOverride(54.f);ClockBox->SetHeightOverride(54.f);
            ClockBox->SetContent(PhaseClock);
            if(USizeBoxSlot* ClockSlot=Cast<USizeBoxSlot>(ClockBox->GetContentSlot())){ClockSlot->SetHorizontalAlignment(HAlign_Center);ClockSlot->SetVerticalAlignment(VAlign_Center);}
            Row->AddChildToHorizontalBox(ClockBox)->SetVerticalAlignment(VAlign_Center);
            auto* Names=WidgetTree->ConstructWidget<UVerticalBox>();
            PhaseRest=Label(TEXT(""),13,LDHUDStyle::TextSecondary);Names->AddChildToVerticalBox(PhaseRest);
            {FSlateFontInfo Font=PhaseRest->GetFont();Font.LetterSpacing=120;PhaseRest->SetFont(Font);}
            PhaseStep=Label(TEXT(""),20,LDHUDStyle::UIText,LDUIFonts::ERole::Title);Names->AddChildToVerticalBox(PhaseStep);
            Row->AddChildToHorizontalBox(Names)->SetPadding(FMargin(14,0,0,0));
            PhasePanel=Surface(Row,FMargin(14,8));Add(PhasePanel,2);
            PageTitle=Label(LDTerms::Choice,34,LDHUDStyle::UIText,LDUIFonts::ERole::Title);Add(PageTitle,2,ESlateVisibility::HitTestInvisible);
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
            auto* PointsHeight=WidgetTree->ConstructWidget<USizeBox>();PointsHeight->SetMinDesiredHeight(54.f);PointsHeight->SetContent(PointsBox);
            PointsBox->SetVerticalAlignment(VAlign_Center);
            Column->AddChildToVerticalBox(PointsHeight)->SetPadding(FMargin(0,4,0,12));
            StatList=WidgetTree->ConstructWidget<UVerticalBox>();Column->AddChildToVerticalBox(StatList);
            AttributeList=WidgetTree->ConstructWidget<UVerticalBox>();Column->AddChildToVerticalBox(AttributeList)->SetPadding(FMargin(0,26,0,0));
            UTextBlock* ReactionTitle=Label(LDTerms::WorkshopReactions,13,LDUIPaint::Hex(0x667086));
            {FSlateFontInfo Font=ReactionTitle->GetFont();Font.LetterSpacing=200;ReactionTitle->SetFont(Font);}
            ReactionBlock=WidgetTree->ConstructWidget<UVerticalBox>();Column->AddChildToVerticalBox(ReactionBlock);
            ReactionBlock->AddChildToVerticalBox(ReactionTitle)->SetPadding(FMargin(0,26,0,10));
            // .rx: all ten element pairs as chips in a three-column grid (lit first, then near, then the rest): two
            // element discs (painted under their characters), a small plus and the reaction's name. No scrolling: the
            // column scales down when it does not fit.
            auto* ReactionGrid=WidgetTree->ConstructWidget<UUniformGridPanel>();
            ReactionGrid->SetSlotPadding(FMargin(2.f));
            ReactionBlock->AddChildToVerticalBox(ReactionGrid);
            for(int32 Index=0;Index<WorkshopReactionChips;++Index)
            {
                FReaction& Reaction=Reactions.AddDefaulted_GetRef();
                auto* Line=WidgetTree->ConstructWidget<UHorizontalBox>();
                const auto Disc=[&](UTextBlock*& Out)
                {
                    Out=Label(TEXT(""),11,LDUIPaint::Hex(0x10141D),LDUIFonts::ERole::Title);Out->SetJustification(ETextJustify::Center);
                    auto* Box=WidgetTree->ConstructWidget<USizeBox>();Box->SetWidthOverride(18.f);Box->SetHeightOverride(18.f);Box->SetContent(Out);
                    if(USizeBoxSlot* DiscSlot=Cast<USizeBoxSlot>(Out->Slot)){DiscSlot->SetHorizontalAlignment(HAlign_Center);DiscSlot->SetVerticalAlignment(VAlign_Center);}
                    Line->AddChildToHorizontalBox(Box)->SetVerticalAlignment(VAlign_Center);Built.Add(Box);
                };
                Disc(Reaction.FirstText);
                UTextBlock* Plus=Label(TEXT("+"),10,LDUIPaint::Hex(0x667086));Reaction.Pair=Plus;
                Line->AddChildToHorizontalBox(Plus)->SetPadding(FMargin(2,0));
                if(UHorizontalBoxSlot* PlusSlot=Cast<UHorizontalBoxSlot>(Plus->Slot)){PlusSlot->SetVerticalAlignment(VAlign_Center);}
                Disc(Reaction.SecondText);
                Line->AddChildToHorizontalBox(WidgetTree->ConstructWidget<USpacer>())->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
                Reaction.Name=Label(TEXT(""),12,LDUIPaint::Hex(0x667086),LDUIFonts::ERole::Strong);
                Line->AddChildToHorizontalBox(Reaction.Name)->SetVerticalAlignment(VAlign_Center);
                Reaction.Row=WidgetTree->ConstructWidget<UBorder>();Reaction.Row->SetPadding(FMargin(5,0));Reaction.Row->SetContent(Line);
                Reaction.Row->SetBrush(FSlateNoResource());Reaction.Row->SetVerticalAlignment(VAlign_Center);
                auto* RowHeight=WidgetTree->ConstructWidget<USizeBox>();RowHeight->SetHeightOverride(28.f);RowHeight->SetContent(Reaction.Row);
                ReactionGrid->AddChildToUniformGrid(RowHeight,Index/WorkshopReactionColumns,Index%WorkshopReactionColumns);
                Built.Add(Plus);Built.Add(Line);Built.Add(RowHeight);
            }
            // The column keeps the panel's inner width and scales down (never up) when larger text would push it past the
            // panel's height.
            auto* ColumnWidth=WidgetTree->ConstructWidget<USizeBox>();ColumnWidth->SetWidthOverride(380.f);ColumnWidth->SetContent(Column);
            CharacterFit=LDWidgets::Fit(WidgetTree,ColumnWidth);
            // The trainer's respec foot sits under the scaled column at full size: its button and reason lines are laid
            // over it by the choices panel and do not scale with the column.
            auto* Stack=WidgetTree->ConstructWidget<UVerticalBox>();
            Stack->AddChildToVerticalBox(CharacterFit)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

            CharacterPanel=Surface(Stack,FMargin(24,22));Add(CharacterPanel,2);
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
                Head.Name->SetAutoWrapText(true);
                auto* NameSlot=Box->AddChildToVerticalBox(Head.Name);NameSlot->SetHorizontalAlignment(HAlign_Fill);NameSlot->SetPadding(FMargin(0,8,0,0));
                auto* Chips=WidgetTree->ConstructWidget<UHorizontalBox>();
                // .pill: the numbers sit inside painted pills, each led by its momentum dot or cooldown diamond.
                Head.Momentum=Label(TEXT(""),14,LDHUDStyle::UIText,LDUIFonts::ERole::Number);
                Head.Cooldown=Label(TEXT(""),14,LDHUDStyle::UIText,LDUIFonts::ERole::Number);
                Chips->AddChildToHorizontalBox(Head.Momentum)->SetPadding(FMargin(WorkshopPillLead,3,WorkshopPillTail,3));
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
            auto* Space=WidgetTree->ConstructWidget<USpacer>();
            Title->AddChildToHorizontalBox(Space)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            auto* Pager=WidgetTree->ConstructWidget<UHorizontalBox>();
            LibraryPrevious=LDWidgets::Button(WidgetTree,TEXT("‹"),[this](){if(LibraryPage>0){--LibraryPage;}},true,TEXT("WorkshopLibraryPrevious"));
            LibraryNext=LDWidgets::Button(WidgetTree,TEXT("›"),[this](){if(LibraryPage+1<LibraryFit.Pages.Num()){++LibraryPage;}},true,TEXT("WorkshopLibraryNext"));
            LibraryPrevious->SetToolTipText(FText::FromString(LDTerms::StoragePreviousPage));
            LibraryNext->SetToolTipText(FText::FromString(LDTerms::StorageNextPage));
            for(ULDCommandButton* Button:{LibraryPrevious.Get(),LibraryNext.Get()})
            {
                auto Style=Button->GetStyle();Style.NormalPadding=FMargin(9,2);Style.PressedPadding=FMargin(9,2);Button->SetStyle(Style);
                WorkshopTint(Button,false);
            }
            Pager->AddChildToHorizontalBox(LibraryPrevious)->SetVerticalAlignment(VAlign_Center);
            LibraryPageText=Label(TEXT(""),13,LDHUDStyle::TextSecondary,LDUIFonts::ERole::Number);
            auto* PageSlot=Pager->AddChildToHorizontalBox(LibraryPageText);
            PageSlot->SetPadding(FMargin(6,0));PageSlot->SetVerticalAlignment(VAlign_Center);
            Pager->AddChildToHorizontalBox(LibraryNext)->SetVerticalAlignment(VAlign_Center);
            LibraryPager=Pager;WorkshopShown(LibraryPager,false);
            Title->AddChildToHorizontalBox(Pager)->SetVerticalAlignment(VAlign_Center);
            LibraryTitle=Title;Add(Title,3);
            FilterRow=WidgetTree->ConstructWidget<UWrapBox>();FilterRow->SetInnerSlotPadding(FVector2D(6,6));
            for(int32 Tag=0;Tag<int32(ELDCardType::Count);++Tag)
            {
                ULDCommandButton* Chip=LDWidgets::Button(WidgetTree,LDTerms::CardTypeName(uint8(Tag)),[this,Tag](){TagFilter=TagFilter==Tag?INDEX_NONE:Tag;LibraryPage=0;
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
            DockName=Label(TEXT(""),32,LDHUDStyle::UIText,LDUIFonts::ERole::Title);
            Centre->AddChildToVerticalBox(DockName);
            DockSummary=Label(TEXT(""),14,LDHUDStyle::TextSecondary);DockSummary->SetAutoWrapText(true);
            Centre->AddChildToVerticalBox(DockSummary)->SetPadding(FMargin(0,2,0,0));
            DockBody=LDWidgets::RichText(WidgetTree,17,TEXT("WorkshopDockBody"));DockBody->SetAutoWrapText(false);
            Centre->AddChildToVerticalBox(DockBody)->SetPadding(FMargin(0,8,0,0));
            DockChains=WidgetTree->ConstructWidget<UVerticalBox>();DockChains->SetVisibility(ESlateVisibility::Collapsed);
            DockChainsShown.Reset();DockChainsScale=0.f;
            Centre->AddChildToVerticalBox(DockChains)->SetPadding(FMargin(0,8,0,0));
            DockChips=Label(TEXT(""),LDHUDStyle::TypeSmall,LDHUDStyle::CardConvert,LDUIFonts::ERole::Strong);DockChips->SetAutoWrapText(true);
            Centre->AddChildToVerticalBox(DockChips)->SetPadding(FMargin(0,10,0,0));
            DockCentre=LDWidgets::Fit(WidgetTree,Centre);Add(DockCentre,3);
            auto* Right=WidgetTree->ConstructWidget<UVerticalBox>();
            const auto Section=[&](TObjectPtr<UTextBlock>& Title,TObjectPtr<UTextBlock>& Body,const TCHAR* Caption,bool bFirst)
            {
                Title=Label(Caption,LDHUDStyle::TypeTiny,LDHUDStyle::TextSecondary);Right->AddChildToVerticalBox(Title)->SetPadding(FMargin(0,bFirst?0.f:12.f,0,0));
                Body=Label(TEXT(""),LDHUDStyle::TypeSmall,LDHUDStyle::UIText,LDUIFonts::ERole::Strong);Body->SetAutoWrapText(true);
                Right->AddChildToVerticalBox(Body)->SetPadding(FMargin(0,4,0,0));
            };
            Section(DockWhereTitle,DockWhere,LDTerms::WorkshopInstalledOn,true);
            DockPreviewTitle=Label(LDTerms::WorkshopPreview,LDHUDStyle::TypeTiny,LDHUDStyle::TextSecondary);
            DockPreviewTitle->SetToolTipText(FText::FromString(LDTerms::WorkshopPreviewSnapshot));
            Right->AddChildToVerticalBox(DockPreviewTitle)->SetPadding(FMargin(0,8,0,0));
            DockPreviewBody=LDWidgets::RichText(WidgetTree,LDHUDStyle::TypeSmall,TEXT("WorkshopPreviewBody"));
            Right->AddChildToVerticalBox(DockPreviewBody)->SetPadding(FMargin(0,4,0,0));
            DockPreviewPager=WidgetTree->ConstructWidget<UHorizontalBox>();
            DockPreviewPrevious=LDWidgets::Button(WidgetTree,TEXT("‹"),[this](){if(PreviewPage>0){--PreviewPage;RefreshPreviewPage();}},true,TEXT("WorkshopPreviewPrevious"));
            DockPreviewNext=LDWidgets::Button(WidgetTree,TEXT("›"),[this](){++PreviewPage;RefreshPreviewPage();},true,TEXT("WorkshopPreviewNext"));
            for(ULDCommandButton* Button:{DockPreviewPrevious.Get(),DockPreviewNext.Get()})
            {
                auto Style=Button->GetStyle();Style.NormalPadding=FMargin(5,0);Style.PressedPadding=FMargin(5,0);Button->SetStyle(Style);
            }
            DockPreviewPager->AddChildToHorizontalBox(DockPreviewPrevious)->SetVerticalAlignment(VAlign_Center);
            DockPreviewPageText=Label(TEXT(""),LDHUDStyle::TypeTiny,LDHUDStyle::TextSecondary,LDUIFonts::ERole::Number);
            auto* PreviewPageSlot=DockPreviewPager->AddChildToHorizontalBox(DockPreviewPageText);
            PreviewPageSlot->SetPadding(FMargin(4,0));PreviewPageSlot->SetVerticalAlignment(VAlign_Center);
            DockPreviewPager->AddChildToHorizontalBox(DockPreviewNext)->SetVerticalAlignment(VAlign_Center);
            Right->AddChildToVerticalBox(DockPreviewPager)->SetPadding(FMargin(0,2,0,0));
            WorkshopShown(DockPreviewTitle,false);WorkshopShown(DockPreviewBody,false);WorkshopShown(DockPreviewPager,false);
            Section(DockLinksTitle,DockLinks,LDTerms::WorkshopLinks,false);
            Section(DockNotesTitle,DockNotes,LDTerms::WorkshopNotes,false);
            DockRight=LDWidgets::Fit(WidgetTree,Right);Add(DockRight,3);
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

ULDWorkshop::FRects ULDWorkshop::ComputeLayout(FVector2D Size,const FInput& Input,float Scale,float TextScale)
{
    FRects Next;
    if(Input.Mode==EMode::Closed){return Next;}
    const float W=float(Size.X),H=float(Size.Y);
    const float Margin=FMath::Min(WorkshopMargin,W*.025f);
    const float Top=FMath::Min(WorkshopTop,H*.14f);
    const float Bottom=H-Margin;
    const float DockHeight=FMath::Min(WorkshopDockHeight,(Bottom-Top)*.23f);
    const float ContentBottom=Bottom-DockHeight-WorkshopDockGap;
    const float Span=W-2.f*(Margin+WorkshopGap);
    const float CharacterRight=Margin+Span*.24f;
    const float DrawerRight=W-Margin;
    const float DrawerLeft=DrawerRight-Span*.28f;
    const float DrawerHeight=ContentBottom-Top;

    Next.Region=WorkshopBox(0,0,W,H);
    Next.Title=WorkshopBox(Margin,30,Margin+WorkshopTitleWidth,84);
    Next.Phase=WorkshopBox(Next.Title.Max.X,20,FMath::Min(DrawerLeft-WorkshopGap,Margin+WorkshopTitleWidth+464.f),94);
    Next.Character=WorkshopBox(Margin,Top,CharacterRight,ContentBottom);
    Next.Board=WorkshopBox(CharacterRight+WorkshopGap,Top,DrawerLeft-WorkshopGap,ContentBottom);
    Next.Dock=WorkshopBox(Margin,ContentBottom+WorkshopDockGap,DrawerRight,Bottom);

    // The drawer budgets are fractions of its height, floored by fixed minimums from the scale and text size: the pending
    // row with its services and one library card first, then the shop takes what is left. Child desired sizes never feed
    // back into these bounds.
    constexpr float LibraryHeader=118.f;
    const float MinIcon=WorkshopMinIconPixels/FMath::Max(.01f,Scale);
    const float Text=FMath::Max(.5f,TextScale);
    float LibraryTop=Top;
    float LibraryBottom=ContentBottom;
    if(Input.bStage)
    {
        LibraryTop=Top+DrawerHeight*.43f;
        Next.Stage=WorkshopBox(DrawerLeft,Top,DrawerRight,LibraryTop-WorkshopGap);
    }
    else
    {
        const bool bPendingArea=Input.Pending>0||Input.bServices;
        const float PendingFloor=Input.Pending>0 ? 56.f+(Input.bServices?40.f:0.f)+LDWorkshop::PendingRowHeight(MinIcon,Text) : 0.f;
        float PendingHeight=!bPendingArea?0.f:Input.Pending>0?FMath::Max(PendingFloor,DrawerHeight*(Input.bShop?.29f:.45f))
            :FMath::Min(104.f,DrawerHeight*.16f);
        const float LibraryFloor=LibraryHeader+14.f+LDWorkshop::LibraryMinPageHeight(MinIcon,Text);
        float ShopHeight=Input.bShop?DrawerHeight*(Input.Pending>0?.34f:.52f):0.f;
        // The shop gives way first, then the pending tray down to its floor; the library keeps at least one card.
        const float Deficit=LibraryFloor-(DrawerHeight-ShopHeight-PendingHeight);
        if(Deficit>0.f)
        {
            const float FromShop=FMath::Min(Deficit,FMath::Max(0.f,ShopHeight-WorkshopGap));
            ShopHeight-=FromShop;
            PendingHeight-=FMath::Min(Deficit-FromShop,FMath::Max(0.f,PendingHeight-PendingFloor));
        }
        if(Input.bShop&&ShopHeight>WorkshopGap)
        {
            LibraryTop=Top+ShopHeight;
            Next.Shop=WorkshopBox(DrawerLeft,Top,DrawerRight,LibraryTop-WorkshopGap);
        }
        if(bPendingArea)
        {
            LibraryBottom-=PendingHeight;
            Next.Pending=WorkshopBox(DrawerLeft,LibraryBottom,DrawerRight,ContentBottom);
        }
    }
    Next.Library=WorkshopBox(DrawerLeft,LibraryTop,DrawerRight,LibraryBottom);
    Next.LibraryBody=WorkshopBox(DrawerLeft+24.f,LibraryTop+LibraryHeader,DrawerRight-20.f,LibraryBottom-14.f);
    Next.Capacity=WorkshopBox(Next.Board.Min.X,Top,Next.Board.Max.X,Top+WorkshopCapacityHeight);
    Next.Columns=WorkshopBox(Next.Board.Min.X+16.f,Next.Capacity.Max.Y,Next.Board.Max.X-16.f,ContentBottom-12.f);
    Next.HeadTop=WorkshopHeadTop;
    Next.HeadHeight=WorkshopSocketTop;
    const float ColumnWidth=(float(Next.Columns.GetSize().X)-WorkshopColumnGap*(LDHUD::StorageColumnCount-1))/LDHUD::StorageColumnCount;
    for(int32 Index=0;Index<LDHUD::StorageColumnCount;++Index)
    {
        const float X=float(Next.Columns.Min.X)+LDHUD::BoardPosition(static_cast<LDHUD::EStorageColumn>(Index))*(ColumnWidth+WorkshopColumnGap);
        Next.Column[Index]=WorkshopBox(X,Next.Columns.Min.Y,X+ColumnWidth,Next.Columns.Max.Y);
    }
    return Next;
}

void ULDWorkshop::ApplyLayout(const FRects& Layout,FVector2D Size,float Scale)
{
    Rects=Layout;
    const float W=float(Size.X),H=float(Size.Y);
    const LDHUD::FStorageView LibraryModel=LDHUD::CardStorage(View,Latest.Content);
    const FIntVector4 Counts=LDWorkshop::CollectLibrary(LibraryModel,Latest.Content,TagFilter).Counts();
    LibraryFit=WorkshopHasArea(Rects.LibraryBody)?LDWorkshop::FitLibrary(Counts,float(Rects.LibraryBody.GetSize().X),
        float(Rects.LibraryBody.GetSize().Y),WorkshopMinIconPixels/Scale,TextScale):LDWorkshop::FLibraryFit();
    LibraryPage=LibraryFit.Pages.IsEmpty()?0:FMath::Clamp(LibraryPage,0,LibraryFit.Pages.Num()-1);
    TrayRight=WorkshopHasArea(Rects.Pending)?W-float(Rects.Pending.Max.X)+12.f:0.f;
    TrayBottom=WorkshopHasArea(Rects.Pending)?H-float(Rects.Pending.Max.Y)+12.f:0.f;
    TrayWrap=WorkshopHasArea(Rects.Pending)?FMath::Max(0.f,float(Rects.Pending.GetSize().X)-24.f):0.f;
    TrayLimit=WorkshopHasArea(Rects.Pending)?FMath::Max(0.f,float(Rects.Pending.GetSize().Y)-56.f-(Latest.bServices?40.f:0.f)):0.f;
    const float Right=float(Rects.Dock.Max.X);
    ActionsAt=FVector2D(Right-94.f,34.f);
    Place(Blur,Rects.Region);
    Place(Backdrop,Rects.Region);
    Place(PhasePanel,(Latest.bRest||Latest.bDead)?Rects.Phase:FBox2D(ForceInit));
    Place(PageTitle,Rects.Title);
    Place(CloseButton,WorkshopBox(Right-74.f,34.f,Right,86.f));
    Place(CharacterPanel,WorkshopHasArea(Rects.Character)?Rects.Character:FBox2D(ForceInit));
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
        Place(CapacityTitle,WorkshopBox(Left,Mid-12.f,Left+FMath::Max(72.f,float(CapacityTitle->GetDesiredSize().X)+4.f),Mid+13.f));
        CapacityCellsAt=Left+FMath::Max(72.f,float(CapacityTitle->GetDesiredSize().X))+16.f;
        const LDWorkshop::FCapacityView Capacity=Storage.IsValid()?LDWorkshop::Capacity(View,Storage->Model(),Latest.Content):LDWorkshop::FCapacityView();
        const int32 Cells=Capacity.Capacity+(Capacity.UnlockLevel>0?1:0);
        const float Room=FMath::Max(0.f,float(Rects.Capacity.Max.X)-224.f-10.f-90.f-10.f-CapacityCellsAt);
        CapacityCellStep=Cells>0?FMath::Min(45.f,Room/Cells):45.f;
        const float CountX=CapacityCellsAt+float(Cells)*CapacityCellStep+10.f;
        Place(CapacityCount,WorkshopBox(CountX,Mid-18.f,CountX+90.f,Mid+18.f));
        Place(CapacityUnlock,WorkshopBox(float(Rects.Capacity.Max.X)-224.f,Mid-9.f,float(Rects.Capacity.Max.X)-24.f,Mid+10.f));
        Place(LockBadge,WorkshopBox(float(Rects.Capacity.Max.X)-224.f,Mid-28.f,float(Rects.Capacity.Max.X)-24.f,Mid-10.f));
    }
    for(int32 Index=0;Index<Heads.Num();++Index)
    {
        const FBox2D& Column=Rects.Column[Index];
        Place(ColumnBacks[Index],Column);
        Place(Heads[Index].Box,WorkshopBox(Column.Min.X,Column.Min.Y+Rects.HeadTop,Column.Max.X,Column.Min.Y+Rects.HeadHeight));
        const float HeadIcon=FMath::Min(Rects.HeadIcon,FMath::Max(1.f,float(Column.GetSize().X)-20.f));
        if(!FMath::IsNearlyEqual(Heads[Index].IconBox->GetWidthOverride(),HeadIcon)){Heads[Index].IconBox->SetWidthOverride(HeadIcon);Heads[Index].IconBox->SetHeightOverride(HeadIcon);}
        // The smaller field-mode art keeps its key chip and the empty slot's "+" in proportion, so neither covers the art.
        const float HeadScale=FMath::Clamp(HeadIcon/100.f,.6f,1.f);
        if(UWidget* Chip=Heads[Index].Key?Heads[Index].Key->GetParent():nullptr)
        {
            Chip->SetRenderTransformPivot(FVector2D::ZeroVector);Chip->SetRenderScale(FVector2D(FMath::Max(HeadScale,.78f)));
            Chip->SetRenderTranslation(FVector2D(-9.f,-9.f)*HeadScale);
        }
        if(Heads[Index].Glyph){Heads[Index].Glyph->SetRenderScale(FVector2D(HeadScale));}
    }
    Place(LibraryTitle,WorkshopBox(Rects.Library.Min.X+24.f,Rects.Library.Min.Y+18.f,Rects.Library.Max.X-20.f,Rects.Library.Min.Y+56.f));
    const bool bOwnDock=!Latest.bFinalStage;
    float CentreWrap=0.f,RightWrap=0.f,DockArtSize=80.f;
    DockDividerX=0.f;
    if(!bOwnDock){for(UWidget* Hidden:{DockLeft.Get(),DockCentre.Get(),DockRight.Get()}){Place(Hidden,FBox2D(ForceInit));}}
    else
    {
        // Give the rule more room when the board narrows or the player selects larger text.
        const float Compact=FMath::Clamp(FMath::Max((1000.f-float(Rects.Dock.GetSize().X))/400.f,(TextScale-1.f)/.25f),0.f,1.f);
        const float LeftWidth=FMath::Lerp(150.f,98.f,Compact);
        const float RightWidth=FMath::Lerp(WorkshopDockRight,150.f,Compact);
        const float LeftGap=FMath::Lerp(26.f,12.f,Compact),RightGap=FMath::Lerp(50.f,20.f,Compact);
        const float CentreStart=float(Rects.Dock.Min.X)+28.f+LeftWidth+LeftGap;
        const float RightStart=float(Rects.Dock.Max.X)-28.f-RightWidth;
        const float CentreEnd=RightStart-RightGap;
        Place(DockLeft,WorkshopBox(Rects.Dock.Min.X+28.f,Rects.Dock.Min.Y+24.f,Rects.Dock.Min.X+28.f+LeftWidth,Rects.Dock.Max.Y-16.f));
        Place(DockCentre,WorkshopBox(CentreStart,Rects.Dock.Min.Y+22.f,CentreEnd,Rects.Dock.Max.Y-16.f));
        Place(DockRight,WorkshopBox(RightStart,Rects.Dock.Min.Y+24.f,Rects.Dock.Max.X-28.f,Rects.Dock.Max.Y-16.f));
        CentreWrap=CentreEnd-CentreStart;RightWrap=RightWidth;
        DockDividerX=RightStart-RightGap*.5f;
        DockArtSize=FMath::Lerp(112.f,80.f,Compact);
    }
    Place(FilterRow,WorkshopBox(Rects.Library.Min.X+24.f,Rects.Library.Min.Y+62.f,Rects.Library.Max.X-20.f,float(Rects.LibraryBody.Min.Y)-6.f));
    Place(PendingTitle,WorkshopHasArea(Rects.Pending)?WorkshopBox(Rects.Pending.Min.X+46.f,Rects.Pending.Min.Y+16.f,Rects.Pending.Min.X+200.f,Rects.Pending.Min.Y+38.f):FBox2D(ForceInit));
    {
        // The dock columns scale to fit, so every wrapping line wraps at its column's width rather than at what it is given.
        if(CentreWrap>0.f)
        {
            if(DockBody){DockBody->SetWrapTextAt(CentreWrap);}
            for(UTextBlock* Line:{DockName.Get(),DockSummary.Get(),DockChips.Get()}){if(Line){Line->SetAutoWrapText(false);Line->SetWrapTextAt(CentreWrap);}}
        }
        if(RightWrap>0.f)
        {
            for(UTextBlock* Line:{DockWhere.Get(),DockLinks.Get(),DockNotes.Get()}){if(Line){Line->SetAutoWrapText(false);Line->SetWrapTextAt(RightWrap);}}
            if(DockPreviewBody){DockPreviewBody->SetWrapTextAt(RightWrap);}
        }
    }
    if(DockIconBox&&!FMath::IsNearlyEqual(DockIconBox->GetWidthOverride(),DockArtSize)){DockIconBox->SetWidthOverride(DockArtSize);DockIconBox->SetHeightOverride(DockArtSize);}
}

const FGeometry& EffectiveWorkshopGeometry(const UWidget& Widget)
{
    for(const UWidget* Cur=&Widget;Cur;Cur=Cur->GetParent())
    {
        const FGeometry& TickG=Cur->GetCachedGeometry();
        if(TickG.GetLocalSize().X>1.f&&TickG.GetLocalSize().Y>1.f){return TickG;}
        if(TSharedPtr<SWidget> Slate=Cur->GetCachedWidget())
        {
            const FGeometry& PaintG=Slate->GetPaintSpaceGeometry();
            if(PaintG.GetLocalSize().X>1.f&&PaintG.GetLocalSize().Y>1.f){return PaintG;}
        }
    }
    return Widget.GetCachedGeometry();
}

ULDChoicesPanel::FWorkshopPlaces ULDWorkshop::Places() const
{
    ULDChoicesPanel::FWorkshopPlaces Out;
    if(!IsOpen()||!Canvas){return Out;}
    const FGeometry& Geometry=LayoutGeometry;
    if(Geometry.GetLocalSize().X<=0.f){return Out;}
    const auto Absolute=[&Geometry](const FBox2D& Box)
    {
        if(!WorkshopHasArea(Box)){return FSlateRect();}
        const FVector2D A=TransformPoint(Geometry.GetAccumulatedLayoutTransform(),Box.Min),B=TransformPoint(Geometry.GetAccumulatedLayoutTransform(),Box.Max);
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
    if(Latest.bCharacterServices)
    {
        Out.CharacterFoot=Absolute(WorkshopBox(Rects.Character.Min.X+24.f,Rects.Character.Max.Y-82.f,
            Rects.Character.Max.X-24.f,Rects.Character.Max.Y-12.f));
    }
    if(WorkshopHasArea(Rects.Character)&&PointsBox&&PointsBox->GetParent())
    {Out.Points=PointsBox->GetParent()->GetCachedGeometry().GetLayoutBoundingRect();}
    for(int32 Index=0;Index<Heads.Num()&&Index<LDHUD::StorageColumnCount;++Index)
    {
        if(!WorkshopHasArea(Rects.Column[Index])||!Heads[Index].IconFace){continue;}
        const FGeometry& Icon=Heads[Index].IconFace->GetCachedGeometry();
        const FVector2D A=Icon.GetAbsolutePosition(),B=A+Icon.GetAbsoluteSize();
        Out.HeadIcons[Index]=FSlateRect(A.X,A.Y,B.X,B.Y);
    }
    if(WorkshopHasArea(Rects.Pending))
    {Out.PendingTop=Absolute(WorkshopBox(Rects.Pending.Min.X+24.f,Rects.Pending.Min.Y+44.f,Rects.Pending.Max.X-20.f,Rects.Pending.Min.Y+96.f));}
    Out.Shop=Absolute(Rects.Shop);
    Out.DrawerHeader=Absolute(WorkshopBox(Rects.Library.Min.X,Rects.Character.Min.Y-54.f,Rects.Library.Max.X,Rects.Character.Min.Y-6.f));
    return Out;
}

ULDComponentStorage::FWorkshopLayout ULDWorkshop::StorageLayout() const
{
    ULDComponentStorage::FWorkshopLayout Out;
    if(!IsOpen()||!Canvas){return Out;}
    const FGeometry& Geometry=LayoutGeometry;
    if(Geometry.GetLocalSize().X<=0.f){return Out;}
    const auto Absolute=[&Geometry](const FBox2D& Box)
    {
        if(!WorkshopHasArea(Box)){return FSlateRect();}
        const FVector2D A=TransformPoint(Geometry.GetAccumulatedLayoutTransform(),Box.Min),B=TransformPoint(Geometry.GetAccumulatedLayoutTransform(),Box.Max);
        return FSlateRect(A.X,A.Y,B.X,B.Y);
    };
    Out.bActive=true;
    for(int32 Index=0;Index<LDHUD::StorageColumnCount;++Index){Out.Columns[Index]=Absolute(Rects.Column[Index]);}
    const float Scale=Geometry.Scale>0.f?Geometry.Scale:1.f;
    Out.HeadHeight=Rects.HeadHeight*Scale;
    Out.Library=Absolute(Rects.LibraryBody);
    Out.TileScale=1.f;
    Out.bNames=true;
    // Icons stay at least WorkshopMinIconPixels on screen; the board then drops the names to keep the room.
    const float MinUnits=WorkshopMinIconPixels/Scale;
    Out.MinIcon=MinUnits;
    if(46.f*Out.TileScale*46.f/56.f<MinUnits){Out.bNames=false;}
    if(46.f*Out.TileScale<MinUnits){Out.TileScale=MinUnits/46.f;Out.bNames=false;}
    if(WorkshopHasArea(Rects.Columns))
    {
        const float ColumnWidth=float(Rects.Column[0].GetSize().X);
        Out.TileIndent=FMath::Clamp((ColumnWidth-16.f-FMath::Max(56.f*Out.TileScale,MinUnits))*.5f,0.f,24.f);
        if(ColumnWidth<56.f*Out.TileScale+100.f){Out.bNames=false;}
    }
    if(LibraryFit.bFits)
    {
        Out.LibraryIcon=LibraryFit.Icon;Out.LibraryLabelHeight=LibraryFit.LabelHeight;Out.bCompactLibrary=LibraryFit.bCompact;
        if(LibraryFit.Pages.IsValidIndex(LibraryPage))
        {Out.LibraryFirst=LibraryFit.Pages[LibraryPage].First;Out.LibraryCount=LibraryFit.Pages[LibraryPage].Counts;}
    }
    Out.TagFilter=TagFilter;
    return Out;
}

bool ULDWorkshop::IsHit(FVector2D Absolute) const
{
    return IsOpen()&&LayoutGeometry.IsUnderLocation(Absolute);
}

void ULDWorkshop::Present(const FInput& Input,const FGeometry* HudGeometry)
{
    const EMode Previous=Mode;
    Mode=Input.Mode;
    if(Latest.bDead&&!Input.bDead&&DockKey.StartsWith(TEXT("D|")))
    {
        ShowTextDock(FString(),FString(),FString(),FString(),FString(),FString());
    }
    if(Previous==EMode::Open&&Mode!=EMode::Open)
    {
        for(FStatRow& Row:StatRows){Row.PendingFlashRevision=0;Row.AddedAt=-100.;Row.FlashCell=INDEX_NONE;}
        InterruptFinalFeedback();
    }
    if(Mode==EMode::Closed)
    {
        // The panels sink and fade out (LDHUDStyle::MotionWorkshopClose) without taking any more input.
        if(Previous!=EMode::Closed){LDWidgets::PlayCue(this,ELDAudioCue::UIWorkshopClose);ClosingAt=UiNow();SetVisibility(ESlateVisibility::HitTestInvisible);}
        else if(ClosingAt<0.){WorkshopShown(this,false);}
        OpenedAt=-1.;
        return;
    }
    const FLDProgressionView* NextView=Input.Progression;
    const bool bLinksChanged=Input.Content!=Latest.Content||Input.Match!=Latest.Match||!NextView||!NextView->bValid||!View.bValid
        ||NextView->Generation!=View.Generation||NextView->Life!=View.Life||NextView->BuildRevision!=View.BuildRevision;
    if(NextView&&NextView->bValid&&(NextView->Generation!=View.Generation||NextView->Life!=View.Life))
    {
        for(FStatRow& Row:StatRows){Row.PendingFlashRevision=0;Row.AddedAt=-100.;Row.FlashCell=INDEX_NONE;}
        InterruptFinalFeedback();
    }
    Latest=Input;
    View=NextView?*NextView:FLDProgressionView();
    if(bLinksChanged)
    {
        // Every installed card's vocabulary links (LDHUD::CardConnections), drawn while it is hovered or held.
        LinkConnections.Reset();
        if(View.bValid&&Input.Content)
        {
            for(const FLDCardInstanceView& Owned:View.Cards)
            {
                const FName Key=LDHUD::CardKey(Owned.DefinitionId,Owned.Serial);
                LinkConnections.Add(Key,LDHUD::CardConnections(Key,View,Input.Content));
            }
        }
    }
    if(Previous==EMode::Closed||OpenedAt<0.){OpenedAt=UiNow();DockKey.Reset();LDWidgets::PlayCue(this,ELDAudioCue::UIWorkshopOpen);}
    ClosingAt=-1.;
    WorkshopShown(this,true);
    LayoutGeometry=HudGeometry?*HudGeometry:EffectiveWorkshopGeometry(*this);
    const FVector2D CachedSize=LayoutGeometry.GetLocalSize();
    const FVector2D Size=CachedSize.X>1.f&&CachedSize.Y>1.f?CachedSize:FVector2D(1920.f,1080.f);
    ApplyLayout(ComputeLayout(Size,Input,FMath::Max(.01f,LayoutGeometry.Scale),TextScale),Size,FMath::Max(.01f,LayoutGeometry.Scale));
    if(WorkshopHasArea(Rects.Library))
    {
        const FIntVector4 Counts=LDWorkshop::CollectLibrary(LDHUD::CardStorage(View,Latest.Content),Latest.Content,TagFilter).Counts();
        const FString Warning=LibraryFit.bFits||Counts.X+Counts.Y+Counts.Z+Counts.W==0?FString()
            :FString::Printf(TEXT("%d/%d/%d/%d %.0fx%.0f"),Counts.X,Counts.Y,Counts.Z,Counts.W,Rects.LibraryBody.GetSize().X,Rects.LibraryBody.GetSize().Y);
        if(!Warning.IsEmpty()&&Warning!=LibraryWarning)
        {UE_LOG(LogLDUI,Warning,TEXT("LD_WORKSHOP_LIBRARY_OVERFLOW counts_and_bounds=%s"),*Warning);}
        if(!Warning.IsEmpty()&&LibraryWarning.IsEmpty()&&Storage.IsValid()&&Storage->Notify)
        {Storage->Notify(LDTerms::StorageNoRoom);}
        LibraryWarning=Warning;
    }
    WorkshopShown(LibraryPager,LibraryFit.Pages.Num()>1);
    WorkshopText(LibraryPageText,FString::Printf(TEXT("%d / %d"),LibraryPage+1,LibraryFit.Pages.Num()));
    if(LibraryPrevious){LibraryPrevious->SetIsEnabled(LibraryPage>0);}
    if(LibraryNext){LibraryNext->SetIsEnabled(LibraryPage+1<LibraryFit.Pages.Num());}
    // The darkening fades in with the opening; at low quality it only darkens.
    const float Open=LDTween::Progress(UiNow()-OpenedAt,LDHUDStyle::MotionWorkshopOpen,LDTween::ECurve::EaseOut);
    if(Blur)
    {
        const float Strength=!Input.bLowQuality?8.f*Open:0.f;
        if(!FMath::IsNearlyEqual(Blur->GetBlurStrength(),Strength,.01f)){Blur->SetBlurStrength(Strength);}
    }
    // The final mod page turns the dark blue to a deep magenta: this step is not like the others.
    const FLinearColor Tint=Input.bFinalStage?FLinearColor(FColor(0x2A,0x06,0x1C)):LDHUDStyle::UIBase;
    if(Shade){Shade->SetBrushColor(Tint.CopyWithNewOpacity((Input.bFinalStage?.82f:.62f)*Open));}
    RefreshPhase();
    RefreshCharacter();
    RefreshHeads();
    const LDHUD::FStorageView* Model=Storage.IsValid()?&Storage->Model():nullptr;
    const LDHUD::FStorageView CountModel=LDHUD::CardStorage(View,Input.Content);
    const FIntVector4 Matched=LDWorkshop::CollectLibrary(CountModel,Input.Content,TagFilter).Counts();
    const int32 MatchCount=Matched.X+Matched.Y+Matched.Z+Matched.W;
    const FIntVector4 Owned=TagFilter==INDEX_NONE?Matched:LDWorkshop::CollectLibrary(CountModel,Input.Content,INDEX_NONE).Counts();
    WorkshopText(LibraryCount,View.bValid?(TagFilter==INDEX_NONE?FString::FromInt(MatchCount)
        :FString::Printf(TEXT("%d / %d"),MatchCount,Owned.X+Owned.Y+Owned.Z+Owned.W)):FString());
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
    if(FSlateApplication::IsInitialized())
    {
        RefreshDock(FSlateApplication::Get().GetCursorPos());
    }
}

void ULDWorkshop::RefreshPhase()
{
    if(Mode!=EMode::Open){return;}
    if(Latest.bDead)
    {
        WorkshopText(PhaseRest,Latest.DeathClock.IsEmpty()?Latest.DeathCountdown:FString(LDTerms::RespawnWait));
        PhaseRest->SetColorAndOpacity(LDHUDStyle::TextSecondary);
        WorkshopText(PhaseStep,Latest.DeathTitle);PhaseStep->SetColorAndOpacity(LDHUDStyle::UIReject);
        WorkshopText(PhaseClock,Latest.DeathClock);
        return;
    }
    PhaseStep->SetColorAndOpacity(LDHUDStyle::UIText);
    const FString Rest=Latest.bRest?FString::Printf(TEXT("休息 R%u"),uint32(Latest.RestIndex)):FString();
    WorkshopText(PhaseRest,Rest);
    PhaseRest->SetColorAndOpacity(LDHUDStyle::TextSecondary);
    WorkshopText(PhaseStep,Latest.bRest?LDHUD::RestStepText(Latest.Step):FString());
    const int64 Remaining=Latest.bClock&&Latest.StepEndsAtMs>0?FMath::Max<int64>(0,Latest.StepEndsAtMs-Latest.NowMs):-1;
    WorkshopText(PhaseClock,Remaining>=0?LDHUD::Clock(Remaining):FString());
}

void ULDWorkshop::AllocationApplied(const FLDBuildReceipt& Receipt)
{
    if(Mode!=EMode::Open||!View.bValid||Receipt.Header.Generation!=View.Generation||Receipt.Header.OriginalLife!=View.Life
        ||Receipt.PointsSpent.Num()!=StatRows.Num()){return;}
    for(int32 Index=0;Index<StatRows.Num();++Index)
    {if(Receipt.PointsSpent[Index]>0){StatRows[Index].PendingFlashRevision=Receipt.Header.Revisions.BuildRevision;}}
    LDWidgets::PlayCue(this,ELDAudioCue::UIAddPoint);
    RefreshCharacter();
}

void ULDWorkshop::FinalModApplied(const FLDBuildReceipt& Receipt,const FLDFinalModSelection& Selection)
{
    if(Mode!=EMode::Open||!View.bValid||Receipt.Header.Generation!=View.Generation||Receipt.Header.OriginalLife!=View.Life){return;}
    PendingFinalFeedback=Selection;
    PendingFinalRevision=Receipt.Header.Revisions.BuildRevision;
}

void ULDWorkshop::InterruptFinalFeedback()
{
    PendingFinalFeedback=FLDFinalModSelection();PendingFinalRevision=0;
    for(FHead& Head:Heads){Head.FinalAt=-1.;LDTween::SetScale(Head.Final,1.f);}
}

void ULDWorkshop::RefreshCharacter()
{
    if(!IsOpen()){return;}
    if(UBorder* PanelBorder=Cast<UBorder>(CharacterPanel.Get())){PanelBorder->SetPadding(FMargin(24,22,24,86));}
    const FLDDemoContentDefinitions* Content=Latest.Content;
    WorkshopText(PortraitGlyph,Latest.HolderName.IsEmpty()?FString(TEXT("?")):Latest.HolderName.Left(1));
    if(Portrait&&Portrait->GetBrushColor()!=Latest.HolderColor){Portrait->SetBrushColor(Latest.HolderColor);}
    WorkshopText(HolderName,Latest.HolderName);
    WorkshopText(LevelBadge,View.bValid?FString::FromInt(View.Level):FString(TEXT("—")));
    WorkshopText(LevelLine,Latest.Level.Experience);
    const int64 Unspent=StatBar.IsValid()&&View.bValid?StatBar->EffectiveUnspent():View.bValid?FMath::Max<int64>(0,View.UnspentPoints):0;
    WorkshopShown(PointsBox,Unspent>0);
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
            Row.Name=LDWidgets::Text(WidgetTree,TEXT(""),LDHUDStyle::TypeLead,NAME_None,LDUIFonts::ERole::Strong);Row.Name->SetAutoWrapText(false);
            Row.Name->SetColorAndOpacity(LDHUDStyle::UIText);
            auto* NameBox=WidgetTree->ConstructWidget<USizeBox>();NameBox->SetMinDesiredWidth(52.f);NameBox->SetContent(Row.Name);
            auto* NameSlot=Line->AddChildToHorizontalBox(NameBox);NameSlot->SetVerticalAlignment(VAlign_Center);NameSlot->SetPadding(FMargin(12,0,0,0));
            auto* Meter=WidgetTree->ConstructWidget<USizeBox>();Meter->SetHeightOverride(10.f);Row.Meter=Meter;
            auto* MeterSlot=Line->AddChildToHorizontalBox(Meter);MeterSlot->SetVerticalAlignment(VAlign_Center);MeterSlot->SetPadding(FMargin(12,0,0,0));
            MeterSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            Row.Points=LDWidgets::Text(WidgetTree,TEXT(""),LDHUDStyle::TypeSection,NAME_None,LDUIFonts::ERole::Number);Row.Points->SetAutoWrapText(false);
            Row.Points->SetColorAndOpacity(LDHUDStyle::UIText);Row.Points->SetJustification(ETextJustify::Right);
            auto* PointsBox34=WidgetTree->ConstructWidget<USizeBox>();PointsBox34->SetMinDesiredWidth(34.f);PointsBox34->SetContent(Row.Points);
            Line->AddChildToHorizontalBox(PointsBox34)->SetVerticalAlignment(VAlign_Center);
            if(UHorizontalBoxSlot* PointsSlot=Cast<UHorizontalBoxSlot>(PointsBox34->Slot)){PointsSlot->SetPadding(FMargin(12,0,0,0));}
            Built.Add(NameBox);Built.Add(PointsBox34);
            Row.Keystone=LDWidgets::Text(WidgetTree,TEXT("◇"),LDHUDStyle::TypeSmall);
            Line->AddChildToHorizontalBox(Row.Keystone)->SetVerticalAlignment(VAlign_Center);
            ScaleFresh(Row.Keystone);
            const int32 StatIndex=Index;
            Row.Plus=LDWidgets::Button(WidgetTree,TEXT("+"),[this,StatIndex](){if(StatBar.IsValid()){StatBar->Allocate(StatIndex);}},true,
                FName(*FString::Printf(TEXT("WorkshopStatPlus%d"),StatIndex)));
            Row.Plus->EnableRepeat([this,StatIndex](){return StatBar.IsValid()&&StatBar->CanAllocate(StatIndex)&&StatBar->Allocate(StatIndex);});
            LDWidgets::SetLook(Row.Plus,LDUIPaint::EButton::Plus);
            if(UTextBlock* PlusText=Cast<UTextBlock>(Row.Plus->GetContent()))
            {PlusText->SetFont(LDUIFonts::Font(LDUIFonts::ERole::Strong,LDHUDStyle::TypeSection));PlusText->SetJustification(ETextJustify::Center);PlusText->SetRenderTranslation(FVector2D(0,-2));}
            {auto Style=Row.Plus->GetStyle();Style.NormalPadding=FMargin(0);Style.PressedPadding=FMargin(0);Row.Plus->SetStyle(Style);}
            auto* PlusBox=WidgetTree->ConstructWidget<USizeBox>();PlusBox->SetWidthOverride(36.f);PlusBox->SetHeightOverride(36.f);PlusBox->SetContent(Row.Plus);
            auto* PlusSlot=Line->AddChildToHorizontalBox(PlusBox);PlusSlot->SetPadding(FMargin(12,0,0,0));PlusSlot->SetVerticalAlignment(VAlign_Center);
            auto* Frame=WidgetTree->ConstructWidget<UBorder>();Frame->SetBrushColor(FLinearColor::Transparent);Frame->SetPadding(FMargin(0));Frame->SetContent(Line);
            Frame->SetVerticalAlignment(VAlign_Center);Frame->SetVisibility(ESlateVisibility::Visible);Row.Row=Frame;
            auto* RowHeight=WidgetTree->ConstructWidget<USizeBox>();RowHeight->SetMinDesiredHeight(54.f);RowHeight->SetContent(Frame);Built.Add(RowHeight);
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
            const int32 ShownPts=StatBar.IsValid()&&View.bValid?StatBar->EffectivePoints(Index):LDHUD::StatPointsOf(View,Row.StatId);
            const LDHUD::FStatRowView Shown=LDHUD::StatRow(*Stats[Index],ShownPts,Content);
            WorkshopText(Row.Name,Shown.Name);WorkshopText(Row.Points,FString::FromInt(Shown.Points));WorkshopText(Row.Glyph,Shown.Glyph);
            const bool bKeystone=Shown.Points>=Content->Parameters.KeystoneThresholdPoints;
            Row.Points->SetColorAndOpacity(bKeystone?LDHUDStyle::UIAction:LDHUDStyle::UIText);
            if(Row.Keystone)
            {
                WorkshopText(Row.Keystone,bKeystone?FString(TEXT("★")):FString(TEXT("◇")));
                Row.Keystone->SetColorAndOpacity(bKeystone?LDHUDStyle::UIAction:LDHUDStyle::TextSecondary);
                Row.Keystone->SetToolTipText(FText::FromString(FString::Printf(TEXT("%d / %lld"),Shown.Points,Content->Parameters.KeystoneThresholdPoints)));
            }
            Row.Value=Shown.Points;
            if(Row.PendingFlashRevision!=0&&View.bValid&&View.BuildRevision>=Row.PendingFlashRevision)
            {
                Row.PendingFlashRevision=0;Row.AddedAt=UiNow();
                Row.FlashCell=Row.Value>0?FMath::Min(Row.Value-1,WorkshopMeterCells-1):INDEX_NONE;
            }
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
            auto* Height=WidgetTree->ConstructWidget<USizeBox>();Height->SetContent(Line);Built.Add(Height);
            AttributeList->AddChildToVerticalBox(Height)->SetPadding(FMargin(0,Index?1.f:0.f,0,0));
        }
        auto* Line=Cast<UHorizontalBox>(Cast<USizeBox>(AttributeList->GetChildAt(AttributeList->GetChildrenCount()-1))->GetContent());
        UTextBlock* Name=LDWidgets::Text(WidgetTree,TEXT(""),14,FName(*FString::Printf(TEXT("WorkshopAttributeName%d"),Index)));Name->SetAutoWrapText(false);Name->SetColorAndOpacity(LDHUDStyle::TextSecondary);
        UTextBlock* Value=LDWidgets::Text(WidgetTree,TEXT(""),16,FName(*FString::Printf(TEXT("WorkshopAttributeValue%d"),Index)),LDUIFonts::ERole::Number);Value->SetAutoWrapText(false);
        Value->SetColorAndOpacity(LDHUDStyle::UIText);Value->SetJustification(ETextJustify::Right);
        auto* Half=WidgetTree->ConstructWidget<UHorizontalBox>();Built.Add(Half);
        auto* NameSlot=Half->AddChildToHorizontalBox(Name);NameSlot->SetVerticalAlignment(VAlign_Center);
        auto* ValueSlot=Half->AddChildToHorizontalBox(Value);ValueSlot->SetPadding(FMargin(12,0,0,0));ValueSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        auto* Fit=LDWidgets::Fit(WidgetTree,Half);
        auto* HalfSlot=Line->AddChildToHorizontalBox(Fit);HalfSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));HalfSlot->SetPadding(FMargin(Index%2?18.f:0.f,0,0,0));
        AttributeHalves.Add(Fit);
        AttributeNames.Add(Name);AttributeValues.Add(Value);ScaleFresh(Name);ScaleFresh(Value);
    }
    // The fitting cells need a stable row height, not a height derived from their own scaled desired size. The row is
    // taller than the value's line (about 29 units), so the fitting box never scales to the row's exact height: at
    // 1280x720 pixel rounding makes small text a fraction taller than the size the box measured and it would stick out.
    for(UWidget* Row:AttributeList->GetAllChildren()){Cast<USizeBox>(Row)->SetHeightOverride(32.f*TextScale);}
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
    bool bStampStarted=false;
    LDHUD::EStorageColumn FinalColumn=LDHUD::EStorageColumn::Body;
    const bool bHasFinalColumn=LDHUD::FinalModColumn(View,FinalColumn);
    for(int32 Index=0;Index<Heads.Num();++Index)
    {
        FHead& Head=Heads[Index];
        const LDHUD::EStorageColumn Column=static_cast<LDHUD::EStorageColumn>(Index);
        const FName Active=View.bValid&&Index>=1&&Index<=3?LDHUD::ActiveInSlot(View,uint8(Index-1)):NAME_None;
        const FLDActiveSlotView* Learned=Active.IsNone()?nullptr:View.Actives.FindByPredicate([Index](const FLDActiveSlotView& Row)
            {return Row.Slot==uint8(Index-1);});
        const ELDSkillBranch Branch=Learned?static_cast<ELDSkillBranch>(Learned->Branch):ELDSkillBranch::None;
        const LDWorkshop::FColumnHead Shown=LDWorkshop::ColumnHead(Column,Active,Branch,Latest.HolderName,Latest.Content);
        Head.Active=Active;Head.Branch=Branch;Head.bEmpty=Shown.bEmpty;Head.Elements=Shown.Elements;
        FString Key;
        if(Column==LDHUD::EStorageColumn::Body){Key=LDTerms::ColumnBody;}
        else if(Column==LDHUD::EStorageColumn::Dash){Key=Latest.DashKey.IsEmpty()?FString(TEXT("D")):Latest.DashKey;}
        else{Key=Latest.Keys.Of(uint8(Index-int32(LDHUD::EStorageColumn::SlotQ)));}
        WorkshopText(Head.Key,Key);
        const FString Name=Shown.Name;
        WorkshopText(Head.Name,Head.Active.IsNone()?Name:Name+TEXT("  ")+LDHUD::RankDots(LDSkillBranch::RankOf(Head.Branch)));
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
        WorkshopText(Head.Momentum,Shown.Momentum);WorkshopShown(Head.Momentum,!Shown.Momentum.IsEmpty(),ESlateVisibility::HitTestInvisible);
        WorkshopText(Head.Cooldown,Shown.Cooldown);WorkshopShown(Head.Cooldown,!Shown.Cooldown.IsEmpty(),ESlateVisibility::HitTestInvisible);
        {
            // A locked final mod shows its icon over the head of what it binds (本体 when it binds the holder).
            const FLDFinalModSelection& Final=View.FinalMod;
            const bool bHere=bHasFinalColumn&&Column==FinalColumn;
            const bool bShowsFinal=bHere&&LDUIIcons::Apply(Head.Final,LDHUD::ContentIconId(Latest.Content,Final.DefinitionId.ToString()),17.f);
            if(!bShowsFinal){WorkshopShown(Head.Final,false);}
            if(bShowsFinal&&LDHUD::FinalStampReady(View,PendingFinalRevision,PendingFinalFeedback,
                !Latest.bStage&&WorkshopHasArea(Rects.Column[Index])))
            {Head.FinalAt=UiNow();bStampStarted=true;}
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
    if(bStampStarted||(PendingFinalRevision!=0&&View.bValid&&View.BuildRevision>=PendingFinalRevision
        &&(View.FinalMod!=PendingFinalFeedback||!bHasFinalColumn)))
    {PendingFinalFeedback=FLDFinalModSelection();PendingFinalRevision=0;}
}

void ULDWorkshop::ShowTextDock(const FString& Key,const FString& Name,const FString& Summary,const FString& Body,const FString& IconId,const FString& Glyph)
{
    if(DockKey==Key){return;}
    DockKey=Key;DockKindValue=0;
    ShowDockChains({});
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
    WorkshopShown(DockPreviewTitle,false);WorkshopShown(DockPreviewBody,false);WorkshopShown(DockPreviewPager,false);
    if(Storage.IsValid()){Storage->SetPreviewCaption(NAME_None,FString(),FString());}
}

// Rebuilt only when the chains or the text scale change: the icon squares do not follow ApplyTextScale.
void ULDWorkshop::ShowDockChains(const TArray<LDHUD::FRecapChain>& Chains)
{
    if(!DockChains||(DockChainsShown==Chains&&FMath::IsNearlyEqual(DockChainsScale,TextScale))){return;}
    DockChainsShown=Chains;DockChainsScale=TextScale;
    DockChains->ClearChildren();
    for(const LDHUD::FRecapChain& Chain:Chains)
    {
        auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();
        LDRecapChain::Fill(WidgetTree,Row,Chain,FMath::RoundToFloat(22.f*TextScale),FMath::RoundToInt(15.f*TextScale));
        DockChains->AddChildToVerticalBox(Row)->SetPadding(FMargin(0,3,0,0));
    }
    WorkshopShown(DockChains,!Chains.IsEmpty(),ESlateVisibility::SelfHitTestInvisible);
}

void ULDWorkshop::ShowComponentDock(FName Component,const ULDComponentStorage::FFocus* Focus)
{
    const FLDDemoContentDefinitions* Content=Latest.Content;
    const FString DropText=Focus&&Focus->bOverDrop?LDHUD::DropPreviewText(Component,Focus->Drop,View,Content):FString();
    const uint64 CombatRevision=Latest.Combat?Latest.Combat->Revision:0;
    FString Key=FString::Printf(TEXT("C|%s|%llu|%llu|%d|%d|%d|%d|%d|%s|%s|%s"),*Component.ToString(),View.BuildRevision,
        CombatRevision,int32(Latest.Stage),int32(Latest.bFinalField),int32(Latest.bClock),int32(Mode),Storage.IsValid()&&Storage->HasPendingRequest()?1:0,
        *DropText,TEXT(""),Focus?*FString::FromInt(int32(Focus->HoveredElement)):TEXT(""));
    Key+=FString::Printf(TEXT("|%u|%u|%u|%d|%d|%llu|%p|%d"),Latest.Match,View.Generation,View.Life,int32(View.LifePhase),
        int32(View.bValid),View.CombatInstalledRevision,Latest.ContentSnapshot,int32(Focus&&Focus->bOverDrop));
    Key+=TEXT("|")+View.CatalogDigest.ToHex();
    if(Latest.Combat)
    {
        Key+=FString::Printf(TEXT("|%u|%u|%d"),Latest.Combat->Generation,Latest.Combat->Life,
            int32(Latest.NowMs>=Latest.Combat->AsOfMs));
    }
    if(Focus&&Focus->bOverDrop)
    {
        Key+=FString::Printf(TEXT("|%d|%s|%s|%s"),int32(Focus->Drop.Kind),*Focus->Drop.TargetId.ToString(),
            *Focus->Drop.Host.ToString(),*Focus->Drop.Replacing.ToString());
    }
    if(DockKey==Key){return;}
    DockKey=Key;
    ShowDockChains({});
    const LDHUD::FCardFaceView Card=LDHUD::CardFace(Component,Content,Latest.Stats);
    const LDWorkshop::FCardFacts Facts=LDWorkshop::CardFacts(Component,Content);
    DockKindValue=uint8(Facts.Type);
    WorkshopText(DockName,Card.Name);
    WorkshopText(DockSummary,FString());WorkshopShown(DockSummary,false);
    const FString Body=Card.Keywords.IsEmpty()?Card.Sentence:Card.Sentence+TEXT("\n")+FString::Join(Card.Keywords,TEXT("\n"));
    if(DockBody){DockBody->SetText(FText::FromString(Body));}
    WorkshopText(DockKind,LDTerms::CardTypeName(uint8(Facts.Type)));
    DockKind->SetColorAndOpacity(LDUIShapes::KindColor(uint8(Facts.Type)));
    WorkshopText(DockTags,LDTerms::CardFormName(uint8(Facts.Form)));
    // ×n of the same card the build owns.
    const int32 Copies=LDHUD::OwnedCopies(View,Component);
    WorkshopText(DockChips,Copies>1?LDTerms::CopiesBadge(Copies):FString());
    WorkshopText(DockGlyph,Card.Glyph);
    // The icon is painted as the card's own tile (LDUIPaint::Tile); the glyph stands in when there is no picture.
    DockIcon=LDUIIcons::Find(Card.IconId);
    WorkshopShown(DockPicture,false);
    WorkshopShown(DockGlyph,!DockIcon.IsValid(),ESlateVisibility::HitTestInvisible);
    // Right: where it sits (or where letting go puts it), what it works with, and what to watch.
    const FLDCardInstanceView* Owned=LDHUD::FindCard(View,Component);
    const bool bInstalled=Owned&&Owned->IsInstalled();
    WorkshopText(DockWhereTitle,bInstalled&&DropText.IsEmpty()?LDTerms::WorkshopInstalledOn:LDTerms::WorkshopGoesOn);
    FString Where;
    if(!DropText.IsEmpty()){Where=DropText;}
    else if(bInstalled)
    {
        LDHUD::EStorageColumn Column=LDHUD::EStorageColumn::Body;
        const FString Target=WorkshopStripParens(LDHUD::CardTargetText(*Owned,Content));
        Where=!Target.IsEmpty()?Target:LDHUD::CardColumn(View,Owned->Target.Kind,Owned->Target.Active,Column)?LDHUD::ColumnLabel(Column):FString();
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
    for(FString& Line:Lines){Line.RemoveFromStart(TEXT("联动："));Links.Add(Line);}
    if(bInstalled){Notes.Add(LDTerms::InstalledCardHint);}
    if(Focus&&Focus->bOverDrop&&!Focus->Drop.Replacing.IsNone())
    {
        Notes.Add(LDTerms::DropReplaces(LDHUD::ContentName(Content,LDHUD::CardDefinition(Focus->Drop.Replacing).ToString()),
            LDHUD::ColumnLabel(Focus->Drop.Column)));
    }
    WorkshopText(DockLinks,FString::Join(Links,TEXT("\n")));
    WorkshopText(DockNotes,FString::Join(Notes,TEXT("\n")));
    WorkshopShown(DockWhereTitle,true,ESlateVisibility::HitTestInvisible);WorkshopShown(DockWhere,true,ESlateVisibility::HitTestInvisible);
    const bool bPreview=Focus&&Focus->bOverDrop;
    WorkshopShown(DockLinksTitle,!bPreview&&!Links.IsEmpty(),ESlateVisibility::HitTestInvisible);WorkshopShown(DockLinks,!bPreview&&!Links.IsEmpty(),ESlateVisibility::HitTestInvisible);
    WorkshopShown(DockNotesTitle,!bPreview&&!Notes.IsEmpty(),ESlateVisibility::HitTestInvisible);WorkshopShown(DockNotes,!bPreview&&!Notes.IsEmpty(),ESlateVisibility::HitTestInvisible);
    WorkshopShown(DockPreviewTitle,bPreview,ESlateVisibility::HitTestInvisible);WorkshopShown(DockPreviewBody,bPreview,ESlateVisibility::HitTestInvisible);
    PreviewPage=0;Preview=LDWorkshopPreview::FResult();
    if(bPreview&&Storage.IsValid()&&!Storage->HasPendingRequest())
    {
        LDWorkshopPreview::FInput Input;Input.Content=Latest.ContentSnapshot;Input.Progression=&View;Input.Combat=Latest.Combat;
        Input.Match=Latest.Match;Input.Stage=Latest.Stage;Input.bClock=Latest.bClock;Input.NowMs=Latest.NowMs;Input.bFinalField=Latest.bFinalField;
        Input.Component=Component;Input.Drop=Focus->Drop;Input.Element=Focus->HoveredElement;
        Preview=LDWorkshopPreview::Compare(Input);
        if(Preview.Status==LDWorkshopPreview::EStatus::Unavailable&&!Preview.Error.IsEmpty())
        {UE_LOG(LogLDUI,Warning,TEXT("LD_WORKSHOP_PREVIEW_UNAVAILABLE component=%s error=%s"),*Component.ToString(),*Preview.Error);}
    }
    if(Storage.IsValid())
    {
        FString Caption;
        if(bPreview&&Preview.Status==LDWorkshopPreview::EStatus::Ready&&!Preview.Lines.IsEmpty())
        {
            const LDWorkshopPreview::FLine& Line=Preview.Lines[0];
            Caption=Line.Label+TEXT(" ")+Line.Before+TEXT(" → ")+Line.After;
            if(Preview.bPartial){Caption+=TEXT(" · 部分");}
        }
        Storage->SetPreviewCaption(Component,DropText,Caption);
    }
    RefreshPreviewPage();
}

void ULDWorkshop::RefreshPreviewPage()
{
    if(!DockPreviewBody||!DockPreviewTitle||!DockPreviewTitle->IsVisible())
    {WorkshopShown(DockPreviewPager,false);return;}
    FString Body;
    using EStatus=LDWorkshopPreview::EStatus;
    WorkshopText(DockPreviewTitle,Preview.Status==EStatus::Ready&&Preview.bPartial?LDTerms::WorkshopPreviewPartial:LDTerms::WorkshopPreview);
    if(Storage.IsValid()&&Storage->HasPendingRequest()){Body=LDTerms::WorkshopPreviewWaiting;}
    else if(Preview.Status==EStatus::Synchronizing){Body=LDTerms::WorkshopPreviewSync;}
    else if(Preview.Status==EStatus::ChoiceRequired){Body=LDTerms::WorkshopPreviewChoice;}
    else if(Preview.Status==EStatus::Refused){Body=LDHUD::GrowthReasonText(Preview.Reason);}
    else if(Preview.Status==EStatus::Unavailable){Body=LDTerms::WorkshopPreviewUnavailable;}
    else if(Preview.Lines.IsEmpty()){Body=Preview.bPartial?LDTerms::WorkshopPreviewConditional:LDTerms::WorkshopPreviewNoChange;}
    const int32 PerPage=3;
    const int32 Pages=FMath::Max(1,FMath::DivideAndRoundUp(Preview.Lines.Num(),PerPage));
    PreviewPage=FMath::Clamp(PreviewPage,0,Pages-1);
    if(Preview.Status==EStatus::Ready)
    {
        for(int32 Index=PreviewPage*PerPage;Index<FMath::Min(Preview.Lines.Num(),(PreviewPage+1)*PerPage);++Index)
        {
            const LDWorkshopPreview::FLine& Line=Preview.Lines[Index];
            const TCHAR* Tag=Line.Impact==LDWorkshopPreview::EImpact::Beneficial?TEXT("good")
                :Line.Impact==LDWorkshopPreview::EImpact::Cost?TEXT("cost"):nullptr;
            FString Change=WorkshopRichEscape(Line.After);
            if(Tag){Change=FString::Printf(TEXT("<%s>%s</>"),Tag,*Change);}
            if(!Body.IsEmpty()){Body+=TEXT("\n");}
            Body+=WorkshopRichEscape(Line.Label)+TEXT("  ")+WorkshopRichEscape(Line.Before)+TEXT(" → ")+Change;
        }
    }
    if(!DockPreviewBody->GetText().ToString().Equals(Body,ESearchCase::CaseSensitive)){DockPreviewBody->SetText(FText::FromString(Body));}
    WorkshopShown(DockPreviewPager,Preview.Status==EStatus::Ready&&Pages>1,ESlateVisibility::SelfHitTestInvisible);
    WorkshopText(DockPreviewPageText,FString::Printf(TEXT("%d / %d"),PreviewPage+1,Pages));
    if(DockPreviewPrevious){DockPreviewPrevious->SetIsEnabled(PreviewPage>0);}
    if(DockPreviewNext){DockPreviewNext->SetIsEnabled(PreviewPage+1<Pages);}
}

void ULDWorkshop::RefreshDock(FVector2D Pointer)
{
    // The dock follows the held or hovered component first, then a hovered card of the tray, a stat row or a column head.
    const bool bChoiceStage=Choices.IsValid()&&Choices->IsStageShown();
    if(!bChoiceStage&&Storage.IsValid()&&!Storage->Focus().Component.IsNone())
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
    if(Content&&Mode==EMode::Open)
    {
        for(const FReaction& Reaction:Reactions)
        {
            if(!Reaction.Row||!ReactionBlock||!ReactionBlock->IsVisible()||!Reaction.Row->GetCachedGeometry().IsUnderLocation(Pointer)){continue;}
            const FString Name=LDWorkshop::ReactionName(Reaction.First,Reaction.Second);
            const FString Pair=LDHUD::ElementName(Reaction.First)+TEXT(" + ")+LDHUD::ElementName(Reaction.Second);
            ShowTextDock(TEXT("R|")+Name,Name,Pair,LDTerms::ReactionExplanation(Name),FString(),FString());
            return;
        }
        const TArray<const FLDDemoStatDefinition*> Stats=Content->StatsInDesignOrder();
        for(int32 Index=0;Index<StatRows.Num()&&Stats.IsValidIndex(Index);++Index)
        {
            if(!StatRows[Index].Row||!StatRows[Index].Row->IsHovered()){continue;}
            const LDHUD::FStatRowView Row=LDHUD::StatRow(*Stats[Index],StatRows[Index].Value,Content);
            const FModifierKeysState Mods=FSlateApplication::IsInitialized()?FSlateApplication::Get().GetModifierKeys():FModifierKeysState();
            const int32 Threshold=int32(FMath::Max<int64>(1,Content->Parameters.KeystoneThresholdPoints));
            const int32 AddStep=Mods.IsControlDown()?FMath::Max(1,Threshold-Row.Points):Mods.IsShiftDown()?5:1;
            const int32 RefundStep=Mods.IsShiftDown()?FMath::Min(5,FMath::Max(1,Row.Points)):1;
            const bool bCanRespec=Storage.IsValid()&&Storage->BuildGate().bCanRespec;
            FLDProgressionView EffectiveProg=View;
            if(StatBar.IsValid()&&View.bValid)
            {
                EffectiveProg.StatPoints.Reset();
                for(int32 SIdx=0;SIdx<StatRows.Num()&&Stats.IsValidIndex(SIdx);++SIdx)
                {
                    const int32 Eff=StatBar->EffectivePoints(SIdx);
                    if(Eff>0){EffectiveProg.StatPoints.Add({StatRows[SIdx].StatId,Eff});}
                }
            }
            const LDHUD::FStatHoverPreview HoverPreview=LDHUD::StatHoverPreview(*Stats[Index],Row.Points,AddStep,RefundStep,
                bCanRespec,Latest.Attributes,*Content,Latest.ContentSnapshot,EffectiveProg.bValid?&EffectiveProg:nullptr,Latest.Combat);
            TArray<FString> Lines;
            for(const FString& AttrLine:HoverPreview.AttributeLines){Lines.Add(WorkshopRichEscape(AttrLine));}
            Lines.Add(FString::Printf(TEXT("<b>%s</>"),*WorkshopRichEscape(HoverPreview.KeystoneProgress)));
            if(!HoverPreview.KeystoneRule.IsEmpty()){Lines.Add(WorkshopRichEscape(HoverPreview.KeystoneRule));}
            if(HoverPreview.bKeystoneLossWarning&&!HoverPreview.RefundWarning.IsEmpty())
            {
                Lines.Add(FString::Printf(TEXT("<cost>%s</>"),*WorkshopRichEscape(HoverPreview.RefundWarning)));
            }
            ShowTextDock(TEXT("S|")+Row.Name+FString::FromInt(Row.Points)+HoverPreview.DeltaSummary+HoverPreview.RefundWarning,
                Row.Name,HoverPreview.DeltaSummary,FString::Join(Lines,TEXT("\n")),Row.IconId,Row.Glyph);
            return;
        }
    }
    for(const FHead& Head:Heads)
    {
        if(!Head.Box||!Head.Box->IsHovered()||Head.Active.IsNone()){continue;}
        const LDHUD::FActiveCardView Card=LDHUD::ActiveCard(Head.Active,Content,Latest.Stats,Head.Branch);
        const LDHUD::FActionDetailView Detail=LDHUD::ActiveChoiceDetail(Card,false,FString());
        ShowTextDock(TEXT("A|")+Head.Active.ToString()+Detail.Body,Detail.Name,Detail.Summary,Detail.Body,Card.IconId,Card.Glyph);
        return;
    }
    if(Latest.bDead)
    {
        const LDHUD::FDamageListView& Recap=Latest.DeathRecap;
        const FString Name=Recap.bAvailable?Recap.Heading:FString(TEXT("承伤记录同步中"));
        // The rows are drawn as icon chains under the heading (the text rows only key the dock).
        const FString Rows=Recap.bAvailable?FString::Join(Recap.Rows,TEXT("\n")):FString();
        ShowTextDock(TEXT("D|")+Name+TEXT("|")+Rows,Name,FString(),FString(),FString(),FString());
        ShowDockChains(Recap.bAvailable?Recap.Chains:TArray<LDHUD::FRecapChain>());
    }
    else if(DockKey.StartsWith(TEXT("D|")))
    {
        ShowTextDock(FString(),FString(),FString(),FString(),FString(),FString());
    }
}

void ULDWorkshop::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);
    AnimateOpening();
    if(!IsOpen()||!FSlateApplication::IsInitialized()){return;}
    RefreshDock(FSlateApplication::Get().GetCursorPos());
}
void ULDWorkshop::AnimateOpening()
{
    if(!Canvas){return;}
    // Fade the backdrop without moving or hiding the interactive surfaces during layout.
    float Shown=1.f;
    if(ClosingAt>=0.)
    {
        const double Age=UiNow()-ClosingAt;
        if(Age>=LDHUDStyle::MotionWorkshopClose){ClosingAt=-1.;WorkshopShown(this,false);Shown=1.f;}
        else{Shown=1.f-LDTween::Progress(Age,LDHUDStyle::MotionWorkshopClose,LDTween::ECurve::EaseIn);}
    }
    else if(OpenedAt>=0.){Shown=LDTween::Progress(UiNow()-OpenedAt,LDHUDStyle::MotionWorkshopOpen,LDTween::ECurve::EaseOut);}
    for(int32 Index=0;Index<Canvas->GetChildrenCount();++Index)
    {
        UWidget* Child=Canvas->GetChildAt(Index);
        if(Child==Blur){LDTween::SetOpacity(Child,ClosingAt>=0.?Shown:1.f);continue;}
        LDTween::SetOpacity(Child,ClosingAt>=0.?Shown:1.f);
    }
    for(FHead& Head:Heads)
    {
        if(!Head.Final){continue;}
        float Stamp=1.f;
        if(Head.FinalAt>=0.)
        {
            const double Age=UiNow()-Head.FinalAt;
            if(Age>=LDHUDStyle::MotionFinalLock){Head.FinalAt=-1.;}
            else{Stamp=FMath::Lerp(1.6f,1.f,LDTween::Progress(Age,LDHUDStyle::MotionFinalLock,LDTween::ECurve::Back));}
        }
        LDTween::SetScale(Head.Final,Stamp);
    }
}
FReply ULDWorkshop::NativeOnMouseWheel(const FGeometry& Geometry,const FPointerEvent& Event)
{
    // The row under the wheel event itself (the band below is hidden while the workshop is open). Only while the full
    // workshop shows its character panel: a collapsed panel keeps its rows' old geometry.
    const bool bRowsShown=IsOpen()&&CharacterPanel&&CharacterPanel->IsVisible();
    if(bRowsShown&&StatBar.IsValid()&&Event.GetWheelDelta()!=0.f)
    {
        for(int32 Index=0;Index<StatRows.Num();++Index)
        {
            const UWidget* Row=StatRows[Index].Row;
            if(!Row||!Row->IsVisible()||!Row->GetCachedGeometry().IsUnderLocation(Event.GetScreenSpacePosition())){continue;}
            if(Event.GetWheelDelta()>0.f){StatBar->Allocate(Index);}else{StatBar->Refund(Index);}
            return FReply::Handled();
        }
    }
    return Super::NativeOnMouseWheel(Geometry,Event);
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

void ULDWorkshop::PaintBackdrop(const LDUIPaint::FContext& Paint) const
{
    if(!IsOpen()&&ClosingAt<0.){return;}
    const FGeometry& Geometry=Paint.Geometry;
    const int32 Layer=Paint.Layer;
    using LDUIPaint::FFill;using LDUIPaint::Hex;using LDUIShapes::EShape;
    const double Now=UiNow();
    const auto Box=[&Geometry](const UWidget* Widget){return Widget?LDUIShapes::RectIn(Geometry,Widget->GetCachedGeometry()):FBox2D(ForceInit);};
    const auto Shown=[](const UWidget* Widget){return Widget&&Widget->IsVisible()&&Widget->GetCachedGeometry().GetLocalSize().X>1.f;};
    const int32 Base=Layer,Ink=Layer+3;
    const FLinearColor Line=FLinearColor(1,1,1,.075f);

    // ---- surfaces ----
    if((Latest.bRest||Latest.bDead)&&WorkshopHasArea(Rects.Phase))
    {
        // .phase: a horizontal fade from the solid surface, its hairline and highlight.
        Paint.Shape(Base,EShape::Rounded,Rects.Phase,6.f,FFill::Linear(90.f,{{0.f,Hex(0x121723,.95f)},{1.f,Hex(0x121723,.7f)}}));
        Paint.Edge(Base,EShape::Rounded,Rects.Phase,6.f,Line,1.f);
        Paint.Line(Base,FVector2D(Rects.Phase.Min.X+6.f,Rects.Phase.Min.Y+1.5f),FVector2D(Rects.Phase.Max.X-6.f,Rects.Phase.Min.Y+1.5f),FLinearColor(1,1,1,.08f),1.f);
    }
    if(WorkshopHasArea(Rects.Character)){Paint.Panel(Base,Rects.Character);}
    if(WorkshopHasArea(Rects.Board)){Paint.Panel(Base,Rects.Board);}
    if(WorkshopHasArea(Rects.Dock))
    {
        Paint.Panel(Base,Rects.Dock);
        if(DockRight&&DockRight->IsVisible()&&!(Latest.bFinalStage))
        {
            Paint.Line(Base+3,FVector2D(DockDividerX,Rects.Dock.Min.Y+24.f),FVector2D(DockDividerX,Rects.Dock.Max.Y-24.f),Line,1.f);
        }
    }
    if(WorkshopHasArea(Rects.Library))
    {
        FBox2D Right=Rects.Library;
        if(WorkshopHasArea(Rects.Pending)){Right+=Rects.Pending;}
        Paint.Panel(Base,Right);
    }
    if(WorkshopHasArea(Rects.Pending))
    {
        // .inbox: the library's lower section under a hairline, warmed with a faint yellow, its square-led title and rule.
        const FBox2D& P=Rects.Pending;
        Paint.Shape(Base+3,EShape::Rounded,WorkshopBox(P.Min.X+1.f,P.Min.Y,P.Max.X-1.f,P.Max.Y-1.f),5.f,
            FFill::Linear(180.f,{{0.f,LDHUDStyle::UIAction.CopyWithNewOpacity(.05f)},{.6f,LDHUDStyle::UIAction.CopyWithNewOpacity(0.f)}}));
        Paint.Line(Base+3,FVector2D(P.Min.X+1.f,P.Min.Y),FVector2D(P.Max.X-1.f,P.Min.Y),Line,1.f);
        const float Y=float(P.Min.Y)+27.f;
        Paint.Shape(Ink,EShape::Rounded,WorkshopBox(P.Min.X+24.f,Y-6.f,P.Min.X+36.f,Y+6.f),2.f,FFill::Solid(LDHUDStyle::UIAction));
        if(Shown(PendingTitle))
        {
            const FBox2D Title=Box(PendingTitle);
            Paint.Line(Ink,FVector2D(Title.Min.X+PendingTitle->GetDesiredSize().X+10.f,Y),FVector2D(P.Max.X-24.f,Y),Line,1.f);
        }
    }
    if(Shown(DockIconFace)&&!DockKey.IsEmpty())
    {
        // .dock .l: the hovered item at 112, a component in its own tile, a skill in its gold art.
        const FBox2D Art=Box(DockIconFace);
        if(DockKey.StartsWith(TEXT("C|"))){Paint.Tile(Ink,Art,DockKindValue,LDUIPaint::ETileState::Normal,DockIcon.Get(),1.f,Now);}
        else{Paint.SkillArt(Ink,Art,false,!DockIcon.IsValid()&&DockGlyph&&DockGlyph->GetText().IsEmpty(),DockIcon.Get());}
    }
    // ---- phase ring and steps ----
    if((Latest.bRest||Latest.bDead)&&PhaseClock&&WorkshopHasArea(Rects.Phase))
    {
        const UWidget* Clock=PhaseClock->GetParent()?static_cast<const UWidget*>(PhaseClock->GetParent()):PhaseClock.Get();
        const FVector2D Centre=Box(Clock).GetCenter();
        float Fraction=0.f;
        if(Latest.bDead){Fraction=Latest.DeathProgress;}
        else if(Latest.bRest&&Latest.bClock&&Latest.StepEndsAtMs>Latest.StepStartedAtMs)
        {Fraction=FMath::Clamp(float(double(Latest.StepEndsAtMs-Latest.NowMs)/double(Latest.StepEndsAtMs-Latest.StepStartedAtMs)),0.f,1.f);}
        Paint.Arc(Ink,Centre,23.f,1.f,FLinearColor(1,1,1,.14f),4.f);
        if(Fraction>0.f){Paint.Arc(Ink+1,Centre,23.f,Fraction,Latest.bDead?LDHUDStyle::UIReject:LDHUDStyle::UIAction,4.f);}
        if(Latest.bRest&&!Latest.bDead)
        {
            const int32 Current=LDWorkshop::RestStepDot(Latest.Step);
            const float Width=34.f,Gap=6.f;
            const float X0=float(Rects.Phase.Max.X)-22.f-LDWorkshop::RestStepDots*Width-(LDWorkshop::RestStepDots-1)*Gap,Y=float(Rects.Phase.GetCenter().Y);
            for(int32 Dot=0;Dot<LDWorkshop::RestStepDots;++Dot)
            {
                const FBox2D Bar=WorkshopBox(X0+Dot*(Width+Gap),Y-2.5f,X0+Dot*(Width+Gap)+Width,Y+2.5f);
                if(Dot==Current){Paint.Halo(Ink,EShape::Rounded,Bar,3.f,LDHUDStyle::UIAction.CopyWithNewOpacity(.6f),10.f);}
                const FLinearColor Color=Dot==Current?LDHUDStyle::UIAction:Dot<Current?LDHUDStyle::UIAction.CopyWithNewOpacity(.45f):FLinearColor(1,1,1,.14f);
                Paint.Shape(Ink+1,EShape::Rounded,Bar,3.f,FFill::Solid(Color));
            }
        }
    }
    // ---- the character column ----
    if(WorkshopHasArea(Rects.Character))
    {
        if(Shown(PortraitFrame))
        {
            // .ava: the experience ring around the holder's face.
            const FBox2D Ava=Box(PortraitFrame);
            const FVector2D Centre=Ava.GetCenter();
            Paint.Arc(Ink,Centre,40.f,1.f,FLinearColor(1,1,1,.12f),5.f);
            const float Xp=Latest.Level.bMax?1.f:FMath::Clamp(Latest.Level.Fraction,0.f,1.f);
            if(Xp>0.f){Paint.Arc(Ink+1,Centre,40.f,Xp,Hex(0xE0A53A),5.f);}
            const FBox2D Face=LDUIPaint::Grow(Ava,-7.f);
            const FLinearColor Holder=Latest.HolderColor;
            Paint.Shape(Ink,EShape::Circle,Face,36.f,FFill::Linear(160.f,{{0.f,FLinearColor::LerpUsingHSV(Holder,FLinearColor::White,.18f)},{1.f,Holder*.42f}}));
            Paint.Edge(Ink+1,EShape::Circle,Face,36.f,FLinearColor(1,1,1,.25f),2.f);
        }
        if(Shown(PointsBox))
        {
            const FBox2D Pts=Box(PointsBox);
            Paint.Shape(Ink,EShape::Rounded,Pts,5.f,FFill::Linear(90.f,{{0.f,LDHUDStyle::UIAction.CopyWithNewOpacity(.2f)},{1.f,LDHUDStyle::UIAction.CopyWithNewOpacity(.03f)}}));
            Paint.Edge(Ink,EShape::Rounded,Pts,5.f,LDHUDStyle::UIAction.CopyWithNewOpacity(.5f),1.f);
        }
        for(const FStatRow& Row:StatRows)
        {
            if(Shown(Row.Icon)){Paint.Edge(Ink+2,EShape::Rounded,Box(Row.Icon),8.f,FLinearColor(1,1,1,.14f),1.f);}
            if(!Shown(Row.Meter)){continue;}
            // .bar: sixteen cells, with the confirmed allocation flashing after the view catches up.
            const FBox2D Meter=Box(Row.Meter);
            const float Gap=2.f,Cell=(float(Meter.GetSize().X)-Gap*(WorkshopMeterCells-1))/WorkshopMeterCells;
            const float Y=float(Meter.GetCenter().Y);
            for(int32 Index=0;Index<WorkshopMeterCells;++Index)
            {
                const FBox2D Piece=WorkshopBox(Meter.Min.X+Index*(Cell+Gap),Y-4.f,Meter.Min.X+Index*(Cell+Gap)+Cell,Y+4.f);
                const FFill Fill=Index<Row.Value?FFill::Linear(180.f,{{0.f,Hex(0xFFF2B0)},{1.f,Hex(0xFFB52E)}}):FFill::Solid(FLinearColor(1,1,1,.08f));
                Paint.Shape(Ink,EShape::Rounded,Piece,1.f,Fill);
            }
            const double Age=Now-Row.AddedAt;
            if(Age>=0.&&Age<LDHUDStyle::MotionAddPoint*3.&&Row.FlashCell!=INDEX_NONE)
            {
                const FBox2D Next=WorkshopBox(Meter.Min.X+Row.FlashCell*(Cell+Gap),Y-5.f,Meter.Min.X+Row.FlashCell*(Cell+Gap)+Cell,Y+5.f);
                Paint.Shape(Ink+1,EShape::Rounded,Next,1.f,FFill::Solid(FLinearColor(1,1,1,1.f-float(Age/(LDHUDStyle::MotionAddPoint*3.)))));
            }
        }
        for(const TObjectPtr<UWidget>& Half:AttributeHalves)
        {
            if(!Shown(Half)){continue;}
            const FBox2D Cell=Box(Half);
            Paint.Line(Ink,FVector2D(Cell.Min.X,Cell.Max.Y+5.f),FVector2D(Cell.Max.X,Cell.Max.Y+5.f),FLinearColor(1,1,1,.05f),1.f);
        }
        if(Shown(AttributeList))
        {
            // .rule: the fading hairline over the derived attributes.
            const FBox2D List=Box(AttributeList);
            Paint.Line(Ink,FVector2D(List.Min.X,List.Min.Y-14.f),FVector2D(List.Max.X,List.Min.Y-14.f),FLinearColor(1,1,1,.14f),1.f,FLinearColor(1,1,1,0.f));
        }
        for(const FReaction& Reaction:Reactions)
        {
            if(!Shown(Reaction.Row)||!Shown(ReactionBlock)){continue;}
            const FBox2D Row=Box(Reaction.Row);
            if(Reaction.bLit)
            {
                Paint.Halo(Ink,EShape::Rounded,Row,5.f,Hex(0xFF7A3D,.2f),18.f);
                Paint.Shape(Ink+1,EShape::Rounded,Row,5.f,FFill::Linear(90.f,{{0.f,Hex(0xFF7A3D,.2f)},{1.f,Hex(0xFFE45C,.06f)}}));
                Paint.Edge(Ink+1,EShape::Rounded,Row,5.f,Hex(0xFF7A3D,.65f),1.f);
            }
            else
            {
                Paint.Shape(Ink+1,EShape::Rounded,Row,5.f,FFill::Solid(FLinearColor(0,0,0,.25f)));
                Paint.Edge(Ink+1,EShape::Rounded,Row,5.f,Line,1.f);
            }
            const float Disc=Reaction.bLit?1.f:.45f;
            if(Reaction.FirstText&&Reaction.FirstText->GetParent()){Paint.ElementDisc(Ink+2,Box(Reaction.FirstText->GetParent()),WorkshopElementColor(Reaction.First),Disc);}
            if(Reaction.SecondText&&Reaction.SecondText->GetParent()){Paint.ElementDisc(Ink+2,Box(Reaction.SecondText->GetParent()),WorkshopElementColor(Reaction.Second),Disc);}
        }
    }
    // ---- the board ----
    if(WorkshopHasArea(Rects.Capacity))
    {
        Paint.Line(Ink,FVector2D(Rects.Board.Min.X+1.f,Rects.Capacity.Max.Y),FVector2D(Rects.Board.Max.X-1.f,Rects.Capacity.Max.Y),Line,1.f);
        if(Storage.IsValid())
        {
            // .segs: one skewed cell per workshop slot in the kind colour of what fills it; the next level's cell dashed.
            // Hovering or holding a stored component previews where it lands: the next free cell pulses green, cells a
            // replacement frees turn orange.
            const LDWorkshop::FCapacityView Capacity=LDWorkshop::Capacity(View,Storage->Model(),Latest.Content);
            const ULDComponentStorage::FFocus& Focus=Storage->Focus();
            TArray<FName> Freed;
            if(Focus.bOverDrop&&!Focus.Drop.Replacing.IsNone()){Freed.Add(Focus.Drop.Replacing);}
            const bool bAdds=!Focus.Component.IsNone()&&!Focus.bInstalled&&Focus.Drops.ContainsByPredicate([](const LDHUD::FCardDrop& Drop){return Drop.Replacing.IsNone();});
            const float Pulse=.55f+.45f*LDTween::PingPong(Now,.8);
            const int32 Cells=Capacity.Capacity+(Capacity.UnlockLevel>0?1:0);
            const float Y=float(Rects.Capacity.GetCenter().Y),CellWidth=CapacityCellStep*40.f/45.f;
            const float Skew=FMath::Min(7.f*FMath::Tan(FMath::DegreesToRadians(18.f)),CellWidth*.2f);
            for(int32 Index=0;Index<Cells;++Index)
            {
                const float X=CapacityCellsAt+Index*CapacityCellStep;
                const TArray<FVector2D> Cell{FVector2D(X+Skew,Y-7.f),FVector2D(X+CellWidth+Skew,Y-7.f),FVector2D(X+CellWidth-Skew,Y+7.f),FVector2D(X-Skew,Y+7.f)};
                if(Index>=Capacity.Capacity)
                {
                    TArray<FVector2D> Loop=Cell;Loop.Add(FVector2D(Cell[0]));
                    for(int32 Edge=0;Edge<4;++Edge)
                    {
                        const FVector2D From=Loop[Edge],To=Loop[Edge+1];
                        for(float At=0.f;At<1.f;At+=.25f){Paint.Line(Ink,FMath::Lerp(From,To,double(At)),FMath::Lerp(From,To,double(At+.14f)),FLinearColor(1,1,1,.25f),1.f);}
                    }
                    continue;
                }
                FLinearColor Color=FLinearColor(1,1,1,.09f);float Glow=0.f;
                if(Capacity.Used.IsValidIndex(Index))
                {
                    const uint8 Kind=uint8(Capacity.Used[Index]);
                    Color=LDUIShapes::KindColor(Kind);Glow=Kind==0?0.f:.5f;
                    if(Freed.Contains(Capacity.Cards[Index])){Color=LDHUDStyle::UIReplace;Glow=.6f;}
                    else if(Focus.bHeld&&Focus.bInstalled&&Capacity.Cards[Index]==Focus.Component){Color=LDHUDStyle::UIAction;Glow=.6f;}
                }
                else if(bAdds&&Index==Capacity.Used.Num()){Color=LDHUDStyle::UILegal.CopyWithNewOpacity(Pulse);Glow=.7f*Pulse;}
                if(Glow>0.f){Paint.Halo(Ink,EShape::Rounded,WorkshopBox(X,Y-7.f,X+CellWidth,Y+7.f),2.f,Color.CopyWithNewOpacity(Glow*.6f),8.f);}
                Paint.Polygon(Ink+1,Cell,FFill::Solid(Color));
                if(!Capacity.Used.IsValidIndex(Index))
                {
                    TArray<FVector2D> Loop=Cell;Loop.Add(FVector2D(Cell[0]));
                    Paint.Stroke(Ink+1,Loop,FLinearColor(1,1,1,.1f),1.f);
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
        const bool bTakes=Focus&&Focus->Drops.ContainsByPredicate([Id](const LDHUD::FCardDrop& Drop){return Drop.Column==Id&&Drop.Replacing.IsNone();});
        const bool bSwaps=Focus&&!bTakes&&Focus->Drops.ContainsByPredicate([Id](const LDHUD::FCardDrop& Drop){return Drop.Column==Id;});
        const FBox2D Back=WorkshopBox(Column.Min.X,Column.Min.Y+44.f,Column.Max.X,Column.Max.Y);
        if(bTakes||bSwaps)
        {
            const FLinearColor Tone=bTakes?LDHUDStyle::UILegal:LDHUDStyle::UIReplace;
            Paint.Halo(Ink,EShape::Rounded,Back,6.f,Tone.CopyWithNewOpacity(.22f),30.f);
            Paint.Shape(Ink+1,EShape::Rounded,Back,6.f,FFill::Linear(180.f,{{0.f,Tone.CopyWithNewOpacity(.16f)},{1.f,Tone.CopyWithNewOpacity(.03f)}}));
            Paint.InnerHalo(Ink+1,EShape::Rounded,Back,6.f,Tone.CopyWithNewOpacity(.12f),30.f);
            Paint.Edge(Ink+1,EShape::Rounded,Back,6.f,Tone.CopyWithNewOpacity(.75f),1.f);
        }
        else
        {
            Paint.Shape(Ink+1,EShape::Rounded,Back,6.f,FFill::Linear(180.f,{{0.f,FLinearColor(1,1,1,.035f)},{1.f,FLinearColor(1,1,1,.01f)}}));
            Paint.Edge(Ink+1,EShape::Rounded,Back,6.f,FLinearColor(1,1,1,.045f),1.f);
        }
        const double RailX=Column.GetCenter().X;
        const double SocketY=Column.Min.Y+WorkshopSocketTop;
        Paint.Stroke(Ink+1,{FVector2D(RailX-5,SocketY-10),FVector2D(RailX,SocketY-4),FVector2D(RailX+5,SocketY-10)},
            LDHUDStyle::TextAccent.CopyWithNewOpacity(.60f),1.5f);
        // The head: the skill art (gold for an active, silver for the body and the dash), the body's face in the holder's
        // colour, and the numbers' pills.
        const FHead& Head=Heads[Index];
        if(!Shown(Head.Box)||!Head.IconFace){continue;}
        const FBox2D Art=Box(Head.IconFace);
        const bool bSilver=Id==LDHUD::EStorageColumn::Body||Id==LDHUD::EStorageColumn::Dash;
        Paint.SkillArt(Ink+2,Art,bSilver,Head.bEmpty,Head.Icon.Get());
        if(Id==LDHUD::EStorageColumn::Body)
        {
            const FLinearColor Holder=Latest.HolderColor;
            Paint.Shape(Ink+6,EShape::Rounded,LDUIPaint::Grow(Art,-3.f),7.f,FFill::Linear(160.f,{{0.f,FLinearColor::LerpUsingHSV(Holder,FLinearColor::White,.18f)},{1.f,Holder*.42f}}));
        }
        for(const UTextBlock* Number:{Head.Momentum,Head.Cooldown})
        {
            if(!Shown(Number)){continue;}
            const FBox2D Text=Box(Number);
            const float Mid=float(Text.GetCenter().Y);
            const FBox2D Pill=WorkshopBox(Text.Min.X-WorkshopPillLead,Mid-11.f,Text.Max.X+WorkshopPillTail,Mid+11.f);
            Paint.Pill(Ink+2,Pill);
            const FVector2D Dot(Pill.Min.X+12.f,Mid);
            if(Number==Head.Momentum){Paint.Shape(Ink+3,EShape::Circle,WorkshopBox(Dot.X-4.f,Mid-4.f,Dot.X+4.f,Mid+4.f),4.f,FFill::Solid(Hex(0x3D8BFF)));}
            else{Paint.Polygon(Ink+3,{FVector2D(Dot.X,Mid-5.f),FVector2D(Dot.X+5.f,Mid),FVector2D(Dot.X,Mid+5.f),FVector2D(Dot.X-5.f,Mid)},FFill::Solid(Hex(0xC9D2E0)));}
        }
    }
}

int32 ULDWorkshop::NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Culling,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool bEnabled) const
{
    const int32 Base=Super::NativePaint(Args,Geometry,Culling,Elements,Layer,Style,bEnabled);
    if(!IsOpen()){return Base;}
    const int32 Top=Base+1;
    const LDUIPaint::FContext Paint(Elements,Top,Geometry,Style.GetColorAndOpacityTint().A*(Backdrop?Backdrop->GetRenderOpacity():1.f));
    // A column that takes nothing of the hovered or held card dims its head (.col.off); the ones that take it light up
    // in the backdrop.
    if(Storage.IsValid()&&!Storage->Focus().Drops.IsEmpty())
    {
        const ULDComponentStorage::FFocus& Focus=Storage->Focus();
        for(int32 Index=0;Index<Heads.Num();++Index)
        {
            const LDHUD::EStorageColumn Id=static_cast<LDHUD::EStorageColumn>(Index);
            if(Focus.Drops.ContainsByPredicate([Id](const LDHUD::FCardDrop& Drop){return Drop.Column==Id;})||!WorkshopHasArea(Rects.Column[Index])){continue;}
            const FBox2D& Column=Rects.Column[Index];
            Paint.Shape(Top,LDUIShapes::EShape::Rounded,WorkshopBox(Column.Min.X,Column.Min.Y,Column.Max.X,Column.Min.Y+Rects.HeadHeight),6.f,
                LDUIPaint::FFill::Solid(LDUIPaint::Hex(0x0C1019,.7f)));
        }
    }
    const auto DrawLink=[&](const UWidget* Source,const UWidget* Target)
    {
        if(!Source||!Target||!Source->IsVisible()||!Target->IsVisible()){return;}
        const FBox2D Start=LDUIShapes::RectIn(Geometry,Source->GetCachedGeometry());
        const FBox2D End=LDUIShapes::RectIn(Geometry,Target->GetCachedGeometry());
        if(!WorkshopHasArea(Start)||!WorkshopHasArea(End)){return;}
        const FVector2D A(Start.GetCenter()),B(End.GetCenter());
        const double Mid=(A.X+B.X)*.5;
        Paint.Stroke(Top,{A,FVector2D(Mid,A.Y),FVector2D(Mid,B.Y),B},LDHUDStyle::UIAction.CopyWithNewOpacity(.65f),1.5f);
        Paint.Frame(Top,LDUIShapes::EShape::Rounded,End,LDHUDStyle::UIAction,1.5f,5.f);
    };
    if(Latest.Content&&FSlateApplication::IsInitialized())
    {
        const FVector2D Pointer=FSlateApplication::Get().GetCursorPos();
        const auto Under=[Pointer](const UWidget* Widget)
        {
            return Widget&&Widget->IsVisible()&&Widget->GetCachedGeometry().IsUnderLocation(Pointer);
        };
        for(const FReaction& Reaction:Reactions)
        {
            for(const auto& Disc:{TPair<const UWidget*,ELDDemoElement>(Reaction.FirstText?Reaction.FirstText->GetParent():nullptr,Reaction.First),
                TPair<const UWidget*,ELDDemoElement>(Reaction.SecondText?Reaction.SecondText->GetParent():nullptr,Reaction.Second)})
            {
                if(!Under(Disc.Key)){continue;}
                for(const FHead& Head:Heads)
                {
                    if(Head.Elements.Contains(Disc.Value)){DrawLink(Disc.Key,Head.IconFace);}
                }
                if(Storage.IsValid())
                {
                    for(const FLDCardInstanceView& Card:View.Cards)
                    {
                        if(!Card.IsInstalled()){continue;}
                        const FName Key=LDHUD::CardKey(Card.DefinitionId,Card.Serial);
                        const auto CardElements=LDWorkshop::CardElements(Card.DefinitionId,Latest.Content);
                        if(Card.Target.Element==Disc.Value||CardElements.Contains(Disc.Value))
                        {
                            FSlateRect Tile;
                            if(Storage->TileBox(Key,Tile))
                            {
                                const FBox2D Bounds=LDUIShapes::AbsoluteIn(Geometry,Tile);
                                Paint.Frame(Top,LDUIShapes::EShape::Rounded,Bounds,LDHUDStyle::UIAction,2.f,5.f);
                            }
                        }
                    }
                }
            }
        }
        if(Choices.IsValid())
        {
            LDHUD::FActionDetailView Detail;FName Component;
            const UWidget* Source=Choices->Hovered(Pointer,Detail,&Component);
            if(Under(Source)&&!Component.IsNone())
            {
                const FLDDemoCardDefinition* Definition=LDHUD::CardDefinitionOf(Latest.Content,Component);
                if(Definition)
                {
                    const FLDGrowthBuild Build=LDProgression::BuildFromView(*Latest.Content,View);
                    for(int32 Index=0;Index<Heads.Num();++Index)
                    {
                        const auto Column=LDHUD::EStorageColumn(Index);
                        const FHead& Head=Heads[Index];
                        FLDCardTarget Target;
                        Target.Active=Head.Active;
                        if(!Head.Active.IsNone()){Target.Kind=ELDCardTargetKind::Skill;}
                        else if(Column==LDHUD::EStorageColumn::Dash){Target.Kind=ELDCardTargetKind::Dash;}
                        else if(Column==LDHUD::EStorageColumn::Body){Target.Kind=ELDCardTargetKind::Body;}
                        if(!Definition->ChooseElements.IsEmpty()){Target.Element=Definition->ChooseElements[0];}
                        if(Target.Kind!=ELDCardTargetKind::None&&LDProgression::CardTargetReason(*Latest.Content,Build,Component,Target)==ELDGrowthReason::None)
                        {DrawLink(Source,Head.IconFace);}
                    }
                }
            }
        }
        const auto Stats=Latest.Content->StatsInDesignOrder();
        for(int32 Index=0;Index<StatRows.Num()&&Stats.IsValidIndex(Index);++Index)
        {
            const FStatRow& Row=StatRows[Index];
            if(!Under(Row.Row)){continue;}
            const auto Has=[&](ELDDemoBonusStat Stat)
            {
                return Stats[Index]->PerPoint.ContainsByPredicate([Stat](const FLDDemoStatBonus& Bonus){return Bonus.Stat==Stat;});
            };
            const bool bAD=Has(ELDDemoBonusStat::ADFlat),bAP=Has(ELDDemoBonusStat::APFlat);
            const bool bHealth=Has(ELDDemoBonusStat::MaxHPFlat);
            const bool bResist=Has(ELDDemoBonusStat::ArmorFlat)||Has(ELDDemoBonusStat::MagicResistFlat);
            const bool bHaste=Has(ELDDemoBonusStat::CooldownHasteFlat);
            for(const FHead& Head:Heads)
            {
                if(Head.Active.IsNone()){continue;}
                const auto* Definition=Latest.Content->Abilities.FindByPredicate([&](const FLDDemoAbilityDefinition& Ability)
                {return FName(*Ability.Id)==Head.Active;});
                if(!Definition){continue;}
                const auto* Variant=Definition->FindBranch(Head.Branch);
                const auto& Values=Variant?Variant->Values:Definition->Values;
                if((bHaste&&Values.CooldownMs>0)||Values.Effects.ContainsByPredicate([=](const FLDDemoEffectDefinition& Effect)
                    {
                        return (bAD&&(Effect.ADRatioBp!=0||Effect.AdaptiveRatioBp!=0))
                            ||(bAP&&(Effect.APRatioBp!=0||Effect.AdaptiveRatioBp!=0))
                            ||(bHealth&&(Effect.MaxHPRatioBp!=0||Effect.HealMissingBp!=0))
                            ||(bResist&&(Effect.ResistRatioBp!=0||Effect.ArmorRatioBp!=0));
                    }))
                {DrawLink(Row.Row,Head.IconFace);}
            }
            break;
        }
    }
    // The lines and the dock read the same owned connections; a legal held drop previews them at its prospective position.
    if(Storage.IsValid()&&Latest.Content&&WorkshopHasArea(Rects.Columns))
    {
        const ULDComponentStorage::FFocus& Focus=Storage->Focus();
        const FSlateFontInfo Font=LDUIFonts::Font(LDUIFonts::ERole::Strong,LDHUDStyle::TypeTiny);
        const LDHUD::FStorageView& Model=Storage->Model();
        const auto DrawConnections=[&](FName Self,int32 ColumnIndex,const FSlateRect& From,bool bLit)
        {
            const LDHUD::FCardConnections* Links=LinkConnections.Find(Self);
            if(!Links||!Model.Columns.IsValidIndex(ColumnIndex)||From.Right<=From.Left||From.Bottom<=From.Top){return;}
            const FBox2D Start=LDUIShapes::AbsoluteIn(Geometry,From);
            const FName HostActive=Model.Columns[ColumnIndex].Active;
            TArray<TPair<FVector2D,FString>> Ends;
            for(int32 Other=0;Other<Heads.Num()&&Other<Model.Columns.Num();++Other)
            {
                const FName Active=Model.Columns[Other].Active;
                if(Other==ColumnIndex||Active.IsNone()||!Links->Actives.Contains(Active)||!Heads[Other].IconFace){continue;}
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
                    if(OtherEntry.Keys.Contains(Self)||!Links->Cards.Contains(LDHUD::CardDefinition(OtherEntry.Card))){continue;}
                    FSlateRect To;if(!Storage->TileBox(OtherEntry.Card,To)){continue;}
                    Ends.Add(TPair<FVector2D,FString>(LDUIShapes::AbsoluteIn(Geometry,To).GetCenter(),FString()));
                }
            }
            const FLinearColor Ink=LDHUDStyle::CardConvert.CopyWithNewOpacity(bLit?1.f:.55f);
            const FVector2D A(Start.Max.X+3.f,Start.GetCenter().Y);
            for(const TPair<FVector2D,FString>& End:Ends)
            {
                const FVector2D B=End.Key;
                const float Mid=float(A.X+B.X)*.5f;
                Paint.Stroke(Top,{A,FVector2D(Mid,A.Y),FVector2D(Mid,B.Y),B},Ink,bLit?3.f:2.f);
                for(const FVector2D& Dot:{A,B}){Paint.Fill(Top+1,FBox2D(Dot-FVector2D(4,4),Dot+FVector2D(4,4)),Ink,4.f);}
                if(!End.Value.IsEmpty())
                {
                    const FVector2D Label(Mid-30.f,float(FMath::Min(A.Y,B.Y)+FMath::Abs(B.Y-A.Y)*.5f)-11.f);
                    Paint.Fill(Top+1,FBox2D(Label,Label+FVector2D(60.f,22.f)),LDHUDStyle::UIBase.CopyWithNewOpacity(.92f),11.f);
                    Paint.Frame(Top+1,LDUIShapes::EShape::Rounded,FBox2D(Label,Label+FVector2D(60.f,22.f)),LDHUDStyle::UIReplace,1.5f,11.f);
                    FSlateDrawElement::MakeText(Elements,Top+2,Geometry.ToPaintGeometry(FVector2D(60.f,22.f),FSlateLayoutTransform(Label+FVector2D(14.f,2.f))),
                        End.Value,Font,ESlateDrawEffect::None,Paint.Fade(LDHUDStyle::UIText));
                }
            }
        };
        const bool bPreview=Focus.bHeld&&Focus.bOverDrop&&LinkConnections.Contains(Focus.Component);
        for(int32 ColumnIndex=0;ColumnIndex<Model.Columns.Num();++ColumnIndex)
        {
            for(const LDHUD::FEquippedView& Entry:Model.Columns[ColumnIndex].Entries)
            {
                if(Focus.Component!=Entry.Card){continue;}
                if(bPreview&&Entry.Card==Focus.Component){continue;}
                FSlateRect From;
                if(Storage->TileBox(Entry.Card,From))
                {DrawConnections(Entry.Card,ColumnIndex,From,Focus.Component==Entry.Card);}
            }
        }
        if(bPreview)
        {
            const int32 Column=int32(Focus.Drop.Column);
            FSlateRect From;
            bool bHasFrom=Storage->HeldBox(From);
            if(!bHasFrom&&Heads.IsValidIndex(Column)&&Heads[Column].IconFace)
            {From=Heads[Column].IconFace->GetCachedGeometry().GetLayoutBoundingRect();bHasFrom=From.Right>From.Left&&From.Bottom>From.Top;}
            if(bHasFrom){DrawConnections(Focus.Component,Column,From,true);}
        }
    }
    return Top+2;
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
