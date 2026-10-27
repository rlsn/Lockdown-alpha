#include "Combat/LDCombatDirector.h"
#include "World/LDEnvironmentDirector.h"
#include "Combat/LDCombatRules.h"
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Objectives/LDObjectiveDirector.h"
#include "Vision/LDVisionDirector.h"
#include "Core/LDGroundQueries.h"
#include "Development/LDDevelopmentDirector.h"
#include "Development/LDWorldChecks.h"
#include "Engine/World.h"

FLDCharacterBuild* ALDCombatDirector::BuildFor(ALDCharacter* Body)
{
    if (!Body) { return nullptr; }
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    if (auto* PC=Cast<ALDPlayerController>(Body->GetController()))
    { auto* O=Mode->StateFor(PC); return O&&O->bActive?&O->Build:nullptr; }
    auto* S=StateFor(Body); return S&&S->bHasNPCBuild?&S->NPCBuild:nullptr;
}
bool ALDCombatDirector::RegisterNPCBuild(ALDCharacter* Body,const FLDCharacterBuild& Build)
{
    auto* S=StateFor(Body);
    if (!HasAuthority()||!S||Cast<ALDPlayerController>(Body->GetController())||S->Action||!LDDevelopment::Valid(Build)) { return false; }
    // Reconfiguration may not reset a previously used NPC's absolute cooldowns.
    const auto Existing=S->NPCBuild.Cooldowns;
    const bool WasConfigured=S->bHasNPCBuild;
    S->NPCBuild=Build;
    if (WasConfigured) { for (int32 I=1;I<FMath::Min(Existing.Num(),S->NPCBuild.Cooldowns.Num());++I) { S->NPCBuild.Cooldowns[I]=FMath::Max(Existing[I],Build.Cooldowns[I]); } }
    S->bHasNPCBuild=true; return true;
}
void ALDCombatDirector::RegisterSkillTask(AActor* Target,const TSet<ELDSkill>& Skills,FSkillTask Task)
{
    if (!HasAuthority()||!IsValid(Target)||Target->GetWorld()!=GetWorld()||!Task) { return; }
    FTask Entry; Entry.Skills=Skills; Entry.Execute=MoveTemp(Task); SkillTasks.Add(Target,MoveTemp(Entry));
}
bool ALDCombatDirector::SkillTargetLegal(ALDCharacter* Source,AActor* Target,ELDSkill Skill) const
{
    if (!IsValid(Source)||!IsValid(Target)||Source==Target) { return false; }
    if (Skill==ELDSkill::Pickpocket)
    {
        auto* Body=Cast<ALDCharacter>(Target); const auto* T=StateFor(Body);
        return Body&&T&&LegalTarget(Source,Target,100,nullptr,0)&&GetWorld()->GetTimeSeconds()>=T->ExposedUntil
            &&LDCombat::IsRear(Source->GetActorLocation(),Body->GetActorLocation(),Body->GetActorForwardVector());
    }
    const FVector From=Source->GetActorLocation(),To=Target->GetActorLocation();
    if (FVector::DistSquared2D(From,To)>120*120||!LDGround::ClearSegment(GetWorld(),From,To)) { return false; }
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    if (!Mode->Vision) { return false; }
    if (auto* PC=Cast<ALDPlayerController>(Source->GetController())) { return Mode->Vision->CanSee(PC,To); }
    return Mode->Vision->Ray({From,720,Mode->Vision->LevelAt(From)},To);
}
void ALDCombatDirector::QueueSkill(ALDPlayerController* PC,uint32 Match,uint32 Life,uint32 RequestId,uint32 Revision,uint8 Slot,const FVector& Direction,FGuid Observed,FVector Point)
{
    FlushPastCommand();
    if (const auto* O=GetWorld()->GetAuthGameMode<ALDGameMode>()->StateFor(PC);!O||Slot>=LDDevelopment::Slots(O->Build)) { PC->CompleteCombatRequest(RequestId,TEXT("Invalid skill slot.")); return; }
    int32 Count=0; for (const auto& Intent:Intents) { if (Intent.Controller==PC) { ++Count; } }
    if (Count>=3) { PC->CompleteCombatRequest(RequestId,TEXT("Too many queued actions.")); return; }
    Intents.Add({PC,Match,Life,RequestId,uint8(3+Slot),Direction,Revision,Observed,Point});
}
void ALDCombatDirector::CancelSkill(ALDCharacter* Body,FLDBodyCombatState& S)
{
    if(S.Action==7&&S.Skill==ELDSkill::Pickpocket){if(auto* D=GetWorld()->GetAuthGameMode<ALDGameMode>()->WorldChecks.Get()){D->CancelAttempt(Body,S.SkillToken);}}
    S.Action=0; S.Skill=ELDSkill::None; S.Target.Reset(); S.SkillToken=0;
    CastChecked<ULDCharacterMovementComponent>(Body->GetCharacterMovement())->bCombatWalkOnly=false;
}
void ALDCombatDirector::RefreshSkillHold(ALDCharacter* Body,uint32 Token,uint32 Sequence,bool Moving)
{
    FlushPastCommand();
    auto* S=StateFor(Body);
    if (!HasAuthority()||!S||S->Action!=7||S->SkillToken!=Token||Sequence<=S->HoldSequence) { return; }
    S->HoldSequence=Sequence; S->LastSkillHold=GetWorld()->GetTimeSeconds(); S->bHoldMoving=Moving;
    // A movement pulse cannot be hidden by a subsequent stationary refresh in this sample.
    if (Moving) { CancelSkill(Body,*S); Sync(Body,*S); }
}
void ALDCombatDirector::ReleaseSkillHold(ALDPlayerController* PC,uint32 Token)
{
    FlushPastCommand();
    if (!HasAuthority()||!PC) { return; }
    auto& Released=ReleasedSkills.FindOrAdd(PC); Released=FMath::Max(Released,Token);
    if (auto* Body=Cast<ALDCharacter>(PC->GetPawn()))
    { if (auto* S=StateFor(Body);S&&S->Action==7&&S->SkillToken<=Token) { CancelSkill(Body,*S); Sync(Body,*S); } }
}
bool ALDCombatDirector::RequestEquipped(ALDCharacter* Body,uint32 Revision,uint8 Slot,uint32 Token,const FVector& Direction,FString& Result,AActor* ExplicitTarget,FVector Point)
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>(); auto* S=StateFor(Body); auto* Build=BuildFor(Body);
    const double Now=GetWorld()->GetTimeSeconds();
    if (!HasAuthority()||!Mode->IsDevelopmentProfile()||!Mode->IsPlaying()||!S||S->Hp<=0||Now<S->ProtectionUntil||!Build||!LDDevelopment::Valid(*Build)
        ||Revision!=Build->Revision||Slot>=LDDevelopment::Slots(*Build)||!Token||Direction.ContainsNaN()||FMath::Abs(Direction.Z)>.001||Direction.SizeSquared()<.9||Direction.SizeSquared()>1.1)
    { Result=TEXT("Invalid slot, build revision or life."); return false; }
    const ELDSkill Skill=Build->Slots[Slot];
    if(auto* Objects=ALDWorldObjects::Get(GetWorld());Objects&&!Objects->ForceDrop(Cast<ALDB91Controller>(Body->GetController()))){Result=TEXT("No safe cargo drop; action unavailable");return false;}
    if(Mode->IsTacticalProfile()&&uint8(Skill)>=10){return RequestTactical(Body,Skill,Token,ExplicitTarget,Result,Point,Direction);}
    if(Mode->IsV08Profile()&&!Allows(Body,LDTactical::EAction::Skill,&Result)){return false;}
    if(!Allows(Body,Skill==ELDSkill::Step?LDTactical::EAction::Dash:LDTactical::EAction::Skill,&Result)){return false;}
    if (Skill==ELDSkill::None||!LDDevelopment::Equipped(*Build,Skill)||LDDevelopment::Rank(*Build,Skill)<1)
    { Result=TEXT("Equip a learned skill in this slot."); return false; }
    auto* PC=Cast<ALDPlayerController>(Body->GetController());
    const bool Held=LDDevelopment::Held(Skill);
    if (Held&&PC&&Token<=ReleasedSkills.FindRef(PC)) { Result=TEXT("Released skill request."); return false; }
    if (Now<Build->Cooldowns[uint8(Skill)]) { Result=TEXT("Skill cooling down."); return false; }
    auto* Move=CastChecked<ULDCharacterMovementComponent>(Body->GetCharacterMovement());
    if (S->Action==3&&Now>=S->RecoveryUntil) { CancelSkill(Body,*S); }
    if (S->Action==4&&Now>=S->DashUntil) { CancelSkill(Body,*S); Move->bCanWalkOffLedges=true; }
    if (S->Action&&Skill!=ELDSkill::Step) { Result=TEXT("Finish the current action first."); return false; }
    if (Held&&(Body->GetVelocity().SizeSquared2D()>1||Move->GetCurrentAcceleration().SizeSquared2D()>1||!Body->GetLastMovementInputVector().IsNearlyZero()))
    { Result=TEXT("Stand still and hold the slot key."); return false; }
    if (Skill==ELDSkill::Patch&&S->Hp>=LDDevelopment::MaxHP(*Build)) { Result=TEXT("Already at full HP."); return false; }
    AActor* Target=nullptr; const FVector Aim=Direction.GetSafeNormal2D();
    if (Skill==ELDSkill::Ambush)
    {
        if (!LDCombat::Quiet(Now,S->QuietSince,S->ExposedUntil,Move->bWantsSprint,S->Action==4)) { Result=TEXT("Ambush needs quiet, unexposed preparation."); return false; }
        Target=ExplicitTarget?ExplicitTarget:Nearest(Body,112,Aim,30,true);
        const auto* Victim=Cast<ALDCharacter>(Target);const auto* State=Victim?StateFor(Victim):nullptr;
        if(ExplicitTarget&&(!State||!LegalTarget(Body,Target,112,&Aim,30)||!LDCombat::IsRear(Body->GetActorLocation(),Victim->GetActorLocation(),Victim->GetActorForwardVector())||!LDCombat::TargetUnalert(Now,State->LastDamage,State->LastDealt))){Target=nullptr;}
        if (!Target) { Result=TEXT("No legal rear Ambush target."); return false; }
    }
    else if (Skill==ELDSkill::Spark)
    {
        Target=ExplicitTarget?ExplicitTarget:Nearest(Body,500,Aim,30,false,true);
        if(ExplicitTarget&&(!Cast<ALDCharacter>(Target)||!LegalTarget(Body,Target,500,&Aim,30))){Target=nullptr;}
        if (!Target) { Result=TEXT("No visible living Spark target."); return false; }
    }
    else if (Held&&Skill!=ELDSkill::Patch)
    {
        if(Mode->WorldChecks){Mode->WorldChecks->EnsureTargets();}
        double Distance=DBL_MAX; FGuid BestId;
        for (auto& Pair:SkillTasks)
        {
            AActor* Candidate=Pair.Key.Get(); auto& Task=Pair.Value;
            if(ExplicitTarget&&Candidate!=ExplicitTarget){continue;}
            if (!Task.Skills.Contains(Skill)||!SkillTargetLegal(Body,Candidate,Skill)) { continue; }
            const double D=FVector::DistSquared2D(Body->GetActorLocation(),Candidate->GetActorLocation());
            if (D<Distance||(D==Distance&&Task.Identity.ToString()<BestId.ToString())) { Target=Candidate;Distance=D;BestId=Task.Identity; }
        }
        if (!Target) { Result=TEXT("No visible eligible task in reach."); return false; }
        if (!SkillTasks.FindChecked(Target).Execute(Body,Skill,*Build,Token,ELDSkillTaskPhase::Validate,Result)) { return false; }
    }
    const double Value=LDDevelopment::SkillValue(*Build,Skill);
    if (Held&&Skill!=ELDSkill::Patch&&!SkillTasks.FindChecked(Target).Execute(Body,Skill,*Build,Token,ELDSkillTaskPhase::Accept,Result)) { return false; }
    if (Skill==ELDSkill::Step)
    {
        if (S->Action==4||!Move->StartAuthorityDash(Direction,Value)) { Result=TEXT("Step requires legal grounded movement."); return false; }
        S->Action=4; S->DashUntil=Now+.2; S->DashReady=Now+7; Move->bCanWalkOffLedges=false;
    }
    else if (Skill==ELDSkill::Ambush) { S->Action=2; S->ImpactAt=Now+.5; S->AssassinReady=Now+18; }
    else if (Skill==ELDSkill::Spark) { S->Action=6; S->ImpactAt=Now+.4; }
    else { S->Action=7; S->ImpactAt=Now+LDDevelopment::ChannelSeconds(Skill); }
    S->bAttackOrder=false;S->Skill=Skill; S->Snapshot=*Build; S->CommittedDamage=Value; S->Target=Target; S->CommittedAim=Aim; S->Order=++NextOrder;
    if(Mode->IsUIControlProfile()&&!Held){Body->SetFacing(Aim.Rotation().Yaw);}
    S->SkillToken=Token; S->HoldSequence=0; S->LastSkillHold=Now; S->bHoldMoving=false; S->SkillAnchor=Body->GetActorLocation();
    Build->Cooldowns[uint8(Skill)]=Now+LDDevelopment::Cooldown(Skill);
    if (Target&&(Skill==ELDSkill::Spark||Skill==ELDSkill::Ambush))
    {
        auto* Victim=CastChecked<ALDCharacter>(Target); auto* T=StateFor(Victim); T->WarningUntil=FMath::Max(T->WarningUntil,S->ImpactAt); Sync(Victim,*T);
        if (auto* VictimPC=Cast<ALDPlayerController>(Victim->GetController())) { VictimPC->ClientLifeReceipt(VictimPC->OwnerState); }
    }
    if (!Held) { S->QuietSince=Now; S->ExposedUntil=FMath::Max(S->ExposedUntil,Now+4); }
    Move->bCombatWalkOnly=S->Action==2||S->Action==6;
    if (PC) { if (Mode->Objectives) { Mode->Objectives->Interrupt(PC,Now,true); }if(Mode->Development){Mode->Development->Cancel(PC,TEXT("Interrupted by skill."));} Mode->SyncOwner(PC); }
    Sync(Body,*S); Result=FString::Printf(TEXT("%s accepted%s."),LDDevelopment::SkillName(Skill),Held?TEXT("; keep holding the slot key"):TEXT("")); return true;
}
void ALDCombatDirector::CompleteSkillChannels(double Now)
{
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    for (auto& Pair:Bodies)
    {
        auto* Body=Pair.Key.Get(); auto& S=Pair.Value; if (!Body||S.Action!=7) { continue; }
        auto* Move=Body->GetCharacterMovement(); auto* Build=BuildFor(Body);
        const bool Valid=Mode->IsPlaying()&&Allows(Body,LDTactical::EAction::Skill)&&Build&&S.Hp>0&&Now>=S.ProtectionUntil&&S.LastDamage<Now-1.e-6
            &&Now-S.LastSkillHold<=.25&&!S.bHoldMoving&&Body->GetVelocity().SizeSquared2D()<=1&&Move->GetCurrentAcceleration().SizeSquared2D()<=1
            &&FVector::DistSquared2D(Body->GetActorLocation(),S.SkillAnchor)<=4
            &&(S.Skill==ELDSkill::Patch||SkillTargetLegal(Body,S.Target.Get(),S.Skill));
        if (!Valid) { CancelSkill(Body,S); continue; }
        if (Now+1.e-6<S.ImpactAt) { continue; }
        FString Reason;
        if (S.Skill==ELDSkill::Patch)
        { S.Hp=FMath::Min(LDDevelopment::MaxHP(*Build),S.Hp+S.CommittedDamage); Reason=TEXT("Patch completed."); }
        else if (auto* Task=SkillTasks.Find(S.Target);Task&&Task->Skills.Contains(S.Skill))
        {
            if (Task->Execute(Body,S.Skill,S.Snapshot,S.SkillToken,ELDSkillTaskPhase::Validate,Reason)) { Task->Execute(Body,S.Skill,S.Snapshot,S.SkillToken,ELDSkillTaskPhase::Commit,Reason); }
        }
        auto* PC=Cast<ALDPlayerController>(Body->GetController()); const uint32 Token=S.SkillToken;
        CancelSkill(Body,S); Sync(Body,S);
        if (PC) { PC->CompleteCombatRequest(Token,Reason.IsEmpty()?TEXT("Task no longer available."):Reason); }
    }
    for (auto It=SkillTasks.CreateIterator();It;++It) { if (!It.Key().IsValid()) { It.RemoveCurrent(); } }
    for (auto It=ReleasedSkills.CreateIterator();It;++It) { if (!It.Key().IsValid()) { It.RemoveCurrent(); } }
}
