#include "Combat/LDCombatDirector.h"
#include "Core/LDLog.h"
#include "Combat/LDTacticalRules.h"
#include "Combat/LDCombatSourceRules.h"
#include "Combat/LDControlRules.h"
#include "Combat/LDCombatMechanics.h"
#include "Character/LDCharacter.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDGameMode.h"
#include "Region/LDMapRegistry.h"
#include "Vision/LDVisionDirector.h"
#include "Engine/World.h"

namespace LDCombatEffectsDetail
{
    // Bisection steps locating where a projectile segment first meets a sight blocker or the world edge.
    constexpr int32 SightBisections=12;
    // Halvings allowed when one segment is longer than a single sight trace may span.
    constexpr int32 SightSplits=8;

    enum class ESegmentSight : uint8 { Clear, Stopped, Fault };

    struct FAreaCandidate
    {
        FLDCombatBodyRef Body;
        FVector FootCm=FVector::ZeroVector;
    };

    struct FProjectileTarget
    {
        FLDCombatBodyRef Body;
        FVector FootCm=FVector::ZeroVector;
        double RadiusCm=0;
    };

    // Terrain only: a blocker or the end of the registered surface stops the segment.
    ESegmentSight TraceSegment(const ALDVisionDirector& Vision, const FLDWorldRevision& WorldRevision,
        const FVector& From, const FVector& To, int32 Splits)
    {
        switch (Vision.TraceSight(From,To,WorldRevision))
        {
        case ELDSightResult::Visible: return ESegmentSight::Clear;
        case ELDSightResult::Blocked:
        case ELDSightResult::InvalidPoint: return ESegmentSight::Stopped;
        case ELDSightResult::OutOfRange:
        {
            if (Splits<=0) { return ESegmentSight::Fault; }
            const FVector Middle=(From+To)*.5;
            const ESegmentSight Head=TraceSegment(Vision,WorldRevision,From,Middle,Splits-1);
            return Head!=ESegmentSight::Clear ? Head : TraceSegment(Vision,WorldRevision,Middle,To,Splits-1);
        }
        default: return ESegmentSight::Fault;
        }
    }

    // Longest clear prefix (0..1) of From->To before static terrain or the registered world edge.
    bool ClearPrefix(const ALDVisionDirector& Vision, const FLDWorldRevision& WorldRevision, const FVector& From,
        const FVector& To, double& OutFraction, FString& Error)
    {
        OutFraction=0;
        const ESegmentSight Whole=TraceSegment(Vision,WorldRevision,From,To,SightSplits);
        if (Whole==ESegmentSight::Clear) { OutFraction=1; return true; }
        if (Whole==ESegmentSight::Fault || Vision.TraceSight(From,From,WorldRevision)==ELDSightResult::InvalidPoint)
        {
            Error=TEXT("A live projectile left the registered sight world or its registration became stale.");
            return false;
        }
        double Clear=0, Stopped=1;
        for (int32 Iteration=0; Iteration<SightBisections; ++Iteration)
        {
            const double Probe=(Clear+Stopped)*.5;
            const ESegmentSight Part=TraceSegment(Vision,WorldRevision,From,FMath::Lerp(From,To,Probe),SightSplits);
            if (Part==ESegmentSight::Fault)
            {
                Error=TEXT("Projectile terrain bisection lost its sight registration.");
                return false;
            }
            if (Part==ESegmentSight::Clear) { Clear=Probe; }
            else { Stopped=Probe; }
        }
        OutFraction=Clear;
        return true;
    }

    // Earliest fraction of From->To at which the moving point comes within Reach of Centre (planar).
    bool PlanarEntry(const FVector& From, const FVector& To, const FVector& Centre, double Reach, double& OutFraction)
    { return LDCombatKeywords::SegmentEntry(From,To,Centre,Reach,OutFraction); }

    // Controls one committed hit applied, for its LD_HIT line.
    constexpr uint8 KnockbackBit=1<<4;
    uint8 ControlBit(ELDControlKind Kind)
    {
        switch (Kind)
        {
        case ELDControlKind::Root: return 1;
        case ELDControlKind::Stun: return 1<<1;
        case ELDControlKind::Disarm: return 1<<2;
        case ELDControlKind::Taunt: return 1<<3;
        case ELDControlKind::Knockback: return KnockbackBit;
        case ELDControlKind::Slow: return 1<<5;
        case ELDControlKind::Silence: return 1<<6;
        default: return 0;
        }
    }
    FString ControlNames(uint8 Mask)
    {
        static const TCHAR* Names[]={TEXT("root"),TEXT("stun"),TEXT("disarm"),TEXT("taunt"),TEXT("knockback"),TEXT("slow"),TEXT("silence")};
        FString Out;
        for (int32 Bit=0; Bit<UE_ARRAY_COUNT(Names); ++Bit)
        {
            if (!(Mask&(1<<Bit))) { continue; }
            if (!Out.IsEmpty()) { Out+=TEXT("+"); }
            Out+=Names[Bit];
        }
        return Out.IsEmpty() ? FString(TEXT("-")) : Out;
    }
    const TCHAR* HitClassName(ELDCombatHitClass Class)
    {
        switch (Class)
        {
        case ELDCombatHitClass::BasicPrimary: return TEXT("basic");
        case ELDCombatHitClass::Ability: return TEXT("ability");
        default: return TEXT("none");
        }
    }
    const TCHAR* VictimKindName(ELDVictimKind Kind) { return Kind==ELDVictimKind::CampMember ? TEXT("camp") : TEXT("contender"); }
}

bool ALDCombatDirector::ApplyHit(const FLDCombatHit& InHit, FString& Error)
{
    // Hits queue more hits; keep this one stable even when the caller passed one of PendingHits' elements.
    FLDCombatHit Hit=InHit;
    ALDGameMode* Mode=Core();
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!Mode || !Content)
    {
        Error=TEXT("A combat hit requires the current core and the approved Demo content.");
        return false;
    }
    ALDCharacter* Actor=nullptr;
    FLDBodyCombatState* Body=FindBody(Hit.Target,&Actor);
    // A retired, replaced or already lethal life cannot be hit again.
    if (!Body || !Actor || Body->bPendingDeath) { return true; }
    const int64 NowMs=Mode->GetAuthorityNowMs();
    // Protection is the target life's state at this hit, not at the time the effect was accepted.
    const bool bProtected=Hit.Target.Victim.Kind==ELDVictimKind::Contender
        && Mode->IsContenderProtected(Hit.Target.Victim.EntityId);
    // The exact releasing life, when it still stands: geometry, heals and mana grants read it; a dead or replaced
    // source still deals its released damage.
    ALDCharacter* SourceActor=nullptr;
    FLDBodyCombatState* Source=FindBody(Hit.SourceBody,&SourceActor);
    if (Source && (Source==Body || Source->bPendingDeath)) { Source=nullptr; SourceActor=nullptr; }
    if ((Hit.bDashContactHit || Hit.bChannelHit) && !Source) { return true; }
    const FLDProjectedAttributes& Defence=Body->AttributeProjection.Final;
    FLDKeywordTerms Keywords;
    if (!KeywordTerms(Keywords,Error)) { return false; }
    // Controls, slows, breaches and HP shares are judged before this hit applies its own.
    const bool bImpaired=IsImpaired(*Body,NowMs);
    const auto BelowShare=[Body](int64 ThresholdBp)
    { return ThresholdBp>0 && LDTactical::BelowHPThreshold(Body->Resources.HpP2,Body->Resources.MaxHpP2,ThresholdBp); };
    // Only a primary basic crits, sampled once at its release.
    const bool bCritical=Hit.bCritical;
    FLDDamageEvaluationInput Evaluation;
    Evaluation.Definition.Emplace(Hit.BaseP2,Hit.ADRatioBp,Hit.APRatioBp,Hit.DamageType,Hit.MaxHPRatioBp);
    Evaluation.HitClass=Hit.HitClass;
    Evaluation.bCritical=bCritical;
    Evaluation.SourceADP2=Hit.SourceADP2;
    Evaluation.SourceAPP2=Hit.SourceAPP2;
    if (Hit.MaxHPRatioBp!=0) { Evaluation.SourceMaxHPP2=Hit.SourceMaxHPP2; }
    const bool bPhysical=Hit.DamageType==ELDDemoDamageType::Physical;
    // The #147 shared breach cuts the matching resistance by its stacks' flat terms.
    Evaluation.TargetResistanceP2=LDCombatKeywords::BreachedResistanceP2(bPhysical ? Defence.ArmorP2 : Defence.MagicResistP2,
        Body->SharedBreach,NowMs,Keywords);
    Evaluation.TargetHPP2=Body->Resources.HpP2;
    const int64 ShieldBeforeP2=Body->Resources.ShieldP2;
    Evaluation.TargetShieldP2=ShieldBeforeP2;
    Evaluation.bTargetProtected=bProtected;
    // #147 D21: every "damage +X%" of this hit adds into one pool applied once before resistance: the source's
    // slowed-or-controlled (or low-HP) amplification.
    int64 PoolBp=0;
    if (Hit.ControlAmpBp>0 && (bImpaired || BelowShare(Hit.ControlAmpBelowBp))) { PoolBp+=Hit.ControlAmpBp; }
    Evaluation.DamageBonusBp=PoolBp;
    Evaluation.DeliveryScaleBp=Hit.DeliveryScaleBp;
    const FLDDemoEffectDefinition* PrimaryGuard=Hit.HitClass==ELDCombatHitClass::BasicPrimary
        ? FindTrigger(Body->AttributeProjection,ELDDemoEffectOp::PrimaryGuard) : nullptr;
    if (PrimaryGuard)
    {
        Evaluation.PrimaryGuardP2=PrimaryGuard->BaseP2;
        Evaluation.PrimaryGuardCapBp=PrimaryGuard->RatioBp;
    }
    const FLDDamageEvaluationResult Evaluated=LDTactical::EvaluateDamage(Evaluation,*Content);
    if (!Evaluated.IsSuccess())
    {
        Error=FString::Printf(TEXT("Hit %s could not be evaluated: %s: %s"),
            *Hit.HitId.ToString(),*Evaluated.Error.Field,*Evaluated.Error.Message);
        return false;
    }
    const FLDDamageEvaluation& Damage=Evaluated.Value.GetValue();

    // Every fallible read of the hit is resolved before its first write.
    const bool bControllable=!bProtected && Damage.RemainingHPP2>0;
    const int64 StunMs=Hit.StunDurationMs;
    TArray<FLDPassiveEffectRef> ManaGrants;
    if (Source && !CollectPassiveEffects(Source->AttributeProjection,ELDDemoEffectOp::ManaOnHit,ManaGrants,Error)) { return false; }
    // Vamp of this hit: on-hit lifesteal (already at its on-hit coefficient) and the source's low-HP basic lifesteal
    // (low_hp_stat_bonus); all on the HP actually lost.
    int64 VampBp=0;
    if (Source && !Hit.bReflect && !Hit.bBurnTick && Damage.HPLostP2>0)
    {
        if (Hit.OnHitScaleBp>0 && Hit.bLifesteal) { VampBp+=Hit.LifestealRatioBp; }
        // #147: a lifesteal that multiplies against a low-HP body, judged on the HP before this hit.
        if (Hit.OnHitScaleBp>0 && Hit.LifestealLowHPBp>0 && BelowShare(Hit.LifestealLowHPBelowBp)) { VampBp+=Hit.LifestealLowHPBp; }
        if (Hit.LowHPBasicLifestealBp>0 && Hit.HitClass==ELDCombatHitClass::BasicPrimary
            && LDTactical::BelowHPThreshold(Source->Resources.HpP2,Source->Resources.MaxHpP2,Hit.SourceLowHPBelowBp))
        { VampBp+=Hit.LowHPBasicLifestealBp; }
        // D22: every source adds up, at most COMBAT.LifestealCap.
        VampBp=LDCombatKeywords::CapLifestealBp(VampBp,Keywords);
    }
    int64 HealP2=0;
    if (VampBp>0)
    {
        if (VampBp<0 || Damage.HPLostP2>MAX_int64/VampBp)
        {
            Error=TEXT("Vamp scaling is outside the checked numeric domain.");
            return false;
        }
        HealP2=Damage.HPLostP2*VampBp/10000;
    }

    const int64 HPBefore=Body->Resources.HpP2;
    const FGuid ShieldBefore=Body->Shield.IsSet() ? Body->Shield->EffectId : FGuid();
    Body->Resources.HpP2=Damage.RemainingHPP2;
    Body->Resources.ShieldP2=Damage.RemainingShieldP2;
    ++Body->Resources.Revision;
    const bool bShieldBroken=LDCombatMechanics::ShieldBroken(ShieldBeforeP2,Damage.RemainingShieldP2,Damage.ShieldAbsorbedP2);
    const int64 ShieldGrantedP2=Body->ShieldGrantedP2;
    // A barrier emptied by this hit ends by the same timed-status rule as an expired one.
    const FLDStatusExpiry Ended=LDTactical::ExpireTimedStatuses(Body->Roots,Body->Shield,Body->Resources.ShieldP2,NowMs);
    if (Ended.bRootsChanged || Ended.bShieldChanged) { ++Body->StateRevision; }
    // A positive mitigated hit is combat even when the barrier absorbed all of it.
    if (Damage.DamageP2>0) { Body->LastCombatAtMs=NowMs; }
    // #198 build safe window: hostile positive damage marks its victim and the contender credited with it (burn ticks
    // and zone pulses included); a self-inflicted hit marks nobody.
    if (Damage.DamageP2>0 && Hit.SourceEntity!=Body->Victim.EntityId)
    {
        MarkBuildUnsafe(Body->Victim.EntityId,NowMs);
        if (Hit.CreditOwner.IsValid() && Hit.CreditOwner!=Body->Victim.EntityId) { MarkBuildUnsafe(Hit.CreditOwner,NowMs); }
    }
    // Controls and slows go through the single control path (diminishing returns, interruptions, spray, assists).
    bool bControlled=false;
    uint8 ControlMask=0;
    const auto Control=[&](ELDControlKind Kind, int64 DurationMs, int64 RatioBp, bool& bOut)
    {
        FLDControlApplication App;
        App.Kind=Kind;
        App.EffectId=Hit.EffectId;
        App.SourceEntity=Hit.SourceEntity;
        App.SourceBody=Hit.SourceBody;
        App.CreditOwner=Hit.CreditOwner;
        App.DurationMs=DurationMs;
        App.RatioBp=RatioBp;
        bOut=false;
        if (!CommitControl(*Body,App,NowMs,bOut,Error)) { return false; }
        bControlled|=bOut;
        if (bOut) { ControlMask|=LDCombatEffectsDetail::ControlBit(Kind); }
        return true;
    };
    if (bControllable)
    {
        bool bApplied=false;
        if (Hit.AttackSlowBp>0 && Hit.AttackSlowMs>0)
        {
            FLDCombatSlow Slow;
            Slow.EffectId=Hit.EffectId;
            Slow.SourceEntity=Hit.SourceEntity;
            Slow.RatioBp=Hit.AttackSlowBp;
            if (!LDMatchLifecycle::AddDurationMs(NowMs,Hit.AttackSlowMs,Slow.ExpiresAtMs,Error)) { return false; }
            if (LDTactical::ApplySlow(Body->AttackSlows,Slow,NowMs))
            {
                bControlled=true;
                ++Body->StateRevision;
                RecordAssistContribution(*Body,Hit.CreditOwner,NowMs);
            }
        }
        if (Hit.RootDurationMs>0 && !Control(ELDControlKind::Root,Hit.RootDurationMs,0,bApplied)) { return false; }
        if ((Hit.DisarmDurationMs>0 && !Control(ELDControlKind::Disarm,Hit.DisarmDurationMs,0,bApplied))
            || (StunMs>0 && !Control(ELDControlKind::Stun,StunMs,0,bApplied))
            || (Hit.SilenceDurationMs>0 && !Control(ELDControlKind::Silence,Hit.SilenceDurationMs,0,bApplied)))
        { return false; }
        // #198: the push stops at the first blocking geometry (ApplyKnockback); a wall adds no further control.
        if (Hit.KnockbackCm>0 && Hit.KnockbackMs>0)
        {
            if (!ApplyKnockback(*Actor,*Body,Hit,NowMs,bApplied,Error)) { return false; }
            bControlled|=bApplied;
            if (bApplied) { ControlMask|=LDCombatEffectsDetail::KnockbackBit; }
        }
        if (Hit.SlowDurationMs>0 && Hit.SlowBp>0 && !Control(ELDControlKind::Slow,Hit.SlowDurationMs,Hit.SlowBp,bApplied))
        { return false; }
    }
    // #147: the shared breach and burn this hit (or its source's passives) adds.
    if (!bProtected && Hit.CastLedger && Hit.CastLedger->bUniqueSkillHits)
    {
        Hit.bSuppressSkillTriggers=Hit.CastLedger->KeywordTargets.Contains(Hit.Target);
        if (!Hit.bSuppressSkillTriggers) { Hit.CastLedger->KeywordTargets.Add(Hit.Target); }
    }
    if (bControllable && !ApplyKeywordStacks(*Body,Hit,Source,NowMs,Error)) { return false; }
    if (PrimaryGuard && Damage.DamageP2>0 && PrimaryGuard->SecondaryRatioBp>0)
    {
        if (!LDMatchLifecycle::AddDurationMs(NowMs,PrimaryGuard->DurationMs,Body->SlowResistanceUntilMs,Error)) { return false; }
        Body->SlowResistanceBp=PrimaryGuard->SecondaryRatioBp;
        ++Body->StateRevision;
    }
    // A hit that harmed, controlled or slowed the body is its visible Hit cue (LDCombatCues).
    if (Damage.DamageP2>0 || bControlled)
    {
        // A zone's periodic slow refresh is not a visible hit.
        if (!Hit.bSilent || Damage.DamageP2>0) { Body->Cues.LastHitAtMs=NowMs; }
        MarkViewChanged(Body->Victim.EntityId);
        // Committed positive damage (absorbed included) is the owner's assist contribution; controls record in CommitControl.
        if (Damage.DamageP2>0) { RecordAssistContribution(*Body,Hit.CreditOwner,NowMs); }
    }
    // Camp retaliation consumes the committed hostile damage or control; misses and zero contact never aggro.
    if ((Damage.DamageP2>0 || bControlled) && Hit.Target.Victim.Kind==ELDVictimKind::CampMember)
    {
        FLDCombatHostileContact& Contact=PendingContacts.AddDefaulted_GetRef();
        Contact.HitId=Hit.HitId;
        Contact.VictimBody=Hit.Target;
        Contact.SourceBody=Hit.SourceBody;
        Contact.CreditOwner=Hit.CreditOwner;
        Contact.OccurredAtSeconds=Mode->GetAuthorityNowSeconds();
    }
    MarkViewChanged(Hit.Target.Victim.EntityId);
    const int64 HPAfterP2=Body->Resources.HpP2;
    // A one-time heal; the record below logs what the source actually regained.
    int64 VampHealedP2=0;
    if (Source && HealP2>0)
    {
        const int64 SourceHPBefore=Source->Resources.HpP2;
        HealBody(*Source,HealP2);
        VampHealedP2=Source->Resources.HpP2-SourceHPBefore;
    }
    // Host playtest record of this committed hit (docs/DEVELOPMENT.md): every term the player could ask about afterwards.
    if (UE_LOG_ACTIVE(LogLDCombat,Log))
    {
        using namespace LDCombatEffectsDetail;
        const FLDCombatEntityRecord* SourceRecord=FindRecord(Hit.SourceEntity);
        const FName SourceMonster=Source ? Source->MonsterId : (SourceRecord ? SourceRecord->MonsterId : NAME_None);
        UE_LOG(LogLDCombat,Log,TEXT("LD_HIT t=%lld src=%s src_kind=%s src_monster=%s credit=%s victim=%s victim_life=%u victim_kind=%s victim_monster=%s via=%s class=%s type=%s dmg=%s absorbed=%s hp_lost=%s hp=%s->%s max_hp=%s shield=%s->%s amp_bp=%lld resist=%s crit=%d protected=%d periodic=%d reflect=%d added=%d controls=%s vamp=%s lethal=%d pool_bp=%lld"),
            NowMs,*LDLog::Id(Hit.SourceEntity),VictimKindName(Hit.SourceBody.Victim.Kind),
            SourceMonster.IsNone() ? TEXT("-") : *SourceMonster.ToString(),*LDLog::Id(Hit.CreditOwner),
            *LDLog::Id(Body->Victim.EntityId),Body->Victim.Life,VictimKindName(Body->Victim.Kind),
            Body->MonsterId.IsNone() ? TEXT("-") : *Body->MonsterId.ToString(),*LDLog::Source(Hit.Source),HitClassName(Hit.HitClass),
            bPhysical ? TEXT("physical") : TEXT("magical"),*LDLog::P2(Damage.DamageP2),
            *LDLog::P2(Damage.ShieldAbsorbedP2),*LDLog::P2(Damage.HPLostP2),*LDLog::P2(HPBefore),
            *LDLog::P2(HPAfterP2),*LDLog::P2(Body->Resources.MaxHpP2),*LDLog::P2(ShieldBeforeP2),
            *LDLog::P2(Body->Resources.ShieldP2),10000+Evaluation.DamageBonusBp,
            *LDLog::P2(Evaluation.TargetResistanceP2.Get(0)),
            bCritical,bProtected,Hit.bPeriodic,Hit.bReflect,Hit.bAddedDamage,*ControlNames(ControlMask),
            *LDLog::P2(VampHealedP2),HPAfterP2==0,Evaluation.DamageBonusBp);
    }
    if (Source)
    {
        // Mana grant (mana_on_hit), at most once per release for an ability hit.
        for (const FLDPassiveEffectRef& Grant : ManaGrants)
        {
            const FLDDemoEffectDefinition& Effect=*Grant.Effect;
            if (Effect.Trigger==ELDDemoEffectTrigger::AbilityHit && !bProtected && !Hit.bReflect
                && Hit.HitClass==ELDCombatHitClass::Ability && Hit.Token!=0
                && Hit.Source.Kind==ELDCombatSourceKind::Learned
                && Source->LastAbilityManaEffect!=Hit.EffectId
                && (!Hit.CastLedger || !Hit.CastLedger->bManaGrantPaid))
            {
                Source->LastAbilityManaEffect=Hit.EffectId;
                if (Hit.CastLedger) { Hit.CastLedger->bManaGrantPaid=true; }
                GrantMana(*Source,Effect.BaseP2);
            }
        }
    }
    if (bShieldBroken && !OnShieldBroken(*Body,Hit,ShieldGrantedP2,NowMs,Error)) { return false; }
    // #147: a broken damage-triggered shield bursts; positive damage taken from another body may grant the next one.
    if (bShieldBroken && !BurstBrokenShield(*Body,ShieldBefore,Hit.SourceBody,NowMs,Error)) { return false; }
    if (Damage.DamageP2>0 && Body->Resources.HpP2>0 && Hit.SourceEntity!=Body->Victim.EntityId && !OnDamaged(*Body,NowMs,Error))
    { return false; }
    // Thorns answer a hit the owner survived; a reflection never answers another reflection.
    if (!Hit.bReflect && Damage.DamageP2>0 && Body->Resources.HpP2>0
        && !QueueThorns(Hit,*Actor,*Body,Damage.DamageP2,bCritical,Error)) { return false; }
    if (Body->Resources.HpP2==0 && !LockDeath(*Actor,*Body,Hit,Error)) { return false; }
    return true;
}

bool ALDCombatDirector::CollectPassiveEffects(const FLDAttributeProjection& /*Projection*/, ELDDemoEffectOp /*Op*/,
    TArray<FLDPassiveEffectRef>& Out, FString& Error) const
{
    // #198 PR1 builds hold no trigger effect; the base components of #198 PR3 enter through the projection here.
    Out.Reset();
    Error.Reset();
    return true;
}
bool ALDCombatDirector::SnapshotOnHit(const FLDBodyCombatState& Source, int64 ScaleBp, FLDCombatHit& InOut, FString& Error) const
{
    InOut.OnHitScaleBp=FMath::Max<int64>(0,ScaleBp);
    InOut.bLifesteal=false;
    InOut.LifestealRatioBp=0;
    InOut.LifestealLowHPBp=InOut.LifestealLowHPBelowBp=0;
    if (InOut.OnHitScaleBp==0) { return true; }
    TArray<FLDPassiveEffectRef> Effects;
    if (!CollectPassiveEffects(Source.AttributeProjection,ELDDemoEffectOp::Lifesteal,Effects,Error)) { return false; }
    for (const FLDPassiveEffectRef& Ref : Effects)
    {
        InOut.bLifesteal=true;
        const int64 Ratio=Ref.Effect->RatioBp*InOut.OnHitScaleBp/10000;
        InOut.LifestealRatioBp+=Ratio;
        // #147: the share a low-HP multiplier adds against a body below its threshold (judged at the hit).
        if (Ref.Effect->MultiplierBp>10000 && Ref.Effect->SecondaryRatioBp>0)
        {
            InOut.LifestealLowHPBp+=Ratio*(Ref.Effect->MultiplierBp-10000)/10000;
            InOut.LifestealLowHPBelowBp=FMath::Max(InOut.LifestealLowHPBelowBp,Ref.Effect->SecondaryRatioBp);
        }
    }
    return true;
}

bool ALDCombatDirector::StampSourceTerms(const FLDBodyCombatState& Source, FLDCombatHit& InOut, FString& Error) const
{
    const FLDAttributeProjection& Projection=Source.AttributeProjection;
    const FLDDemoEffectDefinition* Amp=FindTrigger(Projection,ELDDemoEffectOp::ControlDamageAmp);
    InOut.ControlAmpBp=Amp ? Amp->RatioBp : 0;
    InOut.ControlAmpBelowBp=Amp ? Amp->SecondaryRatioBp : 0;
    const TOptional<FLDLowHPBonusTerm> LowHP=HeldLowHPBonus(Projection);
    InOut.SourceLowHPBelowBp=LowHP.IsSet() ? LowHP->ThresholdBp : 0;
    InOut.LowHPBasicLifestealBp=LowHP.IsSet() && InOut.HitClass==ELDCombatHitClass::BasicPrimary ? LowHP->BasicLifestealBp : 0;
    Error.Reset();
    return true;
}

FLDCombatHit ALDCombatDirector::AddedHit(const FLDCombatHit& Primary)
{
    FLDCombatHit Hit;
    Hit.HitId=FGuid::NewGuid();
    Hit.EffectId=Primary.EffectId;
    Hit.Token=Primary.Token;
    Hit.Target=Primary.Target;
    Hit.SourceBody=Primary.SourceBody;
    Hit.SourceEntity=Primary.SourceEntity;
    Hit.CreditOwner=Primary.CreditOwner;
    Hit.Source=Primary.Source;
    Hit.HitClass=ELDCombatHitClass::Ability;
    Hit.SourceADP2=Primary.SourceADP2;
    Hit.SourceAPP2=Primary.SourceAPP2;
    // The source-side pool terms stay; the primary's own payload does not ride along.
    Hit.ControlAmpBp=Primary.ControlAmpBp;
    Hit.ControlAmpBelowBp=Primary.ControlAmpBelowBp;
    Hit.bAddedDamage=true;
    return Hit;
}

void ALDCombatDirector::GrantMana(FLDBodyCombatState& Body, int64 AmountP2)
{
    auto& Pool=Body.Resources;
    if (AmountP2<=0 || Body.bPendingDeath || Pool.MpP2>=Pool.MaxMpP2) { return; }
    Pool.MpP2=AmountP2>=Pool.MaxMpP2-Pool.MpP2 ? Pool.MaxMpP2 : Pool.MpP2+AmountP2;
    ++Pool.Revision;
    MarkViewChanged(Body.Victim.EntityId);
}

bool ALDCombatDirector::CastRangeCm(const FLDBodyCombatState& Body, const FLDDemoTargetDefinition& Target, double& OutCm,
    FString& Error) const
{
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!Content) { Error=TEXT("Cast range requires the approved Demo content."); return false; }
    const auto Cm=[](const FLDDemoRational& Value) { return Value.Denominator>0 ? double(Value.Numerator)/double(Value.Denominator) : 0.; };
    OutCm=Cm(Target.RangeCm);
    // A share of the extra basic-attack range above the catalog base, up to its cap.
    if (Target.AttackRangeRatioBp>0)
    {
        const double Extra=FMath::Max(0.,Cm(Body.AttributeProjection.Final.AttackRangeCm)-Cm(Content->Get().Parameters.AttackRangeCm.Base));
        OutCm+=FMath::Min(Extra*double(Target.AttackRangeRatioBp)/10000.,Cm(Target.AttackRangeBonusCapCm));
    }
    return FMath::IsFinite(OutCm) && OutCm>0;
}

bool ALDCombatDirector::QueueThorns(const FLDCombatHit& Hit, ALDCharacter& Holder, FLDBodyCombatState& Body, int64 DamageP2,
    bool bCritical, FString& Error)
{
    const FLDDemoContentSnapshot* Content=Catalog();
    const ALDGameMode* Mode=Core();
    if (!Content || !Mode) { Error=TEXT("Thorns require the current core and the approved Demo content."); return false; }
    const FLDAttributeProjection& Projection=Body.AttributeProjection;
    ALDCharacter* Attacker=nullptr;
    const FLDBodyCombatState* AttackerBody=FindBody(Hit.SourceBody,&Attacker);
    // Only the exact attacking life can be answered; a dead, replaced or self source is not.
    if (!AttackerBody || !Attacker || AttackerBody->bPendingDeath || AttackerBody==&Body) { return true; }
    TArray<FLDPassiveEffectRef> Sources;
    if (!CollectPassiveEffects(Projection,ELDDemoEffectOp::Thorns,Sources,Error)) { return false; }
    if (Sources.IsEmpty()) { return true; }
    const int64 NowMs=Mode->GetAuthorityNowMs();
    const FVector OwnerFoot=FootOf(Holder), AttackerFoot=FootOf(*Attacker);
    FLDThornsContact Contact;
    Contact.HitClass=Hit.HitClass;
    Contact.bReflected=Hit.bReflect;
    Contact.bCritical=bCritical;
    Contact.DamageP2=DamageP2;
    Contact.DistanceCm=FVector::Dist2D(OwnerFoot,AttackerFoot);
    const FLDCombatBodyRef OwnerRef{Body.Victim,Body.Identity};
    for (const FLDPassiveEffectRef& Source : Sources)
    {
        if (!LDCombatControl::ThornsTriggers(*Source.Effect,Contact,Body.ThornsMarks,Source.Key,Hit.SourceBody.Victim.EntityId,NowMs)) { continue; }
        int64 BaseP2=0;
        // Armor and max-HP coefficients of the holder now; a learned passive's fixed part already arrives grown for the
        // holder's level (#147, CollectPassiveEffects).
        if (!LDCombatControl::ThornsBaseP2(*Source.Effect,Projection.Final.ArmorP2,Body.Resources.MaxHpP2,bCritical,BaseP2,Error)) { return false; }
        LDCombatControl::MarkThorns(Body.ThornsMarks,Source.Key,Hit.SourceBody.Victim.EntityId,NowMs,Source.Effect->IntervalMs);
        // The same hit, damage, ownership, death and reward path as any other hit; it is flagged so it never crits,
        // heals or triggers the attacker's own thorns.
        FLDCombatHit& Reflect=PendingHits.AddDefaulted_GetRef();
        Reflect.HitId=FGuid::NewGuid();
        Reflect.EffectId=FGuid::NewGuid();
        Reflect.Token=0;
        Reflect.Target=Hit.SourceBody;
        Reflect.SourceBody=OwnerRef;
        Reflect.SourceEntity=Body.Victim.EntityId;
        Reflect.CreditOwner=Body.Victim.Kind==ELDVictimKind::Contender ? Body.Victim.EntityId : FGuid();
        Reflect.Source=Source.Source;
        Reflect.HitClass=ELDCombatHitClass::Ability;
        Reflect.BaseP2=BaseP2;
        Reflect.DamageType=Source.Effect->DamageType;
        Reflect.SourceADP2=Projection.Final.ADP2;
        Reflect.SourceAPP2=Projection.Final.APP2;
        Reflect.bReflect=true;
        // The reflection may slow the attacker's move speed for a while.
        Reflect.SlowBp=Source.Effect->SecondaryRatioBp;
        Reflect.SlowDurationMs=Source.Effect->SecondaryDurationMs;
        if (!StampSourceTerms(Body,Reflect,Error)) { return false; }
    }
    Error.Reset();
    return true;
}

bool ALDCombatDirector::AdvanceProjectiles(FString& Error)
{
    if (Projectiles.IsEmpty()) { return true; }
    ALDGameMode* Mode=Core();
    const UWorld* CurrentWorld=GetWorld();
    const ULDMapRegistry* Registry=CurrentWorld ? CurrentWorld->GetSubsystem<ULDMapRegistry>() : nullptr;
    if (!Mode || !Mode->Vision || !Registry)
    {
        Error=TEXT("Projectiles require the current core, the observation owner and the registered world.");
        return false;
    }
    const FLDWorldRevision WorldRevision=Registry->Revision();
    const int64 NowMs=Mode->GetAuthorityNowMs();
    TArray<LDCombatEffectsDetail::FProjectileTarget> Targets;
    Targets.Reserve(Bodies.Num());
    for (const auto& Pair : Bodies)
    {
        const ALDCharacter* Candidate=Pair.Key.Get();
        if (!Candidate || Pair.Value.bPendingDeath) { continue; }
        LDCombatEffectsDetail::FProjectileTarget& Target=Targets.AddDefaulted_GetRef();
        Target.Body={Pair.Value.Victim,Pair.Value.Identity};
        Target.FootCm=FootOf(*Candidate);
        Target.RadiusCm=Candidate->GetCapsuleComponent()->GetScaledCapsuleRadius();
    }
    Targets.Sort([](const LDCombatEffectsDetail::FProjectileTarget& Left, const LDCombatEffectsDetail::FProjectileTarget& Right)
    { return Left.Body.Victim.EntityId<Right.Body.Victim.EntityId; });
    TArray<int32> Finished;
    for (int32 Index=0; Index<Projectiles.Num(); ++Index)
    {
        FLDCombatProjectile& Projectile=Projectiles[Index];
        const int64 ElapsedMs=NowMs-Projectile.AdvancedThroughMs;
        if (ElapsedMs<=0) { continue; }
        const double Remaining=Projectile.RangeCm-Projectile.TravelledCm;
        const double Travel=Projectile.SpeedCmPerSec*double(ElapsedMs)/1000.;
        const FVector2D Heading=FVector2D(Projectile.Direction.X,Projectile.Direction.Y).GetSafeNormal();
        if (!FMath::IsFinite(Travel) || !FMath::IsFinite(Remaining) || Travel<0 || !FMath::IsFinite(Projectile.RadiusCm)
            || Projectile.RadiusCm<0 || Heading.IsNearlyZero())
        {
            Error=FString::Printf(TEXT("Projectile %s has no finite flight state."),*Projectile.EffectId.ToString());
            return false;
        }
        const bool bReachesRange=Travel>=Remaining;
        const double Step=FMath::Max(0.,FMath::Min(Travel,Remaining));
        const FVector& Origin=Projectile.OriginCm;
        const FVector From(Origin.X+Heading.X*Projectile.TravelledCm,Origin.Y+Heading.Y*Projectile.TravelledCm,Origin.Z);
        const FVector To(From.X+Heading.X*Step,From.Y+Heading.Y*Step,Origin.Z);
        double ClearFraction=1;
        if (Step>0 && !LDCombatEffectsDetail::ClearPrefix(*Mode->Vision,WorldRevision,From,To,ClearFraction,Error)) { return false; }
        const FVector Stop=FMath::Lerp(From,To,ClearFraction);
        int32 Struck=INDEX_NONE;
        double StruckAt=0;
        for (int32 TargetIndex=0; TargetIndex<Targets.Num(); ++TargetIndex)
        {
            const LDCombatEffectsDetail::FProjectileTarget& Candidate=Targets[TargetIndex];
            double Entry=0;
            if (Candidate.Body.Victim.EntityId==Projectile.SourceEntity
                || !LDCombatEffectsDetail::PlanarEntry(From,Stop,Candidate.FootCm,Projectile.RadiusCm+Candidate.RadiusCm,Entry))
            { continue; }
            if (Struck==INDEX_NONE || Entry<StruckAt)
            {
                Struck=TargetIndex;
                StruckAt=Entry;
            }
        }
        if (Struck!=INDEX_NONE)
        {
            // The first body ends the projectile; a protected body still stops it and evaluates to zero. The contact is
            // the payload snapshotted at release.
            FLDCombatHit Contact=Projectile.Payload;
            Contact.HitId=FGuid::NewGuid();
            Contact.Target=Targets[Struck].Body;
            PendingHits.Add(Contact);
            Finished.Add(Index);
            continue;
        }
        if (ClearFraction<1 || bReachesRange)
        {
            Finished.Add(Index);
            continue;
        }
        Projectile.TravelledCm+=Step;
        Projectile.AdvancedThroughMs=NowMs;
    }
    for (int32 Position=Finished.Num()-1; Position>=0; --Position) { Projectiles.RemoveAt(Finished[Position]); }
    return true;
}

bool ALDCombatDirector::CollectAreaTargets(const FVector& Anchor, double RadiusCm, const FLDBodyCombatState* Excluded,
    const FGuid& SourceEntity, TArray<FLDCombatBodyRef>& Out, FString& Error) const
{
    Out.Reset();
    const ALDGameMode* Mode=Core();
    const UWorld* CurrentWorld=GetWorld();
    const ULDMapRegistry* Registry=CurrentWorld ? CurrentWorld->GetSubsystem<ULDMapRegistry>() : nullptr;
    if (!Mode || !Mode->Vision || !Registry || !FMath::IsFinite(RadiusCm) || RadiusCm<0 || Anchor.ContainsNaN())
    {
        Error=TEXT("An area query requires the observation owner, the registered world and a valid radius.");
        return false;
    }
    const double ReachSquared=RadiusCm*RadiusCm;
    TArray<LDCombatEffectsDetail::FAreaCandidate> InReach;
    for (const auto& Pair : Bodies)
    {
        const ALDCharacter* Other=Pair.Key.Get();
        const FLDBodyCombatState& State=Pair.Value;
        if (!Other || &State==Excluded || State.bPendingDeath || State.Victim.EntityId==SourceEntity
            || (State.Victim.Kind==ELDVictimKind::Contender && Mode->IsContenderProtected(State.Victim.EntityId)))
        { continue; }
        const FVector OtherFoot=FootOf(*Other);
        if (FVector::DistSquared2D(Anchor,OtherFoot)>ReachSquared) { continue; }
        LDCombatEffectsDetail::FAreaCandidate& Candidate=InReach.AddDefaulted_GetRef();
        Candidate.Body={State.Victim,State.Identity};
        Candidate.FootCm=OtherFoot;
    }
    InReach.Sort([](const LDCombatEffectsDetail::FAreaCandidate& Left, const LDCombatEffectsDetail::FAreaCandidate& Right)
    { return Left.Body.Victim.EntityId<Right.Body.Victim.EntityId; });
    for (const LDCombatEffectsDetail::FAreaCandidate& Candidate : InReach)
    {
        const ELDSightResult Sight=Mode->Vision->TraceSight(Anchor,Candidate.FootCm,Registry->Revision());
        if (Sight==ELDSightResult::Visible) { Out.Add(Candidate.Body); }
        else if (Sight!=ELDSightResult::Blocked && Sight!=ELDSightResult::OutOfRange)
        {
            Error=TEXT("Area sight failed on a registered body.");
            return false;
        }
    }
    return true;
}

bool ALDCombatDirector::RecordAssistContribution(FLDBodyCombatState& Victim, const FGuid& CreditOwner, int64 NowMs)
{
    if (Victim.Victim.Kind!=ELDVictimKind::Contender || Victim.bPendingDeath) { return false; }
    const FLDCombatEntityRecord* Contributor=CreditOwner.IsValid() ? FindRecord(CreditOwner) : nullptr;
    // Only a contender of this match contributes; camp members and unowned effects carry no credit owner.
    if (!Contributor || Contributor->Kind!=ELDVictimKind::Contender) { return false; }
    return LDAssist::RecordContribution(Victim.AssistContributions,CreditOwner,Victim.Victim.EntityId,NowMs);
}

bool ALDCombatDirector::GrantTimedBonus(const FLDCombatBodyRef& Ref, FName Source, const TArray<FLDDemoStatBonus>& Stats,
    int64 DurationMs, const FLDGameplayWritePermit& Permit, bool& bOutApplied, FString& Error)
{
    bOutApplied=false;
    ALDGameMode* Mode=Core();
    if (!Mode || Mode->ValidateWritePermit(Permit,ELDCommitPhase::DeathsAndRewards)!=ELDCommitReject::None || Source.IsNone()
        || Stats.IsEmpty() || DurationMs<=0)
    {
        Error=TEXT("A granted timed bonus needs the current DeathsAndRewards permit, a source, stats and a duration.");
        return false;
    }
    ALDCharacter* Actor=nullptr;
    FLDBodyCombatState* Body=FindBody(Ref,&Actor);
    if (!Body || Body->bPendingDeath || Body->Resources.HpP2<=0 || Body->Victim.Kind!=ELDVictimKind::Contender)
    {
        Error.Reset();
        return true;
    }
    const int64 Now=Permit.GetBoundary().AtMs;
    FLDCombatTimedBonus Bonus;
    Bonus.EffectId=FGuid::NewGuid();
    Bonus.SourceDefinition=Source;
    Bonus.Stats=Stats;
    if (!LDMatchLifecycle::AddDurationMs(Now,DurationMs,Bonus.ExpiresAtMs,Error)) { return false; }
    LDTactical::ApplyTimedBonus(Body->TimedBonuses,Bonus);
    if (!RefreshTemporary(*Body,Actor,Now,Error)) { return false; }
    ++Body->StateRevision;
    MarkViewChanged(Body->Victim.EntityId);
    bOutApplied=true;
    Error.Reset();
    return true;
}

bool ALDCombatDirector::RefreshTemporary(FLDBodyCombatState& Body, ALDCharacter* Actor, int64 NowMs, FString& Error)
{
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!Content)
    {
        Error=TEXT("Temporary attributes require the approved Demo content.");
        return false;
    }
    FLDTemporaryAttributeTerms Terms=LDTactical::TemporaryTerms(Body.TimedBonuses,Body.Slows,
        HeldLowHPBonus(Body.BuildProjection),Body.Resources.HpP2,Body.Resources.MaxHpP2,NowMs);
    Terms.AttackSlowBp=LDTactical::StrongestSlowBp(Body.AttackSlows,NowMs);
    if (Body.SlowResistanceUntilMs>NowMs) { Terms.MoveSlowBp=Terms.MoveSlowBp*(10000-Body.SlowResistanceBp)/10000; }
    if (Terms==Body.AppliedTemporary) { return true; }
    const FLDAttributeProjectionResult Applied=LDAttributeProjection::ApplyTemporary(Body.BuildProjection,Terms,*Content);
    if (!Applied.IsSuccess())
    {
        Error=FString::Printf(TEXT("Temporary attributes of %s were refused: %s: %s"),
            *Body.Victim.EntityId.ToString(),*Applied.Error.Field,*Applied.Error.Message);
        return false;
    }
    const FLDDemoRational OldAttackSpeed=Body.AttributeProjection.Final.AttackSpeed;
    Body.AttributeProjection=Applied.Value.GetValue();
    Body.AppliedTemporary=Terms;
    ++Body.AttributeRevision;
    const FLDDemoRational& NewAttackSpeed=Body.AttributeProjection.Final.AttackSpeed;
    // Contenders time each basic from the current attack speed; camp members keep their monster cadence.
    if (Body.Victim.Kind==ELDVictimKind::Contender
        && (OldAttackSpeed.Numerator!=NewAttackSpeed.Numerator || OldAttackSpeed.Denominator!=NewAttackSpeed.Denominator))
    {
        FLDBasicTimingInput TimingInput;
        TimingInput.BaseAttackSpeed=Content->Get().Parameters.AttackSpeed.Base;
        TimingInput.CurrentAttackSpeed=NewAttackSpeed;
        TimingInput.BaseWindupMs=Content->Get().Parameters.BasicAttackWindupMs;
        const FLDBasicTimingResult Timing=LDTactical::EvaluateBasicTiming(TimingInput,*Content);
        if (!Timing.IsSuccess())
        {
            Error=TEXT("Temporary attack speed has no basic timing: ")+Timing.Error.Field+TEXT(": ")+Timing.Error.Message;
            return false;
        }
        Body.BasicTiming=Timing.Value.GetValue();
    }
    const double Speed=Body.AttributeProjection.Final.MoveSpeedCmPerSec.Denominator>0
        ? double(Body.AttributeProjection.Final.MoveSpeedCmPerSec.Numerator)/double(Body.AttributeProjection.Final.MoveSpeedCmPerSec.Denominator) : 0.;
    if (Actor && Actor->GetCharacterMovement() && FMath::IsFinite(Speed) && Speed>0 && Speed<=MAX_flt)
    { Actor->GetCharacterMovement()->MaxWalkSpeed=float(Speed); }
    MarkViewChanged(Body.Victim.EntityId);
    return true;
}

bool ALDCombatDirector::InstallBodyProjection(FLDBodyCombatState& Body, ALDCharacter* Actor,
    const FLDAttributeProjection& Projection, int64 NowMs, FString& Error)
{
    Body.BuildProjection=Projection;
    Body.AttributeProjection=Projection;
    Body.AppliedTemporary={};
    return RefreshTemporary(Body,Actor,NowMs,Error);
}

bool ALDCombatDirector::RefreshAllTemporary(int64 NowMs, FString& Error)
{
    for (auto& Pair : Bodies)
    {
        FLDBodyCombatState& Body=Pair.Value;
        if (!Pair.Key.IsValid()) { continue; }
        if (LDTactical::ExpireTemporaryStatuses(Body.Slows,Body.TimedBonuses,NowMs))
        {
            ++Body.StateRevision;
            MarkViewChanged(Body.Victim.EntityId);
        }
        if (Body.bPendingDeath) { continue; }
        if (!RefreshTemporary(Body,Pair.Key.Get(),NowMs,Error)) { return false; }
    }
    return true;
}

void ALDCombatDirector::ExpireStatuses(int64 NowMs)
{
    for (auto& Pair : Bodies)
    {
        FLDBodyCombatState& Body=Pair.Value;
        const FLDStatusExpiry Expired=LDTactical::ExpireTimedStatuses(Body.Roots,Body.Shield,Body.Resources.ShieldP2,NowMs);
        const FLDStatusExpiry Control=LDControl::ExpireControlStatuses(Body.Taunt,NowMs);
        const bool bControls=LDCombatControl::ExpireDisarmsAndStuns(Body.Disarms,Body.Stuns,NowMs,&Body.Silences)
            | LDCombatKeywords::ExpireBreach(Body.SharedBreach,NowMs) | LDCombatKeywords::ExpireBurn(Body.Burn,NowMs);
        const bool bEmpower=Body.Empower.IsSet() && Body.Empower->ExpiresAtMs<=NowMs;
        if (bEmpower) { Body.Empower.Reset(); }
        const bool bMechanics=ExpireMechanics(Body,NowMs);
        if (!Expired.bRootsChanged && !Expired.bShieldChanged && !Control.bTauntChanged
            && !bControls && !bEmpower && !bMechanics) { continue; }
        ++Body.StateRevision;
        if (Expired.bShieldChanged) { ++Body.Resources.Revision; }
        MarkViewChanged(Body.Victim.EntityId);
    }
}

bool ALDCombatDirector::Regenerate(int64 NowMs, FString& Error)
{
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!Content)
    {
        Error=TEXT("Regeneration requires the approved Demo catalog of this match.");
        return false;
    }
    const FLDDemoContentParameters& Parameters=Content->Get().Parameters;
    LDTactical::FOutOfCombatRecovery Recovery;
    Recovery.DelayMs=Parameters.OutOfCombatRegenDelayMs;
    Recovery.MaxHPBpPerSec=Parameters.OutOfCombatMaxHPRegenBp;
    Recovery.MaxMPBpPerSec=Parameters.OutOfCombatMaxMPRegenBp;
    // D21 (#79): the dorm's fast recovery stacks on top for a contender standing in a dorm.
    const ALDGameMode* Mode=Core();
    if (!Mode || !Mode->GetDormZones().IsValid())
    {
        Error=TEXT("Regeneration requires the resolved dorms of this match.");
        return false;
    }
    LDTactical::FOutOfCombatRecovery Dorm;
    Dorm.DelayMs=Parameters.DormRegenDelayMs;
    Dorm.MaxHPBpPerSec=Parameters.DormMaxHPRegenBp;
    Dorm.MaxMPBpPerSec=Parameters.DormMaxMPRegenBp;
    return RegenerateBodies(Recovery,NowMs,Error,&Dorm,&Mode->GetDormZones());
}

void ALDCombatDirector::ReadLivingContenderFeet(TArray<LDAirdrop::FCandidate>& Out) const
{
    Out.Reset();
    for (const auto& Pair : Bodies)
    {
        const FLDBodyCombatState& Body=Pair.Value;
        const ALDCharacter* Actor=Pair.Key.Get();
        if (!Actor || Body.bPendingDeath || Body.Resources.HpP2<=0 || Body.Victim.Kind!=ELDVictimKind::Contender) { continue; }
        // D26: respawn protection is read here so the pure pickup rule can refuse it.
        const ALDGameMode* M=Core();
        Out.Add({Body.Victim.EntityId,FootOf(*Actor),M && M->IsContenderProtected(Body.Victim.EntityId)});
    }
}

bool ALDCombatDirector::RegenerateBodies(const LDTactical::FOutOfCombatRecovery& Recovery, int64 NowMs, FString& Error,
    const LDTactical::FOutOfCombatRecovery* DormRecovery, const FLDDormZones* Dorms)
{
    for (auto& Pair : Bodies)
    {
        FLDBodyCombatState& Body=Pair.Value;
        if (!Pair.Key.IsValid() || Body.bPendingDeath) { continue; }
        const int64 FromMs=Body.RegenThroughMs;
        if (NowMs<FromMs)
        {
            Error=FString::Printf(TEXT("Regeneration of %s regressed from %lld to %lld ms."),
                *Body.Victim.EntityId.ToString(),FromMs,NowMs);
            return false;
        }
        if (NowMs==FromMs) { continue; }
        auto& Pool=Body.Resources;
        LDTactical::FRegenPools Pools;
        Pools.HpP2=Pool.HpP2;
        Pools.MaxHpP2=Pool.MaxHpP2;
        Pools.MpP2=Pool.MpP2;
        Pools.MaxMpP2=Pool.MaxMpP2;
        Pools.HpMicroP2=Body.HpRegenMicroP2;
        Pools.MpMicroP2=Body.MpRegenMicroP2;
        // D21: every contender recovers out of combat; camp members heal only by their reset.
        const bool bContender=Body.Victim.Kind==ELDVictimKind::Contender;
        // The foot at this settle decides the dorm term for the elapsed interval (at most one authority step late).
        const ALDCharacter* Actor=Pair.Key.Get();
        const bool bInDorm=bContender && DormRecovery && Dorms && Actor && Dorms->Find(FootOf(*Actor));
        if (Body.bInDorm!=bInDorm)
        {
            Body.bInDorm=bInDorm;
            MarkViewChanged(Body.Victim.EntityId);
        }
        if (!LDTactical::SettleRegen(Body.AttributeProjection,bContender ? &Recovery : nullptr,Body.LastCombatAtMs,FromMs,NowMs,
            Pools,Error,bInDorm ? DormRecovery : nullptr))
        {
            Error=FString::Printf(TEXT("Regeneration of %s failed: %s"),*Body.Victim.EntityId.ToString(),*Error);
            return false;
        }
        const bool bChanged=Pool.HpP2!=Pools.HpP2 || Pool.MpP2!=Pools.MpP2;
        Pool.HpP2=Pools.HpP2;
        Pool.MpP2=Pools.MpP2;
        Body.HpRegenMicroP2=Pools.HpMicroP2;
        Body.MpRegenMicroP2=Pools.MpMicroP2;
        Body.RegenThroughMs=NowMs;
        if (bChanged)
        {
            ++Pool.Revision;
            MarkViewChanged(Body.Victim.EntityId);
        }
    }
    return true;
}

bool ALDCombatDirector::LockDeath(ALDCharacter& /*Actor*/, FLDBodyCombatState& Body, const FLDCombatHit& Hit, FString& Error)
{
    if (Body.bPendingDeath) { return true; }
    const ALDGameMode* Mode=Core();
    const FLDDemoContentSnapshot* Content=Catalog();
    const FLDCombatEntityRecord* Record=FindRecord(Body.Victim.EntityId);
    if (!Mode || !Content || !Record)
    {
        Error=FString::Printf(TEXT("The death of %s has no core, content or Combat record to lock."),*Body.Victim.EntityId.ToString());
        return false;
    }
    // The first zero-HP commit locks the life: no later hit, heal or regeneration of this batch can change it.
    AbandonAction(Body,ELDCombatReject::Dead);
    Body.bPendingDeath=true;
    ++Body.StateRevision;
    MarkViewChanged(Body.Victim.EntityId);
    FLDCommittedDeath Death;
    Death.Key.LedgerMatch=Mode->GetMatchContext().LedgerMatch;
    Death.Key.Victim=Body.Victim;
    Death.VictimBody={Body.Victim,Body.Identity};
    if (Body.Victim.Kind==ELDVictimKind::Contender) { Death.VictimContender=Body.Victim.EntityId; }
    Death.FatalEffectId=Hit.EffectId;
    Death.CreditOwner=Hit.CreditOwner;
    Death.ReleasedSource=Hit.Source;
    Death.SourceBodyAtRelease=Hit.SourceBody;
    Death.Cause=ELDDeathCause::Damage;
    FLDRewardBasis& Basis=Death.RewardBasis;
    if (Body.Victim.Kind==ELDVictimKind::Contender)
    {
        Basis.RewardDefinitionId=FName(*Content->Get().Character.Id);
        Basis.VictimLevelAtDeath=Record->Build.Level;
        Basis.VictimBuildRevisionAtDeath=Record->Installed.BuildRevision;
    }
    else
    {
        Basis.RewardDefinitionId=Body.MonsterId;
        Basis.CampStageAtDeath=Body.CampStage;
    }
    // Credit eligibility is frozen at the fatal moment; a later leave or level-up never rewrites it.
    if (!Hit.CreditOwner.IsValid()) { Basis.NoRewardReason=ELDNoRewardReason::NoOwner; }
    else if (Hit.CreditOwner==Death.VictimContender) { Basis.NoRewardReason=ELDNoRewardReason::SelfInflicted; }
    else if (!Mode->IsMemberPresent(Hit.CreditOwner)) { Basis.NoRewardReason=ELDNoRewardReason::OwnerLeft; }
    else
    {
        Basis.NoRewardReason=ELDNoRewardReason::None;
        Basis.bCreditOwnerEligibleAtDeath=true;
    }
    // Assists exist only for a contender kill with an eligible killer; a departed contributor no longer earns one.
    if (Body.Victim.Kind==ELDVictimKind::Contender && Basis.bCreditOwnerEligibleAtDeath)
    {
        LDAssist::SelectAssisters(Body.AssistContributions,Hit.CreditOwner,Body.Victim.EntityId,Mode->GetAuthorityNowMs(),
            Content->Get().Parameters.AssistWindowMs,[Mode](const FGuid& Contender) { return Mode->IsMemberPresent(Contender); },
            Death.Assisters);
    }
    // Effects run on an integral authority boundary, so the fatal instant is that exact millisecond.
    Death.OccurredAtSeconds=Mode->GetAuthorityNowSeconds();
    Death.OccurredAtMs=Mode->GetAuthorityNowMs();
    PendingDeaths.Add(MoveTemp(Death));
    Error.Reset();
    return true;
}
