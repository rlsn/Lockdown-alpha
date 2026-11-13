#pragma once
#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "Framework/LDMatchTimeline.h"
#include "Development/LDGrowthProtocol.h"
#include "LDViewTypes.generated.h"

// Observation wire records are owner-directed authorized projections. They carry no actor,
// controller, canonical body identity, life key or private build/economy/cooldown field.
namespace LDObservation
{
    // 11 adds the age of an authorized hit, independent of the current action cue.
    // 10 adds the recent hit presentation and public area warning countdown.
    // 9 adds the authoritative release delay to public action cues.
    // 8 (#198): Q/W/E carry single-level ability ids without ranks; the cowering and contact-guard statuses are gone.
    constexpr uint32 Schema = 11;
    constexpr uint16 RecentHitWindowMs = 300;
    // Display coverage over the registered ground surface bounds: index = y * Side + x, low bit first.
    constexpr int32 CoverageSide = 128;
    constexpr int32 CoverageCells = CoverageSide * CoverageSide;
    constexpr int32 CoverageBytes = CoverageCells / 8;
    // BeginMatch rejects a locked roster plus approved camp members that could exceed this. #97: derived from the
    // approved content: one observer sees at most the five other contenders (3 humans + 3 AI), the 52 camp members of
    // the 34 camps (18 low x 2, 12 high x 1, 4 elite x 1) and the Boss, 58 units; the wire counts are uint8.
    // Lockdown.Rules.CampDecision fails when the approved camps outgrow this bound.
    constexpr int32 MaxUnits = 58;
    // Every public status a unit can show at once: each definition appears at most once, and each kind adds its marker
    // to the combat cues (Lockdown.Rules.DemoVision.FullStatusBound).
    constexpr int32 MaxStatuses = 16;
    constexpr int32 MaxTerminations = 16;
    // Visible flights and skill areas per frame; the nearest are kept when more are in sight.
    constexpr int32 MaxProjectiles = 32;
    // One identity tint per seat of the six-contender roster (APP_SPEC 1).
    constexpr uint8 MaxPresentations = 6;
    constexpr int32 MaxMapIdBytes = 63;
    // Bit 32 (the retired equipment slots, #144) is never reused.
    enum EField : uint32 { Identity = 1, Level = 2, Health = 4, Mana = 8, Statuses = 16, Actives = 64 };
    constexpr uint32 ContenderFields = Identity | Level | Health | Mana | Statuses | Actives;
    constexpr uint32 CampFields = Identity | Health | Statuses;
}

UENUM() enum class ELDObservedUnitKind : uint8 { Contender = 1, CampMember = 2 };
// Combat owns visible action cues; adding one is a schema change.
UENUM() enum class ELDObservedActionCue : uint8 { None = 0, Attack = 1, Cast = 2, Hit = 3 };
UENUM() enum class ELDObservedHitLook : uint8 { None = 0, Magic = 1, Fire = 2, Water = 3, Electric = 4 };
// Declared before the wire projectile record that carries it; Combat maps each projectile source to one look.
UENUM() enum class ELDObservedProjectileLook : uint8
{
    Chalk = 1, Sock = 2, BookShadow = 3, FlyerArming = 4, FlyerArmed = 5, SpeechCone = 6,
    AreaPlain = 7, AreaMagic = 8, AreaFire = 9, AreaWater = 10, AreaElectric = 11
};
UENUM() enum class ELDObservationSource : uint8 { NoLivingBody = 0, ActiveBody = 1 };
UENUM() enum class ELDObservedTerminationKind : uint8 { ObservedDeath = 1 };

USTRUCT()
struct FLDObservedResource
{
    GENERATED_BODY()
    UPROPERTY() int64 CurrentP2 = 0;
    UPROPERTY() int64 MaxP2 = 0;
};

USTRUCT()
struct FLDObservedStatus
{
    GENERATED_BODY()
    UPROPERTY() uint8 StatusId = 0; // ELDDemoStatusId
    UPROPERTY() uint8 Stacks = 1;
    UPROPERTY() int64 ExpiresAtServerMs = 0; // 0 = untimed
};

// The three Q/W/E actives of a visible contender (#198: an active has one level). No cooldown, mana cost or other field.
USTRUCT()
struct FLDObservedActivesView
{
    GENERATED_BODY()
    UPROPERTY() uint8 Abilities[3] = {0, 0, 0}; // ELDDemoAbilityId in Q, W, E order; 0 = empty slot
};

USTRUCT()
struct FLDObservedUnitView
{
    GENERATED_BODY()
    UPROPERTY() FGuid Handle; // fresh for each visibility interval of one observer
    UPROPERTY() uint32 ObservationEpoch = 0;
    UPROPERTY() uint32 StateRevision = 0;
    UPROPERTY() uint32 AuthorizedFieldMask = 0;
    UPROPERTY() ELDObservedUnitKind Kind = ELDObservedUnitKind::Contender;
    UPROPERTY() uint8 Presentation = 0;
    UPROPERTY() FVector_NetQuantize10 FeetCm = FVector::ZeroVector;
    UPROPERTY() uint16 Facing = 0;
    UPROPERTY() ELDObservedActionCue ActionCue = ELDObservedActionCue::None;
    UPROPERTY() uint16 ActionCueAgeMs = 0; // time since the current cue began on the authority clock; 0 without a cue
    UPROPERTY() uint16 ActionCueImpactDelayMs = 0; // action start to authoritative release; 0 for an immediate action or no impact
    UPROPERTY() ELDObservedHitLook HitLook = ELDObservedHitLook::None; // current visible hit cue, never its source or target
    UPROPERTY() uint16 RecentHitAgePlusOneMs = 0; // 0 = no recent hit; otherwise 1 + age on the authority clock
    // The source of a Cast cue, so a visible cast can show its display name at that moment (user-approved amendment of
    // CB-P10): an ELDDemoAbilityId for Q/W, 0 otherwise. No rank, cooldown or learned list is ever carried.
    UPROPERTY() uint8 ActionAbility = 0;
    UPROPERTY() FGuid ContenderId;  // Contender identity -> public roster name
    UPROPERTY() uint8 CreatureId = 0; // CampMember identity -> ELDDemoMonsterId
    UPROPERTY() uint8 Level = 0;
    UPROPERTY() FLDObservedResource Health;
    UPROPERTY() FLDObservedResource Mana;
    UPROPERTY() TArray<FLDObservedStatus> Statuses;
    UPROPERTY() FLDObservedActivesView Actives;
};

USTRUCT()
struct FLDObservedUnitTermination
{
    GENERATED_BODY()
    UPROPERTY() FGuid Handle;
    UPROPERTY() uint32 ObservationEpoch = 0;
    UPROPERTY() uint32 Generation = 0;
    UPROPERTY() uint32 TerminationSequence = 0;
    UPROPERTY() int64 OccurredAtServerMs = 0; // display/archive only
    UPROPERTY() ELDObservedTerminationKind Kind = ELDObservedTerminationKind::ObservedDeath;
};

// One visible flight or placed skill effect. Only public geometry/look, never its caster, rank, cooldown or target.
USTRUCT()
struct FLDObservedProjectileView
{
    GENERATED_BODY()
    UPROPERTY() FGuid Handle; // fresh for each visibility interval of one observer
    UPROPERTY() FVector_NetQuantize10 PositionCm = FVector::ZeroVector; // ground point under the flight
    UPROPERTY() uint16 Heading = 0;
    UPROPERTY() ELDObservedProjectileLook Look = ELDObservedProjectileLook::Chalk;
    UPROPERTY() float RadiusCm = 0;
    UPROPERTY() float InnerRadiusCm = 0;
    UPROPERTY() float AngleDegrees = 0;
    UPROPERTY() uint32 WarningRemainingMs = 0; // time to the authoritative landing/first pulse; 0 when no warning is pending
};

USTRUCT()
struct FLDObservedFootprint
{
    GENERATED_BODY()
    UPROPERTY() ELDObservationSource SourceStatus = ELDObservationSource::NoLivingBody;
    UPROPERTY() FVector_NetQuantize10 SourceFeetCm = FVector::ZeroVector;
    UPROPERTY() float RadiusCm = 0;
    UPROPERTY() TArray<uint8> CurrentBits; // ActiveBody: exactly CoverageBytes; NoLivingBody: empty
};

USTRUCT()
struct FLDWorldView
{
    GENERATED_BODY()
    UPROPERTY() uint32 Schema = 0;
    UPROPERTY() uint32 MatchGeneration = 0;
    UPROPERTY() uint32 ObserverLife = 0;
    UPROPERTY() uint32 Generation = 0; // observation generation; changes with match or observer life/phase
    UPROPERTY() uint32 Revision = 0;   // shared with conceal and termination notices
    UPROPERTY() FName WorldMapId;
    UPROPERTY() uint32 WorldLayoutRevision = 0;
    UPROPERTY() uint32 WorldGeometryRevision = 0;
    UPROPERTY() double SampledAtServerSec = 0;
    UPROPERTY() FLDObservedFootprint Footprint;
    UPROPERTY() TArray<FLDObservedUnitView> Units;
    UPROPERTY() TArray<FLDObservedUnitTermination> RecentTerminations;
    UPROPERTY() TArray<FLDObservedProjectileView> Projectiles;
    bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bSuccess);
};
template<> struct TStructOpsTypeTraits<FLDWorldView> : TStructOpsTypeTraitsBase2<FLDWorldView>
{ enum { WithNetSerializer = true }; };

USTRUCT()
struct FLDObservedConceal
{
    GENERATED_BODY()
    UPROPERTY() uint32 MatchGeneration = 0;
    UPROPERTY() uint32 Generation = 0;
    UPROPERTY() uint32 Revision = 0;
    UPROPERTY() TArray<FGuid> Removed; // empty in the first notice of a new generation
    bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bSuccess);
};
template<> struct TStructOpsTypeTraits<FLDObservedConceal> : TStructOpsTypeTraitsBase2<FLDObservedConceal>
{ enum { WithNetSerializer = true }; };

USTRUCT()
struct FLDObservedTerminationNotice
{
    GENERATED_BODY()
    UPROPERTY() uint32 MatchGeneration = 0;
    UPROPERTY() uint32 Revision = 0;
    UPROPERTY() FLDObservedUnitTermination Termination;
    bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bSuccess);
};
template<> struct TStructOpsTypeTraits<FLDObservedTerminationNotice> : TStructOpsTypeTraitsBase2<FLDObservedTerminationNotice>
{ enum { WithNetSerializer = true }; };

// Server-only public facts supplied by their owning domains through the core. Never serialized.
struct FLDObservedPublicFacts
{
    uint8 Level = 0;
    uint8 CreatureId = 0;
    FLDObservedActivesView Actives;
    TArray<FLDObservedStatus> Statuses;
    // Combat's current action cue and the authority ms it started; None and 0 outside every cue window.
    ELDObservedActionCue ActionCue = ELDObservedActionCue::None;
    int64 ActionCueAtMs = 0;
    int64 ActionCueImpactAtMs = 0;
    int64 LastHitAtMs = 0;
    ELDObservedHitLook HitLook = ELDObservedHitLook::None;
    // Source of a Cast cue (FLDObservedUnitView::ActionAbility): one approved ability id, 0 otherwise.
    uint8 ActionAbility = 0;
};

// Server-only authority record of one projectile in flight, sampled by the observation owner. Never serialized.
struct FLDObservedProjectileFact
{
    FGuid Key;                                   // the release's EffectId
    FGuid SourceEntity;                          // the releasing entity: its isolation group decides who may see it
    FVector PositionCm = FVector::ZeroVector;    // current centre; Z is the releasing foot height
    FVector2D Heading = FVector2D::ZeroVector;   // planar unit direction
    ELDObservedProjectileLook Look = ELDObservedProjectileLook::Chalk;
    float RadiusCm = 0;
    float InnerRadiusCm = 0;
    float AngleDegrees = 0;
    uint32 WarningRemainingMs = 0;
};

USTRUCT()
struct FLDOwnerState
{
    GENERATED_BODY()
    UPROPERTY() uint32 Match = 0;
    UPROPERTY() uint32 Life = 0;
    UPROPERTY() FGuid ContenderId;
    UPROPERTY() FLDCombatBodyRef BodyRef;
    UPROPERTY() ELDLifePhase LifePhase = ELDLifePhase::Unspawned;
    UPROPERTY() uint64 LifeRevision = 0;
    UPROPERTY() int64 RespawnAtMs = 0;
    UPROPERTY() int64 ProtectedUntilMs = 0;
    UPROPERTY() double Hp = 0;
    UPROPERTY() double MaxHp = 0;
    UPROPERTY() bool bReady = false;
    UPROPERTY() bool bRestReady = false;
    UPROPERTY() bool bCanRestReady = false;
    UPROPERTY() ELDRestService NearbyRestService = ELDRestService::None;
    UPROPERTY() bool bLobbyHost = false;
    UPROPERTY() double Mp=0;
    UPROPERTY() double MaxMp=0;
    UPROPERTY() double Barrier=0;
    UPROPERTY() bool bActive = false;
    UPROPERTY() uint64 Revision = 0;
    // Growth: own level, XP and offer as one consistent owner projection.
    UPROPERTY() FLDGrowthOwnerView Growth;
};

namespace LDObservation
{
    LOCKDOWN_API bool DecodeCoverageRuns(TConstArrayView<uint8> Runs, TArray<uint8>& Bits);
    // Shared by NetSerialize and by local Listen Host delivery, which bypasses network serialization.
    LOCKDOWN_API bool ValidateUnit(const FLDObservedUnitView& Unit, FString& Error);
    LOCKDOWN_API bool ValidateProjectile(const FLDObservedProjectileView& Projectile, FString& Error);
    LOCKDOWN_API bool ValidateTermination(const FLDObservedUnitTermination& Termination, FString& Error);
    LOCKDOWN_API bool ValidateView(const FLDWorldView& View, FString& Error);
    LOCKDOWN_API bool ValidateConceal(const FLDObservedConceal& Conceal, FString& Error);
    LOCKDOWN_API bool ValidateNotice(const FLDObservedTerminationNotice& Notice, FString& Error);
    inline bool CoverageBit(const TArray<uint8>& Bits, int32 Index)
    { return Index >= 0 && Index < CoverageCells && Bits.Num() == CoverageBytes && (Bits[Index / 8] & (1 << (Index % 8))) != 0; }
    inline void SetCoverageBit(TArray<uint8>& Bits, int32 Index)
    { if (Index >= 0 && Index < CoverageCells && Bits.Num() == CoverageBytes) { Bits[Index / 8] |= uint8(1 << (Index % 8)); } }
}
