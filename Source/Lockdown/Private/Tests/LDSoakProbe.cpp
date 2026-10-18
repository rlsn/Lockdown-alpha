#include "Tests/LDSoakProbe.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Combat/LDCombatDirector.h"
#include "Objectives/LDObjectiveDirector.h"
#include "Vision/LDVisionDirector.h"
#include "Vision/LDWorldViewRelay.h"
#include "Engine/World.h"
#include "Engine/NetConnection.h"
#include "EngineUtils.h"
#include "UObject/CoreNet.h"
#include "HAL/PlatformMemory.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
    double Percentile(TArray<double> Values,double P){if(Values.IsEmpty()){return 0;}Values.Sort();return Values[FMath::Clamp(FMath::CeilToInt(Values.Num()*P)-1,0,Values.Num()-1)];}
}
ALDSoakProbe::ALDSoakProbe(){PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
void ALDSoakProbe::BeginPlay()
{
    Super::BeginPlay();Started=LastFrame=FPlatformTime::Seconds();FParse::Value(FCommandLine::Get(),TEXT("LDProbeRole="),Role);FParse::Value(FCommandLine::Get(),TEXT("LDSoakSeconds="),Seconds);Seconds=FMath::Clamp(Seconds,60.,600.);
    if(Role.IsEmpty()){Role=TEXT("Server");}if(GetNetMode()==NM_Client){CastChecked<ALDPlayerController>(GetOwner())->ServerProbeHello(Role==TEXT("A")?1:2);}
    UE_LOG(LogTemp,Display,TEXT("LD_SOAK begin role=%s mode=%d duration=%.0f"),*Role,int32(GetNetMode()),Seconds);
}
void ALDSoakProbe::Check(bool Pass,const FString& Text)
{Checks.Add((Pass?TEXT("PASS "):TEXT("FAIL "))+Text);if(!Pass){++Failures;}UE_LOG(LogTemp,Display,TEXT("LD_SOAK %s"),*Checks.Last());}
void ALDSoakProbe::Audit(double Now)
{
    int32 Actors=0;for(TActorIterator<AActor> It(GetWorld());It;++It){++Actors;}MinActors=FMath::Min(MinActors,Actors);MaxActors=FMath::Max(MaxActors,Actors);
    Memory.Add(FPlatformMemory::GetStats().UsedPhysical/1048576.);
    if(GetNetMode()==NM_Client)
    {
        auto* PC=CastChecked<ALDPlayerController>(GetOwner());int32 Bodies=0,States=0,Directors=0,Items=0;bool Private=true;
        for(TActorIterator<ALDCharacter> It(GetWorld());It;++It){++Bodies;Private&=*It==PC->GetPawn();}
        for(TActorIterator<ALDPlayerState> It(GetWorld());It;++It){++States;Private&=*It==PC->PlayerState;}
        for(TActorIterator<ALDVisionDirector> It(GetWorld());It;++It){++Directors;}
        for(TActorIterator<ALDObjectiveDirector> It(GetWorld());It;++It){++Directors;}
        for(TActorIterator<ALDSeal> It(GetWorld());It;++It){++Items;}
        const auto& V=PC->LocalView()->LastView();
        Check(Private&&Bodies==(PC->OwnerState.bActive?1:0)&&States==1&&!Directors&&!Items&&V.BodySources<=1&&V.WardSources<=2&&V.PublicSources<=2&&V.Entities.Num()<=64,TEXT("restricted registry and source bounds remain valid during soak"));
    }
    UE_LOG(LogTemp,Display,TEXT("LD_SOAK progress role=%s elapsed=%.1f actors=%d memory=%.1fMiB"),*Role,Now-MeasureAt,Actors,Memory.Last());
}
void ALDSoakProbe::ServerTick(double Now)
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    if(!bStarted)
    {
        for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It){auto* PC=Cast<ALDPlayerController>(It->Get());if(PC&&PC->GetPawn()){if(PC->ProbeRole==1){A=PC;}if(PC->ProbeRole==2){B=PC;}}}
        if(!A.IsValid()||!B.IsValid()||Now-Started<3){if(Now-LastAudit>5){LastAudit=Now;UE_LOG(LogTemp,Display,TEXT("LD_SOAK awaiting roles A=%d B=%d"),A.IsValid(),B.IsValid());}return;}
        bStarted=true;MeasureAt=Now+10;
        LastAudit=0;
        A->GetPawn()->SetActorLocation(FVector(-1700,-1000,92));B->GetPawn()->SetActorLocation(FVector(-1620,-1000,92));
        A->ClientProbePhase(1);B->ClientProbePhase(1);return;
    }
    if(bEnding){if(Now-LastAction>2){Finish(Now);}return;}
    if(!A.IsValid()||!B.IsValid()){Check(false,TEXT("client disconnected during measured soak"));Finish(Now);return;}
    if(Now<MeasureAt){return;}
    if(Mode->Combat->SampleSerial!=LastRule){LastRule=Mode->Combat->SampleSerial;Rules.Add(Mode->Combat->LastSampleMs);}
    if(Mode->Vision->SampleSerial!=LastView){LastView=Mode->Vision->SampleSerial;Views.Add(Mode->Vision->LastSampleMs);Combined.Add(Mode->Vision->LastSampleMs+Mode->Combat->LastSampleMs);}
    if(Now-LastAudit>=60){LastAudit=Now;Audit(Now);}
    if(Now-MeasureAt>=Seconds){bEnding=true;LastAction=Now;A->ClientProbePhase(16);B->ClientProbePhase(16);}
}
void ALDSoakProbe::ClientTick(double Now)
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwner());auto* Relay=PC->LocalView();if(!Relay||!PC->ProbePhase){return;}
    if(!bStarted){bStarted=true;MeasureAt=Now+10;LastFrame=Now;}
    if(PC->ProbePhase==16){Finish(Now);return;}
    const auto& O=PC->OwnerState;
    if(Now-LastPing>.5){LastPing=Now;PC->ServerProbePing(Now);}
    if(auto* Body=Cast<ALDCharacter>(PC->GetPawn()))
    {
        if(O.bActive&&Now>=MeasureAt)
        {
            const double Sign=FMath::Fmod(Now-MeasureAt,4.)<2?1.:-1.;Body->AddMovementInput(FVector(Sign,0,0));Body->SetFacing(Role==TEXT("A")?0:180);
            auto* Movement=CastChecked<ULDCharacterMovementComponent>(Body->GetCharacterMovement());
            if(bMeasured){Corrections+=Movement->CorrectionCount;MaxCorrection=FMath::Max(MaxCorrection,Movement->MaxCorrectionCm);}
            else{Movement->MaxCorrectionCm=0;}
            Movement->CorrectionCount=0;
            if(LastPawn.Get()==Body){BodyPathCm+=FVector::Dist2D(LastPosition,Body->GetActorLocation());}LastPawn=Body;LastPosition=Body->GetActorLocation();
        }
    }
    if(Now<MeasureAt){LastFrame=Now;return;}
    if(!bMeasured){bMeasured=true;PC->ProbeAckMs.Reset();PC->ProbeRtts.Reset();if(auto* C=PC->GetNetConnection()){NetInStart=C->InTotalBytes;NetOutStart=C->OutTotalBytes;}}
    if(Now-MeasureAt<60){Frames.Add((Now-LastFrame)*1000);}LastFrame=Now;
    if(Now-MeasureAt<Seconds-2&&Now-LastAction>=1.){LastAction=Now;PC->RecordProbeRequest(++Request);PC->ServerCombatAction(O.Match,O.Life,Request,0,Role==TEXT("A")?FVector::ForwardVector:-FVector::ForwardVector);}
    if(Now-MeasureAt<Seconds-2&&Now-LastContext>=2.){LastContext=Now;PC->RecordProbeRequest(++Request);PC->ServerContextRequest(O.Match,O.Life,Request,1,FGuid());}
    const auto& V=Relay->LastView();
    if(V.Revision!=LastReceived)
    {
        LastReceived=V.Revision;FNetBitWriter Writer(nullptr,65536);bool Valid=true;auto Copy=V;Copy.NetSerialize(Writer,nullptr,Valid);if(Valid){FullBytes+=Writer.GetNumBytes();}
        SnapshotAges.Add((GetWorld()->GetGameState()->GetServerWorldTimeSeconds()-V.AuthoritySeconds)*1000);
    }
    if(Now-LastAudit>=60){LastAudit=Now;Audit(Now);}
}
void ALDSoakProbe::Tick(float Delta)
{
    Super::Tick(Delta);
#if !UE_BUILD_SHIPPING
    const double Now=FPlatformTime::Seconds();
    if((!bStarted&&Now-Started>180)||(bStarted&&Now-MeasureAt>Seconds+30)){Check(false,TEXT("soak timeout"));Finish(Now);return;}
    if(GetNetMode()!=NM_Client){ServerTick(Now);}else{ClientTick(Now);}
#endif
}
void ALDSoakProbe::Finish(double Now)
{
    SetActorTickEnabled(false);TSharedRef<FJsonObject> Json=MakeShared<FJsonObject>();const double Elapsed=FMath::Max(1.,Now-MeasureAt);
    Json->SetStringField(TEXT("role"),Role);Json->SetNumberField(TEXT("durationSeconds"),Elapsed);Json->SetNumberField(TEXT("actorMin"),MinActors);Json->SetNumberField(TEXT("actorMax"),MaxActors);
    TArray<TSharedPtr<FJsonValue>> Memories;for(double Value:Memory){Memories.Add(MakeShared<FJsonValueNumber>(Value));}Json->SetArrayField(TEXT("memoryMiB"),Memories);
    if(Memory.Num()>=5){TArray<double> Late;for(int32 I=Memory.Num()/2;I<Memory.Num();++I){Late.Add(Memory[I]);}const double Spread=Percentile(Late,1)-Percentile(Late,0);Json->SetNumberField(TEXT("lateMemorySpreadMiB"),Spread);Check(Spread<128,TEXT("late process memory spread remains below128MiB"));}
    Check(MinActors!=MAX_int32&&MaxActors>=MinActors&&MaxActors-MinActors<=80,TEXT("actor population stays bounded through deaths, corpses, proxies and returns"));
    if(GetNetMode()!=NM_Client)
    {
        Check(!GetWorld()->GetAuthGameMode<ALDGameMode>()->IsFinished(),TEXT("measured soak completes before the match deadline freezes gameplay"));
        Json->SetNumberField(TEXT("ruleP95Ms"),Percentile(Rules,.95));Json->SetNumberField(TEXT("viewP95Ms"),Percentile(Views,.95));Json->SetNumberField(TEXT("combinedP95Ms"),Percentile(Combined,.95));Json->SetNumberField(TEXT("authoritySamples"),Combined.Num());
        Check(Combined.Num()>Seconds*12&&Percentile(Combined,.95)<10,TEXT("project combat/objective plus view p95 below10ms per authority interval"));
    }
    else
    {
        auto* PC=CastChecked<ALDPlayerController>(GetOwner());const double FrameMedian=Percentile(Frames,.5),FrameP95=Percentile(Frames,.95),AckP95=Percentile(PC->ProbeAckMs,.95),EchoMedian=Percentile(PC->ProbeRtts,.5);
        Json->SetNumberField(TEXT("frameSamples"),Frames.Num());Json->SetNumberField(TEXT("medianFps"),FrameMedian>0?1000/FrameMedian:0);Json->SetNumberField(TEXT("frameP95Ms"),FrameP95);Json->SetNumberField(TEXT("actionSamples"),PC->ProbeAckMs.Num());Json->SetNumberField(TEXT("actionP95Ms"),AckP95);Json->SetNumberField(TEXT("echoMedianMs"),EchoMedian);Json->SetNumberField(TEXT("echoP95Ms"),Percentile(PC->ProbeRtts,.95));Json->SetNumberField(TEXT("snapshotAgeP95Ms"),Percentile(SnapshotAges,.95));Json->SetNumberField(TEXT("snapshotSamples"),SnapshotAges.Num());Json->SetNumberField(TEXT("receivedFullViewKiBps"),FullBytes/Elapsed/1024);Json->SetNumberField(TEXT("movementCorrections"),Corrections);Json->SetNumberField(TEXT("maxMovementCorrectionCm"),MaxCorrection);
        Json->SetNumberField(TEXT("clientBodyPathCm"),BodyPathCm);Check(BodyPathCm>Seconds*30,TEXT("active client drives real Character Movement throughout the measured scene"));
        if(auto* C=PC->GetNetConnection()){const double Inbound=(C->InTotalBytes-NetInStart)/Elapsed/1024;Json->SetNumberField(TEXT("totalInboundKiBps"),Inbound);Json->SetNumberField(TEXT("totalOutboundKiBps"),(C->OutTotalBytes-NetOutStart)/Elapsed/1024);Check(Inbound<100,TEXT("all inbound native traffic including relay and framing below100KiBps"));}
        Check(PC->ProbeAckMs.Num()>Seconds&&PC->ProbeRequests.IsEmpty(),TEXT("all measured requests, including rejections, receive an acknowledgement"));
        Check(AckP95<=250,TEXT("normal combat and rejected-context acknowledgement p95 at most250ms"));
        Check(FullBytes/Elapsed/1024<100,TEXT("received serialized full-view payload below100KiBps"));
        int32 Expected=0;FParse::Value(FCommandLine::Get(),TEXT("LDExpectedRTT="),Expected);Check(EchoMedian>=Expected*.8&&EchoMedian<Expected+150,TEXT("echo confirms configured native transport delay"));
        if(!FParse::Param(FCommandLine::Get(),TEXT("nullrhi"))){Check(FrameMedian>0&&1000/FrameMedian>=55&&FrameP95<33,TEXT("60-second active1080p median55FPS and p95 frame below33ms"));}
    }
    Json->SetNumberField(TEXT("failures"),Failures);TArray<TSharedPtr<FJsonValue>> Lines;for(const auto& Text:Checks){Lines.Add(MakeShared<FJsonValueString>(Text));}Json->SetArrayField(TEXT("checks"),Lines);
    FString Directory,Output;FParse::Value(FCommandLine::Get(),TEXT("LDProbeReportDir="),Directory);FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Output));if(Directory.IsEmpty()||!FFileHelper::SaveStringToFile(Output,*(Directory/(Role+TEXT(".json"))))){++Failures;}
    FPlatformMisc::RequestExitWithStatus(false,Failures?1:0);
}
