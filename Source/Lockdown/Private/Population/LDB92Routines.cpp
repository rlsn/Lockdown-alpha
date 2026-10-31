#include "Population/LDB92Routines.h"
#include "Population/LDB92Roster.h"
#include "Population/LDB92PopulationContract.h"
#include "Combat/LDCombatDirector.h"
#include "World/LDEnvironmentDirector.h"
#include "Region/LDMapDefinition.h"
#include "Character/LDCharacterMovementComponent.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Engine/World.h"
#include "EngineUtils.h"

ALDB92RoutineController::ALDB92RoutineController(){bReplicates=false;bSetControlRotationFromPawnOrientation=false;}
ALDB92Routines::ALDB92Routines(){bReplicates=false;PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
bool ALDB92Routines::NativeGoal(FVector Requested,FVector& Goal) const
{
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());FNavLocation P;
    if(!Nav||!Nav->ProjectPointToNavigation(Requested,P,FVector(180,180,200))||FVector::DistSquared2D(P.Location,Requested)>180*180){return false;}Goal=P.Location;return true;
}
bool ALDB92Routines::StrategicRoute(const UWorld* W,FName From,FName To,TArray<FVector>& Points)
{
    Points.Reset();const auto* Map=ULDMapDefinition::Get(W);if(!Map||!Map->Find(From)||!Map->Find(To)||From==To){return false;}
    TMap<FName,int32> Distance,ParentEdge;TMap<FName,FName> Parent;TSet<FName> Closed;Distance.Add(From,0);
    while(true)
    {
        FName N;int32 Best=MAX_int32;for(const auto& P:Distance){if(!Closed.Contains(P.Key)&&(P.Value<Best||(P.Value==Best&&P.Key.LexicalLess(N)))){N=P.Key;Best=P.Value;}}if(N.IsNone()){return false;}if(N==To){break;}Closed.Add(N);
        for(int32 I=0;I<Map->Routes.Num();++I){const auto& E=Map->Routes[I];const FName Other=E.From==N?E.To:E.To==N?E.From:NAME_None;if(Other.IsNone()||Closed.Contains(Other)){continue;}double L=0;for(int32 K=1;K<E.Points.Num();++K){L+=FVector::Dist2D(E.Points[K-1],E.Points[K]);}const int32 D=Best+FMath::RoundToInt(L);if(!Distance.Contains(Other)||D<Distance[Other]){Distance.Add(Other,D);Parent.Add(Other,N);ParentEdge.Add(Other,I);}}
    }
    TArray<FName> Nodes;for(FName N=To;N!=From;N=Parent[N]){if(!Parent.Contains(N)){return false;}Nodes.Insert(N,0);}FName N=From;
    for(FName Other:Nodes){const auto& E=Map->Routes[ParentEdge[Other]];for(int32 K=0;K<E.Points.Num();++K){const FVector P=E.Points[E.From==N?K:E.Points.Num()-1-K];if(Points.IsEmpty()||FVector::DistSquared2D(Points.Last(),P)>50*50){Points.Add(P);}}N=Other;}return !Points.IsEmpty();
}
bool ALDB92Routines::Initialize(ALDB92Roster* R,FString& Error)
{
    if(!HasAuthority()||Ready||!R||!R->Ready){Error=TEXT("Routine authority requires the initialized roster once");return false;}OwnerRoster=R;const auto* Map=ULDMapDefinition::Get(GetWorld());FString Json;TSharedPtr<FJsonObject> Root;
    if(!Map||!FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("Lockdown/Data/V09/schedules_b92.json")))||FMD5::HashAnsiString(*Json)!=LDB92PopulationContract::Schedules||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)){Error=TEXT("Routine schedule contract mismatch");return false;}
    auto Front=[&](FName Id){const auto* S=Map->ServiceFor(Id);return S?S->Position-FVector(0,1400,0):Map->NavigationTarget(Id);};
    for(const auto& V:Root->GetArrayField(TEXT("rows")))
    {
        const auto J=V->AsObject();const FString RoutineRole=J->GetStringField(TEXT("role"));if(RoutineRole==TEXT("guard")){continue;}FLDB92Routine S;S.Source=*J->GetStringField(TEXT("id"));const auto* P=R->People().FindByPredicate([&](const auto& Person){return Person.Source==S.Source;});if(!P||!P->Body.IsValid()||P->Body->GetController()){Error=TEXT("Routine source/body already controlled or absent");return false;}S.Identity=P->Id;S.Body=P->Body;S.Previous=P->Body->GetActorLocation();S.Traveler=RoutineRole==TEXT("traveler");
        if(S.Traveler){S.From=*J->GetStringField(TEXT("from_poi_id"));S.To=*J->GetStringField(TEXT("to_poi_id"));S.Activities={Front(S.From),Front(S.To)};S.Durations={J->GetNumberField(TEXT("dwell_s")),J->GetNumberField(TEXT("dwell_s"))};S.Remaining=FMath::Max(.2,S.Durations[0]-FMath::Fmod(J->GetNumberField(TEXT("phase_offset_s")),S.Durations[0]));S.Destination=S.Activities[0];S.Route={S.Destination};}
        else
        {
            const FName Work=*J->GetStringField(TEXT("work_poi_id")),Rest=*J->GetStringField(TEXT("rest_poi_id")),Public=*J->GetStringField(TEXT("home_public_anchor"));FName Market=Public;for(const auto& Service:Map->Services){if(Service.Settlement==Public&&Service.Functions.Contains(TEXT("bank"))){Market=Service.POI;break;}}
            S.Activities={Front(Work),Front(Market),Map->NavigationTarget(Rest),Front(Work)};const double Offset=FMath::Fmod(J->GetNumberField(TEXT("phase_offset_s")),J->GetNumberField(TEXT("loop_s")));int32 I=0;
            for(const auto& Segment:J->GetArrayField(TEXT("segments"))){const auto A=Segment->AsObject();const double Start=A->GetNumberField(TEXT("start_s")),End=A->GetNumberField(TEXT("end_s"));S.Durations.Add(End-Start);if(Offset>=Start&&Offset<End){S.Phase=I;S.Remaining=End-Offset;}++I;}
            if(S.Durations.Num()!=4){Error=TEXT("Civilian needs four supplied activity durations");return false;}S.Destination=S.Activities[S.Phase];S.Route={S.Destination};
        }
        for(auto& Goal:S.Activities){FVector F;if(!NativeGoal(Goal,F)){Error=TEXT("Routine endpoint lacks a native floor: ")+S.Source.ToString();return false;}Goal=F;}S.Destination=S.Activities[S.Phase];S.Route={S.Destination};
        double Nearest=DBL_MAX;for(const auto& Guard:R->People()){if(Guard.Role!=ELDB92Role::Guard){continue;}const double D=FVector::DistSquared2D(S.Previous,Guard.Home);if(D<Nearest){Nearest=D;S.Post=Guard.Home-FVector(0,0,95);}}
        auto* AI=GetWorld()->SpawnActor<ALDB92RoutineController>();if(!AI){Error=TEXT("Routine controller allocation failed");return false;}AI->Possess(P->Body.Get());S.Controller=AI;auto* Move=P->Body->GetCharacterMovement();Move->MaxWalkSpeed=420;Move->bOrientRotationToMovement=true;Move->RotationRate=FRotator(0,720,0);S.NextSense=GetWorld()->GetTimeSeconds()+Routines.Num()%5*.04;Routines.Add(MoveTemp(S));
    }
    Ready=Routines.Num()==128;if(!Ready){Error=TEXT("Routine capacity must be96 civilians/32 travelers");return false;}return Audit(Error);
}
bool ALDB92Routines::SealedCargo(const ALDCharacter* B) const
{return Routines.ContainsByPredicate([&](const auto& S){return S.Traveler&&!S.Dead&&S.Body.Get()==B;});}
bool ALDB92Routines::RoleDied(ALDCharacter* B)
{for(auto& S:Routines){if(S.Body==B&&!S.Dead){S.Dead=true;S.Mode=TEXT("dead");S.Route.Reset();if(S.Controller.IsValid()){S.Controller->StopMovement();}return true;}}return false;}
bool ALDB92Routines::StartMove(FLDB92Routine& S,FVector Goal,double Now)
{
    if(!S.Controller.IsValid()||!S.Body.IsValid()){return false;}FVector F;if(!NativeGoal(Goal,F)){S.Mode=TEXT("blocked");S.RetryAt=Now+5;++S.Failures;return false;}
    const double Start=FPlatformTime::Seconds();++PathQueries;const auto Result=S.Controller->MoveToLocation(F,35,false,true,true,true,nullptr,false);const double Ms=(FPlatformTime::Seconds()-Start)*1000;PathMilliseconds+=Ms;MaxPathMilliseconds=FMath::Max(MaxPathMilliseconds,Ms);
    if(Result==EPathFollowingRequestResult::Failed){S.Mode=TEXT("blocked");S.RetryAt=Now+5;++S.Failures;return false;}S.Mode=S.Fleeing?TEXT("flee"):TEXT("travel");return true;
}
void ALDB92Routines::PlanActivity(FLDB92Routine& S,double Now)
{
    S.Route.Reset();S.Waypoint=0;S.Dwelling=false;
    if(S.Traveler){const FName From=S.Returning?S.To:S.From,To=S.Returning?S.From:S.To;if(!StrategicRoute(GetWorld(),From,To,S.Route)){S.Mode=TEXT("blocked");S.RetryAt=Now+5;++S.Failures;return;}S.Destination=S.Activities[S.Returning?0:1];S.Route.Add(S.Destination);}
    else{S.Destination=S.Activities[S.Phase];S.Route={S.Destination};}++S.Departures;StartMove(S,S.Route[0],Now);
}
void ALDB92Routines::Sense(FLDB92Routine& S,double Now)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* B=S.Body.Get();const auto* Own=M->Combat->StateFor(B);const auto* Env=ALDEnvironmentDirector::Get(GetWorld());if(!Own||!Env){return;}bool Seen=Now<Own->HitUntil;
    const double Radius=Own->Tactical.Has(LDTactical::EStatus::Blind,LDTactical::Milliseconds(Now))?200:1800;
    for(auto Weak:Danger){auto* Other=Weak.Get();if(!Other||Other==B||FVector::DistSquared2D(B->GetActorLocation(),Other->GetActorLocation())>Radius*Radius){continue;}const uint8 Cue=M->Combat->CueFor(Other),Action=Cue&15;if(!(Cue&32)&&Action!=1&&Action!=2&&Action!=6&&Action!=9){continue;}if(M->Combat->Concealed(Other)&&(M->Combat->Concealed(Other,false)||FVector::DistSquared2D(B->GetActorLocation(),Other->GetActorLocation())>200*200)){continue;}++PerceptionRays;if(Env->Ray(B->GetActorLocation(),Other->GetActorLocation(),nullptr,Radius)){Seen=true;break;}}
    if(Seen){S.LastDanger=Now;if(!S.Fleeing){if(S.Dwelling){S.Remaining=FMath::Max(0.,S.Until-Now);}S.Fleeing=true;S.Dwelling=false;++S.Flees;double Best=DBL_MAX;if(OwnerRoster.IsValid()){for(const auto& Post:OwnerRoster->People()){if(Post.Role!=ELDB92Role::Guard){continue;}const double D=FVector::DistSquared2D(B->GetActorLocation(),Post.Home);if(D<Best){Best=D;S.Post=Post.Home-FVector(0,0,95);}}}S.Controller->StopMovement();StartMove(S,S.Post,Now);}}
    else if(S.Fleeing&&Now-S.LastDanger>=10){S.Fleeing=false;++S.Resumes;S.Controller->StopMovement();if(!S.Route.IsValidIndex(S.Waypoint)){S.Route={S.Destination};S.Waypoint=0;}StartMove(S,S.Route[S.Waypoint],Now);}
}
void ALDB92Routines::Tick(float Delta)
{
    Super::Tick(Delta);if(!Ready){return;}auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const double Now=GetWorld()->GetTimeSeconds();
    if(Now>=NextDangerSample){NextDangerSample=Now+.2;Danger.Reset();for(TActorIterator<ALDCharacter> It(GetWorld());It;++It){Danger.Add(*It);}}
    for(auto& S:Routines)
    {
        if(S.Dead||!S.Body.IsValid()||!S.Controller.IsValid()){continue;}auto* B=S.Body.Get();const double Step=FVector::Dist2D(B->GetActorLocation(),S.Previous);S.Distance+=Step;S.MaxSpeed=FMath::Max(S.MaxSpeed,Step/FMath::Max(.001f,Delta));S.Previous=B->GetActorLocation();
        if(!M->IsPlaying()){S.Controller->StopMovement();continue;}if(Now<S.NextSense){continue;}S.NextSense=Now+.2;Sense(S,Now);
        if(!M->Combat->Allows(B,LDTactical::EAction::Move)){S.Mode=TEXT("paused");continue;}
        if(S.Fleeing){if(S.Controller->GetMoveStatus()==EPathFollowingStatus::Idle&&Now>=S.RetryAt&&FVector::DistSquared2D(B->GetActorLocation(),S.Post)>80*80){StartMove(S,S.Post,Now);}continue;}
        if(S.Dwelling){if(Now<S.Until){continue;}S.Dwelling=false;++S.Transitions;if(S.Traveler){S.Phase=1-S.Phase;S.Returning=S.Phase==0;S.Remaining=S.Durations[S.Phase];}else{S.Phase=(S.Phase+1)%4;S.Remaining=S.Durations[S.Phase];}PlanActivity(S,Now);continue;}
        if(S.Route.IsEmpty()){if(Now>=S.RetryAt){PlanActivity(S,Now);}continue;}
        FVector Goal;if(!NativeGoal(S.Route[S.Waypoint],Goal)){if(Now>=S.RetryAt){StartMove(S,S.Route[S.Waypoint],Now);}continue;}
        if(FVector::DistSquared2D(B->GetActorLocation(),Goal)<=75*75&&FMath::Abs(B->GetActorLocation().Z-Goal.Z-90)<100)
        {
            if(++S.Waypoint<S.Route.Num()){StartMove(S,S.Route[S.Waypoint],Now);}else{S.Controller->StopMovement();S.Dwelling=true;S.Mode=TEXT("dwell");S.Until=Now+S.Remaining;++S.Arrivals;if(S.Traveler&&S.Trips<S.Departures){++S.Trips;}}continue;
        }
        if(S.Controller->GetMoveStatus()==EPathFollowingStatus::Idle&&Now>=S.RetryAt){StartMove(S,S.Route[S.Waypoint],Now);}
    }
}
bool ALDB92Routines::Audit(FString& Error) const
{
    TSet<FGuid> Ids;int32 Travelers=0;for(const auto& S:Routines){Travelers+=S.Traveler;if(!S.Identity.IsValid()||Ids.Contains(S.Identity)||(!S.Dead&&(!S.Body.IsValid()||!S.Controller.IsValid()||S.Controller->GetPawn()!=S.Body.Get()||S.Body->GetIsReplicated()||S.Controller->GetIsReplicated()))){Error=TEXT("Routine source/control identity invariant failed");return false;}Ids.Add(S.Identity);}if(!Ready||Routines.Num()!=128||Travelers!=32){Error=TEXT("Routine role count changed");return false;}return true;
}
FString ALDB92Routines::ExportJson() const
{
    auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("path_queries"),PathQueries);J->SetNumberField(TEXT("path_ms"),PathMilliseconds);J->SetNumberField(TEXT("max_path_ms"),MaxPathMilliseconds);J->SetNumberField(TEXT("perception_rays"),PerceptionRays);TArray<TSharedPtr<FJsonValue>> Rows;
    for(const auto& S:Routines){auto V=MakeShared<FJsonObject>();V->SetStringField(TEXT("source"),S.Source.ToString());V->SetStringField(TEXT("id"),S.Identity.ToString());V->SetBoolField(TEXT("traveler"),S.Traveler);V->SetBoolField(TEXT("dead"),S.Dead);V->SetStringField(TEXT("mode"),S.Mode);V->SetNumberField(TEXT("phase"),S.Phase);V->SetNumberField(TEXT("distance_cm"),S.Distance);V->SetNumberField(TEXT("max_speed_cm_s"),S.MaxSpeed);V->SetNumberField(TEXT("arrivals"),S.Arrivals);V->SetNumberField(TEXT("departures"),S.Departures);V->SetNumberField(TEXT("transitions"),S.Transitions);V->SetNumberField(TEXT("trips"),S.Trips);V->SetNumberField(TEXT("flees"),S.Flees);V->SetNumberField(TEXT("resumes"),S.Resumes);V->SetNumberField(TEXT("path_failures"),S.Failures);V->SetStringField(TEXT("position"),S.Previous.ToString());V->SetStringField(TEXT("destination"),S.Destination.ToString());V->SetNumberField(TEXT("remaining_s"),S.Dwelling?FMath::Max(0.,S.Until-GetWorld()->GetTimeSeconds()):S.Remaining);Rows.Add(MakeShared<FJsonValueObject>(V));}J->SetArrayField(TEXT("routines"),Rows);FString Json;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Json));return Json;
}
void ALDB92Routines::EndPlay(const EEndPlayReason::Type Reason)
{for(auto& S:Routines){if(S.Controller.IsValid()){S.Controller->StopMovement();S.Controller->UnPossess();S.Controller->Destroy();}}Super::EndPlay(Reason);}
