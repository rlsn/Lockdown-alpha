#include "Combat/LDCombatDirector.h"

#include "Character/LDCharacter.h"
#include "Combat/LDCombatSourceRules.h"
#include "Combat/LDTacticalRules.h"
#include "Core/LDLog.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "Vision/LDViewTypes.h"

// Issue #345 tactical combat entities (stagger bleeds, slipper pickups, cordon lines, phantom decoys) and dynamic
// synergy state trackers (active_unplug parry miss refund, card_eagle_stance, card_skate_blade, card_hulking_presence).
namespace LDCombatSynergyDetail
{
    double Cm(const FLDDemoRational& Value)
    {
        return Value.Denominator > 0 ? double(Value.Numerator) / double(Value.Denominator) : 0.;
    }

    int64 Share(int64 Value, int64 Bp)
    {
        if (Value <= 0 || Bp <= 0) { return 0; }
        return Value / 10000 * Bp + Value % 10000 * Bp / 10000;
    }

    bool StandsOnAnyIce(const TArray<FLDGroundState>& Grounds, const FVector& FootCm, int64 NowMs)
    {
        for (const FLDGroundState& Ground : Grounds)
        {
            if (Ground.ExpiresAtMs <= NowMs || Ground.Kind != ELDGroundStateKind::Ice) { continue; }
            if (LDCombatElements::GroundContains(Ground, FootCm)) { return true; }
        }
        return false;
    }
}

double ALDCombatDirector::HulkingAreaScaleOf(const FLDBodyCombatState& Body) const
{
    const FLDDemoContentSnapshot* Content = Catalog();
    if (!Content) { return 1.; }
    TArray<FLDPassiveEffectRef> Hulks;
    FString Error;
    if (!CollectPassiveEffects(Body.AttributeProjection, ELDDemoEffectOp::HulkingPresence, Hulks, Error) || Hulks.IsEmpty())
    {
        return 1.;
    }
    const FLDDemoContentParameters& P = Content->Get().Parameters;
    const int64 LevelSteps = FMath::Max<int64>(0, int64(GrowthLevelOf(Body)) - 1);
    const int64 BaseMaxHPP2 = P.MaxHP.BaseP2 + LevelSteps * P.MaxHP.PerLevelP2;
    const int64 BonusHPP2 = FMath::Max<int64>(0, Body.Resources.MaxHpP2 - BaseMaxHPP2);
    if (BonusHPP2 <= 0) { return 1.; }
    int64 BonusBp = 0;
    for (const FLDPassiveEffectRef& Ref : Hulks)
    {
        if (Ref.Effect.LimitP2 > 0)
        {
            BonusBp += (BonusHPP2 / Ref.Effect.LimitP2) * Ref.Effect.RatioBp;
        }
    }
    return 1. + double(FMath::Min<int64>(BonusBp, 10000)) / 10000.;
}

int64 ALDCombatDirector::HulkingKnockbackKeepBp(const FLDBodyCombatState& Body) const
{
    const FLDDemoContentSnapshot* Content = Catalog();
    if (!Content) { return 10000; }
    TArray<FLDPassiveEffectRef> Hulks;
    FString Error;
    if (!CollectPassiveEffects(Body.AttributeProjection, ELDDemoEffectOp::HulkingPresence, Hulks, Error) || Hulks.IsEmpty())
    {
        return 10000;
    }
    const FLDDemoContentParameters& P = Content->Get().Parameters;
    const int64 LevelSteps = FMath::Max<int64>(0, int64(GrowthLevelOf(Body)) - 1);
    const int64 BaseMaxHPP2 = P.MaxHP.BaseP2 + LevelSteps * P.MaxHP.PerLevelP2;
    const int64 BonusHPP2 = FMath::Max<int64>(0, Body.Resources.MaxHpP2 - BaseMaxHPP2);
    if (BonusHPP2 <= 0) { return 10000; }
    int64 CutBp = 0;
    for (const FLDPassiveEffectRef& Ref : Hulks)
    {
        if (Ref.Effect.LimitP2 > 0)
        {
            CutBp += (BonusHPP2 / Ref.Effect.LimitP2) * Ref.Effect.SecondaryRatioBp;
        }
    }
    return FMath::Clamp<int64>(10000 - CutBp, 2000, 10000);
}

bool ALDCombatDirector::IsEagleStanceActive(const FLDBodyCombatState& Body, int64 NowMs) const
{
    TArray<FLDPassiveEffectRef> Eagles;
    FString Error;
    if (!CollectPassiveEffects(Body.BuildProjection, ELDDemoEffectOp::EagleStance, Eagles, Error) || Eagles.IsEmpty())
    {
        return false;
    }
    if (NowMs < Body.EagleLingerUntilMs) { return true; }
    const int64 DelayMs = Eagles[0].Effect.DelayMs;
    return Body.bHasLastStillFoot && DelayMs > 0 && NowMs - Body.StillSinceMs >= DelayMs;
}

bool ALDCombatDirector::AdvanceStaggerBleeds(int64 NowMs, FString& Error)
{
    for (auto& Pair : Bodies)
    {
        FLDBodyCombatState& Body = Pair.Value;
        if (!Pair.Key.IsValid() || Body.bPendingDeath || Body.StaggerBleeds.IsEmpty()) { continue; }
        for (int32 Idx = Body.StaggerBleeds.Num() - 1; Idx >= 0; --Idx)
        {
            FLDBodyCombatState::FStaggerBleed& Bleed = Body.StaggerBleeds[Idx];
            while (Bleed.TicksLeft > 0 && Bleed.RemainingP2 > 0 && Bleed.NextTickMs <= NowMs)
            {
                const int64 TickP2 = FMath::Max<int64>(1, Bleed.RemainingP2 / Bleed.TicksLeft);
                Bleed.RemainingP2 = FMath::Max<int64>(0, Bleed.RemainingP2 - TickP2);
                --Bleed.TicksLeft;
                if (Bleed.TicksLeft > 0 && !LDMatchLifecycle::AddDurationMs(Bleed.NextTickMs, Bleed.IntervalMs, Bleed.NextTickMs, Error))
                {
                    return false;
                }
                FLDCombatHit TickHit;
                TickHit.HitId = FGuid::NewGuid();
                TickHit.EffectId = FGuid::NewGuid();
                TickHit.Target = {Body.Victim, Body.Identity};
                TickHit.SourceBody = Bleed.SourceBody;
                TickHit.SourceEntity = Bleed.SourceEntity;
                TickHit.CreditOwner = Bleed.CreditOwner;
                TickHit.Source = Bleed.Source;
                TickHit.HitClass = ELDCombatHitClass::Ability;
                TickHit.BaseP2 = TickP2;
                TickHit.DamageType = ELDDemoDamageType::Physical;
                TickHit.bStatusSettle = true;
                TickHit.bPeriodic = true;
                TickHit.bSilent = true;
                TickHit.bStaggerBleed = true;
                PendingHits.Add(MoveTemp(TickHit));
            }
            if (Bleed.TicksLeft <= 0 || Bleed.RemainingP2 <= 0)
            {
                Body.StaggerBleeds.RemoveAt(Idx);
            }
        }
    }
    return true;
}

bool ALDCombatDirector::AdvanceSlipperDrops(int64 NowMs, FString& Error)
{
    using namespace LDCombatSynergyDetail;
    for (int32 Idx = SlipperDrops.Num() - 1; Idx >= 0; --Idx)
    {
        const FLDCombatSlipperDrop Drop = SlipperDrops[Idx];
        if (Drop.ExpiresAtMs <= NowMs)
        {
            SlipperDrops.RemoveAt(Idx);
            continue;
        }
        ALDCharacter* HolderActor = nullptr;
        FLDBodyCombatState* Holder = FindBody(Drop.OwnerBody, &HolderActor);
        if (!Holder || !HolderActor || Holder->bPendingDeath)
        {
            SlipperDrops.RemoveAt(Idx);
            continue;
        }
        if (FVector::Dist2D(FootOf(*HolderActor), Drop.FootCm) > Drop.RadiusCm) { continue; }
        SlipperDrops.RemoveAt(Idx);
        Holder->Cues.LastPickupAtMs = NowMs;
        MarkViewChanged(Holder->Victim.EntityId);
        // Refund refund_ratio of remaining cooldown on active_snark_bolt.
        if (Drop.Terms.RatioBp > 0)
        {
            if (FLDCombatEntityRecord* Record = FindRecord(Holder->Victim.EntityId))
            {
                for (FLDCombatTrack& Track : Record->Tracks)
                {
                    if (Track.Kind == ELDCombatSourceKind::Learned && Track.DefinitionId == Drop.SkillId && Track.ReadyAtMs > NowMs)
                    {
                        const int64 Rem = Track.ReadyAtMs - NowMs;
                        Track.ReadyAtMs = LDCombatSourceRules::ReduceReadyAt(Track.ReadyAtMs, NowMs, Share(Rem, Drop.Terms.RatioBp));
                    }
                }
            }
        }
        // Stackable AS/MS buff for buff_duration, plus 3B2 next_basic_amp if learned.
        if (!Drop.Terms.Stats.IsEmpty() && Drop.Terms.SecondaryDurationMs > 0)
        {
            FLDCombatTimedBonus SlipperBuff;
            SlipperBuff.EffectId = FGuid::NewGuid();
            SlipperBuff.SourceDefinition = FName(TEXT("slipper_pickup_buff"));
            SlipperBuff.Stats = Drop.Terms.Stats;
            if (!LDMatchLifecycle::AddDurationMs(NowMs, Drop.Terms.SecondaryDurationMs, SlipperBuff.ExpiresAtMs, Error)) { return false; }
            LDTactical::ApplyTimedBonus(Holder->TimedBonuses, SlipperBuff);
            if (!RefreshTemporary(*Holder, HolderActor, NowMs, Error)) { return false; }
        }
        if (Catalog())
        {
            const FLDDemoAbilityDefinition* AbilDef = nullptr;
            FString LookupErr;
            if (Catalog()->FindAbility(Drop.SkillId.ToString(), AbilDef, LookupErr) && AbilDef)
            {
                if (const FLDCombatEntityRecord* Rec = FindRecord(Holder->Victim.EntityId))
                {
                    for (const FLDLearnedActiveValue& L : Rec->Build.Actives)
                    {
                        if (L.AbilityId == Drop.SkillId)
                        {
                            const FLDDemoAbilityValues& Val = AbilDef->FindBranch(L.Branch) ? AbilDef->FindBranch(L.Branch)->Values : AbilDef->Values;
                            if (const FLDDemoEffectDefinition* CatchAmp = Val.Effects.FindByPredicate(
                                [](const FLDDemoEffectDefinition& E) { return E.GetOp() == ELDDemoEffectOp::NextBasicAmp && E.bOnCatch; }))
                            {
                                Holder->Branch.NextBasicAmpBp = FMath::Max(Holder->Branch.NextBasicAmpBp, CatchAmp->RatioBp);
                                if (!LDMatchLifecycle::AddDurationMs(NowMs, CatchAmp->DurationMs, Holder->Branch.NextBasicAmpUntilMs, Error))
                                { return false; }
                            }
                        }
                    }
                }
            }
        }
        MarkViewChanged(Holder->Victim.EntityId);
    }
    return true;
}

bool ALDCombatDirector::AdvanceCordons(int64 NowMs, FString& Error)
{
    using namespace LDCombatSynergyDetail;
    FLDElementTerms PushTerms;
    if (!Cordons.IsEmpty() && !ElementTerms(PushTerms, Error)) { return false; }
    for (int32 Idx = Cordons.Num() - 1; Idx >= 0; --Idx)
    {
        if (Cordons[Idx].ExpiresAtMs <= NowMs)
        {
            Cordons.RemoveAt(Idx);
            continue;
        }
        FLDCombatCordon& Cordon = Cordons[Idx];
        FLDBodyCombatState* CordonOwner = FindBody(Cordon.Payload.SourceBody);
        if (!CordonOwner || CordonOwner->bPendingDeath)
        {
            Cordons.RemoveAt(Idx);
            continue;
        }
        const FVector SegA = Cordon.CentreCm - Cordon.Tangent * Cordon.HalfLengthCm;
        const FVector SegB = Cordon.CentreCm + Cordon.Tangent * Cordon.HalfLengthCm;
        const FVector Normal(-Cordon.Tangent.Y, Cordon.Tangent.X, 0);
        for (auto& Pair : Bodies)
        {
            ALDCharacter* CandidateActor = Pair.Key.Get();
            FLDBodyCombatState& Candidate = Pair.Value;
            if (!CandidateActor || Candidate.bPendingDeath || Candidate.Victim.EntityId == Cordon.Payload.SourceEntity
                || IsIsolated(Cordon.Payload.SourceEntity, Candidate))
            { continue; }
            const int64* CooldownUntil = Cordon.EnemyCooldownUntilMs.Find(Candidate.Victim.EntityId);
            if (CooldownUntil && *CooldownUntil > NowMs) { continue; }
            const FVector Foot = FootOf(*CandidateActor);
            const double Dist = FMath::PointDistToSegment(FVector(Foot.X, Foot.Y, Cordon.CentreCm.Z),
                FVector(SegA.X, SegA.Y, Cordon.CentreCm.Z), FVector(SegB.X, SegB.Y, Cordon.CentreCm.Z));
            if (Dist > Cordon.HalfWidthCm + 40.) { continue; }
            Cordon.EnemyCooldownUntilMs.Add(Candidate.Victim.EntityId, NowMs + 650);
            int32& Touches = Cordon.EnemyTouches.FindOrAdd(Candidate.Victim.EntityId);
            ++Touches;
            if (Cordon.Terms.SecondaryRatioBp > 0 && Cordon.Terms.SecondaryDurationMs > 0)
            {
                FLDCombatVulnerability& Vuln = Candidate.Branch.Vulnerabilities.AddDefaulted_GetRef();
                Vuln.Scope = ELDDemoDamageScope::All;
                Vuln.RatioBp = Cordon.Terms.SecondaryRatioBp;
                if (!LDMatchLifecycle::AddDurationMs(NowMs, Cordon.Terms.SecondaryDurationMs, Vuln.ExpiresAtMs, Error)) { return false; }
                ++Candidate.StateRevision;
            }
            FLDCombatHit Hit = Cordon.Payload;
            Hit.HitId = FGuid::NewGuid();
            Hit.Target = {Candidate.Victim, Candidate.Identity};
            Hit.KnockbackCm = Cm(Cordon.Terms.DistanceCm);
            Hit.KnockbackMs = FMath::Max<int64>(120, PushTerms.PushMs);
            Hit.KnockbackSource = Cordon.Payload.Source;
            Hit.bKnockbackFromAnchor = true;
            const double Side = FVector::DotProduct(Foot - Cordon.CentreCm, Normal) >= 0. ? 1. : -1.;
            Hit.KnockbackAnchorCm = Foot - Normal * (Side * 120.);
            if (Cordon.Terms.ExtraDurationMs > 0 && (Touches >= 2 || Candidate.DashUntilMs > NowMs))
            {
                Hit.StunDurationMs = FMath::Max(Hit.StunDurationMs, Cordon.Terms.ExtraDurationMs);
            }
            PendingHits.Add(MoveTemp(Hit));
        }
    }
    return true;
}

bool ALDCombatDirector::AdvanceDecoys(int64 NowMs, FString& Error)
{
    using namespace LDCombatSynergyDetail;
    // Active decoys taunt nearby camp monsters; at ExplodeAtMs they detonate for adaptive damage + slow.
    for (int32 Idx = Decoys.Num() - 1; Idx >= 0; --Idx)
    {
        const FLDCombatDecoy Decoy = Decoys[Idx];
        FLDBodyCombatState* Holder = FindBody(Decoy.OwnerBody);
        if (Decoy.ExplodeAtMs > NowMs) { continue; }
        Decoys.RemoveAt(Idx);
        if (!Catalog()) { continue; }
        TArray<FLDCombatBodyRef> Around;
        if (!CollectAreaTargets(Decoy.FootCm, Decoy.RadiusCm, Holder, Decoy.OwnerEntity, Around, Error)) { return false; }
        const FGuid BlastId = FGuid::NewGuid();
        const ELDDemoDamageType DmgType = LDTactical::EffectiveDamageType(
            ELDDemoDamageType::Physical, Decoy.Terms.AdaptiveRatioBp, Decoy.SourceBonusADP2, Decoy.SourceAPP2);
        for (const FLDCombatBodyRef& TargetRef : Around)
        {
            FLDCombatHit Hit;
            Hit.HitId = FGuid::NewGuid();
            Hit.EffectId = BlastId;
            Hit.Target = TargetRef;
            Hit.SourceBody = Decoy.OwnerBody;
            Hit.SourceEntity = Decoy.OwnerEntity;
            Hit.CreditOwner = Decoy.CreditOwner;
            Hit.Source = Decoy.Source;
            Hit.HitClass = ELDCombatHitClass::Ability;
            Hit.BaseP2 = Decoy.Terms.BaseP2;
            Hit.AdaptiveRatioBp = Decoy.Terms.AdaptiveRatioBp;
            Hit.DamageType = DmgType;
            Hit.SourceADP2 = Decoy.SourceADP2;
            Hit.SourceAPP2 = Decoy.SourceAPP2;
            Hit.SourceBonusADP2 = Decoy.SourceBonusADP2;
            Hit.SlowBp = Decoy.Terms.RatioBp;
            Hit.SlowDurationMs = Decoy.Terms.DurationMs;
            PendingHits.Add(MoveTemp(Hit));
        }
    }
    return true;
}

bool ALDCombatDirector::AdvanceSynergyStates(int64 NowMs, FString& Error)
{
    using namespace LDCombatSynergyDetail;
    const FLDDemoContentSnapshot* Content = Catalog();
    for (auto& Pair : Bodies)
    {
        ALDCharacter* Actor = Pair.Key.Get();
        FLDBodyCombatState& Body = Pair.Value;
        if (!Actor || Body.bPendingDeath) { continue; }
        const FVector Foot = FootOf(*Actor);

        // 1. active_unplug ParryStance expiry: if unparried, cleave lightly around self and refund miss_refund of cooldown.
        if (Body.ActiveParry.IsSet() && NowMs >= Body.ParryUntilMs)
        {
            const FLDDemoEffectDefinition Parry = Body.ActiveParry.GetValue();
            const FLDCombatSourceRef ParrySrc = Body.ParrySource;
            const bool bWasTriggered = Body.bParryTriggered;
            Body.ActiveParry.Reset();
            Body.ParryUntilMs = 0;
            Body.bParryTriggered = false;
            if (!bWasTriggered && Parry.CostRatioBp > 0)
            {
                if (FLDCombatEntityRecord* Rec = FindRecord(Body.Victim.EntityId))
                {
                    for (FLDCombatTrack& Track : Rec->Tracks)
                    {
                        if (Track.Kind == ELDCombatSourceKind::Learned && Track.DefinitionId == ParrySrc.DefinitionId && Track.ReadyAtMs > NowMs)
                        {
                            const int64 Rem = Track.ReadyAtMs - NowMs;
                            Track.ReadyAtMs = LDCombatSourceRules::ReduceReadyAt(Track.ReadyAtMs, NowMs, Share(Rem, Parry.CostRatioBp));
                        }
                    }
                    MarkViewChanged(Body.Victim.EntityId);
                }
            }
        }

        // 2. card_eagle_stance: track standing-still duration and linger window.
        {
            TArray<FLDPassiveEffectRef> Eagles;
            if (!CollectPassiveEffects(Body.BuildProjection, ELDDemoEffectOp::EagleStance, Eagles, Error)) { return false; }
            if (!Eagles.IsEmpty())
            {
                const bool bWasEagle = Body.bEagleStanceActive;
                if (!Body.bHasLastStillFoot)
                {
                    Body.bHasLastStillFoot = true;
                    Body.LastStillFootCm = Foot;
                    Body.StillSinceMs = NowMs;
                }
                else if (FVector::Dist2D(Foot, Body.LastStillFootCm) > 4.0)
                {
                    const int64 DelayMs = Eagles[0].Effect.DelayMs;
                    if (DelayMs > 0 && NowMs - Body.StillSinceMs >= DelayMs)
                    {
                        int64 LingerUntil = 0;
                        if (!LDMatchLifecycle::AddDurationMs(NowMs, Eagles[0].Effect.DurationMs, LingerUntil, Error)) { return false; }
                        Body.EagleLingerUntilMs = FMath::Max(Body.EagleLingerUntilMs, LingerUntil);
                        LedgerCardTriggered(Body, Eagles[0].Key);
                    }
                    Body.LastStillFootCm = Foot;
                    Body.StillSinceMs = NowMs;
                }
                const bool bNowEagle = IsEagleStanceActive(Body, NowMs);
                Body.bEagleStanceActive = bNowEagle;
                if (bWasEagle != bNowEagle && !RefreshTemporary(Body, Actor, NowMs, Error)) { return false; }
            }
        }

        // 3. card_skate_blade: track standing on any ice (own or enemy) + enemies sliding/pushed on ice per 100cm.
        const bool bNowAnyIce = StandsOnAnyIce(GroundStates, Foot, NowMs);
        if (bNowAnyIce != Body.bOnAnyIce)
        {
            Body.bOnAnyIce = bNowAnyIce;
            if (!RefreshTemporary(Body, Actor, NowMs, Error)) { return false; }
        }
        const bool bSlidingOrPushedOnIce = bNowAnyIce && (Body.bOnEnemyIce || LDTactical::HasActiveRoot(Body.Knockbacks, NowMs));
        if (bSlidingOrPushedOnIce)
        {
            if (!Body.bTrackingIceSlide)
            {
                Body.bTrackingIceSlide = true;
                Body.LastIceSlideFootCm = Foot;
                Body.IceSlideAccumCm = 0.;
            }
            else
            {
                Body.IceSlideAccumCm += FVector::Dist2D(Foot, Body.LastIceSlideFootCm);
                Body.LastIceSlideFootCm = Foot;
                for (auto& HolderPair : Bodies)
                {
                    FLDBodyCombatState& Holder = HolderPair.Value;
                    if (!HolderPair.Key.IsValid() || Holder.bPendingDeath || Holder.Victim.EntityId == Body.Victim.EntityId
                        || IsIsolated(Holder.Victim.EntityId, Body))
                    { continue; }
                    TArray<FLDPassiveEffectRef> Skates;
                    if (!CollectPassiveEffects(Holder.AttributeProjection, ELDDemoEffectOp::SkateBlade, Skates, Error)) { return false; }
                    if (Skates.IsEmpty()) { continue; }
                    const double StepCm = FMath::Max(25., Cm(Skates[0].Effect.DistanceCm));
                    if (Body.IceSlideAccumCm >= StepCm && Content)
                    {
                        const int32 Cuts = int32(Body.IceSlideAccumCm / StepCm);
                        Body.IceSlideAccumCm -= double(Cuts) * StepCm;
                        int64 BaseP2 = 0, AdapBp = 0;
                        for (const FLDPassiveEffectRef& Ref : Skates) { BaseP2 += Ref.Effect.BaseP2; AdapBp += Ref.Effect.AdaptiveRatioBp; }
                        const int64 BonusADP2 = LDTactical::BonusADOf(Content->Get().Parameters, GrowthLevelOf(Holder), Holder.AttributeProjection.Final.ADP2);
                        FLDCombatHit Cut;
                        Cut.HitId = FGuid::NewGuid();
                        Cut.EffectId = FGuid::NewGuid();
                        Cut.Target = {Body.Victim, Body.Identity};
                        Cut.SourceBody = {Holder.Victim, Holder.Identity};
                        Cut.SourceEntity = Holder.Victim.EntityId;
                        Cut.CreditOwner = Holder.Victim.Kind == ELDVictimKind::Contender ? Holder.Victim.EntityId : FGuid();
                        Cut.Source = Skates[0].Source;
                        Cut.HitClass = ELDCombatHitClass::Ability;
                        Cut.BaseP2 = BaseP2 * Cuts;
                        Cut.AdaptiveRatioBp = AdapBp * Cuts;
                        Cut.DamageType = LDTactical::EffectiveDamageType(ELDDemoDamageType::Physical, AdapBp, BonusADP2, Holder.AttributeProjection.Final.APP2);
                        Cut.SourceADP2 = Holder.AttributeProjection.Final.ADP2;
                        Cut.SourceAPP2 = Holder.AttributeProjection.Final.APP2;
                        Cut.SourceBonusADP2 = BonusADP2;
                        Cut.bAddedDamage = true;
                        PendingHits.Add(MoveTemp(Cut));
                        LedgerCardTriggered(Holder, Skates[0].Key);
                    }
                }
            }
        }
        else
        {
            Body.bTrackingIceSlide = false;
            Body.IceSlideAccumCm = 0.;
        }
    }
    ShardBursts.RemoveAll([NowMs](const FLDCombatShardBurst& Burst) { return Burst.ExpiresAtMs <= NowMs; });
    return true;
}
