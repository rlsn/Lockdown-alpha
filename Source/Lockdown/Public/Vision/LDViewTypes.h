#pragma once
#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "Framework/LDMatchTimeline.h"
#include "Development/LDGrowthProtocol.h"
#include "World/LDFieldPacing.h"
#include "LDViewTypes.generated.h"

// Observation wire records are owner-directed authorized projections. They carry no actor,
// controller, canonical body identity, life key or private build/economy/cooldown field.
namespace LDObservation
{
    // 19: N4 bags and the wall between two bags are public obstacles (Bag, BagWall looks).
    // 18: a frame carries the public ground states in sight (16 bytes each on the wire); the Wind look shows a fan's blow.
    // 17: a contender carries a quantized momentum bar and its overdrive charge; an Attack cue carries its combo step.
    // 16 adds the observer-local identity of a visible hit and owner hit receipts; the Hit reaction cue is retired.
    // 13 distinguishes visible return flights without carrying their caster or build.
    // 12 adds the authorized hit's critical and shield outcome.
    // 11 adds the age of an authorized hit, independent of the current action cue.
    // 10 adds the recent hit presentation and public area warning countdown.
    // 9 adds the authoritative release delay to public action cues.
    // 8 (#198): Q/W/E carry single-level ability ids without ranks; the cowering and contact-guard statuses are gone.
    // 20 widens the conceal count for the authored roster plus the full flight bound.
    // 21 packs public status expiry times without losing precision.
    // 22 moves camp-clear recovery orbs into sight-authorized FLDWorldView instead of room-public GameState.
    constexpr uint32 Schema = 22;
    // A ground state's wire size (LDViewTypes.cpp SerializeObservedGround): SC-07 registers MaxGroundStates of them per frame.
    constexpr int32 GroundStateWireBytes = 16;
    // The public momentum bar of a visible contender: 0..MomentumBarFull (never the exact value).
    constexpr uint8 MomentumBarFull = 255;
    // The public shield bar: a shield of at least the maximum HP is full.
    constexpr uint8 ShieldBarFull = 255;
    constexpr uint16 RecentHitWindowMs = 300;
    // A visible reaction's name floats over its target while its cue is this young (spec §12).
    constexpr uint16 RecentReactionWindowMs = 1000;
    // Overdriven: the hit came from an overdrive release. Public like the charge that preceded it (spec: the charge ring
    // shows to everyone who sees the body); the owner receipt carries the same bit (LDCombatCues::DealtOverdrive).
    enum EHitFlags : uint8 { Critical = 1, ShieldAbsorbed = 2, ShieldBroken = 4, Overdriven = 16 };
    constexpr uint8 HitFlagsMask = Critical | ShieldAbsorbed | ShieldBroken | Overdriven;
    // Display coverage over the registered ground surface bounds, low bit first.
    // The coverage grid over the ground surface: CoverageWidth columns along X, CoverageHeight rows along Y (the 21000 x
    // 10000 cm ground gives cells of about 82 x 78 cm). Bit index = row * CoverageWidth + column.
    constexpr int32 CoverageWidth = 256;
    constexpr int32 CoverageHeight = 128;
    constexpr int32 CoverageCells = CoverageWidth * CoverageHeight;
    constexpr int32 CoverageBytes = CoverageCells / 8;
    // Bounded uint8 wire counts permit authored camp instances. Admission counts the actual scene roster,
    // every other contender and the Boss; no hidden unit is dropped to fit this capacity.
    // BeginMatch also checks every other resting contender, one owned dummy per contender and all authored shared targets.
    // Lockdown.Rules.CampDecision fails when the approved camps outgrow this bound.
    constexpr int32 MaxUnits = 192;
    // Every public status a unit can show at once: each definition appears at most once, and each kind adds its marker
    // to the combat cues (Lockdown.Rules.DemoVision.FullStatusBound).
    constexpr int32 MaxStatuses = 19;
    constexpr int32 MaxTerminations = 16;
    // Visible flights and skill areas per frame; the nearest are kept when more are in sight.
    constexpr int32 MaxProjectiles = 128;
    // Spec §8.3/§14: every live ground state of the map at once (GROUND.MaxTotal fits it); each is one compact public record.
    constexpr int32 MaxGroundStates = 40;
    constexpr int32 MaxRecoveryOrbs = 48;
    static_assert(MaxUnits + MaxProjectiles <= MAX_uint16, "Conceal count must represent all authorized handles.");
    // One identity tint per seat of the six-contender roster (APP_SPEC 1).
    constexpr uint8 MaxPresentations = 6;
    constexpr int32 MaxMapIdBytes = 63;
    // Bit 32 (the retired equipment slots, #144) is never reused.
    enum EField : uint32 { Identity = 1, Level = 2, Health = 4, Momentum = 8, Statuses = 16, Actives = 64 };
    constexpr uint32 ContenderFields = Identity | Level | Health | Momentum | Statuses | Actives;
    constexpr uint32 CampFields = Identity | Health | Statuses;
}

UENUM() enum class ELDObservedUnitKind : uint8 { Contender = 1, CampMember = 2 };
// Combat owns visible action cues; adding one is a schema change.
// 3 (the retired Hit reaction cue) is a hole: a landed hit is only the RecentHit fields and never poses the body.
UENUM() enum class ELDObservedActionCue : uint8 { None = 0, Attack = 1, Cast = 2 };
UENUM() enum class ELDObservedHitLook : uint8 { None = 0, Magic = 1, Fire = 2, Water = 3, Electric = 4 };
// Declared before the wire projectile record that carries it; Combat maps each projectile source to one look.
UENUM() enum class ELDObservedProjectileLook : uint8
{
    Chalk = 1, Sock = 2, BookShadow = 3, FlyerArming = 4, FlyerArmed = 5, SpeechCone = 6,
    AreaPlain = 7, AreaMagic = 8, AreaFire = 9, AreaWater = 10, AreaElectric = 11,
    ChalkReturn = 12, SockReturn = 13, Road = 14, ConeWarning = 15, LineWarning = 16, Wind = 17,
    // N4: a school bag (RadiusCm = its body) and the wall between two bags (LengthCm, RadiusCm = half width).
    Bag = 18, BagWall = 19
};
// Spec §8.3 ground-state footprints: a circle, D10's band (ExtentCm = half length) or N1's sector (ExtentCm = angle).
UENUM() enum class ELDObservedGroundShape : uint8 { Circle = 0, Band = 1, Sector = 2 };
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

// The three Q/W/E actives of a visible contender. No rank, branch, cooldown, momentum cost or other field.
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
    UPROPERTY() FGuid RecentHitId; // observer-local identity of the visible hit
    UPROPERTY() uint8 HitFlags = 0; // LDObservation::EHitFlags for the recent hit
    // The source of a Cast cue, so a visible cast can show its display name at that moment (user-approved amendment of
    // CB-P10): an ELDDemoAbilityId for Q/W, 0 otherwise. No rank, cooldown or learned list is ever carried.
    UPROPERTY() uint8 ActionAbility = 0;
    UPROPERTY() FGuid ContenderId;  // Contender identity -> public roster name
    UPROPERTY() uint8 CreatureId = 0; // CampMember identity -> ELDDemoMonsterId
    // CampMember only (spec §11.2/§11.3): its camp spawn's affix (ELDCampAffix) and whether it is the second-hand dealer.
    UPROPERTY() uint8 CampAffix = 0;
    UPROPERTY() bool bPeddler = false;
    UPROPERTY() uint8 Level = 0;
    UPROPERTY() FLDObservedResource Health;
    // The live shield as a share of the maximum HP, quantized to 0..LDObservation::ShieldBarFull (never the exact amount);
    // sent with the health field, so the head bar can show how thick it is.
    UPROPERTY() uint8 ShieldBar = 0;
    // Contenders: the momentum bar quantized to 0..LDObservation::MomentumBarFull and whether an overdrive charge shows.
    UPROPERTY() uint8 MomentumBar = 0;
    UPROPERTY() bool bCharging = false;
    // How long the shown charge has been held on the authority clock (0 without a charge), so a body entering sight
    // mid-hold shows the charge where it is.
    UPROPERTY() uint16 ChargeAgeMs = 0;
    // Contenders: the shown Cast cue is an overdrive release (authorized with the momentum bar, like the charge).
    UPROPERTY() bool bOverdriveCast = false;
    // The combo step (1..3) of an Attack cue; 0 for every other cue.
    UPROPERTY() uint8 ActionComboStep = 0;
    // The reaction this unit suffered (ELDCombatReaction 1..15) and 1 + its age; 0 = none within the window.
    UPROPERTY() uint8 RecentReaction = 0;
    UPROPERTY() uint16 RecentReactionAgePlusOneMs = 0;
    // The element that set that reaction off (ELDCombatElement 1..6); 0 with no reaction.
    UPROPERTY() uint8 RecentReactionElement = 0;
    UPROPERTY() TArray<FLDObservedStatus> Statuses;
    UPROPERTY() FLDObservedActivesView Actives;
};

// Owner-only presentation of a committed hit on a currently visible target; it never names a canonical body.
USTRUCT()
struct FLDObservedHitReceipt
{
    GENERATED_BODY()
    UPROPERTY() uint32 MatchGeneration = 0;
    UPROPERTY() uint32 ObserverLife = 0;
    UPROPERTY() uint32 Generation = 0;
    UPROPERTY() FGuid Handle;
    UPROPERTY() uint32 ObservationEpoch = 0;
    UPROPERTY() FGuid HitId;
    UPROPERTY() int64 OccurredAtMs = 0;
    UPROPERTY() ELDObservedHitLook HitLook = ELDObservedHitLook::None;
    UPROPERTY() uint8 HitFlags = 0;
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
    UPROPERTY() float LengthCm = 0;
    UPROPERTY() bool bOwn = false;
    UPROPERTY() uint32 WarningRemainingMs = 0; // time to the authoritative landing/first pulse; 0 when no warning is pending
};

// Spec §8.3/§14: one public ground state in sight: its kind (ELDGroundStateKind 1..6), footprint, owner's seat and remaining
// life (its outline fades by Remaining/Life). Id is the low byte of its authority identity, stable while it lives. On the
// wire it is 16 bytes: position to 1 cm (Z to 2 cm), radius to 4 cm, extent to 8 cm or 2 degrees, heading in 256 steps and
// times in 50 ms steps.
USTRUCT()
struct FLDObservedGroundState
{
    GENERATED_BODY()
    UPROPERTY() uint8 Id = 0;
    UPROPERTY() uint8 Kind = 0;
    UPROPERTY() ELDObservedGroundShape Shape = ELDObservedGroundShape::Circle;
    UPROPERTY() uint8 OwnerSeat = 255; // the owner's roster seat 0..5; 255 = none
    UPROPERTY() FVector CentreCm = FVector::ZeroVector;
    UPROPERTY() float RadiusCm = 0;
    UPROPERTY() float ExtentCm = 0;
    UPROPERTY() uint8 Heading = 0;
    UPROPERTY() uint32 RemainingMs = 0;
    UPROPERTY() uint32 TotalMs = 0; // its whole duration (the decal fades over it)
};

// Authorized camp-clear recovery orb currently visible in the observer's sight; hidden orbs outside fog never replicate.
USTRUCT()
struct FLDObservedRecoveryOrb
{
    GENERATED_BODY()
    UPROPERTY() uint64 Serial = 0;
    UPROPERTY() FVector_NetQuantize10 PositionCm = FVector::ZeroVector;
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
struct LOCKDOWN_API FLDWorldView
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
    UPROPERTY() TArray<FLDObservedGroundState> GroundStates;
    UPROPERTY() TArray<FLDObservedRecoveryOrb> RecoveryOrbs;
    bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bSuccess);
};
template<> struct TStructOpsTypeTraits<FLDWorldView> : TStructOpsTypeTraitsBase2<FLDWorldView>
{ enum { WithNetSerializer = true }; };

USTRUCT()
struct LOCKDOWN_API FLDObservedConceal
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
struct LOCKDOWN_API FLDObservedTerminationNotice
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
    uint8 CampAffix = 0;
    bool bPeddler = false;
    FLDObservedActivesView Actives;
    TArray<FLDObservedStatus> Statuses;
    // Combat's current action cue and the authority ms it started; None and 0 outside every cue window.
    ELDObservedActionCue ActionCue = ELDObservedActionCue::None;
    int64 ActionCueAtMs = 0;
    int64 ActionCueImpactAtMs = 0;
    int64 LastHitAtMs = 0;
    FGuid LastHitId;
    ELDObservedHitLook HitLook = ELDObservedHitLook::None;
    uint8 HitFlags = 0;
    // Source of a Cast cue (FLDObservedUnitView::ActionAbility): one approved ability id, 0 otherwise.
    uint8 ActionAbility = 0;
    // The combo step of an Attack cue and a held overdrive charge (contenders).
    uint8 ActionComboStep = 0;
    bool bCharging = false;
    int64 ChargeAtMs = 0;
    bool bOverdriveCast = false;
    uint8 MomentumBar = 0;
    // The last reaction on this body within LDObservation::RecentReactionWindowMs, and when.
    uint8 LastReaction = 0;
    int64 LastReactionAtMs = 0;
    uint8 LastReactionElement = 0;
};

// Server-only public fact of one live ground state (ALDCombatDirector::ReadObservedGround). Never serialized.
struct FLDObservedGroundFact
{
    uint32 Id = 0;
    uint8 Kind = 0;
    FGuid OwnerEntity;
    uint8 OwnerSeat = 255;
    FVector CentreCm = FVector::ZeroVector;
    float RadiusCm = 0;
    FVector2D Heading = FVector2D(1, 0);
    float HalfLengthCm = 0;
    float AngleDegrees = 0;
    uint32 RemainingMs = 0;
    uint32 LifeMs = 0;
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
    float LengthCm = 0;
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
    UPROPERTY() bool bLobbyHost = false;
    UPROPERTY() double Barrier=0;
    UPROPERTY() bool bActive = false;
    UPROPERTY() uint64 Revision = 0;
    // Growth: own level, XP and offer as one consistent owner projection.
    UPROPERTY() FLDGrowthOwnerView Growth;
    // P6: the death screen's spawn choice.
    UPROPERTY() FLDOwnerPacingView Pacing;
};

namespace LDObservation
{
    LOCKDOWN_API bool DecodeCoverageRuns(TConstArrayView<uint8> Runs, TArray<uint8>& Bits);
    // Shared by NetSerialize and by local Listen Host delivery, which bypasses network serialization.
    LOCKDOWN_API bool ValidateUnit(const FLDObservedUnitView& Unit, FString& Error);
    LOCKDOWN_API bool ValidateProjectile(const FLDObservedProjectileView& Projectile, FString& Error);
    LOCKDOWN_API bool ValidateGround(const FLDObservedGroundState& Ground, FString& Error);
    // The wire form of a ground fact (quantized exactly as SerializeObservedGround keeps it).
    LOCKDOWN_API FLDObservedGroundState GroundView(const FLDObservedGroundFact& Fact);
    LOCKDOWN_API bool ValidateTermination(const FLDObservedUnitTermination& Termination, FString& Error);
    LOCKDOWN_API bool ValidateView(const FLDWorldView& View, FString& Error);
    LOCKDOWN_API bool ValidateConceal(const FLDObservedConceal& Conceal, FString& Error);
    LOCKDOWN_API bool ValidateNotice(const FLDObservedTerminationNotice& Notice, FString& Error);
    inline bool CoverageBit(const TArray<uint8>& Bits, int32 Index)
    { return Index >= 0 && Index < CoverageCells && Bits.Num() == CoverageBytes && (Bits[Index / 8] & (1 << (Index % 8))) != 0; }
    inline void SetCoverageBit(TArray<uint8>& Bits, int32 Index)
    { if (Index >= 0 && Index < CoverageCells && Bits.Num() == CoverageBytes) { Bits[Index / 8] |= uint8(1 << (Index % 8)); } }
}
