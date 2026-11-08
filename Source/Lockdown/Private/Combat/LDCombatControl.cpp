#include "Combat/LDCombatDirector.h"
#include "Core/LDLog.h"
#include "Combat/LDControlRules.h"
#include "Combat/LDCombatSourceRules.h"
#include "Combat/LDTacticalRules.h"
#include "Character/LDCharacter.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDGameMode.h"
#include "Region/LDMapRegistry.h"
#include "Vision/LDVisionDirector.h"
#include "Engine/World.h"

// Taunt (CONTENT_AND_BALANCE 15.5) on Combat's body records; the rules live in LDControlRules.
namespace LDCombatControlDetail
{
    double Cm(const FLDDemoRational& Value)
    { return Value.Denominator>0 ? double(Value.Numerator)/double(Value.Denominator) : 0.; }

    // Observation of Target from SourceFoot under the same sight rule as the observation owner.
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

bool ALDCombatDirector::EffectiveMoveSpeed(const FLDBodyCombatState& Body, double& OutCmPerSec) const
{
    OutCmPerSec=LDCombatControlDetail::Cm(Body.AttributeProjection.Final.MoveSpeedCmPerSec);
    return FMath::IsFinite(OutCmPerSec) && OutCmPerSec>0;
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
        UE_LOG(LogLDCombat,Display,TEXT("LD_COMBAT_TAUNT_END victim=%s alive=%d observed=%d"),
            *Body.Victim.EntityId.ToString(),int32(bAlive),int32(bObserved));
        Body.Taunt.Reset();
        ++Body.StateRevision;
        MarkViewChanged(Body.Victim.EntityId);
    }
    return true;
}

bool ALDCombatDirector::ApplyTauntEffect(ALDCharacter& Actor, FLDBodyCombatState& Body, const FLDDemoEffectDefinition& Effect,
    const FGuid& EffectId, FString& Error, int32* OutTaunted)
{
    if (OutTaunted) { *OutTaunted=0; }
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
        App.CreditOwner=Body.Victim.EntityId;
        App.DurationMs=Effect.DurationMs;
        bool bApplied=false;
        if (!CommitControl(Victim,App,Now,bApplied,Error)) { return false; }
        if (!bApplied) { continue; }
        if (OutTaunted) { ++*OutTaunted; }
        UE_LOG(LogLDCombat,Display,TEXT("LD_COMBAT_TAUNT victim=%s source=%s until=%lld"),
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
    case ELDControlKind::Silence:
        // #147 D10: a silence forbids Q/W only; a learned skill still winding up ends unpaid, a basic or the dash goes on.
        bApplied=LDCombatControl::Apply(Victim.Silences,Instance,NowMs);
        if (bApplied && Victim.Action.IsSet() && Victim.Action->Kind==ELDCombatActionKind::Ability
            && Victim.Action->Source.Kind!=ELDCombatSourceKind::Innate)
        { AbandonAction(Victim,ELDCombatReject::ControlBlocked); }
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
        bApplied=LDControl::ApplyTaunt(Victim.Taunt,Taunt,NowMs);
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
        Slow.ExpiresAtMs=ExpiresAtMs;
        bApplied=LDTactical::ApplySlow(Victim.Slows,Slow,NowMs);
        break;
    }
    case ELDControlKind::Knockback:
        // #112 C2: the forced-displacement window (no diminishing returns). Every voluntary action is blocked while it
        // lasts, the unreleased action ends, and Navigation drops the current order when KnockbackSerial changes; the
        // caller (ApplyKnockback) starts the displacement itself after this commit.
        bApplied=LDCombatControl::Apply(Victim.Knockbacks,Instance,NowMs);
        if (bApplied)
        {
            AbandonAction(Victim,ELDCombatReject::ControlBlocked);
            ++Victim.KnockbackSerial;
        }
        break;
    default:
        Error=FString::Printf(TEXT("Control kind %u has no committed handler."),uint32(App.Kind));
        return false;
    }
    if (!bApplied) { return true; }
    if ((App.Kind==ELDControlKind::Stun || App.Kind==ELDControlKind::Silence
        || App.Kind==ELDControlKind::Taunt || App.Kind==ELDControlKind::Knockback)
        && !CancelSkillPosture(Victim,false,NowMs,Error)) { return false; }
    if (bHard) { Victim.HardControl=Chain; }
    ++Victim.StateRevision;
    MarkViewChanged(Victim.Victim.EntityId);
    // Root, stun, taunt, knockback and (#147 D10) silence reset the victim's spray progress at the moment they land;
    // disarm and slow do not.
    if ((App.Kind==ELDControlKind::Root || App.Kind==ELDControlKind::Stun || App.Kind==ELDControlKind::Taunt
        || App.Kind==ELDControlKind::Knockback || App.Kind==ELDControlKind::Silence) && !InterruptSpray(Victim,Error))
    { return false; }
    // Every committed control or slow is its owner's assist contribution on this life.
    RecordAssistContribution(Victim,App.CreditOwner,NowMs);
    return true;
}

bool ALDCombatDirector::IsImpaired(const FLDBodyCombatState& Body, int64 NowMs) const
{
    return LDTactical::HasActiveRoot(Body.Roots,NowMs) || LDTactical::HasActiveRoot(Body.Stuns,NowMs)
        || LDTactical::HasActiveRoot(Body.Disarms,NowMs) || LDControl::IsTaunted(Body.Taunt,NowMs)
        || LDTactical::HasActiveRoot(Body.Knockbacks,NowMs) || LDTactical::HasActiveRoot(Body.Silences,NowMs)
        || LDTactical::StrongestSlowBp(Body.Slows,NowMs)>0;
}

bool ALDCombatDirector::IsCastBlocked(const FLDBodyCombatState& Body, ELDCombatSourceKind Kind) const
{
    return IsActionBlocked(Body,ELDCombatControlAction::Cast)
        || (Kind!=ELDCombatSourceKind::Innate && IsActionBlocked(Body,ELDCombatControlAction::Skill));
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

bool ALDCombatDirector::InterruptSpray(const FLDBodyCombatState& Target, FString& Error)
{
    auto* Mode=Core();
    if (!Mode) { Error=TEXT("A spray interruption requires the current core."); return false; }
    // Rule fixtures without a spray zone, and camp bodies, carry no spray progress.
    if (Target.Victim.Kind!=ELDVictimKind::Contender || !Mode->CaptureReducer.IsActive() || !Mode->CaptureCircle.IsValid()) { return true; }
    if (!Mode->ActiveWritePermit)
    {
        Error=TEXT("A spray interruption must be committed inside a gameplay permit.");
        return false;
    }
    const ELDCommitReject Reject=Mode->PublishCaptureInterrupt(*Mode->ActiveWritePermit,{Target.Victim,Target.Identity});
    if (Reject!=ELDCommitReject::None)
    {
        Error=FString::Printf(TEXT("The spray zone refused the interruption of %s (reject %u)."),*Target.Victim.EntityId.ToString(),uint32(Reject));
        return false;
    }
    return true;
}
