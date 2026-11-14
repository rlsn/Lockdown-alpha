#include "UI/LDComponentStorage.h"
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
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Framework/Application/SlateApplication.h"
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
const FLDComponentInstanceView* StorageOwned(const FLDProgressionView& View,FName Id)
{return View.Components.FindByPredicate([Id](const FLDComponentInstanceView& Owned){return Owned.DefinitionId==Id;});}
}

TSharedRef<SWidget> ULDComponentStorage::RebuildWidget()
{
    SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    if(!Canvas)
    {
        Canvas=WidgetTree->ConstructWidget<UCanvasPanel>();Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);WidgetTree->RootWidget=Canvas;
        auto Line=[this](const FString& Value,int32 Size,const FLinearColor& Ink)
        {UTextBlock* Label=LDWidgets::Text(WidgetTree,Value,Size);Label->SetAutoWrapText(false);Label->SetColorAndOpacity(Ink);return Label;};
        auto Add=[this](UWidget* Widget,int32 Z)
        {UCanvasPanelSlot* Slot=Canvas->AddChildToCanvas(Widget);Slot->SetAutoSize(true);Slot->SetZOrder(Z);return Slot;};
        {
            // The stack: two card backs behind the newest stored component's icon, its count and the 新 badge, then the
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
            // The open list rises from the stack: only the icons of the stored components in rows, the newest last, nearest
            // the stack; no title or instructions (the layout and the popups say it). An empty storage opens no list.
            auto* Box=WidgetTree->ConstructWidget<UVerticalBox>();
            CardList=WidgetTree->ConstructWidget<UWrapBox>();CardList->SetExplicitWrapSize(true);CardList->SetWrapSize(StorageListWidth-20.f);
            CardList->SetInnerSlotPadding(FVector2D(StorageCardGap,StorageCardGap));Box->AddChildToVerticalBox(CardList);
            auto* Panel=LDWidgets::Panel(WidgetTree,Box,FMargin(10,8));Panel->SetBrushColor(LDHUDStyle::PanelFill.CopyWithNewOpacity(.9f));
            auto* Width=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("HUDComponentStorageList"));
            Width->SetWidthOverride(StorageListWidth);Width->SetContent(Panel);
            ListFrame=Width;ListSlot=Add(Width,3);ListFrame->SetVisibility(ESlateVisibility::Collapsed);
        }
        // The equipped columns, in LDHUD::EStorageColumn order: the list of tiles, the drop placeholder and, for 本体 only, the
        // title (a slot column stands on its own Q/W/E or D slot, whose key the player's bindings decide).
        for(int32 Index=0;Index<LDHUD::StorageColumnCount;++Index)
        {
            FColumn& Column=Columns.AddDefaulted_GetRef();
            auto* Box=WidgetTree->ConstructWidget<UVerticalBox>();
            Column.List=WidgetTree->ConstructWidget<UVerticalBox>();Box->AddChildToVerticalBox(Column.List)->SetHorizontalAlignment(HAlign_Center);
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
            Column.Panel=WidgetTree->ConstructWidget<UBorder>();Column.Panel->SetBrush(StorageRound(8.f,LDHUDStyle::PanelRim,1.f));
            Column.Panel->SetBrushColor(LDHUDStyle::ColumnFill);Column.Panel->SetPadding(FMargin(4,6));Column.Panel->SetContent(Box);
            Column.Frame=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),FName(*FString::Printf(TEXT("HUDStorageColumn%d"),Index)));
            Column.Frame->SetWidthOverride(StorageColumnWidth);Column.Frame->SetClipping(EWidgetClipping::ClipToBounds);Column.Frame->SetContent(Column.Panel);Column.Frame->SetVisibility(ESlateVisibility::Collapsed);
            Column.Slot=Add(Column.Frame,2);
        }
        {
            // The workshop library: one group per kind, its title and count over a wrap of icon cells; it never scrolls (the
            // cells shrink with the count, LDWorkshop::LibraryTier).
            LibraryBox=WidgetTree->ConstructWidget<UVerticalBox>();
            for(uint8 Kind=0;Kind<3;++Kind)
            {
                auto* Group=WidgetTree->ConstructWidget<UVerticalBox>();
                auto* Head=WidgetTree->ConstructWidget<UHorizontalBox>();
                auto* Swatch=WidgetTree->ConstructWidget<UBorder>();Swatch->SetBrush(StorageRound(2.f,FLinearColor::Transparent,0.f));
                Swatch->SetBrushColor(LDUIShapes::KindColor(Kind));
                auto* SwatchBox=WidgetTree->ConstructWidget<USizeBox>();SwatchBox->SetWidthOverride(9.f);SwatchBox->SetHeightOverride(9.f);SwatchBox->SetContent(Swatch);
                Head->AddChildToHorizontalBox(SwatchBox)->SetVerticalAlignment(VAlign_Center);
                UTextBlock* Name=Line(LDTerms::ComponentKindName(Kind),LDHUDStyle::TypeCaption,LDHUDStyle::TextSecondary);
                Head->AddChildToHorizontalBox(Name)->SetPadding(FMargin(8,0,0,0));
                UTextBlock* Count=Line(TEXT(""),LDHUDStyle::TypeCaption,LDHUDStyle::TextSecondary);
                Head->AddChildToHorizontalBox(Count)->SetPadding(FMargin(10,0,0,0));
                Group->AddChildToVerticalBox(Head);
                auto* Wrap=WidgetTree->ConstructWidget<UWrapBox>();Wrap->SetInnerSlotPadding(FVector2D(StorageCardGap+2.f,StorageCardGap+2.f));
                Group->AddChildToVerticalBox(Wrap)->SetPadding(FMargin(0,8,0,0));
                LibraryBox->AddChildToVerticalBox(Group)->SetPadding(FMargin(0,Kind?14.f:0.f,0,0));
                LibraryGroups.Add(Group);LibraryCounts.Add(Count);LibraryWraps.Add(Wrap);
            }
            auto* Width=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("WorkshopLibrary"));Width->SetContent(LibraryBox);
            Width->SetVisibility(ESlateVisibility::Collapsed);LibraryFrame=Width;LibrarySlot=Add(Width,3);
        }
        {
            // The description of the hovered component, in the slot popup's look; it takes no pointer input.
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
            // The chooser a B18 or C12 drop opens at the drop point: one button per element or base, then 取消.
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
            // The dragged component's ghost follows the pointer; it takes no input.
            UImage* Picture=nullptr;UTextBlock* Glyph=nullptr;UOverlay* Face=nullptr;USizeBox* Box=nullptr;
            UBorder* Ghost=IconFace(StorageTile,Picture,Glyph,Face,Box);Ghost->SetBrush(StorageRound(6.f,LDHUDStyle::TextAccent,2.f));
            GhostPicture=Picture;GhostGlyph=Glyph;Box->SetRenderOpacity(.85f);Box->SetVisibility(ESlateVisibility::Collapsed);
            GhostFrame=Box;GhostSlot=Add(Box,8);GhostSlot->SetAlignment(FVector2D(.5f,.5f));
            GhostCaption=Line(TEXT(""),LDHUDStyle::TypeCaption,LDHUDStyle::TextPrimary);GhostCaption->SetAutoWrapText(false);
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
    // An empty stack shows a blank face: there is no component to name.
    if(Component.IsNone()){StorageText(Glyph,FString());StorageShown(Picture,false);return;}
    const LDHUD::FComponentCardView View=LDHUD::ComponentCard(Component,LatestContent,LatestStats);
    StorageText(Glyph,View.Glyph);
    const bool bPicture=LDUIIcons::Apply(Picture,View.IconId,Radius,bDim?LDHUDStyle::IconBlocked:FLinearColor::White);
    StorageShown(Glyph,!bPicture,ESlateVisibility::HitTestInvisible);
}
FString ULDComponentStorage::ComponentName(FName Component) const
{
    return LDHUD::ContentName(LatestContent,Component.ToString());
}

void ULDComponentStorage::Present(const FLDProgressionView* View,uint32 Match,LDProgression::EMatchStage Stage,ELDLifePhase LifePhase,
    const FLDDemoContentDefinitions* Content,const LDHUD::FCardStats& Stats,const FAnchors& InAnchors,bool bShown)
{
    if(!Canvas){return;}
    // Another match's answers never reach this one: its request, move and interaction end with it.
    if(Match!=LatestMatch){PendingRequest=0;Follow=FFollowUp();CancelInteraction();SeenComponents.Reset();}
    LatestMatch=Match;
    Latest=View?*View:FLDProgressionView();
    Gate=LDHUD::BuildGate(Stage,LifePhase,Latest.bBuildLocked);
    LatestContent=Content;LatestStats=Stats;Anchors=InAnchors;
    bShownNow=bShown&&Latest.bValid&&(Workshop.bActive||Anchors.bValid);
    if(!bShownNow){CancelInteraction();Press=FPress();bExpanded=false;}
    StorageShown(StackFrame,bShownNow&&!Workshop.bActive,ESlateVisibility::Visible);
    const LDHUD::FStorageView Model=LDHUD::ComponentStorage(Latest,Content,&SeenComponents);
    LatestModel=Model;
    {
        // New owned components fly into the stack (the workshop shows them with their yellow dot instead); a new match only
        // takes its first view as the baseline.
        TSet<FName> Owned;for(const FLDComponentInstanceView& Each:Latest.Components){Owned.Add(Each.DefinitionId);}
        if(KnownMatch==Match&&Latest.bValid)
        {
            for(const FLDComponentInstanceView& Each:Latest.Components)
            {
                if(KnownOwned.Contains(Each.DefinitionId)||Each.bInstalled||Workshop.bActive){continue;}
                FFly& Fly=Flies.AddDefaulted_GetRef();Fly.Component=Each.DefinitionId;Fly.At=UiNow();Fly.bStrip=!bEngaged;
            }
        }
        if(Latest.bValid){KnownOwned=MoveTemp(Owned);KnownMatch=Match;}
        Flies.RemoveAll([this](const FFly& Fly){return UiNow()-Fly.At>LDHUDStyle::MotionGain+1.5;});
    }
    // The cards and tiles are rebuilt only for another build, catalog or numbers.
    FString Key=FString::Printf(TEXT("%u|%llu|%p|%u|%lld|%lld|%lld|%lld"),Match,Latest.BuildRevision,Content,uint32(Stats.Level),
        int64(Stats.ADP2),int64(Stats.APP2),int64(Stats.MaxHPP2),int64(Stats.ArmorP2));
    for(const FName& Id:Model.Stored){Key+=TEXT("|")+Id.ToString();}
    if(Workshop.bActive)
    {
        Key+=FString::Printf(TEXT("|W%.0f|%.2f|%d"),Workshop.LibraryIcon>0.f?Workshop.LibraryIcon:LDWorkshop::LibraryTier(Model.Stored.Num()).Icon,Workshop.TileScale,int32(Workshop.bNames));
        for(const LDHUD::FStorageColumnView& Column:Model.Columns){Key+=FString::Printf(TEXT("|%d"),Column.Entries.Num()>5?1:0);}
    }
    for(const LDHUD::FStorageColumnView& Column:Model.Columns)
    {
        Key+=TEXT("#")+Column.Active.ToString();
        for(const LDHUD::FEquippedView& Entry:Column.Entries){Key+=TEXT("|")+Entry.Component.ToString()+TEXT("@")+Entry.Host.ToString()+Entry.Note;}
    }
    if(Key!=BuiltKey){BuiltKey=Key;Rebuild(Model);}
    // The stack face: the newest stored component on top, its count and the never-installed badge.
    const FName Top=Model.Stored.IsEmpty()?NAME_None:Model.Stored.Last();
    ShowIcon(StackPicture,StackGlyph,Top);
    StorageText(StackCount,FString::FromInt(Model.Stored.Num()));
    StorageText(StackCapacity,Model.Capacity>0?LDTerms::WorkshopCaption(Model.Installed,Model.Capacity):FString());
    // A full workshop reads in the accent colour: the next install needs a removal first.
    if(StackCapacity){StackCapacity->SetColorAndOpacity(Model.Capacity>0&&Model.Installed>=Model.Capacity?LDHUDStyle::TextAccent:LDHUDStyle::TextSecondary);}
    StorageText(StackFresh,LDTerms::StorageFresh(Model.Fresh));
    StorageShown(StackFreshChip,Model.Fresh>0,ESlateVisibility::HitTestInvisible);
    for(int32 Back=0;Back<StackBacks.Num();++Back){StorageShown(StackBacks[Back],Model.Stored.Num()>Back+1,ESlateVisibility::HitTestInvisible);}
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
            if(Notify){Notify(FString::Printf(TEXT("%s：没有移动完，%s 在构件库里"),LDTerms::Storage,*ComponentName(Follow.Component)));}
            Follow=FFollowUp();
        }
    }
}
void ULDComponentStorage::Rebuild(const LDHUD::FStorageView& Model)
{
    Built.Reset();Cards.Reset();Tiles.Reset();HoverDropsOf=NAME_None;HoverDrops.Reset();
    CardList->ClearChildren();
    for(UWrapBox* Wrap:LibraryWraps){Wrap->ClearChildren();}
    const float LibraryIcon=Workshop.LibraryIcon>0.f?Workshop.LibraryIcon:LDWorkshop::LibraryTier(Model.Stored.Num()).Icon;
    int32 KindCounts[3]={0,0,0};
    for(const FName& Id:Model.Stored)
    {
        const FLDComponentInstanceView* Owned=StorageOwned(Latest,Id);
        const bool bFresh=Owned&&!Owned->bEverInstalled&&!SeenComponents.Contains(Id);
        if(Workshop.bActive){BuildWorkshopCard(Id,bFresh,LibraryIcon);}else{BuildCard(Id,bFresh);}
        FCard& Card=Cards.Last();
        const LDWorkshop::FComponentFacts Facts=LDWorkshop::ComponentFacts(Id,LatestContent);
        Card.Kind=uint8(Facts.Kind);Card.bScarce=Facts.bScarce;Card.bFresh=bFresh;
        // A component with no place now dims and loses its colour; the dock says what it needs.
        Card.bPlaceless=LDHUD::ComponentDrops(Id,Latest,LatestContent).IsEmpty()&&LDHUD::ReplaceDrops(Id,Latest,LatestContent).IsEmpty();
        StyleFace(Card.Face,Id,Card.Kind,Workshop.bActive?LibraryIcon:StorageTile,Card.bPlaceless);
        if(Card.Kind<3){++KindCounts[Card.Kind];}
    }
    for(int32 Kind=0;Kind<LibraryCounts.Num();++Kind)
    {
        StorageText(LibraryCounts[Kind],FString::FromInt(KindCounts[Kind]));
        StorageShown(LibraryGroups[Kind],KindCounts[Kind]>0);
    }
    for(int32 Index=0;Index<Columns.Num()&&Model.Columns.IsValidIndex(Index);++Index)
    {
        FColumn& Column=Columns[Index];Column.List->ClearChildren();Column.AppliedLook=-1;
        const LDHUD::FStorageColumnView& Shown=Model.Columns[Index];
        // Only 本体 shows its title; the others keep theirs (the active's name or 冲刺) for the notices that name a place.
        StorageText(Column.Title,Shown.Title);Column.Active=Shown.Active;
        StorageShown(Column.Title,Index==int32(LDHUD::EStorageColumn::Body),ESlateVisibility::HitTestInvisible);
        // Workshop: more than five rows in a column shrink its icons, so the board never scrolls.
        const float Base=(Shown.Entries.Num()>5?46.f:56.f)*Workshop.TileScale;
        for(const LDHUD::FEquippedView& Entry:Shown.Entries)
        {
            if(Workshop.bActive){BuildWorkshopTile(Column,Entry,static_cast<LDHUD::EStorageColumn>(Index),Entry.Depth>0?Base*46.f/56.f:Base);}
            else{BuildTile(Column,Entry,static_cast<LDHUD::EStorageColumn>(Index));}
            FTile& Tile=Tiles.Last();
            const LDWorkshop::FComponentFacts Facts=LDWorkshop::ComponentFacts(Entry.Component,LatestContent);
            Tile.Kind=uint8(Facts.Kind);Tile.bScarce=Facts.bScarce;
            const float TileSize=Workshop.bActive?(Entry.Depth>0?Base*46.f/56.f:Base):(Entry.Depth>0?StorageNestedTile:StorageTile);
            StyleFace(Tile.Face,Entry.Component,Tile.Kind,TileSize,false);
        }
        // The empty place at the end of a column is the size of its icons.
        const float Empty=Workshop.bActive?Base:StorageTile;
        Column.PlaceholderBox->SetWidthOverride(Empty);Column.PlaceholderBox->SetHeightOverride(Workshop.bActive?Empty:StorageTile*.7f);
        StorageShown(Column.PlaceholderText,!Workshop.bActive,ESlateVisibility::HitTestInvisible);
        Column.PlaceholderFace->SetBrushColor(Workshop.bActive?FLinearColor::Transparent:LDHUDStyle::DropLegal.CopyWithNewOpacity(.10f));
        Column.PlaceholderFace->SetBrush(StorageRound(6.f,Workshop.bActive?FLinearColor::Transparent:LDHUDStyle::DropLegal,Workshop.bActive?0.f:1.f));
    }
}
void ULDComponentStorage::StyleFace(UBorder* Face,FName Component,uint8 Kind,float Size,bool bDim) const
{
    // The fill takes the kind's shape; NativePaint draws its rim (LDUIShapes), so state colours never need a new brush.
    if(!Face){return;}
    Face->SetBrush(StorageRound(FaceRadius(Kind,Size),FLinearColor::Transparent,0.f));Face->SetBrushColor(LDHUDStyle::IconFill);
    if(UOverlay* Stack=Cast<UOverlay>(Face->GetContent()))
    {ShowIcon(Cast<UImage>(Stack->GetChildAt(0)),Cast<UTextBlock>(Stack->GetChildAt(1)),Component,FaceRadius(Kind,Size),bDim);}
}
float ULDComponentStorage::FaceRadius(uint8 Kind,float Size) const
{
    return Kind==2?Size*.5f:Kind==1?Size*.2f:8.f;
}
double ULDComponentStorage::UiNow() const
{
    return GetWorld()?GetWorld()->GetRealTimeSeconds():0.;
}
void ULDComponentStorage::BuildWorkshopCard(FName Component,bool bFresh,float Size)
{
    // A library cell: the icon in its kind's shape over its name; hovering it lights the places it takes.
    const uint8 Kind=uint8(LDWorkshop::ComponentFacts(Component,LatestContent).Kind);
    UImage* Picture=nullptr;UTextBlock* Glyph=nullptr;UOverlay* Face=nullptr;USizeBox* Box=nullptr;
    UBorder* Icon=IconFace(Size,Picture,Glyph,Face,Box);Built.Add(Box);ShowIcon(Picture,Glyph,Component,FaceRadius(Kind,Size));
    Icon->SetBrush(StorageRound(FaceRadius(Kind,Size),FLinearColor::Transparent,0.f));Icon->SetBrushColor(LDHUDStyle::IconFill);
    Icon->SetVisibility(ESlateVisibility::Visible);
    auto* Cell=WidgetTree->ConstructWidget<UVerticalBox>();Built.Add(Cell);
    Cell->AddChildToVerticalBox(Box)->SetHorizontalAlignment(HAlign_Center);
    UTextBlock* Name=LDWidgets::Text(WidgetTree,ComponentName(Component),LDHUDStyle::TypeMicro);Name->SetAutoWrapText(false);Built.Add(Name);
    Name->SetJustification(ETextJustify::Center);Name->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);Name->SetColorAndOpacity(LDHUDStyle::TextSecondary);
    StorageShown(Name,Workshop.bNames,ESlateVisibility::HitTestInvisible);
    auto* NameBox=WidgetTree->ConstructWidget<USizeBox>();NameBox->SetWidthOverride(Size+14.f);NameBox->SetContent(Name);Built.Add(NameBox);
    Cell->AddChildToVerticalBox(NameBox)->SetPadding(FMargin(0,4,0,0));
    FCard& Card=Cards.AddDefaulted_GetRef();Card.Component=Component;Card.Face=Icon;Card.Size=Box;Card.Cell=Cell;
    if(LibraryWraps.IsValidIndex(Kind)){LibraryWraps[Kind]->AddChildToWrapBox(Cell);}
}
void ULDComponentStorage::BuildWorkshopTile(FColumn& Column,const LDHUD::FEquippedView& Entry,LDHUD::EStorageColumn ColumnId,float Size)
{
    // A board row: the icon in its kind's shape and the name beside it; a nested component sits indented under its host.
    const uint8 Kind=uint8(LDWorkshop::ComponentFacts(Entry.Component,LatestContent).Kind);
    UImage* Picture=nullptr;UTextBlock* Glyph=nullptr;UOverlay* Face=nullptr;USizeBox* Box=nullptr;
    UBorder* Icon=IconFace(Size,Picture,Glyph,Face,Box);Built.Add(Box);ShowIcon(Picture,Glyph,Entry.Component,FaceRadius(Kind,Size));
    Icon->SetBrush(StorageRound(FaceRadius(Kind,Size),FLinearColor::Transparent,0.f));Icon->SetBrushColor(LDHUDStyle::IconFill);
    Icon->SetVisibility(ESlateVisibility::Visible);
    if(!Entry.Note.IsEmpty())
    {
        UTextBlock* Note=LDWidgets::Text(WidgetTree,Entry.Note,LDHUDStyle::TypeMicro);Note->SetAutoWrapText(false);StorageBold(Note,2);Built.Add(Note);
        auto* NoteSlot=Face->AddChildToOverlay(Note);NoteSlot->SetHorizontalAlignment(HAlign_Right);NoteSlot->SetVerticalAlignment(VAlign_Bottom);
        NoteSlot->SetPadding(FMargin(2,0,3,1));
    }
    auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();Built.Add(Row);
    Row->AddChildToHorizontalBox(Box)->SetVerticalAlignment(VAlign_Center);
    if(Workshop.bNames)
    {
        UTextBlock* Name=LDWidgets::Text(WidgetTree,ComponentName(Entry.Component),LDHUDStyle::TypeCaption);Name->SetAutoWrapText(false);Built.Add(Name);
        Name->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);Name->SetColorAndOpacity(LDHUDStyle::UIText);
        auto* NameSlot=Row->AddChildToHorizontalBox(Name);NameSlot->SetVerticalAlignment(VAlign_Center);NameSlot->SetPadding(FMargin(10,0,0,0));
        NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    }
    auto* RowSlot=Column.List->AddChildToVerticalBox(Row);
    RowSlot->SetPadding(FMargin(22.f*FMath::Min(Entry.Depth,2),Column.List->GetChildrenCount()>1?10.f*Workshop.TileScale:0.f,0,0));
    RowSlot->SetHorizontalAlignment(HAlign_Fill);
    FTile& Tile=Tiles.AddDefaulted_GetRef();Tile.Component=Entry.Component;Tile.Host=Entry.Host;Tile.Column=ColumnId;Tile.Face=Icon;
}
void ULDComponentStorage::BuildCard(FName Component,bool bFresh)
{
    // A stored component is its icon alone, with 新 on the corner while it was never installed; hovering it shows the popup.
    UImage* Picture=nullptr;UTextBlock* Glyph=nullptr;UOverlay* Face=nullptr;USizeBox* Box=nullptr;
    UBorder* Icon=IconFace(StorageTile,Picture,Glyph,Face,Box);Built.Add(Box);ShowIcon(Picture,Glyph,Component);
    Icon->SetVisibility(ESlateVisibility::Visible);
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
    CardList->AddChildToWrapBox(Box);
}
void ULDComponentStorage::MarkSeen(FName Component)
{
    if(Component.IsNone()||!LatestMatch||SeenComponents.Contains(Component)){return;}
    SeenComponents.Add(Component);
    for(FCard& Card:Cards){if(Card.Component==Component){StorageShown(Card.FreshChip,false);Card.bFresh=false;}}
    const int32 Fresh=LDHUD::ComponentStorage(Latest,LatestContent,&SeenComponents).Fresh;
    StorageText(StackFresh,LDTerms::StorageFresh(Fresh));
    StorageShown(StackFreshChip,Fresh>0,ESlateVisibility::HitTestInvisible);
}
void ULDComponentStorage::SetDetailsHeld(bool bHeld)
{
    if(bDetailsHeld==bHeld){return;}
    bDetailsHeld=bHeld;
    if(!bHeld&&!bPinned&&!Drag.bActive&&!Press.bArmed&&!Chooser.bOpen){bExpanded=false;}
}
void ULDComponentStorage::BuildTile(FColumn& Column,const LDHUD::FEquippedView& Entry,LDHUD::EStorageColumn ColumnId)
{
    const float Size=Entry.Depth>0?StorageNestedTile:StorageTile;
    UImage* Picture=nullptr;UTextBlock* Glyph=nullptr;UOverlay* Face=nullptr;USizeBox* Box=nullptr;
    UBorder* Icon=IconFace(Size,Picture,Glyph,Face,Box);Built.Add(Box);ShowIcon(Picture,Glyph,Entry.Component);
    Icon->SetVisibility(ESlateVisibility::Visible);
    if(!Entry.Note.IsEmpty())
    {
        // B18's element or C12's repeated base rides on the tile's corner.
        UTextBlock* Note=LDWidgets::Text(WidgetTree,Entry.Note,LDHUDStyle::TypeMicro);Note->SetAutoWrapText(false);StorageBold(Note,2);Built.Add(Note);
        Note->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
        auto* NoteSlot=Face->AddChildToOverlay(Note);NoteSlot->SetHorizontalAlignment(HAlign_Right);NoteSlot->SetVerticalAlignment(VAlign_Bottom);
        NoteSlot->SetPadding(FMargin(2,0,3,1));
    }
    // A nested component sits indented under its host.
    auto* TileSlot=Column.List->AddChildToVerticalBox(Box);
    TileSlot->SetPadding(FMargin(StorageNestIndent*FMath::Min(Entry.Depth,2),Column.List->GetChildrenCount()>1?4.f:0.f,0,0));
    TileSlot->SetHorizontalAlignment(Entry.Depth>0?HAlign_Right:HAlign_Center);
    FTile& Tile=Tiles.AddDefaulted_GetRef();Tile.Component=Entry.Component;Tile.Host=Entry.Host;Tile.Column=ColumnId;Tile.Face=Icon;
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
            if(Column.Panel){Column.Panel->SetPadding(FMargin(4,6));}
        }
    }
}
bool ULDComponentStorage::OverExpanded(FVector2D At) const
{
    if(StorageUnder(ListFrame,At)||StorageUnder(ChooserFrame,At)||StorageUnder(LibraryFrame,At)){return true;}
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
    if(StorageUnder(ListFrame,At)||StorageUnder(ChooserFrame,At)||StorageUnder(LibraryFrame,At)){return true;}
    for(const FColumn& Column:Columns){if(StorageUnder(Column.Frame,At)){return true;}}
    return false;
}
TArray<const UWidget*> ULDComponentStorage::ReservedWidgets() const
{
    TArray<const UWidget*> Out;
    for(const UWidget* Widget:{StackFrame.Get(),ListFrame.Get(),ChooserFrame.Get(),PopupFrame.Get(),LibraryFrame.Get()}){if(Widget&&Widget->IsVisible()){Out.Add(Widget);}}
    for(const FColumn& Column:Columns){if(Column.Frame&&Column.Frame->IsVisible()){Out.Add(Column.Frame);}}
    return Out;
}
FReply ULDComponentStorage::NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event)
{
    if(Event.GetEffectingButton()==EKeys::LeftMouseButton&&BeginPress(Event.GetScreenSpacePosition())){return FReply::Handled();}
    if(Event.GetEffectingButton()==EKeys::RightMouseButton&&bShownNow&&bExpanded)
    {
        // D6 right click: the component under the pointer installs at its only place, or comes back to the storage.
        const FVector2D At=Event.GetScreenSpacePosition();
        for(const FCard& Card:Cards){if(StorageUnder(Card.Face,At)){PendingAt=At;QuickInstall(Card.Component);return FReply::Handled();}}
        for(const FTile& Tile:Tiles){if(StorageUnder(Tile.Face,At)){QuickInstall(Tile.Component);return FReply::Handled();}}
        if(IsHit(At)){return FReply::Handled();}
    }
    return Super::NativeOnMouseButtonDown(Geometry,Event);
}
bool ULDComponentStorage::QuickInstall(FName Component)
{
    const FLDComponentInstanceView* Owned=StorageOwned(Latest,Component);
    if(!Owned){return false;}
    if(Drag.bActive){EndDrag();}
    CloseChooser();
    if(Owned->bInstalled){bPendingAt=false;return DropInStorage(Component);}
    const TArray<LDHUD::FComponentDrop> Drops=LDHUD::ComponentDrops(Component,Latest,LatestContent);
    if(Drops.Num()!=1)
    {
        // Several places (or none) need the player's choice: the line says where it goes, nothing is sent.
        if(Notify){Notify(FString(LDTerms::Storage)+TEXT("：")+(Drops.IsEmpty()?LDHUD::StoredPlaceText(Component,Latest,LatestContent):LDHUD::DropColumnsText(Drops)));}
        return false;
    }
    bPendingAt=!PendingAt.IsZero();Chooser.At=PendingAt;
    return DropOn(Component,Drops[0].Column,Drops[0].Host);
}
bool ULDComponentStorage::PickUp(FName Component,bool bFromColumn)
{
    if(Component.IsNone()){return false;}
    Press=FPress();
    Drag.Component=Component;Drag.bFromColumn=bFromColumn;Drag.bActive=true;Drag.bPicked=true;Drag.Since=UiNow();
    Drag.Drops=LDHUD::ComponentDrops(Component,Latest,LatestContent);
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
    if(Drag.bActive&&Drag.bPicked){DropHeld(At);return true;}
    // C12: the installed bases it may repeat light up on the board; a click on one chooses it.
    if(Chooser.bOpen&&!Chooser.Drop.Bases.IsEmpty())
    {
        for(const FTile& Tile:Tiles)
        {
            if(!StorageUnder(Tile.Face,At)){continue;}
            const int32 Option=Chooser.Drop.Bases.IndexOfByKey(Tile.Component);
            if(Option!=INDEX_NONE){ChooseOption(Option);return true;}
        }
    }
    FName Component;bool bFromColumn=false;
    if(bExpanded)
    {
        for(const FCard& Card:Cards){if(StorageUnder(Card.Face,At)){Component=Card.Component;break;}}
        if(Component.IsNone()){for(const FTile& Tile:Tiles){if(StorageUnder(Tile.Face,At)){Component=Tile.Component;bFromColumn=true;break;}}}
    }
    if(Component.IsNone()){return IsHit(At);}
    // One request at a time: a press while one is on its way, or while a move waits for its second half, starts nothing.
    if(PendingRequest||Follow.State!=EFollow::None){if(Notify){Notify(FString(LDTerms::Storage)+TEXT("：上一个请求仍在处理中。"));}return true;}
    if(!Gate.bCanInstall){if(Notify){Notify(FString(LDTerms::Storage)+TEXT("：")+Gate.InstallBlocked);}return true;}
    CloseChooser();
    Press.Component=Component;Press.bFromColumn=bFromColumn;Press.At=At;Press.bArmed=true;
    return true;
}
void ULDComponentStorage::UpdateDrag(FVector2D Pointer,bool bHeld)
{
    if(!Press.bArmed){return;}
    if(!Drag.bActive)
    {
        // A press that never moves is a click: it picks the component up (D6); the next click puts it down.
        if(!bHeld){PickUp(Press.Component,Press.bFromColumn);return;}
        if(FVector2D::Distance(Pointer,Press.At)<StorageDragStartPx){return;}
        Drag.Component=Press.Component;Drag.bFromColumn=Press.bFromColumn;Drag.bActive=true;Drag.bPicked=false;Drag.Since=UiNow();
        Drag.Drops=LDHUD::ComponentDrops(Drag.Component,Latest,LatestContent);
        Drag.Drops.Append(LDHUD::ReplaceDrops(Drag.Component,Latest,LatestContent));
        ShowIcon(GhostPicture,GhostGlyph,Drag.Component);
        StorageShown(GhostFrame,true,ESlateVisibility::HitTestInvisible);
        return;
    }
    if(bHeld||Drag.bPicked){return;}
    // Let go: the storage takes back an installed component; a column or host installs or moves it.
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
    const bool bHad=Drag.bActive||Chooser.bOpen;
    EndDrag();CloseChooser();
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
        if(bAnchor||StorageUnder(Columns[Index].Frame,At)){OutColumn=static_cast<LDHUD::EStorageColumn>(Index);OutHost=NAME_None;return true;}
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
    const FLDComponentInstanceView* Owned=StorageOwned(Latest,Component);
    if(!Owned||!Owned->bInstalled){return false;}
    if(PendingRequest||Follow.State!=EFollow::None){if(Notify){Notify(FString(LDTerms::Storage)+TEXT("：上一个请求仍在处理中。"));}return false;}
    FLDBuildCommand Command;Command.Kind=ELDBuildCommandKind::UninstallComponent;
    if(!FLDDefinitionWire::Encode(Component,Command.Component)){if(Notify){Notify(TEXT("没有提交：这个构件的标识无效。"));}return false;}
    const TArray<FName> Off=LDHUD::DetachedWith(Component,Latest);
    if(!Off.IsEmpty()&&Notify)
    {
        TArray<FString> Names;for(const FName& Id:Off){Names.Add(ComponentName(Id));}
        Notify(FString(LDTerms::Storage)+TEXT("：")+LDTerms::MovingDetaches(FString::Join(Names,TEXT("、"))));
    }
    PendingComponent=Component;
    return Send(Command);
}
bool ULDComponentStorage::DropOn(FName Component,LDHUD::EStorageColumn Column,FName Host)
{
    const FLDComponentInstanceView* Owned=StorageOwned(Latest,Component);
    if(!Owned){return false;}
    if(PendingRequest||Follow.State!=EFollow::None){if(Notify){Notify(FString(LDTerms::Storage)+TEXT("：上一个请求仍在处理中。"));}return false;}
    if(!Gate.bCanInstall){if(Notify){Notify(FString(LDTerms::Storage)+TEXT("：")+Gate.InstallBlocked);}return false;}
    // Let go where it already sits: nothing to do.
    if(Owned->bInstalled)
    {
        for(const FTile& Tile:Tiles)
        {
            if(Tile.Component!=Component){continue;}
            if(Tile.Column==Column&&(Host.IsNone()||Host==Tile.Host||Host==Component)){return false;}
        }
    }
    const TArray<LDHUD::FComponentDrop> Drops=LDHUD::ComponentDrops(Component,Latest,LatestContent);
    const LDHUD::FComponentDrop* Drop=LDHUD::FindDrop(Drops,Column,Host);
    // A stored component let go on an installed one it cannot sit on replaces it in one commit.
    const TArray<LDHUD::FComponentDrop> Replaces=Drop||Host.IsNone()?TArray<LDHUD::FComponentDrop>():LDHUD::ReplaceDrops(Component,Latest,LatestContent);
    if(!Drop){Drop=Replaces.FindByPredicate([Column,Host](const LDHUD::FComponentDrop& D){return D.Column==Column&&D.Host==Host;});}
    if(!Drop&&!Owned->bInstalled&&LDHUD::WorkshopFull(Latest,LatestContent))
    {
        if(Notify){Notify(FString(LDTerms::Storage)+TEXT("：")+LDHUD::StoredPlaceText(Component,Latest,LatestContent));}
        return false;
    }
    if(!Drop)
    {
        if(Notify){Notify(FString(LDTerms::Storage)+TEXT("：")+LDTerms::CannotGoThere(ComponentName(Component),PlaceName(Column,Host)));}
        return false;
    }
    // B18 asks for its element and C12 for the base it repeats, unless there is only one.
    if(Drop->Elements.Num()>1||Drop->Bases.Num()>1){OpenChooser(Component,*Drop);return true;}
    return Commit(Component,*Drop,Drop->Elements.Num()==1?Drop->Elements[0]:ELDDemoElement::None,Drop->Bases.Num()==1?Drop->Bases[0]:NAME_None);
}
void ULDComponentStorage::OpenChooser(FName Component,const LDHUD::FComponentDrop& Drop)
{
    Chooser.bOpen=true;Chooser.Component=Component;Chooser.Drop=Drop;
    const bool bElements=Drop.Elements.Num()>0;
    StorageText(ChooserTitle,ComponentName(Component)+TEXT(" · ")+(bElements?LDTerms::PickElement:LDTerms::PickRepeatedBase));
    const int32 Count=bElements?Drop.Elements.Num():Drop.Bases.Num();
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
        const FString Label=bElements?LDHUD::ElementName(Drop.Elements[Index]):ComponentName(Drop.Bases[Index]);
        if(UTextBlock* Caption=Cast<UTextBlock>(Button->GetContent())){StorageText(Caption,Label);}
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
    const FName Component=Chooser.Component;const LDHUD::FComponentDrop Drop=Chooser.Drop;
    const bool bElements=Drop.Elements.Num()>0;
    if(bElements?!Drop.Elements.IsValidIndex(Index):!Drop.Bases.IsValidIndex(Index)){return false;}
    CloseChooser();
    return Commit(Component,Drop,bElements?Drop.Elements[Index]:ELDDemoElement::None,bElements?NAME_None:Drop.Bases[Index]);
}
bool ULDComponentStorage::Commit(FName Component,const LDHUD::FComponentDrop& Drop,ELDDemoElement Element,FName Base)
{
    const FLDComponentInstanceView* Owned=StorageOwned(Latest,Component);
    if(!Owned){return false;}
    PendingComponent=Component;
    FLDBuildCommand Install;Install.Kind=ELDBuildCommandKind::InstallComponent;
    const auto Refuse=[this](const TCHAR* Why){if(Notify){Notify(FString(TEXT("没有提交："))+Why);}return false;};
    if(!FLDDefinitionWire::Encode(Component,Install.Component)){return Refuse(TEXT("这个构件的标识无效。"));}
    Install.TargetKind=Drop.Kind;
    if(!Drop.TargetId.IsNone()&&!FLDDefinitionWire::Encode(Drop.TargetId,Install.TargetDefinition)){return Refuse(TEXT("安装位置的标识无效。"));}
    if(!Drop.BindActive.IsNone())
    {
        if(!FLDDefinitionWire::Encode(Drop.BindActive,Install.BoundActive)){return Refuse(TEXT("绑定的技能标识无效。"));}
        Install.Element=Element;
    }
    if(!Base.IsNone()&&!FLDDefinitionWire::Encode(Base,Install.BoundComponent)){return Refuse(TEXT("重复的基础构件标识无效。"));}
    if(!Drop.Replacing.IsNone())
    {
        if(Owned->bInstalled||!FLDDefinitionWire::Encode(Drop.Replacing,Install.Replacing)){return Refuse(TEXT("被替换的构件标识无效。"));}
        if(Notify)
        {
            TArray<FString> Names;for(const FName& Id:Drop.Lost){Names.Add(ComponentName(Id));}
            Notify(FString(LDTerms::Storage)+TEXT("：")+LDTerms::MovingDetaches(FString::Join(Names,TEXT("、"))));
        }
        return Send(Install);
    }
    if(!Owned->bInstalled){return Send(Install);}
    // A move: uninstall first (what sits on it comes off as well), then install once the view shows the uninstall.
    FLDBuildCommand Uninstall;Uninstall.Kind=ELDBuildCommandKind::UninstallComponent;Uninstall.Component=Install.Component;
    const TArray<FName> Off=LDHUD::DetachedWith(Component,Latest);
    if(!Off.IsEmpty()&&Notify)
    {
        TArray<FString> Names;for(const FName& Id:Off){Names.Add(ComponentName(Id));}
        Notify(FString(LDTerms::Storage)+TEXT("：")+LDTerms::MovingDetaches(FString::Join(Names,TEXT("、"))));
    }
    Follow.State=EFollow::AwaitUninstall;Follow.Component=Component;Follow.Install=Install;
    return Send(Uninstall);
}
bool ULDComponentStorage::Send(FLDBuildCommand Command)
{
    if(!SubmitBuild)
    {
        UE_LOG(LogLDUI,Error,TEXT("LD_STORAGE_SUBMIT_UNAVAILABLE The growth build entry is not connected to the component storage."));
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
    // A refusal shakes the component and edges it red for a moment; an applied install flashes where it lands.
    if(bRefused){RejectedComponent=PendingComponent;RejectedAt=UiNow();}
    else if(!Text.IsEmpty()){LandedComponent=PendingComponent;LandedAt=UiNow();}
    if(Follow.State==EFollow::None){bPendingAt=false;PendingComponent=NAME_None;}
    if(!Text.IsEmpty()&&Notify){Notify(FString(LDTerms::Storage)+TEXT("：")+Text);}
}
void ULDComponentStorage::ShowFinal(const FLDBuildReceipt& Receipt)
{
    const bool bApplied=Receipt.Header.Outcome==ELDReceiptOutcome::Applied;
    if(Follow.State==EFollow::AwaitUninstall&&Receipt.Kind==ELDBuildCommandKind::UninstallComponent)
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
        for(UWidget* Hidden:{ListFrame.Get(),PopupFrame.Get(),ChooserFrame.Get(),GhostFrame.Get(),GhostCaptionFrame.Get(),LibraryFrame.Get()}){StorageShown(Hidden,false);}
        for(const FColumn& Column:Columns){StorageShown(Column.Frame,false);}
        CurrentFocus=FFocus();
        return;
    }
    const double Now=UiNow();
    const FVector2D Pointer=FSlateApplication::Get().GetCursorPos();
    const bool bHeld=FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::LeftMouseButton);
    // A click away from every place puts a picked-up component down without a command.
    if(Drag.bActive&&Drag.bPicked&&bHeld&&!bWasPressed&&!StorageAt(Pointer)&&!OverExpanded(Pointer)){EndDrag();}
    bWasPressed=bHeld;
    UpdateDrag(Pointer,bHeld);
    if(Workshop.bActive){bExpanded=true;LastInsideAt=Now;}
    else
    {
        // Hovering the stack opens the view; it stays while the pointer is over any part of it (or a slot under a column), a
        // drag or the chooser runs, and closes a moment after the pointer left.
        const bool bOverStack=StorageUnder(StackFrame,Pointer);
        const bool bInside=bPinned||bDetailsHeld||bOverStack||(bExpanded&&OverExpanded(Pointer))||Drag.bActive||Press.bArmed||Chooser.bOpen;
        if(bInside){LastInsideAt=Now;if(bOverStack||bPinned||bDetailsHeld){bExpanded=true;}}
        else if(bExpanded&&Now-LastInsideAt>StorageCloseGrace){bExpanded=false;}
    }
    Layout(Geometry,Pointer);
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
            const LDHUD::FComponentDrop* Drop=Host.IsNone()?nullptr:Drag.Drops.FindByPredicate([Column,Host](const LDHUD::FComponentDrop& D){return D.Column==Column&&D.Host==Host;});
            if(!Drop){Drop=Drag.Drops.FindByPredicate([Column](const LDHUD::FComponentDrop& D){return D.Column==Column&&D.Host.IsNone();});}
            if(Drop){Next.bOverDrop=true;Next.Drop=*Drop;}
        }
    }
    else if(Chooser.bOpen){Next.Component=Chooser.Component;Next.bHeld=true;Next.bOverDrop=true;Next.Drop=Chooser.Drop;}
    else if(bExpanded)
    {
        for(const FCard& Card:Cards){if(StorageUnder(Card.Face,Pointer)){Next.Component=Card.Component;break;}}
        if(!Next.Component.IsNone())
        {
            // Hovering a stored component already shows where it goes: its places light up before anything is picked up.
            if(HoverDropsOf!=Next.Component)
            {
                HoverDropsOf=Next.Component;
                HoverDrops=LDHUD::ComponentDrops(Next.Component,Latest,LatestContent);
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
    // A refused component shakes three times, then keeps its red edge for the notice time.
    const float Shake=RejectAge<LDHUDStyle::MotionReject?FMath::Sin(float(RejectAge/LDHUDStyle::MotionReject)*3.f*2.f*PI)*4.f:0.f;
    TArray<FName> Detached;
    if(Drag.bActive&&Drag.bFromColumn){Detached=LDHUD::DetachedWith(Drag.Component,Latest);}
    if(CurrentFocus.bOverDrop){Detached.Append(CurrentFocus.Drop.Lost);}
    const TArray<FName>* Bases=Chooser.bOpen&&!Chooser.Drop.Bases.IsEmpty()?&Chooser.Drop.Bases:nullptr;
    for(FCard& Card:Cards)
    {
        const bool bRejected=bRejectLive&&Card.Component==RejectedComponent;
        Card.State=bRejected?EFaceState::Rejected:Drag.bActive&&Drag.Component==Card.Component?EFaceState::Held
            :CurrentFocus.Component==Card.Component&&!CurrentFocus.bInstalled?EFaceState::Hovered:EFaceState::Normal;
        LDTween::SetTranslation(Card.Face,FVector2D(bRejected?Shake:0.f,0.f));
    }
    for(FTile& Tile:Tiles)
    {
        const LDHUD::FComponentDrop* OnTile=CurrentFocus.Drops.FindByPredicate([&Tile](const LDHUD::FComponentDrop& Drop){return Drop.Host==Tile.Component;});
        const bool bRejected=bRejectLive&&Tile.Component==RejectedComponent;
        const bool bReplaced=CurrentFocus.bOverDrop&&CurrentFocus.Drop.Replacing==Tile.Component;
        Tile.State=bRejected?EFaceState::Rejected
            :Drag.bActive&&Drag.Component==Tile.Component?EFaceState::Held
            :Bases&&Bases->Contains(Tile.Component)?EFaceState::Pick
            :bReplaced||(OnTile&&!OnTile->Replacing.IsNone())?EFaceState::Replace
            :Detached.Contains(Tile.Component)||Warnings.Contains(Tile.Component)?EFaceState::Detached
            :OnTile?EFaceState::Legal
            :CurrentFocus.Component==Tile.Component&&CurrentFocus.bInstalled?EFaceState::Hovered
            :Bases?EFaceState::Dim:EFaceState::Normal;
        LDTween::SetTranslation(Tile.Face,FVector2D(bRejected?Shake:0.f,0.f));
        LDTween::SetOpacity(Tile.Face,Tile.State==EFaceState::Held?.35f:Tile.State==EFaceState::Dim?.4f:1.f);
    }
}
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
        // The stack sits right of the band, on its bottom edge.
        StoragePlace(StackSlot,Local(FVector2D(Anchors.Band.Right,Anchors.Band.Bottom))+FVector2D(StorageGapFromBand,0),FVector2D(0,1));
        StorageShown(ListFrame,bExpanded&&!Cards.IsEmpty(),ESlateVisibility::Visible);
        const FSlateRect Stack=StorageRect(StackFrame);
        const FVector2D StackTopLeft=Local(FVector2D(Stack.Left,Stack.Top));
        if(bExpanded)
        {
            // The list rises from the stack; a dragged icon fades (its rim is painted, LDUIShapes).
            StoragePlace(ListSlot,FVector2D(StackTopLeft.X,StackTopLeft.Y-6.f),FVector2D(0,1));
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
            const TArray<LDHUD::FComponentDrop>& Drops=CurrentFocus.Drops;
            const bool bOwnDrop=Drags(Drops,Id,true);
            const bool bAnyDrop=Drags(Drops,Id,false);
            const bool bReplaceDrop=Drops.ContainsByPredicate([Id](const LDHUD::FComponentDrop& Drop){return Drop.Column==Id&&!Drop.Replacing.IsNone();});
            StorageShown(Column.Placeholder,bOwnDrop&&CurrentFocus.bHeld,ESlateVisibility::HitTestInvisible);
            const int32 Look=Drops.IsEmpty()?0:bAnyDrop?1:bReplaceDrop?3:2;
            if(Column.AppliedLook!=Look)
            {
                Column.AppliedLook=Look;
                const FLinearColor Rim=Look==1?LDHUDStyle::UILegal:Look==3?LDHUDStyle::UIReplace:LDHUDStyle::PanelRim;
                Column.Panel->SetBrush(StorageRound(8.f,Rim,Look==1||Look==3?2.f:1.f));
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
            const LDHUD::FComponentCardView View=LDHUD::ComponentCard(Shown,LatestContent,LatestStats);
            StorageText(PopupName,View.Name);
            const FLDComponentInstanceView* Owned=StorageOwned(Latest,Shown);
            const FString Target=Owned?LDHUD::InstalledTargetText(*Owned,LatestContent):FString();
            const FString Where=bInstalled?(Target.IsEmpty()?FString(TEXT("已安装")):FString(TEXT("已安装"))+Target)
                :FString(TEXT("在构件库 · "))+LDHUD::StoredPlaceText(Shown,Latest,LatestContent);
            StorageText(PopupWhere,View.KindLine.IsEmpty()?Where:View.KindLine+TEXT("\n")+Where);
            const FString Body=View.Keywords.IsEmpty()?View.Sentence:View.Sentence+TEXT("\n\n")+FString::Join(View.Keywords,TEXT("\n"));
            if(!PopupBody->GetText().ToString().Equals(Body,ESearchCase::CaseSensitive)){PopupBody->SetText(FText::FromString(Body));}
            const FString Links=LDHUD::ConnectionsText(Shown,Latest,LatestContent);
            const FString Missing=bInstalled?FString():LDWorkshop::MissingPlace(Shown,Latest,LatestContent);
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
            const LDHUD::FComponentDrop* Drop=CurrentFocus.bOverDrop?&CurrentFocus.Drop:nullptr;
            StorageText(GhostCaption,Drop?LDHUD::DropPreviewText(Drag.Component,*Drop,Latest,LatestContent):FString(LDTerms::DropNowhere));
            GhostCaption->SetColorAndOpacity(FSlateColor(!Drop?LDHUDStyle::TextSecondary:Drop->Replacing.IsNone()?LDHUDStyle::UILegal:LDHUDStyle::UIReplace));
            StoragePlace(GhostCaptionSlot,Local(Pointer)+FVector2D(StorageTile*.5f+10.f,0),FVector2D(0,.5f));
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
bool ULDComponentStorage::Drags(const TArray<LDHUD::FComponentDrop>& Drops,LDHUD::EStorageColumn Column,bool bOwnOnly)
{
    return Drops.ContainsByPredicate([Column,bOwnOnly](const LDHUD::FComponentDrop& Drop)
    {return Drop.Column==Column&&Drop.Replacing.IsNone()&&(!bOwnOnly||Drop.Host.IsNone());});
}
void ULDComponentStorage::LayoutWorkshop(const FGeometry& Geometry,FVector2D Pointer)
{
    const auto Local=[&Geometry](FVector2D Absolute){return Geometry.AbsoluteToLocal(Absolute);};
    for(UWidget* Hidden:{StackFrame.Get(),ListFrame.Get(),PopupFrame.Get()}){StorageShown(Hidden,false);}
    // The library: the kind groups in its rectangle. The use-tag filter only dims (nothing moves); a component without a
    // place dims too.
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
            const bool bFiltered=Workshop.TagFilter!=INDEX_NONE&&!LDWorkshop::ComponentFacts(Card.Component,LatestContent).Tags.Contains(static_cast<ELDDemoComponentTag>(Workshop.TagFilter));
            LDTween::SetOpacity(Card.Cell,Card.State==EFaceState::Held?.35f:bFiltered?.3f:Card.bPlaceless?.55f:1.f);
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
        const LDHUD::EStorageColumn Id=static_cast<LDHUD::EStorageColumn>(Index);
        const TArray<LDHUD::FComponentDrop>& Drops=CurrentFocus.Drops;
        const bool bOwnDrop=Drags(Drops,Id,true);
        const bool bTakes=Drops.ContainsByPredicate([Id](const LDHUD::FComponentDrop& Drop){return Drop.Column==Id;});
        const bool bEmptySlot=Index>=int32(LDHUD::EStorageColumn::SlotQ)&&Index<=int32(LDHUD::EStorageColumn::SlotE)&&Column.Active.IsNone();
        StorageShown(Column.Placeholder,!bEmptySlot&&(bRoom||bOwnDrop),ESlateVisibility::HitTestInvisible);
        const int32 Look=Drops.IsEmpty()?0:bTakes?1:2;
        if(Column.AppliedLook!=Look)
        {
            Column.AppliedLook=Look;
            Column.Panel->SetBrush(StorageRound(8.f,FLinearColor::Transparent,0.f));Column.Panel->SetBrushColor(FLinearColor::Transparent);
            Column.Panel->SetPadding(FMargin(10,8));
            StorageShown(Column.Title,false);
            Column.Frame->SetRenderOpacity(Look==2?.3f:1.f);
        }
    }
}
int32 ULDComponentStorage::NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Culling,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool bEnabled) const
{
    const int32 Base=Super::NativePaint(Args,Geometry,Culling,Elements,Layer,Style,bEnabled);
    if(!bShownNow){return Base;}
    const double Now=UiNow();
    const int32 Top=Base+1;
    // The kind sets the shape and rim colour (white base rules, cyan skill mods, violet link rules); a state recolours the
    // rim with a halo: yellow held or picked, green a legal place, orange a replacement or what comes off with it, red a
    // refusal. A scarce component keeps the iridescent edge.
    const auto StateColor=[](EFaceState State,uint8 Kind,bool& bGlow)->FLinearColor
    {
        bGlow=true;
        switch(State)
        {
        case EFaceState::Held: case EFaceState::Pick: return LDHUDStyle::UIAction;
        case EFaceState::Legal: return LDHUDStyle::UILegal;
        case EFaceState::Replace: case EFaceState::Detached: return LDHUDStyle::UIReplace;
        case EFaceState::Rejected: return LDHUDStyle::UIReject;
        case EFaceState::Hovered: bGlow=false;return LDHUDStyle::UIAction;
        default: bGlow=false;return LDUIShapes::KindColor(Kind).CopyWithNewOpacity(State==EFaceState::Dim?.35f:.9f);
        }
    };
    const auto PaintFace=[&](const UWidget* Face,uint8 Kind,bool bScarce,EFaceState State,bool bFresh,float Opacity)
    {
        if(!Face){return;}
        const FBox2D Box=LDUIShapes::RectIn(Geometry,Face->GetCachedGeometry());
        if(Box.GetSize().X<4.f){return;}
        const LDUIShapes::EShape Shape=LDUIShapes::KindShape(Kind);
        const float Radius=FaceRadius(Kind,float(Box.GetSize().X));
        bool bGlow=false;const FLinearColor Rim=StateColor(State,Kind,bGlow);
        if(bScarce&&State==EFaceState::Normal){LDUIShapes::Iridescent(Elements,Top,Geometry,Shape,Box,Now,Radius);}
        else
        {
            if(bGlow){LDUIShapes::Glow(Elements,Top,Geometry,Shape,Box,Rim.CopyWithNewOpacity(Opacity),7.f,Radius);}
            LDUIShapes::Frame(Elements,Top,Geometry,Shape,Box,Rim.CopyWithNewOpacity(Rim.A*Opacity),State==EFaceState::Normal||State==EFaceState::Dim?1.5f:2.5f,Radius);
        }
        if(bFresh)
        {
            const FVector2D Dot(Box.Max.X-3.f,Box.Min.Y+3.f);
            LDUIShapes::Fill(Elements,Top+1,Geometry,FBox2D(Dot-FVector2D(5,5),Dot+FVector2D(5,5)),LDHUDStyle::UIAction,5.f);
        }
    };
    const bool bCardsShown=Workshop.bActive?(LibraryFrame&&LibraryFrame->IsVisible()):(ListFrame&&ListFrame->IsVisible());
    if(bCardsShown)
    {
        for(const FCard& Card:Cards)
        {
            const float Opacity=Card.Cell?Card.Cell->GetRenderOpacity():Card.Size?Card.Size->GetRenderOpacity():1.f;
            PaintFace(Card.Face,Card.Kind,Card.bScarce,Card.State,Workshop.bActive&&Card.bFresh,Opacity);
        }
    }
    for(const FTile& Tile:Tiles)
    {
        const int32 Index=int32(Tile.Column);
        if(!Columns.IsValidIndex(Index)||!Columns[Index].Frame||!Columns[Index].Frame->IsVisible()){continue;}
        PaintFace(Tile.Face,Tile.Kind,Tile.bScarce,Tile.State,false,Columns[Index].Frame->GetRenderOpacity());
        // An install that just landed flashes once: a ring spreading out from it.
        const double Age=Now-LandedAt;
        if(Tile.Component==LandedComponent&&Age>=0.&&Age<LDHUDStyle::MotionInstallLand+LDHUDStyle::MotionInstallSettle)
        {
            const float T=LDTween::Progress(Age,LDHUDStyle::MotionInstallLand+LDHUDStyle::MotionInstallSettle,LDTween::ECurve::EaseOut);
            const FBox2D Box=LDUIShapes::RectIn(Geometry,Tile.Face->GetCachedGeometry());
            const FVector2D Grow(10.f*T,10.f*T);
            LDUIShapes::Frame(Elements,Top,Geometry,LDUIShapes::KindShape(Tile.Kind),FBox2D(Box.Min-Grow,Box.Max+Grow),LDHUDStyle::UILegal.CopyWithNewOpacity(1.f-T),2.f,
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
            const FBox2D Box=LDUIShapes::RectIn(Geometry,Column.Placeholder->GetCachedGeometry());
            const bool bLegal=Drags(CurrentFocus.Drops,static_cast<LDHUD::EStorageColumn>(Index),true);
            if(bLegal)
            {
                const float Pulse=CurrentFocus.bHeld?.6f+.4f*LDTween::PingPong(Now,.9):1.f;
                LDUIShapes::Frame(Elements,Top,Geometry,LDUIShapes::EShape::Rounded,Box,LDHUDStyle::UILegal.CopyWithNewOpacity(Pulse),2.f,8.f);
            }
            else{LDUIShapes::Dashed(Elements,Top,Geometry,Box,LDHUDStyle::TextSecondary.CopyWithNewOpacity(.45f));}
            const FVector2D Center=Box.GetCenter();
            const FLinearColor Plus=(bLegal?LDHUDStyle::UILegal:LDHUDStyle::TextSecondary).CopyWithNewOpacity(.6f);
            LDUIShapes::Stroke(Elements,Top,Geometry,{Center-FVector2D(6,0),Center+FVector2D(6,0)},Plus,1.5f);
            LDUIShapes::Stroke(Elements,Top,Geometry,{Center-FVector2D(0,6),Center+FVector2D(0,6)},Plus,1.5f);
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
                    const uint8 Kind=uint8(LDWorkshop::ComponentFacts(Entries[Entry].Component,LatestContent).Kind);
                    const FBox2D Box(FVector2D(X0+Entry*(Mark+Gap),Y),FVector2D(X0+Entry*(Mark+Gap)+Mark,Y+Mark));
                    LDUIShapes::Fill(Elements,Top,Geometry,Box,LDUIShapes::KindColor(Kind),Kind==2?Mark*.5f:Kind==1?2.f:1.5f);
                }
            }
        }
        const FBox2D Stack=LDUIShapes::RectIn(Geometry,StackFrame->GetCachedGeometry());
        const LDWorkshop::FCapacityView Capacity=LDWorkshop::Capacity(Latest,LatestModel,LatestContent);
        for(int32 Cell=0;Cell<Capacity.Capacity&&Cell<16;++Cell)
        {
            const float X=float(Stack.Min.X)+Cell*19.f,Y=float(Stack.Min.Y)-14.f;
            const FLinearColor Color=Capacity.Used.IsValidIndex(Cell)?LDUIShapes::KindColor(uint8(Capacity.Used[Cell])):LDHUDStyle::UIRaised;
            LDUIShapes::Fill(Elements,Top,Geometry,FBox2D(FVector2D(X,Y),FVector2D(X+16.f,Y+9.f)),Color,1.5f);
        }
        // A received component's icon arcs from the screen centre into the stack; its name then stays over the stack.
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
                FSlateBrush Brush;Brush.SetResourceObject(LDUIIcons::Find(LDHUD::ContentIconId(LatestContent,Fly.Component.ToString())));
                Brush.ImageSize=FVector2D(Icon,Icon);Brush.DrawAs=ESlateBrushDrawType::Image;
                if(Brush.GetResourceObject()){FSlateDrawElement::MakeBox(Elements,Top+1,Geometry.ToPaintGeometry(FVector2D(Icon,Icon),FSlateLayoutTransform(At-FVector2D(Icon,Icon)*.5f)),&Brush);}
                LDUIShapes::Frame(Elements,Top+1,Geometry,LDUIShapes::EShape::Rounded,FBox2D(At-FVector2D(Icon,Icon)*.5f,At+FVector2D(Icon,Icon)*.5f),LDHUDStyle::UIAction,2.f,6.f);
            }
            else if(Fly.bStrip&&Age<LDHUDStyle::MotionGain+1.5)
            {
                const FString Name=ComponentName(Fly.Component);
                const FSlateFontInfo Font=LDUIFonts::Font(LDUIFonts::ERole::Strong,LDHUDStyle::TypeSmall);
                const FVector2D Strip(FMath::Max(80.f,Name.Len()*15.f+24.f),26.f);
                const FVector2D Min(float(Stack.GetCenter().X)-Strip.X*.5f,float(Stack.Min.Y)-46.f);
                LDUIShapes::Fill(Elements,Top+1,Geometry,FBox2D(Min,Min+Strip),LDHUDStyle::UIRaised.CopyWithNewOpacity(.95f),LDHUDStyle::RadiusButton);
                LDUIShapes::Frame(Elements,Top+1,Geometry,LDUIShapes::EShape::Rounded,FBox2D(Min,Min+Strip),LDHUDStyle::UIAction,1.5f,LDHUDStyle::RadiusButton);
                FSlateDrawElement::MakeText(Elements,Top+2,Geometry.ToPaintGeometry(Strip,FSlateLayoutTransform(Min+FVector2D(12.f,3.f))),Name,Font,ESlateDrawEffect::None,LDHUDStyle::UIText);
            }
        }
    }
    // A sent component waits where it was let go with a thin turning ring until the answer arrives.
    if(GhostFrame&&GhostFrame->IsVisible()&&!Drag.bActive)
    {
        const FBox2D Box=LDUIShapes::RectIn(Geometry,GhostFrame->GetCachedGeometry());
        LDUIShapes::Arc(Elements,Top+1,Geometry,Box.GetCenter(),float(Box.GetSize().X)*.62f,.3f,LDHUDStyle::UIAction,2.f,float(FMath::Fmod(Now*1.4,1.)));
    }
    return Top+1;
}
