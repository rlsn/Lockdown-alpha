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
        ("SKILL.GrowthPerLevel", "ratio", 0.1), ("SKILL.GrowthPerLevelAfter10", "ratio", 0.05),
        ("UNIT.MoveSpeedCmPerSec", "cm/s", 500), ("STAT.MoveSpeed.Min", "cm/s", 100), ("STAT.MoveSpeed.Max", "cm/s", 1200),
        ("UNIT.CollisionRadiusCm", "cm", 20), ("UNIT.BasicAttackRangeCm", "cm", 100),
        ("STAT.AttackRange.Min", "cm", 40), ("STAT.AttackRange.Max", "cm", 1000),
        ("UNIT.BasicAttackWindupSec", "s", 0.125), ("VISION.RadiusCm", "cm", 1200),
        ("SHOP.InteractRadiusCm", "cm", 200), ("CAPTURE.RadiusCm", "cm", 300),
        ("CAPTURE.SpraySec", "s", 3), ("CAPTURE.GuardSec", "s", 20),
        ("MATCH.CountdownSec", "s", 3), ("MATCH.DevelopmentSec", "s", 90), ("LIFE.RespawnSec", "s", 22), ("LIFE.ProtectionSec", "s", 2),
        ("REGEN.OutOfCombatDelaySec", "s", 4), ("REGEN.OutOfCombatMaxHPRatioPerSec", "ratio", 0.02),
        ("REGEN.OutOfCombatMaxMPRatioPerSec", "ratio", 0.01),
        ("DORM.RadiusCm", "cm", 300), ("DORM.OutOfCombatDelaySec", "s", 2), ("DORM.MaxHPRatioPerSec", "ratio", 0.08),
        ("DORM.MaxMPRatioPerSec", "ratio", 0.05),
        ("SHOP.SellRatio", "ratio", 0.5), ("CAMP.ResetArrivalRadiusCm", "cm", 50),
        ("CAMP.ReturnDeadEndSec", "s", 4), ("CAMP.GrowthStageSec", "s", 30),
        ("CAMP.GrowthHPRatioPerStage", "ratio", 0.1), ("CAMP.GrowthAttackRatioPerStage", "ratio", 0.05),
        ("CAMP.GrowthResistPerStage", "points", 1), ("CAMP.GrowthRewardXPRatioPerStage", "ratio", 0.25),
        ("AIRDROP.FirstLandSec", "s", 30), ("AIRDROP.IntervalSec", "s", 60), ("AIRDROP.AnnounceLeadSec", "s", 10),
        ("AIRDROP.PickupRadiusCm", "cm", 120),
        ("LEADER.RefreshSec", "s", 8), ("LEADER.CircleRadiusCm", "cm", 1200), ("LEADER.CircleMaxOffsetCm", "cm", 800),
        ("REWARD.AssistWindowSec", "s", 10), ("REWARD.AssistGoldRatio", "ratio", 0.4),
        ("SHOP.UpgradeStatBonusRatio", "ratio", 0.4),
        ("CHOICE.NewActiveFreeSlotWeight", "ratio", 0.75), ("CHOICE.NewActiveFullSlotWeight", "ratio", 0.35),
        ("CHOICE.FirstSlotUpgradeChance", "ratio", 0.7), ("CHOICE.FreeSlotAttributeChance", "ratio", 0.6),
        ("CHOICE.AttributeQuality.GreenChance", "ratio", 0.4), ("CHOICE.AttributeQuality.GreenMultiplier", "ratio", 0.9),
        ("CHOICE.AttributeQuality.BlueChance", "ratio", 0.35), ("CHOICE.AttributeQuality.BlueMultiplier", "ratio", 1.0),
        ("CHOICE.AttributeQuality.PurpleChance", "ratio", 0.2), ("CHOICE.AttributeQuality.PurpleMultiplier", "ratio", 1.1),
        ("CHOICE.AttributeQuality.GoldChance", "ratio", 0.05), ("CHOICE.AttributeQuality.GoldMultiplier", "ratio", 1.2),
        ("AI.RetreatHPRatio", "ratio", 0.2), ("AI.ResumeFarmHPRatio", "ratio", 0.6),
        ("AI.HighCampEntryHPRatio", "ratio", 0.85), ("AI.HighCampReserveHPRatio", "ratio", 0.3),
        ("AI.LowCampReserveHPRatio", "ratio", 0.1), ("AI.CaptureReactionBudgetSec", "s", 2),
        ("AI.DecisionIntervalSec", "s", 0.125), ("AI.RetryPolicy.InitialBackoffSec", "s", 0.125),
        ("AI.RetryPolicy.MaxBackoffSec", "s", 0.375), ("AI.ExploreRevisitIntervalSec", "s", 12),
        ("AI.EngagementBudgetSec", "s", 30),
        ("COMBAT.BreachPerStack", "points", 2), ("COMBAT.BreachSec", "s", 3),
        ("COMBAT.BurnPerSec", "points", 4), ("COMBAT.BurnAPRatioPerSec", "ratio", 0.05),
        ("COMBAT.BurnTickSec", "s", 0.25), ("COMBAT.BurnSec", "s", 2), ("COMBAT.LifestealCap", "ratio", 0.4),
    ):
        parameters[key] = q(unit, value)
    parameters.update({
        "SHOP.StartingGold": 10, "SHOP.UpgradeCost": 30, "XP.BaseToNext": 50, "XP.IncreasePerLevel": 10,
        "REWARD.PlayerXP.Base": 12, "REWARD.PlayerXP.PerLevel": 2,
        "REWARD.PlayerGold.Base": 24, "REWARD.PlayerGold.PerLevel": 3, "AI.RetryPolicy.MaxAttempts": 2,
        "CAMP.ReturnDeadEndPlans": 2, "CAMP.GrowthMaxStage": 4, "AIRDROP.RewardXP": 150, "AIRDROP.RewardGold": 100,
        "LEADER.MinLevel": 2, "LEADER.BountyGoldPerRefresh": 40, "LEADER.BountyGoldMax": 200,
        "COMBAT.BreachMaxStacks": 4, "COMBAT.BurnMaxStacks": 3,
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
        "DuplicateEquipmentPassive": "OncePerFamily",
        "MissingSourceCooldown": "ContinueServerTime", "DeadChoiceCommit": "Allowed", "ResourceMaxChange": "PreserveRatio",
        "ReleasedEffectSourceLoss": "ContinueForRemainingContender", "SourceLeave": "CancelUnsettledNoRefund",
        "InvalidAggressionProtection": "BreakOnAggressiveIntent", "CritSampling": "OncePerAttackId",
        "LifestealBasis": "PrimaryBasicAttackHPLost", "LifestealRecursiveTriggers": "Disabled",
        "BasicSecondaryTriggers": "Disabled", "RegenIntegration": "CarrySubP2Remainder", "ShopRequiresLineOfSight": False,
        "ShieldPolicy": "ReplaceIfNotWeaker", "RootBlocks": ["Move", "Dash"], "CampAggroTrigger": "ConfirmedCombatHit",
        "CampGroupRetaliation": True, "CampReturningDamageable": True, "CampReturningControllable": True,
        "CampResetPolicy": "ReturnThenReset", "CampManaPolicy": "NotApplicable",
    }
    # Synthetic #147 v7-shaped skills: two ranks, the advanced rank's growing bases 1.5x the basic rank's.
    abilities = [
        {
            "id": "active_snark_bolt", "kind": "active", "draftable": True, "cooldown_family": "cd_snark_bolt", "windup": q("s", 0.125),
            "target": {"mode": "DirectionProjectile", "range": q("cm", 350), "projectile_radius": q("cm", 15), "projectile_speed": q("cm/s", 900)},
            "ranks": [{
                "rank": rank, "mana_cost": q("mp", 5 + rank), "cooldown": q("s", 3),
                "effects": [{"op": "damage", "base": q("points", base), "ad_ratio": q("ratio", 0),
                             "ap_ratio": q("ratio", 0.125), "max_hp_ratio": q("ratio", 0), "damage_type": "Magical"},
                            {"op": "slow", "ratio": q("ratio", 0.25), "duration": q("s", 0.5)}]
                + ([{"op": "pierce"}] if rank == 2 else []),
            } for rank, base in ((1, 8), (2, 12))],
        },
        {
            "id": "active_not_mad", "kind": "active", "draftable": True, "cooldown_family": "cd_not_mad",
            "target": {"mode": "Self"}, "windup": q("s", 0),
            "ranks": [{
                "rank": rank, "mana_cost": q("mp", 4 * rank), "cooldown": q("s", 7),
                "effects": [{"op": "shield", "base": q("hp", base), "ap_ratio": q("ratio", 0.2), "duration": q("s", 1.25)},
                            {"op": "dispel_slow"}]
                + ([{"op": "dispel_root"}, {"op": "dispel_disarm"}] if rank == 2 else []),
            } for rank, base in ((1, 20), (2, 30))],
        },
        {
            "id": "active_pants_down", "kind": "active", "draftable": True, "cooldown_family": "cd_pants_down",
            "target": {"mode": "Unit", "range": q("cm", 250)}, "windup": q("s", 0.125),
            "ranks": [{
                "rank": rank, "mana_cost": q("mp", 6 + rank), "cooldown": q("s", 9),
                "effects": [{"op": "damage", "base": q("points", base), "ad_ratio": q("ratio", 0), "ap_ratio": q("ratio", 0.1), "max_hp_ratio": q("ratio", 0), "damage_type": "Magical"},
                            {"op": "root", "duration": q("s", 0.25 * rank)}],
            } for rank, base in ((1, 5), (2, 7.5))],
        },
        {
            "id": "passive_iced_americano", "kind": "passive", "draftable": True, "target": {"mode": "Self"},
            "ranks": [{"rank": rank, "effects": [{"op": "stat_bonus", "stats": {"CooldownHasteFlat": q("points", haste),
                                                                                "MPRegenFlat": q("mp/s", 0.25)}}]
                       + ([{"op": "cooldown_refund", "trigger": "BasicHit", "entries": "Actives", "cooldown": q("s", 0.5)}]
                          if rank == 2 else [])}
                      for rank, haste in ((1, 2), (2, 4))],
        },
        {
            "id": "passive_lucky_star", "kind": "passive", "draftable": True, "target": {"mode": "Self"},
            "ranks": [{"rank": rank, "effects": [{"op": "stat_bonus", "stats": {"CritChanceBonusRatio": q("ratio", value)}}]
                       + ([{"op": "ability_crit", "mode": "Chance", "ratio": q("ratio", 0.5), "abilities": ["active_pants_down"]}]
                          if rank == 2 else [])}
                      for rank, value in ((1, 0.03), (2, 0.06))],
        },
        {
            "id": "passive_kick_down", "kind": "passive", "draftable": True, "target": {"mode": "Self"},
            "ranks": [{"rank": 1, "effects": [{"op": "control_damage_amp", "ratio": q("ratio", 0.1)}]},
                      {"rank": 2, "effects": [{"op": "control_damage_amp", "ratio": q("ratio", 0.15), "hp_below": q("ratio", 0.5)}]}],
        },
    ]
    attributes = [
        {"id": "attr_protein", "stats": {"ADFlat": q("points", 2), "MaxHPFlat": q("hp", 10)}},
        {"id": "attr_all_nighter", "stats": {"APFlat": q("points", 4), "CooldownHasteFlat": q("points", 2)}},
        {"id": "attr_morning_rush", "stats": {"MoveSpeedBonusRatio": q("ratio", 0.02), "MaxHPFlat": q("hp", 20)}},
        {"id": "attr_canteen", "stats": {"MaxHPFlat": q("hp", 10), "ArmorFlat": q("points", 1), "MagicResistFlat": q("points", 1)}},
        {"id": "attr_hand_out", "stats": {"AttackRangeCmFlat": q("cm", 10), "AttackSpeedBonusRatio": q("ratio", 0.02)}},
        {"id": "attr_wild_frenzy", "stats": {"ADFlat": q("points", 3), "AttackSpeedBonusRatio": q("ratio", 0.04)},
         "penalties": {"MaxHPFlat": q("hp", 5)}},
    ]
    opening_cards = [
        {"id": "opening_athlete", "stats": {"ADFlat": q("points", 3), "AttackSpeedBonusRatio": q("ratio", 0.04), "MaxHPFlat": q("hp", 20)}},
        {"id": "opening_grinder", "stats": {"APFlat": q("points", 6), "CooldownHasteFlat": q("points", 4), "MaxMPFlat": q("mp", 10)}},
        {"id": "opening_keyboard", "stats": {"AttackRangeCmFlat": q("cm", 20), "AttackSpeedBonusRatio": q("ratio", 0.04),
                                             "MoveSpeedBonusRatio": q("ratio", 0.02)}},
    ]
    equipment = [
        {"id": "item_iron_plate", "buy_price": 51, "stats": {"MaxHPFlat": q("hp", 30), "ArmorFlat": q("points", 4)},
         "passive": {"family": "passive_crit_guard", "effects": [{"op": "crit_damage_reduction", "ratio": q("ratio", 0.1)}]}},
        {"id": "item_edge_blade", "buy_price": 52, "stats": {"ADFlat": q("points", 4), "AttackSpeedBonusRatio": q("ratio", 0.05)},
         "passive": {"family": "passive_third_strike", "effects": [{"op": "nth_basic_bonus", "count": 3, "base": q("points", 5), "damage_type": "Physical"}]}},
        {"id": "item_runic_focus", "buy_price": 53, "stats": {"APFlat": q("points", 6), "CooldownHasteFlat": q("points", 5)},
         "passive": {"family": "passive_mana_refund", "effects": [{"op": "mana_on_hit", "trigger": "AbilityHit", "mana": q("mp", 2)}]}},
        {"id": "item_siphon_emblem", "buy_price": 54, "stats": {"ADFlat": q("points", 2), "MagicResistFlat": q("points", 3)},
         "passive": {"family": "passive_siphon", "effects": [{"op": "lifesteal", "ratio": q("ratio", 0.1)}],
                     "upgraded": {"effects": [{"op": "lifesteal", "ratio": q("ratio", 0.15)}]}}},
        {"id": "item_phase_anchor", "buy_price": 55, "stats": {"MoveSpeedBonusRatio": q("ratio", 0.05)},
         "passive": {"family": "passive_stride", "effects": [{"op": "dash_boost", "cooldown_ratio": q("ratio", 0.5), "distance_bonus": q("ratio", 0.25)}]}},
        {"id": "item_arcane_battery", "buy_price": 56, "stats": {"MaxMPFlat": q("mp", 20), "MagicResistFlat": q("points", 2)}, "passive": None},
        {"id": "item_echo_maul", "buy_price": 57, "stats": {"ADFlat": q("points", 3), "AttackRangeCmFlat": q("cm", 10)},
         "passive": {"family": "passive_cleave", "effects": [{"op": "cleave", "ratio": q("ratio", 0.25), "radius": q("cm", 60)}]}},
    ]
    equipment.append({"id": "item_megaphone", "buy_price": 59, "stats": {"APFlat": q("points", 3), "CooldownHasteFlat": q("points", 2)},
                      "passive": {"family": "passive_megaphone", "effects": [{"op": "affliction_amp", "dot_ratio": q("ratio", 0.25), "slow_scale": q("ratio", 1.1)}]}})
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
            "reward_xp": {"Low": 8, "High": 20, "Elite": 40}[tier], "reward_gold": {"Low": 4, "High": 10, "Elite": 20}[tier],
            "hp_regen": q("hp/s", 0), "max_mp": q("mp", 0), "mp_regen": q("mp/s", 0),
            "ap": q("points", 0), "crit_chance": q("ratio", 0),
        })
    # The world references follow the approved world source, so a layout change needs no fixture edit.
    world_anchors = demo.parse_json((demo.ROOT / demo.WORLD_SOURCE).read_bytes(), "test world source")["anchors"]
    named = {kind: sorted(row["id"] for row in world_anchors if row["kind"] == kind) for kind in ("Spawn", "Camp", "Shop")}
    camps = {
        "monsters": monsters,
        # Two member homes hold the Low pair; single-member camps are Elite when their id says so (e01..), else High.
        "placements": [{"anchor_id": identity, "monster_id": "monster_scavenger" if len(row_members(world_anchors, identity)) != 1
                        else "monster_advisor" if identity.startswith("e") else "monster_guardian"} for identity in named["Camp"]],
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
    identities.extend(row["id"] for rows in (abilities, attributes, opening_cards, equipment, monsters, [boss]) for row in rows)
    display = [{"id": identity, "namespace": "DemoContent", "name_key": identity + ".name",
                "name_text": "测试名称 " + identity, "description_key": identity + ".description",
                "description_text": "TEST ONLY 合成说明 " + "".join(c for c in identity if not c.isdigit()),
                "icon_id": "icon_" + identity} for identity in identities]
    display[0]["portrait_icon_id"] = "portrait_character_demo"
    # Skill cards: a [op.field] placeholder on the basic line and an advanced line for the top rank.
    for row in display:
        if row["id"] in {ability["id"] for ability in abilities}:
            row["advanced_key"] = row["id"] + ".advanced"
            row["advanced_text"] = "TEST ONLY 高级说明 " + row["id"]
    snark = next(row for row in display if row["id"] == "active_snark_bolt")
    snark["description_text"] += " [damage.amount] [slow.ratio]"
    snark["advanced_text"] += " [slow.duration]"
    return {
        "identity": {"schema_version": 1, "handler_revision": 1, "projection_revision": 3,
                     "approval": {"status": "approved", "decision": "TEST ONLY: synthetic fixture, not product approval"}},
        "parameters": parameters, "formulas": formulas, "policies": policies,
        "character": {"id": "character_demo", "basic_attack": {"target_mode": "Unit", "damage_type": "Physical", "ad_ratio": q("ratio", 1)},
                      "dash": {"mana_cost": q("mp", 0), "cooldown": q("s", 10), "distance": q("cm", 120), "duration": q("s", 0.125)}},
        "abilities": abilities, "attributes": attributes, "opening_cards": opening_cards, "equipment": equipment, "camps": camps, "boss": boss,
        "progression": {"max_level": 16, "max_choices": 16, "max_skill_rank": 2, "active_slots": 3, "equipment_slots": 2, "xp_start": 0,
                        "xp_thresholds": [0, 50, 110, 180, 260, 350, 450, 560, 680, 810, 950, 1100, 1260, 1430, 1610, 1800],
                        "opening_offer": {"matched_skill_weight": 3, "other_skill_weight": 2}, "reroll_levels": [5, 11]},
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
