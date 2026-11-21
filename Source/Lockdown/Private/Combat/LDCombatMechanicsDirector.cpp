#include "Combat/LDCombatDirector.h"
#include "Core/LDLog.h"
#include "Combat/LDCombatMechanics.h"
#include "Combat/LDCombatSourceRules.h"
#include "Combat/LDControlRules.h"
#include "Combat/LDTacticalRules.h"
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDLifeAuthority.h"
#include "Engine/World.h"

// #112 §5 mechanics on Combat's body records; the pure rules live in LDCombatMechanics.cpp.
namespace LDCombatMechanicsDetail
{
    double Cm(const FLDDemoRational& Value)
    { return Value.Denominator>0 ? double(Value.Numerator)/double(Value.Denominator) : 0.; }

    const FLDDemoEffectDefinition* FindOp(const FLDDemoAbilityValues& Values, ELDDemoEffectOp Op)
    { return Values.Effects.FindByPredicate([Op](const FLDDemoEffectDefinition& Effect) { return Effect.GetOp()==Op; }); }

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
    const FGuid& RootEffectId, FString* OutError)
{
    if (OutApplied) { *OutApplied=0; }
    if (OutError) { OutError->Reset(); }
    if (Body.bPendingDeath || Body.Resources.HpP2<=0 || AmountP2<=0) { return false; }
    // Spec §8.1: a live rot cuts every new heal once, before it lands.
    if (const ILDLifeAuthority* Mode=Authority()) { AmountP2=LDCombatElements::AfterRotP2(AmountP2,Body.ElementMark,Mode->GetAuthorityNowMs()); }
    if (AmountP2<=0) { return false; }
    auto& Pool=Body.Resources;
    const int64 Before=Pool.HpP2;
    Pool.HpP2=AmountP2>=Pool.MaxHpP2-Pool.HpP2 ? Pool.MaxHpP2 : Pool.HpP2+AmountP2;
    if (Pool.HpP2>=Pool.MaxHpP2) { Body.HpRegenMicroP2=0; }
    const int64 Healed=Pool.HpP2-Before;
    const int64 NowMs=Authority() ? Authority()->GetAuthorityNowMs() : 0;
    // Y14: the overflow of this heal becomes a shield; Y19 counts what really healed.
    {
        FString CardError;
        if (!OverhealShield(Body,AmountP2-Healed,NowMs,CardError))
        {
            if (OutError) { *OutError=CardError; }
            return false;
        }
    }
    if (Healed==0) { return false; }
    if (OutApplied) { *OutApplied=Healed; }
    if (FLDCombatEntityRecord* Record=FindRecord(Body.Victim.EntityId); Record && Body.Victim.Kind==ELDVictimKind::Contender)
    {
        TArray<FLDPassiveEffectRef> Fat;
        FString CardError;
        if (!CollectPassiveEffects(Body.AttributeProjection,ELDDemoEffectOp::HealGrowth,Fat,CardError)
            || (!Fat.IsEmpty() && (Record->CardGrowth.HealedP2+=Healed, !RefreshCardGrowth(Body.Victim.EntityId,NowMs,CardError))))
        {
            if (OutError) { *OutError=CardError; }
            return false;
        }
    }
    ++Pool.Revision;
    MarkViewChanged(Body.Victim.EntityId);
    LedgerHealed(Body,Pool.HpP2-Before);
    return true;
}

int32 ALDCombatDirector::SpendShieldLayers(FLDBodyCombatState& Body, int64 AmountP2, TArray<FGuid>* OutEmptied)
{
    int32 Emptied=0;
    Body.ShieldLayers.Sort([](const FLDBodyCombatState::FShieldLayer& A, const FLDBodyCombatState::FShieldLayer& B)
        { return A.ExpiresAtMs<B.ExpiresAtMs; });
    for (FLDBodyCombatState::FShieldLayer& Layer : Body.ShieldLayers)
    {
        if (AmountP2<=0) { break; }
        const int64 Taken=FMath::Min(AmountP2,Layer.AmountP2);
        Layer.AmountP2-=Taken;
        AmountP2-=Taken;
        if (Layer.AmountP2<=0 && Taken>0)
        {
            ++Emptied;
            if (OutEmptied) { OutEmptied->Add(Layer.EffectId); }
        }
    }
    Body.ShieldLayers.RemoveAll([](const FLDBodyCombatState::FShieldLayer& Layer) { return Layer.AmountP2<=0; });
    return Emptied;
}

bool ALDCombatDirector::ExpireShieldLayers(FLDBodyCombatState& Body, int64 NowMs)
{
    int64 Lapsed=0;
    const int32 Before=Body.ShieldLayers.Num();
    Body.ShieldLayers.RemoveAll([NowMs,&Lapsed](const FLDBodyCombatState::FShieldLayer& Layer)
        { if (Layer.ExpiresAtMs>NowMs) { return false; } Lapsed+=Layer.AmountP2; return true; });
    if (Body.ShieldLayers.Num()==Before) { return false; }
    Body.Resources.ShieldP2=FMath::Max<int64>(0,Body.Resources.ShieldP2-Lapsed);
    return true;
}

bool ALDCombatDirector::GrantShieldTo(FLDBodyCombatState& Body, int64 AmountP2, int64 DurationMs, const FGuid& EffectId,
    const FGuid& SourceEntity, int64 NowMs, FString& Error, const FLDCombatSourceRef& GrantSource)
{
    if (Body.bPendingDeath) { return true; }
    // Spec §8.1: a live rot cuts the new shield before the replacement check; existing shields are untouched.
    AmountP2=LDCombatElements::AfterRotP2(AmountP2,Body.ElementMark,NowMs);
    int64 Expires=0;
    if (!LDMatchLifecycle::AddDurationMs(NowMs,DurationMs,Expires,Error)) { return false; }
    // A weaker shield is a stated sub-effect outcome, not a failed release.
    if (!LDTactical::ApplyShield(Body.Shield,Body.Resources.ShieldP2,FLDCombatShield{EffectId,SourceEntity,Expires},AmountP2,NowMs))
    { return true; }
    Body.ShieldGrantedP2=Body.Resources.ShieldP2;
    Body.LastShieldGrantP2=AmountP2;
    // Layers left over from an expired record are gone with it; this grant is its own layer.
    if (Body.Resources.ShieldP2==AmountP2) { Body.ShieldLayers.Reset(); }
    Body.ShieldLayers.Add({EffectId,Expires,AmountP2});
    ++Body.Resources.Revision;
    ++Body.StateRevision;
    MarkViewChanged(Body.Victim.EntityId);
    LedgerShieldGranted(Body,AmountP2);
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
    App.Source=Hit.KnockbackSource;
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
    // Spec §8.3: a body on an enemy's ice slides GROUND.IceDisplacementScale as far.
    double PushCm=Hit.KnockbackCm;
    if (Body.bOnEnemyIce)
    {
        FLDGroundTerms Ground;
        if (!GroundTerms(Ground,Error)) { return false; }
        PushCm*=double(Ground.IceDisplacementScaleBp)/10000.;
    }
    if (!Movement->StartDemoDash(Direction,PushCm,double(Hit.KnockbackMs)/1000.,Error,true))
    { Error=TEXT("Knockback: ")+Error; return false; }
    if (!WatchKnockbackImpact(Target,Body,Hit,Direction,NowMs,Error)) { return false; }
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
                if (!EmitZonePulse(Zone,Error)) { Error=TEXT("Zone pulse: ")+Error; return false; }
            }
            // S20 A2: the owner standing inside gains momentum every pulse.
            if (Zone.MomentumP2>0 && Holder && HolderActor && !Holder->bPendingDeath
                && FVector::Dist2D(Zone.CentreCm,FootOf(*HolderActor))<=Zone.RadiusCm)
            { GrantMomentum(*Holder,Zone.MomentumP2,ELDMomentumBook::Skill); }
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
        // #198 zone_haste: the owner's move speed while its foot stands inside.
        if (Zone.HasteBp>0) { UpdateZoneHaste(Zone,NowMs); }
        if (Zone.EndMs<=NowMs)
        {
            Zones.RemoveAt(Index);
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
    const FVector& OriginFoot, const FVector& TargetPoint, const FVector& ReleaseDirection, const FGuid& EffectId, uint64 Token,
    TSharedPtr<FLDCombatCastLedger> CastLedger, FString& Error)
{
    using namespace LDCombatMechanicsDetail;
    ILDLifeAuthority* Mode=Authority();
    if (!Mode) { Error=TEXT("Release mechanics require the current core."); return false; }
    const int64 Now=Mode->GetAuthorityNowMs();
    const FLDDemoAbilityValues& Values=Terms.Values;
    const FLDDemoEffectDefinition* ZoneDef=FindOp(Values,ELDDemoEffectOp::Zone);
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
        // A branch tether: the zone rides on the caster's foot (its radius already scaled by CompileActive).
        if (Terms.ZoneTether.IsSet())
        {
            Zone.bTether=true;
            Zone.CentreCm=OriginFoot;
        }
        Zone.StartMs=Now;
        Zone.DelayMs=ZoneDef->DelayMs;
        Zone.IntervalMs=ZoneDef->IntervalMs;
        Zone.Count=int32(ZoneDef->Count);
        if (!LDMatchLifecycle::AddDurationMs(Now,ZoneDef->DurationMs,Zone.EndMs,Error)) { return false; }
        Zone.SourceADP2=Body.AttributeProjection.Final.ADP2;
        Zone.SourceAPP2=Body.AttributeProjection.Final.APP2;
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
        // Skill branches: the owner's cut and momentum inside (S08 A2, S20 A2).
        if (const FLDDemoEffectDefinition* Guard=FindOp(Values,ELDDemoEffectOp::ZoneGuard)) { Zone.GuardBp=Guard->RatioBp; }
        if (const FLDDemoEffectDefinition* Gain=FindOp(Values,ELDDemoEffectOp::ZoneMomentum)) { Zone.MomentumP2=Gain->BaseP2; }
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

bool ALDCombatDirector::EmitZonePulse(const FLDCombatZone& Zone, FString& Error)
{
    const FLDBodyCombatState* Holder=FindBody(Zone.SourceBody);
    TArray<FLDCombatBodyRef> Targets;
    if (!CollectAreaTargets(Zone.CentreCm,Zone.RadiusCm,Holder,Zone.SourceEntity,Targets,Error)) { return false; }
    for (const FLDCombatBodyRef& Target : Targets)
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
        Hit.BaseP2=Zone.BaseP2;
        Hit.ADRatioBp=Zone.ADRatioBp;
        Hit.APRatioBp=Zone.APRatioBp;
        Hit.SourceADP2=Zone.SourceADP2;
        Hit.SourceAPP2=Zone.SourceAPP2;
        Hit.DamageType=Zone.DamageType;
        Hit.bPeriodic=true;
        Hit.bDirectAbilityDamage=Zone.bDamage;
        Hit.PackageScaleBp=Zone.PackageScaleBp;
        if (Zone.bWet) { Hit.NativeElements|=LDCombatElements::Bit(ELDCombatElement::Water); }
        Hit.BurnStacks=Zone.PulseBurnStacks;
        Hit.ElementLevel=Zone.ElementLevel;
        Hit.ElementAPP2=Zone.ElementAPP2;
        Hit.SlowBp=Zone.PulseSlowBp;
        Hit.SlowDurationMs=Zone.PulseSlowMs;
        Hit.CastLedger=Zone.CastLedger;
    }
    return true;
}
