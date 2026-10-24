#include "Combat/LDTacticalGeometry.h"
namespace LDTactical
{
    bool SmokeOccludes(const FVector& From,const FVector& To,const TArray<FSmokeCircle>& Circles)
    {
        const FVector2D A(From),D=FVector2D(To)-A;const double Q=D.SizeSquared();if(Q<1.e-8){return false;}
        TArray<FVector2D> Intervals;
        for(const auto& C:Circles)
        {
            const FVector2D Offset=A-FVector2D(C.Centre);const double B=2*FVector2D::DotProduct(Offset,D),V=Offset.SizeSquared()-C.Radius*C.Radius;
            const double Disc=B*B-4*Q*V;if(Disc<=1.e-8){continue;}
            const double L=FMath::Max(0.,(-B-FMath::Sqrt(Disc))/(2*Q)),R=FMath::Min(1.,(-B+FMath::Sqrt(Disc))/(2*Q));
            if(R-L>1.e-8){Intervals.Add(FVector2D(L,R));}
        }
        if(Intervals.IsEmpty()){return false;}
        Intervals.Sort([](const auto& X,const auto& Y){return X.X<Y.X;});double L=Intervals[0].X,R=Intervals[0].Y;
        for(int32 I=1;I<Intervals.Num();++I){if(Intervals[I].X>R+1.e-8){return true;}R=FMath::Max(R,Intervals[I].Y);}
        return L>1.e-8||R<1.-1.e-8;
    }
}
