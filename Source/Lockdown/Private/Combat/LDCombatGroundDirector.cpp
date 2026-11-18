#include "Combat/LDCombatDirector.h"
#include "Core/LDLog.h"
#include "Combat/LDCombatElements.h"
#include "Combat/LDTacticalRules.h"
#include "Character/LDCharacter.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDGameMode.h"
#include "Vision/LDViewTypes.h"

// Spec §8.3 ground states (docs/systems/COMBAT_AND_GROWTH.md "地面状态"): puddle, ice, fire, miasma, field and the blizzard
// area. They harm only their owner's enemies, never block, hide or collide, convert when an element lands inside, and are
// public (LDWorldViewRelay sends every live one with its owner's seat). Ice is an authoritative movement parameter: its
// enemies accelerate and brake at GROUND.IceAccelScale and their knockbacks and dashes go GROUND.IceDisplacementScale as far.
namespace LDCombatGroundDetail
{
    FLDCombatHit GroundHit(const FLDElementApplier& GroundOwner, const FLDCombatBodyRef& Target, int64 BaseP2)
    {
        FLDCombatHit Hit;
        Hit.HitId=FGuid::NewGuid();
        Hit.EffectId=FGuid::NewGuid();
        Hit.Target=Target;
        Hit.SourceBody=GroundOwner.SourceBody;
        Hit.SourceEntity=GroundOwner.SourceEntity;
        Hit.CreditOwner=GroundOwner.CreditOwner;
        Hit.Source=GroundOwner.Source;
        Hit.HitClass=ELDCombatHitClass::Ability;
        Hit.BaseP2=BaseP2;
        Hit.DamageType=ELDDemoDamageType::Magical;
        Hit.bStatusSettle=true;
        Hit.bPeriodic=true;
        return Hit;
    }

    // The planar reach of a ground state from its centre (its bounding circle).
    double Reach(const FLDGroundState& Ground)
    {
        return Ground.HalfLengthCm>0 ? Ground.HalfLengthCm+Ground.RadiusCm : Ground.RadiusCm;
    }
}

bool ALDCombatDirector::SpawnGround(ELDGroundStateKind Kind, const FLDElementApplier& GroundOwner, const FVector& Centre, int64 NowMs,
    FString& Error, double RadiusCm, int64 LifeMs, const FVector2D& Heading, double HalfLengthCm, double AngleDegrees,
    int64 VulnerableBp, int64 AttackSlowBp)
{
    if (Kind==ELDGroundStateKind::None) { return true; }
    FLDGroundTerms Terms;
    if (!GroundTerms(Terms,Error)) { return false; }
    const FLDGroundKindTerms& KindTerms=Terms.Of(Kind);
    FLDGroundState Ground;
    Ground.Kind=Kind;
    Ground.Owner=GroundOwner;
    Ground.CentreCm=Centre;
    Ground.RadiusCm=RadiusCm>0 ? RadiusCm : KindTerms.RadiusCm;
    Ground.Heading=Heading.IsNearlyZero() ? FVector2D(1,0) : Heading.GetSafeNormal();
    Ground.HalfLengthCm=FMath::Max(HalfLengthCm,0.);
    Ground.AngleDegrees=FMath::Clamp(AngleDegrees,0.,360.);
    Ground.VulnerableBp=VulnerableBp;
    Ground.AttackSlowBp=AttackSlowBp;
    Ground.StartMs=NowMs;
    const int64 Life=LifeMs>0 ? LifeMs : KindTerms.LifeMs;
    if (!FMath::IsFinite(Ground.RadiusCm) || Ground.RadiusCm<=0
        || !LDMatchLifecycle::AddDurationMs(NowMs,Life,Ground.ExpiresAtMs,Error)
        || !LDMatchLifecycle::AddDurationMs(NowMs,KindTerms.TickMs,Ground.NextTickMs,Error))
    {
        if (Error.IsEmpty()) { Error=TEXT("A ground state needs a positive radius and life."); }
        return false;
    }
    // A skill keeps one live instance of its own placed ground state (Y12: one more per copy); then the caps evict the
    // oldest of the owner and of the map so this one fits.
    if (GroundOwner.Source.Kind==ELDCombatSourceKind::Learned && GroundOwner.EffectId.IsValid())
    {
        int32 Allowed=1;
        if (const FLDBodyCombatState* Body=FindBody(GroundOwner.SourceBody); Body && !Body->bPendingDeath)
        {
            TArray<FLDPassiveEffectRef> Stalls;
            if (!CollectPassiveEffects(Body->AttributeProjection,ELDDemoEffectOp::DoubleStall,Stalls,Error)) { return false; }
            Allowed+=Stalls.Num();
        }
        TArray<int32> Same;
        for (int32 Index=0; Index<GroundStates.Num(); ++Index)
        {
            const FLDGroundState& Live=GroundStates[Index];
            if (Live.Owner.SourceEntity==GroundOwner.SourceEntity && Live.Source==GroundOwner.Source && Live.Source.Kind==ELDCombatSourceKind::Learned)
            { Same.Add(Index); }
        }
        Same.Sort([this](int32 Left, int32 Right) { return GroundStates[Left].CreatedSerial<GroundStates[Right].CreatedSerial; });
        TArray<int32> Drop;
        for (int32 Index=0; Index<=Same.Num()-Allowed; ++Index) { Drop.Add(Same[Index]); }
        Drop.Sort([](int32 Left, int32 Right) { return Left>Right; });
        for (const int32 Index : Drop) { GroundStates.RemoveAt(Index); }
        Ground.Source=GroundOwner.Source;
    }
    TArray<int32> Evicted;
    LDCombatElements::GroundEvictions(GroundStates,GroundOwner.SourceEntity,Terms.MaxPerOwner,Terms.MaxTotal,Evicted);
    for (const int32 Index : Evicted)
    {
        UE_LOG(LogLDCombat,Log,TEXT("LD_GROUND_EVICT t=%lld id=%u kind=%s owner=%s"),NowMs,GroundStates[Index].Id,
            LDCombatElements::GroundName(GroundStates[Index].Kind),*LDLog::Id(GroundStates[Index].Owner.SourceEntity));
        GroundStates.RemoveAt(Index);
    }
    // The public id is one byte on the wire: the next free one among the live states (at most 40 live, so one is free).
    do { NextGroundId=NextGroundId%255+1; }
    while (GroundStates.ContainsByPredicate([this](const FLDGroundState& Live) { return Live.Id==NextGroundId; }));
    Ground.Id=NextGroundId;
    Ground.CreatedSerial=++NextAreaSerial;
    UE_LOG(LogLDCombat,Log,TEXT("LD_GROUND t=%lld id=%u kind=%s owner=%s via=%s centre=%s radius=%.0f life=%lld"),NowMs,Ground.Id,
        LDCombatElements::GroundName(Kind),*LDLog::Id(GroundOwner.SourceEntity),*LDLog::Source(GroundOwner.Source),*Centre.ToString(),
        Ground.RadiusCm,Life);
    GroundStates.Add(MoveTemp(Ground));
    return true;
}

bool ALDCombatDirector::ConvertGroundAt(const FVector& Foot, ELDCombatElement Incoming, const FLDElementApplier& Applier,
    const FVector& GustDirection, int64 NowMs, FString& Error)
{
    using namespace LDCombatGroundDetail;
    if (Incoming==ELDCombatElement::None || GroundStates.IsEmpty()) { return true; }
    FLDGroundTerms Terms;
    FLDElementTerms Elements;
    if (!GroundTerms(Terms,Error) || !ElementTerms(Elements,Error)) { return false; }
    // Snapshot the states that contain the landing point: a conversion may add fog, bursts and moved states.
    TArray<uint32> Hits;
    for (const FLDGroundState& Ground : GroundStates)
    {
        if (Ground.ExpiresAtMs>NowMs && LDCombatElements::GroundConversion(Ground.Kind,Incoming).Any()
            && LDCombatElements::GroundContains(Ground,Foot)) { Hits.Add(Ground.Id); }
    }
    for (const uint32 Id : Hits)
    {
        const int32 Index=GroundStates.IndexOfByPredicate([Id](const FLDGroundState& Row) { return Row.Id==Id; });
        if (Index==INDEX_NONE) { continue; }
        const FLDGroundState Before=GroundStates[Index];
        const LDCombatElements::FGroundConversion Conversion=LDCombatElements::GroundConversion(Before.Kind,Incoming);
        if (!Conversion.Any()) { continue; }
        // A converted state belongs to whoever landed the element; a moved one keeps its owner.
        if (Conversion.Becomes!=ELDGroundStateKind::None)
        {
            const FLDGroundKindTerms& Into=Terms.Of(Conversion.Becomes);
            FLDGroundState& Ground=GroundStates[Index];
            Ground.Kind=Conversion.Becomes;
            Ground.Owner=Applier;
            Ground.Source=FLDCombatSourceRef();
            Ground.VulnerableBp=0;
            Ground.AttackSlowBp=0;
            ++Ground.Version;
            if (!LDMatchLifecycle::AddDurationMs(NowMs,Into.LifeMs,Ground.ExpiresAtMs,Error)
                || !LDMatchLifecycle::AddDurationMs(NowMs,Into.TickMs,Ground.NextTickMs,Error)) { return false; }
        }
        if (Conversion.bMove && !GustDirection.IsNearlyZero())
        {
            FLDGroundState& Ground=GroundStates[Index];
            Ground.CentreCm+=GustDirection.GetSafeNormal2D()*Terms.GustMoveCm;
            ++Ground.Version;
        }
        if (Conversion.bFog)
        {
            Fogs.RemoveAll([&Applier](const FLDCombatFog& Fog) { return Fog.Owner.SourceEntity==Applier.SourceEntity; });
            FLDCombatFog& Fog=Fogs.AddDefaulted_GetRef();
            Fog.EffectId=FGuid::NewGuid();
            Fog.Owner=Applier;
            Fog.CentreCm=Before.CentreCm;
            Fog.RadiusCm=Elements.FogRadiusCm;
            Fog.StartMs=NowMs;
            Fog.NextPulseMs=NowMs;
            if (!LDMatchLifecycle::AddDurationMs(NowMs,Elements.FogMs,Fog.EndMs,Error)) { return false; }
        }
        if (Conversion.bBurst)
        {
            // Miasma + fire: everyone inside takes MethaneRatio x R (one methane blast). Field + gust: R to every enemy within
            // FieldDischargeRadius. Both credit the element's applier and hit its enemies.
            const bool bMethane=Before.Kind==ELDGroundStateKind::Miasma;
            const double Radius=bMethane ? Reach(Before) : Terms.FieldDischargeRadiusCm;
            const int64 R=LDCombatElements::ReactionDamageP2(Applier.Level,Applier.SourceAPP2,Elements);
            const int64 Amount=bMethane ? LDCombatElements::ScaledP2(R,Elements.MethaneRatioBp) : R;
            TArray<FLDCombatBodyRef> Inside;
            if (!CollectAreaTargets(Before.CentreCm,Radius,FindBody(Applier.SourceBody),Applier.SourceEntity,Inside,Error))
            { Error=TEXT("Ground burst: ")+Error; return false; }
            for (const FLDCombatBodyRef& Ref : Inside)
            {
                ALDCharacter* Actor=nullptr;
                if (!FindBody(Ref,&Actor) || !Actor || (bMethane && !LDCombatElements::GroundContains(Before,FootOf(*Actor)))) { continue; }
                FLDCombatHit Burst=GroundHit(Applier,Ref,Amount);
                Burst.bPeriodic=false;
                Burst.ReactionKind=bMethane ? ELDCombatReaction::Methane : ELDCombatReaction::Thunderstorm;
                PendingHits.Add(MoveTemp(Burst));
            }
        }
        UE_LOG(LogLDCombat,Log,TEXT("LD_GROUND_CONVERT t=%lld id=%u from=%s element=%s into=%s by=%s moved=%d fog=%d burst=%d vanish=%d"),
            NowMs,Before.Id,LDCombatElements::GroundName(Before.Kind),LDCombatElements::ElementName(Incoming),
            LDCombatElements::GroundName(Conversion.Becomes),*LDLog::Id(Applier.SourceEntity),Conversion.bMove,Conversion.bFog,
            Conversion.bBurst,Conversion.bVanishes);
        if (Conversion.bVanishes)
        {
            const int32 Gone=GroundStates.IndexOfByPredicate([Id](const FLDGroundState& Row) { return Row.Id==Id; });
            if (Gone!=INDEX_NONE) { GroundStates.RemoveAt(Gone); }
        }
    }
    return true;
}

int64 ALDCombatDirector::NextGroundDueMs(int64 NowMs) const
{
    int64 Due=MAX_int64;
    for (const FLDGroundState& Ground : GroundStates)
    {
        if (Ground.NextTickMs>NowMs && Ground.NextTickMs<Ground.ExpiresAtMs) { Due=FMath::Min(Due,Ground.NextTickMs); }
        if (Ground.ExpiresAtMs>NowMs) { Due=FMath::Min(Due,Ground.ExpiresAtMs); }
    }
    return Due;
}

bool ALDCombatDirector::AdvanceGround(int64 NowMs, FString& Error)
{
    using namespace LDCombatGroundDetail;
    FLDGroundTerms Terms;
    FLDElementTerms Elements;
    if (!GroundTerms(Terms,Error) || !ElementTerms(Elements,Error)) { return false; }
    // Ticks due by now, taken from a snapshot: a tick's element may convert, move or end other states.
    TArray<FLDGroundState> Due;
    for (FLDGroundState& Ground : GroundStates)
    {
        const FLDGroundKindTerms& Kind=Terms.Of(Ground.Kind);
        while (Ground.NextTickMs<=NowMs && Ground.NextTickMs<Ground.ExpiresAtMs)
        {
            Due.Add(Ground);
            if (!LDMatchLifecycle::AddDurationMs(Ground.NextTickMs,Kind.TickMs,Ground.NextTickMs,Error)) { return false; }
        }
    }
    GroundStates.RemoveAll([NowMs](const FLDGroundState& Ground) { return Ground.ExpiresAtMs<=NowMs; });
    for (const FLDGroundState& Ground : Due)
    {
        if (Ground.Kind==ELDGroundStateKind::Ice) { continue; }
        TArray<FLDCombatBodyRef> Inside;
        if (!CollectAreaTargets(Ground.CentreCm,Reach(Ground),FindBody(Ground.Owner.SourceBody),Ground.Owner.SourceEntity,Inside,Error))
        { Error=TEXT("Ground tick: ")+Error; return false; }
        const int64 R=LDCombatElements::ReactionDamageP2(Ground.Owner.Level,Ground.Owner.SourceAPP2,Elements);
        for (const FLDCombatBodyRef& Ref : Inside)
        {
            ALDCharacter* Actor=nullptr;
            FLDBodyCombatState* Body=FindBody(Ref,&Actor);
            if (!Body || !Actor || Body->bPendingDeath || !LDCombatElements::GroundContains(Ground,FootOf(*Actor))) { continue; }
            switch (Ground.Kind)
            {
            case ELDGroundStateKind::Field:
                PendingHits.Add(GroundHit(Ground.Owner,Ref,LDCombatElements::ScaledP2(R,Terms.FieldRatioBp)));
                break;
            case ELDGroundStateKind::Blizzard:
            {
                FLDControlApplication App;
                App.Kind=ELDControlKind::Slow;
                App.EffectId=FGuid::NewGuid();
                App.SourceEntity=Ground.Owner.SourceEntity;
                App.SourceBody=Ground.Owner.SourceBody;
                App.CreditOwner=Ground.Owner.CreditOwner;
                App.DurationMs=Terms.Of(Ground.Kind).TickMs;
                App.RatioBp=Terms.BlizzardSlowBp;
                bool bApplied=false;
                if (!CommitControl(*Body,App,NowMs,bApplied,Error)) { return false; }
                break;
            }
            case ELDGroundStateKind::Miasma:
                // N3 A1: its enemies attack slower while inside.
                if (Ground.AttackSlowBp>0)
                {
                    FLDCombatSlow Slow;
                    Slow.EffectId=FGuid::NewGuid();
                    Slow.SourceEntity=Ground.Owner.SourceEntity;
                    Slow.RatioBp=Ground.AttackSlowBp;
                    if (!LDMatchLifecycle::AddDurationMs(NowMs,Terms.Of(Ground.Kind).TickMs,Slow.ExpiresAtMs,Error)) { return false; }
                    LDTactical::ApplySlow(Body->AttackSlows,Slow,NowMs);
                }
                break;
            default: break;
            }
            const ELDCombatElement Element=LDCombatElements::GroundElement(Ground.Kind);
            if (Element!=ELDCombatElement::None
                && !ApplyElement(*Body,Element,Ground.Owner,Element==ELDCombatElement::Fire ? Terms.FireBurnStacks : 0,FGuid::NewGuid(),
                    NowMs,Error)) { return false; }
        }
    }
    // Ice contact (the movement parameter) and Y23's standing start, read from every living body's foot now.
    for (auto& Pair : Bodies)
    {
        FLDBodyCombatState& Body=Pair.Value;
        ALDCharacter* Actor=Pair.Key.Get();
        if (!Actor || Body.bPendingDeath)
        {
            Body.bOnEnemyIce=false;
            Body.TurfSinceMs=0;
            continue;
        }
        const FVector Foot=FootOf(*Actor);
        bool bIce=false, bTurf=false;
        for (const FLDGroundState& Ground : GroundStates)
        {
            if (!LDCombatElements::GroundContains(Ground,Foot)) { continue; }
            if (Ground.Owner.SourceEntity==Body.Victim.EntityId) { bTurf=true; }
            else if (Ground.Kind==ELDGroundStateKind::Ice) { bIce=true; }
        }
        if (bIce!=Body.bOnEnemyIce)
        {
            Body.bOnEnemyIce=bIce;
            MarkViewChanged(Body.Victim.EntityId);
        }
        if (!bTurf) { Body.TurfSinceMs=0; }
        else if (Body.TurfSinceMs==0) { Body.TurfSinceMs=NowMs; }
    }
    return true;
}

void ALDCombatDirector::GroundFactors(const FLDBodyCombatState& Source, const FLDBodyCombatState& Target, TArray<FLDDamageFactor>& Out) const
{
    const ILDLifeAuthority* Mode=Authority();
    ALDCharacter* TargetActor=ActorOf(Target);
    if (!Mode || !TargetActor) { return; }
    const int64 NowMs=Mode->GetAuthorityNowMs();
    // N1 A1: the owner's damage on an enemy standing on its own ice (the strongest of its states there).
    const FVector Foot=FootOf(*TargetActor);
    int64 Vulnerable=0;
    for (const FLDGroundState& Ground : GroundStates)
    {
        if (Ground.VulnerableBp>0 && Ground.Owner.SourceEntity==Source.Victim.EntityId && LDCombatElements::GroundContains(Ground,Foot))
        { Vulnerable=FMath::Max(Vulnerable,Ground.VulnerableBp); }
    }
    if (Vulnerable>0) { Out.Add({FName(TEXT("ground_vulnerable")),Vulnerable}); }
    // Y23: every full interval its holder has stood on its own ground state (copies add their step).
    if (Source.TurfSinceMs>0)
    {
        TArray<FLDPassiveEffectRef> Turf;
        FString Ignored;
        if (!const_cast<ALDCombatDirector*>(this)->CollectPassiveEffects(Source.AttributeProjection,ELDDemoEffectOp::GroundGrowth,Turf,Ignored))
        { return; }
        for (const FLDPassiveEffectRef& Ref : Turf)
        {
            const int64 Steps=Ref.Effect.IntervalMs>0 ? (NowMs-Source.TurfSinceMs)/Ref.Effect.IntervalMs : 0;
            if (Steps>0) { Out.Add({Ref.Key,Steps*Ref.Effect.RatioBp}); }
        }
    }
}

bool ALDCombatDirector::RelocateNewestGround(const FLDBodyCombatState& GroundOwner, const FVector& Foot, int64 NowMs, FString& Error)
{
    // The newest not yet moved ground state, zone or trap of the owner (creation order across all three).
    FLDGroundState* Ground=nullptr;
    FLDCombatZone* Zone=nullptr;
    FLDCombatGroundEffect* Trap=nullptr;
    uint64 Newest=0;
    for (FLDGroundState& Row : GroundStates)
    {
        if (Row.Owner.SourceEntity==GroundOwner.Victim.EntityId && !Row.bRelocated && Row.ExpiresAtMs>NowMs && Row.CreatedSerial>Newest)
        { Newest=Row.CreatedSerial; Ground=&Row; Zone=nullptr; Trap=nullptr; }
    }
    for (FLDCombatZone& Row : Zones)
    {
        if (Row.SourceEntity==GroundOwner.Victim.EntityId && !Row.bRelocated && !Row.bTether && Row.EndMs>NowMs && Row.CreatedSerial>Newest)
        { Newest=Row.CreatedSerial; Zone=&Row; Ground=nullptr; Trap=nullptr; }
    }
    for (FLDCombatGroundEffect& Row : GroundEffects)
    {
        if (Row.bTrap && !Row.bTriggered && Row.Payload.SourceEntity==GroundOwner.Victim.EntityId && !Row.bRelocated
            && Row.CreatedSerial>Newest)
        { Newest=Row.CreatedSerial; Trap=&Row; Ground=nullptr; Zone=nullptr; }
    }
    const TCHAR* What=Ground ? TEXT("ground") : Zone ? TEXT("zone") : Trap ? TEXT("trap") : nullptr;
    if (!What) { return true; }
    if (Ground) { Ground->CentreCm=Foot; Ground->bRelocated=true; ++Ground->Version; }
    if (Zone) { Zone->CentreCm=Foot; Zone->bRelocated=true; }
    if (Trap) { Trap->CentreCm=Foot; Trap->bRelocated=true; }
    UE_LOG(LogLDCombat,Log,TEXT("LD_GROUND_RELOCATE t=%lld owner=%s what=%s to=%s"),NowMs,*LDLog::Id(GroundOwner.Victim.EntityId),What,
        *Foot.ToString());
    Error.Reset();
    return true;
}

bool ALDCombatDirector::ReadObservedGround(TArray<FLDObservedGroundFact>& Out, FString& Error) const
{
    Out.Reset();
    const ILDLifeAuthority* Mode=Authority();
    if (!Mode) { Error=TEXT("Observed ground states require the current match."); return false; }
    const int64 NowMs=Mode->GetAuthorityNowMs();
    for (const FLDGroundState& Ground : GroundStates)
    {
        if (Ground.ExpiresAtMs<=NowMs) { continue; }
        FLDObservedGroundFact& Fact=Out.AddDefaulted_GetRef();
        Fact.Id=Ground.Id;
        Fact.Kind=uint8(Ground.Kind);
        Fact.OwnerEntity=Ground.Owner.SourceEntity;
        // The owner's roster seat (its living body now, whichever life it is on).
        ALDCharacter* OwnerActor=nullptr;
        if (FindEntityBody(Ground.Owner.SourceEntity,&OwnerActor) && OwnerActor && Ground.Owner.SourceBody.Victim.Kind==ELDVictimKind::Contender)
        { Fact.OwnerSeat=OwnerActor->PresentationKey; }
        Fact.CentreCm=Ground.CentreCm;
        Fact.RadiusCm=float(Ground.RadiusCm);
        Fact.Heading=Ground.Heading;
        Fact.HalfLengthCm=float(Ground.HalfLengthCm);
        Fact.AngleDegrees=float(Ground.AngleDegrees);
        Fact.RemainingMs=uint32(FMath::Min<int64>(Ground.ExpiresAtMs-NowMs,MAX_uint32));
        Fact.LifeMs=uint32(FMath::Min<int64>(Ground.ExpiresAtMs-Ground.StartMs,MAX_uint32));
    }
    Error.Reset();
    return true;
}
