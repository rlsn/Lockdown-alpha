#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "Vision/LDViewTypes.h"
#include "Combat/LDCombatTypes.h"
#include "Framework/LDCombatReceiptLog.h"
#include "Development/LDDevTestProtocol.h"
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
    virtual void UpdateCameraManager(float DeltaSeconds) override;
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
    // The Host's final admission check kicks a joiner that lost the race to the match start or the last slot.
    virtual void ClientWasKicked_Implementation(const FText& KickReason) override;
    void ClearControls(bool LifeChanged=false);
    bool CanWorldCommand() const;
    bool NavigationBindingsActive() const { return bNavigationContextActive; }
    UPROPERTY() TObjectPtr<class ULDControlSettings> Controls;
    UPROPERTY() TObjectPtr<class ULDControlsPanel> ControlsUI;
    UPROPERTY() TObjectPtr<class ULDHUD> HUDUI;
    UPROPERTY() TObjectPtr<ULDUIRoot> RootUI;
    UPROPERTY(VisibleAnywhere) TObjectPtr<class ULDNavigationComponent> Navigation;
    UPROPERTY(ReplicatedUsing=OnRep_OwnerState) FLDOwnerState OwnerState;
    UFUNCTION() void OnRep_OwnerState();
    UFUNCTION(Client,Reliable) void ClientLifeReceipt(const FLDOwnerState& State);
    UFUNCTION(Client,Reliable) void ClientIntentResult(uint32 Request,const FString& Reason);
    // ---- Growth/Shop (growth module; implementation in LDPlayerGrowth.cpp) ----
    // The caller fills the business fields; Submit fills Generation, Life (and ExpectedLifePhase for a choice) from
    // the accepted OwnerState at call time, assigns the RequestId, records it in flight before the RPC and returns
    // it, or returns 0 with a local reason without sending anything. A Listen Host answers inside the call, so a
    // caller that must key its pending entry first reads PeekGrowthRequestId(Family) before submitting.
    uint64 SubmitChoice(const FLDChoiceCommand& Command, ELDGrowthReason& OutLocalReason);
    uint64 SubmitShopCommand(const FLDShopCommand& Command, ELDGrowthReason& OutLocalReason);
    uint64 PeekGrowthRequestId(ELDGrowthFamily Family) const;
    // Open intents have no request sequence: true when sent; the terminal result arrives as OnShopOpenResult.
    bool SubmitShopOpen(const FLDShopOpenCommand& Command, ELDGrowthReason& OutLocalReason);
    // Owning client: requests ShopId's session for the given (current) life with the registered world revision; false with
    // the local reason when nothing was sent. The arrival of an ApproachShop order and shop pre-orders both open here.
    bool RequestShopSession(FName ShopId, uint32 Life, ELDGrowthReason& OutLocalReason);
    void QueryGrowthReceipt(const FLDReceiptQuery& Query);
    // Own accepted projection only; null while the view is invalid. ReadOwnShop is non-null only for an Opened session.
    const FLDProgressionView* ReadOwnProgression() const;
    const FLDEquipmentView* ReadOwnEquipment() const;
    const FLDShopView* ReadOwnShop() const;
    FLDOnGrowthSubmit OnGrowthSubmit;
    FLDOnChoiceReceipt OnChoiceReceipt;
    FLDOnShopReceipt OnShopReceipt;
    FLDOnShopOpenResult OnShopOpenResult;
    UFUNCTION(Server,Reliable) void ServerGrowthChoice(const FLDChoiceCommand& Command);
    UFUNCTION(Server,Reliable) void ServerShopOpen(const FLDShopOpenCommand& Command);
    UFUNCTION(Server,Reliable) void ServerShopCommand(const FLDShopCommand& Command);
    UFUNCTION(Server,Reliable) void ServerQueryGrowthReceipt(const FLDReceiptQuery& Query);
    UFUNCTION(Client,Reliable) void ClientGrowthSubmit(const FLDSubmitResult& Result);
    UFUNCTION(Client,Reliable) void ClientChoiceReceipt(const FLDChoiceReceiptQueryResult& Result);
    UFUNCTION(Client,Reliable) void ClientShopReceipt(const FLDShopReceiptQueryResult& Result);
    UFUNCTION(Client,Reliable) void ClientShopOpenResult(const FLDShopOpenResult& Result);
    // ---- end Growth/Shop ----
    // ---- DEV test commands (#78; LDDevTestProtocol.h, implementation and console commands in LDPlayerDevTest.cpp) ----
    // Fills Generation, Life and the RequestId from the accepted OwnerState like SubmitChoice; one command in flight.
    // Returns the RequestId, or 0 with a local reason without sending. The authority admits the command only in a DEV
    // test session (automation-test build, Host started with -LDDevGrowthFixture); every result arrives as OnDevTestResult.
    uint64 SubmitDevTest(ELDDevTestOp Op, int64 Amount, ELDGrowthReason& OutLocalReason, FName Definition = NAME_None,
        uint8 Slot = 0, uint8 Stat = 0);
    FLDOnDevTestResult OnDevTestResult;
    UFUNCTION(Server,Reliable) void ServerDevTestCommand(const FLDDevTestCommand& Command);
    UFUNCTION(Client,Reliable) void ClientDevTestResult(const FLDDevTestResult& Result);
    // ---- end DEV test commands ----
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
    // ---- Combat and orders (combat-orders module, Framework/LDPlayerOrders.cpp and LDPlayerInput.cpp) ----
    // CanWorldCommand (orders, Q/W/1/2, A, S, world and minimap clicks) is IsWorldLifeValid && !IsWorldInputSuppressed.
    // Life half: the current PublicMatch generation is running (Development/CaptureOpen) and this owner's accepted life
    // is Alive with its body. Only losing it ends accepted orders.
    bool IsWorldLifeValid() const;
    // Input half: the menu or Settings, a child confirmation or text edit, or keys still held since a menu, focus or
    // mapping change. It blocks new world commands only; accepted orders keep moving and sending heartbeats (UI 4, 7).
    bool IsWorldInputSuppressed() const;
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
    // Confirms the armed Q/W/1/2 entry on an explicit aim (an observed unit handle or a planar direction) instead of
    // the cursor's; ConfirmArmedCombat resolves the cursor aim and ends here. False when no entry was armed.
    bool ConfirmArmedEntry(const FLDCombatTarget& Aim);
    // S: stop orders and cancel interruptible Direct actions issued so far.
    void StopCombatAndOrders();
    // One player-facing notice (a failure or a removed protection) for the HUD's single toast line; every post shows,
    // even when it repeats the previous text. Order progress and arrival are never posted (UI review P1).
    void PostIntentNotice(const FString& Text);
    // A child confirmation or a text edit owns input (UI 4): no new world command, armed preview or camera input
    // starts; accepted orders keep running.
    bool IsWorldInputCaptured() const;
    // Dead in the running match and waiting for the respawn: only the static-map camera stays usable.
    bool IsAwaitingRespawn() const;
    // Camera input (pan, zoom, recenter, follow): unsuppressed input over a valid life or the dead owner's static camera.
    bool CanCameraCommand() const;
    // Current shop interaction binding (ApproachShop en route or arrived); see ULDNavigationComponent.
    bool ReadShopInteraction(FName& OutShopId, uint32& OutLife) const;
    UFUNCTION(Server,Reliable) void ServerSubmitCombatAction(const FLDCombatActionRequest& Request);
    UFUNCTION(Server,Reliable) void ServerCancelCombatAction(const FLDCombatCancelRequest& Request);
    UFUNCTION(Client,Reliable) void ClientCombatReceipt(const FLDCombatActionReceipt& Receipt);
    UPROPERTY(ReplicatedUsing=OnRep_CombatView) FLDCombatOwnerView CombatView;
    UFUNCTION() void OnRep_CombatView();
    // Every receipt of the current match in arrival order; presenters keep their own cursor (LDCombatReceiptLog.h).
    FLDCombatReceiptLog CombatReceipts;
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
    // ---- Growth/Shop private request cursors (one in-flight request per family) ----
    struct FGrowthRequestCursor
    {
        uint32 Generation=0;
        uint64 Next=1;
        uint64 InFlight=0;
    };
    FGrowthRequestCursor ChoiceCursor;
    FGrowthRequestCursor ShopCursor;
    FGrowthRequestCursor DevTestCursor;
    uint64 BeginGrowthRequest(FGrowthRequestCursor& Cursor, ELDGrowthReason& OutLocalReason);
    void SettleGrowthRequest(FGrowthRequestCursor& Cursor, uint32 Generation, uint64 RequestId, bool bReserved, bool bFinal);
    // Owning client: Navigation's arrival of the current ApproachShop binding requests that shop's session once.
    void BindGrowthClient();
    void HandleShopArrived(FName ShopId, uint32 Life);
    // ---- end Growth/Shop ----
    FLDOwnerState LatestOwner;
    FVector LastBodyPosition=FVector::ZeroVector;
    UPROPERTY() TObjectPtr<class ACameraActor> WaitingCamera;
    // ---- Combat and orders (combat-orders module, Framework/LDPlayerOrders.cpp and LDPlayerInput.cpp) ----
    // Ends every local gesture without touching an accepted order: the armed preview and the held recenter are
    // cleared and every key must be released before the next world command.
    void ReleaseLocalInput();
    void SendCombatAction(ELDCombatEntry Entry, const FLDCombatTarget& Target);
    // Pre-input (LDCombatPreInput): one Direct press held during the own ability windup, sent when it ends.
    struct FQueuedCombat
    {
        ELDCombatEntry Entry=ELDCombatEntry::None;
        FLDCombatTarget Aim;
        uint32 Match=0,Life=0;
        double QueuedAt=0;
        uint64 SourceRevision=0;
    };
#if WITH_DEV_AUTOMATION_TESTS
    friend class FLDControlFeelSourceBufferTest;
#endif
    FQueuedCombat QueuedCombat;
    void FlushQueuedCombat();
    void SendCombatNow(ELDCombatEntry Entry, const FLDCombatTarget& Target);
    ELDCombatEntry ArmedEntry=ELDCombatEntry::None;
    bool bAttackMoveArmed=false;
    uint64 NextDirectSequence=0;
    FLDCombatOwnerView LatestCombatView;
};
