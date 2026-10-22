#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Inventory/LDInventoryTypes.h"
#include "LDObjectiveDirector.generated.h"
class ALDPlayerController;
class ALDCharacter;

UCLASS()
class LOCKDOWN_API ALDSeal : public ALDItem
{
    GENERATED_BODY()
public:
    ALDSeal();
    // Server-only immutable identity, ownership and state; only dropped poses enter observations.
    uint8 District=0;
    // Kept for the retained objective code; the shared Holder is authoritative in V05.
    TWeakObjectPtr<ALDPlayerController> Carrier;
};

UCLASS()
class LOCKDOWN_API ALDObjectiveSite : public AActor
{
    GENERATED_BODY()
public:
    ALDObjectiveSite();
    void Configure(uint8 Site);
private:
    UPROPERTY() TObjectPtr<class UStaticMeshComponent> Marker;
    UPROPERTY() TObjectPtr<class UTextRenderComponent> Label;
};

struct FLDInteractionState
{
    uint32 Token=0,Life=0,HoldSequence=0,CancelledThrough=0;
    uint8 Target=0,Channel=0,Route=0;
    bool bHeld=false,bMoving=false,bContested=false;
    double LastHold=-1000,Started=0,LastInterrupt=-1000;
    FVector Anchor=FVector::ZeroVector;
    FGuid Contract;
};

UCLASS()
class LOCKDOWN_API ALDObjectiveDirector : public AActor
{
    GENERATED_BODY()
public:
    ALDObjectiveDirector();
    virtual void BeginPlay() override;
    void Queue(ALDPlayerController* PC,uint32 Match,uint32 Life,uint32 Request,uint8 Target,FGuid Observed);
    void RefreshHold(ALDPlayerController* PC,uint32 Life,uint32 Token,uint32 Sequence,bool Moving,double Now);
    void Release(ALDPlayerController* PC,uint32 Life,uint32 Token);
    void Interrupt(ALDPlayerController* PC,double Now,bool bEndHold=false);
    void EndLife(ALDPlayerController* PC,const FVector& Point,double Now);
    void Withdraw(ALDPlayerController* PC,const FVector& Point,double Now);
    void Sample(double Now);
    bool HasChannel(ALDPlayerController* PC) const;
    bool HeartSensorActive(double Now) const {return Now<HeartSensorUntil;}
    ALDSeal* SeedFixtureSeal();
    bool SetDefencesFixture(uint8 Flags);
    const FLDInteractionState* StateFor(ALDPlayerController* PC) const {return Interactions.Find(PC);}
    double Deadline() const;
    int32 QueuedRequestCount() const {return Requests.Num();}
    void FailNextFabricationForProbe();
    void AnnounceVoluntaryDrop(ALDSeal* Item,const FVector& Point);
private:
    bool bFailFabrication=false;
    struct FRequest {TWeakObjectPtr<ALDPlayerController> PC;uint32 Match,Life,Id;uint8 Target;FGuid Observed;};
    TArray<FRequest> Requests;
    TMap<TWeakObjectPtr<ALDPlayerController>,FLDInteractionState> Interactions;
    FRandomStream Transactions;
    bool bSeeded=false;
    double HeartSensorUntil=0;
    bool Eligible(ALDPlayerController* PC,double Now) const;
    bool CanFabricate(ALDPlayerController* PC) const;
    bool Near(ALDPlayerController* PC,uint8 Site,double Range=120) const;
    bool Stationary(ALDPlayerController* PC,const FLDInteractionState& S) const;
    void Request(const FRequest& R,double Now);
    void BeginChannel(ALDPlayerController* PC,FLDInteractionState& S,double Now);
    void ClearChannel(ALDPlayerController* PC,FLDInteractionState& S);
    void UpdatePrivate(ALDPlayerController* PC,FLDInteractionState& S,double Now);
    ALDSeal* CarriedBy(ALDPlayerController* PC) const;
    void Drop(ALDPlayerController* PC,const FVector& Point);
    void Announce(ALDSeal* Item,uint8 Kind);
    void Finish(ALDPlayerController* Winner,uint8 Route,double Now);
};
