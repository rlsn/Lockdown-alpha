#include "LDDemoContentJson.h"

#include "LDDemoContentMath.h"
#include "../Region/LDMapGeometryJson.h"

namespace LDDemoContent
{
namespace
{
FExact StatBonus(const TArray<FLDDemoStatBonus>& Bonuses, int32 Stat, FNumbers& N)
{
    for (const auto& Bonus : Bonuses)
    {
        if (int32(Bonus.Stat) == Stat)
        {
            if (Bonus.Stat == ELDDemoBonusStat::AttackRangeCmFlat)
            {
                return N.Value(Bonus.Cm);
            }
            const bool Ratio = Bonus.Stat == ELDDemoBonusStat::AttackSpeedBonusRatio
                || Bonus.Stat == ELDDemoBonusStat::MoveSpeedBonusRatio || Bonus.Stat == ELDDemoBonusStat::CritChanceBonusRatio;
            return N.Value(Bonus.Amount, Ratio ? 10000 : 100);
        }
    }
    return N.Value(0);
}

FExact ChoiceBonus(const FLDDemoContentDefinitions& D, int32 Stat, bool HPRegen, FNumbers& N)
{
    FExact Attribute = N.Value(0);
    for (const auto& Row : D.Attributes)
    {
        Attribute = N.Max(Attribute, StatBonus(Row.Stats, Stat, N));
    }
    // Every legal choice of the match (MaxChoices) goes toward this one coordinate.
    const int32 Choices = FMath::Max(D.Progression.MaxChoices, 0);
    TArray<FExact> Best;
    Best.Reserve(Choices + 1);
    for (int32 Budget = 0; Budget <= Choices; ++Budget)
    {
        Best.Add(N.Mul(Attribute, N.Value(Budget)));
    }
    for (const auto& Ability : D.Abilities)
    {
        if (Ability.Kind != ELDDemoAbilityKind::Passive)
        {
            continue;
        }
        TArray<FExact> Next = Best;
        for (int32 Budget = 0; Budget <= Choices; ++Budget)
        {
            for (const auto& Rank : Ability.Ranks)
            {
                if (Rank.Rank > Budget) { continue; }
                FExact Bonus = N.Value(0);
                for (const auto& Effect : Rank.Effects)
                {
                    if (Effect.GetOp() == ELDDemoEffectOp::StatBonus)
                    {
                        Bonus = N.Add(Bonus, StatBonus(Effect.Stats, Stat, N));
                    }
                    else if (HPRegen && Effect.GetOp() == ELDDemoEffectOp::OutOfCombatRegen)
                    {
                        Bonus = N.Add(Bonus, N.Value(Effect.HPPerSec));
                    }
                }
                Next[Budget] = N.Max(Next[Budget], N.Add(Best[Budget - Rank.Rank], Bonus));
            }
        }
        Best = MoveTemp(Next);
    }
    return Best[Choices];
}

FExact EquipmentBonus(const FLDDemoContentDefinitions& D, int32 Stat, FNumbers& N)
{
    FExact Best = N.Value(0);
    for (const auto& Row : D.Equipment)
    {
        Best = N.Max(Best, StatBonus(Row.Stats, Stat, N));
    }
    return N.Mul(Best, N.Value(2));
}

bool BuildEnvelope(const FLDDemoContentDefinitions& D, FNumbers& N, FCheck& Check)
{
    const auto& P = D.Parameters;
    struct FBound { const TCHAR* Name; const FLDDemoP2Bounds* Bounds; ELDDemoBonusStat Stat; };
    for (const auto& Row : {
        FBound{TEXT("MaxHP"), &P.MaxHP, ELDDemoBonusStat::MaxHPFlat}, {TEXT("MaxMP"), &P.MaxMP, ELDDemoBonusStat::MaxMPFlat},
        {TEXT("AD"), &P.AD, ELDDemoBonusStat::ADFlat}, {TEXT("AP"), &P.AP, ELDDemoBonusStat::APFlat},
        {TEXT("Armor"), &P.Armor, ELDDemoBonusStat::ArmorFlat}, {TEXT("MagicResist"), &P.MagicResist, ELDDemoBonusStat::MagicResistFlat}})
    {
        const FExact Base = N.Add(N.Value(Row.Bounds->BaseP2, 100), N.Mul(N.Value(9), N.Value(Row.Bounds->PerLevelP2, 100)));
        const FExact Max = N.Add(Base, N.Add(ChoiceBonus(D, int32(Row.Stat), false, N), EquipmentBonus(D, int32(Row.Stat), N)));
        if (!Check.Require(N.LE(Max, N.Value(Row.Bounds->MaxP2, 100)), Row.Name,
            TEXT("a legal MaxChoices/two-item build exceeds its bound")))
        {
            return false;
        }
    }
    for (const auto& Pair : {TPair<const TCHAR*, const FLDDemoRateBounds*>(TEXT("HPRegen"), &P.HPRegen), {TEXT("MPRegen"), &P.MPRegen}})
    {
        const FExact Base = N.Add(N.Value(Pair.Value->Base), N.Mul(N.Value(9), N.Value(Pair.Value->PerLevel)));
        const FExact Max = N.Add(Base, ChoiceBonus(D, -1, Pair.Value == &P.HPRegen, N));
        if (!Check.Require(N.LE(Max, N.Value(Pair.Value->Max)), Pair.Key, TEXT("regeneration build envelope exceeds bound")))
        {
            return false;
        }
    }
    const int32 Haste = int32(ELDDemoBonusStat::CooldownHasteFlat);
    const FExact HasteMax = N.Add(N.Value(P.CooldownHasteBaseP2, 100),
        N.Add(ChoiceBonus(D, Haste, false, N), EquipmentBonus(D, Haste, N)));
    const int32 Range = int32(ELDDemoBonusStat::AttackRangeCmFlat);
    const FExact RangeMax = N.Add(N.Value(P.AttackRangeCm.Base),
        N.Add(ChoiceBonus(D, Range, false, N), EquipmentBonus(D, Range, N)));
    if (!Check.Require(N.LE(HasteMax, N.Value(P.CooldownHasteMaxP2, 100)), TEXT("CooldownHaste"), TEXT("build envelope exceeds bound"))
        || !Check.Require(N.LE(RangeMax, N.Value(P.AttackRangeCm.Max)), TEXT("AttackRange"), TEXT("build envelope exceeds bound")))
    {
        return false;
    }
    for (ELDDemoBonusStat Stat : {ELDDemoBonusStat::AttackSpeedBonusRatio, ELDDemoBonusStat::MoveSpeedBonusRatio,
        ELDDemoBonusStat::CritChanceBonusRatio})
    {
        FExact Bonus = N.Add(ChoiceBonus(D, int32(Stat), false, N), EquipmentBonus(D, int32(Stat), N));
        FExact Max, Limit;
        if (Stat == ELDDemoBonusStat::CritChanceBonusRatio)
        {
            Max = N.Add(N.Value(P.CritChanceBaseBp, 10000), Bonus);
            Limit = N.Value(1);
        }
        else
        {
            const FLDDemoRationalBounds& Bound = Stat == ELDDemoBonusStat::AttackSpeedBonusRatio ? P.AttackSpeed : P.MoveSpeedCmPerSec;
            if (Stat == ELDDemoBonusStat::AttackSpeedBonusRatio)
            {
                Bonus = N.Add(Bonus, N.Mul(N.Value(9), N.Value(P.AttackSpeedPerLevelBp, 10000)));
            }
            Max = N.Mul(N.Value(Bound.Base), N.Add(N.Value(1), Bonus));
            Limit = N.Value(Bound.Max);
        }
        if (!N.NativePair(Max, TEXT("ratio build envelope"))
            || !Check.Require(N.LE(Max, Limit), TEXT("ratio build envelope"), TEXT("a legal source combination loses its bonus to the cap")))
        {
            return false;
        }
    }
    const auto CheckEffect = [&N, &P](const FLDDemoEffectDefinition& Effect, const FString& Path)
    {
        if (Effect.GetOp() != ELDDemoEffectOp::Damage && Effect.GetOp() != ELDDemoEffectOp::Shield)
        {
            return true;
        }
        int64 Base, AP, AD, Sum;
        if (!N.Mul64(Effect.BaseP2, 10000, Base, Path) || !N.Mul64(P.AP.MaxP2, Effect.APRatioBp, AP, Path)
            || !N.Add64(Base, AP, Sum, Path))
        {
            return false;
        }
        return Effect.GetOp() != ELDDemoEffectOp::Damage
            || (N.Mul64(P.AD.MaxP2, Effect.ADRatioBp, AD, Path) && N.Add64(Sum, AD, Sum, Path));
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
