#pragma once
#include "CoreMinimal.h"
namespace LDObjective
{
    enum : uint8 { Guild=1,Station=2,Heart=3,Seal=4,Fabricator=9 };
    enum : uint8 { NoChannel=0,Collect=1,Deliver=2,Claim=3,Fabricate=4 };
    enum : uint8 { NoRoute=0,Assault=1,SealRoute=2,Mixed=3 };
    constexpr double WorkSeconds=3,ClaimSeconds=10,HoldExpiry=.25,WorkCooldown=20;
    constexpr double FabricateSeconds=4;
    inline bool CanFabricate(bool Witness,int32 Credits,int64 Gold,bool Crafted,bool Carrying)
    {return Witness&&Credits>=4&&Gold>=100&&!Crafted&&!Carrying;}
    inline double Duration(uint8 Kind){return Kind==Claim?ClaimSeconds:Kind==Fabricate?FabricateSeconds:WorkSeconds;}
    constexpr double InteractRange=120,PickupRange=80,ContestRange=240,HeartSight=360;
    inline FVector Position(uint8 Site)
    {return Site==Guild?FVector(-400,100,0):(Site==Station?FVector(-1280,800,0):FVector::ZeroVector);}
    inline FVector SeedPosition(){return Position(Guild)+FVector(80,0,16);}
    inline uint8 District(const FVector& Point){return Point.Y<-400?2:(Point.X<0?0:1);}
    inline const TCHAR* DistrictName(uint8 Value){return Value==2?TEXT("South"):(Value==0?TEXT("West"):TEXT("East"));}
    inline bool FreshHold(double Now,double Last){return FMath::IsFinite(Now)&&FMath::IsFinite(Last)&&Now>=Last-1.e-6&&Now-Last<=HoldExpiry+1.e-6;}
    inline bool DueBeforeDeadline(double Now,double Started,double Duration,double Deadline)
    {return FMath::IsFinite(Now)&&FMath::IsFinite(Started)&&Duration>0&&Started+Duration<=Deadline&&Now+1.e-6>=Started+Duration;}
}
