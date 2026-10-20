#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Development/LDCharacterBuild.h"
#include "LDDevelopmentDirector.generated.h"
class ALDPlayerController;

namespace LDDevelopmentTransaction
{
    constexpr uint8 Buy=1,Read=2,Prepare=3,PickUp=4;
    constexpr int64 BookPrice=40;
    constexpr int32 BookCapacity=4;
    LOCKDOWN_API bool Sold(ELDSkill Skill);
}

// Canonical physical instance. Only dropped sanitized poses enter observations.
UCLASS()
class LOCKDOWN_API ALDBook : public AActor
{
    GENERATED_BODY()
public:
    ALDBook();
    FGuid Identity=FGuid::NewGuid();
    ELDSkill Skill=ELDSkill::None;
    uint8 State=1; // owned, dropped=0; consumed instances are destroyed
    TWeakObjectPtr<ALDPlayerController> Holder;
};

UCLASS()
class LOCKDOWN_API ALDDevelopmentDirector : public AActor
{
    GENERATED_BODY()
public:
    ALDDevelopmentDirector();
    void Queue(ALDPlayerController* PC,uint32 Match,uint32 Life,uint32 Request,uint8 Kind,ELDSkill Skill,FGuid Item,const FLDBuildPlan& Plan);
    void RefreshHold(ALDPlayerController* PC,uint32 Life,uint32 Token,uint32 Sequence,bool Moving);
    void Release(ALDPlayerController* PC,uint32 Life,uint32 Token);
    void Cancel(ALDPlayerController* PC,const FString& Reason);
    void DropBooks(ALDPlayerController* PC,const FVector& Position);
    void Sample(double Now);
    ALDBook* OwnedBook(ALDPlayerController* PC,const FGuid& Item) const;
    bool NearStation(ALDPlayerController* PC,bool GuildOnly=false) const;
    int32 QueuedCount() const {return Requests.Num();}
private:
    struct FRequest {TWeakObjectPtr<ALDPlayerController> PC;uint32 Match,Life,Id;uint8 Kind;ELDSkill Skill;FGuid Item;FLDBuildPlan Plan;};
    struct FState
    {
        uint32 LastRequest=0,CancelledThrough=0,Token=0,Life=0,Match=0,Sequence=0;
        uint8 Kind=0;
        double Tokens=3,RateAt=-1,LastHold=-1000,EndsAt=0;
        FVector Anchor=FVector::ZeroVector;
        TWeakObjectPtr<ALDBook> Book;
        FLDBuildPlan Plan;
        TMap<uint32,FString> Receipts;
    };
    TArray<FRequest> Requests;
    TMap<TWeakObjectPtr<ALDPlayerController>,FState> States;
    bool Eligible(ALDPlayerController* PC,double Now,bool Quiet) const;
    bool Stationary(ALDPlayerController* PC) const;
    void FinishRequest(ALDPlayerController* PC,uint32 Request,const FString& Reason);
    void Request(const FRequest& Request,double Now);
};
