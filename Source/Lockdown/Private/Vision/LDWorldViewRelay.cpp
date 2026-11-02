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
ALDObservedEntityProxy::ALDObservedEntityProxy()
{
    bReplicates = false;
    PrimaryActorTick.bCanEverTick = true;
    SetActorEnableCollision(false);
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    Body = CreateDefaultSubobject<ULDHumanoidVisualComponent>(TEXT("ObservedBody"));
    Body->SetupAttachment(RootComponent);
}

void ALDObservedEntityProxy::Apply(const FLDObservedUnitView& Unit, double VisualHalfHeightCm)
{
    const FVector Next = FVector(Unit.FeetCm) + FVector(0, 0, VisualHalfHeightCm);
    const double Now = FPlatformTime::Seconds();
    if (!bPlaced || ObservedHandle != Unit.Handle || ObservedEpoch != Unit.ObservationEpoch
        || FVector::DistSquared(GetActorLocation(), Next) > FMath::Square(300.0))
    {
        SetActorLocation(Next);
        DisplayVelocity = FVector::ZeroVector;
        bPlaced = true;
    }
    else if (LastApplyAt >= 0 && Now > LastApplyAt)
    {
        DisplayVelocity = ((Next - Destination) / (Now - LastApplyAt)).GetClampedToMaxSize(1000.0);
    }
    Destination = Next;
    LastApplyAt = Now;
    ObservedHandle = Unit.Handle;
    ObservedEpoch = Unit.ObservationEpoch;
    FacingDegrees = float(Unit.Facing) / 65536.f * 360.f;
    const bool bCamp = Unit.Kind == ELDObservedUnitKind::CampMember;
    const uint8 NextPalette = bCamp ? uint8(1) : uint8(Unit.Presentation % 4);
    if (Palette != NextPalette)
    {
        Palette = NextPalette;
        Body->ApplyPalette(Palette);
    }
    const double Scale = !bCamp ? 1.0 : Unit.CreatureId == uint8(ELDDemoMonsterId::MonsterGuardian) ? 1.3 : .8;
    Body->SetRelativeScale3D(FVector(Scale));
    Body->SetCue(0);
}

void ALDObservedEntityProxy::Tick(float Delta)
{
    Super::Tick(Delta);
    SetActorLocation(FMath::VInterpTo(GetActorLocation(), Destination, Delta, 18.f));
    SetActorRotation(FRotator(0, FacingDegrees, 0));
    Body->Animate(DisplayVelocity, Delta);
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
    if (Receiver.AcceptFull(View, ExpectedRevision, Catalog, FPlatformTime::Seconds(), Cleared, Error) == FLDObservedFrameReceiver::EResult::Rejected)
    {
        Refuse(Error);
        return;
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

void ALDWorldViewRelay::SyncProxies()
{
    const FLDObservedFrameRead Snapshot = Receiver.Read();
    TSet<FGuid> Shown;
    if (Snapshot.Frame && Snapshot.Status == ELDObservedViewStatus::Current)
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
            if (IsValid(Proxy)) { Proxy->Apply(Unit, HalfHeight); }
        }
    }
    for (auto It = Proxies.CreateIterator(); It; ++It)
    {
        if (Shown.Contains(It.Key())) { continue; }
        if (IsValid(It.Value())) { It.Value()->Destroy(); }
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
        FogTexture->Filter = TF_Nearest;
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
    const int32 Side = LDObservation::CoverageSide;
    uint8* Pixels = new uint8[LDObservation::CoverageCells * 4];
    for (int32 Index = 0; Index < LDObservation::CoverageCells; ++Index)
    {
        const uint8 Value = bCurrent && LDObservation::CoverageBit(Snapshot.Frame->Footprint.CurrentBits, Index) ? 255 : 64;
        Pixels[Index * 4] = Pixels[Index * 4 + 1] = Pixels[Index * 4 + 2] = Value;
        Pixels[Index * 4 + 3] = 255;
    }
    FogTexture->UpdateTextureRegions(0, 1, new FUpdateTextureRegion2D(0, 0, 0, 0, Side, Side), Side * 4, 4, Pixels,
        [](uint8* Data, const FUpdateTextureRegion2D* Regions) { delete[] Data; delete Regions; });
    FogSerial = Snapshot.LocalSerial;
}

void ALDWorldViewRelay::Tick(float Delta)
{
    Super::Tick(Delta);
    const auto* Owning = Cast<ALDPlayerController>(GetOwner());
    // Present only for this machine's real viewer: never for a server-side opponent or a remote client's server copy.
    if (!Owning || !Owning->GetLocalPlayer()) { return; }
    AttachFog();
    TArray<FLDObservedClear> Cleared;
    const uint32 OwnerMatch = Owning->OwnerState.Match;
    if (OwnerMatch != 0 && Receiver.CursorMatch() != 0 && OwnerMatch != Receiver.CursorMatch()
        && Receiver.Read().Status != ELDObservedViewStatus::Unavailable)
    {
        Receiver.Reset(ELDObservedClearReason::MatchReset, Cleared);
    }
    Receiver.Tick(FPlatformTime::Seconds(), Cleared);
    if (Cleared.Num() > 0) { Present(Cleared); }
    else { UploadFog(); }
}
