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

// The one current spatial order of a body. Attack kinds and the cast kinds are aggressive intents. CastUnit approaches a
// visible unit until a unit-target Q/W is in reach, CastPoint walks toward GroundCm until a ground-point or projectile
// Q/W reaches it; both then cast (2026-11-06 decisions) and carry that Direct request beside the order
// (ULDNavigationComponent::SubmitCast).
UENUM()
enum class ELDSpatialOrderKind : uint8 { MoveGround, AttackUnit, AttackMove, CastUnit, CastPoint };

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
    // Shift (2026-11-06 user decision): wait behind the current order and the steps already queued instead of
    // replacing them (ULDNavigationComponent shift queue, LDOrderRules::MaxQueuedSteps).
    UPROPERTY() bool bQueue=false;

    bool HasValidShape() const
    {
        if (!Identity.Order || GroundCm.ContainsNaN()) { return false; }
        switch (Kind)
        {
        case ELDSpatialOrderKind::MoveGround:
        case ELDSpatialOrderKind::AttackMove:
        case ELDSpatialOrderKind::CastPoint:
            return !Observed.IsValid() && !ObservationEpoch;
        case ELDSpatialOrderKind::AttackUnit:
        case ELDSpatialOrderKind::CastUnit:
            return GroundCm.IsZero() && SurfaceHint.IsNone();
        default:
            return false;
        }
    }
    bool SameTerms(const FLDSpatialOrderRequest& Other) const
    {
        return Identity==Other.Identity && Kind==Other.Kind && FVector(GroundCm)==FVector(Other.GroundCm)
            && SurfaceHint==Other.SurfaceHint && Observed==Other.Observed
            && ObservationEpoch==Other.ObservationEpoch && bQueue==Other.bQueue;
    }
    bool IsAggressive() const
    {
        return Kind==ELDSpatialOrderKind::AttackUnit || Kind==ELDSpatialOrderKind::AttackMove
            || Kind==ELDSpatialOrderKind::CastUnit || Kind==ELDSpatialOrderKind::CastPoint;
    }
};

UENUM()
enum class ELDOrderStatus : uint8 { Queued, Rejected, Activated, Progress, Completed, Cancelled };

UENUM()
enum class ELDOrderFailure : uint8
{
    None, InvalidRequest, NotOwner, StaleMatch, StaleLife, StaleOrder, RequestConflict, SequenceExhausted,
    InvalidGround, NoCompletePath, PathBlocked, TargetUnavailable, TargetDied,
    Dead, MatchEnded, Superseded, Stopped, Expired,
    // A taunted body takes no order of its own; Navigation chases its taunter instead.
    Taunted,
    // A knockback interrupted the order (#112 §5).
    KnockedBack,
    // The shift queue already holds LDOrderRules::MaxQueuedSteps steps.
    QueueFull,
    // A CastUnit's skill could not be cast (not ready, refused by Combat); its Combat receipt carries the reason.
    CastRefused,
    // #153: the opening countdown admits no new order before the match's T=0.
    NotStarted
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
