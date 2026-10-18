#pragma once
#include "CoreMinimal.h"

namespace LDCombat
{
    constexpr double BasicWindup=.15, BasicCadence=.75, BasicReach=96, BasicHalfCone=40;
    constexpr double AssassinWindup=.5, AssassinRecovery=.25, AssassinCooldown=18, AssassinReach=112;
    constexpr double DashDuration=.2, DashDistance=360, DashCooldown=7;
    inline bool InCone(const FVector& From,const FVector& To,const FVector& Aim,double Radius,double HalfDegrees)
    {
        const FVector Delta(To.X-From.X,To.Y-From.Y,0);
        if (Delta.ContainsNaN() || Aim.ContainsNaN() || Delta.SizeSquared()>Radius*Radius) { return false; }
        if (Delta.IsNearlyZero()) { return true; }
        return FVector::DotProduct(Delta.GetSafeNormal(),FVector(Aim.X,Aim.Y,0).GetSafeNormal())+1.e-8>=FMath::Cos(FMath::DegreesToRadians(HalfDegrees));
    }
    inline bool IsRear(const FVector& Attacker,const FVector& Target,const FVector& TargetForward)
    {
        const FVector Bearing(Attacker.X-Target.X,Attacker.Y-Target.Y,0);
        return !Bearing.IsNearlyZero() && FVector::DotProduct(Bearing.GetSafeNormal(),TargetForward.GetSafeNormal2D())<=-.5+1.e-8;
    }
    inline bool Quiet(double Now,double QuietSince,double ExposedUntil,bool bSprint,bool bDash)
    { return FMath::IsFinite(Now) && Now-QuietSince>=1 && Now>=ExposedUntil && !bSprint && !bDash; }
    inline bool TargetUnalert(double Now,double LastDamage,double LastDealt)
    { return Now-FMath::Max(LastDamage,LastDealt)>=4; }
}
