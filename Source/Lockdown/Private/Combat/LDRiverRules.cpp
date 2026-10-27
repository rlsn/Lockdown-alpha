#include "Combat/LDRiverRules.h"
bool LDRiver::RefreshContact(LDTactical::FState& State,bool& LastWet,int64& SampledAt,
    const FContactInput& Input,int64 At,bool Boundary)
{
    if(At<SampledAt) { return false; }
    const bool Water=Input.Floor&&Input.Water;
    if(Input.Alive&&((Water&&(Boundary||!LastWet))||(!Water&&LastWet&&State.Has(LDTactical::EStatus::Wet,At))))
    { LDTactical::Apply(State,LDTactical::EStatus::Wet,At+(Input.Wading?1500:3000),FGuid()); }
    if(Water) { State.Statuses.RemoveAll([](const auto& S){return S.Kind==LDTactical::EStatus::Burning;}); }
    State.SurfaceSlow=Input.Alive&&!Input.Protected&&Input.Floor&&!Input.Immune
        ?Input.Frozen?(Input.IceGrip?15:30):Input.Growth?15:0:0;
    LastWet=Water; SampledAt=At; return Water;
}
TArray<FIntPoint> LDRiver::Footprint(FVector Start,FVector Aim,double Length,double Width,bool Disk)
{
    TArray<FIntPoint> Out;const FVector Dir=(Aim-Start).GetSafeNormal2D();
    if(!Disk&&Dir.IsNearlyZero()){return Out;}
    const FVector End=Disk?Aim:Start+Dir*Length;
    const FIntPoint Min=Cell((Disk?Aim:Start).ComponentMin(End)-FVector(Width,Width,0));
    const FIntPoint Max=Cell((Disk?Aim:Start).ComponentMax(End)+FVector(Width,Width,0));
    for(int32 Y=FMath::Max(0,Min.Y);Y<=FMath::Min(39,Max.Y);++Y){for(int32 X=FMath::Max(0,Min.X);X<=FMath::Min(59,Max.X);++X)
    {const FIntPoint C(X,Y);const FVector P=Centre(C),V=P-Start;const double Along=FVector::DotProduct(V,Dir);
        if(Disk?FVector::DistSquared2D(P,Aim)<=Width*Width:Along>=0&&Along<=Length&&FVector::DistSquared2D(P,Start+Dir*Along)<=Width*Width){Out.Add(C);}}}
    Out.Sort([&](FIntPoint A,FIntPoint B){const FVector PA=Centre(A),PB=Centre(B);const double DA=Disk?FVector::DistSquared2D(PA,Aim):FVector::DotProduct(PA-Start,Dir),DB=Disk?FVector::DistSquared2D(PB,Aim):FVector::DotProduct(PB-Start,Dir);if(!FMath::IsNearlyEqual(DA,DB)){return DA<DB;}if(!Disk){const double WA=FVector::DistSquared2D(PA,Start+Dir*DA),WB=FVector::DistSquared2D(PB,Start+Dir*DB);if(!FMath::IsNearlyEqual(WA,WB)){return WA<WB;}}return A.Y!=B.Y?A.Y<B.Y:A.X<B.X;});
    if(Out.Num()>CastCap){Out.SetNum(CastCap);}return Out;
}
