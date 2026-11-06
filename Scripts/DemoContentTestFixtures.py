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
        ("STAT.CritChance.Base", "ratio", 0), ("STAT.CritMultiplier", "ratio", 1.5), ("STAT.AbilityCritMultiplier", "ratio", 1.25),
        ("STAT.CooldownHaste.Base", "points", 0), ("STAT.CooldownHaste.Max", "points", 100),
        ("UNIT.MoveSpeedCmPerSec", "cm/s", 500), ("STAT.MoveSpeed.Min", "cm/s", 100), ("STAT.MoveSpeed.Max", "cm/s", 1200),
        ("UNIT.CollisionRadiusCm", "cm", 20), ("UNIT.BasicAttackRangeCm", "cm", 100),
        ("STAT.AttackRange.Min", "cm", 40), ("STAT.AttackRange.Max", "cm", 1000),
        ("UNIT.BasicAttackWindupSec", "s", 0.125), ("VISION.RadiusCm", "cm", 1200),
        ("SHOP.InteractRadiusCm", "cm", 200), ("CAPTURE.RadiusCm", "cm", 300),
        ("CAPTURE.SpraySec", "s", 3), ("CAPTURE.GuardSec", "s", 20),
        ("MATCH.DevelopmentSec", "s", 90), ("LIFE.RespawnSec", "s", 22), ("LIFE.ProtectionSec", "s", 2),
        ("REGEN.OutOfCombatDelaySec", "s", 4), ("REGEN.OutOfCombatMaxHPRatioPerSec", "ratio", 0.02),
        ("REGEN.OutOfCombatMaxMPRatioPerSec", "ratio", 0.01),
        ("DORM.RadiusCm", "cm", 300), ("DORM.OutOfCombatDelaySec", "s", 2), ("DORM.MaxHPRatioPerSec", "ratio", 0.08),
        ("DORM.MaxMPRatioPerSec", "ratio", 0.05),
        ("SHOP.SellRatio", "ratio", 0.5), ("CAMP.ResetArrivalRadiusCm", "cm", 50),
        ("CAMP.ReturnDeadEndSec", "s", 4),
        ("AIRDROP.FirstLandSec", "s", 30), ("AIRDROP.IntervalSec", "s", 60), ("AIRDROP.AnnounceLeadSec", "s", 10),
        ("AIRDROP.PickupRadiusCm", "cm", 120),
        ("LEADER.RefreshSec", "s", 8), ("LEADER.CircleRadiusCm", "cm", 1200), ("LEADER.CircleMaxOffsetCm", "cm", 800),
        ("REWARD.AssistWindowSec", "s", 10), ("REWARD.AssistGoldRatio", "ratio", 0.4),
        ("SHOP.UpgradeStatBonusRatio", "ratio", 0.4), ("CHOICE.UpgradeWeight", "ratio", 2.5),
        ("CHOICE.NewActiveOpenSlotWeight", "ratio", 1.75), ("CHOICE.NewActiveFullSlotWeight", "ratio", 0.25),
        ("CHOICE.NewPassiveWeight", "ratio", 1.25), ("CHOICE.AffinityStep", "ratio", 1.5),
        ("CHOICE.AffinityCap", "ratio", 2), ("CHOICE.ThirdSlotAttributeChance", "ratio", 0.6),
        ("AI.RetreatHPRatio", "ratio", 0.2), ("AI.ResumeFarmHPRatio", "ratio", 0.6),
        ("AI.HighCampEntryHPRatio", "ratio", 0.85), ("AI.HighCampReserveHPRatio", "ratio", 0.3),
        ("AI.LowCampReserveHPRatio", "ratio", 0.1), ("AI.CaptureReactionBudgetSec", "s", 2),
        ("AI.DecisionIntervalSec", "s", 0.125), ("AI.RetryPolicy.InitialBackoffSec", "s", 0.125),
        ("AI.RetryPolicy.MaxBackoffSec", "s", 0.375), ("AI.ExploreRevisitIntervalSec", "s", 12),
        ("AI.EngagementBudgetSec", "s", 30), ("COMBAT.RearArcDeg", "deg", 90),
    ):
        parameters[key] = q(unit, value)
    parameters.update({
        "SHOP.StartingGold": 10, "SHOP.UpgradeCost": 30, "XP.BaseToNext": 50, "XP.IncreasePerLevel": 10,
        "REWARD.PlayerXP.Base": 12, "REWARD.PlayerXP.PerLevel": 2,
        "REWARD.PlayerGold.Base": 24, "REWARD.PlayerGold.PerLevel": 3, "AI.RetryPolicy.MaxAttempts": 2,
        "CAMP.ReturnDeadEndPlans": 2, "AIRDROP.RewardXP": 150, "AIRDROP.RewardGold": 100,
        "LEADER.MinLevel": 2, "LEADER.BountyGoldPerRefresh": 40, "LEADER.BountyGoldMax": 200,
    })
    formulas = {
        "StatProjection": "AddFlatThenAdditivePercent", "Resistance": "PointsOverScalePlusPoints",
        "Crit": "OncePerPrimaryAttack", "Damage": "FloorP2ThenShieldThenHP",
        "AttackSpeed": "BaseTimesAdditiveRatio", "MoveSpeed": "BaseTimesAdditiveRatio", "AttackRange": "BasePlusFlat",
        "AttackMoveAcquireRadius": "MinVisionTwiceAttackRange",
        "AttackIntervalMs": "CeilReciprocalMs", "AttackWindupMs": "CeilBaseWindupTimesBaseOverCurrentAS",
        "CooldownMs": "CeilBaseTimesScaleOverScalePlusHaste", "Regen": "CarrySubP2Remainder",
        "ResourceMaxChange": "FloorFinalRatioRejectAliveUnderflow", "SellGold": "FloorPurchasePriceTimesSellRatio",
        "XPToNext": "LinearIncrement", "ResistanceScale": q("points", 100), "HasteScale": q("points", 100),
    }
    policies = {
        "StatStacking": "AddFlatThenAdditivePercent", "DuplicateEquipment": "Allowed",
        "DuplicateEquipmentPassive": "OncePerFamily", "DuplicateEquipmentActive": "IndependentSources",
        "MissingSourceCooldown": "ContinueServerTime", "DeadChoiceCommit": "Allowed", "ResourceMaxChange": "PreserveRatio",
        "ReleasedEffectSourceLoss": "ContinueForRemainingContender", "SourceLeave": "CancelUnsettledNoRefund",
        "InvalidAggressionProtection": "BreakOnAggressiveIntent", "CritSampling": "OncePerAttackId",
        "LifestealBasis": "PrimaryBasicAttackHPLost", "LifestealRecursiveTriggers": "Disabled",
        "BasicSecondaryTriggers": "Disabled", "RegenIntegration": "CarrySubP2Remainder", "ShopRequiresLineOfSight": False,
        "ShieldPolicy": "ReplaceIfNotWeaker", "RootBlocks": ["Move", "Dash"], "CampAggroTrigger": "ConfirmedCombatHit",
        "CampGroupRetaliation": True, "CampReturningDamageable": True, "CampReturningControllable": True,
        "CampResetPolicy": "ReturnThenReset", "CampManaPolicy": "NotApplicable",
    }
    abilities = [
        {
            "id": "active_arc_bolt", "kind": "active", "tags": ["Mobility", "Breach"], "draftable": True, "cooldown_family": "cd_arc_bolt", "windup": q("s", 0.125),
            "target": {"mode": "DirectionProjectile", "range": q("cm", 350), "projectile_radius": q("cm", 15), "projectile_speed": q("cm/s", 900)},
            "ranks": [{
                "rank": rank, "mana_cost": q("mp", 5 + rank), "cooldown": q("s", 3),
                "effects": [{"op": "damage", "base": q("points", 8 * rank), "ad_ratio": q("ratio", 0),
                             "ap_ratio": q("ratio", 0.125 * rank), "max_hp_ratio": q("ratio", 0), "damage_type": "Magical"}]
                + ([{"op": "resist_shred", "armor_ratio": q("ratio", 0.1), "magic_resist_ratio": q("ratio", 0), "duration": q("s", 1)}]
                   if rank == 3 else []),
            } for rank in range(1, 4)],
        },
        {
            "id": "active_guard_pulse", "kind": "active", "tags": ["Shield", "Control"], "draftable": True, "cooldown_family": "cd_guard_pulse",
            "target": {"mode": "Self"}, "windup": q("s", 0),
            "ranks": [{
                "rank": rank, "mana_cost": q("mp", 4 * rank), "cooldown": q("s", 7),
                "effects": [{"op": "shield", "base": q("hp", 20 * rank), "ap_ratio": q("ratio", 0.2), "duration": q("s", 1.25)},
                            {"op": "dispel_root"}, {"op": "dispel_slow"}]
                + ([{"op": "cooldown_refund", "trigger": "Cleanse", "entries": "Self", "ratio": q("ratio", 0.25)}] if rank == 3 else []),
            } for rank in range(1, 4)],
        },
        {
            "id": "active_binding_snare", "kind": "active", "tags": ["Control", "Breach"], "draftable": True, "cooldown_family": "cd_binding_snare",
            "target": {"mode": "Unit", "range": q("cm", 250)}, "windup": q("s", 0.125),
            "ranks": [{
                "rank": rank, "mana_cost": q("mp", 6 + rank), "cooldown": q("s", 9),
                "effects": [{"op": "damage", "base": q("points", 5 * rank), "ad_ratio": q("ratio", 0), "ap_ratio": q("ratio", 0.1), "max_hp_ratio": q("ratio", 0), "damage_type": "Magical"},
                            {"op": "root", "duration": q("s", 0.25 * rank)}]
                + ([{"op": "vulnerable", "ratio": q("ratio", 0.1)}] if rank == 3 else []),
            } for rank in range(1, 4)],
        },
        {
            "id": "passive_battle_rhythm", "kind": "passive", "tags": ["BasicAttack", "Mobility"], "draftable": True, "target": {"mode": "Self"},
            "ranks": [{"rank": rank, "effects": [{"op": "stat_bonus", "stats": {"AttackSpeedBonusRatio": q("ratio", value)}},
                                                 {"op": "basic_stacks", "stats": {"MoveSpeedBonusRatio": q("ratio", 0.01)},
                                                  "duration": q("s", 1), "max_stacks": 2}]}
                      for rank, value in enumerate((0.02, 0.04, 0.06), 1)],
        },
        {
            "id": "passive_second_wind", "kind": "passive", "tags": ["Tank", "Breach"], "draftable": True, "target": {"mode": "Self"},
            "ranks": [{"rank": rank, "effects": [{"op": "out_of_combat_regen", "delay": q("s", 2.5), "max_hp_ratio": q("ratio", 0.001 * rank)}]}
                      for rank in range(1, 4)],
        },
        {
            "id": "passive_precision", "kind": "passive", "tags": ["BasicAttack", "Breach"], "draftable": True, "target": {"mode": "Self"},
            "ranks": [{"rank": rank, "effects": [{"op": "stat_bonus", "stats": {"CritChanceBonusRatio": q("ratio", value)}},
                                                 {"op": "cooldown_refund", "trigger": "BasicCrit", "entries": "Actives", "cooldown": q("s", 0.5)}]
                       + ([{"op": "ability_crit", "mode": "Chance", "ratio": q("ratio", 0.5), "abilities": ["active_binding_snare"]}]
                          if rank == 3 else [])}
                      for rank, value in enumerate((0.03, 0.06, 0.09), 1)],
        },
        {
            "id": "active_expose_post", "kind": "active", "tags": ["Breach", "Control"], "draftable": False, "cooldown_family": "cd_expose_post",
            "target": {"mode": "Unit", "range": q("cm", 250)}, "windup": q("s", 0.125),
            "ranks": [{
                "rank": rank, "mana_cost": q("mp", 6), "cooldown": q("s", 5),
                "effects": [{"op": "dot", "base": q("points", 3 * rank), "ad_ratio": q("ratio", 0.05), "ap_ratio": q("ratio", 0.05),
                             "damage_type": "Magical", "interval": q("s", 0.5), "count": 3, "shred": q("points", 2)}]
                + ([{"op": "mobility_punish", "base": q("points", 4), "ad_ratio": q("ratio", 0), "ap_ratio": q("ratio", 0.1),
                     "damage_type": "Magical"}] if rank == 3 else []),
            } for rank in range(1, 4)],
        },
        {
            "id": "active_table_flip", "kind": "active", "tags": ["Shield", "Mobility"], "draftable": False, "cooldown_family": "cd_table_flip",
            "target": {"mode": "Self"}, "windup": q("s", 0),
            "ranks": [{
                "rank": rank, "mana_cost": q("mp", 6), "cooldown": q("s", 6),
                "effects": [{"op": "shield", "base": q("hp", 10 * rank), "ap_ratio": q("ratio", 0.1), "max_hp_ratio": q("ratio", 0.01), "duration": q("s", 1)},
                            {"op": "self_area", "radius": q("cm", 80)},
                            {"op": "shield_burst", "base": q("points", 5), "shield_ratio": q("ratio", 1.2), "cap": q("points", 60), "damage_type": "Physical"}]
                + ([{"op": "knockback", "distance": q("cm", 40), "duration": q("s", 0.125)}] if rank == 3 else []),
            } for rank in range(1, 4)],
        },
        {
            "id": "passive_dox_master", "kind": "passive", "tags": ["BasicAttack", "Breach"], "draftable": True, "target": {"mode": "Self"},
            "ranks": [{"rank": rank, "effects": [{"op": "breach_stacks", "base": q("points", rank), "ad_ratio": q("ratio", 0.01),
                                                  "ap_ratio": q("ratio", 0.01), "damage_type": "Magical", "interval": q("s", 0.5),
                                                  "duration": q("s", 1.5), "max_stacks": 3, "shred": q("points", rank),
                                                  "heal_cut": q("ratio", 0.2 if rank == 3 else 0)}]}
                      for rank in range(1, 4)],
        },
    ]
    attributes = [
        {"id": "attr_might", "stats": {"ADFlat": q("points", 2), "CritChanceBonusRatio": q("ratio", 0.02)}},
        {"id": "attr_focus", "stats": {"APFlat": q("points", 4), "CooldownHasteFlat": q("points", 2)}},
        {"id": "attr_vitality", "stats": {"MaxHPFlat": q("hp", 20), "MaxMPFlat": q("mp", 10), "HPRegenFlat": q("hp/s", 0.5)}},
        {"id": "attr_resolve", "stats": {"MaxHPFlat": q("hp", 10), "ArmorFlat": q("points", 1), "MagicResistFlat": q("points", 1)}},
        {"id": "attr_reach", "stats": {"AttackRangeCmFlat": q("cm", 10), "AttackSpeedBonusRatio": q("ratio", 0.02)}},
        {"id": "attr_wild_brawler", "stats": {"AttackSpeedBonusRatio": q("ratio", 0.04), "MoveSpeedBonusRatio": q("ratio", 0.02)},
         "penalties": {"AttackRangeCmFlat": q("cm", 10)}},
    ]
    equipment = [
        {"id": "item_iron_plate", "buy_price": 51, "tags": ["Tank"], "stats": {"MaxHPFlat": q("hp", 30), "ArmorFlat": q("points", 4)}, "active": None,
         "passive": {"family": "passive_crit_guard", "effects": [{"op": "crit_damage_reduction", "ratio": q("ratio", 0.1)}]}},
        {"id": "item_edge_blade", "buy_price": 52, "tags": ["BasicAttack"], "stats": {"ADFlat": q("points", 4), "AttackSpeedBonusRatio": q("ratio", 0.05)}, "active": None,
         "passive": {"family": "passive_third_strike", "effects": [{"op": "nth_basic_bonus", "count": 3, "base": q("points", 5), "damage_type": "Physical"}]}},
        {"id": "item_runic_focus", "buy_price": 53, "tags": ["Breach"], "stats": {"APFlat": q("points", 6), "CooldownHasteFlat": q("points", 5)}, "active": None,
         "passive": {"family": "passive_mana_refund", "effects": [{"op": "mana_on_hit", "trigger": "AbilityHit", "mana": q("mp", 2)}]}},
        {"id": "item_siphon_emblem", "buy_price": 54, "tags": ["BasicAttack", "Tank"], "stats": {"ADFlat": q("points", 2), "MagicResistFlat": q("points", 3)}, "active": None,
         "passive": {"family": "passive_siphon", "effects": [{"op": "lifesteal", "ratio": q("ratio", 0.1)}],
                     "upgraded": {"effects": [{"op": "lifesteal", "ratio": q("ratio", 0.15)}]}}},
        {"id": "item_phase_anchor", "buy_price": 55, "tags": ["Mobility"], "stats": {"MoveSpeedBonusRatio": q("ratio", 0.05)}, "active": None,
         "passive": {"family": "passive_stride", "effects": [{"op": "dash_boost", "cooldown_ratio": q("ratio", 0.5), "distance_bonus": q("ratio", 0.25)}]}},
        {"id": "item_arcane_battery", "buy_price": 56, "tags": ["Shield", "Breach"], "stats": {"MaxMPFlat": q("mp", 20), "MagicResistFlat": q("points", 2)}, "passive": None,
         "active": {"target": {"mode": "Self"}, "windup": q("s", 0), "cooldown_family": "cd_arcane_battery",
                    "rank": {"rank": 1, "mana_cost": q("mp", 0), "cooldown": q("s", 12),
                             "effects": [{"op": "restore_mana", "ratio": q("ratio", 0.2)},
                                         {"op": "shield", "base": q("hp", 45), "ap_ratio": q("ratio", 0.2), "duration": q("s", 1.25)}]},
                    "upgraded_rank": {"rank": 2, "mana_cost": q("mp", 0), "cooldown": q("s", 10),
                                      "effects": [{"op": "restore_mana", "ratio": q("ratio", 0.3)},
                                                  {"op": "shield", "base": q("hp", 60), "ap_ratio": q("ratio", 0.25), "duration": q("s", 1.25)}]}}},
        {"id": "item_echo_maul", "buy_price": 57, "tags": ["BasicAttack", "Mobility"], "stats": {"ADFlat": q("points", 3), "AttackRangeCmFlat": q("cm", 10)}, "active": None,
         "passive": {"family": "passive_cleave", "effects": [{"op": "cleave", "ratio": q("ratio", 0.25), "radius": q("cm", 60)}]}},
        {"id": "item_paper_box", "buy_price": 58, "tags": ["Mobility"], "stats": {"APFlat": q("points", 2), "MoveSpeedBonusRatio": q("ratio", 0.03)}, "passive": None,
         "active": {"target": {"mode": "Self"}, "windup": q("s", 0), "cooldown_family": "cd_paper_box",
                    "rank": {"rank": 1, "mana_cost": q("mp", 6), "cooldown": q("s", 11),
                             "effects": [{"op": "stealth", "duration": q("s", 0.75), "reveal_radius": q("cm", 60),
                                          "move_speed_penalty": q("ratio", 0.05)}]}}},
    ]
    equipment.append({"id": "item_megaphone", "buy_price": 59, "tags": ["Breach", "Control"], "stats": {"APFlat": q("points", 3), "CooldownHasteFlat": q("points", 2)}, "active": None,
                      "passive": {"family": "passive_megaphone", "effects": [{"op": "affliction_amp", "dot_ratio": q("ratio", 0.25), "slow_scale": q("ratio", 1.1)}]}})
    monsters = []
    for identity, tier, count, hp, attack, armor, speed in (
        ("monster_scavenger", "Low", 2, 25, 3, 2, 1),
        ("monster_guardian", "High", 1, 80, 9, 4, 0.75),
    ):
        monsters.append({
            "id": identity, "tier": tier, "count_per_camp": count, "hp": q("hp", hp),
            "armor": q("points", armor), "magic_resist": q("points", armor), "attack": q("points", attack),
            "attack_speed": q("attacks/s", speed), "attack_windup": q("s", 0.125),
            "attack_range": q("cm", 80), "move_speed": q("cm/s", 250), "collision_radius": q("cm", 20),
            "leash_radius": q("cm", 300), "lose_target_grace": q("s", 1), "respawn": q("s", 10),
            "reward_xp": 8 if tier == "Low" else 20, "reward_gold": 4 if tier == "Low" else 10,
            "hp_regen": q("hp/s", 0), "max_mp": q("mp", 0), "mp_regen": q("mp/s", 0),
            "ap": q("points", 0), "crit_chance": q("ratio", 0),
        })
    # The world references follow the approved world source, so a layout change needs no fixture edit.
    world_anchors = demo.parse_json((demo.ROOT / demo.WORLD_SOURCE).read_bytes(), "test world source")["anchors"]
    named = {kind: sorted(row["id"] for row in world_anchors if row["kind"] == kind) for kind in ("Spawn", "Camp", "Shop")}
    camps = {
        "monsters": monsters,
        "placements": [{"anchor_id": identity, "monster_id": "monster_guardian" if len(row_members(world_anchors, identity)) == 1
                        else "monster_scavenger"} for identity in named["Camp"]],
    }
    boss = {"id": "monster_dean", "hp": q("hp", 200), "armor": q("points", 5), "magic_resist": q("points", 5), "attack": q("points", 12),
            "attack_interval": q("s", 1.25), "attack_windup": q("s", 0.25), "attack_range": q("cm", 90), "move_speed": q("cm/s", 200),
            "collision_radius": q("cm", 20), "aggro_radius": q("cm", 150), "attacker_memory": q("s", 2), "appear_at": q("s", 20),
            "reward_xp": 30, "reward_gold": 25,
            "approval": {"duration": q("s", 15), "stats": {"ADBonusRatio": q("ratio", 0.1), "APBonusRatio": q("ratio", 0.1),
                                                           "MoveSpeedBonusRatio": q("ratio", 0.05)}}}
    # Every native status needs one display row; read them so new statuses need no fixture edit.
    statuses = list(demo.parse_json((demo.ROOT / demo.IDS_SOURCE).read_bytes(), "test native ids")["statuses"])
    identities = ["character_demo", *statuses]
    identities.extend(row["id"] for rows in (abilities, attributes, equipment, monsters, [boss]) for row in rows)
    display = [{"id": identity, "namespace": "DemoContent", "name_key": identity + ".name",
                "name_text": "测试名称 " + identity, "description_key": identity + ".description",
                "description_text": "TEST ONLY 合成说明 " + "".join(c for c in identity if not c.isdigit()),
                "icon_id": "icon_" + identity} for identity in identities]
    display[0]["portrait_icon_id"] = "portrait_character_demo"
    return {
        "identity": {"schema_version": 1, "handler_revision": 1, "projection_revision": 3,
                     "approval": {"status": "approved", "decision": "TEST ONLY: synthetic fixture, not product approval"}},
        "parameters": parameters, "formulas": formulas, "policies": policies,
        "character": {"id": "character_demo", "basic_attack": {"target_mode": "Unit", "damage_type": "Physical", "ad_ratio": q("ratio", 1)},
                      "dash": {"mana_cost": q("mp", 0), "cooldown": q("s", 10), "distance": q("cm", 120), "duration": q("s", 0.125)}},
        "abilities": abilities, "attributes": attributes, "equipment": equipment, "camps": camps, "boss": boss,
        "progression": {"max_level": 10, "max_choices": 10, "max_skill_rank": 3, "active_slots": 2, "equipment_slots": 2, "xp_start": 0,
                        "xp_thresholds": [0, 50, 110, 180, 260, 350, 450, 560, 680, 810],
                        "opening_offer": {"active_count": 2, "passive_count": 1}, "reroll_levels": [3, 6]},
        "world": {"map_id": "demo_world", "spawn_ids": named["Spawn"], "camp_ids": named["Camp"], "shop_ids": named["Shop"],
                  "capture_id": "capture"},
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
