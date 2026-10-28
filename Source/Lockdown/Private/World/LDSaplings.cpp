#include "World/LDWorldObjects.h"
#include "World/LDEnvironmentDirector.h"
#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Inventory/LDEquipmentCatalog.h"
#include "Inventory/LDInventoryTypes.h"
#include "Engine/World.h"

namespace
{
bool Breakable(const ALDWorldObject* O)
{return O&&(O->Kind==ELDObjectKind::Sapling||O->Kind==ELDObjectKind::Crate||O->Kind==ELDObjectKind::Barrel||O->Kind==ELDObjectKind::Water||O->Kind==ELDObjectKind::Oil)&&!O->Carrier.IsValid();}
}
ALDWorldObject* ALDWorldObjects::MeleeTarget(ALDCharacter* Body,FVector Aim,double Reach) const
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* PC=Body?Cast<ALDB91Controller>(Body->GetController()):nullptr;
    if(!Ready||!M||!M->IsEnvironmentProfile()||!PC){return nullptr;}
    ALDWorldObject* Best=nullptr;double Distance=Reach*Reach;Aim=Aim.GetSafeNormal2D();
    for(auto Weak:Objects){auto* O=Weak.Get();if(!O||!Breakable(O)||!O->Available()||O->Hp<=0||!Visible(PC,O)){continue;}
     const FVector Delta=O->GetActorLocation()-Body->GetActorLocation();const double D=Delta.SizeSquared2D();
     if(D<=Distance&&FVector::DotProduct(Delta.GetSafeNormal2D(),Aim)>=FMath::Cos(FMath::DegreesToRadians(40.))&&FMath::Abs(Body->GetActorLocation().Z-O->LastClear.Z-90)<100)
     {if(D<Distance||!Best||O->SourceId.LexicalLess(Best->SourceId)){Best=O;Distance=D;}}}
    return Best;
}
bool ALDWorldObjects::StrikeMelee(ALDCharacter* Body,ALDWorldObject* Object,double Damage)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();
    if(!Ready||!M||!M->IsEnvironmentProfile()||!Object||!Breakable(Object)||!Object->Available()||Object->Hp<=0||!FMath::IsFinite(Damage)||Damage<=0){return false;}
    auto* Best=Object;const double Now=GetWorld()->GetTimeSeconds();
    if(Object->Kind==ELDObjectKind::Water||Object->Kind==ELDObjectKind::Oil)
    {
        auto* Item=Object->Physical.Get();if(!Item||Item->State!=0||Item->Holder.IsValid()||Item->bReserved){return false;}
        const auto* Def=ULDEquipmentCatalog::Load(8)->Find(Item->Kind);if(!Def){return false;}
        // Stage the supported finite spill before consuming its actual ground source.
        if(!M->Combat->StageEnvironment(Body,Object->LastClear,Object->Kind==ELDObjectKind::Water?LDEnvironment::EInput::Water:LDEnvironment::EInput::Oil,true,0,Def->Param(TEXT("radius_cm")),LDTactical::Milliseconds(Now),Def->Param(TEXT("lifetime_ms")))){return false;}
        RecordConsumption(Item);Item->State=2;++Item->Revision;Item->Destroy();Best->Hp=0;
    }
    else{Best->Hp=FMath::Max(0.,Best->Hp-Damage);}
    if(auto* State=M->Combat->StateFor(Body)){State->LastDealt=Now;State->ExposedUntil=FMath::Max(State->ExposedUntil,Now+4);}
    Best->Broken=Best->Hp==0;
    if(Best->Broken&&Best->Container())
    {const auto Contents=Best->Contents;Best->Contents.Reset();for(auto P:Contents){if(P.IsValid()){P->Holder.Reset();P->State=0;P->bReserved=false;++P->Revision;P->SetActorLocation(Best->LastClear+FVector(0,0,18));Wrap(P.Get(),Best->LastClear);}}}
    Best->Apply(Best->GetActorLocation(),Best->Flags(),Best->Revision+1);return true;
}
