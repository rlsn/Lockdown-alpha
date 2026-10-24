#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Inventory/LDInventoryDirector.h"
#include "Development/LDDevelopmentDirector.h"
#include "UI/LDInventoryPanel.h"
#include "UI/LDControlsPanel.h"
#include "UI/LDDevelopmentPanel.h"
#include "UI/LDUIRoot.h"
#include "Navigation/LDNavigationComponent.h"
#include "Engine/World.h"

void ALDPlayerController::ToggleInventoryPanel()
{
    const auto* G=GetWorld()->GetGameState<ALDGameState>();if(!Navigation->Enabled()||!G||!G->bMatchStarted||G->EndReason){return;}
    if(InventoryUI&&InventoryUI->IsOpen()){InventoryUI->Close();return;}
    if(ControlsUI&&ControlsUI->IsOpen()){ControlsUI->Close();}if(DevelopmentUI&&DevelopmentUI->IsOpen()){DevelopmentUI->Close();}if(RootUI){RootUI->DismissMenu();}
    if(!InventoryUI){InventoryUI=CreateWidget<ULDInventoryPanel>(this);InventoryUI->AddToViewport(24);}InventoryUI->Open();
}
void ALDPlayerController::InventoryCommand(uint8 Command,FGuid Item,uint8 Slot,FVector Point)
{
    if(InventoryPending()){return;}const auto* Entry=OwnerState.Inventory.Find(Item);InventoryPendingRequest=++NextRequest;
    ServerInventoryRequest(OwnerState.Match,OwnerState.Life,InventoryPendingRequest,OwnerState.Inventory.Revision,Command,Item,Entry?Entry->Revision:0,Slot,Point);
}
void ALDPlayerController::SetQuickItemHeld(uint8 Slot,bool Held,uint8 Gesture)
{
    if(!Held){if(HeldQuickSlot==Slot&&HeldQuickGesture==Gesture){StopDevelopmentHold();HeldQuickSlot=-1;}return;}
    if(!CanWorldCommand()||Slot>=(OwnerState.Build.RulesVersion==7?3:4)||InventoryPending()){return;}const auto* Item=OwnerState.Inventory.Find(OwnerState.Inventory.Quick[Slot]);
    if(!Item){IntentFeedback=TEXT("Quick item slot is empty");return;}if(Item->bReserved){IntentFeedback=TEXT("Item reserved");return;}
    if(Item->Kind==ELDItemKind::NullBell){InventoryCommand(LDInventory::Use,Item->Id,Slot);return;}
    if(Item->Kind==ELDItemKind::WardCharge){ArmWard(Item->Id);return;}
    if(OwnerState.Build.RulesVersion==7||Item->Kind!=ELDItemKind::Book){return;}const FGuid Id=Item->Id;StartDevelopmentHold(LDDevelopmentTransaction::Read,Id,FLDBuildPlan());HeldQuickSlot=Slot;HeldQuickGesture=Gesture;
}
void ALDPlayerController::ArmWard(FGuid Item)
{
    const auto* Entry=OwnerState.Inventory.Find(Item);if(!Entry||Entry->Kind!=ELDItemKind::WardCharge||Entry->bReserved){IntentFeedback=TEXT("Ward charge unavailable");return;}
    const auto Revision=Entry->Revision;if(InventoryUI&&InventoryUI->IsOpen()){InventoryUI->Close();}CancelArmed();ArmedWard=Item;ArmedItemRevision=Revision;IntentFeedback=TEXT("Ward: confirm clear ground within 96 cm. Right-click or Escape cancels.");
}
void ALDPlayerController::BeginPickup(FGuid Observed)
{if(!CanWorldCommand()||InventoryPending()){return;}PendingPickupToken=++NextRequest;ServerPickupQuote(OwnerState.Match,OwnerState.Life,PendingPickupToken,Observed);}
void ALDPlayerController::ServerPickupQuote_Implementation(uint32 Match,uint32 Life,uint32 Token,FGuid Observed)
{auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(M&&M->Inventory){M->Inventory->QuotePickup(this,Match,Life,Token,Observed);}}
void ALDPlayerController::ClientPickupQuote_Implementation(uint32 Match,uint32 Life,uint32 Token,FGuid Observed,uint32 InventoryRevision,uint32 ItemRevision)
{
    if(!Token||Token!=PendingPickupToken||OwnerState.Match!=Match||OwnerState.Life!=Life||!CanWorldCommand()){return;}PendingPickupToken=0;InventoryPendingRequest=Token;
    ServerInventoryRequest(Match,Life,Token,InventoryRevision,LDInventory::Pickup,Observed,ItemRevision,0,FVector::ZeroVector);
}
void ALDPlayerController::ServerInventoryRequest_Implementation(uint32 Match,uint32 Life,uint32 Request,uint32 InventoryRevision,uint8 Command,FGuid Item,uint32 ItemRevision,uint8 Slot,FVector_NetQuantize10 Point)
{auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(M&&M->Inventory){M->Inventory->Queue(this,Match,Life,Request,InventoryRevision,Command,Item,ItemRevision,Slot,Point);}else{ClientIntentResult(Request,TEXT("Inventory unavailable"));}}
void ALDPlayerController::ClientInventoryResult_Implementation(const FLDOwnerState& State,uint32 Request,const FString& Result)
{ClientLifeReceipt_Implementation(State);ClientIntentResult_Implementation(Request,Result);}
