#include "Combat/LDCombatPreInput.h"

bool LDCombatPreInput::IsDirectEntry(ELDCombatEntry Entry)
{
    return Entry == ELDCombatEntry::Q || Entry == ELDCombatEntry::W || Entry == ELDCombatEntry::E || Entry == ELDCombatEntry::Dash;
}

bool LDCombatPreInput::ServerHolds(ELDCombatEntry Entry, bool bOwnAbilityWindup, int64 ReleaseAtMs, int64 NowMs)
{
    return IsDirectEntry(Entry) && bOwnAbilityWindup && ReleaseAtMs >= NowMs && ReleaseAtMs - NowMs <= HoldWindowMs;
}

bool LDCombatPreInput::FiresOnPress(ELDCombatEntry Entry)
{
    return Entry == ELDCombatEntry::Dash;
}

bool LDCombatPreInput::IsWindingUp(const FLDCombatOwnerView& View, int64 NowMs)
{
    if (!View.bHasBody || View.ActionPhase != ELDCombatPhase::Accepted || !IsDirectEntry(View.ActionEntry)) { return false; }
    return NowMs <= 0 || NowMs < View.ActionReleaseAtMs;
}

void LDCombatPreInput::EnqueueFIFO(TArray<FPendingCombatIntent>& Queue, FPendingCombatIntent&& Intent,
    TOptional<FPendingCombatIntent>& OutEvicted)
{
    OutEvicted.Reset();
    int32 InsertIndex = Queue.Num();
    if (Intent.Action.DirectSequence != 0)
    {
        while (InsertIndex > 0 && Queue[InsertIndex - 1].Action.DirectSequence != 0
            && Queue[InsertIndex - 1].Action.DirectSequence > Intent.Action.DirectSequence)
        {
            --InsertIndex;
        }
    }
    Queue.Insert(MoveTemp(Intent), InsertIndex);
    if (Queue.Num() > MaxBufferedSteps)
    {
        OutEvicted = MoveTemp(Queue[0]);
        Queue.RemoveAt(0);
    }
}

bool LDCombatPreInput::ShouldClearForHardControl(int64 ControlExpiresAtMs, int64 WindupReleaseAtMs)
{
    return ControlExpiresAtMs > WindupReleaseAtMs && ControlExpiresAtMs - WindupReleaseAtMs > HoldWindowMs;
}

double LDCombatPreInput::ClampInnateDashDistance(double PlannedDistanceCm, const FVector& StartFootCm,
    const FVector& Direction, const FVector& EnemyCenterCm, bool bHasPassThroughCard)
{
    if (bHasPassThroughCard || !(PlannedDistanceCm > 0.0) || !FMath::IsFinite(PlannedDistanceCm)
        || StartFootCm.ContainsNaN() || Direction.ContainsNaN() || EnemyCenterCm.ContainsNaN())
    {
        return PlannedDistanceCm;
    }
    const FVector Heading = FVector(Direction.X, Direction.Y, 0.0).GetSafeNormal();
    const FVector ToEnemy(EnemyCenterCm.X - StartFootCm.X, EnemyCenterCm.Y - StartFootCm.Y, 0.0);
    const double DistSq = ToEnemy.SizeSquared2D();
    if (Heading.IsNearlyZero() || DistSq <= UE_DOUBLE_SMALL_NUMBER || DistSq > FMath::Square(CloseDashConeCm))
    {
        return PlannedDistanceCm;
    }
    const double Dist = FMath::Sqrt(DistSq);
    if (FVector::DotProduct(ToEnemy / Dist, Heading) < CloseDashCosHalfAngle)
    {
        return PlannedDistanceCm;
    }
    const double Along = FVector::DotProduct(ToEnemy, Heading);
    const double PerpSq = FMath::Max(0.0, DistSq - Along * Along);
    if (PerpSq >= FMath::Square(CloseDashStopCm))
    {
        return PlannedDistanceCm;
    }
    const double StopOffset = Along - FMath::Sqrt(FMath::Square(CloseDashStopCm) - PerpSq);
    if (StopOffset <= 0.0)
    {
        return FMath::Min(PlannedDistanceCm, FMath::Max(1.0, Along * 0.25));
    }
    return FMath::Min(PlannedDistanceCm, StopOffset);
}
