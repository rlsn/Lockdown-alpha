#include "Combat/LDCombatDirector.h"
#include "Combat/LDCombatMechanics.h"
#include "Combat/LDCombatSourceRules.h"
#include "Combat/LDDashContactRules.h"
#include "Combat/LDTacticalRules.h"
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDSkillGrowth.h"
#include "Framework/LDGameMode.h"
#include "Core/LDLog.h"

namespace LDCombatFinalDetail
{
    double Cm(const FLDDemoRational& Value)
    { return double(Value.Numerator)/double(Value.Denominator); }

    bool MainBasic(const FLDCombatHit& Hit)
    { return Hit.Source.Kind==ELDCombatSourceKind::Basic && Hit.HitClass==ELDCombatHitClass::BasicPrimary; }

    bool Direct(const FLDCombatHit& Hit)
    { return MainBasic(Hit) || IsNativeDirectHit(Hit); }

    bool ControlAlive(const FLDBodyCombatState& Body, const FLDFinalControlExit& Watch, int64 NowMs)
    {
        const TArray<FLDCombatRoot>* Instances=nullptr;
        switch (Watch.Kind)
        {
        case ELDControlKind::Root: Instances=&Body.Roots; break;
        case ELDControlKind::Stun: Instances=&Body.Stuns; break;
        case ELDControlKind::Disarm: Instances=&Body.Disarms; break;
        case ELDControlKind::Silence: Instances=&Body.Silences; break;
        case ELDControlKind::Taunt:
            return Body.Taunt.IsSet() && Body.Taunt->EffectId==Watch.EffectId && Body.Taunt->ExpiresAtMs>NowMs;
        default: return false;
        }

        return Instances->ContainsByPredicate([&Watch,NowMs](const FLDCombatRoot& Root)
        { return Root.EffectId==Watch.EffectId && Root.ExpiresAtMs>NowMs; });
    }

    bool InsideBand(const FVector& Point,const FLDFinalTrafficTrail& Trail,double Radius,double At,bool& Out,FString& Error)
    {
        Out=false;
        for(const FLDFinalTrafficSegment& Segment:Trail.Segments)
        {
            TOptional<LDDashContactRules::FGroundBandShape> Shape;
            if(!LDDashContactRules::SampleGroundBand(Segment.Ground,At,Shape,Error)){return false;}
            if(Shape.IsSet()&&FMath::PointDistToSegmentSquared(FVector(Point.X,Point.Y,0),
                FVector(Shape->FromCm.X,Shape->FromCm.Y,0),FVector(Shape->ToCm.X,Shape->ToCm.Y,0))<=Radius*Radius){Out=true;}
        }
        return true;
    }

    bool CrossesBand(const FVector& From,const FVector& To,double FromSeconds,double ToSeconds,
        const FLDFinalTrafficTrail& Trail,double Radius,bool bWasInside,bool& bEntered,FString& Error)
    {
        TArray<LDDashContactRules::FContactInterval> Contacts;
        for(const FLDFinalTrafficSegment& Segment:Trail.Segments)
        {
            TArray<LDDashContactRules::FContactInterval> Part;
            if(!LDDashContactRules::GroundBandContacts(From,To,FromSeconds,ToSeconds,Segment.Ground,Radius,Part,Error)){return false;}
            Contacts.Append(Part);
        }
        Contacts.Sort([](const auto& A,const auto& B){return A.FromSeconds<B.FromSeconds;});
        double Through=-1;
        for(const auto& Contact:Contacts)
        {
            if(Contact.FromSeconds>Through)
            {
                if(!bWasInside||Contact.FromSeconds>FromSeconds){bEntered=true;}
                Through=Contact.ToSeconds;
            }
            else{Through=FMath::Max(Through,Contact.ToSeconds);}
        }
        return true;
    }
}

bool ALDCombatDirector::ReadDirectRequestCursor(const FGuid& Entity,uint64& Through,FString& Error) const
{
    const FLDCombatEntityRecord* Record=Records.Find(Entity);
    if(!Record){Error=TEXT("Reconnect direct cursor has no current combat record.");return false;}
    Through=FMath::Max(Record->DirectHighWater,Record->CancelledThrough);
    if(Through==MAX_uint64){Error=TEXT("Reconnect direct cursor exhausted its sequence.");return false;}
    Error.Reset();return true;
}

bool ALDCombatDirector::FinalModOf(const FGuid& Entity, const FLDDemoFinalModDefinition*& Out, FString& Error) const
{
    Out=nullptr;
    const ALDGameMode* Mode=Core();
    const FLDCombatEntityRecord* Record=FindRecord(Entity);
    if (!Mode || !Mode->IsShowdown() || !Record || Record->Build.FinalMod.IsEmpty()) { return true; }
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!Content) { Error=TEXT("A selected final rule requires the approved content."); return false; }
    return Content->FindFinalMod(Record->Build.FinalMod.DefinitionId.ToString(),Out,Error) && Out;
}

bool ALDCombatDirector::FinalModTargetsSource(const FGuid& Entity, const FLDCombatSourceRef& Source) const
{
    const FLDCombatEntityRecord* Record=FindRecord(Entity);
    if (!Record) { return false; }
    const FLDFinalModSelection& Selected=Record->Build.FinalMod;
    switch (Selected.TargetKind)
    {
    case ELDComponentTargetKind::Active:
        return Source.Kind==ELDCombatSourceKind::Learned && Source.DefinitionId==Selected.TargetId;
    case ELDComponentTargetKind::Component:
        return Source.Kind==ELDCombatSourceKind::Component && Source.DefinitionId==Selected.TargetId;
    case ELDComponentTargetKind::Dash:
        return Source.Kind==ELDCombatSourceKind::Innate && Source.DefinitionId==LDCombatSourceRules::InnateDashId();
    default: return false;
    }
}

bool ALDCombatDirector::NotifyFinalPresence(const FLDCaptureEvent& Event, FString& Error)
{
    using namespace LDCombatFinalDetail;
    const FLDDemoFinalModDefinition* Card=nullptr;
    if (!FinalModOf(Event.Body.Victim.EntityId,Card,Error)) { return false; }
    FLDBodyCombatState* Body=FindBody(Event.Body);
    if (!Card || !Body || Body->bPendingDeath) { return true; }
    FLDFinalModLifeState& State=Body->FinalMod;
    if (Event.Kind==ELDCaptureEventKind::Joined || Event.Kind==ELDCaptureEventKind::Respawned || !State.bPresenceInitialized)
    {
        State.bPresenceInitialized=true;
        State.bInside=Event.bInside;
        State.OutsideSinceSeconds=Event.Time.Seconds;
        return true;
    }
    if (Event.Kind!=ELDCaptureEventKind::Presence || Event.bInside==State.bInside) { return true; }
    State.bInside=Event.bInside;
    if (!Event.bInside) { State.OutsideSinceSeconds=Event.Time.Seconds; return true; }
    bool bArm=false;
    if (Card->GetNativeId()==ELDDemoFinalModId::FinalBreach)
    {
        const int64 OutsideMs=State.bBreachUsed ? Card->RearmOutsideMs : Card->OutsideMs;
        bArm=Event.Time.Seconds-State.OutsideSinceSeconds>=double(OutsideMs)/1000.;
    }
    else if (Card->GetNativeId()==ELDDemoFinalModId::FinalComeback)
    {
        const ALDGameMode* Mode=Core();
        const FGuid FlagOwner=Mode->CaptureReducer.GetGraffitiOwner();
        bool bOwnerAlive=false;
        for (const auto& Pair : Bodies)
        { bOwnerAlive|=Pair.Value.Victim.EntityId==FlagOwner && !Pair.Value.bPendingDeath && Pair.Value.Resources.HpP2>0; }
        bArm=FlagOwner.IsValid() && FlagOwner!=Body->Victim.EntityId && bOwnerAlive;
    }
    if (bArm)
    {
        const int64 EnterMs=FMath::CeilToInt64(Event.Time.Seconds*1000.);
        if (!LDMatchLifecycle::AddDurationMs(EnterMs,Card->WindowMs,State.ArmedUntilMs,Error)) { return false; }
        UE_LOG(LogLDCombat,Log,TEXT("LD_FINAL_ARM t=%lld holder=%s card=%s until=%lld"),
            EnterMs,*LDLog::Id(Body->Victim.EntityId),*Card->Id,State.ArmedUntilMs);
    }
    return true;
}

bool ALDCombatDirector::FinalCooldownCost(const FLDCombatEntityRecord& Record, const FLDCombatSourceRef& Source,
    int64& InOutCooldownMs, FString& Error) const
{
    const FLDDemoFinalModDefinition* Card=nullptr;
    if (!FinalModOf(Record.Entity,Card,Error)) { return false; }
    if (!Card) { return true; }
    const bool bBreach=Card->GetNativeId()==ELDDemoFinalModId::FinalBreach && Source.Kind==ELDCombatSourceKind::Innate;
    const bool bTraffic=Card->GetNativeId()==ELDDemoFinalModId::FinalTraffic && FinalModTargetsSource(Record.Entity,Source);
    return (!bBreach && !bTraffic)
        || LDMatchLifecycle::AddDurationMs(InOutCooldownMs,Card->CooldownCostMs,InOutCooldownMs,Error);
}

bool ALDCombatDirector::RewriteFinalHit(FLDCombatHit& Hit, bool bProtected, int64 NowMs, FString& Error)
{
    using namespace LDCombatFinalDetail;
    if (bProtected || Hit.bFinalModDerived || Hit.SourceEntity==Hit.Target.Victim.EntityId) { return true; }
    const FLDDemoFinalModDefinition* Card=nullptr;
    if (!FinalModOf(Hit.SourceEntity,Card,Error)) { return false; }
    FLDBodyCombatState* Holder=FindBody(Hit.SourceBody);
    FLDCombatEntityRecord* Record=FindRecord(Hit.SourceEntity);
    if (!Card || !Holder || Holder->bPendingDeath || !Record) { return true; }
    const bool bLandlord=Card->GetNativeId()==ELDDemoFinalModId::FinalLandlord && MainBasic(Hit)
        && Core()->CaptureReducer.GetGraffitiOwner()==Hit.SourceEntity;
    const bool bComeback=Card->GetNativeId()==ELDDemoFinalModId::FinalComeback && Direct(Hit)
        && Holder->FinalMod.ArmedUntilMs>NowMs && Record->FinalModReadyAtMs<=NowMs;
    if (bLandlord || bComeback)
    {
        if (Hit.PackageScaleBp<0 || Hit.PackageScaleBp>MAX_int64/Card->DamageScaleBp)
        { Error=TEXT("Final primary damage scale overflow."); return false; }
        Hit.PackageScaleBp=Hit.PackageScaleBp*Card->DamageScaleBp/10000;
    }
    return true;
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
        Hit.bDirectAbilityDamage=Zone.bDamage && !Zone.bFinalModDerived;
        Hit.bFinalModDerived=Zone.bFinalModDerived;
        Hit.ControlAmpBp=Zone.ControlAmpBp;
        Hit.IdleReadyAmpBp=Zone.IdleReadyAmpBp;
        Hit.PackageScaleBp=Zone.PackageScaleBp;
        Hit.bApplyWet=Zone.bWet;
        Hit.BurnStacks=Zone.PulseBurnStacks;
        Hit.ElementLevel=Zone.ElementLevel;
        Hit.ElementAPP2=Zone.ElementAPP2;
        Hit.SlowBp=Zone.PulseSlowBp;
        Hit.SlowDurationMs=Zone.PulseSlowMs;
        Hit.CastLedger=Zone.CastLedger;
    }
    return true;
}

bool ALDCombatDirector::SpawnFinalZone(FLDBodyCombatState& Holder, const FLDDemoFinalModDefinition& Card,
    const FVector& Centre, bool bTether, int64 NowMs, FString& Error)
{
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!Content) { Error=TEXT("A final zone requires current content."); return false; }
    FLDCombatZone Zone;
    Zone.EffectId=Zone.ObservedKey=FGuid::NewGuid();
    Zone.SourceBody={Holder.Victim,Holder.Identity};
    Zone.SourceEntity=Zone.CreditOwner=Holder.Victim.EntityId;
    Zone.Source.Kind=ELDCombatSourceKind::FinalMod;
    Zone.Source.DefinitionId=FName(*Card.Id);
    Zone.CentreCm=Centre;
    Zone.RadiusCm=LDCombatFinalDetail::Cm(Card.RadiusCm);
    Zone.StartMs=NowMs;
    Zone.DelayMs=Card.DelayMs;
    Zone.IntervalMs=Card.PulseIntervalMs;
    const int64 Duration=Card.DurationMs>0 ? Card.DurationMs : Card.DelayMs;
    if (!LDMatchLifecycle::AddDurationMs(NowMs,Duration,Zone.EndMs,Error)) { return false; }
    Zone.Count=Card.PulseIntervalMs>0 ? int32((Duration-1)/Card.PulseIntervalMs+1) : 1;
    Zone.bDamage=Zone.bFinalModDerived=true;
    Zone.bTether=bTether;
    const auto& Parameters=Content->Get().Parameters;
    const FLDCombatEntityRecord* Record=FindRecord(Holder.Victim.EntityId);
    if (!Record) { Error=TEXT("A final zone lost its persistent build."); return false; }
    const int64 Growth=LDSkillGrowth::FactorBp(Parameters.SkillGrowthPerLevelBp,Parameters.SkillGrowthPerLevelAfter10Bp,Record->Build.Level);
    if (!LDSkillGrowth::GrowP2(Card.BaseP2,Growth,Zone.BaseP2))
    { Error=TEXT("Final damage growth overflow."); return false; }
    Zone.ADRatioBp=Card.ADRatioBp;
    Zone.APRatioBp=Card.APRatioBp;
    Zone.SourceADP2=Holder.AttributeProjection.Final.ADP2;
    Zone.SourceAPP2=Holder.AttributeProjection.Final.APP2;
    Zone.ControlAmpBp=Holder.AttributeProjection.ControlAmp.IsSet() ? Holder.AttributeProjection.ControlAmp->RatioBp : 0;
    Zone.PulseSlowBp=Card.SlowBp;
    Zone.PulseSlowMs=Card.SlowMs;
    if (bTether)
    {
        const FLDCombatZone* Previous=Zones.FindByPredicate([&Zone](const FLDCombatZone& Old)
        {return Old.SourceEntity==Zone.SourceEntity&&Old.Source==Zone.Source&&Old.EndMs>Zone.StartMs;});
        if(Previous)
        {
            int64 Next=0;
            if(Previous->IntervalMs<=0||Previous->Pulsed>MAX_int64/Previous->IntervalMs
                ||!LDMatchLifecycle::AddDurationMs(Previous->StartMs,Previous->DelayMs,Next,Error)
                ||!LDMatchLifecycle::AddDurationMs(Next,Previous->IntervalMs*Previous->Pulsed,Next,Error)){return false;}
            if(Next<=NowMs)
            {
                const int64 Steps=(NowMs-Next)/Previous->IntervalMs+1;
                if(Steps>MAX_int64/Previous->IntervalMs
                    ||!LDMatchLifecycle::AddDurationMs(Next,Steps*Previous->IntervalMs,Next,Error)){return false;}
            }
            Zone.DelayMs=Next-NowMs;
            Zone.Count=Zone.DelayMs<Duration?int32((Duration-Zone.DelayMs-1)/Zone.IntervalMs+1):0;
        }
        Zones.RemoveAll([&Zone](const FLDCombatZone& Old)
        { return Old.SourceEntity==Zone.SourceEntity && Old.Source==Zone.Source; });
        Holder.FinalMod.VoiceUntilMs=Zone.EndMs;
        Holder.TimedBonuses.RemoveAll([&Zone](const FLDCombatTimedBonus& Old) { return Old.SourceDefinition==Zone.Source.DefinitionId; });
        FLDCombatTimedBonus Cost;
        Cost.EffectId=Zone.EffectId;
        Cost.SourceDefinition=Zone.Source.DefinitionId;
        Cost.ExpiresAtMs=Zone.EndMs;
        Cost.Stats.Add({ELDDemoBonusStat::MoveSpeedBonusRatio,-Card.MoveSpeedCostBp});
        LDTactical::ApplyTimedBonus(Holder.TimedBonuses,Cost);
    }
    if (Zone.DelayMs==0)
    {
        if (!EmitZonePulse(Zone,Error)) { return false; }
        Zone.Pulsed=1;
    }
    Zones.Add(MoveTemp(Zone));
    UE_LOG(LogLDCombat,Log,TEXT("LD_FINAL_ZONE t=%lld holder=%s card=%s centre=%s"),
        NowMs,*LDLog::Id(Holder.Victim.EntityId),*Card.Id,*Centre.ToString());
    return true;
}

bool ALDCombatDirector::MakeFinalPush(const FLDCombatBodyRef& Holder, const FLDCombatBodyRef& Target,
    FName CardId, const FVector& Anchor, double DistanceCm, FLDCombatHit& Out, FString& Error) const
{
    const FLDDemoContentSnapshot* Content=Catalog();
    const FLDDemoEffectDefinition* Dash=Content ? Content->Get().Character.Dash.Values.Effects.FindByPredicate(
        [](const FLDDemoEffectDefinition& Effect) { return Effect.GetOp()==ELDDemoEffectOp::Dash; }) : nullptr;
    if (!Dash || Dash->DurationMs<=0 || Dash->DistanceCm.Numerator<=0 || !FMath::IsFinite(DistanceCm) || DistanceCm<=0)
    { Error=TEXT("A final push requires the approved native dash displacement rate."); return false; }
    const double Duration=DistanceCm*double(Dash->DurationMs)/LDCombatFinalDetail::Cm(Dash->DistanceCm);
    if (!FMath::IsFinite(Duration) || Duration<=0 || Duration>=double(MAX_int64))
    { Error=TEXT("Final push duration overflow."); return false; }
    Out={};
    Out.HitId=Out.EffectId=FGuid::NewGuid();
    Out.Target=Target;
    Out.SourceBody=Holder;
    Out.SourceEntity=Out.CreditOwner=Holder.Victim.EntityId;
    Out.Source.Kind=ELDCombatSourceKind::FinalMod;
    Out.Source.DefinitionId=CardId;
    Out.HitClass=ELDCombatHitClass::Ability;
    Out.bFinalModDerived=true;
    Out.KnockbackCm=DistanceCm;
    Out.KnockbackMs=FMath::Max<int64>(1,FMath::CeilToInt64(Duration));
    Out.KnockbackSource=Out.Source;
    Out.bKnockbackFromAnchor=true;
    Out.KnockbackAnchorCm=Anchor;
    return true;
}

bool ALDCombatDirector::OnFinalHit(const FLDCombatHit& Hit, FLDBodyCombatState& Target, FLDBodyCombatState* Source,
    bool bEffective, bool bControlled, int64 NowMs, FString& Error)
{
    using namespace LDCombatFinalDetail;
    if ((!bEffective && !bControlled) || !Source || Source->bPendingDeath || Hit.bFinalModDerived
        || Hit.Source.Kind==ELDCombatSourceKind::FinalMod || Hit.SourceEntity==Target.Victim.EntityId || Hit.bSilent)
    { return true; }
    const FLDDemoFinalModDefinition* Card=nullptr;
    if (!FinalModOf(Hit.SourceEntity,Card,Error)) { return false; }
    FLDCombatEntityRecord* Record=FindRecord(Hit.SourceEntity);
    const ALDGameMode* Mode=Core();
    if (!Card || !Record || !Mode) { return true; }
    ALDCharacter* HolderActor=nullptr;
    FindBody(Hit.SourceBody,&HolderActor);
    ALDCharacter* TargetActor=nullptr;
    FindBody(Hit.Target,&TargetActor);
    if (!HolderActor || !TargetActor) { return true; }
    switch (Card->GetNativeId())
    {
    case ELDDemoFinalModId::FinalLandlord:
        if (bEffective && MainBasic(Hit) && Record->FinalModReadyAtMs<=NowMs
            && Mode->CaptureReducer.GetGraffitiOwner()==Hit.SourceEntity && Mode->CaptureReducer.IsCommittedInside(Hit.SourceBody))
        {
            if (!LDMatchLifecycle::AddDurationMs(NowMs,Card->IntervalMs,Record->FinalModReadyAtMs,Error)
                || !SpawnFinalZone(*Source,*Card,FVector(Mode->CaptureCircle.CenterCm,FootOf(*HolderActor).Z),false,NowMs,Error))
            { return false; }
        }
        break;
    case ELDDemoFinalModId::FinalBreach:
        if (bEffective && MainBasic(Hit) && Source->FinalMod.ArmedUntilMs>NowMs)
        {
            Source->FinalMod.ArmedUntilMs=0;
            Source->FinalMod.bBreachUsed=true;
            if (Target.Resources.HpP2>0)
            {
                FLDCombatHit Push;
                if (!MakeFinalPush(Hit.SourceBody,Hit.Target,FName(*Card->Id),
                    FVector(Mode->CaptureCircle.CenterCm,FootOf(*TargetActor).Z),Cm(Card->DistanceCm),Push,Error)) { return false; }
                PendingHits.Add(MoveTemp(Push));
            }
        }
        break;
    case ELDDemoFinalModId::FinalComeback:
        if (bEffective && Direct(Hit) && Source->FinalMod.ArmedUntilMs>NowMs && Record->FinalModReadyAtMs<=NowMs)
        {
            Source->FinalMod.ArmedUntilMs=0;
            if (!LDMatchLifecycle::AddDurationMs(NowMs,Card->IntervalMs,Record->FinalModReadyAtMs,Error)
                || !SpawnFinalZone(*Source,*Card,FootOf(*TargetActor),false,NowMs,Error)) { return false; }
        }
        break;
    case ELDDemoFinalModId::FinalOpenMic:
    {
        // Status payments aggregate older contacts; they are not a new independent hit.
        if (Hit.bLedgerCashOut || Hit.bBurnTick || Hit.bStatusSettle || Hit.bDebtPayment) { break; }
        TArray<FLDFinalModHitMark>& Hits=Source->FinalMod.Hits;
        Hits.RemoveAll([NowMs,Card](const FLDFinalModHitMark& Mark) { return Mark.AtMs<NowMs-Card->WindowMs; });
        if (!Hit.HitId.IsValid() || Hits.ContainsByPredicate([&Hit](const FLDFinalModHitMark& Mark) { return Mark.HitId==Hit.HitId; }))
        { break; }
        Hits.Add({Hit.HitId,NowMs,false});
        if(Hits.FilterByPredicate([](const FLDFinalModHitMark& Mark){return !Mark.bConsumed;}).Num()>=2)
        {
            for(auto& Mark:Hits){Mark.bConsumed=true;}
            if(!SpawnFinalZone(*Source,*Card,FootOf(*HolderActor),true,NowMs,Error)){return false;}
        }
        break;
    }
    default: break;
    }
    return true;
}

bool ALDCombatDirector::FinalControlDuration(const FLDControlApplication& App, int64& DurationMs, FString& Error) const
{
    const FLDDemoFinalModDefinition* Card=nullptr;
    if (!FinalModOf(App.SourceEntity,Card,Error)) { return false; }
    if (!Card || Card->GetNativeId()!=ELDDemoFinalModId::FinalForcedExit || !FinalModTargetsSource(App.SourceEntity,App.Source))
    { return true; }
    if (DurationMs<0 || DurationMs>MAX_int64/Card->ControlScaleBp)
    { Error=TEXT("Final hard-control duration overflow."); return false; }
    DurationMs=FMath::Max<int64>(1,DurationMs*Card->ControlScaleBp/10000);
    return true;
}

bool ALDCombatDirector::WatchFinalControlExit(const FLDBodyCombatState& Target, const FLDControlApplication& App,
    int64 ExpiresAtMs, FString& Error)
{
    const FLDDemoFinalModDefinition* Card=nullptr;
    if (!FinalModOf(App.SourceEntity,Card,Error)) { return false; }
    if (!Card || Card->GetNativeId()!=ELDDemoFinalModId::FinalForcedExit || !FinalModTargetsSource(App.SourceEntity,App.Source)
        || App.SourceEntity==Target.Victim.EntityId || !Core()->CaptureReducer.IsCommittedInside({Target.Victim,Target.Identity}))
    { return true; }
    FLDFinalControlExit Watch;
    Watch.SourceBody=App.SourceBody;
    Watch.Target={Target.Victim,Target.Identity};
    Watch.EffectId=App.EffectId;
    Watch.Source=App.Source;
    Watch.Kind=App.Kind;
    Watch.ExpectedEndMs=ExpiresAtMs;
    FinalControlExits.Add(MoveTemp(Watch));
    return true;
}

bool ALDCombatDirector::SplitFinalZone(FLDCombatZone& Zone, const FVector& ReleaseDirection, int64 NowMs, bool& bHandled,
    FString& Error)
{
    using namespace LDCombatFinalDetail;
    bHandled=false;
    const FLDDemoFinalModDefinition* Card=nullptr;
    if (!FinalModOf(Zone.SourceEntity,Card,Error)) { return false; }
    if (!Card || Card->GetNativeId()!=ELDDemoFinalModId::FinalSplit || !FinalModTargetsSource(Zone.SourceEntity,Zone.Source))
    { return true; }
    if (Zone.bTether || Zone.EndMs-Zone.StartMs<3000 || Zone.EndMs-Zone.StartMs<=Card->DurationCostMs)
    { Error=TEXT("A locked split binding is no longer a placed zone of at least three seconds."); return false; }
    if (Zone.BaseP2>MAX_int64/Card->DamageScaleBp)
    { Error=TEXT("Split fixed damage overflow."); return false; }
    Zone.BaseP2=Zone.BaseP2*Card->DamageScaleBp/10000;
    Zone.RadiusCm*=double(Card->RadiusScaleBp)/10000.;
    Zone.EndMs-=Card->DurationCostMs;
    if (Zone.IntervalMs>0)
    {
        const int64 Remaining=Zone.EndMs-Zone.StartMs-Zone.DelayMs;
        Zone.Count=int32(FMath::Min<int64>(Zone.Count,Remaining>0 ? (Remaining-1)/Zone.IntervalMs+1 : 0));
    }
    TArray<FGuid> Replaced;
    Zones.RemoveAll([&Zone,&Replaced](const FLDCombatZone& Old)
    {
        const bool bSame=Old.bFinalSplit && Old.SourceEntity==Zone.SourceEntity && Old.Source==Zone.Source;
        if (bSame) { Replaced.AddUnique(Old.EffectId); }
        return bSame;
    });
    for (auto& Pair : Bodies)
    { Pair.Value.TimedBonuses.RemoveAll([&Replaced](const FLDCombatTimedBonus& Bonus) { return Replaced.Contains(Bonus.EffectId); }); }
    Zone.bFinalSplit=true;
    FVector Direction=ReleaseDirection.GetSafeNormal2D();
    if (Direction.IsNearlyZero())
    {
        ALDCharacter* Actor=nullptr;
        FindBody(Zone.SourceBody,&Actor);
        if (!Actor) { Error=TEXT("A split release lost its current source body."); return false; }
        Direction=Actor->GetActorForwardVector().GetSafeNormal2D();
    }
    const FVector Offset=FVector(-Direction.Y,Direction.X,0)*Cm(Card->OffsetCm);
    FLDCombatZone Other=Zone;
    Zone.ObservedKey=FGuid::NewGuid();
    Other.ObservedKey=FGuid::NewGuid();
    Zone.CentreCm-=Offset;
    Other.CentreCm+=Offset;
    Zones.Add(Zone);
    Zones.Add(MoveTemp(Other));
    bHandled=true;
    UE_LOG(LogLDCombat,Log,TEXT("LD_FINAL_SPLIT t=%lld holder=%s host=%s"),NowMs,*LDLog::Id(Zone.SourceEntity),
        *Zone.Source.DefinitionId.ToString());
    return true;
}

bool ALDCombatDirector::ResolveFinalBowlingImpact(const FLDCombatKnockbackImpact& Impact, const ALDCharacter* Blocker,
    const FVector& Direction, int64 NowMs, FString& Error)
{
    if (!Impact.bFinalBowling || !Blocker) { return true; }
    const FLDBodyCombatState* Target=StateFor(Blocker);
    const FLDBodyCombatState* Holder=FindBody(Impact.Holder);
    if (!Target || !Holder || Holder->bPendingDeath || Target->bPendingDeath || Target->Victim.EntityId==Holder->Victim.EntityId
        || IsIsolated(Holder->Victim.EntityId,*Target) || Core()->IsContenderProtected(Target->Victim.EntityId))
    { return true; }
    const FVector Along=Direction.IsNearlyZero() ? Impact.Direction : Direction.GetSafeNormal2D();
    if (Along.IsNearlyZero()) { Error=TEXT("A real bowling body impact lost its native direction."); return false; }
    FLDCombatHit Hit;
    if (!MakeFinalPush(Impact.Holder,{Target->Victim,Target->Identity},Impact.FinalCardId,
        FootOf(*Blocker)-Along,Impact.FinalDistanceCm,Hit,Error)) { return false; }
    Hit.BaseP2=Impact.FinalBaseP2;
    Hit.APRatioBp=Impact.FinalAPRatioBp;
    Hit.SourceAPP2=Impact.HolderAPP2;
    Hit.DamageType=ELDDemoDamageType::Magical;
    Hit.KnockbackSource=Impact.PushSource;
    PendingHits.Add(MoveTemp(Hit));
    UE_LOG(LogLDCombat,Log,TEXT("LD_FINAL_BOWLING t=%lld holder=%s target=%s"),NowMs,
        *LDLog::Id(Holder->Victim.EntityId),*LDLog::Id(Target->Victim.EntityId));
    return true;
}

bool ALDCombatDirector::AdvanceFinalMods(int64 NowMs, FString& Error)
{
    using namespace LDCombatFinalDetail;
    const ALDGameMode* Mode=Core();
    if (!Mode || !Mode->IsShowdown()) { FinalMotion.Reset(); return true; }
    for (int32 Index=FinalControlExits.Num()-1; Index>=0; --Index)
    {
        FLDFinalControlExit& Watch=FinalControlExits[Index];
        const FLDBodyCombatState* Target=FindBody(Watch.Target);
        if (!Target || Target->bPendingDeath) { FinalControlExits.RemoveAt(Index); continue; }
        const FLDDemoFinalModDefinition* Card=nullptr;
        if (!FinalModOf(Watch.SourceBody.Victim.EntityId,Card,Error)) { return false; }
        if (!Card) { FinalControlExits.RemoveAt(Index); continue; }
        if (Watch.DueAtMs==0)
        {
            if (ControlAlive(*Target,Watch,NowMs)) { continue; }
            if (!LDMatchLifecycle::AddDurationMs(FMath::Min(NowMs,Watch.ExpectedEndMs),Card->DelayMs,Watch.DueAtMs,Error))
            { return false; }
        }
        if (Watch.DueAtMs>NowMs) { continue; }
        const FLDFinalControlExit Taken=Watch;
        FinalControlExits.RemoveAt(Index);
        FLDCombatEntityRecord* SourceRecord=FindRecord(Taken.SourceBody.Victim.EntityId);
        if (!SourceRecord || !Mode->CaptureReducer.IsCommittedInside(Taken.Target)
            || Mode->IsContenderProtected(Taken.Target.Victim.EntityId)) { continue; }
        FLDFinalModTargetCooldown* Cooldown=SourceRecord->FinalModTargetCooldowns.FindByPredicate(
            [&Taken](const FLDFinalModTargetCooldown& Entry) { return Entry.Target==Taken.Target.Victim.EntityId; });
        if (Cooldown && Cooldown->ReadyAtMs>NowMs) { continue; }
        ALDCharacter* Actor=nullptr;
        FindBody(Taken.Target,&Actor);
        if (!Actor) { continue; }
        FLDCombatHit Push;
        if (!MakeFinalPush(Taken.SourceBody,Taken.Target,FName(*Card->Id),
            FVector(Mode->CaptureCircle.CenterCm,FootOf(*Actor).Z),Cm(Card->DistanceCm),Push,Error)) { return false; }
        if (!Cooldown) { Cooldown=&SourceRecord->FinalModTargetCooldowns.Add_GetRef({Taken.Target.Victim.EntityId,0}); }
        if (!LDMatchLifecycle::AddDurationMs(NowMs,Card->IntervalMs,Cooldown->ReadyAtMs,Error)) { return false; }
        PendingHits.Add(MoveTemp(Push));
        UE_LOG(LogLDCombat,Log,TEXT("LD_FINAL_EXIT t=%lld holder=%s target=%s"),NowMs,
            *LDLog::Id(SourceRecord->Entity),*LDLog::Id(Taken.Target.Victim.EntityId));
    }
    const double NowSeconds=double(NowMs)/1000.;
    for (const FLDFinalMotionFrame& Frame : FinalMotion)
    {
        if (!Frame.Dash.IsSet()) { continue; }
        const FLDDemoFinalModDefinition* Card=nullptr;
        if (!FinalModOf(Frame.Batch->Body.Victim.EntityId,Card,Error)) { return false; }
        if (!Card || Card->GetNativeId()!=ELDDemoFinalModId::FinalTraffic
            || !FinalModTargetsSource(Frame.Batch->Body.Victim.EntityId,Frame.Dash->Source)) { continue; }
        FLDFinalTrafficTrail* Trail=FinalTraffic.FindByPredicate([&Frame](const FLDFinalTrafficTrail& Candidate)
        { return Candidate.EffectId==Frame.Dash->EffectId; });
        bool bGrounded=Frame.Batch->InitialSupport.Grounded;
        for (const FLDNativeMotionRecord& Motion : Frame.Batch->Records)
        {
            const bool bWalked=bGrounded&&Motion.Support.Grounded;
            bGrounded=Motion.Support.Grounded;
            if (Motion.Kind!=ELDNativeMotionRecordKind::Movement || !Motion.RootMotion || !bWalked
                || Motion.EndServerSeconds>double(Frame.Dash->EndMs)/1000. || Motion.FromFootCm==Motion.ToFootCm
                || Motion.EndServerSeconds<=Motion.StartServerSeconds)
            { continue; }
            if (!Trail)
            {
                FinalTraffic.RemoveAll([&Frame](const FLDFinalTrafficTrail& Old) { return Old.SourceBody.Victim.EntityId==Frame.Batch->Body.Victim.EntityId; });
                Trail=&FinalTraffic.AddDefaulted_GetRef();
                Trail->EffectId=Frame.Dash->EffectId;
                Trail->ObservedKey=FGuid::NewGuid();
                Trail->SourceBody=Frame.Batch->Body;
                Trail->Source=Frame.Dash->Source;
            }
            FLDFinalTrafficSegment Segment;
            Segment.Key=FGuid::NewGuid();
            Segment.Ground={Motion.FromFootCm,Motion.ToFootCm,Motion.StartServerSeconds,Motion.EndServerSeconds,double(Card->DurationMs)/1000.};
            Trail->Segments.Add(MoveTemp(Segment));
            if(!LDMatchLifecycle::AddDurationMs(FMath::CeilToInt64(Motion.EndServerSeconds*1000.),Card->DurationMs,Trail->EndMs,Error)){return false;}
        }
    }
    for (FLDFinalTrafficTrail& Trail : FinalTraffic)
    {
        const FLDDemoFinalModDefinition* Card=nullptr;
        if (!FinalModOf(Trail.SourceBody.Victim.EntityId,Card,Error)) { return false; }
        if (!Card) { continue; }
        for (const FLDFinalMotionFrame& Frame : FinalMotion)
        {
            const FLDBodyCombatState* Target=FindBody(Frame.Batch->Body);
            if (!Target || Target->bPendingDeath || Frame.Batch->Body.Victim.EntityId==Trail.SourceBody.Victim.EntityId
                || IsIsolated(Trail.SourceBody.Victim.EntityId,*Target) || Mode->IsContenderProtected(Target->Victim.EntityId))
            { continue; }
            bool bWasInside=Trail.Inside.Contains(Frame.Batch->Body),bEntered=false;
            const double HalfWidth=Cm(Card->RadiusCm);
            bool bGrounded=Frame.Batch->InitialSupport.Grounded;
            double Cursor=Frame.Batch->Step.StartServerSeconds;
            FVector Foot=Frame.Batch->StartFootCm;
            const auto StandUntil=[&](double At)->bool
            {
                if(bGrounded&&At>Cursor&&!CrossesBand(Foot,Foot,Cursor,At,Trail,HalfWidth,bWasInside,bEntered,Error)){return false;}
                if(bGrounded&&!InsideBand(Foot,Trail,HalfWidth,At,bWasInside,Error)){return false;}
                Cursor=At;return true;
            };
            for(const FLDNativeMotionRecord& Motion:Frame.Batch->Records)
            {
                if(Motion.StartServerSeconds>Cursor&&!StandUntil(Motion.StartServerSeconds)){return false;}
                if(Motion.Kind!=ELDNativeMotionRecordKind::Movement&&Motion.EndServerSeconds>Cursor
                    &&!StandUntil(Motion.EndServerSeconds)){return false;}
                const bool bWalked=bGrounded&&Motion.Support.Grounded;
                bGrounded=Motion.Support.Grounded;
                if(Motion.Kind==ELDNativeMotionRecordKind::Movement&&bWalked
                    &&!CrossesBand(Motion.FromFootCm,Motion.ToFootCm,Motion.StartServerSeconds,Motion.EndServerSeconds,
                    Trail,HalfWidth,bWasInside,bEntered,Error)){return false;}
                const bool bBefore=bWasInside;
                Foot=Motion.ToFootCm;Cursor=FMath::Max(Cursor,Motion.EndServerSeconds);
                if(!bGrounded){bWasInside=false;}
                else if(!InsideBand(Foot,Trail,HalfWidth,Cursor,bWasInside,Error)){return false;}
                bEntered|=bWasInside&&!bBefore;
            }
            bool bInside=false;
            if(Frame.Batch->FinalSupport.Grounded)
            {
                if(Frame.Batch->Step.EndServerSeconds>Cursor&&!StandUntil(Frame.Batch->Step.EndServerSeconds)){return false;}
                if(!InsideBand(Frame.Batch->EndFootCm,Trail,HalfWidth,Frame.Batch->Step.EndServerSeconds,bInside,Error)){return false;}
                bEntered|=bInside&&!bWasInside;
            }
            if (bEntered)
            {
                FLDControlApplication Slow;
                Slow.Kind=ELDControlKind::Slow;
                Slow.EffectId=FGuid::NewGuid();
                Slow.SourceBody=Trail.SourceBody;
                Slow.SourceEntity=Slow.CreditOwner=Trail.SourceBody.Victim.EntityId;
                Slow.Source.Kind=ELDCombatSourceKind::FinalMod;
                Slow.Source.DefinitionId=FName(*Card->Id);
                Slow.RatioBp=Card->SlowBp;
                Slow.DurationMs=Card->SlowMs;
                bool bApplied=false;
                FLDBodyCombatState* Mutable=FindBody(Frame.Batch->Body);
                if (!CommitControl(*Mutable,Slow,NowMs,bApplied,Error)) { return false; }
            }
            if (bInside) { Trail.Inside.AddUnique(Frame.Batch->Body); }
            else { Trail.Inside.Remove(Frame.Batch->Body); }
        }
    }
    for(FLDFinalTrafficTrail& Trail:FinalTraffic)
    {Trail.Segments.RemoveAll([NowSeconds](const FLDFinalTrafficSegment& Segment)
        {return Segment.Ground.EndSeconds+Segment.Ground.DurationSeconds<=NowSeconds;});}
    FinalTraffic.RemoveAll([](const FLDFinalTrafficTrail& Trail){return Trail.Segments.IsEmpty();});
    FinalMotion.Reset();
    return true;
}

int64 ALDCombatDirector::NextFinalModDueMs(int64 NowMs) const
{
    int64 Due=MAX_int64;
    const auto Consider=[NowMs,&Due](int64 At) { if (At>NowMs) { Due=FMath::Min(Due,At); } };
    for (const FLDFinalControlExit& Watch : FinalControlExits)
    { Consider(Watch.DueAtMs>0 ? Watch.DueAtMs : Watch.ExpectedEndMs); }
    for (const FLDFinalTrafficTrail& Trail : FinalTraffic) { Consider(Trail.EndMs); }
    for (const auto& Pair : Bodies)
    {
        Consider(Pair.Value.FinalMod.VoiceUntilMs);
        Consider(Pair.Value.FinalMod.ArmedUntilMs);
    }
    return Due;
}
