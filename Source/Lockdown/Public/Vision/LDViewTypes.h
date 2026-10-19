#pragma once
#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "Development/LDCharacterBuild.h"
#include "LDViewTypes.generated.h"

// Wire records deliberately have no actor, controller, role or private-stat fields.
UENUM() enum class ELDObservedKind : uint8 { Body, Ward, Seal };
USTRUCT()
struct FLDObservedEntity
{
    GENERATED_BODY()
    UPROPERTY() FGuid Id;
    UPROPERTY() FVector_NetQuantize10 Position = FVector::ZeroVector;
    UPROPERTY() uint16 Facing = 0;
    UPROPERTY() uint16 Speed = 0;
    UPROPERTY() uint8 Presentation = 0;
    UPROPERTY() ELDObservedKind Kind = ELDObservedKind::Body;
    UPROPERTY() uint8 ActionCue = 0;
    UPROPERTY() float ExposedUntil = 0;
};

USTRUCT()
struct FLDWorldView
{
    GENERATED_BODY()
    UPROPERTY() uint32 Generation = 0;
    UPROPERTY() uint64 Revision = 0;
    UPROPERTY() float AuthoritySeconds = 0;
    UPROPERTY() TArray<uint8> Current;
    UPROPERTY() TArray<uint8> Explored;
    UPROPERTY() TArray<FLDObservedEntity> Entities;
    UPROPERTY() uint8 BodySources = 0;
    UPROPERTY() uint8 WardSources = 0;
    UPROPERTY() uint8 PublicSources = 0;
    bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bSuccess);
};
template<> struct TStructOpsTypeTraits<FLDWorldView> : TStructOpsTypeTraitsBase2<FLDWorldView>
{ enum { WithNetSerializer = true }; };

USTRUCT()
struct FLDOwnerState
{
    GENERATED_BODY()
    UPROPERTY() uint32 Match = 0;
    UPROPERTY() uint32 Life = 1;
    UPROPERTY() double Hp = 100;
    UPROPERTY() double MaxHp = 100;
    UPROPERTY() bool bDevelopmentProfile = false;
    UPROPERTY() bool bReady = false;
    UPROPERTY() bool bLobbyHost = false;
    UPROPERTY() FLDCharacterBuild Build;
    UPROPERTY() int64 Gold = 0;
    UPROPERTY() int64 Xp = 0;
    UPROPERTY() uint8 WardCharges = 1;
    UPROPERTY() bool bActive = true;
    UPROPERTY() uint32 Revision = 0;
    UPROPERTY() uint32 DeathSerial = 0;
    UPROPERTY() int64 LastLostGold = 0;
    UPROPERTY() float ReturnAt = 0;
    UPROPERTY() float ProtectedUntil = 0;
    UPROPERTY() float BasicReadyAt = 0;
    UPROPERTY() ELDSkill ActiveSkill = ELDSkill::None;
    UPROPERTY() double SkillEndsAt = 0;
    UPROPERTY() float AssassinReadyAt = 0;
    UPROPERTY() float DashReadyAt = 0;
    UPROPERTY() float WarningUntil = 0;
    UPROPERTY() float DamageUntil = 0;
    UPROPERTY() float ExposedUntil = 0;
    UPROPERTY() uint8 IncomingSector = 0;
    UPROPERTY() uint8 ActionCue = 0;
    UPROPERTY() uint8 ContractStage = 0; // 0 none, 1 collect, 2 deliver
    UPROPERTY() FGuid ContractId;
    UPROPERTY() int32 Commissions = 0;
    UPROPERTY() uint32 DeliverySerial = 0;
    UPROPERTY() float WorkReadyAt = 0;
    UPROPERTY() FGuid SealId;
    UPROPERTY() uint8 ChannelKind = 0;
    UPROPERTY() uint8 ChannelRoute = 0;
    UPROPERTY() float ChannelStartedAt = 0;
    UPROPERTY() float ChannelProgress = 0;
    UPROPERTY() bool bWinner = false;
};

namespace LDVision
{
    constexpr int32 Columns = 60, Rows = 40, Cells = Columns * Rows, MaskBytes = Cells / 8;
    // Eight contenders, eight residents, wards and short-lived corpse identities.
    constexpr int32 MaxEntities = 64;
    inline bool InWorld(const FVector& P) { return !P.ContainsNaN() && P.X >= -2400 && P.X < 2400 && P.Y >= -1600 && P.Y < 1600; }
    inline int32 Cell(const FVector& P) { return InWorld(P) ? int32((P.Y+1600)/80)*Columns+int32((P.X+2400)/80) : INDEX_NONE; }
    inline FVector Centre(int32 Index) { return FVector(-2360+(Index%Columns)*80, -1560+(Index/Columns)*80,0); }
    inline bool Bit(const TArray<uint8>& Mask, int32 Index) { return Index >= 0 && Index < Cells && Mask.Num() == MaskBytes && (Mask[Index/8] & (1 << (Index%8))); }
    inline void Set(TArray<uint8>& Mask, int32 Index) { Mask[Index/8] |= 1 << (Index%8); }
}
