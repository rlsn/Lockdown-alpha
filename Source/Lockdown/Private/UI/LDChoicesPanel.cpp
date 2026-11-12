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
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/WrapBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"

namespace
{
const FLinearColor GrowthCardIdle(.045f,.12f,.15f);
const FLinearColor TrayIconBlocked(.42f,.42f,.42f);
// A tray card is the icon over one line of name; a grid tile is the icon alone. A tray line takes four cards or eight tiles.
constexpr float TrayCardWidth=114.f,TrayCardIcon=64.f,TrayTileSize=54.f,TrayTileIcon=46.f,TrayCardGap=8.f,TrayTileGap=6.f;
constexpr float TrayIconRadius=7.f,TrayCardPadding=4.f;
constexpr float TrayWidth=4*TrayCardWidth+3*TrayCardGap;
static_assert(8*TrayTileSize+7*TrayTileGap<=TrayWidth,"eight grid tiles fit on one tray line");
static_assert(TrayTileIcon+2*TrayCardPadding<=TrayTileSize,"a grid tile holds its icon");
constexpr float ServicePanelWidth=540.f;
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
UWrapBox* TrayRow(UWidgetTree* Tree)
{
    auto* Row=Tree->ConstructWidget<UWrapBox>();Row->SetExplicitWrapSize(true);Row->SetWrapSize(TrayWidth+1.f);
    Row->SetInnerSlotPadding(FVector2D(TrayCardGap,TrayCardGap));Row->SetHorizontalAlignment(HAlign_Right);
    Row->SetVisibility(ESlateVisibility::Collapsed);
    return Row;
}
void TrayRowTiles(UWrapBox* Row,bool bTiles)
{
    const FVector2D Gap=bTiles?FVector2D(TrayTileGap,TrayTileGap):FVector2D(TrayCardGap,TrayCardGap);
    if(Row&&!Row->GetInnerSlotPadding().Equals(Gap)){Row->SetInnerSlotPadding(Gap);}
}
}

TSharedRef<SWidget> ULDChoicesPanel::RebuildWidget()
{
    if(!Tray)
    {
        auto Text=[this](const FString& Value,int32 Size){return LDWidgets::Text(WidgetTree,Value,Size);};
        auto OneLine=[&Text](int32 Size){UTextBlock* Line=Text(TEXT(""),Size);Line->SetAutoWrapText(false);Line->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);return Line;};
        auto Wrapped=[&Text](int32 Size,float Width){UTextBlock* Line=Text(TEXT(""),Size);Line->SetAutoWrapText(false);Line->SetWrapTextAt(Width);return Line;};
        {
            // The tray: one row per pending decision, stacked upward from the 构筑 button and right-aligned to it; the offer
            // is nearest the button. Rows and cards are built as needed (the offer's three up front).
            Tray=WidgetTree->ConstructWidget<UVerticalBox>();Tray->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
            const auto AddRow=[this](UWidget* Row){auto* RowSlot=Tray->AddChildToVerticalBox(Row);RowSlot->SetHorizontalAlignment(HAlign_Right);RowSlot->SetPadding(FMargin(0,TrayCardGap,0,0));};
            BlueprintCardRow=TrayRow(WidgetTree);AddRow(BlueprintCardRow);
            ChoiceRowList=WidgetTree->ConstructWidget<UVerticalBox>();ChoiceRowList->SetVisibility(ESlateVisibility::SelfHitTestInvisible);AddRow(ChoiceRowList);
            BaseRow=TrayRow(WidgetTree);AddRow(BaseRow);
            OfferRow=TrayRow(WidgetTree);AddRow(OfferRow);
            for(int32 Index=0;Index<3;++Index)
            {
                FTrayCard Card=MakeCard(FName(*FString::Printf(TEXT("GrowthOffer%d"),Index)),[this,Index](){ChooseOffer(Index);});
                OfferRow->AddChildToWrapBox(Card.Frame);OfferCards.Add(Card);
            }
            TrayFrame=WidgetTree->ConstructWidget<UBorder>();TrayFrame->SetBrushColor(FLinearColor::Transparent);TrayFrame->SetPadding(FMargin(0,0,16,72));
            TrayFrame->SetHorizontalAlignment(HAlign_Right);TrayFrame->SetVerticalAlignment(VAlign_Bottom);
            TrayFrame->SetVisibility(ESlateVisibility::Collapsed);TrayFrame->SetContent(Tray);
        }
        {
            // The trainer's pending blueprints: one row each, built as needed, with a "学到 Q/W/E" button per empty slot and a
            // "替换 Q/W/E" per occupied one.
            auto* Section=WidgetTree->ConstructWidget<UVerticalBox>();
            UTextBlock* Heading=OneLine(16);Heading->SetColorAndOpacity(LDHUDStyle::TextAccent);Heading->SetText(FText::FromString(LDTerms::Blueprint));
            Section->AddChildToVerticalBox(Heading);
            BlueprintList=WidgetTree->ConstructWidget<UVerticalBox>();Section->AddChildToVerticalBox(BlueprintList)->SetPadding(FMargin(0,4,0,0));
            Section->SetVisibility(ESlateVisibility::Collapsed);
            BlueprintSection=Section;
        }
        {
            // #198 rest services: shown in a rest, enabled only in its build windows, each with its own reason when refused.
            auto* Section=WidgetTree->ConstructWidget<UVerticalBox>();
            auto ServiceLine=[&](UWidget* Lead,UTextBlock*& OutBlocked)
            {
                auto* Line=WidgetTree->ConstructWidget<UVerticalBox>();
                Line->AddChildToVerticalBox(Lead)->SetHorizontalAlignment(HAlign_Left);
                OutBlocked=Wrapped(15,ServicePanelWidth-20.f);OutBlocked->SetColorAndOpacity(LDHUDStyle::TextSecondary);
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
                auto* Line=WidgetTree->ConstructWidget<UWrapBox>();Line->SetExplicitWrapSize(true);Line->SetWrapSize(ServicePanelWidth-24);
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
            RestConfirmText=Wrapped(16,ServicePanelWidth-220.f);RestConfirmText->SetColorAndOpacity(LDHUDStyle::TextWarning);
            auto* ConfirmTextSlot=ConfirmRow->AddChildToHorizontalBox(RestConfirmText);ConfirmTextSlot->SetVerticalAlignment(VAlign_Center);
            ConfirmTextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            RestConfirmButton=LDWidgets::Button(WidgetTree,LDTerms::ConfirmAction,[this](){ConfirmRestAction();},false,TEXT("GrowthRestConfirm"));
            ConfirmRow->AddChildToHorizontalBox(RestConfirmButton)->SetPadding(FMargin(8,0,0,0));
            ConfirmRow->AddChildToHorizontalBox(LDWidgets::Button(WidgetTree,LDTerms::CancelAction,[this](){CancelRestAction();},false,TEXT("GrowthRestCancel")))
                ->SetPadding(FMargin(6,0,0,0));
            ConfirmRow->SetVisibility(ESlateVisibility::Collapsed);
            Section->SetVisibility(ESlateVisibility::Collapsed);
            RestSection=Section;RestConfirmLine=ConfirmRow;
        }
        // The NPC service window: its name and 关闭, then the services. It never scrolls: a tall window is scaled down to fit.
        auto* ServiceContent=WidgetTree->ConstructWidget<UVerticalBox>();
        auto* ServiceHeader=WidgetTree->ConstructWidget<UHorizontalBox>();
        ServiceHeader->AddChildToHorizontalBox(Text(TEXT("NPC 服务"),22))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        ServiceHeader->AddChildToHorizontalBox(LDWidgets::Button(WidgetTree,TEXT("关闭"),[this](){ClosePanel();},false,TEXT("GrowthServiceClose")));
        ServiceContent->AddChildToVerticalBox(ServiceHeader)->SetPadding(FMargin(0,0,0,8));
        ServiceBody=WidgetTree->ConstructWidget<UVerticalBox>();
        ServiceBody->AddChildToVerticalBox(RestSection);ServiceBody->AddChildToVerticalBox(RestConfirmLine);
        ServiceBody->AddChildToVerticalBox(BlueprintSection)->SetPadding(FMargin(0,10,0,0));
        auto* ServiceSize=WidgetTree->ConstructWidget<USizeBox>();ServiceSize->SetWidthOverride(ServicePanelWidth);ServiceSize->SetContent(ServiceBody);
        ServiceContent->AddChildToVerticalBox(ServiceSize);
        ServicePanel=LDWidgets::Panel(WidgetTree,ServiceContent,FMargin(20));ServicePanel->SetBrushColor(LDHUDStyle::PanelFill.CopyWithNewOpacity(1.f));
        auto* ServiceScale=WidgetTree->ConstructWidget<UScaleBox>();ServiceScale->SetStretch(EStretch::ScaleToFit);ServiceScale->SetStretchDirection(EStretchDirection::DownOnly);
        ServiceScale->SetContent(ServicePanel);
        auto* ServiceFrame=WidgetTree->ConstructWidget<UBorder>();ServiceFrame->SetBrushColor(FLinearColor::Transparent);ServiceFrame->SetPadding(FMargin(0,86,20,118));
        ServiceFrame->SetHorizontalAlignment(HAlign_Right);ServiceFrame->SetVerticalAlignment(VAlign_Top);ServiceFrame->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        ServiceFrame->SetContent(ServiceScale);
        ServicePanel->SetVisibility(ESlateVisibility::Collapsed);
        auto* Layers=WidgetTree->ConstructWidget<UOverlay>();Layers->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        for(UWidget* Layer:{static_cast<UWidget*>(TrayFrame.Get()),static_cast<UWidget*>(ServiceFrame)})
        {auto* LayerSlot=Layers->AddChildToOverlay(Layer);LayerSlot->SetHorizontalAlignment(HAlign_Fill);LayerSlot->SetVerticalAlignment(VAlign_Fill);}
        WidgetTree->RootWidget=Layers;
        SetVisibility(ESlateVisibility::Collapsed);
    }
    return Super::RebuildWidget();
}
ULDChoicesPanel::FTrayCard ULDChoicesPanel::MakeCard(FName ButtonName,TFunction<void()> Click)
{
    FTrayCard Card;
    auto* Face=WidgetTree->ConstructWidget<UVerticalBox>();RowWidgets.Add(Face);
    auto* IconBox=WidgetTree->ConstructWidget<UOverlay>();RowWidgets.Add(IconBox);
    auto* Shape=WidgetTree->ConstructWidget<UBorder>();Shape->SetVisibility(ESlateVisibility::HitTestInvisible);RowWidgets.Add(Shape);
    Shape->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White,TrayIconRadius,FLinearColor(1,1,1,.55f),1.5f));Shape->SetBrushColor(FLinearColor(.12f,.13f,.15f));
    auto* ShapeSlot=IconBox->AddChildToOverlay(Shape);ShapeSlot->SetHorizontalAlignment(HAlign_Fill);ShapeSlot->SetVerticalAlignment(VAlign_Fill);
    Card.Picture=WidgetTree->ConstructWidget<UImage>();Card.Picture->SetVisibility(ESlateVisibility::Collapsed);RowWidgets.Add(Card.Picture);
    auto* PictureSlot=IconBox->AddChildToOverlay(Card.Picture);PictureSlot->SetHorizontalAlignment(HAlign_Fill);PictureSlot->SetVerticalAlignment(VAlign_Fill);
    Card.Glyph=LDWidgets::Text(WidgetTree,TEXT(""),22);Card.Glyph->SetAutoWrapText(false);Card.Glyph->SetJustification(ETextJustify::Center);
    ScaleNewText(Card.Glyph);RowWidgets.Add(Card.Glyph);
    auto* GlyphSlot=IconBox->AddChildToOverlay(Card.Glyph);GlyphSlot->SetHorizontalAlignment(HAlign_Center);GlyphSlot->SetVerticalAlignment(VAlign_Center);
    Card.IconFrame=WidgetTree->ConstructWidget<USizeBox>();Card.IconFrame->SetWidthOverride(TrayCardIcon);Card.IconFrame->SetHeightOverride(TrayCardIcon);
    Card.IconFrame->SetContent(IconBox);RowWidgets.Add(Card.IconFrame);
    Face->AddChildToVerticalBox(Card.IconFrame)->SetHorizontalAlignment(HAlign_Center);
    Card.Name=LDWidgets::Text(WidgetTree,TEXT(""),15);Card.Name->SetAutoWrapText(false);Card.Name->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
    Card.Name->SetJustification(ETextJustify::Center);ScaleNewText(Card.Name);RowWidgets.Add(Card.Name);
    auto* NameSlot=Face->AddChildToVerticalBox(Card.Name);NameSlot->SetHorizontalAlignment(HAlign_Fill);NameSlot->SetPadding(FMargin(0,4,0,0));
    Card.Button=LDWidgets::Button(WidgetTree,TEXT(""),MoveTemp(Click),false,ButtonName);RowWidgets.Add(Card.Button);
    Card.Button->SetContent(Face);GrowthTint(Card.Button,GrowthCardIdle);
    {auto Style=Card.Button->GetStyle();Style.NormalPadding=FMargin(TrayCardPadding);Style.PressedPadding=FMargin(TrayCardPadding);Card.Button->SetStyle(Style);}
    // After SetContent: replacing the caption makes a new, centred slot; the face fills the card instead.
    if(UButtonSlot* FaceSlot=Cast<UButtonSlot>(Card.Button->GetContentSlot())){FaceSlot->SetHorizontalAlignment(HAlign_Fill);FaceSlot->SetVerticalAlignment(VAlign_Center);}
    Card.Frame=WidgetTree->ConstructWidget<USizeBox>();Card.Frame->SetWidthOverride(TrayCardWidth);Card.Frame->SetContent(Card.Button);
    Card.Frame->SetVisibility(ESlateVisibility::Collapsed);RowWidgets.Add(Card.Frame);
    return Card;
}
bool ULDChoicesPanel::CardStale(const FTrayCard& Card,const FString& Key) const
{
    return bCardsStale||!Card.ShownKey.Equals(Key,ESearchCase::CaseSensitive);
}
void ULDChoicesPanel::ShowFace(FTrayCard& Card,const FString& Key,const FString& Name,const FString& IconId,const FString& Glyph,
    const LDHUD::FActionDetailView& Detail)
{
    Card.ShownKey=Key;Card.Detail=Detail;
    GrowthText(Card.Name,Name);GrowthText(Card.Glyph,Glyph);
    Card.AppliedIcon=IconId;Card.AppliedEnabled=-1;
}
void ULDChoicesPanel::ShowLook(FTrayCard& Card,bool bTile,bool bEnabled)
{
    GrowthShown(Card.Frame,true);
    if(Card.bTile!=bTile)
    {
        Card.bTile=bTile;
        Card.Frame->SetWidthOverride(bTile?TrayTileSize:TrayCardWidth);
        const float Icon=bTile?TrayTileIcon:TrayCardIcon;Card.IconFrame->SetWidthOverride(Icon);Card.IconFrame->SetHeightOverride(Icon);
    }
    GrowthShown(Card.Name,!bTile,ESlateVisibility::HitTestInvisible);
    GrowthEnabled(Card.Button,bEnabled);
    // A card that cannot be taken now greys its picture too; the popup says why.
    if(Card.AppliedEnabled!=int32(bEnabled))
    {
        Card.AppliedEnabled=int32(bEnabled);
        const bool bPicture=LDUIIcons::Apply(Card.Picture,Card.AppliedIcon,TrayIconRadius,bEnabled?FLinearColor::White:TrayIconBlocked);
        GrowthShown(Card.Glyph,!bPicture,ESlateVisibility::HitTestInvisible);
    }
}
void ULDChoicesPanel::SetTrayInset(float Right,float Bottom)
{
    const FMargin Inset(0,0,Right,Bottom);
    if(TrayFrame&&!(TrayFrame->GetPadding()==Inset)){TrayFrame->SetPadding(Inset);}
}
const UWidget* ULDChoicesPanel::Hovered(FVector2D Pointer,LDHUD::FActionDetailView& Out) const
{
    if(!bOpen||!Tray){return nullptr;}
    const FTrayCard* Found=nullptr;
    const auto Over=[&Found,Pointer](const FTrayCard& Card)
    {
        if(!Found&&Card.Frame&&Card.Frame->IsVisible()&&Card.Frame->GetCachedGeometry().IsUnderLocation(Pointer)){Found=&Card;}
    };
    const auto Row=[&Over](const UWidget* Box,const TArray<FTrayCard>& Cards){if(Box&&Box->IsVisible()){for(const FTrayCard& Card:Cards){Over(Card);}}};
    Row(OfferRow,OfferCards);Row(BaseRow,BaseCards);Row(BlueprintCardRow,BlueprintCards);
    for(const FChoiceRow& Choice:ChoiceRows){Row(Choice.Row,Choice.Options);if(Choice.Row->IsVisible()){Over(Choice.Points);}}
    if(!Found&&OfferRow->IsVisible()&&OfferCards.IsValidIndex(PinnedPreview)&&OfferCards[PinnedPreview].Frame->IsVisible()){Found=&OfferCards[PinnedPreview];}
    if(!Found){return nullptr;}
    Out=Found->Detail;
    return Found->Frame;
}
bool ULDChoicesPanel::IsEngaged() const
{
    return (bOpen&&Tray&&Tray->IsHovered())||PendingRequest!=0||HasChildConfirmation()||bFillGapOpen||bDismantleOpen;
}
bool ULDChoicesPanel::HasCards() const
{
    return Usable()&&LDHUD::GrowthPending(&Latest,LatestMatch,LatestContent).Any();
}
void ULDChoicesPanel::Tell(const FString& Text)
{
    if(!Text.IsEmpty()&&Notify){Notify(Text);}
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
        Tell(Service==ELDRestService::None?TEXT("已离开 NPC，服务已收起。"):TEXT("服务对象已变化，请重新与 NPC 交互。"));
    }
    else if(Previous!=ELDRestService::None&&Service==ELDRestService::None){Tell(TEXT("已离开 NPC，服务已收起。"));}
    RefreshAll();
}
UWidget* ULDChoicesPanel::FocusScope() const{return bServicePanelOpen?static_cast<UWidget*>(ServicePanel.Get()):static_cast<UWidget*>(Tray.Get());}
UWidget* ULDChoicesPanel::HitArea() const{return bServicePanelOpen?static_cast<UWidget*>(ServicePanel.Get()):static_cast<UWidget*>(Tray.Get());}
void ULDChoicesPanel::OpenPanel()
{
    if(const auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());PC&&PC->BuildBlueprints&&PC->BuildBlueprints->IsViewerOpen())
    {PC->BuildBlueprints->ToggleViewer();}
    bServicePanelOpen=false;GrowthShown(ServicePanel,false);GrowthShown(TrayFrame,true);
    bOpen=true;SetVisibility(ESlateVisibility::SelfHitTestInvisible);Fold.Opened(false,UiNow());
    RefreshAll();
}
bool ULDChoicesPanel::OpenServicePanel()
{
    if(ServiceContext==ELDRestService::None){return false;}
    bOpen=false;Fold.Closed();PinnedPreview=INDEX_NONE;bServicePanelOpen=true;
    GrowthShown(TrayFrame,false);GrowthShown(ServicePanel,true,ESlateVisibility::Visible);
    SetVisibility(ESlateVisibility::SelfHitTestInvisible);RefreshAll();return true;
}
bool ULDChoicesPanel::ClosePanel()
{
    // Closing only hides the tray or the window: the offer, the blueprints and any submitted request keep their state; an
    // unconfirmed replace, forget, fill gap or dismantle is withdrawn with its pickers.
    RestConfirm=FRestConfirm();GrowthShown(RestConfirmLine,false);CloseMaterialPickers();
    PinnedPreview=INDEX_NONE;bOpen=false;bServicePanelOpen=false;
    GrowthShown(TrayFrame,false);GrowthShown(ServicePanel,false);SetVisibility(ESlateVisibility::Collapsed);Fold.Closed();return true;
}
double ULDChoicesPanel::UiNow() const{const UWorld* World=GetWorld();return World?World->GetRealTimeSeconds():0.;}
void ULDChoicesPanel::MarkOpenedAutomatically()
{
    if(!bOpen){return;}
    Fold.Opened(true,UiNow());
    UE_LOG(LogLDUI,Log,TEXT("LD_HUD_GROWTH_AUTO_OPEN The 构筑 tray opened for the opening offer or the opening base pick."));
}
void ULDChoicesPanel::UpdateFold(bool bOwnHit)
{
    // #222: only an automatic opening folds by itself; a tray the player opened stays, in combat and in a PK too.
    if(bOpen&&Fold.ShouldFold(UiNow(),IsEngaged(),bOwnHit))
    {
        UE_LOG(LogLDUI,Log,TEXT("LD_HUD_CHOICES_FOLDED %s"),bOwnHit?TEXT("own hit"):TEXT("automatic open timed out"));
        ClosePanel();
    }
}
bool ULDChoicesPanel::Usable() const
{
    return Latest.bValid&&LatestMatch!=0&&Latest.Generation==LatestMatch&&Latest.Life==LatestLife;
}
uint8 ULDChoicesPanel::FirstEmptySlot() const
{
    for(uint8 Key=0;Key<3;++Key){if(LDHUD::ActiveInSlot(Latest,Key).IsNone()){return Key;}}
    return 3;
}
void ULDChoicesPanel::Present(const FLDProgressionView& View,uint32 Match,uint32 Life,ELDLifePhase LifePhase,LDProgression::EMatchStage Stage,
    const FLDDemoContentDefinitions* Content,const LDHUD::FCardStats& Stats,const LDHUD::FSlotKeys& Keys)
{
    const bool bStatsChanged=Stats.Level!=LatestStats.Level||Stats.ADP2!=LatestStats.ADP2||Stats.APP2!=LatestStats.APP2
        ||Stats.MaxHPP2!=LatestStats.MaxHPP2||Stats.ArmorP2!=LatestStats.ArmorP2||Content!=LatestContent||!(Keys==SlotKeys);
    SlotKeys=Keys;
    Latest=View;LatestMatch=Match;LatestLife=Life;LatestLifePhase=LifePhase;LatestStage=Stage;
    LatestContent=Content;LatestStats=Stats;
    if(!Tray){return;}
    Gate=Usable()?LDHUD::BuildGate(Stage,LifePhase):LDHUD::FBuildGateView();
    if(bStatsChanged){bCardsStale=true;}
    // The HUD presents every frame; a collapsed panel only keeps the inputs and refreshes when it opens.
    if(!bOpen&&!bServicePanelOpen){return;}
    RefreshAll();
}
void ULDChoicesPanel::RefreshAll()
{
    if(!Tray){return;}
    ShownBlueprints=Usable()?Latest.PendingBlueprints:TArray<FName>();
    // Too many component cards turn every component row into a grid of icon tiles.
    const bool bTiles=Usable()&&LDHUD::ChoiceTrayTiles(Latest);
    RefreshOffer();RefreshOpeningBase(bTiles);RefreshComponentChoices(bTiles);RefreshBlueprintCards();RefreshBlueprints();RefreshRest();
    GrowthShown(ChoiceRowList,!ShownChoices.IsEmpty());
    bCardsStale=false;
}
void ULDChoicesPanel::RefreshOffer()
{
    const bool bOffer=Usable()&&Latest.bHasActiveOffer&&!Latest.ActiveOffer.Options.IsEmpty();
    // A new offer drops the pinned popup; ids and revisions travel with the click, never a card index.
    if(!bOffer||Latest.ActiveOffer.OfferId!=ShownOffer){PinnedPreview=INDEX_NONE;}
    ShownOffer=bOffer?Latest.ActiveOffer.OfferId:FGuid();ShownOfferRevision=bOffer?Latest.ActiveOffer.OfferRevision:0;
    GrowthShown(OfferRow,bOffer);
    // What a click does now: learn into the lowest empty slot, or keep it as a blueprint and say why (the server decides).
    const uint8 FirstEmpty=FirstEmptySlot();
    const FString Outcome=!Gate.bCanChoose?Gate.ChooseBlocked
        :Gate.bCanLearn&&FirstEmpty<3?LDTerms::LearnInto(SlotKeys.Of(FirstEmpty))
        :FString::Printf(TEXT("存为%s：%s"),LDTerms::Blueprint,FirstEmpty==3?LDTerms::SlotsFull:*Gate.LearnBlocked);
    for(int32 Index=0;Index<OfferCards.Num();++Index)
    {
        FTrayCard& Card=OfferCards[Index];
        if(!bOffer||!Latest.ActiveOffer.Options.IsValidIndex(Index)){GrowthShown(Card.Frame,false);continue;}
        const FName Id=Latest.ActiveOffer.Options[Index];
        const FString Key=Id.ToString()+TEXT("|")+Outcome;
        if(CardStale(Card,Key))
        {
            const LDHUD::FActiveCardView View=LDHUD::ActiveCard(Id,LatestContent,LatestStats);
            ShowFace(Card,Key,View.Name,View.IconId,View.Glyph,LDHUD::ActiveChoiceDetail(View,false,Outcome));
        }
        ShowLook(Card,false,Gate.bCanChoose&&!PendingRequest);
    }
}
ULDChoicesPanel::FTrayCard& ULDChoicesPanel::BaseCardAt(int32 Index)
{
    while(BaseCards.Num()<=Index)
    {
        const int32 CardIndex=BaseCards.Num();
        FTrayCard Card=MakeCard(FName(*FString::Printf(TEXT("GrowthBase%d"),CardIndex)),[this,CardIndex](){PickOpeningBase(CardIndex);});
        BaseRow->AddChildToWrapBox(Card.Frame);BaseCards.Add(Card);
    }
    return BaseCards[Index];
}
void ULDChoicesPanel::RefreshOpeningBase(bool bTiles)
{
    const bool bShow=Usable()&&Latest.bOpeningBasePending&&!Latest.OpeningBaseOptions.IsEmpty();
    ShownBaseOptions=bShow?Latest.OpeningBaseOptions:TArray<FName>();
    GrowthShown(BaseRow,bShow);TrayRowTiles(BaseRow,bTiles);
    for(int32 Index=0;Index<FMath::Max(ShownBaseOptions.Num(),BaseCards.Num());++Index)
    {
        if(!ShownBaseOptions.IsValidIndex(Index)){GrowthShown(BaseCards[Index].Frame,false);continue;}
        FTrayCard& Card=BaseCardAt(Index);
        const FName Id=ShownBaseOptions[Index];
        const FString Key=Id.ToString()+TEXT("|")+Gate.ChooseBlocked;
        if(CardStale(Card,Key))
        {
            // A plain base is taken and installed on 本体 at once; B18 goes to the storage, to be dragged onto the skill it
            // binds (#227). The server decides.
            const LDHUD::FComponentCardView View=LDHUD::ComponentCard(Id,LatestContent,LatestStats);
            const FString Outcome=!Gate.bCanChoose?Gate.ChooseBlocked:View.bBinding?FString(LDTerms::TakeToStorage):FString(LDTerms::TakeAndEquip);
            ShowFace(Card,Key,View.Name,View.IconId,View.Glyph,LDHUD::ComponentChoiceDetail(View,Outcome));
        }
        ShowLook(Card,bTiles,Gate.bCanChoose&&!PendingRequest);
    }
}
ULDChoicesPanel::FChoiceRow& ULDChoicesPanel::ChoiceRowAt(int32 Index)
{
    while(ChoiceRows.Num()<=Index)
    {
        const int32 RowIndex=ChoiceRows.Num();
        FChoiceRow& Row=ChoiceRows.AddDefaulted_GetRef();
        Row.Row=TrayRow(WidgetTree);RowWidgets.Add(Row.Row);
        for(int32 Option=0;Option<2;++Option)
        {
            FTrayCard Card=MakeCard(FName(*FString::Printf(TEXT("GrowthComponentChoice%d_%d"),RowIndex,Option)),[this,RowIndex,Option](){PickComponentChoice(RowIndex,Option);});
            Row.Row->AddChildToWrapBox(Card.Frame);Row.Options.Add(Card);
        }
        Row.Points=MakeCard(FName(*FString::Printf(TEXT("GrowthComponentChoicePoints%d"),RowIndex)),[this,RowIndex](){TakeComponentChoicePoints(RowIndex);});
        Row.Row->AddChildToWrapBox(Row.Points.Frame);
        auto* RowSlot=ChoiceRowList->AddChildToVerticalBox(Row.Row);RowSlot->SetHorizontalAlignment(HAlign_Right);
        RowSlot->SetPadding(FMargin(0,RowIndex?TrayCardGap:0.f,0,0));
    }
    return ChoiceRows[Index];
}
void ULDChoicesPanel::RefreshComponentChoices(bool bTiles)
{
    ShownChoices.Reset();
    if(Usable()){for(const FLDComponentChoiceView& Choice:Latest.ComponentChoices){ShownChoices.Add(Choice.ChoiceId);}}
    const int64 PoolPoints=LatestContent?LatestContent->Parameters.EmptyComponentPoolPoints:0;
    for(int32 Index=0;Index<FMath::Max(ShownChoices.Num(),ChoiceRows.Num());++Index)
    {
        if(!ShownChoices.IsValidIndex(Index)){GrowthShown(ChoiceRows[Index].Row,false);continue;}
        FChoiceRow& Row=ChoiceRowAt(Index);
        GrowthShown(Row.Row,true);TrayRowTiles(Row.Row,bTiles);
        const FLDComponentChoiceView& Choice=Latest.ComponentChoices[Index];
        // Only one known material left is shown as it is (spec §4.2), never padded with a made-up second card.
        for(int32 Option=0;Option<Row.Options.Num();++Option)
        {
            FTrayCard& Card=Row.Options[Option];
            if(!Choice.Options.IsValidIndex(Option)){GrowthShown(Card.Frame,false);continue;}
            const FName Id=Choice.Options[Option];
            const bool bOwned=LDHUD::OwnsComponent(Latest,Id);
            const FString Outcome=!Gate.bCanComponentChoice?Gate.ComponentChoiceBlocked:bOwned?FString(LDTerms::ComponentChoiceOwned):FString();
            const FString Key=Id.ToString()+TEXT("|")+Outcome;
            if(CardStale(Card,Key))
            {
                const LDHUD::FComponentCardView View=LDHUD::ComponentCard(Id,LatestContent,LatestStats);
                ShowFace(Card,Key,View.Name,View.IconId,View.Glyph,LDHUD::ComponentChoiceDetail(View,Outcome));
            }
            ShowLook(Card,bTiles,Gate.bCanComponentChoice&&!bOwned&&!PendingRequest);
        }
        // Every option already owned: only the empty-pool points remain, as one more card.
        const bool bAllOwned=LDHUD::ComponentChoiceAllOwned(Choice,Latest);
        if(!bAllOwned){GrowthShown(Row.Points.Frame,false);continue;}
        const FString Points=PoolPoints>0?FString::Printf(TEXT("%lld 点"),PoolPoints):FString(TEXT("? 点"));
        const FString Outcome=!Gate.bCanComponentChoice?Gate.ComponentChoiceBlocked:PoolPoints>0?FString():FString(TEXT("改领点数的数据缺失"));
        const FString Key=Points+TEXT("|")+Outcome;
        if(CardStale(Row.Points,Key))
        {
            LDHUD::FActionDetailView Detail;
            Detail.Name=Points;Detail.Summary=LDTerms::Component;
            Detail.Body=PoolPoints>0?LDTerms::ComponentChoicePoints(PoolPoints):FString();
            if(!Outcome.IsEmpty()){Detail.Body+=(Detail.Body.IsEmpty()?TEXT(""):TEXT("\n\n"))+Outcome;}
            ShowFace(Row.Points,Key,Points,FString(),TEXT("+"),Detail);
        }
        ShowLook(Row.Points,bTiles,PoolPoints>0&&Gate.bCanComponentChoice&&!PendingRequest);
    }
}
ULDChoicesPanel::FTrayCard& ULDChoicesPanel::BlueprintCardAt(int32 Index)
{
    while(BlueprintCards.Num()<=Index)
    {
        const int32 CardIndex=BlueprintCards.Num();
        FTrayCard Card=MakeCard(FName(*FString::Printf(TEXT("GrowthBlueprint%d"),CardIndex)),[this,CardIndex](){LearnBlueprintCard(CardIndex);});
        BlueprintCardRow->AddChildToWrapBox(Card.Frame);BlueprintCards.Add(Card);
    }
    return BlueprintCards[Index];
}
void ULDChoicesPanel::RefreshBlueprintCards()
{
    // Blueprints are tray cards only while a Q/W/E slot is empty; a full Q/W/E waits for the trainer's replace.
    const uint8 FirstEmpty=FirstEmptySlot();
    const int32 Shown=FirstEmpty<3?ShownBlueprints.Num():0;
    GrowthShown(BlueprintCardRow,Shown>0);
    const FString Outcome=!Gate.bCanLearn?Gate.LearnBlocked:FirstEmpty<3?LDTerms::LearnInto(SlotKeys.Of(FirstEmpty)):FString();
    for(int32 Index=0;Index<FMath::Max(Shown,BlueprintCards.Num());++Index)
    {
        if(Index>=Shown){GrowthShown(BlueprintCards[Index].Frame,false);continue;}
        FTrayCard& Card=BlueprintCardAt(Index);
        const FName Id=ShownBlueprints[Index];
        const FString Key=Id.ToString()+TEXT("|")+Outcome;
        if(CardStale(Card,Key))
        {
            const LDHUD::FActiveCardView View=LDHUD::ActiveCard(Id,LatestContent,LatestStats);
            ShowFace(Card,Key,View.Name,View.IconId,View.Glyph,LDHUD::ActiveChoiceDetail(View,true,Outcome));
        }
        ShowLook(Card,false,Gate.bCanLearn&&!PendingRequest);
    }
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
        Row.Name=MakeText(15,LDHUDStyle::TextPrimary);Row.Name->SetWrapTextAt(ServicePanelWidth-70.f);Lines->AddChildToVerticalBox(Row.Name);
        Row.Summary=MakeText(15,LDHUDStyle::TextSecondary);Row.Summary->SetWrapTextAt(ServicePanelWidth-70.f);Lines->AddChildToVerticalBox(Row.Summary);
        auto* LinesSlot=Line->AddChildToHorizontalBox(Lines);LinesSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));LinesSlot->SetVerticalAlignment(VAlign_Center);
        auto* Actions=WidgetTree->ConstructWidget<UWrapBox>();RowWidgets.Add(Actions);
        Actions->SetExplicitWrapSize(true);Actions->SetWrapSize(ServicePanelWidth-24.f);
        Actions->SetInnerSlotPadding(FVector2D(6,4));
        Group->AddChildToVerticalBox(Actions)->SetPadding(FMargin(0,4,0,0));
        for(uint8 Key=0;Key<3;++Key)
        {
            ULDCommandButton* Learn=LDWidgets::Button(WidgetTree,TEXT(""),[this,RowIndex,Key](){LearnBlueprint(RowIndex,Key);},false,
                FName(*FString::Printf(TEXT("GrowthLearn%d%s"),RowIndex,*LDHUD::SlotKey(Key))));
            ScaleNewText(Cast<UTextBlock>(Learn->GetContent()));RowWidgets.Add(Learn);
            Actions->AddChildToWrapBox(Learn);
            Row.Learn.Add(Learn);
        }
        for(uint8 Key=0;Key<3;++Key)
        {
            // #198 rest only: the blueprint replaces the active in this occupied slot, after the confirm line.
            ULDCommandButton* Replace=LDWidgets::Button(WidgetTree,TEXT(""),[this,RowIndex,Key](){AskReplace(RowIndex,Key);},false,
                FName(*FString::Printf(TEXT("GrowthReplace%d%s"),RowIndex,*LDHUD::SlotKey(Key))));
            ScaleNewText(Cast<UTextBlock>(Replace->GetContent()));RowWidgets.Add(Replace);
            Actions->AddChildToWrapBox(Replace);
            Row.Replace.Add(Replace);
        }
        Row.Blocked=MakeText(13,LDHUDStyle::TextSecondary);Row.Blocked->SetWrapTextAt(ServicePanelWidth-24.f);
        Group->AddChildToVerticalBox(Row.Blocked)->SetPadding(FMargin(0,4,0,0));
        Row.Row=Group;
        BlueprintList->AddChildToVerticalBox(Group)->SetPadding(FMargin(0,0,0,8));
    }
    return BlueprintRows[Index];
}
void ULDChoicesPanel::RefreshBlueprints()
{
    // The trainer window's rows; the tray shows blueprints as cards (RefreshBlueprintCards).
    const bool bShown=bServicePanelOpen&&ServiceContext==ELDRestService::Trainer&&!ShownBlueprints.IsEmpty();
    GrowthShown(BlueprintSection,bShown);
    if(!bShown){return;}
    bool bEmpty[3]={false,false,false};
    for(uint8 Key=0;Key<3;++Key){bEmpty[Key]=LDHUD::ActiveInSlot(Latest,Key).IsNone();}
    const bool bAnyEmpty=bEmpty[0]||bEmpty[1]||bEmpty[2];
    // A full Q/W/E: the blueprint replaces an occupied slot (after a confirm).
    const FString ReplaceWhy=Gate.bInRest?ReplaceRefusal():FString(LDTerms::ReplaceAtRest);
    const FString Blocked=!bAnyEmpty?ReplaceWhy:Gate.bCanLearn?FString():Gate.LearnBlocked;
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
            GrowthText(Cast<UTextBlock>(Row.Learn[Key]->GetContent()),LDTerms::LearnInto(SlotKeys.Of(Key)));
            GrowthText(Cast<UTextBlock>(Row.Replace[Key]->GetContent()),LDTerms::ReplaceInto(SlotKeys.Of(Key)));
            GrowthShown(Row.Learn[Key],bEmpty[Key],ESlateVisibility::Visible);
            GrowthEnabled(Row.Learn[Key],Gate.bCanLearn&&!PendingRequest);
            GrowthShown(Row.Replace[Key],!bEmpty[Key],ESlateVisibility::Visible);
            GrowthEnabled(Row.Replace[Key],ReplaceWhy.IsEmpty()&&!PendingRequest);
        }
        GrowthText(Row.Blocked,Blocked);
        GrowthShown(Row.Blocked,!Blocked.IsEmpty(),ESlateVisibility::HitTestInvisible);
    }
}
bool ULDChoicesPanel::PickComponentChoice(int32 ChoiceIndex,int32 OptionIndex)
{
    if(!Usable()||!ShownChoices.IsValidIndex(ChoiceIndex)){return false;}
    const FGuid Id=ShownChoices[ChoiceIndex];
    const FLDComponentChoiceView* Choice=Latest.ComponentChoices.FindByPredicate([&Id](const FLDComponentChoiceView& C){return C.ChoiceId==Id;});
    if(!Choice||!Choice->Options.IsValidIndex(OptionIndex)){return false;}
    if(PendingRequest){Tell(TEXT("上一个请求仍在处理中。"));return false;}
    if(!Gate.bCanComponentChoice){Tell(TEXT("没有提交：")+Gate.ComponentChoiceBlocked);return false;}
    const FName Option=Choice->Options[OptionIndex];
    if(LDHUD::OwnsComponent(Latest,Option)){Tell(TEXT("没有提交：")+LDHUD::GrowthReasonText(ELDGrowthReason::ComponentOwned));return false;}
    FLDBuildCommand Command;
    Command.Kind=ELDBuildCommandKind::ComponentChoice;
    Command.OfferId=Id;
    if(!FLDDefinitionWire::Encode(Option,Command.Component)){Tell(TEXT("没有提交：这个构件的标识无效。"));return false;}
    return Send(Command);
}
bool ULDChoicesPanel::TakeComponentChoicePoints(int32 ChoiceIndex)
{
    if(!Usable()||!ShownChoices.IsValidIndex(ChoiceIndex)){return false;}
    const FGuid Id=ShownChoices[ChoiceIndex];
    const FLDComponentChoiceView* Choice=Latest.ComponentChoices.FindByPredicate([&Id](const FLDComponentChoiceView& C){return C.ChoiceId==Id;});
    if(!Choice){return false;}
    if(PendingRequest){Tell(TEXT("上一个请求仍在处理中。"));return false;}
    if(!Gate.bCanComponentChoice){Tell(TEXT("没有提交：")+Gate.ComponentChoiceBlocked);return false;}
    if(!LDHUD::ComponentChoiceAllOwned(*Choice,Latest)){Tell(TEXT("没有提交：")+LDHUD::GrowthReasonText(ELDGrowthReason::ComponentOwned));return false;}
    if(!LatestContent||LatestContent->Parameters.EmptyComponentPoolPoints<=0){Tell(TEXT("没有提交：改领点数的数据缺失。"));return false;}
    // An all-zero Component takes the empty-pool points.
    FLDBuildCommand Command;
    Command.Kind=ELDBuildCommandKind::ComponentChoice;
    Command.OfferId=Id;
    return Send(Command);
}
bool ULDChoicesPanel::PickOpeningBase(int32 Index)
{
    if(!Usable()||!Latest.bOpeningBasePending||!ShownBaseOptions.IsValidIndex(Index)||!Latest.OpeningBaseOptions.Contains(ShownBaseOptions[Index])){return false;}
    if(PendingRequest){Tell(TEXT("上一个请求仍在处理中。"));return false;}
    if(!Gate.bCanChoose){Tell(TEXT("没有提交：")+Gate.ChooseBlocked);return false;}
    const FName Id=ShownBaseOptions[Index];
    const FString Text=Id.ToString();
    const FLDDemoComponentDefinition* Definition=LatestContent?LatestContent->Components.FindByPredicate([&Text](const FLDDemoComponentDefinition& D){return D.Id==Text;}):nullptr;
    if(!Definition){Tell(TEXT("没有提交：内容目录中找不到该构件。"));return false;}
    FLDBuildCommand Command;
    Command.Kind=ELDBuildCommandKind::OpeningBase;
    if(!FLDDefinitionWire::Encode(Id,Command.Component)){Tell(TEXT("没有提交：这个构件的标识无效。"));return false;}
    // A plain base installs on 本体 in the same commit; B18 needs its skill and element, which the storage asks on its drop.
    Command.bInstallIfPossible=Definition->Binding!=ELDDemoComponentBinding::ActiveElement;
    return Send(Command);
}
bool ULDChoicesPanel::ChooseOffer(int32 Index)
{
    if(!Usable()||!Latest.bHasActiveOffer||!Latest.ActiveOffer.Options.IsValidIndex(Index)||Latest.ActiveOffer.OfferId!=ShownOffer){return false;}
    // One click is one choice: a click while a request is in flight never queues a second one.
    if(PendingRequest){Tell(TEXT("上一个请求仍在处理中。"));return false;}
    if(!Gate.bCanChoose){Tell(TEXT("没有提交：")+Gate.ChooseBlocked);return false;}
    FLDBuildCommand Command;
    Command.Kind=ELDBuildCommandKind::ActiveChoice;
    Command.OfferId=Latest.ActiveOffer.OfferId;Command.OfferRevision=Latest.ActiveOffer.OfferRevision;
    if(!FLDDefinitionWire::Encode(Latest.ActiveOffer.Options[Index],Command.Option)){Tell(TEXT("没有提交：这个技能的标识无效。"));return false;}
    // Always ask to learn now: the server learns it into the lowest empty slot when it may, otherwise keeps it as a blueprint.
    Command.bLearnIfPossible=true;Command.Slot=FLDBuildCommand::AnySlot;
    PinnedPreview=INDEX_NONE;
    return Send(Command);
}
bool ULDChoicesPanel::PreviewOffer(int32 Index)
{
    if(!Latest.bHasActiveOffer||!Latest.ActiveOffer.Options.IsValidIndex(Index)||Latest.ActiveOffer.OfferId!=ShownOffer){return false;}
    PinnedPreview=Index;return true;
}
bool ULDChoicesPanel::LearnBlueprint(int32 Index,uint8 Key)
{
    if(!Usable()||!ShownBlueprints.IsValidIndex(Index)||Key>2||!Latest.PendingBlueprints.Contains(ShownBlueprints[Index])){return false;}
    if(PendingRequest){Tell(TEXT("上一个请求仍在处理中。"));return false;}
    if(!LDHUD::ActiveInSlot(Latest,Key).IsNone()){Tell(TEXT("没有提交：")+FString(LDTerms::ReplaceAtRest));return false;}
    if(!Gate.bCanLearn){Tell(TEXT("没有提交：")+Gate.LearnBlocked);return false;}
    FLDBuildCommand Command;
    Command.Kind=ELDBuildCommandKind::LearnBlueprint;
    if(!FLDDefinitionWire::Encode(ShownBlueprints[Index],Command.Blueprint)){Tell(TEXT("没有提交：这份图纸的标识无效。"));return false;}
    Command.Slot=Key;
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
        if(bShown){Tell(bMaterial?TEXT("构件已变化，请重新选择。"):TEXT("技能槽或图纸已变化，请重新选择。"));}
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
        GrowthText(Cast<UTextBlock>(ForgetButtons[Key]->GetContent()),LDTerms::ForgetSlot(SlotKeys.Of(Key),LDHUD::ContentName(LatestContent,Active.ToString())));
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
            Line=LDTerms::ReplaceConfirm(Name(RestConfirm.Blueprint),SlotKeys.Of(RestConfirm.Slot),Name(RestConfirm.Active));
            break;
        case ELDBuildCommandKind::ForgetActive:
            Line=LDTerms::ForgetConfirm(SlotKeys.Of(RestConfirm.Slot),Name(RestConfirm.Active));
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
        auto* Caption=CastChecked<UTextBlock>(Pick->GetContent());Caption->SetAutoWrapText(false);Caption->SetWrapTextAt(ServicePanelWidth-44.f);
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
    if(PendingRequest){Tell(TEXT("上一个请求仍在处理中。"));return false;}
    if(!Materials.FillGapBlocked.IsEmpty()){Tell(TEXT("没有提交：")+Materials.FillGapBlocked);return false;}
    CloseMaterialPickers();
    bFillGapOpen=true;
    // A single gap is picked straight away.
    if(Materials.Gaps.Num()==1){GapTarget=Materials.Gaps[0].Target;}
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
    if(!Materials.FillGapBlocked.IsEmpty()){Tell(TEXT("没有提交：")+Materials.FillGapBlocked);return false;}
    const FName Filler=Gap->Fillers[Index];
    RestConfirm=FRestConfirm();
    RestConfirm.Kind=ELDBuildCommandKind::FillGap;RestConfirm.GapTarget=GapTarget;RestConfirm.GapFiller=Filler;
    RefreshRest();
    return true;
}
bool ULDChoicesPanel::ToggleDismantle()
{
    if(!Usable()){return false;}
    if(bDismantleOpen){CloseMaterialPickers();RefreshRest();return true;}
    if(PendingRequest){Tell(TEXT("上一个请求仍在处理中。"));return false;}
    if(!Materials.DismantleBlocked.IsEmpty()){Tell(TEXT("没有提交：")+Materials.DismantleBlocked);return false;}
    CloseMaterialPickers();
    bDismantleOpen=true;
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
        if(int64(DismantlePicked.Num())>=Materials.DismantleCount){Tell(LDTerms::DismantlePicked(DismantlePicked.Num(),Materials.DismantleCount));return false;}
        DismantlePicked.Add(Id);
        // The approved count picked: the confirm line names what is deleted and what goes back to the inventory.
        if(int64(DismantlePicked.Num())==Materials.DismantleCount)
        {
            RestConfirm=FRestConfirm();
            RestConfirm.Kind=ELDBuildCommandKind::Dismantle;RestConfirm.Surrendered=DismantlePicked;
        }
    }
    RefreshRest();
    return true;
}
bool ULDChoicesPanel::BuyNpcOffer()
{
    if(!Usable()){return false;}
    if(PendingRequest){Tell(TEXT("上一个请求仍在处理中。"));return false;}
    const FString Refusal=NpcRefusal();
    if(!Refusal.IsEmpty()){Tell(TEXT("没有提交：")+Refusal);return false;}
    FLDBuildCommand Command;
    Command.Kind=ELDBuildCommandKind::BuyNpcOffer;
    return Send(Command);
}
bool ULDChoicesPanel::Respec()
{
    if(!Usable()){return false;}
    if(PendingRequest){Tell(TEXT("上一个请求仍在处理中。"));return false;}
    const FString Refusal=RespecRefusal();
    if(!Refusal.IsEmpty()){Tell(TEXT("没有提交：")+Refusal);return false;}
    FLDBuildCommand Command;
    Command.Kind=ELDBuildCommandKind::Respec;
    return Send(Command);
}
bool ULDChoicesPanel::AskReplace(int32 Index,uint8 Key)
{
    if(!Usable()||!ShownBlueprints.IsValidIndex(Index)||Key>2||!Latest.PendingBlueprints.Contains(ShownBlueprints[Index])){return false;}
    const FName Active=LDHUD::ActiveInSlot(Latest,Key);
    if(Active.IsNone()){Tell(TEXT("没有提交：")+LDHUD::GrowthReasonText(ELDGrowthReason::SlotEmpty));return false;}
    const FString Refusal=ReplaceRefusal();
    if(!Refusal.IsEmpty()){Tell(TEXT("没有提交：")+Refusal);return false;}
    RestConfirm.Kind=ELDBuildCommandKind::ReplaceActive;RestConfirm.Blueprint=ShownBlueprints[Index];RestConfirm.Slot=Key;RestConfirm.Active=Active;
    RefreshRest();
    return true;
}
bool ULDChoicesPanel::AskForget(uint8 Key)
{
    if(!Usable()||Key>2){return false;}
    const FName Active=LDHUD::ActiveInSlot(Latest,Key);
    if(Active.IsNone()){Tell(TEXT("没有提交：")+LDHUD::GrowthReasonText(ELDGrowthReason::SlotEmpty));return false;}
    const FString Refusal=ForgetRefusal();
    if(!Refusal.IsEmpty()){Tell(TEXT("没有提交：")+Refusal);return false;}
    RestConfirm=FRestConfirm();
    RestConfirm.Kind=ELDBuildCommandKind::ForgetActive;RestConfirm.Slot=Key;RestConfirm.Active=Active;
    RefreshRest();
    return true;
}
bool ULDChoicesPanel::ConfirmRestAction()
{
    if(RestConfirm.Kind==ELDBuildCommandKind::None){return false;}
    if(PendingRequest){Tell(TEXT("上一个请求仍在处理中。"));return false;}
    const bool bMaterial=RestConfirm.Kind==ELDBuildCommandKind::FillGap||RestConfirm.Kind==ELDBuildCommandKind::Dismantle;
    if(!RestConfirmHolds()){CancelRestAction();Tell(bMaterial?TEXT("构件已变化，请重新选择。"):TEXT("技能槽或图纸已变化，请重新选择。"));return false;}
    const FString Refusal=RestConfirmRefusal();
    if(!Refusal.IsEmpty()){Tell(TEXT("没有提交：")+Refusal);return false;}
    FLDBuildCommand Command;
    Command.Kind=RestConfirm.Kind;
    if(RestConfirm.Kind==ELDBuildCommandKind::FillGap)
    {
        // Component: the gap target; Filler: the unowned common base it receives.
        if(!FLDDefinitionWire::Encode(RestConfirm.GapTarget,Command.Component)||!FLDDefinitionWire::Encode(RestConfirm.GapFiller,Command.Filler))
        {Tell(TEXT("没有提交：构件标识无效。"));return false;}
    }
    else if(RestConfirm.Kind==ELDBuildCommandKind::Dismantle)
    {
        for(const FName& Id:RestConfirm.Surrendered)
        {
            if(!FLDDefinitionWire::Encode(Id,Command.Surrendered.AddDefaulted_GetRef())){Tell(TEXT("没有提交：构件标识无效。"));return false;}
        }
    }
    else
    {
        Command.Slot=RestConfirm.Slot;
        if(RestConfirm.Kind==ELDBuildCommandKind::ReplaceActive&&!FLDDefinitionWire::Encode(RestConfirm.Blueprint,Command.Blueprint))
        {Tell(TEXT("没有提交：这份图纸的标识无效。"));return false;}
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
bool ULDChoicesPanel::LearnBlueprintCard(int32 Index)
{
    const uint8 Key=FirstEmptySlot();
    if(Key>2){Tell(TEXT("没有提交：")+FString(LDTerms::SlotsFull));return false;}
    return LearnBlueprint(Index,Key);
}
bool ULDChoicesPanel::Send(FLDBuildCommand Command)
{
    if(!SubmitBuild)
    {
        UE_LOG(LogLDUI,Error,TEXT("LD_GROWTH_SUBMIT_UNAVAILABLE The growth build entry is not connected to this panel."));
        Tell(TEXT("成长入口不可用，本次没有提交。"));return false;
    }
    Command.ExpectedBuildRevision=Latest.BuildRevision;Command.CatalogDigest=Latest.CatalogDigest;
    // A Listen Host answers inside the submit call, so the pending entry is keyed to the id it will receive first.
    const uint64 Expected=PeekRequestId?PeekRequestId():0;
    const uint32 Before=Answers;PendingRequest=Expected;PendingKind=Command.Kind;ResultLine.Reset();
    ELDGrowthReason Reason=ELDGrowthReason::None;
    const uint64 Request=SubmitBuild(Command,Reason);
    if(!Request)
    {
        if(PendingRequest==Expected){PendingRequest=0;}
        const FString Why=LDHUD::GrowthReasonText(Reason);
        Tell(TEXT("没有提交：")+(Why.IsEmpty()?FString(TEXT("原因未知")):Why));
        RefreshAll();
        return false;
    }
    if(Answers==Before){PendingRequest=Request;}
    RefreshAll();
    return true;
}
void ULDChoicesPanel::FinishRequest()
{
    PendingRequest=0;
    RefreshAll();
    // Every result reaches the player as a toast, whether the tray is open or not.
    Tell(ResultLine);ResultLine.Reset();
}
void ULDChoicesPanel::ShowFinal(const FLDBuildReceipt& Receipt)
{
    ResultLine=LDHUD::BuildReceiptText(Receipt,LatestContent,SlotKeys);
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
        ResultLine=TEXT("没有受理：")+LDHUD::GrowthReasonText(LDGrowthWire::Reason(Result.Reason));
        if(Result.bSequenceReserved){Tell(ResultLine);ResultLine.Reset();return;}
        break;
    case ELDAdmissionStatus::Final:
        if(Result.bHasBuildReceipt){ShowFinal(Result.BuildReceipt);}else{ResultLine=TEXT("请求已结束，请以当前状态为准。");}
        break;
    default: Tell(TEXT("无法识别服务器的答复，请以当前状态为准。"));return;
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
        if(Result.bHasReceipt){ShowFinal(Result.Receipt);}else{ResultLine=TEXT("回执缺少结果，请以当前状态为准。");}
        break;
    case ELDReceiptLookup::ReceiptExpired: ResultLine=TEXT("回执已过期，请以当前状态为准。");break;
    case ELDReceiptLookup::NotFound: Tell(TEXT("服务器没有找到这次请求，请以当前状态为准。"));return;
    default: Tell(TEXT("回执查询无效，请以当前状态为准。"));return;
    }
    FinishRequest();
}
