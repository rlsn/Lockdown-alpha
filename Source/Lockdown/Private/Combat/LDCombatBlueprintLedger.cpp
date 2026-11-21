#include "Combat/LDCombatDirector.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDLifeAuthority.h"

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
    case ELDCombatReject::MomentumShort:
    case ELDCombatReject::HealthShort:
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
    const ILDLifeAuthority* Mode=Authority();
    if (!Record || Record->Kind!=ELDVictimKind::Contender || !Mode) { return INDEX_NONE; }
    return BuildLedger.Observe(Contender,Record->Build,Mode->GetAuthorityNowMs());
}

void ALDCombatDirector::LedgerBuildInstalled(const FGuid& Contender, const FLDBuildProjectionInput& Previous)
{
    const FLDCombatEntityRecord* Record=FindRecord(Contender);
    const ILDLifeAuthority* Mode=Authority();
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
    const int64 CutP2=Damage.BraceCutP2;
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

void ALDCombatDirector::LedgerRegenerated(const FLDBodyCombatState& Body, int64 RestoredP2)
{
    if (Body.Victim.Kind!=ELDVictimKind::Contender || RestoredP2<=0 || LDCombatBlueprintLedgerDetail::IsTraining(Body)) { return; }
    const int32 Version=LedgerVersion(Body.Victim.EntityId);
    if (Version!=INDEX_NONE) { BuildLedger.Add(Body.Victim.EntityId,Version,ELDBlueprintFact::Regeneration,RestoredP2); }
}

void ALDCombatDirector::LedgerCardTriggered(const FLDBodyCombatState& Holder, FName Card)
{
    // Spec §6.5: a card trigger is one step of the chain it fired in.
    if (const ILDLifeAuthority* Mode=Authority()) { ChainStep(Holder.Victim.EntityId,Card,Mode->GetAuthorityNowMs()); }
    if (Holder.Victim.Kind!=ELDVictimKind::Contender || Card.IsNone() || LDCombatBlueprintLedgerDetail::IsTraining(Holder))
    {
        return;
    }
    ++CardTriggerCounts.FindOrAdd(Holder.Victim.EntityId).FindOrAdd(Card);
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

// The death recap and the per-source totals read the same committed hit as the ledger's damage facts: positive damage on
// another body, shield-absorbed part included, never private training.
void ALDCombatDirector::RecapHitCommitted(const FLDCombatHit& Hit, FLDBodyCombatState& Victim, const FLDDamageEvaluation& Damage, int64 NowMs)
{
    // A contender's hit on a training dummy feeds only its own training combo.
    if (Damage.DamageP2>0 && Victim.bTrainingDummy && Hit.CreditOwner.IsValid())
    {
        const FLDCombatEntityRecord* Trainee=FindRecord(Hit.CreditOwner);
        if (Trainee && Trainee->Kind==ELDVictimKind::Contender)
        {
            FTrainingCombo& Combo=TrainingCombos.FindOrAdd(Hit.CreditOwner);
            if (Combo.TotalP2>0 && Combo.LastAtMs>0 && NowMs-Combo.LastAtMs>=ALDGameMode::TrainingComboQuietMs)
            {
                Combo.PreviousTotalP2=Combo.TotalP2;
                Combo.Lines.Reset();
                Combo.TotalP2=0;
                Combo.FirstAtMs=NowMs;
            }
            if (Combo.TotalP2==0) { Combo.FirstAtMs=NowMs; }
            Combo.DummyId=Victim.Victim.EntityId;
            Combo.TotalP2+=Damage.DamageP2;
            LDDamageRecap::AddTotal(Combo.Lines,Hit.Source,Damage.DamageP2);
            Combo.LastAtMs=NowMs;
            Combo.bLiveDirty=true;
            Combo.bFinishPending=true;
        }
    }
    if (Damage.DamageP2<=0 || Hit.SourceEntity==Victim.Victim.EntityId || LDCombatBlueprintLedgerDetail::IsTraining(Victim)) { return; }
    const FLDCombatEntityRecord* Credited=Hit.CreditOwner.IsValid() && Hit.CreditOwner!=Victim.Victim.EntityId ? FindRecord(Hit.CreditOwner) : nullptr;
    const bool bContenderOwner=Credited && Credited->Kind==ELDVictimKind::Contender;
    if (bContenderOwner) { LDDamageRecap::AddTotal(DamageTotals.FindOrAdd(Hit.CreditOwner),Hit.Source,Damage.DamageP2); }
    if (Victim.Victim.Kind!=ELDVictimKind::Contender || Victim.bPendingDeath) { return; }
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!Content) { return; }
    FLDDamageLine Line;
    Line.Source=Hit.Source;
    Line.DamageP2=Damage.DamageP2;
    // Reaction damage keeps its rule chain: an Overload splash consumed nothing on this body; a setter that is not a single
    // contender stays unnamed (LockDeath further hides setters the victim cannot read).
    if (Hit.ReactionKind!=ELDCombatReaction::None)
    {
        Line.Reaction=uint8(Hit.ReactionKind);
        Line.Consumed=uint8(Hit.ReactionConsumed);
        const FLDCombatEntityRecord* Setter=Hit.ReactionConsumed!=ELDCombatElement::None && Hit.ReactionSetter.IsValid()
            ? FindRecord(Hit.ReactionSetter) : nullptr;
        if (Setter && Setter->Kind==ELDVictimKind::Contender) { Line.Setter=Hit.ReactionSetter; }
    }
    if (bContenderOwner) { Line.Contender=Hit.CreditOwner; }
    else if (Hit.SourceBody.Victim.Kind==ELDVictimKind::CampMember)
    {
        // The attacker's definition outlives its body through the persistent Combat record.
        if (const FLDCombatEntityRecord* Monster=FindRecord(Hit.SourceBody.Victim.EntityId)) { Line.MonsterId=Monster->MonsterAtLife(Hit.SourceBody.Victim.Life); }
    }
    LDDamageRecap::Record(Victim.DamageLog,Line,NowMs,Content->Get().Parameters.AssistWindowMs);
}

bool ALDCombatDirector::CardTriggersOf(const FGuid& Contender, TArray<FLDCardTriggerRow>& Out) const
{
    Out.Reset();
    const FLDCombatEntityRecord* Record=FindRecord(Contender);
    if (!Record || Record->Kind!=ELDVictimKind::Contender) { return false; }
    if (const TMap<FName,int32>* Counts=CardTriggerCounts.Find(Contender))
    {
        for (const TPair<FName,int32>& Pair : *Counts)
        {
            FLDCardTriggerRow& Row=Out.AddDefaulted_GetRef();
            Row.Card=Pair.Key;
            Row.Count=Pair.Value;
        }
    }
    Out.Sort([](const FLDCardTriggerRow& A, const FLDCardTriggerRow& B) { return A.Card.LexicalLess(B.Card); });
    return true;
}

void ALDCombatDirector::TakeFinishedTrainingCombos(int64 NowMs, int64 QuietMs, TMap<FGuid,FLDTrainingComboView>& Out)
{
    for (TPair<FGuid,FTrainingCombo>& Pair : TrainingCombos)
    {
        FTrainingCombo& Combo=Pair.Value;
        const bool bQuietFinished=Combo.bFinishPending && Combo.TotalP2>0 && NowMs-Combo.LastAtMs>=QuietMs;
        if (!Combo.bLiveDirty && !bQuietFinished) { continue; }
        LDDamageRecap::SortTotals(Combo.Lines);
        FLDTrainingComboView View;
        View.DummyId=Combo.DummyId;
        View.TotalP2=Combo.TotalP2;
        View.ElapsedMs=Combo.TotalP2>0 && Combo.LastAtMs>=Combo.FirstAtMs ? FMath::Max<int64>(1000,Combo.LastAtMs-Combo.FirstAtMs) : 0;
        View.PreviousTotalP2=Combo.PreviousTotalP2;
        View.bFinished=bQuietFinished;
        View.Lines=Combo.Lines;
        Combo.bLiveDirty=false;
        if (bQuietFinished) { Combo.bFinishPending=false; }
        Out.Add(Pair.Key,MoveTemp(View));
    }
}

bool ALDCombatDirector::DamageTotalsOf(const FGuid& Contender, TArray<FLDDamageLine>& Out) const
{
    Out.Reset();
    const FLDCombatEntityRecord* Record=FindRecord(Contender);
    if (!Record || Record->Kind!=ELDVictimKind::Contender) { return false; }
    if (const TArray<FLDDamageLine>* Totals=DamageTotals.Find(Contender)) { Out=*Totals; }
    LDDamageRecap::SortTotals(Out);
    return true;
}
