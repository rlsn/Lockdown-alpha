#include "Tests/LDSoakProbe.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Combat/LDCombatDirector.h"
#include "Objectives/LDObjectiveDirector.h"
#include "Development/LDDevelopmentDirector.h"
#include "Development/LDWorldChecks.h"
#include "Region/LDRegionDirector.h"
#include "Region/LDRegionDefinition.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "DynamicRHI.h"
#include "RHIStats.h"
#include "UnrealClient.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
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
    Super::BeginPlay();Started=LastFrame=FPlatformTime::Seconds();FParse::Value(FCommandLine::Get(),TEXT("LDProbeRole="),Role);FParse::Value(FCommandLine::Get(),TEXT("LDSoakSeconds="),Seconds);Seconds=FMath::Clamp(Seconds,60.,FParse::Param(FCommandLine::Get(),TEXT("LDReleaseSoak"))?5400.:600.);
    if(Role.IsEmpty()){Role=TEXT("Server");}if(GetNetMode()==NM_Client){CastChecked<ALDPlayerController>(GetOwner())->ServerProbeHello(Role==TEXT("A")?1:2);}
    UE_LOG(LogTemp,Display,TEXT("LD_SOAK begin role=%s mode=%d duration=%.0f"),*Role,int32(GetNetMode()),Seconds);
}
void ALDSoakProbe::Check(bool Pass,const FString& Text)
{Checks.Add((Pass?TEXT("PASS "):TEXT("FAIL "))+Text);if(!Pass){++Failures;}UE_LOG(LogTemp,Display,TEXT("LD_SOAK %s"),*Checks.Last());}
void ALDSoakProbe::Audit(double Now)
{
    int32 Actors=0;for(TActorIterator<AActor> It(GetWorld());It;++It){++Actors;}MinActors=FMath::Min(MinActors,Actors);MaxActors=FMath::Max(MaxActors,Actors);
    Memory.Add(FPlatformMemory::GetStats().UsedPhysical/1048576.);
    auto Record=MakeShared<FJsonObject>();Record->SetNumberField(TEXT("elapsedSeconds"),Now-MeasureAt);Record->SetNumberField(TEXT("actors"),Actors);Record->SetNumberField(TEXT("memoryMiB"),Memory.Last());Record->SetNumberField(TEXT("worldSeconds"),GetWorld()->GetTimeSeconds());
    const auto* GS=GetWorld()->GetGameState<ALDGameState>();Record->SetNumberField(TEXT("deadline"),GS->MatchDeadline);Record->SetNumberField(TEXT("resultSerial"),GS->ResultSerial);
    if(GetNetMode()==NM_Client)
    {
        auto* PC=CastChecked<ALDPlayerController>(GetOwner());int32 Bodies=0,States=0,Directors=0,Items=0;bool Private=true;
        for(TActorIterator<ALDCharacter> It(GetWorld());It;++It){++Bodies;Private&=*It==PC->GetPawn()||*It==LastPawn.Get();}
        for(TActorIterator<ALDPlayerState> It(GetWorld());It;++It){++States;Private&=*It==PC->PlayerState;}
        for(TActorIterator<ALDVisionDirector> It(GetWorld());It;++It){++Directors;}
        for(TActorIterator<ALDObjectiveDirector> It(GetWorld());It;++It){++Directors;}
        for(TActorIterator<ALDSeal> It(GetWorld());It;++It){++Items;}
        for(TActorIterator<ALDBook> It(GetWorld());It;++It){++Items;}
        for(TActorIterator<ALDRegionDirector> It(GetWorld());It;++It){++Directors;}
        for(TActorIterator<ALDDevelopmentDirector> It(GetWorld());It;++It){++Directors;}
        for(TActorIterator<ALDWorldChecks> It(GetWorld());It;++It){++Directors;}
        const auto& V=PC->LocalView()->LastView();
        Check(Private&&Bodies<=1&&States==1&&!Directors&&!Items&&V.BodySources<=1&&V.WardSources<=2&&V.PublicSources<=2&&V.Entities.Num()<=64,TEXT("restricted registry permits only the current or retiring owned body across asynchronous life teardown"));
        Record->SetNumberField(TEXT("viewEntities"),V.Entities.Num());Record->SetNumberField(TEXT("pendingAcknowledgements"),PC->ProbeRequests.Num());Record->SetNumberField(TEXT("ownLife"),PC->OwnerState.Life);Record->SetNumberField(TEXT("ownDeaths"),PC->OwnerState.DeathSerial);Record->SetNumberField(TEXT("ownPaidJobs"),PC->OwnerState.PaidJobs);Record->SetBoolField(TEXT("ownFabricationReceipt"),PC->OwnerState.FabricationReceipt.IsValid());
        if(!FParse::Param(FCommandLine::Get(),TEXT("nullrhi")))
        {FTextureMemoryStats Stats;RHIGetTextureMemoryStats(Stats);Record->SetNumberField(TEXT("dedicatedVideoMiB"),Stats.DedicatedVideoMemory/1048576.);Record->SetNumberField(TEXT("graphicsBudgetMiB"),Stats.TotalGraphicsMemory/1048576.);Record->SetNumberField(TEXT("streamingTextureMiB"),Stats.StreamingMemorySize/1048576.);Record->SetNumberField(TEXT("nonStreamingTextureMiB"),Stats.NonStreamingMemorySize/1048576.);FRHIMemoryStats MemoryStats;RHIGetMemoryStats(MemoryStats);Record->SetBoolField(TEXT("driverMemoryStatsValid"),MemoryStats.IsValid());if(MemoryStats.IsValid()){Record->SetNumberField(TEXT("driverBudgetMiB"),MemoryStats.BudgetLocal/1048576.);Record->SetNumberField(TEXT("driverUsedMiB"),MemoryStats.UsedLocal/1048576.);Record->SetNumberField(TEXT("driverOverBudgetMiB"),MemoryStats.DemotedLocal/1048576.);}if(Audits.IsEmpty()){FString Directory;FParse::Value(FCommandLine::Get(),TEXT("LDProbeReportDir="),Directory);FScreenshotRequest::RequestScreenshot(Directory/TEXT("Profile.png"),true,false);}}
    }
    else if(auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();M->WorldChecks){Record->SetNumberField(TEXT("theftDraws"),M->WorldChecks->DrawCount());Record->SetNumberField(TEXT("walletCount"),M->WorldChecks->WalletCount());Record->SetNumberField(TEXT("lifeA"),M->StateFor(A.Get())->Life);Record->SetNumberField(TEXT("lifeB"),M->StateFor(B.Get())->Life);Record->SetNumberField(TEXT("deathA"),M->StateFor(A.Get())->DeathSerial);Record->SetNumberField(TEXT("deathB"),M->StateFor(B.Get())->DeathSerial);}
    Audits.Add(MakeShared<FJsonValueObject>(Record));
    UE_LOG(LogTemp,Display,TEXT("LD_SOAK progress role=%s elapsed=%.1f actors=%d memory=%.1fMiB"),*Role,Now-MeasureAt,Actors,Memory.Last());
}
void ALDSoakProbe::ServerTick(double Now)
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    if(!bStarted)
    {
        for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It){auto* PC=Cast<ALDPlayerController>(It->Get());if(PC&&PC->GetPawn()){if(PC->ProbeRole==1){A=PC;}if(PC->ProbeRole==2){B=PC;}}}
        if(!A.IsValid()||!B.IsValid()||Now-Started<3){if(Now-LastAudit>5){LastAudit=Now;UE_LOG(LogTemp,Display,TEXT("LD_SOAK awaiting roles A=%d B=%d"),A.IsValid(),B.IsValid());}return;}
        if(Mode->IsDevelopmentProfile()&&!GetWorld()->GetGameState<ALDGameState>()->bMatchStarted){A->ClientProbePhase(1);B->ClientProbePhase(1);return;}
        bStarted=true;MeasureAt=Now+10;
        LastLifeFixture=MeasureAt;
        if(Mode->IsDevelopmentProfile()){auto* G=GetWorld()->GetGameState<ALDGameState>();OriginalDeadline=G->MatchDeadline;
            if(Seconds>1400&&FParse::Param(FCommandLine::Get(),TEXT("LDReleaseSoak"))){G->MatchDeadline=float(GetWorld()->GetTimeSeconds()+Seconds+120);G->ForceNetUpdate();UE_LOG(LogTemp,Display,TEXT("LD_SOAK_FIXTURE deadline extended from %.3f to %.3f; original world clock, rewards and life rules retained"),OriginalDeadline,G->MatchDeadline);}}
        LastAudit=0;
        const FVector Centre=LDRegion::Definition(GetWorld())?LDRegion::Site(GetWorld(),1)+FVector(0,-450,0):FVector(-1660,-1000,0);
        A->GetPawn()->SetActorLocation(Centre+FVector(-40,0,92));B->GetPawn()->SetActorLocation(Centre+FVector(40,0,92));LifeA=A->OwnerState.Life;LifeB=B->OwnerState.Life;
        A->ClientProbePhase(1);B->ClientProbePhase(1);return;
    }
    if(bEnding){if(Now-LastAction>2){Finish(Now);}return;}
    if(!A.IsValid()||!B.IsValid()){Check(false,TEXT("client disconnected during measured soak"));Finish(Now);return;}
    if(Mode->IsDevelopmentProfile())
    {
        for(auto* PC:{A.Get(),B.Get()}){auto& Life=PC==A.Get()?LifeA:LifeB;const auto* O=Mode->StateFor(PC);if(O&&O->bActive&&PC->GetPawn()&&O->Life!=Life){Life=O->Life;PC->GetPawn()->SetActorLocation(LDRegion::Site(GetWorld(),1)+FVector(PC==A.Get()?-40:40,-450,92));UE_LOG(LogTemp,Display,TEXT("LD_SOAK_FIXTURE returned body placed in sustained-load scene; life=%u"),Life);}}
    }
    if(Now<MeasureAt){return;}
    if(FParse::Param(FCommandLine::Get(),TEXT("LDLoadLifeCycles"))&&Now-LastLifeFixture>=120)
    {
        auto* Target=LifeFixtures%2?B.Get():A.Get();auto* Source=LifeFixtures%2?A.Get():B.Get();auto* TO=Mode->StateFor(Target);auto* SO=Mode->StateFor(Source);auto* TB=Cast<ALDCharacter>(Target->GetPawn());auto* SB=Cast<ALDCharacter>(Source->GetPawn());const double RuleNow=GetWorld()->GetTimeSeconds();
        if(TO&&SO&&TO->bActive&&SO->bActive&&TB&&SB&&RuleNow>=TO->ProtectedUntil&&RuleNow>=SO->ProtectedUntil)
        {
            const FVector Point=LDRegion::Site(GetWorld(),1)+FVector(0,-900,92);TB->SetActorLocation(Point-FVector(40,0,0));SB->SetActorLocation(Point+FVector(40,0,0));TB->GetCharacterMovement()->StopMovementImmediately();SB->GetCharacterMovement()->StopMovementImmediately();
            // Explicit technical load fixture: low HP, cleared attacker action/cadence and an immediate lethal batch. Never normal input or pacing evidence.
            auto* TS=Mode->Combat->StateFor(TB);auto* SS=Mode->Combat->StateFor(SB);TS->Hp=1;SS->Action=0;SS->BasicReady=0;FString Why;const double Begin=FPlatformTime::Seconds();const bool bAccepted=Mode->Combat->RequestBody(SB,0,-FVector::ForwardVector,Why);if(bAccepted){SS->ImpactAt=RuleNow;Mode->Combat->Sample(RuleNow);}FixtureCpu.Add((FPlatformTime::Seconds()-Begin)*1000);
            Check(bAccepted&&!TO->bActive&&!Target->GetPawn(),TEXT("Labelled periodic lethal batch executes actual corpse/death/return path"));++LifeFixtures;LastLifeFixture=Now;if(Source->GetPawn()){Source->GetPawn()->SetActorLocation(LDRegion::Site(GetWorld(),1)+FVector(Source==A.Get()?-40:40,-450,92));}
        }
    }
    if(Mode->Combat->SampleSerial!=LastRule){LastRule=Mode->Combat->SampleSerial;Rules.Add(Mode->Combat->LastSampleMs);}
    if(Mode->Vision->SampleSerial!=LastView){LastView=Mode->Vision->SampleSerial;Views.Add(Mode->Vision->LastSampleMs);Combined.Add(Mode->Vision->LastSampleMs+Mode->Combat->LastSampleMs);}
    if(Now-LastAudit>=60){LastAudit=Now;Audit(Now);}
    if(Now-MeasureAt>=Seconds){bEnding=true;LastAction=Now;A->ClientProbePhase(16);B->ClientProbePhase(16);}
}
void ALDSoakProbe::ClientTick(double Now)
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwner());auto* Relay=PC->LocalView();const auto* G=GetWorld()->GetGameState<ALDGameState>();
    if(G&&G->bDevelopmentProfile&&!G->bMatchStarted&&PC->OwnerState.Match)
    {const uint8 Class=Role==TEXT("A")?1:2;if(!bChoiceSent){PC->ChooseLobbyClass(Class);bChoiceSent=true;}if(!bReadySent&&PC->OwnerState.Build.ClassId==Class){PC->ToggleLobbyReady();bReadySent=true;}if(!bStartSent&&PC->OwnerState.bLobbyHost&&G->ReadyCount==2){PC->StartLobby();bStartSent=true;}return;}
    if(!Relay||!PC->ProbePhase){return;}
    if(!bStarted){bStarted=true;MeasureAt=Now+10;LastFrame=Now;PC->SetMenuOpen(false);}
    if(PC->ProbePhase==16){Finish(Now);return;}
    const auto& O=PC->OwnerState;
    if(Now-LastPing>.5){LastPing=Now;PC->ServerProbePing(Now);}
    if(auto* Body=Cast<ALDCharacter>(PC->GetPawn()))
    {
        if(O.bActive&&Now>=MeasureAt)
        {
            ActiveSeconds+=FMath::Clamp(Now-LastFrame,0.,.25);
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
    Frames.Add((Now-LastFrame)*1000);LastFrame=Now;
    if(O.bActive){ActiveFrames.Add(Frames.Last());}
    if(!FParse::Param(FCommandLine::Get(),TEXT("nullrhi"))){const double GpuMs=FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());if(GpuMs>0){GpuFrames.Add(GpuMs);}}
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
    Json->SetStringField(TEXT("profile"),FParse::Param(FCommandLine::Get(),TEXT("LDReleaseSoak"))?TEXT("B47 technical V04 sustained-load fixture: initial/returned-body placements; deadline extended only for duration above 1400 seconds; no clock acceleration or resource/build grants; not normal pacing or human evidence"):TEXT("Historical legacy technical soak"));Json->SetArrayField(TEXT("audits"),Audits);Json->SetNumberField(TEXT("originalDeadline"),OriginalDeadline);
    Json->SetBoolField(TEXT("periodicLethalFixture"),FParse::Param(FCommandLine::Get(),TEXT("LDLoadLifeCycles")));Json->SetNumberField(TEXT("lethalFixtureCount"),LifeFixtures);Json->SetNumberField(TEXT("fixtureCpuP95Ms"),Percentile(FixtureCpu,.95));
    if(GetWorld()->GetGameState<ALDGameState>()){Json->SetNumberField(TEXT("effectiveDeadline"),GetWorld()->GetGameState<ALDGameState>()->MatchDeadline);}
    TArray<TSharedPtr<FJsonValue>> Memories;for(double Value:Memory){Memories.Add(MakeShared<FJsonValueNumber>(Value));}Json->SetArrayField(TEXT("memoryMiB"),Memories);
    if(Memory.Num()>=5){TArray<double> Late;for(int32 I=Memory.Num()/2;I<Memory.Num();++I){Late.Add(Memory[I]);}const double Spread=Percentile(Late,1)-Percentile(Late,0);Json->SetNumberField(TEXT("lateMemorySpreadMiB"),Spread);Check(Spread<128,TEXT("late process memory spread remains below128MiB"));}
    Check(MinActors!=MAX_int32&&MaxActors>=MinActors&&MaxActors-MinActors<=80,TEXT("actor population stays bounded through deaths, corpses, proxies and returns"));
    if(GetNetMode()!=NM_Client)
    {
        Check(!GetWorld()->GetAuthGameMode<ALDGameMode>()->IsFinished(),TEXT("measured soak completes before the match deadline freezes gameplay"));
        Json->SetNumberField(TEXT("ruleP95Ms"),Percentile(Rules,.95));Json->SetNumberField(TEXT("viewP95Ms"),Percentile(Views,.95));Json->SetNumberField(TEXT("combinedP95Ms"),Percentile(Combined,.95));Json->SetNumberField(TEXT("authoritySamples"),Combined.Num());
        Check(Combined.Num()>Seconds*12&&Percentile(Combined,.95)<10,TEXT("project combat/objective plus view p95 below10ms per authority interval"));
        if(FParse::Param(FCommandLine::Get(),TEXT("LDLoadLifeCycles"))){Check(LifeFixtures>=FMath::FloorToInt((Seconds-1)/120),TEXT("Technical lethal batches and ordinary return timers exercised throughout the measured duration"));}
    }
    else
    {
        auto* PC=CastChecked<ALDPlayerController>(GetOwner());const double FrameMedian=Percentile(Frames,.5),FrameP95=Percentile(Frames,.95),AckP95=Percentile(PC->ProbeAckMs,.95),EchoMedian=Percentile(PC->ProbeRtts,.5);
        Json->SetNumberField(TEXT("frameSamples"),Frames.Num());Json->SetNumberField(TEXT("medianFps"),FrameMedian>0?1000/FrameMedian:0);Json->SetNumberField(TEXT("frameP95Ms"),FrameP95);Json->SetNumberField(TEXT("actionSamples"),PC->ProbeAckMs.Num());Json->SetNumberField(TEXT("actionP95Ms"),AckP95);Json->SetNumberField(TEXT("echoMedianMs"),EchoMedian);Json->SetNumberField(TEXT("echoP95Ms"),Percentile(PC->ProbeRtts,.95));Json->SetNumberField(TEXT("snapshotAgeP95Ms"),Percentile(SnapshotAges,.95));Json->SetNumberField(TEXT("snapshotSamples"),SnapshotAges.Num());Json->SetNumberField(TEXT("receivedFullViewKiBps"),FullBytes/Elapsed/1024);Json->SetNumberField(TEXT("movementCorrections"),Corrections);Json->SetNumberField(TEXT("maxMovementCorrectionCm"),MaxCorrection);
        Json->SetNumberField(TEXT("gpuSamples"),GpuFrames.Num());Json->SetNumberField(TEXT("gpuP95Ms"),Percentile(GpuFrames,.95));
        Json->SetNumberField(TEXT("activeFrameSamples"),ActiveFrames.Num());Json->SetNumberField(TEXT("activeFrameP95Ms"),Percentile(ActiveFrames,.95));Json->SetNumberField(TEXT("activeMedianFps"),ActiveFrames.IsEmpty()?0:1000/Percentile(ActiveFrames,.5));
        auto Cvar=[&](const TCHAR* Name){if(const auto* V=IConsoleManager::Get().FindConsoleVariable(Name)){Json->SetStringField(Name,V->GetString());}};
        for(const TCHAR* Name:{TEXT("sg.ResolutionQuality"),TEXT("sg.ViewDistanceQuality"),TEXT("sg.ShadowQuality"),TEXT("sg.GlobalIlluminationQuality"),TEXT("sg.ReflectionQuality"),TEXT("sg.TextureQuality"),TEXT("sg.EffectsQuality"),TEXT("sg.PostProcessQuality"),TEXT("r.RayTracing"),TEXT("r.ScreenPercentage"),TEXT("r.SecondaryScreenPercentage.GameViewport"),TEXT("r.Streaming.PoolSize"),TEXT("r.VSync")}){Cvar(Name);}
        Json->SetNumberField(TEXT("clientBodyPathCm"),BodyPathCm);Json->SetNumberField(TEXT("activeSeconds"),ActiveSeconds);Check(ActiveSeconds>Seconds*.05&&BodyPathCm>ActiveSeconds*30,TEXT("active bodies drive real Character Movement; ordinary waiting downtime recorded separately"));
        if(auto* C=PC->GetNetConnection()){const double Inbound=(C->InTotalBytes-NetInStart)/Elapsed/1024;Json->SetNumberField(TEXT("totalInboundKiBps"),Inbound);Json->SetNumberField(TEXT("totalOutboundKiBps"),(C->OutTotalBytes-NetOutStart)/Elapsed/1024);Check(Inbound<100,TEXT("all inbound native traffic including relay and framing below100KiBps"));}
        Check(PC->ProbeAckMs.Num()>Seconds&&PC->ProbeRequests.IsEmpty(),TEXT("all measured requests, including rejections, receive an acknowledgement"));
        Check(AckP95<=250,TEXT("normal combat and rejected-context acknowledgement p95 at most250ms"));
        Check(FullBytes/Elapsed/1024<100,TEXT("received serialized full-view payload below100KiBps"));
        int32 Expected=0;FParse::Value(FCommandLine::Get(),TEXT("LDExpectedRTT="),Expected);Check(FMath::Abs(EchoMedian-Expected)<=25,TEXT("measured echo median within25ms of declared RTT target"));
        if(!FParse::Param(FCommandLine::Get(),TEXT("nullrhi")))
        {const FIntPoint Size=GEngine&&GEngine->GameViewport&&GEngine->GameViewport->Viewport?GEngine->GameViewport->Viewport->GetSizeXY():FIntPoint::ZeroValue;Json->SetNumberField(TEXT("viewportWidth"),Size.X);Json->SetNumberField(TEXT("viewportHeight"),Size.Y);Check(Size==FIntPoint(1920,1080),TEXT("Actual rendered viewport is1920x1080"));Check(GpuFrames.Num()>60,TEXT("Rendered workload supplies actual GPU timing samples"));Check(FrameMedian>0&&1000/FrameMedian>=55&&FrameP95<33&&!ActiveFrames.IsEmpty()&&1000/Percentile(ActiveFrames,.5)>=55&&Percentile(ActiveFrames,.95)<33,TEXT("measured overall and active-body1080p median55FPS and p95 frame below33ms"));}
    }
    Json->SetNumberField(TEXT("failures"),Failures);TArray<TSharedPtr<FJsonValue>> Lines;for(const auto& Text:Checks){Lines.Add(MakeShared<FJsonValueString>(Text));}Json->SetArrayField(TEXT("checks"),Lines);
    FString Directory,Output;FParse::Value(FCommandLine::Get(),TEXT("LDProbeReportDir="),Directory);FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Output));if(Directory.IsEmpty()||!FFileHelper::SaveStringToFile(Output,*(Directory/(Role+TEXT(".json"))))){++Failures;}
    FPlatformMisc::RequestExitWithStatus(false,Failures?1:0);
}
