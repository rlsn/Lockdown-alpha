#include "Combat/LDCombatDirector.h"
#include "Combat/LDControlRules.h"
#include "Combat/LDCombatSourceRules.h"
#include "Combat/LDTacticalRules.h"
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
    for (const int32 Index : LDControl::SelectTauntTargets(Body.Victim.EntityId,CasterFoot,LDCombatControlDetail::Cm(Effect.RadiusCm),Candidates))
    {
        FLDBodyCombatState& Victim=*States[Index];
        // The taunt is a hard control: the single control path diminishes it and records its consequences.
        FLDControlApplication App;
        App.Kind=ELDControlKind::Taunt;
        App.EffectId=EffectId;
        App.SourceEntity=Body.Victim.EntityId;
        App.SourceBody={Body.Victim,Body.Identity};
        App.DurationMs=Effect.DurationMs;
        bool bApplied=false;
        if (!CommitControl(Victim,App,Now,bApplied,Error)) { return false; }
        if (!bApplied) { continue; }
        UE_LOG(LogTemp,Display,TEXT("LD_COMBAT_TAUNT victim=%s source=%s until=%lld"),
            *Victim.Victim.EntityId.ToString(),*Body.Victim.EntityId.ToString(),Victim.Taunt.IsSet() ? Victim.Taunt->ExpiresAtMs : 0);
    }
    return true;
}

bool ALDCombatDirector::CommitControl(FLDBodyCombatState& Victim, const FLDControlApplication& App, int64 NowMs, bool& bApplied,
    FString& Error)
{
    bApplied=false;
    if (App.DurationMs<=0 || Victim.bPendingDeath) { return true; }
    // Hard controls take the diminished duration; the chain advances only when the control actually lands.
    FLDHardControlChain Chain=Victim.HardControl;
    const bool bHard=LDCombatControl::IsHardControl(App.Kind);
    const int64 DurationMs=bHard ? LDCombatControl::DiminishHardControl(Chain,NowMs,App.DurationMs) : App.DurationMs;
    int64 ExpiresAtMs=0;
    if (!LDMatchLifecycle::AddDurationMs(NowMs,DurationMs,ExpiresAtMs,Error)) { return false; }
    FLDCombatRoot Instance;
    Instance.EffectId=App.EffectId;
    Instance.SourceEntity=App.SourceEntity;
    Instance.ExpiresAtMs=ExpiresAtMs;
    switch (App.Kind)
    {
    case ELDControlKind::Root:
        bApplied=LDTactical::ApplyRoot(Victim.Roots,Instance,NowMs);
        break;
    case ELDControlKind::Disarm:
        bApplied=LDCombatControl::Apply(Victim.Disarms,Instance,NowMs);
        // A disarmed body cannot finish a basic windup.
        if (bApplied && Victim.Action.IsSet() && Victim.Action->Kind==ELDCombatActionKind::Basic)
        { AbandonAction(Victim,ELDCombatReject::ControlBlocked); }
        break;
    case ELDControlKind::Stun:
        bApplied=LDCombatControl::Apply(Victim.Stuns,Instance,NowMs);
        // A stun interrupts any unreleased windup; queued and later requests are refused while it lasts.
        if (bApplied) { AbandonAction(Victim,ELDCombatReject::ControlBlocked); }
        break;
    case ELDControlKind::Taunt:
    {
        // Only contenders are taunted (camp members are never taunted); the newest taunt replaces the current one.
        if (Victim.Victim.Kind!=ELDVictimKind::Contender) { return true; }
        FLDCombatTaunt Taunt;
        Taunt.EffectId=App.EffectId;
        Taunt.SourceEntity=App.SourceEntity;
        Taunt.SourceBody=App.SourceBody;
        Taunt.ExpiresAtMs=ExpiresAtMs;
        bool bRevealed=false;
        bApplied=LDControl::TauntVictim(Victim.Taunt,Victim.Stealth,Taunt,NowMs,bRevealed);
        // Unreleased work the taunt now forbids ends without cost; a basic swing at the taunter continues.
        if (bApplied && Victim.Action.IsSet() && !LDControl::TauntAllows(Victim.Taunt,NowMs,
            Victim.Action->Kind==ELDCombatActionKind::Basic,Victim.Action->TargetBody))
        { AbandonAction(Victim,ELDCombatReject::ControlBlocked); }
        break;
    }
    case ELDControlKind::Slow:
    {
        FLDCombatSlow Slow;
        Slow.EffectId=App.EffectId;
        Slow.SourceEntity=App.SourceEntity;
        Slow.RatioBp=App.RatioBp;
        Slow.AttackRatioBp=App.AttackRatioBp;
        Slow.ExpiresAtMs=ExpiresAtMs;
        bApplied=LDTactical::ApplySlow(Victim.Slows,Slow,NowMs);
        break;
    }
    case ELDControlKind::Knockback:
    default:
        // The forced displacement is committed by its owner (#112 C2) through this path.
        Error=FString::Printf(TEXT("Control kind %u has no committed handler."),uint32(App.Kind));
        return false;
    }
    if (!bApplied) { return true; }
    if (bHard) { Victim.HardControl=Chain; }
    ++Victim.StateRevision;
    MarkViewChanged(Victim.Victim.EntityId);
    // Integration (#112): E's InterruptSpray(Victim) for Root/Stun/Taunt/Knockback and A's
    // RecordAssistContribution(Victim, CreditOwner, NowMs) for every committed control join here.
    return true;
}

bool ALDCombatDirector::IsImpaired(const FLDBodyCombatState& Body, int64 NowMs) const
{
    return LDTactical::HasActiveRoot(Body.Roots,NowMs) || LDTactical::HasActiveRoot(Body.Stuns,NowMs)
        || LDTactical::HasActiveRoot(Body.Disarms,NowMs) || LDControl::IsTaunted(Body.Taunt,NowMs)
        || LDTactical::StrongestSlowBp(Body.Slows,NowMs)>0;
}

bool ALDCombatDirector::ReduceCooldown(const FGuid& Entity, ELDCombatEntry Entry, int64 AmountMs)
{
    const ALDGameMode* Mode=Core();
    FLDCombatEntityRecord* Record=FindRecord(Entity);
    FLDCombatTrack* Track=Record ? TrackFor(*Record,Entry) : nullptr;
    if (!Mode || !Track) { return false; }
    const int64 Ready=LDCombatSourceRules::ReduceReadyAt(Track->ReadyAtMs,Mode->GetAuthorityNowMs(),AmountMs);
    if (Ready!=Track->ReadyAtMs)
    {
        Track->ReadyAtMs=Ready;
        MarkViewChanged(Entity);
    }
    return true;
}

bool ALDCombatDirector::ReduceLearnedCooldown(const FGuid& Entity, FName DefinitionId, int64 AmountMs)
{
    const ALDGameMode* Mode=Core();
    FLDCombatEntityRecord* Record=FindRecord(Entity);
    if (!Mode || !Record) { return false; }
    FLDCombatTrack* Track=Record->Tracks.FindByPredicate([DefinitionId](const FLDCombatTrack& Candidate)
    { return Candidate.Kind==ELDCombatSourceKind::Learned && Candidate.DefinitionId==DefinitionId; });
    if (!Track) { return false; }
    const int64 Ready=LDCombatSourceRules::ReduceReadyAt(Track->ReadyAtMs,Mode->GetAuthorityNowMs(),AmountMs);
    if (Ready!=Track->ReadyAtMs)
    {
        Track->ReadyAtMs=Ready;
        MarkViewChanged(Entity);
    }
    return true;
}
