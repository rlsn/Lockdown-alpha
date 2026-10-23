#include "Tests/LDInstitutionProbe.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Economy/LDProductionDirector.h"
#include "Economy/LDInstitutionSite.h"
#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Region/LDRegionDefinition.h"
#include "Vision/LDVisionDirector.h"
#include "Navigation/LDNavigationComponent.h"
#include "Core/LDGroundQueries.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AIController.h"
#include "Engine/World.h"
#include "EngineUtils.h"
void ALDInstitutionProbe::Geography(float Delta)
{
#if !UE_BUILD_SHIPPING
    auto* W=GetWorld();auto* M=W->GetAuthGameMode<ALDGameMode>();auto* PC=Cast<ALDPlayerController>(W->GetFirstPlayerController());auto* B=Cast<ALDCharacter>(PC->GetPawn());const auto* D=LDRegion::Definition(W);const double Now=W->GetTimeSeconds();
    if(!B||!D){Check(false,TEXT("Geography source body and saved definition available"));Finish(TEXT("Geography failure"));return;}
    if(!Started)
    {
        Started=true;FString Error;M->SetLobbyChoice(PC,0,true,Error);if(!M->StartDevelopmentMatch(PC,Error)){Check(false,Error);Finish(TEXT("Geography startup failure"));return;}PC->SetMenuOpen(false);PC->bFreshInputRequired=false;M->StateFor(PC)->ProtectedUntil=0;M->Combat->StateFor(B)->ProtectionUntil=0;
        for(TActorIterator<ALDResident> It(W);It;++It){It->SetActorTickEnabled(false);It->GetCharacterMovement()->StopMovementImmediately();if(auto* AI=Cast<AAIController>(It->GetController())){AI->StopMovement();}}
        bool Complete=true;for(const auto& Route:D->Routes){for(int32 I=1;I<Route.Points.Num();++I){const auto* P=UNavigationSystemV1::FindPathToLocationSynchronously(W,Route.Points[I-1],Route.Points[I]);Complete&=P&&P->IsValid()&&!P->IsPartial();}}
        Check(Complete,TEXT("All authored public, covered and outer route segments have complete saved-map nav paths"));
        for(uint8 Site=0;Site<5;++Site)
        {
            const FVector Point=ALDProductionDirector::Position(W,Site);FVector Surface;const bool Ground=LDGround::MapSurface(W,Point+FVector(-50,30,0),Surface);if(Ground){B->SetActorLocation(Surface+FVector(0,0,92));}
            const auto* P=UNavigationSystemV1::FindPathToLocationSynchronously(W,D->Guild,Point+FVector(-50,30,0));Check(Ground&&P&&P->IsValid()&&!P->IsPartial()&&M->Production->Eligible(PC,Site,false),TEXT("Reachable ground/body sight within 120 cm at ")+ALDProductionDirector::SiteName(Site));
        }
        for(uint8 Site=0;Site<5;++Site){const auto* P=UNavigationSystemV1::FindPathToLocationSynchronously(W,D->Guild,ALDInstitutionSite::Position(W,Site));Check(P&&P->IsValid()&&!P->IsPartial()&&!D->Residents.ContainsByPredicate([&](const auto& R){return FVector::DistSquared2D(R,ALDInstitutionSite::Position(W,Site))<160*160;}),TEXT("Marked official routine stands on accessible settlement ground: ")+ALDInstitutionSite::Name(Site));}
        bool CoverBlocks=false;for(const auto& Geometry:D->Geometry){if(Geometry.Name.Contains(TEXT("Covered"))&&!Geometry.Floor){CoverBlocks|=!M->Vision->Ray({Geometry.Position-FVector(0,100,0),720,0},Geometry.Position+FVector(0,100,0));}}Check(CoverBlocks,TEXT("Authored covered-route geometry blocks actual sight"));
        for(int32 R=0;R<D->Routes.Num();++R){const auto& Route=D->Routes[R];if(Route.From==D->CityRegion&&(D->Node(Route.To)->Site==2||D->Node(Route.To)->Site==6)){RouteIds.Add(R);}}
        Check(RouteIds.Num()==4,TEXT("Both settlements retain public and covered economic route choices"));UGameplayStatics::SetGlobalTimeDilation(W,4);RouteIndex=-1;
    }
    else if(RouteIndex>=0)
    {
        const auto& Route=D->Routes[RouteIds[RouteIndex]];const FVector Position=B->GetActorLocation();Distance+=FVector::Dist2D(Position,Previous);Previous=Position;Sampled+=Delta;
        if(M->Vision->IsPublicSensorActive()&&M->Vision->Ray({M->Vision->PublicLocation(),600,1},Position)){Exposed+=Delta;}
        if(Now-Began>120){Check(false,TEXT("Native route timed out: ")+D->Node(Route.To)->Name+TEXT(" · ")+PC->Navigation->Status);Finish(TEXT("Native navigation failure"));return;}
        if(FVector::Dist2D(Position,Route.Points[Waypoint])<=65&&!PC->Navigation->IsFollowing())
        {
            ++Waypoint;if(Waypoint<Route.Points.Num()){PC->bFreshInputRequired=false;PC->Navigation->Navigate(Route.Points[Waypoint]);return;}
            auto T=MakeShared<FJsonObject>();T->SetStringField(TEXT("map"),D->Name);T->SetStringField(TEXT("destination"),D->Node(Route.To)->Name);T->SetBoolField(TEXT("covered"),Route.Covered);T->SetNumberField(TEXT("authoritySeconds"),Now-Began);T->SetNumberField(TEXT("distanceCm"),Distance);T->SetNumberField(TEXT("publicWatchExposedSeconds"),Exposed);T->SetNumberField(TEXT("sampledSeconds"),Sampled);T->SetNumberField(TEXT("walkSpeedCmPerSecond"),B->GetCharacterMovement()->MaxWalkSpeed);T->SetNumberField(TEXT("timeDilation"),4);Travel.Add(MakeShared<FJsonValueObject>(T));Check(Distance>500&&Now>Began,TEXT("Actual CharacterMovement completed ")+D->Node(Route.To)->Name+(Route.Covered?TEXT(" covered route"):TEXT(" public route")));
        }
        else{return;}
    }
    if(++RouteIndex>=RouteIds.Num()){Finish(TEXT("Saved map nav/sight and actual native click-navigation travel at 4x world time; endpoints placed between routes, no human route choice or remote input claim"));return;}
    const auto& Route=D->Routes[RouteIds[RouteIndex]];FVector Surface;PC->ClearControls();LDGround::MapSurface(W,Route.Points[0],Surface);B->SetActorLocation(Surface+FVector(0,0,92));B->GetCharacterMovement()->SetMovementMode(MOVE_Walking);PC->bFreshInputRequired=false;Waypoint=1;Began=Now;Distance=Exposed=Sampled=0;Previous=B->GetActorLocation();PC->Navigation->Navigate(Route.Points[Waypoint]);
#endif
}
