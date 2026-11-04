#include "Combat/LDCombatDirector.h"
#include "Combat/LDCombatSourceRules.h"
#include "Combat/LDTacticalRules.h"
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDemoContentIds.h"
#include "Framework/LDGameMode.h"
#include "Engine/World.h"

namespace LDCombatLifeDetail
{
    double ToDouble(const FLDDemoRational& Value)
    { return Value.Denominator>0 ? double(Value.Numerator)/double(Value.Denominator) : 0.; }

    void ApplyMoveSpeed(ALDCharacter& Body, const FLDProjectedAttributes& Final)
    {
        const double Speed=ToDouble(Final.MoveSpeedCmPerSec);
        if (FMath::IsFinite(Speed) && Speed>0 && Speed<=MAX_flt) { Body.GetCharacterMovement()->MaxWalkSpeed=float(Speed); }
    }
}

const FLDBodyCombatState* ALDCombatDirector::StateFor(const ALDCharacter* Body) const
{ return Bodies.Find(const_cast<ALDCharacter*>(Body)); }

void ALDCombatDirector::AppendObservedStatuses(const ALDCharacter& Actor, int64 NowMs, TArray<FLDObservedStatus>& Out) const
{
    const auto* Body=StateFor(&Actor);
    if (!Body || Body->bPendingDeath) { return; }
    // Several Root instances show as one cue with the latest remaining deadline.
    if (const int64 RootUntil=LDTactical::ActiveRootUntilMs(Body->Roots,NowMs); RootUntil>0)
    { Out.Add({uint8(ELDDemoStatusId::Root),1,RootUntil}); }
    if (Body->Shield.IsSet() && Body->Shield->ExpiresAtMs>NowMs && Body->Resources.ShieldP2>0)
    { Out.Add({uint8(ELDDemoStatusId::Shield),1,Body->Shield->ExpiresAtMs}); }
}

bool ALDCombatDirector::PrepareContenderBody(ALDCharacter& Body,const FLDVictimKey& Victim,
    const FLDGameplayWritePermit& Permit,FLDBodyCombatState& Out,FString& Error) const
{
    Out={};
    const auto* Mode=Core();
    const auto* Content=Catalog();
    if (!Mode || !Content || Mode->ValidateWritePermit(Permit,ELDCommitPhase::RespawnsAndWorld)!=ELDCommitReject::None
        || !Victim.IsValid() || Victim.Kind!=ELDVictimKind::Contender
        || !Body.HasAuthority() || Body.GetWorld()!=GetWorld() || Bodies.Contains(&Body))
    {
        Error=TEXT("Contender body preparation requires a new authority body and the current RespawnsAndWorld permit.");
        return false;
    }
    for (const auto& Pair : Bodies)
    {
        if (Pair.Value.Victim.EntityId==Victim.EntityId)
        { Error=TEXT("This contender already has a registered Combat body."); return false; }
    }
    // A new life starts from the persistent build; only the very first life may build the empty level-1 projection.
    const auto* Record=FindRecord(Victim.EntityId);
    FLDAttributeProjection Projection;
    FLDObservedPublicFacts Facts;
    Facts.Level=1;
    if (Record)
    {
        Projection=Record->Projection;
        Facts.Level=Record->Build.Level;
        for (const auto& Item : Record->Build.Equipment)
        {
            const FLDDemoEquipmentDefinition* Definition=nullptr;
            if (!Content->FindEquipment(Item.DefinitionId.ToString(),Definition,Error)) { return false; }
            const int32 Slot=Item.Entry==ELDCombatEntry::Item1 ? 0 : Item.Entry==ELDCombatEntry::Item2 ? 1 : INDEX_NONE;
            if (Slot==INDEX_NONE) { Error=TEXT("Persistent equipment has no item slot."); return false; }
            Facts.Equipment.Slots[Slot]=Definition->NativeId;
        }
    }
    else
    {
        if (Victim.Life!=1) { Error=TEXT("Only the first contender life may start without a persistent Combat record."); return false; }
        const FLDAttributeBuildValues Initial(1,{},{},{});
        const auto Result=LDAttributeProjection::Project(Initial,*Content);
        if (!Result.IsSuccess())
        {
            Error=TEXT("Initial attributes are invalid: ")+Result.Error.Field+TEXT(": ")+Result.Error.Message;
            return false;
        }
        Projection=Result.Value.GetValue();
    }
    const auto& Attributes=Projection.Final;
    if (Attributes.MaxHPP2<=0 || Attributes.MaxMPP2<0)
    {
        Error=TEXT("Contender resources are not defined by the validated Demo content.");
        return false;
    }
    const auto& Parameters=Content->Get().Parameters;
    FLDBasicTimingInput TimingInput;
    TimingInput.BaseAttackSpeed=Parameters.AttackSpeed.Base;
    TimingInput.CurrentAttackSpeed=Attributes.AttackSpeed;
    TimingInput.BaseWindupMs=Parameters.BasicAttackWindupMs;
    const auto Timing=LDTactical::EvaluateBasicTiming(TimingInput,*Content);
    if (!Timing.IsSuccess())
    {
        Error=TEXT("Basic timing is invalid: ")+Timing.Error.Field+TEXT(": ")+Timing.Error.Message;
        return false;
    }
    FLDBodyCombatState State;
    State.Identity=FGuid::NewGuid();
    State.Victim=Victim;
    // Every life (initial or respawn) starts with full resources at the newest maxima.
    State.Resources.HpP2=State.Resources.MaxHpP2=Attributes.MaxHPP2;
    State.Resources.MpP2=State.Resources.MaxMpP2=Attributes.MaxMPP2;
    State.Resources.Revision=1;
    State.BasicTiming=Timing.Value.GetValue();
    State.AttributeProjection=MoveTemp(Projection);
    State.AttributeRevision=1;
    State.PublicFacts=MoveTemp(Facts);
    // The out-of-combat window and regeneration both start at this life's actual commit time.
    State.LastCombatAtMs=State.RegenThroughMs=Mode->GetAuthorityNowMs();
    Out=MoveTemp(State);
    Error.Reset();
    return true;
}

bool ALDCombatDirector::PrepareCampMemberBody(ALDCharacter& Body,const FLDVictimKey& Victim,FName MonsterId,
    const FLDGameplayWritePermit& Permit,FLDBodyCombatState& Out,FString& Error) const
{
    Out={};
    const auto* Mode=Core();
    const auto* Content=Catalog();
    if (!Mode || !Content || Mode->ValidateWritePermit(Permit,ELDCommitPhase::RespawnsAndWorld)!=ELDCommitReject::None
        || !Victim.IsValid() || Victim.Kind!=ELDVictimKind::CampMember
        || !Body.HasAuthority() || Body.GetWorld()!=GetWorld() || Bodies.Contains(&Body))
    {
        Error=TEXT("Camp body preparation requires a new authority body and the current RespawnsAndWorld permit.");
        return false;
    }
    for (const auto& Pair : Bodies)
    {
        if (Pair.Value.Victim.EntityId==Victim.EntityId)
        { Error=TEXT("This camp member already has a registered Combat body."); return false; }
    }
    const FLDDemoMonsterDefinition* Monster=nullptr;
    if (!Content->FindMonster(MonsterId.ToString(),Monster,Error)) { return false; }
    if (Monster->HPP2<=0 || Monster->MaxMPP2<0 || Monster->AttackWindupMs<=0 || Monster->AttackIntervalMs<=Monster->AttackWindupMs)
    {
        Error=TEXT("The approved monster definition has no executable body values.");
        return false;
    }
    FLDBodyCombatState State;
    State.Identity=FGuid::NewGuid();
    State.Victim=Victim;
    State.MonsterId=MonsterId;
    auto& Final=State.AttributeProjection.Final;
    Final.MaxHPP2=Monster->HPP2;
    Final.MaxMPP2=Monster->MaxMPP2;
    Final.ADP2=Monster->AttackP2;
    Final.APP2=Monster->APP2;
    Final.ArmorP2=Monster->ArmorP2;
    Final.MagicResistP2=Monster->MagicResistP2;
    Final.AttackSpeed=Monster->AttackSpeed;
    Final.MoveSpeedCmPerSec=Monster->MoveSpeedCmPerSec;
    Final.AttackRangeCm=Monster->AttackRangeCm;
    Final.HPRegenPerSec=Monster->HPRegenPerSec;
    Final.MPRegenPerSec=Monster->MPRegenPerSec;
    Final.CritChanceBp=Monster->CritChanceBp;
    Final.CooldownHasteP2=0;
    State.Resources.HpP2=State.Resources.MaxHpP2=Monster->HPP2;
    State.Resources.MpP2=State.Resources.MaxMpP2=Monster->MaxMPP2;
    State.Resources.Revision=1;
    State.BasicTiming.IntervalMs=Monster->AttackIntervalMs;
    State.BasicTiming.WindupMs=Monster->AttackWindupMs;
    State.AttributeRevision=1;
    FLDObservedPublicFacts Facts;
    Facts.CreatureId=Monster->NativeId;
    State.PublicFacts=MoveTemp(Facts);
    State.LastCombatAtMs=State.RegenThroughMs=Mode->GetAuthorityNowMs();
    Out=MoveTemp(State);
    Error.Reset();
    return true;
}

bool ALDCombatDirector::InstallCombatBody(ALDCharacter& Body,FLDBodyCombatState&& Prepared,
    const FLDGameplayWritePermit& Permit,FLDCombatBodyRef& Out,FString& Error)
{
    Out={};
    const auto* Mode=Core();
    if (!Mode || Mode->ValidateWritePermit(Permit,ELDCommitPhase::RespawnsAndWorld)!=ELDCommitReject::None
        || !Prepared.Identity.IsValid() || !Prepared.Victim.IsValid() || !Prepared.Resources.Revision
        || !Prepared.AttributeRevision || !Prepared.PublicFacts.IsSet() || Bodies.Contains(&Body))
    {
        Error=TEXT("Prepared body lost its registration preconditions.");
        return false;
    }
    for (const auto& Pair : Bodies)
    {
        if (Pair.Value.Victim.EntityId==Prepared.Victim.EntityId)
        { Error=TEXT("Prepared entity already has a registered body."); return false; }
    }
    if (!Records.Contains(Prepared.Victim.EntityId))
    {
        FLDCombatEntityRecord Record;
        Record.Kind=Prepared.Victim.Kind;
        Record.Entity=Prepared.Victim.EntityId;
        Record.MonsterId=Prepared.MonsterId;
        if (Record.Kind==ELDVictimKind::Contender)
        {
            Record.Build.Level=1;
            Record.Projection=Prepared.AttributeProjection;
            // D19: the one persistent innate dash track, ready at match start and kept for the whole match.
            FLDCombatTrack& Dash=Record.Tracks.AddDefaulted_GetRef();
            Dash.IssuedCooldownId=FGuid::NewGuid();
            Dash.Kind=ELDCombatSourceKind::Innate;
            Dash.DefinitionId=LDCombatSourceRules::InnateDashId();
            Dash.bBound=true;
        }
        Records.Add(Record.Entity,MoveTemp(Record));
    }
    else if (Records.FindChecked(Prepared.Victim.EntityId).Kind!=Prepared.Victim.Kind
        || Records.FindChecked(Prepared.Victim.EntityId).MonsterId!=Prepared.MonsterId)
    {
        Error=TEXT("A Combat entity cannot change its victim kind or monster definition.");
        return false;
    }
    Out={Prepared.Victim,Prepared.Identity};
    LDCombatLifeDetail::ApplyMoveSpeed(Body,Prepared.AttributeProjection.Final);
    const FGuid Entity=Prepared.Victim.EntityId;
    Bodies.Add(&Body,MoveTemp(Prepared));
    MarkViewChanged(Entity);
    Error.Reset();
    return true;
}

void ALDCombatDirector::RemoveRegisteredBody(const FLDCombatBodyRef& Ref)
{
    for (auto It=Bodies.CreateIterator();It;++It)
    {
        if (It.Value().Victim==Ref.Victim && It.Value().Identity==Ref.BodyIdentity)
        {
            const FGuid Entity=Ref.Victim.EntityId;
            // Unreleased work dies with the life; released independent effects stay in Projectiles/PendingHits.
            It.RemoveCurrent();
            MarkViewChanged(Entity);
            return;
        }
    }
    UE_LOG(LogTemp,Error,TEXT("LD_DEMO_BODY_RETIRE_MISMATCH"));
}

void ALDCombatDirector::CancelEntityEffects(const FGuid& Entity)
{
    // Leaving cancels every unsettled effect of the entity; already committed damage and receipts stay.
    Projectiles.RemoveAll([&Entity](const FLDCombatProjectile& Projectile) { return Projectile.SourceEntity==Entity; });
    PendingHits.RemoveAll([&Entity](const FLDCombatHit& Hit) { return Hit.SourceEntity==Entity; });
    for (auto& Pair : Bodies)
    {
        auto& Body=Pair.Value;
        const int32 Removed=Body.Roots.RemoveAll([&Entity](const FLDCombatRoot& Root) { return Root.SourceEntity==Entity; });
        if (Removed>0)
        {
            ++Body.StateRevision;
            MarkViewChanged(Body.Victim.EntityId);
        }
    }
    if (auto* Record=FindRecord(Entity)) { Record->Pending.Reset(); }
}
