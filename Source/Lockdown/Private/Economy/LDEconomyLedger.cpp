#include "Economy/LDEconomyLedger.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#endif

namespace
{

bool LedgerCheckedAdd(int64 A, int64 B, int64& Out)
{
    if ((B > 0 && A > MAX_int64 - B) || (B < 0 && A < MIN_int64 - B))
    {
        return false;
    }
    Out = A + B;
    return true;
}

bool LedgerCheckedIncrement(uint64 Value, uint64& Out)
{
    if (Value == MAX_uint64)
    {
        return false;
    }
    Out = Value + 1;
    return true;
}

bool LedgerFail(LDEconomy::EPrepareError& Error, FString& Message, LDEconomy::EPrepareError Code, const TCHAR* Text)
{
    Error = Code;
    Message = Text;
    return false;
}

bool LedgerSumGold(const TMap<FGuid, LDEconomy::FAccount>& Accounts, int64& Out)
{
    Out = 0;
    for (const auto& Pair : Accounts)
    {
        if (Pair.Value.Gold < 0 || !LedgerCheckedAdd(Out, Pair.Value.Gold, Out))
        {
            return false;
        }
    }
    return true;
}

template <typename TValue>
bool LedgerSameMap(const TMap<FGuid, TValue>& A, const TMap<FGuid, TValue>& B)
{
    if (A.Num() != B.Num())
    {
        return false;
    }
    for (const auto& Pair : A)
    {
        const TValue* Other = B.Find(Pair.Key);
        if (!Other || !(Pair.Value == *Other))
        {
            return false;
        }
    }
    return true;
}
}

FGuid LDEconomy::FLedger::InstanceFor(const FGuid& Match, uint64 IssueSerial)
{
    return FGuid(Match.A, Match.B, uint32(IssueSerial >> 32), uint32(IssueSerial & 0xffffffffull));
}

const TCHAR* LDEconomy::FLedger::ErrorName(EPrepareError Error)
{
    switch (Error)
    {
    case EPrepareError::None: return TEXT("None");
    case EPrepareError::NotInitialized: return TEXT("NotInitialized");
    case EPrepareError::InvalidTransaction: return TEXT("InvalidTransaction");
    case EPrepareError::UnknownAccount: return TEXT("UnknownAccount");
    case EPrepareError::RetiredAccount: return TEXT("RetiredAccount");
    case EPrepareError::AccountStale: return TEXT("AccountStale");
    case EPrepareError::InsufficientGold: return TEXT("InsufficientGold");
    case EPrepareError::Overflow: return TEXT("Overflow");
    case EPrepareError::UnknownItem: return TEXT("UnknownItem");
    case EPrepareError::NotOwner: return TEXT("NotOwner");
    case EPrepareError::ItemStale: return TEXT("ItemStale");
    case EPrepareError::InventoryStale: return TEXT("InventoryStale");
    case EPrepareError::SlotOccupied: return TEXT("SlotOccupied");
    case EPrepareError::EquipmentFull: return TEXT("EquipmentFull");
    case EPrepareError::AlreadyRetired: return TEXT("AlreadyRetired");
    case EPrepareError::DuplicateBusinessKey: return TEXT("DuplicateBusinessKey");
    case EPrepareError::BusinessKeyConflict: return TEXT("BusinessKeyConflict");
    case EPrepareError::JournalFull: return TEXT("JournalFull");
    }
    return TEXT("Unknown");
}

bool LDEconomy::ApplyEntry(FAuditState& State, FJournalEntry& Entry, bool bReplay, EPrepareError& Error, FString& Message)
{
    using E = EPrepareError;
    Error = E::None;
    Message.Reset();
    if (!State.Match.IsValid())
    {
        return LedgerFail(Error, Message, E::NotInitialized, TEXT("Ledger is not initialized for a match."));
    }
    const FTransaction& T = Entry.Transaction;
    uint64 Serial = 0;
    if (!LedgerCheckedIncrement(State.ThroughSerial, Serial))
    {
        return LedgerFail(Error, Message, E::Overflow, TEXT("Ledger commit serial is exhausted."));
    }
    if (bReplay && Entry.Serial != Serial)
    {
        return LedgerFail(Error, Message, E::InvalidTransaction, TEXT("Journal serial is not the next contiguous commit."));
    }
    if (!T.BusinessKey.IsValid() || T.Mint < 0 || T.Burn < 0)
    {
        return LedgerFail(Error, Message, E::InvalidTransaction, TEXT("Transaction identity or classification is invalid."));
    }
    const bool bGold = T.Gold.IsSet();
    const bool bCreate = T.Create.IsSet();
    const bool bConsume = T.Consume.IsSet();
    const bool bRetire = T.Retire.IsSet();
    bool bShape = false;
    switch (T.Cause)
    {
    case ECause::DeathReward:
        bShape = bGold && !bCreate && !bConsume && !bRetire && T.Gold->Delta > 0 && T.Mint == T.Gold->Delta && T.Burn == 0;
        break;
    case ECause::Buy:
        bShape = bGold && bCreate && !bConsume && !bRetire && T.Gold->Delta < 0 && T.Gold->Delta != MIN_int64
            && T.Burn == -T.Gold->Delta && T.Mint == 0 && T.Create->Owner == T.Gold->Account;
        break;
    case ECause::Sell:
        bShape = bGold && bConsume && !bCreate && !bRetire && T.Gold->Delta >= 0 && T.Mint == T.Gold->Delta && T.Burn == 0
            && T.Consume->Owner == T.Gold->Account;
        break;
    case ECause::AccountRetire:
        bShape = bRetire && !bGold && !bCreate && !bConsume && T.Mint == 0 && T.Burn == 0;
        break;
    default:
        break;
    }
    if (!bShape)
    {
        return LedgerFail(Error, Message, E::InvalidTransaction, TEXT("Transaction shape does not match its cause."));
    }
    int64 Minted = 0;
    int64 Burned = 0;
    if (!LedgerCheckedAdd(State.Minted, T.Mint, Minted) || !LedgerCheckedAdd(State.Burned, T.Burn, Burned))
    {
        return LedgerFail(Error, Message, E::Overflow, TEXT("Ledger mint or burn total would overflow."));
    }

    uint64 ResultAccountRevision = 0;
    FAccount* ChangedAccount = nullptr;
    FAccount NewAccount;
    if (bGold)
    {
        const FGoldChange& G = *T.Gold;
        FAccount* Account = State.Accounts.Find(G.Account);
        if (!Account)
        {
            return LedgerFail(Error, Message, E::UnknownAccount, TEXT("Gold account is not part of this match."));
        }
        if (Account->bRetired)
        {
            return LedgerFail(Error, Message, E::RetiredAccount, TEXT("Gold account is retired."));
        }
        if (Account->Revision != G.ExpectedRevision)
        {
            return LedgerFail(Error, Message, E::AccountStale, TEXT("Gold account revision changed."));
        }
        int64 NewGold = 0;
        if (!LedgerCheckedAdd(Account->Gold, G.Delta, NewGold) || !LedgerCheckedIncrement(Account->Revision, ResultAccountRevision))
        {
            return LedgerFail(Error, Message, E::Overflow, TEXT("Gold balance or account revision would overflow."));
        }
        if (NewGold < 0)
        {
            return LedgerFail(Error, Message, E::InsufficientGold, TEXT("Gold balance is insufficient."));
        }
        NewAccount = *Account;
        NewAccount.Gold = NewGold;
        NewAccount.Revision = ResultAccountRevision;
        ChangedAccount = Account;
    }

    uint64* Inventory = nullptr;
    uint64 NewInventory = 0;
    uint64 CreatedSerial = 0;
    FGuid CreatedInstance;
    FItem NewItem;
    if (bCreate)
    {
        const FCreateItem& C = *T.Create;
        const FAccount* Owner = State.Accounts.Find(C.Owner);
        Inventory = State.InventoryRevisions.Find(C.Owner);
        if (!Owner || !Inventory)
        {
            return LedgerFail(Error, Message, E::UnknownAccount, TEXT("Equipment owner is not part of this match."));
        }
        if (Owner->bRetired)
        {
            return LedgerFail(Error, Message, E::RetiredAccount, TEXT("Equipment owner is retired."));
        }
        if (*Inventory != C.ExpectedInventoryRevision)
        {
            return LedgerFail(Error, Message, E::InventoryStale, TEXT("Inventory revision changed."));
        }
        if (C.Definition.IsNone() || C.Slot >= EquipmentSlots)
        {
            return LedgerFail(Error, Message, E::InvalidTransaction, TEXT("Created equipment needs a definition and slot 0 or 1."));
        }
        int32 Held = 0;
        bool bSlotTaken = false;
        for (const auto& Pair : State.LiveItems)
        {
            if (Pair.Value.Owner == C.Owner)
            {
                ++Held;
                bSlotTaken |= Pair.Value.Slot == C.Slot;
            }
        }
        if (Held >= EquipmentSlots)
        {
            return LedgerFail(Error, Message, E::EquipmentFull, TEXT("Both equipment slots are occupied."));
        }
        if (bSlotTaken)
        {
            return LedgerFail(Error, Message, E::SlotOccupied, TEXT("The target equipment slot is occupied."));
        }
        if (!LedgerCheckedIncrement(State.IssuedThrough, CreatedSerial) || !LedgerCheckedIncrement(*Inventory, NewInventory))
        {
            return LedgerFail(Error, Message, E::Overflow, TEXT("Issue serial or inventory revision would overflow."));
        }
        CreatedInstance = FLedger::InstanceFor(State.Match, CreatedSerial);
        if (State.LiveItems.Contains(CreatedInstance))
        {
            return LedgerFail(Error, Message, E::InvalidTransaction, TEXT("Issued equipment identity is already live."));
        }
        NewItem.IssueSerial = CreatedSerial;
        NewItem.Definition = C.Definition;
        NewItem.Owner = C.Owner;
        NewItem.Slot = C.Slot;
        NewItem.Revision = 1;
    }

    FGuid ConsumedInstance;
    if (bConsume)
    {
        const FConsumeItem& C = *T.Consume;
        const FItem* Item = State.LiveItems.Find(C.Instance);
        if (!Item)
        {
            return LedgerFail(Error, Message, E::UnknownItem, TEXT("Equipment instance is not live."));
        }
        if (Item->Owner != C.Owner)
        {
            return LedgerFail(Error, Message, E::NotOwner, TEXT("Equipment instance belongs to another owner."));
        }
        if (Item->Revision != C.ExpectedItemRevision)
        {
            return LedgerFail(Error, Message, E::ItemStale, TEXT("Equipment item revision changed."));
        }
        const FAccount* Owner = State.Accounts.Find(C.Owner);
        Inventory = State.InventoryRevisions.Find(C.Owner);
        if (!Owner || !Inventory)
        {
            return LedgerFail(Error, Message, E::UnknownAccount, TEXT("Equipment owner is not part of this match."));
        }
        if (Owner->bRetired)
        {
            return LedgerFail(Error, Message, E::RetiredAccount, TEXT("Equipment owner is retired."));
        }
        if (*Inventory != C.ExpectedInventoryRevision)
        {
            return LedgerFail(Error, Message, E::InventoryStale, TEXT("Inventory revision changed."));
        }
        if (!LedgerCheckedIncrement(*Inventory, NewInventory))
        {
            return LedgerFail(Error, Message, E::Overflow, TEXT("Inventory revision would overflow."));
        }
        ConsumedInstance = C.Instance;
    }

    if (bRetire)
    {
        const FRetireAccount& R = *T.Retire;
        FAccount* Account = State.Accounts.Find(R.Account);
        if (!Account)
        {
            return LedgerFail(Error, Message, E::UnknownAccount, TEXT("Retired account is not part of this match."));
        }
        if (Account->bRetired)
        {
            return LedgerFail(Error, Message, E::AlreadyRetired, TEXT("Account is already retired."));
        }
        if (Account->Revision != R.ExpectedRevision)
        {
            return LedgerFail(Error, Message, E::AccountStale, TEXT("Account revision changed before retirement."));
        }
        if (R.Stamp.Life == 0 || R.Stamp.LifeRevision == 0 || !LedgerCheckedIncrement(Account->Revision, ResultAccountRevision))
        {
            return LedgerFail(Error, Message, E::InvalidTransaction, TEXT("Retirement needs the core leave stamp."));
        }
        NewAccount = *Account;
        NewAccount.bRetired = true;
        NewAccount.Revision = ResultAccountRevision;
        NewAccount.RetireSerial = Serial;
        NewAccount.RetireStamp = R.Stamp;
        ChangedAccount = Account;
    }

    int64 Total = 0;
    for (const auto& Pair : State.Accounts)
    {
        const int64 Gold = &Pair.Value == ChangedAccount ? NewAccount.Gold : Pair.Value.Gold;
        if (!LedgerCheckedAdd(Total, Gold, Total))
        {
            return LedgerFail(Error, Message, E::Overflow, TEXT("Total gold would overflow."));
        }
    }
    int64 Expected = 0;
    if (!LedgerCheckedAdd(State.InitialGoldTotal, Minted, Expected) || !LedgerCheckedAdd(Expected, -Burned, Expected) || Total != Expected)
    {
        return LedgerFail(Error, Message, E::InvalidTransaction, TEXT("Gold does not reconcile with the initial total, mint and burn."));
    }
    if (bReplay && (Entry.ResultAccountRevision != ResultAccountRevision || Entry.ResultInventoryRevision != NewInventory
        || Entry.CreatedInstance != CreatedInstance || Entry.CreatedIssueSerial != CreatedSerial))
    {
        return LedgerFail(Error, Message, E::InvalidTransaction, TEXT("Recorded journal results differ from the independent replay."));
    }

    // Validation is complete; the remaining steps cannot fail.
    if (ChangedAccount)
    {
        *ChangedAccount = NewAccount;
    }
    if (Inventory)
    {
        *Inventory = NewInventory;
    }
    if (bCreate)
    {
        State.LiveItems.Add(CreatedInstance, NewItem);
        State.IssuedThrough = CreatedSerial;
    }
    if (bConsume)
    {
        State.LiveItems.Remove(ConsumedInstance);
    }
    State.Minted = Minted;
    State.Burned = Burned;
    State.ThroughSerial = Serial;
    if (!bReplay)
    {
        Entry.Serial = Serial;
        Entry.ResultAccountRevision = ResultAccountRevision;
        Entry.ResultInventoryRevision = NewInventory;
        Entry.CreatedInstance = CreatedInstance;
        Entry.CreatedIssueSerial = CreatedSerial;
    }
    return true;
}

bool LDEconomy::SameState(const FAuditState& A, const FAuditState& B, FString& Difference)
{
    Difference.Reset();
    if (A.Match != B.Match || A.ThroughSerial != B.ThroughSerial)
    {
        Difference = TEXT("Match or committed serial differs.");
    }
    else if (A.IssuedThrough != B.IssuedThrough)
    {
        Difference = TEXT("Issued equipment serial differs.");
    }
    else if (A.InitialGoldTotal != B.InitialGoldTotal || A.Minted != B.Minted || A.Burned != B.Burned)
    {
        Difference = TEXT("Initial, mint or burn totals differ.");
    }
    else if (!LedgerSameMap(A.Accounts, B.Accounts))
    {
        Difference = TEXT("Account balances, revisions or retirement differ.");
    }
    else if (!LedgerSameMap(A.InventoryRevisions, B.InventoryRevisions))
    {
        Difference = TEXT("Inventory revisions differ.");
    }
    else if (!LedgerSameMap(A.LiveItems, B.LiveItems))
    {
        Difference = TEXT("Live equipment instances differ.");
    }
    return Difference.IsEmpty();
}

bool LDEconomy::FLedger::Initialize(const FGuid& Match, const TArray<FGuid>& Accounts, int64 StartingGold, FString& Error)
{
    Error.Reset();
    if (IsInitialized())
    {
        Error = TEXT("Ledger already belongs to a match; a new match needs a new ledger.");
        return false;
    }
    if (!Match.IsValid() || Accounts.IsEmpty() || StartingGold < 0)
    {
        Error = TEXT("Ledger initialization needs a match, roster accounts and non-negative starting gold.");
        return false;
    }
    FAuditState State;
    State.Match = Match;
    int64 Total = 0;
    for (const FGuid& Id : Accounts)
    {
        if (!Id.IsValid() || State.Accounts.Contains(Id))
        {
            Error = TEXT("Ledger roster accounts must be valid and distinct.");
            return false;
        }
        if (!LedgerCheckedAdd(Total, StartingGold, Total))
        {
            Error = TEXT("Initial gold total would overflow.");
            return false;
        }
        FAccount Account;
        Account.Gold = StartingGold;
        State.Accounts.Add(Id, Account);
        State.InventoryRevisions.Add(Id, 1);
    }
    State.InitialGoldTotal = Total;
    Prefix = State;
    Current = MoveTemp(State);
    Tail.Reset();
    Tail.Reserve(JournalTailCapacity);
    Pins = 0;
    return true;
}

void LDEconomy::FLedger::Reset()
{
    Prefix = FAuditState();
    Current = FAuditState();
    Tail.Reset();
    Pins = 0;
}

uint64 LDEconomy::FLedger::InventoryRevision(const FGuid& Owner) const
{
    const uint64* Revision = Current.InventoryRevisions.Find(Owner);
    return Revision ? *Revision : 0;
}

void LDEconomy::FLedger::OwnerItems(const FGuid& Owner, TArray<TPair<FGuid, FItem>>& Out) const
{
    Out.Reset();
    for (const auto& Pair : Current.LiveItems)
    {
        if (Pair.Value.Owner == Owner)
        {
            Out.Emplace(Pair.Key, Pair.Value);
        }
    }
    Out.Sort([](const TPair<FGuid, FItem>& A, const TPair<FGuid, FItem>& B) { return A.Value.Slot < B.Value.Slot; });
}

int64 LDEconomy::FLedger::TotalGold() const
{
    int64 Total = 0;
    return LedgerSumGold(Current.Accounts, Total) ? Total : -1;
}

LDEconomy::FPrepareResult LDEconomy::FLedger::Prepare(const FTransaction& Transaction) const
{
    FPrepareResult Result;
    if (!IsInitialized())
    {
        Result.Error = EPrepareError::NotInitialized;
        Result.Message = TEXT("Ledger is not initialized for a match.");
        return Result;
    }
    if (Tail.Num() >= JournalTailCapacity)
    {
        Result.Error = EPrepareError::JournalFull;
        Result.Message = TEXT("Ledger journal tail is full; compact the resolved prefix first.");
        return Result;
    }
    for (const FJournalEntry& Old : Tail)
    {
        if (Old.Transaction.BusinessKey == Transaction.BusinessKey)
        {
            const bool bSameTerms = FMemory::Memcmp(Old.Transaction.Terms, Transaction.Terms, sizeof(Transaction.Terms)) == 0;
            Result.Error = bSameTerms ? EPrepareError::DuplicateBusinessKey : EPrepareError::BusinessKeyConflict;
            Result.Message = bSameTerms ? TEXT("Business operation is already committed.")
                : TEXT("Business key was committed with different terms.");
            return Result;
        }
    }
    FPreparedCommit Prepared;
    Prepared.ExpectedCommittedSerial = Current.ThroughSerial;
    Prepared.Journal.Transaction = Transaction;
    Prepared.AfterState = Current;
    if (!ApplyEntry(Prepared.AfterState, Prepared.Journal, false, Result.Error, Result.Message))
    {
        return Result;
    }
    Result.Value.Emplace(MoveTemp(Prepared));
    return Result;
}

void LDEconomy::FLedger::ApplyPrepared(FPreparedCommit&& Prepared)
{
    checkf(IsInitialized() && Prepared.ExpectedCommittedSerial == Current.ThroughSerial
        && Prepared.Journal.Serial == Current.ThroughSerial + 1 && Tail.Num() < JournalTailCapacity,
        TEXT("Ledger plan does not match the current serial; prepare and apply must be one uninterrupted commit."));
    Current = MoveTemp(Prepared.AfterState);
    Tail.Add(MoveTemp(Prepared.Journal));
}

bool LDEconomy::FLedger::Reconcile(FString& Error) const
{
    Error.Reset();
    if (!IsInitialized())
    {
        Error = TEXT("Ledger is not initialized for a match.");
        return false;
    }
    int64 PrefixGold = 0;
    int64 PrefixExpected = 0;
    if (!LedgerSumGold(Prefix.Accounts, PrefixGold) || !LedgerCheckedAdd(Prefix.InitialGoldTotal, Prefix.Minted, PrefixExpected)
        || !LedgerCheckedAdd(PrefixExpected, -Prefix.Burned, PrefixExpected) || PrefixGold != PrefixExpected)
    {
        Error = TEXT("Audit prefix gold does not reconcile with its initial, mint and burn totals.");
        return false;
    }
    FAuditState Replay = Prefix;
    for (const FJournalEntry& Entry : Tail)
    {
        FJournalEntry Copy = Entry;
        EPrepareError Code = EPrepareError::None;
        FString Message;
        if (!ApplyEntry(Replay, Copy, true, Code, Message))
        {
            Error = FString::Printf(TEXT("Journal serial %llu does not replay: %s"), Entry.Serial, *Message);
            return false;
        }
    }
    return SameState(Replay, Current, Error);
}

LDEconomy::ECompactResult LDEconomy::FLedger::TryCompactResolvedPrefix(uint64 ExpectedCommittedSerial, FString& Error)
{
    Error.Reset();
    if (!IsInitialized())
    {
        Error = TEXT("Ledger is not initialized for a match.");
        return ECompactResult::PrepareUnavailable;
    }
    if (Pins > 0 || ExpectedCommittedSerial != Current.ThroughSerial)
    {
        Error = TEXT("A composite commit still holds the ledger, or the caller saw another serial.");
        return ECompactResult::Pinned;
    }
    if (Tail.IsEmpty())
    {
        return ECompactResult::NothingToCompact;
    }
    FAuditState Replay = Prefix;
    for (const FJournalEntry& Entry : Tail)
    {
        FJournalEntry Copy = Entry;
        EPrepareError Code = EPrepareError::None;
        FString Message;
        if (!ApplyEntry(Replay, Copy, true, Code, Message))
        {
            Error = FString::Printf(TEXT("Journal serial %llu does not replay: %s"), Entry.Serial, *Message);
            return ECompactResult::CorruptHistory;
        }
    }
    if (!SameState(Replay, Current, Error))
    {
        return ECompactResult::CorruptHistory;
    }
    Prefix = MoveTemp(Replay);
    Tail.Reset();
    return ECompactResult::Compacted;
}

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
FString LedgerJsonGuid(const FGuid& Id)
{
    return Id.ToString(EGuidFormats::DigitsWithHyphensLower);
}

FString LedgerJsonU64(uint64 Value)
{
    return FString::Printf(TEXT("%llu"), Value);
}

FString LedgerJsonI64(int64 Value)
{
    return FString::Printf(TEXT("%lld"), Value);
}

const TCHAR* LedgerJsonCause(LDEconomy::ECause Cause)
{
    switch (Cause)
    {
    case LDEconomy::ECause::DeathReward: return TEXT("DeathReward");
    case LDEconomy::ECause::Buy: return TEXT("Buy");
    case LDEconomy::ECause::Sell: return TEXT("Sell");
    case LDEconomy::ECause::AccountRetire: return TEXT("AccountRetire");
    case LDEconomy::ECause::None: break;
    }
    return TEXT("None");
}

TSharedRef<FJsonObject> LedgerJsonState(const LDEconomy::FAuditState& State)
{
    TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
    Out->SetStringField(TEXT("throughSerial"), LedgerJsonU64(State.ThroughSerial));
    Out->SetStringField(TEXT("issuedThrough"), LedgerJsonU64(State.IssuedThrough));
    Out->SetStringField(TEXT("initialGoldTotal"), LedgerJsonI64(State.InitialGoldTotal));
    Out->SetStringField(TEXT("minted"), LedgerJsonI64(State.Minted));
    Out->SetStringField(TEXT("burned"), LedgerJsonI64(State.Burned));

    TArray<FGuid> AccountIds;
    State.Accounts.GetKeys(AccountIds);
    AccountIds.Sort();
    TArray<TSharedPtr<FJsonValue>> Accounts;
    for (const FGuid& Id : AccountIds)
    {
        const LDEconomy::FAccount& Account = State.Accounts.FindChecked(Id);
        TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("id"), LedgerJsonGuid(Id));
        Row->SetStringField(TEXT("gold"), LedgerJsonI64(Account.Gold));
        Row->SetStringField(TEXT("revision"), LedgerJsonU64(Account.Revision));
        Row->SetBoolField(TEXT("retired"), Account.bRetired);
        Row->SetStringField(TEXT("retireSerial"), LedgerJsonU64(Account.RetireSerial));
        Row->SetNumberField(TEXT("retireLife"), Account.RetireStamp.Life);
        Row->SetStringField(TEXT("retireLifeRevision"), LedgerJsonU64(Account.RetireStamp.LifeRevision));
        Accounts.Add(MakeShared<FJsonValueObject>(Row));
    }
    Out->SetArrayField(TEXT("accounts"), Accounts);

    TArray<FGuid> Owners;
    State.InventoryRevisions.GetKeys(Owners);
    Owners.Sort();
    TArray<TSharedPtr<FJsonValue>> Inventories;
    for (const FGuid& OwnerId : Owners)
    {
        TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("owner"), LedgerJsonGuid(OwnerId));
        Row->SetStringField(TEXT("revision"), LedgerJsonU64(State.InventoryRevisions.FindChecked(OwnerId)));
        Inventories.Add(MakeShared<FJsonValueObject>(Row));
    }
    Out->SetArrayField(TEXT("inventories"), Inventories);

    TArray<TPair<FGuid, LDEconomy::FItem>> Live;
    for (const auto& Pair : State.LiveItems)
    {
        Live.Emplace(Pair.Key, Pair.Value);
    }
    Live.Sort([](const TPair<FGuid, LDEconomy::FItem>& A, const TPair<FGuid, LDEconomy::FItem>& B)
    {
        return A.Value.IssueSerial < B.Value.IssueSerial;
    });
    TArray<TSharedPtr<FJsonValue>> Items;
    for (const TPair<FGuid, LDEconomy::FItem>& Pair : Live)
    {
        TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("instance"), LedgerJsonGuid(Pair.Key));
        Row->SetStringField(TEXT("issueSerial"), LedgerJsonU64(Pair.Value.IssueSerial));
        Row->SetStringField(TEXT("definition"), Pair.Value.Definition.ToString().ToLower());
        Row->SetStringField(TEXT("owner"), LedgerJsonGuid(Pair.Value.Owner));
        Row->SetNumberField(TEXT("slot"), Pair.Value.Slot);
        Row->SetStringField(TEXT("revision"), LedgerJsonU64(Pair.Value.Revision));
        Items.Add(MakeShared<FJsonValueObject>(Row));
    }
    Out->SetArrayField(TEXT("items"), Items);
    return Out;
}

TSharedRef<FJsonObject> LedgerJsonEntry(const LDEconomy::FJournalEntry& Entry)
{
    const LDEconomy::FTransaction& T = Entry.Transaction;
    TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
    Out->SetStringField(TEXT("serial"), LedgerJsonU64(Entry.Serial));
    Out->SetStringField(TEXT("cause"), LedgerJsonCause(T.Cause));
    Out->SetStringField(TEXT("businessKey"), LedgerJsonGuid(T.BusinessKey));
    Out->SetStringField(TEXT("terms"), BytesToHex(T.Terms, UE_ARRAY_COUNT(T.Terms)).ToLower());
    Out->SetStringField(TEXT("commitBatch"), LedgerJsonU64(T.CommitBatch));
    Out->SetStringField(TEXT("mint"), LedgerJsonI64(T.Mint));
    Out->SetStringField(TEXT("burn"), LedgerJsonI64(T.Burn));
    if (T.Gold.IsSet())
    {
        TSharedRef<FJsonObject> Gold = MakeShared<FJsonObject>();
        Gold->SetStringField(TEXT("account"), LedgerJsonGuid(T.Gold->Account));
        Gold->SetStringField(TEXT("expectedRevision"), LedgerJsonU64(T.Gold->ExpectedRevision));
        Gold->SetStringField(TEXT("delta"), LedgerJsonI64(T.Gold->Delta));
        Out->SetObjectField(TEXT("gold"), Gold);
    }
    else
    {
        Out->SetField(TEXT("gold"), MakeShared<FJsonValueNull>());
    }
    if (T.Create.IsSet())
    {
        TSharedRef<FJsonObject> Create = MakeShared<FJsonObject>();
        Create->SetStringField(TEXT("owner"), LedgerJsonGuid(T.Create->Owner));
        Create->SetStringField(TEXT("expectedInventoryRevision"), LedgerJsonU64(T.Create->ExpectedInventoryRevision));
        Create->SetStringField(TEXT("definition"), T.Create->Definition.ToString().ToLower());
        Create->SetNumberField(TEXT("slot"), T.Create->Slot);
        Out->SetObjectField(TEXT("create"), Create);
    }
    else
    {
        Out->SetField(TEXT("create"), MakeShared<FJsonValueNull>());
    }
    if (T.Consume.IsSet())
    {
        TSharedRef<FJsonObject> Consume = MakeShared<FJsonObject>();
        Consume->SetStringField(TEXT("instance"), LedgerJsonGuid(T.Consume->Instance));
        Consume->SetStringField(TEXT("expectedItemRevision"), LedgerJsonU64(T.Consume->ExpectedItemRevision));
        Consume->SetStringField(TEXT("owner"), LedgerJsonGuid(T.Consume->Owner));
        Consume->SetStringField(TEXT("expectedInventoryRevision"), LedgerJsonU64(T.Consume->ExpectedInventoryRevision));
        Out->SetObjectField(TEXT("consume"), Consume);
    }
    else
    {
        Out->SetField(TEXT("consume"), MakeShared<FJsonValueNull>());
    }
    if (T.Retire.IsSet())
    {
        TSharedRef<FJsonObject> Retire = MakeShared<FJsonObject>();
        Retire->SetStringField(TEXT("account"), LedgerJsonGuid(T.Retire->Account));
        Retire->SetStringField(TEXT("expectedRevision"), LedgerJsonU64(T.Retire->ExpectedRevision));
        Retire->SetNumberField(TEXT("life"), T.Retire->Stamp.Life);
        Retire->SetStringField(TEXT("lifeRevision"), LedgerJsonU64(T.Retire->Stamp.LifeRevision));
        Out->SetObjectField(TEXT("retire"), Retire);
    }
    else
    {
        Out->SetField(TEXT("retire"), MakeShared<FJsonValueNull>());
    }
    Out->SetStringField(TEXT("resultAccountRevision"), LedgerJsonU64(Entry.ResultAccountRevision));
    Out->SetStringField(TEXT("resultInventoryRevision"), LedgerJsonU64(Entry.ResultInventoryRevision));
    if (Entry.CreatedInstance.IsValid())
    {
        Out->SetStringField(TEXT("createdInstance"), LedgerJsonGuid(Entry.CreatedInstance));
    }
    else
    {
        Out->SetField(TEXT("createdInstance"), MakeShared<FJsonValueNull>());
    }
    Out->SetStringField(TEXT("createdIssueSerial"), LedgerJsonU64(Entry.CreatedIssueSerial));
    return Out;
}
}

bool LDEconomy::ExportEvidence(const FLedger& Ledger, int64 StartingGold, FString& OutJson, FString& Error)
{
    OutJson.Reset();
    Error.Reset();
    if (!Ledger.IsInitialized())
    {
        Error = TEXT("Ledger is not initialized for a match.");
        return false;
    }
    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("schemaVersion"), 2);
    Root->SetStringField(TEXT("match"), LedgerJsonGuid(Ledger.GetMatch()));
    Root->SetStringField(TEXT("startingGold"), LedgerJsonI64(StartingGold));
    Root->SetObjectField(TEXT("prefix"), LedgerJsonState(Ledger.AuditPrefix()));
    TArray<TSharedPtr<FJsonValue>> Tail;
    for (const FJournalEntry& Entry : Ledger.JournalTail())
    {
        Tail.Add(MakeShared<FJsonValueObject>(LedgerJsonEntry(Entry)));
    }
    Root->SetArrayField(TEXT("tail"), Tail);
    Root->SetObjectField(TEXT("current"), LedgerJsonState(Ledger.State()));
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
    if (!FJsonSerializer::Serialize(Root, Writer))
    {
        OutJson.Reset();
        Error = TEXT("Ledger evidence could not be serialized.");
        return false;
    }
    return true;
}
#endif