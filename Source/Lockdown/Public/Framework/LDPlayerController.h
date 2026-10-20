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
    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;
    virtual void PlayerTick(float Delta) override;
    virtual void FlushPressedKeys() override;
    void SetMenuOpen(bool bOpen);
    void ClearControls();
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
    void PressBasicAttack(){Attack();}
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
    void CombatAction(uint8 Action);
    void SkillStarted(uint8 Slot);
    void SkillStopped(uint8 Slot);
    void SkillAlias(ELDSkill Skill);
    void EnqueueAction(uint32 Match,uint32 Life,uint32 Request,uint8 Action,const FVector& Direction,uint32 BuildRevision=0);
    UPROPERTY() TObjectPtr<class UInputMappingContext> SkillInputContext;
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
