#include "LDDemoContentJson.h"

#include "LDDemoContentMath.h"
#include "Framework/LDMatchTimeline.h"
#include "World/LDCharms.h"
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
        const ELDDemoEffectOp Op = Effect.GetOp();
        const bool bDamage = Op == ELDDemoEffectOp::Damage || Op == ELDDemoEffectOp::EmpowerBasic || Op == ELDDemoEffectOp::ZoneDamage
            || Op == ELDDemoEffectOp::DashContactDamage || Op == ELDDemoEffectOp::Ledger;
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
    // #166: levels 2..10 and 11..16 have separate fixed-amount growth steps.
    int64 FirstGrowth = 0, LaterGrowth = 0, Factor = 0;
    if (!N.Mul64(FMath::Min(FMath::Max(D.Progression.MaxLevel - 1, 0), 9), P.SkillGrowthPerLevelBp,
            FirstGrowth, TEXT("SKILL.GrowthPerLevel"))
        || !N.Mul64(FMath::Max(D.Progression.MaxLevel - 10, 0), P.SkillGrowthPerLevelAfter10Bp,
            LaterGrowth, TEXT("SKILL.GrowthPerLevelAfter10"))
        || !N.Add64(10000, FirstGrowth, Factor, TEXT("SKILL.GrowthPerLevel"))
        || !N.Add64(Factor, LaterGrowth, Factor, TEXT("SKILL.GrowthPerLevelAfter10")))
    {
        return false;
    }
    const auto Grow = [&N, Factor](int64 AmountP2, int64& Out, const FString& Path)
    {
        int64 Product = 0;
        if (!N.Mul64(AmountP2, Factor, Product, Path)) { return false; }
        Out = Product / 10000;
        return true;
    };
    const auto CheckGrown = [&Grow, &CheckEffect](const FLDDemoEffectDefinition& Effect, const FString& Path)
    {
        FLDDemoEffectDefinition Grown = Effect;
        if (Effect.GetOp() != ELDDemoEffectOp::ManaOnHit && !Grow(Effect.BaseP2, Grown.BaseP2, Path)) { return false; }
        if (Effect.GetOp() == ELDDemoEffectOp::TimedStatBonus)
        {
            for (FLDDemoStatBonus& Stat : Grown.Stats)
            {
                if (!Grow(Stat.Amount, Stat.Amount, Path)) { return false; }
            }
        }
        return CheckEffect(Grown, Path);
    };
    for (const auto& Ability : D.Abilities)
    {
        for (const auto& Effect : Ability.Values.Effects)
        {
            if (!CheckGrown(Effect, Ability.Id)) { return false; }
        }
    }
    // #198 B: a component's fixed amounts grow with its holder's level like an active's.
    for (const auto& Component : D.Components)
    {
        for (const auto& Effect : Component.Effects)
        {
            if (!CheckGrown(Effect, Component.Id)) { return false; }
        }
    }
    for(const auto& Card:D.FinalMods)
    {
        FLDDemoEffectDefinition Damage;
        Damage.Opcode=uint8(ELDDemoEffectOp::Damage);
        Damage.BaseP2=Card.BaseP2;Damage.ADRatioBp=Card.ADRatioBp;Damage.APRatioBp=Card.APRatioBp;
        if(!CheckGrown(Damage,Card.Id)){return false;}
    }
    int64 BurnBase = 0, BurnAP = 0, BurnTotal = 0;
    if (!Grow(P.BurnPerSecP2, BurnBase, TEXT("COMBAT.BurnPerSec")) || !N.Mul64(P.AP.MaxP2, P.BurnAPRatioBp, BurnAP, TEXT("COMBAT.BurnAPRatioPerSec"))
        || !N.Add64(BurnBase, BurnAP / 10000, BurnTotal, TEXT("COMBAT.BurnPerSec")))
    {
        return false;
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
        if (!N.Mul64(D.Progression.MaxLevel - 1, Bounds->PerLevelP2, Growth, TEXT("stat bounds"))
            || !N.Add64(Bounds->BaseP2, Growth, Final, TEXT("stat bounds"))
            || !Check.Require(Bounds->MinP2 <= Bounds->BaseP2 && Final <= Bounds->MaxP2,
                TEXT("stat bounds"), TEXT("base/level growth is outside the declared bounds")))
        {
            return false;
        }
    }
    for (const FLDDemoRateBounds* Bounds : {&P.HPRegen, &P.MPRegen})
    {
        const FExact Final = N.Add(N.Value(Bounds->Base),
            N.Mul(N.Value(D.Progression.MaxLevel - 1), N.Value(Bounds->PerLevel)));
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
    // D27 (#79): the leader circle always contains the leader and the minimum level exists.
    {
        const auto& Radius = P.LeaderCircleRadiusCm;
        const auto& Offset = P.LeaderCircleMaxOffsetCm;
        const bool bContained = Radius.Denominator > 0 && Offset.Denominator > 0 && Offset.Numerator > 0
            && double(Offset.Numerator) / double(Offset.Denominator) <= double(Radius.Numerator) / double(Radius.Denominator);
        if (!Check.Require(bContained && P.LeaderMinLevel >= 1 && P.LeaderMinLevel <= D.Progression.MaxLevel, TEXT("LEADER"),
            TEXT("the circle must contain the leader and the level must exist")))
        {
            return false;
        }
    }
    // #198 outer §5: the drops land at strictly increasing field progress instants, each announced after T=0 and gone
    // unclaimed before the next announcement; the last one is gone before the final field, which has no build reward
    // target. Camp markers reveal inside their segment.
    {
        const int64 TimedFieldMs = P.FieldSegmentMs > 0 && P.FieldSegmentMs <= MAX_int64 / FLDTimelineParameters::TimedSegments
            ? P.FieldSegmentMs * FLDTimelineParameters::TimedSegments : 0;
        bool bDrops = TimedFieldMs > 0 && !P.AirdropLandMs.IsEmpty() && P.AirdropLandMs.Num() <= 6 && P.AirdropAnnounceLeadMs > 0
            && P.AirdropExpireMs > 0 && P.AirdropAnnounceLeadMs <= P.AirdropLandMs[0];
        for (int32 Index = 0; bDrops && Index < P.AirdropLandMs.Num(); ++Index)
        {
            const int64 Land = P.AirdropLandMs[Index];
            const int64 Gone = Land <= TimedFieldMs - P.AirdropExpireMs ? Land + P.AirdropExpireMs : MAX_int64;
            const int64 Next = Index + 1 < P.AirdropLandMs.Num() ? P.AirdropLandMs[Index + 1] - P.AirdropAnnounceLeadMs : TimedFieldMs;
            bDrops = Land > 0 && Gone <= Next;
        }
        if (!Check.Require(bDrops, TEXT("AIRDROP"),
                TEXT("drops must be announced after T=0, leave before the next announcement and before the final field"))
            || !Check.Require(P.CampMarkRevealMs > 0 && P.CampMarkRevealMs < P.FieldSegmentMs, TEXT("CAMPMARK.RevealSec"),
                TEXT("a camp marker must be revealed inside its field segment"))
            || !Check.Require(D.Boss.AppearAtMs > 0 && D.Boss.AppearAtMs < D.Boss.LeaveAtMs && D.Boss.LeaveAtMs <= TimedFieldMs,
                TEXT("boss"), TEXT("the Boss must appear before it leaves, and leave before the final field")))
        {
            return false;
        }
    }
    // #198 charms: distinct kinds per wave, each living inside one wave interval, every effect positive.
    if (!Check.Require(P.CharmPerWave >= 1 && P.CharmPerWave <= LDCharms::KindCount && P.CharmWaveCount >= 1
        && P.CharmLifetimeMs > 0 && P.CharmLifetimeMs <= P.CharmWaveIntervalMs && P.CharmFirstWaveMs > 0
        && P.CharmPickupRadiusCm.Denominator > 0 && P.CharmPickupRadiusCm.Numerator > 0
        && P.CharmHealMaxHPBp > 0 && P.CharmManaMaxMPBp > 0 && P.CharmMoveSpeedBp > 0 && P.CharmMoveSpeedMs > 0, TEXT("CHARM"),
        TEXT("a wave draws distinct kinds, lives inside one interval and every effect is positive")))
    {
        return false;
    }
    // #198: PK sign-up is the last part of every rest build window (R0 and R1-R3), not an extra wait.
    if (!Check.Require(P.RestSignupMs > 0 && P.RestSignupMs <= P.RestBuildMs && P.RestSignupMs <= P.RestOpeningBuildMs,
        TEXT("REST.SignupSec"), TEXT("PK sign-up must fit inside every rest build window")))
    {
        return false;
    }
    // #198 PK ring: the shrink steps are ordered and end before the PK cap, and every ring lies strictly inside the last.
    if (!Check.Require(P.PKRingShrinkStartMs > 0 && P.PKRingShrinkStartMs < P.PKRingMidMs && P.PKRingMidMs < P.PKRingFinalMs
            && P.PKRingFinalMs < P.RestPKMaxMs && P.PKRingPreviewMs > 0 && P.PKRingTickMs > 0,
            TEXT("PK.RingFinalSec"), TEXT("ring shrink steps must be ordered and finish before the PK cap"))
        || !Check.Require(N.LT(N.Value(0), N.Value(P.PKRingFinalRadiusCm))
            && N.LT(N.Value(P.PKRingFinalRadiusCm), N.Value(P.PKRingMidRadiusCm))
            && N.LT(N.Value(P.PKRingMidRadiusCm), N.Value(P.PKArenaRadiusCm)),
            TEXT("PK.RingFinalRadiusCm"), TEXT("ring radii must shrink strictly inside the arena"))
        || !Check.Require(N.LT(N.Value(0), N.Value(P.ThrowRadiusCm)) && N.LT(N.Value(0), N.Value(P.ThrowRangeCm)),
            TEXT("THROW"), TEXT("throw radius and range must be positive")))
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
    // #198: the opening opportunity draws three distinct actives from the opening pool.
    int32 OpeningActives = 0;
    for (const auto& Ability : D.Abilities)
    {
        OpeningActives += Ability.Opening ? 1 : 0;
    }
    for(const auto& Card:D.FinalMods)
    {
        int64 Sum=0;
        if(!N.Add64(Card.DelayMs,Card.DurationMs,Sum,Card.Id)
            ||!N.Add64(Card.WindowMs,Card.IntervalMs,Sum,Card.Id)){return false;}
        if(Card.CooldownCostMs>0)
        {
            if(!N.Add64(D.Character.Dash.Values.CooldownMs,Card.CooldownCostMs,Sum,Card.Id)){return false;}
            for(const auto& Ability:D.Abilities)
            {
                if(Card.GetNativeId()==ELDDemoFinalModId::FinalTraffic
                    &&Ability.Values.Effects.ContainsByPredicate([](const FLDDemoEffectDefinition& Effect){return Effect.GetOp()==ELDDemoEffectOp::Dash;})
                    &&!N.Add64(Ability.Values.CooldownMs,Card.CooldownCostMs,Sum,Card.Id)){return false;}
            }
        }
        if(Card.PulseIntervalMs>0&&!Check.Require(Card.DurationMs>0
            &&(Card.DurationMs-1)/Card.PulseIntervalMs+1<=MAX_int32,Card.Id,TEXT("final pulse count exceeds its runtime domain"))){return false;}
    }
    if (!Check.Require(OpeningActives >= 3, TEXT("abilities"),
        TEXT("the opening opportunity needs three distinct actives in the opening pool")))
    {
        return false;
    }
    if (!Check.Require(P.NpcActiveSkipPoints > 0 && P.NpcActiveSkipPoints < P.NpcActivePoints && P.ActiveSkipPoints > 0,
            TEXT("GROWTH.NpcActiveSkipPoints"), TEXT("declining a purchased offer must cost points overall")))
    {
        return false;
    }
    // #198 points: the opening grant and one grant per level-up to the cap stay inside int64.
    int64 LevelPoints = 0, TotalPoints = 0;
    if (!N.Mul64(D.Progression.MaxLevel - 1, P.PointsPerLevel, LevelPoints, TEXT("GROWTH.PointsPerLevel"))
        || !N.Add64(P.OpeningPoints, LevelPoints, TotalPoints, TEXT("GROWTH.OpeningPoints")))
    {
        return false;
    }
    // #198 B: a level-up whose component roll finds the common pool empty adds GROWTH.EmptyComponentPoolPoints on top.
    int64 PerLevel = 0;
    if (!N.Add64(P.PointsPerLevel, P.EmptyComponentPoolPoints, PerLevel, TEXT("GROWTH.EmptyComponentPoolPoints"))
        || !N.Mul64(D.Progression.MaxLevel - 1, PerLevel, LevelPoints, TEXT("GROWTH.EmptyComponentPoolPoints"))
        || !N.Add64(P.OpeningPoints, LevelPoints, TotalPoints, TEXT("GROWTH.EmptyComponentPoolPoints")))
    {
        return false;
    }
    int64 TotalXP = 0;
    // #198 G: a dismantle surrenders that many distinct common components, so the common pool must hold more than that.
    const int32 CommonComponents = D.Components.FilterByPredicate(
        [](const FLDDemoComponentDefinition& Row) { return Row.Pool == ELDDemoComponentPool::Common; }).Num();
    if (!Check.Require(P.DismantleInputCount >= 1 && P.DismantleInputCount < CommonComponents && P.FillGapPoints >= 1,
            TEXT("GROWTH.DismantleInputCount"), TEXT("a dismantle needs fewer distinct common components than the pool holds")))
    {
        return false;
    }
    if (!Check.Require(D.Progression.XPThresholds[0] == 0, TEXT("progression"), TEXT("level one starts at zero XP")))
    {
        return false;
    }
    for (int32 I = 0; I < D.Progression.MaxLevel - 1; ++I)
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
    if (!N.Mul64(D.Progression.MaxLevel - 1, P.PlayerXPPerLevel, Growth, TEXT("player XP reward"))
        || !N.Add64(P.PlayerXPBase, Growth, Reward, TEXT("player XP reward")))
    {
        return false;
    }
    // #97: one row per camp tier (Low, High, Elite) in any order, then the Boss row appended by the importer.
    TSet<ELDDemoCampTier> CampTiers;
    for (int32 Index = 0; Index < D.Monsters.Num() - 1; ++Index) { CampTiers.Add(D.Monsters[Index].Tier); }
    if (!Check.Require(D.Monsters.Num() == 4 && CampTiers.Num() == 3 && !CampTiers.Contains(ELDDemoCampTier::Boss)
        && D.Monsters[3].Tier == ELDDemoCampTier::Boss && D.Monsters[3].Id == D.Boss.Id, TEXT("camps"),
        TEXT("the three camp difficulty tiers and exactly one Boss row are required")))
    {
        return false;
    }
    // #97 camp growth: a positive stage length, 1..255 stages (the stage is one byte), and every scaled value fits.
    {
        int64 Product = 0, Factor = 0;
        bool bGrowth = P.CampGrowthStageMs > 0 && P.CampGrowthMaxStage >= 1 && P.CampGrowthMaxStage <= MAX_uint8
            && P.CampGrowthResistPerStageP2 >= 0;
        for (const int64 Bp : {P.CampGrowthHPPerStageBp, P.CampGrowthAttackPerStageBp, P.CampGrowthRewardXPPerStageBp})
        {
            bGrowth = bGrowth && Bp >= 0 && N.Mul64(Bp, P.CampGrowthMaxStage, Product, TEXT("CAMP.Growth"))
                && N.Add64(10000, Product, Factor, TEXT("CAMP.Growth"));
        }
        for (int32 Index = 0; bGrowth && Index < 3; ++Index)
        {
            const FLDDemoMonsterDefinition& Monster = D.Monsters[Index];
            bGrowth = N.Mul64(Monster.HPP2, 10000 + P.CampGrowthHPPerStageBp * P.CampGrowthMaxStage, Product, Monster.Id)
                && N.Mul64(Monster.AttackP2, 10000 + P.CampGrowthAttackPerStageBp * P.CampGrowthMaxStage, Product, Monster.Id)
                && N.Mul64(Monster.RewardXP, 10000 + P.CampGrowthRewardXPPerStageBp * P.CampGrowthMaxStage, Product, Monster.Id)
                && N.Mul64(P.CampGrowthResistPerStageP2, P.CampGrowthMaxStage, Factor, Monster.Id)
                && N.Add64(FMath::Max(Monster.ArmorP2, Monster.MagicResistP2), Factor, Product, Monster.Id);
        }
        if (!Check.Require(bGrowth, TEXT("CAMP.Growth"), TEXT("camp growth needs a positive stage, 1..255 stages and in-range scaled values")))
        {
            return false;
        }
    }
    // D26 (#79): the Boss body is contender-sized, reaches past both radii, sees farther than it hits, winds up inside its
    // interval and stays inside the move speed clamp. It carries no kill buff (#198 outer §5 removed the approval buff).
    {
        const FLDDemoMonsterDefinition& Body = D.Monsters[3];
        if (!Check.Require(N.EQ(N.Value(Body.CollisionRadiusCm), N.Value(P.CollisionRadiusCm))
                && N.LE(N.Add(N.Value(Body.CollisionRadiusCm), N.Value(P.CollisionRadiusCm)), N.Value(Body.AttackRangeCm))
                && N.LT(N.Value(Body.AttackRangeCm), N.Value(D.Boss.AggroRadiusCm)), TEXT("boss"), TEXT("invalid Boss collision/range/aggro geometry"))
            || !Check.Require(Body.AttackWindupMs < Body.AttackIntervalMs && D.Boss.AppearAtMs > 0 && D.Boss.AttackerMemoryMs > 0,
                TEXT("boss"), TEXT("invalid Boss timing"))
            || !Check.Require(N.LE(N.Value(P.MoveSpeedCmPerSec.Min), N.Value(Body.MoveSpeedCmPerSec))
                && N.LE(N.Value(Body.MoveSpeedCmPerSec), N.Value(P.MoveSpeedCmPerSec.Max)), TEXT("boss"), TEXT("Boss move speed outside the clamp")))
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
            || !N.Mul64(Monster.CountPerCamp, Monster.RewardXP, RewardTotal, Monster.Id))
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
