#include "Core/LDGroundQueries.h"
#include "Region/LDMapRegistry.h"
#include "Engine/World.h"
bool LDGround::MapSurface(UWorld* World,const FVector& XY,FVector& Surface,FVector* Normal)
{
    if(!World||XY.ContainsNaN()){return false;}
    const auto* Registry=World->GetSubsystem<ULDMapRegistry>();
    return Registry&&Registry->ResolveWalkableGround(FVector2D(XY.X,XY.Y),Surface,Normal);
}
