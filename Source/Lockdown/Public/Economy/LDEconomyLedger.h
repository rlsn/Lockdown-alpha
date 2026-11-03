#pragma once

#include "CoreMinimal.h"

// Authority-only Demo ledger (PROGRESSION_EQUIPMENT_ECONOMY §4.2/§4.4): one gold balance per
// contender account plus the live equipment instances and their two-slot positions.
// Prepare is pure and builds the complete after-state; ApplyPrepared only moves it in.
// Every committed entry stays replayable from the immutable audit prefix until compaction folds it.
namespace LDEconomy
{
    constexpr int32 EquipmentSlots = 2;
    constexpr int32 JournalTailCapacity = 64;

    struct FRetireStamp
    {
        uint32 Life = 0;
        uint64 LifeRevision = 0;
        bool operator==(const FRetireStamp& Other) const { return Life == Other.Life && LifeRevision == Other.LifeRevision; }
    };

    struct FAccount
    {
        int64 Gold = 0;
        uint64 Revision = 1;
        bool bRetired = false;
        uint64 RetireSerial = 0;
        FRetireStamp RetireStamp;
        bool operator==(const FAccount& Other) const
        {
            return Gold == Other.Gold && Revision == Other.Revision && bRetired == Other.bRetired
                && RetireSerial == Other.RetireSerial && RetireStamp == Other.RetireStamp;
        }
    };

    struct FItem
    {
        uint64 IssueSerial = 0;
        FName Definition;
        FGuid Owner;
        uint8 Slot = 0;
        uint64 Revision = 1;
        bool operator==(const FItem& Other) const
        {
            return IssueSerial == Other.IssueSerial && Definition == Other.Definition && Owner == Other.Owner
                && Slot == Other.Slot && Revision == Other.Revision;
        }
    };

    enum class ECause : uint8
    {
        None, DeathReward, Buy, Sell, AccountRetire,
#if WITH_DEV_AUTOMATION_TESTS
        // DEV growth fixture grant (-LDDevGrowthFixture only): Mint to one account, never a death or a trade.
        DevFixture,
#endif
    };

    struct FGoldChange
    {
        FGuid Account;
        uint64 ExpectedRevision = 0;
        int64 Delta = 0;
    };

    struct FCreateItem
    {
        FGuid Owner;
        uint64 ExpectedInventoryRevision = 0;
        FName Definition;
        uint8 Slot = 0;
    };

    struct FConsumeItem
    {
        FGuid Instance;
        uint64 ExpectedItemRevision = 0;
        FGuid Owner;
        uint64 ExpectedInventoryRevision = 0;
    };

    struct FRetireAccount
    {
        FGuid Account;
        uint64 ExpectedRevision = 0;
        FRetireStamp Stamp;
    };

    // Demo transactions touch at most one account. Cause fixes the allowed shape:
    // DeathReward = Mint to one account; Buy = Burn + Create; Sell = Consume + Mint; AccountRetire = Retire only;
    // DevFixture (DEV builds) = Mint to one account like DeathReward.
    struct FTransaction
    {
        ECause Cause = ECause::None;
        FGuid BusinessKey;
        uint8 Terms[32] = {};
        uint64 CommitBatch = 0;
        int64 Mint = 0;
        int64 Burn = 0;
        TOptional<FGoldChange> Gold;
        TOptional<FCreateItem> Create;
        TOptional<FConsumeItem> Consume;
        TOptional<FRetireAccount> Retire;
    };

    struct FJournalEntry
    {
        uint64 Serial = 0;
        FTransaction Transaction;
        uint64 ResultAccountRevision = 0;
        uint64 ResultInventoryRevision = 0;
        FGuid CreatedInstance;
        uint64 CreatedIssueSerial = 0;
    };

    // Complete replayable state at ThroughSerial: the immutable audit prefix and the live state share it.
    struct FAuditState
    {
        FGuid Match;
        uint64 ThroughSerial = 0;
        TMap<FGuid, FAccount> Accounts;
        TMap<FGuid, uint64> InventoryRevisions;
        TMap<FGuid, FItem> LiveItems;
        uint64 IssuedThrough = 0;
        int64 InitialGoldTotal = 0;
        int64 Minted = 0;
        int64 Burned = 0;
    };

    enum class EPrepareError : uint8
    {
        None, NotInitialized, InvalidTransaction, UnknownAccount, RetiredAccount, AccountStale, InsufficientGold,
        Overflow, UnknownItem, NotOwner, ItemStale, InventoryStale, SlotOccupied, EquipmentFull, AlreadyRetired,
        DuplicateBusinessKey, BusinessKeyConflict, JournalFull
    };

    class LOCKDOWN_API FPreparedCommit
    {
    public:
        FPreparedCommit(FPreparedCommit&&) = default;
        FPreparedCommit& operator=(FPreparedCommit&&) = default;
        FPreparedCommit(const FPreparedCommit&) = delete;
        FPreparedCommit& operator=(const FPreparedCommit&) = delete;

        const FJournalEntry& Entry() const { return Journal; }
        const FAuditState& After() const { return AfterState; }

    private:
        friend class FLedger;
        FPreparedCommit() = default;
        uint64 ExpectedCommittedSerial = 0;
        FJournalEntry Journal;
        FAuditState AfterState;
    };

    struct LOCKDOWN_API FPrepareResult
    {
        TOptional<FPreparedCommit> Value;
        EPrepareError Error = EPrepareError::None;
        FString Message;
        bool IsSuccess() const { return Value.IsSet() && Error == EPrepareError::None; }
    };

    enum class ECompactResult : uint8 { Compacted, NothingToCompact, Pinned, PrepareUnavailable, CorruptHistory };

#if WITH_DEV_AUTOMATION_TESTS
    // One committed DevFixture mint; kept for the whole match because compaction folds its journal entry.
    struct FDevFixtureMint
    {
        uint64 Serial = 0;
        FGuid Account;
        int64 Gold = 0;
    };
#endif

    class LOCKDOWN_API FLedger
    {
    public:
        // One ledger per match. StartingGold is the approved SHOP.StartingGold for every roster account.
        bool Initialize(const FGuid& Match, const TArray<FGuid>& Accounts, int64 StartingGold, FString& Error);
        void Reset();
        bool IsInitialized() const { return Current.Match.IsValid(); }
        const FGuid& GetMatch() const { return Current.Match; }

        FPrepareResult Prepare(const FTransaction& Transaction) const;
        // No-fail install of a plan prepared against the current serial; any mismatch is a broken caller contract.
        void ApplyPrepared(FPreparedCommit&& Prepared);

        bool Reconcile(FString& Error) const;
        // Folds the resolved tail into a new prefix after an independent replay matches the live state.
        ECompactResult TryCompactResolvedPrefix(uint64 ExpectedCommittedSerial, FString& Error);

        const FAccount* Account(const FGuid& Id) const { return Current.Accounts.Find(Id); }
        const FItem* Item(const FGuid& Instance) const { return Current.LiveItems.Find(Instance); }
        // Zero means the owner has no account in this match.
        uint64 InventoryRevision(const FGuid& Owner) const;
        // Live instances of one owner ordered by slot.
        void OwnerItems(const FGuid& Owner, TArray<TPair<FGuid, FItem>>& Out) const;
        const FAuditState& State() const { return Current; }
        const FAuditState& AuditPrefix() const { return Prefix; }
        const TArray<FJournalEntry>& JournalTail() const { return Tail; }
        uint64 CommittedSerial() const { return Current.ThroughSerial; }
        uint64 NextCommitSerial() const { return Current.ThroughSerial + 1; }
        uint64 IssuedThrough() const { return Current.IssuedThrough; }
        int64 TotalGold() const;

        static FGuid InstanceFor(const FGuid& Match, uint64 IssueSerial);
        static const TCHAR* ErrorName(EPrepareError Error);

        // A composite commit pins the ledger between its first Prepare and its Apply.
        void Pin() { ++Pins; }
        void Unpin() { check(Pins > 0); --Pins; }
#if WITH_DEV_AUTOMATION_TESTS
        const TArray<FDevFixtureMint>& DevFixtureGrants() const { return DevFixtureMints; }
#endif

    private:
        FAuditState Prefix;
        FAuditState Current;
        TArray<FJournalEntry> Tail;
        int32 Pins = 0;
#if WITH_DEV_AUTOMATION_TESTS
        TArray<FDevFixtureMint> DevFixtureMints;
#endif
    };

    // Applies one entry to State with full validation. bReplay checks recorded results instead of filling them.
    LOCKDOWN_API bool ApplyEntry(FAuditState& State, FJournalEntry& Entry, bool bReplay, EPrepareError& Error, FString& Message);
    LOCKDOWN_API bool SameState(const FAuditState& A, const FAuditState& B, FString& Difference);
#if WITH_DEV_AUTOMATION_TESTS
    // DEV evidence for Scripts\CheckEconomyEvidence.py (schema 3): the audit prefix, the journal tail after it, the
    // live state and every DevFixture mint of the match, every 64-bit value as a decimal string. StartingGold is the
    // approved SHOP.StartingGold of the match.
    LOCKDOWN_API bool ExportEvidence(const FLedger& Ledger, int64 StartingGold, FString& OutJson, FString& Error);
#endif
}
