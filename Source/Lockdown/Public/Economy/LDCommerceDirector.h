#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Inventory/LDInventoryTypes.h"
#include "LDCommerceDirector.generated.h"
class ALDPlayerController;
USTRUCT()
struct FLDCommerceOffer
{
    GENERATED_BODY()
    UPROPERTY() ELDItemKind Kind=ELDItemKind::Fiber;
    UPROPERTY() int32 Stock=0;
    UPROPERTY() int32 Cap=0;
    UPROPERTY() int32 Ask=0;
    UPROPERTY() int32 Bid=0;
};
USTRUCT()
struct FLDCommerceView
{
    GENERATED_BODY()
    UPROPERTY() FGuid Session;
    UPROPERTY() uint32 Token=0;
    UPROPERTY() uint32 Match=0;
    UPROPERTY() uint32 Life=0;
    UPROPERTY() uint32 Revision=0;
    UPROPERTY() uint8 Service=0; // 0 official materials, 1 City, 2 West, 3 East
    UPROPERTY() int64 Cash=0;
    UPROPERTY() double QuotedAt=0;
    UPROPERTY() TArray<FLDCommerceOffer> Offers;
};
UCLASS()
class LOCKDOWN_API ALDCommerceDirector:public AActor
{
    GENERATED_BODY()
public:
    ALDCommerceDirector(){bReplicates=false;}
    static FVector Position(const UWorld* World,uint8 Service);
    static FString ServiceName(uint8 Service);
    bool Eligible(ALDPlayerController* PC,uint8 Service) const;
    void Open(ALDPlayerController* PC,uint32 Match,uint32 Life,uint32 Token,uint8 Service);
    void Close(ALDPlayerController* PC,uint32 Token);
    void Invalidate(ALDPlayerController* PC);
    void Queue(ALDPlayerController* PC,uint32 Match,uint32 Life,uint32 Request,FGuid Session,uint32 QuoteRevision,uint8 Command,ELDItemKind Kind,FGuid Item,uint32 ItemRevision,uint32 InventoryRevision,int32 Price);
    void Sample(double Now);
private:
    friend class ALDCharterProbe;
    friend class ALDCommerceProbe;
    friend class ALDInstitutionProbe;
    struct FClient {FLDCommerceView View;uint32 LastToken=0,ClosedThrough=0,LastRequest=0;double At=0,Tokens=4,Damage=-1,Dealt=-1;TMap<uint32,FString> Receipts;};
    struct FRequest {TWeakObjectPtr<ALDPlayerController> PC;uint32 Match,Life,Id;FGuid Session;uint32 QuoteRevision;uint8 Command;ELDItemKind Kind;FGuid Item;uint32 ItemRevision,InventoryRevision;int32 Price;};
    TMap<TWeakObjectPtr<ALDPlayerController>,FClient> Clients;
    TArray<FRequest> Requests;
    bool TakeToken(FClient& C);
    bool Valid(ALDPlayerController* PC,const FClient& C) const;
    void Reply(ALDPlayerController* PC,uint32 Request,const FString& Reason);
};
