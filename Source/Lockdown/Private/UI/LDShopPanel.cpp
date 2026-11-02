#include "UI/LDShopPanel.h"
#include "UI/LDHUDModel.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace
{
const FLinearColor ShopPanelIdle(.045f,.12f,.15f),ShopPanelSelected(.26f,.2f,.05f),ShopPanelHintColor(1.f,.66f,.36f);
void ShopPanelTint(ULDCommandButton* Tinted,const FLinearColor& Tint)
{
    FButtonStyle TintedStyle=Tinted->GetStyle();
    if(TintedStyle.Normal.TintColor==FSlateColor(Tint)){return;}
    TintedStyle.Normal.TintColor=FSlateColor(Tint);TintedStyle.Hovered.TintColor=FSlateColor(Tint*1.6f);Tinted->SetStyle(TintedStyle);
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
        HeadRow->AddChildToHorizontalBox(MakeText(TEXT("小卖部"),20))->SetVerticalAlignment(VAlign_Center);
        GoldLine=MakeText(TEXT("零花钱 同步中"),16);
        UHorizontalBoxSlot* GoldCell=HeadRow->AddChildToHorizontalBox(GoldLine);
        GoldCell->SetSize(FSlateChildSize(ESlateSizeRule::Fill));GoldCell->SetPadding(FMargin(16,0));GoldCell->SetVerticalAlignment(VAlign_Center);
        HeadRow->AddChildToHorizontalBox(LDWidgets::Button(WidgetTree,TEXT("关闭"),[this](){ClosePanel();},false,TEXT("ShopClose")));
        for(uint8 SlotIdx=0;SlotIdx<2;++SlotIdx)
        {
            auto* EquipRow=WidgetTree->ConstructWidget<UHorizontalBox>();Layout->AddChildToVerticalBox(EquipRow)->SetPadding(FMargin(0.f,SlotIdx?6.f:14.f,0.f,0.f));
            EquipNames[SlotIdx]=MakeText(TEXT(""),16);
            UHorizontalBoxSlot* NameCell=EquipRow->AddChildToHorizontalBox(EquipNames[SlotIdx]);
            NameCell->SetSize(FSlateChildSize(ESlateSizeRule::Fill));NameCell->SetVerticalAlignment(VAlign_Center);
            SellButtons[SlotIdx]=LDWidgets::Button(WidgetTree,TEXT("卖出"),[this,SlotIdx](){RequestSell(SlotIdx);},false,SlotIdx?FName(TEXT("ShopSell1")):FName(TEXT("ShopSell0")));
            EquipRow->AddChildToHorizontalBox(SellButtons[SlotIdx]);
        }
        auto* Middle=WidgetTree->ConstructWidget<UHorizontalBox>();Layout->AddChildToVerticalBox(Middle)->SetPadding(FMargin(0,14,0,0));
        ItemList=WidgetTree->ConstructWidget<UVerticalBox>();
        auto* ListScroll=WidgetTree->ConstructWidget<UScrollBox>();ListScroll->SetConsumeMouseWheel(EConsumeMouseWheel::Always);ListScroll->AddChild(ItemList);
        auto* ListBox=WidgetTree->ConstructWidget<USizeBox>();ListBox->SetWidthOverride(300);ListBox->SetMaxDesiredHeight(330);ListBox->SetContent(ListScroll);
        Middle->AddChildToHorizontalBox(ListBox)->SetVerticalAlignment(VAlign_Top);
        auto* DetailBox=WidgetTree->ConstructWidget<UVerticalBox>();
        DetailName=MakeText(TEXT(""),20);DetailBox->AddChildToVerticalBox(DetailName);
        DetailPrice=MakeText(TEXT(""),15);DetailBox->AddChildToVerticalBox(DetailPrice)->SetPadding(FMargin(0,4,0,0));
        DetailLines=MakeText(TEXT(""),14);DetailBox->AddChildToVerticalBox(DetailLines)->SetPadding(FMargin(0,8,0,0));
        DetailHint=MakeText(TEXT(""),14);DetailHint->SetColorAndOpacity(ShopPanelHintColor);DetailBox->AddChildToVerticalBox(DetailHint)->SetPadding(FMargin(0,8,0,0));
        UHorizontalBoxSlot* DetailCell=Middle->AddChildToHorizontalBox(DetailBox);
        DetailCell->SetSize(FSlateChildSize(ESlateSizeRule::Fill));DetailCell->SetPadding(FMargin(16,0,0,0));DetailCell->SetVerticalAlignment(VAlign_Top);
        auto* FootRow=WidgetTree->ConstructWidget<UHorizontalBox>();Layout->AddChildToVerticalBox(FootRow)->SetPadding(FMargin(0,14,0,0));
        Feedback=MakeText(TEXT(""),15);
        UHorizontalBoxSlot* FeedbackCell=FootRow->AddChildToHorizontalBox(Feedback);
        FeedbackCell->SetSize(FSlateChildSize(ESlateSizeRule::Fill));FeedbackCell->SetPadding(FMargin(0,0,12,0));FeedbackCell->SetVerticalAlignment(VAlign_Center);
        BuyButton=LDWidgets::Button(WidgetTree,TEXT("购买"),[this](){Buy();},false,TEXT("ShopBuy"));
        FootRow->AddChildToHorizontalBox(BuyButton)->SetVerticalAlignment(VAlign_Center);
        auto* SellBox=WidgetTree->ConstructWidget<UVerticalBox>();
        SellPrompt=MakeText(TEXT(""),16);SellBox->AddChildToVerticalBox(SellPrompt);
        auto* SellActions=WidgetTree->ConstructWidget<UHorizontalBox>();SellBox->AddChildToVerticalBox(SellActions)->SetPadding(FMargin(0,10,0,0));
        SellConfirmButton=LDWidgets::Button(WidgetTree,TEXT("确认卖出"),[this](){ConfirmSell();},false,TEXT("ShopSellConfirm"));
        SellActions->AddChildToHorizontalBox(SellConfirmButton)->SetPadding(FMargin(0,0,10,0));
        SellActions->AddChildToHorizontalBox(LDWidgets::Button(WidgetTree,TEXT("返回"),[this](){CloseChildConfirmation();},false,TEXT("ShopSellBack")));
        SellPanel=LDWidgets::Panel(WidgetTree,SellBox,FMargin(16));SellPanel->SetBrushColor(FLinearColor(.06f,.05f,.03f,.99f));SellPanel->SetVisibility(ESlateVisibility::Collapsed);
        Layout->AddChildToVerticalBox(SellPanel)->SetPadding(FMargin(0,12,0,0));
        MainPanel=LDWidgets::Panel(WidgetTree,Layout,FMargin(20));
        auto* WidthBox=WidgetTree->ConstructWidget<USizeBox>();WidthBox->SetWidthOverride(760);WidthBox->SetContent(MainPanel);
        auto* FrameBorder=WidgetTree->ConstructWidget<UBorder>();FrameBorder->SetBrushColor(FLinearColor::Transparent);
        FrameBorder->SetHorizontalAlignment(HAlign_Center);FrameBorder->SetVerticalAlignment(VAlign_Top);
        FrameBorder->SetPadding(FMargin(0,140,0,0));FrameBorder->SetVisibility(ESlateVisibility::SelfHitTestInvisible);FrameBorder->SetContent(WidthBox);
        WidgetTree->RootWidget=FrameBorder;
        SetVisibility(bOpen?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
        RefreshShop();
    }
    return Super::RebuildWidget();
}
UWidget* ULDShopPanel::FocusScope() const{return bSelling?static_cast<UWidget*>(SellPanel.Get()):static_cast<UWidget*>(MainPanel.Get());}
UWidget* ULDShopPanel::HitArea() const{return MainPanel;}
void ULDShopPanel::OpenPanel()
{
    if(bOpen){return;}
    bOpen=true;SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    if(!PendingRequest){ShopPanelText(Feedback,TEXT("点选商品查看说明，再点购买；卖出需要确认。对局不会暂停，小卖部也不是安全区"));}
    RefreshShop();
}
bool ULDShopPanel::ClosePanel()
{
    // Closing only hides the panel: the session and any submitted request keep their real state, and a later result
    // still reaches the player as a notice without reopening the panel.
    ResetSell();bOpen=false;SetVisibility(ESlateVisibility::Collapsed);return true;
}
bool ULDShopPanel::CloseChildConfirmation()
{
    if(!bSelling){return false;}
    ResetSell();ShopPanelText(Feedback,TEXT("已返回，没有卖出"));RefreshShop();return true;
}
void ULDShopPanel::ResetSell()
{
    bSelling=false;SellInstance.Invalidate();SellItemRevision=0;SellQuotePrice=0;SellName.Reset();
    if(SellPanel){SellPanel->SetVisibility(ESlateVisibility::Collapsed);}
}
void ULDShopPanel::Present(const FLDShopView* View,const FLDEquipmentView* EquipView,bool bBuildKnown,uint64 BuildRevision,uint32 Match,uint32 Life,ELDLifePhase Phase,bool bClock,int64 NowMs,const FLDDemoContentDefinitions* ContentDefs)
{
    if(bOpen)
    {
        // Death and the end of the session, life or match close the shop; taking damage never does.
        const TCHAR* CloseReason=nullptr;
        if(bPresented&&Match!=LatestMatch){CloseReason=TEXT("对局已变化");}
        else if(bPresented&&Life!=LatestLife){CloseReason=TEXT("生命已变化");}
        else if(Phase!=ELDLifePhase::Alive){CloseReason=Phase==ELDLifePhase::Dead?TEXT("社死中不能交易"):TEXT("当前状态不能交易");}
        else if(bLatestView&&!View){CloseReason=TEXT("会话已结束");}
        if(CloseReason){ClosePanel();NotifyPlayer(FString(TEXT("小卖部已关闭："))+CloseReason);}
    }
    if(bPresented&&Match!=LatestMatch&&PendingRequest)
    {
        // A request of an older match can no longer be answered here; the new match starts without it.
        ClearPending();ShopPanelText(Feedback,TEXT("对局已变化，上一笔交易请以当前状态为准"));
    }
    bLatestView=View!=nullptr;Latest=View?*View:FLDShopView();LatestEquipment=EquipView?*EquipView:FLDEquipmentView();
    bLatestBuildKnown=bBuildKnown;LatestBuildRevision=BuildRevision;LatestMatch=Match;LatestLife=Life;LatestPhase=Phase;
    bLatestClock=bClock;LatestNowMs=NowMs;LatestContent=ContentDefs;bPresented=true;
    if(Selected!=INDEX_NONE&&(!Latest.Items.IsValidIndex(Selected)||Latest.Items[Selected].DefinitionId!=SelectedItem))
    {
        // A new session or quote keeps the preview only while the same definition is still listed.
        Selected=Latest.Items.IndexOfByPredicate([this](const FLDShopItemView& ItemView){return ItemView.DefinitionId==SelectedItem;});
        if(Selected==INDEX_NONE){SelectedItem=NAME_None;if(bOpen&&!PendingRequest){ShopPanelText(Feedback,TEXT("商品列表已更新，请重新选择"));}}
    }
    if(bSelling)
    {
        // The confirmation answers one exact item at one price: a changed item or quote withdraws it, never redirects it.
        const FLDShopSellView* SellEntry=Latest.Sellable.FindByPredicate([this](const FLDShopSellView& SellRow){return SellRow.Instance==SellInstance&&SellRow.ItemRevision==SellItemRevision;});
        if(!SellEntry){ResetSell();ShopPanelText(Feedback,TEXT("装备已变化，卖出确认已撤销"));}
        else if(SellEntry->SellPrice!=SellQuotePrice){ResetSell();ShopPanelText(Feedback,TEXT("卖价已变化，卖出确认已撤销，请重新确认"));}
    }
    if(bOpen){RefreshShop();}
}
bool ULDShopPanel::EquipmentKnown() const
{
    return LatestEquipment.bValid&&LatestEquipment.Generation==LatestMatch
        &&ShopPanelEquipSlot(LatestEquipment,0)!=nullptr&&ShopPanelEquipSlot(LatestEquipment,1)!=nullptr;
}
const FLDShopSellView* ULDShopPanel::SellableFor(uint8 SlotIdx) const
{
    // Offered only when the quote lists the exact instance that the own equipment view shows in that slot.
    const FLDEquipmentSlotView* EquipSlot=bLatestView&&EquipmentKnown()?ShopPanelEquipSlot(LatestEquipment,SlotIdx):nullptr;
    if(!EquipSlot||!EquipSlot->bOccupied){return nullptr;}
    return Latest.Sellable.FindByPredicate([EquipSlot](const FLDShopSellView& SellRow){return SellRow.SlotIndex==EquipSlot->SlotIndex&&SellRow.Instance==EquipSlot->Instance;});
}
FString ULDShopPanel::CommandBlock() const
{
    // Why no command can be built from what the panel shows; empty when one can. Range, gold, capacity and prices
    // are not judged here: the growth entry and the server answer those with their own reasons.
    if(PendingRequest){return TEXT("上一笔交易仍在处理中");}
    if(!bLatestView){return TEXT("报价同步中，暂时不能交易");}
    if(Latest.Life!=LatestLife){return TEXT("报价不属于当前生命，等待同步");}
    if(!EquipmentKnown()){return TEXT("装备数据同步中，暂时不能交易");}
    if(!bLatestBuildKnown){return TEXT("构筑数据同步中，暂时不能交易");}
    if(LatestPhase!=ELDLifePhase::Alive){return LatestPhase==ELDLifePhase::Dead?TEXT("社死中不能交易"):TEXT("当前状态不能交易");}
    return FString();
}
void ULDShopPanel::RefreshShop()
{
    if(!MainPanel){return;}
    const bool bEquipKnown=EquipmentKnown();
    const FString Block=CommandBlock();
    ShopPanelText(GoldLine,bEquipKnown?LDHUD::Gold(LatestEquipment.Gold):FString(TEXT("零花钱 同步中")));
    for(uint8 SlotIdx=0;SlotIdx<2;++SlotIdx)
    {
        const FLDEquipmentSlotView* EquipSlot=bEquipKnown?ShopPanelEquipSlot(LatestEquipment,SlotIdx):nullptr;
        const FString Holding=!EquipSlot?FString(TEXT("同步中")):EquipSlot->bOccupied?LDHUD::ContentName(LatestContent,EquipSlot->DefinitionId.ToString()):FString(TEXT("空"));
        ShopPanelText(EquipNames[SlotIdx],FString::Printf(TEXT("装备 %d：%s"),SlotIdx+1,*Holding));
        const FLDShopSellView* SellEntry=SellableFor(SlotIdx);
        const bool bCanSell=SellEntry&&Block.IsEmpty()&&!bSelling;
        SellButtons[SlotIdx]->SetIsEnabled(bCanSell);
        ShopPanelText(CastChecked<UTextBlock>(SellButtons[SlotIdx]->GetContent()),bCanSell?FString::Printf(TEXT("卖出 +%lld"),SellEntry->SellPrice):FString(TEXT("卖出")));
    }
    SyncItemButtons();
    for(int32 ItemIdx=0;ItemIdx<ShownItems.Num();++ItemIdx)
    {
        const FLDShopItemView& ItemView=Latest.Items[ItemIdx];
        ULDCommandButton* ItemButton=ItemButtons[ItemIdx];
        ShopPanelText(CastChecked<UTextBlock>(ItemButton->GetContent()),FString::Printf(TEXT("%s · %lld"),*LDHUD::ContentName(LatestContent,ItemView.DefinitionId.ToString()),ItemView.BuyPrice));
        ShopPanelTint(ItemButton,ItemIdx==Selected?ShopPanelSelected:ShopPanelIdle);
        ItemButton->SetIsEnabled(!bSelling);
    }
    if(Latest.Items.IsValidIndex(Selected))
    {
        const LDHUD::FShopItemView ShownItem=LDHUD::ShopItem(Latest.Items[Selected],bEquipKnown?LatestEquipment.Gold:0,bLatestClock,LatestNowMs,LatestContent);
        ShopPanelText(DetailName,ShownItem.Name);ShopPanelText(DetailPrice,ShownItem.Price);
        ShopPanelText(DetailLines,FString::Join(ShownItem.Lines,TEXT("\n")));
        // Display hints only: Buy stays available and the server's own refusal is what the feedback reports.
        TArray<FString> HintParts;
        if(bEquipKnown&&!ShownItem.bAffordable){HintParts.Add(TEXT("零花钱不足"));}
        if(bEquipKnown&&ShopPanelEquipSlot(LatestEquipment,0)->bOccupied&&ShopPanelEquipSlot(LatestEquipment,1)->bOccupied){HintParts.Add(TEXT("装备槽已满，请先卖出一件"));}
        ShopPanelText(DetailHint,FString::Join(HintParts,TEXT("；")));
    }
    else
    {
        ShopPanelText(DetailName,!bLatestView?TEXT("报价同步中…"):Latest.Items.IsEmpty()?TEXT("当前没有可购买的商品"):TEXT("点选商品查看说明"));
        ShopPanelText(DetailPrice,FString());ShopPanelText(DetailLines,FString());ShopPanelText(DetailHint,FString());
    }
    BuyButton->SetIsEnabled(Latest.Items.IsValidIndex(Selected)&&Block.IsEmpty()&&!bSelling);
    SellConfirmButton->SetIsEnabled(bSelling&&Block.IsEmpty());
}
void ULDShopPanel::SyncItemButtons()
{
    TArray<FName> ListedIds;ListedIds.Reserve(Latest.Items.Num());
    for(const FLDShopItemView& ItemView:Latest.Items){ListedIds.Add(ItemView.DefinitionId);}
    if(ListedIds==ShownItems){return;}
    // Only a changed definition list touches the buttons. They are reused by position and the pool only grows,
    // so a stable name never refers to a second widget and hover, focus and scroll survive owner updates.
    for(int32 ItemIdx=ItemButtons.Num();ItemIdx<ListedIds.Num();++ItemIdx)
    {
        ULDCommandButton* ItemButton=LDWidgets::Button(WidgetTree,TEXT(""),[this,ItemIdx](){SelectItem(ItemIdx);},false,FName(*FString::Printf(TEXT("ShopItem%d"),ItemIdx)));
        CastChecked<UTextBlock>(ItemButton->GetContent())->SetAutoWrapText(true);
        ScaleNewText(CastChecked<UTextBlock>(ItemButton->GetContent()));
        if(UButtonSlot* CaptionSlot=Cast<UButtonSlot>(ItemButton->GetContentSlot())){CaptionSlot->SetHorizontalAlignment(HAlign_Left);}
        ItemList->AddChildToVerticalBox(ItemButton)->SetPadding(FMargin(0,0,0,6));
        ItemButtons.Add(ItemButton);
    }
    for(int32 ItemIdx=0;ItemIdx<ItemButtons.Num();++ItemIdx){ItemButtons[ItemIdx]->SetVisibility(ItemIdx<ListedIds.Num()?ESlateVisibility::Visible:ESlateVisibility::Collapsed);}
    ShownItems=MoveTemp(ListedIds);
}
bool ULDShopPanel::SelectItem(int32 Index)
{
    if(bSelling||!Latest.Items.IsValidIndex(Index)){return false;}
    Selected=Index;SelectedItem=Latest.Items[Index].DefinitionId;
    if(!PendingRequest){ShopPanelText(Feedback,TEXT("已选中 ")+LDHUD::ContentName(LatestContent,SelectedItem.ToString())+TEXT("，看清说明与价格后点击购买"));}
    RefreshShop();return true;
}
void ULDShopPanel::Buy()
{
    if(bSelling){ShopPanelText(Feedback,TEXT("请先确认或返回当前的卖出确认"));return;}
    const FString Block=CommandBlock();
    if(!Block.IsEmpty()){ShopPanelText(Feedback,Block);return;}
    if(!Latest.Items.IsValidIndex(Selected)){ShopPanelText(Feedback,TEXT("请先点选一件商品"));return;}
    const FLDShopItemView ItemView=Latest.Items[Selected];
    FLDShopCommand Command;
    if(!FillCommand(ELDShopCommandKind::Buy,Command)){return;}
    if(!FLDDefinitionWire::Encode(ItemView.DefinitionId,Command.DefinitionId))
    {
        UE_LOG(LogTemp,Error,TEXT("LD_SHOP_PANEL_ITEM_ID_INVALID Shop session %s lists item '%s', which is not a legal definition id."),*Latest.SessionId.ToString(),*ItemView.DefinitionId.ToString());
        ShopPanelText(Feedback,TEXT("内容标识非法，本次没有提交"));return;
    }
    Command.BuyPrice=ItemView.BuyPrice;
    Send(Command,LDHUD::ContentName(LatestContent,ItemView.DefinitionId.ToString()));
}
bool ULDShopPanel::RequestSell(uint8 SlotIndex)
{
    if(!bOpen){ShopPanelText(Feedback,TEXT("小卖部未打开"));return false;}
    if(bSelling){ShopPanelText(Feedback,TEXT("请先确认或返回当前的卖出确认"));return false;}
    const FString Block=CommandBlock();
    if(!Block.IsEmpty()){ShopPanelText(Feedback,Block);return false;}
    const FLDShopSellView* SellEntry=SlotIndex<2?SellableFor(SlotIndex):nullptr;
    if(!SellEntry){ShopPanelText(Feedback,TEXT("这个装备槽当前没有可卖出的装备"));return false;}
    bSelling=true;SellInstance=SellEntry->Instance;SellItemRevision=SellEntry->ItemRevision;SellQuotePrice=SellEntry->SellPrice;
    SellName=LDHUD::ContentName(LatestContent,SellEntry->DefinitionId.ToString());
    ShopPanelText(SellPrompt,FString::Printf(TEXT("卖出 %s，获得 %lld 零花钱。卖出不可撤销。"),*SellName,SellQuotePrice));
    if(SellPanel){SellPanel->SetVisibility(ESlateVisibility::Visible);}
    ShopPanelText(Feedback,TEXT("请确认是否卖出"));
    RefreshShop();return true;
}
void ULDShopPanel::ConfirmSell()
{
    if(!bSelling){ShopPanelText(Feedback,TEXT("请先点选要卖出的装备"));return;}
    const FString Block=CommandBlock();
    if(!Block.IsEmpty()){ShopPanelText(Feedback,Block);return;}
    const FLDShopSellView* SellEntry=Latest.Sellable.FindByPredicate([this](const FLDShopSellView& SellRow){return SellRow.Instance==SellInstance&&SellRow.ItemRevision==SellItemRevision;});
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
        ShopPanelText(Feedback,TEXT("小卖部入口不可用，本次没有提交"));return false;
    }
    if(!PeekRequestId){UE_LOG(LogTemp,Error,TEXT("LD_SHOP_PANEL_PEEK_UNAVAILABLE The growth request-id peek is not connected; a Listen Host answer inside the submit call cannot be matched."));}
    // A Listen Host answers inside the submit call: the request and its terms are recorded under the announced id first,
    // and an answer that already arrived keeps its own feedback and pending state.
    const uint64 PeekedRequest=PeekRequestId?PeekRequestId():0;
    PendingRequest=PeekedRequest;PendingKind=Command.Kind;PendingName=MoveTemp(ItemName);
    PendingPrice=Command.Kind==ELDShopCommandKind::Buy?Command.BuyPrice:Command.SellPrice;
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
        if(Result.Receipt.Header.Outcome==ELDReceiptOutcome::Applied){FinishRequest(AppliedNotice(Result.Receipt));return;}
        if(Result.Receipt.Header.Outcome==ELDReceiptOutcome::Rejected){FinishRequest(TEXT("没有成交：")+ShopPanelReason(LDGrowthWire::Reason(Result.Receipt.Header.Reason)));return;}
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
        FString Summary=FString::Printf(TEXT("已买入 %s，花费 %lld 零花钱"),*ItemName,GoldMoved);
        if(Receipt.bHasSource&&bLatestClock&&Receipt.ReadyAtMs>LatestNowMs){Summary+=TEXT("，继承冷却 ")+LDHUD::TenthsUp(Receipt.ReadyAtMs-LatestNowMs);}
        return Summary;
    }
    if(DoneKind==ELDShopCommandKind::Sell){return FString::Printf(TEXT("已卖出 %s，获得 %lld 零花钱"),*ItemName,Receipt.ActualGoldDelta);}
    UE_LOG(LogTemp,Error,TEXT("LD_SHOP_PANEL_RECEIPT_KIND_INVALID Request %llu was applied with shop kind %d."),PendingRequest,int32(Receipt.Kind));
    return TEXT("交易已成交，请以当前状态为准");
}
void ULDShopPanel::ClearPending()
{
    PendingRequest=0;PendingKind=ELDShopCommandKind::None;PendingName.Reset();PendingPrice=0;
}
void ULDShopPanel::FinishRequest(const FString& Notice)
{
    ClearPending();ShopPanelText(Feedback,Notice);
    // A result that lands after the panel closed still reaches the player, without reopening the panel.
    if(!bOpen){NotifyPlayer(Notice);}
    RefreshShop();
}
void ULDShopPanel::NotifyPlayer(const FString& Notice) const
{
    if(Notify){Notify(Notice);return;}
    UE_LOG(LogTemp,Error,TEXT("LD_SHOP_PANEL_NOTIFY_UNAVAILABLE The HUD did not connect shop notices; undelivered: %s"),*Notice);
}
