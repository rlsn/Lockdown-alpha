#include "UI/LDChoicesPanel.h"
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
constexpr float TrayWidth=4*TrayCardWidth+3*TrayCardGap;
constexpr float TrayPagerHeight=32.f,TrayMinIconPixels=36.f;
static_assert(8*TrayTileSize+7*TrayTileGap<=TrayWidth,"eight grid tiles fit on one tray line");
static_assert(TrayTileIcon+2*TrayCardPadding<=TrayTileSize,"a grid tile holds its icon");
constexpr float ServicePanelWidth=540.f;
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
    return bTile?Icon+2.f*TrayCardPadding:Icon+2.f*TrayCardPadding+4.f+FMath::CeilToFloat(LDHUDStyle::TypeBody*TextScale*2.7f);
}
// #198 G dismantle selection buttons per line (their labels may carry the installed warning).
// A card's painted face (LDUIPaint::EButton::Card): idle, or chosen in its state's colour at full strength.
void GrowthTint(ULDCommandButton* Button,const FLinearColor& Color)
{
    if(!Button){return;}
    if(Button->Look!=LDUIPaint::EButton::Card)
    {
        Button->Look=LDUIPaint::EButton::Card;
        auto Style=Button->GetStyle();Style.Normal=FSlateNoResource();Style.Hovered=FSlateNoResource();Style.Pressed=FSlateNoResource();Style.Disabled=FSlateNoResource();
        Button->SetStyle(Style);
    }
    const float Peak=FMath::Max3(Color.R,Color.G,Color.B);
    const FLinearColor Mark=Color.Equals(LDHUDStyle::CardIdle)||Peak<=0.f?FLinearColor::Transparent:FLinearColor(Color.R/Peak,Color.G/Peak,Color.B/Peak,1.f);
    if(!Button->Tint.Equals(Mark)){Button->Tint=Mark;if(TSharedPtr<SWidget> Cached=Button->GetCachedWidget()){Cached->Invalidate(EInvalidateWidgetReason::Paint);}}
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
            ChoiceRowList=WidgetTree->ConstructWidget<UVerticalBox>();ChoiceRowList->SetVisibility(ESlateVisibility::SelfHitTestInvisible);AddRow(ChoiceRowList);
            BaseRow=TrayRow(WidgetTree);AddRow(BaseRow);
            auto* OfferLine=WidgetTree->ConstructWidget<UHorizontalBox>();
            OfferLine->SetVisibility(ESlateVisibility::Collapsed);OfferGroup=OfferLine;
            OfferNumber=Text(TEXT(""),LDHUDStyle::TypeBody);OfferNumber->SetAutoWrapText(false);OfferNumber->SetColorAndOpacity(LDHUDStyle::TextAccent);
            ScaleNewText(OfferNumber);RowWidgets.Add(OfferNumber);
            OfferLine->AddChildToHorizontalBox(OfferNumber)->SetVerticalAlignment(VAlign_Center);
            OfferRow=TrayRow(WidgetTree);
            auto* OfferFrame=WidgetTree->ConstructWidget<UBorder>();
            OfferFrame->SetBrush(FSlateNoResource());OfferFrame->SetPadding(FMargin(0));OfferFrame->SetContent(OfferRow);
            OfferLine->AddChildToHorizontalBox(OfferFrame)->SetPadding(FMargin(8,0,0,0));AddRow(OfferLine);
            for(int32 Index=0;Index<3;++Index)
            {
                FTrayCard Card=MakeCard(FName(*FString::Printf(TEXT("GrowthOffer%d"),Index)),[this,Index](){ChooseOffer(Index);});
                OfferRow->AddChildToWrapBox(Card.Frame);OfferCards.Add(Card);
            }
            SkipCard=MakeCard(TEXT("GrowthOfferSkip"),[this](){SkipOffer();});
            OfferRow->AddChildToWrapBox(SkipCard.Frame);
            FinalSourceRow=TrayRow(WidgetTree);AddRow(FinalSourceRow);
            auto* FinalLine=WidgetTree->ConstructWidget<UHorizontalBox>();
            FinalModGroup=FinalLine;
            auto* FinalHeading=Text(TEXT("终局改装"),LDHUDStyle::TypeBody);
            FinalHeading->SetColorAndOpacity(LDHUDStyle::TextAccent);
            ScaleNewText(FinalHeading);RowWidgets.Add(FinalHeading);
            FinalLine->AddChildToHorizontalBox(FinalHeading)->SetVerticalAlignment(VAlign_Center);
            FinalModRow=TrayRow(WidgetTree);
            FinalLine->AddChildToHorizontalBox(FinalModRow)->SetPadding(FMargin(8,0,0,0));AddRow(FinalLine);
            for(int32 Index=0;Index<3;++Index)
            {
                FTrayCard Card=MakeCard(FName(*FString::Printf(TEXT("GrowthFinal%d"),Index)),[this,Index](){if(IsFinalStage()){SelectFinalMod(Index);}else if(Index==0&&FinalDormant()){RebindFinalMod();}else{ReopenFinalStage(Index);}});
                FinalModRow->AddChildToWrapBox(Card.Frame);FinalCards.Add(Card);
            }
            TrayFrame=WidgetTree->ConstructWidget<UBorder>();TrayFrame->SetBrushColor(FLinearColor::Transparent);TrayFrame->SetPadding(FMargin(0,0,16,72));
            TrayFrame->SetHorizontalAlignment(HAlign_Right);TrayFrame->SetVerticalAlignment(VAlign_Bottom);
            TrayFrame->SetVisibility(ESlateVisibility::Collapsed);TrayFrame->SetContent(Tray);
            // The offer and final mod rows move onto the stage in the workshop and keep their own width.
            TrayRows={BlueprintCardRow,BaseRow,FinalSourceRow};
        }
        {
            // #198 rest services on the workshop (APP_SPEC §7 构筑): each stands on the object it acts on and shows why it is
            // refused. The trainer's 洗点 at the character column's foot, 遗忘 and the selected blueprint's 学到/替换 on the Q/W/E
            // heads, the blueprints to pick at the top of the pending area; the merchant's card at the top of the pending area;
            // the artisan's 补缺 and 拆解 at the top of the library; their pickers and every confirm line in the dock.
            const auto Boxed=[&](UWidget* Lead,TObjectPtr<UTextBlock>& OutBlocked,float Wrap)
            {
                auto* Box=WidgetTree->ConstructWidget<UVerticalBox>();
                Box->AddChildToVerticalBox(Lead)->SetHorizontalAlignment(HAlign_Left);
                OutBlocked=Wrapped(LDHUDStyle::TypeCaption,Wrap);OutBlocked->SetColorAndOpacity(LDHUDStyle::TextSecondary);
                Box->AddChildToVerticalBox(OutBlocked)->SetPadding(FMargin(0,4,0,0));
                Box->SetVisibility(ESlateVisibility::Collapsed);
                return Box;
            };
            NpcButton=LDWidgets::Button(WidgetTree,TEXT(""),[this](){BuyNpcOffer();},false,TEXT("GrowthRestNpc"));
            NpcBox=Boxed(NpcButton.Get(),NpcBlocked,420.f);
            RespecButton=LDWidgets::HoldButton(WidgetTree,LDTerms::Respec,[this](){Respec();},TEXT("GrowthRestRespec"));
            RespecBox=Boxed(RespecButton.Get(),RespecBlocked,300.f);
            // Mockup 05: 洗点 spans the foot of the character column; 遗忘 is the right click on a skill head.
            if(UVerticalBoxSlot* LeadSlot=Cast<UVerticalBoxSlot>(RespecButton->Slot)){LeadSlot->SetHorizontalAlignment(HAlign_Fill);}
            ForgetBlocked=Wrapped(LDHUDStyle::TypeCaption,300.f);ForgetBlocked->SetColorAndOpacity(LDHUDStyle::TextSecondary);
            Cast<UVerticalBox>(RespecBox.Get())->AddChildToVerticalBox(ForgetBlocked)->SetPadding(FMargin(0,2,0,0));
            FillGapButton=LDWidgets::Button(WidgetTree,TEXT(""),[this](){ToggleFillGap();},false,TEXT("GrowthRestFillGap"));
            DismantleButton=LDWidgets::Button(WidgetTree,TEXT(""),[this](){ToggleDismantle();},false,TEXT("GrowthRestDismantle"));
            {
                auto* Artisan=WidgetTree->ConstructWidget<UHorizontalBox>();
                auto* GapBox=Boxed(FillGapButton.Get(),FillGapBlocked,200.f);GapBox->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
                auto* DismantleBox=Boxed(DismantleButton.Get(),DismantleBlocked,200.f);DismantleBox->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
                Artisan->AddChildToHorizontalBox(GapBox);Artisan->AddChildToHorizontalBox(DismantleBox)->SetPadding(FMargin(10,0,0,0));
                Artisan->SetVisibility(ESlateVisibility::Collapsed);ArtisanBox=Artisan;
            }
            TrainerPicks=WidgetTree->ConstructWidget<UWrapBox>();TrainerPicks->SetInnerSlotPadding(FVector2D(6,6));TrainerPicks->SetVisibility(ESlateVisibility::Collapsed);
            auto* Body=WidgetTree->ConstructWidget<UVerticalBox>();ServiceDockBody=Body;
            auto PickLine=[&](const TCHAR* Label)
            {
                auto* Line=WidgetTree->ConstructWidget<UWrapBox>();Line->SetExplicitWrapSize(true);Line->SetWrapSize(ServicePanelWidth+200.f);
                Line->SetInnerSlotPadding(FVector2D(6,4));
                UTextBlock* Lead=OneLine(LDHUDStyle::TypeCaption);Lead->SetText(FText::FromString(Label));
                Line->AddChildToWrapBox(Lead);
                Line->SetVisibility(ESlateVisibility::Collapsed);
                Body->AddChildToVerticalBox(Line)->SetPadding(FMargin(0,4,0,0));
                return Line;
            };
            GapTargetLine=PickLine(LDTerms::PickGapTarget);
            GapFillerLine=PickLine(LDTerms::PickGapFiller);
            {
                // 拆解: the rule line with the picked count, then the equation (inputs + … → result); the inputs are picked on
                // the library and the board themselves.
                auto* Picker=WidgetTree->ConstructWidget<UVerticalBox>();
                DismantleHeading=OneLine(LDHUDStyle::TypeSmall);DismantleHeading->SetColorAndOpacity(LDHUDStyle::UIText);
                Picker->AddChildToVerticalBox(DismantleHeading);
                auto* Stack=WidgetTree->ConstructWidget<UOverlay>();
                DismantlePaint=WidgetTree->ConstructWidget<ULDPaintLayer>();
                DismantlePaint->Painter=[this](const LDUIPaint::FContext& Paint){PaintDismantle(Paint);};
                auto* PaintSlot=Stack->AddChildToOverlay(DismantlePaint);PaintSlot->SetHorizontalAlignment(HAlign_Fill);PaintSlot->SetVerticalAlignment(VAlign_Fill);
                DismantleEquation=WidgetTree->ConstructWidget<UHorizontalBox>();
                Stack->AddChildToOverlay(DismantleEquation);
                Picker->AddChildToVerticalBox(Stack)->SetPadding(FMargin(0,12,0,0));
                Picker->SetVisibility(ESlateVisibility::Collapsed);
                DismantleSection=Picker;
                Body->AddChildToVerticalBox(Picker)->SetPadding(FMargin(0,0,0,0));
            }
            // The confirm line names what is forgotten, replaced, filled or removed; the components it sends back light up on
            // the board first.
            auto* ConfirmRow=WidgetTree->ConstructWidget<UHorizontalBox>();
            RestConfirmText=Wrapped(LDHUDStyle::TypeText,ServicePanelWidth);RestConfirmText->SetColorAndOpacity(LDHUDStyle::TextWarning);
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
            // The centre stage: title, chance line, the big cards (moved here from their tray rows) and the final sources.
            auto* Box=WidgetTree->ConstructWidget<UVerticalBox>();
            StageTitle=LDWidgets::Text(WidgetTree,TEXT(""),LDHUDStyle::TypeHero,NAME_None,LDUIFonts::ERole::Title);StageTitle->SetAutoWrapText(false);
            StageTitle->SetJustification(ETextJustify::Center);ScaleNewText(StageTitle);
            Box->AddChildToVerticalBox(StageTitle)->SetHorizontalAlignment(HAlign_Center);
            StageSub=LDWidgets::Text(WidgetTree,TEXT(""),LDHUDStyle::TypeSmall,NAME_None,LDUIFonts::ERole::Strong);StageSub->SetAutoWrapText(false);
            StageSub->SetColorAndOpacity(LDHUDStyle::TextSecondary);ScaleNewText(StageSub);
            Box->AddChildToVerticalBox(StageSub)->SetHorizontalAlignment(HAlign_Center);
            StageCards=WidgetTree->ConstructWidget<UHorizontalBox>();
            auto* CardsSlot=Box->AddChildToVerticalBox(StageCards);CardsSlot->SetHorizontalAlignment(HAlign_Center);CardsSlot->SetPadding(FMargin(0,28,0,0));
            auto* SourceLine=WidgetTree->ConstructWidget<UHorizontalBox>();
            UTextBlock* SourceLabel=LDWidgets::Text(WidgetTree,LDTerms::FinalSourceLabel,LDHUDStyle::TypeSmall);SourceLabel->SetColorAndOpacity(LDHUDStyle::TextSecondary);
            SourceLine->AddChildToHorizontalBox(SourceLabel)->SetVerticalAlignment(VAlign_Center);
            StageSources=WidgetTree->ConstructWidget<UHorizontalBox>();
            SourceLine->AddChildToHorizontalBox(StageSources)->SetPadding(FMargin(16,0,0,0));
            SourceLine->SetVisibility(ESlateVisibility::Collapsed);StageSourceLine=SourceLine;
            auto* SourceSlot=Box->AddChildToVerticalBox(SourceLine);SourceSlot->SetHorizontalAlignment(HAlign_Center);SourceSlot->SetPadding(FMargin(0,26,0,0));
            UScaleBox* StageFit=LDWidgets::Fit(WidgetTree,Box);
            if(UScaleBoxSlot* FitSlot=Cast<UScaleBoxSlot>(Box->Slot)){FitSlot->SetHorizontalAlignment(HAlign_Center);}
            StageFit->SetVisibility(ESlateVisibility::Collapsed);StageBox=StageFit;
            StageBack=LDWidgets::Button(WidgetTree,LDTerms::Choice,[this](){LeaveStage();},false,TEXT("GrowthStageBack"));
            StageBack->SetVisibility(ESlateVisibility::Collapsed);
            // The final mod's dock: the sources of a picked column holding several, then its effect, cost and notes lines (mockup
            // 06; the whole description stands alone when the catalog has no split), its numbers, and the hold that locks it.
            auto* Desc=WidgetTree->ConstructWidget<UVerticalBox>();
            {
                auto* ChoiceLine=WidgetTree->ConstructWidget<UHorizontalBox>();
                UTextBlock* ChoiceLabel=LDWidgets::Text(WidgetTree,LDTerms::FinalPickSource,LDHUDStyle::TypeSmall);
                ChoiceLabel->SetColorAndOpacity(LDHUDStyle::UIEvent);ChoiceLabel->SetAutoWrapText(false);
                ChoiceLine->AddChildToHorizontalBox(ChoiceLabel)->SetVerticalAlignment(VAlign_Center);
                FinalSourceChoices=WidgetTree->ConstructWidget<UHorizontalBox>();
                ChoiceLine->AddChildToHorizontalBox(FinalSourceChoices)->SetPadding(FMargin(14,0,0,0));
                ChoiceLine->SetVisibility(ESlateVisibility::Collapsed);FinalSourceChoiceLine=ChoiceLine;
                Desc->AddChildToVerticalBox(ChoiceLine)->SetPadding(FMargin(0,0,0,10));
            }
            const FLinearColor PartInk[3]={LDHUDStyle::UILegal,LDHUDStyle::UIReject,LDHUDStyle::TextSecondary};
            for(int32 Part=0;Part<3;++Part)
            {
                auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();
                UTextBlock* Label=LDWidgets::Text(WidgetTree,LDTerms::FinalPartLabel(Part),LDHUDStyle::TypeCaption,NAME_None,LDUIFonts::ERole::Strong);
                Label->SetAutoWrapText(false);Label->SetColorAndOpacity(PartInk[Part]);
                auto* Chip=WidgetTree->ConstructWidget<UBorder>();
                Chip->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White,3.f,PartInk[Part].CopyWithNewOpacity(.7f),1.f));
                Chip->SetBrushColor(PartInk[Part].CopyWithNewOpacity(.12f));Chip->SetPadding(FMargin(8,1));Chip->SetContent(Label);
                auto* ChipSlot=Row->AddChildToHorizontalBox(Chip);ChipSlot->SetVerticalAlignment(VAlign_Top);ChipSlot->SetPadding(FMargin(0,1,14,0));
                UTextBlock* PartText=Wrapped(LDHUDStyle::TypeText,ServicePanelWidth+300.f);
                PartText->SetColorAndOpacity(Part==2?LDHUDStyle::TextSecondary:LDHUDStyle::UIText);
                Row->AddChildToHorizontalBox(PartText)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
                Row->SetVisibility(ESlateVisibility::Collapsed);
                Desc->AddChildToVerticalBox(Row)->SetPadding(FMargin(0,Part?8:0,0,0));
                FinalPartRows.Add(Row);FinalPartTexts.Add(PartText);
            }
            FinalDescText=Wrapped(LDHUDStyle::TypeText,ServicePanelWidth+300.f);FinalDescText->SetColorAndOpacity(LDHUDStyle::UIText);
            Desc->AddChildToVerticalBox(FinalDescText)->SetPadding(FMargin(0,8,0,0));
            FinalNumbersText=Wrapped(LDHUDStyle::TypeCaption,ServicePanelWidth+300.f);FinalNumbersText->SetColorAndOpacity(LDHUDStyle::TextSecondary);
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
            static_cast<UWidget*>(FinalLockNote.Get()),RespecBox.Get(),NpcBox.Get(),ArtisanBox.Get(),static_cast<UWidget*>(TrainerPicks.Get()),ServiceDock.Get()})
        {UCanvasPanelSlot* ItemSlot=Layer->AddChildToCanvas(Item);ItemSlot->SetAutoSize(true);ItemSlot->SetZOrder(Item==ServiceDock.Get()?3:Item==StageBox.Get()?1:2);LayerSlots.Add(Item,ItemSlot);}
        auto* Layers=WidgetTree->ConstructWidget<UOverlay>();Layers->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        for(UWidget* Each:{static_cast<UWidget*>(TrayFrame.Get()),static_cast<UWidget*>(Layer.Get())})
        {auto* LayerSlot=Layers->AddChildToOverlay(Each);LayerSlot->SetHorizontalAlignment(HAlign_Fill);LayerSlot->SetVerticalAlignment(VAlign_Fill);}
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
    Shape->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White,TrayIconRadius,LDHUDStyle::ShapeRim,1.5f));Shape->SetBrushColor(LDHUDStyle::ShapeFill);
    auto* ShapeSlot=IconBox->AddChildToOverlay(Shape);ShapeSlot->SetHorizontalAlignment(HAlign_Fill);ShapeSlot->SetVerticalAlignment(VAlign_Fill);
    Card.Picture=WidgetTree->ConstructWidget<UImage>();Card.Picture->SetVisibility(ESlateVisibility::Collapsed);RowWidgets.Add(Card.Picture);
    auto* PictureSlot=IconBox->AddChildToOverlay(Card.Picture);PictureSlot->SetHorizontalAlignment(HAlign_Fill);PictureSlot->SetVerticalAlignment(VAlign_Fill);
    Card.Glyph=LDWidgets::Text(WidgetTree,TEXT(""),LDHUDStyle::TypeSection);Card.Glyph->SetAutoWrapText(false);Card.Glyph->SetJustification(ETextJustify::Center);
    ScaleNewText(Card.Glyph);RowWidgets.Add(Card.Glyph);
    auto* GlyphSlot=IconBox->AddChildToOverlay(Card.Glyph);GlyphSlot->SetHorizontalAlignment(HAlign_Center);GlyphSlot->SetVerticalAlignment(VAlign_Center);
    Card.IconFrame=WidgetTree->ConstructWidget<USizeBox>();Card.IconFrame->SetWidthOverride(TrayCardIcon);Card.IconFrame->SetHeightOverride(TrayCardIcon);
    Card.IconFrame->SetContent(IconBox);RowWidgets.Add(Card.IconFrame);
    Face->AddChildToVerticalBox(Card.IconFrame)->SetHorizontalAlignment(HAlign_Center);
    Card.Name=LDWidgets::Text(WidgetTree,TEXT(""),LDHUDStyle::TypeBody);Card.Name->SetAutoWrapText(false);
    Card.Name->SetWrapTextAt(TrayCardWidth-2.f*TrayCardPadding);
    Card.Name->SetJustification(ETextJustify::Center);ScaleNewText(Card.Name);RowWidgets.Add(Card.Name);
    auto* NameSlot=Face->AddChildToVerticalBox(Card.Name);NameSlot->SetHorizontalAlignment(HAlign_Fill);NameSlot->SetPadding(FMargin(0,4,0,0));
    {
        // The stage face's extra lines: targeting, mana and cooldown; the tags; the element chip on the icon; the 可接 row.
        const auto Line=[this](int32 Size,const FLinearColor& Ink,LDUIFonts::ERole Role)
        {
            UTextBlock* Text=LDWidgets::Text(WidgetTree,TEXT(""),Size,NAME_None,Role);Text->SetAutoWrapText(false);Text->SetJustification(ETextJustify::Center);
            Text->SetColorAndOpacity(Ink);Text->SetVisibility(ESlateVisibility::Collapsed);ScaleNewText(Text);RowWidgets.Add(Text);return Text;
        };
        Card.Facts=Line(LDHUDStyle::TypeSmall,LDHUDStyle::UIText,LDUIFonts::ERole::Number);
        Face->AddChildToVerticalBox(Card.Facts)->SetPadding(FMargin(0,10,0,0));
        Card.Tags=Line(LDHUDStyle::TypeTiny,LDHUDStyle::TextSecondary,LDUIFonts::ERole::Body);
        Face->AddChildToVerticalBox(Card.Tags)->SetPadding(FMargin(0,6,0,0));
        Card.Element=Line(LDHUDStyle::TypeTiny,LDHUDStyle::UIBase,LDUIFonts::ERole::Strong);Card.Element->SetVisibility(ESlateVisibility::HitTestInvisible);
        auto* Chip=WidgetTree->ConstructWidget<UBorder>();RowWidgets.Add(Chip);Chip->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White,11.f,LDHUDStyle::UIBase,1.f));
        Chip->SetPadding(FMargin(6,1));Chip->SetContent(Card.Element);Chip->SetVisibility(ESlateVisibility::Collapsed);Chip->SetRenderTranslation(FVector2D(6,6));
        auto* ChipSlot=IconBox->AddChildToOverlay(Chip);ChipSlot->SetHorizontalAlignment(HAlign_Right);ChipSlot->SetVerticalAlignment(VAlign_Bottom);
        Card.ElementChip=Chip;
        Card.Links=WidgetTree->ConstructWidget<UHorizontalBox>();RowWidgets.Add(Card.Links);Card.Links->SetVisibility(ESlateVisibility::Collapsed);
        UTextBlock* Label=LDWidgets::Text(WidgetTree,LDTerms::OfferLinks,LDHUDStyle::TypeTiny);Label->SetAutoWrapText(false);Label->SetColorAndOpacity(LDHUDStyle::TextSecondary);RowWidgets.Add(Label);
        Card.Links->AddChildToHorizontalBox(Label)->SetVerticalAlignment(VAlign_Center);
        for(int32 Link=0;Link<4;++Link)
        {
            UImage* Icon=WidgetTree->ConstructWidget<UImage>();RowWidgets.Add(Icon);Icon->SetVisibility(ESlateVisibility::Collapsed);
            auto* IconBoxSize=WidgetTree->ConstructWidget<USizeBox>();RowWidgets.Add(IconBoxSize);IconBoxSize->SetWidthOverride(34.f);IconBoxSize->SetHeightOverride(34.f);
            IconBoxSize->SetContent(Icon);
            Card.Links->AddChildToHorizontalBox(IconBoxSize)->SetPadding(FMargin(8,0,0,0));
            Card.LinkIcons.Add(Icon);
        }
        Face->AddChildToVerticalBox(Card.Links)->SetPadding(FMargin(0,14,0,0));
        if(UVerticalBoxSlot* LinksSlot=Cast<UVerticalBoxSlot>(Card.Links->Slot)){LinksSlot->SetHorizontalAlignment(HAlign_Center);}
    }
    Card.Button=LDWidgets::Button(WidgetTree,TEXT(""),MoveTemp(Click),false,ButtonName);RowWidgets.Add(Card.Button);
    Card.Button->SetContent(Face);GrowthTint(Card.Button,LDHUDStyle::CardIdle);
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
void ULDChoicesPanel::ShowLook(FTrayCard& Card,bool bTile,bool bEnabled,bool bVisible)
{
    GrowthShown(Card.Frame,bVisible);
    bTile|=bIconOnlyTray&&!Card.bBig;
    Card.bTile=bTile;
    if(!Card.bBig)
    {
        const float Scale=GetCachedGeometry().Scale>0.f?GetCachedGeometry().Scale:1.f;
        const float Width=TrayCellWidth(bTile,Scale,bCompactTray),Height=TrayCellHeight(bTile,AppliedTextScale,Scale,bCompactTray);
        const float Icon=TrayIconSize(bTile,Scale,bCompactTray);
        if(!FMath::IsNearlyEqual(Card.Frame->GetWidthOverride(),Width)){Card.Frame->SetWidthOverride(Width);}
        if(!FMath::IsNearlyEqual(Card.Frame->GetHeightOverride(),Height)){Card.Frame->SetHeightOverride(Height);}
        if(!FMath::IsNearlyEqual(Card.IconFrame->GetWidthOverride(),Icon))
        {Card.IconFrame->SetWidthOverride(Icon);Card.IconFrame->SetHeightOverride(Icon);}
        if(!bTile&&!FMath::IsNearlyEqual(Card.Name->GetWrapTextAt(),Width-2.f*TrayCardPadding))
        {Card.Name->SetWrapTextAt(Width-2.f*TrayCardPadding);}
    }
    GrowthShown(Card.Name,!bTile,ESlateVisibility::HitTestInvisible);
    GrowthEnabled(Card.Button,bEnabled);
    // A card that cannot be taken now greys its picture too; the popup says why.
    if(Card.AppliedEnabled!=int32(bEnabled))
    {
        Card.AppliedEnabled=int32(bEnabled);
        const bool bPicture=LDUIIcons::Apply(Card.Picture,Card.AppliedIcon,TrayIconRadius,bEnabled?FLinearColor::White:LDHUDStyle::IconBlocked);
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
    const auto FitLabeledRow=[Wrap](UWidget* Group,UWrapBox* Row)
    {
        const UPanelWidget* Line=Cast<UPanelWidget>(Group);
        const UWidget* Label=Line?Line->GetChildAt(0):nullptr;
        const float Leading=Label&&Label->IsVisible()?float(Label->GetDesiredSize().X)+8.f:0.f;
        const float RowWidth=FMath::Max(0.f,Wrap-Leading);
        if(Row&&!FMath::IsNearlyEqual(Row->GetWrapSize(),RowWidth)){Row->SetWrapSize(RowWidth);}
    };
    FitLabeledRow(OfferGroup,OfferRow);
    FitLabeledRow(FinalModGroup,FinalModRow);
}
FVector2D ULDChoicesPanel::TrayDesiredSize() const
{
    return bOpen&&Tray?Tray->GetDesiredSize():FVector2D::ZeroVector;
}
const UWidget* ULDChoicesPanel::Hovered(FVector2D Pointer,LDHUD::FActionDetailView& Out,FName* OutComponent,FString* OutIcon) const
{
    if(OutComponent){*OutComponent=NAME_None;}
    // Quick picks in an engagement window are icons only: no description.
    if((!bOpen&&!(bQuickShown&&!bQuickCompact))||!Tray){return nullptr;}
    const FTrayCard* Found=nullptr;
    const auto Over=[&Found,Pointer](const FTrayCard& Card)
    {
        if(!Found&&Card.Frame&&Card.Frame->IsVisible()&&Card.Frame->GetCachedGeometry().IsUnderLocation(Pointer)){Found=&Card;}
    };
    const auto Row=[&Over](const UWidget* Box,const TArray<FTrayCard>& Cards){if(Box&&Box->IsVisible()){for(const FTrayCard& Card:Cards){Over(Card);}}};
    Row(OfferRow,OfferCards);if(OfferRow&&OfferRow->IsVisible()){Over(SkipCard);}Row(BaseRow,BaseCards);Row(BlueprintCardRow,BlueprintCards);
    Row(FinalModRow,FinalCards);Row(FinalSourceRow,FinalSourceCards);
    Row(StageSourceLine,FinalColumnCards);Row(FinalSourceChoiceLine,FinalSourceCards);
    for(const FChoiceRow& Choice:ChoiceRows){Row(Choice.Row,Choice.Options);if(Choice.Row->IsVisible()){Over(Choice.Points);}}
    if(!Found&&OfferRow->IsVisible()&&OfferCards.IsValidIndex(PinnedPreview)&&OfferCards[PinnedPreview].Frame->IsVisible()){Found=&OfferCards[PinnedPreview];}
    if(!Found){return nullptr;}
    Out=Found->Detail;
    if(OutComponent){*OutComponent=Found->Component;}
    if(OutIcon){*OutIcon=Found->AppliedIcon;}
    return Found->Frame;
}
bool ULDChoicesPanel::IsEngaged() const
{
    return (bOpen&&Tray&&Tray->IsHovered())||PendingRequest!=0||HasChildConfirmation()||bFillGapOpen||bDismantleOpen||!PendingFinalOption.IsNone();
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
void ULDChoicesPanel::SetServiceContext(ELDRestService Service)
{
    if(ServiceContext==Service){return;}
    const ELDRestService Previous=ServiceContext;
    ServiceContext=Service;
    CancelRestAction();CloseMaterialPickers();TrainerBlueprint=0;
    // The services follow the NPC beside the player: leaving it takes them off the workshop.
    if(Previous!=ELDRestService::None&&Service==ELDRestService::None&&bOpen){Tell(TEXT("已离开 NPC，服务已收起。"));}
    RefreshAll();
}
UWidget* ULDChoicesPanel::FocusScope() const{return WidgetTree?WidgetTree->RootWidget.Get():nullptr;}
UWidget* ULDChoicesPanel::HitArea() const{return Tray.Get();}
void ULDChoicesPanel::OpenPanel()
{
    if(const auto* PC=Cast<ALDPlayerController>(GetOwningPlayer());PC&&PC->BuildBlueprints&&PC->BuildBlueprints->IsViewerOpen())
    {PC->BuildBlueprints->ToggleViewer();}
    GrowthShown(TrayFrame,true);
    bOpen=true;SetVisibility(ESlateVisibility::SelfHitTestInvisible);Fold.Opened(false,UiNow());
    RefreshAll();
}
bool ULDChoicesPanel::ClosePanel()
{
    // Closing only hides the workshop: the offer, the blueprints and any submitted request keep their state; an unconfirmed
    // replace, forget, fill gap or dismantle is withdrawn with its pickers, an unlocked final selection with it.
    LDWidgets::ReleaseHolds(WidgetTree);
    RestConfirm=FRestConfirm();GrowthShown(RestConfirmLine,false);CloseMaterialPickers();
    PinnedPreview=INDEX_NONE;PendingFinalOption=NAME_None;FinalSourcePick=INDEX_NONE;FinalColumnPick=INDEX_NONE;bOpen=false;
    bFinalFeedbackAllowed=false;
    const LDHUD::FOfferMotionTicket PendingOffer=OfferMotion.Ticket;
    OfferMotion=FOfferMotion();
    if(PendingRequest){OfferMotion.Ticket=PendingOffer;}
    if(!bQuickShown){GrowthShown(TrayFrame,false);SetVisibility(ESlateVisibility::Collapsed);}
    Fold.Closed();RefreshAll();return true;
}
bool ULDChoicesPanel::IsServiceShown() const
{
    // Only a rest's build windows (build and payout) offer services; the locked steps and the return take them away.
    return bOpen&&Places.bActive&&!bStageShown&&ServiceContext!=ELDRestService::None&&LatestStage==LDProgression::EMatchStage::Rest;
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
    RefreshAll();
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
        // What sits on the forgotten active goes back to the storage (APP_SPEC §4.4); it lights orange first.
        const LDHUD::FStorageView Shown=LDHUD::ComponentStorage(Latest,LatestContent);
        const int32 Column=int32(LDHUD::SlotColumn(RestConfirm.Slot));
        if(Shown.Columns.IsValidIndex(Column)){for(const LDHUD::FEquippedView& Entry:Shown.Columns[Column].Entries){Out.Add(Entry.Component);}}
    }
    else if(RestConfirm.Kind==ELDBuildCommandKind::Dismantle)
    {
        Out=RestConfirm.Surrendered;Out.Append(LDHUD::DismantleDetached(Latest,RestConfirm.Surrendered));
    }
    else if(bDismantleOpen)
    {
        // While picking, the installs a pick would send back light orange as soon as it is picked.
        Out=LDHUD::DismantleDetached(Latest,DismantlePicked);
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
    return Usable()&&!ConfirmedOfferStillShown()&&Latest.bFinalModPending&&Latest.FinalModOptions.Num()==3;
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
        const UWidget* Source=SkipCard.Facts&&SkipCard.Facts->IsVisible()?static_cast<const UWidget*>(SkipCard.Facts.Get()):SkipCard.Name.Get();
        if(Source&&Source->IsVisible()){OfferMotion.Chosen.Icon=Source->GetCachedGeometry().GetLayoutBoundingRect();}
    }
    if(!GrowthHasArea(OfferMotion.Chosen.Icon)){return;}
    for(int32 Other=0;Other<OfferCards.Num();++Other)
    {
        if((bSkip||Other!=Index)&&OfferCards[Other].Frame&&OfferCards[Other].Frame->IsVisible()){OfferMotion.Others.Add(Capture(OfferCards[Other]));}
    }
    if(!bSkip&&SkipCard.Frame&&SkipCard.Frame->IsVisible()){OfferMotion.Others.Add(Capture(SkipCard));}
}
void ULDChoicesPanel::Present(const FLDProgressionView& View,uint32 Match,uint32 Life,ELDLifePhase LifePhase,LDProgression::EMatchStage Stage,
    const FLDDemoContentDefinitions* Content,const LDHUD::FCardStats& Stats,const LDHUD::FSlotKeys& Keys)
{
    if(OfferMotion.Ticket.Match!=0&&(OfferMotion.Ticket.Match!=Match||OfferMotion.Ticket.Life!=Life)){OfferMotion=FOfferMotion();}
    if(ConfirmedOffer.IsValid()&&(ConfirmedMatch!=Match||ConfirmedLife!=Life))
    {ConfirmedOffer.Invalidate();ConfirmedMatch=ConfirmedLife=0;}
    const bool bStatsChanged=Stats.Level!=LatestStats.Level||Stats.ADP2!=LatestStats.ADP2||Stats.APP2!=LatestStats.APP2
        ||Stats.MaxHPP2!=LatestStats.MaxHPP2||Stats.ArmorP2!=LatestStats.ArmorP2||Content!=LatestContent||!(Keys==SlotKeys);
    SlotKeys=Keys;
    Latest=View;LatestMatch=Match;LatestLife=Life;LatestLifePhase=LifePhase;LatestStage=Stage;
    LatestContent=Content;LatestStats=Stats;
    if(!Tray){return;}
    Gate=Usable()?LDHUD::BuildGate(Stage,LifePhase):LDHUD::FBuildGateView();
    if(bStatsChanged){bCardsStale=true;}
    // The HUD presents every frame; a collapsed panel only keeps the inputs and refreshes when it opens.
    if(!bOpen&&!bQuick&&!bQuickShown){return;}
    RefreshAll();
}
void ULDChoicesPanel::RefreshAll()
{
    if(!Tray){return;}
    ShownBlueprints=Usable()?Latest.PendingBlueprints:TArray<FName>();
    // Too many component cards turn every component row into a grid of icon tiles.
    const bool bTiles=Usable()&&LDHUD::ChoiceTrayTiles(Latest);
    // Field quick picks stand above the 构筑 button while the workshop is closed and something waits; in an engagement window
    // they are icons only.
    const bool bQuickNow=!bOpen&&bQuick&&Usable()&&HasCards();
    if(bQuickNow!=bQuickShown)
    {
        bQuickShown=bQuickNow;
        GrowthShown(TrayFrame,bOpen||bQuickShown);
        SetVisibility(bOpen||bQuickShown?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
    }
    UpdateStage();
    GrowthShown(TrayFrame,(bOpen||bQuickShown)&&!bStageShown);
    PlanTrayPages(bTiles);
    RefreshOffer();RefreshFinalMod();RefreshOpeningBase(bTiles||bIconOnlyTray||(bQuickShown&&bQuickCompact));
    RefreshComponentChoices(bTiles||bIconOnlyTray||(bQuickShown&&bQuickCompact));
    RefreshBlueprintCards();RefreshBlueprints();RefreshRest();
    bool bChoiceVisible=false;
    for(int32 Index=0;Index<ShownChoices.Num();++Index){bChoiceVisible|=TrayShows(ETrayGroup::ComponentChoice,Index);}
    GrowthShown(ChoiceRowList,bChoiceVisible);
    RefreshStage();
    LayoutLayer();
    bCardsStale=false;
}
void ULDChoicesPanel::PlanTrayPages(bool bTiles)
{
    TrayUnits.Reset();TrayPages.Reset();
    bCompactTray=false;bIconOnlyTray=false;
    if(!bOpen||bStageShown||!Usable())
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
        const auto LabelWidth=[](const UWidget* Group)
        {
            const UPanelWidget* Line=Cast<UPanelWidget>(Group);
            const UWidget* Label=Line?Line->GetChildAt(0):nullptr;
            return FMath::Max(80.f,Label?float(Label->GetDesiredSize().X)+8.f:0.f);
        };
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
        for(int32 Index=0;Index<Latest.ComponentChoices.Num();++Index)
        {
            const FLDComponentChoiceView& Choice=Latest.ComponentChoices[Index];
            Add(ETrayGroup::ComponentChoice,Index,Choice.Options.Num()+(LDHUD::ComponentChoiceAllOwned(Choice,Latest)?1:0),bTiles);
        }
        if(Latest.bOpeningBasePending){Add(ETrayGroup::OpeningBase,0,Latest.OpeningBaseOptions.Num(),bTiles);}
        const bool bOffer=HasAvailableOffer();
        if(bOffer){Add(ETrayGroup::Offer,0,Latest.ActiveOffer.Options.Num()+1,false,LabelWidth(OfferGroup));}
        else if(!ConfirmedOfferStillShown()&&Latest.QueuedActiveOpportunities>0){Add(ETrayGroup::Offer,0,1,false,LabelWidth(OfferGroup));}
        const bool bFinal=HasAvailableFinal();
        if(bFinal||!Latest.FinalMod.IsEmpty()){Add(ETrayGroup::FinalMod,0,bFinal?3:1,false,LabelWidth(FinalModGroup));}
        int32 Sources=0;
        if(!PendingFinalOption.IsNone())
        {for(const FLDFinalModSourceView& Source:Latest.FinalModSources){if(Source.Option==PendingFinalOption){++Sources;}}}
        Add(ETrayGroup::FinalSource,0,Sources,Sources>6);
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
    if(!bOffer||Latest.ActiveOffer.OfferId!=ShownOffer){PinnedPreview=INDEX_NONE;}
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
            // The evidence for the choice: the owned components that go on it and the reactions with the learned actives.
            const LDWorkshop::FOfferLinks Preview=Gate.bCanChoose
                ?LDWorkshop::LinkableComponents(Id,Latest,LatestContent,LatestStage,LatestLifePhase):LDWorkshop::FOfferLinks();
            const TArray<FName>& Links=Preview.Components;
            TArray<FString> Lines;
            FString LinksLine;
            if(Preview.Failure!=ELDGrowthReason::None)
            {
                UE_LOG(LogLDUI,Warning,TEXT("LD_HUD_OFFER_LINK_PREVIEW_FAILED active=%s reason=%u"),*Id.ToString(),uint32(Preview.Failure));
                Lines.Add(LDTerms::OfferLinksUnavailable(LDHUD::GrowthReasonText(Preview.Failure)));
            }
            if(!Links.IsEmpty())
            {
                TArray<FString> Names;for(const FName& Link:Links){Names.Add(LDHUD::ContentName(LatestContent,Link.ToString()));}
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
                    }
                }
            }
            if(!Lines.IsEmpty()){Detail.Body+=TEXT("\n\n")+FString::Join(Lines,TEXT("\n"));}
            ShowFace(Card,Key,View.Name,View.IconId,View.Glyph,Detail);
            // The stage face: targeting, mana and cooldown, the keywords as tags, the element and the 可接 icons.
            GrowthText(Card.Facts,View.Target+TEXT("\n")+View.Cost);
            GrowthText(Card.Tags,FString::Join(LDWorkshop::KeywordNames(View.Keywords),TEXT("  ·  ")));
            GrowthText(Card.Element,Own.IsEmpty()?FString():LDHUD::ElementName(Own[0]));
            if(UBorder* Chip=Cast<UBorder>(Card.ElementChip.Get())){Chip->SetBrushColor(Own.IsEmpty()?FLinearColor::Transparent:LDHUDStyle::UIAction);}
            for(int32 Link=0;Link<Card.LinkIcons.Num();++Link)
            {
                const bool bLink=Links.IsValidIndex(Link);
                if(!bLink){GrowthShown(Card.LinkIcons[Link],false);continue;}
                const uint8 Kind=uint8(LDWorkshop::ComponentFacts(Links[Link],LatestContent).Kind);
                LDUIIcons::Apply(Card.LinkIcons[Link],LDHUD::ContentIconId(LatestContent,Links[Link].ToString()),Kind==2?17.f:Kind==1?7.f:5.f);
            }
            Card.Links->SetToolTipText(FText::FromString(LinksLine));
        }
        ShowLook(Card,bQuickShown&&bQuickCompact,Gate.bCanChoose&&!PendingRequest,bVisible);
        GrowthShown(Card.ElementChip,Card.bBig&&!Card.Element->GetText().IsEmpty(),ESlateVisibility::HitTestInvisible);
        GrowthShown(Card.Links,Card.bBig&&Card.LinkIcons.Num()>0&&Card.LinkIcons[0]->GetVisibility()!=ESlateVisibility::Collapsed,ESlateVisibility::Visible);
    }
    if(!bOffer){GrowthShown(SkipCard.Frame,false);return;}
    const int64 Points=Latest.ActiveOffer.SkipPoints;
    const FString SkipName=Points>0?FString::Printf(TEXT("不选 +%lld 点"),Points):FString(TEXT("不选"));
    const FString SkipKey=SkipName+TEXT("|")+Gate.ChooseBlocked;
    if(CardStale(SkipCard,SkipKey))
    {
        LDHUD::FActionDetailView Detail;
        Detail.Name=SkipName;
        Detail.Body=Points>0?FString(TEXT("放弃本组技能，本次机会不再保留。")):FString(TEXT("奖励数据缺失。"));
        if(!Gate.bCanChoose){Detail.Body+=TEXT("\n\n")+Gate.ChooseBlocked;}
        ShowFace(SkipCard,SkipKey,SkipName,FString(),Points>0?FString::Printf(TEXT("+%lld"),Points):FString(TEXT("+")),Detail);
        GrowthText(SkipCard.Facts,Points>0?LDTerms::OfferSkipPoints(Points):FString());
    }
    ShowLook(SkipCard,bQuickShown&&bQuickCompact,Points>0&&Gate.bCanChoose&&!PendingRequest,bVisible);
    // The skip card's name is only 不选; its points are the glyph on a tile and the facts line on the stage.
    GrowthText(SkipCard.Name,LDTerms::OfferSkip);GrowthShown(SkipCard.IconFrame,!SkipCard.bBig);
}
void ULDChoicesPanel::RefreshFinalMod()
{
    const bool bFinal=HasAvailableFinal();
    const bool bSelected=Usable()&&!Latest.FinalMod.IsEmpty();
    const auto SourceName=[this](ELDComponentTargetKind Kind,FName Id)->FString
    {
        return Kind==ELDComponentTargetKind::None?FString(TEXT("本人"))
            :Kind==ELDComponentTargetKind::Dash?FString(TEXT("冲刺(D)"))
            :Kind==ELDComponentTargetKind::Active?LDHUD::ActiveCard(Id,LatestContent,LatestStats).Name
            :LDHUD::ComponentCard(Id,LatestContent,LatestStats).Name;
    };
    const bool bDormant=FinalDormant();
    if((!bFinal&&!bDormant)||(bFinal&&ShownFinalMod!=Latest.FinalModOfferId)){PendingFinalOption=NAME_None;FinalSourcePick=INDEX_NONE;FinalColumnPick=INDEX_NONE;}
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
            if(!bSelected||Index!=0){GrowthShown(Card.Frame,false);continue;}
            const FString Bound=SourceName(Latest.FinalMod.TargetKind,Latest.FinalMod.TargetId);
            const FString Key=TEXT("FIN_SELECTED|")+Latest.FinalMod.DefinitionId.ToString()+TEXT("|")+Bound+(bDormant?TEXT("|D"):TEXT(""));
            if(CardStale(Card,Key))
            {
                auto View=LDHUD::FinalModCard(Latest.FinalMod.DefinitionId,LatestContent,LatestStats);
                View.Detail.Body+=bDormant?FString(TEXT("\n\n"))+LDTerms::FinalDormant:TEXT("\n\n来源：")+Bound;
                ShowFace(Card,Key,View.Name,View.IconId,View.Glyph,View.Detail);
            }
            GrowthText(Card.Facts,bDormant?FString(LDTerms::FinalDormant):LDTerms::FinalBindsTo(Bound));
            ShowLook(Card,false,true,bFinalVisible);GrowthEnabled(Card.Button,bDormant&&!PendingRequest);
            GrowthTint(Card.Button,bDormant?LDHUDStyle::UIReplace*.35f:LDHUDStyle::SlotArmed);
            continue;
        }
        const FName Id=Latest.FinalModOptions[Index];
        TArray<FString> Names;
        for(const FLDFinalModSourceView& Source:Latest.FinalModSources)
        {
            if(Source.Option!=Id){continue;}
            Names.Add(SourceName(Source.TargetKind,Source.TargetId));
        }
        const FString Outcome=Names.IsEmpty()?FString(TEXT("当前构筑没有合法触发来源。"))
            :!Gate.bCanFinalMod?Gate.FinalModBlocked:FString(TEXT("来源："))+FString::Join(Names,TEXT("、"))
                +TEXT("\n")+LDTerms::FinalTakenNote;
        const FString Key=TEXT("FIN|")+Id.ToString()+TEXT("|")+Outcome;
        LDHUD::FFinalModCardView View=LDHUD::FinalModCard(Id,LatestContent,LatestStats);
        if(CardStale(Card,Key))
        {
            View.Detail.Body+=TEXT("\n\n")+Outcome;
            ShowFace(Card,Key,View.Name,View.IconId,View.Glyph,View.Detail);
        }
        // The stage card names what it binds: 无需绑定 for the holder itself, the chosen or only source, or the choices.
        const bool bSelf=!Names.IsEmpty()&&Latest.FinalModSources.ContainsByPredicate([Id](const FLDFinalModSourceView& S){return S.Option==Id&&S.TargetKind==ELDComponentTargetKind::None;})
            &&Names.Num()==1;
        const FString Bind=Names.IsEmpty()?FString(TEXT("当前构筑没有合法触发来源。")):bSelf?FString(LDTerms::FinalNoBind)
            :PendingFinalOption==Id&&ShownFinalSources.IsValidIndex(FinalSourcePick)?LDTerms::FinalBindsTo(SourceName(ShownFinalSources[FinalSourcePick].TargetKind,ShownFinalSources[FinalSourcePick].TargetId))
            :LDTerms::FinalBindsTo(FString::Join(Names,TEXT(" / ")));
        GrowthText(Card.Facts,Bind);
        ShowLook(Card,false,View.bKnown&&!Names.IsEmpty()&&Gate.bCanFinalMod&&!PendingRequest,bFinalVisible);
        GrowthTint(Card.Button,PendingFinalOption==Id?(IsFinalStage()?LDHUDStyle::UIEvent*.35f:LDHUDStyle::SlotArmed):LDHUDStyle::CardIdle);
    }
    ShownFinalSources.Reset();ShownSourceColumns.Reset();
    if((bFinal||(bDormant&&PendingFinalOption==Latest.FinalMod.DefinitionId))&&!PendingFinalOption.IsNone())
    {
        for(const FLDFinalModSourceView& Source:Latest.FinalModSources)
        {
            if(Source.Option!=PendingFinalOption){continue;}
            ShownFinalSources.Add(Source);
            LDHUD::EStorageColumn Column=LDHUD::EStorageColumn::Body;
            ShownSourceColumns.Add(LDHUD::FinalSourceColumn(Latest,Source.TargetKind,Source.TargetId,Column)?int32(Column):INDEX_NONE);
        }
    }
    if(ShownFinalSources.Num()==1&&IsFinalStage()){FinalSourcePick=0;}
    if(!ShownFinalSources.IsValidIndex(FinalSourcePick)){FinalSourcePick=INDEX_NONE;}
    if(ShownSourceColumns.IsValidIndex(FinalSourcePick)){FinalColumnPick=ShownSourceColumns[FinalSourcePick];}
    int32 ColumnSources[LDHUD::StorageColumnCount]={};
    for(const int32 Column:ShownSourceColumns){if(Column>=0&&Column<LDHUD::StorageColumnCount){++ColumnSources[Column];}}
    if(FinalColumnPick<0||FinalColumnPick>=LDHUD::StorageColumnCount||ColumnSources[FinalColumnPick]==0){FinalColumnPick=INDEX_NONE;}
    // A holder-only card binds nothing: its page shows no source row.
    const bool bUniversal=ShownFinalSources.Num()==1&&ShownFinalSources[0].TargetKind==ELDComponentTargetKind::None;
    const bool bSourceVisible=IsFinalStage()||TrayShows(ETrayGroup::FinalSource);
    const bool bSourceTiles=!IsFinalStage()&&(ShownFinalSources.Num()>6||bIconOnlyTray);
    TrayRowTiles(FinalSourceRow,bSourceTiles);
    GrowthShown(FinalSourceRow,!ShownFinalSources.IsEmpty()&&!IsFinalStage()&&bSourceVisible);
    GrowthShown(StageSourceLine,!ShownFinalSources.IsEmpty()&&!bUniversal&&IsFinalStage(),ESlateVisibility::SelfHitTestInvisible);
    // On the page, only a picked column with several sources lists them (in the dock).
    const bool bColumnChoices=IsFinalStage()&&FinalColumnPick!=INDEX_NONE&&ColumnSources[FinalColumnPick]>1;
    GrowthShown(FinalSourceChoiceLine,bColumnChoices,ESlateVisibility::SelfHitTestInvisible);
    for(int32 Index=0;Index<ShownFinalSources.Num();++Index)
    {
        if(!FinalSourceCards.IsValidIndex(Index))
        {
            FTrayCard Card=MakeCard(FName(*FString::Printf(TEXT("GrowthFinalSource%d"),Index)),[this,Index](){if(IsFinalStage()){SelectFinalSource(Index);}else{ChooseFinalModSource(Index);}});
            if(IsFinalStage()){FinalSourceChoices->AddChildToHorizontalBox(Card.Frame)->SetPadding(FMargin(Index?12.f:0.f,0,0,0));}
            else{FinalSourceRow->AddChildToWrapBox(Card.Frame);}
            FinalSourceCards.Add(Card);
        }
        const FLDFinalModSourceView& Source=ShownFinalSources[Index];
        FString Name,Icon,Glyph;
        if(Source.TargetKind==ELDComponentTargetKind::Dash){Name=TEXT("冲刺(D)");Icon=LDUIIcons::Dash;Glyph=TEXT("D");}
        else if(Source.TargetKind==ELDComponentTargetKind::Active)
        {const auto Card=LDHUD::ActiveCard(Source.TargetId,LatestContent,LatestStats);Name=Card.Name;Icon=Card.IconId;Glyph=Card.Glyph;}
        else
        {const auto Card=LDHUD::ComponentCard(Source.TargetId,LatestContent,LatestStats);Name=Card.Name;Icon=Card.IconId;Glyph=Card.Glyph;}
        LDHUD::FActionDetailView Detail;
        Detail.Name=Name;
        Detail.Body=LDHUD::FinalModCard(PendingFinalOption,LatestContent,LatestStats).Name+TEXT("\n绑定此来源。");
        ShowFace(FinalSourceCards[Index],PendingFinalOption.ToString()+TEXT("|")+Name,Name,Icon,Glyph,Detail);
        const bool bShown=IsFinalStage()?bColumnChoices&&ShownSourceColumns[Index]==FinalColumnPick:bSourceVisible;
        ShowLook(FinalSourceCards[Index],bSourceTiles,Gate.bCanFinalMod&&!PendingRequest,bShown);
        GrowthTint(FinalSourceCards[Index].Button,IsFinalStage()&&Index==FinalSourcePick?LDHUDStyle::UIEvent*.45f:LDHUDStyle::CardIdle);
    }
    for(int32 Index=ShownFinalSources.Num();Index<FinalSourceCards.Num();++Index){GrowthShown(FinalSourceCards[Index].Frame,false);}
    // The page's 绑定来源 row: the board's five columns, lit where a legal source sits, the rest dimmed and unclickable.
    while(FinalColumnCards.Num()<LDHUD::StorageColumnCount)
    {
        const int32 Column=FinalColumnCards.Num();
        FTrayCard Card=MakeCard(FName(*FString::Printf(TEXT("GrowthFinalColumn%d"),Column)),[this,Column](){SelectFinalColumn(Column);});
        StageSources->AddChildToHorizontalBox(Card.Frame)->SetPadding(FMargin(Column?14.f:0.f,0,0,0));
        FinalColumnCards.Add(Card);
    }
    for(int32 Column=0;Column<FinalColumnCards.Num();++Column)
    {
        FTrayCard& Card=FinalColumnCards[Column];
        FString Name,Icon,Glyph;
        if(Column==int32(LDHUD::EStorageColumn::Body)){Name=LDTerms::ColumnBody;Glyph=LDHUD::IconGlyph(Name);}
        else if(Column==int32(LDHUD::EStorageColumn::Dash)){Name=LDTerms::Dash;Icon=LDUIIcons::Dash;Glyph=TEXT("D");}
        else
        {
            const uint8 ServerSlot=uint8(Column-int32(LDHUD::EStorageColumn::SlotQ));
            Name=SlotKeys.Of(ServerSlot);
            const FName Active=LDHUD::ActiveInSlot(Latest,ServerSlot);
            if(!Active.IsNone()){const auto Skill=LDHUD::ActiveCard(Active,LatestContent,LatestStats);Icon=Skill.IconId;Glyph=Skill.Glyph;}
            else{Glyph=TEXT("+");}
        }
        LDHUD::FActionDetailView Detail;Detail.Name=Name;
        TArray<FString> Here;
        for(int32 Index=0;Index<ShownFinalSources.Num();++Index)
        {
            if(ShownSourceColumns[Index]!=Column){continue;}
            const FLDFinalModSourceView& Source=ShownFinalSources[Index];
            Here.Add(Source.TargetKind==ELDComponentTargetKind::Dash?FString(TEXT("冲刺(D)"))
                :Source.TargetKind==ELDComponentTargetKind::Active?LDHUD::ActiveCard(Source.TargetId,LatestContent,LatestStats).Name
                :LDHUD::ComponentCard(Source.TargetId,LatestContent,LatestStats).Name);
        }
        Detail.Body=Here.IsEmpty()?FString(TEXT("这一列没有合法来源。")):LDTerms::FinalBindsTo(FString::Join(Here,TEXT(" / ")));
        const FString Key=TEXT("FINCOL|")+PendingFinalOption.ToString()+TEXT("|")+Name+TEXT("|")+Icon+TEXT("|")+Detail.Body;
        if(CardStale(Card,Key)){ShowFace(Card,Key,Name,Icon,Glyph,Detail);}
        const bool bLegal=ColumnSources[Column]>0;
        ShowLook(Card,false,bLegal&&Gate.bCanFinalMod&&!PendingRequest,IsFinalStage()&&!bUniversal&&!ShownFinalSources.IsEmpty());
        GrowthTint(Card.Button,Column==FinalColumnPick?LDHUDStyle::UIEvent*.45f:LDHUDStyle::CardIdle);
    }
}
bool ULDChoicesPanel::SelectFinalColumn(int32 Column)
{
    if(!IsFinalStage()||PendingFinalOption.IsNone()||Column<0||Column>=LDHUD::StorageColumnCount){return false;}
    TArray<int32> Here;
    for(int32 Index=0;Index<ShownSourceColumns.Num();++Index){if(ShownSourceColumns[Index]==Column){Here.Add(Index);}}
    if(Here.IsEmpty()){return false;}
    FinalColumnPick=Column;
    FinalSourcePick=Here.Num()==1?Here[0]:(Here.Contains(FinalSourcePick)?FinalSourcePick:INDEX_NONE);
    RefreshAll();return true;
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
    const bool bVisible=TrayShows(ETrayGroup::OpeningBase);
    GrowthShown(BaseRow,bShow&&bVisible);TrayRowTiles(BaseRow,bTiles);
    for(int32 Index=0;Index<FMath::Max(ShownBaseOptions.Num(),BaseCards.Num());++Index)
    {
        if(!ShownBaseOptions.IsValidIndex(Index)){GrowthShown(BaseCards[Index].Frame,false);continue;}
        FTrayCard& Card=BaseCardAt(Index);
        const FName Id=ShownBaseOptions[Index];
        Card.Component=Id;
        const FString Key=Id.ToString()+TEXT("|")+Gate.ChooseBlocked;
        if(CardStale(Card,Key))
        {
            // A plain base is taken and installed on 本体 at once; B18 goes to the storage, to be dragged onto the skill it
            // binds (#227). The server decides.
            const LDHUD::FComponentCardView View=LDHUD::ComponentCard(Id,LatestContent,LatestStats);
            const FString Outcome=!Gate.bCanChoose?Gate.ChooseBlocked:View.bBinding?FString(LDTerms::TakeToStorage):FString(LDTerms::TakeAndEquip);
            ShowFace(Card,Key,View.Name,View.IconId,View.Glyph,LDHUD::ComponentChoiceDetail(View,Outcome));
        }
        ShowLook(Card,bTiles,Gate.bCanChoose&&!PendingRequest,bVisible);
    }
}
ULDChoicesPanel::FChoiceRow& ULDChoicesPanel::ChoiceRowAt(int32 Index)
{
    while(ChoiceRows.Num()<=Index)
    {
        const int32 RowIndex=ChoiceRows.Num();
        FChoiceRow& Row=ChoiceRows.AddDefaulted_GetRef();
        Row.Row=TrayRow(WidgetTree);RowWidgets.Add(Row.Row);TrayRows.Add(Row.Row);
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
        const bool bVisible=TrayShows(ETrayGroup::ComponentChoice,Index);
        GrowthShown(Row.Row,bVisible);TrayRowTiles(Row.Row,bTiles);
        const FLDComponentChoiceView& Choice=Latest.ComponentChoices[Index];
        // Only one known material left is shown as it is (spec §4.2), never padded with a made-up second card.
        for(int32 Option=0;Option<Row.Options.Num();++Option)
        {
            FTrayCard& Card=Row.Options[Option];
            if(!Choice.Options.IsValidIndex(Option)){GrowthShown(Card.Frame,false);continue;}
            const FName Id=Choice.Options[Option];
            Card.Component=Id;
            const bool bOwned=LDHUD::OwnsComponent(Latest,Id);
            const FString Outcome=!Gate.bCanComponentChoice?Gate.ComponentChoiceBlocked:bOwned?FString(LDTerms::ComponentChoiceOwned):FString();
            const FString Key=Id.ToString()+TEXT("|")+Outcome;
            if(CardStale(Card,Key))
            {
                const LDHUD::FComponentCardView View=LDHUD::ComponentCard(Id,LatestContent,LatestStats);
                ShowFace(Card,Key,View.Name,View.IconId,View.Glyph,LDHUD::ComponentChoiceDetail(View,Outcome));
            }
            ShowLook(Card,bTiles,Gate.bCanComponentChoice&&!bOwned&&!PendingRequest,bVisible);
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
        ShowLook(Row.Points,bTiles,PoolPoints>0&&Gate.bCanComponentChoice&&!PendingRequest,bVisible);
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
            ShowFace(Card,Key,View.Name,View.IconId,View.Glyph,LDHUD::ActiveChoiceDetail(View,true,Outcome));
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
    // The trainer's buttons on the Q/W/E heads for the selected blueprint; the tray shows blueprints as cards
    // (RefreshBlueprintCards).
    const bool bShown=IsServiceShown()&&ServiceContext==ELDRestService::Trainer&&!ShownBlueprints.IsEmpty();
    GrowthShown(TrainerPicks,bShown);
    if(!ShownBlueprints.IsValidIndex(TrainerBlueprint)){TrainerBlueprint=0;}
    bool bEmpty[3]={false,false,false};
    for(uint8 Key=0;Key<3;++Key){bEmpty[Key]=LDHUD::ActiveInSlot(Latest,Key).IsNone();}
    // A full Q/W/E: the blueprint replaces an occupied slot (after a confirm).
    const FString ReplaceWhy=Gate.bInRest?ReplaceRefusal():FString(LDTerms::ReplaceAtRest);
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
bool ULDChoicesPanel::ChooseFinalMod(int32 Index)
{
    if(!Usable()||!Latest.bFinalModPending||!Latest.FinalModOptions.IsValidIndex(Index)||Latest.FinalModOfferId!=ShownFinalMod){return false;}
    if(PendingRequest){Tell(TEXT("上一个请求仍在处理中。"));return false;}
    if(!Gate.bCanFinalMod){Tell(TEXT("没有提交：")+Gate.FinalModBlocked);return false;}
    const FName Option=Latest.FinalModOptions[Index];
    TArray<FLDFinalModSourceView> Sources;
    for(const FLDFinalModSourceView& Source:Latest.FinalModSources){if(Source.Option==Option){Sources.Add(Source);}}
    if(Sources.IsEmpty()){Tell(TEXT("没有提交：当前构筑没有合法触发来源。"));return false;}
    if(Sources.Num()==1){return SubmitFinalMod(Option,Sources[0]);}
    PendingFinalOption=Option;RefreshAll();return true;
}
bool ULDChoicesPanel::ChooseFinalModSource(int32 Index)
{
    if(!ShownFinalSources.IsValidIndex(Index)||PendingFinalOption.IsNone()){return false;}
    return SubmitFinalMod(PendingFinalOption,ShownFinalSources[Index]);
}
bool ULDChoicesPanel::FinalDormant() const
{
    // A taken card whose bound source is no longer among its legal sources waits for a rebind (once the offer is closed the
    // view lists the taken card's legal sources).
    if(!Usable()||Latest.bFinalModPending||Latest.FinalMod.IsEmpty()){return false;}
    return !Latest.FinalModSources.ContainsByPredicate([this](const FLDFinalModSourceView& Source)
        {return Source.Option==Latest.FinalMod.DefinitionId&&Source.TargetKind==Latest.FinalMod.TargetKind&&Source.TargetId==Latest.FinalMod.TargetId;});
}
bool ULDChoicesPanel::RebindFinalMod()
{
    if(!FinalDormant()){return false;}
    if(PendingRequest){Tell(TEXT("上一个请求仍在处理中。"));return false;}
    if(!Gate.bCanInstall){Tell(TEXT("没有提交：")+Gate.InstallBlocked);return false;}
    const FName Option=Latest.FinalMod.DefinitionId;
    TArray<FLDFinalModSourceView> Sources;
    for(const FLDFinalModSourceView& Source:Latest.FinalModSources){if(Source.Option==Option){Sources.Add(Source);}}
    if(Sources.IsEmpty()){Tell(TEXT("没有提交：当前构筑没有合法触发来源。"));return false;}
    if(Sources.Num()==1){return SubmitFinalMod(Option,Sources[0]);}
    PendingFinalOption=Option;FinalSourcePick=INDEX_NONE;FinalColumnPick=INDEX_NONE;RefreshAll();return true;
}
bool ULDChoicesPanel::SubmitFinalMod(FName Option,const FLDFinalModSourceView& Source)
{
    const bool bRebind=FinalDormant()&&Option==Latest.FinalMod.DefinitionId;
    if(PendingRequest||!Usable()||(!bRebind&&(!Latest.bFinalModPending||Latest.FinalModOfferId!=ShownFinalMod))){return false;}
    if(bRebind?!Gate.bCanInstall:!Gate.bCanFinalMod){Tell(TEXT("没有提交：")+(bRebind?Gate.InstallBlocked:Gate.FinalModBlocked));return false;}
    if(!Latest.FinalModSources.ContainsByPredicate([&Source,Option](const FLDFinalModSourceView& Current)
        {return Current.Option==Option&&Current.TargetKind==Source.TargetKind&&Current.TargetId==Source.TargetId;}))
    {Tell(TEXT("没有提交：触发来源已变化。"));return false;}
    FLDBuildCommand Command;
    Command.Kind=ELDBuildCommandKind::FinalMod;Command.OfferId=bRebind?Latest.FinalModOfferId:ShownFinalMod;Command.Slot=FLDBuildCommand::AnySlot;
    Command.TargetKind=Source.TargetKind;
    if(!FLDDefinitionWire::Encode(Option,Command.Option)
        ||(!Source.TargetId.IsNone()&&!FLDDefinitionWire::Encode(Source.TargetId,Command.TargetDefinition)))
    {Tell(TEXT("没有提交：终局改装标识无效。"));return false;}
    PendingFinalOption=NAME_None;
    SubmittedFinal.DefinitionId=Option;SubmittedFinal.TargetKind=Source.TargetKind;SubmittedFinal.TargetId=Source.TargetId;
    bFinalFeedbackAllowed=bOpen;
    if(Send(Command)){return true;}
    SubmittedFinal=FLDFinalModSelection();bFinalFeedbackAllowed=false;return false;
}
bool ULDChoicesPanel::SelectFinalMod(int32 Index)
{
    // The stage only selects; the hold of GrowthFinalLock commits. One legal source is chosen at once.
    if(!Usable()||!Latest.bFinalModPending||!Latest.FinalModOptions.IsValidIndex(Index)||Latest.FinalModOfferId!=ShownFinalMod){return false;}
    PendingFinalOption=Latest.FinalModOptions[Index];FinalSourcePick=INDEX_NONE;FinalColumnPick=INDEX_NONE;
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
bool ULDChoicesPanel::SelectFinalSource(int32 Index)
{
    if(!ShownFinalSources.IsValidIndex(Index)||PendingFinalOption.IsNone()){return false;}
    FinalSourcePick=Index;
    if(ShownSourceColumns.IsValidIndex(Index)&&ShownSourceColumns[Index]!=INDEX_NONE){FinalColumnPick=ShownSourceColumns[Index];}
    RefreshAll();return true;
}
bool ULDChoicesPanel::LockFinalMod()
{
    if(PendingFinalOption.IsNone()){Tell(TEXT("没有提交：先选一张终局改装。"));return false;}
    if(!ShownFinalSources.IsValidIndex(FinalSourcePick)){Tell(TEXT("没有提交：先选绑定来源。"));return false;}
    const FLDFinalModSourceView Source=ShownFinalSources[FinalSourcePick];
    FinalSourcePick=INDEX_NONE;FinalColumnPick=INDEX_NONE;
    return SubmitFinalMod(PendingFinalOption,Source);
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
    // A confirm line whose terms changed (another slot content, a used blueprint, changed components, the rest over) is
    // withdrawn, never resent.
    if(RestConfirm.Kind!=ELDBuildCommandKind::None&&(!bShown||!RestConfirmHolds()))
    {
        const bool bMaterial=RestConfirm.Kind==ELDBuildCommandKind::FillGap||RestConfirm.Kind==ELDBuildCommandKind::Dismantle;
        RestConfirm=FRestConfirm();
        if(bShown){Tell(bMaterial?TEXT("构件已变化，请重新选择。"):TEXT("技能槽或图纸已变化，请重新选择。"));}
    }
    GrowthShown(RestConfirmLine,RestConfirm.Kind!=ELDBuildCommandKind::None,ESlateVisibility::SelfHitTestInvisible);
    const bool bServices=IsServiceShown();
    GrowthShown(ServiceDock,bServices&&(bFillGapOpen||bDismantleOpen||RestConfirm.Kind!=ELDBuildCommandKind::None),ESlateVisibility::Visible);
    GrowthShown(NpcBox,bServices&&ServiceContext==ELDRestService::SkillMerchant);
    GrowthShown(RespecBox,bServices&&ServiceContext==ELDRestService::Trainer);
    GrowthShown(ArtisanBox,bServices&&ServiceContext==ELDRestService::ComponentArtisan);
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
    {
        // The trainer's 洗点 says how many points it gives back.
        int64 Allocated=0;for(const FLDStatPointsView& Row:Latest.StatPoints){Allocated+=Row.Points;}
        GrowthText(Cast<UTextBlock>(RespecButton->GetContent()),Allocated>0?FString::Printf(TEXT("%s +%lld"),LDTerms::Respec,Allocated):FString(LDTerms::Respec));
    }
    const FString Forget=ForgetRefusal();
    bool bAnyLearned=false;
    for(uint8 Key=0;Key<3;++Key){bAnyLearned|=!LDHUD::ActiveInSlot(Latest,Key).IsNone();}
    const FString ForgetWhy=bAnyLearned?Forget:FString(TEXT("还没有学会的技能可遗忘"));
    GrowthText(ForgetBlocked,ForgetWhy);
    GrowthShown(ForgetBlocked,!ForgetWhy.IsEmpty(),ESlateVisibility::HitTestInvisible);
    RefreshMaterials();
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
        // Replacing, forgetting and dismantling are irreversible and commit on a full hold; filling a gap commits on a click.
        RestConfirmButton->SetHoldRequired(RestConfirm.Kind!=ELDBuildCommandKind::FillGap);
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
void ULDChoicesPanel::BuildDismantleEquation(int32 Inputs)
{
    // Inputs faces joined by "+", then "→" and the result's face; one name under each.
    if(!DismantleEquation||DismantleFaces.Num()==Inputs+1){return;}
    DismantleEquation->ClearChildren();DismantleFaces.Reset();DismantleNames.Reset();
    const auto Sign=[this](const TCHAR* Glyph)
    {
        UTextBlock* Mark=LDWidgets::Text(WidgetTree,Glyph,LDHUDStyle::TypeBanner,NAME_None,LDUIFonts::ERole::Title);
        Mark->SetColorAndOpacity(LDHUDStyle::TextSecondary);
        UHorizontalBoxSlot* MarkSlot=DismantleEquation->AddChildToHorizontalBox(Mark);
        MarkSlot->SetPadding(FMargin(14,14,14,0));MarkSlot->SetVerticalAlignment(VAlign_Top);
    };
    for(int32 Index=0;Index<=Inputs;++Index)
    {
        if(Index>0){Sign(Index==Inputs?TEXT("→"):TEXT("+"));}
        auto* Column=WidgetTree->ConstructWidget<UVerticalBox>();
        auto* Face=WidgetTree->ConstructWidget<USizeBox>();Face->SetWidthOverride(72.f);Face->SetHeightOverride(72.f);
        Column->AddChildToVerticalBox(Face)->SetHorizontalAlignment(HAlign_Center);
        UTextBlock* Name=LDWidgets::Text(WidgetTree,TEXT(""),LDHUDStyle::TypeCaption);
        Name->SetJustification(ETextJustify::Center);Name->SetAutoWrapText(false);Name->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
        auto* NameBox=WidgetTree->ConstructWidget<USizeBox>();NameBox->SetWidthOverride(112.f);NameBox->SetContent(Name);
        Column->AddChildToVerticalBox(NameBox)->SetPadding(FMargin(0,6,0,0));
        DismantleEquation->AddChildToHorizontalBox(Column)->SetVerticalAlignment(VAlign_Top);
        DismantleFaces.Add(Face);DismantleNames.Add(Name);
    }
}
void ULDChoicesPanel::PaintDismantle(const LDUIPaint::FContext& Paint) const
{
    if(!bDismantleOpen||DismantleFaces.IsEmpty()){return;}
    const FGeometry& Geometry=Paint.Geometry;
    const int32 PaintAt=Paint.Layer;
    const int32 Inputs=DismantleFaces.Num()-1;
    const double Now=UiNow();
    for(int32 Index=0;Index<DismantleFaces.Num();++Index)
    {
        const USizeBox* Face=DismantleFaces[Index];
        if(!Face){continue;}
        const FBox2D Box=LDUIShapes::RectIn(Geometry,Face->GetCachedGeometry());
        if(Index<Inputs&&DismantlePicked.IsValidIndex(Index))
        {
            const FName Id=DismantlePicked[Index];
            const LDWorkshop::FComponentFacts Facts=LDWorkshop::ComponentFacts(Id,LatestContent);
            Paint.Tile(PaintAt,Box,uint8(Facts.Kind),Facts.bScarce,LDUIPaint::ETileState::Selected,
                LDUIIcons::Find(LDHUD::ContentIconId(LatestContent,Id.ToString())),1.f,Now);
        }
        else if(Index<Inputs)
        {
            Paint.DashedEdge(PaintAt,LDUIShapes::EShape::Rounded,Box,10.f,LDHUDStyle::UIAction.CopyWithNewOpacity(.55f));
        }
        else
        {
            // The result: a common component drawn at random, shown as a gold-edged unknown.
            const bool bReady=int64(DismantlePicked.Num())==Materials.DismantleCount;
            Paint.Shape(PaintAt,LDUIShapes::EShape::Rounded,Box,10.f,LDUIPaint::FFill::Solid(LDHUDStyle::UIRaised));
            Paint.Edge(PaintAt+1,LDUIShapes::EShape::Rounded,Box,10.f,LDHUDStyle::UIAction.CopyWithNewOpacity(bReady?1.f:.4f),2.f);
            if(bReady){Paint.Halo(PaintAt,LDUIShapes::EShape::Rounded,Box,10.f,LDHUDStyle::UIAction.CopyWithNewOpacity(.35f),10.f);}
        }
    }
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
    // Dismantle: the service, then (while open) the equation of the picks; the candidates are picked on the library and the
    // board (the HUD hands DismantleSelectable to the storage), where installed ones show their detached installs orange.
    const int64 Count=Materials.DismantleCount;
    GrowthText(Cast<UTextBlock>(DismantleButton->GetContent()),Count>0?LDTerms::DismantleService(Count):FString(TEXT("拆解：数量数据缺失")));
    Service(DismantleButton,DismantleBlocked,Materials.DismantleBlocked);
    if(!Materials.DismantleBlocked.IsEmpty()){bDismantleOpen=false;}
    if(!bDismantleOpen){DismantlePicked.Reset();}
    DismantlePicked.RemoveAll([this](const FName& Id){return !Materials.DismantleCandidates.Contains(Id);});
    GrowthShown(DismantleSection,bDismantleOpen,ESlateVisibility::SelfHitTestInvisible);
    GrowthText(DismantleHeading,LDTerms::PickDismantle(Count)+TEXT("  ")+LDTerms::DismantlePicked(DismantlePicked.Num(),Count));
    if(bDismantleOpen&&Count>0)
    {
        BuildDismantleEquation(int32(FMath::Min<int64>(Count,8)));
        for(int32 Index=0;Index<DismantleNames.Num();++Index)
        {
            const bool bResult=Index==DismantleNames.Num()-1;
            GrowthText(DismantleNames[Index],bResult?FString(LDTerms::DismantleResult):DismantlePicked.IsValidIndex(Index)?Name(DismantlePicked[Index]):FString());
            DismantleNames[Index]->SetColorAndOpacity(bResult?LDHUDStyle::UIAction:LDHUDStyle::UIText);
        }
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
bool ULDChoicesPanel::PickDismantleComponent(FName Component)
{
    return PickDismantle(Materials.DismantleCandidates.IndexOfByKey(Component));
}
TArray<FName> ULDChoicesPanel::DismantleSelectable() const
{
    return bDismantleOpen&&!PendingRequest?Materials.DismantleCandidates:TArray<FName>();
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
bool ULDChoicesPanel::ForgetAtColumn(int32 Column)
{
    if(!IsServiceShown()||ServiceContext!=ELDRestService::Trainer){return false;}
    for(uint8 Key=0;Key<3;++Key){if(int32(LDHUD::SlotColumn(Key))==Column){AskForget(Key);return true;}}
    return false;
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
                    {OfferMotion.Chosen.Glyph=FString::Printf(TEXT("+%lld"),Receipt.PointsAwarded);}
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

void ULDChoicesPanel::ShowBig(FTrayCard& Card,bool bBig,float Width,float Icon)
{
    if(!Card.Frame){return;}
    if(Card.bBig==bBig&&(!bBig||FMath::IsNearlyEqual(Card.Frame->GetWidthOverride(),Width))){return;}
    Card.bBig=bBig;Card.bTile=false;Card.AppliedEnabled=-1;
    const float Scale=AppliedTextScale>0.f?AppliedTextScale:1.f;
    Card.Frame->SetWidthOverride(bBig?Width:TrayCardWidth);
    if(bBig){Card.Frame->ClearHeightOverride();}
    const float Face=bBig?Icon:TrayCardIcon;
    if(Face>0.f){Card.IconFrame->SetWidthOverride(Face);Card.IconFrame->SetHeightOverride(Face);}
    Card.Name->SetFont(LDUIFonts::Font(bBig?LDUIFonts::ERole::Title:LDUIFonts::ERole::Body,FMath::RoundToFloat((bBig?LDHUDStyle::TypeSection:LDHUDStyle::TypeBody)*Scale)));
    Card.Name->SetWrapTextAt(bBig?Width-2.f*18.f-8.f:TrayCardWidth-2.f*TrayCardPadding);
    Card.Facts->SetWrapTextAt(bBig?Width-2.f*18.f-8.f:0.f);
    GrowthShown(Card.Facts,bBig&&!Card.Facts->GetText().IsEmpty(),ESlateVisibility::HitTestInvisible);
    GrowthShown(Card.Tags,bBig&&!Card.Tags->GetText().IsEmpty(),ESlateVisibility::HitTestInvisible);
    if(!bBig){GrowthShown(Card.Links,false);GrowthShown(Card.ElementChip,false);GrowthShown(Card.IconFrame,true);}
    auto Style=Card.Button->GetStyle();
    const FMargin Inner=bBig?FMargin(18,22):FMargin(TrayCardPadding);Style.NormalPadding=Inner;Style.PressedPadding=Inner;Card.Button->SetStyle(Style);
}
void ULDChoicesPanel::UpdateStage()
{
    const bool bOffer=HasAvailableOffer();
    const bool bFinal=HasAvailableFinal();
    const bool bFinalNext=!bOffer&&bFinal;
    const FGuid Key=bOffer?Latest.ActiveOffer.OfferId:bFinal?Latest.FinalModOfferId:FGuid();
    const bool bShow=bOpen&&Places.bActive&&(bOffer||bFinal)&&!(Key.IsValid()&&Key==StageLeftFor);
    // A newly available choice takes priority over the previous choice's cosmetic flight.
    if(bShow&&OfferMotionActive()){OfferMotion.StartedAt=-1.;OfferMotion.Others.Reset();}
    // If the view advances while the stage remains visible, deal the new cards without an invisible hit area.
    if(bShow&&bShow==bStageShown&&bFinalNext==bStageFinal&&Key!=StageShownFor)
    {StageShownFor=Key;StageOpenedAt=UiNow();LDWidgets::PlayCue(this,ELDAudioCue::UIOfferDeal);return;}
    if(bShow==bStageShown&&(!bShow||bFinalNext==bStageFinal)){return;}
    StageShownFor=bShow?Key:FGuid();
    // Every card goes back to its tray row first, in order; the stage then takes the ones it shows.
    const auto Move=[](UWidget* Widget,UPanelWidget* To){if(Widget&&To&&Widget->GetParent()!=To){Widget->RemoveFromParent();To->AddChild(Widget);}};
    for(FTrayCard& Card:OfferCards){Move(Card.Frame,OfferRow);ShowBig(Card,false,0.f,0.f);}
    Move(SkipCard.Frame,OfferRow);ShowBig(SkipCard,false,0.f,0.f);
    for(FTrayCard& Card:FinalCards){Move(Card.Frame,FinalModRow);ShowBig(Card,false,0.f,0.f);}
    for(FTrayCard& Card:FinalSourceCards){Move(Card.Frame,FinalSourceRow);}
    bStageShown=bShow;bStageFinal=bFinalNext;
    if(!bShow){return;}
    StageOpenedAt=UiNow();
    LDWidgets::PlayCue(this,ELDAudioCue::UIOfferDeal);
    const auto OnStage=[this,&Move](FTrayCard& Card,UHorizontalBox* Row,float Gap,float Width,float Icon)
    {
        Move(Card.Frame,Row);
        if(UHorizontalBoxSlot* CardSlot=Cast<UHorizontalBoxSlot>(Card.Frame->Slot)){CardSlot->SetPadding(FMargin(Row->GetChildrenCount()>1?Gap:0.f,0,0,0));CardSlot->SetVerticalAlignment(VAlign_Center);}
        ShowBig(Card,true,Width,Icon);
    };
    if(!bStageFinal)
    {
        for(FTrayCard& Card:OfferCards){OnStage(Card,StageCards,26.f,300.f,176.f);}
        OnStage(SkipCard,StageCards,26.f,160.f,0.f);
        // The first card is in focus without a hover, so the dock reads something at once.
        if(PinnedPreview==INDEX_NONE){PinnedPreview=0;}
    }
    else
    {
        for(FTrayCard& Card:FinalCards){OnStage(Card,StageCards,30.f,330.f,200.f);}
        // A column with several legal sources lists them in the dock; the 绑定来源 row itself is the board's columns.
        for(int32 Index=0;Index<FinalSourceCards.Num();++Index)
        {
            FTrayCard& Card=FinalSourceCards[Index];
            Move(Card.Frame,FinalSourceChoices);
            if(UHorizontalBoxSlot* CardSlot=Cast<UHorizontalBoxSlot>(Card.Frame->Slot)){CardSlot->SetPadding(FMargin(Index?12.f:0.f,0,0,0));}
            ShowBig(Card,false,0.f,0.f);
        }
    }
    bCardsStale=true;
}
void ULDChoicesPanel::RefreshStage()
{
    GrowthShown(TrayFrame,(bOpen&&!bStageShown)||bQuickShown);
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
        GrowthShown(StageSourceLine,false);
        return;
    }
    GrowthText(StageTitle,LDTerms::FinalStageTitle);StageTitle->SetColorAndOpacity(LDHUDStyle::UIEvent);
    GrowthText(StageSub,LDTerms::FinalOnce);StageSub->SetColorAndOpacity(LDHUDStyle::UIEvent);
    // The selected card (else the first): its effect, cost and notes lines as the catalog splits them, else the whole
    // description; then its numbers line.
    const FName Shown=!PendingFinalOption.IsNone()?PendingFinalOption:Latest.FinalModOptions.IsValidIndex(0)?Latest.FinalModOptions[0]:NAME_None;
    FString Description;
    LDHUD::ContentText(LatestContent,Shown.ToString(),true,Description);
    FString Numbers=LDHUD::FinalModCard(Shown,LatestContent,LatestStats).Detail.Body;
    Numbers.RemoveFromStart(Description);Numbers.TrimStartAndEndInline();
    FString Parts[3];
    const bool bSplit=LDHUD::FinalModParts(LatestContent,Shown,Parts[0],Parts[1],Parts[2]);
    for(int32 Part=0;Part<FinalPartRows.Num()&&Part<3;++Part)
    {
        GrowthText(FinalPartTexts[Part],bSplit?Parts[Part]:FString());
        GrowthShown(FinalPartRows[Part],bSplit&&!Parts[Part].IsEmpty(),ESlateVisibility::SelfHitTestInvisible);
    }
    GrowthText(FinalDescText,bSplit?FString():Description);
    GrowthShown(FinalDescText,!bSplit);
    GrowthText(FinalNumbersText,Numbers);
    GrowthShown(FinalNumbersText,!Numbers.IsEmpty());
    GrowthText(FinalLockNote,LDTerms::FinalTakenNote);
    GrowthEnabled(FinalLockButton,!PendingFinalOption.IsNone()&&ShownFinalSources.IsValidIndex(FinalSourcePick)&&Gate.bCanFinalMod&&!PendingRequest);
}
void ULDChoicesPanel::PlaceOnLayer(UWidget* Widget,const FSlateRect& Absolute,FVector2D Alignment,bool bFill)
{
    UCanvasPanelSlot* const* Found=LayerSlots.Find(Widget);
    if(!Found||!*Found||Absolute.Right<=Absolute.Left||Absolute.Bottom<=Absolute.Top){return;}
    const FGeometry& Geometry=GetCachedGeometry();
    const FVector2D Min=Geometry.AbsoluteToLocal(FVector2D(Absolute.Left,Absolute.Top));
    const FVector2D Max=Geometry.AbsoluteToLocal(FVector2D(Absolute.Right,Absolute.Bottom));
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
        PlaceOnLayer(StageBox,Places.Stage,FVector2D::ZeroVector,true);
        // Bottom-left of the stage, clear of the phase bar above and the rest actions in the top-right corner.
        const FSlateRect& Stage=Places.Stage;
        const FGeometry& Geometry=GetCachedGeometry();
        const float Scale=FMath::Max(.01f,Geometry.Scale);
        PlaceOnLayer(StageBack,FSlateRect(Stage.Left+16.f*Scale,Stage.Top,Stage.Right,Stage.Bottom-16.f*Scale),FVector2D(0.f,1.f));
        const FSlateRect& Dock=Places.Dock;
        const FSlateRect Description(Dock.Left+28.f*Scale,Dock.Top+16.f*Scale,Dock.Right-340.f*Scale,Dock.Bottom-16.f*Scale);
        const float DescriptionWidth=FMath::Max(1.f,float(Geometry.AbsoluteToLocal(FVector2D(Description.Right,Description.Top)).X
            -Geometry.AbsoluteToLocal(FVector2D(Description.Left,Description.Top)).X));
        for(UTextBlock* Text:{FinalDescText.Get(),FinalNumbersText.Get()})
        {if(Text&&!FMath::IsNearlyEqual(Text->GetWrapTextAt(),DescriptionWidth,.5f)){Text->SetWrapTextAt(DescriptionWidth);}}
        // A part line wraps beside its label chip.
        for(UTextBlock* Text:FinalPartTexts)
        {
            const UPanelWidget* Row=Text?Text->GetParent():nullptr;
            const UWidget* Chip=Row?Row->GetChildAt(0):nullptr;
            const float Wrap=FMath::Max(1.f,DescriptionWidth-(Chip?float(Chip->GetDesiredSize().X):48.f)-14.f);
            if(Text&&!FMath::IsNearlyEqual(Text->GetWrapTextAt(),Wrap,.5f)){Text->SetWrapTextAt(Wrap);}
        }
        PlaceOnLayer(FinalDesc,Description,FVector2D::ZeroVector,true);
        const FSlateRect Actions(Dock.Right-320.f*Scale,Dock.Top,Dock.Right-28.f*Scale,Dock.Bottom);
        PlaceOnLayer(FinalLockButton,Actions,FVector2D(.5f,.4f));
        PlaceOnLayer(FinalLockNote,Actions,FVector2D(.5f,.78f));
    }
    if(!IsServiceShown()){return;}
    PlaceOnLayer(RespecBox,Places.CharacterFoot,FVector2D::ZeroVector,true);
    PlaceOnLayer(NpcBox,Places.PendingTop,FVector2D(0.f,0.f));
    PlaceOnLayer(TrainerPicks,Places.PendingTop,FVector2D(0.f,0.f));
    PlaceOnLayer(ArtisanBox,Places.LibraryTop,FVector2D(0.f,0.f));
    PlaceOnLayer(ServiceDock,Places.Dock,FVector2D::ZeroVector,true);
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
    LDWidgets::RefitScale(ServiceFit);
    LDWidgets::RefitScale(Cast<UScaleBox>(StageBox.Get()));
    LDWidgets::RefitScale(Cast<UScaleBox>(FinalDesc.Get()));
    // The stage cards turn in one after another (LDHUDStyle::MotionOfferStagger apart) and land with the back curve.
    if(!bStageShown||!StageCards){return;}
    const double Age=UiNow()-StageOpenedAt;
    for(int32 Index=0;Index<StageCards->GetChildrenCount();++Index)
    {
        UWidget* Card=StageCards->GetChildAt(Index);
        const float Progress=LDTween::Progress(Age-Index*LDHUDStyle::MotionOfferStagger,LDHUDStyle::MotionOfferCards,LDTween::ECurve::Back);
        LDTween::SetScale(Card,FMath::Lerp(.86f,1.f,Progress));
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
    // The focused offer card wears the action yellow; the selected final card and source the event pink.
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
        if(FinalSourceCards.IsValidIndex(FinalSourcePick)){Edge(FinalSourceCards[FinalSourcePick],LDHUDStyle::UIEvent);}
        // Mockup 06: legal source columns wear a thin pink frame, the picked one the full glow.
        for(int32 Column=0;Column<FinalColumnCards.Num();++Column)
        {
            const FTrayCard& Card=FinalColumnCards[Column];
            if(!Card.Frame||!Card.Frame->IsVisible()||!Card.Button||!Card.Button->GetIsEnabled()){continue;}
            if(Column==FinalColumnPick){Edge(Card,LDHUDStyle::UIEvent);continue;}
            const FBox2D Box=LDUIShapes::RectIn(G,Card.Frame->GetCachedGeometry());
            Paint.Frame(Base+1,LDUIShapes::EShape::Rounded,Box,LDHUDStyle::UIEvent.CopyWithNewOpacity(.55f),1.5f,LDHUDStyle::RadiusPanel);
        }
    }
    return OfferMotionActive()?Base+9:Base+2;
}
