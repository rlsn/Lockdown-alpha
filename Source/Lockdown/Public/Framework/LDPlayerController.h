#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Vision/LDViewTypes.h"
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
    void ClearControls();
    void CancelInterruptibleHolds();
    bool CanWorldCommand() const;
    bool NavigationBindingsActive() const {return bNavigationContextActive;}
    UPROPERTY() TObjectPtr<class ULDControlSettings> Controls;
    UPROPERTY() TObjectPtr<class ULDControlsPanel> ControlsUI;
    void RebuildV05Mappings();
    void ToggleControlsPanel();
    void V05Back();
    FString BindingLabel(FName Action) const;
    FString ControlSummary() const;
    FKey BindingKey(FName Action) const;
    FVector2D CameraPanAxis() const;
    bool IsTargeting() const {return ArmedSkill>=0||bAttackArmed;}
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
    friend class ALDInputProbe;
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
    void EnqueueAction(uint32 Match,uint32 Life,uint32 Request,uint8 Action,const FVector& Direction,uint32 BuildRevision=0,FGuid Observed=FGuid());
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
    FLDBuildPlan PreparedDraft;
    uint32 InspectedGeneration=0;
    double PreferenceSaveAt=0;
    TWeakObjectPtr<APawn> PreferencesPawn;
    TArray<uint32> V05BindingHandles;
    uint32 PendingAimRequest=0;
    uint32 SkillHoldToken=0,SkillHoldSequence=0;
    uint8 HeldSkillSlot=255;
    double LastSkillHoldSent=-1000;
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
