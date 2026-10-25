#include "Combat/LDCombatDirector.h"
#include "Character/LDCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Inventory/LDInventoryDirector.h"
#include "Inventory/LDEquipmentCatalog.h"
#include "Region/LDRegionDefinition.h"
#include "Region/LDRegionDirector.h"
#include "Vision/LDVisionDirector.h"
#include "Core/LDGroundQueries.h"
#include "Navigation/LDNavigationComponent.h"
#include "Navigation/NavLinkProxy.h"
#include "NavAreas/NavArea_Null.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
void ALDCombatDirector::SampleContent(int64 At,TArray<FLDCombatHit>& Hits)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!M->IsTacticalProfile()||!M->IsPlaying()){return;}
    if(!bScrambleLinks)
    {
        bScrambleLinks=true;const auto* Region=LDRegion::Definition(GetWorld());if(Region){for(const auto& G:Region->Geometry){if(!G.High||G.Name!=TEXT("Plateau")){continue;}const double Edge=G.Position.X+G.Size.X/2;FVector A,B;if(!LDGround::MapSurface(GetWorld(),FVector(Edge+80,G.Position.Y,0),A)||!LDGround::MapSurface(GetWorld(),FVector(Edge-80,G.Position.Y,0),B)){continue;}
            auto* Link=GetWorld()->SpawnActor<ANavLinkProxy>((A+B)*.5,FRotator::ZeroRotator);if(Link){Link->SetReplicates(false);Link->Tags.Add(TEXT("LD_Cliffwalkers"));Link->PointLinks[0].Left=A-Link->GetActorLocation();Link->PointLinks[0].Right=B-Link->GetActorLocation();Link->PointLinks[0].SetAreaClass(UNavArea_Null::StaticClass());}
            for(int32 N=0;N<2;++N){auto* F=GetWorld()->SpawnActor<ALDTacticalField>((N?B:A)+FVector(0,0,15),FRotator::ZeroRotator);if(F){F->Kind=ELDTacticalField::Scramble;F->Direction=N?A:B;F->Hp=1;F->ExpiresAt=MAX_int64;}}}}
    }
    TArray<ALDCharacter*> Pending;
    for(auto& Pair:Bodies)
    {
        auto* Body=Pair.Key.Get();auto& S=Pair.Value;if(!Body||S.Hp<=0){continue;}const auto* B=BuildFor(Body);if(!B||B->RulesVersion!=7){continue;}
        if(S.DaggerItem.IsValid())
        {auto* PC=Cast<ALDPlayerController>(Body->GetController());const auto* I=PC?M->Inventory->Owned(PC,S.DaggerItem):nullptr;if(S.DaggerUntil<=At||!I||!I->BindingMask){S.DaggerItem.Invalidate();S.DaggerUntil=0;}}
        if(S.bAttackOrder&&S.Action>1){S.bAttackOrder=false;}
        if(S.bAttackOrder){const auto* T=StateFor(Cast<ALDCharacter>(S.AttackTarget.Get()));const auto* F=Cast<ALDTacticalField>(S.AttackTarget.Get());if(!(T?T->Identity==S.AttackLife:F&&F->Identity==S.AttackLife)||!LegalTarget(Body,S.AttackTarget.Get(),B->Gear.Reach,nullptr,0)){S.bAttackOrder=false;if(S.Action==1){S.Action=0;}S.AttackTarget.Reset();}}
        if(S.bAttackOrder&&S.Action==0&&At>=LDTactical::Milliseconds(S.BasicReady))
        {auto* Target=S.AttackTarget.Get();const auto* T=StateFor(Cast<ALDCharacter>(Target));const auto* F=Cast<ALDTacticalField>(Target);const bool Same=T?T->Identity==S.AttackLife:F&&F->Identity==S.AttackLife;FString Why;if(!Same||!LegalTarget(Body,Target,B->Gear.Reach,nullptr,0)){S.bAttackOrder=false;}else if(RequestBody(Body,0,(Target->GetActorLocation()-Body->GetActorLocation()).GetSafeNormal2D(),Why)){S.bAttackOrder=true;S.Target=Target;}}
        if(S.Action==9&&(IsContentSkill(S.Skill)||S.CoreItem.IsValid()&&S.ContentRequest.ActionKind==1||(Cast<ALDTacticalField>(S.Target.Get())&&Cast<ALDTacticalField>(S.Target.Get())->Kind==ELDTacticalField::Decoy))){Pending.Add(Body);}
        if(Body->GetVelocity().SizeSquared2D()>80*80&&At>=S.LastFootstep+500)
        {S.LastFootstep=At;auto* Cue=CreateField(Body,ELDTacticalField::Footstep,Body->GetActorLocation(),At,600,1,420*(1-(LDTactical::PassiveActive(S.Tactical,true,At)?B->Gear.FootstepReduction:0)/100.));if(Cue){Cue->Direction=Body->GetActorForwardVector();}}
    }
    Pending.Sort([&](const auto& A,const auto& B){const auto* X=StateFor(&A);const auto* Y=StateFor(&B);return X->CoreEndsAt!=Y->CoreEndsAt?X->CoreEndsAt<Y->CoreEndsAt:X->Order<Y->Order;});
    for(auto* Body:Pending)
    {
        auto* S=StateFor(Body);auto* B=BuildFor(Body);FString Why;auto* PC=Cast<ALDPlayerController>(Body->GetController());const bool Item=S->CoreItem.IsValid();const uint32 Token=S->SkillToken;
        if(!B||B->Revision!=S->CoreBuildRevision||!Allows(Body,Item?LDTactical::EAction::Item:LDTactical::EAction::Skill,&Why)||FVector::DistSquared2D(Body->GetActorLocation(),S->SkillAnchor)>4||Body->GetVelocity().SizeSquared2D()>1){CancelTactical(Body,TEXT("Windup interrupted"));continue;}
        if(S->CoreEndsAt>At){continue;}if(!ReleaseContent(Body,At,Hits,Why)){CancelTactical(Body,Why);continue;}
        CancelSkill(Body,*S);Sync(Body,*S);if(PC){PC->CompleteCombatRequest(Token,Why);}
    }
    for(int32 N=Marks.Num()-1;N>=0;--N)
    {
        auto& Mark=Marks[N];const auto* Source=StateFor(Mark.Source.Get());const auto* Target=StateFor(Mark.Target.Get());
        if(!Source||!Target||Source->Identity!=Mark.SourceLife||Target->Identity!=Mark.TargetLife||Source->Hp<=0||Target->Hp<=0||At>=Mark.Until||!Target->Tactical.Statuses.ContainsByPredicate([&](const auto& S){return S.Kind==LDTactical::EStatus::Mark&&S.Source==Mark.SourceLife&&S.Until>At;})){Marks.RemoveAt(N);continue;}
        if(At<Mark.Next){continue;}Mark.Next=At+2000;auto* PC=Cast<ALDPlayerController>(Mark.Source->GetController());if(PC&&M->Vision->CanObserve(PC,Mark.Target.Get())){auto* Cue=CreateField(Mark.Source.Get(),ELDTacticalField::Trail,Mark.Target->GetActorLocation(),At,4000,1,0);if(Cue){Cue->Audience=Mark.Source;}}
    }
    for(TActorIterator<ALDTacticalField> It(GetWorld());It;++It)
    {
        auto* F=*It;
        if(F->Kind==ELDTacticalField::Decoy&&F->Active(At))
        {double Left=420*FMath::Clamp((At-F->LastUpdate)/1000.,0.,.1);F->LastUpdate=At;while(Left>0&&F->PathIndex<F->TravelPath.Num()){const FVector From=F->GetActorLocation(),Delta=F->TravelPath[F->PathIndex]-From;const double Distance=Delta.Size2D();if(Distance<1){++F->PathIndex;continue;}const double Step=FMath::Min(Left,Distance);const FVector To=From+Delta*(Step/Distance);if(!LDGround::ClearSegment(GetWorld(),From,To)){F->PathIndex=F->TravelPath.Num();break;}F->Direction=Delta.GetSafeNormal2D();F->SetActorLocation(To);Left-=Step;if(Step>=Distance){++F->PathIndex;}}continue;}
        if(F->Kind!=ELDTacticalField::Ember||!F->Active(FMath::Min(At,F->ExpiresAt-1))){continue;}
        while(F->TickIndex<6&&At>=F->NextTick)
        {
            const int64 Cents=FMath::RoundToInt64(F->Amount*100);const double Amount=(F->TickIndex==5?Cents-5*(Cents/6):Cents/6)/100.;++F->TickIndex;F->NextTick+=500;
            for(const auto& Pair:Bodies){auto* T=Pair.Key.Get();if(!T||T==F->Source.Get()||Pair.Value.Hp<=0||At<LDTactical::Milliseconds(Pair.Value.ProtectionUntil)){continue;}const FVector Delta=T->GetActorLocation()-F->GetActorLocation();const double Along=FVector::DotProduct(Delta,F->Direction);const FVector Closest=F->GetActorLocation()+F->Direction*FMath::Clamp(Along,-350.,350.);if(FMath::Abs(Along)>350||FVector::DistSquared2D(Closest,T->GetActorLocation())>80*80||!LDGround::ClearSegment(GetWorld(),Closest,T->GetActorLocation())){continue;}FLDCombatHit H{F->Source.Get(),T,Amount,LDTactical::EDamage::Magical,false};H.Periodic=true;Hits.Add(H);}
        }
    }
}
void ALDCombatDirector::CompleteContentChannels(int64 At)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!M->IsTacticalProfile()){return;}
    for(auto& Pair:Bodies)
    {
        auto* Body=Pair.Key.Get();auto& S=Pair.Value;auto* B=BuildFor(Body);if(!Body||!B||S.Hp<=0){continue;}
        if(S.SalveEnd)
        {if(LDTactical::Milliseconds(S.LastDamage)!=S.SalveDamageAt){S.SalveEnd=0;}else{while(S.SalveTicks<8&&S.SalveNext<=At&&S.SalveNext<=S.SalveEnd){S.Hp=FMath::Min(LDDevelopment::MaxHP(*B),S.Hp+20);++S.SalveTicks;S.SalveNext+=1000;}if(S.SalveTicks>=8){S.SalveEnd=0;}}}
        if(S.Action!=10){continue;}auto* PC=Cast<ALDPlayerController>(Body->GetController());auto* O=PC?M->StateFor(PC):nullptr;const bool Travel=S.ContentChannel==TEXT("gate")||S.ContentChannel==TEXT("scramble")||S.ContentChannel==TEXT("recall");
        const bool Controlled=Allows(Body,Travel?LDTactical::EAction::Dash:LDTactical::EAction::Item);const bool Legal=M->IsPlaying()&&O&&O->bActive&&B->Revision==S.CoreBuildRevision&&Controlled&&!S.HadPositiveChannelDamage&&At-LDTactical::Milliseconds(S.LastSkillHold)<=250&&FVector::DistSquared2D(S.SkillAnchor,Body->GetActorLocation())<=4&&Body->GetVelocity().SizeSquared2D()<=1;
        if(!Legal){CancelTactical(Body,TEXT("Held channel interrupted; start costs remain committed"));continue;}if(At<S.CoreEndsAt){continue;}
        FVector End;bool Good=true;const uint32 Token=S.SkillToken;
        if(S.ContentChannel==TEXT("restore_mp")){S.Tactical.MP=FMath::Min(LDTactical::MaxMP(*B),S.Tactical.MP+80);}
        else
        {
            const auto* F=Cast<ALDTacticalField>(S.Target.Get());FVector Dest=S.ChannelDestination;
            if(S.ContentChannel==TEXT("gate")){Good=F&&F->Active(At)&&F->Partner.IsValid()&&F->Partner->Active(At)&&!HasObjectiveCargo(PC)&&At>=O->GateReadyAt;Dest=Good?F->Partner->GetActorLocation():FVector::ZeroVector;}
            if(S.ContentChannel==TEXT("scramble")){Good=F&&F->Kind==ELDTacticalField::Scramble&&F->Active(At)&&B->Gear.Scramble;Dest=Good?F->Direction:FVector::ZeroVector;}
            if(S.ContentChannel==TEXT("recall")){Good=!HasObjectiveCargo(PC);}
            Good=Good&&MobilityEndpoint(Body,Dest,End);if(Good){Body->SetActorLocation(End,false,nullptr,ETeleportType::TeleportPhysics);Body->GetCharacterMovement()->StopMovementImmediately();PC->Navigation->Stop();if(S.ContentChannel==TEXT("gate")){O->GateReadyAt=At+3000;}Body->ForceNetUpdate();}
        }
        S.ContentChannel.Reset();CancelSkill(Body,S);Sync(Body,S);PC->CompleteCombatRequest(Token,Good?TEXT("Held action completed; arrival remains vulnerable"):TEXT("Traversal unavailable; nothing moved"));
    }
}
void ALDCombatDirector::WeaponContact(const FLDCombatHit& Hit,ALDCharacter* Victim,double Contact,int64 At)
{
    if(!Hit.Basic||Contact<=0||!Hit.Source){return;}auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* S=StateFor(Hit.Source);auto* T=StateFor(Victim);const auto* B=BuildFor(Hit.Source);if(!S||!T||!B){return;}
    const bool Passives=LDTactical::PassiveActive(S->Tactical,true,At);
    if(B->Gear.BarrierDamage&&Passives&&T->Tactical.Barrier>0){T->Tactical.Barrier=FMath::Max(0.,T->Tactical.Barrier-Contact*.5);}
    if(B->Gear.Watchblade&&Passives&&!Victim->IsA<ALDWildSentinel>()){auto& Count=S->WatchHits.FindOrAdd(T->Identity);if(++Count>=3){Count=0;LDTactical::Apply(T->Tactical,LDTactical::EStatus::Mark,At+2000,S->Identity);Marks.Add({Hit.Source,Victim,S->Identity,T->Identity,At+2000,At});}}
    else{S->WatchHits.Reset();}
    if(S->DaggerItem.IsValid()&&S->DaggerUntil>At)
    {S->DaggerItem.Invalidate();S->DaggerUntil=0;if(!LDTactical::Block(T->Tactical,At)){ApplyStatus(Victim,LDTactical::EStatus::Silence,At+1300,FGuid::NewGuid());}}
}

void ALDCombatDirector::EndContentLife(ALDCharacter* Body)
{auto* S=StateFor(Body);if(S){S->bAttackOrder=false;S->SalveEnd=0;S->DaggerItem.Invalidate();S->Tactical.ReflectUntil=0;if(S->Action==9||S->Action==10){CancelTactical(Body,TEXT("Life ended"));}}TArray<ALDTacticalField*> Owned;for(TActorIterator<ALDTacticalField> It(GetWorld());It;++It){if(It->Source.Get()==Body){Owned.Add(*It);}}for(auto* F:Owned){DestroyField(F,TEXT("TacticalOwnerLifeEnded"));}}
