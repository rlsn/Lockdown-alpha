#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "World/LDCampDecision.h"

struct FLDDemoWorldGeometry;
struct FLDObservedPublicFacts;

struct FLDCampMemberSpec
{
    FName MemberId;
    FVector HomeCm = FVector::ZeroVector;
    // Spec §11.2 teambuild: the reserved extra member of a High, Elite or Ancient camp, home at its anchor. It spawns only
    // with a spawn that carries the teambuild affix, as the spawn's selected species for High camps and as a High-tier
    // species otherwise; without it the slot stays empty and never holds back a clear.
    bool bTeambuild = false;
    FName DefinitionId;
    // Index into FLDCampSpec::Pools: the tier pool this member's species is drawn from on each whole-group spawn.
    int32 Pool = 0;
};

// The species of one camp tier present in a camp; every whole-group spawn draws one of them for all its members.
struct FLDCampSpeciesPool
{
    ELDDemoCampTier Tier = ELDDemoCampTier::Low;
    TArray<FName> Species;
    TArray<uint8> SpeciesIds;
};

// Read-only projection of the validated content and world roots, not a second tunable table.
struct LOCKDOWN_API FLDCampSpec
{
    FName CampId;
    FName SurfaceId;
    // The fixed tier representative of the camp's own prototype; public records show it, never the drawn species.
    FName MonsterId;
    uint8 CreatureId = 0;
    // Pools[0] is the camp's own tier; an authored member of another tier adds that tier's pool once.
    TArray<FLDCampSpeciesPool> Pools;
    ELDDemoCampTier Tier = ELDDemoCampTier::Low;
    FVector AnchorCm = FVector::ZeroVector;
    TArray<FLDCampMemberSpec> Members;
    // The fixed High-tier species the teambuild member of an Elite or Ancient camp wears, with its native creature id.
    // None and 0 for a High camp (its teambuild member is of each spawn's selected species) and for a Low camp (no slot).
    FName TeambuildMonsterId;
    uint8 TeambuildCreatureId = 0;
    // The registered scene instance and its approved AI/stat definition are separate identities.
    FName DefinitionId;
    // Authored member slots retain their individual tiers and count across refreshes; species are still drawn per tier.
    bool bAuthoredMembers = false;

    // The species a slot wears for one draw (one species per pool): the fixed teambuild species when the camp has one,
    // otherwise the draw of the slot's tier pool. None when the draw does not cover the slot's pool.
    FName MemberSpecies(int32 Slot, TConstArrayView<FName> Draw) const;
};

struct LOCKDOWN_API FLDCampRoster
{
    TArray<FLDCampSpec> Camps;

    int32 FindCamp(FName CampId) const;
};

// SC-07 public camp record for UI/AI: fixed location, tier and definition only. It never carries
// living members, HP, aggro, Returning or refresh timing.
struct FLDCampPublicView
{
    FName CampId;
    FName MonsterId;
    ELDDemoCampTier Tier = ELDDemoCampTier::Low;
    FVector2D AnchorCm = FVector2D::ZeroVector;
};

struct FLDCampMemberState
{
    FGuid EntityId;
    uint32 Life = 0;
    bool bAlive = false;
    FLDCombatBodyRef Body;
    FLDCampBrain Brain;
    uint64 ReturnEpoch = 0;
    uint64 ResetThroughEpoch = 0;
    // Hostile contacts at or before this authority time predate the current spawn, reset or rest return.
    double AggroFloorSeconds = 0;
    TArray<FLDCombatBodyRef> HostileSources;
    uint64 LastDirectSequence = 0;
};

struct FLDCampGroupState
{
    // One species per FLDCampSpec::Pools entry. Pending is the draw prepared for the next whole-group spawn (empty
    // until drawn, kept across blocked retries); Selected and CreatureIds are the committed lives' draw.
    TArray<FName> PendingSpecies;
    TArray<FName> SelectedSpecies;
    TArray<uint8> CreatureIds;
    uint32 Generation = 0;
    bool bRespawnPending = false;
    int64 ClearedAtMs = 0;
    int64 RespawnDueMs = 0;
    TArray<FLDCampMemberState> Members;
};

struct FLDCampContact
{
    FLDCombatBodyRef Victim;
    FLDCombatBodyRef Source;
    double OccurredAtSeconds = 0;
};

enum class ELDCampContactResult : uint8
{
    Retaliating, Unchanged, NotCampMember, StaleVictim, StaleEvent, NoContenderSource, Rejected
};

enum class ELDCampDeathResult : uint8 { MemberDead, CampCleared, Duplicate, Rejected };

struct FLDCampResetKey
{
    FGuid LedgerMatch;
    FLDVictimKey Victim;
    uint64 ReturnEpoch = 0;

    bool IsValid() const { return LedgerMatch.IsValid() && Victim.IsValid() && ReturnEpoch != 0; }
    bool operator==(const FLDCampResetKey& Other) const
    { return LedgerMatch == Other.LedgerMatch && Victim == Other.Victim && ReturnEpoch == Other.ReturnEpoch; }
};

// Per-match camp bookkeeping. Entity ids are allocated once per match for each fixed (camp, slot);
// Life mirrors the core victim life and only advances through a committed whole-group spawn.
class LOCKDOWN_API FLDCampMatchState
{
public:
    static constexpr int32 MaxHostileSources = LDMatchLifecycle::MaxContenders;

    bool Begin(const FLDMatchContext& InMatch, const TSharedRef<const FLDCampRoster>& InRoster, FString& Error);
    void End();
    bool IsActive() const { return Match.IsValid() && Roster.IsValid(); }
    const FLDMatchContext& GetMatch() const { return Match; }
    const FLDCampRoster* GetRoster() const { return Roster.Get(); }
    int32 NumCamps() const { return Camps.Num(); }
    const FLDCampGroupState* FindCamp(int32 Camp) const { return Camps.IsValidIndex(Camp) ? &Camps[Camp] : nullptr; }
    const FLDCampMemberState* FindMember(const FGuid& EntityId, int32* OutCamp = nullptr, int32* OutSlot = nullptr) const;
    // Living member whose current core life and physical body match exactly.
    const FLDCampMemberState* FindLiving(const FLDCombatBodyRef& Body, int32* OutCamp = nullptr, int32* OutSlot = nullptr) const;
    TArray<FLDCampPublicView> BuildPublicViews() const;

    bool CanSpawnInitial(int32 Camp, FString& Error) const;
    // Choices holds one index per pool of the camp. The first call for a whole-group new life fixes the draw; a retry
    // after a blocked placement returns the same draw whatever it passes.
    bool SelectSpecies(int32 Camp, TConstArrayView<int32> Choices, TArray<FName>& Out, FString& Error);
    bool IsRespawnDue(int32 Camp, int64 NowMs) const;
    // Called after the authority has retired surviving bodies, including partially cleared camps.
    void QueueFieldReset(int32 Camp);
    // Earliest pending whole-group refresh deadline in authority ms, for the driver's exact step boundaries;
    // MAX_int64 when no camp is waiting. A deadline that already passed stays reported until its refresh commits.
    int64 NextRespawnDueMs() const;
#if WITH_DEV_AUTOMATION_TESTS
    // DEV test session: every cleared camp still waiting for its refresh becomes due at NowMs; the next due refresh then
    // runs the ordinary whole-group spawn. Living camps are untouched. Returns the number of waiting camps.
    int32 DevRefreshNow(int64 NowMs);
    // Build evaluation: a camp waiting for its refresh stays empty until the next field segment's reset re-queues it.
    void DevHoldEmpty(int32 Camp);
#endif
    // Bodies are the core's committed refs for every slot, in slot order.
    bool CommitSpawn(int32 Camp, TArrayView<const FLDCombatBodyRef> Bodies, double AtSeconds, FString& Error);

    // OccurredAtMs is the committed death's authority millisecond; the clear time and the refresh deadline are
    // exact integers from it, never re-derived from seconds.
    ELDCampDeathResult RecordDeath(const FLDDeathKey& Death, int64 OccurredAtMs, FString& Error);
    ELDCampContactResult ApplyContact(const FLDCampContact& Contact, FString& Error);

    // Installs the evaluated brain; a new Returning cycle advances the member's ReturnEpoch.
    bool ApplyDecision(const FLDCombatBodyRef& Member, const FLDCampDecision& Decision, bool& bBeganReturn, FString& Error);
    bool ReadResetKey(const FLDCombatBodyRef& Member, FLDCampResetKey& Out, FString& Error) const;
    bool CompleteReset(const FLDCampResetKey& Key, double AtSeconds, FString& Error);
    bool NextDirectSequence(const FLDCombatBodyRef& Member, uint64& Out, FString& Error);
    // #198 rest return: clear lost-target timing and hostile sources, rejecting contacts at or before NowSeconds.
    // Preserve the behavior phase, life and return/reset epochs; HP and refresh deadlines are untouched.
    void ForgetTargetsForRest(double NowSeconds);

    // Camp-owned public facts for a currently registered living member: creature identity and the
    // camp_returning cue (one stack, untimed). Existing combat facts are kept; conflicts are rejected.
    bool AppendObservedFacts(const FLDCombatBodyRef& Body, FLDObservedPublicFacts& InOut, FString& Error) const;

private:
    FLDCampMemberState* MutableLiving(const FLDCombatBodyRef& Body, FString& Error);

    FLDMatchContext Match;
    TSharedPtr<const FLDCampRoster> Roster;
    TArray<FLDCampGroupState> Camps;
    TMap<FGuid, FIntPoint> MemberIndex;
};

namespace LDCamp
{
    LOCKDOWN_API bool BuildRoster(const FLDDemoContentDefinitions& Content, const FLDDemoWorldGeometry& World,
        FLDCampRoster& Out, FString& Error);
}
