#include "UI/LDInventoryPanel.h"
#include "UI/LDWidgets.h"
#include "Framework/LDPlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/UniformGridPanel.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/EditableTextBox.h"
#include "Components/SizeBox.h"
#include "Components/ScrollBox.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
void ULDInventoryPanel::BuildTradePanel(UVerticalBox* All)
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());TradePanel=WidgetTree->ConstructWidget<UVerticalBox>();All->AddChildToVerticalBox(TradePanel);
    TradeStatus=LDWidgets::Text(WidgetTree,TEXT(""),16);TradePanel->AddChildToVerticalBox(TradeStatus);
    auto* Trays=WidgetTree->ConstructWidget<UHorizontalBox>();TradePanel->AddChildToVerticalBox(Trays);
    for(int32 N=0;N<2;++N){UTextBlock* T=nullptr;auto* Scroll=LDWidgets::OfferTray(WidgetTree,T);if(N){OtherTray=T;}else{OwnTray=T;}auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetHeightOverride(126);Size->SetContent(Scroll);Trays->AddChildToHorizontalBox(Size)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));}
    auto* Actions=WidgetTree->ConstructWidget<UUniformGridPanel>();Actions->SetSlotPadding(FMargin(3));TradePanel->AddChildToVerticalBox(Actions)->SetPadding(FMargin(0,6));
    GoldInput=WidgetTree->ConstructWidget<UEditableTextBox>();GoldInput->SetText(FText::FromString(TEXT("0")));GoldInput->SetHintText(FText::FromString(TEXT("Your offered gold")));GoldInput->SetToolTipText(FText::FromString(TEXT("Whole gold amount; your real wallet remains exposed to theft and death")));auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetWidthOverride(150);Size->SetContent(GoldInput);Actions->AddChildToUniformGrid(Size,0,0);
    auto Style=GoldInput->GetWidgetStyle();Style.BackgroundImageNormal.TintColor=FSlateColor(FLinearColor(.04f,.08f,.11f));Style.BackgroundImageHovered.TintColor=Style.BackgroundImageNormal.TintColor;Style.BackgroundImageFocused.TintColor=Style.BackgroundImageNormal.TintColor;Style.ForegroundColor=FSlateColor(FLinearColor::White);GoldInput->SetWidgetStyle(Style);
    int32 ActionIndex=1;auto Button=[&](const TCHAR* Label,TFunction<void()> Action){auto* B=LDWidgets::Button(WidgetTree,Label,MoveTemp(Action));Actions->AddChildToUniformGrid(B,ActionIndex/2,ActionIndex%2);++ActionIndex;return B;};
    GoldTradeButton=Button(TEXT("Set gold"),[this](){const FString Text=GoldInput->GetText().ToString();int64 Value=0;bool Digits=!Text.IsEmpty()&&Text.Len()<=10;for(auto C:Text){Digits&=C>=TEXT('0')&&C<=TEXT('9');}if(Digits&&LexTryParseString(Value,*Text)){TradeGold(Value);}else{auto* Owner=CastChecked<ALDPlayerController>(GetOwningPlayer());Owner->TradeView.Message=Owner->IntentFeedback=TEXT("Enter a whole nonnegative gold amount");Refresh();}});
    AddTradeButton=Button(TEXT("Add / Remove item"),[this](){TradeSelected();});
    AcceptTradeButton=Button(TEXT("Accept invitation"),[this](){SubmitTrade(LDTrade::Accept);});
    ReadyTradeButton=Button(TEXT("Ready current offer"),[this](){TradeReady();});ConfirmTradeButton=Button(TEXT("Review exchange"),[this](){ShowTradeReview();});
    CancelTradeButton=Button(TEXT("Cancel / Decline"),[this,PC](){if(PC->TradeView.Active()){PC->CancelTrade();}else{Close();}});
}
void ULDInventoryPanel::RefreshTradePanel()
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());const auto& V=PC->TradeView;const bool Show=V.Active()||V.Stage==ELDTradeStage::Committed;TradePanel->SetVisibility(Show?ESlateVisibility::Visible:ESlateVisibility::Collapsed);if(!Show){bTradeReview=false;TradeReview->SetVisibility(ESlateVisibility::Collapsed);return;}
    if(DisplayedTradeRevision!=V.Revision&&!GoldInput->HasKeyboardFocus()&&!GoldInput->HasFocusedDescendants()){GoldInput->SetText(FText::FromString(FString::Printf(TEXT("%lld"),V.OwnGold)));}CastChecked<UTextBlock>(CancelTradeButton->GetContent())->SetText(FText::FromString(V.Stage==ELDTradeStage::Committed?TEXT("Close receipt"):TEXT("Cancel / Decline")));
    DisplayedTradeRevision=V.Revision;DisplayedTradeDigest=V.Digest;DisplayedTradeItems.Reset();for(const auto& I:V.OwnItems){DisplayedTradeItems.Add(I.Handle);}
    const TCHAR* Stage=V.Stage==ELDTradeStage::Invited?TEXT("INVITATION"):V.Stage==ELDTradeStage::Committed?TEXT("COMPLETED"):V.Stage==ELDTradeStage::Review?TEXT("REVIEW EXCHANGE"):TEXT("EDITING OFFER");
    TradeStatus->SetText(FText::FromString(FString(Stage)+FString::Printf(TEXT(" · offer %u · Match continues\nYOU SEND %d items + %lld gold  →  YOU RECEIVE %d items + %lld gold\n%s"),V.Revision,V.OwnItems.Num(),V.OwnGold,V.OtherItems.Num(),V.OtherGold,*V.Message)));
    auto Tray=[Done=V.Stage==ELDTradeStage::Committed](const TArray<FLDTradeItem>& Items,int64 Gold,bool Ready,bool Confirmed,const TCHAR* Label){FString Text=FString::Printf(TEXT("%s · %lld gold · %s\n"),Label,Gold,Done?TEXT("Completed"):Confirmed?TEXT("Confirmed / waiting"):Ready?TEXT("Ready / waiting"):TEXT("Needs Ready"));for(int32 N=0;N<Items.Num();++N){Text+=FString::Printf(TEXT("%d. %s × 1\n"),N+1,*LDInventory::Name(Items[N].Kind,Items[N].Skill));}if(Items.IsEmpty()){Text+=TEXT("No items offered\n");}return Text;};
    OwnTray->SetText(FText::FromString(Tray(V.OwnItems,V.OwnGold,V.bOwnReady,V.bOwnConfirmed,TEXT("YOUR OFFER"))));OtherTray->SetText(FText::FromString(Tray(V.OtherItems,V.OtherGold,V.bOtherReady,V.bOtherConfirmed,TEXT("THEIR OFFER"))));
    const bool Draft=V.Stage==ELDTradeStage::Draft||V.Stage==ELDTradeStage::Review,Free=!PC->InventoryPending();
    GoldInput->SetIsEnabled(Draft&&Free);GoldTradeButton->SetIsEnabled(Draft&&Free);const bool Offered=V.OwnItems.ContainsByPredicate([&](const auto& I){return I.Handle==Selected;});const auto* Item=PC->OwnerState.Inventory.Find(Selected);AddTradeButton->SetIsEnabled(Draft&&Free&&Item&&(Offered||(!Item->bReserved&&V.OwnItems.Num()<6)));
    AcceptTradeButton->SetVisibility(V.Stage==ELDTradeStage::Invited&&V.bReceiver?ESlateVisibility::Visible:ESlateVisibility::Collapsed);AcceptTradeButton->SetIsEnabled(Free);
    ReadyTradeButton->SetVisibility(Draft?ESlateVisibility::Visible:ESlateVisibility::Collapsed);ReadyTradeButton->SetIsEnabled(Free&&V.bOwnValid&&V.bOtherValid&&!V.bOwnReady&&(!V.OwnItems.IsEmpty()||!V.OtherItems.IsEmpty()||V.OwnGold||V.OtherGold));
    ConfirmTradeButton->SetVisibility(V.Stage==ELDTradeStage::Review?ESlateVisibility::Visible:ESlateVisibility::Collapsed);ConfirmTradeButton->SetIsEnabled(Free);
    const bool Reviewing=V.Stage==ELDTradeStage::Review||V.Stage==ELDTradeStage::Committed;
    if(Reviewing&&(ReviewedSession!=V.Session||ReviewedRevision!=V.Revision||ReviewedStage!=uint8(V.Stage))){ReviewedSession=V.Session;ReviewedRevision=V.Revision;bTradeReview=true;bConfirm=false;CancelDrag();}ReviewedStage=uint8(V.Stage);
    if(!Reviewing&&bTradeReview){bTradeReview=false;SetKeyboardFocus();}
    TradeReview->SetVisibility(bTradeReview?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    ReviewSummary->SetText(TradeStatus->GetText());ReviewOwn->SetText(OwnTray->GetText());ReviewOther->SetText(OtherTray->GetText());
    ReviewConfirm->SetVisibility(V.Stage==ELDTradeStage::Review?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    ReviewConfirm->SetIsEnabled(Free&&!V.bOwnConfirmed&&V.bOwnReady&&V.bOtherReady&&V.bOwnValid&&V.bOtherValid);
    ReviewHint->SetText(FText::FromString(V.Stage==ELDTradeStage::Committed?TEXT("Exchange completed. The lists show the final items sent and received."):TEXT("Both players must confirm this offer. Changes require a fresh review.")));
    CastChecked<UTextBlock>(ReviewConfirm->GetContent())->SetText(FText::FromString(PC->TradePending()?TEXT("Pending…"):V.bOwnConfirmed?TEXT("Confirmed · waiting"):TEXT("Confirm exchange")));
    CastChecked<UTextBlock>(ReviewCancel->GetContent())->SetText(FText::FromString(V.Stage==ELDTradeStage::Committed?TEXT("Close receipt"):TEXT("Cancel trade")));
    ReviewBack->SetVisibility(V.Stage==ELDTradeStage::Review?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    if(bTradeReview&&!PanelFocusTargets().ContainsByPredicate([](const UWidget* W){return (W->HasKeyboardFocus()||W->HasFocusedDescendants());})){MovePanelFocus();}

}
void ULDInventoryPanel::SubmitTrade(uint8 Command,FGuid Item,int64 Amount){auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());if(PC->TradePending()||!PC->TradeView.Active()){return;}PC->TradePendingRequest=++PC->NextRequest;PC->ServerTradeCommand(PC->OwnerState.Match,PC->OwnerState.Life,PC->TradePendingRequest,Command,PC->TradeView.Session,DisplayedTradeRevision,DisplayedTradeDigest,Item,Amount);}
void ULDInventoryPanel::TradeSelected(){SubmitTrade(DisplayedTradeItems.Contains(Selected)?LDTrade::Remove:LDTrade::Add,Selected);}
void ULDInventoryPanel::TradeGold(int64 Amount){SubmitTrade(LDTrade::Gold,FGuid(),Amount);}
void ULDInventoryPanel::TradeReady(){SubmitTrade(LDTrade::Ready);}
void ULDInventoryPanel::TradeConfirm(){SubmitTrade(LDTrade::Confirm);}
FString ULDInventoryPanel::TradeText() const{return TradeStatus?TradeStatus->GetText().ToString():FString();}

void ULDInventoryPanel::BuildTradeReview(UCanvasPanel* Canvas)
{
    TradeReview=LDWidgets::Panel(WidgetTree,nullptr,FMargin(24));
    auto* ReviewSlot=Canvas->AddChildToCanvas(TradeReview);ReviewSlot->SetAnchors(FAnchors(.12f,.23f,.88f,.88f));ReviewSlot->SetOffsets(FMargin(0));ReviewSlot->SetZOrder(11);
    auto* All=WidgetTree->ConstructWidget<UVerticalBox>();TradeReview->SetContent(All);
    All->AddChildToVerticalBox(LDWidgets::Text(WidgetTree,TEXT("PEER EXCHANGE · FINAL REVIEW"),24));
    ReviewSummary=LDWidgets::Text(WidgetTree,TEXT(""));All->AddChildToVerticalBox(ReviewSummary)->SetPadding(FMargin(0,8));
    auto* Trays=WidgetTree->ConstructWidget<UHorizontalBox>();All->AddChildToVerticalBox(Trays)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    for(int32 N=0;N<2;++N){UTextBlock* Text=nullptr;auto* Scroll=LDWidgets::OfferTray(WidgetTree,Text);if(N){ReviewOther=Text;}else{ReviewOwn=Text;}Trays->AddChildToHorizontalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));}
    ReviewHint=LDWidgets::Text(WidgetTree,TEXT(""),14);All->AddChildToVerticalBox(ReviewHint)->SetPadding(FMargin(0,8));
    auto* Choices=WidgetTree->ConstructWidget<UHorizontalBox>();All->AddChildToVerticalBox(Choices);
    ReviewConfirm=LDWidgets::Button(WidgetTree,TEXT("Confirm exchange"),[this](){TradeConfirm();});Choices->AddChildToHorizontalBox(ReviewConfirm);
    ReviewBack=LDWidgets::Button(WidgetTree,TEXT("Back to offer"),[this](){bTradeReview=false;Refresh();ConfirmTradeButton->SetKeyboardFocus();});Choices->AddChildToHorizontalBox(ReviewBack)->SetPadding(FMargin(8,0));
    ReviewCancel=LDWidgets::Button(WidgetTree,TEXT("Cancel trade"),[this](){auto* PC=CastChecked<ALDPlayerController>(GetOwningPlayer());if(PC->TradeView.Active()){PC->CancelTrade();}else{Close();}});Choices->AddChildToHorizontalBox(ReviewCancel);
    TradeReview->SetVisibility(ESlateVisibility::Collapsed);
}
void ULDInventoryPanel::ShowTradeReview(){bTradeReview=true;Refresh();MovePanelFocus();}
