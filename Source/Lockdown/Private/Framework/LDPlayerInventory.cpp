#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
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
