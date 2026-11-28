#include "Combat/LDCombatDirector.h"

#include "Combat/LDCombatElements.h"
#include "Combat/LDCombatSourceRules.h"
#include "Combat/LDMomentum.h"
#include "Core/LDLog.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDSkillGrowth.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDLifeAuthority.h"

namespace LDCombatMomentumDetail
{
    // A + Sign x B for content rates (small denominators), floored at zero.
    FLDDemoRational AddRational(const FLDDemoRational& A, const FLDDemoRational& B, int64 Sign)
    {
        if (A.Denominator<=0 || B.Denominator<=0) { return A; }
        FLDDemoRational Out;
        Out.Numerator=FMath::Max<int64>(A.Numerator*B.Denominator+Sign*B.Numerator*A.Denominator,0);
        Out.Denominator=A.Denominator*B.Denominator;
        if (const int64 Divisor=FMath::GreatestCommonDivisor(FMath::Max<int64>(Out.Numerator,1),Out.Denominator); Out.Numerator>0 && Divisor>1)
        {
            Out.Numerator/=Divisor;
            Out.Denominator/=Divisor;
        }
        return Out;
    }
}

// Momentum (劲) and the momentum income hooks of Combat (docs/systems/COMBAT_AND_GROWTH.md "劲与连击").
// The pure rules are LDMomentum; this file owns the body records they change.

int64 ALDCombatDirector::GrantMomentum(FLDBodyCombatState& Body, int64 AmountP2, ELDMomentumBook Kind)
{
    const FLDDemoContentSnapshot* Content=Catalog();
    // Camp members hold no momentum (POLICY.CampMomentumPolicy=NotApplicable); a dying body takes nothing.
    if (!Content || AmountP2==0 || Body.bPendingDeath || Body.Victim.Kind!=ELDVictimKind::Contender) { return 0; }
    // Auto-empower (rule E1): an empowered action's chain and hits never give their caster momentum.
    if (AmountP2>0 && Kind!=ELDMomentumBook::Spent && EmpowerTerms())
    {
        bool bBlocked=EmpowerHitCaster.IsValid() && EmpowerHitCaster==Body.Victim.EntityId;
        if (const FChainTrack* Track=!bBlocked && CurrentChain.IsValid() ? OpenChains.Find(CurrentChain) : nullptr)
        { bBlocked=Track->bEmpowered && Track->Owner==Body.Victim.EntityId; }
        if (bBlocked)
        {
            Body.EmpowerBlockedP2+=AmountP2;
            return 0;
        }
    }
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
    case ELDMomentumBook::Idle: Ledger.IdleP2+=Applied; break;
    case ELDMomentumBook::Spent: Ledger.SpentP2-=Applied; break;
    default: Ledger.OtherP2+=Applied; break;
    }
    if (ILDLifeAuthority* Mode=Authority()) { TrackEmpowerArmed(Body,Mode->GetAuthorityNowMs()); }
    ++Body.Resources.Revision;
    MarkViewChanged(Body.Victim.EntityId);
    return Applied;
}

bool ALDCombatDirector::SettleMomentum(FLDBodyCombatState& Body, const FLDDemoMomentumParameters& Momentum, int64 FromMs,
    int64 NowMs, int64& InOutP2, FString& Error)
{
    int64 CombatP2=0, IdleP2=0;
    // Auto-empower: the combat gain is EMPOWER.CombatPerSec in place of MOMENTUM.CombatPerSec (the brains bonus stays).
    FLDDemoRational Rate=Body.AttributeProjection.Final.MomentumRegenPerSec;
    if (const FLDDemoEmpowerParameters* EmpowerP=EmpowerTerms())
    {
        Rate=LDCombatMomentumDetail::AddRational(LDCombatMomentumDetail::AddRational(Rate,Momentum.CombatPerSec,-1),EmpowerP->CombatPerSec,1);
    }
    if (!LDMomentum::Settle(Momentum,Rate,
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
    // One MOMENTUM.SkillHit per paid release and body: the release's first native direct hit on it (under the auto-empower
    // experiment at most EMPOWER.SkillHitTargets bodies of one release).
    if (bNativeDirect && Hit.CastLedger && !Hit.CastLedger->bAutoRecast && Hit.Source.Kind==ELDCombatSourceKind::Learned)
    {
        const FLDCombatBodyRef Struck{Victim.Victim,Victim.Identity};
        const FLDDemoEmpowerParameters* EmpowerP=EmpowerTerms();
        if (!Hit.CastLedger->MomentumStruck.Contains(Struck)
            && (!EmpowerP || Hit.CastLedger->MomentumStruck.Num()<EmpowerP->SkillHitTargets))
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
    const ILDLifeAuthority* Mode=Authority();
    const int64 ArmedMs=Body.EmpowerArmedMs+(Body.EmpowerArmedSinceMs>=0 && Mode ? FMath::Max<int64>(Mode->GetAuthorityNowMs()-Body.EmpowerArmedSinceMs,0) : 0);
    UE_LOG(LogLDCombat,Log,TEXT("LD_MOMENTUM entity=%s life=%u basic=%s combat=%s skill=%s reaction=%s other=%s idle=%s spent=%s empower=%d empower_spent=%s blocked=%s armed_ms=%lld end=%s"),
        *LDLog::Id(Body.Victim.EntityId),Body.Victim.Life,*LDLog::P2(L.BasicP2),*LDLog::P2(L.CombatP2),*LDLog::P2(L.SkillP2),
        *LDLog::P2(L.ReactionP2),*LDLog::P2(L.OtherP2),*LDLog::P2(L.IdleP2),*LDLog::P2(L.SpentP2),
        Body.Empowerments,*LDLog::P2(Body.EmpowerSpentP2),*LDLog::P2(Body.EmpowerBlockedP2),ArmedMs,
        *LDLog::P2(Body.Resources.MomentumP2));
}
