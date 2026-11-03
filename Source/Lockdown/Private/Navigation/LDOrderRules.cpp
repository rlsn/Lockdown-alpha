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

bool LDOrderRules::ShouldRepeatHeldMove(double NowSeconds, double LastSentSeconds, const FVector& LastGroundCm,
    const FVector& GroundCm)
{
    if (!FMath::IsFinite(NowSeconds) || !FMath::IsFinite(LastSentSeconds) || GroundCm.ContainsNaN() || LastGroundCm.ContainsNaN())
    { return false; }
    return NowSeconds-LastSentSeconds>=HeldMoveRepeatSeconds
        && FVector::DistSquared2D(GroundCm,LastGroundCm)>=FMath::Square(HeldMoveShiftCm);
}