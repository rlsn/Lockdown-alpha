#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Inventory/LDInventoryTypes.h"
#include "LDB92Services.generated.h"
class ALDB92Controller;
USTRUCT()
struct FLDB92Offer
{
    GENERATED_BODY()
    UPROPERTY() ELDItemKind Kind=ELDItemKind::Fiber;
    UPROPERTY() int32 Stock=0; UPROPERTY() int32 Ask=0; UPROPERTY() int32 Bid=0;
};
// Owner-only local counter quote. No roster, remote inventory or stock IDs.
USTRUCT()
struct FLDB92MarketView
{
    GENERATED_BODY()
    UPROPERTY() FGuid Session;
    UPROPERTY() FName POI;
    UPROPERTY() uint32 Match=0; UPROPERTY() uint32 Life=0; UPROPERTY() uint32 WalletRevision=0;
    UPROPERTY() uint64 Revision=0;
    UPROPERTY() uint32 BankRevision=0;
    UPROPERTY() TArray<FLDB92Offer> Offers;
};
USTRUCT()
struct FLDB92MarketOrder
{
    GENERATED_BODY()
    UPROPERTY() uint32 Match=0; UPROPERTY() uint32 Life=0; UPROPERTY() uint32 Sequence=0; UPROPERTY() uint32 InventoryRevision=0; UPROPERTY() uint32 ItemRevision=0; UPROPERTY() uint32 WalletRevision=0;
    UPROPERTY() FGuid Session; UPROPERTY() FGuid Item;
    UPROPERTY() uint64 Revision=0;
    UPROPERTY() ELDItemKind Kind=ELDItemKind::Fiber;
    UPROPERTY() bool Buy=true;
    UPROPERTY() int32 Price=0;
    // 0 commodity, 1 deposit, 2 withdraw. Amount is total wallet debit on deposit.
    UPROPERTY() uint8 BankAction=0;
    UPROPERTY() int64 Amount=0;
    UPROPERTY() uint32 BankRevision=0;
    bool operator==(const FLDB92MarketOrder& B) const;
};
UCLASS()
class LOCKDOWN_API ALDB92Services:public AActor
{
    GENERATED_BODY()
public:
    ALDB92Services();
    virtual void Tick(float Delta) override;
    static const TArray<FName>& Markets();
    static FVector Counter(const UWorld* World,FName POI);
    bool Eligible(ALDB92Controller* PC,FName POI) const;
    void Open(ALDB92Controller* PC,uint32 Match,uint32 Life,uint32 Sequence,FName POI);
    void Close(ALDB92Controller* PC);
    void Order(ALDB92Controller* PC,const FLDB92MarketOrder& Order);
private:
    struct FCached {FLDB92MarketOrder Order;FGuid Receipt;bool Accepted=false;FString Message;};
    struct FClient {FLDB92MarketView View;uint32 Last=0;double At=0,Tokens=4;TMap<uint32,FCached> Receipts;};
    TMap<TWeakObjectPtr<ALDB92Controller>,FClient> Clients;
    double Next=0;
    bool Token(FClient& Client);
    void Reply(ALDB92Controller* PC,uint32 Sequence,FGuid Receipt,bool Accepted,const FString& Message);
};
