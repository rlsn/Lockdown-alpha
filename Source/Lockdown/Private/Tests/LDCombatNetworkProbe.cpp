#include "Tests/LDCombatNetworkProbe.h"
#include "Combat/LDCombatDirector.h"
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Vision/LDVisionDirector.h"
#include "Vision/LDWorldViewRelay.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "AIController.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
    void Place(ALDPlayerController* PC,FVector Position,bool bWalking=false)
    {
        auto* Body=Cast<ALDCharacter>(PC->GetPawn());if(!Body){return;}
        Body->SetActorLocation(Position,false,nullptr,ETeleportType::TeleportPhysics);Body->SetFacing(0);
        Body->GetCharacterMovement()->StopMovementImmediately();Body->GetCharacterMovement()->SetMovementMode(bWalking?MOVE_Walking:MOVE_None);Body->SetSprint(false);
    }
    FGuid BodyId(const FLDWorldView& View,FVector Point)
    {
        for(const auto& E:View.Entities){if((E.ActionCue&15)!=5&&E.Kind==ELDObservedKind::Body&&FVector::DistSquared2D(E.Position,Point)<25){return E.Id;}}
        return FGuid();
    }
}
ALDCombatNetworkProbe::ALDCombatNetworkProbe(){bReplicates=false;PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
void ALDCombatNetworkProbe::BeginPlay()
{
    Super::BeginPlay();Started=At=FPlatformTime::Seconds();FParse::Value(FCommandLine::Get(),TEXT("LDProbeRole="),Role);
    if(Role.IsEmpty()){Role=TEXT("Server");}
    if(GetNetMode()==NM_Client){if(auto* PC=Cast<ALDPlayerController>(GetOwner())){PC->ServerProbeHello(Role==TEXT("A")?1:2);}}
}
void ALDCombatNetworkProbe::Check(bool Pass,const FString& Text)
{Checks.Add((Pass?TEXT("PASS "):TEXT("FAIL "))+Text);if(!Pass){++Failures;}UE_LOG(LogTemp,Display,TEXT("LD_COMBAT_NET %s"),*Checks.Last());}
void ALDCombatNetworkProbe::Enter(int32 Next)
{
    Phase=Next;At=FPlatformTime::Seconds();bChecked=false;
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* C=Mode->Combat.Get();
    if(Next==1)
    {
        Mode->Vision->SetPublicSensor(false);
        for(TActorIterator<ALDResident> It(GetWorld());It;++It)
        {It->SetActorTickEnabled(false);It->GetCharacterMovement()->DisableMovement();if(auto* AI=Cast<AAIController>(It->GetController())){AI->StopMovement();}}
    }
    if(Next<=5)
    {
        Place(ClientA.Get(),FVector(-1800,-900,92),Next==4);Place(ClientB.Get(),Next==4?FVector(-1200,-900,92):FVector(-1720,-900,92));
        for(auto* PC:{ClientA.Get(),ClientB.Get()})
        {
            auto* Body=CastChecked<ALDCharacter>(PC->GetPawn());auto& S=*C->StateFor(Body);S=FLDBodyCombatState();S.QuietSince=GetWorld()->GetTimeSeconds()-10;
            auto* O=Mode->StateFor(PC);O->Hp=100;O->ActionCue=0;O->WarningUntil=0;O->ProtectedUntil=0;Mode->SyncOwner(PC);
        }
        if(Next==4){DashStart=ClientA->GetPawn()->GetActorLocation();}
        if(Next==5){C->StateFor(Cast<ALDCharacter>(ClientB->GetPawn()))->Hp=20;Mode->StateFor(ClientB.Get())->Hp=20;Mode->StateFor(ClientB.Get())->Gold=101;Mode->SyncOwner(ClientB.Get());OldLife=ClientB->OwnerState.Life;}
    }
    if(Next==6){Place(ClientA.Get(),FVector(1600,1000,92));}
    if(Next==7){Place(ClientA.Get(),FVector(-1800,-900,92));Place(ClientB.Get(),FVector(-1720,-900,92));}
    ClientA->ClientProbePhase(uint8(Next));ClientB->ClientProbePhase(uint8(Next));
}
void ALDCombatNetworkProbe::ServerTick()
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!Mode||!Mode->Combat||!Mode->Vision){return;}
    if(!Phase)
    {
        for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
        {auto* PC=Cast<ALDPlayerController>(It->Get());if(PC&&PC->GetPawn()){if(PC->ProbeRole==1){ClientA=PC;}if(PC->ProbeRole==2){ClientB=PC;}}}
        if(ClientA.IsValid()&&ClientB.IsValid()&&FPlatformTime::Seconds()-Started>2){Enter(1);}return;
    }
    const double Elapsed=FPlatformTime::Seconds()-At;
    if(Phase==9){if(Elapsed>1.8){Finish();}return;}
    if(!ClientA.IsValid()||!ClientB.IsValid()){Check(false,TEXT("Client disconnected during scenario"));Finish();return;}
    if(Phase==6)
    {
        if(ClientB->OwnerState.bActive)
        {
            Check(ClientB->OwnerState.Life==OldLife+1&&ClientB->OwnerState.Hp==100&&ClientB->OwnerState.WardCharges==0,TEXT("Scheduled real-time return renews life and HP without ward refill"));Enter(7);
        }
        return;
    }
    if(Elapsed<(Phase==7?4.6:2.2)){return;}
    const auto& AState=ClientA->OwnerState;const auto& BState=ClientB->OwnerState;
    if(Phase==1){Check(BState.Hp==80&&AState.Hp==100,TEXT("Remote owning basic RPC applies exactly 20 damage"));}
    if(Phase==2){Check(BState.Hp==40&&AState.Hp==100,TEXT("Remote assassination applies 60 damage after warning"));}
    if(Phase==3){Check(BState.Hp==100&&AState.Hp==80,TEXT("Remote earlier hit interrupts warned assassination"));}
    if(Phase==4){const double D=FVector::Dist2D(DashStart,ClientA->GetPawn()->GetActorLocation());Check(FMath::IsNearlyEqual(D,360.,20.),FString::Printf(TEXT("Remote authority root-motion dash %.2f cm"),D));}
    if(Phase==5)
    {
        Check(!BState.bActive&&!ClientB->GetPawn()&&BState.Hp==0&&BState.Gold==80&&BState.LastLostGold==21,TEXT("Remote death has private exact loss receipt and no body"));
        int32 Wards=0;for(TActorIterator<ALDWard> It(GetWorld());It;++It){if(It->Controller==ClientB.Get()){++Wards;}}
        Check(Wards==1,TEXT("Existing owned ward survives real owner death"));
    }
    if(Phase==7){Check(BState.Hp==100&&AState.Hp==100&&BState.Life==OldLife+1,TEXT("Protected and stale-life requests cannot damage either returned body"));}
    if(Phase==8){Check(AState.Hp==80&&BState.Hp==100,TEXT("Returned contender can fight after protection expires"));Enter(9);return;}
    Enter(Phase+1);
}
void ALDCombatNetworkProbe::ClientTick()
{
    auto* PC=Cast<ALDPlayerController>(GetOwner());if(!PC||!PC->LocalView()||!PC->ProbePhase){return;}
    const bool IsA=Role==TEXT("A");auto* Relay=PC->LocalView();const auto& O=PC->OwnerState;
    const double RealNow=FPlatformTime::Seconds();
    if(RealNow-LastPing>.5){LastPing=RealNow;PC->ServerProbePing(RealNow);}
    if(Phase!=PC->ProbePhase){Phase=PC->ProbePhase;At=FPlatformTime::Seconds();bSent=bChecked=bSawWarning=bExtra=false;if(Phase==1){OldLife=O.Life;}}
    if(Phase==9){Finish();return;}
    const double Elapsed=FPlatformTime::Seconds()-At;
    if(O.WarningUntil>GetWorld()->GetGameState()->GetServerWorldTimeSeconds()){bSawWarning=true;}
    if(!bSent&&Elapsed>.3)
    {
        bSent=true;const uint32 Request=uint32(Phase*100);PC->RecordProbeRequest(Request);
        if(IsA&&Phase<=5){PC->ServerCombatAction(O.Match,O.Life,Request,Phase==2||Phase==3?1:(Phase==4?2:0),FVector::ForwardVector);}
        if(!IsA&&Phase==1){PC->ServerPlaceWard(O.Match,O.Life,Request,FVector(-1680,-850,0));}
        if(!IsA&&Phase==6){PC->ServerCombatAction(O.Match,O.Life,Request,0,-FVector::ForwardVector);}
        if(Phase==7){PC->ServerCombatAction(O.Match,IsA?O.Life:OldLife,Request,0,IsA?FVector::ForwardVector:-FVector::ForwardVector);}
        if(!IsA&&Phase==8){PC->ServerCombatAction(O.Match,O.Life,Request,0,-FVector::ForwardVector);}
    }
    if(!IsA&&Phase==3&&!bExtra&&Elapsed>.48){bExtra=true;PC->RecordProbeRequest(300);PC->ServerCombatAction(O.Match,O.Life,300,0,-FVector::ForwardVector);}
    if(!IsA&&Phase==7&&!bExtra&&Elapsed>.7){bExtra=true;PC->RecordProbeRequest(701);PC->ServerCombatAction(O.Match,O.Life,701,0,-FVector::ForwardVector);}
    if(bChecked||Elapsed<1.2){return;}bChecked=true;
    int32 Bodies=0,States=0,Directors=0,Corpses=0,Wards=0;bool Restricted=true;
    for(TActorIterator<ALDCharacter> It(GetWorld());It;++It){++Bodies;Restricted&=*It==PC->GetPawn();}
    for(TActorIterator<ALDPlayerState> It(GetWorld());It;++It){++States;Restricted&=*It==PC->PlayerState;}
    for(TActorIterator<ALDCombatDirector> It(GetWorld());It;++It){++Directors;}
    for(TActorIterator<ALDCorpse> It(GetWorld());It;++It){++Corpses;}
    for(TActorIterator<ALDWard> It(GetWorld());It;++It){++Wards;}
    Check(Restricted&&Bodies==(O.bActive?1:0)&&States==1&&!Directors&&!Corpses&&!Wards,FString::Printf(TEXT("Phase %d registry exposes only own canonical body/state; no combat director/corpse/ward"),Phase));
    if(Phase==1)
    {
        Check(O.Hp==(IsA?100:80),TEXT("Only own HP arrives through private state"));
        if(IsA){OldObserved=BodyId(Relay->LastView(),FVector(-1720,-900,92));Check(OldObserved.IsValid(),TEXT("Visible opponent has opaque body observation"));}
    }
    if(Phase==2&&!IsA){Check(bSawWarning&&O.Hp==40,TEXT("Immediate private assassination warning arrives before 60 damage"));}
    if(Phase==3){Check(O.Hp==(IsA?80:100),TEXT("Damage interruption result reconciles on owning client"));}
    if(Phase==4&&IsA)
    {const double D=PC->GetPawn()?PC->GetPawn()->GetActorLocation().X+1800:0;Check(FMath::IsNearlyEqual(D,360.,25.),FString::Printf(TEXT("Autonomous owner reconciles root-motion dash %.2f cm"),D));}
    if(Phase==5&&!IsA){Check(!O.bActive&&!PC->GetPawn()&&O.LastLostGold==21,TEXT("Death receipt and possession loss reach owner"));}
    if(Phase==6&&!IsA)
    {
        const auto& View=Relay->LastView();
        Check(!O.bActive&&View.BodySources==0&&View.WardSources==1&&View.PublicSources==0,TEXT("Actual waiting view retains only owned ward sight"));
        const ACameraActor* Camera=Cast<ACameraActor>(PC->GetViewTarget());
        Check(Camera&&Camera->GetCameraComponent()->PostProcessSettings.WeightedBlendables.Array.Num()>0,TEXT("Actual waiting camera retains fog material"));
        Check(!BodyId(View,FVector(1600,1000,92)).IsValid(),TEXT("Distant hidden attacker has no waiting observation or effect"));
    }
    if(Phase==7)
    {
        if(IsA){const FGuid Fresh=BodyId(Relay->LastView(),FVector(-1720,-900,92));Check(Fresh.IsValid()&&Fresh!=OldObserved,TEXT("Return replaces opponent observation identity"));}
        else{Check(O.bActive&&O.Life==OldLife+1&&O.Hp==100&&O.WardCharges==0,TEXT("Fresh private life state ignores stale requests"));}
    }
    if(Phase==8){Check(O.Hp==(IsA?80:100),TEXT("Post-return fighting result reaches separate clients"));}
}
void ALDCombatNetworkProbe::Tick(float Delta)
{
    Super::Tick(Delta);
#if !UE_BUILD_SHIPPING
    if(FPlatformTime::Seconds()-Started>75){Check(false,TEXT("Combat network probe timed out"));Finish();return;}
    if(GetNetMode()!=NM_Client){ServerTick();}else{ClientTick();}
#endif
}
void ALDCombatNetworkProbe::Finish()
{
    TSharedRef<FJsonObject> Json=MakeShared<FJsonObject>();Json->SetNumberField(TEXT("failures"),Failures);TArray<TSharedPtr<FJsonValue>> Lines;
    if(GetNetMode()==NM_Client)
    {
        auto* PC=CastChecked<ALDPlayerController>(GetOwner());TArray<double> Sorted=PC->ProbeRtts;Sorted.Sort();int32 Expected=0;FParse::Value(FCommandLine::Get(),TEXT("LDExpectedRTT="),Expected);
        const double Median=Sorted.IsEmpty()?0:Sorted[Sorted.Num()/2];
        Check(Sorted.Num()>=15&&Median>=Expected*.8&&Median<Expected+150,FString::Printf(TEXT("Transport echo median %.1fms from %d samples, configured RTT %dms"),Median,Sorted.Num(),Expected));
        Json->SetNumberField(TEXT("medianEchoRttMs"),Median);Json->SetNumberField(TEXT("echoSamples"),Sorted.Num());
        Json->SetNumberField(TEXT("p95EchoRttMs"),Sorted.IsEmpty()?0:Sorted[FMath::Min(Sorted.Num()-1,FMath::CeilToInt(Sorted.Num()*.95)-1)]);
        TArray<double> Acks=PC->ProbeAckMs;Acks.Sort();const double P95=Acks.IsEmpty()?0:Acks[FMath::Min(Acks.Num()-1,FMath::CeilToInt(Acks.Num()*.95)-1)];
        Check(Acks.Num()>=5&&P95<250,FString::Printf(TEXT("Action acknowledgements p95 %.1fms from %d scripted actions"),P95,Acks.Num()));
        Json->SetNumberField(TEXT("p95ActionAckMs"),P95);Json->SetNumberField(TEXT("actionAckSamples"),Acks.Num());
    }
    Json->SetNumberField(TEXT("failures"),Failures);
    for(const auto& Text:Checks){Lines.Add(MakeShared<FJsonValueString>(Text));}Json->SetArrayField(TEXT("checks"),Lines);
    FString Directory,Output;FParse::Value(FCommandLine::Get(),TEXT("LDProbeReportDir="),Directory);FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Output));
    if(Directory.IsEmpty()||!FFileHelper::SaveStringToFile(Output,*(Directory/(Role+TEXT(".json"))))){++Failures;}
    SetActorTickEnabled(false);FPlatformMisc::RequestExitWithStatus(false,Failures?1:0);
}
