#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Development/LDDevGrowthFixture.h"
#include "Development/LDDevTestProtocol.h"
#include "Development/LDGrowthRequests.h"
#include "Development/LDGrowthProtocol.h"
#include "Development/LDProgressionRules.h"
#include "LDGrowthDirector.generated.h"

class ALDGameMode;
class FLDDemoContentSnapshot;
class FLDGameplayWritePermit;
struct FActorSpawnParameters;
struct FLDCommittedDeath;

// Growth domain hub (N03 rewards, N04): the death-reward and map-event-reward sinks, the Transactions dispatch and the
// owner publication. Every XP grant is one composite commit with its levels, opportunities and Combat projection.
UCLASS()
class LOCKDOWN_API ALDGrowthDirector : public AActor
{
    GENERATED_BODY()

public:
    ALDGrowthDirector();

    // Core authority initialization: spawns Growth and Development onto Mode. A failure leaves no growth director behind.
    static bool SpawnDomain(ALDGameMode& Mode, const FActorSpawnParameters& Spawn, FString& Error);
    static void RetireDomain(ALDGameMode& Mode);
    // Starting: one growth record per locked roster contender (humans and AI slots).
    bool BeginMatch(ALDGameMode& Mode, const FLDMatchContext& InMatch, const TArray<FGuid>& InRoster, FString& Error);

    // DeathsAndRewards: called once per committed death after its Effects set locked every RewardBasis.
    FLDRewardReceipt CommitDeathReward(const FLDCommittedDeath& Death, const FLDGameplayWritePermit& Permit);
    // D26 (#79) / #198 G: one map event payout through the same reward composite as a death reward: XP (>= 0) and the build
    // part Kind (ELDFieldRewardKind; None pays XP only, which must then be positive), prepared together and installed once.
    // Event + Serial is its business identity: the match pays each event once, so any second claim is refused
    // (FieldRewardDuplicate). The build part draws from private per-event random material. Needs the Transactions or
    // DeathsAndRewards permit, where growth applies.
    ELDRewardOutcome CommitFieldReward(const FGuid& Recipient, FName Event, uint64 Serial, int64 XP, ELDFieldRewardKind Kind,
        const FLDGameplayWritePermit& Permit, FLDRewardReceipt& Receipt, ELDGrowthReason& Reason);
    // Transactions: build commands (and DEV test commands) in server arrival order. Single rejections are receipts; false only
    // on a fault.
    bool ProcessTransactions(const FLDGameplayWritePermit& Permit, FString& Error);
    // Publish (after Close, no permit exists): refreshes changed owner projections through SyncOwner and delivers
    // final receipts to owning connections. Writes no gameplay state; false only for a boundary of another match.
    bool PublishGrowth(const FLDAuthorityBoundary& Boundary, FString& Error);

    // Owner projection read by ALDGameMode::SyncOwner; one consistent view of level, XP, points, actives, blueprints and offer.
    FLDGrowthOwnerView ReadOwnerView(const FGuid& ContenderId) const;
    // Committed level for the frozen result rows; contenders that left keep their record until the match ends.
    bool ReadFinalLevel(const FGuid& ContenderId, uint8& Level, FString& Error) const;
#if WITH_DEV_AUTOMATION_TESTS
    // DEV growth fixture (LDDevGrowthFixture.h): queues an XP grant that the next Transactions phase commits through the
    // reward composite without a death fact. Returns a ticket for DevGrantStatus, or 0 with Error when the fixture,
    // match, contender or amount refuse it.
    uint64 DevGrantReward(const FGuid& ContenderId, int64 XP, FString& Error);
    // Only the armed evaluation's AI slots may replace their earned level-up draws; installation still uses build commands.
    uint64 DevGrantEvaluationReward(const FGuid& ContenderId, int64 XP, const TArray<FName>& LevelComponents, FString& Error);
    ELDDevGrantStatus DevGrantStatus(uint64 Ticket, FString& Detail) const;
    // DEV test command (LDDevTestProtocol.h) of ContenderId's own owning connection: refused unless the fixture admits
    // this authority, then admitted into that contender's DEV request stream. The pending command commits in the
    // Transactions phase in server arrival order (CommitPendingDevTest).
    // bHostConnection: the command arrived through the Listen Host's own connection (match-wide ops need it).
    FLDDevTestResult QueueDevTest(const FGuid& ContenderId, const FLDDevTestCommand& Command, bool bHostConnection);
#endif

    // ---- Module internals ----
    const FLDMatchContext& GetMatch() const { return Match; }
    const FLDDemoContentSnapshot* GetContent() const { return Content.Get(); }
    uint64 NextArrival() { return ++ArrivalCounter; }
    void MarkDirty(const FGuid& ContenderId) { Dirty.Add(ContenderId); }
    // SHA-256 of a canonical byte record.
    static void Terms(const TArray<uint8>& Canonical, uint8 (&Out)[32]);

    // DEV telemetry reads the bounded window of recent reward receipts; nothing else depends on it.
    const TArray<FLDRewardReceipt>& DevRecentRewards() const { return RecentRewards; }

private:
    friend class FLDDemoDevTestCommandsTest;
    friend class FLDDemoAssistSettlementTest;
    friend class FLDDemoGrowthBuildCommandsTest;
    void RecordReward(const FLDRewardReceipt& Receipt);
    // One recipient's part of a composite reward; #198 G: a map event's share may carry its build part.
    struct FRewardShare
    {
        FGuid Recipient;
        LDProgression::FRewardAmounts Amounts;
        ELDFieldRewardKind FieldKind = ELDFieldRewardKind::None;
        FName Event;
        uint64 Serial = 0;
#if WITH_DEV_AUTOMATION_TESTS
        TArray<FName> DevLevelComponents;
#endif
    };
    // The reward composite (PROGRESSION_AND_REWARDS): the XP of every share with its levels, opportunities and Combat
    // projection, all prepared before any is installed, so the whole reward commits once or not at all (a contender kill
    // with its assisters, a camp kill, a map event or a DEV grant). Shares[0] fills the receipt's own fields, the rest fill
    // Receipt.Assists in order. Every installed part writes its LD_REWARD line (and an LD_LEVEL_UP line when it raised
    // the level); Cause and LogDetail name what paid it.
    ELDRewardOutcome CommitRewardShares(const TArray<FRewardShare>& Shares, const TCHAR* Cause,
        const FLDGameplayWritePermit& Permit, FLDRewardReceipt& Receipt, ELDGrowthReason& Reason,
        const FString& LogDetail = FString());
    ELDRewardOutcome CommitRewardParts(const FGuid& Recipient, int64 XP, const TCHAR* Cause,
        const FLDGameplayWritePermit& Permit, FLDRewardReceipt& Receipt, ELDGrowthReason& Reason,
        const FString& LogDetail = FString());
    ALDGameMode* Mode() const;
#if WITH_DEV_AUTOMATION_TESTS
    // Commits every pending DEV grant in ticket order; false only on an authority fault.
    bool CommitDevGrants(const FLDGameplayWritePermit& Permit, FString& Error);

    struct FDevGrant
    {
        uint64 Ticket = 0;
        FGuid Contender;
        int64 XP = 0;
        TArray<FName> LevelComponents;
        ELDDevGrantStatus Status = ELDDevGrantStatus::Pending;
        FString Detail;
    };
    // Every DEV grant of this match in ticket order (ticket = index + 1).
    TArray<FDevGrant> DevGrants;

    // QueueDevTest after the fixture admitted this authority: shape, match, contender and the request stream.
    FLDDevTestResult AdmitDevTest(const FGuid& ContenderId, const FLDDevTestCommand& Command, bool bHostConnection = false);
    // Commits or rejects this contender's pending DEV test command; false only on an authority fault. A retryable
    // reward refusal leaves the command pending for the next Transactions phase.
    bool CommitPendingDevTest(const FGuid& ContenderId, const FLDGameplayWritePermit& Permit, FString& Error);
    // ResetToMatchStart as one composite: Growth's DevPrepareReset and Combat's DevPrepareReset prepared against each
    // other, then both installed. Nothing changes on a refusal; AuthorityFault when the install lost its domain.
    ELDGrowthReason CommitDevReset(const FGuid& ContenderId, const FLDGameplayWritePermit& Permit, FString& Detail);
    struct FDevTestRecord
    {
        TLDGrowthRequestStream<FLDDevTestCommand, FLDDevTestReceipt> Stream;
        TArray<FLDDevTestReceipt> Unpublished;
    };
    // DEV test request streams of this match, per contender; a new match spawns a new director and starts empty.
    TMap<FGuid, FDevTestRecord> DevTests;
#endif

    FLDMatchContext Match;
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    TArray<FGuid> Roster;
    uint64 ArrivalCounter = 0;
    TSet<FGuid> Dirty;
    TMap<FGuid, uint64> PublishRevisions;
    // Highest resolved victim life per (kind, entity) and a bounded replay window of reward receipts.
    TMap<TPair<uint8, FGuid>, uint32> ResolvedLifeThrough;
    TArray<FLDRewardReceipt> RecentRewards;
    // Every map event (Event, Serial) this match has paid; a second claim of the same event is refused.
    TSet<TPair<FName, uint64>> PaidEvents;
};
