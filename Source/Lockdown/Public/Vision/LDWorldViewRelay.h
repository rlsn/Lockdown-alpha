#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/LDWorldQueryTypes.h"
#include "Vision/LDViewTypes.h"
#include "LDWorldViewRelay.generated.h"
class ULDHumanoidVisualComponent;
class ULDFlightTrailComponent;
class UStaticMeshComponent;
class UPrimitiveComponent;
class ALDPlayerController;
class UTexture2D;
class UMaterialInstanceDynamic;
class FLDDemoContentSnapshot;

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
    TSet<uint8> Equipment;
    TSet<uint8> Creatures;
    TSet<uint8> Abilities;
    bool Build(const FLDDemoContentSnapshot& Content, FString& Error);
    bool Accepts(const FLDObservedUnitView& Unit, FString& Error) const;
};

// Owner-local acceptance of full views, concealments and terminations under one ordering cursor.
class LOCKDOWN_API FLDObservedFrameReceiver
{
public:
    enum class EResult : uint8 { Applied, Stale, Rejected };
    static constexpr double StaleAfterSeconds = 0.25;
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
    uint64 Serial = 0;
};

// Authorized samples are shown on one delayed clock (SC-07): a pose is interpolated between two received samples at
// most and held at the newest one, never extrapolated; a sample's cue and statuses show when the clock reaches it, so
// actions, flights and hit reactions stay in step with the positions they belong to.
namespace LDObservedPresentation
{
    // The clock trails the authority sample time by one publication interval plus a jitter margin.
    constexpr double DelaySeconds = .13;
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
        TArray<FLDObservedStatus> Statuses;
    };
    // Points ascend by At. Before the first point: the first; between two: linear, yaw along the shorter arc; at or after
    // the newest: the newest with zero velocity. A snap point is reached without sweeping toward it.
    LOCKDOWN_API FPose Sample(TConstArrayView<FPoint> Points, double At);
    // Local-minus-authority clock offset from one arrival: an earlier arrival lowers it at once, later ones raise it by at
    // most OffsetGrowthPerSecond, and a jump beyond ReseedSeconds (another authority clock) re-seeds it.
    LOCKDOWN_API double TrackOffset(bool bHasOffset, double Offset, double SecondsSinceUpdate, double ArrivalLocalSeconds, double SampledAtServerSeconds);
}

// Client-only fading of tall props that stand between the gameplay camera and a body the player must see (A4). A prop
// opts in with the component tag LDFadeable; its material fades through the scalar parameter LDFade (0 opaque, 1 most
// faded). Only the own living body and units presented right now are tested, so a fade never reveals a hidden unit.
namespace LDPropFade
{
    inline const FName Tag = TEXT("LDFadeable");
    inline const FName Parameter = TEXT("LDFade");
    // Room around the prop's bounds, and the stretch before the target that does not count (a prop at the body's own
    // spot is not in front of it).
    constexpr double BoundsSlackCm = 15.;
    constexpr double TargetClearCm = 40.;
    // Full fade in or out takes 1/FadePerSecond seconds.
    constexpr float FadePerSecond = 6.f;
    LOCKDOWN_API bool Occludes(const FBox& Bounds, const FVector& Camera, const FVector& Target);
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
    // An authorized observed death: leaves a client-only falling copy of this body before the proxy is removed.
    void PlayDeathFall() const;
    FGuid ObservedHandle;
    uint32 ObservedEpoch = 0;
private:
    UPROPERTY() TObjectPtr<ULDHumanoidVisualComponent> Body;
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

// Client-only presentation of one visible flight (a thrown chalk stub). It never collides and is never a target. Once
// the current frame stops listing it, it only finishes its last authorized samples on the presentation clock (bounded
// by EndSeconds) and is then removed; a stale, reset or foreign view removes it at once.
UCLASS()
class LOCKDOWN_API ALDObservedProjectileProxy : public AActor
{
    GENERATED_BODY()
public:
    static constexpr double EndSeconds = .4;
    ALDObservedProjectileProxy();
    void Apply(const FLDObservedProjectileView& Projectile, double SampledAtServerSec);
    void Present(double AtServerSec);
    void End(double LocalSeconds);
    bool IsFinished(double AtServerSec, double LocalSeconds) const;
    FGuid ObservedHandle;
private:
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Chalk;
    UPROPERTY() TObjectPtr<ULDFlightTrailComponent> Trail;
    TArray<LDObservedPresentation::FPoint, TInlineAllocator<8>> Points;
    double EndedAt = -1;
    bool bPlaced = false;
};

UCLASS()
class LOCKDOWN_API ALDWorldViewRelay : public AActor
{
    GENERATED_BODY()
public:
    ALDWorldViewRelay();
    virtual void Tick(float Delta) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    UFUNCTION(Client,Unreliable) void ClientFullView(const FLDWorldView& View);
    UFUNCTION(Client,Reliable) void ClientConceal(const FLDObservedConceal& Notice);
    UFUNCTION(Client,Reliable) void ClientTermination(const FLDObservedTerminationNotice& Notice);
    FLDObservedFrameRead ReadObservedFrame() const { return Receiver.Read(); }
    bool ReadUnit(const FGuid& Handle, uint32 ObservationEpoch, FLDObservedUnitView& Out) const
    { return Receiver.FindUnit(Handle, ObservationEpoch, Out); }
    const TArray<FLDObservedClear>& RecentClears() const { return LastClears; }
    int32 ProxyCount() const { return Proxies.Num(); }
    int32 ProjectileProxyCount() const { return ProjectileProxies.Num(); }
    // Display location (body centre) of a unit presented right now: its proxy's interpolated pose. False unless the
    // current frame lists this handle and epoch and its proxy is placed, so callers never label a hidden or stale unit.
    bool ReadPresentedLocation(const FGuid& Handle, uint32 ObservationEpoch, FVector& OutWorld) const;
    // The delayed authority time (FLDWorldView::SampledAtServerSec scale) at which proxies and flights are posed now,
    // so overlays can show a sample's facts when its pose shows. False before the first accepted frame.
    bool ReadPresentationTime(double& OutAtServerSec) const;
private:
    void Present(const TArray<FLDObservedClear>& Cleared);
    void Refuse(const FString& Error);
    bool EnsureCatalog(FString& Error);
    bool ExpectedWorld(FLDWorldRevision& Out) const;
    void SyncProxies();
    // Marks this viewer's own living body with a local ring; hides it while dead or bodiless.
    void TrackOwnBody(const ALDPlayerController& Owning);
    void AttachFog();
    // Takes the current coverage as the fog target; BlendFog then fades each cell toward it, so the lit area moves with
    // the published samples instead of jumping. Only authorized coverage is ever a target.
    void UploadFog();
    void BlendFog(float Delta);
    // Delayed authority time at which proxies and flights are posed.
    double PresentationAt(double LocalSeconds) const { return LocalSeconds - ClockOffset - LDObservedPresentation::DelaySeconds; }
    void PresentProxies(float Delta);
    // Fades tagged props that hide the own body or a presented unit from the camera (LDPropFade).
    void FadeOccludingProps(const ALDPlayerController& Owning, float Delta);
    struct FFadeableProp
    {
        TWeakObjectPtr<UPrimitiveComponent> Primitive;
        TArray<TWeakObjectPtr<UMaterialInstanceDynamic>> Materials;
        float Fade = 0;
        bool bResolved = false;
        bool bParameter = false;
    };
    TArray<FFadeableProp> FadeableProps;
    double FadeablesScannedAt = -1;
    FLDObservedFrameReceiver Receiver;
    FLDObservedCatalog Catalog;
    bool bCatalogReady = false;
    TArray<FLDObservedClear> LastClears;
    UPROPERTY() TMap<FGuid,TObjectPtr<ALDObservedEntityProxy>> Proxies;
    UPROPERTY() TMap<FGuid,TObjectPtr<ALDObservedProjectileProxy>> ProjectileProxies;
    UPROPERTY() TObjectPtr<ALDOwnBodyMarker> OwnMarker;
    UPROPERTY() TObjectPtr<UTexture2D> FogTexture;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> FogMaterial;
    TWeakObjectPtr<AActor> CameraOwner;
    uint64 FogSerial = MAX_uint64;
    TArray<uint8> FogTarget;
    TArray<float> FogShown;
    bool bFogBlending = false;
    bool bClock = false;
    double ClockOffset = 0;
    double ClockUpdatedAt = 0;
};
