#include "Combat/LDCombatDirector.h"

#include "Combat/LDCombatElements.h"
#include "Combat/LDCombatSourceRules.h"
#include "Combat/LDMomentum.h"
#include "Core/LDLog.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDSkillGrowth.h"
#include "Framework/LDGameMode.h"

// Momentum (劲) and the momentum income hooks of Combat (docs/systems/COMBAT_AND_GROWTH.md
// "劲、连击、爆劲与完美闪避"). The pure rules are LDMomentum; this file owns the body records they change.

bool ALDCombatDirector::ResolvePerfectDodges(const FGuid& EffectId, const FGuid& SourceEntity, TFunctionRef<bool(const FVector&)> InArea,
    int64 NowMs, FString& Error, TFunction<int64(const FVector&)> ResolveAt)
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
        const int64 ResolveMs=ResolveAt ? FMath::Min(NowMs,ResolveAt(Body.DashStartFootCm)) : NowMs;
        if (!LDMomentum::IsPerfectDodge(M.PerfectDodgeWindowMs,Body.DashStartedAtMs.GetValue(),ResolveMs,bStartInside,bNowInside)) { continue; }
        const FGuid Dodger=Body.Victim.EntityId;
        if (PerfectDodgeMarks.ContainsByPredicate([&EffectId,&Dodger](const TTuple<FGuid,FGuid,int64>& Mark)
            { return Mark.Get<0>()==EffectId && Mark.Get<1>()==Dodger; })) { continue; }
        PerfectDodgeMarks.Emplace(EffectId,Dodger,NowMs);
        GrantMomentum(Body,M.PerfectDodgeP2,ELDMomentumBook::Dodge);
        if (M.DashDodgeCooldownRefundBp>0)
        {
            if (FLDCombatEntityRecord* Record=FindRecord(Dodger))
            {
                const FName InnateId=LDCombatSourceRules::InnateDashId();
                for (FLDCombatTrack& Track : Record->Tracks)
                {
                    if (Track.Kind==ELDCombatSourceKind::Innate && Track.DefinitionId==InnateId && Track.ReadyAtMs>NowMs)
                    {
                        const int64 Rem=Track.ReadyAtMs-NowMs;
                        Track.ReadyAtMs=LDCombatSourceRules::ReduceReadyAt(Track.ReadyAtMs,NowMs,
                            Rem/10000*M.DashDodgeCooldownRefundBp+Rem%10000*M.DashDodgeCooldownRefundBp/10000);
                    }
                }
            }
        }
        ++Body.PerfectDodges;
        Body.LastPerfectDodgeAtMs=NowMs;
        MarkViewChanged(Dodger);
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

void ALDCombatDirector::LogMomentumLedger(const FLDBodyCombatState& Body) const
{
    if (Body.Victim.Kind!=ELDVictimKind::Contender) { return; }
    const LDMomentum::FLedger& L=Body.MomentumLedger;
    UE_LOG(LogLDCombat,Log,TEXT("LD_MOMENTUM entity=%s life=%u basic=%s combat=%s skill=%s reaction=%s dodge=%s other=%s idle=%s spent=%s end=%s"),
        *LDLog::Id(Body.Victim.EntityId),Body.Victim.Life,*LDLog::P2(L.BasicP2),*LDLog::P2(L.CombatP2),*LDLog::P2(L.SkillP2),
        *LDLog::P2(L.ReactionP2),*LDLog::P2(L.DodgeP2),*LDLog::P2(L.OtherP2),*LDLog::P2(L.IdleP2),*LDLog::P2(L.SpentP2),
        *LDLog::P2(Body.Resources.MomentumP2));
}
