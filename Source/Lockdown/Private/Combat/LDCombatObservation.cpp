#include "Combat/LDCombatDirector.h"
#include "Core/LDLog.h"
#include "Combat/LDCombatCues.h"
#include "Character/LDCharacter.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Development/LDDemoContentIds.h"
#include "Framework/LDGameMode.h"

// Read-only projections of Combat state for the observation owner: action cues and projectiles in flight.
namespace
{
    ELDObservedProjectileLook AreaLookOf(const FLDCombatHit& Hit)
    {
        if (Hit.BurnStacks>0 || Hit.ComponentBurnStacks>0) { return ELDObservedProjectileLook::AreaFire; }
        if (Hit.bApplyElectric || Hit.ComponentElectricSource.Kind!=ELDCombatSourceKind::None)
        { return ELDObservedProjectileLook::AreaElectric; }
        if (Hit.bApplyWet || Hit.ComponentWetSource.Kind!=ELDCombatSourceKind::None) { return ELDObservedProjectileLook::AreaWater; }
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
        Facts.bActingBasic=Body.Action->Kind==ELDCombatActionKind::Basic;
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
    return Facts;
}

bool ALDCombatDirector::AppendObservedAction(const ALDCharacter& Body, int64 NowMs, FLDObservedPublicFacts& InOut, FString& Error) const
{
    InOut.ActionCue=ELDObservedActionCue::None;
    InOut.ActionCueAtMs=0;
    InOut.ActionCueImpactAtMs=0;
    InOut.LastHitAtMs=0;
    InOut.HitLook=ELDObservedHitLook::None;
    InOut.HitFlags=0;
    InOut.ActionAbility=0;
    const FLDBodyCombatState* State=StateFor(&Body);
    if (!State || State->bPendingDeath) { return true; }
    const LDCombatCues::FCue Cue=LDCombatCues::Select(CueFactsOf(*State),NowMs);
    uint8 Ability=0;
    if (!ObservedCastSource(Body,NowMs,Ability))
    {
        Error=TEXT("The visible cast has no approved ability source.");
        return false;
    }
    InOut.ActionCue=Cue.Cue;
    InOut.ActionCueAtMs=Cue.AtMs;
    InOut.ActionCueImpactAtMs=Cue.ImpactAtMs;
    if (State->Cues.LastHitAtMs>0 && NowMs>=State->Cues.LastHitAtMs
        && NowMs-State->Cues.LastHitAtMs<LDCombatCues::HitHoldMs)
    {
        InOut.LastHitAtMs=State->Cues.LastHitAtMs;
        InOut.HitLook=static_cast<ELDObservedHitLook>(State->Cues.LastHitLook);
        InOut.HitFlags=State->Cues.LastHitFlags;
    }
    InOut.ActionAbility=Ability;
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
    if (Source.Kind==ELDCombatSourceKind::Innate) { return true; }
    const FLDDemoContentSnapshot* Content=Catalog();
    FString Error;
    if (Content && Source.Kind==ELDCombatSourceKind::Learned)
    {
        const FLDDemoAbilityDefinition* Ability=nullptr;
        if (Content->FindAbility(Source.DefinitionId.ToString(),Ability,Error)) { OutAbility=Ability->NativeId; return true; }
    }
    UE_LOG(LogLDCombat,Error,TEXT("LD_COMBAT_CAST_SOURCE_UNAVAILABLE %s %s"),*Source.DefinitionId.ToString(),*Error);
    return false;
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
    for (const FLDCombatProjectile& Projectile : Projectiles)
    {
        const FVector2D Heading=FVector2D(Projectile.Direction.X,Projectile.Direction.Y).GetSafeNormal();
        const FLDDemoAbilityDefinition* Ability=nullptr;
        FString LookupError;
        if (!Content->FindAbility(Projectile.Source.DefinitionId.ToString(),Ability,LookupError)
            || (Ability->NativeId!=uint8(ELDDemoAbilityId::ActiveSnarkBolt) && Ability->NativeId!=uint8(ELDDemoAbilityId::ActiveSockShot))
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
        const bool bSock=Ability->NativeId==uint8(ELDDemoAbilityId::ActiveSockShot);
        Fact.Look=Projectile.bReturn
            ? (bSock ? ELDObservedProjectileLook::SockReturn : ELDObservedProjectileLook::ChalkReturn)
            : (bSock ? ELDObservedProjectileLook::Sock : ELDObservedProjectileLook::Chalk);
    }
    const auto* Mode=Core();
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
            : AreaLookOf(Ground.Payload);
        Fact.RadiusCm=float(Ground.RadiusCm);
        Fact.InnerRadiusCm=float(Ground.InnerRadiusCm);
        Fact.WarningRemainingMs=Ground.ReadyAtMs>NowMs
            ? uint32(FMath::Min<int64>(Ground.ReadyAtMs-NowMs,MAX_uint32)) : 0;
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
        Fact.Key=Zone.EffectId;
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
    Error.Reset();
    return true;
}
