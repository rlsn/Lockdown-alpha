#include "Combat/LDEnvironmentRules.h"

using namespace LDEnvironment;
FRecord& FCell::Get(EState S)
{switch(S){case EState::Wet:return Wet;case EState::Frozen:return Frozen;case EState::Burning:return Burning;case EState::Electrified:return Electrified;case EState::Obscured:return Obscured;case EState::Overgrown:return Overgrown;default:return Oil;}}
const FRecord& FCell::Get(EState S) const{return const_cast<FCell*>(this)->Get(S);}
uint8 FCell::Mask(int64 At) const{uint8 M=0;for(uint8 I=0;I<7;++I){if(Get(EState(I)).Until>At){M|=1<<I;}}if(Frozen.Until>At&&Frozen.WetSubstrate){M|=1;}return M;}
void FCell::Expire(int64 At){for(uint8 I=0;I<7;++I){if(Get(EState(I)).Until<=At){Get(EState(I))={};}}}
bool FCell::Owned(FGuid Id,int64 At) const{for(uint8 I=0;I<7;++I){const auto& R=Get(EState(I));if(R.Until>At&&(R.BudgetOwner.IsValid()?R.BudgetOwner:R.Owner)==Id){return true;}}return false;}
int32 LDEnvironment::Cap(EState S){static const int32 Caps[]={10000,10000,6000,4000,6000,8000,8000};return Caps[uint8(S)];}
uint8 LDEnvironment::Substrate(FIntPoint Id,bool RiverMap)
{
    const FVector P=LDRiver::Centre(Id);if(RiverMap&&LDRiver::InRiver(P)){return Water;}
    // Public authored test garden, fuel socket and metal apron. Never infer fuel from trees.
    if(P.X>=-2800&&P.X<-2000&&P.Y>=-1200&&P.Y<-400){return Soil;}
    if(P.X>=-1200&&P.X<-800&&P.Y>=-1200&&P.Y<-800){return Flammable;}
    if(P.X>=200&&P.X<800&&P.Y>=-1200&&P.Y<-600){return Metal;}
    return Stone;
}
bool LDEnvironment::Transition(FCell& C,const FCommand& Q,uint8 Base,int64 At)
{
    C.Expire(At);const bool Ice=C.Frozen.Until>At,Wet=(Base&Water)||C.Wet.Until>At||(Ice&&C.Frozen.WetSubstrate);
    auto Put=[&](EState S,int32 Duration=0)
    {auto& R=C.Get(S);const int64 Old=R.Until;const uint32 Rev=FMath::Max(R.Revision+1,Q.State.Revision);R=Q.State;R.Created=At;R.Revision=Rev;
        int64 Span=Duration?Duration:FMath::Max<int64>(0,Q.State.Until-Q.State.Created);
        if(S==EState::Frozen){Span+=Q.FrozenBonus;}if(S==EState::Burning){Span+=Q.BurningBonus;}if(S==EState::Electrified){Span+=Q.ElectricBonus;}
        R.Until=FMath::Min(At+Cap(S),FMath::Max(Old,At+Span));R.Magnitude=S==EState::Burning?20:S==EState::Electrified?18:R.Magnitude;};
    switch(Q.Input)
    {
    case EInput::Water:C.Burning={};Put(EState::Wet);return true;
    case EInput::Frost:
        if(!Wet){return false;}C.Burning={};C.Electrified={};C.Overgrown={};C.Oil={};Put(EState::Frozen);C.Frozen.WetSubstrate=!(Base&Water);return true;
    case EInput::Fire:
        if(Ice){C.Frozen={};Put(EState::Wet,3000);Put(EState::Obscured,3000);return true;}
        if(Wet){Put(EState::Obscured,3000);return true;}
        if(C.Oil.Until>At){C.Oil={};C.Overgrown={};C.Electrified={};Put(EState::Burning,6000);return true;}
        if(C.Overgrown.Until>At||(Base&Flammable)){C.Overgrown={};C.Electrified={};Put(EState::Burning,3000);return true;}return false;
    case EInput::Lightning:if(!Wet||Ice){return false;}C.Overgrown={};Put(EState::Electrified);return true;
    case EInput::Wind:if(C.Obscured.Until<=At){return false;}C.Obscured={};return true;
    case EInput::Nature:if(!(Base&Soil)||Ice||C.Burning.Until>At){return false;}Put(EState::Overgrown);return true;
    case EInput::Shatter:if(!Ice){return false;}C.Frozen={};Put(EState::Wet,3000);return true;
    case EInput::Smoke:Put(EState::Obscured);return true;
    case EInput::Oil:if(Wet||Ice||C.Burning.Until>At){return false;}Put(EState::Oil);return true;
    default:return false;
    }
}
TArray<FIntPoint> LDEnvironment::Mask(FVector Start,FVector Aim,double Length,double Width,bool Disk,int32 Limit,bool Cone)
{
    // Reuse the deterministic rectangle/disk ordering; cone uses along/perpendicular order.
    TArray<FIntPoint> Out;const FVector Dir=(Aim-Start).GetSafeNormal2D();if(!Disk&&Dir.IsNearlyZero()){return Out;}
    const FVector End=Disk?Aim:Start+Dir*Length;const double Radius=Cone?Length:Width;
    const auto Min=LDRiver::Cell((Disk?Aim:Start).ComponentMin(End)-FVector(Radius,Radius,0));
    const auto Max=LDRiver::Cell((Disk?Aim:Start).ComponentMax(End)+FVector(Radius,Radius,0));
    for(int32 Y=FMath::Max(0,Min.Y);Y<=FMath::Min(39,Max.Y);++Y){for(int32 X=FMath::Max(0,Min.X);X<=FMath::Min(59,Max.X);++X)
    {const FIntPoint Id(X,Y);const FVector P=LDRiver::Centre(Id),V=P-Start;const double Along=FVector::DotProduct(V,Dir);
        const bool Inside=Disk?FVector::DistSquared2D(P,Aim)<=Width*Width:Cone?V.SizeSquared2D()<=Length*Length&&FVector::DotProduct(V.GetSafeNormal2D(),Dir)>=FMath::Cos(FMath::DegreesToRadians(Width/2)):Along>=0&&Along<=Length&&FVector::DistSquared2D(P,Start+Dir*Along)<=Width*Width;if(Inside){Out.Add(Id);}}}
    Out.Sort([&](FIntPoint A,FIntPoint B){const FVector PA=LDRiver::Centre(A),PB=LDRiver::Centre(B);const double DA=Disk?FVector::DistSquared2D(PA,Aim):FVector::DotProduct(PA-Start,Dir),DB=Disk?FVector::DistSquared2D(PB,Aim):FVector::DotProduct(PB-Start,Dir);if(!FMath::IsNearlyEqual(DA,DB)){return DA<DB;}if(!Disk){const double WA=FVector::DistSquared2D(PA,Start+Dir*DA),WB=FVector::DistSquared2D(PB,Start+Dir*DB);if(!FMath::IsNearlyEqual(WA,WB)){return WA<WB;}}return A.Y!=B.Y?A.Y<B.Y:A.X<B.X;});
    if(Out.Num()>Limit){Out.SetNum(Limit);}return Out;
}
bool LDEnvironment::Occludes(FVector From,FVector To,const TSet<FIntPoint>& Cloud)
{
    if(Cloud.IsEmpty()){return false;}const FVector Delta=To-From;TArray<double> Cuts={0.,1.};
    for(int32 Axis=0;Axis<2;++Axis){const double D=Delta[Axis];if(FMath::Abs(D)<1.e-9){continue;}const double Origin=Axis?-4000:-6000,Lo=FMath::Min(From[Axis],To[Axis]),Hi=FMath::Max(From[Axis],To[Axis]);for(int32 I=FMath::FloorToInt((Lo-Origin)/200)+1;Origin+I*200<Hi;++I){Cuts.Add((Origin+I*200-From[Axis])/D);}}
    Cuts.Sort();bool First=true,Inside=false;
    for(int32 I=1;I<Cuts.Num();++I){if(Cuts[I]-Cuts[I-1]<1.e-9){continue;}const bool At=Cloud.Contains(LDRiver::Cell(From+Delta*((Cuts[I]+Cuts[I-1])*.5)));if(First){Inside=At;First=false;}else if(At!=Inside){return true;}}
    // A wholly external ray is clear; a wholly internal ray is clear. Any boundary crossing blocks both ways.
    return false;
}
