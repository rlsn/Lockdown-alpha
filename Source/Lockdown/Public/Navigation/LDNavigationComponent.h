#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LDNavigationComponent.generated.h"

// Also carried by saved native moves. No route points or actor pointers cross the wire.
USTRUCT()
struct FLDMoveIdentity
{
    GENERATED_BODY()
    UPROPERTY() uint32 Match=0;
    UPROPERTY() uint32 Life=0;
    UPROPERTY() uint32 Order=0;
    UPROPERTY() uint32 Epoch=0;
    bool operator==(const FLDMoveIdentity& R) const {return Match==R.Match&&Life==R.Life&&Order==R.Order&&Epoch==R.Epoch;}
    bool operator!=(const FLDMoveIdentity& R) const {return !(*this==R);}
};

class ALDPlayerController;
class ALDCharacter;

UCLASS()
class LOCKDOWN_API ULDNavigationComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    ULDNavigationComponent();
    virtual void TickComponent(float Delta,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction) override;
    bool Enabled() const;
    void Navigate(const FVector& Surface,FGuid Observed=FGuid(),uint8 Site=0);
    void Stop();
    void ResetLife();
    void ClickMove();
    void PanCamera(const FVector2D& Axis,float Delta);
    void Recenter(bool Held);
    void ToggleFollow();
    void PanTo(const FVector& Point);
    bool bFollowCamera=true,bEdgePan=true;
    bool IsFollowing() const {return bFollowing;}
    bool HasMoveOrder() const {return bLocalActive;}
    bool IsAuthorityActive() const {return bServerActive;}
    FLDMoveIdentity LocalIdentity() const {return Local;}
    bool AuthorizesMove(const FLDMoveIdentity& Identity) const;
    uint32 QueryCount=0,RejectedMoves=0,Replans=0;
    double QueryMilliseconds=0;
    TArray<double> AcknowledgementMs;
    TArray<double> RouteAcceptanceMs;
    FString Status;
    FVector Destination=FVector::ZeroVector;
    // Explicitly callable for deterministic adversarial RPC probes; all validation is authority-side.
    UFUNCTION(Server,Reliable) void ServerOrder(FLDMoveIdentity Identity,FVector_NetQuantize10 Surface,FGuid Observed,uint8 Site);
    UFUNCTION(Server,Reliable) void ServerStop(FLDMoveIdentity Identity);
    UFUNCTION(Server,Unreliable) void ServerHeartbeat(FLDMoveIdentity Identity,uint32 Sequence);
    UFUNCTION(Client,Reliable) void ClientOrder(FLDMoveIdentity Identity,uint8 Result,FVector_NetQuantize10 Goal,float Acceptance,const FString& Reason);
private:
    ALDPlayerController* Controller() const;
    bool Plan(const FVector& Surface,TArray<FVector>& Out,FVector& Projected);
    bool ResolveOrder(FVector& Goal,float& Acceptance,TArray<FVector>& Path);
    void ProcessOrder();
    void EndServer(uint8 Result,const FString& Reason);
    void TickCamera(float Delta);
    void Face(const FVector& Direction);
    FLDMoveIdentity Local,Server;
    uint32 Serial=0,Epoch=0,AcceptedOrder=0,HeartbeatSequence=0,ServerHeartbeatSequence=0;
    uint32 AcknowledgedOrder=0,PendingAcknowledgedOrder=0;
    uint32 SeenMatch=0,SeenLife=0;
    bool bFollowing=false,bLocalActive=false,bServerActive=false,bPending=false,bReplanned=false,bServerReplanned=false;
    bool bRecenterHeld=false,bCameraInitialized=false;
    bool bHadFocus=true;
    TArray<FVector> Points;
    TArray<FVector> ServerPoints;
    int32 PointIndex=0;
    FVector ServerGoal=FVector::ZeroVector,PendingSurface=FVector::ZeroVector;
    FVector ProgressAnchor=FVector::ZeroVector,ServerProgressAnchor=FVector::ZeroVector,CameraCentre=FVector::ZeroVector;
    FGuid Approach;
    uint8 PendingSite=0,LastResult=0;
    float AcceptanceRadius=40,ServerAcceptance=40;
    double LastHeartbeatSent=-1000,LastHeartbeat=0,NextQuery=0,LastProgress=0,ServerLastProgress=0;
    double BestProgress=0,ServerBestProgress=0;
    double LocalSentAt=0;
    TWeakObjectPtr<ALDCharacter> LastPawn;
};
