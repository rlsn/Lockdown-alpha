#include "Combat/LDCombatMechanics.h"
#include "Combat/LDTacticalRules.h"

namespace LDCombatMechanics
{
int64 StrongestHealCutBp(const TArray<FLDCombatHealCut>& Cuts, int64 AtMs)
{
    int64 Best = 0;
    for (const FLDCombatHealCut& Cut : Cuts)
    {
        if (Cut.ExpiresAtMs > AtMs) { Best = FMath::Max(Best, Cut.RatioBp); }
    }
    return FMath::Clamp<int64>(Best, 0, 10000);
}

bool ApplyHealCut(TArray<FLDCombatHealCut>& Cuts, const FLDCombatHealCut& Candidate, int64 AtMs)
{
    if (!Candidate.SourceEntity.IsValid() || Candidate.RatioBp <= 0 || Candidate.RatioBp > 10000 || Candidate.ExpiresAtMs <= AtMs)
    {
        return false;
    }
    Cuts.RemoveAll([&Candidate, AtMs](const FLDCombatHealCut& Cut)
    { return Cut.SourceEntity == Candidate.SourceEntity || Cut.ExpiresAtMs <= AtMs; });
    Cuts.Add(Candidate);
    return true;
}

bool ExpireHealCuts(TArray<FLDCombatHealCut>& Cuts, int64 AtMs)
{
    return Cuts.RemoveAll([AtMs](const FLDCombatHealCut& Cut) { return Cut.ExpiresAtMs <= AtMs; }) > 0;
}

namespace
{
bool VulnerableLive(const FLDCombatVulnerable& Row, const TArray<FLDCombatRoot>& Roots, int64 AtMs)
{
    return Row.ExpiresAtMs > AtMs && Roots.ContainsByPredicate([&Row, AtMs](const FLDCombatRoot& Root)
    { return Root.EffectId == Row.EffectId && Root.ExpiresAtMs > AtMs; });
}
}

int64 VulnerableBp(const TArray<FLDCombatVulnerable>& Rows, const TArray<FLDCombatRoot>& Roots, int64 AtMs)
{
    int64 Best = 0;
    for (const FLDCombatVulnerable& Row : Rows) { if (VulnerableLive(Row, Roots, AtMs)) { Best = FMath::Max(Best, Row.RatioBp); } }
    return Best;
}

int64 VulnerableUntilMs(const TArray<FLDCombatVulnerable>& Rows, const TArray<FLDCombatRoot>& Roots, int64 AtMs)
{
    int64 Until = 0;
    for (const FLDCombatVulnerable& Row : Rows) { if (VulnerableLive(Row, Roots, AtMs)) { Until = FMath::Max(Until, Row.ExpiresAtMs); } }
    return Until;
}

bool PruneVulnerabilities(TArray<FLDCombatVulnerable>& Rows, const TArray<FLDCombatRoot>& Roots, int64 AtMs)
{
    return Rows.RemoveAll([&Roots, AtMs](const FLDCombatVulnerable& Row) { return !VulnerableLive(Row, Roots, AtMs); }) > 0;
}

int64 ReduceByBp(int64 AmountP2, int64 CutBp)
{
    if (AmountP2 <= 0) { return 0; }
    const int64 Keep = 10000 - FMath::Clamp<int64>(CutBp, 0, 10000);
    // Split to stay inside int64 for any P2 amount.
    return AmountP2 / 10000 * Keep + AmountP2 % 10000 * Keep / 10000;
}

ELDShieldGrant GrantShield(TOptional<FLDCombatShield>& Shield, int64& ShieldP2, const FLDCombatShield& Candidate,
    int64 AmountP2, int64 AtMs)
{
    const bool bHeld = Shield.IsSet() && Shield->ExpiresAtMs > AtMs && ShieldP2 > 0;
    if (!LDTactical::ApplyShield(Shield, ShieldP2, Candidate, AmountP2, AtMs)) { return ELDShieldGrant::Rejected; }
    return bHeld ? ELDShieldGrant::Refreshed : ELDShieldGrant::Gained;
}

bool ShieldBroken(int64 BeforeP2, int64 AfterP2, int64 AbsorbedP2)
{
    return BeforeP2 > 0 && AfterP2 <= 0 && AbsorbedP2 > 0;
}

bool KnockbackDirection(const FVector& SourceFoot, const FVector& TargetFoot, double SourceYawDeg, FVector& OutDirection)
{
    OutDirection = FVector::ZeroVector;
    if (SourceFoot.ContainsNaN() || TargetFoot.ContainsNaN() || !FMath::IsFinite(SourceYawDeg)) { return false; }
    const FVector Planar(TargetFoot.X - SourceFoot.X, TargetFoot.Y - SourceFoot.Y, 0.);
    if (Planar.SizeSquared() > FMath::Square(UE_KINDA_SMALL_NUMBER))
    {
        OutDirection = Planar.GetSafeNormal();
        return true;
    }
    const double Radians = FMath::DegreesToRadians(SourceYawDeg);
    OutDirection = FVector(FMath::Cos(Radians), FMath::Sin(Radians), 0.);
    return true;
}

int32 CountNegativeKinds(const FLDNegativeKinds& Kinds)
{
    return int32(Kinds.bRoot) + int32(Kinds.bSlow) + int32(Kinds.bDisarm) + int32(Kinds.bStun) + int32(Kinds.bTaunt)
        + int32(Kinds.bBreach);
}

int64 NegativeKindsAmplifyBp(int32 Kinds, int64 PerKindBp)
{
    return 10000 + FMath::Clamp(Kinds, 0, 6) * FMath::Max<int64>(PerKindBp, 0);
}

int64 NextPulseAtMs(int64 StartMs, int64 DelayMs, int64 IntervalMs, int32 Count, int64 FromMs)
{
    if (DelayMs < 0 || IntervalMs <= 0 || Count <= 0 || StartMs < 0
        || StartMs > MAX_int64 - DelayMs - IntervalMs * int64(Count - 1)) { return 0; }
    const int64 First = StartMs + DelayMs;
    if (FromMs <= First) { return First; }
    const int64 Steps = (FromMs - First + IntervalMs - 1) / IntervalMs;
    return Steps < Count ? First + Steps * IntervalMs : 0;
}
bool InsideZone(const FVector& CentreCm, double RadiusCm, const FVector& FootCm)
{
    return FMath::IsFinite(RadiusCm) && RadiusCm >= 0 && !CentreCm.ContainsNaN() && !FootCm.ContainsNaN()
        && FVector::DistSquared2D(CentreCm, FootCm) <= RadiusCm * RadiusCm;
}

int64 ShieldBurstBaseP2(int64 BaseP2, int64 ShieldP2, int64 RatioBp, int64 CapP2)
{
    const int64 Counted = FMath::Clamp<int64>(ShieldP2, 0, FMath::Max<int64>(CapP2, 0));
    const int64 Scaled = Counted / 10000 * RatioBp + Counted % 10000 * RatioBp / 10000;
    return FMath::Max<int64>(BaseP2, 0) + Scaled;
}

bool GuardTriggers(int64 TakenBeforeP2, int64 HitPreMitigationP2, int64 ThresholdP2, int64& OutTakenP2)
{
    OutTakenP2 = TakenBeforeP2 + FMath::Max<int64>(HitPreMitigationP2, 0);
    return TakenBeforeP2 <= ThresholdP2 && OutTakenP2 > ThresholdP2;
}
}
