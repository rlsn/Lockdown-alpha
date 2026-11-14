#include "UI/LDComponentStorage.h"
#include "Core/LDLog.h"
#include "UI/LDHUDStyle.h"
#include "UI/LDTerms.h"
#include "UI/LDUIIcons.h"
#include "UI/LDWidgets.h"
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
const FLinearColor StorageCardFill(.035f,.06f,.09f,.97f);
const FLinearColor StorageColumnFill(.02f,.03f,.05f,.84f);
const FLinearColor StorageLegal(.45f,.95f,.50f);
const FLinearColor StorageReplace(1.f,.72f,.25f);
const FLinearColor StorageIconFill(.16f,.26f,.36f);

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
                Card->SetBrushColor(StorageCardFill);Card->SetRenderTranslation(FVector2D(4.f*Back,-4.f*Back));
                auto* BackSlot=Deck->AddChildToOverlay(Card);BackSlot->SetHorizontalAlignment(HAlign_Fill);BackSlot->SetVerticalAlignment(VAlign_Fill);
                StackBacks.Add(Card);
            }
            UImage* Picture=nullptr;UTextBlock* Glyph=nullptr;UOverlay* Face=nullptr;USizeBox* Box=nullptr;
            StackTop=IconFace(StorageStackSize,Picture,Glyph,Face,Box);StackPicture=Picture;StackGlyph=Glyph;
            StackCount=Line(TEXT(""),14,FLinearColor::White);StorageBold(StackCount,2);
            auto* CountSlot=Face->AddChildToOverlay(StackCount);CountSlot->SetHorizontalAlignment(HAlign_Right);CountSlot->SetVerticalAlignment(VAlign_Bottom);
            CountSlot->SetPadding(FMargin(0,0,5,2));
            StackFresh=Line(TEXT(""),12,LDHUDStyle::Rim);StorageBold(StackFresh,0);
            auto* Chip=WidgetTree->ConstructWidget<UBorder>();Chip->SetBrush(StorageRound(4.f,LDHUDStyle::Rim,1.f));Chip->SetBrushColor(LDHUDStyle::TextAccent);
            Chip->SetPadding(FMargin(4,0));Chip->SetContent(StackFresh);Chip->SetRenderTranslation(FVector2D(8,-8));StackFreshChip=Chip;
            auto* ChipSlot=Face->AddChildToOverlay(Chip);ChipSlot->SetHorizontalAlignment(HAlign_Right);ChipSlot->SetVerticalAlignment(VAlign_Top);
            auto* TopSlot=Deck->AddChildToOverlay(Box);TopSlot->SetHorizontalAlignment(HAlign_Fill);TopSlot->SetVerticalAlignment(VAlign_Fill);
            auto* Column=WidgetTree->ConstructWidget<UVerticalBox>();
            Column->AddChildToVerticalBox(Deck)->SetHorizontalAlignment(HAlign_Center);
            UTextBlock* Caption=Line(LDTerms::Storage,13,LDHUDStyle::TextSecondary);StorageBold(Caption,1);
            auto* CaptionSlot=Column->AddChildToVerticalBox(Caption);CaptionSlot->SetHorizontalAlignment(HAlign_Center);CaptionSlot->SetPadding(FMargin(0,4,0,0));
            StackCapacity=Line(TEXT(""),12,LDHUDStyle::TextSecondary);StorageBold(StackCapacity,1);
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
            UTextBlock* Here=Line(LDTerms::DropHere,12,StorageLegal);Here->SetJustification(ETextJustify::Center);
            auto* Placeholder=WidgetTree->ConstructWidget<UBorder>();Placeholder->SetBrush(StorageRound(6.f,StorageLegal,1.f));
            Placeholder->SetBrushColor(StorageLegal.CopyWithNewOpacity(.10f));Placeholder->SetHorizontalAlignment(HAlign_Center);
            Placeholder->SetVerticalAlignment(VAlign_Center);Placeholder->SetContent(Here);
            auto* PlaceholderBox=WidgetTree->ConstructWidget<USizeBox>();PlaceholderBox->SetWidthOverride(StorageTile);PlaceholderBox->SetHeightOverride(StorageTile*.7f);
            PlaceholderBox->SetContent(Placeholder);PlaceholderBox->SetVisibility(ESlateVisibility::Collapsed);Column.Placeholder=PlaceholderBox;
            Box->AddChildToVerticalBox(PlaceholderBox)->SetHorizontalAlignment(HAlign_Center);
            Column.Title=Line(TEXT(""),13,LDHUDStyle::TextSecondary);StorageBold(Column.Title,1);Column.Title->SetJustification(ETextJustify::Center);
            Column.Title->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
            auto* TitleSlot=Box->AddChildToVerticalBox(Column.Title);TitleSlot->SetHorizontalAlignment(HAlign_Fill);TitleSlot->SetPadding(FMargin(0,4,0,0));
            Column.Panel=WidgetTree->ConstructWidget<UBorder>();Column.Panel->SetBrush(StorageRound(8.f,LDHUDStyle::PanelRim,1.f));
            Column.Panel->SetBrushColor(StorageColumnFill);Column.Panel->SetPadding(FMargin(4,6));Column.Panel->SetContent(Box);
            Column.Frame=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),FName(*FString::Printf(TEXT("HUDStorageColumn%d"),Index)));
            Column.Frame->SetWidthOverride(StorageColumnWidth);Column.Frame->SetClipping(EWidgetClipping::ClipToBounds);Column.Frame->SetContent(Column.Panel);Column.Frame->SetVisibility(ESlateVisibility::Collapsed);
            Column.Slot=Add(Column.Frame,2);
        }
        {
            // The description of the hovered component, in the slot popup's look; it takes no pointer input.
            auto* Box=WidgetTree->ConstructWidget<UVerticalBox>();
            PopupName=Line(TEXT(""),17,LDHUDStyle::TextPrimary);StorageBold(PopupName,0);PopupName->SetWrapTextAt(StoragePopupWidth-28.f);
            Box->AddChildToVerticalBox(PopupName);
            PopupWhere=Line(TEXT(""),13,LDHUDStyle::TextAccent);PopupWhere->SetWrapTextAt(StoragePopupWidth-28.f);
            Box->AddChildToVerticalBox(PopupWhere)->SetPadding(FMargin(0,2,0,0));
            PopupBody=LDWidgets::RichText(WidgetTree,14,TEXT("HUDStoragePopupBody"));PopupBody->SetAutoWrapText(false);PopupBody->SetWrapTextAt(StoragePopupWidth-28.f);
            Box->AddChildToVerticalBox(PopupBody)->SetPadding(FMargin(0,8,0,0));
            PopupHint=Line(TEXT(""),12,LDHUDStyle::TextSecondary);PopupHint->SetWrapTextAt(StoragePopupWidth-28.f);
            Box->AddChildToVerticalBox(PopupHint)->SetPadding(FMargin(0,8,0,0));
            auto* Panel=LDWidgets::Panel(WidgetTree,Box,FMargin(14,12));Panel->SetBrushColor(FLinearColor(.02f,.035f,.055f,.94f));
            auto* Width=WidgetTree->ConstructWidget<USizeBox>();Width->SetWidthOverride(StoragePopupWidth);Width->SetContent(Panel);
            Width->SetVisibility(ESlateVisibility::Collapsed);PopupFrame=Width;PopupSlot=Add(Width,6);
        }
        {
            // The chooser a B18 or C12 drop opens at the drop point: one button per element or base, then 取消.
            auto* Box=WidgetTree->ConstructWidget<UVerticalBox>();
            ChooserTitle=Line(TEXT(""),15,LDHUDStyle::TextPrimary);StorageBold(ChooserTitle,0);ChooserTitle->SetWrapTextAt(300.f);
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
            GhostCaption=Line(TEXT(""),13,LDHUDStyle::TextPrimary);GhostCaption->SetAutoWrapText(false);
            auto* Panel=LDWidgets::Panel(WidgetTree,GhostCaption,FMargin(8,4));Panel->SetBrushColor(FLinearColor(.02f,.035f,.055f,.94f));
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
    auto* Face=WidgetTree->ConstructWidget<UBorder>();Face->SetBrush(StorageRound(6.f,FLinearColor(1,1,1,.18f),1.f));Face->SetBrushColor(StorageIconFill);
    Face->SetPadding(FMargin(0));Face->SetHorizontalAlignment(HAlign_Fill);Face->SetVerticalAlignment(VAlign_Fill);Face->SetContent(OutStack);
    OutBox=WidgetTree->ConstructWidget<USizeBox>();OutBox->SetWidthOverride(Size);OutBox->SetHeightOverride(Size);OutBox->SetContent(Face);
    return Face;
}
void ULDComponentStorage::ShowIcon(UImage* Picture,UTextBlock* Glyph,FName Component) const
{
    // An empty stack shows a blank face: there is no component to name.
    if(Component.IsNone()){StorageText(Glyph,FString());StorageShown(Picture,false);return;}
    const LDHUD::FComponentCardView View=LDHUD::ComponentCard(Component,LatestContent,LatestStats);
    StorageText(Glyph,View.Glyph);
    const bool bPicture=LDUIIcons::Apply(Picture,View.IconId,6.f);
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
    bShownNow=bShown&&Latest.bValid&&Anchors.bValid;
    if(!bShownNow){CancelInteraction();Press=FPress();bExpanded=false;}
    StorageShown(StackFrame,bShownNow,ESlateVisibility::Visible);
    const LDHUD::FStorageView Model=LDHUD::ComponentStorage(Latest,Content,&SeenComponents);
    // The cards and tiles are rebuilt only for another build, catalog or numbers.
    FString Key=FString::Printf(TEXT("%u|%llu|%p|%u|%lld|%lld|%lld|%lld"),Match,Latest.BuildRevision,Content,uint32(Stats.Level),
        int64(Stats.ADP2),int64(Stats.APP2),int64(Stats.MaxHPP2),int64(Stats.ArmorP2));
    for(const FName& Id:Model.Stored){Key+=TEXT("|")+Id.ToString();}
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
            const FLDBuildCommand Install=Follow.Install;Follow=FFollowUp();
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
    Built.Reset();Cards.Reset();Tiles.Reset();
    CardList->ClearChildren();
    for(const FName& Id:Model.Stored)
    {
        const FLDComponentInstanceView* Owned=StorageOwned(Latest,Id);
        BuildCard(Id,Owned&&!Owned->bEverInstalled&&!SeenComponents.Contains(Id));
    }
    for(int32 Index=0;Index<Columns.Num()&&Model.Columns.IsValidIndex(Index);++Index)
    {
        FColumn& Column=Columns[Index];Column.List->ClearChildren();Column.AppliedLook=-1;
        const LDHUD::FStorageColumnView& Shown=Model.Columns[Index];
        // Only 本体 shows its title; the others keep theirs (the active's name or 冲刺) for the notices that name a place.
        StorageText(Column.Title,Shown.Title);Column.Active=Shown.Active;
        StorageShown(Column.Title,Index==int32(LDHUD::EStorageColumn::Body),ESlateVisibility::HitTestInvisible);
        for(const LDHUD::FEquippedView& Entry:Shown.Entries){BuildTile(Column,Entry,static_cast<LDHUD::EStorageColumn>(Index));}
    }
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
        UTextBlock* Fresh=LDWidgets::Text(WidgetTree,TEXT("新"),11);Fresh->SetAutoWrapText(false);Fresh->SetColorAndOpacity(LDHUDStyle::Rim);
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
    for(const FCard& Card:Cards){if(Card.Component==Component){StorageShown(Card.FreshChip,false);}}
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
        UTextBlock* Note=LDWidgets::Text(WidgetTree,Entry.Note,11);Note->SetAutoWrapText(false);StorageBold(Note,2);Built.Add(Note);
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

bool ULDComponentStorage::OverExpanded(FVector2D At) const
{
    if(StorageUnder(ListFrame,At)||StorageUnder(ChooserFrame,At)){return true;}
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
    if(StorageUnder(ListFrame,At)||StorageUnder(ChooserFrame,At)){return true;}
    for(const FColumn& Column:Columns){if(StorageUnder(Column.Frame,At)){return true;}}
    return false;
}
TArray<const UWidget*> ULDComponentStorage::ReservedWidgets() const
{
    TArray<const UWidget*> Out;
    for(const UWidget* Widget:{StackFrame.Get(),ListFrame.Get(),ChooserFrame.Get(),PopupFrame.Get()}){if(Widget&&Widget->IsVisible()){Out.Add(Widget);}}
    for(const FColumn& Column:Columns){if(Column.Frame&&Column.Frame->IsVisible()){Out.Add(Column.Frame);}}
    return Out;
}
FReply ULDComponentStorage::NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event)
{
    if(Event.GetEffectingButton()==EKeys::LeftMouseButton&&BeginPress(Event.GetScreenSpacePosition())){return FReply::Handled();}
    return Super::NativeOnMouseButtonDown(Geometry,Event);
}
bool ULDComponentStorage::BeginPress(FVector2D At)
{
    if(!bShownNow){return false;}
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
        // A press that never moves is only a click: the card stays focused, nothing is sent.
        if(!bHeld){Press=FPress();return;}
        if(FVector2D::Distance(Pointer,Press.At)<StorageDragStartPx){return;}
        Drag.Component=Press.Component;Drag.bFromColumn=Press.bFromColumn;Drag.bActive=true;
        Drag.Drops=LDHUD::ComponentDrops(Drag.Component,Latest,LatestContent);
        Drag.Drops.Append(LDHUD::ReplaceDrops(Drag.Component,Latest,LatestContent));
        ShowIcon(GhostPicture,GhostGlyph,Drag.Component);
        StorageShown(GhostFrame,true,ESlateVisibility::HitTestInvisible);
        return;
    }
    if(bHeld){return;}
    // Let go: the storage takes back an installed component; a column or host installs or moves it.
    const FName Component=Drag.Component;const bool bFromColumn=Drag.bFromColumn;
    EndDrag();
    LDHUD::EStorageColumn Column=LDHUD::EStorageColumn::Body;FName Host;
    if(StorageAt(Pointer)){if(bFromColumn){DropInStorage(Component);}}
    else if(ColumnAt(Pointer,Column,Host)){Chooser.At=Pointer;DropOn(Component,Column,Host);}
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
    return StorageUnder(StackFrame,At)||StorageUnder(ListFrame,At);
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
        Follow=FFollowUp();
        const FString Why=LDHUD::GrowthReasonText(Reason);
        if(Notify){Notify(FString(LDTerms::Storage)+TEXT("：没有提交：")+(Why.IsEmpty()?FString(TEXT("原因未知")):Why));}
        return false;
    }
    if(Answers==Before){PendingRequest=Request;}
    return true;
}
void ULDComponentStorage::FinishRequest(const FString& Text)
{
    PendingRequest=0;
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
    FinishRequest(LDHUD::BuildReceiptText(Receipt,LatestContent));
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
        FinishRequest(TEXT("没有受理：")+LDHUD::GrowthReasonText(LDGrowthWire::Reason(Result.Reason)));
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
        for(UWidget* Hidden:{ListFrame.Get(),PopupFrame.Get(),ChooserFrame.Get(),GhostFrame.Get(),GhostCaptionFrame.Get()}){StorageShown(Hidden,false);}
        for(const FColumn& Column:Columns){StorageShown(Column.Frame,false);}
        return;
    }
    const double Now=GetWorld()?GetWorld()->GetRealTimeSeconds():0;
    const FVector2D Pointer=FSlateApplication::Get().GetCursorPos();
    const bool bHeld=FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::LeftMouseButton);
    UpdateDrag(Pointer,bHeld);
    // Hovering the stack opens the view; it stays while the pointer is over any part of it (or a slot under a column), a
    // drag or the chooser runs, and closes a moment after the pointer left.
    const bool bOverStack=StorageUnder(StackFrame,Pointer);
    const bool bInside=bPinned||bDetailsHeld||bOverStack||(bExpanded&&OverExpanded(Pointer))||Drag.bActive||Press.bArmed||Chooser.bOpen;
    if(bInside){LastInsideAt=Now;if(bOverStack||bPinned||bDetailsHeld){bExpanded=true;}}
    else if(bExpanded&&Now-LastInsideAt>StorageCloseGrace){bExpanded=false;}
    Layout(Geometry,Pointer);
}
void ULDComponentStorage::Layout(const FGeometry& Geometry,FVector2D Pointer)
{
    const FVector2D Area=Geometry.GetLocalSize();
    const auto Local=[&Geometry](FVector2D Absolute){return Geometry.AbsoluteToLocal(Absolute);};
    // The stack sits right of the band, on its bottom edge.
    StoragePlace(StackSlot,Local(FVector2D(Anchors.Band.Right,Anchors.Band.Bottom))+FVector2D(StorageGapFromBand,0),FVector2D(0,1));
    StorageShown(ListFrame,bExpanded&&!Cards.IsEmpty(),ESlateVisibility::Visible);
    const FSlateRect Stack=StorageRect(StackFrame);
    const FVector2D StackTopLeft=Local(FVector2D(Stack.Left,Stack.Top));
    if(bExpanded)
    {
        // The list rises from the stack; the hovered icon is edged and a dragged one fades.
        StoragePlace(ListSlot,FVector2D(StackTopLeft.X,StackTopLeft.Y-6.f),FVector2D(0,1));
        for(FCard& Card:Cards)
        {
            const bool bHovered=!Drag.bActive&&StorageUnder(Card.Face,Pointer);
            const bool bDragged=Drag.bActive&&Drag.Component==Card.Component;
            const int32 Look=(bHovered?1:0)|(bDragged?2:0);
            if(Card.AppliedLook==Look){continue;}
            Card.AppliedLook=Look;
            Card.Face->SetBrush(StorageRound(6.f,bHovered?LDHUDStyle::TextAccent:FLinearColor(1,1,1,.18f),bHovered?2.f:1.f));Card.Face->SetBrushColor(StorageIconFill);
            Card.Size->SetRenderOpacity(bDragged?.35f:1.f);
        }
    }
    // The columns rise over their anchors in one row: 本体 (left of the first Q/W/E slot), Q/W/E and D, each centred.
    // While a drag runs, a column or host that takes the component is edged green and one that does not is dimmed.
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
        const bool bOwnDrop=Drag.bActive&&Drag.Drops.ContainsByPredicate([Id](const LDHUD::FComponentDrop& Drop){return Drop.Column==Id&&Drop.Host.IsNone();});
        const bool bAnyDrop=Drag.bActive&&Drag.Drops.ContainsByPredicate([Id](const LDHUD::FComponentDrop& Drop){return Drop.Column==Id&&Drop.Replacing.IsNone();});
        StorageShown(Column.Placeholder,bOwnDrop,ESlateVisibility::HitTestInvisible);
        const int32 Look=!Drag.bActive?0:bAnyDrop?1:2;
        if(Column.AppliedLook!=Look)
        {
            Column.AppliedLook=Look;
            Column.Panel->SetBrush(StorageRound(8.f,Look==1?StorageLegal:LDHUDStyle::PanelRim,Look==1?2.f:1.f));
            Column.Panel->SetBrushColor(StorageColumnFill);
            Column.Frame->SetRenderOpacity(Look==2?.45f:1.f);
        }
    }
    // A tile the component installs on is edged green; one it would replace is edged amber.
    for(FTile& Tile:Tiles)
    {
        const LDHUD::FComponentDrop* OnTile=Drag.bActive?Drag.Drops.FindByPredicate([&Tile](const LDHUD::FComponentDrop& Drop){return Drop.Host==Tile.Component;}):nullptr;
        const bool bHost=OnTile&&OnTile->Replacing.IsNone();const bool bReplace=OnTile&&!OnTile->Replacing.IsNone();
        const bool bDragged=Drag.bActive&&Drag.Component==Tile.Component;
        const int32 Look=(bHost?1:0)|(bDragged?2:0)|(bReplace?4:0);
        if(Tile.AppliedLook==Look){continue;}
        Tile.AppliedLook=Look;
        const FLinearColor Rim=bHost?StorageLegal:bReplace?StorageReplace:FLinearColor(1,1,1,.18f);
        Tile.Face->SetBrush(StorageRound(6.f,Rim,bHost||bReplace?2.f:1.f));Tile.Face->SetBrushColor(StorageIconFill);
        Tile.Face->SetRenderOpacity(bDragged?.35f:1.f);
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
        const bool bCanInstall=!bInstalled&&!LDHUD::ComponentDrops(Shown,Latest,LatestContent).IsEmpty();
        const bool bCanReplace=!bInstalled&&!LDHUD::ReplaceDrops(Shown,Latest,LatestContent).IsEmpty();
        const FString Hint=bInstalled?FString(TEXT("拖回构件库拆下，拖到别处移动"))
            :bCanInstall?FString(TEXT("拖到对应位置安装")):bCanReplace?FString(LDTerms::ReplaceAvailable):FString();
        const FString Links=LDHUD::ConnectionsText(Shown,Latest,LatestContent);
        StorageText(PopupHint,Links.IsEmpty()?Hint:Hint.IsEmpty()?Links:Links+TEXT("\n")+Hint);
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
    // The chooser sits over the drop point; the ghost follows the pointer.
    if(Chooser.bOpen)
    {
        const FVector2D Size=ChooserFrame->GetDesiredSize();const FVector2D At=Local(Chooser.At);
        const float X=FMath::Clamp(float(At.X),float(Size.X)*.5f+StorageScreenMargin,FMath::Max(float(Size.X)*.5f+StorageScreenMargin,float(Area.X-Size.X*.5f)-StorageScreenMargin));
        StoragePlace(ChooserSlot,FVector2D(X,FMath::Max(float(At.Y)-12.f,float(Size.Y)+StorageScreenMargin)),FVector2D(.5f,1));
    }
    if(Drag.bActive)
    {
        StoragePlace(GhostSlot,Local(Pointer),FVector2D(.5f,.5f));
        // Over a column or tile the caption names what letting go there does; elsewhere it hides.
        LDHUD::EStorageColumn Column=LDHUD::EStorageColumn::Body;FName Host;
        const bool bOverPlace=!StorageAt(Pointer)&&ColumnAt(Pointer,Column,Host);
        if(bOverPlace)
        {
            const LDHUD::FComponentDrop* Drop=Host.IsNone()?nullptr:Drag.Drops.FindByPredicate([Column,Host](const LDHUD::FComponentDrop& D){return D.Column==Column&&D.Host==Host;});
            if(!Drop){Drop=Drag.Drops.FindByPredicate([Column](const LDHUD::FComponentDrop& D){return D.Column==Column&&D.Host.IsNone();});}
            StorageText(GhostCaption,Drop?LDHUD::DropPreviewText(Drag.Component,*Drop,Latest,LatestContent):FString(LDTerms::DropNowhere));
            GhostCaption->SetColorAndOpacity(FSlateColor(!Drop?LDHUDStyle::TextSecondary:Drop->Replacing.IsNone()?StorageLegal:StorageReplace));
            StoragePlace(GhostCaptionSlot,Local(Pointer)+FVector2D(StorageTile*.5f+6.f,0),FVector2D(0,.5f));
        }
        StorageShown(GhostCaptionFrame,bOverPlace,ESlateVisibility::HitTestInvisible);
    }
}
