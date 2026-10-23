#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Economy/LDCommerceDirector.h"
#include "UI/LDInventoryPanel.h"
#include "Core/LDGroundQueries.h"
#include "Engine/World.h"
bool ALDPlayerController::NearCommerce(uint8 Service) const
{return Service<4&&OwnerState.bActive&&GetPawn()&&FVector::Dist2D(GetPawn()->GetActorLocation(),ALDCommerceDirector::Position(GetWorld(),Service))<=120&&LDGround::ClearSegment(GetWorld(),GetPawn()->GetActorLocation(),ALDCommerceDirector::Position(GetWorld(),Service));}
void ALDPlayerController::OpenCommerce(uint8 Service)
{
    if(InventoryPending()||!NearCommerce(Service)){IntentFeedback=TEXT("Approach the selected official service within 120 cm");return;}
    if(!InventoryUI||!InventoryUI->IsOpen()){ToggleInventoryPanel();}if(!InventoryUI||!InventoryUI->IsOpen()){return;}
    CloseCommerce();bCommerceOpening=true;CloseVendor();CloseBank();CancelTrade();ClearControls();SetMenuOpen(true);CommerceToken=++NextRequest;bCommerceOpening=true;CommerceNeedsReview=false;CommerceFeedback=TEXT("Opening local service…");ServerCommerceOpen(OwnerState.Match,OwnerState.Life,CommerceToken,Service);InventoryUI->Refresh();
}
void ALDPlayerController::CloseCommerce(){if(CommerceIsOpen()){ServerCommerceClose(CommerceToken);}bCommerceOpening=false;CommerceNeedsReview=false;CommerceView=FLDCommerceView();}
void ALDPlayerController::SendCommerce(uint8 Command,ELDItemKind Kind,FGuid Item,uint32 Revision,int32 Price)
{
    if(InventoryPending()||!CommerceView.Session.IsValid()){return;}
    if(CommerceNeedsReview){CommerceNeedsReview=false;CommerceFeedback=TEXT("Updated terms reviewed · Click Buy/Sell again to confirm");if(InventoryUI){InventoryUI->Refresh();}return;}
    const auto* I=OwnerState.Inventory.Find(Item);CommercePending=++NextRequest;CommerceFeedback=TEXT("Exchange pending…");ServerCommerceCommit(OwnerState.Match,OwnerState.Life,CommercePending,CommerceView.Session,Revision,Command,Kind,Item,I?I->Revision:0,OwnerState.Inventory.Revision,Price);
}
void ALDPlayerController::ServerCommerceOpen_Implementation(uint32 Match,uint32 Life,uint32 Token,uint8 Service)
{if(auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();M&&M->Commerce){M->Commerce->Open(this,Match,Life,Token,Service);}else{FLDCommerceView V;V.Token=Token;ClientCommerceView(OwnerState,V,0,TEXT("Commerce unavailable"));}}
void ALDPlayerController::ServerCommerceClose_Implementation(uint32 Token){if(auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();M&&M->Commerce){M->Commerce->Close(this,Token);}}
void ALDPlayerController::ServerCommerceCommit_Implementation(uint32 Match,uint32 Life,uint32 Request,FGuid Session,uint32 Revision,uint8 Command,ELDItemKind Kind,FGuid Item,uint32 ItemRevision,uint32 InventoryRevision,int32 Price)
{if(auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();M&&M->Commerce){M->Commerce->Queue(this,Match,Life,Request,Session,Revision,Command,Kind,Item,ItemRevision,InventoryRevision,Price);}else{ClientCommerceView(OwnerState,FLDCommerceView(),Request,TEXT("Commerce unavailable"));}}
void ALDPlayerController::ClientCommerceView_Implementation(const FLDOwnerState& State,const FLDCommerceView& View,uint32 Request,const FString& Reason)
{
    ClientLifeReceipt_Implementation(State);const bool Pending=Request&&Request==CommercePending;if(Pending){CommercePending=0;}
    if(View.Token==CommerceToken&&CommerceIsOpen()&&InventoryUI&&InventoryUI->IsOpen())
    {
        if(CommerceView.Session==View.Session&&CommerceView.Revision!=View.Revision&&!Pending){CommerceNeedsReview=true;}
        bCommerceOpening=false;CommerceView=View;CommerceFeedback=Reason;
        if(Reason.Contains(TEXT("Review"))){CommerceNeedsReview=true;}
        if(View.Match!=OwnerState.Match||View.Life!=OwnerState.Life){CommerceView.Session.Invalidate();}
    }
    ClientIntentResult_Implementation(Request,Reason);if(InventoryUI){InventoryUI->Refresh();}
}
