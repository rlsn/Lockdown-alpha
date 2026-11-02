#include "Navigation/LDNavigationComponent.h"
#include "Navigation/LDPathSteering.h"
#include "Framework/LDCommandLiveness.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDDemoEntry.h"
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
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

namespace
{
    constexpr uint8 Queued=0, NavAccepted=1, Arrived=2, Stopped=3, Rejected=4;
    constexpr double QueryInterval=0.1, NoProgressSeconds=2.;
    using LDPathSteering::HaltLocomotion;
    using LDPathSteering::AlongPath;
    using LDPathSteering::Steering;
    using LDPathSteering::AtGoal;
}

ULDNavigationComponent::ULDNavigationComponent()
{
    SetIsReplicatedByDefault(true);
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.TickGroup=TG_PrePhysics;
}

ALDPlayerController* ULDNavigationComponent::Controller() const { return Cast<ALDPlayerController>(GetOwner()); }

bool ULDNavigationComponent::Enabled() const
{
    const auto* PC=Controller();
    return PC && GetWorld() && UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName())==LDDemoEntry::WorldMap;
}

bool ULDNavigationComponent::Plan(const FVector& Surface, FName SurfaceHint,
    TArray<FVector>& Out, FVector& Projected, FString& Error)
{
    const double Started=FPlatformTime::Seconds();
    ++QueryCount;
    ON_SCOPE_EXIT { QueryMilliseconds+=(FPlatformTime::Seconds()-Started)*1000; };
    Out.Reset();
    const auto* PC=Controller();
    const auto* Body=PC?Cast<ALDCharacter>(PC->GetPawn()):nullptr;
    auto* Registry=GetWorld()->GetSubsystem<ULDMapRegistry>();
    if (!Body || !Registry || !Registry->Enabled() || Surface.ContainsNaN())
    {
        Error=TEXT("No current body or registered Demo world.");
        return false;
    }
    const auto* Movement=Body->GetCharacterMovement();
    const auto* Shape=Body->GetCapsuleComponent();
    const FCollisionShape Capsule=FCollisionShape::MakeCapsule(Shape->GetScaledCapsuleRadius(),Shape->GetScaledCapsuleHalfHeight());
    FLDLegalGround From, To;
    ELDWorldQueryFailure Failure;
    if (!Movement->IsMovingOnGround() || !Movement->CurrentFloor.IsWalkableFloor()
        || !Registry->QueryGround(Registry->Revision(),Body->GetNavAgentLocation(),Capsule,SurfaceHint,From,Failure,Error)
        || From.Floor.Get()!=Movement->CurrentFloor.HitResult.GetComponent())
    {
        if (Error.IsEmpty()) { Error=TEXT("NotGrounded: the body's current native support is not registered."); }
        return false;
    }
    if (!Registry->QueryGround(Registry->Revision(),Surface,Capsule,SurfaceHint,To,Failure,Error)) { return false; }
    FLDCompletePath Path;
    if (!Registry->QueryCompletePath(Registry->Revision(),From,To,Capsule,Path,Failure,Error)) { return false; }
    Out=MoveTemp(Path.Points);
    Projected=To.SurfaceCm;
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
    auto* Registry=GetWorld()->GetSubsystem<ULDMapRegistry>();
    const auto* Geometry=Registry?Registry->GetGeometry():nullptr;
    if (!Geometry || Geometry->Surfaces.Num()!=1)
    {
        Status=TEXT("The current Demo has no single registered ground surface.");
        if (auto* PC=Controller()) { PC->IntentFeedback=Status; }
        return;
    }
    NavigateGround(Surface,FName(*Geometry->Surfaces[0].Id));
}

void ULDNavigationComponent::NavigateGround(const FVector& Surface, FName SurfaceHint)
{
    auto* PC=Controller();
    if (!Enabled() || !PC->CanWorldCommand() || !PC->IsLocalController()) { return; }
    if (!NextLocalIdentity(false,Local)) { return; }
    LocalSurfaceHint=SurfaceHint;
    bFollowing=false;
    bLocalActive=true;
    Points.Reset();
    if (!PC->HasAuthority()) { HaltLocomotion(Cast<ALDCharacter>(PC->GetPawn())); }
    Status=PC->IntentFeedback=TEXT("Move pending");
    LocalSentAt=FPlatformTime::Seconds();
    ServerOrder(Local,Surface,SurfaceHint);
}

void ULDNavigationComponent::Stop()
{
    auto* PC=Controller();
    if (!Enabled() || !PC->IsLocalController() || !NextLocalIdentity(true,Local)) { return; }
    bFollowing=bLocalActive=false;
    Points.Reset();
    Status=TEXT("Stop pending");
    if (!PC->HasAuthority()) { HaltLocomotion(Cast<ALDCharacter>(PC->GetPawn())); }
    ServerStop(Local);
}

void ULDNavigationComponent::ResetLife()
{
    bFollowing=bLocalActive=bServerActive=bPending=false;
    Points.Reset();
    ServerPoints.Reset();
    bCameraInitialized=false;
    bFollowCamera=true;
    bRecenterHeld=false;
    if (auto* PC=Controller())
    {
        SeenMatch=PC->OwnerState.Match;
        SeenLife=PC->OwnerState.Life;
        NextLocalIdentity(true,Local);
        Server=Received=Pending={SeenMatch,SeenLife,0,0};
    }
}

bool ULDNavigationComponent::AcceptEnvelope(const FLDMoveIdentity& Identity, FString& Error) const
{
    const auto* PC=Controller();
    const auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    const auto* Body=PC?Cast<ALDCharacter>(PC->GetPawn()):nullptr;
    FLDCombatBodyRef Ref;
    if (!Enabled() || !Mode || !PC || !Body || Body->GetController()!=PC
        || !Mode->IsGameplayWriteOpen(Mode->GetMatchContext()) || !Mode->ReadBodyRef(*Body,Ref)
        || Identity.Match!=Mode->GetMatchContext().Generation || Identity.Life!=Ref.Victim.Life || !Identity.Order)
    {
        Error=TEXT("NotOwner/StaleLife: ground orders require the current registered, owned live body.");
        return false;
    }
    return true;
}

bool ULDNavigationComponent::AuthorizesMove(const FLDMoveIdentity& Identity) const
{
    FString Error;
    const auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    return AcceptEnvelope(Identity,Error) && bServerActive && Identity==Server
        && LDCommandLiveness::Fresh(Mode->GetAuthorityNowSeconds(),LastHeartbeat);
}

void ULDNavigationComponent::ServerOrder_Implementation(FLDMoveIdentity Identity,
    FVector_NetQuantize10 Surface, FName SurfaceHint)
{
    FString Error;
    if (!AcceptEnvelope(Identity,Error) || Surface.ContainsNaN())
    {
        ClientOrder(Identity,Rejected,{},0,Error.IsEmpty()?TEXT("Invalid ground-order shape."):Error);
        return;
    }
    if (Received.Match!=Identity.Match || Received.Life!=Identity.Life)
    { Received=Server={Identity.Match,Identity.Life,0,0}; }
    if (Identity==Received)
    {
        if (bReceivedStop || FVector(Surface)!=ReceivedSurface || SurfaceHint!=ReceivedSurfaceHint)
        { ClientOrder(Identity,Rejected,{},0,TEXT("RequestConflict: this identity has different ground terms.")); }
        else if (bPending && Pending==Identity) { ClientOrder(Identity,Queued,{},0,TEXT("Move pending")); }
        else { ClientOrder(Identity,LastResult,ServerGoal,ServerAcceptance,LastReason); }
        return;
    }
    if (Identity.Order<=Received.Order || Identity.Epoch<Received.Epoch)
    {
        ClientOrder(Identity,Rejected,{},0,TEXT("StaleOrder: an older order or cancelled epoch cannot run again."));
        return;
    }
    if (!GetWorld()->GetAuthGameMode<ALDGameMode>()->CaptureIncomingTime(PendingNotBeforeMs,Error))
    { ClientOrder(Identity,Rejected,{},0,Error); return; }
    if (bPending) { ClientOrder(Pending,Stopped,{},0,TEXT("Superseded before activation.")); }
    Received=Pending=Identity;
    ReceivedSurface=PendingSurface=Surface;
    ReceivedSurfaceHint=PendingSurfaceHint=SurfaceHint;
    bReceivedStop=bPendingStop=false;
    bPending=true;
    ClientOrder(Identity,Queued,{},0,TEXT("Move pending"));
}

void ULDNavigationComponent::ServerStop_Implementation(FLDMoveIdentity Identity)
{
    FString Error;
    if (!AcceptEnvelope(Identity,Error)) { ClientOrder(Identity,Rejected,{},0,Error); return; }
    if (Received.Match!=Identity.Match || Received.Life!=Identity.Life)
    { Received=Server={Identity.Match,Identity.Life,0,0}; }
    if (Identity==Received)
    {
        if (!bReceivedStop) { ClientOrder(Identity,Rejected,{},0,TEXT("RequestConflict: this identity is not a Stop.")); }
        else { ClientOrder(Identity,bPending?Queued:LastResult,ServerGoal,ServerAcceptance,bPending?TEXT("Stop pending"):LastReason); }
        return;
    }
    if (Identity.Order<=Received.Order || Identity.Epoch<=Received.Epoch)
    { ClientOrder(Identity,Rejected,{},0,TEXT("StaleOrder: Stop must advance both order and cancellation epoch.")); return; }
    if (!GetWorld()->GetAuthGameMode<ALDGameMode>()->CaptureIncomingTime(PendingNotBeforeMs,Error))
    { ClientOrder(Identity,Rejected,{},0,Error); return; }
    if (bPending) { ClientOrder(Pending,Stopped,{},0,TEXT("Cancelled before activation.")); }
    Received=Pending=Identity;
    bReceivedStop=bPendingStop=true;
    bPending=true;
    ClientOrder(Identity,Queued,{},0,TEXT("Stop pending"));
}

void ULDNavigationComponent::ServerHeartbeat_Implementation(FLDMoveIdentity Identity, uint32 Sequence)
{
    FString Error;
    if (bServerActive && Identity==Server && Sequence>ServerHeartbeatSequence && AcceptEnvelope(Identity,Error))
    {
        ServerHeartbeatSequence=Sequence;
        LastHeartbeat=GetWorld()->GetAuthGameMode<ALDGameMode>()->GetAuthorityNowSeconds();
    }
}

bool ULDNavigationComponent::ProcessAuthorityOrders(const FLDGameplayWritePermit& Permit, FString& Error)
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    if (!Mode || Mode->ValidateWritePermit(Permit,ELDCommitPhase::Actions)!=ELDCommitReject::None)
    { Error=TEXT("Ground-order activation requires the current Actions permit."); return false; }
    if (!bPending || Mode->GetAuthorityNowMs()<PendingNotBeforeMs
        || (!bPendingStop && Mode->GetAuthorityNowSeconds()<NextQuery)) { return true; }
    Server=Pending;
    bPending=false;
    if (!AcceptEnvelope(Server,Error))
    {
        EndServer(Rejected,Error);
        Error.Reset();
        return true;
    }
    if (bPendingStop) { EndServer(Stopped,TEXT("Stopped")); return true; }
    NextQuery=Mode->GetAuthorityNowSeconds()+QueryInterval;
    ServerSurfaceHint=PendingSurfaceHint;
    if (!Plan(PendingSurface,ServerSurfaceHint,ServerPoints,ServerGoal,Error))
    {
        EndServer(Rejected,Error);
        Error.Reset();
        return true;
    }
    ServerAcceptance=40;
    ServerPointIndex=ServerPoints.Num()>1?1:0;
    bServerActive=true;
    bServerReplanned=false;
    LastResult=NavAccepted;
    LastReason=TEXT("Move accepted");
    LastHeartbeat=ServerLastProgress=Mode->GetAuthorityNowSeconds();
    ServerHeartbeatSequence=0;
    ServerBestProgress=AlongPath(ServerPoints,Controller()->GetPawn()->GetActorLocation());
    ClientOrder(Server,LastResult,ServerGoal,ServerAcceptance,LastReason);
    UE_LOG(LogTemp,Display,TEXT("LD_NAV_ACCEPT match=%u life=%u order=%u epoch=%u goal=%s"),Server.Match,Server.Life,Server.Order,Server.Epoch,*ServerGoal.ToString());
    return true;
}

bool ULDNavigationComponent::AdvanceAuthorityMotion(const FLDAuthorityStep& Step, const FLDCombatBodyRef& Ref,
    const FLDGameplayWritePermit& Permit, FString& Error)
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    ALDCharacter* Body=nullptr;
    if (!Mode || Mode->ValidateWritePermit(Permit,ELDCommitPhase::Motion)!=ELDCommitReject::None
        || Mode->ResolveBody(Step.Match,Ref,Body)!=ELDCommitReject::None || Body->GetController()!=Controller())
    { Error=TEXT("Ground motion requires the registered body's Motion permit."); return false; }
    if (bServerActive && !AuthorizesMove(Server)) { EndServer(Stopped,TEXT("Movement stopped: stale body, order or connection.")); }
    if (bServerActive && AtGoal(*Body,ServerGoal,ServerAcceptance)) { EndServer(Arrived,TEXT("Arrived")); }
    const FVector Input=bServerActive?Steering(ServerPoints,ServerPointIndex,*Body):FVector::ZeroVector;
    auto* Movement=CastChecked<ULDCharacterMovementComponent>(Body->GetCharacterMovement());
    if (!Movement->AdvanceAuthorityMovement(Step,Ref,Input,Permit,Error)) { return false; }
    if (!bServerActive) { return true; }
    if (AtGoal(*Body,ServerGoal,ServerAcceptance)) { EndServer(Arrived,TEXT("Arrived")); return true; }
    const double Progress=AlongPath(ServerPoints,Body->GetNavAgentLocation());
    if (Progress>=ServerBestProgress+10) { ServerBestProgress=Progress; ServerLastProgress=Step.EndServerSeconds; }
    if (Step.EndServerSeconds-ServerLastProgress>=NoProgressSeconds)
    {
        if (bServerReplanned) { EndServer(Stopped,TEXT("Path blocked")); }
        else
        {
            bServerReplanned=true;
            ServerLastProgress=Step.EndServerSeconds;
            ++Replans;
            FVector Goal;
            if (!Plan(ServerGoal,ServerSurfaceHint,ServerPoints,Goal,Error)) { EndServer(Stopped,Error); Error.Reset(); }
            else { ServerPointIndex=ServerPoints.Num()>1?1:0; ServerBestProgress=0; }
        }
    }
    return true;
}

void ULDNavigationComponent::EndServer(uint8 Result, const FString& Reason)
{
    bServerActive=false;
    LastResult=Result;
    LastReason=Reason;
    ServerPoints.Reset();
    HaltLocomotion(Cast<ALDCharacter>(Controller()->GetPawn()));
    ClientOrder(Server,Result,ServerGoal,ServerAcceptance,Reason);
    UE_LOG(LogTemp,Display,TEXT("LD_NAV_END order=%u result=%u reason=%s"),Server.Order,Result,*Reason);
}

void ULDNavigationComponent::ClientOrder_Implementation(FLDMoveIdentity Identity, uint8 Result,
    FVector_NetQuantize10 Goal, float Acceptance, const FString& Reason)
{
    auto* PC=Controller();
    if (!PC || Identity!=Local || Identity.Match!=PC->OwnerState.Match || Identity.Life!=PC->OwnerState.Life) { return; }
    if (AcknowledgedOrder!=Identity.Order)
    { AcknowledgedOrder=Identity.Order; AcknowledgementMs.Add((FPlatformTime::Seconds()-LocalSentAt)*1000); }
    Status=PC->IntentFeedback=Reason;
    if (Result==Queued) { return; }
    if (Result==NavAccepted)
    {
        if (AcceptedOrder==Identity.Order || !bLocalActive) { return; }
        RouteAcceptanceMs.Add((FPlatformTime::Seconds()-LocalSentAt)*1000);
        AcceptedOrder=Identity.Order;
        Destination=Goal;
        AcceptanceRadius=Acceptance;
        FVector Projected;
        FString Error;
        if (!Plan(Goal,LocalSurfaceHint,Points,Projected,Error))
        {
            Stop();
            Status=PC->IntentFeedback=Error;
            return;
        }
        PointIndex=Points.Num()>1?1:0;
        bFollowing=true;
        bReplanned=false;
        LastProgress=GetWorld()->GetTimeSeconds();
        BestProgress=AlongPath(Points,PC->GetPawn()->GetActorLocation());
    }
    else
    {
        bFollowing=bLocalActive=false;
        Points.Reset();
        if (!PC->HasAuthority()) { HaltLocomotion(Cast<ALDCharacter>(PC->GetPawn())); }
    }
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
    if (!Enabled()) { return; }
    auto* PC=Controller();
    if (!PC->IsLocalController()) { return; }
    auto* Body=Cast<ALDCharacter>(PC->GetPawn());
    const double Now=GetWorld()->GetTimeSeconds();
    if (SeenMatch!=PC->OwnerState.Match || SeenLife!=PC->OwnerState.Life) { ResetLife(); }
    if (Body!=LastPawn.Get())
    {
        LastPawn=Body;
        if (Body)
        {
            Body->GetCharacterMovement()->AddTickPrerequisiteComponent(this);
            bCameraInitialized=false;
            bFollowCamera=!PC->Controls || PC->Controls->bStartFollowing;
        }
    }
    if (!PC->CanWorldCommand()) { if (bLocalActive || bFollowing) { Stop(); } TickCamera(Delta); return; }
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
        else { LastHeartbeatSent=Now; ServerHeartbeat(Local,++HeartbeatSequence); }
    }
    TickCamera(Delta);
    if (!bFollowing || !Body || PC->HasAuthority()) { return; }
    if (AtGoal(*Body,Destination,AcceptanceRadius)) { bFollowing=false; HaltLocomotion(Body); return; }
    const FVector Input=Steering(Points,PointIndex,*Body);
    Body->AddMovementInput(Input,1.);
    Face(Input);
    const double Progress=AlongPath(Points,Body->GetNavAgentLocation());
    if (Progress>=BestProgress+10) { BestProgress=Progress; LastProgress=Now; }
    if (Now-LastProgress>=NoProgressSeconds)
    {
        if (bReplanned) { Stop(); Status=PC->IntentFeedback=TEXT("Path blocked"); }
        else
        {
            bReplanned=true;
            ++Replans;
            LastProgress=Now;
            BestProgress=0;
            FVector Projected;
            FString Error;
            if (Plan(Destination,LocalSurfaceHint,Points,Projected,Error)) { PointIndex=Points.Num()>1?1:0; }
            else { Stop(); Status=PC->IntentFeedback=Error; }
        }
    }
}

void ULDNavigationComponent::ClickMove()
{
    auto* PC=Controller();
    FHitResult Ground;
    if (PC->RootUI && PC->RootUI->PointerOverUI()) { return; }
    FGuid Observed;
    uint32 ObservationEpoch=0;
    if (PC->ObservedUnderPointer(Observed,ObservationEpoch))
    {
        Status=PC->IntentFeedback=TEXT("Unit attack/pursuit orders are not wired in this build; no ground order was substituted.");
        return;
    }
    if (!PC->GetHitResultUnderCursor(ECC_GameTraceChannel1,false,Ground)) { return; }
    Navigate(Ground.ImpactPoint);
}

void ULDNavigationComponent::PanTo(const FVector& Point) { CameraCentre=Point; bFollowCamera=false; bCameraInitialized=true; }
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
    if (!Body) { return; }
    if (!bCameraInitialized) { CameraCentre=Body->GetActorLocation(); bCameraInitialized=true; }
    if (PC->CanWorldCommand())
    {
        FVector2D Axis=PC->CameraPanAxis();
        if (bEdgePan && (!PC->HUDUI || !PC->HUDUI->Minimap || !PC->HUDUI->Minimap->IsDragging())
            && !FApp::IsUnattended() && (!PC->RootUI || !PC->RootUI->PointerOverUI()))
        {
            float X=0,Y=0; int32 Width=0,Height=0;
            PC->GetViewportSize(Width,Height);
            if (PC->GetMousePosition(X,Y) && X>=0 && Y>=0 && X<Width && Y<Height)
            { Axis.X+=(X>=Width-8?1:0)-(X<8?1:0); Axis.Y+=(Y<8?1:0)-(Y>=Height-8?1:0); }
        }
        PanCamera(Axis.GetClampedToMaxSize(1),Delta);
    }
    if (bFollowCamera || bRecenterHeld) { CameraCentre=Body->GetActorLocation(); }
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
    Body->CameraArm->SetWorldLocation(CameraCentre);
}
