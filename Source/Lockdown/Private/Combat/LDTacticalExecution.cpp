#include "Combat/LDCombatDirector.h"
#include "World/LDEnvironmentDirector.h"
#include "World/LDWorldDirector.h"
#include "Combat/LDCombatRules.h"
#include "Region/LDRegionDirector.h"
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
#include "Vision/LDVisionDirector.h"
#include "Inventory/LDEquipmentCatalog.h"
#include "EngineUtils.h"
#include "Engine/World.h"

bool ALDCombatDirector::Allows(ALDCharacter* Body,LDTactical::EAction Action,FString* Why) const
{
    const auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* S=StateFor(Body);
    if(auto* PC=Body?Cast<ALDB91Controller>(Body->GetController()):nullptr)
    {if(auto* Objects=ALDWorldObjects::Get(GetWorld());Objects&&Objects->Traversing(PC)){if(Why){*Why=TEXT("Traversal controls the body until a safe landing");}return false;}}
    if(Action!=LDTactical::EAction::Move){if(auto* PC=Body?Cast<ALDB91Controller>(Body->GetController()):nullptr;PC&&PC->HasObjectCargo()){if(Why){*Why=TEXT("Put down cargo before this action");}return false;}}
    if(M&&M->IsTacticalProfile()&&S&&Action!=LDTactical::EAction::Move&&Action!=LDTactical::EAction::Service&&GetWorld()->GetTimeSeconds()<S->CoreRecoveryUntil){if(Why){*Why=TEXT("Recovering from cancelled windup");}return false;}
    return !M||!M->IsTacticalProfile()||(S&&LDTactical::Allows(S->Tactical,Action,LDTactical::Milliseconds(GetWorld()->GetTimeSeconds()),Why));
}
void ALDCombatDirector::CancelTactical(ALDCharacter* Body,const FString& Why)
{
    auto* S=StateFor(Body);if(!HasAuthority()||!S||(S->Action!=9&&S->Action!=10)){return;}
    const bool Channel=S->Action==10;
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* PC=Cast<ALDPlayerController>(Body->GetController());
    if(PC&&S->CoreItem.IsValid()&&M->Inventory)
    {
        if(auto* Item=M->Inventory->Owned(PC,S->CoreItem)){Item->bReserved=false;if(auto* Entry=const_cast<FLDInventoryItem*>(M->StateFor(PC)->Inventory.Find(Item->Identity))){Entry->bReserved=false;}M->Inventory->Changed(PC);}
    }
    for(TActorIterator<ALDTacticalField> It(GetWorld());It;++It){if(It->Source.Get()==Body&&It->Kind==ELDTacticalField::Telegraph){DestroyField(*It);}}
    const uint32 Token=S->SkillToken;S->ContentChannel.Reset();S->CoreItem.Invalidate();S->CoreRecoveryUntil=GetWorld()->GetTimeSeconds()+.4;CancelSkill(Body,*S);Sync(Body,*S);
    if(PC){PC->CompleteCombatRequest(Token,Why+(Channel?TEXT("; committed channel start costs retained"):TEXT("; no release cost, 400ms recovery")));}
}
void ALDCombatDirector::ApplyStatus(ALDCharacter* Body,LDTactical::EStatus Status,int64 Until,FGuid Source)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* S=StateFor(Body);if(!HasAuthority()||!M->IsTacticalProfile()||!S||S->Hp<=0){return;}
    const int64 At=LDTactical::Milliseconds(GetWorld()->GetTimeSeconds());
    if((Cast<ALDWorldPawn>(Body)&&Cast<ALDWorldPawn>(Body)->ImmuneTo(Status))||Body->IsA<ALDWildSentinel>()||Until<=At||LDTactical::GuardControl(S->Tactical,Status,At)){return;}
    LDTactical::Apply(S->Tactical,Status,Until,Source);
    const bool Hard=Status==LDTactical::EStatus::Stun||Status==LDTactical::EStatus::Hex;
    if(Hard||Status==LDTactical::EStatus::Silence)
    {
        if((S->Action==9||S->Action==10)&&(!S->CoreItem.IsValid()||Hard)){CancelTactical(Body,TEXT("Cast cancelled by control"));}
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
    if(S->Action==10&&(Hard||((Status==LDTactical::EStatus::Root)&&(S->ContentChannel==TEXT("gate")||S->ContentChannel==TEXT("recall")||S->ContentChannel==TEXT("scramble")))||(Status==LDTactical::EStatus::Mute&&(S->ContentChannel==TEXT("restore_mp")||S->ContentChannel==TEXT("recall"))))){CancelTactical(Body,TEXT("Channel controlled"));}
    Sync(Body,*S);
}
bool ALDCombatDirector::RequestTactical(ALDCharacter* Body,ELDSkill Skill,uint32 Token,AActor* Target,FString& Why,FVector Point,FVector Direction)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* S=StateFor(Body);auto* B=BuildFor(Body);
    if((IsContentSkill(Skill)||(Cast<ALDTacticalField>(Target)&&Cast<ALDTacticalField>(Target)->Kind==ELDTacticalField::Decoy))&&B){FLDTacticalRequest R;R.Token=Token;R.BuildRevision=B->Revision;R.Slot=uint8(B->Slots.Find(Skill));R.Direction=Direction;R.Point=Point;return RequestContent(Body,R,Target,nullptr,Why);}const auto* D=M->TacticalCatalog?M->TacticalCatalog->Find(Skill):nullptr;
    const double Now=GetWorld()->GetTimeSeconds();
    const int32 Range=D?D->Range+(D->Range?B?B->Gear.CastRange:0:0):0;
    auto Reject=[&](const TCHAR* Text){Why=Text;return false;};
    if(!D||!B||B->RulesVersion<7||B->CatalogDigest!=M->TacticalCatalog->Checksum||!S||S->Hp<=0||!M->IsPlaying()||Now>M->GetGameState<ALDGameState>()->MatchDeadline||Now<S->ProtectionUntil||S->Action||Now<S->CoreRecoveryUntil||!Token||Token<=S->LastCoreToken||Token<=S->CancelledCoreThrough){return Reject(TEXT("Action busy, stale or unavailable in B71"));}
    if(!Allows(Body,LDTactical::EAction::Skill,&Why)){return false;}
    if(!ULDTacticalCatalog::NaturalGate(*D,*B)||!LDDevelopment::Equipped(*B,Skill)||Now<B->Cooldowns[uint8(Skill)]||S->Tactical.MP<D->Mana){return Reject(TEXT("Skill not equipped, cooling down or insufficient MP"));}
    const bool Spatial=D->Target==ELDTacticalTarget::Ground||D->Target==ELDTacticalTarget::Cone;
    const bool Self=D->Target==ELDTacticalTarget::Self||(D->Target==ELDTacticalTarget::SelfOrUnit&&!Target);
    if(Spatial)
    {FVector Surface;if(Point.ContainsNaN()||Point.GetAbsMax()>20000||!LDVision::InWorld(Point,M->Vision->Grid())||FVector::DistSquared2D(Point,Body->GetActorLocation())>FMath::Square(Range)||!LDGround::MapSurface(GetWorld(),Point,Surface)){return Reject(TEXT("Invalid ground placement"));}Point=Surface;Target=Body;}
    else if(Self){Target=Body;}else if(!Cast<ALDCharacter>(Target)||!LegalTarget(Body,Target,Range,nullptr,0)){return Reject(TEXT("Choose a visible eligible body in range"));}
    const auto* TargetPC=Target?Cast<ALDPlayerController>(Cast<ALDCharacter>(Target)->GetController()):nullptr;
    const auto* Consent=TargetPC?M->StateFor(const_cast<ALDPlayerController*>(TargetPC)):nullptr;
    if(Skill==ELDSkill::RemedyBind&&Target!=Body&&(!Consent||!Consent->bAidConsent)){return Reject(TEXT("Aid consent required"));}
    const auto* CasterPC=Cast<ALDPlayerController>(Body->GetController());const auto* OwnerState=CasterPC?M->StateFor(const_cast<ALDPlayerController*>(CasterPC)):nullptr;
    if(Skill==ELDSkill::RemedyBind&&Target==Body&&LDTactical::Milliseconds(Now)<FMath::Max(S->Tactical.MajorReadyAt,OwnerState?OwnerState->MajorProtectionReadyAt:int64(0))){return Reject(TEXT("Major protection family ready after eight seconds"));}
    auto* PC=Cast<ALDPlayerController>(Body->GetController());if(PC){PC->Navigation->Stop();if(M->Trade){M->Trade->CancelFor(PC);}M->Objectives->Interrupt(PC,Now,true);}
    Body->GetCharacterMovement()->StopMovementImmediately();
    S->bAttackOrder=false;S->Action=9;S->Skill=Skill;S->SkillToken=Token;S->LastCoreToken=Token;S->CoreEndsAt=LDTactical::Milliseconds(Now)+D->Windup;
    S->CorePoint=Point;S->CoreDirection=Direction;S->CoreBuildRevision=B->Revision;S->CoreItem.Invalidate();S->Target=Target;S->CoreTargetLife=StateFor(Cast<ALDCharacter>(Target))->Identity;S->SkillAnchor=Body->GetActorLocation();S->ImpactAt=S->CoreEndsAt/1000.;S->Snapshot=*B;
    S->Order=++NextOrder;Sync(Body,*S);Why=TEXT("Cast accepted: stationary windup; MP and cooldown commit at release");return true;
}
bool ALDCombatDirector::RequestTacticalItem(ALDPlayerController* PC,FGuid Id,uint32 Token,FString& Why)
{auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=M->StateFor(PC);const auto* I=O?O->Inventory.Find(Id):nullptr;if(!I){Why=TEXT("Owned item unavailable");return false;}FLDTacticalRequest R;R.ActionKind=1;R.Match=O->Match;R.Life=O->Life;R.Token=Token;R.BuildRevision=O->Build.Revision;R.CatalogDigest=O->Build.CatalogDigest;R.InventoryRevision=O->Inventory.Revision;R.ItemRevision=I->Revision;R.Item=Id;return RequestContent(Cast<ALDCharacter>(PC->GetPawn()),R,nullptr,nullptr,Why);}
void ALDCombatDirector::PrepareTactical(double Now,TArray<FLDCombatHit>& Hits)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!M->IsTacticalProfile()){return;}const int64 At=LDTactical::Milliseconds(Now);
    TArray<ALDCharacter*> Pending;
    for(auto& P:Bodies)
    {
        auto* Body=P.Key.Get();auto& S=P.Value;if(!Body||S.Hp<=0){continue;}
        S.Tactical.Statuses.RemoveAll([&](const auto& V){return V.Until<=At;});if(S.Tactical.BarrierUntil<=At){S.Tactical.Barrier=0;}
        if(const auto* B=BuildFor(Body);B&&B->RulesVersion>=7){// World bodies retain authored HP and leash-only recovery even with a spell kit.
            double IgnoredHP=S.Hp;
            LDTactical::Regenerate(Cast<ALDWorldPawn>(Body)?IgnoredHP:S.Hp,S.Tactical,LDDevelopment::MaxHP(*B),LDTactical::MaxMP(*B),At,200+B->Gear.MPRegen);}
        if(S.Action==9&&!(Cast<ALDTacticalField>(S.Target.Get())&&Cast<ALDTacticalField>(S.Target.Get())->Kind==ELDTacticalField::Decoy)&&!IsContentSkill(S.Skill)&&!(S.CoreItem.IsValid()&&S.ContentRequest.ActionKind==1)){Pending.Add(Body);}
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
            const int32 Range=D?D->Range+(D->Range?B->Gear.CastRange:0):0;
            const bool Spatial=D&&(D->Target==ELDTacticalTarget::Ground||D->Target==ELDTacticalTarget::Cone);
            FVector Surface;
            const bool SpatialValid=!Spatial||(LDVision::InWorld(S->CorePoint,M->Vision->Grid())&&FVector::DistSquared2D(S->CorePoint,Body->GetActorLocation())<=FMath::Square(Range)&&LDGround::MapSurface(GetWorld(),S->CorePoint,Surface));
            const auto* VPC=Victim?Cast<ALDPlayerController>(Victim->GetController()):nullptr;const auto* Consent=VPC?M->StateFor(const_cast<ALDPlayerController*>(VPC)):nullptr;
            const bool Aid=Victim==Body||(Consent&&Consent->bAidConsent);
            const int64 MajorReady=FMath::Max(S->Tactical.MajorReadyAt,PC?M->StateFor(PC)->MajorProtectionReadyAt:int64(0));
            if(!D||!V||!SpatialValid||V->Identity!=S->CoreTargetLife||V->Hp<=0||(Victim!=Body&&!LegalTarget(Body,Victim,Range,nullptr,0))||S->Tactical.MP<D->Mana||B->Cooldowns[uint8(S->Skill)]>Now||!LDDevelopment::Equipped(*B,S->Skill)||!ULDTacticalCatalog::NaturalGate(*D,*B)||(S->Skill==ELDSkill::RemedyBind&&(!Aid||(Victim==Body&&At<MajorReady))))
            {CancelTactical(Body,TEXT("Invalid release: range, sight, life, build or resources"));continue;}
            ALDArcProjectile* Projectile=nullptr;
            if(S->Skill==ELDSkill::ArcBolt)
            {Projectile=GetWorld()->SpawnActor<ALDArcProjectile>(Body->GetActorLocation(),FRotator::ZeroRotator);if(!Projectile){CancelTactical(Body,TEXT("Projectile allocation failed"));continue;}}
            const int32 Rank=FMath::Clamp(LDDevelopment::Rank(*B,S->Skill),1,3)-1;
            const int32 Magnitude=LDGear::Magnitude(*B,S->Skill,D->RankKey,D->Magnitudes);
            ALDTacticalField* Field=nullptr;
            if(D->Target==ELDTacticalTarget::Ground)
            {
                const auto Kind=S->Skill==ELDSkill::FarwatchWard?ELDTacticalField::Ward:S->Skill==ELDSkill::WireSnare?ELDTacticalField::Trap:S->Skill==ELDSkill::SmokeVeil?ELDTacticalField::Smoke:S->Skill==ELDSkill::SeersFlare?ELDTacticalField::Detector:ELDTacticalField::Grounding;
                Field=CreateField(Body,Kind,Surface+FVector(0,0,15),At,5000,1,D->Area);if(!Field){CancelTactical(Body,TEXT("Field allocation/capacity unavailable"));continue;}
                Field->Source=Body;Field->PlayerSource=PC;Field->bPlayerSource=PC!=nullptr;Field->SourceLife=S->Identity;Field->Kind=Kind;Field->ArmedAt=At+(Kind==ELDTacticalField::Trap?1000:0);
                Field->ExpiresAt=At+(Kind==ELDTacticalField::Ward?Magnitude:Kind==ELDTacticalField::Trap?45000:Kind==ELDTacticalField::Grounding?6000:5000);
                Field->Radius=(D->RankKey==TEXT("radius_cm"))?Magnitude:D->Area;
                Field->Hp=Kind==ELDTacticalField::Ward?40:Kind==ELDTacticalField::Trap?20:Kind==ELDTacticalField::Grounding?Magnitude:1;
            }
            S->Tactical.MP-=D->Mana;B->Cooldowns[uint8(S->Skill)]=Now+(D->RankKey==TEXT("cooldown_ms")?Magnitude:D->Cooldown)/1000.;
            if(S->Skill!=ELDSkill::VeilStep){BreakConcealment(Body);}
            if(Projectile){Projectile->Source=Body;Projectile->CasterIdentity=S->Identity;Projectile->Victim=Victim;Projectile->VictimLife=V->Identity;Projectile->Endpoint=Victim->GetActorLocation();Projectile->AdvancedAt=At;Projectile->Amount=Magnitude*FMath::Min(1.25,1+.025*FMath::Max(0,LDDevelopment::Effective(*B)[3]-8));Projectile->Amount*=1+B->Gear.SpellPercent/100.;Projectile->Token=Token;}
            else if(S->Skill==ELDSkill::AnchorBash){Hits.Add({Body,Victim,45,LDTactical::EDamage::Physical,true,D->Magnitudes[Rank],Token});}
            else if(S->Skill==ELDSkill::PurgeTouch)
            {
                if(Aid||!LDTactical::Block(V->Tactical,At)){LDTactical::Dispel(V->Tactical,false,!Aid);}
            }
            else if(S->Skill==ELDSkill::SuretyShell){LDTactical::Barrier(S->Tactical,Magnitude*FMath::Min(1.25,1+.025*FMath::Max(0,LDDevelopment::Effective(*B)[3]-8)),LDDevelopment::MaxHP(*B),At+3000,At);}
            else if(S->Skill==ELDSkill::VeilStep){S->Tactical.ConcealedUntil=At+Magnitude;S->ExposedUntil=0;}
            else if(S->Skill==ELDSkill::Counterpulse){FLDCombatHit H{Body,Victim,0,LDTactical::EDamage::Magical,true,0,Token};H.Interrupt=true;H.SilenceMS=1300;Hits.Add(H);}
            else if(S->Skill==ELDSkill::BreakSigil){FLDCombatHit H{Body,Victim,0,LDTactical::EDamage::Magical,true,0,Token};H.BreakMS=4000;Hits.Add(H);}
            else if(S->Skill==ELDSkill::RemedyBind){if(Victim==Body){S->Tactical.MajorReadyAt=MajorReady;LDTactical::ActivateGuard(S->Tactical,At);}else{LDTactical::Dispel(V->Tactical,true);}}
            else if(S->Skill==ELDSkill::ForceWave)
            {
                for(auto& P:Bodies){auto* T=P.Key.Get();if(!T||T==Body||P.Value.Hp<=0||Now<P.Value.ProtectionUntil){continue;}
                    if(LDCombat::InCone(Body->GetActorLocation(),T->GetActorLocation(),S->CoreDirection,500,30)&&LDGround::ClearSegment(GetWorld(),Body->GetActorLocation(),T->GetActorLocation())){Push(T,(T->GetActorLocation()-Body->GetActorLocation()).GetSafeNormal2D(),250,Now);}}
            }
            UE_LOG(LogTemp,Display,TEXT("LD_B71_RELEASE owner=%s body=%s token=%u ability=%s mp=%.2f cooldown=%.3f"),*M->ContenderId(PC).ToString(),*S->Identity.ToString(),Token,*D->Id,S->Tactical.MP,B->Cooldowns[uint8(S->Skill)]);
        }
        S->QuietSince=Now;if(!Concealed(Body)){S->ExposedUntil=FMath::Max(S->ExposedUntil,Now+4);}
        CancelSkill(Body,*S);Sync(Body,*S);if(PC){PC->CompleteCombatRequest(Token,TEXT("Released once; blocked or missed effects do not refund costs"));}
    }
    for(TActorIterator<ALDArcProjectile> It(GetWorld());It;++It)
    {
        auto* P=*It;auto* Target=P->ActorVictim.IsValid()?P->ActorVictim.Get():static_cast<AActor*>(P->Victim.Get());auto* V=StateFor(Cast<ALDCharacter>(Target));const auto* F=Cast<ALDTacticalField>(Target);
        if((!V&&!F)||(V&&(V->Identity!=P->VictimLife||V->Hp<=0))||(F&&(F->Identity!=P->VictimLife||!F->Active(At)))){P->Destroy();continue;}
        const FVector From=P->GetActorLocation(),Delta=P->Endpoint-From;const double Distance=Delta.Size();const double Travel=FMath::Max<int64>(0,At-P->AdvancedAt)*1.2;P->AdvancedAt=At;
        const FVector To=Distance<=Travel?P->Endpoint:From+Delta.GetSafeNormal()*Travel;
        if(ProjectileIntercept(P,From,To,At)){continue;}
        if(!LDGround::ClearSegment(GetWorld(),From,To)){P->Destroy();continue;}
        if(FMath::PointDistToSegment(Target->GetActorLocation(),From,To)<=40)
        {FLDCombatHit H{P->Source.Get(),Target,P->Amount,P->Basic?LDTactical::EDamage::Physical:LDTactical::EDamage::Magical,!P->Basic,0,P->Token,P->CasterIdentity};H.Basic=P->Basic;H.DirectFire=P->Fire;H.TargetLife=P->VictimLife;H.SurfaceLease=P->EnvironmentLease;P->EnvironmentLease.Invalidate();if(P->Fire){if(const auto* Lease=EnvironmentLeases.Find(H.SurfaceLease)){H.CreditOwner=Lease->Command.State.Owner;}if(P->Reflected&&P->Source.IsValid()){auto* CasterPC=Cast<ALDPlayerController>(P->Source->GetController());H.CreditOwner=M->ContenderId(CasterPC);}}Hits.Add(H);P->Destroy();}
        else if(Distance<=Travel){P->Destroy();}else{P->SetActorLocation(To);}
    }
}
