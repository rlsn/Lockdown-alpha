#pragma once
#include "CoreMinimal.h"
#include "Inventory/LDInventoryTypes.h"

// Authority-only accounting domain. No UObject replication or client-computed amounts.
namespace LDEconomy
{
    constexpr int64 MaxBalance = MAX_int64;
    struct FAccount { int64 Gold=0; uint32 Revision=1; bool Retired=false; };
    struct FItem
    {
        ELDItemKind Kind=ELDItemKind::Fiber;
        FName Location;
        uint32 Revision=1;
        FGuid Origin;
        TArray<FGuid> Parents;
        bool Consumed=false;
        uint8 Quality=0;
        uint8 Skill=0;
    };
    struct FGoldDelta { FName Account; uint32 Expected=0; int64 Delta=0; };
    enum class EItemOp:uint8 { Create, Transfer, Consume, Quality, Transient };
    struct FItemDelta
    {
        FGuid Id;
        EItemOp Op=EItemOp::Transfer;
        ELDItemKind Kind=ELDItemKind::Fiber;
        uint32 Expected=0;
        uint8 Quality=0;
        uint8 Skill=0;
        FName From,To;
        TArray<FGuid> Parents;
    };
    struct FBudgetDelta { FName Id; int32 Units=0; };
    struct FTransaction
    {
        FGuid Id;
        FName Cause;
        int64 Mint=0,Burn=0;
        TArray<FGoldDelta> Gold;
        TArray<FItemDelta> Items;
        // Optional finite budget. Rewards and extraction share this one commit boundary.
        FName Budget;
        int32 Units=0;
        TArray<FBudgetDelta> Budgets; // additional bounds validated and debited atomically
    };
    struct FReceipt { FTransaction Transaction; uint64 Serial=0; };
    class LOCKDOWN_API FLedger
    {
    public:
        // A ledger belongs to exactly one match. Reset creates a new domain, not a refill.
        bool Initialize(FGuid Match,const TMap<FName,int64>& Endowments,const TMap<FName,int32>& Budgets,FString& Error);
        bool OpenAccount(FName Id); // zero only; existing balances never reset
        bool EndowAccount(FName Id,int64 Gold); // new lazy resident purse only, counted once
        bool Retire(FName Id); // retained in all aggregate totals
        bool Commit(const FTransaction& Transaction,FString& Error);
        bool Reconcile(FString& Error) const;
        const FAccount* Account(FName Id) const {return Accounts.Find(Id);}
        const FItem* Item(FGuid Id) const {return Items.Find(Id);}
        const FReceipt* Receipt(FGuid Id) const {return Receipts.Find(Id);}
        int32 Remaining(FName Id) const {const auto* N=Budgets.Find(Id);return N?*N:-1;}
        const TMap<FGuid,FItem>& ItemRecords() const {return Items;}
        const TMap<FName,FAccount>& AccountRecords() const {return Accounts;}
        FString ExportJson(FName Event) const;
        int64 TotalGold() const;
        int64 Minted() const {return Mints;}
        int64 Burned() const {return Burns;}
        int32 ReceiptCount() const {return Receipts.Num();}
    private:
        FGuid Match;
        int64 Start=0,Mints=0,Burns=0;
        TMap<FName,FAccount> Accounts;
        TMap<FName,int64> InitialAccounts;
        TMap<FName,int32> InitialBudgets;
        TMap<FGuid,FItem> Items;
        TMap<FName,int32> Budgets;
        TMap<FGuid,FReceipt> Receipts;
    };
    LOCKDOWN_API int64 DepositFee(int64 Gross);
    // Pure bank transaction builder; physical access/session checks belong to the service.
    LOCKDOWN_API bool BankTransaction(const FLedger& Ledger,FGuid Request,FName Wallet,FName Bank,
        uint32 WalletRevision,uint32 BankRevision,int64 Amount,bool Deposit,FTransaction& Out,FString& Error);
}
