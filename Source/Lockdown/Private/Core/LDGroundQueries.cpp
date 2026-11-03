#include "Core/LDGroundQueries.h"
#include "Engine/World.h"
bool LDGround::MapSurface(UWorld* World,const FVector& XY,FVector& Surface,FVector* Normal)
{
    if(!World||XY.ContainsNaN()){return false;}FHitResult Ground;
    if(!World->LineTraceSingleByChannel(Ground,FVector(XY.X,XY.Y,1600),FVector(XY.X,XY.Y,-200),ECC_GameTraceChannel1)
        ||Ground.ImpactNormal.Z<FMath::Cos(FMath::DegreesToRadians(40.))){return false;}
    Surface=Ground.ImpactPoint;if(Normal){*Normal=Ground.ImpactNormal;}
    // A wall/roof/tree cell is not an order to an unseen floor beneath it.
    return !World->LineTraceTestByChannel(Surface+FVector(0,0,1400),Surface+FVector(0,0,2),ECC_GameTraceChannel2);
}
bool LDGround::ClearSegment(UWorld* World,const FVector& From,const FVector& To,const AActor* Ignore)
{
    if(!World||From.ContainsNaN()||To.ContainsNaN()){return false;}
    FCollisionQueryParams Sight(SCENE_QUERY_STAT(GroundReach),false);if(Ignore){Sight.AddIgnoredActor(Ignore);}
    if(World->LineTraceTestByChannel(FVector(From.X,From.Y,120),FVector(To.X,To.Y,120),ECC_GameTraceChannel2,Sight)){return false;}
    const int32 Steps=FMath::Max(1,FMath::CeilToInt(FVector::Dist2D(From,To)/14));
    double Previous=0;
    for(int32 I=0;I<=Steps;++I)
    {
        const FVector P=FMath::Lerp(From,To,double(I)/Steps);FHitResult Hit;
        if(!World->LineTraceSingleByChannel(Hit,FVector(P.X,P.Y,600),FVector(P.X,P.Y,-100),ECC_GameTraceChannel1)
            ||Hit.ImpactNormal.Z<FMath::Cos(FMath::DegreesToRadians(40.))||(I&&FMath::Abs(Hit.ImpactPoint.Z-Previous)>35)){return false;}
        Previous=Hit.ImpactPoint.Z;
    }
    return true;
}
