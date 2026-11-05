#include "LDDemoContentJson.h"

#include "LDDemoContentMath.h"
#include "../Region/LDMapGeometryJson.h"

namespace LDDemoContent
{
namespace
{
// Every attribute clamps to its catalog bounds in the projection (#112 §4), so no source combination is refused; what
// remains to check is that every damage or shield term stays inside int64 at the extreme attributes.
bool BuildEnvelope(const FLDDemoContentDefinitions& D, FNumbers& N, FCheck& Check)
{
    const auto& P = D.Parameters;
    const auto CheckEffect = [&N, &P](const FLDDemoEffectDefinition& Effect, const FString& Path)
    {
        if (Effect.GetOp() == ELDDemoEffectOp::Thorns)
        {
            int64 Base, Armor, Sum, Critical;
            return N.Mul64(Effect.BaseP2, 10000, Base, Path) && N.Mul64(P.Armor.MaxP2, Effect.ArmorRatioBp, Armor, Path)
                && N.Add64(Base, Armor, Sum, Path) && N.Mul64(Sum, Effect.CriticalMultiplierBp, Critical, Path);
        }
        const ELDDemoEffectOp Op = Effect.GetOp();
        const bool bDamage = Op == ELDDemoEffectOp::Damage || Op == ELDDemoEffectOp::EmpowerBasic || Op == ELDDemoEffectOp::AmbushDamage
            || Op == ELDDemoEffectOp::Dot || Op == ELDDemoEffectOp::MobilityPunish || Op == ELDDemoEffectOp::ZoneDamage
            || Op == ELDDemoEffectOp::ShieldPulse || Op == ELDDemoEffectOp::BreachStacks;
        if (!bDamage && Op != ELDDemoEffectOp::Shield && Op != ELDDemoEffectOp::BasicRider && Op != ELDDemoEffectOp::ZoneShield)
        {
            return true;
        }
        int64 Base, AP, AD, Sum;
        if (!N.Mul64(Effect.BaseP2, 10000, Base, Path) || !N.Mul64(P.AP.MaxP2, Effect.APRatioBp, AP, Path)
            || !N.Add64(Base, AP, Sum, Path))
        {
            return false;
        }
        int64 HP;
        return !bDamage
            || (N.Mul64(P.AD.MaxP2, Effect.ADRatioBp, AD, Path) && N.Add64(Sum, AD, Sum, Path)
                && N.Mul64(P.MaxHP.MaxP2, Effect.MaxHPRatioBp, HP, Path) && N.Add64(Sum, HP, Sum, Path));
    };
    for (const auto& Ability : D.Abilities)
    {
        for (const auto& Rank : Ability.Ranks)
        {
            for (const auto& Effect : Rank.Effects)
            {
                if (!CheckEffect(Effect, Ability.Id)) { return false; }
            }
        }
    }
    for (const auto& Item : D.Equipment)
    {
        for (const auto& Effect : Item.Active.Rank.Effects)
        {
            if (!CheckEffect(Effect, Item.Id)) { return false; }
        }
        for (const auto& Effect : Item.Passive.Effects)
        {
            if (!CheckEffect(Effect, Item.Id)) { return false; }
        }
    }
    return Check.Good;
}

}

bool ValidateNumbers(const FLDDemoContentDefinitions& D, FCheck& Check)
{
    FNumbers N(Check);
    const auto& P = D.Parameters;
    for (const FLDDemoP2Bounds* Bounds : {&P.MaxHP, &P.MaxMP, &P.AD, &P.AP, &P.Armor, &P.MagicResist})
    {
        int64 Growth, Final;
        if (!N.Mul64(9, Bounds->PerLevelP2, Growth, TEXT("stat bounds"))
            || !N.Add64(Bounds->BaseP2, Growth, Final, TEXT("stat bounds"))
            || !Check.Require(Bounds->MinP2 <= Bounds->BaseP2 && Final <= Bounds->MaxP2,
                TEXT("stat bounds"), TEXT("base/level growth is outside the declared bounds")))
        {
            return false;
        }
    }
    for (const FLDDemoRateBounds* Bounds : {&P.HPRegen, &P.MPRegen})
    {
        const FExact Final = N.Add(N.Value(Bounds->Base), N.Mul(N.Value(9), N.Value(Bounds->PerLevel)));
        if (!Check.Require(N.LE(N.Value(Bounds->Min), N.Value(Bounds->Base)) && N.LE(Final, N.Value(Bounds->Max)),
            TEXT("regeneration bounds"), TEXT("base/level growth is outside the declared bounds")))
        {
            return false;
        }
    }
    if (!Check.Require(P.MaxHP.MinP2 > 0 && P.MaxHP.BaseP2 > 0 && P.AD.BaseP2 > 0,
        TEXT("character"), TEXT("initial HP and attack must be positive"))
        || !Check.Require(P.CooldownHasteBaseP2 <= P.CooldownHasteMaxP2, TEXT("haste"), TEXT("inverted bounds")))
    {
        return false;
    }
    for (const FLDDemoRationalBounds* Bounds : {&P.AttackSpeed, &P.MoveSpeedCmPerSec, &P.AttackRangeCm})
    {
        if (!Check.Require(N.LE(N.Value(Bounds->Min), N.Value(Bounds->Base))
            && N.LE(N.Value(Bounds->Base), N.Value(Bounds->Max)), TEXT("geometry/rate bounds"), TEXT("base outside bounds")))
        {
            return false;
        }
    }
    int64 Interval;
    if (!Check.Require(N.LE(N.Mul(N.Value(2), N.Value(P.CollisionRadiusCm)), N.Value(P.AttackRangeCm.Base)),
            TEXT("basic attack"), TEXT("range requires overlapping bodies"))
        || !N.Interval(P.AttackSpeed.Base, Interval, TEXT("basic attack"))
        || !Check.Require(Interval == D.Character.BasicIntervalMs && P.BasicAttackWindupMs < Interval,
            TEXT("basic attack"), TEXT("derived interval mismatch or invalid windup")))
    {
        return false;
    }
    if (!Check.Require(P.AI.RetreatHPBp < P.AI.ResumeFarmHPBp && P.AI.ResumeFarmHPBp <= P.AI.HighCampEntryHPBp
        && P.AI.LowCampReserveHPBp <= P.AI.HighCampReserveHPBp && P.AI.HighCampReserveHPBp < P.AI.HighCampEntryHPBp
        && P.AI.RetryInitialBackoffMs <= P.AI.RetryMaxBackoffMs, TEXT("AI"), TEXT("inconsistent thresholds/backoff")))
    {
        return false;
    }
    // A whole 0.001 share keeps every P2 maximum an exact 1e-6 P2 per millisecond rate (LDTactical::SettleRegen).
    for (const int64 Bp : {P.OutOfCombatMaxHPRegenBp, P.OutOfCombatMaxMPRegenBp})
    {
        if (!Check.Require(P.OutOfCombatRegenDelayMs > 0 && Bp > 0 && Bp <= 10000 && Bp % 10 == 0, TEXT("REGEN"),
            TEXT("out-of-combat recovery needs a positive delay and a positive multiple of 0.001 of the maximum per second")))
        {
            return false;
        }
    }
    const FLDDemoProgressionDefinition& Opening = D.Progression;
    int32 Passives = 0;
    for (const auto& Ability : D.Abilities) { Passives += Ability.Kind == ELDDemoAbilityKind::Passive ? 1 : 0; }
    for (const FString& Id : Opening.OpeningAttributeIds)
    {
        if (!Check.Require(D.Attributes.ContainsByPredicate([&Id](const FLDDemoAttributeDefinition& Row) { return Row.Id == Id; }),
            TEXT("progression.opening_offer"), TEXT("opening offer names an unknown attribute card")))
        {
            return false;
        }
    }
    if (!Check.Require(Opening.OpeningAttributeCount >= 0 && Opening.OpeningPassiveCount >= 0
        && Opening.OpeningAttributeCount + Opening.OpeningPassiveCount == 3
        && Opening.OpeningAttributeCount <= Opening.OpeningAttributeIds.Num() && Opening.OpeningPassiveCount <= Passives,
        TEXT("progression.opening_offer"), TEXT("opening offer cannot draw three distinct cards from its attributes and passives")))
    {
        return false;
    }
    int64 TotalXP = 0;
    if (!Check.Require(D.Progression.XPThresholds[0] == 0, TEXT("progression"), TEXT("level one starts at zero XP")))
    {
        return false;
    }
    for (int32 I = 0; I < 9; ++I)
    {
        int64 Growth, Increment;
        if (!N.Mul64(I, P.XPIncreasePerLevel, Growth, TEXT("XP"))
            || !N.Add64(P.XPBaseToNext, Growth, Increment, TEXT("XP"))
            || !N.Add64(TotalXP, Increment, TotalXP, TEXT("XP"))
            || !Check.Require(D.Progression.XPToNext[I] == Increment && D.Progression.XPThresholds[I + 1] == TotalXP,
                TEXT("progression"), TEXT("threshold/derived increment does not match the linear XP formula")))
        {
            return false;
        }
    }
    int64 Growth, Reward;
    if (!N.Mul64(9, P.PlayerXPPerLevel, Growth, TEXT("player XP reward"))
        || !N.Add64(P.PlayerXPBase, Growth, Reward, TEXT("player XP reward"))
        || !N.Mul64(9, P.PlayerGoldPerLevel, Growth, TEXT("player gold reward"))
        || !N.Add64(P.PlayerGoldBase, Growth, Reward, TEXT("player gold reward")))
    {
        return false;
    }
    for (const auto& Item : D.Equipment)
    {
        int64 SaleNumerator, PurchaseEnvelope;
        if (!N.Mul64(Item.BuyPrice, P.SellRatioBp, SaleNumerator, Item.Id)
            || !N.Add64(P.StartingGold, Item.BuyPrice, PurchaseEnvelope, Item.Id)
            || !Check.Require(Item.SellGold == SaleNumerator / 10000 && Item.SellGold < Item.BuyPrice,
                Item.Id, TEXT("derived sell gold differs or permits arbitrage")))
        {
            return false;
        }
    }
    if (!Check.Require(D.Monsters[0].Tier != D.Monsters[1].Tier, TEXT("camps"), TEXT("both difficulty tiers are required")))
    {
        return false;
    }
    for (const auto& Monster : D.Monsters)
    {
        int64 RewardTotal;
        if (!N.Interval(Monster.AttackSpeed, Interval, Monster.Id)
            || !Check.Require(Interval == Monster.AttackIntervalMs && Monster.AttackWindupMs < Interval,
                Monster.Id, TEXT("derived attack interval mismatch or invalid windup"))
            || !Check.Require(N.LE(N.Add(N.Value(Monster.CollisionRadiusCm), N.Value(P.CollisionRadiusCm)), N.Value(Monster.AttackRangeCm))
                && N.LT(N.Value(Monster.AttackRangeCm), N.Value(Monster.LeashRadiusCm)), Monster.Id, TEXT("invalid collision/range/leash geometry"))
            || !Check.Require(Monster.PerceptionRadiusCm.Numerator == Monster.LeashRadiusCm.Numerator
                && Monster.PerceptionRadiusCm.Denominator == Monster.LeashRadiusCm.Denominator,
                Monster.Id, TEXT("derived perception radius differs from leash"))
            || !N.Mul64(Monster.CountPerCamp, Monster.RewardXP, RewardTotal, Monster.Id)
            || !N.Mul64(Monster.CountPerCamp, Monster.RewardGold, RewardTotal, Monster.Id))
        {
            return false;
        }
    }
    return BuildEnvelope(D, N, Check);
}

bool ValidateWorld(const FValue& Value, const FLDDemoContentDefinitions& D, const FLDDemoContentBundle& Bundle, FCheck& Check)
{
    FLDDemoWorldGeometry Geometry;
    FLDDemoContentDigest Source, Schema;
    return LDDemoWorld::Project(Value, D, Geometry, Check)
        && Hash(Bundle.WorldSource, Source, Check, TEXT("world author"))
        && Hash(Bundle.WorldAuthorSchema, Schema, Check, TEXT("world schema"))
        && Check.Require(Source == Geometry.SourceDigest && Schema == Geometry.AuthorSchemaDigest,
            TEXT("world provenance"), TEXT("world author/schema file bytes differ from the trusted payload"));
}
}
