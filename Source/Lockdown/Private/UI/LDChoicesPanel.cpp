#include "UI/LDChoicesPanel.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "UI/LDUIPaint.h"
#include "Components/ScaleBoxSlot.h"
#include "Core/LDLog.h"
#include "UI/LDHUDModel.h"
#include "UI/LDHUDStyle.h"
#include "UI/LDTerms.h"
#include "UI/LDUIFonts.h"
#include "UI/LDUIIcons.h"
#include "UI/LDUIShapes.h"
#include "UI/LDWorkshopModel.h"
#include "UI/LDTween.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Framework/Application/SlateApplication.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDBuildBlueprintComponent.h"
#include "UI/LDUIRoot.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Spacer.h"
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
#include "Fonts/FontMeasure.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"

namespace
{
// A tray card is the icon over one line of name; a grid tile is the icon alone. A tray line takes four cards or eight tiles.
constexpr float TrayCardWidth=114.f,TrayCardIcon=64.f,TrayTileSize=54.f,TrayTileIcon=46.f,TrayCardGap=8.f,TrayTileGap=6.f;
constexpr float TrayIconRadius=7.f,TrayCardPadding=4.f;
// Mock 3's centre stage: three wide skill cards with a framed icon and the narrow 不选 card, 40 apart; the final stage keeps
// room for four cards in the same row. The stage scale box shrinks the row on narrow screens instead of wrapping or scrolling.
constexpr float StageOfferWidth=116.f,StageOfferIcon=68.f,StageSkipWidth=78.f,StageOfferGap=6.f;
constexpr float StageFinalWidth=116.f,StageFinalIcon=68.f,StageFinalGap=6.f;
constexpr float TrayWidth=4*TrayCardWidth+3*TrayCardGap;
constexpr float TrayPagerHeight=32.f,TrayMinIconPixels=36.f,TrayLabelWidth=76.f;
static_assert(8*TrayTileSize+7*TrayTileGap<=TrayWidth,"eight grid tiles fit on one tray line");
static_assert(TrayTileIcon+2*TrayCardPadding<=TrayTileSize,"a grid tile holds its icon");
constexpr float ServicePanelWidth=540.f;
// The confirm line wraps short of the hold and cancel buttons that share its row.
constexpr float ServiceConfirmButtons=200.f;
bool GrowthHasArea(const FSlateRect& Rect){return Rect.Right>Rect.Left&&Rect.Bottom>Rect.Top;}
float TrayIconSize(bool bTile,float Scale,bool bCompact)
{
    const float Preferred=bTile?TrayTileIcon:bCompact?TrayMinIconPixels:TrayCardIcon;
    return FMath::Max(Preferred,TrayMinIconPixels/FMath::Max(.01f,Scale));
}
float TrayCellWidth(bool bTile,float Scale,bool bCompact)
{
    return FMath::Max(bTile?TrayTileSize:bCompact?90.f:TrayCardWidth,TrayIconSize(bTile,Scale,bCompact)+2.f*TrayCardPadding);
}
float TrayCellHeight(bool bTile,float TextScale,float Scale,bool bCompact)
{
    const float Icon=TrayIconSize(bTile,Scale,bCompact);
    return bTile?Icon+2.f*TrayCardPadding:Icon+2.f*TrayCardPadding+4.f+FMath::CeilToFloat((LDHUDStyle::TypeBody*2.7f+28.f)*TextScale);
}
// #328 §2: maps a reward card's button to its 3-way surface look (CardActive / CardBuild / CardStone) and optional accent tint.
void GrowthCardLook(ULDCommandButton* Button,LDUIPaint::EButton Look,const FLinearColor& Color=LDHUDStyle::CardIdle)
{
    if(!Button){return;}
    if(Button->Look!=Look)
    {
        Button->Look=Look;
        auto Style=Button->GetStyle();Style.Normal=FSlateNoResource();Style.Hovered=FSlateNoResource();Style.Pressed=FSlateNoResource();Style.Disabled=FSlateNoResource();
        Button->SetStyle(Style);
        if(TSharedPtr<SWidget> Cached=Button->GetCachedWidget()){Cached->Invalidate(EInvalidateWidgetReason::Paint);}
    }
    const float Peak=FMath::Max3(Color.R,Color.G,Color.B);
    const FLinearColor Mark=Color.Equals(LDHUDStyle::CardIdle)||Peak<=0.f?FLinearColor::Transparent:FLinearColor(Color.R/Peak,Color.G/Peak,Color.B/Peak,1.f);
    if(!Button->Tint.Equals(Mark)){Button->Tint=Mark;if(TSharedPtr<SWidget> Cached=Button->GetCachedWidget()){Cached->Invalidate(EInvalidateWidgetReason::Paint);}}
}
void GrowthTint(ULDCommandButton* Button,const FLinearColor& Color)
{
    if(!Button){return;}
    const LDUIPaint::EButton Keep=(Button->Look==LDUIPaint::EButton::CardActive||Button->Look==LDUIPaint::EButton::CardBuild||Button->Look==LDUIPaint::EButton::CardStone)
        ?Button->Look:LDUIPaint::EButton::Card;
    GrowthCardLook(Button,Keep,Color);
}
FLinearColor ElementStoneRingColor(FName DefinitionId)
{
    const FString Id=LDHUD::CardDefinition(DefinitionId).ToString();
    if(Id.Contains(TEXT("fire"))){return FLinearColor(FColor(0xFB,0x92,0x3C));}
    if(Id.Contains(TEXT("water"))){return FLinearColor(FColor(0x38,0xBD,0xF8));}
    if(Id.Contains(TEXT("electric"))||Id.Contains(TEXT("shock"))){return FLinearColor(FColor(0xC0,0x84,0xFC));}
    if(Id.Contains(TEXT("chill"))||Id.Contains(TEXT("ice"))){return FLinearColor(FColor(0x67,0xE8,0xF9));}
    if(Id.Contains(TEXT("rot"))){return FLinearColor(FColor(0x84,0xCC,0x16));}
    return FLinearColor(FColor(0x34,0xD3,0x99));
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
            TrayPager=WidgetTree->ConstructWidget<UHorizontalBox>();
            TrayPrevious=LDWidgets::Button(WidgetTree,TEXT("‹"),[this](){if(TrayPage>0){--TrayPage;RefreshAll();}},true,TEXT("GrowthPendingPrevious"));
            TrayNext=LDWidgets::Button(WidgetTree,TEXT("›"),[this](){if(TrayPage+1<TrayPages.Num()){++TrayPage;RefreshAll();}},true,TEXT("GrowthPendingNext"));
            TrayPrevious->SetToolTipText(FText::FromString(LDTerms::StoragePreviousPage));
            TrayNext->SetToolTipText(FText::FromString(LDTerms::StorageNextPage));
            const auto PagerButton=[this](ULDCommandButton* Button)
            {
                auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetWidthOverride(32.f);Size->SetHeightOverride(28.f);Size->SetContent(Button);
                TrayPager->AddChildToHorizontalBox(Size)->SetVerticalAlignment(VAlign_Center);
            };
            PagerButton(TrayPrevious);
            TrayPageText=LDWidgets::Text(WidgetTree,TEXT(""),LDHUDStyle::TypeSmall,NAME_None,LDUIFonts::ERole::Number);
            ScaleNewText(TrayPageText);RowWidgets.Add(TrayPageText);
            auto* PageSlot=TrayPager->AddChildToHorizontalBox(TrayPageText);PageSlot->SetPadding(FMargin(10.f,0.f));PageSlot->SetVerticalAlignment(VAlign_Center);
            PagerButton(TrayNext);
            TrayPager->SetVisibility(ESlateVisibility::Collapsed);
            auto* PagerSlot=Tray->AddChildToVerticalBox(TrayPager);PagerSlot->SetHorizontalAlignment(HAlign_Right);PagerSlot->SetPadding(FMargin(0.f,0.f,0.f,4.f));
            const auto AddRow=[this](UWidget* Row){auto* RowSlot=Tray->AddChildToVerticalBox(Row);RowSlot->SetHorizontalAlignment(HAlign_Right);RowSlot->SetPadding(FMargin(0,TrayCardGap,0,0));};
            BlueprintCardRow=TrayRow(WidgetTree);AddRow(BlueprintCardRow);
            {
                // The card offer: the cards, then 重抽 n and 跳过.
                CardRow=TrayRow(WidgetTree);AddRow(CardRow);
                for(int32 Index=0;Index<3;++Index)
                {
                    FTrayCard Card=MakeCard(FName(*FString::Printf(TEXT("GrowthCard%d"),Index)),[this,Index](){PickCardOffer(Index);},ETrayLook::Card);
                    auto* Cell=WidgetTree->ConstructWidget<UVerticalBox>();Cell->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
                    Cell->AddChildToVerticalBox(Card.Frame)->SetHorizontalAlignment(HAlign_Center);
                    CardRow->AddChildToWrapBox(Cell);RowWidgets.Add(Cell);
                    CardCards.Add(Card);CardCells.Add(Cell);
                }
                auto* Actions=WidgetTree->ConstructWidget<UVerticalBox>();Actions->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
                CardRerollButton=LDWidgets::Button(WidgetTree,TEXT(""),[this](){RerollCardOffer();},true,TEXT("GrowthCardReroll"));
                Actions->AddChildToVerticalBox(CardRerollButton)->SetHorizontalAlignment(HAlign_Fill);
                CardSkipButton=LDWidgets::HoldButton(WidgetTree,LDTerms::CardSkip,[this](){SkipCardOffer();},TEXT("GrowthCardSkip"));
                Actions->AddChildToVerticalBox(CardSkipButton)->SetPadding(FMargin(0,6,0,0));
                CardRow->AddChildToWrapBox(Actions);RowWidgets.Add(Actions);CardActions=Actions;
            }
            auto* OfferLine=WidgetTree->ConstructWidget<UHorizontalBox>();
            OfferLine->SetVisibility(ESlateVisibility::Collapsed);OfferGroup=OfferLine;
            OfferNumber=Text(TEXT(""),LDHUDStyle::TypeBody);OfferNumber->SetAutoWrapText(false);OfferNumber->SetColorAndOpacity(LDHUDStyle::TextAccent);
            ScaleNewText(OfferNumber);RowWidgets.Add(OfferNumber);
            auto* OfferLabel=WidgetTree->ConstructWidget<USizeBox>();
            OfferLabel->SetWidthOverride(TrayLabelWidth-8.f);OfferLabel->SetContent(OfferNumber);
            OfferLine->AddChildToHorizontalBox(OfferLabel)->SetVerticalAlignment(VAlign_Center);
            OfferRow=TrayRow(WidgetTree);
            auto* OfferFrame=WidgetTree->ConstructWidget<UBorder>();
            OfferFrame->SetBrush(FSlateNoResource());OfferFrame->SetPadding(FMargin(0));OfferFrame->SetContent(OfferRow);
            OfferLine->AddChildToHorizontalBox(OfferFrame)->SetPadding(FMargin(8,0,0,0));AddRow(OfferLine);
            for(int32 Index=0;Index<3;++Index)
            {
                FTrayCard Card=MakeCard(FName(*FString::Printf(TEXT("GrowthOffer%d"),Index)),[this,Index](){ChooseOffer(Index);},ETrayLook::Skill);
                OfferRow->AddChildToWrapBox(Card.Frame);OfferCards.Add(Card);
            }
            SkipCard=MakeCard(TEXT("GrowthOfferSkip"),[this](){SkipOffer();},ETrayLook::Plain,true);
            OfferRow->AddChildToWrapBox(SkipCard.Frame);
            auto* FinalLine=WidgetTree->ConstructWidget<UHorizontalBox>();
            FinalModGroup=FinalLine;
            auto* FinalHeading=Text(TEXT("终局改装"),LDHUDStyle::TypeBody);
            FinalHeading->SetColorAndOpacity(LDHUDStyle::TextAccent);
            ScaleNewText(FinalHeading);RowWidgets.Add(FinalHeading);
            auto* FinalLabel=WidgetTree->ConstructWidget<USizeBox>();
            FinalLabel->SetWidthOverride(TrayLabelWidth-8.f);FinalLabel->SetContent(FinalHeading);
            FinalLine->AddChildToHorizontalBox(FinalLabel)->SetVerticalAlignment(VAlign_Center);
            FinalModRow=TrayRow(WidgetTree);
            FinalLine->AddChildToHorizontalBox(FinalModRow)->SetPadding(FMargin(8,0,0,0));AddRow(FinalLine);
            for(int32 Index=0;Index<4;++Index)
            {
                FTrayCard Card=MakeCard(FName(*FString::Printf(TEXT("GrowthFinal%d"),Index)),[this,Index](){if(IsFinalStage()){SelectFinalMod(Index);}else{ReopenFinalStage(Index);}});
                FinalModRow->AddChildToWrapBox(Card.Frame);FinalCards.Add(Card);
            }
            TrayFrame=WidgetTree->ConstructWidget<UBorder>();TrayFrame->SetBrushColor(FLinearColor::Transparent);TrayFrame->SetPadding(FMargin(0,0,16,72));
            TrayFrame->SetHorizontalAlignment(HAlign_Right);TrayFrame->SetVerticalAlignment(VAlign_Bottom);
            TrayFrame->SetVisibility(ESlateVisibility::Collapsed);TrayFrame->SetContent(Tray);
            // The offer and final mod rows move onto the stage in the workshop and keep their own width.
            TrayRows={BlueprintCardRow,CardRow};
        }
        {
            // #198 rest services on the workshop (APP_SPEC §7 构筑): each stands on the object it acts on and shows why it is
            // refused. The trainer's 洗点 at the character column's foot, 遗忘 and the selected blueprint's 学到/替换 on the Q/W/E
            // heads, the blueprints to pick at the top of the pending area; the merchant's card at the top of the pending area;
            // every confirm line in the dock.
            const auto Boxed=[&](UWidget* Lead,TObjectPtr<UTextBlock>& OutBlocked,float Wrap)
            {
                auto* Box=WidgetTree->ConstructWidget<UVerticalBox>();
                Box->AddChildToVerticalBox(Lead)->SetHorizontalAlignment(HAlign_Left);
                OutBlocked=Wrapped(LDHUDStyle::TypeCaption,Wrap);OutBlocked->SetColorAndOpacity(LDHUDStyle::TextSecondary);
                Box->AddChildToVerticalBox(OutBlocked)->SetPadding(FMargin(0,4,0,0));
                Box->SetVisibility(ESlateVisibility::Collapsed);
                return Box;
            };
            RespecButton=LDWidgets::HoldButton(WidgetTree,LDTerms::Respec,[this](){Respec();},TEXT("GrowthRestRespec"));
            RespecBox=Boxed(RespecButton.Get(),RespecBlocked,300.f);
            // Mockup 05: 洗点 spans the foot of the character column; 遗忘 is the right click on a skill head.
            if(UVerticalBoxSlot* LeadSlot=Cast<UVerticalBoxSlot>(RespecButton->Slot)){LeadSlot->SetHorizontalAlignment(HAlign_Fill);}
            ForgetBlocked=Wrapped(LDHUDStyle::TypeCaption,300.f);ForgetBlocked->SetColorAndOpacity(LDHUDStyle::TextSecondary);
            Cast<UVerticalBox>(RespecBox.Get())->AddChildToVerticalBox(ForgetBlocked)->SetPadding(FMargin(0,2,0,0));
            TrainerPicks=WidgetTree->ConstructWidget<UWrapBox>();TrainerPicks->SetInnerSlotPadding(FVector2D(6,6));TrainerPicks->SetVisibility(ESlateVisibility::Collapsed);
            auto* Body=WidgetTree->ConstructWidget<UVerticalBox>();ServiceDockBody=Body;
            // The confirm line names what is forgotten or replaced; the cards it sends back light up on the board first.
            auto* ConfirmRow=WidgetTree->ConstructWidget<UHorizontalBox>();
            RestConfirmText=Wrapped(LDHUDStyle::TypeText,ServicePanelWidth-ServiceConfirmButtons);RestConfirmText->SetColorAndOpacity(LDHUDStyle::TextWarning);
            auto* ConfirmTextSlot=ConfirmRow->AddChildToHorizontalBox(RestConfirmText);ConfirmTextSlot->SetVerticalAlignment(VAlign_Center);
            ConfirmTextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            RestConfirmButton=LDWidgets::HoldButton(WidgetTree,LDTerms::ConfirmAction,[this](){ConfirmRestAction();},TEXT("GrowthRestConfirm"));
            ConfirmRow->AddChildToHorizontalBox(RestConfirmButton)->SetPadding(FMargin(8,0,0,0));
            ConfirmRow->AddChildToHorizontalBox(LDWidgets::Button(WidgetTree,LDTerms::CancelAction,[this](){CancelRestAction();},false,TEXT("GrowthRestCancel")))
                ->SetPadding(FMargin(6,0,0,0));
            ConfirmRow->SetVisibility(ESlateVisibility::Collapsed);
            Body->AddChildToVerticalBox(ConfirmRow)->SetPadding(FMargin(0,8,0,0));
            RestConfirmLine=ConfirmRow;
            UScaleBox* Fit=LDWidgets::Fit(WidgetTree,Body);ServiceFit=Fit;
            UBorder* Dock=LDWidgets::Panel(WidgetTree,Fit,FMargin(24,18));Dock->SetBrushColor(LDHUDStyle::UIRaised.CopyWithNewOpacity(1.f));
            Dock->SetVisibility(ESlateVisibility::Collapsed);ServiceDock=Dock;RestSection=Dock;
        }
        {
            // Spec §3.13 rest shop over the board: vouchers, refresh, the skill point and the pick tickets on the head line, the
            // skill then card slots with their prices, the sell zone a library card is dragged onto. Shrinks to fit, never
            // scrolls.
            auto* Tabs=WidgetTree->ConstructWidget<UHorizontalBox>();
            DrawerTabs=Tabs;
            auto* RewardsTab=LDWidgets::Button(WidgetTree,TEXT("奖励"),[this]()
            {
                bLibraryOnly=false;bShopOpen=false;StageLeftFor.Invalidate();RefreshAll();
            },false,TEXT("GrowthRewardsTab"));
            Tabs->AddChildToHorizontalBox(RewardsTab)->SetPadding(FMargin(0,0,6,0));
            ShopToggleButton=LDWidgets::Button(WidgetTree,LDTerms::ShopTitle,[this](){ToggleShop();},false,TEXT("GrowthShopToggle"));
            Tabs->AddChildToHorizontalBox(ShopToggleButton)->SetPadding(FMargin(0,0,6,0));
            auto* LibraryTab=LDWidgets::Button(WidgetTree,TEXT("牌库"),[this]()
            {
                bLibraryOnly=true;bShopOpen=false;
                RefreshAll();
            },false,TEXT("GrowthLibraryTab"));
            Tabs->AddChildToHorizontalBox(LibraryTab);
            ShopToggleButton->SetVisibility(ESlateVisibility::Collapsed);
            auto* Body=WidgetTree->ConstructWidget<UVerticalBox>();
            auto* Head=WidgetTree->ConstructWidget<UHorizontalBox>();
            UTextBlock* Title=LDWidgets::Text(WidgetTree,LDTerms::ShopTitle,LDHUDStyle::TypeHeading,NAME_None,LDUIFonts::ERole::Title);
            Title->SetAutoWrapText(false);
            Head->AddChildToHorizontalBox(Title)->SetVerticalAlignment(VAlign_Center);
            ShopPoints=Text(TEXT(""),LDHUDStyle::TypeBody);ShopPoints->SetAutoWrapText(false);ShopPoints->SetColorAndOpacity(LDHUDStyle::TextAccent);
            auto* PointsSlot=Head->AddChildToHorizontalBox(ShopPoints);PointsSlot->SetVerticalAlignment(VAlign_Center);PointsSlot->SetPadding(FMargin(18,0,0,0));
            Head->AddChildToHorizontalBox(WidgetTree->ConstructWidget<USpacer>())->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            ShopRefreshButton=LDWidgets::Button(WidgetTree,TEXT(""),[this](){RefreshShopShelf();},false,TEXT("GrowthShopRefresh"));
            ShopPointButton=LDWidgets::Button(WidgetTree,TEXT(""),[this](){BuyShopSkillPoint();},false,TEXT("GrowthShopSkillPoint"));
            for(ULDCommandButton* Button:{ShopRefreshButton.Get(),ShopPointButton.Get()})
            {auto* ButtonSlot=Head->AddChildToHorizontalBox(Button);ButtonSlot->SetVerticalAlignment(VAlign_Center);ButtonSlot->SetPadding(FMargin(10,0,0,0));}
            Body->AddChildToVerticalBox(Head)->SetHorizontalAlignment(HAlign_Fill);
            ShopBlockedText=Wrapped(LDHUDStyle::TypeCaption,900.f);ShopBlockedText->SetColorAndOpacity(LDHUDStyle::TextSecondary);
            ShopBlockedText->SetVisibility(ESlateVisibility::Collapsed);
            Body->AddChildToVerticalBox(ShopBlockedText)->SetPadding(FMargin(0,6,0,0));
            ShopSlotRow=WidgetTree->ConstructWidget<UUniformGridPanel>();
            for(int32 Index=0;Index<8;++Index)
            {
                auto* Cell=WidgetTree->ConstructWidget<UVerticalBox>();
                FTrayCard Card=MakeCard(FName(*FString::Printf(TEXT("GrowthShopSlot%d"),Index)),[this,Index](){BuyShopSlot(Index);});
                Cell->AddChildToVerticalBox(Card.Frame)->SetHorizontalAlignment(HAlign_Center);
                UTextBlock* Price=Text(TEXT(""),LDHUDStyle::TypeBody);Price->SetAutoWrapText(false);Price->SetJustification(ETextJustify::Center);
                Cell->AddChildToVerticalBox(Price)->SetHorizontalAlignment(HAlign_Center);
                Cell->SetVisibility(ESlateVisibility::Collapsed);
                ShopSlotRow->AddChildToUniformGrid(Cell,Index/4,Index%4)->SetHorizontalAlignment(HAlign_Center);
                ShopCards.Add(Card);ShopPrices.Add(Price);ShopCells.Add(Cell);RowWidgets.Add(Cell);
            }
            Body->AddChildToVerticalBox(ShopSlotRow)->SetPadding(FMargin(0,18,0,0));
            UScaleBox* ShopFit=LDWidgets::Fit(WidgetTree,Body);
            if(UScaleBoxSlot* FitSlot=Cast<UScaleBoxSlot>(Body->Slot)){FitSlot->SetHorizontalAlignment(HAlign_Center);FitSlot->SetVerticalAlignment(VAlign_Top);}
            UBorder* ShopFrame=LDWidgets::Panel(WidgetTree,ShopFit,FMargin(28,22));ShopFrame->SetBrushColor(LDHUDStyle::UIRaised.CopyWithNewOpacity(1.f));
            ShopFrame->SetVisibility(ESlateVisibility::Collapsed);ShopPanel=ShopFrame;
        }
        {
            // Active and final-mod choices move from their tray rows into the drawer.
            auto* Box=WidgetTree->ConstructWidget<UVerticalBox>();
            StageTitle=LDWidgets::Text(WidgetTree,TEXT(""),LDHUDStyle::TypeHero,NAME_None,LDUIFonts::ERole::Title);StageTitle->SetAutoWrapText(false);
            StageTitle->SetJustification(ETextJustify::Center);ScaleNewText(StageTitle);
            Box->AddChildToVerticalBox(StageTitle)->SetHorizontalAlignment(HAlign_Center);
            StageSub=LDWidgets::Text(WidgetTree,TEXT(""),LDHUDStyle::TypeSmall,NAME_None,LDUIFonts::ERole::Strong);StageSub->SetAutoWrapText(false);
            StageSub->SetColorAndOpacity(LDHUDStyle::TextSecondary);ScaleNewText(StageSub);
            Box->AddChildToVerticalBox(StageSub)->SetHorizontalAlignment(HAlign_Center);
            StageCards=WidgetTree->ConstructWidget<UHorizontalBox>();
            auto* CardsSlot=Box->AddChildToVerticalBox(StageCards);CardsSlot->SetHorizontalAlignment(HAlign_Center);CardsSlot->SetPadding(FMargin(0,28,0,0));
            UScaleBox* StageFit=LDWidgets::Fit(WidgetTree,Box);
            if(UScaleBoxSlot* FitSlot=Cast<UScaleBoxSlot>(Box->Slot)){FitSlot->SetHorizontalAlignment(HAlign_Center);}
            StageFit->SetVisibility(ESlateVisibility::Collapsed);StageBox=StageFit;
            StageBack=LDWidgets::Button(WidgetTree,TEXT("收起"),[this](){LeaveStage();},false,TEXT("GrowthStageBack"));
            StageBack->SetVisibility(ESlateVisibility::Collapsed);
            // The final mod's dock: its one rule sentence, its numbers and the hold that locks it.
            auto* Desc=WidgetTree->ConstructWidget<UVerticalBox>();
            FinalDescText=Wrapped(LDHUDStyle::TypeText,ServicePanelWidth+300.f);FinalDescText->SetColorAndOpacity(LDHUDStyle::UIText);
            FinalDescText->SetAutoWrapText(false);
            Desc->AddChildToVerticalBox(FinalDescText)->SetPadding(FMargin(0,8,0,0));
            FinalNumbersText=Wrapped(LDHUDStyle::TypeCaption,ServicePanelWidth+300.f);FinalNumbersText->SetColorAndOpacity(LDHUDStyle::TextSecondary);
            FinalNumbersText->SetAutoWrapText(false);
            Desc->AddChildToVerticalBox(FinalNumbersText)->SetPadding(FMargin(0,8,0,0));
            UScaleBox* DescFit=LDWidgets::Fit(WidgetTree,Desc);
            if(UScaleBoxSlot* FitSlot=Cast<UScaleBoxSlot>(Desc->Slot)){FitSlot->SetVerticalAlignment(VAlign_Center);}
            DescFit->SetVisibility(ESlateVisibility::Collapsed);FinalDesc=DescFit;
            FinalLockButton=LDWidgets::HoldButton(WidgetTree,LDTerms::FinalLock,[this](){LockFinalMod();},TEXT("GrowthFinalLock"));
            FinalLockButton->SetVisibility(ESlateVisibility::Collapsed);
            FinalLockNote=Wrapped(LDHUDStyle::TypeCaption,280.f);FinalLockNote->SetColorAndOpacity(LDHUDStyle::UIEvent);FinalLockNote->SetJustification(ETextJustify::Center);
            FinalLockNote->SetVisibility(ESlateVisibility::Collapsed);
        }
        // Two layers over the canvas: the tray (pending area or quick picks) and the stage and services, placed on the
        // workshop's rectangles.
        Layer=WidgetTree->ConstructWidget<UCanvasPanel>();Layer->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        for(UWidget* Item:{StageBox.Get(),static_cast<UWidget*>(StageBack.Get()),FinalDesc.Get(),static_cast<UWidget*>(FinalLockButton.Get()),
            static_cast<UWidget*>(FinalLockNote.Get()),RespecBox.Get(),static_cast<UWidget*>(TrainerPicks.Get()),ServiceDock.Get(),
            ShopPanel.Get(),DrawerTabs.Get()})
        {UCanvasPanelSlot* ItemSlot=Layer->AddChildToCanvas(Item);ItemSlot->SetAutoSize(true);ItemSlot->SetZOrder(Item==ServiceDock.Get()?3:Item==StageBox.Get()?1:2);LayerSlots.Add(Item,ItemSlot);}
        auto* Layers=WidgetTree->ConstructWidget<UOverlay>();Layers->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        for(UWidget* Each:{static_cast<UWidget*>(TrayFrame.Get()),static_cast<UWidget*>(Layer.Get())})
        {auto* LayerSlot=Layers->AddChildToOverlay(Each);LayerSlot->SetHorizontalAlignment(HAlign_Fill);LayerSlot->SetVerticalAlignment(VAlign_Fill);}
        WidgetTree->RootWidget=Layers;
        SetVisibility(ESlateVisibility::Collapsed);
    }
    return Super::RebuildWidget();
}
ULDChoicesPanel::FTrayCard ULDChoicesPanel::MakeCard(FName ButtonName,TFunction<void()> Click,ETrayLook Look,bool bHold)
{
    FTrayCard Card;
    const FLinearColor LookRim=Look==ETrayLook::Skill?LDHUDStyle::RewardActiveRim:Look==ETrayLook::Card?LDHUDStyle::RewardCardRim:LDHUDStyle::ShapeRim;
    const float LookRadius=Look==ETrayLook::Skill?3.f:TrayIconRadius;
    auto* Face=WidgetTree->ConstructWidget<UVerticalBox>();RowWidgets.Add(Face);
    Card.KindBannerText=LDWidgets::Text(WidgetTree,TEXT(""),LDHUDStyle::TypeMicro,NAME_None,LDUIFonts::ERole::Strong);
    Card.KindBannerText->SetAutoWrapText(false);Card.KindBannerText->SetJustification(ETextJustify::Center);
    ScaleNewText(Card.KindBannerText);RowWidgets.Add(Card.KindBannerText);
    auto* BannerBorder=WidgetTree->ConstructWidget<UBorder>();RowWidgets.Add(BannerBorder);
    BannerBorder->SetBrush(FSlateRoundedBoxBrush(FLinearColor(0.f,0.f,0.f,.42f),3.f,FLinearColor::Transparent,0.f));
    BannerBorder->SetPadding(FMargin(5,1));BannerBorder->SetContent(Card.KindBannerText);
    BannerBorder->SetVisibility(ESlateVisibility::Collapsed);Card.KindBanner=BannerBorder;
    Face->AddChildToVerticalBox(BannerBorder)->SetHorizontalAlignment(HAlign_Center);
    if(UVerticalBoxSlot* BSlot=Cast<UVerticalBoxSlot>(BannerBorder->Slot)){BSlot->SetPadding(FMargin(0,0,0,3));}

    auto* IconBox=WidgetTree->ConstructWidget<UOverlay>();RowWidgets.Add(IconBox);
    auto* Shape=WidgetTree->ConstructWidget<UBorder>();Shape->SetVisibility(ESlateVisibility::HitTestInvisible);RowWidgets.Add(Shape);
    Shape->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White,LookRadius,LookRim,Look==ETrayLook::Plain?1.5f:2.2f));Shape->SetBrushColor(LDHUDStyle::ShapeFill);
    Card.Shape=Shape;
    auto* ShapeSlot=IconBox->AddChildToOverlay(Shape);ShapeSlot->SetHorizontalAlignment(HAlign_Fill);ShapeSlot->SetVerticalAlignment(VAlign_Fill);
    Card.Picture=WidgetTree->ConstructWidget<UImage>();Card.Picture->SetVisibility(ESlateVisibility::Collapsed);RowWidgets.Add(Card.Picture);
    auto* PictureSlot=IconBox->AddChildToOverlay(Card.Picture);PictureSlot->SetHorizontalAlignment(HAlign_Fill);PictureSlot->SetVerticalAlignment(VAlign_Fill);
    Card.Glyph=LDWidgets::Text(WidgetTree,TEXT(""),LDHUDStyle::TypeSection);Card.Glyph->SetAutoWrapText(false);Card.Glyph->SetJustification(ETextJustify::Center);
    ScaleNewText(Card.Glyph);RowWidgets.Add(Card.Glyph);
    auto* GlyphSlot=IconBox->AddChildToOverlay(Card.Glyph);GlyphSlot->SetHorizontalAlignment(HAlign_Center);GlyphSlot->SetVerticalAlignment(VAlign_Center);
    Card.HotkeyText=LDWidgets::Text(WidgetTree,TEXT(""),LDHUDStyle::TypeMicro,NAME_None,LDUIFonts::ERole::Number);
    Card.HotkeyText->SetAutoWrapText(false);Card.HotkeyText->SetColorAndOpacity(LDHUDStyle::UIAction);ScaleNewText(Card.HotkeyText);RowWidgets.Add(Card.HotkeyText);
    auto* HotkeyBorder=WidgetTree->ConstructWidget<UBorder>();RowWidgets.Add(HotkeyBorder);
    HotkeyBorder->SetBrush(FSlateRoundedBoxBrush(FLinearColor(0.f,0.f,0.f,.78f),4.f,LDHUDStyle::UIAction.CopyWithNewOpacity(.65f),1.f));
    HotkeyBorder->SetPadding(FMargin(4,0));HotkeyBorder->SetContent(Card.HotkeyText);HotkeyBorder->SetVisibility(ESlateVisibility::Collapsed);
    HotkeyBorder->SetRenderTranslation(FVector2D(-4,-4));Card.HotkeyChip=HotkeyBorder;
    auto* HotkeySlot=IconBox->AddChildToOverlay(HotkeyBorder);HotkeySlot->SetHorizontalAlignment(HAlign_Left);HotkeySlot->SetVerticalAlignment(VAlign_Top);
    Card.IconFrame=WidgetTree->ConstructWidget<USizeBox>();Card.IconFrame->SetWidthOverride(TrayCardIcon);Card.IconFrame->SetHeightOverride(TrayCardIcon);
    Card.IconFrame->SetContent(IconBox);RowWidgets.Add(Card.IconFrame);
    Face->AddChildToVerticalBox(Card.IconFrame)->SetHorizontalAlignment(HAlign_Center);
    Card.Name=LDWidgets::Text(WidgetTree,TEXT(""),LDHUDStyle::TypeBody);Card.Name->SetAutoWrapText(false);
    Card.Name->SetWrapTextAt(TrayCardWidth-2.f*TrayCardPadding);
    Card.Name->SetJustification(ETextJustify::Center);ScaleNewText(Card.Name);RowWidgets.Add(Card.Name);
    auto* NameSlot=Face->AddChildToVerticalBox(Card.Name);NameSlot->SetHorizontalAlignment(HAlign_Fill);NameSlot->SetPadding(FMargin(0,4,0,0));
    Card.Benefit=LDWidgets::Text(WidgetTree,TEXT(""),LDHUDStyle::TypeTiny,NAME_None,LDUIFonts::ERole::Strong);
    Card.Benefit->SetAutoWrapText(false);Card.Benefit->SetJustification(ETextJustify::Center);
    Card.Benefit->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
    Card.Benefit->SetVisibility(ESlateVisibility::Collapsed);ScaleNewText(Card.Benefit);RowWidgets.Add(Card.Benefit);
    Face->AddChildToVerticalBox(Card.Benefit)->SetPadding(FMargin(0,2,0,0));
    // A world button: picking a card in the field never takes keyboard focus from the game.
    Card.Button=bHold?static_cast<ULDCommandButton*>(LDWidgets::HoldButton(WidgetTree,TEXT(""),MoveTemp(Click),ButtonName))
        :LDWidgets::Button(WidgetTree,TEXT(""),MoveTemp(Click),true,ButtonName);
    RowWidgets.Add(Card.Button);
    Card.Button->SetContent(Face);
    const LDUIPaint::EButton InitialLook=Look==ETrayLook::Skill?LDUIPaint::EButton::CardActive
        :Look==ETrayLook::Card?LDUIPaint::EButton::CardBuild:LDUIPaint::EButton::Card;
    GrowthCardLook(Card.Button,InitialLook,Look==ETrayLook::Skill?LDHUDStyle::RewardActiveRim
        :Look==ETrayLook::Card?LDHUDStyle::RewardCardRim:LDHUDStyle::CardIdle);
    {auto Style=Card.Button->GetStyle();Style.NormalPadding=FMargin(TrayCardPadding);Style.PressedPadding=FMargin(TrayCardPadding);Card.Button->SetStyle(Style);}
    // After SetContent: replacing the caption makes a new, centred slot; the face fills the card instead.
    if(UButtonSlot* FaceSlot=Cast<UButtonSlot>(Card.Button->GetContentSlot())){FaceSlot->SetHorizontalAlignment(HAlign_Fill);FaceSlot->SetVerticalAlignment(VAlign_Center);}
    Card.Frame=WidgetTree->ConstructWidget<USizeBox>();Card.Frame->SetWidthOverride(TrayCardWidth);
    Card.Frame->SetContent(Card.Button);
    Card.Frame->SetVisibility(ESlateVisibility::Collapsed);RowWidgets.Add(Card.Frame);
    return Card;
}
bool ULDChoicesPanel::CardStale(const FTrayCard& Card,const FString& Key) const
{
    return bCardsStale||!Card.ShownKey.Equals(Key,ESearchCase::CaseSensitive);
}
void ULDChoicesPanel::ShowFace(FTrayCard& Card,const FString& Key,const FString& Name,const FString& IconId,const FString& Glyph,
    const LDHUD::FActionDetailView& Detail,const FString& Rule)
{
    // Use an authored opening clause when it already fits on a compact face.
    int32 End=0;
    while(End<Rule.Len()&&!FCString::Strchr(TEXT("，。；、：,.;:\n"),Rule[End])){++End;}
    const FString Benefit=Rule.Left(End).TrimStartAndEnd();
    GrowthText(Card.Benefit,!Benefit.IsEmpty()&&Benefit.Len()<=10?Benefit:FString());
    Card.ShownKey=Key;Card.Detail=Detail;Card.FullName=Name;
    GrowthText(Card.Name,Name);GrowthText(Card.Glyph,Glyph);
    Card.AppliedIcon=IconId;Card.AppliedEnabled=-1;
}
void ULDChoicesPanel::ShowLook(FTrayCard& Card,bool bTile,bool bEnabled,bool bVisible)
{
    GrowthShown(Card.Frame,bVisible);
    const bool bCombatCompact=!bOpen&&bQuickShown&&bQuickCompact&&!Card.bBig;
    // Combat-folded compact mode keeps icon + 4-char short name + category color frame (#331).
    if(bCombatCompact){bTile=false;}
    bTile|=bIconOnlyTray&&!Card.bBig;
    Card.bTile=bTile;
    if(!Card.FullName.IsEmpty())
    {
        GrowthText(Card.Name,bCombatCompact?Card.FullName.Left(4):Card.FullName);
    }
    if(!Card.bBig)
    {
        const float Scale=GetCachedGeometry().Scale>0.f?GetCachedGeometry().Scale:1.f;
        const bool bUseCompact=bCompactTray||bCombatCompact;
        const float Width=TrayCellWidth(bTile,Scale,bUseCompact);
        // #328 §2: Element stones (medal_*) use a true circular crystal badge silhouette (square aspect Width x Width).
        const float Height=Card.bElementStone&&!bTile
            ?FMath::Max(Width,TrayCellHeight(bTile,AppliedTextScale,Scale,bUseCompact)*.88f)
            :TrayCellHeight(bTile,AppliedTextScale,Scale,bUseCompact);
        const float Icon=Card.bElementStone?TrayIconSize(bTile,Scale,bUseCompact)*.86f:TrayIconSize(bTile,Scale,bUseCompact);
        if(!FMath::IsNearlyEqual(Card.Frame->GetWidthOverride(),Width)){Card.Frame->SetWidthOverride(Width);}
        if(!FMath::IsNearlyEqual(Card.Frame->GetHeightOverride(),Height)){Card.Frame->SetHeightOverride(Height);}
        if(!FMath::IsNearlyEqual(Card.IconFrame->GetWidthOverride(),Icon))
        {Card.IconFrame->SetWidthOverride(Icon);Card.IconFrame->SetHeightOverride(Icon);}
        if(!bTile&&!FMath::IsNearlyEqual(Card.Name->GetWrapTextAt(),Width-2.f*TrayCardPadding))
        {Card.Name->SetWrapTextAt(Width-2.f*TrayCardPadding);}
    }
    GrowthShown(Card.Name,!bTile,ESlateVisibility::HitTestInvisible);
    GrowthShown(Card.Benefit,!bTile&&!bCombatCompact&&!Card.Benefit->GetText().IsEmpty(),ESlateVisibility::HitTestInvisible);
    GrowthEnabled(Card.Button,bEnabled);
    // A card that cannot be taken now greys its picture too; the popup says why.
    if(Card.AppliedEnabled!=int32(bEnabled))
    {
        Card.AppliedEnabled=int32(bEnabled);
        const float PicRadius=Card.bElementStone?Card.IconFrame->GetWidthOverride()*.5f:TrayIconRadius;
        const bool bPicture=LDUIIcons::Apply(Card.Picture,Card.AppliedIcon,PicRadius,bEnabled?FLinearColor::White:LDHUDStyle::IconBlocked);
        GrowthShown(Card.Glyph,!bPicture,ESlateVisibility::HitTestInvisible);
    }
}
void ULDChoicesPanel::SetTrayInset(float Right,float Bottom,float Width,float Height)
{
    const FMargin Inset(0,0,Right,Bottom);
    if(TrayFrame&&!(TrayFrame->GetPadding()==Inset)){TrayFrame->SetPadding(Inset);}
    const float Wrap=(Width>0.f?FMath::Min(Width,TrayWidth):TrayWidth)+1.f;
    TrayAvailableWidth=Wrap;
    TrayAvailableHeight=Height;
    for(UWrapBox* Row:TrayRows){if(Row&&!FMath::IsNearlyEqual(Row->GetWrapSize(),Wrap)){Row->SetWrapSize(Wrap);}}
    const float RowWidth=FMath::Max(0.f,Wrap-TrayLabelWidth);
    for(UWrapBox* Row:{OfferRow.Get(),FinalModRow.Get()})
    {
        if(Row&&!FMath::IsNearlyEqual(Row->GetWrapSize(),RowWidth)){Row->SetWrapSize(RowWidth);}
    }
}
const UWidget* ULDChoicesPanel::Hovered(FVector2D Pointer,LDHUD::FActionDetailView& Out,FName* OutComponent,FString* OutIcon) const
{
    if(OutComponent){*OutComponent=NAME_None;}
    // Combat-folded compact cards still pop full hover details on explicit mouse hover (#331).
    if((!bOpen&&!bQuickShown)||!Tray){return nullptr;}
    const FTrayCard* Found=nullptr;
    const bool bOfferPinnedSurface=(OfferRow&&OfferRow->IsVisible())||(bStageShown&&!bStageFinal);
    if(bPinnedOfferExplicit&&bOfferPinnedSurface&&OfferCards.IsValidIndex(PinnedPreview)
        &&OfferCards[PinnedPreview].Frame&&OfferCards[PinnedPreview].Frame->IsVisible())
    {
        Found=&OfferCards[PinnedPreview];
    }
    const auto Over=[&Found,Pointer](const FTrayCard& Card)
    {
        if(!Found&&Card.Frame&&Card.Frame->IsVisible()&&Card.Frame->GetCachedGeometry().IsUnderLocation(Pointer)){Found=&Card;}
    };
    const auto Row=[&Over](const UWidget* Box,const TArray<FTrayCard>& Cards){if(Box&&Box->IsVisible()){for(const FTrayCard& Card:Cards){Over(Card);}}};
    Row(OfferRow,OfferCards);if(OfferRow&&OfferRow->IsVisible()){Over(SkipCard);}
    if(bStageShown&&StageCards&&StageCards->IsVisible()){for(const FTrayCard& Card:OfferCards){Over(Card);}Over(SkipCard);for(const FTrayCard& Card:FinalCards){Over(Card);}}
    Row(CardRow,CardCards);Row(BlueprintCardRow,BlueprintCards);
    Row(FinalModRow,FinalCards);
    if(ShopPanel&&ShopPanel->IsVisible()){Row(ShopSlotRow,ShopCards);}
    if(!Found&&bOfferPinnedSurface&&OfferCards.IsValidIndex(PinnedPreview)&&OfferCards[PinnedPreview].Frame&&OfferCards[PinnedPreview].Frame->IsVisible()){Found=&OfferCards[PinnedPreview];}
    if(!Found){return nullptr;}
    Out=Found->Detail;
    if(OutComponent){*OutComponent=Found->Component;}
    if(OutIcon){*OutIcon=Found->AppliedIcon;}
    return Found->Frame;
}
bool ULDChoicesPanel::IsEngaged() const
{
    return (bOpen&&Tray&&Tray->IsHovered())||PendingRequest!=0||HasChildConfirmation()||!PendingFinalOption.IsNone();
}
bool ULDChoicesPanel::HasCards() const
{
    if(!Usable()){return false;}
    LDHUD::FGrowthPending Pending=LDHUD::GrowthPending(&Latest,LatestMatch,LatestContent);
    if(ConfirmedOfferStillShown()){Pending.bOffer=false;}
    return Pending.Any()||!Latest.FinalMod.IsEmpty();
}
void ULDChoicesPanel::Tell(const FString& Text)
{
    if(!Text.IsEmpty()&&Notify){Notify(Text);}
}
UWidget* ULDChoicesPanel::FocusScope() const{return WidgetTree?WidgetTree->RootWidget.Get():nullptr;}
UWidget* ULDChoicesPanel::HitArea() const{return Tray.Get();}
void ULDChoicesPanel::OpenPanel()
{
    if(const auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());PC&&PC->BuildBlueprints&&PC->BuildBlueprints->IsViewerOpen())
    {PC->BuildBlueprints->ToggleViewer();}
    bOpen=true;bLibraryOnly=false;Fold.Opened(false,UiNow());
}
bool ULDChoicesPanel::ClosePanel()
{
    // Closing only hides the workshop: the offer, the blueprints and any submitted request keep their state; an unconfirmed
    // replace, forget, fill gap or dismantle is withdrawn with its pickers, an unlocked final selection with it.
    LDWidgets::ReleaseHolds(WidgetTree);
    RestConfirm=FRestConfirm();GrowthShown(RestConfirmLine,false);
    PinnedPreview=INDEX_NONE;PendingFinalOption=NAME_None;bOpen=false;
    bFinalFeedbackAllowed=false;
    const LDHUD::FOfferMotionTicket PendingOffer=OfferMotion.Ticket;
    OfferMotion=FOfferMotion();
    if(PendingRequest){OfferMotion.Ticket=PendingOffer;}
    if(!bQuickShown||bHeadless){GrowthShown(TrayFrame,false);SetVisibility(ESlateVisibility::Collapsed);}
    Fold.Closed();RefreshAll();return true;
}
FString ULDChoicesPanel::ConfirmLine() const
{
    if(RestConfirm.Kind==ELDBuildCommandKind::None){return FString();}
    const auto Name=[this](FName Id){return LDHUD::ContentName(LatestContent,Id.ToString());};
    FString Line=RestConfirm.Kind==ELDBuildCommandKind::ReplaceActive
        ?LDTerms::ReplaceConfirm(Name(RestConfirm.Blueprint),SlotKeys.Of(RestConfirm.Slot),Name(RestConfirm.Active))
        :LDTerms::ForgetConfirm(SlotKeys.Of(RestConfirm.Slot),Name(RestConfirm.Active));
    const FString Refusal=RestConfirmRefusal();
    if(!Refusal.IsEmpty()){Line+=TEXT("（")+Refusal+TEXT("）");}
    return Line;
}
bool ULDChoicesPanel::IsServiceShown() const
{
    // Only a rest's build windows (build and settlement/payout) offer the rest rebuilds; the locked steps and the return take them away.
    return bOpen&&(Places.bActive||bHeadless)&&!bStageShown&&LatestStage==LDProgression::EMatchStage::Rest;
}
bool ULDChoicesPanel::IsBlueprintSwapShown() const
{
    return bOpen&&(Places.bActive||bHeadless)&&!bLibraryOnly&&!bStageShown&&(LatestStage==LDProgression::EMatchStage::Rest||Gate.bDormSwap);
}
bool ULDChoicesPanel::LeaveStage()
{
    if(!bStageShown){return false;}
    StageLeftFor=bStageFinal?Latest.FinalModOfferId:Latest.ActiveOffer.OfferId;
    LDWidgets::ReleaseHolds(WidgetTree);
    RefreshAll();return true;
}
void ULDChoicesPanel::SetQuickPick(bool bInQuick,bool bCompact)
{
    if(bQuick==bInQuick&&bQuickCompact==bCompact){return;}
    bQuick=bInQuick;bQuickCompact=bCompact;bCardsStale=true;
}
bool ULDChoicesPanel::IsQuickHit(FVector2D Absolute) const
{
    return bQuickShown&&Tray&&Tray->IsVisible()&&Tray->GetCachedGeometry().IsUnderLocation(Absolute);
}
TArray<FName> ULDChoicesPanel::ConfirmWarnings() const
{
    TArray<FName> Out;
    if(RestConfirm.Kind==ELDBuildCommandKind::ReplaceActive||RestConfirm.Kind==ELDBuildCommandKind::ForgetActive)
    {
        const LDHUD::FStorageView Shown=LDHUD::CardStorage(Latest,LatestContent);
        const int32 Column=int32(LDHUD::SlotColumn(RestConfirm.Slot));
        if(Shown.Columns.IsValidIndex(Column))
        {
            TSet<uint16> ReclaimedSerials;
            if(RestConfirm.Kind==ELDBuildCommandKind::ReplaceActive&&LatestContent)
            {
                FLDGrowthBuild Current=LDProgression::BuildFromView(*LatestContent,Latest),After;
                FLDProgressionState Dummy;FName Forgotten;TArray<uint16> Reclaimed;
                LDProgression::FBuildContext Context;Context.Stage=LDProgression::EMatchStage::Rest;Context.LifePhase=ELDLifePhase::Alive;
                if(LDProgression::ApplyReplaceActive(*LatestContent,Current,Dummy,RestConfirm.Blueprint,RestConfirm.Slot,Context,After,Forgotten,Reclaimed)==ELDGrowthReason::None)
                {
                    for(uint16 Serial:Reclaimed){ReclaimedSerials.Add(Serial);}
                }
            }
            auto AppendFiltered=[&](const TArray<FName>& Keys)
            {
                if(RestConfirm.Kind==ELDBuildCommandKind::ForgetActive||!LatestContent){Out.Append(Keys);return;}
                for(const FName& Key:Keys)
                {
                    const uint16 Serial=LDHUD::CardSerial(Key);
                    if(Serial!=0&&ReclaimedSerials.Contains(Serial)){Out.Add(Key);}
                }
            };
            for(const LDHUD::FEquippedView& Entry:Shown.Columns[Column].Entries){AppendFiltered(Entry.Keys);}
            if(Shown.Columns[Column].bHasMedal){AppendFiltered(Shown.Columns[Column].Medal.Keys);}
        }
    }
    return Out;
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
    if(bOwnHit&&OfferMotion.StartedAt>=0.){OfferMotion.StartedAt=-1.;OfferMotion.Others.Reset();}
    if(bOwnHit){bFinalFeedbackAllowed=false;}
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
bool ULDChoicesPanel::ConfirmedOfferStillShown() const
{
    return ConfirmedOffer.IsValid()&&LatestMatch==ConfirmedMatch&&LatestLife==ConfirmedLife
        &&Latest.bHasActiveOffer&&Latest.ActiveOffer.OfferId==ConfirmedOffer;
}
bool ULDChoicesPanel::HasAvailableOffer() const
{
    return Usable()&&Latest.bHasActiveOffer&&!Latest.ActiveOffer.Options.IsEmpty()&&!ConfirmedOfferStillShown();
}
bool ULDChoicesPanel::HasAvailableFinal() const
{
    return Usable()&&!ConfirmedOfferStillShown()&&Latest.bFinalModPending
        &&(Latest.FinalModOptions.Num()==3||Latest.FinalModOptions.Num()==4);
}
bool ULDChoicesPanel::OfferMotionActive() const
{
    return bOpen&&OfferMotion.StartedAt>=0.&&UiNow()-OfferMotion.StartedAt<LDHUDStyle::MotionChoose;
}
void ULDChoicesPanel::CaptureOfferMotion(int32 Index)
{
    OfferMotion=FOfferMotion();
    const bool bSkip=Index==INDEX_NONE;
    if(!bSkip&&!Latest.ActiveOffer.Options.IsValidIndex(Index)){return;}
    OfferMotion.Ticket.Match=LatestMatch;OfferMotion.Ticket.Life=LatestLife;
    OfferMotion.Ticket.Offer=Latest.ActiveOffer.OfferId;
    OfferMotion.Ticket.Kind=bSkip?ELDBuildCommandKind::SkipActiveOffer:ELDBuildCommandKind::ActiveChoice;
    if(!bSkip){OfferMotion.Ticket.Option=Latest.ActiveOffer.Options[Index];}
    if(!bStageShown||bStageFinal||(!bSkip&&!OfferCards.IsValidIndex(Index))){return;}
    const auto Capture=[this](const FTrayCard& Card)
    {
        FOfferGhost Ghost;
        if(!Card.Frame||!Card.Frame->IsVisible()){return Ghost;}
        Ghost.Frame=Card.Frame->GetCachedGeometry().GetLayoutBoundingRect();
        if(Card.IconFrame&&Card.IconFrame->IsVisible()){Ghost.Icon=Card.IconFrame->GetCachedGeometry().GetLayoutBoundingRect();}
        if(Card.Name)
        {
            Ghost.Name=Card.Name->GetText().ToString();
            Ghost.NameBounds=Card.Name->GetCachedGeometry().GetLayoutBoundingRect();
            Ghost.NameSize=Card.Name->GetFont().Size*float(Card.Name->GetCachedGeometry().Scale/FMath::Max(.01f,GetCachedGeometry().Scale));
        }
        if(Card.Glyph){Ghost.Glyph=Card.Glyph->GetText().ToString();}
        if(!Card.AppliedIcon.IsEmpty()){Ghost.Texture=LDUIIcons::Find(Card.AppliedIcon);}
        return Ghost;
    };
    OfferMotion.Chosen=Capture(bSkip?SkipCard:OfferCards[Index]);
    if(bSkip)
    {
        const UWidget* Source=SkipCard.Name.Get();
        if(Source&&Source->IsVisible()){OfferMotion.Chosen.Icon=Source->GetCachedGeometry().GetLayoutBoundingRect();}
    }
    if(!GrowthHasArea(OfferMotion.Chosen.Icon)){return;}
    for(int32 Other=0;Other<OfferCards.Num();++Other)
    {
        if((bSkip||Other!=Index)&&OfferCards[Other].Frame&&OfferCards[Other].Frame->IsVisible()){OfferMotion.Others.Add(Capture(OfferCards[Other]));}
    }
    if(!bSkip&&SkipCard.Frame&&SkipCard.Frame->IsVisible()){OfferMotion.Others.Add(Capture(SkipCard));}
}
void ULDChoicesPanel::UpdateInputs(const FLDProgressionView& View,uint32 Match,uint32 Life,ELDLifePhase LifePhase,LDProgression::EMatchStage Stage,
    const FLDDemoContentDefinitions* Content,const LDHUD::FCardStats& Stats,const LDHUD::FSlotKeys& Keys,bool bDormRested)
{
    if(OfferMotion.Ticket.Match!=0&&(OfferMotion.Ticket.Match!=Match||OfferMotion.Ticket.Life!=Life)){OfferMotion=FOfferMotion();}
    if(ConfirmedOffer.IsValid()&&(ConfirmedMatch!=Match||ConfirmedLife!=Life))
    {ConfirmedOffer.Invalidate();ConfirmedMatch=ConfirmedLife=0;}
    const bool bStatsChanged=Stats.Level!=LatestStats.Level||Stats.ADP2!=LatestStats.ADP2||Stats.APP2!=LatestStats.APP2
        ||Stats.MaxHPP2!=LatestStats.MaxHPP2||Stats.ArmorP2!=LatestStats.ArmorP2||Stats.bShowScalingFormula!=LatestStats.bShowScalingFormula
        ||Content!=LatestContent||!(Keys==SlotKeys);
    SlotKeys=Keys;
    if(Match!=LatestMatch||View.ActiveOffer.OfferId!=Latest.ActiveOffer.OfferId||View.CardOffer.OfferId!=Latest.CardOffer.OfferId
        ||View.FinalModOfferId!=Latest.FinalModOfferId||View.PendingBlueprints.Num()>Latest.PendingBlueprints.Num())
    {
        bLibraryOnly=false;
    }
    Latest=View;LatestMatch=Match;LatestLife=Life;LatestLifePhase=LifePhase;LatestStage=Stage;
    LatestContent=Content;LatestStats=Stats;
    if(Latest.Shop.bStocked&&(Match!=AutoShopMatch||uint32(Latest.Shop.Revision)!=AutoShopStock))
    {
        bShopOpen=true;bLibraryOnly=false;
        AutoShopMatch=Match;
        AutoShopStock=uint32(Latest.Shop.Revision);
    }
    if(!Tray){return;}
    Gate=Usable()?LDHUD::BuildGate(Stage,LifePhase,bDormRested):LDHUD::FBuildGateView();
    if(bStatsChanged){bCardsStale=true;}
}
void ULDChoicesPanel::Present(const FLDProgressionView& View,uint32 Match,uint32 Life,ELDLifePhase LifePhase,LDProgression::EMatchStage Stage,
    const FLDDemoContentDefinitions* Content,const LDHUD::FCardStats& Stats,const LDHUD::FSlotKeys& Keys,bool bDormRested)
{
    UpdateInputs(View,Match,Life,LifePhase,Stage,Content,Stats,Keys,bDormRested);
    if(bOpen||bQuick||bQuickShown){RefreshAll();}
}

ULDChoicesPanel::FDrawerState ULDChoicesPanel::DrawerState(bool bWorkshopOpen) const
{
    FDrawerState State;
    if(!bWorkshopOpen||!bOpen){return State;}
    const bool bOffer=HasAvailableOffer();
    const bool bFinal=!bOffer&&HasAvailableFinal();
    const FGuid Key=bOffer?Latest.ActiveOffer.OfferId:bFinal?Latest.FinalModOfferId:FGuid();
    State.bLibraryOnly=bLibraryOnly;
    State.bStage=!bLibraryOnly&&(bOffer||bFinal)&&!(Key.IsValid()&&Key==StageLeftFor);
    State.bFinalStage=State.bStage&&bFinal;
    State.bCharacterServices=!State.bStage&&LatestStage==LDProgression::EMatchStage::Rest;
    State.bServices=!bLibraryOnly&&!State.bStage&&(State.bCharacterServices||Gate.bDormSwap);
    State.bShop=State.bCharacterServices&&bShopOpen&&Usable()&&Latest.Shop.bStocked;
    return State;
}

void ULDChoicesPanel::PresentWithBounds(const FWorkshopPlaces& InPlaces,FVector2D Inset,float Width,float Height)
{
    Places=InPlaces;
    SetTrayInset(Inset.X,Inset.Y,Width,Height);
    if(bOpen||bQuick||bQuickShown){RefreshAll();}
}

void ULDChoicesPanel::RefreshAll()
{
    if(!Tray){return;}
    if(bHeadless)
    {
        // The ring shows exactly the current view: the guards every command carries follow it.
        ShownBlueprints=Usable()?Latest.PendingBlueprints:TArray<FName>();
        const bool bOffer=HasAvailableOffer();
        ShownOffer=bOffer?Latest.ActiveOffer.OfferId:FGuid();ShownOfferRevision=bOffer?Latest.ActiveOffer.OfferRevision:0;
        ShownFinalMod=HasAvailableFinal()?Latest.FinalModOfferId:FGuid();
        const bool bCards=Usable()&&Latest.bHasCardOffer&&!Latest.CardOffer.Options.IsEmpty();
        ShownCardOffer=bCards?Latest.CardOffer.OfferId:FGuid();ShownCardRevision=bCards?Latest.CardOffer.OfferRevision:0;
        ShownCardOptions=bCards?Latest.CardOffer.Options:TArray<FName>();
        bQuickShown=false;bStageShown=false;
        const bool bRest=Usable()&&(Gate.bInRest||Gate.bDormSwap);
        if(RestConfirm.Kind!=ELDBuildCommandKind::None&&(!bRest||!RestConfirmHolds()))
        {
            RestConfirm=FRestConfirm();
            if(bRest){Tell(TEXT("技能槽或图纸已变化，请重新选择。"));}
        }
        if(GetVisibility()!=ESlateVisibility::Collapsed){SetVisibility(ESlateVisibility::Collapsed);}
        bCardsStale=false;
        return;
    }
    ShownBlueprints=Usable()?Latest.PendingBlueprints:TArray<FName>();
    const bool bTiles=false;
    // Field quick picks stand above the 构筑 button while the workshop is closed and something waits; while engaged in field
    // combat (#345), non-opening offers stay folded into the compact corner badge so big cards never block combat sight,
    // unfolding automatically once disengaged or in Rest.
    const bool bOpeningOffer=HasAvailableOffer()&&Latest.ActiveOffer.OpportunityOrdinal==1;
    const bool bStashedInCombat=bQuickCompact&&!bOpeningOffer&&!HasPendingRequest();
    const bool bQuickNow=!bOpen&&bQuick&&!bStashedInCombat&&Usable()&&HasCards();
    bQuickShown=bQuickNow;
    GrowthShown(this,(bOpen&&Places.bActive)||bQuickShown);
    UpdateStage();
    PlanTrayPages(bTiles);
    RefreshOffer();RefreshFinalMod();RefreshCardOffer(bTiles||bIconOnlyTray);
    RefreshBlueprintCards();RefreshBlueprints();RefreshRest();
    RefreshStage();
    RefreshShopPanel();
    LayoutLayer();
    bCardsStale=false;
}
void ULDChoicesPanel::PlanTrayPages(bool bTiles)
{
    TrayUnits.Reset();TrayPages.Reset();
    bCompactTray=false;bIconOnlyTray=false;
    if(!bOpen||bLibraryOnly||bStageShown||!Usable())
    {
        bTrayOverflow=false;bTrayOverflowReported=false;GrowthShown(TrayPager,false);return;
    }
    if(TrayAvailableHeight<=0.f){bTrayOverflow=true;bTrayOverflowReported=false;GrowthShown(TrayPager,false);return;}
    const float Width=TrayAvailableWidth>0.f?TrayAvailableWidth:TrayWidth;
    const float Scale=GetCachedGeometry().Scale>0.f?GetCachedGeometry().Scale:1.f;
    const auto Plan=[&](bool bCompact,bool bIcons)
    {
        TrayUnits.Reset();
        TArray<float> Heights;
        bool bFitsWidth=true;
        const auto PerRow=[&](bool bTile,float Leading=0.f)
        {
            bTile|=bIcons;
            const float Gap=bTile?TrayTileGap:TrayCardGap;
            return FMath::Max(1,FMath::FloorToInt((Width-Leading+Gap)/(TrayCellWidth(bTile,Scale,bCompact)+Gap)));
        };
        const auto Add=[&](ETrayGroup Group,int32 First,int32 Count,bool bTile,float Leading=0.f)
        {
            if(Count<=0){return;}
            bTile|=bIcons;
            if(Width-Leading<TrayCellWidth(bTile,Scale,bCompact)){bFitsWidth=false;return;}
            const int32 Rows=FMath::DivideAndRoundUp(Count,PerRow(bTile,Leading));
            const float Gap=bTile?TrayTileGap:TrayCardGap;
            const float Height=Rows*TrayCellHeight(bTile,AppliedTextScale,Scale,bCompact)+(Rows-1)*Gap+TrayCardGap;
            TrayUnits.Add({Group,First,Count});Heights.Add(Height);
        };
        if(FirstEmptySlot()<3)
        {
            const int32 Per=PerRow(false);
            for(int32 First=0;First<ShownBlueprints.Num();First+=Per)
            {Add(ETrayGroup::Blueprint,First,FMath::Min(Per,ShownBlueprints.Num()-First),false);}
        }
        // The card offer: its cards and the reroll/skip column.
        if(Latest.bHasCardOffer){Add(ETrayGroup::CardOffer,0,Latest.CardOffer.Options.Num()+1,bTiles);}
        const bool bOffer=HasAvailableOffer();
        if(bOffer){Add(ETrayGroup::Offer,0,Latest.ActiveOffer.Options.Num()+1,false,TrayLabelWidth);}
        else if(!ConfirmedOfferStillShown()&&Latest.QueuedActiveOpportunities>0){Add(ETrayGroup::Offer,0,1,false,TrayLabelWidth);}
        const bool bFinal=HasAvailableFinal();
        if(bFinal||!Latest.FinalMod.IsEmpty()){Add(ETrayGroup::FinalMod,0,bFinal?Latest.FinalModOptions.Num():1,false,TrayLabelWidth);}
        return bFitsWidth?LDWorkshop::FitPending(Heights,TrayAvailableHeight,TrayPagerHeight):LDWorkshop::FPendingFit();
    };
    LDWorkshop::FPendingFit Fit=Plan(false,false);
    if(!Fit.bFits){bCompactTray=true;Fit=Plan(true,false);}
    if(!Fit.bFits){bIconOnlyTray=true;Fit=Plan(true,true);}
    if(!Fit.bFits)
    {
        if(!bTrayOverflowReported)
        {
            UE_LOG(LogLDUI,Warning,TEXT("LD_HUD_PENDING_OVERFLOW width=%.0f height=%.0f groups=%d"),Width,TrayAvailableHeight,TrayUnits.Num());
            Tell(LDTerms::PendingNoRoom);
            bTrayOverflowReported=true;
        }
        bTrayOverflow=true;GrowthShown(TrayPager,false);return;
    }
    bTrayOverflow=false;bTrayOverflowReported=false;
    for(const LDWorkshop::FPendingPage& Page:Fit.Pages){TrayPages.Add(FIntPoint(Page.First,Page.Count));}
    TrayPage=FMath::Clamp(TrayPage,0,TrayPages.Num()-1);
    GrowthShown(TrayPager,TrayPages.Num()>1);
    GrowthText(TrayPageText,FString::Printf(TEXT("%d / %d"),TrayPage+1,TrayPages.Num()));
    GrowthEnabled(TrayPrevious,TrayPage>0);GrowthEnabled(TrayNext,TrayPage+1<TrayPages.Num());
    TrayRowTiles(OfferRow,bIconOnlyTray);TrayRowTiles(FinalModRow,bIconOnlyTray);TrayRowTiles(BlueprintCardRow,bIconOnlyTray);
}
bool ULDChoicesPanel::TrayShows(ETrayGroup Group,int32 Index) const
{
    if(bTrayOverflow){return false;}
    if(TrayPages.Num()<=1||!TrayPages.IsValidIndex(TrayPage)){return true;}
    const FIntPoint Page=TrayPages[TrayPage];
    for(int32 Unit=Page.X;Unit<Page.X+Page.Y&&TrayUnits.IsValidIndex(Unit);++Unit)
    {
        const FTrayUnit& Item=TrayUnits[Unit];
        if(Item.Group==Group&&(Group==ETrayGroup::Blueprint?Index>=Item.First&&Index<Item.First+Item.Count:Index==Item.First)){return true;}
    }
    return false;
}
void ULDChoicesPanel::RefreshOffer()
{
    const bool bOffer=HasAvailableOffer();
    // A new offer drops the pinned popup; ids and revisions travel with the click, never a card index.
    if(!bOffer||Latest.ActiveOffer.OfferId!=ShownOffer){PinnedPreview=INDEX_NONE;bPinnedOfferExplicit=false;}
    ShownOffer=bOffer?Latest.ActiveOffer.OfferId:FGuid();ShownOfferRevision=bOffer?Latest.ActiveOffer.OfferRevision:0;
    // A queued opportunity without an eligible candidate waits as one grey card; its description says why.
    const bool bWaiting=!bOffer&&Usable()&&!ConfirmedOfferStillShown()&&Latest.QueuedActiveOpportunities>0;
    const bool bOnStage=bStageShown&&!bStageFinal;
    if(bOnStage&&PinnedPreview==INDEX_NONE&&!Latest.ActiveOffer.Options.IsEmpty()){PinnedPreview=0;}
    const bool bVisible=bOnStage||TrayShows(ETrayGroup::Offer);
    GrowthShown(OfferGroup,(bOffer||bWaiting)&&!bOnStage&&bVisible);
    GrowthShown(OfferRow,(bOffer||bWaiting)&&bVisible);
    if(bWaiting)
    {
        GrowthText(OfferNumber,FString::Printf(TEXT("%u/%u"),uint32(Latest.ActiveOpportunitiesConsumed+1),uint32(Latest.ActiveOpportunitiesEarned)));
        for(int32 Index=1;Index<OfferCards.Num();++Index){GrowthShown(OfferCards[Index].Frame,false);}
        GrowthShown(SkipCard.Frame,false);
        FTrayCard& Card=OfferCards[0];
        const FString Key=TEXT("WAIT");
        if(CardStale(Card,Key))
        {
            LDHUD::FActionDetailView Detail;Detail.Name=LDTerms::OfferWaitingName;Detail.Body=LDTerms::OfferWaiting;
            ShowFace(Card,Key,LDTerms::OfferWaitingName,FString(),TEXT("?"),Detail);
        }
        ShowLook(Card,bQuickShown&&bQuickCompact,false,bVisible);
        return;
    }
    if(bOffer){GrowthText(OfferNumber,FString::Printf(TEXT("%u/%u"),uint32(Latest.ActiveOffer.OpportunityOrdinal),uint32(Latest.ActiveOpportunitiesEarned)));}
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
        const FString Key=Id.ToString()+TEXT("|")+Outcome+FString::Printf(TEXT("|%llu|%d|%d"),Latest.BuildRevision,int32(LatestStage),int32(LatestLifePhase));
        if(CardStale(Card,Key))
        {
            const LDHUD::FActiveCardView View=LDHUD::ActiveCard(Id,LatestContent,LatestStats);
            LDHUD::FActionDetailView Detail=LDHUD::ActiveChoiceDetail(View,false,Outcome);
            // The evidence for the choice: the owned cards that go on it or share its vocabulary, and the reactions with the
            // learned actives.
            const LDWorkshop::FOfferLinks Preview=Gate.bCanChoose
                ?LDWorkshop::LinkableCards(Id,Latest,LatestContent,LatestStage,LatestLifePhase):LDWorkshop::FOfferLinks();
            const TArray<FName>& Links=Preview.Cards;
            TArray<FString> Lines;
            FString LinksLine;
            if(Preview.Failure!=ELDGrowthReason::None)
            {
                UE_LOG(LogLDUI,Warning,TEXT("LD_HUD_OFFER_LINK_PREVIEW_FAILED active=%s reason=%u"),*Id.ToString(),uint32(Preview.Failure));
                Lines.Add(LDTerms::OfferLinksUnavailable(LDHUD::GrowthReasonText(Preview.Failure)));
            }
            if(!Links.IsEmpty())
            {
                TArray<FString> Names;for(const FName& Link:Links){Names.Add(LDHUD::ContentName(LatestContent,LDHUD::CardDefinition(Link).ToString()));}
                LinksLine=LDTerms::OfferLinksLine(FString::Join(Names,TEXT("、")));
                Lines.Add(LinksLine);
            }
            const TArray<ELDDemoElement> Own=LDWorkshop::ActiveElements(Id,LatestContent);
            for(const FLDActiveSlotView& Held:Latest.Actives)
            {
                for(const ELDDemoElement Theirs:LDWorkshop::ActiveElements(Held.DefinitionId,LatestContent))
                {
                    for(const ELDDemoElement Mine:Own)
                    {
                        const FString Reaction=LDWorkshop::ReactionName(Mine,Theirs);
                        if(Reaction.IsEmpty()){continue;}
                        Lines.AddUnique(LDTerms::ReactsWith(SlotKeys.Of(Held.Slot),LDHUD::ContentName(LatestContent,Held.DefinitionId.ToString()),
                            LDHUD::ElementName(Mine)+TEXT(" + ")+LDHUD::ElementName(Theirs),Reaction));
                        Lines.AddUnique(LDTerms::ReactionExplanation(Reaction));
                    }
                }
            }
            if(!Lines.IsEmpty()){Detail.Body+=TEXT("\n\n")+FString::Join(Lines,TEXT("\n"));}
            ShowFace(Card,Key,View.Name,View.IconId,View.Glyph,Detail,View.SentencePlain);
            if(Card.Shape)
            {
                Card.Shape->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White,TrayIconRadius,LDHUDStyle::UIAction,1.8f));
                Card.Shape->SetBrushColor(LDHUDStyle::ShapeFill);
            }
        }
        if(Card.HotkeyText&&Card.HotkeyChip)
        {
            GrowthText(Card.HotkeyText,FString::Printf(TEXT("Alt+%d"),Index+1));
            GrowthShown(Card.HotkeyChip,bVisible,ESlateVisibility::HitTestInvisible);
        }
        Card.bElementStone=false;
        GrowthCardLook(Card.Button,LDUIPaint::EButton::CardActive,LDHUDStyle::RewardActiveRim);
        if(Card.KindBanner&&Card.KindBannerText)
        {
            GrowthText(Card.KindBannerText,TEXT("主动技能"));
            Card.KindBannerText->SetColorAndOpacity(LDHUDStyle::RewardActiveRim);
            GrowthShown(Card.KindBanner,bVisible&&!(bQuickShown&&bQuickCompact),ESlateVisibility::HitTestInvisible);
        }
        ShowLook(Card,false,Gate.bCanChoose&&!PendingRequest,bVisible);
    }
    if(!bOffer){GrowthShown(SkipCard.Frame,false);return;}
    const int64 Points=Latest.ActiveOffer.SkipPoints;
    const FString SkipName=Points>0?LDTerms::OfferSkipPoints(Points):FString(TEXT("不选"));
    const FString SkipKey=SkipName+TEXT("|")+Gate.ChooseBlocked;
    if(CardStale(SkipCard,SkipKey))
    {
        LDHUD::FActionDetailView Detail;
        Detail.Name=SkipName;
        Detail.Body=Points>0?FString(TEXT("放弃本组技能，本次机会不再保留。")):FString(TEXT("奖励数据缺失。"));
        if(!Gate.bCanChoose){Detail.Body+=TEXT("\n\n")+Gate.ChooseBlocked;}
        ShowFace(SkipCard,SkipKey,SkipName,FString(),Points>0?FString::Printf(TEXT("+%lld"),Points):FString(TEXT("+")),Detail);
    }
    ShowLook(SkipCard,bQuickShown&&bQuickCompact,Points>0&&Gate.bCanChoose&&!PendingRequest,bVisible);
    // The skip card's name is only 不选; its points are the glyph on a tile and the facts line on the stage.
    GrowthText(SkipCard.Name,LDTerms::OfferSkip);GrowthShown(SkipCard.IconFrame,!SkipCard.bBig);
}
void ULDChoicesPanel::RefreshFinalMod()
{
    const bool bFinal=HasAvailableFinal();
    const bool bSelected=Usable()&&!Latest.FinalMod.IsEmpty();
    if(!bFinal||ShownFinalMod!=Latest.FinalModOfferId){PendingFinalOption=NAME_None;}
    ShownFinalMod=bFinal?Latest.FinalModOfferId:FGuid();
    const bool bOnStage=IsFinalStage();
    const bool bFinalVisible=bOnStage||TrayShows(ETrayGroup::FinalMod);
    GrowthShown(FinalModGroup,(bFinal||bSelected)&&!bOnStage&&bFinalVisible);
    GrowthShown(FinalModRow,(bFinal||bSelected)&&bFinalVisible);
    for(int32 Index=0;Index<FinalCards.Num();++Index)
    {
        FTrayCard& Card=FinalCards[Index];
        if(!bFinal)
        {
            // The taken card stays on the tray as a reminder; it binds nothing and needs no action.
            if(!bSelected||Index!=0){GrowthShown(Card.Frame,false);continue;}
            const FString Key=TEXT("FIN_SELECTED|")+Latest.FinalMod.DefinitionId.ToString();
            if(CardStale(Card,Key))
            {
                const auto View=LDHUD::FinalModCard(Latest.FinalMod.DefinitionId,LatestContent,LatestStats);
                ShowFace(Card,Key,View.Name,View.IconId,View.Glyph,View.Detail);
            }
            ShowLook(Card,false,false,bFinalVisible);
            GrowthTint(Card.Button,LDHUDStyle::SlotArmed);
            continue;
        }
        if(!Latest.FinalModOptions.IsValidIndex(Index)){GrowthShown(Card.Frame,false);continue;}
        const FName Id=Latest.FinalModOptions[Index];
        const FString Outcome=!Gate.bCanFinalMod?Gate.FinalModBlocked:FString(LDTerms::FinalTakenNote);
        const FString Key=TEXT("FIN|")+Id.ToString()+TEXT("|")+Outcome;
        LDHUD::FFinalModCardView View=LDHUD::FinalModCard(Id,LatestContent,LatestStats);
        if(CardStale(Card,Key))
        {
            View.Detail.Body+=TEXT("\n\n")+Outcome;
            ShowFace(Card,Key,View.Name,View.IconId,View.Glyph,View.Detail);
        }
        ShowLook(Card,false,View.bKnown&&Gate.bCanFinalMod&&!PendingRequest,bFinalVisible);
        GrowthTint(Card.Button,PendingFinalOption==Id?(IsFinalStage()?LDHUDStyle::UIEvent*.35f:LDHUDStyle::SlotArmed):LDHUDStyle::CardIdle);
    }
}
void ULDChoicesPanel::RefreshCardOffer(bool bTiles)
{
    const bool bShow=Usable()&&Latest.bHasCardOffer&&!Latest.CardOffer.Options.IsEmpty();
    ShownCardOffer=bShow?Latest.CardOffer.OfferId:FGuid();
    ShownCardRevision=bShow?Latest.CardOffer.OfferRevision:0;
    ShownCardOptions=bShow?Latest.CardOffer.Options:TArray<FName>();
    // A larger offer (a bonus or bounty pick of five) grows the row; 重抽 and 跳过 stay after the cards.
    while(CardRow&&CardCards.Num()<ShownCardOptions.Num())
    {
        const int32 Index=CardCards.Num();
        FTrayCard Card=MakeCard(FName(*FString::Printf(TEXT("GrowthCard%d"),Index)),[this,Index](){PickCardOffer(Index);},ETrayLook::Card);
        auto* Cell=WidgetTree->ConstructWidget<UVerticalBox>();Cell->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        Cell->AddChildToVerticalBox(Card.Frame)->SetHorizontalAlignment(HAlign_Center);
        CardRow->InsertChildAt(Index,Cell);RowWidgets.Add(Cell);
        CardCards.Add(Card);CardCells.Add(Cell);
    }
    const bool bVisible=bShow&&TrayShows(ETrayGroup::CardOffer);
    GrowthShown(CardRow,bVisible);TrayRowTiles(CardRow,bTiles);
    const bool bCan=Gate.bCanCardOffer&&!PendingRequest;
    const bool bShowHotkeys=bVisible&&!HasAvailableOffer();
    const bool bCombatCompact=!bOpen&&bQuickShown&&bQuickCompact;
    for(int32 Index=0;Index<CardCards.Num();++Index)
    {
        FTrayCard& Card=CardCards[Index];
        const bool bHas=ShownCardOptions.IsValidIndex(Index);
        GrowthShown(CardCells[Index],bHas);
        if(!bHas){continue;}
        const FName Id=ShownCardOptions[Index];
        Card.Component=Id;
        // Where a pick puts it (its form's place, or the library when there is none yet), or why the offer is closed.
        FString Outcome=Gate.bCanCardOffer?FString():Gate.CardOfferBlocked;
        if(Outcome.IsEmpty()&&LatestContent)
        {
            FLDCardTarget Target;
            const FLDGrowthBuild Build=LDProgression::BuildFromView(*LatestContent,Latest);
            LDHUD::EStorageColumn Column=LDHUD::EStorageColumn::Body;
            Outcome=LDHUD::WorkshopFull(Latest,LatestContent)||!LDProgression::DefaultCardTarget(*LatestContent,Build,Id,Target)
                ||!LDHUD::CardColumn(Latest,Target.Kind,Target.Active,Column)?FString(LDTerms::TakeToStorage)
                :LDTerms::DropInstallsOn(Target.Kind==ELDCardTargetKind::Skill?LDHUD::ContentName(LatestContent,Target.Active.ToString())
                    :LDHUD::ColumnLabel(Column));
        }
        bool bCompatMatch=false,bCompatFull=false;
        const FString Compat=LDHUD::SkillCardCompatStrip(Id,Latest,LatestContent,SlotKeys,bCompatMatch,bCompatFull);
        const int32 Copies=LDHUD::OwnedCopies(Latest,Id);
        const FString Key=Id.ToString()+TEXT("|")+Outcome+TEXT("|")+Compat+FString::Printf(TEXT("|%d"),Copies);
        const LDHUD::FCardFaceView View=LDHUD::CardFace(Id,LatestContent,LatestStats);
        if(CardStale(Card,Key))
        {
            LDHUD::FActionDetailView Detail=LDHUD::CardChoiceDetail(View,Compat.IsEmpty()?Outcome:Outcome+TEXT("\n")+Compat);
            ShowFace(Card,Key,Copies>0?View.Name+TEXT(" ")+LDTerms::CopiesBadge(Copies+1):View.Name,View.IconId,View.Glyph,Detail,View.SentencePlain);
        }
        if(Card.Shape)
        {
            const bool bStone=View.Form==ELDCardForm::Medal||Id.ToString().StartsWith(TEXT("medal_"));
            Card.bElementStone=bStone;
            if(bStone)
            {
                const FLinearColor InnerRing=ElementStoneRingColor(Id);
                GrowthCardLook(Card.Button,LDUIPaint::EButton::CardStone,InnerRing);
                const float DiscR=FMath::Max(18.f,Card.IconFrame->GetWidthOverride()*.5f);
                Card.Shape->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White,DiscR,LDHUDStyle::RewardStoneRim,2.2f));
                Card.Shape->SetBrushColor(LDHUDStyle::RewardStoneBase);
                if(Card.KindBanner&&Card.KindBannerText)
                {
                    GrowthText(Card.KindBannerText,TEXT("元素石"));
                    Card.KindBannerText->SetColorAndOpacity(LDHUDStyle::RewardStoneRim);
                    GrowthShown(Card.KindBanner,!bTiles&&!bCombatCompact,ESlateVisibility::HitTestInvisible);
                }
            }
            else
            {
                const FLinearColor RimColor=View.Type==ELDCardType::Convert?LDHUDStyle::RewardCardRare
                    :View.Type==ELDCardType::Grow?LDHUDStyle::RewardCardLegend:LDHUDStyle::RewardCardRim;
                GrowthCardLook(Card.Button,LDUIPaint::EButton::CardBuild,RimColor);
                const FLinearColor KindEdge=LDUIShapes::KindColor(uint8(View.Type));
                Card.Shape->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White,TrayIconRadius,KindEdge,bCombatCompact?2.2f:1.5f));
                Card.Shape->SetBrushColor(LDHUDStyle::ShapeFill);
                if(Card.KindBanner&&Card.KindBannerText)
                {
                    const TCHAR* FormTag=View.Form==ELDCardForm::Skill?TEXT("构筑牌·技能")
                        :View.Form==ELDCardForm::Dash?TEXT("构筑牌·冲刺"):TEXT("构筑牌·身体");
                    GrowthText(Card.KindBannerText,FormTag);
                    Card.KindBannerText->SetColorAndOpacity(RimColor);
                    GrowthShown(Card.KindBanner,!bTiles&&!bCombatCompact,ESlateVisibility::HitTestInvisible);
                }
            }
        }
        if(Card.HotkeyText&&Card.HotkeyChip)
        {
            GrowthText(Card.HotkeyText,FString::Printf(TEXT("Alt+%d"),Index+1));
            GrowthShown(Card.HotkeyChip,bShowHotkeys&&Index<3,ESlateVisibility::HitTestInvisible);
        }
        ShowLook(Card,bTiles,bCan,bVisible);
        if(LatestContent)
        {
            const auto Rarity=LDProgression::CardRarity(*LatestContent,Id);
            const FLinearColor Tint=Rarity==LDProgression::ELDCardRarity::Legendary?FLinearColor(.55f,.42f,.08f,1.f)
                :Rarity==LDProgression::ELDCardRarity::Rare?FLinearColor(.36f,.18f,.55f,1.f)
                :FLinearColor(.16f,.26f,.38f,1.f);
            GrowthTint(Card.Button,Tint);
        }
    }
    GrowthShown(CardActions,bShow&&!bTiles&&!bCombatCompact);
    GrowthText(Cast<UTextBlock>(CardRerollButton->GetContent()),LDTerms::CardReroll(Latest.CardRerolls));
    GrowthEnabled(CardRerollButton,bCan&&Latest.CardRerolls>0);
    GrowthEnabled(CardSkipButton,bCan);
}
bool ULDChoicesPanel::CardOfferCommand(ELDBuildCommandKind Kind,FLDBuildCommand& Out)
{
    if(!Usable()||!Latest.bHasCardOffer||Latest.CardOffer.OfferId!=ShownCardOffer){return false;}
    if(PendingRequest){Tell(TEXT("上一个请求仍在处理中。"));return false;}
    if(!Gate.bCanCardOffer){Tell(TEXT("没有提交：")+Gate.CardOfferBlocked);return false;}
    Out=FLDBuildCommand();
    Out.Kind=Kind;Out.OfferId=ShownCardOffer;Out.OfferRevision=ShownCardRevision;
    return true;
}
bool ULDChoicesPanel::PickCardOffer(int32 Index)
{
    FLDBuildCommand Command;
    if(!ShownCardOptions.IsValidIndex(Index)||!CardOfferCommand(ELDBuildCommandKind::PickCard,Command)){return false;}
    const FName Id=ShownCardOptions[Index];
    if(!FLDDefinitionWire::Encode(Id,Command.Option)){Tell(TEXT("没有提交：这张牌的标识无效。"));return false;}
    // Installed in the same commit on its form's place when there is one and a slot is free; the library keeps it otherwise.
    FLDCardTarget Target;
    if(LatestContent&&!LDHUD::WorkshopFull(Latest,LatestContent)
        &&LDProgression::DefaultCardTarget(*LatestContent,LDProgression::BuildFromView(*LatestContent,Latest),Id,Target))
    {
        Command.bInstallIfPossible=true;Command.CardTarget=Target.Kind;Command.Element=Target.Element;
        if(!Target.Active.IsNone()&&!FLDDefinitionWire::Encode(Target.Active,Command.TargetDefinition)){Tell(TEXT("没有提交：技能标识无效。"));return false;}
    }
    LDWidgets::PlayCue(this,ELDAudioCue::UIChoose);
    return Send(Command);
}
bool ULDChoicesPanel::RerollCardOffer()
{
    FLDBuildCommand Command;
    if(!CardOfferCommand(ELDBuildCommandKind::RerollCards,Command)){return false;}
    if(Latest.CardRerolls<=0){Tell(TEXT("没有提交：")+LDHUD::GrowthReasonText(ELDGrowthReason::NoReroll));return false;}
    return Send(Command);
}
bool ULDChoicesPanel::SkipCardOffer()
{
    FLDBuildCommand Command;
    return CardOfferCommand(ELDBuildCommandKind::SkipCards,Command)&&Send(Command);
}
ULDChoicesPanel::FTrayCard& ULDChoicesPanel::BlueprintCardAt(int32 Index)
{
    while(BlueprintCards.Num()<=Index)
    {
        const int32 CardIndex=BlueprintCards.Num();
        FTrayCard Card=MakeCard(FName(*FString::Printf(TEXT("GrowthBlueprint%d"),CardIndex)),[this,CardIndex](){LearnBlueprintCard(CardIndex);},ETrayLook::Skill);
        BlueprintCardRow->AddChildToWrapBox(Card.Frame);BlueprintCards.Add(Card);
    }
    return BlueprintCards[Index];
}
void ULDChoicesPanel::RefreshBlueprintCards()
{
    // Blueprints are tray cards only while a Q/W/E slot is empty; a full Q/W/E waits for the trainer's replace.
    const uint8 FirstEmpty=FirstEmptySlot();
    const int32 Shown=FirstEmpty<3?ShownBlueprints.Num():0;
    bool bVisible=false;
    for(int32 Index=0;Index<Shown;++Index){bVisible|=TrayShows(ETrayGroup::Blueprint,Index);}
    GrowthShown(BlueprintCardRow,bVisible);
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
            ShowFace(Card,Key,View.Name,View.IconId,View.Glyph,LDHUD::ActiveChoiceDetail(View,true,Outcome),View.SentencePlain);
        }
        Card.bElementStone=false;
        GrowthCardLook(Card.Button,LDUIPaint::EButton::CardActive,LDHUDStyle::RewardActiveRim);
        if(Card.KindBanner&&Card.KindBannerText)
        {
            GrowthText(Card.KindBannerText,TEXT("技能图纸"));
            Card.KindBannerText->SetColorAndOpacity(LDHUDStyle::RewardActiveRim);
            GrowthShown(Card.KindBanner,TrayShows(ETrayGroup::Blueprint,Index),ESlateVisibility::HitTestInvisible);
        }
        ShowLook(Card,false,Gate.bCanLearn&&!PendingRequest,TrayShows(ETrayGroup::Blueprint,Index));
    }
}
ULDChoicesPanel::FBlueprintRow& ULDChoicesPanel::BlueprintRowAt(int32 Index)
{
    while(BlueprintRows.Num()<=Index)
    {
        const int32 RowIndex=BlueprintRows.Num();
        FBlueprintRow& Row=BlueprintRows.AddDefaulted_GetRef();
        Row.Pick=LDWidgets::Button(WidgetTree,TEXT(""),[this,RowIndex](){TrainerBlueprint=RowIndex;CancelRestAction();RefreshAll();},false,
            FName(*FString::Printf(TEXT("GrowthTrainerBlueprint%d"),RowIndex)));
        ScaleNewText(Cast<UTextBlock>(Row.Pick->GetContent()));RowWidgets.Add(Row.Pick);Row.Pick->SetVisibility(ESlateVisibility::Collapsed);
        TrainerPicks->AddChildToWrapBox(Row.Pick);
        const auto OnLayer=[this](ULDCommandButton* Button)
        {
            ScaleNewText(Cast<UTextBlock>(Button->GetContent()));RowWidgets.Add(Button);Button->SetVisibility(ESlateVisibility::Collapsed);
            UCanvasPanelSlot* ItemSlot=Layer->AddChildToCanvas(Button);ItemSlot->SetAutoSize(true);ItemSlot->SetZOrder(2);LayerSlots.Add(Button,ItemSlot);
        };
        for(uint8 Key=0;Key<3;++Key)
        {
            ULDCommandButton* Learn=LDWidgets::Button(WidgetTree,TEXT(""),[this,RowIndex,Key](){LearnBlueprint(RowIndex,Key);},false,
                FName(*FString::Printf(TEXT("GrowthLearn%d%s"),RowIndex,*LDHUD::SlotKey(Key))));
            OnLayer(Learn);Row.Learn.Add(Learn);
            // #198 rest only: the blueprint replaces the active in this occupied slot, after the confirm line.
            ULDCommandButton* Replace=LDWidgets::Button(WidgetTree,TEXT(""),[this,RowIndex,Key](){AskReplace(RowIndex,Key);},false,
                FName(*FString::Printf(TEXT("GrowthReplace%d%s"),RowIndex,*LDHUD::SlotKey(Key))));
            OnLayer(Replace);Row.Replace.Add(Replace);
        }
    }
    return BlueprintRows[Index];
}
void ULDChoicesPanel::RefreshBlueprints()
{
    // The rest rebuild buttons on the Q/W/E heads for the selected blueprint (rest or a dorm rest on the field); the tray shows
    // blueprints as cards (RefreshBlueprintCards).
    const bool bShown=IsBlueprintSwapShown()&&!ShownBlueprints.IsEmpty();
    GrowthShown(TrainerPicks,bShown);
    if(!ShownBlueprints.IsValidIndex(TrainerBlueprint)){TrainerBlueprint=0;}
    bool bEmpty[3]={false,false,false};
    for(uint8 Key=0;Key<3;++Key){bEmpty[Key]=LDHUD::ActiveInSlot(Latest,Key).IsNone();}
    // A full Q/W/E: the blueprint replaces an occupied slot (after a confirm).
    const FString ReplaceWhy=Gate.bInRest||Gate.bDormSwap?ReplaceRefusal():FString(LDTerms::ReplaceAtRest);
    for(int32 Index=0;Index<FMath::Max(ShownBlueprints.Num(),BlueprintRows.Num());++Index)
    {
        const bool bRow=bShown&&ShownBlueprints.IsValidIndex(Index);
        if(!bRow&&!BlueprintRows.IsValidIndex(Index)){continue;}
        FBlueprintRow& Row=BlueprintRowAt(Index);
        GrowthShown(Row.Pick,bRow,ESlateVisibility::Visible);
        const bool bSelected=bRow&&Index==TrainerBlueprint;
        if(bRow)
        {
            GrowthText(Cast<UTextBlock>(Row.Pick->GetContent()),FString(LDTerms::Blueprint)+TEXT(" · ")+LDHUD::ContentName(LatestContent,ShownBlueprints[Index].ToString()));
            GrowthTint(Row.Pick,bSelected?LDHUDStyle::SlotArmed:LDHUDStyle::CardIdle);
        }
        for(uint8 Key=0;Key<3;++Key)
        {
            GrowthText(Cast<UTextBlock>(Row.Learn[Key]->GetContent()),LDTerms::LearnInto(SlotKeys.Of(Key)));
            GrowthText(Cast<UTextBlock>(Row.Replace[Key]->GetContent()),LDTerms::ReplaceInto(SlotKeys.Of(Key)));
            GrowthShown(Row.Learn[Key],bSelected&&bEmpty[Key],ESlateVisibility::Visible);
            GrowthEnabled(Row.Learn[Key],Gate.bCanLearn&&!PendingRequest);
            GrowthShown(Row.Replace[Key],bSelected&&!bEmpty[Key],ESlateVisibility::Visible);
            GrowthEnabled(Row.Replace[Key],ReplaceWhy.IsEmpty()&&!PendingRequest);
            const FText Why=FText::FromString(ReplaceWhy);
            if(!Row.Replace[Key]->GetToolTipText().EqualTo(Why)){Row.Replace[Key]->SetToolTipText(Why);}
            GrowthTint(Row.Replace[Key],LDHUDStyle::DropReplace*.35f);
        }
    }
}
bool ULDChoicesPanel::ChooseOffer(int32 Index)
{
    if(!HasAvailableOffer()||!Latest.ActiveOffer.Options.IsValidIndex(Index)||Latest.ActiveOffer.OfferId!=ShownOffer){return false;}
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
    CaptureOfferMotion(Index);
    if(Send(Command)){return true;}
    OfferMotion=FOfferMotion();return false;
}
bool ULDChoicesPanel::SkipOffer()
{
    if(!HasAvailableOffer()||Latest.ActiveOffer.OfferId!=ShownOffer||Latest.ActiveOffer.SkipPoints<=0){return false;}
    if(PendingRequest){Tell(TEXT("上一个请求仍在处理中。"));return false;}
    if(!Gate.bCanChoose){Tell(TEXT("没有提交：")+Gate.ChooseBlocked);return false;}
    FLDBuildCommand Command;
    Command.Kind=ELDBuildCommandKind::SkipActiveOffer;
    Command.OfferId=Latest.ActiveOffer.OfferId;Command.OfferRevision=Latest.ActiveOffer.OfferRevision;
    PinnedPreview=INDEX_NONE;
    CaptureOfferMotion(INDEX_NONE);
    if(Send(Command)){return true;}
    OfferMotion=FOfferMotion();return false;
}
bool ULDChoicesPanel::PickByHotkey(int32 Index)
{
    if(Index<0||Index>=3||(!bOpen&&!bQuickShown)){return false;}
    if(HasAvailableOffer()&&Latest.ActiveOffer.Options.IsValidIndex(Index)){return ChooseOffer(Index);}
    if(Usable()&&Latest.bHasCardOffer&&ShownCardOptions.IsValidIndex(Index)){return PickCardOffer(Index);}
    if(HasAvailableFinal()&&Latest.FinalModOptions.IsValidIndex(Index))
    {
        return IsFinalStage()?SelectFinalMod(Index):ReopenFinalStage(Index);
    }
    return false;
}
bool ULDChoicesPanel::ChooseFinalMod(int32 Index)
{
    if(!Usable()||!Latest.bFinalModPending||!Latest.FinalModOptions.IsValidIndex(Index)||Latest.FinalModOfferId!=ShownFinalMod){return false;}
    return SubmitFinalMod(Latest.FinalModOptions[Index]);
}
bool ULDChoicesPanel::SubmitFinalMod(FName Option)
{
    if(PendingRequest){Tell(TEXT("上一个请求仍在处理中。"));return false;}
    if(!Usable()||!Latest.bFinalModPending||Latest.FinalModOfferId!=ShownFinalMod||!Latest.FinalModOptions.Contains(Option)){return false;}
    if(!Gate.bCanFinalMod){Tell(TEXT("没有提交：")+Gate.FinalModBlocked);return false;}
    FLDBuildCommand Command;
    Command.Kind=ELDBuildCommandKind::FinalMod;Command.OfferId=ShownFinalMod;Command.Slot=FLDBuildCommand::AnySlot;
    if(!FLDDefinitionWire::Encode(Option,Command.Option)){Tell(TEXT("没有提交：终局改装标识无效。"));return false;}
    PendingFinalOption=NAME_None;
    SubmittedFinal.DefinitionId=Option;
    bFinalFeedbackAllowed=bOpen;
    if(Send(Command)){return true;}
    SubmittedFinal=FLDFinalModSelection();bFinalFeedbackAllowed=false;return false;
}
bool ULDChoicesPanel::SelectFinalMod(int32 Index)
{
    // The stage only selects; the hold of GrowthFinalLock commits.
    if(!Usable()||!Latest.bFinalModPending||!Latest.FinalModOptions.IsValidIndex(Index)||Latest.FinalModOfferId!=ShownFinalMod){return false;}
    PendingFinalOption=Latest.FinalModOptions[Index];
    RefreshAll();return true;
}
bool ULDChoicesPanel::ReopenFinalStage(int32 Index)
{
    // A final card picked outside the page (after leaving it, or from the field quick picks) brings the page back with that
    // card selected, so its description, sources and hold always read in the dock.
    if(!Usable()||!Latest.bFinalModPending||!Latest.FinalModOptions.IsValidIndex(Index)||Latest.FinalModOfferId!=ShownFinalMod){return false;}
    if(!bOpen)
    {
        const auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());
        if(!PC||!PC->RootUI||!PC->RootUI->ShowMainPanel(ELDUIMainPanel::Choices)){return false;}
    }
    StageLeftFor=FGuid();
    return SelectFinalMod(Index);
}
bool ULDChoicesPanel::LockFinalMod()
{
    if(PendingFinalOption.IsNone()){Tell(TEXT("没有提交：先选一张终局改装。"));return false;}
    return SubmitFinalMod(PendingFinalOption);
}
bool ULDChoicesPanel::PreviewOffer(int32 Index)
{
    if(Index==INDEX_NONE){bPinnedOfferExplicit=false;PinnedPreview=INDEX_NONE;return true;}
    if(!Latest.bHasActiveOffer||!Latest.ActiveOffer.Options.IsValidIndex(Index)||Latest.ActiveOffer.OfferId!=ShownOffer){return false;}
    PinnedPreview=Index;bPinnedOfferExplicit=true;return true;
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
FString ULDChoicesPanel::RespecRefusal() const
{
    if(!Gate.bCanRespec){return Gate.RespecBlocked;}
    int64 Allocated=0;for(const FLDStatPointsView& Row:Latest.StatPoints){Allocated+=FMath::Max(0,Row.Points);}
    return Allocated>0?FString():LDHUD::GrowthReasonText(ELDGrowthReason::NothingToRespec);
}
FString ULDChoicesPanel::ReplaceRefusal() const
{
    if(!Gate.bCanReplace){return Gate.ReplaceBlocked;}
    return Latest.bHasActiveOffer?LDHUD::GrowthReasonText(ELDGrowthReason::OfferOutstanding):FString();
}
FString ULDChoicesPanel::ForgetRefusal() const
{
    if(!Gate.bCanForget){return Gate.ForgetBlocked;}
    return Latest.bHasActiveOffer?LDHUD::GrowthReasonText(ELDGrowthReason::OfferOutstanding):FString();
}
bool ULDChoicesPanel::RestConfirmHolds() const
{
    if(RestConfirm.Kind==ELDBuildCommandKind::None||!Usable()){return false;}
    if(LDHUD::ActiveInSlot(Latest,RestConfirm.Slot)!=RestConfirm.Active){return false;}
    return RestConfirm.Kind!=ELDBuildCommandKind::ReplaceActive||Latest.PendingBlueprints.Contains(RestConfirm.Blueprint);
}
FString ULDChoicesPanel::RestConfirmRefusal() const
{
    switch(RestConfirm.Kind)
    {
    case ELDBuildCommandKind::ReplaceActive: return ReplaceRefusal();
    case ELDBuildCommandKind::ForgetActive: return ForgetRefusal();
    default: return LDHUD::GrowthReasonText(ELDGrowthReason::InvalidRequest);
    }
}
void ULDChoicesPanel::RefreshRest()
{
    if(!RestSection){return;}
    const bool bShown=Usable()&&(Gate.bInRest||Gate.bDormSwap);
    // A confirm line whose terms changed (another slot content, a used blueprint, changed components, the rest over) is
    // withdrawn, never resent.
    if(RestConfirm.Kind!=ELDBuildCommandKind::None&&(!bShown||!RestConfirmHolds()))
    {
        RestConfirm=FRestConfirm();
        if(bShown){Tell(TEXT("技能槽或图纸已变化，请重新选择。"));}
    }
    GrowthShown(RestConfirmLine,RestConfirm.Kind!=ELDBuildCommandKind::None,ESlateVisibility::SelfHitTestInvisible);
    const bool bServices=IsServiceShown();
    // A dorm swap on the field confirms in the same dock; respec and forget stay in a rest.
    GrowthShown(ServiceDock,(IsServiceShown()||IsBlueprintSwapShown())&&RestConfirm.Kind!=ELDBuildCommandKind::None,ESlateVisibility::Visible);
    GrowthShown(RespecBox,bServices);
    if(!bShown){return;}
    const auto Service=[this](ULDCommandButton* Button,UTextBlock* Why,const FString& Refusal)
    {
        GrowthEnabled(Button,Refusal.IsEmpty()&&!PendingRequest);
        GrowthText(Why,Refusal);
        GrowthShown(Why,!Refusal.IsEmpty(),ESlateVisibility::HitTestInvisible);
    };
    Service(RespecButton,RespecBlocked,RespecRefusal());
    {
        // 洗点 says how many points it gives back.
        int64 Allocated=0;for(const FLDStatPointsView& Row:Latest.StatPoints){Allocated+=Row.Points;}
        GrowthText(Cast<UTextBlock>(RespecButton->GetContent()),Allocated>0?FString::Printf(TEXT("%s +%lld"),LDTerms::Respec,Allocated):FString(LDTerms::Respec));
    }
    const FString Forget=ForgetRefusal();
    bool bAnyLearned=false;
    for(uint8 Key=0;Key<3;++Key){bAnyLearned|=!LDHUD::ActiveInSlot(Latest,Key).IsNone();}
    const FString ForgetWhy=bAnyLearned?Forget:FString(TEXT("还没有学会的技能可遗忘"));
    GrowthText(ForgetBlocked,ForgetWhy);
    GrowthShown(ForgetBlocked,!ForgetWhy.IsEmpty(),ESlateVisibility::HitTestInvisible);
    if(RestConfirm.Kind!=ELDBuildCommandKind::None)
    {
        const auto Name=[this](FName Id){return LDHUD::ContentName(LatestContent,Id.ToString());};
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
        default: break;
        }
        if(!Refusal.IsEmpty()){Line+=TEXT("（")+Refusal+TEXT("）");}
        GrowthText(RestConfirmText,Line);
        // Replacing and forgetting are irreversible and commit on a full hold.
        RestConfirmButton->SetHoldRequired(true);
        GrowthEnabled(RestConfirmButton,Refusal.IsEmpty()&&!PendingRequest);
    }
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
bool ULDChoicesPanel::ForgetAtColumn(int32 Column)
{
    if(!IsServiceShown()){return false;}
    for(uint8 Key=0;Key<3;++Key){if(int32(LDHUD::SlotColumn(Key))==Column){AskForget(Key);return true;}}
    return false;
}
bool ULDChoicesPanel::ConfirmRestAction()
{
    if(RestConfirm.Kind==ELDBuildCommandKind::None){return false;}
    if(PendingRequest){Tell(TEXT("上一个请求仍在处理中。"));return false;}
    if(!RestConfirmHolds()){CancelRestAction();Tell(TEXT("技能槽或图纸已变化，请重新选择。"));return false;}
    const FString Refusal=RestConfirmRefusal();
    if(!Refusal.IsEmpty()){Tell(TEXT("没有提交：")+Refusal);return false;}
    FLDBuildCommand Command;
    Command.Kind=RestConfirm.Kind;
    Command.Slot=RestConfirm.Slot;
    if(RestConfirm.Kind==ELDBuildCommandKind::ReplaceActive&&!FLDDefinitionWire::Encode(RestConfirm.Blueprint,Command.Blueprint))
    {Tell(TEXT("没有提交：这份图纸的标识无效。"));return false;}
    // The line is consumed by the attempt; the receipt or the refusal reason takes its place.
    RestConfirm=FRestConfirm();
    GrowthShown(RestConfirmLine,false);
    return Send(Command);
}
void ULDChoicesPanel::CancelRestAction()
{
    RestConfirm=FRestConfirm();
    GrowthShown(RestConfirmLine,false);
}
bool ULDChoicesPanel::CloseChildConfirmation()
{
    if(RestConfirm.Kind==ELDBuildCommandKind::None){return false;}
    CancelRestAction();
    return true;
}
bool ULDChoicesPanel::IsShopOffered() const
{
    return bOpen&&(Places.bActive||bHeadless)&&Usable()&&LatestStage==LDProgression::EMatchStage::Rest&&Latest.Shop.bStocked;
}
bool ULDChoicesPanel::IsShopShown() const
{
    return bShopOpen&&!bStageShown&&IsShopOffered();
}
bool ULDChoicesPanel::ToggleShop()
{
    if(!IsShopOffered()){bShopOpen=false;return false;}
    if(bStageShown){StageLeftFor=StageShownFor;bShopOpen=true;}
    else{bShopOpen=!bShopOpen;}
    bLibraryOnly=false;
    if(bShopOpen){CancelRestAction();}
    RefreshAll();
    return true;
}
void ULDChoicesPanel::RefreshShopPanel()
{
    if(!ShopPanel){return;}
    const bool bOffered=IsShopOffered();
    const bool bShown=IsShopShown();
    GrowthShown(DrawerTabs,bOpen&&Places.bActive);
    GrowthShown(ShopToggleButton,bOffered,ESlateVisibility::Visible);
    GrowthShown(ShopPanel,bShown&&GrowthHasArea(Places.Shop),ESlateVisibility::Visible);
    if(!bOffered){return;}
    const FLDShopView& Shop=Latest.Shop;
    GrowthTint(ShopToggleButton,bShown?LDHUDStyle::UIAction:LDHUDStyle::CardIdle);
    if(!bShown){return;}
    const bool bCan=Gate.bCanShop&&!PendingRequest;
    // Spec §3.13: the shop takes unspent stat points (a respec in this rest frees the allocated ones).
    const int64 Points=Latest.UnspentPoints;
    GrowthText(ShopPoints,FString::Printf(TEXT("%s %lld"),LDTerms::PointGlyph,Points));
    GrowthText(ShopBlockedText,Gate.bCanShop?FString():Gate.ShopBlocked);
    GrowthShown(ShopBlockedText,!Gate.bCanShop,ESlateVisibility::HitTestInvisible);
    GrowthText(Cast<UTextBlock>(ShopRefreshButton->GetContent()),LDTerms::ShopRefresh(Shop.RefreshPrice));
    GrowthEnabled(ShopRefreshButton,bCan&&Points>=Shop.RefreshPrice);
    GrowthText(Cast<UTextBlock>(ShopPointButton->GetContent()),LDTerms::ShopSkillPoint(Shop.SkillPointPrice,Shop.SkillPointsBought,Shop.SkillPointsMax));
    GrowthEnabled(ShopPointButton,bCan&&Points>=Shop.SkillPointPrice&&Shop.SkillPointsBought<Shop.SkillPointsMax);
    GrowthShown(ShopSlotRow,true,ESlateVisibility::SelfHitTestInvisible);
    for(int32 Index=0;Index<ShopCards.Num();++Index)
    {
        FTrayCard& Card=ShopCards[Index];
        const bool bHas=Shop.Slots.IsValidIndex(Index);
        GrowthShown(ShopCells[Index],bHas,ESlateVisibility::SelfHitTestInvisible);
        if(!bHas){continue;}
        const FLDShopSlotView& Row=Shop.Slots[Index];
        Card.Component=Row.DefinitionId;
        const bool bAfford=Points>=Row.Price;
        FString Outcome=Row.bSold?FString(LDTerms::ShopSoldMark):!Gate.bCanShop?Gate.ShopBlocked
            :bAfford?LDTerms::ShopPrice(Row.Price):LDHUD::GrowthReasonText(ELDGrowthReason::NotEnoughPoints);
        const bool bNoHost=Row.Kind==ELDShopItemKind::Card&&LatestContent
            &&!LDProgression::HasValidSkillHost(*LatestContent,LDProgression::BuildFromView(*LatestContent,Latest),Row.DefinitionId);
        if(bNoHost&&!Row.bSold){Outcome+=TEXT(" · 需替换对应宿主技能后生效");}
        bool bCompatMatch=false,bCompatFull=false;
        const FString Compat=Row.Kind==ELDShopItemKind::Skill?FString()
            :LDHUD::SkillCardCompatStrip(Row.DefinitionId,Latest,LatestContent,SlotKeys,bCompatMatch,bCompatFull);
        const FString Key=FString::Printf(TEXT("%llu|%d|"),Shop.Revision,Index)+Row.DefinitionId.ToString()+TEXT("|")+Outcome+TEXT("|")+Compat;
        if(CardStale(Card,Key))
        {
            if(Row.Kind==ELDShopItemKind::Skill)
            {
                const LDHUD::FActiveCardView View=LDHUD::ActiveCard(Row.DefinitionId,LatestContent,LatestStats);
                ShowFace(Card,Key,View.Name,View.IconId,View.Glyph,LDHUD::ActiveChoiceDetail(View,true,Outcome),View.SentencePlain);
            }
            else
            {
                const LDHUD::FCardFaceView View=LDHUD::CardFace(Row.DefinitionId,LatestContent,LatestStats);
                const FString ExtraCompat=(Compat.IsEmpty()||(bNoHost&&!Row.bSold))?Outcome:(Outcome+TEXT("\n")+Compat);
                ShowFace(Card,Key,View.Name,View.IconId,View.Glyph,LDHUD::CardChoiceDetail(View,ExtraCompat),View.SentencePlain);
            }
        }
        if(Row.Kind==ELDShopItemKind::Skill)
        {
            Card.bElementStone=false;
            GrowthCardLook(Card.Button,LDUIPaint::EButton::CardActive,LDHUDStyle::RewardActiveRim);
            if(Card.KindBanner&&Card.KindBannerText)
            {
                GrowthText(Card.KindBannerText,TEXT("主动技能"));
                Card.KindBannerText->SetColorAndOpacity(LDHUDStyle::RewardActiveRim);
                GrowthShown(Card.KindBanner,true,ESlateVisibility::HitTestInvisible);
            }
        }
        else
        {
            const LDWorkshop::FCardFacts Facts=LDWorkshop::CardFacts(Row.DefinitionId,LatestContent);
            const bool bStone=Facts.Form==ELDCardForm::Medal||Row.DefinitionId.ToString().StartsWith(TEXT("medal_"));
            Card.bElementStone=bStone;
            if(bStone)
            {
                GrowthCardLook(Card.Button,LDUIPaint::EButton::CardStone,ElementStoneRingColor(Row.DefinitionId));
                if(Card.Shape)
                {
                    Card.Shape->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White,32.f,LDHUDStyle::RewardStoneRim,2.2f));
                    Card.Shape->SetBrushColor(LDHUDStyle::RewardStoneBase);
                }
                if(Card.KindBanner&&Card.KindBannerText)
                {
                    GrowthText(Card.KindBannerText,TEXT("元素石"));
                    Card.KindBannerText->SetColorAndOpacity(LDHUDStyle::RewardStoneRim);
                    GrowthShown(Card.KindBanner,true,ESlateVisibility::HitTestInvisible);
                }
            }
            else
            {
                const FLinearColor RimColor=Facts.Type==ELDCardType::Convert?LDHUDStyle::RewardCardRare
                    :Facts.Type==ELDCardType::Grow?LDHUDStyle::RewardCardLegend:LDHUDStyle::RewardCardRim;
                GrowthCardLook(Card.Button,LDUIPaint::EButton::CardBuild,RimColor);
                if(Card.Shape)
                {
                    const FLinearColor KindEdge=LDUIShapes::KindColor(uint8(Facts.Type));
                    Card.Shape->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White,TrayIconRadius,KindEdge,1.5f));
                    Card.Shape->SetBrushColor(LDHUDStyle::ShapeFill);
                }
                if(Card.KindBanner&&Card.KindBannerText)
                {
                    const TCHAR* FormTag=Facts.Form==ELDCardForm::Skill?TEXT("构筑牌·技能")
                        :Facts.Form==ELDCardForm::Dash?TEXT("构筑牌·冲刺"):TEXT("构筑牌·身体");
                    GrowthText(Card.KindBannerText,FormTag);
                    Card.KindBannerText->SetColorAndOpacity(RimColor);
                    GrowthShown(Card.KindBanner,true,ESlateVisibility::HitTestInvisible);
                }
            }
        }
        ShowLook(Card,false,bCan&&!Row.bSold&&bAfford,true);
        if(Row.Kind==ELDShopItemKind::Card&&LatestContent)
        {
            const auto Rarity=LDProgression::CardRarity(*LatestContent,Row.DefinitionId);
            const FLinearColor Tint=bNoHost?FLinearColor(.52f,.34f,.08f,1.f)
                :Rarity==LDProgression::ELDCardRarity::Legendary?FLinearColor(.55f,.42f,.08f,1.f)
                :Rarity==LDProgression::ELDCardRarity::Rare?FLinearColor(.36f,.18f,.55f,1.f)
                :FLinearColor(.16f,.26f,.38f,1.f);
            GrowthTint(Card.Button,Tint);
        }
        else
        {
            GrowthTint(Card.Button,LDHUDStyle::CardIdle);
        }
        GrowthText(ShopPrices[Index],Row.bSold?FString(LDTerms::ShopSoldMark):LDTerms::ShopPrice(Row.Price));
        ShopPrices[Index]->SetColorAndOpacity(Row.bSold||!bAfford?LDHUDStyle::TextSecondary:LDHUDStyle::TextAccent);
    }
}
bool ULDChoicesPanel::ShopCommand(ELDBuildCommandKind Kind,FLDBuildCommand& Out)
{
    if(!IsShopShown()){return false;}
    if(PendingRequest){Tell(TEXT("上一个请求仍在处理中。"));return false;}
    if(!Gate.bCanShop){Tell(TEXT("没有提交：")+Gate.ShopBlocked);return false;}
    Out=FLDBuildCommand();Out.Kind=Kind;Out.OfferRevision=Latest.Shop.Revision;
    return true;
}
bool ULDChoicesPanel::BuyShopSlot(int32 Index)
{
    FLDBuildCommand Command;
    if(!Latest.Shop.Slots.IsValidIndex(Index)||!ShopCommand(ELDBuildCommandKind::ShopBuy,Command)){return false;}
    const FLDShopSlotView& Row=Latest.Shop.Slots[Index];
    if(Row.bSold){Tell(TEXT("没有提交：")+LDHUD::GrowthReasonText(ELDGrowthReason::ShopSoldOut));return false;}
    if(Latest.UnspentPoints<Row.Price){Tell(TEXT("没有提交：")+LDHUD::GrowthReasonText(ELDGrowthReason::NotEnoughPoints));return false;}
    Command.Slot=uint8(Index);
    LDWidgets::PlayCue(this,ELDAudioCue::UIChoose);
    return Send(Command);
}
bool ULDChoicesPanel::BuyShopSkillPoint()
{
    FLDBuildCommand Command;
    if(!ShopCommand(ELDBuildCommandKind::ShopBuy,Command)){return false;}
    const FLDShopView& Shop=Latest.Shop;
    if(Shop.SkillPointsBought>=Shop.SkillPointsMax){Tell(TEXT("没有提交：")+LDHUD::GrowthReasonText(ELDGrowthReason::SkillPointLimit));return false;}
    if(Latest.UnspentPoints<Shop.SkillPointPrice){Tell(TEXT("没有提交：")+LDHUD::GrowthReasonText(ELDGrowthReason::NotEnoughPoints));return false;}
    Command.Slot=FLDBuildCommand::ShopSkillPointSlot;
    return Send(Command);
}
bool ULDChoicesPanel::RefreshShopShelf()
{
    FLDBuildCommand Command;
    if(!ShopCommand(ELDBuildCommandKind::ShopRefresh,Command)){return false;}
    if(Latest.UnspentPoints<Latest.Shop.RefreshPrice){Tell(TEXT("没有提交：")+LDHUD::GrowthReasonText(ELDGrowthReason::NotEnoughPoints));return false;}
    return Send(Command);
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
    if((Command.Kind==ELDBuildCommandKind::ActiveChoice||Command.Kind==ELDBuildCommandKind::SkipActiveOffer)
        &&OfferMotion.Ticket.Offer==Command.OfferId){OfferMotion.Ticket.Request=Expected;}
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
    if(Answers==Before)
    {
        PendingRequest=Request;
        if((Command.Kind==ELDBuildCommandKind::ActiveChoice||Command.Kind==ELDBuildCommandKind::SkipActiveOffer)
            &&OfferMotion.Ticket.Offer==Command.OfferId){OfferMotion.Ticket.Request=Request;}
    }
    RefreshAll();
    return true;
}
void ULDChoicesPanel::FinishRequest()
{
    PendingRequest=0;
    SubmittedFinal=FLDFinalModSelection();bFinalFeedbackAllowed=false;
    if(OfferMotion.StartedAt<0.){OfferMotion=FOfferMotion();}
    RefreshAll();
    // Every result reaches the player as a toast, whether the tray is open or not.
    Tell(ResultLine);ResultLine.Reset();
}
void ULDChoicesPanel::ShowFinal(const FLDBuildReceipt& Receipt)
{
    ResultLine=LDHUD::BuildReceiptText(Receipt,LatestContent,SlotKeys);
    // The moments the receipt confirms get their cue: an active chosen, the final mod locked, a refusal.
    if(Receipt.Header.Outcome==ELDReceiptOutcome::Applied)
    {
        if(Receipt.Kind==ELDBuildCommandKind::ActiveChoice||Receipt.Kind==ELDBuildCommandKind::SkipActiveOffer)
        {
            if(Receipt.Kind==ELDBuildCommandKind::ActiveChoice){LDWidgets::PlayCue(this,ELDAudioCue::UIChoose);}
            if(OfferMotion.Ticket.Matches(Receipt,LatestMatch,LatestLife))
            {
                ConfirmedOffer=Receipt.OfferId;ConfirmedMatch=LatestMatch;ConfirmedLife=LatestLife;
                if(bOpen&&GrowthHasArea(OfferMotion.Chosen.Icon))
                {
                    if(Receipt.Kind==ELDBuildCommandKind::SkipActiveOffer)
                    {OfferMotion.Chosen.Glyph=FString::Printf(TEXT("+%lld"),Receipt.PointsDelta);}
                    OfferMotion.Result=Receipt.ChoiceResult;OfferMotion.Slot=Receipt.LearnedSlot;OfferMotion.StartedAt=UiNow();
                }
            }
            else if(OfferMotion.Ticket.Offer.IsValid())
            {UE_LOG(LogLDUI,Warning,TEXT("LD_UI_CHOICE_RECEIPT_MISMATCH Receipt does not match the submitted offer and life."));}
        }
        else if(Receipt.Kind==ELDBuildCommandKind::FinalMod)
        {
            if(LDHUD::AppliedFinalFeedback(Receipt,LatestMatch,LatestLife,PendingRequest,SubmittedFinal.DefinitionId))
            {
                LDWidgets::PlayCue(this,ELDAudioCue::UIFinalLock);
                if(bFinalFeedbackAllowed&&FinalModApplied){FinalModApplied(Receipt,SubmittedFinal);}
            }
            else{UE_LOG(LogLDUI,Warning,TEXT("LD_UI_FINAL_RECEIPT_MISMATCH Receipt does not match the submitted final mod and life."));}
        }
    }
    else{LDWidgets::PlayCue(this,ELDAudioCue::UIReject);}
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

void ULDChoicesPanel::ShowBig(FTrayCard& Card,bool bBig,float Width,float Icon)
{
    if(!Card.Frame){return;}
    if(Card.bBig==bBig&&(!bBig||FMath::IsNearlyEqual(Card.Frame->GetWidthOverride(),Width))){return;}
    Card.bBig=bBig;Card.bTile=false;Card.AppliedEnabled=-1;
    LDTween::SetOpacity(Card.Frame,1.f);
    const float Scale=AppliedTextScale>0.f?AppliedTextScale:1.f;
    Card.Frame->SetWidthOverride(bBig?Width:TrayCardWidth);
    if(bBig){Card.Frame->ClearHeightOverride();}
    const float Face=bBig?Icon:TrayCardIcon;
    if(Face>0.f){Card.IconFrame->SetWidthOverride(Face);Card.IconFrame->SetHeightOverride(Face);}
    Card.Name->SetFont(LDUIFonts::Font(bBig?LDUIFonts::ERole::Title:LDUIFonts::ERole::Body,FMath::RoundToFloat((bBig?LDHUDStyle::TypeSection:LDHUDStyle::TypeBody)*Scale)));
    Card.Name->SetWrapTextAt(bBig?Width-16.f:TrayCardWidth-2.f*TrayCardPadding);
    if(!bBig){GrowthShown(Card.IconFrame,true);}
    auto Style=Card.Button->GetStyle();
    const FMargin Inner=bBig?FMargin(4,10):FMargin(TrayCardPadding);Style.NormalPadding=Inner;Style.PressedPadding=Inner;Card.Button->SetStyle(Style);
    // Stage cards stretch to one height: a face with an icon hangs from the top so the icons and names of the row line up,
    // whatever extra lines one card carries; the icon-less 不选 card stays centred.
    if(UButtonSlot* FaceSlot=Cast<UButtonSlot>(Card.Button->GetContentSlot())){FaceSlot->SetVerticalAlignment(bBig&&Icon>0.f?VAlign_Top:VAlign_Center);}
}
void ULDChoicesPanel::UpdateStage()
{
    const bool bOffer=HasAvailableOffer();
    const bool bFinal=HasAvailableFinal();
    const bool bFinalNext=!bOffer&&bFinal;
    const FGuid Key=bOffer?Latest.ActiveOffer.OfferId:bFinal?Latest.FinalModOfferId:FGuid();
    const bool bShow=DrawerState(Places.bActive).bStage;
    // A newly available choice takes priority over the previous choice's cosmetic flight.
    if(bShow&&OfferMotionActive()){OfferMotion.StartedAt=-1.;OfferMotion.Others.Reset();}
    if(bShow==bStageShown&&(!bShow||(bFinalNext==bStageFinal&&Key==StageShownFor))){return;}
    StageShownFor=bShow?Key:FGuid();
    // Every card goes back to its tray row first, in order; the stage then takes the ones it shows.
    const auto Move=[](UWidget* Widget,UPanelWidget* To){if(Widget&&To&&Widget->GetParent()!=To){Widget->RemoveFromParent();To->AddChild(Widget);}};
    for(FTrayCard& Card:OfferCards){Move(Card.Frame,OfferRow);ShowBig(Card,false,0.f,0.f);}
    Move(SkipCard.Frame,OfferRow);ShowBig(SkipCard,false,0.f,0.f);
    for(FTrayCard& Card:FinalCards){Move(Card.Frame,FinalModRow);ShowBig(Card,false,0.f,0.f);}
    bStageShown=bShow;bStageFinal=bFinalNext;
    if(!bShow){return;}
    StageOpenedAt=UiNow();
    LDWidgets::PlayCue(this,ELDAudioCue::UIOfferDeal);
    const auto OnStage=[this,&Move](FTrayCard& Card,UHorizontalBox* Row,float Gap,float Width,float Icon)
    {
        Move(Card.Frame,Row);
        if(UHorizontalBoxSlot* CardSlot=Cast<UHorizontalBoxSlot>(Card.Frame->Slot)){CardSlot->SetPadding(FMargin(Row->GetChildrenCount()>1?Gap:0.f,0,0,0));CardSlot->SetVerticalAlignment(VAlign_Fill);}
        ShowBig(Card,true,Width,Icon);
    };
    if(!bStageFinal)
    {
        for(FTrayCard& Card:OfferCards){OnStage(Card,StageCards,StageOfferGap,StageOfferWidth,StageOfferIcon);}
        OnStage(SkipCard,StageCards,StageOfferGap,StageSkipWidth,0.f);
        // The first card is in focus without a hover, so the dock reads something at once.
        if(PinnedPreview==INDEX_NONE){PinnedPreview=0;}
    }
    else
    {
        const bool bFourth=Latest.FinalModOptions.Num()==4;
        for(int32 Index=0;Index<FinalCards.Num()&&Latest.FinalModOptions.IsValidIndex(Index);++Index)
        {OnStage(FinalCards[Index],StageCards,StageFinalGap,bFourth?108.f:StageFinalWidth,StageFinalIcon);}
    }
    bCardsStale=true;
}
void ULDChoicesPanel::RefreshStage()
{
    GrowthShown(TrayFrame,(bOpen&&!bLibraryOnly&&!bStageShown)||bQuickShown);
    GrowthShown(StageBox,bStageShown);
    GrowthShown(StageBack,bStageShown,ESlateVisibility::Visible);
    const bool bFinalStage=IsFinalStage();
    GrowthShown(FinalDesc,bFinalStage);GrowthShown(FinalLockButton,bFinalStage,ESlateVisibility::Visible);GrowthShown(FinalLockNote,bFinalStage);
    if(!bStageShown){return;}
    if(!bStageFinal)
    {
        GrowthText(StageTitle,LDTerms::OfferStageTitle);StageTitle->SetColorAndOpacity(LDHUDStyle::UIText);
        // Queued opportunities show as dots after the current one: the next group deals in when this one is chosen.
        const FString Queue=LDHUD::OfferQueueDots(Latest.QueuedActiveOpportunities);
        const FString Dots=Queue.IsEmpty()?FString():TEXT("    ")+Queue;
        GrowthText(StageSub,FString::Printf(TEXT("Lv %d    %s%s"),int32(Latest.Level),*LDTerms::OfferChanceOf(Latest.ActiveOffer.OpportunityOrdinal,Latest.ActiveOpportunitiesEarned),*Dots));
        StageSub->SetColorAndOpacity(LDHUDStyle::TextSecondary);
        return;
    }
    GrowthText(StageTitle,LDTerms::FinalStageTitle);StageTitle->SetColorAndOpacity(LDHUDStyle::UIEvent);
    GrowthText(StageSub,LDTerms::FinalOnce);StageSub->SetColorAndOpacity(LDHUDStyle::UIEvent);
    // The selected card (else the hovered, else the first): its one rule sentence, then its numbers line.
    FName Shown=!PendingFinalOption.IsNone()?PendingFinalOption:Latest.FinalModOptions.IsValidIndex(0)?Latest.FinalModOptions[0]:NAME_None;
    if(PendingFinalOption.IsNone()&&FSlateApplication::IsInitialized())
    {
        const FVector2D Pointer=FSlateApplication::Get().GetCursorPos();
        for(int32 Index=0;Index<FinalCards.Num()&&Latest.FinalModOptions.IsValidIndex(Index);++Index)
        {
            const UWidget* Card=FinalCards[Index].Frame;
            if(Card&&Card->IsVisible()&&Card->GetCachedGeometry().IsUnderLocation(Pointer))
            {Shown=Latest.FinalModOptions[Index];break;}
        }
    }
    FString Description;
    LDHUD::ContentText(LatestContent,Shown.ToString(),true,Description);
    FString Numbers=LDHUD::FinalModCard(Shown,LatestContent,LatestStats).Detail.Body;
    Numbers.RemoveFromStart(Description);Numbers.TrimStartAndEndInline();
    GrowthText(FinalDescText,Description);
    GrowthText(FinalNumbersText,Numbers);
    GrowthShown(FinalNumbersText,!Numbers.IsEmpty());
    GrowthText(FinalLockNote,LDTerms::FinalTakenNote);
    GrowthEnabled(FinalLockButton,!PendingFinalOption.IsNone()&&Gate.bCanFinalMod&&!PendingRequest);
}
namespace
{
const FGeometry& EffectiveChoicesGeometry(const UWidget& Widget)
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
FVector2D ChoicesAbsoluteToLayoutLocal(const FGeometry& Geometry,const FVector2D& AbsolutePoint)
{
    return TransformPoint(Inverse(Geometry.GetAccumulatedLayoutTransform()),AbsolutePoint);
}
}
void ULDChoicesPanel::PlaceOnLayer(UWidget* Widget,const FSlateRect& Absolute,FVector2D Alignment,bool bFill)
{
    UCanvasPanelSlot* const* Found=LayerSlots.Find(Widget);
    if(!Found||!*Found||Absolute.Right<=Absolute.Left||Absolute.Bottom<=Absolute.Top){return;}
    const FGeometry& Geometry=EffectiveChoicesGeometry(*this);
    const FVector2D Min=ChoicesAbsoluteToLayoutLocal(Geometry,FVector2D(Absolute.Left,Absolute.Top));
    const FVector2D Max=ChoicesAbsoluteToLayoutLocal(Geometry,FVector2D(Absolute.Right,Absolute.Bottom));
    UCanvasPanelSlot* ItemSlot=*Found;
    if(bFill)
    {
        if(ItemSlot->GetAutoSize()){ItemSlot->SetAutoSize(false);}
        if(!ItemSlot->GetAlignment().IsZero()){ItemSlot->SetAlignment(FVector2D::ZeroVector);}
        if(!ItemSlot->GetPosition().Equals(Min,.5f)){ItemSlot->SetPosition(Min);}
        if(!ItemSlot->GetSize().Equals(Max-Min,.5f)){ItemSlot->SetSize(Max-Min);}
        return;
    }
    const FVector2D At(FMath::RoundToFloat(float(FMath::Lerp(Min.X,Max.X,Alignment.X))),FMath::RoundToFloat(float(FMath::Lerp(Min.Y,Max.Y,Alignment.Y))));
    if(!ItemSlot->GetAlignment().Equals(Alignment)){ItemSlot->SetAlignment(Alignment);}
    if(!ItemSlot->GetPosition().Equals(At,.5f)){ItemSlot->SetPosition(At);}
}
void ULDChoicesPanel::LayoutLayer()
{
    if(!Layer||!Places.bActive){return;}
    if(bStageShown)
    {
        // Bottom-left of the stage, clear of the phase bar above and the rest actions in the top-right corner.
        const FSlateRect& Stage=Places.Stage;
        const FGeometry& Geometry=EffectiveChoicesGeometry(*this);
        const float Scale=FMath::Max(.01f,Geometry.Scale);
        PlaceOnLayer(StageBox,FSlateRect(Stage.Left,Stage.Top,Stage.Right,Stage.Bottom-44.f*Scale),FVector2D::ZeroVector,true);
        PlaceOnLayer(StageBack,FSlateRect(Stage.Left+16.f*Scale,Stage.Top,Stage.Right,Stage.Bottom-16.f*Scale),FVector2D(0.f,1.f));
        const FSlateRect& Dock=Places.Dock;
        const FSlateRect Description(Dock.Left+28.f*Scale,Dock.Top+16.f*Scale,Dock.Right-340.f*Scale,Dock.Bottom-16.f*Scale);
        const float DescriptionWidth=FMath::Max(1.f,float(ChoicesAbsoluteToLayoutLocal(Geometry,FVector2D(Description.Right,Description.Top)).X
            -ChoicesAbsoluteToLayoutLocal(Geometry,FVector2D(Description.Left,Description.Top)).X));
        for(UTextBlock* Text:{FinalDescText.Get(),FinalNumbersText.Get()})
        {if(Text&&!FMath::IsNearlyEqual(Text->GetWrapTextAt(),DescriptionWidth,.5f)){Text->SetWrapTextAt(DescriptionWidth);}}
        PlaceOnLayer(FinalDesc,Description,FVector2D::ZeroVector,true);
        const FSlateRect Actions(Dock.Right-320.f*Scale,Dock.Top,Dock.Right-28.f*Scale,Dock.Bottom);
        PlaceOnLayer(FinalLockButton,Actions,FVector2D(.5f,.4f));
        PlaceOnLayer(FinalLockNote,Actions,FVector2D(.5f,.78f));
    }
    PlaceOnLayer(DrawerTabs,Places.DrawerHeader,FVector2D(0.f,.5f));
    if(IsShopShown()){PlaceOnLayer(ShopPanel,Places.Shop,FVector2D::ZeroVector,true);}
    if(IsServiceShown()){PlaceOnLayer(RespecBox,Places.CharacterFoot,FVector2D::ZeroVector,true);}
    if(IsServiceShown()||IsBlueprintSwapShown()){PlaceOnLayer(ServiceDock,Places.Dock,FVector2D::ZeroVector,true);}
    if(!IsBlueprintSwapShown()){return;}
    PlaceOnLayer(TrainerPicks,Places.PendingTop,FVector2D(0.f,0.f));
    for(uint8 Key=0;Key<3;++Key)
    {
        if(BlueprintRows.IsValidIndex(TrainerBlueprint))
        {
            // Mockup 05: the pill hangs on the bottom edge of the skill icon the blueprint lands on, clear of the key chip.
            const FSlateRect& Icon=Places.HeadIcons[int32(LDHUD::SlotColumn(Key))];
            const FSlateRect Foot(Icon.Left,Icon.Bottom-2.f,Icon.Right,Icon.Bottom-2.f+1.f);
            PlaceOnLayer(BlueprintRows[TrainerBlueprint].Learn[Key],Foot,FVector2D(.5f,.5f));
            PlaceOnLayer(BlueprintRows[TrainerBlueprint].Replace[Key],Foot,FVector2D(.5f,.5f));
        }
    }
}
void ULDChoicesPanel::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);
    // The stage cards turn in one after another (LDHUDStyle::MotionOfferStagger apart) and land with the back curve.
    if(!bStageShown||!StageCards){return;}
    const double Age=UiNow()-StageOpenedAt;
    for(int32 Index=0;Index<StageCards->GetChildrenCount();++Index)
    {
        UWidget* Card=StageCards->GetChildAt(Index);
        const float Progress=LDTween::Progress(Age-Index*LDHUDStyle::MotionOfferStagger,LDHUDStyle::MotionOfferCards,LDTween::ECurve::Back);
        LDTween::SetOpacity(Card,FMath::Lerp(.25f,1.f,FMath::Clamp(Progress,0.f,1.f)));
    }
}
void ULDChoicesPanel::PaintOfferMotion(const LDUIPaint::FContext& Paint) const
{
    const double Age=UiNow()-OfferMotion.StartedAt;
    const float Fade=1.f-LDTween::Progress(Age,LDHUDStyle::MotionChoose,LDTween::ECurve::EaseIn);
    const float Slide=32.f*LDTween::Progress(Age,LDHUDStyle::MotionChoose,LDTween::ECurve::EaseIn);
    FSlateRenderer* Renderer=FSlateApplication::IsInitialized()?FSlateApplication::Get().GetRenderer():nullptr;
    const auto Art=[&](const LDUIPaint::FContext& Ink,int32 Layer,const FBox2D& Box,const FOfferGhost& Ghost)
    {
        UTexture2D* Texture=Ghost.Texture.Get();
        Ink.SkillArt(Layer,Box,false,false,Texture);
        if(Texture||Ghost.Glyph.IsEmpty()||!Renderer){return;}
        const FSlateFontInfo Font=LDUIFonts::Font(LDUIFonts::ERole::Title,FMath::Max(12.f,float(Box.GetSize().Y)*.45f));
        const FVector2D Extent=Renderer->GetFontMeasureService()->Measure(Ghost.Glyph,Font);
        const FVector2D At=Box.GetCenter()-Extent*.5f;
        FSlateDrawElement::MakeText(Ink.Elements,Layer+4,Ink.Geometry.ToPaintGeometry(Extent,FSlateLayoutTransform(At)),
            Ghost.Glyph,Font,ESlateDrawEffect::None,Ink.Fade(LDHUDStyle::UIText));
    };
    for(const FOfferGhost& Ghost:OfferMotion.Others)
    {
        if(!GrowthHasArea(Ghost.Frame)){continue;}
        const LDUIPaint::FContext Ink=Paint.WithOpacity(Fade);
        FBox2D Frame=LDUIShapes::AbsoluteIn(Paint.Geometry,Ghost.Frame);
        Frame.Min.Y+=Slide;Frame.Max.Y+=Slide;
        Ink.Panel(Paint.Layer,Frame,false);
        if(GrowthHasArea(Ghost.Icon))
        {
            FBox2D Icon=LDUIShapes::AbsoluteIn(Paint.Geometry,Ghost.Icon);
            Icon.Min.Y+=Slide;Icon.Max.Y+=Slide;
            Art(Ink,Paint.Layer+2,Icon,Ghost);
        }
        if(Renderer&&!Ghost.Name.IsEmpty()&&GrowthHasArea(Ghost.NameBounds))
        {
            FBox2D Name=LDUIShapes::AbsoluteIn(Paint.Geometry,Ghost.NameBounds);
            Name.Min.Y+=Slide;Name.Max.Y+=Slide;
            const FSlateFontInfo Font=LDUIFonts::Font(LDUIFonts::ERole::Title,FMath::Max(1.f,Ghost.NameSize));
            const FVector2D Extent=Renderer->GetFontMeasureService()->Measure(Ghost.Name,Font);
            if(Extent.X>0.f&&Extent.Y>0.f)
            {
                const float Scale=FMath::Min(1.f,float(Name.GetSize().X/Extent.X));
                FSlateDrawElement::MakeText(Ink.Elements,Paint.Layer+6,Paint.Geometry.ToPaintGeometry(Extent,FSlateLayoutTransform(Scale,Name.Min)),
                    Ghost.Name,Font,ESlateDrawEffect::None,Ink.Fade(LDHUDStyle::UIText));
            }
        }
    }
    FSlateRect Destination;
    const bool bSkip=OfferMotion.Ticket.Kind==ELDBuildCommandKind::SkipActiveOffer;
    if(bSkip){Destination=Places.Points;}
    else if(OfferMotion.Result==ELDBuildChoiceResult::Learned)
    {Destination=Places.HeadIcons[int32(LDHUD::SlotColumn(OfferMotion.Slot))];}
    else if(OfferMotion.Result==ELDBuildChoiceResult::StoredAsBlueprint){Destination=Places.PendingTop;}
    const FBox2D Origin=LDUIShapes::AbsoluteIn(Paint.Geometry,OfferMotion.Chosen.Icon);
    FVector2D At=Origin.GetCenter();
    float Size=float(FMath::Min(Origin.GetSize().X,Origin.GetSize().Y));
    FBox2D Target(ForceInit);
    const float T=LDTween::Progress(Age,LDHUDStyle::MotionChoose*.85,LDTween::ECurve::EaseOut);
    if(GrowthHasArea(Destination))
    {
        Target=LDUIShapes::AbsoluteIn(Paint.Geometry,Destination);
        const FVector2D End=Target.GetCenter();
        const FVector2D Control((At.X+End.X)*.5f,FMath::Min(At.Y,End.Y)-90.f);
        At=FMath::Lerp(FMath::Lerp(At,Control,T),FMath::Lerp(Control,End,T),T);
        const float EndSize=OfferMotion.Result==ELDBuildChoiceResult::Learned
            ?float(FMath::Min(Target.GetSize().X,Target.GetSize().Y)):FMath::Min(56.f,float(Target.GetSize().Y));
        Size=FMath::Lerp(Size,EndSize,T);
    }
    const FVector2D Half(Size*.5f,Size*.5f);
    const FBox2D Chosen(At-Half,At+Half);
    if(bSkip&&GrowthHasArea(Destination)&&Renderer)
    {
        Paint.Shape(Paint.Layer+3,LDUIShapes::EShape::Circle,Chosen,Size*.5f,
            LDUIPaint::FFill::Solid(LDHUDStyle::UIAction.CopyWithNewOpacity(.95f)));
        const FSlateFontInfo Font=LDUIFonts::Font(LDUIFonts::ERole::Number,FMath::Max(12.f,Size*.4f));
        const FVector2D Extent=Renderer->GetFontMeasureService()->Measure(OfferMotion.Chosen.Glyph,Font);
        const float Scale=Extent.X>0.f?FMath::Min(1.f,Size*.85f/float(Extent.X)):1.f;
        const FVector2D TextAt=At-Extent*Scale*.5f;
        FSlateDrawElement::MakeText(Paint.Elements,Paint.Layer+4,Paint.Geometry.ToPaintGeometry(Extent,FSlateLayoutTransform(Scale,TextAt)),
            OfferMotion.Chosen.Glyph,Font,ESlateDrawEffect::None,Paint.Fade(LDHUDStyle::UIBase));
    }
    else if(!bSkip){Art(Paint,Paint.Layer+3,Chosen,OfferMotion.Chosen);}
    const float Phase=FMath::Clamp(float(Age/LDHUDStyle::MotionChoose),0.f,1.f);
    if(GrowthHasArea(Destination)&&Phase>.65f)
    {
        const float Flash=FMath::Sin((Phase-.65f)/.35f*PI);
        Paint.Glow(Paint.Layer+7,LDUIShapes::EShape::Rounded,Target,LDHUDStyle::UIAction.CopyWithNewOpacity(.8f*Flash),18.f,10.f);
        Paint.Frame(Paint.Layer+7,LDUIShapes::EShape::Rounded,Target,LDHUDStyle::UIAction.CopyWithNewOpacity(Flash),2.f,10.f);
    }
}
int32 ULDChoicesPanel::NativePaint(const FPaintArgs& A,const FGeometry& G,const FSlateRect& C,FSlateWindowElementList& E,int32 L,const FWidgetStyle& S,bool Enabled) const
{
    const int32 Base=Super::NativePaint(A,G,C,E,L,S,Enabled);
    const LDUIPaint::FContext Paint(E,Base+1,G,S.GetColorAndOpacityTint().A);
    if(OfferMotionActive()){PaintOfferMotion(Paint);}
    if(!bStageShown){return OfferMotionActive()?Base+9:Base;}
    // .ocard .art img: a big card's picture sits in a bright rounded frame.
    for(const TArray<FTrayCard>* Group:{&OfferCards,&FinalCards})
    {
        for(const FTrayCard& Card:*Group)
        {
            if(!Card.bBig||!Card.Frame||!Card.Frame->IsVisible()||!Card.IconFrame||!Card.IconFrame->IsVisible()){continue;}
            const FGeometry& Icon=Card.IconFrame->GetCachedGeometry();
            const FBox2D Box(FVector2D::ZeroVector,Icon.GetLocalSize());
            Paint.WithGeometry(Icon).WithOpacity(Card.Frame->GetRenderOpacity()).Edge(Base+1,LDUIShapes::EShape::Rounded,
                LDUIPaint::Grow(Box,2.f),12.f,FLinearColor(1,1,1,.85f),3.f);
        }
    }
    // The focused offer card wears the action yellow; the selected final card the event pink.
    const auto Edge=[&](const FTrayCard& Card,const FLinearColor& Color)
    {
        if(!Card.Frame||!Card.Frame->IsVisible()){return;}
        const FBox2D Box=LDUIShapes::RectIn(G,Card.Frame->GetCachedGeometry());
        const LDUIPaint::FContext CardPaint=Paint.WithOpacity(Card.Frame->GetRenderOpacity());
        CardPaint.Glow(Base+1,LDUIShapes::EShape::Rounded,Box,Color,10.f,LDHUDStyle::RadiusPanel);
        CardPaint.Frame(Base+1,LDUIShapes::EShape::Rounded,Box,Color,2.f,LDHUDStyle::RadiusPanel);
    };
    if(!bStageFinal)
    {
        const FVector2D Pointer=FSlateApplication::IsInitialized()?FSlateApplication::Get().GetCursorPos():FVector2D::ZeroVector;
        int32 Focus=PinnedPreview;
        for(int32 Index=0;Index<OfferCards.Num();++Index)
        {if(OfferCards[Index].Frame&&OfferCards[Index].Frame->IsVisible()&&OfferCards[Index].Frame->GetCachedGeometry().IsUnderLocation(Pointer)){Focus=Index;}}
        if(OfferCards.IsValidIndex(Focus)){Edge(OfferCards[Focus],LDHUDStyle::UIAction);}
    }
    else
    {
        for(int32 Index=0;Index<FinalCards.Num()&&Latest.FinalModOptions.IsValidIndex(Index);++Index)
        {if(Latest.FinalModOptions[Index]==PendingFinalOption){Edge(FinalCards[Index],LDHUDStyle::UIEvent);}}
    }
    return OfferMotionActive()?Base+9:Base+2;
}
