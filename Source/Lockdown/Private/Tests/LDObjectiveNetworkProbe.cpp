#include "Tests/LDObjectiveNetworkProbe.h"
#include "Objectives/LDObjectiveDirector.h"
#include "Objectives/LDObjectiveRules.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Vision/LDVisionDirector.h"
#include "Vision/LDWorldViewRelay.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AIController.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
    void PlaceObjective(ALDPlayerController* PC,FVector Point)
    {if(APawn* Body=PC?PC->GetPawn():nullptr){Body->SetActorLocation(Point,false,nullptr,ETeleportType::TeleportPhysics);auto* Character=CastChecked<ALDCharacter>(Body);Character->GetCharacterMovement()->StopMovementImmediately();Character->GetCharacterMovement()->DisableMovement();Character->SetFacing(0);}}
    FGuid ObservedSeal(ALDWorldViewRelay* Relay)
    {if(Relay){for(const auto& E:Relay->LastView().Entities){if(E.Kind==ELDObservedKind::Seal){return E.Id;}}}return FGuid();}
}
ALDObjectiveNetworkProbe::ALDObjectiveNetworkProbe(){bReplicates=false;PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
void ALDObjectiveNetworkProbe::BeginPlay()
{
    Super::BeginPlay();Started=At=FPlatformTime::Seconds();FParse::Value(FCommandLine::Get(),TEXT("LDProbeRole="),Role);FParse::Value(FCommandLine::Get(),TEXT("LDObjectiveScenario="),Scenario);
    if(Role.IsEmpty()){Role=TEXT("Server");}if(Scenario.IsEmpty()){Scenario=TEXT("Assault");}
    if(GetNetMode()==NM_Client){if(auto* PC=Cast<ALDPlayerController>(GetOwner())){PC->ServerProbeHello(Role==TEXT("A")?1:2);}}
}
void ALDObjectiveNetworkProbe::Check(bool Pass,const FString& Text)
{Checks.Add((Pass?TEXT("PASS "):TEXT("FAIL "))+Text);if(!Pass){++Failures;}UE_LOG(LogTemp,Display,TEXT("LD_OBJECTIVE_NET %s"),*Checks.Last());}
void ALDObjectiveNetworkProbe::Enter(int32 Next)
{
    Phase=Next;At=FPlatformTime::Seconds();WorldAt=GetWorld()->GetTimeSeconds();bChecked=bExtra=bSawChannel=false;
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    if(Next==1)
    {
        Mode->Vision->SetPublicSensor(false);int32 I=0;
        for(TActorIterator<ALDResident> It(GetWorld());It;++It){It->SetActorTickEnabled(false);It->GetCharacterMovement()->DisableMovement();It->SetActorLocation(FVector(-2100+I++*70,1300,92));if(auto* AI=Cast<AAIController>(It->GetController())){AI->StopMovement();}}
        PlaceObjective(A.Get(),LDObjective::Position(LDObjective::Guild)+FVector(0,60,92));PlaceObjective(B.Get(),FVector(1700,-900,92));
    }
    if(Next==2){PlaceObjective(A.Get(),LDObjective::Position(LDObjective::Station)+FVector(0,60,92));}
    if(Next==3){PlaceObjective(A.Get(),LDObjective::Position(LDObjective::Guild)+FVector(0,60,92));}
    if(Next==4)
    {
        Item=Mode->Objectives->SeedFixtureSeal();ItemId=Item->Identity;
        PlaceObjective(A.Get(),LDObjective::SeedPosition()+FVector(40,0,76));PlaceObjective(B.Get(),LDObjective::SeedPosition()+FVector(-40,0,76));
        Mode->Combat->SetActorTickEnabled(false);
    }
    if(Next==5)
    {
        Carrier=Item->Carrier;Thief=Carrier==A?B:A;
        PlaceObjective(Carrier.Get(),LDObjective::SeedPosition()+FVector(0,0,76));PlaceObjective(Thief.Get(),LDObjective::SeedPosition()+FVector(-80,0,76));
        auto* Body=CastChecked<ALDCharacter>(Carrier->GetPawn());Mode->Combat->StateFor(Body)->Hp=20;Mode->StateFor(Carrier.Get())->Hp=20;Mode->SyncOwner(Carrier.Get());
    }
    if(Next==6){PlaceObjective(Thief.Get(),Item->GetActorLocation()+FVector(30,0,76));}
    if(Next==7)
    {
        Mode->Objectives->SetDefencesFixture(Scenario==TEXT("Seal")?15:0);
        PlaceObjective(Thief.Get(),FVector(60,0,92));PlaceObjective(Carrier.Get(),FVector(-60,0,92));
    }
    if(Next==8){PlaceObjective(Carrier.Get(),FVector(1600,1000,92));}
    A->ClientProbePhase(uint8(Next));B->ClientProbePhase(uint8(Next));
}
void ALDObjectiveNetworkProbe::ServerTick()
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!Mode||!Mode->Objectives){return;}auto* GS=GetWorld()->GetGameState<ALDGameState>();
    const double Now=GetWorld()->GetTimeSeconds();
    if(!Phase)
    {
        for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It){auto* PC=Cast<ALDPlayerController>(It->Get());if(PC&&PC->GetPawn()){if(PC->ProbeRole==1){A=PC;}if(PC->ProbeRole==2){B=PC;}}}
        if(A.IsValid()&&B.IsValid()&&FPlatformTime::Seconds()-Started>2){Enter(1);}return;
    }
    const double Elapsed=FPlatformTime::Seconds()-At;
    if(Phase==11){if(Elapsed>1.5){Finish();}return;}
    if(!A.IsValid()||!B.IsValid()){Check(false,TEXT("A client disconnected before final objective evidence"));Finish();return;}
    if(Phase==4&&!bExtra&&Elapsed>.8)
    {
        if(Mode->Objectives->QueuedRequestCount()<2&&Elapsed<1.8){return;}
        Check(Mode->Objectives->QueuedRequestCount()==2,TEXT("Both actual pickup RPCs enter one authority batch"));Mode->Combat->Sample(Now);Mode->Combat->SetActorTickEnabled(true);bExtra=true;
    }
    if(Phase==6)
    {
        if(!bChecked&&Elapsed>1.4)
        {bChecked=true;Check(Item->State==1&&Item->Carrier==Thief&&Thief->OwnerState.SealId==ItemId,TEXT("Survivor steals exactly the dropped immutable Seal through owning RPC"));}
        if(Carrier->OwnerState.bActive&&Now>=Carrier->OwnerState.ProtectedUntil)
        {
            int32 Corpses=0;for(TActorIterator<ALDCorpse> It(GetWorld());It;++It){++Corpses;}
            Check(Corpses==0&&Item.IsValid()&&Item->State==1&&Item->Carrier==Thief,TEXT("Natural corpse expiry and former-carrier return preserve stolen item ownership"));Enter(7);
        }
        return;
    }
    if(Phase==8)
    {
        // A delayed release for phase 7 can arrive after phase 8 starts. Only measure
        // expiry of the new held token, never that previous attempt's explicit release.
        const auto* S=Mode->Objectives->StateFor(Thief.Get());if(S&&S->Token==800&&S->Channel==3){bSawChannel=true;}
        if(bSawChannel&&!bChecked&&S&&S->Token==800&&!S->Channel)
        {bChecked=true;const double Gap=Now-S->LastHold;Check(Gap>=.25&&Gap<.35,FString::Printf(TEXT("Lost hold expires within250ms plus sample allowance: %.1fms"),Gap*1000));}
    }
    if(Phase==9)
    {
        if(GS->EndReason)
        {
            Check(GS->EndReason==1&&GS->ResultSerial==1&&GS->WinningRoute==(Scenario==TEXT("Seal")?2:1)&&Thief->OwnerState.bWinner,TEXT("Actual ten-second held claim commits expected route and one winner"));
            Check(Scenario==TEXT("Seal")?Item->State==2&&!Thief->OwnerState.SealId.IsValid():Item->State==1&&Thief->OwnerState.SealId==ItemId,TEXT("Only a Seal-route win consumes the carried item"));Result=GS->ResultReceipt;Enter(10);
        }
        return;
    }
    const double Duration=Phase==2||Phase==3?4.5:(Phase==8?4.8:2.2);if(Elapsed<Duration){return;}
    if(Phase==1){Check(A->OwnerState.ContractStage==1&&B->OwnerState.ContractStage==0,TEXT("Guild acceptance creates only the requester's private contract"));}
    if(Phase==2){Check(A->OwnerState.ContractStage==2&&A->OwnerState.Gold==0&&A->OwnerState.Xp==0,TEXT("Real three-second collection creates package without payout"));}
    if(Phase==3)
    {Check(A->OwnerState.Gold==25&&A->OwnerState.Xp==15&&A->OwnerState.Commissions==1&&A->OwnerState.DeliverySerial==1&&!A->OwnerState.ContractStage,TEXT("Real delivery and duplicate retry yield one25gold15XP receipt"));Check(B->OwnerState.Gold==0&&B->OwnerState.Xp==0,TEXT("Another connection receives no courier reward"));}
    if(Phase==4)
    {const int32 Owners=int32(A->OwnerState.SealId.IsValid())+int32(B->OwnerState.SealId.IsValid());Check(Owners==1&&Item->State==1&&Item->Carrier.IsValid(),TEXT("Simultaneous shuffled pickup preserves exactly one owner"));}
    if(Phase==5)
    {Check(!Carrier->OwnerState.bActive&&!Carrier->GetPawn()&&Item->State==0&&!Item->Carrier.IsValid()&&Item->Identity==ItemId,TEXT("Real combat death drops one item independently of its corpse"));}
    if(Phase==7){Check(GS->bClaimActive&&GS->bContested&&GS->ClaimProgress==0&&!GS->EndReason,TEXT("Other unprotected contender contests regardless of access or request order"));}
    if(Phase==8){Check(bChecked&&!GS->bClaimActive&&!Mode->Objectives->HeartSensorActive(Now)&&!GS->EndReason,TEXT("Expired hold resets progress and loses the shared Heart source after linger"));}
    if(Phase==10){Check(GS->ResultReceipt==Result&&GS->ResultSerial==1&&Item->Identity==ItemId,TEXT("Post-result commands cannot replace receipt or ownership"));Enter(11);return;}
    Enter(Phase+1);
}
void ALDObjectiveNetworkProbe::Send(uint8 Target,FGuid Observed)
{
    auto* PC=CastChecked<ALDPlayerController>(GetOwner());Token=uint32(Phase*100);Sequence=0;bHold=Target!=LDObjective::Seal;PC->RecordProbeRequest(Token);
    PC->ServerContextRequest(PC->OwnerState.Match,PC->OwnerState.Life,Token,Target,Observed);
}
void ALDObjectiveNetworkProbe::ClientTick()
{
    auto* PC=Cast<ALDPlayerController>(GetOwner());if(!PC||!PC->LocalView()||!PC->ProbePhase){return;}auto* Relay=PC->LocalView();const auto& O=PC->OwnerState;
    auto* GS=GetWorld()->GetGameState<ALDGameState>();if(!GS){return;}const double RealNow=FPlatformTime::Seconds();
    if(RealNow-LastPing>.5){LastPing=RealNow;PC->ServerProbePing(RealNow);}
    if(PC->ProbePhase!=Phase)
    {
        if(bHold){PC->ServerContextRelease(O.Match,O.Life,Token);}bHold=false;Phase=PC->ProbePhase;At=RealNow;bSent=bChecked=bExtra=false;
        if(Phase==1){OriginalLife=O.Life;}
    }
    if(Phase==11){Finish();return;}
    const double Elapsed=RealNow-At;const bool IsA=Role==TEXT("A");
    if(!bSent&&Elapsed>.3)
    {
        if(Phase==4||Phase==6)
        {
            const FGuid Id=ObservedSeal(Relay);
            if(O.bActive&&!O.SealId.IsValid()&&Id.IsValid()){Send(LDObjective::Seal,Id);bSent=true;}
            else if(Phase==6&&!O.bActive){bSent=true;PC->ServerContextRequest(O.Match,O.Life,uint32(Phase*100),LDObjective::Seal,FGuid::NewGuid());}
        }
        else
        {
            bSent=true;
            if(IsA&&(Phase==1||Phase==3)){Send(LDObjective::Guild);}
            if(IsA&&Phase==2){Send(LDObjective::Station);}
            if(Phase==5&&O.bActive&&!O.SealId.IsValid()){PC->RecordProbeRequest(500);PC->ServerCombatAction(O.Match,O.Life,500,0,FVector::ForwardVector);}
            if(Phase==7){Send(LDObjective::Heart);}
            if((Phase==8||Phase==9)&&O.SealId.IsValid()){Send(LDObjective::Heart);}
            if(Phase==10){PC->RecordProbeRequest(1000);PC->ServerContextRequest(O.Match,O.Life,1000,LDObjective::Heart,FGuid());PC->ServerCombatAction(O.Match,O.Life,1000,0,FVector::ForwardVector);}
        }
    }
    if(bHold&&RealNow-LastHold>.1&&!(Phase==8&&Elapsed>.9))
    {LastHold=RealNow;PC->ServerContextHold(O.Match,O.Life,Token,++Sequence,false);}
    if(Phase==3&&IsA&&!bExtra&&Elapsed>3.9){bExtra=true;PC->ServerContextRequest(O.Match,O.Life,300,LDObjective::Guild,FGuid());}
    if(Phase==7&&O.Life!=OriginalLife&&!bExtra&&Elapsed>.8){bExtra=true;PC->ServerContextRequest(O.Match,OriginalLife,701,LDObjective::Heart,FGuid());}
    // Include final heartbeat delivery, 250ms expiry, two-second linger, publication
    // cadence and return transport before checking the restricted client's source.
    if(Phase==8&&!bExtra&&Elapsed>4.2)
    {bExtra=true;Check(Relay->LastView().PublicSources==0,TEXT("Expired Heart linger removes the shared source on restricted client"));}
    if(bChecked||Elapsed<(Phase==2||Phase==3?4.:1.5)){return;}bChecked=true;
    int32 Bodies=0,States=0,Seals=0,Directors=0;bool Restricted=true;
    for(TActorIterator<ALDCharacter> It(GetWorld());It;++It){++Bodies;Restricted&=*It==PC->GetPawn();}
    for(TActorIterator<ALDPlayerState> It(GetWorld());It;++It){++States;Restricted&=*It==PC->PlayerState;}
    for(TActorIterator<ALDSeal> It(GetWorld());It;++It){++Seals;}
    for(TActorIterator<ALDObjectiveDirector> It(GetWorld());It;++It){++Directors;}
    Check(Restricted&&Bodies==(O.bActive?1:0)&&States==1&&!Seals&&!Directors,FString::Printf(TEXT("Phase%d restricted registry: own body/state, no canonical Seal or objective director"),Phase));
    if(Phase==1){Check(O.ContractStage==(IsA?1:0),TEXT("Only owner receives exact contract stage"));}
    if(Phase==2&&IsA){Check(O.ContractStage==2&&O.Gold==0,TEXT("Confirmed collection stage arrives without reward"));}
    if(Phase==3){Check(O.Gold==(IsA?25:0)&&O.Xp==(IsA?15:0),TEXT("Exact private reward balances arrive once"));}
    if(Phase==4){Check(!ObservedSeal(Relay).IsValid(),TEXT("Carried Seal has no unauthorized world pose or carrier record"));}
    if(Phase==5&&!O.bActive){Check(!O.SealId.IsValid()&&!O.ContractStage,TEXT("Dead owner loses carried inventory and active work"));}
    if(Phase==6){Check(O.bActive?O.SealId.IsValid():!O.SealId.IsValid(),TEXT("Theft updates only new owner's private inventory"));}
    if(Phase==7){Check(GS->bClaimActive&&GS->bContested&&GS->ClaimProgress==0&&Relay->LastView().PublicSources==1,TEXT("Public contested announcement and shared Heart source arrive without claimant identity"));}
    if(Phase==8&&!O.SealId.IsValid()){Check(Relay->LastView().PublicSources<=1,TEXT("Claim source remains revision-filtered during hold expiry"));}
    if(Phase==10){Check(GS->EndReason==1&&GS->ResultSerial==1&&GS->ResultReceipt.IsValid(),TEXT("One public final receipt reaches separate clients"));}
}
void ALDObjectiveNetworkProbe::Tick(float Delta)
{
    Super::Tick(Delta);
#if !UE_BUILD_SHIPPING
    if(FPlatformTime::Seconds()-Started>140){Check(false,TEXT("Objective network scenario timed out"));Finish();return;}
    if(GetNetMode()!=NM_Client){ServerTick();}else{ClientTick();}
#endif
}
void ALDObjectiveNetworkProbe::Finish()
{
    TSharedRef<FJsonObject> Json=MakeShared<FJsonObject>();Json->SetStringField(TEXT("scenario"),Scenario);
    if(GetNetMode()==NM_Client)
    {
        auto* PC=CastChecked<ALDPlayerController>(GetOwner());auto Echo=PC->ProbeRtts;Echo.Sort();auto Acks=PC->ProbeAckMs;Acks.Sort();
        int32 Expected=0;FParse::Value(FCommandLine::Get(),TEXT("LDExpectedRTT="),Expected);const double Median=Echo.IsEmpty()?0:Echo[Echo.Num()/2];
        Check(Echo.Num()>=30&&Median>=Expected*.8&&Median<Expected+150,FString::Printf(TEXT("Measured transport echo %.1fms, %d samples"),Median,Echo.Num()));
        Json->SetNumberField(TEXT("medianEchoRttMs"),Median);Json->SetNumberField(TEXT("echoSamples"),Echo.Num());
        Json->SetNumberField(TEXT("p95ActionAckMs"),Acks.IsEmpty()?0:Acks[FMath::Min(Acks.Num()-1,FMath::CeilToInt(Acks.Num()*.95)-1)]);Json->SetNumberField(TEXT("actionAckSamples"),Acks.Num());
    }
    Json->SetNumberField(TEXT("failures"),Failures);TArray<TSharedPtr<FJsonValue>> Lines;for(const auto& Text:Checks){Lines.Add(MakeShared<FJsonValueString>(Text));}Json->SetArrayField(TEXT("checks"),Lines);
    FString Directory,Output;FParse::Value(FCommandLine::Get(),TEXT("LDProbeReportDir="),Directory);FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Output));
    if(Directory.IsEmpty()||!FFileHelper::SaveStringToFile(Output,*(Directory/(Role+TEXT(".json"))))){++Failures;}
    SetActorTickEnabled(false);FPlatformMisc::RequestExitWithStatus(false,Failures?1:0);
}
