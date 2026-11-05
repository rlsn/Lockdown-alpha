#include "Navigation/LDOrderRules.h"

int32 LDOrderRules::NearestCandidate(const TArray<FLDObservedUnitView>& Units, const FVector& FootCm,
    double RadiusCm, const FGuid& Self)
{
    if (!(RadiusCm>0) || !FMath::IsFinite(RadiusCm)) { return INDEX_NONE; }
    const double ReachSquared=RadiusCm*RadiusCm;
    int32 Best=INDEX_NONE;
    double BestDistance=0;
    for (int32 Index=0; Index<Units.Num(); ++Index)
    {
        const FLDObservedUnitView& Unit=Units[Index];
        if (Unit.Kind==ELDObservedUnitKind::Contender && Unit.ContenderId==Self) { continue; }
        const double Distance=FVector::DistSquared2D(FootCm,FVector(Unit.FeetCm));
        if (Distance>ReachSquared) { continue; }
        if (Best==INDEX_NONE || Distance<BestDistance || (Distance==BestDistance && Unit.Handle<Units[Best].Handle))
        {
            Best=Index;
            BestDistance=Distance;
        }
    }
    return Best;
}

LDOrderRules::EForcedOrder LDOrderRules::ForcedOrderStep(bool bTaunted, bool bActiveForced, bool bSameTaunter)
{
    if (!bTaunted) { return bActiveForced ? EForcedOrder::End : EForcedOrder::None; }
    return bActiveForced && bSameTaunter ? EForcedOrder::Keep : EForcedOrder::Begin;
}

bool LDOrderRules::ShopFrontPoint(const FBox2D& FootprintCm, const FVector& FromCm, double BodyRadiusCm, FVector& OutCm)
{
    OutCm = FromCm;
    if (!FootprintCm.bIsValid || FromCm.ContainsNaN() || !(BodyRadiusCm >= 0)) { return false; }
    const FBox2D Grown = FootprintCm.ExpandBy(BodyRadiusCm);
    const FVector2D From(FromCm.X, FromCm.Y);
    const FVector2D Nearest(FMath::Clamp(From.X, Grown.Min.X, Grown.Max.X), FMath::Clamp(From.Y, Grown.Min.Y, Grown.Max.Y));
    // Inside or on the grown footprint already: nothing nearer to walk to.
    if (FVector2D::DistSquared(From, Nearest) < 1.) { return false; }
    OutCm = FVector(Nearest.X, Nearest.Y, FromCm.Z);
    return true;
}

TArray<FVector> LDOrderRules::ShopStopCandidates(const FVector& FrontCm, const FVector& FromCm, double StepCm)
{
    TArray<FVector> Candidates;
    if (FrontCm.ContainsNaN() || FromCm.ContainsNaN() || !(StepCm > 0)) { return Candidates; }
    const double Length = FVector::Dist2D(FrontCm, FromCm);
    const FVector Back = (FromCm - FrontCm).GetSafeNormal2D();
    for (double Along = 0; Along < Length; Along += StepCm) { Candidates.Add(FrontCm + Back * Along); }
    Candidates.Add(FromCm);
    return Candidates;
}

bool LDOrderRules::ShouldRepeatHeldMove(double NowSeconds, double LastSentSeconds, const FVector& LastGroundCm,
    const FVector& GroundCm)
{
    if (!FMath::IsFinite(NowSeconds) || !FMath::IsFinite(LastSentSeconds) || GroundCm.ContainsNaN() || LastGroundCm.ContainsNaN())
    { return false; }
    return NowSeconds-LastSentSeconds>=HeldMoveRepeatSeconds
        && FVector::DistSquared2D(GroundCm,LastGroundCm)>=FMath::Square(HeldMoveShiftCm);
}