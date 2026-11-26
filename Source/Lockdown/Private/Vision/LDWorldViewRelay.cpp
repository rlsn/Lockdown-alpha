#include "Vision/LDWorldViewRelay.h"
#include "Components/PoseableMeshComponent.h"
#include "Audio/LDAudioSubsystem.h"
#include "UI/LDBroadcastStyle.h"
#include "World/LDRecoveryOrbMarker.h"
#include "Art/LDArtSubsystem.h"
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
#include "Framework/LDGameInstance.h"
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
#include "RenderingThread.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Camera/PlayerCameraManager.h"
#include "HAL/IConsoleManager.h"

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
        || Look==ELDObservedProjectileLook::ChalkReturn || Look==ELDObservedProjectileLook::SockReturn
        || Look==ELDObservedProjectileLook::FistWind;
}

bool IsObservedReturn(ELDObservedProjectileLook Look)
{
    return Look==ELDObservedProjectileLook::ChalkReturn || Look==ELDObservedProjectileLook::SockReturn;
}

bool IsObservedArea(ELDObservedProjectileLook Look)
{
    return Look==ELDObservedProjectileLook::AreaPlain || Look==ELDObservedProjectileLook::AreaMagic
        || Look==ELDObservedProjectileLook::AreaFire || Look==ELDObservedProjectileLook::AreaWater
        || Look==ELDObservedProjectileLook::AreaElectric || Look==ELDObservedProjectileLook::SlipperDrop
        || Look==ELDObservedProjectileLook::PhantomDecoy || Look==ELDObservedProjectileLook::ShardBurst;
}

FLinearColor ObservedEffectColor(ELDObservedProjectileLook Look)
{
    switch (Look)
    {
    case ELDObservedProjectileLook::Chalk:
    case ELDObservedProjectileLook::ChalkReturn: return FLinearColor(.95f,.95f,.9f);
    case ELDObservedProjectileLook::Sock:
    case ELDObservedProjectileLook::SockReturn: return FLinearColor(.86f,.88f,.76f);
    case ELDObservedProjectileLook::FistWind: return FLinearColor(1.f,.42f,.18f);
    case ELDObservedProjectileLook::SlipperDrop: return FLinearColor(.25f,.95f,1.f);
    case ELDObservedProjectileLook::CordonTape: return FLinearColor(1.f,.84f,.08f);
    case ELDObservedProjectileLook::PhantomDecoy: return FLinearColor(.78f,.38f,1.f);
    case ELDObservedProjectileLook::ShardBurst: return FLinearColor(.2f,.92f,1.f);
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
    case ELDObservedProjectileLook::FlyerArming:
    case ELDObservedProjectileLook::FlyerArmed: return FLinearColor(.9f,.88f,.8f);
    case ELDObservedProjectileLook::BookShadow: return FLinearColor(.62f,.23f,.13f);
    case ELDObservedProjectileLook::SpeechCone: return FLinearColor(1.f,.38f,.16f);
    default: return FLinearColor(.92f,.9f,.98f);
    }
}
}

bool FLDObservedCatalog::Build(const FLDDemoContentSnapshot& Content, FString& Error)
{
    *this = FLDObservedCatalog();
    const FLDDemoContentDefinitions& Definitions = Content.Get();
    const auto& Radius = Definitions.Parameters.CampClearOrbRadiusCm;
    RecoveryPackRadiusCm = Radius.Denominator > 0 ? double(Radius.Numerator) / double(Radius.Denominator) : 0.;
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
    Shown.ActionComboStep = Unit.ActionComboStep;
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
    // A level seen to rise on a body that stays in sight plays its level-up; a body first seen shows none.
    if (ObservedHandle != Unit.Handle) { Body->SetLevel(0); }
    if (Unit.AuthorizedFieldMask & LDObservation::Level) { Body->SetLevel(Unit.Level); }
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
        Body->SetElementStates(Reached.Statuses);
        Body->SetCastAbility(Reached.Cue == ELDObservedActionCue::Cast ? Reached.ActionAbility : 0);
        Body->SetComboStep(Reached.Cue == ELDObservedActionCue::Attack ? Reached.ActionComboStep : 0);
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
        if (IsValid(Mark)) { Mark->DestroyComponent(); }
        Mark = nullptr;
        SetActorHiddenInGame(true);
        return;
    }
    if (Tracked.Get() != Body)
    {
        const UCapsuleComponent* Capsule = Body->FindComponentByClass<UCapsuleComponent>();
        const auto* Character=Cast<ALDCharacter>(Body);
        AttachToComponent(Character && Character->Body ? Character->Body.Get() : Body->GetRootComponent(),
            FAttachmentTransformRules::SnapToTargetNotIncludingScale);
        SetActorRelativeLocation(FVector(0, 0, -(Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.0)));
        Tracked = Body;
    }
    if (TintSlot != Slot)
    {
        UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Lockdown/Generated/Materials/M_Character.M_Character"));
        if (!ensureMsgf(Material, TEXT("Character material missing; rerun asset bootstrap"))) { return; }
        // Fixed colours: the shared tint instances serve them, as they serve the body markers.
        Rim->SetMaterial(0, ULDArtSubsystem::SharedTint(this, Material, FLinearColor(1.f, .85f, .2f)));
        Core->SetMaterial(0, ULDArtSubsystem::SharedTint(this, Material, ULDHumanoidVisualComponent::ContenderTint(Slot)));
        TintSlot = Slot;
    }
    if (ULDImpactFxSubsystem* Impacts = GetWorld() ? GetWorld()->GetSubsystem<ULDImpactFxSubsystem>() : nullptr)
    {
        const FVector At = Impacts->FloorUnder(GetActorLocation() + FVector(0, 0, 20));
        Mark = Impacts->ShowLoop(*RootComponent, Mark, LDSliceVfx::ESystem::OwnMark, At, At + FVector(100, 0, 0), ULDHumanoidVisualComponent::ContenderTint(Slot),
            64.f, 2.2f, 1.e4f, true);
    }
    Rim->SetVisibility(!Mark);
    Core->SetVisibility(!Mark);
    SetActorHiddenInGame(false);
}

ALDObservedGroundProxy::ALDObservedGroundProxy()
{
    bReplicates = false;
    PrimaryActorTick.bCanEverTick = false;
    SetActorEnableCollision(false);
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Box(TEXT("/Engine/BasicShapes/Cube"));
    Rim = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("GroundRim"));
    Rim->SetupAttachment(RootComponent);
    Rim->SetStaticMesh(Box.Object);
    Rim->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Rim->SetCanEverAffectNavigation(false);
    Rim->SetCastShadow(false);
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
        Arc(FVector(H, 0, 0), R, -90., 90., 16);
        Arc(FVector(-H, 0, 0), R, 90., 270., 16);
        // A copy: adding an element of the array to itself may reallocate under the reference.
        const FVector First = Out[0];
        Out.Add(First);
    }
    else if (Ground.Shape == ELDObservedGroundShape::Sector)
    {
        const double Half = Ground.ExtentCm * .5;
        Out.Add(FVector::ZeroVector);
        Arc(FVector::ZeroVector, R, -Half, Half, 32);
        Out.Add(FVector::ZeroVector);
    }
    // Enough segments that the outline reads as a curve at gameplay zoom (no visible corners).
    else { Arc(FVector::ZeroVector, R, 0., 360., 64); }
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
        if (!SeatTint)
        {
            UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Lockdown/Generated/Materials/M_Character.M_Character"));
            if (!ensureMsgf(Material, TEXT("Character material missing; rerun asset bootstrap"))) { return; }
            SeatTint = UMaterialInstanceDynamic::Create(Material, this);
            Rim->SetMaterial(0, SeatTint);
        }
        SeatTint->SetVectorParameterValue(TEXT("Tint"), Ground.OwnerSeat < LDObservation::MaxPresentations
            ? ULDHumanoidVisualComponent::ContenderTint(Ground.OwnerSeat) : FLinearColor(.6f, .6f, .6f));
        Outline(Ground, RimPoints);
        ShownOutline = -1;
    }
    // A freshly laid state (not one walked into sight) appears with its burst; the burst leaves with the proxy.
    const bool bFresh = !bShown && Ground.TotalMs >= Ground.RemainingMs && Ground.TotalMs - Ground.RemainingMs < 400;
    const bool bNewKind = !bShown || Ground.Kind != Shown.Kind;
    Shown = Ground;
    bShown = true;
    if (bShape) { ShowParticles(); }
    // Its sound: one quiet loop at its centre while it is shown (AUDIO_AND_FEEL).
    if (ULDAudioSubsystem* Audio = bNewKind && GetGameInstance() ? GetGameInstance()->GetSubsystem<ULDAudioSubsystem>() : nullptr)
    { Audio->StartGroundLoop(*this, Ground.Kind, GetActorLocation()); }
    if (bFresh)
    {
        LDHitVfx::EBurst Kind;
        FLinearColor Tint;
        ULDImpactFxSubsystem* Impacts = GetWorld() ? GetWorld()->GetSubsystem<ULDImpactFxSubsystem>() : nullptr;
        if (Impacts && ULDImpactFxSubsystem::GroundSystem(Ground.Kind) == LDSliceVfx::ESystem::Count && LDHitVfx::GroundAppearBurstOf(Ground.Kind, Kind, Tint))
        { Impacts->Kit(GetActorLocation() + FVector(0, 0, 20.f), LDHitVfx::BurstOf(Kind, Tint, Impacts->LocalEffectScale()), *this); }
    }
    Present(SampledAtServerSec);
}

void ALDObservedGroundProxy::ShowParticles()
{
    ULDImpactFxSubsystem* Impacts = GetWorld() ? GetWorld()->GetSubsystem<ULDImpactFxSubsystem>() : nullptr;
    if (!Impacts || !bShown) { return; }
    ParticleScale = Impacts->LocalEffectScale();
    // A kind with its own designed effect (ULDImpactFxSubsystem::ShowGround, laid in Present) has no kit particles.
    if (ULDImpactFxSubsystem::GroundSystem(Shown.Kind) != LDSliceVfx::ESystem::Count)
    {
        if (IsValid(Particles)) { Particles->DestroyComponent(); }
        Particles = nullptr;
        return;
    }
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
    if (ULDAudioSubsystem* Audio = GetGameInstance() ? GetGameInstance()->GetSubsystem<ULDAudioSubsystem>() : nullptr) { Audio->StopGroundLoop(*this); }
    if (ULDImpactFxSubsystem* Impacts = GetWorld() ? GetWorld()->GetSubsystem<ULDImpactFxSubsystem>() : nullptr) { Impacts->ClearForActor(*this); }
    Super::EndPlay(Reason);
}

void ALDObservedGroundProxy::Present(double AtServerSec)
{
    // The reduced-effects setting changed: rebuild the particles at the new density.
    if (const ULDImpactFxSubsystem* Impacts = GetWorld() ? GetWorld()->GetSubsystem<ULDImpactFxSubsystem>() : nullptr;
        Impacts && bShown && ParticleScale >= 0.f && Impacts->LocalEffectScale() != ParticleScale) { ShowParticles(); }
    if (!bShown) { return; }
    const double Left = FMath::Clamp((ExpiresAtServerSec - AtServerSec) / LifeSeconds, 0., 1.);
    // A designed ground state (the field, a puddle, ice, a fire floor, miasma; ULDImpactFxSubsystem::ShowGround) fades out over
    // the last third of its life. A band or a sector shows the disc that fits inside it (as its particles did).
    if (ULDImpactFxSubsystem* Impacts = GetWorld() ? GetWorld()->GetSubsystem<ULDImpactFxSubsystem>() : nullptr;
        Impacts && ULDImpactFxSubsystem::GroundSystem(Shown.Kind) != LDSliceVfx::ESystem::Count)
    {
        float Radius = Shown.RadiusCm;
        FVector Offset = FVector::ZeroVector;
        if (Shown.Shape == ELDObservedGroundShape::Sector)
        {
            const float Sine = FMath::Sin(FMath::DegreesToRadians(FMath::Clamp(Shown.ExtentCm * .5f, 5.f, 90.f)));
            Offset = FVector(Shown.RadiusCm / (1.f + Sine), 0, 0);
            Radius = FMath::Max(Offset.X * Sine, 40.f);
        }
        else if (Shown.Shape != ELDObservedGroundShape::Circle)
        { Radius = FMath::Max(FMath::Min(Shown.RadiusCm, Shown.ExtentCm > 0.f ? Shown.ExtentCm : Shown.RadiusCm), 40.f); }
        Impacts->ShowGround(*this, Shown.Kind, GetActorLocation() + GetActorRotation().RotateVector(Offset), Radius, float(FMath::Min(Left * 3., 1.)), Shown.OwnerSeat);
    }
    if (RimPoints.Num() < 2) { return; }
    // The outline drains with the remaining life (spec §12): the first Remaining/Life of its segments stay.
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
    using ELook=ELDObservedProjectileLook;
    const bool bFlight=IsObservedFlight(Effect.Look);
    const bool bCone=Effect.Look==ELook::SpeechCone || Effect.Look==ELook::ConeWarning;
    const bool bBook=Effect.Look==ELook::BookShadow;
    const bool bArea=IsObservedArea(Effect.Look);
    const bool bForwardLine=Effect.Look==ELook::LineWarning;
    const bool bCordon=Effect.Look==ELook::CordonTape;
    const bool bRoad=Effect.Look==ELook::Road || Effect.Look==ELook::LineWarning || Effect.Look==ELook::Wind || bCordon;
    const bool bBag=Effect.Look==ELook::Bag;
    const bool bWall=Effect.Look==ELook::BagWall;
    const bool bDecoy=Effect.Look==ELook::PhantomDecoy;
    const FLinearColor Color=Effect.bOwn ? FMath::Lerp(ObservedEffectColor(Effect.Look),FLinearColor(.08f,.85f,.6f),.5f)
        : ObservedEffectColor(Effect.Look);
    ShownLook=Effect.Look;
    ShownRadius=Effect.RadiusCm; ShownInnerRadius=Effect.InnerRadiusCm; ShownAngle=Effect.AngleDegrees;
    ShownLength=Effect.LengthCm;bShownOwn=Effect.bOwn;
    ShownColor=Color;
    ShownHeading=FRotator(0,float(Effect.Heading)/65536.f*360.f,0);
    ULDImpactFxSubsystem* Impacts=GetWorld()?GetWorld()->GetSubsystem<ULDImpactFxSubsystem>():nullptr;
    if (Impacts)
    {
        USceneComponent* Anchor=bFlight ? static_cast<USceneComponent*>(Trail) : RootComponent.Get();
        ParticleScale=Impacts->LocalEffectScale();
        Particles=Impacts->ShowEffect(*Anchor,Particles,Effect.Look,Effect.RadiusCm,ShownHeading);
    }
    if (EffectTint) { EffectTint->SetVectorParameterValue(TEXT("Tint"),Color); }
    if (AccentTint) { AccentTint->SetVectorParameterValue(TEXT("Tint"),Color); }
    Trail->SetReturning(IsObservedReturn(Effect.Look));
    // The body: the look's prop where it has one (a missing asset leaves the plain shape), sized to its real radius.
    const auto Prop=[Impacts](bool bWanted,LDSliceVfx::EMesh Mesh) { return bWanted&&Impacts ? Impacts->PropMesh(Mesh) : nullptr; };
    UStaticMesh* const Takeout=Prop(Effect.Look==ELook::Sock || Effect.Look==ELook::SockReturn,LDSliceVfx::EMesh::PropTakeout);
    UStaticMesh* const Slipper=Prop(Effect.Look==ELook::SlipperDrop,LDSliceVfx::EMesh::PropSlipper);
    UStaticMesh* const BagBody=Prop(bBag,LDSliceVfx::EMesh::PropBag);
    UStaticMesh* const Brick=Prop(bBook,LDSliceVfx::EMesh::PropBrick);
    UStaticMesh* const Flyer=Prop(Effect.Look==ELook::FlyerArming || Effect.Look==ELook::FlyerArmed,LDSliceVfx::EMesh::PropFlyer);
    UStaticMesh* const Tape=Prop(bCordon,LDSliceVfx::EMesh::PropTape);
    UStaticMesh* const BodyProp=Takeout ? Takeout : Slipper ? Slipper : Brick ? Brick : Flyer ? Flyer : Tape;
    // The prop's trim: a label band, a strap, the pocket and straps, the holes and old mortar, the print (the warning's
    // ink), the tape's dark stripes.
    TrimColor=Takeout ? FLinearColor(.8f,.26f,.12f) : Slipper ? FLinearColor(.06f,.26f,.55f) : bBag ? FLinearColor(.28f,.17f,.07f)
        : Brick ? FLinearColor(.2f,.13f,.1f) : Flyer ? FLinearColor(.85f,.16f,.08f) : FLinearColor(.05f,.05f,.05f);
    if (BodyTint) { BodyTint->SetVectorParameterValue(TEXT("Tint"),Color); }
    if (TrimTint) { TrimTint->SetVectorParameterValue(TEXT("Tint"),TrimColor); }
    Chalk->SetMaterial(0,BodyProp&&BodyTint ? BodyTint.Get() : EffectTint.Get());
    Chalk->SetMaterial(1,TrimTint);
    Chalk->SetStaticMesh(BodyProp ? BodyProp : bFlight ? CylinderMesh.Get() : BoxMesh.Get());
    Chalk->SetRelativeLocation(FVector::ZeroVector);
    Chalk->SetRelativeRotation(bFlight&&!Takeout ? FRotator(90,0,0) : FRotator::ZeroRotator);
    Chalk->SetRelativeScale3D(Takeout ? FVector(FMath::Clamp(Effect.RadiusCm/22.f,1.f,2.f)) : bFlight ? FVector(.22,.22,.5)
        : bCone ? FVector(.4,.75,.04) : bArea ? FVector(.38,.38,.04) : Brick ? FVector(1.) : bBook ? FVector(.9,.45,.3)
        : Flyer ? FVector(1.25) : FVector(1.1,.85,.025));
    if (bBook) { Chalk->SetRelativeLocation(FVector(0,0,12)); }
    if (Flyer) { Chalk->SetRelativeLocation(FVector(0,0,-3)); Chalk->SetRelativeRotation(FRotator(0,20,0)); }
    if (bCone) { Chalk->SetRelativeLocation(FVector(Effect.RadiusCm*.35,0,100)); }
    if (bArea)
    {
        if (bDecoy)
        {
            Chalk->SetStaticMesh(CylinderMesh);
            Chalk->SetRelativeLocation(FVector(0,0,88));
            Chalk->SetRelativeScale3D(FVector(.75,.75,1.72));
        }
        else if (Slipper)
        {
            Chalk->SetRelativeLocation(FVector(0,0,-3));
            Chalk->SetRelativeRotation(FRotator(0,25,0));
            Chalk->SetRelativeScale3D(FVector(FMath::Clamp(Effect.RadiusCm/40.f,1.f,1.6f)));
        }
        else if (Effect.Look==ELook::SlipperDrop)
        {
            Chalk->SetRelativeLocation(FVector(0,0,22));
            Chalk->SetRelativeRotation(FRotator(18,25,0));
            Chalk->SetRelativeScale3D(FVector(.46,.24,.16));
        }
        else
        {
            Chalk->SetRelativeLocation(FVector(0,0,7));
        }
    }
    if(bRoad)
    {
        // A cordon is a tape standing on edge between two posts; a road, a line and the wind are flat on the floor.
        Chalk->SetRelativeLocation(FVector(bForwardLine?Effect.LengthCm*.5:0,0,bCordon?64.:1.));
        Chalk->SetRelativeScale3D(Tape ? FVector(Effect.LengthCm/100.,1.,1.)
            : FVector(Effect.LengthCm/100.,bCordon?.03:(Effect.RadiusCm*2./100.),bCordon?.12:.012));
        // The tape hangs from the tops of its posts.
        if (Tape) { Chalk->SetRelativeLocation(FVector(0,0,66)); }
    }
    // The phantom decoy stands as a still copy of the body it left; the plain column only when no body is near.
    if (!bDecoy && StandIn) { StandIn->DestroyComponent(); StandIn=nullptr; }
    if (bDecoy && !StandIn && Impacts) { StandIn=Impacts->StandIn(*this,GetActorLocation(),Color*1.6f,450.f); }
    for (const auto& Segment : ShapeLines) { Segment->SetVisibility(false); }
    int32 Used=0;
    const auto Piece=[&](const FVector& At,const FRotator& Turn,const FVector& Scale)
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
        Segment->SetRelativeLocation(At);
        Segment->SetRelativeRotation(Turn);
        Segment->SetRelativeScale3D(Scale);
        Segment->SetVisibility(true);
    };
    const auto Line=[&](const FVector& A,const FVector& B)
    {
        const FVector Delta=B-A;
        Piece((A+B)*.5,Delta.Rotation(),FVector(Delta.Size()/100.,.04,.02));
    };
    // N4: a bag or a bag wall blocks bodies with a box like the server's blocker (owner prediction agrees with the
    // authority while the obstacle is seen); every other look never collides. The box itself is not drawn: a bag shows
    // its backpack, a wall the two straps pulled tight between its bags.
    Chalk->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Accent->SetStaticMesh(BoxMesh);
    Accent->SetMaterial(0,BagBody&&BodyTint ? BodyTint.Get() : AccentTint.Get());
    Accent->SetMaterial(1,TrimTint);
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
        Chalk->SetVisibility(bBag && !BagBody);
        Accent->SetVisibility(BagBody!=nullptr);
        if (BagBody)
        {
            Accent->SetStaticMesh(BagBody);
            Accent->SetRelativeRotation(FRotator::ZeroRotator);
            Accent->SetRelativeLocation(FVector(0,0,-4));
            Accent->SetRelativeScale3D(FVector(Effect.RadiusCm*2./100.,Effect.RadiusCm*2./100.,Height/120.));
        }
        if (bWall)
        {
            Piece(FVector(0,0,46),FRotator::ZeroRotator,FVector(Effect.LengthCm/100.,.06,.04));
            Piece(FVector(0,0,88),FRotator::ZeroRotator,FVector(Effect.LengthCm/100.,.06,.04));
        }
        Trail->SetVisibility(false,true);
        return;
    }
    // A look whose designed system is its whole picture (an area's floor, the fan's wind, the fist wind's front, a
    // speech cone's voice, the armour's shards) draws no shape of its own; its outline below stays.
    FLinearColor DesignedInk;
    const bool bDesigned=LDSliceVfx::EffectSystemOf(uint8(Effect.Look),DesignedInk)!=LDSliceVfx::ESystem::Count;
    const bool bBodyless=(bDesigned&&!bFlight) || Effect.Look==ELook::FistWind || Effect.Look==ELook::SpeechCone || Effect.Look==ELook::ConeWarning
        || Effect.Look==ELook::ShardBurst || (bDecoy&&StandIn);
    Chalk->SetVisibility(!bBodyless);
    // The accent is a trap's marking while its paper is missing.
    Accent->SetVisibility((Effect.Look==ELook::FlyerArming || Effect.Look==ELook::FlyerArmed) && !Flyer);
    Accent->SetRelativeRotation(FRotator::ZeroRotator);
    Accent->SetRelativeLocation(bCone ? FVector(Effect.RadiusCm*.65,0,110) : FVector(0,0,3));
    Accent->SetRelativeScale3D(bCone ? FVector(.5,.95,.04) : FVector(.8,.12,.02));
    // A flight with its own designed wake (LDSliceVfx::EffectSystemOf) shows that, not the mesh streak.
    // (The streak is the trail's children, and so are the wake's particles: those stay shown.)
    Trail->SetVisibility(bFlight&&!bDesigned,true);
    if (IsValid(Particles)) { Particles->SetVisibility(true); }
    if (bFlight) { return; }
    // Enough segments that a boundary reads as a curve at gameplay zoom.
    constexpr int32 Segments=32;
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
        if (bCordon)
        {
            Piece(FVector(From,0,37),FRotator::ZeroRotator,FVector(.06,.06,.74));
            Piece(FVector(To,0,37),FRotator::ZeroRotator,FVector(.06,.06,.74));
            Piece(FVector(From,0,2),FRotator::ZeroRotator,FVector(.22,.22,.04));
            Piece(FVector(To,0,2),FRotator::ZeroRotator,FVector(.22,.22,.04));
        }
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
        // A new effect on this proxy builds its look afresh.
        ShownRadius = -1;
        if (StandIn) { StandIn->DestroyComponent(); StandIn = nullptr; }
        ShownLook = Projectile.Look;
        PlayBurst(false);
        if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Lockdown/Generated/Materials/M_Character.M_Character")))
        {
            EffectTint = UMaterialInstanceDynamic::Create(Material, this);
            Chalk->SetMaterial(0, EffectTint);
            AccentTint = UMaterialInstanceDynamic::Create(Material, this);
            AccentTint->SetVectorParameterValue(TEXT("Tint"),FLinearColor(.95f,.95f,.9f));
            Accent->SetMaterial(0,AccentTint);
            BodyTint = UMaterialInstanceDynamic::Create(Material, this);
            TrimTint = UMaterialInstanceDynamic::Create(Material, this);
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
    // A look with its own designed system follows the proxy (the flight's body, or the area's centre).
    if (const ULDImpactFxSubsystem* Follow=GetWorld()?GetWorld()->GetSubsystem<ULDImpactFxSubsystem>():nullptr; Follow && IsValid(Particles))
    { Follow->MoveEffect(Particles,IsObservedFlight(ShownLook)&&Trail ? Trail->GetComponentLocation() : GetActorLocation(),ShownHeading); }
    if (IsValid(Particles) && ParticleScale>0.f && !IsObservedFlight(ShownLook))
    {
        const LDHitVfx::FZone Zone=LDHitVfx::ZoneOf(ShownLook,ShownRadius,ParticleScale);
        if (Zone.bShow && !FVector2D(Zone.Velocity).IsNearlyZero())
        { Particles->SetVariableVec3(TEXT("User.Velocity"),ShownHeading.RotateVector(Zone.Velocity)); }
    }
    const double Remaining=WarningAtServerSec>AtServerSec ? WarningAtServerSec-AtServerSec : 0.;
    const float Pulse=Remaining>0. ? .3f+.55f*(.5f+.5f*FMath::Sin(float(AtServerSec)*(Remaining<1. ? 14.f : 7.f))) : 0.f;
    // A trap is a sheet of paper; its marking is the warning ink.
    const bool bFlyer=ShownLook==ELDObservedProjectileLook::FlyerArming || ShownLook==ELDObservedProjectileLook::FlyerArmed;
    if (AccentTint) { AccentTint->SetVectorParameterValue(TEXT("Tint"),FMath::Lerp(bFlyer ? FLinearColor(.85f,.16f,.08f) : ShownColor,FLinearColor::White,Pulse)); }
    // Without the accent the outline carries the warning's pulse, and a trap's print with it.
    if (EffectTint && !Accent->IsVisible()) { EffectTint->SetVectorParameterValue(TEXT("Tint"),FMath::Lerp(ShownColor,FLinearColor::White,Pulse*.6f)); }
    if (TrimTint && bFlyer) { TrimTint->SetVectorParameterValue(TEXT("Tint"),FMath::Lerp(TrimColor,FLinearColor::White,Pulse)); }
    // A cordon's tape stirs a little on its posts.
    if (ShownLook==ELDObservedProjectileLook::CordonTape) { Chalk->SetRelativeRotation(FRotator(0,0,7.f*FMath::Sin(float(AtServerSec)*4.3f))); }
    // The dropped brick falls through its warning, tumbling, and lies where it lands.
    if (ShownLook==ELDObservedProjectileLook::BookShadow)
    {
        const double Left=FMath::Min(Remaining,1.2);
        Chalk->SetRelativeLocation(FVector(0,0,12.+Left*Left*900.));
        Chalk->SetRelativeRotation(FRotator(Left*140.,Left*90.,0));
    }
    if (Pose.Reached > 0) { Points.RemoveAt(0, Pose.Reached); }
}

void ALDObservedProjectileProxy::PlayBurst(bool bEnd)
{
    if (bEnd && (!bPlaced || !bNaturalEnd)) { return; }
    LDHitVfx::EBurst Kind;
    FLinearColor Tint;
    ULDImpactFxSubsystem* Impacts=GetWorld()?GetWorld()->GetSubsystem<ULDImpactFxSubsystem>():nullptr;
    if (!Impacts || (!bEnd && IsHidden())) { return; }
    // Looks with a designed appear or end of their own: the armour's shards, the decoy going off, the brick and the bag
    // landing in dust. A slipper, a tape and a bag wall simply stand there.
    {
        const FVector Floor=Impacts->FloorUnder(GetActorLocation()),Chest=Floor+FVector(0,0,95);
        LDSliceVfx::FLook Look;
        Look.EffectScale=Impacts->LocalEffectScale();
        switch (ShownLook)
        {
        case ELDObservedProjectileLook::ShardBurst:
            if (!bEnd) { Impacts->PlayFree(LDSliceVfx::ESystem::ShardBurst,Floor,Chest,FLinearColor::White,FMath::Max(ShownRadius,80.f),1.2f,Look); }
            return;
        case ELDObservedProjectileLook::PhantomDecoy:
            if (bEnd) { Impacts->PlayFree(LDSliceVfx::ESystem::DecoyPop,Floor,Chest,FLinearColor::White,FMath::Max(ShownRadius,80.f),1.2f,Look); }
            return;
        case ELDObservedProjectileLook::BookShadow:
            if (bEnd) { Impacts->PlayPack(LDSliceVfx::EPack::DustRing,Floor+FVector(0,0,4),.55f); }
            return;
        case ELDObservedProjectileLook::Bag:
            if (!bEnd) { Impacts->PlayPack(LDSliceVfx::EPack::DustRing,Floor+FVector(0,0,4),.4f); }
            return;
        case ELDObservedProjectileLook::BagWall:
        case ELDObservedProjectileLook::SlipperDrop:
        case ELDObservedProjectileLook::CordonTape: return;
        default: break;
        }
    }
    // A look with its own designed system has no particle-kit burst.
    if (FLinearColor Ink; LDSliceVfx::EffectSystemOf(uint8(ShownLook),Ink)!=LDSliceVfx::ESystem::Count) { return; }
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

void ALDWorldViewRelay::FreezeForWorldTeardown()
{
    bFrozenForTeardown = true;
    bFogBlending = false;
    SetActorTickEnabled(false);
    if (FogTexture && FogTexture->GetResource())
    {
        FlushRenderingCommands();
    }
}

bool ALDWorldViewRelay::IsShuttingDown() const
{
    const UWorld* World = GetWorld();
    const auto* GI = World ? World->GetGameInstance<ULDGameInstance>() : nullptr;
    return bFrozenForTeardown || !World || World->bIsTearingDown || (GI && GI->IsLeavingWorld(World));
}

void ALDWorldViewRelay::EndPlay(const EEndPlayReason::Type Reason)
{
    if (FogMaterial) { FogMaterial->SetScalarParameterValue(TEXT("CoverageActive"), 0); }
    GroundFog.Reset();
    FreezeForWorldTeardown();
    TArray<FLDObservedClear> Cleared;
    Receiver.Reset(ELDObservedClearReason::Disconnected, Cleared);
    LastClears = Cleared;
    for (auto& Pair : Proxies) { if (IsValid(Pair.Value)) { Pair.Value->Destroy(); } }
    Proxies.Empty();
    for (auto& Pair : ProjectileProxies) { if (IsValid(Pair.Value)) { Pair.Value->Destroy(); } }
    ProjectileProxies.Empty();
    for (auto& Pair : GroundProxies) { if (IsValid(Pair.Value)) { Pair.Value->Destroy(); } }
    GroundProxies.Empty();
    for (auto& Pair : RecoveryOrbProxies) { if (IsValid(Pair.Value)) { Pair.Value->Destroy(); } }
    RecoveryOrbProxies.Empty();
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
    if (IsShuttingDown()) { return; }
    PeakViewWireBytes = FMath::Max(PeakViewWireBytes, View.ReceivedWireBytes);
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
    if (IsShuttingDown() || !bClock) { return; }
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
    if (IsShuttingDown()) { return; }
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
    if (IsShuttingDown()) { return; }
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
    TSet<uint64> Orbs;
    if (bCurrent)
    {
        const double OrbRadiusCm = Catalog.RecoveryPackRadiusCm;
        FString OrbError;
        for (const FLDObservedRecoveryOrb& Orb : Snapshot.Frame->RecoveryOrbs)
        {
            Orbs.Add(Orb.Serial);
            TObjectPtr<ALDRecoveryOrbMarker>& Marker = RecoveryOrbProxies.FindOrAdd(Orb.Serial);
            if (IsValid(Marker) && Marker->GetGeneration() != Snapshot.Frame->MatchGeneration)
            {
                Marker->Destroy();
                Marker = nullptr;
            }
            if (!IsValid(Marker))
            {
                FActorSpawnParameters Params;
                Params.ObjectFlags |= RF_Transient;
                Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
                Marker = GetWorld()->SpawnActor<ALDRecoveryOrbMarker>(Params);
                if (!Marker || !Marker->Build(FVector(Orb.PositionCm), OrbRadiusCm, Orb.Serial, Snapshot.Frame->MatchGeneration, OrbError))
                {
                    if (Marker) { Marker->Destroy(); }
                    Marker = nullptr;
                    UE_LOG(LogLDWorld, Warning, TEXT("LD_RECOVERY_ORB_MARKER_FAILED serial=%llu %s"), Orb.Serial, *OrbError);
                    Orbs.Remove(Orb.Serial);
                }
            }
        }
    }
    for (auto It = RecoveryOrbProxies.CreateIterator(); It; ++It)
    {
        if (Orbs.Contains(It.Key()) && IsValid(It.Value())) { continue; }
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
        Camera->PostProcessSettings.bOverride_AutoExposureMethod = true;
        Camera->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
        Camera->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
        Camera->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
        Camera->PostProcessSettings.bOverride_AutoExposureBias = true;
        Camera->PostProcessSettings.AutoExposureBias = 0.0f;
        Camera->PostProcessSettings.bOverride_AutoExposureMinBrightness = true;
        Camera->PostProcessSettings.bOverride_AutoExposureMaxBrightness = true;
        Camera->PostProcessSettings.AutoExposureMinBrightness = 1.0f;
        Camera->PostProcessSettings.AutoExposureMaxBrightness = 1.0f;
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
        FogBlendRegion = FIntRect();
        bFogBlending = false;
        return;
    }
    const FBox2D& Bounds = GroundFog->Sight.CoverageBounds();
    const FVector2D Span=Bounds.GetSize();
    FogMaterial->SetVectorParameterValue(TEXT("FieldWorldMin"), FLinearColor(Bounds.Min.X, Bounds.Min.Y, 0, 0));
    FogMaterial->SetVectorParameterValue(TEXT("FieldWorldSpan"), FLinearColor(Span.X, Span.Y, 0, 0));
    LDFogDistanceField::Build(*Bits, Span.X / LDObservation::CoverageWidth, Span.Y / LDObservation::CoverageHeight, FogTarget);
    if (FogShown.Num() != FogTarget.Num())
    {
        // Nothing was shown yet: the whole texture takes the target once.
        FogShown = FogTarget;
        FogBlendRegion = FIntRect(0, 0, LDObservation::CoverageWidth, LDObservation::CoverageHeight);
    }
    else
    {
        // Cells already at the new target keep their pixel; the blend covers the ones this walk changed and the ones
        // still easing from the walk before.
        const FIntRect Changed = LDFogDistanceField::Difference(FogShown, FogTarget);
        if (Changed.Area() > 0)
        {
            if (FogBlendRegion.Area() > 0) { FogBlendRegion.Union(Changed); } else { FogBlendRegion = Changed; }
        }
    }
    bFogBlending = FogBlendRegion.Area() > 0;
}

void ALDWorldViewRelay::BlendFog(float Delta)
{
    if (!bFogBlending || !FogTexture || !FogTexture->GetResource() || FogShown.Num() != LDObservation::CoverageCells
        || FogTarget.Num() != LDObservation::CoverageCells) { return; }
    const float Alpha = 1.f - FMath::Exp(-FMath::Max(Delta, 0.f) / .06f);
    // A walking viewer changes the field around the body many times a second and the blend runs on every frame between:
    // only the rectangle still in motion is recomputed and uploaded, never the whole map-sized texture.
    const FIntRect Region = FogBlendRegion;
    if (Region.Area() <= 0) { bFogBlending = false; return; }
    uint8* Pixels = new uint8[SIZE_T(Region.Area()) * 4];
    FogBlendRegion = LDFogDistanceField::Blend(FogTarget, FogShown, Region, Alpha, Pixels);
    FogTexture->UpdateTextureRegions(0, 1,
        new FUpdateTextureRegion2D(Region.Min.X, Region.Min.Y, 0, 0, Region.Width(), Region.Height()), Region.Width() * 4, 4, Pixels,
        [](uint8* Data, const FUpdateTextureRegion2D* Regions) { delete[] Data; delete Regions; });
    bFogBlending = FogBlendRegion.Area() > 0;
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
    const UWorld* World = GetWorld();
    if (bFrozenForTeardown || !World || World->bIsTearingDown) { return; }
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
        if (!IsValid(Actor) || Actor->IsEditorOnly()) { continue; }
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
        const AActor* Actor = Primitive ? Primitive->GetOwner() : nullptr;
        if (!Actor || Actor->IsHidden() || Primitive->bHiddenInGame || !Primitive->IsVisible()) { continue; }
        if (!Prop.bResolved)
        {
            // Fading is an opt-in material effect. Imported opaque materials keep their authored visibility and shadow.
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
                UE_LOG(LogLDWorld, Verbose, TEXT("LD_FADEABLE_PARAMETER_MISSING %s keeps its authored visibility and shadow."),
                    *Primitive->GetPathName());
            }
        }
        if (!Prop.bParameter) { continue; }
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
        if (Next == Prop.Fade) { continue; }
        Prop.Fade = Next;
        // A faded prop's shadow would still darken the bodies it no longer hides; it returns with the opaque prop.
        const bool bShadow = Prop.bCastShadow && Prop.Fade <= 0.f;
        if (Primitive->CastShadow != bShadow) { Primitive->SetCastShadow(bShadow); }
        EnsureMaterials(Prop, *Primitive);
        for (const TWeakObjectPtr<UMaterialInstanceDynamic>& Material : Prop.Materials)
        {
            if (UMaterialInstanceDynamic* Dynamic = Material.Get())
            { Dynamic->SetScalarParameterValue(LDPropFade::Parameter, Prop.Fade); }
        }
    }
}
