#include "Combat/LDMonsterRules.h"
namespace LDMonsterRules
{
TArray<FLDMonsterArea> Areas(const FLDDemoMonsterSkill& S,uint8 Stage,const FVector& Origin,const FVector& Target)
{
    const auto Value=[](const FLDDemoRational& Q){return double(Q.Numerator)/double(Q.Denominator);};
    TArray<FLDMonsterArea> Out;
    FVector Direction=(Target-Origin).GetSafeNormal2D();
    if(Direction.IsNearlyZero()){Direction=FVector::ForwardVector;}
    const bool Circle=S.Pattern==ELDMonsterPattern::Circle || S.Pattern==ELDMonsterPattern::EchoCircle || S.Pattern==ELDMonsterPattern::DoubleCircle;
    const bool Line=S.Pattern==ELDMonsterPattern::SweepLine || S.Pattern==ELDMonsterPattern::Projectile || S.Pattern==ELDMonsterPattern::Charge;
    for(int32 I=0;I<S.Pulses;++I)
    {
        auto& A=Out.AddDefaulted_GetRef();
        A.Shape=Circle?ELDMonsterShape::Circle:Line?ELDMonsterShape::Line:ELDMonsterShape::Cone;
        A.Origin=Circle?Target:Origin;A.Direction=Direction;
        A.Range=Value(S.RangeCm);A.Radius=Value(S.RadiusCm);A.Angle=double(S.AngleDegrees);
        A.DelayMs=I*S.PulseIntervalMs;A.RatioBp=S.RatioBp;
        if(S.Pattern==ELDMonsterPattern::SweepLine || S.Pattern==ELDMonsterPattern::Fan || S.Pattern==ELDMonsterPattern::RotatingCone)
        { A.Direction=Direction.RotateAngleAxis((I-(S.Pulses-1)*.5)*S.PulseAngleDegrees,FVector::UpVector); }
        if(S.Pattern==ELDMonsterPattern::DoubleCircle && I>0){A.Origin+=Direction*Value(S.OffsetCm);}
    }
    if(Stage>=S.EchoStage && (S.Pattern==ELDMonsterPattern::Circle || S.Pattern==ELDMonsterPattern::SweepLine
        || S.Pattern==ELDMonsterPattern::EchoCircle || S.Pattern==ELDMonsterPattern::Fan
        || S.Pattern==ELDMonsterPattern::DoubleCircle || S.Pattern==ELDMonsterPattern::RotatingCone) && !Out.IsEmpty())
    {auto A=Out.Last();A.DelayMs+=S.EchoDelayMs;A.RatioBp=S.EchoRatioBp;Out.Add(A);}
    return Out;
}
bool Contains(const FLDMonsterArea& A,const FVector& Point)
{
    const FVector D=FVector(Point.X-A.Origin.X,Point.Y-A.Origin.Y,0);
    if(A.Shape==ELDMonsterShape::Circle){return D.SizeSquared()<=A.Radius*A.Radius;}
    const double Along=FVector::DotProduct(D,A.Direction);
    if(A.Shape==ELDMonsterShape::Line){return Along>=0 && Along<=A.Range && (D-A.Direction*Along).SizeSquared()<=A.Radius*A.Radius;}
    return D.SizeSquared()<=A.Range*A.Range && (D.IsNearlyZero() || FVector::DotProduct(D.GetSafeNormal(),A.Direction)>=FMath::Cos(FMath::DegreesToRadians(A.Angle*.5)));
}
}
