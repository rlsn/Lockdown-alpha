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
    {
        const FVector2D Delta(To.X-From.X,To.Y-From.Y), Offset(From.X-Centre.X,From.Y-Centre.Y);
        const double Outside=Offset.SizeSquared()-Reach*Reach;
        if (Outside<=0) { OutFraction=0; return true; }
        const double LengthSquared=Delta.SizeSquared();
        if (LengthSquared<=UE_DOUBLE_SMALL_NUMBER) { return false; }
        const double Half=FVector2D::DotProduct(Offset,Delta);
        const double Discriminant=Half*Half-LengthSquared*Outside;
        if (Discriminant<0) { return false; }
        const double Fraction=(-Half-FMath::Sqrt(Discriminant))/LengthSquared;
        if (Fraction<0 || Fraction>1) { return false; }
        OutFraction=Fraction;
        return true;
    }

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
        default: return 0;
        }
    }
    FString ControlNames(uint8 Mask)
    {
        static const TCHAR* Names[]={TEXT("root"),TEXT("stun"),TEXT("disarm"),TEXT("taunt"),TEXT("knockback"),TEXT("slow")};
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
        case ELDCombatHitClass::BasicSecondary: return TEXT("basic_secondary");
        case ELDCombatHitClass::Ability: return TEXT("ability");
        default: return TEXT("none");
        }
    }
    const TCHAR* VictimKindName(ELDVictimKind Kind) { return Kind==ELDVictimKind::CampMember ? TEXT("camp") : TEXT("contender"); }
}

bool ALDCombatDirector::ApplyHit(const FLDCombatHit& InHit, FString& Error)
{
    // Cleave appends to PendingHits; keep this hit stable even when the caller passed one of its elements.
    const FLDCombatHit Hit=InHit;
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
    const FLDProjectedAttributes& Defence=Body->AttributeProjection.Final;
    // Controls and breaches are judged before this hit applies its own.
    const bool bImpaired=IsImpaired(*Body,NowMs);
    bool bCritical=Hit.bCritical;
    // An ability critical is decided once per hit (one per target per cast); reflections and added damage never crit.
    if (!bCritical && Hit.HitClass==ELDCombatHitClass::Ability && !Hit.bReflect && !Hit.bAddedDamage)
    {
        if (Hit.bCritIfImpaired && (bImpaired || LDTactical::HasBreach(Body->Breaches,NowMs))) { bCritical=true; }
        else if (Hit.AbilityCritChanceBp>0 && CritRandom.RandRange(0,9999)<Hit.AbilityCritChanceBp) { bCritical=true; }
    }
    FLDDamageEvaluationInput Evaluation;
    Evaluation.Definition.Emplace(Hit.BaseP2,Hit.ADRatioBp,Hit.APRatioBp,Hit.DamageType,Hit.MaxHPRatioBp);
    Evaluation.HitClass=Hit.HitClass;
    Evaluation.bCritical=bCritical;
    Evaluation.SourceADP2=Hit.SourceADP2;
    Evaluation.SourceAPP2=Hit.SourceAPP2;
    if (Hit.MaxHPRatioBp!=0) { Evaluation.SourceMaxHPP2=Hit.SourceMaxHPP2; }
    const bool bPhysical=Hit.DamageType==ELDDemoDamageType::Physical;
    // Breaches cut the matching resistance first by their flat terms, then by their summed ratio.
    Evaluation.TargetResistanceP2=LDTactical::BreachedResistanceP2(Body->Breaches,
        bPhysical ? Defence.ArmorP2 : Defence.MagicResistP2,bPhysical,NowMs);
    Evaluation.TargetHPP2=Body->Resources.HpP2;
    // active_cancel_blast Lv3 shatters a share of the shield before the damage; that is not a break.
    const int64 ShieldBeforeP2=Body->Resources.ShieldP2;
    const int64 ShatteredP2=bProtected ? 0 : FMath::Min(ShieldBeforeP2,
        ShieldBeforeP2/10000*Hit.ShieldShatterBp+ShieldBeforeP2%10000*Hit.ShieldShatterBp/10000);
    Evaluation.TargetShieldP2=ShieldBeforeP2-ShatteredP2;
    Evaluation.bTargetProtected=bProtected;
    // A damage guard reduces what reaches the shield (active_porcelain_flop).
    if (Body->Guard.IsSet() && Body->Guard->ExpiresAtMs>NowMs) { Evaluation.MitigateBp=Body->Guard->ReductionBp; }
    // Rear strike: the releasing body in the target's rear arc at this hit, by the target's authority facing; active_thousand_year Lv3
    // also counts a rooted, slowed or taunted target.
    const bool bRearStrike=Hit.BackstabRatioBp>0 || Hit.BackstabStunMs>0;
    const bool bBehind=bRearStrike && (IsSourceBehind(Hit.SourceBody,*Actor)
        || (Hit.bBackstabImpaired && (LDTactical::HasActiveRoot(Body->Roots,NowMs) || LDTactical::StrongestSlowBp(Body->Slows,NowMs)>0
            || LDControl::IsTaunted(Body->Taunt,NowMs))));
    // "Your damage +X%" terms (passive_public_shame, passive_eagle_eye) add into one (1 + sum) factor before resistance; the ability's own
    // multipliers (a rear strike) and the target's critical reduction multiply it.
    int64 DealtBp=0;
    if (Hit.ControlAmpBp>0 && bImpaired) { DealtBp+=Hit.ControlAmpBp; }
    if (Hit.DistanceAmpPer100Bp>0 && SourceActor)
    {
        const double Distance=FVector::Dist2D(FootOf(*SourceActor),FootOf(*Actor));
        const double Amp=FMath::IsFinite(Distance) ? Distance*double(Hit.DistanceAmpPer100Bp)/100. : 0.;
        DealtBp+=FMath::Clamp<int64>(int64(FMath::FloorToDouble(Amp)),0,Hit.DistanceAmpCapBp>0 ? Hit.DistanceAmpCapBp : MAX_int32);
    }
    int64 AmplifyBp=10000+DealtBp;
    if (bBehind) { AmplifyBp=AmplifyBp*(10000+Hit.BackstabRatioBp)/10000; }
    if (bCritical)
    {
        TArray<FLDPassiveEffectRef> Guards;
        if (!CollectPassiveEffects(Body->AttributeProjection,ELDDemoEffectOp::CritDamageReduction,Guards,Error)) { return false; }
        int64 CutBp=0;
        for (const FLDPassiveEffectRef& Guard : Guards) { CutBp=FMath::Max(CutBp,Guard.Effect->RatioBp); }
        AmplifyBp=AmplifyBp*(10000-FMath::Clamp<int64>(CutBp,0,10000))/10000;
    }
    Evaluation.AmplifyBp=AmplifyBp;
    // #112: the negative kinds the target carries at this hit (counted before this hit adds any).
    const int32 NegativeKinds=Hit.NegativeAmpBp>0 || Hit.NegativeHealMaxHPBp>0
        ? LDCombatMechanics::CountNegativeKinds(NegativeKindsOf(*Body,NowMs)) : 0;
    int64 VulnerableBp=0;
    if (Hit.bPeriodic || Hit.HitClass==ELDCombatHitClass::BasicPrimary)
    {
        VulnerableBp=LDCombatMechanics::VulnerableBp(Body->Vulnerabilities,Body->Roots,NowMs);
    }
    for (const int64 Factor : {Hit.ExtraAmplifyBp, 10000+VulnerableBp, LDCombatMechanics::NegativeKindsAmplifyBp(NegativeKinds,Hit.NegativeAmpBp)})
    {
        if (Factor!=10000) { Evaluation.AmplifyBp=Evaluation.AmplifyBp*FMath::Max<int64>(Factor,0)/10000; }
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
    const int64 StunMs=FMath::Max(Hit.StunDurationMs,bBehind ? Hit.BackstabStunMs : 0);
    TArray<FLDCombatBodyRef> Cleaved;
    if (Hit.OnHitScaleBp>0 && Hit.bCleave && Damage.DamageP2>0)
    {
        if (Hit.CleaveRatioBp<0 || !FMath::IsFinite(Hit.CleaveRadiusCm) || Hit.CleaveRadiusCm<0)
        {
            Error=TEXT("Cleave requires a valid ratio and radius.");
            return false;
        }
        // The anchor is the primary target's foot at this hit, never re-read after that target dies.
        if (!CollectAreaTargets(FootOf(*Actor),Hit.CleaveRadiusCm,Body,Hit.SourceEntity,Cleaved,Error))
        {
            Error=TEXT("Cleave: ")+Error;
            return false;
        }
    }
    TArray<FLDPassiveEffectRef> LowShields;
    if (!CollectPassiveEffects(Body->AttributeProjection,ELDDemoEffectOp::TriggeredShield,LowShields,Error)) { return false; }
    TArray<FLDPassiveEffectRef> ManaGrants;
    if (Source && !CollectPassiveEffects(Source->AttributeProjection,ELDDemoEffectOp::ManaOnHit,ManaGrants,Error)) { return false; }
    // Vamp of this hit: on-hit lifesteal (already at its on-hit coefficient), passive_cornered omnivamp while the source is low and
    // a vamp mark the source holds on this body from an earlier hit; all on the HP actually lost.
    int64 VampBp=0;
    if (Source && !Hit.bReflect && Damage.HPLostP2>0)
    {
        if (Hit.OnHitScaleBp>0 && Hit.bLifesteal) { VampBp+=Hit.LifestealRatioBp; }
        if (Hit.OmnivampBp>0 && LDTactical::BelowHPThreshold(Source->Resources.HpP2,Source->Resources.MaxHpP2,Hit.OmnivampThresholdBp))
        { VampBp+=Hit.OmnivampBp; }
        VampBp+=LDTactical::VampMarkBp(Body->VampMarks,Body->Slows,Hit.SourceEntity,NowMs);
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
    Body->Resources.HpP2=Damage.RemainingHPP2;
    Body->Resources.ShieldP2=Damage.RemainingShieldP2;
    ++Body->Resources.Revision;
    const bool bShieldBroken=LDCombatMechanics::ShieldBroken(ShieldBeforeP2-ShatteredP2,Damage.RemainingShieldP2,Damage.ShieldAbsorbedP2);
    const int64 ShieldGrantedP2=Body->ShieldGrantedP2;
    // A barrier emptied by this hit ends by the same timed-status rule as an expired one.
    const FLDStatusExpiry Ended=LDTactical::ExpireTimedStatuses(Body->Roots,Body->Shield,Body->Resources.ShieldP2,NowMs);
    if (Ended.bRootsChanged || Ended.bShieldChanged) { ++Body->StateRevision; }
    // A positive mitigated hit is combat even when the barrier absorbed all of it.
    if (Damage.DamageP2>0) { Body->LastCombatAtMs=NowMs; }
    // Controls and slows go through the single control path (diminishing returns, interruptions, spray, assists).
    bool bControlled=false;
    uint8 ControlMask=0;
    const auto Control=[&](ELDControlKind Kind, int64 DurationMs, int64 RatioBp, int64 AttackRatioBp, bool& bOut)
    {
        FLDControlApplication App;
        App.Kind=Kind;
        App.EffectId=Hit.EffectId;
        App.SourceEntity=Hit.SourceEntity;
        App.SourceBody=Hit.SourceBody;
        App.CreditOwner=Hit.CreditOwner;
        App.DurationMs=DurationMs;
        App.RatioBp=RatioBp;
        App.AttackRatioBp=AttackRatioBp;
        bOut=false;
        if (!CommitControl(*Body,App,NowMs,bOut,Error)) { return false; }
        bControlled|=bOut;
        if (bOut) { ControlMask|=LDCombatEffectsDetail::ControlBit(Kind); }
        return true;
    };
    if (bControllable)
    {
        bool bApplied=false;
        if (Hit.RootDurationMs>0 && !Control(ELDControlKind::Root,Hit.RootDurationMs,0,0,bApplied)) { return false; }
        // active_binding_snare Lv3: the target is vulnerable for as long as the root this hit committed.
        if (bApplied && Hit.VulnerableBp>0)
        {
            if (const FLDCombatRoot* Root=Body->Roots.FindByPredicate([&Hit](const FLDCombatRoot& Row) { return Row.EffectId==Hit.EffectId; }))
            { Body->Vulnerabilities.Add(FLDCombatVulnerable{Hit.EffectId,Hit.VulnerableBp,Root->ExpiresAtMs}); }
        }
        if ((Hit.DisarmDurationMs>0 && !Control(ELDControlKind::Disarm,Hit.DisarmDurationMs,0,0,bApplied))
            || (StunMs>0 && !Control(ELDControlKind::Stun,StunMs,0,0,bApplied)))
        { return false; }
        if (Hit.KnockbackCm>0 && Hit.KnockbackMs>0)
        {
            if (!ApplyKnockback(*Actor,*Body,Hit,NowMs,bApplied,Error)) { return false; }
            bControlled|=bApplied;
            if (bApplied) { ControlMask|=LDCombatEffectsDetail::KnockbackBit; }
        }
        if (Hit.SlowDurationMs>0 && Hit.SlowBp>0)
        {
            // item_megaphone scales the slows its holder applies (#112 §3), never past a full stop.
            int64 SlowBp=Hit.SlowBp;
            if (Source)
            {
                if (const FLDDemoEffectDefinition* Amp=FindTrigger(Source->AttributeProjection,ELDDemoEffectOp::AfflictionAmp))
                { SlowBp=FMath::Min<int64>(SlowBp*Amp->SecondaryRatioBp/10000,9999); }
            }
            if (!Control(ELDControlKind::Slow,Hit.SlowDurationMs,SlowBp,Hit.SlowAttackBp,bApplied)) { return false; }
            // active_barrage_rant restarts this source's slows whole: remember the authored duration.
            if (bApplied)
            {
                for (FLDCombatSlow& Slow : Body->Slows) { if (Slow.EffectId==Hit.EffectId) { Slow.DurationMs=Hit.SlowDurationMs; } }
            }
            // active_slipper_throw Lv3: while this slow lasts the source vamps against this body; the mark is bound to it.
            const FLDCombatSlow* Bound=bApplied && Hit.SlowVampBp>0
                ? Body->Slows.FindByPredicate([&Hit](const FLDCombatSlow& Slow) { return Slow.EffectId==Hit.EffectId; }) : nullptr;
            if (Bound)
            {
                FLDCombatVampMark Mark;
                Mark.SourceEntity=Hit.SourceEntity;
                Mark.RatioBp=Hit.SlowVampBp;
                Mark.ExpiresAtMs=Bound->ExpiresAtMs;
                Mark.SlowEffectId=Bound->EffectId;
                LDTactical::ApplyVampMark(Body->VampMarks,Mark);
            }
        }
        if (Hit.ShredMs>0 && (Hit.ShredArmorBp>0 || Hit.ShredMagicResistBp>0))
        {
            FLDCombatBreach Breach;
            Breach.Key=Hit.ShredKey;
            Breach.SourceEntity=Hit.SourceEntity;
            Breach.ArmorRatioBp=Hit.ShredArmorBp;
            Breach.MagicResistRatioBp=Hit.ShredMagicResistBp;
            if (!LDMatchLifecycle::AddDurationMs(NowMs,Hit.ShredMs,Breach.ExpiresAtMs,Error)) { return false; }
            LDTactical::ApplyBreach(Body->Breaches,Breach,NowMs);
            ++Body->StateRevision;
            // A breach is a debuff: its owner's assist contribution on this life.
            RecordAssistContribution(*Body,Hit.CreditOwner,NowMs);
        }
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
    // active_come_here Lv3 counts the enemy basic and ability hits (positive damage, absorbed included) taken while it lasts.
    if (Body->Retaliation.IsSet() && NowMs<Body->Retaliation->EndAtMs && Damage.DamageP2>0 && !Hit.bReflect && !Hit.bAddedDamage
        && Hit.SourceEntity!=Body->Victim.EntityId
        && (Hit.HitClass==ELDCombatHitClass::BasicPrimary || Hit.HitClass==ELDCombatHitClass::Ability))
    { ++Body->Retaliation->HitsTaken; }
    // passive_second_wind: an ability hit opens the regeneration surge.
    const auto& Wind=Body->AttributeProjection.OutOfCombatHPRegen;
    if (Wind.IsSet() && Wind->SurgeMs>0 && Hit.HitClass==ELDCombatHitClass::Ability && Damage.DamageP2>0
        && !LDMatchLifecycle::AddDurationMs(NowMs,Wind->SurgeMs,Body->RegenSurgeUntilMs,Error))
    { return false; }
    // passive_cornered Lv3: falling below the threshold grants a shield once per internal cooldown.
    for (const FLDPassiveEffectRef& Shield : LowShields)
    {
        const FLDDemoEffectDefinition& Effect=*Shield.Effect;
        if (Effect.Trigger!=ELDDemoEffectTrigger::LowHP || Body->Resources.HpP2<=0 || NowMs<Body->LowHPShieldReadyAtMs
            || LDTactical::BelowHPThreshold(HPBefore,Body->Resources.MaxHpP2,Effect.RatioBp)
            || !LDTactical::BelowHPThreshold(Body->Resources.HpP2,Body->Resources.MaxHpP2,Effect.RatioBp))
        { continue; }
        const int64 Amount=Effect.BaseP2+Body->Resources.MaxHpP2/10000*Effect.MaxHPRatioBp+Body->Resources.MaxHpP2%10000*Effect.MaxHPRatioBp/10000;
        if (!GrantShield(*Body,Amount,Effect.DurationMs,FGuid::NewGuid(),NowMs,Error)
            || !LDMatchLifecycle::AddDurationMs(NowMs,Effect.IntervalMs,Body->LowHPShieldReadyAtMs,Error))
        { return false; }
    }
    MarkViewChanged(Hit.Target.Victim.EntityId);
    const int64 HPAfterP2=Body->Resources.HpP2;
    // A one-time heal: it passes the source's heal cut (#112 §5); the record below logs what the source actually regained.
    int64 VampHealedP2=0;
    if (Source && HealP2>0)
    {
        const int64 SourceHPBefore=Source->Resources.HpP2;
        HealBody(*Source,HealP2,NowMs);
        VampHealedP2=Source->Resources.HpP2-SourceHPBefore;
    }
    // Host playtest record of this committed hit (docs/DEVELOPMENT.md): every term the player could ask about afterwards.
    if (UE_LOG_ACTIVE(LogLDCombat,Log))
    {
        using namespace LDCombatEffectsDetail;
        const FLDCombatEntityRecord* SourceRecord=FindRecord(Hit.SourceEntity);
        const FName SourceMonster=Source ? Source->MonsterId : (SourceRecord ? SourceRecord->MonsterId : NAME_None);
        UE_LOG(LogLDCombat,Log,TEXT("LD_HIT t=%lld src=%s src_kind=%s src_monster=%s credit=%s victim=%s victim_life=%u victim_kind=%s victim_monster=%s via=%s class=%s type=%s dmg=%s pre=%s absorbed=%s shattered=%s hp_lost=%s hp=%s->%s max_hp=%s shield=%s->%s amp_bp=%lld resist=%s mitigate_bp=%lld crit=%d backstab=%d protected=%d periodic=%d reflect=%d added=%d controls=%s vamp=%s lethal=%d"),
            NowMs,*LDLog::Id(Hit.SourceEntity),VictimKindName(Hit.SourceBody.Victim.Kind),
            SourceMonster.IsNone() ? TEXT("-") : *SourceMonster.ToString(),*LDLog::Id(Hit.CreditOwner),
            *LDLog::Id(Body->Victim.EntityId),Body->Victim.Life,VictimKindName(Body->Victim.Kind),
            Body->MonsterId.IsNone() ? TEXT("-") : *Body->MonsterId.ToString(),*LDLog::Source(Hit.Source),HitClassName(Hit.HitClass),
            bPhysical ? TEXT("physical") : TEXT("magical"),*LDLog::P2(Damage.DamageP2),*LDLog::P2(Damage.PreMitigationP2),
            *LDLog::P2(Damage.ShieldAbsorbedP2),*LDLog::P2(ShatteredP2),*LDLog::P2(Damage.HPLostP2),*LDLog::P2(HPBefore),
            *LDLog::P2(HPAfterP2),*LDLog::P2(Body->Resources.MaxHpP2),*LDLog::P2(ShieldBeforeP2),
            *LDLog::P2(Body->Resources.ShieldP2),Evaluation.AmplifyBp,*LDLog::P2(Evaluation.TargetResistanceP2.Get(0)),
            Evaluation.MitigateBp,bCritical,bBehind,bProtected,Hit.bPeriodic,Hit.bReflect,Hit.bAddedDamage,*ControlNames(ControlMask),
            *LDLog::P2(VampHealedP2),HPAfterP2==0);
    }
    if (Source)
    {
        // Mana grants, at most once per release: passive_public_shame Lv3 for a control this hit committed, item_runic_focus for an ability hit.
        for (const FLDPassiveEffectRef& Grant : ManaGrants)
        {
            const FLDDemoEffectDefinition& Effect=*Grant.Effect;
            if (Effect.Trigger==ELDDemoEffectTrigger::Control && bControlled && Source->LastControlManaEffect!=Hit.EffectId)
            {
                Source->LastControlManaEffect=Hit.EffectId;
                GrantMana(*Source,Effect.BaseP2);
            }
            else if (Effect.Trigger==ELDDemoEffectTrigger::AbilityHit && !bProtected && !Hit.bReflect
                && Hit.HitClass==ELDCombatHitClass::Ability && Hit.Token!=0
                && (Hit.Source.Kind==ELDCombatSourceKind::Learned || Hit.Source.Kind==ELDCombatSourceKind::Gear)
                && Source->LastAbilityManaEffect!=Hit.EffectId)
            {
                Source->LastAbilityManaEffect=Hit.EffectId;
                GrantMana(*Source,Effect.BaseP2);
            }
        }
    }
    // #112 §5 mechanics of this committed hit, after its damage and controls.
    if (bControllable && Hit.Dot.IsSet())
    {
        FLDCombatDot Dot=Hit.Dot.GetValue();
        if (!LDMatchLifecycle::AddDurationMs(NowMs,Dot.IntervalMs,Dot.NextTickAtMs,Error)
            // The record outlives its last tick by one millisecond so that tick still meets the full breach.
            || !LDMatchLifecycle::AddDurationMs(NowMs,Dot.DurationMs+1,Dot.ExpiresAtMs,Error)) { return false; }
        Body->Dots.RemoveAll([&Dot](const FLDCombatDot& Row) { return Row.Key==Dot.Key && Row.SourceEntity==Dot.SourceEntity; });
        if (Dot.ShredStepP2>0)
        {
            FLDCombatBreach Breach;
            Breach.Key=Dot.Key;
            Breach.SourceEntity=Dot.SourceEntity;
            Breach.ArmorFlatP2=Breach.MagicResistFlatP2=Dot.ShredStepP2;
            Breach.Stacks=1;
            Breach.MaxStacks=FMath::Max(Dot.TicksTotal,1);
            Breach.ExpiresAtMs=Dot.ExpiresAtMs;
            LDTactical::ApplyBreach(Body->Breaches,Breach,NowMs);
        }
        Body->Dots.Add(MoveTemp(Dot));
        ++Body->StateRevision;
        // Damage over time (and its first breach stack) is a debuff: its owner's assist contribution on this life.
        RecordAssistContribution(*Body,Hit.CreditOwner,NowMs);
    }
    if (bControllable && Hit.bRefreshDebuffs)
    {
        // active_barrage_rant: this source's slows and damage over time on the target restart whole.
        for (FLDCombatSlow& Slow : Body->Slows)
        {
            if (Slow.SourceEntity==Hit.SourceEntity && Slow.DurationMs>0 && Slow.ExpiresAtMs>NowMs
                && !LDMatchLifecycle::AddDurationMs(NowMs,Slow.DurationMs,Slow.ExpiresAtMs,Error)) { return false; }
        }
        // A refreshed slow carries its vamp mark along.
        LDTactical::SyncVampMarks(Body->VampMarks,Body->Slows,NowMs);
        for (FLDCombatDot& Dot : Body->Dots)
        {
            if (Dot.SourceEntity!=Hit.SourceEntity || Dot.ExpiresAtMs<=NowMs) { continue; }
            Dot.TicksLeft=Dot.TicksTotal;
            if (Dot.TicksTotal>0 && !LDMatchLifecycle::AddDurationMs(NowMs,Dot.IntervalMs,Dot.NextTickAtMs,Error)) { return false; }
            if (!LDMatchLifecycle::AddDurationMs(NowMs,Dot.DurationMs+(Dot.TicksTotal>0 ? 1 : 0),Dot.ExpiresAtMs,Error)) { return false; }
            for (FLDCombatBreach& Breach : Body->Breaches)
            {
                if (Breach.Key==Dot.Key && Breach.SourceEntity==Dot.SourceEntity) { Breach.ExpiresAtMs=Dot.ExpiresAtMs; }
            }
        }
        ++Body->StateRevision;
    }
    if (Hit.NegativeHealMaxHPBp>0 && NegativeKinds>0)
    {
        if (Source)
        {
            const int64 PerKind=Source->Resources.MaxHpP2/10000*Hit.NegativeHealMaxHPBp+Source->Resources.MaxHpP2%10000*Hit.NegativeHealMaxHPBp/10000;
            HealBody(*Source,PerKind*NegativeKinds,NowMs);
        }
    }
    if (bShieldBroken && !OnShieldBroken(*Body,Hit,ShieldGrantedP2,NowMs,Error)) { return false; }
    if (Damage.PreMitigationP2>0 && !CountGuard(*Actor,*Body,Damage.PreMitigationP2,NowMs,Error)) { return false; }
    if (bControlled && !OnControlApplied(Hit,NowMs,Error)) { return false; }
    if (!OnHitLanded(*Body,Hit,Damage.DamageP2,NowMs,Error)) { return false; }
    if (!ApplyOnHitEffects(Hit,*Body,Damage,Cleaved,NowMs,Error)) { return false; }
    // Thorns answer a hit the owner survived; a reflection never answers another reflection.
    if (!Hit.bReflect && Damage.DamageP2>0 && Body->Resources.HpP2>0
        && !QueueThorns(Hit,*Actor,*Body,Damage.DamageP2,bCritical,Error)) { return false; }
    if (Body->Resources.HpP2==0 && !LockDeath(*Actor,*Body,Hit,Error)) { return false; }
    return true;
}

bool ALDCombatDirector::ApplyOnHitEffects(const FLDCombatHit& Hit, FLDBodyCombatState& Target, const FLDDamageEvaluation& Damage,
    const TArray<FLDCombatBodyRef>& Cleaved, int64 NowMs, FString& Error)
{
    // Only a hit that triggers on-hit effects reaches here with a coefficient; anything it makes triggers nothing.
    if (Hit.OnHitScaleBp<=0) { return true; }
    // passive_dox_master: one layer per landed coefficient hit, at any coefficient (#112 Q3).
    if (Damage.DamageP2>0)
    {
        FLDBodyCombatState* Source=FindBody(Hit.SourceBody);
        if (Source && !StackDox(Target,*Source,Hit,NowMs,Error)) { return false; }
    }
    // Cleave: each secondary takes CleaveRatio (already at the on-hit coefficient) of the primary's raw terms, the merged
    // empowerment included, never its critical. POLICY.BasicSecondaryTriggers=Disabled: secondaries never crit, heal,
    // cleave or start another attack.
    for (const FLDCombatBodyRef& Secondary : Cleaved)
    {
        FLDCombatHit& Sweep=PendingHits.AddDefaulted_GetRef();
        Sweep.HitId=FGuid::NewGuid();
        Sweep.EffectId=Hit.EffectId;
        Sweep.Token=Hit.Token;
        Sweep.Target=Secondary;
        Sweep.SourceBody=Hit.SourceBody;
        Sweep.SourceEntity=Hit.SourceEntity;
        Sweep.CreditOwner=Hit.CreditOwner;
        Sweep.Source=Hit.Source;
        Sweep.HitClass=ELDCombatHitClass::BasicSecondary;
        Sweep.BaseP2=Hit.BaseP2*Hit.CleaveRatioBp/10000;
        Sweep.ADRatioBp=Hit.ADRatioBp*Hit.CleaveRatioBp/10000;
        Sweep.APRatioBp=0;
        Sweep.DamageType=ELDDemoDamageType::Physical;
        Sweep.SourceADP2=Hit.SourceADP2;
        Sweep.SourceAPP2=Hit.SourceAPP2;
        Sweep.ControlAmpBp=Hit.ControlAmpBp;
        Sweep.DistanceAmpPer100Bp=Hit.DistanceAmpPer100Bp;
        Sweep.DistanceAmpCapBp=Hit.DistanceAmpCapBp;
        Sweep.OmnivampBp=Hit.OmnivampBp;
        Sweep.OmnivampThresholdBp=Hit.OmnivampThresholdBp;
        Sweep.bAddedDamage=true;
    }
    return true;
}

bool ALDCombatDirector::CollectPassiveEffects(const FLDAttributeProjection& Projection, ELDDemoEffectOp Op,
    TArray<FLDPassiveEffectRef>& Out, FString& Error) const
{
    Out.Reset();
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!Content) { Error=TEXT("Passive effects require the approved Demo content."); return false; }
    for (const FLDLearnedTriggerTerm& Term : Projection.LearnedTriggers)
    {
        const FLDDemoAbilityDefinition* Ability=nullptr;
        if (!Content->FindAbility(Term.AbilityId,Ability,Error)) { return false; }
        const FLDDemoRankDefinition* Rank=Ability->Ranks.FindByPredicate([&Term](const FLDDemoRankDefinition& Row) { return Row.Rank==int32(Term.Rank); });
        if (!Rank) { Error=FString::Printf(TEXT("Learned trigger %s has no rank %u."),*Term.AbilityId,uint32(Term.Rank)); return false; }
        for (const FLDDemoEffectDefinition& Effect : Rank->Effects)
        {
            if (Effect.GetOp()!=Op) { continue; }
            FLDPassiveEffectRef& Ref=Out.AddDefaulted_GetRef();
            Ref.Key=Term.AbilityId;
            Ref.Source.Kind=ELDCombatSourceKind::Learned;
            Ref.Source.DefinitionId=FName(*Term.AbilityId);
            Ref.Effect=&Effect;
        }
    }
    // POLICY.DuplicateEquipmentPassive=OncePerFamily: two copies still apply once per family.
    for (const FLDEquipmentPassiveFamily& Family : Projection.EquipmentPassives)
    {
        const FLDDemoEquipmentDefinition* Equipment=nullptr;
        if (!Content->FindEquipment(Family.EquipmentId,Equipment,Error)) { return false; }
        // The upgraded copy's terms replace the base ones (#112 equipment upgrade).
        const TArray<FLDDemoEffectDefinition>* Effects=LDDemoEquipment::EffectivePassiveEffects(*Equipment,Family.bUpgraded);
        if (!Effects)
        {
            Error=FString::Printf(TEXT("Upgraded equipment passive %s has no approved terms."),*Family.Family);
            return false;
        }
        for (const FLDDemoEffectDefinition& Effect : *Effects)
        {
            if (Effect.GetOp()!=Op) { continue; }
            FLDPassiveEffectRef& Ref=Out.AddDefaulted_GetRef();
            Ref.Key=Family.Family;
            Ref.Source.Kind=ELDCombatSourceKind::Gear;
            Ref.Source.DefinitionId=FName(*Family.EquipmentId);
            Ref.Effect=&Effect;
        }
    }
    Error.Reset();
    return true;
}

bool ALDCombatDirector::SnapshotOnHit(const FLDBodyCombatState& Source, int64 ScaleBp, FLDCombatHit& InOut, FString& Error) const
{
    InOut.OnHitScaleBp=FMath::Max<int64>(0,ScaleBp);
    InOut.bCleave=InOut.bLifesteal=false;
    InOut.CleaveRatioBp=InOut.LifestealRatioBp=0;
    InOut.CleaveRadiusCm=0;
    if (InOut.OnHitScaleBp==0) { return true; }
    TArray<FLDPassiveEffectRef> Effects;
    if (!CollectPassiveEffects(Source.AttributeProjection,ELDDemoEffectOp::Cleave,Effects,Error)) { return false; }
    for (const FLDPassiveEffectRef& Ref : Effects)
    {
        InOut.bCleave=true;
        InOut.CleaveRatioBp=FMath::Max(InOut.CleaveRatioBp,Ref.Effect->RatioBp*InOut.OnHitScaleBp/10000);
        InOut.CleaveRadiusCm=FMath::Max(InOut.CleaveRadiusCm,Ref.Effect->RadiusCm.Denominator>0
            ? double(Ref.Effect->RadiusCm.Numerator)/double(Ref.Effect->RadiusCm.Denominator) : 0.);
    }
    if (!CollectPassiveEffects(Source.AttributeProjection,ELDDemoEffectOp::Lifesteal,Effects,Error)) { return false; }
    for (const FLDPassiveEffectRef& Ref : Effects)
    {
        InOut.bLifesteal=true;
        InOut.LifestealRatioBp+=Ref.Effect->RatioBp*InOut.OnHitScaleBp/10000;
    }
    // C2: passive_dox_master stacks snapshot here.
    return true;
}

bool ALDCombatDirector::StampSourceTerms(const FLDBodyCombatState& Source, FLDCombatHit& InOut, FString& Error) const
{
    const FLDAttributeProjection& Projection=Source.AttributeProjection;
    InOut.ControlAmpBp=Projection.ControlAmp.IsSet() ? Projection.ControlAmp->RatioBp : 0;
    InOut.OmnivampBp=Projection.LowHPBonus.IsSet() ? Projection.LowHPBonus->OmnivampBp : 0;
    InOut.OmnivampThresholdBp=Projection.LowHPBonus.IsSet() ? Projection.LowHPBonus->ThresholdBp : 0;
    InOut.DistanceAmpPer100Bp=InOut.DistanceAmpCapBp=0;
    TArray<FLDPassiveEffectRef> Reach;
    if (!CollectPassiveEffects(Projection,ELDDemoEffectOp::LongReach,Reach,Error)) { return false; }
    for (const FLDPassiveEffectRef& Ref : Reach)
    {
        InOut.DistanceAmpPer100Bp=FMath::Max(InOut.DistanceAmpPer100Bp,Ref.Effect->RatioBp);
        InOut.DistanceAmpCapBp=FMath::Max(InOut.DistanceAmpCapBp,Ref.Effect->CapBp);
    }
    return true;
}

FLDCombatHit ALDCombatDirector::MakeAmbushHit(const FLDCombatHit& Primary, const FLDAmbushDamageTerm& Term)
{
    FLDCombatHit Hit=Primary;
    Hit.HitId=FGuid::NewGuid();
    Hit.HitClass=ELDCombatHitClass::Ability;
    Hit.BaseP2=Term.BaseP2;
    Hit.ADRatioBp=Term.ADRatioBp;
    Hit.APRatioBp=Term.APRatioBp;
    Hit.DamageType=Term.DamageType;
    Hit.MaxHPRatioBp=0;
    Hit.bCritical=Hit.bCritIfImpaired=false;
    Hit.AbilityCritChanceBp=0;
    Hit.RootDurationMs=Hit.SlowBp=Hit.SlowDurationMs=Hit.SlowAttackBp=Hit.SlowVampBp=0;
    Hit.DisarmDurationMs=Hit.StunDurationMs=Hit.BackstabRatioBp=Hit.BackstabStunMs=0;
    Hit.bBackstabImpaired=Hit.bReflect=false;
    Hit.OnHitScaleBp=0;
    Hit.bCleave=Hit.bLifesteal=false;
    Hit.CleaveRatioBp=Hit.LifestealRatioBp=0;
    Hit.CleaveRadiusCm=0;
    Hit.bAddedDamage=true;
    // #112 C2: an added hit carries none of the primary's mechanics payload.
    Hit.bPeriodic=Hit.bFromShieldTrigger=Hit.bSilent=Hit.bRefreshDebuffs=Hit.bKnockbackFromAnchor=false;
    Hit.ExtraAmplifyBp=10000;
    Hit.KnockbackCm=0;
    Hit.KnockbackMs=Hit.VulnerableBp=Hit.NegativeAmpBp=Hit.ShieldShatterBp=Hit.NegativeHealMaxHPBp=0;
    Hit.Dot.Reset();
    // passive_lurking Lv3: the first strike shreds both resistances of the struck body.
    Hit.ShredKey=FName(*Term.AbilityId);
    Hit.ShredArmorBp=Hit.ShredMagicResistBp=Term.ShredRatioBp;
    Hit.ShredMs=Term.ShredDurationMs;
    return Hit;
}

bool ALDCombatDirector::AmbushReadyNow(const FLDBodyCombatState& Body, int64 NowMs) const
{
    const auto& Term=Body.AttributeProjection.Ambush;
    const int64 Credit=Body.AmbushCreditFromMs==Body.LastCombatAtMs ? Body.AmbushCreditMs : 0;
    return Term.IsSet() && !Body.bPendingDeath && LDTactical::AmbushReady(Body.LastCombatAtMs,Term->DelayMs,NowMs,Credit);
}

void ALDCombatDirector::CreditAmbush(FLDBodyCombatState& Body, int64 AmountMs)
{
    if (AmountMs<=0 || !Body.AttributeProjection.Ambush.IsSet()) { return; }
    if (Body.AmbushCreditFromMs!=Body.LastCombatAtMs)
    {
        Body.AmbushCreditFromMs=Body.LastCombatAtMs;
        Body.AmbushCreditMs=0;
    }
    Body.AmbushCreditMs=FMath::Min(Body.AttributeProjection.Ambush->DelayMs,Body.AmbushCreditMs+AmountMs);
    MarkViewChanged(Body.Victim.EntityId);
}

void ALDCombatDirector::GrantMana(FLDBodyCombatState& Body, int64 AmountP2)
{
    auto& Pool=Body.Resources;
    if (AmountP2<=0 || Body.bPendingDeath || Pool.MpP2>=Pool.MaxMpP2) { return; }
    Pool.MpP2=AmountP2>=Pool.MaxMpP2-Pool.MpP2 ? Pool.MaxMpP2 : Pool.MpP2+AmountP2;
    ++Pool.Revision;
    MarkViewChanged(Body.Victim.EntityId);
}

bool ALDCombatDirector::GrantShield(FLDBodyCombatState& Body, int64 AmountP2, int64 DurationMs, const FGuid& EffectId,
    int64 NowMs, FString& Error)
{
    // Triggered shields pass the single shield entry (heal cut, ReplaceIfNotWeaker, refresh events; #112 §5).
    return AmountP2<=0 || Body.bPendingDeath
        || GrantShieldTo(Body,AmountP2,DurationMs,EffectId,Body.Victim.EntityId,false,NowMs,Error);
}

bool ALDCombatDirector::CastRangeCm(const FLDBodyCombatState& Body, const FLDDemoTargetDefinition& Target, double& OutCm,
    FString& Error) const
{
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!Content) { Error=TEXT("Cast range requires the approved Demo content."); return false; }
    const auto Cm=[](const FLDDemoRational& Value) { return Value.Denominator>0 ? double(Value.Numerator)/double(Value.Denominator) : 0.; };
    OutCm=Cm(Target.RangeCm);
    // active_arc_bolt: a share of the extra basic-attack range above the catalog base, up to its cap.
    if (Target.AttackRangeRatioBp>0)
    {
        const double Extra=FMath::Max(0.,Cm(Body.AttributeProjection.Final.AttackRangeCm)-Cm(Content->Get().Parameters.AttackRangeCm.Base));
        OutCm+=FMath::Min(Extra*double(Target.AttackRangeRatioBp)/10000.,Cm(Target.AttackRangeBonusCapCm));
    }
    if (Target.Mode==ELDDemoTargetMode::Unit || Target.Mode==ELDDemoTargetMode::DirectionProjectile)
    {
        TArray<FLDPassiveEffectRef> Reach;
        if (!CollectPassiveEffects(Body.AttributeProjection,ELDDemoEffectOp::LongReach,Reach,Error)) { return false; }
        double Bonus=0;
        for (const FLDPassiveEffectRef& Ref : Reach) { Bonus=FMath::Max(Bonus,Cm(Ref.Effect->DistanceCm)); }
        OutCm+=Bonus;
    }
    else if (Target.Mode==ELDDemoTargetMode::GroundPoint)
    {
        int64 CooldownRatioBp=10000, DistanceBonusBp=0;
        if (!LDCombatSourceRules::SkillDisplacementBoost(*Content,Body.AttributeProjection.EquipmentPassives,CooldownRatioBp,DistanceBonusBp,Error))
        { return false; }
        OutCm*=double(10000+DistanceBonusBp)/10000.;
    }
    return FMath::IsFinite(OutCm) && OutCm>0;
}

bool ALDCombatDirector::ResolveDashSweeps(FString& Error)
{
    if (DashSweeps.IsEmpty()) { return true; }
    const ALDGameMode* Mode=Core();
    if (!Mode) { Error=TEXT("Dash sweeps require the current core."); return false; }
    const int64 NowMs=Mode->GetAuthorityNowMs();
    TArray<FLDCombatDashSweep> Due;
    for (int32 Index=DashSweeps.Num()-1; Index>=0; --Index)
    {
        if (DashSweeps[Index].EndAtMs>NowMs) { continue; }
        Due.Insert(MoveTemp(DashSweeps[Index]),0);
        DashSweeps.RemoveAt(Index);
    }
    for (const FLDCombatDashSweep& Sweep : Due)
    {
        ALDCharacter* Actor=nullptr;
        FLDBodyCombatState* Body=FindBody(Sweep.Body,&Actor);
        if (!Body || !Actor || Body->bPendingDeath) { continue; }
        const FVector End=FootOf(*Actor);
        const FVector2D A(Sweep.StartFootCm.X,Sweep.StartFootCm.Y), B(End.X,End.Y);
        TArray<FLDCombatBodyRef> Struck;
        for (const auto& Pair : Bodies)
        {
            const ALDCharacter* Other=Pair.Key.Get();
            const FLDBodyCombatState& State=Pair.Value;
            if (!Other || &State==Body || State.bPendingDeath
                || (State.Victim.Kind==ELDVictimKind::Contender && Mode->IsContenderProtected(State.Victim.EntityId)))
            { continue; }
            const FVector Foot=FootOf(*Other);
            const FVector2D P(Foot.X,Foot.Y);
            const FVector2D Segment=B-A;
            const double Length=Segment.SizeSquared();
            const double T=Length>UE_DOUBLE_SMALL_NUMBER ? FMath::Clamp(FVector2D::DotProduct(P-A,Segment)/Length,0.,1.) : 0.;
            if (FVector2D::Distance(P,A+Segment*T)<=Sweep.HalfWidthCm) { Struck.Add({State.Victim,State.Identity}); }
        }
        Struck.Sort([](const FLDCombatBodyRef& Left, const FLDCombatBodyRef& Right) { return Left.Victim.EntityId<Right.Victim.EntityId; });
        for (const FLDCombatBodyRef& Target : Struck)
        {
            FLDCombatHit& Hit=PendingHits.AddDefaulted_GetRef();
            Hit.HitId=FGuid::NewGuid();
            Hit.EffectId=Sweep.EffectId;
            Hit.Token=Sweep.Token;
            Hit.Target=Target;
            Hit.SourceBody=Sweep.Body;
            Hit.SourceEntity=Body->Victim.EntityId;
            Hit.CreditOwner=Body->Victim.Kind==ELDVictimKind::Contender ? Body->Victim.EntityId : FGuid();
            Hit.Source=Sweep.Source;
            Hit.HitClass=ELDCombatHitClass::Ability;
            Hit.SlowBp=Sweep.SlowBp;
            Hit.SlowDurationMs=Sweep.SlowMs;
            Hit.bAddedDamage=true;
        }
    }
    return true;
}

bool ALDCombatDirector::ResolveRetaliations(FString& Error)
{
    const ALDGameMode* Mode=Core();
    if (!Mode) { Error=TEXT("Retaliations require the current core."); return false; }
    const int64 NowMs=Mode->GetAuthorityNowMs();
    for (auto& Pair : Bodies)
    {
        FLDBodyCombatState& Body=Pair.Value;
        ALDCharacter* Actor=Pair.Key.Get();
        if (!Body.Retaliation.IsSet() || Body.Retaliation->EndAtMs>NowMs) { continue; }
        const FLDCombatRetaliation Answer=Body.Retaliation.GetValue();
        Body.Retaliation.Reset();
        if (!Actor || Body.bPendingDeath) { continue; }
        // base + per attack (counted up to its cap) + armor share of the caster now.
        const int64 Armor=Body.AttributeProjection.Final.ArmorP2;
        const int64 Counted=FMath::Min(Answer.HitsTaken,Answer.MaxHits);
        if (Answer.BaseP2<0 || Answer.PerHitP2<0 || Answer.ArmorRatioBp<0 || Answer.PerHitP2>MAX_int32 || Answer.BaseP2>MAX_int32)
        { Error=TEXT("Retaliation scaling is outside the checked numeric domain."); return false; }
        const int64 BaseP2=Answer.BaseP2+Answer.PerHitP2*Counted+Armor/10000*Answer.ArmorRatioBp+Armor%10000*Answer.ArmorRatioBp/10000;
        TArray<FLDCombatBodyRef> Targets;
        if (!CollectAreaTargets(FootOf(*Actor),Answer.RadiusCm,&Body,Body.Victim.EntityId,Targets,Error)) { return false; }
        for (const FLDCombatBodyRef& Target : Targets)
        {
            FLDCombatHit& Hit=PendingHits.AddDefaulted_GetRef();
            Hit.HitId=FGuid::NewGuid();
            Hit.EffectId=Answer.EffectId;
            Hit.Target=Target;
            Hit.SourceBody={Body.Victim,Body.Identity};
            Hit.SourceEntity=Body.Victim.EntityId;
            Hit.CreditOwner=Body.Victim.Kind==ELDVictimKind::Contender ? Body.Victim.EntityId : FGuid();
            Hit.Source=Answer.Source;
            Hit.HitClass=ELDCombatHitClass::Ability;
            Hit.BaseP2=BaseP2;
            Hit.DamageType=Answer.DamageType;
            Hit.SourceADP2=Body.AttributeProjection.Final.ADP2;
            Hit.SourceAPP2=Body.AttributeProjection.Final.APP2;
            Hit.bAddedDamage=true;
            if (!StampSourceTerms(Body,Hit,Error)) { return false; }
        }
        ++Body.StateRevision;
        MarkViewChanged(Body.Victim.EntityId);
    }
    return true;
}

void ALDCombatDirector::AdvanceMomentum(int64 NowMs)
{
    for (auto& Pair : Bodies)
    {
        FLDBodyCombatState& Body=Pair.Value;
        ALDCharacter* Actor=Pair.Key.Get();
        if (!Body.Momentum.IsSet()) { continue; }
        if (!Actor || Body.bPendingDeath || NowMs>=Body.Momentum->ConsumeUntilMs)
        {
            Body.Momentum.Reset();
            ++Body.StateRevision;
            MarkViewChanged(Body.Victim.EntityId);
            continue;
        }
        const FVector Foot=FootOf(*Actor);
        if (Body.Momentum->LastFootCm!=Foot && Body.Momentum->AccumulateUntilMs>=NowMs)
        {
            Body.Momentum->DistanceCm+=FVector::Dist2D(Body.Momentum->LastFootCm,Foot);
        }
        Body.Momentum->LastFootCm=Foot;
    }
}

bool ALDCombatDirector::IsSourceBehind(const FLDCombatBodyRef& Source, const ALDCharacter& Target) const
{
    const FLDDemoContentSnapshot* Content=Catalog();
    ALDCharacter* SourceActor=nullptr;
    const FLDBodyCombatState* SourceBody=FindBody(Source,&SourceActor);
    if (!Content || !SourceBody || !SourceActor || SourceActor==&Target) { return false; }
    return LDCombatControl::IsBehind(FootOf(Target),Target.GetActorRotation().Yaw,FootOf(*SourceActor),
        Content->Get().Parameters.RearArcDeg);
}

bool ALDCombatDirector::QueueThorns(const FLDCombatHit& Hit, ALDCharacter& Holder, FLDBodyCombatState& Body, int64 DamageP2,
    bool bCritical, FString& Error)
{
    const FLDDemoContentSnapshot* Content=Catalog();
    const ALDGameMode* Mode=Core();
    if (!Content || !Mode) { Error=TEXT("Thorns require the current core and the approved Demo content."); return false; }
    const FLDAttributeProjection& Projection=Body.AttributeProjection;
    if (Projection.LearnedTriggers.IsEmpty() && Projection.EquipmentPassives.IsEmpty()) { return true; }
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
    Contact.bFromBehind=LDCombatControl::IsBehind(OwnerFoot,Holder.GetActorRotation().Yaw,AttackerFoot,Content->Get().Parameters.RearArcDeg);
    const FLDCombatBodyRef OwnerRef{Body.Victim,Body.Identity};
    for (const FLDPassiveEffectRef& Source : Sources)
    {
        if (!LDCombatControl::ThornsTriggers(*Source.Effect,Contact,Body.ThornsMarks,Source.Key,Hit.SourceBody.Victim.EntityId,NowMs)) { continue; }
        int64 BaseP2=0;
        // Armor and max-HP coefficients of the holder now (passive_iron_crotch, item_nail_uniform).
        if (!LDCombatControl::ThornsBaseP2(*Source.Effect,Projection.Final.ArmorP2,Body.Resources.MaxHpP2,bCritical,BaseP2,Error)) { return false; }
        LDCombatControl::MarkThorns(Body.ThornsMarks,Source.Key,Hit.SourceBody.Victim.EntityId,NowMs,Source.Effect->IntervalMs);
        // The same hit, damage, ownership, death and reward path as any other hit; it is flagged so it never crits,
        // heals, cleaves or triggers the attacker's own thorns.
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
        // passive_iron_crotch Lv3: the reflection cuts the attacker's attack and move speed for a while.
        Reflect.SlowBp=Reflect.SlowAttackBp=Source.Effect->RatioBp;
        Reflect.SlowDurationMs=Source.Effect->DurationMs;
        if (!StampSourceTerms(Body,Reflect,Error)) { return false; }
        Reflect.OmnivampBp=0;
    }
    Error.Reset();
    return true;
}

bool ALDCombatDirector::ResolveLandings(FString& Error)
{
    if (Landings.IsEmpty()) { return true; }
    const ALDGameMode* Mode=Core();
    if (!Mode) { Error=TEXT("Jump landings require the current core."); return false; }
    const int64 NowMs=Mode->GetAuthorityNowMs();
    TArray<FLDCombatLanding> Due;
    for (int32 Index=Landings.Num()-1; Index>=0; --Index)
    {
        if (Landings[Index].LandAtMs>NowMs) { continue; }
        Due.Insert(MoveTemp(Landings[Index]),0);
        Landings.RemoveAt(Index);
    }
    for (const FLDCombatLanding& Landing : Due)
    {
        // Only the same, still living body life lands; a death or a replaced life drops the landing.
        ALDCharacter* Actor=nullptr;
        FLDBodyCombatState* Body=FindBody(Landing.Body,&Actor);
        if (!Body || !Actor || Body->bPendingDeath) { continue; }
        for (const FLDDemoEffectDefinition& Effect : Landing.Effects)
        {
            if (!ApplySelfEffect(*Body,Effect,Landing.EffectId,NowMs,Error))
            {
                Error=FString::Printf(TEXT("Jump landing %s: %s"),*Landing.EffectId.ToString(),*Error);
                return false;
            }
        }
        if (Landing.HitTemplate.IsSet())
        {
            // The area is named now, around the landing foot, by the same rule as a self area at release.
            TArray<FLDCombatBodyRef> Targets;
            if (!CollectAreaTargets(FootOf(*Actor),Landing.AreaRadiusCm,Body,Body->Victim.EntityId,Targets,Error))
            {
                Error=FString::Printf(TEXT("Jump landing %s: %s"),*Landing.EffectId.ToString(),*Error);
                return false;
            }
            for (const FLDCombatBodyRef& Target : Targets)
            {
                FLDCombatHit Hit=Landing.HitTemplate.GetValue();
                Hit.HitId=FGuid::NewGuid();
                Hit.Target=Target;
                PendingHits.Add(Hit);
                // The ambush bonus is a separate instance of its own damage type against the same struck body.
                if (Landing.Ambush.IsSet()) { PendingHits.Add(MakeAmbushHit(Hit,Landing.Ambush.GetValue())); }
            }
            // active_body_slam Lv3: one shield sized by every body this landing struck.
            if (Landing.LandingShield.IsSet() && !Targets.IsEmpty())
            {
                const FLDDemoEffectDefinition& Effect=Landing.LandingShield.GetValue();
                const int64 MaxHP=Body->Resources.MaxHpP2;
                const int64 Each=Effect.BaseP2+MaxHP/10000*Effect.MaxHPRatioBp+MaxHP%10000*Effect.MaxHPRatioBp/10000;
                if (Each>MAX_int64/FMath::Max(1,Targets.Num()))
                { Error=TEXT("Landing shield overflowed."); return false; }
                if (!GrantShield(*Body,Each*Targets.Num(),Effect.DurationMs,Landing.EffectId,NowMs,Error)) { return false; }
            }
        }
        ++Body->StateRevision;
        MarkViewChanged(Body->Victim.EntityId);
    }
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
            // The ambush bonus is a separate instance of its own damage type against the same struck body.
            if (Projectile.Ambush.IsSet()) { PendingHits.Add(MakeAmbushHit(Contact,Projectile.Ambush.GetValue())); }
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
    const FLDTemporaryAttributeTerms Terms=LDTactical::TemporaryTerms(Body.TimedBonuses,Body.Slows,
        Body.BuildProjection.LowHPBonus,Body.Resources.HpP2,Body.Resources.MaxHpP2,NowMs);
    const bool bAmbushReady=AmbushReadyNow(Body,NowMs);
    if (bAmbushReady!=Body.bAmbushReadyShown)
    {
        Body.bAmbushReadyShown=bAmbushReady;
        MarkViewChanged(Body.Victim.EntityId);
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
        if (LDTactical::ExpireTemporaryStatuses(Body.Slows,Body.TimedBonuses,Body.Breaches,Body.VampMarks,NowMs))
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
        const bool bControls=LDCombatControl::ExpireDisarmsAndStuns(Body.Disarms,Body.Stuns,NowMs);
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
            Pools,Error,Body.RegenSurgeUntilMs,bInDorm ? DormRecovery : nullptr))
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
        Basis.LeaderBountyGoldAtDeath=Mode->LeaderBountyFor(Body.Victim.EntityId);
    }
    else { Basis.RewardDefinitionId=Body.MonsterId; }
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
