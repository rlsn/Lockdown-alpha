#include "Combat/LDCombatDirector.h"
#include "Core/LDLog.h"
#include "Combat/LDCombatKeywords.h"
#include "Combat/LDCombatElements.h"
#include "Combat/LDCombatSourceRules.h"
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDGameMode.h"
#include "Region/LDMapRegistry.h"
#include "Vision/LDVisionDirector.h"
#include "Engine/World.h"

// #198 F mods M01-M12 and connectors C01-C12 on Combat's records (docs/systems/COMBAT_AND_GROWTH.md, "改装件与连接件"). A mod
// or connector is an installed component whose target (an active, a component, the innate D or the holder) names the exit
// it reads; FLDPassiveEffectRef::Targets matches that exit against a hit, a control or a release source.
namespace LDCombatWorkshopDetail
{
    // floor(Value x Bp / 10000) for nonnegative inputs, without overflow for any P2 or ms value.
    int64 Share(int64 Value, int64 Bp)
    {
        if (Value<=0 || Bp<=0) { return 0; }
        return Value/10000*Bp+Value%10000*Bp/10000;
    }

    double Cm(const FLDDemoRational& Value) { return Value.Denominator>0 ? double(Value.Numerator)/double(Value.Denominator) : 0.; }

    FLDDemoRational Reduce(int64 Numerator, int64 Denominator)
    {
        int64 A=FMath::Abs(Numerator), B=FMath::Abs(Denominator);
        while (B!=0) { const int64 T=A%B; A=B; B=T; }
        return A>1 ? FLDDemoRational{Numerator/A,Denominator/A} : FLDDemoRational{Numerator,Denominator};
    }

    FLDDemoRational ScaleRational(const FLDDemoRational& Value, int64 Bp) { return Reduce(Value.Numerator*Bp,Value.Denominator*10000); }

    // A ≤ B for two rationals with positive denominators.
    bool RationalLE(const FLDDemoRational& A, const FLDDemoRational& B) { return A.Numerator*B.Denominator<=B.Numerator*A.Denominator; }

    // One skill-class hit of a body's component with the body's current AD/AP as its source values.
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

    void Remember(TArray<FLDCombatBodyMark>& Marks, const FLDCombatBodyRef& Body, int64 AtMs)
    {
        Marks.RemoveAll([&Body](const FLDCombatBodyMark& Row) { return Row.Body==Body; });
        Marks.Add({Body,AtMs});
        // The newest few are enough for "the most recent legal one".
        while (Marks.Num()>8) { Marks.RemoveAt(0); }
    }

    // M18/M20 come before M03 so its scale reaches their pushes too.
    constexpr ELDDemoEffectOp ActiveMods[]={ELDDemoEffectOp::FinalKnockback,ELDDemoEffectOp::Pull,ELDDemoEffectOp::KnockbackScale,ELDDemoEffectOp::ControlScale,ELDDemoEffectOp::RangeBonus,
        ELDDemoEffectOp::CooldownCost,ELDDemoEffectOp::CompressCone,ELDDemoEffectOp::CompressZone,ELDDemoEffectOp::DashScale,
        ELDDemoEffectOp::SpreadUnit,ELDDemoEffectOp::SpreadProjectile,ELDDemoEffectOp::ChainExtra,ELDDemoEffectOp::CompressChain,
        ELDDemoEffectOp::ProjectileReturn,ELDDemoEffectOp::ProjectilePierce,ELDDemoEffectOp::EchoImpact,ELDDemoEffectOp::ZoneTether,
        ELDDemoEffectOp::DefenseShove,ELDDemoEffectOp::EmpowerReach};
}

bool ALDCombatDirector::WorkshopOn(const FLDAttributeProjection& Projection, ELDDemoEffectOp Op, const FLDCombatSourceRef& Exit,
    TArray<FLDPassiveEffectRef>& Out, FString& Error) const
{
    if (!CollectPassiveEffects(Projection,Op,Out,Error)) { return false; }
    Out.RemoveAll([&Exit](const FLDPassiveEffectRef& Ref) { return !Ref.Targets(Exit); });
    return true;
}

bool ALDCombatDirector::CompileActive(const FLDAttributeProjection& Projection, const FLDCombatSourceRef& Source,
    FLDCombatActiveTerms& Terms, FString& Error) const
{
    using namespace LDCombatWorkshopDetail;
    if (Source.Kind!=ELDCombatSourceKind::Learned && Source.Kind!=ELDCombatSourceKind::Innate) { return true; }
    FLDDemoAbilityValues& Values=Terms.Values;
    const auto Has=[&Values](ELDDemoEffectOp Op)
    { return Values.Effects.ContainsByPredicate([Op](const FLDDemoEffectDefinition& Effect) { return Effect.GetOp()==Op; }); };
    const auto ScaleCooldown=[&Values,&Terms,&Source](int64 Bp)
    {
        if (Source.Kind==ELDCombatSourceKind::Innate) { Terms.CooldownScaleBp=Share(Terms.CooldownScaleBp,Bp); }
        else { Values.CooldownMs=Share(Values.CooldownMs,Bp); }
    };
    // Spec §12.3: eligibility first, then coverage/range/motion, then timing, then cost/multipliers; every mod on this exit.
    for (const ELDDemoEffectOp Op : ActiveMods)
    {
        TArray<FLDPassiveEffectRef> Mods;
        if (!WorkshopOn(Projection,Op,Source,Mods,Error)) { return false; }
        for (const FLDPassiveEffectRef& Mod : Mods)
        {
            const FLDDemoEffectDefinition& Effect=Mod.Effect;
            switch (Op)
            {
            case ELDDemoEffectOp::KnockbackScale:
                // M03: a farther, slower push for a lighter native hit; the C02 impact package is its own and unchanged.
                for (FLDDemoEffectDefinition& Push : Values.Effects)
                {
                    if (Push.GetOp()!=ELDDemoEffectOp::Knockback) { continue; }
                    Push.DistanceCm=ScaleRational(Push.DistanceCm,Effect.RatioBp);
                    Push.DurationMs=Share(Push.DurationMs,Effect.RatioBp);
                }
                for (TOptional<FLDDemoEffectDefinition>* Push : {&Terms.FinalKnockbackTerms,&Terms.PullTerms})
                {
                    if (!Push->IsSet()) { continue; }
                    (*Push)->DistanceCm=ScaleRational((*Push)->DistanceCm,Effect.RatioBp);
                    (*Push)->DurationMs=Share((*Push)->DurationMs,Effect.RatioBp);
                }
                // On S17 (with M18) the lighter hit is its empowered package.
                for (FLDDemoEffectDefinition& Package : Values.Effects)
                {
                    if (Package.GetOp()!=ELDDemoEffectOp::EmpowerBasic || !Terms.FinalKnockbackTerms.IsSet()) { continue; }
                    Package.BaseP2=Share(Package.BaseP2,Effect.MultiplierBp);
                    Package.ADRatioBp=Share(Package.ADRatioBp,Effect.MultiplierBp);
                    Package.APRatioBp=Share(Package.APRatioBp,Effect.MultiplierBp);
                }
                Terms.PackageScaleBp=Share(Terms.PackageScaleBp,Effect.MultiplierBp);
                break;
            case ELDDemoEffectOp::ControlScale:
                // M04: the active's native hard controls last longer; its base cooldown grows. Diminishing still applies.
                for (FLDDemoEffectDefinition& Control : Values.Effects)
                {
                    const ELDDemoEffectOp Kind=Control.GetOp();
                    if (Kind==ELDDemoEffectOp::Root || Kind==ELDDemoEffectOp::Stun || Kind==ELDDemoEffectOp::Silence
                        || Kind==ELDDemoEffectOp::Disarm || Kind==ELDDemoEffectOp::Taunt)
                    { Control.DurationMs=Share(Control.DurationMs,Effect.RatioBp); }
                }
                ScaleCooldown(Effect.SecondaryRatioBp);
                break;
            case ELDDemoEffectOp::RangeBonus:
            {
                // M05: the initial reach grows up to the cap (never shrinks a reach already past it); no AOE grows.
                const FLDDemoRational& Range=Terms.Target.RangeCm;
                const FLDDemoRational& Add=Effect.DistanceCm;
                if (RationalLE(Range,Effect.RadiusCm))
                {
                    const FLDDemoRational Longer=Reduce(Range.Numerator*Add.Denominator+Add.Numerator*Range.Denominator,
                        Range.Denominator*Add.Denominator);
                    Terms.Target.RangeCm=RationalLE(Longer,Effect.RadiusCm) ? Longer : Effect.RadiusCm;
                }
                Terms.PackageScaleBp=Share(Terms.PackageScaleBp,Effect.MultiplierBp);
                break;
            }
            case ELDDemoEffectOp::CooldownCost:
            {
                // M06: a shorter base cooldown for a dearer cast, the cost rounded up to the next 0.01 MP.
                ScaleCooldown(Effect.SecondaryRatioBp);
                const int64 Cost=Values.ManaCostP2;
                Values.ManaCostP2=Cost/10000*Effect.CostRatioBp+(Cost%10000*Effect.CostRatioBp+9999)/10000;
                break;
            }
            case ELDDemoEffectOp::CompressCone:
                if (Has(ELDDemoEffectOp::ChannelCone)) { Terms.CompressCone=Effect; }
                break;
            case ELDDemoEffectOp::CompressZone:
                if (Has(ELDDemoEffectOp::Zone) && Has(ELDDemoEffectOp::ZoneDamage)) { Terms.CompressZone=Effect; }
                break;
            case ELDDemoEffectOp::ChainExtra:
                // M14 on S10: more hops at the authored kept share; compiled before M07 so a compression sums them too.
                for (FLDDemoEffectDefinition& Bounce : Values.Effects)
                {
                    if (Bounce.GetOp()!=ELDDemoEffectOp::ChainBounce) { continue; }
                    Bounce.Count+=Effect.Count;
                    Terms.ChainExtra=Mod;
                }
                break;
            case ELDDemoEffectOp::CompressChain:
                // M07 on S10: the first body takes the planned hops' summed share (1 + r + r^2 ...) times the multiplier.
                for (const FLDDemoEffectDefinition& Bounce : Values.Effects)
                {
                    if (Bounce.GetOp()!=ELDDemoEffectOp::ChainBounce) { continue; }
                    int64 Total=10000, Kept=10000;
                    for (int32 Hop=0; Hop<Bounce.Count; ++Hop) { Kept=Share(Kept,Bounce.RatioBp); Total+=Kept; }
                    Terms.CompressChain=Effect;
                    Terms.PackageScaleBp=Share(Terms.PackageScaleBp,Share(Total,Effect.MultiplierBp));
                }
                break;
            case ELDDemoEffectOp::DashScale:
                // M08/M09: distance and time scale together (speed kept); the cooldown and the contact package follow.
                for (FLDDemoEffectDefinition& Dash : Values.Effects)
                {
                    if (Dash.GetOp()!=ELDDemoEffectOp::Dash) { continue; }
                    Dash.DistanceCm=ScaleRational(Dash.DistanceCm,Effect.RatioBp);
                    Dash.DurationMs=Share(Dash.DurationMs,Effect.RatioBp);
                }
                if (Source.Kind==ELDCombatSourceKind::Innate) { Terms.DashScaleBp=Share(Terms.DashScaleBp,Effect.RatioBp); }
                ScaleCooldown(Effect.SecondaryRatioBp);
                Terms.ContactScaleBp=Share(Terms.ContactScaleBp,Effect.MultiplierBp);
                break;
            case ELDDemoEffectOp::SpreadUnit:
                if (Terms.Target.Mode==ELDDemoTargetMode::Unit && Has(ELDDemoEffectOp::Damage))
                {
                    Terms.SpreadUnit=Effect;
                    Terms.PackageScaleBp=Share(Terms.PackageScaleBp,Effect.MultiplierBp);
                }
                break;
            case ELDDemoEffectOp::SpreadProjectile:
                if (Terms.Target.Mode==ELDDemoTargetMode::DirectionProjectile && Has(ELDDemoEffectOp::Damage))
                {
                    Terms.SpreadProjectile=Effect;
                    Terms.PackageScaleBp=Share(Terms.PackageScaleBp,Effect.MultiplierBp);
                }
                break;
            case ELDDemoEffectOp::ProjectileReturn:
                if (Terms.Target.Mode==ELDDemoTargetMode::DirectionProjectile && Has(ELDDemoEffectOp::Damage)) { Terms.ProjectileReturn=Mod; }
                break;
            case ELDDemoEffectOp::ProjectilePierce:
                if (Terms.Target.Mode==ELDDemoTargetMode::DirectionProjectile && Has(ELDDemoEffectOp::Damage)) { Terms.ProjectilePierce=Mod; }
                break;
            case ELDDemoEffectOp::EchoImpact:
                // M15: S07's self blast, S18's impact or S19's trap blast repeats once; the host's base cooldown grows.
                if (Has(ELDDemoEffectOp::Damage) && (Has(ELDDemoEffectOp::SelfArea) || Has(ELDDemoEffectOp::GroundImpact)
                    || Has(ELDDemoEffectOp::ContactTrap)))
                {
                    Terms.EchoImpact=Mod;
                    ScaleCooldown(Effect.SecondaryRatioBp);
                }
                break;
            case ELDDemoEffectOp::FinalKnockback:
                // M18: S17's last package or S22's final wave pushes; S17 pays with its attack-speed bonus.
                if (Has(ELDDemoEffectOp::EmpowerBasic) || Has(ELDDemoEffectOp::ChannelCone))
                {
                    Terms.FinalKnockback=Mod;
                    Terms.FinalKnockbackTerms=Effect;
                    for (FLDDemoEffectDefinition& Package : Values.Effects)
                    {
                        if (Package.GetOp()!=ELDDemoEffectOp::EmpowerBasic) { continue; }
                        for (FLDDemoStatBonus& Stat : Package.Stats)
                        {
                            if (Stat.Stat==ELDDemoBonusStat::AttackSpeedBonusRatio) { Stat.Amount=Share(Stat.Amount,Effect.CostRatioBp); }
                        }
                    }
                }
                break;
            case ELDDemoEffectOp::Pull:
                // M20: S01/S09's push turns into a pull; S15's landed taunts pull too.
                if (Has(ELDDemoEffectOp::Knockback) || Has(ELDDemoEffectOp::Taunt))
                {
                    Terms.Pull=Mod;
                    Terms.PullTerms=Effect;
                }
                break;
            case ELDDemoEffectOp::ZoneTether:
                // M16: the zone follows the caster at a smaller radius.
                for (FLDDemoEffectDefinition& Zone : Values.Effects)
                {
                    if (Zone.GetOp()!=ELDDemoEffectOp::Zone) { continue; }
                    Zone.RadiusCm=ScaleRational(Zone.RadiusCm,Effect.RatioBp);
                    Terms.ZoneTether=Mod;
                }
                break;
            case ELDDemoEffectOp::DefenseShove:
                // M17: a weaker shield (S13/S14) whose break shoves; S16's answer shoves without a shield change.
                for (FLDDemoEffectDefinition& Shield : Values.Effects)
                {
                    if (Shield.GetOp()!=ELDDemoEffectOp::Shield) { continue; }
                    Shield.BaseP2=Share(Shield.BaseP2,Effect.RatioBp);
                    Shield.APRatioBp=Share(Shield.APRatioBp,Effect.RatioBp);
                    Shield.MaxHPRatioBp=Share(Shield.MaxHPRatioBp,Effect.RatioBp);
                }
                if (Has(ELDDemoEffectOp::Shield) || Has(ELDDemoEffectOp::Brace)) { Terms.DefenseShove=Mod; }
                break;
            case ELDDemoEffectOp::EmpowerReach:
                // M19: the packages are weaker (at arm); the arm adds the range as an AttackRangeCmFlat stat on the charge bonus.
                if (Has(ELDDemoEffectOp::EmpowerBasic)) { Terms.EmpowerReach=Mod; }
                break;
            default:
                break;
            }
        }
    }
    if (Values.CooldownMs<=0 || Terms.PackageScaleBp<=0 || Terms.ContactScaleBp<=0 || Terms.DashScaleBp<=0 || Terms.CooldownScaleBp<=0)
    {
        Error=FString::Printf(TEXT("The mods on %s compile to a non-positive cooldown or multiplier."),*LDLog::Source(Source));
        return false;
    }
    return true;
}

bool ALDCombatDirector::SpreadUnitHits(const FLDBodyCombatState& Body, const FLDCombatActiveTerms& Terms, const FVector& Origin,
    const FVector& PrimaryFoot, const FLDCombatBodyRef& Primary, const FLDCombatHit& Template, FString& Error)
{
    double RangeCm=0;
    if (!CastRangeCm(Body,Terms.Target,RangeCm,Error)) { return false; }
    const FVector Facing=(PrimaryFoot-Origin).GetSafeNormal2D();
    const double CosHalf=FMath::Cos(FMath::DegreesToRadians(LDCombatWorkshopDetail::Cm(Terms.SpreadUnit->AngleDegrees)*.5));
    TArray<FLDCombatBodyRef> Around;
    if (!CollectAreaTargets(Origin,RangeCm,&Body,Body.Victim.EntityId,Around,Error)) { return false; }
    bool bSpread=false;
    for (const FLDCombatBodyRef& Other : Around)
    {
        ALDCharacter* OtherActor=nullptr;
        if (Other==Primary || !FindBody(Other,&OtherActor) || !OtherActor) { continue; }
        const FVector Toward=(FootOf(*OtherActor)-Origin).GetSafeNormal2D();
        if (!Facing.IsNearlyZero() && !Toward.IsNearlyZero() && FVector::DotProduct(Facing,Toward)<CosHalf) { continue; }
        PendingHits.Add(SpreadCopy(Template,Other));
        bSpread=true;
    }
    // §10: M01 is one trigger per release whose cone reached another body.
    TArray<FLDPassiveEffectRef> Spreads;
    if (bSpread && !WorkshopOn(Body.AttributeProjection,ELDDemoEffectOp::SpreadUnit,Template.Source,Spreads,Error)) { return false; }
    if (!Spreads.IsEmpty()) { LedgerComponentTriggered(Body,Spreads[0].Source.DefinitionId); }
    return true;
}

bool ALDCombatDirector::TriggerAftershock(const FLDCombatBodyRef& OwnerRef, const FLDCombatSourceRef& Exit, const FGuid& EffectId,
    const FVector& Centre, double RadiusCm, int64 NowMs, FString& Error)
{
    const FLDBodyCombatState* Holder=FindBody(OwnerRef);
    if (!Holder || Holder->bPendingDeath) { return true; }
    TArray<FLDPassiveEffectRef> Shocks;
    if (!WorkshopOn(Holder->AttributeProjection,ELDDemoEffectOp::Aftershock,Exit,Shocks,Error)) { return false; }
    for (const FLDPassiveEffectRef& Shock : Shocks)
    {
        // C06: the same place and outline, once, a skill hit with its root; never a new natural completion itself.
        FLDCombatGroundEffect Blast;
        Blast.Payload=LDCombatWorkshopDetail::MakeHit(*Holder,Shock.Source,FLDCombatBodyRef(),EffectId);
        Blast.Payload.BaseP2=Shock.Effect.BaseP2;
        Blast.Payload.ADRatioBp=Shock.Effect.ADRatioBp;
        Blast.Payload.APRatioBp=Shock.Effect.APRatioBp;
        Blast.Payload.DamageType=Shock.Effect.DamageType;
        Blast.Payload.RootDurationMs=Shock.Effect.SecondaryDurationMs;
        if (!StampSourceTerms(*Holder,Blast.Payload,Error)) { return false; }
        Blast.CentreCm=Centre;
        Blast.RadiusCm=Blast.InnerRadiusCm=RadiusCm;
        Blast.Origin=ELDCombatGroundOrigin::Aftershock;
        if (!LDMatchLifecycle::AddDurationMs(NowMs,Shock.Effect.DelayMs,Blast.ReadyAtMs,Error)) { return false; }
        Blast.ExpiresAtMs=Blast.ReadyAtMs;
        GroundEffects.Add(MoveTemp(Blast));
        // §10: C06 is one trigger when its blast is released (not per struck body).
        LedgerComponentTriggered(*Holder,Shock.Source.DefinitionId);
        UE_LOG(LogLDCombat,Log,TEXT("LD_COMBAT_AFTERSHOCK t=%lld owner=%s exit=%s radius=%.0f"),NowMs,*LDLog::Id(OwnerRef.Victim.EntityId),
            *LDLog::Source(Exit),RadiusCm);
    }
    return true;
}

bool ALDCombatDirector::ScheduleEcho(const FLDCombatBodyRef& OwnerRef, const FLDPassiveEffectRef& Echo,
    const FLDCombatGroundEffect& Blast, int64 NowMs, FString& Error)
{
    const FLDBodyCombatState* Holder=FindBody(OwnerRef);
    if (!Holder || Holder->bPendingDeath) { return true; }
    // M15 (spec §4): the same release's hit at the same place and outline, once, at its share of the package. It is a
    // derived host hit: it never echoes again and C06 never answers it (ELDCombatGroundOrigin::Echo).
    FLDCombatGroundEffect Repeat;
    Repeat.Payload=Blast.Payload;
    Repeat.Payload.DeliveryScaleBp=LDCombatWorkshopDetail::Share(Blast.Payload.DeliveryScaleBp,Echo.Effect.MultiplierBp);
    Repeat.CentreCm=Blast.CentreCm;
    Repeat.RadiusCm=Blast.RadiusCm;
    Repeat.InnerRadiusCm=Blast.InnerRadiusCm;
    Repeat.OuterRatioBp=Blast.OuterRatioBp;
    Repeat.Origin=ELDCombatGroundOrigin::Echo;
    Repeat.ObservedKey=FGuid::NewGuid();
    if (Repeat.Payload.DeliveryScaleBp<=0 || !LDMatchLifecycle::AddDurationMs(NowMs,Echo.Effect.DelayMs,Repeat.ReadyAtMs,Error))
    {
        if (Error.IsEmpty()) { Error=TEXT("An echo needs a positive share."); }
        return false;
    }
    Repeat.ExpiresAtMs=Repeat.ReadyAtMs;
    GroundEffects.Add(MoveTemp(Repeat));
    LedgerComponentTriggered(*Holder,Echo.Source.DefinitionId);
    UE_LOG(LogLDCombat,Log,TEXT("LD_COMBAT_ECHO t=%lld owner=%s exit=%s at=%lld share_bp=%lld radius=%.0f"),NowMs,
        *LDLog::Id(OwnerRef.Victim.EntityId),*LDLog::Source(Blast.Payload.Source),NowMs+Echo.Effect.DelayMs,Echo.Effect.MultiplierBp,
        Blast.RadiusCm);
    return true;
}

bool ALDCombatDirector::LaunchReturn(const FLDCombatProjectile& Outbound, const FVector& EndCm, const FGuid& EndBody, FString& Error)
{
    ALDCharacter* ThrowerActor=nullptr;
    const FLDBodyCombatState* Thrower=FindBody(Outbound.SourceBody,&ThrowerActor);
    if (!Thrower || !ThrowerActor || Thrower->bPendingDeath) { return true; }
    const FVector Foot=FootOf(*ThrowerActor);
    const FVector2D Toward(Foot.X-EndCm.X,Foot.Y-EndCm.Y);
    const double Length=Toward.Size();
    if (!FMath::IsFinite(Length)) { Error=TEXT("A return flight has no finite length."); return false; }
    if (Length<=UE_KINDA_SMALL_NUMBER) { return true; }
    // M13 (spec §4): straight and fixed toward the thrower's foot now, as long as that distance, at the same speed and
    // radius; it carries the payload's damage share, elements, burn, breach and grievous but no slow or control.
    // Outbound lives in Projectiles, which the new flight may reallocate: everything is read before the add.
    FLDCombatProjectile Return=Outbound;
    const FName Component=Outbound.ReturnComponent;
    const int64 ShareBp=Outbound.ReturnScaleBp;
    Return.FlightId=FGuid::NewGuid();
    Return.AdvancedThroughMs=Core() ? Core()->GetAuthorityNowMs() : Outbound.AdvancedThroughMs;
    Return.OriginCm=FVector(EndCm.X,EndCm.Y,Outbound.OriginCm.Z);
    Return.Direction=FVector(Toward.X/Length,Toward.Y/Length,0);
    Return.RangeCm=Length;
    Return.TravelledCm=0;
    Return.SlowBp=Return.SlowDurationMs=0;
    Return.SpreadRadiusCm=0;
    Return.PierceRemaining=0;
    Return.PierceScaleBp=Return.ReturnScaleBp=0;
    Return.bReturn=true;
    Return.Struck.Reset();
    Return.StartContact=EndBody;
    FLDCombatHit& Payload=Return.Payload;
    Payload.DeliveryScaleBp=LDCombatWorkshopDetail::Share(Payload.DeliveryScaleBp,Outbound.ReturnScaleBp);
    Payload.RootDurationMs=Payload.SlowBp=Payload.SlowDurationMs=Payload.DisarmDurationMs=Payload.StunDurationMs=0;
    Payload.SilenceDurationMs=Payload.AttackSlowBp=Payload.AttackSlowMs=Payload.KnockbackMs=0;
    Payload.KnockbackCm=0;
    Payload.KnockbackSource=FLDCombatSourceRef();
    if (Payload.DeliveryScaleBp<=0) { Error=TEXT("A return flight needs a positive share."); return false; }
    UE_LOG(LogLDCombat,Log,TEXT("LD_COMBAT_PROJECTILE_RETURN owner=%s exit=%s length=%.0f share_bp=%lld"),
        *LDLog::Id(Return.SourceEntity),*LDLog::Source(Return.Source),Length,ShareBp);
    Projectiles.Add(MoveTemp(Return));
    LedgerComponentTriggered(*Thrower,Component);
    return true;
}

bool ALDCombatDirector::ArmReactionEmpower(const FLDCombatReactionFact& Fact, int64 NowMs, FString& Error)
{
    FLDBodyCombatState* Trigger=FindBody(Fact.TriggerBody);
    if (!Trigger || Trigger->bPendingDeath) { return true; }
    TArray<FLDPassiveEffectRef> Chases;
    if (!WorkshopOn(Trigger->AttributeProjection,ELDDemoEffectOp::ReactionEmpower,Fact.TriggerSource,Chases,Error)) { return false; }
    for (const FLDPassiveEffectRef& Chase : Chases)
    {
        // One package per reaction (not per struck body); a later reaction replaces the unused one of this connector.
        FLDCombatEmpower Package;
        if (!MakeEmpower(*Trigger,Chase.Source,Chase.Effect,FGuid::NewGuid(),NowMs,Package,Error)) { return false; }
        LDCombatControl::ArmEmpower(Trigger->Empowers,Package);
        ++Trigger->StateRevision;
        MarkViewChanged(Trigger->Victim.EntityId);
        UE_LOG(LogLDCombat,Log,TEXT("LD_COMBAT_REACTION_EMPOWER t=%lld holder=%s via=%s amount=%s"),NowMs,*LDLog::Id(Trigger->Victim.EntityId),
            *LDLog::Source(Fact.TriggerSource),*LDLog::P2(Package.AmountP2));
    }
    return true;
}

bool ALDCombatDirector::OnHardControl(FLDBodyCombatState& Victim, const FLDControlApplication& App, int64 NowMs, FString& Error)
{
    FLDBodyCombatState* Holder=FindBody(App.SourceBody);
    if (!Holder || Holder==&Victim || Holder->bPendingDeath) { return true; }
    const FLDCombatBodyRef VictimRef{Victim.Victim,Victim.Identity};
    Holder->ControlMarks.RemoveAll([NowMs](const FLDCombatControlMark& Mark) { return Mark.ExpiresAtMs<=NowMs; });
    // C05 (spec §11.3): another kind of hard control from the holder consumes the mark for one added physical hit; the
    // same fact never sets a new mark after consuming one.
    TArray<FLDPassiveEffectRef> Marks;
    if (!CollectPassiveEffects(Holder->AttributeProjection,ELDDemoEffectOp::ControlMark,Marks,Error)) { return false; }
    bool bConsumed=false;
    for (int32 Index=Holder->ControlMarks.Num()-1; Index>=0; --Index)
    {
        const FLDCombatControlMark Mark=Holder->ControlMarks[Index];
        if (Mark.Target!=VictimRef || Mark.Kind==App.Kind) { continue; }
        Holder->ControlMarks.RemoveAt(Index);
        const FLDPassiveEffectRef* Installed=Marks.FindByPredicate([&Mark](const FLDPassiveEffectRef& Ref)
        { return Ref.Source.DefinitionId==Mark.Component; });
        if (!Installed) { continue; }
        FLDCombatHit Hit=LDCombatWorkshopDetail::MakeHit(*Holder,Installed->Source,VictimRef,App.EffectId);
        Hit.BaseP2=Installed->Effect.BaseP2;
        Hit.ADRatioBp=Installed->Effect.ADRatioBp;
        Hit.APRatioBp=Installed->Effect.APRatioBp;
        Hit.DamageType=Installed->Effect.DamageType;
        if (!StampSourceTerms(*Holder,Hit,Error)) { return false; }
        PendingHits.Add(MoveTemp(Hit));
        bConsumed=true;
        // §10: C05 is one trigger when its mark is consumed for the follow-up; setting the mark is none.
        LedgerComponentTriggered(*Holder,Installed->Source.DefinitionId);
        UE_LOG(LogLDCombat,Log,TEXT("LD_COMBAT_CONTROL_MARK_CONSUMED t=%lld holder=%s target=%s first=%u second=%u"),NowMs,
            *LDLog::Id(Holder->Victim.EntityId),*LDLog::Id(Victim.Victim.EntityId),uint32(Mark.Kind),uint32(App.Kind));
    }
    if (!bConsumed)
    {
        for (const FLDPassiveEffectRef& Ref : Marks)
        {
            if (!Ref.Targets(App.Source)) { continue; }
            FLDCombatControlMark* Mark=Holder->ControlMarks.FindByPredicate([&](const FLDCombatControlMark& Row)
            { return Row.Target==VictimRef && Row.Component==Ref.Source.DefinitionId; });
            if (!Mark) { Mark=&Holder->ControlMarks.AddDefaulted_GetRef(); }
            Mark->Target=VictimRef;
            Mark->Component=Ref.Source.DefinitionId;
            Mark->Kind=App.Kind;
            if (!LDMatchLifecycle::AddDurationMs(NowMs,Ref.Effect.DurationMs,Mark->ExpiresAtMs,Error)) { return false; }
        }
    }
    // C07: mana per controlled body, at most the cap per original action and all of its derived controls.
    TArray<FLDPassiveEffectRef> Manas;
    if (!WorkshopOn(Holder->AttributeProjection,ELDDemoEffectOp::ControlMana,App.Source,Manas,Error)) { return false; }
    for (const FLDPassiveEffectRef& Mana : Manas)
    {
        FLDCombatEffectBudget* Budget=Holder->EffectBudgets.FindByPredicate([&](const FLDCombatEffectBudget& Row)
        { return Row.EffectId==App.EffectId && Row.Component==Mana.Source.DefinitionId; });
        if (!Budget) { Budget=&Holder->EffectBudgets.Add_GetRef({App.EffectId,Mana.Source.DefinitionId,0,NowMs}); }
        const int64 Grant=FMath::Min(Mana.Effect.BaseP2,Mana.Effect.LimitP2-Budget->SpentP2);
        if (Grant<=0) { continue; }
        Budget->SpentP2+=Grant;
        const int64 MpBeforeP2=Holder->Resources.MpP2;
        GrantMana(*Holder,Grant);
        // §10: C07 is one trigger per grant that actually raised the holder's mana.
        if (Holder->Resources.MpP2>MpBeforeP2) { LedgerComponentTriggered(*Holder,Mana.Source.DefinitionId); }
    }
    // Budgets of actions long finished are no longer read.
    Holder->EffectBudgets.RemoveAll([NowMs](const FLDCombatEffectBudget& Row) { return NowMs-Row.AtMs>30000; });
    // C20: a host's landed hard control (re)arms one wide token for the holder's next other active.
    TArray<FLDPassiveEffectRef> Widens;
    if (App.Source.Kind==ELDCombatSourceKind::Learned
        && !WorkshopOn(Holder->AttributeProjection,ELDDemoEffectOp::ControlWiden,App.Source,Widens,Error)) { return false; }
    if (!Widens.IsEmpty())
    {
        const FLDPassiveEffectRef& Widen=Widens[0];
        FLDCombatWideToken Token;
        Token.Component=Widen.Source.DefinitionId;
        Token.Host=App.Source.DefinitionId;
        Token.RatioBp=Widen.Effect.RatioBp;
        if (!LDMatchLifecycle::AddDurationMs(NowMs,Widen.Effect.DurationMs,Token.ExpiresAtMs,Error)) { return false; }
        Holder->WideToken=Token;
        UE_LOG(LogLDCombat,Log,TEXT("LD_COMBAT_WIDE_TOKEN t=%lld holder=%s host=%s"),NowMs,
            *LDLog::Id(Holder->Victim.EntityId),*App.Source.DefinitionId.ToString());
    }
    return true;
}

bool ALDCombatDirector::ReactionHaste(const FLDCombatReactionFact& Fact, int64 NowMs, FString& Error)
{
    // C15: only a reaction an active's own release triggered (component elements carry no release).
    if (Fact.TriggerSource.Kind!=ELDCombatSourceKind::Learned || !Fact.TriggerEffectId.IsValid()) { return true; }
    FLDBodyCombatState* Trigger=FindBody(Fact.TriggerBody);
    if (!Trigger || Trigger->bPendingDeath) { return true; }
    TArray<FLDPassiveEffectRef> Hastes;
    if (!WorkshopOn(Trigger->AttributeProjection,ELDDemoEffectOp::ReactionHaste,Fact.TriggerSource,Hastes,Error)) { return false; }
    FLDCombatEntityRecord* Record=Hastes.IsEmpty() ? nullptr : FindRecord(Trigger->Victim.EntityId);
    for (const FLDPassiveEffectRef& Haste : Hastes)
    {
        if (!Record) { break; }
        if (Trigger->EffectBudgets.ContainsByPredicate([&](const FLDCombatEffectBudget& Row)
            { return Row.EffectId==Fact.TriggerEffectId && Row.Component==Haste.Source.DefinitionId; }))
        { continue; }
        Trigger->EffectBudgets.Add({Fact.TriggerEffectId,Haste.Source.DefinitionId,0,NowMs});
        FLDCombatTrack* Track=Record->Tracks.FindByPredicate([&Fact](const FLDCombatTrack& Row)
        { return Row.Kind==ELDCombatSourceKind::Learned && Row.DefinitionId==Fact.TriggerSource.DefinitionId; });
        if (!Track || Track->ReadyAtMs<=NowMs) { continue; }
        const int64 BeforeMs=Track->ReadyAtMs;
        Track->ReadyAtMs=LDCombatSourceRules::ReduceReadyAt(Track->ReadyAtMs,NowMs,Haste.Effect.AmountMs);
        MarkViewChanged(Trigger->Victim.EntityId);
        // §10: C15 is one trigger per cut that really moved the host's ready time.
        LedgerComponentTriggered(*Trigger,Haste.Source.DefinitionId);
        UE_LOG(LogLDCombat,Log,TEXT("LD_COMBAT_REACTION_HASTE t=%lld holder=%s host=%s ready=%lld->%lld"),NowMs,
            *LDLog::Id(Trigger->Victim.EntityId),*LDLog::Source(Fact.TriggerSource),BeforeMs,Track->ReadyAtMs);
    }
    Trigger->EffectBudgets.RemoveAll([NowMs](const FLDCombatEffectBudget& Row) { return NowMs-Row.AtMs>30000; });
    return true;
}

bool ALDCombatDirector::HasteOtherActives(FLDBodyCombatState& Holder, const FLDCombatSourceRef& Host, const FGuid& EffectId,
    int64 NowMs, FString& Error)
{
    if (Holder.bPendingDeath || !EffectId.IsValid()) { return true; }
    TArray<FLDPassiveEffectRef> Hastes;
    if (!WorkshopOn(Holder.AttributeProjection,ELDDemoEffectOp::HitHasteOther,Host,Hastes,Error)) { return false; }
    FLDCombatEntityRecord* Record=Hastes.IsEmpty() ? nullptr : FindRecord(Holder.Victim.EntityId);
    for (const FLDPassiveEffectRef& Haste : Hastes)
    {
        if (!Record) { break; }
        // C13: once per release EffectId, at its first native direct hit (or first dash contact).
        if (Holder.EffectBudgets.ContainsByPredicate([&](const FLDCombatEffectBudget& Row)
            { return Row.EffectId==EffectId && Row.Component==Haste.Source.DefinitionId; }))
        { continue; }
        Holder.EffectBudgets.Add({EffectId,Haste.Source.DefinitionId,0,NowMs});
        int32 Cut=0;
        for (FLDCombatTrack& Track : Record->Tracks)
        {
            if (Track.Kind!=ELDCombatSourceKind::Learned || Track.ReadyAtMs<=NowMs) { continue; }
            if (Host.Kind==ELDCombatSourceKind::Learned && Track.DefinitionId==Host.DefinitionId) { continue; }
            Track.ReadyAtMs=LDCombatSourceRules::ReduceReadyAt(Track.ReadyAtMs,NowMs,Haste.Effect.AmountMs);
            ++Cut;
        }
        if (Cut==0) { continue; }
        MarkViewChanged(Holder.Victim.EntityId);
        // §10: C13 is one trigger per release that really cut another active.
        LedgerComponentTriggered(Holder,Haste.Source.DefinitionId);
        UE_LOG(LogLDCombat,Log,TEXT("LD_COMBAT_HIT_HASTE t=%lld holder=%s host=%s cut=%d amount_ms=%lld"),NowMs,
            *LDLog::Id(Holder.Victim.EntityId),*LDLog::Source(Host),Cut,Haste.Effect.AmountMs);
    }
    Holder.EffectBudgets.RemoveAll([NowMs](const FLDCombatEffectBudget& Row) { return NowMs-Row.AtMs>30000; });
    return true;
}

bool ALDCombatDirector::OnHostDirectHit(FLDBodyCombatState& Holder, const FLDCombatHit& Hit, int64 NowMs, FString& Error)
{
    if (Holder.bPendingDeath) { return true; }
    // C22: the host's first native direct hit of a release calls the holder's newest movable zone or trap to the struck
    // enemy's feet (before C21 can detonate it on this same hit); each zone still moves once in its life, shared with C17.
    TArray<FLDPassiveEffectRef> Calls;
    if (Hit.EffectId.IsValid() && Hit.Source.Kind==ELDCombatSourceKind::Learned
        && !WorkshopOn(Holder.AttributeProjection,ELDDemoEffectOp::ZoneRelocate,Hit.Source,Calls,Error)) { return false; }
    if (!Calls.IsEmpty() && !Holder.EffectBudgets.ContainsByPredicate([&](const FLDCombatEffectBudget& Row)
        { return Row.EffectId==Hit.EffectId && Row.Component==Calls[0].Source.DefinitionId; }))
    {
        Holder.EffectBudgets.Add({Hit.EffectId,Calls[0].Source.DefinitionId,0,NowMs});
        ALDCharacter* TargetActor=nullptr;
        if (FindBody(Hit.Target,&TargetActor) && TargetActor
            && !RelocateArea(Holder,Hit.Source,FootOf(*TargetActor),NowMs,Error)) { return false; }
    }
    // C14: the host's hit sets (or refreshes) its mark on the struck life.
    Holder.SetupMarks.RemoveAll([NowMs](const FLDCombatSetupMark& Mark) { return Mark.ExpiresAtMs<=NowMs; });
    TArray<FLDPassiveEffectRef> Setups;
    if (!WorkshopOn(Holder.AttributeProjection,ELDDemoEffectOp::SetupMark,Hit.Source,Setups,Error)) { return false; }
    for (const FLDPassiveEffectRef& Setup : Setups)
    {
        FLDCombatSetupMark* Mark=Holder.SetupMarks.FindByPredicate([&](const FLDCombatSetupMark& Row)
        { return Row.Target==Hit.Target && Row.Component==Setup.Source.DefinitionId && Row.Host==Setup.TargetId; });
        if (!Mark) { Mark=&Holder.SetupMarks.AddDefaulted_GetRef(); }
        Mark->Target=Hit.Target;
        Mark->Component=Setup.Source.DefinitionId;
        Mark->Host=Setup.TargetId;
        if (!LDMatchLifecycle::AddDurationMs(NowMs,Setup.Effect.DurationMs,Mark->ExpiresAtMs,Error)) { return false; }
        UE_LOG(LogLDCombat,Verbose,TEXT("LD_COMBAT_SETUP_MARK t=%lld holder=%s host=%s target=%s until=%lld"),NowMs,
            *LDLog::Id(Holder.Victim.EntityId),*Setup.TargetId.ToString(),*LDLog::Id(Hit.Target.Victim.EntityId),Mark->ExpiresAtMs);
    }
    // C16: each distinct enemy past the first this release's native direct hits struck, up to the cap per release.
    TArray<FLDPassiveEffectRef> Manas;
    if (!Hit.EffectId.IsValid()) { return true; }
    if (!WorkshopOn(Holder.AttributeProjection,ELDDemoEffectOp::MultiHitMana,Hit.Source,Manas,Error)) { return false; }
    for (const FLDPassiveEffectRef& Mana : Manas)
    {
        FLDCombatEffectBudget* Budget=Holder.EffectBudgets.FindByPredicate([&](const FLDCombatEffectBudget& Row)
        { return Row.EffectId==Hit.EffectId && Row.Component==Mana.Source.DefinitionId; });
        if (!Budget) { Budget=&Holder.EffectBudgets.Add_GetRef({Hit.EffectId,Mana.Source.DefinitionId,0,NowMs}); }
        if (Budget->Struck.Contains(Hit.Target)) { continue; }
        Budget->Struck.Add(Hit.Target);
        if (Budget->Struck.Num()<2) { continue; }
        const int64 Grant=FMath::Min(Mana.Effect.BaseP2,Mana.Effect.LimitP2-Budget->SpentP2);
        if (Grant<=0) { continue; }
        Budget->SpentP2+=Grant;
        const int64 MpBeforeP2=Holder.Resources.MpP2;
        GrantMana(Holder,Grant);
        if (Holder.Resources.MpP2<=MpBeforeP2) { continue; }
        // §10: C16 is one trigger per grant that actually raised the holder's mana.
        LedgerComponentTriggered(Holder,Mana.Source.DefinitionId);
        UE_LOG(LogLDCombat,Log,TEXT("LD_COMBAT_MULTI_HIT_MANA t=%lld holder=%s host=%s struck=%d mp=%s->%s"),NowMs,
            *LDLog::Id(Holder.Victim.EntityId),*LDLog::Source(Hit.Source),Budget->Struck.Num(),*LDLog::P2(MpBeforeP2),
            *LDLog::P2(Holder.Resources.MpP2));
    }
    return true;
}

bool ALDCombatDirector::SettleStoredForce(FLDBodyCombatState& Body, int64 NowMs, FString& Error)
{
    if (!Body.StoredForceWatch.IsSet() || (Body.Shield.IsSet() && Body.Shield->EffectId==Body.StoredForceWatch->ShieldId)) { return true; }
    // C18: the watched host shield is gone (broken, expired or replaced): store a share of what it really absorbed.
    const FLDCombatStoredForceWatch Watch=Body.StoredForceWatch.GetValue();
    Body.StoredForceWatch.Reset();
    if (Body.bPendingDeath || Watch.AbsorbedP2<=0) { return true; }
    TArray<FLDPassiveEffectRef> Stores;
    FLDCombatSourceRef Host;
    Host.Kind=ELDCombatSourceKind::Learned;
    Host.DefinitionId=Watch.Host;
    if (!WorkshopOn(Body.AttributeProjection,ELDDemoEffectOp::StoredForce,Host,Stores,Error)) { return false; }
    const FLDPassiveEffectRef* Installed=Stores.FindByPredicate([&Watch](const FLDPassiveEffectRef& Ref)
    { return Ref.Source.DefinitionId==Watch.Component; });
    return !Installed || StoreForce(Body,*Installed,Watch.AbsorbedP2,NowMs,Error);
}

bool ALDCombatDirector::StoreForce(FLDBodyCombatState& Body, const FLDPassiveEffectRef& Ref, int64 RecordedP2, int64 NowMs, FString& Error)
{
    const int64 AmountP2=LDCombatWorkshopDetail::Share(RecordedP2,Ref.Effect.RatioBp);
    if (AmountP2<=0) { return true; }
    // One store at a time: a new one replaces the old.
    FLDCombatStoredForce Store;
    Store.Component=Ref.Source.DefinitionId;
    Store.Host=Ref.TargetId;
    Store.AmountP2=AmountP2;
    if (!LDMatchLifecycle::AddDurationMs(NowMs,Ref.Effect.DurationMs,Store.ExpiresAtMs,Error)) { return false; }
    Body.StoredForce=Store;
    UE_LOG(LogLDCombat,Log,TEXT("LD_COMBAT_STORED_FORCE t=%lld holder=%s host=%s recorded=%s stored=%s until=%lld"),NowMs,
        *LDLog::Id(Body.Victim.EntityId),*Store.Host.ToString(),*LDLog::P2(RecordedP2),*LDLog::P2(AmountP2),Store.ExpiresAtMs);
    return true;
}

bool ALDCombatDirector::SpendOnNativeHit(FLDBodyCombatState& Holder, const FLDCombatHit& Hit, int64 NowMs, FString& Error)
{
    using namespace LDCombatWorkshopDetail;
    if (Holder.bPendingDeath || Hit.Source.Kind!=ELDCombatSourceKind::Learned) { return true; }
    // C18: a different active's native direct hit spends the whole live store on its target, once.
    if (Holder.StoredForce.IsSet() && Holder.StoredForce->ExpiresAtMs<=NowMs) { Holder.StoredForce.Reset(); }
    if (Holder.StoredForce.IsSet() && Holder.StoredForce->Host!=Hit.Source.DefinitionId)
    {
        const FLDCombatStoredForce Store=Holder.StoredForce.GetValue();
        Holder.StoredForce.Reset();
        TArray<FLDPassiveEffectRef> Stores;
        if (!CollectPassiveEffects(Holder.AttributeProjection,ELDDemoEffectOp::StoredForce,Stores,Error)) { return false; }
        const FLDPassiveEffectRef* Installed=Stores.FindByPredicate([&Store](const FLDPassiveEffectRef& Ref)
        { return Ref.Source.DefinitionId==Store.Component && Ref.TargetId==Store.Host; });
        if (Installed)
        {
            FLDCombatHit Added=MakeHit(Holder,Installed->Source,Hit.Target,FGuid());
            Added.BaseP2=Store.AmountP2;
            Added.DamageType=Hit.DamageType;
            Added.bAddedDamage=true;
            if (!StampSourceTerms(Holder,Added,Error)) { return false; }
            PendingHits.Add(MoveTemp(Added));
            LedgerComponentTriggered(Holder,Store.Component);
            UE_LOG(LogLDCombat,Log,TEXT("LD_COMBAT_STORED_FORCE_SPENT t=%lld holder=%s host=%s by=%s target=%s amount=%s"),NowMs,
                *LDLog::Id(Holder.Victim.EntityId),*Store.Host.ToString(),*LDLog::Source(Hit.Source),
                *LDLog::Id(Hit.Target.Victim.EntityId),*LDLog::P2(Store.AmountP2));
        }
    }
    // C19: another active's native direct hit spends one charge of a host's pending package on its target, at most
    // once per release; the charge is gone from the basic.
    TArray<FLDPassiveEffectRef> Spends;
    if (!CollectPassiveEffects(Holder.AttributeProjection,ELDDemoEffectOp::EmpowerSpend,Spends,Error)) { return false; }
    for (const FLDPassiveEffectRef& Spend : Spends)
    {
        if (!Hit.EffectId.IsValid()) { break; }
        if (Holder.EffectBudgets.ContainsByPredicate([&](const FLDCombatEffectBudget& Row)
            { return Row.EffectId==Hit.EffectId && Row.Component==Spend.Source.DefinitionId; }))
        { continue; }
        const int32 Index=Holder.Empowers.IndexOfByPredicate([&](const FLDCombatEmpower& Package)
        {
            return Package.ExpiresAtMs>NowMs && Package.Charges>0 && Spend.Targets(Package.Source)
                && !(Package.Source.Kind==ELDCombatSourceKind::Learned && Package.Source.DefinitionId==Hit.Source.DefinitionId);
        });
        if (Index==INDEX_NONE) { continue; }
        Holder.EffectBudgets.Add({Hit.EffectId,Spend.Source.DefinitionId,0,NowMs});
        FLDCombatEmpower& Package=Holder.Empowers[Index];
        const FLDCombatEmpower Spent=Package;
        --Package.Charges;
        if (Package.Charges<=0)
        {
            Holder.Empowers.RemoveAt(Index);
            if (Spent.bChargeBonus && Holder.TimedBonuses.RemoveAll([&Spent](const FLDCombatTimedBonus& Row)
                { return Row.EffectId==Spent.EffectId; })>0
                && !RefreshTemporary(Holder,ActorOf(Holder),NowMs,Error))
            { return false; }
        }
        ++Holder.StateRevision;
        MarkViewChanged(Holder.Victim.EntityId);
        FLDCombatHit Added=MakeHit(Holder,Spend.Source,Hit.Target,FGuid());
        Added.BaseP2=Spent.AmountP2;
        Added.DamageType=ELDDemoDamageType::Physical;
        Added.bAddedDamage=true;
        if (!StampSourceTerms(Holder,Added,Error)) { return false; }
        PendingHits.Add(MoveTemp(Added));
        LedgerComponentTriggered(Holder,Spend.Source.DefinitionId);
        UE_LOG(LogLDCombat,Log,TEXT("LD_COMBAT_EMPOWER_SPENT t=%lld holder=%s package=%s by=%s target=%s amount=%s left=%d"),NowMs,
            *LDLog::Id(Holder.Victim.EntityId),*LDLog::Source(Spent.Source),*LDLog::Source(Hit.Source),
            *LDLog::Id(Hit.Target.Victim.EntityId),*LDLog::P2(Spent.AmountP2),Spent.Charges-1);
    }
    return DetonateZone(Holder,Hit,NowMs,Error);
}

bool ALDCombatDirector::DetonateZone(FLDBodyCombatState& Holder, const FLDCombatHit& Hit, int64 NowMs, FString& Error)
{
    using namespace LDCombatWorkshopDetail;
    if (!Hit.EffectId.IsValid()) { return true; }
    TArray<FLDPassiveEffectRef> Detonates;
    if (!WorkshopOn(Holder.AttributeProjection,ELDDemoEffectOp::ZoneDetonate,Hit.Source,Detonates,Error)) { return false; }
    if (Detonates.IsEmpty()) { return true; }
    const FLDPassiveEffectRef& Detonate=Detonates[0];
    // C21: once per host release, the first of the holder's live zones the struck enemy stands in.
    if (Holder.EffectBudgets.ContainsByPredicate([&](const FLDCombatEffectBudget& Row)
        { return Row.EffectId==Hit.EffectId && Row.Component==Detonate.Source.DefinitionId; }))
    { return true; }
    ALDCharacter* TargetActor=nullptr;
    const FLDBodyCombatState* Struck=FindBody(Hit.Target,&TargetActor);
    if (!Struck || !TargetActor) { return true; }
    const FVector TargetFoot=FootOf(*TargetActor);
    const int32 Index=Zones.IndexOfByPredicate([&](const FLDCombatZone& Zone)
    {
        return Zone.SourceEntity==Holder.Victim.EntityId && Zone.EndMs>NowMs
            && FVector::DistSquared2D(Zone.CentreCm,TargetFoot)<=Zone.RadiusCm*Zone.RadiusCm;
    });
    if (Index==INDEX_NONE) { return true; }
    Holder.EffectBudgets.Add({Hit.EffectId,Detonate.Source.DefinitionId,0,NowMs});
    const FLDCombatZone Zone=Zones[Index];
    // The zone ends now (no natural completion, so no C06 aftershock); its slow, burn checks and haste end with it.
    Zones.RemoveAt(Index);
    if (Zone.HasteBp>0)
    {
        for (auto& Pair : Bodies)
        {
            if (Pair.Value.TimedBonuses.RemoveAll([&Zone](const FLDCombatTimedBonus& Row) { return Row.EffectId==Zone.EffectId; })>0)
            {
                ++Pair.Value.StateRevision;
                MarkViewChanged(Pair.Value.Victim.EntityId);
            }
        }
    }
    const int32 Remaining=Zone.bDamage ? FMath::Max(Zone.Count-Zone.Pulsed,0) : 0;
    TArray<FLDCombatBodyRef> Inside;
    if (!CollectAreaTargets(Zone.CentreCm,Zone.RadiusCm,&Holder,Zone.SourceEntity,Inside,Error))
    { Error=TEXT("Zone detonation: ")+Error; return false; }
    const int64 ScaleBp=Share(Zone.PackageScaleBp,Detonate.Effect.RatioBp)*Remaining;
    for (const FLDCombatBodyRef& Target : Inside)
    {
        if (Remaining<=0 && !Zone.bWet) { break; }
        FLDCombatHit Blast=MakeHit(Holder,Detonate.Source,Target,FGuid());
        Blast.SourceADP2=Zone.SourceADP2;
        Blast.SourceAPP2=Zone.SourceAPP2;
        if (Remaining>0)
        {
            Blast.BaseP2=Zone.BaseP2;
            Blast.ADRatioBp=Zone.ADRatioBp;
            Blast.APRatioBp=Zone.APRatioBp;
            Blast.DamageType=Zone.DamageType;
            Blast.PackageScaleBp=ScaleBp;
            Blast.BurnStacks=Zone.PulseBurnStacks;
        }
        Blast.bAddedDamage=true;
        Blast.bApplyWet=Zone.bWet;
        Blast.ElementLevel=Zone.ElementLevel;
        Blast.ElementAPP2=Zone.ElementAPP2;
        if (!StampSourceTerms(Holder,Blast,Error)) { return false; }
        PendingHits.Add(MoveTemp(Blast));
    }
    MarkViewChanged(Holder.Victim.EntityId);
    LedgerComponentTriggered(Holder,Detonate.Source.DefinitionId);
    UE_LOG(LogLDCombat,Log,TEXT("LD_COMBAT_ZONE_DETONATE t=%lld holder=%s host=%s zone_host=%s remaining=%d inside=%d scale_bp=%lld"),
        NowMs,*LDLog::Id(Holder.Victim.EntityId),*LDLog::Source(Hit.Source),*LDLog::Source(Zone.Source),Remaining,Inside.Num(),ScaleBp);
    return true;
}

bool ALDCombatDirector::WidenTerms(FLDBodyCombatState& Body, const FLDCombatSourceRef& Source, FLDCombatActiveTerms& Terms,
    int64 NowMs, bool& bWidened, FString& Error) const
{
    using namespace LDCombatWorkshopDetail;
    bWidened=false;
    if (!Body.WideToken.IsSet() || Source.Kind!=ELDCombatSourceKind::Learned) { return true; }
    const FLDCombatWideToken Token=Body.WideToken.GetValue();
    if (Token.ExpiresAtMs<=NowMs || Token.Host==Source.DefinitionId) { return true; }
    TArray<FLDPassiveEffectRef> Widens;
    if (!CollectPassiveEffects(Body.AttributeProjection,ELDDemoEffectOp::ControlWiden,Widens,Error)) { return false; }
    if (!Widens.ContainsByPredicate([&Token](const FLDPassiveEffectRef& Ref)
        { return Ref.Source.DefinitionId==Token.Component && Ref.TargetId==Token.Host; }))
    { return true; }
    // C20: every area term of this release's values (and of its compiled area rewrites) x (1 + ratio).
    const int64 Bp=10000+Token.RatioBp;
    const auto Widen=[Bp](FLDDemoEffectDefinition& Effect)
    {
        switch (Effect.GetOp())
        {
        case ELDDemoEffectOp::Zone:
        case ELDDemoEffectOp::SelfArea:
        case ELDDemoEffectOp::ChainBounce:
            if (Effect.RadiusCm.Denominator>0) { Effect.RadiusCm=ScaleRational(Effect.RadiusCm,Bp); }
            break;
        case ELDDemoEffectOp::GroundImpact:
        case ELDDemoEffectOp::ContactTrap:
            if (Effect.RadiusCm.Denominator>0) { Effect.RadiusCm=ScaleRational(Effect.RadiusCm,Bp); }
            if (Effect.DistanceCm.Denominator>0) { Effect.DistanceCm=ScaleRational(Effect.DistanceCm,Bp); }
            break;
        default: break;
        }
    };
    bool bCone=false;
    for (FLDDemoEffectDefinition& Effect : Terms.Values.Effects)
    {
        Widen(Effect);
        bCone|=Effect.GetOp()==ELDDemoEffectOp::ChannelCone;
    }
    FLDDemoTargetDefinition& Target=Terms.Target;
    if (Target.ProjectileRadiusCm.Denominator>0) { Target.ProjectileRadiusCm=ScaleRational(Target.ProjectileRadiusCm,Bp); }
    if (Target.LineHalfWidthCm.Denominator>0) { Target.LineHalfWidthCm=ScaleRational(Target.LineHalfWidthCm,Bp); }
    // A cone's reach is its area; any other active's cast range is not.
    if (bCone && Target.RangeCm.Denominator>0) { Target.RangeCm=ScaleRational(Target.RangeCm,Bp); }
    if (Terms.SpreadProjectile.IsSet() && Terms.SpreadProjectile->RadiusCm.Denominator>0)
    { Terms.SpreadProjectile->RadiusCm=ScaleRational(Terms.SpreadProjectile->RadiusCm,Bp); }
    bWidened=true;
    return true;
}

bool ALDCombatDirector::SetupMarkBonus(FLDBodyCombatState& Holder, const FLDCombatHit& Hit, int64 NowMs, int64& OutBp,
    TArray<FLDCombatSetupMark>& OutTaken, FString& Error) const
{
    OutBp=0;
    OutTaken.Reset();
    if (Hit.Source.Kind!=ELDCombatSourceKind::Learned || Holder.SetupMarks.IsEmpty()) { return true; }
    TArray<FLDPassiveEffectRef> Setups;
    if (!CollectPassiveEffects(Holder.AttributeProjection,ELDDemoEffectOp::SetupMark,Setups,Error)) { return false; }
    for (const FLDCombatSetupMark& Mark : Holder.SetupMarks)
    {
        // The host's own hits never take or consume its mark; a removed connector's mark gives nothing.
        if (Mark.Target!=Hit.Target || Mark.ExpiresAtMs<=NowMs || Mark.Host==Hit.Source.DefinitionId) { continue; }
        const FLDPassiveEffectRef* Installed=Setups.FindByPredicate([&Mark](const FLDPassiveEffectRef& Ref)
        { return Ref.Source.DefinitionId==Mark.Component && Ref.TargetId==Mark.Host; });
        if (!Installed) { continue; }
        OutBp+=Installed->Effect.RatioBp;
        OutTaken.Add(Mark);
    }
    return true;
}

bool ALDCombatDirector::WatchKnockbackImpact(ALDCharacter& Target, FLDBodyCombatState& Body, const FLDCombatHit& Hit, int64 NowMs,
    FString& Error)
{
    Body.PendingImpact.Reset();
    auto* Movement=Cast<ULDCharacterMovementComponent>(Target.GetCharacterMovement());
    const FLDBodyCombatState* Holder=FindBody(Hit.SourceBody);
    if (!Movement || !Holder || Holder->bPendingDeath || Hit.KnockbackSource.Kind==ELDCombatSourceKind::None) { return true; }
    TArray<FLDPassiveEffectRef> Impacts;
    if (!WorkshopOn(Holder->AttributeProjection,ELDDemoEffectOp::KnockbackImpact,Hit.KnockbackSource,Impacts,Error)) { return false; }
    if (Impacts.IsEmpty()) { return true; }
    FLDCombatKnockbackImpact& Impact=Body.PendingImpact.Emplace();
    Impact.EffectId=Hit.EffectId;
    Impact.Holder=Hit.SourceBody;
    Impact.CreditOwner=Hit.CreditOwner;
    Impact.Impact=Impacts[0];
    Impact.HolderADP2=Holder->AttributeProjection.Final.ADP2;
    Impact.HolderAPP2=Holder->AttributeProjection.Final.APP2;
    if (!LDMatchLifecycle::AddDurationMs(NowMs,Hit.KnockbackMs,Impact.EndMs,Error)) { return false; }
    Movement->WatchKnockbackBlock();
    return true;
}

bool ALDCombatDirector::ShoveAround(const FLDBodyCombatState& Holder, const FVector& HolderFoot, const FLDCombatSourceRef& Host,
    const FLDPassiveEffectRef& Shove, FString& Error)
{
    using namespace LDCombatWorkshopDetail;
    TArray<FLDCombatBodyRef> Around;
    if (!CollectAreaTargets(HolderFoot,Cm(Shove.Effect.RadiusCm),&Holder,Holder.Victim.EntityId,Around,Error)) { return false; }
    const FGuid ShoveId=FGuid::NewGuid();
    for (const FLDCombatBodyRef& Other : Around)
    {
        FLDCombatHit Push=MakeHit(Holder,Host,Other,ShoveId);
        Push.KnockbackCm=Cm(Shove.Effect.DistanceCm);
        Push.KnockbackMs=Shove.Effect.DurationMs;
        Push.bKnockbackFromAnchor=true;
        Push.KnockbackAnchorCm=HolderFoot;
        Push.KnockbackSource=Host;
        if (!StampSourceTerms(Holder,Push,Error)) { return false; }
        PendingHits.Add(MoveTemp(Push));
    }
    UE_LOG(LogLDCombat,Display,TEXT("LD_COMBAT_DEFENSE_SHOVE holder=%s host=%s bodies=%d"),*LDLog::Id(Holder.Victim.EntityId),
        *Host.DefinitionId.ToString(),Around.Num());
    // §10: one trigger per shove that reached a body.
    if (!Around.IsEmpty()) { LedgerComponentTriggered(Holder,Shove.Source.DefinitionId); }
    return true;
}

bool ALDCombatDirector::ApplyWorkshopHit(const FLDCombatHit& Hit, FLDBodyCombatState& Target, FLDBodyCombatState* Source,
    const FLDDamageEvaluation& Damage, bool bShieldBroken, int64 ShieldGrantedP2, int64 NowMs, FString& Error)
{
    using namespace LDCombatWorkshopDetail;
    const FLDCombatBodyRef TargetRef{Target.Victim,Target.Identity};
    const bool bHostile=Hit.SourceEntity!=Target.Victim.EntityId && !Hit.bDebtPayment;
    // M12: the deferred share becomes a debt of this life, due at its original amount.
    if (Damage.DeferredP2>0)
    {
        TOptional<FLDPassiveEffectRef> Defer;
        if (!FindTrigger(Target.AttributeProjection,ELDDemoEffectOp::DamageDefer,Defer,Error)) { return false; }
        if (!Defer.IsSet()) { Error=TEXT("A deferred share has no installed deferral."); return false; }
        FLDCombatDebt& Debt=Target.Debts.AddDefaulted_GetRef();
        Debt.AmountP2=Damage.DeferredP2;
        Debt.SourceBody=Hit.SourceBody;
        Debt.SourceEntity=Hit.SourceEntity;
        Debt.CreditOwner=Hit.CreditOwner;
        Debt.Source=Hit.Source;
        if (!LDMatchLifecycle::AddDurationMs(NowMs,Defer->Effect.DurationMs,Debt.DueAtMs,Error)) { return false; }
        ++Target.Resources.Revision;
        MarkViewChanged(Target.Victim.EntityId);
        // §10: M12 is one trigger per hit whose share it deferred into a debt.
        LedgerComponentTriggered(Target,Defer->Source.DefinitionId);
    }
    // C08's memory: the bodies that recently hurt this one.
    if (bHostile && Damage.DamageP2+Damage.DeferredP2>0) { Remember(Target.RecentAttackers,Hit.SourceBody,NowMs); }
    // C10: the broken shield's granting source answers around the holder with a share of the granted amount.
    if (bShieldBroken && Target.ShieldGrantSource.Kind!=ELDCombatSourceKind::None && Target.Resources.HpP2>0)
    {
        TArray<FLDPassiveEffectRef> Bursts;
        ALDCharacter* TargetActor=nullptr;
        FindBody(TargetRef,&TargetActor);
        if (!WorkshopOn(Target.AttributeProjection,ELDDemoEffectOp::ShieldBurst,Target.ShieldGrantSource,Bursts,Error)) { return false; }
        for (const FLDPassiveEffectRef& Burst : Bursts)
        {
            TArray<FLDCombatBodyRef> Around;
            if (!TargetActor) { break; }
            if (!CollectAreaTargets(FootOf(*TargetActor),Cm(Burst.Effect.RadiusCm),&Target,Target.Victim.EntityId,Around,Error))
            { return false; }
            // One answer is one effect: every body it reaches shares its identity, so C11's 30G budget on it is one budget.
            const FGuid BurstId=FGuid::NewGuid();
            for (const FLDCombatBodyRef& Other : Around)
            {
                FLDCombatHit Answer=MakeHit(Target,Burst.Source,Other,BurstId);
                Answer.BaseP2=Share(ShieldGrantedP2,Burst.Effect.RatioBp);
                Answer.DamageType=ELDDemoDamageType::Magical;
                if (!StampSourceTerms(Target,Answer,Error)) { return false; }
                PendingHits.Add(MoveTemp(Answer));
            }
            // §10: C10 is one trigger per burst that reached a body (not per struck body).
            if (!Around.IsEmpty()) { LedgerComponentTriggered(Target,Burst.Source.DefinitionId); }
        }
    }
    // M17: a host shield an enemy broke shoves every legal enemy around the holder away (a knockback of the host).
    if (bShieldBroken && bHostile && Target.ShieldGrantSource.Kind!=ELDCombatSourceKind::None && Target.Resources.HpP2>0)
    {
        TArray<FLDPassiveEffectRef> Shoves;
        ALDCharacter* TargetActor=nullptr;
        FindBody(TargetRef,&TargetActor);
        if (!WorkshopOn(Target.AttributeProjection,ELDDemoEffectOp::DefenseShove,Target.ShieldGrantSource,Shoves,Error)) { return false; }
        if (!Shoves.IsEmpty() && TargetActor
            && !ShoveAround(Target,FootOf(*TargetActor),Target.ShieldGrantSource,Shoves[0],Error)) { return false; }
    }
    if (!Source) { return true; }
    // C01: the bound fire source's real HP damage joins the fixed window it falls in (spec §13.3); it settles after this
    // boundary's hits (SettleFireWindows).
    if (Hit.bBurnTick && Damage.HPLostP2>0)
    {
        TArray<FLDPassiveEffectRef> Shields;
        if (!WorkshopOn(Source->AttributeProjection,ELDDemoEffectOp::FireShield,Hit.Source,Shields,Error)) { return false; }
        if (!Shields.IsEmpty())
        {
            const FLDCombatEntityRecord* Record=FindRecord(Source->Victim.EntityId);
            int64 EndMs=0;
            if (!Record || !Record->FireWindowAnchorMs.IsSet()
                || !LDCombatMechanics::FixedWindowEndMs(Record->FireWindowAnchorMs.GetValue(),Shields[0].Effect.IntervalMs,NowMs,EndMs))
            {
                Error=FString::Printf(TEXT("C01 of %s has no install boundary for a fire fact at %lld."),*Source->Victim.EntityId.ToString(),NowMs);
                return false;
            }
            if (Source->FireWindowEndMs>0 && Source->FireWindowEndMs!=EndMs)
            {
                Error=FString::Printf(TEXT("C01 of %s still holds the window ending %lld when a fact of the window ending %lld arrives."),
                    *Source->Victim.EntityId.ToString(),Source->FireWindowEndMs,EndMs);
                return false;
            }
            Source->FireWindowSumP2+=Damage.HPLostP2;
            Source->FireWindowEndMs=EndMs;
        }
    }
    const bool bEffective=Damage.HPLostP2>0 || Damage.ShieldAbsorbedP2>0;
    // M11: the bodies this holder's actives hit directly.
    if (bEffective && Hit.bDirectAbilityDamage && Hit.Source.Kind==ELDCombatSourceKind::Learned)
    { Remember(Source->DirectHitMarks,TargetRef,NowMs); }
    // Skill damage hits (spec §8): native active damage and derived component damage; status, settle and debt hits are not.
    const bool bSkill=Hit.HitClass==ELDCombatHitClass::Ability && !Hit.bBurnTick && !Hit.bLedgerCashOut && !Hit.bStatusSettle
        && !Hit.bDebtPayment && bHostile;
    if (!bSkill) { return true; }
    // C11: a share of the real HP damage heals the holder, capped per original action and its derived hits.
    if (Damage.HPLostP2>0)
    {
        TArray<FLDPassiveEffectRef> Heals;
        if (!WorkshopOn(Source->AttributeProjection,ELDDemoEffectOp::SkillLifesteal,Hit.Source,Heals,Error)) { return false; }
        for (const FLDPassiveEffectRef& Heal : Heals)
        {
            FLDCombatEffectBudget* Budget=Source->EffectBudgets.FindByPredicate([&](const FLDCombatEffectBudget& Row)
            { return Row.EffectId==Hit.EffectId && Row.Component==Heal.Source.DefinitionId; });
            if (!Budget) { Budget=&Source->EffectBudgets.Add_GetRef({Hit.EffectId,Heal.Source.DefinitionId,0,NowMs}); }
            const int64 Amount=FMath::Min(Share(Damage.HPLostP2,Heal.Effect.RatioBp),Heal.Effect.BaseP2-Budget->SpentP2);
            if (Amount<=0) { continue; }
            Budget->SpentP2+=Amount;
            if (HealBody(*Source,Amount,Heal.Source,nullptr,Hit.EffectId)) { LedgerComponentTriggered(*Source,Heal.Source.DefinitionId); }
        }
    }
    // C12: each effective hit of the bound exit runs the chosen enabled base once more (no basic attack is made).
    if (bEffective && Target.Resources.HpP2>0 && !Target.bPendingDeath)
    {
        TArray<FLDPassiveEffectRef> Repeats;
        if (!WorkshopOn(Source->AttributeProjection,ELDDemoEffectOp::ExtraBase,Hit.Source,Repeats,Error)) { return false; }
        FLDKeywordTerms Terms;
        if (!Repeats.IsEmpty() && !KeywordTerms(Terms,Error)) { return false; }
        for (const FLDPassiveEffectRef& Repeat : Repeats)
        {
            const FName Base=Repeat.BoundComponent;
            const auto OfBase=[this,Source,&Base,&Error](ELDDemoEffectOp Op, TArray<FLDPassiveEffectRef>& Out)
            {
                if (!CollectPassiveEffects(Source->AttributeProjection,Op,Out,Error)) { return false; }
                Out.RemoveAll([&Base](const FLDPassiveEffectRef& Ref) { return Ref.Source.DefinitionId!=Base; });
                return true;
            };
            TArray<FLDPassiveEffectRef> Burns, Breaches, Ledgers, Elements;
            if (!OfBase(ELDDemoEffectOp::Burn,Burns) || !OfBase(ELDDemoEffectOp::Breach,Breaches) || !OfBase(ELDDemoEffectOp::Ledger,Ledgers)
                || !OfBase(ELDDemoEffectOp::BasicElement,Elements)) { return false; }
            // The repeated fire, water or electric is C12's own application (C01/C09 may read it as "C12 -> B01/B10/B11"),
            // through the element batch so it can react; it is a new child fact, never part of the parent hit's batch.
            FLDElementApplier Applier;
            Applier.SourceEntity=Source->Victim.EntityId;
            Applier.SourceBody={Source->Victim,Source->Identity};
            Applier.CreditOwner=Source->Victim.Kind==ELDVictimKind::Contender ? Source->Victim.EntityId : FGuid();
            Applier.Source=Repeat.Source;
            Applier.Level=GrowthLevelOf(*Source);
            Applier.SourceAPP2=FMath::Max<int64>(Source->AttributeProjection.Final.APP2,0);
            bool bChanged=false;
            for (const FLDPassiveEffectRef& Burn : Burns)
            {
                if (!ApplyElement(Target,ELDCombatElement::Fire,Applier,Burn.Effect.BasicCount,FGuid::NewGuid(),NowMs,Error)) { return false; }
            }
            for (const FLDPassiveEffectRef& Element : Elements)
            {
                const ELDCombatElement Kind=Element.Effect.Element==ELDDemoElement::Water ? ELDCombatElement::Water
                    : Element.Effect.Element==ELDDemoElement::Electric ? ELDCombatElement::Electric
                    : Element.Effect.Element==ELDDemoElement::Fire ? ELDCombatElement::Fire : ELDCombatElement::None;
                if (Kind==ELDCombatElement::None) { Error=FString::Printf(TEXT("%s repeats a base without an element."),*Repeat.Key); return false; }
                if (!ApplyElement(Target,Kind,Applier,Kind==ELDCombatElement::Fire ? 1 : 0,FGuid::NewGuid(),NowMs,Error)) { return false; }
            }
            for (const FLDPassiveEffectRef& Breach : Breaches)
            { bChanged|=LDCombatKeywords::ApplyBreach(Target.SharedBreach,Breach.Effect.BasicCount,NowMs,Terms); }
            if (bChanged)
            {
                RecordAssistContribution(Target,Hit.CreditOwner,NowMs);
                ++Target.StateRevision;
                MarkViewChanged(Target.Victim.EntityId);
            }
            // The original hit's B02 entry and this repeat are two entries, not a duplicate delivery.
            if (!Ledgers.IsEmpty() && !RecordLedgerEntries(Target,Hit,*Source,Ledgers,NowMs,Error)) { return false; }
            if (Burns.IsEmpty() && Breaches.IsEmpty() && Ledgers.IsEmpty() && Elements.IsEmpty())
            {
                UE_LOG(LogLDCombat,Verbose,TEXT("LD_COMBAT_EXTRA_BASE_IDLE holder=%s base=%s reason=base_not_installed"),
                    *LDLog::Id(Source->Victim.EntityId),*Base.ToString());
            }
            else { LedgerComponentTriggered(*Source,Repeat.Source.DefinitionId); }
        }
    }
    return true;
}

bool ALDCombatDirector::DeliverCounter(FLDBodyCombatState& Holder, const FLDCombatSourceRef& Exit, const FLDCombatBodyRef& Default,
    int64 BaseP2, ELDDemoDamageType DamageType, double ReachCm, const FGuid& RootEffectId, int64 NowMs, bool& bDelivered,
    FString& Error)
{
    using namespace LDCombatWorkshopDetail;
    bDelivered=false;
    const ALDGameMode* Mode=Core();
    const UWorld* CurrentWorld=GetWorld();
    const ULDMapRegistry* Registry=CurrentWorld ? CurrentWorld->GetSubsystem<ULDMapRegistry>() : nullptr;
    ALDCharacter* HolderActor=ActorOf(Holder);
    if (!Mode || !Mode->Vision || !Registry || !HolderActor)
    { Error=TEXT("A counter needs the observation owner, the registered world and its holder's body."); return false; }
    const FVector HolderFoot=FootOf(*HolderActor);
    TArray<FLDPassiveEffectRef> Spreads, Delays, Reaches, Marked;
    if (!WorkshopOn(Holder.AttributeProjection,ELDDemoEffectOp::SpreadCounter,Exit,Spreads,Error)
        || !WorkshopOn(Holder.AttributeProjection,ELDDemoEffectOp::DelayCounter,Exit,Delays,Error)
        || !WorkshopOn(Holder.AttributeProjection,ELDDemoEffectOp::CounterRangeBonus,Exit,Reaches,Error)
        || !WorkshopOn(Holder.AttributeProjection,ELDDemoEffectOp::MarkedCounter,Exit,Marked,Error))
    { return false; }
    int64 ScaleBp=10000;
    for (const auto* Mods : {&Spreads,&Delays,&Reaches,&Marked})
    {
        for (const FLDPassiveEffectRef& Mod : *Mods) { ScaleBp=Share(ScaleBp,Mod.Effect.MultiplierBp); }
    }
    // A legal counter target: the same live life, within reach, observed with clear sight.
    const auto Legal=[&](const FLDCombatBodyRef& Ref, FVector& OutFoot, bool& bFault)
    {
        ALDCharacter* Actor=nullptr;
        const FLDBodyCombatState* Body=FindBody(Ref,&Actor);
        if (!Body || !Actor || Body==&Holder || Body->bPendingDeath || IsIsolated(Holder.Victim.EntityId,*Body)) { return false; }
        OutFoot=FootOf(*Actor);
        if (FVector::Dist2D(HolderFoot,OutFoot)>ReachCm) { return false; }
        const ELDSightResult Sight=Mode->Vision->TraceObservation(HolderFoot,*Actor,Registry->Revision());
        bFault=Sight!=ELDSightResult::Visible && Sight!=ELDSightResult::Blocked && Sight!=ELDSightResult::OutOfRange;
        return Sight==ELDSightResult::Visible;
    };
    FLDCombatBodyRef Chosen;
    FVector ChosenFoot=FVector::ZeroVector;
    bool bFault=false;
    const FLDPassiveEffectRef* MarkedBy=nullptr;
    // M11: the most recent body this holder's actives hit directly, when it is a legal target of this exit.
    for (const FLDPassiveEffectRef& Mod : Marked)
    {
        for (int32 Index=Holder.DirectHitMarks.Num()-1; Index>=0 && !Chosen.IsValid(); --Index)
        {
            const FLDCombatBodyMark& Mark=Holder.DirectHitMarks[Index];
            if (NowMs-Mark.AtMs<=Mod.Effect.DurationMs && Legal(Mark.Body,ChosenFoot,bFault)) { Chosen=Mark.Body; MarkedBy=&Mod; }
            if (bFault) { Error=TEXT("Counter sight failed on a registered body."); return false; }
        }
    }
    if (!Chosen.IsValid() && Default.IsValid())
    {
        if (Legal(Default,ChosenFoot,bFault)) { Chosen=Default; }
        if (bFault) { Error=TEXT("Counter sight failed on a registered body."); return false; }
    }
    if (!Chosen.IsValid()) { return true; }
    FLDCombatHit Hit=MakeHit(Holder,Exit,Chosen,RootEffectId);
    Hit.BaseP2=BaseP2;
    Hit.DamageType=DamageType;
    Hit.PackageScaleBp=ScaleBp;
    Hit.bReflect=true;
    if (!StampSourceTerms(Holder,Hit,Error)) { return false; }
    bDelivered=true;
    // §10: one counter is one trigger of its exit (B03, C08) and of each mod that changed it: M11's marked target, M02's
    // locked blast, M01's spread that reached a body other than the chosen one (under M02 it only shapes the blast).
    const auto Triggered=[&](const FLDPassiveEffectRef* Shape)
    {
        LedgerComponentTriggered(Holder,Exit.DefinitionId);
        if (MarkedBy) { LedgerComponentTriggered(Holder,MarkedBy->Source.DefinitionId); }
        if (Shape) { LedgerComponentTriggered(Holder,Shape->Source.DefinitionId); }
    };
    const double SpreadRadius=Spreads.IsEmpty() ? 0. : Cm(Spreads[0].Effect.RadiusCm);
    if (!Delays.IsEmpty())
    {
        // M02: the landing point freezes now (the holder's own circle under M01) and blows after the delay.
        FLDCombatGroundEffect Blast;
        Blast.Payload=Hit;
        Blast.Payload.Target=FLDCombatBodyRef();
        Blast.CentreCm=SpreadRadius>0 ? HolderFoot : ChosenFoot;
        Blast.RadiusCm=Blast.InnerRadiusCm=SpreadRadius>0 ? SpreadRadius : Cm(Delays[0].Effect.RadiusCm);
        Blast.Origin=ELDCombatGroundOrigin::Settle;
        if (!LDMatchLifecycle::AddDurationMs(NowMs,Delays[0].Effect.DelayMs,Blast.ReadyAtMs,Error)) { return false; }
        Blast.ExpiresAtMs=Blast.ReadyAtMs;
        GroundEffects.Add(MoveTemp(Blast));
        Triggered(&Delays[0]);
        return true;
    }
    if (SpreadRadius>0)
    {
        // M01: the holder's surroundings instead of one body.
        TArray<FLDCombatBodyRef> Around;
        if (!CollectAreaTargets(HolderFoot,SpreadRadius,&Holder,Holder.Victim.EntityId,Around,Error)) { return false; }
        for (const FLDCombatBodyRef& Other : Around)
        {
            FLDCombatHit Copy=Hit;
            Copy.HitId=FGuid::NewGuid();
            Copy.Target=Other;
            PendingHits.Add(MoveTemp(Copy));
        }
        if (!Around.IsEmpty())
        { Triggered(Around.ContainsByPredicate([&Chosen](const FLDCombatBodyRef& Ref) { return Ref!=Chosen; }) ? &Spreads[0] : nullptr); }
        return true;
    }
    PendingHits.Add(MoveTemp(Hit));
    Triggered(nullptr);
    return true;
}

bool ALDCombatDirector::CashOutLedger(const FLDBodyCombatState& Holder, const FLDCombatBodyRef& Target, const FLDCombatSourceRef& Source,
    int64 AmountP2, ELDDemoDamageType DamageType, int64 NowMs, FString& Error)
{
    using namespace LDCombatWorkshopDetail;
    TArray<FLDPassiveEffectRef> Spreads, Delays;
    if (!WorkshopOn(Holder.AttributeProjection,ELDDemoEffectOp::SpreadLedger,Source,Spreads,Error)
        || !WorkshopOn(Holder.AttributeProjection,ELDDemoEffectOp::DelayLedger,Source,Delays,Error))
    { return false; }
    int64 ScaleBp=Spreads.IsEmpty() ? 10000 : Spreads[0].Effect.MultiplierBp;
    const double SpreadRadius=Spreads.IsEmpty() ? 0. : Cm(Spreads[0].Effect.RadiusCm);
    if (!Delays.IsEmpty())
    {
        // M02: the full batch is locked now and settles later on the same target life, if still legal then.
        FLDCombatLockedLedger& Locked=LockedLedgers.AddDefaulted_GetRef();
        Locked.Owner={Holder.Victim,Holder.Identity};
        Locked.Target=Target;
        Locked.Source=Source;
        Locked.AmountP2=AmountP2;
        Locked.ScaleBp=Share(ScaleBp,Delays[0].Effect.MultiplierBp);
        Locked.MaxDistanceCm=Cm(Delays[0].Effect.DistanceCm);
        Locked.SpreadRadiusCm=SpreadRadius;
        Locked.DamageType=DamageType;
        if (!LDMatchLifecycle::AddDurationMs(NowMs,Delays[0].Effect.DelayMs,Locked.DueAtMs,Error)) { return false; }
        UE_LOG(LogLDCombat,Log,TEXT("LD_COMBAT_LEDGER_LOCKED t=%lld owner=%s victim=%s amount=%s due=%lld"),NowMs,
            *LDLog::Id(Holder.Victim.EntityId),*LDLog::Id(Target.Victim.EntityId),*LDLog::P2(AmountP2),Locked.DueAtMs);
        // §10: locking the batch is M02's trigger; B02's is its settle (AdvanceWorkshop), a lapse is none.
        LedgerComponentTriggered(Holder,Delays[0].Source.DefinitionId);
        return true;
    }
    ALDCharacter* TargetActor=nullptr;
    if (!FindBody(Target,&TargetActor) || !TargetActor) { return true; }
    TArray<FLDCombatBodyRef> Struck{Target};
    if (SpreadRadius>0)
    {
        // M01: the batch also lands on the bodies around its target; their own ledgers stay untouched.
        TArray<FLDCombatBodyRef> Around;
        if (!CollectAreaTargets(FootOf(*TargetActor),SpreadRadius,&Holder,Holder.Victim.EntityId,Around,Error)) { return false; }
        for (const FLDCombatBodyRef& Other : Around) { if (Other!=Target) { Struck.Add(Other); } }
    }
    for (const FLDCombatBodyRef& Body : Struck)
    {
        FLDCombatHit Cash=MakeHit(Holder,Source,Body,FGuid());
        Cash.BaseP2=AmountP2;
        Cash.DamageType=DamageType;
        Cash.PackageScaleBp=ScaleBp;
        Cash.bLedgerCashOut=true;
        if (!StampSourceTerms(Holder,Cash,Error)) { return false; }
        PendingHits.Add(MoveTemp(Cash));
    }
    // §10: one cash-out is one B02 trigger, and one of M01 when its spread reached another body.
    LedgerComponentTriggered(Holder,Source.DefinitionId);
    if (Struck.Num()>1) { LedgerComponentTriggered(Holder,Spreads[0].Source.DefinitionId); }
    return true;
}

bool ALDCombatDirector::ResolveKnockbackImpact(FLDBodyCombatState& Body, const FLDCombatKnockbackImpact& Impact, bool bBody,
    const ALDCharacter* BlockerActor, int64 NowMs, FString& Error)
{
    const ALDGameMode* Mode=Core();
    if (!Mode) { Error=TEXT("A knockback impact needs the current core."); return false; }
    const FLDCombatBodyRef SelfRef{Body.Victim,Body.Identity};
    const auto Package=[&](const FLDCombatBodyRef& To, bool bStun)
    {
        FLDCombatHit Hit;
        Hit.HitId=FGuid::NewGuid();
        Hit.EffectId=Impact.EffectId;
        Hit.Target=To;
        Hit.SourceBody=Impact.Holder;
        Hit.SourceEntity=Impact.Holder.Victim.EntityId;
        Hit.CreditOwner=Impact.CreditOwner;
        Hit.Source=Impact.Impact.Source;
        Hit.HitClass=ELDCombatHitClass::Ability;
        Hit.BaseP2=Impact.Impact.Effect.BaseP2;
        Hit.ADRatioBp=Impact.Impact.Effect.ADRatioBp;
        Hit.APRatioBp=Impact.Impact.Effect.APRatioBp;
        Hit.DamageType=Impact.Impact.Effect.DamageType;
        Hit.SourceADP2=Impact.HolderADP2;
        Hit.SourceAPP2=Impact.HolderAPP2;
        Hit.StunDurationMs=bStun ? Impact.Impact.Effect.SecondaryDurationMs : 0;
        PendingHits.Add(MoveTemp(Hit));
    };
    const FLDBodyCombatState* Other=bBody && BlockerActor ? StateFor(BlockerActor) : nullptr;
    const bool bLegalBody=Other && !Other->bPendingDeath && Other!=&Body && !IsIsolated(Impact.Holder.Victim.EntityId,*Other)
        && Other->Victim.EntityId!=Impact.Holder.Victim.EntityId
        && !(Other->Victim.Kind==ELDVictimKind::Contender && Mode->IsContenderProtected(Other->Victim.EntityId));
    if (!bBody)
    {
        // A registered static blocker: damage and a stun through the normal hard-control path.
        Package(SelfRef,true);
    }
    else if (bLegalBody)
    {
        // Another legal enemy: both take the same physical package; nobody is pushed or stunned further.
        Package(SelfRef,false);
        Package({Other->Victim,Other->Identity},false);
    }
    // §10: an answered block is one C02 trigger of its holder (both packages of a body block included).
    if (!bBody || bLegalBody)
    {
        if (const FLDBodyCombatState* Holder=FindBody(Impact.Holder)) { LedgerComponentTriggered(*Holder,Impact.Impact.Source.DefinitionId); }
    }
    UE_LOG(LogLDCombat,Log,TEXT("LD_COMBAT_KNOCKBACK_IMPACT t=%lld victim=%s holder=%s kind=%s"),NowMs,*LDLog::Id(Body.Victim.EntityId),
        *LDLog::Id(Impact.Holder.Victim.EntityId),!bBody ? TEXT("wall") : bLegalBody ? TEXT("body") : TEXT("blocked_no_answer"));
    return true;
}

bool ALDCombatDirector::AdvanceWorkshop(int64 NowMs, FString& Error)
{
    using namespace LDCombatWorkshopDetail;
    const ALDGameMode* Mode=Core();
    const UWorld* CurrentWorld=GetWorld();
    const ULDMapRegistry* Registry=CurrentWorld ? CurrentWorld->GetSubsystem<ULDMapRegistry>() : nullptr;
    if (!Mode) { Error=TEXT("Workshop effects need the current core."); return false; }
    for (auto& Pair : Bodies)
    {
        FLDBodyCombatState& Body=Pair.Value;
        ALDCharacter* Actor=Pair.Key.Get();
        if (!Actor || Body.bPendingDeath) { continue; }
        const FLDCombatBodyRef SelfRef{Body.Victim,Body.Identity};
        // C02: the push's first real block (static geometry or another legal body) answers once; a push that ended unblocked
        // leaves nothing.
        if (Body.PendingImpact.IsSet())
        {
            auto* Movement=Cast<ULDCharacterMovementComponent>(Actor->GetCharacterMovement());
            TOptional<ULDCharacterMovementComponent::FKnockbackBlock> Block;
            if (Movement) { Block=Movement->TakeKnockbackBlock(); }
            const FLDCombatKnockbackImpact Impact=Body.PendingImpact.GetValue();
            if (Block.IsSet() || NowMs>=Impact.EndMs || !Movement)
            {
                Body.PendingImpact.Reset();
                if (Movement) { Movement->StopKnockbackWatch(); }
            }
            if (Block.IsSet() && !ResolveKnockbackImpact(Body,Impact,Block->bBody,Cast<ALDCharacter>(Block->Blocker.Get()),NowMs,Error))
            { return false; }
        }
        // M12: debts due now are paid at their original amount.
        for (int32 Index=Body.Debts.Num()-1; Index>=0; --Index)
        {
            const FLDCombatDebt Debt=Body.Debts[Index];
            if (Debt.DueAtMs>NowMs) { continue; }
            Body.Debts.RemoveAt(Index);
            FLDCombatHit Pay;
            Pay.HitId=FGuid::NewGuid();
            Pay.EffectId=FGuid::NewGuid();
            Pay.Target=SelfRef;
            Pay.SourceBody=Debt.SourceBody;
            Pay.SourceEntity=Debt.SourceEntity;
            Pay.CreditOwner=Debt.CreditOwner;
            Pay.Source=Debt.Source;
            Pay.HitClass=ELDCombatHitClass::Ability;
            Pay.BaseP2=Debt.AmountP2;
            Pay.DamageType=ELDDemoDamageType::Physical;
            Pay.bDebtPayment=true;
            PendingHits.Add(MoveTemp(Pay));
        }
        // C08: the store fires once per interval at the nearest legal attacker; idle for its window, it clears.
        if (Body.CounterStoreP2>0)
        {
            TArray<FLDPassiveEffectRef> Counters;
            if (!CollectPassiveEffects(Body.AttributeProjection,ELDDemoEffectOp::HealCounter,Counters,Error)) { return false; }
            if (Counters.IsEmpty() || NowMs-Body.CounterLastAddMs>=Counters[0].Effect.DurationMs)
            {
                Body.CounterStoreP2=0;
                Body.CounterNextFireMs=0;
                Body.CounterRootEffectId.Invalidate();
            }
            else if (NowMs>=Body.CounterNextFireMs)
            {
                const FLDDemoEffectDefinition& Counter=Counters[0].Effect;
                TArray<FLDPassiveEffectRef> Reaches;
                if (!WorkshopOn(Body.AttributeProjection,ELDDemoEffectOp::CounterRangeBonus,Counters[0].Source,Reaches,Error)) { return false; }
                double ReachCm=Cm(Counter.RadiusCm);
                for (const FLDPassiveEffectRef& Reach : Reaches) { ReachCm+=Cm(Reach.Effect.DistanceCm); }
                // The nearest body that hurt this one and is still a legal target.
                FLDCombatBodyRef Nearest;
                double Best=TNumericLimits<double>::Max();
                const FVector Foot=FootOf(*Actor);
                for (const FLDCombatBodyMark& Mark : Body.RecentAttackers)
                {
                    ALDCharacter* Other=nullptr;
                    const FLDBodyCombatState* OtherBody=FindBody(Mark.Body,&Other);
                    if (!OtherBody || !Other || OtherBody->bPendingDeath) { continue; }
                    const double Distance=FVector::Dist2D(Foot,FootOf(*Other));
                    if (Distance<=ReachCm && (Distance<Best || (Distance==Best && Mark.Body.Victim.EntityId<Nearest.Victim.EntityId)))
                    {
                        Best=Distance;
                        Nearest=Mark.Body;
                    }
                }
                bool bDelivered=false;
                if (!DeliverCounter(Body,Counters[0].Source,Nearest,Body.CounterStoreP2,Counter.DamageType,ReachCm,Body.CounterRootEffectId,
                    NowMs,bDelivered,Error))
                { return false; }
                if (bDelivered)
                {
                    Body.CounterStoreP2=0;
                    Body.CounterRootEffectId.Invalidate();
                }
                while (Body.CounterNextFireMs<=NowMs) { Body.CounterNextFireMs+=Counter.IntervalMs; }
            }
        }
    }
    // M02: locked batches settle on their same live target within reach and sight, or lapse.
    for (int32 Index=LockedLedgers.Num()-1; Index>=0; --Index)
    {
        const FLDCombatLockedLedger Locked=LockedLedgers[Index];
        if (Locked.DueAtMs>NowMs) { continue; }
        if (!Mode->Vision || !Registry) { Error=TEXT("A locked ledger needs the observation owner and the registered world."); return false; }
        LockedLedgers.RemoveAt(Index);
        ALDCharacter* OwnerActor=nullptr;
        ALDCharacter* TargetActor=nullptr;
        const FLDBodyCombatState* Settler=FindBody(Locked.Owner,&OwnerActor);
        const FLDBodyCombatState* Target=FindBody(Locked.Target,&TargetActor);
        bool bSettles=Settler && OwnerActor && !Settler->bPendingDeath && Target && TargetActor && !Target->bPendingDeath
            && FVector::Dist2D(FootOf(*OwnerActor),FootOf(*TargetActor))<=Locked.MaxDistanceCm;
        if (bSettles)
        {
            const ELDSightResult Sight=Mode->Vision->TraceObservation(FootOf(*OwnerActor),*TargetActor,Registry->Revision());
            if (Sight!=ELDSightResult::Visible && Sight!=ELDSightResult::Blocked && Sight!=ELDSightResult::OutOfRange)
            { Error=TEXT("A locked ledger lost its sight registration."); return false; }
            bSettles=Sight==ELDSightResult::Visible;
        }
        UE_LOG(LogLDCombat,Log,TEXT("LD_COMBAT_LEDGER_SETTLE t=%lld owner=%s victim=%s settled=%d"),NowMs,
            *LDLog::Id(Locked.Owner.Victim.EntityId),*LDLog::Id(Locked.Target.Victim.EntityId),int32(bSettles));
        if (!bSettles) { continue; }
        TArray<FLDCombatBodyRef> Struck{Locked.Target};
        if (Locked.SpreadRadiusCm>0)
        {
            TArray<FLDCombatBodyRef> Around;
            if (!CollectAreaTargets(FootOf(*TargetActor),Locked.SpreadRadiusCm,Settler,Settler->Victim.EntityId,Around,Error)) { return false; }
            for (const FLDCombatBodyRef& Other : Around) { if (Other!=Locked.Target) { Struck.Add(Other); } }
        }
        for (const FLDCombatBodyRef& Body : Struck)
        {
            FLDCombatHit Cash=MakeHit(*Settler,Locked.Source,Body,FGuid());
            Cash.BaseP2=Locked.AmountP2;
            Cash.DamageType=Locked.DamageType;
            Cash.PackageScaleBp=Locked.ScaleBp;
            Cash.bLedgerCashOut=true;
            if (!StampSourceTerms(*Settler,Cash,Error)) { return false; }
            PendingHits.Add(MoveTemp(Cash));
        }
        // §10: the settle of a locked batch is its B02 trigger.
        LedgerComponentTriggered(*Settler,Locked.Source.DefinitionId);
    }
    return true;
}

bool ALDCombatDirector::SettleFireWindows(int64 NowMs, FString& Error)
{
    using namespace LDCombatWorkshopDetail;
    for (auto& Pair : Bodies)
    {
        FLDBodyCombatState& Body=Pair.Value;
        if (Body.FireWindowEndMs<=0 || NowMs<Body.FireWindowEndMs) { continue; }
        // C01: a closed window grants its candidate shield once (no input, no shield; the fire itself is never consumed).
        const int64 Sum=Body.FireWindowSumP2, EndMs=Body.FireWindowEndMs;
        Body.FireWindowSumP2=0;
        Body.FireWindowEndMs=0;
        if (Body.bPendingDeath || Sum<=0) { continue; }
        TArray<FLDPassiveEffectRef> Shields;
        if (!CollectPassiveEffects(Body.AttributeProjection,ELDDemoEffectOp::FireShield,Shields,Error)) { return false; }
        if (Shields.IsEmpty())
        {
            Error=FString::Printf(TEXT("A C01 window of %s closed without its connector installed."),*Body.Victim.EntityId.ToString());
            return false;
        }
        const FLDDemoEffectDefinition& Effect=Shields[0].Effect;
        const int64 Amount=FMath::Min(Effect.BaseP2+Share(Sum,Effect.RatioBp),Share(Body.Resources.MaxHpP2,Effect.MultiplierBp));
        const FGuid ShieldId=FGuid::NewGuid();
        if (!GrantShieldTo(Body,Amount,Effect.DurationMs,ShieldId,Body.Victim.EntityId,NowMs,Error,Shields[0].Source)) { return false; }
        // §10: C01 is one trigger when its shield is actually granted (a weaker one is not), never the fire summed.
        if (Body.Shield.IsSet() && Body.Shield->EffectId==ShieldId) { LedgerComponentTriggered(Body,Shields[0].Source.DefinitionId); }
        UE_LOG(LogLDCombat,Log,TEXT("LD_COMBAT_FIRE_SHIELD t=%lld body=%s window_end=%lld fire=%s candidate=%s"),NowMs,
            *LDLog::Id(Body.Victim.EntityId),EndMs,*LDLog::P2(Sum),*LDLog::P2(Amount));
    }
    return true;
}

bool ALDCombatDirector::NoteWorkshopInstall(FLDCombatEntityRecord& Record, const FLDAttributeProjection& Previous,
    FLDBodyCombatState* Body, int64 NowMs, FString& Error)
{
    TArray<FLDPassiveEffectRef> Before, After;
    if (!CollectPassiveEffects(Previous,ELDDemoEffectOp::FireShield,Before,Error)
        || !CollectPassiveEffects(Record.Projection,ELDDemoEffectOp::FireShield,After,Error)) { return false; }
    // Spec §13.3: the first install fixes the boundaries for good; a reinstall keeps them (no replay, no early claim).
    if (!After.IsEmpty() && !Record.FireWindowAnchorMs.IsSet()) { Record.FireWindowAnchorMs=NowMs; }
    // An uninstall (or a binding moved to another fire source) stops collecting and drops the unsettled window.
    const bool bSameBinding=!Before.IsEmpty() && !After.IsEmpty() && Before[0].TargetKind==After[0].TargetKind
        && Before[0].TargetId==After[0].TargetId;
    if (Body && !bSameBinding && (Body->FireWindowEndMs>0 || Body->FireWindowSumP2>0))
    {
        Body->FireWindowSumP2=0;
        Body->FireWindowEndMs=0;
    }
    return true;
}

int64 ALDCombatDirector::NextWorkshopDueMs(int64 NowMs) const
{
    int64 Due=MAX_int64;
    const auto Consider=[&Due,NowMs](int64 At) { if (At>NowMs && At<Due) { Due=At; } };
    for (const auto& Pair : Bodies)
    {
        const FLDBodyCombatState& Body=Pair.Value;
        if (Body.bPendingDeath) { continue; }
        for (const FLDCombatDebt& Debt : Body.Debts) { Consider(Debt.DueAtMs); }
        if (Body.FireWindowEndMs>0) { Consider(Body.FireWindowEndMs); }
        if (Body.CounterStoreP2>0) { Consider(Body.CounterNextFireMs); }
        if (Body.PendingImpact.IsSet()) { Consider(Body.PendingImpact->EndMs); }
    }
    for (const FLDCombatLockedLedger& Locked : LockedLedgers) { Consider(Locked.DueAtMs); }
    return Due;
}
