#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Inventory/LDVendorTypes.h"
#include "LDVendorDirector.generated.h"
class ALDPlayerController;
UCLASS(Config=Game)
class LOCKDOWN_API ALDVendorDirector:public AActor
{
    GENERATED_BODY()
public:
    ALDVendorDirector(){bReplicates=false;}
    UPROPERTY(Config) int32 BookPrice=40;
    UPROPERTY(Config) uint32 CatalogRevision=1;
    bool Eligible(ALDPlayerController* PC) const;
    void Open(ALDPlayerController* PC,uint32 Match,uint32 Life,uint32 Token);
    void Close(ALDPlayerController* PC,uint32 Token);
    void Queue(ALDPlayerController* PC,uint32 Match,uint32 Life,uint32 Request,FGuid Session,uint32 QuoteRevision,int32 Price,uint32 InventoryRevision,ELDSkill Skill);
    void Sample(double Now);
    void FailNextCreationForProbe();
private:
    friend class ALDVendorProbe;
    struct FReceipt{FGuid Item;FString Reason;bool bFinal=true;};
    struct FClient{uint32 Token=0,ClosedThrough=0,Match=0,Life=0,LastRequest=0,SentRevision=0;int32 SentPrice=0;FGuid Session;double Damage=-1,At=0,Tokens=4;TMap<uint32,FReceipt> Receipts;};
    struct FRequest{TWeakObjectPtr<ALDPlayerController> PC;uint32 Match,Life,Id;FGuid Session;uint32 QuoteRevision;int32 Price;uint32 InventoryRevision;ELDSkill Skill;};
    TMap<TWeakObjectPtr<ALDPlayerController>,FClient> Clients;
    TArray<FRequest> Requests;
    bool bFailCreation=false;
    FLDVendorQuote Quote(ALDPlayerController* PC,const FClient& Client) const;
    bool ValidSession(ALDPlayerController* PC,const FClient& Client) const;
    bool TakeToken(FClient& Client);
    void Request(const FRequest& Request);
    void Reply(ALDPlayerController* PC,uint32 Request,const FReceipt& Receipt);
};
