#include "Combat/LDCombatDirector.h"
#include "Core/LDLog.h"
#include "Character/LDCharacter.h"
#include "Combat/LDAttributeProjection.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDGameMode.h"
#include "PK/LDPKDirector.h"

// #198 rest phases: the Combat side of the approved phase-boundary resource policy (issue #198 comment 6752601175 §8)
// and of the shared rest space. Field bodies have no isolation group; rest lives share one group, so nothing
// crosses between rest groups or into the frozen field. A frozen world body (camp member,
// Boss) is suspended: it keeps HP/MP, regenerates nothing, and can neither be affected nor act.

bool ALDCombatDirector::IsIsolated(const FGuid& SourceEntity, const FLDBodyCombatState& Target) const
{
#if WITH_DEV_AUTOMATION_TESTS
    if (const auto* Mode = Core(); Mode && Mode->IsDevPeaceful(Target.Victim.EntityId)
        && SourceEntity != Target.Victim.EntityId) { return true; }
#endif
    if (IsOutOfGroup(SourceEntity,Target)) { return true; }
    // Sharing visibility is not permission to damage/control another trainee (or oneself through a released effect).
    if (Target.IsolationGroup == ALDGameMode::RestIsolationGroup() && !Target.bTrainingDummy) { return true; }
    // #198 session D: PK friendly fire is off; a body still affects itself.
    if (Target.PKSide==0 || SourceEntity==Target.Victim.EntityId) { return false; }
    uint8 SourceSide=0;
    if (const FLDBodyCombatState* Source=FindEntityBody(SourceEntity)) { SourceSide=Source->PKSide; }
    else if (const ALDGameMode* Mode=Core(); Mode && Mode->PKDirector && Mode->PKDirector->IsArenaActive())
    {
        SourceSide=Mode->PKDirector->SideOf(SourceEntity);
    }
    return SourceSide!=0 && SourceSide==Target.PKSide;
}

bool ALDCombatDirector::ActingGroup(const FGuid& Entity, FGuid& OutGroup) const
{
    OutGroup.Invalidate();
    if (const FLDBodyCombatState* Source=FindEntityBody(Entity))
    {
        if (Source->bSuspended) { return false; }
        OutGroup=Source->IsolationGroup;
        return true;
    }
    if (const FLDCombatEntityRecord* Record=FindRecord(Entity); Record && Record->Kind==ELDVictimKind::Contender)
    {
        // A contender between lives keeps the group of the space it is in: its released effects never cross it.
        const ALDGameMode* Mode=Core();
        if (Mode && Mode->IsInRestSpace(Entity)) { OutGroup=ALDGameMode::RestIsolationGroup(); }
        // #198 session D: an entrant between lives during the PK keeps the PK group.
        else if (Mode && Mode->PKDirector && Mode->PKDirector->IsArenaActive() && Mode->PKDirector->SideOf(Entity)!=0)
        {
            OutGroup=Mode->PKDirector->GetGroup();
        }
    }
    return true;
}

bool ALDCombatDirector::IsOutOfGroup(const FGuid& SourceEntity, const FLDBodyCombatState& Target) const
{
    FGuid SourceGroup;
    return Target.bSuspended || !ActingGroup(SourceEntity, SourceGroup) || SourceGroup!=Target.IsolationGroup;
}

bool ALDCombatDirector::IsHiddenFrom(const FGuid& Observer, const FLDCombatBodyRef& Target) const
{
    const FLDBodyCombatState* Body=FindBody(Target);
    return Body && IsOutOfGroup(Observer,*Body);
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
    const FGuid Group=bRest ? ALDGameMode::RestIsolationGroup() : FGuid();
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
        if (Pair.Value.bPendingDeath) { continue; }
        FString Error;
        if (!ClearBodyForPhase(Pair.Value,Pair.Key.Get(),NowMs,Error))
        {
            UE_LOG(LogLDCombat,Error,TEXT("LD_PHASE_CLEANUP_REFRESH_FAILED entity=%s %s"),*LDLog::Id(Pair.Value.Victim.EntityId),*Error);
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

bool ALDCombatDirector::ClearBodyForPhase(FLDBodyCombatState& Body, ALDCharacter* Actor, int64 NowMs, FString& Error)
{
    // An unreleased action of a body that stays (a world body, a training pair) ends unreleased.
    AbandonAction(Body,ELDCombatReject::Cancelled);
    Body.Roots.Reset();
    Body.Slows.Reset();
    Body.TimedBonuses.Reset();
    Body.Disarms.Reset();
    Body.Stuns.Reset();
    Body.Silences.Reset();
    Body.Knockbacks.Reset();
    Body.AttackSlows.Reset();
    Body.Empowers.Reset();
    Body.Ledgers.Reset();
    Body.VoluntaryDash.Reset();
    // #198 F workshop state of the old phase: watches, marks, budgets, memories, debts, windows and stores.
    Body.PendingImpact.Reset();
    Body.ControlMarks.Reset();
    Body.SetupMarks.Reset();
    Body.EffectBudgets.Reset();
    Body.DirectHitMarks.Reset();
    Body.RecentAttackers.Reset();
    Body.Debts.Reset();
    Body.FireWindowSumP2=Body.FireWindowEndMs=0;
    Body.CounterStoreP2=Body.CounterLastAddMs=Body.CounterNextFireMs=0;
    Body.CounterRootEffectId.Invalidate();
    Body.ShieldGrantSource=FLDCombatSourceRef();
    Body.StoredForceWatch.Reset();
    Body.StoredForce.Reset();
    Body.WideToken.Reset();
    Body.LandedBasics=0;
    Body.ThornsMarks.Reset();
    Body.AssistContributions.Reset();
    Body.Taunt.Reset();
    Body.SharedBreach={};
    Body.Burn={};
    // A new phase is a new fight: the hard-control diminishing returns start over (approved §9).
    Body.HardControl={};
    Body.ExposedUntilMs=0;
    Body.DashContact.Reset();
    Body.DashUntilMs=0;
    // Component rests are cooldowns too: every one is ready in the new phase.
    Body.RetortReadyAtMs=0;
    Body.DamagedShieldReadyAtMs=0;
    Body.AbilityHitShieldReadyAtMs=0;
    Body.LastActiveRelease=NAME_None;
    Body.LastActiveReleaseAtMs=0;
    Body.ContactRefundReadyAtMs=0;
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
    return RefreshTemporary(Body,Actor,NowMs,Error);
}

bool ALDCombatDirector::PrepareTrainingDummy(ALDCharacter& Body, const FLDVictimKey& Victim, uint8 Level,
    const FLDGameplayWritePermit& Permit, FLDBodyCombatState& Out, FString& Error) const
{
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!Content || Level==0 || Victim.Life!=1 || FindRecord(Victim.EntityId))
    {
        Error=TEXT("A training dummy is a fresh entity of a known chassis level.");
        return false;
    }
    if (!PrepareContenderBody(Body,Victim,Permit,Out,Error)) { return false; }
    // The existing contender chassis at the owner's current level, without any build (coordinator decision).
    const auto Projected=LDAttributeProjection::Project(FLDAttributeBuildValues(Level,{}),*Content);
    if (!Projected.IsSuccess())
    {
        Error=TEXT("The training dummy's chassis is invalid: ")+Projected.Error.Field+TEXT(": ")+Projected.Error.Message;
        return false;
    }
    const FLDAttributeProjection& Chassis=Projected.Value.GetValue();
    Out.BuildProjection=Chassis;
    Out.AttributeProjection=Chassis;
    Out.Resources.HpP2=Out.Resources.MaxHpP2=Chassis.Final.MaxHPP2;
    Out.Resources.MpP2=Out.Resources.MaxMpP2=Chassis.Final.MaxMPP2;
    if (Out.PublicFacts.IsSet()) { Out.PublicFacts->Level=Level; }
    Out.IsolationGroup=ALDGameMode::RestIsolationGroup();
    Out.bTrainingDummy=true;
    Error.Reset();
    return true;
}

bool ALDCombatDirector::ResetTraining(const FGuid& OwnerId, const FGuid& Dummy, const FLDGameplayWritePermit& Permit, FString& Error)
{
    const ALDGameMode* Mode=Core();
    if (!Mode || Mode->ValidateWritePermit(Permit,ELDCommitPhase::RespawnsAndWorld)!=ELDCommitReject::None)
    {
        Error=TEXT("A training reset needs the current RespawnsAndWorld permit.");
        return false;
    }
    const int64 NowMs=Mode->GetAuthorityNowMs();
    for (const FGuid& Entity : {OwnerId,Dummy})
    {
        CancelEntityEffects(Entity);
        ALDCharacter* Actor=nullptr;
        FLDBodyCombatState* Body=FindEntityBody(Entity,&Actor);
        if (!Body || Body->bPendingDeath)
        {
            Error=TEXT("A training reset needs the owner's rest body and its dummy.");
            return false;
        }
        if (!ClearBodyForPhase(*Body,Actor,NowMs,Error)) { return false; }
        Body->Resources.HpP2=Body->Resources.MaxHpP2;
        Body->Resources.MpP2=Body->Resources.MaxMpP2;
        ++Body->Resources.Revision;
    }
    return ReadyAllForPhase(OwnerId,Permit,Error);
}

void ALDCombatDirector::RetireTrainingEntity(const FLDCombatBodyRef& Body)
{
    RetireBodyForPhase(Body);
    Records.Remove(Body.Victim.EntityId);
}
