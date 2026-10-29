#pragma once
#include "CoreMinimal.h"
#include "World/LDEnvironmentDirector.h"
#include "LDB92Roster.generated.h"

enum class ELDB92Role:uint8 {Civilian,Traveler,Guard,Official,Hostile};
struct FLDB92Seat
{
    FGuid Id;
    TWeakObjectPtr<ALDPlayerController> Controller;
    TWeakObjectPtr<ALDCharacter> Body;
    FVector Start=FVector::ZeroVector;
    bool Human=false;
};
struct FLDB92Person
{
    FName Source;
    FGuid Id;
    ELDB92Role Role=ELDB92Role::Civilian;
    FVector Home=FVector::ZeroVector;
    TWeakObjectPtr<ALDCharacter> Body;
};

UCLASS()
class LOCKDOWN_API ALDB92Controller:public ALDB91Controller
{
    GENERATED_BODY()
public:
    virtual void PawnLeavingGame() override;
    virtual void Destroyed() override;
    UFUNCTION(Client,Reliable) void ClientB92Admission(const FLDOwnerState& State,uint32 CombatFloor);
};
// The same authority command/state interfaces, without a local UI, a connection
// or a claim to an implemented autonomous contender policy.
UCLASS()
class LOCKDOWN_API ALDB92SeatController:public ALDB92Controller
{
    GENERATED_BODY()
public:
    ALDB92SeatController();
    virtual bool IsLocalController() const override {return false;}
    virtual void BeginPlay() override;
};
UCLASS()
class LOCKDOWN_API ALDB92GameMode:public ALDB91GameMode
{
    GENERATED_BODY()
public:
    ALDB92GameMode();
};

UCLASS()
class LOCKDOWN_API ALDB92Roster:public AActor
{
    GENERATED_BODY()
public:
    ALDB92Roster();
    virtual void Tick(float Delta) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    bool Ready=false;
    bool Configure(int32 Humans,FString& Error);
    bool Admit(ALDPlayerController* Controller,FString& Error);
    bool Release(ALDPlayerController* Controller,FString& Error);
    bool CanAdmit() const;
    int32 Humans() const;
    int32 HumanLimit() const {return Limit;}
    const TArray<FLDB92Seat>& Seats() const {return Contenders;}
    const TArray<FLDB92Person>& People() const {return Population;}
    FVector StartFor(ALDPlayerController* Controller) const;
    bool Audit(FString& Error) const;
    bool RecordGold(FName Cause,int64 Mint,int64 Burn,FString& Error);
    UPROPERTY() TObjectPtr<class ALDB92Economy> Economy;
private:
    friend class ALDB92RosterProbe;
    int32 Limit=4;
    bool Configured=false,Initializing=false,Ending=false;
    FGuid Match;
    FString Failure;
    double Began=0;
    TArray<FLDB92Seat> Contenders;
    TArray<FLDB92Person> Population;
    TSet<TWeakObjectPtr<ALDPlayerController>> Pending;
    bool Initialize(FString& Error);
    bool Fit(FVector Source,const TArray<FVector>& Occupied,FVector& Out) const;
    bool Transfer(int32 Seat,ALDPlayerController* Controller,bool Human,FString& Error,ALDPlayerController* Previous=nullptr);
};
