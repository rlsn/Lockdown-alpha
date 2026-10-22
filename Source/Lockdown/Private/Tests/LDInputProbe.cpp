#include "Tests/LDInputProbe.h"
#include "Framework/LDPlayerController.h"
#include "Framework/LDGameMode.h"
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Combat/LDCombatDirector.h"
#include "Navigation/LDNavigationComponent.h"
#include "UI/LDControlSettings.h"
#include "UI/LDControlsPanel.h"
#include "UI/LDDevelopmentPanel.h"
#include "Vision/LDWorldViewRelay.h"
#include "Region/LDRegionDefinition.h"
#include "Objectives/LDObjectiveDirector.h"
#include "EnhancedInputComponent.h"
#include "InputKeyEventArgs.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

ALDInputProbe::ALDInputProbe(){bReplicates=false;PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
void ALDInputProbe::BeginPlay()
{Super::BeginPlay();Started=PhaseAt=FPlatformTime::Seconds();bHUD=FParse::Param(FCommandLine::Get(),TEXT("LDHUDProbe"));FParse::Value(FCommandLine::Get(),TEXT("LDProbeRole="),Role);if(GetNetMode()==NM_Client){CastChecked<ALDPlayerController>(GetOwner())->ServerProbeHello(Role==TEXT("A")?1:2);}}
void ALDInputProbe::Key(FKey Input,bool Down)
{CastChecked<ALDPlayerController>(GetOwner())->InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),Input,Down?IE_Pressed:IE_Released,Down?1.f:0.f,false,FPlatformTime::Cycles64()));}
void ALDInputProbe::Check(bool Pass,const FString& Text)
{Checks.Add((Pass?TEXT("PASS "):TEXT("FAIL "))+Text);if(!Pass){++Failures;}UE_LOG(LogTemp,Display,TEXT("LD_INPUT_PROBE %s"),*Checks.Last());}
void ALDInputProbe::Enter(int32 Next){Phase=Next;Step=0;PhaseAt=FPlatformTime::Seconds();A->ClientProbePhase(uint8(Next));B->ClientProbePhase(uint8(Next));}
void ALDInputProbe::Tick(float Delta)
{
    Super::Tick(Delta);
#if !UE_BUILD_SHIPPING
    if(FPlatformTime::Seconds()-Started>150){Check(false,TEXT("Input probe timed out"));Finish();return;}
    if(GetNetMode()==NM_Client){ClientTick();}else{ServerTick();}
#endif
}
void ALDInputProbe::ServerTick()
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* G=GetWorld()->GetGameState<ALDGameState>();
    if(!Phase)
    {
        for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
        {if(auto* PC=Cast<ALDPlayerController>(It->Get());PC&&PC->GetPawn()){if(PC->ProbeRole==1){A=PC;}if(PC->ProbeRole==2){B=PC;}}}
        if(A.IsValid()&&B.IsValid()){Check(G->bUIControlProfile&&G->LobbyCount==2,TEXT("Independent V05 input clients retain real private lobby"));Enter(1);}return;
    }
    const double E=FPlatformTime::Seconds()-PhaseAt;
    if(bHUD&&Phase>=10){HUDServerTick();return;}
    if(Phase==10){if(E>4){Finish();}return;}
    if(!A.IsValid()||!B.IsValid()){Check(false,TEXT("Client disconnected before input checks completed"));Finish();return;}
    if(Phase==1&&G->bMatchStarted&&E>1.5)
    {
        int32 I=0;for(auto PC:{A,B})
        {
            const double Y=I++?-500:-1000;auto* Body=CastChecked<ALDCharacter>(PC->GetPawn());Body->SetActorLocation(FVector(-1700,Y,92),false,nullptr,ETeleportType::TeleportPhysics);Body->SetFacing(0);Body->GetCharacterMovement()->StopMovementImmediately();
            auto* O=M->StateFor(PC.Get());O->ProtectedUntil=0;O->Build.Ranks[1]=O->Build.Ranks[3]=O->Build.Ranks[4]=O->Build.Ranks[5]=1;O->Build.Slots={ELDSkill::Step,ELDSkill::Spark,ELDSkill::Patch,ELDSkill::Pickpocket};++O->Build.Revision;M->RefreshBuild(PC.Get());M->Combat->StateFor(Body)->ProtectionUntil=0;
            for(bool IsNear:{false,true}){auto* Resident=GetWorld()->SpawnActor<ALDResident>(FVector(IsNear?-1580:-1400,Y,92),FRotator::ZeroRotator);Resident->SetActorTickEnabled(false);if(Resident->GetController()){Resident->GetController()->Destroy();}Resident->GetCharacterMovement()->DisableMovement();if(IsNear){Near.Add(Resident);NearHp.Add(M->Combat->StateFor(Resident)->Hp);}else{Far.Add(Resident);FarHp.Add(M->Combat->StateFor(Resident)->Hp);}}
        }
        Enter(2);
    }
    else if(Phase==2&&E>4.5)
    {for(auto PC:{A,B}){const auto* State=M->Combat->StateFor(Cast<ALDCharacter>(PC->GetPawn()));Check(M->StateFor(PC.Get())->Build.Cooldowns[1]==0&&M->StateFor(PC.Get())->Build.Cooldowns[3]==0&&State->DashReady==0&&State->AssassinReady==0&&State->Action==0&&FMath::Abs(PC->GetPawn()->GetActorRotation().Yaw)<.01,TEXT("Arming/cancelling and old RPC aliases cannot spend cooldown, turn idle facing or bypass equipped targeting"));}Enter(3);}
    else if(Phase==3&&E>3)
    {
        int32 I=0;for(auto PC:{A,B}){const double Damage=LDDevelopment::SkillValue(M->StateFor(PC.Get())->Build,ELDSkill::Spark);Check(FMath::IsNearlyEqual(M->Combat->StateFor(Far[I].Get())->Hp,FarHp[I]-Damage,.001)&&M->Combat->StateFor(Near[I].Get())->Hp==NearHp[I],TEXT("Explicit Spark strikes selected observed body instead of a nearer legal body"));auto* O=M->StateFor(PC.Get());O->Hp=50;M->RefreshBuild(PC.Get());++I;}
        Enter(4);
    }
    else if(Phase==4&&E>2.2)
    {for(auto PC:{A,B}){auto* O=M->StateFor(PC.Get());Check(O->Hp==50&&O->Build.Cooldowns[4]>GetWorld()->GetTimeSeconds(),TEXT("Early mapped Patch release cancels healing and retains spent cooldown"));O->Build.Cooldowns[4]=0;M->SyncOwner(PC.Get());}Enter(5);}
    else if(Phase==5&&E>2.7)
    {for(auto PC:{A,B}){auto* O=M->StateFor(PC.Get());Check(O->Hp>50&&O->Hp<=O->MaxHp&&O->ContractStage==0,TEXT("Mapped E completes its equipped Patch hold without firing old context action"));}Enter(6);}
    else if(Phase==6)
    {
        if(!Step&&E>1.3){for(auto T:Far){T->SetActorLocation(FVector(2000,1000,92));}Step=1;}
        if(E>4.5){for(auto PC:{A,B}){Check(!PC->Navigation->IsAuthorityActive()&&M->StateFor(PC.Get())->ContractStage==0,TEXT("Panel input cannot issue navigation or contextual work"));M->AwardExperience(PC.Get(),60);auto* Body=CastChecked<ALDCharacter>(PC->GetPawn());Body->SetActorLocation(LDRegion::Site(GetWorld(),1)+FVector(PC==A?-40:40,50,92));Body->GetCharacterMovement()->StopMovementImmediately();}Enter(7);}
    }
    else if(Phase==7&&E>5.5)
    {for(auto PC:{A,B}){auto* O=M->StateFor(PC.Get());Check(O->Build.Attributes[0]==10&&O->Build.AttributePoints==1,TEXT("Fresh mapped F commits one real two-second station preparation"));}Enter(8);}
    else if(Phase==8&&E>5)
    {
        int32 I=0;for(auto PC:{A,B}){auto* Body=CastChecked<ALDCharacter>(PC->GetPawn());Near[I]->SetActorLocation(Body->GetActorLocation()+FVector(0,80,0));NearHp[I]=M->Combat->StateFor(Near[I].Get())->Hp;++I;}Enter(9);
    }
    else if(Phase==9&&E>3)
    {
        int32 I=0;for(auto PC:{A,B}){const auto* State=M->Combat->StateFor(Cast<ALDCharacter>(PC->GetPawn()));Check(FMath::IsNearlyEqual(M->Combat->StateFor(Near[I].Get())->Hp,NearHp[I]-LDDevelopment::BasicDamage(M->StateFor(PC.Get())->Build),.001)&&State->BasicReady>0,TEXT("Native A then explicit world-direction confirmation performs exactly one retained melee strike"));++I;}Enter(10);
    }
}
void ALDInputProbe::ClientTick()
{
    auto* PC=Cast<ALDPlayerController>(GetOwner());if(!PC||!PC->OwnerState.Match){return;}
    if(FPlatformTime::Seconds()-LastPing>.25){LastPing=FPlatformTime::Seconds();PC->ServerProbePing(LastPing);}
    if(PC->ProbePhase!=Phase){Phase=PC->ProbePhase;PhaseAt=FPlatformTime::Seconds();Step=0;}
    if(bHUD&&Phase>=10){HUDClientTick();return;}
    const double E=FPlatformTime::Seconds()-PhaseAt;auto* Body=Cast<ALDCharacter>(PC->GetPawn());if(!Body){return;}
    const double Y=Role==TEXT("A")?-1000:-500;
    auto FindTarget=[&](){if(const auto* R=PC->LocalView();R&&!R->IsStale()){for(const auto& V:R->LastView().Entities){if(V.Kind==ELDObservedKind::Body&&FVector::Dist2D(V.Position,FVector(-1400,Y,92))<20){return V.Id;}}}return FGuid();};
    if(Phase==1){if(!bChoice){PC->ChooseLobbyClass(1);bChoice=true;}if(!bReady&&PC->OwnerState.Build.ClassId==1&&E>.4){PC->ToggleLobbyReady();bReady=true;}if(PC->OwnerState.bLobbyHost&&PC->OwnerState.bReady&&E>1){PC->StartLobby();}return;}
    if(Phase==2)
    {
        if(!Step&&E>.5)
        {
            PC->Controls->ResetBindings();PC->RebuildV05Mappings();auto* Input=CastChecked<UEnhancedInputComponent>(PC->InputComponent);const int32 Before=Input->GetActionEventBindings().Num();FString Error;
            for(int32 I=0;I<10;++I){PC->Controls->Rebind(TEXT("Skill1"),I%2?EKeys::Q:EKeys::Z,Error);PC->RebuildV05Mappings();}
            Check(Input->GetActionEventBindings().Num()==Before,TEXT("Repeated remapping does not accumulate action delegates"));
            Check(!PC->Controls->Rebind(TEXT("Skill1"),EKeys::W,Error)&&!PC->Controls->Rebind(TEXT("Skill1"),EKeys::Escape,Error),TEXT("Live mapping rejects conflicts and reserved Escape"));
            PC->Controls->Rebind(TEXT("Skill1"),EKeys::Z,Error);PC->RebuildV05Mappings();PC->IntentFeedback=TEXT("Input probe marker");Step=1;
        }
        if(Step==1&&E>1){Key(EKeys::Q,true);Step=2;}
        if(Step==2&&E>1.2){Key(EKeys::Q,false);Check(!PC->IsTargeting()&&PC->IntentFeedback==TEXT("Input probe marker"),TEXT("Retired Q mapping and old alias cannot fire after rebind"));Key(EKeys::Z,true);Step=3;}
        if(Step==3&&E>1.5){Key(EKeys::Z,false);Check(PC->IsTargeting()&&PC->OwnerState.Build.Cooldowns[1]==0,TEXT("Native remapped key arms Step without casting"));Key(EKeys::RightMouseButton,true);Step=4;}
        if(Step==4&&E>1.8){Key(EKeys::RightMouseButton,false);Check(!PC->IsTargeting()&&!PC->Navigation->IsFollowing(),TEXT("First right-click cancels targeting and consumes its gesture"));Step=5;}
        if(Step==5&&E>2.2){PC->ServerSetAim(PC->OwnerState.Match,PC->OwnerState.Life,90);PC->ServerCombatAction(PC->OwnerState.Match,PC->OwnerState.Life,8000001,1,FVector::ForwardVector);PC->ServerCombatAction(PC->OwnerState.Match,PC->OwnerState.Life,8000002,2,FVector::ForwardVector);PC->ServerSkillAction(PC->OwnerState.Match,PC->OwnerState.Life,8000003,PC->OwnerState.Build.Revision,1,FVector::ForwardVector);Key(EKeys::One,true);Step=6;}
        if(Step==6&&E>2.5){Key(EKeys::One,false);Check(!PC->IsTargeting()&&PC->OwnerState.Build.Cooldowns[1]==0,TEXT("Numeric item entry does not invoke old skill-slot alias"));Step=7;}
    }
    if(Phase==3)
    {
        if(!Step&&E>.2){PC->ServerSkillTargetAction(PC->OwnerState.Match,PC->OwnerState.Life,++PC->NextRequest,PC->OwnerState.Build.Revision,1,FVector::ForwardVector,FGuid::NewGuid());Step=1;}
        if(Step==1&&E>.7){Check(PC->OwnerState.Build.Cooldowns[3]==0&&PC->IntentFeedback.Contains(TEXT("Target no longer visible")),TEXT("Forged observation cannot fall back to a nearby target or spend cooldown"));Target=FindTarget();Key(EKeys::W,true);Step=2;}
        if(Step==2&&E>.95){Key(EKeys::W,false);PC->ConfirmArmed(FVector(-1400,Y,0),Target);Step=3;}
    }
    if(Phase==4){if(!Step&&E>.5){Key(EKeys::E,true);Step=1;}if(Step==1&&E>.8){Key(EKeys::E,false);Step=2;}}
    if(Phase==5){if(!Step&&E>.5){Key(EKeys::E,true);Step=1;}if(Step==1&&E>2){Key(EKeys::E,false);Step=2;}}
    if(Phase==6)
    {
        if(!Step&&E>.5){Target=FindTarget();const auto* Own=PC->GetPawn().Get();const float Facing=Body->GetActorRotation().Yaw;PC->Inspect(Target);Check(Target.IsValid()&&PC->GetPawn()==Own&&FMath::IsNearlyEqual(Body->GetActorRotation().Yaw,Facing),TEXT("Selection neither changes ownership nor rotates own body"));PC->ArmSkill(1);Step=1;}
        if(Step==1&&E>2){Check(!PC->Inspected.IsValid()&&!PC->IsTargeting(),TEXT("Lost observation clears inspection and armed targeting"));PC->ToggleControlsPanel();Key(EKeys::Z,true);Key(EKeys::F,true);Step=2;}
        if(Step==2&&E>2.4){PC->Navigation->Navigate(Body->GetNavAgentLocation()+FVector(200,0,0));Check(!PC->IsTargeting()&&!PC->Navigation->IsFollowing(),TEXT("Major panel blocks world shortcuts and navigation everywhere"));Key(EKeys::Z,false);PC->ControlsUI->Close();Step=3;}
        if(Step==3&&E>2.8){Check(!PC->CanWorldCommand(),TEXT("Closing a panel requires all held physical keys to release"));Key(EKeys::F,false);Step=4;}
    }
    if(Phase==7)
    {
        if(!Step&&E>.6){InitialRevision=PC->OwnerState.Build.Revision;InitialStrength=PC->OwnerState.Build.Attributes[0];PC->ToggleDevelopmentPanel();PC->DevelopmentUI->Choose(1,0);Key(EKeys::F,true);Step=1;}
        if(Step==1&&E>1){PC->DevelopmentUI->Choose(8,0);Step=2;}
        if(Step==2&&E>1.5){Check(!PC->DevelopmentUI->IsOpen()&&PC->OwnerState.Build.Revision==InitialRevision,TEXT("Apply closes and arms without spend or replaying a held key"));Key(EKeys::F,false);Step=3;}
        if(Step==3&&E>2){Key(EKeys::F,true);Step=4;}
        if(Step==4&&E>4.5){Key(EKeys::F,false);Check(PC->OwnerState.Build.Attributes[0]==InitialStrength+1,TEXT("Fresh native F hold receives authoritative preparation commit"));Step=5;}
    }
    if(Phase==8)
    {
        if(!Step&&E>.4){BodyAnchor=Body->GetActorLocation();PC->Navigation->PanTo(BodyAnchor+FVector(200,200,0));CameraMask=PC->LocalView()->LastView().Current;Key(EKeys::SpaceBar,true);Step=1;}
        if(Step==1&&E>.8){Check(FVector::Dist2D(Body->CameraArm->GetComponentLocation(),Body->GetActorLocation())<1,TEXT("Mapped Space follows own body while held"));Key(EKeys::SpaceBar,false);CameraAnchor=Body->CameraArm->GetComponentLocation();Step=2;}
        if(Step==2&&E>1.1){Check(PC->LocalView()->LastView().Current==CameraMask,TEXT("Camera motion does not expand authorized sight"));PC->Navigation->Navigate(Body->GetNavAgentLocation()+FVector(200,0,0));Step=3;}
        if(Step==3&&E>3){Check(FVector::Dist2D(BodyAnchor,Body->GetActorLocation())>140&&FVector::Dist2D(CameraAnchor,Body->CameraArm->GetComponentLocation())<5,TEXT("Space release leaves free camera in place while native navigation continues"));Key(EKeys::Home,true);Step=4;}
        if(Step==4&&E>3.3){Key(EKeys::Home,false);Check(PC->Navigation->bFollowCamera&&FVector::Dist2D(Body->CameraArm->GetComponentLocation(),Body->GetActorLocation())<1,TEXT("Mapped Home restores persistent follow"));Step=5;}
    }
    if(Phase==9)
    {
        if(!Step&&E>.5){Key(EKeys::A,true);Step=1;}
        if(Step==1&&E>.8){Key(EKeys::A,false);Check(PC->IsTargeting()&&PC->OwnerState.BasicReadyAt==0,TEXT("Native A arms a direction without attacking on key press"));PC->ConfirmArmed(Body->GetActorLocation()+FVector(0,80,0),FGuid());Key(EKeys::S,true);Step=2;}
        if(Step==2&&E>1.1){Key(EKeys::S,false);Step=3;}
    }
    if(Phase==10&&E>1){Finish();}
}
void ALDInputProbe::Finish()
{
    SetActorTickEnabled(false);auto Report=MakeShared<FJsonObject>();Report->SetStringField(TEXT("profile"),bHUD?TEXT("B53 engineering fixture: B52 synthetic native keys and observation/ray fixtures plus direct minimap gestures, positions, skills, HP, points, one cooldown reset, 100-gold death balance and terminal deadline; ward consumption, death/return, holds and authority timers remain real; not human evidence"):TEXT("B52 engineering fixture: synthetic native key events and explicit observation/ray fixtures; setup positions, skills, HP, points and one cooldown reset are labelled; holds and authority timers remain real; not human evidence"));
    if(GetNetMode()==NM_Client){auto* PC=CastChecked<ALDPlayerController>(GetOwner());auto Echo=PC->ProbeRtts;Echo.Sort();Check(Echo.Num()>=8,TEXT("Independent native input client measured transport echo"));Report->SetNumberField(TEXT("medianEchoRttMs"),Echo.IsEmpty()?0:Echo[Echo.Num()/2]);}
    Report->SetNumberField(TEXT("failures"),Failures);TArray<TSharedPtr<FJsonValue>> Values;for(const auto& C:Checks){Values.Add(MakeShared<FJsonValueString>(C));}Report->SetArrayField(TEXT("checks"),Values);
    FString Directory,Output;FParse::Value(FCommandLine::Get(),TEXT("LDProbeReportDir="),Directory);FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Output));if(!FFileHelper::SaveStringToFile(Output,*FPaths::Combine(Directory,Role+TEXT(".json")))){++Failures;}FPlatformMisc::RequestExitWithStatus(false,Failures?1:0);
}
