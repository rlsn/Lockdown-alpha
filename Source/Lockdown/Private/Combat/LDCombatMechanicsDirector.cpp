#include "Combat/LDCombatDirector.h"
#include "Core/LDLog.h"
#include "Combat/LDCombatMechanics.h"
#include "Combat/LDCombatSourceRules.h"
#include "Combat/LDControlRules.h"
#include "Combat/LDTacticalRules.h"
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDGameMode.h"
#include "Engine/World.h"

// #112 §5 mechanics on Combat's body records; the pure rules live in LDCombatMechanics.cpp.
namespace LDCombatMechanicsDetail
{
    double Cm(const FLDDemoRational& Value)
    { return Value.Denominator>0 ? double(Value.Numerator)/double(Value.Denominator) : 0.; }

    const FLDDemoEffectDefinition* FindOp(const FLDDemoAbilityValues& Values, ELDDemoEffectOp Op)
    { return Values.Effects.FindByPredicate([Op](const FLDDemoEffectDefinition& Effect) { return Effect.GetOp()==Op; }); }

    // floor(ValueP2 x Bp / 10000) without overflow for nonnegative inputs.
    int64 Scale(int64 ValueP2, int64 Bp)
    {
        if (ValueP2<=0 || Bp<=0) { return 0; }
        return ValueP2/10000*Bp+ValueP2%10000*Bp/10000;
    }

    // One damage-carrying hit from a body to a target, with the body's current AD/AP as its source values.
    FLDCombatHit MakeHit(const FLDBodyCombatState& Source, const FLDCombatSourceRef& Ref, const FLDCombatBodyRef& Target,
        const FGuid& EffectId)
    {
        FLDCombatHit Hit;
        Hit.HitId=FGuid::NewGuid();
        Hit.EffectId=EffectId.IsValid() ? EffectId : FGuid::NewGuid();
        Hit.Target=Target;
        Hit.SourceBody={Source.Victim,Source.Identity};
        Hit.SourceEntity=Source.Victim.EntityId;
        Hit.CreditOwner=Source.Victim.Kind==ELDVictimKind::Contender ? Source.Victim.EntityId : FGuid();
        Hit.Source=Ref;
        Hit.HitClass=ELDCombatHitClass::Ability;
        Hit.SourceADP2=Source.AttributeProjection.Final.ADP2;
        Hit.SourceAPP2=Source.AttributeProjection.Final.APP2;
        return Hit;
    }
}

ALDCharacter* ALDCombatDirector::ActorOf(const FLDBodyCombatState& Body) const
{
    for (const auto& Pair : Bodies) { if (&Pair.Value==&Body) { return Pair.Key.Get(); } }
    return nullptr;
}

bool ALDCombatDirector::FindTrigger(const FLDAttributeProjection& Projection, ELDDemoEffectOp Op,
    TOptional<FLDPassiveEffectRef>& Out, FString& Error) const
{
    Out.Reset();
    TArray<FLDPassiveEffectRef> Held;
    if (!CollectPassiveEffects(Projection,Op,Held,Error)) { return false; }
    if (!Held.IsEmpty()) { Out=MoveTemp(Held[0]); }
    return true;
}

bool ALDCombatDirector::HealBody(FLDBodyCombatState& Body, int64 AmountP2, const FLDCombatSourceRef& HealSource, int64* OutApplied,
    const FGuid& RootEffectId)
{
    if (OutApplied) { *OutApplied=0; }
    if (Body.bPendingDeath || Body.Resources.HpP2<=0 || AmountP2<=0) { return false; }
    // #198 S21: a live grievous cuts every new heal once, before it lands.
    if (const ALDGameMode* Mode=Core()) { AmountP2=LDCombatElements::AfterGrievousP2(AmountP2,Body.Grievous,Mode->GetAuthorityNowMs()); }
    if (AmountP2<=0) { return false; }
    // #198 F M12: a heal repays the oldest debts first (spec §10.3); only the rest restores HP.
    int64 Left=AmountP2, Repaid=0;
    for (FLDCombatDebt& Debt : Body.Debts)
    {
        if (Left<=0) { break; }
        const int64 Part=FMath::Min(Left,Debt.AmountP2);
        Debt.AmountP2-=Part;
        Left-=Part;
        Repaid+=Part;
    }
    Body.Debts.RemoveAll([](const FLDCombatDebt& Debt) { return Debt.AmountP2<=0; });
    auto& Pool=Body.Resources;
    const int64 Before=Pool.HpP2;
    if (Left>0)
    {
        Pool.HpP2=Left>=Pool.MaxHpP2-Pool.HpP2 ? Pool.MaxHpP2 : Pool.HpP2+Left;
        if (Pool.HpP2>=Pool.MaxHpP2) { Body.HpRegenMicroP2=0; }
    }
    const int64 Healed=Pool.HpP2-Before;
    if (Healed==0 && Repaid==0) { return false; }
    if (OutApplied) { *OutApplied=Healed+Repaid; }
    if (Repaid>0)
    {
        UE_LOG(LogLDCombat,Log,TEXT("LD_COMBAT_DEBT_REPAID body=%s repaid=%s healed=%s"),*LDLog::Id(Body.Victim.EntityId),
            *LDLog::P2(Repaid),*LDLog::P2(Healed));
    }
    // #198 F C08: half of the real heal and repayment of the source it sits on joins its counter store.
    if (HealSource.Kind!=ELDCombatSourceKind::None)
    {
        TArray<FLDPassiveEffectRef> Counters;
        FString Error;
        if (!WorkshopOn(Body.AttributeProjection,ELDDemoEffectOp::HealCounter,HealSource,Counters,Error))
        {
            UE_LOG(LogLDCombat,Error,TEXT("LD_COMBAT_WORKSHOP_UNRESOLVED body=%s op=heal_counter error=%s"),*LDLog::Id(Body.Victim.EntityId),*Error);
        }
        else if (!Counters.IsEmpty())
        {
            const FLDDemoEffectDefinition& Counter=Counters[0].Effect;
            const int64 Stored=(Healed+Repaid)/10000*Counter.RatioBp+(Healed+Repaid)%10000*Counter.RatioBp/10000;
            const int64 Now=Core() ? Core()->GetAuthorityNowMs() : 0;
            if (Stored>0)
            {
                Body.CounterStoreP2=FMath::Min(Body.CounterStoreP2+Stored,Counter.BaseP2);
                if (RootEffectId.IsValid()) { Body.CounterRootEffectId=RootEffectId; }
                Body.CounterLastAddMs=Now;
                if (Body.CounterNextFireMs<=Now) { Body.CounterNextFireMs=Now+Counter.IntervalMs; }
            }
        }
    }
    ++Pool.Revision;
    MarkViewChanged(Body.Victim.EntityId);
    LedgerHealed(Body,Pool.HpP2-Before);
    LedgerDebtRepaid(Body,Repaid);
    return true;
}

bool ALDCombatDirector::GrantShieldTo(FLDBodyCombatState& Body, int64 AmountP2, int64 DurationMs, const FGuid& EffectId,
    const FGuid& SourceEntity, int64 NowMs, FString& Error, const FLDCombatSourceRef& GrantSource)
{
    if (Body.bPendingDeath) { return true; }
    // #198 S21: a live grievous cuts the new shield before the replacement check; existing shields are untouched.
    AmountP2=LDCombatElements::AfterGrievousP2(AmountP2,Body.Grievous,NowMs);
    int64 Expires=0;
    if (!LDMatchLifecycle::AddDurationMs(NowMs,DurationMs,Expires,Error)) { return false; }
    // A weaker shield is a stated sub-effect outcome, not a failed release.
    if (!LDTactical::ApplyShield(Body.Shield,Body.Resources.ShieldP2,FLDCombatShield{EffectId,SourceEntity,Expires},AmountP2,NowMs))
    { return true; }
    Body.ShieldGrantedP2=AmountP2;
    Body.ShieldGrantSource=GrantSource;
    // C18: the replaced shield settles; a host shield of an installed connector is watched from now.
    if (!SettleStoredForce(Body,NowMs,Error)) { return false; }
    TArray<FLDPassiveEffectRef> Stores;
    if (GrantSource.Kind==ELDCombatSourceKind::Learned
        && !WorkshopOn(Body.AttributeProjection,ELDDemoEffectOp::StoredForce,GrantSource,Stores,Error)) { return false; }
    if (!Stores.IsEmpty() && !(Body.StoredForceWatch.IsSet() && Body.StoredForceWatch->ShieldId==EffectId))
    {
        FLDCombatStoredForceWatch& Watch=Body.StoredForceWatch.Emplace();
        Watch.ShieldId=EffectId;
        Watch.Component=Stores[0].Source.DefinitionId;
        Watch.Host=GrantSource.DefinitionId;
    }
    ++Body.Resources.Revision;
    ++Body.StateRevision;
    MarkViewChanged(Body.Victim.EntityId);
    LedgerShieldGranted(Body,AmountP2);
    return true;
}

bool ALDCombatDirector::OnShieldBroken(FLDBodyCombatState& Holder, const FLDCombatHit& Hit, int64 GrantedP2, int64 NowMs, FString& Error)
{
    TOptional<FLDPassiveEffectRef> Held;
    if (!FindTrigger(Holder.AttributeProjection,ELDDemoEffectOp::ShieldBreakRetort,Held,Error)) { return false; }
    const FLDBodyCombatState* Breaker=FindBody(Hit.SourceBody);
    if (!Held.IsSet() || !Breaker || Breaker==&Holder || Breaker->bPendingDeath || Holder.bPendingDeath || NowMs<Holder.RetortReadyAtMs) { return true; }
    const FLDDemoEffectDefinition& Retort=Held->Effect;
    if (!LDMatchLifecycle::AddDurationMs(NowMs,Retort.IntervalMs,Holder.RetortReadyAtMs,Error)) { return false; }
    FLDCombatHit Answer=LDCombatMechanicsDetail::MakeHit(Holder,Held->Source,Hit.SourceBody,FGuid());
    Answer.BaseP2=LDCombatMechanicsDetail::Scale(GrantedP2,Retort.RatioBp);
    Answer.DamageType=ELDDemoDamageType::Magical;
    Answer.SlowBp=Retort.SecondaryRatioBp;
    Answer.SlowDurationMs=Retort.SecondaryDurationMs;
    Answer.bFromShieldTrigger=true;
    PendingHits.Add(Answer);
    return true;
}

bool ALDCombatDirector::ApplyKnockback(ALDCharacter& Target, FLDBodyCombatState& Body, const FLDCombatHit& InHit, int64 NowMs,
    bool& bApplied, FString& Error)
{
    bApplied=false;
    ALDCharacter* SourceActor=nullptr;
    FindBody(InHit.SourceBody,&SourceActor);
    const FVector From=InHit.bKnockbackFromAnchor || !SourceActor ? InHit.KnockbackAnchorCm : FootOf(*SourceActor);
    const double Yaw=SourceActor ? SourceActor->GetActorRotation().Yaw : Target.GetActorRotation().Yaw+180.;
    FVector Direction;
    if (!LDCombatMechanics::KnockbackDirection(From,FootOf(Target),Yaw,Direction))
    { Error=TEXT("A knockback has no finite direction."); return false; }
    FLDCombatHit Hit=InHit;
    if (Hit.bKnockbackPull)
    {
        // M20: toward the origin, stopping KnockbackStopCm short of it; the time shrinks with a clamped distance.
        Direction=-Direction;
        const double Room=FVector::Dist2D(From,FootOf(Target))-Hit.KnockbackStopCm;
        if (Room<=1.) { return true; }
        if (Room<Hit.KnockbackCm)
        {
            Hit.KnockbackMs=FMath::Max<int64>(1,int64(double(Hit.KnockbackMs)*Room/Hit.KnockbackCm));
            Hit.KnockbackCm=Room;
        }
    }
    auto* Movement=Cast<ULDCharacterMovementComponent>(Target.GetCharacterMovement());
    if (!Movement) { Error=TEXT("Knockback requires the Demo character movement."); return false; }
    // The control itself (window, interruptions, order drop, spray, assists) commits through the single control path.
    FLDControlApplication App;
    App.Kind=ELDControlKind::Knockback;
    App.EffectId=Hit.EffectId;
    App.SourceEntity=Hit.SourceEntity;
    App.SourceBody=Hit.SourceBody;
    App.CreditOwner=Hit.CreditOwner;
    App.DurationMs=Hit.KnockbackMs;
    if (!CommitControl(Body,App,NowMs,bApplied,Error)) { return false; }
    if (!bApplied) { return true; }
    Body.DashContact.Reset();
    // B12: a knocked-back dash is cancelled, never completed.
    Body.VoluntaryDash.Reset();
    // The same native root motion as a dash: integrated by the authority steps along the ground, stopped by blocking
    // geometry at the last legal position, never a teleport.
    // A push is blocked by bodies as well as static geometry (#198 F: C02 reads its first real block).
    if (!Movement->StartDemoDash(Direction,Hit.KnockbackCm,double(Hit.KnockbackMs)/1000.,Error,true))
    { Error=TEXT("Knockback: ")+Error; return false; }
    if (!WatchKnockbackImpact(Target,Body,Hit,NowMs,Error)) { return false; }
    UE_LOG(LogLDCombat,Display,TEXT("LD_COMBAT_KNOCKBACK victim=%s source=%s cm=%.0f ms=%lld pull=%d"),*Body.Victim.EntityId.ToString(),
        *Hit.SourceEntity.ToString(),Hit.KnockbackCm,Hit.KnockbackMs,Hit.bKnockbackPull ? 1 : 0);
    return true;
}

bool ALDCombatDirector::ExpireMechanics(FLDBodyCombatState& Body, int64 NowMs)
{
    bool Changed=Body.Knockbacks.RemoveAll([NowMs](const FLDCombatRoot& Row) { return Row.ExpiresAtMs<=NowMs; })>0;
    Changed|=Body.AttackSlows.RemoveAll([NowMs](const FLDCombatSlow& Row) { return Row.ExpiresAtMs<=NowMs; })>0;
    if (Body.DashContact && Body.DashContact->EndMs<=NowMs) { Body.DashContact.Reset(); }
    return Changed;
}

int64 ALDCombatDirector::NextMechanicsDueMs(int64 NowMs) const
{
    int64 Due=MAX_int64;
    const auto Consider=[&Due,NowMs](int64 At) { if (At>NowMs && At<Due) { Due=At; } };
    for (const auto& Pair : Bodies)
    {
        const FLDBodyCombatState& Body=Pair.Value;
        if (Body.bPendingDeath) { continue; }
        for (const auto& Row : Body.Knockbacks) { Consider(Row.ExpiresAtMs); }
        for (const auto& Row : Body.AttackSlows) { Consider(Row.ExpiresAtMs); }
        if (Body.DashContact) { Consider(Body.DashContact->EndMs); }
    }
    for (const FLDCombatZone& Zone : Zones)
    {
        if (Zone.Pulsed<Zone.Count) { Consider(Zone.StartMs+Zone.DelayMs+Zone.IntervalMs*Zone.Pulsed); }
        if (Zone.bSlow) { Consider(Zone.NextSlowAtMs); }
        if (Zone.BurnStacks>0 && Zone.NextBurnAtMs<Zone.EndMs) { Consider(Zone.NextBurnAtMs); }
        Consider(Zone.EndMs);
    }
    for (const auto& Channel : Channels) { Consider(Channel.NextPulseMs); }
    for (const auto& Ground : GroundEffects) { Consider(Ground.ReadyAtMs); if (Ground.bTrap) { Consider(Ground.ExpiresAtMs); } }
    Consider(NextElementsDueMs(NowMs));
    return Due;
}

bool ALDCombatDirector::AdvanceMechanics(int64 NowMs, FString& Error)
{
    // Ground zones: damage pulses, slow checks and burn checks.
    for (int32 Index=Zones.Num()-1; Index>=0; --Index)
    {
        FLDCombatZone& Zone=Zones[Index];
        ALDCharacter* HolderActor=nullptr;
        FLDBodyCombatState* Holder=FindBody(Zone.SourceBody,&HolderActor);
        // M16: a tethered zone follows its living caster before any check; a dead one leaves it where it was.
        if (Zone.bTether && Holder && HolderActor && !Holder->bPendingDeath) { Zone.CentreCm=FootOf(*HolderActor); }
        const auto Inside=[this,&Zone,Holder](TArray<FLDCombatBodyRef>& Out, FString& InError)
        { return CollectAreaTargets(Zone.CentreCm,Zone.RadiusCm,Holder,Zone.SourceEntity,Out,InError); };
        const auto Emit=[this,&Zone](const FLDCombatBodyRef& Target)->FLDCombatHit&
        {
            FLDCombatHit& Hit=PendingHits.AddDefaulted_GetRef();
            Hit.HitId=FGuid::NewGuid();
            Hit.EffectId=Zone.EffectId;
            Hit.Target=Target;
            Hit.SourceBody=Zone.SourceBody;
            Hit.SourceEntity=Zone.SourceEntity;
            Hit.CreditOwner=Zone.CreditOwner;
            Hit.Source=Zone.Source;
            Hit.HitClass=ELDCombatHitClass::Ability;
            Hit.SourceADP2=Zone.SourceADP2;
            Hit.SourceAPP2=Zone.SourceAPP2;
            Hit.CastLedger=Zone.CastLedger;
            return Hit;
        };
        while (Zone.Pulsed<Zone.Count && Zone.StartMs+Zone.DelayMs+Zone.IntervalMs*Zone.Pulsed<=NowMs)
        {
            ++Zone.Pulsed;
            if (Zone.bDamage || Zone.bWet)
            {
                TArray<FLDCombatBodyRef> Targets;
                if (!Inside(Targets,Error)) { Error=TEXT("Zone pulse: ")+Error; return false; }
                for (const FLDCombatBodyRef& Target : Targets)
                {
                    FLDCombatHit& Hit=Emit(Target);
                    Hit.BaseP2=Zone.BaseP2;
                    Hit.ADRatioBp=Zone.ADRatioBp;
                    Hit.APRatioBp=Zone.APRatioBp;
                    Hit.DamageType=Zone.DamageType;
                    Hit.bPeriodic=true;
                    // A native pulse is the active's direct damage (spec §8): B17's lock rides on it and B18 reads it.
                    Hit.bDirectAbilityDamage=Zone.bDamage;
                    Hit.ControlAmpBp=Zone.ControlAmpBp;
                    Hit.IdleReadyAmpBp=Zone.IdleReadyAmpBp;
                    Hit.PackageScaleBp=Zone.PackageScaleBp;
                    // #198 zone_wet: every pulse also applies wet with the owner's snapshot at release.
                    Hit.bApplyWet=Zone.bWet;
                    // #198 S08: the pulse's own fire, in the same batch before any water or electric of the hit.
                    Hit.BurnStacks=Zone.PulseBurnStacks;
                    Hit.ElementLevel=Zone.ElementLevel;
                    Hit.ElementAPP2=Zone.ElementAPP2;
                }
            }
        }
        while (Zone.bSlow && Zone.NextSlowAtMs>0 && Zone.NextSlowAtMs<=NowMs && Zone.NextSlowAtMs<Zone.EndMs)
        {
            TArray<FLDCombatBodyRef> Targets;
            if (!Inside(Targets,Error)) { Error=TEXT("Zone slow: ")+Error; return false; }
            const FGuid CheckId=FGuid::NewGuid();
            for (const FLDCombatBodyRef& Target : Targets)
            {
                FLDCombatHit& Hit=Emit(Target);
                Hit.EffectId=CheckId;
                Hit.SlowBp=Zone.SlowBp;
                // #198: each check slows for its own duration (no linger).
                Hit.SlowDurationMs=Zone.SlowDurationMs;
                Hit.bSilent=true;
            }
            Zone.NextSlowAtMs+=Zone.SlowIntervalMs;
        }
        while (Zone.BurnStacks>0 && Zone.BurnIntervalMs>0 && Zone.NextBurnAtMs<=NowMs && Zone.NextBurnAtMs<Zone.EndMs)
        {
            TArray<FLDCombatBodyRef> Targets;
            if (!Inside(Targets,Error)) { Error=TEXT("Zone burn: ")+Error; return false; }
            const FGuid PulseId=FGuid::NewGuid();
            for (const FLDCombatBodyRef& Target : Targets)
            {
                FLDCombatHit& Hit=Emit(Target);
                Hit.EffectId=PulseId;
                Hit.BurnStacks=Zone.BurnStacks;
                Hit.ElementLevel=Zone.ElementLevel;
                Hit.ElementAPP2=Zone.ElementAPP2;
                Hit.bPeriodic=true;
                Hit.bSilent=true;
            }
            Zone.NextBurnAtMs+=Zone.BurnIntervalMs;
        }
        // #198 zone_haste: the owner's and its teammates' move speed while their foot stands inside.
        if (Zone.HasteBp>0) { UpdateZoneHaste(Zone,NowMs); }
        if (Zone.EndMs<=NowMs)
        {
            // #198 F C06: a zone that ran its whole life completes naturally.
            const FLDCombatZone Ended=Zone;
            Zones.RemoveAt(Index);
            if (!TriggerAftershock(Ended.SourceBody,Ended.Source,Ended.EffectId,Ended.CentreCm,Ended.RadiusCm,NowMs,Error)) { return false; }
        }
    }
    return true;
}

void ALDCombatDirector::UpdateZoneHaste(const FLDCombatZone& Zone, int64 NowMs)
{
    for (auto& Pair : Bodies)
    {
        FLDBodyCombatState& Body=Pair.Value;
        const ALDCharacter* Actor=Pair.Key.Get();
        // Spec §9 S20: its owner and the owner's current-match teammates (the PK group and side it was released in); never
        // an enemy.
        const bool bOwner=Body.Victim.EntityId==Zone.SourceEntity;
        const bool bMate=!bOwner && Zone.OwnerPKSide!=0 && Body.PKSide==Zone.OwnerPKSide
            && Body.IsolationGroup==Zone.OwnerGroup && Body.Victim.Kind==ELDVictimKind::Contender;
        const bool bHeld=Body.TimedBonuses.ContainsByPredicate([&Zone](const FLDCombatTimedBonus& Row)
        { return Row.EffectId==Zone.EffectId; });
        if (!bOwner && !bMate && !bHeld) { continue; }
        const bool bInside=(bOwner || bMate) && Actor && !Body.bPendingDeath && !Body.bSuspended && NowMs<Zone.EndMs
            && FVector::DistSquared2D(Zone.CentreCm,FootOf(*Actor))<=Zone.RadiusCm*Zone.RadiusCm;
        if (bInside && !bHeld)
        {
            FLDCombatTimedBonus Bonus;
            Bonus.EffectId=Zone.EffectId;
            Bonus.SourceDefinition=Zone.Source.DefinitionId;
            FLDDemoStatBonus& Speed=Bonus.Stats.AddDefaulted_GetRef();
            Speed.Stat=ELDDemoBonusStat::MoveSpeedBonusRatio;
            Speed.Amount=Zone.HasteBp;
            Bonus.ExpiresAtMs=Zone.EndMs;
            LDTactical::ApplyTimedBonus(Body.TimedBonuses,Bonus);
        }
        else if (!bInside && bHeld)
        {
            Body.TimedBonuses.RemoveAll([&Zone](const FLDCombatTimedBonus& Row) { return Row.EffectId==Zone.EffectId; });
        }
        else { continue; }
        ++Body.StateRevision;
        MarkViewChanged(Body.Victim.EntityId);
    }
}

void ALDCombatDirector::FillMechanicsTemplate(const FLDDemoAbilityValues& Values, const FVector& OriginFoot, FLDCombatHit& Template) const
{
    using namespace LDCombatMechanicsDetail;
    if (const FLDDemoEffectDefinition* Push=FindOp(Values,ELDDemoEffectOp::Knockback))
    {
        // The push leaves from the caster's foot at release.
        Template.KnockbackCm=Cm(Push->DistanceCm);
        Template.KnockbackMs=Push->DurationMs;
        Template.bKnockbackFromAnchor=true;
        Template.KnockbackAnchorCm=OriginFoot;
    }
    if (const auto* Slow=FindOp(Values,ELDDemoEffectOp::AttackSlow))
    { Template.AttackSlowBp=Slow->RatioBp; Template.AttackSlowMs=Slow->DurationMs; }
}

bool ALDCombatDirector::ReleaseMechanics(FLDBodyCombatState& Body, const FLDCombatSourceRef& Source, const FLDCombatActiveTerms& Terms,
    const FVector& OriginFoot, const FVector& TargetPoint, const FGuid& EffectId, uint64 Token, int64 IdleAmpBp,
    TSharedPtr<FLDCombatCastLedger> CastLedger, FString& Error)
{
    using namespace LDCombatMechanicsDetail;
    ALDGameMode* Mode=Core();
    if (!Mode) { Error=TEXT("Release mechanics require the current core."); return false; }
    const int64 Now=Mode->GetAuthorityNowMs();
    const FLDDemoAbilityValues& Values=Terms.Values;
    const FLDDemoEffectDefinition* ZoneDef=FindOp(Values,ELDDemoEffectOp::Zone);
    if (ZoneDef && Terms.CompressCone.IsSet())
    {
        Error=TEXT("A zone active compiled a cone compression.");
        return false;
    }
    // #198 F M07: the pulses become one fixed blast after the delay: radius x ratio, the planned pulses' summed package x the
    // multiplier, its slow, burn and native-hit triggers once; no lasting zone or in-zone haste.
    if (ZoneDef && Terms.CompressZone.IsSet())
    {
        const FLDDemoEffectDefinition& Compress=Terms.CompressZone.GetValue();
        const FLDDemoEffectDefinition* Damage=FindOp(Values,ELDDemoEffectOp::ZoneDamage);
        if (!Damage) { Error=TEXT("A compressed zone has no zone damage to compress."); return false; }
        FLDCombatGroundEffect Blast;
        FLDCombatHit& Payload=Blast.Payload;
        Payload=MakeHit(Body,Source,FLDCombatBodyRef(),EffectId);
        Payload.Token=Token;
        Payload.CastLedger=MoveTemp(CastLedger);
        Payload.BaseP2=Damage->BaseP2;
        Payload.ADRatioBp=Damage->ADRatioBp;
        Payload.APRatioBp=Damage->APRatioBp;
        Payload.DamageType=Damage->DamageType;
        Payload.bDirectAbilityDamage=true;
        Payload.IdleReadyAmpBp=IdleAmpBp;
        const int64 Scale=int64(ZoneDef->Count)*Compress.MultiplierBp;
        Payload.PackageScaleBp=Terms.PackageScaleBp/10000*Scale+Terms.PackageScaleBp%10000*Scale/10000;
        if (const FLDDemoEffectDefinition* Slow=FindOp(Values,ELDDemoEffectOp::ZoneSlow))
        { Payload.SlowBp=Slow->RatioBp; Payload.SlowDurationMs=Slow->DurationMs; }
        // S20's wet once; its in-zone haste has no zone to live in. The burn and wet take the owner's element snapshot now.
        Payload.bApplyWet=FindOp(Values,ELDDemoEffectOp::ZoneWet)!=nullptr;
        if (!StampElementSnapshot(Body,Payload,Error)) { return false; }
        if (const FLDDemoEffectDefinition* Burn=FindOp(Values,ELDDemoEffectOp::ZoneBurn)) { Payload.BurnStacks=Burn->Count; }
        if (!StampSourceTerms(Body,Payload,Error)) { return false; }
        Blast.CentreCm=FVector(TargetPoint.X,TargetPoint.Y,OriginFoot.Z);
        Blast.RadiusCm=Cm(ZoneDef->RadiusCm)*double(Compress.RatioBp)/10000.;
        Blast.InnerRadiusCm=Blast.RadiusCm;
        Blast.Origin=ELDCombatGroundOrigin::CompressedZone;
        if (!LDMatchLifecycle::AddDurationMs(Now,Compress.DelayMs,Blast.ReadyAtMs,Error)) { return false; }
        Blast.ExpiresAtMs=Blast.ReadyAtMs;
        GroundEffects.Add(MoveTemp(Blast));
        return true;
    }
    if (ZoneDef)
    {
        FLDCombatZone Zone;
        Zone.EffectId=EffectId;
        Zone.CastLedger=MoveTemp(CastLedger);
        Zone.SourceBody={Body.Victim,Body.Identity};
        Zone.SourceEntity=Body.Victim.EntityId;
        Zone.CreditOwner=Body.Victim.Kind==ELDVictimKind::Contender ? Body.Victim.EntityId : FGuid();
        Zone.Source=Source;
        // A ground point zone sits at the requested point (its height taken from the caster's foot); a dash leaves
        // its zone at the start foot.
        Zone.CentreCm=FindOp(Values,ELDDemoEffectOp::Dash) ? OriginFoot : FVector(TargetPoint.X,TargetPoint.Y,OriginFoot.Z);
        Zone.RadiusCm=Cm(ZoneDef->RadiusCm);
        // M16: the zone rides on the caster's foot (its radius already scaled by CompileActive).
        if (Terms.ZoneTether.IsSet())
        {
            Zone.bTether=true;
            Zone.CentreCm=OriginFoot;
            LedgerComponentTriggered(Body,Terms.ZoneTether->Source.DefinitionId);
        }
        Zone.StartMs=Now;
        Zone.DelayMs=ZoneDef->DelayMs;
        Zone.IntervalMs=ZoneDef->IntervalMs;
        Zone.Count=int32(ZoneDef->Count);
        if (!LDMatchLifecycle::AddDurationMs(Now,ZoneDef->DurationMs,Zone.EndMs,Error)) { return false; }
        Zone.SourceADP2=Body.AttributeProjection.Final.ADP2;
        Zone.SourceAPP2=Body.AttributeProjection.Final.APP2;
        FLDCombatHit SourceTerms;
        if (!StampSourceTerms(Body,SourceTerms,Error)) { return false; }
        Zone.ControlAmpBp=SourceTerms.ControlAmpBp;
        // #198: every interval from the zone's start, each enemy inside is slowed for DurationMs.
        if (const FLDDemoEffectDefinition* Slow=FindOp(Values,ELDDemoEffectOp::ZoneSlow))
        {
            if (Slow->IntervalMs<=0 || Slow->DurationMs<=0 || Slow->RatioBp<=0)
            {
                Error=TEXT("A zone slow needs a positive interval, duration and ratio.");
                return false;
            }
            Zone.bSlow=true;
            Zone.SlowBp=Slow->RatioBp;
            Zone.SlowIntervalMs=Slow->IntervalMs;
            Zone.SlowDurationMs=Slow->DurationMs;
            Zone.NextSlowAtMs=Now;
        }
        // The owner's element snapshot at release (burn layers, wet pulses).
        Zone.ElementLevel=GrowthLevelOf(Body);
        Zone.ElementAPP2=FMath::Max<int64>(Body.AttributeProjection.Final.APP2,0);
        // #147: burn layers on the enemies inside. #198 S08: a damaging zone's burn beats with its pulses and rides on them
        // (one element batch per pulse); a burn of another rhythm is no approved zone shape.
        if (const FLDDemoEffectDefinition* Burn=FindOp(Values,ELDDemoEffectOp::ZoneBurn))
        {
            if (FindOp(Values,ELDDemoEffectOp::ZoneDamage))
            {
                if (Burn->IntervalMs!=ZoneDef->IntervalMs || ZoneDef->DelayMs!=0
                    || int64(ZoneDef->Count)*ZoneDef->IntervalMs!=ZoneDef->DurationMs)
                {
                    Error=TEXT("A damaging zone's burn must beat with its pulses (same interval, no delay, pulses spanning its life).");
                    return false;
                }
                Zone.PulseBurnStacks=Burn->Count;
            }
            else
            {
                Zone.BurnStacks=Burn->Count;
                Zone.BurnIntervalMs=Burn->IntervalMs;
                Zone.NextBurnAtMs=Now;
            }
        }
        // #198: wet on every pulse, and the owner's (and its current-match teammates') move speed inside.
        Zone.bWet=FindOp(Values,ELDDemoEffectOp::ZoneWet)!=nullptr;
        if (const FLDDemoEffectDefinition* Haste=FindOp(Values,ELDDemoEffectOp::ZoneHaste))
        {
            Zone.HasteBp=Haste->RatioBp;
            Zone.OwnerGroup=Body.IsolationGroup;
            Zone.OwnerPKSide=Body.PKSide;
        }
        if (const FLDDemoEffectDefinition* Damage=FindOp(Values,ELDDemoEffectOp::ZoneDamage))
        {
            Zone.bDamage=true;
            Zone.BaseP2=Damage->BaseP2;
            Zone.ADRatioBp=Damage->ADRatioBp;
            Zone.APRatioBp=Damage->APRatioBp;
            Zone.DamageType=Damage->DamageType;
        }
        Zone.CreatedSerial=++NextAreaSerial;
        Zones.Add(MoveTemp(Zone));
    }
    return true;
}

void ALDCombatDirector::AppendMechanicsStatuses(const FLDBodyCombatState& Body, int64 NowMs, TArray<FLDObservedStatus>& Out) const
{
    if (const int64 KnockUntil=LDTactical::ActiveRootUntilMs(Body.Knockbacks,NowMs); KnockUntil>0)
    { Out.Add({uint8(ELDDemoStatusId::Knockback),1,KnockUntil}); }
    int64 AttackSlowUntil=0;
    for (const auto& Slow : Body.AttackSlows) { if (Slow.ExpiresAtMs>NowMs) { AttackSlowUntil=FMath::Max(AttackSlowUntil,Slow.ExpiresAtMs); } }
    if (AttackSlowUntil>0) { Out.Add({uint8(ELDDemoStatusId::AttackSlowed),1,AttackSlowUntil}); }
}
uint64 ALDCombatDirector::ReadKnockbackSerial(const FLDCombatBodyRef& Ref) const
{
    const FLDBodyCombatState* Body=FindBody(Ref);
    return Body ? Body->KnockbackSerial : 0;
}
