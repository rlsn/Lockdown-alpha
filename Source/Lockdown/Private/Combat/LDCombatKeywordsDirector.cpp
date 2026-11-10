#include "Combat/LDCombatDirector.h"
#include "Core/LDLog.h"
#include "Combat/LDCombatKeywords.h"
#include "Combat/LDTacticalRules.h"
#include "Character/LDCharacter.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDGameMode.h"

// #147 build v7 keywords on Combat's body records (docs/systems/COMBAT_AND_GROWTH.md, "构筑 v7 战斗原语"); the pure
// rules live in LDCombatKeywords.cpp.
namespace LDCombatKeywordsDetail
{
    int64 Share(int64 ValueP2, int64 Bp)
    {
        if (ValueP2<=0 || Bp<=0) { return 0; }
        return ValueP2/10000*Bp+ValueP2%10000*Bp/10000;
    }

    double Cm(const FLDDemoRational& Value)
    { return Value.Denominator>0 ? double(Value.Numerator)/double(Value.Denominator) : 0.; }
}

bool ALDCombatDirector::KeywordTerms(FLDKeywordTerms& Out, FString& Error) const
{
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!Content) { Error=TEXT("Keyword rules require the approved Demo content."); return false; }
    Out=FLDKeywordTerms::From(Content->Get().Parameters);
    return true;
}

int32 ALDCombatDirector::GrowthLevelOf(const FLDBodyCombatState& Body) const
{
    if (Body.Victim.Kind!=ELDVictimKind::Contender) { return 1; }
    const FLDCombatEntityRecord* Record=FindRecord(Body.Victim.EntityId);
    return Record ? FMath::Max<int32>(Record->Build.Level,1) : 1;
}

bool ALDCombatDirector::BurnLayerOf(const FLDBodyCombatState& Source, const FLDCombatSourceRef& Ref, FLDCombatBurnLayer& Out,
    FString& Error) const
{
    Out=FLDCombatBurnLayer();
    Out.SourceEntity=Source.Victim.EntityId;
    Out.SourceBody={Source.Victim,Source.Identity};
    Out.CreditOwner=Source.Victim.Kind==ELDVictimKind::Contender ? Source.Victim.EntityId : FGuid();
    Out.Source=Ref;
    Out.Level=GrowthLevelOf(Source);
    Out.SourceAPP2=Source.AttributeProjection.Final.APP2;
    Error.Reset();
    return true;
}

bool ALDCombatDirector::ApplyKeywordStacks(FLDBodyCombatState& Target, const FLDCombatHit& Hit, FLDBodyCombatState* Source,
    int64 NowMs, FString& Error)
{
    FLDKeywordTerms Terms;
    if (!KeywordTerms(Terms,Error)) { return false; }
    bool bChanged=false;
    const auto Breach=[&](int32 Stacks, const FGuid& Credit)
    {
        if (Stacks<=0 || !LDCombatKeywords::ApplyBreach(Target.SharedBreach,Stacks,NowMs,Terms)) { return; }
        bChanged=true;
        RecordAssistContribution(Target,Credit,NowMs);
    };
    const auto Burn=[&](int32 Stacks, const FLDCombatBurnLayer& Layer)
    {
        if (Stacks<=0 || !LDCombatKeywords::ApplyBurn(Target.Burn,Layer,Stacks,NowMs,Terms)) { return; }
        bChanged=true;
        RecordAssistContribution(Target,Layer.CreditOwner,NowMs);
    };
    // The hit's own stacks (an active's payload or a zone pulse), its burn snapshotted at release.
    Breach(Hit.BreachStacks,Hit.CreditOwner);
    if (Hit.BurnStacks>0)
    {
        FLDCombatBurnLayer Layer;
        Layer.SourceEntity=Hit.SourceEntity;
        Layer.SourceBody=Hit.SourceBody;
        Layer.CreditOwner=Hit.CreditOwner;
        Layer.Source=Hit.Source;
        Layer.Level=Hit.BurnLevel;
        Layer.SourceAPP2=Hit.BurnAPP2;
        Burn(Hit.BurnStacks,Layer);
    }
    // The living source's held triggers: basic_stacks on a primary basic hit, stacks on a learned Q/W hit. Ticks, pulses,
    // reflections, added damage and shield-triggered answers are neither.
    const bool bBasic=Hit.HitClass==ELDCombatHitClass::BasicPrimary;
    const bool bSkill=Hit.HitClass==ELDCombatHitClass::Ability && Hit.Source.Kind==ELDCombatSourceKind::Learned && Hit.Token!=0;
    if (Source && !Source->bPendingDeath && (bBasic || bSkill) && !Hit.bReflect && !Hit.bAddedDamage && !Hit.bPeriodic
        && !Hit.bFromShieldTrigger && !Hit.bSuppressSkillTriggers)
    {
        TArray<FLDPassiveEffectRef> Passives;
        if (!CollectPassiveEffects(Source->AttributeProjection,ELDDemoEffectOp::Breach,Passives,Error)) { return false; }
        for (const FLDPassiveEffectRef& Ref : Passives) { Breach(bBasic ? Ref.Effect->BasicCount : Ref.Effect->Count,Hit.CreditOwner); }
        if (!CollectPassiveEffects(Source->AttributeProjection,ELDDemoEffectOp::Burn,Passives,Error)) { return false; }
        for (const FLDPassiveEffectRef& Ref : Passives)
        {
            FLDCombatBurnLayer Layer;
            if (!BurnLayerOf(*Source,Ref.Source,Layer,Error)) { return false; }
            Burn(bBasic ? Ref.Effect->BasicCount : Ref.Effect->Count,Layer);
        }
    }
    if (bChanged)
    {
        ++Target.StateRevision;
        MarkViewChanged(Target.Victim.EntityId);
    }
    return true;
}

bool ALDCombatDirector::AdvanceBurns(int64 NowMs, FString& Error)
{
    FLDKeywordTerms Terms;
    if (!KeywordTerms(Terms,Error)) { return false; }
    TArray<FLDBurnTickShare> Shares;
    for (auto& Pair : Bodies)
    {
        FLDBodyCombatState& Body=Pair.Value;
        if (!Pair.Key.IsValid() || Body.bPendingDeath || Body.Burn.Layers.IsEmpty()) { continue; }
        bool bChanged=false;
        while (Body.Burn.NextTickAtMs>0 && Body.Burn.NextTickAtMs<=NowMs && Body.Burn.NextTickAtMs<=Body.Burn.ExpiresAtMs
            && !Body.Burn.Layers.IsEmpty())
        {
            LDCombatKeywords::BurnTickShares(Body.Burn,Terms,Shares);
            for (const FLDBurnTickShare& Share : Shares)
            {
                if (Share.DamageP2<=0) { continue; }
                FLDCombatHit& Tick=PendingHits.AddDefaulted_GetRef();
                Tick.HitId=FGuid::NewGuid();
                Tick.EffectId=FGuid::NewGuid();
                Tick.Target={Body.Victim,Body.Identity};
                Tick.SourceBody=Share.SourceBody;
                Tick.SourceEntity=Share.SourceEntity;
                Tick.CreditOwner=Share.CreditOwner;
                Tick.Source=Share.Source;
                Tick.HitClass=ELDCombatHitClass::Ability;
                Tick.BaseP2=Share.DamageP2;
                Tick.DamageType=ELDDemoDamageType::Magical;
                Tick.bPeriodic=true;
                Tick.bBurnTick=true;
                // A burn takes its living applier's slowed-or-controlled amplification (v7 4.2) in the damage bonus pool;
                // nothing else of it.
                if (const FLDBodyCombatState* Applier=FindBody(Share.SourceBody); Applier && !Applier->bPendingDeath)
                {
                    if (const FLDDemoEffectDefinition* Amp=FindTrigger(Applier->AttributeProjection,ELDDemoEffectOp::ControlDamageAmp))
                    {
                        Tick.ControlAmpBp=Amp->RatioBp;
                        Tick.ControlAmpBelowBp=Amp->SecondaryRatioBp;
                    }
                }
            }
            if (Body.Burn.NextTickAtMs>MAX_int64-Terms.BurnTickMs) { Error=TEXT("A burn tick left the authority clock."); return false; }
            Body.Burn.NextTickAtMs+=Terms.BurnTickMs;
            bChanged=true;
        }
        // The last tick falls on the shared deadline; then the burn ends.
        if (Body.Burn.NextTickAtMs>Body.Burn.ExpiresAtMs) { Body.Burn=FLDCombatBurn(); }
        if (bChanged)
        {
            ++Body.StateRevision;
            MarkViewChanged(Body.Victim.EntityId);
        }
    }
    return true;
}

bool ALDCombatDirector::OnDamaged(FLDBodyCombatState& Body, int64 NowMs, FString& Error)
{
    if (Body.bPendingDeath || NowMs<Body.DamagedShieldReadyAtMs) { return true; }
    TArray<FLDPassiveEffectRef> Shields;
    if (!CollectPassiveEffects(Body.AttributeProjection,ELDDemoEffectOp::TriggeredShield,Shields,Error)) { return false; }
    const FLDPassiveEffectRef* Ref=Shields.FindByPredicate([](const FLDPassiveEffectRef& Row)
    { return Row.Effect->Trigger==ELDDemoEffectTrigger::Damaged; });
    if (!Ref) { return true; }
    const FLDDemoEffectDefinition& Effect=*Ref->Effect;
    // A held trigger's fixed amount already arrives grown for the holder's level (CollectPassiveEffects).
    const int64 Amount=Effect.BaseP2
        +LDCombatKeywordsDetail::Share(Body.Resources.MaxHpP2,Effect.MaxHPRatioBp);
    const FGuid ShieldId=FGuid::NewGuid();
    if (!GrantShieldTo(Body,Amount,Effect.DurationMs,ShieldId,Body.Victim.EntityId,NowMs,Error)) { return false; }
    // A weaker shield than the one held is not granted, and then the rest does not start.
    if (!Body.Shield.IsSet() || Body.Shield->EffectId!=ShieldId) { return true; }
    if (!LDMatchLifecycle::AddDurationMs(NowMs,Effect.IntervalMs,Body.DamagedShieldReadyAtMs,Error)) { return false; }
    Body.ShieldBurst.Reset();
    if (Effect.SecondaryRatioBp>0)
    {
        FLDCombatShieldBurst& Burst=Body.ShieldBurst.Emplace();
        Burst.ShieldEffectId=ShieldId;
        Burst.Source=Ref->Source;
        Burst.GrantedP2=Body.ShieldGrantedP2;
        Burst.RatioBp=Effect.SecondaryRatioBp;
        Burst.RadiusCm=LDCombatKeywordsDetail::Cm(Effect.RadiusCm);
    }
    return true;
}

bool ALDCombatDirector::BurstBrokenShield(FLDBodyCombatState& Body, const FGuid& BrokenShieldId,
    const FLDCombatBodyRef& BreakerBody, int64 NowMs, FString& Error)
{
    if (!Body.ShieldBurst.IsSet()) { return true; }
    const FLDCombatShieldBurst Burst=Body.ShieldBurst.GetValue();
    Body.ShieldBurst.Reset();
    ALDCharacter* Actor=ActorOf(Body);
    if (!BrokenShieldId.IsValid() || Burst.ShieldEffectId!=BrokenShieldId || !Actor || Body.bPendingDeath) { return true; }
    TArray<FLDCombatBodyRef> Targets;
    if (!CollectAreaTargets(FootOf(*Actor),Burst.RadiusCm,&Body,Body.Victim.EntityId,Targets,Error))
    { Error=TEXT("Shield burst: ")+Error; return false; }
    const FGuid EffectId=FGuid::NewGuid();
    const FLDBodyCombatState* Breaker=FindBody(BreakerBody);
    if (Breaker && Breaker!=&Body && !Breaker->bPendingDeath)
    {
        FLDCombatHit& Hit=PendingHits.AddDefaulted_GetRef();
        Hit.HitId=FGuid::NewGuid();
        Hit.EffectId=EffectId;
        Hit.Target=BreakerBody;
        Hit.SourceBody={Body.Victim,Body.Identity};
        Hit.SourceEntity=Body.Victim.EntityId;
        Hit.CreditOwner=Body.Victim.Kind==ELDVictimKind::Contender ? Body.Victim.EntityId : FGuid();
        Hit.Source=Burst.Source;
        Hit.HitClass=ELDCombatHitClass::Ability;
        Hit.BaseP2=LDCombatKeywords::ShieldBurstShareP2(Burst.GrantedP2,Burst.RatioBp,false);
        Hit.DamageType=ELDDemoDamageType::Magical;
        Hit.bFromShieldTrigger=true;
    }
    for (const FLDCombatBodyRef& Target : Targets)
    {
        if (Target==BreakerBody) { continue; }
        FLDCombatHit& Hit=PendingHits.AddDefaulted_GetRef();
        Hit.HitId=FGuid::NewGuid();
        Hit.EffectId=EffectId;
        Hit.Target=Target;
        Hit.SourceBody={Body.Victim,Body.Identity};
        Hit.SourceEntity=Body.Victim.EntityId;
        Hit.CreditOwner=Body.Victim.Kind==ELDVictimKind::Contender ? Body.Victim.EntityId : FGuid();
        Hit.Source=Burst.Source;
        Hit.HitClass=ELDCombatHitClass::Ability;
        Hit.BaseP2=LDCombatKeywords::ShieldBurstShareP2(Burst.GrantedP2,Burst.RatioBp,true);
        Hit.DamageType=ELDDemoDamageType::Magical;
        // A burst never grants another shield-triggered effect.
        Hit.bFromShieldTrigger=true;
    }
    UE_LOG(LogLDCombat,Display,TEXT("LD_COMBAT_SHIELD_BURST holder=%s targets=%d breaker=%s at=%lld"),*LDLog::Id(Body.Victim.EntityId),
        Targets.Num(),*LDLog::Id(BreakerBody.Victim.EntityId),NowMs);
    return true;
}

void ALDCombatDirector::AppendKeywordStatuses(const FLDBodyCombatState& Body, int64 NowMs, TArray<FLDObservedStatus>& Out) const
{
    if (const int64 SilencedUntil=LDTactical::ActiveRootUntilMs(Body.Silences,NowMs); SilencedUntil>0)
    { Out.Add({uint8(ELDDemoStatusId::Silence),1,SilencedUntil}); }
    // The shared breach shows its stacks.
    if (const int32 Stacks=LDCombatKeywords::BreachStacks(Body.SharedBreach,NowMs); Stacks>0)
    { Out.Add({uint8(ELDDemoStatusId::Exposed),uint8(FMath::Clamp(Stacks,1,255)),Body.SharedBreach.ExpiresAtMs}); }
    if (LDCombatKeywords::BurnActive(Body.Burn,NowMs))
    { Out.Add({uint8(ELDDemoStatusId::Burn),uint8(FMath::Clamp(Body.Burn.Layers.Num(),1,255)),Body.Burn.ExpiresAtMs}); }
}
