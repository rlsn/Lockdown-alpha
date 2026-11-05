#include "UI/LDShopPanel.h"
#include "UI/LDHUDModel.h"
#include "UI/LDHUDStyle.h"
#include "UI/LDTerms.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace
{
const FLinearColor ShopPanelHintColor(1.f,.66f,.36f);
// Grid geometry (1080p reference units): six icons a row, the hover description beside the panel at a fixed width.
constexpr int32 ShopGridColumns=6;
constexpr float ShopIconSize=56.f,ShopIconGap=8.f,ShopDetailWidth=300.f;
void ShopPanelTint(ULDCommandButton* Tinted,const FLinearColor& Tint)
{
    FButtonStyle TintedStyle=Tinted->GetStyle();
    if(TintedStyle.Normal.TintColor==FSlateColor(Tint)){return;}
    TintedStyle.Normal.TintColor=FSlateColor(Tint);TintedStyle.Hovered.TintColor=FSlateColor(Tint*1.6f);Tinted->SetStyle(TintedStyle);
}
// Placeholder item icons until real art lands: a rounded square with the name's first character, orange for an item with
// an active effect and blue for a passive-only one; dimmed while the gold does not cover it, gold-rimmed when selected.
void ShopPanelIcon(UBorder* Shape,UTextBlock* Glyph,bool bActive,bool bAffordable,bool bSelected)
{
    const FLinearColor Base=bActive?FLinearColor(.62f,.22f,.04f):FLinearColor(.08f,.28f,.52f);
    const FLinearColor Fill=bAffordable?Base:Base*.4f;
    const FLinearColor Rim=bSelected?FLinearColor(1.f,.78f,.25f):FLinearColor(1,1,1,bAffordable?.45f:.2f);
    Shape->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White,8.f,Rim,bSelected?3.f:1.5f));Shape->SetBrushColor(Fill);
    Glyph->SetColorAndOpacity(FSlateColor(bAffordable?FLinearColor::White:FLinearColor(.6f,.6f,.62f)));
}
void ShopPanelText(UTextBlock* Block,const FString& Wanted)
{
    if(Block&&!Block->GetText().ToString().Equals(Wanted,ESearchCase::CaseSensitive)){Block->SetText(FText::FromString(Wanted));}
}
// A refusal must name its reason: a missing one is a protocol error and is shown as such, never as a blank.
FString ShopPanelReason(ELDGrowthReason RefusalReason)
{
    const FString Shown=LDHUD::GrowthReasonText(RefusalReason);
    if(!Shown.IsEmpty()){return Shown;}
    UE_LOG(LogTemp,Error,TEXT("LD_SHOP_PANEL_REASON_MISSING A shop refusal arrived without a reason code."));
    return TEXT("原因未知");
}
const FLDEquipmentSlotView* ShopPanelEquipSlot(const FLDEquipmentView& EquipView,uint8 SlotIdx)
{
    return EquipView.Slots.FindByPredicate([SlotIdx](const FLDEquipmentSlotView& EquipRow){return EquipRow.SlotIndex==SlotIdx;});
}
}

TSharedRef<SWidget> ULDShopPanel::RebuildWidget()
{
    if(!MainPanel)
    {
        auto MakeText=[this](const FString& Caption,int32 FontSize){return LDWidgets::Text(WidgetTree,Caption,FontSize);};
        auto* Layout=WidgetTree->ConstructWidget<UVerticalBox>();
        auto* HeadRow=WidgetTree->ConstructWidget<UHorizontalBox>();Layout->AddChildToVerticalBox(HeadRow);
        HeadRow->AddChildToHorizontalBox(MakeText(LDTerms::Shop,20))->SetVerticalAlignment(VAlign_Center);
        GoldLine=MakeText(FString(LDTerms::Gold)+TEXT(" 同步中"),16);
        UHorizontalBoxSlot* GoldCell=HeadRow->AddChildToHorizontalBox(GoldLine);
        GoldCell->SetSize(FSlateChildSize(ESlateSizeRule::Fill));GoldCell->SetPadding(FMargin(16,0));GoldCell->SetVerticalAlignment(VAlign_Center);
        for(uint8 SlotIdx=0;SlotIdx<2;++SlotIdx)
        {
            auto* EquipRow=WidgetTree->ConstructWidget<UHorizontalBox>();Layout->AddChildToVerticalBox(EquipRow)->SetPadding(FMargin(0.f,SlotIdx?6.f:14.f,0.f,0.f));
            EquipNames[SlotIdx]=MakeText(TEXT(""),16);
            UHorizontalBoxSlot* NameCell=EquipRow->AddChildToHorizontalBox(EquipNames[SlotIdx]);
            NameCell->SetSize(FSlateChildSize(ESlateSizeRule::Fill));NameCell->SetVerticalAlignment(VAlign_Center);
            SellButtons[SlotIdx]=LDWidgets::Button(WidgetTree,TEXT("卖出"),[this,SlotIdx](){RequestSell(SlotIdx);},false,SlotIdx?FName(TEXT("ShopSell1")):FName(TEXT("ShopSell0")));
            EquipRow->AddChildToHorizontalBox(SellButtons[SlotIdx]);
            UpgradeButtons[SlotIdx]=LDWidgets::Button(WidgetTree,TEXT("进阶"),[this,SlotIdx](){RequestUpgrade(SlotIdx);},false,SlotIdx?FName(TEXT("ShopUpgrade1")):FName(TEXT("ShopUpgrade0")));
            EquipRow->AddChildToHorizontalBox(UpgradeButtons[SlotIdx])->SetPadding(FMargin(8,0,0,0));
        }
        // The goods as an icon grid (placeholder icons until real art lands); the full description pops up on hover.
        ItemGrid=WidgetTree->ConstructWidget<UUniformGridPanel>();ItemGrid->SetSlotPadding(FMargin(ShopIconGap*.5f));
        auto* GridScroll=WidgetTree->ConstructWidget<UScrollBox>();GridScroll->SetConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible);GridScroll->AddChild(ItemGrid);
        auto* GridBox=WidgetTree->ConstructWidget<USizeBox>();GridBox->SetMaxDesiredHeight(4*(ShopIconSize+ShopIconGap));GridBox->SetContent(GridScroll);
        Layout->AddChildToVerticalBox(GridBox)->SetPadding(FMargin(-ShopIconGap*.5f,10,-ShopIconGap*.5f,0));
        auto* FootRow=WidgetTree->ConstructWidget<UHorizontalBox>();Layout->AddChildToVerticalBox(FootRow)->SetPadding(FMargin(0,14,0,0));
        Feedback=MakeText(TEXT(""),15);
        UHorizontalBoxSlot* FeedbackCell=FootRow->AddChildToHorizontalBox(Feedback);
        FeedbackCell->SetSize(FSlateChildSize(ESlateSizeRule::Fill));FeedbackCell->SetPadding(FMargin(0,0,12,0));FeedbackCell->SetVerticalAlignment(VAlign_Center);
        // A right click on an icon buys it at the shop or pre-orders it anywhere else (QuickBuy); there is no pre-order button.
        BuyButton=LDWidgets::Button(WidgetTree,TEXT("购买"),[this](){Buy();},false,TEXT("ShopBuy"));
        FootRow->AddChildToHorizontalBox(BuyButton)->SetVerticalAlignment(VAlign_Center);
        // The pre-order list: a line that says how it buys, then one chip per queued item (clicking it removes it).
        PreorderLine=MakeText(TEXT(""),14);PreorderLine->SetColorAndOpacity(ShopPanelHintColor);
        Layout->AddChildToVerticalBox(PreorderLine)->SetPadding(FMargin(0,10,0,0));
        auto* PreorderRow=WidgetTree->ConstructWidget<UHorizontalBox>();Layout->AddChildToVerticalBox(PreorderRow)->SetPadding(FMargin(0,6,0,0));
        for(int32 ChipIdx=0;ChipIdx<2;++ChipIdx)
        {
            PreorderChips[ChipIdx]=LDWidgets::Button(WidgetTree,TEXT(""),[this,ChipIdx](){RemovePreorder(ChipIdx);},false,ChipIdx?FName(TEXT("ShopPreorder1")):FName(TEXT("ShopPreorder0")));
            PreorderChips[ChipIdx]->SetVisibility(ESlateVisibility::Collapsed);
            PreorderRow->AddChildToHorizontalBox(PreorderChips[ChipIdx])->SetPadding(FMargin(0,0,8,0));
        }
        auto* SellBox=WidgetTree->ConstructWidget<UVerticalBox>();
        SellPrompt=MakeText(TEXT(""),16);SellBox->AddChildToVerticalBox(SellPrompt);
        auto* SellActions=WidgetTree->ConstructWidget<UHorizontalBox>();SellBox->AddChildToVerticalBox(SellActions)->SetPadding(FMargin(0,10,0,0));
        SellConfirmButton=LDWidgets::Button(WidgetTree,TEXT("确认卖出"),[this](){ConfirmSell();},false,TEXT("ShopSellConfirm"));
        SellActions->AddChildToHorizontalBox(SellConfirmButton)->SetPadding(FMargin(0,0,10,0));
        SellActions->AddChildToHorizontalBox(LDWidgets::Button(WidgetTree,TEXT("返回"),[this](){CloseChildConfirmation();},false,TEXT("ShopSellBack")));
        SellPanel=LDWidgets::Panel(WidgetTree,SellBox,FMargin(16));SellPanel->SetBrushColor(FLinearColor(.06f,.05f,.03f,.99f));SellPanel->SetVisibility(ESlateVisibility::Collapsed);
        Layout->AddChildToVerticalBox(SellPanel)->SetPadding(FMargin(0,12,0,0));
        MainPanel=LDWidgets::Panel(WidgetTree,Layout,FMargin(20));
        // Docked in the bottom-right corner right above the HUD's shop entry (SetBottomDock), which also opens and closes it;
        // clear of the screen centre where the own body is, so the player keeps moving and fighting beside it. The frame
        // around it takes no input. Back still closes it like every main panel.
        MainPanel->SetBrushColor(FLinearColor(.025f,.045f,.07f,.86f));
        auto* WidthBox=WidgetTree->ConstructWidget<USizeBox>();WidthBox->SetWidthOverride(DockWidth);WidthBox->SetContent(MainPanel);
        auto* FrameBorder=WidgetTree->ConstructWidget<UBorder>();FrameBorder->SetBrushColor(FLinearColor::Transparent);
        FrameBorder->SetHorizontalAlignment(HAlign_Right);FrameBorder->SetVerticalAlignment(VAlign_Bottom);
        FrameBorder->SetPadding(FMargin(0,0,16,BottomDock));DockFrame=FrameBorder;FrameBorder->SetVisibility(ESlateVisibility::SelfHitTestInvisible);FrameBorder->SetContent(WidthBox);
        // The hovered or focused item's full description pops up left of the panel, level with its icon, the same way the
        // blind-box cards explain themselves: on its own canvas layer (so it never moves the grid and drops the hover),
        // wrapped at a fixed width, taking no pointer input.
        auto* DetailBox=WidgetTree->ConstructWidget<UVerticalBox>();
        auto FixedWrap=[](UTextBlock* Line){Line->SetAutoWrapText(false);Line->SetWrapTextAt(ShopDetailWidth-28.f);return Line;};
        DetailName=FixedWrap(MakeText(TEXT(""),17));DetailBox->AddChildToVerticalBox(DetailName);
        DetailPrice=FixedWrap(MakeText(TEXT(""),13));DetailPrice->SetColorAndOpacity(LDHUDStyle::TextAccent);DetailBox->AddChildToVerticalBox(DetailPrice)->SetPadding(FMargin(0,2,0,0));
        DetailLines=FixedWrap(MakeText(TEXT(""),14));DetailBox->AddChildToVerticalBox(DetailLines)->SetPadding(FMargin(0,8,0,0));
        DetailHint=FixedWrap(MakeText(TEXT(""),14));DetailHint->SetColorAndOpacity(ShopPanelHintColor);DetailBox->AddChildToVerticalBox(DetailHint)->SetPadding(FMargin(0,8,0,0));
        Detail=LDWidgets::Panel(WidgetTree,DetailBox,FMargin(14,12));Detail->SetBrushColor(FLinearColor(.02f,.035f,.055f,.94f));
        auto* DetailWidth=WidgetTree->ConstructWidget<USizeBox>();DetailWidth->SetWidthOverride(ShopDetailWidth);DetailWidth->SetContent(Detail);
        DetailWidth->SetVisibility(ESlateVisibility::Collapsed);
        DetailLayer=WidgetTree->ConstructWidget<UCanvasPanel>();DetailLayer->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        DetailSlot=DetailLayer->AddChildToCanvas(DetailWidth);DetailSlot->SetAutoSize(true);DetailSlot->SetAlignment(FVector2D(1,0));
        auto* Layers=WidgetTree->ConstructWidget<UOverlay>();Layers->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        auto* FrameLayer=Layers->AddChildToOverlay(FrameBorder);FrameLayer->SetHorizontalAlignment(HAlign_Fill);FrameLayer->SetVerticalAlignment(VAlign_Fill);
        auto* PopupLayer=Layers->AddChildToOverlay(DetailLayer);PopupLayer->SetHorizontalAlignment(HAlign_Fill);PopupLayer->SetVerticalAlignment(VAlign_Fill);
        WidgetTree->RootWidget=Layers;
        SetVisibility(bOpen?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
        RefreshShop();
    }
    return Super::RebuildWidget();
}
void ULDShopPanel::SetBottomDock(float Bottom)
{
    BottomDock=Bottom;
    if(DockFrame){DockFrame->SetPadding(FMargin(0,0,16,Bottom));}
}
UWidget* ULDShopPanel::FocusScope() const{return bSelling?static_cast<UWidget*>(SellPanel.Get()):static_cast<UWidget*>(MainPanel.Get());}
UWidget* ULDShopPanel::HitArea() const{return MainPanel;}
void ULDShopPanel::OpenPanel()
{
    if(bOpen){return;}
    bOpen=true;SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    if(!PendingRequest)
    {
        ShopPanelText(Feedback,HasSession()
            ?FString(TEXT("悬停图标看说明，右键图标直接购买；卖出需要确认。对局不会暂停，"))+LDTerms::Shop+TEXT("也不是安全区")
            :FString(TEXT("不在"))+LDTerms::Shop+TEXT("：悬停图标看说明，右键图标加入预购，到了自动买"));
    }
    RefreshShop();
}
bool ULDShopPanel::ClosePanel()
{
    // Closing only hides the panel: the session and any submitted request keep their real state, and a later result
    // still reaches the player as a notice without reopening the panel.
    ResetSell();bOpen=false;SetVisibility(ESlateVisibility::Collapsed);UpdateDetail();return true;
}
bool ULDShopPanel::CloseChildConfirmation()
{
    if(!bSelling){return false;}
    ResetSell();ShopPanelText(Feedback,TEXT("已返回，没有卖出"));RefreshShop();return true;
}
void ULDShopPanel::ResetSell()
{
    bSelling=false;bUpgrading=false;UpgradeQuotePrice=0;SellInstance.Invalidate();SellItemRevision=0;SellQuotePrice=0;SellName.Reset();
    if(SellPanel){SellPanel->SetVisibility(ESlateVisibility::Collapsed);}
}
void ULDShopPanel::Present(const FLDShopView* View,const FLDEquipmentView* EquipView,bool bBuildKnown,uint64 BuildRevision,uint32 Match,uint32 Life,ELDLifePhase Phase,bool bClock,int64 NowMs,const FLDDemoContentDefinitions* ContentDefs,bool bNearShop)
{
    // A new match closes the shop and starts without pre-orders. A session ending, a new life or death only switch the
    // list back to the catalog and block trading; taking damage never closes the panel.
    if(bOpen&&bPresented&&Match!=LatestMatch){ClosePanel();NotifyPlayer(FString(LDTerms::Shop)+TEXT("已关闭：对局已变化"));}
    if(bPresented&&Match!=LatestMatch){PreorderQueue.Reset();bAutoBlocked=false;}
    if(bPresented&&Match!=LatestMatch&&PendingRequest)
    {
        // A request of an older match can no longer be answered here; the new match starts without it.
        ClearPending();ShopPanelText(Feedback,TEXT("对局已变化，上一笔交易请以当前状态为准"));
    }
    bLatestView=View!=nullptr;Latest=View?*View:FLDShopView();LatestEquipment=EquipView?*EquipView:FLDEquipmentView();
    bLatestBuildKnown=bBuildKnown;LatestBuildRevision=BuildRevision;LatestMatch=Match;LatestLife=Life;LatestPhase=Phase;
    bLatestClock=bClock;LatestNowMs=NowMs;LatestContent=ContentDefs;bPresented=true;bLatestNear=bNearShop;
    Listing=HasSession()?Latest.Items:LDHUD::CatalogItems(ContentDefs);
    if(Selected!=INDEX_NONE&&(!Listing.IsValidIndex(Selected)||Listing[Selected].DefinitionId!=SelectedItem))
    {
        // A new session, quote or catalog keeps the preview only while the same definition is still listed.
        Selected=Listing.IndexOfByPredicate([this](const FLDShopItemView& ItemView){return ItemView.DefinitionId==SelectedItem;});
        if(Selected==INDEX_NONE){SelectedItem=NAME_None;if(bOpen&&!PendingRequest){ShopPanelText(Feedback,TEXT("商品列表已更新，请重新选择"));}}
    }
    if(bSelling)
    {
        // The confirmation answers one exact item at one price: a changed item or quote withdraws it, never redirects it.
        const FLDShopSellView* SellEntry=Latest.Sellable.FindByPredicate([this](const FLDShopSellView& SellRow){return SellRow.Instance==SellInstance&&SellRow.ItemRevision==SellItemRevision;});
        if(!SellEntry){ResetSell();ShopPanelText(Feedback,TEXT("装备已变化，确认已撤销"));}
        else if(bUpgrading&&(!SellEntry->bCanUpgrade||Latest.UpgradePrice!=UpgradeQuotePrice)){ResetSell();ShopPanelText(Feedback,TEXT("进阶条件已变化，确认已撤销"));}
        else if(!bUpgrading&&SellEntry->SellPrice!=SellQuotePrice){ResetSell();ShopPanelText(Feedback,TEXT("卖价已变化，卖出确认已撤销，请重新确认"));}
    }
    TryAutoBuy();
    if(bOpen){RefreshShop();}
}
bool ULDShopPanel::HasSession() const{return bLatestView&&Latest.Life==LatestLife;}
int32 ULDShopPanel::FreeSlots() const
{
    if(!EquipmentKnown()){return 0;}
    int32 Free=0;
    for(uint8 SlotIdx=0;SlotIdx<2;++SlotIdx){if(!ShopPanelEquipSlot(LatestEquipment,SlotIdx)->bOccupied){++Free;}}
    return Free;
}
bool ULDShopPanel::WantsSession() const
{
    // While the page is open (to trade by hand), or for a pre-order that could be bought now by the catalog price; the
    // quote then decides.
    if(HasSession()||LatestPhase!=ELDLifePhase::Alive){return false;}
    return bOpen||(!PreorderQueue.IsEmpty()&&EquipmentKnown()&&LDHUD::NextPreorder(PreorderQueue,Listing,LatestEquipment.Gold,FreeSlots())!=INDEX_NONE);
}
bool ULDShopPanel::QuickBuy(int32 Index)
{
    if(bSelling){ShopPanelText(Feedback,TEXT("请先确认或返回当前的确认"));return false;}
    if(!Listing.IsValidIndex(Index)){return false;}
    Selected=Index;SelectedItem=Listing[Index].DefinitionId;
    // In the shop's range with this life's quote it buys right away; anywhere else it joins the pre-orders.
    if(!bLatestNear||!HasSession()){return AddPreorder(SelectedItem);}
    const FString Block=CommandBlock();
    if(!Block.IsEmpty()){ShopPanelText(Feedback,Block);RefreshShop();return false;}
    return BuyItem(SelectedItem,false);
}
FReply ULDShopPanel::NativeOnPreviewMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event)
{
    // Seen before the icon button, which answers only the left button; the world never gets this right click.
    if(bOpen&&Event.GetEffectingButton()==EKeys::RightMouseButton)
    {
        for(int32 Index=0;Index<ShownItems.Num()&&ItemButtons.IsValidIndex(Index);++Index)
        {
            if(ItemButtons[Index]->IsVisible()&&ItemButtons[Index]->GetCachedGeometry().IsUnderLocation(Event.GetScreenSpacePosition()))
            {QuickBuy(Index);return FReply::Handled();}
        }
    }
    return Super::NativeOnPreviewMouseButtonDown(Geometry,Event);
}
bool ULDShopPanel::AddPreorder(FName DefinitionId)
{
    if(PreorderQueue.Num()>=LDHUD::MaxShopPreorders)
    {ShopPanelText(Feedback,FString::Printf(TEXT("预购最多 %d 件，先点掉一件再加"),LDHUD::MaxShopPreorders));RefreshShop();return false;}
    if(!Listing.ContainsByPredicate([DefinitionId](const FLDShopItemView& Row){return Row.DefinitionId==DefinitionId;}))
    {ShopPanelText(Feedback,TEXT("这件商品不在列表里，不能预购"));RefreshShop();return false;}
    PreorderQueue.Add(DefinitionId);bAutoBlocked=false;
    ShopPanelText(Feedback,TEXT("已加入预购：")+LDHUD::ContentName(LatestContent,DefinitionId.ToString())+TEXT("，到")+LDTerms::Shop+TEXT("附近且")+LDTerms::Gold+TEXT("够时自动买入"));
    RefreshShop();return true;
}
bool ULDShopPanel::RemovePreorder(int32 Index)
{
    if(!PreorderQueue.IsValidIndex(Index)){return false;}
    const FName Removed=PreorderQueue[Index];PreorderQueue.RemoveAt(Index);
    ShopPanelText(Feedback,TEXT("已取消预购：")+LDHUD::ContentName(LatestContent,Removed.ToString()));
    RefreshShop();return true;
}
void ULDShopPanel::TryAutoBuy()
{
    // Only at the shop, with this life's quote, nothing in flight and no confirmation open; one buy at a time.
    if(PreorderQueue.IsEmpty()||!bLatestNear||bSelling||!CommandBlock().IsEmpty()){return;}
    const UWorld* World=GetWorld();const double Now=World?World->GetRealTimeSeconds():0.;
    if(bAutoBlocked)
    {
        const bool bMoved=LatestEquipment.AccountRevision!=AutoBlockAccount||LatestEquipment.InventoryRevision!=AutoBlockInventory||Latest.QuoteRevision!=AutoBlockQuote;
        if(!bMoved&&Now<AutoRetryAt){return;}
        bAutoBlocked=false;
    }
    const int32 Next=LDHUD::NextPreorder(PreorderQueue,Latest.Items,LatestEquipment.Gold,FreeSlots());
    if(Next!=INDEX_NONE){BuyItem(PreorderQueue[Next],true);}
}
void ULDShopPanel::BlockAutoBuy()
{
    // A refused or unsent automatic buy waits for gold, inventory or quote to move, or for the retry delay.
    const UWorld* World=GetWorld();bAutoBlocked=true;AutoRetryAt=(World?World->GetRealTimeSeconds():0.)+2.;
    AutoBlockAccount=LatestEquipment.AccountRevision;AutoBlockInventory=LatestEquipment.InventoryRevision;AutoBlockQuote=Latest.QuoteRevision;
}
bool ULDShopPanel::EquipmentKnown() const
{
    return LatestEquipment.bValid&&LatestEquipment.Generation==LatestMatch
        &&ShopPanelEquipSlot(LatestEquipment,0)!=nullptr&&ShopPanelEquipSlot(LatestEquipment,1)!=nullptr;
}
const FLDShopSellView* ULDShopPanel::SellableFor(uint8 SlotIdx) const
{
    // Offered only when the quote lists the exact instance that the own equipment view shows in that slot.
    const FLDEquipmentSlotView* EquipSlot=HasSession()&&EquipmentKnown()?ShopPanelEquipSlot(LatestEquipment,SlotIdx):nullptr;
    if(!EquipSlot||!EquipSlot->bOccupied){return nullptr;}
    return Latest.Sellable.FindByPredicate([EquipSlot](const FLDShopSellView& SellRow){return SellRow.SlotIndex==EquipSlot->SlotIndex&&SellRow.Instance==EquipSlot->Instance;});
}
FString ULDShopPanel::CommandBlock() const
{
    // Why no command can be built from what the panel shows; empty when one can. Range, gold, capacity and prices
    // are not judged here: the growth entry and the server answer those with their own reasons.
    if(PendingRequest){return TEXT("上一笔交易仍在处理中");}
    if(!HasSession()){return FString(TEXT("到"))+LDTerms::Shop+TEXT("才能交易，可以先右键图标加入预购");}
    if(!EquipmentKnown()){return TEXT("装备数据同步中，暂时不能交易");}
    if(!bLatestBuildKnown){return TEXT("构筑数据同步中，暂时不能交易");}
    if(LatestPhase!=ELDLifePhase::Alive){return LatestPhase==ELDLifePhase::Dead?FString(LDTerms::Dead)+TEXT("，不能交易"):FString(TEXT("当前状态不能交易"));}
    return FString();
}
void ULDShopPanel::RefreshShop()
{
    if(!MainPanel){return;}
    const bool bEquipKnown=EquipmentKnown();
    const FString Block=CommandBlock();
    ShopPanelText(GoldLine,bEquipKnown?LDHUD::Gold(LatestEquipment.Gold):FString(LDTerms::Gold)+TEXT(" 同步中"));
    for(uint8 SlotIdx=0;SlotIdx<2;++SlotIdx)
    {
        const FLDEquipmentSlotView* EquipSlot=bEquipKnown?ShopPanelEquipSlot(LatestEquipment,SlotIdx):nullptr;
        const FString Holding=!EquipSlot?FString(TEXT("同步中")):EquipSlot->bOccupied
            ?LDHUD::ContentName(LatestContent,EquipSlot->DefinitionId.ToString())+(EquipSlot->bUpgraded?TEXT("（已进阶）"):TEXT("")):FString(TEXT("空"));
        ShopPanelText(EquipNames[SlotIdx],FString::Printf(TEXT("装备 %d：%s"),SlotIdx+1,*Holding));
        const FLDShopSellView* SellEntry=SellableFor(SlotIdx);
        const bool bCanSell=SellEntry&&Block.IsEmpty()&&!bSelling;
        SellButtons[SlotIdx]->SetIsEnabled(bCanSell);
        ShopPanelText(CastChecked<UTextBlock>(SellButtons[SlotIdx]->GetContent()),bCanSell?FString::Printf(TEXT("卖出 +%lld"),SellEntry->SellPrice):FString(TEXT("卖出")));
        const bool bCanUpgrade=bCanSell&&SellEntry->bCanUpgrade;
        UpgradeButtons[SlotIdx]->SetIsEnabled(bCanUpgrade);
        ShopPanelText(CastChecked<UTextBlock>(UpgradeButtons[SlotIdx]->GetContent()),
            bLatestView&&Latest.bUpgradeUsed?FString(TEXT("进阶已用")):bCanUpgrade?FString::Printf(TEXT("进阶 -%lld"),Latest.UpgradePrice):FString(TEXT("进阶")));
    }
    SyncItemButtons();
    for(int32 ItemIdx=0;ItemIdx<ShownItems.Num();++ItemIdx)
    {
        const FLDShopItemView& ItemView=Listing[ItemIdx];
        const FString Name=LDHUD::ContentName(LatestContent,ItemView.DefinitionId.ToString());
        ShopPanelText(ItemGlyphs[ItemIdx],Name.Left(1));
        // Unknown gold shows every icon as affordable; the hover hint and the server decide.
        const bool bAffordable=!bEquipKnown||ItemView.BuyPrice<=LatestEquipment.Gold;
        const uint8 Look=uint8(ItemView.bHasActive)|uint8(bAffordable)<<1|uint8(ItemIdx==Selected)<<2|0x80;
        if(ItemLooks[ItemIdx]!=Look){ItemLooks[ItemIdx]=Look;ShopPanelIcon(ItemShapes[ItemIdx],ItemGlyphs[ItemIdx],ItemView.bHasActive,bAffordable,ItemIdx==Selected);}
        ItemButtons[ItemIdx]->SetIsEnabled(!bSelling);
    }
    bDetailStale=true;
    const bool bPicked=Listing.IsValidIndex(Selected);
    ShopPanelText(CastChecked<UTextBlock>(BuyButton->GetContent()),bPicked?FString::Printf(TEXT("购买 %lld"),Listing[Selected].BuyPrice):FString(TEXT("购买")));
    BuyButton->SetIsEnabled(Listing.IsValidIndex(Selected)&&Block.IsEmpty()&&!bSelling);
    RefreshPreorders();
    SellConfirmButton->SetIsEnabled(bSelling&&Block.IsEmpty());
    ShopPanelText(CastChecked<UTextBlock>(SellConfirmButton->GetContent()),bUpgrading?TEXT("确认进阶"):TEXT("确认卖出"));
}
void ULDShopPanel::SyncItemButtons()
{
    TArray<FName> ListedIds;ListedIds.Reserve(Listing.Num());
    for(const FLDShopItemView& ItemView:Listing){ListedIds.Add(ItemView.DefinitionId);}
    if(ListedIds==ShownItems){return;}
    // Only a changed definition list touches the buttons. They are reused by position and the pool only grows,
    // so a stable name never refers to a second widget and hover, focus and scroll survive owner updates.
    for(int32 ItemIdx=ItemButtons.Num();ItemIdx<ListedIds.Num();++ItemIdx)
    {
        ULDCommandButton* ItemButton=LDWidgets::Button(WidgetTree,TEXT(""),[this,ItemIdx](){SelectItem(ItemIdx);},false,FName(*FString::Printf(TEXT("ShopItem%d"),ItemIdx)));
        // The button's own backing stays clear: the icon shape is the whole face.
        ShopPanelTint(ItemButton,FLinearColor::Transparent);
        if(UButtonSlot* FaceSlot=Cast<UButtonSlot>(ItemButton->GetContentSlot()))
        {FaceSlot->SetPadding(FMargin(0));FaceSlot->SetHorizontalAlignment(HAlign_Fill);FaceSlot->SetVerticalAlignment(VAlign_Fill);}
        auto* Face=WidgetTree->ConstructWidget<UOverlay>();
        auto* Shape=WidgetTree->ConstructWidget<UBorder>();Shape->SetVisibility(ESlateVisibility::HitTestInvisible);
        auto* ShapeSlot=Face->AddChildToOverlay(Shape);ShapeSlot->SetHorizontalAlignment(HAlign_Fill);ShapeSlot->SetVerticalAlignment(VAlign_Fill);
        UTextBlock* Glyph=LDWidgets::Text(WidgetTree,TEXT(""),22);Glyph->SetAutoWrapText(false);Glyph->SetJustification(ETextJustify::Center);
        Glyph->SetVisibility(ESlateVisibility::HitTestInvisible);ScaleNewText(Glyph);
        auto* GlyphSlot=Face->AddChildToOverlay(Glyph);GlyphSlot->SetHorizontalAlignment(HAlign_Center);GlyphSlot->SetVerticalAlignment(VAlign_Center);
        ItemButton->SetContent(Face);
        auto* Cell=WidgetTree->ConstructWidget<USizeBox>();Cell->SetWidthOverride(ShopIconSize);Cell->SetHeightOverride(ShopIconSize);Cell->SetContent(ItemButton);
        UUniformGridSlot* GridCell=ItemGrid->AddChildToUniformGrid(Cell,ItemIdx/ShopGridColumns,ItemIdx%ShopGridColumns);
        GridCell->SetHorizontalAlignment(HAlign_Left);GridCell->SetVerticalAlignment(VAlign_Top);
        ItemButtons.Add(ItemButton);ItemCells.Add(Cell);ItemShapes.Add(Shape);ItemGlyphs.Add(Glyph);ItemLooks.Add(0);
    }
    for(int32 ItemIdx=0;ItemIdx<ItemButtons.Num();++ItemIdx){ItemCells[ItemIdx]->SetVisibility(ItemIdx<ListedIds.Num()?ESlateVisibility::Visible:ESlateVisibility::Collapsed);}
    ShownItems=MoveTemp(ListedIds);
}
void ULDShopPanel::NativeTick(const FGeometry& Geometry,float Delta)
{
    Super::NativeTick(Geometry,Delta);
    UpdateDetail();
}
void ULDShopPanel::UpdateDetail()
{
    if(!Detail||!DetailSlot){return;}
    // The pointer wins over keyboard focus; nothing hovered or focused shows no description.
    auto Shown=[this](int32 Index){return Index<ShownItems.Num()&&Listing.IsValidIndex(Index)&&ItemButtons.IsValidIndex(Index);};
    int32 Next=INDEX_NONE;
    if(bOpen&&!bSelling)
    {
        for(int32 Index=0;Index<ItemButtons.Num()&&Next==INDEX_NONE;++Index){if(Shown(Index)&&ItemButtons[Index]->IsHovered()){Next=Index;}}
        for(int32 Index=0;Index<ItemButtons.Num()&&Next==INDEX_NONE;++Index){if(Shown(Index)&&ItemButtons[Index]->HasKeyboardFocus()){Next=Index;}}
    }
    UWidget* Popup=DetailSlot->Content;
    if(Next!=DetailItem||bDetailStale)
    {
        DetailItem=Next;bDetailStale=false;
        if(Popup){Popup->SetVisibility(Next==INDEX_NONE?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);}
        if(Next!=INDEX_NONE){FillDetail(Next);}
    }
    if(DetailItem!=INDEX_NONE&&DetailLayer&&Popup&&MainPanel)
    {
        // Right edge 8 units left of the panel, top level with the icon, read from last frame's layout; neither depends on
        // the popup, so this never feeds back. The popup is only pulled up to stay on screen.
        const FGeometry& Layer=DetailLayer->GetCachedGeometry();
        const FVector2D PanelTop=Layer.AbsoluteToLocal(MainPanel->GetCachedGeometry().GetAbsolutePosition());
        const FVector2D IconTop=Layer.AbsoluteToLocal(ItemButtons[DetailItem]->GetCachedGeometry().GetAbsolutePosition());
        const float Bottom=float(Layer.GetLocalSize().Y)-float(Popup->GetDesiredSize().Y)-8.f;
        const FVector2D Place(FMath::RoundToFloat(float(PanelTop.X)-8.f),FMath::RoundToFloat(FMath::Max(0.f,FMath::Min(float(IconTop.Y),Bottom))));
        if(!DetailSlot->GetPosition().Equals(Place,.5f)){DetailSlot->SetPosition(Place);}
    }
}
void ULDShopPanel::FillDetail(int32 Index)
{
    const bool bEquipKnown=EquipmentKnown();
    const FLDShopItemView& Item=Listing[Index];
    const LDHUD::FShopItemView ShownItem=LDHUD::ShopItem(Item,bEquipKnown?LatestEquipment.Gold:0,bLatestClock,LatestNowMs,LatestContent);
    ShopPanelText(DetailName,ShownItem.Name);ShopPanelText(DetailPrice,ShownItem.Price);
    FString Body=FString::Join(ShownItem.Lines,TEXT("\n"));
    const FString ShownId=Item.DefinitionId.ToString();
    if(const FLDDemoEquipmentDefinition* Definition=LatestContent?LatestContent->Equipment.FindByPredicate([&ShownId](const FLDDemoEquipmentDefinition& D){return D.Id==ShownId;}):nullptr)
    {
        if(LDDemoEquipment::Upgradable(*Definition))
        {Body+=FString::Printf(TEXT("\n进阶后（每局一次，%lld %s）：\n"),HasSession()?Latest.UpgradePrice:LatestContent->Parameters.UpgradeCost,LDTerms::Gold)+FString::Join(LDHUD::UpgradeLines(*Definition),TEXT("\n"));}
        else{Body+=TEXT("\n暂不能进阶");}
    }
    ShopPanelText(DetailLines,Body);
    // Display hints only: Buy stays available and the server's own refusal is what the feedback reports.
    TArray<FString> HintParts;
    if(bEquipKnown&&!ShownItem.bAffordable){HintParts.Add(LDTerms::NotEnoughGold());}
    if(bEquipKnown&&ShopPanelEquipSlot(LatestEquipment,0)->bOccupied&&ShopPanelEquipSlot(LatestEquipment,1)->bOccupied){HintParts.Add(TEXT("装备槽已满，请先卖出一件"));}
    if(PreorderQueue.Contains(Item.DefinitionId)){HintParts.Add(TEXT("已在预购中"));}
    HintParts.Add(bLatestNear&&HasSession()?TEXT("右键直接购买"):TEXT("右键加入预购，到了自动买"));
    ShopPanelText(DetailHint,FString::Join(HintParts,TEXT("；")));
}
bool ULDShopPanel::SelectItem(int32 Index)
{
    if(bSelling||!Listing.IsValidIndex(Index)){return false;}
    Selected=Index;SelectedItem=Listing[Index].DefinitionId;
    if(!PendingRequest){ShopPanelText(Feedback,TEXT("已选中 ")+LDHUD::ContentName(LatestContent,SelectedItem.ToString())+(bLatestNear&&HasSession()?TEXT("，可以点购买，或右键图标直接购买"):TEXT("，右键图标可以加入预购")));}
    RefreshShop();return true;
}
void ULDShopPanel::Buy()
{
    if(bSelling){ShopPanelText(Feedback,TEXT("请先确认或返回当前的卖出确认"));return;}
    const FString Block=CommandBlock();
    if(!Block.IsEmpty()){ShopPanelText(Feedback,Block);return;}
    if(!Listing.IsValidIndex(Selected)){ShopPanelText(Feedback,TEXT("请先点选一件商品"));return;}
    BuyItem(Listing[Selected].DefinitionId,false);
}
bool ULDShopPanel::BuyItem(FName DefinitionId,bool bAuto)
{
    // Always at the price of this life's session quote, never the catalog's.
    const FLDShopItemView* ItemView=HasSession()?Latest.Items.FindByPredicate([DefinitionId](const FLDShopItemView& Row){return Row.DefinitionId==DefinitionId;}):nullptr;
    if(!ItemView)
    {
        ShopPanelText(Feedback,FString(TEXT("这件商品不在当前"))+LDTerms::Shop+TEXT("报价里"));
        if(bAuto){BlockAutoBuy();}
        return false;
    }
    FLDShopCommand Command;
    if(!FillCommand(ELDShopCommandKind::Buy,Command)){if(bAuto){BlockAutoBuy();}return false;}
    if(!FLDDefinitionWire::Encode(ItemView->DefinitionId,Command.DefinitionId))
    {
        UE_LOG(LogTemp,Error,TEXT("LD_SHOP_PANEL_ITEM_ID_INVALID Shop session %s lists item '%s', which is not a legal definition id."),*Latest.SessionId.ToString(),*ItemView->DefinitionId.ToString());
        ShopPanelText(Feedback,TEXT("内容标识非法，本次没有提交"));
        if(bAuto){BlockAutoBuy();}
        return false;
    }
    Command.BuyPrice=ItemView->BuyPrice;
    // Recorded before the submit: a Listen Host answers inside it.
    PendingDefinition=DefinitionId;bPendingAuto=bAuto;
    const bool bSent=Send(Command,(bAuto?FString(TEXT("预购 ")):FString())+LDHUD::ContentName(LatestContent,ItemView->DefinitionId.ToString()));
    if(!bSent){PendingDefinition=NAME_None;bPendingAuto=false;if(bAuto){BlockAutoBuy();}}
    return bSent;
}
bool ULDShopPanel::RequestSell(uint8 SlotIndex)
{
    if(!bOpen){ShopPanelText(Feedback,FString(LDTerms::Shop)+TEXT("未打开"));return false;}
    if(bSelling){ShopPanelText(Feedback,TEXT("请先确认或返回当前的卖出确认"));return false;}
    const FString Block=CommandBlock();
    if(!Block.IsEmpty()){ShopPanelText(Feedback,Block);return false;}
    const FLDShopSellView* SellEntry=SlotIndex<2?SellableFor(SlotIndex):nullptr;
    if(!SellEntry){ShopPanelText(Feedback,TEXT("这个装备槽当前没有可卖出的装备"));return false;}
    bSelling=true;SellInstance=SellEntry->Instance;SellItemRevision=SellEntry->ItemRevision;SellQuotePrice=SellEntry->SellPrice;
    SellName=LDHUD::ContentName(LatestContent,SellEntry->DefinitionId.ToString());
    ShopPanelText(SellPrompt,FString::Printf(TEXT("卖出 %s，获得 %lld %s。卖出不可撤销。"),*SellName,SellQuotePrice,LDTerms::Gold));
    if(SellPanel){SellPanel->SetVisibility(ESlateVisibility::Visible);}
    ShopPanelText(Feedback,TEXT("请确认是否卖出"));
    RefreshShop();return true;
}
bool ULDShopPanel::RequestUpgrade(uint8 SlotIndex)
{
    if(!bOpen){ShopPanelText(Feedback,FString(LDTerms::Shop)+TEXT("未打开"));return false;}
    if(bSelling){ShopPanelText(Feedback,TEXT("请先确认或返回当前的确认"));return false;}
    const FString Block=CommandBlock();
    if(!Block.IsEmpty()){ShopPanelText(Feedback,Block);return false;}
    const FLDShopSellView* Entry=SlotIndex<2?SellableFor(SlotIndex):nullptr;
    if(!Entry||!Entry->bCanUpgrade)
    {ShopPanelText(Feedback,Latest.bUpgradeUsed?TEXT("本局的装备进阶已经用过了"):TEXT("这个装备槽当前没有可进阶的装备"));return false;}
    bSelling=true;bUpgrading=true;SellInstance=Entry->Instance;SellItemRevision=Entry->ItemRevision;UpgradeQuotePrice=Latest.UpgradePrice;
    SellName=LDHUD::ContentName(LatestContent,Entry->DefinitionId.ToString());
    ShopPanelText(SellPrompt,FString::Printf(TEXT("花 %lld %s 进阶 %s：固定属性提高，被动与主动增强一档。每局只能进阶一次，卖出这件后进阶随之失去，费用不返还；冷却不刷新。"),
        UpgradeQuotePrice,LDTerms::Gold,*SellName));
    if(SellPanel){SellPanel->SetVisibility(ESlateVisibility::Visible);}
    ShopPanelText(Feedback,TEXT("请确认是否进阶"));
    RefreshShop();return true;
}
void ULDShopPanel::ConfirmSell()
{
    if(!bSelling){ShopPanelText(Feedback,TEXT("请先点选要卖出的装备"));return;}
    const FString Block=CommandBlock();
    if(!Block.IsEmpty()){ShopPanelText(Feedback,Block);return;}
    const FLDShopSellView* SellEntry=Latest.Sellable.FindByPredicate([this](const FLDShopSellView& SellRow){return SellRow.Instance==SellInstance&&SellRow.ItemRevision==SellItemRevision;});
    if(bUpgrading)
    {
        if(!SellEntry||!SellEntry->bCanUpgrade||Latest.UpgradePrice!=UpgradeQuotePrice)
        {ResetSell();ShopPanelText(Feedback,TEXT("进阶条件已变化，确认已撤销"));RefreshShop();return;}
        FLDShopCommand Command;
        if(!FillCommand(ELDShopCommandKind::Upgrade,Command)){return;}
        Command.Instance=SellEntry->Instance;Command.ExpectedItemRevision=SellEntry->ItemRevision;Command.UpgradePrice=UpgradeQuotePrice;
        if(Send(Command,SellName)){ResetSell();RefreshShop();}
        return;
    }
    if(!SellEntry||SellEntry->SellPrice!=SellQuotePrice)
    {
        ResetSell();ShopPanelText(Feedback,SellEntry?TEXT("卖价已变化，卖出确认已撤销，请重新确认"):TEXT("装备已变化，卖出确认已撤销"));RefreshShop();return;
    }
    FLDShopCommand Command;
    if(!FillCommand(ELDShopCommandKind::Sell,Command)){return;}
    Command.Instance=SellEntry->Instance;Command.ExpectedItemRevision=SellEntry->ItemRevision;Command.SellPrice=SellEntry->SellPrice;
    if(Send(Command,SellName)){ResetSell();RefreshShop();}
}
bool ULDShopPanel::FillCommand(ELDShopCommandKind CommandKind,FLDShopCommand& Command)
{
    // RequestId stays 0: the growth entry allocates it. Every guard is the one the panel showed.
    Command.Generation=LatestMatch;Command.Life=LatestLife;Command.Kind=CommandKind;
    Command.SessionId=Latest.SessionId;Command.QuoteRevision=Latest.QuoteRevision;Command.CatalogDigest=Latest.CatalogDigest;
    Command.WorldLayoutRevision=Latest.WorldLayoutRevision;Command.WorldGeometryRevision=Latest.WorldGeometryRevision;
    Command.ExpectedAccountRevision=LatestEquipment.AccountRevision;Command.ExpectedInventoryRevision=LatestEquipment.InventoryRevision;
    Command.ExpectedBuildRevision=LatestBuildRevision;
    if(FLDDefinitionWire::Encode(Latest.WorldMapId,Command.WorldMapId)){return true;}
    UE_LOG(LogTemp,Error,TEXT("LD_SHOP_PANEL_WORLD_MAP_ID_INVALID Shop session %s quotes world map '%s', which is not a legal definition id."),*Latest.SessionId.ToString(),*Latest.WorldMapId.ToString());
    ShopPanelText(Feedback,TEXT("内容标识非法，本次没有提交"));return false;
}
bool ULDShopPanel::Send(const FLDShopCommand& Command,FString ItemName)
{
    if(!SubmitShopCommand)
    {
        UE_LOG(LogTemp,Error,TEXT("LD_SHOP_PANEL_SUBMIT_UNAVAILABLE The growth shop entry is not connected to this panel."));
        ShopPanelText(Feedback,FString(LDTerms::Shop)+TEXT("入口不可用，本次没有提交"));return false;
    }
    if(!PeekRequestId){UE_LOG(LogTemp,Error,TEXT("LD_SHOP_PANEL_PEEK_UNAVAILABLE The growth request-id peek is not connected; a Listen Host answer inside the submit call cannot be matched."));}
    // A Listen Host answers inside the submit call: the request and its terms are recorded under the announced id first,
    // and an answer that already arrived keeps its own feedback and pending state.
    const uint64 PeekedRequest=PeekRequestId?PeekRequestId():0;
    PendingRequest=PeekedRequest;PendingKind=Command.Kind;PendingName=MoveTemp(ItemName);
    PendingPrice=Command.Kind==ELDShopCommandKind::Buy?Command.BuyPrice:Command.Kind==ELDShopCommandKind::Upgrade?Command.UpgradePrice:Command.SellPrice;
    const uint32 AnswersBefore=AnswerCount;
    ELDGrowthReason RefusalReason=ELDGrowthReason::None;
    const uint64 AllocatedRequest=SubmitShopCommand(Command,RefusalReason);
    if(!AllocatedRequest)
    {
        if(PendingRequest==PeekedRequest){ClearPending();}
        ShopPanelText(Feedback,TEXT("没有提交：")+ShopPanelReason(RefusalReason));RefreshShop();return false;
    }
    if(PeekedRequest&&AllocatedRequest!=PeekedRequest)
    {UE_LOG(LogTemp,Error,TEXT("LD_SHOP_PANEL_REQUEST_ID_MISMATCH The peek announced request %llu but the entry allocated %llu."),PeekedRequest,AllocatedRequest);}
    if(AnswerCount==AnswersBefore){PendingRequest=AllocatedRequest;ShopPanelText(Feedback,TEXT("已提交，等待服务器确认…"));}
    RefreshShop();return true;
}
void ULDShopPanel::HandleSubmit(const FLDSubmitResult& Result)
{
    if(Result.Family!=ELDGrowthFamily::Shop||!PendingRequest||Result.RequestId!=PendingRequest||Result.Generation!=LatestMatch){return;}
    ++AnswerCount;
    // Pending ends exactly where the controller ends it: Final, or an admission refusal that reserved no sequence.
    switch(Result.Status)
    {
    case ELDAdmissionStatus::AdmissionRejected:
        if(Result.bSequenceReserved)
        {ShopPanelText(Feedback,TEXT("没有受理：")+ShopPanelReason(LDGrowthWire::Reason(Result.Reason))+TEXT("，等待最终回执…"));return;}
        if(bPendingAuto){BlockAutoBuy();}
        FinishRequest(TEXT("没有受理：")+ShopPanelReason(LDGrowthWire::Reason(Result.Reason)));return;
    case ELDAdmissionStatus::AcceptedPending: ShopPanelText(Feedback,TEXT("已受理，等待结算…"));return;
    case ELDAdmissionStatus::Final:
    {
        FLDShopReceiptQueryResult FinalQuery;
        FinalQuery.Query.Generation=Result.Generation;FinalQuery.Query.Family=Result.Family;FinalQuery.Query.RequestId=Result.RequestId;
        FinalQuery.Lookup=ELDReceiptLookup::Final;FinalQuery.bHasReceipt=Result.bHasShopReceipt;FinalQuery.Receipt=Result.ShopReceipt;
        AnswerReceipt(FinalQuery);return;
    }
    default:
        UE_LOG(LogTemp,Error,TEXT("LD_SHOP_PANEL_SUBMIT_STATUS_INVALID Request %llu returned admission status %d."),Result.RequestId,int32(Result.Status));
        ShopPanelText(Feedback,TEXT("提交结果无效，请以当前状态为准"));return;
    }
}
void ULDShopPanel::HandleReceipt(const FLDShopReceiptQueryResult& Result)
{
    if(Result.Query.Family!=ELDGrowthFamily::Shop||!PendingRequest||Result.Query.RequestId!=PendingRequest||Result.Query.Generation!=LatestMatch){return;}
    ++AnswerCount;AnswerReceipt(Result);
}
void ULDShopPanel::AnswerReceipt(const FLDShopReceiptQueryResult& Result)
{
    switch(Result.Lookup)
    {
    case ELDReceiptLookup::Final:
        if(!Result.bHasReceipt){FinishRequest(TEXT("回执缺少结果，请以当前状态为准"));return;}
        if(Result.Receipt.Header.Outcome==ELDReceiptOutcome::Applied)
        {
            const ELDShopCommandKind DoneKind=Result.Receipt.Kind!=ELDShopCommandKind::None?Result.Receipt.Kind:PendingKind;
            const FName Bought=Result.Receipt.DefinitionId.IsNone()?PendingDefinition:Result.Receipt.DefinitionId;
            // A bought definition leaves the pre-orders once, whether a pre-order or the player bought it.
            if(DoneKind==ELDShopCommandKind::Buy&&!Bought.IsNone()){PreorderQueue.RemoveSingle(Bought);}
            FinishRequest(AppliedNotice(Result.Receipt));return;
        }
        if(Result.Receipt.Header.Outcome==ELDReceiptOutcome::Rejected)
        {
            if(bPendingAuto){BlockAutoBuy();}
            FinishRequest(TEXT("没有成交：")+ShopPanelReason(LDGrowthWire::Reason(Result.Receipt.Header.Reason)));return;
        }
        UE_LOG(LogTemp,Error,TEXT("LD_SHOP_PANEL_RECEIPT_OUTCOME_INVALID Request %llu has a final receipt with outcome %d."),PendingRequest,int32(Result.Receipt.Header.Outcome));
        FinishRequest(TEXT("回执结果无效，请以当前状态为准"));return;
    case ELDReceiptLookup::ReceiptExpired: FinishRequest(TEXT("回执已过期，请以当前状态为准"));return;
    // The controller still holds these in flight, so the panel keeps the request pending with it.
    case ELDReceiptLookup::Pending: ShopPanelText(Feedback,TEXT("已受理，等待结算…"));return;
    case ELDReceiptLookup::NotFound: ShopPanelText(Feedback,TEXT("服务器没有找到这次交易，请以当前状态为准"));return;
    case ELDReceiptLookup::InvalidContext: ShopPanelText(Feedback,TEXT("回执查询无效，请以当前状态为准"));return;
    default:
        UE_LOG(LogTemp,Error,TEXT("LD_SHOP_PANEL_RECEIPT_LOOKUP_INVALID Request %llu returned receipt lookup %d."),PendingRequest,int32(Result.Lookup));
        ShopPanelText(Feedback,TEXT("回执查询无效，请以当前状态为准"));return;
    }
}
FString ULDShopPanel::AppliedNotice(const FLDShopReceipt& Receipt) const
{
    // Notice only: the new gold and equipment come from the next owner publication, never from this receipt.
    const ELDShopCommandKind DoneKind=Receipt.Kind!=ELDShopCommandKind::None?Receipt.Kind:PendingKind;
    const FString ItemName=Receipt.DefinitionId.IsNone()?PendingName:LDHUD::ContentName(LatestContent,Receipt.DefinitionId.ToString());
    const int64 GoldMoved=FMath::Abs(Receipt.ActualGoldDelta);
    // A trade settles at the quoted price; the receipt still decides what is shown.
    if(GoldMoved!=PendingPrice)
    {UE_LOG(LogTemp,Warning,TEXT("LD_SHOP_PANEL_GOLD_DELTA_DIFFERS Request %llu moved %lld gold for a quoted price of %lld."),PendingRequest,GoldMoved,PendingPrice);}
    if(DoneKind==ELDShopCommandKind::Buy)
    {
        FString Summary=FString::Printf(TEXT("已买入 %s，花费 %lld %s"),*ItemName,GoldMoved,LDTerms::Gold);
        if(Receipt.bHasSource&&bLatestClock&&Receipt.ReadyAtMs>LatestNowMs){Summary+=TEXT("，继承冷却 ")+LDHUD::TenthsUp(Receipt.ReadyAtMs-LatestNowMs);}
        return Summary;
    }
    if(DoneKind==ELDShopCommandKind::Sell){return FString::Printf(TEXT("已卖出 %s，获得 %lld %s"),*ItemName,Receipt.ActualGoldDelta,LDTerms::Gold);}
    if(DoneKind==ELDShopCommandKind::Upgrade){return FString::Printf(TEXT("已进阶 %s，花费 %lld %s"),*ItemName,GoldMoved,LDTerms::Gold);}
    UE_LOG(LogTemp,Error,TEXT("LD_SHOP_PANEL_RECEIPT_KIND_INVALID Request %llu was applied with shop kind %d."),PendingRequest,int32(Receipt.Kind));
    return TEXT("交易已成交，请以当前状态为准");
}
void ULDShopPanel::ClearPending()
{
    PendingRequest=0;PendingKind=ELDShopCommandKind::None;PendingName.Reset();PendingPrice=0;PendingDefinition=NAME_None;bPendingAuto=false;
}
void ULDShopPanel::FinishRequest(const FString& Notice)
{
    const bool bAuto=bPendingAuto;
    ClearPending();ShopPanelText(Feedback,Notice);
    // A result that lands after the panel closed still reaches the player, without reopening the panel; a pre-order's
    // result is told the same way, as nobody clicked for it.
    if(!bOpen){NotifyPlayer(bAuto?FString(TEXT("预购："))+Notice:Notice);}
    RefreshShop();
}
void ULDShopPanel::RefreshPreorders()
{
    if(!PreorderLine){return;}
    const int64 Gold=EquipmentKnown()?LatestEquipment.Gold:0;
    ShopPanelText(PreorderLine,PreorderQueue.IsEmpty()
        ?FString::Printf(TEXT("预购：空（最多 %d 件，到%s附近且%s够时自动买入）"),LDHUD::MaxShopPreorders,LDTerms::Shop,LDTerms::Gold)
        :FString::Printf(TEXT("预购（点一件可取消）：到%s附近且%s够时自动买入"),LDTerms::Shop,LDTerms::Gold));
    for(int32 ChipIdx=0;ChipIdx<2;++ChipIdx)
    {
        ULDCommandButton* Chip=PreorderChips[ChipIdx];
        const bool bShown=PreorderQueue.IsValidIndex(ChipIdx);
        Chip->SetVisibility(bShown?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
        if(!bShown){continue;}
        const FName Id=PreorderQueue[ChipIdx];
        const FLDShopItemView* Row=Listing.FindByPredicate([Id](const FLDShopItemView& Item){return Item.DefinitionId==Id;});
        FString Caption=FString::Printf(TEXT("%d. %s"),ChipIdx+1,*LDHUD::ContentName(LatestContent,Id.ToString()));
        if(Row){Caption+=FString::Printf(TEXT(" · %lld"),Row->BuyPrice);if(EquipmentKnown()&&Row->BuyPrice>Gold){Caption+=TEXT("（")+LDTerms::NotEnoughGold()+TEXT("）");}}
        Caption+=TEXT(" ✕");
        ShopPanelText(CastChecked<UTextBlock>(Chip->GetContent()),Caption);
    }
}
void ULDShopPanel::NotifyPlayer(const FString& Notice) const
{
    if(Notify){Notify(Notice);return;}
    UE_LOG(LogTemp,Error,TEXT("LD_SHOP_PANEL_NOTIFY_UNAVAILABLE The HUD did not connect shop notices; undelivered: %s"),*Notice);
}
