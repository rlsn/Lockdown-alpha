#include "Combat/LDCombatDirector.h"
#include "Core/LDLog.h"
#include "Combat/LDCombatElements.h"
#include "Combat/LDCombatKeywords.h"
#include "Combat/LDTacticalRules.h"
#include "Character/LDCharacter.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDGameMode.h"

// #198 water, fire and electric on Combat's body records (docs/systems/COMBAT_AND_GROWTH.md "水火电与反应"), the element
// actives' deliveries (S09 line, S10 chain) and the S16 stance; the pure rules live in LDCombatElements.cpp.
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
}

bool ALDCombatDirector::ElementTerms(FLDElementTerms& Out, FString& Error) const
{
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!Content) { Error=TEXT("Element rules require the approved Demo content."); return false; }
    Out=FLDElementTerms::From(Content->Get().Parameters);
    if (!Out.IsUsable()) { Error=TEXT("The approved Demo content has no usable element terms."); return false; }
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
    if (Slow.IsValid()) { Body.Slows.RemoveAll([&Slow](const FLDCombatSlow& Row) { return Row.EffectId==Slow; }); }
    Body.ElementMark=FLDCombatElementMark();
    ++Body.StateRevision;
    MarkViewChanged(Body.Victim.EntityId);
}

bool ALDCombatDirector::ApplyElements(FLDBodyCombatState& Target, const FLDCombatHit& Hit, FLDBodyCombatState* Source, int64 NowMs,
    FString& Error)
{
    using namespace LDCombatElementsDirectorDetail;
    // Fire first: the hit's own layers, then the living source's held burn triggers (basic_stacks on a primary basic, B01)
    // and a component's fire on this hit (B18, ApplyComponentElement), at the living source's level and AP now. Ticks,
    // settles, reflections, added damage and shield answers carry none of the source's triggers.
    if (Hit.BurnStacks>0
        && !ApplyElement(Target,ELDCombatElement::Fire,ApplierOf(Hit),Hit.BurnStacks,Hit.HitId,NowMs,Error)) { return false; }
    const bool bLivingSource=Source && !Source->bPendingDeath;
    const auto SourceApplier=[this,Source](const FLDCombatSourceRef& Ref)
    {
        FLDElementApplier Applier;
        Applier.SourceEntity=Source->Victim.EntityId;
        Applier.SourceBody={Source->Victim,Source->Identity};
        Applier.CreditOwner=Source->Victim.Kind==ELDVictimKind::Contender ? Source->Victim.EntityId : FGuid();
        Applier.Source=Ref;
        Applier.Level=GrowthLevelOf(*Source);
        Applier.SourceAPP2=FMath::Max<int64>(Source->AttributeProjection.Final.APP2,0);
        return Applier;
    };
    if (bLivingSource && Hit.HitClass==ELDCombatHitClass::BasicPrimary && !Hit.bReflect && !Hit.bAddedDamage && !Hit.bPeriodic
        && !Hit.bStatusSettle && !Hit.bFromShieldTrigger)
    {
        TArray<FLDPassiveEffectRef> Passives;
        if (!CollectPassiveEffects(Source->AttributeProjection,ELDDemoEffectOp::Burn,Passives,Error)) { return false; }
        for (const FLDPassiveEffectRef& Ref : Passives)
        {
            if (Ref.Effect.BasicCount<=0) { continue; }
            if (!ApplyElement(Target,ELDCombatElement::Fire,SourceApplier(Ref.Source),Ref.Effect.BasicCount,Hit.HitId,NowMs,Error))
            { return false; }
        }
    }
    if (bLivingSource && Hit.ComponentBurnStacks>0
        && !ApplyElement(Target,ELDCombatElement::Fire,SourceApplier(Hit.ComponentBurnSource),Hit.ComponentBurnStacks,Hit.HitId,
            NowMs,Error)) { return false; }
    if (Hit.bApplyWet && !ApplyElement(Target,ELDCombatElement::Water,ApplierOf(Hit),0,Hit.HitId,NowMs,Error)) { return false; }
    if (Hit.bApplyElectric && !ApplyElement(Target,ELDCombatElement::Electric,ApplierOf(Hit),0,Hit.HitId,NowMs,Error)) { return false; }
    return true;
}

bool ALDCombatDirector::ApplyElement(FLDBodyCombatState& Target, ELDCombatElement Kind, const FLDElementApplier& Applier,
    int32 FireStacks, const FGuid& HitId, int64 NowMs, FString& Error)
{
    using namespace LDCombatElementsDirectorDetail;
    if (Target.bPendingDeath || Kind==ELDCombatElement::None) { return true; }
    FLDElementTerms Terms;
    FLDKeywordTerms Keywords;
    if (!ElementTerms(Terms,Error) || !KeywordTerms(Keywords,Error)) { return false; }
    if (Kind==ELDCombatElement::Fire && FireStacks<=0) { Error=TEXT("A fire application needs at least one layer."); return false; }
    const ELDCombatElement Held=ElementOfBody(Target,NowMs);
    // A lapsed mark is gone before anything else reads it.
    if (Held!=ELDCombatElement::Water && Held!=ELDCombatElement::Electric) { ClearMark(Target); }
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
    const ELDCombatReaction Reaction=LDCombatElements::ReactionOf(Held,Kind);
    if (Reaction==ELDCombatReaction::None)
    {
        if (Kind==ELDCombatElement::Fire)
        {
            if (!LDCombatKeywords::ApplyBurn(Target.Burn,Layer,FireStacks,NowMs,Keywords))
            { Error=TEXT("A fire layer could not be applied with the approved burn terms."); return false; }
            return true;
        }
        // Water or electric: a new mark, or the same one refreshed (the latest applier holds it).
        ClearMark(Target);
        FLDCombatElementMark& Mark=Target.ElementMark;
        Mark.Kind=Kind;
        Mark.Applier=Applier;
        if (!LDMatchLifecycle::AddDurationMs(NowMs,Kind==ELDCombatElement::Water ? Terms.WetMs : Terms.ElectrifiedMs,
            Mark.ExpiresAtMs,Error)) { return false; }
        if (Kind==ELDCombatElement::Water)
        {
            FLDControlApplication App;
            App.Kind=ELDControlKind::Slow;
            App.EffectId=FGuid::NewGuid();
            App.SourceEntity=Applier.SourceEntity;
            App.SourceBody=Applier.SourceBody;
            App.CreditOwner=Applier.CreditOwner;
            App.DurationMs=Terms.WetMs;
            App.RatioBp=Terms.WetSlowBp;
            bool bApplied=false;
            if (!CommitControl(Target,App,NowMs,bApplied,Error)) { return false; }
            Mark.SlowEffectId=App.EffectId;
        }
        return true;
    }
    // A reaction: both elements are consumed; the trigger is the new element's applier.
    const FLDCombatBodyRef Primary{Target.Victim,Target.Identity};
    ALDCharacter* PrimaryActor=ActorOf(Target);
    if (!PrimaryActor) { Error=TEXT("A reacting body has no registered actor."); return false; }
    const FVector PrimaryFoot=FootOf(*PrimaryActor);
    const int64 R=LDCombatElements::ReactionDamageP2(Applier.Level,Applier.SourceAPP2,Terms);
    const FGuid ReactionId=FGuid::NewGuid();
    // The fire part settles at once: the held layers' remaining ticks, or the new layers' full budget, each attributed to
    // its own applier and source as fire damage.
    TArray<FLDBurnTickShare> FireShares;
    if (Held==ELDCombatElement::Fire)
    {
        LDCombatKeywords::BurnRemainingShares(Target.Burn,Keywords,FireShares);
        Target.Burn=FLDCombatBurn();
    }
    else if (Kind==ELDCombatElement::Fire)
    {
        FLDCombatBurn Budget;
        if (!LDCombatKeywords::ApplyBurn(Budget,Layer,FireStacks,NowMs,Keywords))
        { Error=TEXT("A reacting fire could not be budgeted with the approved burn terms."); return false; }
        LDCombatKeywords::BurnRemainingShares(Budget,Keywords,FireShares);
    }
    ClearMark(Target);
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
    TArray<FLDCombatBodyRef> Struck;
    switch (Reaction)
    {
    case ELDCombatReaction::Fog:
    {
        Struck.Add(Primary);
        Fogs.RemoveAll([&Applier](const FLDCombatFog& Fog) { return Fog.Owner.SourceEntity==Applier.SourceEntity; });
        FLDCombatFog& Fog=Fogs.AddDefaulted_GetRef();
        Fog.EffectId=ReactionId;
        Fog.Owner=Applier;
        Fog.CentreCm=PrimaryFoot;
        Fog.RadiusCm=Terms.FogRadiusCm;
        Fog.StartMs=NowMs;
        Fog.NextPulseMs=NowMs;
        if (!LDMatchLifecycle::AddDurationMs(NowMs,Terms.FogMs,Fog.EndMs,Error)) { return false; }
        break;
    }
    case ELDCombatReaction::Conduct:
    {
        // The wet bodies around the primary at this instant; each consumed wet pays once.
        Struck.Add(Primary);
        TArray<FLDCombatBodyRef> Around;
        if (!CollectAreaTargets(PrimaryFoot,Terms.ConductRadiusCm,FindBody(Applier.SourceBody),Applier.SourceEntity,Around,Error))
        { Error=TEXT("Conduct: ")+Error; return false; }
        for (const FLDCombatBodyRef& Ref : Around)
        {
            if (Ref==Primary) { continue; }
            FLDBodyCombatState* Other=FindBody(Ref);
            if (!Other || Other->bPendingDeath || ElementOfBody(*Other,NowMs)!=ELDCombatElement::Water) { continue; }
            ClearMark(*Other);
            Struck.Add(Ref);
        }
        break;
    }
    case ELDCombatReaction::Overload:
    {
        Struck.Add(Primary);
        TArray<FLDCombatBodyRef> Around;
        if (!CollectAreaTargets(PrimaryFoot,Terms.OverloadRadiusCm,FindBody(Applier.SourceBody),Applier.SourceEntity,Around,Error))
        { Error=TEXT("Overload: ")+Error; return false; }
        for (const FLDCombatBodyRef& Ref : Around) { if (Ref!=Primary) { Struck.Add(Ref); } }
        break;
    }
    default:
        Error=TEXT("An unknown element reaction.");
        return false;
    }
    for (const FLDCombatBodyRef& Ref : Struck)
    {
        FLDCombatHit Hit=SettleHit(Applier,Ref,ReactionId,R);
        if (Reaction==ELDCombatReaction::Conduct) { Hit.SlowBp=Terms.ConductSlowBp; Hit.SlowDurationMs=Terms.ConductSlowMs; }
        if (Reaction==ELDCombatReaction::Overload) { Hit.BreachStacks=Terms.OverloadBreachStacks; }
        PendingHits.Add(MoveTemp(Hit));
    }
    FLDCombatReactionFact Fact;
    Fact.Kind=Reaction;
    Fact.TriggerBody=Applier.SourceBody;
    Fact.TriggerEntity=Applier.SourceEntity;
    Fact.TriggerSource=Applier.Source;
    Fact.Primary=Primary;
    Fact.HitId=HitId;
    Fact.AtMs=NowMs;
    OnReactionCompleted(Fact);
    // #198 F C09: the trigger's connector on this applier arms its next primary basic.
    if (!ArmReactionEmpower(Fact,NowMs,Error)) { return false; }
    UE_LOG(LogLDCombat,Log,TEXT("LD_REACTION t=%lld kind=%s trigger=%s via=%s primary=%s struck=%d r=%s fire_settles=%d"),
        NowMs,LDCombatElements::ReactionName(Reaction),*LDLog::Id(Applier.SourceEntity),*LDLog::Source(Applier.Source),
        *LDLog::Id(Target.Victim.EntityId),Struck.Num(),*LDLog::P2(R),FireShares.Num());
    return true;
}

void ALDCombatDirector::OnReactionCompleted(const FLDCombatReactionFact& Fact)
{
    ReactionFacts.Add(Fact);
}

bool ALDCombatDirector::TryDischarge(FLDBodyCombatState& Target, const FVector& TargetFoot, int64 NowMs, FString& Error)
{
    using namespace LDCombatElementsDirectorDetail;
    if (Target.bPendingDeath || ElementOfBody(Target,NowMs)!=ELDCombatElement::Electric || NowMs<Target.DischargeReadyAtMs) { return true; }
    FLDElementTerms Terms;
    if (!ElementTerms(Terms,Error)) { return false; }
    const FLDElementApplier& Applier=Target.ElementMark.Applier;
    TArray<FLDCombatBodyRef> Around;
    if (!CollectAreaTargets(TargetFoot,Terms.DischargeRadiusCm,FindBody(Applier.SourceBody),Applier.SourceEntity,Around,Error))
    { Error=TEXT("Discharge: ")+Error; return false; }
    const FLDCombatBodyRef Self{Target.Victim,Target.Identity};
    const FLDCombatBodyRef* Nearest=nullptr;
    double NearestSquared=0;
    for (const FLDCombatBodyRef& Ref : Around)
    {
        ALDCharacter* Actor=nullptr;
        if (Ref==Self || !FindBody(Ref,&Actor) || !Actor) { continue; }
        const double Squared=FVector::DistSquared2D(TargetFoot,FootOf(*Actor));
        if (!Nearest || Squared<NearestSquared) { Nearest=&Ref; NearestSquared=Squared; }
    }
    // #198 F M11 on this electric source: the applier's most recent direct active target among the legal ones, at x0.85.
    int64 MarkedScaleBp=10000;
    if (const FLDBodyCombatState* ApplierBody=FindBody(Applier.SourceBody); ApplierBody && !ApplierBody->bPendingDeath)
    {
        TArray<FLDPassiveEffectRef> Marked;
        if (!WorkshopOn(ApplierBody->AttributeProjection,ELDDemoEffectOp::MarkedCounter,Applier.Source,Marked,Error)) { return false; }
        for (const FLDPassiveEffectRef& Mod : Marked)
        {
            MarkedScaleBp=MarkedScaleBp/10000*Mod.Effect.MultiplierBp+MarkedScaleBp%10000*Mod.Effect.MultiplierBp/10000;
            for (int32 Index=ApplierBody->DirectHitMarks.Num()-1; Index>=0; --Index)
            {
                const FLDCombatBodyMark& Mark=ApplierBody->DirectHitMarks[Index];
                const FLDCombatBodyRef* Legal=Around.FindByPredicate([&Mark,&Self](const FLDCombatBodyRef& Ref)
                { return Ref==Mark.Body && Ref!=Self; });
                if (NowMs-Mark.AtMs<=Mod.Effect.DurationMs && Legal) { Nearest=Legal; break; }
            }
        }
    }
    if (!Nearest) { return true; }
    if (!LDMatchLifecycle::AddDurationMs(NowMs,Terms.DischargeIntervalMs,Target.DischargeReadyAtMs,Error)) { return false; }
    FLDCombatHit Discharge=SettleHit(Applier,*Nearest,FGuid::NewGuid(),LDCombatElements::ReactionDamageP2(Applier.Level,Applier.SourceAPP2,Terms));
    Discharge.PackageScaleBp=MarkedScaleBp;
    PendingHits.Add(MoveTemp(Discharge));
    UE_LOG(LogLDCombat,Log,TEXT("LD_DISCHARGE t=%lld from=%s to=%s applier=%s"),NowMs,*LDLog::Id(Target.Victim.EntityId),
        *LDLog::Id(Nearest->Victim.EntityId),*LDLog::Id(Applier.SourceEntity));
    return true;
}

bool ALDCombatDirector::ReleaseBrace(FLDBodyCombatState& Body, const FLDDemoEffectDefinition& Effect, const FLDCombatSourceRef& Source,
    const FGuid& EffectId, uint64 Token, int64 NowMs, FString& Error)
{
    FLDCombatBrace Brace;
    Brace.EffectId=EffectId;
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
        if (Body.Brace.EffectId.IsValid()) { Consider(Body.Brace.EndsAtMs); }
        if (Body.Grievous.ExpiresAtMs>0) { Consider(Body.Grievous.ExpiresAtMs); }
    }
    return Due;
}

bool ALDCombatDirector::AdvanceElements(int64 NowMs, FString& Error)
{
    using namespace LDCombatElementsDirectorDetail;
    // Fog pulses: wet on every element-free enemy of the owner inside, at the start and every interval until it ends.
    for (int32 Index=Fogs.Num()-1; Index>=0; --Index)
    {
        FLDCombatFog& Fog=Fogs[Index];
        while (Fog.NextPulseMs<=NowMs && Fog.NextPulseMs<Fog.EndMs)
        {
            FLDElementTerms Terms;
            if (!ElementTerms(Terms,Error)) { return false; }
            TArray<FLDCombatBodyRef> Inside;
            if (!CollectAreaTargets(Fog.CentreCm,Fog.RadiusCm,FindBody(Fog.Owner.SourceBody),Fog.Owner.SourceEntity,Inside,Error))
            { Error=TEXT("Fog: ")+Error; return false; }
            for (const FLDCombatBodyRef& Ref : Inside)
            {
                FLDBodyCombatState* Body=FindBody(Ref);
                if (!Body || Body->bPendingDeath || ElementOfBody(*Body,NowMs)!=ELDCombatElement::None) { continue; }
                if (!ApplyElement(*Body,ELDCombatElement::Water,Fog.Owner,0,Fog.EffectId,NowMs,Error)) { return false; }
            }
            if (!LDMatchLifecycle::AddDurationMs(Fog.NextPulseMs,Terms.FogIntervalMs,Fog.NextPulseMs,Error)) { return false; }
        }
        if (Fog.EndMs<=NowMs) { Fogs.RemoveAt(Index); }
    }
    // S10 bounces: the nearest not yet struck enemy around the last struck body.
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
            if (!Next) { Chain.Remaining=0; break; }
            Chain.ScaleBp=Chain.ScaleBp*Chain.RatioBp/10000;
            FLDCombatHit Hit=Chain.Payload;
            Hit.HitId=FGuid::NewGuid();
            Hit.Target=*Next;
            Hit.DeliveryScaleBp=Chain.ScaleBp;
            PendingHits.Add(MoveTemp(Hit));
            Chain.Struck.Add(Next->Victim.EntityId);
            Chain.Last=*Next;
            Chain.LastFootCm=NextFoot;
            --Chain.Remaining;
            if (Chain.Remaining>0 && !LDMatchLifecycle::AddDurationMs(Chain.NextAtMs,Chain.IntervalMs,Chain.NextAtMs,Error)) { return false; }
        }
        if (Chain.Remaining<=0) { Chains.RemoveAt(Index); }
    }
    for (auto& Pair : Bodies)
    {
        FLDBodyCombatState& Body=Pair.Value;
        ALDCharacter* Actor=Pair.Key.Get();
        // A lapsed mark ends (its wet slow ends on the same deadline); a lapsed grievous clears.
        if (Body.ElementMark.Kind!=ELDCombatElement::None && Body.ElementMark.ExpiresAtMs<=NowMs) { ClearMark(Body); }
        if (Body.Grievous.ExpiresAtMs>0 && Body.Grievous.ExpiresAtMs<=NowMs)
        {
            Body.Grievous=FLDCombatGrievous();
            ++Body.StateRevision;
            MarkViewChanged(Body.Victim.EntityId);
        }
        // S16: a dying life never answers; a living one answers once at the natural end, at its foot then.
        if (!Body.Brace.EffectId.IsValid()) { continue; }
        if (Body.bPendingDeath || !Actor) { Body.Brace=FLDCombatBrace(); continue; }
        if (Body.Brace.EndsAtMs>NowMs) { continue; }
        const FLDCombatBrace Brace=Body.Brace;
        Body.Brace=FLDCombatBrace();
        ++Body.StateRevision;
        MarkViewChanged(Body.Victim.EntityId);
        TArray<FLDCombatBodyRef> Around;
        if (!CollectAreaTargets(FootOf(*Actor),Brace.RadiusCm,&Body,Body.Victim.EntityId,Around,Error))
        { Error=TEXT("Brace answer: ")+Error; return false; }
        const int64 Amount=LDCombatElements::BraceAnswerP2(Brace);
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
            if (!StampSourceTerms(Body,Hit,Error) || !StampElementSnapshot(Body,Hit,Error)) { return false; }
        }
        UE_LOG(LogLDCombat,Log,TEXT("LD_BRACE_ANSWER t=%lld src=%s recorded=%s amount=%s targets=%d"),NowMs,
            *LDLog::Id(Body.Victim.EntityId),*LDLog::P2(Brace.RecordedP2),*LDLog::P2(Amount),Around.Num());
    }
    return true;
}

void ALDCombatDirector::AppendElementStatuses(const FLDBodyCombatState& Body, int64 NowMs, TArray<FLDObservedStatus>& Out) const
{
    if (LDCombatElements::MarkActive(Body.ElementMark,NowMs))
    {
        const ELDDemoStatusId Id=Body.ElementMark.Kind==ELDCombatElement::Water ? ELDDemoStatusId::Wet : ELDDemoStatusId::Electrified;
        Out.Add({uint8(Id),1,Body.ElementMark.ExpiresAtMs});
    }
    if (LDCombatElements::BraceActive(Body.Brace,NowMs)) { Out.Add({uint8(ELDDemoStatusId::Braced),1,Body.Brace.EndsAtMs}); }
    if (Body.Grievous.ExpiresAtMs>NowMs) { Out.Add({uint8(ELDDemoStatusId::Soured),1,Body.Grievous.ExpiresAtMs}); }
}
