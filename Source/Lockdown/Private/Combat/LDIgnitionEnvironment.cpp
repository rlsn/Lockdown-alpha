#include "Combat/LDCombatDirector.h"
#include "World/LDWorldObjects.h"
#include "Character/LDCharacter.h"
#include "Core/LDGroundQueries.h"
#include "Region/LDMapDefinition.h"
#include "Engine/World.h"

FGuid ALDCombatDirector::ReserveIgnition(ALDCharacter* Body,FVector Point,ALDWorldObject* Socket,int64 At)
{
    if(!FullMapEnvironment()||!Body||!BuildFor(Body)){return {};}
    if(Socket&&(Socket->Kind!=ELDObjectKind::Sapling||!Socket->Available()||Socket->Hp<=0||Socket->BurnReceipt.IsValid())){return {};}
    RetireEnvironment(At);auto Q=EnvironmentCommand(Body,LDEnvironment::EInput::Fire,At,3000);Q.FireDuration=3000;Q.BurningBonus=0;
    const auto& Cells=RiverCommands.IsEmpty()?RiverCells:ProjectedEnvironment;
    for(auto Id:LDEnvironment::Mask(Body->GetActorLocation(),Point,0,180,true,4,false,true))
    {
        FVector P=LDRiver::Centre(Id),Ground,Normal;P.Z=Point.Z;
        if(!LDGround::MapSurface(GetWorld(),P,Ground,&Normal)||FMath::Abs(Ground.Z-Point.Z)>80||!LDGround::ClearSegment(GetWorld(),Point+FVector(0,0,30),Ground+FVector(0,0,30),Socket)||ULDMapDefinition::Get(GetWorld())->RiverAt(Ground)){continue;}
        const auto* C=Cells.Find(Id);if(C&&(C->Wet.Until>At||C->Frozen.Until>At||C->Burning.Until>At||(C->Mask(At)&&FMath::Abs(C->SurfaceZ-Ground.Z)>12))){continue;}
        if(!Socket&&(!C||C->Oil.Until<=At)){continue;}
        Q.Cells.Add(Id);Q.Surfaces.Add(Id,Ground.Z);Q.Normals.Add(Id,Normal);if(Socket){Q.FuelCells.Add(Id);}
    }
    if(Q.Cells.IsEmpty()){return {};}
    const FGuid Lease=ReserveEnvironment(Body,Q.Cells.Num(),At,3000);auto* Entry=EnvironmentLeases.Find(Lease);if(!Entry){return {};}
    Entry->Command=MoveTemp(Q);Entry->Socket=Socket;Entry->Ignition=true;Entry->Expires=At+3000;return Lease;
}
bool ALDCombatDirector::CommitIgnition(FGuid Lease,ALDCharacter* Body,ALDWorldObject* Socket,int64 At)
{
    const auto* Entry=EnvironmentLeases.Find(Lease);const auto* S=StateFor(Body);
    if(!Entry||!Entry->Ignition||Entry->Socket.Get()!=Socket||At>=Entry->Expires||!S||S->Identity!=Entry->Command.State.Life){return false;}
    if(Socket&&(Socket->Kind!=ELDObjectKind::Sapling||!Socket->Available()||Socket->Hp<=0||Socket->BurnReceipt.IsValid())){return false;}
    auto Q=Entry->Command;const auto& Cells=RiverCommands.IsEmpty()?RiverCells:ProjectedEnvironment;
    const FVector Aim=Socket?Socket->LastClear:FVector(LDRiver::Centre(Q.Cells[0]).X,LDRiver::Centre(Q.Cells[0]).Y,Q.Surfaces.FindChecked(Q.Cells[0]));
    for(auto Id:Q.Cells)
    {
        FVector P=LDRiver::Centre(Id),Ground;P.Z=Q.Surfaces.FindChecked(Id);
        if(!LDGround::MapSurface(GetWorld(),P,Ground)||FMath::Abs(Ground.Z-P.Z)>12||!LDGround::ClearSegment(GetWorld(),Aim+FVector(0,0,30),Ground+FVector(0,0,30),Socket)||ULDMapDefinition::Get(GetWorld())->RiverAt(Ground)){return false;}
        const auto* C=Cells.Find(Id);if(C&&(C->Wet.Until>At||C->Frozen.Until>At||C->Burning.Until>At||(C->Mask(At)&&FMath::Abs(C->SurfaceZ-P.Z)>12))){return false;}
        if(!Socket&&(!C||C->Oil.Until<=At)){return false;}
    }
    Q.State.Created=At;Q.State.Until=At+3000;EnvironmentLeases.Remove(Lease);
    return QueueEnvironment(MoveTemp(Q),At,true);
}
