#include "Combat/LDCombatDirector.h"
#include "Character/LDCharacter.h"
#include "Region/LDRegionDirector.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Inventory/LDInventoryDirector.h"
#include "Inventory/LDEquipmentCatalog.h"
#include "Economy/LDEconomyDirector.h"
#include "Objectives/LDObjectiveDirector.h"
#include "Vision/LDVisionDirector.h"
#include "Core/LDGroundQueries.h"
#include "Navigation/LDNavigationComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
bool ALDCombatDirector::Explored(ALDPlayerController* PC,FVector Point) const
{const auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* V=M->Vision->AuthorityViewFor(PC);return V&&LDVision::Bit(V->Explored,LDVision::Cell(Point,V->Grid));}
bool ALDCombatDirector::HasObjectiveCargo(ALDPlayerController* PC) const
{const auto* O=GetWorld()->GetAuthGameMode<ALDGameMode>()->StateFor(PC);return !O||O->Inventory.Items.ContainsByPredicate([](const auto& I){return I.Kind==ELDItemKind::Heartbreaker||I.Kind==ELDItemKind::HeartFragment||I.Kind==ELDItemKind::TowerRelic||(I.Kind>=ELDItemKind::AncientKeyA&&I.Kind<=ELDItemKind::AncientKeyC);});}
bool ALDCombatDirector::MobilityEndpoint(ALDCharacter* Body,FVector Desired,FVector& Endpoint,const AActor* Ignore) const
{
    if(!Body||Desired.ContainsNaN()){return false;}const auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();FVector Surface;
    if(!LDVision::InWorld(Desired,M->Vision->Grid())||!LDGround::MapSurface(GetWorld(),Desired,Surface)){return false;}
    const auto* Capsule=Body->GetCapsuleComponent();Endpoint=Surface+FVector(0,0,Capsule->GetScaledCapsuleHalfHeight()+2);
    FCollisionQueryParams Q(SCENE_QUERY_STAT(LDContentEndpoint),false,Body);if(Ignore){Q.AddIgnoredActor(Ignore);}
    return !GetWorld()->OverlapBlockingTestByChannel(Endpoint,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius()-1,Capsule->GetScaledCapsuleHalfHeight()-2),Q);
}
bool ALDCombatDirector::AtomicSwap(AActor* A,AActor* B,ALDCharacter* Caster,int64 At,FString& Why,bool Execute)
{
    auto Reject=[&](){Why=TEXT("Swap placement unavailable");return false;};if(!A||!B||A==B){return Reject();}auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();
    auto Allowed=[&](AActor* T){if(auto* Body=Cast<ALDCharacter>(T)){const auto* S=StateFor(Body);return S&&S->Hp>0&&At>=LDTactical::Milliseconds(S->ProtectionUntil)&&!Body->IsA<ALDWildSentinel>()&&!S->Tactical.Has(LDTactical::EStatus::Root,At)&&S->Tactical.GroundedUntil<=At;}
        const auto* F=Cast<ALDTacticalField>(T);return F&&F->Source.Get()==Caster&&F->Active(At)&&(F->Kind==ELDTacticalField::Marker||F->Kind==ELDTacticalField::Ward||F->Kind==ELDTacticalField::Decoy);};
    if(!Allowed(A)||!Allowed(B)||FVector::DistSquared2D(A->GetActorLocation(),B->GetActorLocation())>800*800){return Reject();}
    const FVector OldA=A->GetActorLocation(),OldB=B->GetActorLocation();FVector ToA,ToB;
    auto Endpoint=[&](AActor* Actor,FVector Point,AActor* Other,FVector& Out){if(auto* Body=Cast<ALDCharacter>(Actor)){return MobilityEndpoint(Body,Point,Out,Other);}FVector Surface;if(!LDGround::MapSurface(GetWorld(),Point,Surface)){return false;}Out=Surface+FVector(0,0,15);FCollisionQueryParams Q;Q.AddIgnoredActor(Actor);Q.AddIgnoredActor(Other);return !GetWorld()->OverlapBlockingTestByChannel(Out,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeSphere(12),Q);};
    if(!Endpoint(A,OldB,B,ToA)||!Endpoint(B,OldA,A,ToB)){return Reject();}if(!Execute){return true;}
    // Deterministic selected-target order: consume one block, cancel the entire packet.
    for(AActor* T:{A,B}){if(auto* Body=Cast<ALDCharacter>(T);Body&&Body!=Caster){if(LDTactical::Block(StateFor(Body)->Tactical,At)){return true;}}}
    if(!A->SetActorLocation(ToA,false,nullptr,ETeleportType::TeleportPhysics)){return Reject();}
    if(!B->SetActorLocation(ToB,false,nullptr,ETeleportType::TeleportPhysics)){A->SetActorLocation(OldA,false,nullptr,ETeleportType::TeleportPhysics);return Reject();}
    for(AActor* T:{A,B}){if(auto* Body=Cast<ALDCharacter>(T)){Body->GetCharacterMovement()->StopMovementImmediately();ExplicitInterrupt(Body,At/1000.);if(auto* PC=Cast<ALDPlayerController>(Body->GetController())){PC->Navigation->Stop();}}T->ForceNetUpdate();}return true;
}
ALDTacticalField* ALDCombatDirector::CreateField(ALDCharacter* Body,ELDTacticalField Kind,FVector Point,int64 At,int64 Duration,double Hp,double Radius,bool Item)
{
    int32 Count=0,Total=0;ALDTacticalField* Oldest=nullptr;
    for(TActorIterator<ALDTacticalField> It(GetWorld());It;++It){if(It->Active(At)){++Total;if(It->Source.Get()==Body&&It->Kind==Kind&&It->ItemSource==Item){++Count;if(!Oldest||It->ExpiresAt<Oldest->ExpiresAt){Oldest=*It;}}}}
    if(Total>=80){return nullptr;}auto* F=GetWorld()->SpawnActor<ALDTacticalField>(Point,FRotator::ZeroRotator);if(!F){return nullptr;}
    F->Source=Body;F->SourceLife=StateFor(Body)->Identity;F->PlayerSource=Cast<ALDPlayerController>(Body->GetController());F->bPlayerSource=F->PlayerSource.IsValid();F->Kind=Kind;F->ExpiresAt=At+Duration;F->Hp=Hp;F->Radius=Radius;F->ArmedAt=At;F->ItemSource=Item;
    const int32 Limit=Kind==ELDTacticalField::Ward||Kind==ELDTacticalField::Trap?2:1;
    if(Count>=Limit&&Oldest&&Kind!=ELDTacticalField::Trail&&Kind!=ELDTacticalField::Footstep&&Kind!=ELDTacticalField::Gate){DestroyField(Oldest,TEXT("TacticalSourceReplaced"));}return F;
}
void ALDCombatDirector::DestroyField(ALDTacticalField* F,const TCHAR* Cause)
{
    if(!F||F->IsActorBeingDestroyed()){return;}auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const FGuid Charge=F->ChargeIdentity;
    if(F->Kind==ELDTacticalField::Marker&&F->EnemyDestroyed&&F->Source.IsValid()){if(auto* B=BuildFor(F->Source.Get())){B->Cooldowns[uint8(ELDSkill::GuardSwap)]=FMath::Max(B->Cooldowns[uint8(ELDSkill::GuardSwap)],GetWorld()->GetTimeSeconds()+F->SkillCooldown);}}
    auto* Partner=F->Partner.Get();F->Partner.Reset();if(Partner){Partner->Partner.Reset();DestroyField(Partner,Cause);}F->Destroy();
    if(Charge.IsValid()&&M->Economy&&!M->Economy->Record(FName(Cause),0,0,{Charge})){++M->Economy->AuditFailures;}
}
bool ALDCombatDirector::ProjectileIntercept(ALDArcProjectile* P,FVector From,FVector To,int64 At)
{
    TArray<ALDTacticalField*> Domes;for(TActorIterator<ALDTacticalField> It(GetWorld());It;++It){if(It->Kind==ELDTacticalField::Dome&&It->Active(At)&&It->Source!=P->Source){Domes.Add(*It);}}
    Domes.Sort([](const auto& A,const auto& B){return A.Identity.ToString()<B.Identity.ToString();});
    for(auto* D:Domes){const FVector C=D->GetActorLocation();if(FVector::DistSquared2D(From,C)>FMath::Square(D->Radius)&&FMath::PointDistToSegment(C,From,To)<=D->Radius){P->Destroy();return true;}}
    auto* Victim=P->Victim.Get();auto* V=StateFor(Victim);
    if(!P->Basic&&!P->Reflected&&V&&V->Tactical.ReflectUntil>At&&FMath::PointDistToSegment(Victim->GetActorLocation(),From,To)<=40)
    {V->Tactical.ReflectUntil=0;auto* Original=P->Source.Get();const auto* S=StateFor(Original);if(!S||S->Identity!=P->CasterIdentity){P->Destroy();return true;}P->Reflected=true;P->Source=Victim;P->CasterIdentity=V->Identity;P->Victim=Original;P->VictimLife=S->Identity;P->Endpoint=Original->GetActorLocation();P->SetActorLocation(Victim->GetActorLocation());return true;}return false;
}
