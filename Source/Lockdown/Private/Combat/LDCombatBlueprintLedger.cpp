#include "Combat/LDCombatDirector.h"
#include "Framework/LDGameMode.h"

// #198 §10: the build blueprint ledger is fed only from the points where Combat commits a build install, a release, a hit,
// a heal, a repayment, a regeneration, a shield, a component proc or a refusal. It never decides anything; a missing record
// simply records nothing.

namespace LDCombatBlueprintLedgerDetail
{
// §16.3: a rest life and its own training dummy share their owner's isolation group without a PK side; training results
// are owner-only rehearsal, never match facts of the build.
bool IsTraining(const FLDBodyCombatState& Body)
{
    return Body.IsolationGroup.IsValid() && Body.PKSide==0;
}

// The refusals a player reads as "this build could not do that now"; protocol, staleness and phase refusals are not facts.
bool IsGameplayRefusal(ELDCombatReject Reason)
{
    switch (Reason)
    {
    case ELDCombatReject::Busy:
    case ELDCombatReject::Cooldown:
    case ELDCombatReject::AttackNotReady:
    case ELDCombatReject::InsufficientMana:
    case ELDCombatReject::ControlBlocked:
    case ELDCombatReject::TargetUnavailable:
    case ELDCombatReject::InvalidDirection:
    case ELDCombatReject::OutOfRange:
    case ELDCombatReject::Blocked:
        return true;
    default:
        return false;
    }
}
}

int32 ALDCombatDirector::LedgerVersion(const FGuid& Contender)
{
    const FLDCombatEntityRecord* Record=FindRecord(Contender);
    const ALDGameMode* Mode=Core();
    if (!Record || Record->Kind!=ELDVictimKind::Contender || !Mode) { return INDEX_NONE; }
    return BuildLedger.Observe(Contender,Record->Build,Mode->GetAuthorityNowMs());
}

void ALDCombatDirector::LedgerBuildInstalled(const FGuid& Contender, const FLDBuildProjectionInput& Previous)
{
    const FLDCombatEntityRecord* Record=FindRecord(Contender);
    const ALDGameMode* Mode=Core();
    if (!Record || Record->Kind!=ELDVictimKind::Contender || !Mode) { return; }
    const int64 NowMs=Mode->GetAuthorityNowMs();
    // The replaced build is the match-start version when this is the contender's first change.
    BuildLedger.Observe(Contender,Previous,NowMs);
    BuildLedger.Observe(Contender,Record->Build,NowMs);
}

void ALDCombatDirector::LedgerEffectReleased(const FLDBodyCombatState& Body, const FGuid& EffectId, ELDBlueprintFact Counted)
{
    if (Body.Victim.Kind!=ELDVictimKind::Contender || LDCombatBlueprintLedgerDetail::IsTraining(Body)) { return; }
    const FGuid& Contender=Body.Victim.EntityId;
    const int32 Version=LedgerVersion(Contender);
    if (Version==INDEX_NONE) { return; }
    BuildLedger.NoteEffect(EffectId,Contender,Version);
    if (Counted!=ELDBlueprintFact::Count) { BuildLedger.Add(Contender,Version,Counted,1); }
}

void ALDCombatDirector::LedgerHitCommitted(const FLDCombatHit& Hit, const FLDBodyCombatState& Victim, const FLDDamageEvaluation& Damage)
{
    // Training never meets the field: isolation keeps both sides of a training hit inside the owner's group.
    if (LDCombatBlueprintLedgerDetail::IsTraining(Victim)) { return; }
    // The victim's own cuts count even when they leave nothing to apply.
    const int64 CutP2=Damage.BraceCutP2+Damage.GuardCutP2;
    if (Victim.Victim.Kind==ELDVictimKind::Contender && CutP2>0 && Hit.SourceEntity!=Victim.Victim.EntityId)
    {
        const int32 Version=LedgerVersion(Victim.Victim.EntityId);
        if (Version!=INDEX_NONE) { BuildLedger.Add(Victim.Victim.EntityId,Version,ELDBlueprintFact::DamageReduced,CutP2); }
    }
    // Protection, a miss and a zero result commit no damage; a self-inflicted hit is neither dealt nor taken.
    if (Damage.DamageP2<=0 || Hit.SourceEntity==Victim.Victim.EntityId) { return; }
    if (Hit.CreditOwner.IsValid() && Hit.CreditOwner!=Victim.Victim.EntityId)
    {
        // The version that released the effect; a reaction without a recorded release (thorns, retort, burst) belongs to
        // the version installed when it commits.
        int32 Version=BuildLedger.EffectVersion(Hit.EffectId,Hit.CreditOwner);
        if (Version==INDEX_NONE) { Version=LedgerVersion(Hit.CreditOwner); }
        if (Version!=INDEX_NONE)
        {
            BuildLedger.Add(Hit.CreditOwner,Version,ELDBlueprintFact::DamageDealt,Damage.DamageP2);
            if (Victim.Victim.Kind==ELDVictimKind::Contender)
            { BuildLedger.Add(Hit.CreditOwner,Version,ELDBlueprintFact::DamageToContenders,Damage.DamageP2); }
        }
    }
    if (Victim.Victim.Kind!=ELDVictimKind::Contender) { return; }
    const FGuid& Contender=Victim.Victim.EntityId;
    const int32 Version=LedgerVersion(Contender);
    if (Version==INDEX_NONE) { return; }
    BuildLedger.Add(Contender,Version,ELDBlueprintFact::DamageTaken,Damage.DamageP2);
    BuildLedger.Add(Contender,Version,ELDBlueprintFact::ShieldAbsorbed,Damage.ShieldAbsorbedP2);
    BuildLedger.Add(Contender,Version,ELDBlueprintFact::HealthLost,Damage.HPLostP2);
}

void ALDCombatDirector::LedgerHealed(const FLDBodyCombatState& Body, int64 HealedP2)
{
    if (Body.Victim.Kind!=ELDVictimKind::Contender || HealedP2<=0 || LDCombatBlueprintLedgerDetail::IsTraining(Body)) { return; }
    const int32 Version=LedgerVersion(Body.Victim.EntityId);
    if (Version!=INDEX_NONE) { BuildLedger.Add(Body.Victim.EntityId,Version,ELDBlueprintFact::Healing,HealedP2); }
}

void ALDCombatDirector::LedgerShieldGranted(const FLDBodyCombatState& Body, int64 AmountP2)
{
    if (Body.Victim.Kind!=ELDVictimKind::Contender || AmountP2<=0 || LDCombatBlueprintLedgerDetail::IsTraining(Body)) { return; }
    const int32 Version=LedgerVersion(Body.Victim.EntityId);
    if (Version!=INDEX_NONE) { BuildLedger.Add(Body.Victim.EntityId,Version,ELDBlueprintFact::ShieldGranted,AmountP2); }
}

void ALDCombatDirector::LedgerDebtRepaid(const FLDBodyCombatState& Body, int64 RepaidP2)
{
    if (Body.Victim.Kind!=ELDVictimKind::Contender || RepaidP2<=0 || LDCombatBlueprintLedgerDetail::IsTraining(Body)) { return; }
    const int32 Version=LedgerVersion(Body.Victim.EntityId);
    if (Version!=INDEX_NONE) { BuildLedger.Add(Body.Victim.EntityId,Version,ELDBlueprintFact::DebtRepaid,RepaidP2); }
}

void ALDCombatDirector::LedgerRegenerated(const FLDBodyCombatState& Body, int64 RestoredP2)
{
    if (Body.Victim.Kind!=ELDVictimKind::Contender || RestoredP2<=0 || LDCombatBlueprintLedgerDetail::IsTraining(Body)) { return; }
    const int32 Version=LedgerVersion(Body.Victim.EntityId);
    if (Version!=INDEX_NONE) { BuildLedger.Add(Body.Victim.EntityId,Version,ELDBlueprintFact::Regeneration,RestoredP2); }
}

void ALDCombatDirector::LedgerComponentTriggered(const FLDBodyCombatState& Holder, FName Component)
{
    if (Holder.Victim.Kind!=ELDVictimKind::Contender || Component.IsNone() || LDCombatBlueprintLedgerDetail::IsTraining(Holder))
    {
        return;
    }
    const int32 Version=LedgerVersion(Holder.Victim.EntityId);
    if (Version!=INDEX_NONE) { BuildLedger.Add(Holder.Victim.EntityId,Version,ELDBlueprintFact::Triggers,1); }
}

void ALDCombatDirector::LedgerActionRefused(const FGuid& Entity, ELDCombatReject Reason)
{
    if (!LDCombatBlueprintLedgerDetail::IsGameplayRefusal(Reason)) { return; }
    const FLDBodyCombatState* Body=FindEntityBody(Entity);
    if (Body && LDCombatBlueprintLedgerDetail::IsTraining(*Body)) { return; }
    const int32 Version=LedgerVersion(Entity);
    if (Version!=INDEX_NONE) { BuildLedger.Add(Entity,Version,ELDBlueprintFact::Rejections,1); }
}