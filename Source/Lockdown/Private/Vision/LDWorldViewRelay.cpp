#include "Vision/LDWorldViewRelay.h"
#include "Character/LDHumanoidVisualComponent.h"
#include "Character/LDCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDDemoEntry.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDemoContentIds.h"
#include "Region/LDMapRegistry.h"
#include "Region/LDMapGeometry.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"

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
    for (const FLDDemoEquipmentDefinition& Item : Definitions.Equipment) { Equipment.Add(Item.NativeId); }
    for (const FLDDemoMonsterDefinition& Monster : Definitions.Monsters) { Creatures.Add(Monster.NativeId); }
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
    for (const uint8 Slot : Unit.Equipment.Slots)
    {
        if (Slot != 0 && !Equipment.Contains(Slot))
        {
            Error = TEXT("Observed equipment is not an approved public definition.");
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
    if (Status != ELDObservedViewStatus::Current || LastFullAt < 0 || NowSeconds - LastFullAt <= StaleAfterSeconds) { return false; }
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

double LDObservedPresentation::TrackOffset(bool bHasOffset, double Offset, double SecondsSinceUpdate, double ArrivalLocalSeconds, double SampledAtServerSeconds)
{
    const double Candidate = ArrivalLocalSeconds - SampledAtServerSeconds;
    if (!bHasOffset || Candidate <= Offset || Candidate - Offset > ReseedSeconds) { return Candidate; }
    return FMath::Min(Candidate, Offset + FMath::Max(0.0, SecondsSinceUpdate) * OffsetGrowthPerSecond);
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
    Shown.Statuses = Unit.Statuses;
    // A presentation clock that stalls keeps only a bounded queue: the oldest samples are dropped first.
    while (Points.Num() > 8) { Points.RemoveAt(0); Facts.RemoveAt(0); }
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
    const double Scale = !bCamp ? 1.0 : Unit.CreatureId == uint8(ELDDemoMonsterId::MonsterGuardian) ? 1.3 : .8;
    Body->SetRelativeScale3D(FVector(Scale));
}

void ALDObservedEntityProxy::Present(double AtServerSec, float Delta)
{
    if (Points.IsEmpty()) { return; }
    const LDObservedPresentation::FPose Pose = LDObservedPresentation::Sample(Points, AtServerSec);
    SetActorLocationAndRotation(Pose.Location, FRotator(0, Pose.Yaw, 0));
    if (Pose.Reached != INDEX_NONE && Points[Pose.Reached].At > ShownAt)
    {
        // The same cue byte as the owner's own body: action pose plus one marker shape per public status, shown when
        // the clock reaches the sample that carried them.
        const LDObservedPresentation::FFacts& Reached = Facts[Pose.Reached];
        Body->SetCue(ALDCharacter::PresentationCue(Reached.Cue, Reached.Statuses));
        ShownAt = Points[Pose.Reached].At;
    }
    if (Pose.Reached > 0)
    {
        Points.RemoveAt(0, Pose.Reached);
        Facts.RemoveAt(0, Pose.Reached);
    }
    Body->Animate(Pose.Velocity.GetClampedToMaxSize(1000.0), Delta);
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
        AttachToActor(Body, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
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

ALDObservedProjectileProxy::ALDObservedProjectileProxy()
{
    bReplicates = false;
    // Posed by the relay on the same presentation clock as the units.
    PrimaryActorTick.bCanEverTick = false;
    SetActorEnableCollision(false);
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder"));
    Chalk = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ObservedChalk"));
    Chalk->SetupAttachment(RootComponent);
    Chalk->SetStaticMesh(Cylinder.Object);
    Chalk->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Chalk->SetCanEverAffectNavigation(false);
    // An exaggerated chalk stub lying along the flight direction (+X), readable from the gameplay camera at 720p.
    Chalk->SetRelativeRotation(FRotator(90, 0, 0));
    Chalk->SetRelativeScale3D(FVector(.22, .22, .5));
}

void ALDObservedProjectileProxy::Apply(const FLDObservedProjectileView& Projectile, double SampledAtServerSec)
{
    // The flight is drawn at chest height over its authorized ground point.
    LDObservedPresentation::FPoint Point;
    Point.At = SampledAtServerSec;
    Point.Location = FVector(Projectile.PositionCm) + FVector(0, 0, 100);
    Point.Yaw = float(Projectile.Heading) / 65536.f * 360.f;
    if (!bPlaced || ObservedHandle != Projectile.Handle)
    {
        Points.Reset();
        SetActorLocationAndRotation(Point.Location, FRotator(0, Point.Yaw, 0));
        bPlaced = true;
        if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Lockdown/Generated/Materials/M_Character.M_Character")))
        {
            UMaterialInstanceDynamic* Tint = UMaterialInstanceDynamic::Create(Material, this);
            Tint->SetVectorParameterValue(TEXT("Tint"), FLinearColor(.95f, .95f, .9f));
            Chalk->SetMaterial(0, Tint);
        }
        else { ensureMsgf(false, TEXT("Character material missing; rerun asset bootstrap")); }
    }
    else if (!Points.IsEmpty() && SampledAtServerSec <= Points.Last().At) { return; }
    ObservedHandle = Projectile.Handle;
    EndedAt = -1;
    Point.bSnap = Points.IsEmpty();
    Points.Add(Point);
    while (Points.Num() > 8) { Points.RemoveAt(0); }
}

void ALDObservedProjectileProxy::Present(double AtServerSec)
{
    if (Points.IsEmpty()) { return; }
    const LDObservedPresentation::FPose Pose = LDObservedPresentation::Sample(Points, AtServerSec);
    SetActorLocationAndRotation(Pose.Location, FRotator(0, Pose.Yaw, 0));
    if (Pose.Reached > 0) { Points.RemoveAt(0, Pose.Reached); }
}

void ALDObservedProjectileProxy::End(double LocalSeconds)
{
    if (EndedAt < 0) { EndedAt = LocalSeconds; }
}

bool ALDObservedProjectileProxy::IsFinished(double AtServerSec, double LocalSeconds) const
{
    return EndedAt >= 0 && (Points.IsEmpty() || AtServerSec >= Points.Last().At || LocalSeconds - EndedAt >= EndSeconds);
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
    TArray<FLDObservedClear> Cleared;
    Receiver.Reset(ELDObservedClearReason::Disconnected, Cleared);
    LastClears = Cleared;
    for (auto& Pair : Proxies) { if (IsValid(Pair.Value)) { Pair.Value->Destroy(); } }
    Proxies.Empty();
    for (auto& Pair : ProjectileProxies) { if (IsValid(Pair.Value)) { Pair.Value->Destroy(); } }
    ProjectileProxies.Empty();
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
    UE_LOG(LogTemp, Error, TEXT("LD_OBSERVATION_PROTOCOL_REJECTED %s"), *Error);
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
        // Only an accepted frame advances the presentation clock.
        ClockOffset = LDObservedPresentation::TrackOffset(bClock, ClockOffset, Arrival - ClockUpdatedAt, Arrival, View.SampledAtServerSec);
        bClock = true;
        ClockUpdatedAt = Arrival;
    }
    Present(Cleared);
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
        if (Proxies.RemoveAndCopyValue(Clear.Handle, Proxy) && IsValid(Proxy)) { Proxy->Destroy(); }
    }
    SyncProxies();
    UploadFog();
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
    const double Local = FPlatformTime::Seconds();
    for (auto It = ProjectileProxies.CreateIterator(); It; ++It)
    {
        if (Flying.Contains(It.Key())) { continue; }
        // A current frame that no longer lists a flight lets it finish its authorized samples; any other view ends it now.
        if (bCurrent && IsValid(It.Value())) { It.Value()->End(Local); continue; }
        if (IsValid(It.Value())) { It.Value()->Destroy(); }
        It.RemoveCurrent();
    }
}

void ALDWorldViewRelay::PresentProxies(float Delta)
{
    const double Local = FPlatformTime::Seconds();
    const double At = PresentationAt(Local);
    for (auto& Pair : Proxies) { if (IsValid(Pair.Value)) { Pair.Value->Present(At, Delta); } }
    for (auto It = ProjectileProxies.CreateIterator(); It; ++It)
    {
        ALDObservedProjectileProxy* Flight = It.Value();
        if (IsValid(Flight)) { Flight->Present(At); }
        if (IsValid(Flight) && !Flight->IsFinished(At, Local)) { continue; }
        if (IsValid(Flight)) { Flight->Destroy(); }
        It.RemoveCurrent();
    }
}
void ALDWorldViewRelay::AttachFog()
{
    const auto* Owning = Cast<ALDPlayerController>(GetOwner());
    AActor* Target = Owning ? Owning->GetViewTarget() : nullptr;
    UCameraComponent* Camera = Target ? Target->FindComponentByClass<UCameraComponent>() : nullptr;
    if (!Owning || !Owning->GetLocalPlayer() || !Camera || CameraOwner == Target) { return; }
    const int32 Side = LDObservation::CoverageSide;
    if (!FogMaterial)
    {
        UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Lockdown/Generated/Materials/M_Fog.M_Fog"));
        if (!ensureMsgf(Base, TEXT("Missing native fog material; regenerate content"))) { return; }
        FogTexture = UTexture2D::CreateTransient(Side, Side, PF_B8G8R8A8);
        FogTexture->SRGB = false;
        // Bilinear sampling softens the coverage cell edges; with the blend the lit area glides instead of stepping.
        FogTexture->Filter = TF_Bilinear;
        FogTexture->AddressX = TA_Clamp;
        FogTexture->AddressY = TA_Clamp;
        FogTexture->NeverStream = true;
        void* Pixels = FogTexture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
        FMemory::Memzero(Pixels, Side * Side * 4);
        FogTexture->GetPlatformData()->Mips[0].BulkData.Unlock();
        FogTexture->UpdateResource();
        FogMaterial = UMaterialInstanceDynamic::Create(Base, this);
        FogMaterial->SetTextureParameterValue(TEXT("FogMask"), FogTexture);
    }
    Camera->PostProcessSettings.AddBlendable(FogMaterial, 1);
    CameraOwner = Target;
    FogSerial = MAX_uint64;
    UploadFog();
}

void ALDWorldViewRelay::UploadFog()
{
    if (!FogTexture || !FogTexture->GetResource() || !FogMaterial) { return; }
    const FLDObservedFrameRead Snapshot = Receiver.Read();
    if (Snapshot.LocalSerial == FogSerial) { return; }
    const auto* Registry = GetWorld() ? GetWorld()->GetSubsystem<ULDMapRegistry>() : nullptr;
    const FLDDemoWorldGeometry* Geometry = Registry && Registry->Enabled() ? Registry->GetGeometry() : nullptr;
    FVector GroundMin, GroundMax;
    FString Error;
    if (!Geometry || Geometry->Surfaces.Num() != 1 || !Geometry->Surfaces[0].MinCm.ToVector(GroundMin, Error)
        || !Geometry->Surfaces[0].MaxCm.ToVector(GroundMax, Error) || GroundMax.X <= GroundMin.X || GroundMax.Y <= GroundMin.Y) { return; }
    const double SpanX = GroundMax.X - GroundMin.X, SpanY = GroundMax.Y - GroundMin.Y;
    FogMaterial->SetVectorParameterValue(TEXT("WorldScale"), FLinearColor(float(1.0 / SpanX), float(1.0 / SpanY), 0, 0));
    FogMaterial->SetVectorParameterValue(TEXT("WorldOffset"), FLinearColor(float(-GroundMin.X / SpanX), float(-GroundMin.Y / SpanY), 0, 0));
    // Static terrain stays readable; only current authorized coverage is bright. No explored layer exists.
    const bool bCurrent = Snapshot.Frame && Snapshot.Status == ELDObservedViewStatus::Current
        && Snapshot.Frame->Footprint.SourceStatus == ELDObservationSource::ActiveBody;
    FogTarget.SetNumUninitialized(LDObservation::CoverageCells);
    for (int32 Index = 0; Index < LDObservation::CoverageCells; ++Index)
    {
        FogTarget[Index] = bCurrent && LDObservation::CoverageBit(Snapshot.Frame->Footprint.CurrentBits, Index) ? 255 : 64;
    }
    // The first coverage shows at once; later ones blend from what is on screen.
    if (FogShown.Num() != LDObservation::CoverageCells)
    {
        FogShown.SetNumUninitialized(LDObservation::CoverageCells);
        for (int32 Index = 0; Index < LDObservation::CoverageCells; ++Index) { FogShown[Index] = FogTarget[Index]; }
    }
    bFogBlending = true;
    FogSerial = Snapshot.LocalSerial;
}

void ALDWorldViewRelay::BlendFog(float Delta)
{
    if (!bFogBlending || !FogTexture || !FogTexture->GetResource() || FogShown.Num() != LDObservation::CoverageCells
        || FogTarget.Num() != LDObservation::CoverageCells) { return; }
    // One publication interval per full fade: a cell entering or leaving coverage eases over about 0.1 s.
    const float Step = (255.f - 64.f) * FMath::Max(Delta, 0.f) / .1f;
    bool bMoving = false;
    const int32 Side = LDObservation::CoverageSide;
    uint8* Pixels = new uint8[LDObservation::CoverageCells * 4];
    for (int32 Index = 0; Index < LDObservation::CoverageCells; ++Index)
    {
        const float Target = FogTarget[Index];
        float& Shown = FogShown[Index];
        Shown = Shown < Target ? FMath::Min(Target, Shown + Step) : FMath::Max(Target, Shown - Step);
        bMoving |= Shown != Target;
        const uint8 Value = uint8(FMath::RoundToInt(Shown));
        Pixels[Index * 4] = Pixels[Index * 4 + 1] = Pixels[Index * 4 + 2] = Value;
        Pixels[Index * 4 + 3] = 255;
    }
    FogTexture->UpdateTextureRegions(0, 1, new FUpdateTextureRegion2D(0, 0, 0, 0, Side, Side), Side * 4, 4, Pixels,
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
    // A reset or timeout clears units and flights alike, even when no unit was presented.
    const bool bTimedOut = Receiver.Tick(FPlatformTime::Seconds(), Cleared);
    if (bReset || bTimedOut || Cleared.Num() > 0) { Present(Cleared); }
    else { UploadFog(); }
    PresentProxies(Delta);
    BlendFog(Delta);
}
