#include "Navigation/LDNavigationComponent.h"
#include "Navigation/LDPathSteering.h"
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
#include "UI/LDHUD.h"
#include "UI/LDMinimap.h"

namespace LDNavigationDetail
{
    const TCHAR* FailureText(ELDOrderFailure Failure)
    {
        switch (Failure)
        {
        case ELDOrderFailure::None: return TEXT("");
        case ELDOrderFailure::InvalidRequest: return TEXT("InvalidRequest: the order shape is invalid.");
        case ELDOrderFailure::NotOwner: return TEXT("NotOwner: orders need the current owned body.");
        case ELDOrderFailure::StaleMatch: return TEXT("StaleMatch: the order belongs to another match.");
        case ELDOrderFailure::StaleLife: return TEXT("StaleLife: the order belongs to another life.");
        case ELDOrderFailure::StaleOrder: return TEXT("StaleOrder: an older order or cancelled epoch cannot run again.");
        case ELDOrderFailure::RequestConflict: return TEXT("RequestConflict: this identity has different order terms.");
        case ELDOrderFailure::SequenceExhausted: return TEXT("SequenceExhausted: no further order identity is available.");
        case ELDOrderFailure::InvalidGround: return TEXT("InvalidGround: the destination is not legal ground on the registered surface.");
        case ELDOrderFailure::NoCompletePath: return TEXT("NoCompletePath: no complete walkable route exists.");
        case ELDOrderFailure::PathBlocked: return TEXT("Path blocked");
        case ELDOrderFailure::TargetUnavailable: return TEXT("Target unavailable");
        case ELDOrderFailure::TargetDied: return TEXT("Target died");
        case ELDOrderFailure::ShopUnavailable: return TEXT("Shop unavailable");
        case ELDOrderFailure::Dead: return TEXT("Dead");
        case ELDOrderFailure::MatchEnded: return TEXT("Match ended");
        case ELDOrderFailure::Superseded: return TEXT("Superseded by a newer order");
        case ELDOrderFailure::Stopped: return TEXT("Stopped");
        case ELDOrderFailure::Expired: return TEXT("Order expired");
        default: return TEXT("Order failed");
        }
    }

    const TCHAR* PendingText(ELDSpatialOrderKind Kind)
    {
        switch (Kind)
        {
        case ELDSpatialOrderKind::AttackUnit: return TEXT("Attack pending");
        case ELDSpatialOrderKind::AttackMove: return TEXT("Attack-move pending");
        case ELDSpatialOrderKind::ApproachShop: return TEXT("Shop approach pending");
        default: return TEXT("Move pending");
        }
    }

    const TCHAR* RunningText(const FLDOrderReceipt& Receipt)
    {
        if (Receipt.bEngaged) { return TEXT("Attacking"); }
        switch (Receipt.Kind)
        {
        case ELDSpatialOrderKind::AttackUnit: return TEXT("Chasing target");
        case ELDSpatialOrderKind::AttackMove: return TEXT("Attack-moving");
        case ELDSpatialOrderKind::ApproachShop: return TEXT("Approaching shop");
        default: return TEXT("Move accepted");
        }
    }

    FString ReceiptText(const FLDOrderReceipt& Receipt, bool bStop)
    {
        FString Text;
        switch (Receipt.Status)
        {
        case ELDOrderStatus::Queued: Text=bStop?TEXT("Stop pending"):PendingText(Receipt.Kind); break;
        case ELDOrderStatus::Activated:
        case ELDOrderStatus::Progress: Text=RunningText(Receipt); break;
        case ELDOrderStatus::Completed:
            Text=Receipt.Failure==ELDOrderFailure::None?TEXT("Arrived"):FailureText(Receipt.Failure); break;
        default: Text=FailureText(Receipt.Failure); break;
        }
        if (Receipt.bProtectionRemoved && (Receipt.Status==ELDOrderStatus::Activated || Receipt.Status==ELDOrderStatus::Rejected))
        { Text+=TEXT(" (protection removed)"); }
        return Text;
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
            UE_LOG(LogTemp,Error,TEXT("LD_NAV_CONTENT_UNAVAILABLE %s"),*Error);
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
    if (!Registry->QueryGround(Registry->Revision(),Body->GetNavAgentLocation(),Capsule,Surface,Out,Failure,Error)) { return false; }
    if (Out.Floor.Get()!=Movement->CurrentFloor.HitResult.GetComponent())
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

bool ULDNavigationComponent::Plan(const FVector& Surface, FName SurfaceHint, TArray<FVector>& Out, FVector& Projected,
    ELDWorldQueryFailure& Failure, FString& Error)
{
    const double Started=FPlatformTime::Seconds();
    ++QueryCount;
    ON_SCOPE_EXIT { QueryMilliseconds+=(FPlatformTime::Seconds()-Started)*1000; };
    Out.Reset();
    if (Surface.ContainsNaN())
    {
        Failure=ELDWorldQueryFailure::InvalidPosition;
        Error=TEXT("InvalidPosition: nonfinite destination.");
        return false;
    }
    FLDLegalGround From, To;
    FCollisionShape Capsule;
    if (!QueryPlanStart(From,Capsule,Failure,Error)) { return false; }
    const auto* Registry=GetWorld()->GetSubsystem<ULDMapRegistry>();
    FLDCompletePath Path;
    if (!Registry->QueryGround(Registry->Revision(),Surface,Capsule,SurfaceHint,To,Failure,Error)
        || !Registry->QueryCompletePath(Registry->Revision(),From,To,Capsule,Path,Failure,Error)) { return false; }
    Out=MoveTemp(Path.Points);
    Projected=To.SurfaceCm;
    Failure=ELDWorldQueryFailure::None;
    return true;
}

bool ULDNavigationComponent::ShopAt(const FVector& GroundPoint, FName& OutShop)
{
    OutShop=NAME_None;
    const auto* Loaded=DemoContent();
    const auto* Registry=GetWorld()->GetSubsystem<ULDMapRegistry>();
    const FName ShopId=Loaded?FName(*Loaded->Get().World.ShopId):NAME_None;
    const FLDMapBinding* Shop=Registry && !ShopId.IsNone()?Registry->Resolve(ShopId,Registry->Generation):nullptr;
    if (!Shop || Loaded->Get().Parameters.ShopInteractRadiusCm.Denominator<=0)
    {
        if (auto* PC=Controller()) { Status=PC->IntentFeedback=TEXT("The public shop is unavailable in this world."); }
        return false;
    }
    const auto& Radius=Loaded->Get().Parameters.ShopInteractRadiusCm;
    const double Reach=double(Radius.Numerator)/double(Radius.Denominator);
    if (FVector::DistSquared2D(GroundPoint,Shop->Position)<=FMath::Square(Reach)) { OutShop=ShopId; }
    return true;
}

bool ULDNavigationComponent::NextLocalIdentity(bool Cancel, FLDMoveIdentity& Out)
{
    auto* PC=Controller();
    if (!PC || Serial==MAX_uint32 || (Cancel && Epoch==MAX_uint32))
    {
        Status=TEXT("Movement identity exhausted; this body cannot issue another order.");
        if (PC) { PC->IntentFeedback=Status; }
        UE_LOG(LogTemp, Error, TEXT("LD_NAV_IDENTITY_EXHAUSTED"));
        return false;
    }
    if (Cancel) { ++Epoch; }
    Out={PC->OwnerState.Match,PC->OwnerState.Life,++Serial,Epoch};
    return true;
}

void ULDNavigationComponent::Navigate(const FVector& Surface)
{
    const FName Hint=GroundSurfaceHint();
    if (Hint.IsNone())
    {
        Status=TEXT("The current Demo has no single registered ground surface.");
        if (auto* PC=Controller()) { PC->IntentFeedback=Status; }
        return;
    }
    NavigateGround(Surface,Hint);
}

void ULDNavigationComponent::NavigateGround(const FVector& Surface, FName SurfaceHint)
{
    FLDSpatialOrderRequest Request;
    Request.Kind=ELDSpatialOrderKind::MoveGround;
    Request.GroundCm=Surface;
    Request.SurfaceHint=SurfaceHint;
    SendOrder(Request,TEXT("Move pending"));
}

void ULDNavigationComponent::AttackUnit(const FGuid& Observed, uint32 ObservationEpoch)
{
    FLDSpatialOrderRequest Request;
    Request.Kind=ELDSpatialOrderKind::AttackUnit;
    Request.Observed=Observed;
    Request.ObservationEpoch=ObservationEpoch;
    SendOrder(Request,TEXT("Attack pending"));
}

// A missing ground point is still sent: the server judges the intent and ends protection.
void ULDNavigationComponent::AttackMove(const FVector& Surface, FName SurfaceHint)
{
    FLDSpatialOrderRequest Request;
    Request.Kind=ELDSpatialOrderKind::AttackMove;
    Request.GroundCm=Surface;
    Request.SurfaceHint=SurfaceHint;
    SendOrder(Request,TEXT("Attack-move pending"));
}

void ULDNavigationComponent::ApproachShop(FName ShopId)
{
    FLDSpatialOrderRequest Request;
    Request.Kind=ELDSpatialOrderKind::ApproachShop;
    Request.ShopId=ShopId;
    SendOrder(Request,TEXT("Shop approach pending"));
}

// The previous accepted order keeps running (and is still predicted) until the server activates this one.
void ULDNavigationComponent::SendOrder(FLDSpatialOrderRequest Request, const TCHAR* PendingStatus)
{
    auto* PC=Controller();
    if (!Enabled() || !IsLocalOwner() || !PC->CanWorldCommand() || !NextLocalIdentity(false,Local)) { return; }
    Request.Identity=Local;
    // Only the newest local order can carry the shop interaction binding.
    ShopBindingId=Request.Kind==ELDSpatialOrderKind::ApproachShop ? Request.ShopId : NAME_None;
    ShopBindingIdentity=Request.Kind==ELDSpatialOrderKind::ApproachShop ? Local : FLDMoveIdentity();
    bShopArrived=false;
    bLocalStop=false;
    bLocalPending=true;
    RefreshLocalActive();
    Status=PC->IntentFeedback=PendingStatus;
    LocalSentAt=FPlatformTime::Seconds();
    ServerSubmitOrder(Request);
}

void ULDNavigationComponent::Stop()
{
    auto* PC=Controller();
    if (!Enabled() || !IsLocalOwner()) { return; }
    PC->CancelArmedCombat();
    if (!NextLocalIdentity(true,Local)) { return; }
    ShopBindingId=NAME_None;
    ShopBindingIdentity={};
    bShopArrived=false;
    bLocalStop=bLocalPending=true;
    bFollowing=false;
    Points.Reset();
    RefreshLocalActive();
    Status=TEXT("Stop pending");
    if (!PC->HasAuthority()) { LDPathSteering::HaltLocomotion(Cast<ALDCharacter>(PC->GetPawn())); }
    LocalSentAt=FPlatformTime::Seconds();
    ServerStop(Local);
}

void ULDNavigationComponent::ResetLife()
{
    bFollowing=bLocalActive=bLocalPending=bLocalStop=false;
    bActiveHasGoal=bActiveEngaged=false;
    ClientActive={};
    ShopBindingId=NAME_None;
    ShopBindingIdentity={};
    bShopArrived=false;
    Points.Reset();
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
        break;
    default:
        if (bLocal) { bLocalPending=false; }
        if (bActive)
        {
            ClientActive={};
            bActiveHasGoal=bActiveEngaged=false;
            bFollowing=false;
            Points.Reset();
            // A superseding activation follows immediately; only a real end stops the predicted body.
            if (!PC->HasAuthority() && Receipt.Failure!=ELDOrderFailure::Superseded)
            { LDPathSteering::HaltLocomotion(Cast<ALDCharacter>(PC->GetPawn())); }
        }
        break;
    }
    if (bLocal || (bActive && !bLocalPending))
    { Status=PC->IntentFeedback=LDNavigationDetail::ReceiptText(Receipt,bLocal && bLocalStop); }
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
    if (!Plan(ActiveGoal,GroundSurfaceHint(),Points,Projected,Failure,Error))
    {
        bFollowing=false;
        if (ActiveKind==ELDSpatialOrderKind::MoveGround) { Stop(); Status=PC->IntentFeedback=Error; }
        else { UE_LOG(LogTemp,Verbose,TEXT("LD_NAV_PREDICTION_UNAVAILABLE %s"),*Error); }
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
        if ((bLocalActive || bFollowing) && !bLocalStop) { Stop(); }
        TickCamera(Delta);
        return;
    }
    // Losing OS focus is the one input loss that stops: a single Stop, no heartbeat until focus returns and a fresh
    // press before the next command; nothing resumes on its own.
    if (!FApp::IsUnattended() && FSlateApplication::IsInitialized() && !FSlateApplication::Get().IsActive())
    {
        if (bHadFocus) { PC->FlushPressedKeys(); Stop(); }
        bHadFocus=false;
        return;
    }
    bHadFocus=true;
    if (bLocalActive && Now-LastHeartbeatSent>=.5)
    {
        if (HeartbeatSequence==MAX_uint32) { Stop(); Status=PC->IntentFeedback=TEXT("Movement heartbeat sequence exhausted."); }
        else { LastHeartbeatSent=Now; ServerHeartbeat(ClientActive.Order?ClientActive:Local,++HeartbeatSequence); }
    }
    TickCamera(Delta);
    if (!bFollowing || !Body || PC->HasAuthority()) { return; }
    if (LDPathSteering::AtGoal(*Body,Destination,AcceptanceRadius)) { bFollowing=false; LDPathSteering::HaltLocomotion(Body); return; }
    const FVector Input=LDPathSteering::Steering(Points,PointIndex,*Body);
    Body->AddMovementInput(Input,1.);
    Face(Input);
    const double Progress=LDPathSteering::AlongPath(Points,Body->GetNavAgentLocation());
    if (Progress>=BestProgress+10) { BestProgress=Progress; LastProgress=Now; }
    if (Now-LastProgress>=OrderNoProgressSeconds)
    {
        if (bReplanned) { Stop(); Status=PC->IntentFeedback=TEXT("Path blocked"); }
        else
        {
            bReplanned=true;
            ++Replans;
            LastProgress=Now;
            BestProgress=0;
            FVector Projected;
            ELDWorldQueryFailure Failure=ELDWorldQueryFailure::None;
            FString Error;
            if (Plan(Destination,GroundSurfaceHint(),Points,Projected,Failure,Error)) { PointIndex=Points.Num()>1?1:0; }
            else { Stop(); Status=PC->IntentFeedback=Error; }
        }
    }
}

void ULDNavigationComponent::ClickMove()
{
    auto* PC=Controller();
    if (!Enabled() || !IsLocalOwner() || (PC->RootUI && PC->RootUI->PointerOverUI())) { return; }
    FGuid Observed;
    uint32 ObservationEpoch=0;
    if (PC->ObservedUnderPointer(Observed,ObservationEpoch)) { AttackUnit(Observed,ObservationEpoch); return; }
    FHitResult GroundHit;
    if (!PC->GetHitResultUnderCursor(ECC_GameTraceChannel1,false,GroundHit)) { return; }
    FName Shop;
    if (!ShopAt(GroundHit.ImpactPoint,Shop)) { return; }
    if (!Shop.IsNone()) { ApproachShop(Shop); return; }
    Navigate(GroundHit.ImpactPoint);
}

void ULDNavigationComponent::PanTo(const FVector& Point)
{ CameraCentre=Point; bFollowCamera=false; bCameraInitialized=bCameraCentreKnown=true; }
void ULDNavigationComponent::PanCamera(const FVector2D& Axis, float Delta)
{
    if (!Axis.IsNearlyZero())
    {
        CameraCentre+=FRotator(0,-45,0).RotateVector(FVector(Axis.Y,Axis.X,0))*1800*Delta;
        bFollowCamera=false;
    }
}
void ULDNavigationComponent::Recenter(bool Held) { bRecenterHeld=Held; }
void ULDNavigationComponent::ToggleFollow() { bFollowCamera=!bFollowCamera; }
void ULDNavigationComponent::TickCamera(float Delta)
{
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
    if (Body && (bFollowCamera || bRecenterHeld)) { CameraCentre=Body->GetActorLocation(); }
    const auto* Registry=GetWorld()->GetSubsystem<ULDMapRegistry>();
    const auto* Geometry=Registry?Registry->GetGeometry():nullptr;
    FVector Min,Max;
    FString Error;
    if (!Geometry || Geometry->Surfaces.Num()!=1 || !Geometry->Surfaces[0].MinCm.ToVector(Min,Error)
        || !Geometry->Surfaces[0].MaxCm.ToVector(Max,Error))
    {
        UE_LOG(LogTemp, Error, TEXT("LD_CAMERA_WORLD_UNAVAILABLE %s"), *Error);
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
