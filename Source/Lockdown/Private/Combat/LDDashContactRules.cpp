#include "Combat/LDDashContactRules.h"

namespace
{
bool BallInterval(const FVector& Offset,const FVector& Rate,double RadiusSquared,double Span,
    TOptional<LDDashContactRules::FContactInterval>& Out,FString& Error)
{
    Out.Reset();
    const double A=Rate.SizeSquared(),B=2.*FVector::DotProduct(Offset,Rate),C=Offset.SizeSquared()-RadiusSquared;
    if(!FMath::IsFinite(A)||!FMath::IsFinite(B)||!FMath::IsFinite(C))
    {Error=TEXT("Contact arithmetic exceeded the finite geometry domain.");return false;}
    if(A<=UE_DOUBLE_SMALL_NUMBER)
    {
        if(C<=0){Out=LDDashContactRules::FContactInterval{0,Span};}
        return true;
    }
    const double D=B*B-4.*A*C;
    if(!FMath::IsFinite(D)){Error=TEXT("Contact quadratic exceeded the finite geometry domain.");return false;}
    if(D<0){return true;}
    const double Root=FMath::Sqrt(D);
    const double From=FMath::Max(0.,(-B-Root)/(2.*A)),To=FMath::Min(Span,(-B+Root)/(2.*A));
    if(From<=To){Out=LDDashContactRules::FContactInterval{From,To};}
    return true;
}
bool ValidBand(const LDDashContactRules::FGroundBand& Band,FString& Error)
{
    if(Band.FromCm.ContainsNaN()||Band.ToCm.ContainsNaN()||!FMath::IsFinite(Band.StartSeconds)
        ||!FMath::IsFinite(Band.EndSeconds)||!FMath::IsFinite(Band.DurationSeconds)||Band.StartSeconds<0
        ||Band.EndSeconds<=Band.StartSeconds||Band.DurationSeconds<=0||!FMath::IsFinite(Band.EndSeconds+Band.DurationSeconds)
        ||(Band.ToCm-Band.FromCm).ContainsNaN()||!FMath::IsFinite(FVector::DistSquared2D(Band.FromCm,Band.ToCm))
        ||FVector::DistSquared2D(Band.FromCm,Band.ToCm)<=UE_DOUBLE_SMALL_NUMBER)
    {Error=TEXT("A ground band requires a finite, nonempty timed native path.");return false;}
    return true;
}
LDDashContactRules::FGroundBandShape BandEnds(const LDDashContactRules::FGroundBand& Band,double At)
{
    const double Span=Band.EndSeconds-Band.StartSeconds;
    return {FMath::Lerp(Band.FromCm,Band.ToCm,FMath::Clamp((At-Band.DurationSeconds-Band.StartSeconds)/Span,0.,1.)),
        FMath::Lerp(Band.FromCm,Band.ToCm,FMath::Clamp((At-Band.StartSeconds)/Span,0.,1.))};
}
FVector Flat(const FVector& Point){return FVector(Point.X,Point.Y,0);}
}

bool LDDashContactRules::FirstContact(const FVector& SourceFrom, const FVector& SourceTo,
    const FVector& TargetFrom, const FVector& TargetTo, double RadiusSum, double StemHalfLengthSum,
    TOptional<double>& OutFraction, FString& Error)
{
    OutFraction.Reset();
    Error.Reset();
    if (SourceFrom.ContainsNaN() || SourceTo.ContainsNaN() || TargetFrom.ContainsNaN() || TargetTo.ContainsNaN()
        || !FMath::IsFinite(RadiusSum) || RadiusSum <= 0
        || !FMath::IsFinite(StemHalfLengthSum) || StemHalfLengthSum < 0)
    {
        Error = TEXT("Dash contact requires finite capsule trajectories and positive radii.");
        return false;
    }
    if (SourceFrom.Equals(SourceTo, UE_DOUBLE_SMALL_NUMBER)) { return true; }
    const FVector Offset = SourceFrom - TargetFrom;
    const FVector Velocity = (SourceTo - SourceFrom) - (TargetTo - TargetFrom);
    const auto SurfaceOffset = [StemHalfLengthSum](FVector Value)
    {
        Value.Z = Value.Z > StemHalfLengthSum ? Value.Z - StemHalfLengthSum
            : Value.Z < -StemHalfLengthSum ? Value.Z + StemHalfLengthSum : 0.;
        return Value;
    };
    const FVector Initial = SurfaceOffset(Offset);
    const double RadiusSquared = RadiusSum * RadiusSum;
    if (Offset.ContainsNaN() || Velocity.ContainsNaN() || !FMath::IsFinite(RadiusSquared)
        || !FMath::IsFinite(Initial.SizeSquared()) || !FMath::IsFinite(Velocity.SizeSquared()))
    { Error = TEXT("Dash contact arithmetic exceeded the finite geometry domain."); return false; }
    // Departing an existing overlap is not another collision, even when a new dash was just issued.
    if (Initial.SizeSquared() <= RadiusSquared
        && FVector::DotProduct(Initial, Velocity) >= 0) { return true; }

    TArray<double, TInlineAllocator<4>> Cuts{0., 1.};
    if (FMath::Abs(Velocity.Z) > UE_DOUBLE_SMALL_NUMBER)
    {
        for (const double Height : {-StemHalfLengthSum, StemHalfLengthSum})
        {
            const double At = (Height - Offset.Z) / Velocity.Z;
            if (At > 0 && At < 1) { Cuts.Add(At); }
        }
    }
    Cuts.Sort();
    // The closest vertical points change only at the two capsule-stem boundaries.
    for (int32 Index = 1; Index < Cuts.Num(); ++Index)
    {
        const double From = Cuts[Index - 1], To = Cuts[Index];
        FVector Local = Offset + Velocity * From;
        FVector Rate = Velocity;
        const double MidHeight = Offset.Z + Velocity.Z * (From + To) * .5;
        if (MidHeight > StemHalfLengthSum) { Local.Z -= StemHalfLengthSum; }
        else if (MidHeight < -StemHalfLengthSum) { Local.Z += StemHalfLengthSum; }
        else { Local.Z = 0; Rate.Z = 0; }
        TOptional<FContactInterval> Contact;
        if(!BallInterval(Local,Rate,RadiusSquared,To-From,Contact,Error)){return false;}
        if(Contact.IsSet()){OutFraction=From+Contact->FromSeconds;return true;}
    }
    return true;
}

bool LDDashContactRules::SampleGroundBand(const FGroundBand& Band,double AtSeconds,TOptional<FGroundBandShape>& Out,FString& Error)
{
    Out.Reset();
    if(!ValidBand(Band,Error)||!FMath::IsFinite(AtSeconds))
    {if(Error.IsEmpty()){Error=TEXT("A ground band sample requires a finite time.");}return false;}
    if(AtSeconds>=Band.StartSeconds&&AtSeconds<Band.EndSeconds+Band.DurationSeconds){Out=BandEnds(Band,AtSeconds);}
    return true;
}

bool LDDashContactRules::GroundBandContacts(const FVector& FromCm,const FVector& ToCm,double FromSeconds,double ToSeconds,
    const FGroundBand& Band,double HalfWidthCm,TArray<FContactInterval>& Out,FString& Error)
{
    Out.Reset();
    if(!ValidBand(Band,Error)){return false;}
    if(FromCm.ContainsNaN()||ToCm.ContainsNaN()||!FMath::IsFinite(FromSeconds)||!FMath::IsFinite(ToSeconds)
        ||FromSeconds<0||ToSeconds<FromSeconds||!FMath::IsFinite(HalfWidthCm)||HalfWidthCm<=0
        ||!FMath::IsFinite(HalfWidthCm*HalfWidthCm))
    {Error=TEXT("Ground band contacts require finite, ordered native evidence and positive width.");return false;}
    if(ToSeconds==FromSeconds)
    {
        TOptional<FGroundBandShape> Shape;
        if(!SampleGroundBand(Band,ToSeconds,Shape,Error)){return false;}
        if(Shape.IsSet()&&FMath::PointDistToSegmentSquared(Flat(ToCm),Flat(Shape->FromCm),Flat(Shape->ToCm))<=HalfWidthCm*HalfWidthCm)
        {Out.Add({ToSeconds,ToSeconds});}
        return true;
    }
    const auto PointAt=[&](double At){return Flat(FMath::Lerp(FromCm,ToCm,(At-FromSeconds)/(ToSeconds-FromSeconds)));};
    const FVector Axis=Flat(Band.ToCm-Band.FromCm).GetSafeNormal();
    TArray<double,TInlineAllocator<6>> Cuts{FromSeconds,ToSeconds};
    for(double At:{Band.StartSeconds,Band.EndSeconds,Band.StartSeconds+Band.DurationSeconds,Band.EndSeconds+Band.DurationSeconds})
    {if(At>FromSeconds&&At<ToSeconds){Cuts.Add(At);}}
    Cuts.Sort();
    for(int32 Index=1;Index<Cuts.Num();++Index)
    {
        const double From=Cuts[Index-1],To=Cuts[Index],Span=To-From;
        if(Span<=0||To<=Band.StartSeconds||From>=Band.EndSeconds+Band.DurationSeconds){continue;}
        const auto A=BandEnds(Band,From),B=BandEnds(Band,To);
        const FVector P=PointAt(From),Q=PointAt(To);
        TArray<double,TInlineAllocator<4>> Pieces{0.,Span};
        const auto CutProjection=[&](const FVector& Lo,const FVector& Hi)
        {
            const double First=FVector::DotProduct(P-Flat(Lo),Axis),Last=FVector::DotProduct(Q-Flat(Hi),Axis);
            if(First!=Last)
            {
                const double At=Span*First/(First-Last);
                if(At>0&&At<Span){Pieces.Add(At);}
            }
        };
        CutProjection(A.FromCm,B.FromCm);CutProjection(A.ToCm,B.ToCm);Pieces.Sort();
        for(int32 Part=1;Part<Pieces.Num();++Part)
        {
            const double Lo=Pieces[Part-1],Hi=Pieces[Part];
            if(Hi<=Lo){continue;}
            const double Mid=From+(Lo+Hi)*.5;
            const auto Bounds=BandEnds(Band,Mid);
            const FVector Middle=PointAt(Mid),Point=PointAt(From+Lo);
            FVector Offset,Rate=(Q-P)/Span;
            if(FVector::DotProduct(Middle-Flat(Bounds.FromCm),Axis)<0)
            {
                Offset=Point-Flat(BandEnds(Band,From+Lo).FromCm);Rate-=Flat(B.FromCm-A.FromCm)/Span;
            }
            else if(FVector::DotProduct(Middle-Flat(Bounds.ToCm),Axis)>0)
            {
                Offset=Point-Flat(BandEnds(Band,From+Lo).ToCm);Rate-=Flat(B.ToCm-A.ToCm)/Span;
            }
            else
            {
                Offset=Point-Flat(Band.FromCm);Offset-=Axis*FVector::DotProduct(Offset,Axis);
                Rate-=Axis*FVector::DotProduct(Rate,Axis);
            }
            TOptional<FContactInterval> Contact;
            if(!BallInterval(Offset,Rate,HalfWidthCm*HalfWidthCm,Hi-Lo,Contact,Error)){return false;}
            if(Contact.IsSet()&&From+Lo+Contact->FromSeconds<Band.EndSeconds+Band.DurationSeconds)
            {Out.Add({From+Lo+Contact->FromSeconds,From+Lo+Contact->ToSeconds});}
        }
    }
    return true;
}
