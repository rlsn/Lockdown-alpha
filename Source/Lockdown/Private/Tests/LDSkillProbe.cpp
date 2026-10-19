#include "Tests/LDDevelopmentProbe.h"
#include "Combat/LDCombatDirector.h"
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Vision/LDVisionDirector.h"
#include "Vision/LDWorldViewRelay.h"
#include "Objectives/LDObjectiveDirector.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "AIController.h"

void ALDDevelopmentProbe::SkillCheck()
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>(); auto* C=Mode->Combat.Get();
    auto* PC=CastChecked<ALDPlayerController>(GetWorld()->GetFirstPlayerController()); auto* O=Mode->StateFor(PC);
    auto* PlayerBody=CastChecked<ALDCharacter>(PC->GetPawn()); FString Reason; const FVector Aim=FVector::ForwardVector;
    Check(!C->RequestEquipped(PlayerBody,O->Build.Revision,1,1,Aim,Reason),TEXT("Equipped skills cannot activate in lobby"));
    Mode->SetLobbyChoice(PC,1,true,Reason); Mode->StartDevelopmentMatch(PC,Reason); C->SetActorTickEnabled(false);
    for(TActorIterator<ALDResident> It(GetWorld());It;++It)
    {It->SetActorTickEnabled(false);It->GetCharacterMovement()->DisableMovement();if(auto* AI=Cast<AAIController>(It->GetController())){AI->StopMovement();}It->SetActorLocation(FVector(1800,1000,92));}
    auto* Victim=GetWorld()->SpawnActor<ALDCharacter>(FVector(-1720,-900,92),FRotator::ZeroRotator);
    auto* Third=GetWorld()->SpawnActor<ALDCharacter>(FVector(1800,1000,92),FRotator::ZeroRotator);
    const double Now=GetWorld()->GetTimeSeconds(); uint32 Token=100;
    auto Reset=[&]()
    {
        for(auto* Body:{PlayerBody,Victim,Third})
        {auto* S=C->StateFor(Body);*S=FLDBodyCombatState();S->QuietSince=Now-10;Body->GetCharacterMovement()->StopMovementImmediately();Body->GetCharacterMovement()->SetMovementMode(MOVE_Walking);Body->SetSprint(false);Body->SetFacing(0);}
        const uint32 Revision=O->Build.Revision+1;O->Build=LDDevelopment::StartingBuild(1);O->Build.Revision=Revision;
        for(int32 I=1;I<LDDevelopment::SkillCount;++I){O->Build.Ranks[I]=1;}
        O->Build.Slots={ELDSkill::Step,ELDSkill::Ambush,ELDSkill::Spark,ELDSkill::Patch};O->Hp=O->MaxHp=95;O->ProtectedUntil=0;
        C->StateFor(PlayerBody)->Hp=95;PlayerBody->SetActorLocation(FVector(-1800,-900,92));Victim->SetActorLocation(FVector(-1720,-900,92));Third->SetActorLocation(FVector(1800,1000,92));Mode->SyncOwner(PC);
    };
    auto CastSlot=[&](uint8 Slot){return C->RequestEquipped(PlayerBody,O->Build.Revision,Slot,++Token,Aim,Reason);};
    auto HoldUntil=[&](double At){C->RefreshSkillHold(PlayerBody,Token,1,false);C->StateFor(PlayerBody)->LastSkillHold=At;C->Sample(At);};
    Reset();
    Check(!C->RequestBody(PlayerBody,1,Aim,Reason)&&!C->RequestBody(PlayerBody,2,Aim,Reason),TEXT("Legacy body and controller wrappers cannot bypass v04 equipment"));
    Check(!C->RequestEquipped(PlayerBody,O->Build.Revision+1,2,++Token,Aim,Reason)&&!C->RequestEquipped(PlayerBody,O->Build.Revision,4,++Token,Aim,Reason),TEXT("Stale build revision and fifth slot rejected"));
    O->Build.Slots[2]=ELDSkill::None;
    Check(!CastSlot(2)&&O->Build.Cooldowns[3]==0,TEXT("Learned but unequipped Spark cannot activate or consume cooldown"));
    O->Build.Slots[2]=ELDSkill::Spark;O->Build.Ranks[3]=0;
    Check(!CastSlot(2),TEXT("Forged unlearned slot rejected"));
    Reset();C->StateFor(PlayerBody)->ProtectionUntil=Now+4;Check(!CastSlot(0),TEXT("Protection blocks Step"));
    Reset();C->StateFor(PlayerBody)->Hp=0;Check(!CastSlot(2),TEXT("Dead source cannot cast"));
    Reset();C->QueueSkill(PC,O->Match,O->Life,++Token,O->Build.Revision,2,Aim);++O->Build.Revision;C->Sample(Now);
    Check(O->Build.Cooldowns[3]==0,TEXT("Queued skill revalidates build revision at authority sample"));
    Reset();C->StateFor(PlayerBody)->Hp=50;C->QueueSkill(PC,O->Match,O->Life,++Token,O->Build.Revision,3,Aim);C->ReleaseSkillHold(PC,Token);C->Sample(Now);
    Check(O->Build.Cooldowns[4]==0,TEXT("Release before queued acceptance cannot start a held channel"));

    Reset();O->Build.Attributes[LDDevelopment::INT]=12;O->Build.Ranks[3]=2;
    auto* Ward=GetWorld()->SpawnActor<ALDWard>(FVector(-1770,-900,15),FRotator::ZeroRotator);Ward->ExpiresAt=Now+60;Ward->Identity=FGuid::NewGuid();
    Check(CastSlot(2)&&C->StateFor(PlayerBody)->Target==Victim&&C->StateFor(Victim)->WarningUntil>=Now+.39,TEXT("Spark ignores nearer ward, locks living target and warns privately"));
    Check(FMath::IsNearlyEqual(C->StateFor(PlayerBody)->ImpactAt-Now,.4)&&FMath::IsNearlyEqual(O->Build.Cooldowns[3]-Now,6.),TEXT("Spark starts fixed windup and persistent cooldown on acceptance"));
    Third->SetActorLocation(FVector(-1760,-900,92));O->Build.Attributes[LDDevelopment::INT]=16;O->Build.Ranks[3]=3;
    C->Sample(Now+.401);
    Check(FMath::IsNearlyEqual(C->StateFor(Victim)->Hp,69.5)&&C->StateFor(Third)->Hp==100&&Ward->Hp==20,TEXT("Spark snapshots fractional attribute/rank damage and never retargets"));
    Check(C->StateFor(PlayerBody)->Action==3&&FMath::IsNearlyEqual(C->StateFor(PlayerBody)->RecoveryUntil,Now+.751),TEXT("Spark retains 350ms recovery"));Ward->Destroy();
    Reset();CastSlot(2);Victim->SetActorLocation(FVector(-1200,-900,92));C->Sample(Now+.401);
    Check(C->StateFor(Victim)->Hp==100&&C->StateFor(PlayerBody)->Action==3,TEXT("Spark range escape misses and retains recovery"));
    Reset();Victim->SetActorLocation(FVector(-1880,-900,92));Check(!CastSlot(2),TEXT("Spark rejects targets outside its aim cone"));
    Reset();PlayerBody->SetActorLocation(FVector(-1160,-450,92));Victim->SetActorLocation(FVector(-1065,-450,92));Check(!CastSlot(2),TEXT("Static wall blocks Spark acceptance"));
    Reset();CastSlot(2);PlayerBody->SetActorLocation(FVector(-1160,-450,92));Victim->SetActorLocation(FVector(-1065,-450,92));C->Sample(Now+.401);
    Check(C->StateFor(Victim)->Hp==100,TEXT("Spark rechecks geometric sight at impact"));
    Reset();CastSlot(2);C->StateFor(Victim)->ProtectionUntil=Now+10;C->Sample(Now+.401);
    Check(C->StateFor(Victim)->Hp==100,TEXT("Spark rechecks target protection at impact"));
    Reset();CastSlot(2);C->RequestBody(Victim,0,-Aim,Reason);C->Sample(Now+.151);
    Check(C->StateFor(PlayerBody)->Action==0&&O->Build.Cooldowns[3]>Now+5,TEXT("Early positive damage cancels Spark without cooldown refund"));
    Reset();CastSlot(2);C->RequestBody(Victim,0,-Aim,Reason);C->StateFor(Victim)->ImpactAt=Now+.4;C->Sample(Now+.401);
    Check(C->StateFor(Victim)->Hp==76&&C->StateFor(PlayerBody)->Hp==75,TEXT("Due Spark and counterhit survive the same damage batch"));

    Reset();Check(CastSlot(1)&&C->StateFor(PlayerBody)->ExposedUntil>Now,TEXT("Equipped Ambush locks rear target and exposes source"));C->Sample(Now+.501);
    Check(FMath::IsNearlyEqual(C->StateFor(Victim)->Hp,39.5)&&C->StateFor(PlayerBody)->Action==3,TEXT("Accepted exposure does not invalidate 60.5-damage Ambush"));
    Reset();CastSlot(1);Victim->SetFacing(180);C->Sample(Now+.501);Check(C->StateFor(Victim)->Hp==100,TEXT("Turning counters equipped Ambush"));
    Reset();C->StateFor(Victim)->LastDealt=Now;Check(!CastSlot(1),TEXT("Alert target history blocks Ambush before cooldown"));
    Reset();Check(!CastSlot(3)&&O->Build.Cooldowns[4]==0,TEXT("Full HP rejects Patch without cooldown"));
    Reset();C->StateFor(PlayerBody)->Hp=50;Check(CastSlot(3)&&C->StateFor(PlayerBody)->Action==7,TEXT("Patch starts one held stationary action"));
    O->Build.Attributes[LDDevelopment::WIS]=16;O->Build.Ranks[4]=3;HoldUntil(Now+1.001);
    Check(C->StateFor(PlayerBody)->Hp==77&&O->Build.Cooldowns[4]>Now+11,TEXT("Patch snapshots healing and completes once with fresh held evidence"));
    Reset();C->StateFor(PlayerBody)->Hp=90;CastSlot(3);HoldUntil(Now+1.001);Check(C->StateFor(PlayerBody)->Hp==95,TEXT("Patch caps at current maximum HP"));
    Reset();C->StateFor(PlayerBody)->Hp=50;CastSlot(3);C->ReleaseSkillHold(PC,Token);C->Sample(Now+1.001);
    Check(C->StateFor(PlayerBody)->Hp==50&&O->Build.Cooldowns[4]>Now+11,TEXT("Released Patch never heals and retains cooldown"));
    Reset();C->StateFor(PlayerBody)->Hp=50;CastSlot(3);C->Sample(Now+.251);Check(C->StateFor(PlayerBody)->Action==0,TEXT("250ms stale held input cancels Patch"));
    Reset();C->StateFor(PlayerBody)->Hp=50;CastSlot(3);C->RefreshSkillHold(PlayerBody,Token,1,true);C->RefreshSkillHold(PlayerBody,Token,2,false);
    Check(C->StateFor(PlayerBody)->Action==0,TEXT("A movement pulse cannot be erased by a later stationary hold"));
    Reset();C->StateFor(PlayerBody)->Hp=50;CastSlot(3);PlayerBody->SetActorLocation(PlayerBody->GetActorLocation()+FVector(3,0,0));HoldUntil(Now+1.001);
    Check(C->StateFor(PlayerBody)->Hp==50,TEXT("Authoritative displacement cancels Patch"));
    Reset();C->StateFor(PlayerBody)->Hp=50;CastSlot(3);Check(C->RequestBody(PlayerBody,0,Aim,Reason)&&C->StateFor(PlayerBody)->Action==1,TEXT("Accepted basic attack interrupts held Patch"));
    Reset();C->StateFor(PlayerBody)->Hp=50;CastSlot(3);C->RequestBody(Victim,0,-Aim,Reason);C->StateFor(Victim)->ImpactAt=Now+1;HoldUntil(Now+1.001);
    Check(C->StateFor(PlayerBody)->Hp==30&&C->StateFor(PlayerBody)->Action==0,TEXT("Positive damage on Patch deadline wins over healing"));

    for(ELDSkill Skill:{ELDSkill::Parley,ELDSkill::Analyze,ELDSkill::Survey,ELDSkill::Heave,ELDSkill::Pickpocket})
    {
        Reset();O->Build.Slots[3]=Skill;int32 Commits=0;double SnapshotValue=-1;
        C->RegisterSkillTask(Victim,{Skill},[&](ALDCharacter*,ELDSkill Used,const FLDCharacterBuild& Snapshot,uint32,ELDSkillTaskPhase Stage,FString& Feedback)
        {if(Stage==ELDSkillTaskPhase::Commit){++Commits;SnapshotValue=LDDevelopment::SkillValue(Snapshot,Used);Feedback=TEXT("Fixture capability committed.");}return true;});
        const double Value=LDDevelopment::SkillValue(O->Build,Skill),Duration=LDDevelopment::ChannelSeconds(Skill);
        Check(CastSlot(3)&&FMath::IsNearlyEqual(C->StateFor(PlayerBody)->ImpactAt-Now,Duration),FString::Printf(TEXT("%s uses equipped held channel and catalog duration"),LDDevelopment::SkillName(Skill)));
        O->Build.Ranks[uint8(Skill)]=3;HoldUntil(Now+Duration+.001);C->Sample(Now+Duration+.05);
        Check(Commits==1&&SnapshotValue==Value&&O->Build.Cooldowns[uint8(Skill)]>=Now+LDDevelopment::Cooldown(Skill),FString::Printf(TEXT("%s commits capability once with snapshot and persistent cooldown (fixture outcome)"),LDDevelopment::SkillName(Skill)));
    }
    Reset();O->Build.Slots[3]=ELDSkill::Heave;
    C->RegisterSkillTask(Victim,{ELDSkill::Heave},[](ALDCharacter*,ELDSkill,const FLDCharacterBuild&,uint32,ELDSkillTaskPhase,FString& Feedback){Feedback=TEXT("Requirements unmet.");return false;});
    Check(!CastSlot(3)&&O->Build.Cooldowns[9]==0,TEXT("Static task validation rejects before accepting cooldown"));
    Reset();O->Build.Slots[3]=ELDSkill::Heave;int32 Reservations=0,TaskCommits=0;
    C->RegisterSkillTask(Victim,{ELDSkill::Heave},[&](ALDCharacter*,ELDSkill,const FLDCharacterBuild&,uint32,ELDSkillTaskPhase Stage,FString&)
    {if(Stage==ELDSkillTaskPhase::Accept){++Reservations;}if(Stage==ELDSkillTaskPhase::Commit){++TaskCommits;}return true;});
    CastSlot(3);C->ReleaseSkillHold(PC,Token);C->Sample(Now+2.001);
    Check(Reservations==1&&TaskCommits==0&&O->Build.Cooldowns[9]>Now,TEXT("Cancelled task retains acceptance reservation and cooldown without a commit"));
    Reset();O->Build.Slots[3]=ELDSkill::Heave;CastSlot(3);Victim->SetActorLocation(FVector(-1500,-900,92));HoldUntil(Now+2.001);
    Check(TaskCommits==0,TEXT("Utility target range is rechecked before capability commit"));
    Reset();PC->ServerContextRequest(O->Match,O->Life,++Token,9,FGuid());C->Sample(Now);
    Check(O->Build.Cooldowns[9]==0&&C->StateFor(PlayerBody)->Action==0,TEXT("Context RPC cannot encode an arbitrary learned skill ID"));
    Reset();O->Build.Slots[3]=ELDSkill::Pickpocket;Victim->SetFacing(180);Check(!CastSlot(3),TEXT("Pickpocket requires rear bearing before capability dispatch"));
    Reset();O->Build.Slots[3]=ELDSkill::Pickpocket;C->StateFor(Victim)->ExposedUntil=Now+10;Check(!CastSlot(3),TEXT("Pickpocket rejects exposed target without revealing its identity type"));
    Reset();Check(!C->RequestEquipped(Victim,1,0,++Token,Aim,Reason),TEXT("Untrained NPC cannot use an arbitrary skill wrapper"));
    auto NPC=LDDevelopment::StartingBuild(2);Check(C->RegisterNPCBuild(Victim,NPC)&&C->RequestEquipped(Victim,NPC.Revision,0,++Token,-Aim,Reason),TEXT("Trained NPC invokes the same equipped slot path"));
    Check(!C->RequestEquipped(Victim,NPC.Revision,1,++Token,-Aim,Reason),TEXT("NPC skill requests obey the occupied action slot"));
    *C->StateFor(Victim)=FLDBodyCombatState();auto MedicNPC=LDDevelopment::StartingBuild(0);C->StateFor(Victim)->Hp=20;
    C->RegisterNPCBuild(Victim,MedicNPC);const uint32 NPCToken=++Token;
    Check(C->RequestEquipped(Victim,MedicNPC.Revision,1,NPCToken,-Aim,Reason),TEXT("NPC Patch requires its learned equipped slot"));
    C->RefreshSkillHold(Victim,NPCToken,1,false);C->StateFor(Victim)->LastSkillHold=Now+1.001;C->Sample(Now+1.001);
    Check(C->StateFor(Victim)->Hp==45&&!C->RequestEquipped(Victim,MedicNPC.Revision,1,++Token,-Aim,Reason),TEXT("NPC Patch heals through shared channel and cannot bypass cooldown"));
    C->RegisterNPCBuild(Victim,MedicNPC);Check(!C->RequestEquipped(Victim,MedicNPC.Revision,1,++Token,-Aim,Reason),TEXT("NPC build refresh cannot clear an existing cooldown"));
    Reset();C->QueueSkill(PC,O->Match,O->Life,++Token,O->Build.Revision,2,Aim);C->QueueSkill(PC,O->Match,O->Life,++Token,O->Build.Revision,0,Aim);C->Sample(Now);
    Check(C->StateFor(PlayerBody)->Action==4&&O->Build.Cooldowns[1]>Now+6&&O->Build.Cooldowns[3]==0,TEXT("Same-sample Step arbitration accepts one action, without charging superseded Spark"));
    const double StepCooldown=O->Build.Cooldowns[1];auto Plan=LDDevelopment::PlanFrom(O->Build);Plan.Slots[0]=ELDSkill::None;FLDCharacterBuild Prepared;
    Check(LDDevelopment::Prepare(O->Build,Plan,Prepared,Reason)&&Prepared.Cooldowns[1]==StepCooldown,TEXT("Unequipping preserves an accepted Step cooldown"));
    Reset();C->StateFor(PlayerBody)->Hp=50;CastSlot(3);PC->ServerSkillRelease(O->Match,O->Life-1,Token);Check(C->StateFor(PlayerBody)->Action==7,TEXT("Old-life release cannot cancel a new-life channel"));
    C->ReleaseSkillHold(PC,Token);
    Reset();CastSlot(1);const double AmbushCooldown=O->Build.Cooldowns[2];const uint32 Life=O->Life;
    C->StateFor(PlayerBody)->Hp=1;C->RequestBody(Victim,0,-Aim,Reason);C->Sample(Now+.151);
    Check(!O->bActive&&O->Build.Cooldowns[2]==AmbushCooldown&&O->ActiveSkill==ELDSkill::None,TEXT("Actual interrupted-cast death retains cooldown and clears active skill UI state"));
    Mode->ReturnDueContenders(O->ReturnAt+.001);auto* ReturnedBody=Cast<ALDCharacter>(PC->GetPawn());
    if(ReturnedBody){C->StateFor(ReturnedBody)->ProtectionUntil=0;}
    Check(ReturnedBody&&O->Life==Life+1&&O->Build.Cooldowns[2]==AmbushCooldown&&!C->RequestEquipped(ReturnedBody,O->Build.Revision,1,++Token,Aim,Reason),TEXT("Fresh returned body cannot clear persistent Ambush cooldown"));
    if(ReturnedBody)
    {
        C->StateFor(ReturnedBody)->Hp=50;C->RequestEquipped(ReturnedBody,O->Build.Revision,3,++Token,Aim,Reason);
        GetWorld()->GetGameState<ALDGameState>()->EndReason=2;C->Sample(Now+.05);
        Check(C->StateFor(ReturnedBody)->Action==0&&O->ActiveSkill==ELDSkill::None&&C->StateFor(ReturnedBody)->Hp==50,TEXT("Results cancel pending skill state without healing or leaving a stuck channel"));
    }
}

void ALDDevelopmentProbe::SkillServerTick()
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* Public=GetWorld()->GetGameState<ALDGameState>();auto* C=Mode->Combat.Get();
    if(!Phase)
    {
        for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
        {if(auto* PC=Cast<ALDPlayerController>(It->Get());PC&&PC->GetPawn()){if(PC->ProbeRole==1){A=PC;}if(PC->ProbeRole==2){B=PC;}}}
        if(A.IsValid()&&B.IsValid()){Check(Public->LobbyCount==2,TEXT("Skill network has two real remote owning controllers"));Enter(1);}return;
    }
    if(Phase==8){if(FPlatformTime::Seconds()-PhaseAt>3){Finish();}return;}
    if(!A.IsValid()||!B.IsValid()){Check(false,TEXT("Skill client disconnected early"));Finish();return;}
    const double Elapsed=FPlatformTime::Seconds()-PhaseAt;auto* AS=Mode->StateFor(A.Get());auto* BS=Mode->StateFor(B.Get());
    auto* AB=CastChecked<ALDCharacter>(A->GetPawn());auto* BB=CastChecked<ALDCharacter>(B->GetPawn());
    if(Phase==1&&AS->bReady&&BS->bReady){Enter(2);}
    else if(Phase==2&&Public->bMatchStarted&&Elapsed>1)
    {
        for(TActorIterator<ALDResident> It(GetWorld());It;++It)
        {It->SetActorTickEnabled(false);It->GetCharacterMovement()->DisableMovement();if(auto* AI=Cast<AAIController>(It->GetController())){AI->StopMovement();}It->SetActorLocation(FVector(1800,1000,92));}
        AS->Build.Ranks.Init(1,LDDevelopment::SkillCount);AS->Build.Ranks[0]=0;AS->Build.Slots={ELDSkill::Step,ELDSkill::Spark,ELDSkill::Patch,ELDSkill::Ambush};++AS->Build.Revision;
        AB->SetActorLocation(FVector(-1800,-900,92));BB->SetActorLocation(FVector(-1480,-900,92));AB->GetCharacterMovement()->StopMovementImmediately();BB->GetCharacterMovement()->StopMovementImmediately();
        C->StateFor(AB)->ProtectionUntil=C->StateFor(BB)->ProtectionUntil=0;C->StateFor(AB)->Hp=60;C->StateFor(BB)->Hp=100;Mode->SyncOwner(A.Get());Mode->SyncOwner(B.Get());
        Check(LDDevelopment::Valid(AS->Build),TEXT("Labelled mixed loadout fixture is valid"));Enter(3);
    }
    else if(Phase==3&&Elapsed>2)
    {
        Check(C->StateFor(BB)->Hp==76&&AS->Build.Cooldowns[3]>GetWorld()->GetTimeSeconds(),TEXT("Remote duplicate Spark request hits once with retained cooldown"));
        Check(C->StateFor(AB)->Hp==60&&C->StateFor(AB)->Action==0,TEXT("Combat key release did not cancel accepted cast or leave action occupied"));Enter(4);
    }
    else if(Phase==4&&Elapsed>2.2)
    {
        Check(C->StateFor(AB)->Hp==87&&AS->Build.Cooldowns[4]>GetWorld()->GetTimeSeconds(),TEXT("Delayed held Patch heals once through owning RPC refreshes"));
        BS->Build.Ranks[1]=3;BS->Build.Slots[3]=ELDSkill::Step;BS->Build.Attributes[LDDevelopment::DEX]=16;++BS->Build.Revision;
        BB->SetActorLocation(FVector(-1160,-450,92));BB->GetCharacterMovement()->StopMovementImmediately();Mode->SyncOwner(B.Get());Enter(5);
    }
    else if(Phase==5&&Elapsed>1.5)
    {
        const double Travel=AB->GetActorLocation().Y+900;
        Check(Travel>330&&Travel<420&&AS->Build.Cooldowns[1]>GetWorld()->GetTimeSeconds(),FString::Printf(TEXT("Remote native Step travels %.2f cm within its 380 cm rule"),Travel));
        Check(BB->GetActorLocation().X<=-1125&&BS->Build.Cooldowns[1]>GetWorld()->GetTimeSeconds(),TEXT("Rank-three 420 cm Step stops at native wall collision"));
        C->StateFor(AB)->Hp=50;AS->Build.Cooldowns[4]=0;Mode->SyncOwner(A.Get());Enter(6);
    }
    else if(Phase==6&&Elapsed>1.5)
    {Check(C->StateFor(AB)->Hp==50&&C->StateFor(AB)->Action==0&&AS->Build.Cooldowns[4]>GetWorld()->GetTimeSeconds(),TEXT("Released delayed Patch cancels without healing or refund"));Enter(7);}
    else if(Phase==7&&Elapsed>1.5){Enter(8);}
}
void ALDDevelopmentProbe::SkillClientTick()
{
    auto* PC=Cast<ALDPlayerController>(GetOwner());if(!PC||!PC->OwnerState.Match){return;}
    if(FPlatformTime::Seconds()-LastPing>.2){LastPing=FPlatformTime::Seconds();PC->ServerProbePing(LastPing);}
    if(PC->ProbePhase!=Phase){Phase=PC->ProbePhase;PhaseAt=FPlatformTime::Seconds();bChecked=false;bChoiceSent=false;}
    const double Elapsed=FPlatformTime::Seconds()-PhaseAt;const auto& O=PC->OwnerState;
    if(Role==TEXT("B")&&Phase==3&&O.WarningUntil>GetWorld()->GetGameState<ALDGameState>()->GetServerWorldTimeSeconds()){bSawSkillWarning=true;}
    if(Phase==1)
    {if(!bChoiceSent){PC->ChooseLobbyClass(Role==TEXT("A")?1:2);bChoiceSent=true;}if(!bReadySent&&O.Build.ClassId==(Role==TEXT("A")?1:2)&&Elapsed>.4){PC->ToggleLobbyReady();bReadySent=true;}}
    if(Phase==2&&!bStartSent&&O.bLobbyHost){PC->StartLobby();bStartSent=true;}
    if(Role==TEXT("A"))
    {
        if(Phase==3&&!bChoiceSent&&Elapsed>.4)
        {
            bChoiceSent=true;PC->ServerSkillAction(O.Match,O.Life,7001,O.Build.Revision,1,FVector::ForwardVector);PC->ServerSkillAction(O.Match,O.Life,7001,O.Build.Revision,1,FVector::ForwardVector);
            PC->ServerSkillRelease(O.Match,O.Life,7001);PC->ServerSkillAction(O.Match,O.Life,7002,O.Build.Revision-1,1,FVector::ForwardVector);
        }
        if((Phase==4||Phase==6)&&!bChoiceSent&&Elapsed>.4)
        {bChoiceSent=true;PC->ServerSkillAction(O.Match,O.Life,Phase==4?7010:7030,O.Build.Revision,2,FVector::ForwardVector);}
        if((Phase==4||Phase==6)&&Elapsed>.4&&Elapsed<1.8&&FPlatformTime::Seconds()-SkillRefreshAt>.1){SkillRefreshAt=FPlatformTime::Seconds();PC->ServerSkillHold(O.Match,O.Life,Phase==4?7010:7030,uint32(Elapsed*1000),false);}
        if(Phase==6&&Elapsed>.65&&Elapsed<.8){PC->ServerSkillRelease(O.Match,O.Life,7030);}
        if(Phase==5&&!bChoiceSent&&Elapsed>.4){bChoiceSent=true;PC->ServerSkillAction(O.Match,O.Life,7020,O.Build.Revision,0,FVector(0,1,0));}
    }
    else if(Phase==5&&!bChoiceSent&&Elapsed>.4){bChoiceSent=true;PC->ServerSkillAction(O.Match,O.Life,7020,O.Build.Revision,3,FVector::ForwardVector);}
    if(Phase==7&&!bChecked&&Elapsed>.7)
    {
        bChecked=true;
        Check(O.bDevelopmentProfile&&O.bActive,TEXT("Owner retains active private v04 state after delayed skill actions"));
        Check(Role==TEXT("B")?O.Hp==76:O.Hp==50,TEXT("Owner receives exact resulting private HP"));
        Check(Role==TEXT("B")?O.Build.ClassId==2:O.Build.Cooldowns[1]>0&&O.Build.Cooldowns[3]>0&&O.Build.Cooldowns[4]>0,TEXT("Skill cooldowns remain in the correct owner's build"));
        int32 Canonical=0;for(TActorIterator<ALDCharacter> It(GetWorld());It;++It){++Canonical;}
        Check(Canonical==1&&PC->LocalView()&&PC->RootUI,TEXT("Skill traffic retains one own canonical body, filtered relay and native UI"));
        if(Role==TEXT("B")){Check(bSawSkillWarning,TEXT("Remote victim receives private cast warning before delayed Spark impact"));}
    }
    if(Phase==8&&Elapsed>1){Finish();}
}
