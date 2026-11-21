#include "Vision/LDWorldViewRelay.h"
#include "Character/LDImpactFx.h"
#include "NiagaraComponent.h"
#include "Combat/LDCombatBagBlocker.h"
#include "Combat/LDCombatElements.h"
#include "Vision/LDFogDistanceField.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Core/LDLog.h"
#include "Character/LDHumanoidVisualComponent.h"
#include "Character/LDModelPicking.h"
#include "Character/LDBodyPuppet.h"
#include "Character/LDFlightTrailComponent.h"
#include "Character/LDCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameState.h"
#include "Navigation/LDNavigationComponent.h"
#include "Framework/LDDemoEntry.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDemoContentIds.h"
#include "Region/LDMapRegistry.h"
#include "Vision/LDVisionDirector.h"
#include "Region/LDMapGeometry.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/Level.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Camera/PlayerCameraManager.h"

static_assert(LDObservedPresentation::HistorySamples * ALDVisionDirector::PublishPeriodMs
    >= LDObservedPresentation::MaxDelaySeconds * 1000 + 2 * ALDVisionDirector::PublishPeriodMs,
    "A proxy history must span the longest presentation delay plus two publish intervals.");

struct FLDLocalGroundFog
{
    FLDSightGeometry Sight;
    FLDWorldRevision BuildRevision;
    FLDSightGeometry::FCoverageScan Pending;
    TWeakObjectPtr<const ALDCharacter> Pawn;
    uint32 Match = 0;
    uint32 Life = 0;
    bool bBlackout = false;
    bool bScanning = false;
    double NextScanAt = 0.;
    FVector Source = FVector::ZeroVector;
    TArray<uint8> OwnCoverage;
    TArray<uint8> DisplayCoverage;
    uint64 ArenaSerial = 0;
};

namespace
{
bool IsObservedFlight(ELDObservedProjectileLook Look)
{
    return Look==ELDObservedProjectileLook::Chalk || Look==ELDObservedProjectileLook::Sock
        || Look==ELDObservedProjectileLook::ChalkReturn || Look==ELDObservedProjectileLook::SockReturn;
}

bool IsObservedReturn(ELDObservedProjectileLook Look)
{
    return Look==ELDObservedProjectileLook::ChalkReturn || Look==ELDObservedProjectileLook::SockReturn;
}

bool IsObservedArea(ELDObservedProjectileLook Look)
{
    return Look==ELDObservedProjectileLook::AreaPlain || Look==ELDObservedProjectileLook::AreaMagic
        || Look==ELDObservedProjectileLook::AreaFire || Look==ELDObservedProjectileLook::AreaWater
        || Look==ELDObservedProjectileLook::AreaElectric;
}

FLinearColor ObservedEffectColor(ELDObservedProjectileLook Look)
{
    switch (Look)
    {
    case ELDObservedProjectileLook::Chalk:
    case ELDObservedProjectileLook::ChalkReturn: return FLinearColor(.95f,.95f,.9f);
    case ELDObservedProjectileLook::Sock:
    case ELDObservedProjectileLook::SockReturn: return FLinearColor(.55f,.85f,.25f);
    case ELDObservedProjectileLook::FlyerArming: return FLinearColor(.9f,.65f,.2f);
    case ELDObservedProjectileLook::AreaMagic: return FLinearColor(.65f,.2f,1.f);
    case ELDObservedProjectileLook::AreaFire: return FLinearColor(1.f,.18f,.015f);
    case ELDObservedProjectileLook::AreaWater: return FLinearColor(.02f,.62f,1.f);
    case ELDObservedProjectileLook::AreaElectric: return FLinearColor(1.f,.78f,.02f);
    case ELDObservedProjectileLook::AreaPlain: return FLinearColor(.95f,.48f,.12f);
    case ELDObservedProjectileLook::Road: return FLinearColor(1.f,.68f,.08f);
    case ELDObservedProjectileLook::Wind: return FLinearColor(.72f,.78f,.82f);
    case ELDObservedProjectileLook::Bag:
    case ELDObservedProjectileLook::BagWall: return FLinearColor(.5f,.32f,.14f);
    case ELDObservedProjectileLook::ConeWarning:
    case ELDObservedProjectileLook::LineWarning: return FLinearColor(1.f,.24f,.08f);
    case ELDObservedProjectileLook::BookShadow:
    case ELDObservedProjectileLook::FlyerArmed:
    case ELDObservedProjectileLook::SpeechCone: return FLinearColor(1.f,.38f,.16f);
    default: return FLinearColor(.92f,.9f,.98f);
    }
}
}

bool FLDObservedCatalog::Build(const FLDDemoContentSnapshot& Content, FString& Error)
{
    *this = FLDObservedCatalog();
    const FLDDemoContentDefinitions& Definitions = Content.Get();
    if (Definitions.Progression.MaxLevel < 1 || Definitions.Progression.MaxLevel > MAX_uint8)
    {
        Error = TEXT("Observed level validation requires the approved progression maximum.");
        return false;
    }
    MaxLevel = uint8(Definitions.Progression.MaxLevel);
    for (const FLDDemoMonsterDefinition& Monster : Definitions.Monsters) { Creatures.Add(Monster.NativeId); }
    for (const FLDDemoAbilityDefinition& Ability : Definitions.Abilities) { Abilities.Add(Ability.NativeId); }
    Error.Reset();
    return true;
}

bool FLDObservedCatalog::Accepts(const FLDObservedUnitView& Unit, FString& Error) const
{
    if ((Unit.AuthorizedFieldMask & LDObservation::Level) && (Unit.Level < 1 || Unit.Level > MaxLevel))
    {
        Error = TEXT("Observed level is outside the approved progression range.");
        return false;
    }
    if (Unit.ActionAbility != 0 && !Abilities.Contains(Unit.ActionAbility))
    {
        Error = TEXT("Observed cast source is not an approved public definition.");
        return false;
    }
    for (int32 Slot = 0; Slot < 3; ++Slot)
    {
        if (Unit.Actives.Abilities[Slot] != 0 && !Abilities.Contains(Unit.Actives.Abilities[Slot]))
        {
            Error = TEXT("Observed Q/W/E active is not an approved active ability.");
            return false;
        }
    }
    if (Unit.Kind == ELDObservedUnitKind::CampMember && !Creatures.Contains(Unit.CreatureId))
    {
        Error = TEXT("Observed creature is not an approved public definition.");
        return false;
    }
    Error.Reset();
    return true;
}

bool FLDObservedFrameReceiver::Advance(uint32 InMatch, uint32 InGeneration, uint32 InRevision, TArray<FLDObservedClear>& Cleared)
{
    const bool bNewer = InMatch > Match
        || (InMatch == Match && (InGeneration > Generation || (InGeneration == Generation && InRevision > Revision)));
    if (!bNewer) { return false; }
    if (InMatch != Match || InGeneration != Generation)
    {
        ClearUnits(Match != 0 && InMatch != Match ? ELDObservedClearReason::MatchReset : ELDObservedClearReason::ObserverReset, Cleared);
        Frame = FLDWorldView();
        if (Status == ELDObservedViewStatus::Current) { Status = ELDObservedViewStatus::Stale; }
        LastTermination = 0;
    }
    Match = InMatch;
    Generation = InGeneration;
    Revision = InRevision;
    return true;
}

void FLDObservedFrameReceiver::ClearUnits(ELDObservedClearReason Reason, TArray<FLDObservedClear>& Cleared)
{
    for (const FLDObservedUnitView& Unit : Frame.Units)
    {
        FLDObservedClear Clear;
        Clear.Handle = Unit.Handle;
        Clear.ObservationEpoch = Unit.ObservationEpoch;
        Clear.Reason = Reason;
        Cleared.Add(Clear);
    }
    Frame.Units.Reset();
    // Flights are presentation only: they have no epoch or clear record, they simply stop being drawn.
    Frame.Projectiles.Reset();
}

void FLDObservedFrameReceiver::ApplyTermination(const FLDObservedUnitTermination& Termination, TArray<FLDObservedClear>& Cleared)
{
    const int32 Index = Frame.Units.IndexOfByPredicate([&Termination](const FLDObservedUnitView& Unit)
    { return Unit.Handle == Termination.Handle && Unit.ObservationEpoch == Termination.ObservationEpoch; });
    if (Index == INDEX_NONE) { return; }
    FLDObservedClear Clear;
    Clear.Handle = Termination.Handle;
    Clear.ObservationEpoch = Termination.ObservationEpoch;
    Clear.Reason = ELDObservedClearReason::ObservedDeath;
    Cleared.Add(Clear);
    Frame.Units.RemoveAt(Index);
}

FLDObservedFrameReceiver::EResult FLDObservedFrameReceiver::AcceptFull(const FLDWorldView& View, const FLDWorldRevision& ExpectedWorld,
    const FLDObservedCatalog& Catalog, double NowSeconds, TArray<FLDObservedClear>& Cleared, FString& Error)
{
    if (!LDObservation::ValidateView(View, Error)) { return EResult::Rejected; }
    if (!ExpectedWorld.IsValid() || View.WorldMapId != ExpectedWorld.MapId
        || View.WorldLayoutRevision != ExpectedWorld.LayoutRevision || View.WorldGeometryRevision != ExpectedWorld.GeometryRevision)
    {
        Error = TEXT("The observation frame belongs to a different world revision.");
        return EResult::Rejected;
    }
    for (const FLDObservedUnitView& Unit : View.Units)
    {
        if (!Catalog.Accepts(Unit, Error)) { return EResult::Rejected; }
    }
    if (!Advance(View.MatchGeneration, View.Generation, View.Revision, Cleared)) { return EResult::Stale; }
    for (const FLDObservedUnitTermination& Termination : View.RecentTerminations)
    {
        if (Termination.TerminationSequence <= LastTermination) { continue; }
        ApplyTermination(Termination, Cleared);
        LastTermination = Termination.TerminationSequence;
    }
    for (const FLDObservedUnitView& Previous : Frame.Units)
    {
        if (View.Units.ContainsByPredicate([&Previous](const FLDObservedUnitView& Unit)
            { return Unit.Handle == Previous.Handle && Unit.ObservationEpoch == Previous.ObservationEpoch; })) { continue; }
        FLDObservedClear Clear;
        Clear.Handle = Previous.Handle;
        Clear.ObservationEpoch = Previous.ObservationEpoch;
        Clear.Reason = ELDObservedClearReason::FullSetRemoved;
        Cleared.Add(Clear);
    }
    Frame = View;
    Status = ELDObservedViewStatus::Current;
    LastFullAt = NowSeconds;
    ++Serial;
    Error.Reset();
    return EResult::Applied;
}

FLDObservedFrameReceiver::EResult FLDObservedFrameReceiver::AcceptConceal(const FLDObservedConceal& Notice,
    TArray<FLDObservedClear>& Cleared, FString& Error)
{
    if (!LDObservation::ValidateConceal(Notice, Error)) { return EResult::Rejected; }
    if (!Advance(Notice.MatchGeneration, Notice.Generation, Notice.Revision, Cleared)) { return EResult::Stale; }
    for (const FGuid& Handle : Notice.Removed)
    {
        Frame.Projectiles.RemoveAll([&Handle](const FLDObservedProjectileView& Projectile) { return Projectile.Handle == Handle; });
        const int32 Index = Frame.Units.IndexOfByPredicate([&Handle](const FLDObservedUnitView& Unit) { return Unit.Handle == Handle; });
        if (Index == INDEX_NONE) { continue; }
        FLDObservedClear Clear;
        Clear.Handle = Handle;
        Clear.ObservationEpoch = Frame.Units[Index].ObservationEpoch;
        Clear.Reason = ELDObservedClearReason::Conceal;
        Cleared.Add(Clear);
        Frame.Units.RemoveAt(Index);
    }
    ++Serial;
    Error.Reset();
    return EResult::Applied;
}

FLDObservedFrameReceiver::EResult FLDObservedFrameReceiver::AcceptTermination(const FLDObservedTerminationNotice& Notice,
    TArray<FLDObservedClear>& Cleared, FString& Error)
{
    if (!LDObservation::ValidateNotice(Notice, Error)) { return EResult::Rejected; }
    const FLDObservedUnitTermination& Termination = Notice.Termination;
    if (Notice.MatchGeneration == Match && Termination.Generation == Generation && Termination.TerminationSequence <= LastTermination)
    { return EResult::Stale; }
    if (!Advance(Notice.MatchGeneration, Termination.Generation, Notice.Revision, Cleared)) { return EResult::Stale; }
    ApplyTermination(Termination, Cleared);
    LastTermination = FMath::Max(LastTermination, Termination.TerminationSequence);
    Frame.RecentTerminations.Add(Termination);
    if (Frame.RecentTerminations.Num() > LDObservation::MaxTerminations) { Frame.RecentTerminations.RemoveAt(0); }
    ++Serial;
    Error.Reset();
    return EResult::Applied;
}

bool FLDObservedFrameReceiver::Tick(double NowSeconds, TArray<FLDObservedClear>& Cleared)
{
    if (Status != ELDObservedViewStatus::Current || LastFullAt < 0 || NowSeconds - LastFullAt <= StaleAfter) { return false; }
    // Never extrapolate a hidden or late unit; no LastSeen record survives the timeout.
    ClearUnits(ELDObservedClearReason::Timeout, Cleared);
    Frame.Footprint.CurrentBits.Reset();
    Status = ELDObservedViewStatus::Stale;
    ++Serial;
    return true;
}

void FLDObservedFrameReceiver::Reset(ELDObservedClearReason Reason, TArray<FLDObservedClear>& Cleared)
{
    ClearUnits(Reason, Cleared);
    Frame = FLDWorldView();
    Status = ELDObservedViewStatus::Unavailable;
    LastFullAt = -1;
    ++Serial;
}

FLDObservedFrameRead FLDObservedFrameReceiver::Read() const
{
    FLDObservedFrameRead Out;
    Out.Frame = Status == ELDObservedViewStatus::Unavailable ? nullptr : &Frame;
    Out.Status = Status;
    Out.LocalSerial = Serial;
    return Out;
}

bool FLDObservedFrameReceiver::FindUnit(const FGuid& Handle, uint32 ObservationEpoch, FLDObservedUnitView& Out) const
{
    if (Status != ELDObservedViewStatus::Current) { return false; }
    const FLDObservedUnitView* Unit = Frame.Units.FindByPredicate([&Handle, ObservationEpoch](const FLDObservedUnitView& Candidate)
    { return Candidate.Handle == Handle && Candidate.ObservationEpoch == ObservationEpoch; });
    if (!Unit) { return false; }
    Out = *Unit;
    return true;
}
LDObservedPresentation::FPose LDObservedPresentation::Sample(TConstArrayView<FPoint> Points, double At)
{
    FPose Pose;
    if (Points.IsEmpty()) { return Pose; }
    for (int32 Index = 0; Index < Points.Num() && Points[Index].At <= At; ++Index) { Pose.Reached = Index; }
    const FPoint& From = Points[FMath::Max(Pose.Reached, 0)];
    Pose.Location = From.Location;
    Pose.Yaw = From.Yaw;
    if (Pose.Reached == INDEX_NONE || Pose.Reached + 1 >= Points.Num()) { return Pose; }
    const FPoint& To = Points[Pose.Reached + 1];
    if (To.bSnap || To.At <= From.At) { return Pose; }
    const double Alpha = FMath::Clamp((At - From.At) / (To.At - From.At), 0.0, 1.0);
    Pose.Location = FMath::Lerp(From.Location, To.Location, Alpha);
    Pose.Yaw = From.Yaw + float(Alpha) * FMath::FindDeltaAngleDegrees(From.Yaw, To.Yaw);
    Pose.Velocity = (To.Location - From.Location) / (To.At - From.At);
    return Pose;
}

float LDObservedPresentation::HitAgeSeconds(uint16 AgePlusOneMs, double ElapsedSinceSample)
{
    return AgePlusOneMs ? float(double(AgePlusOneMs - 1) / 1000.0 + FMath::Max(0.0, ElapsedSinceSample)) : -1.f;
}

bool LDObservedPresentation::ImmediateHitAge(const FLDObservedFrameRead& Read, const FLDObservedHitReceipt& Hit,
    double AuthorityNowSeconds, float& OutAgeSeconds)
{
    const FLDWorldView* Frame = Read.Frame;
    if (Read.Status != ELDObservedViewStatus::Current || !Frame || Frame->MatchGeneration != Hit.MatchGeneration
        || Frame->ObserverLife != Hit.ObserverLife || Frame->Generation != Hit.Generation
        || !Hit.Handle.IsValid() || Hit.ObservationEpoch == 0 || !Hit.HitId.IsValid() || Hit.OccurredAtMs <= 0
        || uint8(Hit.HitLook) > uint8(ELDObservedHitLook::Electric)
        || (Hit.HitFlags & ~LDObservation::HitFlagsMask) != 0
        || ((Hit.HitFlags & LDObservation::ShieldBroken) && !(Hit.HitFlags & LDObservation::ShieldAbsorbed))
        || !FMath::IsFinite(AuthorityNowSeconds))
    { return false; }
    if (!Frame->Units.ContainsByPredicate([&Hit](const FLDObservedUnitView& Unit)
        { return Unit.Handle == Hit.Handle && Unit.ObservationEpoch == Hit.ObservationEpoch; }))
    { return false; }
    const double Age = AuthorityNowSeconds - double(Hit.OccurredAtMs) / 1000.0;
    if (Age < -.05 || Age >= .3) { return false; }
    OutAgeSeconds = float(FMath::Max(Age, 0.0));
    return true;
}

double LDObservedPresentation::TrackOffset(bool bHasOffset, double Offset, double SecondsSinceUpdate, double ArrivalLocalSeconds, double SampledAtServerSeconds)
{
    const double Candidate = ArrivalLocalSeconds - SampledAtServerSeconds;
    if (!bHasOffset || Candidate <= Offset || Candidate - Offset > ReseedSeconds) { return Candidate; }
    return FMath::Min(Candidate, Offset + FMath::Max(0.0, SecondsSinceUpdate) * OffsetGrowthPerSecond);
}

double LDObservedPresentation::TrackLateness(double Envelope, double SecondsSinceUpdate, double Lateness)
{
    const double Late = FMath::IsFinite(Lateness) ? FMath::Max(0.0, Lateness) : 0.0;
    const double Held = FMath::IsFinite(Envelope) ? FMath::Max(0.0, Envelope) : 0.0;
    if (Late >= Held) { return Late; }
    return Late + (Held - Late) * FMath::Exp(-FMath::Max(0.0, SecondsSinceUpdate) / LatenessReleaseSeconds);
}

LDObservedPresentation::FTiming LDObservedPresentation::Timing(double LatenessEnvelope, double PublishIntervalSeconds)
{
    FTiming Out;
    const double Late = FMath::IsFinite(LatenessEnvelope) ? FMath::Max(0.0, LatenessEnvelope) : 0.0;
    const double Interval = FMath::IsFinite(PublishIntervalSeconds) ? FMath::Max(0.0, PublishIntervalSeconds) : 0.0;
    Out.DelaySeconds = FMath::Clamp(Interval + DelayMarginSeconds + Late, MinDelaySeconds, MaxDelaySeconds);
    Out.StaleAfterSeconds = FMath::Max(FLDObservedFrameReceiver::StaleAfterSeconds, Out.DelaySeconds + StaleOverDelaySeconds);
    return Out;
}

double LDObservedPresentation::SlewDelay(double Applied, double Target, double DeltaSeconds)
{
    const double Step = FMath::Max(0.0, DeltaSeconds);
    return Target > Applied ? FMath::Min(Target, Applied + Step * DelayGrowthPerSecond)
        : FMath::Max(Target, Applied - Step * DelayShrinkPerSecond);
}

double LDObservedPresentation::TrackTimeout(double Applied, double Target, double DeltaSeconds)
{
    return Target >= Applied ? Target : FMath::Max(Target, Applied - FMath::Max(0.0, DeltaSeconds) * TimeoutShrinkPerSecond);
}

bool LDPropFade::Occludes(const FBox& Bounds, const FVector& Camera, const FVector& Target)
{
    if (!Bounds.IsValid) { return false; }
    // Decorations have no collision, so a body can stand inside or against the prop: the pole then runs through the body
    // on screen even though the body is not behind it.
    if (Target.Z >= Bounds.Min.Z && Target.Z <= Bounds.Max.Z)
    {
        const double OutX = FMath::Max3(Bounds.Min.X - Target.X, 0., Target.X - Bounds.Max.X);
        const double OutY = FMath::Max3(Bounds.Min.Y - Target.Y, 0., Target.Y - Bounds.Max.Y);
        if (OutX * OutX + OutY * OutY <= PierceRadiusCm * PierceRadiusCm) { return true; }
    }
    const FVector Span = Target - Camera;
    const double Length = Span.Size();
    if (Length <= TargetClearCm) { return false; }
    const FVector Direction = Span / Length;
    const FVector End = Target - Direction * TargetClearCm;
    return FMath::LineBoxIntersection(Bounds.ExpandBy(BoundsSlackCm), Camera, End, End - Camera);
}

FBox LDPropFade::Reach(const FVector& Camera, TConstArrayView<FVector> Targets)
{
    if (Targets.IsEmpty()) { return FBox(ForceInit); }
    FBox Box(Camera, Camera);
    for (const FVector& Target : Targets) { Box += Target; }
    return Box.ExpandBy(FMath::Max(BoundsSlackCm, PierceRadiusCm));
}

int32 LDPropFade::FSweep::Budget(int32 Total, float DeltaSeconds) const
{
    if (!bComplete) { return Total + 1; }
    return FMath::Max(32, FMath::CeilToInt32(Total * FMath::Max(DeltaSeconds, 0.f) / SweepSeconds));
}

bool LDPropFade::FSweep::Next(TConstArrayView<int32> Counts, int32& OutLevel, int32& OutIndex)
{
    while (Counts.IsValidIndex(Level) && Index >= Counts[Level]) { ++Level; Index = 0; }
    if (!Counts.IsValidIndex(Level)) { Level = Index = 0; bComplete = true; return false; }
    OutLevel = Level;
    OutIndex = Index++;
    return true;
}

float LDPropFade::StrengthFor(const FBox& Bounds)
{
    const FVector Size = Bounds.IsValid ? Bounds.GetSize() : FVector::ZeroVector;
    return Size.Z >= TallPropCm && FMath::Min(Size.X, Size.Y) >= BroadPropCm ? TallFadedStrength : FadedStrength;
}

float LDPropFade::Step(float Current, bool bOccluding, float DeltaSeconds)
{
    const float Move = FadePerSecond * FMath::Max(DeltaSeconds, 0.f);
    return bOccluding ? FMath::Min(1.f, Current + Move) : FMath::Max(0.f, Current - Move);
}

ALDObservedEntityProxy::ALDObservedEntityProxy()
{
    bReplicates = false;
    // The relay poses every proxy from its one presentation clock.
    PrimaryActorTick.bCanEverTick = false;
    SetActorEnableCollision(false);
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    Body = CreateDefaultSubobject<ULDHumanoidVisualComponent>(TEXT("ObservedBody"));
    Body->SetupAttachment(RootComponent);
    Blocker = CreateDefaultSubobject<UCapsuleComponent>(TEXT("BodyBlocker"));
    Blocker->SetupAttachment(RootComponent);
    Blocker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Blocker->SetGenerateOverlapEvents(false);
    Blocker->SetCanEverAffectNavigation(false);
    Blocker->SetHiddenInGame(true);
}

void ALDObservedEntityProxy::EnableBodyBlocking(float RadiusCm, float HalfHeightCm)
{
    // The root sits at the body's capsule centre (feet + half height), so the capsule centres on it.
    Blocker->SetCapsuleSize(RadiusCm, HalfHeightCm);
    Blocker->SetCollisionObjectType(ECC_Pawn);
    Blocker->SetCollisionResponseToAllChannels(ECR_Ignore);
    Blocker->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
    Blocker->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    SetActorEnableCollision(true);
}

bool ALDObservedEntityProxy::IsBodyBlocking() const
{
    return Blocker && Blocker->GetCollisionEnabled() != ECollisionEnabled::NoCollision;
}

void ALDObservedEntityProxy::Apply(const FLDObservedUnitView& Unit, double VisualHalfHeightCm, double SampledAtServerSec)
{
    LDObservedPresentation::FPoint Point;
    Point.At = SampledAtServerSec;
    Point.Location = FVector(Unit.FeetCm) + FVector(0, 0, VisualHalfHeightCm);
    Point.Yaw = float(Unit.Facing) / 65536.f * 360.f;
    if (!bPlaced || ObservedHandle != Unit.Handle || ObservedEpoch != Unit.ObservationEpoch)
    {
        // A new visibility interval starts from its own first sample; nothing of an earlier interval is kept.
        Points.Reset();
        Facts.Reset();
        ShownAt = -1;
        SetActorLocationAndRotation(Point.Location, FRotator(0, Point.Yaw, 0));
        bPlaced = true;
    }
    else if (!Points.IsEmpty() && SampledAtServerSec <= Points.Last().At) { return; }
    Point.bSnap = Points.IsEmpty() || FVector::DistSquared(Points.Last().Location, Point.Location) > FMath::Square(LDObservedPresentation::SnapCm);
    Points.Add(Point);
    LDObservedPresentation::FFacts& Shown = Facts.AddDefaulted_GetRef();
    Shown.Cue = Unit.ActionCue;
    Shown.CueAgeMs = Unit.ActionCueAgeMs;
    Shown.CueImpactDelayMs = Unit.ActionCueImpactDelayMs;
    Shown.ActionAbility = Unit.ActionAbility;
    Shown.bCharging = Unit.bCharging;
    Shown.ChargeAgeMs = Unit.ChargeAgeMs;
    Shown.bOverdriveCast = Unit.bOverdriveCast;
    Shown.RecentReaction = Unit.RecentReaction;
    Shown.RecentReactionElement = Unit.RecentReactionElement;
    Shown.RecentReactionAgePlusOneMs = Unit.RecentReactionAgePlusOneMs;
    Shown.HitLook = Unit.HitLook;
    Shown.RecentHitAgePlusOneMs = Unit.RecentHitAgePlusOneMs;
    Shown.RecentHitId = Unit.RecentHitId;
    Shown.HitFlags = Unit.HitFlags;
    Shown.Statuses = Unit.Statuses;
    // A presentation clock that stalls keeps only a bounded queue: the oldest samples are dropped first.
    while (Points.Num() > LDObservedPresentation::HistorySamples) { Points.RemoveAt(0); Facts.RemoveAt(0); }
    ObservedHandle = Unit.Handle;
    ObservedEpoch = Unit.ObservationEpoch;
    const bool bCamp = Unit.Kind == ELDObservedUnitKind::CampMember;
    // Contenders wear their stable roster-slot tint; camp NPCs have their own look and never a contender colour.
    const uint16 NextLook = bCamp ? uint16(0x100 | Unit.CreatureId) : uint16(Unit.Presentation);
    if (Look != NextLook)
    {
        Look = NextLook;
        if (bCamp) { Body->ApplyMonsterLook(Unit.CreatureId); }
        else { Body->ApplyPalette(Unit.Presentation); }
    }
    // Body height comes from the art catalog's mesh transform (Combat's visual); the proxy never rescales it.
}

void ALDObservedEntityProxy::Present(double AtServerSec, float Delta)
{
    if (Points.IsEmpty()) { return; }
    const LDObservedPresentation::FPose Pose = LDObservedPresentation::Sample(Points, AtServerSec);
    SetActorLocationAndRotation(Pose.Location, FRotator(0, Pose.Yaw, 0));
    if (Pose.Reached != INDEX_NONE && Points[Pose.Reached].At > ShownAt)
    {
        // The same cue byte as the owner's own body: action pose plus one marker shape per public status, shown when
        // the clock reaches the sample that carried them. An action's age is its age in that sample plus the time the
        // clock has run past it, so its timeline stays in step with the delayed pose.
        const LDObservedPresentation::FFacts& Reached = Facts[Pose.Reached];
        const float Age = Reached.Cue == ELDObservedActionCue::None ? -1.f
            : float(double(Reached.CueAgeMs) / 1000.0 + FMath::Max(0.0, AtServerSec - Points[Pose.Reached].At));
        Body->SetCue(ALDCharacter::PresentationCue(Reached.Cue, Reached.Statuses), Age,
            float(Reached.CueImpactDelayMs) / 1000.f, ALDCharacter::PresentationKnockback(Reached.Statuses));
        Body->SetCastAbility(Reached.Cue == ELDObservedActionCue::Cast ? Reached.ActionAbility : 0);
        Body->SetCharging(Reached.bCharging,
            float(double(Reached.ChargeAgeMs) / 1000.0 + FMath::Max(0.0, AtServerSec - Points[Pose.Reached].At)));
        Body->SetOverdriveCast(Reached.Cue == ELDObservedActionCue::Cast && Reached.bOverdriveCast);
        if (Reached.RecentReactionAgePlusOneMs)
        {
            const double ReactionAge = double(Reached.RecentReactionAgePlusOneMs - 1) / 1000.0;
            Body->SetReaction(Reached.RecentReaction, Reached.RecentReactionElement,
                float(ReactionAge + FMath::Max(0.0, AtServerSec - Points[Pose.Reached].At)), Points[Pose.Reached].At - ReactionAge);
        }
        if (Reached.RecentHitAgePlusOneMs)
        { Body->SetHitReaction(LDObservedPresentation::HitAgeSeconds(Reached.RecentHitAgePlusOneMs,
            AtServerSec-Points[Pose.Reached].At), Reached.HitLook, Reached.HitFlags, Reached.RecentHitId,
            FMath::RoundToInt64(Points[Pose.Reached].At * 1000.0) - (Reached.RecentHitAgePlusOneMs - 1)); }
        ShownAt = Points[Pose.Reached].At;
    }
    if (Pose.Reached > 0)
    {
        Points.RemoveAt(0, Pose.Reached);
        Facts.RemoveAt(0, Pose.Reached);
    }
    Body->Animate(Pose.Velocity.GetClampedToMaxSize(1000.0), Delta);
}

void ALDObservedEntityProxy::PlayDeathFall() const
{
    if (IsValid(Body) && !ALDBodyPuppet::SpawnFall(*Body))
    {
        UE_LOG(LogLDWorld, Warning, TEXT("LD_OBSERVATION_DEATH_FALL_UNAVAILABLE The observed death is shown without a falling body."));
    }
}

bool ALDObservedEntityProxy::PresentImmediateHit(float AgeSeconds, ELDObservedHitLook HitLook, uint8 HitFlags,
    const FGuid& HitId, int64 HitAtMs)
{
    if (bPlaced && IsValid(Body) && !IsHidden())
    {
        Body->SetHitReaction(AgeSeconds, HitLook, HitFlags, HitId, HitAtMs);
        return true;
    }
    return false;
}

bool ALDObservedEntityProxy::TracePresentedModel(const FVector& Start, const FVector& End, double& OutFraction) const
{
    return bPlaced && IsValid(Body) && Body->TracePresentedModel(Start, End, OutFraction);
}

bool ALDObservedEntityProxy::PresentedPickVolume(FVector& OutFoot, FVector& OutTop, double& OutRadiusCm) const
{
    return bPlaced && IsValid(Body) && Body->PresentedPickVolume(OutFoot, OutTop, OutRadiusCm);
}

ALDOwnBodyMarker::ALDOwnBodyMarker()
{
    bReplicates = false;
    PrimaryActorTick.bCanEverTick = false;
    SetActorEnableCollision(false);
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder"));
    Rim = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("OwnRingRim"));
    Core = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("OwnRingCore"));
    for (UStaticMeshComponent* Disc : {Rim.Get(), Core.Get()})
    {
        Disc->SetupAttachment(RootComponent);
        Disc->SetStaticMesh(Cylinder.Object);
        Disc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Disc->SetCanEverAffectNavigation(false);
        Disc->SetCastShadow(false);
    }
    // A bright rim around a disc in the player's own roster tint, just above the ground and below status rings.
    Rim->SetRelativeLocation(FVector(0, 0, .5));
    Rim->SetRelativeScale3D(FVector(1.35, 1.35, .01));
    Core->SetRelativeLocation(FVector(0, 0, .8));
    Core->SetRelativeScale3D(FVector(1.1, 1.1, .012));
    SetActorHiddenInGame(true);
}

void ALDOwnBodyMarker::Track(APawn* Body, uint8 Slot)
{
    if (!Body)
    {
        if (Tracked.IsValid()) { DetachFromActor(FDetachmentTransformRules::KeepWorldTransform); }
        Tracked.Reset();
        SetActorHiddenInGame(true);
        return;
    }
    if (Tracked.Get() != Body)
    {
        const UCapsuleComponent* Capsule = Body->FindComponentByClass<UCapsuleComponent>();
        const auto* Character=Cast<ALDCharacter>(Body);
        AttachToComponent(Character ? Character->Body.Get() : Body->GetRootComponent(),
            FAttachmentTransformRules::SnapToTargetNotIncludingScale);
        SetActorRelativeLocation(FVector(0, 0, -(Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.0)));
        Tracked = Body;
    }
    if (TintSlot != Slot)
    {
        UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Lockdown/Generated/Materials/M_Character.M_Character"));
        if (!ensureMsgf(Material, TEXT("Character material missing; rerun asset bootstrap"))) { return; }
        UMaterialInstanceDynamic* RimTint = UMaterialInstanceDynamic::Create(Material, this);
        RimTint->SetVectorParameterValue(TEXT("Tint"), FLinearColor(1.f, .85f, .2f));
        Rim->SetMaterial(0, RimTint);
        UMaterialInstanceDynamic* CoreTint = UMaterialInstanceDynamic::Create(Material, this);
        CoreTint->SetVectorParameterValue(TEXT("Tint"), ULDHumanoidVisualComponent::ContenderTint(Slot));
        Core->SetMaterial(0, CoreTint);
        TintSlot = Slot;
    }
    SetActorHiddenInGame(false);
}

ALDObservedGroundProxy::ALDObservedGroundProxy()
{
    bReplicates = false;
    PrimaryActorTick.bCanEverTick = false;
    SetActorEnableCollision(false);
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Box(TEXT("/Engine/BasicShapes/Cube"));
    for (const TCHAR* Name : {TEXT("GroundPattern"), TEXT("GroundRim")})
    {
        UInstancedStaticMeshComponent* Mesh = CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
        Mesh->SetupAttachment(RootComponent);
        Mesh->SetStaticMesh(Box.Object);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetCanEverAffectNavigation(false);
        Mesh->SetCastShadow(false);
        (FCString::Strcmp(Name, TEXT("GroundRim")) == 0 ? Rim : Lines) = Mesh;
    }
}

FLinearColor ALDObservedGroundProxy::KindColor(uint8 Kind)
{
    // Spec §8.1/§8.3: each ground state in its element's colour (the ice and puddle blues differ in value).
    switch (static_cast<ELDGroundStateKind>(Kind))
    {
    case ELDGroundStateKind::Puddle: return FLinearColor(.08f,.42f,1.f);
    case ELDGroundStateKind::Ice: return FLinearColor(.55f,.98f,1.f);
    case ELDGroundStateKind::Fire: return FLinearColor(1.f,.3f,.04f);
    case ELDGroundStateKind::Miasma: return FLinearColor(.4f,.85f,.12f);
    case ELDGroundStateKind::Field: return FLinearColor(.66f,.3f,1.f);
    default: return FLinearColor::White;
    }
}

void ALDObservedGroundProxy::Outline(const FLDObservedGroundState& Ground, TArray<FVector>& Out)
{
    Out.Reset();
    const double R = Ground.RadiusCm;
    const auto Arc = [&Out](const FVector& Centre, double Radius, double From, double To, int32 Steps)
    {
        for (int32 Step = 0; Step <= Steps; ++Step)
        {
            const double Angle = FMath::DegreesToRadians(FMath::Lerp(From, To, double(Step) / Steps));
            Out.Add(Centre + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0));
        }
    };
    if (Ground.Shape == ELDObservedGroundShape::Band)
    {
        const double H = Ground.ExtentCm;
        Arc(FVector(H, 0, 0), R, -90., 90., 6);
        Arc(FVector(-H, 0, 0), R, 90., 270., 6);
        // A copy: adding an element of the array to itself may reallocate under the reference.
        const FVector First = Out[0];
        Out.Add(First);
    }
    else if (Ground.Shape == ELDObservedGroundShape::Sector)
    {
        const double Half = Ground.ExtentCm * .5;
        Out.Add(FVector::ZeroVector);
        Arc(FVector::ZeroVector, R, -Half, Half, 12);
        Out.Add(FVector::ZeroVector);
    }
    else { Arc(FVector::ZeroVector, R, 0., 360., 20); }
}

void ALDObservedGroundProxy::Pattern(const FLDObservedGroundState& Ground, TArray<TPair<FVector,FVector>>& Out)
{
    Out.Reset();
    // A sector's pattern sits mid-cone and a band's along its middle; a circle's around its centre.
    const double R = Ground.RadiusCm;
    const FVector C = Ground.Shape == ELDObservedGroundShape::Sector ? FVector(R * .55, 0, 0) : FVector::ZeroVector;
    const double S = Ground.Shape == ELDObservedGroundShape::Sector ? R * .35 : R * .7;
    const auto Seg = [&Out, &C](const FVector& A, const FVector& B) { Out.Emplace(C + A, C + B); };
    const auto Ring = [&Seg](const FVector& At, double Radius, int32 Steps)
    {
        for (int32 Step = 0; Step < Steps; ++Step)
        {
            const double A0 = 2. * PI * Step / Steps, A1 = 2. * PI * (Step + 1) / Steps;
            Seg(At + FVector(FMath::Cos(A0), FMath::Sin(A0), 0) * Radius, At + FVector(FMath::Cos(A1), FMath::Sin(A1), 0) * Radius);
        }
    };
    switch (static_cast<ELDGroundStateKind>(Ground.Kind))
    {
    case ELDGroundStateKind::Puddle:
        // Two ripples.
        Ring(FVector::ZeroVector, S * .55, 10);
        Ring(FVector::ZeroVector, S * .95, 14);
        break;
    case ELDGroundStateKind::Ice:
        // Parallel cracks across.
        for (int32 Line = -2; Line <= 2; ++Line)
        { Seg(FVector(-S, Line * S * .35 - S * .2, 0), FVector(S, Line * S * .35 + S * .2, 0)); }
        break;
    case ELDGroundStateKind::Fire:
        // A ring of flame teeth.
        for (int32 Tooth = 0; Tooth < 8; ++Tooth)
        {
            const double A0 = 2. * PI * Tooth / 8, A1 = 2. * PI * (Tooth + .5) / 8, A2 = 2. * PI * (Tooth + 1) / 8;
            const FVector P0(FMath::Cos(A0) * S * .6, FMath::Sin(A0) * S * .6, 0), P1(FMath::Cos(A1) * S, FMath::Sin(A1) * S, 0);
            const FVector P2(FMath::Cos(A2) * S * .6, FMath::Sin(A2) * S * .6, 0);
            Seg(P0, P1);
            Seg(P1, P2);
        }
        break;
    case ELDGroundStateKind::Miasma:
        // Bubbles.
        for (const FVector& At : {FVector(0, 0, 0), FVector(S * .55, S * .25, 0), FVector(-S * .5, S * .4, 0), FVector(-S * .2, -S * .55, 0),
            FVector(S * .4, -S * .5, 0)})
        { Ring(At, S * .18, 6); }
        break;
    case ELDGroundStateKind::Field:
        // A lightning zigzag.
        Seg(FVector(-S, -S * .2, 0), FVector(-S * .35, S * .35, 0));
        Seg(FVector(-S * .35, S * .35, 0), FVector(0, -S * .3, 0));
        Seg(FVector(0, -S * .3, 0), FVector(S * .35, S * .35, 0));
        Seg(FVector(S * .35, S * .35, 0), FVector(S, -S * .2, 0));
        break;
    default: break;
    }
}

FTransform ALDObservedGroundProxy::SegmentTransform(const FVector& A, const FVector& B, double Thickness)
{
    const FVector Delta = B - A;
    return FTransform(Delta.Rotation(), (A + B) * .5 + FVector(0, 0, 3), FVector(FMath::Max(Delta.Size(), 1.) / 100., Thickness / 100., .02));
}

void ALDObservedGroundProxy::Apply(const FLDObservedGroundState& Ground, double SampledAtServerSec)
{
    const bool bShape = !bShown || Ground.Kind != Shown.Kind || Ground.Shape != Shown.Shape || Ground.RadiusCm != Shown.RadiusCm
        || Ground.ExtentCm != Shown.ExtentCm || Ground.OwnerSeat != Shown.OwnerSeat;
    SetActorLocationAndRotation(Ground.CentreCm, FRotator(0, float(Ground.Heading) / 256.f * 360.f, 0));
    ExpiresAtServerSec = SampledAtServerSec + double(Ground.RemainingMs) / 1000.;
    LifeSeconds = FMath::Max(double(Ground.TotalMs) / 1000., .05);
    if (bShape)
    {
        if (!KindTint || !SeatTint)
        {
            UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Lockdown/Generated/Materials/M_Character.M_Character"));
            if (!ensureMsgf(Material, TEXT("Character material missing; rerun asset bootstrap"))) { return; }
            KindTint = UMaterialInstanceDynamic::Create(Material, this);
            SeatTint = UMaterialInstanceDynamic::Create(Material, this);
            Lines->SetMaterial(0, KindTint);
            Rim->SetMaterial(0, SeatTint);
        }
        KindTint->SetVectorParameterValue(TEXT("Tint"), KindColor(Ground.Kind));
        SeatTint->SetVectorParameterValue(TEXT("Tint"), Ground.OwnerSeat < LDObservation::MaxPresentations
            ? ULDHumanoidVisualComponent::ContenderTint(Ground.OwnerSeat) : FLinearColor(.6f, .6f, .6f));
        TArray<TPair<FVector,FVector>> Segments;
        Pattern(Ground, Segments);
        Lines->ClearInstances();
        for (const TPair<FVector,FVector>& Segment : Segments) { Lines->AddInstance(SegmentTransform(Segment.Key, Segment.Value, 7.)); }
        Outline(Ground, RimPoints);
        ShownOutline = -1;
    }
    // A freshly laid state (not one walked into sight) appears with its burst; the burst leaves with the proxy.
    const bool bFresh = !bShown && Ground.TotalMs >= Ground.RemainingMs && Ground.TotalMs - Ground.RemainingMs < 400;
    Shown = Ground;
    bShown = true;
    if (bShape) { ShowParticles(); }
    if (bFresh)
    {
        LDHitVfx::EBurst Kind;
        FLinearColor Tint;
        ULDImpactFxSubsystem* Impacts = GetWorld() ? GetWorld()->GetSubsystem<ULDImpactFxSubsystem>() : nullptr;
        if (Impacts && LDHitVfx::GroundAppearBurstOf(Ground.Kind, Kind, Tint))
        { Impacts->Kit(GetActorLocation() + FVector(0, 0, 20.f), LDHitVfx::BurstOf(Kind, Tint, Impacts->LocalEffectScale()), *this); }
    }
    Present(SampledAtServerSec);
}

void ALDObservedGroundProxy::ShowParticles()
{
    ULDImpactFxSubsystem* Impacts = GetWorld() ? GetWorld()->GetSubsystem<ULDImpactFxSubsystem>() : nullptr;
    if (!Impacts || !bShown) { return; }
    ParticleScale = Impacts->LocalEffectScale();
    // A circle fills its radius and a band keeps the particles inside its narrower half-extent. A sector starts at its
    // apex and ExtentCm is its angle: the particles fill the disc inscribed between its two edges and its arc.
    float Radius = Shown.RadiusCm * .9f;
    FVector Offset = FVector::ZeroVector;
    if (Shown.Shape == ELDObservedGroundShape::Sector)
    {
        const float Sine = FMath::Sin(FMath::DegreesToRadians(FMath::Clamp(Shown.ExtentCm * .5f, 5.f, 90.f)));
        Offset = FVector(Shown.RadiusCm / (1.f + Sine), 0, 0);
        Radius = FMath::Max(Offset.X * Sine * .9f, 40.f);
    }
    else if (Shown.Shape != ELDObservedGroundShape::Circle)
    { Radius = FMath::Max(FMath::Min(Shown.RadiusCm, Shown.ExtentCm > 0.f ? Shown.ExtentCm : Shown.RadiusCm), 40.f); }
    const LDHitVfx::FZone Zone = LDHitVfx::GroundZoneOf(Shown.Kind, Radius, ParticleScale);
    Particles = Impacts->ShowZone(*RootComponent, Particles, Zone, Radius, GetActorRotation().RotateVector(Zone.Velocity), Offset);
}

void ALDObservedGroundProxy::EndPlay(const EEndPlayReason::Type Reason)
{
    if (ULDImpactFxSubsystem* Impacts = GetWorld() ? GetWorld()->GetSubsystem<ULDImpactFxSubsystem>() : nullptr) { Impacts->ClearForActor(*this); }
    Super::EndPlay(Reason);
}

void ALDObservedGroundProxy::Present(double AtServerSec)
{
    // The reduced-effects setting changed: rebuild the particles at the new density.
    if (const ULDImpactFxSubsystem* Impacts = GetWorld() ? GetWorld()->GetSubsystem<ULDImpactFxSubsystem>() : nullptr;
        Impacts && bShown && ParticleScale >= 0.f && Impacts->LocalEffectScale() != ParticleScale) { ShowParticles(); }
    if (!bShown || RimPoints.Num() < 2) { return; }
    // The outline drains with the remaining life (spec §12): the first Remaining/Life of its segments stay.
    const double Left = FMath::Clamp((ExpiresAtServerSec - AtServerSec) / LifeSeconds, 0., 1.);
    const int32 Segments = RimPoints.Num() - 1;
    const int32 Keep = FMath::Clamp(FMath::CeilToInt(Left * Segments), 0, Segments);
    if (Keep == ShownOutline) { return; }
    ShownOutline = Keep;
    Rim->ClearInstances();
    for (int32 Index = 0; Index < Keep; ++Index) { Rim->AddInstance(SegmentTransform(RimPoints[Index], RimPoints[Index + 1], 5.)); }
}

ALDObservedProjectileProxy::ALDObservedProjectileProxy()
{
    bReplicates = false;
    // Posed by the relay on the same presentation clock as the units.
    PrimaryActorTick.bCanEverTick = false;
    SetActorEnableCollision(false);
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Box(TEXT("/Engine/BasicShapes/Cube"));
    BoxMesh=Box.Object;
    CylinderMesh=Cylinder.Object;
    Chalk = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ObservedChalk"));
    Chalk->SetupAttachment(RootComponent);
    Chalk->SetStaticMesh(Cylinder.Object);
    Chalk->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Chalk->SetCanEverAffectNavigation(false);
    // An exaggerated chalk stub lying along the flight direction (+X), readable from the gameplay camera at 720p.
    Chalk->SetRelativeRotation(FRotator(90, 0, 0));
    Chalk->SetRelativeScale3D(FVector(.22, .22, .5));
    Accent=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ObservedSkillAccent"));
    Accent->SetupAttachment(RootComponent);
    Accent->SetStaticMesh(Box.Object);
    Accent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Accent->SetCanEverAffectNavigation(false);
    Accent->SetVisibility(false);
    // Chalk dust strung out behind the flight from the chalk's tail (its 50 cm stub is centred on the root); the proxy
    // faces +X along its heading.
    Trail = CreateDefaultSubobject<ULDFlightTrailComponent>(TEXT("Trail"));
    Trail->SetupAttachment(RootComponent);
    Trail->SetRelativeLocation(FVector(-25, 0, 0));
}

void ALDObservedProjectileProxy::ApplyLook(const FLDObservedProjectileView& Effect)
{
    const bool bFlight=IsObservedFlight(Effect.Look);
    const bool bCone=Effect.Look==ELDObservedProjectileLook::SpeechCone || Effect.Look==ELDObservedProjectileLook::ConeWarning;
    const bool bBook=Effect.Look==ELDObservedProjectileLook::BookShadow;
    const bool bArea=IsObservedArea(Effect.Look);
    const bool bForwardLine=Effect.Look==ELDObservedProjectileLook::LineWarning;
    const bool bRoad=Effect.Look==ELDObservedProjectileLook::Road || Effect.Look==ELDObservedProjectileLook::LineWarning
        || Effect.Look==ELDObservedProjectileLook::Wind;
    const FLinearColor Color=Effect.bOwn ? FMath::Lerp(ObservedEffectColor(Effect.Look),FLinearColor(.08f,.85f,.6f),.5f)
        : ObservedEffectColor(Effect.Look);
    ShownLook=Effect.Look;
    ShownRadius=Effect.RadiusCm; ShownInnerRadius=Effect.InnerRadiusCm; ShownAngle=Effect.AngleDegrees;
    ShownLength=Effect.LengthCm;bShownOwn=Effect.bOwn;
    ShownColor=Color;
    ShownHeading=FRotator(0,float(Effect.Heading)/65536.f*360.f,0);
    if (ULDImpactFxSubsystem* Impacts=GetWorld()?GetWorld()->GetSubsystem<ULDImpactFxSubsystem>():nullptr)
    {
        USceneComponent* Anchor=bFlight ? static_cast<USceneComponent*>(Trail) : RootComponent.Get();
        ParticleScale=Impacts->LocalEffectScale();
        Particles=Impacts->ShowEffect(*Anchor,Particles,Effect.Look,Effect.RadiusCm,ShownHeading);
    }
    if (EffectTint) { EffectTint->SetVectorParameterValue(TEXT("Tint"),Color); }
    if (AccentTint) { AccentTint->SetVectorParameterValue(TEXT("Tint"),Color); }
    Trail->SetReturning(IsObservedReturn(Effect.Look));
    Chalk->SetStaticMesh(bFlight ? CylinderMesh : BoxMesh);
    Chalk->SetRelativeLocation(FVector::ZeroVector);
    Chalk->SetRelativeRotation(bFlight ? FRotator(90,0,0) : FRotator::ZeroRotator);
    Chalk->SetRelativeScale3D(bFlight ? FVector(.22,.22,.5)
        : bCone ? FVector(.4,.75,.04) : bArea ? FVector(.38,.38,.04) : bBook ? FVector(1.7,1.3,.16) : FVector(1.1,.85,.025));
    if (bBook) { Chalk->SetRelativeLocation(FVector(0,0,95)); }
    if (bCone) { Chalk->SetRelativeLocation(FVector(Effect.RadiusCm*.35,0,100)); }
    if (bArea) { Chalk->SetRelativeLocation(FVector(0,0,7)); }
    if(bRoad)
    {
        Chalk->SetRelativeLocation(FVector(bForwardLine?Effect.LengthCm*.5:0,0,1));
        Chalk->SetRelativeScale3D(FVector(Effect.LengthCm/100.,Effect.RadiusCm*2./100.,.012));
    }
    // N4: a bag or a bag wall stands as a solid box that blocks bodies like the server's blocker (owner prediction agrees
    // with the authority while the obstacle is seen); every other look never collides.
    const bool bBag=Effect.Look==ELDObservedProjectileLook::Bag;
    const bool bWall=Effect.Look==ELDObservedProjectileLook::BagWall;
    Chalk->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    if (bBag || bWall)
    {
        const double Height=ALDCombatBagBlocker::HeightCm;
        Chalk->SetStaticMesh(BoxMesh);
        Chalk->SetRelativeRotation(FRotator::ZeroRotator);
        Chalk->SetRelativeLocation(FVector(0,0,Height*.5));
        Chalk->SetRelativeScale3D(bBag ? FVector(Effect.RadiusCm*2./100.,Effect.RadiusCm*2./100.,Height/100.)
            : FVector(Effect.LengthCm/100.,Effect.RadiusCm*2./100.,Height/100.));
        Chalk->SetCollisionObjectType(ECC_WorldDynamic);
        Chalk->SetCollisionResponseToAllChannels(ECR_Ignore);
        Chalk->SetCollisionResponseToChannel(ECC_Pawn,ECR_Block);
        Chalk->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Accent->SetVisibility(false);
        Trail->SetVisibility(false,true);
        for (const auto& Segment : ShapeLines) { Segment->SetVisibility(false); }
        return;
    }
    Accent->SetVisibility(Effect.Look!=ELDObservedProjectileLook::Chalk);
    Accent->SetRelativeRotation(FRotator::ZeroRotator);
    Accent->SetRelativeLocation(bFlight ? FVector(20,0,-12) : bArea ? FVector(0,0,14) : bCone ? FVector(Effect.RadiusCm*.65,0,110)
        : bBook ? FVector(0,0,105) : FVector(0,0,3));
    Accent->SetRelativeScale3D(bFlight ? FVector(.3,.2,.16) : bArea ? FVector(.34,.34,.06) : bCone ? FVector(.5,.95,.04)
        : bBook ? FVector(1.5,1.15,.06) : FVector(.8,.12,.02));
    Trail->SetVisibility(bFlight,true);
    if(bRoad){Accent->SetVisibility(false);}
    for (const auto& Segment : ShapeLines) { Segment->SetVisibility(false); }
    if (bFlight) { return; }
    int32 Used=0;
    const auto Line=[&](const FVector& A,const FVector& B)
    {
        UStaticMeshComponent* Segment=nullptr;
        if (Used==ShapeLines.Num())
        {
            Segment=NewObject<UStaticMeshComponent>(this);
            Segment->SetStaticMesh(BoxMesh);
            Segment->SetupAttachment(RootComponent);
            Segment->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Segment->SetCanEverAffectNavigation(false);
            if (EffectTint) { Segment->SetMaterial(0,EffectTint); }
            Segment->RegisterComponent();
            ShapeLines.Add(Segment);
        }
        else { Segment=ShapeLines[Used]; }
        ++Used;
        const FVector Delta=B-A;
        Segment->SetRelativeLocation((A+B)*.5);
        Segment->SetRelativeRotation(Delta.Rotation());
        Segment->SetRelativeScale3D(FVector(Delta.Size()/100.,.04,.02));
        Segment->SetVisibility(true);
    };
    constexpr int32 Segments=16;
    const auto Arc=[&](double Radius,double Begin,double Span)
    {
        FVector Previous(Radius*FMath::Cos(Begin),Radius*FMath::Sin(Begin),2);
        for (int32 Index=1; Index<=Segments; ++Index)
        {
            const double Angle=Begin+Span*Index/Segments;
            const FVector Next(Radius*FMath::Cos(Angle),Radius*FMath::Sin(Angle),2);
            Line(Previous,Next);
            Previous=Next;
        }
    };
    if(bRoad)
    {
        const double From=bForwardLine?0:-Effect.LengthCm*.5,To=From+Effect.LengthCm,Width=Effect.RadiusCm;
        Line(FVector(From,-Width,2),FVector(To,-Width,2));
        Line(FVector(From,Width,2),FVector(To,Width,2));
        Line(FVector(From,-Width,2),FVector(From,Width,2));
        Line(FVector(To,-Width,2),FVector(To,Width,2));
    }
    else if (bCone)
    {
        const double Half=FMath::DegreesToRadians(double(Effect.AngleDegrees)*.5);
        Arc(Effect.RadiusCm,-Half,Half*2);
        Line(FVector(0,0,2),FVector(Effect.RadiusCm*FMath::Cos(Half),Effect.RadiusCm*FMath::Sin(Half),2));
        Line(FVector(0,0,2),FVector(Effect.RadiusCm*FMath::Cos(Half),-Effect.RadiusCm*FMath::Sin(Half),2));
    }
    else
    {
        Arc(Effect.RadiusCm,0,2.*PI);
        if (Effect.InnerRadiusCm>0) { Arc(Effect.InnerRadiusCm,0,2.*PI); }
    }
}

void ALDObservedProjectileProxy::Apply(const FLDObservedProjectileView& Projectile, double SampledAtServerSec)
{
    const bool bFlight=IsObservedFlight(Projectile.Look);
    LDObservedPresentation::FPoint Point;
    Point.At = SampledAtServerSec;
    Point.Location = FVector(Projectile.PositionCm) + FVector(0, 0, bFlight ? 100 : 4);
    Point.Yaw = float(Projectile.Heading) / 65536.f * 360.f;
    if (!bPlaced || ObservedHandle != Projectile.Handle)
    {
        Points.Reset();
        SetActorLocationAndRotation(Point.Location, FRotator(0, Point.Yaw, 0));
        bPlaced = true;
        ShownLook = Projectile.Look;
        PlayBurst(false);
        if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Lockdown/Generated/Materials/M_Character.M_Character")))
        {
            EffectTint = UMaterialInstanceDynamic::Create(Material, this);
            Chalk->SetMaterial(0, EffectTint);
            AccentTint = UMaterialInstanceDynamic::Create(Material, this);
            AccentTint->SetVectorParameterValue(TEXT("Tint"),FLinearColor(.95f,.95f,.9f));
            Accent->SetMaterial(0,AccentTint);
        }
        else { ensureMsgf(false, TEXT("Character material missing; rerun asset bootstrap")); }
    }
    else if (!Points.IsEmpty() && SampledAtServerSec <= Points.Last().At) { return; }
    WarningAtServerSec=Projectile.WarningRemainingMs>0
        ? SampledAtServerSec+double(Projectile.WarningRemainingMs)/1000. : -1.;
    if (ShownRadius<0 || ShownLook!=Projectile.Look || ShownRadius!=Projectile.RadiusCm
        || ShownInnerRadius!=Projectile.InnerRadiusCm || ShownAngle!=Projectile.AngleDegrees
        || ShownLength!=Projectile.LengthCm || bShownOwn!=Projectile.bOwn) { ApplyLook(Projectile); }
    ObservedHandle = Projectile.Handle;
    EndedAt = -1;
    Point.bSnap = Points.IsEmpty();
    Points.Add(Point);
    while (Points.Num() > LDObservedPresentation::HistorySamples) { Points.RemoveAt(0); }
}

void ALDObservedProjectileProxy::Present(double AtServerSec)
{
    if (Points.IsEmpty()) { return; }
    // The reduced-effects setting changed: rebuild the particles at the new density and width.
    if (ULDImpactFxSubsystem* Impacts=GetWorld()?GetWorld()->GetSubsystem<ULDImpactFxSubsystem>():nullptr;
        Impacts && ParticleScale>=0.f && Impacts->LocalEffectScale()!=ParticleScale)
    {
        USceneComponent* Anchor=IsObservedFlight(ShownLook) ? static_cast<USceneComponent*>(Trail) : RootComponent.Get();
        ParticleScale=Impacts->LocalEffectScale();
        Particles=Impacts->ShowEffect(*Anchor,Particles,ShownLook,ShownRadius,ShownHeading);
    }
    const LDObservedPresentation::FPose Pose = LDObservedPresentation::Sample(Points, AtServerSec);
    SetActorLocationAndRotation(Pose.Location, FRotator(0, Pose.Yaw, 0));
    // A directional zone (a cone, the sweeping fan's wind) blows along the presented heading as it turns.
    ShownHeading=FRotator(0, Pose.Yaw, 0);
    if (IsValid(Particles) && ParticleScale>0.f && !IsObservedFlight(ShownLook))
    {
        const LDHitVfx::FZone Zone=LDHitVfx::ZoneOf(ShownLook,ShownRadius,ParticleScale);
        if (Zone.bShow && !FVector2D(Zone.Velocity).IsNearlyZero())
        { Particles->SetVariableVec3(TEXT("User.Velocity"),ShownHeading.RotateVector(Zone.Velocity)); }
    }
    if (AccentTint)
    {
        const double Remaining=WarningAtServerSec>AtServerSec ? WarningAtServerSec-AtServerSec : 0.;
        const float Pulse=Remaining>0. ? .3f+.55f*(.5f+.5f*FMath::Sin(float(AtServerSec)*(Remaining<1. ? 14.f : 7.f))) : 0.f;
        AccentTint->SetVectorParameterValue(TEXT("Tint"),FMath::Lerp(ShownColor,FLinearColor::White,Pulse));
    }
    if (Pose.Reached > 0) { Points.RemoveAt(0, Pose.Reached); }
}

void ALDObservedProjectileProxy::PlayBurst(bool bEnd)
{
    if (bEnd && (!bPlaced || !bNaturalEnd)) { return; }
    LDHitVfx::EBurst Kind;
    FLinearColor Tint;
    ULDImpactFxSubsystem* Impacts=GetWorld()?GetWorld()->GetSubsystem<ULDImpactFxSubsystem>():nullptr;
    if (!Impacts || (!bEnd && IsHidden()) || !(bEnd ? LDHitVfx::EndBurstOf(ShownLook,Kind,Tint) : LDHitVfx::AppearBurstOf(ShownLook,Kind,Tint))) { return; }
    const LDHitVfx::FBurst Burst=LDHitVfx::BurstOf(Kind,Tint,Impacts->LocalEffectScale());
    if (bEnd) { Impacts->Kit(GetActorLocation(),Burst); }
    else { Impacts->Kit(GetActorLocation(),Burst,*this); }
}

void ALDObservedProjectileProxy::EndPlay(const EEndPlayReason::Type Reason)
{
    // Its launch or landing burst leaves with it; only a natural end's burst (PlayBurst(true)) plays on alone.
    if (ULDImpactFxSubsystem* Impacts=GetWorld()?GetWorld()->GetSubsystem<ULDImpactFxSubsystem>():nullptr) { Impacts->ClearForActor(*this); }
    Super::EndPlay(Reason);
}

bool ALDObservedProjectileProxy::LastPath(FVector& Last, FVector& Next) const
{
    if (Points.IsEmpty()) { return false; }
    Last=Points.Last().Location;
    Next=Points.Num()>1 ? Last+(Last-Points[Points.Num()-2].Location) : Last;
    return true;
}

void ALDObservedProjectileProxy::End(double LocalSeconds, bool bInNaturalEnd)
{
    if (EndedAt < 0) { EndedAt = LocalSeconds; bNaturalEnd = bInNaturalEnd; }
    if (!IsObservedFlight(ShownLook)) { SetActorHiddenInGame(true); }
}

bool ALDObservedProjectileProxy::IsFinished(double AtServerSec, double LocalSeconds) const
{
    return EndedAt >= 0 && (!IsObservedFlight(ShownLook)
        || Points.IsEmpty() || AtServerSec >= Points.Last().At || LocalSeconds - EndedAt >= EndSeconds);
}

ALDWorldViewRelay::ALDWorldViewRelay()
{
    bReplicates = true;
    bOnlyRelevantToOwner = true;
    bAlwaysRelevant = false;
    SetNetUpdateFrequency(10);
    PrimaryActorTick.bCanEverTick = true;
}

void ALDWorldViewRelay::EndPlay(const EEndPlayReason::Type Reason)
{
    if (FogMaterial) { FogMaterial->SetScalarParameterValue(TEXT("CoverageActive"), 0); }
    GroundFog.Reset();
    TArray<FLDObservedClear> Cleared;
    Receiver.Reset(ELDObservedClearReason::Disconnected, Cleared);
    LastClears = Cleared;
    for (auto& Pair : Proxies) { if (IsValid(Pair.Value)) { Pair.Value->Destroy(); } }
    Proxies.Empty();
    for (auto& Pair : ProjectileProxies) { if (IsValid(Pair.Value)) { Pair.Value->Destroy(); } }
    ProjectileProxies.Empty();
    for (auto& Pair : GroundProxies) { if (IsValid(Pair.Value)) { Pair.Value->Destroy(); } }
    GroundProxies.Empty();
    if (IsValid(OwnMarker)) { OwnMarker->Destroy(); }
    OwnMarker = nullptr;
    Super::EndPlay(Reason);
}

bool ALDWorldViewRelay::EnsureCatalog(FString& Error)
{
    if (bCatalogReady) { Error.Reset(); return true; }
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    if (!LDDemoEntry::LoadRuntimeContent(Content, Error) || !Content || !Catalog.Build(*Content, Error))
    {
        if (Error.IsEmpty()) { Error = TEXT("Observation requires the approved Demo content on this connection."); }
        return false;
    }
    bCatalogReady = true;
    return true;
}

bool ALDWorldViewRelay::ExpectedWorld(FLDWorldRevision& Out) const
{
    const auto* Registry = GetWorld() ? GetWorld()->GetSubsystem<ULDMapRegistry>() : nullptr;
    if (!Registry || !Registry->Enabled()) { return false; }
    Out = Registry->Revision();
    return Out.IsValid();
}

void ALDWorldViewRelay::Refuse(const FString& Error)
{
    UE_LOG(LogLDWorld, Error, TEXT("LD_OBSERVATION_PROTOCOL_REJECTED %s"), *Error);
    TArray<FLDObservedClear> Cleared;
    Receiver.Reset(ELDObservedClearReason::ProtocolRejected, Cleared);
    Present(Cleared);
}

void ALDWorldViewRelay::ClientFullView_Implementation(const FLDWorldView& View)
{
    FString Error;
    FLDWorldRevision ExpectedRevision;
    if (!EnsureCatalog(Error) || !ExpectedWorld(ExpectedRevision))
    {
        Refuse(Error.IsEmpty() ? FString(TEXT("The Demo world registry is unavailable for observation.")) : Error);
        return;
    }
    TArray<FLDObservedClear> Cleared;
    const double Arrival = FPlatformTime::Seconds();
    const FLDObservedFrameReceiver::EResult Result = Receiver.AcceptFull(View, ExpectedRevision, Catalog, Arrival, Cleared, Error);
    if (Result == FLDObservedFrameReceiver::EResult::Rejected)
    {
        Refuse(Error);
        return;
    }
    if (Result == FLDObservedFrameReceiver::EResult::Applied)
    {
        // Only an accepted frame advances the presentation clock and measures arrival lateness.
        using namespace LDObservedPresentation;
        const double SinceUpdate = bClock ? Arrival - ClockUpdatedAt : 0.0;
        ClockOffset = TrackOffset(bClock, ClockOffset, SinceUpdate, Arrival, View.SampledAtServerSec);
        Lateness = bClock ? TrackLateness(Lateness, SinceUpdate, Arrival - View.SampledAtServerSec - ClockOffset) : 0.0;
        bClock = true;
        ClockUpdatedAt = Arrival;
        const FTiming Target = Timing(Lateness, double(ALDVisionDirector::PublishPeriodMs) / 1000.0);
        TargetDelay = Target.DelaySeconds;
        Receiver.SetStaleAfter(TrackTimeout(Receiver.GetStaleAfter(), Target.StaleAfterSeconds, SinceUpdate));
    }
    Present(Cleared);
}

void ALDWorldViewRelay::ClientPresentedHit_Implementation(const FLDObservedHitReceipt& Hit)
{
    if (!bClock) { return; }
    const double Arrival = FPlatformTime::Seconds();
    TArray<FLDObservedClear> Cleared;
    if (Receiver.Tick(Arrival, Cleared))
    {
        ++Timeouts;
        Present(Cleared);
    }
    float Age = 0.f;
    if (!LDObservedPresentation::ImmediateHitAge(Receiver.Read(), Hit, Arrival - ClockOffset, Age)) { return; }
    const TObjectPtr<ALDObservedEntityProxy>* Found = Proxies.Find(Hit.Handle);
    ALDObservedEntityProxy* Proxy = Found ? Found->Get() : nullptr;
    if (IsValid(Proxy) && Proxy->ObservedHandle == Hit.Handle && Proxy->ObservedEpoch == Hit.ObservationEpoch)
    {
        if (Proxy->PresentImmediateHit(Age, Hit.HitLook, Hit.HitFlags, Hit.HitId, Hit.OccurredAtMs)) { ++AcceptedImmediateHits; }
    }
}

void ALDWorldViewRelay::ClientConceal_Implementation(const FLDObservedConceal& Notice)
{
    FString Error;
    TArray<FLDObservedClear> Cleared;
    if (Receiver.AcceptConceal(Notice, Cleared, Error) == FLDObservedFrameReceiver::EResult::Rejected)
    {
        Refuse(Error);
        return;
    }
    Present(Cleared);
}

void ALDWorldViewRelay::ClientTermination_Implementation(const FLDObservedTerminationNotice& Notice)
{
    FString Error;
    TArray<FLDObservedClear> Cleared;
    if (Receiver.AcceptTermination(Notice, Cleared, Error) == FLDObservedFrameReceiver::EResult::Rejected)
    {
        Refuse(Error);
        return;
    }
    Present(Cleared);
}

void ALDWorldViewRelay::Present(const TArray<FLDObservedClear>& Cleared)
{
    LastClears = Cleared;
    for (const FLDObservedClear& Clear : Cleared)
    {
        TObjectPtr<ALDObservedEntityProxy> Proxy;
        if (!Proxies.RemoveAndCopyValue(Clear.Handle, Proxy) || !IsValid(Proxy)) { continue; }
        // Only an authorized observed death falls; leaving sight, timeouts and resets simply remove the body.
        if (Clear.Reason == ELDObservedClearReason::ObservedDeath) { Proxy->PlayDeathFall(); }
        Proxy->Destroy();
    }
    SyncProxies();
}

bool ALDWorldViewRelay::ReadPresentedLocation(const FGuid& Handle, uint32 ObservationEpoch, FVector& OutWorld) const
{
    FLDObservedUnitView Unit;
    const TObjectPtr<ALDObservedEntityProxy>* Proxy = Proxies.Find(Handle);
    if (!Proxy || !IsValid(*Proxy) || (*Proxy)->ObservedEpoch != ObservationEpoch || !Receiver.FindUnit(Handle, ObservationEpoch, Unit))
    {
        return false;
    }
    OutWorld = (*Proxy)->GetActorLocation();
    return true;
}

bool ALDWorldViewRelay::PickPresentedUnit(const FVector& RayStart, const FVector& RayDirection, double AngleTolerance,
    FGuid& OutHandle, uint32& OutEpoch) const
{
    OutHandle.Invalidate();
    OutEpoch = 0;
    const FLDObservedFrameRead Read = Receiver.Read();
    if (!Read.Frame || Read.Status != ELDObservedViewStatus::Current
        || RayStart.ContainsNaN() || RayDirection.ContainsNaN() || RayDirection.IsNearlyZero()) { return false; }
    const FVector End = RayStart + RayDirection.GetSafeNormal() * 2000000.;
    double Closest = 1.;
    FHitResult Obstruction;
    if (GetWorld()->LineTraceSingleByChannel(Obstruction,RayStart,End,ECC_GameTraceChannel2))
    { Closest=Obstruction.Time; }
    const auto* Viewer=Cast<ALDPlayerController>(GetOwner());
    const auto* OwnBody=Viewer ? Cast<ALDCharacter>(Viewer->GetPawn()) : nullptr;
    double OwnFraction=1.;
    const bool bOwnModelHit = OwnBody && OwnBody->Body->TracePresentedModel(RayStart,End,OwnFraction);
    const bool bOwnModelFirst = bOwnModelHit && OwnFraction <= Closest;
    if (bOwnModelHit)
    { Closest=FMath::Min(Closest,OwnFraction); }
    for (const FLDObservedUnitView& Unit : Read.Frame->Units)
    {
        const auto* Proxy = Proxies.Find(Unit.Handle);
        if (!Proxy || !IsValid(*Proxy) || (*Proxy)->ObservedEpoch != Unit.ObservationEpoch) { continue; }
        double Fraction = 1.;
        if ((*Proxy)->TracePresentedModel(RayStart, End, Fraction)
            && (Fraction < Closest || (Fraction == Closest && OutHandle.IsValid() && Unit.Handle < OutHandle)))
        {
            Closest = Fraction;
            OutHandle = Unit.Handle;
            OutEpoch = Unit.ObservationEpoch;
        }
    }
    if (OutHandle.IsValid()) { return true; }
    if (bOwnModelFirst) { return false; }
    // No model surface under the pointer: the nearest pick volume around it on screen, never through a sight blocker.
    const double Reach = (End - RayStart).Size() * (Obstruction.bBlockingHit ? Obstruction.Time : 1.);
    double Best = TNumericLimits<double>::Max();
    for (const FLDObservedUnitView& Unit : Read.Frame->Units)
    {
        const auto* Proxy = Proxies.Find(Unit.Handle);
        if (!Proxy || !IsValid(*Proxy) || (*Proxy)->ObservedEpoch != Unit.ObservationEpoch) { continue; }
        FVector Foot, Top;
        double Radius = 0., Angle = 0., Along = 0.;
        if (!(*Proxy)->PresentedPickVolume(Foot, Top, Radius)
            || !LDModelPicking::NearVolume(RayStart, RayDirection, Reach, Foot, Top, Radius, AngleTolerance, Angle, Along))
        { continue; }
        if (Angle < Best || (Angle == Best && OutHandle.IsValid() && Unit.Handle < OutHandle))
        {
            Best = Angle;
            OutHandle = Unit.Handle;
            OutEpoch = Unit.ObservationEpoch;
        }
    }
    return OutHandle.IsValid();
}

bool ALDWorldViewRelay::ReadPresentationTime(double& OutAtServerSec) const
{
    if (!bClock) { return false; }
    OutAtServerSec = PresentationAt(FPlatformTime::Seconds());
    return true;
}

void ALDWorldViewRelay::SyncProxies()
{
    const FLDObservedFrameRead Snapshot = Receiver.Read();
    const bool bCurrent = Snapshot.Frame && Snapshot.Status == ELDObservedViewStatus::Current;
    TSet<FGuid> Shown;
    if (bCurrent)
    {
        const double HalfHeight = GetDefault<ALDCharacter>()->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
        for (const FLDObservedUnitView& Unit : Snapshot.Frame->Units)
        {
            Shown.Add(Unit.Handle);
            TObjectPtr<ALDObservedEntityProxy>& Proxy = Proxies.FindOrAdd(Unit.Handle);
            if (!IsValid(Proxy))
            {
                FActorSpawnParameters Params;
                Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
                Proxy = GetWorld()->SpawnActor<ALDObservedEntityProxy>(ALDObservedEntityProxy::StaticClass(),
                    FTransform(FVector(Unit.FeetCm) + FVector(0, 0, HalfHeight)), Params);
            }
            if (IsValid(Proxy)) { Proxy->Apply(Unit, HalfHeight, Snapshot.Frame->SampledAtServerSec); }
            // #97: on a remote client the presented body blocks the own predicted body; every body shares the own body's
            // approved capsule. The Host's world has the authority bodies themselves, so its proxies never collide.
            if (IsValid(Proxy) && GetNetMode() == NM_Client && !Proxy->IsBodyBlocking())
            {
                const APlayerController* Local = GetWorld()->GetFirstPlayerController();
                const auto* Own = Local ? Cast<ACharacter>(Local->GetPawn()) : nullptr;
                if (Own && Own->GetCapsuleComponent())
                {
                    Proxy->EnableBodyBlocking(Own->GetCapsuleComponent()->GetUnscaledCapsuleRadius(), float(HalfHeight));
                }
            }
        }
    }
    for (auto It = Proxies.CreateIterator(); It; ++It)
    {
        if (Shown.Contains(It.Key())) { continue; }
        if (IsValid(It.Value())) { It.Value()->Destroy(); }
        It.RemoveCurrent();
    }
    TSet<FGuid> Flying;
    if (bCurrent)
    {
        for (const FLDObservedProjectileView& Projectile : Snapshot.Frame->Projectiles)
        {
            Flying.Add(Projectile.Handle);
            TObjectPtr<ALDObservedProjectileProxy>& Proxy = ProjectileProxies.FindOrAdd(Projectile.Handle);
            if (!IsValid(Proxy))
            {
                FActorSpawnParameters Params;
                Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
                Proxy = GetWorld()->SpawnActor<ALDObservedProjectileProxy>(ALDObservedProjectileProxy::StaticClass(),
                    FTransform(FVector(Projectile.PositionCm)), Params);
            }
            if (IsValid(Proxy)) { Proxy->Apply(Projectile, Snapshot.Frame->SampledAtServerSec); }
        }
    }
    // Ground states are public facts of the current frame only: any other view removes them.
    TSet<uint8> Grounds;
    if (bCurrent)
    {
        for (const FLDObservedGroundState& Ground : Snapshot.Frame->GroundStates)
        {
            Grounds.Add(Ground.Id);
            TObjectPtr<ALDObservedGroundProxy>& Proxy = GroundProxies.FindOrAdd(Ground.Id);
            if (!IsValid(Proxy))
            {
                FActorSpawnParameters Params;
                Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
                Proxy = GetWorld()->SpawnActor<ALDObservedGroundProxy>(ALDObservedGroundProxy::StaticClass(),
                    FTransform(Ground.CentreCm), Params);
            }
            if (IsValid(Proxy)) { Proxy->Apply(Ground, Snapshot.Frame->SampledAtServerSec); }
        }
    }
    for (auto It = GroundProxies.CreateIterator(); It; ++It)
    {
        if (Grounds.Contains(It.Key())) { continue; }
        if (IsValid(It.Value())) { It.Value()->Destroy(); }
        It.RemoveCurrent();
    }
    const double Local = FPlatformTime::Seconds();
    for (auto It = ProjectileProxies.CreateIterator(); It; ++It)
    {
        if (Flying.Contains(It.Key())) { continue; }
        // A current frame that no longer lists a flight lets it finish its authorized samples; any other view ends it now.
        // It ended in plain sight (and may show its end burst) only when its last position and the next step of its
        // path lie in the observer's current coverage, so leaving sight is never shown as a hit or a landing; a warning
        // (a dropped book) also needs its landing time to have come.
        if (bCurrent && IsValid(It.Value()))
        {
            FVector Last, Next;
            const bool bSeen=It.Value()->LastPath(Last,Next) && CoversPoint(*Snapshot.Frame,Last) && CoversPoint(*Snapshot.Frame,Next)
                && (It.Value()->WarningDue(Snapshot.Frame->SampledAtServerSec) || !It.Value()->HasWarning());
            It.Value()->End(Local,bSeen);
            continue;
        }
        if (IsValid(It.Value())) { It.Value()->Destroy(); }
        It.RemoveCurrent();
    }
}

bool ALDWorldViewRelay::CoversPoint(const FLDWorldView& Frame, const FVector& Point) const
{
    if (Frame.Footprint.SourceStatus != ELDObservationSource::ActiveBody) { return false; }
    const int32 Index=LDObservation::CoverageIndex(LDObservation::MapCoverageBounds(GetWorld()),FVector2D(Point));
    return LDObservation::CoverageBit(Frame.Footprint.CurrentBits,Index);
}

void ALDWorldViewRelay::PresentProxies(float Delta)
{
    const double Local = FPlatformTime::Seconds();
    const double At = PresentationAt(Local);
    for (auto& Pair : Proxies) { if (IsValid(Pair.Value)) { Pair.Value->Present(At, Delta); } }
    for (auto& Pair : GroundProxies) { if (IsValid(Pair.Value)) { Pair.Value->Present(At); } }
    for (auto It = ProjectileProxies.CreateIterator(); It; ++It)
    {
        ALDObservedProjectileProxy* Flight = It.Value();
        if (IsValid(Flight)) { Flight->Present(At); }
        if (IsValid(Flight) && !Flight->IsFinished(At, Local)) { continue; }
        // A naturally finished effect ends with its burst (a flight's hit, a dropped book's landing).
        if (IsValid(Flight)) { Flight->PlayBurst(true); Flight->Destroy(); }
        It.RemoveCurrent();
    }
}
void ALDWorldViewRelay::AttachFog()
{
    const auto* Owning = Cast<ALDPlayerController>(GetOwner());
    AActor* Target = Owning ? Owning->GetViewTarget() : nullptr;
    UCameraComponent* Camera = Target ? Target->FindComponentByClass<UCameraComponent>() : nullptr;
    if (!Owning || !Owning->GetLocalPlayer() || !Camera || CameraOwner == Target) { return; }
    const int32 Width = LDObservation::CoverageWidth, Height = LDObservation::CoverageHeight;
    if (!FogMaterial)
    {
        UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Lockdown/Art/Fog/MI_FogMist.MI_FogMist"));
        if (!ensureMsgf(Base, TEXT("Missing fog material instance /Game/Lockdown/Art/Fog/MI_FogMist"))) { return; }
        FogTexture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
        FogTexture->SRGB = false;
        // Bilinear sampling softens the coverage cell edges; with the blend the lit area glides instead of stepping.
        FogTexture->Filter = TF_Bilinear;
        FogTexture->AddressX = TA_Clamp;
        FogTexture->AddressY = TA_Clamp;
        FogTexture->NeverStream = true;
        void* Pixels = FogTexture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
        FMemory::Memzero(Pixels, Width * Height * 4);
        FogTexture->GetPlatformData()->Mips[0].BulkData.Unlock();
        FogTexture->UpdateResource();
        FogMaterial = UMaterialInstanceDynamic::Create(Base, this);
        FogMaterial->SetTextureParameterValue(TEXT("OcclusionDistance"), FogTexture);
        FogMaterial->SetScalarParameterValue(TEXT("CoverageOnly"), 1);
        FogMaterial->SetScalarParameterValue(TEXT("CoverageActive"), 0);
    }
    Camera->PostProcessSettings.AddBlendable(FogMaterial, 1);
    CameraOwner = Target;
    FogUploadSerial = MAX_uint64;
    UploadFog();
}

const TArray<uint8>* ALDWorldViewRelay::ReadGroundFog(uint64& OutSerial) const
{
    OutSerial = GroundFogSerial;
    if (!GroundFog || GroundFog->DisplayCoverage.IsEmpty()) { return nullptr; }
    const auto* Owning = Cast<ALDPlayerController>(GetOwner());
    return Owning && Owning->OwnerState.LifePhase == ELDLifePhase::Alive
        && Owning->OwnerState.Match == GroundFog->Match && Owning->OwnerState.Life == GroundFog->Life
        && Owning->GetPawn() == GroundFog->Pawn.Get() ? &GroundFog->DisplayCoverage : nullptr;
}

void ALDWorldViewRelay::UpdateGroundFog(const ALDPlayerController& Owning, double Now)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(LDLocalGroundFog);
    UWorld* World = GetWorld();
    const auto* Registry = World->GetSubsystem<ULDMapRegistry>();
    const auto* Game = World->GetGameState<ALDGameState>();
    const auto* Pawn = Cast<ALDCharacter>(Owning.GetPawn());
    const FLDOwnerState& Own = Owning.OwnerState;
    const FLDPublicMatchView* Public = Game && Game->PublicMatch.Generation == Own.Match ? &Game->PublicMatch : nullptr;
    const auto Clear = [this]()
    {
        if (!GroundFog) { return; }
        auto& Fog = *GroundFog;
        Fog.bScanning = false;
        Fog.NextScanAt = 0.;
        Fog.OwnCoverage.Reset();
        Fog.ArenaSerial = 0;
        if (Fog.DisplayCoverage.IsEmpty()) { return; }
        Fog.DisplayCoverage.Reset();
        ++GroundFogSerial;
    };
    if (!Registry || !Registry->Enabled() || !Public || !Pawn || !Own.BodyRef.IsValid()
        || Own.LifePhase != ELDLifePhase::Alive || Own.Life == 0
        || (Public->Phase != ELDMatchPhase::Rest && Public->Phase != ELDMatchPhase::Development
            && Public->Phase != ELDMatchPhase::CaptureOpen))
    { Clear(); return; }

    if (!GroundFog) { GroundFog = MakeShared<FLDLocalGroundFog>(); }
    auto& Fog = *GroundFog;
    if (Fog.Match != Own.Match || Fog.Life != Own.Life || Fog.Pawn.Get() != Pawn)
    {
        Clear();
        Fog.Match = Own.Match;
        Fog.Life = Own.Life;
        Fog.Pawn = Pawn;
    }
    const bool bBlackout = Public->DailyModifiers.Contains(ELDDailyModifier::Blackout);
    if (Fog.BuildRevision != Registry->Revision() || Fog.bBlackout != bBlackout)
    {
        Clear();
        Fog.Sight = FLDSightGeometry();
        Fog.BuildRevision = Registry->Revision();
        Fog.bBlackout = bBlackout;
        TSharedPtr<const FLDDemoContentSnapshot> Content;
        FString Error;
        if (!LDDemoEntry::LoadRuntimeContent(Content, Error) || !Content)
        { UE_LOG(LogLDWorld, Warning, TEXT("Local ground fog: %s"), *Error); return; }
        const auto& Parameters = Content->Get().Parameters;
        FLDDemoRational Radius = Parameters.VisionRadiusCm;
        if (bBlackout)
        {
            Radius.Numerator *= 10000 - Parameters.Daily.BlackoutVisionCutBp;
            Radius.Denominator *= 10000;
        }
        if (!Fog.Sight.Build(*Registry->GetGeometry(), Fog.BuildRevision, Radius, Parameters.VisionRange,
            Error, World, &Registry->GetTerrain(), true))
        { UE_LOG(LogLDWorld, Warning, TEXT("Local ground fog: %s"), *Error); return; }
    }
    if (!Fog.Sight.IsValid()) { return; }

    const FVector Source = Pawn->GetActorLocation() - FVector(0, 0, Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    if (!Fog.Sight.InBounds(Source)) { Clear(); return; }
    if ((Fog.bScanning || !Fog.OwnCoverage.IsEmpty()) && FVector::Dist2D(Source, Fog.Source) > Fog.Sight.MaximumRadius())
    { Clear(); }

    const double QuarterCell = Fog.Sight.CoverageBounds().GetSize().X / LDObservation::CoverageWidth * .25;
    if (!Fog.bScanning && Now >= Fog.NextScanAt
        && (Fog.OwnCoverage.IsEmpty() || !Source.Equals(Fog.Source, QuarterCell)))
    {
        Fog.Source = Source;
        Fog.NextScanAt = Now + .05;
        Fog.bScanning = Fog.Sight.BeginCoverage(Source, Fog.Pending);
    }
    const bool bCompleted = Fog.bScanning && Fog.Sight.ContinueCoverage(Fog.Pending, FPlatformTime::Seconds() + .002);
    if (bCompleted)
    {
        Fog.bScanning = false;
        Swap(Fog.OwnCoverage, Fog.Pending.Bits);
    }

    // Arena teammate/spectator coverage remains a server grant. A stale grant never replaces the local ground result.
    const auto Step = Public->Timeline.Step;
    const bool bArena = Public->Phase == ELDMatchPhase::Rest && Public->PK.Serial != 0
        && (Step == ELDRestStep::PKReady || Step == ELDRestStep::PK);
    const FLDObservedFrameRead Snapshot = bArena ? Receiver.Read() : FLDObservedFrameRead();
    const bool bArenaGrant = Snapshot.Status == ELDObservedViewStatus::Current && Snapshot.Frame
        && Snapshot.Frame->MatchGeneration == Own.Match && Snapshot.Frame->ObserverLife == Own.Life
        && Snapshot.Frame->Footprint.CurrentBits.Num() == LDObservation::CoverageBytes;
    const uint64 ArenaSerial = bArenaGrant ? Snapshot.LocalSerial : 0;
    if (!bCompleted && Fog.ArenaSerial == ArenaSerial) { return; }
    Fog.ArenaSerial = ArenaSerial;
    TArray<uint8> Next = Fog.OwnCoverage;
    if (bArenaGrant)
    {
        if (Next.IsEmpty()) { Next.Init(0, LDObservation::CoverageBytes); }
        const auto& Grant = Snapshot.Frame->Footprint.CurrentBits;
        for (int32 Index = 0; Index < Next.Num(); ++Index) { Next[Index] |= Grant[Index]; }
    }
    if (Next != Fog.DisplayCoverage)
    {
        Fog.DisplayCoverage = MoveTemp(Next);
        ++GroundFogSerial;
    }
}

void ALDWorldViewRelay::UploadFog()
{
    if (!FogTexture || !FogTexture->GetResource() || !FogMaterial) { return; }
    uint64 Serial;
    const TArray<uint8>* Bits = ReadGroundFog(Serial);
    if (Serial == FogUploadSerial) { return; }
    FogUploadSerial = Serial;
    const bool bCurrent = Bits != nullptr;
    FogMaterial->SetScalarParameterValue(TEXT("CoverageActive"), bCurrent ? 1.f : 0.f);
    if (!bCurrent)
    {
        FogShown.Reset();
        FogTarget.Reset();
        bFogBlending = false;
        return;
    }
    const FBox2D& Bounds = GroundFog->Sight.CoverageBounds();
    const FVector2D Span=Bounds.GetSize();
    FogMaterial->SetVectorParameterValue(TEXT("FieldWorldMin"), FLinearColor(Bounds.Min.X, Bounds.Min.Y, 0, 0));
    FogMaterial->SetVectorParameterValue(TEXT("FieldWorldSpan"), FLinearColor(Span.X, Span.Y, 0, 0));
    LDFogDistanceField::Build(*Bits, Span.X / LDObservation::CoverageWidth, Span.Y / LDObservation::CoverageHeight, FogTarget);
    if (FogShown.Num() != FogTarget.Num()) { FogShown = FogTarget; }
    bFogBlending = true;
}

void ALDWorldViewRelay::BlendFog(float Delta)
{
    if (!bFogBlending || !FogTexture || !FogTexture->GetResource() || FogShown.Num() != LDObservation::CoverageCells
        || FogTarget.Num() != LDObservation::CoverageCells) { return; }
    const float Alpha = 1.f - FMath::Exp(-FMath::Max(Delta, 0.f) / .06f);
    bool bMoving = false;
    const int32 Width = LDObservation::CoverageWidth, Height = LDObservation::CoverageHeight;
    uint8* Pixels = new uint8[LDObservation::CoverageCells * 4];
    for (int32 Index = 0; Index < LDObservation::CoverageCells; ++Index)
    {
        const float Target = FogTarget[Index];
        float& Shown = FogShown[Index];
        Shown = FMath::Lerp(Shown, Target, Alpha);
        if (FMath::Abs(Shown - Target) < 1.f / 1024.f) { Shown = Target; }
        bMoving |= Shown != Target;
        const uint8 Value = uint8(FMath::Clamp(FMath::RoundToInt(Shown * 255.f), 0, 255));
        Pixels[Index * 4] = Pixels[Index * 4 + 1] = Pixels[Index * 4 + 2] = Value;
        Pixels[Index * 4 + 3] = 255;
    }
    FogTexture->UpdateTextureRegions(0, 1, new FUpdateTextureRegion2D(0, 0, 0, 0, Width, Height), Width * 4, 4, Pixels,
        [](uint8* Data, const FUpdateTextureRegion2D* Regions) { delete[] Data; delete Regions; });
    bFogBlending = bMoving;
}

void ALDWorldViewRelay::TrackOwnBody(const ALDPlayerController& Owning)
{
    // Same condition that shows the own body (ALDCharacter::Tick): only this viewer's current living body.
    ALDCharacter* Own = Cast<ALDCharacter>(Owning.GetPawn());
    const bool bAlive = Own && Owning.OwnerState.LifePhase == ELDLifePhase::Alive && Owning.OwnerState.BodyRef.IsValid();
    if (bAlive && !IsValid(OwnMarker))
    {
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        OwnMarker = GetWorld()->SpawnActor<ALDOwnBodyMarker>(ALDOwnBodyMarker::StaticClass(), FTransform::Identity, Params);
    }
    if (IsValid(OwnMarker)) { OwnMarker->Track(bAlive ? Own : nullptr, bAlive ? Own->PresentationKey : 0); }
}

void ALDWorldViewRelay::Tick(float Delta)
{
    Super::Tick(Delta);
    const auto* Owning = Cast<ALDPlayerController>(GetOwner());
    // Present only for this machine's real viewer: never for a server-side opponent or a remote client's server copy.
    if (!Owning || !Owning->GetLocalPlayer()) { return; }
    AttachFog();
    TrackOwnBody(*Owning);
    TArray<FLDObservedClear> Cleared;
    const uint32 OwnerMatch = Owning->OwnerState.Match;
    bool bReset = false;
    if (OwnerMatch != 0 && Receiver.CursorMatch() != 0 && OwnerMatch != Receiver.CursorMatch()
        && Receiver.Read().Status != ELDObservedViewStatus::Unavailable)
    {
        Receiver.Reset(ELDObservedClearReason::MatchReset, Cleared);
        bReset = true;
    }
    const double Now = FPlatformTime::Seconds();
    ShownDelay = LDObservedPresentation::SlewDelay(ShownDelay, TargetDelay, DelaySlewedAt < 0 ? 0.0 : Now - DelaySlewedAt);
    DelaySlewedAt = Now;
    // A reset or timeout clears units and flights alike, even when no unit was presented.
    const bool bTimedOut = Receiver.Tick(Now, Cleared);
    if (bTimedOut) { ++Timeouts; }
    if (bReset || bTimedOut || Cleared.Num() > 0) { Present(Cleared); }
    UpdateGroundFog(*Owning, Now);
    UploadFog();
    PresentProxies(Delta);
    BlendFog(Delta);
    FadeOccludingProps(*Owning, Delta);
}

void ALDWorldViewRelay::FadeOccludingProps(const ALDPlayerController& Owning, float Delta)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(LDPropFade);
    UWorld* World = GetWorld();
    const double Now = FPlatformTime::Seconds();
    // Known props that are gone, left this world or lost the tag are forgotten; this reads only the known list.
    if (FadeablesScannedAt < 0 || Now - FadeablesScannedAt > LDPropFade::SweepSeconds)
    {
        FadeablesScannedAt = Now;
        FadeableProps.RemoveAll([World](const FFadeableProp& Prop)
        {
            const UPrimitiveComponent* Primitive = Prop.Primitive.Get();
            return !Primitive || Primitive->GetWorld() != World || !Primitive->ComponentHasTag(LDPropFade::Tag);
        });
    }
    // Rebuilt tagged scene components are found by walking the level actors a slice per frame: a whole sweep takes
    // about SweepSeconds and never lands in one frame. Only the first sweep runs at once, so the list is complete
    // from the start.
    const TArray<ULevel*>& Levels = World->GetLevels();
    TArray<int32, TInlineAllocator<8>> Counts;
    int32 Total = 0;
    for (const ULevel* Level : Levels) { Counts.Add(Level ? Level->Actors.Num() : 0); Total += Counts.Last(); }
    for (int32 Budget = FadeSweep.Budget(Total, Delta); Budget > 0; --Budget)
    {
        int32 LevelIndex = 0, ActorIndex = 0;
        if (!FadeSweep.Next(Counts, LevelIndex, ActorIndex)) { break; }
        const AActor* Actor = Levels[LevelIndex]->Actors[ActorIndex];
        if (!IsValid(Actor)) { continue; }
        Actor->ForEachComponent<UPrimitiveComponent>(false, [this](UPrimitiveComponent* Primitive)
        {
            if (Primitive->ComponentHasTag(LDPropFade::Tag)
                && !FadeableProps.ContainsByPredicate([Primitive](const FFadeableProp& Prop) { return Prop.Primitive.Get() == Primitive; }))
            { FFadeableProp& Prop = FadeableProps.AddDefaulted_GetRef(); Prop.Primitive = Primitive; }
        });
    }
    if (FadeableProps.IsEmpty()) { return; }
    // Targets: the own living body and every unit presented right now (head and chest), never a hidden or stale one.
    TArray<FVector, TInlineAllocator<2 * (1 + LDObservation::MaxUnits)>> Targets;
    const APawn* Own = Owning.OwnerState.LifePhase == ELDLifePhase::Alive ? Owning.GetPawn().Get() : nullptr;
    if (Own) { Targets.Add(Own->GetActorLocation()); Targets.Add(Own->GetActorLocation() + FVector(0, 0, 70)); }
    for (const auto& Pair : Proxies)
    {
        if (!IsValid(Pair.Value)) { continue; }
        Targets.Add(Pair.Value->GetActorLocation());
        Targets.Add(Pair.Value->GetActorLocation() + FVector(0, 0, 70));
    }
    // The surface under the pointer, exactly where it was hit: ground behind a building fades the building through the
    // sight line, and a roof under the pointer lies on its own bounds, so its building is run through and fades too.
    FHitResult PointerGround;
    if (Own && Owning.Navigation && Owning.Navigation->GroundUnderPointer(PointerGround))
    { Targets.Add(PointerGround.ImpactPoint); }
    const FVector Camera = Owning.PlayerCameraManager ? Owning.PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
    const FBox Reach = LDPropFade::Reach(Camera, Targets);
    auto Hides = [&](const FBox& Bounds)
    {
        if (!Reach.IsValid || !Reach.Intersect(Bounds)) { return false; }
        for (const FVector& Target : Targets) { if (LDPropFade::Occludes(Bounds, Camera, Target)) { return true; } }
        return false;
    };
    // A prop's own material instances are made the first time it fades, not when it is found: most props never hide
    // anything, and the first sweep finds every one of them in the same frame.
    auto EnsureMaterials = [](FFadeableProp& Prop, UPrimitiveComponent& Primitive)
    {
        UMeshComponent* Mesh = Cast<UMeshComponent>(&Primitive);
        if (!Prop.Materials.IsEmpty() || !Mesh) { return; }
        // The strength is one material value: an instanced prop is tall only when every instance is on its own (the
        // spread of several poles is no building); a whole component judges its own bounds.
        float Strength = LDPropFade::StrengthFor(Primitive.Bounds.GetBox());
        if (Prop.bPerInstance)
        {
            Strength = LDPropFade::TallFadedStrength;
            for (const FBox& Instance : Prop.InstanceBounds) { Strength = FMath::Min(Strength, LDPropFade::StrengthFor(Instance)); }
        }
        for (int32 Index = 0; Index < Mesh->GetNumMaterials(); ++Index)
        {
            UMaterialInstanceDynamic* Dynamic = Mesh->CreateDynamicMaterialInstance(Index);
            if (Dynamic) { Dynamic->SetScalarParameterValue(LDPropFade::StrengthParameter, Strength); }
            Prop.Materials.Add(Dynamic);
        }
    };
    for (FFadeableProp& Prop : FadeableProps)
    {
        UPrimitiveComponent* Primitive = Prop.Primitive.Get();
        if (!Primitive) { continue; }
        if (!Prop.bResolved)
        {
            // The fade needs the material's LDFade parameter; without it the prop hides while it occludes instead.
            Prop.bResolved = true;
            Prop.bCastShadow = Primitive->CastShadow;
            if (UMeshComponent* Mesh = Cast<UMeshComponent>(Primitive))
            {
                float Value = 0;
                bool bAll = Mesh->GetNumMaterials() > 0;
                for (int32 Index = 0; Index < Mesh->GetNumMaterials(); ++Index)
                {
                    UMaterialInterface* Material = Mesh->GetMaterial(Index);
                    bAll &= Material && Material->GetScalarParameterValue(FHashedMaterialParameterInfo(LDPropFade::Parameter), Value);
                }
                const auto* Instanced = Cast<UInstancedStaticMeshComponent>(Mesh);
                Prop.bPerInstance = bAll && Instanced && Instanced->NumCustomDataFloats >= 1 && Instanced->GetStaticMesh();
                if (Prop.bPerInstance)
                {
                    const FBox Local = Instanced->GetStaticMesh()->GetBoundingBox();
                    for (int32 Index = 0; Index < Instanced->GetInstanceCount(); ++Index)
                    {
                        FTransform Placement;
                        Instanced->GetInstanceTransform(Index, Placement, true);
                        Prop.InstanceBounds.Add(Local.TransformBy(Placement));
                        Prop.InstanceUnion += Prop.InstanceBounds.Last();
                    }
                    Prop.InstanceFades.SetNumZeroed(Prop.InstanceBounds.Num());
                }
                Prop.bParameter = bAll;
            }
            if (!Prop.bParameter)
            {
                UE_LOG(LogLDWorld, Warning, TEXT("LD_FADEABLE_PARAMETER_MISSING %s has no LDFade material parameter; it hides while it occludes."),
                    *Primitive->GetPathName());
            }
        }
        if (Prop.bPerInstance)
        {
            if (Prop.InstancesFaded == 0 && (!Reach.IsValid || !Reach.Intersect(Prop.InstanceUnion))) { continue; }
            auto* Instanced = Cast<UInstancedStaticMeshComponent>(Primitive);
            bool bChanged = false;
            for (int32 Index = 0; Instanced && Index < Prop.InstanceBounds.Num(); ++Index)
            {
                const float Was = Prop.InstanceFades[Index];
                const float Next = LDPropFade::Step(Was, Hides(Prop.InstanceBounds[Index]), Delta);
                if (Next == Was) { continue; }
                EnsureMaterials(Prop, *Primitive);
                Prop.InstancesFaded += (Was == 0.f) - (Next == 0.f);
                Prop.InstanceFades[Index] = Next;
                bChanged |= Instanced->SetCustomDataValue(Index, 0, Next, false);
            }
            if (bChanged) { Instanced->MarkRenderStateDirty(); }
            continue;
        }
        const bool bOccluding = Hides(Primitive->Bounds.GetBox());
        const float Next = LDPropFade::Step(Prop.Fade, bOccluding, Delta);
        if (Next == Prop.Fade && Prop.bResolved && (Next == 0.f || Next == 1.f) && Prop.bParameter) { continue; }
        Prop.Fade = Next;
        // A faded prop's shadow would still darken the bodies it no longer hides; it returns with the opaque prop.
        const bool bShadow = Prop.bCastShadow && Prop.Fade <= 0.f;
        if (Primitive->CastShadow != bShadow) { Primitive->SetCastShadow(bShadow); }
        if (Prop.bParameter)
        {
            EnsureMaterials(Prop, *Primitive);
            for (const TWeakObjectPtr<UMaterialInstanceDynamic>& Material : Prop.Materials)
            { if (UMaterialInstanceDynamic* Dynamic = Material.Get()) { Dynamic->SetScalarParameterValue(LDPropFade::Parameter, Prop.Fade); } }
        }
        else if (Primitive->bHiddenInGame != (Prop.Fade > .5f)) { Primitive->SetHiddenInGame(Prop.Fade > .5f); }
    }
}
