#pragma once

#include "CoreMinimal.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "Development/LDDemoContentDefinitions.h"
#include "LDGrowthProtocol.generated.h"

// Growth protocol (GROWTH_TRANSACTIONS). Client-sent enums are decoded by an explicit whitelist; unknown values are
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

// Build commands: choose one active of the current active offer, learn a pending blueprint into an empty Q/W/E slot, rank up a
// skill or allocate unspent points across the stats; take, skip or reroll the current card offer and install or
// uninstall an owned card (spec §6). In a rest window also the rest shop (buy a shelf item or a skill point, refresh the
// shelf, sell a library card) and the rest rebuilds: respec the allocated points, replace a learned active with a pending
// blueprint, or forget a learned active.
UENUM()
enum class ELDBuildCommandKind : uint8
{
    // 10-13, 24 and 25 (the #198 component commands) and 20 (the skill merchant's BuyNpcOffer) are retired and never reused.
    None = 0, ActiveChoice = 1, LearnBlueprint = 2, AllocatePoints = 3,
    Respec = 21, ReplaceActive = 22, ForgetActive = 23,
    SkipActiveOffer = 26,
    // One independent final rule rewrite; selection and the build lock commit together.
    FinalMod = 27,
    // Spend one skill point on the next rank of a learned skill (issue #287 spec §4.5, SC-05).
    RankUpSkill = 28,
    // Spec §6.4: take one card of the current offer (installed in the same commit when a legal target was named and a slot
    // is free), skip the offer, reroll it; install or uninstall an owned card. 33 is retired.
    PickCard = 30, SkipCards = 31, RerollCards = 32, InstallCard = 34, UninstallCard = 35,
    // Spec §3.13 rest shop (paid in stat points): buy shelf slot Slot (or a skill point, Slot ShopSkillPointSlot), refresh the
    // shelf. 38 (the card sale) and 39 (the pick ticket) are retired.
    ShopBuy = 36, ShopRefresh = 37
};

// What one rest shop slot holds (spec §3.13).
UENUM()
enum class ELDShopItemKind : uint8 { None = 0, Skill = 1, Card = 2 };

// Spec §6.1: where an installed card sits: one learned Q/W/E skill (by id), the D dash or the body (which carries the basic
// attack). 1 (the former basic place) is retired.
UENUM()
enum class ELDCardTargetKind : uint8 { None = 0, Skill = 2, Dash = 3, Body = 4 };

// What filled one card offer slot of the queue (spec §6.4): the R0 opening offer, a level-up, or a field reward (an
// airdrop or the Boss build draw, an elite camp marker).
UENUM()
// Spec §10.3/§11.2/§11.3 (P6): the mid-field objectives, lockers, the dealer and the fridge and bonus affixes give their own
// offers, each with its content size (OBJECTIVE.*CardOffer, EXPLORE.*CardOffer, AFFIX.*CardOffer).
enum class ELDCardOfferSource : uint8
{
    None = 0, Opening = 1, LevelUp = 2, FieldReward = 3, MiniFlag = 4, Arena = 5, Locker = 6, Peddler = 7, CampFridge = 8, CampBonus = 9,
    // The bounty killer of a trending contender: REWARD.BountyCardOffer Rare/Legendary cards.
    Bounty = 10, PvPKill = 11, LeaderBounty = 12, BossLegendary = 13, BossMedal = 14
};

USTRUCT()
struct LOCKDOWN_API FLDFinalModSelection
{
    GENERATED_BODY()

    UPROPERTY() FName DefinitionId;

    bool operator==(const FLDFinalModSelection& Other) const { return DefinitionId == Other.DefinitionId; }
    bool operator!=(const FLDFinalModSelection& Other) const { return !(*this == Other); }
    bool IsEmpty() const { return *this == FLDFinalModSelection(); }
};

// What a field reward's build part gave (spec §6.4): an active blueprint, a card offer queued for the recipient, or a
// blueprint held because no active was eligible. 2-5 (the #198 component results) are retired.
UENUM()
enum class ELDFieldRewardResult : uint8 { None = 0, Blueprint = 1, BlueprintHeld = 6, CardOffer = 7 };

// The build part a map event pays (server-internal). BuildDraw: the Boss/airdrop build reward (50% active blueprint / 50%
// one card offer; no eligible active gives the card offer); Blueprint: the high-tier camp marker's active blueprint (an
// empty pool holds it); CardOffer: the elite camp marker's card offer.
// MiniFlagOffer..BonusOffer (P6): a card offer of that source's size.
enum class ELDFieldRewardKind : uint8
{
    None = 0, BuildDraw = 1, Blueprint = 2, CardOffer = 3, MiniFlagOffer = 4, ArenaOffer = 5, LockerOffer = 6, PeddlerOffer = 7,
    FridgeOffer = 8, BonusOffer = 9, BountyOffer = 10, BossKill = 11
};

// What an applied ActiveChoice or LearnBlueprint did with its active.
UENUM()
enum class ELDBuildChoiceResult : uint8 { None = 0, Learned = 1, StoredAsBlueprint = 2 };

enum class ELDRewardOutcome : uint8 { None = 0, Granted = 1, NoReward = 2, RetryableNotCommitted = 3, PermanentlyRejected = 4 };

// Travels as UPROPERTY uint16 Reason; read through LDGrowthWire::Reason. Retired numbers stay reserved: 66-68 and 160-166,
// 173 (the #198 component reasons), 40 NoOffer,
// 46 ReplacementRequired, 47 ReplacementMismatch, 49 NoReroll, 50 OfferAlreadyRerolled, 51 OpeningNoReroll (#198), 65
// ElementUnsupported (#198 B, water/electric before the #198 A element state), and 52 NotSafe (the build safe window) and
// 63 RestOnly (moves and uninstalls in a rest only), both retired when #222 opened the own build commands at all times.
enum class ELDGrowthReason : uint16
{
    None = 0,
    InvalidRequest = 1, OldMatch = 2, OldLife = 3, LifePhaseChanged = 4, LeftMatch = 5, MatchClosed = 6,
    NotContender = 7, CatalogMismatch = 8, NotAuthority = 9, Dead = 10,
    RequestConflict = 20, OutOfOrder = 21, ReceiptExpired = 22, RequestInFlight = 23,
    UnknownOffer = 41, InvalidOption = 42, OfferAlreadyCommitted = 43, OfferGuardStale = 44, BuildStale = 45,
    // #198: replacing or forgetting a learned active outside a rest window.
    ReplacementNotAllowed = 48,
    NoEmptySlot = 53, SlotOccupied = 54, NoBlueprint = 55, NotEnoughPoints = 56, NoActiveOffer = 57,
    // The match is outside the field segments and the rest steps (results and every other phase).
    PhaseClosed = 58,
    // Cards: NotOwned (no such card), AlreadyInstalled, NotInstalled, InvalidTarget (a target outside the card's form, an
    // unlearned skill or an element the card does not offer).
    NotOwned = 60, AlreadyInstalled = 61, NotInstalled = 62, InvalidTarget = 64,
    // WorkshopFull: every capacity slot of the level holds an installed card. 150 (TargetConflict) is retired.
    WorkshopFull = 151,
    // #198 rest: an NPC service in a rest step outside the build and payout windows (draw, betting, PK, settlement, return
    // countdown); #222 keeps the own build commands open there.
    BuildLocked = 70,
    // 71 (NpcPoolTooSmall, the retired skill merchant) is reserved.
    // An active offer is outstanding.
    OfferOutstanding = 72,
    NothingToRespec = 73,
    // ReplaceActive/ForgetActive name an empty slot.
    SlotEmpty = 74,
    // A rest rebuild (respec) or the shop outside a rest window. 76 (the retired rest NPC's proximity) is never reused.
    NotRestWindow = 75,
    DataNotReady = 78, StaleContext = 79,
    NumericUnderflow = 90, NumericDomain = 91, ArithmeticOverflow = 92, DataInvalid = 93, PrepareUnavailable = 94,
    AuthorityFault = 95, CapacityExceeded = 96,
    NoRewardNoOwner = 110, NoRewardSelf = 111, NoRewardOwnerLeft = 112, DeathConflict = 113, StaleDeath = 114,
    // #198 session D (allocated 140-149): a PK arena death pays no kill/assist reward; a PK award identity paid twice.
    NoRewardPKArena = 140, PKAwardDuplicate = 141,
    // FieldRewardDuplicate: a map event paid twice.
    FieldRewardDuplicate = 167,
    // Phase 9 final mod (allocated 170-179; 172 is retired): FinalLocked (the build was locked by the final mod choice or the
    // final field entry), NoFinalMod (no open final mod of that id).
    FinalLocked = 170, NoFinalMod = 171,
    // Skill ranks (issue #287 spec §4.5): NoSkillPoint (no unspent skill point), BranchUnavailable (not the next rank of that
    // skill's branch: A/B from rank 1, A1/A2 under A, B1/B2 under B; rank 3 has none).
    NoSkillPoint = 180, BranchUnavailable = 181,
    // Card offers (spec §6.4, allocated 190-199): NoCardOffer (no current offer of that id), NoReroll (no reroll left),
    // NotInOffer (the card is not one of the offer's).
    NoCardOffer = 190, NoReroll = 191, NotInOffer = 192,
    // Spec §3.13 the rest shop (allocated 200-209; a price the unspent stat points cannot pay is NotEnoughPoints): ShopSoldOut
    // (the slot was bought already or holds nothing), ShopStale (the shelf was restocked since the command saw it),
    // SkillPointLimit (the match's bought skill points are all taken), NoShop (no shelf stocked yet: the opening rest has
    // none). 200 (the vouchers), 205 and 206 (the pick ticket) and 207 (the card sale) are retired.
    ShopSoldOut = 201, ShopStale = 202, SkillPointLimit = 203, NoShop = 204
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
    // The chosen branch path; its rank follows from it (None = rank 1).
    UPROPERTY() ELDSkillBranch Branch = ELDSkillBranch::None;
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
    // Spec §6.4: stat points paid for skipping it (GROWTH.ActiveSkipPoints).
    UPROPERTY() int64 SkipPoints = 0;
};

// Spec §6.1: a card install target on the wire and in views (ELDCardTargetKind; unused fields zero/None).
USTRUCT()
struct LOCKDOWN_API FLDCardTargetView
{
    GENERATED_BODY()

    UPROPERTY() ELDCardTargetKind Kind = ELDCardTargetKind::None;
    // Skill: the learned active the card sits on.
    UPROPERTY() FName Active;
    // A card that offers element choices (Y25): the chosen element.
    UPROPERTY() ELDDemoElement Element = ELDDemoElement::None;
};

// One owned card (spec §6.1): copies of one definition are separate cards with their own serial and slot.
USTRUCT()
struct LOCKDOWN_API FLDCardInstanceView
{
    GENERATED_BODY()

    // Unique within the build, never reused in a match.
    UPROPERTY() uint16 Serial = 0;
    UPROPERTY() FName DefinitionId;
    // The offer ordinal that gave it (0 for a DEV fixture card).
    UPROPERTY() uint16 OfferOrdinal = 0;
    // Kind None = in the library.
    UPROPERTY() FLDCardTargetView Target;

    bool IsInstalled() const { return Target.Kind != ELDCardTargetKind::None; }
};

// The current card offer (spec §6.4): up to CARDS.OfferSize distinct cards, fixed until it is taken, skipped or rerolled.
USTRUCT()
struct LOCKDOWN_API FLDCardOfferView
{
    GENERATED_BODY()

    UPROPERTY() FGuid OfferId;
    UPROPERTY() uint16 Ordinal = 0;
    UPROPERTY() uint64 OfferRevision = 0;
    UPROPERTY() ELDCardOfferSource Source = ELDCardOfferSource::None;
    UPROPERTY() TArray<FName> Options;
};

// Spec §3.13: one slot of the rest shop shelf: a skill or a card, its price and whether it was bought.
USTRUCT()
struct LOCKDOWN_API FLDShopSlotView
{
    GENERATED_BODY()

    UPROPERTY() ELDShopItemKind Kind = ELDShopItemKind::None;
    UPROPERTY() FName DefinitionId;
    UPROPERTY() int32 Price = 0;
    UPROPERTY() bool bSold = false;
};

// The owner's rest shop: the shelf (skills first, then cards) of its current stock and the prices of its services. Stocked at
// every mid-match rest start; usable in the rest build window.
USTRUCT()
struct LOCKDOWN_API FLDShopView
{
    GENERATED_BODY()

    UPROPERTY() bool bStocked = false;
    // Advances with every restock and purchase; ShopBuy and ShopRefresh name it (OfferRevision).
    UPROPERTY() uint64 Revision = 0;
    UPROPERTY() TArray<FLDShopSlotView> Slots;
    UPROPERTY() int32 RefreshPrice = 0;
    UPROPERTY() int32 SkillPointPrice = 0;
    UPROPERTY() int32 SkillPointsBought = 0;
    UPROPERTY() int32 SkillPointsMax = 0;
};

// #198 G: one field reward this contender received (owner-only history, oldest first; at most the room's 13 opportunities).
USTRUCT()
struct LOCKDOWN_API FLDFieldRewardView
{
    GENERATED_BODY()

    // airdrop, boss, camp_marker_high or camp_marker_elite, and its serial.
    UPROPERTY() FName Event;
    UPROPERTY() uint64 Serial = 0;
    UPROPERTY() int64 XP = 0;
    UPROPERTY() ELDFieldRewardResult Result = ELDFieldRewardResult::None;
    // Blueprint: the active; otherwise None.
    UPROPERTY() FName Definition;
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
    // The last build revision Combat installed; inventory-only revisions do not advance it.
    UPROPERTY() uint64 CombatInstalledRevision = 0;
    // Earned (opening, levels and the max-level XP grants) minus allocated minus permanently spent.
    UPROPERTY() int64 UnspentPoints = 0;
    // Spec §3.13: stat points the rest shop took (never returned), so earned = unspent + allocated + these.
    UPROPERTY() int64 ShopSpentPoints = 0;
    // One row per stat, in the stats' design order (FLDDemoContentDefinitions::StatsInDesignOrder).
    UPROPERTY() TArray<FLDStatPointsView> StatPoints;
    // Learned Q/W/E actives, by slot.
    UPROPERTY() TArray<FLDActiveSlotView> Actives;
    // Skill points earned at SKILL.PointLevels and not yet spent on a rank (LDProgression::SkillPoints).
    UPROPERTY() int32 SkillPoints = 0;
    // Spec §11.2: the skill points the audit affix paid (part of SkillPoints' earned side).
    UPROPERTY() int32 AffixSkillPoints = 0;
    // Chosen actives waiting to be learned (待学图纸), sorted by id.
    UPROPERTY() TArray<FName> PendingBlueprints;
    UPROPERTY() uint8 ActiveOpportunitiesEarned = 0;
    UPROPERTY() uint8 ActiveOpportunitiesConsumed = 0;
    // Earned and not yet chosen, the current offer's included; one without an eligible active waits without an offer.
    UPROPERTY() uint8 QueuedActiveOpportunities = 0;
    UPROPERTY() bool bHasActiveOffer = false;
    UPROPERTY() FLDActiveOfferView ActiveOffer;
    // Owned cards sorted by serial, and the workshop capacity of the current level (CARDS.CapacityBase + growth).
    UPROPERTY() TArray<FLDCardInstanceView> Cards;
    UPROPERTY() int32 CardCapacity = 0;
    // The card offer queue (spec §6.4): earned and not yet resolved, the current offer included; the head waits without an
    // offer until the opening active is chosen.
    UPROPERTY() uint16 QueuedCardOffers = 0;
    UPROPERTY() bool bHasCardOffer = false;
    UPROPERTY() FLDCardOfferView CardOffer;
    UPROPERTY() int32 CardRerolls = 0;
    UPROPERTY() FLDDemoContentDigest CatalogDigest;
    // Spec §3.13 the rest shop.
    UPROPERTY() FLDShopView Shop;
    // #198 G: high-tier camp blueprints held because no active was eligible; each is granted once one becomes eligible.
    UPROPERTY() uint8 HeldBlueprints = 0;
    // #198 G: every field reward received, oldest first.
    UPROPERTY() TArray<FLDFieldRewardView> FieldRewards;
    // Immutable once-revealed candidates.
    UPROPERTY() bool bFinalModPending = false;
    UPROPERTY() bool bExtraFinalModCard = false;
    UPROPERTY() FGuid FinalModOfferId;
    UPROPERTY() TArray<FName> FinalModOptions;
    UPROPERTY() FName FinalModChosen;
    UPROPERTY() FLDFinalModSelection FinalMod;
    UPROPERTY() bool bBuildLocked = false;
};

USTRUCT()
struct LOCKDOWN_API FLDBuildCommand
{
    GENERATED_BODY()

    // Slot of an ActiveChoice without a preferred slot (the lowest empty one); every other kind leaves Slot here
    // except LearnBlueprint, ReplaceActive and ForgetActive, which name 0..2.
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
    // ShopBuy: the skill point instead of a shelf slot.
    static constexpr uint8 ShopSkillPointSlot = 0xFE;

    // ActiveChoice with bLearnIfPossible: the preferred slot 0..2 or AnySlot; LearnBlueprint: the empty slot 0..2;
    // ReplaceActive, ForgetActive and RankUpSkill: the learned slot 0..2; ShopBuy: the shelf slot or ShopSkillPointSlot.
    // Respec and ShopRefresh leave it at AnySlot.
    UPROPERTY() uint8 Slot = AnySlot;
    // RankUpSkill: the next branch path of the skill in Slot (A|B at rank 1, A1|A2 under A, B1|B2 under B).
    UPROPERTY() ELDSkillBranch Branch = ELDSkillBranch::None;
    // LearnBlueprint and ReplaceActive: the pending blueprint to learn.
    UPROPERTY() FLDDefinitionWire Blueprint;
    // AllocatePoints: one count per stat in the stats' design order, at least one point in total.
    UPROPERTY() TArray<uint8> PointCounts;
    // PickCard, SkipCards and RerollCards name the offer by OfferId/OfferRevision; PickCard the card by Option.
    // PickCard: install the card in the same commit on CardTarget when it is legal and a slot is free, else keep it.
    UPROPERTY() bool bInstallIfPossible = false;
    // InstallCard / UninstallCard: the owned card; InstallCard and PickCard: the target.
    UPROPERTY() uint16 CardSerial = 0;
    UPROPERTY() ELDCardTargetKind CardTarget = ELDCardTargetKind::None;
    UPROPERTY() FLDDefinitionWire TargetDefinition;
    UPROPERTY() ELDDemoElement Element = ELDDemoElement::None;
    // InstallCard only: an installed card taken off in the same commit to make room; 0 = a plain install.
    UPROPERTY() uint16 ReplacingSerial = 0;

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
    // ActiveChoice: the chosen active; LearnBlueprint and ReplaceActive: the learned blueprint; ShopBuy: the skill or card
    // received (a card also names CardSerial/CardId).
    UPROPERTY() FName DefinitionId;
    UPROPERTY() ELDBuildChoiceResult ChoiceResult = ELDBuildChoiceResult::None;
    // Learned: its slot (0 = Q, 1 = W, 2 = E).
    UPROPERTY() uint8 LearnedSlot = 0;
    // StoredAsBlueprint after bLearnIfPossible: why it was not learned (NoEmptySlot); None when the
    // command did not ask to learn.
    UPROPERTY() uint16 StoredReason = 0;
    // AllocatePoints: the points spent per stat, in the stats' design order.
    UPROPERTY() TArray<uint8> PointsSpent;
    UPROPERTY() int64 UnspentPointsAfter = 0;
    // Card kinds: the card (its serial and definition), whether it is installed after the commit, and (PickCard stored
    // without install) why not; None when not asked.
    UPROPERTY() uint16 CardSerial = 0;
    UPROPERTY() FName CardId;
    UPROPERTY() bool bCardInstalled = false;
    UPROPERTY() uint16 InstallStoredReason = 0;
    // RerollCards: rerolls left after the commit.
    UPROPERTY() int32 CardRerollsAfter = 0;
    UPROPERTY() bool bHasNextOffer = false;
    UPROPERTY() FGuid NextOfferId;
    // ReplaceActive and ForgetActive: the forgotten active.
    UPROPERTY() FName ForgottenId;
    // RankUpSkill: the branch now held by the skill in LearnedSlot (DefinitionId).
    UPROPERTY() ELDSkillBranch Branch = ELDSkillBranch::None;
    // Unspent skill points after the commit.
    UPROPERTY() int32 SkillPointsAfter = 0;
    // Respec: the allocated points returned to the unspent ones.
    UPROPERTY() int64 PointsRefunded = 0;
    // Stat points this commit awarded (an offer skip) or the shop took (negative).
    UPROPERTY() int64 PointsDelta = 0;
    // InstallCard with ReplacingSerial, ReplaceActive and ForgetActive: the cards that went back to the library.
    UPROPERTY() TArray<uint16> UninstalledCards;
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
    // Card offers queued by the levels crossed (one each), as offer ordinals; First > Last when none.
    uint16 CardOfferFirst = 1;
    uint16 CardOfferLast = 0;
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
    // Card offers queued by the levels crossed or the field reward, as offer ordinals; First > Last when none.
    uint16 CardOfferFirst = 1;
    uint16 CardOfferLast = 0;
    // #198 G: the build part of a field reward (None for a death or a reward without one) and its definition.
    ELDFieldRewardResult FieldResult = ELDFieldRewardResult::None;
    FName FieldDefinition;
    FLDResultRevisions Revisions;
    // Assisters of a contender kill in the fact's order, each with the part granted in the same commit.
    TArray<FLDRewardShareReceipt> Assists;
};

namespace LDGrowthWire
{
    inline ELDGrowthReason Reason(uint16 Raw) { return static_cast<ELDGrowthReason>(Raw); }
    inline uint16 Raw(ELDGrowthReason Reason) { return static_cast<uint16>(Reason); }
    LOCKDOWN_API bool IsKnownFamily(ELDGrowthFamily Family);
    // Whitelists of client-sent enums: build command kinds, card target kinds and elements.
    LOCKDOWN_API bool IsKnownBuildKind(ELDBuildCommandKind Kind);
    LOCKDOWN_API bool IsKnownCardTarget(ELDCardTargetKind Kind);
    LOCKDOWN_API bool IsKnownElement(ELDDemoElement Element);
    // An optional definition field: all-zero bytes are None (true); anything else must decode.
    LOCKDOWN_API bool DecodeOptional(const FLDDefinitionWire& Wire, FName& Out);
    // Only an Alive or Dead contender may send a build command; Unspawned/Left never can.
    LOCKDOWN_API bool IsBuildLifePhase(ELDLifePhase Phase);
    LOCKDOWN_API const TCHAR* ReasonName(ELDGrowthReason Reason);
}
