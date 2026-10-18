#include "Core/LDGroundQueries.h"
#include "Engine/World.h"
bool LDGround::ClearSegment(UWorld* World,const FVector& From,const FVector& To)
{
    if(!World||From.ContainsNaN()||To.ContainsNaN()){return false;}
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
