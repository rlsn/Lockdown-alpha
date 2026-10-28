#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/HUD.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "World/LDWorldObjects.h"
#include "LDEnvironmentDirector.generated.h"

class UStaticMeshComponent;
class ALDCharacter;
class ULDHumanoidVisualComponent;

UENUM() enum class ELDWorldVerb:uint8 { Open=1,Close,Lockpick,Force,Lock };
USTRUCT()
struct FLDWorldRequest
{
    GENERATED_BODY()
    UPROPERTY() uint32 Match=0;
    UPROPERTY() uint32 Life=0;
    UPROPERTY() uint32 Sequence=0;
    UPROPERTY() uint32 Layout=1;
    UPROPERTY() FGuid Observed;
    UPROPERTY() uint32 ObjectRevision=0;
    UPROPERTY() ELDWorldVerb Verb=ELDWorldVerb::Open;
    UPROPERTY() FString Contract;
};
// A separate sparse full-map wire record. The legacy 60x40 fog codec is unchanged.
// Doors have public static addresses; body handles are observer-local opaque IDs.
USTRUCT()
struct FLDEnvironmentViewEntry
{
    GENERATED_BODY()
    UPROPERTY() FGuid Handle;
    UPROPERTY() FName PublicDoor;
    UPROPERTY() FVector_NetQuantize10 Position=FVector::ZeroVector;
    UPROPERTY() uint32 Revision=0;
    UPROPERTY() uint8 State=0; // door open/locked/broken, or public body pose
    UPROPERTY() uint8 Presentation=0;
    UPROPERTY() uint16 Facing=0;
    UPROPERTY() uint8 Light=0; // observed cosmetic cue only; never a sight source
};
USTRUCT()
struct FLDWorldReceipt
{
    GENERATED_BODY()
    UPROPERTY() uint32 Sequence=0;
    UPROPERTY() FGuid Receipt;
    UPROPERTY() uint8 Result=0; // pending, committed, rejected/cancelled
    UPROPERTY() uint32 Revision=0;
    UPROPERTY() FString Reason;
};

UCLASS()
class LOCKDOWN_API ALDEnvironmentDoor:public AActor
{
    GENERATED_BODY()
public:
    ALDEnvironmentDoor();
    FName Id,Building,Resident;
    uint32 Revision=1,CollisionRevision=1,SightRevision=1,NavigationRevision=1;
    uint32 ProxyUpdates=0;
    bool Rear=false,Open=false,Locked=false,Broken=false;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Proxy;
    void Apply(uint8 State,uint32 NewRevision);
    uint8 State() const {return uint8(Open)|(uint8(Locked)<<1)|(uint8(Broken)<<2);}
};

UCLASS()
class LOCKDOWN_API ALDEnvironmentDirector:public AActor
{
    GENERATED_BODY()
public:
    ALDEnvironmentDirector();
    virtual void Tick(float Delta) override;
    bool Initialize(FString& Error);
    static ALDEnvironmentDirector* Get(UWorld* World);
    static bool InFullMap(const UWorld* World);
    static bool LoadContract(FString& Json,FString& Error);
    ALDEnvironmentDoor* Door(FName Id) const;
    TArray<TWeakObjectPtr<ALDEnvironmentDoor>> Doors;
    TArray<FVector> RoofGoals,StairStarts;
    FString Contract;
    uint32 MutationRevision=1;
    bool Ready=false;
    void Begin(class ALDB91Controller* PC,const FLDWorldRequest& Request);
    void Hold(class ALDB91Controller* PC,uint32 Match,uint32 Life,uint32 Sequence,uint32 Nonce,bool Release);
    void Cancel(class ALDB91Controller* PC,const FString& Reason);
    bool ResidentLock(FName DoorId,FName Resident,bool Locked);
    bool Ray(FVector From,FVector To,const AActor* Endpoint=nullptr,double Radius=1800) const;
    float SightFor(ALDPlayerController* PC) const;
    bool Visible(ALDPlayerController* PC,AActor* Actor) const;
    AActor* Resolve(ALDPlayerController* PC,FGuid Handle) const;
    void Publish();
    TArray<FLDWorldReceipt> Journal;
private:
    struct FObserver
    {
        TMap<TWeakObjectPtr<AActor>,FGuid> Handles;
        TSet<FGuid> Visible;
    };
    struct FClient
    {
        uint32 Last=0,CancelledThrough=0;
        double RateAt=0,Tokens=8;
        TMap<uint64,FLDWorldReceipt> Receipts;
    };
    struct FHold
    {
        FLDWorldRequest Request;
        TWeakObjectPtr<ALDEnvironmentDoor> Door;
        FVector Start;
        double Started=0,Ends=0,Heartbeat=0,DamageAt=0;
        uint32 Nonce=0;
    };
    TMap<TWeakObjectPtr<ALDPlayerController>,FObserver> Observers;
    TMap<TWeakObjectPtr<ALDB91Controller>,FClient> Clients;
    TMap<TWeakObjectPtr<ALDB91Controller>,FHold> Holds;
    struct FCutaway{TWeakObjectPtr<UStaticMeshComponent> Floor;FVector Centre;};
    TArray<FCutaway> Cutaways;
    TArray<TWeakObjectPtr<UStaticMeshComponent>> CameraWalls;
    double NextPublish=0;
    uint64 ViewRevision=0;
    bool Eligible(ALDB91Controller* PC,ALDEnvironmentDoor* Door,ELDWorldVerb Verb,FString& Why) const;
    bool Occupied(const ALDEnvironmentDoor* Door) const;
    void Reply(ALDB91Controller* PC,const FLDWorldRequest& Request,uint8 Result,const FString& Why,uint32 Revision=0);
};

UCLASS()
class LOCKDOWN_API ALDB91Controller:public ALDPlayerController
{
    GENERATED_BODY()
public:
    virtual void BeginPlay() override;
    virtual void PlayerTick(float Delta) override;
    virtual void SetupInputComponent() override;
    virtual bool EnvironmentUI() const override {return true;}
    UFUNCTION(Server,Reliable) void ServerWorldInteraction(FLDWorldRequest Request);
    UFUNCTION(Server,Unreliable) void ServerWorldHold(uint32 Match,uint32 Life,uint32 Sequence,uint32 NonceValue,bool Release);
    UFUNCTION(Server,Reliable) void ServerWorldRelease(uint32 Match,uint32 Life,uint32 Sequence);
    UFUNCTION(Client,Reliable) void ClientEnvironmentView(uint32 Match,uint32 Life,uint64 Revision,const TArray<FLDEnvironmentViewEntry>& Entries);
    UFUNCTION(Client,Reliable) void ClientWorldReceipt(FLDWorldReceipt Receipt);
    UFUNCTION(Server,Reliable) void ServerObjectInteraction(FLDObjectRequest Request);
    UFUNCTION(Server,Unreliable) void ServerObjectHold(uint32 Match,uint32 Life,uint32 Sequence,uint32 NonceValue);
    UFUNCTION(Server,Reliable) void ServerObjectRelease(uint32 Match,uint32 Life,uint32 Sequence);
    UFUNCTION(Client,Reliable) void ClientObjectReceipt(FLDWorldReceipt Receipt);
    UFUNCTION(Client,Reliable) void ClientObjectView(uint32 Match,uint32 Life,uint64 Revision,const TArray<FLDObjectView>& Entries);
    UFUNCTION(Client,Reliable) void ClientContainerView(uint32 Match,uint32 Life,FGuid Handle,uint32 Revision,const TArray<FLDContainerEntry>& Entries);
    UFUNCTION(Client,Reliable) void ClientCargoState(uint32 Match,uint32 Life,bool Carrying,FGuid Observed);
    UFUNCTION(Client,Reliable) void ClientTraversalState(uint32 Match,uint32 Life,uint32 Sequence,bool Active,FVector_NetQuantize10 Position);
    UFUNCTION(Client,Unreliable) void ClientTraversalPose(uint32 Match,uint32 Life,uint32 Sequence,FVector_NetQuantize10 Position);
    bool bTraversalActive=false;
    void StartLink(ELDObjectVerb Verb);
    TArray<FLDObjectView> ObjectView;
    TArray<FLDContainerEntry> ContainerView;
    FGuid ContainerHandle;
    uint32 ContainerRevision=0;
    FLDWorldReceipt LastObjectReceipt;
    uint32 ObjectReceiptSerial=0;
    bool bObjectCargo=false;
    FGuid CargoHandle;
    bool HasObjectCargo() const;
    const FLDObjectView* NearestObject() const;
    void StartObject(ELDObjectVerb Verb,FVector Point=FVector::ZeroVector);
    void ToggleSelectedTool();
    int32 ContainerSelection=0,BagSelection=0;
    TArray<FLDEnvironmentViewEntry> View;
    FLDWorldReceipt LastWorldReceipt;
    uint32 WorldReceiptSerial=0;
    uint64 LastViewRevision=0;
    uint32 LastViewMatch=0,LastViewLife=0;
    double LastViewAt=0;
    const FLDEnvironmentViewEntry* NearestDoor() const;
    void StartDoor(ELDWorldVerb Verb);
    void ReleaseDoor();
    // Explicit diagnostic handshake; disabled without the native network probe flag.
    uint8 EnvironmentProbeRole=0;
    TMap<uint8,FString> EnvironmentProbeReports;
    UFUNCTION(Server,Reliable) void ServerB91ProbeHello(uint8 ProbeIdentity);
    UFUNCTION(Server,Reliable) void ServerB91ProbeAck(uint8 Phase,const FString& Report);
    UFUNCTION(Client,Reliable) void ClientB91ProbeTask(uint8 Phase);
    void TickB91Probe();
    void TickB91ObjectsProbe();
    void TickB91ToolsProbe();
    void TickB91SaplingsProbe();
    void TickB91TraversalProbe();
private:
    uint32 WorldSequence=0,HeldSequence=0,Nonce=0;
    double SentAt=0;
    FKey HeldKey;
    bool HeldObject=false;
    bool ToolBindingHeld=false;
    FKey ToolBindingKey;
    uint64 ObjectViewRevision=0;
    uint32 ObjectViewMatch=0,ObjectViewLife=0;
    uint32 TraversalSequence=0;
    uint32 TraversalMatch=0,TraversalLife=0;
    double ObjectViewAt=0;
    TMap<FGuid,TWeakObjectPtr<ALDWorldObject>> ObjectProxies;
    TMap<FGuid,TWeakObjectPtr<ULDHumanoidVisualComponent>> BodyViews;
    uint8 ProbeTask=0,ProbeStep=0;
    uint8 ProbeRetries=0;
    bool ProbeSent=false;
    bool TraversalProbeDeployer=false;
    bool SaplingProbeWinner=false;
    double ProbeAt=0,ProbeHoldAt=0;
    double ProbeRetryAt=0;
    FLDWorldRequest ProbeRequest;
    FLDObjectRequest ObjectProbeRequest;
    FGuid ToolProbeIdentity;
    uint32 ToolProbeDevToken=0,ToolProbeDevNonce=0;
    double ToolProbeDevAt=0;
    FVector ObjectProbePad=FVector::ZeroVector,ObjectProbeStart=FVector::ZeroVector;
    int32 ObjectProbeContainerBefore=0;
    bool ObjectProbeRejectedPlace=false;
    FGuid ProbeReceipt;
    uint32 ProbeDuplicateSerial=0;
    FString ProbeMotion=TEXT("phase,time_s,x_cm,y_cm,z_cm\n");
    uint32 ProbeFrame=0;
    double ProbeCaptureAt=0;
    FString ProbeFrames=TEXT("frame,world_seconds,phase\n");
};

UCLASS()
class LOCKDOWN_API ALDB91GameMode:public ALDGameMode
{
    GENERATED_BODY()
public:
    ALDB91GameMode();
};
UCLASS()
class LOCKDOWN_API ALDB91HUD:public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
};
