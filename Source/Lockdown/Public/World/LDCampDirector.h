#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "World/LDCampDecision.h"
#include "World/LDCampLifecycle.h"
#include "World/LDBoss.h"
#include "World/LDWorldQueryTypes.h"
#include "LDCampDirector.generated.h"

class ALDCampMemberCharacter;
class ALDGameMode;
class FLDDemoContentSnapshot;
struct FLDDemoMonsterDefinition;
struct FLDCombatHostileContact;
struct FLDCommittedDeath;
struct FLDObservedPublicFacts;

// Server-only owner of the five Demo camps: whole-group spawn and refresh, retaliation brains,
// native-motion steering and home resets. It also owns the D26 (#79) 画饼老板 Boss, the one roaming monster body: it
// appears once per match, patrols the public waypoints and attacks through the same Combat entries as a camp member. The Camps match provider spawns it during Starting and
// destroys it on MatchReset; every write entry runs inside the Combat-driven phase named on it.
UCLASS(NotBlueprintable)
class LOCKDOWN_API ALDCampDirector : public AActor
{
    GENERATED_BODY()
public:
    ALDCampDirector();
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

    // Provider Prepare (Starting, no permit): approved roster, per-match member identities and the Boss schedule.
    bool BeginMatch(const FLDMatchContext& Match, int64 StartedAtMs, FString& Error);
    // RespawnsAndWorld (Starting initialization), after the contenders spawned.
    bool SpawnInitialCamps(const FLDGameplayWritePermit& Permit, FString& Error);
    // Motion: exactly one native integration for one registered living camp body.
    bool AdvanceMemberMotion(const FLDAuthorityStep& Step, const FLDCombatBodyRef& Body,
        const FLDGameplayWritePermit& Permit, FString& Error);
    // Actions: evaluate every living member and submit basic attacks through Combat.
    bool ProcessAuthorityIntents(const FLDGameplayWritePermit& Permit, FString& Error);
    // Effects: one committed hostile contact against a camp member. False only for a broken invariant.
    bool OnCombatHostileContact(const FLDCombatHostileContact& Fact, const FLDGameplayWritePermit& Permit, FString& Error);
    // DeathsAndRewards, after the core ended the life: bookkeeping only; rewards use the single growth sink.
    bool ConsumeMemberDeath(const FLDCommittedDeath& Fact, const FLDGameplayWritePermit& Permit, FString& Error);
    // RespawnsAndWorld: home resets, then due whole-group refreshes, then the Boss's appearance when due.
    bool AdvanceCamps(const FLDGameplayWritePermit& Permit, FString& Error);
    // Driver step planning: the earliest pending refresh or Boss appearance in authority ms, MAX_int64 when none.
    // AdvanceCamps refreshes a camp at the first boundary whose AuthorityNowMs reaches that deadline.
    int64 NextRefreshDueMs() const { return FMath::Min(State.NextRespawnDueMs(), Boss.NextDueMs()); }
    // D26: the public Boss view and state (read-only).
    FLDBossView BossView() const { return Boss.View(); }
    const FLDBossState& GetBoss() const { return Boss; }
    // Provider MatchReset: removes every camp body and forgets the match.
    void EndMatch();
#if WITH_DEV_AUTOMATION_TESTS
    // DEV test session (Transactions permit): FLDCampMatchState::DevRefreshNow at the permit boundary; the next
    // RespawnsAndWorld refreshes them through AdvanceCamps. OutWaiting is the number of cleared camps.
    bool DevRefreshCleared(const FLDGameplayWritePermit& Permit, int32& OutWaiting, FString& Error);
#endif

    // Vision publication of a currently registered camp member; see FLDCampMatchState::AppendObservedFacts.
    bool AppendObservedFacts(const FLDMatchContext& Match, const FLDCombatBodyRef& Body,
        FLDObservedPublicFacts& InOut, FString& Error) const;
    TArray<FLDCampPublicView> PublicViews() const { return State.BuildPublicViews(); }
    const FLDCampMatchState& GetState() const { return State; }

private:
    struct FMotion
    {
        TArray<FVector> Points;
        int32 Index = 0;
        FVector Goal = FVector::ZeroVector;
        bool bActive = false;
        double BestProgress = 0;
        double LastProgressSeconds = 0;
    };
    struct FMemberRead
    {
        ALDCampMemberCharacter* Body = nullptr;
        FVector FootCm = FVector::ZeroVector;
        bool bGrounded = false;
        FName SurfaceId;
    };

    ALDGameMode* AuthorityMode() const;
    bool CheckPermit(const FLDGameplayWritePermit& Permit, ELDCommitPhase Phase, FString& Error) const;
    bool ReadMember(const FLDCampSpec& Spec, const FLDCombatBodyRef& Ref, FMemberRead& Out, FString& Error) const;
    bool ReadBody(FName SurfaceId, const FLDCombatBodyRef& Ref, FMemberRead& Out, FString& Error) const;
    bool BuildCandidates(const FLDCampMemberState& Member, const FMemberRead& Self, double NowSeconds,
        TArray<FLDAuthorityTargetSample>& Out, FString& Error) const;
    bool PlanPath(const FMemberRead& Self, FName SurfaceId, const FVector& Goal, double NowSeconds,
        FMotion& Out, FString& Error) const;
    void StopMotion(const FGuid& EntityId);
    bool FindPlacement(const FLDCampSpec& Spec, int32 Slot, const TArray<FVector>& Reserved,
        FLDLegalGround& Out, bool& bBlocked, FString& Error) const;
    bool FindGround(const FVector& HomeCm, FName SurfaceId, const FString& Label, const TArray<FVector>& Reserved,
        FLDLegalGround& Out, bool& bBlocked, FString& Error) const;
    // A hidden, tick-disabled monster body resting on Ground; the caller reveals it once the core committed its life.
    bool CreateBody(const FLDLegalGround& Ground, const FLDDemoMonsterDefinition& Monster, const FString& Label,
        ALDCampMemberCharacter*& Out, FString& Error);
    bool SpawnGroup(int32 Camp, const FLDGameplayWritePermit& Permit, bool& bBlocked, FString& Error);
    // D26 Boss: appearance (RespawnsAndWorld), one decision (Actions) and the approval buff for the last hit
    // (DeathsAndRewards).
    bool SpawnBoss(const FLDGameplayWritePermit& Permit, bool& bBlocked, FString& Error);
    bool ProcessBoss(const FLDGameplayWritePermit& Permit, FString& Error);
    bool GrantApproval(const FGuid& Killer, const FLDGameplayWritePermit& Permit, FString& Error);
    bool TryReset(int32 Camp, int32 Slot, const FLDGameplayWritePermit& Permit, FString& Error);

    FLDCampMatchState State;
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    TMap<FGuid, TWeakObjectPtr<ALDCampMemberCharacter>> Bodies;
    TMap<FGuid, FMotion> Motion;
    TMap<FGuid, FLDCampReturnWatch> Returns;
    // #28: members whose current life was last reset in place, not at home (they hold their ground beyond the leash).
    TSet<FGuid> Stranded;
    TSet<int32> BlockedRefresh;
    FLDBossState Boss;
    bool bBossBlockedLogged = false;
    uint64 BossSequence = 0;
};
