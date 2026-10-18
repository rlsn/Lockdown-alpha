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
    bool IsFrontend() const;
    UPROPERTY(ReplicatedUsing=OnRep_OwnerState) FLDOwnerState OwnerState;
    UFUNCTION() void OnRep_OwnerState();
    UFUNCTION(Client,Reliable) void ClientLifeReceipt(const FLDOwnerState& State);
    UFUNCTION(Server,Reliable) void ServerCombatAction(uint32 Match,uint32 Life,uint32 Request,uint8 Action,FVector_NetQuantizeNormal Direction);
    void CompleteCombatRequest(uint32 Request,const FString& Result);
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    class ALDWorldViewRelay* LocalView() const;
    FString IntentFeedback;
    void TogglePublicSensorFixture();
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
