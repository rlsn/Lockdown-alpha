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
    // The hit's own stacks (an active's payload, a zone pulse or an overload). Fire is an element (ApplyElements).
    Breach(Hit.BreachStacks,Hit.CreditOwner);
    // The living source's held breach trigger on a primary basic hit (basic_stacks: B08). Ticks, pulses, settles,
    // reflections, added damage and shield-triggered answers are none.
    const bool bBasic=Hit.HitClass==ELDCombatHitClass::BasicPrimary;
    if (Source && !Source->bPendingDeath && bBasic && !Hit.bReflect && !Hit.bAddedDamage && !Hit.bPeriodic && !Hit.bStatusSettle
        && !Hit.bFromShieldTrigger)
    {
        TArray<FLDPassiveEffectRef> Passives;
        if (!CollectPassiveEffects(Source->AttributeProjection,ELDDemoEffectOp::Breach,Passives,Error)) { return false; }
        for (const FLDPassiveEffectRef& Ref : Passives) { Breach(Ref.Effect.BasicCount,Hit.CreditOwner); }
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
        // #198: every layer ticks on its own timer; layers ticking at the same instant for one applier and source merge.
        int64 TickAtMs=0;
        while (LDCombatKeywords::PopBurnTick(Body.Burn,NowMs,Terms,TickAtMs,Shares))
        {
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
                // A burn takes its living applier's control amplification (B07) in the damage bonus pool; nothing else of it.
                if (const FLDBodyCombatState* Applier=FindBody(Share.SourceBody); Applier && !Applier->bPendingDeath
                    && Applier->AttributeProjection.ControlAmp.IsSet())
                {
                    Tick.ControlAmpBp=Applier->AttributeProjection.ControlAmp->RatioBp;
                }
            }
            bChanged=true;
        }
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
    { return Row.Effect.Trigger==ELDDemoEffectTrigger::Damaged; });
    if (!Ref) { return true; }
    const FLDDemoEffectDefinition& Effect=Ref->Effect;
    // B09: the fixed amount already arrives grown for the holder's level (CollectPassiveEffects), plus its max-HP share now.
    const int64 Amount=Effect.BaseP2
        +LDCombatKeywordsDetail::Share(Body.Resources.MaxHpP2,Effect.MaxHPRatioBp);
    const FGuid ShieldId=FGuid::NewGuid();
    if (!GrantShieldTo(Body,Amount,Effect.DurationMs,ShieldId,Body.Victim.EntityId,NowMs,Error,Ref->Source)) { return false; }
    // A weaker shield than the one held is not granted (no fake grant, no refresh), and then the rest does not start.
    if (!Body.Shield.IsSet() || Body.Shield->EffectId!=ShieldId) { return true; }
    return LDMatchLifecycle::AddDurationMs(NowMs,Effect.IntervalMs,Body.DamagedShieldReadyAtMs,Error);
}

void ALDCombatDirector::AppendKeywordStatuses(const FLDBodyCombatState& Body, int64 NowMs, TArray<FLDObservedStatus>& Out) const
{
    if (const int64 SilencedUntil=LDTactical::ActiveRootUntilMs(Body.Silences,NowMs); SilencedUntil>0)
    { Out.Add({uint8(ELDDemoStatusId::Silence),1,SilencedUntil}); }
    // The shared breach shows its stacks.
    if (const int32 Stacks=LDCombatKeywords::BreachStacks(Body.SharedBreach,NowMs); Stacks>0)
    { Out.Add({uint8(ELDDemoStatusId::Exposed),uint8(FMath::Clamp(Stacks,1,255)),Body.SharedBreach.ExpiresAtMs}); }
    if (LDCombatKeywords::BurnActive(Body.Burn,NowMs))
    { Out.Add({uint8(ELDDemoStatusId::Burn),uint8(FMath::Clamp(Body.Burn.Layers.Num(),1,255)),LDCombatKeywords::BurnExpiresAtMs(Body.Burn)}); }
    // #198: wet, electrified, the S16 stance and the S21 cut.
    AppendElementStatuses(Body,NowMs,Out);
}
