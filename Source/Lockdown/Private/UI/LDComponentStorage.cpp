#include "UI/LDComponentStorage.h"
#include "UI/LDControlSettings.h"
#include "UI/LDUIPaint.h"
#include "Core/LDLog.h"
#include "UI/LDHUDStyle.h"
#include "UI/LDTerms.h"
#include "UI/LDUIIcons.h"
#include "UI/LDWidgets.h"
#include "UI/LDUIShapes.h"
#include "UI/LDWorkshopModel.h"
#include "UI/LDTween.h"
#include "UI/LDUIFonts.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/RichTextBlock.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"
#include "Engine/World.h"

namespace
{
// Sizes in Slate units at the 1080p reference, like the band (720p draws the same layout at ~0.667).
constexpr float StorageStackSize=64.f;
constexpr float StorageGapFromBand=12.f;
constexpr float StorageListWidth=270.f;
// The open list is a grid of icons, StorageListColumns a row; the popup carries the words.
constexpr int32 StorageListColumns=4;
constexpr float StorageCardGap=6.f;
constexpr float StorageColumnWidth=78.f;
constexpr float StorageTile=54.f;
static_assert(StorageListColumns*StorageTile+(StorageListColumns-1)*StorageCardGap<=StorageListWidth-20.f,"The icon grid fits the list.");
constexpr float StorageNestedTile=42.f;
constexpr float StorageNestIndent=8.f;
constexpr float StoragePopupWidth=320.f;
constexpr float StorageDragStartPx=8.f;
constexpr float StorageScreenMargin=8.f;
// The open view stays a moment after the pointer leaves it, so crossing a gap between the list and a column keeps it.
constexpr double StorageCloseGrace=.45;
// A move whose uninstall is applied but never shows in the own view gives up and says where the component is.
constexpr double StorageMoveTimeout=5.;

FSlateRoundedBoxBrush StorageRound(float Radius,const FLinearColor& Edge,float Width)
{FSlateRoundedBoxBrush Brush(FLinearColor::White,Radius,Edge,Width);Brush.OutlineSettings.bUseBrushTransparency=true;return Brush;}
void StorageText(UTextBlock* Text,const FString& Value)
{if(Text&&!Text->GetText().ToString().Equals(Value,ESearchCase::CaseSensitive)){Text->SetText(FText::FromString(Value));}}
void StorageShown(UWidget* Widget,bool bShown,ESlateVisibility Shown=ESlateVisibility::SelfHitTestInvisible)
{
    if(!Widget){return;}
    const ESlateVisibility Next=bShown?Shown:ESlateVisibility::Collapsed;
    if(Widget->GetVisibility()!=Next){Widget->SetVisibility(Next);}
}
void StorageBold(UTextBlock* Text,int32 Outline)
{
    auto Font=Text->GetFont();Font.TypefaceFontName=FName(TEXT("Bold"));
    if(Outline>0){Font.OutlineSettings.OutlineSize=Outline;Font.OutlineSettings.OutlineColor=LDHUDStyle::Rim;}
    Text->SetFont(Font);
}
bool StorageUnder(const UWidget* Widget,FVector2D At){return Widget&&Widget->IsVisible()&&Widget->GetCachedGeometry().IsUnderLocation(At);}
FSlateRect StorageRect(const UWidget* Widget){return Widget?Widget->GetCachedGeometry().GetLayoutBoundingRect():FSlateRect();}
bool StorageHasArea(const FSlateRect& Rect){return Rect.Right>Rect.Left&&Rect.Bottom>Rect.Top;}
void StoragePlace(UCanvasPanelSlot* Slot,FVector2D Position,FVector2D Alignment)
{
    if(!Slot){return;}
    const FVector2D Rounded(FMath::RoundToFloat(float(Position.X)),FMath::RoundToFloat(float(Position.Y)));
    if(!Slot->GetPosition().Equals(Rounded,.5f)){Slot->SetPosition(Rounded);}
    if(!Slot->GetAlignment().Equals(Alignment)){Slot->SetAlignment(Alignment);}
}
}

TSharedRef<SWidget> ULDComponentStorage::RebuildWidget()
{
    SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    if(!Canvas)
    {
        Canvas=WidgetTree->ConstructWidget<UCanvasPanel>();Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);WidgetTree->RootWidget=Canvas;
        {
            TilePaint=WidgetTree->ConstructWidget<ULDPaintLayer>();
            TilePaint->Painter=[this](const LDUIPaint::FContext& Paint){PaintTiles(Paint);};
            UCanvasPanelSlot* PaintSlot=Canvas->AddChildToCanvas(TilePaint);PaintSlot->SetAnchors(::FAnchors(0.f,0.f,1.f,1.f));PaintSlot->SetOffsets(FMargin(0));
            PaintSlot->SetZOrder(0);TilePaint->SetVisibility(ESlateVisibility::HitTestInvisible);
        }
        auto Line=[this](const FString& Value,int32 Size,const FLinearColor& Ink)
        {UTextBlock* Label=LDWidgets::Text(WidgetTree,Value,Size);Label->SetAutoWrapText(false);Label->SetColorAndOpacity(Ink);return Label;};
        auto Add=[this](UWidget* Widget,int32 Z)
        {UCanvasPanelSlot* Slot=Canvas->AddChildToCanvas(Widget);Slot->SetAutoSize(true);Slot->SetZOrder(Z);return Slot;};
        {
            // The stack: two card backs behind the newest stored card's icon, its count and the 新 badge, then the
            // caption. The whole stack takes the pointer, so hovering anywhere on it opens the view.
            auto* Deck=WidgetTree->ConstructWidget<UOverlay>();
            for(int32 Back=2;Back>=1;--Back)
            {
                auto* Card=WidgetTree->ConstructWidget<UBorder>();Card->SetBrush(StorageRound(6.f,LDHUDStyle::PanelRim,1.f));
                Card->SetBrushColor(LDHUDStyle::CardFill);Card->SetRenderTranslation(FVector2D(4.f*Back,-4.f*Back));
                auto* BackSlot=Deck->AddChildToOverlay(Card);BackSlot->SetHorizontalAlignment(HAlign_Fill);BackSlot->SetVerticalAlignment(VAlign_Fill);
                StackBacks.Add(Card);
            }
            UImage* Picture=nullptr;UTextBlock* Glyph=nullptr;UOverlay* Face=nullptr;USizeBox* Box=nullptr;
            StackTop=IconFace(StorageStackSize,Picture,Glyph,Face,Box);StackPicture=Picture;StackGlyph=Glyph;
            StackCount=Line(TEXT(""),LDHUDStyle::TypeSmall,FLinearColor::White);StorageBold(StackCount,2);
            auto* CountSlot=Face->AddChildToOverlay(StackCount);CountSlot->SetHorizontalAlignment(HAlign_Right);CountSlot->SetVerticalAlignment(VAlign_Bottom);
            CountSlot->SetPadding(FMargin(0,0,5,2));
            StackFresh=Line(TEXT(""),LDHUDStyle::TypeTiny,LDHUDStyle::Rim);StorageBold(StackFresh,0);
            auto* Chip=WidgetTree->ConstructWidget<UBorder>();Chip->SetBrush(StorageRound(4.f,LDHUDStyle::Rim,1.f));Chip->SetBrushColor(LDHUDStyle::TextAccent);
            Chip->SetPadding(FMargin(4,0));Chip->SetContent(StackFresh);Chip->SetRenderTranslation(FVector2D(8,-8));StackFreshChip=Chip;
            auto* ChipSlot=Face->AddChildToOverlay(Chip);ChipSlot->SetHorizontalAlignment(HAlign_Right);ChipSlot->SetVerticalAlignment(VAlign_Top);
            auto* TopSlot=Deck->AddChildToOverlay(Box);TopSlot->SetHorizontalAlignment(HAlign_Fill);TopSlot->SetVerticalAlignment(VAlign_Fill);
            auto* Column=WidgetTree->ConstructWidget<UVerticalBox>();
            Column->AddChildToVerticalBox(Deck)->SetHorizontalAlignment(HAlign_Center);
            UTextBlock* Caption=Line(LDTerms::Storage,LDHUDStyle::TypeCaption,LDHUDStyle::TextSecondary);StorageBold(Caption,1);
            auto* CaptionSlot=Column->AddChildToVerticalBox(Caption);CaptionSlot->SetHorizontalAlignment(HAlign_Center);CaptionSlot->SetPadding(FMargin(0,4,0,0));
            StackCapacity=Line(TEXT(""),LDHUDStyle::TypeTiny,LDHUDStyle::TextSecondary);StorageBold(StackCapacity,1);
            Column->AddChildToVerticalBox(StackCapacity)->SetHorizontalAlignment(HAlign_Center);
            auto* Hit=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("HUDComponentStorage"));
            Hit->SetBrushColor(FLinearColor::Transparent);Hit->SetPadding(FMargin(2,10,10,2));Hit->SetContent(Column);
            StackFrame=Hit;StackSlot=Add(Hit,1);StackFrame->SetVisibility(ESlateVisibility::Collapsed);
        }
        {
            // #328 §2: dedicated top Element Stone emerald inlay tray above the main Build Card grid in the open storage list.
            auto* Box=WidgetTree->ConstructWidget<UVerticalBox>();
            auto* StoneBox=WidgetTree->ConstructWidget<UVerticalBox>();
            UTextBlock* StoneHead=Line(TEXT("◆ 元素石镶嵌托盘"),LDHUDStyle::TypeMicro,LDHUDStyle::RewardStoneRim);
            StorageBold(StoneHead,1);
            StoneBox->AddChildToVerticalBox(StoneHead)->SetPadding(FMargin(2,0,0,4));
            StoneTrayList=WidgetTree->ConstructWidget<UWrapBox>();StoneTrayList->SetExplicitWrapSize(true);
            StoneTrayList->SetWrapSize(StorageListWidth-32.f);StoneTrayList->SetInnerSlotPadding(FVector2D(StorageCardGap,StorageCardGap));
            StoneBox->AddChildToVerticalBox(StoneTrayList);
            auto* StoneBorder=WidgetTree->ConstructWidget<UBorder>();
            StoneBorder->SetBrush(StorageRound(6.f,LDHUDStyle::RewardStoneRim.CopyWithNewOpacity(.65f),1.2f));
            StoneBorder->SetBrushColor(LDHUDStyle::RewardStoneBase.CopyWithNewOpacity(.55f));
            StoneBorder->SetPadding(FMargin(6,5));StoneBorder->SetContent(StoneBox);
            StoneBorder->SetVisibility(ESlateVisibility::Collapsed);
            StoneTraySection=StoneBorder;
            Box->AddChildToVerticalBox(StoneBorder)->SetPadding(FMargin(0,0,0,6));

            CardList=WidgetTree->ConstructWidget<UWrapBox>();CardList->SetExplicitWrapSize(true);CardList->SetWrapSize(StorageListWidth-20.f);
            CardList->SetInnerSlotPadding(FVector2D(StorageCardGap,StorageCardGap));Box->AddChildToVerticalBox(CardList);
            auto* Panel=LDWidgets::Panel(WidgetTree,Box,FMargin(10,8));Panel->SetBrushColor(LDHUDStyle::PanelFill.CopyWithNewOpacity(.9f));
            auto* Width=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("HUDComponentStorageList"));
            Width->SetWidthOverride(StorageListWidth);Width->SetContent(Panel);
            ListFrame=Width;ListSlot=Add(Width,3);ListFrame->SetVisibility(ESlateVisibility::Collapsed);
        }
        // The equipped columns, in LDHUD::EStorageColumn order: the list of tiles, the drop placeholder and, for 普攻 and 本体,
        // the title (a slot column stands on its own Q/W/E or D slot, whose key the player's bindings decide).
        for(int32 Index=0;Index<LDHUD::StorageColumnCount;++Index)
        {
            FColumn& Column=Columns.AddDefaulted_GetRef();
            auto* Box=WidgetTree->ConstructWidget<UVerticalBox>();
            Column.List=WidgetTree->ConstructWidget<UVerticalBox>();Box->AddChildToVerticalBox(Column.List)->SetHorizontalAlignment(HAlign_Center);
            Column.DenseList=WidgetTree->ConstructWidget<UWrapBox>();Column.DenseList->SetExplicitWrapSize(true);
            Column.DenseList->SetInnerSlotPadding(FVector2D(6.f,6.f));
            Box->AddChildToVerticalBox(Column.DenseList)->SetHorizontalAlignment(HAlign_Fill);
            StorageShown(Column.DenseList,false);
            UTextBlock* Here=Line(LDTerms::DropHere,LDHUDStyle::TypeTiny,LDHUDStyle::DropLegal);Here->SetJustification(ETextJustify::Center);
            auto* Placeholder=WidgetTree->ConstructWidget<UBorder>();Placeholder->SetBrush(StorageRound(6.f,LDHUDStyle::DropLegal,1.f));
            Placeholder->SetBrushColor(LDHUDStyle::DropLegal.CopyWithNewOpacity(.10f));Placeholder->SetHorizontalAlignment(HAlign_Center);
            Placeholder->SetVerticalAlignment(VAlign_Center);Placeholder->SetContent(Here);
            auto* PlaceholderBox=WidgetTree->ConstructWidget<USizeBox>();PlaceholderBox->SetWidthOverride(StorageTile);PlaceholderBox->SetHeightOverride(StorageTile*.7f);
            PlaceholderBox->SetContent(Placeholder);PlaceholderBox->SetVisibility(ESlateVisibility::Collapsed);Column.Placeholder=PlaceholderBox;
            Column.PlaceholderBox=PlaceholderBox;Column.PlaceholderFace=Placeholder;Column.PlaceholderText=Here;
            Box->AddChildToVerticalBox(PlaceholderBox)->SetHorizontalAlignment(HAlign_Center);
            Column.Title=Line(TEXT(""),LDHUDStyle::TypeCaption,LDHUDStyle::TextSecondary);StorageBold(Column.Title,1);Column.Title->SetJustification(ETextJustify::Center);
            Column.Title->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
            auto* TitleSlot=Box->AddChildToVerticalBox(Column.Title);TitleSlot->SetHorizontalAlignment(HAlign_Fill);TitleSlot->SetPadding(FMargin(0,4,0,0));
            Column.Fit=LDWidgets::Fit(WidgetTree,Box);Column.Fit->SetStretch(EStretch::None);
            if(UScaleBoxSlot* FitSlot=Cast<UScaleBoxSlot>(Box->Slot))
            {FitSlot->SetHorizontalAlignment(HAlign_Fill);FitSlot->SetVerticalAlignment(VAlign_Fill);}
            Column.Panel=WidgetTree->ConstructWidget<UBorder>();Column.Panel->SetBrush(StorageRound(8.f,LDHUDStyle::PanelRim,1.f));
            Column.Panel->SetBrushColor(LDHUDStyle::ColumnFill);Column.Panel->SetPadding(FMargin(4,6));Column.Panel->SetContent(Column.Fit);
            Column.Frame=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),FName(*FString::Printf(TEXT("HUDStorageColumn%d"),Index)));
            Column.Frame->SetWidthOverride(StorageColumnWidth);Column.Frame->SetClipping(EWidgetClipping::ClipToBounds);Column.Frame->SetContent(Column.Panel);Column.Frame->SetVisibility(ESlateVisibility::Collapsed);
            Column.Slot=Add(Column.Frame,2);
        }
        {
            // The workshop library: one group per card form, its title and count over a wrap of icon cells.
            LibraryBox=WidgetTree->ConstructWidget<UVerticalBox>();
            for(uint8 Kind=0;Kind<LDWorkshop::LibraryGroupCount;++Kind)
            {
                auto* Group=WidgetTree->ConstructWidget<UVerticalBox>();
                auto* Head=WidgetTree->ConstructWidget<UHorizontalBox>();
                // .grp: a mark (painted), the form's name spaced out, the count, and a hairline to the edge (painted).
                auto* SwatchBox=WidgetTree->ConstructWidget<USizeBox>();SwatchBox->SetWidthOverride(12.f);SwatchBox->SetHeightOverride(12.f);
                Head->AddChildToHorizontalBox(SwatchBox)->SetVerticalAlignment(VAlign_Center);LibrarySwatches.Add(SwatchBox);
                UTextBlock* Name=Line(LDTerms::CardFormName(LDWorkshop::LibraryGroupForm(Kind)),13,LDHUDStyle::TextSecondary);
                {FSlateFontInfo Font=Name->GetFont();Font.LetterSpacing=160;Name->SetFont(Font);}
                Head->AddChildToHorizontalBox(Name)->SetPadding(FMargin(10,0,0,0));
                UTextBlock* Count=Line(TEXT(""),13,LDHUDStyle::TextSecondary);Count->SetFont(LDUIFonts::Font(LDUIFonts::ERole::Number,13));
                Head->AddChildToHorizontalBox(Count)->SetPadding(FMargin(8,0,0,0));
                Group->AddChildToVerticalBox(Head);LibraryHeads.Add(Head);
                auto* Wrap=WidgetTree->ConstructWidget<UWrapBox>();Wrap->SetInnerSlotPadding(FVector2D(14.f,8.f));
                Group->AddChildToVerticalBox(Wrap)->SetPadding(FMargin(0,10,0,0));
                LibraryBox->AddChildToVerticalBox(Group)->SetPadding(FMargin(0,Kind?16.f:4.f,0,0));
                LibraryGroups.Add(Group);LibraryCounts.Add(Count);LibraryWraps.Add(Wrap);
            }
            LibraryEmpty=Line(TEXT(""),LDHUDStyle::TypeCaption,LDHUDStyle::TextSecondary);
            LibraryBox->AddChildToVerticalBox(LibraryEmpty)->SetPadding(FMargin(0,8,0,0));
            StorageShown(LibraryEmpty,false);
            auto* Width=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("WorkshopLibrary"));Width->SetContent(LibraryBox);
            Width->SetVisibility(ESlateVisibility::Collapsed);LibraryFrame=Width;LibrarySlot=Add(Width,3);
        }
        {
            // The description of the hovered card, in the slot popup's look; it takes no pointer input.
            auto* Box=WidgetTree->ConstructWidget<UVerticalBox>();
            PopupName=Line(TEXT(""),LDHUDStyle::TypeLabel,LDHUDStyle::TextPrimary);StorageBold(PopupName,0);PopupName->SetWrapTextAt(StoragePopupWidth-28.f);
            Box->AddChildToVerticalBox(PopupName);
            PopupWhere=Line(TEXT(""),LDHUDStyle::TypeCaption,LDHUDStyle::TextAccent);PopupWhere->SetWrapTextAt(StoragePopupWidth-28.f);
            Box->AddChildToVerticalBox(PopupWhere)->SetPadding(FMargin(0,2,0,0));
            PopupBody=LDWidgets::RichText(WidgetTree,LDHUDStyle::TypeSmall,TEXT("HUDStoragePopupBody"));PopupBody->SetAutoWrapText(false);PopupBody->SetWrapTextAt(StoragePopupWidth-28.f);
            Box->AddChildToVerticalBox(PopupBody)->SetPadding(FMargin(0,8,0,0));
            PopupHint=Line(TEXT(""),LDHUDStyle::TypeTiny,LDHUDStyle::TextSecondary);PopupHint->SetWrapTextAt(StoragePopupWidth-28.f);
            Box->AddChildToVerticalBox(PopupHint)->SetPadding(FMargin(0,8,0,0));
            auto* Panel=LDWidgets::Panel(WidgetTree,Box,FMargin(14,12));Panel->SetBrushColor(LDHUDStyle::OverlayFill);
            auto* Width=WidgetTree->ConstructWidget<USizeBox>();Width->SetWidthOverride(StoragePopupWidth);Width->SetContent(Panel);
            Width->SetVisibility(ESlateVisibility::Collapsed);PopupFrame=Width;PopupSlot=Add(Width,6);
        }
        {
            // The chooser a Y25 drop opens at the drop point: one button per element, then 取消.
            auto* Box=WidgetTree->ConstructWidget<UVerticalBox>();
            ChooserTitle=Line(TEXT(""),LDHUDStyle::TypeBody,LDHUDStyle::TextPrimary);StorageBold(ChooserTitle,0);ChooserTitle->SetWrapTextAt(300.f);
            Box->AddChildToVerticalBox(ChooserTitle);
            ChooserLine=WidgetTree->ConstructWidget<UHorizontalBox>();Box->AddChildToVerticalBox(ChooserLine)->SetPadding(FMargin(0,8,0,0));
            ULDCommandButton* Cancel=LDWidgets::Button(WidgetTree,LDTerms::CancelBinding,[this](){CloseChooser();},true,TEXT("HUDStorageChooserCancel"));
            Box->AddChildToVerticalBox(Cancel)->SetPadding(FMargin(0,8,0,0));
            auto* Panel=LDWidgets::Panel(WidgetTree,Box,FMargin(12,10));
            Panel->SetVisibility(ESlateVisibility::Collapsed);ChooserFrame=Panel;ChooserSlot=Add(Panel,7);
        }
        {
            // Zero-drag quick capsules floating right above a clicked/right-clicked stored card (#331).
            QuickLine=WidgetTree->ConstructWidget<UHorizontalBox>();
            auto* Panel=LDWidgets::Panel(WidgetTree,QuickLine,FMargin(8,6));
            Panel->SetBrushColor(LDHUDStyle::OverlayFill);
            Panel->SetVisibility(ESlateVisibility::Collapsed);QuickFrame=Panel;QuickSlot=Add(Panel,7);
            ReplacePromptText=Line(LDTerms::ReplacePickPrompt,LDHUDStyle::TypeBody,LDHUDStyle::UIReplace);
            StorageBold(ReplacePromptText,1);
            auto* PromptPanel=LDWidgets::Panel(WidgetTree,ReplacePromptText,FMargin(14,7));
            PromptPanel->SetBrush(StorageRound(6.f,LDHUDStyle::UIReplace,1.5f));
            PromptPanel->SetBrushColor(LDHUDStyle::OverlayFill);
            PromptPanel->SetVisibility(ESlateVisibility::Collapsed);
            ReplacePromptFrame=PromptPanel;ReplacePromptSlot=Add(PromptPanel,7);
        }
        {
            // The dragged card's ghost follows the pointer; it takes no input.
            UImage* Picture=nullptr;UTextBlock* Glyph=nullptr;UOverlay* Face=nullptr;USizeBox* Box=nullptr;
            UBorder* Ghost=IconFace(StorageTile,Picture,Glyph,Face,Box);Ghost->SetBrush(StorageRound(6.f,LDHUDStyle::TextAccent,2.f));
            GhostPicture=Picture;GhostGlyph=Glyph;Box->SetRenderOpacity(.85f);Box->SetVisibility(ESlateVisibility::Collapsed);
            GhostFrame=Box;GhostSlot=Add(Box,8);GhostSlot->SetAlignment(FVector2D(.5f,.5f));
            GhostCaption=Line(TEXT(""),LDHUDStyle::TypeCaption,LDHUDStyle::TextPrimary);GhostCaption->SetAutoWrapText(false);
            GhostCaption->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
            auto* Panel=LDWidgets::Panel(WidgetTree,GhostCaption,FMargin(8,4));Panel->SetBrushColor(LDHUDStyle::OverlayFill);
            Panel->SetVisibility(ESlateVisibility::Collapsed);GhostCaptionFrame=Panel;GhostCaptionSlot=Add(Panel,8);
        }
    }
    return Super::RebuildWidget();
}
UBorder* ULDComponentStorage::IconFace(float Size,UImage*& OutPicture,UTextBlock*& OutGlyph,UOverlay*& OutStack,USizeBox*& OutBox)
{
    OutPicture=WidgetTree->ConstructWidget<UImage>();OutPicture->SetVisibility(ESlateVisibility::Collapsed);
    OutGlyph=LDWidgets::Text(WidgetTree,TEXT(""),Size>=StorageTile?22:16);OutGlyph->SetAutoWrapText(false);OutGlyph->SetJustification(ETextJustify::Center);
    StorageBold(OutGlyph,2);
    OutStack=WidgetTree->ConstructWidget<UOverlay>();
    auto* PictureSlot=OutStack->AddChildToOverlay(OutPicture);PictureSlot->SetHorizontalAlignment(HAlign_Fill);PictureSlot->SetVerticalAlignment(VAlign_Fill);
    auto* GlyphSlot=OutStack->AddChildToOverlay(OutGlyph);GlyphSlot->SetHorizontalAlignment(HAlign_Center);GlyphSlot->SetVerticalAlignment(VAlign_Center);
    auto* Face=WidgetTree->ConstructWidget<UBorder>();Face->SetBrush(StorageRound(6.f,LDHUDStyle::IconRim,1.f));Face->SetBrushColor(LDHUDStyle::IconFill);
    Face->SetPadding(FMargin(0));Face->SetHorizontalAlignment(HAlign_Fill);Face->SetVerticalAlignment(VAlign_Fill);Face->SetContent(OutStack);
    OutBox=WidgetTree->ConstructWidget<USizeBox>();OutBox->SetWidthOverride(Size);OutBox->SetHeightOverride(Size);OutBox->SetContent(Face);
    return Face;
}
void ULDComponentStorage::ShowIcon(UImage* Picture,UTextBlock* Glyph,FName Component,float Radius,bool bDim) const
{
    // An empty stack shows a blank face: there is no card to name.
    if(Component.IsNone()){StorageText(Glyph,FString());StorageShown(Picture,false);return;}
    const LDHUD::FCardFaceView View=LDHUD::CardFace(Component,LatestContent,LatestStats);
    StorageText(Glyph,View.Glyph);
    const bool bPicture=LDUIIcons::Apply(Picture,View.IconId,Radius,bDim?LDHUDStyle::IconBlocked:FLinearColor::White);
    StorageShown(Glyph,!bPicture,ESlateVisibility::HitTestInvisible);
}
FString ULDComponentStorage::ComponentName(FName Component) const
{
    return LDHUD::ContentName(LatestContent,LDHUD::CardDefinition(Component).ToString());
}

void ULDComponentStorage::Present(const FLDProgressionView* View,uint32 Match,LDProgression::EMatchStage Stage,ELDLifePhase LifePhase,
    const FLDDemoContentDefinitions* Content,const LDHUD::FCardStats& Stats,const FAnchors& InAnchors,bool bShown,
    const LDHUD::FSlotKeys& InKeys)
{
    if(!Canvas){return;}
    // Another match's answers never reach this one: its request, move and interaction end with it.
    if(Match!=LatestMatch){PendingRequest=0;Follow=FFollowUp();CancelInteraction();SeenComponents.Reset();}
    LatestMatch=Match;
    Latest=View?*View:FLDProgressionView();
    Gate=LDHUD::BuildGate(Stage,LifePhase);
    LatestContent=Content;LatestStats=Stats;SlotKeys=InKeys;Anchors=InAnchors;
    bShownNow=bShown&&Latest.bValid&&(Workshop.bActive||Anchors.bValid);
    if(!bShownNow){CancelInteraction();Press=FPress();bExpanded=false;}
    StorageShown(StackFrame,bShownNow&&!Workshop.bActive,ESlateVisibility::Visible);
    const LDHUD::FStorageView Model=LDHUD::CardStorage(Latest,Content,&SeenComponents);
    LatestModel=Model;
    {
        // New owned cards fly into the stack (the workshop shows them with their yellow dot instead); a new match only takes
        // its first view as the baseline.
        TSet<FName> Owned;for(const FLDCardInstanceView& Each:Latest.Cards){Owned.Add(LDHUD::CardKey(Each.DefinitionId,Each.Serial));}
        if(KnownMatch==Match&&Latest.bValid)
        {
            for(const FLDCardInstanceView& Each:Latest.Cards)
            {
                const FName Key=LDHUD::CardKey(Each.DefinitionId,Each.Serial);
                if(KnownOwned.Contains(Key)||Each.IsInstalled()||Workshop.bActive){continue;}
                FFly& Fly=Flies.AddDefaulted_GetRef();Fly.Component=Key;Fly.At=UiNow();Fly.bStrip=!bEngaged;
                LDWidgets::PlayCue(this,ELDAudioCue::UIGain);
            }
        }
        if(Latest.bValid){KnownOwned=MoveTemp(Owned);KnownMatch=Match;}
        Flies.RemoveAll([this](const FFly& Fly){return UiNow()-Fly.At>LDHUDStyle::MotionGain+1.5;});
    }
    // The cards and tiles are rebuilt only for another build, catalog or numbers.
    FString Key=FString::Printf(TEXT("%u|%llu|%p|%u|%lld|%lld|%lld|%lld"),Match,Latest.BuildRevision,Content,uint32(Stats.Level),
        int64(Stats.ADP2),int64(Stats.APP2),int64(Stats.MaxHPP2),int64(Stats.ArmorP2));
    Key+=FString::Printf(TEXT("|S%u"),ContributionSerial);
    for(const LDHUD::FStoredView& Stored:Model.Stored){Key+=FString::Printf(TEXT("|%s*%d%d"),*Stored.Card.ToString(),Stored.Copies,int32(Stored.bFresh));}
    if(Workshop.bActive)
    {
        Key+=FString::Printf(TEXT("|W%.2f|%.2f|%.2f|%.2f|%.2f|%d|%d"),Workshop.LibraryIcon>0.f?Workshop.LibraryIcon:LDWorkshop::LibraryTier(Model.Stored.Num()).Icon,
            Workshop.TileScale,Workshop.MinIcon,Workshop.TileIndent,Workshop.LibraryLabelHeight,int32(Workshop.bNames),int32(Workshop.bCompactLibrary));
        Key+=FString::Printf(TEXT("|F%d|B%d,%d,%d,%d|N%d,%d,%d,%d"),Workshop.TagFilter,
            Workshop.LibraryFirst.X,Workshop.LibraryFirst.Y,Workshop.LibraryFirst.Z,Workshop.LibraryFirst.W,
            Workshop.LibraryCount.X,Workshop.LibraryCount.Y,Workshop.LibraryCount.Z,Workshop.LibraryCount.W);
        for(const LDHUD::FStorageColumnView& Column:Model.Columns){Key+=FString::Printf(TEXT("|%d"),Column.Entries.Num()>5?1:0);}
    }
    for(const LDHUD::FStorageColumnView& Column:Model.Columns)
    {
        Key+=TEXT("#")+Column.Active.ToString();
        for(const LDHUD::FEquippedView& Entry:Column.Entries){Key+=FString::Printf(TEXT("|%s*%d%s"),*Entry.Card.ToString(),Entry.Copies,*Entry.Note);}
    }
    if(Key!=BuiltKey){BuiltKey=Key;Rebuild(Model);}
    // The stack face: the newest stored card on top, the stored card count and the new-card badge.
    const FName Top=Model.Stored.IsEmpty()?NAME_None:Model.Stored.Last().Card;
    ShowIcon(StackPicture,StackGlyph,Top);
    StorageText(StackCount,FString::FromInt(Model.StoredCards()));
    StorageText(StackCapacity,Model.Capacity>0?LDTerms::WorkshopCaption(Model.Installed,Model.Capacity):FString());
    // A full workshop reads in the accent colour: the next install needs a removal first.
    if(StackCapacity){StackCapacity->SetColorAndOpacity(Model.Capacity>0&&Model.Installed>=Model.Capacity?LDHUDStyle::TextAccent:LDHUDStyle::TextSecondary);}
    StorageText(StackFresh,LDTerms::StorageFresh(Model.Fresh));
    StorageShown(StackFreshChip,Model.Fresh>0,ESlateVisibility::HitTestInvisible);
    for(int32 Back=0;Back<StackBacks.Num();++Back){StorageShown(StackBacks[Back],Model.StoredCards()>Back+1,ESlateVisibility::HitTestInvisible);}
    StackTop->SetRenderOpacity(Model.Stored.IsEmpty()?.45f:1.f);
    // A move's install half follows once the own view shows its uninstall; a view that never does gives up.
    if(Follow.State==EFollow::AwaitView&&!PendingRequest)
    {
        const double Now=GetWorld()?GetWorld()->GetRealTimeSeconds():0;
        if(Latest.BuildRevision>=Follow.AfterRevision)
        {
            const FLDBuildCommand Install=Follow.Install;PendingComponent=Follow.Component;Follow=FFollowUp();
            Send(Install);
        }
        else if(Now-Follow.Since>StorageMoveTimeout)
        {
            if(Notify){Notify(FString::Printf(TEXT("%s：没有移动完，%s 在牌库里"),LDTerms::Storage,*ComponentName(Follow.Component)));}
            Follow=FFollowUp();
        }
    }
}
void ULDComponentStorage::Rebuild(const LDHUD::FStorageView& Model)
{
    Built.Reset();Cards.Reset();Tiles.Reset();HoverDropsOf=NAME_None;HoverDrops.Reset();
    CardList->ClearChildren();
    if(StoneTrayList){StoneTrayList->ClearChildren();}
    for(int32 Kind=0;Kind<LibraryWraps.Num();++Kind)
    {
        UWrapBox* Wrap=LibraryWraps[Kind];Wrap->ClearChildren();
        const bool bCompact=Workshop.bActive&&Workshop.bCompactLibrary;
        Wrap->SetInnerSlotPadding(bCompact?FVector2D(4.f,3.f):FVector2D(14.f,8.f));
        if(UVerticalBoxSlot* WrapSlot=Cast<UVerticalBoxSlot>(Wrap->Slot)){WrapSlot->SetPadding(FMargin(0,bCompact?4.f:10.f,0,0));}
        if(LibraryGroups.IsValidIndex(Kind))
        {
            if(UVerticalBoxSlot* GroupSlot=Cast<UVerticalBoxSlot>(LibraryGroups[Kind]->Slot))
            {GroupSlot->SetPadding(FMargin(0,Kind?(bCompact?6.f:16.f):(bCompact?2.f:4.f),0,0));}
        }
    }
    const float LibraryIcon=Workshop.LibraryIcon>0.f?Workshop.LibraryIcon:LDWorkshop::LibraryTier(Model.Stored.Num()).Icon;
    int32 KindCounts[LDWorkshop::LibraryGroupCount]={0,0,0,0};
    int32 ShownCounts[LDWorkshop::LibraryGroupCount]={0,0,0,0};
    const auto AddCard=[this,LibraryIcon,&ShownCounts,&Model](FName Id)
    {
        const LDHUD::FStoredView* Stored=Model.Stored.FindByPredicate([Id](const LDHUD::FStoredView& Row){return Row.Card==Id;});
        const bool bFresh=Stored&&Stored->bFresh;
        const int32 Copies=Stored?Stored->Copies:1;
        const LDWorkshop::FCardFacts Facts=LDWorkshop::CardFacts(Id,LatestContent);
        if(Workshop.bActive){BuildWorkshopCard(Id,bFresh,LibraryIcon,LDWorkshop::LibraryGroupOf(Facts.Form),Copies);}else{BuildCard(Id,bFresh,Copies);}
        FCard& Card=Cards.Last();
        Card.Kind=uint8(Facts.Type);Card.bFresh=bFresh;
        Card.Icon=LDUIIcons::Find(LDHUD::ContentIconId(LatestContent,LDHUD::CardDefinition(Id).ToString()));
        // A card with no place now dims and loses its colour; the dock says what it needs.
        Card.bPlaceless=LDHUD::CardDrops(Id,Latest,LatestContent).IsEmpty()&&LDHUD::ReplaceDrops(Id,Latest,LatestContent).IsEmpty();
        StyleFace(Card.Face,Id,Card.Kind,Workshop.bActive?LibraryIcon:StorageTile,Card.bPlaceless);
        const int32 Group=LDWorkshop::LibraryGroupOf(Facts.Form);
        if(Group>=0&&Group<LDWorkshop::LibraryGroupCount){++ShownCounts[Group];}
    };
    if(Workshop.bActive)
    {
        const LDWorkshop::FLibraryGroups Groups=LDWorkshop::CollectLibrary(Model,LatestContent,Workshop.TagFilter);
        for(int32 Kind=0;Kind<LDWorkshop::LibraryGroupCount;++Kind)
        {
            const TArray<FName>& Items=Groups.Items[Kind];
            KindCounts[Kind]=Items.Num();
            const int32 First=FMath::Clamp(Workshop.LibraryFirst[Kind],0,Items.Num());
            const int32 End=FMath::Clamp(First+Workshop.LibraryCount[Kind],First,Items.Num());
            for(int32 Index=First;Index<End;++Index){AddCard(Items[Index]);}
        }
    }
    else
    {
        for(const LDHUD::FStoredView& Stored:Model.Stored){AddCard(Stored.Card);}
        for(int32 Kind=0;Kind<LDWorkshop::LibraryGroupCount;++Kind){KindCounts[Kind]=ShownCounts[Kind];}
    }
    StorageShown(StoneTraySection,!Workshop.bActive&&StoneTrayList&&StoneTrayList->GetChildrenCount()>0);
    for(int32 Kind=0;Kind<LibraryCounts.Num();++Kind)
    {
        StorageText(LibraryCounts[Kind],FString::FromInt(KindCounts[Kind]));
        StorageShown(LibraryGroups[Kind],ShownCounts[Kind]>0);
    }
    const bool bEmpty=KindCounts[0]+KindCounts[1]+KindCounts[2]+KindCounts[3]==0;
    StorageText(LibraryEmpty,Workshop.TagFilter==INDEX_NONE?LDTerms::StorageEmpty:LDTerms::StorageNoMatches);
    StorageShown(LibraryEmpty,Workshop.bActive&&bEmpty);
    for(int32 Index=0;Index<Columns.Num()&&Model.Columns.IsValidIndex(Index);++Index)
    {
        FColumn& Column=Columns[Index];Column.List->ClearChildren();Column.DenseList->ClearChildren();Column.AppliedLook=-1;
        const LDHUD::FStorageColumnView& Shown=Model.Columns[Index];
        Column.bDense=Workshop.bActive&&Shown.Entries.Num()>5;
        StorageShown(Column.List,!Column.bDense);
        StorageShown(Column.DenseList,Column.bDense);
        // Only 本体 shows its title; the others keep theirs (the active's name or 冲刺) for the notices that name a place.
        StorageText(Column.Title,Shown.Title);Column.Active=Shown.Active;
        StorageShown(Column.Title,Index==int32(LDHUD::EStorageColumn::Body),ESlateVisibility::HitTestInvisible);
        // Workshop: more than five rows in a column shrink its icons, so the board never scrolls.
        const float Base=FMath::Max((Shown.Entries.Num()>5?46.f:56.f)*Workshop.TileScale,Workshop.bActive?Workshop.MinIcon:0.f);
        const auto AddEntry=[&](const LDHUD::FEquippedView& Entry)
        {
            const float TileSize=Workshop.bActive?Base:StorageTile;
            if(Workshop.bActive){BuildWorkshopTile(Column,Entry,static_cast<LDHUD::EStorageColumn>(Index),TileSize);}
            else{BuildTile(Column,Entry,static_cast<LDHUD::EStorageColumn>(Index));}
            FTile& Tile=Tiles.Last();
            const LDWorkshop::FCardFacts Facts=LDWorkshop::CardFacts(Entry.Card,LatestContent);
            Tile.Kind=uint8(Facts.Type);Tile.Copies=Entry.Copies;Tile.MedalTier=Entry.MedalTier;
            Tile.Icon=LDUIIcons::Find(LDHUD::ContentIconId(LatestContent,LDHUD::CardDefinition(Entry.Card).ToString()));
            StyleFace(Tile.Face,Entry.Card,Tile.Kind,TileSize,false);
        };
        // The element slot heads the column: its medal, or (on the workshop board) an empty round slot.
        if(Shown.bHasMedal){AddEntry(Shown.Medal);}
        else if(Shown.bMedalSlot&&Workshop.bActive){BuildMedalSlot(Column,Base);}
        for(const LDHUD::FEquippedView& Entry:Shown.Entries){AddEntry(Entry);}
        // The empty place at the end of a column is the size of its icons.
        const float Empty=Workshop.bActive?Base:StorageTile;
        Column.PlaceholderBox->SetWidthOverride(Empty);Column.PlaceholderBox->SetHeightOverride(Workshop.bActive?Empty:StorageTile*.7f);
        StorageShown(Column.PlaceholderText,!Workshop.bActive,ESlateVisibility::HitTestInvisible);
        Column.PlaceholderFace->SetBrushColor(Workshop.bActive?FLinearColor::Transparent:LDHUDStyle::DropLegal.CopyWithNewOpacity(.10f));
        Column.PlaceholderFace->SetBrush(StorageRound(6.f,Workshop.bActive?FLinearColor::Transparent:LDHUDStyle::DropLegal,Workshop.bActive?0.f:1.f));
    }
    ScaleBuiltText();
}
void ULDComponentStorage::ScaleBuiltText()
{
    if(FMath::IsNearlyEqual(TextScale,1.f)){return;}
    for(UWidget* Widget:Built)
    {if(auto* Text=Cast<UTextBlock>(Widget)){FSlateFontInfo Font=Text->GetFont();Font.Size=FMath::Max(1.f,FMath::RoundToFloat(float(Font.Size)*TextScale));Text->SetFont(Font);}}
}
void ULDComponentStorage::StyleFace(UBorder* Face,FName Component,uint8 Kind,float Size,bool bDim) const
{
    // The tile (frame, face, icon and state) is painted under the face by PaintTiles (LDUIPaint::Tile); the face keeps
    // only the glyph that stands in for a missing picture.
    if(!Face){return;}
    Face->SetBrush(FSlateNoResource());
    if(UOverlay* Stack=Cast<UOverlay>(Face->GetContent()))
    {
        UImage* Picture=Cast<UImage>(Stack->GetChildAt(0));
        ShowIcon(Picture,Cast<UTextBlock>(Stack->GetChildAt(1)),Component,FaceRadius(Kind,Size),bDim);
        StorageShown(Picture,false);
    }
}
float ULDComponentStorage::FaceRadius(uint8 Kind,float Size) const
{
    return LDUIPaint::TileRadius(Kind,Size);
}
double ULDComponentStorage::UiNow() const
{
    return GetWorld()?GetWorld()->GetRealTimeSeconds():0.;
}
void ULDComponentStorage::AddCopiesBadge(UOverlay* Face,int32 Copies)
{
    // ×n: copies of one card in one place share the face.
    if(!Face||Copies<2){return;}
    UTextBlock* Badge=LDWidgets::Text(WidgetTree,LDTerms::CopiesBadge(Copies),LDHUDStyle::TypeTiny);Badge->SetAutoWrapText(false);
    StorageBold(Badge,2);Badge->SetColorAndOpacity(FLinearColor::White);Built.Add(Badge);
    auto* BadgeSlot=Face->AddChildToOverlay(Badge);BadgeSlot->SetHorizontalAlignment(HAlign_Left);BadgeSlot->SetVerticalAlignment(VAlign_Top);
    BadgeSlot->SetPadding(FMargin(3,1,0,0));
}
void ULDComponentStorage::BuildWorkshopCard(FName Component,bool bFresh,float Size,int32 Group,int32 Copies)
{
    // A library cell: the icon in its type's shape over its name; hovering it lights the places it takes.
    const uint8 Kind=uint8(LDWorkshop::CardFacts(Component,LatestContent).Type);
    UImage* Picture=nullptr;UTextBlock* Glyph=nullptr;UOverlay* Face=nullptr;USizeBox* Box=nullptr;
    UBorder* Icon=IconFace(Size,Picture,Glyph,Face,Box);Built.Add(Box);ShowIcon(Picture,Glyph,Component,FaceRadius(Kind,Size));
    Icon->SetBrush(StorageRound(FaceRadius(Kind,Size),FLinearColor::Transparent,0.f));Icon->SetBrushColor(LDHUDStyle::IconFill);
    Icon->SetVisibility(ESlateVisibility::Visible);
    AddCopiesBadge(Face,Copies);
    auto* Cell=WidgetTree->ConstructWidget<UVerticalBox>();Built.Add(Cell);
    Cell->AddChildToVerticalBox(Box)->SetHorizontalAlignment(HAlign_Center);
    // .cell .t: the name under the icon, two lines at most.
    UTextBlock* Name=LDWidgets::Text(WidgetTree,ComponentName(Component),12);Name->SetAutoWrapText(false);Name->SetWrapTextAt(Size+10.f);Built.Add(Name);
    Name->SetJustification(ETextJustify::Center);Name->SetColorAndOpacity(LDHUDStyle::TextSecondary);Name->SetLineHeightPercentage(1.05f);
    UScaleBox* NameFit=LDWidgets::Fit(WidgetTree,Name);Built.Add(NameFit);
    auto* NameBox=WidgetTree->ConstructWidget<USizeBox>();NameBox->SetWidthOverride(Size+10.f);
    NameBox->SetHeightOverride(Workshop.LibraryLabelHeight);NameBox->SetContent(NameFit);Built.Add(NameBox);
    Cell->AddChildToVerticalBox(NameBox)->SetPadding(FMargin(0,Workshop.bCompactLibrary?3.f:6.f,0,0));
    FCard& Card=Cards.AddDefaulted_GetRef();Card.Component=Component;Card.Face=Icon;Card.Size=Box;Card.Cell=Cell;Card.NameFit=NameFit;
    if(LibraryWraps.IsValidIndex(Group)){LibraryWraps[Group]->AddChildToWrapBox(Cell);}
}
void ULDComponentStorage::BuildWorkshopTile(FColumn& Column,const LDHUD::FEquippedView& Entry,LDHUD::EStorageColumn ColumnId,float Size)
{
    // A board row: the icon in its type's shape and the name beside it.
    const uint8 Kind=uint8(LDWorkshop::CardFacts(Entry.Card,LatestContent).Type);
    UImage* Picture=nullptr;UTextBlock* Glyph=nullptr;UOverlay* Face=nullptr;USizeBox* Box=nullptr;
    UBorder* Icon=IconFace(Size,Picture,Glyph,Face,Box);Built.Add(Box);ShowIcon(Picture,Glyph,Entry.Card,FaceRadius(Kind,Size));
    Icon->SetBrush(StorageRound(FaceRadius(Kind,Size),FLinearColor::Transparent,0.f));Icon->SetBrushColor(LDHUDStyle::IconFill);
    Icon->SetVisibility(ESlateVisibility::Visible);
    AddCopiesBadge(Face,Entry.Copies);
    if(const FString* Did=Contributions.Find(LDHUD::CardDefinition(Entry.Card)))
    {
        // What this card did so far this match, on the top-right corner.
        UTextBlock* Done=LDWidgets::Text(WidgetTree,*Did,LDHUDStyle::TypeMicro);Done->SetAutoWrapText(false);StorageBold(Done,2);Built.Add(Done);
        Done->SetColorAndOpacity(LDHUDStyle::TextAccent);
        auto* DoneSlot=Face->AddChildToOverlay(Done);DoneSlot->SetHorizontalAlignment(HAlign_Right);DoneSlot->SetVerticalAlignment(VAlign_Top);
        DoneSlot->SetPadding(FMargin(0,1,3,0));
    }
    if(!Entry.Note.IsEmpty())
    {
        UTextBlock* Note=LDWidgets::Text(WidgetTree,Entry.Note,LDHUDStyle::TypeMicro);Note->SetAutoWrapText(false);StorageBold(Note,2);Built.Add(Note);
        if(Entry.MedalTier>=3){Note->SetColorAndOpacity(FLinearColor(1.f,.95f,.55f));}
        else if(Entry.MedalTier==2){Note->SetColorAndOpacity(LDHUDStyle::TextAccent);}
        auto* NoteSlot=Face->AddChildToOverlay(Note);NoteSlot->SetHorizontalAlignment(HAlign_Right);NoteSlot->SetVerticalAlignment(VAlign_Bottom);
        NoteSlot->SetPadding(FMargin(2,0,3,1));
    }
    if(Column.bDense)
    {
        Column.DenseList->AddChildToWrapBox(Box);
        FTile& Tile=Tiles.AddDefaulted_GetRef();Tile.Component=Entry.Card;Tile.Column=ColumnId;Tile.Face=Icon;
        return;
    }
    auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();Built.Add(Row);
    Row->AddChildToHorizontalBox(Box)->SetVerticalAlignment(VAlign_Center);
    if(Workshop.bNames)
    {
        // .sock .t: the name beside the tile, two lines at most.
        UTextBlock* Name=LDWidgets::Text(WidgetTree,ComponentName(Entry.Card),14);Name->SetAutoWrapText(true);Built.Add(Name);
        Name->SetColorAndOpacity(LDHUDStyle::UIText);Name->SetLineHeightPercentage(1.05f);
        auto* NameSlot=Row->AddChildToHorizontalBox(Name);NameSlot->SetVerticalAlignment(VAlign_Center);NameSlot->SetPadding(FMargin(10,0,0,0));
        NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    }
    auto* RowSlot=Column.List->AddChildToVerticalBox(Row);
    RowSlot->SetPadding(FMargin(0,Column.List->GetChildrenCount()>1?14.f*Workshop.TileScale:0.f,0,0));
    RowSlot->SetHorizontalAlignment(HAlign_Fill);
    FTile& Tile=Tiles.AddDefaulted_GetRef();Tile.Component=Entry.Card;Tile.Column=ColumnId;Tile.Face=Icon;
}
void ULDComponentStorage::BuildCard(FName Component,bool bFresh,int32 Copies)
{
    // A stored card is its icon alone, with 新 on the corner while it is new; hovering it shows the popup.
    UImage* Picture=nullptr;UTextBlock* Glyph=nullptr;UOverlay* Face=nullptr;USizeBox* Box=nullptr;
    UBorder* Icon=IconFace(StorageTile,Picture,Glyph,Face,Box);Built.Add(Box);
    // The face takes its card type's cut and colour, as in the workshop: an element medal is round, a gem in its element ring.
    const uint8 Kind=uint8(LDWorkshop::CardFacts(Component,LatestContent).Type);
    ShowIcon(Picture,Glyph,Component,FaceRadius(Kind,StorageTile));
    Icon->SetBrush(StorageRound(FaceRadius(Kind,StorageTile),LDUIShapes::KindColor(Kind),Kind==uint8(ELDCardType::Element)?2.5f:1.5f));
    Icon->SetBrushColor(LDHUDStyle::IconFill);
    Icon->SetVisibility(ESlateVisibility::Visible);
    AddCopiesBadge(Face,Copies);
    UWidget* FreshChip=nullptr;
    if(bFresh)
    {
        UTextBlock* Fresh=LDWidgets::Text(WidgetTree,TEXT("新"),LDHUDStyle::TypeMicro);Fresh->SetAutoWrapText(false);Fresh->SetColorAndOpacity(LDHUDStyle::Rim);
        StorageBold(Fresh,0);Built.Add(Fresh);
        auto* Chip=WidgetTree->ConstructWidget<UBorder>();Built.Add(Chip);Chip->SetBrush(StorageRound(4.f,LDHUDStyle::Rim,1.f));
        Chip->SetBrushColor(LDHUDStyle::TextAccent);Chip->SetPadding(FMargin(3,0));Chip->SetContent(Fresh);Chip->SetVisibility(ESlateVisibility::HitTestInvisible);
        FreshChip=Chip;
        auto* ChipSlot=Face->AddChildToOverlay(Chip);ChipSlot->SetHorizontalAlignment(HAlign_Right);ChipSlot->SetVerticalAlignment(VAlign_Top);
    }
    FCard& Card=Cards.AddDefaulted_GetRef();Card.Component=Component;Card.Face=Icon;Card.Size=Box;Card.FreshChip=FreshChip;
    const bool bStone=LDWorkshop::CardFacts(Component,LatestContent).Form==ELDCardForm::Medal||LDHUD::CardDefinition(Component).ToString().StartsWith(TEXT("medal_"));
    if(bStone&&StoneTrayList){StoneTrayList->AddChildToWrapBox(Box);}
    else{CardList->AddChildToWrapBox(Box);}
}
void ULDComponentStorage::MarkSeen(FName Component)
{
    if(Component.IsNone()||!LatestMatch||SeenComponents.Contains(Component)){return;}
    // Looking at a stored face marks every copy it stands for.
    for(const LDHUD::FStoredView& Stored:LatestModel.Stored)
    {
        if(Stored.Keys.Contains(Component)){for(const FName& Key:Stored.Keys){SeenComponents.Add(Key);}}
    }
    SeenComponents.Add(Component);
    for(FCard& Card:Cards){if(Card.Component==Component){StorageShown(Card.FreshChip,false);Card.bFresh=false;}}
    const int32 Fresh=LDHUD::CardStorage(Latest,LatestContent,&SeenComponents).Fresh;
    StorageText(StackFresh,LDTerms::StorageFresh(Fresh));
    StorageShown(StackFreshChip,Fresh>0,ESlateVisibility::HitTestInvisible);
}
void ULDComponentStorage::SetDetailsHeld(bool bHeld)
{
    if(bDetailsHeld==bHeld){return;}
    bDetailsHeld=bHeld;
    if(!bHeld&&!bPinned&&!Drag.bActive&&!Press.bArmed&&!Chooser.bOpen){bExpanded=false;}
}
void ULDComponentStorage::BuildMedalSlot(FColumn& Column,float Size)
{
    // An empty element slot: a round outline the size of a tile, named 元素槽 beside it on the board.
    auto* Box=WidgetTree->ConstructWidget<USizeBox>();Box->SetWidthOverride(Size);Box->SetHeightOverride(Size);Built.Add(Box);
    auto* Ring=WidgetTree->ConstructWidget<UBorder>();Built.Add(Ring);
    // A clear fill with its own outline opacity: the rim stays visible while the face shows through.
    FSlateRoundedBoxBrush RingBrush(FLinearColor::Transparent,Size*.5f,LDHUDStyle::DropLegal.CopyWithNewOpacity(.55f),1.5f);
    RingBrush.OutlineSettings.bUseBrushTransparency=false;
    Ring->SetBrush(RingBrush);Ring->SetHorizontalAlignment(HAlign_Center);Ring->SetVerticalAlignment(VAlign_Center);
    UTextBlock* Glyph=LDWidgets::Text(WidgetTree,TEXT("+"),LDHUDStyle::TypeBody);Glyph->SetAutoWrapText(false);Built.Add(Glyph);
    Glyph->SetColorAndOpacity(LDHUDStyle::TextSecondary);Ring->SetContent(Glyph);
    Box->SetContent(Ring);Box->SetVisibility(ESlateVisibility::HitTestInvisible);
    if(Column.bDense){Column.DenseList->AddChildToWrapBox(Box);return;}
    auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();Built.Add(Row);
    Row->AddChildToHorizontalBox(Box)->SetVerticalAlignment(VAlign_Center);
    if(Workshop.bNames)
    {
        UTextBlock* Name=LDWidgets::Text(WidgetTree,LDTerms::MedalSlot,14);Name->SetAutoWrapText(false);Built.Add(Name);
        Name->SetColorAndOpacity(LDHUDStyle::TextSecondary);
        auto* NameSlot=Row->AddChildToHorizontalBox(Name);NameSlot->SetVerticalAlignment(VAlign_Center);NameSlot->SetPadding(FMargin(10,0,0,0));
    }
    auto* RowSlot=Column.List->AddChildToVerticalBox(Row);
    RowSlot->SetPadding(FMargin(0,Column.List->GetChildrenCount()>1?14.f*Workshop.TileScale:0.f,0,0));
    RowSlot->SetHorizontalAlignment(HAlign_Fill);
}
void ULDComponentStorage::BuildTile(FColumn& Column,const LDHUD::FEquippedView& Entry,LDHUD::EStorageColumn ColumnId)
{
    UImage* Picture=nullptr;UTextBlock* Glyph=nullptr;UOverlay* Face=nullptr;USizeBox* Box=nullptr;
    UBorder* Icon=IconFace(StorageTile,Picture,Glyph,Face,Box);Built.Add(Box);ShowIcon(Picture,Glyph,Entry.Card);
    Icon->SetVisibility(ESlateVisibility::Visible);
    AddCopiesBadge(Face,Entry.Copies);
    if(!Entry.Note.IsEmpty())
    {
        // Y25's chosen element rides on the tile's corner.
        UTextBlock* Note=LDWidgets::Text(WidgetTree,Entry.Note,LDHUDStyle::TypeMicro);Note->SetAutoWrapText(false);StorageBold(Note,2);Built.Add(Note);
        Note->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
        auto* NoteSlot=Face->AddChildToOverlay(Note);NoteSlot->SetHorizontalAlignment(HAlign_Right);NoteSlot->SetVerticalAlignment(VAlign_Bottom);
        NoteSlot->SetPadding(FMargin(2,0,3,1));
    }
    auto* TileSlot=Column.List->AddChildToVerticalBox(Box);
    TileSlot->SetPadding(FMargin(0,Column.List->GetChildrenCount()>1?4.f:0.f,0,0));
    TileSlot->SetHorizontalAlignment(HAlign_Center);
    FTile& Tile=Tiles.AddDefaulted_GetRef();Tile.Component=Entry.Card;Tile.Column=ColumnId;Tile.Face=Icon;
}

void ULDComponentStorage::SetWorkshop(const FWorkshopLayout& Layout)
{
    const bool bWas=Workshop.bActive;
    Workshop=Layout;
    if(bWas!=Layout.bActive)
    {
        // The layouts build different faces: the next Present rebuilds them; an interaction of the other layout ends.
        CancelInteraction();Press=FPress();BuiltKey.Reset();
        for(FColumn& Column:Columns)
        {
            Column.AppliedLook=-1;
            if(Column.Frame){Column.Frame->SetWidthOverride(StorageColumnWidth);Column.Frame->ClearHeightOverride();Column.Frame->SetRenderOpacity(1.f);}
            if(Column.Fit){Column.Fit->SetStretch(Layout.bActive?EStretch::ScaleToFit:EStretch::None);}
            if(Column.Panel){Column.Panel->SetPadding(FMargin(4,6));}
        }
    }
}
bool ULDComponentStorage::OverExpanded(FVector2D At) const
{
    if(StorageUnder(ListFrame,At)||StorageUnder(ChooserFrame,At)||StorageUnder(QuickFrame,At)||StorageUnder(LibraryFrame,At)){return true;}
    for(int32 Index=0;Index<Columns.Num();++Index)
    {
        if(StorageUnder(Columns[Index].Frame,At)){return true;}
        if(Anchors.bValid&&StorageHasArea(Anchors.Columns[Index])&&Anchors.Columns[Index].ContainsPoint(At)){return true;}
    }
    return false;
}
bool ULDComponentStorage::IsHit(FVector2D At) const
{
    if(!bShownNow){return false;}
    if(Drag.bActive||StorageUnder(StackFrame,At)){return true;}
    if(!bExpanded){return false;}
    if(StorageUnder(ListFrame,At)||StorageUnder(ChooserFrame,At)||StorageUnder(QuickFrame,At)||StorageUnder(LibraryFrame,At)){return true;}
    for(const FColumn& Column:Columns){if(StorageUnder(Column.Frame,At)){return true;}}
    return false;
}
TArray<const UWidget*> ULDComponentStorage::ReservedWidgets() const
{
    TArray<const UWidget*> Out;
    for(const UWidget* Widget:{StackFrame.Get(),ListFrame.Get(),ChooserFrame.Get(),QuickFrame.Get(),ReplacePromptFrame.Get(),PopupFrame.Get(),LibraryFrame.Get()}){if(Widget&&Widget->IsVisible()){Out.Add(Widget);}}
    for(const FColumn& Column:Columns){if(Column.Frame&&Column.Frame->IsVisible()){Out.Add(Column.Frame);}}
    return Out;
}
FReply ULDComponentStorage::NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event)
{
    if(Event.GetEffectingButton()==EKeys::LeftMouseButton&&BeginPress(Event.GetScreenSpacePosition())){return FReply::Handled();}
    if(Event.GetEffectingButton()==EKeys::RightMouseButton&&bShownNow&&bExpanded)
    {
        // D6 / #331 right click: installed card unequips to storage; stored card installs at its single free place or pops [Q/W/E] capsules.
        const FVector2D At=Event.GetScreenSpacePosition();
        for(const FCard& Card:Cards){if(StorageUnder(Card.Face,At)){PendingAt=At;QuickInstall(Card.Component);return FReply::Handled();}}
        for(const FTile& Tile:Tiles){if(StorageUnder(Tile.Face,At)){QuickInstall(Tile.Component);return FReply::Handled();}}
        if(QuickEquip.bOpen||ReplacePick.bOpen){CloseQuickEquip();CloseReplacePick();return FReply::Handled();}
        if(IsHit(At)){return FReply::Handled();}
    }
    return Super::NativeOnMouseButtonDown(Geometry,Event);
}
void ULDComponentStorage::CollectQuickTargets(FName Component,TArray<FQuickTarget>& Out) const
{
    Out.Reset();
    const FLDCardInstanceView* Owned=LDHUD::FindCard(Latest,Component);
    if(!Owned||Owned->IsInstalled()||!LatestContent){return;}
    const TArray<LDHUD::FCardDrop> FreeDrops=LDHUD::CardDrops(Component,Latest,LatestContent);
    for(int32 Position=0;Position<LDHUD::StorageColumnCount;++Position)
    {
        const LDHUD::EStorageColumn Col=LDHUD::BoardColumn(Position);
        if(FreeDrops.ContainsByPredicate([Col](const LDHUD::FCardDrop& D){return D.Column==Col;}))
        {
            Out.Add({Col,false});
        }
        else if(!LDHUD::ColumnReplaceDrops(Component,Col,Latest,LatestContent).IsEmpty())
        {
            Out.Add({Col,true});
        }
    }
}
FString ULDComponentStorage::QuickTargetLabel(const FQuickTarget& Target) const
{
    FString HostName;
    if(Target.Column==LDHUD::EStorageColumn::SlotQ){HostName=SlotKeys.Of(0);}
    else if(Target.Column==LDHUD::EStorageColumn::SlotW){HostName=SlotKeys.Of(1);}
    else if(Target.Column==LDHUD::EStorageColumn::SlotE){HostName=SlotKeys.Of(2);}
    else if(Target.Column==LDHUD::EStorageColumn::Dash){HostName=LDTerms::Dash;}
    else{HostName=LDTerms::ColumnBody;}
    return FString::Printf(TEXT("%s %s"),Target.bReplace?TEXT("替换"):TEXT("装入"),*HostName);
}
bool ULDComponentStorage::OpenQuickEquip(FName Component,FVector2D At)
{
    CloseChooser();CloseReplacePick();
    TArray<FQuickTarget> Targets;
    CollectQuickTargets(Component,Targets);
    if(Targets.IsEmpty())
    {
        CloseQuickEquip();
        if(Notify)
        {
            const FString Missing=LDWorkshop::MissingPlace(Component,Latest,LatestContent);
            Notify(FString(LDTerms::Storage)+TEXT("：")+(!Missing.IsEmpty()?Missing:LDHUD::StoredPlaceText(Component,Latest,LatestContent)));
        }
        return false;
    }
    QuickEquip.bOpen=true;QuickEquip.Component=Component;QuickEquip.Targets=MoveTemp(Targets);QuickEquip.At=At;
    for(const FCard& Card:Cards)
    {
        if(Card.Component==Component&&Card.Face)
        {
            const FSlateRect Rect=StorageRect(Card.Face);
            if(StorageHasArea(Rect)){QuickEquip.At=FVector2D((Rect.Left+Rect.Right)*.5f,Rect.Top);}
            break;
        }
    }
    const int32 Count=QuickEquip.Targets.Num();
    while(QuickButtons.Num()<Count)
    {
        const int32 Index=QuickButtons.Num();
        ULDCommandButton* Button=LDWidgets::Button(WidgetTree,TEXT(""),[this,Index](){SelectQuickTarget(Index);},true,
            FName(*FString::Printf(TEXT("HUDStorageQuick%d"),Index)));
        QuickLine->AddChildToHorizontalBox(Button)->SetPadding(FMargin(Index?6.f:0.f,0,0,0));
        QuickButtons.Add(Button);
    }
    for(int32 Index=0;Index<QuickButtons.Num();++Index)
    {
        ULDCommandButton* Button=QuickButtons[Index];
        StorageShown(Button,Index<Count,ESlateVisibility::Visible);
        if(Index>=Count){continue;}
        const FQuickTarget& Target=QuickEquip.Targets[Index];
        if(UTextBlock* Caption=Cast<UTextBlock>(Button->GetContent())){StorageText(Caption,QuickTargetLabel(Target));}
        LDWidgets::SetLook(Button,Target.bReplace?LDUIPaint::EButton::Normal:LDUIPaint::EButton::Primary);
    }
    StorageShown(QuickFrame,true,ESlateVisibility::Visible);
    return true;
}
void ULDComponentStorage::CloseQuickEquip()
{
    QuickEquip=FQuickEquip();
    StorageShown(QuickFrame,false);
}
bool ULDComponentStorage::SelectQuickTarget(int32 Index)
{
    if(!QuickEquip.bOpen||!QuickEquip.Targets.IsValidIndex(Index)){return false;}
    const FName Component=QuickEquip.Component;
    const FQuickTarget Target=QuickEquip.Targets[Index];
    const FVector2D At=QuickEquip.At;
    CloseQuickEquip();
    if(Target.bReplace){return BeginReplacePick(Component,Target.Column);}
    Chooser.At=At;PendingAt=At;bPendingAt=true;
    return DropOn(Component,Target.Column,NAME_None);
}
bool ULDComponentStorage::HandleQuickEquipKey(FKey Key,const ELDCombatEntry (&SkillAtKey)[3],const ULDControlSettings* Settings)
{
    if(!QuickEquip.bOpen||!bShownNow){return false;}
    auto ColumnForEntry=[](ELDCombatEntry Entry)->LDHUD::EStorageColumn
    {
        return Entry==ELDCombatEntry::Q?LDHUD::EStorageColumn::SlotQ
            :Entry==ELDCombatEntry::W?LDHUD::EStorageColumn::SlotW
            :Entry==ELDCombatEntry::E?LDHUD::EStorageColumn::SlotE:LDHUD::EStorageColumn::Body;
    };
    TOptional<LDHUD::EStorageColumn> Wanted;
    const TCHAR* Actions[3]={TEXT("Skill1"),TEXT("Skill2"),TEXT("SkillE")};
    const FKey Defaults[3]={EKeys::Q,EKeys::W,EKeys::E};
    for(int32 SlotIdx=0;SlotIdx<3;++SlotIdx)
    {
        const FKey Bound=Settings?Settings->Key(Actions[SlotIdx]):Defaults[SlotIdx];
        if(Key==Bound||Key==Defaults[SlotIdx])
        {
            Wanted=ColumnForEntry(SkillAtKey[SlotIdx]);
            break;
        }
    }
    if(!Wanted.IsSet())
    {
        const FKey DashKey=Settings?Settings->Key(TEXT("Dash")):EKeys::D;
        if(Key==DashKey||Key==EKeys::D){Wanted=LDHUD::EStorageColumn::Dash;}
    }
    if(!Wanted.IsSet()){return false;}
    for(int32 Index=0;Index<QuickEquip.Targets.Num();++Index)
    {
        if(QuickEquip.Targets[Index].Column==Wanted.GetValue()){return SelectQuickTarget(Index);}
    }
    return false;
}
bool ULDComponentStorage::BeginReplacePick(FName Component,LDHUD::EStorageColumn Column)
{
    CloseQuickEquip();CloseChooser();
    if(LDHUD::ColumnReplaceDrops(Component,Column,Latest,LatestContent).IsEmpty()){return false;}
    ReplacePick.bOpen=true;
    ReplacePick.Component=Component;
    ReplacePick.Column=Column;
    StorageText(ReplacePromptText,LDTerms::ReplacePickPrompt);
    StorageShown(ReplacePromptFrame,true,ESlateVisibility::HitTestInvisible);
    return true;
}
void ULDComponentStorage::CloseReplacePick()
{
    ReplacePick=FReplacePick();
    StorageShown(ReplacePromptFrame,false);
}
bool ULDComponentStorage::QuickInstall(FName Component)
{
    const FLDCardInstanceView* Owned=LDHUD::FindCard(Latest,Component);
    if(!Owned){return false;}
    if(Drag.bActive){EndDrag();}
    CloseChooser();CloseQuickEquip();CloseReplacePick();
    if(Owned->IsInstalled()){bPendingAt=false;return DropInStorage(Component);}
    TArray<FQuickTarget> Targets;
    CollectQuickTargets(Component,Targets);
    if(Targets.IsEmpty())
    {
        const FString Missing=LDWorkshop::MissingPlace(Component,Latest,LatestContent);
        if(Notify){Notify(FString(LDTerms::Storage)+TEXT("：")+(!Missing.IsEmpty()?Missing:LDHUD::StoredPlaceText(Component,Latest,LatestContent)));}
        return false;
    }
    if(Targets.Num()==1)
    {
        if(Targets[0].bReplace){return BeginReplacePick(Component,Targets[0].Column);}
        bPendingAt=!PendingAt.IsZero();Chooser.At=PendingAt;
        return DropOn(Component,Targets[0].Column,NAME_None);
    }
    return OpenQuickEquip(Component,PendingAt);
}
bool ULDComponentStorage::PickUp(FName Component,bool bFromColumn)
{
    if(Component.IsNone()){return false;}
    Press=FPress();
    CloseQuickEquip();CloseReplacePick();
    Drag.Component=Component;Drag.bFromColumn=bFromColumn;Drag.bActive=true;Drag.bPicked=true;Drag.Since=UiNow();
    LDWidgets::PlayCue(this,ELDAudioCue::UIPickup);
    Drag.Drops=LDHUD::CardDrops(Component,Latest,LatestContent);
    Drag.Drops.Append(LDHUD::ReplaceDrops(Component,Latest,LatestContent));
    ShowIcon(GhostPicture,GhostGlyph,Component);
    StorageShown(GhostFrame,true,ESlateVisibility::HitTestInvisible);
    return true;
}
bool ULDComponentStorage::DropHeld(FVector2D At)
{
    // The second click of a pick-up: a place takes it, the storage takes back an installed one, anywhere else puts it down.
    const FName Component=Drag.Component;const bool bFromColumn=Drag.bFromColumn;
    EndDrag();
    LDHUD::EStorageColumn Column=LDHUD::EStorageColumn::Body;FName Host;
    if(StorageAt(At)){if(bFromColumn){bPendingAt=false;return DropInStorage(Component);}return true;}
    if(ColumnAt(At,Column,Host)){Chooser.At=At;PendingAt=At;bPendingAt=true;return DropOn(Component,Column,Host);}
    return true;
}
bool ULDComponentStorage::BeginPress(FVector2D At)
{
    if(!bShownNow){return false;}
    // Let buttons inside QuickFrame or ChooserFrame take their own clicks.
    if((QuickEquip.bOpen&&StorageUnder(QuickFrame,At))||(Chooser.bOpen&&StorageUnder(ChooserFrame,At))){return false;}
    if(ReplacePick.bOpen)
    {
        for(const FTile& Tile:Tiles)
        {
            if(Tile.Column==ReplacePick.Column&&StorageUnder(Tile.Face,At))
            {
                const FName Component=ReplacePick.Component;
                const LDHUD::EStorageColumn Column=ReplacePick.Column;
                CloseReplacePick();
                Chooser.At=At;PendingAt=At;bPendingAt=true;
                DropOn(Component,Column,Tile.Component);
                return true;
            }
        }
        CloseReplacePick();
    }
    if(Drag.bActive&&Drag.bPicked){DropHeld(At);return true;}
    FName Component;bool bFromColumn=false;
    if(bExpanded)
    {
        for(const FCard& Card:Cards){if(StorageUnder(Card.Face,At)){Component=Card.Component;break;}}
        if(Component.IsNone()){for(const FTile& Tile:Tiles){if(StorageUnder(Tile.Face,At)){Component=Tile.Component;bFromColumn=true;break;}}}
    }
    if(Component.IsNone())
    {
        if(QuickEquip.bOpen){CloseQuickEquip();return true;}
        return IsHit(At);
    }
    // One request at a time: a press while one is on its way, or while a move waits for its second half, starts nothing.
    if(PendingRequest||Follow.State!=EFollow::None){if(Notify){Notify(FString(LDTerms::Storage)+TEXT("：上一个请求仍在处理中。"));}return true;}
    if(!Gate.bCanInstall){if(Notify){Notify(FString(LDTerms::Storage)+TEXT("：")+Gate.InstallBlocked);}return true;}
    CloseChooser();
    if(QuickEquip.bOpen&&QuickEquip.Component!=Component){CloseQuickEquip();}
    Press.Component=Component;Press.bFromColumn=bFromColumn;Press.At=At;Press.bArmed=true;
    return true;
}
void ULDComponentStorage::UpdateDrag(FVector2D Pointer,bool bHeld)
{
    if(!Press.bArmed){return;}
    if(!Drag.bActive)
    {
        // A press that never moves is a click: an uninstalled card opens inline [装入 ...] quick capsules (#331),
        // while an installed card picks up so the next click moves or stores it.
        if(!bHeld)
        {
            const FName Component=Press.Component;const bool bFromColumn=Press.bFromColumn;const FVector2D At=Press.At;
            Press=FPress();
            if(!bFromColumn){PendingAt=At;QuickInstall(Component);}
            else{PickUp(Component,true);}
            return;
        }
        if(FVector2D::Distance(Pointer,Press.At)<StorageDragStartPx){return;}
        CloseQuickEquip();CloseReplacePick();
        Drag.Component=Press.Component;Drag.bFromColumn=Press.bFromColumn;Drag.bActive=true;Drag.bPicked=false;Drag.Since=UiNow();
        LDWidgets::PlayCue(this,ELDAudioCue::UIPickup);
        Drag.Drops=LDHUD::CardDrops(Drag.Component,Latest,LatestContent);
        Drag.Drops.Append(LDHUD::ReplaceDrops(Drag.Component,Latest,LatestContent));
        ShowIcon(GhostPicture,GhostGlyph,Drag.Component);
        StorageShown(GhostFrame,true,ESlateVisibility::HitTestInvisible);
        return;
    }
    if(bHeld||Drag.bPicked){return;}
    // Let go: the storage takes back an installed card; a column or a tile installs, moves or replaces it.
    const FName Component=Drag.Component;const bool bFromColumn=Drag.bFromColumn;
    EndDrag();
    LDHUD::EStorageColumn Column=LDHUD::EStorageColumn::Body;FName Host;
    if(StorageAt(Pointer)){if(bFromColumn){bPendingAt=false;DropInStorage(Component);}}
    else if(ColumnAt(Pointer,Column,Host)){Chooser.At=Pointer;PendingAt=Pointer;bPendingAt=true;DropOn(Component,Column,Host);}
}
void ULDComponentStorage::EndDrag()
{
    Press=FPress();Drag=FDrag();
    StorageShown(GhostFrame,false);StorageShown(GhostCaptionFrame,false);
}
bool ULDComponentStorage::CancelInteraction()
{
    const bool bHad=Drag.bActive||Chooser.bOpen||QuickEquip.bOpen||ReplacePick.bOpen;
    EndDrag();CloseChooser();CloseQuickEquip();CloseReplacePick();
    return bHad;
}
bool ULDComponentStorage::StorageAt(FVector2D At) const
{
    return StorageUnder(StackFrame,At)||StorageUnder(ListFrame,At)||StorageUnder(LibraryFrame,At);
}
bool ULDComponentStorage::ColumnAt(FVector2D At,LDHUD::EStorageColumn& OutColumn,FName& OutHost) const
{
    for(const FTile& Tile:Tiles){if(StorageUnder(Tile.Face,At)){OutColumn=Tile.Column;OutHost=Tile.Component;return true;}}
    for(int32 Index=0;Index<Columns.Num();++Index)
    {
        const bool bAnchor=Anchors.bValid&&StorageHasArea(Anchors.Columns[Index])&&Anchors.Columns[Index].ContainsPoint(At);
        const bool bWorkshopCol=Workshop.bActive&&StorageHasArea(Workshop.Columns[Index])&&Workshop.Columns[Index].ContainsPoint(At);
        if(bAnchor||bWorkshopCol||StorageUnder(Columns[Index].Frame,At)){OutColumn=static_cast<LDHUD::EStorageColumn>(Index);OutHost=NAME_None;return true;}
    }
    return false;
}
FString ULDComponentStorage::PlaceName(LDHUD::EStorageColumn Column,FName Host) const
{
    if(!Host.IsNone()){return ComponentName(Host);}
    const int32 Index=int32(Column);
    if(!Columns.IsValidIndex(Index)){return FString();}
    const FString Title=Columns[Index].Title->GetText().ToString();
    return Title.IsEmpty()&&!Columns[Index].Active.IsNone()?ComponentName(Columns[Index].Active):Title;
}

bool ULDComponentStorage::DropInStorage(FName Component)
{
    const FLDCardInstanceView* Owned=LDHUD::FindCard(Latest,Component);
    if(!Owned||!Owned->IsInstalled()){return false;}
    if(PendingRequest||Follow.State!=EFollow::None){if(Notify){Notify(FString(LDTerms::Storage)+TEXT("：上一个请求仍在处理中。"));}return false;}
    FLDBuildCommand Command;Command.Kind=ELDBuildCommandKind::UninstallCard;Command.CardSerial=Owned->Serial;
    PendingComponent=Component;
    return Send(Command);
}
bool ULDComponentStorage::DropOn(FName Component,LDHUD::EStorageColumn Column,FName Host)
{
    const FLDCardInstanceView* Owned=LDHUD::FindCard(Latest,Component);
    if(!Owned){return false;}
    if(PendingRequest||Follow.State!=EFollow::None){if(Notify){Notify(FString(LDTerms::Storage)+TEXT("：上一个请求仍在处理中。"));}return false;}
    if(!Gate.bCanInstall){if(Notify){Notify(FString(LDTerms::Storage)+TEXT("：")+Gate.InstallBlocked);}return false;}
    // Let go where it already sits: nothing to do.
    if(Owned->IsInstalled())
    {
        for(const FTile& Tile:Tiles)
        {
            if(Tile.Component!=Component){continue;}
            if(Tile.Column==Column&&(Host.IsNone()||Host==Component)){return false;}
        }
    }
    // The shared capacity is the only limit (CardDrops applies it); an element medal takes none of it.
    const TArray<LDHUD::FCardDrop> Drops=LDHUD::CardDrops(Component,Latest,LatestContent);
    // A tile of the same column is the column itself unless it is a replace drop.
    const LDHUD::FCardDrop* Drop=LDHUD::FindDrop(Drops,Column,NAME_None);
    // A stored card let go on an installed card replaces it in one commit; let go on a column of a full workshop enters 1-click replace pick mode (#331).
    const TArray<LDHUD::FCardDrop> ColReplaces=Drop||Owned->IsInstalled()?TArray<LDHUD::FCardDrop>():LDHUD::ColumnReplaceDrops(Component,Column,Latest,LatestContent);
    if(!Drop&&!Host.IsNone())
    {
        Drop=ColReplaces.FindByPredicate([Column,Host](const LDHUD::FCardDrop& D){return D.Column==Column&&D.Host==Host;});
    }
    if(!Drop&&Host.IsNone()&&!ColReplaces.IsEmpty())
    {
        return BeginReplacePick(Component,Column);
    }
    if(!Drop&&!Owned->IsInstalled()&&Drops.IsEmpty()&&LDHUD::WorkshopFull(Latest,LatestContent))
    {
        if(Notify){Notify(FString(LDTerms::Storage)+TEXT("：")+LDHUD::StoredPlaceText(Component,Latest,LatestContent));}
        return false;
    }
    if(!Drop)
    {
        if(Notify){Notify(FString(LDTerms::Storage)+TEXT("：")+LDTerms::CannotGoThere(ComponentName(Component),PlaceName(Column,Host)));}
        return false;
    }
    // Y25 asks for its element, unless there is only one.
    if(Drop->Elements.Num()>1){OpenChooser(Component,*Drop);return true;}
    return Commit(Component,*Drop,Drop->Elements.Num()==1?Drop->Elements[0]:ELDDemoElement::None);
}
void ULDComponentStorage::OpenChooser(FName Component,const LDHUD::FCardDrop& Drop)
{
    Chooser.bOpen=true;Chooser.Component=Component;Chooser.Drop=Drop;
    StorageText(ChooserTitle,ComponentName(Component)+TEXT(" · ")+LDTerms::PickElement);
    const int32 Count=Drop.Elements.Num();
    while(ChooserButtons.Num()<Count)
    {
        const int32 Index=ChooserButtons.Num();
        ULDCommandButton* Button=LDWidgets::Button(WidgetTree,TEXT(""),[this,Index](){ChooseOption(Index);},true,FName(*FString::Printf(TEXT("HUDStorageChoice%d"),Index)));
        ChooserLine->AddChildToHorizontalBox(Button)->SetPadding(FMargin(Index?6.f:0.f,0,0,0));
        ChooserButtons.Add(Button);
    }
    for(int32 Index=0;Index<ChooserButtons.Num();++Index)
    {
        ULDCommandButton* Button=ChooserButtons[Index];
        StorageShown(Button,Index<Count,ESlateVisibility::Visible);
        if(Index>=Count){continue;}
        if(UTextBlock* Caption=Cast<UTextBlock>(Button->GetContent())){StorageText(Caption,LDHUD::ElementName(Drop.Elements[Index]));}
    }
    StorageShown(ChooserFrame,true,ESlateVisibility::Visible);
}
void ULDComponentStorage::CloseChooser()
{
    Chooser=FChooser();
    StorageShown(ChooserFrame,false);
}
bool ULDComponentStorage::ChooseOption(int32 Index)
{
    if(!Chooser.bOpen){return false;}
    const FName Component=Chooser.Component;const LDHUD::FCardDrop Drop=Chooser.Drop;
    if(!Drop.Elements.IsValidIndex(Index)){return false;}
    CloseChooser();
    return Commit(Component,Drop,Drop.Elements[Index]);
}
bool ULDComponentStorage::Commit(FName Component,const LDHUD::FCardDrop& Drop,ELDDemoElement Element)
{
    const FLDCardInstanceView* Owned=LDHUD::FindCard(Latest,Component);
    if(!Owned){return false;}
    PendingComponent=Component;
    FLDBuildCommand Install;Install.Kind=ELDBuildCommandKind::InstallCard;Install.CardSerial=Owned->Serial;
    Install.CardTarget=Drop.Kind;Install.Element=Element;
    const auto Refuse=[this](const TCHAR* Why){if(Notify){Notify(FString(TEXT("没有提交："))+Why);}return false;};
    if(!Drop.TargetId.IsNone()&&!FLDDefinitionWire::Encode(Drop.TargetId,Install.TargetDefinition)){return Refuse(TEXT("安装位置的标识无效。"));}
    if(!Drop.Replacing.IsNone())
    {
        const FLDCardInstanceView* Replaced=LDHUD::FindCard(Latest,Drop.Replacing);
        if(Owned->IsInstalled()||!Replaced){return Refuse(TEXT("被替换的牌已经不在了。"));}
        bPendingMedalFuse=(Owned->DefinitionId==Replaced->DefinitionId);
        Install.ReplacingSerial=Replaced->Serial;
        return Send(Install);
    }
    bPendingMedalFuse=Latest.Cards.ContainsByPredicate([&](const FLDCardInstanceView& C)
    {
        return C.IsInstalled() && C.DefinitionId==Owned->DefinitionId && C.Target.Kind==Drop.Kind
            && (Drop.Kind!=ELDCardTargetKind::Skill || C.Target.Active==Drop.TargetId);
    });
    if(!Owned->IsInstalled()){return Send(Install);}
    // A move: uninstall first, then install once the view shows the uninstall.
    FLDBuildCommand Uninstall;Uninstall.Kind=ELDBuildCommandKind::UninstallCard;Uninstall.CardSerial=Owned->Serial;
    Follow.State=EFollow::AwaitUninstall;Follow.Component=Component;Follow.Install=Install;
    return Send(Uninstall);
}
bool ULDComponentStorage::Send(FLDBuildCommand Command)
{
    if(!SubmitBuild)
    {
        UE_LOG(LogLDUI,Error,TEXT("LD_STORAGE_SUBMIT_UNAVAILABLE The growth build entry is not connected to the card storage."));
        Follow=FFollowUp();
        if(Notify){Notify(TEXT("成长入口不可用，本次没有提交。"));}
        return false;
    }
    Command.ExpectedBuildRevision=Latest.BuildRevision;Command.CatalogDigest=Latest.CatalogDigest;
    // A Listen Host answers inside the submit call, so the pending entry is keyed to the id it will receive first.
    const uint64 Expected=PeekRequestId?PeekRequestId():0;
    const uint32 Before=Answers;PendingRequest=Expected;
    ELDGrowthReason Reason=ELDGrowthReason::None;
    const uint64 Request=SubmitBuild(Command,Reason);
    if(!Request)
    {
        if(PendingRequest==Expected){PendingRequest=0;}
        Follow=FFollowUp();PendingComponent=NAME_None;bPendingAt=false;
        const FString Why=LDHUD::GrowthReasonText(Reason);
        if(Notify){Notify(FString(LDTerms::Storage)+TEXT("：没有提交：")+(Why.IsEmpty()?FString(TEXT("原因未知")):Why));}
        return false;
    }
    if(Answers==Before){PendingRequest=Request;}
    return true;
}
void ULDComponentStorage::FinishRequest(const FString& Text,bool bRefused)
{
    PendingRequest=0;
    const bool bFused=bPendingMedalFuse;
    bPendingMedalFuse=false;
    // A refusal shakes the card and edges it red for a moment; an applied install flashes where it lands.
    if(bRefused){RejectedComponent=PendingComponent;RejectedAt=UiNow();LDWidgets::PlayCue(this,ELDAudioCue::UIReject);}
    else if(!Text.IsEmpty()){LandedComponent=PendingComponent;LandedAt=UiNow();LDWidgets::PlayCue(this,bFused?ELDAudioCue::UIMedalFuse:ELDAudioCue::UIInstall);}
    if(Follow.State==EFollow::None){bPendingAt=false;PendingComponent=NAME_None;}
    if(!Text.IsEmpty()&&Notify){Notify(FString(LDTerms::Storage)+TEXT("：")+Text);}
}
void ULDComponentStorage::ShowFinal(const FLDBuildReceipt& Receipt)
{
    const bool bApplied=Receipt.Header.Outcome==ELDReceiptOutcome::Applied;
    if(Follow.State==EFollow::AwaitUninstall&&Receipt.Kind==ELDBuildCommandKind::UninstallCard)
    {
        // The move's first half: its install waits for the own view to carry this build revision.
        if(bApplied)
        {
            Follow.State=EFollow::AwaitView;
            Follow.AfterRevision=Receipt.Header.Revisions.bHasBuild?Receipt.Header.Revisions.BuildRevision:Latest.BuildRevision+1;
            Follow.Since=GetWorld()?GetWorld()->GetRealTimeSeconds():0;
            FinishRequest(FString());
            return;
        }
        Follow=FFollowUp();
    }
    FinishRequest(LDHUD::BuildReceiptText(Receipt,LatestContent),!bApplied);
}
// The pending entry clears exactly when the controller releases its in-flight request (like the growth panel's).
void ULDComponentStorage::HandleSubmit(const FLDSubmitResult& Result)
{
    if(Result.Family!=ELDGrowthFamily::Build||!PendingRequest||Result.RequestId!=PendingRequest||Result.Generation!=LatestMatch){return;}
    ++Answers;
    switch(Result.Status)
    {
    case ELDAdmissionStatus::AcceptedPending: return;
    case ELDAdmissionStatus::AdmissionRejected:
        if(Result.bSequenceReserved){return;}
        Follow=FFollowUp();
        FinishRequest(TEXT("没有受理：")+LDHUD::GrowthReasonText(LDGrowthWire::Reason(Result.Reason)),true);
        return;
    case ELDAdmissionStatus::Final:
        if(Result.bHasBuildReceipt){ShowFinal(Result.BuildReceipt);return;}
        Follow=FFollowUp();FinishRequest(TEXT("请求已结束，请以当前状态为准。"));
        return;
    default:
        if(Notify){Notify(FString(LDTerms::Storage)+TEXT("：无法识别服务器的答复，请以当前状态为准。"));}
        return;
    }
}
void ULDComponentStorage::HandleReceipt(const FLDBuildReceiptQueryResult& Result)
{
    if(Result.Query.Family!=ELDGrowthFamily::Build||!PendingRequest||Result.Query.RequestId!=PendingRequest||Result.Query.Generation!=LatestMatch){return;}
    ++Answers;
    switch(Result.Lookup)
    {
    case ELDReceiptLookup::Pending: return;
    case ELDReceiptLookup::Final:
        if(Result.bHasReceipt){ShowFinal(Result.Receipt);return;}
        Follow=FFollowUp();FinishRequest(TEXT("回执缺少结果，请以当前状态为准。"));
        return;
    case ELDReceiptLookup::ReceiptExpired: Follow=FFollowUp();FinishRequest(TEXT("回执已过期，请以当前状态为准。"));return;
    case ELDReceiptLookup::NotFound:
        if(Notify){Notify(FString(LDTerms::Storage)+TEXT("：服务器没有找到这次请求，请以当前状态为准。"));}
        return;
    default:
        if(Notify){Notify(FString(LDTerms::Storage)+TEXT("：回执查询无效，请以当前状态为准。"));}
        return;
    }
}

void ULDComponentStorage::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);
    if(!bShownNow||!FSlateApplication::IsInitialized())
    {
        for(UWidget* Hidden:{ListFrame.Get(),PopupFrame.Get(),ChooserFrame.Get(),QuickFrame.Get(),ReplacePromptFrame.Get(),GhostFrame.Get(),GhostCaptionFrame.Get(),LibraryFrame.Get()}){StorageShown(Hidden,false);}
        for(const FColumn& Column:Columns){StorageShown(Column.Frame,false);}
        CurrentFocus=FFocus();
        return;
    }
    const double Now=UiNow();
    const FVector2D Pointer=FSlateApplication::Get().GetCursorPos();
    const bool bHeld=FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::LeftMouseButton);
    // A click away from every place puts a picked-up card down or closes quick capsules / replace pick.
    if(bHeld&&!bWasPressed&&!StorageAt(Pointer)&&!OverExpanded(Pointer))
    {
        if(Drag.bActive&&Drag.bPicked){EndDrag();}
        if(QuickEquip.bOpen){CloseQuickEquip();}
        if(ReplacePick.bOpen){CloseReplacePick();}
    }
    bWasPressed=bHeld;
    UpdateDrag(Pointer,bHeld);
    if(Workshop.bActive){bExpanded=true;LastInsideAt=Now;}
    else
    {
        // Hovering the stack opens the view; it stays while the pointer is over any part of it (or a slot under a column), a
        // drag or the chooser runs, and closes a moment after the pointer left.
        const bool bOverStack=StorageUnder(StackFrame,Pointer);
        const bool bInside=bPinned||bDetailsHeld||bOverStack||(bExpanded&&OverExpanded(Pointer))||Drag.bActive||Press.bArmed||Chooser.bOpen||QuickEquip.bOpen||ReplacePick.bOpen;
        if(bInside){LastInsideAt=Now;if(bOverStack||bPinned||bDetailsHeld){bExpanded=true;}}
        else if(bExpanded&&Now-LastInsideAt>StorageCloseGrace){bExpanded=false;}
    }
    Layout(Geometry,Pointer);
}
void ULDComponentStorage::SetPreviewCaption(FName Component,const FString& DropText,const FString& Value)
{
    PreviewCaptionComponent=Component;PreviewCaptionDrop=DropText;PreviewCaption=Value;
}
void ULDComponentStorage::UpdateFocus(FVector2D Pointer)
{
    FFocus Next;
    if(Drag.bActive)
    {
        Next.Component=Drag.Component;Next.bHeld=true;Next.bInstalled=Drag.bFromColumn;Next.Drops=Drag.Drops;
        LDHUD::EStorageColumn Column=LDHUD::EStorageColumn::Body;FName Host;
        if(!StorageAt(Pointer)&&ColumnAt(Pointer,Column,Host))
        {
            const LDHUD::FCardDrop* Drop=Host.IsNone()?nullptr:Drag.Drops.FindByPredicate([Column,Host](const LDHUD::FCardDrop& D){return D.Column==Column&&D.Host==Host;});
            if(!Drop){Drop=Drag.Drops.FindByPredicate([Column](const LDHUD::FCardDrop& D){return D.Column==Column&&D.Host.IsNone();});}
            if(Drop){Next.bOverDrop=true;Next.Drop=*Drop;}
        }
    }
    else if(ReplacePick.bOpen)
    {
        if(!LDHUD::FindCard(Latest,ReplacePick.Component)){CloseReplacePick();}
        else
        {
            Next.Component=ReplacePick.Component;Next.bHeld=true;
            Next.Drops=LDHUD::ColumnReplaceDrops(ReplacePick.Component,ReplacePick.Column,Latest,LatestContent);
            for(const FTile& Tile:Tiles)
            {
                if(Tile.Column==ReplacePick.Column&&StorageUnder(Tile.Face,Pointer))
                {
                    if(const LDHUD::FCardDrop* Found=Next.Drops.FindByPredicate([&Tile](const LDHUD::FCardDrop& D){return D.Host==Tile.Component;}))
                    {Next.bOverDrop=true;Next.Drop=*Found;}
                    break;
                }
            }
        }
    }
    else if(QuickEquip.bOpen)
    {
        if(!LDHUD::FindCard(Latest,QuickEquip.Component)){CloseQuickEquip();}
        else
        {
            Next.Component=QuickEquip.Component;Next.bHeld=true;
            Next.Drops=LDHUD::CardDrops(QuickEquip.Component,Latest,LatestContent);
            for(const FQuickTarget& Target:QuickEquip.Targets)
            {
                if(Target.bReplace){Next.Drops.Append(LDHUD::ColumnReplaceDrops(QuickEquip.Component,Target.Column,Latest,LatestContent));}
            }
        }
    }
    else if(Chooser.bOpen)
    {
        Next.Component=Chooser.Component;Next.bHeld=true;Next.bOverDrop=true;Next.Drop=Chooser.Drop;
        for(int32 Index=0;Index<ChooserButtons.Num();++Index)
        {
            if(!ChooserButtons[Index]||!ChooserButtons[Index]->IsHovered()){continue;}
            if(Chooser.Drop.Elements.IsValidIndex(Index)){Next.HoveredElement=Chooser.Drop.Elements[Index];}
            break;
        }
    }
    else if(bExpanded)
    {
        for(const FCard& Card:Cards){if(StorageUnder(Card.Face,Pointer)){Next.Component=Card.Component;break;}}
        if(!Next.Component.IsNone())
        {
            // Hovering a stored card already shows where it goes: its places light up before anything is picked up.
            if(HoverDropsOf!=Next.Component)
            {
                HoverDropsOf=Next.Component;
                HoverDrops=LDHUD::CardDrops(Next.Component,Latest,LatestContent);
                HoverDrops.Append(LDHUD::ReplaceDrops(Next.Component,Latest,LatestContent));
            }
            Next.Drops=HoverDrops;
            if(Workshop.bActive){MarkSeen(Next.Component);}
        }
        else{for(const FTile& Tile:Tiles){if(StorageUnder(Tile.Face,Pointer)){Next.Component=Tile.Component;Next.bInstalled=true;break;}}}
    }
    CurrentFocus=MoveTemp(Next);
}
void ULDComponentStorage::UpdateStates(FVector2D Pointer)
{
    const double Now=UiNow();
    const double RejectAge=Now-RejectedAt;
    const bool bRejectLive=RejectAge<LDHUDStyle::MotionRejectNotice;
    // A refused card shakes three times, then keeps its red edge for the notice time.
    const float Shake=RejectAge<LDHUDStyle::MotionReject?FMath::Sin(float(RejectAge/LDHUDStyle::MotionReject)*3.f*2.f*PI)*4.f:0.f;
    const float ReplaceJitter=ReplacePick.bOpen?FMath::Sin(float(Now*16.))*2.2f:0.f;
    for(FCard& Card:Cards)
    {
        const bool bRejected=bRejectLive&&Card.Component==RejectedComponent;
        const bool bSelected=(Drag.bActive&&Drag.Component==Card.Component)
            ||(QuickEquip.bOpen&&QuickEquip.Component==Card.Component)
            ||(ReplacePick.bOpen&&ReplacePick.Component==Card.Component);
        const bool bHovered=CurrentFocus.Component==Card.Component&&!CurrentFocus.bInstalled&&!bSelected;
        Card.State=bRejected?EFaceState::Rejected
            :bSelected?EFaceState::Held:bHovered?EFaceState::Hovered:EFaceState::Normal;
        LDTween::SetTranslation(Card.Face,FVector2D(bRejected?Shake:0.f,0.f));
        LDTween::SetScale(Card.Face,Card.State==EFaceState::Hovered?LDHUDStyle::HoverScale:1.f);
        LDTween::SetOpacity(Card.Face,1.f);
    }
    for(FTile& Tile:Tiles)
    {
        const LDHUD::FCardDrop* OnTile=CurrentFocus.Drops.FindByPredicate([&Tile](const LDHUD::FCardDrop& Drop){return Drop.Host==Tile.Component;});
        const bool bRejected=bRejectLive&&Tile.Component==RejectedComponent;
        const bool bReplaceCandidate=ReplacePick.bOpen&&Tile.Column==ReplacePick.Column&&OnTile!=nullptr;
        const bool bReplaced=bReplaceCandidate||(CurrentFocus.bOverDrop&&CurrentFocus.Drop.Replacing==Tile.Component);
        Tile.State=bRejected?EFaceState::Rejected
            :Drag.bActive&&Drag.Component==Tile.Component?EFaceState::Held
            :bReplaced||(OnTile&&!OnTile->Replacing.IsNone())?EFaceState::Replace
            :ReplacePick.bOpen&&Tile.Column!=ReplacePick.Column?EFaceState::Dim
            :Warnings.Contains(Tile.Component)?EFaceState::Detached
            :OnTile?EFaceState::Legal
            :CurrentFocus.Component==Tile.Component&&CurrentFocus.bInstalled?EFaceState::Hovered:EFaceState::Normal;
        LDTween::SetTranslation(Tile.Face,FVector2D(bRejected?Shake:bReplaceCandidate?ReplaceJitter:0.f,0.f));
        LDTween::SetOpacity(Tile.Face,Tile.State==EFaceState::Held?.35f:Tile.State==EFaceState::Dim?.4f:1.f);
    }
}
float ULDComponentStorage::StackHeight() const
{return StackFrame&&StackFrame->IsVisible()?float(StackFrame->GetDesiredSize().Y):0.f;}
void ULDComponentStorage::Layout(const FGeometry& Geometry,FVector2D Pointer)
{
    const FVector2D Area=Geometry.GetLocalSize();
    const auto Local=[&Geometry](FVector2D Absolute){return Geometry.AbsoluteToLocal(Absolute);};
    UpdateFocus(Pointer);
    UpdateStates(Pointer);
    if(Workshop.bActive){LayoutWorkshop(Geometry,Pointer);}
    else
    {
        StorageShown(LibraryFrame,false);
        // The stack stands right above the 构筑 button, right edges aligned; without the button it sits right of the band.
        if(StorageHasArea(Anchors.Corner))
        {StoragePlace(StackSlot,Local(FVector2D(Anchors.Corner.Right,Anchors.Corner.Top))-FVector2D(0,StorageGapFromBand),FVector2D(1,1));}
        else{StoragePlace(StackSlot,Local(FVector2D(Anchors.Band.Right,Anchors.Band.Bottom))+FVector2D(StorageGapFromBand,0),FVector2D(0,1));}
        StorageShown(ListFrame,bExpanded&&!Cards.IsEmpty(),ESlateVisibility::Visible);
        const FSlateRect Stack=StorageRect(StackFrame);
        const FVector2D StackTopLeft=Local(FVector2D(Stack.Left,Stack.Top));
        if(bExpanded)
        {
            // The list rises from the stack; a dragged icon fades (its rim is painted, LDUIShapes). Over the corner button the
            // list keeps the stack's right edge, so it stays on screen.
            if(StorageHasArea(Anchors.Corner))
            {StoragePlace(ListSlot,Local(FVector2D(Stack.Right,Stack.Top))-FVector2D(0,6.f),FVector2D(1,1));}
            else{StoragePlace(ListSlot,FVector2D(StackTopLeft.X,StackTopLeft.Y-6.f),FVector2D(0,1));}
            for(FCard& Card:Cards){LDTween::SetOpacity(Card.Size,Card.State==EFaceState::Held?.35f:1.f);}
        }
        // The columns rise over their anchors in one row: 本体 (left of the first Q/W/E slot), Q/W/E and D, each centred.
        // While a component is hovered or held, a column that takes it is edged green, one it only replaces in amber, and one
        // that takes nothing dims.
        for(int32 Index=0;Index<Columns.Num();++Index)
        {
            FColumn& Column=Columns[Index];
            const FSlateRect& Anchor=Anchors.Columns[Index];
            const bool bShown=bExpanded&&StorageHasArea(Anchor);
            StorageShown(Column.Frame,bShown,ESlateVisibility::Visible);
            if(!bShown){continue;}
            const FVector2D Top=Local(FVector2D((Anchor.Left+Anchor.Right)*.5f,Anchor.Top));
            StoragePlace(Column.Slot,Top-FVector2D(0,6),FVector2D(.5f,1));
            const LDHUD::EStorageColumn Id=static_cast<LDHUD::EStorageColumn>(Index);
            const TArray<LDHUD::FCardDrop>& Drops=CurrentFocus.Drops;
            const bool bOwnDrop=Drags(Drops,Id,true);
            const bool bAnyDrop=Drags(Drops,Id,false);
            const bool bReplaceDrop=Drops.ContainsByPredicate([Id](const LDHUD::FCardDrop& Drop){return Drop.Column==Id&&!Drop.Replacing.IsNone();});
            StorageShown(Column.Placeholder,bOwnDrop&&CurrentFocus.bHeld,ESlateVisibility::HitTestInvisible);
            const int32 Look=Drops.IsEmpty()?0:bAnyDrop?1:bReplaceDrop?3:2;
            const bool bBreathing=QuickEquip.bOpen&&(Look==1||Look==3);
            if(Column.AppliedLook!=Look||bBreathing)
            {
                Column.AppliedLook=bBreathing?-1:Look;
                const float Breath=bBreathing?.55f+.45f*LDTween::PingPong(UiNow(),.9):1.f;
                const FLinearColor Rim=(Look==1?LDHUDStyle::UILegal:Look==3?LDHUDStyle::UIReplace:LDHUDStyle::PanelRim).CopyWithNewOpacity(Breath);
                Column.Panel->SetBrush(StorageRound(8.f,Rim,Look==1||Look==3?2.2f:1.f));
                Column.Panel->SetBrushColor(LDHUDStyle::ColumnFill);
                Column.Frame->SetRenderOpacity(Look==2?.45f:1.f);
            }
        }
        // The hovered component's description beside it: right of it, or left when there is no room.
        FName Shown;const UWidget* Over=nullptr;bool bInstalled=false;
        if(bExpanded&&!Drag.bActive&&!Chooser.bOpen)
        {
            for(const FCard& Card:Cards){if(StorageUnder(Card.Face,Pointer)){Shown=Card.Component;Over=Card.Face;break;}}
            if(!Over){for(const FTile& Tile:Tiles){if(StorageUnder(Tile.Face,Pointer)){Shown=Tile.Component;Over=Tile.Face;bInstalled=true;break;}}}
        }
        StorageShown(PopupFrame,Over!=nullptr,ESlateVisibility::HitTestInvisible);
        if(Over)
        {
            MarkSeen(Shown);
            const LDHUD::FCardFaceView View=LDHUD::CardFace(Shown,LatestContent,LatestStats);
            StorageText(PopupName,View.Name);
            const FLDCardInstanceView* Owned=LDHUD::FindCard(Latest,Shown);
            const FString Target=Owned?LDHUD::CardTargetText(*Owned,LatestContent):FString();
            const FString Where=bInstalled?(Target.IsEmpty()?FString(TEXT("已安装")):FString(TEXT("已安装"))+Target)
                :FString(TEXT("在牌库 · "))+LDHUD::StoredPlaceText(Shown,Latest,LatestContent);
            StorageText(PopupWhere,View.KindLine.IsEmpty()?Where:View.KindLine+TEXT("\n")+Where);
            const FString Body=View.Keywords.IsEmpty()?View.Sentence:View.Sentence+TEXT("\n\n")+FString::Join(View.Keywords,TEXT("\n"));
            if(!PopupBody->GetText().ToString().Equals(Body,ESearchCase::CaseSensitive)){PopupBody->SetText(FText::FromString(Body));}
            const FString Links=LDHUD::ConnectionsText(Shown,Latest,LatestContent);
            const FString Missing=bInstalled?FString(LDTerms::InstalledCardHint):LDWorkshop::MissingPlace(Shown,Latest,LatestContent);
            StorageText(PopupHint,Links.IsEmpty()?Missing:Missing.IsEmpty()?Links:Links+TEXT("\n")+Missing);
            const FSlateRect Rect=StorageRect(Over);
            const FVector2D TopLeft=Local(FVector2D(Rect.Left,Rect.Top));const FVector2D BottomRight=Local(FVector2D(Rect.Right,Rect.Bottom));
            const FVector2D Size=PopupFrame->GetDesiredSize();
            const bool bRight=BottomRight.X+8.f+StoragePopupWidth<=Area.X-StorageScreenMargin;
            // Over a card in the list the popup goes left of the list, so it never covers the cards being browsed.
            const bool bCard=!bInstalled;
            const FSlateRect List=StorageRect(ListFrame);
            const float LeftEdge=bCard?float(Local(FVector2D(List.Left,List.Top)).X):float(TopLeft.X);
            const float X=bRight&&!bCard?float(BottomRight.X)+8.f:LeftEdge-8.f;
            const float Y=FMath::Max(float(BottomRight.Y),float(Size.Y)+StorageScreenMargin);
            StoragePlace(PopupSlot,FVector2D(X,FMath::Min(Y,float(Area.Y)-StorageScreenMargin)),FVector2D(bRight&&!bCard?0.f:1.f,1));
        }
    }
    // The chooser sits over the drop point; the held component follows the pointer, grown a little; a sent one waits,
    // translucent, where it was let go until its answer arrives.
    if(Chooser.bOpen)
    {
        const FVector2D Size=ChooserFrame->GetDesiredSize();const FVector2D At=Local(Chooser.At);
        const float X=FMath::Clamp(float(At.X),float(Size.X)*.5f+StorageScreenMargin,FMath::Max(float(Size.X)*.5f+StorageScreenMargin,float(Area.X-Size.X*.5f)-StorageScreenMargin));
        StoragePlace(ChooserSlot,FVector2D(X,FMath::Max(float(At.Y)-12.f,float(Size.Y)+StorageScreenMargin)),FVector2D(.5f,1));
    }
    if(QuickEquip.bOpen)
    {
        FVector2D AnchorAbs=QuickEquip.At;
        for(const FCard& Card:Cards)
        {
            if(Card.Component==QuickEquip.Component&&Card.Face&&Card.Face->IsVisible())
            {
                const FSlateRect Rect=StorageRect(Card.Face);
                if(StorageHasArea(Rect)){AnchorAbs=FVector2D((Rect.Left+Rect.Right)*.5f,Rect.Top);}
                break;
            }
        }
        const FVector2D Size=QuickFrame->GetDesiredSize();const FVector2D At=Local(AnchorAbs);
        const float X=FMath::Clamp(float(At.X),float(Size.X)*.5f+StorageScreenMargin,FMath::Max(float(Size.X)*.5f+StorageScreenMargin,float(Area.X-Size.X*.5f)-StorageScreenMargin));
        StoragePlace(QuickSlot,FVector2D(X,FMath::Max(float(At.Y)-8.f,float(Size.Y)+StorageScreenMargin)),FVector2D(.5f,1));
    }
    if(ReplacePick.bOpen)
    {
        const int32 ColIdx=int32(ReplacePick.Column);
        FVector2D TopCenter(Area.X*.5f,100.f);
        if(Workshop.bActive&&StorageHasArea(Workshop.Columns[ColIdx]))
        {
            const FSlateRect& ColRect=Workshop.Columns[ColIdx];
            TopCenter=Local(FVector2D((ColRect.Left+ColRect.Right)*.5f,ColRect.Top-8.f));
        }
        else if(Columns.IsValidIndex(ColIdx)&&Columns[ColIdx].Frame&&Columns[ColIdx].Frame->IsVisible())
        {
            const FSlateRect ColRect=StorageRect(Columns[ColIdx].Frame);
            TopCenter=Local(FVector2D((ColRect.Left+ColRect.Right)*.5f,ColRect.Top-8.f));
        }
        const FVector2D Size=ReplacePromptFrame->GetDesiredSize();
        const float X=FMath::Clamp(float(TopCenter.X),float(Size.X)*.5f+StorageScreenMargin,FMath::Max(float(Size.X)*.5f+StorageScreenMargin,float(Area.X-Size.X*.5f)-StorageScreenMargin));
        StoragePlace(ReplacePromptSlot,FVector2D(X,FMath::Max(float(TopCenter.Y),float(Size.Y)+StorageScreenMargin)),FVector2D(.5f,1));
    }
    const bool bWaiting=!Drag.bActive&&HasPendingRequest()&&bPendingAt&&!PendingComponent.IsNone();
    const FName GhostOf=Drag.bActive?Drag.Component:bWaiting?PendingComponent:NAME_None;
    if(GhostOf!=GhostShowing){GhostShowing=GhostOf;if(!GhostOf.IsNone()){ShowIcon(GhostPicture,GhostGlyph,GhostOf);}}
    StorageShown(GhostFrame,!GhostOf.IsNone(),ESlateVisibility::HitTestInvisible);
    if(Drag.bActive)
    {
        StoragePlace(GhostSlot,Local(Pointer),FVector2D(.5f,.5f));
        const float Grow=FMath::Lerp(1.f,LDHUDStyle::PickupScale,LDTween::Progress(UiNow()-Drag.Since,LDHUDStyle::MotionPickup,LDTween::ECurve::EaseOut));
        LDTween::SetScale(GhostFrame,Grow);LDTween::SetOpacity(GhostFrame,.85f);
        // Over a column or tile the caption names what letting go there does; elsewhere it hides.
        LDHUD::EStorageColumn Column=LDHUD::EStorageColumn::Body;FName Host;
        const bool bOverPlace=!StorageAt(Pointer)&&ColumnAt(Pointer,Column,Host);
        if(bOverPlace)
        {
            const LDHUD::FCardDrop* Drop=CurrentFocus.bOverDrop?&CurrentFocus.Drop:nullptr;
            const FString DropText=Drop?LDHUD::DropPreviewText(Drag.Component,*Drop,Latest,LatestContent):FString(LDTerms::DropNowhere);
            const bool bPreview=Drop&&PreviewCaptionComponent==Drag.Component&&PreviewCaptionDrop==DropText&&!PreviewCaption.IsEmpty();
            StorageText(GhostCaption,bPreview?DropText+TEXT(" · ")+PreviewCaption:DropText);
            GhostCaption->SetColorAndOpacity(FSlateColor(!Drop?LDHUDStyle::TextSecondary:Drop->Replacing.IsNone()?LDHUDStyle::UILegal:LDHUDStyle::UIReplace));
            const FVector2D Desired=GhostCaptionFrame->GetDesiredSize();
            const float Width=FMath::Min(float(Desired.X),FMath::Max(1.f,float(Area.X)-2.f*StorageScreenMargin));
            GhostCaptionSlot->SetAutoSize(false);GhostCaptionSlot->SetSize(FVector2D(Width,Desired.Y));
            const FVector2D At=Local(Pointer)+FVector2D(StorageTile*.5f+10.f,0);
            StoragePlace(GhostCaptionSlot,FVector2D(FMath::Clamp(float(At.X),StorageScreenMargin,FMath::Max(StorageScreenMargin,float(Area.X)-StorageScreenMargin-Width)),
                FMath::Clamp(float(At.Y),StorageScreenMargin+float(Desired.Y)*.5f,FMath::Max(StorageScreenMargin+float(Desired.Y)*.5f,float(Area.Y)-StorageScreenMargin-float(Desired.Y)*.5f))),FVector2D(0,.5f));
        }
        StorageShown(GhostCaptionFrame,bOverPlace,ESlateVisibility::HitTestInvisible);
    }
    else
    {
        StorageShown(GhostCaptionFrame,false);
        if(bWaiting){StoragePlace(GhostSlot,Local(PendingAt),FVector2D(.5f,.5f));LDTween::SetScale(GhostFrame,1.f);LDTween::SetOpacity(GhostFrame,.5f);}
    }
}
bool ULDComponentStorage::TileBox(FName Component,FSlateRect& Out) const
{
    for(const FTile& Tile:Tiles)
    {
        if(Tile.Component!=Component||!Tile.Face||!Columns.IsValidIndex(int32(Tile.Column))||!Columns[int32(Tile.Column)].Frame->IsVisible()){continue;}
        Out=StorageRect(Tile.Face);
        return StorageHasArea(Out);
    }
    return false;
}
bool ULDComponentStorage::HeldBox(FSlateRect& Out) const
{
    if(!Drag.bActive||!GhostFrame||!GhostFrame->IsVisible()){return false;}
    Out=StorageRect(GhostFrame);
    return StorageHasArea(Out);
}
bool ULDComponentStorage::Drags(const TArray<LDHUD::FCardDrop>& Drops,LDHUD::EStorageColumn Column,bool bOwnOnly)
{
    return Drops.ContainsByPredicate([Column,bOwnOnly](const LDHUD::FCardDrop& Drop)
    {return Drop.Column==Column&&Drop.Replacing.IsNone()&&(!bOwnOnly||Drop.Host.IsNone());});
}
void ULDComponentStorage::LayoutWorkshop(const FGeometry& Geometry,FVector2D Pointer)
{
    const auto Local=[&Geometry](FVector2D Absolute){return Geometry.AbsoluteToLocal(Absolute);};
    for(UWidget* Hidden:{StackFrame.Get(),ListFrame.Get(),PopupFrame.Get()}){StorageShown(Hidden,false);}
    // The library: the current page's kind groups in its rectangle. A component without a place dims.
    const bool bLibrary=StorageHasArea(Workshop.Library);
    StorageShown(LibraryFrame,bLibrary,ESlateVisibility::Visible);
    if(bLibrary)
    {
        const FVector2D TopLeft=Local(FVector2D(Workshop.Library.Left,Workshop.Library.Top));
        const FVector2D BottomRight=Local(FVector2D(Workshop.Library.Right,Workshop.Library.Bottom));
        StoragePlace(LibrarySlot,TopLeft,FVector2D(0,0));
        if(USizeBox* Width=Cast<USizeBox>(LibraryFrame.Get());Width&&!FMath::IsNearlyEqual(Width->GetWidthOverride(),float(BottomRight.X-TopLeft.X),.5f))
        {Width->SetWidthOverride(float(BottomRight.X-TopLeft.X));}
        for(UWrapBox* Wrap:LibraryWraps){if(!FMath::IsNearlyEqual(Wrap->GetWrapSize(),float(BottomRight.X-TopLeft.X),.5f)){Wrap->SetExplicitWrapSize(true);Wrap->SetWrapSize(float(BottomRight.X-TopLeft.X));}}
        for(FCard& Card:Cards)
        {
            LDTween::SetOpacity(Card.Cell,Card.State==EFaceState::Held?.35f:Card.bPlaceless?.55f:1.f);
        }
    }
    // The columns: each in its board rectangle under the head the workshop draws. A column that takes nothing of the hovered
    // or held component dims; the empty place at its end shows while the workshop has room (or the component goes there).
    const bool bRoom=LatestModel.Installed<LatestModel.Capacity;
    for(int32 Index=0;Index<Columns.Num();++Index)
    {
        FColumn& Column=Columns[Index];
        const FSlateRect& Rect=Workshop.Columns[Index];
        const bool bShown=StorageHasArea(Rect);
        StorageShown(Column.Frame,bShown,ESlateVisibility::Visible);
        if(!bShown){continue;}
        const FVector2D TopLeft=Local(FVector2D(Rect.Left,Rect.Top+Workshop.HeadHeight));
        const FVector2D BottomRight=Local(FVector2D(Rect.Right,Rect.Bottom));
        StoragePlace(Column.Slot,TopLeft,FVector2D(0,0));
        const FVector2D Size=BottomRight-TopLeft;
        if(!FMath::IsNearlyEqual(Column.Frame->GetWidthOverride(),float(Size.X),.5f)){Column.Frame->SetWidthOverride(float(Size.X));}
        if(!FMath::IsNearlyEqual(Column.Frame->GetHeightOverride(),float(Size.Y),.5f)){Column.Frame->SetHeightOverride(float(Size.Y));}
        const float GridWidth=FMath::Max(1.f,float(Size.X)-16.f);
        if(!FMath::IsNearlyEqual(Column.DenseList->GetWrapSize(),GridWidth,.5f)){Column.DenseList->SetWrapSize(GridWidth);}
        const LDHUD::EStorageColumn Id=static_cast<LDHUD::EStorageColumn>(Index);
        const TArray<LDHUD::FCardDrop>& Drops=CurrentFocus.Drops;
        const bool bOwnDrop=Drags(Drops,Id,true);
        const bool bTakes=Drops.ContainsByPredicate([Id](const LDHUD::FCardDrop& Drop){return Drop.Column==Id;});
        const bool bEmptySlot=Index>=int32(LDHUD::EStorageColumn::SlotQ)&&Index<=int32(LDHUD::EStorageColumn::SlotE)&&Column.Active.IsNone();
        StorageShown(Column.Placeholder,!bEmptySlot&&(bRoom||bOwnDrop),ESlateVisibility::HitTestInvisible);
        const int32 Look=Drops.IsEmpty()?0:bTakes?1:2;
        if(Column.AppliedLook!=Look)
        {
            Column.AppliedLook=Look;
            Column.Panel->SetBrush(StorageRound(8.f,FLinearColor::Transparent,0.f));Column.Panel->SetBrushColor(FLinearColor::Transparent);
            // .sock: rows hang from the left on the column's rail, 10 in from its edge.
            Column.Panel->SetPadding(FMargin(10,0,6,0));
            if(UVerticalBoxSlot* RowsSlot=Cast<UVerticalBoxSlot>(Column.List->Slot)){RowsSlot->SetHorizontalAlignment(HAlign_Fill);}
            if(UVerticalBoxSlot* EmptySlot=Cast<UVerticalBoxSlot>(Column.PlaceholderBox->Slot)){EmptySlot->SetHorizontalAlignment(HAlign_Left);EmptySlot->SetPadding(FMargin(0,14,0,0));}
            StorageShown(Column.Title,false);
            Column.Frame->SetRenderOpacity(Look==2?.3f:1.f);
        }
    }
}
void ULDComponentStorage::PaintTiles(const LDUIPaint::FContext& Paint) const
{
    if(!bShownNow){return;}
    const FGeometry& Geometry=Paint.Geometry;
    const int32 Layer=Paint.Layer;
    const double Now=UiNow();
    const auto TileState=[](EFaceState State)
    {
        switch(State)
        {
        case EFaceState::Hovered: return LDUIPaint::ETileState::Hovered;
        case EFaceState::Held: return LDUIPaint::ETileState::Selected;
        case EFaceState::Legal: return LDUIPaint::ETileState::Legal;
        case EFaceState::Replace: case EFaceState::Detached: return LDUIPaint::ETileState::Replace;
        case EFaceState::Rejected: return LDUIPaint::ETileState::Rejected;
        case EFaceState::Dim: return LDUIPaint::ETileState::Dim;
        default: return LDUIPaint::ETileState::Normal;
        }
    };
    // A tile is painted in its face's own geometry, so the face's hover lift and refusal shake carry the painting with them.
    const auto PaintFace=[&](const UWidget* Face,uint8 Kind,EFaceState State,UTexture2D* Icon,float Opacity,bool bFresh)
    {
        if(!Face||!Face->IsVisible()){return;}
        const FGeometry& FaceGeometry=Face->GetCachedGeometry();
        const FVector2D Size=FaceGeometry.GetLocalSize();
        if(Size.X<4.){return;}
        const FBox2D Box(FVector2D::ZeroVector,Size);
        const LDUIPaint::FContext FacePaint=Paint.WithGeometry(FaceGeometry).WithOpacity(Opacity*Face->GetRenderOpacity());
        FacePaint.Tile(Layer,Box,Kind,TileState(State),Icon,1.f,Now);
        if(bFresh){FacePaint.NewDot(Layer+4,FVector2D(Size.X+3.,-3.));}
    };
    const bool bCardsShown=Workshop.bActive?(LibraryFrame&&LibraryFrame->IsVisible()):(ListFrame&&ListFrame->IsVisible());
    if(bCardsShown)
    {
        for(const FCard& Card:Cards)
        {
            const float Opacity=Card.Cell?Card.Cell->GetRenderOpacity():Card.Size?Card.Size->GetRenderOpacity():1.f;
            PaintFace(Card.Face,Card.Kind,Card.State,Card.Icon.Get(),Opacity,Workshop.bActive&&Card.bFresh);
        }
        if(Workshop.bActive)
        {
            // .grp: a mark before each form group's name and the hairline after its count.
            for(int32 Kind=0;Kind<LibrarySwatches.Num()&&Kind<LibraryHeads.Num();++Kind)
            {
                const UWidget* Swatch=LibrarySwatches[Kind];const UWidget* Head=LibraryHeads[Kind];
                if(!Swatch||!Swatch->IsVisible()||!Head||!Head->IsVisible()||!LibraryGroups[Kind]->IsVisible()){continue;}
                const FBox2D Mark=LDUIShapes::RectIn(Geometry,Swatch->GetCachedGeometry());
                Paint.Shape(Layer,LDUIShapes::EShape::Rounded,Mark,2.f,LDUIPaint::FFill::Solid(LDHUDStyle::TextSecondary));
                const FBox2D Row=LDUIShapes::RectIn(Geometry,Head->GetCachedGeometry());
                const float From=float(Row.Min.X)+float(Head->GetDesiredSize().X)+10.f,Y=float(Mark.GetCenter().Y);
                const float To=LibraryFrame?float(LDUIShapes::RectIn(Geometry,LibraryFrame->GetCachedGeometry()).Max.X)-4.f:From;
                if(To>From){Paint.Line(Layer,FVector2D(From,Y),FVector2D(To,Y),FLinearColor(1,1,1,.075f),1.f);}
            }
        }
    }
    for(const FTile& Tile:Tiles)
    {
        const int32 Index=int32(Tile.Column);
        if(!Columns.IsValidIndex(Index)||!Columns[Index].Frame||!Columns[Index].Frame->IsVisible()){continue;}
        const float Opacity=Columns[Index].Frame->GetRenderOpacity();
        PaintFace(Tile.Face,Tile.Kind,Tile.State,Tile.Icon.Get(),Opacity,false);
        if(Tile.MedalTier>=2&&Tile.Face&&Tile.Face->IsVisible())
        {
            const FGeometry& FaceGeometry=Tile.Face->GetCachedGeometry();
            const FVector2D Size=FaceGeometry.GetLocalSize();
            if(Size.X>=4.)
            {
                const float Pulse=.72f+.28f*FMath::Sin(float(Now*(Tile.MedalTier>=3?5.2:3.4)));
                const FLinearColor Halo=Tile.MedalTier>=3
                    ?FLinearColor(.55f,1.f,.95f,Pulse)
                    :FLinearColor(1.f,.78f,.22f,Pulse);
                const LDUIPaint::FContext FacePaint=Paint.WithGeometry(FaceGeometry).WithOpacity(Opacity*Tile.Face->GetRenderOpacity());
                const FBox2D AuraBox(FVector2D(-2.5f,-2.5f),Size+FVector2D(2.5f,2.5f));
                const float AuraRadius=FaceRadius(Tile.Kind,float(Size.X))+2.5f;
                FacePaint.Halo(Layer+2,LDUIShapes::EShape::Rounded,AuraBox,AuraRadius,Halo.CopyWithNewOpacity(Pulse*.42f),Tile.MedalTier>=3?7.f:4.5f);
                FacePaint.Edge(Layer+3,LDUIShapes::EShape::Rounded,AuraBox,AuraRadius,Halo,Tile.MedalTier>=3?2.4f:1.8f);
            }
        }
    }
}
int32 ULDComponentStorage::NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Culling,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool bEnabled) const
{
    const int32 Base=Super::NativePaint(Args,Geometry,Culling,Elements,Layer,Style,bEnabled);
    if(!bShownNow){return Base;}
    const double Now=UiNow();
    const int32 Top=Base+1;
    const LDUIPaint::FContext Paint(Elements,Top,Geometry,Style.GetColorAndOpacityTint().A);
    for(const FTile& Tile:Tiles)
    {
        const int32 Index=int32(Tile.Column);
        if(!Columns.IsValidIndex(Index)||!Columns[Index].Frame||!Columns[Index].Frame->IsVisible()){continue;}
        // An install that just landed flashes once: a ring spreading out from it.
        const double Age=Now-LandedAt;
        if(Tile.Component==LandedComponent&&Age>=0.&&Age<LDHUDStyle::MotionInstallLand+LDHUDStyle::MotionInstallSettle)
        {
            const float T=LDTween::Progress(Age,LDHUDStyle::MotionInstallLand+LDHUDStyle::MotionInstallSettle,LDTween::ECurve::EaseOut);
            const FBox2D Box=LDUIShapes::RectIn(Geometry,Tile.Face->GetCachedGeometry());
            const FVector2D Grow(10.f*T,10.f*T);
            Paint.WithOpacity(Columns[Index].Frame->GetRenderOpacity()).Frame(Top,LDUIShapes::KindShape(Tile.Kind),FBox2D(Box.Min-Grow,Box.Max+Grow),
                LDHUDStyle::UILegal.CopyWithNewOpacity(1.f-T),2.f,
                FaceRadius(Tile.Kind,float(Box.GetSize().X))+10.f*T);
        }
    }
    if(Workshop.bActive)
    {
        // The empty place at the end of a column: dashed, solid green while the held component can go there.
        for(int32 Index=0;Index<Columns.Num();++Index)
        {
            const FColumn& Column=Columns[Index];
            if(!Column.Frame||!Column.Frame->IsVisible()||!Column.Placeholder||!Column.Placeholder->IsVisible()){continue;}
            // .sock.empty .slot: a dashed rounded place with a thin plus; solid green and glowing while the held component
            // can go there.
            const FBox2D Box=LDUIShapes::RectIn(Geometry,Column.Placeholder->GetCachedGeometry());
            const LDUIPaint::FContext ColumnPaint=Paint.WithOpacity(Column.Frame->GetRenderOpacity());
            const bool bLegal=Drags(CurrentFocus.Drops,static_cast<LDHUD::EStorageColumn>(Index),true);
            if(bLegal)
            {
                const float Pulse=CurrentFocus.bHeld?.7f+.3f*LDTween::PingPong(Now,.9):1.f;
                ColumnPaint.Halo(Top,LDUIShapes::EShape::Rounded,Box,10.f,LDHUDStyle::UILegal.CopyWithNewOpacity(.55f*Pulse),18.f);
                ColumnPaint.Shape(Top+1,LDUIShapes::EShape::Rounded,Box,10.f,LDUIPaint::FFill::Solid(LDHUDStyle::UILegal.CopyWithNewOpacity(.12f)));
                ColumnPaint.Edge(Top+1,LDUIShapes::EShape::Rounded,Box,10.f,LDHUDStyle::UILegal,2.f);
            }
            else{ColumnPaint.DashedEdge(Top,LDUIShapes::EShape::Rounded,Box,10.f,FLinearColor(1,1,1,.18f),2.f);}
            const FVector2D Center=Box.GetCenter();
            const FLinearColor Plus=bLegal?LDHUDStyle::UILegal:FLinearColor(1,1,1,.25f);
            ColumnPaint.Line(Top+2,Center-FVector2D(8,0),Center+FVector2D(8,0),Plus,1.5f);
            ColumnPaint.Line(Top+2,Center-FVector2D(0,8),Center+FVector2D(0,8),Plus,1.5f);
        }
    }
    if(!Workshop.bActive&&StackFrame&&StackFrame->IsVisible())
    {
        // Over each skill slot, one small mark per installed component in its kind's colour and shape, so the build reads
        // without opening anything; the mini capacity bar sits over the stack.
        if(!bExpanded&&Anchors.bValid)
        {
            for(int32 Index=0;Index<LatestModel.Columns.Num()&&Index<LDHUD::StorageColumnCount;++Index)
            {
                const FSlateRect& Anchor=Anchors.Columns[Index];
                const TArray<LDHUD::FEquippedView>& Entries=LatestModel.Columns[Index].Entries;
                if(!StorageHasArea(Anchor)||Entries.IsEmpty()){continue;}
                const FBox2D Over=LDUIShapes::AbsoluteIn(Geometry,Anchor);
                const int32 Count=FMath::Min(Entries.Num(),7);
                const float Mark=8.f,Gap=3.f;
                const float X0=float(Over.GetCenter().X)-(Count*Mark+(Count-1)*Gap)*.5f,Y=float(Over.Min.Y)-Mark-4.f;
                for(int32 Entry=0;Entry<Count;++Entry)
                {
                    const uint8 Kind=uint8(LDWorkshop::CardFacts(Entries[Entry].Card,LatestContent).Type);
                    const FBox2D Box(FVector2D(X0+Entry*(Mark+Gap),Y),FVector2D(X0+Entry*(Mark+Gap)+Mark,Y+Mark));
                    Paint.Fill(Top,Box,LDUIShapes::KindColor(Kind),Kind==4?Mark*.5f:Kind==0?1.5f:2.f);
                }
            }
        }
        const FBox2D Stack=LDUIShapes::RectIn(Geometry,StackFrame->GetCachedGeometry());
        const LDWorkshop::FCapacityView Capacity=LDWorkshop::Capacity(Latest,LatestModel,LatestContent);
        for(int32 Cell=0;Cell<Capacity.Capacity&&Cell<16;++Cell)
        {
            const float X=float(Stack.Min.X)+Cell*19.f,Y=float(Stack.Min.Y)-14.f;
            const FLinearColor Color=Capacity.Used.IsValidIndex(Cell)?LDUIShapes::KindColor(uint8(Capacity.Used[Cell])):LDHUDStyle::UIRaised;
            Paint.Fill(Top,FBox2D(FVector2D(X,Y),FVector2D(X+16.f,Y+9.f)),Color,1.5f);
        }
        // A received card's icon arcs from the screen centre into the stack; its name then stays over the stack.
        const FVector2D Size=Geometry.GetLocalSize();
        for(const FFly& Fly:Flies)
        {
            const double Age=Now-Fly.At;
            const FVector2D From(Size.X*.5f,Size.Y*.42f),To=Stack.GetCenter();
            const float T=LDTween::Progress(Age,LDHUDStyle::MotionGain,LDTween::ECurve::EaseOut);
            if(T<1.f)
            {
                const FVector2D Control((From.X+To.X)*.5f,FMath::Min(From.Y,To.Y)-160.f);
                const FVector2D At=FMath::Lerp(FMath::Lerp(From,Control,T),FMath::Lerp(Control,To,T),T);
                const float Icon=FMath::Lerp(56.f,36.f,T);
                FSlateBrush Brush;Brush.SetResourceObject(LDUIIcons::Find(LDHUD::ContentIconId(LatestContent,LDHUD::CardDefinition(Fly.Component).ToString())));
                Brush.ImageSize=FVector2D(Icon,Icon);Brush.DrawAs=ESlateBrushDrawType::Image;
                if(Brush.GetResourceObject()){FSlateDrawElement::MakeBox(Elements,Top+1,Geometry.ToPaintGeometry(FVector2D(Icon,Icon),FSlateLayoutTransform(At-FVector2D(Icon,Icon)*.5f)),
                    &Brush,ESlateDrawEffect::None,Paint.Fade(FLinearColor::White));}
                Paint.Frame(Top+1,LDUIShapes::EShape::Rounded,FBox2D(At-FVector2D(Icon,Icon)*.5f,At+FVector2D(Icon,Icon)*.5f),LDHUDStyle::UIAction,2.f,6.f);
            }
            else if(Fly.bStrip&&Age<LDHUDStyle::MotionGain+1.5)
            {
                const FString Name=ComponentName(Fly.Component);
                const FSlateFontInfo Font=LDUIFonts::Font(LDUIFonts::ERole::Strong,LDHUDStyle::TypeSmall);
                const FVector2D Strip(FMath::Max(80.f,Name.Len()*15.f+24.f),26.f);
                const FVector2D Min(float(Stack.GetCenter().X)-Strip.X*.5f,float(Stack.Min.Y)-46.f);
                Paint.Fill(Top+1,FBox2D(Min,Min+Strip),LDHUDStyle::UIRaised.CopyWithNewOpacity(.95f),LDHUDStyle::RadiusButton);
                Paint.Frame(Top+1,LDUIShapes::EShape::Rounded,FBox2D(Min,Min+Strip),LDHUDStyle::UIAction,1.5f,LDHUDStyle::RadiusButton);
                FSlateDrawElement::MakeText(Elements,Top+2,Geometry.ToPaintGeometry(Strip,FSlateLayoutTransform(Min+FVector2D(12.f,3.f))),Name,Font,ESlateDrawEffect::None,
                    Paint.Fade(LDHUDStyle::UIText));
            }
        }
    }
    // A sent card waits where it was let go with a thin turning ring until the answer arrives.
    if(GhostFrame&&GhostFrame->IsVisible()&&!Drag.bActive)
    {
        const FBox2D Box=LDUIShapes::RectIn(Geometry,GhostFrame->GetCachedGeometry());
        Paint.Arc(Top+1,Box.GetCenter(),float(Box.GetSize().X)*.62f,.3f,LDHUDStyle::UIAction,2.f,float(FMath::Fmod(Now*1.4,1.)));
    }
    return Top+1;
}

void ULDComponentStorage::ApplyTextScale(float Scale)
{
    if(Scale<=0.f||!WidgetTree||FMath::IsNearlyEqual(Scale,TextScale)){return;}
    const float Ratio=Scale/TextScale;TextScale=Scale;
    WidgetTree->ForEachWidget([Ratio](UWidget* Widget)
    {
        if(auto* Text=Cast<UTextBlock>(Widget)){FSlateFontInfo Font=Text->GetFont();Font.Size=FMath::Max(1.f,FMath::RoundToFloat(float(Font.Size)*Ratio));Text->SetFont(Font);}
        else if(auto* Rich=Cast<URichTextBlock>(Widget)){LDWidgets::SetRichTextSize(Rich,FMath::Max(1,FMath::RoundToInt(float(LDWidgets::RichTextSize(Rich))*Ratio)));}
    });
}
