#include "Tests/LDB91Probe.h"
#include "Region/LDMapDefinition.h"
#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Navigation/LDNavigationComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "UnrealClient.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/World.h"

ALDB91Probe::ALDB91Probe(){PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
void ALDB91Probe::Check(bool Good,const FString& Name)
{auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("case"),Name);J->SetBoolField(TEXT("passed"),Good);Checks.Add(MakeShared<FJsonValueObject>(J));Failures+=!Good;UE_LOG(LogTemp,Display,TEXT("LD_B91_CHECK %s passed=%d"),*Name,Good);}
void ALDB91Probe::Next(int32 Value){Phase=Value;At=GetWorld()->GetTimeSeconds();Save(false);}
void ALDB91Probe::Issue(ELDWorldVerb Verb,uint32 Revision)
{
    Environment->Publish();const auto* E=PC->View.FindByPredicate([&](const auto& V){return V.PublicDoor==Rear->Id;});
    Request=FLDWorldRequest();Request.Match=PC->OwnerState.Match;Request.Life=PC->OwnerState.Life;Request.Sequence=++Token;
    Request.Observed=E?E->Handle:FGuid();Request.ObjectRevision=Revision?Revision:Rear->Revision;Request.Verb=Verb;Request.Contract=Environment->Contract;
    PC->ServerWorldInteraction(Request);
}
void ALDB91Probe::Tick(float Delta)
{
    Super::Tick(Delta);const double Now=GetWorld()->GetTimeSeconds();auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();
    if(!M||Phase==99){return;}if(Phase==0)
    {
        if(Now<7){return;}Environment=ALDEnvironmentDirector::Get(GetWorld());PC=Cast<ALDB91Controller>(GetWorld()->GetFirstPlayerController());
        if(!Environment||!Environment->Ready||!PC||!PC->GetPawn()){return;}
        AddTickPrerequisiteActor(Environment);const auto* D=ULDMapDefinition::Get(GetWorld());
        Check(Environment->Doors.Num()==72&&Environment->RoofGoals.Num()==16&&M->IsEnvironmentProfile()&&!M->Economy&&!M->WorldRuntime&&!M->WorldChecks,TEXT("Exact structural counts and isolated profile"));
        int32 Blocked=0,Opaque=0,Clear=0;FCollisionQueryParams Q(SCENE_QUERY_STAT(B91DoorFixture),false,PC->GetPawn());
        for(auto Weak:Environment->Doors)
        {
            auto* Door=Weak.Get();const FVector P=Door->GetActorLocation()-FVector(0,0,37.85);FHitResult Hit;
            const bool Closed=GetWorld()->SweepSingleByChannel(Hit,P+FVector(0,-180,0),P+FVector(0,180,0),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(28,90),Q);Blocked+=Closed;
            const bool Sight=!Environment->Ray(P+FVector(0,-180,0),P+FVector(0,180,0));Opaque+=Sight;
            Door->Apply(1,++Environment->MutationRevision);
            const bool Open=!GetWorld()->SweepSingleByChannel(Hit,P+FVector(0,-180,0),P+FVector(0,180,0),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(28,90),Q);Clear+=Open;
            if(!Closed||!Sight||!Open){UE_LOG(LogTemp,Warning,TEXT("LD_B91_DOOR_FIT %s closed=%d sight=%d open=%d blocker=%s"),*Door->Id.ToString(),Closed,Sight,Open,Hit.GetActor()?*Hit.GetActor()->GetName():TEXT("none"));}
        }
        UE_LOG(LogTemp,Display,TEXT("LD_B91_DOOR_COUNTS closed=%d sight=%d clear=%d"),Blocked,Opaque,Clear);
        const auto* First=Environment->Doors[0].Get();const uint32 Updates=First->ProxyUpdates;
        Environment->Doors[0]->Apply(First->State(),First->Revision);
        Check(First->ProxyUpdates==Updates,TEXT("Repeated same-revision snapshots do not dirty navigation"));
        Check(Blocked==72&&Opaque==72&&Clear==72,TEXT("All72 doors block capsule/sight closed and admit capsule open"));
        int32 Windows=0;for(const auto& B:D->Buildings){if(B.Enterable)
        {
            for(int32 Side:{-1,1}){const FVector A=B.Position+FVector(Side*900,0,92.15),Z=B.Position+FVector(Side*600,0,92.15);FHitResult Hit;
            Windows+=Environment->Ray(A,Z)&&GetWorld()->SweepSingleByChannel(Hit,A,Z,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(28,90),Q);}
            Starts.Add(B.Position+FVector(0,-1150,0));Goals.Add(B.Position+FVector(0,1150,0));Names.Add(B.Id.ToString()+TEXT("_interior"));
        }}Check(Windows==72,TEXT("All72 windows admit eye ray but reject native capsule"));
        Starts.Append(Environment->StairStarts);Goals.Append(Environment->RoofGoals);
        for(int32 I=0;I<16;++I){Names.Add(FString::Printf(TEXT("permanent_stairs_%02d"),I));}
        Rear=Environment->Door(TEXT("building_riverport_00_door_north"));Check(Rear!=nullptr,TEXT("Riverport rear has exact authored identity"));
        Next(1);return;
    }
    auto* Body=Cast<ALDCharacter>(PC->GetPawn());auto* S=Body?M->Combat->StateFor(Body):nullptr;if(!Body||!S){Check(false,TEXT("Body/state retained"));Save(true);FPlatformMisc::RequestExitWithStatus(false,3);Phase=99;return;}
    if(Phase==1)
    {
        if(Now-At<2){return;}PC->Navigation->Stop();Body->SetActorLocation(Starts[Index]+FVector(0,0,92),false,nullptr,ETeleportType::TeleportPhysics);
        Body->GetCharacterMovement()->SetMovementMode(MOVE_Falling);Next(2);return;
    }
    if(Phase==2)
    {
        if(Now-At<.5){return;}WalkAt=Now;Length=Falling=0;Last=Body->GetActorLocation();NextTrace=Now;Goal=Goals[Index];
        if(Index>=36)
        {auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());FNavLocation A,B;
         const bool Start=Nav->ProjectPointToNavigation(Starts[Index],A,FVector(100,100,50)),End=Nav->ProjectPointToNavigation(Goal,B,FVector(100,100,50));
         const auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),Body->GetNavAgentLocation(),Goal);
         UE_LOG(LogTemp,Display,TEXT("LD_B91_STAIR_PATH %d start=%d %s end=%d %s path=%d partial=%d"),Index-36,Start,*A.Location.ToString(),End,*B.Location.ToString(),Path&&Path->IsValid(),Path&&Path->IsPartial());}
        PC->Navigation->Navigate(Goal);Next(3);return;
    }
    if(Phase==3)
    {
        const FVector P=Body->GetActorLocation();Length+=FVector::Dist(P,Last);Last=P;if(Body->GetCharacterMovement()->IsFalling()){Falling+=Delta;}
        const bool Arrived=FVector::Dist2D(P,Goal)<60&&FMath::Abs(P.Z-90-Goal.Z)<20;
        if(Now>=NextTrace||Arrived){Trace+=FString::Printf(TEXT("%s,%.6f,%.3f,%.3f,%.3f,%.3f\n"),*Names[Index],Now-WalkAt,P.X,P.Y,P.Z,Length);NextTrace=Now+.1;}
        if(Arrived||Now-WalkAt>45)
        {
            const bool Good=Arrived&&Falling==0;Check(Good,Names[Index]);auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("case"),Names[Index]);J->SetBoolField(TEXT("passed"),Good);
            J->SetNumberField(TEXT("walk_seconds"),Now-WalkAt);J->SetNumberField(TEXT("walked_cm"),Length);J->SetNumberField(TEXT("falling_s"),Falling);Walks.Add(MakeShared<FJsonValueObject>(J));
            PC->Navigation->Stop();++Index;if(Index<Starts.Num()){Next(1);}else{Next(4);}return;
        }return;
    }
    if(Phase==4)
    {
        if(!Rear){Save(true);FPlatformMisc::RequestExitWithStatus(false,3);Phase=99;return;}
        Rear->Apply(0,++Environment->MutationRevision);Body->SetActorLocation(Rear->GetActorLocation()+FVector(0,85,-40));
        Body->GetCharacterMovement()->StopMovementImmediately();Next(5);return;
    }
    if(Phase==5)
    {
        if(Now-At<.5){return;}S->QuietSince=Now-7;Issue(ELDWorldVerb::Open,1);
        Check(PC->LastWorldReceipt.Result==2&&!Rear->Open,TEXT("Stale object revision changes nothing"));
        Issue(ELDWorldVerb::Lock);Check(PC->LastWorldReceipt.Result==2,TEXT("Contender cannot claim resident lock"));
        Check(!Environment->ResidentLock(Rear->Id,TEXT("wrong_resident"),true)&&Environment->ResidentLock(Rear->Id,Rear->Resident,true),TEXT("Resident lock permission is exact"));
        Issue(ELDWorldVerb::Open);Check(PC->LastWorldReceipt.Result==2&&!Rear->Open,TEXT("Locked rear rejects ordinary Open"));
        Issue(ELDWorldVerb::Lockpick);Next(6);return;
    }
    if(Phase==6||Phase==8||Phase==10||Phase==12)
    {
        Environment->Hold(PC,Request.Match,Request.Life,Request.Sequence,++Nonce,false);
        if(PC->LastWorldReceipt.Result==0&&Now-At<4){return;}
        if(Phase==6){Check(PC->LastWorldReceipt.Result==2&&Rear->Locked&&!Rear->Open,TEXT("Source-free lockpick is disabled by the control policy"));Environment->ResidentLock(Rear->Id,Rear->Resident,false);Issue(ELDWorldVerb::Open);Next(8);return;}
        if(Phase==8)
        {
            Check(PC->LastWorldReceipt.Result==1&&Rear->Open,TEXT("Held Open commits physical revision"));
            auto* Other=GetWorld()->SpawnActor<ALDCharacter>(Rear->GetActorLocation()-FVector(0,0,40),FRotator::ZeroRotator);Issue(ELDWorldVerb::Close);
            Check(PC->LastWorldReceipt.Result==2&&Rear->Open,TEXT("Occupied closure rejects without revision"));Other->Destroy();
            Issue(ELDWorldVerb::Close);Next(10);return;
        }
        if(Phase==10)
        {Check(PC->LastWorldReceipt.Result==1&&!Rear->Open,TEXT("Unoccupied Close commits"));Issue(ELDWorldVerb::Force);Next(12);return;}
        Check(PC->LastWorldReceipt.Result==2&&!Rear->Open&&!Rear->Broken,TEXT("Source-free force is disabled by the control policy"));
        // Explicit structural-state setup tests collision, not earned destruction.
        Rear->Apply(5,++Environment->MutationRevision);
        PriorRevision=Rear->Revision;PriorReceipt=PC->LastWorldReceipt.Receipt;PC->ServerWorldInteraction(Request);
        Check(Rear->Revision==PriorRevision&&PC->LastWorldReceipt.Receipt==PriorReceipt,TEXT("Duplicate sequence replays original receipt without mutation"));
        Issue(ELDWorldVerb::Close);Check(PC->LastWorldReceipt.Result==2&&Rear->Open,TEXT("Broken-open rear cannot close"));
        Check(Rear->Revision==Rear->CollisionRevision&&Rear->Revision==Rear->SightRevision&&Rear->Revision==Rear->NavigationRevision,TEXT("Collision sight and local-nav revision agree"));
        Rear=Environment->Door(TEXT("building_riverport_00_door_south"));Rear->Apply(0,++Environment->MutationRevision);
        Body->SetActorLocation(Rear->GetActorLocation()+FVector(0,85,-40));Body->GetCharacterMovement()->StopMovementImmediately();Next(14);return;
    }
    if(Phase==14)
    {
        if(Now-At<.5){return;}
        Check(!Environment->ResidentLock(Rear->Id,TEXT("resident:riverport"),true),TEXT("Public front cannot lock even through resident authority"));
        S->ProtectionUntil=Now+1;Issue(ELDWorldVerb::Open);Check(PC->LastWorldReceipt.Result==2,TEXT("Protection rejects world start"));S->ProtectionUntil=0;
        M->Combat->ApplyStatus(Body,LDTactical::EStatus::Stun,LDTactical::Milliseconds(Now+1),FGuid::NewGuid());
        Issue(ELDWorldVerb::Open);Check(PC->LastWorldReceipt.Result==2,TEXT("Stun rejects world start"));
        Next(15);return;
    }
    if(Phase==15)
    {
        if(Now-At<1.1){return;}
        M->Combat->ApplyStatus(Body,LDTactical::EStatus::Mute,LDTactical::Milliseconds(Now+20),FGuid::NewGuid());
        M->Combat->ApplyStatus(Body,LDTactical::EStatus::Silence,LDTactical::Milliseconds(Now+20),FGuid::NewGuid());
        Issue(ELDWorldVerb::Open);Next(16);return;
    }
    if(Phase==16)
    {
        Environment->Hold(PC,Request.Match,Request.Life,Request.Sequence,++Nonce,false);
        if(PC->LastWorldReceipt.Result==0&&Now-At<1){return;}
        Check(PC->LastWorldReceipt.Result==1&&Rear->Open,TEXT("Mute and Silence do not prohibit ordinary doors"));
        Rear->Apply(0,++Environment->MutationRevision);Issue(ELDWorldVerb::Open);Body->SetActorLocation(Body->GetActorLocation()+FVector(10,0,0));Next(17);return;
    }
    if(Phase==17)
    {
        if(Now-At<.5){return;}Check(PC->LastWorldReceipt.Result==2&&!Rear->Open,TEXT("Actual movement cancels held door without mutation"));
        const auto Previous=Request;Request.Sequence=++Token;Request.Life+=1;PC->ServerWorldInteraction(Request);
        Check(PC->LastWorldReceipt.Result==2&&!Rear->Open,TEXT("Stale life rejects without mutation"));Request=Previous;
        Save(true);
        if(FParse::Param(FCommandLine::Get(),TEXT("LDB91Capture")))
        {Body->SetActorLocation(ULDMapDefinition::Get(GetWorld())->Buildings.FindByPredicate([](const auto& B){return B.Id==TEXT("building_riverport_00");})->Position+FVector(0,-400,92));
         Body->CameraArm->TargetArmLength=2200;Next(97);return;}
        Phase=99;FPlatformMisc::RequestExitWithStatus(false,Failures?3:0);return;
    }
    if(Phase==97)
    {if(Now-At<3){return;}FString Output;FParse::Value(FCommandLine::Get(),TEXT("B91Output="),Output);FScreenshotRequest::RequestScreenshot(Output/TEXT("RiverportInterior.png"),true,false);Next(98);return;}
    if(Phase==98&&Now-At>1){Save(true);Phase=99;FPlatformMisc::RequestExitWithStatus(false,Failures?3:0);}
}
void ALDB91Probe::Save(bool Complete)
{
    FString Output;FParse::Value(FCommandLine::Get(),TEXT("B91Output="),Output);if(Output.IsEmpty()){return;}
    auto R=MakeShared<FJsonObject>();R->SetBoolField(TEXT("complete"),Complete);R->SetNumberField(TEXT("failures"),Failures);
    R->SetStringField(TEXT("stage"),TEXT("B91"));R->SetStringField(TEXT("scope"),TEXT("Structural/door increment only; later B91 object, foliage, traversal and network gates remain open"));
    R->SetStringField(TEXT("contract"),Environment?Environment->Contract:TEXT(""));R->SetArrayField(TEXT("checks"),Checks);R->SetArrayField(TEXT("walks"),Walks);
    FString Json;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Json));
    FFileHelper::SaveStringToFile(Json,*(Output/TEXT("Environment.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    FFileHelper::SaveStringToFile(Trace,*(Output/TEXT("EnvironmentTrace.csv")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
