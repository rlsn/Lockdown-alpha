#pragma once

#include "CoreMinimal.h"
#include "Combat/LDAttributeProjection.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Development/LDGrowthProtocol.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "Framework/LDCaptureTypes.h"
#include "Vision/LDViewTypes.h"
#include "Development/LDProgressionRules.h"
#include "World/LDFieldPacing.h"

class FLDDemoContentSnapshot;

// Pure opponent strategy over one already-published, authorized read copy (docs/systems/OPPONENT_AI.md §6).
// It never reads a world, actor, clock or hidden state: the server adapter fills the frame from the
// same owner, observation and public projections a human player receives, then submits each intent to its
// owning domain, which revalidates everything at its own commit point.

enum class ELDOpponentMode : uint8
{
    Stopped, DeadWaiting, Disengage, DefendOrHold, Choose, Engage, Contest, Farm, Explore,
    // D26: going for a 天降外卖; not a contest of the circle, so a stop-loss exit from it never counts against the capture.
    Airdrop,
    // P6 spec §10.3: going for a live mini flag or the arena.
    Objective
};

// Bounded diagnostic cause of a decision; never a coordinate or private source.
enum class ELDOpponentReason : uint8
{
    None, Stopped, ProviderUnavailable, DeadWaiting, Recover, PublicCaptureUrgent, HoldCapture, ChooseOffer,
    EngageTarget, ContestCapture, FarmCamp, Explore, NoLegalTarget,
    TargetSuppressed, GuardGraffiti, BesiegeOwner, ContestAirdrop, HuntLeader, AvoidBoss,
    // P6 spec §10.3: a live mid-field objective within reach.
    ContestObjective,
    // #305 spec §11.3: a seen, still stocked delivery locker within reach, and the visible second-hand dealer.
    OpenLocker, ChaseDealer,
    // #198 §16.3: a visible shared training dummy engaged in the rest build window once nothing is left to build.
    Train, RestService
};

enum class ELDOpponentSourceSlot : uint8 { Q, W, E, Dash };

// One learned Q/W/E active (#198: an active has one level).
struct FLDOpponentActive
{
    FString DefinitionId;
    // 0 = Q, 1 = W, 2 = E.
    uint8 Slot = 0;
    // The skill's branch path; its rank follows from it (None = rank 1).
    ELDSkillBranch Branch = ELDSkillBranch::None;
};

// A visible enemy warning (a ground point, trap, cone or camp telegraph) about to resolve: its footprint and remaining time.
struct FLDOpponentWarning
{
    FGuid Handle;
    FVector CentreCm = FVector::ZeroVector;
    FVector Heading = FVector::ForwardVector;
    double RadiusCm = 0, LengthCm = 0, AngleDegrees = 0;
    int64 RemainingMs = 0;
};

// One bound active source exactly as the authority's availability read reports it; no second cooldown.
struct FLDOpponentSource
{
    ELDOpponentSourceSlot Slot = ELDOpponentSourceSlot::Q;
    FString DefinitionId;
    bool bReady = false;
    // The authority's current reach of this active (the innate dash distance with held boosts); never recomputed.
    int32 RangeCm = 0;
};

// The current active offer (#198): one to three distinct actives that never change until one is chosen.
struct FLDOpponentOffer
{
    FGuid OfferId;
    uint64 OfferRevision = 0;
    uint8 Ordinal = 0;
    TArray<FString> Options;
};

// The points the own build allocated to one catalog stat.
struct FLDOpponentStatPoints
{
    FString StatId;
    int32 Points = 0;
};

// One owned card (copies are separate cards): its serial, definition and, once installed, its target (a learned skill for a
// skill card, Y25's element).
struct FLDOpponentCard
{
    uint16 Serial = 0;
    FString DefinitionId;
    ELDCardTargetKind TargetKind = ELDCardTargetKind::None;
    FString TargetActive;
    ELDDemoElement Element = ELDDemoElement::None;
    bool IsInstalled() const { return TargetKind != ELDCardTargetKind::None; }
};

// The current card offer of the own view.
struct FLDOpponentCardOffer
{
    FGuid OfferId;
    uint64 OfferRevision = 0;
    TArray<FString> Options;
};

// The AI's own authorized projection: owner life, progression and source readiness.
struct FLDOpponentSelf
{
    FGuid ContenderId;
    ELDLifePhase Phase = ELDLifePhase::Unspawned;
    uint32 Life = 0;
    FVector FootCm = FVector::ZeroVector;
    // The own roster seat (its ground states are its own; 255 = unknown).
    uint8 Seat = 255;
    int64 HpP2 = 0;
    int64 MaxHpP2 = 0;
    int64 MomentumP2 = 0;
    int64 MaxMomentumP2 = 0;
    FLDProjectedAttributes Attributes;
    // Authority ms at which the next basic AttackStart may begin; at or before now when ready.
    int64 BasicReadyAtMs = 0;
    // Authority ms until which authorized control blocks voluntary movement; at or before now when free.
    int64 MoveBlockedUntilMs = 0;
    bool bChanneling = false;
    // The same nearby-service owner projection a human receives; never another contender's position.
    ELDRestService NearbyRestService = ELDRestService::None;
    uint64 BuildRevision = 0;
    uint8 Level = 1;
    TArray<FLDOpponentActive> Actives;
    // Unspent skill points (issue #287 spec §4.5).
    int32 SkillPoints = 0;
    // Chosen actives waiting for an empty slot (待学图纸), sorted by id.
    TArray<FString> Blueprints;
    // One row per stat, in the stats' design order (FLDDemoContentDefinitions::StatsInDesignOrder).
    TArray<FLDOpponentStatPoints> StatPoints;
    int64 UnspentPoints = 0;
    // Active opportunities consumed so far, the opening one included.
    uint8 OffersConsumed = 0;
    // Active opportunities earned and not chosen yet, the current offer's included.
    uint8 QueuedOpportunities = 0;
    // Spec §3.13: the own vouchers and rest shop.
    int32 Vouchers = 0;
    FLDShopView Shop;
    // #305: the vending machine within reach (own pacing view; None = none).
    FName NearbyVending;
    // Owned cards by serial, the level's card capacity and the current card offer.
    TArray<FLDOpponentCard> Cards;
    int32 CardCapacity = 0;
    TOptional<FLDOpponentCardOffer> CardOffer;
    TArray<FLDOpponentSource> Sources;
    TOptional<FLDOpponentOffer> Offer;
    // Phase 9: the untaken final mod three-choice (an invalid id when none is open) and whether the build is locked.
    FGuid FinalModOfferId;
    TArray<FString> FinalModOptions;
    TArray<FLDFinalModSourceView> FinalModSources;
    // #153: this adapter's own server-random opening timing (drawn once per match, never a hidden fact): the authority
    // ms from which it makes its countdown build decisions.
    int64 OpeningPickAtMs = 0;
};

// Guarded: only the graffiti owner stands in the spray zone.
enum class ELDOpponentCircle : uint8 { Empty, Solo, Contested, Guarded };

// Public match state shared with every participant; a sprayer or graffiti owner never carries a position.
struct FLDOpponentPublicMatch
{
    ELDMatchPhase Phase = ELDMatchPhase::Faulted;
    int64 NowMs = 0;
    // The public match start (FLDPublicMatchView::StartedAtMs, the opening rest R0 begins).
    int64 StartedAtMs = 0;
    // #198: the public timeline (its build stage is LDProgression::StageForPublic(Phase, Timeline)) and the field
    // progress T at NowMs; #97 camp growth stages count T.
    FLDTimelineView Timeline;
    int64 FieldNowMs = 0;
    LDProgression::EMatchStage BuildStage() const { return LDProgression::StageForPublic(Phase, Timeline); }
    ELDOpponentCircle Circle = ELDOpponentCircle::Empty;
    // The contender holding spray progress: the sole sprayer, or the holder a contest only paused.
    // Invalid when nobody holds progress.
    FGuid Occupier;
    // Real instant the sole sprayer's graffiti lands while Solo; zero otherwise.
    int64 DueAtMs = 0;
    // The contender whose graffiti the flagpole shows; invalid while it is bare.
    FGuid Owner;
    // Real instant the owner's running guard wins; zero without an owner, while the owner is away from an empty zone
    // (paused) or while anyone else in the zone (a sole sprayer or a contest) freezes it: only Guarded runs it.
    int64 GuardDueAtMs = 0;
    // D26: the public point of the current airdrop while it is announced or on the ground; None otherwise.
    FName AirdropPOI;
    bool bAirdropLanded = false;
    // #198 outer §5: the camps whose public marker is open (revealed and not yet taken) now.
    TArray<FName> MarkedCamps;
    // D27: the public 上热搜 notice; Leader invalid when nobody is named.
    FGuid Leader;
    uint8 LeaderLevel = 0;
    FVector LeaderCircleCentreCm = FVector::ZeroVector;
    // P6 spec §10.3: the public mid-field objective (Serial 0: none yet this segment).
    FLDObjectiveView Objective;
    // The public room roster's contender ids (FLDLobbyView members), as every participant's HUD reads them; the adapter reads
    // it in a rest, where visible contender units outside it are shared training dummies (LDHUD::ObservedContenderName).
    TArray<FGuid> Roster;
};

enum class ELDOpponentUnitKind : uint8 { Contender, CampMember };

// One currently authorized visible unit. Handles are observer-local and fresh per visibility interval.
struct FLDOpponentUnit
{
    FGuid Handle;
    uint32 Epoch = 0;
    ELDOpponentUnitKind Kind = ELDOpponentUnitKind::Contender;
    FGuid ContenderId;
    FString MonsterId;
    FVector FootCm = FVector::ZeroVector;
    // Unset when the field is not authorized or not applicable; never a guessed zero.
    TOptional<int64> HpP2;
    TOptional<int64> MaxHpP2;
    bool bReturning = false;
    bool bProtected = false;
    // The element it visibly holds (ELDCombatElement from its public statuses; 0 = none or not authorized).
    uint8 HeldElement = 0;
    // Spec §11.3: the visible second-hand dealer (its public look).
    bool bPeddler = false;
};


// A public ground state in sight (spec §13): its kind (ELDGroundStateKind), footprint and whether an enemy owns it.
struct FLDOpponentGround
{
    uint8 Kind = 0;
    FLDObservedGroundState View;
    bool bEnemy = false;
};

// Service places are carried only by rest frames, not the field POI list.
// Locker (#305): a delivery locker this contender has seen and not opened (its own pacing view); never a static place.
enum class ELDOpponentPOIKind : uint8 { Spawn, Camp, Capture, Dorm, Airdrop, MiniFlag, RestService, Locker };

// Static public place from the approved world and content; never a live occupancy fact.
struct FLDOpponentPOI
{
    FName Id;
    ELDOpponentPOIKind Kind = ELDOpponentPOIKind::Spawn;
    FName SurfaceId;
    FVector FootCm = FVector::ZeroVector;
    // Camps only.
    FString MonsterId;
    ELDDemoCampTier Tier = ELDDemoCampTier::Low;
    ELDRestService RestService = ELDRestService::None;
};

enum class ELDOpponentOrderKind : uint8 { None, MoveGround, AttackUnit, Action };
enum class ELDOpponentOrderStatus : uint8 { None, Pending, Active, Completed, Failed, Cancelled };

// The AI's own latest spatial order as its order receipts report it.
struct FLDOpponentOrder
{
    ELDOpponentOrderKind Kind = ELDOpponentOrderKind::None;
    ELDOpponentOrderStatus Status = ELDOpponentOrderStatus::None;
    FName POI;
    FGuid Handle;
    uint32 Epoch = 0;
    bool IsLive() const { return Status == ELDOpponentOrderStatus::Pending || Status == ELDOpponentOrderStatus::Active; }
};

struct FLDOpponentDecisionFrame
{
    uint32 Generation = 0;
    uint32 ViewRevision = 0;
    FLDOpponentSelf Self;
    FLDOpponentPublicMatch Public;
    TArray<FLDOpponentUnit> Units;
    // Visible enemy warnings (issue #287 spec §13 dodging).
    TArray<FLDOpponentWarning> Warnings;
    // Public ground states in sight (spec §13: never stand in an enemy's).
    TArray<FLDOpponentGround> Grounds;
    TArray<FLDOpponentPOI> POIs;
    FLDOpponentOrder Order;
    // Complete-path length from the current pose to the capture entry, when the world answered.
    TOptional<double> CapturePathCm;
    // A build request awaits its final receipt; no second build change is proposed meanwhile.
    bool bBuildChangeInFlight = false;
    TArray<FLDOpponentPOI> RestServicePOIs;
};

// Walk: the body's own walking, refused for its ground or path from where it stands (#160); fingerprinted by that spot.
// Build: one build command (#198 active choice, blueprint learn, point allocation, opening base pick, component install or
// uninstall, component choice, or a rest service: gap fill, dismantle, NPC purchase, respec, active replacement), keyed by
// its kind and subject.
enum class ELDOpponentFailure : uint8 { Path, Engagement, Activation, Build, Walk };

struct FLDOpponentRetryKey
{
    ELDOpponentFailure Category = ELDOpponentFailure::Path;
    FName POI;
    FGuid Target;
    uint8 Slot = 0;
    bool operator==(const FLDOpponentRetryKey& Other) const
    {
        return Category == Other.Category && POI == Other.POI && Target == Other.Target && Slot == Other.Slot;
    }
};

enum class ELDOpponentBodyKind : uint8 { None, Keep, Stop, MoveGround, AttackUnit, Activate };

struct FLDOpponentBodyIntent
{
    ELDOpponentBodyKind Kind = ELDOpponentBodyKind::None;
    FName POI;
    FGuid Handle;
    uint32 Epoch = 0;
    ELDOpponentSourceSlot Source = ELDOpponentSourceSlot::Q;
    ELDDemoTargetMode TargetMode = ELDDemoTargetMode::Self;
    FVector Direction = FVector::ZeroVector;
    // GroundPoint activations: the requested zone centre (#147 active_quick_meeting). A MoveGround without a POI: the
    // ground point clicked, on Surface.
    FVector Point = FVector::ZeroVector;
    // A MoveGround to Point: the walkable surface it is on (the surface of a registered public place); None otherwise.
    FName Surface;
    // Failure memory of MoveGround, AttackUnit and Activate; the final receipt settles against it.
    FLDOpponentRetryKey Key;
    // Activate: release it as an overdrive (the adapter holds the charge for OVERDRIVE.HoldSec first).
    bool bOverdrive = false;
};

// #198 build commands, as a player sends them: choose an active of the current offer (always asking to learn it now),
// learn a pending blueprint into an empty slot, allocate unspent points, pick the opening base component (always asking to
// enable it now) or install an owned component that is not installed (a first install, or in a rest window a move of a
// once-installed one). #198 G: resolve a pending component choice (an option, or the empty-pool points once every option
// is owned). The rest services: fill a gap, dismantle common components, uninstall an installed component (with what sits
// on it), buy the NPC active offer, respec the allocated points, or replace a learned active with a pending blueprint (the
// attachments of the old one return to the inventory in the same commit).
enum class ELDOpponentBuildKind : uint8
{
    // ShopBuy: spec §13 rest shop purchase of shelf slot Slot (or the skill point) of stock OfferRevision.
    None, ChooseActive, LearnBlueprint, AllocatePoints, ShopBuy, Respec, ReplaceActive, FinalMod,
    // Issue #287 spec §13: a skill point raises a learned skill (Slot) to its next branch (Branch).
    RankUpSkill,
    // Spec §13: a card of the current card offer (installed on its default place when one is free), and an owned card into a
    // free slot. DEV build evaluation also skips offers and takes cards off.
    PickCard, InstallCard, SkipCards, UninstallCard
};

struct FLDOpponentBuildIntent
{
    ELDOpponentBuildKind Kind = ELDOpponentBuildKind::None;
    // ChooseActive: the offer and its chosen active.
    FGuid OfferId;
    uint64 OfferRevision = 0;
    FString Option;
    // RankUpSkill: the next branch path of the skill in Slot.
    ELDSkillBranch Branch = ELDSkillBranch::None;
    // FinalMod uses OfferId/Option and TargetKind/TargetId, independently of active slots.
    // LearnBlueprint: the blueprint and the empty slot 0..2; ReplaceActive: the blueprint and the learned slot 0..2.
    FString Blueprint;
    uint8 Slot = 0;
    // AllocatePoints: one count per stat, in the stats' design order.
    TArray<uint8> PointCounts;
    // PickCard: the offer (OfferId/OfferRevision) and the card (Option); InstallCard: the owned card. Both carry the target.
    uint16 CardSerial = 0;
    ELDCardTargetKind CardTarget = ELDCardTargetKind::None;
    FString TargetId;
    ELDDemoElement Element = ELDDemoElement::None;
    // FinalMod: the source kind (TargetId names the active).
    ELDFinalModTargetKind FinalTargetKind = ELDFinalModTargetKind::None;
    FLDOpponentRetryKey Key;
};

struct FLDOpponentDecision
{
    ELDOpponentMode Mode = ELDOpponentMode::Stopped;
    ELDOpponentReason Reason = ELDOpponentReason::None;
    FLDOpponentBodyIntent Body;
    FLDOpponentBuildIntent Build;
    // #305 spec §11.3: a vending purchase at the machine within reach (ELDVendingItem; 0 = none).
    uint8 Vending = 0;
};

// What a domain's final answer to one intent teaches the failure memory (OPPONENT_AI.md §3.3).
// TargetLost covers TargetUnavailable/TargetDied: the unit is dropped, never chased to its last position.
enum class ELDOpponentOutcome : uint8 { Succeeded, Failed, TargetLost };

// Bounded failure memory (§6.5): attempts include the first, the wait doubles up to the cap, and an
// exhausted key stays suppressed until the caller's condition fingerprint changes. Ordinary frames,
// revisions caused by the AI itself and cache expiry never refund attempts.
class LOCKDOWN_API FLDOpponentRetryLedger
{
public:
    // #97: path and engagement keys for every public place of the larger world.
    static constexpr int32 Capacity = 96;
    bool CanAttempt(const FLDOpponentRetryKey& Key, uint64 Fingerprint, int64 NowMs) const;
    bool IsSuppressed(const FLDOpponentRetryKey& Key, uint64 Fingerprint) const;
    void RecordFailure(const FLDOpponentRetryKey& Key, uint64 Fingerprint, int64 NowMs, const FLDDemoAIParameters& AI);
    void Suppress(const FLDOpponentRetryKey& Key, uint64 Fingerprint);
    void RecordSuccess(const FLDOpponentRetryKey& Key);
    void Reset() { Entries.Reset(); }
    int32 Num() const { return Entries.Num(); }

private:
    struct FEntry
    {
        FLDOpponentRetryKey Key;
        uint64 Fingerprint = 0;
        int64 Attempts = 0;
        int64 RetryAtMs = 0;
        bool bSuppressed = false;
        uint64 Order = 0;
    };
    const FEntry* Find(const FLDOpponentRetryKey& Key) const;
    FEntry& FindOrAdd(const FLDOpponentRetryKey& Key, uint64 Fingerprint);
    TArray<FEntry> Entries;
    uint64 NextOrder = 0;
};

struct FLDOpponentVisit
{
    FName POI;
    int64 VisitedAtMs = 0;
};

// Strategy-only memory: never a second body, cooldown or camp table.
struct FLDOpponentPolicyState
{
    uint32 Generation = 0;
    uint32 Life = 0;
    ELDOpponentMode Mode = ELDOpponentMode::Stopped;
    bool bRecovering = false;
    // The public place this recovery escapes toward; kept while it still leads away from visible threats.
    FName RetreatPOI;
    // D26 (#160): the walk to RetreatPOI steps away from the Boss and continues while the Boss stays in sight, so the
    // body does not turn back at the edge of the Boss's band and flip between the escape and its errand.
    bool bEscapingBoss = false;
    // #160: the goal of the last walk navigation refused for its ground or path; a different refused goal from the
    // same standing point counts against the body's own walking.
    FLDOpponentRetryKey LastRefusedWalk;
    bool bHasRefusedWalk = false;
    FName VisitPOI;
    int64 VisitEngagedAtMs = 0;
    // #305: when the last vending purchase was asked for (0 = never this match).
    int64 VendingAskedAtMs = 0;
    FName ExplorePOI;
    FGuid TargetHandle;
    uint32 TargetEpoch = 0;
    int64 TargetEngagedAtMs = 0;
    TArray<FLDOpponentVisit> Visits;
    FLDOpponentRetryLedger Failures;
    // Spec §13 breakout goal: the breakout preset (LDBreakoutPresets) this AI chases this match, None for none; drawn once.
    FName BreakoutGoal;
    bool bBreakoutDrawn = false;
};

enum class ELDCampCostStatus : uint8 { Ok, NotFeasible, EstimateUnavailable };

struct FLDCampCostInput
{
    int64 CurrentHpP2 = 0;
    int64 MaxHpP2 = 0;
    FLDProjectedAttributes Attributes;
    // Wait before the AI's next basic AttackStart may begin; zero when ready.
    int64 BasicReadyInMs = 0;
    int64 EngagementBudgetMs = 0;
};

// Conservative stand-still model of one full public camp template (§6.2, CONTENT_AND_BALANCE §7.2):
// no crit, ability, shield, lifesteal, cleave or regeneration credit; retaliation starts at the first
// confirmed hit and a retaliation landing at a member's death time still counts.
struct FLDCampCostEstimate
{
    ELDCampCostStatus Status = ELDCampCostStatus::EstimateUnavailable;
    int64 ClearMs = 0;
    int64 DamageTakenP2 = 0;
    int64 RemainingHpP2 = 0;
    int64 PlayerHits = 0;
    int64 RetaliationHits = 0;
    FString Error;
};

struct FLDOpponentUsage
{
    bool bEngage = false;
    bool bSelfProtect = false;
    bool bMobility = false;
};

struct FLDCaptureUrgencyInput
{
    int64 NowMs = 0;
    int64 DueAtMs = 0;
    bool bInside = false;
    TOptional<double> PathCm;
    FLDDemoRational MoveSpeedCmPerSec;
    int64 MoveBlockedUntilMs = 0;
    int64 ReactionBudgetMs = 0;
};

namespace LDOpponentPolicy
{
    // Retaliation swings starting at RetaliateFromMs that land at or before DeathMs.
    LOCKDOWN_API int64 RetaliationHits(int64 RetaliateFromMs, int64 DeathMs, int64 IntervalMs, int64 WindupMs);
    LOCKDOWN_API FLDCampCostEstimate EstimateCampCost(const FLDCampCostInput& Input,
        const FLDDemoMonsterDefinition& Monster, const FLDDemoContentSnapshot& Catalog);
    LOCKDOWN_API FLDOpponentUsage AbilityUsage(const FLDDemoAbilityDefinition& Ability);
    // #198 active preference (OPPONENT_AI.md §6.4): an active that brings a role (engagement or self-protection) the learned
    // actives and blueprints lack comes first; then the fixed target-mode order Unit > GroundPoint > DirectionCone =
    // Issue #287 spec §13: the learned skill below rank 3 that deals the most damage now (lower slot on ties) and its next
    // branch (A, then A1 or B1). False without such a skill.
    LOCKDOWN_API bool PickRankUp(const FLDOpponentSelf& Self, const FLDDemoContentSnapshot& Catalog, uint8& OutSlot,
        ELDSkillBranch& OutBranch);
    // DirectionLine > DirectionProjectile > Self > DirectionDash; then catalog order. Picks among the current offer's options.
    LOCKDOWN_API bool SelectActive(const FLDOpponentSelf& Self, const FLDDemoContentSnapshot& Catalog, FString& Out, FString& Error);
    // The pending blueprint to learn first by the same preference and the lowest empty Q/W/E slot; false when either is missing.
    LOCKDOWN_API bool SelectBlueprint(const FLDOpponentSelf& Self, const FLDDemoContentSnapshot& Catalog, FString& OutBlueprint,
        uint8& OutSlot, FString& Error);
    // #198 rest replacement (rule-based, no tunable numbers): with Q, W and E all learned, the learned active the same
    // preference ranks lowest against the roles of the other two, and the pending blueprint it ranks highest against those
    // two; that blueprint and the low active's slot only when the blueprint ranks strictly higher, false otherwise. A
    // replacement forgets the old active (it never becomes a blueprint again), so each one spends a blueprint and they end.
    LOCKDOWN_API bool SelectReplacement(const FLDOpponentSelf& Self, const FLDDemoContentSnapshot& Catalog, FString& OutBlueprint,
        uint8& OutSlot, FString& Error);
    // Every unspent point (at most 255 per stat in one command), each to the stat with the fewest points among the relevant
    // set: physique and thick skin always; strength when a learned active or the basic attack scales with AD; brains when a
    // learned active scales with AP. Ties go to the stats' design order, which also orders the counts; false without unspent points.
    LOCKDOWN_API bool AllocatePoints(const FLDOpponentSelf& Self, const FLDDemoContentSnapshot& Catalog, TArray<uint8>& Out,
        FString& Error);
    // #198 rest respec: some allocated point sits in a stat outside AllocatePoints' relevant set (after a replacement took the
    // last AP or AD active away, say). After the respec AllocatePoints spends only into that set, so it does not repeat
    // until the learned actives change. False with Error when the stat rows are not the catalog's.
    LOCKDOWN_API bool WantsRespec(const FLDOpponentSelf& Self, const FLDDemoContentSnapshot& Catalog, FString& Error);
    // Spec §13 rest shop: with at least the skill point's price in vouchers and points left to buy this match, the skill point
    // (OutSlot FLDBuildCommand::ShopSkillPointSlot); otherwise the affordable, unsold shelf card scoring highest like a card
    // pick (SelectCard's score). The AI never buys skills and never sells. False when nothing qualifies.
    LOCKDOWN_API bool SelectShopBuy(const FLDOpponentSelf& Self, const FLDDemoContentSnapshot& Catalog, FName Goal, uint8& OutSlot);
    // Spec §13: an offer none of whose cards shares a vocabulary key with the build is skipped for vouchers with
    // AI.CardSkipChance (a deterministic draw of the offer).
    LOCKDOWN_API bool WantsCardSkip(const FLDOpponentSelf& Self, const FLDDemoContentSnapshot& Catalog);
    // Spec §13 breakout goal: with AI.BreakoutChance an AI takes one breakout preset (LDBreakoutPresets) as its goal for the
    // match, drawn deterministically from its id and the match generation; None otherwise.
    LOCKDOWN_API FName DrawBreakoutGoal(const FGuid& Contender, uint32 Generation, const FLDDemoAIParameters& AI);
    // Spec §13 card pick: each option scores AI.CardVocabScore per vocabulary key it shares with the installed cards and
    // learned skills, AI.BreakoutCardScore more when it belongs to Goal, plus a jitter in [0, 1) drawn from the offer and the
    // card; the highest score wins (WantsCardSkip decides a skip first). False without an offer.
    LOCKDOWN_API bool SelectCard(const FLDOpponentSelf& Self, const FLDDemoContentSnapshot& Catalog, FName Goal, FString& OutCard,
        FString& Error);
    // The place the AI puts a card (LDProgression::DefaultCardTarget over the own build); false when none exists now.
    LOCKDOWN_API bool CardTarget(const FLDOpponentSelf& Self, const FLDDemoContentSnapshot& Catalog, const FString& Card,
        FLDCardTarget& Out);
    // The first stored card (serial order) with a place while a slot is free; false when none or the workshop is full.
    LOCKDOWN_API bool SelectCardInstall(const FLDOpponentSelf& Self, const FLDDemoContentSnapshot& Catalog, uint16& OutSerial,
        FLDCardTarget& OutTarget);
    // True when the public solo deadline leaves no more than travel, control and reaction budget.
    // An unknown path or zero speed is conservatively urgent; it never divides by zero.
    LOCKDOWN_API bool IsCaptureDefenseUrgent(const FLDCaptureUrgencyInput& Input);
    // True when the own foot is inside the public capture circle of the frame's static capture POI.
    LOCKDOWN_API bool InsideCircle(const FLDOpponentDecisionFrame& Frame, const FLDDemoContentSnapshot& Catalog);
    // The owner's visible training target by the HUD's roster rule: lowest handle first; nullptr
    // outside a rest, without one, or when the own id is not on the roster (an unread roster names nothing).
    LOCKDOWN_API const FLDOpponentUnit* TrainingDummy(const FLDOpponentDecisionFrame& Frame);
    LOCKDOWN_API ELDRestService RestServiceForBuild(ELDOpponentBuildKind Kind);
    LOCKDOWN_API uint64 Fingerprint(ELDOpponentFailure Category, const FLDOpponentSelf& Self);
    LOCKDOWN_API FLDOpponentDecision Decide(const FLDOpponentDecisionFrame& Frame,
        const FLDDemoContentSnapshot& Catalog, FLDOpponentPolicyState& State);
    // Consumes a domain's final result for an intent; success clears, failure backs off or suppresses, and a
    // lost unit target is suppressed for this visibility interval and released as the current target.
    LOCKDOWN_API void RecordOutcome(FLDOpponentPolicyState& State, const FLDOpponentRetryKey& Key,
        ELDOpponentOutcome Outcome, const FLDOpponentSelf& Self, int64 NowMs, const FLDDemoAIParameters& AI);
    // #160: the admission answer to a walk (a ground move or attack chase). A refusal for its ground
    // or path that names a different goal than the last one counts once against the body's own walking from where it
    // stands; within the retry policy every new walk waits, and once it is spent none is sent until the body has moved
    // or respawned. Any admitted walk clears it.
    LOCKDOWN_API void RecordWalkAdmission(FLDOpponentPolicyState& State, const FLDOpponentRetryKey& Goal, bool bRefusedForGround,
        const FLDOpponentSelf& Self, int64 NowMs, const FLDDemoAIParameters& AI);
    // The build step after any offer choice, exactly as Decide makes it (OPPONENT_AI.md §6.4): the card offer's pick
    // (SelectCard), the preferred blueprint into the lowest empty slot when learning is allowed; in a rest build window then
    // the replacement (SelectReplacement); a stored card into a free slot (SelectCardInstall); a respec (WantsRespec); then
    // every unspent point when allocating is allowed (a rest build window, dead, or alive inside the window). It leaves offers to its caller: it never chooses one and never buys the
    // NPC's (Decide does, before the points). False when nothing applies, a build request is in flight or failure memory
    // holds the step back.
    LOCKDOWN_API bool DecideFollowUpBuild(const FLDOpponentDecisionFrame& Frame, const FLDDemoContentSnapshot& Catalog,
        FLDOpponentPolicyState& State, FLDOpponentBuildIntent& Out);
    // Issue #287 spec §13 path cost: the length of a walked ground polyline, its part inside a visible enemy ground state
    // weighed by CostBp (AI.EnemyGroundPathCost; 10000 = 1). Sampled every GroundPathSampleCm along each segment.
    LOCKDOWN_API double GroundWeightedCm(TConstArrayView<FVector> Points, TConstArrayView<FLDOpponentGround> Grounds, int64 CostBp);
    // Detour waypoints for a walk From -> To: for each visible enemy ground state the straight walk passes through or
    // near, one point on either side of it just outside its footprint. Empty when no enemy ground state is in the way.
    LOCKDOWN_API void GroundDetours(const FVector& From, const FVector& To, TConstArrayView<FLDOpponentGround> Grounds,
        TArray<FVector>& Out);
    constexpr double GroundPathSampleCm = 25.;
    constexpr double GroundDetourMarginCm = 120.;
#if WITH_DEV_AUTOMATION_TESTS
    // DEV build evaluation (docs/systems/BUILD_EVALUATION.md): the production engagement of one visible unit exactly as
    // Decide engages it (activation order §6.3, dash chase, then the basic attack, with the same failure memory). A fixed
    // evaluation strategy chooses only whom to engage; how to fight stays this policy's. None when the frame cannot
    // initialize the policy context.
    LOCKDOWN_API FLDOpponentBodyIntent EngageUnit(const FLDOpponentDecisionFrame& Frame, const FLDDemoContentSnapshot& Catalog,
        FLDOpponentPolicyState& State, const FLDOpponentUnit& Target);
    // The production walk to one public place (keeps a live walk there and honours refused-walk memory). None when the
    // place is unknown or the context cannot initialize.
    LOCKDOWN_API FLDOpponentBodyIntent WalkTo(const FLDOpponentDecisionFrame& Frame, const FLDDemoContentSnapshot& Catalog,
        FLDOpponentPolicyState& State, FName POI);
    // #198: the production walk to the rest service NPC a build command needs (the same route Decide takes before a service
    // command). None when no such service is published or the context cannot initialize.
    LOCKDOWN_API FLDOpponentBodyIntent WalkToRestService(const FLDOpponentDecisionFrame& Frame, const FLDDemoContentSnapshot& Catalog,
        FLDOpponentPolicyState& State, ELDRestService Service);
#endif
}
