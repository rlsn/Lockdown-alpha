#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Economy/LDEconomyLedger.h"
#include "Economy/LDEconomyCatalog.h"
#include "Economy/LDCommerceDirector.h"
#include "LDEconomyDirector.generated.h"
class ALDPlayerController;

UCLASS()
class LOCKDOWN_API ALDEconomyDirector:public AActor
{
    GENERATED_BODY()
public:
    ALDEconomyDirector(){bReplicates=false;}
    bool Initialize(FString& Error);
    void GoldChanged(FName Cause,int64 Mint=0,int64 Burn=0);
    bool Record(FName Cause,int64 Mint=0,int64 Burn=0,const TArray<FGuid>& Sinks={});
    bool Audit(FString& Error) const;
    bool Export(FName Event,FString& Error) const;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    bool AllowDiagnosticItems() const;
    uint32 AuditFailures=0;
    void Retire(ALDPlayerController* PC);
    void ResidentEndowment(FGuid Id,int64 Gold);
    static FName Wallet(FGuid Id);
    static FName BankAccount(FGuid Id);
    bool CommitBank(ALDPlayerController* PC,int64 Amount,bool Deposit,uint32 WalletRevision,uint32 BankRevision,FString& Error);
    static FName MarketId(uint8 Service);
    FLDCommerceView CommerceQuote(uint8 Service) const;
    bool CommitCommerce(ALDPlayerController* PC,uint8 Service,uint32 QuoteRevision,uint8 Command,ELDItemKind Kind,FGuid Item,uint32 ItemRevision,uint32 InventoryRevision,int32 Price,FGuid& Received,FString& Error);
    bool CommitProduction(ALDPlayerController* PC,uint8 Site,uint8 Recipe,const TArray<FLDInventoryItem>& Inputs,int32 Fee,FGuid& Output,FString& Error);
    const LDEconomy::FLedger& Accounting() const {return Ledger;}
    const LDEconomy::FCatalog& Rules() const {return Catalog;}
private:
    friend class ALDCommerceProbe;
    friend class ALDProductionProbe;
    struct FPhysical {ELDItemKind Kind;FName Location;ALDItem* Actor=nullptr;};
    bool Snapshot(TMap<FGuid,FPhysical>& Out,FString& Error) const;
    TMap<uint8,uint32> MarketRevisions;
    TArray<ALDItem*> MarketItems(uint8 Service,ELDItemKind Kind) const;
    bool bFailNextSupply=false,bFailNextProduction=false;
    bool Initialized=false;
    LDEconomy::FCatalog Catalog;
    LDEconomy::FLedger Ledger;
    UPROPERTY() TArray<TObjectPtr<ALDItem>> InstitutionalItems;
};
