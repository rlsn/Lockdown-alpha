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
// or allocate unspent points across the stats. #198 B: pick the opening base component (once, after the opening active
// choice), install/enable an owned component for the first time, or uninstall one (rest window only). In a rest window also
// the rest services: buy an NPC active offer, respec the allocated points, replace a learned active with a pending blueprint,
// or forget a learned active. #198 G: take one option of a pending elite-camp component choice (ComponentChoice, rest and
// field, alive or dead), and the rest services FillGap (pay GROWTH.FillGapPoints for one unowned common base that gives a
// never-enabled mod/connector its first compatible entry) and Dismantle (GROWTH.DismantleInputCount distinct owned common
// components for one random unowned common component).
UENUM()
enum class ELDBuildCommandKind : uint8
{
    None = 0, ActiveChoice = 1, LearnBlueprint = 2, AllocatePoints = 3,
    OpeningBase = 10, InstallComponent = 11, UninstallComponent = 12, ComponentChoice = 13,
    BuyNpcOffer = 20, Respec = 21, ReplaceActive = 22, ForgetActive = 23, FillGap = 24, Dismantle = 25,
    SkipActiveOffer = 26,
    // One independent final rule rewrite; selection and the build lock commit together.
    FinalMod = 27
};

// #198 B: where an installed component sits. Base components take None (enabled on the build); mods/connectors
// (#198 F) take an active, an effect segment of an active, another component, or the innate D dash.
UENUM()
enum class ELDComponentTargetKind : uint8 { None = 0, Active = 1, EffectSegment = 2, Component = 3, Dash = 4 };

USTRUCT()
struct LOCKDOWN_API FLDFinalModSelection
{
    GENERATED_BODY()

    UPROPERTY() FName DefinitionId;
    UPROPERTY() ELDComponentTargetKind TargetKind = ELDComponentTargetKind::None;
    UPROPERTY() FName TargetId;

    bool operator==(const FLDFinalModSelection& Other) const
    {
        return DefinitionId == Other.DefinitionId && TargetKind == Other.TargetKind && TargetId == Other.TargetId;
    }
    bool operator!=(const FLDFinalModSelection& Other) const { return !(*this == Other); }
    bool IsEmpty() const { return *this == FLDFinalModSelection(); }
};

// #198 B: how a component instance was obtained (one stable business identity each). #198 G: BuildReward (a Boss or airdrop
// build reward: scarce, or the common fallback), EliteChoice (an elite camp marker's two-choice), FillGap, Dismantle.
UENUM()
enum class ELDComponentSource : uint8 { None = 0, OpeningPick = 1, LevelUp = 2, BuildReward = 3, EliteChoice = 4, FillGap = 5, Dismantle = 6 };

// #198 G (spec §4.2, outer parameters §5): what one field reward gave its recipient. Blueprint: an active blueprint;
// ScarceComponent / CommonComponent: a component; PoolPoints: GROWTH.EmptyComponentPoolPoints because the material pool was
// empty; ComponentChoice: a pending two-choice of known common components; BlueprintHeld: the active pool was empty, the
// blueprint waits until an active becomes eligible again.
UENUM()
enum class ELDFieldRewardResult : uint8
{
    None = 0, Blueprint = 1, ScarceComponent = 2, CommonComponent = 3, PoolPoints = 4, ComponentChoice = 5, BlueprintHeld = 6
};

// #198 G: the build part a map event pays (server-internal). BuildDraw: the Boss/airdrop build reward (50% active blueprint /
// 50% scarce component, each uniform among the unheld ones; an empty class uses the other, both empty one common material);
// Blueprint: the high-tier camp marker's active blueprint (an empty pool holds it); ComponentChoice: the elite camp marker's
// two known common components (one when only one is left, the empty-pool points when none is).
enum class ELDFieldRewardKind : uint8 { None = 0, BuildDraw = 1, Blueprint = 2, ComponentChoice = 3 };

// What an applied ActiveChoice or LearnBlueprint did with its active.
UENUM()
enum class ELDBuildChoiceResult : uint8 { None = 0, Learned = 1, StoredAsBlueprint = 2 };

enum class ELDRewardOutcome : uint8 { None = 0, Granted = 1, NoReward = 2, RetryableNotCommitted = 3, PermanentlyRejected = 4 };

// Travels as UPROPERTY uint16 Reason; read through LDGrowthWire::Reason. Retired numbers stay reserved: 40 NoOffer,
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
    // #198 B components: NotOwned (no such instance), AlreadyInstalled, NotInstalled, InvalidTarget (wrong target kind/binding),
    // OpeningBaseUnavailable (not an option of the current opening pick), OpeningBaseTaken (already picked),
    // OpeningActiveFirst (opening active not chosen yet).
    NotOwned = 60, AlreadyInstalled = 61, NotInstalled = 62, InvalidTarget = 64,
    OpeningBaseUnavailable = 66, OpeningBaseTaken = 67, OpeningActiveFirst = 68,
    // #198 F: TargetConflict (M08 and M09 on the same dash target). WorkshopFull: every workshop capacity slot of the
    // level is taken by an installed component.
    TargetConflict = 150, WorkshopFull = 151,
    // #198 rest: an NPC service in a rest step outside the build and payout windows (draw, betting, PK, settlement, return
    // countdown); #222 keeps the own build commands open there.
    BuildLocked = 70,
    // Fewer than three actives are eligible for an NPC offer; nothing is charged.
    NpcPoolTooSmall = 71,
    // An active offer (or, for the NPC, any queued active opportunity) is outstanding.
    OfferOutstanding = 72,
    NothingToRespec = 73,
    // ReplaceActive/ForgetActive name an empty slot.
    SlotEmpty = 74,
    // A rest service (NPC, respec) outside a rest window.
    NotRestWindow = 75,
    RestServiceUnavailable = 76,
    DataNotReady = 78, StaleContext = 79,
    NumericUnderflow = 90, NumericDomain = 91, ArithmeticOverflow = 92, DataInvalid = 93, PrepareUnavailable = 94,
    AuthorityFault = 95, CapacityExceeded = 96,
    NoRewardNoOwner = 110, NoRewardSelf = 111, NoRewardOwnerLeft = 112, DeathConflict = 113, StaleDeath = 114,
    // #198 session D (allocated 140-149): a PK arena death pays no kill/assist reward; a PK award identity paid twice.
    NoRewardPKArena = 140, PKAwardDuplicate = 141,
    // #198 G (allocated 160-169): NoComponentChoice (no pending choice of that id), ComponentOwned (the option is already
    // owned, or the points fallback while an option is still free), NotAGap (the target is no owned, never-enabled
    // mod/connector without any compatible entry), NoGapFiller (the base is not one that gives it its missing entry),
    // GapAlreadyFilled (the target was filled once), DismantleInvalid (not distinct owned common components of the approved
    // count), DismantlePoolEmpty (no common component left to receive; nothing is removed), FieldRewardDuplicate (a map event
    // paid twice).
    NoComponentChoice = 160, ComponentOwned = 161, NotAGap = 162, NoGapFiller = 163, GapAlreadyFilled = 164,
    DismantleInvalid = 165, DismantlePoolEmpty = 166, FieldRewardDuplicate = 167,
    // Phase 9 final mod (allocated 170-179): FinalLocked (the build was locked by the final mod choice or the final field
    // entry), NoFinalMod (no open final mod of that id), FinalModUnavailable (its selected source is no longer legal).
    FinalLocked = 170, NoFinalMod = 171, FinalModUnavailable = 172
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
    UPROPERTY() int64 SkipPoints = 0;
};

// #198 B: an install target on the wire and in views (ELDComponentTargetKind; unused fields zero/None).
USTRUCT()
struct LOCKDOWN_API FLDComponentTargetView
{
    GENERATED_BODY()

    UPROPERTY() ELDComponentTargetKind Kind = ELDComponentTargetKind::None;
    // Active / EffectSegment: the learned active; Component: the host component.
    UPROPERTY() FName Definition;
    // EffectSegment: index of the effect in the active's value set.
    UPROPERTY() uint8 Segment = 0;
    // ActiveElement binding (B18): the chosen element.
    UPROPERTY() ELDDemoElement Element = ELDDemoElement::None;
    // ActiveElement binding (B18): the bound learned active.
    UPROPERTY() FName BoundActive;
    // #198 F C12: the installed base component the exit repeats.
    UPROPERTY() FName BoundComponent;
};

// #198 B: one owned material instance (inventory and installed deduplicated: one per definition).
USTRUCT()
struct LOCKDOWN_API FLDComponentInstanceView
{
    GENERATED_BODY()

    UPROPERTY() FName DefinitionId;
    UPROPERTY() ELDComponentSource Source = ELDComponentSource::None;
    // LevelUp: the level whose reward granted it; OpeningPick: 1.
    UPROPERTY() uint8 SourceLevel = 0;
    UPROPERTY() bool bInstalled = false;
    // Installed at least once: a later re-install is a rest-window move, never a field first-time install.
    UPROPERTY() bool bEverInstalled = false;
    UPROPERTY() FLDComponentTargetView Target;
    // #198 G: this mod/connector already received its one FillGap; uninstalling its entry never opens another.
    UPROPERTY() bool bGapFilled = false;
};

USTRUCT()
struct FLDFinalModSourceView
{
    GENERATED_BODY()

    UPROPERTY() FName Option;
    UPROPERTY() ELDComponentTargetKind TargetKind = ELDComponentTargetKind::None;
    UPROPERTY() FName TargetId;
};

// #198 G: one pending elite-camp two-choice: one or two distinct known common components fixed when it was granted.
USTRUCT()
struct LOCKDOWN_API FLDComponentChoiceView
{
    GENERATED_BODY()

    UPROPERTY() FGuid ChoiceId;
    // The map event that paid it (camp_marker_elite) and its serial.
    UPROPERTY() FName Event;
    UPROPERTY() uint64 Serial = 0;
    UPROPERTY() TArray<FName> Options;
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
    // Blueprint: the active; Scarce/CommonComponent: the component; otherwise None.
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
    // Earned (opening, levels and the max-level XP grants) minus allocated minus permanently spent.
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
    // #198 B: owned components sorted by id.
    UPROPERTY() TArray<FLDComponentInstanceView> Components;
    // #198 B opening base pick: open once the opening active is chosen, until a base is picked; its options are the
    // currently available bases (spec §4.3) not owned, in catalog order.
    UPROPERTY() bool bOpeningBasePending = false;
    UPROPERTY() TArray<FName> OpeningBaseOptions;
    UPROPERTY() FLDDemoContentDigest CatalogDigest;
    // Points paid for rest services (the NPC, FillGap); a respec never returns them.
    UPROPERTY() int64 PermanentSpentPoints = 0;
    // #198 G: pending elite-camp component choices, oldest first.
    UPROPERTY() TArray<FLDComponentChoiceView> ComponentChoices;
    // #198 G: high-tier camp blueprints held because no active was eligible; each is granted once one becomes eligible.
    UPROPERTY() uint8 HeldBlueprints = 0;
    // #198 G: every field reward received, oldest first.
    UPROPERTY() TArray<FLDFieldRewardView> FieldRewards;
    // Immutable once-revealed candidates; legal bindings reflect the current build without redrawing them.
    UPROPERTY() bool bFinalModPending = false;
    UPROPERTY() FGuid FinalModOfferId;
    UPROPERTY() TArray<FName> FinalModOptions;
    UPROPERTY() TArray<FLDFinalModSourceView> FinalModSources;
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
    // ActiveChoice with bLearnIfPossible: the preferred slot 0..2 or AnySlot; LearnBlueprint: the empty slot 0..2;
    // ReplaceActive and ForgetActive: the learned slot 0..2. BuyNpcOffer and Respec carry no further field.
    UPROPERTY() uint8 Slot = AnySlot;
    // LearnBlueprint and ReplaceActive: the pending blueprint to learn.
    UPROPERTY() FLDDefinitionWire Blueprint;
    // AllocatePoints: one count per stat in the stats' design order, at least one point in total.
    UPROPERTY() TArray<uint8> PointCounts;
    // #198 B OpeningBase / InstallComponent / UninstallComponent: the component definition.
    UPROPERTY() FLDDefinitionWire Component;
    // OpeningBase: install it in the same commit when allowed (like bLearnIfPossible), else keep it in the inventory.
    UPROPERTY() bool bInstallIfPossible = false;
    // InstallComponent (and OpeningBase with bInstallIfPossible): the target kind and binding.
    UPROPERTY() ELDComponentTargetKind TargetKind = ELDComponentTargetKind::None;
    UPROPERTY() FLDDefinitionWire TargetDefinition;
    UPROPERTY() uint8 TargetSegment = 0;
    UPROPERTY() FLDDefinitionWire BoundActive;
    // #198 F InstallComponent of C12: the installed base component its exit repeats.
    UPROPERTY() FLDDefinitionWire BoundComponent;
    UPROPERTY() ELDDemoElement Element = ELDDemoElement::None;
    // #198 G ComponentChoice: OfferId names the pending choice and Component the chosen option; an all-zero Component takes
    // the empty-pool points, allowed only once every option is owned. FillGap: Component is the gap target (an owned
    // mod/connector) and Filler the unowned common base received. Dismantle: Surrendered lists the owned common components
    // given up (distinct, GROWTH.DismantleInputCount of them).
    UPROPERTY() FLDDefinitionWire Filler;
    UPROPERTY() TArray<FLDDefinitionWire> Surrendered;
    // InstallComponent only: an installed component taken off in the same commit (with what sits on it) to make room or
    // clear a form conflict; all-zero = a plain install.
    UPROPERTY() FLDDefinitionWire Replacing;

    bool SameTerms(const FLDBuildCommand& Other) const;
};

USTRUCT()
struct LOCKDOWN_API FLDBuildReceipt
{
    GENERATED_BODY()

    UPROPERTY() FLDGrowthReceiptHeader Header;
    UPROPERTY() ELDBuildCommandKind Kind = ELDBuildCommandKind::None;
    // ActiveChoice: the consumed offer and its opportunity; BuyNpcOffer: the bought opportunity (OfferId stays unset,
    // its drawn offer is NextOfferId).
    UPROPERTY() FGuid OfferId;
    UPROPERTY() uint8 OpportunityOrdinal = 0;
    // ActiveChoice: the chosen active; LearnBlueprint and ReplaceActive: the learned blueprint.
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
    // #198 B component kinds: the component, whether it is installed after the commit, and (OpeningBase stored without
    // install) why not (InvalidTarget); None when not asked.
    UPROPERTY() FName ComponentId;
    UPROPERTY() bool bComponentInstalled = false;
    UPROPERTY() uint16 InstallStoredReason = 0;
    UPROPERTY() bool bHasNextOffer = false;
    UPROPERTY() FGuid NextOfferId;
    // ReplaceActive and ForgetActive: the forgotten active.
    UPROPERTY() FName ForgottenId;
    // Respec: the allocated points returned to the unspent ones.
    UPROPERTY() int64 PointsRefunded = 0;
    // SkipActiveOffer: points awarded for permanently giving up this opportunity.
    UPROPERTY() int64 PointsAwarded = 0;
    // BuyNpcOffer: the points permanently paid.
    UPROPERTY() int64 NpcPointsPaid = 0;
    // #198 G: ComponentChoice / FillGap / Dismantle put ComponentId (the received component; None when a ComponentChoice
    // took the empty-pool points) into the inventory. FillGap: the points permanently paid. Dismantle: the surrendered
    // components and every installed mod/connector left without its host that went back to the inventory. InstallComponent
    // with Replacing: the replaced component and what sat on it, back in the inventory.
    UPROPERTY() int64 ServicePointsPaid = 0;
    UPROPERTY() TArray<FName> RemovedComponents;
    UPROPERTY() TArray<FName> UninstalledComponents;
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

// #198 B: one normal level-up's component reward: the granted definition, or None when the common pool was exhausted and
// GROWTH.EmptyComponentPoolPoints were earned instead.
struct FLDComponentGrant
{
    uint8 Level = 0;
    FName Component;

    bool operator==(const FLDComponentGrant& Other) const { return Level == Other.Level && Component == Other.Component; }
};

// One assister's part of a contender kill reward, committed in the same composite as the killer's part.
struct LOCKDOWN_API FLDRewardShareReceipt
{
    FGuid Recipient;
    int64 XPDelta = 0;
    uint8 LevelBefore = 0;
    uint8 LevelAfter = 0;
    uint8 OpportunityFirst = 1;
    uint8 OpportunityLast = 0;
    // #198 B: the component of each level crossed, in level order.
    TArray<FLDComponentGrant> ComponentGrants;
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
    // #198 B: the component of each level crossed, in level order (None: the pool was exhausted, points instead).
    TArray<FLDComponentGrant> ComponentGrants;
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
    // #198 B whitelists of client-sent enums: build command kinds, install target kinds and elements.
    LOCKDOWN_API bool IsKnownBuildKind(ELDBuildCommandKind Kind);
    LOCKDOWN_API bool IsKnownTargetKind(ELDComponentTargetKind Kind);
    LOCKDOWN_API bool IsKnownElement(ELDDemoElement Element);
    // An optional definition field: all-zero bytes are None (true); anything else must decode.
    LOCKDOWN_API bool DecodeOptional(const FLDDefinitionWire& Wire, FName& Out);
    // Only an Alive or Dead contender may send a build command; Unspawned/Left never can.
    LOCKDOWN_API bool IsBuildLifePhase(ELDLifePhase Phase);
    LOCKDOWN_API const TCHAR* ReasonName(ELDGrowthReason Reason);
}
