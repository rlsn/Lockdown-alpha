#include "Combat/LDCombatDirector.h"
#include "Combat/LDTacticalRules.h"
#include "Character/LDCharacter.h"
#include "Components/CapsuleComponent.h"
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

    struct FCleaveTarget
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
    Evaluation.Definition.Emplace(Hit.BaseP2,Hit.ADRatioBp,Hit.APRatioBp,Hit.DamageType);
    Evaluation.HitClass=Hit.HitClass;
    Evaluation.bCritical=Hit.bCritical;
    Evaluation.SourceADP2=Hit.SourceADP2;
    Evaluation.SourceAPP2=Hit.SourceAPP2;
    Evaluation.TargetResistanceP2=Hit.DamageType==ELDDemoDamageType::Physical ? Defence.ArmorP2 : Defence.MagicResistP2;
    Evaluation.TargetHPP2=Body->Resources.HpP2;
    Evaluation.TargetShieldP2=Body->Resources.ShieldP2;
    Evaluation.bTargetProtected=bProtected;
    const FLDDamageEvaluationResult Evaluated=LDTactical::EvaluateDamage(Evaluation,*Content);
    if (!Evaluated.IsSuccess())
    {
        Error=FString::Printf(TEXT("Hit %s could not be evaluated: %s: %s"),
            *Hit.HitId.ToString(),*Evaluated.Error.Field,*Evaluated.Error.Message);
        return false;
    }
    const FLDDamageEvaluation& Damage=Evaluated.Value.GetValue();

    // Every fallible part of the hit is resolved before its first write.
    FLDCombatRoot RootInstance;
    const bool bRoots=Hit.RootDurationMs>0 && !bProtected && Damage.RemainingHPP2>0;
    if (bRoots)
    {
        RootInstance.EffectId=Hit.EffectId;
        RootInstance.SourceEntity=Hit.SourceEntity;
        if (!LDMatchLifecycle::AddDurationMs(NowMs,Hit.RootDurationMs,RootInstance.ExpiresAtMs,Error)) { return false; }
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
    TArray<LDCombatEffectsDetail::FCleaveTarget> Cleaved;
    if (Hit.bCleave && Hit.HitClass==ELDCombatHitClass::BasicPrimary && Damage.DamageP2>0)
    {
        const UWorld* CurrentWorld=GetWorld();
        const ULDMapRegistry* Registry=CurrentWorld ? CurrentWorld->GetSubsystem<ULDMapRegistry>() : nullptr;
        if (!Mode->Vision || !Registry || Hit.CleaveRatioBp<0 || !FMath::IsFinite(Hit.CleaveRadiusCm) || Hit.CleaveRadiusCm<0)
        {
            Error=TEXT("Cleave requires the observation owner, the registered world and a valid radius.");
            return false;
        }
        // The anchor is the primary target's foot at this hit, never re-read after that target dies.
        const FVector Anchor=FootOf(*Actor);
        const double ReachSquared=Hit.CleaveRadiusCm*Hit.CleaveRadiusCm;
        TArray<LDCombatEffectsDetail::FCleaveTarget> InReach;
        for (const auto& Pair : Bodies)
        {
            const ALDCharacter* Other=Pair.Key.Get();
            const FLDBodyCombatState& State=Pair.Value;
            if (!Other || &State==Body || State.bPendingDeath || State.Victim.EntityId==Hit.SourceEntity
                || (State.Victim.Kind==ELDVictimKind::Contender && Mode->IsContenderProtected(State.Victim.EntityId)))
            { continue; }
            const FVector OtherFoot=FootOf(*Other);
            if (FVector::DistSquared2D(Anchor,OtherFoot)>ReachSquared) { continue; }
            LDCombatEffectsDetail::FCleaveTarget& Candidate=InReach.AddDefaulted_GetRef();
            Candidate.Body={State.Victim,State.Identity};
            Candidate.FootCm=OtherFoot;
        }
        InReach.Sort([](const LDCombatEffectsDetail::FCleaveTarget& Left, const LDCombatEffectsDetail::FCleaveTarget& Right)
        { return Left.Body.Victim.EntityId<Right.Body.Victim.EntityId; });
        for (const LDCombatEffectsDetail::FCleaveTarget& Candidate : InReach)
        {
            const ELDSightResult Sight=Mode->Vision->TraceSight(Anchor,Candidate.FootCm,Registry->Revision());
            if (Sight==ELDSightResult::Visible) { Cleaved.Add(Candidate); }
            else if (Sight!=ELDSightResult::Blocked && Sight!=ELDSightResult::OutOfRange)
            {
                Error=TEXT("Cleave sight failed on a registered body.");
                return false;
            }
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
    // A hit that harmed or rooted the body is its visible Hit cue (LDCombatCues).
    if (Damage.DamageP2>0 || bRoots)
    {
        Body->Cues.LastHitAtMs=NowMs;
        MarkViewChanged(Body->Victim.EntityId);
    }
    // Camp retaliation consumes the committed hostile damage or control; misses and zero contact never aggro.
    if ((Damage.DamageP2>0 || bRoots) && Hit.Target.Victim.Kind==ELDVictimKind::CampMember)
    {
        FLDCombatHostileContact& Contact=PendingContacts.AddDefaulted_GetRef();
        Contact.HitId=Hit.HitId;
        Contact.VictimBody=Hit.Target;
        Contact.SourceBody=Hit.SourceBody;
        Contact.CreditOwner=Hit.CreditOwner;
        Contact.OccurredAtSeconds=Mode->GetAuthorityNowSeconds();
    }
    if (bRoots && LDTactical::ApplyRoot(Body->Roots,RootInstance,NowMs)) { ++Body->StateRevision; }
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
    for (const LDCombatEffectsDetail::FCleaveTarget& Secondary : Cleaved)
    {
        FLDCombatHit& Sweep=PendingHits.AddDefaulted_GetRef();
        Sweep.HitId=FGuid::NewGuid();
        Sweep.EffectId=Hit.EffectId;
        Sweep.Token=Hit.Token;
        Sweep.Target=Secondary.Body;
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
    if (Body->Resources.HpP2==0 && !LockDeath(*Actor,*Body,Hit,Error)) { return false; }
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
            Contact.bCritical=false;
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

void ALDCombatDirector::ExpireStatuses(int64 NowMs)
{
    for (auto& Pair : Bodies)
    {
        FLDBodyCombatState& Body=Pair.Value;
        const FLDStatusExpiry Expired=LDTactical::ExpireTimedStatuses(Body.Roots,Body.Shield,Body.Resources.ShieldP2,NowMs);
        if (!Expired.bRootsChanged && !Expired.bShieldChanged) { continue; }
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
    return RegenerateBodies(Recovery,NowMs,Error);
}

bool ALDCombatDirector::RegenerateBodies(const LDTactical::FOutOfCombatRecovery& Recovery, int64 NowMs, FString& Error)
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
        if (!LDTactical::SettleRegen(Body.AttributeProjection,bContender ? &Recovery : nullptr,Body.LastCombatAtMs,FromMs,NowMs,
            Pools,Error))
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
