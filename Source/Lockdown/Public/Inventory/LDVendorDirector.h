#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Development/LDGrowthProtocol.h"
#include "Development/LDGrowthRequests.h"
#include "Development/LDDevelopmentDirector.h"
#include "LDVendorDirector.generated.h"

class ALDGameMode;
class FLDDemoContentSnapshot;
class FLDGameplayWritePermit;

// N05 shop: private sessions at the single fixed shop, atomic one-item Buy/Sell against the single ledger.
// Opening and every transaction re-check life, match phase and the world's shop eligibility in Transactions.
// The shop is not a safe zone and never gates equipment actives (Combat owns 1/2 casting).
UCLASS()
class LOCKDOWN_API ALDVendorDirector : public AActor
{
    GENERATED_BODY()

public:
    ALDVendorDirector();

    bool BeginMatch(const FLDMatchContext& Match, const TArray<FGuid>& Roster,
        TSharedPtr<const FLDDemoContentSnapshot> Content, FString& Error);

    // ---- Narrow server entries shared by the owning-connection adapter and server AI ----
    // Queues the open intent (at most one per contender; an identical intent merges) and returns Pending,
    // or Rejected when admission fails. The result arrives through ReadShop after the Transactions phase.
    FLDShopOpenResult QueueShopOpen(const FGuid& ContenderId, const FLDShopOpenCommand& Command);
    // Current-life session: Pending, Opened with a fresh quote view, Rejected, or None without any intent.
    FLDShopOpenResult ReadShop(const FGuid& ContenderId) const;
    FLDSubmitResult QueueShopCommand(const FGuid& ContenderId, const FLDShopCommand& Command);
    FLDShopReceiptQueryResult QueryShopReceipt(const FGuid& ContenderId, const FLDReceiptQuery& Query) const;

    // ---- Transactions phase (dispatched by ALDEconomyDirector::ProcessTransactions) ----
    void CollectPending(TArray<FLDPendingGrowthIntent>& Out) const;
    FLDShopOpenResult OpenPendingShop(const FGuid& ContenderId, const FLDGameplayWritePermit& Permit);
    // An AuthorityFault reason in the receipt means the boundary must fault.
    FLDShopReceipt CommitPendingShopCommand(const FGuid& ContenderId, const FLDGameplayWritePermit& Permit);

    // Shop receipts finalized since the last publication, for the owning connection.
    void TakeFinalReceipts(const FGuid& ContenderId, TArray<FLDShopReceipt>& Out);
    // Terminal open result (Opened with its view, or Rejected) produced since the last publication; moves it out.
    bool TakeOpenResult(const FGuid& ContenderId, FLDShopOpenResult& Out);

private:
    struct FPendingOpen
    {
        FLDShopOpenCommand Command;
        uint64 Arrival = 0;
    };

    struct FSession
    {
        FGuid SessionId;
        uint32 Life = 0;
        uint64 QuoteRevision = 0;
    };

    struct FRecord
    {
        TOptional<FPendingOpen> PendingOpen;
        TOptional<FSession> Session;
        // Last terminal open result of the current life (Opened or Rejected), without its view.
        FLDShopOpenResult LastOpen;
        // Terminal open result produced since the last publication.
        TOptional<FLDShopOpenResult> UnpublishedOpen;
        TLDGrowthRequestStream<FLDShopCommand, FLDShopReceipt> Requests;
        TArray<FLDShopReceipt> Unpublished;
    };

    bool BuildView(const FGuid& ContenderId, const FSession& Session, FLDShopView& Out) const;
    // Completes the pending request's receipt, finalizes its sequence and queues it for the owner publication.
    // Reason None finalizes Applied with Receipt's committed payload and versions; any other reason finalizes
    // Rejected with only the current account/inventory/build guards as sync hints.
    FLDShopReceipt Finish(const FGuid& ContenderId, FRecord& Record, ELDGrowthReason Reason,
        const FLDGameplayWritePermit& Permit, FLDShopReceipt Receipt = FLDShopReceipt());
    ALDGameMode* Mode() const;

    FLDMatchContext Match;
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    TMap<FGuid, FRecord> Records;
};
