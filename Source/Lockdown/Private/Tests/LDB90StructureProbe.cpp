#include "Tests/LDB90StructureProbe.h"
#include "Region/LDMapRegistry.h"
#include "Region/LDMapDefinition.h"
#include "Region/LDB90Preview.h"
#include "Character/LDCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

ALDB90StructureProbe::ALDB90StructureProbe()
{ PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickGroup=TG_PostPhysics; }
bool ALDB90StructureProbe::Initialize()
{
    Controller=Cast<ALDB90PreviewController>(GetWorld()->GetFirstPlayerController()); Body=Controller?Cast<ALDCharacter>(Controller->GetPawn()):nullptr;
    Definition=ULDMapDefinition::Get(GetWorld()); Registry=GetWorld()->GetSubsystem<ULDMapRegistry>();
    FParse::Value(FCommandLine::Get(),TEXT("B90Output="),Output); FParse::Value(FCommandLine::Get(),TEXT("B90Settlement="),Settlement);
    Capture=FParse::Param(FCommandLine::Get(),TEXT("LDB90SettlementCapture")); if(Settlement.IsEmpty()) { Settlement=TEXT("capital"); }
    FString Error;
    if(!Controller||!Body||!Definition||!Registry||Output.IsEmpty()||!Registry->Rebuild(Error)) { return false; }
    const FString Before=Registry->CanonicalBindings(); const bool Repeat=Registry->Rebuild(Error)&&Before==Registry->CanonicalBindings();
    Root=Registry->Report(); Root->SetBoolField(TEXT("repeat_registry_identical"),Repeat); Failures+=!Repeat;
    auto* Imported=NewObject<ULDMapDefinition>();
    const bool ImportedTwice=Imported->Import(Definition->SourceJson,Error)&&Imported->Import(Definition->SourceJson,Error)
        &&Imported->POIs.Num()==96&&Imported->Buildings.Num()==144&&Imported->Services.Num()==36&&Imported->Aliases.Num()==16;
    Root->SetBoolField(TEXT("repeat_import_identical"),ImportedTwice); Failures+=!ImportedTwice;
    TArray<TSharedPtr<FJsonValue>> Aliases,Settlements,Doors,Closed;
    for(const auto& A:Definition->Aliases)
    { const bool Same=Registry->Resolve(A.Key,Registry->Generation)==Registry->Resolve(A.Value,Registry->Generation)&&Registry->Resolve(A.Key,Registry->Generation);
      auto J=MakeShared<FJsonObject>(); J->SetStringField(TEXT("alias"),A.Key.ToString()); J->SetStringField(TEXT("target"),A.Value.ToString()); J->SetBoolField(TEXT("same_address"),Same); Aliases.Add(MakeShared<FJsonValueObject>(J)); Failures+=!Same; }
    for(const auto& S:Definition->Settlements)
    {
        auto J=MakeShared<FJsonObject>(); J->SetStringField(TEXT("id"),S.Id.ToString()); J->SetStringField(TEXT("name"),S.Name); J->SetStringField(TEXT("identity"),S.Identity); J->SetStringField(TEXT("kind"),S.Kind);
        J->SetNumberField(TEXT("footprint_cm"),S.Radius); J->SetNumberField(TEXT("civic_radius_cm"),S.CivicRadius);
        const FVector Core=S.Position+FVector(S.CivicRadius*.9,0,0),Edge=S.Position+FVector(S.Radius*.95,0,0);
        J->SetStringField(TEXT("core_sample_cm"),Core.ToString()); J->SetStringField(TEXT("core_policy"),Definition->CivicPolicy(Core).ToString());
        J->SetStringField(TEXT("edge_sample_cm"),Edge.ToString()); J->SetStringField(TEXT("edge_policy"),Definition->CivicPolicy(Edge).ToString());
        TArray<TSharedPtr<FJsonValue>> ServiceIds; for(const auto& Service:Definition->Services) { if(Service.Settlement==S.Id) { ServiceIds.Add(MakeShared<FJsonValueString>(Service.Id.ToString())); } }
        J->SetArrayField(TEXT("district_services"),ServiceIds); Settlements.Add(MakeShared<FJsonValueObject>(J));
        Failures+=Definition->CivicPolicy(Core)!=TEXT("civic_core")||Definition->CivicPolicy(Edge)!=TEXT("unwatched_edge");
    }
    FCollisionQueryParams Q(SCENE_QUERY_STAT(B90DoorClearance),false,Body);
    for(const auto& B:Definition->Buildings)
    {
        if(!B.Enterable)
        {
            const bool Reject=!Controller->NavigateTo(B.Position); Controller->StopNavigation();
            auto J=MakeShared<FJsonObject>(); J->SetStringField(TEXT("building"),B.Id.ToString()); J->SetBoolField(TEXT("rejected"),Reject); Closed.Add(MakeShared<FJsonValueObject>(J)); Failures+=!Reject; continue;
        }
        for(int32 Side:{-1,1})
        {
            bool Clear=true; TArray<TSharedPtr<FJsonValue>> Lanes;
            for(int32 X:{-120,0,120})
            {
                const FVector A=B.Position+FVector(X,Side*1300,92),Z=B.Position+FVector(X,Side*500,92);
                FHitResult Hit; const bool Lane=!GetWorld()->SweepSingleByChannel(Hit,A,Z,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(28,90),Q);
                Clear&=Lane; Lanes.Add(MakeShared<FJsonValueBoolean>(Lane));
            }
            auto J=MakeShared<FJsonObject>(); J->SetStringField(TEXT("building"),B.Id.ToString()); J->SetStringField(TEXT("side"),Side<0?TEXT("south"):TEXT("north"));
            J->SetArrayField(TEXT("three_capsule_lanes_clear"),Lanes); J->SetBoolField(TEXT("passed"),Clear); Doors.Add(MakeShared<FJsonValueObject>(J)); Failures+=!Clear;
        }
    }
    const auto Walls=LDMapFitting::StructuralWalls(GetWorld()); int32 Paint=0,Overlap=0;
    for(TActorIterator<AActor> It(GetWorld());It;++It) { if(It->Tags.Contains(TEXT("FittedRoadSurfaces")))
    { if(const auto* C=It->FindComponentByClass<UInstancedStaticMeshComponent>())
      { for(int32 I=0;I<C->GetInstanceCount();++I) { FTransform T; C->GetInstanceTransform(I,T,true); ++Paint; Overlap+=Walls.ContainsByPredicate([&](const FBox& B){return LDMapFitting::RoadOverlaps(T,B);}); } } } }
    Root->SetNumberField(TEXT("road_paint_instances"),Paint); Root->SetNumberField(TEXT("road_wall_overlaps"),Overlap); Failures+=!Paint||Overlap!=0;
    Root->SetArrayField(TEXT("aliases"),Aliases); Root->SetArrayField(TEXT("settlements"),Settlements); Root->SetArrayField(TEXT("door_clearance"),Doors); Root->SetArrayField(TEXT("closed_shells"),Closed);
    Root->SetBoolField(TEXT("heart_contested_override"),Definition->CivicPolicy(Definition->Find(TEXT("heart"))->Position)==TEXT("contested_precinct"));
    Root->SetBoolField(TEXT("capture_only"),Capture);
    Trace=TEXT("case,elapsed_s,x_cm,y_cm,z_cm,speed_cm_s,walked_cm\n");
    if(Capture)
    { const auto* S=Definition->Find(FName(*Settlement)); if(!S) { return false; }
      Body->SetActorLocation(S->Position+FVector(0,0,92)); Body->GetCharacterMovement()->StopMovementImmediately(); Phase=3; At=GetWorld()->GetTimeSeconds(); Save(false); return true; }
    for(const auto& S:Definition->Services)
    {
        const auto* Hub=Registry->Resolve(S.Settlement,Registry->Generation);
        Cases.Add({S.Id,S.Building,TEXT("hub_to_service"),Hub->Position,S.Position});
        Cases.Add({S.Id,S.Building,TEXT("front_to_rear"),S.Position+FVector(0,-1300,0),S.Position+FVector(0,1300,0)});
    }
    BeginCase(); return true;
}
void ALDB90StructureProbe::BeginCase()
{
    if(Index>=Cases.Num()) { Save(true); Phase=4; FPlatformMisc::RequestExitWithStatus(false,Failures?3:0); return; }
    Controller->StopNavigation(); const auto& C=Cases[Index];
    Body->SetActorLocation(C.Start+FVector(0,0,92),false,nullptr,ETeleportType::TeleportPhysics); Body->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
    Phase=1; At=GetWorld()->GetTimeSeconds(); Length=Peak=Falling=0; Front=Rear=Inside=false; Step=0;
}
void ALDB90StructureProbe::EndCase(bool Passed,const FString& Why)
{
    const auto& C=Cases[Index]; Controller->StopNavigation(); auto J=MakeShared<FJsonObject>();
    J->SetStringField(TEXT("case"),C.Service.ToString()+TEXT("__")+C.Kind); J->SetStringField(TEXT("building"),C.Building.ToString()); J->SetStringField(TEXT("kind"),C.Kind);
    J->SetStringField(TEXT("start_cm"),C.Start.ToString()); J->SetStringField(TEXT("goal_cm"),C.Goal.ToString()); J->SetStringField(TEXT("end_cm"),Body->GetActorLocation().ToString());
    J->SetNumberField(TEXT("walk_seconds"),GetWorld()->GetTimeSeconds()-WalkAt); J->SetNumberField(TEXT("walked_cm"),Length); J->SetNumberField(TEXT("peak_speed_cm_s"),Peak);
    J->SetNumberField(TEXT("falling_seconds"),Falling); J->SetNumberField(TEXT("replans"),Controller->NavigationReplans-Replans);
    J->SetBoolField(TEXT("front_crossed"),Front); J->SetBoolField(TEXT("rear_crossed"),Rear); J->SetBoolField(TEXT("interior_crossed"),Inside);
    const bool Good=Passed&&Falling==0&&Peak<=421&&(C.Kind!=TEXT("front_to_rear")||(Front&&Rear&&Inside));
    J->SetBoolField(TEXT("passed"),Good); J->SetStringField(TEXT("reason"),Why); Failures+=!Good; Walks.Add(MakeShared<FJsonValueObject>(J));
    UE_LOG(LogTemp,Display,TEXT("LD_B90_STRUCTURE %s %s passed=%d"),*C.Service.ToString(),*C.Kind,Good); ++Index; Save(false); BeginCase();
}
void ALDB90StructureProbe::Tick(float Delta)
{
    Super::Tick(Delta); const double Now=GetWorld()->GetTimeSeconds();
    if(Phase==0) { if(Now>2&&!Initialize()) { FPlatformMisc::RequestExitWithStatus(false,2); } return; }
    if(Phase==4) { return; }
    if(Phase==3)
    { if(Now-At>4&&Step==0) { FScreenshotRequest::RequestScreenshot(Output/TEXT("Settlement.png"),true,false); ++Step; }
      if(Now-At>6) { Save(true); Phase=4; FPlatformMisc::RequestExitWithStatus(false,Failures?3:0); } return; }
    const auto& C=Cases[Index];
    if(Phase==1)
    { if(Now-At<.3) { return; } WalkAt=Now; Replans=Controller->NavigationReplans;
      if(!Body->GetCharacterMovement()->IsMovingOnGround()||!Controller->NavigateTo(C.Goal)) { EndCase(false,TEXT("No ordinary path")); return; }
      Phase=2; Last=Body->GetActorLocation(); NextSample=Now; }
    if(Phase==2)
    {
        const FVector P=Body->GetActorLocation(); Length+=FVector::Dist2D(P,Last); Peak=FMath::Max(Peak,double(Body->GetVelocity().Size2D()));
        if(Body->GetCharacterMovement()->IsFalling()) { Falling+=Delta; }
        const auto* B=Definition->Buildings.FindByPredicate([&](const auto& V){return V.Id==C.Building;});
        if(B)
        {
            const FVector Local=P-B->Position,Prev=Last-B->Position;
            if(FVector2D(Local).Size()<100) { Inside=true; }
            if(FMath::Abs(Local.X)<125)
            { Front|=(Prev.Y<=-880&&Local.Y>=-880)||(Prev.Y>=-880&&Local.Y<=-880); Rear|=(Prev.Y<=880&&Local.Y>=880)||(Prev.Y>=880&&Local.Y<=880); }
        }
        Last=P;
        const bool Arrived=FVector::Dist2D(P,C.Goal)<60;
        if(Now>=NextSample||Arrived)
        { Trace+=FString::Printf(TEXT("%s__%s,%.6f,%.3f,%.3f,%.3f,%.3f,%.3f\n"),*C.Service.ToString(),*C.Kind,Now-WalkAt,P.X,P.Y,P.Z,Body->GetVelocity().Size2D(),Length); NextSample=Now+.25; }
        if(Arrived) { EndCase(true,TEXT("Native arrival")); }
        else if(!Controller->IsNavigating()||Now-WalkAt>120) { EndCase(false,TEXT("Stopped or timed out")); }
    }
}
void ALDB90StructureProbe::Save(bool Complete)
{
    if(!Root) { return; } Root->SetBoolField(TEXT("complete"),Complete); Root->SetNumberField(TEXT("failures"),Failures); Root->SetNumberField(TEXT("planned_walks"),Cases.Num()); Root->SetArrayField(TEXT("walks"),Walks);
    FString Json; FJsonSerializer::Serialize(Root.ToSharedRef(),TJsonWriterFactory<>::Create(&Json));
    FFileHelper::SaveStringToFile(Json,*(Output/TEXT("Structure.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    FFileHelper::SaveStringToFile(Trace,*(Output/TEXT("StructureTrace.csv")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
