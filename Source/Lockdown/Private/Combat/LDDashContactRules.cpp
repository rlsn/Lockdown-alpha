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

