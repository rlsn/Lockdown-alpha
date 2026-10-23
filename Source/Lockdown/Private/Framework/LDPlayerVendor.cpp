#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Inventory/LDVendorDirector.h"
#include "UI/LDInventoryPanel.h"
#include "Region/LDRegionDefinition.h"
#include "Core/LDGroundQueries.h"
#include "Engine/World.h"
bool ALDPlayerController::NearGuildVendor() const
{return GetPawn()&&OwnerState.bActive&&FVector::Dist2D(GetPawn()->GetActorLocation(),LDRegion::Site(GetWorld(),1))<=120&&LDGround::ClearSegment(GetWorld(),GetPawn()->GetActorLocation(),LDRegion::Site(GetWorld(),1));}
void ALDPlayerController::OpenVendor()
{
    if(InventoryPending()||!NearGuildVendor()){IntentFeedback=TEXT("Approach the Guild bookshop within 120 cm");return;}
    if(!InventoryUI||!InventoryUI->IsOpen()){ToggleInventoryPanel();}if(!InventoryUI||!InventoryUI->IsOpen()){return;}
    InventoryUI->ShowService(1);
    CloseCommerce();CloseBank();TradeView=FLDTradeView();CloseVendor();ClearControls();SetMenuOpen(true);VendorOpenToken=++NextRequest;bVendorOpening=true;VendorNeedsReview=false;VendorFeedback=TEXT("Opening Guild catalog…");
    ServerOpenVendor(OwnerState.Match,OwnerState.Life,VendorOpenToken);InventoryUI->Refresh();
}
void ALDPlayerController::CloseVendor()
{if(VendorIsOpen()){ServerCloseVendor(VendorOpenToken);}bVendorOpening=false;VendorNeedsReview=false;VendorQuote=FLDVendorQuote();}
void ALDPlayerController::BuyVendorBook(ELDSkill Skill,uint32 Revision,int32 Price)
{
    if(InventoryPending()||!VendorQuote.Session.IsValid()){return;}VendorPendingRequest=++NextRequest;VendorFeedback=TEXT("Purchase pending…");
    ServerVendorBuy(OwnerState.Match,OwnerState.Life,VendorPendingRequest,VendorQuote.Session,Revision,Price,OwnerState.Inventory.Revision,Skill);
}
void ALDPlayerController::ServerOpenVendor_Implementation(uint32 Match,uint32 Life,uint32 Token)
{if(auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();M&&M->Vendor){M->Vendor->Open(this,Match,Life,Token);}}
void ALDPlayerController::ServerCloseVendor_Implementation(uint32 Token)
{if(auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();M&&M->Vendor){M->Vendor->Close(this,Token);}}
void ALDPlayerController::ServerVendorBuy_Implementation(uint32 Match,uint32 Life,uint32 Request,FGuid Session,uint32 Revision,int32 Price,uint32 InventoryRevision,ELDSkill Skill)
{if(auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();M&&M->Vendor){M->Vendor->Queue(this,Match,Life,Request,Session,Revision,Price,InventoryRevision,Skill);}}
void ALDPlayerController::ClientVendorQuote_Implementation(const FLDOwnerState& State,const FLDVendorQuote& Quote)
{
    ClientLifeReceipt_Implementation(State);if(Quote.Token!=VendorOpenToken||(!VendorIsOpen())||!InventoryUI||!InventoryUI->IsOpen()||Quote.Match!=OwnerState.Match||Quote.Life!=OwnerState.Life){return;}
    if(VendorQuote.Session==Quote.Session&&(VendorQuote.CatalogRevision!=Quote.CatalogRevision||(!VendorQuote.Offers.IsEmpty()&&!Quote.Offers.IsEmpty()&&VendorQuote.Offers[0].Price!=Quote.Offers[0].Price))){VendorNeedsReview=true;}
    bVendorOpening=false;VendorQuote=Quote;VendorFeedback=TEXT("Live Guild quote · One book per purchase · Match continues");InventoryUI->Refresh();
}
void ALDPlayerController::ClientVendorClosed_Implementation(uint32 Token,const FString& Reason)
{if(Token!=VendorOpenToken){return;}const bool WasOpen=VendorIsOpen();bVendorOpening=false;VendorNeedsReview=false;VendorQuote=FLDVendorQuote();VendorFeedback=IntentFeedback=Reason;if(WasOpen&&InventoryUI&&InventoryUI->IsOpen()&&!BankIsOpen()&&!CommerceIsOpen()){InventoryUI->Close();}}
void ALDPlayerController::ClientVendorResult_Implementation(const FLDOwnerState& State,uint32 Request,const FLDVendorQuote& Quote,FGuid Item,const FString& Reason)
{
    ClientLifeReceipt_Implementation(State);const bool Pending=Request==VendorPendingRequest;if(Pending){VendorPendingRequest=0;}
    if(Pending&&VendorIsOpen()&&Quote.Token==VendorOpenToken&&Quote.Session==VendorQuote.Session){VendorQuote=Quote;VendorFeedback=Reason;if(Reason.Contains(TEXT("Catalog updated"))){VendorNeedsReview=true;}if(Item.IsValid()&&InventoryUI){InventoryUI->Select(Item);}}
    ClientIntentResult_Implementation(Request,Reason);if(InventoryUI){InventoryUI->Refresh();}
}
