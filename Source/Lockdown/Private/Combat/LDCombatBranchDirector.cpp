#include "Combat/LDCombatDirector.h"

#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Combat/LDCombatControlRules.h"
#include "Combat/LDCombatElements.h"
#include "Combat/LDMomentum.h"
#include "Combat/LDTacticalRules.h"
#include "Core/LDLog.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDLifeAuthority.h"
#include "Framework/LDMatchLifecycleTypes.h"

// Skill branch mechanics (issue #287 spec §5.1; docs/systems/COMBAT_AND_GROWTH.md "技能等级与分支"). A branch variant is an
// ordinary value set: its known operations run through their usual handlers, the workshop mod operations it carries through
// CompileCardActive, and the branch-only operations here. Hit riders ride on the release's cast ledger, so every native
// direct hit of that release (projectile, chain hop, zone pulse, impact, echo, return) applies them.

namespace LDCombatBranchDetail
{
    int64 Share(int64 Value, int64 Bp) { return Value<=0 || Bp<=0 ? 0 : Value/10000*Bp+Value%10000*Bp/10000; }
    double Cm(const FLDDemoRational& Value) { return Value.Denominator>0 ? double(Value.Numerator)/double(Value.Denominator) : 0.; }
    const FLDDemoEffectDefinition* Find(const FLDDemoAbilityValues& Values, ELDDemoEffectOp Op)
    {
        return Values.Effects.FindByPredicate([Op](const FLDDemoEffectDefinition& Effect) { return Effect.GetOp()==Op; });
    }
    bool IsRider(ELDDemoEffectOp Op)
    {
        switch (Op)
        {
        case ELDDemoEffectOp::Vulnerable:
        case ELDDemoEffectOp::NextBasicAmp:
        case ELDDemoEffectOp::MomentumGain:
        case ELDDemoEffectOp::MomentumSteal:
        case ELDDemoEffectOp::FollowUp:
        case ELDDemoEffectOp::BurnBurst:
        case ELDDemoEffectOp::Overcharge:
        case ELDDemoEffectOp::FollowBasic:
        case ELDDemoEffectOp::DashBack:
        case ELDDemoEffectOp::Recast:
            return true;
        default:
            return false;
        }
    }
    // A hit of Source's life on Target, owned like its release.
    FLDCombatHit MakeHit(const FLDBodyCombatState& Source, const FLDCombatSourceRef& Exit, const FLDCombatBodyRef& Target, const FGuid& EffectId)
    {
        FLDCombatHit Hit;
        Hit.HitId=FGuid::NewGuid();
        Hit.EffectId=EffectId;
        Hit.Target=Target;
        Hit.SourceBody={Source.Victim,Source.Identity};
        Hit.SourceEntity=Source.Victim.EntityId;
        Hit.CreditOwner=Source.Victim.Kind==ELDVictimKind::Contender ? Source.Victim.EntityId : FGuid();
        Hit.Source=Exit;
        Hit.HitClass=ELDCombatHitClass::Ability;
        Hit.SourceADP2=Source.AttributeProjection.Final.ADP2;
        Hit.SourceAPP2=Source.AttributeProjection.Final.APP2;
        return Hit;
    }
}

bool ALDCombatDirector::ArmBranchRelease(ALDCharacter& Actor, FLDBodyCombatState& Body, const FLDCombatSourceRef& Source,
    const FLDCombatActiveTerms& Terms, const FGuid& EffectId, const TSharedPtr<FLDCombatCastLedger>& CastLedger,
    const FLDCombatAction& Action, int64 NowMs, FString& Error)
{
    using namespace LDCombatBranchDetail;
    if (!CastLedger) { Error=TEXT("A branch release needs its cast ledger."); return false; }
    FLDCombatBranchState& Branch=Body.Branch;
    bool bTimed=false;
    for (const FLDDemoEffectDefinition& Effect : Terms.Values.Effects)
    {
        const ELDDemoEffectOp Op=Effect.GetOp();
        if (IsRider(Op)) { CastLedger->Riders.Add(Effect); }
        switch (Op)
        {
        case ELDDemoEffectOp::SelfBuff:
            // S03 B2, S15 A1/B2: a self damage cut and stats for the duration.
            if (Effect.RatioBp>0)
            {
                Branch.GuardBp=FMath::Max(Branch.GuardBp,Effect.RatioBp);
                if (!LDMatchLifecycle::AddDurationMs(NowMs,Effect.DurationMs,Branch.GuardUntilMs,Error)) { return false; }
            }
            if (!Effect.Stats.IsEmpty())
            {
                FLDCombatTimedBonus Bonus;
                Bonus.EffectId=FGuid::NewGuid();
                Bonus.SourceDefinition=Source.DefinitionId;
                Bonus.Stats=Effect.Stats;
                if (!LDMatchLifecycle::AddDurationMs(NowMs,Effect.DurationMs,Bonus.ExpiresAtMs,Error)) { return false; }
                LDTactical::ApplyTimedBonus(Body.TimedBonuses,Bonus);
                bTimed=true;
            }
            break;
        case ELDDemoEffectOp::BasicRider:
            // S13 A2, S15 A2: primary basics shock or breach while it lasts.
            if (!LDMatchLifecycle::AddDurationMs(NowMs,Effect.DurationMs,Branch.RiderUntilMs,Error)) { return false; }
            Branch.bRiderElectric=Effect.Element==ELDDemoElement::Electric;
            Branch.RiderBreach=Effect.Count;
            break;
        case ELDDemoEffectOp::ShieldReflect:
            Branch.ShieldEffectId=EffectId;
            Branch.ShieldReflectBp=Effect.RatioBp;
            Branch.ShieldExit=Source;
            break;
        case ELDDemoEffectOp::ShieldEndHeal:
            Branch.ShieldEffectId=EffectId;
            Branch.ShieldEndHealBp=Effect.RatioBp;
            Branch.ShieldExit=Source;
            break;
        case ELDDemoEffectOp::ShieldBurstBreak:
            Branch.ShieldEffectId=EffectId;
            Branch.ShieldBreakBurst=Effect;
            Branch.ShieldExit=Source;
            break;
        case ELDDemoEffectOp::BraceStun:
            Branch.BraceEffectId=EffectId; Branch.BraceStunMs=Effect.DurationMs; break;
        case ELDDemoEffectOp::BraceHeal:
            Branch.BraceEffectId=EffectId; Branch.BraceHealBp=Effect.RatioBp; break;
        case ELDDemoEffectOp::BraceStack:
            Branch.BraceEffectId=EffectId; Branch.BraceStackBp=Effect.RatioBp; break;
        case ELDDemoEffectOp::BraceShove:
            Branch.BraceEffectId=EffectId; Branch.BraceShoveCm=Cm(Effect.DistanceCm); Branch.BraceShoveMs=Effect.DurationMs; break;
        case ELDDemoEffectOp::BraceShield:
            Branch.BraceEffectId=EffectId; Branch.BraceShieldMs=Effect.DurationMs; break;
        case ELDDemoEffectOp::DelayedBurst:
            // S13 B2: placed where this release's dash ends.
            Branch.DashEndBurst=Effect;
            Branch.DashEndExit=Source;
            Branch.DashEndEffectId=EffectId;
            Branch.DashEndAtMs=FMath::Max(Body.DashUntilMs,NowMs);
            Branch.DashEndLedger=CastLedger;
            break;
        case ELDDemoEffectOp::Recast:
            // S02 B, S12 B: a first release opens the window; the recast itself never reopens it.
            if (!CastLedger->bRecast)
            {
                FLDCombatRecastWindow& Window=Branch.Recast.Emplace();
                Window.Definition=Source.DefinitionId;
                Window.Direction=Action.Direction;
                Window.Terms=Effect;
                Window.FirstLedger=CastLedger;
                if (!LDMatchLifecycle::AddDurationMs(NowMs,Effect.DurationMs,Window.UntilMs,Error)) { return false; }
            }
            break;
        default:
            break;
        }
    }
    // A dash of a branch with a ghost window passes through bodies for that long after it ends (S11 B).
    if (const FLDDemoEffectDefinition* Dash=Find(Terms.Values,ELDDemoEffectOp::Dash); Dash && Dash->ExtraDurationMs>0)
    {
        if (auto* Movement=Cast<ULDCharacterMovementComponent>(Actor.GetCharacterMovement()))
        {
            Movement->SetBodyPassThroughSeconds(double(Dash->DurationMs+Dash->ExtraDurationMs)/1000.);
        }
    }
    if (bTimed && !RefreshTemporary(Body,&Actor,NowMs,Error)) { return false; }
    ++Body.StateRevision;
    return true;
}

bool ALDCombatDirector::ArmBranchHostile(FLDBodyCombatState& Body, const FLDCombatActiveTerms& Terms, const FLDCombatHit& Template,
    const TArray<FLDCombatBodyRef>& AreaTargets, int64 NowMs, FString& Error)
{
    using namespace LDCombatBranchDetail;
    // S07 B: the self area repeats around the owner's foot.
    const FLDDemoEffectDefinition* Pulse=Find(Terms.Values,ELDDemoEffectOp::Pulse);
    const FLDDemoEffectDefinition* Area=Find(Terms.Values,ELDDemoEffectOp::SelfArea);
    if (Pulse && Area)
    {
        FLDCombatPulse& Row=Pulses.AddDefaulted_GetRef();
        Row.Template=Template;
        Row.Template.DeliveryScaleBp=Share(Template.DeliveryScaleBp,Pulse->MultiplierBp);
        Row.RadiusCm=Cm(Area->RadiusCm);
        Row.IntervalMs=Pulse->IntervalMs;
        Row.Remaining=Pulse->Count;
        Row.MomentumP2=Pulse->LimitP2;
        Row.ShieldBaseP2=Pulse->BaseP2;
        Row.ShieldMaxHPBp=Pulse->MaxHPRatioBp;
        if (!LDMatchLifecycle::AddDurationMs(NowMs,Pulse->IntervalMs,Row.NextMs,Error)) { return false; }
    }
    // S14 B2: a self area that reached anybody raises the release's shield.
    if (const FLDDemoEffectDefinition* Bonus=Find(Terms.Values,ELDDemoEffectOp::ShieldHitBonus);
        Bonus && !AreaTargets.IsEmpty() && Body.Resources.ShieldP2>0)
    {
        const int64 Added=Share(Body.Resources.ShieldP2,Bonus->RatioBp);
        Body.Resources.ShieldP2+=Added;
        Body.ShieldGrantedP2+=Added;
        ++Body.Resources.Revision;
        MarkViewChanged(Body.Victim.EntityId);
    }
    return true;
}

bool ALDCombatDirector::BranchRecastOpen(const FLDBodyCombatState& Body, const FLDCombatSourceRef& Source, int64 NowMs) const
{
    return Source.Kind==ELDCombatSourceKind::Learned && Body.Branch.Recast.IsSet() && Body.Branch.Recast->Definition==Source.DefinitionId
        && Body.Branch.Recast->UntilMs>NowMs;
}

bool ALDCombatDirector::BranchFreeCast(const FLDBodyCombatState& Body, const FLDCombatSourceRef& Source, int64 NowMs) const
{
    return Source.Kind==ELDCombatSourceKind::Learned && Body.Branch.FreeCastUntilMs>NowMs;
}

void ALDCombatDirector::ApplyRecastTerms(const FLDCombatRecastWindow& Window, FLDCombatActiveTerms& Terms, FLDCombatAction& Delivery) const
{
    using namespace LDCombatBranchDetail;
    const FLDDemoEffectDefinition& Recast=Window.Terms;
    Terms.PackageScaleBp=Share(Terms.PackageScaleBp,Recast.MultiplierBp);
    for (FLDDemoEffectDefinition& Effect : Terms.Values.Effects)
    {
        if (Effect.GetOp()==ELDDemoEffectOp::Damage)
        {
            Effect.BaseP2+=Recast.BaseP2;
            Effect.ADRatioBp+=Recast.ADRatioBp;
        }
        else if (Effect.GetOp()==ELDDemoEffectOp::Root && Recast.RecastControl==ELDDemoRecastControl::Root)
        {
            Effect.DurationMs=Recast.SecondaryDurationMs;
        }
        else if (Effect.GetOp()==ELDDemoEffectOp::Root && Recast.RecastControl==ELDDemoRecastControl::Disarm)
        {
            Effect.Opcode=uint8(ELDDemoEffectOp::Disarm);
            Effect.DurationMs=Recast.SecondaryDurationMs;
        }
    }
    // S12 B: the second dash keeps the direction of the first unless B2 frees it.
    if (!Recast.bFreeDirection && !Window.Direction.IsNearlyZero()) { Delivery.Direction=Window.Direction; }
}

int64 ALDCombatDirector::BranchVulnerableBp(const FLDBodyCombatState& Target, const FLDCombatHit& Hit, int64 NowMs) const
{
    if (Hit.SourceEntity==Target.Victim.EntityId) { return 0; }
    int64 Bp=0;
    for (const FLDCombatVulnerability& Row : Target.Branch.Vulnerabilities)
    {
        if (Row.ExpiresAtMs<=NowMs || (Row.OwnerEntity.IsValid() && Row.OwnerEntity!=Hit.SourceEntity)) { continue; }
        if (Row.Scope==ELDDemoDamageScope::Skill && Hit.HitClass!=ELDCombatHitClass::Ability) { continue; }
        if (Row.Scope==ELDDemoDamageScope::Basic && Hit.HitClass!=ELDCombatHitClass::BasicPrimary) { continue; }
        Bp+=Row.RatioBp;
    }
    return Bp;
}

int64 ALDCombatDirector::BranchGuardBp(const FLDBodyCombatState& Target, int64 NowMs) const
{
    int64 Bp=Target.Branch.GuardUntilMs>NowMs ? Target.Branch.GuardBp : 0;
    // S08 A2: the owner's foot inside its own guarding zone.
    const ALDCharacter* Actor=ActorOf(Target);
    if (Actor)
    {
        const FVector Foot=FootOf(*Actor);
        for (const FLDCombatZone& Zone : Zones)
        {
            if (Zone.GuardBp>0 && Zone.SourceEntity==Target.Victim.EntityId && Zone.EndMs>NowMs
                && FVector::Dist2D(Zone.CentreCm,Foot)<=Zone.RadiusCm)
            { Bp=FMath::Max(Bp,Zone.GuardBp); }
        }
    }
    return FMath::Clamp<int64>(Bp,0,9999);
}

bool ALDCombatDirector::ApplyBranchRiders(const FLDCombatHit& Hit, FLDBodyCombatState& Source, FLDBodyCombatState& Target,
    int64 NowMs, FString& Error)
{
    using namespace LDCombatBranchDetail;
    FLDCombatCastLedger& Ledger=*Hit.CastLedger;
    const FLDCombatBodyRef TargetRef{Target.Victim,Target.Identity};
    // Once-per-target riders reach each body on its first native hit of the release.
    const bool bFirstOnTarget=!Ledger.RiderStruck.Contains(TargetRef);
    if (bFirstOnTarget) { Ledger.RiderStruck.Add(TargetRef); }
    for (const FLDDemoEffectDefinition& Effect : Ledger.Riders)
    {
        switch (Effect.GetOp())
        {
        case ELDDemoEffectOp::Vulnerable:
        {
            if (!bFirstOnTarget) { break; }
            FLDCombatVulnerability& Row=Target.Branch.Vulnerabilities.AddDefaulted_GetRef();
            Row.OwnerEntity=Effect.bOwnOnly ? Source.Victim.EntityId : FGuid();
            Row.Scope=Effect.Scope;
            Row.RatioBp=Effect.RatioBp;
            if (!LDMatchLifecycle::AddDurationMs(NowMs,Effect.DurationMs,Row.ExpiresAtMs,Error)) { return false; }
            ++Target.StateRevision;
            break;
        }
        case ELDDemoEffectOp::NextBasicAmp:
            if (Effect.bOnCatch || Ledger.bNextBasicGranted) { break; }
            Ledger.bNextBasicGranted=true;
            Source.Branch.NextBasicAmpBp=FMath::Max(Source.Branch.NextBasicAmpBp,Effect.RatioBp);
            if (!LDMatchLifecycle::AddDurationMs(NowMs,Effect.DurationMs,Source.Branch.NextBasicAmpUntilMs,Error)) { return false; }
            break;
        case ELDDemoEffectOp::MomentumGain:
            // A center-only grant reads the impact's inner ring (a full-share hit).
            if (!Effect.bCenterOnly || Hit.DeliveryScaleBp>=10000) { GrantMomentum(Source,Effect.BaseP2,ELDMomentumBook::Skill); }
            break;
        case ELDDemoEffectOp::MomentumSteal:
        {
            if (!bFirstOnTarget) { break; }
            if (Effect.DelayMs>0)
            {
                FLDCombatFollowUp& Row=FollowUps.AddDefaulted_GetRef();
                Row.Payload=MakeHit(Source,Hit.Source,TargetRef,Hit.EffectId);
                Row.StealP2=Effect.BaseP2;
                Row.StealMultiplierBp=Effect.MultiplierBp;
                Row.FreeCastMs=Effect.ExtraDurationMs;
                if (!LDMatchLifecycle::AddDurationMs(NowMs,Effect.DelayMs,Row.DueMs,Error)) { return false; }
                break;
            }
            const int64 Taken=FMath::Min(FMath::Max<int64>(Target.Resources.MomentumP2,0),Effect.BaseP2);
            if (Taken<=0 || Target.Victim.Kind!=ELDVictimKind::Contender) { break; }
            GrantMomentum(Target,-Taken,ELDMomentumBook::Spent);
            GrantMomentum(Source,Share(Taken,Effect.MultiplierBp),ELDMomentumBook::Skill);
            if (Effect.ExtraDurationMs>0 && !LDMatchLifecycle::AddDurationMs(NowMs,Effect.ExtraDurationMs,Source.Branch.FreeCastUntilMs,Error))
            { return false; }
            UE_LOG(LogLDCombat,Log,TEXT("LD_COMBAT_MOMENTUM_STEAL t=%lld src=%s target=%s taken=%s"),NowMs,
                *LDLog::Id(Source.Victim.EntityId),*LDLog::Id(Target.Victim.EntityId),*LDLog::P2(Taken));
            break;
        }
        case ELDDemoEffectOp::FollowUp:
        {
            if (!bFirstOnTarget) { break; }
            FLDCombatFollowUp& Row=FollowUps.AddDefaulted_GetRef();
            FLDCombatHit& Payload=Row.Payload;
            Payload=MakeHit(Source,Hit.Source,TargetRef,Hit.EffectId);
            Payload.BaseP2=Effect.BaseP2;
            Payload.ADRatioBp=Effect.ADRatioBp;
            Payload.APRatioBp=Effect.APRatioBp;
            Payload.DamageType=Effect.DamageType;
            if (Effect.Element!=ELDDemoElement::None && Effect.Element!=ELDDemoElement::Fire)
            { Payload.NativeElements|=LDCombatElements::Bit(LDCombatElements::FromDemo(Effect.Element)); }
            // N3 B1: the second blast's rot lasts its own duration.
            Payload.RotMs=Effect.Element==ELDDemoElement::Rot ? Effect.ExtraDurationMs : 0;
            Row.MomentumP2=Effect.LimitP2;
            Payload.AttackSlowBp=Effect.RatioBp;
            Payload.AttackSlowMs=Effect.DurationMs;
            Payload.bAddedDamage=true;
            if (!StampElementSnapshot(Source,Payload,Error)) { return false; }
            Row.Remaining=FMath::Max(Effect.Count,1);
            Row.IntervalMs=Effect.IntervalMs;
            if (!LDMatchLifecycle::AddDurationMs(NowMs,Effect.DelayMs,Row.DueMs,Error)) { return false; }
            break;
        }
        case ELDDemoEffectOp::BurnBurst:
        {
            // S07 A1: a body this release left at the burn's full stacks takes the extra package once.
            if (!bFirstOnTarget || Target.Burn.Layers.Num()<Effect.Count) { break; }
            FLDCombatHit Extra=MakeHit(Source,Hit.Source,TargetRef,Hit.EffectId);
            Extra.BaseP2=Effect.BaseP2;
            Extra.APRatioBp=Effect.APRatioBp;
            Extra.DamageType=ELDDemoDamageType::Magical;
            Extra.bAddedDamage=true;
            PendingHits.Add(MoveTemp(Extra));
            break;
        }
        case ELDDemoEffectOp::Overcharge:
            // S10 B1: every hit this body takes discharges for the duration (credited to this source).
            if (!LDMatchLifecycle::AddDurationMs(NowMs,Effect.DurationMs,Target.Branch.OverchargeUntilMs,Error)) { return false; }
            Target.Branch.OverchargeSource={Source.Victim,Source.Identity};
            Target.Branch.OverchargeExit=Hit.Source;
            Target.Branch.OverchargeLevel=GrowthLevelOf(Source);
            Target.Branch.OverchargeAPP2=FMath::Max<int64>(Source.AttributeProjection.Final.APP2,0);
            break;
        case ELDDemoEffectOp::FollowBasic:
        {
            // S12 A2: the contact is followed by one ordinary primary basic on that body.
            if (!bFirstOnTarget) { break; }
            const FLDDemoContentSnapshot* Content=Catalog();
            if (!Content) { Error=TEXT("A follow-up basic needs the approved content."); return false; }
            FLDCombatHit Basic=MakeHit(Source,Hit.Source,TargetRef,FGuid::NewGuid());
            Basic.HitClass=ELDCombatHitClass::BasicPrimary;
            Basic.ADRatioBp=Content->Get().Character.BasicADRatioBp;
            Basic.DamageType=Content->Get().Character.BasicDamageType;
            if (!StampSourceTerms(Source,Basic,Error)) { return false; }
            PendingHits.Add(MoveTemp(Basic));
            break;
        }
        case ELDDemoEffectOp::DashBack:
        {
            // S03 B: after the hit the caster dashes away from the body it struck (once per release).
            if (!bFirstOnTarget || Ledger.RiderStruck.Num()>1) { break; }
            ALDCharacter* SourceActor=nullptr;
            ALDCharacter* TargetActor=nullptr;
            FindBody({Source.Victim,Source.Identity},&SourceActor);
            FindBody(TargetRef,&TargetActor);
            auto* Movement=SourceActor ? Cast<ULDCharacterMovementComponent>(SourceActor->GetCharacterMovement()) : nullptr;
            if (!Movement || !TargetActor || Source.bPendingDeath) { break; }
            const FVector Away=(FootOf(*SourceActor)-FootOf(*TargetActor)).GetSafeNormal2D();
            if (Away.IsNearlyZero() || !Movement->CanStartDash(Away,50.)) { break; }
            if (!Movement->StartDemoDash(Away,Cm(Effect.DistanceCm),double(Effect.DurationMs)/1000.,Error)) { return false; }
            if (!LDMatchLifecycle::AddDurationMs(NowMs,Effect.DurationMs,Source.DashUntilMs,Error)) { return false; }
            if (Effect.bAsDash)
            {
                // S03 B1: it counts as a voluntary dash (dash triggers, perfect dodge).
                FLDCombatVoluntaryDash& Voluntary=Source.VoluntaryDash.Emplace();
                Voluntary.EffectId=Hit.EffectId;
                Voluntary.Source=Hit.Source;
                Voluntary.StartFootCm=FootOf(*SourceActor);
                Voluntary.EndMs=Source.DashUntilMs;
                Source.DashStartedAtMs=NowMs;
                Source.DashStartFootCm=FootOf(*SourceActor);
            }
            break;
        }
        case ELDDemoEffectOp::Recast:
            // S12 B1: the recast stuns a body the first release also struck.
            if (Ledger.bRecast && bFirstOnTarget && Effect.ExtraDurationMs>0 && Ledger.RecastFirst.Contains(TargetRef))
            {
                FLDCombatHit Stun=MakeHit(Source,Hit.Source,TargetRef,Hit.EffectId);
                Stun.StunDurationMs=Effect.ExtraDurationMs;
                PendingHits.Add(MoveTemp(Stun));
            }
            break;
        default:
            break;
        }
    }
    return true;
}

bool ALDCombatDirector::AdvanceBranches(int64 NowMs, FString& Error)
{
    using namespace LDCombatBranchDetail;
    // Delayed and repeated packages (follow_up, delayed steals).
    for (int32 Index=FollowUps.Num()-1; Index>=0; --Index)
    {
        FLDCombatFollowUp& Row=FollowUps[Index];
        while (Row.Remaining>0 && Row.DueMs<=NowMs)
        {
            --Row.Remaining;
            FLDBodyCombatState* Source=FindBody(Row.Payload.SourceBody);
            FLDBodyCombatState* Target=FindBody(Row.Payload.Target);
            if (!Target || Target->bPendingDeath) { Row.Remaining=0; break; }
            if (Row.StealP2>0)
            {
                // S04 A2: the steal waits for the silence to end; a gone thief takes nothing.
                const int64 Taken=FMath::Min(FMath::Max<int64>(Target->Resources.MomentumP2,0),Row.StealP2);
                if (Source && !Source->bPendingDeath && Taken>0 && Target->Victim.Kind==ELDVictimKind::Contender)
                {
                    GrantMomentum(*Target,-Taken,ELDMomentumBook::Spent);
                    GrantMomentum(*Source,Share(Taken,Row.StealMultiplierBp),ELDMomentumBook::Skill);
                }
            }
            else
            {
                FLDCombatHit Hit=Row.Payload;
                Hit.HitId=FGuid::NewGuid();
                PendingHits.Add(MoveTemp(Hit));
                if (Row.MomentumP2>0 && Source && !Source->bPendingDeath) { GrantMomentum(*Source,Row.MomentumP2,ELDMomentumBook::Skill); }
            }
            if (Row.Remaining>0 && !LDMatchLifecycle::AddDurationMs(Row.DueMs,Row.IntervalMs,Row.DueMs,Error)) { return false; }
        }
        if (Row.Remaining<=0) { FollowUps.RemoveAt(Index); }
    }
    // S07 B: the self area repeats around its living owner.
    for (int32 Index=Pulses.Num()-1; Index>=0; --Index)
    {
        FLDCombatPulse& Row=Pulses[Index];
        ALDCharacter* Actor=nullptr;
        FLDBodyCombatState* Holder=FindBody(Row.Template.SourceBody,&Actor);
        if (!Holder || !Actor || Holder->bPendingDeath) { Pulses.RemoveAt(Index); continue; }
        while (Row.Remaining>0 && Row.NextMs<=NowMs)
        {
            --Row.Remaining;
            TArray<FLDCombatBodyRef> Around;
            if (!CollectAreaTargets(FootOf(*Actor),Row.RadiusCm,Holder,Holder->Victim.EntityId,Around,Error)) { return false; }
            for (const FLDCombatBodyRef& Target : Around)
            {
                FLDCombatHit Hit=Row.Template;
                Hit.HitId=FGuid::NewGuid();
                Hit.Target=Target;
                PendingHits.Add(MoveTemp(Hit));
                if (Row.MomentumP2>0) { GrantMomentum(*Holder,Row.MomentumP2,ELDMomentumBook::Skill); }
                if (Row.ShieldBaseP2>0 || Row.ShieldMaxHPBp>0)
                {
                    const int64 Amount=Row.ShieldBaseP2+Share(Holder->Resources.MaxHpP2,Row.ShieldMaxHPBp);
                    if (!GrantShieldTo(*Holder,Amount,3000,Row.Template.EffectId,Holder->Victim.EntityId,NowMs,Error,Row.Template.Source))
                    { return false; }
                }
            }
            if (Row.Remaining>0 && !LDMatchLifecycle::AddDurationMs(Row.NextMs,Row.IntervalMs,Row.NextMs,Error)) { return false; }
        }
        if (Row.Remaining<=0) { Pulses.RemoveAt(Index); }
    }
    for (auto& Pair : Bodies)
    {
        FLDBodyCombatState& Body=Pair.Value;
        ALDCharacter* Actor=Pair.Key.Get();
        FLDCombatBranchState& Branch=Body.Branch;
        Branch.Vulnerabilities.RemoveAll([NowMs](const FLDCombatVulnerability& Row) { return Row.ExpiresAtMs<=NowMs; });
        if (Branch.Recast.IsSet() && Branch.Recast->UntilMs<=NowMs) { Branch.Recast.Reset(); }
        if (!Actor || Body.bPendingDeath) { continue; }
        // S13 B2: the dash ended; the burst lands at that foot after its delay.
        if (Branch.DashEndBurst.IsSet() && Branch.DashEndAtMs<=NowMs)
        {
            const FLDDemoEffectDefinition Burst=Branch.DashEndBurst.GetValue();
            Branch.DashEndBurst.Reset();
            FLDElementTerms Terms;
            if (!ElementTerms(Terms,Error)) { return false; }
            const int64 R=LDCombatElements::ReactionDamageP2(GrowthLevelOf(Body),Body.AttributeProjection.Final.APP2,Terms);
            FLDCombatGroundEffect Blast;
            Blast.Payload=MakeHit(Body,Branch.DashEndExit,FLDCombatBodyRef(),Branch.DashEndEffectId);
            Blast.Payload.BaseP2=Share(R,Burst.RatioBp);
            Blast.Payload.DamageType=ELDDemoDamageType::Magical;
            Blast.Payload.NativeElements|=LDCombatElements::Bit(ELDCombatElement::Shock);
            Blast.Payload.bDirectAbilityDamage=true;
            Blast.Payload.CastLedger=Branch.DashEndLedger;
            if (!StampElementSnapshot(Body,Blast.Payload,Error) || !StampSourceTerms(Body,Blast.Payload,Error)) { return false; }
            Blast.CentreCm=FootOf(*Actor);
            Blast.RadiusCm=Blast.InnerRadiusCm=Cm(Burst.RadiusCm);
            Blast.Origin=ELDCombatGroundOrigin::Echo;
            Blast.ObservedKey=FGuid::NewGuid();
            if (!LDMatchLifecycle::AddDurationMs(NowMs,Burst.DelayMs,Blast.ReadyAtMs,Error)) { return false; }
            Blast.ExpiresAtMs=Blast.ReadyAtMs;
            GroundEffects.Add(MoveTemp(Blast));
            Branch.DashEndLedger.Reset();
        }
        // S14 A2: the shield ran out naturally; its remaining share heals (checked before statuses expire this boundary).
        if (Branch.ShieldEffectId.IsValid() && (!Body.Shield.IsSet() || Body.Shield->EffectId!=Branch.ShieldEffectId))
        {
            Branch.ShieldEffectId.Invalidate();
            Branch.ShieldReflectBp=Branch.ShieldEndHealBp=0;
            Branch.ShieldBreakBurst.Reset();
        }
        else if (Branch.ShieldEndHealBp>0 && Body.Shield.IsSet() && Body.Shield->ExpiresAtMs<=NowMs && Body.Resources.ShieldP2>0)
        {
            const int64 Heal=Share(Body.Resources.ShieldP2,Branch.ShieldEndHealBp);
            Branch.ShieldEndHealBp=0;
            if (Heal>0 && !HealBody(Body,Heal,Branch.ShieldExit,nullptr,FGuid(),&Error) && !Error.IsEmpty()) { return false; }
        }
        // A brace that ended clears its branch terms after its answer used them (AdvanceElements runs first).
        if (Branch.BraceEffectId.IsValid() && Body.Brace.EffectId!=Branch.BraceEffectId)
        {
            Branch.BraceEffectId.Invalidate();
            Branch.BraceStunMs=Branch.BraceHealBp=Branch.BraceStackBp=Branch.BraceShieldMs=Branch.BraceShoveMs=0;
            Branch.BraceShoveCm=0;
        }
    }
    return true;
}

int64 ALDCombatDirector::NextBranchDueMs(int64 NowMs) const
{
    int64 Due=MAX_int64;
    const auto Consider=[&Due,NowMs](int64 At) { if (At>NowMs && At<Due) { Due=At; } };
    for (const FLDCombatFollowUp& Row : FollowUps) { Consider(Row.DueMs); }
    for (const FLDCombatPulse& Row : Pulses) { Consider(Row.NextMs); }
    for (const auto& Pair : Bodies)
    {
        const FLDCombatBranchState& Branch=Pair.Value.Branch;
        if (Branch.DashEndBurst.IsSet()) { Consider(Branch.DashEndAtMs); }
        if (Pair.Value.Shield.IsSet() && Branch.ShieldEndHealBp>0) { Consider(Pair.Value.Shield->ExpiresAtMs); }
        if (Branch.Recast.IsSet()) { Consider(Branch.Recast->UntilMs); }
    }
    return Due;
}

void ALDCombatDirector::ClearBranchState(FLDBodyCombatState& Body)
{
    Body.Branch=FLDCombatBranchState();
    const FGuid Entity=Body.Victim.EntityId;
    FollowUps.RemoveAll([&Entity](const FLDCombatFollowUp& Row) { return Row.Payload.SourceEntity==Entity || Row.Payload.Target.Victim.EntityId==Entity; });
    Pulses.RemoveAll([&Entity](const FLDCombatPulse& Row) { return Row.Template.SourceEntity==Entity; });
}

bool ALDCombatDirector::OnBranchHitTaken(const FLDCombatHit& Hit, FLDBodyCombatState& Target, int64 DamageP2, bool bShieldBroken,
    int64 NowMs, FString& Error)
{
    using namespace LDCombatBranchDetail;
    // A11/K13 rider damage triggers nothing, the taker's branch effects included.
    if (Hit.SourceEntity==Target.Victim.EntityId || Hit.bRider) { return true; }
    FLDCombatBranchState& Branch=Target.Branch;
    const FLDCombatBodyRef TargetRef{Target.Victim,Target.Identity};
    const bool bOrdinary=!Hit.bReflect && !Hit.bPeriodic && !Hit.bBurnTick && !Hit.bStatusSettle;
    // S10 B1: every damaging hit on an overcharged body discharges once more for its applier.
    if (Branch.OverchargeUntilMs>NowMs && DamageP2>0 && !Hit.bOverchargeDischarge && !Hit.bBurnTick)
    {
        const FLDDemoContentSnapshot* Content=Catalog();
        FLDElementTerms Terms;
        if (!Content || !ElementTerms(Terms,Error))
        {
            if (Error.IsEmpty()) { Error=TEXT("A discharge needs the approved content."); }
            return false;
        }
        const int64 R=LDCombatElements::ReactionDamageP2(Branch.OverchargeLevel,Branch.OverchargeAPP2,Terms);
        FLDCombatHit Discharge;
        Discharge.HitId=FGuid::NewGuid();
        Discharge.EffectId=FGuid::NewGuid();
        Discharge.Target=TargetRef;
        Discharge.SourceBody=Branch.OverchargeSource;
        Discharge.SourceEntity=Branch.OverchargeSource.Victim.EntityId;
        Discharge.CreditOwner=Branch.OverchargeSource.Victim.Kind==ELDVictimKind::Contender ? Discharge.SourceEntity : FGuid();
        Discharge.Source=Branch.OverchargeExit;
        Discharge.HitClass=ELDCombatHitClass::Ability;
        Discharge.BaseP2=Share(R,Content->Get().Parameters.Overdrive.ShockReactionBp);
        Discharge.DamageType=ELDDemoDamageType::Magical;
        Discharge.bStatusSettle=true;
        Discharge.bOverchargeDischarge=true;
        PendingHits.Add(MoveTemp(Discharge));
    }
    // S14 A1: the shield of this branch reflects a share of the direct damage it took.
    const bool bShieldOfBranch=Branch.ShieldEffectId.IsValid()
        && ((Target.Shield.IsSet() && Target.Shield->EffectId==Branch.ShieldEffectId) || bShieldBroken);
    if (bShieldOfBranch && Branch.ShieldReflectBp>0 && DamageP2>0 && bOrdinary && FindBody(Hit.SourceBody))
    {
        FLDCombatHit Reflect=MakeHit(Target,Branch.ShieldExit,Hit.SourceBody,FGuid::NewGuid());
        Reflect.BaseP2=Share(DamageP2,Branch.ShieldReflectBp);
        Reflect.DamageType=Hit.DamageType;
        Reflect.bReflect=true;
        Reflect.bFromShieldTrigger=true;
        if (Reflect.BaseP2>0) { PendingHits.Add(MoveTemp(Reflect)); }
    }
    // S13 A1: the shield of this branch bursts when it breaks.
    if (bShieldBroken && bShieldOfBranch && Branch.ShieldBreakBurst.IsSet())
    {
        const FLDDemoEffectDefinition Burst=Branch.ShieldBreakBurst.GetValue();
        Branch.ShieldBreakBurst.Reset();
        if (const ALDCharacter* Actor=ActorOf(Target))
        {
            TArray<FLDCombatBodyRef> Around;
            if (!CollectAreaTargets(FootOf(*Actor),Cm(Burst.RadiusCm),&Target,Target.Victim.EntityId,Around,Error)) { return false; }
            const FGuid BurstId=FGuid::NewGuid();
            for (const FLDCombatBodyRef& Other : Around)
            {
                FLDCombatHit Blast=MakeHit(Target,Branch.ShieldExit,Other,BurstId);
                Blast.BaseP2=Burst.BaseP2;
                Blast.APRatioBp=Burst.APRatioBp;
                Blast.DamageType=ELDDemoDamageType::Magical;
                Blast.bFromShieldTrigger=true;
                if (!StampSourceTerms(Target,Blast,Error)) { return false; }
                PendingHits.Add(MoveTemp(Blast));
            }
        }
    }
    // S16 B: each hit taken while the brace holds raises the next primary basic.
    if (Branch.BraceStackBp>0 && DamageP2>0 && Target.Brace.EffectId.IsValid() && Target.Brace.EffectId==Branch.BraceEffectId
        && Target.Brace.EndsAtMs>NowMs)
    {
        Branch.NextBasicAmpBp+=Branch.BraceStackBp;
        Branch.NextBasicAmpUntilMs=FMath::Max(Branch.NextBasicAmpUntilMs,Target.Brace.EndsAtMs+3000);
    }
    return true;
}

bool ALDCombatDirector::EndBranchProjectile(const FLDCombatProjectile& Projectile, const FVector& EndCm, bool bReachedRange,
    FString& Error)
{
    ILDLifeAuthority* Mode=Authority();
    if (!Mode) { Error=TEXT("A flight end needs the current core."); return false; }
    const int64 NowMs=Mode->GetAuthorityNowMs();
    // A telegraphed flight can be perfectly dodged where it went (spec §4.4): its corridor from launch to end.
    if (Projectile.bTelegraphed && !Projectile.bReturn)
    {
        const FVector From(Projectile.OriginCm.X,Projectile.OriginCm.Y,0), To(EndCm.X,EndCm.Y,0);
        const double Reach=Projectile.RadiusCm+40.;
        if (!ResolvePerfectDodges(Projectile.EffectId,Projectile.SourceEntity,[&From,&To,Reach](const FVector& Point)
            { return FMath::PointDistToSegment(FVector(Point.X,Point.Y,0),From,To)<=Reach; },NowMs,Error)) { return false; }
    }
    // S06 B2: a return that reached its living thrower was caught.
    if (Projectile.bReturn && bReachedRange && Projectile.Payload.CastLedger)
    {
        FLDBodyCombatState* Thrower=FindBody(Projectile.SourceBody);
        for (const FLDDemoEffectDefinition& Rider : Projectile.Payload.CastLedger->Riders)
        {
            if (!Thrower || Thrower->bPendingDeath || Rider.GetOp()!=ELDDemoEffectOp::NextBasicAmp || !Rider.bOnCatch) { continue; }
            Thrower->Branch.NextBasicAmpBp=FMath::Max(Thrower->Branch.NextBasicAmpBp,Rider.RatioBp);
            if (!LDMatchLifecycle::AddDurationMs(NowMs,Rider.DurationMs,Thrower->Branch.NextBasicAmpUntilMs,Error)) { return false; }
        }
    }
    // N3 and S21 A2: where the outbound flight ends (its hit point or its range) it leaves its ground state, owned by the
    // thrower at the release snapshot (one live instance per skill; Y12 adds one).
    if (!Projectile.bReturn && Projectile.LandingGround.IsSet())
    {
        const FLDDemoEffectDefinition& Landing=Projectile.LandingGround.GetValue();
        const FLDCombatHit& Payload=Projectile.Payload;
        FLDGroundTerms Ground;
        if (!GroundTerms(Ground,Error)) { return false; }
        FLDElementApplier LandingOwner;
        LandingOwner.SourceEntity=Payload.SourceEntity;
        LandingOwner.SourceBody=Payload.SourceBody;
        LandingOwner.CreditOwner=Payload.CreditOwner;
        LandingOwner.Source=Payload.Source;
        LandingOwner.Level=FMath::Max(Payload.ElementLevel,1);
        LandingOwner.SourceAPP2=Payload.ElementAPP2;
        LandingOwner.EffectId=Payload.EffectId;
        const ELDGroundStateKind Kind=static_cast<ELDGroundStateKind>(Landing.GroundKind);
        const double Radius=Landing.RatioBp>0 ? Ground.Of(Kind).RadiusCm*double(Landing.RatioBp)/10000. : 0.;
        if (!SpawnGround(Kind,LandingOwner,EndCm,NowMs,Error,Radius,Landing.DurationMs,FVector2D(1,0),0,0,Landing.SecondaryRatioBp,
            Landing.MultiplierBp)) { return false; }
    }
    return true;
}

bool ALDCombatDirector::StrikeTrapWire(FLDCombatGroundEffect& Ground, const FLDBodyCombatState& Holder, FString& Error)
{
    using namespace LDCombatBranchDetail;
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!Content) { Error=TEXT("A trap wire requires the approved Demo content."); return false; }
    // A body is on the wire when its capsule touches the segment between the two trap points.
    const double HalfWidthCm=Cm(Content->Get().Parameters.CollisionRadiusCm);
    for (const FLDCombatGroundEffect& Other : GroundEffects)
    {
        // Each pair is wired once: from the older trap to the owner's next newer armed one within reach.
        if (&Other==&Ground || !Other.bTrap || Other.bTriggered || Other.Payload.SourceEntity!=Ground.Payload.SourceEntity
            || Other.CreatedSerial<=Ground.CreatedSerial) { continue; }
        const FVector Span(Other.CentreCm.X-Ground.CentreCm.X,Other.CentreCm.Y-Ground.CentreCm.Y,0);
        const double LengthCm=Span.Size();
        if (LengthCm<=0 || LengthCm>Ground.WireCm) { continue; }
        TArray<TPair<FLDCombatBodyRef,FVector>> Crossing;
        if (!CollectLineTargets(Ground.CentreCm,Span,LengthCm,HalfWidthCm,Holder,Crossing,Error)) { return false; }
        for (const TPair<FLDCombatBodyRef,FVector>& Pair : Crossing)
        {
            const FGuid Struck=Pair.Key.Victim.EntityId;
            if (Ground.WireStruck.Contains(Struck)) { continue; }
            Ground.WireStruck.Add(Struck);
            FLDCombatHit Wire=MakeHit(Holder,Ground.Payload.Source,Pair.Key,Ground.Payload.EffectId);
            Wire.BaseP2=Ground.WireBaseP2;
            Wire.APRatioBp=Ground.WireAPRatioBp;
            Wire.DamageType=ELDDemoDamageType::Magical;
            Wire.bAddedDamage=true;
            PendingHits.Add(MoveTemp(Wire));
            UE_LOG(LogLDCombat,Log,TEXT("LD_COMBAT_TRAP_WIRE owner=%s target=%s"),*LDLog::Id(Ground.Payload.SourceEntity),*LDLog::Id(Struck));
        }
        break;
    }
    return true;
}

bool ALDCombatDirector::SpawnLingerZone(const FLDCombatHit& Payload, const FVector& Centre, double RadiusCm, int64 LingerMs,
    int64 NowMs, FString& Error)
{
    FLDCombatZone Zone;
    Zone.EffectId=Payload.EffectId;
    Zone.ObservedKey=FGuid::NewGuid();
    Zone.SourceBody=Payload.SourceBody;
    Zone.SourceEntity=Payload.SourceEntity;
    Zone.CreditOwner=Payload.CreditOwner;
    Zone.Source=Payload.Source;
    Zone.CentreCm=Centre;
    Zone.RadiusCm=RadiusCm;
    Zone.StartMs=NowMs;
    Zone.IntervalMs=1000;
    // The impact itself was the first wetting; the area wets once a second after it.
    Zone.DelayMs=1000;
    Zone.Count=int32(FMath::Clamp<int64>(LingerMs/1000,1,20));
    if (!LDMatchLifecycle::AddDurationMs(NowMs,LingerMs,Zone.EndMs,Error)) { return false; }
    Zone.bWet=true;
    Zone.ElementLevel=Payload.ElementLevel;
    Zone.ElementAPP2=Payload.ElementAPP2;
    Zone.CastLedger=Payload.CastLedger;
    Zone.CreatedSerial=++NextAreaSerial;
    Zones.Add(MoveTemp(Zone));
    return true;
}
