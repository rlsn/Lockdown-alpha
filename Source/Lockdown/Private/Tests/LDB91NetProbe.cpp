#include "Tests/LDB91NetProbe.h"
#include "World/LDEnvironmentDirector.h"
#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/World.h"

ALDB91NetProbe::ALDB91NetProbe(){PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;StartWall=FPlatformTime::Seconds();}
void ALDB91NetProbe::Check(bool Good,const FString& Name)
{auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("case"),Name);J->SetBoolField(TEXT("passed"),Good);Checks.Add(MakeShared<FJsonValueObject>(J));Failures+=!Good;UE_LOG(LogTemp,Display,TEXT("LD_B91_NET %s passed=%d"),*Name,Good);}
void ALDB91NetProbe::Place(ALDB91Controller* PC,FVector Offset)
{auto* Body=Cast<ALDCharacter>(PC->GetPawn());Body->SetActorLocation(Door->GetActorLocation()+Offset);Body->GetCharacterMovement()->StopMovementImmediately();PC->ClientSetLocation(Body->GetActorLocation(),Body->GetActorRotation());}
void ALDB91NetProbe::Task(uint8 Value)
{
    Phase=Value;At=GetWorld()->GetTimeSeconds();Environment->Publish();
    if(Value==2||Value==6||Value==7){B->ClientB91ProbeTask(Value);}else {A->ClientB91ProbeTask(Value);}
    if(Value==8){B->ClientB91ProbeTask(Value);}Save(false);
}
void ALDB91NetProbe::Tick(float Delta)
{
    Super::Tick(Delta);const double Now=GetWorld()->GetTimeSeconds();if(Phase==99){return;}
    if(Failures){Save(true);Phase=99;FPlatformMisc::RequestExitWithStatus(false,3);return;}
    if(FPlatformTime::Seconds()-StartWall>180){Check(false,TEXT("Network scenario timed out"));Save(true);Phase=99;FPlatformMisc::RequestExitWithStatus(false,3);return;}
    if(Phase==0)
    {
        if(Now<3){return;}for(FConstPlayerControllerIterator I=GetWorld()->GetPlayerControllerIterator();I;++I)
        {if(auto* PC=Cast<ALDB91Controller>(I->Get());PC&&PC->GetPawn()){if(PC->EnvironmentProbeRole==1){A=PC;}if(PC->EnvironmentProbeRole==2){B=PC;}}}
        if(!A||!B){return;}Environment=ALDEnvironmentDirector::Get(GetWorld());Door=Environment?Environment->Door(TEXT("building_riverport_00_door_north")):nullptr;if(!Door){return;}
        Check(A!=B&&!A->IsLocalController()&&!B->IsLocalController(),TEXT("Two independent remote owners"));
        Door->Apply(0,++Environment->MutationRevision);Place(A,FVector(0,85,-37.85));Place(B,FVector(0,-85,-37.85));
        Phase=90;At=Now;return;
    }
    if(Phase==90){if(Now-At>1){Task(1);}return;}
    if(Phase==91){if(Now-At>1){Task(7);}return;}
    if(Phase==2&&B->GetPawn()->GetActorLocation().Y>Door->GetActorLocation().Y+200){Through=true;}
    auto Read=[&](ALDB91Controller* PC,uint8 Key)->TSharedPtr<FJsonObject>
    {const auto* Text=PC->EnvironmentProbeReports.Find(Key);TSharedPtr<FJsonObject> J;if(Text){FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(*Text),J);}return J;};
    const auto R=Read(Phase==2||Phase==6||Phase==7?B:A,uint8(Phase));if(!R){return;}
    if(Phase==8&&!Read(B,8)){return;}
    Check(R->GetBoolField(TEXT("completed")),FString::Printf(TEXT("Remote phase %d completed"),Phase));
    if(Phase==1)
    {Check(Door->Open&&R->GetIntegerField(TEXT("receipt_result"))==1&&R->GetBoolField(TEXT("duplicate_receipt_same")),TEXT("Remote Open and duplicate replay"));Task(2);return;}
    if(Phase==2)
    {Check(Through&&!Door->Open&&R->GetIntegerField(TEXT("receipt_result"))==1,TEXT("Other remote body paths through opening then closes"));Task(3);return;}
    if(Phase==3){Check(Door->Open,TEXT("Remote reopen observed"));PriorRevision=Door->Revision;Place(B,FVector(0,0,-37.85));Task(4);return;}
    if(Phase==4)
    {Check(Door->Open&&Door->Revision==PriorRevision&&R->GetIntegerField(TEXT("receipt_result"))==2,TEXT("Occupied remote Close rejects without revision"));Place(B,FVector(4000,0,-37.85));Task(5);return;}
    if(Phase==5){Check(!Door->Open&&R->GetIntegerField(TEXT("receipt_result"))==1,TEXT("Local owner closes after other observer leaves"));Task(6);return;}
    if(Phase==6)
    {Check(!R->GetBoolField(TEXT("door_observed")),TEXT("Distant client receives no fogged door mutation"));Place(B,FVector(0,-85,-37.85));Phase=91;At=Now;return;}
    if(Phase==7)
    {Check(R->GetBoolField(TEXT("door_observed"))&&R->GetIntegerField(TEXT("door_revision"))==Door->Revision&&!R->GetIntegerField(TEXT("door_state")),TEXT("Reobservation receives current closed revision"));PriorRevision=Door->Revision;Task(8);return;}
    if(Phase==8)
    {
        const auto Other=Read(B,8);if(!Other){return;}
        Check(Other->GetBoolField(TEXT("completed")),TEXT("Second race response completes"));
        const int32 Wins=int32(R->GetIntegerField(TEXT("receipt_result"))==1)+int32(Other->GetIntegerField(TEXT("receipt_result"))==1);
        Check(Wins==1&&Door->Open&&Door->Revision==PriorRevision+1,TEXT("Concurrent door requests commit exactly once"));
        Check(Door->Revision==Door->CollisionRevision&&Door->Revision==Door->SightRevision&&Door->Revision==Door->NavigationRevision,TEXT("Remote mutation proxies share revision"));
        Save(true);Phase=99;FPlatformMisc::RequestExitWithStatus(false,Failures?3:0);return;
    }
}
void ALDB91NetProbe::Save(bool Complete)
{
    FString Output;FParse::Value(FCommandLine::Get(),TEXT("B91Output="),Output);if(Output.IsEmpty()){return;}
    auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("stage"),TEXT("B91"));R->SetBoolField(TEXT("complete"),Complete);R->SetNumberField(TEXT("failures"),Failures);
    R->SetNumberField(TEXT("phase"),Phase);R->SetStringField(TEXT("contract"),Environment?Environment->Contract:TEXT(""));R->SetArrayField(TEXT("checks"),Checks);
    R->SetStringField(TEXT("scope"),TEXT("Two independent owning clients; real clocks, native RPC/CharacterMovement, door race and restricted received snapshots. No video/performance/full-stage acceptance."));
    FString Json;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Json));FFileHelper::SaveStringToFile(Json,*(Output/TEXT("Network.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
