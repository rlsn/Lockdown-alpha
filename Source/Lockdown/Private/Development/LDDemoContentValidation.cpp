#include "Development/LDDemoContentJson.h"

#include "Development/LDDemoContentMath.h"
#include "Framework/LDMatchTimeline.h"
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
    // G(L) = 1 + SKILL.GrowthPerLevel x (L - 1) at the top level; the top rank and overdrive scale the grown amounts again
    // (base and ratios alike), so the envelope checks their product.
    int64 Growth = 0, Factor = 0, RankFactor = 0;
    const auto& Ranks = P.SkillRanks;
    if (Ranks.RankBaseScaleBp.Num() != 3 || Ranks.RankRatioScaleBp.Num() != 3
        || !N.Mul64(FMath::Max(D.Progression.MaxLevel - 1, 0), P.SkillGrowthPerLevelBp, Growth, TEXT("SKILL.GrowthPerLevel"))
        || !N.Add64(10000, Growth, Factor, TEXT("SKILL.GrowthPerLevel"))
        || !N.Mul64(FMath::Max(Ranks.RankBaseScaleBp[2], Ranks.RankRatioScaleBp[2]), P.Overdrive.ScaleBp, RankFactor,
            TEXT("SKILL.RankBaseScale")))
    {
        return false;
    }
    const auto Grow = [&N, Factor, RankFactor](int64 AmountP2, int64& Out, const FString& Path)
    {
        int64 Product = 0, Scaled = 0;
        if (!N.Mul64(AmountP2, Factor, Product, Path) || !N.Mul64(Product / 10000, RankFactor, Scaled, Path)) { return false; }
        Out = Scaled / 100000000;
        return true;
    };
    const auto CheckGrown = [&Grow, &CheckEffect, &N, RankFactor](const FLDDemoEffectDefinition& Effect, const FString& Path)
    {
        FLDDemoEffectDefinition Grown = Effect;
        if (!Grow(Effect.BaseP2, Grown.BaseP2, Path)) { return false; }
        // Ratios scale with the top rank and overdrive too.
        for (int64* Ratio : {&Grown.ADRatioBp, &Grown.APRatioBp, &Grown.MaxHPRatioBp})
        {
            int64 Scaled = 0;
            if (!N.Mul64(*Ratio, RankFactor, Scaled, Path)) { return false; }
            *Ratio = Scaled / 100000000;
        }
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
        for (const auto& Branch : Ability.Branches)
        {
            for (const auto& Effect : Branch.Values.Effects)
            {
                if (!CheckGrown(Effect, LDSkillBranch::DisplayId(Ability.Id, Branch.Path))) { return false; }
            }
        }
    }
    // A card's fixed amounts grow with its holder's level like an active's.
    for (const auto& Card : D.Cards)
    {
        for (const auto& Effect : Card.Effects)
        {
            if (!CheckGrown(Effect, Card.Id)) { return false; }
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
    for (const FLDDemoP2Bounds* Bounds : {&P.MaxHP, &P.AD, &P.AP, &P.Armor, &P.MagicResist})
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
    for (const FLDDemoRateBounds* Bounds : {&P.HPRegen})
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
    for (const int64 Bp : {P.OutOfCombatMaxHPRegenBp})
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
    if (!Check.Require(P.AirdropHealMaxHPBp > 0 && P.AirdropMomentumP2 > 0 && P.AirdropMoveSpeedBp > 0
        && P.AirdropMoveSpeedMs > 0, TEXT("AIRDROP"),
        TEXT("the airdrop healing, momentum and move-speed rewards must all be positive")))
    {
        return false;
    }
    // #198: PK sign-up is the last part of every rest build window (R0 and R1-R3), not an extra wait.
    if (!Check.Require(P.RestSignupMs > 0 && P.RestSignupMs <= P.RestBuildMs && P.RestSignupMs <= P.RestOpeningBuildMs,
        TEXT("REST.SignupSec"), TEXT("PK sign-up must fit inside every rest build window")))
    {
        return false;
    }
    // The final showdown ring shrinks inside its start and hurts outside.
    if (!Check.Require(N.LT(N.Value(0), N.Value(P.ShowdownRingFinalRadiusCm))
            && N.LT(N.Value(P.ShowdownRingFinalRadiusCm), N.Value(P.ShowdownRingRadiusCm))
            && P.ShowdownRingEarlyDamageBp > 0 && P.ShowdownRingLateDamageBp > 0 && P.ShowdownRingTickMs > 0,
            TEXT("SHOWDOWN.RingFinalRadiusCm"), TEXT("the showdown ring must shrink and hurt outside")))
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
    // Spec §11.1 delivery day: its own denser drop list keeps the one-drop-at-a-time spacing and leaves before T=480; the
    // patrol day's early Boss still appears before it leaves.
    {
        const auto& Daily = P.Daily;
        const int64 TimedFieldMs = P.FieldSegmentMs * FLDTimelineParameters::TimedSegments;
        bool bDrops = !Daily.DeliveryLandMs.IsEmpty() && Daily.DeliveryExpireMs > 0
            && P.AirdropAnnounceLeadMs <= Daily.DeliveryLandMs[0];
        for (int32 Index = 0; bDrops && Index < Daily.DeliveryLandMs.Num(); ++Index)
        {
            const int64 Land = Daily.DeliveryLandMs[Index];
            const int64 Next = Index + 1 < Daily.DeliveryLandMs.Num() ? Daily.DeliveryLandMs[Index + 1] - P.AirdropAnnounceLeadMs
                : TimedFieldMs;
            bDrops = Land > 0 && Land <= TimedFieldMs - Daily.DeliveryExpireMs && Land + Daily.DeliveryExpireMs <= Next;
        }
        if (!Check.Require(bDrops, TEXT("DAILY.DeliveryLandSec"),
                TEXT("delivery-day drops follow the airdrop spacing and leave before the final field"))
            || !Check.Require(Daily.PatrolBossAppearMs > 0 && Daily.PatrolBossAppearMs < D.Boss.LeaveAtMs, TEXT("DAILY.PatrolBossAppearSec"),
                TEXT("the early Boss still appears before it leaves")))
        {
            return false;
        }
    }
    // D21 (#79): the dorm's fast recovery uses the same exact integration as out-of-combat recovery.
    for (const int64 Bp : {P.DormMaxHPRegenBp})
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
    // Spec §3.13: a shelf of distinct cards and skills.
    if (!Check.Require(P.Shop.CardSlots <= D.Cards.Num(), TEXT("SHOP.CardSlots"), TEXT("a shelf shows distinct cards")))
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
    int64 TotalXP = 0;
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
    // Three species per camp tier in any order, then the Boss row appended by the importer.
    TSet<ELDDemoCampTier> CampTiers;
    for (int32 Index = 0; Index < D.Monsters.Num() - 1; ++Index) { CampTiers.Add(D.Monsters[Index].Tier); }
    if (!Check.Require(D.Monsters.Num() == 13 && CampTiers.Num() == 4 && !CampTiers.Contains(ELDDemoCampTier::Boss)
        && D.Monsters.Last().Tier == ELDDemoCampTier::Boss && D.Monsters.Last().Id == D.Boss.Id, TEXT("camps"),
        TEXT("four camp tiers with three species each and exactly one Boss are required")))
    {
        return false;
    }
    for (const ELDDemoCampTier Tier : {ELDDemoCampTier::Low,ELDDemoCampTier::High,ELDDemoCampTier::Elite,ELDDemoCampTier::Ancient})
    {
        const FLDDemoMonsterDefinition* First=nullptr;
        int32 Count=0;
        for (const auto& M:D.Monsters)
        {
            if(M.Tier!=Tier){continue;} ++Count;
            if(!First){First=&M;}
            if(!Check.Require(M.CountPerCamp==First->CountPerCamp && M.RewardXP==First->RewardXP
                && M.RespawnMs==First->RespawnMs && M.Skill.Every>0 && M.Skill.RatioBp*M.Skill.Pulses<=22500,
                M.Id,TEXT("inconsistent camp pool or invalid monster skill"))){return false;}
        }
        if(!Check.Require(Count==3,TEXT("camps"),TEXT("each camp tier requires three species"))){return false;}
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
        for (int32 Index = 0; bGrowth && Index < D.Monsters.Num()-1; ++Index)
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
        const FLDDemoMonsterDefinition& Body = D.Monsters.Last();
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
            || !Check.Require(N.LE(N.Value(Monster.Skill.RangeCm),N.Value(Monster.LeashRadiusCm))
                && Monster.Skill.EvolveStage<=255 && Monster.Skill.EchoStage<=255,
                Monster.Id,TEXT("monster skill exceeds the leash or stage storage bound"))
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
