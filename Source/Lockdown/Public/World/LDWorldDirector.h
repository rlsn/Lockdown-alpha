#pragma once
#include "CoreMinimal.h"
#include "Character/LDCharacter.h"
#include "Engine/DataAsset.h"
#include "Combat/LDCombatDirector.h"
#include "Vision/LDVisionDirector.h"
#include "LDWorldDirector.generated.h"
UCLASS()
class LOCKDOWN_API ULDWorldCatalog:public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY() FString SourceJson;
    UPROPERTY() FString Checksum;
    bool Import(const FString& Text,FString& Error);
    bool Validate(FString& Error) const;
    static ULDWorldCatalog* Load();
    FVector Position(const FString& Name) const;
};
UCLASS()
class LOCKDOWN_API ALDWorldPawn:public ALDCharacter
{
    GENERATED_BODY()
public:
    ALDWorldPawn(const FObjectInitializer& Initializer);
    uint8 Kind=0,Index=0; // camp / guardian / tower
    FGuid SpawnId=FGuid::NewGuid(),LastOwner;
    double Maximum=260,Armor=5,MR=.15,AttackDamage=35,Interval=1.5,NextAttack=0,NextPulse=0,PulseAt=0;
    FVector Home=FVector::ZeroVector,PulsePoint=FVector::ZeroVector;
    TWeakObjectPtr<ALDPlayerController> LastHitter;
    TWeakObjectPtr<ALDCharacter> Target;
    bool Paid=false;
    bool Immune() const{return Kind!=0;} // displacement / explicit hard interruption
    bool ImmuneTo(LDTactical::EStatus Status) const{return Immune()&&(Status==LDTactical::EStatus::Stun||Status==LDTactical::EStatus::Hex||Status==LDTactical::EStatus::Break||Status==LDTactical::EStatus::Silence||Status==LDTactical::EStatus::Mute);}
};
UCLASS()
class LOCKDOWN_API ALDWorldSite:public AActor
{
    GENERATED_BODY()
public:
    ALDWorldSite(){bReplicates=false;RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));}
    uint8 Kind=0;
    FGuid Id=FGuid::NewGuid();
};
UCLASS()
class LOCKDOWN_API ALDWorldDirector:public AActor
{
    GENERATED_BODY()
public:
    ALDWorldDirector(){bReplicates=false;}
    void Initialize();
    void Prepare(double Now,TArray<FLDCombatHit>& Hits);
    void Contact(const FLDCombatHit& Hit,double Amount);
    bool Defeated(ALDWorldPawn* Pawn,double Now);
    void EndLife(ALDPlayerController* PC);
    void InventoryChanged(ALDPlayerController* PC);
    uint8 Authorization(ALDPlayerController* PC,double At,double Through) const;
    bool CanActivate(ALDPlayerController* PC,uint8 Route,double End,FString& Why) const;
    bool Activate(ALDPlayerController* PC,uint8 Route,double At,FString& Why);
    void Reserve(ALDPlayerController* PC,uint8 Route,bool Value);
    bool SelectKey(ALDPlayerController* PC,ALDItem* Item,FString& Why);
    ALDItem* KeyTarget(ALDPlayerController* PC) const;
    double NextEvent(double After,double Through) const;
    FVector Position(const FString& Name) const;
    void Publish(double Now);
    void PvPKill(ALDCharacter* Victim,double Now);
    TArray<FLDSightSource> TowerSight() const;
    bool bReady=false;
    TArray<TWeakObjectPtr<ALDWorldPawn>> Pawns;
private:
    friend class ALDWorldProbe;
    struct FLease{FGuid Id;uint8 Route=0;uint32 Life=0;double Open=0,Close=0;TArray<FGuid> Keys;};
    TMap<TWeakObjectPtr<ALDPlayerController>,FLease> Leases;
    struct FPending{uint8 Route=0;uint32 Build=0,Inventory=0;TArray<FLDInventoryItem> Items;TWeakObjectPtr<ALDItem> Key;uint32 KeyRevision=0;};
    TMap<TWeakObjectPtr<ALDPlayerController>,FPending> Pending;
    TMap<TWeakObjectPtr<ALDPlayerController>,TWeakObjectPtr<ALDItem>> KeyCandidates;
    struct FWindow{FGuid Id;double Open=0,Close=0;};
    TArray<FWindow> CharterHistory;
    FGuid CampIds[3][12];
    struct FTraversal{FVector Position=FVector::ZeroVector;double At=0;uint32 Life=0;bool Eligible=false;};
    TMap<FGuid,FTraversal> Traversal;
    TMap<FGuid,int32> AttributedXP;
    int32 CampCursor[3]={0,0,0};
    double CampNext[3]={0,0,0};
    TMap<FGuid,FGuid> LifeOwners;
    TMap<FGuid,TWeakObjectPtr<ALDPlayerController>> Owners;
    TMap<FGuid,FGuid> LastDamageOwner;
    TMap<FString,double> PvPReady;
    TMap<FGuid,int32> PvPEarned;
    TMap<FGuid,TSet<uint8>> Discoveries;
    ALDWorldPawn* Spawn(uint8 Kind,uint8 Index,double At);
    bool Reward(ALDWorldPawn* Pawn,int32 Gold,int32 XP,FName Cause,FGuid& Receipt);
};
