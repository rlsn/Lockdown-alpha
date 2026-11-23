#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/LDWorldQueryTypes.h"
#include "Vision/LDViewTypes.h"
#include "LDWorldViewRelay.generated.h"
class ULDHumanoidVisualComponent;
class ULDFlightTrailComponent;
class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UStaticMesh;
class UPrimitiveComponent;
class ALDPlayerController;
class ALDRecoveryOrbMarker;
class UTexture2D;
class UMaterialInstanceDynamic;
class FLDDemoContentSnapshot;
struct FLDLocalGroundFog;

enum class ELDObservedViewStatus : uint8 { Unavailable, Current, Stale };
enum class ELDObservedClearReason : uint8
{
    Conceal, FullSetRemoved, ObservedDeath, Timeout, ObserverReset, MatchReset, Disconnected, MatchEnded, ProtocolRejected
};

struct FLDObservedClear
{
    FGuid Handle;
    uint32 ObservationEpoch = 0;
    ELDObservedClearReason Reason = ELDObservedClearReason::Conceal;
};

struct FLDObservedFrameRead
{
    const FLDWorldView* Frame = nullptr;
    ELDObservedViewStatus Status = ELDObservedViewStatus::Unavailable;
    uint64 LocalSerial = 0;
};

// Approved public definitions that a received unit may reference; unknown references are protocol errors.
struct LOCKDOWN_API FLDObservedCatalog
{
    uint8 MaxLevel = 0;
    double RecoveryPackRadiusCm = 0.;
    TSet<uint8> Creatures;
    // #198: every approved ability is a Q/W/E active, a cast source or a slot a visible contender shows.
    TSet<uint8> Abilities;
    bool Build(const FLDDemoContentSnapshot& Content, FString& Error);
    bool Accepts(const FLDObservedUnitView& Unit, FString& Error) const;
};

// Owner-local acceptance of full views, concealments and terminations under one ordering cursor.
class LOCKDOWN_API FLDObservedFrameReceiver
{
public:
    enum class EResult : uint8 { Applied, Stale, Rejected };
    // Clean-link receive timeout; SetStaleAfter widens it with the measured arrival jitter (LDObservedPresentation::Timing).
    static constexpr double StaleAfterSeconds = 0.25;
    void SetStaleAfter(double Seconds) { StaleAfter = FMath::Max(Seconds, StaleAfterSeconds); }
    double GetStaleAfter() const { return StaleAfter; }
    EResult AcceptFull(const FLDWorldView& View, const FLDWorldRevision& ExpectedWorld, const FLDObservedCatalog& Catalog,
        double NowSeconds, TArray<FLDObservedClear>& Cleared, FString& Error);
    EResult AcceptConceal(const FLDObservedConceal& Notice, TArray<FLDObservedClear>& Cleared, FString& Error);
    EResult AcceptTermination(const FLDObservedTerminationNotice& Notice, TArray<FLDObservedClear>& Cleared, FString& Error);
    bool Tick(double NowSeconds, TArray<FLDObservedClear>& Cleared);
    void Reset(ELDObservedClearReason Reason, TArray<FLDObservedClear>& Cleared);
    FLDObservedFrameRead Read() const;
    bool FindUnit(const FGuid& Handle, uint32 ObservationEpoch, FLDObservedUnitView& Out) const;
    uint32 CursorMatch() const { return Match; }
private:
    bool Advance(uint32 InMatch, uint32 InGeneration, uint32 InRevision, TArray<FLDObservedClear>& Cleared);
    void ClearUnits(ELDObservedClearReason Reason, TArray<FLDObservedClear>& Cleared);
    void ApplyTermination(const FLDObservedUnitTermination& Termination, TArray<FLDObservedClear>& Cleared);
    FLDWorldView Frame;
    ELDObservedViewStatus Status = ELDObservedViewStatus::Unavailable;
    uint32 Match = 0, Generation = 0, Revision = 0, LastTermination = 0;
    double LastFullAt = -1;
    double StaleAfter = StaleAfterSeconds;
    uint64 Serial = 0;
};

// Authorized samples are shown on one delayed clock (SC-07): a pose is interpolated between two received samples at
// most and held at the newest one, never extrapolated; a sample's cue and statuses show when the clock reaches it, so
// actions, flights and hit reactions stay in step with the positions they belong to.
namespace LDObservedPresentation
{
    // The clock trails the authority sample time by one publication interval, a margin and the measured arrival
    // lateness of recent frames (Timing), never less than MinDelaySeconds and never more than MaxDelaySeconds. The floor
    // leaves 10 ms over one 20 Hz interval and the margin, so a clean link (and the Listen Host's own relay) shows other
    // bodies as fresh as interpolation allows; any measured lateness raises the delay above it.
    constexpr double MinDelaySeconds = .09;
    constexpr double MaxDelaySeconds = .6;
    // Samples a proxy keeps: a clock MaxDelaySeconds behind the newest sample still has its bracketing samples at the
    // 20 Hz publish rate, with interpolation headroom (static_assert in LDWorldViewRelay.cpp); a stalled clock still
    // keeps a bounded queue.
    constexpr int32 HistorySamples = 16;
    // A finished flight waits for the presentation clock to pass its last sample, at most this long in local time: the
    // longest presentation delay plus margin. Stale, reset and non-flight ends still clear at once.
    constexpr double FlightEndSeconds = MaxDelaySeconds + .2;
    constexpr double DelayMarginSeconds = .03;
    // The receive timeout keeps this much room over the delay, never below FLDObservedFrameReceiver::StaleAfterSeconds.
    constexpr double StaleOverDelaySeconds = .12;
    // Lateness envelope: a later arrival raises it at once; it then decays toward newer arrivals with this time constant.
    constexpr double LatenessReleaseSeconds = 4.;
    // The applied delay follows its target at most this fast (seconds of delay per second): growth slows the clock,
    // shrinking runs it slightly fast, so no shown pose jumps.
    constexpr double DelayGrowthPerSecond = .5;
    constexpr double DelayShrinkPerSecond = .05;
    // Later arrivals may raise the tracked offset only this fast; a jump beyond ReseedSeconds re-seeds it.
    constexpr double OffsetGrowthPerSecond = .05;
    constexpr double ReseedSeconds = .5;
    // A step longer than this between two samples (respawn, teleport, first sight) is shown as a jump, not a sweep.
    constexpr double SnapCm = 300.;
    struct FPoint
    {
        double At = 0;  // FLDWorldView::SampledAtServerSec of the sample
        FVector Location = FVector::ZeroVector;
        float Yaw = 0;
        bool bSnap = false;
    };
    struct FPose
    {
        FVector Location = FVector::ZeroVector;
        float Yaw = 0;
        FVector Velocity = FVector::ZeroVector;
        int32 Reached = INDEX_NONE;  // newest point the clock has passed; INDEX_NONE before the first
    };
    // The public cue and statuses one unit sample carried.
    struct FFacts
    {
        ELDObservedActionCue Cue = ELDObservedActionCue::None;
        uint16 CueAgeMs = 0;
        uint16 CueImpactDelayMs = 0;
        uint8 ActionAbility = 0;  // the Cast cue's ability, 0 otherwise
        bool bCharging = false;   // the public overdrive charge
        uint16 ChargeAgeMs = 0;
        bool bOverdriveCast = false;  // the Cast cue is an overdrive release
        uint8 RecentReaction = 0, RecentReactionElement = 0;
        uint16 RecentReactionAgePlusOneMs = 0;
        ELDObservedHitLook HitLook = ELDObservedHitLook::None;
        uint16 RecentHitAgePlusOneMs = 0;
        FGuid RecentHitId;
        uint8 HitFlags = 0;
        TArray<FLDObservedStatus> Statuses;
    };
    // Points ascend by At. Before the first point: the first; between two: linear, yaw along the shorter arc; at or after
    // the newest: the newest with zero velocity. A snap point is reached without sweeping toward it.
    LOCKDOWN_API FPose Sample(TConstArrayView<FPoint> Points, double At);
    LOCKDOWN_API float HitAgeSeconds(uint16 AgePlusOneMs, double ElapsedSinceSample);
    LOCKDOWN_API bool ImmediateHitAge(const FLDObservedFrameRead& Read, const FLDObservedHitReceipt& Hit,
        double AuthorityNowSeconds, float& OutAgeSeconds);
    // Local-minus-authority clock offset from one arrival: an earlier arrival lowers it at once, later ones raise it by at
    // most OffsetGrowthPerSecond, and a jump beyond ReseedSeconds (another authority clock) re-seeds it.
    LOCKDOWN_API double TrackOffset(bool bHasOffset, double Offset, double SecondsSinceUpdate, double ArrivalLocalSeconds, double SampledAtServerSeconds);
    // Lateness of one accepted arrival is how far it came after the tracked offset (never negative). The envelope keeps
    // the largest recent lateness: a larger one replaces it, otherwise it decays toward the new one.
    LOCKDOWN_API double TrackLateness(double Envelope, double SecondsSinceUpdate, double Lateness);
    struct FTiming
    {
        double DelaySeconds = MinDelaySeconds;
        double StaleAfterSeconds = FLDObservedFrameReceiver::StaleAfterSeconds;
    };
    // Target delay = publish interval + margin + lateness envelope, clamped to [MinDelaySeconds, MaxDelaySeconds];
    // receive timeout = delay + StaleOverDelaySeconds, never below the clean-link timeout. A clean link keeps the floors.
    LOCKDOWN_API FTiming Timing(double LatenessEnvelope, double PublishIntervalSeconds);
    // Moves the applied delay toward the target within the growth and shrink rates.
    LOCKDOWN_API double SlewDelay(double Applied, double Target, double DeltaSeconds);
    // The receive timeout widens to its target at once and narrows only this fast, so a link that spiked once keeps
    // room for the next spike long after the delay has come back down. A wide timeout costs no latency.
    constexpr double TimeoutShrinkPerSecond = .01;
    LOCKDOWN_API double TrackTimeout(double Applied, double Target, double DeltaSeconds);
}

// Client-only fading of tall props that stand between the gameplay camera and a body the player must see (A4), or that
// run through a body standing in or against them. A prop opts in with the component tag LDFadeable; its material fades
// through the scalar parameter LDFade (0 opaque, 1 most faded) and, while it fades, casts no shadow. Only the own living
// body and units presented right now are tested, so a fade never reveals a hidden unit. Presentation only: collision,
// sight and navigation never read it.
namespace LDPropFade
{
    inline const FName Tag = TEXT("LDFadeable");
    inline const FName Parameter = TEXT("LDFade");
    // Scalar the relay sets on each fading material: at LDFade 1 a pixel survives with probability 1 - FadedStrength,
    // retaining 70% of the prop at full fade so nearby obstacles remain readable.
    inline const FName StrengthParameter = TEXT("LDFadeStrength");
    constexpr float FadedStrength = .3f;
    // A building or tall tree (bounds at least TallPropCm high and BroadPropCm across both ways; a pole is not) hides
    // whole bodies: it keeps only 30% at full fade.
    constexpr double TallPropCm = 600.;
    constexpr double BroadPropCm = 200.;
    constexpr float TallFadedStrength = .7f;
    LOCKDOWN_API float StrengthFor(const FBox& Bounds);
    // Room around the prop's bounds, and the stretch just before the target that the line test skips (a prop right behind
    // the body is not in front of it; one running through the body is caught by PierceRadiusCm).
    constexpr double BoundsSlackCm = 15.;
    constexpr double TargetClearCm = 40.;
    // A target within this horizontal distance of the prop's bounds, inside its height, is pierced by it (a body about
    // 28 cm in capsule radius, plus its arms).
    constexpr double PierceRadiusCm = 45.;
    // Full fade in or out takes 1/FadePerSecond seconds.
    constexpr float FadePerSecond = 6.f;
    // One sweep of the level actors for tagged scene components takes about this long.
    constexpr double SweepSeconds = 2.;
    // The sliced sweep over the level actors: where it stands and whether one whole sweep has finished.
    struct LOCKDOWN_API FSweep
    {
        int32 Level = 0;
        int32 Index = 0;
        bool bComplete = false;
        // Entries to visit this frame: all of them (and the wrap) on the first sweep, afterwards the share of Total
        // that finishes a sweep in SweepSeconds at this frame time, never fewer than a small floor.
        int32 Budget(int32 Total, float DeltaSeconds) const;
        // The next entry among the per-level entry counts. False once a sweep ends: the cursor is back at the start
        // and bComplete is set. Counts may grow or shrink between calls.
        bool Next(TConstArrayView<int32> Counts, int32& OutLevel, int32& OutIndex);
    };
    LOCKDOWN_API bool Occludes(const FBox& Bounds, const FVector& Camera, const FVector& Target);
    // The only space an occluding prop can reach into: bounds that miss this box occlude none of the targets, so the
    // per-target tests are skipped for them. Invalid without targets.
    LOCKDOWN_API FBox Reach(const FVector& Camera, TConstArrayView<FVector> Targets);
    LOCKDOWN_API float Step(float Current, bool bOccluding, float DeltaSeconds);
}

// Client-only presentation of one authorized unit. It never collides, navigates or owns gameplay state.
UCLASS()
class LOCKDOWN_API ALDObservedEntityProxy : public AActor
{
    GENERATED_BODY()
public:
    ALDObservedEntityProxy();
    // Queues one authorized sample; an equal or older sample time is ignored.
    void Apply(const FLDObservedUnitView& Unit, double VisualHalfHeightCm, double SampledAtServerSec);
    // Poses the body at the relay's delayed presentation time; the relay drives every proxy from one clock.
    void Present(double AtServerSec, float Delta);
    bool PresentImmediateHit(float AgeSeconds, ELDObservedHitLook HitLook, uint8 HitFlags, const FGuid& HitId, int64 HitAtMs);
    // An authorized observed death: leaves a client-only falling copy of this body before the proxy is removed.
    void PlayDeathFall() const;
    bool TracePresentedModel(const FVector& Start, const FVector& End, double& OutFraction) const;
    bool PresentedPickVolume(FVector& OutFoot, FVector& OutTop, double& OutRadiusCm) const;
    // #97 remote clients only: a query-only body capsule that blocks the own predicted body, as the authority body does
    // on the server, so prediction stops where the server will and is not corrected through the presented body.
    void EnableBodyBlocking(float RadiusCm, float HalfHeightCm);
    bool IsBodyBlocking() const;
    FGuid ObservedHandle;
    uint32 ObservedEpoch = 0;
private:
    UPROPERTY() TObjectPtr<ULDHumanoidVisualComponent> Body;
    UPROPERTY() TObjectPtr<class UCapsuleComponent> Blocker;
    // Parallel queues of received samples, oldest first, trimmed as the clock passes them.
    TArray<LDObservedPresentation::FPoint, TInlineAllocator<8>> Points;
    TArray<LDObservedPresentation::FFacts, TInlineAllocator<8>> Facts;
    double ShownAt = -1;
    uint16 Look = MAX_uint16;
    bool bPlaced = false;
};

// Client-only marker under this machine's own living body, so the player finds themself at a glance (APP_SPEC 8).
// Never replicated and never attached to anything but the local viewer's own body.
UCLASS()
class LOCKDOWN_API ALDOwnBodyMarker : public AActor
{
    GENERATED_BODY()
public:
    ALDOwnBodyMarker();
    // Follows Body's feet in the roster-slot tint; a null body hides the marker.
    void Track(APawn* Body, uint8 Slot);
private:
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Rim;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Core;
    TWeakObjectPtr<APawn> Tracked;
    uint8 TintSlot = MAX_uint8;
};

// Client-only presentation of one visible skill effect. It never collides and is never a target. Flights finish their
// last authorized samples on the presentation clock (bounded by EndSeconds); areas disappear with their authorized
// frame. A stale, reset or foreign view removes it at once.
UCLASS()
class LOCKDOWN_API ALDObservedProjectileProxy : public AActor
{
    GENERATED_BODY()
public:
    static constexpr double EndSeconds = LDObservedPresentation::FlightEndSeconds;
    ALDObservedProjectileProxy();
    void Apply(const FLDObservedProjectileView& Projectile, double SampledAtServerSec);
    void Present(double AtServerSec);
    // bNaturalEnd: the authority ended it in plain sight (ALDWorldViewRelay checks the observer's coverage); only then
    // does it play its end burst. A visibility removal, reset or stale view never does.
    void End(double LocalSeconds, bool bNaturalEnd = false);
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    // The last authorized position and the one its path heads to next (equal for a still effect).
    bool LastPath(FVector& Last, FVector& Next) const;
    // Against the removal frame's authority sample time (not the delayed playback clock).
    bool WarningDue(double SampledAtServerSec) const { return WarningAtServerSec > 0. && SampledAtServerSec >= WarningAtServerSec - .15; }
    bool HasWarning() const { return WarningAtServerSec > 0.; }
    bool IsFinished(double AtServerSec, double LocalSeconds) const;
    // The look's appear burst (a launch, a landing; it lives with this proxy) or, after a natural end, its end burst
    // (a flight's hit, a dropped book), which plays out on its own.
    void PlayBurst(bool bEnd);
    FGuid ObservedHandle;
    const ULDFlightTrailComponent* GetTrail() const { return Trail; }
private:
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Chalk;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Accent;
    UPROPERTY() TObjectPtr<UStaticMesh> BoxMesh;
    UPROPERTY() TObjectPtr<UStaticMesh> CylinderMesh;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> EffectTint;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> AccentTint;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> ShapeLines;
    UPROPERTY() TObjectPtr<ULDFlightTrailComponent> Trail;
    // The look's particles (LDHitVfx::TrailOf / ZoneOf), rebuilt with the look.
    UPROPERTY() TObjectPtr<class UNiagaraComponent> Particles;
    // The local effect scale the particles were built with; a change of the reduced-effects setting rebuilds them.
    float ParticleScale = -1.f;
    FRotator ShownHeading = FRotator::ZeroRotator;
    bool bNaturalEnd = false;
    void ApplyLook(const FLDObservedProjectileView& Effect);
    ELDObservedProjectileLook ShownLook=ELDObservedProjectileLook::Chalk;
    float ShownRadius=-1, ShownInnerRadius=-1, ShownAngle=-1;
    float ShownLength=-1;
    bool bShownOwn=false;
    FLinearColor ShownColor=FLinearColor::White;
    double WarningAtServerSec=-1;
    TArray<LDObservedPresentation::FPoint, TInlineAllocator<8>> Points;
    double EndedAt = -1;
    bool bPlaced = false;
};

// Client-only presentation of one public ground state (spec §12): the kind's own line pattern in its colour (never colour
// alone) and the owner's seat-coloured outline, which drains around the footprint as its life runs out. Two instanced
// meshes per state; it never collides, blocks or hides anything.
UCLASS()
class LOCKDOWN_API ALDObservedGroundProxy : public AActor
{
    GENERATED_BODY()
public:
    ALDObservedGroundProxy();
    void Apply(const FLDObservedGroundState& Ground, double SampledAtServerSec);
    void Present(double AtServerSec);
    static FLinearColor KindColor(uint8 Kind);
    // The footprint outline (closed, local space) and the kind's pattern segments for a state of this shape.
    static void Outline(const FLDObservedGroundState& Ground, TArray<FVector>& Out);
    static void Pattern(const FLDObservedGroundState& Ground, TArray<TPair<FVector,FVector>>& Out);
    int32 OutlineShown() const { return ShownOutline; }
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    const class UNiagaraComponent* GetParticles() const { return Particles; }
private:
    // The kind's looping particles (LDHitVfx::GroundZoneOf) over the pattern, rebuilt with the shape or the effect scale.
    UPROPERTY() TObjectPtr<class UNiagaraComponent> Particles;
    float ParticleScale = -1.f;
    void ShowParticles();
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Lines;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Rim;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> KindTint;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> SeatTint;
    FLDObservedGroundState Shown;
    bool bShown=false;
    double ExpiresAtServerSec=0, LifeSeconds=1;
    TArray<FVector> RimPoints;
    int32 ShownOutline=-1;
    static FTransform SegmentTransform(const FVector& A, const FVector& B, double Thickness);
};

UCLASS()
class LOCKDOWN_API ALDWorldViewRelay : public AActor
{
    GENERATED_BODY()
public:
    ALDWorldViewRelay();
    void FreezeForWorldTeardown();
    virtual void Tick(float Delta) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    UFUNCTION(Client,Unreliable) void ClientFullView(const FLDWorldView& View);
    UFUNCTION(Client,Unreliable) void ClientPresentedHit(const FLDObservedHitReceipt& Hit);
    UFUNCTION(Client,Reliable) void ClientConceal(const FLDObservedConceal& Notice);
    UFUNCTION(Client,Reliable) void ClientTermination(const FLDObservedTerminationNotice& Notice);
    FLDObservedFrameRead ReadObservedFrame() const { return Receiver.Read(); }
    // Static ground presentation only. This bitmap never authorizes an entity, ground effect or projectile.
    const TArray<uint8>* ReadGroundFog(uint64& OutSerial) const;
    bool ReadUnit(const FGuid& Handle, uint32 ObservationEpoch, FLDObservedUnitView& Out) const
    { return Receiver.FindUnit(Handle, ObservationEpoch, Out); }
    const TArray<FLDObservedClear>& RecentClears() const { return LastClears; }
    int32 ProxyCount() const { return Proxies.Num(); }
    uint32 ImmediateHitCount() const { return AcceptedImmediateHits; }
    int32 ProjectileProxyCount() const { return ProjectileProxies.Num(); }
    int32 GroundProxyCount() const { return GroundProxies.Num(); }
    int32 RecoveryOrbProxyCount() const { return RecoveryOrbProxies.Num(); }
    // Display location (body centre) of a unit presented right now: its proxy's interpolated pose. False unless the
    // current frame lists this handle and epoch and its proxy is placed, so callers never label a hidden or stale unit.
    bool ReadPresentedLocation(const FGuid& Handle, uint32 ObservationEpoch, FVector& OutWorld) const;
    // The unit whose shown model the ray hits first; failing any surface hit, the unit whose pick volume (model height,
    // root ring footprint) passes closest to the ray within AngleTolerance radians. Sight blockers stop both.
    bool PickPresentedUnit(const FVector& RayStart, const FVector& RayDirection, double AngleTolerance,
        FGuid& OutHandle, uint32& OutEpoch) const;
    // The delayed authority time (FLDWorldView::SampledAtServerSec scale) at which proxies and flights are posed now,
    // so overlays can show a sample's facts when its pose shows. False before the first accepted frame.
    bool ReadPresentationTime(double& OutAtServerSec) const;
    // This client's applied presentation delay, arrival lateness envelope and receive timeout, for the playtest log.
    double PresentationDelaySeconds() const { return ShownDelay; }
    double ArrivalLatenessSeconds() const { return Lateness; }
    double ReceiveTimeoutSeconds() const { return Receiver.GetStaleAfter(); }
    // Receive timeouts (every unit cleared for lack of frames) since this relay began.
    uint32 TimeoutCount() const { return Timeouts; }
    // The largest view a connection delivered to this relay since the last call, in bytes; then starts over.
    int32 TakePeakViewWireBytes() { const int32 Peak = PeakViewWireBytes; PeakViewWireBytes = 0; return Peak; }
private:
    int32 PeakViewWireBytes = 0;
    bool bFrozenForTeardown = false;
    bool IsShuttingDown() const;
    uint32 Timeouts = 0;
    uint32 AcceptedImmediateHits = 0;
    void Present(const TArray<FLDObservedClear>& Cleared);
    void Refuse(const FString& Error);
    bool EnsureCatalog(FString& Error);
    bool ExpectedWorld(FLDWorldRevision& Out) const;
    void SyncProxies();
    // Marks this viewer's own living body with a local ring; hides it while dead or bodiless.
    void TrackOwnBody(const ALDPlayerController& Owning);
    void AttachFog();
    void UpdateGroundFog(const ALDPlayerController& Owning, double Now);
    void UploadFog();
    void BlendFog(float Delta);
    // Delayed authority time at which proxies and flights are posed.
    double PresentationAt(double LocalSeconds) const { return LocalSeconds - ClockOffset - ShownDelay; }
    void PresentProxies(float Delta);
    // Whether the frame's current coverage (the observer's sight) holds the ground cell under Point.
    bool CoversPoint(const FLDWorldView& Frame, const FVector& Point) const;
    // Fades tagged props that hide the own body or a presented unit from the camera (LDPropFade).
    void FadeOccludingProps(const ALDPlayerController& Owning, float Delta);
    struct FFadeableProp
    {
        TWeakObjectPtr<UPrimitiveComponent> Primitive;
        TArray<TWeakObjectPtr<UMaterialInstanceDynamic>> Materials;
        float Fade = 0;
        // Instanced props with a per-instance fade: each instance's world bounds and fade.
        TArray<FBox> InstanceBounds;
        TArray<float> InstanceFades;
        FBox InstanceUnion = FBox(ForceInit);
        // Instances not at full opacity; while none is and the union misses the reach, the prop is skipped whole.
        int32 InstancesFaded = 0;
        bool bPerInstance = false;
        bool bResolved = false;
        bool bParameter = false;
        bool bCastShadow = false;  // the prop's own shadow setting, restored when it is opaque again
    };
    TArray<FFadeableProp> FadeableProps;
    double FadeablesScannedAt = -1;
    LDPropFade::FSweep FadeSweep;
    FLDObservedFrameReceiver Receiver;
    FLDObservedCatalog Catalog;
    bool bCatalogReady = false;
    TArray<FLDObservedClear> LastClears;
    UPROPERTY() TMap<FGuid,TObjectPtr<ALDObservedEntityProxy>> Proxies;
    UPROPERTY() TMap<FGuid,TObjectPtr<ALDObservedProjectileProxy>> ProjectileProxies;
    // Public ground states of the current frame, by their observed id.
    UPROPERTY() TMap<uint8,TObjectPtr<ALDObservedGroundProxy>> GroundProxies;
    // Sight-authorized camp-clear recovery orbs of the current frame, by their serial.
    UPROPERTY() TMap<uint64,TObjectPtr<ALDRecoveryOrbMarker>> RecoveryOrbProxies;
    UPROPERTY() TObjectPtr<ALDOwnBodyMarker> OwnMarker;
    UPROPERTY() TObjectPtr<UTexture2D> FogTexture;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> FogMaterial;
    TWeakObjectPtr<AActor> CameraOwner;
    TSharedPtr<FLDLocalGroundFog> GroundFog;
    uint64 GroundFogSerial = 0;
    uint64 FogUploadSerial = MAX_uint64;
    TArray<float> FogTarget;
    TArray<float> FogShown;
    bool bFogBlending = false;
    bool bClock = false;
    double ClockOffset = 0;
    double ClockUpdatedAt = 0;
    // Adaptive presentation timing of this client (LDObservedPresentation::Timing).
    double Lateness = 0;
    double TargetDelay = LDObservedPresentation::MinDelaySeconds;
    double ShownDelay = LDObservedPresentation::MinDelaySeconds;
    double DelaySlewedAt = -1;
};
