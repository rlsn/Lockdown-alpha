#include "Combat/LDCombatDirector.h"
#include "Core/LDLog.h"
#include "Character/LDCharacter.h"
#include "Combat/LDTacticalRules.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDLifeAuthority.h"
#include "Region/LDMapGeometry.h"
#include "Region/LDMapRegistry.h"
#include "World/LDAirdrop.h"
#include "Engine/World.h"

// P6 combat hooks (spec §11.1/§11.3): the rain's puddles, the typhoon's push, and the damage-taken read the locker channel
// uses.

bool ALDCombatDirector::TookDamageSince(const FLDCombatBodyRef& Ref, int64 SinceMs) const
{
    const FLDBodyCombatState* Body=FindBody(Ref);
    return Body && Body->Cues.LastHitId.IsValid() && Body->Cues.LastHitAtMs>SinceMs;
}

bool ALDCombatDirector::SpawnDailyRain(uint64 Seed, int32 Count, const FLDGameplayWritePermit& Permit, FString& Error)
{
    ILDLifeAuthority* Mode=Authority();
    const FLDDemoContentSnapshot* Content=Catalog();
    const ULDMapRegistry* Registry=GetWorld() ? GetWorld()->GetSubsystem<ULDMapRegistry>() : nullptr;
    const FLDDemoWorldGeometry* Geometry=Registry ? Registry->GetGeometry() : nullptr;
    if (!Mode || !Content || !Geometry || Mode->ValidateWritePermit(Permit,ELDCommitPhase::RespawnsAndWorld)!=ELDCommitReject::None)
    {
        Error=TEXT("The rain needs the current RespawnsAndWorld permit, approved content and the registered world.");
        return false;
    }
    // Spec §11.1: puddles at random field places: distinct field layout points, each moved by a seeded offset within the
    // puddle's own radius. The rest room and the final arena never get rain.
    TArray<FVector> Places;
    for (const FLDDemoWorldAnchor& Anchor : Geometry->Anchors)
    {
        switch (Anchor.Kind)
        {
        case ELDDemoWorldAnchorKind::Spawn: case ELDDemoWorldAnchorKind::Camp: case ELDDemoWorldAnchorKind::Airdrop:
        case ELDDemoWorldAnchorKind::MiniFlag: case ELDDemoWorldAnchorKind::Locker:
        case ELDDemoWorldAnchorKind::BossWaypoint: case ELDDemoWorldAnchorKind::Capture:
        {
            const FLDMapBinding* Binding=Registry->Resolve(FName(*Anchor.Id),Registry->Generation);
            if (Binding) { Places.Add(Binding->Position); }
            break;
        }
        default: break;
        }
    }
    FLDGroundTerms Terms;
    if (!GroundTerms(Terms,Error)) { return false; }
    const double Spread=Terms.Of(ELDGroundStateKind::Puddle).RadiusCm;
    const int64 NowMs=Permit.GetBoundary().AtMs;
    uint64 State=Seed;
    for (int32 Index=0; Index<Count && !Places.IsEmpty(); ++Index)
    {
        State=LDAirdrop::Mix(State+0x9E3779B97F4A7C15ull);
        const int32 Pick=int32(State%uint64(Places.Num()));
        const double Angle=double((State>>20)%3600)/3600.*2.*UE_DOUBLE_PI;
        const FVector Centre=Places[Pick]+FVector(FMath::Cos(Angle)*Spread,FMath::Sin(Angle)*Spread,0.);
        Places.RemoveAtSwap(Pick);
        if (!SpawnGround(ELDGroundStateKind::Puddle,FLDElementApplier(),Centre,NowMs,Error,0,Content->Get().Parameters.Daily.RainPuddleMs))
        {
            return false;
        }
    }
    UE_LOG(LogLDCombat,Display,TEXT("LD_DAILY_RAIN t=%lld puddles=%d"),NowMs,Count);
    Error.Reset();
    return true;
}

bool ALDCombatDirector::ApplyTyphoon(const FVector2D& Direction, double PushCm, const FLDGameplayWritePermit& Permit, FString& Error)
{
    ILDLifeAuthority* Mode=Authority();
    if (!Mode || Mode->ValidateWritePermit(Permit,ELDCommitPhase::RespawnsAndWorld)!=ELDCommitReject::None || Direction.IsNearlyZero()
        || !FMath::IsFinite(PushCm) || PushCm<=0)
    {
        Error=TEXT("The typhoon needs the current RespawnsAndWorld permit, a direction and a positive push.");
        return false;
    }
    FLDElementTerms Terms;
    if (!ElementTerms(Terms,Error)) { return false; }
    const FVector Push(Direction.GetSafeNormal(),0.);
    // Every living contender is blown the same way through the ordinary knockback path (walls stop it); no damage or credit.
    int32 Pushed=0;
    for (auto& Pair : Bodies)
    {
        FLDBodyCombatState& Body=Pair.Value;
        ALDCharacter* Actor=Pair.Key.Get();
        if (!Actor || Body.bPendingDeath || Body.bSuspended || Body.Victim.Kind!=ELDVictimKind::Contender
            || Mode->IsInRestSpace(Body.Victim.EntityId) || Mode->IsTrainingDummy(Body.Victim.EntityId)) { continue; }
        FLDCombatHit Hit;
        Hit.HitId=FGuid::NewGuid();
        Hit.EffectId=Hit.HitId;
        Hit.Target={Body.Victim,Body.Identity};
        // A zero-damage settle: it still needs a hit class and damage type to pass evaluation.
        Hit.HitClass=ELDCombatHitClass::Ability;
        Hit.DamageType=ELDDemoDamageType::Magical;
        Hit.bStatusSettle=true;
        Hit.KnockbackCm=PushCm;
        Hit.KnockbackMs=Terms.PushMs;
        Hit.bKnockbackFromAnchor=true;
        Hit.KnockbackAnchorCm=FootOf(*Actor)-Push*100.;
        PendingHits.Add(MoveTemp(Hit));
        ++Pushed;
    }
    // Every ground state drifts as far.
    for (FLDGroundState& Ground : GroundStates)
    {
        Ground.CentreCm+=Push*PushCm;
        ++Ground.Version;
    }
    UE_LOG(LogLDCombat,Display,TEXT("LD_DAILY_TYPHOON t=%lld direction=%s pushed=%d grounds=%d"),Permit.GetBoundary().AtMs,
        *Direction.ToString(),Pushed,GroundStates.Num());
    Error.Reset();
    return true;
}
