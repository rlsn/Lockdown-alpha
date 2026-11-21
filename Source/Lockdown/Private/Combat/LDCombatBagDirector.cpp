#include "Combat/LDCombatDirector.h"
#include "Combat/LDCombatBagBlocker.h"
#include "Combat/LDCombatElements.h"
#include "Core/LDLog.h"
#include "Character/LDCharacter.h"
#include "Development/LDDemoContentCatalog.h"
#include "Engine/World.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDMatchLifecycleTypes.h"
#include "Region/LDMapRegistry.h"
#include "Vision/LDVisionDirector.h"

// Spec §5 N4 占座书包 (CONTENT_AND_BALANCE §4.2): a Combat-owned obstacle, not a body. It blocks bodies (a server blocker
// and, while seen, the owning client's observation proxy) and the enemy flights that reach it; enemy flights, ground blasts,
// channelled cones and release areas damage it. When its life runs out or it is knocked out it blows: magical damage to
// the owner's enemies around it, with its branch terms.

int64 ALDCombatDirector::RawBagDamageP2(const FLDCombatHit& Hit)
{
    // The hit's own amount before crits, factors and resistances: a bag has none of them.
    const auto ScaleTerm = [](int64 ValueP2, int64 RatioBp) -> int64
    {
        if (ValueP2 <= 0 || RatioBp <= 0) { return 0; }
        return ValueP2 / 10000 * RatioBp + ValueP2 % 10000 * RatioBp / 10000;
    };
    const int64 Scale = Hit.DeliveryScaleBp > 0 ? Hit.DeliveryScaleBp : 10000;
    const int64 AdaptiveStatP2 = Hit.SourceBonusADP2 * 14 >= Hit.SourceAPP2 * 10 ? Hit.SourceBonusADP2 : Hit.SourceAPP2;
    const int64 Raw = FMath::Max<int64>(Hit.BaseP2, 0) + ScaleTerm(Hit.SourceADP2, Hit.ADRatioBp)
        + ScaleTerm(Hit.SourceAPP2, Hit.APRatioBp)
        + ScaleTerm(AdaptiveStatP2, Hit.AdaptiveRatioBp);
    return ScaleTerm(FMath::Max<int64>(Raw, 0), Scale);
}

bool ALDCombatDirector::BagIsEnemyOf(const FLDCombatBag& Bag, const FGuid& Source) const
{
    if (!Source.IsValid() || Source == Bag.Payload.SourceEntity) { return false; }
    FGuid BagGroup, SourceGroup;
    const bool bBag = ActingGroup(Bag.Payload.SourceEntity, BagGroup), bSource = ActingGroup(Source, SourceGroup);
    return bBag == bSource && (!bBag || BagGroup == SourceGroup);
}

bool ALDCombatDirector::PlaceBag(FLDBodyCombatState& Body, const FLDCombatHit& Payload, const FLDDemoEffectDefinition& Effect,
    const FLDDemoEffectDefinition* Fire, const FVector& Centre, int64 NowMs, FString& Error)
{
    UWorld* World = GetWorld();
    if (!World) { Error = TEXT("A bag needs the authority world."); return false; }
    const auto Cm = [](const FLDDemoRational& Value) { return Value.Denominator > 0 ? double(Value.Numerator) / Value.Denominator : 0.; };
    FLDCombatBag Bag;
    Bag.Payload = Payload;
    Bag.ObservedKey = FGuid::NewGuid();
    Bag.CentreCm = Centre;
    Bag.BodyRadiusCm = Cm(Effect.DistanceCm);
    Bag.BlastRadiusCm = Cm(Effect.RadiusCm);
    Bag.WallHalfWidthCm = Cm(Effect.ExtraDistanceCm) * .5;
    Bag.FireFieldMs = Fire && Fire->GroundKind == uint8(ELDGroundStateKind::Fire) ? Fire->DurationMs : 0;
    Bag.CreatedSerial = ++NextAreaSerial;
    // Life: base + per level above the first, at the caster's level now; a dynamite bag (B) keeps RatioBp of it.
    const int64 Level = FMath::Max(GrowthLevelOf(Body), 1);
    int64 HpP2 = Effect.BaseP2 + Effect.LimitP2 * (Level - 1);
    if (Effect.RatioBp > 0) { HpP2 = HpP2 / 10000 * Effect.RatioBp + HpP2 % 10000 * Effect.RatioBp / 10000; }
    Bag.HpP2 = FMath::Max<int64>(HpP2, 1);
    if (Bag.BodyRadiusCm <= 0 || Bag.BlastRadiusCm <= 0 || !LDMatchLifecycle::AddDurationMs(NowMs, Effect.DurationMs, Bag.ExpiresAtMs, Error))
    {
        if (Error.IsEmpty()) { Error = TEXT("A bag needs a body, a blast and a life."); }
        return false;
    }
    // One skill keeps Count live bags (A: two), Y12 one more per copy; the oldest goes quietly.
    int32 Allowed = FMath::Max(Effect.Count, 1);
    TArray<FLDPassiveEffectRef> Stalls;
    if (!CollectPassiveEffects(Body.AttributeProjection, ELDDemoEffectOp::DoubleStall, Stalls, Error)) { return false; }
    Allowed += Stalls.Num();
    TArray<int32> Owned;
    for (int32 Index = 0; Index < Bags.Num(); ++Index)
    {
        if (Bags[Index].Payload.SourceEntity == Body.Victim.EntityId && Bags[Index].Payload.Source == Payload.Source) { Owned.Add(Index); }
    }
    for (int32 Drop = Owned.Num() - Allowed; Drop >= 0; --Drop) { RemoveBag(Owned[Drop]); }
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Params.ObjectFlags |= RF_Transient;
    ALDCombatBagBlocker* Blocker = World->SpawnActor<ALDCombatBagBlocker>(Params);
    if (!Blocker) { Error = TEXT("The bag's blocker could not be spawned."); return false; }
    Blocker->Shape(Centre, FVector2D(1, 0), Bag.BodyRadiusCm, Bag.BodyRadiusCm, ALDCombatBagBlocker::HeightCm);
    Bag.Blocker = Blocker;
    // A1: a wall to the newest other live bag of the same skill, as long as both stand.
    if (Bag.WallHalfWidthCm > 0)
    {
        const FLDCombatBag* Partner = nullptr;
        for (const FLDCombatBag& Other : Bags)
        {
            if (Other.Payload.SourceEntity == Body.Victim.EntityId && Other.Payload.Source == Payload.Source
                && (!Partner || Other.CreatedSerial > Partner->CreatedSerial)) { Partner = &Other; }
        }
        if (Partner && FVector::Dist2D(Partner->CentreCm, Centre) > Partner->BodyRadiusCm + Bag.BodyRadiusCm)
        {
            const FVector Span = Centre - Partner->CentreCm;
            ALDCombatBagBlocker* Wall = World->SpawnActor<ALDCombatBagBlocker>(Params);
            if (!Wall) { Blocker->Destroy(); Error = TEXT("The bag wall's blocker could not be spawned."); return false; }
            Wall->Shape((Partner->CentreCm + Centre) * .5, FVector2D(Span.X, Span.Y), Span.Size2D() * .5, Bag.WallHalfWidthCm,
                ALDCombatBagBlocker::HeightCm);
            Bag.WallTo = Partner->ObservedKey;
            Bag.WallKey = FGuid::NewGuid();
            Bag.WallBlocker = Wall;
        }
    }
    UE_LOG(LogLDCombat, Log, TEXT("LD_BAG t=%lld owner=%s via=%s centre=%s hp=%lld blast=%.0f until=%lld wall=%d"), NowMs,
        *LDLog::Id(Body.Victim.EntityId), *LDLog::Source(Payload.Source), *Centre.ToCompactString(), Bag.HpP2, Bag.BlastRadiusCm,
        Bag.ExpiresAtMs, Bag.WallKey.IsValid() ? 1 : 0);
    Bags.Add(MoveTemp(Bag));
    return true;
}

void ALDCombatDirector::RemoveBag(int32 Index)
{
    if (!Bags.IsValidIndex(Index)) { return; }
    const FGuid Key = Bags[Index].ObservedKey;
    if (AActor* Blocker = Bags[Index].Blocker.Get()) { Blocker->Destroy(); }
    if (AActor* Wall = Bags[Index].WallBlocker.Get()) { Wall->Destroy(); }
    Bags.RemoveAt(Index);
    // A wall needs both of its bags.
    for (FLDCombatBag& Other : Bags)
    {
        if (Other.WallTo != Key) { continue; }
        if (AActor* Wall = Other.WallBlocker.Get()) { Wall->Destroy(); }
        Other.WallBlocker.Reset();
        Other.WallTo.Invalidate();
        Other.WallKey.Invalidate();
    }
}

void ALDCombatDirector::RemoveBags(TFunctionRef<bool(const FLDCombatBag&)> Which)
{
    for (int32 Index = Bags.Num() - 1; Index >= 0; --Index)
    {
        if (Which(Bags[Index])) { RemoveBag(Index); }
    }
}

void ALDCombatDirector::DamageBag(FLDCombatBag& Bag, const FLDCombatHit& Hit, int64 NowMs)
{
    if (Bag.HpP2 <= 0) { return; }
    const int64 Raw = RawBagDamageP2(Hit);
    Bag.HpP2 -= FMath::Min(Raw, Bag.HpP2);
    UE_LOG(LogLDCombat, Log, TEXT("LD_BAG_HIT t=%lld owner=%s by=%s via=%s damage=%lld hp=%lld"), NowMs, *LDLog::Id(Bag.Payload.SourceEntity),
        *LDLog::Id(Hit.SourceEntity), *LDLog::Source(Hit.Source), Raw, Bag.HpP2);
}

bool ALDCombatDirector::AreaSees(const FVector& Anchor, const FVector& Point, bool& bSees, FString& Error) const
{
    bSees = false;
    const ILDLifeAuthority* Mode = Authority();
    const UWorld* CurrentWorld = GetWorld();
    const ULDMapRegistry* Registry = CurrentWorld ? CurrentWorld->GetSubsystem<ULDMapRegistry>() : nullptr;
    if (!Mode || !Mode->GetVisionAuthority() || !Registry)
    {
        Error = TEXT("An area reaching a bag requires the observation owner and the registered world.");
        return false;
    }
    const ELDSightResult Sight = Mode->GetVisionAuthority()->TraceSight(Anchor, Point, Registry->Revision());
    if (Sight == ELDSightResult::Visible) { bSees = true; }
    else if (Sight != ELDSightResult::Blocked && Sight != ELDSightResult::OutOfRange)
    {
        Error = TEXT("Area sight failed on a bag.");
        return false;
    }
    return true;
}

bool ALDCombatDirector::HitBagsInCircle(const FVector& Centre, double RadiusCm, const FLDCombatHit& Payload, int64 NowMs, FString& Error)
{
    for (FLDCombatBag& Bag : Bags)
    {
        if (!BagIsEnemyOf(Bag, Payload.SourceEntity) || FVector::Dist2D(Bag.CentreCm, Centre) > RadiusCm + Bag.BodyRadiusCm) { continue; }
        bool bSees = false;
        if (!AreaSees(Centre, Bag.CentreCm, bSees, Error)) { return false; }
        if (bSees) { DamageBag(Bag, Payload, NowMs); }
    }
    return true;
}

bool ALDCombatDirector::HitBagsInCone(const FVector& Origin, const FVector& Direction, double RangeCm, double AngleDegrees,
    const FLDCombatHit& Payload, int64 NowMs, FString& Error)
{
    const double CosHalf = FMath::Cos(FMath::DegreesToRadians(AngleDegrees * .5));
    for (FLDCombatBag& Bag : Bags)
    {
        const FVector Offset(Bag.CentreCm.X - Origin.X, Bag.CentreCm.Y - Origin.Y, 0);
        if (!BagIsEnemyOf(Bag, Payload.SourceEntity) || Offset.Size() > RangeCm + Bag.BodyRadiusCm) { continue; }
        if (!Offset.IsNearlyZero() && FVector::DotProduct(Direction.GetSafeNormal2D(), Offset.GetSafeNormal()) < CosHalf) { continue; }
        bool bSees = false;
        if (!AreaSees(Origin, Bag.CentreCm, bSees, Error)) { return false; }
        if (bSees) { DamageBag(Bag, Payload, NowMs); }
    }
    return true;
}

int32 ALDCombatDirector::FirstBagOnSegment(const FGuid& Source, const FVector& From, const FVector& To, double RadiusCm,
    double& OutFraction) const
{
    int32 First = INDEX_NONE;
    OutFraction = 1;
    const FVector2D A(From.X, From.Y), B(To.X, To.Y);
    const FVector2D D = B - A;
    const double Length2 = D.SizeSquared();
    for (int32 Index = 0; Index < Bags.Num(); ++Index)
    {
        const FLDCombatBag& Bag = Bags[Index];
        if (Bag.HpP2 <= 0 || !BagIsEnemyOf(Bag, Source)) { continue; }
        // The first contact of a circle of RadiusCm moving A -> B with the bag's circle.
        const FVector2D C(Bag.CentreCm.X, Bag.CentreCm.Y);
        const double Reach = RadiusCm + Bag.BodyRadiusCm;
        const FVector2D F = A - C;
        double Entry = 0;
        if (F.SizeSquared() <= Reach * Reach) { Entry = 0; }
        else
        {
            if (Length2 <= 0) { continue; }
            const double b = 2 * FVector2D::DotProduct(F, D), c = F.SizeSquared() - Reach * Reach;
            const double Disc = b * b - 4 * Length2 * c;
            if (Disc < 0) { continue; }
            Entry = (-b - FMath::Sqrt(Disc)) / (2 * Length2);
            if (Entry < 0 || Entry > 1) { continue; }
        }
        if (First == INDEX_NONE || Entry < OutFraction)
        {
            First = Index;
            OutFraction = Entry;
        }
    }
    return First;
}

bool ALDCombatDirector::AdvanceBags(int64 NowMs, FString& Error)
{
    for (int32 Index = 0; Index < Bags.Num();)
    {
        const FLDCombatBag& Bag = Bags[Index];
        if (Bag.HpP2 > 0 && Bag.ExpiresAtMs > NowMs) { ++Index; continue; }
        // The blast is copied out before the bag goes: a blast may knock out a neighbour, which blows on the next pass.
        const FLDCombatBag Blowing = Bag;
        RemoveBag(Index);
        if (!ExplodeBag(Blowing, NowMs, Error)) { return false; }
    }
    return true;
}

bool ALDCombatDirector::ExplodeBag(const FLDCombatBag& Bag, int64 NowMs, FString& Error)
{
    TArray<FLDCombatBodyRef> Targets;
    if (!CollectAreaTargets(Bag.CentreCm, Bag.BlastRadiusCm, FindBody(Bag.Payload.SourceBody), Bag.Payload.SourceEntity, Targets, Error))
    { return false; }
    for (const FLDCombatBodyRef& Ref : Targets)
    {
        FLDCombatHit Hit = Bag.Payload;
        Hit.HitId = FGuid::NewGuid();
        Hit.Target = Ref;
        // The blast pushes away from the bag (B1).
        if (Hit.KnockbackCm > 0 && !Hit.bKnockbackPull) { Hit.bKnockbackFromAnchor = true; Hit.KnockbackAnchorCm = Bag.CentreCm; }
        PendingHits.Add(MoveTemp(Hit));
    }
    if (!HitBagsInCircle(Bag.CentreCm, Bag.BlastRadiusCm, Bag.Payload, NowMs, Error)) { return false; }
    UE_LOG(LogLDCombat, Log, TEXT("LD_BAG_BLAST t=%lld owner=%s via=%s centre=%s knocked_out=%d targets=%d fire_ms=%lld"), NowMs,
        *LDLog::Id(Bag.Payload.SourceEntity), *LDLog::Source(Bag.Payload.Source), *Bag.CentreCm.ToCompactString(), Bag.HpP2 <= 0 ? 1 : 0,
        Targets.Num(), Bag.FireFieldMs);
    // B2: a fire field where it blew, owned by the bag's owner.
    if (Bag.FireFieldMs > 0)
    {
        FLDElementApplier FieldOwner;
        FieldOwner.SourceEntity = Bag.Payload.SourceEntity;
        FieldOwner.SourceBody = Bag.Payload.SourceBody;
        FieldOwner.CreditOwner = Bag.Payload.CreditOwner;
        FieldOwner.Source = Bag.Payload.Source;
        FieldOwner.Level = FMath::Max(Bag.Payload.ElementLevel, 1);
        FieldOwner.SourceAPP2 = Bag.Payload.ElementAPP2;
        FieldOwner.EffectId = Bag.Payload.EffectId;
        if (!SpawnGround(ELDGroundStateKind::Fire, FieldOwner, Bag.CentreCm, NowMs, Error, 0, Bag.FireFieldMs)) { return false; }
    }
    return true;
}
