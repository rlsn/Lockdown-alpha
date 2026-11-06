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
#include "Region/LDMapRegistry.h"
#include "Vision/LDVisionDirector.h"
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
        const FLDDemoRankDefinition* Rank=Ability->Ranks.FindByPredicate([&Term](const FLDDemoRankDefinition& Row) { return Row.Rank==int32(Term.Rank); });
        if (const FLDDemoEffectDefinition* Effect=Rank ? LDCombatMechanicsDetail::FindOp(*Rank,Op) : nullptr)
        {
            if (OutSource) { OutSource->Kind=ELDCombatSourceKind::Learned; OutSource->DefinitionId=FName(*Term.AbilityId); OutSource->ItemInstance.Invalidate(); }
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
            if (OutSource) { OutSource->Kind=ELDCombatSourceKind::Gear; OutSource->DefinitionId=FName(*Family.EquipmentId); OutSource->ItemInstance.Invalidate(); }
            return &Effect;
        }
    }
    return nullptr;
}

bool ALDCombatDirector::HealBody(FLDBodyCombatState& Body, int64 AmountP2, int64 NowMs)
{
    if (Body.bPendingDeath || Body.Resources.HpP2<=0 || AmountP2<=0) { return false; }
    const int64 Heal=LDCombatMechanics::ReduceByBp(AmountP2,LDCombatMechanics::StrongestHealCutBp(Body.HealCuts,NowMs));
    auto& Pool=Body.Resources;
    const int64 Before=Pool.HpP2;
    Pool.HpP2=Heal>=Pool.MaxHpP2-Pool.HpP2 ? Pool.MaxHpP2 : Pool.HpP2+Heal;
    if (Pool.HpP2>=Pool.MaxHpP2) { Body.HpRegenMicroP2=0; }
    if (Pool.HpP2==Before) { return false; }
    ++Pool.Revision;
    MarkViewChanged(Body.Victim.EntityId);
    return true;
}

bool ALDCombatDirector::GrantShieldTo(FLDBodyCombatState& Body, int64 AmountP2, int64 DurationMs, const FGuid& EffectId,
    const FGuid& SourceEntity, bool bFromShieldTrigger, int64 NowMs, FString& Error)
{
    if (Body.bPendingDeath) { return true; }
    const int64 Amount=LDCombatMechanics::ReduceByBp(AmountP2,LDCombatMechanics::StrongestHealCutBp(Body.HealCuts,NowMs));
    int64 Expires=0;
    if (!LDMatchLifecycle::AddDurationMs(NowMs,DurationMs,Expires,Error)) { return false; }
    // A weaker shield is a stated sub-effect outcome, not a failed release.
    const ELDShieldGrant Grant=LDCombatMechanics::GrantShield(Body.Shield,Body.Resources.ShieldP2,
        FLDCombatShield{EffectId,SourceEntity,Expires},Amount,NowMs);
    if (Grant==ELDShieldGrant::Rejected) { return true; }
    Body.ShieldGrantedP2=Amount;
    ++Body.Resources.Revision;
    ++Body.StateRevision;
    MarkViewChanged(Body.Victim.EntityId);
    // R1: only replacing a shield that still held an amount is a refresh.
    return Grant!=ELDShieldGrant::Refreshed || bFromShieldTrigger || PulseShieldEvent(Body,NowMs,Error);
}

bool ALDCombatDirector::PulseShieldEvent(FLDBodyCombatState& Holder, int64 NowMs, FString& Error)
{
    FLDCombatSourceRef Ref;
    const FLDDemoEffectDefinition* Pulse=FindTrigger(Holder.AttributeProjection,ELDDemoEffectOp::ShieldPulse,&Ref);
    ALDCharacter* Actor=ActorOf(Holder);
    if (!Pulse || !Actor || Holder.bPendingDeath || NowMs<Holder.ShieldPulseReadyAtMs) { return true; }
    if (!LDMatchLifecycle::AddDurationMs(NowMs,Pulse->IntervalMs,Holder.ShieldPulseReadyAtMs,Error)) { return false; }
    TArray<FLDCombatBodyRef> Targets;
    if (!CollectAreaTargets(FootOf(*Actor),LDCombatMechanicsDetail::Cm(Pulse->RadiusCm),&Holder,Holder.Victim.EntityId,Targets,Error))
    { Error=TEXT("Shield pulse: ")+Error; return false; }
    const FGuid EffectId=FGuid::NewGuid();
    for (const FLDCombatBodyRef& Target : Targets)
    {
        FLDCombatHit Hit=LDCombatMechanicsDetail::MakeHit(Holder,Ref,Target,EffectId);
        Hit.BaseP2=Pulse->BaseP2;
        Hit.ADRatioBp=Pulse->ADRatioBp;
        Hit.APRatioBp=Pulse->APRatioBp;
        Hit.DamageType=Pulse->DamageType;
        Hit.SlowBp=Pulse->SecondaryRatioBp;
        Hit.SlowDurationMs=Pulse->SecondaryDurationMs;
        // A pulse's slow never grants shields (#112 §5: no shield gain from pulse- or shield-triggered effects).
        Hit.bFromShieldTrigger=true;
        PendingHits.Add(Hit);
    }
    ReduceCooldown(Holder.Victim.EntityId,ELDCombatEntry::Q,Pulse->CooldownCutMs);
    ReduceCooldown(Holder.Victim.EntityId,ELDCombatEntry::W,Pulse->CooldownCutMs);
    ++Holder.StateRevision;
    MarkViewChanged(Holder.Victim.EntityId);
    return true;
}

bool ALDCombatDirector::OnShieldBroken(FLDBodyCombatState& Holder, const FLDCombatHit& Hit, int64 GrantedP2, int64 NowMs, FString& Error)
{
    if (!PulseShieldEvent(Holder,NowMs,Error)) { return false; }
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

bool ALDCombatDirector::GrantThickSkin(FLDBodyCombatState& Body, int64 NowMs, FString& Error)
{
    const FLDDemoEffectDefinition* Skin=FindTrigger(Body.AttributeProjection,ELDDemoEffectOp::MobilityShield);
    if (!Skin || Body.bPendingDeath || NowMs<Body.MobilityShieldReadyAtMs) { return true; }
    if (!LDMatchLifecycle::AddDurationMs(NowMs,Skin->IntervalMs,Body.MobilityShieldReadyAtMs,Error)) { return false; }
    const int64 Before=Body.Resources.ShieldP2;
    if (!GrantShieldTo(Body,Skin->BaseP2,Skin->DurationMs,FGuid::NewGuid(),Body.Victim.EntityId,false,NowMs,Error)) { return false; }
    if (Skin->RatioBp>0 && Body.Resources.ShieldP2!=Before)
    {
        Body.ShieldBonusP2=LDCombatMechanicsDetail::Scale(Body.ShieldGrantedP2,Skin->RatioBp);
        if (!LDMatchLifecycle::AddDurationMs(NowMs,Skin->SecondaryDurationMs,Body.ShieldBonusUntilMs,Error)) { return false; }
    }
    return true;
}

bool ALDCombatDirector::OnMobilityUsed(FLDBodyCombatState& Body, int64 NowMs, FString& Error)
{
    for (FLDCombatDot& Dot : Body.Dots)
    {
        if (!Dot.bPunish || Dot.ExpiresAtMs<=NowMs) { continue; }
        Dot.bPunish=false;
        const FLDBodyCombatState* Source=FindBody(Dot.SourceBody);
        FLDCombatHit Hit;
        Hit.HitId=FGuid::NewGuid();
        Hit.EffectId=Dot.EffectId;
        Hit.Target={Body.Victim,Body.Identity};
        Hit.SourceBody=Dot.SourceBody;
        Hit.SourceEntity=Dot.SourceEntity;
        Hit.CreditOwner=Dot.CreditOwner;
        Hit.Source=Dot.Source;
        Hit.HitClass=ELDCombatHitClass::Ability;
        Hit.BaseP2=Dot.PunishBaseP2;
        Hit.ADRatioBp=Dot.PunishADRatioBp;
        Hit.APRatioBp=Dot.PunishAPRatioBp;
        Hit.DamageType=Dot.DamageType;
        Hit.SourceADP2=Source ? Source->AttributeProjection.Final.ADP2 : Dot.SourceADP2;
        Hit.SourceAPP2=Source ? Source->AttributeProjection.Final.APP2 : Dot.SourceAPP2;
        Hit.bAddedDamage=true;
        PendingHits.Add(Hit);
    }
    return GrantThickSkin(Body,NowMs,Error);
}

bool ALDCombatDirector::OnControlApplied(const FLDCombatHit& Hit, int64 NowMs, FString& Error)
{
    if (Hit.bFromShieldTrigger) { return true; }
    FLDBodyCombatState* Source=FindBody(Hit.SourceBody);
    return !Source || Source->Victim.EntityId==Hit.Target.Victim.EntityId || GrantThickSkin(*Source,NowMs,Error);
}

bool ALDCombatDirector::OnHitLanded(FLDBodyCombatState& Target, const FLDCombatHit& Hit, int64 DamageP2, int64 NowMs, FString& Error)
{
    FLDBodyCombatState* Source=FindBody(Hit.SourceBody);
    if (!Source || Source==&Target || Source->bPendingDeath || DamageP2<=0 || Hit.bAddedDamage || Hit.bReflect) { return true; }
    const bool bBasic=Hit.HitClass==ELDCombatHitClass::BasicPrimary;
    const bool bAbility=Hit.HitClass==ELDCombatHitClass::Ability && !Hit.bPeriodic;
    // passive_dox_master: a damaging ability hit stacks one layer here; basic and coefficient hits stack through
    // ApplyOnHitEffects (Q3: one layer at any coefficient).
    if (bAbility && Hit.OnHitScaleBp<=0 && !StackDox(Target,*Source,Hit,NowMs,Error)) { return false; }
    // passive_thick_skin Lv3: the next basic or ability hit after the shield adds a share of it as a separate magic hit.
    if ((bBasic || bAbility) && Source->ShieldBonusP2>0 && NowMs<Source->ShieldBonusUntilMs)
    {
        FLDCombatSourceRef SkinRef;
        FindTrigger(Source->AttributeProjection,ELDDemoEffectOp::MobilityShield,&SkinRef);
        FLDCombatHit Bonus=LDCombatMechanicsDetail::MakeHit(*Source,SkinRef,Hit.Target,FGuid());
        Bonus.BaseP2=Source->ShieldBonusP2;
        Bonus.DamageType=ELDDemoDamageType::Magical;
        Bonus.bAddedDamage=true;
        PendingHits.Add(Bonus);
        Source->ShieldBonusP2=0;
        Source->ShieldBonusUntilMs=0;
    }
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

bool ALDCombatDirector::StackDox(FLDBodyCombatState& Target, FLDBodyCombatState& Source, const FLDCombatHit& Hit, int64 NowMs,
    FString& Error)
{
    if (&Source==&Target || Source.bPendingDeath || Target.bPendingDeath || Hit.bAddedDamage || Hit.bReflect) { return true; }
    FLDCombatSourceRef DoxRef;
    if (const FLDDemoEffectDefinition* Dox=FindTrigger(Source.AttributeProjection,ELDDemoEffectOp::BreachStacks,&DoxRef))
    {
        FLDCombatDot* Dot=Target.Dots.FindByPredicate([&DoxRef,&Source](const FLDCombatDot& Row)
        { return Row.Key==DoxRef.DefinitionId && Row.SourceEntity==Source.Victim.EntityId; });
        if (!Dot)
        {
            Dot=&Target.Dots.AddDefaulted_GetRef();
            Dot->Key=DoxRef.DefinitionId;
            Dot->EffectId=FGuid::NewGuid();
            Dot->SourceEntity=Source.Victim.EntityId;
            Dot->SourceBody={Source.Victim,Source.Identity};
            Dot->CreditOwner=Hit.CreditOwner;
            Dot->Source=DoxRef;
            Dot->BaseP2=Dox->BaseP2;
            Dot->ADRatioBp=Dox->ADRatioBp;
            Dot->APRatioBp=Dox->APRatioBp;
            Dot->DamageType=Dox->DamageType;
            Dot->IntervalMs=Dox->IntervalMs;
            Dot->MaxStacks=int32(Dox->Count);
            Dot->ShredStepP2=Dox->ShredP2;
            Dot->DurationMs=Dox->DurationMs;
            Dot->FullHealCutBp=Dox->SecondaryRatioBp;
            Dot->Stacks=0;
            if (!LDMatchLifecycle::AddDurationMs(NowMs,Dox->IntervalMs,Dot->NextTickAtMs,Error)) { return false; }
        }
        // All layers share one timer that a new layer restarts.
        Dot->Stacks=FMath::Min(Dot->Stacks+1,Dot->MaxStacks);
        Dot->SourceADP2=Source.AttributeProjection.Final.ADP2;
        Dot->SourceAPP2=Source.AttributeProjection.Final.APP2;
        const FLDDemoEffectDefinition* Amp=FindTrigger(Source.AttributeProjection,ELDDemoEffectOp::AfflictionAmp);
        Dot->AmplifyBp=10000+(Amp ? Amp->RatioBp : 0);
        if (!LDMatchLifecycle::AddDurationMs(NowMs,Dot->DurationMs,Dot->ExpiresAtMs,Error)) { return false; }
        // One more layer on C1's breach record: per-layer flat shred, the shared timer restarted (R5).
        FLDCombatBreach Breach;
        Breach.Key=Dot->Key;
        Breach.SourceEntity=Dot->SourceEntity;
        Breach.ArmorFlatP2=Breach.MagicResistFlatP2=Dot->ShredStepP2;
        Breach.Stacks=1;
        Breach.MaxStacks=Dot->MaxStacks;
        Breach.ExpiresAtMs=Dot->ExpiresAtMs;
        LDTactical::ApplyBreach(Target.Breaches,Breach,NowMs);
        if (Dot->FullHealCutBp>0 && Dot->Stacks>=Dot->MaxStacks)
        {
            LDCombatMechanics::ApplyHealCut(Target.HealCuts,
                FLDCombatHealCut{Dot->EffectId,Dot->SourceEntity,Dot->FullHealCutBp,Dot->ExpiresAtMs},NowMs);
        }
        ++Target.StateRevision;
        MarkViewChanged(Target.Victim.EntityId);
        // A breach layer (and the Lv3 heal cut) is a debuff: its owner's assist contribution on this life.
        RecordAssistContribution(Target,Hit.CreditOwner,NowMs);
    }
    return true;
}

bool ALDCombatDirector::CountGuard(ALDCharacter& Actor, FLDBodyCombatState& Body, int64 PreMitigationP2, int64 NowMs, FString& Error)
{
    if (!Body.Guard.IsSet() || Body.Guard->ExpiresAtMs<=NowMs || Body.Guard->bTriggered) { return true; }
    FLDCombatGuard& Guard=Body.Guard.GetValue();
    int64 Taken=0;
    const bool bRecoil=LDCombatMechanics::GuardTriggers(Guard.TakenP2,PreMitigationP2,Guard.ThresholdP2,Taken);
    Guard.TakenP2=Taken;
    if (!bRecoil || Body.bPendingDeath || Body.Resources.HpP2<=0) { return true; }
    Guard.bTriggered=true;
    TArray<FLDCombatBodyRef> Targets;
    if (!CollectAreaTargets(FootOf(Actor),Guard.RadiusCm,&Body,Body.Victim.EntityId,Targets,Error))
    { Error=TEXT("Guard recoil: ")+Error; return false; }
    const FGuid EffectId=FGuid::NewGuid();
    for (const FLDCombatBodyRef& Target : Targets)
    {
        FLDCombatHit Hit=LDCombatMechanicsDetail::MakeHit(Body,Guard.Source,Target,EffectId);
        Hit.SlowBp=Guard.SlowBp;
        Hit.SlowDurationMs=Guard.SlowMs;
        Hit.StunDurationMs=Guard.StunMs;
        PendingHits.Add(Hit);
    }
    const auto& Final=Body.AttributeProjection.Final;
    const int64 Shield=Guard.ShieldBaseP2+LDCombatMechanicsDetail::Scale(Final.APP2,Guard.ShieldAPRatioBp)
        +LDCombatMechanicsDetail::Scale(Body.Resources.MaxHpP2,Guard.ShieldMaxHPRatioBp);
    return GrantShieldTo(Body,Shield,Guard.ShieldMs,EffectId,Body.Victim.EntityId,false,NowMs,Error);
}

FLDNegativeKinds ALDCombatDirector::NegativeKindsOf(const FLDBodyCombatState& Body, int64 NowMs) const
{
    const auto Active=[NowMs](const TArray<FLDCombatRoot>& Rows)
    { return Rows.ContainsByPredicate([NowMs](const FLDCombatRoot& Row) { return Row.ExpiresAtMs>NowMs; }); };
    FLDNegativeKinds Kinds;
    Kinds.bRoot=LDTactical::HasActiveRoot(Body.Roots,NowMs);
    Kinds.bSlow=LDTactical::StrongestSlowBp(Body.Slows,NowMs)>0;
    Kinds.bDisarm=Active(Body.Disarms);
    Kinds.bStun=Active(Body.Stuns);
    Kinds.bTaunt=LDControl::IsTaunted(Body.Taunt,NowMs);
    // Q4: any armor/MR breach or damage over time counts as one kind.
    Kinds.bBreach=LDTactical::HasBreach(Body.Breaches,NowMs)
        || Body.Dots.ContainsByPredicate([NowMs](const FLDCombatDot& Dot) { return Dot.ExpiresAtMs>NowMs; });
    return Kinds;
}

bool ALDCombatDirector::ExpireMechanics(FLDBodyCombatState& Body, int64 NowMs)
{
    bool bChanged=LDCombatMechanics::ExpireHealCuts(Body.HealCuts,NowMs);
    bChanged|=Body.Knockbacks.RemoveAll([NowMs](const FLDCombatRoot& Row) { return Row.ExpiresAtMs<=NowMs; })>0;
    bChanged|=LDCombatMechanics::PruneVulnerabilities(Body.Vulnerabilities,Body.Roots,NowMs);
    bChanged|=Body.Dots.RemoveAll([NowMs](const FLDCombatDot& Dot) { return Dot.ExpiresAtMs<=NowMs; })>0;
    if (Body.Guard.IsSet() && Body.Guard->ExpiresAtMs<=NowMs)
    {
        Body.Guard.Reset();
        bChanged=true;
    }
    if (Body.ShieldBonusP2>0 && Body.ShieldBonusUntilMs<=NowMs)
    {
        Body.ShieldBonusP2=0;
        Body.ShieldBonusUntilMs=0;
    }
    return bChanged;
}

int64 ALDCombatDirector::NextMechanicsDueMs(int64 NowMs) const
{
    int64 Due=MAX_int64;
    const auto Consider=[&Due,NowMs](int64 At) { if (At>NowMs && At<Due) { Due=At; } };
    for (const auto& Pair : Bodies)
    {
        const FLDBodyCombatState& Body=Pair.Value;
        if (Body.bPendingDeath) { continue; }
        for (const auto& Row : Body.HealCuts) { Consider(Row.ExpiresAtMs); }
        for (const auto& Row : Body.Breaches) { Consider(Row.ExpiresAtMs); }
        for (const auto& Row : Body.Knockbacks) { Consider(Row.ExpiresAtMs); }
        for (const auto& Row : Body.Vulnerabilities) { Consider(Row.ExpiresAtMs); }
        for (const auto& Dot : Body.Dots) { Consider(Dot.NextTickAtMs); Consider(Dot.ExpiresAtMs); }
        if (Body.Guard.IsSet()) { Consider(Body.Guard->ExpiresAtMs); }
    }
    for (const FLDCombatZone& Zone : Zones)
    {
        if (Zone.Pulsed<Zone.Count) { Consider(Zone.StartMs+Zone.DelayMs+Zone.IntervalMs*Zone.Pulsed); }
        if (Zone.bSlow) { Consider(Zone.NextSlowAtMs); }
        Consider(Zone.EndMs);
    }
    for (const FLDCombatVolleyShot& Shot : VolleyShots) { Consider(Shot.AtMs); }
    for (const FLDCombatDecoy& Decoy : Decoys) { Consider(Decoy.ExpiresAtMs); }
    return Due;
}

bool ALDCombatDirector::AdvanceMechanics(int64 NowMs, FString& Error)
{
    // Later volley shots leave from the shooter's current foot along the release direction.
    for (int32 Index=VolleyShots.Num()-1; Index>=0; --Index)
    {
        const FLDCombatVolleyShot Shot=VolleyShots[Index];
        if (Shot.AtMs>NowMs) { continue; }
        VolleyShots.RemoveAt(Index);
        ALDCharacter* Shooter=nullptr;
        const FLDBodyCombatState* Body=FindBody(Shot.Body,&Shooter);
        if (!Body || !Shooter || Body->bPendingDeath) { continue; }
        FLDCombatProjectile Projectile=Shot.Template;
        Projectile.OriginCm=FootOf(*Shooter);
        Projectile.TravelledCm=0;
        Projectile.AdvancedThroughMs=NowMs;
        Projectiles.Add(Projectile);
    }
    Decoys.RemoveAll([NowMs](const FLDCombatDecoy& Decoy) { return Decoy.ExpiresAtMs<=NowMs; });
    // Damage over time: every tick due now, damage terms snapshotted at application, stacks multiplying them.
    for (auto& Pair : Bodies)
    {
        FLDBodyCombatState& Body=Pair.Value;
        if (!Pair.Key.IsValid() || Body.bPendingDeath) { continue; }
        bool bChanged=false;
        for (FLDCombatDot& Dot : Body.Dots)
        {
            while (Dot.NextTickAtMs>0 && Dot.NextTickAtMs<=NowMs && Dot.NextTickAtMs<Dot.ExpiresAtMs
                && (Dot.TicksTotal==0 || Dot.TicksLeft>0))
            {
                const int64 Stacks=FMath::Max(Dot.Stacks,1);
                FLDCombatHit Tick;
                Tick.HitId=FGuid::NewGuid();
                Tick.EffectId=Dot.EffectId;
                Tick.Target={Body.Victim,Body.Identity};
                Tick.SourceBody=Dot.SourceBody;
                Tick.SourceEntity=Dot.SourceEntity;
                Tick.CreditOwner=Dot.CreditOwner;
                Tick.Source=Dot.Source;
                Tick.HitClass=ELDCombatHitClass::Ability;
                Tick.BaseP2=Dot.BaseP2*Stacks;
                Tick.ADRatioBp=Dot.ADRatioBp*Stacks;
                Tick.APRatioBp=Dot.APRatioBp*Stacks;
                Tick.DamageType=Dot.DamageType;
                Tick.SourceADP2=Dot.SourceADP2;
                Tick.SourceAPP2=Dot.SourceAPP2;
                Tick.ExtraAmplifyBp=Dot.AmplifyBp;
                Tick.bPeriodic=true;
                PendingHits.Add(Tick);
                if (Dot.TicksTotal>0)
                {
                    --Dot.TicksLeft;
                    // A fixed-count dot deepens its breach by one layer before each further tick (R2: -6/-12/-18 at
                    // hit/1s/2s), all removed when it ends.
                    if (Dot.ShredStepP2>0 && Dot.TicksLeft>0)
                    {
                        FLDCombatBreach Breach;
                        Breach.Key=Dot.Key;
                        Breach.SourceEntity=Dot.SourceEntity;
                        Breach.ArmorFlatP2=Breach.MagicResistFlatP2=Dot.ShredStepP2;
                        Breach.Stacks=1;
                        Breach.MaxStacks=Dot.TicksTotal;
                        Breach.ExpiresAtMs=Dot.ExpiresAtMs;
                        LDTactical::ApplyBreach(Body.Breaches,Breach,NowMs);
                        // Each deeper breach layer is a debuff: its owner's assist contribution on this life.
                        RecordAssistContribution(Body,Dot.CreditOwner,NowMs);
                    }
                }
                if (!LDMatchLifecycle::AddDurationMs(Dot.NextTickAtMs,Dot.IntervalMs,Dot.NextTickAtMs,Error)) { return false; }
                bChanged=true;
            }
        }
        if (bChanged)
        {
            ++Body.StateRevision;
            MarkViewChanged(Body.Victim.EntityId);
        }
    }
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
            if (Zone.bShield && Holder && !Holder->bPendingDeath)
            {
                ALDCharacter* OwnerActor=ActorOf(*Holder);
                if (OwnerActor && LDCombatMechanics::InsideZone(Zone.CentreCm,Zone.RadiusCm,FootOf(*OwnerActor)))
                {
                    const int64 Amount=Zone.ShieldBaseP2+LDCombatMechanicsDetail::Scale(Holder->AttributeProjection.Final.APP2,Zone.ShieldAPRatioBp);
                    if (!GrantShieldTo(*Holder,Amount,Zone.ShieldMs,Zone.EffectId,Zone.SourceEntity,false,NowMs,Error)) { return false; }
                }
            }
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
                    // Gas pulses trigger the living owner's on-hit effects at their coefficient (C1's on-hit framework);
                    // a dead owner's pulses only deal their damage.
                    if (Holder && !Holder->bPendingDeath && Zone.OnHitScaleBp>0
                        && !SnapshotOnHit(*Holder,Zone.OnHitScaleBp,Hit,Error)) { return false; }
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

void ALDCombatDirector::FillMechanicsTemplate(const FLDBodyCombatState& Body, const FLDDemoRankDefinition& Rank,
    const FVector& OriginFoot, int64 NowMs, FLDCombatHit& Template) const
{
    using namespace LDCombatMechanicsDetail;
    if (const FLDDemoEffectDefinition* Cut=FindOp(Rank,ELDDemoEffectOp::HealCut))
    {
        Template.HealCutBp=Cut->RatioBp;
        Template.HealCutMs=Cut->DurationMs;
    }
    if (const FLDDemoEffectDefinition* Vulnerable=FindOp(Rank,ELDDemoEffectOp::Vulnerable)) { Template.VulnerableBp=Vulnerable->RatioBp; }
    if (const FLDDemoEffectDefinition* Amp=FindOp(Rank,ELDDemoEffectOp::DebuffAmp)) { Template.NegativeAmpBp=Amp->RatioBp; }
    if (const FLDDemoEffectDefinition* Shatter=FindOp(Rank,ELDDemoEffectOp::ShieldShatter)) { Template.ShieldShatterBp=Shatter->RatioBp; }
    if (const FLDDemoEffectDefinition* Heal=FindOp(Rank,ELDDemoEffectOp::DebuffHeal)) { Template.NegativeHealMaxHPBp=Heal->MaxHPRatioBp; }
    if (const FLDDemoEffectDefinition* Push=FindOp(Rank,ELDDemoEffectOp::Knockback))
    {
        // A self-centred push leaves from the caster's foot at release.
        Template.KnockbackCm=Cm(Push->DistanceCm);
        Template.KnockbackMs=Push->DurationMs;
        Template.bKnockbackFromAnchor=true;
        Template.KnockbackAnchorCm=OriginFoot;
    }
    Template.bRefreshDebuffs=FindOp(Rank,ELDDemoEffectOp::RefreshDebuffs)!=nullptr;
    if (const FLDDemoEffectDefinition* OnHit=FindOp(Rank,ELDDemoEffectOp::OnHit))
    {
        // A zone's pulses carry the coefficient themselves; a volley's shots carry it here.
        if (!FindOp(Rank,ELDDemoEffectOp::Zone)) { Template.OnHitScaleBp=OnHit->RatioBp; }
    }
    if (const FLDDemoEffectDefinition* DotDef=FindOp(Rank,ELDDemoEffectOp::Dot))
    {
        FLDCombatDot Dot;
        Dot.Key=Template.Source.DefinitionId;
        Dot.EffectId=Template.EffectId;
        Dot.SourceEntity=Template.SourceEntity;
        Dot.SourceBody=Template.SourceBody;
        Dot.CreditOwner=Template.CreditOwner;
        Dot.Source=Template.Source;
        Dot.BaseP2=DotDef->BaseP2;
        Dot.ADRatioBp=DotDef->ADRatioBp;
        Dot.APRatioBp=DotDef->APRatioBp;
        Dot.DamageType=DotDef->DamageType;
        Dot.SourceADP2=Body.AttributeProjection.Final.ADP2;
        Dot.SourceAPP2=Body.AttributeProjection.Final.APP2;
        const FLDDemoEffectDefinition* Amp=FindTrigger(Body.AttributeProjection,ELDDemoEffectOp::AfflictionAmp);
        Dot.AmplifyBp=10000+(Amp ? Amp->RatioBp : 0);
        Dot.IntervalMs=DotDef->IntervalMs;
        Dot.TicksLeft=Dot.TicksTotal=int32(DotDef->Count);
        Dot.ShredStepP2=DotDef->ShredP2;
        Dot.DurationMs=DotDef->IntervalMs*DotDef->Count;
        if (const FLDDemoEffectDefinition* Punish=FindOp(Rank,ELDDemoEffectOp::MobilityPunish))
        {
            Dot.bPunish=true;
            Dot.PunishBaseP2=Punish->BaseP2;
            Dot.PunishADRatioBp=Punish->ADRatioBp;
            Dot.PunishAPRatioBp=Punish->APRatioBp;
        }
        Template.Dot=Dot;
    }
}

bool ALDCombatDirector::ReleaseMechanics(ALDCharacter& Actor, FLDBodyCombatState& Body, const FLDCombatSourceRef& Source,
    const FLDDemoRankDefinition& Rank, const FLDCombatHit* Template, const FVector& OriginFoot, const FVector& TargetPoint,
    const FGuid& EffectId, uint64 Token, int64 DisplacementBonusBp, FString& Error)
{
    using namespace LDCombatMechanicsDetail;
    ALDGameMode* Mode=Core();
    if (!Mode) { Error=TEXT("Release mechanics require the current core."); return false; }
    const int64 Now=Mode->GetAuthorityNowMs();
    const FLDCombatBodyRef SelfRef{Body.Victim,Body.Identity};
    if (const FLDDemoEffectDefinition* GuardDef=FindOp(Rank,ELDDemoEffectOp::DamageGuard))
    {
        // active_porcelain_flop: the slow/stun and the shield are the recoil's payload, not release effects.
        FLDCombatGuard Guard;
        Guard.EffectId=EffectId;
        Guard.Source=Source;
        Guard.ReductionBp=GuardDef->RatioBp;
        Guard.ThresholdP2=GuardDef->ThresholdP2;
        if (!LDMatchLifecycle::AddDurationMs(Now,GuardDef->DurationMs,Guard.ExpiresAtMs,Error)) { return false; }
        if (const FLDDemoEffectDefinition* Area=FindOp(Rank,ELDDemoEffectOp::SelfArea)) { Guard.RadiusCm=Cm(Area->RadiusCm); }
        if (const FLDDemoEffectDefinition* Slow=FindOp(Rank,ELDDemoEffectOp::Slow)) { Guard.SlowBp=Slow->RatioBp; Guard.SlowMs=Slow->DurationMs; }
        if (const FLDDemoEffectDefinition* Stun=FindOp(Rank,ELDDemoEffectOp::Stun)) { Guard.StunMs=Stun->DurationMs; }
        if (const FLDDemoEffectDefinition* Shield=FindOp(Rank,ELDDemoEffectOp::Shield))
        {
            Guard.ShieldBaseP2=Shield->BaseP2;
            Guard.ShieldAPRatioBp=Shield->APRatioBp;
            Guard.ShieldMaxHPRatioBp=Shield->MaxHPRatioBp;
            Guard.ShieldMs=Shield->DurationMs;
        }
        Body.Guard=Guard;
    }
    if (const FLDDemoEffectDefinition* ZoneDef=FindOp(Rank,ELDDemoEffectOp::Zone))
    {
        FLDCombatZone Zone;
        Zone.EffectId=FGuid::NewGuid();
        Zone.SourceBody=SelfRef;
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
        if (const FLDDemoEffectDefinition* Shield=FindOp(Rank,ELDDemoEffectOp::ZoneShield))
        {
            Zone.bShield=true;
            Zone.ShieldBaseP2=Shield->BaseP2;
            Zone.ShieldAPRatioBp=Shield->APRatioBp;
            Zone.ShieldMs=Shield->DurationMs;
        }
        if (const FLDDemoEffectDefinition* Slow=FindOp(Rank,ELDDemoEffectOp::ZoneSlow))
        {
            Zone.bSlow=true;
            Zone.SlowBp=Slow->RatioBp;
            Zone.SlowCheckMs=Slow->IntervalMs;
            Zone.SlowLingerMs=Slow->SecondaryDurationMs;
            Zone.NextSlowAtMs=Now;
        }
        if (const FLDDemoEffectDefinition* Root=FindOp(Rank,ELDDemoEffectOp::ZoneRoot)) { Zone.EndRootMs=Root->DurationMs; }
        if (const FLDDemoEffectDefinition* Damage=FindOp(Rank,ELDDemoEffectOp::ZoneDamage))
        {
            Zone.bDamage=true;
            Zone.BaseP2=Damage->BaseP2;
            Zone.ADRatioBp=Damage->ADRatioBp;
            Zone.APRatioBp=Damage->APRatioBp;
            Zone.DamageType=Damage->DamageType;
            const FLDDemoEffectDefinition* OnHit=FindOp(Rank,ELDDemoEffectOp::OnHit);
            Zone.OnHitScaleBp=OnHit ? OnHit->RatioBp : 0;
        }
        Zones.Add(MoveTemp(Zone));
    }
    if (const FLDDemoEffectDefinition* Volley=FindOp(Rank,ELDDemoEffectOp::Volley))
    {
        const FLDCombatProjectile* First=nullptr;
        for (int32 Index=Projectiles.Num()-1; Index>=0 && !First; --Index)
        {
            if (Projectiles[Index].EffectId==EffectId) { First=&Projectiles[Index]; }
        }
        if (!First) { Error=TEXT("A volley release has no first projectile."); return false; }
        for (int64 Shot=1; Shot<Volley->Count; ++Shot)
        {
            FLDCombatVolleyShot& Later=VolleyShots.AddDefaulted_GetRef();
            Later.Body=SelfRef;
            Later.Template=*First;
            // Each later shot is its own flight (the effect identity stays shared).
            Later.Template.FlightId=FGuid::NewGuid();
            if (!LDMatchLifecycle::AddDurationMs(Now,Volley->IntervalMs*Shot,Later.AtMs,Error)) { return false; }
        }
    }
    if (const FLDDemoEffectDefinition* Leap=FindOp(Rank,ELDDemoEffectOp::LeapBack))
    {
        // Backwards from the body's authority facing, with the same native displacement as a dash.
        const double Radians=FMath::DegreesToRadians(Actor.GetActorRotation().Yaw+180.);
        const FVector Back(FMath::Cos(Radians),FMath::Sin(Radians),0.);
        auto* Movement=Cast<ULDCharacterMovementComponent>(Actor.GetCharacterMovement());
        // item_phase_anchor lengthens it like any other skill displacement (bonus fixed at the release).
        const double LeapCm=Cm(Leap->DistanceCm)*double(10000+DisplacementBonusBp)/10000.;
        if (!Movement || !Movement->StartDemoDash(Back,LeapCm,double(Leap->DurationMs)/1000.,Error)) { return false; }
        if (!LDMatchLifecycle::AddDurationMs(Now,Leap->DurationMs,Body.DashUntilMs,Error) || !OnMobilityUsed(Body,Now,Error)) { return false; }
    }
    if (const FLDDemoEffectDefinition* DecoyDef=FindOp(Rank,ELDDemoEffectOp::Decoy))
    {
        FLDCombatDecoy Decoy;
        Decoy.EffectId=EffectId;
        Decoy.Holder=SelfRef;
        Decoy.PointCm=OriginFoot;
        if (!LDMatchLifecycle::AddDurationMs(Now,DecoyDef->DurationMs,Decoy.ExpiresAtMs,Error)) { return false; }
        Decoys.Add(Decoy);
        const UWorld* CurrentWorld=GetWorld();
        const ULDMapRegistry* Registry=CurrentWorld ? CurrentWorld->GetSubsystem<ULDMapRegistry>() : nullptr;
        if (!Mode->Vision || !Registry) { Error=TEXT("A decoy lure requires the observation owner and the registered world."); return false; }
        FLDControlApplication Lure;
        Lure.Kind=ELDControlKind::Taunt;
        Lure.EffectId=EffectId;
        Lure.SourceEntity=Body.Victim.EntityId;
        Lure.SourceBody=SelfRef;
        Lure.CreditOwner=Body.Victim.EntityId;
        Lure.DurationMs=DecoyDef->SecondaryDurationMs;
        const double Radius=Cm(DecoyDef->RadiusCm);
        for (auto& Pair : Bodies)
        {
            ALDCharacter* Other=Pair.Key.Get();
            FLDBodyCombatState& Victim=Pair.Value;
            if (!Other || &Victim==&Body || Victim.bPendingDeath || Victim.Victim.Kind!=ELDVictimKind::Contender
                || Mode->IsContenderProtected(Victim.Victim.EntityId)) { continue; }
            const FVector Foot=FootOf(*Other);
            if (!LDCombatMechanics::InsideZone(Decoy.PointCm,Radius,Foot)) { continue; }
            // Only enemies that can see the decoy's spot are lured (the same rule as a taunt observing its caster).
            const ELDSightResult Sight=Mode->Vision->TraceSight(Foot,Decoy.PointCm,Registry->Revision());
            if (Sight==ELDSightResult::InvalidPoint || Sight==ELDSightResult::StaleWorld)
            { Error=TEXT("Decoy sight failed on a registered body."); return false; }
            if (Sight!=ELDSightResult::Visible) { continue; }
            bool bApplied=false;
            if (!CommitControl(Victim,Lure,Now,bApplied,Error)) { return false; }
            if (!bApplied || !Victim.Taunt.IsSet()) { continue; }
            // The committed taunt is a lure: no attack, cast or item, a forced walk to the decoy.
            Victim.Taunt->bDecoy=true;
            Victim.Taunt->DecoyPointCm=Decoy.PointCm;
            if (Victim.Action.IsSet()) { AbandonAction(Victim,ELDCombatReject::ControlBlocked); }
            UE_LOG(LogLDCombat,Display,TEXT("LD_COMBAT_DECOY_LURE victim=%s source=%s until=%lld"),
                *Victim.Victim.EntityId.ToString(),*Body.Victim.EntityId.ToString(),Victim.Taunt->ExpiresAtMs);
        }
    }
    if (FindOp(Rank,ELDDemoEffectOp::CooldownRefresh))
    {
        // item_energy_drink: of Q and W, the one closer to ready becomes ready now (its availability was checked before
        // the release paid).
        FLDCombatEntityRecord* Record=FindRecord(Body.Victim.EntityId);
        const FLDCombatTrack* Q=Record ? TrackFor(*Record,ELDCombatEntry::Q) : nullptr;
        const FLDCombatTrack* W=Record ? TrackFor(*Record,ELDCombatEntry::W) : nullptr;
        const bool bQ=Q && Q->ReadyAtMs>Now, bW=W && W->ReadyAtMs>Now;
        if (bQ || bW)
        {
            const ELDCombatEntry Entry=bQ && (!bW || Q->ReadyAtMs<=W->ReadyAtMs) ? ELDCombatEntry::Q : ELDCombatEntry::W;
            ReduceCooldown(Body.Victim.EntityId,Entry,MAX_int64);
        }
    }
    (void)Template;
    (void)Token;
    return true;
}

void ALDCombatDirector::AppendMechanicsStatuses(const FLDBodyCombatState& Body, int64 NowMs, bool bOwner, TArray<FLDObservedStatus>& Out) const
{
    if (const int64 KnockUntil=LDTactical::ActiveRootUntilMs(Body.Knockbacks,NowMs); KnockUntil>0)
    { Out.Add({uint8(ELDDemoStatusId::Knockback),1,KnockUntil}); }
    int64 CutUntil=0;
    for (const FLDCombatHealCut& Cut : Body.HealCuts) { if (Cut.ExpiresAtMs>NowMs) { CutUntil=FMath::Max(CutUntil,Cut.ExpiresAtMs); } }
    if (CutUntil>0) { Out.Add({uint8(ELDDemoStatusId::HealCut),1,CutUntil}); }
    if (!bOwner) { return; }
    if (Body.Guard.IsSet() && Body.Guard->ExpiresAtMs>NowMs) { Out.Add({uint8(ELDDemoStatusId::DamageGuard),1,Body.Guard->ExpiresAtMs}); }
    const int64 VulnerableUntil=LDCombatMechanics::VulnerableUntilMs(Body.Vulnerabilities,Body.Roots,NowMs);
    if (VulnerableUntil>0) { Out.Add({uint8(ELDDemoStatusId::Vulnerable),1,VulnerableUntil}); }
}

bool ALDCombatDirector::ReadDecoyLure(const FLDCombatBodyRef& Victim, FVector& OutPoint) const
{
    const ALDGameMode* Mode=Core();
    const FLDBodyCombatState* Body=FindBody(Victim);
    if (!Mode || !Body || Body->bPendingDeath || !Body->Taunt.IsSet() || !Body->Taunt->bDecoy
        || !LDControl::IsTaunted(Body->Taunt,Mode->GetAuthorityNowMs())) { return false; }
    OutPoint=Body->Taunt->DecoyPointCm;
    return true;
}

uint64 ALDCombatDirector::ReadKnockbackSerial(const FLDCombatBodyRef& Ref) const
{
    const FLDBodyCombatState* Body=FindBody(Ref);
    return Body ? Body->KnockbackSerial : 0;
}
