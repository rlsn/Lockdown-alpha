#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Economy/LDEconomyLedger.h"
#include "LDEconomyDirector.generated.h"
class ALDPlayerController;

UCLASS()
class LOCKDOWN_API ALDEconomyDirector:public AActor
{
    GENERATED_BODY()
public:
    ALDEconomyDirector(){bReplicates=false;}
    bool Record(FName Cause,int64 Mint=0,int64 Burn=0,const TArray<FGuid>& Sinks={});
    bool Audit(FString& Error) const;
    bool Export(FName Event,FString& Error) const;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    uint32 AuditFailures=0;
    void Retire(ALDPlayerController* PC);
    static FName Wallet(FGuid Id);
    const LDEconomy::FLedger& Accounting() const {return Ledger;}
private:
    struct FPhysical {ELDItemKind Kind;FName Location;ALDItem* Actor=nullptr;};
    bool Snapshot(TMap<FGuid,FPhysical>& Out,FString& Error) const;
    bool Initialized=false;
    LDEconomy::FLedger Ledger;
};
