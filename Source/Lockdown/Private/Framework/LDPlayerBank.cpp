#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Economy/LDBankDirector.h"
#include "UI/LDInventoryPanel.h"
#include "Engine/World.h"
void ALDPlayerController::OpenBank()
{
    if(InventoryPending()||!NearGuildVendor()){IntentFeedback=TEXT("Approach City Treasury at the Guild within 120 cm");return;}
    if(!InventoryUI||!InventoryUI->IsOpen()){ToggleInventoryPanel();}if(!InventoryUI||!InventoryUI->IsOpen()){return;}
    CloseBank();bBankOpening=true;CloseCommerce();CloseVendor();CancelTrade();ClearControls();SetMenuOpen(true);BankToken=++NextRequest;bBankOpening=true;BankFeedback=TEXT("Opening City Treasury…");
    ServerBankOpen(OwnerState.Match,OwnerState.Life,BankToken);InventoryUI->Refresh();
}
void ALDPlayerController::CloseBank()
{if(BankIsOpen()){ServerBankClose(BankToken);}bBankOpening=false;BankView=FLDBankView();}
void ALDPlayerController::ConfirmBank(int64 Amount,bool Deposit,uint32 WR,uint32 BR)
{
    if(InventoryPending()||!BankView.Session.IsValid()){return;}BankPending=++NextRequest;BankFeedback=TEXT("Bank transaction pending…");
    ServerBankCommit(OwnerState.Match,OwnerState.Life,BankPending,BankView.Session,Amount,Deposit,WR,BR);
}
void ALDPlayerController::ServerBankOpen_Implementation(uint32 Match,uint32 Life,uint32 Token)
{if(auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();M&&M->Bank){M->Bank->Open(this,Match,Life,Token);}else{FLDBankView V;V.Token=Token;ClientBankView(OwnerState,V,0,TEXT("Treasury unavailable until match start"));}}
void ALDPlayerController::ServerBankClose_Implementation(uint32 Token)
{if(auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();M&&M->Bank){M->Bank->Close(this,Token);}}
void ALDPlayerController::ServerBankCommit_Implementation(uint32 Match,uint32 Life,uint32 Request,FGuid Session,int64 Amount,bool Deposit,uint32 WR,uint32 BR)
{if(auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();M&&M->Bank){M->Bank->Queue(this,Match,Life,Request,Session,Amount,Deposit,WR,BR);}else{ClientBankView(OwnerState,FLDBankView(),Request,TEXT("Treasury unavailable"));}}
void ALDPlayerController::ClientBankView_Implementation(const FLDOwnerState& State,const FLDBankView& View,uint32 Request,const FString& Reason)
{
    ClientLifeReceipt_Implementation(State);if(Request&&Request==BankPending){BankPending=0;}
    if(View.Token==BankToken&&BankIsOpen()&&InventoryUI&&InventoryUI->IsOpen())
    {bBankOpening=false;BankView=View;BankFeedback=Reason;if(View.Match!=OwnerState.Match||View.Life!=OwnerState.Life){BankView.Session.Invalidate();}}
    ClientIntentResult_Implementation(Request,Reason);if(InventoryUI){InventoryUI->Refresh();}
}
