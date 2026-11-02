#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "Vision/LDViewTypes.h"
#include "Combat/LDCombatTypes.h"
#include "LDPlayerController.generated.h"

struct FInputActionValue;
class ULDUIRoot;

UCLASS()
class LOCKDOWN_API ALDPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    ALDPlayerController();
    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;
    virtual void PlayerTick(float Delta) override;
    virtual void FlushPressedKeys() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    void SetMenuOpen(bool bOpen);
    void SetDemoReady(bool Ready);
    void SetDemoAICount(uint8 Count);
    void StartDemoMatch();
    // Host only: end the running match without a winner / take the room back to a new Lobby.
    void EndDemoMatch();
    void ReturnDemoToLobby();
    UFUNCTION(Server,Reliable) void ServerDemoSessionCommand(const FLDSessionRequest& Request);
    UFUNCTION(Client,Reliable) void ClientDemoSessionReceipt(const FLDSessionReceipt& Receipt);
    UFUNCTION(Client,Reliable) void ClientDemoRoomClosed(uint32 Generation,const FString& Reason);
    void ClearControls(bool LifeChanged=false);
    bool CanWorldCommand() const;
    bool NavigationBindingsActive() const { return bNavigationContextActive; }
    UPROPERTY() TObjectPtr<class ULDControlSettings> Controls;
    UPROPERTY() TObjectPtr<class ULDControlsPanel> ControlsUI;
    UPROPERTY() TObjectPtr<class ULDHUD> HUDUI;
    UPROPERTY() TObjectPtr<class ULDInventoryPanel> InventoryUI;
    UPROPERTY() TObjectPtr<ULDUIRoot> RootUI;
    UPROPERTY(VisibleAnywhere) TObjectPtr<class ULDNavigationComponent> Navigation;
    UPROPERTY(ReplicatedUsing=OnRep_OwnerState) FLDOwnerState OwnerState;
    UFUNCTION() void OnRep_OwnerState();
    UFUNCTION(Client,Reliable) void ClientLifeReceipt(const FLDOwnerState& State);
    UFUNCTION(Client,Reliable) void ClientIntentResult(uint32 Request,const FString& Reason);
    void ToggleInventoryPanel();
    void InventoryCommand(uint8 Command,FGuid Item=FGuid(),uint8 Slot=0,FVector Point=FVector::ZeroVector);
    bool InventoryPending() const { return InventoryPendingRequest!=0; }
    UFUNCTION(Server,Reliable) void ServerInventoryRequest(uint32 Match,uint32 Life,uint32 Request,
        uint32 InventoryRevision,uint8 Command,FGuid Item,uint32 ItemRevision,uint8 Slot,FVector_NetQuantize10 Point);
    UFUNCTION(Client,Reliable) void ClientInventoryResult(const FLDOwnerState& State,uint32 Request,const FString& Result);
    void RebuildV05Mappings();
    void RecordAppliedCameraZoom();
    void ToggleControlsPanel();
    void V05Back();
    FString BindingLabel(FName Action) const;
    FString ControlSummary() const;
    FKey BindingKey(FName Action) const;
    FVector2D CameraPanAxis() const;
    FString HeldInputLabel() const;
    void Inspect(FGuid Handle,uint32 ObservationEpoch);
    bool ObservedUnderPointer(FGuid& OutHandle,uint32& OutEpoch) const;
    bool ReadInspected(FLDObservedUnitView& Out) const;
    bool IsFrontend() const;
    bool ReadDemoAuthorityTimeMs(int64& OutNowMs) const;
    class ALDWorldViewRelay* LocalView() const;
    FString IntentFeedback;
    uint32 FeedbackSerial() const { return IntentFeedbackSerial; }
    // ---- Combat and orders (combat-orders module, Framework/LDPlayerOrders.cpp) ----
    // Hotkeys and HUD icons share this entry. Self sends now; Unit/Direction arm and wait for a
    // left-click confirmation; right click or Back cancels the armed entry without a world command.
    void ActivateCombatEntry(ELDCombatEntry Entry);
    bool ReadArmedCombatEntry(ELDCombatEntry& Out) const;
    bool IsAttackMoveArmed() const { return bAttackMoveArmed; }
    void ArmAttackMove();
    // Clears any armed entry or attack-move without sending anything; true if something was armed.
    bool CancelArmedCombat();
    // Left-click confirmation of the armed entry or attack-move; false when nothing was armed.
    bool ConfirmArmedCombat();
    // S: stop orders and cancel interruptible Direct actions issued so far.
    void StopCombatAndOrders();
    // Current shop interaction binding (ApproachShop en route or arrived); see ULDNavigationComponent.
    bool ReadShopInteraction(FName& OutShopId, uint32& OutLife) const;
    UFUNCTION(Server,Reliable) void ServerSubmitCombatAction(const FLDCombatActionRequest& Request);
    UFUNCTION(Server,Reliable) void ServerCancelCombatAction(const FLDCombatCancelRequest& Request);
    UFUNCTION(Client,Reliable) void ClientCombatReceipt(const FLDCombatActionReceipt& Receipt);
    UPROPERTY(ReplicatedUsing=OnRep_CombatView) FLDCombatOwnerView CombatView;
    UFUNCTION() void OnRep_CombatView();
    FLDCombatActionReceipt LatestCombatReceipt;
    uint32 CombatReceiptSerial=0;
private:
    void SendDemoSessionCommand(ELDSessionCommand Command,bool Ready=false,uint8 AICount=0,FGuid ExpectedResult=FGuid());
    uint32 DemoRequestGeneration=0;
    uint64 DemoRequestSequence=0;
    uint64 DemoPendingSequence=0;
    void InitializeLocalUI();
    void UpdateV05Inputs(float Delta);
    void V05Pressed(FName Action);
    void V05Released(FName Action);
    void V05Zoom(const FInputActionValue& Value);
    TArray<TPair<uint32,FString>> SeenIntentResults;
    uint32 IntentFeedbackSerial=0;
    bool bControlsSuppressed=false;
    UPROPERTY() TObjectPtr<class UInputMappingContext> NavigationInputContext;
    bool bNavigationContextActive=false;
    bool bFreshInputRequired=false;
    TWeakObjectPtr<APawn> PreferencesPawn;
    TArray<uint32> V05BindingHandles;
    FGuid Inspected;
    uint32 InspectedGeneration=0,InspectedEpoch=0;
    uint32 NextRequest=0,InventoryPendingRequest=0;
    FLDOwnerState LatestOwner;
    FVector LastBodyPosition=FVector::ZeroVector;
    UPROPERTY() TObjectPtr<class ACameraActor> WaitingCamera;
    // ---- Combat and orders (combat-orders module, Framework/LDPlayerOrders.cpp) ----
    void SendCombatAction(ELDCombatEntry Entry, const FLDCombatTarget& Target);
    ELDCombatEntry ArmedEntry=ELDCombatEntry::None;
    bool bAttackMoveArmed=false;
    uint64 NextDirectSequence=0;
    FLDCombatOwnerView LatestCombatView;
};
