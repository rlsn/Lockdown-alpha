#pragma once

#include "CoreMinimal.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "Development/LDDemoContentDefinitions.h"
#include "LDGrowthProtocol.generated.h"

// Growth protocol (PROGRESSION_AND_REWARDS). Client-sent enums are decoded by an explicit whitelist; unknown values are
// rejected, never clamped to a neighbouring meaning. Retired values (the #144 shop family and trade reasons, the #198
// three-card choice family, rerolls and replacements) are never reused.

// Retired: 1 the three-card Choice family (#198), 2 the shop family (#144).
UENUM()
enum class ELDGrowthFamily : uint8 { None = 0, Build = 3 };

UENUM()
enum class ELDAdmissionStatus : uint8 { None = 0, AcceptedPending = 1, Final = 2, AdmissionRejected = 3 };

UENUM()
enum class ELDReceiptOutcome : uint8 { None = 0, Applied = 1, Rejected = 2 };

UENUM()
enum class ELDReceiptLookup : uint8 { None = 0, Pending = 1, Final = 2, NotFound = 3, ReceiptExpired = 4, InvalidContext = 5 };

// #198 build commands: choose one active of the current active offer, learn a pending blueprint into an empty Q/W/E slot,
// or allocate unspent points across the stats.
UENUM()
enum class ELDBuildCommandKind : uint8 { None = 0, ActiveChoice = 1, LearnBlueprint = 2, AllocatePoints = 3 };

// What an applied ActiveChoice or LearnBlueprint did with its active.
UENUM()
enum class ELDBuildChoiceResult : uint8 { None = 0, Learned = 1, StoredAsBlueprint = 2 };

enum class ELDRewardOutcome : uint8 { None = 0, Granted = 1, NoReward = 2, RetryableNotCommitted = 3, PermanentlyRejected = 4 };

// Travels as UPROPERTY uint16 Reason; read through LDGrowthWire::Reason. Retired numbers stay reserved: 40 NoOffer,
// 46 ReplacementRequired, 47 ReplacementMismatch, 49 NoReroll, 50 OfferAlreadyRerolled, 51 OpeningNoReroll (#198), and 48
// (ReplacementNotAllowed) for the rest-only replacement of #198 PR5.
enum class ELDGrowthReason : uint16
{
    None = 0,
    InvalidRequest = 1, OldMatch = 2, OldLife = 3, LifePhaseChanged = 4, LeftMatch = 5, MatchClosed = 6,
    NotContender = 7, CatalogMismatch = 8, NotAuthority = 9, Dead = 10,
    RequestConflict = 20, OutOfOrder = 21, ReceiptExpired = 22, RequestInFlight = 23,
    UnknownOffer = 41, InvalidOption = 42, OfferAlreadyCommitted = 43, OfferGuardStale = 44, BuildStale = 45,
    // #198: the build safe window (GROWTH.BuildSafeWindowSec) has not passed since the last combat action.
    NotSafe = 52,
    NoEmptySlot = 53, SlotOccupied = 54, NoBlueprint = 55, NotEnoughPoints = 56, NoActiveOffer = 57,
    // The match is outside the countdown and the development/capture phases.
    PhaseClosed = 58,
    DataNotReady = 78, StaleContext = 79,
    NumericUnderflow = 90, NumericDomain = 91, ArithmeticOverflow = 92, DataInvalid = 93, PrepareUnavailable = 94,
    AuthorityFault = 95, CapacityExceeded = 96,
    NoRewardNoOwner = 110, NoRewardSelf = 111, NoRewardOwnerLeft = 112, DeathConflict = 113, StaleDeath = 114
};

// Fixed 64-byte UTF-8 definition identity shared with Combat: at most 63 content bytes, NUL terminated,
// zero padded, restricted to the approved [a-z][a-z0-9_]* subset before an FName is constructed.
USTRUCT()
struct LOCKDOWN_API FLDDefinitionWire
{
    GENERATED_BODY()

    UPROPERTY() uint8 Bytes[64] = {};

    static bool Encode(FName Id, FLDDefinitionWire& Out);
    bool Decode(FName& Out) const;
    bool operator==(const FLDDefinitionWire& Other) const { return FMemory::Memcmp(Bytes, Other.Bytes, sizeof(Bytes)) == 0; }
};

USTRUCT()
struct LOCKDOWN_API FLDReceiptQuery
{
    GENERATED_BODY()

    UPROPERTY() uint32 Generation = 0;
    UPROPERTY() ELDGrowthFamily Family = ELDGrowthFamily::None;
    UPROPERTY() uint64 RequestId = 0;
};

USTRUCT()
struct LOCKDOWN_API FLDPendingReceiptInfo
{
    GENERATED_BODY()

    UPROPERTY() uint32 OriginalLife = 0;
    UPROPERTY() ELDLifePhase OriginalLifePhase = ELDLifePhase::Unspawned;
};

// Applied receipts carry committed versions; Rejected ones only carry current guards as a sync hint.
USTRUCT()
struct LOCKDOWN_API FLDResultRevisions
{
    GENERATED_BODY()

    UPROPERTY() bool bHasProgression = false;
    UPROPERTY() uint64 ProgressionRevision = 0;
    UPROPERTY() bool bHasBuild = false;
    UPROPERTY() uint64 BuildRevision = 0;
    UPROPERTY() bool bHasOffer = false;
    UPROPERTY() uint64 OfferRevision = 0;
};

USTRUCT()
struct LOCKDOWN_API FLDGrowthReceiptHeader
{
    GENERATED_BODY()

    UPROPERTY() uint32 Generation = 0;
    UPROPERTY() ELDGrowthFamily Family = ELDGrowthFamily::None;
    UPROPERTY() uint64 RequestId = 0;
    UPROPERTY() uint32 OriginalLife = 0;
    UPROPERTY() ELDLifePhase OriginalLifePhase = ELDLifePhase::Unspawned;
    UPROPERTY() ELDReceiptOutcome Outcome = ELDReceiptOutcome::None;
    UPROPERTY() uint16 Reason = 0;
    UPROPERTY() bool bHasCommitBatch = false;
    // Authority boundary batch that committed or rejected the request.
    UPROPERTY() uint64 CommitBatch = 0;
    UPROPERTY() FLDResultRevisions Revisions;
};

// ---- Build (#198) -----------------------------------------------------------------------------

USTRUCT()
struct LOCKDOWN_API FLDStatPointsView
{
    GENERATED_BODY()

    UPROPERTY() FName StatId;
    UPROPERTY() int32 Points = 0;
};

USTRUCT()
struct LOCKDOWN_API FLDActiveSlotView
{
    GENERATED_BODY()

    UPROPERTY() FName DefinitionId;
    // 0 = Q, 1 = W, 2 = E.
    UPROPERTY() uint8 Slot = 0;
};

USTRUCT()
struct LOCKDOWN_API FLDActiveOfferView
{
    GENERATED_BODY()

    UPROPERTY() FGuid OfferId;
    UPROPERTY() uint8 OpportunityOrdinal = 0;
    UPROPERTY() uint64 OfferRevision = 0;
    // One to three distinct active ids, immutable for the offer's lifetime.
    UPROPERTY() TArray<FName> Options;
};

// The owner's whole growth state. Opponents never receive it; they see only the public level and Q/W/E.
USTRUCT()
struct LOCKDOWN_API FLDProgressionView
{
    GENERATED_BODY()

    UPROPERTY() bool bValid = false;
    UPROPERTY() uint32 Generation = 0;
    UPROPERTY() uint32 Life = 0;
    UPROPERTY() ELDLifePhase LifePhase = ELDLifePhase::Unspawned;
    UPROPERTY() uint8 Level = 0;
    UPROPERTY() uint8 MaxLevel = 0;
    UPROPERTY() int64 CumulativeXP = 0;
    UPROPERTY() int64 LevelThresholdXP = 0;
    UPROPERTY() bool bHasNextThreshold = false;
    UPROPERTY() int64 NextThresholdXP = 0;
    UPROPERTY() uint64 ProgressionRevision = 0;
    UPROPERTY() uint64 BuildRevision = 0;
    // Earned (opening, levels and the max-level XP grants) minus allocated.
    UPROPERTY() int64 UnspentPoints = 0;
    // One row per stat, in the stats' design order (FLDDemoContentDefinitions::StatsInDesignOrder).
    UPROPERTY() TArray<FLDStatPointsView> StatPoints;
    // Learned Q/W/E actives, by slot.
    UPROPERTY() TArray<FLDActiveSlotView> Actives;
    // Chosen actives waiting to be learned (待学图纸), sorted by id.
    UPROPERTY() TArray<FName> PendingBlueprints;
    UPROPERTY() uint8 ActiveOpportunitiesEarned = 0;
    UPROPERTY() uint8 ActiveOpportunitiesConsumed = 0;
    // Earned and not yet chosen, the current offer's included; one without an eligible active waits without an offer.
    UPROPERTY() uint8 QueuedActiveOpportunities = 0;
    UPROPERTY() bool bHasActiveOffer = false;
    UPROPERTY() FLDActiveOfferView ActiveOffer;
    // Combat's build safe window (absolute authority ms; <= now is safe), copied at publication like the owner CombatView's.
    UPROPERTY() int64 BuildSafeAtMs = 0;
    UPROPERTY() FLDDemoContentDigest CatalogDigest;
};

USTRUCT()
struct LOCKDOWN_API FLDBuildCommand
{
    GENERATED_BODY()

    // Slot of an ActiveChoice without a preferred slot (the lowest empty one); every other kind leaves Slot here
    // except LearnBlueprint, which names 0..2.
    static constexpr uint8 AnySlot = 0xFF;

    UPROPERTY() uint32 Generation = 0;
    UPROPERTY() uint32 Life = 0;
    UPROPERTY() ELDLifePhase ExpectedLifePhase = ELDLifePhase::Unspawned;
    UPROPERTY() uint64 RequestId = 0;
    UPROPERTY() uint64 ExpectedBuildRevision = 0;
    UPROPERTY() FLDDemoContentDigest CatalogDigest;
    UPROPERTY() ELDBuildCommandKind Kind = ELDBuildCommandKind::None;
    // ActiveChoice: the offer and the chosen active of it.
    UPROPERTY() FGuid OfferId;
    UPROPERTY() uint64 OfferRevision = 0;
    UPROPERTY() FLDDefinitionWire Option;
    // ActiveChoice: learn it in the same commit when the contender may learn now and a slot is empty, else store it.
    UPROPERTY() bool bLearnIfPossible = false;
    // ActiveChoice with bLearnIfPossible: the preferred slot 0..2 or AnySlot; LearnBlueprint: the empty slot 0..2.
    UPROPERTY() uint8 Slot = AnySlot;
    // LearnBlueprint: the pending blueprint to learn.
    UPROPERTY() FLDDefinitionWire Blueprint;
    // AllocatePoints: one count per stat in the stats' design order, at least one point in total.
    UPROPERTY() TArray<uint8> PointCounts;

    bool SameTerms(const FLDBuildCommand& Other) const;
};

USTRUCT()
struct LOCKDOWN_API FLDBuildReceipt
{
    GENERATED_BODY()

    UPROPERTY() FLDGrowthReceiptHeader Header;
    UPROPERTY() ELDBuildCommandKind Kind = ELDBuildCommandKind::None;
    // ActiveChoice: the consumed offer and its opportunity.
    UPROPERTY() FGuid OfferId;
    UPROPERTY() uint8 OpportunityOrdinal = 0;
    // ActiveChoice: the chosen active; LearnBlueprint: the learned blueprint.
    UPROPERTY() FName DefinitionId;
    UPROPERTY() ELDBuildChoiceResult ChoiceResult = ELDBuildChoiceResult::None;
    // Learned: its slot (0 = Q, 1 = W, 2 = E).
    UPROPERTY() uint8 LearnedSlot = 0;
    // StoredAsBlueprint after bLearnIfPossible: why it was not learned (NotSafe, Dead or NoEmptySlot); None when the
    // command did not ask to learn.
    UPROPERTY() uint16 StoredReason = 0;
    // AllocatePoints: the points spent per stat, in the stats' design order.
    UPROPERTY() TArray<uint8> PointsSpent;
    UPROPERTY() int64 UnspentPointsAfter = 0;
    UPROPERTY() bool bHasNextOffer = false;
    UPROPERTY() FGuid NextOfferId;
};

USTRUCT()
struct LOCKDOWN_API FLDSubmitResult
{
    GENERATED_BODY()

    UPROPERTY() uint32 Generation = 0;
    UPROPERTY() ELDGrowthFamily Family = ELDGrowthFamily::None;
    UPROPERTY() uint64 RequestId = 0;
    UPROPERTY() ELDAdmissionStatus Status = ELDAdmissionStatus::None;
    UPROPERTY() uint16 Reason = 0;
    UPROPERTY() bool bSequenceReserved = false;
    UPROPERTY() bool bHasBuildReceipt = false;
    UPROPERTY() FLDBuildReceipt BuildReceipt;
};

USTRUCT()
struct LOCKDOWN_API FLDBuildReceiptQueryResult
{
    GENERATED_BODY()

    UPROPERTY() FLDReceiptQuery Query;
    UPROPERTY() ELDReceiptLookup Lookup = ELDReceiptLookup::None;
    UPROPERTY() bool bHasPending = false;
    UPROPERTY() FLDPendingReceiptInfo Pending;
    UPROPERTY() bool bHasReceipt = false;
    UPROPERTY() FLDBuildReceipt Receipt;
};

// One owner-only publication inside FLDOwnerState: level, XP, points, actives, blueprints and offer always move together.
USTRUCT()
struct LOCKDOWN_API FLDGrowthOwnerView
{
    GENERATED_BODY()

    UPROPERTY() uint32 Generation = 0;
    UPROPERTY() uint64 PublishRevision = 0;
    UPROPERTY() FLDProgressionView Progression;
    // D26: the serial of the current 天降外卖 when this contender claimed it; 0 otherwise. Owner-only: the public
    // view never names the claimer.
    UPROPERTY() uint64 AirdropClaimedSerial = 0;
};

// Local presentation notifications on the owning controller; listeners never write gameplay state.
DECLARE_MULTICAST_DELEGATE_OneParam(FLDOnGrowthSubmit, const FLDSubmitResult&);
DECLARE_MULTICAST_DELEGATE_OneParam(FLDOnBuildReceipt, const FLDBuildReceiptQueryResult&);

// ---- Server-internal death reward result ------------------------------------------------------

// One assister's part of a contender kill reward, committed in the same composite as the killer's part.
struct LOCKDOWN_API FLDRewardShareReceipt
{
    FGuid Recipient;
    int64 XPDelta = 0;
    uint8 LevelBefore = 0;
    uint8 LevelAfter = 0;
    uint8 OpportunityFirst = 1;
    uint8 OpportunityLast = 0;
};

struct LOCKDOWN_API FLDRewardReceipt
{
    FLDDeathKey Key;
    // SHA-256 of the canonical committed-death fact; a different fact under the same key conflicts.
    uint8 Fingerprint[32] = {};
    ELDRewardOutcome Outcome = ELDRewardOutcome::None;
    ELDGrowthReason Reason = ELDGrowthReason::None;
    // The killer (or the only recipient of a camp, map event or DEV reward); the fields below are their part.
    FGuid Recipient;
    int64 XPDelta = 0;
    uint8 LevelBefore = 0;
    uint8 LevelAfter = 0;
    // Active opportunity ordinals granted by this reward, inclusive; First > Last when none.
    uint8 OpportunityFirst = 1;
    uint8 OpportunityLast = 0;
    FLDResultRevisions Revisions;
    // Assisters of a contender kill in the fact's order, each with the part granted in the same commit.
    TArray<FLDRewardShareReceipt> Assists;
};

namespace LDGrowthWire
{
    inline ELDGrowthReason Reason(uint16 Raw) { return static_cast<ELDGrowthReason>(Raw); }
    inline uint16 Raw(ELDGrowthReason Reason) { return static_cast<uint16>(Reason); }
    LOCKDOWN_API bool IsKnownFamily(ELDGrowthFamily Family);
    // Only an Alive or Dead contender may send a build command; Unspawned/Left never can.
    LOCKDOWN_API bool IsBuildLifePhase(ELDLifePhase Phase);
    LOCKDOWN_API const TCHAR* ReasonName(ELDGrowthReason Reason);
}
