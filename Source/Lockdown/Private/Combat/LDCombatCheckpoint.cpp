#include "Combat/LDCombatDirector.h"
#include "Engine/World.h"
#include "EngineUtils.h"

bool ALDCombatDirector::EnvironmentSettled() const
{
    if(!EnvironmentLeases.IsEmpty()||!RiverCommands.IsEmpty()){return false;}
    const int64 At=LDTactical::Milliseconds(GetWorld()->GetTimeSeconds());
    for(const auto& Cell:RiverCells){if(Cell.Value.Mask(At)){return false;}}
    for(TActorIterator<ALDTacticalField> Field(GetWorld());Field;++Field)
    {if(!Field->EnvironmentPool&&Field->Active(At)){return false;}}
    for(TActorIterator<ALDArcProjectile> Projectile(GetWorld());Projectile;++Projectile)
    {if(!Projectile->IsActorBeingDestroyed()){return false;}}
    return true;
}
