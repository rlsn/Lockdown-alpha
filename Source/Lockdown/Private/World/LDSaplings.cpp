#include "World/LDWorldObjects.h"
#include "World/LDEnvironmentDirector.h"
#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Engine/World.h"

ALDWorldObject* ALDWorldObjects::MeleeTarget(ALDCharacter* Body,FVector Aim,double Reach) const
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* PC=Body?Cast<ALDB91Controller>(Body->GetController()):nullptr;
    if(!Ready||!M||!M->IsEnvironmentProfile()||!PC){return nullptr;}
    ALDWorldObject* Best=nullptr;double Distance=Reach*Reach;Aim=Aim.GetSafeNormal2D();
    for(auto Weak:Objects){auto* O=Weak.Get();if(!O||O->Kind!=ELDObjectKind::Sapling||!O->Available()||O->Hp<=0||!Visible(PC,O)){continue;}
     const FVector Delta=O->GetActorLocation()-Body->GetActorLocation();const double D=Delta.SizeSquared2D();
     if(D<=Distance&&FVector::DotProduct(Delta.GetSafeNormal2D(),Aim)>=FMath::Cos(FMath::DegreesToRadians(40.))&&FMath::Abs(Body->GetActorLocation().Z-O->LastClear.Z-90)<100)
     {if(D<Distance||!Best||O->SourceId.LexicalLess(Best->SourceId)){Best=O;Distance=D;}}}
    return Best;
}
bool ALDWorldObjects::StrikeMelee(ALDCharacter* Body,ALDWorldObject* Object,double Damage)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();
    if(!Ready||!M||!M->IsEnvironmentProfile()||!Object||Object->Kind!=ELDObjectKind::Sapling||!Object->Available()||Object->Hp<=0||!FMath::IsFinite(Damage)||Damage<=0){return false;}
    auto* Best=Object;const double Now=GetWorld()->GetTimeSeconds();
    if(auto* State=M->Combat->StateFor(Body)){State->LastDealt=Now;State->ExposedUntil=FMath::Max(State->ExposedUntil,Now+4);}
    Best->Hp=FMath::Max(0.,Best->Hp-Damage);Best->Broken=Best->Hp==0;
    Best->Apply(Best->GetActorLocation(),Best->Flags(),Best->Revision+1);return true;
}
