#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "Vision/LDViewTypes.h"
#include "Combat/LDCombatTypes.h"
#include "Framework/LDCombatReceiptLog.h"
#include "Development/LDDevTestProtocol.h"
#include "Templates/PimplPtr.h"
#include "LDPlayerController.generated.h"

struct FInputActionValue;
class ACameraActor;
class ULDUIRoot;
class FLDPlaytestTrace;

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
#if WITH_DEV_AUTOMATION_TESTS
    void SetDevArtCaptureCamera(ACameraActor* Camera);
#endif
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
    // #198 §10 owner-only end-of-match build blueprint channel and its read-only viewer (LDBuildBlueprintComponent.h).
    UPROPERTY(VisibleAnywhere) TObjectPtr<class ULDBuildBlueprintComponent> BuildBlueprints;
    UPROPERTY(ReplicatedUsing=OnRep_OwnerState) FLDOwnerState OwnerState;
    UFUNCTION() void OnRep_OwnerState();
    UFUNCTION(Client,Reliable) void ClientLifeReceipt(const FLDOwnerState& State);
    UFUNCTION(Client,Reliable) void ClientIntentResult(uint32 Request,const FString& Reason);
    // ---- Growth (growth module; implementation in LDPlayerGrowth.cpp) ----
    // #198 build commands (active choice, learn blueprint, allocate points). The caller fills Kind and its business
    // fields (ExpectedBuildRevision and CatalogDigest from ReadOwnProgression); Submit fills Generation, Life and
    // ExpectedLifePhase from the accepted OwnerState at call time, assigns the RequestId, records it in flight before the
    // RPC and returns it, or returns 0 with a local reason without sending anything. A Listen Host answers inside the
    // call, so a caller that must key its pending entry first reads PeekGrowthRequestId(Family) before submitting.
    uint64 SubmitBuild(const FLDBuildCommand& Command, ELDGrowthReason& OutLocalReason);
    uint64 PeekGrowthRequestId(ELDGrowthFamily Family) const;
    void QueryGrowthReceipt(const FLDReceiptQuery& Query);
    // Own accepted projection only; null while the view is invalid.
    const FLDProgressionView* ReadOwnProgression() const;
    FLDOnGrowthSubmit OnGrowthSubmit;
    FLDOnBuildReceipt OnBuildReceipt;
    UFUNCTION(Server,Reliable) void ServerGrowthBuild(const FLDBuildCommand& Command);
    UFUNCTION(Server,Reliable) void ServerQueryGrowthReceipt(const FLDReceiptQuery& Query);
    UFUNCTION(Client,Reliable) void ClientGrowthSubmit(const FLDSubmitResult& Result);
    UFUNCTION(Client,Reliable) void ClientBuildReceipt(const FLDBuildReceiptQueryResult& Result);
    // ---- end Growth ----
    // ---- #198 rest PK sign-up (Framework/LDPlayerRest.cpp) ----
    // Joins or leaves the PK sign-up of the current rest; the result arrives as OnRestSignup (a Listen Host inside the call).
    void SubmitRestSignup(bool bJoin);
    DECLARE_MULTICAST_DELEGATE_OneParam(FLDOnRestSignup, ELDRestSignupResult);
    FLDOnRestSignup OnRestSignup;
    UFUNCTION(Server,Reliable) void ServerRestSignup(uint32 Generation,bool bJoin);
    UFUNCTION(Client,Reliable) void ClientRestSignup(ELDRestSignupResult Result);
    // Resets the own training in the rest space (owner and dummy refilled, cooldowns ready); OnTrainingReset answers.
    void SubmitTrainingReset();
    DECLARE_MULTICAST_DELEGATE_OneParam(FLDOnTrainingReset, ELDTrainingResetResult);
    FLDOnTrainingReset OnTrainingReset;
    UFUNCTION(Server,Reliable) void ServerTrainingReset(uint32 Generation);
    UFUNCTION(Client,Reliable) void ClientTrainingReset(ELDTrainingResetResult Result);
    // ---- end rest ----
    // ---- DEV test commands (#78; LDDevTestProtocol.h, implementation and console commands in LDPlayerDevTest.cpp) ----
    // Fills Generation, Life and the RequestId from the accepted OwnerState like SubmitBuild; one command in flight.
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
    // CanWorldCommand (orders, Q/W/D, A, S, world and minimap clicks) is IsWorldLifeValid && !IsWorldInputSuppressed.
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
    // #97 local slot order (dragging a HUD slot onto another swaps them): SkillAtKey[i] is the combat entry the i-th skill
    // key (Skill1, Skill2, SkillE) and HUD skill slot fire and show. Local presentation and key routing only, like a key
    // binding: the server still sees entries Q/W/E. Each match starts in the default order.
    ELDCombatEntry SkillAtKey[3] = {ELDCombatEntry::Q, ELDCombatEntry::W, ELDCombatEntry::E};
    uint32 SlotOrderMatch = 0;
    void ResetSlotOrder(uint32 Match)
    {
        SkillAtKey[0] = ELDCombatEntry::Q; SkillAtKey[1] = ELDCombatEntry::W; SkillAtKey[2] = ELDCombatEntry::E;
        SlotOrderMatch = Match;
    }
    bool IsAttackMoveArmed() const { return bAttackMoveArmed; }
    void ArmAttackMove();
    // Clears any armed entry or attack-move without sending anything; true if something was armed.
    bool CancelArmedCombat();
    // Left-click confirmation of the armed entry or attack-move; false when nothing was armed. An armed attack-move on an
    // attackable visible unit under the pointer attacks that unit like a right click; elsewhere it attack-moves.
    bool ConfirmArmedCombat();
    // Confirms the armed Q/W entry on an explicit aim (an observed unit handle or a planar direction) instead of
    // the cursor's; ConfirmArmedCombat resolves the cursor aim and ends here. False when no entry was armed. AimCm is
    // the ground point a direction aim points at: a Shift confirmation queues it and the server re-aims from there.
    bool ConfirmArmedEntry(const FLDCombatTarget& Aim, const FVector* AimCm=nullptr);
    // Shift held (either side): the world command being issued joins the server's shift queue instead of replacing
    // the current order (ULDNavigationComponent, 2026-11-06 user decision). Fixed modifier, never a rebindable action.
    bool IsQueueModifierDown() const;
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
    UFUNCTION(Server,Reliable) void ServerSubmitCombatAction(const FLDCombatActionRequest& Request);
    UFUNCTION(Server,Reliable) void ServerCancelCombatAction(const FLDCombatCancelRequest& Request);
    UFUNCTION(Client,Reliable) void ClientCombatReceipt(const FLDCombatActionReceipt& Receipt);
    UPROPERTY(ReplicatedUsing=OnRep_CombatView) FLDCombatOwnerView CombatView;
    UFUNCTION() void OnRep_CombatView();
    // Every receipt of the current match in arrival order; presenters keep their own cursor (LDCombatReceiptLog.h).
    FLDCombatReceiptLog CombatReceipts;
    // ---- Playtest log (Framework/LDPlayerTrace.cpp; docs/DEVELOPMENT.md, playtest troubleshooting) ----
    // The mark key (also the PlaytestMark console command): LD_PLAYER_MARK with the own state in this process's log, the
    // same mark in the Host log and a short HUD confirmation. Only inside a running match of this owner
    // (LDPlaytest::InRunningMatch); the server checks the match the mark was sent for again before the Host log records it.
    UFUNCTION(Exec) void PlaytestMark();
    UFUNCTION(Server,Reliable) void ServerPlaytestMark(uint32 Match,uint32 Sequence,int64 ClientAuthorityMs);
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
    // ---- Growth private request cursors (one in-flight request per family) ----
    struct FGrowthRequestCursor
    {
        uint32 Generation=0;
        uint64 Next=1;
        uint64 InFlight=0;
    };
    FGrowthRequestCursor BuildCursor;
    FGrowthRequestCursor DevTestCursor;
    uint64 BeginGrowthRequest(FGrowthRequestCursor& Cursor, ELDGrowthReason& OutLocalReason);
    void SettleGrowthRequest(FGrowthRequestCursor& Cursor, uint32 Generation, uint64 RequestId, bool bReserved, bool bFinal);
    // ---- end Growth ----
    FLDOwnerState LatestOwner;
    void UpdateLocalViewTarget();
    // Local music (#94): the track for the frontend, or for the room-public phase and authority clock, handed to the audio output.
    void UpdateMusic();
    FTransform LastCameraPose;
    float LastCameraFOV=0.f;
    bool bHasCameraPose=false;
    UPROPERTY() TObjectPtr<class ACameraActor> WaitingCamera;
#if WITH_DEV_AUTOMATION_TESTS
    TWeakObjectPtr<ACameraActor> DevArtCaptureCamera;
    friend class FLDFixedCameraTest;
#endif
    // ---- Combat and orders (combat-orders module, Framework/LDPlayerOrders.cpp and LDPlayerInput.cpp) ----
    // Ends every local gesture without touching an accepted order: the armed preview and the held recenter are
    // cleared and every key must be released before the next world command.
    void ReleaseLocalInput();
    // bQueue sends the request to the shift queue (no pre-input hold: the queue itself waits); AimCm as ConfirmArmedEntry.
    void SendCombatAction(ELDCombatEntry Entry, const FLDCombatTarget& Target, bool bQueue=false, const FVector* AimCm=nullptr);
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
    // The next Direct request of this life with the current source revision; false (and a notice) when exhausted.
    bool NextCombatRequest(ELDCombatEntry Entry, const FLDCombatTarget& Target, FLDCombatActionRequest& Out);
    ELDCombatEntry ArmedEntry=ELDCombatEntry::None;
    bool bAttackMoveArmed=false;
    uint64 NextDirectSequence=0;
    FLDCombatOwnerView LatestCombatView;
    // Local controller only: this process's playtest log (LDPlaytestTrace.h); created on first use.
    TPimplPtr<FLDPlaytestTrace> PlaytestTrace;
    FLDPlaytestTrace& Trace();
};
