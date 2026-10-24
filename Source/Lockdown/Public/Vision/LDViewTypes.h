#pragma once
#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "Development/LDCharacterBuild.h"
#include "Inventory/LDInventoryTypes.h"
#include "LDViewTypes.generated.h"

// Wire records deliberately have no actor, controller, role or private-stat fields.
UENUM() enum class ELDObservedKind : uint8 { Body, Ward, Seal, Book, Charter, WatchControl, Sentinel, WardCharge, Cargo, Production, Institution, Projectile };
USTRUCT()
struct FLDVisionGrid
{
    GENERATED_BODY()
    UPROPERTY() FVector2D Min=FVector2D(-2400,-1600);
    UPROPERTY() float CellSize=80;
    bool Valid() const {return !Min.ContainsNaN()&&FMath::IsFinite(CellSize)&&CellSize>=40&&CellSize<=500&&Min.X>=-20000&&Min.Y>=-20000&&Min.X+60*CellSize<=20000&&Min.Y+40*CellSize<=20000;}
};
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
    UPROPERTY() FLDVisionGrid Grid;
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
    UPROPERTY() TArray<FLDOwnedBook> Books;
    UPROPERTY() FLDInventory Inventory;
    UPROPERTY() uint8 DevelopmentKind=0;
    UPROPERTY() uint32 DevelopmentToken=0;
    UPROPERTY() double DevelopmentEndsAt=0;
    UPROPERTY() FGuid DevelopmentItem;
    UPROPERTY() FGuid CraftCoupon;
    UPROPERTY() uint8 CouponSite=0;
    UPROPERTY() uint8 ProductionSite=0;
    UPROPERTY() double CouponUntil=0;
    UPROPERTY() double ProductionSeconds=0;
    UPROPERTY() FGuid IndustrialEndorsement;
    UPROPERTY() FGuid SupplyEndorsement;
    UPROPERTY() uint8 CoreOrders=0;
    UPROPERTY() uint8 SupplyOrders=0;
    UPROPERTY() FGuid CharterPurchase;
    UPROPERTY() FGuid CharterActivation;
    UPROPERTY() double CharterReadyAt=0;
    UPROPERTY() bool bCharterAwaitingRespawn=false;
    UPROPERTY() FGuid Witness;
    UPROPERTY() FGuid TheftReceipt;
    UPROPERTY() uint8 TheftOutcome=0;
    UPROPERTY() int64 TheftAmount=0;
    UPROPERTY() double TheftRetryAt=0;
    UPROPERTY() double AlertUntil=0;
    UPROPERTY() uint32 TamperSerial=0;
    UPROPERTY() double Mp=0;
    UPROPERTY() double MaxMp=0;
    UPROPERTY() double Barrier=0;
    UPROPERTY() uint16 StatusMask=0;
    UPROPERTY() FString StatusFeedback;
    UPROPERTY() double NullBellReadyAt=0;
    UPROPERTY() int64 Gold = 0;
    UPROPERTY() int64 BankGold = 0;
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
    UPROPERTY() uint8 PaidJobs=0;
    UPROPERTY() uint8 FirstCourierTown=0;
    UPROPERTY() uint8 ContractDestination=0;
    UPROPERTY() TArray<uint8> VisitedRegions;
    UPROPERTY() uint8 PaidSentinels=0;
    UPROPERTY() FGuid SentinelReceipt;
    UPROPERTY() uint32 DeliverySerial = 0;
    UPROPERTY() float WorkReadyAt = 0;
    UPROPERTY() FGuid SealId;
    // Private once-per-match receipt; survives death, dropping and consuming the item.
    UPROPERTY() FGuid FabricationReceipt;
    UPROPERTY() uint8 ChannelKind = 0;
    UPROPERTY() uint8 ChannelRoute = 0;
    UPROPERTY() float ChannelStartedAt = 0;
    UPROPERTY() float ChannelProgress = 0;
    UPROPERTY() bool bWinner = false;
};

namespace LDVision
{
    inline bool PickupKind(ELDObservedKind Kind)
    {return Kind==ELDObservedKind::Book||Kind==ELDObservedKind::Seal||Kind==ELDObservedKind::WardCharge||Kind==ELDObservedKind::Cargo;}
    constexpr int32 Columns = 60, Rows = 40, Cells = Columns * Rows, MaskBytes = Cells / 8;
    // Four full 24-slot death drops, bodies, wards and short-lived corpse identities.
    // Still bounded by the existing uint8 count; the observed field mask is unchanged.
    constexpr int32 MaxEntities = 128;
    inline bool InWorld(const FVector& P,const FLDVisionGrid& G=FLDVisionGrid()) { return G.Valid()&&!P.ContainsNaN()&&P.X>=G.Min.X&&P.X<G.Min.X+Columns*G.CellSize&&P.Y>=G.Min.Y&&P.Y<G.Min.Y+Rows*G.CellSize; }
    inline int32 Cell(const FVector& P,const FLDVisionGrid& G=FLDVisionGrid()) { return InWorld(P,G)?int32((P.Y-G.Min.Y)/G.CellSize)*Columns+int32((P.X-G.Min.X)/G.CellSize):INDEX_NONE; }
    inline FVector Centre(int32 Index,const FLDVisionGrid& G=FLDVisionGrid()) { return FVector(G.Min.X+((Index%Columns)+.5)*G.CellSize,G.Min.Y+((Index/Columns)+.5)*G.CellSize,0); }
    inline bool Bit(const TArray<uint8>& Mask, int32 Index) { return Index >= 0 && Index < Cells && Mask.Num() == MaskBytes && (Mask[Index/8] & (1 << (Index%8))); }
    inline void Set(TArray<uint8>& Mask, int32 Index) { Mask[Index/8] |= 1 << (Index%8); }
}
