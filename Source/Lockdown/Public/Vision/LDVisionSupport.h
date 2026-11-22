#pragma once
#include "CoreMinimal.h"

class UWorld;

// Derived geometry only. Cooked collision, rather than visual vertices or actor identity, defines a departure surface.
class LOCKDOWN_API FLDVisionSupport
{
public:
    void Build(UWorld& World);
    double DepartureExit(const FVector& From,const FVector& To,const FHitResult& Support) const;
private:
    struct FKey
    {
        FIntVector Normal=FIntVector::ZeroValue;
        int32 Height=0;
        bool operator==(const FKey& B)const{return Normal==B.Normal&&Height==B.Height;}
        friend uint32 GetTypeHash(const FKey& K){return HashCombine(GetTypeHash(K.Normal),GetTypeHash(K.Height));}
    };
    struct FTriangle
    {
        FVector2D A,B,C;
        FVector Normal;
        double Height=0;
    };
    struct FPlane
    {
        TArray<FTriangle> Triangles;
        TMap<FIntPoint,TArray<int32>> Cells;
    };
    static FKey Key(const FVector& Normal,double Height);
    TMap<FKey,FPlane> Planes;
};
