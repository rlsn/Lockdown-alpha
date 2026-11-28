#include "Vision/LDViewTypes.h"
#include "Development/LDDemoContentIds.h"
#include "Engine/World.h"
#include "Region/LDMapGeometry.h"
#include "Region/LDMapRegistry.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "Serialization/BitReader.h"
#include "Serialization/BitWriter.h"

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
    case ELDDemoStatusId::Knockback:
    case ELDDemoStatusId::Silence:
    case ELDDemoStatusId::Burn:
    case ELDDemoStatusId::AttackSlowed:
    // Spec §8.1 held elements and the S16 stance.
    case ELDDemoStatusId::Wet:
    case ELDDemoStatusId::Electrified:
    case ELDDemoStatusId::Chilled:
    case ELDDemoStatusId::Rotten:
    case ELDDemoStatusId::Braced:
    case ELDDemoStatusId::CampEnraged:
    case ELDDemoStatusId::CampGuarding:
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
    case ELDDemoMonsterId::MonsterDean:
    case ELDDemoMonsterId::MonsterAdvisor:
    case ELDDemoMonsterId::MonsterPromoter:
    case ELDDemoMonsterId::MonsterCourier:
    case ELDDemoMonsterId::MonsterAgent:
    case ELDDemoMonsterId::MonsterGuard:
    case ELDDemoMonsterId::MonsterAuditor:
    case ELDDemoMonsterId::MonsterClient:
    case ELDDemoMonsterId::MonsterDirector:
    case ELDDemoMonsterId::MonsterFinalClient:
    case ELDDemoMonsterId::MonsterProperty:
        return true;
    default:
        return false;
    }
}

bool IsObservedResourcePair(const FLDObservedResource& Resource, bool RequirePositiveMax)
{
    return Resource.MaxP2 >= (RequirePositiveMax ? 1 : 0) && Resource.CurrentP2 >= 0 && Resource.CurrentP2 <= Resource.MaxP2;
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

bool SerializeObservedStatusExpiry(FArchive& Ar, int64& ExpiresAtServerMs)
{
    if (Ar.IsSaving())
    {
        if (ExpiresAtServerMs < 0) { return false; }
        uint64 Value = uint64(ExpiresAtServerMs);
        do
        {
            uint8 Byte = uint8(Value & 0x7f);
            Value >>= 7;
            if (Value != 0) { Byte |= 0x80; }
            Ar.Serialize(&Byte, sizeof(Byte));
        }
        while (Value != 0 && !Ar.IsError());
        return !Ar.IsError();
    }

    uint64 Value = 0;
    for (uint32 Shift = 0; Shift < 64; Shift += 7)
    {
        uint8 Byte = 0;
        Ar.Serialize(&Byte, sizeof(Byte));
        if (Ar.IsError()) { return false; }
        const uint8 Payload = Byte & 0x7f;
        if (Shift == 63 && Payload > 1) { return false; }
        Value |= uint64(Payload) << Shift;
        if ((Byte & 0x80) == 0)
        {
            if ((Shift != 0 && Payload == 0) || Value > uint64(MAX_int64)) { return false; }
            ExpiresAtServerMs = int64(Value);
            return true;
        }
        if (Shift == 63) { return false; }
    }
    return false;
}

bool SerializeObservedUnit(FArchive& Ar, UPackageMap* Map, FLDObservedUnitView& Unit)
{
    if (Ar.IsLoading()) { Unit = FLDObservedUnitView(); }
    uint8 Kind = uint8(Unit.Kind);
    uint8 Cue = uint8(Unit.ActionCue);
    uint8 HitLook = uint8(Unit.HitLook);
    Ar << Unit.Handle << Unit.ObservationEpoch << Unit.StateRevision << Unit.AuthorizedFieldMask << Kind << Unit.Presentation;
    bool bFeet = true;
    Unit.FeetCm.NetSerialize(Ar, Map, bFeet);
    Ar << Unit.Facing << Cue << Unit.ActionCueAgeMs << Unit.ActionCueImpactDelayMs << HitLook
        << Unit.RecentHitAgePlusOneMs << Unit.RecentHitId << Unit.HitFlags << Unit.ActionAbility << Unit.ActionComboStep
        << Unit.RecentReaction << Unit.RecentReactionAgePlusOneMs << Unit.RecentReactionElement;
    if (Ar.IsLoading())
    {
        Unit.Kind = static_cast<ELDObservedUnitKind>(Kind);
        Unit.ActionCue = static_cast<ELDObservedActionCue>(Cue);
        Unit.HitLook = static_cast<ELDObservedHitLook>(HitLook);
    }
    if (!bFeet || Ar.IsError()
        || (Kind != uint8(ELDObservedUnitKind::Contender) && Kind != uint8(ELDObservedUnitKind::CampMember))) { return false; }
    const uint32 Mask = Unit.AuthorizedFieldMask;
    if (Mask & LDObservation::Identity)
    {
        if (Kind == uint8(ELDObservedUnitKind::Contender)) { Ar << Unit.ContenderId; }
        else { Ar << Unit.CreatureId << Unit.CampAffix << Unit.bPeddler; }
    }
    if (Mask & LDObservation::Level) { Ar << Unit.Level; }
    if (Mask & LDObservation::Health) { Ar << Unit.Health.CurrentP2 << Unit.Health.MaxP2 << Unit.ShieldBar; }
    if (Mask & LDObservation::Momentum) { Ar << Unit.MomentumBar; }
    if (Mask & LDObservation::Statuses)
    {
        uint8 Count = uint8(FMath::Min(Unit.Statuses.Num(), 255));
        Ar << Count;
        if (Ar.IsError() || Count > LDObservation::MaxStatuses) { return false; }
        if (Ar.IsLoading()) { Unit.Statuses.SetNum(Count); }
        for (FLDObservedStatus& Status : Unit.Statuses)
        {
            Ar << Status.StatusId << Status.Stacks;
            if (!SerializeObservedStatusExpiry(Ar, Status.ExpiresAtServerMs)) { return false; }
        }
    }
    if (Mask & LDObservation::Actives)
    {
        Ar << Unit.Actives.Abilities[0] << Unit.Actives.Abilities[1] << Unit.Actives.Abilities[2];
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
    uint8 Own=Projectile.bOwn ? 1 : 0;
    Ar.SerializeBits(&Own,1);
    if(Ar.IsLoading()){Projectile.bOwn=Own!=0;}
    Ar << Projectile.Heading << Look << Projectile.RadiusCm << Projectile.InnerRadiusCm << Projectile.AngleDegrees << Projectile.LengthCm
        << Projectile.WarningRemainingMs;
    if (Ar.IsLoading()) { Projectile.Look = static_cast<ELDObservedProjectileLook>(Look); }
    return bPosition && !Ar.IsError();
}

// 16 bytes (LDObservation::GroundStateWireBytes): id, kind | shape << 3, seat, X and Y as signed 24-bit centimetres, Z as
// signed 16-bit 2 cm steps, radius in 4 cm steps, extent in 8 cm (band) or 2 degree (sector) steps, heading in 256 steps,
// remaining and life in 50 ms steps.
bool SerializeObservedGround(FArchive& Ar, FLDObservedGroundState& Ground)
{
    const auto Clamp24 = [](double Value) { return int32(FMath::Clamp(FMath::RoundToDouble(Value), -8388608., 8388607.)); };
    uint8 KindShape = uint8((Ground.Kind & 7) | (uint8(Ground.Shape) << 3));
    int32 X = Clamp24(Ground.CentreCm.X), Y = Clamp24(Ground.CentreCm.Y);
    int16 Z = int16(FMath::Clamp(FMath::RoundToDouble(Ground.CentreCm.Z * .5), -32768., 32767.));
    uint8 Radius = uint8(FMath::Clamp(FMath::RoundToDouble(Ground.RadiusCm * .25), 0., 255.));
    const double ExtentStep = Ground.Shape == ELDObservedGroundShape::Sector ? 2. : 8.;
    uint8 Extent = uint8(FMath::Clamp(FMath::RoundToDouble(Ground.ExtentCm / ExtentStep), 0., 255.));
    uint8 Remaining = uint8(FMath::Clamp((int64(Ground.RemainingMs) + 49) / 50, int64(0), int64(255)));
    uint8 Total = uint8(FMath::Clamp((int64(Ground.TotalMs) + 49) / 50, int64(0), int64(255)));
    uint8 XYZ[8] = {uint8(X), uint8(X >> 8), uint8(X >> 16), uint8(Y), uint8(Y >> 8), uint8(Y >> 16), uint8(uint16(Z)), uint8(uint16(Z) >> 8)};
    Ar << Ground.Id << KindShape << Ground.OwnerSeat;
    Ar.Serialize(XYZ, 8);
    Ar << Radius << Extent << Ground.Heading << Remaining << Total;
    if (Ar.IsLoading())
    {
        const auto Sign24 = [](uint32 Raw) { return int32(Raw << 8) >> 8; };
        Ground.Kind = KindShape & 7;
        Ground.Shape = static_cast<ELDObservedGroundShape>(KindShape >> 3);
        Ground.CentreCm = FVector(Sign24(XYZ[0] | XYZ[1] << 8 | XYZ[2] << 16), Sign24(XYZ[3] | XYZ[4] << 8 | XYZ[5] << 16),
            double(int16(uint16(XYZ[6] | XYZ[7] << 8))) * 2.);
        Ground.RadiusCm = float(Radius) * 4.f;
        Ground.ExtentCm = float(Extent) * (Ground.Shape == ELDObservedGroundShape::Sector ? 2.f : 8.f);
        Ground.RemainingMs = uint32(Remaining) * 50;
        Ground.TotalMs = uint32(Total) * 50;
    }
    return !Ar.IsError();
}

bool RejectObservationArchive(FArchive& Ar, bool& bSuccess)
{
    bSuccess = false;
    Ar.SetError();
    return false;
}

bool SerializeObservedCoverage(FArchive& Ar, TArray<uint8>& Bits)
{
    TArray<uint8> Runs;
    uint16 EncodedBytes = 0;
    if (Ar.IsSaving())
    {
        if (Bits.Num() != LDObservation::CoverageBytes) { return false; }
        for (int32 Start = 0; Start < Bits.Num();)
        {
            int32 End = Start + 1;
            while (End < Bits.Num() && End - Start < 256 && Bits[End] == Bits[Start]) { ++End; }
            Runs.Add(uint8(End - Start - 1));
            Runs.Add(Bits[Start]);
            Start = End;
        }
        // Zero selects raw bytes; incompressible masks never grow beyond the original bitmap plus this header.
        if (Runs.Num() < Bits.Num()) { EncodedBytes = uint16(Runs.Num()); }
    }
    Ar << EncodedBytes;
    if (Ar.IsError() || EncodedBytes >= LDObservation::CoverageBytes || (EncodedBytes & 1) != 0) { return false; }
    if (Ar.IsLoading()) { Bits.SetNumZeroed(LDObservation::CoverageBytes); }
    if (EncodedBytes == 0)
    {
        Ar.Serialize(Bits.GetData(), LDObservation::CoverageBytes);
        return !Ar.IsError();
    }
    if (Ar.IsSaving()) { Ar.Serialize(Runs.GetData(), EncodedBytes); return !Ar.IsError(); }
    Runs.SetNumUninitialized(EncodedBytes);
    Ar.Serialize(Runs.GetData(),EncodedBytes);
    return !Ar.IsError() && LDObservation::DecodeCoverageRuns(Runs,Bits);
}
}

bool LDObservation::DecodeCoverageRuns(TConstArrayView<uint8> Runs, TArray<uint8>& Bits)
{
    if (Runs.IsEmpty() || Runs.Num() >= CoverageBytes || (Runs.Num() & 1) != 0) { return false; }
    Bits.SetNumZeroed(CoverageBytes);
    int32 Written = 0;
    for (int32 Index = 0; Index < Runs.Num(); Index += 2)
    {
        const int32 Count = int32(Runs[Index]) + 1;
        if (Count > Bits.Num() - Written) { return false; }
        FMemory::Memset(Bits.GetData() + Written, Runs[Index+1], Count);
        Written += Count;
    }
    return Written == Bits.Num();
}

FBox2D LDObservation::SquareCoverageBounds(const FBox2D& Ground)
{
    if (!Ground.bIsValid) { return FBox2D(ForceInit); }
    const FVector2D Size=Ground.GetSize();
    const double Cell=FMath::Max(Size.X/CoverageWidth, Size.Y/CoverageHeight);
    const FVector2D HalfSpan=FVector2D(CoverageWidth,CoverageHeight)*(.5*Cell);
    return FBox2D(Ground.GetCenter()-HalfSpan,Ground.GetCenter()+HalfSpan);
}

FBox2D LDObservation::MapCoverageBounds(const UWorld* World)
{
    const auto* Registry=World?World->GetSubsystem<ULDMapRegistry>():nullptr;
    const auto* Geometry=Registry&&Registry->Enabled()?Registry->GetGeometry():nullptr;
    FVector Min,Max;
    FString Error;
    if (!Geometry || Geometry->Surfaces.Num()!=1 || !Geometry->Surfaces[0].MinCm.ToVector(Min,Error)
        || !Geometry->Surfaces[0].MaxCm.ToVector(Max,Error) || Max.X<=Min.X || Max.Y<=Min.Y)
    { return FBox2D(ForceInit); }
    FBox2D Bounds{FVector2D(Min),FVector2D(Max)};
    const FBox& Native=Registry->GetTerrain().NativeBounds();
    if (Native.IsValid) { Bounds+=FVector2D(Native.Min); Bounds+=FVector2D(Native.Max); }
    return SquareCoverageBounds(Bounds);
}

int32 LDObservation::CoverageIndex(const FBox2D& Bounds,const FVector2D& Point)
{
    if (!Bounds.bIsValid || !Bounds.IsInsideOrOn(Point)) { return INDEX_NONE; }
    const FVector2D Cell=(Point-Bounds.Min)/Bounds.GetSize();
    const int32 X=FMath::Min(CoverageWidth-1,FMath::FloorToInt(Cell.X*CoverageWidth));
    const int32 Y=FMath::Min(CoverageHeight-1,FMath::FloorToInt(Cell.Y*CoverageHeight));
    return Y*CoverageWidth+X;
}

bool LDObservation::ValidateUnit(const FLDObservedUnitView& Unit, FString& Error)
{
    if (!Unit.Handle.IsValid() || Unit.ObservationEpoch == 0 || Unit.StateRevision == 0)
    { return FailObservation(Error, TEXT("An observed unit requires an opaque handle, observation epoch and state revision.")); }
    if (!IsFiniteObservedPoint(Unit.FeetCm)) { return FailObservation(Error, TEXT("An observed unit position is outside the finite world range.")); }
    if (uint8(Unit.ActionCue) > uint8(ELDObservedActionCue::Cast)
        || uint8(Unit.HitLook) > uint8(ELDObservedHitLook::Electric)
        || (Unit.HitFlags & ~HitFlagsMask) != 0
        || ((Unit.HitFlags & ShieldBroken) != 0 && (Unit.HitFlags & ShieldAbsorbed) == 0)
        || Unit.RecentHitAgePlusOneMs > RecentHitWindowMs || Unit.RecentReaction > 15
        || (Unit.RecentReaction != 0) != (Unit.RecentReactionAgePlusOneMs != 0) || Unit.RecentReactionAgePlusOneMs > RecentReactionWindowMs
        || (Unit.RecentReaction != 0) != (Unit.RecentReactionElement != 0) || Unit.RecentReactionElement > 6
        || (Unit.RecentHitId.IsValid() != (Unit.RecentHitAgePlusOneMs != 0))
        || (Unit.RecentHitAgePlusOneMs == 0 && (Unit.HitLook != ELDObservedHitLook::None || Unit.HitFlags != 0))
        || (Unit.ActionCue == ELDObservedActionCue::None && (Unit.ActionCueAgeMs != 0 || Unit.ActionCueImpactDelayMs != 0)))
    { return FailObservation(Error, TEXT("Unknown observed action cue, or cue timing without a compatible cue.")); }
    if (Unit.ActionAbility != 0 && Unit.ActionCue != ELDObservedActionCue::Cast)
    { return FailObservation(Error, TEXT("An observed cast source needs a Cast cue and names an ability.")); }
    const bool Contender = Unit.Kind == ELDObservedUnitKind::Contender;
    if (!Contender && Unit.Kind != ELDObservedUnitKind::CampMember) { return FailObservation(Error, TEXT("Unknown observed unit kind.")); }
    const uint32 Mask = Unit.AuthorizedFieldMask;
    if (Contender ? Mask != ContenderFields : Mask != CampFields)
    { return FailObservation(Error, TEXT("Observed field authorization is not the complete applicable set for this unit kind.")); }
    if (Contender
        ? Unit.Presentation >= MaxPresentations || !Unit.ContenderId.IsValid() || Unit.CreatureId != 0 || Unit.Level == 0
            || Unit.CampAffix != 0 || Unit.bPeddler
        : Unit.Presentation != 0 || Unit.ContenderId.IsValid() || !IsPublicCreatureId(Unit.CreatureId) || Unit.Level != 0
            || Unit.CampAffix > uint8(ELDCampAffix::Audit) || (Unit.bPeddler && Unit.CampAffix != 0))
    { return FailObservation(Error, TEXT("Observed identity, presentation or level does not match the unit kind.")); }
    if (!IsObservedResourcePair(Unit.Health, true)) { return FailObservation(Error, TEXT("Observed health must be a complete current/maximum P2 pair.")); }
    if (!(Mask & Health) && Unit.ShieldBar != 0)
    { return FailObservation(Error, TEXT("An observed shield bar is present without health authorization.")); }
    if (!(Mask & Momentum) && Unit.MomentumBar != 0)
    { return FailObservation(Error, TEXT("Observed momentum is present without authorization.")); }
    if (Unit.ActionComboStep > 3 || (Unit.ActionComboStep != 0 && Unit.ActionCue != ELDObservedActionCue::Attack))
    { return FailObservation(Error, TEXT("An observed combo step needs an Attack cue and is 1..3.")); }
    if (Unit.Statuses.Num() > MaxStatuses) { return FailObservation(Error, TEXT("Too many observed statuses.")); }
    uint64 SeenStatuses = 0;
    for (const FLDObservedStatus& Status : Unit.Statuses)
    {
        const uint64 Bit = Status.StatusId < 64 ? uint64(1) << Status.StatusId : 0u;
        if (!IsPublicStatusId(Status.StatusId) || Status.Stacks == 0 || Status.ExpiresAtServerMs < 0 || (SeenStatuses & Bit) != 0
            // camp_returning is a camp-only marker with neither stacks nor expiry (SC-07).
            || (Status.StatusId == uint8(ELDDemoStatusId::CampReturning)
                && (Contender || Status.Stacks != 1 || Status.ExpiresAtServerMs != 0))
            || (Status.StatusId==uint8(ELDDemoStatusId::CampEnraged)
                && (Contender || Status.Stacks!=1 || Status.ExpiresAtServerMs!=0))
            || (Status.StatusId==uint8(ELDDemoStatusId::CampGuarding)
                && (Contender || Status.Stacks!=1 || Status.ExpiresAtServerMs<=0))
            // The core derives respawn protection from its protected-until time: contender-only, non-stacking, timed.
            || (Status.StatusId == uint8(ELDDemoStatusId::RespawnProtection)
                && (!Contender || Status.Stacks != 1 || Status.ExpiresAtServerMs <= 0)))
        { return FailObservation(Error, TEXT("Observed statuses must be unique public definitions with valid stacks and expiry.")); }
        SeenStatuses |= Bit;
    }
    const FLDObservedActivesView& Actives = Unit.Actives;
    const bool bActives = Contender
        || (Actives.Abilities[0] == 0 && Actives.Abilities[1] == 0 && Actives.Abilities[2] == 0);
    const bool bDuplicate = (Actives.Abilities[0] != 0 && Actives.Abilities[0] == Actives.Abilities[1])
        || (Actives.Abilities[0] != 0 && Actives.Abilities[0] == Actives.Abilities[2])
        || (Actives.Abilities[1] != 0 && Actives.Abilities[1] == Actives.Abilities[2]);
    if (!bActives || bDuplicate)
    { return FailObservation(Error, TEXT("Observed Q/W/E actives must be three distinct abilities or empty slots, contenders only.")); }
    Error.Reset();
    return true;
}

bool LDObservation::ValidateGround(const FLDObservedGroundState& Ground, FString& Error)
{
    if (Ground.Kind < 1 || Ground.Kind > 6 || uint8(Ground.Shape) > uint8(ELDObservedGroundShape::Sector)
        || (Ground.OwnerSeat >= MaxPresentations && Ground.OwnerSeat != 255) || !IsFiniteObservedPoint(Ground.CentreCm)
        || !FMath::IsFinite(Ground.RadiusCm) || Ground.RadiusCm <= 0 || !FMath::IsFinite(Ground.ExtentCm) || Ground.ExtentCm < 0
        || (Ground.Shape == ELDObservedGroundShape::Circle) != (Ground.ExtentCm == 0)
        || (Ground.Shape == ELDObservedGroundShape::Sector && Ground.ExtentCm > 360) || Ground.RemainingMs > Ground.TotalMs)
    { return FailObservation(Error, TEXT("An observed ground state is outside its public domain.")); }
    Error.Reset();
    return true;
}

FLDObservedGroundState LDObservation::GroundView(const FLDObservedGroundFact& Fact)
{
    // Quantized exactly as the wire keeps it, so a Listen Host's local view matches what a client decodes.
    FLDObservedGroundState Out;
    Out.Id = uint8(Fact.Id & 0xff);
    Out.Kind = Fact.Kind;
    Out.Shape = Fact.AngleDegrees > 0 ? ELDObservedGroundShape::Sector : Fact.HalfLengthCm > 0 ? ELDObservedGroundShape::Band
        : ELDObservedGroundShape::Circle;
    Out.OwnerSeat = Fact.OwnerSeat < MaxPresentations ? Fact.OwnerSeat : 255;
    Out.CentreCm = FVector(FMath::RoundToDouble(Fact.CentreCm.X), FMath::RoundToDouble(Fact.CentreCm.Y),
        FMath::RoundToDouble(Fact.CentreCm.Z * .5) * 2.);
    Out.RadiusCm = FMath::Max(1.f, FMath::RoundToFloat(FMath::Min(Fact.RadiusCm, 1020.f) * .25f)) * 4.f;
    Out.ExtentCm = Out.Shape == ELDObservedGroundShape::Sector ? FMath::Max(1.f, FMath::RoundToFloat(FMath::Min(Fact.AngleDegrees, 360.f) * .5f)) * 2.f
        : Out.Shape == ELDObservedGroundShape::Band ? FMath::Max(1.f, FMath::RoundToFloat(FMath::Min(Fact.HalfLengthCm, 2040.f) / 8.f)) * 8.f : 0.f;
    const double Yaw = FMath::RadiansToDegrees(FMath::Atan2(Fact.Heading.Y, Fact.Heading.X));
    Out.Heading = uint8(int32(FMath::RoundToDouble(Yaw / 360. * 256.)) & 0xff);
    Out.TotalMs = uint32(FMath::Clamp((int64(Fact.LifeMs) + 49) / 50, int64(1), int64(255))) * 50;
    Out.RemainingMs = FMath::Min(Out.TotalMs, uint32(FMath::Clamp((int64(Fact.RemainingMs) + 49) / 50, int64(0), int64(255))) * 50);
    return Out;
}

bool LDObservation::ValidateProjectile(const FLDObservedProjectileView& Projectile, FString& Error)
{
    if (!Projectile.Handle.IsValid()) { return FailObservation(Error, TEXT("An observed projectile requires an opaque handle.")); }
    if (!IsFiniteObservedPoint(Projectile.PositionCm)) { return FailObservation(Error, TEXT("An observed projectile position is outside the finite world range.")); }
    if (!FMath::IsFinite(Projectile.LengthCm) || Projectile.LengthCm<0
        || (Projectile.Look!=ELDObservedProjectileLook::Road && Projectile.Look!=ELDObservedProjectileLook::LineWarning
            && Projectile.Look!=ELDObservedProjectileLook::Wind && Projectile.Look!=ELDObservedProjectileLook::BagWall
            && Projectile.Look!=ELDObservedProjectileLook::CordonTape
            && Projectile.LengthCm!=0)
        || !FMath::IsFinite(Projectile.RadiusCm) || !FMath::IsFinite(Projectile.InnerRadiusCm)
        || !FMath::IsFinite(Projectile.AngleDegrees) || Projectile.RadiusCm < 0 || Projectile.RadiusCm > MaxObservedCoordinateCm
        || Projectile.InnerRadiusCm < 0 || Projectile.InnerRadiusCm > Projectile.RadiusCm
        || Projectile.AngleDegrees < 0 || Projectile.AngleDegrees > 180)
    { return FailObservation(Error, TEXT("Observed skill geometry is outside its finite public domain.")); }
    switch (Projectile.Look)
    {
    case ELDObservedProjectileLook::LineWarning:
        if(Projectile.LengthCm<=0 || Projectile.RadiusCm<=0 || Projectile.InnerRadiusCm!=0 || Projectile.AngleDegrees!=0)
        {return FailObservation(Error,TEXT("A line warning needs its locked length and half-width."));}
        break;
    case ELDObservedProjectileLook::ConeWarning:
        if(Projectile.RadiusCm<=0 || Projectile.InnerRadiusCm!=0 || Projectile.AngleDegrees<=0)
        {return FailObservation(Error,TEXT("A cone warning needs its locked reach and angle."));}
        break;
    case ELDObservedProjectileLook::Bag:
        if(Projectile.RadiusCm<=0 || Projectile.InnerRadiusCm!=0 || Projectile.AngleDegrees!=0 || Projectile.WarningRemainingMs!=0)
        {return FailObservation(Error,TEXT("A bag needs its body radius, without a landing warning."));}
        break;
    case ELDObservedProjectileLook::Road:
    case ELDObservedProjectileLook::Wind:
    case ELDObservedProjectileLook::BagWall:
    case ELDObservedProjectileLook::CordonTape:
        if(Projectile.LengthCm<=0 || Projectile.RadiusCm<=0 || Projectile.InnerRadiusCm!=0
            || Projectile.AngleDegrees!=0 || Projectile.WarningRemainingMs!=0)
        {return FailObservation(Error,TEXT("A road needs its walked length and half-width, without a landing warning."));}
        break;
    case ELDObservedProjectileLook::Chalk:
    case ELDObservedProjectileLook::Sock:
    case ELDObservedProjectileLook::ChalkReturn:
    case ELDObservedProjectileLook::SockReturn:
    case ELDObservedProjectileLook::FistWind:
        if (Projectile.RadiusCm != 0 || Projectile.InnerRadiusCm != 0 || Projectile.AngleDegrees != 0
            || Projectile.WarningRemainingMs != 0)
        { return FailObservation(Error, TEXT("A flight cannot carry a ground-effect shape.")); }
        break;
    case ELDObservedProjectileLook::BookShadow:
    case ELDObservedProjectileLook::FlyerArming:
    case ELDObservedProjectileLook::FlyerArmed:
        if (Projectile.RadiusCm <= 0 || Projectile.InnerRadiusCm <= 0 || Projectile.AngleDegrees != 0)
        { return FailObservation(Error, TEXT("A ground effect needs its two public radii.")); }
        break;
    case ELDObservedProjectileLook::AreaPlain:
    case ELDObservedProjectileLook::AreaMagic:
    case ELDObservedProjectileLook::AreaFire:
    case ELDObservedProjectileLook::AreaWater:
    case ELDObservedProjectileLook::AreaElectric:
    case ELDObservedProjectileLook::SlipperDrop:
    case ELDObservedProjectileLook::PhantomDecoy:
    case ELDObservedProjectileLook::ShardBurst:
        if (Projectile.RadiusCm <= 0 || Projectile.InnerRadiusCm < 0 || Projectile.AngleDegrees != 0)
        { return FailObservation(Error, TEXT("An observed area needs its public radius and no cone angle.")); }
        break;
    case ELDObservedProjectileLook::SpeechCone:
        if (Projectile.RadiusCm <= 0 || Projectile.InnerRadiusCm != 0 || Projectile.AngleDegrees <= 0
            || Projectile.WarningRemainingMs != 0)
        { return FailObservation(Error, TEXT("A cone needs public reach and angle.")); }
        break;
    default: return FailObservation(Error, TEXT("Unknown observed skill effect look."));
    }
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
            || View.Units.Num() != 0 || View.RecentTerminations.Num() != 0 || View.Projectiles.Num() != 0
            || View.GroundStates.Num() != 0 || View.RecoveryOrbs.Num() != 0 || !View.WitnessedEmptyCamps.IsEmpty())
        { return FailObservation(Error, TEXT("A frame without a living source carries no coverage, unit, termination or projectile.")); }
    }
    else { return FailObservation(Error, TEXT("Unknown observation source status.")); }
    if (View.Units.Num() > MaxUnits || View.RecentTerminations.Num() > MaxTerminations || View.Projectiles.Num() > MaxProjectiles
        || View.GroundStates.Num() > MaxGroundStates || View.RecoveryOrbs.Num() > MaxRecoveryOrbs
        || View.WitnessedEmptyCamps.Num() > MaxUnits)
    { return FailObservation(Error, TEXT("Observation frame exceeds its bounded capacity.")); }
    for (const FLDObservedGroundState& Ground : View.GroundStates)
    {
        if (!ValidateGround(Ground, Error)) { return false; }
    }
    TSet<FName> CampIds;
    for (FName Camp : View.WitnessedEmptyCamps)
    {
        if (CampIds.Contains(Camp) || !IsValidObservedMapId(Camp.ToString().ToLower()))
        {
            return FailObservation(Error, TEXT("An observed empty camp requires a unique registered camp id."));
        }
        CampIds.Add(Camp);
    }
    TSet<uint64> OrbSerials;
    for (const FLDObservedRecoveryOrb& Orb : View.RecoveryOrbs)
    {
        bool bDupOrb = false;
        OrbSerials.Add(Orb.Serial, &bDupOrb);
        if (Orb.Serial == 0 || bDupOrb || !IsFiniteObservedPoint(Orb.PositionCm))
        {
            return FailObservation(Error, TEXT("An observed recovery orb requires a unique nonzero serial and finite position."));
        }
    }
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
    // Net archives are bit streams: the position before and after is this view's size on the wire.
    const auto WireBits = [&Ar]()
    {
        return !Ar.IsNetArchive() ? int64(0) : Ar.IsLoading() ? static_cast<FBitReader&>(Ar).GetPosBits()
            : static_cast<FBitWriter&>(Ar).GetNumBits();
    };
    const int64 StartBits = WireBits();
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
        if (!bFeet || !SerializeObservedCoverage(Ar, Footprint.CurrentBits)) { return RejectObservationArchive(Ar, bSuccess); }
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
    uint8 GroundCount = uint8(FMath::Min(GroundStates.Num(), 255));
    Ar << GroundCount;
    if (Ar.IsError() || GroundCount > LDObservation::MaxGroundStates) { return RejectObservationArchive(Ar, bSuccess); }
    if (Ar.IsLoading()) { GroundStates.SetNum(GroundCount); }
    for (FLDObservedGroundState& Ground : GroundStates)
    {
        if (!SerializeObservedGround(Ar, Ground)) { return RejectObservationArchive(Ar, bSuccess); }
    }
    uint8 OrbCount = uint8(FMath::Min(RecoveryOrbs.Num(), 255));
    Ar << OrbCount;
    if (Ar.IsError() || OrbCount > LDObservation::MaxRecoveryOrbs) { return RejectObservationArchive(Ar, bSuccess); }
    if (Ar.IsLoading()) { RecoveryOrbs.SetNum(OrbCount); }
    for (FLDObservedRecoveryOrb& Orb : RecoveryOrbs)
    {
        if (Ar.IsLoading()) { Orb = FLDObservedRecoveryOrb(); }
        Ar << Orb.Serial;
        bool bOrbPos = true;
        Orb.PositionCm.NetSerialize(Ar, Map, bOrbPos);
        if (!bOrbPos || Ar.IsError()) { return RejectObservationArchive(Ar, bSuccess); }
    }
    uint8 EmptyCampCount = uint8(WitnessedEmptyCamps.Num());
    Ar << EmptyCampCount;
    if (Ar.IsError() || EmptyCampCount > LDObservation::MaxUnits) { return RejectObservationArchive(Ar, bSuccess); }
    if (Ar.IsLoading()) { WitnessedEmptyCamps.SetNum(EmptyCampCount); }
    for (FName& Camp : WitnessedEmptyCamps)
    {
        if (!SerializeObservedMapId(Ar, Camp)) { return RejectObservationArchive(Ar, bSuccess); }
    }
    if (Ar.IsError() || (Ar.IsLoading() && !LDObservation::ValidateView(*this, Error))) { return RejectObservationArchive(Ar, bSuccess); }
    // A connection always brings its package map; a probe or test that encodes a view by itself has none and is not traffic.
    if (Ar.IsNetArchive() && Map)
    {
        const int32 Bytes = int32((WireBits() - StartBits + 7) / 8);
        if (Ar.IsLoading()) { ReceivedWireBytes = Bytes; }
        else
        {
            TRACE_INT_VALUE(TEXT("LD/ViewWireBytesSent"), Bytes);
            TRACE_INT_VALUE(TEXT("LD/ViewUnitsSent"), Units.Num());
        }
    }
    bSuccess = true;
    return true;
}

bool FLDObservedConceal::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bSuccess)
{
    FString Error;
    if (Ar.IsSaving() && !LDObservation::ValidateConceal(*this, Error)) { return RejectObservationArchive(Ar, bSuccess); }
    Ar << MatchGeneration << Generation << Revision;
    uint16 Count = uint16(Removed.Num());
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
