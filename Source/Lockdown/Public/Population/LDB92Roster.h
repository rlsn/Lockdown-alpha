#pragma once
#include "CoreMinimal.h"
#include "World/LDEnvironmentDirector.h"
#include "Economy/LDB92Services.h"
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
    virtual void PlayerTick(float Delta) override;
    virtual bool PointerService(FVector Point) override;
    UFUNCTION(Client,Reliable) void ClientB92Admission(const FLDOwnerState& State,uint32 CombatFloor);
    UFUNCTION(Server,Reliable) void ServerB92Open(uint32 Match,uint32 Life,uint32 Sequence,FName POI);
    UFUNCTION(Server,Reliable) void ServerB92Order(FLDB92MarketOrder Order);
    UFUNCTION(Server,Reliable) void ServerB92Close();
    UFUNCTION(Client,Reliable) void ClientB92Market(const FLDOwnerState& Snapshot,const FLDB92MarketView& Terms,uint32 Sequence,FGuid Receipt,bool Accepted,const FString& Message);
    void OpenMarket(FName POI);
    FLDB92MarketOrder MarketOrder(bool Buy,ELDItemKind Kind,FGuid Item=FGuid());
    void SubmitMarket(bool Buy,ELDItemKind Kind);
    FLDB92MarketView MarketView;
    uint32 MarketSequence=0,MarketReceiptSequence=0,MarketReceiptSerial=0;
    FGuid MarketReceipt;
    bool MarketAccepted=false;
    FString MarketMessage;
    TArray<TWeakObjectPtr<AActor>> MarketMarkers;
    FName PendingMarket;
    uint32 PendingMarketOrder=0;
    double PendingMarketAt=0;
    UFUNCTION(Server,Reliable) void ServerB92ProbeHello(uint8 ClientRole);
    UFUNCTION(Server,Reliable) void ServerB92ProbeReport(uint8 Task,const FString& Report);
    UFUNCTION(Client,Reliable) void ClientB92ProbeTask(uint8 Task,FName POI);
    void TickServicesProbe();
    uint8 ServicesProbeRole=0,ServicesProbeTask=0,ServicesProbeStep=0;
    FName ServicesProbePOI;
    double ServicesProbeAt=0;
    uint32 ServicesProbeSequence=0,ServicesProbeSerial=0;
    FGuid ServicesProbeBought;
    TArray<FGuid> ServicesProbeBefore;
    FLDB92MarketOrder ServicesProbeSaved;
    TMap<uint8,FString> ServicesProbeReports;
    TMap<uint32,FGuid> ServicesProbeReceipts;
    TMap<uint32,int32> ServicesProbeReceiptCounts;
    bool ServicesProbeReplaySame=true;
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
class LOCKDOWN_API ALDB92HUD:public ALDB91HUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
    virtual void NotifyHitBoxClick(FName Name) override;
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
    UPROPERTY() TObjectPtr<ALDB92Services> Services;
private:
    friend class ALDB92RosterProbe;
    friend class ALDB92ServicesProbe;
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
