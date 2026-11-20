#include "Core/LDGroundQueries.h"
#include "Character/LDCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
bool LDGround::MapSurface(UWorld* World,const FVector& XY,FVector& Surface,FVector* Normal)
{
    if(!World||XY.ContainsNaN()){return false;}FHitResult Ground;
    // The body's own walkable limit: a slope the capsule and the navigation climb is a legal map point too.
    if(!World->LineTraceSingleByChannel(Ground,FVector(XY.X,XY.Y,1600),FVector(XY.X,XY.Y,-200),ECC_GameTraceChannel1)
        ||Ground.ImpactNormal.Z<GetDefault<ALDCharacter>()->GetCharacterMovement()->GetWalkableFloorZ()){return false;}
    Surface=Ground.ImpactPoint;if(Normal){*Normal=Ground.ImpactNormal;}
    // A wall/roof/tree cell is not an order to an unseen floor beneath it.
    return !World->LineTraceTestByChannel(Surface+FVector(0,0,1400),Surface+FVector(0,0,2),ECC_GameTraceChannel2);
}
