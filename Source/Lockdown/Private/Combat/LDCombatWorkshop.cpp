#include "Combat/LDCombatDirector.h"
#include "Core/LDLog.h"
#include "Combat/LDCombatKeywords.h"
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

    // A damage-only copy: the original target keeps the controls and debuffs (M01).
    void StripToDamage(FLDCombatHit& Hit)
    {
        Hit.RootDurationMs=Hit.SlowBp=Hit.SlowDurationMs=Hit.DisarmDurationMs=Hit.StunDurationMs=Hit.SilenceDurationMs=0;
        Hit.AttackSlowBp=Hit.AttackSlowMs=Hit.KnockbackMs=0;
        Hit.KnockbackCm=0;
        Hit.BreachStacks=Hit.BurnStacks=0;
        Hit.KnockbackSource=FLDCombatSourceRef();
    }

    void Remember(TArray<FLDCombatBodyMark>& Marks, const FLDCombatBodyRef& Body, int64 AtMs)
    {
        Marks.RemoveAll([&Body](const FLDCombatBodyMark& Row) { return Row.Body==Body; });
        Marks.Add({Body,AtMs});
        // The newest few are enough for "the most recent legal one".
        while (Marks.Num()>8) { Marks.RemoveAt(0); }
    }

    constexpr ELDDemoEffectOp ActiveMods[]={ELDDemoEffectOp::KnockbackScale,ELDDemoEffectOp::ControlScale,ELDDemoEffectOp::RangeBonus,
        ELDDemoEffectOp::CooldownCost,ELDDemoEffectOp::CompressCone,ELDDemoEffectOp::CompressZone,ELDDemoEffectOp::DashScale,
        ELDDemoEffectOp::SpreadUnit,ELDDemoEffectOp::SpreadProjectile};
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
    for (const FLDCombatBodyRef& Other : Around)
    {
        ALDCharacter* OtherActor=nullptr;
        if (Other==Primary || !FindBody(Other,&OtherActor) || !OtherActor) { continue; }
        const FVector Toward=(FootOf(*OtherActor)-Origin).GetSafeNormal2D();
        if (!Facing.IsNearlyZero() && !Toward.IsNearlyZero() && FVector::DotProduct(Facing,Toward)<CosHalf) { continue; }
        FLDCombatHit Hit=Template;
        Hit.HitId=FGuid::NewGuid();
        Hit.Target=Other;
        LDCombatWorkshopDetail::StripToDamage(Hit);
        PendingHits.Add(MoveTemp(Hit));
    }
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
        UE_LOG(LogLDCombat,Log,TEXT("LD_COMBAT_AFTERSHOCK t=%lld owner=%s exit=%s radius=%.0f"),NowMs,*LDLog::Id(OwnerRef.Victim.EntityId),
            *LDLog::Source(Exit),RadiusCm);
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
        GrantMana(*Holder,Grant);
    }
    // Budgets of actions long finished are no longer read.
    Holder->EffectBudgets.RemoveAll([NowMs](const FLDCombatEffectBudget& Row) { return NowMs-Row.AtMs>30000; });
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
            for (const FLDCombatBodyRef& Other : Around)
            {
                FLDCombatHit Answer=MakeHit(Target,Burst.Source,Other,FGuid());
                Answer.BaseP2=Share(ShieldGrantedP2,Burst.Effect.RatioBp);
                Answer.DamageType=ELDDemoDamageType::Magical;
                if (!StampSourceTerms(Target,Answer,Error)) { return false; }
                PendingHits.Add(MoveTemp(Answer));
            }
        }
    }
    if (!Source) { return true; }
    // C01: the bound fire source's real HP damage joins the open one-second window.
    if (Hit.bBurnTick && Damage.HPLostP2>0)
    {
        TArray<FLDPassiveEffectRef> Shields;
        if (!WorkshopOn(Source->AttributeProjection,ELDDemoEffectOp::FireShield,Hit.Source,Shields,Error)) { return false; }
        if (!Shields.IsEmpty())
        {
            Source->FireWindowSumP2+=Damage.HPLostP2;
            if (Source->FireWindowEndMs<=0 && !LDMatchLifecycle::AddDurationMs(NowMs,Shields[0].Effect.IntervalMs,Source->FireWindowEndMs,Error))
            { return false; }
        }
    }
    const bool bEffective=Damage.HPLostP2>0 || Damage.ShieldAbsorbedP2>0;
    // M11: the bodies this holder's actives hit directly.
    if (bEffective && Hit.bDirectAbilityDamage && Hit.Source.Kind==ELDCombatSourceKind::Learned)
    { Remember(Source->DirectHitMarks,TargetRef,NowMs); }
    // Skill damage hits (spec §8): native active damage and derived component damage; status, settle and debt hits are not.
    const bool bSkill=Hit.HitClass==ELDCombatHitClass::Ability && !Hit.bBurnTick && !Hit.bLedgerCashOut && !Hit.bDebtPayment && bHostile;
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
            HealBody(*Source,Amount,Heal.Source);
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
            if (!Elements.IsEmpty())
            {
                Error=FString::Printf(TEXT("%s repeats %s, which requires #198 A element state; installing it must have been refused."),
                    *Repeat.Key,*Base.ToString());
                return false;
            }
            bool bChanged=false;
            for (const FLDPassiveEffectRef& Burn : Burns)
            {
                // The repeated fire is C12's own layer (C01 may read it as "C12 -> B01").
                FLDCombatBurnLayer Layer;
                if (!BurnLayerOf(*Source,Repeat.Source,Layer,Error)) { return false; }
                bChanged|=LDCombatKeywords::ApplyBurn(Target.Burn,Layer,Burn.Effect.BasicCount,NowMs,Terms);
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
            if (Burns.IsEmpty() && Breaches.IsEmpty() && Ledgers.IsEmpty())
            {
                UE_LOG(LogLDCombat,Verbose,TEXT("LD_COMBAT_EXTRA_BASE_IDLE holder=%s base=%s reason=base_not_installed"),
                    *LDLog::Id(Source->Victim.EntityId),*Base.ToString());
            }
        }
    }
    return true;
}

bool ALDCombatDirector::DeliverCounter(FLDBodyCombatState& Holder, const FLDCombatSourceRef& Exit, const FLDCombatBodyRef& Default,
    int64 BaseP2, ELDDemoDamageType DamageType, double ReachCm, int64 NowMs, bool& bDelivered, FString& Error)
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
        if (!Body || !Actor || Body==&Holder || Body->bPendingDeath || Body->IsolationGroup!=Holder.IsolationGroup) { return false; }
        OutFoot=FootOf(*Actor);
        if (FVector::Dist2D(HolderFoot,OutFoot)>ReachCm) { return false; }
        const ELDSightResult Sight=Mode->Vision->TraceObservation(HolderFoot,*Actor,Registry->Revision());
        bFault=Sight!=ELDSightResult::Visible && Sight!=ELDSightResult::Blocked && Sight!=ELDSightResult::OutOfRange;
        return Sight==ELDSightResult::Visible;
    };
    FLDCombatBodyRef Chosen;
    FVector ChosenFoot=FVector::ZeroVector;
    bool bFault=false;
    // M11: the most recent body this holder's actives hit directly, when it is a legal target of this exit.
    for (const FLDPassiveEffectRef& Mod : Marked)
    {
        for (int32 Index=Holder.DirectHitMarks.Num()-1; Index>=0 && !Chosen.IsValid(); --Index)
        {
            const FLDCombatBodyMark& Mark=Holder.DirectHitMarks[Index];
            if (NowMs-Mark.AtMs<=Mod.Effect.DurationMs && Legal(Mark.Body,ChosenFoot,bFault)) { Chosen=Mark.Body; }
            if (bFault) { Error=TEXT("Counter sight failed on a registered body."); return false; }
        }
    }
    if (!Chosen.IsValid() && Default.IsValid())
    {
        if (Legal(Default,ChosenFoot,bFault)) { Chosen=Default; }
        if (bFault) { Error=TEXT("Counter sight failed on a registered body."); return false; }
    }
    if (!Chosen.IsValid()) { return true; }
    FLDCombatHit Hit=MakeHit(Holder,Exit,Chosen,FGuid());
    Hit.BaseP2=BaseP2;
    Hit.DamageType=DamageType;
    Hit.PackageScaleBp=ScaleBp;
    Hit.bReflect=true;
    if (!StampSourceTerms(Holder,Hit,Error)) { return false; }
    bDelivered=true;
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
        return true;
    }
    PendingHits.Add(MoveTemp(Hit));
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
    const bool bLegalBody=Other && !Other->bPendingDeath && Other!=&Body && Other->IsolationGroup==Body.IsolationGroup
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
        // C01: a closed window grants its candidate shield (no damage, no shield; the fire itself is never consumed).
        if (Body.FireWindowEndMs>0 && NowMs>=Body.FireWindowEndMs)
        {
            const int64 Sum=Body.FireWindowSumP2;
            Body.FireWindowSumP2=0;
            Body.FireWindowEndMs=0;
            TArray<FLDPassiveEffectRef> Shields;
            if (!CollectPassiveEffects(Body.AttributeProjection,ELDDemoEffectOp::FireShield,Shields,Error)) { return false; }
            if (!Shields.IsEmpty() && Sum>0)
            {
                const FLDDemoEffectDefinition& Effect=Shields[0].Effect;
                const int64 Amount=FMath::Min(Effect.BaseP2+Share(Sum,Effect.RatioBp),Share(Body.Resources.MaxHpP2,Effect.MultiplierBp));
                if (!GrantShieldTo(Body,Amount,Effect.DurationMs,FGuid::NewGuid(),Body.Victim.EntityId,NowMs,Error,Shields[0].Source)) { return false; }
                UE_LOG(LogLDCombat,Log,TEXT("LD_COMBAT_FIRE_SHIELD t=%lld body=%s fire=%s candidate=%s"),NowMs,*LDLog::Id(Body.Victim.EntityId),
                    *LDLog::P2(Sum),*LDLog::P2(Amount));
            }
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
                if (!DeliverCounter(Body,Counters[0].Source,Nearest,Body.CounterStoreP2,Counter.DamageType,ReachCm,NowMs,bDelivered,Error))
                { return false; }
                if (bDelivered) { Body.CounterStoreP2=0; }
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
