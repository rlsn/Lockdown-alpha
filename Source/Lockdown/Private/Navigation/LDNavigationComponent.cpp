#include "Navigation/LDNavigationComponent.h"
#include "Core/LDLog.h"
#include "UI/LDCursor.h"
#include "UI/LDTerms.h"
#include "Navigation/LDOrderRules.h"
#include "Navigation/LDPathSteering.h"
#include "Navigation/LDPointerFeedback.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDDemoEntry.h"
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Development/LDDemoContentCatalog.h"
#include "Components/CapsuleComponent.h"
#include "Camera/CameraActor.h"
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"
#include "Region/LDMapRegistry.h"
#include "Region/LDMapGeometry.h"
#include "Misc/App.h"
#include "Misc/ScopeExit.h"
#include "Framework/Application/SlateApplication.h"
#include "UI/LDUIRoot.h"
#include "UI/LDControlSettings.h"
#include "Character/LDCameraSettings.h"
#include "UI/LDHUD.h"
#include "UI/LDMinimap.h"
#include "Art/LDArtSubsystem.h"
#include "Vision/LDWorldViewRelay.h"

namespace LDNavigationDetail
{
    const TCHAR* FailureText(ELDOrderFailure Failure)
    {
        switch (Failure)
        {
        case ELDOrderFailure::None: return TEXT("");
        case ELDOrderFailure::InvalidRequest: return TEXT("订单无效");
        case ELDOrderFailure::NotOwner: return TEXT("不是你当前的角色");
        case ELDOrderFailure::StaleMatch:
        case ELDOrderFailure::StaleLife:
        case ELDOrderFailure::StaleOrder: return TEXT("订单已失效");
        case ELDOrderFailure::RequestConflict: return TEXT("订单编号冲突");
        case ELDOrderFailure::SequenceExhausted: return TEXT("订单编号已用尽");
        case ELDOrderFailure::InvalidGround:
        case ELDOrderFailure::NoCompletePath: return TEXT("无法到达该位置");
        case ELDOrderFailure::PathBlocked: return TEXT("路线受阻");
        case ELDOrderFailure::TargetUnavailable: return TEXT("目标不可见");
        case ELDOrderFailure::TargetDied: { static const FString Text=FString(TEXT("目标已经"))+LDTerms::Dead; return *Text; }
        case ELDOrderFailure::ShopUnavailable: { static const FString Text=FString(LDTerms::Shop)+TEXT("不可用"); return *Text; }
        case ELDOrderFailure::Dead: { static const FString Text=FString(TEXT("你已经"))+LDTerms::Dead; return *Text; }
        case ELDOrderFailure::MatchEnded: return TEXT("本局已结束");
        case ELDOrderFailure::Superseded: return TEXT("已被新订单替换");
        case ELDOrderFailure::Stopped: return TEXT("已停止");
        case ELDOrderFailure::Expired: return TEXT("订单已过期");
        case ELDOrderFailure::Taunted: { static const FString Text=FString(LDTerms::Taunt)+TEXT("：只能")+LDTerms::BasicAttack+TEXT("嘲讽你的人"); return *Text; }
        case ELDOrderFailure::KnockedBack: return TEXT("被击退，指令中断");
        case ELDOrderFailure::QueueFull: return TEXT("指令队列已满");
        case ELDOrderFailure::CastRefused: return TEXT("技能无法施放");
        case ELDOrderFailure::NotStarted: return LDTerms::NotStartedYet;
        default: return TEXT("订单失败");
        }
    }

    const TCHAR* QueuedText() { return TEXT("已加入指令队列"); }

    const TCHAR* PendingText(ELDSpatialOrderKind Kind)
    {
        switch (Kind)
        {
        case ELDSpatialOrderKind::AttackUnit: return TEXT("攻击请求已发送");
        case ELDSpatialOrderKind::AttackMove: return TEXT("攻击移动已发送");
        case ELDSpatialOrderKind::CastUnit:
        case ELDSpatialOrderKind::CastPoint: return TEXT("施法请求已发送");
        case ELDSpatialOrderKind::ApproachShop: { static const FString Text=FString(TEXT("正在前往"))+LDTerms::Shop; return *Text; }
        default: return TEXT("移动请求已发送");
        }
    }

    const TCHAR* RunningText(const FLDOrderReceipt& Receipt)
    {
        if (Receipt.Kind==ELDSpatialOrderKind::CastUnit) { return Receipt.bEngaged ? TEXT("准备施法") : TEXT("接近目标施法"); }
        if (Receipt.Kind==ELDSpatialOrderKind::CastPoint) { return Receipt.bEngaged ? TEXT("准备施法") : TEXT("接近施法位置"); }
        if (Receipt.bEngaged) { static const FString Text=FString(TEXT("正在"))+LDTerms::BasicAttack; return *Text; }
        switch (Receipt.Kind)
        {
        case ELDSpatialOrderKind::AttackUnit: return TEXT("追击目标");
        case ELDSpatialOrderKind::AttackMove: return TEXT("攻击移动中");
        case ELDSpatialOrderKind::ApproachShop: { static const FString Text=FString(TEXT("前往"))+LDTerms::Shop; return *Text; }
        default: return TEXT("移动中");
        }
    }

    FString ReceiptText(const FLDOrderReceipt& Receipt, bool bStop)
    {
        FString Text;
        switch (Receipt.Status)
        {
        case ELDOrderStatus::Queued: Text=bStop?TEXT("停止请求已发送"):PendingText(Receipt.Kind); break;
        case ELDOrderStatus::Activated:
        case ELDOrderStatus::Progress: Text=RunningText(Receipt); break;
        case ELDOrderStatus::Completed:
            Text=Receipt.Failure==ELDOrderFailure::None?TEXT("已到达"):FailureText(Receipt.Failure); break;
        default: Text=FailureText(Receipt.Failure); break;
        }
        if (Receipt.bProtectionRemoved && (Receipt.Status==ELDOrderStatus::Activated || Receipt.Status==ELDOrderStatus::Rejected))
        { Text+=TEXT("（")+LDTerms::ProtectionRemoved()+TEXT("）"); }
        return Text;
    }

    // The one notice a receipt may post to the HUD's toast line: a real failure and a removed respawn protection.
    // Progress, arrival and the player's own replacing order or S stay in Status only (UI review P1).
    FString NoticeText(const FLDOrderReceipt& Receipt)
    {
        const bool bEnded=Receipt.Status==ELDOrderStatus::Rejected || Receipt.Status==ELDOrderStatus::Cancelled
            || Receipt.Status==ELDOrderStatus::Completed;
        // A refused cast is explained by its own Combat receipt beside the skill button, not a second toast.
        const bool bFailed=bEnded && Receipt.Failure!=ELDOrderFailure::None && Receipt.Failure!=ELDOrderFailure::Superseded
            && Receipt.Failure!=ELDOrderFailure::Stopped && Receipt.Failure!=ELDOrderFailure::CastRefused;
        FString Text=bFailed?FString(FailureText(Receipt.Failure)):FString();
        if (Receipt.bProtectionRemoved && (Receipt.Status==ELDOrderStatus::Activated || Receipt.Status==ELDOrderStatus::Rejected))
        { Text+=Text.IsEmpty()?LDTerms::ProtectionRemoved():TEXT("（")+LDTerms::ProtectionRemoved()+TEXT("）"); }
        return Text;
    }

    // Owner prediction of an attack order plans a body goal: a chase receipt carries the target's foot, and an
    // attack-move's ground goal is already the server's strict on-mesh point, which the body snap leaves unchanged.
    LDPathSteering::EGoal PredictedGoal(ELDSpatialOrderKind Kind)
    {
        return Kind==ELDSpatialOrderKind::AttackUnit || Kind==ELDSpatialOrderKind::AttackMove || Kind==ELDSpatialOrderKind::CastUnit
            || Kind==ELDSpatialOrderKind::CastPoint ? LDPathSteering::EGoal::Body : LDPathSteering::EGoal::Ground;
    }
}

ULDNavigationComponent::ULDNavigationComponent()
{
    SetIsReplicatedByDefault(true);
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.TickGroup=TG_PrePhysics;
}

ALDPlayerController* ULDNavigationComponent::Controller() const { return Cast<ALDPlayerController>(GetOwner()); }

ALDGameMode* ULDNavigationComponent::AuthorityMode() const
{ return GetWorld()?GetWorld()->GetAuthGameMode<ALDGameMode>():nullptr; }

bool ULDNavigationComponent::IsLocalOwner() const
{
    const auto* PC=Controller();
    return PC && PC->IsLocalController() && PC->GetLocalPlayer();
}

// Registered opponent AI controllers have neither; their lease is their current registration.
bool ULDNavigationComponent::IsHumanOwner() const
{
    const auto* PC=Controller();
    return PC && (PC->GetLocalPlayer() || PC->GetNetConnection());
}

bool ULDNavigationComponent::Enabled() const
{
    const auto* PC=Controller();
    return PC && GetWorld() && UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName())==LDDemoEntry::WorldMap;
}

FName ULDNavigationComponent::GroundSurfaceHint() const
{
    const auto* Registry=GetWorld()?GetWorld()->GetSubsystem<ULDMapRegistry>():nullptr;
    const auto* Layout=Registry?Registry->GetGeometry():nullptr;
    return Layout && Layout->Surfaces.Num()==1?FName(*Layout->Surfaces[0].Id):NAME_None;
}

const FLDDemoContentSnapshot* ULDNavigationComponent::DemoContent()
{
    if (!Content)
    {
        FString Error;
        if (!LDDemoEntry::LoadRuntimeContent(Content,Error))
        {
            Content.Reset();
            UE_LOG(LogLDNav,Error,TEXT("LD_NAV_CONTENT_UNAVAILABLE %s"),*Error);
            return nullptr;
        }
    }
    return Content.Get();
}

bool ULDNavigationComponent::QueryBodyGround(FLDLegalGround& Out, FCollisionShape& Capsule,
    ELDWorldQueryFailure& Failure, FString& Error) const
{
    Failure=ELDWorldQueryFailure::MissingWorld;
    const auto* PC=Controller();
    const auto* Body=PC?Cast<ALDCharacter>(PC->GetPawn()):nullptr;
    const auto* Registry=GetWorld()->GetSubsystem<ULDMapRegistry>();
    const FName Surface=GroundSurfaceHint();
    if (!Body || !Registry || !Registry->Enabled() || Surface.IsNone())
    {
        Error=TEXT("No current body or registered Demo world.");
        return false;
    }
    const auto* Movement=Body->GetCharacterMovement();
    const auto* Shape=Body->GetCapsuleComponent();
    Capsule=FCollisionShape::MakeCapsule(Shape->GetScaledCapsuleRadius(),Shape->GetScaledCapsuleHalfHeight());
    if (!Movement->IsMovingOnGround() || !Movement->CurrentFloor.IsWalkableFloor())
    {
        Failure=ELDWorldQueryFailure::NotGrounded;
        Error=TEXT("NotGrounded: the body's current native support is not registered.");
        return false;
    }
    // The body's support comes from its native floor mapped to the registry, never from an order hint.
    if (!Registry->QueryGround(Registry->Revision(),Body->GetNavAgentLocation(),Capsule,Surface,Out,Failure,Error))
    {
        Error+=FString::Printf(TEXT(" Native floor: distance=%.6f lineDistance=%.6f line=%d impact=%s location=%s component=%s."),
            Movement->CurrentFloor.FloorDist,Movement->CurrentFloor.LineDist,Movement->CurrentFloor.bLineTrace,
            *Movement->CurrentFloor.HitResult.ImpactPoint.ToString(),*Movement->CurrentFloor.HitResult.Location.ToString(),
            *GetNameSafe(Movement->CurrentFloor.HitResult.GetComponent()));
        return false;
    }
    // Ground, ramp and plateau pieces meet at seams where the foot's trace and the capsule's floor can name neighbours.
    if (!Registry->IsTerrainFloor(Movement->CurrentFloor.HitResult.GetComponent()))
    {
        Failure=ELDWorldQueryFailure::WrongSurface;
        Error=TEXT("WrongSurface: the body's native floor is not the registered surface.");
        return false;
    }
    return true;
}

bool ULDNavigationComponent::QueryPlanStart(FLDLegalGround& Out, FCollisionShape& Capsule,
    ELDWorldQueryFailure& Failure, FString& Error) const
{
    FLDLegalGround Body;
    return QueryBodyGround(Body,Capsule,Failure,Error)
        && LDPathSteering::PlanStart(*GetWorld()->GetSubsystem<ULDMapRegistry>(),Body,Capsule,Out,Failure,Error);
}

bool ULDNavigationComponent::Plan(const FVector& Goal, FName SurfaceHint, LDPathSteering::EGoal GoalKind, TArray<FVector>& Out,
    FVector& Projected, ELDWorldQueryFailure& Failure, FString& Error)
{
    const double Started=FPlatformTime::Seconds();
    ++QueryCount;
    ON_SCOPE_EXIT { QueryMilliseconds+=(FPlatformTime::Seconds()-Started)*1000; };
    Out.Reset();
    if (Goal.ContainsNaN())
    {
        Failure=ELDWorldQueryFailure::InvalidPosition;
        Error=TEXT("InvalidPosition: nonfinite destination.");
        return false;
    }
    FLDLegalGround Body, To;
    FCollisionShape Capsule;
    if (!QueryBodyGround(Body,Capsule,Failure,Error)) { return false; }
    FLDCompletePath Path;
    if (!LDPathSteering::PlanRoute(*GetWorld()->GetSubsystem<ULDMapRegistry>(),Body,Goal,SurfaceHint,GoalKind,Capsule,Path,To,
        Failure,Error)) { return false; }
    Out=MoveTemp(Path.Points);
    Projected=To.SurfaceCm;
    Failure=ELDWorldQueryFailure::None;
    return true;
}

bool ULDNavigationComponent::ShopAt(const FVector& GroundPoint, FName& OutShop, bool bNotify)
{
    OutShop=NAME_None;
    const auto* Loaded=DemoContent();
    const auto* Registry=GetWorld()->GetSubsystem<ULDMapRegistry>();
    // #97: the nearest of the registered shops; a ground point is at a shop when it lies in that shop's reach.
    const FName ShopId=Loaded && Registry?Registry->NearestShop(GroundPoint):NAME_None;
    const FLDMapBinding* Shop=Registry && !ShopId.IsNone()?Registry->Resolve(ShopId,Registry->Generation):nullptr;
    if (!Shop || Loaded->Get().Parameters.ShopInteractRadiusCm.Denominator<=0)
    {
        if (auto* PC=Controller(); PC && bNotify) { Status=FString(LDTerms::Shop)+TEXT("不可用"); PC->PostIntentNotice(Status); }
        return false;
    }
    const auto& Radius=Loaded->Get().Parameters.ShopInteractRadiusCm;
    const double Reach=double(Radius.Numerator)/double(Radius.Denominator);
    if (FVector::DistSquared2D(GroundPoint,Shop->Position)<=FMath::Square(Reach)) { OutShop=ShopId; }
    return true;
}

bool ULDNavigationComponent::OwnBodyAtShop(FName& OutShop)
{
    OutShop=NAME_None;
    auto* PC=Controller();
    const auto* Body=PC?Cast<ALDCharacter>(PC->GetPawn()):nullptr;
    if (!Body || !Body->GetCharacterMovement()) { return false; }
    return ShopAt(Body->GetCharacterMovement()->GetActorFeetLocation(),OutShop,false) && !OutShop.IsNone();
}

bool ULDNavigationComponent::NextLocalIdentity(bool Cancel, FLDMoveIdentity& Out)
{
    auto* PC=Controller();
    if (!PC || Serial==MAX_uint32 || (Cancel && Epoch==MAX_uint32))
    {
        Status=TEXT("订单编号已用尽，无法再下达新订单");
        if (PC) { PC->PostIntentNotice(Status); }
        UE_LOG(LogLDNav, Error, TEXT("LD_NAV_IDENTITY_EXHAUSTED"));
        return false;
    }
    if (Cancel) { ++Epoch; }
    Out={PC->OwnerState.Match,PC->OwnerState.Life,++Serial,Epoch};
    return true;
}

void ULDNavigationComponent::Navigate(const FVector& Surface, bool bQueue)
{
    const FName Hint=GroundSurfaceHint();
    if (Hint.IsNone())
    {
        Status=TEXT("当前地图没有可用地面");
        if (auto* PC=Controller()) { PC->PostIntentNotice(Status); }
        return;
    }
    NavigateGround(Surface,Hint,nullptr,bQueue);
}

void ULDNavigationComponent::NavigateGround(const FVector& Surface, FName SurfaceHint, const FVector* MarkerCm, bool bQueue)
{
    bHeldMove=false;
    FLDSpatialOrderRequest Request;
    Request.Kind=ELDSpatialOrderKind::MoveGround;
    Request.GroundCm=Surface;
    Request.SurfaceHint=SurfaceHint;
    Request.bQueue=bQueue;
    if (!SendOrder(Request,LDNavigationDetail::PendingText(Request.Kind))) { return; }
    // The ground marker answers in the same frame: green, or a red cross where the local route already fails (the
    // server's own refusal still follows as the notice).
    bool bKnown=false;
    const bool bReachable=ProbeGround(Surface,bKnown) || !bKnown;
    if (ALDPointerFeedback* Feedback=EnsurePointer()) { Feedback->ShowMove(MarkerCm?*MarkerCm:Surface,bReachable); }
}

void ULDNavigationComponent::AttackUnit(const FGuid& Observed, uint32 ObservationEpoch, bool bQueue)
{
    bHeldMove=false;
    FLDSpatialOrderRequest Request;
    Request.Kind=ELDSpatialOrderKind::AttackUnit;
    Request.Observed=Observed;
    Request.ObservationEpoch=ObservationEpoch;
    Request.bQueue=bQueue;
    if (!SendOrder(Request,LDNavigationDetail::PendingText(Request.Kind))) { return; }
    if (ALDPointerFeedback* Feedback=EnsurePointer()) { Feedback->ShowAttack(Observed,ObservationEpoch); }
}

void ULDNavigationComponent::CastAtUnit(const FLDCombatActionRequest& Cast, bool bQueue)
{
    bHeldMove=false;
    FLDSpatialOrderRequest Request;
    Request.Kind=ELDSpatialOrderKind::CastUnit;
    Request.Observed=Cast.Target.Observed;
    Request.ObservationEpoch=Cast.Target.ObservationEpoch;
    Request.bQueue=bQueue;
    if (!SendOrder(Request,LDNavigationDetail::PendingText(Request.Kind),&Cast)) { return; }
    if (ALDPointerFeedback* Feedback=EnsurePointer()) { Feedback->ShowAttack(Request.Observed,Request.ObservationEpoch); }
}

void ULDNavigationComponent::CastAtPoint(const FLDCombatActionRequest& Cast, const FVector& PointCm, bool bQueue)
{
    bHeldMove=false;
    FLDSpatialOrderRequest Request;
    Request.Kind=ELDSpatialOrderKind::CastPoint;
    Request.GroundCm=PointCm;
    Request.SurfaceHint=GroundSurfaceHint();
    Request.bQueue=bQueue;
    SendOrder(Request,LDNavigationDetail::PendingText(Request.Kind),&Cast);
}

// A missing ground point is still sent: the server judges the intent and ends protection.
void ULDNavigationComponent::AttackMove(const FVector& Surface, FName SurfaceHint, bool bQueue)
{
    bHeldMove=false;
    FLDSpatialOrderRequest Request;
    Request.Kind=ELDSpatialOrderKind::AttackMove;
    Request.GroundCm=Surface;
    Request.SurfaceHint=SurfaceHint;
    Request.bQueue=bQueue;
    if (!SendOrder(Request,LDNavigationDetail::PendingText(Request.Kind)) || SurfaceHint.IsNone()) { return; }
    // The ground marker of a right-click move, in red, in the same frame; a point the local route check already refuses
    // is the same red cross.
    bool bKnown=false;
    const bool bReachable=ProbeGround(Surface,bKnown) || !bKnown;
    if (ALDPointerFeedback* Feedback=EnsurePointer()) { Feedback->ShowMove(Surface,bReachable,true); }
}

void ULDNavigationComponent::ApproachShop(FName ShopId)
{
    bHeldMove=false;
    FLDSpatialOrderRequest Request;
    Request.Kind=ELDSpatialOrderKind::ApproachShop;
    Request.ShopId=ShopId;
    SendOrder(Request,LDNavigationDetail::PendingText(Request.Kind));
}

// The previous accepted order keeps running (and is still predicted) until the server activates this one.
bool ULDNavigationComponent::SendOrder(FLDSpatialOrderRequest Request, const TCHAR* PendingStatus, const FLDCombatActionRequest* Cast)
{
    auto* PC=Controller();
    if (!Enabled() || !IsLocalOwner() || !PC->CanWorldCommand() || !NextLocalIdentity(false,Local)) { return false; }
    Request.Identity=Local;
    LogOwnOrder(Request);
    // Only the newest local order can carry the shop interaction binding.
    ShopBindingId=Request.Kind==ELDSpatialOrderKind::ApproachShop ? Request.ShopId : NAME_None;
    ShopBindingIdentity=Request.Kind==ELDSpatialOrderKind::ApproachShop ? Local : FLDMoveIdentity();
    bShopArrived=false;
    bLocalStop=false;
    bLocalPending=true;
    // A non-Shift order replaces the server's queue, so its waypoints go now; a Shift ground step adds one. A cast only
    // drops the queue once the server admits it (a refused skill keeps it, 2026-11-07 decision): its dropped steps'
    // Cancelled receipts remove their waypoints.
    if (!Request.bQueue) { if (!Cast) { QueuedMarks.Reset(); } }
    else if (Request.Kind==ELDSpatialOrderKind::MoveGround || Request.Kind==ELDSpatialOrderKind::AttackMove
        || Request.Kind==ELDSpatialOrderKind::CastPoint)
    { QueuedMarks.Add({Local,FVector(Request.GroundCm),Request.Kind!=ELDSpatialOrderKind::MoveGround}); }
    RefreshQueuedPath();
    RefreshLocalActive();
    Status=Request.bQueue?LDNavigationDetail::QueuedText():PendingStatus;
    LocalSentAt=FPlatformTime::Seconds();
    if (Cast) { ServerSubmitCast(Request,*Cast); }
    else { ServerSubmitOrder(Request); }
    return true;
}

void ULDNavigationComponent::QueueCombatAction(const FLDCombatActionRequest& Request, const FVector* AimCm)
{
    auto* PC=Controller();
    if (!Enabled() || !IsLocalOwner() || !PC->CanWorldCommand()) { return; }
    Status=LDNavigationDetail::QueuedText();
    ServerQueueCombatAction(Request,AimCm!=nullptr,AimCm?FVector_NetQuantize10(*AimCm):FVector_NetQuantize10(FVector::ZeroVector));
}

// The waypoints of the own queued ground steps, drawn after the current goal; local presentation only.
void ULDNavigationComponent::RefreshQueuedPath()
{
    ALDPointerFeedback* Feedback=QueuedMarks.IsEmpty()?Pointer.Get():EnsurePointer();
    if (!Feedback) { return; }
    TArray<FVector> Waypoints;
    TArray<bool> Attack;
    for (const FQueuedMark& Mark : QueuedMarks)
    {
        Waypoints.Add(Mark.GroundCm);
        Attack.Add(Mark.bAttack);
    }
    const bool bGoal=ClientActive.Order!=0 && bActiveHasGoal && (ActiveKind==ELDSpatialOrderKind::MoveGround
        || ActiveKind==ELDSpatialOrderKind::AttackMove || ActiveKind==ELDSpatialOrderKind::CastPoint);
    Feedback->SetQueue(Waypoints,Attack,bGoal?&ActiveGoal:nullptr);
}

void ULDNavigationComponent::Stop()
{
    auto* PC=Controller();
    if (!Enabled() || !IsLocalOwner()) { return; }
    bHeldMove=false;
    PC->CancelArmedCombat();
    if (!NextLocalIdentity(true,Local)) { return; }
    ShopBindingId=NAME_None;
    ShopBindingIdentity={};
    bShopArrived=false;
    bLocalStop=bLocalPending=true;
    bFollowing=false;
    Points.Reset();
    QueuedMarks.Reset();
    RefreshQueuedPath();
    RefreshLocalActive();
    Status=TEXT("停止请求已发送");
    if (!PC->HasAuthority()) { LDPathSteering::HaltLocomotion(Cast<ALDCharacter>(PC->GetPawn())); }
    LocalSentAt=FPlatformTime::Seconds();
    UE_LOG(LogLDNav,Log,TEXT("LD_SELF_ORDER kind=Stop match=%u life=%u order=%u"),Local.Match,Local.Life,Local.Order);
    ServerStop(Local);
}

void ULDNavigationComponent::LogOwnOrder(const FLDSpatialOrderRequest& Request)
{
    if (!UE_LOG_ACTIVE(LogLDNav,Log)) { return; }
    // A held move button repeats up to eight times a second; the log keeps at most four of them.
    uint32 Suppressed=0;
    if (!OrderLogLimit.Allow(FPlatformTime::Seconds(),Suppressed)) { return; }
    FString Target;
    if (Request.Kind==ELDSpatialOrderKind::AttackUnit || Request.Kind==ELDSpatialOrderKind::CastUnit)
    {
        // The observer-local handle means nothing later; the visible unit's public identity does.
        FLDObservedUnitView Unit;
        const auto* PC=Controller();
        const auto* Relay=PC?PC->LocalView():nullptr;
        Target=!Relay || !Relay->ReadUnit(Request.Observed,Request.ObservationEpoch,Unit) ? FString(TEXT(" target=unseen"))
            : Unit.Kind==ELDObservedUnitKind::Contender ? FString::Printf(TEXT(" target=%s"),*LDLog::Id(Unit.ContenderId))
            : FString::Printf(TEXT(" target_monster=%u"),uint32(Unit.CreatureId));
    }
    else if (Request.Kind==ELDSpatialOrderKind::ApproachShop) { Target=TEXT(" shop=")+Request.ShopId.ToString(); }
    else { Target=TEXT(" point=")+LDLog::Cm(Request.GroundCm); }
    UE_LOG(LogLDNav,Log,TEXT("LD_SELF_ORDER kind=%s match=%u life=%u order=%u%s suppressed=%u"),*LDLog::EnumName(Request.Kind),
        Request.Identity.Match,Request.Identity.Life,Request.Identity.Order,*Target,Suppressed);
}

void ULDNavigationComponent::ResetLife()
{
    bFollowing=bLocalActive=bLocalPending=bLocalStop=false;
    bActiveHasGoal=bActiveEngaged=false;
    bHeldMove=false;
    ClientActive={};
    ShopBindingId=NAME_None;
    ShopBindingIdentity={};
    bShopArrived=false;
    Points.Reset();
    QueuedMarks.Reset();
    RefreshQueuedPath();
    bCameraInitialized=false;
    bFollowCamera=true;
    bRecenterHeld=false;
    if (auto* PC=Controller())
    {
        PC->CancelArmedCombat();
        SeenMatch=PC->OwnerState.Match;
        SeenLife=PC->OwnerState.Life;
        NextLocalIdentity(true,Local);
    }
    EndStaleServerOrders();
}

void ULDNavigationComponent::RefreshLocalActive()
{ bLocalActive=(bLocalPending && !bLocalStop) || ClientActive.Order!=0; }

void ULDNavigationComponent::ClientOrderReceipt_Implementation(const FLDOrderReceipt& Receipt)
{
    auto* PC=Controller();
    if (!PC || !IsLocalOwner() || Receipt.Identity.Match!=PC->OwnerState.Match || Receipt.Identity.Life!=PC->OwnerState.Life
        || Receipt.ReceiptSerial<=LastReceiptSerial) { return; }
    LastReceiptSerial=Receipt.ReceiptSerial;
    LastReceipt=Receipt;
    const FLDMoveIdentity& Identity=Receipt.Identity;
    const bool bLocal=Identity==Local;
    const bool bActive=ClientActive.Order!=0 && Identity==ClientActive;
    if (bLocal && AcknowledgedOrder!=Identity.Order)
    { AcknowledgedOrder=Identity.Order; AcknowledgementMs.Add((FPlatformTime::Seconds()-LocalSentAt)*1000); }
    // A queued waypoint leaves once its step starts or ends.
    if (Receipt.Status!=ELDOrderStatus::Queued
        && QueuedMarks.RemoveAll([&Identity](const FQueuedMark& Mark) { return Mark.Identity==Identity; })>0)
    { RefreshQueuedPath(); }
    switch (Receipt.Status)
    {
    case ELDOrderStatus::Queued:
        break;
    case ELDOrderStatus::Activated:
    case ELDOrderStatus::Progress:
        if (ClientActive.Order>Identity.Order) { return; }
        ClientActive=Identity;
        ActiveKind=Receipt.Kind;
        bActiveHasGoal=Receipt.bHasGoal;
        ActiveGoal=Receipt.GoalCm;
        ActiveAcceptance=Receipt.AcceptanceCm;
        bActiveEngaged=Receipt.bEngaged;
        if (bLocal) { bLocalPending=false; }
        if (bLocal && Receipt.Status==ELDOrderStatus::Activated && AcceptedOrder!=Identity.Order)
        { AcceptedOrder=Identity.Order; RouteAcceptanceMs.Add((FPlatformTime::Seconds()-LocalSentAt)*1000); }
        ApplyPrediction();
        if (!QueuedMarks.IsEmpty()) { RefreshQueuedPath(); }
        break;
    default:
        if (bLocal) { bLocalPending=false; }
        if (bActive)
        {
            ClientActive={};
            bActiveHasGoal=bActiveEngaged=false;
            bFollowing=false;
            Points.Reset();
            // A superseding activation follows immediately, and so does the next queued walk after an arrival; only a
            // real end stops the predicted body.
            const bool bWalkOn=Receipt.Status==ELDOrderStatus::Completed && !QueuedMarks.IsEmpty();
            if (!PC->HasAuthority() && Receipt.Failure!=ELDOrderFailure::Superseded && !bWalkOn)
            { LDPathSteering::HaltLocomotion(Cast<ALDCharacter>(PC->GetPawn())); }
        }
        break;
    }
    if (bLocal || (bActive && !bLocalPending))
    {
        Status=LDNavigationDetail::ReceiptText(Receipt,bLocal && bLocalStop);
        PC->PostIntentNotice(LDNavigationDetail::NoticeText(Receipt));
    }
    RefreshLocalActive();
    if (!ShopBindingId.IsNone() && Identity==ShopBindingIdentity)
    {
        if (Receipt.Status==ELDOrderStatus::Completed)
        {
            if (!bShopArrived)
            {
                bShopArrived=true;
                OnShopArrived.Broadcast(ShopBindingId,Identity.Life);
            }
        }
        else if (Receipt.Status==ELDOrderStatus::Rejected || Receipt.Status==ELDOrderStatus::Cancelled)
        {
            ShopBindingId=NAME_None;
            ShopBindingIdentity={};
            bShopArrived=false;
        }
    }
}

bool ULDNavigationComponent::ReadShopInteraction(FName& OutShopId, uint32& OutLife) const
{
    OutShopId=NAME_None;
    OutLife=0;
    const auto* PC=Controller();
    if (!PC || !IsLocalOwner() || ShopBindingId.IsNone() || ShopBindingIdentity.Match!=PC->OwnerState.Match
        || ShopBindingIdentity.Life!=PC->OwnerState.Life || ShopBindingIdentity!=Local)
    { return false; }
    OutShopId=ShopBindingId;
    OutLife=ShopBindingIdentity.Life;
    return true;
}

// Owning-client prediction of the server's current order; never a second authority.
void ULDNavigationComponent::ApplyPrediction()
{
    auto* PC=Controller();
    auto* Body=PC?Cast<ALDCharacter>(PC->GetPawn()):nullptr;
    if (!Body) { return; }
    if (!bActiveHasGoal || bActiveEngaged || ClientActive.Epoch!=Local.Epoch)
    {
        if (bFollowing && !PC->HasAuthority()) { LDPathSteering::HaltLocomotion(Body); }
        bFollowing=false;
        Points.Reset();
        return;
    }
    if (bFollowing && FVector::DistSquared2D(ActiveGoal,Destination)<1. && AcceptanceRadius==ActiveAcceptance) { return; }
    Destination=ActiveGoal;
    AcceptanceRadius=ActiveAcceptance;
    if (PC->HasAuthority()) { return; }
    FVector Projected;
    ELDWorldQueryFailure Failure=ELDWorldQueryFailure::None;
    FString Error;
    if (!Plan(ActiveGoal,GroundSurfaceHint(),LDNavigationDetail::PredictedGoal(ActiveKind),Points,Projected,Failure,Error))
    {
        bFollowing=false;
        if (ActiveKind==ELDSpatialOrderKind::MoveGround)
        {
            UE_LOG(LogLDNav,Display,TEXT("LD_NAV_PREDICTION_FAILED %s"),*Error);
            Stop();
            Status=TEXT("无法到达该位置"); PC->PostIntentNotice(Status);
        }
        else { UE_LOG(LogLDNav,Verbose,TEXT("LD_NAV_PREDICTION_UNAVAILABLE %s"),*Error); }
        return;
    }
    PointIndex=Points.Num()>1?1:0;
    bFollowing=true;
    bReplanned=false;
    LastProgress=GetWorld()->GetTimeSeconds();
    BestProgress=LDPathSteering::AlongPath(Points,Body->GetNavAgentLocation());
}

void ULDNavigationComponent::Face(const FVector& Direction)
{
    auto* PC=Controller();
    auto* Body=Cast<ALDCharacter>(PC->GetPawn());
    if (Body && !Direction.IsNearlyZero()) { Body->SetFacing(Direction.Rotation().Yaw); }
}

void ULDNavigationComponent::TickComponent(float Delta, ELevelTick TickType, FActorComponentTickFunction* Function)
{
    Super::TickComponent(Delta,TickType,Function);
    if (!Enabled() || !IsLocalOwner()) { return; }
    auto* PC=Controller();
    auto* Body=Cast<ALDCharacter>(PC->GetPawn());
    const double Now=GetWorld()->GetTimeSeconds();
    if (SeenMatch!=PC->OwnerState.Match || SeenLife!=PC->OwnerState.Life) { ResetLife(); }
    if (Body!=LastPawn.Get())
    {
        LastPawn=Body;
        if (Body)
        {
            Body->GetCharacterMovement()->AddTickPrerequisiteComponent(this);
            // Known from this body's first frame, so a later death camera starts where this life's camera was.
            CameraCentre=Body->GetActorLocation();
            bCameraInitialized=bCameraCentreKnown=true;
            bFollowCamera=!PC->Controls || PC->Controls->bStartFollowing;
        }
    }
    // Only a lost life, body, match or phase ends the accepted order. The menu, Settings, panels and confirmations
    // only suppress new commands: the order keeps its heartbeat and path-following (UI 4, 7).
    if (!PC->IsWorldLifeValid())
    {
        ClearPointer();
        if ((bLocalActive || bFollowing) && !bLocalStop) { Stop(); }
        return;
    }
    // Losing OS focus is the one input loss that stops: a single Stop, no heartbeat until focus returns and a fresh
    // press before the next command; nothing resumes on its own.
    if (!FApp::IsUnattended() && FSlateApplication::IsInitialized() && !FSlateApplication::Get().IsActive())
    {
        if (bHadFocus) { PC->FlushPressedKeys(); Stop(); }
        bHadFocus=false;
        ClearPointer();
        return;
    }
    bHadFocus=true;
    if (bLocalActive && Now-LastHeartbeatSent>=.5)
    {
        if (HeartbeatSequence==MAX_uint32) { Stop(); Status=TEXT("移动心跳编号已用尽"); PC->PostIntentNotice(Status); }
        else { LastHeartbeatSent=Now; ServerHeartbeat(ClientActive.Order?ClientActive:Local,++HeartbeatSequence); }
    }
    TickPointer();
    TickHeldMove();
    if (!bFollowing || !Body || PC->HasAuthority()) { return; }
    if (LDPathSteering::AtGoal(*Body,Destination,AcceptanceRadius)) { bFollowing=false; LDPathSteering::HaltLocomotion(Body); return; }
    const FVector Input=LDPathSteering::Steering(Points,PointIndex,*Body);
    Body->AddMovementInput(Input,1.);
    Face(Input);
    const double Progress=LDPathSteering::AlongPath(Points,Body->GetNavAgentLocation());
    if (Progress>=BestProgress+10) { BestProgress=Progress; LastProgress=Now; }
    if (Now-LastProgress>=OrderNoProgressSeconds)
    {
        if (bReplanned) { Stop(); Status=TEXT("路线受阻"); PC->PostIntentNotice(Status); }
        else
        {
            bReplanned=true;
            ++Replans;
            LastProgress=Now;
            BestProgress=0;
            FVector Projected;
            ELDWorldQueryFailure Failure=ELDWorldQueryFailure::None;
            FString Error;
            if (Plan(Destination,GroundSurfaceHint(),LDNavigationDetail::PredictedGoal(ActiveKind),Points,Projected,Failure,Error))
            { PointIndex=Points.Num()>1?1:0; }
            else
            {
                UE_LOG(LogLDNav,Display,TEXT("LD_NAV_PREDICTION_FAILED %s"),*Error);
                Stop();
                Status=TEXT("无法到达该位置"); PC->PostIntentNotice(Status);
            }
        }
    }
}

void ULDNavigationComponent::ClickMove(bool bQueue)
{
    auto* PC=Controller();
    if (!Enabled() || !IsLocalOwner() || (PC->RootUI && PC->RootUI->PointerOverUI())) { return; }
    FGuid Observed;
    uint32 ObservationEpoch=0;
    if (PC->ObservedUnderPointer(Observed,ObservationEpoch)) { AttackUnit(Observed,ObservationEpoch,bQueue); return; }
    FHitResult GroundHit;
    if (!GroundUnderPointer(GroundHit)) { return; }
    // The stall model takes a right click like a wall: an ordinary ground order to the nearest walkable point beside it.
    // The left click opens the shop page.
    FVector Goal;
    PointerGoal(GroundHit,Goal);
    const uint32 Before=Local.Order;
    const FName Hint=GroundSurfaceHint();
    if (Hint.IsNone()) { Navigate(Goal,bQueue); }
    // The marker stays where the click landed, as for any obstacle; only the order's point moves beside the model.
    else { NavigateGround(Goal,Hint,&GroundHit.ImpactPoint,bQueue); }
    // While the button stays down the walk follows the pointer (TickHeldMove); a refused click starts nothing, and a
    // Shift click is a single queued step.
    bHeldMove=!bQueue && Local.Order!=Before;
    HeldSentAt=GetWorld()->GetTimeSeconds();
    HeldPoint=GroundHit.ImpactPoint;
}

bool ULDNavigationComponent::PointerPosition(FVector2D& Out) const
{
#if WITH_DEV_AUTOMATION_TESTS
    if (DevPointer.IsSet())
    {
        Out=DevPointer.GetValue();
        return true;
    }
#endif
    const auto* PC=Controller();
    float X=0,Y=0;
    if (!PC || !PC->GetMousePosition(X,Y)) { return false; }
    Out=FVector2D(X,Y);
    return true;
}

bool ULDNavigationComponent::GroundUnderPointer(FHitResult& Out) const
{
    const auto* PC=Controller();
    FVector2D At;
    return PC && PointerPosition(At) && PC->GetHitResultAtScreenPosition(At,ECC_GameTraceChannel1,false,Out);
}

bool ULDNavigationComponent::ShopUnderPointer(FName& OutShop)
{
    OutShop=NAME_None;
    const auto* PC=Controller();
    const FLDArtResolved* Art=ULDArtSubsystem::Get(this);
    const auto* Loaded=DemoContent();
    const auto* Registry=GetWorld()->GetSubsystem<ULDMapRegistry>();
    FVector2D At;
    FVector Start, Direction;
    if (!PC || !Art || Art->ShopModelBounds.IsEmpty() || !Loaded || !Registry || !PointerPosition(At)
        || !PC->DeprojectScreenPositionToWorld(float(At.X),float(At.Y),Start,Direction)) { return false; }
    const FVector End=Start+Direction*1000000.;
    for (const FBox& Model : Art->ShopModelBounds)
    {
        // Each shop model stands on its own shop anchor; the pointed model names the shop nearest to it.
        const FName ShopId=Registry->NearestShop(Model.GetCenter());
        if (!ShopId.IsNone() && Registry->Resolve(ShopId,Registry->Generation)
            && FMath::LineBoxIntersection(Model,Start,End,End-Start)) { OutShop=ShopId; return true; }
    }
    return false;
}

void ULDNavigationComponent::PointerGoal(const FHitResult& GroundHit, FVector& OutGoal)
{
    OutGoal=GroundHit.ImpactPoint;
    FName Shop;
    if (!ShopUnderPointer(Shop)) { return; }
    // The ground trace passes through the collision-free stall, so its own point would lead into the model.
    auto* PC=Controller();
    const auto* Body=PC?Cast<ALDCharacter>(PC->GetPawn()):nullptr;
    const FLDArtResolved* Art=ULDArtSubsystem::Get(this);
    const auto* Registry=GetWorld()->GetSubsystem<ULDMapRegistry>();
    if (!Body || !Art || !Registry) { return; }
    // Only the pointed shop's own models: each shop stands apart.
    FBox2D Footprint(ForceInit);
    for (const FBox& Model : Art->ShopModelBounds)
    {
        if (Registry->NearestShop(Model.GetCenter())==Shop) { Footprint+=FBox2D(FVector2D(Model.Min),FVector2D(Model.Max)); }
    }
    FVector Foot=Body->GetNavAgentLocation();
    Foot.Z=GroundHit.ImpactPoint.Z;
    FVector Front;
    // Already standing beside the model: the nearest walkable point is where the body is.
    if (!LDOrderRules::ShopFrontPoint(Footprint,Foot,Body->GetCapsuleComponent()->GetScaledCapsuleRadius(),Front)) { OutGoal=Foot; return; }
    for (const FVector& Candidate : LDOrderRules::ShopStopCandidates(Front,Foot,ShopStopStepCm))
    {
        bool bKnown=false;
        // An unknown local route is still sent: the server judges it like any ground order.
        if (ProbeGround(Candidate,bKnown) || !bKnown) { OutGoal=Candidate; return; }
    }
}

bool ULDNavigationComponent::ReadHovered(FGuid& OutHandle, uint32& OutEpoch) const
{
    OutHandle=Hovered;
    OutEpoch=HoveredEpoch;
    return Hovered.IsValid() && HoveredEpoch!=0;
}

void ULDNavigationComponent::DevSetPointer(const TOptional<FVector2D>& Value)
{
#if WITH_DEV_AUTOMATION_TESTS
    DevPointer=Value;
#endif
}

ALDPointerFeedback* ULDNavigationComponent::PointerFeedback() const { return Pointer.Get(); }

void ULDNavigationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if (ALDPointerFeedback* Feedback=Pointer.Get()) { Feedback->Destroy(); }
    Pointer.Reset();
    Super::EndPlay(Reason);
}

ALDPointerFeedback* ULDNavigationComponent::EnsurePointer()
{
    if (ALDPointerFeedback* Existing=Pointer.Get()) { return Existing; }
    auto* PC=Controller();
    UWorld* World=GetWorld();
    if (!PC || !World || World->bIsTearingDown || !IsLocalOwner()) { return nullptr; }
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Params.ObjectFlags|=RF_Transient;
    auto* Spawned=World->SpawnActor<ALDPointerFeedback>(ALDPointerFeedback::StaticClass(),FTransform::Identity,Params);
    if (!Spawned)
    {
        UE_LOG(LogLDNav,Error,TEXT("LD_POINTER_FEEDBACK_UNAVAILABLE The local pointer feedback could not be placed."));
        return nullptr;
    }
    Spawned->Bind(*PC);
    Pointer=Spawned;
    return Spawned;
}

bool ULDNavigationComponent::ProbeGround(const FVector& GroundCm, bool& bKnown) const
{
    bKnown=false;
    FLDLegalGround Body, To;
    FCollisionShape Capsule;
    ELDWorldQueryFailure Failure=ELDWorldQueryFailure::None;
    FString Error;
    if (GroundCm.ContainsNaN() || !QueryBodyGround(Body,Capsule,Failure,Error)) { return true; }
    bKnown=true;
    FLDCompletePath Path;
    return LDPathSteering::PlanRoute(*GetWorld()->GetSubsystem<ULDMapRegistry>(),Body,GroundCm,GroundSurfaceHint(),
        LDPathSteering::EGoal::Ground,Capsule,Path,To,Failure,Error);
}

// Hover: the visible unit a right click would attack gets the ring, read from the authorized frame only. The pointer is
// the plain arrow except while an order waits for its left click: a confirmed attack-move glows red, a confirmed active
// skill or equipment active blue (LDCursor); hovering a unit or the shop never changes it.
void ULDNavigationComponent::TickPointer()
{
    auto* PC=Controller();
    Hovered.Invalidate();
    HoveredEpoch=0;
    EMouseCursor::Type Cursor=EMouseCursor::Default;
    ELDCombatEntry Armed=ELDCombatEntry::None;
    if (PC->CanWorldCommand() && (!PC->RootUI || !PC->RootUI->PointerOverUI()))
    {
        PC->ObservedUnderPointer(Hovered,HoveredEpoch);
        if (PC->IsAttackMoveArmed()) { Cursor=LDCursor::AttackAim; }
        else if (PC->ReadArmedCombatEntry(Armed)) { Cursor=LDCursor::CastAim; }
    }
    // Slate asks for the cursor only when the pointer moves; a changed aim (A or a skill just armed, or a confirm or
    // cancel) asks again at once, so the glow shows the moment the key is pressed.
    if (PC->CurrentMouseCursor!=Cursor)
    {
        PC->CurrentMouseCursor=Cursor;
        if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().QueryCursor(); }
    }
    // The inspected unit keeps its selection ring while the current authorized frame still lists it.
    FLDObservedUnitView Inspected;
    const bool bInspected=PC->ReadInspected(Inspected);
    if (ALDPointerFeedback* Feedback=EnsurePointer())
    {
        Feedback->SetHover(Hovered,HoveredEpoch);
        Feedback->SetSelected(bInspected?Inspected.Handle:FGuid(),bInspected?Inspected.ObservationEpoch:0);
    }
}

void ULDNavigationComponent::ClearPointer()
{
    Hovered.Invalidate();
    HoveredEpoch=0;
    bHeldMove=false;
    if (auto* PC=Controller(); PC && PC->CurrentMouseCursor!=EMouseCursor::Default)
    {
        PC->CurrentMouseCursor=EMouseCursor::Default;
        if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().QueryCursor(); }
    }
    if (ALDPointerFeedback* Feedback=Pointer.Get()) { Feedback->SetHover(FGuid(),0); Feedback->SetSelected(FGuid(),0); }
}

// The held move button: a new ground order toward the pointer at most 8 times a second and only after the pointer's
// ground point moved; ground the local route check already refuses is skipped quietly, so the walk keeps its last
// order. Every repeat is an ordinary order with a fresh identity.
void ULDNavigationComponent::TickHeldMove()
{
    if (!bHeldMove) { return; }
    auto* PC=Controller();
    const FKey Key=PC->BindingKey(TEXT("Navigate"));
    if (!Key.IsValid() || !PC->IsInputKeyDown(Key) || !PC->CanWorldCommand())
    {
        bHeldMove=false;
        return;
    }
    if (PC->RootUI && PC->RootUI->PointerOverUI()) { return; }
    const double Now=GetWorld()->GetTimeSeconds();
    FHitResult GroundHit;
    FVector Goal;
    if (!GroundUnderPointer(GroundHit) || !LDOrderRules::ShouldRepeatHeldMove(Now,HeldSentAt,HeldPoint,GroundHit.ImpactPoint)) { return; }
    PointerGoal(GroundHit,Goal);
    bool bKnown=false;
    const FName Hint=GroundSurfaceHint();
    if (Hint.IsNone() || (!ProbeGround(Goal,bKnown) && bKnown)) { return; }
    FLDSpatialOrderRequest Request;
    Request.Kind=ELDSpatialOrderKind::MoveGround;
    Request.GroundCm=Goal;
    Request.SurfaceHint=Hint;
    if (!SendOrder(Request,LDNavigationDetail::PendingText(Request.Kind)))
    {
        bHeldMove=false;
        return;
    }
    HeldSentAt=Now;
    HeldPoint=GroundHit.ImpactPoint;
}

void ULDNavigationComponent::PanTo(const FVector& Point)
{ CameraCentre=Point; bFollowCamera=false; bCameraInitialized=bCameraCentreKnown=true; }
void ULDNavigationComponent::PanCamera(const FVector2D& Axis, float Delta)
{
    if (!Axis.IsNearlyZero())
    {
        // The applied local camera speed (cm/s, UI settings); before settings apply, the setting's own default.
        const ALDPlayerController* PC=Controller();
        const float Speed=PC&&PC->Controls?PC->Controls->GetCameraPanCmPerSec():GetDefault<ULDControlSettings>()->GetCameraPanCmPerSec();
        CameraCentre+=FRotator(0,LDCamera::Yaw,0).RotateVector(FVector(Axis.Y,Axis.X,0))*Speed*Delta;
        bFollowCamera=false;
    }
}
void ULDNavigationComponent::Recenter(bool Held) { bRecenterHeld=Held; }
void ULDNavigationComponent::ToggleFollow() { bFollowCamera=!bFollowCamera; }
void ULDNavigationComponent::UpdateLocalCamera(float Delta)
{
    if (!Enabled() || !IsLocalOwner()) { return; }
    auto* PC=Controller();
    auto* Body=Cast<ALDCharacter>(PC->GetPawn());
    // Dead in the running match, the owner's waiting camera keeps this same camera state over the static map:
    // it pans and zooms, never follows anyone and never moves a body (CONTROLS 3.3, UI 4).
    ACameraActor* DeathView=!Body && bCameraCentreKnown && PC->IsAwaitingRespawn()
        ? Cast<ACameraActor>(PC->GetViewTarget()) : nullptr;
    if (!Body && !DeathView) { return; }
    if (Body && !bCameraInitialized) { CameraCentre=Body->GetActorLocation(); bCameraInitialized=bCameraCentreKnown=true; }
    if (PC->CanCameraCommand())
    {
        FVector2D Axis=PC->CameraPanAxis();
        // Edge pan stays off without focus, over UI, during a minimap drag and while an armed preview aims.
        ELDCombatEntry Armed=ELDCombatEntry::None;
        if (bEdgePan && (!PC->HUDUI || !PC->HUDUI->Minimap || !PC->HUDUI->Minimap->IsDragging())
            && !FApp::IsUnattended() && FSlateApplication::IsInitialized() && FSlateApplication::Get().IsActive()
            && (!PC->RootUI || !PC->RootUI->PointerOverUI()) && !PC->IsAttackMoveArmed() && !PC->ReadArmedCombatEntry(Armed))
        {
            float X=0,Y=0; int32 Width=0,Height=0;
            PC->GetViewportSize(Width,Height);
            if (PC->GetMousePosition(X,Y) && X>=0 && Y>=0 && X<Width && Y<Height)
            { Axis.X+=(X>=Width-8?1:0)-(X<8?1:0); Axis.Y+=(Y<8?1:0)-(Y>=Height-8?1:0); }
        }
        PanCamera(Axis.GetClampedToMaxSize(1),Delta);
    }
    if (Body && (bFollowCamera || bRecenterHeld)) { CameraCentre=Body->PresentedLocation(); }
    const auto* Registry=GetWorld()->GetSubsystem<ULDMapRegistry>();
    const auto* Geometry=Registry?Registry->GetGeometry():nullptr;
    FVector Min,Max;
    FString Error;
    if (!Geometry || Geometry->Surfaces.Num()!=1 || !Geometry->Surfaces[0].MinCm.ToVector(Min,Error)
        || !Geometry->Surfaces[0].MaxCm.ToVector(Max,Error))
    {
        UE_LOG(LogLDNav, Error, TEXT("LD_CAMERA_WORLD_UNAVAILABLE %s"), *Error);
        return;
    }
    CameraCentre.X=FMath::Clamp(CameraCentre.X,Min.X,Max.X);
    CameraCentre.Y=FMath::Clamp(CameraCentre.Y,Min.Y,Max.Y);
    if (Body)
    {
        Body->CameraArm->SetWorldLocation(CameraCentre);
        return;
    }
    // The applied zoom preference is the death camera's arm length; the next body starts from the same value.
    const float ArmLength=PC->Controls?PC->Controls->Zoom:GetDefault<ALDCharacter>()->CameraArm->TargetArmLength;
    DeathView->SetActorTransform(ALDCharacter::DefaultArmCameraPose(CameraCentre,ArmLength));
}
