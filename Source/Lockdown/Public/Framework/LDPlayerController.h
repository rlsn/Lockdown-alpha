#pragma once
#include "CoreMinimal.h"
#include "Combat/LDTacticalRequest.h"
#include "GameFramework/PlayerController.h"
#include "Vision/LDViewTypes.h"
#include "Inventory/LDVendorTypes.h"
#include "Inventory/LDTradeTypes.h"
#include "Economy/LDBankDirector.h"
#include "Economy/LDCommerceDirector.h"
#include "Economy/LDProductionDirector.h"
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
    void SetMenuOpen(bool bOpen);
    UFUNCTION(Server,Reliable) void ServerTacticalAction(FLDTacticalRequest Request);
    void SendTacticalAction(uint32 Token,uint8 Slot,FVector Direction,FGuid Target,FVector Point=FVector::ZeroVector);
    UFUNCTION(Server,Reliable) void ServerAidConsent(uint32 Match,uint32 Life,bool Allow);
    UFUNCTION(Server,Reliable) void ServerCancelTactical(uint32 Match,uint32 Life,uint32 Through);
    void ClearControls();
    void CancelInterruptibleHolds();
    bool CanWorldCommand() const;
    bool NavigationBindingsActive() const {return bNavigationContextActive;}
    UPROPERTY() TObjectPtr<class ULDControlSettings> Controls;
    UPROPERTY() TObjectPtr<class ULDControlsPanel> ControlsUI;
    UPROPERTY() TObjectPtr<class ULDHUD> HUDUI;
    UPROPERTY() TObjectPtr<class ULDInventoryPanel> InventoryUI;
    void ToggleInventoryPanel();
    FLDTradeView TradeView;
    void OfferTrade();void CancelTrade();
    bool TradePending() const{return TradePendingRequest!=0;}
    void TradeCommand(uint8 Command,FGuid Item=FGuid(),int64 Gold=0);
    UFUNCTION(Server,Reliable) void ServerTradeCommand(uint32 Match,uint32 Life,uint32 Request,uint8 Command,FGuid Session,uint32 Revision,const FString& Digest,FGuid Item,int64 Gold);
    UFUNCTION(Client,Reliable) void ClientTradeUpdate(const FLDOwnerState& State,const FLDTradeView& View,uint32 Request);

    FLDBankView BankView;
    FString BankFeedback;
    uint32 BankToken=0,BankPending=0;
    bool bBankOpening=false;
    bool BankIsOpen() const {return bBankOpening||BankView.Session.IsValid();}
    void OpenBank();void CloseBank();
    void ConfirmCharter(int64 WalletShare,uint32 WR,uint32 BR);
    void ConfirmBank(int64 Amount,bool Deposit,uint32 WalletRevision,uint32 BankRevision);
    UFUNCTION(Server,Reliable) void ServerBankOpen(uint32 Match,uint32 Life,uint32 Token);
    UFUNCTION(Server,Reliable) void ServerBankClose(uint32 Token);
    UFUNCTION(Server,Reliable) void ServerCharterPurchase(uint32 Match,uint32 Life,uint32 Request,FGuid Session,int64 WalletShare,uint32 WR,uint32 BR);
    UFUNCTION(Server,Reliable) void ServerBankCommit(uint32 Match,uint32 Life,uint32 Request,FGuid Session,int64 Amount,bool Deposit,uint32 WalletRevision,uint32 BankRevision);
    UFUNCTION(Client,Reliable) void ClientBankView(const FLDOwnerState& State,const FLDBankView& View,uint32 Request,const FString& Reason);
    FLDCommerceView CommerceView;
    TMap<uint8,FLDCommerceView> LastCommerceQuotes; // owner-local visited quotes, no replication or remote refresh
    FString CommerceFeedback;
    uint32 CommerceToken=0,CommercePending=0;
    bool bCommerceOpening=false,CommerceNeedsReview=false;
    bool CommerceIsOpen() const {return bCommerceOpening||CommerceView.Session.IsValid();}
    bool NearCommerce(uint8 Service) const;
    void OpenCommerce(uint8 Service);void CloseCommerce();
    void SendCommerce(uint8 Command,ELDItemKind Kind,FGuid Item,uint32 Revision,int32 Price);
    UFUNCTION(Server,Reliable) void ServerCommerceOpen(uint32 Match,uint32 Life,uint32 Token,uint8 Service);
    UFUNCTION(Server,Reliable) void ServerCommerceClose(uint32 Token);
    UFUNCTION(Server,Reliable) void ServerCommerceCommit(uint32 Match,uint32 Life,uint32 Request,FGuid Session,uint32 QuoteRevision,uint8 Command,ELDItemKind Kind,FGuid Item,uint32 ItemRevision,uint32 InventoryRevision,int32 Price);
    UFUNCTION(Client,Reliable) void ClientCommerceView(const FLDOwnerState& State,const FLDCommerceView& View,uint32 Request,const FString& Reason);
    bool NearProduction(uint8 Site) const;
    void ArmProduction(const FLDProductionPlan& Plan);
    void StartProductionHold();void StopProductionHold();
    UFUNCTION(Server,Reliable) void ServerProductionRequest(uint32 Match,uint32 Life,uint32 Token,const FLDProductionPlan& Plan);
    UFUNCTION(Server,Unreliable) void ServerProductionHold(uint32 Match,uint32 Life,uint32 Token,uint32 Sequence,bool Moving);
    UFUNCTION(Server,Reliable) void ServerProductionRelease(uint32 Match,uint32 Life,uint32 Token);
    UFUNCTION(Client,Reliable) void ClientProductionResult(const FLDOwnerState& State,uint32 Token,const FString& Reason);
    bool NearGuildVendor() const;
    void OpenVendor();void CloseVendor();
    bool VendorIsOpen() const{return bVendorOpening||VendorQuote.Session.IsValid();}
    bool VendorPending() const{return VendorPendingRequest!=0;}
    FLDVendorQuote VendorQuote;
    FString VendorFeedback;
    bool VendorNeedsReview=false;
    void BuyVendorBook(ELDSkill Skill,uint32 CatalogRevision,int32 Price);
    UFUNCTION(Server,Reliable) void ServerOpenVendor(uint32 Match,uint32 Life,uint32 Token);
    UFUNCTION(Server,Reliable) void ServerCloseVendor(uint32 Token);
    UFUNCTION(Server,Reliable) void ServerVendorBuy(uint32 Match,uint32 Life,uint32 Request,FGuid Session,uint32 CatalogRevision,int32 Price,uint32 InventoryRevision,ELDSkill Skill);
    UFUNCTION(Client,Reliable) void ClientVendorQuote(const FLDOwnerState& State,const FLDVendorQuote& Quote);
    UFUNCTION(Client,Reliable) void ClientVendorClosed(uint32 Token,const FString& Reason);
    UFUNCTION(Client,Reliable) void ClientVendorResult(const FLDOwnerState& State,uint32 Request,const FLDVendorQuote& Quote,FGuid Item,const FString& Reason);
    void InventoryCommand(uint8 Command,FGuid Item=FGuid(),uint8 Slot=0,FVector Point=FVector::ZeroVector);
    void SetQuickItemHeld(uint8 Slot,bool Held,uint8 Gesture=0);
    void ArmWard(FGuid Item);
    void BeginPickup(FGuid Observed);
    bool InventoryPending() const{return CommercePending!=0||BankPending!=0||InventoryPendingRequest!=0||VendorPendingRequest!=0||TradePendingRequest!=0;}
    FGuid ArmedItem() const{return ArmedWard;}
    UFUNCTION(Server,Reliable) void ServerInventoryRequest(uint32 Match,uint32 Life,uint32 Request,uint32 InventoryRevision,uint8 Command,FGuid Item,uint32 ItemRevision,uint8 Slot,FVector_NetQuantize10 Point);
    UFUNCTION(Server,Reliable) void ServerPickupQuote(uint32 Match,uint32 Life,uint32 Token,FGuid Observed);
    UFUNCTION(Client,Reliable) void ClientPickupQuote(uint32 Match,uint32 Life,uint32 Token,FGuid Observed,uint32 InventoryRevision,uint32 ItemRevision);
    UFUNCTION(Client,Reliable) void ClientInventoryResult(const FLDOwnerState& State,uint32 Request,const FString& Result);
    void RebuildV05Mappings();
    void ToggleControlsPanel();
    void V05Back();
    FString BindingLabel(FName Action) const;
    FString ControlSummary() const;
    FKey BindingKey(FName Action) const;
    FVector2D CameraPanAxis() const;
    bool IsTargeting() const {return ArmedSkill>=0||bAttackArmed||ArmedWard.IsValid();}
    bool HasArmedAction() const {return bProductionArmed||IsTargeting()||bDraftArmed||bFabricationArmed||ReadArmed.IsValid();}
    int32 ArmedSlot() const {return ArmedSkill;}
    uint8 PendingSlot() const {return PendingAimRequest?PendingSkillSlot:255;}
    double ArmedRange() const;
    FString HeldInputLabel() const;
    void ArmSkill(uint8 Slot,uint8 Gesture=0);
    void ArmAttack();
    void ConfirmArmed(const FVector& Ground,FGuid Observed);
    void EndSkillGesture(uint8 Slot,uint8 Gesture);
    bool HasPendingAim() const {return PendingAimRequest!=0;}
    void CancelArmed();
    void Inspect(FGuid Observed);
    FGuid ObservedUnderPointer() const;
    bool ReadInspected(FLDObservedEntity& Out) const;
    FGuid Inspected;
    void ArmPreparation(const FLDBuildPlan& Plan);
    void ArmRead(FGuid Book);
    void ArmFabrication();
    UFUNCTION(Server,Reliable) void ServerSkillTargetAction(uint32 Match,uint32 Life,uint32 Request,uint32 BuildRevision,uint8 Slot,FVector_NetQuantizeNormal Direction,FGuid Observed);
    UPROPERTY(VisibleAnywhere) TObjectPtr<class ULDNavigationComponent> Navigation;
    void ChooseLobbyClass(uint8 ClassId);
    void ToggleLobbyReady();
    void StartLobby();
    void ToggleDevelopmentPanel();
    void BuyBook(ELDSkill Skill);
    void StartDevelopmentHold(uint8 Kind,FGuid Item,const FLDBuildPlan& Plan);
    void StopDevelopmentHold();
    void SetInteractionHeld(bool Held);
    void SetFabricationHeld(bool Held);
    void SetSkillHeld(uint8 Slot,bool Held);
    void PressBasicAttack();
    UFUNCTION(Server,Reliable) void ServerDevelopmentRequest(uint32 Match,uint32 Life,uint32 Request,uint8 Kind,ELDSkill Skill,FGuid Item,const FLDBuildPlan& Plan);
    UFUNCTION(Server,Unreliable) void ServerDevelopmentHold(uint32 Match,uint32 Life,uint32 Token,uint32 Sequence,bool Moving);
    UFUNCTION(Server,Reliable) void ServerDevelopmentRelease(uint32 Match,uint32 Life,uint32 Token);
    UFUNCTION(Client,Reliable) void ClientDevelopmentResult(const FLDOwnerState& State,uint32 Request,const FString& Result);
    UPROPERTY() TObjectPtr<class ULDDevelopmentPanel> DevelopmentUI;
    UFUNCTION(Server,Reliable) void ServerLobbyChoice(uint32 Match,uint32 Life,uint32 BuildRevision,uint8 ClassId,bool Ready);
    UFUNCTION(Server,Reliable) void ServerStartLobby(uint32 Match,uint32 Life);
    bool IsFrontend() const;
    UPROPERTY(ReplicatedUsing=OnRep_OwnerState) FLDOwnerState OwnerState;
    UFUNCTION() void OnRep_OwnerState();
    UFUNCTION(Client,Reliable) void ClientLifeReceipt(const FLDOwnerState& State);
    UFUNCTION(Client,Reliable) void ClientCombatResult(const FLDOwnerState& State,uint32 Request,const FString& Result,float Facing);
    UFUNCTION(Server,Reliable) void ServerCombatAction(uint32 Match,uint32 Life,uint32 Request,uint8 Action,FVector_NetQuantizeNormal Direction);
    UFUNCTION(Server,Reliable) void ServerSkillAction(uint32 Match,uint32 Life,uint32 Request,uint32 BuildRevision,uint8 Slot,FVector_NetQuantizeNormal Direction);
    UFUNCTION(Server,Unreliable) void ServerSkillHold(uint32 Match,uint32 Life,uint32 Token,uint32 Sequence,bool Moving);
    UFUNCTION(Server,Reliable) void ServerSkillRelease(uint32 Match,uint32 Life,uint32 Token);
    void CompleteCombatRequest(uint32 Request,const FString& Result);
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    class ALDWorldViewRelay* LocalView() const;
    FString IntentFeedback;
    uint32 FeedbackSerial() const{return IntentFeedbackSerial;}
    void TogglePublicSensorFixture();
    void ToggleDefenceFixture(uint8 Index);
    void SeedSealFixture();
    FString ContextPrompt() const;
    FString LegacyContextPrompt() const;
    bool ChooseContext(uint8& Target,FGuid& Observed) const;
    UFUNCTION(Server,Reliable) void ServerContextRequest(uint32 Match,uint32 Life,uint32 Request,uint8 Target,FGuid Observed);
    UFUNCTION(Server,Unreliable) void ServerContextHold(uint32 Match,uint32 Life,uint32 Token,uint32 Sequence,bool Moving);
    UFUNCTION(Server,Reliable) void ServerContextRelease(uint32 Match,uint32 Life,uint32 Token);
    UFUNCTION(Client,Reliable) void ClientTransactionResult(const FLDOwnerState& State,uint32 Request,const FString& Result);
    void CompleteContextRequest(uint32 Request,const FString& Result);
    UFUNCTION(Server,Reliable) void ServerPlaceWard(uint32 Match, uint32 Life, uint32 Request, FVector_NetQuantize10 Position);
    UFUNCTION(Client,Reliable) void ClientIntentResult(uint32 Request, const FString& Reason);
    UFUNCTION(Client,Reliable) void ClientProbePhase(uint8 Phase);
    uint8 ProbePhase = 0;
    uint8 ProbeRole = 0;
    UFUNCTION(Server,Reliable) void ServerProbeHello(uint8 ProbeIdentity);
    UFUNCTION(Server,Unreliable) void ServerProbePing(double Sent);
    UFUNCTION(Client,Unreliable) void ClientProbePong(double Sent);
    TArray<double> ProbeRtts;
    TArray<double> ProbeAckMs;
    TMap<uint32,double> ProbeRequests;
    void RecordProbeRequest(uint32 Request){ProbeRequests.Add(Request,FPlatformTime::Seconds());}
    UPROPERTY() TObjectPtr<ULDUIRoot> RootUI;
private:
    friend class ALDEconomyUIProbe;
    friend class ALDCharterProbe;
    void InitializeLocalUI();
    TArray<TPair<uint32,FString>> SeenIntentResults;
    uint32 IntentFeedbackSerial=0;
    friend class ALDInputProbe;
    friend class ALDProductionProbe;
    friend class ALDInstitutionProbe;
    friend class ALDInventoryProbe;
    friend class ALDVendorProbe;
    friend class ALDTradeProbe;
    friend class ULDInventoryPanel;
    uint32 TradePendingRequest=0,TradeSerial=0;
    bool bApplyingTradeView=false,bTradeDismissed=false;
    TArray<FGuid> SeenTradeCompletions;
    uint32 VendorOpenToken=0,VendorPendingRequest=0;
    bool bVendorOpening=false;
    bool bControlsSuppressed = false;
    float AimSendTime = 0;
    double LastAcceptedAimTime = -1;
    void Move(const FInputActionValue& Value);
    void SprintStarted();
    void SprintStopped();
    void Zoom(const FInputActionValue& Value);
    void ToggleMenu();
    void Ward();
    void Attack();
    void Assassinate();
    void Dash();
    void RecenterEnded();
    void NavigateClick();
    void StopOrder();
    void CameraFollow();
    void CombatAction(uint8 Action);
    void SkillStarted(uint8 Slot);
    void SkillStopped(uint8 Slot);
    void SkillAlias(ELDSkill Skill);
    void EnqueueAction(uint32 Match,uint32 Life,uint32 Request,uint8 Action,const FVector& Direction,uint32 BuildRevision=0,FGuid Observed=FGuid(),FVector Point=FVector::ZeroVector);
    UPROPERTY() TObjectPtr<class UInputMappingContext> SkillInputContext;
    UPROPERTY() TObjectPtr<class UInputMappingContext> NavigationInputContext;
    bool bNavigationContextActive=false;
    void UpdateV05Inputs(float Delta);
    void V05Pressed(FName Action);
    void V05Released(FName Action);
    void V05Zoom(const FInputActionValue& Value);
    void ContextV05();
    void BeginTargetSkill(uint8 Slot,FGuid Observed,const FVector& Aim,uint8 Gesture);
    bool bFreshInputRequired=false,bPrimaryOwned=false,bAttackArmed=false,bDraftArmed=false,bFabricationArmed=false;
    int32 ArmedSkill=-1;
    uint8 HeldSkillGesture=0;
    FGuid HeldObserved,ReadArmed;
    FGuid ArmedWard;
    uint32 ArmedItemRevision=0,InventoryPendingRequest=0,PendingPickupToken=0;
    int32 HeldQuickSlot=-1;
    uint8 HeldQuickGesture=0;
    FLDBuildPlan PreparedDraft;
    uint32 InspectedGeneration=0;
    double PreferenceSaveAt=0;
    TWeakObjectPtr<APawn> PreferencesPawn;
    TArray<uint32> V05BindingHandles;
    uint32 PendingAimRequest=0;
    uint8 PendingSkillSlot=255;
    uint32 SkillHoldToken=0,SkillHoldSequence=0;
    uint8 HeldSkillSlot=255;
    double LastSkillHoldSent=-1000;
    FLDProductionPlan ProductionPlan;
    bool bProductionArmed=false;
    uint32 ProductionHoldToken=0,ProductionSequence=0;
    double LastProductionSent=-1000;
    uint32 DevelopmentHoldToken=0,DevelopmentHoldSequence=0;
    double LastDevelopmentHoldSent=-1000;
    void InteractStarted();
    void StartInteraction(bool Fabricate);
    void InteractStopped();
    bool bInteractHeld=false;
    uint32 ContextToken=0,HoldSequence=0,LastContextRequest=0;
    double LastHoldSent=-1000,LastHoldReceived=-1000,ContextRateAt=-1,ContextTokens=2;
    TMap<uint32,FString> ContextReceipts;
    FLDOwnerState LatestOwner;
    uint32 LastCombatRequest=0;
    double LastCombatAt=-1,CombatTokens=3;
    FString LastCombatResult;
    TMap<uint32,FString> CombatReceipts;
    FVector LastBodyPosition=FVector::ZeroVector;
    UPROPERTY() TObjectPtr<class ACameraActor> WaitingCamera;
    uint32 NextRequest = 0, LastWardRequest = 0;
    double LastWardAt = -1;
    FString LastWardResult;
    UFUNCTION(Server, Unreliable) void ServerSetAim(uint32 Match,uint32 Life,float Yaw);
};
