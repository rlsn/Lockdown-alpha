#include "Combat/LDCombatDirector.h"
#include "Core/LDLog.h"
#include "Character/LDCharacter.h"
#include "Framework/LDGameMode.h"

// #198 rest phases: the Combat side of the approved phase-boundary resource policy (issue #198 comment 6752601175 §8)
// and of the private rest space. Field bodies have no isolation group; a rest life has its owner's group, so nothing
// crosses between rest groups or into the frozen field. A frozen world body (camp member,
// Boss) is suspended: it keeps HP/MP, regenerates nothing, and can neither be affected nor act.

bool ALDCombatDirector::IsIsolated(const FGuid& SourceEntity, const FLDBodyCombatState& Target) const
{
    if (Target.bSuspended) { return true; }
    FGuid SourceGroup;
    if (const FLDBodyCombatState* Source=FindEntityBody(SourceEntity))
    {
        if (Source->bSuspended) { return true; }
        SourceGroup=Source->IsolationGroup;
    }
    else if (const FLDCombatEntityRecord* Record=FindRecord(SourceEntity); Record && Record->Kind==ELDVictimKind::Contender)
    {
        // A contender between lives keeps the group of the space it is in: its released effects never cross it.
        const ALDGameMode* Mode=Core();
        if (Mode && Mode->IsInRestSpace(SourceEntity)) { SourceGroup=SourceEntity; }
    }
    return SourceGroup!=Target.IsolationGroup;
}

bool ALDCombatDirector::IsHiddenFrom(const FGuid& Observer, const FLDCombatBodyRef& Target) const
{
    const FLDBodyCombatState* Body=FindBody(Target);
    return Body && IsIsolated(Observer,*Body);
}

bool ALDCombatDirector::SetRestIsolation(const FLDCombatBodyRef& Ref, bool bRest, const FLDGameplayWritePermit& Permit, FString& Error)
{
    const ALDGameMode* Mode=Core();
    FLDBodyCombatState* Body=FindBody(Ref);
    if (!Mode || Mode->ValidateWritePermit(Permit,ELDCommitPhase::RespawnsAndWorld)!=ELDCommitReject::None
        || !Body || Body->bPendingDeath || Ref.Victim.Kind!=ELDVictimKind::Contender)
    {
        Error=TEXT("Rest isolation needs a living contender body and the current RespawnsAndWorld permit.");
        return false;
    }
    const FGuid Group=bRest ? Ref.Victim.EntityId : FGuid();
    if (Body->IsolationGroup!=Group)
    {
        Body->IsolationGroup=Group;
        ++Body->StateRevision;
        MarkViewChanged(Ref.Victim.EntityId);
    }
    Error.Reset();
    return true;
}

bool ALDCombatDirector::SetBodySuspended(const FLDCombatBodyRef& Ref, bool bSuspended, const FLDGameplayWritePermit& Permit,
    FString& Error)
{
    const ALDGameMode* Mode=Core();
    FLDBodyCombatState* Body=FindBody(Ref);
    if (!Mode || Mode->ValidateWritePermit(Permit,ELDCommitPhase::RespawnsAndWorld)!=ELDCommitReject::None
        || !Body || Body->bPendingDeath || Ref.Victim.Kind!=ELDVictimKind::CampMember)
    {
        Error=TEXT("Only a living world body can be frozen or thawed, under the current RespawnsAndWorld permit.");
        return false;
    }
    if (Body->bSuspended==bSuspended) { Error.Reset(); return true; }
    Body->bSuspended=bSuspended;
    // Nothing accrues while frozen: the thaw restarts the regeneration and out-of-combat clocks at the boundary.
    Body->RegenThroughMs=Mode->GetAuthorityNowMs();
    if (!bSuspended)
    {
        Body->LastCombatAtMs=Mode->GetAuthorityNowMs();
        Body->MonsterNextAttackAtMs=FMath::Min(Body->MonsterNextAttackAtMs,Mode->GetAuthorityNowMs());
    }
    ++Body->StateRevision;
    MarkViewChanged(Ref.Victim.EntityId);
    Error.Reset();
    return true;
}

int32 ALDCombatDirector::CancelAllForPhase(const FLDGameplayWritePermit& Permit)
{
    ALDGameMode* Mode=Core();
    if (!Mode || Mode->ValidateWritePermit(Permit,ELDCommitPhase::RespawnsAndWorld)!=ELDCommitReject::None) { return INDEX_NONE; }
    const int64 NowMs=Mode->GetAuthorityNowMs();
    // Future effects end as CancelledByPhase: no hit, break, heal, repayment or death is produced for them.
    const int32 Cancelled=Projectiles.Num()+PendingHits.Num()+Zones.Num()+Channels.Num()+GroundEffects.Num();
    Projectiles.Reset();
    PendingHits.Reset();
    Zones.Reset();
    Channels.Reset();
    GroundEffects.Reset();
    ContactMotion.Reset();
    PendingContacts.Reset();
    for (auto& Pair : Bodies)
    {
        FLDBodyCombatState& Body=Pair.Value;
        if (Body.bPendingDeath) { continue; }
        // An unreleased action of a body that stays (a world body) ends unreleased.
        AbandonAction(Body,ELDCombatReject::Cancelled);
        Body.Roots.Reset();
        Body.Slows.Reset();
        Body.TimedBonuses.Reset();
        Body.Disarms.Reset();
        Body.Stuns.Reset();
        Body.Silences.Reset();
        Body.Knockbacks.Reset();
        Body.AttackSlows.Reset();
        Body.Empower.Reset();
        Body.ThornsMarks.Reset();
        Body.AssistContributions.Reset();
        Body.Taunt.Reset();
        Body.ShieldBurst.Reset();
        Body.SharedBreach={};
        Body.Burn={};
        // A new phase is a new fight: the hard-control diminishing returns start over (approved §9).
        Body.HardControl={};
        Body.DashContact.Reset();
        Body.DashUntilMs=0;
        Body.SlowResistanceUntilMs=0;
        Body.SlowResistanceBp=0;
        Body.ChannelEffectId={};
        Body.ChannelDirectSequence=0;
        Body.ChannelStartedAtMs=0;
        if (Body.Shield.IsSet() || Body.Resources.ShieldP2!=0)
        {
            Body.Shield.Reset();
            Body.Resources.ShieldP2=0;
            Body.ShieldGrantedP2=0;
            ++Body.Resources.Revision;
        }
        ++Body.StateRevision;
        MarkViewChanged(Body.Victim.EntityId);
        FString Error;
        if (!RefreshTemporary(Body,Pair.Key.Get(),NowMs,Error))
        {
            UE_LOG(LogLDCombat,Error,TEXT("LD_PHASE_CLEANUP_REFRESH_FAILED entity=%s %s"),*LDLog::Id(Body.Victim.EntityId),*Error);
            return INDEX_NONE;
        }
    }
    for (auto& Pair : Records) { Pair.Value.Pending.Reset(); }
    UE_LOG(LogLDCombat,Display,TEXT("LD_PHASE_EFFECTS_CANCELLED count=%d at_ms=%lld reason=CancelledByPhase"),Cancelled,NowMs);
    return Cancelled;
}

bool ALDCombatDirector::ReadyAllForPhase(const FGuid& Contender, const FLDGameplayWritePermit& Permit, FString& Error)
{
    const ALDGameMode* Mode=Core();
    FLDCombatEntityRecord* Record=FindRecord(Contender);
    if (!Mode || Mode->ValidateWritePermit(Permit,ELDCommitPhase::RespawnsAndWorld)!=ELDCommitReject::None
        || !Record || Record->Kind!=ELDVictimKind::Contender)
    {
        Error=TEXT("The phase refill needs a contender's Combat record and the current RespawnsAndWorld permit.");
        return false;
    }
    const int64 NowMs=Mode->GetAuthorityNowMs();
    for (FLDCombatTrack& Track : Record->Tracks)
    {
        // Only the end moves: identity and binding stay, so a forgotten active's track is kept (and now ready).
        Track.ReadyAtMs=FMath::Min(Track.ReadyAtMs,NowMs);
    }
    Record->NextAttackAtMs=FMath::Min(Record->NextAttackAtMs,NowMs);
    MarkViewChanged(Contender);
    Error.Reset();
    return true;
}

void ALDCombatDirector::RetireBodyForPhase(const FLDCombatBodyRef& Body)
{
    // Its own unsettled effects end with the life; already committed damage and receipts stay.
    CancelEntityEffects(Body.Victim.EntityId);
    RemoveRegisteredBody(Body);
}
