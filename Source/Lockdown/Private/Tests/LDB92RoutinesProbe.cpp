#include "Tests/LDB92RoutinesProbe.h"
#include "Population/LDB92Roster.h"
#include "Population/LDB92Routines.h"
#include "Economy/LDB92Economy.h"
#include "Combat/LDCombatDirector.h"
#include "World/LDWorldObjects.h"
#include "NavigationSystem.h"
#include "Region/LDMapDefinition.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Engine/World.h"

ALDB92RoutinesProbe::ALDB92RoutinesProbe(){bReplicates=false;PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;FParse::Value(FCommandLine::Get(),TEXT("B92RoutineSeconds="),Seconds);Seconds=FMath::Clamp(Seconds,60,1800);}
void ALDB92RoutinesProbe::Check(bool Good,const FString& Name)
{Failures+=!Good;auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("case"),Name);J->SetBoolField(TEXT("passed"),Good);Checks.Add(MakeShared<FJsonValueObject>(J));UE_LOG(LogTemp,Display,TEXT("LD_B92_ROUTINES passed=%d %s"),Good,*Name);}
bool ALDB92RoutinesProbe::PlaceObserver(FVector Floor)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* B=Observer.IsValid()?Cast<ALDCharacter>(Observer->GetPawn()):nullptr;auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());FNavLocation N;if(!B||!Nav||!Nav->ProjectPointToNavigation(Floor,N,FVector(180,180,200))){return false;}
    Observer->StopSourceOrder();B->SetActorLocation(N.Location+FVector(0,0,95));B->GetCharacterMovement()->StopMovementImmediately();Observer->ClientSetLocation(B->GetActorLocation(),B->GetActorRotation());M->StateFor(Observer.Get())->ProtectedUntil=0;M->Combat->StateFor(B)->ProtectionUntil=0;M->SyncOwner(Observer.Get());ALDEnvironmentDirector::Get(GetWorld())->Publish();return true;
}
void ALDB92RoutinesProbe::Finish()
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* R=M->Population.Get();auto* A=R->Routines.Get();auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("scope"),TEXT("native_civilian_traveler_routines"));J->SetBoolField(TEXT("complete"),true);J->SetBoolField(TEXT("full_stage_acceptance"),false);J->SetNumberField(TEXT("failures"),Failures);J->SetNumberField(TEXT("duration_s"),Started?GetWorld()->GetTimeSeconds()-Started:0);J->SetNumberField(TEXT("wall_duration_s"),Wall?FPlatformTime::Seconds()-Wall:0);J->SetNumberField(TEXT("requested_s"),Seconds);J->SetArrayField(TEXT("checks"),Checks);J->SetArrayField(TEXT("samples"),Samples);FString Json,Out;FParse::Value(FCommandLine::Get(),TEXT("B92Output="),Out);FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Json));bool Saved=FFileHelper::SaveStringToFile(Json,*(Out/TEXT("Routines.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);Saved&=FFileHelper::SaveStringToFile(A->ExportJson(),*(Out/TEXT("Final.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);Saved&=FFileHelper::SaveStringToFile(R->Economy->Accounting().ExportJson(TEXT("B92Routines")),*(Out/TEXT("Ledger.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);Phase=99;FPlatformMisc::RequestExitWithStatus(false,Failures||!Saved?3:0);
}
void ALDB92RoutinesProbe::Tick(float Delta)
{
    Super::Tick(Delta);
#if !UE_BUILD_SHIPPING
    if(Phase==99){return;}auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* R=M?M->Population.Get():nullptr;if(!R||!R->Ready||!R->Routines){return;}auto* A=R->Routines.Get();auto* C=M->Combat.Get();const double Now=GetWorld()->GetTimeSeconds();FString Error;
    if(Failures){Finish();return;}if(Started&&FPlatformTime::Seconds()-Wall>Seconds+180){Check(false,TEXT("Native routine observation timed out"));Finish();return;}
    auto Find=[&](){return A->States().FindByPredicate([&](const auto& S){return S.Source==Subject;});};
    auto Done=[&](uint8 Id){TSharedPtr<FJsonObject> J;if(Observer.IsValid()){if(const auto* Text=Observer->ServicesProbeReports.Find(Id)){FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(*Text),J);}}return J&&J->GetBoolField(TEXT("completed"))&&J->GetBoolField(TEXT("accepted"));};
    if(Phase==0)
    {
        for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It){if(auto* PC=Cast<ALDB92Controller>(It->Get());PC&&PC->GetNetConnection()&&PC->GetPawn()){Observer=PC;break;}}if(!Observer.IsValid()){return;}AddTickPrerequisiteActor(A);Started=Now;Wall=FPlatformTime::Seconds();
        Check(A->States().Num()==128&&R->People().Num()==196&&R->Seats().Num()==24&&A->Audit(Error)&&R->Audit(Error),TEXT("One actual remote observer and128 native routine controllers retain all196 role identities/24 seats"));
        Check(!A->GetIsReplicated()&&A->States().FilterByPredicate([](const auto& S){return S.Traveler;}).Num()==32,TEXT("96 civilians and32 travelers remain server-owned; no routine/role/identity replication"));
        Check(M->StateFor(Observer.Get())->Gold==0&&M->StateFor(Observer.Get())->Xp==0&&M->StateFor(Observer.Get())->Inventory.Items.IsEmpty()&&R->Economy->Accounting().ItemRecords().Num()==569,TEXT("Routine startup and cosmetic cargo grant no item, purse, income or progression"));Phase=1;
    }
    if(Now>=NextSnapshot&&Started)
    {
        NextSnapshot=Now+10;auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("elapsed_s"),Now-Started);TArray<TSharedPtr<FJsonValue>> Rows;for(const auto& S:A->States()){auto V=MakeShared<FJsonObject>();V->SetStringField(TEXT("source"),S.Source.ToString());V->SetStringField(TEXT("id"),S.Identity.ToString());V->SetStringField(TEXT("mode"),S.Mode);V->SetNumberField(TEXT("phase"),S.Phase);V->SetStringField(TEXT("position"),S.Previous.ToString());V->SetNumberField(TEXT("distance_cm"),S.Distance);V->SetNumberField(TEXT("arrivals"),S.Arrivals);V->SetNumberField(TEXT("trips"),S.Trips);Rows.Add(MakeShared<FJsonValueObject>(V));}J->SetArrayField(TEXT("bodies"),Rows);Samples.Add(MakeShared<FJsonValueObject>(J));
    }
    if(Phase==1){for(const auto& S:A->States()){if(!S.Traveler&&S.Body.IsValid()&&S.Body->GetVelocity().Size2D()>100){Subject=S.Source;RootPosition=S.Body->GetActorLocation();RootDistance=S.Distance;C->ApplyStatus(S.Body.Get(),LDTactical::EStatus::Root,LDTactical::Milliseconds(Now)+2000,FGuid::NewGuid());At=Now;Phase=2;break;}}return;}
    const auto* S=Find();
    if(Phase==2){if(Now-At<1){return;}Check(S&&S->Body.IsValid()&&FVector::Dist2D(S->Body->GetActorLocation(),RootPosition)<30,TEXT("Labelled Root fixture stops native path-following movement on the full map"));Phase=3;return;}
    if(Phase==3){if(Now-At<7){return;}Check(S&&S->Distance>RootDistance+100,TEXT("After Root expires the same body resumes its existing native route"));FleeBefore=S->Flees;Check(PlaceObserver(S->Body->GetActorLocation()+FVector(3500,0,-95)),TEXT("Only the remote observer is staged; scheduled NPC positions are never moved by the probe"));Check(C->RequestBody(Cast<ALDCharacter>(Observer->GetPawn()),0,FVector(0,1,0),Error),TEXT("Out-of-sight fixture issues an ordinary targetless basic attack, without funding or gear"));At=Now;Phase=4;return;}
    if(Phase==4){if(Now-At<1){return;}Check(S->Flees==FleeBefore,TEXT("An attack outside1800cm produces no remote danger knowledge or flee order"));bool Visible=false;for(int32 I=0;I<8;++I){const double Angle=I*PI/4;const FVector P=S->Body->GetActorLocation()+FVector(FMath::Cos(Angle)*600,FMath::Sin(Angle)*600,-95);if(!PlaceObserver(P)){continue;}if(ALDEnvironmentDirector::Get(GetWorld())->Ray(S->Body->GetActorLocation(),Observer->GetPawn()->GetActorLocation())){Visible=true;break;}}Check(Visible,TEXT("A staged nearby attack has an actual clear normal sight ray"));At=Now;Phase=5;return;}
    if(Phase==5)
    {
        if(Now-At<1.5){return;}Check(S->Flees==FleeBefore,TEXT("A currently visible idle body does not inherit its earlier unobserved attack cue"));
        C->ApplyStatus(S->Body.Get(),LDTactical::EStatus::Root,LDTactical::Milliseconds(Now)+4500,FGuid::NewGuid());bool Visible=false;for(int32 I=0;I<8;++I){const double Angle=I*PI/4;if(PlaceObserver(S->Body->GetActorLocation()+FVector(FMath::Cos(Angle)*600,FMath::Sin(Angle)*600,-95))&&ALDEnvironmentDirector::Get(GetWorld())->Ray(S->Body->GetActorLocation(),Observer->GetPawn()->GetActorLocation())){Visible=true;break;}}
        Check(Visible,TEXT("Labelled brief Root holds the positive sight fixture while the current clear ray is rechecked"));Check(C->RequestBody(Cast<ALDCharacter>(Observer->GetPawn()),0,FVector(0,1,0),Error),TEXT("Visible danger is an actual accepted basic attack, not an injected private target"));AttackTrials=1;At=Now;Phase=6;return;
    }
    if(Phase==6)
    {
        if(S->Flees>FleeBefore&&S->Fleeing){Check(true,TEXT("Observed public attack cue starts fleeing toward a known guard/post without joining combat"));At=Now;Phase=7;return;}if(Now-At<1.5){return;}if(AttackTrials<3){Check(C->RequestBody(Cast<ALDCharacter>(Observer->GetPawn()),0,FVector(0,1,0),Error),TEXT("Sampling fixture retries a normal basic attack with its ordinary cadence and windup"));++AttackTrials;At=Now;return;}Check(false,TEXT("Three ordinary visible attack attempts produced no observed flee response"));return;
    }
    if(Phase==7){if(Now-At<11){return;}Check(!S->Fleeing&&S->Resumes>0&&S->Body.IsValid(),TEXT("Ten seconds without observed danger resumes the retained activity and source identity"));for(const auto& T:A->States()){if(T.Traveler&&T.Body.IsValid()&&T.Body->GetVelocity().Size2D()>100){if(PlaceObserver(T.Body->GetActorLocation()+FVector(-250,-250,-95))){Observer->ServicesProbeReports.Remove(70);Observer->ClientB92ProbeTask(70,NAME_None);At=Now;Phase=8;return;}}}return;}
    if(Phase==8){if(!Done(70)){if(Now-At>15){Check(false,TEXT("Remote moving sealed-cargo view capture timed out"));}return;}Check(true,TEXT("Remote authorized moving-body view shows a cosmetic sealed pack and no item or stock identity"));bool Found=false;for(const auto& Point:ULDMapDefinition::Get(GetWorld())->POIs){if(!A->States().ContainsByPredicate([&](const auto& T){return T.Traveler&&T.Body.IsValid()&&FVector::DistSquared2D(T.Body->GetActorLocation(),Point.Position)<4000*4000;})&&PlaceObserver(Point.Position)){Found=true;break;}}Check(Found,TEXT("Observer-only privacy fixture moves beyond all traveler sight ranges"));Observer->ServicesProbeReports.Remove(71);Observer->ClientB92ProbeTask(71,NAME_None);At=Now;Phase=9;return;}
    if(Phase==9){if(!Done(71)){if(Now-At>10){Check(false,TEXT("Remote hidden-body view retirement timed out"));}return;}Check(true,TEXT("Leaving sight retires observed cargo/body proxies; full role actors remain unreplicated"));const auto* P=R->People().FindByPredicate([&](const auto& Person){return Person.Source==Subject;});const FGuid Identity=P->Id;C->Die(S->Body.Get(),Now);Check(A->States().FindByPredicate([&](const auto& T){return T.Source==Subject;})->Dead&&R->People().FindByPredicate([&](const auto& Person){return Person.Id==Identity;})->Defeated&&R->Economy->Accounting().Account(ALDB92Economy::Wallet(Identity))->Gold==3&&R->Audit(Error),TEXT("Labelled native death retains source/purse, retires its routine and grants no replacement body or reward"));Phase=10;}
    if(Phase==10&&Now-Started>=Seconds)
    {
        int32 Walking=0,Trips=0,Civil=0,Dead=0;bool Bounded=true;for(const auto& T:A->States()){Walking+=T.Distance>1000;Trips+=T.Traveler&&T.Trips>0;Civil+=!T.Traveler&&T.Transitions>0;Dead+=T.Dead;Bounded&=T.MaxSpeed<=500;}
        Check(Walking>=(Seconds>=900?127:32)&&Bounded,TEXT("Native planar motion remains bounded; physical movement coverage requires every continuing source in the full observation"));if(Seconds>=900){Check(Trips>=16&&Civil>=80,TEXT("Fifteen real unaccelerated minutes include repeated civilian activities and at least16 cross-settlement traveler arrivals"));Check(FPlatformTime::Seconds()-Wall>=899,TEXT("Fifteen-minute observation uses real wall time without time dilation or schedule fast-forward"));}
        Check(Dead==1&&A->Audit(Error)&&R->Audit(Error)&&R->Economy->SynchronizeWorld(Error)&&R->Economy->Audit(Error)&&ALDWorldObjects::Get(GetWorld())->Conserved(Error),TEXT("One labelled defeated source and127 continuing routines close canonical identities/economy/world"));Check(R->Economy->Accounting().Minted()==0&&M->StateFor(Observer.Get())->Gold==0&&M->StateFor(Observer.Get())->Xp==0&&M->StateFor(Observer.Get())->Inventory.Items.IsEmpty(),TEXT("Scheduled activity, cosmetic cargo, flee/resume and noncontender death mint no reward or physical goods"));Finish();
    }
#endif
}
