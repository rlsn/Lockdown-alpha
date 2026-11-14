#include "Combat/LDCombatCues.h"
#include "Combat/LDCombatDirector.h"
#include "Combat/LDCombatMechanics.h"

LDCombatCues::FCue LDCombatCues::Select(const FLDCombatCueFacts& Facts, int64 NowMs)
{
    FCue Cast, Attack, Hit;
    const auto Offer=[NowMs](FCue& Slot, ELDObservedActionCue Cue, int64 StartedAtMs, int64 ImpactAtMs, int64 EndAtMs,
        ELDObservedHitLook HitLook=ELDObservedHitLook::None)
    {
        if (StartedAtMs<=0 || NowMs<StartedAtMs || NowMs>=EndAtMs) { return; }
        if (Slot.Cue==ELDObservedActionCue::None || StartedAtMs>Slot.AtMs)
        { Slot={Cue,StartedAtMs,ImpactAtMs,HitLook}; }
    };
    const auto Kind=[&Cast,&Attack](bool bBasic) -> FCue& { return bBasic?Attack:Cast; };
    if (Facts.bActing)
    {
        Offer(Kind(Facts.bActingBasic),Facts.bActingBasic?ELDObservedActionCue::Attack:ELDObservedActionCue::Cast,
            Facts.ActingStartedAtMs,Facts.ActingReleaseAtMs,MAX_int64);
    }
    if (Facts.ReleasedAtMs>0 && Facts.ReleasedAtMs<=MAX_int64-ReleaseHoldMs)
    {
        int64 EndAtMs=Facts.ReleasedAtMs+ReleaseHoldMs;
        if (!Facts.bReleasedBasic) { EndAtMs=FMath::Max(EndAtMs,Facts.DashUntilMs); }
        Offer(Kind(Facts.bReleasedBasic),Facts.bReleasedBasic?ELDObservedActionCue::Attack:ELDObservedActionCue::Cast,
            Facts.ReleasedStartedAtMs,Facts.ReleasedAtMs,EndAtMs);
    }
    if (Facts.LastHitAtMs>0 && Facts.LastHitAtMs<=MAX_int64-HitHoldMs)
    {
        Offer(Hit,ELDObservedActionCue::Hit,Facts.LastHitAtMs,0,Facts.LastHitAtMs+HitHoldMs,
            static_cast<ELDObservedHitLook>(Facts.LastHitLook));
    }
    return Cast.Cue!=ELDObservedActionCue::None ? Cast : Hit.Cue!=ELDObservedActionCue::None ? Hit : Attack;
}

ELDObservedHitLook LDCombatCues::ClassifyHit(const FLDCombatHit& Hit)
{
    if (Hit.BurnStacks>0 || Hit.ComponentBurnStacks>0) { return ELDObservedHitLook::Fire; }
    if (Hit.bApplyElectric || Hit.ComponentElectricSource.Kind!=ELDCombatSourceKind::None) { return ELDObservedHitLook::Electric; }
    if (Hit.bApplyWet || Hit.ComponentWetSource.Kind!=ELDCombatSourceKind::None) { return ELDObservedHitLook::Water; }
    return Hit.DamageType==ELDDemoDamageType::Magical ? ELDObservedHitLook::Magic : ELDObservedHitLook::None;
}

uint8 LDCombatCues::ClassifyImpact(bool bCritical, int64 ShieldBeforeP2, const FLDDamageEvaluation& Damage)
{
    uint8 Flags=0;
    if (bCritical && Damage.DamageP2>0) { Flags|=LDObservation::Critical; }
    if (Damage.ShieldAbsorbedP2>0) { Flags|=LDObservation::ShieldAbsorbed; }
    if (LDCombatMechanics::ShieldBroken(ShieldBeforeP2,Damage.RemainingShieldP2,Damage.ShieldAbsorbedP2))
    { Flags|=LDObservation::ShieldBroken; }
    return Flags;
}
