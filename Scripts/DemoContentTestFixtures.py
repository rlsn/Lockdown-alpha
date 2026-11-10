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


def content_fixture():
    parameters = {}
    for stat, unit, base, per_level, minimum, maximum in (
        ("MaxHP", "hp", 100, 10, 1, 10000), ("MaxMP", "mp", 60, 5, 1, 5000),
        ("AD", "points", 20, 1, 0, 1000), ("AP", "points", 8, 1, 0, 1000),
        ("Armor", "points", 5, 1, 0, 500), ("MagicResist", "points", 6, 1, 0, 500),
        ("HPRegen", "hp/s", 1.25, 0.25, 0, 100), ("MPRegen", "mp/s", 0.5, 0.25, 0, 100),
    ):
        for suffix, value in (("Base", base), ("PerLevel", per_level), ("Min", minimum), ("Max", maximum)):
            parameters[f"STAT.{stat}.{suffix}"] = q(unit, value)
    for key, unit, value in (
        ("STAT.BaseAttackSpeed", "attacks/s", 1), ("STAT.AttackSpeedPerLevelRatio", "ratio", 0.03),
        ("STAT.AttackSpeed.Min", "attacks/s", 0.1), ("STAT.AttackSpeed.Max", "attacks/s", 4),
        ("STAT.CritChance.Base", "ratio", 0), ("STAT.CritMultiplier", "ratio", 1.5),
        ("STAT.CooldownHaste.Base", "points", 0), ("STAT.CooldownHaste.Max", "points", 100),
        ("SKILL.GrowthPerLevel", "ratio", 0.1), ("SKILL.GrowthPerLevelAfter10", "ratio", 0.05),
        ("UNIT.MoveSpeedCmPerSec", "cm/s", 500), ("STAT.MoveSpeed.Min", "cm/s", 100), ("STAT.MoveSpeed.Max", "cm/s", 1200),
        ("UNIT.CollisionRadiusCm", "cm", 20), ("UNIT.BasicAttackRangeCm", "cm", 100),
        ("STAT.AttackRange.Min", "cm", 40), ("STAT.AttackRange.Max", "cm", 1000),
        ("UNIT.BasicAttackWindupSec", "s", 0.125), ("VISION.RadiusCm", "cm", 1200),
        ("CAPTURE.RadiusCm", "cm", 300),
        ("CAPTURE.SpraySec", "s", 3), ("CAPTURE.GuardSec", "s", 20),
        ("MATCH.FieldSegmentSec", "s", 90),
        ("REST.OpeningBuildSec", "s", 40), ("REST.BuildSec", "s", 25), ("REST.SignupSec", "s", 8),
        ("REST.DrawSec", "s", 4), ("REST.BettingSec", "s", 6), ("REST.PKReadySec", "s", 2), ("REST.PKMaxSec", "s", 45),
        ("REST.SettlementSec", "s", 2.5), ("REST.PayoutSec", "s", 9), ("REST.ReturnSec", "s", 4),
        ("REST.ReturnSearchRadiusCm", "cm", 250),
        ("PK.ArenaRadiusCm", "cm", 1500), ("PK.RingMidRadiusCm", "cm", 500), ("PK.RingFinalRadiusCm", "cm", 100),
        ("PK.RingShrinkStartSec", "s", 8), ("PK.RingMidSec", "s", 30), ("PK.RingFinalSec", "s", 40),
        ("PK.RingPreviewSec", "s", 8), ("PK.RingDamageTickSec", "s", 0.5),
        ("PK.RingDamageEarlyMaxHPRatioPerSec", "ratio", 0.04), ("PK.RingDamageMidMaxHPRatioPerSec", "ratio", 0.1),
        ("PK.RingDamageFinalMaxHPRatioPerSec", "ratio", 0.2), ("PK.TeamFormatRatio", "ratio", 0.25),
        ("THROW.CooldownSec", "s", 8), ("THROW.TelegraphSec", "s", 0.5), ("THROW.RadiusCm", "cm", 100),
        ("THROW.RangeCm", "cm", 3000), ("THROW.SlowRatio", "ratio", 0.25), ("THROW.SlowSec", "s", 1.5),
        ("THROW.ExposeSec", "s", 3),
        ("LIFE.RespawnSec.Field1", "s", 6), ("LIFE.RespawnSec.Field2", "s", 9), ("LIFE.RespawnSec.Field3", "s", 11),
        ("LIFE.RespawnSec.Final", "s", 22), ("LIFE.ProtectionSec", "s", 2),
        ("REGEN.OutOfCombatDelaySec", "s", 4), ("REGEN.OutOfCombatMaxHPRatioPerSec", "ratio", 0.02),
        ("REGEN.OutOfCombatMaxMPRatioPerSec", "ratio", 0.01),
        ("DORM.RadiusCm", "cm", 300), ("DORM.OutOfCombatDelaySec", "s", 2), ("DORM.MaxHPRatioPerSec", "ratio", 0.08),
        ("DORM.MaxMPRatioPerSec", "ratio", 0.05),
        ("CAMP.ResetArrivalRadiusCm", "cm", 50),
        ("CAMP.ReturnDeadEndSec", "s", 4), ("CAMP.GrowthStageSec", "s", 30),
        ("CAMP.GrowthHPRatioPerStage", "ratio", 0.1), ("CAMP.GrowthAttackRatioPerStage", "ratio", 0.05),
        ("CAMP.GrowthResistPerStage", "points", 1), ("CAMP.GrowthRewardXPRatioPerStage", "ratio", 0.25),
        ("AIRDROP.FirstLandSec", "s", 30), ("AIRDROP.IntervalSec", "s", 60), ("AIRDROP.AnnounceLeadSec", "s", 10),
        ("AIRDROP.PickupRadiusCm", "cm", 120),
        ("CHARM.FirstWaveSec", "s", 20), ("CHARM.WaveIntervalSec", "s", 60), ("CHARM.LifetimeSec", "s", 30),
        ("CHARM.PickupRadiusCm", "cm", 80), ("CHARM.HealMaxHPRatio", "ratio", 0.15),
        ("CHARM.ManaMaxMPRatio", "ratio", 0.2), ("CHARM.MoveSpeedRatio", "ratio", 0.1), ("CHARM.MoveSpeedSec", "s", 4),
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
        ("COMBAT.BurnTickSec", "s", 0.25), ("COMBAT.BurnSec", "s", 2), ("COMBAT.LifestealCap", "ratio", 0.4),
        ("COMBAT.WetSec", "s", 3), ("COMBAT.WetSlow", "ratio", 0.1), ("COMBAT.ElectrifiedSec", "s", 3.5),
        ("COMBAT.DischargeRadiusCm", "cm", 300), ("COMBAT.DischargeIntervalSec", "s", 0.75),
        ("COMBAT.ReactionBase", "points", 10), ("COMBAT.ReactionPerLevel", "points", 0.5), ("COMBAT.ReactionAPRatio", "ratio", 0.125),
        ("COMBAT.FogRadiusCm", "cm", 200), ("COMBAT.FogSec", "s", 2), ("COMBAT.FogIntervalSec", "s", 0.5),
        ("COMBAT.ConductRadiusCm", "cm", 400), ("COMBAT.ConductSlow", "ratio", 0.25), ("COMBAT.ConductSlowSec", "s", 1.25),
        ("COMBAT.OverloadRadiusCm", "cm", 150),
    ):
        parameters[key] = q(unit, value)
    parameters.update({
        "XP.BaseToNext": 50, "XP.IncreasePerLevel": 10,
        "REWARD.PlayerXP.Base": 12, "REWARD.PlayerXP.PerLevel": 2, "AI.RetryPolicy.MaxAttempts": 2,
        "CAMP.ReturnDeadEndPlans": 2, "CAMP.GrowthMaxStage": 4, "AIRDROP.RewardXP": 150,
        "CHARM.WaveCount": 5, "CHARM.PerWave": 2,
        "LEADER.MinLevel": 2,
        "COMBAT.BreachMaxStacks": 4, "COMBAT.BurnMaxStacks": 3, "COMBAT.OverloadBreachStacks": 3,
        "GROWTH.OpeningPoints": 2, "GROWTH.PointsPerLevel": 4, "GROWTH.MaxLevelPointsXP": 250, "GROWTH.MaxLevelPoints": 1,
        "GROWTH.NpcActivePoints": 5,
        "PK.ParticipationPoints": 2, "PK.WinnerPointsMax": 4, "PK.BettingPoolMaxPoints": 3,
        "GROWTH.EmptyComponentPoolPoints": 2,
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
        "CampResetPolicy": "ReturnThenReset", "CampManaPolicy": "NotApplicable",
    }
    # Synthetic #198-shaped single-level actives; only stare_down stays out of the opening pool.
    abilities = [
        {
            "id": "active_snark_bolt", "opening": True, "cooldown_family": "cd_snark_bolt", "windup": q("s", 0.125),
            "target": {"mode": "DirectionProjectile", "range": q("cm", 350), "projectile_radius": q("cm", 15), "projectile_speed": q("cm/s", 900)},
            "mana_cost": q("mp", 6), "cooldown": q("s", 3),
            "effects": [{"op": "damage", "base": q("points", 8), "ad_ratio": q("ratio", 0),
                         "ap_ratio": q("ratio", 0.125), "max_hp_ratio": q("ratio", 0), "damage_type": "Magical"},
                        {"op": "slow", "ratio": q("ratio", 0.25), "duration": q("s", 0.5)}],
        },
        {
            "id": "active_not_mad", "opening": True, "cooldown_family": "cd_not_mad",
            "target": {"mode": "Self"}, "windup": q("s", 0), "mana_cost": q("mp", 4), "cooldown": q("s", 7),
            "effects": [{"op": "shield", "base": q("hp", 20), "ap_ratio": q("ratio", 0.2), "duration": q("s", 1.25)},
                        {"op": "dispel_slow"}, {"op": "dispel_root"}],
        },
        {
            "id": "active_pants_down", "opening": True, "cooldown_family": "cd_pants_down",
            "target": {"mode": "Unit", "range": q("cm", 250)}, "windup": q("s", 0.125), "mana_cost": q("mp", 7), "cooldown": q("s", 9),
            "effects": [{"op": "damage", "base": q("points", 5), "ad_ratio": q("ratio", 0), "ap_ratio": q("ratio", 0.1), "max_hp_ratio": q("ratio", 0), "damage_type": "Magical"},
                        {"op": "root", "duration": q("s", 0.25)}],
        },
        {
            "id": "active_stare_down", "opening": False, "cooldown_family": "cd_stare_down",
            "target": {"mode": "Self"}, "windup": q("s", 0.125), "mana_cost": q("mp", 5), "cooldown": q("s", 11),
            "effects": [{"op": "taunt", "radius": q("cm", 150), "duration": q("s", 0.5)},
                        {"op": "timed_stat_bonus", "stats": {"ArmorFlat": q("points", 3), "MagicResistFlat": q("points", 2)},
                         "duration": q("s", 1.5)}],
        },
    ]
    stats = [
        {"id": "stat_physique", "per_point": {"MaxHPFlat": q("hp", 10), "HPRegenFlat": q("hp/s", 0.125)}},
        {"id": "stat_thick_skin", "per_point": {"ArmorFlat": q("points", 1), "MagicResistFlat": q("points", 1.5)}},
        {"id": "stat_strength", "per_point": {"ADFlat": q("points", 2), "CritChanceBonusRatio": q("ratio", 0.01)}},
        {"id": "stat_brains", "per_point": {"APFlat": q("points", 4), "CooldownHasteFlat": q("points", 2), "MPRegenFlat": q("mp/s", 0.25)}},
        {"id": "stat_agility", "per_point": {"AttackSpeedBonusRatio": q("ratio", 0.02), "MoveSpeedBonusRatio": q("ratio", 0.01)}},
    ]
    # Synthetic #198 B components in native ID order: an always-opening burn, a conditional opening, a dash-armed empowered
    # basic and the active/element binding.
    components = [
        {"id": "component_fist_fire", "kind": "base", "pool": "common", "opening": "always",
         "effects": [{"op": "burn", "basic_stacks": 2}]},
        {"id": "component_kick_down", "kind": "base", "pool": "common", "opening": {"actives": ["active_pants_down"]},
         "effects": [{"op": "control_damage_amp", "ratio": q("ratio", 0.1)}]},
        {"id": "component_dash_punch", "kind": "base", "pool": "common",
         "effects": [{"op": "empower_basic", "trigger": "DashComplete", "base": q("points", 6), "ad_ratio": q("ratio", 0.125),
                      "ap_ratio": q("ratio", 0), "damage_type": "Physical", "duration": q("s", 1.5), "charges": 1}]},
        {"id": "component_dirty_trick", "kind": "base", "pool": "common", "opening": {"actives": ["active_snark_bolt"]},
         "binding": {"kind": "active_element", "actives": ["active_snark_bolt", "active_pants_down"], "elements": ["fire", "water"]},
         "effects": [{"op": "active_element"}]},
    ]
    monsters = []
    for identity, tier, count, hp, attack, armor, speed in (
        ("monster_scavenger", "Low", 2, 25, 3, 2, 1),
        ("monster_guardian", "High", 1, 80, 9, 4, 0.75),
        ("monster_advisor", "Elite", 1, 160, 14, 6, 0.6),
    ):
        monsters.append({
            "id": identity, "tier": tier, "count_per_camp": count, "hp": q("hp", hp),
            "armor": q("points", armor), "magic_resist": q("points", armor), "attack": q("points", attack),
            "attack_speed": q("attacks/s", speed), "attack_windup": q("s", 0.125),
            "attack_range": q("cm", 80), "move_speed": q("cm/s", 250), "collision_radius": q("cm", 20),
            "leash_radius": q("cm", 300), "lose_target_grace": q("s", 1), "respawn": q("s", 10),
            "reward_xp": {"Low": 8, "High": 20, "Elite": 40}[tier],
            "hp_regen": q("hp/s", 0), "max_mp": q("mp", 0), "mp_regen": q("mp/s", 0),
            "ap": q("points", 0), "crit_chance": q("ratio", 0),
        })
    # The world references follow the approved world source, so a layout change needs no fixture edit.
    world_anchors = demo.parse_json((demo.ROOT / demo.WORLD_SOURCE).read_bytes(), "test world source")["anchors"]
    named = {kind: sorted(row["id"] for row in world_anchors if row["kind"] == kind) for kind in ("Spawn", "Camp")}
    camps = {
        "monsters": monsters,
        # Two member homes hold the Low pair; single-member camps are Elite when their id says so (e01..), else High.
        "placements": [{"anchor_id": identity, "monster_id": "monster_scavenger" if len(row_members(world_anchors, identity)) != 1
                        else "monster_advisor" if identity.startswith("e") else "monster_guardian"} for identity in named["Camp"]],
    }
    boss = {"id": "monster_dean", "hp": q("hp", 200), "armor": q("points", 5), "magic_resist": q("points", 5), "attack": q("points", 12),
            "attack_interval": q("s", 1.25), "attack_windup": q("s", 0.25), "attack_range": q("cm", 90), "move_speed": q("cm/s", 200),
            "collision_radius": q("cm", 20), "aggro_radius": q("cm", 150), "attacker_memory": q("s", 2), "appear_at": q("s", 20),
            "reward_xp": 30,
            "approval": {"duration": q("s", 15), "stats": {"ADBonusRatio": q("ratio", 0.1), "APBonusRatio": q("ratio", 0.1),
                                                           "MoveSpeedBonusRatio": q("ratio", 0.05)}}}
    # Every native status needs one display row; read them so new statuses need no fixture edit.
    statuses = list(demo.parse_json((demo.ROOT / demo.IDS_SOURCE).read_bytes(), "test native ids")["statuses"])
    identities = ["character_demo", *statuses]
    identities.extend(row["id"] for rows in (abilities, stats, components, monsters, [boss]) for row in rows)
    display = [{"id": identity, "namespace": "DemoContent", "name_key": identity + ".name",
                "name_text": "测试名称 " + identity, "description_key": identity + ".description",
                "description_text": "TEST ONLY 合成说明 " + "".join(c for c in identity if not c.isdigit()),
                "icon_id": "icon_" + identity} for identity in identities]
    display[0]["portrait_icon_id"] = "portrait_character_demo"
    # Card templates: an active reads [op.field] of its effects, a stat [per_point.<Stat>] of one point.
    next(row for row in display if row["id"] == "active_snark_bolt")["description_text"] += " [damage.amount] [slow.ratio] [slow.duration]"
    next(row for row in display if row["id"] == "stat_brains")["description_text"] += " [per_point.APFlat] [per_point.MPRegenFlat]"
    next(row for row in display if row["id"] == "component_fist_fire")["description_text"] += " [burn.basic_count]"
    return {
        "identity": {"schema_version": 2, "handler_revision": 3, "projection_revision": 5,
                     "approval": {"status": "approved", "decision": "TEST ONLY: synthetic fixture, not product approval"}},
        "parameters": parameters, "formulas": formulas, "policies": policies,
        "character": {"id": "character_demo", "basic_attack": {"target_mode": "Unit", "damage_type": "Physical", "ad_ratio": q("ratio", 1)},
                      "dash": {"mana_cost": q("mp", 0), "cooldown": q("s", 10), "distance": q("cm", 120), "duration": q("s", 0.125)}},
        "abilities": abilities, "stats": stats, "components": components, "camps": camps, "boss": boss,
        "progression": {"max_level": 16, "active_slots": 3, "xp_start": 0,
                        "xp_thresholds": [0, 50, 110, 180, 260, 350, 450, 560, 680, 810, 950, 1100, 1260, 1430, 1610, 1800],
                        "active_offer_levels": [4, 9],
                        "component_category_weights": [{"levels": [2, 8], "base": 60, "mod": 20, "connector": 20},
                                                       {"levels": [9, 16], "base": 30, "mod": 30, "connector": 40}]},
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
