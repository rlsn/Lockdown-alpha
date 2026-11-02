#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Inventory/LDInventoryDirector.h"
#include "Inventory/LDInventoryTypes.h"
#include "UI/LDInventoryPanel.h"
#include "UI/LDControlsPanel.h"
#include "UI/LDUIRoot.h"
#include "Navigation/LDNavigationComponent.h"
#include "Engine/World.h"

void ALDPlayerController::ToggleInventoryPanel()
{
    const auto* Public=GetWorld()->GetGameState<ALDGameState>();
    if (!Navigation->Enabled() || !Public || Public->DemoLobby.Generation!=OwnerState.Match
        || (Public->DemoLobby.Phase!=ELDMatchPhase::Development && Public->DemoLobby.Phase!=ELDMatchPhase::CaptureOpen)) { return; }
    if (InventoryUI && InventoryUI->IsOpen()) { InventoryUI->Close(); return; }
    if (ControlsUI && ControlsUI->IsOpen()) { ControlsUI->ForceClose(); }
    if (RootUI) { RootUI->DismissMenu(); }
    if (!InventoryUI)
    {
        InventoryUI=CreateWidget<ULDInventoryPanel>(this);
        if (!InventoryUI)
        {
            UE_LOG(LogTemp,Error,TEXT("LD_DEMO_INVENTORY_UI_CREATION_FAILED"));
            return;
        }
        InventoryUI->AddToViewport(24);
    }
    InventoryUI->Open();
}

void ALDPlayerController::InventoryCommand(uint8 Command,FGuid Item,uint8 Slot,FVector Point)
{
    if (InventoryPending()) { IntentFeedback=TEXT("Wait for the pending inventory request."); return; }
    if (Command!=LDInventory::Move && Command!=LDInventory::Sort && Command!=LDInventory::Drop)
    { IntentFeedback=TEXT("This inventory operation is not supported."); return; }
    if (NextRequest==MAX_uint32)
    { IntentFeedback=TEXT("Inventory request sequence exhausted. Leave and create a new room."); return; }
    const auto* Entry=OwnerState.Inventory.Find(Item);
    InventoryPendingRequest=++NextRequest;
    ServerInventoryRequest(OwnerState.Match,OwnerState.Life,InventoryPendingRequest,OwnerState.Inventory.Revision,
        Command,Item,Entry?Entry->Revision:0,Slot,Point);
}

void ALDPlayerController::ServerInventoryRequest_Implementation(uint32 Match,uint32 Life,uint32 Request,
    uint32 InventoryRevision,uint8 Command,FGuid Item,uint32 ItemRevision,uint8 Slot,FVector_NetQuantize10 Point)
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    if (Mode && Mode->Inventory)
    { Mode->Inventory->Queue(this,Match,Life,Request,InventoryRevision,Command,Item,ItemRevision,Slot,Point); }
    else { ClientIntentResult(Request,TEXT("Inventory is not available in this build.")); }
}

void ALDPlayerController::ClientInventoryResult_Implementation(const FLDOwnerState& State,uint32 Request,const FString& Result)
{
    const bool Current=Request && Request==InventoryPendingRequest && State.Match==OwnerState.Match && State.Life==OwnerState.Life;
    ClientLifeReceipt_Implementation(State);
    if (Current) { ClientIntentResult_Implementation(Request,Result); }
}
