#include "Combat/LDCombatDirector.h"
#include "Combat/LDTacticalCatalog.h"
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Inventory/LDEquipmentCatalog.h"
#include "Inventory/LDInventoryDirector.h"
#include "Economy/LDEconomyDirector.h"
#include "Objectives/LDObjectiveDirector.h"
#include "Vision/LDVisionDirector.h"
#include "Core/LDGroundQueries.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Engine/World.h"
bool ALDCombatDirector::ReleaseContent(ALDCharacter* Body,int64 At,TArray<FLDCombatHit>& Hits,FString& Why)
{
    auto Reject=[&](){Why=TEXT("Release placement, target or resources unavailable");return false;};auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* S=StateFor(Body);auto* B=BuildFor(Body);auto* PC=Cast<ALDPlayerController>(Body->GetController());auto* O=PC?M->StateFor(PC):nullptr;const auto R=S->ContentRequest;auto* First=S->Target.Get();auto* Second=S->CoreSecondTarget.Get();const auto Skill=S->Skill;const double Now=At/1000.;
    const auto* D=R.ActionKind==0?M->TacticalCatalog->Find(Skill):nullptr;auto* Item=R.ActionKind==1&&PC?M->Inventory->Owned(PC,R.Item):nullptr;const auto* I=Item?ULDEquipmentCatalog::Load()->Find(Item->Kind):nullptr;
    if(!B||B->Revision!=S->CoreBuildRevision||(!D&&!I)){return Reject();}
    auto Identity=[&](AActor* T){if(auto* C=Cast<ALDCharacter>(T)){const auto* V=StateFor(C);return V?V->Identity:FGuid();}const auto* F=Cast<ALDTacticalField>(T);return F?F->Identity:FGuid();};
    if((S->CoreTargetLife.IsValid()&&Identity(First)!=S->CoreTargetLife)||(S->CoreSecondLife.IsValid()&&Identity(Second)!=S->CoreSecondLife)){return Reject();}
    auto Observe=[&](AActor* T,int32 Range){if(!T){return false;}if(T==Body){return true;}if(const auto* F=Cast<ALDTacticalField>(T);F&&F->Source.Get()==Body){return F->Active(At)&&FVector::DistSquared2D(T->GetActorLocation(),Body->GetActorLocation())<=FMath::Square(Range)&&(!PC||M->Vision->CanObserve(PC,T));}return LegalTarget(Body,T,Range,nullptr,0);};
    int32 Mana=D?D->Mana:0;const int32 Value=D?LDGear::Magnitude(*B,Skill,D->RankKey,D->Magnitudes):0;const int32 Rank=D?FMath::Clamp(LDDevelopment::Rank(*B,Skill),1,3)-1:0;const double Scale=FMath::Min(1.25,1+.025*FMath::Max(0,LDDevelopment::Effective(*B)[3]-8));
    TArray<FVector> DecoyPath;ALDArcProjectile* StagedProjectile=nullptr;int32 Duration=0;ALDTacticalField* Field=nullptr;ALDTacticalField* Pair=nullptr;FVector End,Surface;
    if(D)
    {
        if(Skill==ELDSkill::GuardSwap&&!First){Mana=10;}if(S->Tactical.MP<Mana){return Reject();}
        if(!LDDevelopment::Equipped(*B,Skill)||!ULDTacticalCatalog::NaturalGate(*D,*B)||B->Cooldowns[uint8(Skill)]>Now){return Reject();}const int32 Range=D->Range+(D->Range&&Skill!=ELDSkill::GateWeave?B->Gear.CastRange:0);
        if(Skill==ELDSkill::GrandExchange){if(!Observe(First,Range)||!Observe(Second,Range)||!AtomicSwap(First,Second,Body,At,Why,false)){return Reject();}}
        else if(Skill==ELDSkill::GuardSwap)
        {if(First){auto* Marker=Cast<ALDTacticalField>(First);if(!Marker||Marker->Kind!=ELDTacticalField::Marker||!Observe(First,Range)||!Allows(Body,LDTactical::EAction::Dash)||!MobilityEndpoint(Body,Marker->GetActorLocation(),End,Marker)){return Reject();}}
            else{Mana=10;if(FVector::DistSquared2D(R.Point,Body->GetActorLocation())>FMath::Square(Range)||!MobilityEndpoint(Body,R.Point,End)){return Reject();}Field=CreateField(Body,ELDTacticalField::Marker,End-FVector(0,0,Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()-13),At,20000,40,0);if(!Field){return Reject();}Field->SkillCooldown=(D->RankKey==TEXT("cooldown_ms")?D->Magnitudes[Rank]:D->Cooldown)/1000.;}}
        else if(Skill==ELDSkill::GateWeave)
        {int32 Emitters=0;for(TActorIterator<ALDTacticalField> It(GetWorld());It;++It){Emitters+=It->Active(At);}if(Emitters>78){return Reject();}FVector A,BPoint;if(!PC||FVector::DistSquared2D(R.Point,Body->GetActorLocation())>FMath::Square(650+B->Gear.CastRange)||FVector::DistSquared2D(R.Point,R.SecondPoint)>4000.*4000||!Explored(PC,R.Point)||!Explored(PC,R.SecondPoint)||!MobilityEndpoint(Body,R.Point,A)||!MobilityEndpoint(Body,R.SecondPoint,BPoint)||FVector::DistSquared2D(A,BPoint)<120*120){return Reject();}
            Field=CreateField(Body,ELDTacticalField::Gate,A-FVector(0,0,Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()-13),At,18000,Value,0);if(!Field){return Reject();}Pair=CreateField(Body,ELDTacticalField::Gate,BPoint-FVector(0,0,Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()-13),At,18000,Value,0);if(!Pair){DestroyField(Field);return Reject();}Field->Partner=Pair;Pair->Partner=Field;}
        else if(D->Target==ELDTacticalTarget::SelfOrUnit){if(!Observe(First,Range)||Skill==ELDSkill::RemedyBind){return Reject();}}
        else if(D->Target==ELDTacticalTarget::Unit||Skill==ELDSkill::Grapple&&First){if(!Observe(First,Range)||(!Cast<ALDCharacter>(First)&&!(Cast<ALDTacticalField>(First)&&Cast<ALDTacticalField>(First)->Kind==ELDTacticalField::Decoy))){return Reject();}}
        else if(D->Target!=ELDTacticalTarget::Self)
        {if(FVector::DistSquared2D(R.Point,Body->GetActorLocation())>FMath::Square(Range)||!LDGround::MapSurface(GetWorld(),R.Point,Surface)){return Reject();}}
        if(Skill==ELDSkill::Slipstream&&(!Allows(Body,LDTactical::EAction::Dash)||!Body->GetCharacterMovement()->IsMovingOnGround())){return Reject();}
        if(Skill==ELDSkill::Grapple&&!Allows(Body,LDTactical::EAction::Dash)){return Reject();}
        if(Skill==ELDSkill::SovereignWard&&At<FMath::Max(S->Tactical.MajorReadyAt,O?O->MajorProtectionReadyAt:int64(0))){return Reject();}
        if(S->Tactical.MP<Mana){if(Field){DestroyField(Field);}if(Pair){DestroyField(Pair);}return Reject();}
        if(Skill==ELDSkill::FalseTrail||Skill==ELDSkill::EmberLine||Skill==ELDSkill::ArcanePrison||Skill==ELDSkill::SovereignWard)
        {
            if(Skill==ELDSkill::FalseTrail){auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),Body->GetActorLocation(),Surface,Body);if(!Path||!Path->IsValid()||Path->IsPartial()||Path->PathPoints.Num()<2){return Reject();}for(const auto& P:Path->PathPoints){FVector G;if(!LDGround::MapSurface(GetWorld(),P,G)){return Reject();}DecoyPath.Add(G+FVector(0,0,Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2));}}
            const auto Kind=Skill==ELDSkill::FalseTrail?ELDTacticalField::Decoy:Skill==ELDSkill::EmberLine?ELDTacticalField::Ember:Skill==ELDSkill::ArcanePrison?ELDTacticalField::Prison:ELDTacticalField::Dome;
            Duration=Kind==ELDTacticalField::Decoy?Value:Kind==ELDTacticalField::Ember?3000:Kind==ELDTacticalField::Prison?3500:4000;const FVector Point=Kind==ELDTacticalField::Decoy?Body->GetActorLocation():Kind==ELDTacticalField::Ember?Body->GetActorLocation()+R.Direction*350:Surface+FVector(0,0,15);
            Field=CreateField(Body,Kind,Point,At,Duration,Kind==ELDTacticalField::Prison||Kind==ELDTacticalField::Dome?Value:1,Kind==ELDTacticalField::Prison?300:Kind==ELDTacticalField::Dome?350:0);if(!Field){return Reject();}
            Field->TravelPath=DecoyPath;Field->LastUpdate=At;Field->Direction=R.Direction;Field->Amount=Skill==ELDSkill::EmberLine?Value*Scale:0;Field->NextTick=At+500;
            if(Kind==ELDTacticalField::Prison)
            {for(int32 N=0;N<24;++N){const double Angle=2*PI*N/24.;const FVector Offset(FMath::Cos(Angle)*300,FMath::Sin(Angle)*300,85);const FVector P=Field->GetActorLocation()+Offset;const FQuat Q=FRotator(0,FMath::RadiansToDegrees(Angle)+90,0).Quaternion();
                if(GetWorld()->OverlapAnyTestByObjectType(P,Q,FCollisionObjectQueryParams(ECC_Pawn),FCollisionShape::MakeBox(FVector(43,8,100)))){continue;}auto* Wall=NewObject<UBoxComponent>(Field);Wall->SetupAttachment(Field->GetRootComponent());Wall->SetRelativeLocation(Offset);Wall->SetRelativeRotation(Q);Wall->SetBoxExtent(FVector(43,8,100));Wall->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Wall->SetCollisionObjectType(ECC_WorldDynamic);Wall->SetCollisionResponseToAllChannels(ECR_Ignore);Wall->SetCollisionResponseToChannel(ECC_Pawn,ECR_Block);Wall->SetCanEverAffectNavigation(false);Field->AddInstanceComponent(Wall);Wall->RegisterComponent();}}
        }
        if(Skill==ELDSkill::ArcBolt){StagedProjectile=GetWorld()->SpawnActor<ALDArcProjectile>(Body->GetActorLocation(),FRotator::ZeroRotator);if(!StagedProjectile){return Reject();}}
        // All fallible allocations and endpoint checks precede release costs.
        S->Tactical.MP-=Mana;B->Cooldowns[uint8(Skill)]=Skill==ELDSkill::GuardSwap&&!First?B->Cooldowns[uint8(Skill)]:Now+(D->RankKey==TEXT("cooldown_ms")?D->Magnitudes[Rank]:D->Cooldown)/1000.;
        if(Skill==ELDSkill::GuardSwap&&!First){S->CoreRecoveryUntil=Now+1;}
        if(Skill==ELDSkill::GateWeave){TArray<ALDTacticalField*> Old;for(TActorIterator<ALDTacticalField> It(GetWorld());It;++It){if(It->Source.Get()==Body&&It->Kind==ELDTacticalField::Gate&&*It!=Field&&*It!=Pair){Old.Add(*It);}}for(auto* F:Old){DestroyField(F,TEXT("GatePairReplaced"));}}
        if(Skill==ELDSkill::BraceWall){const bool Replace=At>=S->Tactical.BarrierUntil||FMath::Min(Value*Scale,.4*LDDevelopment::MaxHP(*B))>S->Tactical.Barrier;LDTactical::Barrier(S->Tactical,Value*Scale,LDDevelopment::MaxHP(*B),At+3000,At);if(Replace){S->Tactical.Directional=true;S->Tactical.BarrierFacing=Body->GetActorForwardVector();}}
        if(Skill==ELDSkill::ArcBolt){auto* P=StagedProjectile;if(P){P->Source=Body;P->CasterIdentity=S->Identity;P->Victim=Cast<ALDCharacter>(First);P->ActorVictim=First;P->VictimLife=S->CoreTargetLife;P->Endpoint=First->GetActorLocation();P->AdvancedAt=At;P->Amount=Value*Scale*(1+B->Gear.SpellPercent/100.);P->Token=R.Token;}}
        if(Skill==ELDSkill::AnchorBash){Hits.Add({Body,First,45,LDTactical::EDamage::Physical,true,D->Magnitudes[Rank],R.Token});}
        if(Skill==ELDSkill::Hamstring){FLDCombatHit H{Body,First,double(Value),LDTactical::EDamage::Physical,true,0,R.Token};H.SlowMS=2500;Hits.Add(H);}
        if(Skill==ELDSkill::NullSeal){FLDCombatHit H{Body,First,0,LDTactical::EDamage::Magical,true,0,R.Token};H.MuteMS=1800;Hits.Add(H);}
        if(Skill==ELDSkill::LedgerMark){auto* T=Cast<ALDCharacter>(First);auto* V=StateFor(T);if(V&&!LDTactical::Block(V->Tactical,At)){LDTactical::Apply(V->Tactical,LDTactical::EStatus::Mark,At+Value,S->Identity);Marks.Add({Body,T,S->Identity,V->Identity,At+Value,At});}}
        if(Skill==ELDSkill::GuardSwap&&First){Body->SetActorLocation(End,false,nullptr,ETeleportType::TeleportPhysics);DestroyField(Cast<ALDTacticalField>(First));}
        if(Skill==ELDSkill::GrandExchange){AtomicSwap(First,Second,Body,At,Why,true);}
        if(Skill==ELDSkill::TemporalLock){for(const auto& P:Bodies){auto* T=P.Key.Get();if(T&&T!=Body&&P.Value.Hp>0&&At>=LDTactical::Milliseconds(P.Value.ProtectionUntil)&&FVector::DistSquared2D(T->GetActorLocation(),R.Point)<=FMath::Square(Value)&&LDGround::ClearSegment(GetWorld(),R.Point+FVector(0,0,20),T->GetActorLocation())){Hits.Add({Body,T,0,LDTactical::EDamage::Magical,false,1800,R.Token});}}}
        if(Skill==ELDSkill::SovereignWard){S->Tactical.MajorReadyAt=At+8000;if(O){O->MajorProtectionReadyAt=At+8000;}}
        if(Skill==ELDSkill::Slipstream){S->Action=0;S->DashUntil=Now+.3;CastChecked<ULDCharacterMovementComponent>(Body->GetCharacterMovement())->StartContentDash(R.Direction,FMath::Min(450.,FVector::Dist2D(Body->GetActorLocation(),R.Point)),.3);}
        if(Skill==ELDSkill::Grapple){auto* Victim=Cast<ALDCharacter>(First);if(!Victim||!LDTactical::Block(StateFor(Victim)->Tactical,At)){const FVector Anchor=Victim?Victim->GetActorLocation():FVector(R.Point),Original=Body->GetActorLocation();S->Action=0;Push(Body,(Anchor-Original).GetSafeNormal2D(),FMath::Min(500.,FMath::Max(0.,FVector::Dist2D(Anchor,Original)-(Victim?90:0))),Now);if(Victim){Push(Victim,(Original-Anchor).GetSafeNormal2D(),180,Now);}}}
    }
    else
    {
        if(!O||!Item||!Item->bReserved||Item->Revision!=R.ItemRevision||!O->Inventory.Quick.Contains(Item->Identity)||(I->Binding!=255&&!Item->BindingMask)||O->ItemFamilies.FindRef(I->CooldownKey())>At||Item->ReadyAt>At){return Reject();}
        const auto Op=I->Operation;const int32 Range=I->Param(TEXT("range_cm"));
        if(First&&(Op==TEXT("basic_dispel")||Op==TEXT("targeted_mute")||Op==TEXT("grapple"))&&!Observe(First,Range)){return Reject();}
        if(Op==TEXT("blink")&&(!Allows(Body,LDTactical::EAction::Dash)||At<O->DirectDamageUntil||FVector::DistSquared2D(R.Point,Body->GetActorLocation())>FMath::Square(I->Param(TEXT("distance_cm")))||!MobilityEndpoint(Body,R.Point,End))){return Reject();}
        if((Op==TEXT("grapple")||Op==TEXT("terrain_grapple"))&&!Allows(Body,LDTactical::EAction::Dash)){return Reject();}
        if(I->Family==TEXT("major_protection")&&At<FMath::Max(S->Tactical.MajorReadyAt,O->MajorProtectionReadyAt)){return Reject();}
        if(Op==TEXT("smoke")||Op==TEXT("spawn_ward")||Op==TEXT("spawn_trap"))
        {if(FVector::DistSquared2D(R.Point,Body->GetActorLocation())>FMath::Square(Range)||!LDGround::MapSurface(GetWorld(),R.Point,Surface)){return Reject();}const auto Kind=Op==TEXT("smoke")?ELDTacticalField::Smoke:Op==TEXT("spawn_ward")?ELDTacticalField::Ward:ELDTacticalField::Trap;
            Field=CreateField(Body,Kind,Surface+FVector(0,0,15),At,I->Param(TEXT("duration_ms")),I->Param(TEXT("hp"),1),Kind==ELDTacticalField::Ward?I->Param(TEXT("vision_cm")):I->Param(TEXT("radius_cm")),true);if(!Field){return Reject();}if(Kind==ELDTacticalField::Trap){Field->ArmedAt=At+1000;Field->Amount=I->Param(TEXT("root_ms"));}}
        if(Op==TEXT("detector")){Field=CreateField(Body,ELDTacticalField::Detector,Body->GetActorLocation(),At,I->Param(TEXT("duration_ms")),1,I->Param(TEXT("radius_cm")),true);if(!Field){return Reject();}}
        const int64 Cooldown=At+I->Cooldown;const FGuid Id=Item->Identity;O->ItemFamilies.FindOrAdd(I->CooldownKey())=Cooldown;Item->ReadyAt=Cooldown;Item->bReserved=false;auto* Entry=const_cast<FLDInventoryItem*>(O->Inventory.Find(Id));Entry->ReadyAt=Cooldown;Entry->bReserved=false;++Item->Revision;Entry->Revision=Item->Revision;++O->Inventory.Revision;S->CoreItem.Invalidate();
        if(I->Family==TEXT("major_protection")){S->Tactical.MajorReadyAt=At+8000;O->MajorProtectionReadyAt=At+8000;}
        if(I->Family==TEXT("cleanse")){S->NullBellReadyAt=Cooldown/1000.;O->NullBellReadyAt=S->NullBellReadyAt;}
        if(Op==TEXT("basic_dispel_self")){LDTactical::Dispel(S->Tactical,false);}
        if(Op==TEXT("basic_dispel")){auto* V=First?StateFor(Cast<ALDCharacter>(First)):S;if(V&&(V==S||!LDTactical::Block(V->Tactical,At))){LDTactical::Dispel(V->Tactical,false,V!=S);}}
        if(Op==TEXT("targeted_mute")){FLDCombatHit H{Body,First,0,LDTactical::EDamage::Magical,true,0,R.Token};H.MuteMS=I->Param(TEXT("duration_ms"));Hits.Add(H);}
        if(Op==TEXT("arm_next_hit")){S->DaggerItem=Id;S->DaggerUntil=At+I->Param(TEXT("duration_ms"));}
        if(Op==TEXT("reflect_next_projectile")){S->Tactical.ReflectUntil=At+I->Param(TEXT("duration_ms"));}
        if(Op==TEXT("spell_block")){S->Tactical.BlockUntil=FMath::Max(S->Tactical.BlockUntil,At+int64(I->Param(TEXT("duration_ms"))));}
        if(Op==TEXT("control_guard")){S->Tactical.GuardUntil=FMath::Max(S->Tactical.GuardUntil,At+int64(I->Param(TEXT("duration_ms"))));}
        if(Op==TEXT("anchor")){S->Tactical.AnchorUntil=At+I->Param(TEXT("duration_ms"));LDTactical::Apply(S->Tactical,LDTactical::EStatus::Slow,S->Tactical.AnchorUntil,Id,30);}
        if(Op==TEXT("barrier")){const double Amount=FMath::FloorToDouble(I->Param(TEXT("amount"))*(Item->Quality==2?114:Item->Quality==1?107:100)/100.);LDTactical::Barrier(S->Tactical,Amount,LDDevelopment::MaxHP(*B),At+int64(I->Param(TEXT("duration_ms"))),At);}
        if(Op==TEXT("heal_over_time")){S->SalveNext=At+1000;S->SalveEnd=At+8000;S->SalveTicks=0;S->SalveDamageAt=LDTactical::Milliseconds(S->LastDamage);}
        if(Op==TEXT("blink")){Body->SetActorLocation(End,false,nullptr,ETeleportType::TeleportPhysics);}
        if(Op==TEXT("grapple")||Op==TEXT("terrain_grapple")){auto* T=Cast<ALDCharacter>(First);if(!T||!LDTactical::Block(StateFor(T)->Tactical,At)){const FVector Anchor=T?T->GetActorLocation():FVector(R.Point),Original=Body->GetActorLocation();S->Action=0;Push(Body,(Anchor-Original).GetSafeNormal2D(),FMath::Min(500.,FMath::Max(0.,FVector::Dist2D(Anchor,Original)-(T?90:0))),Now);if(T){Push(T,(Original-Anchor).GetSafeNormal2D(),180,Now);}}}
        if(I->Consumable)
        {
            if(Field&&(Field->Kind==ELDTacticalField::Ward||Field->Kind==ELDTacticalField::Trap))
            {Field->ChargeIdentity=Id;Field->ChargeKind=Item->Kind;LDInventory::Remove(O->Inventory,Id);Item->State=2;Item->Holder.Reset();Item->Destroy();M->Inventory->Changed(PC);if(M->Economy&&!M->Economy->Record(TEXT("TacticalChargeActivated"))){++M->Economy->AuditFailures;}}
            else{M->Inventory->Remove(PC,Item,true,TEXT("TacticalConsumableUsed"));}
        }
        else{M->Inventory->Changed(PC);}
    }
    for(TActorIterator<ALDTacticalField> It(GetWorld());It;++It){if(It->Source.Get()==Body&&It->Kind==ELDTacticalField::Telegraph){DestroyField(*It);}}
    BreakConcealment(Body);S->QuietSince=Now;S->ExposedUntil=FMath::Max(S->ExposedUntil,Now+4);Why=TEXT("Released once; no refund for blocked/missed effect");return true;
}
