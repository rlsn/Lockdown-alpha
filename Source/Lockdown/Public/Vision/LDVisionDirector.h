#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "Navigation/LDNavigationComponent.h"
#include "World/LDWorldQueryTypes.h"
#include "Vision/LDViewTypes.h"
#include "Region/LDMapGeometry.h"
#include "LDVisionDirector.generated.h"
class ALDPlayerController;
class ALDWorldViewRelay;
class ALDCharacter;
class ALDGameMode;
class FLDDemoContentSnapshot;
struct FLDDemoWorldGeometry;
struct FLDDemoRational;

enum class ELDSightResult : uint8 { Visible, OutOfRange, Blocked, InvalidPoint, StaleWorld };
enum class ELDObservationPurpose : uint8 { Inspect, NewAttackOrder, AttackMoveCandidate, AttackStart };
enum class ELDObservedResolve : uint8 { Resolved, TargetUnavailable, ObserverUnavailable, AuthorityFault };

namespace LDObservation
{
    // Keeps the schedule's phase across ordinary late frames; after a stall it skips the missed intervals instead
    // of replaying them, so a caller samples the current pose once.
    inline int64 NextSampleDue(int64 NowMs, int64 DueMs, int64 PeriodMs, bool bStarted)
    { return !bStarted || NowMs - DueMs > PeriodMs ? NowMs + PeriodMs : DueMs + PeriodMs; }

    // Authority-time sight to a target: only Visible resolves; OutOfRange/Blocked are ordinary unavailability.
    // InvalidPoint/StaleWorld break a body or world invariant and surface as a fault, never as a missing target.
    inline ELDObservedResolve ResolveFromSight(ELDSightResult Sight)
    {
        switch (Sight)
        {
        case ELDSightResult::Visible: return ELDObservedResolve::Resolved;
        case ELDSightResult::OutOfRange:
        case ELDSightResult::Blocked: return ELDObservedResolve::TargetUnavailable;
        default: return ELDObservedResolve::AuthorityFault;
        }
    }

    // Stealth (CONTENT_AND_BALANCE 15.4): a hidden body is observed only within ConcealBeyondCm (planar, inclusive) of
    // the observer; beyond it the result is ordinary OutOfRange, exactly like a body past the vision radius. Zero means
    // not hidden. Faults and other results pass through unchanged.
    inline ELDSightResult ConcealedSight(ELDSightResult Geometric, const FVector& ObserverFootCm, const FVector& TargetFootCm,
        double ConcealBeyondCm)
    {
        if (Geometric != ELDSightResult::Visible || !(ConcealBeyondCm > 0)) { return Geometric; }
        return FVector::DistSquared2D(ObserverFootCm, TargetFootCm) <= ConcealBeyondCm * ConcealBeyondCm
            ? ELDSightResult::Visible : ELDSightResult::OutOfRange;
    }
}

// Valid only inside the calling authority phase; never cached, serialized or shown.
struct FLDAuthorityTargetSample
{
    FLDCombatBodyRef Target;
    FVector FootCm = FVector::ZeroVector;
    FLDWorldRevision World;
    double SampledAtSeconds = 0;
};

// Server-only ObservedDeath fact with its core binding. Never serialized.
struct FLDObservedTerminationFact
{
    FLDObservedUnitTermination Wire;
    FLDMatchContext Match;
    FLDVictimKey Victim;
    double OccurredAtSeconds = 0;
};

// One registered body at one authority sample, read from the core body/combat owners.
struct FLDObservedTarget
{
    FLDCombatBodyRef Body;
    FVector FootCm = FVector::ZeroVector;
    double ConcealBeyondCm = 0;    // stealth reveal distance at this sample; 0 = not hidden (LDObservation::ConcealedSight)
    FLDObservedUnitView Presented; // authorized fields; Handle/Epoch/StateRevision are observer-local
};

// Static line of sight over the approved world geometry. The importer guarantees every sight blocker is a
// strictly convex polygon or an upright cylinder standing on one flat level, taller than the sight sample height.
// #97 tiers (LDDemoWorld::FTerrain): nothing on a higher tier than the observer is seen, terrain of a higher tier
// between them hides, and an obstacle hides only from observers at or below its level (a ramp counts as its lower
// level), so high ground sees low ground over its walls and trees.
class LOCKDOWN_API FLDSightGeometry
{
public:
    bool Build(const FLDDemoWorldGeometry& Geometry, const FLDWorldRevision& InWorld, const FLDDemoRational& InRadius, FString& Error);
    bool IsValid() const { return World.IsValid() && RadiusCm > 0; }
    const FLDWorldRevision& Revision() const { return World; }
    double Radius() const { return RadiusCm; }
    const FVector2D& MinCm() const { return Min; }
    const FVector2D& MaxCm() const { return Max; }
    bool InBounds(const FVector& FootCm) const;
    ELDSightResult Trace(const FVector& SourceFootCm, const FVector& TargetFootCm) const;
    FVector2D CellCentre(int32 Index) const;
    void Coverage(const FVector& SourceFootCm, TArray<uint8>& OutBits) const;
private:
    bool SegmentBlocked(const FVector2D& From, const FVector2D& To) const;
    struct FCircle
    {
        FVector2D Centre = FVector2D::ZeroVector;
        double Radius = 0;
        int32 Level = 0;
    };
    FLDWorldRevision World;
    FVector2D Min = FVector2D::ZeroVector;
    FVector2D Max = FVector2D::ZeroVector;
    double RadiusCm = 0;
    TArray<TArray<FVector2D>> Blockers;
    TArray<int32> BlockerLevels;
    TArray<FCircle> Circles;
    LDDemoWorld::FTerrain Terrain;
};

namespace LDObservation
{
    // Flights in sight of SourceFootCm under the same rule as units, nearest first, at most MaxProjectiles.
    // A flight beyond the registered ground is simply out of sight; a non-finite flight or a changed world is a fault.
    LOCKDOWN_API bool SelectVisibleProjectiles(const FLDSightGeometry& Sight, const FVector& SourceFootCm,
        const TArray<FLDObservedProjectileFact>& Facts, TArray<const FLDObservedProjectileFact*>& Out, FString& Error);
}

// Per-observer authorization bookkeeping. It owns opaque handles, epochs, revisions, ObservedDeath
// facts and the single current navigation pin; HP, life and body truth stay with their core owners.
class LOCKDOWN_API FLDObserverLedger
{
public:
    enum class ETerminate : uint8 { Terminated, NotBound, Failed };
    bool Begin(uint32 InMatchGeneration, FString& Error);
    bool Bind(ELDLifePhase InPhase, uint32 InLife, bool& bReset, FString& Error);
    bool AllocateRevision(uint32& Out, FString& Error);
    bool Sample(const TArray<const FLDObservedTarget*>& Visible, TArray<FGuid>& Removed,
        TArray<FLDObservedUnitView>& Units, FString& Error);
    // Visible flights: a fresh opaque handle per visibility interval, keyed by the server-only flight key.
    // Removed lists handles of flights that left sight or ended; output order is observer-local.
    bool SampleProjectiles(const TArray<const FLDObservedProjectileFact*>& Visible, TArray<FGuid>& Removed,
        TArray<FLDObservedProjectileView>& Out, FString& Error);
    ETerminate Terminate(const FLDCombatBodyRef& Victim, const FLDMatchContext& InMatch, double OccurredAtSeconds,
        int64 OccurredAtServerMs, FLDObservedTerminationFact& Out, FString& Error);
    bool BindOrder(const FLDMoveIdentity& Order, const FGuid& Handle, uint32 ObservationEpoch, FString& Error);
    bool ReleaseOrder(const FLDMoveIdentity& Order, FString& Error);
    bool FindBinding(const FGuid& Handle, uint32 ObservationEpoch, FLDCombatBodyRef& Out) const;
    // Server-only reverse lookup of the current interval of one body (e.g. a taunter this observer must chase).
    bool FindHandle(const FLDCombatBodyRef& Body, FGuid& OutHandle, uint32& OutEpoch) const;
    bool IsBound(const FLDCombatBodyRef& Body) const;
    const FLDObservedTerminationFact* FindTermination(const FGuid& Handle, uint32 ObservationEpoch) const;
    void RecentTerminations(TArray<FLDObservedUnitTermination>& Out) const;
    bool HasOrderPin() const { return Pin.IsSet(); }
    uint32 MatchGeneration() const { return Match; }
    uint32 Generation() const { return Gen; }
    uint32 Revision() const { return Rev; }
    uint32 ObserverLife() const { return Life; }
    ELDLifePhase Phase() const { return LifePhase; }
private:
    struct FBinding
    {
        FLDCombatBodyRef Body;
        FGuid Handle;
        uint32 Epoch = 0;
        uint32 StateRevision = 0;
        FLDObservedUnitView LastPresented;
    };
    struct FOrderPin
    {
        FLDMoveIdentity Order;
        FGuid Handle;
        uint32 Epoch = 0;
    };
    struct FProjectileBinding
    {
        FGuid Key;
        FGuid Handle;
    };
    bool OrderBound(const FLDObservedTerminationFact& Fact) const;
    bool Newer(const FLDMoveIdentity& Candidate, const FLDMoveIdentity& Current) const;
    TArray<FBinding> Bindings;
    TArray<FProjectileBinding> ProjectileBindings;
    TArray<FLDObservedTerminationFact> Terminations;
    TOptional<FOrderPin> Pin;
    uint32 Match = 0, Gen = 0, Rev = 0, Life = 0, NextEpoch = 0, NextSequence = 0;
    ELDLifePhase LifePhase = ELDLifePhase::Unspawned;
    bool bBegun = false;
};

UCLASS()
class LOCKDOWN_API ALDVisionDirector : public AActor
{
    GENERATED_BODY()
public:
    ALDVisionDirector();
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    bool BeginMatch(const FLDMatchContext& InMatch, FString& Error);
    void EndMatch();
    bool RegisterObserver(const FGuid& ContenderId, ALDPlayerController* Controller, FString& Error);
    void UnregisterObserver(const FGuid& ContenderId);
    // Read-only Publish work after the driver's last completed step; samples on the core clock.
    bool PublishAuthority(const FLDMatchContext& InMatch, FString& Error);
    bool RecordObservedDeath(const FLDGameplayWritePermit& Permit, const FLDCombatBodyRef& Victim,
        double OccurredAtSeconds, FString& Error);
    ELDObservedResolve ResolveObservedUnit(const ALDPlayerController& RegisteredCaller, const FGuid& Handle,
        uint32 ObservationEpoch, ELDObservationPurpose Purpose, FLDAuthorityTargetSample& Out, FString* Diagnostic = nullptr) const;
    bool EnumerateVisibleUnits(const ALDPlayerController& RegisteredCaller, ELDObservationPurpose Purpose,
        FLDWorldView& Out, FString& Error) const;
    bool FindObservedTermination(const ALDPlayerController& RegisteredCaller, const FGuid& Handle,
        uint32 ObservationEpoch, FLDObservedTerminationFact& Out) const;
    bool BindObservedOrder(const ALDPlayerController& Caller, const FLDMoveIdentity& Order, FGuid Handle,
        uint32 ObservationEpoch, const FLDGameplayWritePermit& Permit, FString& Error);
    bool ReleaseObservedOrder(const ALDPlayerController& Caller, const FLDMoveIdentity& Order, FString& Error);
    ELDSightResult TraceSight(const FVector& SourceFootCm, const FVector& TargetFootCm, const FLDWorldRevision& Expected) const;
    // Whether an observer standing at SourceFootCm may observe Target now: static sight plus the target's stealth.
    // Every enemy perception (contenders, opponent AI, camp members, taunt upkeep) uses this, never bare TraceSight.
    ELDSightResult TraceObservation(const FVector& SourceFootCm, const ALDCharacter& Target, const FLDWorldRevision& Expected) const;
    // The caller's current observer-local handle of Body, only while Body is bound in its latest sample.
    bool FindObservedHandle(const ALDPlayerController& Caller, const FLDCombatBodyRef& Body, FGuid& OutHandle, uint32& OutEpoch) const;
    static constexpr int64 SamplePeriodMs = 50;
    static constexpr int64 PublishPeriodMs = 100;
private:
    struct FObserver
    {
        FGuid ContenderId;
        TWeakObjectPtr<ALDPlayerController> Controller;
        TWeakObjectPtr<ALDWorldViewRelay> Relay;
        FLDObserverLedger Ledger;
        FLDWorldView Latest;
        bool bHasLatest = false;
    };
    ALDGameMode* AuthorityMode() const;
    FObserver* FindObserver(const ALDPlayerController& Caller);
    const FObserver* FindObserver(const ALDPlayerController& Caller) const;
    bool ReadObserver(const FObserver& Observer, FLDContenderLifeRead& Life, FVector& Foot, FString& Error) const;
    // Authority-time observation of a registered body (sight plus its stealth) that also proves the registered world
    // has not changed under observation.
    ELDSightResult CurrentObservation(const FVector& SourceFootCm, const ALDCharacter& Target) const;
    bool GatherTargets(TArray<FLDObservedTarget>& Out, FString& Error) const;
    bool SampleObserver(FObserver& Observer, const TArray<FLDObservedTarget>& Targets,
        const TArray<FLDObservedProjectileFact>& Flights, bool bPublish, FString& Error);
    static FVector FootOf(const ALDCharacter& Body);
    TArray<FObserver> Observers;
    FLDMatchContext ActiveMatch;
    FLDSightGeometry Sight;
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    int64 NextSampleMs = 0;
    int64 NextPublishMs = 0;
    bool bSampled = false;
    bool bActive = false;
};
