#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Inventory/LDTradeTypes.h"
#include "LDTradeDirector.generated.h"
class ALDPlayerController;
UCLASS()
class LOCKDOWN_API ALDTradeDirector:public AActor
{
    GENERATED_BODY()
public:
    ALDTradeDirector(){bReplicates=false;}
    void Queue(ALDPlayerController* PC,uint32 Match,uint32 Life,uint32 Request,uint8 Command,FGuid Session,uint32 Revision,const FString& Digest,FGuid Item,int64 Gold);
    void Sample(double Now);
    void CancelFor(ALDPlayerController* PC,const FString& Reason=TEXT("Cancelled: another action started"));
    bool HasSession(ALDPlayerController* PC) const;
private:
    friend class ALDTradeProbe;
    struct FOfferItem{FGuid Id,Handle;uint32 Revision;ELDItemKind Kind;ELDSkill Skill;};
    struct FSide{TWeakObjectPtr<ALDPlayerController> PC;TWeakObjectPtr<APawn> Body;uint32 Match=0,Life=0,Inventory=0;int64 Balance=0,Gold=0;TArray<FOfferItem> Items;bool Ready=false,Confirmed=false;};
    struct FSession{FGuid Id;FSide Sides[2];uint32 Revision=1;FString Digest;ELDTradeStage Stage=ELDTradeStage::Invited;double Started=0,Changed=0;};
    struct FReceipt{FGuid Session;FString Message;};
    struct FClient{uint32 Last=0,CancelledThrough=0,Serial=0;double At=0,Tokens=4;TMap<uint32,FReceipt> Receipts;TMap<FGuid,FLDTradeView> Terminal;TArray<FGuid> TerminalOrder;TMap<FGuid,double> Attempts;};
    struct FRequest{TWeakObjectPtr<ALDPlayerController> PC;uint32 Match,Life,Id;uint8 Command;FGuid Session;uint32 Revision;FString Digest;FGuid Item;int64 Gold;double Due;};
    TMap<FGuid,FSession> Sessions;
    TMap<TWeakObjectPtr<ALDPlayerController>,FClient> Clients;
    TArray<FRequest> Requests;
    FSession* Find(ALDPlayerController* PC);
    bool Eligible(const FSession& S) const;
    bool PairEligible(ALDPlayerController* A,ALDPlayerController* B) const;
    bool Project(const FSession& S,int32 Side) const;
    void Revision(FSession& S,bool Activity);
    void Refresh(FSession& S);
    void Reserve(FSide& Side,const FOfferItem& Item,bool Value);
    FLDTradeView View(const FSession& S,int32 Side) const;
    void Send(ALDPlayerController* PC,FLDTradeView View,uint32 Request=0);
    void Push(FSession& S);
    void Finish(FGuid Session,ELDTradeStage Stage,const FString& Reason);
    void Reply(ALDPlayerController* PC,uint32 Request,FGuid Session,const FString& Message);
    void Request(const FRequest& R);
    void Commit(FGuid Session);
};
