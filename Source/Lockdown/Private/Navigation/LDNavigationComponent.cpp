#include "Navigation/LDNavigationComponent.h"
#include "Region/LDMapDefinition.h"
#include "Inventory/LDTradeDirector.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "NavigationPath.h"
#include "Vision/LDVisionDirector.h"
#include "Vision/LDWorldViewRelay.h"
#include "Region/LDRegionDefinition.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Misc/App.h"
#include "Misc/ScopeExit.h"
#include "Framework/Application/SlateApplication.h"
#include "UI/LDUIRoot.h"
#include "UI/LDControlSettings.h"
#include "UI/LDHUD.h"
#include "UI/LDMinimap.h"

namespace
{
    constexpr uint8 NavAccepted=1,Arrived=2,Stopped=3,Rejected=4;
    void HaltLocomotion(ALDCharacter* Body)
    {
        if(!Body){return;}Body->ConsumeMovementInputVector();auto* Move=Body->GetCharacterMovement();
        if(Move->IsFalling()){Move->Velocity.X=Move->Velocity.Y=0;}else if(!Move->HasRootMotionSources()){Move->StopMovementImmediately();}
    }
    bool Newer(const FLDMoveIdentity& A,const FLDMoveIdentity& B)
    {return A.Epoch>=B.Epoch&&A.Order>B.Order;}
    double AlongPath(const TArray<FVector>& Path,const FVector& Position)
    {
        double Length=0,BestDistance=DBL_MAX,Progress=0;
        for(int32 I=1;I<Path.Num();++I)
        {
            const FVector A=Path[I-1],D=Path[I]-A;const double Segment=D.Size2D();
            if(Segment<UE_SMALL_NUMBER){continue;}
            const double T=FMath::Clamp(((Position.X-A.X)*D.X+(Position.Y-A.Y)*D.Y)/(Segment*Segment),0.,1.);
            const double Distance=FVector::DistSquared2D(Position,A+D*T);
            if(Distance<BestDistance){BestDistance=Distance;Progress=Length+T*Segment;}
            Length+=Segment;
        }
        return Progress;
    }
}

ULDNavigationComponent::ULDNavigationComponent()
{
    SetIsReplicatedByDefault(true);
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.TickGroup=TG_PrePhysics;
}
ALDPlayerController* ULDNavigationComponent::Controller() const {return Cast<ALDPlayerController>(GetOwner());}
bool ULDNavigationComponent::Enabled() const
{const auto* PC=Controller();const auto* G=GetWorld()?GetWorld()->GetGameState<ALDGameState>():nullptr;return PC&&G&&G->bUIControlProfile;}

bool ULDNavigationComponent::Plan(const FVector& Surface,TArray<FVector>& Out,FVector& Projected)
{
    const double Started=FPlatformTime::Seconds();++QueryCount;
    ON_SCOPE_EXIT {QueryMilliseconds+=(FPlatformTime::Seconds()-Started)*1000;};
    const auto* PC=Controller();const auto* Body=PC?Cast<ALDCharacter>(PC->GetPawn()):nullptr;
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    const auto* Public=GetWorld()->GetGameState<ALDGameState>();
    const bool InWorld=Public&&Public->bEnvironmentProfile?ULDMapDefinition::InBounds(Surface):LDVision::InWorld(Surface,LDRegion::Definition(GetWorld())?LDRegion::Definition(GetWorld())->Grid:FLDVisionGrid());
    if(!Body||!Nav||Surface.ContainsNaN()||!InWorld){return false;}
    FHitResult Hit;
    if(!GetWorld()->LineTraceSingleByChannel(Hit,Surface+FVector(0,0,25),Surface-FVector(0,0,50),ECC_GameTraceChannel1)
        ||Hit.ImpactNormal.Z<FMath::Cos(FMath::DegreesToRadians(40.))||FMath::Abs(Hit.ImpactPoint.Z-Surface.Z)>50){return false;}
    FNavLocation End;
    const auto& Props=Body->GetCharacterMovement()->GetNavAgentPropertiesRef();
    if(!Nav->ProjectPointToNavigation(Hit.ImpactPoint,End,FVector(100,100,50),&Props)
        ||FVector::DistSquared2D(End.Location,Surface)>100*100||FMath::Abs(End.Location.Z-Surface.Z)>50){return false;}
    const ANavigationData* Data=Nav->GetNavDataForProps(Props,Body->GetNavAgentLocation());
    if(!Data){return false;}
    FPathFindingQuery Query(PC,*Data,Body->GetNavAgentLocation(),End.Location);
    Query.SetAllowPartialPaths(false);
    const auto Path=Nav->FindPathSync(Props,Query);
    if(!Path.IsSuccessful()||!Path.Path.IsValid()||Path.Path->IsPartial()||Path.Path->GetPathPoints().IsEmpty()){return false;}
    Out.Reset();for(const auto& P:Path.Path->GetPathPoints()){Out.Add(P.Location);}
    Projected=End.Location;return true;
}

void ULDNavigationComponent::Navigate(const FVector& Surface,FGuid Observed,uint8 Site)
{
    auto* PC=Controller();
    if(!Enabled()||!PC->CanWorldCommand()||!PC->IsLocalController()){return;}
    PC->CancelInterruptibleHolds();
    Local={PC->OwnerState.Match,PC->OwnerState.Life,++Serial,Epoch};
    bFollowing=false;bLocalActive=true;Points.Reset();
    HaltLocomotion(Cast<ALDCharacter>(PC->GetPawn()));
    Status=TEXT("Move pending");PC->IntentFeedback=Status;
    LocalSentAt=FPlatformTime::Seconds();
    ServerOrder(Local,Surface,Observed,Site);
}
void ULDNavigationComponent::Stop()
{
    auto* PC=Controller();if(!Enabled()||!PC->IsLocalController()){return;}
    Local={PC->OwnerState.Match,PC->OwnerState.Life,++Serial,++Epoch};
    bFollowing=bLocalActive=false;Points.Reset();Status=TEXT("Stopped");
    HaltLocomotion(Cast<ALDCharacter>(PC->GetPawn()));
    ServerStop(Local);
}
void ULDNavigationComponent::ResetLife()
{
    bFollowing=bLocalActive=bServerActive=bPending=false;Points.Reset();Approach.Invalidate();
    bCameraInitialized=false;bFollowCamera=true;bRecenterHeld=false;
    // Serial and cancellation epoch are never rewound by an old-life receipt.
    if(auto* PC=Controller()){SeenMatch=PC->OwnerState.Match;SeenLife=PC->OwnerState.Life;Local={SeenMatch,SeenLife,++Serial,++Epoch};}
}
bool ULDNavigationComponent::AuthorizesMove(const FLDMoveIdentity& Identity) const
{return !Enabled()||(bServerActive&&Identity==Server&&GetWorld()->GetTimeSeconds()-LastHeartbeat<=2.);}

void ULDNavigationComponent::ServerOrder_Implementation(FLDMoveIdentity Identity,FVector_NetQuantize10 Surface,FGuid Observed,uint8 Site)
{
    auto* PC=Controller();auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=M?M->StateFor(PC):nullptr;
    if(!Enabled()||!O||!O->bActive||!M->IsPlaying()||Identity.Match!=O->Match||Identity.Life!=O->Life||!Identity.Order||Surface.ContainsNaN()||Site>8)
    {ClientOrder(Identity,Rejected,{},40,TEXT("Invalid order or stale life"));return;}
    if(Server.Match!=O->Match||Server.Life!=O->Life){bServerActive=bPending=false;Server={O->Match,O->Life,0,0};}
    if(Identity==Server){if(!bPending){ClientOrder(Server,LastResult,ServerGoal,ServerAcceptance,LastResult==NavAccepted?TEXT("Move accepted"):TEXT("Order finished"));}return;}
    if(!Newer(Identity,Server)){ClientOrder(Identity,Rejected,{},40,TEXT("Expired move order"));return;}
    Server=Identity;PendingSurface=Surface;Approach=Observed;PendingSite=Site;
    bPending=true;bServerActive=false;LastHeartbeat=GetWorld()->GetTimeSeconds();ServerHeartbeatSequence=0;
    // Tick evaluates only the newest request, at most ten routes per second.
    // An approach may test up to twelve bounded ring candidates.
}
void ULDNavigationComponent::ServerStop_Implementation(FLDMoveIdentity Identity)
{
    auto* PC=Controller();auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=M?M->StateFor(PC):nullptr;
    if(!Enabled()||!O||Identity.Match!=O->Match||Identity.Life!=O->Life||!Identity.Order){return;}
    if(Server.Match!=O->Match||Server.Life!=O->Life){Server={O->Match,O->Life,0,0};}
    if(Identity==Server){ClientOrder(Server,LastResult,ServerGoal,ServerAcceptance,TEXT("Stopped"));return;}
    if(!Newer(Identity,Server)||Identity.Epoch<=Server.Epoch){return;}
    Server=Identity;EndServer(Stopped,TEXT("Stopped"));
}
void ULDNavigationComponent::ServerHeartbeat_Implementation(FLDMoveIdentity Identity,uint32 Sequence)
{
    if((bPending||bServerActive)&&Identity==Server&&Sequence>ServerHeartbeatSequence)
    {ServerHeartbeatSequence=Sequence;LastHeartbeat=GetWorld()->GetTimeSeconds();}
}

bool ULDNavigationComponent::ResolveOrder(FVector& Goal,float& Acceptance,TArray<FVector>& Path)
{
    auto* PC=Controller();const APawn* Body=PC?PC->GetPawn():nullptr;
    if(!Body){return false;}
    FVector Target=PendingSurface;Acceptance=40;
    if(Approach.IsValid())
    {
        const auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* View=M&&M->Vision?M->Vision->AuthorityViewFor(PC):nullptr;
        const auto* E=View?View->Entities.FindByPredicate([&](const FLDObservedEntity& X){return X.Id==Approach;}):nullptr;
        if(!E){return false;}
        Target=E->Position;
        Acceptance=LDVision::PickupKind(E->Kind)?64:100;
    }
    else if(PendingSite)
    {
        if(PendingSite!=1&&PendingSite!=2&&PendingSite!=3&&PendingSite!=6&&PendingSite!=7&&PendingSite!=8){return false;}
        if(PendingSite>3&&!LDRegion::Definition(GetWorld())){return false;}
        Target=LDRegion::Site(GetWorld(),PendingSite);Acceptance=100;
    }
    if(!Approach.IsValid()&&!PendingSite){return Plan(Target,Path,Goal);}
    if(FVector::DistSquared2D(Body->GetActorLocation(),Target)<=FMath::Square(Acceptance))
    {Goal=CastChecked<ALDCharacter>(Body)->GetNavAgentLocation();return true;}
    // Approach a reachable point outside the observed body's collision. Never query its future position.
    const FVector From=(Body->GetActorLocation()-Target).GetSafeNormal2D();
    const double Radius=FMath::Max(60.f,Acceptance-12);
    for(int32 I=0;I<12;++I)
    {
        const int32 Offset=I%2?-((I+1)/2):I/2;
        FVector Candidate=Target+From.RotateAngleAxis(Offset*30.,FVector::UpVector)*Radius;
        FHitResult Ground;
        if(!GetWorld()->LineTraceSingleByChannel(Ground,Candidate+FVector(0,0,30),Candidate-FVector(0,0,200),ECC_GameTraceChannel1)){continue;}
        Candidate=Ground.ImpactPoint;
        if(Plan(Candidate,Path,Goal))
        {
            // Endpoint tolerance must fit inside the requested approach range, including pickups.
            const float Tolerance=FMath::Min(8.f,float(Acceptance-FVector::Dist2D(Goal,Target)-1));
            if(Tolerance>=1){Acceptance=Tolerance;return true;}
        }
    }
    return false;
}
void ULDNavigationComponent::ProcessOrder()
{
    bPending=false;NextQuery=GetWorld()->GetTimeSeconds()+.1;
    if(!ResolveOrder(ServerGoal,ServerAcceptance,ServerPoints)){EndServer(Rejected,TEXT("No walkable route"));return;}
    if(auto* TradeMode=GetWorld()->GetAuthGameMode<ALDGameMode>();TradeMode&&TradeMode->Trade){TradeMode->Trade->CancelFor(Controller());}bServerActive=true;LastResult=NavAccepted;bServerReplanned=false;
    ServerProgressAnchor=Controller()->GetPawn()->GetActorLocation();ServerLastProgress=GetWorld()->GetTimeSeconds();
    ServerBestProgress=AlongPath(ServerPoints,ServerProgressAnchor);
    ClientOrder(Server,NavAccepted,ServerGoal,ServerAcceptance,TEXT("Move accepted"));
    UE_LOG(LogTemp,Display,TEXT("LD_NAV_ACCEPT match=%u life=%u order=%u epoch=%u"),Server.Match,Server.Life,Server.Order,Server.Epoch);
}
void ULDNavigationComponent::EndServer(uint8 Result,const FString& Reason)
{
    bPending=bServerActive=false;LastResult=Result;Approach.Invalidate();
    HaltLocomotion(Cast<ALDCharacter>(Controller()->GetPawn()));
    ClientOrder(Server,Result,ServerGoal,ServerAcceptance,Reason);
}
void ULDNavigationComponent::ClientOrder_Implementation(FLDMoveIdentity Identity,uint8 Result,FVector_NetQuantize10 Goal,float Acceptance,const FString& Reason)
{
    auto* PC=Controller();
    if(Identity!=Local||Identity.Match!=PC->OwnerState.Match||Identity.Life!=PC->OwnerState.Life){return;}
    if((Result==0||Result==NavAccepted||Result==Rejected)&&AcknowledgedOrder!=Identity.Order)
    {AcknowledgedOrder=Identity.Order;AcknowledgementMs.Add((FPlatformTime::Seconds()-LocalSentAt)*1000);}
    if(Result==0){Status=Reason;PC->IntentFeedback=Reason;return;}
    if(Result==NavAccepted)
    {
        if(AcceptedOrder==Identity.Order||!bLocalActive){return;}
        RouteAcceptanceMs.Add((FPlatformTime::Seconds()-LocalSentAt)*1000);
        AcceptedOrder=Identity.Order;Destination=Goal;AcceptanceRadius=Acceptance;PointIndex=1;
        FVector Projected;
        if(!Plan(Goal,Points,Projected)){Status=TEXT("No walkable route");Stop();PC->IntentFeedback=TEXT("No walkable route");return;}
        bFollowing=true;bReplanned=false;ProgressAnchor=PC->GetPawn()->GetActorLocation();LastProgress=GetWorld()->GetTimeSeconds();
        BestProgress=AlongPath(Points,ProgressAnchor);
    }
    else {bFollowing=bLocalActive=false;Points.Reset();}
    Status=Reason;PC->IntentFeedback=Reason;
}
void ULDNavigationComponent::Face(const FVector& Direction)
{
    auto* PC=Controller();auto* Body=Cast<ALDCharacter>(PC->GetPawn());
    if(Body&&!PC->HasPendingAim()&&(PC->OwnerState.ActionCue&15)==0&&!Direction.IsNearlyZero()){Body->SetFacing(Direction.Rotation().Yaw);}
}
void ULDNavigationComponent::TickComponent(float Delta,ELevelTick TickType,FActorComponentTickFunction* Function)
{
    Super::TickComponent(Delta,TickType,Function);
    if(!Enabled()){return;}
    auto* PC=Controller();auto* Body=Cast<ALDCharacter>(PC->GetPawn());const double Now=GetWorld()->GetTimeSeconds();
    if(PC->HasAuthority())
    {
        const auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=M?const_cast<ALDGameMode*>(M)->StateFor(PC):nullptr;
        if((bServerActive||bPending)&&(!O||!Body||!O->bActive||!M->IsPlaying()||O->Life!=Server.Life||O->Match!=Server.Match))
        {EndServer(Stopped,TEXT("Movement cancelled"));}
        if((bServerActive||bPending)&&Now-LastHeartbeat>2){EndServer(Stopped,TEXT("Movement stopped: connection inactive"));}
        if(bPending&&Now>=NextQuery){ProcessOrder();}
        else if(bPending&&PendingAcknowledgedOrder!=Server.Order)
        {PendingAcknowledgedOrder=Server.Order;ClientOrder(Server,0,{},40,TEXT("Move pending"));}
        if(bServerActive&&Body)
        {
            if(Approach.IsValid()&&(!M->Vision||!M->Vision->ResolveObserved(PC,Approach))) {EndServer(Stopped,TEXT("Target no longer visible"));}
            else if(!Body->GetCharacterMovement()->HasRootMotionSources()&&FVector::DistSquared2D(Body->GetActorLocation(),ServerGoal)<=FMath::Square(ServerAcceptance)&&FMath::Abs(Body->GetNavAgentLocation().Z-ServerGoal.Z)<=50)
            {EndServer(Arrived,TEXT("Arrived"));}
            else
            {
                // Server uses its own body pose, never a client-reported arrival/progress claim.
                const double Progress=AlongPath(ServerPoints,Body->GetActorLocation());
                if(Progress>=ServerBestProgress+10){ServerBestProgress=Progress;ServerLastProgress=Now;}
                if(Now-ServerLastProgress>=2)
                {
                    if(bServerReplanned){EndServer(Stopped,TEXT("Path blocked"));}
                    else {bServerReplanned=true;ServerLastProgress=Now;++Replans;FVector G;if(!Plan(ServerGoal,ServerPoints,G)){EndServer(Stopped,TEXT("Path blocked"));}ServerBestProgress=0;}
                }
            }
        }
    }
    if(!PC->IsLocalController()){return;}
    if(SeenMatch!=PC->OwnerState.Match||SeenLife!=PC->OwnerState.Life){ResetLife();}
    if(Body!=LastPawn.Get())
    {LastPawn=Body;if(Body){Body->GetCharacterMovement()->AddTickPrerequisiteComponent(this);bCameraInitialized=false;bFollowCamera=PC->OwnerState.Life>1||!PC->Controls||PC->Controls->bStartFollowing;}}
    if(!PC->CanWorldCommand()) {if(bLocalActive||bFollowing){Stop();}TickCamera(Delta);return;}
    if(!FApp::IsUnattended()&&FSlateApplication::IsInitialized()&&!FSlateApplication::Get().IsActive())
    {if(bHadFocus){PC->FlushPressedKeys();}bHadFocus=false;return;}
    bHadFocus=true;
    if(bLocalActive&&Now-LastHeartbeatSent>=.5){LastHeartbeatSent=Now;ServerHeartbeat(Local,++HeartbeatSequence);}
    TickCamera(Delta);
    if(!bFollowing||!Body){return;}
    if(!Body->GetCharacterMovement()->HasRootMotionSources()&&FVector::DistSquared2D(Body->GetActorLocation(),Destination)<=FMath::Square(AcceptanceRadius)&&FMath::Abs(Body->GetNavAgentLocation().Z-Destination.Z)<=50)
    {bFollowing=false;Body->ConsumeMovementInputVector();Body->GetCharacterMovement()->StopMovementImmediately();return;}
    if(PointIndex>=Points.Num()){bFollowing=false;return;}
    if(FVector::DistSquared2D(Body->GetActorLocation(),Points[PointIndex])<=12*12&&PointIndex<Points.Num()-1){++PointIndex;}
    const FVector Direction=(Points[PointIndex]-Body->GetActorLocation()).GetSafeNormal2D();
    const double Remaining=FVector::Dist2D(Points[PointIndex],Body->GetActorLocation());
    const double Scale=FMath::Clamp(Remaining/FMath::Max(1.,Body->GetCharacterMovement()->GetMaxSpeed()*.2),.08,1.);
    Body->AddMovementInput(Direction,float(Scale));Face(Direction);
    const double Progress=AlongPath(Points,Body->GetActorLocation());
    if(Progress>=BestProgress+10){BestProgress=Progress;LastProgress=Now;}
    if(Now-LastProgress>=2)
    {
        if(bReplanned){Stop();Status=PC->IntentFeedback=TEXT("Path blocked");}
        else {bReplanned=true;++Replans;LastProgress=Now;BestProgress=0;FVector G;if(Plan(Destination,Points,G)){PointIndex=1;}else{Stop();Status=PC->IntentFeedback=TEXT("Path blocked");}}
    }
}

void ULDNavigationComponent::ClickMove()
{
    auto* PC=Controller();FHitResult Ground;
    if(PC->RootUI&&PC->RootUI->PointerOverUI()){return;}
    if(!PC->GetHitResultUnderCursor(ECC_GameTraceChannel1,false,Ground)){return;}
    // Share the filtered screen-space hit test with selection; ground under a
    // projected head need not be near that body's feet in an angled camera.
    const FGuid Observed=PC->ObservedUnderPointer();
    if(Observed.IsValid()){Navigate(Ground.ImpactPoint,Observed);return;}
    for(uint8 Site:{uint8(1),uint8(2),uint8(3),uint8(6),uint8(7),uint8(8)})
    {if(Site>3&&!LDRegion::Definition(GetWorld())){continue;}if(FVector::DistSquared2D(Ground.ImpactPoint,LDRegion::Site(GetWorld(),Site))<40*40){Navigate(Ground.ImpactPoint,FGuid(),Site);return;}}
    Navigate(Ground.ImpactPoint);
}
void ULDNavigationComponent::PanTo(const FVector& Point)
{CameraCentre=Point;bFollowCamera=false;bCameraInitialized=true;}
void ULDNavigationComponent::PanCamera(const FVector2D& Axis,float Delta)
{if(!Axis.IsNearlyZero()){const FVector World=FRotator(0,-45,0).RotateVector(FVector(Axis.Y,Axis.X,0));CameraCentre+=World*1800*Delta;bFollowCamera=false;}}
void ULDNavigationComponent::Recenter(bool Held) {bRecenterHeld=Held;}
void ULDNavigationComponent::ToggleFollow() {bFollowCamera=!bFollowCamera;}
void ULDNavigationComponent::TickCamera(float Delta)
{
    auto* PC=Controller();auto* B=Cast<ALDCharacter>(PC->GetPawn());if(!B){return;}
    if(!bCameraInitialized){CameraCentre=B->GetActorLocation();bCameraInitialized=true;}
    if(PC->CanWorldCommand())
    {
        FVector2D Axis=PC->CameraPanAxis();
        if(bEdgePan&&!PC->HasArmedAction()&&(!PC->HUDUI||!PC->HUDUI->Minimap||!PC->HUDUI->Minimap->IsDragging())&&!FApp::IsUnattended()&&(!PC->RootUI||!PC->RootUI->PointerOverUI()))
        {float X=0,Y=0;int32 W=0,H=0;PC->GetViewportSize(W,H);if(PC->GetMousePosition(X,Y)&&X>=0&&Y>=0&&X<W&&Y<H){Axis.X+=(X>=W-8?1:0)-(X<8?1:0);Axis.Y+=(Y<8?1:0)-(Y>=H-8?1:0);}}
        PanCamera(Axis.GetClampedToMaxSize(1),Delta);
    }
    if(bFollowCamera||bRecenterHeld){CameraCentre=B->GetActorLocation();}
    const auto Grid=LDRegion::Definition(GetWorld())?LDRegion::Definition(GetWorld())->Grid:FLDVisionGrid();
    if(const auto* Public=GetWorld()->GetGameState<ALDGameState>();Public&&Public->bEnvironmentProfile)
    {CameraCentre.X=FMath::Clamp(CameraCentre.X,-118000.,118000.);CameraCentre.Y=FMath::Clamp(CameraCentre.Y,-118000.,118000.);}
    else {CameraCentre.X=FMath::Clamp(CameraCentre.X,double(Grid.Min.X),double(Grid.Min.X+60*Grid.CellSize));
    CameraCentre.Y=FMath::Clamp(CameraCentre.Y,double(Grid.Min.Y),double(Grid.Min.Y+40*Grid.CellSize));}
    B->CameraArm->SetWorldLocation(CameraCentre);
}
