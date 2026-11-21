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

bool LDOrderRules::QueueHeadDue(bool bPending, bool bActive, bool bActionRunning)
{ return !bPending && !bActive && !bActionRunning; }

bool LDOrderRules::ShouldCoalesceClickMove(double NowSeconds, double LastSentSeconds, const FVector& LastGroundCm,
    const FVector& GroundCm, const FLDMoveIdentity& CurrentLocal, const FLDMoveIdentity& CachedMoveIdentity,
    int32 QueuedStepCount)
{
    if (!FMath::IsFinite(NowSeconds) || !FMath::IsFinite(LastSentSeconds) || LastSentSeconds <= 0.
        || LastGroundCm.ContainsNaN() || GroundCm.ContainsNaN()
        || CachedMoveIdentity.Order == 0 || !(CurrentLocal == CachedMoveIdentity) || QueuedStepCount != 0)
    {
        return false;
    }
    return NowSeconds >= LastSentSeconds
        && (NowSeconds - LastSentSeconds) < ClickMoveCoalesceSeconds
        && FVector::DistSquared2D(LastGroundCm, GroundCm) < FMath::Square(ClickMoveCoalesceCm);
}

bool LDOrderRules::IntersectElevationPlane(const FVector& RayOrigin, const FVector& RayDirection, double PlaneZ,
    const FVector& FallbackForward, const FVector& FallbackFeet, FVector& OutWorldPoint)
{
    OutWorldPoint = FVector::ZeroVector;
    if (!FMath::IsFinite(PlaneZ) || RayOrigin.ContainsNaN() || RayDirection.ContainsNaN()) { return false; }
    if (RayDirection.Z >= -0.001)
    {
        FVector Fwd(FallbackForward.X, FallbackForward.Y, 0.);
        Fwd = Fwd.GetSafeNormal();
        if (Fwd.IsZero()) { return false; }
        OutWorldPoint = FVector(FallbackFeet.X, FallbackFeet.Y, PlaneZ) + Fwd * 400.;
        OutWorldPoint.Z = PlaneZ;
        return true;
    }
    const double T = (PlaneZ - RayOrigin.Z) / RayDirection.Z;
    if (!FMath::IsFinite(T) || T < 0.) { return false; }
    OutWorldPoint = RayOrigin + RayDirection * T;
    OutWorldPoint.Z = PlaneZ;
    return !OutWorldPoint.ContainsNaN();
}

FVector LDOrderRules::ResolveElevationAimDirection(const FVector& CasterFeet, const FVector& AimWorldPoint,
    const FVector& CasterForward)
{
    const FVector Delta(AimWorldPoint.X - CasterFeet.X, AimWorldPoint.Y - CasterFeet.Y, 0.);
    if (!Delta.ContainsNaN() && Delta.SizeSquared() >= FMath::Square(AimDeadzoneCm))
    {
        return Delta.GetSafeNormal();
    }
    const FVector Fallback(CasterForward.X, CasterForward.Y, 0.);
    const FVector Norm = Fallback.GetSafeNormal();
    return Norm.IsZero() ? FVector(1., 0., 0.) : Norm;
}

bool LDOrderRules::ClipViewportSegment(FVector2D& InOutA, FVector2D& InOutB, const FVector2D& Min, const FVector2D& Max)
{
    if (InOutA.ContainsNaN() || InOutB.ContainsNaN() || Min.X >= Max.X || Min.Y >= Max.Y) { return false; }
    const double DX = InOutB.X - InOutA.X, DY = InOutB.Y - InOutA.Y;
    const double P[4] = {-DX, DX, -DY, DY};
    const double Q[4] = {InOutA.X - Min.X, Max.X - InOutA.X, InOutA.Y - Min.Y, Max.Y - InOutA.Y};
    double T0 = 0., T1 = 1.;
    for (int32 Edge = 0; Edge < 4; ++Edge)
    {
        if (FMath::Abs(P[Edge]) <= 1e-9)
        {
            if (Q[Edge] < 0.) { return false; }
        }
        else
        {
            const double R = Q[Edge] / P[Edge];
            if (P[Edge] < 0.)
            {
                if (R > T1) { return false; }
                T0 = FMath::Max(T0, R);
            }
            else
            {
                if (R < T0) { return false; }
                T1 = FMath::Min(T1, R);
            }
        }
    }
    const FVector2D A0 = InOutA;
    InOutA = A0 + FVector2D(DX, DY) * T0;
    InOutB = A0 + FVector2D(DX, DY) * T1;
    return true;
}

LDOrderRules::EQueueAfter LDOrderRules::QueueAfter(ELDOrderFailure Failure)
{
    switch (Failure)
    {
    case ELDOrderFailure::Stopped:
    case ELDOrderFailure::Dead:
    case ELDOrderFailure::MatchEnded:
    case ELDOrderFailure::StaleMatch:
    case ELDOrderFailure::StaleLife:
    case ELDOrderFailure::NotOwner:
    case ELDOrderFailure::Expired:
    case ELDOrderFailure::Taunted:
        return EQueueAfter::Clear;
    default:
        return EQueueAfter::Next;
    }
}

bool LDOrderRules::QueuedActionSettled(ELDCombatPhase Phase)
{ return Phase==ELDCombatPhase::Released || Phase==ELDCombatPhase::Finished || Phase==ELDCombatPhase::Rejected; }

bool LDOrderRules::QueuedAimDirection(const FVector& FootCm, const FVector& AimCm, FVector& OutDirection)
{
    OutDirection=FVector::ZeroVector;
    if (FootCm.ContainsNaN() || AimCm.ContainsNaN()) { return false; }
    const FVector Planar(AimCm.X-FootCm.X,AimCm.Y-FootCm.Y,0);
    if (Planar.SizeSquared()<1.) { return false; }
    OutDirection=Planar.GetSafeNormal();
    return !OutDirection.IsZero();
}

ELDSpatialOrderKind LDOrderRules::AttackConfirmKind(bool bUnitUnderPointer)
{ return bUnitUnderPointer ? ELDSpatialOrderKind::AttackUnit : ELDSpatialOrderKind::AttackMove; }

bool LDOrderRules::SkillRequestReady(ELDCombatReject Unavailable)
{ return Unavailable==ELDCombatReject::None || Unavailable==ELDCombatReject::Busy; }

LDOrderRules::EForcedOrder LDOrderRules::ForcedOrderStep(bool bTaunted, bool bActiveForced, bool bSameTaunter)
{
    if (!bTaunted) { return bActiveForced ? EForcedOrder::End : EForcedOrder::None; }
    return bActiveForced && bSameTaunter ? EForcedOrder::Keep : EForcedOrder::Begin;
}


FLDMoveIdentity LDOrderRules::PredictedMoveIdentity(const FLDMoveIdentity& Local, const FLDMoveIdentity& Active,
    const FLDMoveIdentity& Early, bool bLocalStop, const FLDMoveIdentity& Revoked)
{
    if (!bLocalStop && Early.Order != 0 && Early.Epoch == Local.Epoch) { return Early; }
    if (!bLocalStop && Active.Order != 0 && Active.Epoch == Local.Epoch) { return Active; }
    if (Local.Order != 0 && Local == Revoked) { return {Local.Match, Local.Life, 0, Local.Epoch}; }
    return Local;
}

bool LDOrderRules::ShouldRepeatHeldMove(double NowSeconds, double LastSentSeconds, const FVector& LastGroundCm,
    const FVector& GroundCm)
{
    if (!FMath::IsFinite(NowSeconds) || !FMath::IsFinite(LastSentSeconds) || GroundCm.ContainsNaN() || LastGroundCm.ContainsNaN())
    { return false; }
    return NowSeconds-LastSentSeconds>=HeldMoveRepeatSeconds
        && FVector::DistSquared2D(GroundCm,LastGroundCm)>=FMath::Square(HeldMoveShiftCm);
}

bool LDOrderRules::ActionHoldsChase(bool bActionInProgress, ELDCombatEntry Entry, int64 ReleaseAtMs, int64 NowMs)
{ return bActionInProgress && !(Entry==ELDCombatEntry::Basic && ReleaseAtMs<=NowMs); }

bool LDOrderRules::ChasePress(const FChasePressInput& Input)
{
    const FVector Shift(Input.TargetFootCm.X-Input.PreviousTargetFootCm.X,Input.TargetFootCm.Y-Input.PreviousTargetFootCm.Y,0);
    if (!Input.bInRange || Input.bActing || !Input.bAttackAllowed || Input.bBeatReady || Input.bCloseIn
        || Shift.ContainsNaN() || Input.AttackerFootCm.ContainsNaN() || Shift.SizeSquared()>FMath::Square(PressSampleLimitCm))
    { return false; }
    const double Retreat=FVector::DotProduct(Shift,(Input.TargetFootCm-Input.AttackerFootCm).GetSafeNormal2D());
    if (Retreat>=PressShiftCm) { return true; }
    if (Retreat<=-PressShiftCm) { return false; }
    return Input.bWasPressing;
}
bool LDOrderRules::CastFollowUpTarget(const FCastFollowUpInput& Input, FGuid& OutTarget, uint32& OutEpoch)
{
    OutTarget.Invalidate();
    OutEpoch=0;
    if (!Input.bEnabled || Input.bQueued) { return false; }
    if (Input.CastKind==ELDSpatialOrderKind::CastUnit) { OutTarget=Input.CastTarget; OutEpoch=Input.CastEpoch; }
    else if (Input.CastKind==ELDSpatialOrderKind::CastPoint && Input.bPriorAttackCurrent)
    { OutTarget=Input.PriorTarget; OutEpoch=Input.PriorEpoch; }
    if (!OutTarget.IsValid()) { OutEpoch=0; return false; }
    return true;
}

LDOrderRules::ECastFollowUp LDOrderRules::CastFollowUpStep(ELDOrderStatus Status, ELDOrderFailure Failure,
    bool bNewestOwnOrder, bool bTargetVisible)
{
    if (!bNewestOwnOrder) { return ECastFollowUp::Drop; }
    switch (Status)
    {
    case ELDOrderStatus::Queued:
    case ELDOrderStatus::Activated:
    case ELDOrderStatus::Progress: return ECastFollowUp::Wait;
    case ELDOrderStatus::Completed:
        return Failure==ELDOrderFailure::None && bTargetVisible ? ECastFollowUp::Attack : ECastFollowUp::Drop;
    default: return ECastFollowUp::Drop;
    }
}