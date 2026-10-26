#include "Combat/LDCombatDirector.h"
#include "World/LDWorldDirector.h"
#include "Region/LDRegionDirector.h"
#include "Combat/LDTacticalGeometry.h"
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Objectives/LDObjectiveDirector.h"
#include "Vision/LDVisionDirector.h"
#include "Core/LDGroundQueries.h"
#include "EngineUtils.h"
#include "Navigation/LDNavigationComponent.h"

bool ALDTacticalField::Active(int64 At) const
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* S=M&&M->Combat?M->Combat->StateFor(Source.Get()):nullptr;
    if(uint8(Kind)>=16){return !IsActorBeingDestroyed()&&M&&M->IsPlaying()&&ExpiresAt>At;}
    if(Kind==ELDTacticalField::Scramble){return !IsActorBeingDestroyed()&&M&&M->IsPlaying();}
    if(Kind==ELDTacticalField::Gate&&(!Partner.IsValid()||Partner->Hp<=0||Partner->IsActorBeingDestroyed())){return false;}
    return !IsActorBeingDestroyed()&&M&&M->IsPlaying()&&S&&S->Identity==SourceLife&&S->Hp>0&&(!bPlayerSource||(PlayerSource.IsValid()&&PlayerSource->GetPawn()==Source.Get()&&M->StateFor(PlayerSource.Get())&&M->StateFor(PlayerSource.Get())->bActive))&&ExpiresAt>At&&Hp>0;
}
bool ALDCombatDirector::QuietForPreparation(ALDCharacter* Body) const
{const auto* S=StateFor(Body);if(!S||GetWorld()->GetTimeSeconds()-FMath::Max(S->LastDamage,S->LastDealt)<6){return false;}for(TActorIterator<ALDArcProjectile> It(GetWorld());It;++It){if(It->Source.Get()==Body&&!It->IsActorBeingDestroyed()){return false;}}return true;}
bool ALDCombatDirector::Concealed(const ALDCharacter* Body) const
{const auto* S=StateFor(Body);return S&&S->Tactical.ConcealedUntil>LDTactical::Milliseconds(GetWorld()->GetTimeSeconds());}
void ALDCombatDirector::BreakConcealment(ALDCharacter* Body){if(auto* S=StateFor(Body)){S->Tactical.ConcealedUntil=0;}}
void ALDCombatDirector::ExplicitInterrupt(ALDCharacter* Body,double Now)
{
    auto* S=StateFor(Body);if(!S||S->Hp<=0||Body->IsA<ALDWildSentinel>()||(Cast<ALDWorldPawn>(Body)&&Cast<ALDWorldPawn>(Body)->Immune())){return;}
    if(S->Action==9||S->Action==10){CancelTactical(Body,TEXT("Explicit interrupt"));}else{CancelSkill(Body,*S);}
    if(auto* PC=Cast<ALDPlayerController>(Body->GetController())){GetWorld()->GetAuthGameMode<ALDGameMode>()->Objectives->Interrupt(PC,Now,true);}
}
bool ALDCombatDirector::Push(ALDCharacter* Body,const FVector& Direction,double Distance,double Now)
{
    auto* S=StateFor(Body);if(!HasAuthority()||!S||S->Hp<=0||Body->IsA<ALDWildSentinel>()||(Cast<ALDWorldPawn>(Body)&&Cast<ALDWorldPawn>(Body)->Immune())||S->Tactical.AnchorUntil>LDTactical::Milliseconds(Now)){return false;}
    const FVector Start=Body->GetActorLocation(),Dir=Direction.GetSafeNormal2D();if(Dir.IsNearlyZero()||Distance<=0){return false;}
    auto* Move=Body->GetCharacterMovement();
    // Small native swept capsule moves, with static floor/ledge validation before each step.
    for(double Left=FMath::Min(Distance,1000.);Left>0;Left-=10)
    {
        const FVector From=Body->GetActorLocation(),XY=From+Dir*FMath::Min(Left,10.);FVector Ground;
        if(!LDVision::InWorld(XY,GetWorld()->GetAuthGameMode<ALDGameMode>()->Vision->Grid())||!LDGround::MapSurface(GetWorld(),XY,Ground)||!LDGround::ClearSegment(GetWorld(),From,Ground)||FMath::Abs(Ground.Z-(From.Z-Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()-2))>35){break;}
        const FVector To(Ground.X,Ground.Y,Ground.Z+Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2);FHitResult Hit;
        Move->SafeMoveUpdatedComponent(To-From,Body->GetActorQuat(),true,Hit);
        if(Hit.bBlockingHit){break;}
    }
    if(FVector::DistSquared2D(Start,Body->GetActorLocation())<=UE_DOUBLE_SMALL_NUMBER){return false;}
    Move->RemoveRootMotionSource(TEXT("LD_AuthorityDash"));Move->StopMovementImmediately();
    if(auto* PC=Cast<ALDPlayerController>(Body->GetController())){PC->Navigation->Stop();}
    ExplicitInterrupt(Body,Now);return true;
}
void ALDCombatDirector::SampleFields(int64 At)
{
    const auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(!HasAuthority()||!M||!M->IsTacticalProfile()){return;}
    for(auto& Pair:Bodies){Pair.Value.Tactical.GroundedUntil=0;}
    TArray<ALDTacticalField*> Fields;for(TActorIterator<ALDTacticalField> It(GetWorld());It;++It){if(It->EnvironmentPool){continue;}if(It->Active(At)||(It->Kind==ELDTacticalField::Ember&&It->TickIndex<6&&It->Active(FMath::Min(At,It->ExpiresAt-1)))){Fields.Add(*It);}else{DestroyField(*It);}}
    Fields.Sort([](const auto& A,const auto& B){return A.Identity.ToString()<B.Identity.ToString();});
    for(auto* F:Fields)
    {
        if(F->Kind!=ELDTacticalField::Grounding&&(F->Kind!=ELDTacticalField::Trap||F->ArmedAt>At)){continue;}
        TArray<ALDCharacter*> Targets;
        for(const auto& Pair:Bodies)
        {
            auto* B=Pair.Key.Get();if(!B||B==F->Source.Get()||Pair.Value.Hp<=0||B->IsA<ALDWildSentinel>()||At<LDTactical::Milliseconds(Pair.Value.ProtectionUntil)){continue;}
            const auto P=B->GetActorLocation();if(FVector::DistSquared2D(P,F->GetActorLocation())>FMath::Square(F->Radius)||!LDGround::ClearSegment(GetWorld(),F->GetActorLocation(),P)){continue;}
            if(F->Kind==ELDTacticalField::Grounding){auto* S=StateFor(B);S->Tactical.GroundedUntil=F->ExpiresAt;B->GetCharacterMovement()->RemoveRootMotionSource(TEXT("LD_AuthorityDash"));}
            else{Targets.Add(B);}
        }
        Targets.Sort([&](const auto& A,const auto& B){return StateFor(&A)->Identity.ToString()<StateFor(&B)->Identity.ToString();});
        if(!Targets.IsEmpty()){ApplyStatus(Targets[0],LDTactical::EStatus::Root,At+int64(F->Amount>0?F->Amount:1600),F->Identity);DestroyField(F);}
    }
}
