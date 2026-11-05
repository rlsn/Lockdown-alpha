#pragma once

#include "CoreMinimal.h"
#include "Combat/LDAttributeProjection.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Framework/LDMatchLifecycleTypes.h"

class FLDDemoContentSnapshot;

// Pure opponent strategy over one already-published, authorized read copy (docs/demo/OPPONENT_AI.md §6).
// It never reads a world, actor, ledger, clock or hidden state: the server adapter fills the frame from the
// same owner, observation and public projections a human player receives, then submits each intent to its
// owning domain, which revalidates everything at its own commit point.

enum class ELDOpponentMode : uint8
{
    Stopped, DeadWaiting, Disengage, DefendOrHold, Choose, Engage, Shop, Contest, Farm, Explore,
    // D26: going for a 天降外卖; not a contest of the circle, so a stop-loss exit from it never counts against the capture.
    Airdrop
};

// Bounded diagnostic cause of a decision; never a coordinate, wallet or private source.
enum class ELDOpponentReason : uint8
{
    None, Stopped, ProviderUnavailable, DeadWaiting, Recover, PublicCaptureUrgent, HoldCapture, ChooseOffer,
    EngageTarget, ShopImprovement, NoAffordableImprovement, ContestCapture, FarmCamp, Explore, NoLegalTarget,
    AwaitReceipt, TargetSuppressed, ContestAirdrop, HuntLeader
};

enum class ELDOpponentSkillSlot : uint8 { Q, W, Passive };
enum class ELDOpponentSourceSlot : uint8 { Q, W, Gear0, Gear1, Dash };

struct FLDOpponentSkill
{
    FString DefinitionId;
    uint8 Rank = 0;
    ELDOpponentSkillSlot Slot = ELDOpponentSkillSlot::Passive;
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

enum class ELDOpponentOptionKind : uint8 { LearnActive, UpgradeActive, LearnPassive, UpgradePassive, Attribute };

struct FLDOpponentOption
{
    FGuid OptionId;
    ELDOpponentOptionKind Kind = ELDOpponentOptionKind::Attribute;
    FString DefinitionId;
    uint8 FromRank = 0;
    uint8 ToRank = 0;
};

// The head offer of the persistent queue; its cards never change until it is consumed.
struct FLDOpponentOffer
{
    FGuid OfferId;
    uint64 OfferRevision = 0;
    TArray<FLDOpponentOption> Options;
};

// The AI's own authorized projection: owner life, progression, equipment and source readiness.
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
    int64 Gold = 0;
    uint64 BuildRevision = 0;
    uint8 Level = 1;
    // Persistent choices already consumed, including the opening pick; equipment is read separately.
    uint8 ChoicesConsumed = 0;
    TArray<FLDOpponentSkill> Skills;
    // Equipment slots 0/1 by catalog definition id; an empty string is an empty slot.
    FString Equipment[2];
    TArray<FLDOpponentSource> Sources;
    TOptional<FLDOpponentOffer> Offer;
};

enum class ELDOpponentCircle : uint8 { Empty, Solo, Contested };

// Public match state shared with every participant; an occupier never carries a position.
struct FLDOpponentPublicMatch
{
    ELDMatchPhase Phase = ELDMatchPhase::Faulted;
    int64 NowMs = 0;
    ELDOpponentCircle Circle = ELDOpponentCircle::Empty;
    // The contender holding capture progress: the solo occupier, or under D12 the holder a contest only paused.
    // Invalid when nobody holds progress.
    FGuid Occupier;
    // Real expiry of the holder's remaining progress while Solo; zero while Contested.
    int64 DueAtMs = 0;
    // D26: the public point of the current airdrop while it is announced or on the ground; None otherwise.
    FName AirdropPOI;
    bool bAirdropLanded = false;
    // D27: the public 上热搜 notice; Leader invalid when nobody is named.
    FGuid Leader;
    FVector LeaderCircleCentreCm = FVector::ZeroVector;
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

enum class ELDOpponentPOIKind : uint8 { Spawn, Camp, Shop, Capture, Dorm, Airdrop };

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
};

enum class ELDOpponentOrderKind : uint8 { None, MoveGround, AttackUnit, ApproachShop, Action };
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
    // A choice or shop request awaits its final receipt; no second build change is proposed meanwhile.
    bool bBuildChangeInFlight = false;
    // The shop domain reported Opened for this life with a quote matching the public catalog.
    bool bShopOpen = false;
};

enum class ELDOpponentFailure : uint8 { Path, Engagement, Activation, Shop, Choice };

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

enum class ELDOpponentBodyKind : uint8 { None, Keep, Stop, MoveGround, AttackUnit, ApproachShop, Activate };

struct FLDOpponentBodyIntent
{
    ELDOpponentBodyKind Kind = ELDOpponentBodyKind::None;
    FName POI;
    FGuid Handle;
    uint32 Epoch = 0;
    ELDOpponentSourceSlot Source = ELDOpponentSourceSlot::Q;
    ELDDemoTargetMode TargetMode = ELDDemoTargetMode::Self;
    FVector Direction = FVector::ZeroVector;
    // Failure memory of MoveGround, ApproachShop, AttackUnit and Activate; the final receipt settles against it.
    FLDOpponentRetryKey Key;
};

enum class ELDOpponentBuildKind : uint8 { None, Choose, Sell, Buy };

struct FLDOpponentChoicePick
{
    FGuid OptionId;
    bool bReplace = false;
    ELDOpponentSkillSlot ReplaceSlot = ELDOpponentSkillSlot::Q;
    FString ReplacedSkillId;
    uint8 ReplacedRank = 0;
};

// Non-binding plan from public prices; the shop's real quote and current state are rechecked to trade.
struct FLDOpponentEquipmentPlan
{
    int32 SellSlot = INDEX_NONE;
    FString BuyItemId;
};

struct FLDOpponentBuildIntent
{
    ELDOpponentBuildKind Kind = ELDOpponentBuildKind::None;
    FGuid OfferId;
    uint64 OfferRevision = 0;
    FLDOpponentChoicePick Choice;
    int32 SellSlot = INDEX_NONE;
    FString BuyItemId;
    FLDOpponentRetryKey Key;
};

struct FLDOpponentDecision
{
    ELDOpponentMode Mode = ELDOpponentMode::Stopped;
    ELDOpponentReason Reason = ELDOpponentReason::None;
    FLDOpponentBodyIntent Body;
    FLDOpponentBuildIntent Build;
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
    static constexpr int32 Capacity = 32;
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

// Strategy-only memory: never a second body, wallet, cooldown or camp table.
struct FLDOpponentPolicyState
{
    uint32 Generation = 0;
    uint32 Life = 0;
    ELDOpponentMode Mode = ELDOpponentMode::Stopped;
    bool bRecovering = false;
    // The public place this recovery escapes toward; kept while it still leads away from visible threats.
    FName RetreatPOI;
    FName VisitPOI;
    int64 VisitEngagedAtMs = 0;
    FName ExplorePOI;
    FGuid TargetHandle;
    uint32 TargetEpoch = 0;
    int64 TargetEngagedAtMs = 0;
    TArray<FLDOpponentVisit> Visits;
    FLDOpponentRetryLedger Failures;
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
    bool bBasicGrowth = false;
    bool bSurvival = false;
};

// Lexicographic structural preference of one equipment pair (§6.4); not a damage weight.
struct FLDEquipmentPairRank
{
    int32 Filled = 0;
    int32 BasicGrowth = 0;
    int32 Survival = 0;
    int32 ActiveSources = 0;
    int32 DistinctPassives = 0;
    int32 Compare(const FLDEquipmentPairRank& Other) const;
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
    LOCKDOWN_API FLDOpponentUsage EquipmentUsage(const FLDDemoEquipmentDefinition& Item);
    LOCKDOWN_API bool SelectChoice(const FLDOpponentSelf& Self, const FLDDemoContentSnapshot& Catalog,
        FLDOpponentChoicePick& Out, FString& Error);
    LOCKDOWN_API bool RankEquipmentPair(const FString& First, const FString& Second,
        const FLDDemoContentSnapshot& Catalog, FLDEquipmentPairRank& Out, FString& Error);
    LOCKDOWN_API bool PlanEquipment(const FLDOpponentSelf& Self, const FLDDemoContentSnapshot& Catalog,
        TOptional<FLDOpponentEquipmentPlan>& Out, FString& Error);
    // True when the public solo deadline leaves no more than travel, control and reaction budget.
    // An unknown path or zero speed is conservatively urgent; it never divides by zero.
    LOCKDOWN_API bool IsCaptureDefenseUrgent(const FLDCaptureUrgencyInput& Input);
    // True when the own foot is inside the public capture circle of the frame's static capture POI.
    LOCKDOWN_API bool InsideCircle(const FLDOpponentDecisionFrame& Frame, const FLDDemoContentSnapshot& Catalog);
    LOCKDOWN_API uint64 Fingerprint(ELDOpponentFailure Category, const FLDOpponentSelf& Self);
    LOCKDOWN_API FLDOpponentDecision Decide(const FLDOpponentDecisionFrame& Frame,
        const FLDDemoContentSnapshot& Catalog, FLDOpponentPolicyState& State);
    // Consumes a domain's final result for an intent; success clears, failure backs off or suppresses, and a
    // lost unit target is suppressed for this visibility interval and released as the current target.
    LOCKDOWN_API void RecordOutcome(FLDOpponentPolicyState& State, const FLDOpponentRetryKey& Key,
        ELDOpponentOutcome Outcome, const FLDOpponentSelf& Self, int64 NowMs, const FLDDemoAIParameters& AI);
}
