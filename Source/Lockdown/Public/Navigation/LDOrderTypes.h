#pragma once

#include "CoreMinimal.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "LDOrderTypes.generated.h"

class AController;

// Also carried by saved native moves. No route points or actor pointers cross the wire.
USTRUCT()
struct FLDMoveIdentity
{
    GENERATED_BODY()
    UPROPERTY() uint32 Match=0;
    UPROPERTY() uint32 Life=0;
    UPROPERTY() uint32 Order=0;
    UPROPERTY() uint32 Epoch=0;
    bool operator==(const FLDMoveIdentity& R) const {return Match==R.Match&&Life==R.Life&&Order==R.Order&&Epoch==R.Epoch;}
    bool operator!=(const FLDMoveIdentity& R) const {return !(*this==R);}
};

// The one current spatial order of a body. Attack kinds are aggressive intents.
UENUM()
enum class ELDSpatialOrderKind : uint8 { MoveGround, AttackUnit, AttackMove, ApproachShop };

// Exactly one target arm is meaningful for each kind; the others must stay at their defaults.
USTRUCT()
struct FLDSpatialOrderRequest
{
    GENERATED_BODY()
    UPROPERTY() FLDMoveIdentity Identity;
    UPROPERTY() ELDSpatialOrderKind Kind=ELDSpatialOrderKind::MoveGround;
    // MoveGround / AttackMove: footpoint destination and the registered surface id from the public ground trace.
    UPROPERTY() FVector_NetQuantize10 GroundCm=FVector::ZeroVector;
    UPROPERTY() FName SurfaceHint;
    // AttackUnit: observer-local opaque handle and observation epoch.
    UPROPERTY() FGuid Observed;
    UPROPERTY() uint32 ObservationEpoch=0;
    // ApproachShop: public POI id.
    UPROPERTY() FName ShopId;

    bool HasValidShape() const
    {
        if (!Identity.Order || GroundCm.ContainsNaN()) { return false; }
        switch (Kind)
        {
        case ELDSpatialOrderKind::MoveGround:
        case ELDSpatialOrderKind::AttackMove:
            return !Observed.IsValid() && !ObservationEpoch && ShopId.IsNone();
        case ELDSpatialOrderKind::AttackUnit:
            return GroundCm.IsZero() && SurfaceHint.IsNone() && ShopId.IsNone();
        case ELDSpatialOrderKind::ApproachShop:
            return GroundCm.IsZero() && SurfaceHint.IsNone() && !Observed.IsValid() && !ObservationEpoch && !ShopId.IsNone();
        default:
            return false;
        }
    }
    bool SameTerms(const FLDSpatialOrderRequest& Other) const
    {
        return Identity==Other.Identity && Kind==Other.Kind && FVector(GroundCm)==FVector(Other.GroundCm)
            && SurfaceHint==Other.SurfaceHint && Observed==Other.Observed
            && ObservationEpoch==Other.ObservationEpoch && ShopId==Other.ShopId;
    }
    bool IsAggressive() const { return Kind==ELDSpatialOrderKind::AttackUnit || Kind==ELDSpatialOrderKind::AttackMove; }
};

UENUM()
enum class ELDOrderStatus : uint8 { Queued, Rejected, Activated, Progress, Completed, Cancelled };

UENUM()
enum class ELDOrderFailure : uint8
{
    None, InvalidRequest, NotOwner, StaleMatch, StaleLife, StaleOrder, RequestConflict, SequenceExhausted,
    InvalidGround, NoCompletePath, PathBlocked, TargetUnavailable, TargetDied, ShopUnavailable,
    Dead, MatchEnded, Superseded, Stopped, Expired,
    // A taunted body takes no order of its own; Navigation chases its taunter instead.
    Taunted
};

// Owner-only order receipt. Goal is only ever a public ground point or a visible target's approach point.
USTRUCT()
struct FLDOrderReceipt
{
    GENERATED_BODY()
    UPROPERTY() FLDMoveIdentity Identity;
    UPROPERTY() ELDSpatialOrderKind Kind=ELDSpatialOrderKind::MoveGround;
    UPROPERTY() ELDOrderStatus Status=ELDOrderStatus::Rejected;
    UPROPERTY() ELDOrderFailure Failure=ELDOrderFailure::InvalidRequest;
    UPROPERTY() uint64 ReceiptSerial=0;
    UPROPERTY() bool bHasGoal=false;
    UPROPERTY() FVector_NetQuantize10 GoalCm=FVector::ZeroVector;
    UPROPERTY() float AcceptanceCm=0;
    // Within range and handing basic attacks to Combat: the owner stops its local steering.
    UPROPERTY() bool bEngaged=false;
    UPROPERTY() bool bProtectionRemoved=false;
    UPROPERTY() uint64 LifeRevision=0;
};

// Server-only caller binding for SubmitOrder/CancelOrder/QueryOrder; never serialized.
struct FLDOrderCaller
{
    FLDMatchContext Match;
    FLDCombatBodyRef Body;
    TWeakObjectPtr<AController> Controller;
};

UENUM()
enum class ELDOrderCancelReason : uint8 { Stop, FocusLost, LifeEnded, Left, MatchClosed };
