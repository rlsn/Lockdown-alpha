#include "Navigation/LDNavigationComponent.h"
#include "Navigation/LDOrderRules.h"
#include "Navigation/LDPathSteering.h"
#include "Framework/LDCommandLiveness.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Combat/LDCombatTypes.h"
#include "Combat/LDCombatDirector.h"
#include "Vision/LDVisionDirector.h"
#include "Development/LDDemoContentCatalog.h"
#include "Region/LDMapRegistry.h"
#include "Engine/World.h"

// Authority order book: one current order per contender body, Navigation is its only writer.
namespace LDNavOrdersDetail
{
    ELDOrderFailure FromWorld(ELDWorldQueryFailure Failure)
    {
        switch (Failure)
        {
        case ELDWorldQueryFailure::IncompletePath:
        case ELDWorldQueryFailure::Unreachable:
        case ELDWorldQueryFailure::MissingNavigation:
        case ELDWorldQueryFailure::MissingWorld:
        case ELDWorldQueryFailure::StaleWorld:
            return ELDOrderFailure::NoCompletePath;
        default:
            return ELDOrderFailure::InvalidGround;
        }
    }

    ELDOrderFailure FromCommit(ELDCommitReject Reason)
    {
        switch (Reason)
        {
        case ELDCommitReject::StaleMatch: return ELDOrderFailure::StaleMatch;
        case ELDCommitReject::StaleLife:
        case ELDCommitReject::UnknownBody: return ELDOrderFailure::StaleLife;
        case ELDCommitReject::MatchClosed: return ELDOrderFailure::MatchEnded;
        default: return ELDOrderFailure::InvalidRequest;
        }
    }

    ELDOrderFailure FromCancel(ELDOrderCancelReason Reason)
    {
        switch (Reason)
        {
        case ELDOrderCancelReason::LifeEnded: return ELDOrderFailure::Dead;
        case ELDOrderCancelReason::MatchClosed: return ELDOrderFailure::MatchEnded;
        default: return ELDOrderFailure::Stopped;
        }
    }

    // Pursuit of a pinned unit: AttackUnit always, AttackMove while it holds an acquired target.
    bool IsChase(const FLDSpatialOrderRequest& Request, bool bPinned)
    { return Request.Kind==ELDSpatialOrderKind::AttackUnit || (Request.Kind==ELDSpatialOrderKind::AttackMove && bPinned); }

    FLDAggressiveIntent IntentOf(const FLDSpatialOrderRequest& Request)
    {
        FLDAggressiveIntent Intent;
        Intent.Kind=Request.Kind==ELDSpatialOrderKind::AttackUnit
            ? ELDAggressiveIntentKind::AttackUnit : ELDAggressiveIntentKind::AttackMove;
        Intent.Request.Origin=ELDAggressionOrigin::Navigation;
        Intent.Request.Sequence=Request.Identity.Order;
        Intent.Request.CancelEpoch=Request.Identity.Epoch;
        return Intent;
    }
}

void ULDNavigationComponent::ServerSubmitOrder_Implementation(const FLDSpatialOrderRequest& Request)
{
    FLDOrderCaller Caller;
    ELDOrderFailure Failure=ELDOrderFailure::None;
    if (!ResolveOwnerCaller(Caller,Failure)) { Reply(Request,ELDOrderStatus::Rejected,Failure,false); return; }
    SubmitOrder(Caller,Request);
}

void ULDNavigationComponent::ServerStop_Implementation(FLDMoveIdentity Identity)
{
    FLDOrderCaller Caller;
    ELDOrderFailure Failure=ELDOrderFailure::None;
    if (!ResolveOwnerCaller(Caller,Failure))
    {
        FLDSpatialOrderRequest StopRequest;
        StopRequest.Identity=Identity;
        Reply(StopRequest,ELDOrderStatus::Rejected,Failure,false);
        return;
    }
    CancelOrder(Caller,Identity,ELDOrderCancelReason::Stop);
}

// Refreshes only the lease of the complete current accepted identity; it never creates or revives an order.
void ULDNavigationComponent::ServerHeartbeat_Implementation(FLDMoveIdentity Identity, uint32 Sequence)
{
    const auto* Mode=AuthorityMode();
    FLDOrderCaller Caller;
    ELDOrderFailure Failure=ELDOrderFailure::None;
    if (!Mode || !ActiveOrder || Identity!=ActiveOrder->Request.Identity || Sequence<=ActiveOrder->HeartbeatSequence
        || !ResolveOwnerCaller(Caller,Failure) || Caller.Body!=ActiveOrder->Body) { return; }
    ActiveOrder->HeartbeatSequence=Sequence;
    ActiveOrder->LastHeartbeat=Mode->GetAuthorityNowSeconds();
}

bool ULDNavigationComponent::ResolveOwnerCaller(FLDOrderCaller& Out, ELDOrderFailure& Failure) const
{
    Out={};
    auto* PC=Controller();
    const auto* Mode=AuthorityMode();
    if (!Mode || !PC || !Enabled()) { Failure=ELDOrderFailure::NotOwner; return false; }
    if (!Mode->IsGameplayWriteOpen(Mode->GetMatchContext())) { Failure=ELDOrderFailure::MatchEnded; return false; }
    const auto* Body=Cast<ALDCharacter>(PC->GetPawn());
    if (!Body || Body->GetController()!=PC || !Mode->ReadBodyRef(*Body,Out.Body)) { Failure=ELDOrderFailure::Dead; return false; }
    Out.Match=Mode->GetMatchContext();
    Out.Controller=PC;
    Failure=ELDOrderFailure::None;
    return true;
}

bool ULDNavigationComponent::ValidateCaller(const FLDOrderCaller& Caller, const FLDMoveIdentity& Identity,
    ELDOrderFailure& Failure) const
{
    const auto* PC=Controller();
    const auto* Mode=AuthorityMode();
    if (!Mode || !PC || !Enabled() || Caller.Controller.Get()!=PC) { Failure=ELDOrderFailure::NotOwner; return false; }
    if (Caller.Match!=Mode->GetMatchContext() || Identity.Match!=Caller.Match.Generation)
    { Failure=ELDOrderFailure::StaleMatch; return false; }
    if (!Mode->IsGameplayWriteOpen(Caller.Match)) { Failure=ELDOrderFailure::MatchEnded; return false; }
    const auto* Body=Cast<ALDCharacter>(PC->GetPawn());
    FLDCombatBodyRef Current;
    if (!Body || Body->GetController()!=PC || !Mode->ReadBodyRef(*Body,Current)) { Failure=ELDOrderFailure::Dead; return false; }
    if (Current.Victim.Kind!=ELDVictimKind::Contender) { Failure=ELDOrderFailure::NotOwner; return false; }
    if (Current!=Caller.Body || Identity.Life!=Current.Victim.Life) { Failure=ELDOrderFailure::StaleLife; return false; }
    Failure=ELDOrderFailure::None;
    return true;
}

FLDOrderReceipt ULDNavigationComponent::SubmitOrder(const FLDOrderCaller& Caller, const FLDSpatialOrderRequest& Request)
{
    ELDOrderFailure Failure=ELDOrderFailure::None;
    if (!ValidateCaller(Caller,Request.Identity,Failure)) { return Reply(Request,ELDOrderStatus::Rejected,Failure,false); }
    if (!Request.HasValidShape()) { return Reply(Request,ELDOrderStatus::Rejected,ELDOrderFailure::InvalidRequest,false); }
    EnsureBook(Request.Identity);
    if (const FOrderRecord* Existing=FindRecord(Request.Identity))
    {
        return !Existing->bStop && Existing->Request.SameTerms(Request) ? Replay(*Existing)
            : Reply(Request,ELDOrderStatus::Rejected,ELDOrderFailure::RequestConflict,false);
    }
    if (Request.Identity.Order<=Book.Received.Order || Request.Identity.Epoch<Book.Received.Epoch)
    { return Reply(Request,ELDOrderStatus::Rejected,ELDOrderFailure::StaleOrder,false); }
    return Enqueue(Caller,Request,false,ELDOrderCancelReason::Stop);
}

FLDOrderReceipt ULDNavigationComponent::CancelOrder(const FLDOrderCaller& Caller, const FLDMoveIdentity& Identity,
    ELDOrderCancelReason Reason)
{
    FLDSpatialOrderRequest StopRequest;
    StopRequest.Identity=Identity;
    ELDOrderFailure Failure=ELDOrderFailure::None;
    if (!ValidateCaller(Caller,Identity,Failure)) { return Reply(StopRequest,ELDOrderStatus::Rejected,Failure,false); }
    if (!Identity.Order || uint8(Reason)>uint8(ELDOrderCancelReason::MatchClosed))
    { return Reply(StopRequest,ELDOrderStatus::Rejected,ELDOrderFailure::InvalidRequest,false); }
    EnsureBook(Identity);
    if (const FOrderRecord* Existing=FindRecord(Identity))
    { return Existing->bStop ? Replay(*Existing) : Reply(StopRequest,ELDOrderStatus::Rejected,ELDOrderFailure::RequestConflict,false); }
    if (Identity.Order<=Book.Received.Order || Identity.Epoch<=Book.Received.Epoch)
    { return Reply(StopRequest,ELDOrderStatus::Rejected,ELDOrderFailure::StaleOrder,false); }
    return Enqueue(Caller,StopRequest,true,Reason);
}

FLDOrderReceipt ULDNavigationComponent::QueryOrder(const FLDOrderCaller& Caller, const FLDMoveIdentity& Identity) const
{
    FLDOrderReceipt Out;
    Out.Identity=Identity;
    Out.Status=ELDOrderStatus::Rejected;
    if (!ValidateCaller(Caller,Identity,Out.Failure)) { return Out; }
    if (const FOrderRecord* Record=FindRecord(Identity)) { return Record->Receipt; }
    Out.Failure=Book.IsBoundTo(Identity.Match,Identity.Life) && Identity.Order && Identity.Order<=Book.Received.Order
        ? ELDOrderFailure::Expired : ELDOrderFailure::InvalidRequest;
    return Out;
}

void ULDNavigationComponent::EnsureBook(const FLDMoveIdentity& Identity)
{
    if (Book.IsBoundTo(Identity.Match,Identity.Life)) { return; }
    if (PendingOrder || ActiveOrder)
    {
        UE_LOG(LogTemp,Warning,TEXT("LD_NAV_STALE_LIFE_ORDERS_ENDED match=%u life=%u"),Book.Match,Book.Life);
        EndLifeOrders(ELDOrderCancelReason::LifeEnded);
    }
    ResetBook();
    Book.Bind(Identity);
}

void ULDNavigationComponent::ResetBook()
{
    Records.Reset();
    Book.Reset();
}

void ULDNavigationComponent::ResetMatch()
{
    EndLifeOrders(ELDOrderCancelReason::MatchClosed);
    ResetBook();
    Content.Reset();
}

// ResetLife on the authority: orders of an older life, or of a body the owner no longer holds, end here.
void ULDNavigationComponent::EndStaleServerOrders()
{
    const auto* Mode=AuthorityMode();
    const auto* PC=Controller();
    if (!Mode || !PC) { return; }
    const bool bOldBook=!Book.IsBoundTo(PC->OwnerState.Match,PC->OwnerState.Life);
    const FLDCombatBodyRef* Bound=ActiveOrder?&ActiveOrder->Body:PendingOrder?&PendingOrder->Body:nullptr;
    const auto* Body=Cast<ALDCharacter>(PC->GetPawn());
    FLDCombatBodyRef Current;
    const bool bBodyGone=Bound && (!Body || !Mode->ReadBodyRef(*Body,Current) || Current!=*Bound);
    if (bOldBook || bBodyGone) { EndLifeOrders(bOldBook?ELDOrderCancelReason::LifeEnded:ELDOrderCancelReason::Left); }
    if (bOldBook) { ResetBook(); }
}

auto ULDNavigationComponent::FindRecord(const FLDMoveIdentity& Identity) const -> const FOrderRecord*
{ return Records.FindByPredicate([&Identity](const FOrderRecord& Record) { return Record.Request.Identity==Identity; }); }

auto ULDNavigationComponent::FindRecord(const FLDMoveIdentity& Identity) -> FOrderRecord*
{ return Records.FindByPredicate([&Identity](const FOrderRecord& Record) { return Record.Request.Identity==Identity; }); }

// Bounded window: the pending and active identities are never evicted; older identities answer Expired.
void ULDNavigationComponent::AddRecord(const FLDSpatialOrderRequest& Request, bool bStop)
{
    if (Records.Num()>=MaxOrderRecords)
    {
        const int32 Oldest=Records.IndexOfByPredicate([this](const FOrderRecord& Record)
        {
            return (!PendingOrder || Record.Request.Identity!=PendingOrder->Request.Identity)
                && (!ActiveOrder || Record.Request.Identity!=ActiveOrder->Request.Identity);
        });
        if (Oldest!=INDEX_NONE) { Records.RemoveAt(Oldest); }
    }
    FOrderRecord Record;
    Record.Request=Request;
    Record.bStop=bStop;
    Records.Add(MoveTemp(Record));
}

FLDOrderReceipt ULDNavigationComponent::Enqueue(const FLDOrderCaller& Caller, const FLDSpatialOrderRequest& Request,
    bool bStop, ELDOrderCancelReason Reason)
{
    int64 NotBefore=0;
    FString Error;
    TOptional<FLDAggressiveIntent> Owed;
    if (!AuthorityMode()->CaptureIncomingTime(NotBefore,Error))
    {
        UE_LOG(LogTemp,Warning,TEXT("LD_NAV_INTENT_TIME_REJECTED %s"),*Error);
        return Reply(Request,ELDOrderStatus::Rejected,ELDOrderFailure::MatchEnded,false);
    }
    if (PendingOrder)
    {
        const FPendingOrder Older=MoveTemp(PendingOrder.GetValue());
        PendingOrder.Reset();
        Reply(Older.Request,ELDOrderStatus::Cancelled,bStop?ELDOrderFailure::Stopped:ELDOrderFailure::Superseded,true);
        // A sent aggressive intent that is superseded before Actions still owes its protection removal.
        Owed=Older.OwedAggression.IsSet() ? Older.OwedAggression
            : Older.Request.IsAggressive() && !Older.bStop ? TOptional<FLDAggressiveIntent>(LDNavOrdersDetail::IntentOf(Older.Request))
            : TOptional<FLDAggressiveIntent>();
    }
    Book.Received=Request.Identity;
    FPendingOrder Next;
    Next.Request=Request;
    Next.Match=Caller.Match;
    Next.Body=Caller.Body;
    Next.NotBeforeMs=NotBefore;
    Next.bStop=bStop;
    Next.Reason=Reason;
    Next.OwedAggression=MoveTemp(Owed);
    PendingOrder=MoveTemp(Next);
    AddRecord(Request,bStop);
    return Reply(Request,ELDOrderStatus::Queued,ELDOrderFailure::None,true);
}

FLDOrderReceipt ULDNavigationComponent::Reply(const FLDSpatialOrderRequest& Request, ELDOrderStatus Outcome,
    ELDOrderFailure Failure, bool bStore, bool bProtectionRemoved, uint64 LifeRevision)
{
    FLDOrderReceipt Receipt;
    Receipt.Identity=Request.Identity;
    Receipt.Kind=Request.Kind;
    Receipt.Status=Outcome;
    Receipt.Failure=Failure;
    Receipt.bProtectionRemoved=bProtectionRemoved;
    Receipt.LifeRevision=LifeRevision;
    return Publish(Receipt,bStore);
}

// Same identity and terms: the existing result is sent again, nothing is executed twice.
FLDOrderReceipt ULDNavigationComponent::Replay(const FOrderRecord& Record)
{
    if (IsHumanOwner()) { ClientOrderReceipt(Record.Receipt); }
    return Record.Receipt;
}

FLDOrderReceipt ULDNavigationComponent::Publish(FLDOrderReceipt Receipt, bool bStore)
{
    if (NextReceiptSerial==MAX_uint64)
    {
        UE_LOG(LogTemp,Error,TEXT("LD_NAV_RECEIPT_SERIAL_EXHAUSTED order=%u"),Receipt.Identity.Order);
        return Receipt;
    }
    Receipt.ReceiptSerial=++NextReceiptSerial;
    if (bStore)
    {
        if (FOrderRecord* Record=FindRecord(Receipt.Identity)) { Record->Receipt=Receipt; }
    }
    if (IsHumanOwner()) { ClientOrderReceipt(Receipt); }
    return Receipt;
}

FLDOrderReceipt ULDNavigationComponent::MakeReceipt(const FActiveOrder& Order, ELDOrderStatus Outcome, ELDOrderFailure Failure) const
{
    const bool bChase=LDNavOrdersDetail::IsChase(Order.Request,Order.bPinned);
    FLDOrderReceipt Receipt;
    Receipt.Identity=Order.Request.Identity;
    Receipt.Kind=Order.Request.Kind;
    Receipt.Status=Outcome;
    Receipt.Failure=Failure;
    // Only a public ground goal or the approach point of a target this owner currently sees.
    Receipt.bHasGoal=bChase || !Order.Points.IsEmpty();
    Receipt.GoalCm=bChase?Order.TargetFoot:Order.Goal;
    Receipt.AcceptanceCm=bChase?float(Order.RangeCm):Order.Acceptance;
    Receipt.bEngaged=Order.bEngaged;
    Receipt.bProtectionRemoved=Order.bProtectionRemoved;
    Receipt.LifeRevision=Order.LifeRevision;
    return Receipt;
}

void ULDNavigationComponent::PublishActive(ELDOrderStatus Outcome)
{
    auto& Order=ActiveOrder.GetValue();
    if (Order.bForced) { return; }
    const FLDOrderReceipt Receipt=MakeReceipt(Order,Outcome,ELDOrderFailure::None);
    Order.bSentGoal=Receipt.bHasGoal;
    Order.SentGoal=Receipt.GoalCm;
    Order.bSentEngaged=Receipt.bEngaged;
    Publish(Receipt,true);
}

bool ULDNavigationComponent::LeaseFresh(const ALDGameMode& Mode, const FActiveOrder& Order) const
{ return !Order.bHumanLease || LDCommandLiveness::Fresh(Mode.GetAuthorityNowSeconds(),Order.LastHeartbeat); }

bool ULDNavigationComponent::AuthorizesMove(const FLDMoveIdentity& Identity) const
{
    const auto* Mode=AuthorityMode();
    if (!Mode || !ActiveOrder || Identity!=ActiveOrder->Request.Identity) { return false; }
    const FLDOrderCaller Caller{ActiveOrder->Match,ActiveOrder->Body,Controller()};
    ELDOrderFailure Failure=ELDOrderFailure::None;
    return ValidateCaller(Caller,Identity,Failure) && LeaseFresh(*Mode,*ActiveOrder);
}

void ULDNavigationComponent::EndLifeOrders(ELDOrderCancelReason Reason)
{
    const ELDOrderFailure Failure=LDNavOrdersDetail::FromCancel(Reason);
    if (PendingOrder)
    {
        const FPendingOrder Dropped=MoveTemp(PendingOrder.GetValue());
        PendingOrder.Reset();
        Reply(Dropped.Request,ELDOrderStatus::Cancelled,Failure,true);
    }
    EndActive(ELDOrderStatus::Cancelled,Failure,true);
}

void ULDNavigationComponent::EndActive(ELDOrderStatus Outcome, ELDOrderFailure Failure, bool bHalt)
{
    if (!ActiveOrder) { return; }
    const FLDOrderReceipt Receipt=MakeReceipt(*ActiveOrder,Outcome,Failure);
    const FActiveOrder Ended=MoveTemp(ActiveOrder.GetValue());
    ActiveOrder.Reset();
    ReleaseOrderLinks(Ended,true);
    if (auto* PC=bHalt?Controller():nullptr) { LDPathSteering::HaltLocomotion(Cast<ALDCharacter>(PC->GetPawn())); }
    // A forced taunt chase is not the owner's order: it has no record or receipt, and one that stopped by itself is
    // re-created only after an order query interval.
    if (Ended.bForced)
    {
        if (const auto* Mode=AuthorityMode()) { ForcedRetryAt=Mode->GetAuthorityNowSeconds()+OrderQueryIntervalSeconds; }
    }
    else { Publish(Receipt,true); }
    UE_LOG(LogTemp,Display,TEXT("LD_NAV_END order=%u epoch=%u status=%u failure=%u"),Ended.Request.Identity.Order,
        Ended.Request.Identity.Epoch,uint32(Outcome),uint32(Failure));
}

void ULDNavigationComponent::ReleaseOrderLinks(const FActiveOrder& Order, bool bReleasePin)
{
    auto* Mode=AuthorityMode();
    if (!Mode) { return; }
    if (Order.Request.IsAggressive() && Mode->Combat)
    { Mode->Combat->CancelNavAttack(Order.Body.Victim.EntityId,Order.Request.Identity,KeptSwingTarget); }
    if (!Order.bPinned || !bReleasePin) { return; }
    FString Error;
    auto* PC=Controller();
    if (!Mode->Vision || !PC || !Mode->Vision->ReleaseObservedOrder(*PC,Order.Request.Identity,Error))
    {
        UE_LOG(LogTemp,Warning,TEXT("LD_NAV_PIN_RELEASE_REFUSED order=%u epoch=%u %s"),Order.Request.Identity.Order,
            Order.Request.Identity.Epoch,*Error);
    }
}

bool ULDNavigationComponent::ProcessAuthorityOrders(const FLDGameplayWritePermit& Permit, FString& Error)
{
    auto* Mode=AuthorityMode();
    if (!Mode || Mode->ValidateWritePermit(Permit,ELDCommitPhase::Actions)!=ELDCommitReject::None)
    { Error=TEXT("Order activation requires the current Actions permit."); return false; }
    {
        auto* Owner=Controller();
        auto* OwnBody=Owner?Cast<ALDCharacter>(Owner->GetPawn()):nullptr;
        FLDCombatBodyRef SelfRef, TaunterRef;
        const bool bTaunted=OwnBody && OwnBody->GetController()==Owner && Mode->ReadBodyRef(*OwnBody,SelfRef)
            && Mode->Combat && Mode->Combat->ReadTaunter(SelfRef,TaunterRef);
        const bool bForced=ActiveOrder.IsSet() && ActiveOrder->bForced;
        switch (LDOrderRules::ForcedOrderStep(bTaunted,bForced,bForced && ActiveOrder->ForcedTarget==TaunterRef))
        {
        case LDOrderRules::EForcedOrder::Begin:
        case LDOrderRules::EForcedOrder::Keep:
            return AdvanceForced(*Mode,*OwnBody,SelfRef,TaunterRef,Permit,Error);
        case LDOrderRules::EForcedOrder::End:
            // The taunt is over: control returns without resuming the order it replaced.
            EndActive(ELDOrderStatus::Completed,ELDOrderFailure::None,true);
            break;
        default:
            break;
        }
    }
    if (PendingOrder && Mode->GetAuthorityNowMs()>=PendingOrder->NotBeforeMs
        && (PendingOrder->bStop || Book.ActivationDue(Mode->GetAuthorityNowSeconds()))
        && !ActivatePending(*Mode,Permit,Error)) { return false; }
    if (!ActiveOrder) { return true; }
    auto* PC=Controller();
    auto* Body=PC?Cast<ALDCharacter>(PC->GetPawn()):nullptr;
    FLDCombatBodyRef Current;
    if (!Body || Body->GetController()!=PC || !Mode->ReadBodyRef(*Body,Current) || Current!=ActiveOrder->Body
        || ActiveOrder->Match!=Mode->GetMatchContext())
    {
        EndActive(ELDOrderStatus::Cancelled,ELDOrderFailure::StaleLife,false);
        return true;
    }
    if (!LeaseFresh(*Mode,*ActiveOrder))
    {
        EndActive(ELDOrderStatus::Cancelled,ELDOrderFailure::Expired,true);
        return true;
    }
    if (!AdvanceActive(*Mode,*Body,Permit,Error)) { return false; }
    if (!ActiveOrder) { return true; }
    const FActiveOrder& Order=ActiveOrder.GetValue();
    const FLDOrderReceipt Latest=MakeReceipt(Order,ELDOrderStatus::Progress,ELDOrderFailure::None);
    if (Latest.bHasGoal!=Order.bSentGoal || Latest.bEngaged!=Order.bSentEngaged
        || (Latest.bHasGoal && FVector::DistSquared2D(FVector(Latest.GoalCm),Order.SentGoal)>=FMath::Square(GoalShiftCm)))
    { PublishActive(ELDOrderStatus::Progress); }
    return true;
}

bool ULDNavigationComponent::AdvanceForced(ALDGameMode& Mode, ALDCharacter& Body, const FLDCombatBodyRef& Self,
    const FLDCombatBodyRef& Taunter, const FLDGameplayWritePermit& Permit, FString& Error)
{
    const double Now=Mode.GetAuthorityNowSeconds();
    // Every own order (Stop included) is refused while taunted; the identity watermark already advanced at receipt.
    if (PendingOrder && Mode.GetAuthorityNowMs()>=PendingOrder->NotBeforeMs)
    {
        const FPendingOrder Refused=MoveTemp(PendingOrder.GetValue());
        PendingOrder.Reset();
        Reply(Refused.Request,ELDOrderStatus::Rejected,ELDOrderFailure::Taunted,true);
    }
    if (!ActiveOrder || !ActiveOrder->bForced || ActiveOrder->ForcedTarget!=Taunter || ActiveOrder->Body!=Self)
    {
        if (ActiveOrder)
        {
            TGuardValue<FLDCombatBodyRef> Keep(KeptSwingTarget,Taunter);
            EndActive(ELDOrderStatus::Cancelled,ELDOrderFailure::Taunted,true);
        }
        if (Now<ForcedRetryAt) { return true; }
        if (ForcedSerial==MAX_uint32)
        {
            UE_LOG(LogTemp,Error,TEXT("LD_NAV_TAUNT_SERIAL_EXHAUSTED"));
            return true;
        }
        FActiveOrder Forced;
        Forced.Request.Identity={Mode.GetMatchContext().Generation,Self.Victim.Life,0,++ForcedSerial};
        Forced.Request.Kind=ELDSpatialOrderKind::AttackUnit;
        Forced.Match=Mode.GetMatchContext();
        Forced.Body=Self;
        Forced.bForced=true;
        Forced.ForcedTarget=Taunter;
        Forced.Surface=GroundSurfaceHint();
        Forced.LastHeartbeat=Forced.LastProgress=Now;
        ActiveOrder=MoveTemp(Forced);
        UE_LOG(LogTemp,Display,TEXT("LD_NAV_TAUNT_FORCED life=%u serial=%u"),Self.Victim.Life,ForcedSerial);
    }
    auto& Order=ActiveOrder.GetValue();
    auto* PC=Controller();
    FGuid Handle;
    uint32 HandleEpoch=0;
    FLDAuthorityTargetSample Sample;
    FString Diagnostic;
    // The taunter is chased only through this observer's own current interval: no hidden position is ever read.
    const ELDObservedResolve Resolved=PC && Mode.Vision && Mode.Vision->FindObservedHandle(*PC,Taunter,Handle,HandleEpoch)
        ? Mode.Vision->ResolveObservedUnit(*PC,Handle,HandleEpoch,ELDObservationPurpose::AttackStart,Sample,&Diagnostic)
        : ELDObservedResolve::TargetUnavailable;
    if (Resolved==ELDObservedResolve::AuthorityFault) { Error=TEXT("Taunt chase observation fault: ")+Diagnostic; return false; }
    if (Resolved!=ELDObservedResolve::Resolved || Sample.Target!=Taunter)
    {
        // Not yet in this observer's sampled view (or just lost): hold; Combat ends a taunt that lost observation.
        Order.bEngaged=false;
        Order.Points.Reset();
        return true;
    }
    if (Order.Target!=Handle || Order.TargetEpoch!=HandleEpoch)
    {
        Order.Target=Handle;
        Order.TargetEpoch=HandleEpoch;
        Order.bForceReplan=true;
    }
    return EngageTarget(Mode,Body,Sample,Permit,Error);
}

bool ULDNavigationComponent::ActivatePending(ALDGameMode& Mode, const FLDGameplayWritePermit& Permit, FString& Error)
{
    const FPendingOrder Next=MoveTemp(PendingOrder.GetValue());
    PendingOrder.Reset();
    auto* PC=Controller();
    const FLDOrderCaller Caller{Next.Match,Next.Body,PC};
    ELDOrderFailure Failure=ELDOrderFailure::None;
    if (!ValidateCaller(Caller,Next.Request.Identity,Failure))
    {
        Reply(Next.Request,ELDOrderStatus::Rejected,Failure,true);
        return true;
    }
    if (Next.OwedAggression.IsSet())
    {
        // Same body and life as the validated newer request; Removed or NotProtected both settle the debt.
        const FLDProtectionReceipt Owed=Mode.EndProtectionForAggressiveIntent(*PC,Next.Match,Next.Body,
            Next.OwedAggression.GetValue(),Permit);
        if (Owed.Outcome==ELDProtectionOutcome::Rejected)
        { UE_LOG(LogTemp,Verbose,TEXT("LD_NAV_OWED_PROTECTION_REJECTED reason=%u"),uint32(Owed.Reason)); }
    }
    if (Next.bStop)
    {
        const ELDOrderFailure Reason=LDNavOrdersDetail::FromCancel(Next.Reason);
        EndActive(ELDOrderStatus::Cancelled,Reason,true);
        Reply(Next.Request,ELDOrderStatus::Cancelled,Reason,true);
        UE_LOG(LogTemp,Display,TEXT("LD_NAV_STOP order=%u epoch=%u"),Next.Request.Identity.Order,Next.Request.Identity.Epoch);
        return true;
    }
    Book.ArmActivation(Mode.GetAuthorityNowSeconds(),OrderQueryIntervalSeconds);
    FActiveOrder Admitted;
    Admitted.Request=Next.Request;
    Admitted.Match=Next.Match;
    Admitted.Body=Next.Body;
    Admitted.bHumanLease=IsHumanOwner();
    Admitted.LastHeartbeat=Admitted.LastProgress=Mode.GetAuthorityNowSeconds();
    if (Next.Request.IsAggressive())
    {
        // An admitted aggressive intent ends protection before any target/path check; a later rejection keeps it ended.
        const FLDAggressiveIntent Intent=LDNavOrdersDetail::IntentOf(Next.Request);
        const FLDProtectionReceipt Protection=Mode.EndProtectionForAggressiveIntent(*PC,Next.Match,Next.Body,Intent,Permit);
        if (Protection.Outcome==ELDProtectionOutcome::Rejected)
        {
            Reply(Next.Request,ELDOrderStatus::Rejected,LDNavOrdersDetail::FromCommit(Protection.Reason),true);
            return true;
        }
        Admitted.bProtectionRemoved=Protection.bProtectionRemoved;
        Admitted.LifeRevision=Protection.LifeRevision;
    }
    if (!AdmitOrder(Mode,*CastChecked<ALDCharacter>(PC->GetPawn()),Permit,Admitted,Failure,Error))
    {
        if (!Error.IsEmpty()) { return false; }
        // The previous accepted order keeps running; the rejection still reports the protection change.
        Reply(Next.Request,ELDOrderStatus::Rejected,Failure,true,Admitted.bProtectionRemoved,Admitted.LifeRevision);
        return true;
    }
    if (ActiveOrder)
    {
        const FLDOrderReceipt Superseded=MakeReceipt(*ActiveOrder,ELDOrderStatus::Cancelled,ELDOrderFailure::Superseded);
        const FActiveOrder Previous=MoveTemp(ActiveOrder.GetValue());
        ActiveOrder.Reset();
        // A newly bound pin already replaced the previous order's pin inside Vision.
        ReleaseOrderLinks(Previous,!Admitted.bPinned);
        Publish(Superseded,true);
    }
    ActiveOrder=MoveTemp(Admitted);
    PublishActive(ELDOrderStatus::Activated);
    UE_LOG(LogTemp,Display,TEXT("LD_NAV_ACCEPT match=%u life=%u order=%u epoch=%u kind=%u goal=%s"),Next.Request.Identity.Match,
        Next.Request.Identity.Life,Next.Request.Identity.Order,Next.Request.Identity.Epoch,uint32(Next.Request.Kind),
        *ActiveOrder->Goal.ToString());
    return true;
}

bool ULDNavigationComponent::AdmitOrder(ALDGameMode& Mode, ALDCharacter& Body, const FLDGameplayWritePermit& Permit,
    FActiveOrder& Order, ELDOrderFailure& Failure, FString& Error)
{
    const FLDSpatialOrderRequest& Request=Order.Request;
    ELDWorldQueryFailure WorldFailure=ELDWorldQueryFailure::None;
    FString Detail;
    if (Request.Kind==ELDSpatialOrderKind::MoveGround || Request.Kind==ELDSpatialOrderKind::AttackMove)
    {
        FVector Projected;
        if (!Plan(Request.GroundCm,Request.SurfaceHint,LDPathSteering::EGoal::Ground,Order.Points,Projected,WorldFailure,Detail))
        {
            Failure=LDNavOrdersDetail::FromWorld(WorldFailure);
            UE_LOG(LogTemp,Display,TEXT("LD_NAV_REJECT order=%u %s"),Request.Identity.Order,*Detail);
            return false;
        }
        Order.Surface=Request.SurfaceHint;
        Order.Goal=Order.Destination=Projected;
        Order.Acceptance=float(GoalAcceptanceCm);
    }
    else if (Request.Kind==ELDSpatialOrderKind::ApproachShop)
    {
        const auto* Loaded=DemoContent();
        const auto* Registry=GetWorld()->GetSubsystem<ULDMapRegistry>();
        const FLDMapBinding* Shop=Loaded && Registry && Request.ShopId==FName(*Loaded->Get().World.ShopId)
            ? Registry->Resolve(Request.ShopId,Registry->Generation) : nullptr;
        if (!Shop) { Failure=ELDOrderFailure::ShopUnavailable; return false; }
        FLDLegalGround From, Approach;
        FLDCompletePath Path;
        FCollisionShape Capsule;
        if (!QueryPlanStart(From,Capsule,WorldFailure,Detail)
            || !Registry->FindPublicApproach(Registry->Revision(),From,Request.ShopId,Capsule,Approach,Path,WorldFailure,Detail))
        {
            Failure=ELDOrderFailure::NoCompletePath;
            UE_LOG(LogTemp,Display,TEXT("LD_NAV_REJECT order=%u %s"),Request.Identity.Order,*Detail);
            return false;
        }
        Order.Points=MoveTemp(Path.Points);
        Order.Surface=Shop->Surface;
        Order.Goal=Approach.SurfaceCm;
        Order.ShopCentre=Shop->Position;
        Order.Acceptance=float(LDPathSteering::WaypointToleranceCm);
    }
    else if (Request.Kind==ELDSpatialOrderKind::AttackUnit)
    {
        auto* PC=Controller();
        FLDAuthorityTargetSample Sample;
        FString Diagnostic;
        const ELDObservedResolve Resolved=Mode.Vision
            ? Mode.Vision->ResolveObservedUnit(*PC,Request.Observed,Request.ObservationEpoch,
                ELDObservationPurpose::NewAttackOrder,Sample,&Diagnostic)
            : ELDObservedResolve::ObserverUnavailable;
        if (Resolved==ELDObservedResolve::AuthorityFault) { Error=TEXT("Attack order observation fault: ")+Diagnostic; return false; }
        // Never falls back to ground or a nearby unit: an unresolved handle is only TargetUnavailable.
        if (Resolved!=ELDObservedResolve::Resolved || Sample.Target.Victim.EntityId==Order.Body.Victim.EntityId)
        { Failure=ELDOrderFailure::TargetUnavailable; return false; }
        FLDCombatBodyRead Read;
        if (!Mode.Combat || !Mode.Combat->ReadCombatBody(Order.Body,Read))
        { Error=TEXT("An admitted attacker has no Combat body read."); return false; }
        Order.Surface=GroundSurfaceHint();
        Order.Target=Request.Observed;
        Order.TargetEpoch=Request.ObservationEpoch;
        Order.TargetFoot=Order.Goal=Sample.FootCm;
        Order.RangeCm=Read.AttackRangeCm;
        Order.Acceptance=float(Read.AttackRangeCm);
        Order.bEngaged=FVector::DistSquared2D(ALDCombatDirector::FootOf(Body),Sample.FootCm)<=FMath::Square(Read.AttackRangeCm);
        if (!Order.bEngaged)
        {
            FVector Projected;
            // The target is a body: against a wall, another body or cutting a corner its foot can sit in Recast's
            // margin, so the chase plans to its nearest navmesh point within one capsule radius.
            if (!Plan(Sample.FootCm,Order.Surface,LDPathSteering::EGoal::Body,Order.Points,Projected,WorldFailure,Detail))
            {
                Failure=ELDOrderFailure::NoCompletePath;
                UE_LOG(LogTemp,Display,TEXT("LD_NAV_REJECT order=%u %s"),Request.Identity.Order,*Detail);
                return false;
            }
            Order.Goal=Projected;
            Order.PlannedTargetFoot=Sample.FootCm;
            Order.NextChasePlan=Mode.GetAuthorityNowSeconds()+OrderQueryIntervalSeconds;
        }
        FString BindError;
        if (!Mode.Vision->BindObservedOrder(*PC,Request.Identity,Request.Observed,Request.ObservationEpoch,Permit,BindError))
        {
            UE_LOG(LogTemp,Display,TEXT("LD_NAV_REJECT order=%u %s"),Request.Identity.Order,*BindError);
            Failure=ELDOrderFailure::TargetUnavailable;
            return false;
        }
        Order.bPinned=true;
    }
    else
    {
        Failure=ELDOrderFailure::InvalidRequest;
        return false;
    }
    Order.PointIndex=Order.Points.Num()>1?1:0;
    Order.BestProgress=LDPathSteering::AlongPath(Order.Points,Body.GetNavAgentLocation());
    return true;
}

bool ULDNavigationComponent::AdvanceActive(ALDGameMode& Mode, ALDCharacter& Body, const FLDGameplayWritePermit& Permit,
    FString& Error)
{
    auto* PC=Controller();
    const ELDSpatialOrderKind Kind=ActiveOrder->Request.Kind;
    if (Kind==ELDSpatialOrderKind::MoveGround) { return true; }
    if (Kind==ELDSpatialOrderKind::ApproachShop)
    {
        const auto* Registry=GetWorld()->GetSubsystem<ULDMapRegistry>();
        bool bWithin=false;
        ELDWorldQueryFailure Failure=ELDWorldQueryFailure::MissingWorld;
        FString Detail;
        if (Registry && Registry->QueryShopReach(Registry->Revision(),ALDCombatDirector::FootOf(Body),bWithin,Failure,Detail))
        {
            if (bWithin) { EndActive(ELDOrderStatus::Completed,ELDOrderFailure::None,true); }
            return true;
        }
        if (Failure==ELDWorldQueryFailure::NotGrounded || Failure==ELDWorldQueryFailure::Blocked) { return true; }
        UE_LOG(LogTemp,Warning,TEXT("LD_NAV_SHOP_REACH_UNAVAILABLE %s"),*Detail);
        EndActive(ELDOrderStatus::Cancelled,ELDOrderFailure::ShopUnavailable,true);
        return true;
    }
    if (!Mode.Vision || !Mode.Combat) { Error=TEXT("Attack orders require the current Vision and Combat owners."); return false; }
    if (ActiveOrder->bPinned)
    {
        FLDAuthorityTargetSample Sample;
        FString Diagnostic;
        const ELDObservedResolve Resolved=Mode.Vision->ResolveObservedUnit(*PC,ActiveOrder->Target,ActiveOrder->TargetEpoch,
            ELDObservationPurpose::AttackStart,Sample,&Diagnostic);
        if (Resolved==ELDObservedResolve::AuthorityFault) { Error=TEXT("Attack order observation fault: ")+Diagnostic; return false; }
        if (Resolved==ELDObservedResolve::Resolved) { return EngageTarget(Mode,Body,Sample,Permit,Error); }
        FLDObservedTerminationFact Termination;
        const bool bDied=Mode.Vision->FindObservedTermination(*PC,ActiveOrder->Target,ActiveOrder->TargetEpoch,Termination);
        if (Kind==ELDSpatialOrderKind::AttackUnit || !bDied)
        {
            // Lost sight cancels the lock and stops this pursuit; it never walks to a last seen position or resumes.
            EndActive(bDied?ELDOrderStatus::Completed:ELDOrderStatus::Cancelled,
                bDied?ELDOrderFailure::TargetDied:ELDOrderFailure::TargetUnavailable,true);
            return true;
        }
        // AttackMove saw its target die: drop it, then search again or continue to the fixed destination.
        auto& Order=ActiveOrder.GetValue();
        ReleaseOrderLinks(Order,true);
        Order.bPinned=Order.bEngaged=Order.bCloseIn=Order.bForceReplan=Order.bProgressReplan=false;
        Order.Target.Invalidate();
        Order.TargetEpoch=0;
        TArray<FVector> Path;
        FVector Projected;
        ELDWorldQueryFailure Failure=ELDWorldQueryFailure::None;
        FString Detail;
        if (!Plan(Order.Destination,Order.Surface,LDPathSteering::EGoal::Ground,Path,Projected,Failure,Detail))
        {
            UE_LOG(LogTemp,Display,TEXT("LD_NAV_RESUME_REJECTED order=%u %s"),Order.Request.Identity.Order,*Detail);
            EndActive(ELDOrderStatus::Cancelled,LDNavOrdersDetail::FromWorld(Failure),true);
            return true;
        }
        Order.Points=MoveTemp(Path);
        Order.PointIndex=Order.Points.Num()>1?1:0;
        Order.Goal=Projected;
        Order.Acceptance=float(GoalAcceptanceCm);
        Order.BestProgress=LDPathSteering::AlongPath(Order.Points,Body.GetNavAgentLocation());
        Order.LastProgress=Mode.GetAuthorityNowSeconds();
        Order.bReplanned=false;
    }
    if (Kind!=ELDSpatialOrderKind::AttackMove)
    {
        EndActive(ELDOrderStatus::Cancelled,ELDOrderFailure::TargetUnavailable,true);
        return true;
    }
    FLDAuthorityTargetSample Sample;
    bool bFound=false;
    if (!AcquireTarget(Mode,Body,Permit,Sample,bFound,Error)) { return false; }
    if (bFound) { return EngageTarget(Mode,Body,Sample,Permit,Error); }
    // Arriving with no current target completes the order; an attack-move is not a permanent attack toggle.
    if (LDPathSteering::AtGoal(Body,ActiveOrder->Goal,ActiveOrder->Acceptance))
    { EndActive(ELDOrderStatus::Completed,ELDOrderFailure::None,true); }
    return true;
}

// Nearest currently visible candidate within min(vision, 2 x attack range); ties by the smaller observer handle.
bool ULDNavigationComponent::AcquireTarget(ALDGameMode& Mode, ALDCharacter& Body, const FLDGameplayWritePermit& Permit,
    FLDAuthorityTargetSample& Out, bool& bFound, FString& Error)
{
    bFound=false;
    auto& Order=ActiveOrder.GetValue();
    auto* PC=Controller();
    const auto* Loaded=DemoContent();
    FLDCombatBodyRead Read;
    if (!Loaded || !Mode.Combat->ReadCombatBody(Order.Body,Read))
    { Error=TEXT("Attack-move acquisition requires the approved content and the attacker's Combat body."); return false; }
    const auto& Vision=Loaded->Get().Parameters.VisionRadiusCm;
    const double VisionCm=Vision.Denominator>0?double(Vision.Numerator)/double(Vision.Denominator):0.;
    const double Radius=FMath::Min(VisionCm,2.*Read.AttackRangeCm);
    if (!(Radius>0) || !FMath::IsFinite(Radius))
    { Error=TEXT("Attack-move acquisition radius is not a positive approved distance."); return false; }
    FLDWorldView View;
    FString ViewError;
    if (!Mode.Vision->EnumerateVisibleUnits(*PC,ELDObservationPurpose::AttackMoveCandidate,View,ViewError))
    {
        // No authorized frame yet (for example right after a new life): no candidate this step.
        UE_LOG(LogTemp,Verbose,TEXT("LD_NAV_ACQUIRE_NO_FRAME %s"),*ViewError);
        return true;
    }
    const FVector Foot=ALDCombatDirector::FootOf(Body);
    const int32 BestIndex=LDOrderRules::NearestCandidate(View.Units,Foot,Radius,Order.Body.Victim.EntityId);
    if (BestIndex==INDEX_NONE) { return true; }
    const FLDObservedUnitView* Best=&View.Units[BestIndex];
    FString Diagnostic;
    const ELDObservedResolve Resolved=Mode.Vision->ResolveObservedUnit(*PC,Best->Handle,Best->ObservationEpoch,
        ELDObservationPurpose::AttackMoveCandidate,Out,&Diagnostic);
    if (Resolved==ELDObservedResolve::AuthorityFault) { Error=TEXT("Attack-move candidate observation fault: ")+Diagnostic; return false; }
    FString BindError;
    if (Resolved!=ELDObservedResolve::Resolved || Out.Target.Victim.EntityId==Order.Body.Victim.EntityId
        || !Mode.Vision->BindObservedOrder(*PC,Order.Request.Identity,Best->Handle,Best->ObservationEpoch,Permit,BindError))
    {
        UE_LOG(LogTemp,Verbose,TEXT("LD_NAV_ACQUIRE_SKIPPED %s %s"),*Diagnostic,*BindError);
        return true;
    }
    Order.bPinned=true;
    Order.Target=Best->Handle;
    Order.TargetEpoch=Best->ObservationEpoch;
    Order.bEngaged=Order.bCloseIn=Order.bReplanned=Order.bProgressReplan=false;
    Order.bForceReplan=true;
    Order.LastProgress=Mode.GetAuthorityNowSeconds();
    bFound=true;
    return true;
}

bool ULDNavigationComponent::EngageTarget(ALDGameMode& Mode, ALDCharacter& Body, const FLDAuthorityTargetSample& Sample,
    const FLDGameplayWritePermit& Permit, FString& Error)
{
    auto& Order=ActiveOrder.GetValue();
    FLDCombatBodyRead Read;
    if (!Mode.Combat->ReadCombatBody(Order.Body,Read)) { Error=TEXT("An attack order lost its attacker's Combat body read."); return false; }
    const double Now=Mode.GetAuthorityNowSeconds();
    const FVector Foot=ALDCombatDirector::FootOf(Body);
    Order.TargetFoot=Sample.FootCm;
    Order.RangeCm=Read.AttackRangeCm;
    const bool bInRange=FVector::DistSquared2D(Foot,Sample.FootCm)<=FMath::Square(Read.AttackRangeCm);
    if (!bInRange) { Order.bCloseIn=false; }
    bool bStarted=false;
    if (bInRange && Read.bBasicAttackReady)
    {
        FLDCombatCaller Caller;
        FString CallerError;
        if (!Mode.Combat->ResolveControllerCaller(*Controller(),Caller,CallerError) || Caller.EntityId!=Order.Body.Victim.EntityId)
        {
            UE_LOG(LogTemp,Error,TEXT("LD_NAV_ATTACK_CALLER_UNRESOLVED %s"),*CallerError);
            EndActive(ELDOrderStatus::Cancelled,ELDOrderFailure::NotOwner,true);
            return true;
        }
        if (Order.AttackCycle==MAX_uint64)
        {
            EndActive(ELDOrderStatus::Cancelled,ELDOrderFailure::SequenceExhausted,true);
            return true;
        }
        // Each repeat is a new Nav request of the same order; Combat alone allocates the action token.
        FLDCombatRequestKey AttackKey;
        AttackKey.Origin=ELDCombatRequestOrigin::Nav;
        AttackKey.Generation=Order.Match.Generation;
        AttackKey.Entity=Caller.EntityId;
        AttackKey.Life=Order.Body.Victim.Life;
        AttackKey.Move=Order.Request.Identity;
        AttackKey.AttackCycle=++Order.AttackCycle;
        const FLDCombatActionReceipt Attack=Mode.Combat->StartNavAttack(Caller,AttackKey,Sample.Target,Order.Target,Order.TargetEpoch,Permit);
        if (Attack.Phase!=ELDCombatPhase::Rejected) { bStarted=true; Order.bCloseIn=false; }
        else if (Attack.Reject==ELDCombatReject::OutOfRange || Attack.Reject==ELDCombatReject::Blocked)
        { Order.bCloseIn=Order.bForceReplan=true; }
    }
    Order.bEngaged=bStarted || Read.bActionInProgress || (bInRange && !Order.bCloseIn);
    if (Order.bEngaged) { return true; }
    const bool bTargetMoved=FVector::DistSquared2D(Sample.FootCm,Order.PlannedTargetFoot)>=FMath::Square(GoalShiftCm);
    if (!Order.bForceReplan && !Order.Points.IsEmpty() && !(bTargetMoved && Now>=Order.NextChasePlan)) { return true; }
    const bool bProgressReplan=Order.bProgressReplan;
    Order.bForceReplan=Order.bProgressReplan=false;
    Order.NextChasePlan=Now+OrderQueryIntervalSeconds;
    TArray<FVector> Path;
    FVector Projected;
    ELDWorldQueryFailure Failure=ELDWorldQueryFailure::None;
    FString Detail;
    if (Plan(Sample.FootCm,Order.Surface,LDPathSteering::EGoal::Body,Path,Projected,Failure,Detail))
    {
        Order.Points=MoveTemp(Path);
        Order.PointIndex=Order.Points.Num()>1?1:0;
        Order.Goal=Projected;
        Order.PlannedTargetFoot=Sample.FootCm;
        Order.BestProgress=LDPathSteering::AlongPath(Order.Points,Body.GetNavAgentLocation());
        return true;
    }
    if (bProgressReplan || Order.Points.IsEmpty())
    {
        UE_LOG(LogTemp,Display,TEXT("LD_NAV_CHASE_UNREACHABLE order=%u %s"),Order.Request.Identity.Order,*Detail);
        EndActive(ELDOrderStatus::Cancelled,ELDOrderFailure::NoCompletePath,true);
        return true;
    }
    UE_LOG(LogTemp,Verbose,TEXT("LD_NAV_CHASE_REPLAN_DEFERRED order=%u %s"),Order.Request.Identity.Order,*Detail);
    return true;
}

// Voluntary steering only while the order is moving; engaged or arrived orders hold position.
FVector ULDNavigationComponent::AuthoritySteering(const ALDCharacter& Body)
{
    auto& Order=ActiveOrder.GetValue();
    if (Order.bEngaged || Order.Points.IsEmpty()) { return FVector::ZeroVector; }
    const FVector Foot=Body.GetNavAgentLocation();
    if (LDNavOrdersDetail::IsChase(Order.Request,Order.bPinned))
    {
        if (!Order.bCloseIn && FVector::DistSquared2D(Foot,Order.TargetFoot)<=FMath::Square(Order.RangeCm)) { return FVector::ZeroVector; }
    }
    else if (Order.Request.Kind==ELDSpatialOrderKind::AttackMove && LDPathSteering::AtGoal(Body,Order.Goal,Order.Acceptance))
    { return FVector::ZeroVector; }
    else if (Order.Request.Kind==ELDSpatialOrderKind::ApproachShop && Order.PointIndex>=Order.Points.Num()-1
        && FVector::DistSquared2D(Foot,Order.Points.Last())<=FMath::Square(LDPathSteering::WaypointToleranceCm))
    {
        // The approach endpoint lies on the interaction radius; ease inward until the shop reports reach.
        return (Order.ShopCentre-Foot).GetSafeNormal2D()*.25;
    }
    return LDPathSteering::Steering(Order.Points,Order.PointIndex,Body);
}

void ULDNavigationComponent::TrackProgress(const ALDCharacter& Body, double Now, bool bMoving)
{
    auto& Order=ActiveOrder.GetValue();
    const FVector Foot=Body.GetNavAgentLocation();
    const bool bChase=LDNavOrdersDetail::IsChase(Order.Request,Order.bPinned);
    if (!bMoving || (bChase && FVector::DistSquared2D(Foot,Order.TargetFoot)<=FMath::Square(Order.RangeCm)))
    {
        Order.LastProgress=Now;
        return;
    }
    const double Progress=LDPathSteering::AlongPath(Order.Points,Foot);
    if (Progress>=Order.BestProgress+10) { Order.BestProgress=Progress; Order.LastProgress=Now; }
    if (Now-Order.LastProgress<OrderNoProgressSeconds) { return; }
    if (Order.bReplanned)
    {
        EndActive(ELDOrderStatus::Cancelled,ELDOrderFailure::PathBlocked,true);
        return;
    }
    Order.bReplanned=true;
    Order.LastProgress=Now;
    ++Replans;
    if (bChase)
    {
        // Pursuit replans toward the target's current authorized position in the next Actions phase.
        Order.bForceReplan=Order.bProgressReplan=true;
        return;
    }
    TArray<FVector> Path;
    FVector Projected;
    ELDWorldQueryFailure Failure=ELDWorldQueryFailure::None;
    FString Detail;
    if (!Plan(Order.Goal,Order.Surface,LDPathSteering::EGoal::Ground,Path,Projected,Failure,Detail))
    {
        UE_LOG(LogTemp,Display,TEXT("LD_NAV_REPLAN_REJECTED order=%u %s"),Order.Request.Identity.Order,*Detail);
        EndActive(ELDOrderStatus::Cancelled,LDNavOrdersDetail::FromWorld(Failure),true);
        return;
    }
    Order.Points=MoveTemp(Path);
    Order.PointIndex=Order.Points.Num()>1?1:0;
    Order.BestProgress=0;
}

bool ULDNavigationComponent::AdvanceAuthorityMotion(const FLDAuthorityStep& Step, const FLDCombatBodyRef& Ref,
    const FLDGameplayWritePermit& Permit, FString& Error)
{
    auto* Mode=AuthorityMode();
    ALDCharacter* Body=nullptr;
    if (!Mode || Mode->ValidateWritePermit(Permit,ELDCommitPhase::Motion)!=ELDCommitReject::None
        || Mode->ResolveBody(Step.Match,Ref,Body)!=ELDCommitReject::None || Body->GetController()!=Controller())
    { Error=TEXT("Ground motion requires the registered body's Motion permit."); return false; }
    if (ActiveOrder && (ActiveOrder->Body!=Ref || ActiveOrder->Match!=Step.Match))
    { EndActive(ELDOrderStatus::Cancelled,ELDOrderFailure::StaleLife,true); }
    else if (ActiveOrder && !LeaseFresh(*Mode,*ActiveOrder))
    { EndActive(ELDOrderStatus::Cancelled,ELDOrderFailure::Expired,true); }
    if (ActiveOrder && ActiveOrder->Request.Kind==ELDSpatialOrderKind::MoveGround
        && LDPathSteering::AtGoal(*Body,ActiveOrder->Goal,ActiveOrder->Acceptance))
    { EndActive(ELDOrderStatus::Completed,ELDOrderFailure::None,true); }
    // Rooted or stunned bodies get no voluntary input; committed root motion (dashes, jumps) still integrates natively.
    const bool bRooted=Mode->Combat && Mode->Combat->IsMoveBlocked(*Body);
    const FVector Input=ActiveOrder && !bRooted?AuthoritySteering(*Body):FVector::ZeroVector;
    auto* Movement=CastChecked<ULDCharacterMovementComponent>(Body->GetCharacterMovement());
    if (!Movement->AdvanceAuthorityMovement(Step,Ref,Input,Permit,Error)) { return false; }
    if (!ActiveOrder) { return true; }
    if (ActiveOrder->Request.Kind==ELDSpatialOrderKind::MoveGround && LDPathSteering::AtGoal(*Body,ActiveOrder->Goal,ActiveOrder->Acceptance))
    {
        EndActive(ELDOrderStatus::Completed,ELDOrderFailure::None,true);
        return true;
    }
    TrackProgress(*Body,Step.EndServerSeconds,!Input.IsNearlyZero());
    return true;
}
