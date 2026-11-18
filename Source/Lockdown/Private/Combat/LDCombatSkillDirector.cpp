#include "Combat/LDCombatDirector.h"
#include "Combat/LDDashContactRules.h"
#include "Combat/LDCombatSourceRules.h"
#include "Combat/LDControlRules.h"
#include "Core/LDLog.h"
#include "Combat/LDTacticalRules.h"
#include "Character/LDCharacter.h"
#include "Character/LDNativeMotionTypes.h"
#include "Components/CapsuleComponent.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDLifeAuthority.h"
#include "Region/LDMapRegistry.h"
#include "Vision/LDVisionDirector.h"
#include "Engine/World.h"

namespace LDCombatSkillDetail
{
    double Cm(const FLDDemoRational& Value) { return double(Value.Numerator) / double(Value.Denominator); }
    int64 Share(int64 Value, int64 Bp) { return Value / 10000 * Bp + Value % 10000 * Bp / 10000; }
    const FLDDemoEffectDefinition* Op(const FLDDemoAbilityValues& Values, ELDDemoEffectOp Kind)
    { return Values.Effects.FindByPredicate([Kind](const auto& Effect) { return Effect.GetOp() == Kind; }); }

    struct FSpan
    {
        FVector From, To;
        double Start=0, End=0;
        FVector At(double Time) const
        { return End > Start ? FMath::Lerp(From, To, FMath::Clamp((Time - Start) / (End - Start), 0., 1.)) : To; }
    };

    void TargetSpans(const FLDNativeMotionBatch& Batch, TArray<FSpan>& Out)
    {
        double Cursor=Batch.Step.StartServerSeconds;
        FVector Foot=Batch.StartFootCm;
        for (const auto& Record : Batch.Records)
        {
            if (Record.StartServerSeconds > Cursor) { Out.Add({Foot, Foot, Cursor, Record.StartServerSeconds}); }
            if (Record.Kind == ELDNativeMotionRecordKind::Movement && Record.EndServerSeconds > Record.StartServerSeconds)
            { Out.Add({Record.FromFootCm, Record.ToFootCm, Record.StartServerSeconds, Record.EndServerSeconds}); }
            Cursor=Record.EndServerSeconds;
            Foot=Record.ToFootCm;
        }
        if (Cursor < Batch.Step.EndServerSeconds) { Out.Add({Foot, Foot, Cursor, Batch.Step.EndServerSeconds}); }
    }

    struct FContact
    {
        TSharedPtr<FLDCombatDashContact> Dash;
        FLDCombatBodyRef Target;
        double At=0;
    };
}

bool ALDCombatDirector::IsAggressiveEntry(const FLDCombatBodyRef& Ref, ELDCombatEntry Entry) const
{
    if (Entry != ELDCombatEntry::Dash) { return true; }
    const auto* Body=FindBody(Ref);
    if (!Body) { return false; }
    // D06 and D12: a D that strikes or shocks on contact is an offensive entry.
    TArray<FLDPassiveEffectRef> Phases, Shocks;
    FString Error;
    FLDCombatSourceRef Innate;
    Innate.Kind=ELDCombatSourceKind::Innate;
    Innate.DefinitionId=LDCombatSourceRules::InnateDashId();
    if (!CardsOn(Body->AttributeProjection, ELDDemoEffectOp::DashPhase, Innate, Phases, Error)
        || !CardsOn(Body->AttributeProjection, ELDDemoEffectOp::DashContactElement, Innate, Shocks, Error))
    {
        UE_LOG(LogLDCombat, Error, TEXT("LD_COMBAT_TRIGGER_UNRESOLVED body=%s op=dash_contact error=%s"),
            *LDLog::Id(Ref.Victim.EntityId), *Error);
        return true;
    }
    return !Phases.IsEmpty() || !Shocks.IsEmpty();
}

void ALDCombatDirector::CaptureSkillMotion(TSharedPtr<const FLDNativeMotionBatch> Batch)
{
    const auto* Body=FindBody(Batch->Body);
    if (Authority() && Authority()->IsShowdown()) { FinalMotion.Add({Batch,Body ? Body->VoluntaryDash : TOptional<FLDCombatVoluntaryDash>()}); }
    ContactMotion.Add({MoveTemp(Batch), Body ? Body->DashContact : nullptr});
}

void ALDCombatDirector::CancelChannel(FLDBodyCombatState& Body)
{
    if (!Body.ChannelEffectId.IsValid()) { return; }
    // The channel record itself ends at the next advance (AdvanceSkillContent no longer finds its owner).
    Body.ChannelEffectId.Invalidate();
    Body.ChannelDirectSequence=0;
    ++Body.StateRevision;
    MarkViewChanged(Body.Victim.EntityId);
}

bool ALDCombatDirector::StartSkillDelivery(FLDBodyCombatState& Body, const FLDCombatActiveTerms& Terms, const FLDCombatAction& Action,
    FLDCombatHit& Payload, int64 NowMs, bool& bHandled, FString& Error)
{
    using namespace LDCombatSkillDetail;
    bHandled=false;
    const FLDDemoAbilityValues& Values=Terms.Values;
    const FLDDemoTargetDefinition& Target=Terms.Target;
    if (const auto* Cone=Op(Values, ELDDemoEffectOp::ChannelCone))
    {
        FLDCombatChannel Channel;
        Channel.Payload=Payload;
        Channel.Payload.bChannelHit=true;
        Channel.Direction=Action.Direction;
        Channel.RangeCm=Cm(Target.RangeCm);
        Channel.AngleDegrees=Cm(Target.AngleDegrees);
        Channel.NextPulseMs=NowMs;
        Channel.IntervalMs=Cone->IntervalMs;
        Channel.Remaining=Cone->Count;
        Channel.FinalSlowBp=Cone->SecondaryRatioBp;
        Channel.FinalSlowMs=Cone->SecondaryDurationMs;
        // S22 A2: the last waves (not only the final one) slow.
        Channel.SlowWaves=FMath::Max(Cone->BasicCount,1);
        // Spec §5: S22's gust rides on the last wave only; N1 B1 roots on its last wave.
        Channel.bFinalGust=(Payload.NativeElements & LDCombatElements::Bit(ELDCombatElement::Gust))!=0;
        Channel.Payload.NativeElements&=~LDCombatElements::Bit(ELDCombatElement::Gust);
        Channel.FinalRootMs=Cone->ExtraDurationMs;
        // N1 A: the cone leaves its ice over the whole sector, owned by the caster at the release snapshot.
        if (const auto* Ice=Op(Values, ELDDemoEffectOp::GroundState))
        {
            ALDCharacter* Caster=ActorOf(Body);
            if (!Caster) { Error=TEXT("A cone skill lost its releasing body."); return false; }
            FLDElementApplier IceOwner;
            IceOwner.SourceEntity=Body.Victim.EntityId;
            IceOwner.SourceBody={Body.Victim,Body.Identity};
            IceOwner.CreditOwner=Payload.CreditOwner;
            IceOwner.Source=Payload.Source;
            IceOwner.Level=FMath::Max(Payload.ElementLevel,1);
            IceOwner.SourceAPP2=Payload.ElementAPP2;
            IceOwner.EffectId=Payload.EffectId;
            if (!SpawnGround(static_cast<ELDGroundStateKind>(Ice->GroundKind),IceOwner,FootOf(*Caster),NowMs,Error,Channel.RangeCm,
                Ice->DurationMs,FVector2D(Action.Direction.X,Action.Direction.Y),0,Channel.AngleDegrees,Ice->SecondaryRatioBp,
                Ice->MultiplierBp)) { return false; }
        }
        if (Terms.FinalKnockbackTerms.IsSet())
        {
            // A branch's final wave (S22) pushes everything it hits away from the caster instead of slowing it.
            Channel.FinalKnockbackCm=Cm(Terms.FinalKnockbackTerms->DistanceCm);
            Channel.FinalKnockbackMs=Terms.FinalKnockbackTerms->DurationMs;
            Channel.FinalSlowBp=0;
            Channel.FinalSlowMs=0;
        }
        Body.ChannelEffectId=Payload.EffectId;
        Body.ChannelDirectSequence=Action.Key.DirectSequence;
        Body.ChannelStartedAtMs=NowMs;
        Channels.Add(MoveTemp(Channel));
        bHandled=true;
        return true;
    }
    // N2: the fan stands at the target point (within reach) and blows toward the cast direction.
    if (const auto* Blow=Op(Values, ELDDemoEffectOp::Fan))
    {
        const auto* Actor=ActorOf(Body);
        if (!Actor) { Error=TEXT("A fan skill lost its releasing body."); return false; }
        const FVector Origin=FootOf(*Actor);
        const FVector Offset(Action.TargetPoint.X-Origin.X, Action.TargetPoint.Y-Origin.Y, 0);
        FLDCombatFan Fan;
        Fan.Payload=Payload;
        Fan.ObservedKey=FGuid::NewGuid();
        Fan.OriginCm=Origin + Offset.GetClampedToMaxSize(Cm(Target.RangeCm));
        Fan.OriginCm.Z=Action.TargetPoint.Z;
        Fan.Direction=FVector(Action.Direction.X, Action.Direction.Y, 0).GetSafeNormal();
        if (Fan.Direction.IsNearlyZero()) { Fan.Direction=Offset.GetSafeNormal2D(); }
        if (Fan.Direction.IsNearlyZero()) { Fan.Direction=FVector(1,0,0); }
        Fan.LengthCm=Cm(Blow->RadiusCm);
        Fan.HalfWidthCm=Cm(Blow->ExtraDistanceCm)*.5;
        Fan.SweepDegrees=Cm(Blow->AngleDegrees);
        Fan.Payload.GustPushCm=Cm(Blow->DistanceCm);
        Fan.Payload.KnockbackWallStunMs=Blow->ExtraDurationMs;
        Fan.bCarryElement=Blow->bCarryElement;
        Fan.MomentumP2=Blow->BaseP2;
        Fan.StartMs=NowMs;
        Fan.NextBlowMs=NowMs;
        Fan.IntervalMs=Blow->IntervalMs;
        Fan.CreatedSerial=++NextAreaSerial;
        if (Fan.LengthCm<=0 || Fan.HalfWidthCm<=0 || Fan.IntervalMs<=0
            || !LDMatchLifecycle::AddDurationMs(NowMs, Blow->DurationMs, Fan.EndMs, Error))
        {
            if (Error.IsEmpty()) { Error=TEXT("A fan needs a positive rectangle and rhythm."); }
            return false;
        }
        // A skill keeps one live fan (Y12: one more per copy).
        int32 Allowed=1;
        TArray<FLDPassiveEffectRef> Stalls;
        if (!CollectPassiveEffects(Body.AttributeProjection, ELDDemoEffectOp::DoubleStall, Stalls, Error)) { return false; }
        Allowed+=Stalls.Num();
        TArray<int32> Owned;
        for (int32 Index=0; Index < Fans.Num(); ++Index)
        {
            if (Fans[Index].Payload.SourceEntity == Body.Victim.EntityId && Fans[Index].Payload.Source == Payload.Source) { Owned.Add(Index); }
        }
        for (int32 Drop=Owned.Num()-Allowed; Drop >= 0; --Drop) { Fans.RemoveAt(Owned[Drop]); }
        Fans.Add(MoveTemp(Fan));
        bHandled=true;
        return true;
    }
    const auto* Impact=Op(Values, ELDDemoEffectOp::GroundImpact);
    const auto* Trap=Op(Values, ELDDemoEffectOp::ContactTrap);
    if (Impact || Trap)
    {
        const auto& Shape=Impact ? *Impact : *Trap;
        const auto* Actor=ActorOf(Body);
        if (!Actor) { Error=TEXT("A ground skill lost its releasing body."); return false; }
        const FVector Origin=FootOf(*Actor);
        const FVector Offset(Action.TargetPoint.X-Origin.X, Action.TargetPoint.Y-Origin.Y, 0);
        const double Reach=Cm(Target.RangeCm);
        FLDCombatGroundEffect Ground;
        Ground.Payload=Payload;
        Ground.CentreCm=Origin + Offset.GetClampedToMaxSize(Reach);
        Ground.CentreCm.Z=Action.TargetPoint.Z;
        Ground.RadiusCm=Cm(Shape.RadiusCm);
        Ground.InnerRadiusCm=Cm(Shape.DistanceCm);
        Ground.OuterRatioBp=Impact ? Shape.RatioBp : 10000;
        Ground.bTrap=Trap != nullptr;
        Ground.Origin=Trap ? ELDCombatGroundOrigin::Trap : ELDCombatGroundOrigin::Impact;
        // M15: the landing or detonation echoes once (AdvanceSkillContent).
        Ground.Echo=Terms.EchoImpact;
        // Skill branches: S18 B1's center stun and S20 B2's wet area; S19 A1's wire.
        Ground.CenterStunMs=Impact ? Shape.ExtraDurationMs : 0;
        Ground.LingerMs=Impact ? Shape.SecondaryDurationMs : 0;
        if (Trap && Shape.ExtraDistanceCm.Denominator>0 && Shape.ExtraDistanceCm.Numerator>0)
        {
            Ground.WireCm=Cm(Shape.ExtraDistanceCm);
            Ground.WireBaseP2=Shape.BaseP2;
            Ground.WireAPRatioBp=Shape.APRatioBp;
        }
        if (Trap)
        {
            // S19 (spec §9): armed from its placement for its life; a trigger starts its own fuse (AdvanceSkillContent).
            Ground.ReadyAtMs=NowMs;
            Ground.FuseMs=Shape.DelayMs;
            if (Ground.FuseMs < 0 || !LDMatchLifecycle::AddDurationMs(NowMs, Shape.DurationMs, Ground.ExpiresAtMs, Error)) { return false; }
        }
        else if (!LDMatchLifecycle::AddDurationMs(NowMs, Shape.DelayMs, Ground.ReadyAtMs, Error)
            || !LDMatchLifecycle::AddDurationMs(Ground.ReadyAtMs, Shape.DurationMs, Ground.ExpiresAtMs, Error)) { return false; }
        if (Trap)
        {
            // One trap per owner (content §4; S19 A raises it, Y12 adds one per copy): a newer one ends the oldest, armed or
            // fusing, without its blast.
            TArray<FLDPassiveEffectRef> Stalls;
            if (!CollectPassiveEffects(Body.AttributeProjection, ELDDemoEffectOp::DoubleStall, Stalls, Error)) { return false; }
            const int32 Allowed=FMath::Max(Shape.Count,1)+Stalls.Num();
            TArray<int32> Owned;
            for (int32 Index=0; Index < GroundEffects.Num(); ++Index)
            {
                if (GroundEffects[Index].bTrap && GroundEffects[Index].Payload.SourceEntity == Body.Victim.EntityId) { Owned.Add(Index); }
            }
            Owned.Sort([this](int32 Left, int32 Right) { return GroundEffects[Left].CreatedSerial < GroundEffects[Right].CreatedSerial; });
            for (int32 Drop=Owned.Num()-Allowed; Drop >= 0; --Drop) { GroundEffects.RemoveAt(Owned[Drop]); }
            Ground.CreatedSerial=++NextAreaSerial;
        }
        GroundEffects.Add(MoveTemp(Ground));
        bHandled=true;
        return true;
    }
    if (const auto* Contact=Op(Values, ELDDemoEffectOp::DashContact))
    {
        if (!Body.DashContact || Body.DashContact->EffectId != Payload.EffectId)
        { Error=TEXT("A contact skill has no matching released dash."); return false; }
        Payload.bDashContactHit=true;
        Body.DashContact->NativeHit=Payload;
        Body.DashContact->NativeHitsRemaining=Contact->bEveryContact ? MAX_int32 : Contact->Count;
        bHandled=true;
    }
    return true;
}

FVector FLDCombatFan::HeadingAt(int64 AtMs) const
{
    if (SweepDegrees<=0) { return Direction; }
    // A triangle wave of amplitude SweepDegrees at SweepDegrees per second (one full swing takes four seconds).
    const double Phase=FMath::Fmod(double(AtMs-StartMs)/1000.,4.);
    const double Unit=Phase<1. ? Phase : Phase<3. ? 2.-Phase : Phase-4.;
    return Direction.RotateAngleAxis(Unit*SweepDegrees,FVector::UpVector);
}

bool ALDCombatDirector::AdvanceFans(int64 NowMs, FString& Error)
{
    FLDGroundTerms GroundRules;
    if (!Fans.IsEmpty() && !GroundTerms(GroundRules,Error)) { return false; }
    for (int32 Index=Fans.Num()-1; Index>=0; --Index)
    {
        FLDBodyCombatState* FanOwner=FindBody(Fans[Index].Payload.SourceBody);
        if (!FanOwner || FanOwner->bPendingDeath || Fans[Index].EndMs<=NowMs) { Fans.RemoveAt(Index); continue; }
        while (Fans.IsValidIndex(Index) && Fans[Index].NextBlowMs<=NowMs && Fans[Index].NextBlowMs<Fans[Index].EndMs)
        {
            FLDCombatFan& Fan=Fans[Index];
            const FVector Heading=Fan.HeadingAt(Fan.NextBlowMs);
            if (!LDMatchLifecycle::AddDurationMs(Fan.NextBlowMs,Fan.IntervalMs,Fan.NextBlowMs,Error)) { return false; }
            TArray<TPair<FLDCombatBodyRef,FVector>> Inside;
            if (!CollectLineTargets(Fan.OriginCm,Heading,Fan.LengthCm,Fan.HalfWidthCm,*FanOwner,Inside,Error)) { return false; }
            FLDCombatHit Blow=Fan.Payload;
            Blow.GustDirection=Heading;
            // A2: the owner's last element rides on the wind.
            if (Fan.bCarryElement && FanOwner->LastAppliedElement!=ELDCombatElement::None)
            {
                if (FanOwner->LastAppliedElement==ELDCombatElement::Fire) { Blow.BurnStacks=FMath::Max(Blow.BurnStacks,1); }
                else { Blow.NativeElements|=LDCombatElements::Bit(FanOwner->LastAppliedElement); }
            }
            for (const TPair<FLDCombatBodyRef,FVector>& Row : Inside)
            {
                FLDCombatHit Hit=Blow;
                Hit.HitId=FGuid::NewGuid();
                Hit.Target=Row.Key;
                PendingHits.Add(MoveTemp(Hit));
            }
            // B1: a blow that reached an enemy refills momentum.
            if (!Inside.IsEmpty() && Fan.MomentumP2>0) { GrantMomentum(*FanOwner,Fan.MomentumP2,ELDMomentumBook::Skill); }
            // The wind moves fire and miasma (and discharges fields) whose centre stands in the blow.
            FLDElementApplier Wind;
            Wind.SourceEntity=Fan.Payload.SourceEntity;
            Wind.SourceBody=Fan.Payload.SourceBody;
            Wind.CreditOwner=Fan.Payload.CreditOwner;
            Wind.Source=Fan.Payload.Source;
            Wind.Level=FMath::Max(Fan.Payload.ElementLevel,1);
            Wind.SourceAPP2=Fan.Payload.ElementAPP2;
            TArray<FVector> Centres;
            const FVector Along=Heading.GetSafeNormal2D();
            for (const FLDGroundState& Ground : GroundStates)
            {
                const FVector Offset(Ground.CentreCm.X-Fan.OriginCm.X,Ground.CentreCm.Y-Fan.OriginCm.Y,0);
                const double Forward=FVector::DotProduct(Offset,Along);
                const double Side=FMath::Abs(Offset.X*Along.Y-Offset.Y*Along.X);
                if (Forward>=0 && Forward<=Fan.LengthCm && Side<=Fan.HalfWidthCm) { Centres.Add(Ground.CentreCm); }
            }
            for (const FVector& Centre : Centres)
            {
                if (!ConvertGroundAt(Centre,ELDCombatElement::Gust,Wind,Along,NowMs,Error)) { return false; }
            }
        }
        if (Fans.IsValidIndex(Index) && Fans[Index].EndMs<=NowMs) { Fans.RemoveAt(Index); }
    }
    return true;
}

bool ALDCombatDirector::AdvanceSkillContent(int64 NowMs, FString& Error)
{
    if (!AdvanceFans(NowMs,Error)) { return false; }
    for (int32 Index=Channels.Num()-1; Index>=0; --Index)
    {
        auto& Channel=Channels[Index];
        ALDCharacter* Actor=nullptr;
        auto* Body=FindBody(Channel.Payload.SourceBody, &Actor);
        if (!Body || !Actor || Body->bPendingDeath || Body->ChannelEffectId != Channel.Payload.EffectId
            || IsCastBlocked(*Body, ELDCombatSourceKind::Learned)
            || LDTactical::HasActiveRoot(Body->Knockbacks, NowMs) || LDControl::IsTaunted(Body->Taunt, NowMs))
        {
            if (Body && Body->ChannelEffectId == Channel.Payload.EffectId) { Body->ChannelEffectId.Invalidate(); }
            Channels.RemoveAt(Index);
            continue;
        }
        Actor->SetFacing(Channel.Direction.Rotation().Yaw);
        bool bFinalPushed=false;
        while (Channel.Remaining > 0 && Channel.NextPulseMs <= NowMs)
        {
            const FVector Origin=FootOf(*Actor);
            TArray<FLDCombatBodyRef> Targets;
            if (!CollectAreaTargets(Origin, Channel.RangeCm, Body, Body->Victim.EntityId, Targets, Error)) { return false; }
            const double CosHalfAngle=FMath::Cos(FMath::DegreesToRadians(Channel.AngleDegrees * .5));
            // Each wave of a telegraphed cone can be perfectly dodged (spec §4.4).
            {
                const FVector Facing=Channel.Direction;
                const double Range=Channel.RangeCm;
                if (!ResolvePerfectDodges(Channel.Payload.EffectId, Channel.Payload.SourceEntity, [&Origin, &Facing, Range, CosHalfAngle](const FVector& Point)
                {
                    const FVector Offset(Point.X-Origin.X, Point.Y-Origin.Y, 0);
                    return Offset.Size() <= Range && (Offset.IsNearlyZero() || FVector::DotProduct(Facing, Offset.GetSafeNormal()) >= CosHalfAngle);
                }, NowMs, Error)) { return false; }
            }
            for (const auto& Ref : Targets)
            {
                ALDCharacter* Target=nullptr;
                if (!FindBody(Ref, &Target) || !Target) { continue; }
                const FVector Direction=(FootOf(*Target)-Origin).GetSafeNormal2D();
                if (!Direction.IsNearlyZero() && FVector::DotProduct(Channel.Direction, Direction) < CosHalfAngle) { continue; }
                auto Hit=Channel.Payload;
                Hit.HitId=FGuid::NewGuid();
                Hit.Target=Ref;
                if (Channel.Remaining <= Channel.SlowWaves) { Hit.SlowBp=Channel.FinalSlowBp; Hit.SlowDurationMs=Channel.FinalSlowMs; }
                if (Channel.Remaining == 1)
                {
                    if (Channel.bFinalGust)
                    {
                        Hit.NativeElements|=LDCombatElements::Bit(ELDCombatElement::Gust);
                        Hit.GustDirection=Channel.Direction;
                    }
                    if (Channel.FinalRootMs > 0) { Hit.RootDurationMs=FMath::Max(Hit.RootDurationMs, Channel.FinalRootMs); }
                }
                if (Channel.Remaining == 1 && Channel.FinalKnockbackCm > 0)
                {
                    Hit.KnockbackCm=Channel.FinalKnockbackCm;
                    Hit.KnockbackMs=Channel.FinalKnockbackMs;
                    Hit.bKnockbackFromAnchor=true;
                    Hit.KnockbackAnchorCm=Origin;
                    Hit.KnockbackSource=Channel.Payload.Source;
                    bFinalPushed=true;
                }
                PendingHits.Add(MoveTemp(Hit));
            }
            --Channel.Remaining;
            if (Channel.Remaining > 0
                && !LDMatchLifecycle::AddDurationMs(Channel.NextPulseMs, Channel.IntervalMs, Channel.NextPulseMs, Error)) { return false; }
        }
        if (Channel.Remaining == 0)
        {
            Body->ChannelEffectId.Invalidate();
            Channels.RemoveAt(Index);
            ++Body->StateRevision;
            MarkViewChanged(Body->Victim.EntityId);
        }
    }
    for (int32 Index=GroundEffects.Num()-1; Index>=0; --Index)
    {
        auto& Ground=GroundEffects[Index];
        if (Ground.ReadyAtMs > NowMs) { continue; }
        auto* Holder=FindBody(Ground.Payload.SourceBody);
        TArray<FLDCombatBodyRef> Targets;
        // S19: an armed trap waits for a legal enemy within its trigger radius; untriggered at the end of its life it ends
        // without a blast. A trigger starts the fuse; the blast then lands at the trap's own point even past that life.
        if (Ground.bTrap && !Ground.bTriggered)
        {
            if (NowMs >= Ground.ExpiresAtMs) { GroundEffects.RemoveAt(Index); continue; }
            if (Ground.WireCm > 0 && Holder && !Holder->bPendingDeath && !StrikeTrapWire(Ground, *Holder, Error)) { return false; }
            if (!CollectAreaTargets(Ground.CentreCm, Ground.InnerRadiusCm, Holder,
                Ground.Payload.SourceEntity, Targets, Error)) { return false; }
            if (Targets.IsEmpty()) { continue; }
            Ground.bTriggered=true;
            if (!LDMatchLifecycle::AddDurationMs(NowMs, Ground.FuseMs, Ground.ReadyAtMs, Error)) { return false; }
            UE_LOG(LogLDCombat,Log,TEXT("LD_COMBAT_TRAP_TRIGGERED t=%lld owner=%s blast_at=%lld"),NowMs,
                *LDLog::Id(Ground.Payload.SourceEntity),Ground.ReadyAtMs);
            if (Ground.ReadyAtMs > NowMs) { continue; }
        }
        // C06 (#198 F) answers the natural completion; the blast record is copied first because the list may grow.
        const FLDCombatGroundEffect Completed=Ground;
        // A landing or a trap blast is telegraphed: it can be perfectly dodged (spec §4.4).
        {
            const FVector Centre=Ground.CentreCm;
            const double Radius=Ground.RadiusCm;
            if (!ResolvePerfectDodges(Ground.Payload.EffectId, Ground.Payload.SourceEntity,
                [&Centre, Radius](const FVector& Point) { return FVector::Dist2D(Centre, Point) <= Radius; }, NowMs, Error)) { return false; }
        }
        if (!CollectAreaTargets(Ground.CentreCm, Ground.RadiusCm, Holder, Ground.Payload.SourceEntity, Targets, Error)) { return false; }
        for (const auto& Ref : Targets)
        {
            ALDCharacter* Target=nullptr;
            if (!FindBody(Ref, &Target) || !Target) { continue; }
            auto Hit=Ground.Payload;
            Hit.HitId=FGuid::NewGuid();
            Hit.Target=Ref;
            if (!Ground.bTrap && FVector::Dist2D(Ground.CentreCm, FootOf(*Target)) > Ground.InnerRadiusCm)
            {
                Hit.DeliveryScaleBp=Hit.DeliveryScaleBp/10000*Ground.OuterRatioBp+Hit.DeliveryScaleBp%10000*Ground.OuterRatioBp/10000;
                Hit.RootDurationMs=0;
            }
            else if (Ground.CenterStunMs > 0) { Hit.StunDurationMs=FMath::Max(Hit.StunDurationMs, Ground.CenterStunMs); }
            // A blast pushes away from its own point.
            if (Hit.KnockbackCm > 0 && !Hit.bKnockbackPull) { Hit.bKnockbackFromAnchor=true; Hit.KnockbackAnchorCm=Ground.CentreCm; }
            PendingHits.Add(MoveTemp(Hit));
        }
        GroundEffects.RemoveAt(Index);
        if (Completed.LingerMs > 0
            && !SpawnLingerZone(Completed.Payload, Completed.CentreCm, Completed.RadiusCm, Completed.LingerMs, NowMs, Error)) { return false; }
        // A branch echo (S18 A): it keeps the impact's outline and falloff; S19's is a plain blast of the trap's radius
        // without its slow. Only a host's own completion carries Echo, so an echo never echoes.
        if (Completed.Echo.IsSet() && (Completed.Origin==ELDCombatGroundOrigin::Impact || Completed.Origin==ELDCombatGroundOrigin::Trap))
        {
            FLDCombatGroundEffect Blast=Completed;
            if (Blast.bTrap)
            {
                Blast.InnerRadiusCm=Blast.RadiusCm;
                Blast.OuterRatioBp=10000;
                Blast.Payload.SlowBp=Blast.Payload.SlowDurationMs=0;
            }
            if (!ScheduleEcho(Completed.Payload.SourceBody, Completed.Echo.GetValue(), Blast, NowMs, Error)) { return false; }
        }
    }
    return ResolveDashContacts(NowMs, Error);
}

bool ALDCombatDirector::ResolveDashContacts(int64 NowMs, FString& Error)
{
    using namespace LDCombatSkillDetail;
    if (ContactMotion.IsEmpty()) { return true; }
    if (!ContactMotion.ContainsByPredicate([](const auto& Frame) { return Frame.Dash.IsValid(); }))
    { ContactMotion.Reset(); return true; }
    const auto* Mode=Authority();
    const auto* Registry=GetWorld()->GetSubsystem<ULDMapRegistry>();
    if (!Mode || !Mode->GetVisionAuthority() || !Registry) { Error=TEXT("Dash contacts require the registered sight world."); return false; }
    TArray<FContact> Contacts;
    for (const auto& Frame : ContactMotion)
    {
        if (!Frame.Dash) { continue; }
        const auto& Dash=*Frame.Dash;
        ALDCharacter* SourceActor=nullptr;
        const auto* Source=FindBody(Dash.Body, &SourceActor);
        if (!Source || !SourceActor || Source->bPendingDeath || Source->bSuspended
            || (Source->Victim.Kind==ELDVictimKind::Contender && (Mode->IsContenderProtected(Source->Victim.EntityId)
                || Mode->IsInputLocked(Mode->GetMatchContext(),Source->Victim.EntityId)))) { continue; }
        const double SourceRadius=SourceActor->GetCapsuleComponent()->GetScaledCapsuleRadius();
        const double SourceHeight=SourceActor->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        for (const auto& TargetFrame : ContactMotion)
        {
            if (TargetFrame.Batch->Body == Dash.Body || Dash.Struck.Contains(TargetFrame.Batch->Body)) { continue; }
            ALDCharacter* TargetActor=nullptr;
            const auto* Target=FindBody(TargetFrame.Batch->Body, &TargetActor);
            if (!Target || !TargetActor || Target->bPendingDeath || Target->bSuspended || IsIsolated(Source->Victim.EntityId,*Target)
                || (Source->Victim.Kind==ELDVictimKind::CampMember && Target->Victim.Kind!=ELDVictimKind::Contender)
                || (Target->Victim.Kind == ELDVictimKind::Contender && Mode->IsContenderProtected(Target->Victim.EntityId))) { continue; }
            const double TargetRadius=TargetActor->GetCapsuleComponent()->GetScaledCapsuleRadius();
            const double TargetHeight=TargetActor->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
            TArray<FSpan> Targets;
            TargetSpans(*TargetFrame.Batch, Targets);
            TOptional<double> First;
            for (const auto& Record : Frame.Batch->Records)
            {
                if (Record.Kind != ELDNativeMotionRecordKind::Movement || Record.EndServerSeconds <= Record.StartServerSeconds) { continue; }
                const FSpan Moving{Record.FromFootCm, Record.ToFootCm, Record.StartServerSeconds, Record.EndServerSeconds};
                for (const auto& Station : Targets)
                {
                    const double Start=FMath::Max3(Moving.Start, Station.Start, double(Dash.StartMs)/1000.);
                    const double End=FMath::Min3(Moving.End, Station.End, double(Dash.EndMs)/1000.);
                    if (End <= Start) { continue; }
                    TOptional<double> Fraction;
                    if (!LDDashContactRules::FirstContact(Moving.At(Start)+FVector(0,0,SourceHeight),
                        Moving.At(End)+FVector(0,0,SourceHeight), Station.At(Start)+FVector(0,0,TargetHeight),
                        Station.At(End)+FVector(0,0,TargetHeight), SourceRadius+TargetRadius,
                        SourceHeight+TargetHeight-SourceRadius-TargetRadius, Fraction, Error)) { return false; }
                    if (!Fraction.IsSet()) { continue; }
                    const double At=Start+(End-Start)*Fraction.GetValue();
                    const auto Sight=Mode->GetVisionAuthority()->TraceSight(Moving.At(At), Station.At(At), Registry->Revision());
                    if (Sight == ELDSightResult::Blocked) { continue; }
                    if (Sight != ELDSightResult::Visible) { Error=TEXT("A native dash contact left the registered sight surface."); return false; }
                    if (!First.IsSet() || At < First.GetValue()) { First=At; }
                }
            }
            if (First.IsSet()) { Contacts.Add({Frame.Dash, TargetFrame.Batch->Body, First.GetValue()}); }
        }
    }
    Contacts.Sort([](const FContact& A, const FContact& B)
    {
        if (A.At != B.At) { return A.At < B.At; }
        if (A.Dash->Body.Victim.EntityId != B.Dash->Body.Victim.EntityId) { return A.Dash->Body.Victim.EntityId < B.Dash->Body.Victim.EntityId; }
        return A.Target.Victim.EntityId < B.Target.Victim.EntityId;
    });
    for (const auto& Contact : Contacts)
    {
        auto& Dash=*Contact.Dash;
        auto* Body=FindBody(Dash.Body);
        if (!Body || Body->bPendingDeath || Dash.Struck.Contains(Contact.Target)) { continue; }
        Dash.Struck.Add(Contact.Target);
        const bool bNativeHit=Dash.NativeHit.IsSet() && Dash.NativeHitsRemaining>0;
        if (bNativeHit)
        {
            auto Hit=Dash.NativeHit.GetValue();
            Hit.HitId=FGuid::NewGuid();
            Hit.Target=Contact.Target;
            PendingHits.Add(MoveTemp(Hit));
            --Dash.NativeHitsRemaining;
        }
        if (Body->Victim.Kind==ELDVictimKind::CampMember) { continue; }
        // D06: a phasing dash strikes every body it passes with a basic (no combo step); D12 shocks it.
        if (Dash.bPhase)
        {
            FLDCombatHit Strike;
            if (!MakeBasicStrike(*Body, Contact.Target, Strike, Error)) { return false; }
            PendingHits.Add(MoveTemp(Strike));
            LedgerCardTriggered(*Body, FName(TEXT("card_pass_through")));
        }
        TArray<FLDPassiveEffectRef> Shocks;
        if (!CardsOn(Body->AttributeProjection, ELDDemoEffectOp::DashContactElement, Dash.Source, Shocks, Error)) { return false; }
        FLDBodyCombatState* Struck=FindBody(Contact.Target);
        for (const FLDPassiveEffectRef& Shock : Shocks)
        {
            if (!Struck || Struck->bPendingDeath) { break; }
            FLDElementApplier Applier;
            Applier.SourceEntity=Body->Victim.EntityId;
            Applier.SourceBody=Dash.Body;
            Applier.CreditOwner=Body->Victim.Kind==ELDVictimKind::Contender ? Body->Victim.EntityId : FGuid();
            Applier.Source=Shock.Source;
            Applier.Level=GrowthLevelOf(*Body);
            Applier.SourceAPP2=FMath::Max<int64>(Body->AttributeProjection.Final.APP2, 0);
            const ELDCombatElement Kind=LDCombatElements::FromDemo(Shock.Effect.Element);
            if (!ApplyElement(*Struck, Kind, Applier, Kind==ELDCombatElement::Fire ? 1 : 0, FGuid::NewGuid(), NowMs, Error)) { return false; }
            LedgerCardTriggered(*Body, Shock.Key);
        }
        // D02: the hosted dash's first real contact cuts the holder's skills (once per action).
        if (!HasteOtherActives(*Body, Dash.Source, Dash.EffectId, NowMs, Error)) { return false; }
        if (!OnDashContact(*Body, Dash, NowMs, Error)) { return false; }
        UE_LOG(LogLDCombat,Log,TEXT("LD_DASH_CONTACT t=%lld src=%s target=%s effect=%s native=%d phase=%d"),
            NowMs,*LDLog::Id(Body->Victim.EntityId),*LDLog::Id(Contact.Target.Victim.EntityId),
            *LDLog::Id(Dash.EffectId),bNativeHit,Dash.bPhase);
    }
    ContactMotion.Reset();
    return true;
}

bool ALDCombatDirector::OnDashContact(FLDBodyCombatState& Body, FLDCombatDashContact& Dash, int64 NowMs, FString& Error)
{
    using namespace LDCombatSkillDetail;
    if (Dash.Body!=FLDCombatBodyRef{Body.Victim,Body.Identity})
    { Error=TEXT("Dash contact belongs to another body life."); return false; }
    // D01: the dash's first enemy contact makes it ready again (once per dash).
    if (Dash.bReset && !Dash.bRefunded)
    {
        auto* Record=FindRecord(Body.Victim.EntityId);
        auto* Track=Record ? Record->Tracks.FindByPredicate([&Dash](const auto& Row)
        { return Row.Kind == Dash.Source.Kind && Row.DefinitionId == Dash.Source.DefinitionId; }) : nullptr;
        if (!Track) { Error=TEXT("A dash refresh lost its issuing cooldown track."); return false; }
        Dash.bRefunded=true;
        if (Track->LastEffectId==Dash.EffectId && Track->ReadyAtMs>NowMs)
        {
            Track->ReadyAtMs=NowMs;
            MarkViewChanged(Body.Victim.EntityId);
            LedgerCardTriggered(Body, FName(TEXT("card_bump_reset")));
            UE_LOG(LogLDCombat,Log,TEXT("LD_COMBAT_DASH_RESET t=%lld src=%s"),NowMs,*LDLog::Id(Body.Victim.EntityId));
        }
    }
    return true;
}
