#include "Tests/LDB90TraversalProbe.h"
#include "Region/LDB90Preview.h"
#include "Region/LDMapDefinition.h"
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"

ALDB90TraversalProbe::ALDB90TraversalProbe()
{ PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickGroup=TG_PostPhysics; bReplicates=false; }

bool ALDB90TraversalProbe::Initialize()
{
    Controller=Cast<ALDB90PreviewController>(GetWorld()->GetFirstPlayerController());
    Body=Controller?Cast<ALDCharacter>(Controller->GetPawn()):nullptr;
    Definition=ULDMapDefinition::Get(GetWorld());
    if(!Controller||!Body||!Definition||GetNetMode()!=NM_Standalone) { return false; }
    FParse::Value(FCommandLine::Get(),TEXT("B90Output="),Output);
    FParse::Value(FCommandLine::Get(),TEXT("B90TraversalLane="),Lane);
    FParse::Value(FCommandLine::Get(),TEXT("B90Trip="),Filter);
    if(Lane.IsEmpty()) { Lane=TEXT("All"); }
    if(Output.IsEmpty()||(Lane!=TEXT("All")&&Lane!=TEXT("Routes")&&Lane!=TEXT("Bounds"))) { return false; }
    IFileManager::Get().MakeDirectory(*Output,true);
    Controller->PadsVisible=Controller->RoutesVisible=Controller->BandsVisible=Controller->ProxiesVisible=false;
    NavigationContract=MakeShared<FJsonObject>();
    const bool Arrived=Controller->NavigateTo(Body->GetActorLocation())&&!Controller->IsNavigating();
    const bool Outside=!Controller->NavigateTo(FVector(118001,0,0));
    const auto* Shell=Definition->Buildings.FindByPredicate([](const auto& B){return !B.Enterable;});
    const bool Closed=Shell&&!Controller->NavigateTo(Shell->Position);
    NavigationContract->SetBoolField(TEXT("coincident_arrival"),Arrived);
    NavigationContract->SetBoolField(TEXT("outside_rejected"),Outside);
    NavigationContract->SetBoolField(TEXT("closed_shell_rejected"),Closed);
    if(!Arrived||!Outside||!Closed) { ++Failures; }
    Controller->StopNavigation();
    TSharedPtr<FJsonObject> Source;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Definition->SourceJson),Source)) { return false; }
    for(const auto& V:Source->GetArrayField(TEXT("travel")))
    { const auto O=V->AsObject(); Trips.Add({FName(*O->GetStringField(TEXT("start"))),FName(*O->GetStringField(TEXT("end"))),TEXT("prescribed")}); }
    for(const auto& City:Definition->Settlements)
    {
        if(City.Id==TEXT("capital")||City.Radius<10000) { continue; }
        TArray<const FLDMapPoint*> Towns;
        for(const auto& Town:Definition->Settlements) { if(Town.Radius<10000) { Towns.Add(&Town); } }
        Towns.Sort([&](const auto& A,const auto& B)
        { const double DA=FVector::DistSquared2D(A.Position,City.Position), DB=FVector::DistSquared2D(B.Position,City.Position);
          return DA==DB?A.Id.LexicalLess(B.Id):DA<DB; });
        for(int32 I=0;I<2;++I) { Trips.Add({City.Id,Towns[I]->Id,TEXT("city_nearby_town")}); }
    }
    Trips.Add({TEXT("capital"),TEXT("highfort"),TEXT("settlement_arrival")});
    Trips.Add({TEXT("capital_ford"),TEXT("heart"),TEXT("western_heart_approach")});
    Trips.Add({TEXT("heart_dry_flank"),TEXT("heart"),TEXT("dry_heart_approach")});
    if(!Filter.IsEmpty()) { Trips.RemoveAll([&](const FTrip& T){return T.From.ToString()+TEXT("__")+T.To.ToString()!=Filter;}); }
    if(Lane==TEXT("Bounds")) { Trips.Reset(); }
    if(Lane!=TEXT("Routes"))
    {
        for(int32 Y=-1;Y<=1;++Y) { for(int32 X=-1;X<=1;++X) { if(X||Y)
        { Boundaries.Add({FString::Printf(TEXT("border_%d_%d"),X,Y),FVector(X*117500,Y*117500,0),FVector(X,Y,0).GetSafeNormal2D()}); } } }
        for(const auto& P:Definition->Plateaus)
        { Boundaries.Add({P.Id.ToString(),P.Position+FVector(P.Radius+1500,0,0),FVector(1,0,0)}); }
    }
    if(Trips.IsEmpty()&&Boundaries.IsEmpty()) { return false; }
    Trace=TEXT("trip,elapsed_s,x_cm,y_cm,z_cm,speed_cm_s,walked_cm,nearest_poi,distance_to_poi_cm,static_water_geometry\n");
    BoundaryTrace=TEXT("id,action,phase,x_cm,y_cm,z_cm,safe_endpoint\n");
    WallStart=FPlatformTime::Seconds();
    BeginTrip(); return true;
}

bool ALDB90TraversalProbe::Place(const FVector& Point, bool Project)
{
    Controller->StopNavigation(); Body->GetCharacterMovement()->RemoveRootMotionSource(TEXT("LD_AuthorityDash"));
    FVector Position=Point;
    if(Project)
    {
        auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()); FNavLocation Target;
        if(!Nav||!Nav->ProjectPointToNavigation(Point,Target,FVector(60,60,1000))) { return false; }
        Position=Target.Location;
    }
    FHitResult Ground; FCollisionQueryParams Q(SCENE_QUERY_STAT(B90FixtureFloor),false,Body);
    const FVector Top=Project?Position+FVector(0,0,25):FVector(Position.X,Position.Y,2000);
    if(!GetWorld()->LineTraceSingleByChannel(Ground,Top,Project?Position-FVector(0,0,100):FVector(Position.X,Position.Y,-200),ECC_GameTraceChannel1,Q)) { return false; }
    if(!Body->SetActorLocation(Ground.ImpactPoint+FVector(0,0,92),false,nullptr,ETeleportType::TeleportPhysics)) { return false; }
    Body->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
    PhaseAt=GetWorld()->GetTimeSeconds(); return true;
}

void ALDB90TraversalProbe::BeginTrip()
{
    if(TripIndex>=Trips.Num()) { BeginBoundary(); return; }
    Phase=1; PlannedLength=Length=PeakSpeed=FallingSeconds=EmptySeconds=MaxEmptySeconds=EmptyDistance=0;
    Samples=WaterSamples=0; StartReplans=Controller->NavigationReplans;
    EmptyStart=LongestEmptyStart=LongestEmptyEnd=FVector::ZeroVector;
    if(!Place(Definition->NavigationTarget(Trips[TripIndex].From),true)) { EndTrip(false,TEXT("Start pad has no native floor")); }
}

void ALDB90TraversalProbe::SampleTravel(float Delta, bool Force)
{
    const FVector P=Body->GetActorLocation(); const double Distance=FVector::Dist2D(P,Last);
    Length+=Distance; Last=P; PeakSpeed=FMath::Max(PeakSpeed,double(Body->GetVelocity().Size2D()));
    if(Body->GetCharacterMovement()->IsFalling()) { FallingSeconds+=Delta; }
    const FLDMapPoint* Nearest=nullptr; double Best=DBL_MAX;
    for(const auto& POI:Definition->POIs)
    { const double D=FVector::DistSquared2D(P,POI.Position); if(D<Best) { Best=D; Nearest=&POI; } }
    if(Best>5000.*5000.)
    {
        if(EmptySeconds==0) { EmptyStart=P; }
        EmptySeconds+=Delta; EmptyDistance+=Distance;
        if(EmptySeconds>MaxEmptySeconds) { MaxEmptySeconds=EmptySeconds; LongestEmptyStart=EmptyStart; LongestEmptyEnd=P; }
    }
    else { EmptySeconds=0; }
    const double Now=GetWorld()->GetTimeSeconds();
    if(Force||Now>=NextSample)
    {
        ++Samples; const bool Water=Definition->RiverAt(P-FVector(0,0,92)); if(Water) { ++WaterSamples; }
        Trace+=FString::Printf(TEXT("%s__%s,%.4f,%.3f,%.3f,%.3f,%.3f,%.3f,%s,%.3f,%s\n"),
            *Trips[TripIndex].From.ToString(),*Trips[TripIndex].To.ToString(),Now-WalkAt,P.X,P.Y,P.Z,Body->GetVelocity().Size2D(),Length,
            Nearest?*Nearest->Id.ToString():TEXT("none"),FMath::Sqrt(Best),Water?TEXT("true"):TEXT("false"));
        NextSample=Now+1;
    }
}

void ALDB90TraversalProbe::EndTrip(bool Passed,const FString& Reason)
{
    Controller->StopNavigation();
    auto Row=MakeShared<FJsonObject>(); const auto& T=Trips[TripIndex];
    Row->SetStringField(TEXT("from"),T.From.ToString()); Row->SetStringField(TEXT("to"),T.To.ToString()); Row->SetStringField(TEXT("kind"),T.Kind);
    Row->SetBoolField(TEXT("passed"),Passed); Row->SetStringField(TEXT("reason"),Reason);
    Row->SetNumberField(TEXT("native_path_cm"),PlannedLength); Row->SetNumberField(TEXT("path_estimate_s"),PlannedLength/420);
    Row->SetNumberField(TEXT("walked_cm"),Length); Row->SetNumberField(TEXT("walk_seconds"),Phase==2?GetWorld()->GetTimeSeconds()-WalkAt:0);
    Row->SetNumberField(TEXT("peak_speed_cm_s"),PeakSpeed); Row->SetNumberField(TEXT("trace_samples"),Samples);
    Row->SetNumberField(TEXT("replans"),Controller->NavigationReplans-StartReplans); Row->SetNumberField(TEXT("falling_seconds"),FallingSeconds);
    Row->SetNumberField(TEXT("longest_empty_s"),MaxEmptySeconds); Row->SetNumberField(TEXT("empty_distance_cm"),EmptyDistance);
    Row->SetStringField(TEXT("longest_empty_start_cm"),LongestEmptyStart.ToString()); Row->SetStringField(TEXT("longest_empty_end_cm"),LongestEmptyEnd.ToString());
    Row->SetNumberField(TEXT("static_water_samples"),WaterSamples); Row->SetStringField(TEXT("end_cm"),Body->GetActorLocation().ToString());
    Results.Add(MakeShared<FJsonValueObject>(Row)); if(!Passed) { ++Failures; }
    UE_LOG(LogTemp,Display,TEXT("LD_B90_TRAVEL %s -> %s passed=%d seconds=%.3f reason=%s"),*T.From.ToString(),*T.To.ToString(),Passed,
        Row->GetNumberField(TEXT("walk_seconds")),*Reason);
    ++TripIndex; Export(false); BeginTrip();
}

bool ALDB90TraversalProbe::SafeEndpoint() const
{
    const FVector P=Body->GetActorLocation();
    if(!ULDMapDefinition::InBounds(P)) { return false; }
    FCollisionQueryParams Q(SCENE_QUERY_STAT(B90Endpoint),false,Body);
    return !GetWorld()->OverlapBlockingTestByChannel(P,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(27,89),Q);
}

double ALDB90TraversalProbe::SweptPush(const FVector& Direction,double Distance)
{
    const FVector Initial=Body->GetActorLocation(); auto* Move=Body->GetCharacterMovement();
    for(double Left=Distance;Left>0;Left-=10)
    {
        const FVector From=Body->GetActorLocation(), XY=From+Direction*FMath::Min(10.,Left);
        if(!ULDMapDefinition::InBounds(XY)) { break; }
        FHitResult Floor; FCollisionQueryParams Q(SCENE_QUERY_STAT(B90PushFloor),false,Body);
        if(!GetWorld()->LineTraceSingleByChannel(Floor,XY+FVector(0,0,100),XY-FVector(0,0,200),ECC_GameTraceChannel1,Q)
           ||Floor.ImpactNormal.Z<FMath::Cos(FMath::DegreesToRadians(40.))||FMath::Abs(Floor.ImpactPoint.Z-(From.Z-92))>35) { break; }
        FHitResult Hit; Move->SafeMoveUpdatedComponent(Floor.ImpactPoint+FVector(0,0,92)-From,Body->GetActorQuat(),true,Hit);
        RecordBoundary(TEXT("push")); if(Hit.bBlockingHit) { break; }
    }
    Move->StopMovementImmediately(); return FVector::Dist2D(Initial,Body->GetActorLocation());
}

void ALDB90TraversalProbe::RecordBoundary(const TCHAR* Action)
{
    const bool Safe=SafeEndpoint(); BoundarySafe&=Safe; const FVector P=Body->GetActorLocation();
    BoundaryTrace+=FString::Printf(TEXT("%s,%s,%d,%.4f,%.4f,%.4f,%s\n"),*Boundaries[BoundaryIndex].Id,Action,Phase,P.X,P.Y,P.Z,Safe?TEXT("true"):TEXT("false"));
}

void ALDB90TraversalProbe::BeginBoundary()
{
    if(BoundaryIndex>=Boundaries.Num()) { Finish(); return; }
    const auto& B=Boundaries[BoundaryIndex];
    BoundaryResult=MakeShared<FJsonObject>(); BoundaryResult->SetStringField(TEXT("id"),B.Id);
    BoundarySafe=Place(B.Position,false); Phase=3;
    if(!BoundarySafe)
    {
        BoundaryResult->SetBoolField(TEXT("passed"),false); BoundaryResult->SetStringField(TEXT("reason"),TEXT("Fixture has no floor"));
        BoundaryResults.Add(MakeShared<FJsonValueObject>(BoundaryResult)); ++Failures; ++BoundaryIndex; BeginBoundary();
    }
}

void ALDB90TraversalProbe::Tick(float Delta)
{
    Super::Tick(Delta); if(Finished) { return; }
    const double Now=GetWorld()->GetTimeSeconds();
    if(Phase==0)
    {
        if(Now<2) { return; }
        if(!Initialize()) { ++Failures; Finish(); } return;
    }
    MinTick=FMath::Min(MinTick,double(Delta)); MaxTick=FMath::Max(MaxTick,double(Delta));
    if(Phase==1)
    {
        if(Now-PhaseAt<.3) { return; }
        if(!Body->GetCharacterMovement()->IsMovingOnGround())
        { if(Now-PhaseAt>3) { EndTrip(false,TEXT("Start did not settle onto native floor")); } return; }
        const FVector Goal=Definition->NavigationTarget(Trips[TripIndex].To);
        auto* Native=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),Body->GetNavAgentLocation(),Goal);
        if(!Native||!Native->IsValid()||Native->IsPartial()) { EndTrip(false,TEXT("Native planning failed")); return; }
        for(int32 I=1;I<Native->PathPoints.Num();++I) { PlannedLength+=FVector::Distance(Native->PathPoints[I-1],Native->PathPoints[I]); }
        if(!Controller->NavigateTo(Goal)) { EndTrip(false,TEXT("Controller rejected navigation")); return; }
        WalkAt=Now; NextSample=Now; Last=Start=Body->GetActorLocation(); Phase=2; SampleTravel(0,true); return;
    }
    if(Phase==2)
    {
        SampleTravel(Delta); const FVector Goal=Definition->NavigationTarget(Trips[TripIndex].To);
        if(FVector::Dist2D(Body->GetActorLocation(),Goal)<60)
        { SampleTravel(0,true); EndTrip(PeakSpeed<=421&&SafeEndpoint(),TEXT("Arrived by native movement")); }
        else if(!Controller->IsNavigating()||Now-WalkAt>FMath::Max(30.,PlannedLength/420*2.5+20))
        { SampleTravel(0,true); EndTrip(false,TEXT("Stopped, blocked or travel timeout")); }
        else if(!ULDMapDefinition::InBounds(Body->GetActorLocation())) { EndTrip(false,TEXT("Body left playable world")); }
        return;
    }
    RecordBoundary(TEXT("sample")); const auto& B=Boundaries[BoundaryIndex];
    if(Phase==3)
    {
        if(Now-PhaseAt<.3) { return; }
        if(!Body->GetCharacterMovement()->IsMovingOnGround())
        { if(Now-PhaseAt>3) { BoundarySafe=false; Phase=6; PhaseAt=Now; } return; }
        Start=Body->GetActorLocation(); Phase=4; PhaseAt=Now;
    }
    if(Phase==4)
    {
        Body->AddMovementInput(B.Direction);
        if(Now-PhaseAt>=3)
        {
            BoundaryResult->SetNumberField(TEXT("walked_cm"),FVector::Dist2D(Start,Body->GetActorLocation()));
            Controller->StopNavigation(); Start=Body->GetActorLocation();
            const bool Accepted=CastChecked<ULDCharacterMovementComponent>(Body->GetCharacterMovement())->StartAuthorityDash(B.Direction,360);
            BoundaryResult->SetBoolField(TEXT("native_dash_accepted"),Accepted); BoundarySafe&=Accepted; Phase=5; PhaseAt=Now;
        }
    }
    else if(Phase==5&&Now-PhaseAt>.6)
    {
        const double Dash=FVector::Dist2D(Start,Body->GetActorLocation());
        BoundaryResult->SetNumberField(TEXT("dash_cm"),Dash); BoundarySafe&=Dash<=361;
        Controller->StopNavigation();
        BoundaryResult->SetNumberField(TEXT("inward_push_cm"),SweptPush(-B.Direction,200));
        BoundaryResult->SetNumberField(TEXT("outward_push_cm"),SweptPush(B.Direction,1000));
        Phase=6; PhaseAt=Now;
    }
    else if(Phase==6&&Now-PhaseAt>.5)
    {
        BoundarySafe&=Body->GetCharacterMovement()->IsMovingOnGround();
        BoundaryResult->SetBoolField(TEXT("passed"),BoundarySafe); BoundaryResult->SetStringField(TEXT("end_cm"),Body->GetActorLocation().ToString());
        BoundaryResults.Add(MakeShared<FJsonValueObject>(BoundaryResult)); if(!BoundarySafe) { ++Failures; }
        UE_LOG(LogTemp,Display,TEXT("LD_B90_BOUNDARY %s passed=%d"),*B.Id,BoundarySafe);
        ++BoundaryIndex; Export(false); BeginBoundary();
    }
}

void ALDB90TraversalProbe::Export(bool Complete)
{
    if(Output.IsEmpty()) { return; }
    auto Report=MakeShared<FJsonObject>(); Report->SetStringField(TEXT("stage"),TEXT("B90"));
    Report->SetStringField(TEXT("payload_sha256"),Definition?Definition->Checksum:TEXT("unavailable"));
    Report->SetStringField(TEXT("lane"),Lane); Report->SetStringField(TEXT("trip_filter"),Filter);
    Report->SetBoolField(TEXT("complete"),Complete); Report->SetNumberField(TEXT("failures"),Failures);
    Report->SetNumberField(TEXT("planned_trips"),Trips.Num()); Report->SetNumberField(TEXT("planned_boundaries"),Boundaries.Num());
    Report->SetNumberField(TEXT("speed_cm_s"),420); Report->SetNumberField(TEXT("wall_seconds"),FPlatformTime::Seconds()-WallStart);
    Report->SetNumberField(TEXT("min_tick_seconds"),MinTick); Report->SetNumberField(TEXT("max_tick_seconds"),MaxTick);
    if(NavigationContract) { Report->SetObjectField(TEXT("navigation_contract"),NavigationContract); }
    Report->SetNumberField(TEXT("empty_anchor_radius_cm"),5000); Report->SetArrayField(TEXT("trips"),Results); Report->SetArrayField(TEXT("boundaries"),BoundaryResults);
    Report->SetStringField(TEXT("scope"),TEXT("Standalone spatial fixtures; placement only before each case; ordinary native movement at fixed60Hz, accelerated wall execution. Empty means no POI anchor within50m, not a functional interaction test. Push uses native swept floor-following geometry, not competitive combat costs/control. No Wet/frozen effects, economy, AI, networking or human pacing acceptance."));
    FString Json; FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));
    FFileHelper::SaveStringToFile(Json,*(Output/TEXT("Traversal.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    FFileHelper::SaveStringToFile(Trace,*(Output/TEXT("TraversalTrace.csv")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    FFileHelper::SaveStringToFile(BoundaryTrace,*(Output/TEXT("BoundaryTrace.csv")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
void ALDB90TraversalProbe::Finish()
{
    Finished=true; if(Controller) { Controller->StopNavigation(); } Export(true);
    UE_LOG(LogTemp,Display,TEXT("LD_B90_TRAVERSAL_FINISHED failures=%d trips=%d boundaries=%d"),Failures,Results.Num(),BoundaryResults.Num());
    FPlatformMisc::RequestExitWithStatus(false,Failures?1:0);
}
