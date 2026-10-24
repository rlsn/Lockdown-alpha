#include "Combat/LDCombatDirector.h"
#include "Combat/LDTacticalCatalog.h"
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Inventory/LDInventoryDirector.h"
#include "Inventory/LDTradeDirector.h"
#include "Development/LDDevelopmentDirector.h"
#include "Economy/LDProductionDirector.h"
#include "Objectives/LDObjectiveDirector.h"
#include "Core/LDGroundQueries.h"
#include "EngineUtils.h"
#include "Engine/World.h"

bool ALDCombatDirector::Allows(ALDCharacter* Body,LDTactical::EAction Action,FString* Why) const
{
    const auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* S=StateFor(Body);
    if(M&&M->IsTacticalProfile()&&S&&Action!=LDTactical::EAction::Move&&Action!=LDTactical::EAction::Service&&GetWorld()->GetTimeSeconds()<S->CoreRecoveryUntil){if(Why){*Why=TEXT("Recovering from cancelled windup");}return false;}
    return !M||!M->IsTacticalProfile()||(S&&LDTactical::Allows(S->Tactical,Action,LDTactical::Milliseconds(GetWorld()->GetTimeSeconds()),Why));
}
void ALDCombatDirector::CancelTactical(ALDCharacter* Body,const FString& Why)
{
    auto* S=StateFor(Body);if(!HasAuthority()||!S||S->Action!=9){return;}
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* PC=Cast<ALDPlayerController>(Body->GetController());
    if(PC&&S->CoreItem.IsValid()&&M->Inventory)
    {
        if(auto* Item=M->Inventory->Owned(PC,S->CoreItem)){Item->bReserved=false;if(auto* Entry=const_cast<FLDInventoryItem*>(M->StateFor(PC)->Inventory.Find(Item->Identity))){Entry->bReserved=false;}M->Inventory->Changed(PC);}
    }
    const uint32 Token=S->SkillToken;S->CoreItem.Invalidate();S->CoreRecoveryUntil=GetWorld()->GetTimeSeconds()+.4;CancelSkill(Body,*S);Sync(Body,*S);
    if(PC){PC->CompleteCombatRequest(Token,Why+TEXT("; no release cost, 400ms recovery"));}
}
void ALDCombatDirector::ApplyStatus(ALDCharacter* Body,LDTactical::EStatus Status,int64 Until,FGuid Source)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* S=StateFor(Body);if(!HasAuthority()||!M->IsTacticalProfile()||!S||S->Hp<=0){return;}
    LDTactical::Apply(S->Tactical,Status,Until,Source);
    const bool Hard=Status==LDTactical::EStatus::Stun||Status==LDTactical::EStatus::Hex;
    if(Hard||Status==LDTactical::EStatus::Silence)
    {
        if(S->Action==9&&(!S->CoreItem.IsValid()||Hard)){CancelTactical(Body,TEXT("Cast cancelled by control"));}
        if(Hard||S->Action==7||S->Action==6){CancelSkill(Body,*S);}
    }
    if(Status==LDTactical::EStatus::Mute&&S->Action==9&&S->CoreItem.IsValid()){CancelTactical(Body,TEXT("Item cancelled by mute"));}
    if(Status==LDTactical::EStatus::Disarm&&S->Action==1){CancelSkill(Body,*S);}
    if(Hard&&S->Action==4){CancelSkill(Body,*S);}
    if(Hard)
    {
        if(auto* PC=Cast<ALDPlayerController>(Body->GetController()))
        {PC->Navigation->Stop();M->Objectives->Interrupt(PC,GetWorld()->GetTimeSeconds(),true);M->Development->Cancel(PC,TEXT("Hard control cancelled preparation"));if(M->Production){M->Production->Cancel(PC,TEXT("Hard control cancelled production"));}}
    }
    if(Hard||Status==LDTactical::EStatus::Root){Body->GetCharacterMovement()->RemoveRootMotionSource(TEXT("LD_AuthorityDash"));Body->GetCharacterMovement()->StopMovementImmediately();}
    Sync(Body,*S);
}
bool ALDCombatDirector::RequestTactical(ALDCharacter* Body,ELDSkill Skill,uint32 Token,AActor* Target,FString& Why)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* S=StateFor(Body);auto* B=BuildFor(Body);const auto* D=M->TacticalCatalog?M->TacticalCatalog->Find(Skill):nullptr;
    const double Now=GetWorld()->GetTimeSeconds();
    auto Reject=[&](const TCHAR* Text){Why=Text;return false;};
    if(!D||!B||B->RulesVersion!=7||B->CatalogDigest!=M->TacticalCatalog->Checksum||!S||S->Hp<=0||!M->IsPlaying()||Now>M->GetGameState<ALDGameState>()->MatchDeadline||Now<S->ProtectionUntil||S->Action||Now<S->CoreRecoveryUntil||!Token||Token<=S->LastCoreToken||Token<=S->CancelledCoreThrough){return Reject(TEXT("Action busy, stale or unavailable in B71"));}
    if(!Allows(Body,LDTactical::EAction::Skill,&Why)){return false;}
    if(!LDDevelopment::Equipped(*B,Skill)||Now<B->Cooldowns[uint8(Skill)]||S->Tactical.MP<D->Mana){return Reject(TEXT("Skill not equipped, cooling down or insufficient MP"));}
    const bool Self=D->Range==0||(!Target&&Skill==ELDSkill::PurgeTouch);
    if(Self){Target=Body;}else if(!Cast<ALDCharacter>(Target)||!LegalTarget(Body,Target,D->Range,nullptr,0)){return Reject(TEXT("Choose a visible eligible body in range"));}
    auto* PC=Cast<ALDPlayerController>(Body->GetController());if(PC){PC->Navigation->Stop();if(M->Trade){M->Trade->CancelFor(PC);}M->Objectives->Interrupt(PC,Now,true);}
    Body->GetCharacterMovement()->StopMovementImmediately();
    S->Action=9;S->Skill=Skill;S->SkillToken=Token;S->LastCoreToken=Token;S->CoreEndsAt=LDTactical::Milliseconds(Now)+D->Windup;
    S->CoreBuildRevision=B->Revision;S->CoreItem.Invalidate();S->Target=Target;S->CoreTargetLife=StateFor(Cast<ALDCharacter>(Target))->Identity;S->SkillAnchor=Body->GetActorLocation();S->ImpactAt=S->CoreEndsAt/1000.;S->Snapshot=*B;
    S->Order=++NextOrder;Sync(Body,*S);Why=TEXT("Cast accepted: stationary windup; MP and cooldown commit at release");return true;
}
bool ALDCombatDirector::RequestTacticalItem(ALDPlayerController* PC,FGuid Id,uint32 Token,FString& Why)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=M->StateFor(PC);auto* Body=PC?Cast<ALDCharacter>(PC->GetPawn()):nullptr;auto* S=StateFor(Body);auto* Item=M->Inventory?M->Inventory->Owned(PC,Id):nullptr;const double Now=GetWorld()->GetTimeSeconds();
    if(!HasAuthority()||!M->IsTacticalProfile()||!M->IsPlaying()||!O||!S||S->Hp<=0||Now<S->ProtectionUntil||S->Action||Now<S->CoreRecoveryUntil||!Item||Item->Kind!=ELDItemKind::NullBell||Item->bReserved||!(O->Inventory.Quick.IndexOfByKey(Id)>=0&&O->Inventory.Quick.IndexOfByKey(Id)<3)||Now<FMath::Max(S->NullBellReadyAt,O->NullBellReadyAt)||!Token||Token<=S->LastCoreToken||Token<=S->CancelledCoreThrough){Why=TEXT("Null Bell needs a current quick reference, ready cooldown and free action");return false;}
    if(!Allows(Body,LDTactical::EAction::Item,&Why)){return false;}
    if(M->Trade){M->Trade->CancelFor(PC);}PC->Navigation->Stop();M->Objectives->Interrupt(PC,Now,true);Body->GetCharacterMovement()->StopMovementImmediately();
    Item->bReserved=true;auto* Entry=const_cast<FLDInventoryItem*>(O->Inventory.Find(Id));Entry->bReserved=true;M->Inventory->Changed(PC);
    S->Action=9;S->Skill=ELDSkill::None;S->SkillToken=Token;S->LastCoreToken=Token;S->CoreItem=Id;S->CoreEndsAt=LDTactical::Milliseconds(Now);S->CoreBuildRevision=O->Build.Revision;S->SkillAnchor=Body->GetActorLocation();S->Order=++NextOrder;Sync(Body,*S);Why=TEXT("Null Bell use accepted");return true;
}
void ALDCombatDirector::PrepareTactical(double Now,TArray<FLDCombatHit>& Hits)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!M->IsTacticalProfile()){return;}const int64 At=LDTactical::Milliseconds(Now);
    TArray<ALDCharacter*> Pending;
    for(auto& P:Bodies)
    {
        auto* Body=P.Key.Get();auto& S=P.Value;if(!Body||S.Hp<=0){continue;}
        S.Tactical.Statuses.RemoveAll([&](const auto& V){return V.Until<=At;});if(S.Tactical.BarrierUntil<=At){S.Tactical.Barrier=0;}
        if(const auto* B=BuildFor(Body);B&&B->RulesVersion==7){LDTactical::Regenerate(S.Hp,S.Tactical,LDDevelopment::MaxHP(*B),LDTactical::MaxMP(*B),At);}
        if(S.Action==9){Pending.Add(Body);}
    }
    Pending.Sort([&](const ALDCharacter& A,const ALDCharacter& B){const auto* X=StateFor(&A);const auto* Y=StateFor(&B);return X->CoreEndsAt!=Y->CoreEndsAt?X->CoreEndsAt<Y->CoreEndsAt:X->Order<Y->Order;});
    for(auto* Body:Pending)
    {
        auto* S=StateFor(Body);auto* B=BuildFor(Body);auto* PC=Cast<ALDPlayerController>(Body->GetController());FString Why;
        const bool ItemAction=S->CoreItem.IsValid();
        const auto* G=GetWorld()->GetGameState<ALDGameState>();
        if(!M->IsPlaying()||!G||Now>G->MatchDeadline){CancelTactical(Body,TEXT("Match deadline reached"));continue;}
        if(!B||B->Revision!=S->CoreBuildRevision||!Allows(Body,ItemAction?LDTactical::EAction::Item:LDTactical::EAction::Skill,&Why)||FVector::DistSquared2D(Body->GetActorLocation(),S->SkillAnchor)>4||Body->GetVelocity().SizeSquared2D()>1)
        {CancelTactical(Body,TEXT("Windup cancelled: control, movement or build changed"));continue;}
        if(S->CoreEndsAt>At){continue;}
        const uint32 Token=S->SkillToken;
        if(ItemAction)
        {
            auto* Item=PC?M->Inventory->Owned(PC,S->CoreItem):nullptr;
            if(!Item||!Item->bReserved||!M->StateFor(PC)->Inventory.Quick.Contains(Item->Identity)){CancelTactical(Body,TEXT("Item changed before release"));continue;}
            const FGuid ItemId=Item->Identity;S->CoreItem.Invalidate();S->NullBellReadyAt=Now+M->TacticalCatalog->NullBellCooldown/1000.;
            LDTactical::Dispel(S->Tactical,false);M->Inventory->Remove(PC,Item,true,TEXT("NullBellConsumed"));
            UE_LOG(LogTemp,Display,TEXT("LD_B71_RELEASE token=%u item=%s null_bell consumed once"),Token,*ItemId.ToString());
        }
        else
        {
            const auto* D=M->TacticalCatalog->Find(S->Skill);auto* Victim=Cast<ALDCharacter>(S->Target.Get());auto* V=StateFor(Victim);
            if(!D||!V||V->Identity!=S->CoreTargetLife||V->Hp<=0||(Victim!=Body&&!LegalTarget(Body,Victim,D->Range,nullptr,0))||S->Tactical.MP<D->Mana||B->Cooldowns[uint8(S->Skill)]>Now||!LDDevelopment::Equipped(*B,S->Skill))
            {CancelTactical(Body,TEXT("Invalid release: range, sight, life, build or resources"));continue;}
            ALDArcProjectile* Projectile=nullptr;
            if(S->Skill==ELDSkill::ArcBolt)
            {Projectile=GetWorld()->SpawnActor<ALDArcProjectile>(Body->GetActorLocation(),FRotator::ZeroRotator);if(!Projectile){CancelTactical(Body,TEXT("Projectile allocation failed"));continue;}}
            const int32 Rank=FMath::Clamp(LDDevelopment::Rank(*B,S->Skill),1,3)-1;
            S->Tactical.MP-=D->Mana;B->Cooldowns[uint8(S->Skill)]=Now+(S->Skill==ELDSkill::PurgeTouch?D->Magnitudes[Rank]:D->Cooldown)/1000.;
            if(Projectile){Projectile->Source=Body;Projectile->CasterIdentity=S->Identity;Projectile->Victim=Victim;Projectile->VictimLife=V->Identity;Projectile->Endpoint=Victim->GetActorLocation();Projectile->AdvancedAt=At;Projectile->Amount=D->Magnitudes[Rank]*FMath::Min(1.25,1+.025*FMath::Max(0,B->Attributes[3]-8));Projectile->Token=Token;}
            else if(S->Skill==ELDSkill::AnchorBash){Hits.Add({Body,Victim,45,LDTactical::EDamage::Physical,true,D->Magnitudes[Rank],Token});}
            else if(S->Skill==ELDSkill::PurgeTouch)
            {
                // No implicit aid: other bodies receive positive dispel. B72 adds explicit aid consent.
                if(Victim==Body||!LDTactical::Block(V->Tactical,At)){LDTactical::Dispel(V->Tactical,false,Victim!=Body);}
            }
            else if(S->Skill==ELDSkill::SuretyShell){LDTactical::Barrier(S->Tactical,D->Magnitudes[Rank]*FMath::Min(1.25,1+.025*FMath::Max(0,B->Attributes[3]-8)),LDDevelopment::MaxHP(*B),At+3000,At);}
            UE_LOG(LogTemp,Display,TEXT("LD_B71_RELEASE owner=%s body=%s token=%u ability=%s mp=%.2f cooldown=%.3f"),*M->ContenderId(PC).ToString(),*S->Identity.ToString(),Token,*D->Id,S->Tactical.MP,B->Cooldowns[uint8(S->Skill)]);
        }
        S->QuietSince=Now;S->ExposedUntil=FMath::Max(S->ExposedUntil,Now+4);
        CancelSkill(Body,*S);Sync(Body,*S);if(PC){PC->CompleteCombatRequest(Token,TEXT("Released once; blocked or missed effects do not refund costs"));}
    }
    for(TActorIterator<ALDArcProjectile> It(GetWorld());It;++It)
    {
        auto* P=*It;auto* V=StateFor(P->Victim.Get());
        if(!V||V->Identity!=P->VictimLife||V->Hp<=0){P->Destroy();continue;}
        const FVector From=P->GetActorLocation(),Delta=P->Endpoint-From;const double Distance=Delta.Size();const double Travel=FMath::Max<int64>(0,At-P->AdvancedAt)*1.2;P->AdvancedAt=At;
        const FVector To=Distance<=Travel?P->Endpoint:From+Delta.GetSafeNormal()*Travel;
        if(!LDGround::ClearSegment(GetWorld(),From,To)){P->Destroy();continue;}
        if(FMath::PointDistToSegment(P->Victim->GetActorLocation(),From,To)<=40)
        {Hits.Add({P->Source.Get(),P->Victim.Get(),P->Amount,LDTactical::EDamage::Magical,true,0,P->Token,P->CasterIdentity});P->Destroy();}
        else if(Distance<=Travel){P->Destroy();}else{P->SetActorLocation(To);}
    }
}
