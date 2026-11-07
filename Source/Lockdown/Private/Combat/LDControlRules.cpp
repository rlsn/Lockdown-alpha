#include "Combat/LDControlRules.h"

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

FLDStatusExpiry LDControl::ExpireControlStatuses(TOptional<FLDCombatTaunt>& Taunt, int64 AtMs)
{
    FLDStatusExpiry Out;
    if (Taunt.IsSet() && AtMs>=Taunt->ExpiresAtMs) { Taunt.Reset(); Out.bTauntChanged=true; }
    return Out;
}

bool LDControl::TauntBlocks(const TOptional<FLDCombatTaunt>& Taunt, int64 AtMs, ELDCombatControlAction Action)
{
    return IsTaunted(Taunt, AtMs) && (Action == ELDCombatControlAction::Cast || Action == ELDCombatControlAction::Displacement);
}
