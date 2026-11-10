#pragma once

#include "CoreMinimal.h"
#include "Combat/LDAttributeProjection.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Development/LDGrowthProtocol.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "Framework/LDCaptureTypes.h"
#include "Development/LDProgressionRules.h"
#include "PK/LDPKRules.h"

class FLDDemoContentSnapshot;

// Pure opponent strategy over one already-published, authorized read copy (docs/demo/OPPONENT_AI.md §6).
// It never reads a world, actor, clock or hidden state: the server adapter fills the frame from the
// same owner, observation and public projections a human player receives, then submits each intent to its
// owning domain, which revalidates everything at its own commit point.

enum class ELDOpponentMode : uint8
{
    Stopped, DeadWaiting, Disengage, DefendOrHold, Choose, Engage, Contest, Farm, Explore,
    // D26: going for a 天降外卖; not a contest of the circle, so a stop-loss exit from it never counts against the capture.
    Airdrop,
    // #198: walking onto a lying field charm whose effect the body can use now.
    Charm,
    // #198 D: the rest PK: signing up, betting, throwing as a spectator or fighting as an entrant.
    PK
};

// Bounded diagnostic cause of a decision; never a coordinate or private source.
enum class ELDOpponentReason : uint8
{
    None, Stopped, ProviderUnavailable, DeadWaiting, Recover, PublicCaptureUrgent, HoldCapture, ChooseOffer,
    EngageTarget, ContestCapture, FarmCamp, Explore, NoLegalTarget,
    TargetSuppressed, GuardGraffiti, BesiegeOwner, ContestAirdrop, HuntLeader, AvoidBoss, TakeCharm,
    // #198 D: PK sign-up, bet and throw requests, an entrant's fight and its walk back inside the ring.
    PKSignUp, PKBet, PKThrow, PKFight, PKRing,
    // #198 §16.3: the own private training dummy engaged in the rest build window once nothing is left to build.
    Train, RestService
};

enum class ELDOpponentSourceSlot : uint8 { Q, W, E, Dash };

// One learned Q/W/E active (#198: an active has one level).
struct FLDOpponentActive
{
    FString DefinitionId;
    // 0 = Q, 1 = W, 2 = E.
    uint8 Slot = 0;
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

// #198 B: one owned component instance (inventory and installed, one per definition).
struct FLDOpponentComponent
{
    FString DefinitionId;
    bool bInstalled = false;
    // Installed at least once: enabling it again is a rest-window move, never a field install.
    bool bEverInstalled = false;
    // Where an installed component sits (FLDComponentTargetView): the target kind and id (#198 F: a learned active or a host
    // component; empty for a base, the innate D or the holder itself), B18's bound active and element and C12's repeated base.
    ELDComponentTargetKind TargetKind = ELDComponentTargetKind::None;
    FString TargetId;
    FString BoundActive;
    ELDDemoElement Element = ELDDemoElement::None;
    FString BoundComponent;
};

// #198 F: where a mod or connector goes (FLDBuildCommand's TargetKind / TargetDefinition / BoundComponent): a learned active
// (Active) or an installed host component (Component) named by TargetId, the innate D (Dash) or the holder itself (None),
// and C12's repeated installed base (empty otherwise).
struct FLDOpponentComponentTarget
{
    ELDComponentTargetKind Kind = ELDComponentTargetKind::None;
    FString TargetId;
    FString BoundComponent;
};

// #198 G: one pending elite camp component choice of the own view: its id and one or two known common components.
struct FLDOpponentComponentChoice
{
    FGuid ChoiceId;
    TArray<FString> Options;
};

// #198 G: one rest FillGap the own build allows (LDProgression::GapTargets / GapFillers over the own view): a never-enabled
// mod or connector without any compatible entry and the first unowned common base, in catalog order, that gives it one.
struct FLDOpponentGapFill
{
    FString Target;
    FString Filler;
};

// The AI's own authorized projection: owner life, progression and source readiness.
struct FLDOpponentSelf
{
    FGuid ContenderId;
    ELDLifePhase Phase = ELDLifePhase::Unspawned;
    uint32 Life = 0;
    FVector FootCm = FVector::ZeroVector;
    int64 HpP2 = 0;
    int64 MaxHpP2 = 0;
    int64 MpP2 = 0;
    int64 MaxMpP2 = 0;
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
    // Chosen actives waiting for an empty slot (待学图纸), sorted by id.
    TArray<FString> Blueprints;
    // One row per stat, in the stats' design order (FLDDemoContentDefinitions::StatsInDesignOrder).
    TArray<FLDOpponentStatPoints> StatPoints;
    int64 UnspentPoints = 0;
    // Active opportunities consumed so far, the opening one included.
    uint8 OffersConsumed = 0;
    // Active opportunities earned and not chosen yet, the current offer's included (the rest NPC charges for none meanwhile).
    uint8 QueuedOpportunities = 0;
    // #198 B: owned components sorted by id; the opening base pick while it is open and its options in catalog order.
    TArray<FLDOpponentComponent> Components;
    bool bOpeningBasePending = false;
    TArray<FString> OpeningBaseOptions;
    // #198 G: pending component choices (oldest first) and the gap fills the own build allows, in catalog order.
    TArray<FLDOpponentComponentChoice> ComponentChoices;
    TArray<FLDOpponentGapFill> GapFills;
    // Combat's build safe window (absolute authority ms; at or before now is safe).
    int64 BuildSafeAtMs = 0;
    TArray<FLDOpponentSource> Sources;
    TOptional<FLDOpponentOffer> Offer;
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
    // #198: the public airdrop points that hold a lying field charm now, with its kind, in serial order.
    TArray<TPair<FName, ELDCharmKind>> Charms;
    // #198 outer §5: the camps whose public marker is open (revealed and not yet taken) now.
    TArray<FName> MarkedCamps;
    // D27: the public 上热搜 notice; Leader invalid when nobody is named.
    FGuid Leader;
    FVector LeaderCircleCentreCm = FVector::ZeroVector;
    // #198 D: the current rest's public PK (Serial 0: none), exactly as every participant's public view shows it.
    FLDPKView PK;
    // The public room roster's contender ids (FLDLobbyView members), as every participant's HUD reads them; the adapter reads
    // it in a rest, where the visible contender unit outside it is the own private training dummy (LDHUD::ObservedContenderName).
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
};

// Sign-up and service places are carried only by rest frames, not the field POI list.
enum class ELDOpponentPOIKind : uint8 { Spawn, Camp, Capture, Dorm, Airdrop, PKSignup, RestService };

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
    TArray<FLDOpponentPOI> POIs;
    FLDOpponentOrder Order;
    // Complete-path length from the current pose to the capture entry, when the world answered.
    TOptional<double> CapturePathCm;
    // A build request awaits its final receipt; no second build change is proposed meanwhile.
    bool bBuildChangeInFlight = false;
    // #198 D: when this contender may throw again in the current PK, from its own latest throw answer (0 when none).
    int64 PKThrowReadyAtMs = 0;
    // #198 站圈报名 (user decision A on issue #198): in a rest, the own PK sign-up disk (its roster slot's public anchor);
    // unset outside a rest. Never part of POIs, so no field decision ever walks there.
    TOptional<FLDOpponentPOI> SignupPOI;
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
    // GroundPoint activations: the requested zone centre (#147 active_quick_meeting).
    FVector Point = FVector::ZeroVector;
    // Failure memory of MoveGround, AttackUnit and Activate; the final receipt settles against it.
    FLDOpponentRetryKey Key;
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
    None, ChooseActive, LearnBlueprint, AllocatePoints, OpeningBase, InstallComponent, ComponentChoice, FillGap,
    UninstallComponent, BuyNpcOffer, Respec, ReplaceActive, Dismantle
};

struct FLDOpponentBuildIntent
{
    ELDOpponentBuildKind Kind = ELDOpponentBuildKind::None;
    // ChooseActive: the offer and its chosen active.
    FGuid OfferId;
    uint64 OfferRevision = 0;
    FString Option;
    // LearnBlueprint: the blueprint and the empty slot 0..2; ReplaceActive: the blueprint and the learned slot 0..2.
    FString Blueprint;
    uint8 Slot = 0;
    // AllocatePoints: one count per stat, in the stats' design order.
    TArray<uint8> PointCounts;
    // OpeningBase / InstallComponent / UninstallComponent: the component and, for an install with an ActiveElement binding
    // (B18), the bound learned active and element (empty / None otherwise). BuyNpcOffer and Respec carry nothing else.
    FString Component;
    FString BoundActive;
    ELDDemoElement Element = ELDDemoElement::None;
    // InstallComponent of a mod or connector (#198 F): its target (LDOpponentPolicy::WorkshopTarget); None / empty for a base.
    ELDComponentTargetKind TargetKind = ELDComponentTargetKind::None;
    FString TargetId;
    FString BoundComponent;
    // #198 G ComponentChoice: the pending choice; Component is the chosen option, empty for the empty-pool points.
    // FillGap: Component is the gap target and Filler the unowned common base it receives.
    FGuid ChoiceId;
    FString Filler;
    // Dismantle: the owned common components given up, GROWTH.DismantleInputCount distinct ones in catalog order.
    TArray<FString> Surrendered;
    FLDOpponentRetryKey Key;
};

// #198 D: one PK request, as a player sends it through the same server entries: bet on one side, or throw at a ground
// point of the running arena. The sign-up is no request: the body walks into its own sign-up disk (站圈报名).
enum class ELDOpponentPKKind : uint8 { None, Bet, Throw };

struct FLDOpponentPKIntent
{
    ELDOpponentPKKind Kind = ELDOpponentPKKind::None;
    // Bet / Throw: the public PK serial the request is for.
    uint32 Serial = 0;
    // Bet: the side 1..SideCount.
    uint8 Side = 0;
    // Throw: the current foot of the chosen visible entrant.
    FVector TargetCm = FVector::ZeroVector;
};

struct FLDOpponentDecision
{
    ELDOpponentMode Mode = ELDOpponentMode::Stopped;
    ELDOpponentReason Reason = ELDOpponentReason::None;
    FLDOpponentBodyIntent Body;
    FLDOpponentBuildIntent Build;
    FLDOpponentPKIntent PK;
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
    FName ExplorePOI;
    FGuid TargetHandle;
    uint32 TargetEpoch = 0;
    int64 TargetEngagedAtMs = 0;
    TArray<FLDOpponentVisit> Visits;
    FLDOpponentRetryLedger Failures;
    // #198 D: a bet is sent once per PK whatever its answer (no retry storm): the PK serial whose bet this adapter sent.
    uint32 PKBetSerial = 0;
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
    // #198 rest NPC: buy one active offer exactly when growth would charge for it and it fills a gap: a mid rest (RestIndex
    // 1..3; never the opening rest R0, whose opening points go to the stats), no offer and no queued opportunity, at least
    // GROWTH.NpcActivePoints unspent, at least three catalog actives neither learned nor pending (the offer's draw), and an
    // empty Q/W/E slot with no pending blueprint to fill it.
    LOCKDOWN_API bool WantsNpcOffer(const FLDOpponentSelf& Self, const FLDDemoContentSnapshot& Catalog, uint8 RestIndex);
    // #198 B component picks (rule-based, no tunable numbers). Whether the AI could enable Component apart from the build
    // gate: a base component and, for an ActiveElement binding (B18), the first learned active in Q/W/E order that its
    // BindActives lists, bound with the first element its BindElements lists (Out* stay empty / None without a binding).
    LOCKDOWN_API bool ComponentUsable(const FLDOpponentSelf& Self, const FLDDemoComponentDefinition& Component,
        FString& OutBoundActive, ELDDemoElement& OutElement);
    // The opening base pick: the first option in catalog order that ComponentUsable accepts; false when none does.
    LOCKDOWN_API bool SelectOpeningBase(const FLDOpponentSelf& Self, const FLDDemoContentSnapshot& Catalog, FString& OutComponent,
        FString& OutBoundActive, ELDDemoElement& OutElement, FString& Error);
    // #198 F: where the AI installs an owned mod or connector (rule-based, no tunable numbers): the first legal target in
    // catalog list order: a learned active of TargetActives, else an installed host of TargetComponents (C01 on B18 only
    // while B18 burns), else the innate D (bTargetDash), else the holder itself (bTargetSelf); C12 also repeats the first
    // installed base of ExtraBases. Skips what growth refuses: M08/M09 on a target the other already holds (TargetConflict).
    // False for a base
    // component or without any legal target.
    LOCKDOWN_API bool WorkshopTarget(const FLDOpponentSelf& Self, const FLDDemoContentSnapshot& Catalog,
        const FLDDemoComponentDefinition& Component, FLDOpponentComponentTarget& Out);
    // The owned component to enable next: the first never-installed, not installed one in catalog order that
    // ComponentUsable accepts (a base) or WorkshopTarget places (a mod or connector; the decision carries its target);
    // false when none does.
    LOCKDOWN_API bool SelectComponentInstall(const FLDOpponentSelf& Self, const FLDDemoContentSnapshot& Catalog,
        FString& OutComponent, FString& OutBoundActive, ELDDemoElement& OutElement, FString& Error);
    // #198 G: how the AI resolves one pending component choice (rule-based, no tunable numbers): the first unowned option
    // it could enable now (ComponentUsable for a base, WorkshopTarget for a mod or connector), else the first unowned option;
    // OutOption empty (the empty-pool points) once every option is owned. False with Error for an option outside the catalog.
    LOCKDOWN_API bool SelectComponentChoice(const FLDOpponentSelf& Self, const FLDDemoContentSnapshot& Catalog,
        const FLDOpponentComponentChoice& Choice, FString& OutOption, FString& Error);
    // #198 rest move: the first installed component in catalog order that sits away from its preferred placement, judged on
    // the build its uninstall leaves (it and everything installed on it, transitively, back in the inventory): a B18 binding
    // other than ComponentUsable's, or a mod or connector target other than WorkshopTarget's. That build is exactly what the
    // reinstall reads, so the component goes back to the same placement and is not moved again. A component without any
    // preferred legal placement stays. False when none is misplaced.
    LOCKDOWN_API bool SelectComponentMove(const FLDOpponentSelf& Self, const FLDDemoContentSnapshot& Catalog, FString& OutComponent,
        FString& Error);
    // #198 G rest dismantle (rule-based, no tunable numbers): the first GROWTH.DismantleInputCount owned common components, in
    // catalog order, that the AI cannot use on its committed build (it reads no future offer): never installed (an installed
    // or once-installed one is never given up), without a ComponentUsable/WorkshopTarget placement, and not a gap the build
    // allows to fill. Only when it holds that many, the count is above one (each dismantle then leaves strictly fewer owned
    // components, so dismantles end) and an unowned common component is left to receive (DismantlePoolEmpty otherwise).
    LOCKDOWN_API bool SelectDismantle(const FLDOpponentSelf& Self, const FLDDemoContentSnapshot& Catalog, TArray<FString>& Out,
        FString& Error);
    // True when the public solo deadline leaves no more than travel, control and reaction budget.
    // An unknown path or zero speed is conservatively urgent; it never divides by zero.
    LOCKDOWN_API bool IsCaptureDefenseUrgent(const FLDCaptureUrgencyInput& Input);
    // True when the own foot is inside the public capture circle of the frame's static capture POI.
    LOCKDOWN_API bool InsideCircle(const FLDOpponentDecisionFrame& Frame, const FLDDemoContentSnapshot& Catalog);
    // ---- #198 D rest PK (OPPONENT_AI.md §6.6; issue #198 comment 6752601175 §2-§6): deterministic, public reads only ----
    // The PK sign-up window of a build step is open at Public.NowMs (from SignupOpensAtMs until before StepEndsAtMs) and
    // Self does not stand in the public sign-up yet. Every AI signs up, walking into its own sign-up disk; there is no
    // chance.
    LOCKDOWN_API bool WantsPKSignUp(const FLDOpponentPublicMatch& Public, const FGuid& Self);
    // Self's public bet side in this PK; 0 without one.
    LOCKDOWN_API uint8 PKOwnBetSide(const FLDPKView& PK, const FGuid& Self);
    // The side to bet on in the betting step: the lowest-seat entrant's side, for an eligible bettor without a public bet; 0
    // otherwise.
    LOCKDOWN_API uint8 PKBetSide(const FLDOpponentPublicMatch& Public, const FGuid& Self);
    // The visible contender unit of the lowest seat among the living entrants whose side is not BetSide and whose foot lies
    // within ThrowRangeCm (XY) of the public throw anchor; nullptr when none of them is in this observation frame.
    LOCKDOWN_API const FLDOpponentUnit* PKThrowTarget(const FLDPKView& PK, uint8 BetSide, const TArray<FLDOpponentUnit>& Units,
        double ThrowRangeCm);
    // Other is a locked entrant on Self's side (only the team format shares a side); never Self itself.
    LOCKDOWN_API bool IsPKTeammate(const FLDPKView& PK, const FGuid& Self, const FGuid& Other);
    // An entrant at FootCm must walk back toward the arena centre at NowMs: outside the current safe radius, or outside the
    // previewed next radius while one is shown (LDPKRules on the approved ring parameters). False before the fight starts.
    LOCKDOWN_API bool PKRingRetreat(const FLDPKParameters& Params, const FLDPKView& PK, const FVector& FootCm, int64 NowMs);
    // A visible shared training dummy by the HUD's roster rule: lowest handle first; nullptr
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
    // #198: the build step after any offer choice, exactly as Decide makes it (OPPONENT_AI.md §6.4): the opening base pick
    // while it is open (when an active may be chosen), the oldest pending component choice (#198 G, under the same gate), the
    // preferred blueprint into the lowest empty slot when learning is allowed (a rest build window, or alive inside the build
    // safe window); in a rest build window then the replacement (SelectReplacement), the once-installed inventory components
    // back on the build and one misplaced component off it (SelectComponentMove); the next owned component to enable for the
    // first time under the learn gate; in a rest build window the first gap fill the unspent points pay for (#198 G), a
    // dismantle (SelectDismantle) and a respec (WantsRespec); then every unspent point when allocating is allowed (a rest
    // build window, dead, or alive inside the window). It leaves offers to its caller: it never chooses one and never buys the
    // NPC's (Decide does, before the points). False when nothing applies, a build request is in flight or failure memory
    // holds the step back.
    LOCKDOWN_API bool DecideFollowUpBuild(const FLDOpponentDecisionFrame& Frame, const FLDDemoContentSnapshot& Catalog,
        FLDOpponentPolicyState& State, FLDOpponentBuildIntent& Out);
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
#endif
}
