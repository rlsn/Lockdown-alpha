#include "UI/LDChoicesPanel.h"
#include "Core/LDLog.h"
#include "UI/LDHUDModel.h"
#include "UI/LDHUDStyle.h"
#include "UI/LDTerms.h"
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
            auto* OfferLine=WidgetTree->ConstructWidget<UHorizontalBox>();
            OfferLine->SetVisibility(ESlateVisibility::Collapsed);OfferGroup=OfferLine;
            OfferNumber=Text(TEXT(""),LDHUDStyle::TypeBody);OfferNumber->SetAutoWrapText(false);OfferNumber->SetColorAndOpacity(LDHUDStyle::TextAccent);
            ScaleNewText(OfferNumber);RowWidgets.Add(OfferNumber);
            OfferLine->AddChildToHorizontalBox(OfferNumber)->SetVerticalAlignment(VAlign_Center);
            OfferRow=TrayRow(WidgetTree);
            auto* OfferFrame=WidgetTree->ConstructWidget<UBorder>();
            OfferFrame->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White,8.f,LDHUDStyle::PanelRim,1.f));
            OfferFrame->SetBrushColor(LDHUDStyle::PanelFill);OfferFrame->SetPadding(FMargin(4));OfferFrame->SetContent(OfferRow);
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
                FTrayCard Card=MakeCard(FName(*FString::Printf(TEXT("GrowthFinal%d"),Index)),[this,Index](){if(IsFinalStage()){SelectFinalMod(Index);}else{ChooseFinalMod(Index);}});
                FinalModRow->AddChildToWrapBox(Card.Frame);FinalCards.Add(Card);
            }
            TrayFrame=WidgetTree->ConstructWidget<UBorder>();TrayFrame->SetBrushColor(FLinearColor::Transparent);TrayFrame->SetPadding(FMargin(0,0,16,72));
            TrayFrame->SetHorizontalAlignment(HAlign_Right);TrayFrame->SetVerticalAlignment(VAlign_Bottom);
            TrayFrame->SetVisibility(ESlateVisibility::Collapsed);TrayFrame->SetContent(Tray);
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
            ForgetBlocked=Wrapped(LDHUDStyle::TypeCaption,300.f);ForgetBlocked->SetColorAndOpacity(LDHUDStyle::TextSecondary);
            Cast<UVerticalBox>(RespecBox.Get())->AddChildToVerticalBox(ForgetBlocked)->SetPadding(FMargin(0,2,0,0));
            for(uint8 Key=0;Key<3;++Key)
            {
                ULDCommandButton* Forget=LDWidgets::Button(WidgetTree,LDTerms::ForgetShort,[this,Key](){AskForget(Key);},false,
                    FName(*FString::Printf(TEXT("GrowthRestForget%s"),*LDHUD::SlotKey(Key))));
                Forget->SetVisibility(ESlateVisibility::Collapsed);ForgetButtons.Add(Forget);
            }
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
                auto* Picker=WidgetTree->ConstructWidget<UVerticalBox>();
                DismantleHeading=OneLine(LDHUDStyle::TypeCaption);Picker->AddChildToVerticalBox(DismantleHeading);
                DismantleList=WidgetTree->ConstructWidget<UVerticalBox>();Picker->AddChildToVerticalBox(DismantleList)->SetPadding(FMargin(0,2,0,0));
                Picker->SetVisibility(ESlateVisibility::Collapsed);
                DismantleSection=Picker;
                Body->AddChildToVerticalBox(Picker)->SetPadding(FMargin(0,4,0,0));
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
            UBorder* Dock=LDWidgets::Panel(WidgetTree,Body,FMargin(24,18));Dock->SetBrushColor(LDHUDStyle::UIRaised.CopyWithNewOpacity(.98f));
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
            Box->SetVisibility(ESlateVisibility::Collapsed);StageBox=Box;
            StageBack=LDWidgets::Button(WidgetTree,LDTerms::Choice,[this](){LeaveStage();},false,TEXT("GrowthStageBack"));
            StageBack->SetVisibility(ESlateVisibility::Collapsed);
            // The final mod's description whole (its sentences carry no reliable effect/cost/notes boundary), its numbers, and the hold that locks it.
            auto* Desc=WidgetTree->ConstructWidget<UVerticalBox>();
            FinalDescText=Wrapped(LDHUDStyle::TypeText,ServicePanelWidth+300.f);FinalDescText->SetColorAndOpacity(LDHUDStyle::UIText);
            Desc->AddChildToVerticalBox(FinalDescText)->SetPadding(FMargin(0,8,0,0));
            FinalNumbersText=Wrapped(LDHUDStyle::TypeCaption,ServicePanelWidth+300.f);FinalNumbersText->SetColorAndOpacity(LDHUDStyle::TextSecondary);
            Desc->AddChildToVerticalBox(FinalNumbersText)->SetPadding(FMargin(0,8,0,0));
            Desc->SetVisibility(ESlateVisibility::Collapsed);FinalDesc=Desc;
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
        for(ULDCommandButton* Forget:ForgetButtons){UCanvasPanelSlot* ItemSlot=Layer->AddChildToCanvas(Forget);ItemSlot->SetAutoSize(true);ItemSlot->SetZOrder(2);LayerSlots.Add(Forget,ItemSlot);}
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
    Card.Name=LDWidgets::Text(WidgetTree,TEXT(""),LDHUDStyle::TypeBody);Card.Name->SetAutoWrapText(false);Card.Name->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
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
        UTextBlock* Label=LDWidgets::Text(WidgetTree,LDTerms::OfferLinks,LDHUDStyle::TypeTiny);Label->SetColorAndOpacity(LDHUDStyle::TextSecondary);RowWidgets.Add(Label);
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
        const bool bPicture=LDUIIcons::Apply(Card.Picture,Card.AppliedIcon,TrayIconRadius,bEnabled?FLinearColor::White:LDHUDStyle::IconBlocked);
        GrowthShown(Card.Glyph,!bPicture,ESlateVisibility::HitTestInvisible);
    }
}
void ULDChoicesPanel::SetTrayInset(float Right,float Bottom)
{
    const FMargin Inset(0,0,Right,Bottom);
    if(TrayFrame&&!(TrayFrame->GetPadding()==Inset)){TrayFrame->SetPadding(Inset);}
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
    return Usable()&&(LDHUD::GrowthPending(&Latest,LatestMatch,LatestContent).Any()||!Latest.FinalMod.IsEmpty());
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
    PinnedPreview=INDEX_NONE;PendingFinalOption=NAME_None;FinalSourcePick=INDEX_NONE;bOpen=false;
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
    Gate=Usable()?LDHUD::BuildGate(Stage,LifePhase,Latest.bBuildLocked):LDHUD::FBuildGateView();
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
    RefreshOffer();RefreshFinalMod();RefreshOpeningBase(bTiles||(bQuickShown&&bQuickCompact));RefreshComponentChoices(bTiles||(bQuickShown&&bQuickCompact));
    RefreshBlueprintCards();RefreshBlueprints();RefreshRest();
    GrowthShown(ChoiceRowList,!ShownChoices.IsEmpty());
    RefreshStage();
    LayoutLayer();
    bCardsStale=false;
}
void ULDChoicesPanel::RefreshOffer()
{
    const bool bOffer=Usable()&&Latest.bHasActiveOffer&&!Latest.ActiveOffer.Options.IsEmpty();
    // A new offer drops the pinned popup; ids and revisions travel with the click, never a card index.
    if(!bOffer||Latest.ActiveOffer.OfferId!=ShownOffer){PinnedPreview=INDEX_NONE;}
    ShownOffer=bOffer?Latest.ActiveOffer.OfferId:FGuid();ShownOfferRevision=bOffer?Latest.ActiveOffer.OfferRevision:0;
    // A queued opportunity without an eligible candidate waits as one grey card; its description says why.
    const bool bWaiting=!bOffer&&Usable()&&Latest.QueuedActiveOpportunities>0;
    GrowthShown(OfferGroup,bOffer||bWaiting);
    GrowthShown(OfferRow,bOffer||bWaiting);
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
        ShowLook(Card,bQuickShown&&bQuickCompact,false);
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
        const FString Key=Id.ToString()+TEXT("|")+Outcome+FString::Printf(TEXT("|%llu"),Latest.BuildRevision);
        if(CardStale(Card,Key))
        {
            const LDHUD::FActiveCardView View=LDHUD::ActiveCard(Id,LatestContent,LatestStats);
            LDHUD::FActionDetailView Detail=LDHUD::ActiveChoiceDetail(View,false,Outcome);
            // The evidence for the choice: the owned components that go on it and the reactions with the learned actives.
            const TArray<FName> Links=LDWorkshop::LinkableComponents(Id,Latest,LatestContent);
            TArray<FString> Lines;
            if(!Links.IsEmpty())
            {
                TArray<FString> Names;for(const FName& Link:Links){Names.Add(LDHUD::ContentName(LatestContent,Link.ToString()));}
                Lines.Add(LDTerms::OfferLinksLine(FString::Join(Names,TEXT("、"))));
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
            GrowthText(Card.Facts,View.Target+TEXT("    ")+View.Cost);
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
            Card.Links->SetToolTipText(FText::FromString(Links.IsEmpty()?FString():Lines[0]));
        }
        ShowLook(Card,bQuickShown&&bQuickCompact,Gate.bCanChoose&&!PendingRequest);
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
        ShowFace(SkipCard,SkipKey,SkipName,FString(),TEXT("+"),Detail);
        GrowthText(SkipCard.Facts,Points>0?LDTerms::OfferSkipPoints(Points):FString());
    }
    ShowLook(SkipCard,bQuickShown&&bQuickCompact,Points>0&&Gate.bCanChoose&&!PendingRequest);
    // On the stage the skip card is narrow: 不选 over its points.
    if(SkipCard.bBig){GrowthText(SkipCard.Name,LDTerms::OfferSkip);GrowthShown(SkipCard.IconFrame,false);}
    else{GrowthText(SkipCard.Name,SkipName);GrowthShown(SkipCard.IconFrame,true);}
}
void ULDChoicesPanel::RefreshFinalMod()
{
    const bool bFinal=Usable()&&Latest.bFinalModPending&&Latest.FinalModOptions.Num()==3;
    const bool bSelected=Usable()&&!Latest.FinalMod.IsEmpty();
    const auto SourceName=[this](ELDComponentTargetKind Kind,FName Id)->FString
    {
        return Kind==ELDComponentTargetKind::None?FString(TEXT("本人"))
            :Kind==ELDComponentTargetKind::Dash?FString(TEXT("冲刺(D)"))
            :Kind==ELDComponentTargetKind::Active?LDHUD::ActiveCard(Id,LatestContent,LatestStats).Name
            :LDHUD::ComponentCard(Id,LatestContent,LatestStats).Name;
    };
    if(!bFinal||ShownFinalMod!=Latest.FinalModOfferId){PendingFinalOption=NAME_None;FinalSourcePick=INDEX_NONE;}
    ShownFinalMod=bFinal?Latest.FinalModOfferId:FGuid();
    GrowthShown(FinalModGroup,bFinal||bSelected);GrowthShown(FinalModRow,bFinal||bSelected);
    for(int32 Index=0;Index<FinalCards.Num();++Index)
    {
        FTrayCard& Card=FinalCards[Index];
        if(!bFinal)
        {
            if(!bSelected||Index!=0){GrowthShown(Card.Frame,false);continue;}
            const FString Bound=SourceName(Latest.FinalMod.TargetKind,Latest.FinalMod.TargetId);
            const FString Key=TEXT("FIN_SELECTED|")+Latest.FinalMod.DefinitionId.ToString()+TEXT("|")+Bound;
            if(CardStale(Card,Key))
            {
                auto View=LDHUD::FinalModCard(Latest.FinalMod.DefinitionId,LatestContent,LatestStats);
                View.Detail.Body+=TEXT("\n\n来源：")+Bound+TEXT("\n本局构筑已锁定。");
                ShowFace(Card,Key,View.Name,View.IconId,View.Glyph,View.Detail);
            }
            ShowLook(Card,false,true);GrowthEnabled(Card.Button,false);
            GrowthTint(Card.Button,LDHUDStyle::SlotArmed);
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
                +TEXT("\n选定后锁定本局构筑；保留技能、构件和工坊容量。");
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
        ShowLook(Card,false,View.bKnown&&!Names.IsEmpty()&&Gate.bCanFinalMod&&!PendingRequest);
        GrowthTint(Card.Button,PendingFinalOption==Id?(IsFinalStage()?LDHUDStyle::UIEvent*.35f:LDHUDStyle::SlotArmed):LDHUDStyle::CardIdle);
    }
    ShownFinalSources.Reset();
    if(bFinal&&!PendingFinalOption.IsNone())
    {for(const FLDFinalModSourceView& Source:Latest.FinalModSources){if(Source.Option==PendingFinalOption){ShownFinalSources.Add(Source);}}}
    if(ShownFinalSources.Num()==1&&IsFinalStage()){FinalSourcePick=0;}
    if(!ShownFinalSources.IsValidIndex(FinalSourcePick)){FinalSourcePick=INDEX_NONE;}
    GrowthShown(FinalSourceRow,!ShownFinalSources.IsEmpty()&&!IsFinalStage());
    GrowthShown(StageSourceLine,!ShownFinalSources.IsEmpty()&&IsFinalStage(),ESlateVisibility::SelfHitTestInvisible);
    for(int32 Index=0;Index<ShownFinalSources.Num();++Index)
    {
        if(!FinalSourceCards.IsValidIndex(Index))
        {
            FTrayCard Card=MakeCard(FName(*FString::Printf(TEXT("GrowthFinalSource%d"),Index)),[this,Index](){if(IsFinalStage()){SelectFinalSource(Index);}else{ChooseFinalModSource(Index);}});
            if(IsFinalStage()){StageSources->AddChildToHorizontalBox(Card.Frame)->SetPadding(FMargin(Index?12.f:0.f,0,0,0));}
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
        Detail.Body=LDHUD::FinalModCard(PendingFinalOption,LatestContent,LatestStats).Name+TEXT("\n绑定此来源，确认后锁定构筑。");
        ShowFace(FinalSourceCards[Index],PendingFinalOption.ToString()+TEXT("|")+Name,Name,Icon,Glyph,Detail);
        ShowLook(FinalSourceCards[Index],IsFinalStage(),Gate.bCanFinalMod&&!PendingRequest);
        GrowthTint(FinalSourceCards[Index].Button,IsFinalStage()&&Index==FinalSourcePick?LDHUDStyle::UIEvent*.45f:LDHUDStyle::CardIdle);
    }
    for(int32 Index=ShownFinalSources.Num();Index<FinalSourceCards.Num();++Index){GrowthShown(FinalSourceCards[Index].Frame,false);}
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
            Card.Component=Id;
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
bool ULDChoicesPanel::SkipOffer()
{
    if(!Usable()||!Latest.bHasActiveOffer||Latest.ActiveOffer.OfferId!=ShownOffer||Latest.ActiveOffer.SkipPoints<=0){return false;}
    if(PendingRequest){Tell(TEXT("上一个请求仍在处理中。"));return false;}
    if(!Gate.bCanChoose){Tell(TEXT("没有提交：")+Gate.ChooseBlocked);return false;}
    FLDBuildCommand Command;
    Command.Kind=ELDBuildCommandKind::SkipActiveOffer;
    Command.OfferId=Latest.ActiveOffer.OfferId;Command.OfferRevision=Latest.ActiveOffer.OfferRevision;
    PinnedPreview=INDEX_NONE;
    return Send(Command);
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
bool ULDChoicesPanel::SubmitFinalMod(FName Option,const FLDFinalModSourceView& Source)
{
    if(PendingRequest||!Usable()||!Latest.bFinalModPending||Latest.FinalModOfferId!=ShownFinalMod){return false;}
    if(!Gate.bCanFinalMod){Tell(TEXT("没有提交：")+Gate.FinalModBlocked);return false;}
    if(!Latest.FinalModSources.ContainsByPredicate([&Source,Option](const FLDFinalModSourceView& Current)
        {return Current.Option==Option&&Current.TargetKind==Source.TargetKind&&Current.TargetId==Source.TargetId;}))
    {Tell(TEXT("没有提交：触发来源已变化。"));return false;}
    FLDBuildCommand Command;
    Command.Kind=ELDBuildCommandKind::FinalMod;Command.OfferId=ShownFinalMod;Command.Slot=FLDBuildCommand::AnySlot;
    Command.TargetKind=Source.TargetKind;
    if(!FLDDefinitionWire::Encode(Option,Command.Option)
        ||(!Source.TargetId.IsNone()&&!FLDDefinitionWire::Encode(Source.TargetId,Command.TargetDefinition)))
    {Tell(TEXT("没有提交：终局改装标识无效。"));return false;}
    PendingFinalOption=NAME_None;
    return Send(Command);
}
bool ULDChoicesPanel::SelectFinalMod(int32 Index)
{
    // The stage only selects; the hold of GrowthFinalLock commits. One legal source is chosen at once.
    if(!Usable()||!Latest.bFinalModPending||!Latest.FinalModOptions.IsValidIndex(Index)||Latest.FinalModOfferId!=ShownFinalMod){return false;}
    PendingFinalOption=Latest.FinalModOptions[Index];FinalSourcePick=INDEX_NONE;
    RefreshAll();return true;
}
bool ULDChoicesPanel::SelectFinalSource(int32 Index)
{
    if(!ShownFinalSources.IsValidIndex(Index)||PendingFinalOption.IsNone()){return false;}
    FinalSourcePick=Index;RefreshAll();return true;
}
bool ULDChoicesPanel::LockFinalMod()
{
    if(PendingFinalOption.IsNone()){Tell(TEXT("没有提交：先选一张终局改装。"));return false;}
    if(!ShownFinalSources.IsValidIndex(FinalSourcePick)){Tell(TEXT("没有提交：先选绑定来源。"));return false;}
    const FLDFinalModSourceView Source=ShownFinalSources[FinalSourcePick];
    FinalSourcePick=INDEX_NONE;
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
    if(!bShown){CloseMaterialPickers();for(ULDCommandButton* Forget:ForgetButtons){GrowthShown(Forget,false);}return;}
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
    for(uint8 Key=0;Key<3&&ForgetButtons.IsValidIndex(Key);++Key)
    {
        const FName Active=LDHUD::ActiveInSlot(Latest,Key);
        GrowthShown(ForgetButtons[Key],bServices&&ServiceContext==ELDRestService::Trainer&&!Active.IsNone(),ESlateVisibility::Visible);
        if(Active.IsNone()){continue;}
        bAnyLearned=true;
        const FText ForgetTip=FText::FromString(LDTerms::ForgetSlot(SlotKeys.Of(Key),LDHUD::ContentName(LatestContent,Active.ToString())));
        if(!ForgetButtons[Key]->GetToolTipText().EqualTo(ForgetTip)){ForgetButtons[Key]->SetToolTipText(ForgetTip);}
        GrowthTint(ForgetButtons[Key],LDHUDStyle::UIReject*.35f);
        GrowthEnabled(ForgetButtons[Key],Forget.IsEmpty()&&!PendingRequest);
    }
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
    // The moments the receipt confirms get their cue: an active chosen, the final mod locked, a refusal.
    if(Receipt.Header.Outcome==ELDReceiptOutcome::Applied)
    {
        if(Receipt.Kind==ELDBuildCommandKind::ActiveChoice){LDWidgets::PlayCue(this,ELDAudioCue::UIChoose);}
        else if(Receipt.Kind==ELDBuildCommandKind::FinalMod){LDWidgets::PlayCue(this,ELDAudioCue::UIFinalLock);}
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
    const float Face=bBig?Icon:TrayCardIcon;
    if(Face>0.f){Card.IconFrame->SetWidthOverride(Face);Card.IconFrame->SetHeightOverride(Face);}
    Card.Name->SetFont(LDUIFonts::Font(bBig?LDUIFonts::ERole::Title:LDUIFonts::ERole::Body,FMath::RoundToFloat((bBig?LDHUDStyle::TypeSection:LDHUDStyle::TypeBody)*Scale)));
    GrowthShown(Card.Facts,bBig&&!Card.Facts->GetText().IsEmpty(),ESlateVisibility::HitTestInvisible);
    GrowthShown(Card.Tags,bBig&&!Card.Tags->GetText().IsEmpty(),ESlateVisibility::HitTestInvisible);
    if(!bBig){GrowthShown(Card.Links,false);GrowthShown(Card.ElementChip,false);GrowthShown(Card.IconFrame,true);}
    auto Style=Card.Button->GetStyle();
    const FMargin Inner=bBig?FMargin(18,22):FMargin(TrayCardPadding);Style.NormalPadding=Inner;Style.PressedPadding=Inner;Card.Button->SetStyle(Style);
}
void ULDChoicesPanel::UpdateStage()
{
    const bool bOffer=Usable()&&Latest.bHasActiveOffer&&!Latest.ActiveOffer.Options.IsEmpty();
    const bool bFinal=Usable()&&Latest.bFinalModPending&&Latest.FinalModOptions.Num()==3;
    const bool bFinalNext=!bOffer&&bFinal;
    const FGuid Key=bOffer?Latest.ActiveOffer.OfferId:bFinal?Latest.FinalModOfferId:FGuid();
    const bool bShow=bOpen&&Places.bActive&&(bOffer||bFinal)&&!(Key.IsValid()&&Key==StageLeftFor);
    // The next queued offer deals its cards again a moment after the previous choice (LDHUDStyle::MotionChoose).
    if(bShow&&bShow==bStageShown&&bFinalNext==bStageFinal&&Key!=StageShownFor)
    {StageShownFor=Key;StageOpenedAt=UiNow()+LDHUDStyle::MotionChoose;LDWidgets::PlayCue(this,ELDAudioCue::UIOfferDeal);return;}
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
        for(FTrayCard& Card:FinalSourceCards){OnStage(Card,StageSources,12.f,TrayTileSize,0.f);ShowBig(Card,false,0.f,0.f);}
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
        GrowthText(StageSub,FString::Printf(TEXT("Lv %d    %s"),int32(Latest.Level),*LDTerms::OfferChanceOf(Latest.ActiveOffer.OpportunityOrdinal,Latest.ActiveOpportunitiesEarned)));
        StageSub->SetColorAndOpacity(LDHUDStyle::TextSecondary);
        GrowthShown(StageSourceLine,false);
        return;
    }
    GrowthText(StageTitle,LDTerms::FinalStageTitle);StageTitle->SetColorAndOpacity(LDHUDStyle::UIEvent);
    GrowthText(StageSub,LDTerms::FinalOnce);StageSub->SetColorAndOpacity(LDHUDStyle::UIEvent);
    // The description of the selected card (else the first) as written, then its numbers line.
    const FName Shown=!PendingFinalOption.IsNone()?PendingFinalOption:Latest.FinalModOptions.IsValidIndex(0)?Latest.FinalModOptions[0]:NAME_None;
    FString Description;
    LDHUD::ContentText(LatestContent,Shown.ToString(),true,Description);
    FString Numbers=LDHUD::FinalModCard(Shown,LatestContent,LatestStats).Detail.Body;
    Numbers.RemoveFromStart(Description);Numbers.TrimStartAndEndInline();
    GrowthText(FinalDescText,Description);
    GrowthText(FinalNumbersText,Numbers);
    GrowthShown(FinalNumbersText,!Numbers.IsEmpty());
    GrowthText(FinalLockNote,TEXT("选定后锁定本局构筑；保留技能、构件和工坊容量。"));
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
        PlaceOnLayer(StageBox,Places.Stage,FVector2D(.5f,0.f));
        PlaceOnLayer(StageBack,Places.Stage,FVector2D(1.f,0.f));
        const FSlateRect& Dock=Places.Dock;
        PlaceOnLayer(FinalDesc,FSlateRect(Dock.Left+28.f,Dock.Top,Dock.Right-340.f,Dock.Bottom),FVector2D(0.f,.5f));
        PlaceOnLayer(FinalLockButton,FSlateRect(Dock.Right-320.f,Dock.Top,Dock.Right-28.f,Dock.Bottom),FVector2D(.5f,.4f));
        PlaceOnLayer(FinalLockNote,FSlateRect(Dock.Right-320.f,Dock.Top,Dock.Right-28.f,Dock.Bottom),FVector2D(.5f,.78f));
    }
    if(!IsServiceShown()){return;}
    PlaceOnLayer(RespecBox,Places.CharacterFoot,FVector2D(0.f,0.f));
    PlaceOnLayer(NpcBox,Places.PendingTop,FVector2D(0.f,0.f));
    PlaceOnLayer(TrainerPicks,Places.PendingTop,FVector2D(0.f,0.f));
    PlaceOnLayer(ArtisanBox,Places.LibraryTop,FVector2D(1.f,0.f));
    PlaceOnLayer(ServiceDock,Places.Dock,FVector2D::ZeroVector,true);
    for(uint8 Key=0;Key<3;++Key)
    {
        const FSlateRect& Head=Places.Heads[int32(LDHUD::SlotColumn(Key))];
        if(ForgetButtons.IsValidIndex(Key)){PlaceOnLayer(ForgetButtons[Key],Head,FVector2D(1.f,0.f));}
        if(BlueprintRows.IsValidIndex(TrainerBlueprint))
        {
            PlaceOnLayer(BlueprintRows[TrainerBlueprint].Learn[Key],Head,FVector2D(.5f,1.f));
            PlaceOnLayer(BlueprintRows[TrainerBlueprint].Replace[Key],Head,FVector2D(.5f,1.f));
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
        LDTween::SetScale(Card,FMath::Lerp(.86f,1.f,Progress));
        LDTween::SetOpacity(Card,FMath::Clamp(Progress,0.f,1.f));
    }
}
int32 ULDChoicesPanel::NativePaint(const FPaintArgs& A,const FGeometry& G,const FSlateRect& C,FSlateWindowElementList& E,int32 L,const FWidgetStyle& S,bool Enabled) const
{
    const int32 Base=Super::NativePaint(A,G,C,E,L,S,Enabled);
    if(!bStageShown){return Base;}
    // The focused offer card wears the action yellow; the selected final card and source the event pink.
    const auto Edge=[&](const FTrayCard& Card,const FLinearColor& Color)
    {
        if(!Card.Frame||!Card.Frame->IsVisible()){return;}
        const FBox2D Box=LDUIShapes::RectIn(G,Card.Frame->GetCachedGeometry());
        LDUIShapes::Glow(E,Base+1,G,LDUIShapes::EShape::Rounded,Box,Color,10.f,LDHUDStyle::RadiusPanel);
        LDUIShapes::Frame(E,Base+1,G,LDUIShapes::EShape::Rounded,Box,Color,2.f,LDHUDStyle::RadiusPanel);
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
    }
    return Base+2;
}
