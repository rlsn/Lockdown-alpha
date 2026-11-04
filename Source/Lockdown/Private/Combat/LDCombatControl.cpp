#include "Combat/LDCombatDirector.h"
#include "Combat/LDControlRules.h"
#include "Character/LDCharacter.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDGameMode.h"
#include "Region/LDMapRegistry.h"
#include "Vision/LDVisionDirector.h"
#include "Engine/World.h"

// Taunt and stealth (CONTENT_AND_BALANCE 15.4/15.5) on Combat's body records; the rules live in LDControlRules.
namespace LDCombatControlDetail
{
    double Cm(const FLDDemoRational& Value)
    { return Value.Denominator>0 ? double(Value.Numerator)/double(Value.Denominator) : 0.; }

    // Observation of Target from SourceFoot under the same sight and stealth rule as the observation owner.
    bool Observes(const ALDGameMode& Mode, const UWorld& World, const FVector& SourceFoot, const ALDCharacter& Target,
        bool& bFault)
    {
        bFault=false;
        const auto* Registry=World.GetSubsystem<ULDMapRegistry>();
        if (!Mode.Vision || !Registry) { bFault=true; return false; }
        const ELDSightResult Sight=Mode.Vision->TraceObservation(SourceFoot,Target,Registry->Revision());
        if (Sight==ELDSightResult::InvalidPoint || Sight==ELDSightResult::StaleWorld) { bFault=true; return false; }
        return Sight==ELDSightResult::Visible;
    }
}

bool ALDCombatDirector::ReadTaunter(const FLDCombatBodyRef& Victim, FLDCombatBodyRef& OutTaunter) const
{
    OutTaunter={};
    const auto* Mode=Core();
    const auto* Body=FindBody(Victim);
    if (!Mode || !Body || Body->bPendingDeath || !LDControl::IsTaunted(Body->Taunt,Mode->GetAuthorityNowMs())) { return false; }
    OutTaunter=Body->Taunt->SourceBody;
    return true;
}

double ALDCombatDirector::ConcealBeyondCm(const ALDCharacter& Actor) const
{
    const auto* Mode=Core();
    const auto* Body=StateFor(&Actor);
    return Mode && Body && !Body->bPendingDeath ? LDControl::ConcealBeyondCm(Body->Stealth,Mode->GetAuthorityNowMs()) : 0.;
}

void ALDCombatDirector::RevealStealth(FLDBodyCombatState& Body)
{
    if (!LDControl::Reveal(Body.Stealth)) { return; }
    ++Body.StateRevision;
    MarkViewChanged(Body.Victim.EntityId);
}

bool ALDCombatDirector::EffectiveMoveSpeed(const FLDBodyCombatState& Body, int64 NowMs, double& OutCmPerSec) const
{
    OutCmPerSec=0;
    const int64 Penalty=LDControl::IsStealthed(Body.Stealth,NowMs) ? Body.Stealth->MoveSpeedPenaltyBp : 0;
    if (Penalty==0)
    {
        OutCmPerSec=LDCombatControlDetail::Cm(Body.AttributeProjection.Final.MoveSpeedCmPerSec);
        return FMath::IsFinite(OutCmPerSec) && OutCmPerSec>0;
    }
    const auto* Content=Catalog();
    const int32 Row=int32(ELDProjectedAttribute::MoveSpeed);
    return Content && Body.AttributeProjection.Contributions.IsValidIndex(Row)
        && LDControl::MoveSpeedCmPerSec(Body.AttributeProjection.Contributions[Row],Penalty,
            Content->Get().Parameters.MoveSpeedCmPerSec,OutCmPerSec);
}

bool ALDCombatDirector::RefreshTaunts(FString& Error)
{
    auto* Mode=Core();
    if (!Mode) { Error=TEXT("Taunts require the current core."); return false; }
    const int64 Now=Mode->GetAuthorityNowMs();
    for (auto& Pair : Bodies)
    {
        FLDBodyCombatState& Body=Pair.Value;
        const ALDCharacter* Victim=Pair.Key.Get();
        if (!Body.Taunt.IsSet()) { continue; }
        ALDCharacter* Taunter=nullptr;
        const FLDBodyCombatState* Source=FindBody(Body.Taunt->SourceBody,&Taunter);
        const bool bAlive=Victim && !Body.bPendingDeath && Source && Taunter && !Source->bPendingDeath
            && Mode->IsMemberPresent(Body.Taunt->SourceEntity);
        bool bObserved=false, bFault=false;
        if (bAlive) { bObserved=LDCombatControlDetail::Observes(*Mode,*GetWorld(),FootOf(*Victim),*Taunter,bFault); }
        if (bFault) { Error=TEXT("Taunt observation failed on a registered body."); return false; }
        if (LDControl::TauntHolds(Body.Taunt.GetValue(),Now,bAlive,bObserved)) { continue; }
        UE_LOG(LogTemp,Display,TEXT("LD_COMBAT_TAUNT_END victim=%s alive=%d observed=%d"),
            *Body.Victim.EntityId.ToString(),int32(bAlive),int32(bObserved));
        Body.Taunt.Reset();
        ++Body.StateRevision;
        MarkViewChanged(Body.Victim.EntityId);
    }
    return true;
}

bool ALDCombatDirector::ApplyTauntEffect(ALDCharacter& Actor, FLDBodyCombatState& Body, const FLDDemoEffectDefinition& Effect,
    const FGuid& EffectId, FString& Error)
{
    auto* Mode=Core();
    if (!Mode) { Error=TEXT("A taunt requires the current core."); return false; }
    const int64 Now=Mode->GetAuthorityNowMs();
    const FVector CasterFoot=FootOf(Actor);
    TArray<FLDTauntCandidate> Candidates;
    TArray<FLDBodyCombatState*> States;
    for (auto& Pair : Bodies)
    {
        ALDCharacter* Other=Pair.Key.Get();
        FLDBodyCombatState& State=Pair.Value;
        if (!Other || &State==&Body || State.Victim.Kind!=ELDVictimKind::Contender) { continue; }
        FLDTauntCandidate& Candidate=Candidates.AddDefaulted_GetRef();
        Candidate.Body={State.Victim,State.Identity};
        Candidate.FootCm=FootOf(*Other);
        Candidate.bPendingDeath=State.bPendingDeath;
        Candidate.bProtected=Mode->IsContenderProtected(State.Victim.EntityId);
        bool bFault=false;
        Candidate.bSeesCaster=LDCombatControlDetail::Observes(*Mode,*GetWorld(),Candidate.FootCm,Actor,bFault);
        if (bFault) { Error=TEXT("Taunt observation failed on a registered body."); return false; }
        States.Add(&State);
    }
    FLDCombatTaunt Taunt;
    Taunt.EffectId=EffectId;
    Taunt.SourceEntity=Body.Victim.EntityId;
    Taunt.SourceBody={Body.Victim,Body.Identity};
    if (!LDMatchLifecycle::AddDurationMs(Now,Effect.DurationMs,Taunt.ExpiresAtMs,Error)) { return false; }
    for (const int32 Index : LDControl::SelectTauntTargets(Body.Victim.EntityId,CasterFoot,LDCombatControlDetail::Cm(Effect.RadiusCm),Candidates))
    {
        FLDBodyCombatState& Victim=*States[Index];
        bool bRevealed=false;
        if (!LDControl::TauntVictim(Victim.Taunt,Victim.Stealth,Taunt,Now,bRevealed)) { continue; }
        // Unreleased work the taunt now forbids ends without cost; a basic swing at the taunter continues.
        if (Victim.Action.IsSet() && !LDControl::TauntAllows(Victim.Taunt,Now,
            Victim.Action->Kind==ELDCombatActionKind::Basic,Victim.Action->TargetBody))
        { AbandonAction(Victim,ELDCombatReject::ControlBlocked); }
        ++Victim.StateRevision;
        MarkViewChanged(Victim.Victim.EntityId);
        UE_LOG(LogTemp,Display,TEXT("LD_COMBAT_TAUNT victim=%s source=%s until=%lld"),
            *Victim.Victim.EntityId.ToString(),*Body.Victim.EntityId.ToString(),Taunt.ExpiresAtMs);
    }
    return true;
}
