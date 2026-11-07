#include "LDDemoContentJson.h"

#include "LDDemoContentMath.h"
#include "../Region/LDMapGeometryJson.h"

namespace LDDemoContent
{
namespace
{
// Every attribute clamps to its catalog bounds in the projection (#112 §4), so no source combination is refused; what
// remains to check is that every damage or shield term stays inside int64 at the extreme attributes.
// 112 §7: Stats times 1 + UpgradeStatBonusBp, floored per field precision (P2/R4 storage, whole centimetres of range).
bool UpgradedStatsMatch(const FLDDemoEquipmentDefinition& Item, int64 BonusBp, FNumbers& N)
{
    if (Item.UpgradedStats.Num() != Item.Stats.Num())
    {
        return false;
    }
    for (int32 I = 0; I < Item.Stats.Num(); ++I)
    {
        const FLDDemoStatBonus& Base = Item.Stats[I];
        const FLDDemoStatBonus& Up = Item.UpgradedStats[I];
        if (Base.Stat != Up.Stat)
        {
            return false;
        }
        if (Base.Stat == ELDDemoBonusStat::AttackRangeCmFlat)
        {
            int64 Scaled, Whole;
            if (Base.Cm.Denominator <= 0 || !N.Mul64(Base.Cm.Numerator, 10000 + BonusBp, Scaled, Item.Id)
                || !N.Mul64(Base.Cm.Denominator, 10000, Whole, Item.Id))
            {
                return false;
            }
            const int64 Expected = Scaled >= 0 ? Scaled / Whole : -((-Scaled + Whole - 1) / Whole);
            if (Up.Amount != 0 || Up.Cm.Denominator != 1 || Up.Cm.Numerator != Expected)
            {
                return false;
            }
            continue;
        }
        int64 Scaled;
        if (!N.Mul64(Base.Amount, 10000 + BonusBp, Scaled, Item.Id))
        {
            return false;
        }
        const int64 Expected = Scaled >= 0 ? Scaled / 10000 : -((-Scaled + 9999) / 10000);
        if (Up.Amount != Expected || Up.Cm.Numerator != 0 || Up.Cm.Denominator != 1)
        {
            return false;
        }
    }
    return true;
}

bool BuildEnvelope(const FLDDemoContentDefinitions& D, FNumbers& N, FCheck& Check)
{
    const auto& P = D.Parameters;
    const auto CheckEffect = [&N, &P](const FLDDemoEffectDefinition& Effect, const FString& Path)
    {
        if (Effect.GetOp() == ELDDemoEffectOp::Thorns)
        {
            int64 Base, Armor, Health, Sum, Total, Critical;
            return N.Mul64(Effect.BaseP2, 10000, Base, Path) && N.Mul64(P.Armor.MaxP2, Effect.ArmorRatioBp, Armor, Path)
                && N.Mul64(P.MaxHP.MaxP2, Effect.MaxHPRatioBp, Health, Path)
                && N.Add64(Base, Armor, Sum, Path) && N.Add64(Sum, Health, Total, Path)
                && N.Mul64(Total, Effect.CriticalMultiplierBp, Critical, Path);
        }
        const ELDDemoEffectOp Op = Effect.GetOp();
        const bool bDamage = Op == ELDDemoEffectOp::Damage || Op == ELDDemoEffectOp::EmpowerBasic || Op == ELDDemoEffectOp::ZoneDamage;
        if (!bDamage && Op != ELDDemoEffectOp::Shield)
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
        for (const auto& Effect : Item.Passive.Effects)
        {
            if (!CheckEffect(Effect, Item.Id)) { return false; }
        }
        for (const auto& Effect : Item.Passive.UpgradedEffects)
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
    // D27 (#79): the leader circle always contains the leader, the minimum level exists, and the cap is reachable.
    {
        const auto& Radius = P.LeaderCircleRadiusCm;
        const auto& Offset = P.LeaderCircleMaxOffsetCm;
        const bool bContained = Radius.Denominator > 0 && Offset.Denominator > 0 && Offset.Numerator > 0
            && double(Offset.Numerator) / double(Offset.Denominator) <= double(Radius.Numerator) / double(Radius.Denominator);
        if (!Check.Require(bContained && P.LeaderMinLevel >= 1 && P.LeaderMinLevel <= D.Progression.MaxLevel
            && P.LeaderBountyGoldPerRefresh > 0 && P.LeaderBountyGoldPerRefresh <= P.LeaderBountyGoldMax, TEXT("LEADER"),
            TEXT("the circle must contain the leader, the level must exist and the cap must be reachable")))
        {
            return false;
        }
    }
    // D26 (#79): every drop is announced after the match starts, and one announcement ends before the next begins.
    if (!Check.Require(P.AirdropAnnounceLeadMs > 0 && P.AirdropAnnounceLeadMs <= P.AirdropFirstLandMs
        && P.AirdropAnnounceLeadMs < P.AirdropIntervalMs, TEXT("AIRDROP"),
        TEXT("the announcement lead must fit before the first landing and inside one interval")))
    {
        return false;
    }
    // D21 (#79): the dorm's fast recovery uses the same exact integration as out-of-combat recovery.
    for (const int64 Bp : {P.DormMaxHPRegenBp, P.DormMaxMPRegenBp})
    {
        if (!Check.Require(P.DormRegenDelayMs > 0 && Bp > 0 && Bp <= 10000 && Bp % 10 == 0, TEXT("DORM"),
            TEXT("dorm recovery needs a positive delay and a positive multiple of 0.001 of the maximum per second")))
        {
            return false;
        }
    }
    const FLDDemoProgressionDefinition& Opening = D.Progression;
    int32 Actives = 0, Passives = 0;
    for (const auto& Ability : D.Abilities)
    {
        if (Ability.Draftable)
        {
            (Ability.Kind == ELDDemoAbilityKind::Passive ? Passives : Actives) += 1;
        }
    }
    if (!Check.Require(Opening.OpeningActiveCount >= 0 && Opening.OpeningPassiveCount >= 0
        && Opening.OpeningActiveCount + Opening.OpeningPassiveCount == 3
        && Opening.OpeningActiveCount <= Actives && Opening.OpeningPassiveCount <= Passives,
        TEXT("progression.opening_offer"), TEXT("opening offer cannot draw three distinct new draftable skills")))
    {
        return false;
    }
    const auto& Choice = P.Choice;
    if (!Check.Require(Choice.NewActiveFullSlotWeightBp > 0 && Choice.NewActiveFullSlotWeightBp <= 10000
        && Choice.FreeSlotAttributeBp >= 0 && Choice.FreeSlotAttributeBp <= 10000, TEXT("CHOICE"),
        TEXT("the full-slot active weight must lie in (0, 1] and the free-slot attribute chance in [0, 1]")))
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
                Item.Id, TEXT("derived sell gold differs or permits arbitrage"))
            || !Check.Require(UpgradedStatsMatch(Item, P.UpgradeStatBonusBp, N), Item.Id,
                TEXT("derived upgraded stats differ from the floored stat bonus")))
        {
            return false;
        }
    }
    if (!Check.Require(D.Monsters.Num() == 3 && D.Monsters[0].Tier != D.Monsters[1].Tier
        && D.Monsters[0].Tier != ELDDemoCampTier::Boss && D.Monsters[1].Tier != ELDDemoCampTier::Boss
        && D.Monsters[2].Tier == ELDDemoCampTier::Boss && D.Monsters[2].Id == D.Boss.Id, TEXT("camps"),
        TEXT("both camp difficulty tiers and exactly one Boss row are required")))
    {
        return false;
    }
    // D26 (#79): the Boss body is contender-sized, reaches past both radii, sees farther than it hits, winds up inside its
    // interval, stays inside the move speed clamp, and its approval buff is positive temporary ratios only.
    {
        const FLDDemoMonsterDefinition& Body = D.Monsters[2];
        bool bApproval = !D.Boss.ApprovalStats.IsEmpty() && D.Boss.ApprovalDurationMs > 0;
        for (const FLDDemoStatBonus& Bonus : D.Boss.ApprovalStats)
        {
            bApproval &= Bonus.Amount > 0 && (Bonus.Stat == ELDDemoBonusStat::ADBonusRatio || Bonus.Stat == ELDDemoBonusStat::APBonusRatio
                || Bonus.Stat == ELDDemoBonusStat::MoveSpeedBonusRatio || Bonus.Stat == ELDDemoBonusStat::AttackSpeedBonusRatio);
        }
        if (!Check.Require(N.EQ(N.Value(Body.CollisionRadiusCm), N.Value(P.CollisionRadiusCm))
                && N.LE(N.Add(N.Value(Body.CollisionRadiusCm), N.Value(P.CollisionRadiusCm)), N.Value(Body.AttackRangeCm))
                && N.LT(N.Value(Body.AttackRangeCm), N.Value(D.Boss.AggroRadiusCm)), TEXT("boss"), TEXT("invalid Boss collision/range/aggro geometry"))
            || !Check.Require(Body.AttackWindupMs < Body.AttackIntervalMs && D.Boss.AppearAtMs > 0 && D.Boss.AttackerMemoryMs > 0,
                TEXT("boss"), TEXT("invalid Boss timing"))
            || !Check.Require(N.LE(N.Value(P.MoveSpeedCmPerSec.Min), N.Value(Body.MoveSpeedCmPerSec))
                && N.LE(N.Value(Body.MoveSpeedCmPerSec), N.Value(P.MoveSpeedCmPerSec.Max)), TEXT("boss"), TEXT("Boss move speed outside the clamp"))
            || !Check.Require(bApproval, TEXT("boss.approval"), TEXT("the approval buff needs positive temporary ratio stats and a duration")))
        {
            return false;
        }
    }
    for (const auto& Monster : D.Monsters)
    {
        if (Monster.Tier == ELDDemoCampTier::Boss) { continue; }
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
