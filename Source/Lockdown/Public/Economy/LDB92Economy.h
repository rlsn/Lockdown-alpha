#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Economy/LDEconomyCatalog.h"
#include "Economy/LDEconomyLedger.h"
#include "LDB92Economy.generated.h"
struct FLDB92CourierView;

// Authority domain, intentionally not a client payload. Physical stock remains
// in institutional custody until an admitted service projects it to a real bag.
struct FLDB92MarketQuote
{
    FName Market;
    uint64 Revision=0;
    int64 Cash=0;
    struct FGood {ELDItemKind Kind;int32 Stock,Cap,Ask,Bid;};
    TArray<FGood> Goods;
};
struct FLDB92Trade
{
    FGuid Request,Contender,Item;
    FName Market;
    ELDItemKind Kind=ELDItemKind::Fiber;
    bool Buy=true;
    uint64 QuoteRevision=0;
    uint32 WalletRevision=0,ItemRevision=0,InventoryRevision=0;
    int32 Price=0;
    bool operator==(const FLDB92Trade& B) const;
};
struct FLDB92InstitutionEvent
{
    int32 At=0,Row=0;
    FName Status;
    FGuid Receipt,Item;
};

UCLASS()
class LOCKDOWN_API ALDB92Economy:public AActor
{
    GENERATED_BODY()
public:
    ALDB92Economy();
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    // IDs are supplied by the eventual roster authority. No identity is inferred
    // from a controller count, body respawn, or the selected human/AI split.
    bool Initialize(FGuid Match,const TArray<FGuid>& Contenders,const TArray<FGuid>& Civilians,FString& Error,bool IncludeWorld=false);
    FLDB92MarketQuote Quote(FName Market) const;
    bool Trade(const FLDB92Trade& Request,FGuid& Received,FString& Error);
    bool ExtractDeposit(class ALDB92Controller* PC,int32 Node,FString& Error,bool Survey=false);
    static FName BookBudget(ELDSkill Skill);
    uint32 OfficialRevision=1;
    uint32 WorkRevision=1;
    FString ExportCouriers() const;
    static TArray<FName> CourierBoards(const UWorld* World);
    static int32 CourierDistance(const UWorld* World,FName From,FName To);
    static FName CourierBudget(FGuid Id,bool Issuance);
    FLDB92CourierView CourierView(class ALDB92Controller* PC,FName Board) const;
    bool CourierTerms(class ALDB92Controller* PC,FName POI,const struct FLDB92MarketOrder& Order,bool Reserved,FString& Error) const;
    bool CourierCommand(class ALDB92Controller* PC,FName POI,const struct FLDB92MarketOrder& Order,FGuid Receipt,FString& Error,bool Reserved=false);
    static FName WorkBudget(FGuid Id,uint8 Recipe);
    bool WorkTerms(class ALDB92Controller* PC,FName POI,const struct FLDB92MarketOrder& Order,bool Reserved,FString& Error) const;
    bool CommitWork(class ALDB92Controller* PC,FName POI,const struct FLDB92MarketOrder& Order,FGuid Receipt,FString& Error);
    bool PurchaseOfficial(class ALDB92Controller* PC,FName POI,FGuid Receipt,const struct FLDB92MarketOrder& Order,FString& Error);
    // Called by one authority clock, never by clients. Missed intervals are
    // discarded rather than compressed into a production burst on wake/reload.
    bool Sample(double Elapsed,FString& Error);
    bool Audit(FString& Error) const;
    bool SynchronizeWorld(FString& Error);
    void RecordConsumption(class ALDItem* Item);
    bool HasWorldLedger() const {return FullWorld;}
    const LDEconomy::FLedger& Accounting() const {return Ledger;}
    const LDEconomy::FCatalog& Rules() const {return Catalog;}
    const TArray<FLDB92InstitutionEvent>& Timeline() const {return Events;}
    static FName Wallet(FGuid Id);
    static FName Bank(FGuid Id);
private:
    friend class ALDB92EconomyProbe;
    friend class ALDB92Roster;
    friend class ALDB92RosterProbe;
    friend class ALDB92Services;
    friend class ALDB92ServicesProbe;
    bool Ready=false,Closed=false;
    bool FullWorld=false;
    struct FCourierJob {FGuid Contract,Token,Owner,Terminal;FName Origin,Destination;int32 Distance=0;uint32 Revision=1;bool Active=false;};
    TMap<FGuid,FCourierJob> Couriers;
    TMap<FGuid,FCourierJob> CourierArchive;
    TSet<FGuid> ConsumedWorld;
    FName PhysicalLocation(const ALDItem* Item,FString& Error) const;
    bool ImportWorld(LDEconomy::FLedger& Candidate,TArray<TObjectPtr<ALDItem>>& Items,FString& Error);
    int32 Cursor=0;
    double LastSample=0;
    FGuid Schedule;
    TSet<FGuid> Seats;
    LDEconomy::FCatalog Catalog;
    LDEconomy::FLedger Ledger;
    UPROPERTY() TArray<TObjectPtr<class ALDItem>> Custody;
    TMap<FName,uint32> MarketRevisions;
    struct FReplay {FLDB92Trade Request;FGuid Received;};
    TMap<FGuid,FReplay> Replays;
    TArray<FLDB92InstitutionEvent> Events;
    TArray<ALDItem*> Stock(FName Market,ELDItemKind Kind) const;
    FName MarketFor(uint8 Service) const;
    bool ApplyInstitution(int32 Tick,int32 Row,FString& Error);
};
