#include "Combat/LDCombatDirector.h"
#include "Core/LDLog.h"
#include "Combat/LDCombatCues.h"
#include "Character/LDCharacter.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Development/LDDemoContentIds.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDLifeAuthority.h"

// Read-only projections of Combat state for the observation owner: action cues and projectiles in flight.
namespace
{
    ELDObservedProjectileLook AreaLookOf(const FLDCombatHit& Hit)
    {
        if (Hit.BurnStacks>0 || Hit.CardBurnStacks>0) { return ELDObservedProjectileLook::AreaFire; }
        if (Hit.CarriesElement(ELDCombatElement::Shock)) { return ELDObservedProjectileLook::AreaElectric; }
        if (Hit.CarriesElement(ELDCombatElement::Water)) { return ELDObservedProjectileLook::AreaWater; }
        return Hit.DamageType==ELDDemoDamageType::Magical
            ? ELDObservedProjectileLook::AreaMagic : ELDObservedProjectileLook::AreaPlain;
    }
}

FLDCombatCueFacts ALDCombatDirector::CueFactsOf(const FLDBodyCombatState& Body)
{
    FLDCombatCueFacts Facts=Body.Cues;
    if (Body.Action.IsSet())
    {
        Facts.bActing=true;
        Facts.bActingBasic=Body.Action->Kind==ELDCombatActionKind::Basic && !Body.Action->bMonsterSpecial;
        Facts.ActingStartedAtMs=Body.Action->StartedAtMs;
        Facts.ActingReleaseAtMs=Body.Action->ReleaseAtMs;
    }
    else if (Body.ChannelEffectId.IsValid())
    {
        Facts.bActing=true;
        Facts.bActingBasic=false;
        Facts.ActingStartedAtMs=Body.ChannelStartedAtMs;
        Facts.ActingReleaseAtMs=Body.ChannelStartedAtMs;
    }
    Facts.DashUntilMs=Body.DashUntilMs;
    // The cast these describe: the acting Q/W/E, else the last released one (a basic that started inside the released
    // cast's cue window leaves that cast's facts in place).
    const bool bActingCast=Body.Action.IsSet() && Body.Action->Kind!=ELDCombatActionKind::Basic;
    const FLDCombatSourceRef& CastSource=bActingCast ? Body.Action->Source : Body.ReleasedSource;
    Facts.CastDefinitionId=CastSource.Kind==ELDCombatSourceKind::Learned ? CastSource.DefinitionId : NAME_None;
    return Facts;
}

bool ALDCombatDirector::AppendObservedAction(const ALDCharacter& Body, int64 NowMs, FLDObservedPublicFacts& InOut, FString& Error) const
{
    InOut.ActionCue=ELDObservedActionCue::None;
    InOut.ActionCueAtMs=0;
    InOut.ActionCueImpactAtMs=0;
    InOut.LastHitAtMs=0;
    InOut.LastHitId.Invalidate();
    InOut.HitLook=ELDObservedHitLook::None;
    InOut.HitFlags=0;
    InOut.ActionAbility=0;
    InOut.ActionComboStep=0;
    InOut.MomentumBar=0;
    InOut.EmpowerReady=0;
    InOut.LastReaction=0;
    InOut.LastReactionAtMs=0;
    InOut.LastReactionElement=0;
    const FLDBodyCombatState* State=StateFor(&Body);
    if (!State || State->bPendingDeath) { return true; }
    const LDCombatCues::FCue Cue=LDCombatCues::Select(CueFactsOf(*State),NowMs);
    if (State->Victim.Kind==ELDVictimKind::Contender)
    {
        // The public bar is quantized.
        const FLDDemoContentSnapshot* Content=Catalog();
        const int64 MaxP2=Content ? Content->Get().Parameters.Momentum.MaxP2 : 0;
        if (MaxP2<=0) { Error=TEXT("The visible momentum bar needs the approved maximum."); return false; }
        const int64 Bar=FMath::Clamp<int64>(State->Resources.MomentumP2,0,MaxP2)*LDObservation::MomentumBarFull/MaxP2;
        InOut.MomentumBar=uint8(Bar);
        InOut.EmpowerReady=uint8(EmpowerReadyOf(*State,NowMs));
        if (Cue.Cue==ELDObservedActionCue::Attack)
        {
            const bool bActingBasic=State->Action.IsSet() && State->Action->Kind==ELDCombatActionKind::Basic
                && State->Action->StartedAtMs==Cue.AtMs;
            InOut.ActionComboStep=bActingBasic ? State->Action->ComboStep : State->Cues.ReleasedComboStep;
        }
    }
    uint8 Ability=0;
    if (!ObservedCastSource(Body,NowMs,Ability))
    {
        Error=TEXT("The visible cast has no approved ability source.");
        return false;
    }
    if(State->bMonsterEnraged){InOut.Statuses.Add({uint8(ELDDemoStatusId::CampEnraged),1,0});}
    if(State->MonsterGuardUntilMs>NowMs){InOut.Statuses.Add({uint8(ELDDemoStatusId::CampGuarding),1,State->MonsterGuardUntilMs});}
    InOut.ActionCue=Cue.Cue;
    InOut.ActionCueAtMs=Cue.AtMs;
    InOut.ActionCueImpactAtMs=Cue.ImpactAtMs;
    if (State->Cues.LastHitAtMs>0 && NowMs>=State->Cues.LastHitAtMs
        && NowMs-State->Cues.LastHitAtMs<LDCombatCues::HitHoldMs)
    {
        InOut.LastHitAtMs=State->Cues.LastHitAtMs;
        InOut.LastHitId=State->Cues.LastHitId;
        InOut.HitLook=static_cast<ELDObservedHitLook>(State->Cues.LastHitLook);
        InOut.HitFlags=State->Cues.LastHitFlags;
    }
    InOut.ActionAbility=Ability;
    if (State->Cues.LastReaction!=0 && State->Cues.LastReactionAtMs>0 && NowMs>=State->Cues.LastReactionAtMs
        && NowMs-State->Cues.LastReactionAtMs<LDObservation::RecentReactionWindowMs)
    {
        InOut.LastReaction=State->Cues.LastReaction;
        InOut.LastReactionAtMs=State->Cues.LastReactionAtMs;
        InOut.LastReactionElement=State->Cues.LastReactionElement;
    }
    return true;
}

bool ALDCombatDirector::ObservedCastSource(const ALDCharacter& Body, int64 NowMs, uint8& OutAbility) const
{
    OutAbility=0;
    const FLDBodyCombatState* State=StateFor(&Body);
    if (!State || State->bPendingDeath) { return true; }
    const LDCombatCues::FCue Cue=LDCombatCues::Select(CueFactsOf(*State),NowMs);
    if (Cue.Cue!=ELDObservedActionCue::Cast) { return true; }
    // The Cast cue shows the winding-up ability when that is the newest, else the released one (LDCombatCues).
    const bool bActing=State->Action.IsSet() && State->Action->Kind!=ELDCombatActionKind::Basic
        && State->Action->StartedAtMs==Cue.AtMs;
    const FLDCombatSourceRef& Source=bActing ? State->Action->Source : State->ReleasedSource;
    // The innate dash is movement shared by every contender: its cast shows no ability name.
    if (Source.Kind==ELDCombatSourceKind::Innate || State->Victim.Kind==ELDVictimKind::CampMember) { return true; }
    const FLDDemoContentSnapshot* Content=Catalog();
    FString Error;
    if (Content && Source.Kind==ELDCombatSourceKind::Learned)
    {
        const FLDDemoAbilityDefinition* Ability=nullptr;
        if (Content->FindAbilityByName(Source.DefinitionId,Ability,Error)) { OutAbility=Ability->NativeId; return true; }
    }
    UE_LOG(LogLDCombat,Error,TEXT("LD_COMBAT_CAST_SOURCE_UNAVAILABLE %s %s"),*Source.DefinitionId.ToString(),*Error);
    return false;
}

bool ALDCombatDirector::ReadIncomingLocks(const FGuid& Observer, TArray<FLDCombatIncomingLock>& Out, FString& Error) const
{
    Out.Reset();
    const auto* Mode=Authority();
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!Mode || !Content) { Error=TEXT("Incoming locks require the current match and its content."); return false; }
    const int64 NowMs=Mode->GetAuthorityNowMs();
    for (const auto& Pair : Bodies)
    {
        const FLDBodyCombatState& Body=Pair.Value;
        if (Body.bPendingDeath || !Body.Action.IsSet() || Body.Victim.EntityId==Observer) { continue; }
        const FLDCombatAction& Action=Body.Action.GetValue();
        if (Action.Kind!=ELDCombatActionKind::Ability || Action.Source.Kind!=ELDCombatSourceKind::Learned
            || Action.Target.Kind!=ELDCombatTargetKind::Unit || Action.TargetBody.Victim.EntityId!=Observer || Action.ReleaseAtMs<=NowMs)
        { continue; }
        const FLDDemoAbilityDefinition* Ability=nullptr;
        if (!Content->FindAbilityByName(Action.Source.DefinitionId,Ability,Error)) { return false; }
        FLDCombatIncomingLock& Lock=Out.AddDefaulted_GetRef();
        Lock.SourceEntity=Body.Victim.EntityId;
        Lock.RemainingMs=Action.ReleaseAtMs-NowMs;
        Lock.Ability=Ability->NativeId;
    }
    Error.Reset();
    return true;
}

bool ALDCombatDirector::ReadObservedProjectiles(TArray<FLDObservedProjectileFact>& Out, FString& Error) const
{
    Out.Reset();
    const FLDDemoContentSnapshot* Content=Catalog();
    if (!Content)
    {
        Error=TEXT("Observed projectiles require the approved Demo content.");
        return false;
    }
    const int64 MonsterNow=Authority()->GetAuthorityNowMs();
    for(const auto& Strike:MonsterStrikes)
    {
        const auto* Source=FindBody(Strike.Payload.SourceBody);
        if(Strike.AtMs<MonsterNow || (!Strike.bReleased && (!Source || !Source->Action.IsSet() || Source->Action->Token!=Strike.Token))){continue;}
        auto& Fact=Out.AddDefaulted_GetRef();Fact.Key=Strike.Key;Fact.SourceEntity=Strike.Payload.SourceEntity;
        Fact.PositionCm=Strike.Area.Origin;Fact.Heading=FVector2D(Strike.Area.Direction.X,Strike.Area.Direction.Y);
        Fact.Look=Strike.Area.Shape==ELDMonsterShape::Circle?ELDObservedProjectileLook::AreaMagic:
            Strike.Area.Shape==ELDMonsterShape::Cone?ELDObservedProjectileLook::ConeWarning:ELDObservedProjectileLook::LineWarning;
        Fact.RadiusCm=float(Strike.Area.Shape==ELDMonsterShape::Cone?Strike.Area.Range:Strike.Area.Radius);
        if(Strike.Area.Shape==ELDMonsterShape::Cone){Fact.AngleDegrees=float(Strike.Area.Angle);}
        if(Strike.Area.Shape==ELDMonsterShape::Line){Fact.LengthCm=float(Strike.Area.Range);}
        // Authorize a line at its locked origin: a hidden midpoint beyond a wall cannot hide a visible incoming warning.
        Fact.WarningRemainingMs=uint32(FMath::Min<int64>(FMath::Max<int64>(Strike.AtMs-MonsterNow,0),MAX_uint32));
    }
    for (const FLDCombatProjectile& Projectile : Projectiles)
    {
        const FVector2D Heading=FVector2D(Projectile.Direction.X,Projectile.Direction.Y).GetSafeNormal();
        const FLDDemoAbilityDefinition* Ability=nullptr;
        FString LookupError;
        const FLDDemoMonsterDefinition* Monster=nullptr;
        // A05 makes a contender's basic fly: its source is the unnamed basic, never a catalog definition.
        const bool bFlyingBasic=Projectile.Source.Kind==ELDCombatSourceKind::Basic && Projectile.Source.DefinitionId.IsNone();
        const bool bSpellWave=Projectile.Source.Kind==ELDCombatSourceKind::Card
            && Projectile.Source.DefinitionId==FName(TEXT("card_spellblade_echo"));
        const bool bMonster=!bFlyingBasic && Projectile.Source.Kind==ELDCombatSourceKind::Basic
            && Content->FindMonsterByName(Projectile.Source.DefinitionId,Monster,LookupError)
            && Monster->Skill.Pattern==ELDMonsterPattern::Projectile;
        if ((!bFlyingBasic && !bSpellWave && !bMonster && (!Content->FindAbilityByName(Projectile.Source.DefinitionId,Ability,LookupError)
            || (Ability->NativeId!=uint8(ELDDemoAbilityId::ActiveSnarkBolt)
                && Ability->NativeId!=uint8(ELDDemoAbilityId::ActiveStaleTakeout)
                && Ability->NativeId!=uint8(ELDDemoAbilityId::ActiveFlurryRush))))
            || Heading.IsNearlyZero())
        {
            Out.Reset();
            Error=FString::Printf(TEXT("Projectile %s from %s has no approved observed look. %s"),
                *Projectile.EffectId.ToString(),*Projectile.Source.DefinitionId.ToString(),*LookupError);
            return false;
        }
        FLDObservedProjectileFact& Fact=Out.AddDefaulted_GetRef();
        // Each flight is observed by its own identity.
        Fact.Key=Projectile.FlightId;
        Fact.SourceEntity=Projectile.SourceEntity;
        Fact.PositionCm=Projectile.OriginCm+FVector(Heading.X,Heading.Y,0)*Projectile.TravelledCm;
        Fact.Heading=Heading;
        const bool bSock=Ability && Ability->NativeId==uint8(ELDDemoAbilityId::ActiveStaleTakeout);
        const bool bFistWind=bSpellWave || (Ability && Ability->NativeId==uint8(ELDDemoAbilityId::ActiveFlurryRush));
        Fact.Look=bFistWind ? ELDObservedProjectileLook::FistWind
            : Projectile.bReturn
                ? (bSock ? ELDObservedProjectileLook::SockReturn : ELDObservedProjectileLook::ChalkReturn)
                : (bSock ? ELDObservedProjectileLook::Sock : ELDObservedProjectileLook::Chalk);
    }
    const auto* Mode=Authority();
    if (!Mode) { Error=TEXT("Observed skill effects require the current match."); return false; }
    const int64 NowMs=Mode->GetAuthorityNowMs();
    for (const auto& Ground : GroundEffects)
    {
        FLDObservedProjectileFact& Fact=Out.AddDefaulted_GetRef();
        Fact.Key=Ground.ObservedKey.IsValid() ? Ground.ObservedKey : Ground.Payload.EffectId;
        Fact.SourceEntity=Ground.Payload.SourceEntity;
        Fact.PositionCm=Ground.CentreCm;
        Fact.Heading=FVector2D(1,0);
        Fact.Look=Ground.bTrap
            ? (Ground.bTriggered && NowMs<Ground.ReadyAtMs ? ELDObservedProjectileLook::FlyerArming : ELDObservedProjectileLook::FlyerArmed)
            : (Ground.Origin==ELDCombatGroundOrigin::Impact
                || (Ground.Origin==ELDCombatGroundOrigin::Echo
                    && Ground.Payload.Source.DefinitionId==FName(TEXT("active_calculus_drop"))))
                ? ELDObservedProjectileLook::BookShadow : AreaLookOf(Ground.Payload);
        Fact.RadiusCm=float(Ground.RadiusCm);
        Fact.InnerRadiusCm=float(Ground.InnerRadiusCm);
        Fact.WarningRemainingMs=Ground.ReadyAtMs>NowMs
            ? uint32(FMath::Min<int64>(Ground.ReadyAtMs-NowMs,MAX_uint32)) : 0;
    }
    // C-12: a ground impact in its windup shows the landing it will strike, under the key its ground effect keeps.
    for (const auto& Pair : Bodies)
    {
        const FLDBodyCombatState& Body=Pair.Value;
        if (Body.bPendingDeath || !Body.Action.IsSet() || !Body.Action->Landing.IsSet()) { continue; }
        const FLDCombatLandingPreview& Landing=Body.Action->Landing.GetValue();
        FLDObservedProjectileFact& Fact=Out.AddDefaulted_GetRef();
        Fact.Key=Landing.ObservedKey;
        Fact.SourceEntity=Body.Victim.EntityId;
        Fact.PositionCm=Landing.CentreCm;
        Fact.Heading=FVector2D(1,0);
        Fact.Look=ELDObservedProjectileLook::BookShadow;
        Fact.RadiusCm=float(Landing.RadiusCm);
        Fact.InnerRadiusCm=float(Landing.InnerRadiusCm);
        Fact.WarningRemainingMs=uint32(FMath::Clamp<int64>(Landing.ImpactAtMs-NowMs,0,MAX_uint32));
    }
    for (const FLDCombatZone& Zone : Zones)
    {
        if (Zone.EndMs<=NowMs) { continue; }
        int64 FirstPulseAtMs=0;
        if (Zone.DelayMs>0 && !LDMatchLifecycle::AddDurationMs(Zone.StartMs,Zone.DelayMs,FirstPulseAtMs,Error))
        {
            Out.Reset();
            Error=TEXT("Observed zone warning time is outside the checked authority clock.");
            return false;
        }
        FLDObservedProjectileFact& Fact=Out.AddDefaulted_GetRef();
        Fact.Key=Zone.ObservedKey.IsValid() ? Zone.ObservedKey : Zone.EffectId;
        Fact.SourceEntity=Zone.SourceEntity;
        Fact.PositionCm=Zone.CentreCm;
        Fact.Heading=FVector2D(1,0);
        Fact.Look=Zone.PulseBurnStacks>0 || Zone.BurnStacks>0 ? ELDObservedProjectileLook::AreaFire
            : Zone.bWet ? ELDObservedProjectileLook::AreaWater
            : Zone.bDamage && Zone.DamageType==ELDDemoDamageType::Magical ? ELDObservedProjectileLook::AreaMagic
            : ELDObservedProjectileLook::AreaPlain;
        Fact.RadiusCm=float(Zone.RadiusCm);
        Fact.WarningRemainingMs=FirstPulseAtMs>NowMs
            ? uint32(FMath::Min<int64>(FirstPulseAtMs-NowMs,MAX_uint32)) : 0;
    }
    // N2: a fan shows the rectangle of its next blow.
    for (const FLDCombatFan& Fan : Fans)
    {
        if (Fan.EndMs<=NowMs) { continue; }
        const FVector Heading=Fan.HeadingAt(NowMs).GetSafeNormal2D();
        FLDObservedProjectileFact& Fact=Out.AddDefaulted_GetRef();
        Fact.Key=Fan.ObservedKey;
        Fact.SourceEntity=Fan.Payload.SourceEntity;
        Fact.PositionCm=Fan.OriginCm+Heading*(Fan.LengthCm*.5);
        Fact.Heading=FVector2D(Heading.X,Heading.Y);
        Fact.Look=ELDObservedProjectileLook::Wind;
        Fact.LengthCm=float(Fan.LengthCm);
        Fact.RadiusCm=float(Fan.HalfWidthCm);
    }
    // N4: a bag and the wall to its partner are public obstacles.
    for (const FLDCombatBag& Bag : Bags)
    {
        FLDObservedProjectileFact& Fact=Out.AddDefaulted_GetRef();
        Fact.Key=Bag.ObservedKey;
        Fact.SourceEntity=Bag.Payload.SourceEntity;
        Fact.PositionCm=Bag.CentreCm;
        Fact.Heading=FVector2D(1,0);
        Fact.Look=ELDObservedProjectileLook::Bag;
        Fact.RadiusCm=float(Bag.BodyRadiusCm);
        if (!Bag.WallKey.IsValid()) { continue; }
        const FLDCombatBag* Partner=Bags.FindByPredicate([&Bag](const FLDCombatBag& Other) { return Other.ObservedKey==Bag.WallTo; });
        if (!Partner) { continue; }
        const FVector Span=Bag.CentreCm-Partner->CentreCm;
        FLDObservedProjectileFact& Wall=Out.AddDefaulted_GetRef();
        Wall.Key=Bag.WallKey;
        Wall.SourceEntity=Bag.Payload.SourceEntity;
        Wall.PositionCm=(Bag.CentreCm+Partner->CentreCm)*.5;
        Wall.Heading=FVector2D(Span.X,Span.Y).GetSafeNormal();
        Wall.Look=ELDObservedProjectileLook::BagWall;
        Wall.LengthCm=float(Span.Size2D());
        Wall.RadiusCm=float(Bag.WallHalfWidthCm);
    }
    for (const auto& Channel : Channels)
    {
        ALDCharacter* Actor=nullptr;
        const auto* Body=FindBody(Channel.Payload.SourceBody,&Actor);
        if (!Body || !Actor || Body->bPendingDeath || Body->ChannelEffectId!=Channel.Payload.EffectId) { continue; }
        FLDObservedProjectileFact& Fact=Out.AddDefaulted_GetRef();
        Fact.Key=Channel.Payload.EffectId;
        Fact.SourceEntity=Channel.Payload.SourceEntity;
        Fact.PositionCm=FootOf(*Actor);
        Fact.Heading=FVector2D(Channel.Direction.X,Channel.Direction.Y);
        Fact.Look=ELDObservedProjectileLook::SpeechCone;
        Fact.RadiusCm=float(Channel.RangeCm);
        Fact.AngleDegrees=float(Channel.AngleDegrees);
    }
    // #345 tactical world entities: slipper pickups, cordon tapes, phantom decoys, and shield shard bursts.
    for (const FLDCombatSlipperDrop& Drop : SlipperDrops)
    {
        if (Drop.ExpiresAtMs<=NowMs) { continue; }
        FLDObservedProjectileFact& Fact=Out.AddDefaulted_GetRef();
        Fact.Key=Drop.Id;
        Fact.SourceEntity=Drop.OwnerEntity;
        Fact.PositionCm=Drop.FootCm;
        Fact.Heading=FVector2D(1,0);
        Fact.Look=ELDObservedProjectileLook::SlipperDrop;
        Fact.RadiusCm=float(FMath::Max(Drop.RadiusCm,45.));
    }
    for (const FLDCombatCordon& Cordon : Cordons)
    {
        if (Cordon.ExpiresAtMs<=NowMs) { continue; }
        FLDObservedProjectileFact& Fact=Out.AddDefaulted_GetRef();
        Fact.Key=Cordon.Id;
        Fact.SourceEntity=Cordon.Payload.SourceEntity;
        Fact.PositionCm=Cordon.CentreCm;
        Fact.Heading=FVector2D(Cordon.Tangent.X,Cordon.Tangent.Y).GetSafeNormal();
        if (Fact.Heading.IsNearlyZero()) { Fact.Heading=FVector2D(0,1); }
        Fact.Look=ELDObservedProjectileLook::CordonTape;
        Fact.LengthCm=float(FMath::Max(Cordon.HalfLengthCm*2.,60.));
        Fact.RadiusCm=float(FMath::Max(Cordon.HalfWidthCm,30.));
    }
    for (const FLDCombatShardBurst& Burst : ShardBursts)
    {
        if (Burst.ExpiresAtMs<=NowMs) { continue; }
        FLDObservedProjectileFact& Fact=Out.AddDefaulted_GetRef();
        Fact.Key=Burst.Id;
        Fact.SourceEntity=Burst.OwnerEntity;
        Fact.PositionCm=Burst.FootCm;
        Fact.Heading=FVector2D(1,0);
        Fact.Look=ELDObservedProjectileLook::ShardBurst;
        Fact.RadiusCm=float(FMath::Max(Burst.RadiusCm,80.));
    }
    Error.Reset();
    return true;
}
