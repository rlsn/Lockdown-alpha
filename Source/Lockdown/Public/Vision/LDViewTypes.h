#pragma once
#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "Development/LDCharacterBuild.h"
#include "Inventory/LDInventoryTypes.h"
#include "LDViewTypes.generated.h"

// Observation wire records are owner-directed authorized projections. They carry no actor,
// controller, canonical body identity, life key or private build/economy/cooldown field.
namespace LDObservation
{
    constexpr uint32 Schema = 1;
    // Display coverage over the registered ground surface bounds: index = y * Side + x, low bit first.
    constexpr int32 CoverageSide = 128;
    constexpr int32 CoverageCells = CoverageSide * CoverageSide;
    constexpr int32 CoverageBytes = CoverageCells / 8;
    // BeginMatch rejects a locked roster plus approved camp members that could exceed this.
    constexpr int32 MaxUnits = 16;
    constexpr int32 MaxStatuses = 8;
    constexpr int32 MaxTerminations = 16;
    constexpr int32 MaxMapIdBytes = 63;
    enum EField : uint32 { Identity = 1, Level = 2, Health = 4, Mana = 8, Statuses = 16, Equipment = 32 };
    constexpr uint32 ContenderFields = Identity | Level | Health | Mana | Statuses | Equipment;
    constexpr uint32 CampFields = Identity | Health | Statuses;
}

UENUM() enum class ELDObservedUnitKind : uint8 { Contender = 1, CampMember = 2 };
// Combat owns visible action cues; adding one is a schema change.
UENUM() enum class ELDObservedActionCue : uint8 { None = 0 };
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

USTRUCT()
struct FLDObservedEquipmentView
{
    GENERATED_BODY()
    UPROPERTY() uint8 Slots[2] = {0, 0}; // ELDDemoEquipmentId; 0 = empty slot
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
    UPROPERTY() FGuid ContenderId;  // Contender identity -> public roster name
    UPROPERTY() uint8 CreatureId = 0; // CampMember identity -> ELDDemoMonsterId
    UPROPERTY() uint8 Level = 0;
    UPROPERTY() FLDObservedResource Health;
    UPROPERTY() FLDObservedResource Mana;
    UPROPERTY() TArray<FLDObservedStatus> Statuses;
    UPROPERTY() FLDObservedEquipmentView Equipment;
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
    FLDObservedEquipmentView Equipment;
    TArray<FLDObservedStatus> Statuses;
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
    UPROPERTY() bool bLobbyHost = false;
    UPROPERTY() FLDCharacterBuild Build;
    UPROPERTY() FLDInventory Inventory;
    UPROPERTY() double Mp=0;
    UPROPERTY() double MaxMp=0;
    UPROPERTY() double Barrier=0;
    UPROPERTY() int64 Gold = 0;
    UPROPERTY() int64 Xp = 0;
    UPROPERTY() bool bActive = false;
    UPROPERTY() uint64 Revision = 0;
};

namespace LDObservation
{
    // Shared by NetSerialize and by local Listen Host delivery, which bypasses network serialization.
    LOCKDOWN_API bool ValidateUnit(const FLDObservedUnitView& Unit, FString& Error);
    LOCKDOWN_API bool ValidateTermination(const FLDObservedUnitTermination& Termination, FString& Error);
    LOCKDOWN_API bool ValidateView(const FLDWorldView& View, FString& Error);
    LOCKDOWN_API bool ValidateConceal(const FLDObservedConceal& Conceal, FString& Error);
    LOCKDOWN_API bool ValidateNotice(const FLDObservedTerminationNotice& Notice, FString& Error);
    inline bool CoverageBit(const TArray<uint8>& Bits, int32 Index)
    { return Index >= 0 && Index < CoverageCells && Bits.Num() == CoverageBytes && (Bits[Index / 8] & (1 << (Index % 8))) != 0; }
    inline void SetCoverageBit(TArray<uint8>& Bits, int32 Index)
    { if (Index >= 0 && Index < CoverageCells && Bits.Num() == CoverageBytes) { Bits[Index / 8] |= uint8(1 << (Index % 8)); } }
}
