#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Inventory/LDInventoryTypes.h"
#include "LDWorldObjects.generated.h"
class ALDB91Controller;
class ALDCharacter;
class UStaticMeshComponent;
class UPointLightComponent;

UENUM() enum class ELDObjectKind:uint8 { Chest=1,Crate,Barrel,Water,Oil,LooseItem,Torch,Lantern,Rope,Ladder,Anchor };
UENUM() enum class ELDObjectVerb:uint8 { Open=1,Take,Store,Pickup,Drop,Carry,Place,Push,Break,Toggle,Attach,Deploy,Use,Retrieve };
USTRUCT()
struct FLDObjectRequest
{
    GENERATED_BODY()
    UPROPERTY() uint32 Match=0;
    UPROPERTY() uint32 Life=0;
    UPROPERTY() uint32 Sequence=0;
    UPROPERTY() uint32 Layout=1;
    UPROPERTY() FString Contract;
    UPROPERTY() FGuid Observed;
    UPROPERTY() uint32 ObjectRevision=0;
    UPROPERTY() uint32 InventoryRevision=0;
    UPROPERTY() FGuid Item;
    UPROPERTY() uint32 ItemRevision=0;
    UPROPERTY() ELDObjectVerb Verb=ELDObjectVerb::Open;
    UPROPERTY() uint8 Accessory=0;
    UPROPERTY() FGuid Anchor;
    UPROPERTY() uint32 AnchorRevision=0;
    UPROPERTY() FVector_NetQuantize10 Point=FVector::ZeroVector;
};
// Observer-local identity, public geometry and state only. Contents require a
// separate, short-lived owner quote after a legal Open transaction.
USTRUCT()
struct FLDObjectView
{
    GENERATED_BODY()
    UPROPERTY() FGuid Handle;
    UPROPERTY() ELDObjectKind Kind=ELDObjectKind::Chest;
    UPROPERTY() FVector_NetQuantize10 Position=FVector::ZeroVector;
    UPROPERTY() uint32 Revision=0;
    UPROPERTY() uint8 State=0;
    UPROPERTY() uint32 PhysicalRevision=0;
    UPROPERTY() FVector_NetQuantize10 Endpoint=FVector::ZeroVector;
};
USTRUCT()
struct FLDContainerEntry
{
    GENERATED_BODY()
    UPROPERTY() FGuid Item;
    UPROPERTY() ELDItemKind Kind=ELDItemKind::Fiber;
    UPROPERTY() uint32 Revision=0;
};
UCLASS()
class LOCKDOWN_API ALDWorldObject:public AActor
{
    GENERATED_BODY()
public:
    ALDWorldObject();
    FName SourceId;
    ELDObjectKind Kind=ELDObjectKind::Chest;
    uint32 Revision=1,CollisionRevision=1,SightRevision=1,NavigationRevision=1,ProxyUpdates=0;
    uint8 AppliedState=255;
    bool Broken=false;
    bool bCarriedWorld=false;
    TWeakObjectPtr<ALDB91Controller> Carrier;
    TArray<TWeakObjectPtr<ALDItem>> Contents;
    TWeakObjectPtr<ALDItem> Physical;
    FVector SourcePosition,LastClear;
    FVector Lower=FVector::ZeroVector,Upper=FVector::ZeroVector;
    TWeakObjectPtr<ALDWorldObject> LinkedLadder,InstalledAt;
    FGuid Deployer;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Proxy;
    UPROPERTY() TObjectPtr<UPointLightComponent> Glow;
    FVector Size() const;
    bool Heavy() const {return Kind==ELDObjectKind::Crate||Kind==ELDObjectKind::Barrel||Kind==ELDObjectKind::Ladder;}
    bool BlocksWalk() const {return Kind==ELDObjectKind::Crate||Kind==ELDObjectKind::Barrel;}
    bool Container() const {return Kind==ELDObjectKind::Chest||BlocksWalk();}
    bool LinkInstalled() const {return Kind==ELDObjectKind::Anchor&&(LinkedLadder.IsValid()||!Contents.IsEmpty());}
    bool Available() const;
    int32 Weight() const;
    uint8 Flags() const {return uint8(Broken)|(uint8(bCarriedWorld)<<1)|uint8(!Available()?4:0)|uint8(Physical.IsValid()&&Physical->State==0&&!Physical->Holder.IsValid()&&Physical->bLit?8:0)|uint8(LinkInstalled()||InstalledAt.IsValid()?16:0);}
    FVector EndFor(FVector Body) const {return FVector::DistSquared(Body,Lower+FVector(0,0,90))<=FVector::DistSquared(Body,Upper+FVector(0,0,90))?Lower:Upper;}
    void Apply(FVector Position,uint8 State,uint32 NewRevision);
    void SetObserved(bool Visible);
};
UCLASS()
class LOCKDOWN_API ALDWorldObjects:public AActor
{
    GENERATED_BODY()
public:
    ALDWorldObjects();
    virtual void Tick(float Delta) override;
    static ALDWorldObjects* Get(UWorld* World);
    bool Initialize(FString& Error);
    bool Ready=false;
    FString Contract;
    TArray<TWeakObjectPtr<ALDWorldObject>> Objects;
    ALDWorldObject* Find(FName SourceId) const;
    ALDWorldObject* Carried(const ALDB91Controller* PC) const;
    bool ForceDrop(ALDB91Controller* PC);
    bool Traversing(const ALDB91Controller* PC) const;
    void Begin(ALDB91Controller* PC,const FLDObjectRequest& Request);
    void Hold(ALDB91Controller* PC,uint32 Match,uint32 Life,uint32 Sequence,uint32 Nonce,bool Release);
    void Cancel(ALDB91Controller* PC,const FString& Why);
    void Publish();
    bool Visible(ALDB91Controller* PC,ALDWorldObject* Object) const;
    ALDWorldObject* Resolve(ALDB91Controller* PC,FGuid Handle) const;
    bool Placement(ALDWorldObject* Object,FVector Point,FVector& Surface) const;
    uint32 Committed=0;
    // Explicit physical source census, also used by the native conservation probe.
    bool Conserved(FString& Error) const;
    void RecordConsumption(ALDItem* Item);
private:
    friend class ALDB91ObjectsProbe;
    friend class ALDB91TraversalProbe;
    struct FReceipt{FGuid Id;uint8 Result=0;uint32 Revision=0;FString Why;};
    struct FClient
    {
        uint32 Last=0,Cancelled=0;
        double At=0,Tokens=8;
        TMap<uint64,FReceipt> Receipts;
        TMap<TWeakObjectPtr<ALDWorldObject>,FGuid> Handles;
        TMap<TWeakObjectPtr<ALDWorldObject>,uint32> Opened;
        TSet<FGuid> Visible;
        TMap<FGuid,FLDObjectView> LastSeen;
    };
    struct FHold
    {
        FLDObjectRequest Request;
        TWeakObjectPtr<ALDWorldObject> Object;
        TWeakObjectPtr<ALDWorldObject> Link;
        TWeakObjectPtr<ALDCharacter> Body;
        FVector Start,Destination,PushStart;
        FVector Via;
        double Started=0,Ends=0,Heartbeat=0,Damage=0;
        uint32 Nonce=0;
    };
    struct FCargo{TWeakObjectPtr<ALDWorldObject> Object;uint32 Life=0;double Damage=0;};
    TMap<TWeakObjectPtr<ALDB91Controller>,FClient> Clients;
    TMap<TWeakObjectPtr<ALDB91Controller>,FHold> Holds;
    TMap<TWeakObjectPtr<ALDB91Controller>,FHold> Returns;
    TMap<TWeakObjectPtr<ALDB91Controller>,FCargo> Cargo;
    TMap<FGuid,TWeakObjectPtr<ALDItem>> Finite;
    TSet<FGuid> Consumed;
    uint64 ViewRevision=0;
    double NextPublish=0;
    double NextFuelPublish=0;
    bool FitSource(ALDWorldObject* Object,FVector Source,FVector& Surface) const;
    bool InitializeTools(FString& Error);
    bool InitializeTraversal(FString& Error);
    bool LinkEligible(ALDB91Controller* PC,ALDWorldObject* Object,const FLDObjectRequest& R,FString& Why) const;
    bool BeginLink(ALDB91Controller* PC,ALDWorldObject* Object,FHold& Hold,FString& Why);
    bool CommitLink(ALDB91Controller* PC,ALDWorldObject* Object,const FHold& Hold,FString& Why);
    bool MoveTraversal(ALDB91Controller* PC,const FHold& Hold,double Now,FString& Why);
    void EndTraversal(ALDB91Controller* PC,const FHold& Hold,bool Success);
    bool EndpointClear(ALDB91Controller* PC,FVector Feet) const;
    bool RestoreTraversal(ALDB91Controller* PC,const FHold& Hold,bool Success);
    void TickTools(double Now);
    bool ToggleTool(ALDB91Controller* PC,const FLDObjectRequest& Request,FString& Why);
    bool SurfaceAt(FVector Point,FVector& Surface) const;
    bool Reserved(FVector Surface,FVector Size) const;
    bool Eligible(ALDB91Controller* PC,ALDWorldObject* Object,const FLDObjectRequest& R,FString& Why) const;
    bool Commit(ALDB91Controller* PC,ALDWorldObject* Object,const FHold& Hold,FString& Why);
    void Reply(ALDB91Controller* PC,const FLDObjectRequest& Request,uint8 Result,const FString& Why,uint32 Revision=0);
    void Quote(ALDB91Controller* PC,ALDWorldObject* Object);
    ALDWorldObject* Wrap(ALDItem* Item,FVector Surface);
    void DiscoverDrops();
};
