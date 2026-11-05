#include "Economy/LDEconomyDirector.h"

#include "Combat/LDAssistRules.h"
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
#include "World/LDCampDirector.h"

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
    int32 AssisterCount = Death.Assisters.Num();
    Writer << AssisterCount;
    for (FGuid Assister : Death.Assisters)
    {
        Writer << Assister;
    }
}

// Business key of one assister's mint of a contender death; the killer's mint keeps the death key itself.
void EconomyAssistKeyBytes(const FLDDeathKey& Key, const FGuid& Assister, TArray<uint8>& Out)
{
    Out.Reset();
    FMemoryWriter Writer(Out);
    FString Tag = TEXT("death-assist");
    FGuid LedgerMatch = Key.LedgerMatch;
    FGuid Recipient = Assister;
    Writer << Tag << LedgerMatch;
    EconomyWriteVictim(Writer, Key.Victim);
    Writer << Recipient;
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
        if (Basis.bCreditOwnerEligibleAtDeath || Basis.NoRewardReason == ELDNoRewardReason::None || !Death.Assisters.IsEmpty())
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
    // Assisters exist only for a contender victim: distinct contenders of this match other than the killer and victim.
    for (int32 Index = 0; Index < Death.Assisters.Num(); ++Index)
    {
        const FGuid& Assister = Death.Assisters[Index];
        if (!bContenderVictim || !Assister.IsValid() || Assister == Recipient || Assister == Death.Key.Victim.EntityId
            || (Index > 0 && !(Death.Assisters[Index - 1] < Assister))
            || !M->Development->FindProgression(Assister) || !Ledger.Account(Assister))
        {
            return Finish(ELDRewardOutcome::PermanentlyRejected, ELDGrowthReason::DataInvalid);
        }
    }

    LDProgression::FRewardAmounts Amounts;
    ELDGrowthReason AmountReason = ELDGrowthReason::None;
    int64 Bounty = 0;
    if (bContenderVictim)
    {
        AmountReason = Basis.RewardDefinitionId == FName(*Content->Get().Character.Id)
            ? LDProgression::ContenderKillReward(Content->Get().Parameters, uint8(Content->Get().Progression.MaxLevel),
                Basis.VictimLevelAtDeath, Amounts)
            : ELDGrowthReason::DataInvalid;
        // D27: the leader bounty locked at the fatal moment comes on top of the killer's normal kill gold. It is
        // extra bounty, so it stays out of the assist base (APP_SPEC: 不含额外赏金).
        Bounty = Basis.LeaderBountyGoldAtDeath;
        if (AmountReason == ELDGrowthReason::None
            && (Bounty < 0 || Bounty > Content->Get().Parameters.LeaderBountyGoldMax || Amounts.Gold > MAX_int64 - Bounty))
        {
            AmountReason = ELDGrowthReason::DataInvalid;
        }
    }
    else
    {
        AmountReason = LDProgression::MonsterKillReward(*Content, Basis.RewardDefinitionId, Amounts);
    }
    if (AmountReason != ELDGrowthReason::None)
    {
        return Finish(ELDRewardOutcome::PermanentlyRejected, AmountReason);
    }

    // The killer keeps the full kill gold; the assisters split the assist share and everyone splits the XP.
    FLDAssistRewardShares Split;
    if (!LDAssist::SplitKillReward(Amounts.Gold, Amounts.XP, Death.Assisters.Num(),
        bContenderVictim ? Content->Get().Parameters.AssistGoldBp : 0, Split))
    {
        return Finish(ELDRewardOutcome::PermanentlyRejected, ELDGrowthReason::ArithmeticOverflow);
    }
    TArray<FRewardShare> Shares;
    FRewardShare& Killer = Shares.AddDefaulted_GetRef();
    Killer.Recipient = Recipient;
    Killer.Amounts.Gold = Split.KillerGold + Bounty;
    Killer.Amounts.XP = Split.KillerXP;
    EconomyDeathKeyBytes(Death.Key, Killer.KeyBytes);
    for (const FGuid& Assister : Death.Assisters)
    {
        FRewardShare& Share = Shares.AddDefaulted_GetRef();
        Share.Recipient = Assister;
        Share.Amounts.Gold = Split.AssisterGold;
        Share.Amounts.XP = Split.AssisterXP;
        EconomyAssistKeyBytes(Death.Key, Assister, Share.KeyBytes);
    }
    ELDGrowthReason Reason = ELDGrowthReason::None;
    const ELDRewardOutcome Outcome = CommitRewardShares(Shares, LDEconomy::ECause::DeathReward, Receipt.Fingerprint,
        Permit, Receipt, Reason);
    if (Outcome != ELDRewardOutcome::Granted)
    {
        return Finish(Outcome, Reason);
    }
    Finish(ELDRewardOutcome::Granted, ELDGrowthReason::None);
    RecordReward(Receipt);
    return Receipt;
}

ELDRewardOutcome ALDEconomyDirector::CommitEventReward(const FGuid& Recipient, FName Event, uint64 Serial, int64 XP,
    int64 Gold, const FLDGameplayWritePermit& Permit, FLDRewardReceipt& Receipt, ELDGrowthReason& Reason)
{
    Reason = ELDGrowthReason::None;
    if (!Recipient.IsValid() || Event.IsNone() || !Serial || XP < 0 || Gold < 0 || (XP == 0 && Gold == 0))
    {
        Reason = ELDGrowthReason::DataInvalid;
        return ELDRewardOutcome::PermanentlyRejected;
    }
    // The business key is the event alone, so the ledger itself pays each event once whoever claims it; the recipient
    // and amounts are its terms, and a second claimant of the same event is a conflicting reuse of the key.
    TArray<uint8> KeyBytes, TermBytes;
    {
        FMemoryWriter Writer(KeyBytes);
        FString Tag = TEXT("map-event-reward");
        FString EventName = Event.ToString();
        FGuid LedgerMatch = Match.LedgerMatch;
        Writer << Tag << LedgerMatch << EventName << Serial;
    }
    {
        TermBytes = KeyBytes;
        FMemoryWriter Writer(TermBytes, false, true);
        FGuid Contender = Recipient;
        int64 TermXP = XP, TermGold = Gold;
        Writer << Contender << TermXP << TermGold;
    }
    uint8 EventTerms[32] = {};
    Terms(TermBytes, EventTerms);
    LDProgression::FRewardAmounts Amounts;
    Amounts.XP = XP;
    Amounts.Gold = Gold;
    return CommitRewardParts(Recipient, Amounts, LDEconomy::ECause::MapEventReward, KeyBytes, EventTerms, Permit, Receipt, Reason);
}

ELDRewardOutcome ALDEconomyDirector::CommitRewardParts(const FGuid& Recipient, const LDProgression::FRewardAmounts& Amounts,
    LDEconomy::ECause Cause, const TArray<uint8>& KeyBytes, const uint8 (&TransactionTerms)[32],
    const FLDGameplayWritePermit& Permit, FLDRewardReceipt& Receipt, ELDGrowthReason& Reason)
{
    TArray<FRewardShare> Shares;
    FRewardShare& Share = Shares.AddDefaulted_GetRef();
    Share.Recipient = Recipient;
    Share.Amounts = Amounts;
    Share.KeyBytes = KeyBytes;
    return CommitRewardShares(Shares, Cause, TransactionTerms, Permit, Receipt, Reason);
}

ELDRewardOutcome ALDEconomyDirector::CommitRewardShares(const TArray<FRewardShare>& Shares, LDEconomy::ECause Cause,
    const uint8 (&TransactionTerms)[32], const FLDGameplayWritePermit& Permit, FLDRewardReceipt& Receipt,
    ELDGrowthReason& Reason)
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
    TSet<FGuid> Seen;
    for (const FRewardShare& Share : Shares)
    {
        bool bDuplicate = false;
        Seen.Add(Share.Recipient, &bDuplicate);
        if (bDuplicate || Share.Amounts.Gold < 0 || Share.Amounts.XP < 0
            || !M->Development->FindProgression(Share.Recipient) || !Ledger.Account(Share.Recipient))
        {
            Reason = ELDGrowthReason::DataInvalid;
            return ELDRewardOutcome::PermanentlyRejected;
        }
    }
    if (Shares.IsEmpty())
    {
        Reason = ELDGrowthReason::DataInvalid;
        return ELDRewardOutcome::PermanentlyRejected;
    }
    Ledger.Pin();
    ON_SCOPE_EXIT { Ledger.Unpin(); };

    // Each recipient's mint is its own single-account entry, chained after the previous one of this composite.
    TArray<TOptional<LDEconomy::FPreparedCommit>> LedgerPlans;
    LedgerPlans.Reserve(Shares.Num());
    TArray<int32> LedgerPlanOf;
    const LDEconomy::FPreparedCommit* Previous = nullptr;
    for (const FRewardShare& Share : Shares)
    {
        LedgerPlanOf.Add(INDEX_NONE);
        if (Share.Amounts.Gold <= 0)
        {
            continue;
        }
        const LDEconomy::FAccount* Account = Ledger.Account(Share.Recipient);
        LDEconomy::FTransaction Transaction;
        Transaction.Cause = Cause;
        Transaction.BusinessKey = BusinessKey(Share.KeyBytes);
        FMemory::Memcpy(Transaction.Terms, TransactionTerms, sizeof(Transaction.Terms));
        Transaction.CommitBatch = Permit.GetBoundary().BatchSerial;
        Transaction.Mint = Share.Amounts.Gold;
        Transaction.Gold.Emplace(LDEconomy::FGoldChange{Share.Recipient, Account->Revision, Share.Amounts.Gold});
        LDEconomy::FPrepareResult Prepared = Previous ? Ledger.PrepareAfter(*Previous, Transaction) : Ledger.Prepare(Transaction);
        if (!Prepared.IsSuccess())
        {
            return EconomyLedgerOutcome(Prepared.Error, Reason);
        }
        LedgerPlanOf.Last() = LedgerPlans.Num();
        LedgerPlans.Emplace(MoveTemp(Prepared.Value.GetValue()));
        Previous = &LedgerPlans.Last().GetValue();
    }

    TArray<TOptional<FLDPreparedGrowthChange>> GrowthPlans;
    TArray<TOptional<FLDPreparedCombatProjection>> CombatPlans;
    for (const FRewardShare& Share : Shares)
    {
        FLDPrepareGrowthResult Growth = M->Development->PrepareExperience(Share.Recipient, Share.Amounts.XP);
        if (!Growth.IsSuccess())
        {
            Reason = Growth.Reason;
            return ELDRewardOutcome::PermanentlyRejected;
        }
        FLDPreparedGrowthChange& GrowthPlan = Growth.Value.GetValue();
        TOptional<FLDPreparedCombatProjection>& CombatPlan = CombatPlans.AddDefaulted_GetRef();
        if (GrowthPlan.ChangesBuild())
        {
            FLDContenderLifeRead Life;
            FString LifeError;
            if (!M->ReadContenderLife(Match, Share.Recipient, Life, LifeError))
            {
                Reason = ELDGrowthReason::StaleContext;
                return ELDRewardOutcome::PermanentlyRejected;
            }
            // A recipient that left after the locked fatal hit is settled without a future body to project.
            if (Life.Phase != ELDLifePhase::Left)
            {
                TArray<FLDEquippedItemValue> Equipped;
                ALDInventoryDirector::EquippedItems(Ledger.State(), Share.Recipient, Equipped);
                const uint64 InventoryRevision = Ledger.InventoryRevision(Share.Recipient);
                FLDExpectedBuildVersions Expected;
                if (!M->Development->ReadCombatInstalled(Share.Recipient, Expected))
                {
                    Reason = ELDGrowthReason::NotContender;
                    return ELDRewardOutcome::PermanentlyRejected;
                }
                FLDCombatPrepareResult Projection = M->Combat->PrepareBuildChange(M->Development->MakeCaller(Share.Recipient),
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
        GrowthPlans.Emplace(MoveTemp(GrowthPlan));
    }
    if (M->ValidateWritePermit(Permit, Permit.GetPhase()) != ELDCommitReject::None)
    {
        Reason = ELDGrowthReason::NotAuthority;
        return ELDRewardOutcome::PermanentlyRejected;
    }

    // Every part of every recipient is prepared; the composite install below cannot fail.
    Receipt.Assists.Reset();
    for (int32 Index = 0; Index < Shares.Num(); ++Index)
    {
        const FRewardShare& Share = Shares[Index];
        const FLDPreparedGrowthChange& GrowthPlan = GrowthPlans[Index].GetValue();
        const FLDProgressionState* Progression = M->Development->FindProgression(Share.Recipient);
        const LDEconomy::FPreparedCommit* LedgerPlan = LedgerPlanOf[Index] != INDEX_NONE
            ? &LedgerPlans[LedgerPlanOf[Index]].GetValue() : nullptr;
        if (Index == 0)
        {
            Receipt.Recipient = Share.Recipient;
            Receipt.GoldDelta = Share.Amounts.Gold;
            Receipt.XPDelta = Share.Amounts.XP;
            Receipt.LevelBefore = GrowthPlan.LevelBefore();
            Receipt.LevelAfter = GrowthPlan.Build().Level;
            Receipt.OpportunityFirst = uint8(Progression->EarnedThrough + 1);
            Receipt.OpportunityLast = GrowthPlan.Progression().EarnedThrough;
            Receipt.Revisions.bHasProgression = true;
            Receipt.Revisions.ProgressionRevision = GrowthPlan.Progression().Revision;
            Receipt.Revisions.bHasBuild = true;
            Receipt.Revisions.BuildRevision = GrowthPlan.Build().Revision;
            if (LedgerPlan)
            {
                Receipt.bHasLedgerSerial = true;
                Receipt.LedgerSerial = LedgerPlan->Entry().Serial;
                Receipt.Revisions.bHasAccount = true;
                Receipt.Revisions.AccountRevision = LedgerPlan->Entry().ResultAccountRevision;
            }
            continue;
        }
        FLDRewardShareReceipt& Part = Receipt.Assists.AddDefaulted_GetRef();
        Part.Recipient = Share.Recipient;
        Part.GoldDelta = Share.Amounts.Gold;
        Part.XPDelta = Share.Amounts.XP;
        Part.LevelBefore = GrowthPlan.LevelBefore();
        Part.LevelAfter = GrowthPlan.Build().Level;
        Part.OpportunityFirst = uint8(Progression->EarnedThrough + 1);
        Part.OpportunityLast = GrowthPlan.Progression().EarnedThrough;
        if (LedgerPlan)
        {
            Part.bHasLedgerSerial = true;
            Part.LedgerSerial = LedgerPlan->Entry().Serial;
        }
    }
    for (TOptional<LDEconomy::FPreparedCommit>& LedgerPlan : LedgerPlans)
    {
        Ledger.ApplyPrepared(MoveTemp(LedgerPlan.GetValue()));
    }
    for (int32 Index = 0; Index < Shares.Num(); ++Index)
    {
        M->Development->ApplyPreparedGrowth(MoveTemp(GrowthPlans[Index].GetValue()), Permit);
        if (CombatPlans[Index].IsSet())
        {
            M->Combat->ApplyBuildChange(MoveTemp(CombatPlans[Index].GetValue()), Permit);
        }
        MarkDirty(Shares[Index].Recipient);
    }
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
    const FLDAirdropState& Drop = M->GetAirdrop();
    View.AirdropClaimedSerial = Drop.Phase == ELDAirdropPhase::Claimed && Drop.ClaimedBy == ContenderId ? Drop.Serial : 0;
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

FLDDevTestResult ALDEconomyDirector::QueueDevTest(const FGuid& ContenderId, const FLDDevTestCommand& Command, bool bHostConnection)
{
    FString Error;
    if (!LDDevGrowthFixture::Admit(GetWorld(), Error))
    {
        return LDDevTest::Refused(Command, ELDGrowthReason::NotAuthority, Error);
    }
    return AdmitDevTest(ContenderId, Command, bHostConnection);
}

FLDDevTestResult ALDEconomyDirector::AdmitDevTest(const FGuid& ContenderId, const FLDDevTestCommand& Command, bool bHostConnection)
{
    const ELDGrowthReason Shape = LDDevTest::ValidateShape(Command);
    if (Shape != ELDGrowthReason::None)
    {
        return LDDevTest::Refused(Command, Shape, FString::Printf(
            TEXT("Op %u with amount %lld is not a DEV test command: grants need a positive amount, the other ops none."),
            uint32(Command.Op), Command.Amount));
    }
    if (LDDevTest::IsMatchWide(Command.Op) && !bHostConnection)
    {
        return LDDevTest::Refused(Command, ELDGrowthReason::NotAuthority,
            TEXT("Camp refreshes and an early capture opening change the match for everyone; only the Listen Host may send them."));
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
    if (!PendingEntry || !M || !M->Combat || !M->Development || !M->Vendor
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
    Receipt.Slot = Command.Slot;
    Receipt.Stat = Command.Stat;
    // The shape was proven at admission: a learn or buy names a valid definition, every other op none.
    FName Definition;
    Command.Definition.Decode(Definition);
    Receipt.Definition = Definition;
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
        case ELDDevTestOp::LearnOption:
            // Like a choice: Alive or Dead may consume an opportunity.
            Reason = LDGrowthWire::IsChoicePhase(Life.Phase)
                ? M->Development->DevChooseOption(ContenderId, Definition, Command.Slot, Permit, Detail)
                : ELDGrowthReason::LifePhaseChanged;
            break;
        case ELDDevTestOp::BuyItem:
        case ELDDevTestOp::SellItem:
            // Like a shop command: only a living contender trades.
            Reason = Life.Phase == ELDLifePhase::Alive
                ? M->Vendor->DevTrade(ContenderId, Command.Op == ELDDevTestOp::BuyItem, Definition, Command.Slot,
                    Command.RequestId, Permit, Detail)
                : ELDGrowthReason::Dead;
            break;
        case ELDDevTestOp::ResetToMatchStart:
        {
            // Like a choice: Alive or Dead; before the first life there is nothing to reset in Combat.
            if (!LDGrowthWire::IsChoicePhase(Life.Phase))
            {
                Reason = ELDGrowthReason::LifePhaseChanged;
                break;
            }
            bool bRetry = false;
            Reason = CommitDevReset(ContenderId, Command, Permit, Detail, bRetry);
            if (bRetry)
            {
                // Nothing was installed; the command stays pending for the next Transactions phase.
                UE_LOG(LogTemp, Warning, TEXT("LD_DEV_FIXTURE_ACTIVE test command contender=%s request=%llu retries: %s"),
                    *ContenderId.ToString(), Command.RequestId, *Detail);
                return true;
            }
            break;
        }
        case ELDDevTestOp::RefreshCamps:
        {
            int32 Waiting = 0;
            if (!M->Camps)
            {
                Reason = ELDGrowthReason::DataNotReady;
                Detail = TEXT("This match has no camp director.");
            }
            else if (!M->Camps->DevRefreshCleared(Permit, Waiting, Detail))
            {
                Reason = ELDGrowthReason::AuthorityFault;
            }
            else
            {
                Detail = FString::Printf(TEXT("%d cleared camps refresh at the next boundary"), Waiting);
            }
            break;
        }
        case ELDDevTestOp::OpenCapture:
        {
            bool bOutsideDevelopment = false;
            if (!M->DevOpenCaptureNow(Permit, bOutsideDevelopment, Detail))
            {
                Reason = bOutsideDevelopment ? ELDGrowthReason::StaleContext : ELDGrowthReason::DataInvalid;
            }
            break;
        }
        case ELDDevTestOp::SetAttribute:
        case ELDDevTestOp::ClearAttributes:
            // A persistent build term: Combat installs it on the living body or keeps it for the next life.
            Reason = M->Combat->DevSetAttribute(ContenderId, Command.Op == ELDDevTestOp::ClearAttributes,
                ELDDemoBonusStat(Command.Stat), Command.Amount, Permit, Detail);
            break;
        default:
            Reason = ELDGrowthReason::InvalidRequest;
            break;
        }
    }
    if (Reason == ELDGrowthReason::AuthorityFault)
    {
        Error = FString::Printf(TEXT("A DEV test command lost its growth domain or permit inside the Transactions phase: %s"), *Detail);
        return false;
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

ELDGrowthReason ALDEconomyDirector::CommitDevReset(const FGuid& ContenderId, const FLDDevTestCommand& Command,
    const FLDGameplayWritePermit& Permit, FString& Detail, bool& bRetry)
{
    bRetry = false;
    ALDGameMode* M = Mode();
    const LDEconomy::FAccount* Account = Ledger.Account(ContenderId);
    if (!M || !M->Development || !M->Combat || !Content.IsValid())
    {
        Detail = TEXT("A DEV reset needs the growth domain.");
        return ELDGrowthReason::AuthorityFault;
    }
    if (!Account)
    {
        Detail = TEXT("The contender has no ledger account in this match.");
        return ELDGrowthReason::DataInvalid;
    }
    const int64 StartingGold = Content->Get().Parameters.StartingGold;
    const int64 GoldBefore = Account->Gold;
    const uint8 LevelBefore = M->Development->FindBuild(ContenderId) ? M->Development->FindBuild(ContenderId)->Level : 0;

    // Ledger part, rehearsed on a copy so that every entry is known to commit before anything changes.
    TArray<LDEconomy::FTransaction> Parts;
    LDEconomy::FLedger Rehearsal = Ledger;
    const auto Rehearse = [&](LDEconomy::FTransaction& Part) -> ELDGrowthReason
    {
        TArray<uint8> KeyBytes;
        {
            FMemoryWriter Writer(KeyBytes);
            FString Tag = TEXT("dev-test-reset");
            FGuid LedgerMatch = Match.LedgerMatch;
            FGuid Contender = ContenderId;
            uint64 RequestId = Command.RequestId;
            uint8 Index = uint8(Parts.Num());
            Writer << Tag << LedgerMatch << Contender << RequestId << Index;
        }
        Part.Cause = LDEconomy::ECause::DevReset;
        Part.BusinessKey = BusinessKey(KeyBytes);
        Terms(KeyBytes, Part.Terms);
        Part.CommitBatch = Permit.GetBoundary().BatchSerial;
        LDEconomy::FPrepareResult Prepared = Rehearsal.Prepare(Part);
        if (!Prepared.IsSuccess())
        {
            bRetry = Prepared.Error == LDEconomy::EPrepareError::JournalFull;
            Detail = FString::Printf(TEXT("ledger %s: %s"), LDEconomy::FLedger::ErrorName(Prepared.Error), *Prepared.Message);
            return ELDGrowthReason::DataInvalid;
        }
        Rehearsal.ApplyPrepared(MoveTemp(Prepared.Value.GetValue()));
        Parts.Add(Part);
        return ELDGrowthReason::None;
    };
    TArray<TPair<FGuid, LDEconomy::FItem>> Owned;
    Ledger.OwnerItems(ContenderId, Owned);
    for (const TPair<FGuid, LDEconomy::FItem>& Pair : Owned)
    {
        LDEconomy::FTransaction Part;
        LDEconomy::FConsumeItem Consume;
        Consume.Instance = Pair.Key;
        Consume.ExpectedItemRevision = Pair.Value.Revision;
        Consume.Owner = ContenderId;
        Consume.ExpectedInventoryRevision = Rehearsal.InventoryRevision(ContenderId);
        Part.Consume.Emplace(Consume);
        if (const ELDGrowthReason Refused = Rehearse(Part); Refused != ELDGrowthReason::None)
        {
            return Refused;
        }
    }
    if (GoldBefore != StartingGold)
    {
        LDEconomy::FTransaction Part;
        LDEconomy::FGoldChange Gold;
        Gold.Account = ContenderId;
        Gold.ExpectedRevision = Rehearsal.Account(ContenderId)->Revision;
        Gold.Delta = StartingGold - GoldBefore;
        Part.Mint = FMath::Max<int64>(Gold.Delta, 0);
        Part.Burn = FMath::Max<int64>(-Gold.Delta, 0);
        Part.Gold.Emplace(Gold);
        if (const ELDGrowthReason Refused = Rehearse(Part); Refused != ELDGrowthReason::None)
        {
            return Refused;
        }
    }
    const uint64 ResultInventoryRevision = Rehearsal.InventoryRevision(ContenderId);

    // Growth and Combat against the ledger's results.
    FLDPrepareGrowthResult Growth = M->Development->DevPrepareReset(ContenderId);
    if (!Growth.IsSuccess())
    {
        Detail = Growth.Message;
        return Growth.Reason != ELDGrowthReason::None ? Growth.Reason : ELDGrowthReason::PrepareUnavailable;
    }
    const uint64 ResultBuildRevision = Growth.Value->Build().Revision;
    FLDCombatPrepareResult Combat = M->Combat->DevPrepareReset(ContenderId, ResultBuildRevision, ResultInventoryRevision);
    if (!Combat.IsSuccess())
    {
        Detail = Combat.Message;
        return ELDGrowthReason::DataInvalid;
    }
    if (M->ValidateWritePermit(Permit, ELDCommitPhase::Transactions) != ELDCommitReject::None)
    {
        Detail = TEXT("The Transactions permit expired inside the reset.");
        return ELDGrowthReason::AuthorityFault;
    }

    // Install. Every ledger entry was rehearsed against the same state; a refusal now is a broken contract.
    Ledger.Pin();
    for (const LDEconomy::FTransaction& Part : Parts)
    {
        LDEconomy::FPrepareResult Prepared = Ledger.Prepare(Part);
        if (!Prepared.IsSuccess())
        {
            Ledger.Unpin();
            Detail = FString::Printf(TEXT("the rehearsed ledger entry was refused: %s"), *Prepared.Message);
            return ELDGrowthReason::AuthorityFault;
        }
        Ledger.ApplyPrepared(MoveTemp(Prepared.Value.GetValue()));
    }
    Ledger.Unpin();
    FLDExpectedBuildVersions Install;
    Install.BuildRevision = ResultBuildRevision;
    Install.InventoryRevision = ResultInventoryRevision;
    Growth.Value->SetCombatInstall(Install);
    M->Development->ApplyPreparedGrowth(MoveTemp(Growth.Value.GetValue()), Permit);
    M->Combat->ApplyBuildChange(MoveTemp(Combat.Value.GetValue()), Permit);
    const int32 Dropped = M->Combat->DevFinishReset(ContenderId, Permit.GetBoundary().AtMs);
    MarkDirty(ContenderId);
    Detail = FString::Printf(TEXT("level %u->1, gold %lld->%lld, %d items removed, %d cooldown tracks dropped, %d DevReset ledger entries"),
        uint32(LevelBefore), GoldBefore, StartingGold, Owned.Num(), Dropped, Parts.Num());
    return ELDGrowthReason::None;
}
#endif
