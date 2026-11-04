#include "Combat/LDControlRules.h"
#include <limits>

namespace
{
    double ControlRational(const FLDDemoRational& Value)
    { return Value.Denominator>0 ? double(Value.Numerator)/double(Value.Denominator) : std::numeric_limits<double>::quiet_NaN(); }
}

bool LDControl::IsTaunted(const TOptional<FLDCombatTaunt>& Taunt, int64 AtMs)
{ return Taunt.IsSet() && AtMs<Taunt->ExpiresAtMs; }

bool LDControl::TauntAllows(const TOptional<FLDCombatTaunt>& Taunt, int64 AtMs, bool bBasicAttack, const FLDCombatBodyRef& Target)
{
    if (!IsTaunted(Taunt,AtMs)) { return true; }
    return bBasicAttack && Target.IsValid() && Target==Taunt->SourceBody;
}

bool LDControl::ApplyTaunt(TOptional<FLDCombatTaunt>& Taunt, const FLDCombatTaunt& Candidate, int64 AtMs)
{
    if (!Candidate.EffectId.IsValid() || !Candidate.SourceBody.IsValid() || Candidate.ExpiresAtMs<=AtMs) { return false; }
    if (Taunt.IsSet() && Taunt->EffectId==Candidate.EffectId) { return false; }
    Taunt=Candidate;
    return true;
}

bool LDControl::TauntHolds(const FLDCombatTaunt& Taunt, int64 AtMs, bool bTaunterAlive, bool bVictimObservesTaunter)
{ return AtMs<Taunt.ExpiresAtMs && bTaunterAlive && bVictimObservesTaunter; }

TArray<int32> LDControl::SelectTauntTargets(const FGuid& Caster, const FVector& CasterFootCm, double RadiusCm,
    const TArray<FLDTauntCandidate>& Candidates)
{
    TArray<int32> Out;
    if (!(RadiusCm>0) || !FMath::IsFinite(RadiusCm) || CasterFootCm.ContainsNaN()) { return Out; }
    const double ReachSquared=RadiusCm*RadiusCm;
    for (int32 Index=0; Index<Candidates.Num(); ++Index)
    {
        const FLDTauntCandidate& Candidate=Candidates[Index];
        if (Candidate.Body.Victim.Kind!=ELDVictimKind::Contender || !Candidate.Body.IsValid()
            || Candidate.Body.Victim.EntityId==Caster || Candidate.bPendingDeath || Candidate.bProtected
            || !Candidate.bSeesCaster || Candidate.FootCm.ContainsNaN()
            || FVector::DistSquared2D(CasterFootCm,Candidate.FootCm)>ReachSquared)
        { continue; }
        Out.Add(Index);
    }
    Out.Sort([&Candidates](int32 A, int32 B) { return Candidates[A].Body.Victim.EntityId<Candidates[B].Body.Victim.EntityId; });
    return Out;
}

bool LDControl::IsStealthed(const TOptional<FLDCombatStealth>& Stealth, int64 AtMs)
{ return Stealth.IsSet() && AtMs<Stealth->ExpiresAtMs; }

double LDControl::ConcealBeyondCm(const TOptional<FLDCombatStealth>& Stealth, int64 AtMs)
{ return IsStealthed(Stealth,AtMs) ? Stealth->RevealRadiusCm : 0.; }

bool LDControl::ApplyStealth(TOptional<FLDCombatStealth>& Stealth, const FLDCombatStealth& Candidate, int64 AtMs)
{
    if (!Candidate.EffectId.IsValid() || Candidate.ExpiresAtMs<=AtMs || !(Candidate.RevealRadiusCm>0)
        || !FMath::IsFinite(Candidate.RevealRadiusCm) || Candidate.MoveSpeedPenaltyBp<0 || Candidate.MoveSpeedPenaltyBp>=10000)
    { return false; }
    Stealth=Candidate;
    return true;
}

bool LDControl::Reveal(TOptional<FLDCombatStealth>& Stealth)
{
    if (!Stealth.IsSet()) { return false; }
    Stealth.Reset();
    return true;
}

FLDStatusExpiry LDControl::ExpireControlStatuses(TOptional<FLDCombatTaunt>& Taunt, TOptional<FLDCombatStealth>& Stealth, int64 AtMs)
{
    FLDStatusExpiry Out;
    if (Taunt.IsSet() && AtMs>=Taunt->ExpiresAtMs) { Taunt.Reset(); Out.bTauntChanged=true; }
    if (Stealth.IsSet() && AtMs>=Stealth->ExpiresAtMs) { Stealth.Reset(); Out.bStealthChanged=true; }
    return Out;
}

bool LDControl::MoveSpeedCmPerSec(const FLDAttributeContributions& Row, int64 PenaltyBp, const FLDDemoRationalBounds& Bounds,
    double& OutCmPerSec)
{
    OutCmPerSec=0;
    if (PenaltyBp<0 || PenaltyBp>=10000) { return false; }
    if (PenaltyBp==0)
    {
        OutCmPerSec=ControlRational(Row.Final);
        return FMath::IsFinite(OutCmPerSec) && OutCmPerSec>0;
    }
    // The same FORMULA.MoveSpeed rows as the projection, temporary terms (timed boosts, the strongest slow) included,
    // with the stealth penalty as one more additive ratio.
    const double Flat=ControlRational(Row.Base)+ControlRational(Row.LevelGrowth.Flat)+ControlRational(Row.PermanentChoices.Flat)
        +ControlRational(Row.Passives.Flat)+ControlRational(Row.Equipment.Flat)+ControlRational(Row.Temporary.Flat);
    const double Ratio=ControlRational(Row.LevelGrowth.AdditiveRatio)+ControlRational(Row.PermanentChoices.AdditiveRatio)
        +ControlRational(Row.Passives.AdditiveRatio)+ControlRational(Row.Equipment.AdditiveRatio)
        +ControlRational(Row.Temporary.AdditiveRatio)-double(PenaltyBp)/10000.;
    const double Min=ControlRational(Bounds.Min), Max=ControlRational(Bounds.Max);
    const double Speed=Flat*(1.+Ratio);
    if (!FMath::IsFinite(Speed) || !FMath::IsFinite(Min) || !FMath::IsFinite(Max) || !(Min>0) || Max<Min) { return false; }
    OutCmPerSec=FMath::Clamp(Speed,Min,Max);
    return true;
}

bool LDControl::TauntBlocks(const TOptional<FLDCombatTaunt>& Taunt, int64 AtMs, ELDCombatControlAction Action)
{
    return IsTaunted(Taunt, AtMs) && (Action == ELDCombatControlAction::Cast || Action == ELDCombatControlAction::Gear
        || Action == ELDCombatControlAction::Displacement);
}

bool LDControl::TauntVictim(TOptional<FLDCombatTaunt>& Taunt, TOptional<FLDCombatStealth>& Stealth,
    const FLDCombatTaunt& Candidate, int64 AtMs, bool& bRevealed)
{
    bRevealed=false;
    if (!ApplyTaunt(Taunt,Candidate,AtMs)) { return false; }
    bRevealed=Reveal(Stealth);
    return true;
}
