#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LDBankDirector.generated.h"
class ALDPlayerController;
USTRUCT()
struct FLDBankView
{
    GENERATED_BODY()
    UPROPERTY() FGuid Session;
    UPROPERTY() uint32 Token=0;
    UPROPERTY() uint32 Match=0;
    UPROPERTY() uint32 Life=0;
    UPROPERTY() uint32 WalletRevision=0;
    UPROPERTY() uint32 BankRevision=0;
    UPROPERTY() int64 Wallet=0;
    UPROPERTY() int64 Bank=0;
};
UCLASS()
class LOCKDOWN_API ALDBankDirector:public AActor
{
    GENERATED_BODY()
public:
    ALDBankDirector(){bReplicates=false;}
    bool Eligible(ALDPlayerController* PC) const;
    void Open(ALDPlayerController* PC,uint32 Match,uint32 Life,uint32 Token);
    void Close(ALDPlayerController* PC,uint32 Token);
    void Invalidate(ALDPlayerController* PC);
    void Queue(ALDPlayerController* PC,uint32 Match,uint32 Life,uint32 Request,FGuid Session,int64 Amount,bool Deposit,uint32 WR,uint32 BR,bool Charter=false);
    void Sample(double Now);
private:
    friend class ALDCharterProbe;
    friend class ALDEconomyProbe;
    struct FClient
    {
        FLDBankView View;
        uint32 LastToken=0,ClosedThrough=0,LastRequest=0;
        double At=0,Tokens=4,Damage=-1,Dealt=-1;
        TMap<uint32,FString> Receipts;
    };
    struct FRequest {TWeakObjectPtr<ALDPlayerController> PC;uint32 Match,Life,Id;FGuid Session;int64 Amount;bool Deposit;uint32 WR,BR;bool Charter=false;};
    TMap<TWeakObjectPtr<ALDPlayerController>,FClient> Clients;
    TArray<FRequest> Requests;
    bool TakeToken(FClient& C);
    bool Valid(ALDPlayerController* PC,const FClient& C) const;
    FLDBankView View(ALDPlayerController* PC,const FClient& C) const;
    void Reply(ALDPlayerController* PC,uint32 Request,const FString& Reason);
};
