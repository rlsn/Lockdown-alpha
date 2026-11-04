#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Development/LDDevGrowthFixture.h"
#include "Development/LDDevTestProtocol.h"
#include "Development/LDGrowthRequests.h"
#include "Development/LDGrowthProtocol.h"
#include "Development/LDProgressionRules.h"
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
    // DEV growth fixture (LDDevGrowthFixture.h): queues an XP/gold grant that the next Transactions phase commits through
    // the reward composite with ledger cause DevFixture and no death fact. Returns a ticket for DevGrantStatus, or 0
    // with Error when the fixture, match, contender or amounts refuse it.
    uint64 DevGrantReward(const FGuid& ContenderId, int64 XP, int64 Gold, FString& Error);
    ELDDevGrantStatus DevGrantStatus(uint64 Ticket, FString& Detail) const;
    // DEV test command (LDDevTestProtocol.h) of ContenderId's own owning connection: refused unless the fixture admits
    // this authority, then admitted into that contender's DEV request stream. The pending command commits in the
    // Transactions phase in server arrival order (CommitPendingDevTest).
    // bHostConnection: the command arrived through the Listen Host's own connection (match-wide ops need it).
    FLDDevTestResult QueueDevTest(const FGuid& ContenderId, const FLDDevTestCommand& Command, bool bHostConnection);
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
    friend class FLDAccountRetireAwaitsRewardTest;
    friend class FLDDemoDevTestCommandsTest;
    // Retires the ledger accounts of contenders that left; M is the authority core of this match.
    bool RetireLeftAccounts(ALDGameMode& M, const FLDGameplayWritePermit& Permit, FString& Error);
    bool CompactLedger(FString& Error);
    void RecordReward(const FLDRewardReceipt& Receipt);
    // The reward composite (PEE §4.2): a ledger mint when Gold > 0, the XP with its levels and opportunities and the
    // Combat projection, all prepared before any is installed. Death rewards and the DEV growth fixture share it.
    ELDRewardOutcome CommitRewardParts(const FGuid& Recipient, const LDProgression::FRewardAmounts& Amounts,
        LDEconomy::ECause Cause, const TArray<uint8>& KeyBytes, const uint8 (&TransactionTerms)[32],
        const FLDGameplayWritePermit& Permit, FLDRewardReceipt& Receipt, ELDGrowthReason& Reason);
    ALDGameMode* Mode() const;
#if WITH_DEV_AUTOMATION_TESTS
    // Commits every pending DEV grant in ticket order; false only on an authority fault.
    bool CommitDevGrants(const FLDGameplayWritePermit& Permit, FString& Error);

    struct FDevGrant
    {
        uint64 Ticket = 0;
        FGuid Contender;
        int64 XP = 0;
        int64 Gold = 0;
        ELDDevGrantStatus Status = ELDDevGrantStatus::Pending;
        FString Detail;
    };
    // Every DEV grant of this match in ticket order (ticket = index + 1).
    TArray<FDevGrant> DevGrants;

    // QueueDevTest after the fixture admitted this authority: shape, match, contender and the request stream.
    FLDDevTestResult AdmitDevTest(const FGuid& ContenderId, const FLDDevTestCommand& Command, bool bHostConnection = false);
    // Commits or rejects this contender's pending DEV test command; false only on an authority fault. A retryable
    // reward refusal leaves the command pending for the next Transactions phase.
    bool CommitPendingDevTest(const FGuid& ContenderId, const FLDGameplayWritePermit& Permit, FString& Error);
    // ResetToMatchStart as one composite: DevReset ledger entries (each held item consumed without a refund, then one gold
    // change back to the starting gold) rehearsed on a copy of the ledger, Growth's DevPrepareReset and Combat's
    // DevPrepareReset prepared against those results, then everything installed. Nothing changes on a refusal; bRetry
    // when the journal tail is full (the command stays pending); AuthorityFault when the install lost its domain.
    ELDGrowthReason CommitDevReset(const FGuid& ContenderId, const FLDDevTestCommand& Command, const FLDGameplayWritePermit& Permit,
        FString& Detail, bool& bRetry);
    struct FDevTestRecord
    {
        TLDGrowthRequestStream<FLDDevTestCommand, FLDDevTestReceipt> Stream;
        TArray<FLDDevTestReceipt> Unpublished;
    };
    // DEV test request streams of this match, per contender; a new match spawns a new director and starts empty.
    TMap<FGuid, FDevTestRecord> DevTests;
#endif

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
