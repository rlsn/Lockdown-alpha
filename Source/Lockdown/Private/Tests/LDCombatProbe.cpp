#include "Tests/LDCombatProbe.h"
#include "Combat/LDCombatDirector.h"
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Vision/LDVisionDirector.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "AIController.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"

ALDCombatProbe::ALDCombatProbe(){PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
void ALDCombatProbe::BeginPlay()
{
    Super::BeginPlay();At=GetWorld()->GetTimeSeconds();
    A=GetWorld()->SpawnActor<ALDCharacter>(FVector(-1800,-900,100),FRotator::ZeroRotator);
    B=GetWorld()->SpawnActor<ALDCharacter>(FVector(-1720,-900,100),FRotator::ZeroRotator);
    for(TActorIterator<ALDResident> It(GetWorld());It;++It){It->SetActorTickEnabled(false);It->GetCharacterMovement()->DisableMovement();if(auto* AI=Cast<AAIController>(It->GetController())){AI->StopMovement();}}
}
void ALDCombatProbe::Check(bool Pass,const FString& Text)
{Checks.Add((Pass?TEXT("PASS "):TEXT("FAIL "))+Text);if(!Pass){++Failures;}UE_LOG(LogTemp,Display,TEXT("LD_COMBAT_PROBE %s"),*Checks.Last());}
void ALDCombatProbe::Reset()
{
    auto* C=GetWorld()->GetAuthGameMode<ALDGameMode>()->Combat.Get();
    for(auto* Body:{A.Get(),B.Get()})
    {
        if(!IsValid(Body)){continue;}
        auto& S=*C->StateFor(Body);S=FLDBodyCombatState();S.QuietSince=GetWorld()->GetTimeSeconds()-10;
        Body->GetCharacterMovement()->StopMovementImmediately();Body->GetCharacterMovement()->DisableMovement();Body->SetSprint(false);Body->SetFacing(0);
    }
    A->SetActorLocation(FVector(-1800,-900,90));if(IsValid(B)){B->SetActorLocation(FVector(-1720,-900,90));}
}
void ALDCombatProbe::Rules()
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* C=M->Combat.Get();C->SetActorTickEnabled(false);
    FString Reason;const double Now=GetWorld()->GetTimeSeconds();const FVector F=FVector::ForwardVector;
    Reset();Check(C->RequestBody(A,0,F,Reason),TEXT("Basic accepted"));A->SetFacing(180);C->Sample(Now+.149);
    Check(C->StateFor(B)->Hp==100,TEXT("No damage before 150ms windup"));C->Sample(Now+.151);
    Check(C->StateFor(B)->Hp==80,TEXT("20 damage uses committed aim despite later turn"));
    Check(!C->RequestBody(A,0,F,Reason),TEXT("750ms start cadence enforced"));
    Reset();C->RequestBody(A,0,F,Reason);B->SetActorLocation(FVector(-1600,-900,90));C->Sample(Now+.2);
    Check(C->StateFor(B)->Hp==100&&C->StateFor(A)->BasicReady>Now,TEXT("Range escape misses and consumes cadence"));
    Reset();C->RequestBody(A,0,F,Reason);C->RequestBody(B,0,-F,Reason);C->Sample(Now+.2);
    Check(C->StateFor(A)->Hp==80&&C->StateFor(B)->Hp==80,TEXT("Simultaneous due strikes both land"));
    Reset();C->RequestBody(A,1,F,Reason);C->RequestBody(B,0,-F,Reason);C->StateFor(B)->ImpactAt=C->StateFor(A)->ImpactAt;C->Sample(Now+.501);
    Check(C->StateFor(A)->Hp==80&&C->StateFor(B)->Hp==40,TEXT("Same-batch counterhit cannot cancel already-due assassination"));
    Reset();C->RequestBody(A,1,F,Reason);C->RequestBody(B,0,-F,Reason);C->Sample(Now+.2);
    Check(C->StateFor(A)->Hp==80&&C->StateFor(A)->Action==0&&C->StateFor(A)->AssassinReady>Now+17,TEXT("Earlier damage interrupts assassination without refund"));
    Reset();Check(C->RequestBody(A,1,F,Reason)&&C->StateFor(B)->WarningUntil>Now+.49,TEXT("Legal rear assassination warns immediately"));C->Sample(Now+.501);
    Check(C->StateFor(B)->Hp==40&&C->StateFor(A)->Action==3,TEXT("Assassination deals 60 then enters recovery"));
    C->StateFor(A)->RecoveryUntil=Now;
    Check(C->RequestBody(A,0,F,Reason),TEXT("Elapsed recovery does not reject the next edge action"));
    Reset();C->RequestBody(A,1,F,Reason);B->SetFacing(180);C->Sample(Now+.501);
    Check(C->StateFor(B)->Hp==100,TEXT("Turning breaks rear assassination"));
    Reset();C->RequestBody(A,1,F,Reason);B->SetActorLocation(FVector(-1600,-900,90));C->Sample(Now+.501);
    Check(C->StateFor(B)->Hp==100,TEXT("Moving out of reach breaks assassination"));
    Reset();C->RequestBody(A,1,F,Reason);A->SetSprint(true);C->Sample(Now+.501);
    Check(C->StateFor(B)->Hp==100,TEXT("Sprint at impact breaks assassination"));
    Reset();C->StateFor(B)->LastDealt=Now;Check(!C->RequestBody(A,1,F,Reason),TEXT("Target damage history blocks assassination"));
    Reset();C->StateFor(A)->ProtectionUntil=Now+4;Check(!C->RequestBody(A,0,F,Reason),TEXT("Protected source cannot attack"));
    Reset();C->StateFor(B)->ProtectionUntil=Now+4;C->RequestBody(A,0,F,Reason);C->Sample(Now+.2);
    Check(C->StateFor(B)->Hp==100,TEXT("Protected target cannot take melee damage"));
    Reset();A->SetActorLocation(FVector(-1160,-450,90));B->SetActorLocation(FVector(-1065,-450,90));
    C->RequestBody(A,0,F,Reason);C->Sample(Now+.2);Check(C->StateFor(B)->Hp==100,TEXT("Wall blocks melee even within reach"));
    Reset();A->SetActorLocation(FVector(900,40,90));B->SetActorLocation(FVector(900,-40,290));
    C->RequestBody(A,0,FVector(0,-1,0),Reason);C->Sample(Now+.2);Check(C->StateFor(B)->Hp==100,TEXT("Vertical cliff blocks planar melee"));
    Reset();ALDWard* Ward=GetWorld()->SpawnActor<ALDWard>(FVector(-1750,-900,15),FRotator::ZeroRotator);Ward->Identity=FGuid::NewGuid();Ward->ExpiresAt=Now+60;
    C->RequestBody(A,0,F,Reason);C->Sample(Now+.2);Check(Ward->Hp==0&&C->StateFor(B)->Hp==100,TEXT("Nearest ward takes one 20-damage strike; no cleave"));Ward->Destroy();
    auto* LocalPC=CastChecked<ALDPlayerController>(GetWorld()->GetFirstPlayerController());auto* LocalBody=CastChecked<ALDCharacter>(LocalPC->GetPawn());
    LocalBody->SetActorLocation(FVector(-1880,-900,92));LocalBody->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    auto* LocalState=C->StateFor(LocalBody);*LocalState=FLDBodyCombatState();LocalState->QuietSince=Now-10;
    const auto Envelope=LocalPC->OwnerState;
    LocalPC->ServerCombatAction(Envelope.Match,Envelope.Life,800,0,F);
    LocalPC->ServerCombatAction(Envelope.Match,Envelope.Life,801,1,F);
    LocalPC->ServerCombatAction(Envelope.Match,Envelope.Life,802,2,F);C->Sample(Now);
    Check(LocalState->Action==4&&LocalState->BasicReady==0&&LocalState->AssassinReady==0,TEXT("Same-sample RPC priority chooses dash without consuming lower-action cooldowns"));
    const double DashDeadline=LocalState->DashReady;LocalPC->ServerCombatAction(Envelope.Match,Envelope.Life,802,2,F);C->Sample(Now+.01);
    Check(LocalState->DashReady==DashDeadline,TEXT("Duplicate action request replays receipt without another commitment"));
    LocalBody->GetCharacterMovement()->RemoveRootMotionSource(TEXT("LD_AuthorityDash"));LocalBody->GetCharacterMovement()->DisableMovement();*LocalState=FLDBodyCombatState();
    Reset();B->Destroy();B=GetWorld()->SpawnActor<ALDResident>(FVector(-1720,-900,100),FRotator::ZeroRotator);
    B->SetActorTickEnabled(false);B->GetCharacterMovement()->DisableMovement();if(auto* AI=Cast<AAIController>(B->GetController())){AI->StopMovement();}
    Check(C->StateFor(B)->Hp==40,TEXT("Residents start with 40 HP"));C->StateFor(B)->Hp=20;
    auto* PC=Cast<ALDPlayerController>(GetWorld()->GetFirstPlayerController());const auto Before=*M->StateFor(PC);
    C->RequestBody(A,0,F,Reason);C->Sample(Now+.2);
    Check(!IsValid(B)&&M->StateFor(PC)->Gold==Before.Gold&&M->StateFor(PC)->Xp==Before.Xp,TEXT("Resident death grants no gold or XP"));
    int32 Corpses=0;for(TActorIterator<ALDCorpse> It(GetWorld());It;++It){++Corpses;}
    Check(Corpses>0,TEXT("Death creates a separate temporary corpse identity"));
    // Real contender death uses the same damage batch and retained one-time receipt.
    auto* Player=Cast<ALDCharacter>(PC->GetPawn());Player->SetActorLocation(FVector(-1720,-900,90));Player->GetCharacterMovement()->DisableMovement();
    C->StateFor(Player)->Hp=20;M->StateFor(PC)->Gold=101;
    Reset();C->RequestBody(A,0,F,Reason);C->Sample(Now+.2);
    const auto Dead=*M->StateFor(PC);const uint32 OldBodyId=Player->GetUniqueID();
    Check(!Dead.bActive&&!PC->GetPawn()&&Dead.Hp==0&&Dead.Gold==80&&Dead.LastLostGold==21&&Dead.WardCharges==0,TEXT("Death unpossesses and snapshots ceiling gold loss once"));
    M->EndContenderLife(PC,Now+100);Check(M->StateFor(PC)->ReturnAt==Dead.ReturnAt&&M->StateFor(PC)->DeathSerial==Dead.DeathSerial,TEXT("Duplicate death cannot extend return or charge loss twice"));
    M->ReturnDueContenders(Dead.ReturnAt-.01);Check(!PC->GetPawn(),TEXT("No early return"));M->ReturnDueContenders(Dead.ReturnAt+.01);
    Check(PC->GetPawn()&&PC->GetPawn()->GetUniqueID()!=OldBodyId&&M->StateFor(PC)->Life==Dead.Life+1&&M->StateFor(PC)->Hp==100&&M->StateFor(PC)->WardCharges==0,TEXT("Due return creates fresh body/life with full HP and no free ward"));
    Check(C->StateFor(Cast<ALDCharacter>(PC->GetPawn()))->ProtectionUntil>=Dead.ReturnAt+4,TEXT("Returned body carries four seconds of authority protection"));
    C->SetActorTickEnabled(true);
}
void ALDCombatProbe::DashPhase(int32 Next,FVector Point)
{
    Phase=Next;At=GetWorld()->GetTimeSeconds();bDashStarted=false;Start=Point;
    A->SetActorLocation(Point,false,nullptr,ETeleportType::TeleportPhysics);A->GetCharacterMovement()->StopMovementImmediately();A->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
    auto* C=GetWorld()->GetAuthGameMode<ALDGameMode>()->Combat.Get();auto& S=*C->StateFor(A);S=FLDBodyCombatState();S.QuietSince=At-10;
}
void ALDCombatProbe::Tick(float Delta)
{
    Super::Tick(Delta);const double Now=GetWorld()->GetTimeSeconds();
    if(Phase==0){if(Now-At>.4){Rules();DashPhase(1,FVector(-1800,-900,100));}return;}
    const double Elapsed=Now-At;FString Reason;auto* C=GetWorld()->GetAuthGameMode<ALDGameMode>()->Combat.Get();
    if(Elapsed>.3&&!bDashStarted)
    {
        bDashStarted=true;Start=A->GetActorLocation();
        if(Phase==1)
        {
            B=GetWorld()->SpawnActor<ALDCharacter>(Start+FVector(80,0,0),FRotator::ZeroRotator);B->GetCharacterMovement()->DisableMovement();
            Check(C->RequestBody(A,1,FVector::ForwardVector,Reason),TEXT("Assassination windup before dash cancel"));
            A->SetSprint(true);Check(FMath::IsNearlyEqual(A->GetCharacterMovement()->GetMaxSpeed(),180.f),TEXT("Assassination caps sprint input to walk speed"));A->SetSprint(false);
        }
        Check(C->RequestBody(A,2,Phase==4?FVector(0,1,0):FVector::ForwardVector,Reason),FString::Printf(TEXT("Dash phase %d accepts authority root motion"),Phase));
        if(Phase==1){Check(C->StateFor(A)->Action==4&&C->StateFor(A)->AssassinReady>Now+17,TEXT("Dash cancels assassination without refunding cooldown"));}
        Check(!C->RequestBody(A,2,FVector::ForwardVector,Reason),TEXT("Dash cooldown rejects repeat"));
    }
    if(Elapsed<.7){return;}
    const FVector End=A->GetActorLocation();const double Distance=FVector::Dist2D(Start,End);
    if(Phase==1){Check(FMath::IsNearlyEqual(Distance,360.,15.)&&A->GetVelocity().Size2D()<1,FString::Printf(TEXT("Open dash: %.2f cm, stops after 200ms"),Distance));DashPhase(2,FVector(-1300,-450,100));}
    else if(Phase==2){Check(End.X<=-1125,TEXT("Dash sweeps capsule against wall"));DashPhase(3,FVector(300,-400,150));}
    else if(Phase==3){Check(End.X>600&&End.Z>275,TEXT("Dash follows ramp onto raised plateau: ")+End.ToString());DashPhase(4,FVector(900,-180,300));}
    else if(Phase==4){Check(End.Y<=28&&End.Z>275,TEXT("Dash cannot escape over vertical ledge: ")+End.ToString());DashPhase(5,FVector(-1450,500,100));}
    else if(Phase==5){Check(End.X<-1240,TEXT("Dash collides with opaque tree"));Finish();}
}
void ALDCombatProbe::Finish()
{
    TSharedRef<FJsonObject> Json=MakeShared<FJsonObject>();Json->SetNumberField(TEXT("failures"),Failures);TArray<TSharedPtr<FJsonValue>> Lines;
    for(const auto& Line:Checks){Lines.Add(MakeShared<FJsonValueString>(Line));}Json->SetArrayField(TEXT("checks"),Lines);
    FString Output,Path;FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Output));FParse::Value(FCommandLine::Get(),TEXT("LDProbeReport="),Path);
    if(Path.IsEmpty()||!FFileHelper::SaveStringToFile(Output,*Path)){++Failures;}
    SetActorTickEnabled(false);FPlatformMisc::RequestExitWithStatus(false,Failures?1:0);
}
