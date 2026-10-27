#include "World/LDEnvironmentDirector.h"
#include "Navigation/LDNavigationComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"
#include "UnrealClient.h"
#include "HAL/FileManager.h"

namespace
{
bool Probe(){return !UE_BUILD_SHIPPING&&FParse::Param(FCommandLine::Get(),TEXT("LDB91NetProbe"));}
}
void ALDB91Controller::ServerB91ProbeHello_Implementation(uint8 ProbeIdentity)
{if(Probe()&&ProbeIdentity>=1&&ProbeIdentity<=2&&!EnvironmentProbeRole){EnvironmentProbeRole=ProbeIdentity;}}
void ALDB91Controller::ServerB91ProbeAck_Implementation(uint8 Phase,const FString& Report)
{if(Probe()&&EnvironmentProbeRole&&Phase>=1&&Phase<=8&&Report.Len()<65536){EnvironmentProbeReports.Add(Phase,Report);}}
void ALDB91Controller::ClientB91ProbeTask_Implementation(uint8 Phase)
{if(Probe()&&Phase>=1&&Phase<=8){ProbeTask=Phase;ProbeStep=ProbeRetries=0;ProbeSent=false;ProbeAt=ProbeRetryAt=GetWorld()->GetTimeSeconds();ProbeReceipt.Invalidate();ToolProbeDevToken=ToolProbeDevNonce=0;}}
void ALDB91Controller::TickB91Probe()
{
    if(!Probe()||GetNetMode()!=NM_Client||!IsLocalController()||!GetPawn()||!OwnerState.Match){return;}
    if(!EnvironmentProbeRole)
    {FString ProbeLabel;FParse::Value(FCommandLine::Get(),TEXT("B91Role="),ProbeLabel);EnvironmentProbeRole=ProbeLabel==TEXT("A")?1:2;ServerB91ProbeHello(EnvironmentProbeRole);}
    if(FParse::Param(FCommandLine::Get(),TEXT("LDB91ToolsProbe"))){TickB91ToolsProbe();return;}
    if(FParse::Param(FCommandLine::Get(),TEXT("LDB91ObjectsProbe"))){TickB91ObjectsProbe();return;}
    const double Now=GetWorld()->GetTimeSeconds();
    if(FParse::Param(FCommandLine::Get(),TEXT("LDB91NetCapture"))&&Now-ProbeCaptureAt>=.2
       &&(ProbeFrame||View.ContainsByPredicate([](const auto& Entry){return Entry.PublicDoor==TEXT("building_riverport_00_door_north");})))
    {
        ProbeCaptureAt=Now;FString Output;FParse::Value(FCommandLine::Get(),TEXT("B91Output="),Output);
        const FString Label=EnvironmentProbeRole==1?TEXT("A"):TEXT("B"),Name=FString::Printf(TEXT("%06u.png"),ProbeFrame++);
        const FString Directory=Output/TEXT("frames")/Label;IFileManager::Get().MakeDirectory(*Directory,true);
        FScreenshotRequest::RequestScreenshot(Directory/Name,true,false);
        ProbeFrames+=FString::Printf(TEXT("%s,%.6f,%d\n"),*Name,Now,ProbeTask);
        FFileHelper::SaveStringToFile(ProbeFrames,*(Output/(Label+TEXT("_Frames.csv"))),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }
    if(!ProbeTask){return;}const FVector P=GetPawn()->GetActorLocation();
    ProbeMotion+=FString::Printf(TEXT("%d,%.6f,%.3f,%.3f,%.3f\n"),ProbeTask,Now,P.X,P.Y,P.Z);
    const auto* D=View.FindByPredicate([](const auto& E){return E.PublicDoor==TEXT("building_riverport_00_door_north");});
    const bool Snapshot=ProbeTask==6||ProbeTask==7;
    bool Done=false;
    if(Snapshot){Done=Now-ProbeAt>1&&(ProbeTask==6?!D:D!=nullptr);}
    else if(!ProbeSent&&D&&Now-ProbeAt<15)
    {
        if(ProbeTask==2)
        {
            if(!(D->State&1)){return;}const FVector Goal=FVector(D->Position)+FVector(0,ProbeStep<2?400:65,-130);
            if(ProbeStep==0||ProbeStep==2){Navigation->Navigate(Goal);++ProbeStep;return;}
            if(Navigation->Status==TEXT("Stopped")&&Now-ProbeRetryAt>.5&&ProbeRetries<2)
            {ProbeRetryAt=Now;++ProbeRetries;Navigation->Navigate(Goal);return;}
            if(FVector::Dist2D(P,Goal)>60){return;}if(ProbeStep==1){ProbeStep=2;return;}
            if(Navigation->Status!=TEXT("Arrived")){return;}
            Navigation->Stop();
        }
        const bool Close=ProbeTask==2||ProbeTask==4||ProbeTask==5;
        if(ProbeTask!=8&&bool(D->State&1)!=Close){return;}
        ProbeRequest=FLDWorldRequest();ProbeRequest.Match=OwnerState.Match;ProbeRequest.Life=OwnerState.Life;ProbeRequest.Sequence=++WorldSequence;
        ProbeRequest.Observed=D->Handle;ProbeRequest.ObjectRevision=D->Revision;ProbeRequest.Verb=Close?ELDWorldVerb::Close:ELDWorldVerb::Open;
        const auto* E=ALDEnvironmentDirector::Get(GetWorld());if(!E){return;}ProbeRequest.Contract=E->Contract;
        ServerWorldInteraction(ProbeRequest);ProbeSent=true;ProbeHoldAt=Now;
    }
    if(ProbeSent)
    {
        if(Now-ProbeHoldAt>.08){ProbeHoldAt=Now;ServerWorldHold(ProbeRequest.Match,ProbeRequest.Life,ProbeRequest.Sequence,++Nonce,false);}
        if(LastWorldReceipt.Sequence==ProbeRequest.Sequence&&LastWorldReceipt.Result)
        {
            if(ProbeTask==1&&!ProbeReceipt.IsValid())
            {ProbeReceipt=LastWorldReceipt.Receipt;ProbeDuplicateSerial=WorldReceiptSerial;ServerWorldInteraction(ProbeRequest);ServerWorldInteraction(ProbeRequest);return;}
            const bool ExpectedOpen=ProbeTask==1||ProbeTask==3||ProbeTask==4||ProbeTask==8;
            Done=D&&bool(D->State&1)==ExpectedOpen&&(ProbeTask!=1||Now-ProbeHoldAt>.15);
            // Hold heartbeats stop after completion, so the duplicate receipt wait is real.
            if(ProbeTask==1&&ProbeReceipt.IsValid()){Done=D&&(D->State&1)&&LastWorldReceipt.Receipt==ProbeReceipt&&WorldReceiptSerial>=ProbeDuplicateSerial+2;}
        }
    }
    if(!Done&&Now-ProbeAt<15){return;}
    auto R=MakeShared<FJsonObject>();R->SetNumberField(TEXT("phase"),ProbeTask);R->SetBoolField(TEXT("completed"),Done);
    R->SetNumberField(TEXT("receipt_result"),Snapshot?-1:LastWorldReceipt.Result);R->SetStringField(TEXT("receipt"),Snapshot?TEXT(""):LastWorldReceipt.Receipt.ToString());
    R->SetNumberField(TEXT("local_nav_retries"),ProbeRetries);
    R->SetStringField(TEXT("receipt_reason"),Snapshot?TEXT(""):LastWorldReceipt.Reason);
    R->SetBoolField(TEXT("duplicate_receipt_same"),ProbeTask!=1||LastWorldReceipt.Receipt==ProbeReceipt);
    R->SetNumberField(TEXT("door_revision"),D?D->Revision:0);R->SetNumberField(TEXT("door_state"),D?D->State:0);
    R->SetBoolField(TEXT("door_observed"),D!=nullptr);R->SetNumberField(TEXT("view_revision"),LastViewRevision);
    TArray<TSharedPtr<FJsonValue>> Fields,Rows;
    for(TFieldIterator<FProperty> I(FLDEnvironmentViewEntry::StaticStruct());I;++I){Fields.Add(MakeShared<FJsonValueString>(I->GetName()));}
    for(const auto& Entry:View)
    {auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("handle"),Entry.Handle.ToString());J->SetStringField(TEXT("public_door"),Entry.PublicDoor.ToString());
     J->SetStringField(TEXT("position_cm"),FVector(Entry.Position).ToString());J->SetNumberField(TEXT("revision"),Entry.Revision);J->SetNumberField(TEXT("state"),Entry.State);J->SetNumberField(TEXT("light"),Entry.Light);Rows.Add(MakeShared<FJsonValueObject>(J));}
    R->SetArrayField(TEXT("wire_fields"),Fields);R->SetArrayField(TEXT("received_entries"),Rows);
    FString Json;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Json));ServerB91ProbeAck(ProbeTask,Json);
    FString Output;FParse::Value(FCommandLine::Get(),TEXT("B91Output="),Output);
    const FString ProbeLabel=EnvironmentProbeRole==1?TEXT("A"):TEXT("B");
    FFileHelper::SaveStringToFile(Json,*(Output/(ProbeLabel+FString::Printf(TEXT("_%d.json"),ProbeTask))),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    FFileHelper::SaveStringToFile(ProbeMotion,*(Output/(ProbeLabel+TEXT("_Motion.csv"))),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    ProbeTask=0;
}
