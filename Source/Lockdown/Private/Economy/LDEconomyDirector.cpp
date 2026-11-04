#include "Economy/LDEconomyDirector.h"

#include "Combat/LDCombatDirector.h"
#include "Combat/LDCombatTypes.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDevelopmentDirector.h"
#include "Development/LDProgressionRules.h"
#include "Engine/World.h"
#include "Framework/LDDemoEntry.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Inventory/LDInventoryDirector.h"
#include "Inventory/LDVendorDirector.h"
#include "Misc/ScopeExit.h"
#include "Serialization/MemoryWriter.h"

THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END

namespace
{
constexpr int32 EconomyRewardWindow = 64;

void EconomyWriteVictim(FMemoryWriter& Writer, const FLDVictimKey& Victim)
{
    uint8 Kind = uint8(Victim.Kind);
    FGuid Entity = Victim.EntityId;
    uint32 Life = Victim.Life;
    Writer << Kind << Entity << Life;
}

void EconomyDeathKeyBytes(const FLDDeathKey& Key, TArray<uint8>& Out)
{
    Out.Reset();
    FMemoryWriter Writer(Out);
    FString Tag = TEXT("death-reward");
    FGuid LedgerMatch = Key.LedgerMatch;
    Writer << Tag << LedgerMatch;
    EconomyWriteVictim(Writer, Key.Victim);
}

// Every field of the immutable committed fact; a different fact under the same DeathKey conflicts.
void EconomyDeathFactBytes(const FLDCommittedDeath& Death, TArray<uint8>& Out)
{
    EconomyDeathKeyBytes(Death.Key, Out);
    FMemoryWriter Writer(Out, false, true);
    EconomyWriteVictim(Writer, Death.VictimBody.Victim);
    FGuid VictimBody = Death.VictimBody.BodyIdentity;
    FGuid VictimContender = Death.VictimContender;
    FGuid FatalEffect = Death.FatalEffectId;
    FGuid CreditOwner = Death.CreditOwner;
    uint8 SourceKind = uint8(Death.ReleasedSource.Kind);
    FString SourceDefinition = Death.ReleasedSource.DefinitionId.ToString();
    FGuid SourceInstance = Death.ReleasedSource.ItemInstance;
    Writer << VictimBody << VictimContender << FatalEffect << CreditOwner << SourceKind << SourceDefinition << SourceInstance;
    EconomyWriteVictim(Writer, Death.SourceBodyAtRelease.Victim);
    FGuid SourceBody = Death.SourceBodyAtRelease.BodyIdentity;
    FString RewardDefinition = Death.RewardBasis.RewardDefinitionId.ToString();
    uint8 VictimLevel = Death.RewardBasis.VictimLevelAtDeath;
    uint64 VictimBuild = Death.RewardBasis.VictimBuildRevisionAtDeath;
    uint8 Eligible = Death.RewardBasis.bCreditOwnerEligibleAtDeath ? 1 : 0;
    uint8 NoRewardReason = uint8(Death.RewardBasis.NoRewardReason);
    uint8 Cause = uint8(Death.Cause);
    double OccurredAtSeconds = Death.OccurredAtSeconds;
    int64 OccurredAtMs = Death.OccurredAtMs;
    Writer << SourceBody << RewardDefinition << VictimLevel << VictimBuild << Eligible << NoRewardReason << Cause
        << OccurredAtSeconds << OccurredAtMs;
}

ELDGrowthReason EconomyNoRewardReason(ELDNoRewardReason Reason)
{
    switch (Reason)
    {
    case ELDNoRewardReason::NoOwner: return ELDGrowthReason::NoRewardNoOwner;
    case ELDNoRewardReason::SelfInflicted: return ELDGrowthReason::NoRewardSelf;
    case ELDNoRewardReason::OwnerLeft: return ELDGrowthReason::NoRewardOwnerLeft;
    default: return ELDGrowthReason::DataInvalid;
    }
}

ELDRewardOutcome EconomyLedgerOutcome(LDEconomy::EPrepareError Error, ELDGrowthReason& Reason)
{
    switch (Error)
    {
    case LDEconomy::EPrepareError::JournalFull:
        Reason = ELDGrowthReason::PrepareUnavailable;
        return ELDRewardOutcome::RetryableNotCommitted;
    case LDEconomy::EPrepareError::Overflow:
        Reason = ELDGrowthReason::ArithmeticOverflow;
        return ELDRewardOutcome::PermanentlyRejected;
    case LDEconomy::EPrepareError::RetiredAccount:
        Reason = ELDGrowthReason::LeftMatch;
        return ELDRewardOutcome::PermanentlyRejected;
    default:
        Reason = ELDGrowthReason::DataInvalid;
        return ELDRewardOutcome::PermanentlyRejected;
    }
}

ELDRewardOutcome EconomyCombatOutcome(ELDCombatPrepareError Error, ELDGrowthReason& Reason)
{
    switch (Error)
    {
    case ELDCombatPrepareError::CapacityExceeded:
        Reason = ELDGrowthReason::CapacityExceeded;
        return ELDRewardOutcome::RetryableNotCommitted;
    case ELDCombatPrepareError::NumericUnderflow:
        Reason = ELDGrowthReason::NumericUnderflow;
        return ELDRewardOutcome::PermanentlyRejected;
    case ELDCombatPrepareError::NumericDomain:
        Reason = ELDGrowthReason::NumericDomain;
        return ELDRewardOutcome::PermanentlyRejected;
    case ELDCombatPrepareError::StaleBuild:
        Reason = ELDGrowthReason::BuildStale;
        return ELDRewardOutcome::PermanentlyRejected;
    case ELDCombatPrepareError::UnknownContender:
        Reason = ELDGrowthReason::NotContender;
        return ELDRewardOutcome::PermanentlyRejected;
    case ELDCombatPrepareError::NotAuthority:
    case ELDCombatPrepareError::StaleMatch:
        Reason = ELDGrowthReason::NotAuthority;
        return ELDRewardOutcome::PermanentlyRejected;
    default:
        Reason = ELDGrowthReason::DataInvalid;
        return ELDRewardOutcome::PermanentlyRejected;
    }
}

bool EconomyIsFault(uint16 Reason)
{
    return LDGrowthWire::Reason(Reason) == ELDGrowthReason::AuthorityFault;
}
}

ALDEconomyDirector::ALDEconomyDirector()
{
    bReplicates = false;
    PrimaryActorTick.bCanEverTick = false;
}

ALDGameMode* ALDEconomyDirector::Mode() const
{
    UWorld* World = GetWorld();
    return World ? World->GetAuthGameMode<ALDGameMode>() : nullptr;
}

FGuid ALDEconomyDirector::BusinessKey(const TArray<uint8>& Canonical)
{
    uint8 Digest[32];
    Terms(Canonical, Digest);
    uint32 Words[4];
    FMemory::Memcpy(Words, Digest, sizeof(Words));
    FGuid Key(Words[0], Words[1], Words[2], Words[3]);
    // A zero GUID is the invalid key; a real record's digest prefix is never accepted as one.
    if (!Key.IsValid())
    {
        Key.D = 1;
    }
    return Key;
}

void ALDEconomyDirector::Terms(const TArray<uint8>& Canonical, uint8 (&Out)[32])
{
    verifyf(SHA256(Canonical.GetData(), size_t(Canonical.Num()), Out) != nullptr, TEXT("SHA-256 of a growth business record failed."));
}

bool ALDEconomyDirector::SpawnDomain(ALDGameMode& InMode, const FActorSpawnParameters& Spawn, FString& Error)
{
    if (InMode.Economy || InMode.Development || InMode.Inventory || InMode.Vendor)
    {
        Error = TEXT("The growth domain already exists for this authority core.");
        return false;
    }
    UWorld* World = InMode.GetWorld();
    InMode.Economy = World->SpawnActor<ALDEconomyDirector>(Spawn);
    InMode.Development = World->SpawnActor<ALDDevelopmentDirector>(Spawn);
    InMode.Inventory = World->SpawnActor<ALDInventoryDirector>(Spawn);
    InMode.Vendor = World->SpawnActor<ALDVendorDirector>(Spawn);
    if (!InMode.Economy || !InMode.Development || !InMode.Inventory || !InMode.Vendor)
    {
        RetireDomain(InMode);
        Error = TEXT("The growth, inventory, vendor or economy director could not be created.");
        return false;
    }
    return true;
}

void ALDEconomyDirector::RetireDomain(ALDGameMode& InMode)
{
    if (InMode.Vendor) { InMode.Vendor->Destroy(); InMode.Vendor = nullptr; }
    if (InMode.Inventory) { InMode.Inventory->Destroy(); InMode.Inventory = nullptr; }
    if (InMode.Development) { InMode.Development->Destroy(); InMode.Development = nullptr; }
    if (InMode.Economy) { InMode.Economy->Destroy(); InMode.Economy = nullptr; }
}

bool ALDEconomyDirector::BeginMatch(ALDGameMode& InMode, const FLDMatchContext& InMatch, const TArray<FGuid>& InRoster, FString& Error)
{
    Error.Reset();
    if (!InMatch.IsValid() || InMatch != InMode.GetMatchContext() || InRoster.IsEmpty())
    {
        Error = TEXT("Growth initialization needs the current match and its locked roster.");
        return false;
    }
    if (!InMode.Development || !InMode.Inventory || !InMode.Vendor)
    {
        Error = TEXT("Growth initialization requires the spawned growth directors.");
        return false;
    }
    TSharedPtr<const FLDDemoContentSnapshot> Loaded;
    if (!LDDemoEntry::LoadRuntimeContent(Loaded, Error) || !Loaded)
    {
        if (Error.IsEmpty()) { Error = TEXT("The approved Demo catalog is unavailable for growth."); }
        return false;
    }
    if (Loaded->Get().Parameters.StartingGold < 0)
    {
        Error = TEXT("The approved starting gold is negative.");
        return false;
    }
    LDEconomy::FLedger Fresh;
    if (!Fresh.Initialize(InMatch.LedgerMatch, InRoster, Loaded->Get().Parameters.StartingGold, Error)
        || !InMode.Development->BeginMatch(InMatch, InRoster, Loaded, Error)
        || !InMode.Vendor->BeginMatch(InMatch, InRoster, Loaded, Error))
    {
        return false;
    }
    InMode.Inventory->BeginMatch(Loaded);
    Match = InMatch;
    Content = Loaded;
    Ledger = MoveTemp(Fresh);
    Roster = InRoster;
    ArrivalCounter = 0;
    Dirty = TSet<FGuid>(InRoster);
    Retired.Reset();
    PublishRevisions.Reset();
    ResolvedLifeThrough.Reset();
    RecentRewards.Reset();
#if WITH_DEV_AUTOMATION_TESTS
    DevGrants.Reset();
    if (LDDevGrowthFixture::IsSwitchPresent())
    {
        UE_LOG(LogTemp, Warning, TEXT("LD_DEV_FIXTURE_ACTIVE -LDDevGrowthFixture: generation=%u accepts DevQueueOffer and DevGrantReward on this authority"),
            Match.Generation);
    }
#endif
    return true;
}

void ALDEconomyDirector::RecordReward(const FLDRewardReceipt& Receipt)
{
    ResolvedLifeThrough.Add(TPair<uint8, FGuid>(uint8(Receipt.Key.Victim.Kind), Receipt.Key.Victim.EntityId), Receipt.Key.Victim.Life);
    if (RecentRewards.Num() == EconomyRewardWindow)
    {
        RecentRewards.RemoveAt(0, 1, EAllowShrinking::No);
    }
    RecentRewards.Add(Receipt);
}

bool ALDEconomyDirector::CompactLedger(FString& Error)
{
    if (!Ledger.IsInitialized() || Ledger.JournalTail().IsEmpty())
    {
        return true;
    }
    switch (Ledger.TryCompactResolvedPrefix(Ledger.CommittedSerial(), Error))
    {
    case LDEconomy::ECompactResult::Compacted:
    case LDEconomy::ECompactResult::NothingToCompact:
    case LDEconomy::ECompactResult::Pinned:
        Error.Reset();
        return true;
    default:
        return false;
    }
}

FLDRewardReceipt ALDEconomyDirector::CommitDeathReward(const FLDCommittedDeath& Death, const FLDGameplayWritePermit& Permit)
{
    FLDRewardReceipt Receipt;
    Receipt.Key = Death.Key;
    TArray<uint8> FactBytes;
    EconomyDeathFactBytes(Death, FactBytes);
    Terms(FactBytes, Receipt.Fingerprint);
    auto Finish = [&Receipt](ELDRewardOutcome Outcome, ELDGrowthReason Reason)
    {
        Receipt.Outcome = Outcome;
        Receipt.Reason = Reason;
        return Receipt;
    };

    ALDGameMode* M = Mode();
    if (!M || !M->Development || !M->Combat || !Content || !Ledger.IsInitialized()
        || M->ValidateWritePermit(Permit, ELDCommitPhase::DeathsAndRewards) != ELDCommitReject::None
        || Permit.GetBoundary().Match != Match)
    {
        return Finish(ELDRewardOutcome::PermanentlyRejected, ELDGrowthReason::NotAuthority);
    }
    if (!Death.Key.IsValid() || Death.Key.LedgerMatch != Match.LedgerMatch)
    {
        return Finish(ELDRewardOutcome::PermanentlyRejected, ELDGrowthReason::OldMatch);
    }
    const TPair<uint8, FGuid> Victim(uint8(Death.Key.Victim.Kind), Death.Key.Victim.EntityId);
    if (const uint32* Through = ResolvedLifeThrough.Find(Victim); Through && Death.Key.Victim.Life <= *Through)
    {
        const FLDRewardReceipt* Previous = RecentRewards.FindByPredicate(
            [&](const FLDRewardReceipt& Row) { return Row.Key == Death.Key; });
        if (!Previous)
        {
            return Finish(ELDRewardOutcome::PermanentlyRejected, ELDGrowthReason::StaleDeath);
        }
        if (FMemory::Memcmp(Previous->Fingerprint, Receipt.Fingerprint, sizeof(Receipt.Fingerprint)) != 0)
        {
            return Finish(ELDRewardOutcome::PermanentlyRejected, ELDGrowthReason::DeathConflict);
        }
        return *Previous;
    }

    const FLDRewardBasis& Basis = Death.RewardBasis;
    const bool bContenderVictim = Death.Key.Victim.Kind == ELDVictimKind::Contender;
    if (!Basis.bCreditOwnerEligibleAtDeath || !Death.CreditOwner.IsValid())
    {
        // A locked fact without an eligible owner is a handled death with no payout, never a later retry.
        if (Basis.bCreditOwnerEligibleAtDeath || Basis.NoRewardReason == ELDNoRewardReason::None)
        {
            return Finish(ELDRewardOutcome::PermanentlyRejected, ELDGrowthReason::DataInvalid);
        }
        Receipt.Recipient = Death.CreditOwner;
        Finish(ELDRewardOutcome::NoReward, EconomyNoRewardReason(Basis.NoRewardReason));
        RecordReward(Receipt);
        return Receipt;
    }
    const FGuid Recipient = Death.CreditOwner;
    const FLDGrowthBuild* RecipientBuild = M->Development->FindBuild(Recipient);
    const FLDProgressionState* RecipientProgression = M->Development->FindProgression(Recipient);
    const LDEconomy::FAccount* Account = Ledger.Account(Recipient);
    if (Basis.NoRewardReason != ELDNoRewardReason::None || !RecipientBuild || !RecipientProgression || !Account
        || (bContenderVictim && (Recipient == Death.VictimContender || Recipient == Death.Key.Victim.EntityId)))
    {
        return Finish(ELDRewardOutcome::PermanentlyRejected, ELDGrowthReason::DataInvalid);
    }

    LDProgression::FRewardAmounts Amounts;
    ELDGrowthReason AmountReason = ELDGrowthReason::None;
    if (bContenderVictim)
    {
        AmountReason = Basis.RewardDefinitionId == FName(*Content->Get().Character.Id)
            ? LDProgression::ContenderKillReward(Content->Get().Parameters, uint8(Content->Get().Progression.MaxLevel),
                Basis.VictimLevelAtDeath, Amounts)
            : ELDGrowthReason::DataInvalid;
    }
    else
    {
        AmountReason = LDProgression::MonsterKillReward(*Content, Basis.RewardDefinitionId, Amounts);
    }
    if (AmountReason != ELDGrowthReason::None)
    {
        return Finish(ELDRewardOutcome::PermanentlyRejected, AmountReason);
    }

    TArray<uint8> KeyBytes;
    EconomyDeathKeyBytes(Death.Key, KeyBytes);
    ELDGrowthReason Reason = ELDGrowthReason::None;
    const ELDRewardOutcome Outcome = CommitRewardParts(Recipient, Amounts, LDEconomy::ECause::DeathReward, KeyBytes,
        Receipt.Fingerprint, Permit, Receipt, Reason);
    if (Outcome != ELDRewardOutcome::Granted)
    {
        return Finish(Outcome, Reason);
    }
    Finish(ELDRewardOutcome::Granted, ELDGrowthReason::None);
    RecordReward(Receipt);
    return Receipt;
}

ELDRewardOutcome ALDEconomyDirector::CommitRewardParts(const FGuid& Recipient, const LDProgression::FRewardAmounts& Amounts,
    LDEconomy::ECause Cause, const TArray<uint8>& KeyBytes, const uint8 (&TransactionTerms)[32],
    const FLDGameplayWritePermit& Permit, FLDRewardReceipt& Receipt, ELDGrowthReason& Reason)
{
    Reason = ELDGrowthReason::None;
    ALDGameMode* M = Mode();
    FString MaintenanceError;
    if (!CompactLedger(MaintenanceError))
    {
        UE_LOG(LogTemp, Error, TEXT("LD_GROWTH_LEDGER_CORRUPT %s"), *MaintenanceError);
        Reason = ELDGrowthReason::AuthorityFault;
        return ELDRewardOutcome::PermanentlyRejected;
    }
    const FLDProgressionState* RecipientProgression = M->Development->FindProgression(Recipient);
    const LDEconomy::FAccount* Account = Ledger.Account(Recipient);
    if (!RecipientProgression || !Account)
    {
        Reason = ELDGrowthReason::DataInvalid;
        return ELDRewardOutcome::PermanentlyRejected;
    }
    Ledger.Pin();
    ON_SCOPE_EXIT { Ledger.Unpin(); };

    TOptional<LDEconomy::FPreparedCommit> LedgerPlan;
    if (Amounts.Gold > 0)
    {
        LDEconomy::FTransaction Transaction;
        Transaction.Cause = Cause;
        Transaction.BusinessKey = BusinessKey(KeyBytes);
        FMemory::Memcpy(Transaction.Terms, TransactionTerms, sizeof(Transaction.Terms));
        Transaction.CommitBatch = Permit.GetBoundary().BatchSerial;
        Transaction.Mint = Amounts.Gold;
        Transaction.Gold.Emplace(LDEconomy::FGoldChange{Recipient, Account->Revision, Amounts.Gold});
        LDEconomy::FPrepareResult Prepared = Ledger.Prepare(Transaction);
        if (!Prepared.IsSuccess())
        {
            return EconomyLedgerOutcome(Prepared.Error, Reason);
        }
        LedgerPlan.Emplace(MoveTemp(Prepared.Value.GetValue()));
    }

    FLDPrepareGrowthResult Growth = M->Development->PrepareExperience(Recipient, Amounts.XP);
    if (!Growth.IsSuccess())
    {
        Reason = Growth.Reason;
        return ELDRewardOutcome::PermanentlyRejected;
    }
    FLDPreparedGrowthChange& GrowthPlan = Growth.Value.GetValue();

    TOptional<FLDPreparedCombatProjection> CombatPlan;
    if (GrowthPlan.ChangesBuild())
    {
        FLDContenderLifeRead Life;
        FString LifeError;
        if (!M->ReadContenderLife(Match, Recipient, Life, LifeError))
        {
            Reason = ELDGrowthReason::StaleContext;
            return ELDRewardOutcome::PermanentlyRejected;
        }
        // A recipient that left after the locked fatal hit is settled without a future body to project.
        if (Life.Phase != ELDLifePhase::Left)
        {
            TArray<FLDEquippedItemValue> Equipped;
            ALDInventoryDirector::EquippedItems(Ledger.State(), Recipient, Equipped);
            const uint64 InventoryRevision = Ledger.InventoryRevision(Recipient);
            FLDExpectedBuildVersions Expected;
            if (!M->Development->ReadCombatInstalled(Recipient, Expected))
            {
                Reason = ELDGrowthReason::NotContender;
                return ELDRewardOutcome::PermanentlyRejected;
            }
            FLDCombatPrepareResult Projection = M->Combat->PrepareBuildChange(M->Development->MakeCaller(Recipient),
                M->Development->MakeProjectionInput(GrowthPlan.Build(), Equipped, InventoryRevision), Expected);
            if (!Projection.IsSuccess())
            {
                const ELDRewardOutcome Outcome = EconomyCombatOutcome(Projection.Error, Reason);
                UE_LOG(LogTemp, Warning, TEXT("LD_GROWTH_REWARD_PROJECTION %s %s"), LDGrowthWire::ReasonName(Reason), *Projection.Message);
                return Outcome;
            }
            CombatPlan.Emplace(MoveTemp(Projection.Value.GetValue()));
            GrowthPlan.SetCombatInstall({GrowthPlan.Build().Revision, InventoryRevision});
        }
    }
    if (M->ValidateWritePermit(Permit, Permit.GetPhase()) != ELDCommitReject::None)
    {
        Reason = ELDGrowthReason::NotAuthority;
        return ELDRewardOutcome::PermanentlyRejected;
    }

    // Every part is prepared; the composite install below cannot fail.
    Receipt.Recipient = Recipient;
    Receipt.GoldDelta = Amounts.Gold;
    Receipt.XPDelta = Amounts.XP;
    Receipt.LevelBefore = GrowthPlan.LevelBefore();
    Receipt.LevelAfter = GrowthPlan.Build().Level;
    Receipt.OpportunityFirst = uint8(RecipientProgression->EarnedThrough + 1);
    Receipt.OpportunityLast = GrowthPlan.Progression().EarnedThrough;
    Receipt.Revisions.bHasProgression = true;
    Receipt.Revisions.ProgressionRevision = GrowthPlan.Progression().Revision;
    Receipt.Revisions.bHasBuild = true;
    Receipt.Revisions.BuildRevision = GrowthPlan.Build().Revision;
    if (LedgerPlan.IsSet())
    {
        Receipt.bHasLedgerSerial = true;
        Receipt.LedgerSerial = LedgerPlan->Entry().Serial;
        Receipt.Revisions.bHasAccount = true;
        Receipt.Revisions.AccountRevision = LedgerPlan->Entry().ResultAccountRevision;
        Ledger.ApplyPrepared(MoveTemp(LedgerPlan.GetValue()));
    }
    M->Development->ApplyPreparedGrowth(MoveTemp(GrowthPlan), Permit);
    if (CombatPlan.IsSet())
    {
        M->Combat->ApplyBuildChange(MoveTemp(CombatPlan.GetValue()), Permit);
    }
    MarkDirty(Recipient);
    return ELDRewardOutcome::Granted;
}

bool ALDEconomyDirector::RetireLeftAccounts(ALDGameMode& M, const FLDGameplayWritePermit& Permit, FString& Error)
{
    for (const FGuid& Id : Roster)
    {
        if (Retired.Contains(Id))
        {
            continue;
        }
        FLDContenderLifeRead Life;
        FString LifeError;
        if (!M.ReadContenderLife(Match, Id, Life, LifeError) || Life.Phase != ELDLifePhase::Left)
        {
            continue;
        }
        // A death credited to him before he left is still owed; the account stays open until it settles
        // (its single retry runs at the next DeathsAndRewards, before the next Transactions retires it).
        if (M.Combat && M.Combat->OwesRewardTo(Id))
        {
            continue;
        }
        const LDEconomy::FAccount* Account = Ledger.Account(Id);
        if (!Account)
        {
            Error = TEXT("A contender that left has no ledger account to retire.");
            return false;
        }
        if (Account->bRetired)
        {
            Retired.Add(Id);
            continue;
        }
        TArray<uint8> KeyBytes;
        {
            FMemoryWriter Writer(KeyBytes);
            FString Tag = TEXT("account-retire");
            FGuid LedgerMatch = Match.LedgerMatch;
            FGuid Contender = Id;
            Writer << Tag << LedgerMatch << Contender;
        }
        LDEconomy::FTransaction Transaction;
        Transaction.Cause = LDEconomy::ECause::AccountRetire;
        Transaction.BusinessKey = BusinessKey(KeyBytes);
        Terms(KeyBytes, Transaction.Terms);
        Transaction.CommitBatch = Permit.GetBoundary().BatchSerial;
        Transaction.Retire.Emplace(LDEconomy::FRetireAccount{Id, Account->Revision, {Life.Life, Life.LifeRevision}});
        LDEconomy::FPrepareResult Prepared = Ledger.Prepare(Transaction);
        if (!Prepared.IsSuccess())
        {
            Error = FString::Printf(TEXT("Account retirement failed: %s"), *Prepared.Message);
            return false;
        }
        Ledger.ApplyPrepared(MoveTemp(Prepared.Value.GetValue()));
        Retired.Add(Id);
        MarkDirty(Id);
    }
    return true;
}

bool ALDEconomyDirector::ProcessTransactions(const FLDGameplayWritePermit& Permit, FString& Error)
{
    Error.Reset();
    ALDGameMode* M = Mode();
    if (!M || !M->Development || !M->Vendor || !Ledger.IsInitialized())
    {
        Error = TEXT("The growth transaction domain is not initialized for this match.");
        return false;
    }
    if (M->ValidateWritePermit(Permit, ELDCommitPhase::Transactions) != ELDCommitReject::None || Permit.GetBoundary().Match != Match)
    {
        Error = TEXT("Growth transactions require the current Transactions permit.");
        return false;
    }
    if (!CompactLedger(Error))
    {
        return false;
    }
#if WITH_DEV_AUTOMATION_TESTS
    if (!CommitDevGrants(Permit, Error))
    {
        return false;
    }
#endif
    TArray<FLDPendingGrowthIntent> Pending;
    M->Development->CollectPending(Pending);
    M->Vendor->CollectPending(Pending);
#if WITH_DEV_AUTOMATION_TESTS
    for (const TPair<FGuid, FDevTestRecord>& Entry : DevTests)
    {
        if (const auto* DevPending = Entry.Value.Stream.GetPending())
        {
            FLDPendingGrowthIntent& Intent = Pending.AddDefaulted_GetRef();
            Intent.Kind = FLDPendingGrowthIntent::EKind::DevTest;
            Intent.Contender = Entry.Key;
            Intent.Arrival = DevPending->Arrival;
        }
    }
#endif
    Pending.Sort([](const FLDPendingGrowthIntent& A, const FLDPendingGrowthIntent& B) { return A.Arrival < B.Arrival; });
    for (const FLDPendingGrowthIntent& Intent : Pending)
    {
        bool bFault = false;
        switch (Intent.Kind)
        {
        case FLDPendingGrowthIntent::EKind::Choice:
            bFault = EconomyIsFault(M->Development->CommitPendingChoice(Intent.Contender, Permit).Header.Reason);
            break;
        case FLDPendingGrowthIntent::EKind::ShopOpen:
            bFault = EconomyIsFault(M->Vendor->OpenPendingShop(Intent.Contender, Permit).Reason);
            break;
        case FLDPendingGrowthIntent::EKind::ShopCommand:
            bFault = EconomyIsFault(M->Vendor->CommitPendingShopCommand(Intent.Contender, Permit).Header.Reason);
            break;
#if WITH_DEV_AUTOMATION_TESTS
        case FLDPendingGrowthIntent::EKind::DevTest:
            if (!CommitPendingDevTest(Intent.Contender, Permit, Error))
            {
                return false;
            }
            break;
#endif
        }
        if (bFault)
        {
            Error = TEXT("A growth transaction lost its write permit inside the Transactions phase.");
            return false;
        }
        if (!CompactLedger(Error))
        {
            return false;
        }
    }
    return RetireLeftAccounts(*M, Permit, Error) && CompactLedger(Error);
}

bool ALDEconomyDirector::PublishGrowth(const FLDAuthorityBoundary& Boundary, FString& Error)
{
    Error.Reset();
    ALDGameMode* M = Mode();
    if (!M || !M->Development || !M->Vendor || !Ledger.IsInitialized())
    {
        Error = TEXT("The growth domain is not initialized for this match.");
        return false;
    }
    if (Boundary.Match != Match)
    {
        Error = TEXT("Growth publication received a boundary of another match.");
        return false;
    }
    if (Dirty.IsEmpty())
    {
        return true;
    }
    TMap<FGuid, ALDPlayerController*> Owners;
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        if (ALDPlayerController* Controller = Cast<ALDPlayerController>(It->Get()))
        {
            const FGuid Id = M->ContenderId(Controller);
            if (Id.IsValid())
            {
                Owners.Add(Id, Controller);
            }
        }
    }
    const TSet<FGuid> Changed = MoveTemp(Dirty);
    Dirty.Reset();
    for (const FGuid& Id : Changed)
    {
        uint64& Revision = PublishRevisions.FindOrAdd(Id);
        ++Revision;
        TArray<FLDChoiceReceipt> ChoiceReceipts;
        TArray<FLDShopReceipt> ShopReceipts;
        FLDShopOpenResult OpenResult;
        M->Development->TakeFinalReceipts(Id, ChoiceReceipts);
        M->Vendor->TakeFinalReceipts(Id, ShopReceipts);
        const bool bOpenResult = M->Vendor->TakeOpenResult(Id, OpenResult);
#if WITH_DEV_AUTOMATION_TESTS
        TArray<FLDDevTestReceipt> DevReceipts;
        if (FDevTestRecord* DevRecord = DevTests.Find(Id))
        {
            DevReceipts = MoveTemp(DevRecord->Unpublished);
            DevRecord->Unpublished.Reset();
        }
#endif
        ALDPlayerController* const* OwnerController = Owners.Find(Id);
        if (!OwnerController)
        {
            // Server AI reads the same results through the query entries.
            continue;
        }
        M->SyncOwner(*OwnerController);
#if WITH_DEV_AUTOMATION_TESTS
        // Final DEV receipts follow the owner refresh like the choice and shop receipts below.
        for (const FLDDevTestReceipt& DevReceipt : DevReceipts)
        {
            FLDDevTestResult Result;
            Result.Generation = DevReceipt.Header.Generation;
            Result.RequestId = DevReceipt.Header.RequestId;
            Result.Status = ELDAdmissionStatus::Final;
            Result.Reason = DevReceipt.Header.Reason;
            Result.bSequenceReserved = true;
            Result.bHasReceipt = true;
            Result.Receipt = DevReceipt;
            (*OwnerController)->ClientDevTestResult(Result);
        }
#endif
        for (const FLDChoiceReceipt& Choice : ChoiceReceipts)
        {
            FLDChoiceReceiptQueryResult Result;
            Result.Query.Generation = Choice.Header.Generation;
            Result.Query.Family = ELDGrowthFamily::Choice;
            Result.Query.RequestId = Choice.Header.RequestId;
            Result.Lookup = ELDReceiptLookup::Final;
            Result.bHasReceipt = true;
            Result.Receipt = Choice;
            (*OwnerController)->ClientChoiceReceipt(Result);
        }
        for (const FLDShopReceipt& Shop : ShopReceipts)
        {
            FLDShopReceiptQueryResult Result;
            Result.Query.Generation = Shop.Header.Generation;
            Result.Query.Family = ELDGrowthFamily::Shop;
            Result.Query.RequestId = Shop.Header.RequestId;
            Result.Lookup = ELDReceiptLookup::Final;
            Result.bHasReceipt = true;
            Result.Receipt = Shop;
            (*OwnerController)->ClientShopReceipt(Result);
        }
        if (bOpenResult)
        {
            (*OwnerController)->ClientShopOpenResult(OpenResult);
        }
    }
    return true;
}

FLDGrowthOwnerView ALDEconomyDirector::ReadOwnerView(const FGuid& ContenderId) const
{
    FLDGrowthOwnerView View;
    ALDGameMode* M = Mode();
    if (!M || !M->Development || !M->Inventory || !M->Vendor || !Ledger.IsInitialized())
    {
        return View;
    }
    View.Generation = Match.Generation;
    if (const uint64* Revision = PublishRevisions.Find(ContenderId))
    {
        View.PublishRevision = *Revision;
    }
    View.Progression = M->Development->ReadProgression(ContenderId);
    View.Equipment = M->Inventory->ReadEquipment(ContenderId);
    View.Shop = M->Vendor->ReadShop(ContenderId);
    return View;
}

bool ALDEconomyDirector::ReadFinalLevel(const FGuid& ContenderId, uint8& Level, FString& Error) const
{
    Level = 0;
    ALDGameMode* M = Mode();
    const FLDGrowthBuild* Build = M && M->Development ? M->Development->FindBuild(ContenderId) : nullptr;
    if (!Build)
    {
        Error = TEXT("The contender has no growth record in this match.");
        return false;
    }
    Level = Build->Level;
    return true;
}

#if WITH_DEV_AUTOMATION_TESTS
bool ALDEconomyDirector::ExportLedgerEvidence(FString& OutJson, FString& Error) const
{
    if (!Content)
    {
        OutJson.Reset();
        Error = TEXT("Growth has no approved catalog for this match.");
        return false;
    }
    return LDEconomy::ExportEvidence(Ledger, Content->Get().Parameters.StartingGold, OutJson, Error);
}

uint64 ALDEconomyDirector::DevGrantReward(const FGuid& ContenderId, int64 XP, int64 Gold, FString& Error)
{
    if (!LDDevGrowthFixture::Admit(GetWorld(), Error))
    {
        return 0;
    }
    ALDGameMode* M = Mode();
    if (!M || !M->Development || !Ledger.IsInitialized() || M->GetMatchContext() != Match || !M->IsGameplayWriteOpen(Match))
    {
        Error = TEXT("A DEV grant needs the current open match.");
        return 0;
    }
    if (!Ledger.Account(ContenderId) || !M->Development->FindBuild(ContenderId))
    {
        Error = TEXT("The DEV grant recipient is not a contender of this match.");
        return 0;
    }
    if (XP < 0 || Gold < 0 || (XP == 0 && Gold == 0))
    {
        Error = FString::Printf(TEXT("A DEV grant needs non-negative XP and gold, at least one of them positive (xp=%lld gold=%lld)."), XP, Gold);
        return 0;
    }
    FDevGrant& Grant = DevGrants.AddDefaulted_GetRef();
    Grant.Ticket = uint64(DevGrants.Num());
    Grant.Contender = ContenderId;
    Grant.XP = XP;
    Grant.Gold = Gold;
    UE_LOG(LogTemp, Warning, TEXT("LD_DEV_FIXTURE_ACTIVE queued grant ticket=%llu contender=%s xp=%lld gold=%lld"), Grant.Ticket,
        *ContenderId.ToString(), XP, Gold);
    return Grant.Ticket;
}

ELDDevGrantStatus ALDEconomyDirector::DevGrantStatus(uint64 Ticket, FString& Detail) const
{
    Detail.Reset();
    if (Ticket == 0 || Ticket > uint64(DevGrants.Num()))
    {
        Detail = TEXT("No DEV grant with this ticket in the current match.");
        return ELDDevGrantStatus::Unknown;
    }
    const FDevGrant& Grant = DevGrants[int32(Ticket - 1)];
    Detail = Grant.Detail;
    return Grant.Status;
}

bool ALDEconomyDirector::CommitDevGrants(const FLDGameplayWritePermit& Permit, FString& Error)
{
    for (FDevGrant& Grant : DevGrants)
    {
        if (Grant.Status != ELDDevGrantStatus::Pending)
        {
            continue;
        }
        // The grant's own identity: a DevFixture business record, never a death key.
        TArray<uint8> KeyBytes;
        {
            FMemoryWriter Writer(KeyBytes);
            FString Tag = TEXT("dev-fixture-grant");
            FGuid LedgerMatch = Match.LedgerMatch;
            uint64 Ticket = Grant.Ticket;
            FGuid Contender = Grant.Contender;
            int64 XP = Grant.XP;
            int64 Gold = Grant.Gold;
            Writer << Tag << LedgerMatch << Ticket << Contender << XP << Gold;
        }
        uint8 GrantTerms[32] = {};
        Terms(KeyBytes, GrantTerms);
        LDProgression::FRewardAmounts Amounts;
        Amounts.Gold = Grant.Gold;
        Amounts.XP = Grant.XP;
        FLDRewardReceipt Receipt;
        ELDGrowthReason Reason = ELDGrowthReason::None;
        const ELDRewardOutcome Outcome = CommitRewardParts(Grant.Contender, Amounts, LDEconomy::ECause::DevFixture, KeyBytes,
            GrantTerms, Permit, Receipt, Reason);
        if (Reason == ELDGrowthReason::AuthorityFault)
        {
            Error = TEXT("A DEV growth grant found a corrupt ledger inside the Transactions phase.");
            return false;
        }
        if (Outcome == ELDRewardOutcome::RetryableNotCommitted)
        {
            // Nothing was installed; the grant stays pending for the next Transactions phase, like a retried death reward.
            continue;
        }
        if (Outcome == ELDRewardOutcome::Granted)
        {
            Grant.Status = ELDDevGrantStatus::Granted;
            Grant.Detail = FString::Printf(TEXT("level %u->%u, opportunities %u..%u, xp +%lld, gold +%lld, ledger serial %llu"),
                uint32(Receipt.LevelBefore), uint32(Receipt.LevelAfter), uint32(Receipt.OpportunityFirst),
                uint32(Receipt.OpportunityLast), Receipt.XPDelta, Receipt.GoldDelta, Receipt.bHasLedgerSerial ? Receipt.LedgerSerial : 0);
        }
        else
        {
            Grant.Status = ELDDevGrantStatus::Rejected;
            Grant.Detail = FString::Printf(TEXT("outcome %u: %s"), uint32(Outcome), LDGrowthWire::ReasonName(Reason));
        }
        UE_LOG(LogTemp, Warning, TEXT("LD_DEV_FIXTURE_ACTIVE grant ticket=%llu contender=%s %s"), Grant.Ticket,
            *Grant.Contender.ToString(), *Grant.Detail);
        if (!CompactLedger(Error))
        {
            return false;
        }
    }
    return true;
}

FLDDevTestResult ALDEconomyDirector::QueueDevTest(const FGuid& ContenderId, const FLDDevTestCommand& Command)
{
    FString Error;
    if (!LDDevGrowthFixture::Admit(GetWorld(), Error))
    {
        return LDDevTest::Refused(Command, ELDGrowthReason::NotAuthority, Error);
    }
    return AdmitDevTest(ContenderId, Command);
}

FLDDevTestResult ALDEconomyDirector::AdmitDevTest(const FGuid& ContenderId, const FLDDevTestCommand& Command)
{
    const ELDGrowthReason Shape = LDDevTest::ValidateShape(Command);
    if (Shape != ELDGrowthReason::None)
    {
        return LDDevTest::Refused(Command, Shape, FString::Printf(
            TEXT("Op %u with amount %lld is not a DEV test command: grants need a positive amount, the other ops none."),
            uint32(Command.Op), Command.Amount));
    }
    ALDGameMode* M = Mode();
    if (!M || !M->Development || !Ledger.IsInitialized())
    {
        return LDDevTest::Refused(Command, ELDGrowthReason::NotAuthority, TEXT("The growth domain of this match is not initialized."));
    }
    if (!Match.IsValid() || Command.Generation != Match.Generation)
    {
        return LDDevTest::Refused(Command, ELDGrowthReason::OldMatch, TEXT("The command names another match."));
    }
    if (!Ledger.Account(ContenderId) || !M->Development->FindBuild(ContenderId))
    {
        return LDDevTest::Refused(Command, ELDGrowthReason::NotContender, TEXT("The caller is not a contender of this match."));
    }
    FDevTestRecord& Record = DevTests.FindOrAdd(ContenderId);
    // As for choices: only a request that opens the next sequence takes an arrival number and needs an open match;
    // replays of an accepted or final request still answer from the stream.
    const bool bOpensSequence = !Record.Stream.GetPending() && Command.RequestId == Record.Stream.GetFinalizedThrough() + 1;
    if (bOpensSequence && !M->IsGameplayWriteOpen(Match))
    {
        return LDDevTest::Refused(Command, ELDGrowthReason::MatchClosed, TEXT("The match no longer accepts gameplay writes."));
    }
    FLDPendingReceiptInfo Info;
    Info.OriginalLife = Command.Life;
    const uint64 Arrival = bOpensSequence ? NextArrival() : 0;
    ELDGrowthReason Reason = ELDGrowthReason::None;
    const FLDDevTestReceipt* Final = nullptr;
    bool bReserved = false;
    FLDDevTestResult Result;
    Result.Generation = Command.Generation;
    Result.RequestId = Command.RequestId;
    Result.Status = Record.Stream.Admit(Command, Command.RequestId, Info, Arrival, Reason, Final, bReserved);
    Result.Reason = LDGrowthWire::Raw(Reason);
    Result.bSequenceReserved = bReserved;
    if (Result.Status == ELDAdmissionStatus::Final && Final)
    {
        Result.bHasReceipt = true;
        Result.Receipt = *Final;
    }
    else if (Result.Status == ELDAdmissionStatus::AdmissionRejected)
    {
        Result.Detail = LDGrowthWire::ReasonName(Reason);
    }
    else if (bOpensSequence)
    {
        UE_LOG(LogTemp, Warning, TEXT("LD_DEV_FIXTURE_ACTIVE queued test command contender=%s request=%llu op=%s amount=%lld"),
            *ContenderId.ToString(), Command.RequestId, LDDevTest::OpName(Command.Op), Command.Amount);
    }
    return Result;
}

bool ALDEconomyDirector::CommitPendingDevTest(const FGuid& ContenderId, const FLDGameplayWritePermit& Permit, FString& Error)
{
    FDevTestRecord* Record = DevTests.Find(ContenderId);
    const auto* PendingEntry = Record ? Record->Stream.GetPending() : nullptr;
    ALDGameMode* M = Mode();
    if (!PendingEntry || !M || !M->Combat || !M->Development
        || M->ValidateWritePermit(Permit, ELDCommitPhase::Transactions) != ELDCommitReject::None
        || Permit.GetBoundary().Match != Match)
    {
        Error = TEXT("A DEV test command commit needs its pending command, the growth domain and the current Transactions permit.");
        return false;
    }
    // A copy: finalizing the stream releases the pending entry.
    const FLDDevTestCommand Command = PendingEntry->Command;
    FLDDevTestReceipt Receipt;
    Receipt.Header.Generation = Command.Generation;
    Receipt.Header.RequestId = Command.RequestId;
    Receipt.Header.OriginalLife = Command.Life;
    Receipt.Header.CommitBatch = Permit.GetBoundary().BatchSerial;
    Receipt.Op = Command.Op;
    Receipt.Amount = Command.Amount;
    ELDGrowthReason Reason = ELDGrowthReason::None;
    FString Detail;
    FLDContenderLifeRead Life;
    FString LifeError;
    if (Command.Generation != Match.Generation)
    {
        Reason = ELDGrowthReason::OldMatch;
    }
    else if (!M->ReadContenderLife(Match, ContenderId, Life, LifeError))
    {
        Reason = ELDGrowthReason::StaleContext;
        Detail = LifeError;
    }
    else if (Life.Phase == ELDLifePhase::Left)
    {
        Reason = ELDGrowthReason::LeftMatch;
    }
    else if (Command.Life != Life.Life)
    {
        // A command of an earlier life never applies to the next one.
        Reason = ELDGrowthReason::OldLife;
    }
    else
    {
        switch (Command.Op)
        {
        case ELDDevTestOp::GrantExperience:
        case ELDDevTestOp::GrantGold:
        {
            // The command's own business identity: a DevFixture record, never a death key or a fixture ticket.
            TArray<uint8> KeyBytes;
            {
                FMemoryWriter Writer(KeyBytes);
                FString Tag = TEXT("dev-test-grant");
                FGuid LedgerMatch = Match.LedgerMatch;
                FGuid Contender = ContenderId;
                uint64 RequestId = Command.RequestId;
                uint8 Op = uint8(Command.Op);
                int64 Amount = Command.Amount;
                Writer << Tag << LedgerMatch << Contender << RequestId << Op << Amount;
            }
            uint8 GrantTerms[32] = {};
            Terms(KeyBytes, GrantTerms);
            LDProgression::FRewardAmounts Amounts;
            (Command.Op == ELDDevTestOp::GrantGold ? Amounts.Gold : Amounts.XP) = Command.Amount;
            FLDRewardReceipt Reward;
            const ELDRewardOutcome Outcome = CommitRewardParts(ContenderId, Amounts, LDEconomy::ECause::DevFixture, KeyBytes,
                GrantTerms, Permit, Reward, Reason);
            if (Reason == ELDGrowthReason::AuthorityFault)
            {
                Error = TEXT("A DEV test grant found a corrupt ledger inside the Transactions phase.");
                return false;
            }
            if (Outcome == ELDRewardOutcome::RetryableNotCommitted)
            {
                // Nothing was installed; the command stays pending for the next Transactions phase.
                UE_LOG(LogTemp, Warning, TEXT("LD_DEV_FIXTURE_ACTIVE test command contender=%s request=%llu retries: %s"),
                    *ContenderId.ToString(), Command.RequestId, LDGrowthWire::ReasonName(Reason));
                return true;
            }
            if (Outcome == ELDRewardOutcome::Granted)
            {
                Reason = ELDGrowthReason::None;
                Detail = FString::Printf(TEXT("level %u->%u, xp +%lld, gold +%lld"), uint32(Reward.LevelBefore),
                    uint32(Reward.LevelAfter), Reward.XPDelta, Reward.GoldDelta);
            }
            else if (Reason == ELDGrowthReason::None)
            {
                Reason = ELDGrowthReason::DataInvalid;
            }
            break;
        }
        case ELDDevTestOp::RefillResources:
            if (Life.Phase != ELDLifePhase::Alive)
            {
                Reason = ELDGrowthReason::Dead;
            }
            else if (!M->Combat->DevRefillResources(ContenderId, Permit, Detail))
            {
                Reason = ELDGrowthReason::DataInvalid;
            }
            break;
        case ELDDevTestOp::ResetCooldowns:
            if (!M->Combat->DevResetCooldowns(ContenderId, Permit, Detail))
            {
                Reason = ELDGrowthReason::DataInvalid;
            }
            break;
        default:
            Reason = ELDGrowthReason::InvalidRequest;
            break;
        }
    }
    Receipt.Header.Outcome = Reason == ELDGrowthReason::None ? ELDReceiptOutcome::Applied : ELDReceiptOutcome::Rejected;
    Receipt.Header.Reason = LDGrowthWire::Raw(Reason);
    Receipt.Detail = Reason == ELDGrowthReason::None || !Detail.IsEmpty() ? Detail : FString(LDGrowthWire::ReasonName(Reason));
    Record->Stream.Finalize(Receipt);
    Record->Unpublished.Add(Receipt);
    MarkDirty(ContenderId);
    UE_LOG(LogTemp, Warning, TEXT("LD_DEV_FIXTURE_ACTIVE test command contender=%s request=%llu op=%s amount=%lld %s: %s"),
        *ContenderId.ToString(), Command.RequestId, LDDevTest::OpName(Command.Op), Command.Amount,
        Reason == ELDGrowthReason::None ? TEXT("applied") : TEXT("rejected"), *Receipt.Detail);
    return true;
}
#endif
