#include "Combat/LDMomentum.h"

#include "Combat/LDTacticalRules.h"

namespace LDMomentumDetail
{
    // Rises from InOutP2 toward Ceiling at Rate over ElapsedMs, carrying a nonnegative remainder.
    bool Rise(const FLDDemoRational& Rate, int64 ElapsedMs, int64 Ceiling, int64& InOutP2, int64& InOutRemainder,
        int64& OutChange, FString& Error)
    {
        int64 Remainder = FMath::Max<int64>(InOutRemainder, 0);
        const int64 Before = InOutP2;
        if (!LDTactical::IntegrateRegen(Rate, ElapsedMs, Ceiling, InOutP2, Remainder, Error)) { return false; }
        InOutRemainder = Remainder;
        OutChange += InOutP2 - Before;
        return true;
    }

    // Falls from InOutP2 toward Floor at Rate over ElapsedMs, carrying the remainder as a negative number.
    bool Fall(const FLDDemoRational& Rate, int64 ElapsedMs, int64 Floor, int64& InOutP2, int64& InOutRemainder,
        int64& OutChange, FString& Error)
    {
        int64 Fallen = 0;
        int64 Remainder = InOutRemainder < 0 ? -InOutRemainder : 0;
        if (!LDTactical::IntegrateRegen(Rate, ElapsedMs, InOutP2 - Floor, Fallen, Remainder, Error)) { return false; }
        InOutP2 -= Fallen;
        InOutRemainder = -Remainder;
        OutChange -= Fallen;
        return true;
    }

    bool Idle(const FLDDemoMomentumParameters& P, int64 ElapsedMs, int64& InOutP2, int64& InOutRemainder, int64& OutChange,
        FString& Error)
    {
        if (ElapsedMs <= 0) { return true; }
        if (InOutP2 < P.StartP2) { return Rise(P.IdleReturnPerSec, ElapsedMs, P.StartP2, InOutP2, InOutRemainder, OutChange, Error); }
        if (InOutP2 > P.StartP2) { return Fall(P.IdleReturnPerSec, ElapsedMs, P.StartP2, InOutP2, InOutRemainder, OutChange, Error); }
        InOutRemainder = 0;
        return true;
    }
}

bool LDMomentum::InCombat(const FLDDemoMomentumParameters& P, const TOptional<int64>& EngagedAtMs, int64 NowMs)
{
    return EngagedAtMs.IsSet() && P.IdleDelayMs >= 0 && EngagedAtMs.GetValue() <= NowMs
        && EngagedAtMs.GetValue() <= MAX_int64 - P.IdleDelayMs && NowMs < EngagedAtMs.GetValue() + P.IdleDelayMs;
}

bool LDMomentum::Settle(const FLDDemoMomentumParameters& P, const FLDDemoRational& CombatPerSec,
    const TOptional<int64>& EngagedAtMs, int64 FromMs, int64 NowMs, int64& InOutP2, int64& InOutRemainderMicroP2,
    int64& OutCombatP2, int64& OutIdleP2, FString& Error)
{
    using namespace LDMomentumDetail;
    OutCombatP2 = OutIdleP2 = 0;
    if (NowMs < FromMs)
    {
        Error = TEXT("Momentum cannot settle a reversed interval.");
        return false;
    }
    if (P.MaxP2 <= 0 || P.StartP2 < 0 || P.StartP2 > P.MaxP2 || InOutP2 < 0 || InOutP2 > P.MaxP2 || P.IdleDelayMs < 0)
    {
        Error = TEXT("Momentum is outside its numeric domain.");
        return false;
    }
    int64 CombatStart = NowMs, CombatEnd = NowMs;
    if (EngagedAtMs.IsSet())
    {
        if (EngagedAtMs.GetValue() > MAX_int64 - P.IdleDelayMs)
        {
            Error = TEXT("The combat window of momentum has no representable end.");
            return false;
        }
        CombatStart = FMath::Clamp(EngagedAtMs.GetValue(), FromMs, NowMs);
        CombatEnd = FMath::Clamp(EngagedAtMs.GetValue() + P.IdleDelayMs, CombatStart, NowMs);
    }
    if (!Idle(P, CombatStart - FromMs, InOutP2, InOutRemainderMicroP2, OutIdleP2, Error)) { return false; }
    if (CombatEnd > CombatStart
        && !Rise(CombatPerSec, CombatEnd - CombatStart, P.MaxP2, InOutP2, InOutRemainderMicroP2, OutCombatP2, Error))
    {
        return false;
    }
    if (!Idle(P, NowMs - CombatEnd, InOutP2, InOutRemainderMicroP2, OutIdleP2, Error)) { return false; }
    Error.Reset();
    return true;
}

int64 LDMomentum::BasicGainP2(const FLDDemoMomentumParameters& P, int64 IntervalMs)
{
    if (IntervalMs <= 0 || P.BasicPerRefP2 <= 0 || P.BasicRefIntervalMs <= 0 || IntervalMs > MAX_int64 / P.BasicPerRefP2)
    {
        return 0;
    }
    return P.BasicPerRefP2 * IntervalMs / P.BasicRefIntervalMs;
}

int64 LDMomentum::Apply(const FLDDemoMomentumParameters& P, int64 CurrentP2, int64 DeltaP2, int64& OutAppliedP2)
{
    const int64 Current = FMath::Clamp<int64>(CurrentP2, 0, P.MaxP2);
    const int64 Next = DeltaP2 >= 0 ? (DeltaP2 >= P.MaxP2 - Current ? P.MaxP2 : Current + DeltaP2)
        : (DeltaP2 <= -Current ? 0 : Current + DeltaP2);
    OutAppliedP2 = Next - Current;
    return Next;
}

uint8 LDMomentum::NextComboStep(uint8 Step, int64 PreviousStartMs, int64 NowMs, int64 ResetMs)
{
    if (Step < 1 || Step > 3 || NowMs < PreviousStartMs || NowMs - PreviousStartMs > ResetMs) { return 1; }
    return uint8(Step % 3 + 1);
}

int64 LDMomentum::ComboRatioBp(const FLDDemoComboParameters& P, uint8 Step)
{
    return Step >= 1 && Step <= P.StepRatiosBp.Num() ? P.StepRatiosBp[Step - 1] : 10000;
}

bool LDMomentum::OverdriveReady(const FLDDemoOverdriveParameters& P, int64 MomentumP2)
{
    return P.ThresholdP2 > 0 && MomentumP2 >= P.ThresholdP2;
}

bool LDMomentum::IsPerfectDodge(int64 WindowMs, int64 DashStartMs, int64 ResolveMs, bool bStartInside, bool bResolvedInside)
{
    return WindowMs > 0 && DashStartMs <= ResolveMs && ResolveMs - DashStartMs <= WindowMs && bStartInside && !bResolvedInside;
}
