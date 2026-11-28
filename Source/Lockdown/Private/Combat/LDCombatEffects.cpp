#include "Combat/LDCombatDirector.h"
#include "Combat/LDCombatCues.h"
#include "Core/LDLog.h"
#include "Combat/LDTacticalRules.h"
#include "Combat/LDCombatSourceRules.h"
#include "Combat/LDControlRules.h"
#include "Combat/LDCombatMechanics.h"
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDSkillGrowth.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDLifeAuthority.h"
#include "World/LDAirdrop.h"
#include "World/LDDormZones.h"
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
    // "key:bp,..." of a hit's damage factors for LD_HIT and LD_DAMAGE_FACTOR_CAPPED ("-" when none).
    FString FactorNames(const TArray<FLDDamageFactor>& Factors)
    {
        FString Out;
        for (const FLDDamageFactor& Factor : Factors)
        {
            Out+=FString::Printf(TEXT("%s%s:%lld"),Out.IsEmpty() ? TEXT("") : TEXT(","),*Factor.Key.ToString(),Factor.Bp);
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
    // Auto-empower: an empowered action's hit gives its caster no momentum (E1); a reaction a player's own hit sets off
    // keeps its full reaction momentum, a tick's, pulse's or settle's only EMPOWER.DerivedReaction.
    TGuardValue<FGuid> EmpowerScope(EmpowerHitCaster,IsEmpoweredHit(InHit) ? InHit.SourceEntity : EmpowerHitCaster);
    TGuardValue<bool> OwnHitScope(bOwnHitScope,!InHit.bPeriodic && !InHit.bBurnTick && !InHit.bStatusSettle
        && InHit.ReactionKind==ELDCombatReaction::None && !InHit.bReflect);
    ILDLifeAuthority* Mode=Authority();
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
    if (!Hit.HitId.IsValid())
    {
        Error=TEXT("A combat hit requires a unique hit identity.");
        return false;
    }
    // #198: a frozen world body or another contender's rest group is out of reach (no hit, no event).
    if (IsIsolated(Hit.SourceEntity,*Body)) { return true; }
    const int64 NowMs=Mode->GetAuthorityNowMs();
    // Protection is the target life's state at this hit, not at the time the effect was accepted.
    bool bProtected=Hit.Target.Victim.Kind==ELDVictimKind::Contender
        && Mode->IsContenderProtected(Hit.Target.Victim.EntityId);
#if WITH_DEV_AUTOMATION_TESTS
    bProtected |= Hit.Target.Victim.Kind==ELDVictimKind::Contender && Mode->IsDevInvulnerable(Hit.Target.Victim.EntityId);
#endif
    // The exact releasing life, when it still stands: geometry, heals and momentum grants read it; a dead or replaced
    // source still deals its released damage.
    ALDCharacter* SourceActor=nullptr;
    FLDBodyCombatState* Source=FindBody(Hit.SourceBody,&SourceActor);
    if (Source && (Source==Body || Source->bPendingDeath)) { Source=nullptr; SourceActor=nullptr; }
    if ((Hit.bDashContactHit || Hit.bChannelHit) && !Source) { return true; }
    const FLDProjectedAttributes& Defence=Body->AttributeProjection.Final;
    FLDKeywordTerms Keywords;
    if (!KeywordTerms(Keywords,Error)) { return false; }
    // A primary basic crits, sampled once at its release; Y09 (and card_reactive_plating on shield-spending skills) lets a
    // native skill hit sample its own (once per HitId).
    if (!Hit.bCritical && Source && IsNativeDirectHit(Hit) && Hit.SourceEntity!=Body->Victim.EntityId)
    {
        TArray<FLDPassiveEffectRef> Crits, Spends, Platings;
        if (!CollectPassiveEffects(Source->AttributeProjection,ELDDemoEffectOp::SkillCrit,Crits,Error)
            || !CardsOn(Source->AttributeProjection,ELDDemoEffectOp::ShieldSpend,Hit.Source,Spends,Error)
            || (!Spends.IsEmpty() && !CollectPassiveEffects(Source->AttributeProjection,ELDDemoEffectOp::ReactivePlating,Platings,Error)))
        { return false; }
        int64 ChanceBp=!Crits.IsEmpty() ? Source->AttributeProjection.Final.CritChanceBp : 0;
        for (const FLDPassiveEffectRef& Ref : Crits) { ChanceBp+=Ref.Effect.RatioBp; }
        if (!Spends.IsEmpty())
        {
            for (const FLDPassiveEffectRef& Ref : Platings) { ChanceBp+=Ref.Effect.SecondaryRatioBp; }
        }
        Hit.bCritical=(!Crits.IsEmpty() || (!Spends.IsEmpty() && !Platings.IsEmpty())) && ChanceBp>0 && CritRandom.RandRange(0,9999)<ChanceBp;
    }
    // active_crotch_grab backstab lunge: rear arc (150 deg) or hitting a rooted/stunned/knockbacked target triggers guaranteed crit + stun.
    bool bBackstabTriggered=false;
    if (Hit.BackstabTerms.IsSet() && Hit.SourceEntity!=Body->Victim.EntityId)
    {
        const FLDDemoEffectDefinition& Backstab=Hit.BackstabTerms.GetValue();
        bool bRearHit=false;
        if (SourceActor && Actor)
        {
            const FVector ToAttacker=(FootOf(*SourceActor)-FootOf(*Actor)).GetSafeNormal2D();
            const FVector TargetForward=Actor->GetActorForwardVector().GetSafeNormal2D();
            const double RearHalfAngleRad=FMath::DegreesToRadians(
                Backstab.AngleDegrees.Denominator>0 ? double(Backstab.AngleDegrees.Numerator)/double(Backstab.AngleDegrees.Denominator)*.5 : 75.);
            bRearHit=!ToAttacker.IsNearlyZero() && !TargetForward.IsNearlyZero()
                && FVector::DotProduct(-TargetForward,ToAttacker)>=FMath::Cos(RearHalfAngleRad);
        }
        const bool bPinned=LDTactical::HasActiveRoot(Body->Roots,NowMs) || LDTactical::HasActiveRoot(Body->Stuns,NowMs)
            || LDTactical::HasActiveRoot(Body->Knockbacks,NowMs);
        if (bRearHit || bPinned)
        {
            bBackstabTriggered=true;
            Hit.bCritical=true;
            Hit.StunDurationMs=FMath::Max(Hit.StunDurationMs,Backstab.DurationMs);
            if (Source && Backstab.CostRatioBp>0 && Hit.Source.Kind==ELDCombatSourceKind::Learned)
            {
                if (FLDCombatEntityRecord* SourceRecord=FindRecord(Source->Victim.EntityId))
                {
                    for (FLDCombatTrack& Track : SourceRecord->Tracks)
                    {
                        if (Track.Kind==ELDCombatSourceKind::Learned && Track.DefinitionId==Hit.Source.DefinitionId && Track.ReadyAtMs>NowMs)
                        {
                            const int64 Rem=Track.ReadyAtMs-NowMs;
                            Track.ReadyAtMs=LDCombatSourceRules::ReduceReadyAt(Track.ReadyAtMs,NowMs,
                                Rem/10000*Backstab.CostRatioBp+Rem%10000*Backstab.CostRatioBp/10000);
                        }
                    }
                    MarkViewChanged(Source->Victim.EntityId);
                }
            }
        }
        if (Backstab.ExtraDurationMs>0) { Hit.DisarmDurationMs=FMath::Max(Hit.DisarmDurationMs,Backstab.ExtraDurationMs); }
        if (Backstab.SecondaryRatioBp>0 && Backstab.RatioBp>0 && Body->Resources.MaxHpP2>0
            && Body->Resources.HpP2*10000<Body->Resources.MaxHpP2*Backstab.SecondaryRatioBp)
        {
            const int64 MissingP2=FMath::Max<int64>(0,Body->Resources.MaxHpP2-Body->Resources.HpP2);
            Hit.BaseP2+=MissingP2/10000*Backstab.RatioBp+MissingP2%10000*Backstab.RatioBp/10000;
        }
    }
    const bool bCritical=Hit.bCritical;
    // Adaptive damage: a hit with no explicit adaptive_ratio that scales with both attack damage and ability power deals the type of its larger share.
    if (Hit.AdaptiveRatioBp==0 && Hit.ADRatioBp>0 && Hit.APRatioBp>0)
    {
        const int64 ADShare=Hit.SourceADP2/10000*Hit.ADRatioBp, APShare=Hit.SourceAPP2/10000*Hit.APRatioBp;
        Hit.DamageType=ADShare>APShare ? ELDDemoDamageType::Physical : ADShare<APShare ? ELDDemoDamageType::Magical : Hit.DamageType;
    }
    FLDDamageEvaluationInput Evaluation;
    const ELDDemoDamageType EffectiveType=LDTactical::EffectiveDamageType(Hit.DamageType,Hit.AdaptiveRatioBp,Hit.SourceBonusADP2,Hit.SourceAPP2);
    Evaluation.Definition.Emplace(Hit.BaseP2,Hit.ADRatioBp,Hit.APRatioBp,EffectiveType,Hit.MaxHPRatioBp,Hit.AdaptiveRatioBp);
    Evaluation.HitClass=Hit.HitClass;
    Evaluation.bCritical=bCritical;
    Evaluation.SourceADP2=Hit.SourceADP2;
    Evaluation.SourceAPP2=Hit.SourceAPP2;
    if (Hit.AdaptiveRatioBp!=0) { Evaluation.SourceBonusADP2=Hit.SourceBonusADP2; }
    if (Hit.MaxHPRatioBp!=0) { Evaluation.SourceMaxHPP2=Hit.SourceMaxHPP2; }
    const bool bPhysical=EffectiveType==ELDDemoDamageType::Physical;
    // The #147 shared breach cuts the matching resistance by its stacks' flat terms.
    Evaluation.TargetResistanceP2=LDCombatKeywords::BreachedResistanceP2(bPhysical ? Defence.ArmorP2 : Defence.MagicResistP2,
        Body->SharedBreach,NowMs,Keywords);
    Evaluation.TargetHPP2=Body->Resources.HpP2;
    const int64 ShieldBeforeP2=Body->Resources.ShieldP2;
    Evaluation.TargetShieldP2=ShieldBeforeP2;
    Evaluation.bTargetProtected=bProtected;
    // Spec §3.7: every damage factor of this hit (one multiplier per source; copies of a card add into theirs) multiplies once
    // before resistance, up to the technical cap.
    const bool bNativeDirect=IsNativeDirectHit(Hit) && Hit.SourceEntity!=Body->Victim.EntityId;
    TArray<FLDDamageFactor> Factors;
    TArray<FLDCombatSetupMark> SetupTaken;
    if (!HitFactors(Source,*Body,Hit,NowMs,Factors,SetupTaken,Error)) { return false; }
    bool bFactorCapped=false;
    if (!LDTactical::DamageFactorProduct(Factors,Content->Get().Parameters.DamageFactorTechCapBp,Evaluation.DamageFactorBp,
        bFactorCapped,Error))
    {
        Error=FString::Printf(TEXT("Hit %s: %s"),*Hit.HitId.ToString(),*Error);
        return false;
    }
    if (bFactorCapped)
    {
        UE_LOG(LogLDCombat,Warning,TEXT("LD_DAMAGE_FACTOR_CAPPED t=%lld src=%s via=%s victim=%s cap_bp=%lld factors=%s"),NowMs,
            *LDLog::Id(Hit.SourceEntity),*LDLog::Source(Hit.Source),*LDLog::Id(Body->Victim.EntityId),
            Content->Get().Parameters.DamageFactorTechCapBp,*LDCombatEffectsDetail::FactorNames(Factors));
    }
    Evaluation.DeliveryScaleBp=Hit.DeliveryScaleBp;
    Evaluation.DeliveryScaleBp=Hit.DeliveryScaleBp;
    // A branch's self damage cut (S15 A1, S08 A2) on hits from other bodies.
    if (Hit.SourceEntity!=Body->Victim.EntityId && !Hit.bStaggerBleed)
    {
        if (const int64 Guard=BranchGuardBp(*Body,NowMs); Guard>0)
        { Evaluation.DeliveryScaleBp=Evaluation.DeliveryScaleBp/10000*(10000-Guard)+Evaluation.DeliveryScaleBp%10000*(10000-Guard)/10000; }
    }
    // #345 Batch 2.1: innate D dash (and as_dash roll branches) native 0.12s hit-graze Perfect Dodge.
    bool bNativeDashDodged=false;
    if (Hit.SourceEntity!=Body->Victim.EntityId && Body->Victim.Kind==ELDVictimKind::Contender && Body->DashStartedAtMs.IsSet()
        && !Hit.bReflect && !Hit.bBurnTick && !Hit.bPeriodic && !Hit.bStatusSettle && !Hit.bStaggerBleed && !Hit.bRider)
    {
        const FLDDemoMomentumParameters& M=Content->Get().Parameters.Momentum;
        const int64 DashStart=Body->DashStartedAtMs.GetValue();
        if (NowMs>=DashStart && NowMs-DashStart<=M.DashDodgeWindowMs && Body->LastDashDodgeStartMs!=DashStart)
        {
            const FGuid Dodger=Body->Victim.EntityId;
            if (!PerfectDodgeMarks.ContainsByPredicate([&Hit,&Dodger](const TTuple<FGuid,FGuid,int64>& Mark)
                { return Mark.Get<0>()==Hit.EffectId && Mark.Get<1>()==Dodger; }))
            {
                bNativeDashDodged=true;
                Body->LastDashDodgeStartMs=DashStart;
                PerfectDodgeMarks.Emplace(Hit.EffectId,Dodger,NowMs);
                const int64 KeepBp=FMath::Clamp<int64>(10000-M.DashDodgeCutBp,0,10000);
                Evaluation.DeliveryScaleBp=Evaluation.DeliveryScaleBp/10000*KeepBp+Evaluation.DeliveryScaleBp%10000*KeepBp/10000;
                GrantMomentum(*Body,M.PerfectDodgeP2,ELDMomentumBook::Dodge);
                if (M.DashDodgeCooldownRefundBp>0)
                {
                    if (FLDCombatEntityRecord* DodgerRecord=FindRecord(Dodger))
                    {
                        const FName InnateId=LDCombatSourceRules::InnateDashId();
                        for (FLDCombatTrack& Track : DodgerRecord->Tracks)
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
                ++Body->PerfectDodges;
                Body->LastPerfectDodgeAtMs=NowMs;
                MarkViewChanged(Dodger);
                if (!OnPerfectDodgeCards(*Body,NowMs,Error)) { return false; }
                UE_LOG(LogLDCombat,Log,TEXT("LD_PERFECT_DODGE t=%lld entity=%s life=%u effect=%s source=%s momentum=%s"),NowMs,*LDLog::Id(Dodger),
                    Body->Victim.Life,*LDLog::Id(Hit.EffectId),*LDLog::Id(Hit.SourceEntity),*LDLog::P2(Body->Resources.MomentumP2));
            }
        }
    }
    // #345 active_unplug ParryStance: 65% damage cut while active; first direct/projectile/dash hit triggers counter-shock & Combo 3.
    bool bParryCounterNow=false;
    if (Body->ActiveParry.IsSet() && NowMs<Body->ParryUntilMs && Hit.SourceEntity!=Body->Victim.EntityId
        && !Hit.bReflect && !Hit.bBurnTick && !Hit.bPeriodic && !Hit.bStatusSettle && !Hit.bStaggerBleed && !Hit.bRider)
    {
        const FLDDemoEffectDefinition& Parry=Body->ActiveParry.GetValue();
        const int64 KeepBp=FMath::Clamp<int64>(10000-Parry.RatioBp,0,10000);
        Evaluation.DeliveryScaleBp=Evaluation.DeliveryScaleBp/10000*KeepBp+Evaluation.DeliveryScaleBp%10000*KeepBp/10000;
        if (!Body->bParryTriggered)
        {
            Body->bParryTriggered=true;
            bParryCounterNow=true;
        }
    }
    if(Body->MonsterGuardUntilMs>NowMs && SourceActor
        && FVector::DotProduct(Actor->GetActorForwardVector(),(FootOf(*SourceActor)-FootOf(*Actor)).GetSafeNormal2D())>0)
    {
        const FLDDemoMonsterDefinition* Monster=nullptr;
        if(!Content->FindMonsterByName(Body->MonsterId,Monster,Error)){return false;}
        Evaluation.DeliveryScaleBp=Evaluation.DeliveryScaleBp*(10000-Monster->Skill.PassiveBp)/10000;
    }
    Evaluation.PackageScaleBp=Hit.PackageScaleBp;
    // final_cage: a basic between a body inside the fence and one outside wears it (it still lands).
    if (SourceActor && Hit.HitClass==ELDCombatHitClass::BasicPrimary
        && !Hit.bReflect && !Hit.bRider && !Hit.bExtraStrike && FenceCrosses(FootOf(*SourceActor),FootOf(*Actor)))
    { HitFence(Hit); }
    // The thick-skin keystone takes its share off the next direct hostile hit of any kind (a basic or a skill), once per interval.
    const bool bKeystoneGuardReady=Body->AttributeProjection.Keystones.bThickSkin
        && NowMs>=Body->KeystoneThickSkinReadyAtMs && Hit.SourceEntity!=Body->Victim.EntityId
        && (Hit.HitClass==ELDCombatHitClass::BasicPrimary || Hit.HitClass==ELDCombatHitClass::Ability)
        && !Hit.bReflect && !Hit.bBurnTick && !Hit.bPeriodic && !Hit.bStatusSettle && !Hit.bRider;
    if (bKeystoneGuardReady)
    {
        Evaluation.KeystoneGuardP2=Content->Get().Parameters.KeystoneThickSkinGuardP2;
        Evaluation.KeystoneGuardCapBp=Content->Get().Parameters.KeystoneThickSkinGuardCapBp;
    }
    // #198 S16: the target's live stance cuts hostile damage (never its own); a due M12 debt is paid at its original amount
    // and neither takes the cut nor feeds the record (spec §9.1/§12.1).
    const bool bBraced=LDCombatElements::BraceActive(Body->Brace,NowMs) && Hit.SourceEntity!=Body->Victim.EntityId;
    if (bBraced) { Evaluation.BraceCutBp=Body->Brace.RatioBp; }
    const FLDDamageEvaluationResult Evaluated=LDTactical::EvaluateDamage(Evaluation,*Content);
    if (!Evaluated.IsSuccess())
    {
        Error=FString::Printf(TEXT("Hit %s could not be evaluated: %s: %s"),
            *Hit.HitId.ToString(),*Evaluated.Error.Field,*Evaluated.Error.Message);
        return false;
    }
    FLDDamageEvaluation Damage=Evaluated.Value.GetValue();
    // Y03: during the holder's stand every hostile hit deals at most one point.
    if (Hit.SourceEntity!=Body->Victim.EntityId && Body->LastStandUntilMs>NowMs && Damage.DamageP2>100)
    {
        Damage.DamageP2=100;
        Damage.ShieldAbsorbedP2=FMath::Min<int64>(100,ShieldBeforeP2);
        Damage.HPLostP2=FMath::Min<int64>(100-Damage.ShieldAbsorbedP2,Body->Resources.HpP2);
        Damage.OverkillP2=100-Damage.ShieldAbsorbedP2-Damage.HPLostP2;
        Damage.RemainingHPP2=Body->Resources.HpP2-Damage.HPLostP2;
        Damage.RemainingShieldP2=ShieldBeforeP2-Damage.ShieldAbsorbedP2;
    }
    // #345 Batch 2.2: keystone_thick_skin 35% MaxHP stagger bleed gate (excess HP loss settles over 2.0s every 0.5s).
    if (Body->AttributeProjection.Keystones.bThickSkin && Hit.SourceEntity!=Body->Victim.EntityId && !Hit.bStaggerBleed
        && Body->Resources.MaxHpP2>0 && Content->Get().Parameters.KeystoneThickSkinBleedThresholdBp>0)
    {
        const FLDDemoContentParameters& P=Content->Get().Parameters;
        const int64 CapP2=Body->Resources.MaxHpP2/10000*P.KeystoneThickSkinBleedThresholdBp
            +Body->Resources.MaxHpP2%10000*P.KeystoneThickSkinBleedThresholdBp/10000;
        if (CapP2>0 && Damage.HPLostP2>CapP2)
        {
            const int64 DeferredP2=Damage.HPLostP2-CapP2;
            Damage.HPLostP2=CapP2;
            Damage.OverkillP2=0;
            Damage.RemainingHPP2=FMath::Max<int64>(0,Body->Resources.HpP2-CapP2);
            const int32 Ticks=int32(FMath::Max<int64>(1,P.KeystoneThickSkinBleedMs/FMath::Max<int64>(1,P.KeystoneThickSkinBleedTickMs)));
            FLDBodyCombatState::FStaggerBleed& Bleed=Body->StaggerBleeds.AddDefaulted_GetRef();
            Bleed.RemainingP2=DeferredP2;
            Bleed.TicksLeft=Ticks;
            Bleed.IntervalMs=FMath::Max<int64>(1,P.KeystoneThickSkinBleedTickMs);
            if (!LDMatchLifecycle::AddDurationMs(NowMs,Bleed.IntervalMs,Bleed.NextTickMs,Error)) { return false; }
            Bleed.SourceBody=Hit.SourceBody;
            Bleed.SourceEntity=Hit.SourceEntity;
            Bleed.CreditOwner=Hit.CreditOwner;
            Bleed.Source=Hit.Source;
        }
    }
    int64 KeystoneGuardReadyAtMs=Body->KeystoneThickSkinReadyAtMs;
    if (Damage.KeystoneGuardCutP2>0
        && !LDMatchLifecycle::AddDurationMs(NowMs,Content->Get().Parameters.KeystoneThickSkinIntervalMs,
            KeystoneGuardReadyAtMs,Error))
    { return false; }

    // Every fallible read of the hit is resolved before its first write.
    const bool bControllable=!bProtected && Damage.RemainingHPP2>0;
    const int64 StunMs=Hit.StunDurationMs;
    // Spec §8: an effective hit lost HP or had its damage absorbed by a shield.
    const bool bEffective=Damage.HPLostP2>0 || Damage.ShieldAbsorbedP2>0;
    // Cards' elements on this hit: A16-A19 on a primary basic, K19-K24 on an effective native hit of their host skill. Derived,
    // reflected, rider, status and settle hits carry none. Each is a ledger trigger once the element batch applies it.
    struct FElementTrigger { FName Key; FLDCombatSourceRef Source; bool bFire=false; };
    TArray<FElementTrigger> ElementTriggers;
    if (Source && !Hit.bReflect && !Hit.bAddedDamage && !Hit.bBurnTick && !Hit.bFromShieldTrigger && !Hit.bRider)
    {
        if (Hit.HitClass==ELDCombatHitClass::BasicPrimary || Hit.bTriggersBasicOnHit)
        {
            // A body medal rides every basic from the second combo step on (and basic-triggering skill hits).
            TArray<FLDPassiveEffectRef> Elements;
            if ((int32(Hit.ComboStep)>=2 || Hit.bTriggersBasicOnHit)
                && !CardsOn(Source->AttributeProjection,ELDDemoEffectOp::SkillElement,{ELDCombatSourceKind::Basic,NAME_None},Elements,Error))
            { return false; }
            for (const FLDPassiveEffectRef& Ref : Elements)
            {
                if (!ApplyCardElement(Hit,Ref.Effect.Element,Ref.Effect.Count,Ref.Source,Error,Ref.Tier)) { return false; }
                ElementTriggers.Add({Ref.Key,Ref.Source,Ref.Effect.Element==ELDDemoElement::Fire});
            }
        }
        if (bEffective && IsNativeDirectHit(Hit))
        {
            TArray<FLDPassiveEffectRef> Mixed;
            if (!CardsOn(Source->AttributeProjection,ELDDemoEffectOp::SkillElement,Hit.Source,Mixed,Error)) { return false; }
            for (const FLDPassiveEffectRef& Ref : Mixed)
            {
                if (!ApplyCardElement(Hit,Ref.Effect.Element,Ref.Effect.Count,Ref.Source,Error,Ref.Tier)) { return false; }
                ElementTriggers.Add({Ref.Key,Ref.Source,Ref.Effect.Element==ELDDemoElement::Fire});
            }
        }
    }
    // Spec §11.2 fridge affix: the camp's attacks chill.
    if (Source && Source->Victim.Kind==ELDVictimKind::CampMember && Source->CampAffix==ELDCampAffix::Fridge && !Hit.bReflect
        && !Hit.bBurnTick && !Hit.bStatusSettle && !Hit.bAddedDamage)
    {
        if (!ApplyCardElement(Hit,ELDDemoElement::Chill,0,FLDCombatSourceRef(),Error)) { return false; }
    }
    // Vamp of this hit: on-hit lifesteal (Y17, already at its on-hit coefficient) on the HP actually lost, only the hit's
    // own share of it (merged empowered packages never feed it), never the shield's share. Every source adds up; no cap.
    int64 VampBp=0;
    if (Source && !Hit.bReflect && !Hit.bBurnTick && !Hit.bStatusSettle && Damage.HPLostP2>0 && Hit.OnHitScaleBp>0 && Hit.bLifesteal)
    {
        VampBp=Hit.LifestealRatioBp;
    }
    int64 HealP2=0;
    if (VampBp>0)
    {
        if (VampBp<0 || Damage.HPLostP2>MAX_int64/VampBp || Hit.LifestealShareBp<0 || Hit.LifestealShareBp>10000)
        {
            Error=TEXT("Vamp scaling is outside the checked numeric domain.");
            return false;
        }
        HealP2=Damage.HPLostP2*VampBp/10000;
        HealP2=HealP2/10000*Hit.LifestealShareBp+HealP2%10000*Hit.LifestealShareBp/10000;
    }

    const int64 HPBefore=Body->Resources.HpP2;
    Body->Resources.HpP2=Damage.RemainingHPP2;
    Body->Resources.ShieldP2=Damage.RemainingShieldP2;
    // The absorbed amount spends the layers, earliest deadline first; each layer it empties is a break of its own.
    TArray<FGuid> BrokenLayerIds;
    const int32 BrokenLayers=Damage.ShieldAbsorbedP2>0 ? SpendShieldLayers(*Body,Damage.ShieldAbsorbedP2,&BrokenLayerIds) : 0;
    ++Body->Resources.Revision;
    if (Damage.KeystoneGuardCutP2>0)
    {
        Body->KeystoneThickSkinReadyAtMs=KeystoneGuardReadyAtMs;
        GrantMomentum(*Body,1000,ELDMomentumBook::Reaction);
        if (!LDMatchLifecycle::AddDurationMs(NowMs,Content->Get().Parameters.KeystoneThickSkinSteadyMs,Body->KeystoneSteadyUntilMs,Error))
        { return false; }
    }
    if (bBraced && Damage.BraceCutP2>0)
    {
        Body->Brace.RecordedP2=LDCombatElements::BraceRecordP2(Body->Brace.RecordedP2,Damage.BraceCutP2,Body->Brace.CapP2);
        MarkViewChanged(Body->Victim.EntityId);
    }
    // #345 active_unplug ParryStance counter-shock, Combo 3 arm, and branch rewards.
    if (bParryCounterNow && Body->ActiveParry.IsSet() && Body->Resources.HpP2>0)
    {
        const FLDDemoEffectDefinition Parry=Body->ActiveParry.GetValue();
        Body->ForcedCombo3Charges=FMath::Max(Body->ForcedCombo3Charges,FMath::Max(Parry.Count,1));
        if (!Parry.Stats.IsEmpty() && Parry.IntervalMs>0)
        {
            FLDCombatTimedBonus ASBuff;
            ASBuff.EffectId=FGuid::NewGuid();
            ASBuff.SourceDefinition=Body->ParrySource.DefinitionId;
            ASBuff.Stats=Parry.Stats;
            if (!LDMatchLifecycle::AddDurationMs(NowMs,Parry.IntervalMs,ASBuff.ExpiresAtMs,Error)) { return false; }
            LDTactical::ApplyTimedBonus(Body->TimedBonuses,ASBuff);
            if (!RefreshTemporary(*Body,Actor,NowMs,Error)) { return false; }
        }
        if (Parry.LimitP2>0)
        {
            GrantMomentum(*Body,Parry.LimitP2,ELDMomentumBook::Skill);
            if (SourceActor && Actor)
            {
                if (auto* Mov=Cast<ULDCharacterMovementComponent>(Actor->GetCharacterMovement()))
                {
                    const FVector Gap=FootOf(*SourceActor)-FootOf(*Actor);
                    const double Dist=FMath::Clamp(Gap.Size2D()-110.,0.,360.);
                    if (Dist>20.) { FString Ignored; Mov->StartDemoDash(Gap.GetSafeNormal2D(),Dist,0.12,Ignored); }
                }
            }
        }
        if (Parry.MaxHPRatioBp>0)
        {
            const int64 ResistSum=Body->AttributeProjection.Final.ArmorP2+Body->AttributeProjection.Final.MagicResistP2;
            const int64 ShieldAmt=Body->Resources.MaxHpP2/10000*Parry.MaxHPRatioBp+ResistSum/10000*Parry.ResistRatioBp;
            if (ShieldAmt>0 && !GrantShieldTo(*Body,ShieldAmt,2500,FGuid::NewGuid(),Body->Victim.EntityId,NowMs,Error,Body->ParrySource))
            { return false; }
        }
        if (Parry.HealMissingBp>0 && Body->Resources.MaxHpP2>Body->Resources.HpP2)
        {
            const int64 Missing=Body->Resources.MaxHpP2-Body->Resources.HpP2;
            FString HealErr;
            HealBody(*Body,Missing/10000*Parry.HealMissingBp,Body->ParrySource,nullptr,FGuid(),&HealErr);
            if (!HealErr.IsEmpty()) { Error=MoveTemp(HealErr); return false; }
        }
        const double CounterRadius=(Parry.RadiusCm.Denominator>0 ? double(Parry.RadiusCm.Numerator)/double(Parry.RadiusCm.Denominator) : 280.)
            *HulkingAreaScaleOf(*Body);
        TArray<FLDCombatBodyRef> Around;
        if (!CollectAreaTargets(FootOf(*Actor),CounterRadius,Body,Body->Victim.EntityId,Around,Error)) { return false; }
        {
            const int64 BonusADP2=LDTactical::BonusADOf(Content->Get().Parameters,GrowthLevelOf(*Body),Body->AttributeProjection.Final.ADP2);
            const int64 ResistSum=Body->AttributeProjection.Final.ArmorP2+Body->AttributeProjection.Final.MagicResistP2;
            int64 CounterBaseP2=Parry.BaseP2+BonusADP2/10000*Parry.ADRatioBp+ResistSum/10000*Parry.ResistRatioBp;
            if (Parry.MultiplierBp>0)
            {
                const int64 BlockedP2=FMath::Max<int64>(0,Hit.BaseP2+Hit.SourceADP2/10000*Hit.ADRatioBp+Hit.SourceAPP2/10000*Hit.APRatioBp);
                CounterBaseP2+=BlockedP2/10000*Parry.MultiplierBp;
            }
            const bool bMeleeOrDash=Hit.HitClass==ELDCombatHitClass::BasicPrimary || Hit.bDashContactHit;
            const FGuid CounterEffectId=FGuid::NewGuid();
            for (const FLDCombatBodyRef& Enemy : Around)
            {
                FLDCombatHit Counter;
                Counter.HitId=FGuid::NewGuid();
                Counter.EffectId=CounterEffectId;
                Counter.Target=Enemy;
                Counter.SourceBody={Body->Victim,Body->Identity};
                Counter.SourceEntity=Body->Victim.EntityId;
                Counter.CreditOwner=Body->Victim.Kind==ELDVictimKind::Contender ? Body->Victim.EntityId : FGuid();
                Counter.Source=Body->ParrySource;
                Counter.HitClass=ELDCombatHitClass::Ability;
                Counter.bDirectAbilityDamage=true;
                Counter.BaseP2=CounterBaseP2;
                Counter.DamageType=ELDDemoDamageType::Physical;
                Counter.SourceADP2=Body->AttributeProjection.Final.ADP2;
                Counter.SourceAPP2=Body->AttributeProjection.Final.APP2;
                Counter.SourceBonusADP2=BonusADP2;
                Counter.DisarmDurationMs=Parry.SecondaryDurationMs;
                if (bMeleeOrDash && Parry.ExtraDurationMs>0) { Counter.StunDurationMs=Parry.ExtraDurationMs; }
                PendingHits.Add(MoveTemp(Counter));
            }
        }
    }
    // #345 active_crotch_grab 3A2: critical strike unleashes a shockwave around the target.
    if (bCritical && Source && Actor && Hit.BackstabTerms.IsSet() && Hit.BackstabTerms->RadiusCm.Numerator>0 && Hit.BackstabTerms->RadiusCm.Denominator>0)
    {
        const double ShockRadius=double(Hit.BackstabTerms->RadiusCm.Numerator)/double(Hit.BackstabTerms->RadiusCm.Denominator);
        TArray<FLDCombatBodyRef> Around;
        if (!CollectAreaTargets(FootOf(*Actor),ShockRadius,Source,Source->Victim.EntityId,Around,Error)) { return false; }
        {
            const FGuid ShockId=FGuid::NewGuid();
            for (const FLDCombatBodyRef& Other : Around)
            {
                if (Other==Hit.Target) { continue; }
                FLDCombatHit Wave=Hit;
                Wave.HitId=FGuid::NewGuid();
                Wave.EffectId=ShockId;
                Wave.Target=Other;
                Wave.BackstabTerms.Reset();
                Wave.bTriggersBasicOnHit=false;
                Wave.StunDurationMs=0;
                Wave.DeliveryScaleBp=5500;
                Wave.NativeElements|=LDCombatElements::Bit(ELDCombatElement::Shock);
                PendingHits.Add(MoveTemp(Wave));
            }
        }
    }
    const bool bShieldBroken=BrokenLayers>0
        || LDCombatMechanics::ShieldBroken(ShieldBeforeP2,Damage.RemainingShieldP2,Damage.ShieldAbsorbedP2);
    // A barrier emptied by this hit ends by the same timed-status rule as an expired one.
    const FLDStatusExpiry Ended=LDTactical::ExpireTimedStatuses(Body->Roots,Body->Shield,Body->Resources.ShieldP2,NowMs);
    if (!Body->Shield.IsSet()) { Body->ShieldLayers.Reset(); }
    if (Ended.bRootsChanged || Ended.bShieldChanged || BrokenLayers>0) { ++Body->StateRevision; }
    // A positive mitigated hit is combat even when the barrier absorbed all of it.
    if (Damage.DamageP2>0) { MarkEngaged(*Body,NowMs); }
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
        // The exit this control belongs to (an active, Y08's wall stun).
        App.Source=Hit.Source;
        App.DurationMs=DurationMs;
        App.RatioBp=RatioBp;
        bOut=false;
        if (!CommitControl(*Body,App,NowMs,bOut,Error)) { return false; }
        bControlled|=bOut;
        if (bOut) { ControlMask|=LDCombatEffectsDetail::ControlBit(Kind); }
        return true;
    };
    // The thick-skin steady window: slows miss and a knockback travels its share of the distance.
    const bool bSteady=NowMs<Body->KeystoneSteadyUntilMs;
    if (bControllable)
    {
        bool bApplied=false;
        if (Hit.AttackSlowBp>0 && Hit.AttackSlowMs>0 && !bSteady)
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
            FLDCombatHit Push=Hit;
            if (bSteady)
            {
                const int64 KeepBp=Content->Get().Parameters.KeystoneThickSkinKnockbackBp;
                Push.KnockbackCm=Push.KnockbackCm*KeepBp/10000;
                Push.KnockbackMs=FMath::Max<int64>(1,Push.KnockbackMs*KeepBp/10000);
            }
            if (Push.KnockbackCm>0 && !ApplyKnockback(*Actor,*Body,Push,NowMs,bApplied,Error)) { return false; }
            bControlled|=bApplied;
            if (bApplied) { ControlMask|=LDCombatEffectsDetail::KnockbackBit; }
        }
        if (Hit.SlowDurationMs>0 && Hit.SlowBp>0 && !bSteady && !Control(ELDControlKind::Slow,Hit.SlowDurationMs,Hit.SlowBp,bApplied))
        { return false; }
    }
    // #147: the shared breach and burn this hit (its payload, its card fire or its source's basic triggers) adds.
    if (bControllable && !ApplyKeywordStacks(*Body,Hit,Source,NowMs,Error)) { return false; }
    // Spec §8.1: a shocked target counts the direct damage it takes (before this hit's own elements); status damage, ticks
    // and shock strikes never count. Then the hit's element batch applies.
    if (bControllable && Damage.DamageP2>0 && !Hit.bStatusSettle && !Hit.bBurnTick && !Hit.bPeriodic && !Hit.bOverchargeDischarge
        && !Hit.bRider && !CountShock(*Body,FootOf(*Actor),NowMs,Error)) { return false; }
    FLDAppliedCardElement AppliedCard;
    if (bControllable && !ApplyElements(*Body,Hit,Source,NowMs,Error,&AppliedCard)) { return false; }
    // Only the card element that took effect is a trigger: every fire card of the hit shares its one fire application; a
    // card the fixed order passed over, or one the reaction lock dropped, triggered nothing.
    if (bControllable && AppliedCard.bApplied)
    {
        for (const FElementTrigger& Card : ElementTriggers)
        {
            if (AppliedCard.bFire ? Card.bFire : (!Card.bFire && Card.Source==AppliedCard.Source)) { LedgerCardTriggered(*Source,Card.Key); }
        }
    }
    bool bSendOwnHit=false;
    ELDObservedHitLook OwnHitLook=ELDObservedHitLook::None;
    uint8 OwnHitFlags=0;
    // A hit that harmed, controlled, slowed, was perfect-dodged or was parried shows its cue on the struck body.
    if (Damage.DamageP2>0 || bControlled || bNativeDashDodged || bParryCounterNow)
    {
        // A zone's periodic slow refresh is not a visible hit.
        if (!Hit.bSilent || Damage.DamageP2>0 || bNativeDashDodged || bParryCounterNow)
        {
            Body->Cues.LastHitAtMs=NowMs;
            Body->Cues.LastHitId=Hit.HitId;
            Body->Cues.LastHitLook=uint8(LDCombatCues::ClassifyHit(Hit));
            Body->Cues.LastHitFlags=LDCombatCues::ClassifyImpact(bCritical,ShieldBeforeP2,Damage)
                | (bNativeDashDodged ? LDObservation::Dodged : 0)
                | (bParryCounterNow ? LDObservation::Parried : 0);
        }
        MarkViewChanged(Body->Victim.EntityId);
        if (Source && LDCombatCues::ConfirmsDealt(Hit,Damage.DamageP2))
        {
            // A training dummy refills at zero rather than dying, so emptying it is no kill.
            const uint8 Dealt=LDCombatCues::ClassifyDealt(bCritical,ShieldBeforeP2,Damage,
                HPBefore>0 && Body->Resources.HpP2==0 && !Body->bTrainingDummy);
            // One receipt per authority instant: an area release confirms its strongest hit whatever the target order.
            if (Source->Cues.LastDealtAtMs!=NowMs || LDCombatCues::DealtRank(Dealt)>LDCombatCues::DealtRank(Source->Cues.LastDealtFlags))
            {
                Source->Cues.LastDealtLook=uint8(LDCombatCues::ClassifyHit(Hit));
                Source->Cues.LastDealtFlags=Dealt;
            }
            Source->Cues.LastDealtAtMs=NowMs;
            MarkViewChanged(Source->Victim.EntityId);
            bSendOwnHit=true;
            OwnHitLook=static_cast<ELDObservedHitLook>(Body->Cues.LastHitLook);
            OwnHitFlags=Body->Cues.LastHitFlags;
        }
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
    // Spec §10.3: the open arena counts contender-on-contender damage dealt inside it.
    if (Damage.DamageP2>0 && Hit.CreditOwner.IsValid() && Body->Victim.Kind==ELDVictimKind::Contender)
    {
        if (ALDGameMode* Arena=Core()) { Arena->NoteContenderDamage(Hit.CreditOwner,Body->Victim.EntityId,Damage.DamageP2); }
    }
    LedgerHitCommitted(Hit,*Body,Damage,EffectiveType);
    RecapHitCommitted(Hit,*Body,Damage,NowMs);
    // K04: a mark that amplified a positive hit is consumed (one trigger each).
    if (Source && Damage.DamageP2>0)
    {
        for (const FLDCombatSetupMark& Taken : SetupTaken)
        {
            Source->SetupMarks.RemoveAll([&Taken](const FLDCombatSetupMark& Mark)
            { return Mark.Target==Taken.Target && Mark.Serial==Taken.Serial; });
            LedgerCardTriggered(*Source,Taken.Card);
        }
    }
    // What this hit left, not what the body holds by now: an effect the hit set off (HP that grows on being hit) may have
    // raised it since, and the record below states the hit's own change (hp_lost is exactly before minus after).
    const int64 HPAfterP2=Damage.RemainingHPP2;
    // A one-time heal; the record below logs what the source actually regained.
    int64 VampHealedP2=0;
    if (Source && HealP2>0)
    {
        // Y17 is the lifesteal source.
        TOptional<FLDPassiveEffectRef> Vamp;
        if (!FindTrigger(Source->AttributeProjection,ELDDemoEffectOp::Lifesteal,Vamp,Error)) { return false; }
        FString HealError;
        HealBody(*Source,HealP2,Vamp.IsSet() ? Vamp->Source : FLDCombatSourceRef(),&VampHealedP2,FGuid(),&HealError);
        if (!HealError.IsEmpty()) { Error=MoveTemp(HealError); return false; }
        if (Vamp.IsSet() && VampHealedP2>0) { LedgerCardTriggered(*Source,Vamp->Key); }
    }
    // Host playtest record of this committed hit (docs/DEVELOPMENT.md): every term the player could ask about afterwards.
    if (UE_LOG_ACTIVE(LogLDCombat,Log))
    {
        using namespace LDCombatEffectsDetail;
        const FLDCombatEntityRecord* SourceRecord=FindRecord(Hit.SourceEntity);
        const FName SourceMonster=Source ? Source->MonsterId : (SourceRecord ? SourceRecord->MonsterAtLife(Hit.SourceBody.Victim.Life) : NAME_None);
        UE_LOG(LogLDCombat,Log,TEXT("LD_HIT t=%lld src=%s src_kind=%s src_monster=%s credit=%s victim=%s victim_life=%u victim_kind=%s victim_monster=%s via=%s class=%s type=%s dmg=%s absorbed=%s hp_lost=%s hp=%s->%s max_hp=%s shield=%s->%s factor_bp=%lld resist=%s crit=%d protected=%d periodic=%d reflect=%d added=%d controls=%s vamp=%s lethal=%d factors=%s empower=%d"),
            NowMs,*LDLog::Id(Hit.SourceEntity),VictimKindName(Hit.SourceBody.Victim.Kind),
            SourceMonster.IsNone() ? TEXT("-") : *SourceMonster.ToString(),*LDLog::Id(Hit.CreditOwner),
            *LDLog::Id(Body->Victim.EntityId),Body->Victim.Life,VictimKindName(Body->Victim.Kind),
            Body->MonsterId.IsNone() ? TEXT("-") : *Body->MonsterId.ToString(),*LDLog::Source(Hit.Source),HitClassName(Hit.HitClass),
            bPhysical ? TEXT("physical") : TEXT("magical"),*LDLog::P2(Damage.DamageP2),
            *LDLog::P2(Damage.ShieldAbsorbedP2),*LDLog::P2(Damage.HPLostP2),*LDLog::P2(HPBefore),
            *LDLog::P2(HPAfterP2),*LDLog::P2(Body->Resources.MaxHpP2),*LDLog::P2(ShieldBeforeP2),
            *LDLog::P2(Body->Resources.ShieldP2),Evaluation.DamageFactorBp,
            *LDLog::P2(Evaluation.TargetResistanceP2.Get(0)),
            bCritical,bProtected,Hit.bPeriodic,Hit.bReflect,Hit.bAddedDamage,*ControlNames(ControlMask),
            *LDLog::P2(VampHealedP2),HPAfterP2==0,*FactorNames(Factors),
            Hit.EmpowerSerial>0 ? Hit.EmpowerSerial : Hit.CastLedger ? Hit.CastLedger->EmpowerSerial : 0);
    }
    if (Source)
    {
        // Momentum income of a committed hostile hit (MOMENTUM.*): a primary basic gains by its committed interval; a
        // release's first native direct hit on each body gains MOMENTUM.SkillHit once.
        if (!bProtected && Hit.SourceEntity!=Body->Victim.EntityId && !OnMomentumHit(*Source,Hit,*Body,bNativeDirect,Error))
        { return false; }
        // Auto-empower A/B switch: a contender gains EMPOWER.DamagedPerPercent per percent of its max HP a hostile hit took.
        const FLDDemoEmpowerParameters* EmpowerP=EmpowerTerms();
        if (EmpowerP && EmpowerP->DamagedPerPercentP2>0 && !bProtected && Hit.SourceEntity!=Body->Victim.EntityId
            && Body->Victim.Kind==ELDVictimKind::Contender && Body->Resources.MaxHpP2>0 && Damage.HPLostP2>0)
        {
            GrantMomentum(*Body,Damage.HPLostP2*100/Body->Resources.MaxHpP2*EmpowerP->DamagedPerPercentP2,ELDMomentumBook::Other);
        }
        // Skill branch riders of the release on each of its native direct hits.
        if (!bProtected && bNativeDirect && Hit.CastLedger && !Hit.CastLedger->Riders.IsEmpty()
            && !ApplyBranchRiders(Hit,*Source,*Body,NowMs,Error)) { return false; }
    }
    if (!bProtected && !OnBranchHitTaken(Hit,*Body,Damage.DamageP2,BrokenLayerIds,NowMs,Error)) { return false; }
    // Spec §6.5: the hit's damage joins its chain and its owner's strongest hit.
    if (!bProtected && Hit.SourceEntity!=Body->Victim.EntityId) { ChainDamage(Hit,*Body,Damage.DamageP2); }
    // Issue #292 P5 / #330 §5.2.8: Y06 bursts once for every layer this hit broke (a stacked shield bursts layer by layer).
    if (!bProtected && Hit.SourceEntity!=Body->Victim.EntityId)
    {
        for (int32 Layer=0;Layer<FMath::Max(BrokenLayers,bShieldBroken ? 1 : 0);++Layer)
        {
            if (!OnShieldBrokenCards(*Body,NowMs,Error)) { return false; }
        }
    }
    if (!bProtected && Source && Hit.bBurnTick && Hit.SourceEntity!=Body->Victim.EntityId
        && !OnBurnDealtCards(*Source,Damage.HPLostP2,NowMs,Error)) { return false; }
    if (!bProtected && bNativeDirect && Damage.DamageP2>0 && Body->Victim.Kind==ELDVictimKind::Contender && Hit.CreditOwner.IsValid()
        && Hit.CreditOwner!=Body->Victim.EntityId)
    {
        FLDSkillContribution* Row=Body->SkillContributions.FindByPredicate([&Hit](const FLDSkillContribution& Each)
        { return Each.Contributor==Hit.CreditOwner && Each.Skill==Hit.Source.DefinitionId; });
        if (!Row) { Row=&Body->SkillContributions.AddDefaulted_GetRef(); Row->Contributor=Hit.CreditOwner; Row->Skill=Hit.Source.DefinitionId; }
        Row->AtMs=NowMs;
    }
    // Cards on this committed hit (both sides; LDCombatCards.cpp).
    if (!bProtected && !OnCardHit(Hit,*Body,Source,Damage,NowMs,Error)) { return false; }
    // Y07: hostile positive damage the holder survived may grant its shield.
    if (Damage.DamageP2>0 && Body->Resources.HpP2>0 && Hit.SourceEntity!=Body->Victim.EntityId && !OnDamaged(*Body,NowMs,Error))
    { return false; }
    if (Body->Resources.HpP2==0 && !LockDeath(*Actor,*Body,Hit,Error)) { return false; }
    // The reaction's setters hear of it only now that its hit on this body fully committed with an effect.
    if (Hit.ReactionKind!=ELDCombatReaction::None && (Damage.DamageP2>0 || bControlled)
        && !NotifyReactionAssists(Hit,Error)) { return false; }
    if (bSendOwnHit)
    {
        ALDGameMode* GameMode=Core();
        if (GameMode && GameMode->Vision)
        { GameMode->Vision->SendOwnHit(Hit.SourceBody,Hit.Target,Hit.HitId,NowMs,OwnHitLook,OwnHitFlags); }
    }
    return true;
}

bool ALDCombatDirector::SnapshotOnHit(const FLDBodyCombatState& Source, int64 ScaleBp, FLDCombatHit& InOut, FString& Error) const
{
    InOut.OnHitScaleBp=FMath::Max<int64>(0,ScaleBp);
    InOut.bLifesteal=false;
    InOut.LifestealRatioBp=0;
    Error.Reset();
    if (InOut.OnHitScaleBp==0) { return true; }
    TArray<FLDPassiveEffectRef> Effects;
    if (!CollectPassiveEffects(Source.AttributeProjection,ELDDemoEffectOp::Lifesteal,Effects,Error)) { return false; }
    for (const FLDPassiveEffectRef& Ref : Effects)
    {
        InOut.bLifesteal=true;
        InOut.LifestealRatioBp+=Ref.Effect.RatioBp*InOut.OnHitScaleBp/10000;
    }
    return true;
}

bool ALDCombatDirector::StampSourceTerms(const FLDBodyCombatState& Source, FLDCombatHit& InOut, FString& Error) const
{
    if (const FLDDemoContentSnapshot* Content=Catalog())
    {
        InOut.SourceBonusADP2=LDTactical::BonusADOf(Content->Get().Parameters,GrowthLevelOf(Source),Source.AttributeProjection.Final.ADP2);
    }
    Error.Reset();
    return true;
}

bool ALDCombatDirector::ApplyPendingHits(int64 NowMs, FString& Error)
{
    // Hits queue more hits (reactions, settles, cards); a queued hit is one chain step deeper than the hit that queued it.
    // Spec §3.8: a step past CHAIN.MaxDepth derives nothing more; past CHAIN.MaxEffectsPerStep derived hits wait for the
    // next authority step (LD_CHAIN_GUARD).
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!Content) { Error=TEXT("Pending hits require the approved Demo content."); return false; }
    const int32 MaxDepth=Content->Get().Parameters.ChainMaxDepth;
    const int32 Budget=FMath::Min(PendingHits.Num()+Content->Get().Parameters.ChainMaxEffectsPerStep,TechHitsPerStep);
    int32 Index=0;
    for (; Index<PendingHits.Num() && Index<Budget; ++Index)
    {
        const FLDCombatHit Hit=PendingHits[Index];
        const int32 Before=PendingHits.Num();
        // Spec §6.5: the hit's chain is current while it resolves; everything it derives inherits it.
        const FGuid Chain=ChainOfHit(Hit);
        {
            TGuardValue<FGuid> ChainScope(CurrentChain,Chain);
            if (!ApplyHit(Hit,Error)) { return false; }
        }
        for (int32 Derived=PendingHits.Num()-1; Derived>=Before; --Derived)
        {
            if (!PendingHits[Derived].ChainId.IsValid()) { PendingHits[Derived].ChainId=Chain; }
            PendingHits[Derived].ChainDepth=uint8(FMath::Min<int32>(FMath::Max<int32>(PendingHits[Derived].ChainDepth,Hit.ChainDepth+1),255));
            if (int32(PendingHits[Derived].ChainDepth)>MaxDepth)
            {
                UE_LOG(LogLDCombat,Warning,TEXT("LD_CHAIN_GUARD reason=Depth t=%lld src=%s via=%s depth=%u"),NowMs,
                    *LDLog::Id(PendingHits[Derived].SourceEntity),*LDLog::Source(PendingHits[Derived].Source),uint32(PendingHits[Derived].ChainDepth));
                PendingHits.RemoveAt(Derived);
            }
        }
    }
    if (Index<PendingHits.Num())
    {
        UE_LOG(LogLDCombat,Warning,TEXT("LD_CHAIN_GUARD reason=Step t=%lld deferred=%d"),NowMs,PendingHits.Num()-Index);
        PendingHits.RemoveAt(0,Index);
        if (PendingHits.Num()>TechPendingHitsCap)
        {
            UE_LOG(LogLDCombat,Error,TEXT("LD_CHAIN_GUARD reason=Overflow t=%lld dropped=%d kept=%d"),NowMs,
                PendingHits.Num()-TechPendingHitsCap,TechPendingHitsCap);
            PendingHits.SetNum(TechPendingHitsCap);
        }
        return true;
    }
    PendingHits.Reset();
    return true;
}

bool ALDCombatDirector::CastRangeCm(const FLDBodyCombatState& Body, const FLDDemoTargetDefinition& Target, double& OutCm,
    FString& Error) const
{
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!Content) { Error=TEXT("Cast range requires the approved Demo content."); return false; }
    if (!LDCombatSourceRules::CastRangeCm(*Content,Body.AttributeProjection,Target,OutCm)) { return false; }
    if ((Target.Mode==ELDDemoTargetMode::Unit || Target.Mode==ELDDemoTargetMode::DirectionProjectile
        || Target.Mode==ELDDemoTargetMode::DirectionLine || Target.Mode==ELDDemoTargetMode::GroundPoint)
        && (Body.bEagleStanceActive || (Authority() && IsEagleStanceActive(Body,Authority()->GetAuthorityNowMs()))))
    {
        TArray<FLDPassiveEffectRef> Eagles;
        if (CollectPassiveEffects(Body.BuildProjection,ELDDemoEffectOp::EagleStance,Eagles,Error))
        {
            for (const FLDPassiveEffectRef& Ref : Eagles)
            {
                if (Ref.Effect.DistanceCm.Denominator>0) { OutCm+=double(Ref.Effect.DistanceCm.Numerator)/double(Ref.Effect.DistanceCm.Denominator); }
            }
        }
    }
    return true;
}

bool LDCombatSourceRules::CastRangeCm(const FLDDemoContentSnapshot& Content, const FLDAttributeProjection& Projection,
    const FLDDemoTargetDefinition& Target, double& OutCm)
{
    const auto Cm=[](const FLDDemoRational& Value) { return Value.Denominator>0 ? double(Value.Numerator)/double(Value.Denominator) : 0.; };
    OutCm=Cm(Target.RangeCm);
    // A share of the extra basic-attack range above the catalog base, up to its cap.
    if (Target.AttackRangeRatioBp>0)
    {
        const double Extra=FMath::Max(0.,Cm(Projection.Final.AttackRangeCm)-Cm(Content.Get().Parameters.AttackRangeCm.Base));
        OutCm+=FMath::Min(Extra*double(Target.AttackRangeRatioBp)/10000.,Cm(Target.AttackRangeBonusCapCm));
    }
    return FMath::IsFinite(OutCm) && OutCm>0;
}

bool ALDCombatDirector::AdvanceProjectiles(FString& Error)
{
    if (Projectiles.IsEmpty()) { return true; }
    ILDLifeAuthority* Mode=Authority();
    const UWorld* CurrentWorld=GetWorld();
    const ULDMapRegistry* Registry=CurrentWorld ? CurrentWorld->GetSubsystem<ULDMapRegistry>() : nullptr;
    if (!Mode || !Mode->GetVisionAuthority() || !Registry)
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
    const int32 InitialCount=Projectiles.Num();
    for (int32 Index=0; Index<InitialCount; ++Index)
    {
        if (!Projectiles.IsValidIndex(Index)) { break; }
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
        if (Step>0 && !LDCombatEffectsDetail::ClearPrefix(*Mode->GetVisionAuthority(),WorldRevision,From,To,ClearFraction,Error)) { return false; }
        // N4: the first enemy bag on the way stops the flight before any body behind it.
        double BagFraction=1;
        const FVector Clear=FMath::Lerp(From,To,ClearFraction);
        const int32 BagStruck=FirstBagOnSegment(Projectile.SourceEntity,From,Clear,Projectile.RadiusCm,BagFraction);
        const FVector Stop=BagStruck!=INDEX_NONE ? FMath::Lerp(From,Clear,BagFraction) : Clear;
        // final_cage: a skill flight that crosses the fence wears it once (a basic wears it where it lands, in ApplyHit).
        if (!Projectile.bCrossedFence && Projectile.Payload.HitClass==ELDCombatHitClass::Ability && FenceCrosses(From,Stop))
        {
            Projectile.bCrossedFence=true;
            HitFence(Projectile.Payload);
        }
        // active_cordon_tape: own projectile crossing an elastic cordon boosts remaining range & damage (and optional 3B1/3B2).
        for (FLDCombatCordon& Cordon : Cordons)
        {
            if (Cordon.ExpiresAtMs<=NowMs || Cordon.Payload.SourceEntity!=Projectile.SourceEntity
                || Cordon.BoostedFlights.Contains(Projectile.FlightId))
            { continue; }
            const FVector2D SegA(From.X,From.Y), SegB(Stop.X,Stop.Y);
            const FVector2D LineC(Cordon.CentreCm.X,Cordon.CentreCm.Y), LineT(Cordon.Tangent.X,Cordon.Tangent.Y);
            const FVector2D LineA=LineC-LineT*Cordon.HalfLengthCm, LineB=LineC+LineT*Cordon.HalfLengthCm;
            const auto Cross2D=[](const FVector2D& U, const FVector2D& V) { return U.X*V.Y-U.Y*V.X; };
            const FVector2D R=SegB-SegA, S=LineB-LineA, QP=LineA-SegA;
            const double Denom=Cross2D(R,S);
            bool bCrossed=false;
            if (FMath::Abs(Denom)>1e-4)
            {
                const double T=Cross2D(QP,S)/Denom, U=Cross2D(QP,R)/Denom;
                bCrossed=(T>=0. && T<=1. && U>=0. && U<=1.);
            }
            if (!bCrossed) { continue; }
            Cordon.BoostedFlights.Add(Projectile.FlightId);
            const int64 AmpBp=Cordon.Terms.RatioBp;
            if (AmpBp>0)
            {
                const double RemRange=FMath::Max(0.,Projectile.RangeCm-Projectile.TravelledCm);
                Projectile.RangeCm+=RemRange*double(AmpBp)/10000.;
                // A damage boost above the full share rides the package multiplier; the delivery share never exceeds 1.
                const int64 Scale=Projectile.Payload.PackageScaleBp;
                Projectile.Payload.PackageScaleBp=FMath::Min<int64>(100000,Scale/10000*(10000+AmpBp)+Scale%10000*(10000+AmpBp)/10000);
            }
            if (Cordon.Terms.MultiplierBp>10000)
            {
                Projectile.SpeedCmPerSec*=double(Cordon.Terms.MultiplierBp)/10000.;
            }
            if (Cordon.Terms.Count>0)
            {
                Projectile.PierceRemaining+=Cordon.Terms.Count;
                if (Projectile.PierceScaleBp<=0) { Projectile.PierceScaleBp=8500; }
            }
            if (Cordon.Terms.AmountMs>0)
            {
                ReduceLearnedCooldown(Projectile.SourceEntity,Cordon.Payload.Source.DefinitionId,Cordon.Terms.AmountMs);
            }
        }
        // M14: a pierced body does not end the flight; the search goes on past it in the same step.
        FVector SegmentFrom=From;
        bool bEnded=false;
        while (true)
        {
            int32 Struck=INDEX_NONE;
            double StruckAt=0;
            for (int32 TargetIndex=0; TargetIndex<Targets.Num(); ++TargetIndex)
            {
                const LDCombatEffectsDetail::FProjectileTarget& Candidate=Targets[TargetIndex];
                double Entry=0;
                const FLDBodyCombatState* CandidateState=FindBody(Candidate.Body);
                const FGuid& CandidateId=Candidate.Body.Victim.EntityId;
                if (CandidateId==Projectile.SourceEntity || Projectile.Struck.Contains(CandidateId)
                    || (Projectile.SourceBody.Victim.Kind==ELDVictimKind::CampMember && Candidate.Body.Victim.Kind!=ELDVictimKind::Contender)
                    || !CandidateState || IsIsolated(Projectile.SourceEntity,*CandidateState)) { continue; }
                // M13: a return starting on the body its outbound flight ended on touches it at once.
                if (Projectile.bReturn && Projectile.TravelledCm<=0 && SegmentFrom==From && CandidateId==Projectile.StartContact)
                { Entry=0; }
                else if (!LDCombatEffectsDetail::PlanarEntry(SegmentFrom,Stop,Candidate.FootCm,Projectile.RadiusCm+Candidate.RadiusCm,Entry))
                { continue; }
                if (Struck==INDEX_NONE || Entry<StruckAt)
                {
                    Struck=TargetIndex;
                    StruckAt=Entry;
                }
            }
            if (Struck==INDEX_NONE) { break; }
            // The first body ends the projectile unless M14 lets it go on; a protected body still stops it (or uses a
            // pierce) and evaluates to zero. The contact is the payload snapshotted at release; a pierced extra body takes
            // M14's share with the same slow and controls.
            const bool bFirstContact=Projectile.Struck.IsEmpty();
            const FVector HitAt=FMath::Lerp(SegmentFrom,Stop,StruckAt);
            FLDCombatHit Contact=Projectile.Payload;
            Contact.HitId=FGuid::NewGuid();
            Contact.Target=Targets[Struck].Body;
            Contact.FlightDistanceCm=Projectile.TravelledCm+FVector::Dist2D(From,HitAt);
            if (!bFirstContact)
            {
                const int64 Scale=Contact.DeliveryScaleBp, Bp=Projectile.PierceScaleBp;
                Contact.DeliveryScaleBp=Scale/10000*Bp+Scale%10000*Bp/10000;
            }
            PendingHits.Add(Contact);
            // active_snark_bolt: first enemy hit drops a collectible slipper for DurationMs slightly toward the caster.
            if (bFirstContact && !Projectile.bReturn && Projectile.Source.Kind==ELDCombatSourceKind::Learned && Catalog())
            {
                const FLDDemoAbilityDefinition* AbilDef=nullptr;
                FString LookupErr;
                if (Catalog()->FindAbilityByName(Projectile.Source.DefinitionId,AbilDef,LookupErr) && AbilDef)
                {
                    FLDBodyCombatState* Thrower=FindBody(Projectile.SourceBody);
                    const FLDDemoAbilityValues* ValSet=&AbilDef->Values;
                    if (Thrower)
                    {
                        if (const FLDCombatEntityRecord* Rec=FindRecord(Thrower->Victim.EntityId))
                        {
                            for (const FLDLearnedActiveValue& L : Rec->Build.Actives)
                            {
                                if (L.AbilityId==Projectile.Source.DefinitionId)
                                {
                                    if (const FLDDemoAbilityBranch* Br=AbilDef->FindBranch(L.Branch)) { ValSet=&Br->Values; }
                                    break;
                                }
                            }
                        }
                    }
                    if (const FLDDemoEffectDefinition* Slipper=ValSet->Effects.FindByPredicate(
                        [](const FLDDemoEffectDefinition& E) { return E.GetOp()==ELDDemoEffectOp::SlipperPickup; }))
                    {
                        FLDCombatSlipperDrop& Drop=SlipperDrops.AddDefaulted_GetRef();
                        Drop.Id=FGuid::NewGuid();
                        Drop.OwnerEntity=Projectile.SourceEntity;
                        Drop.OwnerBody=Projectile.SourceBody;
                        Drop.SkillId=Projectile.Source.DefinitionId;
                        Drop.FootCm=HitAt-FVector(Heading.X,Heading.Y,0)*85.;
                        Drop.RadiusCm=Slipper->RadiusCm.Denominator>0
                            ? double(Slipper->RadiusCm.Numerator)/double(Slipper->RadiusCm.Denominator) : 110.;
                        Drop.Terms=*Slipper;
                        if (!LDMatchLifecycle::AddDurationMs(NowMs,Slipper->DurationMs,Drop.ExpiresAtMs,Error)) { return false; }
                    }
                }
            }
            // A05: the flying basic's strikes and riders on its aimed target land on the body it struck; hits aimed at other
            // bodies (an A07 sweep and its riders) keep their own targets and land once, with the first contact.
            for (const FLDCombatHit& Rider : Projectile.Riders)
            {
                const bool bOnPrimary=Rider.Target==Projectile.Payload.Target;
                if (!bOnPrimary && !bFirstContact) { continue; }
                FLDCombatHit Landed=Rider;
                Landed.HitId=FGuid::NewGuid();
                if (bOnPrimary) { Landed.Target=Targets[Struck].Body; }
                PendingHits.Add(MoveTemp(Landed));
            }
            // A08 on a flying third basic: the free Q goes toward the first body the flight struck, in the basic's chain.
            if (bFirstContact && Projectile.bComboCast)
            {
                if (FLDBodyCombatState* Holder=FindBody(Projectile.SourceBody); Holder && !Holder->bPendingDeath)
                {
                    TGuardValue<FGuid> ChainScope(CurrentChain,EffectChains.FindRef(Projectile.EffectId));
                    if (!QueueComboCast(*Holder,Projectile.ComboBasic,Contact,NowMs,Error)) { return false; }
                }
            }
            Projectile.Struck.Add(Targets[Struck].Body.Victim.EntityId);
            const FVector At=FMath::Lerp(SegmentFrom,Stop,StruckAt);
            if (Projectile.PierceRemaining>0 && !Projectile.bReturn)
            {
                --Projectile.PierceRemaining;
                UE_LOG(LogLDCombat,Log,TEXT("LD_COMBAT_PROJECTILE_PIERCE t=%lld owner=%s exit=%s left=%d"),NowMs,
                    *LDLog::Id(Projectile.SourceEntity),*LDLog::Source(Projectile.Source),Projectile.PierceRemaining);
                SegmentFrom=At;
                continue;
            }
            const FLDCombatProjectile FinishedCopy=Projectile;
            if (FinishedCopy.ReturnScaleBp>0 && !FinishedCopy.bReturn
                && !LaunchReturn(FinishedCopy,At,Targets[Struck].Body.Victim.EntityId,Error)) { return false; }
            if (!EndBranchProjectile(FinishedCopy,At,false,Error)) { return false; }
            bEnded=true;
            break;
        }
        if (bEnded)
        {
            Finished.Add(Index);
            continue;
        }
        if (BagStruck!=INDEX_NONE)
        {
            const FLDCombatProjectile FinishedCopy=Projectiles[Index];
            if (Bags.IsValidIndex(BagStruck)) { DamageBag(Bags[BagStruck],FinishedCopy.Payload,NowMs); }
            if (FinishedCopy.ReturnScaleBp>0 && !FinishedCopy.bReturn && !LaunchReturn(FinishedCopy,Stop,FGuid(),Error)) { return false; }
            if (!EndBranchProjectile(FinishedCopy,Stop,false,Error)) { return false; }
            Finished.Add(Index);
            continue;
        }
        if (ClearFraction<1 || bReachesRange)
        {
            // M13: terrain or the end of the reach still sends the return home.
            const FLDCombatProjectile FinishedCopy=Projectiles[Index];
            if (FinishedCopy.ReturnScaleBp>0 && !FinishedCopy.bReturn && !LaunchReturn(FinishedCopy,Stop,FGuid(),Error)) { return false; }
            if (!EndBranchProjectile(FinishedCopy,Stop,bReachesRange,Error)) { return false; }
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
    const ILDLifeAuthority* Mode=Authority();
    const UWorld* CurrentWorld=GetWorld();
    const ULDMapRegistry* Registry=CurrentWorld ? CurrentWorld->GetSubsystem<ULDMapRegistry>() : nullptr;
    if (!Mode || !Mode->GetVisionAuthority() || !Registry || !FMath::IsFinite(RadiusCm) || RadiusCm<0 || Anchor.ContainsNaN())
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
            || IsIsolated(SourceEntity,State)
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
        const ELDSightResult Sight=Mode->GetVisionAuthority()->TraceSight(Anchor,Candidate.FootCm,Registry->Revision());
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

bool ALDCombatDirector::RefreshTemporary(FLDBodyCombatState& Body, ALDCharacter* Actor, int64 NowMs, FString& Error)
{
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!Content)
    {
        Error=TEXT("Temporary attributes require the approved Demo content.");
        return false;
    }
    FLDTemporaryAttributeTerms Terms=LDTactical::TemporaryTerms(Body.TimedBonuses,Body.Slows,NowMs);
    Terms.AttackSlowBp=LDTactical::StrongestSlowBp(Body.AttackSlows,NowMs);
    if(Body.Victim.Kind==ELDVictimKind::CampMember && !Body.MonsterId.IsNone())
    {
        const FLDDemoMonsterDefinition* Monster=nullptr;
        if(!Content->FindMonsterByName(Body.MonsterId,Monster,Error)){return false;}
        const auto& S=Monster->Skill;
        if(S.LowHPThresholdBp>0 && Body.Resources.HpP2<=Body.Resources.MaxHpP2/10000*S.LowHPThresholdBp
            +Body.Resources.MaxHpP2%10000*S.LowHPThresholdBp/10000)
        {FLDDemoStatBonus Bonus;Bonus.Stat=ELDDemoBonusStat::AttackSpeedBonusRatio;Bonus.Amount=S.LowHPHasteBp;Terms.Bonuses.Add(Bonus);}
    }
    // Y15: every missing percent of HP adds its attack speed; A12: every focus stack adds its share (copies add).
    int64 CardHasteBp=0;
    if (!LDCombatSourceRules::CardHasteBp(Body.BuildProjection,Body.Resources.HpP2,Body.Resources.MaxHpP2,
        NowMs<=Body.FocusUntilMs ? Body.FocusHits : 0,Content,CardHasteBp,Error))
    { return false; }
    if (CardHasteBp>0)
    {
        FLDDemoStatBonus Bonus;
        Bonus.Stat=ELDDemoBonusStat::AttackSpeedBonusRatio;
        Bonus.Amount=CardHasteBp;
        Terms.Bonuses.Add(Bonus);
    }
    // card_eagle_stance: +AttackRangeCmFlat while standing still >= delay (plus linger after moving).
    if (IsEagleStanceActive(Body,NowMs))
    {
        TArray<FLDPassiveEffectRef> Eagles;
        if (!CollectPassiveEffects(Body.BuildProjection,ELDDemoEffectOp::EagleStance,Eagles,Error)) { return false; }
        for (const FLDPassiveEffectRef& Ref : Eagles)
        {
            if (Ref.Effect.DistanceCm.Denominator>0 && Ref.Effect.DistanceCm.Numerator>0)
            {
                FLDDemoStatBonus RangeBonus;
                RangeBonus.Stat=ELDDemoBonusStat::AttackRangeCmFlat;
                RangeBonus.Cm=Ref.Effect.DistanceCm;
                Terms.Bonuses.Add(RangeBonus);
            }
        }
    }
    // card_skate_blade: +AS and +MoveSpeed while standing on any ice ground state.
    if (Body.bOnAnyIce)
    {
        TArray<FLDPassiveEffectRef> Skates;
        if (!CollectPassiveEffects(Body.BuildProjection,ELDDemoEffectOp::SkateBlade,Skates,Error)) { return false; }
        for (const FLDPassiveEffectRef& Ref : Skates)
        {
            Terms.Bonuses.Append(Ref.Effect.Stats);
        }
    }
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
    if ((OldAttackSpeed.Numerator!=NewAttackSpeed.Numerator || OldAttackSpeed.Denominator!=NewAttackSpeed.Denominator))
    {
        FLDBasicTimingInput TimingInput;
        TimingInput.BaseAttackSpeed=Content->Get().Parameters.AttackSpeed.Base;
        TimingInput.CurrentAttackSpeed=NewAttackSpeed;
        TimingInput.BaseWindupMs=Content->Get().Parameters.BasicAttackWindupMs;
        if(Body.Victim.Kind==ELDVictimKind::CampMember)
        {
            const FLDDemoMonsterDefinition* Monster=nullptr;
            if(!Content->FindMonsterByName(Body.MonsterId,Monster,Error)){return false;}
            TimingInput.BaseAttackSpeed=Monster->AttackSpeed;TimingInput.BaseWindupMs=Monster->AttackWindupMs;
        }
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
    // Nothing below registers or removes a body, so the map is walked in place (twice per step for every body).
    for (TPair<TWeakObjectPtr<ALDCharacter>,FLDBodyCombatState>& Pair : Bodies)
    {
        const TWeakObjectPtr<ALDCharacter>& Key=Pair.Key;
        if (!Key.IsValid()) { continue; }
        FLDBodyCombatState& Body=Pair.Value;
        if (LDTactical::ExpireTemporaryStatuses(Body.Slows,Body.TimedBonuses,NowMs))
        {
            ++Body.StateRevision;
            MarkViewChanged(Body.Victim.EntityId);
        }
        if (Body.bPendingDeath) { continue; }
        if (!RefreshTemporary(Body,Key.Get(),NowMs,Error)) { return false; }
    }
    return true;
}

void ALDCombatDirector::ExpireStatuses(int64 NowMs)
{
    for (TPair<TWeakObjectPtr<ALDCharacter>,FLDBodyCombatState>& Pair : Bodies)
    {
        FLDBodyCombatState& Body=Pair.Value;
        // Layers lapse on their own deadlines; the record goes with the last of them.
        const bool bLayers=ExpireShieldLayers(Body,NowMs);
        FLDStatusExpiry Expired=LDTactical::ExpireTimedStatuses(Body.Roots,Body.Shield,Body.Resources.ShieldP2,NowMs);
        Expired.bShieldChanged|=bLayers;
        if (!Body.Shield.IsSet()) { Body.ShieldLayers.Reset(); }
        const FLDStatusExpiry Control=LDControl::ExpireControlStatuses(Body.Taunt,NowMs);
        const bool bControls=LDCombatControl::ExpireDisarmsAndStuns(Body.Disarms,Body.Stuns,NowMs,&Body.Silences)
            | LDCombatKeywords::ExpireBreach(Body.SharedBreach,NowMs) | LDCombatKeywords::ExpireBurn(Body.Burn,NowMs);
        const bool bEmpower=LDCombatControl::ExpireEmpowers(Body.Empowers,NowMs);
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
    // D21 (#79): the dorm's fast recovery stacks on top for a contender standing in a dorm.
    const ILDLifeAuthority* Mode=Authority();
    if (!Mode || !Mode->GetDormZones().IsValid())
    {
        Error=TEXT("Regeneration requires the resolved dorms of this match.");
        return false;
    }
    LDTactical::FOutOfCombatRecovery Dorm;
    Dorm.DelayMs=Parameters.DormRegenDelayMs;
    Dorm.MaxHPBpPerSec=Parameters.DormMaxHPRegenBp;
    return RegenerateBodies(Recovery,NowMs,Error,&Dorm,&Mode->GetDormZones(),&Parameters.Momentum);
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
        const ILDLifeAuthority* M=Authority();
        bool Protected = M && M->IsContenderProtected(Body.Victim.EntityId);
#if WITH_DEV_AUTOMATION_TESTS
        Protected |= M && M->IsDevPeaceful(Body.Victim.EntityId);
#endif
        Out.Add({Body.Victim.EntityId,FootOf(*Actor),Protected});
    }
}

bool ALDCombatDirector::RegenerateBodies(const LDTactical::FOutOfCombatRecovery& Recovery, int64 NowMs, FString& Error,
    const LDTactical::FOutOfCombatRecovery* DormRecovery, const FLDDormZones* Dorms, const FLDDemoMomentumParameters* MomentumTerms)
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
        // #198: a body frozen by a rest keeps its HP; its interval is consumed without recovery.
        if (Body.bSuspended)
        {
            Body.RegenThroughMs=NowMs;
            continue;
        }
        auto& Pool=Body.Resources;
        LDTactical::FRegenPools Pools;
        Pools.HpP2=Pool.HpP2;
        Pools.MaxHpP2=Pool.MaxHpP2;
        Pools.HpMicroP2=Body.HpRegenMicroP2;
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
        // The physique keystone: recovery starts sooner out of combat; heal-to-shield exclusively owns KeystonePhysiqueReadyAtMs.
        const bool bPhysique=bContender && Body.AttributeProjection.Keystones.bPhysique;
        LDTactical::FOutOfCombatRecovery BodyRecovery=Recovery;
        LDTactical::FOutOfCombatRecovery BodyDorm=DormRecovery ? *DormRecovery : LDTactical::FOutOfCombatRecovery();
        if (bPhysique)
        {
            const FLDDemoContentSnapshot* Content=Catalog();
            if (!Content) { Error=TEXT("The physique keystone requires the approved Demo content."); return false; }
            const FLDDemoContentParameters& Parameters=Content->Get().Parameters;
            const int64 CutBp=Parameters.KeystonePhysiqueOutOfCombatCutBp;
            BodyRecovery.DelayMs-=BodyRecovery.DelayMs/10000*CutBp+BodyRecovery.DelayMs%10000*CutBp/10000;
            BodyDorm.DelayMs-=BodyDorm.DelayMs/10000*CutBp+BodyDorm.DelayMs%10000*CutBp/10000;
        }
        if (!LDTactical::SettleRegen(Body.AttributeProjection,bContender ? &BodyRecovery : nullptr,Body.LastCombatAtMs,FromMs,NowMs,
            Pools,Error,bInDorm && DormRecovery ? &BodyDorm : nullptr))
        {
            Error=FString::Printf(TEXT("Regeneration of %s failed: %s"),*Body.Victim.EntityId.ToString(),*Error);
            return false;
        }
        // Momentum (contenders only): the combat gain while engaged, then the idle drift toward MOMENTUM.Start.
        int64 Momentum=Pool.MomentumP2;
        if (bContender && MomentumTerms && !SettleMomentum(Body,*MomentumTerms,FromMs,NowMs,Momentum,Error))
        {
            Error=FString::Printf(TEXT("Momentum of %s failed: %s"),*Body.Victim.EntityId.ToString(),*Error);
            return false;
        }
        const bool bChanged=Pool.HpP2!=Pools.HpP2 || Pool.MomentumP2!=Momentum;
        const int64 RestoredP2=Pools.HpP2-Pool.HpP2;
        Pool.HpP2=Pools.HpP2;
        Pool.MomentumP2=Momentum;
        Body.HpRegenMicroP2=Pools.HpMicroP2;
        Body.RegenThroughMs=NowMs;
        if (bChanged)
        {
            ++Pool.Revision;
            MarkViewChanged(Body.Victim.EntityId);
            if (bContender) { TrackEmpowerArmed(Body,NowMs); }
        }
        LedgerRegenerated(Body,RestoredP2);
    }
    return true;
}

bool ALDCombatDirector::LockDeath(ALDCharacter& /*Actor*/, FLDBodyCombatState& Body, const FLDCombatHit& Hit, FString& Error)
{
    if (Body.bPendingDeath) { return true; }
    // #198: a personal training dummy never dies; a fatal hit refills it instead (no death, reward or assist).
    if (Body.bTrainingDummy)
    {
        Body.Resources.HpP2=Body.Resources.MaxHpP2;
        ++Body.Resources.Revision;
        MarkViewChanged(Body.Victim.EntityId);
        return true;
    }
    const ILDLifeAuthority* Mode=Authority();
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
        Basis.CampAffixAtDeath=uint8(Body.CampAffix);
        Basis.bPeddlerAtDeath=Body.bPeddler;
    }
    // Credit eligibility is frozen at the fatal moment; a later leave or level-up never rewrites it.
    // #198 session D: a PK arena death is isolated from field growth rewards (no kill or assist reward).
    if (Body.PKSide!=0) { Basis.NoRewardReason=ELDNoRewardReason::PKArena; }
    else if (!Hit.CreditOwner.IsValid()) { Basis.NoRewardReason=ELDNoRewardReason::NoOwner; }
    else if (Hit.CreditOwner==Death.VictimContender) { Basis.NoRewardReason=ELDNoRewardReason::SelfInflicted; }
    else if (!Mode->IsContenderPresent(Hit.CreditOwner)) { Basis.NoRewardReason=ELDNoRewardReason::OwnerLeft; }
    else
    {
        Basis.NoRewardReason=ELDNoRewardReason::None;
        Basis.bCreditOwnerEligibleAtDeath=true;
    }
    // Assists exist only for a contender kill with an eligible killer; a departed contributor no longer earns one.
    if (Body.Victim.Kind==ELDVictimKind::Contender && Basis.bCreditOwnerEligibleAtDeath)
    {
        LDAssist::SelectAssisters(Body.AssistContributions,Hit.CreditOwner,Body.Victim.EntityId,Mode->GetAuthorityNowMs(),
            Content->Get().Parameters.AssistWindowMs,[Mode](const FGuid& Contender) { return Mode->IsContenderPresent(Contender); },
            Death.Assisters);
    }
    // The owner's recap of this death: the same window as the assists, the fatal hit included.
    if (Body.Victim.Kind==ELDVictimKind::Contender)
    {
        Death.RecapWindowMs=Content->Get().Parameters.AssistWindowMs;
        // Setters the victim cannot read (neither in its recap as attackers nor among the frozen assisters) stay unnamed.
        TArray<FLDDamageSample> Readable=Body.DamageLog;
        LDDamageRecap::HideUnreadableSetters(Readable,Mode->GetAuthorityNowMs(),Death.RecapWindowMs,Body.Victim.EntityId,Death.Assisters);
        LDDamageRecap::Summarize(Readable,Mode->GetAuthorityNowMs(),Death.RecapWindowMs,Death.Recap);
        if (!Hit.CreditOwner.IsValid() && Hit.SourceBody.Victim.Kind==ELDVictimKind::CampMember)
        {
            if (const FLDCombatEntityRecord* Monster=FindRecord(Hit.SourceBody.Victim.EntityId)) { Death.KillerMonsterId=Monster->MonsterAtLife(Hit.SourceBody.Victim.Life); }
        }
    }
    // Effects run on an integral authority boundary, so the fatal instant is that exact millisecond.
    Death.OccurredAtSeconds=Mode->GetAuthorityNowSeconds();
    Death.OccurredAtMs=Mode->GetAuthorityNowMs();
    // Y02/Y20: the kill and every frozen assist of a contender death reach the takers' cards.
    if (Body.Victim.Kind==ELDVictimKind::Contender && Basis.bCreditOwnerEligibleAtDeath)
    {
        const int64 WindowMs=Content->Get().Parameters.AssistWindowMs;
        // Spec §6.5: a contender kill is one step of the fatal hit's chain.
        ChainStep(Hit.CreditOwner,LDChain::KillStep(),Death.OccurredAtMs);
        if (!OnCardTakedown(Hit.CreditOwner,true,Body,WindowMs,Death.OccurredAtMs,Error)) { return false; }
        for (const FGuid& Assister : Death.Assisters)
        {
            if (Assister!=Hit.CreditOwner && !OnCardTakedown(Assister,false,Body,WindowMs,Death.OccurredAtMs,Error)) { return false; }
        }
    }
    PendingDeaths.Add(MoveTemp(Death));
    Error.Reset();
    return true;
}
