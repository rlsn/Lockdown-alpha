#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Development/LDGrowthProtocol.h"
#include "Economy/LDEconomyLedger.h"
#include "LDEconomyDirector.generated.h"

class ALDGameMode;
class FLDDemoContentSnapshot;
class FLDGameplayWritePermit;
struct FActorSpawnParameters;
struct FLDCommittedDeath;

// Growth/shop domain hub (N03 rewards, N04, N05): owns the single ledger, the death-reward sink, the
// Transactions dispatch and the owner publication. Every gold/XP/item change is one composite commit.
UCLASS()
class LOCKDOWN_API ALDEconomyDirector : public AActor
{
    GENERATED_BODY()

public:
    ALDEconomyDirector();

    // Core authority initialization: spawns Economy, Development, Inventory and Vendor onto Mode.
    // A failure leaves no growth director behind.
    static bool SpawnDomain(ALDGameMode& Mode, const FActorSpawnParameters& Spawn, FString& Error);
    static void RetireDomain(ALDGameMode& Mode);
    // Starting: one ledger account and growth record per locked roster contender (humans and AI slots).
    bool BeginMatch(ALDGameMode& Mode, const FLDMatchContext& InMatch, const TArray<FGuid>& InRoster, FString& Error);

    // DeathsAndRewards: called once per committed death after its Effects set locked every RewardBasis.
    FLDRewardReceipt CommitDeathReward(const FLDCommittedDeath& Death, const FLDGameplayWritePermit& Permit);
    // Transactions: shop openings, choices and shop commands in server arrival order, then account retirement
    // of contenders that left and ledger maintenance. Single rejections are receipts; false only on a fault.
    bool ProcessTransactions(const FLDGameplayWritePermit& Permit, FString& Error);
    // Publish (after Close, no permit exists): refreshes changed owner projections through SyncOwner and delivers
    // final receipts to owning connections. Writes no gameplay state; false only for a boundary of another match.
    bool PublishGrowth(const FLDAuthorityBoundary& Boundary, FString& Error);

    // Owner projection read by ALDGameMode::SyncOwner; one consistent view of gold, items, level and offer.
    FLDGrowthOwnerView ReadOwnerView(const FGuid& ContenderId) const;
    // Committed level for the frozen result rows; contenders that left keep their record until the match ends.
    bool ReadFinalLevel(const FGuid& ContenderId, uint8& Level, FString& Error) const;
#if WITH_DEV_AUTOMATION_TESTS
    // DEV scenarios: this match's ledger evidence for Scripts\CheckEconomyEvidence.py (LDEconomy::ExportEvidence).
    bool ExportLedgerEvidence(FString& OutJson, FString& Error) const;
#endif

    // ---- Module internals ----
    const LDEconomy::FLedger& GetLedger() const { return Ledger; }
    LDEconomy::FLedger& MutableLedger() { return Ledger; }
    const FLDMatchContext& GetMatch() const { return Match; }
    const FLDDemoContentSnapshot* GetContent() const { return Content.Get(); }
    uint64 NextArrival() { return ++ArrivalCounter; }
    void MarkDirty(const FGuid& ContenderId) { Dirty.Add(ContenderId); }
    // Stable 16-byte business key and SHA-256 terms of a canonical byte record.
    static FGuid BusinessKey(const TArray<uint8>& Canonical);
    static void Terms(const TArray<uint8>& Canonical, uint8 (&Out)[32]);

private:
    bool RetireLeftAccounts(const FLDGameplayWritePermit& Permit, FString& Error);
    bool CompactLedger(FString& Error);
    void RecordReward(const FLDRewardReceipt& Receipt);
    ALDGameMode* Mode() const;

    FLDMatchContext Match;
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    LDEconomy::FLedger Ledger;
    TArray<FGuid> Roster;
    uint64 ArrivalCounter = 0;
    TSet<FGuid> Dirty;
    TSet<FGuid> Retired;
    TMap<FGuid, uint64> PublishRevisions;
    // Highest resolved victim life per (kind, entity) and a bounded replay window of reward receipts.
    TMap<TPair<uint8, FGuid>, uint32> ResolvedLifeThrough;
    TArray<FLDRewardReceipt> RecentRewards;
};
