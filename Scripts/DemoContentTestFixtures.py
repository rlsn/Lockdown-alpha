"""Synthetic content with copied world geometry, only in temporary unittest roots.

These numbers, geometry, and approval-shaped test metadata are NOT product approval.
Production tools never import this module and expose no fixture/candidate switch.
"""
from copy import deepcopy
from pathlib import Path
import shutil

from CatalogTools import encode
import DemoContent as demo


def q(unit, value):
    return {"unit": unit, "value": value}



def simple_ranks(tag):
    """Six rank steps that each lengthen the cooldown a little: the smallest valid ranks of a synthetic active."""
    q = lambda unit, value: {"unit": unit, "value": value}
    return {step: {"name": f"测试分支{tag}{step}", "text": "冷却变长。", "patches": [{"op": "cooldown", "add": q("s", 0.5)}]}
            for step in ("2A", "3A1", "3A2", "2B", "3B1", "3B2")}

def content_fixture():
    parameters = {}
    for stat, unit, base, per_level, minimum, maximum in (
        ("MaxHP", "hp", 100, 10, 1, 10000),
        ("AD", "points", 20, 1, 0, 1000), ("AP", "points", 8, 1, 0, 1000),
        ("Armor", "points", 5, 1, 0, 500), ("MagicResist", "points", 6, 1, 0, 500),
        ("HPRegen", "hp/s", 1.25, 0.25, 0, 100),
    ):
        for suffix, value in (("Base", base), ("PerLevel", per_level), ("Min", minimum), ("Max", maximum)):
            parameters[f"STAT.{stat}.{suffix}"] = q(unit, value)
    for key, unit, value in (
        ("STAT.BaseAttackSpeed", "attacks/s", 1), ("STAT.AttackSpeedPerLevelRatio", "ratio", 0.03),
        ("STAT.AttackSpeed.Min", "attacks/s", 0.1), ("STAT.AttackSpeed.Max", "attacks/s", 4),
        ("STAT.CritChance.Base", "ratio", 0), ("STAT.CritMultiplier", "ratio", 1.5),
        ("STAT.CooldownHaste.Base", "points", 0), ("STAT.CooldownHaste.Max", "points", 100),
        ("SKILL.GrowthPerLevel", "ratio", 0.05), ("SKILL.CooldownFloorSec", "s", 0.25),
        ("MOMENTUM.Max", "momentum", 80), ("MOMENTUM.Start", "momentum", 40), ("MOMENTUM.CombatPerSec", "momentum/s", 2),
        ("MOMENTUM.BasicPerRefInterval", "momentum", 8), ("MOMENTUM.BasicRefIntervalSec", "s", 1),
        ("MOMENTUM.SkillHit", "momentum", 4), ("MOMENTUM.Reaction", "momentum", 6), ("MOMENTUM.PerfectDodge", "momentum", 12),
        ("MOMENTUM.PerfectDodgeWindowSec", "s", 0.25), ("MOMENTUM.IdleDelaySec", "s", 4), ("MOMENTUM.IdleReturnPerSec", "momentum/s", 8),
        ("COMBO.ResetSec", "s", 1.25), ("OVERDRIVE.Threshold", "momentum", 80), ("OVERDRIVE.HoldSec", "s", 0.25),
        ("OVERDRIVE.Scale", "ratio", 1.5), ("OVERDRIVE.WaterSlow", "ratio", 0.4), ("OVERDRIVE.WaterSlowSec", "s", 1.25),
        ("OVERDRIVE.ShockReactionRatio", "ratio", 0.5), ("OVERDRIVE.RotDurationScale", "ratio", 2),
        ("OVERDRIVE.GustPushCm", "cm", 250), ("OVERDRIVE.GustPushSec", "s", 0.25),
        ("AI.DodgeChance", "ratio", 0.5), ("AIRDROP.Momentum", "momentum", 30),
        ("UNIT.MoveSpeedCmPerSec", "cm/s", 500), ("STAT.MoveSpeed.Min", "cm/s", 100), ("STAT.MoveSpeed.Max", "cm/s", 1200),
        ("UNIT.CollisionRadiusCm", "cm", 20), ("UNIT.BasicAttackRangeCm", "cm", 100),
        ("STAT.AttackRange.Min", "cm", 40), ("STAT.AttackRange.Max", "cm", 1000),
        ("UNIT.BasicAttackWindupSec", "s", 0.125), ("VISION.RadiusCm", "cm", 1200),
        ("CAPTURE.RadiusCm", "cm", 300),
        ("CAPTURE.SpraySec", "s", 3), ("CAPTURE.GuardSec", "s", 20),
        ("SHOWDOWN.UnlockDelaySec", "s", 10), ("SHOWDOWN.DurationSec", "s", 240),
        ("SHOWDOWN.FirstRespawnSec", "s", 18),
        ("SHOWDOWN.SecondRespawnSec", "s", 24), ("SHOWDOWN.RespawnCutoffSec", "s", 150),
        ("SHOWDOWN.ClosureStartsSec", "s", 120),
        ("MATCH.FieldSegmentSec", "s", 90),
        ("REST.OpeningBuildSec", "s", 40), ("REST.BuildSec", "s", 25), ("REST.SignupSec", "s", 8),
        ("REST.DrawSec", "s", 4), ("REST.BettingSec", "s", 6), ("REST.PKReadySec", "s", 2), ("REST.PKMaxSec", "s", 45),
        ("REST.SettlementSec", "s", 2.5), ("REST.PayoutSec", "s", 9), ("REST.ReturnSec", "s", 4),
        ("REST.ReturnSearchRadiusCm", "cm", 250), ("REST.EntryReadyTimeoutSec", "s", 20),
        ("PK.ArenaRadiusCm", "cm", 1500), ("PK.RingMidRadiusCm", "cm", 500), ("PK.RingFinalRadiusCm", "cm", 100),
        ("PK.RingShrinkStartSec", "s", 8), ("PK.RingMidSec", "s", 30), ("PK.RingFinalSec", "s", 40),
        ("PK.RingPreviewSec", "s", 8), ("PK.RingDamageTickSec", "s", 0.5),
        ("PK.RingDamageEarlyMaxHPRatioPerSec", "ratio", 0.04), ("PK.RingDamageMidMaxHPRatioPerSec", "ratio", 0.1),
        ("PK.RingDamageFinalMaxHPRatioPerSec", "ratio", 0.2), ("PK.TeamFormatRatio", "ratio", 0.25),
        ("PK.RewardMultiplier", "ratio", 1.5),
        ("THROW.CooldownSec", "s", 8), ("THROW.TelegraphSec", "s", 0.5), ("THROW.RadiusCm", "cm", 100),
        ("THROW.RangeCm", "cm", 3000), ("THROW.SlowRatio", "ratio", 0.25), ("THROW.SlowSec", "s", 1.5),
        ("THROW.ExposeSec", "s", 3),
        ("LIFE.RespawnSec.Field1", "s", 6), ("LIFE.RespawnSec.Field2", "s", 9), ("LIFE.RespawnSec.Field3", "s", 11),
        ("LIFE.ProtectionSec", "s", 2),
        ("REGEN.OutOfCombatDelaySec", "s", 4), ("REGEN.OutOfCombatMaxHPRatioPerSec", "ratio", 0.02),
        ("DORM.RadiusCm", "cm", 300), ("DORM.OutOfCombatDelaySec", "s", 2), ("DORM.MaxHPRatioPerSec", "ratio", 0.08),
        ("CAMP.ResetArrivalRadiusCm", "cm", 50),
        ("CAMP.ReturnDeadEndSec", "s", 4), ("CAMP.GrowthStageSec", "s", 30),
        ("CAMP.GrowthHPRatioPerStage", "ratio", 0.1), ("CAMP.GrowthAttackRatioPerStage", "ratio", 0.05),
        ("CAMP.GrowthResistPerStage", "points", 1), ("CAMP.GrowthRewardXPRatioPerStage", "ratio", 0.25),
        ("AIRDROP.ExpireSec", "s", 20), ("AIRDROP.AnnounceLeadSec", "s", 10), ("CAMPMARK.RevealSec", "s", 10),
        ("AIRDROP.PickupRadiusCm", "cm", 120),
        ("AIRDROP.HealMaxHPRatio", "ratio", 0.15),
        ("AIRDROP.MoveSpeedRatio", "ratio", 0.1), ("AIRDROP.MoveSpeedSec", "s", 4),
        ("LEADER.RefreshSec", "s", 8), ("LEADER.CircleRadiusCm", "cm", 1200), ("LEADER.CircleMaxOffsetCm", "cm", 800),
        ("REWARD.AssistWindowSec", "s", 10), ("GROWTH.BuildSafeWindowSec", "s", 2.5),
        ("AI.RetreatHPRatio", "ratio", 0.2), ("AI.ResumeFarmHPRatio", "ratio", 0.6),
        ("AI.HighCampEntryHPRatio", "ratio", 0.85), ("AI.HighCampReserveHPRatio", "ratio", 0.3),
        ("AI.LowCampReserveHPRatio", "ratio", 0.1), ("AI.CaptureReactionBudgetSec", "s", 2),
        ("AI.DecisionIntervalSec", "s", 0.125), ("AI.RetryPolicy.InitialBackoffSec", "s", 0.125),
        ("AI.RetryPolicy.MaxBackoffSec", "s", 0.375), ("AI.ExploreRevisitIntervalSec", "s", 12),
        ("AI.EngagementBudgetSec", "s", 30),
        ("COMBAT.BreachPerStack", "points", 2), ("COMBAT.BreachSec", "s", 3),
        ("COMBAT.BurnPerSec", "points", 4), ("COMBAT.BurnAPRatioPerSec", "ratio", 0.05),
        ("COMBAT.BurnTickSec", "s", 0.25), ("COMBAT.BurnSec", "s", 2),
        ("COMBAT.KeystonePhysiqueHealToShieldRatio", "ratio", 0.25),
        ("COMBAT.KeystonePhysiqueShieldMaxHPRatio", "ratio", 0.035),
        ("COMBAT.KeystonePhysiqueShieldDurationSec", "s", 3), ("COMBAT.KeystonePhysiqueIntervalSec", "s", 6),
        ("COMBAT.KeystoneThickSkinGuardPoints", "points", 6),
        ("COMBAT.KeystoneThickSkinGuardCapRatio", "ratio", 0.12), ("COMBAT.KeystoneThickSkinIntervalSec", "s", 8),
        ("COMBAT.KeystoneStrengthDamageRatio", "ratio", 0.06),
        ("COMBAT.KeystoneBrainsMomentumRefundRatio", "ratio", 0.15),
        ("COMBAT.KeystoneAgilityDamageBase", "points", 12.5),
        ("COMBAT.KeystoneAgilityADRatio", "ratio", 0.125),
        ("COMBAT.KeystoneAgilityEmpowerDurationSec", "s", 3),
        ("COMBAT.WetSec", "s", 3), ("COMBAT.WetSlow", "ratio", 0.1), ("COMBAT.ElectrifiedSec", "s", 3.5),
        ("COMBAT.DischargeRadiusCm", "cm", 300), ("COMBAT.DischargeIntervalSec", "s", 0.75),
        ("COMBAT.ReactionBase", "points", 10), ("COMBAT.ReactionPerLevel", "points", 0.5), ("COMBAT.ReactionAPRatio", "ratio", 0.125),
        ("COMBAT.FogRadiusCm", "cm", 200), ("COMBAT.FogSec", "s", 2), ("COMBAT.FogIntervalSec", "s", 0.5),
        ("COMBAT.ConductRadiusCm", "cm", 400), ("COMBAT.ConductSlow", "ratio", 0.25), ("COMBAT.ConductSlowSec", "s", 1.25),
        ("COMBAT.OverloadRadiusCm", "cm", 150),
    ):
        parameters[key] = q(unit, value)
    parameters["AIRDROP.LandSec"] = [q("s", 30), q("s", 90), q("s", 150)]
    parameters["SKILL.RankBaseScale"] = [q("ratio", 1), q("ratio", 1.25), q("ratio", 1.75)]
    parameters["SKILL.RankRatioScale"] = [q("ratio", 1), q("ratio", 1.125), q("ratio", 1.25)]
    parameters["SKILL.PointLevels"] = [3, 6, 9]
    parameters["COMBO.StepRatios"] = [q("ratio", 0.75), q("ratio", 1), q("ratio", 1.5)]
    parameters["OVERDRIVE.FireBurnStacks"] = 2
    parameters.update({
        "XP.BaseToNext": 50, "XP.IncreasePerLevel": 10,
        "REWARD.PlayerXP.Base": 12, "REWARD.PlayerXP.PerLevel": 2, "AI.RetryPolicy.MaxAttempts": 2,
        "SHOWDOWN.RespawnLimit": 2,
        "CAMP.ReturnDeadEndPlans": 2, "CAMP.GrowthMaxStage": 4, "AIRDROP.RewardXP": 150,
        "LEADER.MinLevel": 2,
        "COMBAT.BreachMaxStacks": 4, "COMBAT.BurnMaxStacks": 3, "COMBAT.OverloadBreachStacks": 3,
        "GROWTH.OpeningPoints": 2, "GROWTH.PointsPerLevel": 4, "GROWTH.MaxLevelPointsXP": 250, "GROWTH.MaxLevelPoints": 1,
        "GROWTH.NpcActivePoints": 5,
        "GROWTH.ActiveSkipPoints": 5, "GROWTH.NpcActiveSkipPoints": 3,
        "PK.ParticipationPoints": 2, "PK.WinnerPointsMax": 4, "PK.BettingPoolMaxPoints": 3,
        "GROWTH.KeystoneThresholdPoints": 18,
        "CARDS.CapacityBase": 5, "CARDS.LevelsPerSlot": 4, "CARDS.OfferSize": 3, "CARDS.FreeRerolls": 2,
        "CARDS.RerollsPerRest": 1, "CARDS.LockSlots": 1, "CARDS.VocabWeight": 3,
        "DAMAGE.FactorTechCap": q("ratio", 500), "CHAIN.MaxDepth": 8, "CHAIN.MaxEffectsPerStep": 64,
        "AI.CardVocabScore": 3, "AI.BreakoutChance": q("ratio", 0.5), "AI.BreakoutCardScore": 4,
    })
    formulas = {
        "StatProjection": "AddFlatThenAdditivePercent", "Resistance": "PointsOverScalePlusPoints",
        "Crit": "OncePerPrimaryAttack", "Damage": "FloorP2ThenShieldThenHP",
        "AttackSpeed": "BaseTimesAdditiveRatio", "MoveSpeed": "BaseTimesAdditiveRatio", "AttackRange": "BasePlusFlat",
        "AttackMoveAcquireRadius": "MinVisionTwiceAttackRange",
        "AttackIntervalMs": "CeilReciprocalMs", "AttackWindupMs": "CeilBaseWindupTimesBaseOverCurrentAS",
        "CooldownMs": "CeilBaseTimesScaleOverScalePlusHaste", "Regen": "CarrySubP2Remainder",
        "ResourceMaxChange": "FloorFinalRatioRejectAliveUnderflow",
        "XPToNext": "LinearIncrement", "ResistanceScale": q("points", 100), "HasteScale": q("points", 100),
    }
    policies = {
        "StatStacking": "AddFlatThenAdditivePercent",
        "MissingSourceCooldown": "ContinueServerTime", "DeadChoiceCommit": "Allowed", "ResourceMaxChange": "PreserveRatio",
        "ReleasedEffectSourceLoss": "ContinueForRemainingContender", "SourceLeave": "CancelUnsettledNoRefund",
        "InvalidAggressionProtection": "BreakOnAggressiveIntent", "CritSampling": "OncePerAttackId",
        "LifestealBasis": "PrimaryBasicAttackHPLost", "LifestealRecursiveTriggers": "Disabled",
        "BasicSecondaryTriggers": "Disabled", "RegenIntegration": "CarrySubP2Remainder",
        "ShieldPolicy": "ReplaceIfNotWeaker", "RootBlocks": ["Move", "Dash"], "CampAggroTrigger": "ConfirmedCombatHit",
        "CampGroupRetaliation": True, "CampReturningDamageable": True, "CampReturningControllable": True,
        "CampResetPolicy": "ReturnThenReset", "CampMomentumPolicy": "NotApplicable",
    }
    # Synthetic #198-shaped single-level actives; only stare_down stays out of the opening pool.
    abilities = [
        {
            "id": "active_snark_bolt", "opening": True, "cooldown_family": "cd_snark_bolt", "windup": q("s", 0.125),
            "element": "none", "hit_type": "projectile",
            "target": {"mode": "DirectionProjectile", "range": q("cm", 350), "projectile_radius": q("cm", 15), "projectile_speed": q("cm/s", 900)},
            "momentum_cost": q("momentum", 6), "cooldown": q("s", 3),
            "effects": [{"op": "damage", "base": q("points", 8), "ad_ratio": q("ratio", 0),
                         "ap_ratio": q("ratio", 0.125), "max_hp_ratio": q("ratio", 0), "damage_type": "Magical"},
                        {"op": "slow", "ratio": q("ratio", 0.25), "duration": q("s", 0.5)}],
        },
        {
            "id": "active_not_mad", "opening": True, "cooldown_family": "cd_not_mad",
            "target": {"mode": "Self"}, "windup": q("s", 0), "momentum_cost": q("momentum", 4), "cooldown": q("s", 7),
            "element": "none", "hit_type": "self",
            "effects": [{"op": "shield", "base": q("hp", 20), "ap_ratio": q("ratio", 0.2), "duration": q("s", 1.25)},
                        {"op": "dispel_slow"}, {"op": "dispel_root"}],
        },
        {
            "id": "active_pants_down", "opening": True, "cooldown_family": "cd_pants_down",
            "target": {"mode": "Unit", "range": q("cm", 250)}, "windup": q("s", 0.125), "momentum_cost": q("momentum", 7), "cooldown": q("s", 9),
            "element": "none", "hit_type": "targeted",
            "effects": [{"op": "damage", "base": q("points", 5), "ad_ratio": q("ratio", 0), "ap_ratio": q("ratio", 0.1), "max_hp_ratio": q("ratio", 0), "damage_type": "Magical"},
                        {"op": "root", "duration": q("s", 0.25)}],
        },
        {
            "id": "active_stare_down", "opening": False, "cooldown_family": "cd_stare_down",
            "target": {"mode": "Self"}, "windup": q("s", 0.125), "momentum_cost": q("momentum", 5), "cooldown": q("s", 11),
            "element": "none", "hit_type": "area",
            "effects": [{"op": "taunt", "radius": q("cm", 150), "duration": q("s", 0.5)},
                        {"op": "timed_stat_bonus", "stats": {"ArmorFlat": q("points", 3), "MagicResistFlat": q("points", 2)},
                         "duration": q("s", 1.5)}],
        },
    ]
    for index, ability in enumerate(abilities):
        ability["ranks"] = simple_ranks(index)
    stats = [
        {"id": "stat_physique", "per_point": {"MaxHPFlat": q("hp", 10), "HPRegenFlat": q("hp/s", 0.125)}},
        {"id": "stat_thick_skin", "per_point": {"ArmorFlat": q("points", 1), "MagicResistFlat": q("points", 1.5)}},
        {"id": "stat_strength", "per_point": {"ADFlat": q("points", 2), "CritChanceBonusRatio": q("ratio", 0.01)}},
        {"id": "stat_brains", "per_point": {"APFlat": q("points", 4), "CooldownHasteFlat": q("points", 2), "MomentumRegenFlat": q("momentum/s", 0.25)}},
        {"id": "stat_agility", "per_point": {"AttackSpeedBonusRatio": q("ratio", 0.02), "MoveSpeedBonusRatio": q("ratio", 0.01)}},
    ]
    # Synthetic P2 cards in native ID order: a basic burn, a skill repeat, a dash contact element and Y25's element choice.
    cards = [
        {"id": "card_fire_fist", "form": "basic", "type": "element", "vocab": ["element", "basic_hit"],
         "effects": [{"op": "burn", "basic_stacks": 2}]},
        {"id": "card_encore", "form": "skill", "type": "modify", "vocab": ["skill_cast"],
         "effects": [{"op": "auto_recast", "delay": q("s", 0.5), "damage_ratio": q("ratio", 0.5)}]},
        {"id": "card_shock_dash", "form": "dash", "type": "element", "vocab": ["element", "dash_contact"],
         "effects": [{"op": "dash_contact_element", "element": "electric"}]},
        {"id": "card_live_wire", "form": "body", "type": "element", "vocab": ["element", "damaged"],
         "choose_element": ["fire", "water"], "effects": [{"op": "element_aura", "radius": q("cm", 150)}]},
    ]
    monsters = []
    for identity, tier, count, hp, attack, armor, speed in (
        ("monster_scavenger", "Low", 3, 25, 3, 2, 1),
        ("monster_guardian", "High", 2, 80, 9, 4, 0.75),
        ("monster_advisor", "Elite", 1, 160, 14, 6, 0.6),
        ("monster_director", "Ancient", 1, 220, 18, 8, 0.5),
    ):
        monsters.append({
            "id": identity, "tier": tier, "count_per_camp": count, "hp": q("hp", hp),
            "armor": q("points", armor), "magic_resist": q("points", armor), "attack": q("points", attack),
            "attack_speed": q("attacks/s", speed), "attack_windup": q("s", 0.125),
            "attack_range": q("cm", 80), "move_speed": q("cm/s", 250), "collision_radius": q("cm", 20),
            "leash_radius": q("cm", 300), "lose_target_grace": q("s", 1), "respawn": q("s", 10),
            "reward_xp": {"Low": 8, "High": 20, "Elite": 40, "Ancient": 60}[tier],
            "hp_regen": q("hp/s", 0),
            "ap": q("points", 0), "crit_chance": q("ratio", 0),
        })
    for m in monsters:
        m["skill"]={"name":"测试技能","pattern":"Cone","every":3,"windup":q("s",.2),"recovery":q("s",.1),"ratio":q("ratio",1.5),
                    "range":q("cm",100),"radius":q("cm",30),"angle":60,"pulses":1,"pulse_interval":q("s",.3),
                    "pulse_angle":30,"offset":q("cm",40),"passive":q("ratio",0),"evolved_passive":q("ratio",0)}
        m["skill"].update(guard_duration=q("s",.7),evolve_stage=3,echo_stage=6,echo_delay=q("s",.4),echo_ratio=q("ratio",.5),
                          evolved_slow=q("ratio",.15),evolved_slow_duration=q("s",.6),projectile_speed=q("cm/s",400),low_hp_threshold=q("ratio",0),low_hp_haste=q("ratio",0))
    roots={m["tier"]:m for m in monsters}
    for id,tier in [("monster_promoter","Low"),("monster_courier","Low"),("monster_agent","High"),("monster_guard","High"),
                    ("monster_auditor","Elite"),("monster_client","Elite"),("monster_final_client","Ancient"),("monster_property","Ancient")]:
        m=deepcopy(roots[tier]);m["id"]=id;monsters.append(m)
    # The world references follow the approved world source, so a layout change needs no fixture edit.
    world_anchors = demo.parse_json((demo.ROOT / demo.WORLD_SOURCE).read_bytes(), "test world source")["anchors"]
    named = {kind: sorted(row["id"] for row in world_anchors if row["kind"] == kind) for kind in ("Spawn", "Camp")}
    camps = {
        "monsters": monsters,
        # Two member homes hold the Low pair; single-member camps are Elite when their id says so (e01..), else High.
        "placements": [{"anchor_id": identity, "monster_id": "monster_scavenger" if identity.startswith("l") else "monster_advisor" if identity.startswith("e")
                        else "monster_director" if identity in ("h09","h11") else "monster_guardian"} for identity in named["Camp"]],
    }
    boss = {"id": "monster_dean", "hp": q("hp", 200), "armor": q("points", 5), "magic_resist": q("points", 5), "attack": q("points", 12),
            "attack_interval": q("s", 1.25), "attack_windup": q("s", 0.25), "attack_range": q("cm", 90), "move_speed": q("cm/s", 200),
            "collision_radius": q("cm", 20), "aggro_radius": q("cm", 150), "attacker_memory": q("s", 2), "appear_at": q("s", 20),
            "leave_at": q("s", 200), "reward_xp": 30}
    finals = [
        {"id": "final_bowling", "requirement": "ForcedMove", "base": q("points", 45), "ap_ratio": q("ratio", 0.35),
         "distance": q("cm", 140), "distance_ratio": q("ratio", 0.8)},
        {"id": "final_forced_exit", "requirement": "HardControl", "distance": q("cm", 180), "delay": q("s", 0.25),
         "interval": q("s", 8), "control_ratio": q("ratio", 0.7)},
        {"id": "final_traffic", "requirement": "Dash", "radius": q("cm", 45), "duration": q("s", 2.5),
         "slow": q("ratio", 0.35), "slow_duration": q("s", 1), "cooldown_cost": q("s", 4)},
        {"id": "final_split", "requirement": "PlacedZone", "offset": q("cm", 140), "radius_ratio": q("ratio", 0.55),
         "damage_ratio": q("ratio", 0.6), "duration_cost": q("s", 1)},
        {"id": "final_landlord", "requirement": "Basic", "base": q("points", 50), "ad_ratio": q("ratio", 0.4),
         "radius": q("cm", 220), "delay": q("s", 0.7), "interval": q("s", 6), "damage_ratio": q("ratio", 0.8)},
        {"id": "final_breach", "requirement": "Basic", "outside": q("s", 3), "rearm_outside": q("s", 8),
         "window": q("s", 3), "distance": q("cm", 160), "cooldown_cost": q("s", 3)},
        {"id": "final_comeback", "requirement": "Basic", "base": q("points", 30), "ap_ratio": q("ratio", 0.25),
         "radius": q("cm", 220), "duration": q("s", 2), "interval": q("s", 10), "window": q("s", 4),
         "pulse_interval": q("s", 1), "slow": q("ratio", 0.3), "slow_duration": q("s", 1), "damage_ratio": q("ratio", 0.8)},
        {"id": "final_open_mic", "requirement": "Basic", "base": q("points", 25), "ap_ratio": q("ratio", 0.15),
         "radius": q("cm", 220), "duration": q("s", 2.5), "window": q("s", 3), "slow": q("ratio", 0.2),
         "pulse_interval": q("s", 1), "slow_duration": q("s", 1), "move_speed_cost": q("ratio", 0.1)},
    ]
    # Every native status needs one display row; read them so new statuses need no fixture edit.
    statuses = list(demo.parse_json((demo.ROOT / demo.IDS_SOURCE).read_bytes(), "test native ids")["statuses"])
    identities = ["character_demo", *statuses]
    identities.extend(row["id"] for rows in (abilities, stats, cards, finals, monsters, [boss]) for row in rows)
    display = [{"id": identity, "namespace": "DemoContent", "name_key": identity + ".name",
                "name_text": "测试名称 " + identity, "description_key": identity + ".description",
                "description_text": "TEST ONLY 合成说明 " + "".join(c for c in identity if not c.isdigit()),
                "icon_id": "icon_" + identity} for identity in identities]
    display[0]["portrait_icon_id"] = "portrait_character_demo"
    for row in display:
        if row["id"] in {final["id"] for final in finals}:
            for part in demo.FINAL_MOD_DISPLAY_PARTS:
                row[part + "_key"] = row["id"] + "." + part
                row[part + "_text"] = "TEST ONLY 合成" + part
    # Card templates: an active reads [op.field] of its effects, a stat [per_point.<Stat>] of one point.
    next(row for row in display if row["id"] == "active_snark_bolt")["description_text"] += " [damage.amount] [slow.ratio] [slow.duration]"
    next(row for row in display if row["id"] == "stat_brains")["description_text"] += " [per_point.APFlat] [per_point.MomentumRegenFlat]"
    next(row for row in display if row["id"] == "card_fire_fist")["description_text"] += " [burn.basic_count]"
    return {
        "identity": {"schema_version": 4, "handler_revision": 11, "projection_revision": 10,
                     "approval": {"status": "approved", "decision": "TEST ONLY: synthetic fixture, not product approval"}},
        "parameters": parameters, "formulas": formulas, "policies": policies,
        "character": {"id": "character_demo", "basic_attack": {"target_mode": "Unit", "damage_type": "Physical", "ad_ratio": q("ratio", 1)},
                      "dash": {"cooldown": q("s", 10), "distance": q("cm", 120), "duration": q("s", 0.125)}},
        "abilities": abilities, "stats": stats, "cards": cards, "final_mods": finals, "camps": camps, "boss": boss,
        "progression": {"max_level": 16, "active_slots": 3, "xp_start": 0,
                        "xp_thresholds": [0, 50, 110, 180, 260, 350, 450, 560, 680, 810, 950, 1100, 1260, 1430, 1610, 1800],
                        "active_offer_levels": [4, 9]},
        "world": {"map_id": "demo_world", "spawn_ids": named["Spawn"], "camp_ids": named["Camp"], "capture_id": "capture",
                  "pk_spawn_ids": [f"pk_spawn_{n}" for n in range(6)], "pk_throw_id": "pk_throw"},
        "display": display,
    }


def row_members(anchors, identity):
    return next(row for row in anchors if row["id"] == identity)["members"]


def vector(x, y, z=0):
    return {axis: {"numerator": value, "denominator": 1} for axis, value in zip(("x", "y", "z"), (x, y, z))}


def world_source_fixture():
    source = demo.parse_json((demo.ROOT / demo.WORLD_SOURCE).read_bytes(), "test world source")
    source["approval"] = dict(content_fixture()["identity"]["approval"])
    return source


def world_fixture():
    from DemoWorld import project_author

    source = world_source_fixture()
    return project_author(source, encode(source), (demo.ROOT / demo.WORLD_SCHEMA_SOURCE).read_bytes())


def write_world(root, world):
    """Inject test-only wire bytes, including intentionally invalid cases."""
    from DemoWorld import world_contract_bytes

    raw = encode(demo.canonical(world))
    payload = Path(root) / demo.WORLD_OUTPUT
    payload.parent.mkdir(parents=True, exist_ok=True)
    payload.write_bytes(raw)
    contract = Path(root) / demo.WORLD_CONTRACT
    contract.parent.mkdir(parents=True, exist_ok=True)
    contract.write_bytes(world_contract_bytes(raw))


def write_fixture(root):
    from SyncDemoWorldData import synchronize

    root = Path(root)
    for relative in (demo.SCHEMA_SOURCE, demo.IDS_SOURCE, demo.NATIVE_IDS, demo.WORLD_SCHEMA_SOURCE):
        target = root / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(demo.ROOT / relative, target)
    content = deepcopy(content_fixture())
    (root / demo.CONTENT_SOURCE).write_bytes(encode(content))
    (root / demo.WORLD_SOURCE).write_bytes(encode(world_source_fixture()))
    synchronize(root)
    return content
