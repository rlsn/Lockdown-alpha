#include "Combat/LDCombatDirector.h"
#include "Combat/LDTacticalRules.h"
#include "Combat/LDControlRules.h"
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
    const FLDProjectedAttributes& Defence=Body->AttributeProjection.Final;
    FLDDamageEvaluationInput Evaluation;
    Evaluation.Definition.Emplace(Hit.BaseP2,Hit.ADRatioBp,Hit.APRatioBp,Hit.DamageType,Hit.MaxHPRatioBp);
    Evaluation.HitClass=Hit.HitClass;
    Evaluation.bCritical=Hit.bCritical;
    Evaluation.SourceADP2=Hit.SourceADP2;
    Evaluation.SourceAPP2=Hit.SourceAPP2;
    if (Hit.MaxHPRatioBp!=0) { Evaluation.SourceMaxHPP2=Hit.SourceMaxHPP2; }
    Evaluation.TargetResistanceP2=Hit.DamageType==ELDDemoDamageType::Physical ? Defence.ArmorP2 : Defence.MagicResistP2;
    Evaluation.TargetHPP2=Body->Resources.HpP2;
    Evaluation.TargetShieldP2=Body->Resources.ShieldP2;
    Evaluation.bTargetProtected=bProtected;
    // Rear strike: the releasing body in the target's rear arc at this hit, by the target's authority facing.
    const bool bBehind=(Hit.BackstabRatioBp>0 || Hit.BackstabStunMs>0) && IsSourceBehind(Hit.SourceBody,*Actor);
    // The source's control damage amplification on this life multiplies with a rear strike; both act before resistance.
    const int64 ExposureBp=LDTactical::ExposureBp(Body->Exposures,Hit.SourceEntity,NowMs);
    Evaluation.AmplifyBp=(10000+ExposureBp)*(bBehind ? 10000+Hit.BackstabRatioBp : 10000)/10000;
    const FLDDamageEvaluationResult Evaluated=LDTactical::EvaluateDamage(Evaluation,*Content);
    if (!Evaluated.IsSuccess())
    {
        Error=FString::Printf(TEXT("Hit %s could not be evaluated: %s: %s"),
            *Hit.HitId.ToString(),*Evaluated.Error.Field,*Evaluated.Error.Message);
        return false;
    }
    const FLDDamageEvaluation& Damage=Evaluated.Value.GetValue();

    // Every fallible part of the hit is resolved before its first write.
    FLDCombatRoot RootInstance, DisarmInstance, StunInstance;
    const bool bControllable=!bProtected && Damage.RemainingHPP2>0;
    const int64 StunMs=FMath::Max(Hit.StunDurationMs,bBehind ? Hit.BackstabStunMs : 0);
    const bool bRoots=Hit.RootDurationMs>0 && bControllable;
    const bool bDisarms=Hit.DisarmDurationMs>0 && bControllable;
    const bool bStuns=StunMs>0 && bControllable;
    const auto Prepare=[&Hit,NowMs,&Error](bool bApplies, int64 DurationMs, FLDCombatRoot& Instance)
    {
        if (!bApplies) { return true; }
        Instance.EffectId=Hit.EffectId;
        Instance.SourceEntity=Hit.SourceEntity;
        return LDMatchLifecycle::AddDurationMs(NowMs,DurationMs,Instance.ExpiresAtMs,Error);
    };
    if (!Prepare(bRoots,Hit.RootDurationMs,RootInstance) || !Prepare(bDisarms,Hit.DisarmDurationMs,DisarmInstance)
        || !Prepare(bStuns,StunMs,StunInstance))
    { return false; }
    const bool bControls=bRoots || bDisarms || bStuns;
    FLDCombatSlow SlowInstance;
    const bool bSlows=Hit.SlowDurationMs>0 && Hit.SlowBp>0 && bControllable;
    if (bSlows)
    {
        SlowInstance.EffectId=Hit.EffectId;
        SlowInstance.SourceEntity=Hit.SourceEntity;
        SlowInstance.RatioBp=Hit.SlowBp;
        if (!LDMatchLifecycle::AddDurationMs(NowMs,Hit.SlowDurationMs,SlowInstance.ExpiresAtMs,Error)) { return false; }
    }
    FLDBodyCombatState* Healed=nullptr;
    int64 HealP2=0;
    if (Hit.bLifesteal && Hit.HitClass==ELDCombatHitClass::BasicPrimary && Damage.HPLostP2>0)
    {
        if (Hit.LifestealRatioBp<0 || (Hit.LifestealRatioBp>0 && Damage.HPLostP2>MAX_int64/Hit.LifestealRatioBp))
        {
            Error=TEXT("Lifesteal scaling is outside the checked numeric domain.");
            return false;
        }
        HealP2=Damage.HPLostP2*Hit.LifestealRatioBp/10000;
        // Only the exact releasing life heals; a lethal, dead or respawned body never does.
        Healed=FindBody(Hit.SourceBody);
        if (Healed && (Healed==Body || Healed->bPendingDeath || Healed->Resources.HpP2<=0)) { Healed=nullptr; }
    }
    TArray<FLDCombatBodyRef> Cleaved;
    if (Hit.bCleave && Hit.HitClass==ELDCombatHitClass::BasicPrimary && Damage.DamageP2>0)
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

    Body->Resources.HpP2=Damage.RemainingHPP2;
    Body->Resources.ShieldP2=Damage.RemainingShieldP2;
    ++Body->Resources.Revision;
    // A barrier emptied by this hit ends by the same timed-status rule as an expired one.
    const FLDStatusExpiry Ended=LDTactical::ExpireTimedStatuses(Body->Roots,Body->Shield,Body->Resources.ShieldP2,NowMs);
    if (Ended.bRootsChanged || Ended.bShieldChanged) { ++Body->StateRevision; }
    // A positive mitigated hit is combat even when the barrier absorbed all of it.
    if (Damage.DamageP2>0) { Body->LastCombatAtMs=NowMs; }
    // A hit that harmed, controlled or slowed the body is its visible Hit cue (LDCombatCues).
    if (Damage.DamageP2>0 || bControls || bSlows)
    {
        Body->Cues.LastHitAtMs=NowMs;
        MarkViewChanged(Body->Victim.EntityId);
        // Being hurt (absorbed damage included) or controlled reveals a stealthed body.
        RevealStealth(*Body);
    }
    // Camp retaliation consumes the committed hostile damage or control; misses and zero contact never aggro.
    if ((Damage.DamageP2>0 || bControls || bSlows) && Hit.Target.Victim.Kind==ELDVictimKind::CampMember)
    {
        FLDCombatHostileContact& Contact=PendingContacts.AddDefaulted_GetRef();
        Contact.HitId=Hit.HitId;
        Contact.VictimBody=Hit.Target;
        Contact.SourceBody=Hit.SourceBody;
        Contact.CreditOwner=Hit.CreditOwner;
        Contact.OccurredAtSeconds=Mode->GetAuthorityNowSeconds();
    }
    if (bRoots && LDTactical::ApplyRoot(Body->Roots,RootInstance,NowMs)) { ++Body->StateRevision; }
    if (bSlows && LDTactical::ApplySlow(Body->Slows,SlowInstance,NowMs)) { ++Body->StateRevision; }
    if (bDisarms && LDCombatControl::Apply(Body->Disarms,DisarmInstance,NowMs))
    {
        ++Body->StateRevision;
        // A disarmed body cannot finish a basic windup.
        if (Body->Action.IsSet() && Body->Action->Kind==ELDCombatActionKind::Basic) { AbandonAction(*Body,ELDCombatReject::ControlBlocked); }
    }
    // A stun interrupts any unreleased windup; queued and later requests are refused while it lasts.
    if (bStuns && LDCombatControl::Apply(Body->Stuns,StunInstance,NowMs))
    {
        ++Body->StateRevision;
        AbandonAction(*Body,ELDCombatReject::ControlBlocked);
    }
    // A Root or Stun this source just applied opens its control damage amplification on this life (社死现场).
    if ((bRoots || bStuns) && !MarkControlled(*Body,Hit,NowMs,Error)) { return false; }
    MarkViewChanged(Hit.Target.Victim.EntityId);
    if (Healed && HealP2>0)
    {
        auto& Pool=Healed->Resources;
        const int64 Before=Pool.HpP2;
        Pool.HpP2=HealP2>=Pool.MaxHpP2-Pool.HpP2 ? Pool.MaxHpP2 : Pool.HpP2+HealP2;
        if (Pool.HpP2>=Pool.MaxHpP2) { Healed->HpRegenMicroP2=0; }
        if (Pool.HpP2!=Before)
        {
            ++Pool.Revision;
            MarkViewChanged(Hit.SourceBody.Victim.EntityId);
        }
    }
    // POLICY.BasicSecondaryTriggers=Disabled: secondaries never crit, heal, cleave or start another attack.
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
        Sweep.BaseP2=0;
        Sweep.ADRatioBp=Hit.CleaveRatioBp;
        Sweep.APRatioBp=0;
        Sweep.DamageType=ELDDemoDamageType::Physical;
        Sweep.SourceADP2=Hit.SourceADP2;
        Sweep.SourceAPP2=Hit.SourceAPP2;
    }
    // Thorns answer a hit the owner survived; a reflection never answers another reflection.
    if (!Hit.bReflect && Damage.DamageP2>0 && Body->Resources.HpP2>0 && !QueueThorns(Hit,*Actor,*Body,Damage.DamageP2,Error)) { return false; }
    if (Body->Resources.HpP2==0 && !LockDeath(*Actor,*Body,Hit,Error)) { return false; }
    return true;
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

bool ALDCombatDirector::QueueThorns(const FLDCombatHit& Hit, ALDCharacter& Holder, FLDBodyCombatState& Body, int64 DamageP2, FString& Error)
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
    struct FThornsSource
    {
        FString Key;
        FLDCombatSourceRef Ref;
        const FLDDemoEffectDefinition* Effect=nullptr;
    };
    TArray<FThornsSource> Sources;
    for (const FLDLearnedTriggerTerm& Term : Projection.LearnedTriggers)
    {
        const FLDDemoAbilityDefinition* Ability=nullptr;
        if (!Content->FindAbility(Term.AbilityId,Ability,Error)) { return false; }
        const FLDDemoRankDefinition* Rank=Ability->Ranks.FindByPredicate([&Term](const FLDDemoRankDefinition& Row) { return Row.Rank==int32(Term.Rank); });
        if (!Rank) { Error=FString::Printf(TEXT("Learned trigger %s has no rank %u."),*Term.AbilityId,uint32(Term.Rank)); return false; }
        for (const FLDDemoEffectDefinition& Effect : Rank->Effects)
        {
            if (Effect.GetOp()!=ELDDemoEffectOp::Thorns) { continue; }
            FThornsSource& Source=Sources.AddDefaulted_GetRef();
            Source.Key=Term.AbilityId;
            Source.Ref.Kind=ELDCombatSourceKind::Learned;
            Source.Ref.DefinitionId=FName(*Term.AbilityId);
            Source.Effect=&Effect;
        }
    }
    // POLICY.DuplicateEquipmentPassive=OncePerFamily: two copies still reflect once per family.
    for (const FLDEquipmentPassiveFamily& Family : Projection.EquipmentPassives)
    {
        const FLDDemoEquipmentDefinition* Equipment=nullptr;
        if (!Content->FindEquipment(Family.EquipmentId,Equipment,Error)) { return false; }
        for (const FLDDemoEffectDefinition& Effect : Equipment->Passive.Effects)
        {
            if (Effect.GetOp()!=ELDDemoEffectOp::Thorns) { continue; }
            FThornsSource& Source=Sources.AddDefaulted_GetRef();
            Source.Key=Family.Family;
            Source.Ref.Kind=ELDCombatSourceKind::Gear;
            Source.Ref.DefinitionId=FName(*Family.EquipmentId);
            Source.Effect=&Effect;
        }
    }
    if (Sources.IsEmpty()) { return true; }
    const int64 NowMs=Mode->GetAuthorityNowMs();
    const FVector OwnerFoot=FootOf(Holder), AttackerFoot=FootOf(*Attacker);
    FLDThornsContact Contact;
    Contact.HitClass=Hit.HitClass;
    Contact.bReflected=Hit.bReflect;
    Contact.bCritical=Hit.bCritical;
    Contact.DamageP2=DamageP2;
    Contact.DistanceCm=FVector::Dist2D(OwnerFoot,AttackerFoot);
    Contact.bFromBehind=LDCombatControl::IsBehind(OwnerFoot,Holder.GetActorRotation().Yaw,AttackerFoot,Content->Get().Parameters.RearArcDeg);
    const FLDCombatBodyRef OwnerRef{Body.Victim,Body.Identity};
    for (const FThornsSource& Source : Sources)
    {
        if (!LDCombatControl::ThornsTriggers(*Source.Effect,Contact,Body.ThornsMarks,Source.Key,Hit.SourceBody.Victim.EntityId,NowMs)) { continue; }
        int64 BaseP2=0;
        if (!LDCombatControl::ThornsBaseP2(*Source.Effect,Projection.Final.ArmorP2,Hit.bCritical,BaseP2,Error)) { return false; }
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
        Reflect.Source=Source.Ref;
        Reflect.HitClass=ELDCombatHitClass::Ability;
        Reflect.BaseP2=BaseP2;
        Reflect.DamageType=Source.Effect->DamageType;
        Reflect.SourceADP2=Projection.Final.ADP2;
        Reflect.SourceAPP2=Projection.Final.APP2;
        Reflect.bReflect=true;
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
                if (Landing.Ambush.IsSet())
                {
                    // The ambush bonus is a separate instance of its own damage type against the same struck body.
                    FLDCombatHit Ambush=Hit;
                    Ambush.HitId=FGuid::NewGuid();
                    Ambush.BaseP2=Landing.Ambush->BaseP2;
                    Ambush.ADRatioBp=Landing.Ambush->ADRatioBp;
                    Ambush.APRatioBp=Landing.Ambush->APRatioBp;
                    Ambush.DamageType=Landing.Ambush->DamageType;
                    Ambush.MaxHPRatioBp=0;
                    Ambush.bCritical=false;
                    Ambush.RootDurationMs=Ambush.SlowBp=Ambush.SlowDurationMs=Ambush.ControlAmpBp=Ambush.ControlAmpDurationMs=0;
                    Ambush.DisarmDurationMs=Ambush.StunDurationMs=Ambush.BackstabRatioBp=Ambush.BackstabStunMs=0;
                    PendingHits.Add(Ambush);
                }
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
            // The first body ends the projectile; a protected body still stops it and evaluates to zero.
            FLDCombatHit& Contact=PendingHits.AddDefaulted_GetRef();
            Contact.HitId=FGuid::NewGuid();
            Contact.EffectId=Projectile.EffectId;
            Contact.Token=Projectile.Token;
            Contact.Target=Targets[Struck].Body;
            Contact.SourceBody=Projectile.SourceBody;
            Contact.SourceEntity=Projectile.SourceEntity;
            Contact.CreditOwner=Projectile.CreditOwner;
            Contact.Source=Projectile.Source;
            Contact.HitClass=ELDCombatHitClass::Ability;
            Contact.BaseP2=Projectile.BaseP2;
            Contact.ADRatioBp=Projectile.ADRatioBp;
            Contact.APRatioBp=Projectile.APRatioBp;
            Contact.DamageType=Projectile.DamageType;
            Contact.SourceADP2=Projectile.SourceADP2;
            Contact.SourceAPP2=Projectile.SourceAPP2;
            Contact.MaxHPRatioBp=Projectile.MaxHPRatioBp;
            Contact.SourceMaxHPP2=Projectile.SourceMaxHPP2;
            Contact.bCritical=false;
            Contact.SlowBp=Projectile.SlowBp;
            Contact.SlowDurationMs=Projectile.SlowDurationMs;
            Contact.ControlAmpBp=Projectile.ControlAmpBp;
            Contact.ControlAmpDurationMs=Projectile.ControlAmpDurationMs;
            if (Projectile.Ambush.IsSet())
            {
                // The ambush bonus is a separate instance of its own damage type against the same struck body.
                FLDCombatHit Ambush=Contact;
                Ambush.HitId=FGuid::NewGuid();
                Ambush.BaseP2=Projectile.Ambush->BaseP2;
                Ambush.ADRatioBp=Projectile.Ambush->ADRatioBp;
                Ambush.APRatioBp=Projectile.Ambush->APRatioBp;
                Ambush.DamageType=Projectile.Ambush->DamageType;
                Ambush.MaxHPRatioBp=0;
                Ambush.SlowBp=Ambush.SlowDurationMs=Ambush.ControlAmpBp=Ambush.ControlAmpDurationMs=0;
                PendingHits.Add(Ambush);
            }
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

bool ALDCombatDirector::MarkControlled(FLDBodyCombatState& Target, const FLDCombatHit& Hit, int64 NowMs, FString& Error)
{
    if (Hit.ControlAmpBp<=0 || Hit.ControlAmpDurationMs<=0 || !Hit.SourceEntity.IsValid()
        || Hit.SourceEntity==Target.Victim.EntityId)
    { return true; }
    FLDCombatExposure Exposure;
    Exposure.SourceEntity=Hit.SourceEntity;
    Exposure.AmplifyBp=Hit.ControlAmpBp;
    if (!LDMatchLifecycle::AddDurationMs(NowMs,Hit.ControlAmpDurationMs,Exposure.ExpiresAtMs,Error)) { return false; }
    LDTactical::ApplyExposure(Target.Exposures,Exposure);
    ++Target.StateRevision;
    MarkViewChanged(Target.Victim.EntityId);
    return true;
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
    const bool bAmbushReady=Body.BuildProjection.Ambush.IsSet() && !Body.bPendingDeath
        && LDTactical::AmbushReady(Body.LastCombatAtMs,Body.BuildProjection.Ambush->DelayMs,NowMs);
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
        if (LDTactical::ExpireTemporaryStatuses(Body.Slows,Body.TimedBonuses,Body.Exposures,NowMs))
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
        const FLDStatusExpiry Control=LDControl::ExpireControlStatuses(Body.Taunt,Body.Stealth,NowMs);
        const bool bControls=LDCombatControl::ExpireDisarmsAndStuns(Body.Disarms,Body.Stuns,NowMs);
        const bool bEmpower=Body.Empower.IsSet() && Body.Empower->ExpiresAtMs<=NowMs;
        if (bEmpower) { Body.Empower.Reset(); }
        if (!Expired.bRootsChanged && !Expired.bShieldChanged && !Control.bTauntChanged && !Control.bStealthChanged
            && !bControls && !bEmpower) { continue; }
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
        Out.Add({Body.Victim.EntityId,FootOf(*Actor)});
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
    // Effects run on an integral authority boundary, so the fatal instant is that exact millisecond.
    Death.OccurredAtSeconds=Mode->GetAuthorityNowSeconds();
    Death.OccurredAtMs=Mode->GetAuthorityNowMs();
    PendingDeaths.Add(MoveTemp(Death));
    Error.Reset();
    return true;
}
