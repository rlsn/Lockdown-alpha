#include "Combat/LDCombatDirector.h"
#include "Core/LDLog.h"
#include "Combat/LDCombatMechanics.h"
#include "Combat/LDCombatSourceRules.h"
#include "Combat/LDControlRules.h"
#include "Combat/LDTacticalRules.h"
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDGameMode.h"
#include "Engine/World.h"

// #112 §5 mechanics on Combat's body records; the pure rules live in LDCombatMechanics.cpp.
namespace LDCombatMechanicsDetail
{
    double Cm(const FLDDemoRational& Value)
    { return Value.Denominator>0 ? double(Value.Numerator)/double(Value.Denominator) : 0.; }

    const FLDDemoEffectDefinition* FindOp(const FLDDemoRankDefinition& Rank, ELDDemoEffectOp Op)
    { return Rank.Effects.FindByPredicate([Op](const FLDDemoEffectDefinition& Effect) { return Effect.GetOp()==Op; }); }

    // floor(ValueP2 x Bp / 10000) without overflow for nonnegative inputs.
    int64 Scale(int64 ValueP2, int64 Bp)
    {
        if (ValueP2<=0 || Bp<=0) { return 0; }
        return ValueP2/10000*Bp+ValueP2%10000*Bp/10000;
    }

    // One damage-carrying hit from a body to a target, with the body's current AD/AP as its source values.
    FLDCombatHit MakeHit(const FLDBodyCombatState& Source, const FLDCombatSourceRef& Ref, const FLDCombatBodyRef& Target,
        const FGuid& EffectId)
    {
        FLDCombatHit Hit;
        Hit.HitId=FGuid::NewGuid();
        Hit.EffectId=EffectId.IsValid() ? EffectId : FGuid::NewGuid();
        Hit.Target=Target;
        Hit.SourceBody={Source.Victim,Source.Identity};
        Hit.SourceEntity=Source.Victim.EntityId;
        Hit.CreditOwner=Source.Victim.Kind==ELDVictimKind::Contender ? Source.Victim.EntityId : FGuid();
        Hit.Source=Ref;
        Hit.HitClass=ELDCombatHitClass::Ability;
        Hit.SourceADP2=Source.AttributeProjection.Final.ADP2;
        Hit.SourceAPP2=Source.AttributeProjection.Final.APP2;
        return Hit;
    }
}

ALDCharacter* ALDCombatDirector::ActorOf(const FLDBodyCombatState& Body) const
{
    for (const auto& Pair : Bodies) { if (&Pair.Value==&Body) { return Pair.Key.Get(); } }
    return nullptr;
}

const FLDDemoEffectDefinition* ALDCombatDirector::FindTrigger(const FLDAttributeProjection& Projection, ELDDemoEffectOp Op,
    FLDCombatSourceRef* OutSource) const
{
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!Content) { return nullptr; }
    FString Ignored;
    for (const FLDLearnedTriggerTerm& Term : Projection.LearnedTriggers)
    {
        const FLDDemoAbilityDefinition* Ability=nullptr;
        if (!Content->FindAbility(Term.AbilityId,Ability,Ignored) || !Ability) { continue; }
        const FLDDemoRankDefinition* Rank=Content->FindRank(*Ability,Term.Rank,Term.Level);
        if (const FLDDemoEffectDefinition* Effect=Rank ? LDCombatMechanicsDetail::FindOp(*Rank,Op) : nullptr)
        {
            if (OutSource) { OutSource->Kind=ELDCombatSourceKind::Learned; OutSource->DefinitionId=FName(*Term.AbilityId); }
            return Effect;
        }
    }
    // POLICY.DuplicateEquipmentPassive=OncePerFamily: one family answers once.
    for (const FLDEquipmentPassiveFamily& Family : Projection.EquipmentPassives)
    {
        const FLDDemoEquipmentDefinition* Equipment=nullptr;
        if (!Content->FindEquipment(Family.EquipmentId,Equipment,Ignored) || !Equipment || !Equipment->HasPassive) { continue; }
        // An upgraded copy answers with its upgraded terms (#112 §7).
        const TArray<FLDDemoEffectDefinition>* Effects=LDDemoEquipment::EffectivePassiveEffects(*Equipment,Family.bUpgraded);
        if (!Effects) { continue; }
        for (const FLDDemoEffectDefinition& Effect : *Effects)
        {
            if (Effect.GetOp()!=Op) { continue; }
            if (OutSource) { OutSource->Kind=ELDCombatSourceKind::Gear; OutSource->DefinitionId=FName(*Family.EquipmentId); }
            return &Effect;
        }
    }
    return nullptr;
}

bool ALDCombatDirector::HealBody(FLDBodyCombatState& Body, int64 AmountP2)
{
    if (Body.bPendingDeath || Body.Resources.HpP2<=0 || AmountP2<=0) { return false; }
    auto& Pool=Body.Resources;
    const int64 Before=Pool.HpP2;
    Pool.HpP2=AmountP2>=Pool.MaxHpP2-Pool.HpP2 ? Pool.MaxHpP2 : Pool.HpP2+AmountP2;
    if (Pool.HpP2>=Pool.MaxHpP2) { Body.HpRegenMicroP2=0; }
    if (Pool.HpP2==Before) { return false; }
    ++Pool.Revision;
    MarkViewChanged(Body.Victim.EntityId);
    return true;
}

bool ALDCombatDirector::GrantShieldTo(FLDBodyCombatState& Body, int64 AmountP2, int64 DurationMs, const FGuid& EffectId,
    const FGuid& SourceEntity, int64 NowMs, FString& Error)
{
    if (Body.bPendingDeath) { return true; }
    int64 Expires=0;
    if (!LDMatchLifecycle::AddDurationMs(NowMs,DurationMs,Expires,Error)) { return false; }
    // A weaker shield is a stated sub-effect outcome, not a failed release.
    if (!LDTactical::ApplyShield(Body.Shield,Body.Resources.ShieldP2,FLDCombatShield{EffectId,SourceEntity,Expires},AmountP2,NowMs))
    { return true; }
    Body.ShieldGrantedP2=AmountP2;
    ++Body.Resources.Revision;
    ++Body.StateRevision;
    MarkViewChanged(Body.Victim.EntityId);
    return true;
}

bool ALDCombatDirector::OnShieldBroken(FLDBodyCombatState& Holder, const FLDCombatHit& Hit, int64 GrantedP2, int64 NowMs, FString& Error)
{
    FLDCombatSourceRef Ref;
    const FLDDemoEffectDefinition* Retort=FindTrigger(Holder.AttributeProjection,ELDDemoEffectOp::ShieldBreakRetort,&Ref);
    const FLDBodyCombatState* Breaker=FindBody(Hit.SourceBody);
    if (!Retort || !Breaker || Breaker==&Holder || Breaker->bPendingDeath || Holder.bPendingDeath || NowMs<Holder.RetortReadyAtMs) { return true; }
    if (!LDMatchLifecycle::AddDurationMs(NowMs,Retort->IntervalMs,Holder.RetortReadyAtMs,Error)) { return false; }
    FLDCombatHit Answer=LDCombatMechanicsDetail::MakeHit(Holder,Ref,Hit.SourceBody,FGuid());
    Answer.BaseP2=LDCombatMechanicsDetail::Scale(GrantedP2,Retort->RatioBp);
    Answer.DamageType=ELDDemoDamageType::Magical;
    Answer.SlowBp=Retort->SecondaryRatioBp;
    Answer.SlowDurationMs=Retort->SecondaryDurationMs;
    Answer.bFromShieldTrigger=true;
    PendingHits.Add(Answer);
    return true;
}

bool ALDCombatDirector::ApplyKnockback(ALDCharacter& Target, FLDBodyCombatState& Body, const FLDCombatHit& Hit, int64 NowMs,
    bool& bApplied, FString& Error)
{
    bApplied=false;
    ALDCharacter* SourceActor=nullptr;
    FindBody(Hit.SourceBody,&SourceActor);
    const FVector From=Hit.bKnockbackFromAnchor || !SourceActor ? Hit.KnockbackAnchorCm : FootOf(*SourceActor);
    const double Yaw=SourceActor ? SourceActor->GetActorRotation().Yaw : Target.GetActorRotation().Yaw+180.;
    FVector Direction;
    if (!LDCombatMechanics::KnockbackDirection(From,FootOf(Target),Yaw,Direction))
    { Error=TEXT("A knockback has no finite direction."); return false; }
    auto* Movement=Cast<ULDCharacterMovementComponent>(Target.GetCharacterMovement());
    if (!Movement) { Error=TEXT("Knockback requires the Demo character movement."); return false; }
    // The control itself (window, interruptions, order drop, spray, assists) commits through the single control path.
    FLDControlApplication App;
    App.Kind=ELDControlKind::Knockback;
    App.EffectId=Hit.EffectId;
    App.SourceEntity=Hit.SourceEntity;
    App.SourceBody=Hit.SourceBody;
    App.CreditOwner=Hit.CreditOwner;
    App.DurationMs=Hit.KnockbackMs;
    if (!CommitControl(Body,App,NowMs,bApplied,Error)) { return false; }
    if (!bApplied) { return true; }
    // The same native root motion as a dash: integrated by the authority steps along the ground, stopped by blocking
    // geometry at the last legal position, never a teleport.
    if (!Movement->StartDemoDash(Direction,Hit.KnockbackCm,double(Hit.KnockbackMs)/1000.,Error))
    { Error=TEXT("Knockback: ")+Error; return false; }
    UE_LOG(LogLDCombat,Display,TEXT("LD_COMBAT_KNOCKBACK victim=%s source=%s cm=%.0f ms=%lld"),*Body.Victim.EntityId.ToString(),
        *Hit.SourceEntity.ToString(),Hit.KnockbackCm,Hit.KnockbackMs);
    return true;
}

bool ALDCombatDirector::OnHitLanded(FLDBodyCombatState& Target, const FLDCombatHit& Hit, int64 DamageP2, int64 NowMs, FString& Error)
{
    FLDBodyCombatState* Source=FindBody(Hit.SourceBody);
    if (!Source || Source==&Target || Source->bPendingDeath || DamageP2<=0 || Hit.bAddedDamage || Hit.bReflect) { return true; }
    const bool bBasic=Hit.HitClass==ELDCombatHitClass::BasicPrimary;
    // item_skateboard: a landed basic hurries its owner for a moment.
    FLDCombatSourceRef SkateRef;
    if (const FLDDemoEffectDefinition* Skate=bBasic ? FindTrigger(Source->AttributeProjection,ELDDemoEffectOp::HitHaste,&SkateRef) : nullptr)
    {
        FLDCombatTimedBonus Haste;
        Haste.EffectId=FGuid::NewGuid();
        Haste.SourceDefinition=SkateRef.DefinitionId;
        FLDDemoStatBonus& Faster=Haste.Stats.AddDefaulted_GetRef();
        Faster.Stat=ELDDemoBonusStat::MoveSpeedBonusRatio;
        Faster.Amount=Skate->RatioBp;
        if (!LDMatchLifecycle::AddDurationMs(NowMs,Skate->DurationMs,Haste.ExpiresAtMs,Error)) { return false; }
        LDTactical::ApplyTimedBonus(Source->TimedBonuses,Haste);
        ++Source->StateRevision;
        MarkViewChanged(Source->Victim.EntityId);
    }
    return true;
}

bool ALDCombatDirector::ExpireMechanics(FLDBodyCombatState& Body, int64 NowMs)
{
    return Body.Knockbacks.RemoveAll([NowMs](const FLDCombatRoot& Row) { return Row.ExpiresAtMs<=NowMs; })>0;
}

int64 ALDCombatDirector::NextMechanicsDueMs(int64 NowMs) const
{
    int64 Due=MAX_int64;
    const auto Consider=[&Due,NowMs](int64 At) { if (At>NowMs && At<Due) { Due=At; } };
    for (const auto& Pair : Bodies)
    {
        const FLDBodyCombatState& Body=Pair.Value;
        if (Body.bPendingDeath) { continue; }
        for (const auto& Row : Body.Knockbacks) { Consider(Row.ExpiresAtMs); }
    }
    for (const FLDCombatZone& Zone : Zones)
    {
        if (Zone.Pulsed<Zone.Count) { Consider(Zone.StartMs+Zone.DelayMs+Zone.IntervalMs*Zone.Pulsed); }
        if (Zone.bSlow) { Consider(Zone.NextSlowAtMs); }
        if (Zone.BurnStacks>0 && Zone.NextBurnAtMs<Zone.EndMs) { Consider(Zone.NextBurnAtMs); }
        Consider(Zone.EndMs);
    }
    return Due;
}

bool ALDCombatDirector::AdvanceMechanics(int64 NowMs, FString& Error)
{
    // Ground zones: pulses, the kept slow and the end root.
    for (int32 Index=Zones.Num()-1; Index>=0; --Index)
    {
        FLDCombatZone& Zone=Zones[Index];
        FLDBodyCombatState* Holder=FindBody(Zone.SourceBody);
        const auto Inside=[this,&Zone,Holder](TArray<FLDCombatBodyRef>& Out, FString& InError)
        { return CollectAreaTargets(Zone.CentreCm,Zone.RadiusCm,Holder,Zone.SourceEntity,Out,InError); };
        const auto Emit=[this,&Zone](const FLDCombatBodyRef& Target)->FLDCombatHit&
        {
            FLDCombatHit& Hit=PendingHits.AddDefaulted_GetRef();
            Hit.HitId=FGuid::NewGuid();
            Hit.EffectId=Zone.EffectId;
            Hit.Target=Target;
            Hit.SourceBody=Zone.SourceBody;
            Hit.SourceEntity=Zone.SourceEntity;
            Hit.CreditOwner=Zone.CreditOwner;
            Hit.Source=Zone.Source;
            Hit.HitClass=ELDCombatHitClass::Ability;
            Hit.SourceADP2=Zone.SourceADP2;
            Hit.SourceAPP2=Zone.SourceAPP2;
            return Hit;
        };
        while (Zone.Pulsed<Zone.Count && Zone.StartMs+Zone.DelayMs+Zone.IntervalMs*Zone.Pulsed<=NowMs)
        {
            ++Zone.Pulsed;
            if (Zone.bDamage)
            {
                TArray<FLDCombatBodyRef> Targets;
                if (!Inside(Targets,Error)) { Error=TEXT("Zone pulse: ")+Error; return false; }
                for (const FLDCombatBodyRef& Target : Targets)
                {
                    FLDCombatHit& Hit=Emit(Target);
                    Hit.BaseP2=Zone.BaseP2;
                    Hit.ADRatioBp=Zone.ADRatioBp;
                    Hit.APRatioBp=Zone.APRatioBp;
                    Hit.DamageType=Zone.DamageType;
                    Hit.bPeriodic=true;
                }
            }
        }
        while (Zone.bSlow && Zone.NextSlowAtMs>0 && Zone.NextSlowAtMs<=NowMs && Zone.NextSlowAtMs<Zone.EndMs)
        {
            TArray<FLDCombatBodyRef> Targets;
            if (!Inside(Targets,Error)) { Error=TEXT("Zone slow: ")+Error; return false; }
            const FGuid CheckId=FGuid::NewGuid();
            for (const FLDCombatBodyRef& Target : Targets)
            {
                FLDCombatHit& Hit=Emit(Target);
                Hit.EffectId=CheckId;
                Hit.SlowBp=Zone.SlowBp;
                // Kept while inside and for the linger after leaving.
                Hit.SlowDurationMs=Zone.SlowCheckMs+Zone.SlowLingerMs;
                Hit.bSilent=true;
            }
            Zone.NextSlowAtMs+=Zone.SlowCheckMs;
        }
        while (Zone.BurnStacks>0 && Zone.BurnIntervalMs>0 && Zone.NextBurnAtMs<=NowMs && Zone.NextBurnAtMs<Zone.EndMs)
        {
            TArray<FLDCombatBodyRef> Targets;
            if (!Inside(Targets,Error)) { Error=TEXT("Zone burn: ")+Error; return false; }
            const FGuid PulseId=FGuid::NewGuid();
            for (const FLDCombatBodyRef& Target : Targets)
            {
                FLDCombatHit& Hit=Emit(Target);
                Hit.EffectId=PulseId;
                Hit.BurnStacks=Zone.BurnStacks;
                Hit.BurnLevel=Zone.BurnLevel;
                Hit.BurnAPP2=Zone.BurnAPP2;
                Hit.bPeriodic=true;
                Hit.bSilent=true;
            }
            Zone.NextBurnAtMs+=Zone.BurnIntervalMs;
        }
        if (Zone.EndMs<=NowMs)
        {
            if (Zone.EndRootMs>0)
            {
                TArray<FLDCombatBodyRef> Targets;
                if (!Inside(Targets,Error)) { Error=TEXT("Zone end: ")+Error; return false; }
                for (const FLDCombatBodyRef& Target : Targets) { Emit(Target).RootDurationMs=Zone.EndRootMs; }
            }
            Zones.RemoveAt(Index);
        }
    }
    return true;
}

void ALDCombatDirector::FillMechanicsTemplate(const FLDDemoRankDefinition& Rank, const FVector& OriginFoot, FLDCombatHit& Template) const
{
    using namespace LDCombatMechanicsDetail;
    if (const FLDDemoEffectDefinition* Push=FindOp(Rank,ELDDemoEffectOp::Knockback))
    {
        // The push leaves from the caster's foot at release.
        Template.KnockbackCm=Cm(Push->DistanceCm);
        Template.KnockbackMs=Push->DurationMs;
        Template.bKnockbackFromAnchor=true;
        Template.KnockbackAnchorCm=OriginFoot;
        // #147: a push that travels less than this met a wall and stuns.
        Template.WallCheckCm=Cm(Push->RadiusCm);
        Template.WallStunMs=Push->SecondaryDurationMs;
    }
}

bool ALDCombatDirector::ReleaseMechanics(FLDBodyCombatState& Body, const FLDCombatSourceRef& Source, const FLDDemoRankDefinition& Rank,
    const FVector& OriginFoot, const FVector& TargetPoint, FString& Error)
{
    using namespace LDCombatMechanicsDetail;
    ALDGameMode* Mode=Core();
    if (!Mode) { Error=TEXT("Release mechanics require the current core."); return false; }
    const int64 Now=Mode->GetAuthorityNowMs();
    if (const FLDDemoEffectDefinition* ZoneDef=FindOp(Rank,ELDDemoEffectOp::Zone))
    {
        FLDCombatZone Zone;
        Zone.EffectId=FGuid::NewGuid();
        Zone.SourceBody={Body.Victim,Body.Identity};
        Zone.SourceEntity=Body.Victim.EntityId;
        Zone.CreditOwner=Body.Victim.Kind==ELDVictimKind::Contender ? Body.Victim.EntityId : FGuid();
        Zone.Source=Source;
        // A ground point zone sits at the requested point (its height taken from the caster's foot); a dash leaves
        // its zone at the start foot.
        Zone.CentreCm=FindOp(Rank,ELDDemoEffectOp::Dash) ? OriginFoot : FVector(TargetPoint.X,TargetPoint.Y,OriginFoot.Z);
        Zone.RadiusCm=Cm(ZoneDef->RadiusCm);
        Zone.StartMs=Now;
        Zone.DelayMs=ZoneDef->DelayMs;
        Zone.IntervalMs=ZoneDef->IntervalMs;
        Zone.Count=int32(ZoneDef->Count);
        if (!LDMatchLifecycle::AddDurationMs(Now,ZoneDef->DurationMs,Zone.EndMs,Error)) { return false; }
        Zone.SourceADP2=Body.AttributeProjection.Final.ADP2;
        Zone.SourceAPP2=Body.AttributeProjection.Final.APP2;
        if (const FLDDemoEffectDefinition* Slow=FindOp(Rank,ELDDemoEffectOp::ZoneSlow))
        {
            Zone.bSlow=true;
            Zone.SlowBp=Slow->RatioBp;
            Zone.SlowCheckMs=Slow->IntervalMs;
            Zone.SlowLingerMs=Slow->SecondaryDurationMs;
            Zone.NextSlowAtMs=Now;
        }
        if (const FLDDemoEffectDefinition* Root=FindOp(Rank,ELDDemoEffectOp::ZoneRoot)) { Zone.EndRootMs=Root->DurationMs; }
        // #147: burn layers on the enemies inside, every interval from the zone's start.
        if (const FLDDemoEffectDefinition* Burn=FindOp(Rank,ELDDemoEffectOp::ZoneBurn))
        {
            FLDCombatBurnLayer Layer;
            if (!BurnLayerOf(Body,Source,Layer,Error)) { return false; }
            Zone.BurnStacks=Burn->Count;
            Zone.BurnIntervalMs=Burn->IntervalMs;
            Zone.NextBurnAtMs=Now;
            Zone.BurnLevel=Layer.Level;
            Zone.BurnAPP2=Layer.SourceAPP2;
        }
        if (const FLDDemoEffectDefinition* Damage=FindOp(Rank,ELDDemoEffectOp::ZoneDamage))
        {
            Zone.bDamage=true;
            Zone.BaseP2=Damage->BaseP2;
            Zone.ADRatioBp=Damage->ADRatioBp;
            Zone.APRatioBp=Damage->APRatioBp;
            Zone.DamageType=Damage->DamageType;
        }
        Zones.Add(MoveTemp(Zone));
    }
    return true;
}

void ALDCombatDirector::AppendMechanicsStatuses(const FLDBodyCombatState& Body, int64 NowMs, TArray<FLDObservedStatus>& Out) const
{
    if (const int64 KnockUntil=LDTactical::ActiveRootUntilMs(Body.Knockbacks,NowMs); KnockUntil>0)
    { Out.Add({uint8(ELDDemoStatusId::Knockback),1,KnockUntil}); }
}
uint64 ALDCombatDirector::ReadKnockbackSerial(const FLDCombatBodyRef& Ref) const
{
    const FLDBodyCombatState* Body=FindBody(Ref);
    return Body ? Body->KnockbackSerial : 0;
}
