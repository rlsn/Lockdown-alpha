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
    const FGuid& EffectId, const FLDCombatSourceRef& Source, FString& Error, TArray<FLDCombatBodyRef>* Landed)
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
        if (!Other || &State==&Body || State.Victim.Kind!=ELDVictimKind::Contender
            || IsIsolated(Body.Victim.EntityId,State)) { continue; }
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
        App.Source=Source;
        bool bApplied=false;
        if (!CommitControl(Victim,App,Now,bApplied,Error)) { return false; }
        if (!bApplied) { continue; }
        if (Landed) { Landed->Add({Victim.Victim,Victim.Identity}); }
        UE_LOG(LogLDCombat,Display,TEXT("LD_COMBAT_TAUNT victim=%s source=%s until=%lld"),
            *Victim.Victim.EntityId.ToString(),*Body.Victim.EntityId.ToString(),Victim.Taunt.IsSet() ? Victim.Taunt->ExpiresAtMs : 0);
    }
    return true;
}

bool ALDCombatDirector::CommitControl(FLDBodyCombatState& Victim, const FLDControlApplication& App, int64 NowMs, bool& bApplied,
    FString& Error)
{
    bApplied=false;
#if WITH_DEV_AUTOMATION_TESTS
    if (const auto* Mode = Core(); Mode && Mode->IsDevPeaceful(Victim.Victim.EntityId)
        && App.SourceEntity != Victim.Victim.EntityId) { return true; }
#endif
    const bool bIsolated = App.bSpectatorThrow
        ? Victim.IsolationGroup == ALDGameMode::RestIsolationGroup() && !Victim.bTrainingDummy
        : IsIsolated(App.SourceEntity,Victim);
    if (App.DurationMs<=0 || Victim.bPendingDeath || bIsolated) { return true; }
    const bool bHard=LDCombatControl::IsHardControl(App.Kind);
    // C-10 prototype: inside a contender's guard no new hard control lands, and a knockback from the chain's roll on (or in
    // the guard) still pushes the body but keeps its input.
    const bool bAgency=Proto.bAgency && Victim.Victim.Kind==ELDVictimKind::Contender && App.SourceEntity!=Victim.Victim.EntityId;
    const bool bGuarded=bAgency && NowMs<Victim.Agency.GuardUntilMs;
    if (bGuarded && bHard)
    {
        UE_LOG(LogLDCombat,Display,TEXT("LD_PROTO_AGENCY guarded t=%lld victim=%s kind=%u"),NowMs,*LDLog::Id(Victim.Victim.EntityId),
            uint32(App.Kind));
        return true;
    }
    bool bRoll=false;
    if (bAgency && App.Kind==ELDControlKind::Knockback)
    {
        int64 EndMs=0;
        if (!LDMatchLifecycle::AddDurationMs(NowMs,App.DurationMs,EndMs,Error)) { return false; }
        bRoll=LDCombatControl::AgencyKnockbackRolls(Victim.Agency,NowMs,EndMs,Proto.Agency) || bGuarded;
        if (bRoll)
        {
            UE_LOG(LogLDCombat,Display,TEXT("LD_PROTO_AGENCY roll t=%lld victim=%s chain=%d"),NowMs,*LDLog::Id(Victim.Victim.EntityId),
                Victim.Agency.KnockChain);
        }
    }
    // Hard controls take the diminished duration; the chain advances only when the control actually lands.
    FLDHardControlChain Chain=Victim.HardControl;
    const int64 BaseDurationMs=App.DurationMs;
    const int64 DurationMs=bHard ? LDCombatControl::DiminishHardControl(Chain,NowMs,BaseDurationMs) : BaseDurationMs;
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
        if (bApplied && Victim.Action.IsSet() && (Victim.Action->Kind==ELDCombatActionKind::Ability || Victim.Action->bMonsterSpecial)
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
        // C-10 roll: the caller still pushes the body, but no window blocks its actions, its windup and order go on.
        if (bRoll) { bApplied=true; break; }
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
    if (bAgency && !bRoll && LDCombatControl::AgencyWeightBp(App.Kind)>0)
    { Victim.Agency.Losses.Add({NowMs,ExpiresAtMs,LDCombatControl::AgencyWeightBp(App.Kind)}); }
    if (App.Kind==ELDControlKind::Stun || App.Kind==ELDControlKind::Silence
        || App.Kind==ELDControlKind::Taunt || (App.Kind==ELDControlKind::Knockback && !bRoll))
    { CancelChannel(Victim); }
    if (bHard) { Victim.HardControl=Chain; }
    // #198 build safe window: a hard control or a knockback received is a combat action of its victim; a slow is not.
    if (bHard || App.Kind==ELDControlKind::Knockback) { MarkBuildUnsafe(Victim.Victim.EntityId,NowMs); }
    ++Victim.StateRevision;
    MarkViewChanged(Victim.Victim.EntityId);
    // Root, stun, taunt, knockback and (#147 D10) silence reset the victim's spray progress at the moment they land;
    // disarm and slow do not.
    if ((App.Kind==ELDControlKind::Root || App.Kind==ELDControlKind::Stun || App.Kind==ELDControlKind::Taunt
        || (App.Kind==ELDControlKind::Knockback && !bRoll) || App.Kind==ELDControlKind::Silence) && !InterruptSpray(Victim,Error))
    { return false; }
    // Every committed control or slow is its owner's assist contribution on this life.
    RecordAssistContribution(Victim,App.CreditOwner,NowMs);
    // #198 F C05/C07 read every hard control that actually landed.
    return !bHard || OnHardControl(Victim,App,NowMs,Error);
}

// C-10 prototype "急眼了": a contender whose weighted lost agency inside the window reaches the threshold (outside its guard
// and cooldown) gets the guard; V-Cut also ends the controls others hold on it (a push in flight keeps moving).
void ALDCombatDirector::AdvanceAgency(int64 NowMs)
{
    const FLDAgencyParams& Params=Proto.Agency;
    for (auto& Pair : Bodies)
    {
        FLDBodyCombatState& Body=Pair.Value;
        if (Body.Victim.Kind!=ELDVictimKind::Contender || Body.bPendingDeath) { continue; }
        LDCombatControl::PruneAgency(Body.Agency,NowMs,Params.WindowMs);
        if (Body.Agency.Losses.IsEmpty() || !LDCombatControl::AgencyTriggers(Body.Agency,NowMs,Params)) { continue; }
        const int64 LossBpMs=LDCombatControl::AgencyLossBpMs(Body.Agency.Losses,NowMs,Params.WindowMs);
        Body.Agency.LastTriggerMs=NowMs;
        Body.Agency.GuardUntilMs=NowMs+Params.GuardMs;
        if (Params.bCut)
        {
            const FGuid Self=Body.Victim.EntityId;
            const auto Others=[Self](const FLDCombatRoot& Row) { return Row.SourceEntity!=Self; };
            Body.Stuns.RemoveAll(Others);
            Body.Roots.RemoveAll(Others);
            Body.Disarms.RemoveAll(Others);
            Body.Silences.RemoveAll(Others);
            Body.Knockbacks.RemoveAll(Others);
            if (Body.Taunt.IsSet() && Body.Taunt->SourceEntity!=Self) { Body.Taunt.Reset(); }
            for (FLDAgencyLoss& Loss : Body.Agency.Losses) { Loss.EndMs=FMath::Min(Loss.EndMs,NowMs); }
        }
        ++Body.StateRevision;
        MarkViewChanged(Body.Victim.EntityId);
        UE_LOG(LogLDCombat,Display,TEXT("LD_PROTO_AGENCY trigger t=%lld victim=%s meter=%.2f cut=%d"),NowMs,*LDLog::Id(Body.Victim.EntityId),
            double(LossBpMs)/10000000.,Params.bCut ? 1 : 0);
    }
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
    // P6: a hard control on a field contender breaks its mini flag spray and its locker channel.
    if (Target.Victim.Kind==ELDVictimKind::Contender && !Target.IsolationGroup.IsValid()) { Mode->NotePacingControl(Target.Victim.EntityId); }
    // Rule fixtures without a spray zone, camp bodies and rest-space bodies (#198: a rest life or a training dummy)
    // carry no spray progress.
    if (Target.Victim.Kind!=ELDVictimKind::Contender || Target.IsolationGroup.IsValid()
        || !Mode->CaptureReducer.IsActive() || !Mode->CaptureCircle.IsValid()) { return true; }
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
