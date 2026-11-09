#pragma once

#include "CoreMinimal.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "Development/LDDemoContentDefinitions.h"
#include "LDGrowthProtocol.generated.h"

// Growth protocol (PROGRESSION_AND_REWARDS). Client-sent enums are decoded by an explicit whitelist; unknown values are
// rejected, never clamped to a neighbouring meaning. Retired values (the #144 shop family and trade reasons) are never
// reused.

UENUM()
enum class ELDGrowthFamily : uint8 { None = 0, Choice = 1 };

UENUM()
enum class ELDAdmissionStatus : uint8 { None = 0, AcceptedPending = 1, Final = 2, AdmissionRejected = 3 };

UENUM()
enum class ELDReceiptOutcome : uint8 { None = 0, Applied = 1, Rejected = 2 };

UENUM()
enum class ELDReceiptLookup : uint8 { None = 0, Pending = 1, Final = 2, NotFound = 3, ReceiptExpired = 4, InvalidContext = 5 };

// Opening (#153): an opening direction card (DefinitionId) that also teaches one hidden draftable active, revealed only
// by the applied receipt; only ever opportunity 1.
UENUM()
enum class ELDChoiceOptionKind : uint8 { None = 0, LearnActive = 1, UpgradeActive = 2, LearnPassive = 3, UpgradePassive = 4, Attribute = 5, Opening = 6 };

enum class ELDRewardOutcome : uint8 { None = 0, Granted = 1, NoReward = 2, RetryableNotCommitted = 3, PermanentlyRejected = 4 };

// Travels as UPROPERTY uint16 Reason; read through LDGrowthWire::Reason.
enum class ELDGrowthReason : uint16
{
    None = 0,
    InvalidRequest = 1, OldMatch = 2, OldLife = 3, LifePhaseChanged = 4, LeftMatch = 5, MatchClosed = 6,
    NotContender = 7, CatalogMismatch = 8, NotAuthority = 9, Dead = 10,
    RequestConflict = 20, OutOfOrder = 21, ReceiptExpired = 22, RequestInFlight = 23,
    NoOffer = 40, UnknownOffer = 41, InvalidOption = 42, OfferAlreadyCommitted = 43, OfferGuardStale = 44,
    BuildStale = 45, ReplacementRequired = 46, ReplacementMismatch = 47, ReplacementNotAllowed = 48,
    NoReroll = 49, OfferAlreadyRerolled = 50,
    // #153 D9: the opening direction offer is never rerolled, not even with a token earned at a reroll level.
    OpeningNoReroll = 51,
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

// ---- Choice -----------------------------------------------------------------------------------

USTRUCT()
struct LOCKDOWN_API FLDChoiceOption
{
    GENERATED_BODY()

    UPROPERTY() FGuid OptionId;
    UPROPERTY() ELDChoiceOptionKind Kind = ELDChoiceOptionKind::None;
    // Ability id for skills, attribute id for attribute cards; the effect is DefinitionId at ToRank.
    UPROPERTY() FName DefinitionId;
    UPROPERTY() uint8 FromRank = 0;
    UPROPERTY() uint8 ToRank = 0;
    // Attribute options only. Opening and skill options always carry None.
    UPROPERTY() ELDAttributeQuality AttributeQuality = ELDAttributeQuality::None;
};

USTRUCT()
struct LOCKDOWN_API FLDChoiceOfferView
{
    GENERATED_BODY()

    UPROPERTY() FGuid OfferId;
    UPROPERTY() uint8 OpportunityOrdinal = 0;
    UPROPERTY() uint64 OfferRevision = 0;
    UPROPERTY() uint64 GuardBuildRevision = 0;
    // This offer is the opportunity's free reroll; it cannot be rerolled again.
    UPROPERTY() bool bRerolled = false;
    // Exactly three distinct options, immutable for the offer's lifetime.
    UPROPERTY() TArray<FLDChoiceOption> Options;
};

USTRUCT()
struct LOCKDOWN_API FLDLearnedSkillView
{
    GENERATED_BODY()

    UPROPERTY() FName DefinitionId;
    UPROPERTY() bool bActive = false;
    UPROPERTY() uint8 Rank = 0;
    // Active skills only: 0 = Q, 1 = W, 2 = E.
    UPROPERTY() uint8 Slot = 0;
};

USTRUCT()
struct LOCKDOWN_API FLDAttributeCardView
{
    GENERATED_BODY()

    UPROPERTY() FName DefinitionId;
    UPROPERTY() uint8 Count = 0;
    UPROPERTY() ELDAttributeQuality Quality = ELDAttributeQuality::None;
};

USTRUCT()
struct LOCKDOWN_API FLDProgressionView
{
    GENERATED_BODY()

    UPROPERTY() bool bValid = false;
    UPROPERTY() uint32 Generation = 0;
    UPROPERTY() uint32 Life = 0;
    UPROPERTY() ELDLifePhase LifePhase = ELDLifePhase::Unspawned;
    UPROPERTY() bool bCanChoose = false;
    UPROPERTY() uint8 Level = 0;
    UPROPERTY() uint8 MaxLevel = 0;
    UPROPERTY() int64 CumulativeXP = 0;
    UPROPERTY() int64 LevelThresholdXP = 0;
    UPROPERTY() bool bHasNextThreshold = false;
    UPROPERTY() int64 NextThresholdXP = 0;
    UPROPERTY() uint64 ProgressionRevision = 0;
    UPROPERTY() uint64 BuildRevision = 0;
    UPROPERTY() uint8 EarnedThrough = 0;
    UPROPERTY() uint8 ConsumedThrough = 0;
    UPROPERTY() uint8 PendingChoiceCount = 0;
    // Free rerolls left (earned at the reroll levels, kept until used).
    UPROPERTY() uint8 RerollTokens = 0;
    UPROPERTY() bool bHasOffer = false;
    UPROPERTY() FLDChoiceOfferView Offer;
    UPROPERTY() TArray<FLDLearnedSkillView> Skills;
    UPROPERTY() TArray<FLDAttributeCardView> AttributeCards;
    // #153: the chosen opening direction card, a permanent stat source; None until the opening choice is taken.
    UPROPERTY() FName OpeningCard;
    UPROPERTY() FLDDemoContentDigest CatalogDigest;
};

USTRUCT()
struct LOCKDOWN_API FLDChoiceCommand
{
    GENERATED_BODY()

    UPROPERTY() uint32 Generation = 0;
    UPROPERTY() uint32 Life = 0;
    UPROPERTY() ELDLifePhase ExpectedLifePhase = ELDLifePhase::Unspawned;
    UPROPERTY() uint64 RequestId = 0;
    UPROPERTY() FGuid OfferId;
    UPROPERTY() uint64 OfferRevision = 0;
    UPROPERTY() FGuid OptionId;
    UPROPERTY() uint64 ExpectedBuildRevision = 0;
    UPROPERTY() FLDDemoContentDigest CatalogDigest;
    UPROPERTY() bool bReplace = false;
    UPROPERTY() uint8 ReplaceSlot = 0;
    UPROPERTY() FLDDefinitionWire ExpectedOldSkillId;
    UPROPERTY() uint8 ExpectedOldRank = 0;
    UPROPERTY() bool bReplaceConfirmed = false;
    // 112 §6: spend one free reroll on OfferId instead of choosing; OptionId stays empty and nothing is replaced.
    UPROPERTY() bool bReroll = false;

    bool SameTerms(const FLDChoiceCommand& Other) const;
};

USTRUCT()
struct LOCKDOWN_API FLDChoiceReceipt
{
    GENERATED_BODY()

    UPROPERTY() FLDGrowthReceiptHeader Header;
    UPROPERTY() FGuid OfferId;
    UPROPERTY() FGuid OptionId;
    UPROPERTY() uint8 OpportunityOrdinal = 0;
    UPROPERTY() uint8 ConsumedThrough = 0;
    UPROPERTY() ELDChoiceOptionKind Kind = ELDChoiceOptionKind::None;
    UPROPERTY() FName DefinitionId;
    UPROPERTY() uint8 NewRank = 0;
    UPROPERTY() ELDAttributeQuality AttributeQuality = ELDAttributeQuality::None;
    UPROPERTY() bool bReplaced = false;
    UPROPERTY() uint8 ReplacedSlot = 0;
    UPROPERTY() FName ReplacedSkillId;
    UPROPERTY() uint8 ReplacedRank = 0;
    // #153: an applied opening choice reveals the active it taught (learned into Q at rank 1); None for every other receipt.
    UPROPERTY() FName OpeningActiveId;
    UPROPERTY() bool bHasNextOffer = false;
    UPROPERTY() FGuid NextOfferId;
    // Applied reroll: NextOfferId replaced OfferId for the same opportunity.
    UPROPERTY() bool bRerolled = false;
    UPROPERTY() uint8 RerollTokensLeft = 0;
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
    UPROPERTY() bool bHasChoiceReceipt = false;
    UPROPERTY() FLDChoiceReceipt ChoiceReceipt;
};

USTRUCT()
struct LOCKDOWN_API FLDChoiceReceiptQueryResult
{
    GENERATED_BODY()

    UPROPERTY() FLDReceiptQuery Query;
    UPROPERTY() ELDReceiptLookup Lookup = ELDReceiptLookup::None;
    UPROPERTY() bool bHasPending = false;
    UPROPERTY() FLDPendingReceiptInfo Pending;
    UPROPERTY() bool bHasReceipt = false;
    UPROPERTY() FLDChoiceReceipt Receipt;
};

// One owner-only publication inside FLDOwnerState: level, XP, skills, cards and offer always move together.
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
DECLARE_MULTICAST_DELEGATE_OneParam(FLDOnChoiceReceipt, const FLDChoiceReceiptQueryResult&);

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
    // Opportunity ordinals granted by this reward, inclusive; First > Last when none.
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
    // Only Alive and Dead may confirm a choice; Unspawned/Left never can.
    LOCKDOWN_API bool IsChoicePhase(ELDLifePhase Phase);
    LOCKDOWN_API const TCHAR* ReasonName(ELDGrowthReason Reason);
}
