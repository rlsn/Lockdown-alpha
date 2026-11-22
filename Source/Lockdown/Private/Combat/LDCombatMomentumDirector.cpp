#include "Combat/LDCombatDirector.h"

#include "Combat/LDCombatElements.h"
#include "Combat/LDCombatSourceRules.h"
#include "Combat/LDMomentum.h"
#include "Core/LDLog.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDSkillGrowth.h"
#include "Framework/LDGameMode.h"

// Momentum (劲), overdrive charges and the momentum income hooks of Combat (docs/systems/COMBAT_AND_GROWTH.md
// "劲、连击、爆劲与完美闪避"). The pure rules are LDMomentum; this file owns the body records they change.

bool ALDCombatDirector::ResolvePerfectDodges(const FGuid& EffectId, const FGuid& SourceEntity, TFunctionRef<bool(const FVector&)> InArea,
    int64 NowMs, FString& Error)
{
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!Content) { Error=TEXT("A perfect dodge needs the approved momentum terms."); return false; }
    const FLDDemoMomentumParameters& M=Content->Get().Parameters.Momentum;
    PerfectDodgeMarks.RemoveAll([NowMs,&M](const TTuple<FGuid,FGuid,int64>& Mark) { return NowMs-Mark.Get<2>()>M.PerfectDodgeWindowMs*4; });
    for (auto& Pair : Bodies)
    {
        FLDBodyCombatState& Body=Pair.Value;
        ALDCharacter* Actor=Pair.Key.Get();
        if (!Actor || Body.bPendingDeath || Body.Victim.Kind!=ELDVictimKind::Contender || Body.Victim.EntityId==SourceEntity
            || !Body.DashStartedAtMs.IsSet()) { continue; }
        const bool bStartInside=InArea(Body.DashStartFootCm);
        const bool bNowInside=InArea(FootOf(*Actor));
        if (!LDMomentum::IsPerfectDodge(M.PerfectDodgeWindowMs,Body.DashStartedAtMs.GetValue(),NowMs,bStartInside,bNowInside)) { continue; }
        const FGuid Dodger=Body.Victim.EntityId;
        if (PerfectDodgeMarks.ContainsByPredicate([&EffectId,&Dodger](const TTuple<FGuid,FGuid,int64>& Mark)
            { return Mark.Get<0>()==EffectId && Mark.Get<1>()==Dodger; })) { continue; }
        PerfectDodgeMarks.Emplace(EffectId,Dodger,NowMs);
        GrantMomentum(Body,M.PerfectDodgeP2,ELDMomentumBook::Dodge);
        ++Body.PerfectDodges;
        Body.LastPerfectDodgeAtMs=NowMs;
        MarkViewChanged(Dodger);
        // D04 and D09 answer the dodge.
        if (!OnPerfectDodgeCards(Body,NowMs,Error)) { return false; }
        UE_LOG(LogLDCombat,Log,TEXT("LD_PERFECT_DODGE t=%lld entity=%s life=%u effect=%s source=%s momentum=%s"),NowMs,*LDLog::Id(Dodger),
            Body.Victim.Life,*LDLog::Id(EffectId),*LDLog::Id(SourceEntity),*LDLog::P2(Body.Resources.MomentumP2));
    }
    return true;
}

int64 ALDCombatDirector::GrantMomentum(FLDBodyCombatState& Body, int64 AmountP2, ELDMomentumBook Kind)
{
    const FLDDemoContentSnapshot* Content=Catalog();
    // Camp members hold no momentum (POLICY.CampMomentumPolicy=NotApplicable); a dying body takes nothing.
    if (!Content || AmountP2==0 || Body.bPendingDeath || Body.Victim.Kind!=ELDVictimKind::Contender) { return 0; }
    int64 Applied=0;
    Body.Resources.MomentumP2=LDMomentum::Apply(Content->Get().Parameters.Momentum,Body.Resources.MomentumP2,AmountP2,Applied);
    if (Applied==0) { return 0; }
    // A spend or a gain changes the direction of the sub-P2 drift: its remainder belongs to the old value.
    Body.MomentumMicroP2=0;
    LDMomentum::FLedger& Ledger=Body.MomentumLedger;
    switch (Kind)
    {
    case ELDMomentumBook::Basic: Ledger.BasicP2+=Applied; break;
    case ELDMomentumBook::Combat: Ledger.CombatP2+=Applied; break;
    case ELDMomentumBook::Skill: Ledger.SkillP2+=Applied; break;
    case ELDMomentumBook::Reaction: Ledger.ReactionP2+=Applied; break;
    case ELDMomentumBook::Dodge: Ledger.DodgeP2+=Applied; break;
    case ELDMomentumBook::Idle: Ledger.IdleP2+=Applied; break;
    case ELDMomentumBook::Spent: Ledger.SpentP2-=Applied; break;
    default: Ledger.OtherP2+=Applied; break;
    }
    // A bar that drops below the overdrive threshold ends a charge that waits for it.
    if (Body.OverdriveCharge.IsSet() && Body.Resources.MomentumP2<OverdriveThresholdOf(Body)) { EndOverdriveCharge(Body); }
    ++Body.Resources.Revision;
    MarkViewChanged(Body.Victim.EntityId);
    return Applied;
}

bool ALDCombatDirector::SettleMomentum(FLDBodyCombatState& Body, const FLDDemoMomentumParameters& Momentum, int64 FromMs,
    int64 NowMs, int64& InOutP2, FString& Error)
{
    int64 CombatP2=0, IdleP2=0;
    if (!LDMomentum::Settle(Momentum,Body.AttributeProjection.Final.MomentumRegenPerSec,
        Body.MomentumEngagedAtMs,FromMs,NowMs,InOutP2,Body.MomentumMicroP2,CombatP2,IdleP2,Error)) { return false; }
    Body.MomentumLedger.CombatP2+=CombatP2;
    Body.MomentumLedger.IdleP2+=IdleP2;
    return true;
}

bool ALDCombatDirector::OnMomentumHit(FLDBodyCombatState& Source, const FLDCombatHit& Hit, const FLDBodyCombatState& Victim,
    bool bNativeDirect, FString& Error)
{
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!Content) { Error=TEXT("Momentum income requires the approved Demo content."); return false; }
    if (Source.Victim.Kind!=ELDVictimKind::Contender || Hit.bReflect) { return true; }
    const FLDDemoMomentumParameters& P=Content->Get().Parameters.Momentum;
    if (Hit.HitClass==ELDCombatHitClass::BasicPrimary && Hit.BasicIntervalMs>0)
    {
        // A primary basic gains by the interval it committed: the gain per second does not depend on attack speed.
        GrantMomentum(Source,LDMomentum::BasicGainP2(P,Hit.BasicIntervalMs),ELDMomentumBook::Basic);
        return true;
    }
    // One MOMENTUM.SkillHit per paid release and body: the release's first native direct hit on it.
    if (bNativeDirect && Hit.CastLedger && !Hit.CastLedger->bAutoRecast && Hit.Source.Kind==ELDCombatSourceKind::Learned)
    {
        const FLDCombatBodyRef Struck{Victim.Victim,Victim.Identity};
        if (!Hit.CastLedger->MomentumStruck.Contains(Struck))
        {
            Hit.CastLedger->MomentumStruck.Add(Struck);
            GrantMomentum(Source,P.SkillHitP2,ELDMomentumBook::Skill);
        }
    }
    return true;
}

void ALDCombatDirector::EndOverdriveCharge(FLDBodyCombatState& Body)
{
    if (!Body.OverdriveCharge.IsSet()) { return; }
    Body.OverdriveCharge.Reset();
    ++Body.StateRevision;
    MarkViewChanged(Body.Victim.EntityId);
}

void ALDCombatDirector::SubmitCharge(const FLDCombatCaller& Caller, const FLDCombatChargeRequest& Request)
{
    ALDGameMode* Mode=Core();
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!Mode || !Content || Caller.Kind!=ELDVictimKind::Contender || Caller.Match!=Mode->GetMatchContext()
        || Request.Generation!=Caller.Match.Generation || !Mode->IsGameplayWriteOpen(Caller.Match))
    { return; }
    if (Request.Entry!=ELDCombatEntry::Q && Request.Entry!=ELDCombatEntry::W && Request.Entry!=ELDCombatEntry::E) { return; }
    FLDCombatEntityRecord* Record=FindRecord(Caller.EntityId);
    FLDBodyCombatState* Body=FindEntityBody(Caller.EntityId);
    if (!Record || !Body || Body->bPendingDeath || Body->Victim.Life!=Request.Life) { return; }
    if (!Request.bCharging)
    {
        if (Body->OverdriveCharge.IsSet() && Body->OverdriveCharge->Entry==Request.Entry) { EndOverdriveCharge(*Body); }
        return;
    }
    // A charge is shown only for a held learned active with a full bar that the body may cast now.
    FLDCombatSourceRef Source;
    const FLDDemoAbilityDefinition* Ability=nullptr;
    const int64 NowMs=Mode->GetAuthorityNowMs();
    if (!ResolveEntry(*Record,Request.Entry,Source,Ability) || Source.Kind!=ELDCombatSourceKind::Learned
        || OverdriveThresholdOf(*Body)<=0 || Body->Resources.MomentumP2<OverdriveThresholdOf(*Body)
        || IsCastBlocked(*Body,Source.Kind) || Mode->IsInputLocked(Caller.Match,Caller.EntityId)
        || Body->Knockbacks.ContainsByPredicate([NowMs](const FLDCombatRoot& Row) { return Row.ExpiresAtMs>NowMs; }))
    { return; }
    if (Body->OverdriveCharge.IsSet() && Body->OverdriveCharge->Entry==Request.Entry) { return; }
    FLDCombatOverdriveCharge& Charge=Body->OverdriveCharge.Emplace();
    Charge.Entry=Request.Entry;
    Charge.StartMs=NowMs;
    ++Body->StateRevision;
    MarkViewChanged(Caller.EntityId);
}

bool ALDCombatDirector::ApplyOverdrive(const FLDBodyCombatState& Body, const FLDDemoAbilityDefinition& Ability,
    FLDCombatActiveTerms& Terms, FString& Error) const
{
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!Content) { Error=TEXT("Overdrive requires the approved Demo content."); return false; }
    const FLDDemoContentParameters& P=Content->Get().Parameters;
    const FLDDemoOverdriveParameters& O=P.Overdrive;
    FLDDemoAbilityValues& Values=Terms.Values;
    // OVERDRIVE.Scale (Y11's own) on every base amount and ratio (damage, shield, heal, reflection); control times never scale.
    const int64 ScaleBp=OverdriveScaleOf(Body);
    if (!LDSkillGrowth::ScaleValues(Values,ScaleBp,ScaleBp))
    { Error=FString::Printf(TEXT("Overdrive of %s leaves the checked numeric domain."),*Ability.Id); return false; }
    const auto Find=[&Values](ELDDemoEffectOp Op)
    { return Values.Effects.FindByPredicate([Op](const FLDDemoEffectDefinition& Effect) { return Effect.GetOp()==Op; }); };
    auto MapCardElement=[](ELDDemoElement Elem)->ELDDemoSkillElement
    {
        switch (Elem)
        {
        case ELDDemoElement::Fire: return ELDDemoSkillElement::Fire;
        case ELDDemoElement::Water: return ELDDemoSkillElement::Water;
        case ELDDemoElement::Electric: return ELDDemoSkillElement::Shock;
        case ELDDemoElement::Chill: return ELDDemoSkillElement::Chill;
        case ELDDemoElement::Rot: return ELDDemoSkillElement::Rot;
        default: return ELDDemoSkillElement::None;
        }
    };
    ELDDemoSkillElement EffectiveElement=Ability.Element;
    if (EffectiveElement==ELDDemoSkillElement::None)
    {
        if (const FLDCombatEntityRecord* Record=FindRecord(Body.Victim.EntityId))
        {
            if (const FLDLearnedActiveValue* Learned=Record->Build.Actives.FindByPredicate(
                [&Ability](const FLDLearnedActiveValue& Row) { return Row.AbilityId==FName(*Ability.Id); }))
            {
                if (const FLDDemoAbilityBranch* Branch=Ability.FindBranch(Learned->Branch);
                    Branch && Branch->Element!=ELDDemoSkillElement::None)
                {
                    EffectiveElement=Branch->Element;
                }
            }
        }
    }
    if (EffectiveElement==ELDDemoSkillElement::None)
    {
        TArray<FLDPassiveEffectRef> MedalElements;
        if (!CardsOn(Body.AttributeProjection,ELDDemoEffectOp::SkillElement,
            {ELDCombatSourceKind::Learned,FName(*Ability.Id)},MedalElements,Error))
        {
            return false;
        }
        for (const FLDPassiveEffectRef& Ref : MedalElements)
        {
            const ELDDemoSkillElement Mapped=MapCardElement(Ref.Effect.Element);
            if (Mapped!=ELDDemoSkillElement::None) { EffectiveElement=Mapped; break; }
        }
    }
    const bool bAreaMode=Terms.Target.Mode==ELDDemoTargetMode::Self || Terms.Target.Mode==ELDDemoTargetMode::DirectionDash;
    const bool bCanDeliverHostile=!bAreaMode || Find(ELDDemoEffectOp::SelfArea)!=nullptr || Find(ELDDemoEffectOp::DashContact)!=nullptr;
    switch (EffectiveElement)
    {
    case ELDDemoSkillElement::Fire:
        // OVERDRIVE.FireBurnStacks more burn layers: on the release's hits, or spread over a zone's pulses.
        if (FLDDemoEffectDefinition* Burn=Find(ELDDemoEffectOp::Burn)) { Burn->Count+=int32(O.FireBurnStacks); }
        else if (const FLDDemoEffectDefinition* Zone=Find(ELDDemoEffectOp::Zone))
        {
            const int64 PulseCount=Zone->Count>0 ? Zone->Count : 1;
            const int64 IntervalMs=Zone->IntervalMs;
            FLDDemoEffectDefinition* ZoneBurn=Find(ELDDemoEffectOp::ZoneBurn);
            if (!ZoneBurn)
            {
                ZoneBurn=&Values.Effects.AddDefaulted_GetRef();
                ZoneBurn->Opcode=uint8(ELDDemoEffectOp::ZoneBurn);
                ZoneBurn->IntervalMs=IntervalMs;
            }
            ZoneBurn->Count+=int32(FMath::Max<int64>(1,O.FireBurnStacks/PulseCount));
        }
        else if (bCanDeliverHostile)
        {
            FLDDemoEffectDefinition& AddedBurn=Values.Effects.AddDefaulted_GetRef();
            AddedBurn.Opcode=uint8(ELDDemoEffectOp::Burn);
            AddedBurn.Count=int32(O.FireBurnStacks);
        }
        break;
    case ELDDemoSkillElement::Water:
        // The slow rises to OVERDRIVE.WaterSlow for OVERDRIVE.WaterSlowSec: the hit's slow, or the zone's pulse slow.
        if (const FLDDemoEffectDefinition* Zone=Find(ELDDemoEffectOp::Zone))
        {
            const int64 IntervalMs=Zone->IntervalMs;
            FLDDemoEffectDefinition* ZoneSlow=Find(ELDDemoEffectOp::ZoneSlow);
            if (!ZoneSlow)
            {
                ZoneSlow=&Values.Effects.AddDefaulted_GetRef();
                ZoneSlow->Opcode=uint8(ELDDemoEffectOp::ZoneSlow);
                ZoneSlow->IntervalMs=IntervalMs;
            }
            ZoneSlow->RatioBp=FMath::Max(ZoneSlow->RatioBp,O.WaterSlowBp);
            ZoneSlow->DurationMs=FMath::Max(ZoneSlow->DurationMs,O.WaterSlowMs);
        }
        else if (bCanDeliverHostile)
        {
            FLDDemoEffectDefinition* Slow=Find(ELDDemoEffectOp::Slow);
            if (!Slow) { Slow=&Values.Effects.AddDefaulted_GetRef(); Slow->Opcode=uint8(ELDDemoEffectOp::Slow); }
            Slow->RatioBp=FMath::Max(Slow->RatioBp,O.WaterSlowBp);
            Slow->DurationMs=FMath::Max(Slow->DurationMs,O.WaterSlowMs);
        }
        break;
    case ELDDemoSkillElement::Shock:
    {
        // One extra discharge on each body hit: OVERDRIVE.ShockReactionRatio x R of the caster, added to its damage.
        FLDDemoEffectDefinition* Damage=Find(ELDDemoEffectOp::Damage);
        bool bZonePulseShare=false;
        int64 PulseCount=1;
        if (!Damage) { Damage=Find(ELDDemoEffectOp::GroundImpact); }
        if (!Damage) { Damage=Find(ELDDemoEffectOp::DashContact); }
        if (!Damage) { Damage=Find(ELDDemoEffectOp::ChannelCone); }
        if (!Damage)
        {
            Damage=Find(ELDDemoEffectOp::ZoneDamage);
            if (Damage)
            {
                bZonePulseShare=true;
                if (const FLDDemoEffectDefinition* Zone=Find(ELDDemoEffectOp::Zone); Zone && Zone->Count>0) { PulseCount=Zone->Count; }
            }
        }
        if (Damage)
        {
            FLDElementTerms Reaction;
            if (!ElementTerms(Reaction,Error)) { return false; }
            const FLDCombatEntityRecord* Record=FindRecord(Body.Victim.EntityId);
            const int64 R=LDCombatElements::ReactionDamageP2(Record ? Record->Build.Level : 1,Body.AttributeProjection.Final.APP2,Reaction);
            int64 Extra=R/10000*O.ShockReactionBp+R%10000*O.ShockReactionBp/10000;
            if (bZonePulseShare && PulseCount>1) { Extra=FMath::Max<int64>(1,Extra/PulseCount); }
            if (Extra<0 || Extra>MAX_int64-Damage->BaseP2) { Error=TEXT("An overdrive discharge leaves the checked numeric domain."); return false; }
            Damage->BaseP2+=Extra;
        }
        break;
    }
    case ELDDemoSkillElement::Rot:
        // The rot lasts OVERDRIVE.RotDurationScale times longer (its own duration, or the approved COMBAT.RotSec).
        if (FLDDemoEffectDefinition* Rot=Find(ELDDemoEffectOp::Rot))
        {
            const int64 Base=Rot->DurationMs>0 ? Rot->DurationMs : P.Elements.RotMs;
            Rot->DurationMs=Base/10000*O.RotDurationBp+Base%10000*O.RotDurationBp/10000;
        }
        else if (bCanDeliverHostile)
        {
            FLDDemoEffectDefinition& AddedRot=Values.Effects.AddDefaulted_GetRef();
            AddedRot.Opcode=uint8(ELDDemoEffectOp::Rot);
            AddedRot.DurationMs=P.Elements.RotMs/10000*O.RotDurationBp+P.Elements.RotMs%10000*O.RotDurationBp/10000;
        }
        break;
    case ELDDemoSkillElement::Chill:
        // The chill roots for OVERDRIVE.ChillRootSec (hard control, diminishing returns; control times never scale).
        if (Find(ELDDemoEffectOp::Chill) || bCanDeliverHostile)
        {
            FLDDemoEffectDefinition* Root=Find(ELDDemoEffectOp::Root);
            if (!Root) { Root=&Values.Effects.AddDefaulted_GetRef(); Root->Opcode=uint8(ELDDemoEffectOp::Root); }
            Root->DurationMs=FMath::Max(Root->DurationMs,O.ChillRootMs);
        }
        break;
    default:
        break;
    }
    Error.Reset();
    return true;
}

void ALDCombatDirector::LogMomentumLedger(const FLDBodyCombatState& Body) const
{
    if (Body.Victim.Kind!=ELDVictimKind::Contender) { return; }
    const LDMomentum::FLedger& L=Body.MomentumLedger;
    UE_LOG(LogLDCombat,Log,TEXT("LD_MOMENTUM entity=%s life=%u basic=%s combat=%s skill=%s reaction=%s dodge=%s other=%s idle=%s spent=%s overdrive=%d end=%s"),
        *LDLog::Id(Body.Victim.EntityId),Body.Victim.Life,*LDLog::P2(L.BasicP2),*LDLog::P2(L.CombatP2),*LDLog::P2(L.SkillP2),
        *LDLog::P2(L.ReactionP2),*LDLog::P2(L.DodgeP2),*LDLog::P2(L.OtherP2),*LDLog::P2(L.IdleP2),*LDLog::P2(L.SpentP2),
        L.Overdrives,*LDLog::P2(Body.Resources.MomentumP2));
}
