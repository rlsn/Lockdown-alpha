#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Economy/LDEconomyLedger.h"
#include "Economy/LDEconomyCatalog.h"
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
    const LDEconomy::FLedger& Accounting() const {return Ledger;}
    const LDEconomy::FCatalog& Rules() const {return Catalog;}
private:
    struct FPhysical {ELDItemKind Kind;FName Location;ALDItem* Actor=nullptr;};
    bool Snapshot(TMap<FGuid,FPhysical>& Out,FString& Error) const;
    bool Initialized=false;
    LDEconomy::FCatalog Catalog;
    LDEconomy::FLedger Ledger;
    UPROPERTY() TArray<TObjectPtr<ALDItem>> InstitutionalItems;
};
