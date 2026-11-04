#include "Vision/LDViewTypes.h"
#include "Development/LDDemoContentIds.h"

namespace
{
constexpr double MaxObservedCoordinateCm = 1000000.0;

bool FailObservation(FString& Error, const TCHAR* Message)
{
    Error = Message;
    return false;
}

bool IsFiniteObservedPoint(const FVector& Point)
{
    return FMath::IsFinite(Point.X) && FMath::IsFinite(Point.Y) && FMath::IsFinite(Point.Z)
        && FMath::Abs(Point.X) <= MaxObservedCoordinateCm && FMath::Abs(Point.Y) <= MaxObservedCoordinateCm && FMath::Abs(Point.Z) <= MaxObservedCoordinateCm;
}

bool IsPublicStatusId(uint8 Id)
{
    switch (static_cast<ELDDemoStatusId>(Id))
    {
    case ELDDemoStatusId::Root:
    case ELDDemoStatusId::Shield:
    case ELDDemoStatusId::CampReturning:
    case ELDDemoStatusId::RespawnProtection:
    case ELDDemoStatusId::Slow:
    case ELDDemoStatusId::StatBoost:
    case ELDDemoStatusId::Exposed:
    case ELDDemoStatusId::Disarm:
    case ELDDemoStatusId::Stun:
        return true;
    default:
        return false;
    }
}

bool IsPublicEquipmentOrEmpty(uint8 Id)
{
    if (Id == 0) { return true; }
    switch (static_cast<ELDDemoEquipmentId>(Id))
    {
    case ELDDemoEquipmentId::ItemIronPlate:
    case ELDDemoEquipmentId::ItemEdgeBlade:
    case ELDDemoEquipmentId::ItemRunicFocus:
    case ELDDemoEquipmentId::ItemSiphonEmblem:
    case ELDDemoEquipmentId::ItemPhaseAnchor:
    case ELDDemoEquipmentId::ItemArcaneBattery:
    case ELDDemoEquipmentId::ItemEchoMaul:
    case ELDDemoEquipmentId::ItemSlingshot:
    case ELDDemoEquipmentId::ItemPaperBox:
    case ELDDemoEquipmentId::ItemNailUniform:
        return true;
    default:
        return false;
    }
}

bool IsPublicCreatureId(uint8 Id)
{
    switch (static_cast<ELDDemoMonsterId>(Id))
    {
    case ELDDemoMonsterId::MonsterScavenger:
    case ELDDemoMonsterId::MonsterGuardian:
        return true;
    default:
        return false;
    }
}

bool IsObservedResourcePair(const FLDObservedResource& Resource, bool RequirePositiveMax)
{
    return Resource.MaxP2 >= (RequirePositiveMax ? 1 : 0) && Resource.CurrentP2 >= 0 && Resource.CurrentP2 <= Resource.MaxP2;
}

bool IsEmptyObservedResource(const FLDObservedResource& Resource)
{
    return Resource.CurrentP2 == 0 && Resource.MaxP2 == 0;
}

bool IsObservedMapIdCharacter(TCHAR Character, bool First)
{
    return (Character >= TEXT('a') && Character <= TEXT('z'))
        || (!First && ((Character >= TEXT('0') && Character <= TEXT('9')) || Character == TEXT('_')));
}

bool IsValidObservedMapId(const FString& Text)
{
    if (Text.Len() < 1 || Text.Len() > LDObservation::MaxMapIdBytes) { return false; }
    for (int32 Index = 0; Index < Text.Len(); ++Index)
    {
        if (!IsObservedMapIdCharacter(Text[Index], Index == 0)) { return false; }
    }
    return true;
}

bool SerializeObservedMapId(FArchive& Ar, FName& Id)
{
    ANSICHAR Text[LDObservation::MaxMapIdBytes] = {};
    uint8 Length = 0;
    if (Ar.IsSaving())
    {
        const FString Lower = Id.ToString().ToLower();
        if (!IsValidObservedMapId(Lower)) { return false; }
        Length = uint8(Lower.Len());
        for (int32 Index = 0; Index < Lower.Len(); ++Index) { Text[Index] = ANSICHAR(Lower[Index]); }
    }
    Ar << Length;
    if (Ar.IsError() || Length == 0 || Length > LDObservation::MaxMapIdBytes) { return false; }
    Ar.Serialize(Text, Length);
    if (Ar.IsError()) { return false; }
    if (Ar.IsLoading())
    {
        FString Loaded;
        Loaded.Reserve(Length);
        for (int32 Index = 0; Index < Length; ++Index) { Loaded.AppendChar(TCHAR(uint8(Text[Index]))); }
        if (!IsValidObservedMapId(Loaded)) { return false; }
        Id = FName(*Loaded);
    }
    return true;
}

bool SerializeObservedUnit(FArchive& Ar, UPackageMap* Map, FLDObservedUnitView& Unit)
{
    if (Ar.IsLoading()) { Unit = FLDObservedUnitView(); }
    uint8 Kind = uint8(Unit.Kind);
    uint8 Cue = uint8(Unit.ActionCue);
    Ar << Unit.Handle << Unit.ObservationEpoch << Unit.StateRevision << Unit.AuthorizedFieldMask << Kind << Unit.Presentation;
    bool bFeet = true;
    Unit.FeetCm.NetSerialize(Ar, Map, bFeet);
    Ar << Unit.Facing << Cue << Unit.ActionCueAgeMs << Unit.ActionAbility << Unit.ActionEquipment;
    if (Ar.IsLoading())
    {
        Unit.Kind = static_cast<ELDObservedUnitKind>(Kind);
        Unit.ActionCue = static_cast<ELDObservedActionCue>(Cue);
    }
    if (!bFeet || Ar.IsError()
        || (Kind != uint8(ELDObservedUnitKind::Contender) && Kind != uint8(ELDObservedUnitKind::CampMember))) { return false; }
    const uint32 Mask = Unit.AuthorizedFieldMask;
    if (Mask & LDObservation::Identity)
    {
        if (Kind == uint8(ELDObservedUnitKind::Contender)) { Ar << Unit.ContenderId; }
        else { Ar << Unit.CreatureId; }
    }
    if (Mask & LDObservation::Level) { Ar << Unit.Level; }
    if (Mask & LDObservation::Health) { Ar << Unit.Health.CurrentP2 << Unit.Health.MaxP2; }
    if (Mask & LDObservation::Mana) { Ar << Unit.Mana.CurrentP2 << Unit.Mana.MaxP2; }
    if (Mask & LDObservation::Statuses)
    {
        uint8 Count = uint8(FMath::Min(Unit.Statuses.Num(), 255));
        Ar << Count;
        if (Ar.IsError() || Count > LDObservation::MaxStatuses) { return false; }
        if (Ar.IsLoading()) { Unit.Statuses.SetNum(Count); }
        for (FLDObservedStatus& Status : Unit.Statuses) { Ar << Status.StatusId << Status.Stacks << Status.ExpiresAtServerMs; }
    }
    if (Mask & LDObservation::Equipment) { Ar << Unit.Equipment.Slots[0] << Unit.Equipment.Slots[1]; }
    if (Mask & LDObservation::Actives)
    {
        Ar << Unit.Actives.Abilities[0] << Unit.Actives.Ranks[0] << Unit.Actives.Abilities[1] << Unit.Actives.Ranks[1];
    }
    return !Ar.IsError();
}

bool SerializeObservedTermination(FArchive& Ar, FLDObservedUnitTermination& Termination)
{
    uint8 Kind = uint8(Termination.Kind);
    Ar << Termination.Handle << Termination.ObservationEpoch << Termination.Generation
        << Termination.TerminationSequence << Termination.OccurredAtServerMs << Kind;
    if (Ar.IsLoading()) { Termination.Kind = static_cast<ELDObservedTerminationKind>(Kind); }
    return !Ar.IsError();
}

bool SerializeObservedProjectile(FArchive& Ar, UPackageMap* Map, FLDObservedProjectileView& Projectile)
{
    if (Ar.IsLoading()) { Projectile = FLDObservedProjectileView(); }
    uint8 Look = uint8(Projectile.Look);
    Ar << Projectile.Handle;
    bool bPosition = true;
    Projectile.PositionCm.NetSerialize(Ar, Map, bPosition);
    Ar << Projectile.Heading << Look;
    if (Ar.IsLoading()) { Projectile.Look = static_cast<ELDObservedProjectileLook>(Look); }
    return bPosition && !Ar.IsError();
}

bool RejectObservationArchive(FArchive& Ar, bool& bSuccess)
{
    bSuccess = false;
    Ar.SetError();
    return false;
}
}

bool LDObservation::ValidateUnit(const FLDObservedUnitView& Unit, FString& Error)
{
    if (!Unit.Handle.IsValid() || Unit.ObservationEpoch == 0 || Unit.StateRevision == 0)
    { return FailObservation(Error, TEXT("An observed unit requires an opaque handle, observation epoch and state revision.")); }
    if (!IsFiniteObservedPoint(Unit.FeetCm)) { return FailObservation(Error, TEXT("An observed unit position is outside the finite world range.")); }
    if (uint8(Unit.ActionCue) > uint8(ELDObservedActionCue::Hit)
        || (Unit.ActionCue == ELDObservedActionCue::None && Unit.ActionCueAgeMs != 0))
    { return FailObservation(Error, TEXT("Unknown observed action cue, or a cue age without a cue.")); }
    if ((Unit.ActionAbility != 0 || Unit.ActionEquipment != 0) && (Unit.ActionCue != ELDObservedActionCue::Cast || (Unit.ActionAbility != 0 && Unit.ActionEquipment != 0)))
    { return FailObservation(Error, TEXT("An observed cast source needs a Cast cue and names exactly one ability or equipment.")); }
    const bool Contender = Unit.Kind == ELDObservedUnitKind::Contender;
    if (!Contender && Unit.Kind != ELDObservedUnitKind::CampMember) { return FailObservation(Error, TEXT("Unknown observed unit kind.")); }
    const uint32 Mask = Unit.AuthorizedFieldMask;
    if (Contender ? Mask != ContenderFields : (Mask != CampFields && Mask != (CampFields | Mana)))
    { return FailObservation(Error, TEXT("Observed field authorization is not the complete applicable set for this unit kind.")); }
    if (Contender
        ? Unit.Presentation >= MaxPresentations || !Unit.ContenderId.IsValid() || Unit.CreatureId != 0 || Unit.Level == 0
        : Unit.Presentation != 0 || Unit.ContenderId.IsValid() || !IsPublicCreatureId(Unit.CreatureId) || Unit.Level != 0)
    { return FailObservation(Error, TEXT("Observed identity, presentation or level does not match the unit kind.")); }
    if (!IsObservedResourcePair(Unit.Health, true)) { return FailObservation(Error, TEXT("Observed health must be a complete current/maximum P2 pair.")); }
    if ((Mask & Mana) ? !IsObservedResourcePair(Unit.Mana, false) : !IsEmptyObservedResource(Unit.Mana))
    { return FailObservation(Error, TEXT("Observed mana is either an invalid P2 pair or present without authorization.")); }
    if (Unit.Statuses.Num() > MaxStatuses) { return FailObservation(Error, TEXT("Too many observed statuses.")); }
    uint32 SeenStatuses = 0;
    for (const FLDObservedStatus& Status : Unit.Statuses)
    {
        const uint32 Bit = Status.StatusId < 32 ? 1u << Status.StatusId : 0u;
        if (!IsPublicStatusId(Status.StatusId) || Status.Stacks == 0 || Status.ExpiresAtServerMs < 0 || (SeenStatuses & Bit) != 0
            // camp_returning is a camp-only marker with neither stacks nor expiry (SC-07).
            || (Status.StatusId == uint8(ELDDemoStatusId::CampReturning)
                && (Contender || Status.Stacks != 1 || Status.ExpiresAtServerMs != 0))
            // The core derives respawn protection from its protected-until time: contender-only, non-stacking, timed.
            || (Status.StatusId == uint8(ELDDemoStatusId::RespawnProtection)
                && (!Contender || Status.Stacks != 1 || Status.ExpiresAtServerMs <= 0)))
        { return FailObservation(Error, TEXT("Observed statuses must be unique public definitions with valid stacks and expiry.")); }
        SeenStatuses |= Bit;
    }
    if (Contender
        ? !IsPublicEquipmentOrEmpty(Unit.Equipment.Slots[0]) || !IsPublicEquipmentOrEmpty(Unit.Equipment.Slots[1])
        : Unit.Equipment.Slots[0] != 0 || Unit.Equipment.Slots[1] != 0)
    { return FailObservation(Error, TEXT("Observed equipment must be two public equipment definitions or empty slots.")); }
    const FLDObservedActivesView& Actives = Unit.Actives;
    bool bActives = true;
    for (int32 Slot = 0; Slot < 2; ++Slot)
    {
        bActives &= Contender
            ? (Actives.Abilities[Slot] == 0) == (Actives.Ranks[Slot] == 0) && Actives.Ranks[Slot] <= MaxActiveRank
            : Actives.Abilities[Slot] == 0 && Actives.Ranks[Slot] == 0;
    }
    if (!bActives || (Actives.Abilities[0] != 0 && Actives.Abilities[0] == Actives.Abilities[1]))
    { return FailObservation(Error, TEXT("Observed Q/W actives must be two distinct ranked abilities or empty slots, contenders only.")); }
    Error.Reset();
    return true;
}

bool LDObservation::ValidateProjectile(const FLDObservedProjectileView& Projectile, FString& Error)
{
    if (!Projectile.Handle.IsValid()) { return FailObservation(Error, TEXT("An observed projectile requires an opaque handle.")); }
    if (!IsFiniteObservedPoint(Projectile.PositionCm)) { return FailObservation(Error, TEXT("An observed projectile position is outside the finite world range.")); }
    if (Projectile.Look != ELDObservedProjectileLook::Chalk) { return FailObservation(Error, TEXT("Unknown observed projectile look.")); }
    Error.Reset();
    return true;
}

bool LDObservation::ValidateTermination(const FLDObservedUnitTermination& Termination, FString& Error)
{
    if (!Termination.Handle.IsValid() || Termination.ObservationEpoch == 0 || Termination.Generation == 0
        || Termination.TerminationSequence == 0 || Termination.OccurredAtServerMs < 0
        || Termination.Kind != ELDObservedTerminationKind::ObservedDeath)
    { return FailObservation(Error, TEXT("An observed termination requires a handle, epoch, generation, sequence and ObservedDeath kind.")); }
    Error.Reset();
    return true;
}

bool LDObservation::ValidateView(const FLDWorldView& View, FString& Error)
{
    if (View.Schema != Schema) { return FailObservation(Error, TEXT("Unsupported observation schema.")); }
    if (View.MatchGeneration == 0 || View.Generation == 0 || View.Revision == 0)
    { return FailObservation(Error, TEXT("An observation frame requires match, observation generation and revision.")); }
    if (!IsValidObservedMapId(View.WorldMapId.ToString().ToLower()) || View.WorldLayoutRevision == 0 || View.WorldGeometryRevision == 0)
    { return FailObservation(Error, TEXT("An observation frame requires its complete world revision.")); }
    if (!FMath::IsFinite(View.SampledAtServerSec) || View.SampledAtServerSec < 0)
    { return FailObservation(Error, TEXT("An observation frame requires a finite match-relative sample time.")); }
    const FLDObservedFootprint& Footprint = View.Footprint;
    if (Footprint.SourceStatus == ELDObservationSource::ActiveBody)
    {
        if (!IsFiniteObservedPoint(Footprint.SourceFeetCm) || !FMath::IsFinite(Footprint.RadiusCm) || Footprint.RadiusCm <= 0
            || Footprint.RadiusCm > MaxObservedCoordinateCm || Footprint.CurrentBits.Num() != CoverageBytes)
        { return FailObservation(Error, TEXT("An active observation source requires a finite foot, radius and complete coverage.")); }
    }
    else if (Footprint.SourceStatus == ELDObservationSource::NoLivingBody)
    {
        if (!FVector(Footprint.SourceFeetCm).IsZero() || Footprint.RadiusCm != 0 || Footprint.CurrentBits.Num() != 0
            || View.Units.Num() != 0 || View.RecentTerminations.Num() != 0 || View.Projectiles.Num() != 0)
        { return FailObservation(Error, TEXT("A frame without a living source carries no coverage, unit, termination or projectile.")); }
    }
    else { return FailObservation(Error, TEXT("Unknown observation source status.")); }
    if (View.Units.Num() > MaxUnits || View.RecentTerminations.Num() > MaxTerminations || View.Projectiles.Num() > MaxProjectiles)
    { return FailObservation(Error, TEXT("Observation frame exceeds its bounded capacity.")); }
    TSet<FGuid> Handles;
    for (const FLDObservedUnitView& Unit : View.Units)
    {
        if (!ValidateUnit(Unit, Error)) { return false; }
        bool Duplicate = false;
        Handles.Add(Unit.Handle, &Duplicate);
        if (Duplicate) { return FailObservation(Error, TEXT("Observation frame repeats an opaque handle.")); }
    }
    uint32 LastSequence = 0;
    TSet<FGuid> Terminated;
    for (const FLDObservedUnitTermination& Termination : View.RecentTerminations)
    {
        if (!ValidateTermination(Termination, Error)) { return false; }
        bool Duplicate = false;
        Terminated.Add(Termination.Handle, &Duplicate);
        if (Termination.Generation != View.Generation || Termination.TerminationSequence <= LastSequence
            || Duplicate || Handles.Contains(Termination.Handle))
        { return FailObservation(Error, TEXT("Recent terminations must be ordered, unique and absent from the current unit set.")); }
        LastSequence = Termination.TerminationSequence;
    }
    // Projectile handles share the observer's handle space with units and recent terminations.
    for (const FLDObservedProjectileView& Projectile : View.Projectiles)
    {
        if (!ValidateProjectile(Projectile, Error)) { return false; }
        bool Duplicate = false;
        Handles.Add(Projectile.Handle, &Duplicate);
        if (Duplicate || Terminated.Contains(Projectile.Handle))
        { return FailObservation(Error, TEXT("Observation frame repeats an opaque handle.")); }
    }
    Error.Reset();
    return true;
}

bool LDObservation::ValidateConceal(const FLDObservedConceal& Conceal, FString& Error)
{
    if (Conceal.MatchGeneration == 0 || Conceal.Generation == 0 || Conceal.Revision == 0 || Conceal.Removed.Num() > MaxUnits + MaxProjectiles)
    { return FailObservation(Error, TEXT("A concealment notice requires a bounded handle list and complete ordering identity.")); }
    TSet<FGuid> Seen;
    for (const FGuid& Handle : Conceal.Removed)
    {
        bool Duplicate = false;
        Seen.Add(Handle, &Duplicate);
        if (!Handle.IsValid() || Duplicate) { return FailObservation(Error, TEXT("A concealment notice repeats or omits a handle.")); }
    }
    Error.Reset();
    return true;
}

bool LDObservation::ValidateNotice(const FLDObservedTerminationNotice& Notice, FString& Error)
{
    if (Notice.MatchGeneration == 0 || Notice.Revision == 0)
    { return FailObservation(Error, TEXT("A termination notice requires match and revision.")); }
    return ValidateTermination(Notice.Termination, Error);
}

bool FLDWorldView::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bSuccess)
{
    FString Error;
    if (Ar.IsSaving() && !LDObservation::ValidateView(*this, Error)) { return RejectObservationArchive(Ar, bSuccess); }
    Ar << Schema;
    if (Ar.IsError() || Schema != LDObservation::Schema) { return RejectObservationArchive(Ar, bSuccess); }
    Ar << MatchGeneration << ObserverLife << Generation << Revision;
    if (!SerializeObservedMapId(Ar, WorldMapId)) { return RejectObservationArchive(Ar, bSuccess); }
    Ar << WorldLayoutRevision << WorldGeometryRevision << SampledAtServerSec;
    uint8 Source = uint8(Footprint.SourceStatus);
    Ar << Source;
    if (Ar.IsError() || Source > uint8(ELDObservationSource::ActiveBody)) { return RejectObservationArchive(Ar, bSuccess); }
    if (Ar.IsLoading())
    {
        Footprint = FLDObservedFootprint();
        Footprint.SourceStatus = static_cast<ELDObservationSource>(Source);
    }
    if (Footprint.SourceStatus == ELDObservationSource::ActiveBody)
    {
        bool bFeet = true;
        Footprint.SourceFeetCm.NetSerialize(Ar, Map, bFeet);
        Ar << Footprint.RadiusCm;
        if (Ar.IsLoading()) { Footprint.CurrentBits.SetNumZeroed(LDObservation::CoverageBytes); }
        if (!bFeet || Footprint.CurrentBits.Num() != LDObservation::CoverageBytes) { return RejectObservationArchive(Ar, bSuccess); }
        Ar.Serialize(Footprint.CurrentBits.GetData(), LDObservation::CoverageBytes);
    }
    uint8 UnitCount = uint8(FMath::Min(Units.Num(), 255));
    Ar << UnitCount;
    if (Ar.IsError() || UnitCount > LDObservation::MaxUnits) { return RejectObservationArchive(Ar, bSuccess); }
    if (Ar.IsLoading()) { Units.SetNum(UnitCount); }
    for (FLDObservedUnitView& Unit : Units)
    {
        if (!SerializeObservedUnit(Ar, Map, Unit)) { return RejectObservationArchive(Ar, bSuccess); }
    }
    uint8 TerminationCount = uint8(FMath::Min(RecentTerminations.Num(), 255));
    Ar << TerminationCount;
    if (Ar.IsError() || TerminationCount > LDObservation::MaxTerminations) { return RejectObservationArchive(Ar, bSuccess); }
    if (Ar.IsLoading()) { RecentTerminations.SetNum(TerminationCount); }
    for (FLDObservedUnitTermination& Termination : RecentTerminations)
    {
        if (!SerializeObservedTermination(Ar, Termination)) { return RejectObservationArchive(Ar, bSuccess); }
    }
    uint8 ProjectileCount = uint8(FMath::Min(Projectiles.Num(), 255));
    Ar << ProjectileCount;
    if (Ar.IsError() || ProjectileCount > LDObservation::MaxProjectiles) { return RejectObservationArchive(Ar, bSuccess); }
    if (Ar.IsLoading()) { Projectiles.SetNum(ProjectileCount); }
    for (FLDObservedProjectileView& Projectile : Projectiles)
    {
        if (!SerializeObservedProjectile(Ar, Map, Projectile)) { return RejectObservationArchive(Ar, bSuccess); }
    }
    if (Ar.IsError() || (Ar.IsLoading() && !LDObservation::ValidateView(*this, Error))) { return RejectObservationArchive(Ar, bSuccess); }
    bSuccess = true;
    return true;
}

bool FLDObservedConceal::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bSuccess)
{
    FString Error;
    if (Ar.IsSaving() && !LDObservation::ValidateConceal(*this, Error)) { return RejectObservationArchive(Ar, bSuccess); }
    Ar << MatchGeneration << Generation << Revision;
    uint8 Count = uint8(FMath::Min(Removed.Num(), 255));
    Ar << Count;
    if (Ar.IsError() || Count > LDObservation::MaxUnits + LDObservation::MaxProjectiles) { return RejectObservationArchive(Ar, bSuccess); }
    if (Ar.IsLoading()) { Removed.SetNum(Count); }
    for (FGuid& Handle : Removed) { Ar << Handle; }
    if (Ar.IsError() || (Ar.IsLoading() && !LDObservation::ValidateConceal(*this, Error))) { return RejectObservationArchive(Ar, bSuccess); }
    bSuccess = true;
    return true;
}

bool FLDObservedTerminationNotice::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bSuccess)
{
    FString Error;
    if (Ar.IsSaving() && !LDObservation::ValidateNotice(*this, Error)) { return RejectObservationArchive(Ar, bSuccess); }
    Ar << MatchGeneration << Revision;
    if (!SerializeObservedTermination(Ar, Termination)
        || (Ar.IsLoading() && !LDObservation::ValidateNotice(*this, Error))) { return RejectObservationArchive(Ar, bSuccess); }
    bSuccess = true;
    return true;
}
