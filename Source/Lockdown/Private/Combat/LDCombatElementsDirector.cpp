#include "Combat/LDCombatDirector.h"
#include "Core/LDLog.h"
#include "Combat/LDCombatElements.h"
#include "Combat/LDCombatKeywords.h"
#include "Combat/LDTacticalRules.h"
#include "Character/LDCharacter.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDGameMode.h"

// Spec §8 six elements and fifteen reactions on Combat's body records (docs/systems/COMBAT_AND_GROWTH.md "元素、反应与地面状态"),
// the element actives' deliveries (S09 line, S10 chain) and the S16 stance; the pure rules live in LDCombatElements.cpp and the
// ground states in LDCombatGroundDirector.cpp.
namespace LDCombatElementsDirectorDetail
{
    double Cm(const FLDDemoRational& Value)
    { return Value.Denominator>0 ? double(Value.Numerator)/double(Value.Denominator) : 0.; }

    FLDElementApplier ApplierOf(const FLDCombatHit& Hit)
    {
        FLDElementApplier Applier;
        Applier.SourceEntity=Hit.SourceEntity;
        Applier.SourceBody=Hit.SourceBody;
        Applier.CreditOwner=Hit.CreditOwner;
        Applier.Source=Hit.Source;
        Applier.Level=FMath::Max(Hit.ElementLevel,1);
        Applier.SourceAPP2=FMath::Max<int64>(Hit.ElementAPP2,0);
        Applier.EffectId=Hit.EffectId;
        Applier.CastLedger=Hit.CastLedger;
        return Applier;
    }

    // A status or settle damage hit of one applier on Target (AI spec §8: never a skill hit, never an element carrier).
    FLDCombatHit SettleHit(const FLDElementApplier& Applier, const FLDCombatBodyRef& Target, const FGuid& EffectId, int64 BaseP2)
    {
        FLDCombatHit Hit;
        Hit.HitId=FGuid::NewGuid();
        Hit.EffectId=EffectId;
        Hit.Target=Target;
        Hit.SourceBody=Applier.SourceBody;
        Hit.SourceEntity=Applier.SourceEntity;
        Hit.CreditOwner=Applier.CreditOwner;
        Hit.Source=Applier.Source;
        Hit.HitClass=ELDCombatHitClass::Ability;
        Hit.BaseP2=BaseP2;
        Hit.DamageType=ELDDemoDamageType::Magical;
        Hit.bStatusSettle=true;
        return Hit;
    }

    FLDElementExtras ExtrasOf(const FLDCombatHit& Hit)
    {
        FLDElementExtras Extras;
        Extras.RotCutBp=Hit.RotCutBp;
        Extras.RotMs=Hit.RotMs;
        return Extras;
    }

    int64 Longer(int64 Ms, int64 Bp) { return Ms/10000*Bp+Ms%10000*Bp/10000; }
}

bool ALDCombatDirector::ElementTerms(FLDElementTerms& Out, FString& Error) const
{
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!Content) { Error=TEXT("Element rules require the approved Demo content."); return false; }
    Out=FLDElementTerms::From(Content->Get().Parameters);
    if (!Out.IsUsable()) { Error=TEXT("The approved Demo content has no usable element terms."); return false; }
    // Spec §11.1 daily modifiers that change element terms for the whole match.
    if (const ILDLifeAuthority* Mode=Authority())
    {
        const FLDDemoDailyParameters& Daily=Content->Get().Parameters.Daily;
        if (Mode->HasDaily(ELDDailyModifier::Rain)) { Out.WetMs+=Daily.RainWetBonusMs; }
        if (Mode->HasDaily(ELDDailyModifier::Cold)) { Out.ChillMs+=Daily.ColdChillBonusMs; }
        if (Mode->HasDaily(ELDDailyModifier::Blackout)) { Out.ShockRatioBp=Out.ShockRatioBp*Daily.BlackoutShockScaleBp/10000; }
        Out.bFogArea=!Mode->HasDaily(ELDDailyModifier::Heat);
    }
    return true;
}

bool ALDCombatDirector::GroundTerms(FLDGroundTerms& Out, FString& Error) const
{
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!Content) { Error=TEXT("Ground states require the approved Demo content."); return false; }
    Out=FLDGroundTerms::From(Content->Get().Parameters);
    if (!Out.IsUsable()) { Error=TEXT("The approved Demo content has no usable ground-state terms."); return false; }
    return true;
}

bool ALDCombatDirector::StampElementSnapshot(const FLDBodyCombatState& Source, FLDCombatHit& InOut, FString& Error) const
{
    InOut.ElementLevel=GrowthLevelOf(Source);
    InOut.ElementAPP2=FMath::Max<int64>(Source.AttributeProjection.Final.APP2,0);
    Error.Reset();
    return true;
}

ELDCombatElement ALDCombatDirector::ElementOfBody(const FLDBodyCombatState& Body, int64 NowMs) const
{
    return LDCombatElements::ElementOf(LDCombatKeywords::BurnActive(Body.Burn,NowMs),Body.ElementMark,NowMs);
}

void ALDCombatDirector::ClearMark(FLDBodyCombatState& Body)
{
    if (Body.ElementMark.Kind==ELDCombatElement::None) { return; }
    const FGuid Slow=Body.ElementMark.SlowEffectId;
    const FGuid AttackSlow=Body.ElementMark.AttackSlowEffectId;
    if (Slow.IsValid()) { Body.Slows.RemoveAll([&Slow](const FLDCombatSlow& Row) { return Row.EffectId==Slow; }); }
    if (AttackSlow.IsValid()) { Body.AttackSlows.RemoveAll([&AttackSlow](const FLDCombatSlow& Row) { return Row.EffectId==AttackSlow; }); }
    Body.ElementMark=FLDCombatElementMark();
    ++Body.StateRevision;
    MarkViewChanged(Body.Victim.EntityId);
}

bool ALDCombatDirector::ApplyElements(FLDBodyCombatState& Target, const FLDCombatHit& Hit, FLDBodyCombatState* Source, int64 NowMs,
    FString& Error, FLDAppliedCardElement* OutCard)
{
    using namespace LDCombatElementsDirectorDetail;
    if (OutCard) { *OutCard=FLDAppliedCardElement(); }
    const FLDElementExtras Extras=ExtrasOf(Hit);
    const bool bLivingSource=Source && !Source->bPendingDeath;
    // A hit carries one element: a card's element on it replaces the hit's own, and of several cards the first in the
    // fixed order (fire, water, shock, chill, rot) applies. So one release never reacts with itself. Ticks, settles,
    // reflections, added damage and shield answers carry none of the source's triggers.
    const bool bCardFire=bLivingSource && Hit.CardBurnStacks>0;
    const bool bCardElement=bCardFire || (bLivingSource && !Hit.CardElements.IsEmpty());
    const auto SourceApplier=[this,Source,CastLedger=Hit.CastLedger](const FLDCombatSourceRef& Ref)
    {
        FLDElementApplier Applier;
        Applier.SourceEntity=Source->Victim.EntityId;
        Applier.SourceBody={Source->Victim,Source->Identity};
        Applier.CreditOwner=Source->Victim.Kind==ELDVictimKind::Contender ? Source->Victim.EntityId : FGuid();
        Applier.Source=Ref;
        Applier.Level=GrowthLevelOf(*Source);
        Applier.SourceAPP2=FMath::Max<int64>(Source->AttributeProjection.Final.APP2,0);
        Applier.CastLedger=CastLedger;
        return Applier;
    };
    // A card's fire on this hit (K19, ApplyCardElement): one application of all its layers.
    if (bCardFire)
    {
        if (!ApplyElement(Target,ELDCombatElement::Fire,SourceApplier(Hit.CardBurnSource),Hit.CardBurnStacks,Hit.HitId,
            NowMs,Error)) { return false; }
        if (OutCard) { OutCard->bApplied=bElementApplied; OutCard->bFire=true; }
        return true;
    }
    // Fire is first in the fixed order, so A15 goes before the other card elements of the hit; without one, the hit's
    // own fire layers below keep their place.
    // A15 (basic_stacks) rides the last step of the basic combo only, like the other basic element cards. Every copy's
    // layers join one application, so a reaction that application triggers never drops the other copies' layers.
    const int32 LastComboStep=Catalog() ? Catalog()->Get().Parameters.Combo.StepRatiosBp.Num() : 0;
    if (bLivingSource && Hit.HitClass==ELDCombatHitClass::BasicPrimary && !Hit.bReflect && !Hit.bAddedDamage && !Hit.bPeriodic
        && !Hit.bStatusSettle && !Hit.bFromShieldTrigger && int32(Hit.ComboStep)==LastComboStep
        && (Hit.BurnStacks<=0 || !Hit.CardElements.IsEmpty()))
    {
        TArray<FLDPassiveEffectRef> Passives;
        if (!CollectPassiveEffects(Source->AttributeProjection,ELDDemoEffectOp::Burn,Passives,Error)) { return false; }
        int32 Stacks=0;
        const FLDPassiveEffectRef* First=nullptr;
        for (const FLDPassiveEffectRef& Ref : Passives)
        {
            if (Ref.Effect.BasicCount<=0) { continue; }
            Stacks+=Ref.Effect.BasicCount;
            if (!First) { First=&Ref; }
        }
        if (First)
        {
            if (!ApplyElement(Target,ELDCombatElement::Fire,SourceApplier(First->Source),Stacks,Hit.HitId,NowMs,Error)) { return false; }
            // Each copy's layers on this primary basic are one trigger, when the application was not dropped.
            if (bElementApplied)
            {
                for (const FLDPassiveEffectRef& Ref : Passives) { if (Ref.Effect.BasicCount>0) { LedgerCardTriggered(*Source,Ref.Key); } }
            }
            return true;
        }
    }
    // Another card's element on this hit (A16-A19, K20-K24, D12): the first in the fixed order.
    if (bCardElement)
    {
        for (const ELDCombatElement Kind : {ELDCombatElement::Water,ELDCombatElement::Shock,ELDCombatElement::Chill,
            ELDCombatElement::Rot})
        {
            for (const auto& Card : Hit.CardElements)
            {
                if (Card.Key!=Kind) { continue; }
                if (!ApplyElement(Target,Kind,SourceApplier(Card.Value),0,Hit.HitId,NowMs,Error)) { return false; }
                if (OutCard) { OutCard->bApplied=bElementApplied; OutCard->Source=Card.Value; }
                return true;
            }
        }
        return true;
    }
    // The hit's own fire layers.
    if (Hit.BurnStacks>0) { return ApplyElement(Target,ELDCombatElement::Fire,ApplierOf(Hit),Hit.BurnStacks,Hit.HitId,NowMs,Error); }
    // The hit's native element (its release snapshot), the first in the fixed order.
    for (const ELDCombatElement Kind : {ELDCombatElement::Water,ELDCombatElement::Shock,ELDCombatElement::Chill,
        ELDCombatElement::Rot})
    {
        if ((Hit.NativeElements & LDCombatElements::Bit(Kind))!=0)
        { return ApplyElement(Target,Kind,ApplierOf(Hit),0,Hit.HitId,NowMs,Error,Extras); }
    }
    return true;
}

bool ALDCombatDirector::ApplyElement(FLDBodyCombatState& Target, ELDCombatElement Kind, const FLDElementApplier& Applier,
    int32 FireStacks, const FGuid& HitId, int64 NowMs, FString& Error, const FLDElementExtras& Extras)
{
    using namespace LDCombatElementsDirectorDetail;
    bElementApplied=false;
    if (Target.bPendingDeath || Kind==ELDCombatElement::None) { return true; }
    FLDElementTerms Terms;
    FLDKeywordTerms Keywords;
    if (!ElementTerms(Terms,Error) || !KeywordTerms(Keywords,Error)) { return false; }
    // The applier's element cards: Y26 lengthens what it applies (copies add), Y28 raises the layer cap of its fire (copies
    // add their raise).
    if (const FLDBodyCombatState* ApplierBody=FindBody(Applier.SourceBody); ApplierBody && !ApplierBody->bPendingDeath)
    {
        TArray<FLDPassiveEffectRef> Soaks, Caps;
        if (!CollectPassiveEffects(ApplierBody->AttributeProjection,ELDDemoEffectOp::ElementDuration,Soaks,Error)
            || !CollectPassiveEffects(ApplierBody->AttributeProjection,ELDDemoEffectOp::BurnCap,Caps,Error)) { return false; }
        int64 DurationBp=10000;
        for (const FLDPassiveEffectRef& Ref : Soaks) { DurationBp+=Ref.Effect.MultiplierBp-10000; }
        if (DurationBp!=10000)
        {
            Terms.WetMs=Longer(Terms.WetMs,DurationBp);
            Terms.ShockMs=Longer(Terms.ShockMs,DurationBp);
            Terms.ChillMs=Longer(Terms.ChillMs,DurationBp);
            Terms.RotMs=Longer(Terms.RotMs,DurationBp);
            Keywords.BurnMs=Longer(Keywords.BurnMs,DurationBp);
        }
        const int32 BaseCap=Keywords.BurnMaxStacks;
        for (const FLDPassiveEffectRef& Ref : Caps) { Keywords.BurnMaxStacks+=FMath::Max(Ref.Effect.Count-BaseCap,0); }
    }
    if (Kind==ELDCombatElement::Fire && FireStacks<=0) { Error=TEXT("A fire application needs at least one layer."); return false; }
    ALDCharacter* TargetActor=ActorOf(Target);
    if (!TargetActor) { Error=TEXT("An element target has no registered actor."); return false; }
    const FVector Foot=FootOf(*TargetActor);
    const ELDCombatElement Held=ElementOfBody(Target,NowMs);
    if (NowMs>=Target.ReactionLockedUntilMs) { Target.WaveAfterExemptChainId.Invalidate(); }
    // One reaction per body per COMBAT.ReactionLockSec: meanwhile an element that would react is dropped whole unless
    // card_wave_after granted one secondary-reaction exemption for the same rule chain on this body.
    if (NowMs<Target.ReactionLockedUntilMs && LDCombatElements::ReactionOf(Held,Kind)!=ELDCombatReaction::None)
    {
        const bool bWaveExempt=Target.WaveAfterExemptChainId.IsValid() && CurrentChain.IsValid()
            && Target.WaveAfterExemptChainId==CurrentChain && !WaveAfterExemptedChains.Contains(CurrentChain);
        if (!bWaveExempt) { return true; }
        Target.WaveAfterExemptChainId.Invalidate();
        if (WaveAfterExemptedChains.Num()>512) { WaveAfterExemptedChains.Reset(); }
        WaveAfterExemptedChains.Add(CurrentChain);
    }
    bElementApplied=true;
    // Who applied the held element, the setters of a reaction's rule chain: the mark's latest applier; for fire, the applier
    // of every layer that still had ticks to settle (filled from the settled shares below).
    TArray<FGuid> HeldSetters;
    if (Held!=ELDCombatElement::None && Held!=ELDCombatElement::Fire) { HeldSetters.Add(Target.ElementMark.Applier.CreditOwner); }
    // A lapsed mark is gone before anything else reads it.
    if (Held==ELDCombatElement::None || Held==ELDCombatElement::Fire) { ClearMark(Target); }
    FLDCombatBurnLayer Layer;
    Layer.SourceEntity=Applier.SourceEntity;
    Layer.SourceBody=Applier.SourceBody;
    Layer.CreditOwner=Applier.CreditOwner;
    Layer.Source=Applier.Source;
    Layer.Level=FMath::Max(Applier.Level,1);
    Layer.SourceAPP2=Applier.SourceAPP2;
    RecordAssistContribution(Target,Applier.CreditOwner,NowMs);
    ++Target.StateRevision;
    MarkViewChanged(Target.Victim.EntityId);
    // N2 A2 reads the last element its owner applied to someone else.
    if (Applier.SourceEntity!=Target.Victim.EntityId)
    {
        if (FLDBodyCombatState* ApplierBody=FindBody(Applier.SourceBody)) { ApplierBody->LastAppliedElement=Kind; }
    }
    const ELDCombatReaction Reaction=LDCombatElements::ReactionOf(Held,Kind);
    // MOMENTUM.Reaction: the contender who triggers a reaction gains it, once per reaction.
    if (Reaction!=ELDCombatReaction::None)
    {
        if (FLDBodyCombatState* Trigger=FindBody(Applier.SourceBody); Trigger && !Trigger->bPendingDeath)
        { GrantMomentum(*Trigger,Catalog()->Get().Parameters.Momentum.ReactionP2,ELDMomentumBook::Reaction); }
    }
    if (Reaction!=ELDCombatReaction::None && Applier.CastLedger && !Applier.CastLedger->bBrainsKeystoneRefunded)
    {
        FLDBodyCombatState* SourceBody=FindBody(Applier.SourceBody);
        if (SourceBody && !SourceBody->bPendingDeath && SourceBody->AttributeProjection.Keystones.bBrains)
        {
            Applier.CastLedger->bBrainsKeystoneRefunded=true;
            const int64 RatioBp=Catalog()->Get().Parameters.KeystoneBrainsMomentumRefundBp;
            const int64 PaidP2=Applier.CastLedger->PaidMomentumP2;
            const int64 RefundP2=PaidP2/10000*RatioBp+PaidP2%10000*RatioBp/10000;
            if (RefundP2>0) { GrantMomentum(*SourceBody,RefundP2,ELDMomentumBook::Reaction); }
            if (FLDCombatEntityRecord* Record=FindRecord(SourceBody->Victim.EntityId))
            {
                bool bAnyReduced=false;
                for (FLDCombatTrack& Track : Record->Tracks)
                {
                    if (Track.Kind==ELDCombatSourceKind::Learned && Track.ReadyAtMs>NowMs)
                    {
                        Track.ReadyAtMs=FMath::Max<int64>(NowMs,Track.ReadyAtMs-1000);
                        bAnyReduced=true;
                    }
                }
                if (bAnyReduced) { MarkViewChanged(SourceBody->Victim.EntityId); }
            }
        }
    }
    if (Reaction==ELDCombatReaction::None)
    {
        if (Kind==ELDCombatElement::Fire)
        {
            if (!LDCombatKeywords::ApplyBurn(Target.Burn,Layer,FireStacks,NowMs,Keywords))
            { Error=TEXT("A fire layer could not be applied with the approved burn terms."); return false; }
        }
        else
        {
            // A new mark, or the same one refreshed (the latest applier holds it; a shock keeps its count, a rot its rhythm
            // and the stronger of its cuts).
            const bool bSame=Held==Kind;
            const FLDCombatElementMark Before=Target.ElementMark;
            ClearMark(Target);
            FLDCombatElementMark& Mark=Target.ElementMark;
            Mark.Kind=Kind;
            Mark.Applier=Applier;
            int64 LifeMs=Kind==ELDCombatElement::Water ? Terms.WetMs : Kind==ELDCombatElement::Shock ? Terms.ShockMs
                : Kind==ELDCombatElement::Chill ? Terms.ChillMs : Terms.RotMs;
            if (Kind==ELDCombatElement::Rot && Extras.RotMs>0) { LifeMs=Extras.RotMs; }
            if (!LDMatchLifecycle::AddDurationMs(NowMs,LifeMs,Mark.ExpiresAtMs,Error)) { return false; }
            if (Kind==ELDCombatElement::Shock && bSame) { Mark.ShockHits=Before.ShockHits; }
            if (Kind==ELDCombatElement::Rot)
            {
                Mark.HealCutBp=Extras.RotCutBp>0 ? Extras.RotCutBp : Terms.RotHealCutBp;
                if (bSame)
                {
                    Mark.HealCutBp=FMath::Max(Mark.HealCutBp,Before.HealCutBp);
                    Mark.ExpiresAtMs=FMath::Max(Mark.ExpiresAtMs,Before.ExpiresAtMs);
                    Mark.NextTickMs=Before.NextTickMs;
                }
                else if (!LDMatchLifecycle::AddDurationMs(NowMs,Terms.RotTickMs,Mark.NextTickMs,Error)) { return false; }
            }
            if (Kind==ELDCombatElement::Water || Kind==ELDCombatElement::Chill)
            {
                FLDControlApplication App;
                App.Kind=ELDControlKind::Slow;
                App.EffectId=FGuid::NewGuid();
                App.SourceEntity=Applier.SourceEntity;
                App.SourceBody=Applier.SourceBody;
                App.CreditOwner=Applier.CreditOwner;
                App.DurationMs=Mark.ExpiresAtMs-NowMs;
                App.RatioBp=Kind==ELDCombatElement::Water ? Terms.WetSlowBp : Terms.ChillMoveSlowBp;
                bool bApplied=false;
                if (!CommitControl(Target,App,NowMs,bApplied,Error)) { return false; }
                Mark.SlowEffectId=App.EffectId;
            }
            if (Kind==ELDCombatElement::Chill)
            {
                FLDCombatSlow Slow;
                Slow.EffectId=FGuid::NewGuid();
                Slow.SourceEntity=Applier.SourceEntity;
                Slow.RatioBp=Terms.ChillAttackSlowBp;
                Slow.ExpiresAtMs=Mark.ExpiresAtMs;
                LDTactical::ApplySlow(Target.AttackSlows,Slow,NowMs);
                Mark.AttackSlowEffectId=Slow.EffectId;
            }
        }
        return ConvertGroundAt(Foot,Kind,Applier,NowMs,Error);
    }
    // A reaction: the held element is consumed; the trigger is the new element's applier. Observers float its name over the target.
    Target.WaveAfterExemptChainId.Invalidate();
    const FLDReactionRule Rule=LDCombatElements::RuleOf(Reaction,Terms);
    Target.Cues.LastReaction=uint8(Reaction);
    Target.Cues.LastReactionAtMs=NowMs;
    Target.Cues.LastReactionElement=uint8(Kind);
    Target.ReactionLockedUntilMs=NowMs+Terms.ReactionLockMs;
    const FLDCombatBodyRef Primary{Target.Victim,Target.Identity};
    const int64 R=LDCombatElements::ReactionDamageP2(Applier.Level,Applier.SourceAPP2,Terms);
    const FGuid ReactionId=FGuid::NewGuid();
    // Fog settles the fire at once: the held layers' remaining ticks, or the new layers' full budget, each attributed to its
    // own applier and source as fire damage. Any other reaction consuming fire ends it unsettled.
    TArray<FLDBurnTickShare> FireShares;
    if (Held==ELDCombatElement::Fire)
    {
        if (Reaction==ELDCombatReaction::Fog) { LDCombatKeywords::BurnRemainingShares(Target.Burn,Keywords,FireShares); }
        for (const FLDBurnTickShare& Share : FireShares) { HeldSetters.AddUnique(Share.CreditOwner); }
        for (const FLDCombatBurnLayer& Burning : Target.Burn.Layers) { HeldSetters.AddUnique(Burning.CreditOwner); }
        Target.Burn=FLDCombatBurn();
    }
    else if (Kind==ELDCombatElement::Fire && Reaction==ELDCombatReaction::Fog)
    {
        FLDCombatBurn Budget;
        if (!LDCombatKeywords::ApplyBurn(Budget,Layer,FireStacks,NowMs,Keywords))
        { Error=TEXT("A reacting fire could not be budgeted with the approved burn terms."); return false; }
        LDCombatKeywords::BurnRemainingShares(Budget,Keywords,FireShares);
    }
    if (Held!=ELDCombatElement::Fire) { ClearMark(Target); }
    for (const FLDBurnTickShare& Share : FireShares)
    {
        FLDElementApplier FireApplier;
        FireApplier.SourceEntity=Share.SourceEntity;
        FireApplier.SourceBody=Share.SourceBody;
        FireApplier.CreditOwner=Share.CreditOwner;
        FireApplier.Source=Share.Source;
        FLDCombatHit Settle=SettleHit(FireApplier,Primary,ReactionId,Share.DamageP2);
        Settle.bBurnTick=true;
        PendingHits.Add(MoveTemp(Settle));
    }
    // The primary target's reaction hit: its damage (a multiple of R) with the reaction's controls and statuses.
    FLDCombatHit Main=SettleHit(Applier,Primary,ReactionId,LDCombatElements::ScaledP2(R,Rule.PrimaryRatioBp));
    Main.ReactionKind=Reaction;
    Main.ReactionConsumed=Held;
    switch (Reaction)
    {
    case ELDCombatReaction::Conduct: Main.SlowBp=Terms.ConductSlowBp; Main.SlowDurationMs=Terms.ConductSlowMs; break;
    case ELDCombatReaction::Overload: Main.BreachStacks=Terms.OverloadBreachStacks; break;
    case ELDCombatReaction::Freeze: Main.RootDurationMs=Terms.FreezeRootMs; break;
    case ELDCombatReaction::Corrode: Main.BreachStacks=Terms.CorrodeBreachStacks; Main.BreachMs=Terms.CorrodeBreachMs; break;
    case ELDCombatReaction::Frostbite: Main.AttackSlowBp=Terms.FrostbiteAttackSlowBp; Main.AttackSlowMs=Terms.FrostbiteMs; break;
    default: break;
    }
    // The chain names a setter only when one applier alone held the consumed element (several burn appliers stay unnamed);
    // every applier is a prompt candidate, told only once this hit commits (ApplyHit).
    if (HeldSetters.Num()==1) { Main.ReactionSetter=HeldSetters[0]; }
    for (const FGuid& Setter : HeldSetters) { if (Setter.IsValid()) { Main.ReactionAssistSetters.AddUnique(Setter); } }
    PendingHits.Add(Main);
    int32 Struck=1;
    // The splash: the other enemies of the trigger around the primary (conduct: the wet ones, each consuming its own wet).
    if (Rule.SplashRadiusCm>0 && Rule.SplashRatioBp>0)
    {
        TArray<FLDCombatBodyRef> Around;
        if (!CollectAreaTargets(Foot,Rule.SplashRadiusCm,FindBody(Applier.SourceBody),Applier.SourceEntity,Around,Error))
        { Error=FString(LDCombatElements::ReactionName(Reaction))+TEXT(": ")+Error; return false; }
        for (const FLDCombatBodyRef& Ref : Around)
        {
            if (Ref==Primary) { continue; }
            FLDBodyCombatState* Other=FindBody(Ref);
            if (!Other || Other->bPendingDeath) { continue; }
            FLDCombatHit Splash=SettleHit(Applier,Ref,ReactionId,LDCombatElements::ScaledP2(R,Rule.SplashRatioBp));
            Splash.ReactionKind=Reaction;
            if (Rule.bSplashWetOnly)
            {
                if (ElementOfBody(*Other,NowMs)!=ELDCombatElement::Water) { continue; }
                const FGuid OtherSetter=Other->ElementMark.Applier.CreditOwner;
                ClearMark(*Other);
                Splash.ReactionConsumed=ELDCombatElement::Water;
                Splash.ReactionSetter=OtherSetter;
                if (OtherSetter.IsValid()) { Splash.ReactionAssistSetters.Add(OtherSetter); }
            }
            PendingHits.Add(MoveTemp(Splash));
            ++Struck;
        }
    }
    // What the reaction leaves on the primary: fog, a shatter, a stronger rot or new fire. Each is the trigger's own application (the held element is gone, so none of them reacts again).
    TOptional<FLDCombatFog> NewFog;
    switch (Reaction)
    {
    case ELDCombatReaction::Fog:
    {
        if (!Terms.bFogArea) { break; }
        Fogs.RemoveAll([&Applier](const FLDCombatFog& Fog) { return Fog.Owner.SourceEntity==Applier.SourceEntity; });
        FLDCombatFog& Fog=Fogs.AddDefaulted_GetRef();
        Fog.EffectId=ReactionId;
        Fog.Owner=Applier;
        Fog.CentreCm=Foot;
        Fog.RadiusCm=Terms.FogRadiusCm;
        Fog.StartMs=NowMs;
        // Its 0 s pulse lands below, inside this batch; the next one follows an interval later.
        if (!LDMatchLifecycle::AddDurationMs(NowMs,Terms.FogIntervalMs,Fog.NextPulseMs,Error)
            || !LDMatchLifecycle::AddDurationMs(NowMs,Terms.FogMs,Fog.EndMs,Error)) { return false; }
        NewFog=Fog;
        break;
    }
    case ELDCombatReaction::Shatter:
        Target.ShatterBp=Terms.ShatterSkillDamageBp;
        if (!LDMatchLifecycle::AddDurationMs(NowMs,Terms.ShatterMs,Target.ShatterUntilMs,Error)) { return false; }
        break;
    case ELDCombatReaction::Ferment:
    {
        FLDElementExtras Strong;
        Strong.RotCutBp=Terms.FermentHealCutBp;
        Strong.RotMs=Terms.FermentRotMs;
        if (!ApplyElement(Target,ELDCombatElement::Rot,Applier,0,HitId,NowMs,Error,Strong)) { return false; }
        break;
    }
    case ELDCombatReaction::Methane:
        if (!ApplyElement(Target,ELDCombatElement::Fire,Applier,Terms.MethaneBurnStacks,HitId,NowMs,Error)) { return false; }
        break;
    default: break;
    }
    // The incoming element also lands on the ground under the primary (before the reaction leaves its own state there).
    if (!ConvertGroundAt(Foot,Kind,Applier,NowMs,Error)) { return false; }
    // The ground state the reaction leaves at the primary's foot.
    if (Rule.Ground!=ELDGroundStateKind::None && !SpawnGround(Rule.Ground,Applier,Foot,NowMs,Error)) { return false; }
    FLDCombatReactionFact Fact;
    Fact.Kind=Reaction;
    Fact.Consumed=Held;
    Fact.TriggerBody=Applier.SourceBody;
    Fact.TriggerEntity=Applier.SourceEntity;
    Fact.TriggerSource=Applier.Source;
    Fact.TriggerEffectId=Applier.EffectId;
    Fact.Primary=Primary;
    Fact.HitId=HitId;
    Fact.AtMs=NowMs;
    OnReactionCompleted(Fact);
    // Spec §6.5: the reaction is one step of the chain it resolved in.
    ChainStep(Applier.SourceEntity,LDChain::ReactionStep(LDCombatElements::ReactionName(Reaction)),NowMs);
    // The trigger's cards on this reaction (Y04, Y05, K25).
    if (!OnCardReaction(Fact,NowMs,Error)) { return false; }
    UE_LOG(LogLDCombat,Log,TEXT("LD_REACTION t=%lld kind=%s held=%s incoming=%s trigger=%s via=%s primary=%s struck=%d r=%s fire_settles=%d ground=%s"),
        NowMs,LDCombatElements::ReactionName(Reaction),LDCombatElements::ElementName(Held),LDCombatElements::ElementName(Kind),
        *LDLog::Id(Applier.SourceEntity),*LDLog::Source(Applier.Source),*LDLog::Id(Target.Victim.EntityId),Struck,*LDLog::P2(R),
        FireShares.Num(),LDCombatElements::GroundName(Rule.Ground));
    // Spec §11.1/§18.1: a new fog's 0 s wet lands before the batch that made it continues (a later element of the same hit
    // already finds its centre body wet), and later bodies of this boundary read the bodies it wetted.
    return !NewFog.IsSet() || PulseFog(NewFog.GetValue(),NowMs,Error);
}

bool ALDCombatDirector::StrikeShock(const FLDBodyCombatState& Holder, const FLDElementApplier& Applier, const FVector& Foot,
    int64 NowMs, FString& Error)
{
    using namespace LDCombatElementsDirectorDetail;
    FLDElementTerms Terms;
    if (!ElementTerms(Terms,Error)) { return false; }
    const int64 Strike=LDCombatElements::ScaledP2(LDCombatElements::ReactionDamageP2(Applier.Level,Applier.SourceAPP2,Terms),
        Terms.ShockRatioBp);
    const FGuid StrikeId=FGuid::NewGuid();
    const FLDCombatBodyRef Self{Holder.Victim,Holder.Identity};
    FLDCombatHit OnHolder=SettleHit(Applier,Self,StrikeId,Strike);
    OnHolder.bOverchargeDischarge=true;
    PendingHits.Add(MoveTemp(OnHolder));
    TArray<FLDCombatBodyRef> Around;
    if (!CollectAreaTargets(Foot,Terms.ShockSplashRadiusCm,FindBody(Applier.SourceBody),Applier.SourceEntity,Around,Error))
    { Error=TEXT("Shock: ")+Error; return false; }
    int32 Splashed=0;
    for (const FLDCombatBodyRef& Ref : Around)
    {
        if (Ref==Self) { continue; }
        FLDCombatHit Splash=SettleHit(Applier,Ref,StrikeId,Strike);
        Splash.bOverchargeDischarge=true;
        PendingHits.Add(MoveTemp(Splash));
        ++Splashed;
    }
    UE_LOG(LogLDCombat,Log,TEXT("LD_SHOCK t=%lld holder=%s applier=%s strike=%s splashed=%d"),NowMs,*LDLog::Id(Holder.Victim.EntityId),
        *LDLog::Id(Applier.SourceEntity),*LDLog::P2(Strike),Splashed);
    return true;
}

bool ALDCombatDirector::CountShock(FLDBodyCombatState& Target, const FVector& TargetFoot, int64 NowMs, FString& Error)
{
    // S10 B1 strikes on every hit for the time it lasts (OnBranchHitTaken) and leaves the count alone.
    if (Target.bPendingDeath || Target.Branch.OverchargeUntilMs>NowMs) { return true; }
    FLDElementTerms Terms;
    if (!ElementTerms(Terms,Error)) { return false; }
    if (!LDCombatElements::CountShockHit(Target.ElementMark,Terms.ShockEveryNth,false,NowMs)) { return true; }
    MarkViewChanged(Target.Victim.EntityId);
    return StrikeShock(Target,Target.ElementMark.Applier,TargetFoot,NowMs,Error);
}

bool ALDCombatDirector::PulseFog(const FLDCombatFog& Fog, int64 NowMs, FString& Error)
{
    TArray<FLDCombatBodyRef> Inside;
    if (!CollectAreaTargets(Fog.CentreCm,Fog.RadiusCm,FindBody(Fog.Owner.SourceBody),Fog.Owner.SourceEntity,Inside,Error))
    { Error=TEXT("Fog: ")+Error; return false; }
    for (const FLDCombatBodyRef& Ref : Inside)
    {
        FLDBodyCombatState* Body=FindBody(Ref);
        if (!Body || Body->bPendingDeath || ElementOfBody(*Body,NowMs)!=ELDCombatElement::None) { continue; }
        if (!ApplyElement(*Body,ELDCombatElement::Water,Fog.Owner,0,Fog.EffectId,NowMs,Error)) { return false; }
    }
    return true;
}

void ALDCombatDirector::OnReactionCompleted(const FLDCombatReactionFact& Fact)
{
    ReactionFacts.Add(Fact);
}

bool ALDCombatDirector::NotifyReactionAssists(const FLDCombatHit& Hit, FString& Error)
{
    ALDGameMode* Mode=Core();
    const FGuid& Trigger=Hit.CreditOwner;
    const FLDCombatEntityRecord* TriggerRecord=Trigger.IsValid() ? FindRecord(Trigger) : nullptr;
    if (!Mode || Hit.ReactionKind==ELDCombatReaction::None || Hit.ReactionConsumed==ELDCombatElement::None
        || !TriggerRecord || TriggerRecord->Kind!=ELDVictimKind::Contender) { Error.Reset(); return true; }
    for (const FGuid& Setter : Hit.ReactionAssistSetters)
    {
        const FLDCombatEntityRecord* SetterRecord=Setter.IsValid() ? FindRecord(Setter) : nullptr;
        if (!SetterRecord || SetterRecord->Kind!=ELDVictimKind::Contender || Setter==Trigger || Setter==Hit.Target.Victim.EntityId
            || ReactionAssists.ContainsByPredicate([&Hit,&Setter](const FReactionAssistSent& Sent)
            { return Sent.ReactionId==Hit.EffectId && Sent.Setter==Setter; })) { continue; }
        FLDReactionAssistView View;
        View.Match=Mode->GetMatchContext().Generation;
        View.Trigger=Trigger;
        View.Consumed=uint8(Hit.ReactionConsumed);
        View.Reaction=uint8(Hit.ReactionKind);
        View.AtMs=Mode->GetAuthorityNowMs();
        FLDReactionAssistView Sent;
        // Unsent (the setter does not see the consumed body now): a later struck body of the same reaction may still tell it.
        const ELDReactionAssistDelivery Result=Mode->SendReactionAssist(Setter,Hit.Target,Hit.SourceBody,View,Sent,Error);
        if (Result==ELDReactionAssistDelivery::AuthorityFault) { Error=TEXT("Reaction prompt sight: ")+Error; return false; }
        if (Result!=ELDReactionAssistDelivery::Sent) { continue; }
        ReactionAssists.Add({Hit.EffectId,Setter,Sent});
        UE_LOG(LogLDCombat,Log,TEXT("LD_REACTION_ASSIST setter=%s trigger=%s named=%d kind=%s target=%s"),*LDLog::Id(Setter),
            *LDLog::Id(Trigger),Sent.Trigger.IsValid(),LDCombatElements::ReactionName(Hit.ReactionKind),*LDLog::Id(Hit.Target.Victim.EntityId));
    }
    Error.Reset();
    return true;
}

bool ALDCombatDirector::ReleaseBrace(FLDBodyCombatState& Body, const FLDDemoEffectDefinition& Effect, const FLDCombatSourceRef& Source,
    const FGuid& EffectId, uint64 Token, int64 NowMs, FString& Error, TSharedPtr<FLDCombatCastLedger> CastLedger)
{
    FLDCombatBrace Brace;
    Brace.EffectId=EffectId;
    Brace.CastLedger=MoveTemp(CastLedger);
    Brace.Source=Source;
    Brace.Token=Token;
    Brace.RatioBp=Effect.RatioBp;
    Brace.CapP2=Effect.LimitP2;
    Brace.BaseP2=Effect.BaseP2;
    Brace.RecordRatioBp=Effect.SecondaryRatioBp;
    Brace.RadiusCm=LDCombatElementsDirectorDetail::Cm(Effect.RadiusCm);
    if (Brace.RatioBp<=0 || Brace.RatioBp>=10000 || Brace.CapP2<=0 || Brace.RadiusCm<=0
        || !LDMatchLifecycle::AddDurationMs(NowMs,Effect.DurationMs,Brace.EndsAtMs,Error))
    {
        if (Error.IsEmpty()) { Error=TEXT("A brace needs a cut below one, a positive cap and a positive radius."); }
        return false;
    }
    // A newer stance replaces the old record; the old one never answers.
    Body.Brace=Brace;
    ++Body.StateRevision;
    MarkViewChanged(Body.Victim.EntityId);
    return true;
}

bool ALDCombatDirector::StartChain(const FLDCombatHit& Payload, const FLDCombatBodyRef& First, const FVector& FirstFoot,
    const FLDDemoEffectDefinition& Bounce, int64 NowMs, FString& Error)
{
    if (Bounce.Count<=0 || Bounce.IntervalMs<=0 || Bounce.RatioBp<=0 || Bounce.RatioBp>=10000)
    { Error=TEXT("A chain needs bounces, a positive interval and a kept share below one."); return false; }
    FLDCombatChain& Chain=Chains.AddDefaulted_GetRef();
    Chain.Payload=Payload;
    Chain.Struck.Add(First.Victim.EntityId);
    Chain.First=First;
    Chain.Last=First;
    Chain.LastFootCm=FirstFoot;
    Chain.Remaining=Bounce.Count;
    Chain.IntervalMs=Bounce.IntervalMs;
    Chain.RatioBp=Bounce.RatioBp;
    Chain.RadiusCm=LDCombatElementsDirectorDetail::Cm(Bounce.RadiusCm);
    return LDMatchLifecycle::AddDurationMs(NowMs,Bounce.IntervalMs,Chain.NextAtMs,Error);
}

bool ALDCombatDirector::CollectLineTargets(const FVector& Origin, const FVector& Direction, double LengthCm, double HalfWidthCm,
    const FLDBodyCombatState& Caster, TArray<TPair<FLDCombatBodyRef,FVector>>& Out, FString& Error) const
{
    Out.Reset();
    const FVector Along=FVector(Direction.X,Direction.Y,0).GetSafeNormal();
    if (Along.IsNearlyZero() || !FMath::IsFinite(LengthCm) || !FMath::IsFinite(HalfWidthCm) || LengthCm<=0 || HalfWidthCm<=0)
    { Error=TEXT("A line needs a planar direction, a positive length and a positive half width."); return false; }
    TArray<FLDCombatBodyRef> Near;
    if (!CollectAreaTargets(Origin,FMath::Sqrt(LengthCm*LengthCm+HalfWidthCm*HalfWidthCm),&Caster,Caster.Victim.EntityId,Near,Error))
    { return false; }
    for (const FLDCombatBodyRef& Ref : Near)
    {
        ALDCharacter* Actor=nullptr;
        if (!FindBody(Ref,&Actor) || !Actor) { continue; }
        const FVector Foot=FootOf(*Actor);
        const FVector Offset(Foot.X-Origin.X,Foot.Y-Origin.Y,0);
        const double Forward=FVector::DotProduct(Offset,Along);
        const double Side=FMath::Abs(Offset.X*Along.Y-Offset.Y*Along.X);
        if (Forward<0 || Forward>LengthCm || Side>HalfWidthCm) { continue; }
        Out.Emplace(Ref,Foot);
    }
    return true;
}

int64 ALDCombatDirector::NextElementsDueMs(int64 NowMs) const
{
    int64 Due=MAX_int64;
    const auto Consider=[&Due,NowMs](int64 At) { if (At>NowMs && At<Due) { Due=At; } };
    for (const FLDCombatFog& Fog : Fogs) { Consider(Fog.NextPulseMs); Consider(Fog.EndMs); }
    for (const FLDCombatChain& Chain : Chains) { Consider(Chain.NextAtMs); }
    for (const auto& Pair : Bodies)
    {
        const FLDBodyCombatState& Body=Pair.Value;
        if (Body.bPendingDeath) { continue; }
        if (Body.ElementMark.Kind!=ELDCombatElement::None) { Consider(Body.ElementMark.ExpiresAtMs); }
        if (Body.ElementMark.Kind==ELDCombatElement::Rot) { Consider(Body.ElementMark.NextTickMs); }
        if (Body.Brace.EffectId.IsValid()) { Consider(Body.Brace.EndsAtMs); }
        if (Body.ShatterUntilMs>0) { Consider(Body.ShatterUntilMs); }
    }
    for (const FLDCombatFan& Fan : Fans) { Consider(Fan.NextBlowMs); Consider(Fan.EndMs); }
    Consider(NextGroundDueMs(NowMs));
    return Due;
}

bool ALDCombatDirector::AdvanceElements(int64 NowMs, FString& Error)
{
    using namespace LDCombatElementsDirectorDetail;
    // Fog pulses: wet on every element-free enemy of the owner inside, at the start (inside the batch that made it) and
    // every interval until it ends.
    for (int32 Index=Fogs.Num()-1; Index>=0; --Index)
    {
        while (Fogs.IsValidIndex(Index) && Fogs[Index].NextPulseMs<=NowMs && Fogs[Index].NextPulseMs<Fogs[Index].EndMs)
        {
            FLDElementTerms Terms;
            if (!ElementTerms(Terms,Error)) { return false; }
            const FLDCombatFog Pulse=Fogs[Index];
            if (!LDMatchLifecycle::AddDurationMs(Pulse.NextPulseMs,Terms.FogIntervalMs,Fogs[Index].NextPulseMs,Error)) { return false; }
            if (!PulseFog(Pulse,NowMs,Error)) { return false; }
        }
        if (Fogs.IsValidIndex(Index) && Fogs[Index].EndMs<=NowMs) { Fogs.RemoveAt(Index); }
    }
    // S10 bounces: the nearest not yet struck enemy around the last struck body; with nobody else near (spec §9, a duel) the
    // bolt bounces back to its first target.
    for (int32 Index=Chains.Num()-1; Index>=0; --Index)
    {
        FLDCombatChain& Chain=Chains[Index];
        while (Chain.Remaining>0 && Chain.NextAtMs<=NowMs)
        {
            ALDCharacter* LastActor=nullptr;
            if (const FLDBodyCombatState* Last=FindBody(Chain.Last,&LastActor); Last && LastActor && !Last->bPendingDeath)
            { Chain.LastFootCm=FootOf(*LastActor); }
            TArray<FLDCombatBodyRef> Around;
            if (!CollectAreaTargets(Chain.LastFootCm,Chain.RadiusCm,FindBody(Chain.Payload.SourceBody),Chain.Payload.SourceEntity,
                Around,Error)) { Error=TEXT("Chain: ")+Error; return false; }
            const FLDCombatBodyRef* Next=nullptr;
            FVector NextFoot=FVector::ZeroVector;
            double NextSquared=0;
            for (const FLDCombatBodyRef& Ref : Around)
            {
                ALDCharacter* Actor=nullptr;
                if (Chain.Struck.Contains(Ref.Victim.EntityId) || !FindBody(Ref,&Actor) || !Actor) { continue; }
                const FVector Foot=FootOf(*Actor);
                const double Squared=FVector::DistSquared2D(Chain.LastFootCm,Foot);
                if (!Next || Squared<NextSquared) { Next=&Ref; NextSquared=Squared; NextFoot=Foot; }
            }
            FLDCombatBodyRef Back;
            if (!Next)
            {
                ALDCharacter* FirstActor=nullptr;
                const FLDBodyCombatState* First=FindBody(Chain.First,&FirstActor);
                if (!First || !FirstActor || First->bPendingDeath) { Chain.Remaining=0; break; }
                Back=Chain.First;
                Next=&Back;
                NextFoot=FootOf(*FirstActor);
            }
            Chain.ScaleBp=Chain.ScaleBp*Chain.RatioBp/10000;
            FLDCombatHit Hit=Chain.Payload;
            Hit.HitId=FGuid::NewGuid();
            Hit.Target=*Next;
            Hit.DeliveryScaleBp=Chain.ScaleBp;
            PendingHits.Add(MoveTemp(Hit));
            Chain.Struck.AddUnique(Next->Victim.EntityId);
            Chain.Last=*Next;
            Chain.LastFootCm=NextFoot;
            --Chain.Remaining;
            if (Chain.Remaining>0 && !LDMatchLifecycle::AddDurationMs(Chain.NextAtMs,Chain.IntervalMs,Chain.NextAtMs,Error)) { return false; }
        }
        if (Chain.Remaining<=0) { Chains.RemoveAt(Index); }
    }
    FLDElementTerms Terms;
    if (!ElementTerms(Terms,Error)) { return false; }
    TArray<TWeakObjectPtr<ALDCharacter>> BodyKeys;
    Bodies.GetKeys(BodyKeys);
    for (const TWeakObjectPtr<ALDCharacter>& Key : BodyKeys)
    {
        FLDBodyCombatState* BodyPtr=Bodies.Find(Key);
        if (!BodyPtr) { continue; }
        FLDBodyCombatState& Body=*BodyPtr;
        ALDCharacter* Actor=Key.Get();
        // Rot ticks due by now (through its end), credited to its latest applier at the applier's snapshot.
        FLDCombatElementMark& Mark=Body.ElementMark;
        while (!Body.bPendingDeath && Mark.Kind==ELDCombatElement::Rot && Mark.NextTickMs>0 && Mark.NextTickMs<=NowMs
            && Mark.NextTickMs<=Mark.ExpiresAtMs)
        {
            FLDCombatHit Tick=SettleHit(Mark.Applier,{Body.Victim,Body.Identity},FGuid::NewGuid(),
                LDCombatElements::RotTickP2(Mark.Applier.SourceAPP2,Terms));
            Tick.bPeriodic=true;
            if (Tick.BaseP2>0) { PendingHits.Add(MoveTemp(Tick)); }
            if (!LDMatchLifecycle::AddDurationMs(Mark.NextTickMs,Terms.RotTickMs,Mark.NextTickMs,Error)) { return false; }
        }
        // A lapsed mark ends (its slows end on the same deadline); a lapsed shatter is gone.
        if (Mark.Kind!=ELDCombatElement::None && Mark.ExpiresAtMs<=NowMs) { ClearMark(Body); }
        if (Body.ShatterUntilMs>0 && Body.ShatterUntilMs<=NowMs) { Body.ShatterBp=0; Body.ShatterUntilMs=0; }
        // S16: a dying life never answers; a living one answers once at the natural end, at its foot then.
        if (!Body.Brace.EffectId.IsValid()) { continue; }
        if (Body.bPendingDeath || !Actor) { Body.Brace=FLDCombatBrace(); continue; }
        if (Body.Brace.EndsAtMs>NowMs) { continue; }
        const FLDCombatBrace Brace=Body.Brace;
        Body.Brace=FLDCombatBrace();
        // Skill branch terms of this brace (S16 A1/A2, B1/B2).
        const FLDCombatBranchState& Branched=Body.Branch;
        const bool bBranch=Branched.BraceEffectId==Brace.EffectId && Brace.EffectId.IsValid();
        ++Body.StateRevision;
        MarkViewChanged(Body.Victim.EntityId);
        TArray<FLDCombatBodyRef> Around;
        if (!CollectAreaTargets(FootOf(*Actor),Brace.RadiusCm,&Body,Body.Victim.EntityId,Around,Error))
        { Error=TEXT("Brace answer: ")+Error; return false; }
        const int64 Amount=LDCombatElements::BraceAnswerP2(Brace);
        const FVector HolderFoot=FootOf(*Actor);
        for (const FLDCombatBodyRef& Ref : Around)
        {
            FLDCombatHit& Hit=PendingHits.AddDefaulted_GetRef();
            Hit.HitId=FGuid::NewGuid();
            Hit.EffectId=Brace.EffectId;
            Hit.Token=Brace.Token;
            Hit.Target=Ref;
            Hit.SourceBody={Body.Victim,Body.Identity};
            Hit.SourceEntity=Body.Victim.EntityId;
            Hit.CreditOwner=Body.Victim.Kind==ELDVictimKind::Contender ? Body.Victim.EntityId : FGuid();
            Hit.Source=Brace.Source;
            Hit.HitClass=ELDCombatHitClass::Ability;
            Hit.BaseP2=Amount;
            Hit.DamageType=ELDDemoDamageType::Magical;
            Hit.bDirectAbilityDamage=true;
            Hit.CastLedger=Brace.CastLedger;
            if (bBranch && Branched.BraceShoveCm>0)
            {
                Hit.KnockbackCm=Branched.BraceShoveCm;
                Hit.KnockbackMs=Branched.BraceShoveMs;
                Hit.bKnockbackFromAnchor=true;
                Hit.KnockbackAnchorCm=HolderFoot;
                Hit.KnockbackSource=Brace.Source;
            }
            if (bBranch && Branched.BraceStunMs>0) { Hit.StunDurationMs=Branched.BraceStunMs; }
            if (!StampSourceTerms(Body,Hit,Error) || !StampElementSnapshot(Body,Hit,Error)) { return false; }
        }
        // S16 A2 heals a share of the record; S16 B2 turns the record into a shield.
        if (bBranch && Branched.BraceHealBp>0 && Brace.RecordedP2>0)
        {
            const int64 Heal=Brace.RecordedP2/10000*Branched.BraceHealBp+Brace.RecordedP2%10000*Branched.BraceHealBp/10000;
            if (Heal>0 && !HealBody(Body,Heal,Brace.Source,nullptr,FGuid(),&Error) && !Error.IsEmpty()) { return false; }
        }
        if (bBranch && Branched.BraceShieldMs>0 && Brace.RecordedP2>0
            && !GrantShieldTo(Body,Brace.RecordedP2,Branched.BraceShieldMs,Brace.EffectId,Body.Victim.EntityId,NowMs,Error,Brace.Source))
        { return false; }
        UE_LOG(LogLDCombat,Log,TEXT("LD_BRACE_ANSWER t=%lld src=%s recorded=%s amount=%s targets=%d"),NowMs,
            *LDLog::Id(Body.Victim.EntityId),*LDLog::P2(Brace.RecordedP2),*LDLog::P2(Amount),Around.Num());
    }
    return true;
}

void ALDCombatDirector::AppendElementStatuses(const FLDBodyCombatState& Body, int64 NowMs, TArray<FLDObservedStatus>& Out) const
{
    if (LDCombatElements::MarkActive(Body.ElementMark,NowMs))
    {
        ELDDemoStatusId Id=ELDDemoStatusId::Wet;
        switch (Body.ElementMark.Kind)
        {
        case ELDCombatElement::Shock: Id=ELDDemoStatusId::Electrified; break;
        case ELDCombatElement::Chill: Id=ELDDemoStatusId::Chilled; break;
        case ELDCombatElement::Rot: Id=ELDDemoStatusId::Rotten; break;
        default: break;
        }
        Out.Add({uint8(Id),1,Body.ElementMark.ExpiresAtMs});
    }
    if (LDCombatElements::BraceActive(Body.Brace,NowMs)) { Out.Add({uint8(ELDDemoStatusId::Braced),1,Body.Brace.EndsAtMs}); }
}
