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

FLDCombatHit ALDCombatDirector::EchoHit(const FLDCombatHit& Template, int64 RatioBp)
{
    FLDCombatHit Echo=Template;
    Echo.EffectId=FGuid::NewGuid();
    const int64 Ratio=FMath::Clamp<int64>(RatioBp,0,10000);
    const auto Scale=[Ratio](int64 Value) { return Value/10000*Ratio+Value%10000*Ratio/10000; };
    Echo.BaseP2=Scale(Template.BaseP2);
    Echo.ADRatioBp=Scale(Template.ADRatioBp);
    Echo.APRatioBp=Scale(Template.APRatioBp);
    Echo.MaxHPRatioBp=Scale(Template.MaxHPRatioBp);
    return Echo;
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
    // The living source's passives: basic_stacks on a primary basic hit, stacks on a learned Q/W hit. Ticks, pulses,
    // reflections, added damage and shield-triggered answers are neither.
    const bool bBasic=Hit.HitClass==ELDCombatHitClass::BasicPrimary;
    const bool bSkill=Hit.HitClass==ELDCombatHitClass::Ability && Hit.Source.Kind==ELDCombatSourceKind::Learned && Hit.Token!=0;
    if (Source && !Source->bPendingDeath && (bBasic || bSkill) && !Hit.bReflect && !Hit.bAddedDamage && !Hit.bPeriodic
        && !Hit.bFromShieldTrigger)
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
                // A burn takes its living applier's slowed-or-controlled amplification (v7 4.2); nothing else of it.
                if (const FLDBodyCombatState* Applier=FindBody(Share.SourceBody); Applier && !Applier->bPendingDeath
                    && Applier->AttributeProjection.ControlAmp.IsSet())
                {
                    Tick.ControlAmpBp=Applier->AttributeProjection.ControlAmp->RatioBp;
                    Tick.ControlAmpBelowBp=Applier->AttributeProjection.ControlAmp->LowHPBelowBp;
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
    const int64 Amount=LDCombatKeywords::GrowthScaledP2(Effect.BaseP2,GrowthLevelOf(Body))
        +LDCombatKeywordsDetail::Share(Body.Resources.MaxHpP2,Effect.MaxHPRatioBp);
    const FGuid ShieldId=FGuid::NewGuid();
    if (!GrantShieldTo(Body,Amount,Effect.DurationMs,ShieldId,Body.Victim.EntityId,false,NowMs,Error)) { return false; }
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

bool ALDCombatDirector::BurstBrokenShield(FLDBodyCombatState& Body, const FGuid& BrokenShieldId, int64 NowMs, FString& Error)
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
    for (const FLDCombatBodyRef& Target : Targets)
    {
        FLDCombatHit& Hit=PendingHits.AddDefaulted_GetRef();
        Hit.HitId=FGuid::NewGuid();
        Hit.EffectId=EffectId;
        Hit.Target=Target;
        Hit.SourceBody={Body.Victim,Body.Identity};
        Hit.SourceEntity=Body.Victim.EntityId;
        Hit.CreditOwner=Body.Victim.Kind==ELDVictimKind::Contender ? Body.Victim.EntityId : FGuid();
        Hit.Source=Burst.Source;
        Hit.HitClass=ELDCombatHitClass::Ability;
        Hit.BaseP2=LDCombatKeywordsDetail::Share(Burst.GrantedP2,Burst.RatioBp);
        Hit.DamageType=ELDDemoDamageType::Magical;
        // A burst never grants another shield-triggered effect.
        Hit.bFromShieldTrigger=true;
    }
    UE_LOG(LogLDCombat,Display,TEXT("LD_COMBAT_SHIELD_BURST holder=%s targets=%d at=%lld"),*LDLog::Id(Body.Victim.EntityId),
        Targets.Num(),NowMs);
    return true;
}

void ALDCombatDirector::AppendKeywordStatuses(const FLDBodyCombatState& Body, int64 NowMs, TArray<FLDObservedStatus>& Out) const
{
    if (const int64 SilencedUntil=LDTactical::ActiveRootUntilMs(Body.Silences,NowMs); SilencedUntil>0)
    { Out.Add({uint8(ELDDemoStatusId::Silence),1,SilencedUntil}); }
    // The shared breach shows its stacks; a legacy shred still active extends the same cue's deadline.
    if (const int32 Stacks=LDCombatKeywords::BreachStacks(Body.SharedBreach,NowMs); Stacks>0)
    {
        const int64 Until=FMath::Max(Body.SharedBreach.ExpiresAtMs,LDTactical::BreachUntilMs(Body.Breaches,NowMs));
        Out.Add({uint8(ELDDemoStatusId::Exposed),uint8(FMath::Clamp(Stacks,1,255)),Until});
    }
    if (LDCombatKeywords::BurnActive(Body.Burn,NowMs))
    { Out.Add({uint8(ELDDemoStatusId::Burn),uint8(FMath::Clamp(Body.Burn.Layers.Num(),1,255)),Body.Burn.ExpiresAtMs}); }
}

bool ALDCombatDirector::ResolveEchoes(FString& Error)
{
    if (Echoes.IsEmpty()) { return true; }
    const ALDGameMode* Mode=Core();
    if (!Mode) { Error=TEXT("Second blasts require the current core."); return false; }
    const int64 NowMs=Mode->GetAuthorityNowMs();
    TArray<FLDCombatEcho> Due;
    for (int32 Index=Echoes.Num()-1; Index>=0; --Index)
    {
        if (Echoes[Index].AtMs>NowMs) { continue; }
        Due.Insert(MoveTemp(Echoes[Index]),0);
        Echoes.RemoveAt(Index);
    }
    for (const FLDCombatEcho& Echo : Due)
    {
        // Only the same, still living body life blasts again, around where it stands now.
        ALDCharacter* Actor=nullptr;
        FLDBodyCombatState* Body=FindBody(Echo.Body,&Actor);
        if (!Body || !Actor || Body->bPendingDeath) { continue; }
        TArray<FLDCombatBodyRef> Targets;
        if (!CollectAreaTargets(FootOf(*Actor),Echo.RadiusCm,Body,Body->Victim.EntityId,Targets,Error))
        { Error=TEXT("Second blast: ")+Error; return false; }
        for (const FLDCombatBodyRef& Target : Targets)
        {
            FLDCombatHit Hit=Echo.Template;
            Hit.HitId=FGuid::NewGuid();
            Hit.Target=Target;
            PendingHits.Add(Hit);
        }
        Body->LastCombatAtMs=NowMs;
        ++Body->StateRevision;
        MarkViewChanged(Body->Victim.EntityId);
    }
    return true;
}

bool ALDCombatDirector::ResolveWallChecks(FString& Error)
{
    if (WallChecks.IsEmpty()) { return true; }
    const ALDGameMode* Mode=Core();
    if (!Mode) { Error=TEXT("Wall checks require the current core."); return false; }
    const int64 NowMs=Mode->GetAuthorityNowMs();
    TArray<FLDCombatWallCheck> Due;
    for (int32 Index=WallChecks.Num()-1; Index>=0; --Index)
    {
        if (WallChecks[Index].AtMs>NowMs) { continue; }
        Due.Insert(WallChecks[Index],0);
        WallChecks.RemoveAt(Index);
    }
    for (const FLDCombatWallCheck& Check : Due)
    {
        ALDCharacter* Actor=nullptr;
        FLDBodyCombatState* Body=FindBody(Check.Target,&Actor);
        if (!Body || !Actor || Body->bPendingDeath) { continue; }
        const FVector EndFoot=FootOf(*Actor);
        const bool bWall=LDCombatKeywords::KnockbackHitWall(Check.StartFootCm,EndFoot,Check.ThresholdCm);
        UE_LOG(LogLDCombat,Display,TEXT("LD_COMBAT_KNOCKBACK_END victim=%s travelled_cm=%.0f wall=%d"),*LDLog::Id(Body->Victim.EntityId),
            FVector::Dist2D(Check.StartFootCm,EndFoot),int32(bWall));
        if (!bWall) { continue; }
        FLDControlApplication App;
        App.Kind=ELDControlKind::Stun;
        App.EffectId=Check.EffectId;
        App.SourceEntity=Check.SourceEntity;
        App.SourceBody=Check.SourceBody;
        App.CreditOwner=Check.CreditOwner;
        App.DurationMs=Check.StunMs;
        bool bApplied=false;
        if (!CommitControl(*Body,App,NowMs,bApplied,Error)) { return false; }
        if (bApplied) { Body->Cues.LastHitAtMs=NowMs; }
    }
    return true;
}
