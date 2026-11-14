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
#include "Framework/LDGameMode.h"
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
    // B05 (and #198 F C04 on D): a D that can hurt or push on contact is an offensive entry.
    TOptional<FLDPassiveEffectRef> Contact;
    TArray<FLDPassiveEffectRef> Pushes;
    FString Error;
    FLDCombatSourceRef Innate;
    Innate.Kind=ELDCombatSourceKind::Innate;
    Innate.DefinitionId=LDCombatSourceRules::InnateDashId();
    if (!FindTrigger(Body->AttributeProjection, ELDDemoEffectOp::DashContactDamage, Contact, Error)
        || !WorkshopOn(Body->AttributeProjection, ELDDemoEffectOp::ContactKnockback, Innate, Pushes, Error))
    {
        UE_LOG(LogLDCombat, Error, TEXT("LD_COMBAT_TRIGGER_UNRESOLVED body=%s op=dash_contact error=%s"),
            *LDLog::Id(Ref.Victim.EntityId), *Error);
        return true;
    }
    return Contact.IsSet() || !Pushes.IsEmpty();
}

void ALDCombatDirector::CaptureSkillMotion(TSharedPtr<const FLDNativeMotionBatch> Batch)
{
    const auto* Body=FindBody(Batch->Body);
    if (Core() && Core()->IsShowdown()) { FinalMotion.Add({Batch,Body ? Body->VoluntaryDash : TOptional<FLDCombatVoluntaryDash>()}); }
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
        if (Terms.FinalKnockbackTerms.IsSet())
        {
            // M18: the final wave pushes everything it hits away from the caster instead of slowing it.
            Channel.FinalKnockbackCm=Cm(Terms.FinalKnockbackTerms->DistanceCm);
            Channel.FinalKnockbackMs=Terms.FinalKnockbackTerms->DurationMs;
            Channel.FinalSlowBp=0;
            Channel.FinalSlowMs=0;
        }
        // #198 F M07: the waves become one narrower cone after the delay carrying their summed package times its multiplier;
        // the same channel rules cancel it, and its one wave is the last (it carries the final slow once).
        if (Terms.CompressCone.IsSet())
        {
            const FLDDemoEffectDefinition& Compress=Terms.CompressCone.GetValue();
            if (!LDMatchLifecycle::AddDurationMs(NowMs, Compress.DelayMs, Channel.NextPulseMs, Error)) { return false; }
            Channel.AngleDegrees=Cm(Compress.AngleDegrees);
            Channel.Remaining=1;
            const int64 Scale=int64(Cone->Count)*Compress.MultiplierBp;
            Channel.Payload.PackageScaleBp=Channel.Payload.PackageScaleBp/10000*Scale+Channel.Payload.PackageScaleBp%10000*Scale/10000;
        }
        Body.ChannelEffectId=Payload.EffectId;
        Body.ChannelDirectSequence=Action.Key.DirectSequence;
        Body.ChannelStartedAtMs=NowMs;
        Channels.Add(MoveTemp(Channel));
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
            // One trap per owner (content §4): a newer one ends the old one, armed or fusing, without its blast.
            GroundEffects.RemoveAll([&Body](const auto& Existing)
            { return Existing.bTrap && Existing.Payload.SourceEntity == Body.Victim.EntityId; });
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

bool ALDCombatDirector::AdvanceSkillContent(int64 NowMs, FString& Error)
{
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
            for (const auto& Ref : Targets)
            {
                ALDCharacter* Target=nullptr;
                if (!FindBody(Ref, &Target) || !Target) { continue; }
                const FVector Direction=(FootOf(*Target)-Origin).GetSafeNormal2D();
                if (!Direction.IsNearlyZero() && FVector::DotProduct(Channel.Direction, Direction) < CosHalfAngle) { continue; }
                auto Hit=Channel.Payload;
                Hit.HitId=FGuid::NewGuid();
                Hit.Target=Ref;
                if (Channel.Remaining == 1) { Hit.SlowBp=Channel.FinalSlowBp; Hit.SlowDurationMs=Channel.FinalSlowMs; }
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
        if (bFinalPushed)
        {
            TArray<FLDPassiveEffectRef> Finals;
            if (!WorkshopOn(Body->AttributeProjection, ELDDemoEffectOp::FinalKnockback, Channel.Payload.Source, Finals, Error)) { return false; }
            if (!Finals.IsEmpty()) { LedgerComponentTriggered(*Body, Finals[0].Source.DefinitionId); }
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
            PendingHits.Add(MoveTemp(Hit));
        }
        GroundEffects.RemoveAt(Index);
        if ((Completed.Origin==ELDCombatGroundOrigin::Impact || Completed.Origin==ELDCombatGroundOrigin::Trap
                || Completed.Origin==ELDCombatGroundOrigin::CompressedZone)
            && !TriggerAftershock(Completed.Payload.SourceBody, Completed.Payload.Source, Completed.Payload.EffectId,
                Completed.CentreCm, Completed.RadiusCm, NowMs, Error)) { return false; }
        // M15: S18's echo keeps the impact's outline and falloff; S19's is a plain blast of the trap's radius
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
    const auto* Mode=Core();
    const auto* Registry=GetWorld()->GetSubsystem<ULDMapRegistry>();
    if (!Mode || !Mode->Vision || !Registry) { Error=TEXT("Dash contacts require the registered sight world."); return false; }
    TArray<FContact> Contacts;
    for (const auto& Frame : ContactMotion)
    {
        if (!Frame.Dash) { continue; }
        const auto& Dash=*Frame.Dash;
        ALDCharacter* SourceActor=nullptr;
        const auto* Source=FindBody(Dash.Body, &SourceActor);
        if (!Source || !SourceActor || Source->bPendingDeath
            || Mode->IsContenderProtected(Source->Victim.EntityId) || Mode->IsInputLocked(Mode->GetMatchContext(),Source->Victim.EntityId)) { continue; }
        const double SourceRadius=SourceActor->GetCapsuleComponent()->GetScaledCapsuleRadius();
        const double SourceHeight=SourceActor->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        for (const auto& TargetFrame : ContactMotion)
        {
            if (TargetFrame.Batch->Body == Dash.Body || Dash.Struck.Contains(TargetFrame.Batch->Body)) { continue; }
            ALDCharacter* TargetActor=nullptr;
            const auto* Target=FindBody(TargetFrame.Batch->Body, &TargetActor);
            if (!Target || !TargetActor || Target->bPendingDeath || IsIsolated(Source->Victim.EntityId,*Target)
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
                    const auto Sight=Mode->Vision->TraceSight(Moving.At(At), Station.At(At), Registry->Revision());
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
        // B05: the holder's own added package for each real contact, once per dash per body.
        TOptional<FLDPassiveEffectRef> Held;
        if (Dash.bCanDamage && !FindTrigger(Body->AttributeProjection, ELDDemoEffectOp::DashContactDamage, Held, Error)) { return false; }
        if (Held.IsSet())
        {
            const FLDDemoEffectDefinition& Damage=Held->Effect;
            FLDCombatHit Hit;
            Hit.HitId=FGuid::NewGuid();
            Hit.EffectId=Dash.EffectId;
            Hit.SourceBody=Dash.Body;
            Hit.SourceEntity=Hit.CreditOwner=Body->Victim.EntityId;
            Hit.Source=Held->Source;
            Hit.Target=Contact.Target;
            Hit.HitClass=ELDCombatHitClass::Ability;
            Hit.BaseP2=Damage.BaseP2;
            Hit.ADRatioBp=Damage.ADRatioBp;
            Hit.APRatioBp=Damage.APRatioBp;
            Hit.SourceADP2=Body->AttributeProjection.Final.ADP2;
            Hit.SourceAPP2=Body->AttributeProjection.Final.APP2;
            Hit.DamageType=Damage.DamageType;
            Hit.bAddedDamage=Hit.bDashContactHit=true;
            // #198 F M08: the action's contact package and B05 grow together.
            Hit.PackageScaleBp=Dash.ContactScaleBp;
            if (!StampSourceTerms(*Body, Hit, Error)) { return false; }
            PendingHits.Add(MoveTemp(Hit));
            // §10: one B05 trigger per contacted body (the contact is its proc).
            LedgerComponentTriggered(*Body, Held->Source.DefinitionId);
        }
        // #198 F C04: the contacted body is pushed along the dash, once per action and target (Struck).
        if (Dash.bCanKnock)
        {
            TArray<FLDPassiveEffectRef> Knocks;
            ALDCharacter* TargetActor=nullptr;
            if (!WorkshopOn(Body->AttributeProjection, ELDDemoEffectOp::ContactKnockback, Dash.Source, Knocks, Error)) { return false; }
            if (!Knocks.IsEmpty() && FindBody(Contact.Target, &TargetActor) && TargetActor && !Dash.Direction.IsNearlyZero())
            {
                const FLDDemoEffectDefinition& Push=Knocks[0].Effect;
                FLDCombatHit Hit;
                Hit.HitId=FGuid::NewGuid();
                Hit.EffectId=Dash.EffectId;
                Hit.SourceBody=Dash.Body;
                Hit.SourceEntity=Hit.CreditOwner=Body->Victim.EntityId;
                Hit.Source=Knocks[0].Source;
                Hit.Target=Contact.Target;
                Hit.HitClass=ELDCombatHitClass::Ability;
                Hit.KnockbackCm=Cm(Push.DistanceCm);
                Hit.KnockbackMs=Push.DurationMs;
                Hit.bKnockbackFromAnchor=true;
                Hit.KnockbackAnchorCm=FootOf(*TargetActor)-Dash.Direction.GetSafeNormal2D()*100.;
                Hit.KnockbackSource=Knocks[0].Source;
                PendingHits.Add(MoveTemp(Hit));
            }
        }
        // C13: the hosted dash's first real contact cuts the holder's other actives (once per action).
        if (!HasteOtherActives(*Body, Dash.Source, Dash.EffectId, NowMs, Error)) { return false; }
        if (!OnDashContact(*Body, Dash, NowMs, Error)) { return false; }
        UE_LOG(LogLDCombat,Log,TEXT("LD_DASH_CONTACT t=%lld src=%s target=%s effect=%s native=%d added=%d"),
            NowMs,*LDLog::Id(Body->Victim.EntityId),*LDLog::Id(Contact.Target.Victim.EntityId),
            *LDLog::Id(Dash.EffectId),bNativeHit,Dash.bCanDamage);
    }
    ContactMotion.Reset();
    return true;
}

bool ALDCombatDirector::OnDashContact(FLDBodyCombatState& Body, FLDCombatDashContact& Dash, int64 NowMs, FString& Error)
{
    using namespace LDCombatSkillDetail;
    if (Dash.Body!=FLDCombatBodyRef{Body.Victim,Body.Identity})
    { Error=TEXT("Dash contact belongs to another body life."); return false; }
    if (Dash.bCanRefund && !Dash.bRefunded && NowMs >= Body.ContactRefundReadyAtMs)
    {
        // #198 F C03: the refund installed on this dash.
        TArray<FLDPassiveEffectRef> Refunds;
        if (!WorkshopOn(Body.AttributeProjection, ELDDemoEffectOp::DashContactRefund, Dash.Source, Refunds, Error)) { return false; }
        TOptional<FLDPassiveEffectRef> Held;
        if (!Refunds.IsEmpty()) { Held=Refunds[0]; }
        if (Held.IsSet())
        {
            const FLDDemoEffectDefinition* Refund=&Held->Effect;
            auto* Record=FindRecord(Body.Victim.EntityId);
            auto* Track=Record ? Record->Tracks.FindByPredicate([&Dash](const auto& Row)
            { return Row.Kind == Dash.Source.Kind && Row.DefinitionId == Dash.Source.DefinitionId; }) : nullptr;
            if (!Track) { Error=TEXT("A dash contact refund lost its issuing cooldown track."); return false; }
            if (Track->LastEffectId!=Dash.EffectId)
            {
                Dash.bRefunded=true;
                UE_LOG(LogLDCombat,Verbose,TEXT("LD_DASH_REFUND_IGNORED src=%s reason=superseded_cast"),
                    *LDLog::Id(Body.Victim.EntityId));
            }
            else
            {
                int64 Ready=0;
                if (!LDMatchLifecycle::AddDurationMs(NowMs, Refund->IntervalMs, Ready, Error)) { return false; }
                Dash.bRefunded=true;
                Track->ReadyAtMs=LDCombatSourceRules::ReduceReadyAt(Track->ReadyAtMs, NowMs, Refund->AmountMs);
                Body.ContactRefundReadyAtMs=Ready;
                GrantMana(Body, FMath::Min(Share(Dash.PaidManaP2, Refund->SecondaryRatioBp), Refund->LimitP2));
                MarkViewChanged(Body.Victim.EntityId);
                LedgerComponentTriggered(Body, Held->Source.DefinitionId);
            }
        }
    }
    return true;
}
