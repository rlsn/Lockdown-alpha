#include "Core/LDGroundQueries.h"
#include "Engine/World.h"
#include "World/LDEnvironmentDirector.h"
#include "Region/LDMapDefinition.h"
bool LDGround::MapSurface(UWorld* World,const FVector& XY,FVector& Surface,FVector* Normal)
{
    if(!World||XY.ContainsNaN()){return false;}FHitResult Ground;
    if(ALDEnvironmentDirector::Get(World)!=nullptr)
    {if(!ULDMapDefinition::InBounds(XY)||!World->LineTraceSingleByChannel(Ground,XY+FVector(0,0,150),XY-FVector(0,0,200),ECC_GameTraceChannel1)||Ground.ImpactNormal.Z<FMath::Cos(FMath::DegreesToRadians(40.))){return false;}
     Surface=Ground.ImpactPoint;if(Normal){*Normal=Ground.ImpactNormal;}return FMath::Abs(Surface.Z-XY.Z)<=150; }
    if(!World->LineTraceSingleByChannel(Ground,FVector(XY.X,XY.Y,1600),FVector(XY.X,XY.Y,-200),ECC_GameTraceChannel1)
        ||Ground.ImpactNormal.Z<FMath::Cos(FMath::DegreesToRadians(40.))){return false;}
    Surface=Ground.ImpactPoint;if(Normal){*Normal=Ground.ImpactNormal;}
    // A wall/roof/tree cell is not an order to an unseen floor beneath it.
    return !World->LineTraceTestByChannel(Surface+FVector(0,0,1400),Surface+FVector(0,0,2),ECC_GameTraceChannel2);
}
bool LDGround::ClearSegment(UWorld* World,const FVector& From,const FVector& To)
{
    if(!World||From.ContainsNaN()||To.ContainsNaN()){return false;}
    if(ALDEnvironmentDirector::Get(World)!=nullptr)
    {if(!ULDMapDefinition::InBounds(From)||!ULDMapDefinition::InBounds(To)||World->LineTraceTestByChannel(From,To,ECC_GameTraceChannel2)){return false;}
     const int32 Count=FMath::Max(1,FMath::CeilToInt(FVector::Dist2D(From,To)/14));double Previous=0;
     for(int32 I=0;I<=Count;++I){FVector Surface;if(!MapSurface(World,FMath::Lerp(From,To,double(I)/Count),Surface)||(I&&FMath::Abs(Surface.Z-Previous)>35)){return false;}Previous=Surface.Z;}return true;}
    if(World->LineTraceTestByChannel(FVector(From.X,From.Y,120),FVector(To.X,To.Y,120),ECC_GameTraceChannel2)){return false;}
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
