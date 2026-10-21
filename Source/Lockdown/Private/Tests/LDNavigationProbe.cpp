#include "Tests/LDNavigationProbe.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Combat/LDCombatDirector.h"
#include "Vision/LDWorldViewRelay.h"
#include "Vision/LDVisionDirector.h"
#include "Region/LDRegionDefinition.h"
#include "GameFramework/SpringArmComponent.h"
#include "NavigationSystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Components/BoxComponent.h"
#include "Development/LDDevelopmentDirector.h"

ALDNavigationProbe::ALDNavigationProbe(){PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;bReplicates=false;}
void ALDNavigationProbe::BeginPlay()
{
    Super::BeginPlay();Started=PhaseAt=FPlatformTime::Seconds();FParse::Value(FCommandLine::Get(),TEXT("LDProbeRole="),Role);
    if(GetNetMode()==NM_Client){CastChecked<ALDPlayerController>(GetOwner())->ServerProbeHello(Role==TEXT("A")?1:2);}
}
void ALDNavigationProbe::Check(bool Pass,const FString& Text)
{Checks.Add((Pass?TEXT("PASS "):TEXT("FAIL "))+Text);if(!Pass){++Failures;}UE_LOG(LogTemp,Display,TEXT("LD_NAV_PROBE %s"),*Checks.Last());}
void ALDNavigationProbe::Enter(int32 Next)
{Phase=Next;PhaseAt=FPlatformTime::Seconds();Step=0;A->ClientProbePhase(uint8(Next));B->ClientProbePhase(uint8(Next));}
void ALDNavigationProbe::Place(const FVector& First,const FVector& Second)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();int32 I=0;
    for(auto P:{A,B})
    {
        auto* Body=CastChecked<ALDCharacter>(P->GetPawn());Body->SetActorLocation(I++?Second:First,false,nullptr,ETeleportType::TeleportPhysics);
        Body->GetCharacterMovement()->StopMovementImmediately();Body->SetSprint(false);
        auto* O=M->StateFor(P.Get());O->ProtectedUntil=0;M->Combat->StateFor(Body)->ProtectionUntil=0;M->SyncOwner(P.Get());
    }
}
void ALDNavigationProbe::Tick(float Delta)
{
    Super::Tick(Delta);
#if !UE_BUILD_SHIPPING
    if(FPlatformTime::Seconds()-Started>210){Check(false,TEXT("Navigation probe timed out"));Finish();return;}
    if(GetNetMode()==NM_Client){ClientTick();}else{ServerTick();}
#endif
}
void ALDNavigationProbe::ServerTick()
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* G=GetWorld()->GetGameState<ALDGameState>();
    const bool Region=LDRegion::Definition(GetWorld())!=nullptr;
    if(!Phase)
    {
        for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
        {if(auto* PC=Cast<ALDPlayerController>(It->Get());PC&&PC->GetPawn()){if(PC->ProbeRole==1){A=PC;}if(PC->ProbeRole==2){B=PC;}}}
        if(A.IsValid()&&B.IsValid()&&!UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(this)){Check(G->bUIControlProfile&&G->LobbyCount==2,TEXT("V05 retains two independent private lobby contenders"));Enter(1);}return;
    }
    if(Phase==14)
    {
        const double E=FPlatformTime::Seconds()-PhaseAt;
        if(!Step&&E>2.5){for(auto PC:{A,B}){Check(PC.IsValid()&&!PC->Navigation->IsAuthorityActive(),TEXT("New-life destination completes under new order identity"));if(PC.IsValid()){ServerQueries+=PC->Navigation->QueryCount;ServerRejectedMoves+=PC->Navigation->RejectedMoves;}}Step=1;}
        if(E>4){Finish();}return;
    }
    if(!A.IsValid()||!B.IsValid()){Check(false,TEXT("Client disconnected before navigation gate"));Finish();return;}
    const double E=FPlatformTime::Seconds()-PhaseAt;
    auto* AB=Cast<ALDCharacter>(A->GetPawn());auto* BB=Cast<ALDCharacter>(B->GetPawn());
    if(Phase==1&&G->bMatchStarted&&E>1.5)
    {
        Check(M->IsPlaying(),TEXT("Real owning lobby RPCs start the retained match"));
        Place(Region?FVector(-3000,-1450,92):FVector(-1300,-450,92),Region?FVector(-3000,-1350,92):FVector(-1300,-350,92));Enter(2);
    }
    else if(Phase==2&&E>10)
    {
        for(auto PC:{A,B}){const FVector Goal=Region?FVector(-2500,PC==A?-1450:-1350,0):FVector(-500,PC==A?-450:-350,0);Check(FVector::Dist2D(PC->GetPawn()->GetActorLocation(),Goal)<=45&&PC->Navigation->Status!=TEXT("Path blocked"),TEXT("Independent owner routes around authored wall/tree and arrives"));}
        Place(FVector(-1700,-1000,92),FVector(-1700,-850,92));Enter(3);
    }
    else if(Phase==3&&E>5.5)
    {
        for(auto PC:{A,B}){Check(FMath::Abs(PC->GetPawn()->GetActorLocation().X+600)<=45,TEXT("Predicted sprint reaches native destination without teleport"));}
        Place(FVector(-1700,-1000,92),FVector(-1700,-850,92));Enter(4);
    }
    else if(Phase==4&&E>5)
    {
        for(auto PC:{A,B}){Check(!PC->Navigation->IsAuthorityActive()&&PC->GetPawn()->GetVelocity().Size2D()<1,TEXT("Supersede and Stop defeat delayed order replay and native saved moves"));}
        Place(Region?FVector(520,-2100,92):FVector(120,-400,92),Region?FVector(520,-2040,92):FVector(120,-340,92));Enter(5);
    }
    else if(Phase==5&&E>8)
    {
        for(auto PC:{A,B}){Check(PC->GetPawn()->GetActorLocation().Z>275&&PC->GetPawn()->GetActorLocation().X>(Region?1150:750),TEXT("Independent native path follows 200 cm ramp onto intended plateau"));}
        Enter(6);
    }
    else if(Phase==6&&E>2.5)
    {
        for(auto PC:{A,B}){Check(!PC->Navigation->IsAuthorityActive(),TEXT("Wrong-floor and forged observed-handle requests cannot create movement"));}
        Place(FVector(-1700,-1000,92),FVector(-1700,-500,92));
        TargetA=GetWorld()->SpawnActor<ALDResident>(FVector(-1400,-1000,92),FRotator::ZeroRotator);
        TargetB=GetWorld()->SpawnActor<ALDResident>(FVector(-1400,-500,92),FRotator::ZeroRotator);
        for(auto T:{TargetA,TargetB}){T->SetActorTickEnabled(false);if(T->GetController()){T->GetController()->Destroy();}T->GetCharacterMovement()->DisableMovement();}
        Enter(7);
    }
    else if(Phase==7)
    {
        if(!Step&&E>1.3){TargetA->SetActorLocation(FVector(-1200,-1000,92));TargetB->SetActorLocation(FVector(-1200,-500,92));Step=1;}
        if(E>4)
        {for(auto PC:{A,B}){Check(FMath::Abs(PC->GetPawn()->GetActorLocation().X+1488)<35&&!PC->Navigation->IsAuthorityActive(),TEXT("Observed-body approach stops at snapshot without chase or attack"));}Place(FVector(-1700,-1000,92),FVector(-1700,-500,92));TargetA->SetActorLocation(FVector(-1150,-1000,92));TargetB->SetActorLocation(FVector(-1150,-500,92));Enter(8);}
    }
    else if(Phase==8)
    {
        if(!Step&&E>1.5){TargetA->SetActorLocation(FVector(2000,1000,92));TargetB->SetActorLocation(FVector(2000,800,92));Step=1;}
        if(E>3.5)
        {
            for(auto PC:{A,B}){Check(!PC->Navigation->IsAuthorityActive(),TEXT("Loss of observation cancels target approach"));}
            Place(FVector(-1700,-1000,92),FVector(-1700,-500,92));
            for(double Y:{-1000.,-500.}){auto* Book=GetWorld()->SpawnActor<ALDBook>();Book->State=0;Book->Skill=ELDSkill::Spark;Book->SetActorLocation(FVector(-1400,Y,16));PickupBooks.Add(Book);}
            Enter(17);
        }
    }
    else if(Phase==17&&E>4)
    {
        int32 I=0;for(auto PC:{A,B}){auto* Book=PickupBooks[I++].Get();const double Distance=Book?FVector::Dist2D(PC->GetPawn()->GetActorLocation(),Book->GetActorLocation()):DBL_MAX;Check(Book&&Book->State==0&&Distance<=64,FString::Printf(TEXT("Pickup approach stops within 64 cm without automatic loot: %.2f cm"),Distance));}
        for(auto Book:PickupBooks){if(Book.IsValid()){Book->Destroy();}}PickupBooks.Reset();Place(FVector(-1700,-1000,92),FVector(-1700,-850,92));Enter(9);
    }
    else if(Phase==9&&E>4)
    {for(auto PC:{A,B}){Check(!PC->Navigation->IsAuthorityActive(),TEXT("Missing navigation heartbeat expires authority order in two seconds"));}QueryStart=A->Navigation->QueryCount+B->Navigation->QueryCount;Enter(10);}
    else if(Phase==10&&E>2.5)
    {Check(A->Navigation->QueryCount+B->Navigation->QueryCount-QueryStart<=24,TEXT("Rapid destination requests coalesce with bounded native query work"));Enter(11);}
    else if(Phase==11&&E>3.5)
    {
        for(auto PC:{A,B}){Check(!PC->Navigation->IsAuthorityActive()&&PC->GetPawn()->GetVelocity().Size2D()<1,TEXT("Focus loss cancels path; focus regain remains inert"));}
        Place(FVector(-1700,-1000,92),FVector(-1700,-850,92));
        for(auto PC:{A,B}){auto* O=M->StateFor(PC.Get());O->Build.Ranks[1]=1;O->Build.Slots[1]=ELDSkill::Step;O->Build.Cooldowns[1]=0;++O->Build.Revision;M->SyncOwner(PC.Get());}
        Enter(12);
    }
    else if(Phase==12&&E>4.5)
    {
        for(auto PC:{A,B}){Check(M->StateFor(PC.Get())->Build.Cooldowns[1]>0&&FMath::Abs(PC->GetPawn()->GetActorLocation().X+600)<=45,TEXT("Native Step and sprint remain integrated with destination following"));}
        Place(FVector(-1700,-1000,92),FVector(-1700,-500,92));
        ReplanStart=A->Navigation->Replans+B->Navigation->Replans;Enter(15);
    }
    else if(Phase==15)
    {
        if(!Step&&E>1)
        {
            for(double Y:{-1000.,-500.})
            {
                auto* Actor=GetWorld()->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Actor);
                Actor->SetRootComponent(Box);Box->SetBoxExtent(FVector(30,180,120));Box->SetCollisionObjectType(ECC_WorldStatic);Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);Box->SetCollisionResponseToAllChannels(ECR_Block);Box->SetCanEverAffectNavigation(false);Box->RegisterComponent();Actor->SetActorLocation(FVector(-1500,Y,100));Blockers.Add(Actor);
            }
            Step=1;
        }
        if(E>7)
        {
            for(auto PC:{A,B}){Check(!PC->Navigation->IsAuthorityActive()&&PC->GetPawn()->GetActorLocation().X<-1530,TEXT("Late physical blocker stops route after one replan without passing through collision"));}
            Check(A->Navigation->Replans+B->Navigation->Replans-ReplanStart==2,TEXT("Authority performs exactly one replan per stalled route"));
            for(auto Actor:Blockers){if(Actor.IsValid()){Actor->Destroy();}}Blockers.Reset();
            Place(FVector(-1700,-1000,600),FVector(-1700,-500,600));
            for(auto PC:{A,B}){CastChecked<ALDCharacter>(PC->GetPawn())->GetCharacterMovement()->SetMovementMode(MOVE_Falling);}
            Enter(16);
        }
    }
    else if(Phase==16&&E>3)
    {
        for(auto PC:{A,B})
        {
            Check(PC->GetPawn()->GetActorLocation().Z<120,TEXT("Cancelled movement preserves native gravity while falling"));
            APawn* OldBody=PC->GetPawn();M->EndContenderLife(PC.Get(),GetWorld()->GetTimeSeconds());if(OldBody){OldBody->Destroy();}
        }
        Enter(13);
    }
    else if(Phase==13&&AB&&BB&&E>24)
    {
        for(auto PC:{A,B}){Check(M->StateFor(PC.Get())->Life==2&&M->StateFor(PC.Get())->bActive&&!PC->Navigation->IsAuthorityActive(),TEXT("Ordinary return deadline creates a fresh life without stale route"));}
        Enter(14);
    }
}
void ALDNavigationProbe::ClientTick()
{
    auto* PC=Cast<ALDPlayerController>(GetOwner());if(!PC||!PC->OwnerState.Match){return;}
    auto* N=PC->Navigation.Get();auto* Body=Cast<ALDCharacter>(PC->GetPawn());
    if(FPlatformTime::Seconds()-LastPing>.3){LastPing=FPlatformTime::Seconds();PC->ServerProbePing(LastPing);}
    if(PC->ProbePhase!=Phase){Phase=PC->ProbePhase;PhaseAt=FPlatformTime::Seconds();Step=0;bSent=false;MaxSpeed=0;}
    const double E=FPlatformTime::Seconds()-PhaseAt;const bool Region=LDRegion::Definition(GetWorld())!=nullptr;const bool First=Role==TEXT("A");
    if(Body)
    {
        MaxSpeed=FMath::Max(MaxSpeed,Body->GetVelocity().Size2D());
        const auto* Move=CastChecked<ULDCharacterMovementComponent>(Body->GetCharacterMovement());
        if(CorrectionBody.Get()!=Body){CorrectionBody=Body;LastCorrections=0;}
        const uint32 NewCorrections=Move->CorrectionCount-LastCorrections;
        if(E>.65&&NewCorrections)
        {RegularCorrections+=NewCorrections;MaxRegularCorrection=FMath::Max(MaxRegularCorrection,Move->LastCorrectionCm);PhaseCorrections.FindOrAdd(Phase)+=NewCorrections;auto& Maximum=PhaseMaxCorrection.FindOrAdd(Phase);Maximum=FMath::Max(Maximum,Move->LastCorrectionCm);}
        Corrections+=NewCorrections;LastCorrections=Move->CorrectionCount;MaxCorrection=FMath::Max(MaxCorrection,Move->MaxCorrectionCm);
    }
    if(Phase==1)
    {if(!bSent){PC->ChooseLobbyClass(1);bSent=true;}if(!bReady&&PC->OwnerState.Build.ClassId==1&&E>.4){PC->ToggleLobbyReady();bReady=true;}if(PC->OwnerState.bLobbyHost&&PC->OwnerState.bReady&&E>1){PC->StartLobby();}return;}
    if(!Body&&Phase!=13){return;}
    if(Phase==2&&!bSent&&E>.7)
    {N->Navigate(Region?FVector(-2500,First?-1450:-1350,0):FVector(-500,First?-450:-350,0));bSent=true;}
    if(Phase==2&&!Step&&E>9){Check(!N->IsFollowing()&&N->QueryCount>0&&PC->NavigationBindingsActive(),TEXT("Owning client installs V05-only controls and completes its static native route"));Step=1;}
    if(Phase==3)
    {if(!bSent&&E>.7){Body->SetSprint(true);N->Navigate(FVector(-600,First?-1000:-850,0));bSent=true;}if(!Step&&E>5){Check(MaxSpeed>280&&MaxSpeed<310,TEXT("Native compressed sprint respects actual derived speed cap"));Body->SetSprint(false);Step=1;}}
    if(Phase==4)
    {
        if(!bSent&&E>.7){N->Navigate(FVector(-600,First?-1000:-850,0));bSent=true;}
        if(Step==0&&E>1.4){Old=N->LocalIdentity();N->Navigate(FVector(-2100,First?-1000:-850,0));Step=1;}
        if(Step==1&&E>2.1){N->Stop();Anchor=Body->GetActorLocation();N->ClientOrder_Implementation(Old,1,FVector(-600,First?-1000:-850,0),40,TEXT("Delayed old acceptance fixture"));N->ServerOrder(Old,FVector(-600,First?-1000:-850,0),FGuid(),0);Step=2;}
        if(Step==2&&E>4){Check(!N->IsFollowing()&&FVector::Dist2D(Anchor,Body->GetActorLocation())<20,TEXT("Stop epoch ignores stale acceptance and old server order"));Step=3;}
    }
    if(Phase==5&&!bSent&&E>.7){N->Navigate(Region?FVector(1300,First?-2100:-2040,200):FVector(900,First?-400:-340,200));bSent=true;}
    if(Phase==5&&!Step&&E>7){Check(!N->IsFollowing()&&Body->GetActorLocation().Z>275,TEXT("Owning prediction reaches plateau through the ramp"));Step=1;}
    if(Phase==6)
    {
        if(!bSent&&E>.5){N->Navigate(Region?FVector(1400,-2100,0):FVector(1000,-400,0));bSent=true;}
        if(!Step&&E>1.2){Check(!N->IsFollowing()&&PC->IntentFeedback.Contains(TEXT("No walkable route")),TEXT("Projection cannot snap ground command up to another floor"));N->Navigate(Body->GetNavAgentLocation()+FVector(200,0,0),FGuid::NewGuid());Step=1;}
        if(Step==1&&E>2){Check(!N->IsFollowing()&&PC->IntentFeedback.Contains(TEXT("No walkable route")),TEXT("Unknown observed handle is rejected without target tracking"));Step=2;}
    }
    if((Phase==7||Phase==8||Phase==17)&&!bSent&&E>.7)
    {
        const FVector Expected(Phase==8?-1150:-1400,First?-1000:-500,92);
        if(const auto* R=PC->LocalView();R&&!R->IsStale())
        {for(const auto& X:R->LastView().Entities){if(X.Kind==(Phase==17?ELDObservedKind::Book:ELDObservedKind::Body)&&FVector::Dist2D(X.Position,Expected)<20){N->Navigate(X.Position,X.Id);bSent=true;break;}}}
    }
    if(Phase==17&&!Step&&E>3.5){Check(bSent&&!N->IsFollowing(),TEXT("Observed pickup uses a reachable native endpoint with bounded arrival"));Step=1;}
    if(Phase==9&&!bSent&&E>.5)
    {N->Stop();Old=N->LocalIdentity();++Old.Order;N->ServerOrder(Old,FVector(-600,First?-1000:-850,0),FGuid(),0);bSent=true;}
    if(Phase==10&&!bSent&&E>.5)
    {for(int32 I=0;I<100;++I){N->Navigate(FVector(-1500+I,First?-1000:-850,0));}bSent=true;}
    if(Phase==11)
    {if(!bSent&&E>.5){N->Navigate(FVector(-600,First?-1000:-850,0));bSent=true;}if(!Step&&E>1.2){PC->FlushPressedKeys();Anchor=Body->GetActorLocation();Step=1;}if(Step==1&&E>2.5){Check(!N->IsFollowing()&&FVector::Dist2D(Anchor,Body->GetActorLocation())<20,TEXT("Focus flush stops locally without replay"));Step=2;}}
    if(Phase==12)
    {if(!bSent&&E>.7){Body->SetSprint(true);N->Navigate(FVector(-600,First?-1000:-850,0));bSent=true;}if(!Step&&E>1.5){PC->ServerSkillAction(PC->OwnerState.Match,PC->OwnerState.Life,100000,PC->OwnerState.Build.Revision,1,FVector::ForwardVector);Step=1;}}
    if(Phase==13&&Body&&PC->OwnerState.Life==2&&!bSent)
    {
        const FVector Returned=Body->GetActorLocation();N->ClientOrder_Implementation(Old,1,FVector(-600,-1000,0),40,TEXT("Old life fixture"));
        Check(!N->IsFollowing(),TEXT("Old-life acknowledgement cannot restore destination"));
        N->PanTo(Returned+FVector(200,200,0));
        Check(!N->bFollowCamera&&Body->GetActorLocation().Equals(Returned),TEXT("Camera manual pan leaves follow without changing controlled body"));
        N->Recenter(true);N->Recenter(false);N->ToggleFollow();Check(N->bFollowCamera,TEXT("Home restores persistent follow mode"));
        bSent=true;
    }
    if(Phase==14&&!bSent&&E>.4){Anchor=Body->GetActorLocation();N->Navigate(Body->GetNavAgentLocation()+FVector(200,0,0));bSent=true;}
    if(Phase==15&&!bSent&&E>.7){Body->SetSprint(false);N->Navigate(FVector(-900,First?-1000:-500,0));bSent=true;}
    if(Phase==15&&!Step&&E>6){Check(!N->IsFollowing()&&PC->IntentFeedback.Contains(TEXT("Path blocked")),TEXT("Late physical obstruction yields bounded Path blocked outcome"));Step=1;}
    if(Phase==16&&!bSent&&E>.7){N->Stop();bSent=true;}
    if(Phase==16&&!Step&&E>2.5){Check(Body->GetActorLocation().Z<120&&Body->GetCharacterMovement()->IsMovingOnGround(),TEXT("Stop and stale-move rejection do not suspend gravity"));Step=1;}
    if(Phase==14&&E>3)
    {
        Check(FVector::Dist2D(Anchor,Body->GetActorLocation())>140&&!N->IsFollowing(),TEXT("Fresh explicit navigation works after ordinary respawn"));
        int32 OtherBodies=0;for(TActorIterator<ALDCharacter> It(GetWorld());It;++It){OtherBodies+=*It!=PC->GetPawn();}
        Check(OtherBodies==0,TEXT("Restricted owner has no other canonical bodies or hidden navigation targets"));
        Check(PC->OwnerState.Life==2&&PC->OwnerState.bActive,TEXT("Owning client receives actual respawn and remains idle"));Finish();
    }
}
void ALDNavigationProbe::Finish()
{
    SetActorTickEnabled(false);auto Report=MakeShared<FJsonObject>();
    Report->SetStringField(TEXT("profile"),TEXT("B51 independent-client engineering probe; setup positions/build/life termination are labelled fixtures; native movement and ordinary return timers remain real"));
    if(GetNetMode()==NM_Client)
    {
        const auto* PC=CastChecked<ALDPlayerController>(GetOwner());auto RTT=PC->ProbeRtts;RTT.Sort();
        Check(RTT.Num()>=8,TEXT("Independent client measured native transport echo"));Report->SetNumberField(TEXT("medianEchoRttMs"),RTT.IsEmpty()?0:RTT[RTT.Num()/2]);
        Report->SetNumberField(TEXT("correctionsIncludingFixturePlacements"),Corrections);Report->SetNumberField(TEXT("maxCorrectionIncludingFixturePlacementsCm"),MaxCorrection);
        Report->SetNumberField(TEXT("correctionsOutsideFixtureSettling"),RegularCorrections);Report->SetNumberField(TEXT("maxSampledCorrectionOutsideFixtureSettlingCm"),MaxRegularCorrection);
        Report->SetNumberField(TEXT("nativeQueries"),PC->Navigation->QueryCount);Report->SetNumberField(TEXT("queryMs"),PC->Navigation->QueryMilliseconds);
        auto Ack=PC->Navigation->AcknowledgementMs;Ack.Sort();Report->SetNumberField(TEXT("moveAcknowledgementP95Ms"),Ack.IsEmpty()?0:Ack[FMath::Min(Ack.Num()-1,FMath::CeilToInt(Ack.Num()*.95)-1)]);
        auto Acceptance=PC->Navigation->RouteAcceptanceMs;Acceptance.Sort();Report->SetNumberField(TEXT("routeAcceptanceP95Ms"),Acceptance.IsEmpty()?0:Acceptance[FMath::Min(Acceptance.Num()-1,FMath::CeilToInt(Acceptance.Num()*.95)-1)]);
        TArray<TSharedPtr<FJsonValue>> CorrectionPhases;
        for(const auto& Pair:PhaseCorrections){auto Entry=MakeShared<FJsonObject>();Entry->SetNumberField(TEXT("phase"),Pair.Key);Entry->SetNumberField(TEXT("count"),Pair.Value);Entry->SetNumberField(TEXT("maxSampledCm"),PhaseMaxCorrection.FindRef(Pair.Key));CorrectionPhases.Add(MakeShared<FJsonValueObject>(Entry));}
        Report->SetArrayField(TEXT("correctionsByPhaseOutsideInitialSettling"),CorrectionPhases);
    }
    else {Report->SetNumberField(TEXT("nativeQueries"),ServerQueries);Report->SetNumberField(TEXT("rejectedSavedMoves"),ServerRejectedMoves);}
    Report->SetNumberField(TEXT("failures"),Failures);TArray<TSharedPtr<FJsonValue>> Values;for(const auto& S:Checks){Values.Add(MakeShared<FJsonValueString>(S));}Report->SetArrayField(TEXT("checks"),Values);
    FString Directory,Output;FParse::Value(FCommandLine::Get(),TEXT("LDProbeReportDir="),Directory);FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Output));
    if(!FFileHelper::SaveStringToFile(Output,*FPaths::Combine(Directory,Role+TEXT(".json")))){++Failures;}
    FPlatformMisc::RequestExitWithStatus(false,Failures?1:0);
}
