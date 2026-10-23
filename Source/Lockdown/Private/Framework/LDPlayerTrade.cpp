#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Inventory/LDTradeDirector.h"
#include "UI/LDInventoryPanel.h"
#include "Engine/World.h"
void ALDPlayerController::OfferTrade()
{
    FLDObservedEntity Target;if(!CanWorldCommand()||InventoryPending()||!ReadInspected(Target)||Target.Kind!=ELDObservedKind::Body){IntentFeedback=TEXT("Select an observed nearby humanoid");return;}
    bTradeDismissed=false;ClearControls();TradePendingRequest=++NextRequest;ServerTradeCommand(OwnerState.Match,OwnerState.Life,TradePendingRequest,LDTrade::Invite,FGuid(),0,TEXT(""),Target.Id,0);
}
void ALDPlayerController::TradeCommand(uint8 Command,FGuid Item,int64 Gold)
{
    if(TradePending()||!TradeView.Active()){return;}TradePendingRequest=++NextRequest;ServerTradeCommand(OwnerState.Match,OwnerState.Life,TradePendingRequest,Command,TradeView.Session,TradeView.Revision,TradeView.Digest,Item,Gold);
}
void ALDPlayerController::CancelTrade()
{
    if(bApplyingTradeView||TradeView.Stage>=ELDTradeStage::Committed||(!TradeView.Active()&&!TradePending())){return;}bTradeDismissed=true;const uint32 Request=++NextRequest;TradePendingRequest=Request;ServerTradeCommand(OwnerState.Match,OwnerState.Life,Request,LDTrade::Cancel,TradeView.Session,TradeView.Revision,TradeView.Digest,FGuid(),0);
}
void ALDPlayerController::ServerTradeCommand_Implementation(uint32 Match,uint32 Life,uint32 Request,uint8 Command,FGuid Session,uint32 Revision,const FString& Digest,FGuid Item,int64 Gold)
{if(auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();M&&M->Trade){M->Trade->Queue(this,Match,Life,Request,Command,Session,Revision,Digest,Item,Gold);}}
void ALDPlayerController::ClientTradeUpdate_Implementation(const FLDOwnerState& State,const FLDTradeView& View,uint32 Request)
{
    TGuardValue<bool> Applying(bApplyingTradeView,true);ClientLifeReceipt_Implementation(State);if(Request==TradePendingRequest){TradePendingRequest=0;}if(View.Serial<=TradeSerial){return;}TradeSerial=View.Serial;
    if(View.Active()&&(View.Match!=OwnerState.Match||View.Life!=OwnerState.Life)){return;}
    if(TradeView.Active()&&View.Session!=TradeView.Session){if(!View.Message.IsEmpty()){IntentFeedback=View.Message;}return;}
    if(View.Active()&&bTradeDismissed){TradeView=View;return;}
    if(View.Stage==ELDTradeStage::Committed){if(SeenTradeCompletions.Contains(View.Session)&&TradeView.Session!=View.Session){IntentFeedback=View.Message;return;}SeenTradeCompletions.AddUnique(View.Session);while(SeenTradeCompletions.Num()>8){SeenTradeCompletions.RemoveAt(0);}}
    if(View.Active()||View.Stage==ELDTradeStage::Committed)
    {
        if(!InventoryUI||!InventoryUI->IsOpen()){ToggleInventoryPanel();}CloseVendor();CloseBank();CloseCommerce();
    }
    TradeView=View;if(!View.Active()){bTradeDismissed=false;}if(!View.Message.IsEmpty()){IntentFeedback=View.Message;}
    if((View.Stage==ELDTradeStage::Cancelled||View.Stage==ELDTradeStage::Expired)&&InventoryUI&&InventoryUI->IsOpen()&&!VendorIsOpen()&&!BankIsOpen()&&!CommerceIsOpen()){InventoryUI->Close();}
    if(InventoryUI){InventoryUI->Refresh();}
}
