#include "Region/LDB90Preview.h"
#include "Region/LDMapDefinition.h"
#include "Tests/LDB90TraversalProbe.h"
#include "Character/LDCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "DrawDebugHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Canvas.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "GameFramework/SpringArmComponent.h"
#include "UnrealClient.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformMisc.h"

ALDB90PreviewMode::ALDB90PreviewMode()
{
    DefaultPawnClass=ALDCharacter::StaticClass(); PlayerControllerClass=ALDB90PreviewController::StaticClass();
    HUDClass=ALDB90PreviewHUD::StaticClass();
}
void ALDB90PreviewMode::InitGame(const FString& MapName,const FString& Options,FString& Error)
{
    Super::InitGame(MapName,Options,Error);
    const auto* Definition=ULDMapDefinition::Get(GetWorld()); FString Why,Staged;
    if(!Definition||!Definition->Validate(Why)
       ||!FFileHelper::LoadFileToString(Staged,*(FPaths::ProjectContentDir()/TEXT("Lockdown/Data/V09/map_b90.json")))
       ||Staged.Replace(TEXT("\r\n"),TEXT("\n"))!=Definition->SourceJson)
    { Error=TEXT("B90 saved map/payload mismatch; rebuild the B90 assets. ")+Why; return; }
    if (UGameplayStatics::ParseOption(Options,TEXT("Profile"))!=TEXT("v09_full"))
    { Error=TEXT("B90 spatial preview requires a fresh v09_full map; use OpenB90.ps1"); }
    if (UGameplayStatics::HasOption(Options,TEXT("listen")) || IsRunningDedicatedServer())
    { Error=TEXT("B90 preview is standalone only; multiplayer full-match admission remains B92-B94"); }
    UE_LOG(LogTemp,Display,TEXT("LD_B90_PREVIEW spatial-only v09_full; no legacy match services/endowments spawned"));
}
ALDB90PreviewController::ALDB90PreviewController()
{ bShowMouseCursor=true; PrimaryActorTick.bCanEverTick=true; }
bool ALDB90PreviewController::Plan(const FVector& Goal)
{
    Path.Reset(); Waypoint=0;
    auto* Body=Cast<ALDCharacter>(GetPawn()); if (!Body) { return false; }
    if(!ULDMapDefinition::InBounds(Goal)) { Feedback=TEXT("Move outside the playable map"); return false; }
    Destination=Goal;
    if(FVector::DistSquared2D(Body->GetActorLocation(),Goal)<=35*35) { Feedback=TEXT("Arrived"); return true; }
    auto* Route=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),Body->GetNavAgentLocation(),Goal);
    if (!Route || !Route->IsValid() || Route->IsPartial()) { Feedback=TEXT("No complete native path"); return false; }
    Path=Route->PathPoints; Waypoint=1; Destination=Path.Last();
    ProgressAt=GetWorld()->GetTimeSeconds(); ProgressPosition=Body->GetActorLocation();
    Feedback=TEXT("Walking at 420 cm/s"); return true;
}
bool ALDB90PreviewController::NavigateTo(const FVector& Goal)
{ Stalls=0; return Plan(Goal); }
void ALDB90PreviewController::StopNavigation()
{
    Path.Reset(); Waypoint=0;
    if(auto* Body=Cast<ALDCharacter>(GetPawn())) { Body->ConsumeMovementInputVector(); Body->GetCharacterMovement()->StopMovementImmediately(); }
    Feedback=TEXT("Stopped");
}
void ALDB90PreviewController::PlayerTick(float Delta)
{
    Super::PlayerTick(Delta);
    auto* Body=Cast<ALDCharacter>(GetPawn()); const auto* D=ULDMapDefinition::Get(GetWorld()); if (!Body || !D) { return; }
    Body->GetCharacterMovement()->MaxWalkSpeed=420;
    if (WasInputKeyJustPressed(EKeys::F1)) { RoutesVisible=!RoutesVisible; }
    if (WasInputKeyJustPressed(EKeys::F2)) { PadsVisible=!PadsVisible; }
    if (WasInputKeyJustPressed(EKeys::F3)) { BandsVisible=!BandsVisible; }
    if (WasInputKeyJustPressed(EKeys::F4)) { ProxiesVisible=!ProxiesVisible; }
    if (WasInputKeyJustPressed(EKeys::MouseScrollUp)) { Body->Zoom(1); }
    if (WasInputKeyJustPressed(EKeys::MouseScrollDown)) { Body->Zoom(-1); }
    if (WasInputKeyJustPressed(EKeys::P)) { ConsoleCommand(TEXT("show Navigation")); }
    if (WasInputKeyJustPressed(EKeys::RightMouseButton))
    {
        FHitResult Hit;
        if (GetHitResultUnderCursorByChannel(UEngineTypes::ConvertToTraceType(ECC_GameTraceChannel1),false,Hit)) { NavigateTo(Hit.ImpactPoint); }
    }
    if (WasInputKeyJustPressed(EKeys::S)) { StopNavigation(); }
    const double Forward=(IsInputKeyDown(EKeys::W)?1.:0.)-(IsInputKeyDown(EKeys::Down)?1.:0.);
    const double Side=(IsInputKeyDown(EKeys::Right)?1.:0.)-(IsInputKeyDown(EKeys::Left)?1.:0.);
    if (Forward || Side)
    {
        Path.Reset(); const FVector Direction=FRotator(0,-45,0).RotateVector(FVector(Forward,Side,0)).GetSafeNormal();
        Body->AddMovementInput(Direction); Body->SetFacing(Direction.Rotation().Yaw);
    }
    if (Path.IsValidIndex(Waypoint))
    {
        const FVector Direction=Path[Waypoint]-Body->GetActorLocation();
        if (Direction.Size2D()<35)
        { ++Waypoint; if(!Path.IsValidIndex(Waypoint)) { StopNavigation(); Feedback=TEXT("Arrived"); } }
        else { Body->AddMovementInput(Direction.GetSafeNormal2D()); Body->SetFacing(Direction.Rotation().Yaw); }
        const double Now=GetWorld()->GetTimeSeconds();
        if(FVector::DistSquared2D(ProgressPosition,Body->GetActorLocation())>10*10)
        { ProgressPosition=Body->GetActorLocation(); ProgressAt=Now; }
        else if(IsNavigating()&&Now-ProgressAt>2)
        {
            ++NavigationReplans;
            if(++Stalls>2||!Plan(Destination)) { ++NavigationFailures; StopNavigation(); Feedback=TEXT("Path blocked; choose another approach"); }
        }
    }
    if(!TraversalStarted&&FParse::Param(FCommandLine::Get(),TEXT("LDB90Traversal")))
    { TraversalStarted=true; GetWorld()->SpawnActor<ALDB90TraversalProbe>(); }
    if (RoutesVisible) { for (const auto& R:D->Routes) { for (int32 I=1; I<R.Points.Num(); ++I)
    { DrawDebugLine(GetWorld(),R.Points[I-1]+FVector(0,0,80),R.Points[I]+FVector(0,0,80),FColor::Cyan,false,0,0,5); } } }
    if (PadsVisible) { for (const auto& P:D->POIs)
    {
        if (FVector::DistSquared2D(P.Position,Body->GetActorLocation())<7000.*7000.)
        { DrawDebugCircle(GetWorld(),P.Position+FVector(0,0,8),120,24,FColor::Yellow,false,0,0,3,FVector(1,0,0),FVector(0,1,0),false);
          DrawDebugString(GetWorld(),P.Position+FVector(0,0,120),P.Name,nullptr,FColor::White,0,true); }
    } }
    if (BandsVisible)
    {
        const FVector Origin=Body->GetActorLocation();
        for (int32 Y=-5;Y<=5;++Y) { for (int32 X=-5;X<=5;++X)
        {
            FVector P=Origin+FVector(X*400,Y*400,0); P.Z=D->Height(FVector2D(P));
            DrawDebugPoint(GetWorld(),P+FVector(0,0,10),12,P.Z>600?FColor::Red:P.Z>200?FColor::Yellow:FColor::Green,false,0);
        } }
    }
    if (ProxiesVisible) { for(TActorIterator<AStaticMeshActor> It(GetWorld());It;++It)
    {
        const bool Building=It->Tags.ContainsByPredicate([](FName Tag){return Tag.ToString().StartsWith(TEXT("building_"));});
        if(Building&&FVector::DistSquared2D(It->GetActorLocation(),Body->GetActorLocation())<7000.*7000.)
        { const FBox Bounds=It->GetComponentsBoundingBox(true); DrawDebugBox(GetWorld(),Bounds.GetCenter(),Bounds.GetExtent(),FColor::Magenta,false,0,0,3); }
    } }
    if(FParse::Param(FCommandLine::Get(),TEXT("LDB90Capture")))
    {
        const double Now=GetWorld()->GetTimeSeconds(); FString Output;
        FParse::Value(FCommandLine::Get(),TEXT("B90Output="),Output);
        if(CaptureState==0&&Now>2)
        { Body->SetActorLocation(D->Find(TEXT("heart"))->Position+FVector(0,0,92)); Body->GetCharacterMovement()->StopMovementImmediately();
          Body->CameraArm->TargetArmLength=18000; PadsVisible=true; CaptureState=1; }
        if(CaptureState==1&&Now>5)
        { FScreenshotRequest::RequestScreenshot(Output/TEXT("Capital.png"),true,false); CaptureState=2; }
        if(CaptureState==2&&Now>8) { FPlatformMisc::RequestExitWithStatus(false,0); }
    }
    if (FParse::Param(FCommandLine::Get(),TEXT("LDB90Probe")))
    {
        const double Now=GetWorld()->GetTimeSeconds();
        const bool BridgeEntry=FParse::Param(FCommandLine::Get(),TEXT("LDB90BridgeEntry"));
        const FVector Goal=D->Find(BridgeEntry?TEXT("east_bridge"):TEXT("heart"))->Position;
        if (!ProbeStarted && Now>2)
        {
            ProbeStarted=true;
            Body->SetActorLocation(BridgeEntry?FVector(60962.5,30537.5,470.941):D->Find(TEXT("antechamber"))->Position+FVector(0,0,92));
            Body->GetCharacterMovement()->StopMovementImmediately();
            ProbeOrigin=Body->GetActorLocation();
            if (!Plan(Goal)) { FPlatformMisc::RequestExitWithStatus(false,2); return; }
            ProbeStart=Now;
        }
        if (ProbeStart>=0)
        {
            ProbeLength+=FVector::Dist2D(ProbeOrigin,Body->GetActorLocation()); ProbeOrigin=Body->GetActorLocation();
            const bool Arrived=FVector::Dist2D(Body->GetActorLocation(),Goal)<60;
            if (Arrived || Now-ProbeStart>20)
            {
                const bool Passed=Arrived && Now-ProbeStart<=15;
                const FString Result=FString::Printf(TEXT("{\"stage\":\"B90\",\"case\":\"%s\",\"passed\":%s,\"walk_seconds\":%.4f,\"walked_cm\":%.3f,\"speed_cm_s\":420,\"setup\":\"labelled %s placement; ordinary native CharacterMovement without abilities\"}\n"),BridgeEntry?TEXT("B90-BridgeEntry"):TEXT("B90-T06"),Passed?TEXT("true"):TEXT("false"),Now-ProbeStart,ProbeLength,BridgeEntry?TEXT("Garden bridge edge"):TEXT("antechamber"));
                FString Output; FParse::Value(FCommandLine::Get(),TEXT("B90Output="),Output);
                if (!Output.IsEmpty()) { FFileHelper::SaveStringToFile(Result,*(Output/TEXT("Walk.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM); }
                UE_LOG(LogTemp,Display,TEXT("LD_B90_WALK %s"),*Result); ProbeStart=-1;
                FPlatformMisc::RequestExitWithStatus(false,Passed?0:3);
            }
        }
    }
}
void ALDB90PreviewHUD::DrawHUD()
{
    Super::DrawHUD(); const auto* PC=Cast<ALDB90PreviewController>(PlayerOwner); if (!PC) { return; }
    DrawRect(FLinearColor(0,0,0,.8f),12,12,1000,85);
    DrawText(TEXT("B90 FULL MAP / spatial review / combat, economy and population pending"),FLinearColor::White,22,20);
    DrawText(TEXT("RMB move | W/arrows walk | S stop | F1 routes | F2 POIs | F3 heights | F4 proxies | P navmesh"),FLinearColor::White,22,43);
    DrawText(PC->Feedback,FLinearColor::Yellow,22,66);
}
