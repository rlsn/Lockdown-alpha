"""Export compiler-backed, TEST-ONLY inputs for Lockdown.Rules.

Nothing here is a production content source or an approval decision. All output
must be a new directory under this worktree's ignored Artifacts directory.

With --cache the export is kept under that directory, keyed by the hash of every input it reads (this script and the
Scripts modules it imports, SourceData, the generated Demo data and the native id and contract headers), and reused
while the key and every exported file's hash still match. The runtime profile writes only the four cases needed by
rules outside the heavy data-import group; the full profile writes every valid mutation and refusal case.
"""
import argparse
from concurrent.futures import ProcessPoolExecutor
from copy import deepcopy
from decimal import Decimal
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import sys
import time

from CatalogTools import encode
from CIChangeScope import python_closure
import DemoContent as demo
import DemoContentTestFixtures as fixtures
import DemoWorld
from SyncDemoContentData import write_outputs
from SyncDemoWorldData import synchronize as synchronize_world


FORMULAS = (
    "None", "AddFlatThenAdditivePercent", "PointsOverScalePlusPoints", "OncePerPrimaryAttack",
    "FloorP2ThenShieldThenHP", "BaseTimesAdditiveRatio", "BasePlusFlat", "CeilReciprocalMs",
    "CeilBaseWindupTimesBaseOverCurrentAS", "CeilBaseTimesScaleOverScalePlusHaste",
    "CarrySubP2Remainder", "FloorFinalRatioRejectAliveUnderflow", "FloorPurchasePriceTimesSellRatio",
    "LinearIncrement", "MinVisionTwiceAttackRange",
)
POLICIES = (
    "None", "AddFlatThenAdditivePercent", "Allowed", "OncePerFamily",
    "ContinueServerTime", "PreserveRatio", "ContinueForRemainingContender", "CancelUnsettledNoRefund",
    "BreakOnAggressiveIntent", "OncePerAttackId", "PrimaryBasicAttackHPLost", "Disabled",
    "CarrySubP2Remainder", "ReplaceIfNotWeaker", "ConfirmedCombatHit", "ReturnThenReset", "NotApplicable",
)
BONUS_STATS = (
    "ADFlat", "APFlat", "MaxHPFlat", "MaxMPFlat", "ArmorFlat", "MagicResistFlat", "CooldownHasteFlat",
    "AttackRangeCmFlat", "AttackSpeedBonusRatio", "MoveSpeedBonusRatio", "CritChanceBonusRatio", "ADBonusRatio",
    "HPRegenFlat", "APBonusRatio", "MPRegenFlat",
)
TARGETS = ("Self", "Unit", "DirectionProjectile", "DirectionDash", "GroundPoint", "DirectionCone")
BUNDLE_PATHS = (
    demo.MANIFEST_OUTPUT, demo.CONTENT_OUTPUT, demo.WORLD_OUTPUT, demo.CONTENT_CONTRACT,
    demo.WORLD_CONTRACT, demo.SCHEMA_SOURCE, demo.IDS_SOURCE, demo.NATIVE_IDS,
    demo.WORLD_SOURCE, demo.WORLD_SCHEMA_SOURCE,
)
PROFILES = ("runtime", "full")


def scalar(value):
    return value[next(key for key in ("p2", "bp", "ms") if key in value)]


def expected_world_projection(compilation):
    world = json.loads(compilation.inputs[demo.WORLD_OUTPUT])

    def point(value=None):
        return {axis.upper(): rational(None if value is None else value[axis]) for axis in ("x", "y", "z")}

    return {
        "MapId": world["map_id"], "LayoutRevision": world["layout_revision"],
        "GeometryRevision": world["geometry_revision"],
        "SourceDigest": {"Bytes": list(bytes.fromhex(world["source_sha256"]))},
        "AuthorSchemaDigest": {"Bytes": list(bytes.fromhex(world["author_schema_sha256"]))},
        "ApprovalDecision": world["approval"]["decision"],
        "Surfaces": [{"Id": row["id"], "MinCm": point(row["min_cm"]), "MaxCm": point(row["max_cm"])}
                     for row in world["geometry"]["surfaces"]],
        "LevelStepCm": rational(world["geometry"]["elevation"]["level_step_cm"]),
        "Plateaus": [{"Id": row["id"], "Level": row["level"], "PolygonCm": [point(vertex) for vertex in row["polygon_cm"]]}
                     for row in world["geometry"]["elevation"]["plateaus"]],
        "Ramps": [{"Id": row["id"], "LowerLevel": row["lower_level"], "PolygonCm": [point(vertex) for vertex in row["polygon_cm"]]}
                  for row in world["geometry"]["elevation"]["ramps"]],
        "Blockers": [{
            "Id": row["id"], "SurfaceId": row["surface_id"], "Shape": ("polygon", "cylinder").index(row["shape"]),
            "PolygonCm": [point(vertex) for vertex in row.get("polygon_cm", [])],
            "CenterCm": point(row.get("center_cm")), "RadiusCm": rational(row.get("radius_cm")),
            "HeightCm": rational(row["height_cm"]), "BlocksMovement": row["blocks_movement"],
            "BlocksSight": row["blocks_sight"],
        } for row in world["geometry"]["blockers"]],
        "SightSampleHeightCm": rational(world["geometry"]["sight_sample_height_cm"]),
        "CampClearanceCm": rational(world["geometry"]["camp_clearance_cm"]),
        "SpawnOffsetsCm": [point(value) for value in world["placement"]["spawn_offsets_cm"]],
        "SpawnValidationRadiusCm": rational(world["placement"]["spawn_validation_radius_cm"]),
        "ParameterRefs": world["parameter_refs"],
        "Anchors": [{
            "Id": row["id"], "Kind": ("Spawn", "Camp", "Shop", "Capture", "Dorm", "Airdrop", "BossWaypoint").index(row["kind"]),
            "SurfaceId": row["surface_id"], "PositionCm": point(row["position_cm"]),
            "Members": [{"Id": member["id"], "PositionCm": point(member["position_cm"])}
                        for member in row.get("members", [])],
        } for row in world["anchors"]],
    }


def rational(value=None):
    return {"Numerator": 0, "Denominator": 1} if value is None else {
        "Numerator": value["numerator"], "Denominator": value["denominator"],
    }


# Rational stats keep their exact amount in Cm (range in cm, HP regeneration in hp/s) and leave Amount 0.
RATIONAL_STATS = frozenset(("AttackRangeCmFlat", "HPRegenFlat", "MPRegenFlat"))


def stats(values):
    return [{"Stat": BONUS_STATS.index(key), "Amount": 0 if key in RATIONAL_STATS else scalar(value),
             "Cm": rational(value if key in RATIONAL_STATS else None)}
            for key, value in sorted(values.items())]


def target(value=None):
    value = {} if value is None else value
    return {
        "Mode": TARGETS.index(value.get("mode", "Self")),
        "RangeCm": rational(value.get("range")),
        "ProjectileRadiusCm": rational(value.get("projectile_radius")),
        "ProjectileSpeedCmPerSec": rational(value.get("projectile_speed")),
        "AngleDegrees": rational(value.get("angle")),
        "AttackRangeRatioBp": scalar(value["attack_range_ratio"]) if "attack_range_ratio" in value else 0,
        "AttackRangeBonusCapCm": rational(value.get("attack_range_bonus_cap")),
    }


TRIGGERS = ("None", "BasicHit", "AbilityHit", "Damaged")
COOLDOWN_ENTRIES = ("None", "Actives")
CRIT_MODES = ("None", "Chance")


def effect(value):
    result = {
        "Opcode": value["opcode"], "BaseP2": 0, "ADRatioBp": 0, "APRatioBp": 0, "MaxHPRatioBp": 0, "DamageType": 0,
        "DurationMs": 0, "DelayMs": 0, "RatioBp": 0,
        "DistanceCm": rational(), "RadiusCm": rational(), "CooldownRatioBp": 0, "DistanceBonusBp": 0, "Stats": [],
        "ArmorRatioBp": 0, "IntervalMs": 0, "CriticalMultiplierBp": 0,
        "Trigger": 0, "Entries": 0, "CritMode": 0, "AmountMs": 0,
        "MultiplierBp": 0, "Count": 0, "BasicCount": 0, "SkillCooldownRatioBp": 0,
        "SkillDistanceBonusBp": 0, "AbilityIds": [], "SecondaryRatioBp": 0, "SecondaryDurationMs": 0,
        "LimitP2": 0, "bCleanseSlow": False,
    }
    for source, dest in (
        ("base", "BaseP2"), ("ad_ratio", "ADRatioBp"), ("ap_ratio", "APRatioBp"), ("max_hp_ratio", "MaxHPRatioBp"),
        ("duration", "DurationMs"), ("delay", "DelayMs"), ("ratio", "RatioBp"), ("threshold", "RatioBp"),
        ("cooldown_ratio", "CooldownRatioBp"), ("distance_bonus", "DistanceBonusBp"),
        ("armor_ratio", "ArmorRatioBp"), ("interval", "IntervalMs"), ("critical_multiplier", "CriticalMultiplierBp"),
        ("cooldown", "AmountMs"), ("mana", "BaseP2"), ("skill_cooldown_ratio", "SkillCooldownRatioBp"),
        ("skill_distance_bonus", "SkillDistanceBonusBp"),
        # #112 handlers.
        ("dot_ratio", "RatioBp"), ("slow", "SecondaryRatioBp"),
        ("slow_scale", "SecondaryRatioBp"), ("slow_duration", "SecondaryDurationMs"),
        ("linger", "SecondaryDurationMs"), ("internal_cooldown", "IntervalMs"),
        # #147 build v7 terms.
        ("hp_below", "SecondaryRatioBp"), ("slowed_stun", "SecondaryDurationMs"), ("wall_stun", "SecondaryDurationMs"),
        ("cleave_ratio", "SecondaryRatioBp"), ("burst_ratio", "SecondaryRatioBp"), ("low_hp_multiplier", "MultiplierBp"),
        ("lifesteal", "SecondaryRatioBp"),
        ("outer_ratio", "RatioBp"), ("move_bonus", "SecondaryRatioBp"),
        ("mana_ratio", "SecondaryRatioBp"), ("mana_cap", "LimitP2"), ("slow_resistance", "SecondaryRatioBp"),
    ):
        if source in value:
            result[dest] = scalar(value[source])
    for source in ("count", "stacks", "charges"):
        if source in value:
            result["Count"] = value[source]
    if "basic_stacks" in value:
        result["BasicCount"] = value["basic_stacks"]
    for source, dest in (("distance", "DistanceCm"), ("radius", "RadiusCm"),
                         ("half_width", "RadiusCm"), ("wall_distance", "RadiusCm"),
                         ("cleave_radius", "RadiusCm"), ("burst_radius", "RadiusCm"),
                         ("inner_radius", "DistanceCm"), ("trigger_radius", "DistanceCm")):
        if source in value:
            result[dest] = rational(value[source])
    if "damage_type" in value:
        result["DamageType"] = ("Physical", "Magical").index(value["damage_type"])
    if "stats" in value:
        result["Stats"] = stats(value["stats"])
    # A reflection answers primary basics only; its trigger names no native selector.
    if "trigger" in value and value.get("op") != "thorns":
        result["Trigger"] = TRIGGERS.index(value["trigger"])
    if "entries" in value:
        result["Entries"] = COOLDOWN_ENTRIES.index(value["entries"])
    if "mode" in value:
        result["CritMode"] = CRIT_MODES.index(value["mode"])
    if "abilities" in value:
        result["AbilityIds"] = list(value["abilities"])
    if "cleanse_slow" in value:
        result["bCleanseSlow"] = value["cleanse_slow"]
    if value.get("op") == "channel_cone":
        result["DurationMs"] = result["IntervalMs"] * (result["Count"] - 1)
    return result


def rank(value=None):
    value = {} if value is None else value
    return {
        "Rank": value.get("rank", 0),
        "ManaCostP2": scalar(value["mana_cost"]) if "mana_cost" in value else 0,
        "CooldownMs": scalar(value["cooldown"]) if "cooldown" in value else 0,
        "Effects": [effect(row) for row in value.get("effects", [])],
    }


def dash_definition(value):
    return {
        "Target": target(value["target"]), "WindupMs": 0, "CooldownFamily": "", "Rank": rank(value["rank"]),
    }




def passive(value):
    return {
        "Family": value["family"] if value else "",
        "Effects": [effect(item) for item in value["effects"]] if value else [],
        "HasUpgraded": bool(value) and "upgraded" in value,
        "UpgradedEffects": [effect(item) for item in value["upgraded"]["effects"]] if value and "upgraded" in value else [],
    }


def parameter_projection(parameters):
    result = {}
    for stat in ("MaxHP", "MaxMP", "AD", "AP", "Armor", "MagicResist", "HPRegen", "MPRegen"):
        rate = stat in ("HPRegen", "MPRegen")
        result[stat] = {
            suffix + ("" if rate else "P2"): (rational if rate else scalar)(parameters[f"STAT.{stat}.{suffix}"])
            for suffix in ("Base", "PerLevel", "Min", "Max")
        }
    for dest, base, prefix in (
        ("AttackSpeed", "STAT.BaseAttackSpeed", "STAT.AttackSpeed"),
        ("MoveSpeedCmPerSec", "UNIT.MoveSpeedCmPerSec", "STAT.MoveSpeed"),
        ("AttackRangeCm", "UNIT.BasicAttackRangeCm", "STAT.AttackRange"),
    ):
        result[dest] = {"Base": rational(parameters[base]), "Min": rational(parameters[prefix + ".Min"]),
                        "Max": rational(parameters[prefix + ".Max"])}
    for source, dest in (
        ("STAT.AttackSpeedPerLevelRatio", "AttackSpeedPerLevelBp"), ("STAT.CritChance.Base", "CritChanceBaseBp"),
        ("STAT.CritMultiplier", "CritMultiplierBp"), ("STAT.AbilityCritMultiplier", "AbilityCritMultiplierBp"), ("STAT.CooldownHaste.Base", "CooldownHasteBaseP2"),
        ("STAT.CooldownHaste.Max", "CooldownHasteMaxP2"), ("SKILL.GrowthPerLevel", "SkillGrowthPerLevelBp"),
        ("SKILL.GrowthPerLevelAfter10", "SkillGrowthPerLevelAfter10Bp"), ("UNIT.BasicAttackWindupSec", "BasicAttackWindupMs"),
        ("CAPTURE.SpraySec", "CaptureSprayMs"), ("CAPTURE.GuardSec", "CaptureGuardMs"),
        ("MATCH.CountdownSec", "CountdownMs"), ("MATCH.DevelopmentSec", "DevelopmentMs"), ("LIFE.RespawnSec", "RespawnMs"), ("LIFE.ProtectionSec", "ProtectionMs"),
        ("SHOP.SellRatio", "SellRatioBp"), ("REGEN.OutOfCombatDelaySec", "OutOfCombatRegenDelayMs"),
        ("REWARD.AssistWindowSec", "AssistWindowMs"), ("REWARD.AssistGoldRatio", "AssistGoldBp"),
        ("SHOP.SellRatio", "SellRatioBp"), ("SHOP.UpgradeStatBonusRatio", "UpgradeStatBonusBp"),
        ("REGEN.OutOfCombatDelaySec", "OutOfCombatRegenDelayMs"),
        ("REGEN.OutOfCombatMaxHPRatioPerSec", "OutOfCombatMaxHPRegenBp"),
        ("REGEN.OutOfCombatMaxMPRatioPerSec", "OutOfCombatMaxMPRegenBp"),
        ("DORM.OutOfCombatDelaySec", "DormRegenDelayMs"),
        ("DORM.MaxHPRatioPerSec", "DormMaxHPRegenBp"), ("DORM.MaxMPRatioPerSec", "DormMaxMPRegenBp"),
        ("CAMP.ReturnDeadEndSec", "CampReturnDeadEndMs"), ("CAMP.GrowthStageSec", "CampGrowthStageMs"),
        ("CAMP.GrowthHPRatioPerStage", "CampGrowthHPPerStageBp"), ("CAMP.GrowthAttackRatioPerStage", "CampGrowthAttackPerStageBp"),
        ("CAMP.GrowthResistPerStage", "CampGrowthResistPerStageP2"), ("CAMP.GrowthRewardXPRatioPerStage", "CampGrowthRewardXPPerStageBp"),
        ("AIRDROP.FirstLandSec", "AirdropFirstLandMs"), ("AIRDROP.IntervalSec", "AirdropIntervalMs"),
        ("AIRDROP.AnnounceLeadSec", "AirdropAnnounceLeadMs"), ("LEADER.RefreshSec", "LeaderRefreshMs"),
        ("COMBAT.BreachPerStack", "BreachPerStackP2"), ("COMBAT.BreachSec", "BreachMs"),
        ("COMBAT.BurnPerSec", "BurnPerSecP2"), ("COMBAT.BurnAPRatioPerSec", "BurnAPRatioBp"),
        ("COMBAT.BurnTickSec", "BurnTickMs"), ("COMBAT.BurnSec", "BurnMs"), ("COMBAT.LifestealCap", "LifestealCapBp"),
    ):
        result[dest] = scalar(parameters[source])
    for source, dest in (
        ("UNIT.CollisionRadiusCm", "CollisionRadiusCm"), ("VISION.RadiusCm", "VisionRadiusCm"),
        ("SHOP.InteractRadiusCm", "ShopInteractRadiusCm"), ("CAPTURE.RadiusCm", "CaptureRadiusCm"),
        ("CAMP.ResetArrivalRadiusCm", "CampResetArrivalRadiusCm"),
        ("DORM.RadiusCm", "DormRadiusCm"), ("AIRDROP.PickupRadiusCm", "AirdropPickupRadiusCm"),
        ("LEADER.CircleRadiusCm", "LeaderCircleRadiusCm"), ("LEADER.CircleMaxOffsetCm", "LeaderCircleMaxOffsetCm"),
    ):
        result[dest] = rational(parameters[source])
    for source, dest in (
        ("SHOP.StartingGold", "StartingGold"), ("SHOP.UpgradeCost", "UpgradeCost"),
        ("XP.BaseToNext", "XPBaseToNext"), ("XP.IncreasePerLevel", "XPIncreasePerLevel"),
        ("REWARD.PlayerXP.Base", "PlayerXPBase"), ("REWARD.PlayerXP.PerLevel", "PlayerXPPerLevel"),
        ("REWARD.PlayerGold.Base", "PlayerGoldBase"), ("REWARD.PlayerGold.PerLevel", "PlayerGoldPerLevel"),
        ("CAMP.ReturnDeadEndPlans", "CampReturnDeadEndPlans"), ("CAMP.GrowthMaxStage", "CampGrowthMaxStage"),
        ("AIRDROP.RewardXP", "AirdropRewardXP"), ("AIRDROP.RewardGold", "AirdropRewardGold"),
        ("LEADER.MinLevel", "LeaderMinLevel"), ("LEADER.BountyGoldPerRefresh", "LeaderBountyGoldPerRefresh"),
        ("LEADER.BountyGoldMax", "LeaderBountyGoldMax"),
        ("COMBAT.BreachMaxStacks", "BreachMaxStacks"), ("COMBAT.BurnMaxStacks", "BurnMaxStacks"),
    ):
        result[dest] = parameters[source]
    result["AI"] = {}
    for source, dest in (
        ("RetreatHPRatio", "RetreatHPBp"), ("ResumeFarmHPRatio", "ResumeFarmHPBp"),
        ("HighCampEntryHPRatio", "HighCampEntryHPBp"), ("HighCampReserveHPRatio", "HighCampReserveHPBp"),
        ("LowCampReserveHPRatio", "LowCampReserveHPBp"), ("CaptureReactionBudgetSec", "CaptureReactionBudgetMs"),
        ("DecisionIntervalSec", "DecisionIntervalMs"), ("RetryPolicy.InitialBackoffSec", "RetryInitialBackoffMs"),
        ("RetryPolicy.MaxBackoffSec", "RetryMaxBackoffMs"), ("ExploreRevisitIntervalSec", "ExploreRevisitIntervalMs"),
        ("EngagementBudgetSec", "EngagementBudgetMs"),
    ):
        result["AI"][dest] = scalar(parameters["AI." + source])
    result["AI"]["RetryMaxAttempts"] = parameters["AI.RetryPolicy.MaxAttempts"]
    result["Choice"] = {dest: scalar(parameters["CHOICE." + source]) for source, dest in (
        ("NewActiveFreeSlotWeight", "NewActiveFreeSlotWeightBp"),
        ("NewActiveFullSlotWeight", "NewActiveFullSlotWeightBp"),
        ("FirstSlotUpgradeChance", "FirstSlotUpgradeBp"),
        ("FreeSlotAttributeChance", "FreeSlotAttributeBp"),
        ("AttributeQuality.GreenChance", "AttributeGreenChanceBp"),
        ("AttributeQuality.GreenMultiplier", "AttributeGreenMultiplierBp"),
        ("AttributeQuality.BlueChance", "AttributeBlueChanceBp"),
        ("AttributeQuality.BlueMultiplier", "AttributeBlueMultiplierBp"),
        ("AttributeQuality.PurpleChance", "AttributePurpleChanceBp"),
        ("AttributeQuality.PurpleMultiplier", "AttributePurpleMultiplierBp"),
        ("AttributeQuality.GoldChance", "AttributeGoldChanceBp"),
        ("AttributeQuality.GoldMultiplier", "AttributeGoldMultiplierBp"),
    )}
    return result


def expected_projection(compilation):
    """Independent named-member assertions, not source JSON/hash-only assertions."""
    data = json.loads(compilation.outputs[demo.CONTENT_OUTPUT])
    manifest = json.loads(compilation.outputs[demo.MANIFEST_OUTPUT])
    identity = {
        "Root": compilation.digest.hex(), "Content": manifest["content"]["sha256"], "World": manifest["world"]["sha256"],
        "AuthorSchema": manifest["author_schema_sha256"], "NativeIds": manifest["native_ids_sha256"],
        "NativeContract": manifest["native_contract_sha256"], "Display": manifest["display_sha256"],
        "WorldContract": manifest["world"]["contract"]["sha256"],
    }
    identity = {key: {"Bytes": list(bytes.fromhex(value))} for key, value in identity.items()}
    identity.update({
        "SchemaVersion": manifest["schema_version"], "HandlerRevision": manifest["handler_revision"],
        "ProjectionRevision": manifest["projection_revision"], "WorldSchemaVersion": manifest["world"]["contract"]["schema_version"],
        "WorldProjectionRevision": manifest["world"]["contract"]["projection_revision"],
        "ApprovalStatus": data["identity"]["approval"]["status"], "ApprovalDecision": data["identity"]["approval"]["decision"],
    })
    formulas = {key: FORMULAS.index(value) for key, value in data["formulas"].items() if type(value) is str}
    formulas.update({key + "P2": scalar(data["formulas"][key]) for key in ("ResistanceScale", "HasteScale")})
    policies = {key: POLICIES.index(value) if type(value) is str else
                [("Move", "Dash").index(entry) for entry in value] if type(value) is list else value
                for key, value in data["policies"].items()}
    character = data["character"]
    dash = character["dash"]
    dash_opcode = json.loads(compilation.inputs[demo.IDS_SOURCE])["effect_ops"]["dash"]
    innate_dash = dash_definition({"target": {"mode": "DirectionDash"}, "rank": {
        "rank": 1, "mana_cost": dash["mana_cost"], "cooldown": dash["cooldown"],
        "effects": [{"opcode": dash_opcode, "distance": dash["distance"], "duration": dash["duration"]}]}})
    result = {
        "Identity": identity, "Parameters": parameter_projection(data["parameters"]), "Formulas": formulas, "Policies": policies,
        "Character": {"Id": character["id"], "NativeId": character["native_id"], "BasicTarget": 1, "BasicDamageType": 0,
                      "BasicADRatioBp": scalar(character["basic_attack"]["ad_ratio"]),
                      "BasicIntervalMs": character["basic_attack"]["interval_ms"], "Dash": innate_dash},
        "Abilities": [{
            "Id": row["id"], "NativeId": row["native_id"], "Kind": ("active", "passive").index(row["kind"]),
            "Draftable": row["draftable"], "Target": target(row["target"]), "WindupMs": scalar(row["windup"]) if "windup" in row else 0,
            "CooldownFamily": row.get("cooldown_family", ""), "Ranks": [rank(item) for item in row["ranks"]],
        } for row in data["abilities"]],
        "Attributes": [{"Id": row["id"], "NativeId": row["native_id"], "Stats": stats(row["stats"]),
                        "Penalties": stats(row.get("penalties", {}))} for row in data["attributes"]],
        "OpeningCards": [{"Id": row["id"], "Stats": stats(row["stats"])} for row in data["opening_cards"]],
        "Equipment": [{
            "Id": row["id"], "NativeId": row["native_id"], "BuyPrice": row["buy_price"], "SellGold": row["sell_gold"],
            "Stats": stats(row["stats"]), "UpgradedStats": stats(row["upgraded_stats"]),
            "HasPassive": row["passive"] is not None, "Passive": passive(row["passive"]),
        } for row in data["equipment"]],
        "Monsters": [], "Camps": [{"AnchorId": row["anchor_id"], "MonsterId": row["monster_id"]} for row in data["camps"]["placements"]],
        "Progression": {**{dest: data["progression"][source] for source, dest in (
            ("max_level", "MaxLevel"), ("max_choices", "MaxChoices"), ("max_skill_rank", "MaxSkillRank"),
            ("active_slots", "ActiveSlots"), ("equipment_slots", "EquipmentSlots"), ("xp_start", "XPStart"),
            ("xp_thresholds", "XPThresholds"), ("xp_to_next", "XPToNext"),
        )}, **{dest: data["progression"]["opening_offer"][source] for source, dest in (
            ("matched_skill_weight", "OpeningMatchedSkillWeight"), ("other_skill_weight", "OpeningOtherSkillWeight"),
        )}, "RerollLevels": data["progression"]["reroll_levels"]},
        "World": {dest: data["world"][source] for source, dest in (
            ("map_id", "MapId"), ("spawn_ids", "SpawnIds"), ("camp_ids", "CampIds"), ("shop_ids", "ShopIds"), ("capture_id", "CaptureId"),
        )},
        "Display": [{
            "Id": row["id"], "Namespace": row["namespace"], "NameKey": row["name_key"], "NameText": row["name_text"],
            "DescriptionKey": row["description_key"], "DescriptionText": row["description_text"],
            "AdvancedKey": row.get("advanced_key", ""), "AdvancedText": row.get("advanced_text", ""),
            "IconId": row["icon_id"], "PortraitIconId": row.get("portrait_icon_id", ""),
        } for row in data["display"]],
    }
    for row in data["camps"]["monsters"]:
        monster = {"Id": row["id"], "NativeId": row["native_id"], "Tier": {"Low": 0, "High": 1, "Elite": 3}[row["tier"]]}
        for source, dest in (
            ("count_per_camp", "CountPerCamp"), ("attack_interval_ms", "AttackIntervalMs"), ("reward_xp", "RewardXP"), ("reward_gold", "RewardGold"),
        ):
            monster[dest] = row[source]
        for source, dest in (
            ("hp", "HPP2"), ("armor", "ArmorP2"), ("magic_resist", "MagicResistP2"), ("attack", "AttackP2"),
            ("attack_windup", "AttackWindupMs"), ("lose_target_grace", "LoseTargetGraceMs"),
            ("respawn", "RespawnMs"), ("max_mp", "MaxMPP2"), ("ap", "APP2"), ("crit_chance", "CritChanceBp"),
        ):
            monster[dest] = scalar(row[source])
        for source, dest in (
            ("attack_speed", "AttackSpeed"), ("attack_range", "AttackRangeCm"), ("move_speed", "MoveSpeedCmPerSec"),
            ("collision_radius", "CollisionRadiusCm"), ("leash_radius", "LeashRadiusCm"), ("perception_radius", "PerceptionRadiusCm"),
            ("hp_regen", "HPRegenPerSec"), ("mp_regen", "MPRegenPerSec"),
        ):
            monster[dest] = rational(row[source])
        result["Monsters"].append(monster)
    # D26 (#79): the Boss block is its rules plus one Boss-tier monster row after the camp tiers.
    boss = data["boss"]
    interval = scalar(boss["attack_interval"])
    divisor = math.gcd(1000, interval)
    result["Boss"] = {"Id": boss["id"], "AppearAtMs": scalar(boss["appear_at"]), "AggroRadiusCm": rational(boss["aggro_radius"]),
                      "AttackerMemoryMs": scalar(boss["attacker_memory"]),
                      "ApprovalDurationMs": scalar(boss["approval"]["duration"]), "ApprovalStats": stats(boss["approval"]["stats"])}
    result["Monsters"].append({
        "Id": boss["id"], "NativeId": boss["native_id"], "Tier": 2, "CountPerCamp": 1, "AttackIntervalMs": interval,
        "RewardXP": boss["reward_xp"], "RewardGold": boss["reward_gold"], "HPP2": scalar(boss["hp"]), "ArmorP2": scalar(boss["armor"]),
        "MagicResistP2": scalar(boss["magic_resist"]), "AttackP2": scalar(boss["attack"]), "AttackWindupMs": scalar(boss["attack_windup"]),
        "LoseTargetGraceMs": scalar(boss["attacker_memory"]), "RespawnMs": 0, "MaxMPP2": 0, "APP2": 0, "CritChanceBp": 0,
        "AttackSpeed": {"Numerator": 1000 // divisor, "Denominator": interval // divisor}, "AttackRangeCm": rational(boss["attack_range"]),
        "MoveSpeedCmPerSec": rational(boss["move_speed"]), "CollisionRadiusCm": rational(boss["collision_radius"]),
        "LeashRadiusCm": rational(boss["aggro_radius"]), "PerceptionRadiusCm": rational(boss["aggro_radius"]),
        "HPRegenPerSec": rational(), "MPRegenPerSec": rational(),
    })
    return result


def flatten(value, path="", result=None):
    result = {} if result is None else result
    if type(value) is dict:
        for key, child in value.items():
            flatten(child, f"{path}.{key}" if path else key, result)
    elif type(value) is list:
        result[path + ".#"] = str(len(value))
        for index, child in enumerate(value):
            flatten(child, f"{path}[{index}]", result)
    else:
        result[path] = "true" if value is True else "false" if value is False else str(value)
    return result


def _save(path, raw):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(raw)


def _valid_case(output, name, author=None, world=None, literal=None):
    root = output / name
    initial = fixtures.write_fixture(root)
    if author is not None or literal is not None:
        raw = encode(initial if author is None else author)
        if literal is not None:
            raw = raw.replace(b'"__exact_literal__"', literal)
        _save(root / demo.CONTENT_SOURCE, raw)
    if world is not None:
        _save(root / demo.WORLD_SOURCE, encode(world))
    synchronize_world(root)
    compilation = demo.compile_content(root)
    write_outputs(root, compilation)
    assertions = flatten(expected_projection(compilation))
    _save(root / "native_expected.json", encode(demo.canonical(assertions)))
    _save(root / "native_world_expected.json", encode(demo.canonical(flatten(expected_world_projection(compilation)))))
    return compilation, assertions


def _approved_case(output):
    """Mirror approved authors for tests; never promote synthetic fixture values."""
    root = output / "approved_base"
    for path in (demo.CONTENT_SOURCE, demo.SCHEMA_SOURCE, demo.IDS_SOURCE, demo.NATIVE_IDS,
                 demo.WORLD_SOURCE, demo.WORLD_SCHEMA_SOURCE):
        _save(root / path, (demo.ROOT / path).read_bytes())
    synchronize_world(root)
    compilation = demo.compile_content(root)
    demo.check_outputs(demo.ROOT, compilation.outputs)
    write_outputs(root, compilation)
    _save(root / "native_expected.json", encode(demo.canonical(flatten(expected_projection(compilation)))))
    _save(root / "native_world_expected.json", encode(demo.canonical(flatten(expected_world_projection(compilation)))))
    return compilation


def _bundle(compilation):
    return {path: compilation.outputs[path] if path in compilation.outputs else compilation.inputs[path] for path in BUNDLE_PATHS}


def _tamper(compilation, *, content=None, content_raw=None, world=None, ids=None, manifest_change=None):
    """Create explicitly invalid test bytes, re-signing only to reach native semantic checks."""
    bundle = _bundle(compilation)
    manifest = json.loads(bundle[demo.MANIFEST_OUTPUT])
    if content is not None:
        content_raw = encode(demo.canonical(content))
        manifest["display_sha256"] = demo.sha256(encode(content["display"]))
    if content_raw is not None:
        bundle[demo.CONTENT_OUTPUT] = content_raw
    if world is not None:
        old_hash = demo.sha256(bundle[demo.WORLD_OUTPUT])
        bundle[demo.WORLD_OUTPUT] = encode(demo.canonical(world))
        new_hash = demo.sha256(bundle[demo.WORLD_OUTPUT])
        bundle[demo.WORLD_CONTRACT] = bundle[demo.WORLD_CONTRACT].replace(old_hash.encode("ascii"), new_hash.encode("ascii"))
        manifest["world"]["contract"]["sha256"] = demo.sha256(bundle[demo.WORLD_CONTRACT])
    if ids is not None:
        bundle[demo.IDS_SOURCE] = encode(demo.canonical(ids))
        manifest["native_ids_sha256"] = demo.sha256(bundle[demo.IDS_SOURCE])
    manifest["content"]["sha256"] = demo.sha256(bundle[demo.CONTENT_OUTPUT])
    manifest["world"]["sha256"] = demo.sha256(bundle[demo.WORLD_OUTPUT])
    if manifest_change is not None:
        manifest_change(manifest)
    bundle[demo.MANIFEST_OUTPUT] = encode(demo.canonical(manifest))
    digest = demo.sha256(bundle[demo.MANIFEST_OUTPUT])
    bundle[demo.CONTENT_CONTRACT] = demo.content_contract_bytes(manifest["content"]["sha256"], digest)
    return bundle, digest


def _at(value, path):
    for key in path:
        value = value[key]
    return value


def _quantities(value, path=()):
    if type(value) is dict:
        if set(value) == {"unit", "value"}:
            yield path, value
        else:
            for key, child in value.items():
                yield from _quantities(child, path + (key,))
    elif type(value) is list:
        for index, child in enumerate(value):
            yield from _quantities(child, path + (index,))


_worker_reuse = None


def default_jobs():
    # Windows ProcessPoolExecutor refuses more than 61 workers.
    return min(os.cpu_count() or 1, 61)


def _init_worker():
    # Each worker process keeps its own binding-report reuse for the life of the pool.
    global _worker_reuse
    _worker_reuse = DemoWorld.reuse_binding_reports()
    _worker_reuse.__enter__()


def _case_hashes(output, name):
    """Hashes of the files one case's writer has just written, keyed relative to the export root. Each case writes
    only below its own directory."""
    root = Path(output) / name
    return {p.relative_to(output).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(root.rglob("*")) if p.is_file()}


def _valid_job(output, name, author, world, literal):
    compilation, assertions = _valid_case(output, name, author, world, literal)
    return compilation.digest.hex(), assertions, _case_hashes(output, name)


def _approved_job(output):
    return _approved_case(output).digest.hex(), _case_hashes(output, "approved_base")


class _InlinePool:
    """Runs jobs immediately in this process, for jobs=1."""

    class _Done:
        def __init__(self, value):
            self._value = value

        def result(self):
            return self._value

    def submit(self, function, *args):
        return self._Done(function(*args))

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False


def export(output, *, profile="full", rank_mutations=True, jobs=None, written=None):
    """Write the selected fixture profile. Valid cases compile in up to `jobs` processes (default: CPU count); the written
    bytes and index order do not depend on `jobs`. When `written` is a dict, each writer records there the hash of
    every file it wrote, keyed relative to `output`."""
    jobs = default_jobs() if jobs is None else jobs
    demo.require(type(jobs) is int and jobs >= 1, "FixtureJobs", output, "jobs must be a positive integer")
    demo.require(profile in PROFILES, "FixtureProfile", output, f"profile must be one of {PROFILES}")
    # Most cases share the fixture world and its binding sections; validate each distinct pair once per process.
    with DemoWorld.reuse_binding_reports():
        return _export(output, profile=profile, rank_mutations=rank_mutations, jobs=jobs,
                       written={} if written is None else written)


def _export(output, *, profile, rank_mutations, jobs, written):
    output = Path(output)
    resolved = output.resolve()
    artifacts = (demo.ROOT / "Artifacts").resolve()
    demo.require(resolved.is_relative_to(artifacts) and resolved != artifacts and not output.exists(),
                 "FixturePath", output, "test output must be a NEW directory below this worktree's Artifacts")
    output = resolved
    output.mkdir(parents=True)
    index = {"format": "demo-native-fixtures-1", "test_only": True, "valid": [], "invalid": []}
    base, base_assertions = _valid_case(output, "base")
    written.update(_case_hashes(output, "base"))
    index["valid"].append({"name": "base", "catalog_sha256": base.digest.hex(), "changed_paths": [], "mutated_quantity": ""})
    pool = _InlinePool() if jobs == 1 else ProcessPoolExecutor(jobs, initializer=_init_worker)
    with pool:
        approved = pool.submit(_approved_job, output)
        pending = []

        def valid(name, author=None, world=None, literal=None, mutated_quantity=""):
            # Snapshot the inputs now: callers reuse and mutate the same author between cases.
            job = pool.submit(_valid_job, output, name, deepcopy(author), deepcopy(world), literal)
            pending.append((name, mutated_quantity, job))

        if profile == "runtime":
            _export_exact_gold_case(valid)
            _export_world_spawn_shift_case(valid)
            _export_runtime_refusal(output, index, base, written)
        else:
            _export_cases(output, index, base, rank_mutations, valid, written)
        approved_digest, approved_files = approved.result()
        written.update(approved_files)
        index["valid"].append({"name": "approved_base", "catalog_sha256": approved_digest,
                              "changed_paths": [], "mutated_quantity": ""})
        for name, mutated_quantity, job in pending:
            digest, assertions, files = job.result()
            written.update(files)
            changes = [key for key, value in assertions.items() if not key.startswith("Identity.") and base_assertions.get(key) != value]
            index["valid"].append({"name": name, "catalog_sha256": digest,
                                  "changed_paths": changes, "mutated_quantity": mutated_quantity})
    raw = encode(demo.canonical(index))
    _save(output / "index.json", raw)
    written["index.json"] = hashlib.sha256(raw).hexdigest()
    return index


# D2 (#147): the advanced rank keeps every coefficient and holds each growing base at 1.5x the basic rank's.
COUPLED_RANK_FIELDS = ("base", "ad_ratio", "ap_ratio", "max_hp_ratio", "armor_ratio")


def _rank_mutation(author, path):
    """Change one rank quantity; a field D2 couples across ranks moves in both ranks so the case stays valid."""
    changed = deepcopy(author)
    quantity = _at(changed, path)
    delta = Decimal("0.001") if quantity["unit"] == "s" else Decimal("0.01")
    field = path[-1]
    if len(path) == 7 and path[4] == "effects" and field in COUPLED_RANK_FIELDS:
        ranks = _at(changed, path[:3])
        op = _at(changed, path[:6])["op"]
        basic = next((row for row in ranks[0]["effects"] if row["op"] == op), None)
        advanced = next((row for row in ranks[-1]["effects"] if row["op"] == op), None)
        if (ranks[0] is not ranks[-1] and basic is not None and advanced is not None and field in basic
                and not (field == "base" and op == "mana_on_hit")):
            step, scale = (delta * 2, Decimal(3) / 2) if field == "base" else (delta, Decimal(1))
            basic[field]["value"] = float(Decimal(str(basic[field]["value"])) + step)
            advanced[field]["value"] = float(Decimal(str(advanced[field]["value"])) + step * scale)
            return changed
    quantity["value"] = float(Decimal(str(quantity["value"])) + delta)
    return changed


def _export_exact_gold_case(valid):
    exact = fixtures.content_fixture()
    exact["parameters"]["SHOP.StartingGold"] = 9007199254740993
    valid("exact_gold", exact)


def _export_world_spawn_shift_case(valid):
    world = fixtures.world_source_fixture()
    next(row for row in world["anchors"] if row["id"] == "s00")["position_cm"]["x"] += 1
    valid("world_spawn_shift", world=world)


def _add_invalid(output, index, written, name, bundle, digest, error):
    root = output / name
    for relative, raw in bundle.items():
        _save(root / relative, raw)
        written[(Path(name) / relative).as_posix()] = hashlib.sha256(raw).hexdigest()
    index["invalid"].append({"name": name, "catalog_sha256": digest, "error_contains": error})


def _export_runtime_refusal(output, index, base, written):
    changed = json.loads(base.outputs[demo.CONTENT_OUTPUT])
    changed["future_content"] = 1
    bundle, digest = _tamper(base, content=changed)
    _add_invalid(output, index, written, "unknown_root_field", bundle, digest, "unknown field")


def _export_cases(output, index, base, rank_mutations, valid, written):
    exact = fixtures.content_fixture()
    exact["parameters"]["LIFE.RespawnSec"]["value"] = "__exact_literal__"
    valid("int64_max_ms", exact, literal=b"9223372036854775.807")
    valid("above_double_integer", exact, literal=b"9007199254740.993")
    _export_exact_gold_case(valid)
    exact = fixtures.content_fixture()
    exact["parameters"]["STAT.MPRegen.PerLevel"]["value"] = "__exact_literal__"
    valid("exact_rational", exact, literal=b"0.000000000000000001")
    _export_world_spawn_shift_case(valid)
    unicode_author = fixtures.content_fixture()
    unicode_author["identity"]["approval"]["decision"] = "TEST ONLY \u6d4b\u8bd5 \U0001f9ea"
    valid("unicode_metadata", unicode_author)
    unicode_author["identity"]["approval"]["decision"] = "TEST " + "a" * 506 + "\U0001f9ea"
    valid("unicode_scalar_boundary", unicode_author)

    if rank_mutations:
        author = fixtures.content_fixture()
        paths = [path for path, _ in _quantities(author) if "ranks" in path]
        for index_number, path in enumerate(paths):
            changed = _rank_mutation(author, path)
            source_path = "content" + "".join(f"[{key}]" if type(key) is int else "." + key for key in path) + ".value"
            native_quantity = base.catalog.field_projection[source_path][0].rsplit(".", 1)[0]
            valid(f"rank_field_{index_number:03d}", changed, mutated_quantity=native_quantity)

    data = json.loads(base.outputs[demo.CONTENT_OUTPUT])
    world_data = json.loads(base.inputs[demo.WORLD_OUTPUT])
    ids_data = json.loads(base.inputs[demo.IDS_SOURCE])

    def invalid(name, bundle, digest, error):
        _add_invalid(output, index, written, name, bundle, digest, error)

    def bad_content(name, mutate, error):
        changed = deepcopy(data)
        mutate(changed)
        bundle, digest = _tamper(base, content=changed)
        invalid(name, bundle, digest, error)

    bad_content("unknown_root_field", lambda d: d.update({"future_content": 1}), "unknown field")
    bad_content("unknown_parameter", lambda d: d["parameters"].update({"STAT.Unknown": 1}), "unknown field")
    bad_content("missing_parameter", lambda d: d["parameters"].pop("STAT.AD.Base"), "missing")
    bad_content("bad_unit", lambda d: d["parameters"]["STAT.MaxHP.Base"].update({"unit": "mp"}), "unsupported constant")
    bad_content("bad_storage", lambda d: d["parameters"]["STAT.MaxHP.Base"].update({"bp": 10}), "unknown field")
    bad_content("bool_integer", lambda d: d["parameters"].update({"SHOP.StartingGold": True}), "integer token")
    bad_content("string_integer", lambda d: d["parameters"].update({"SHOP.StartingGold": "10"}), "integer token")
    bad_content("retired_id", lambda d: d["abilities"][0].update({"native_id": 1}), "renumbered native ID")
    bad_content("unknown_id", lambda d: d["abilities"][0].update({"id": "active_missing"}), "unknown")
    bad_content("alias_id", lambda d: d["abilities"][0].update({"id": "Active_not_mad"}), "canonical lowercase")
    bad_content("long_id", lambda d: d["abilities"][0].update({"id": "a" * 64}), "string length")
    bad_content("duplicate_ability", lambda d: d["abilities"].__setitem__(1, deepcopy(d["abilities"][0])), "duplicate")
    bad_content("missing_rank", lambda d: d["abilities"][0]["ranks"].pop(), "array length")
    bad_content("wrong_rank", lambda d: d["abilities"][0]["ranks"][1].update({"rank": 3}), "integer outside")
    bad_content("wrong_opcode", lambda d: d["abilities"][0]["ranks"][0]["effects"][0].update({"opcode": 255}), "opcode")
    bad_content("unknown_effect", lambda d: d["abilities"][0]["ranks"][0]["effects"][0].update({"op": "grant_gold"}), "opcode")
    bad_content("duplicate_effect", lambda d: d["abilities"][0]["ranks"][0]["effects"].append(deepcopy(d["abilities"][0]["ranks"][0]["effects"][0])), "duplicate effect")
    bad_content("unknown_effect_field", lambda d: d["abilities"][0]["ranks"][0]["effects"][0].update({"unexpected": 1}), "unknown field")
    # The bolt's first effect is damage and it does not target Self (the payload sorts abilities by id).
    bolt = lambda d: next(a for a in d["abilities"] if a["id"] == "active_snark_bolt")
    bad_content("bad_damage_type", lambda d: bolt(d)["ranks"][0]["effects"][0].update({"damage_type": "True"}), "unknown damage type")
    bad_content("wrong_target", lambda d: bolt(d).update({"target": {"mode": "Self"}}), "wrong definition target")
    # #147: fits ungrown, overflows once grown for the top level.
    bad_content("grown_base_overflow", lambda d: bolt(d)["ranks"][1]["effects"][0]["base"].update({"p2": 6 * 10 ** 14}),
                "multiplication overflow")
    bad_content("wrong_family", lambda d: d["abilities"][0].update({"cooldown_family": "cd_wrong"}), "cooldown family")
    bad_content("zero_cooldown", lambda d: d["abilities"][0]["ranks"][0]["cooldown"].update({"ms": 0}), "integer outside")
    bad_content("negative_cost", lambda d: d["abilities"][0]["ranks"][0]["mana_cost"].update({"p2": -1}), "integer outside")
    bad_content("coefficient_overflow", lambda d: d["abilities"][0]["ranks"][0]["effects"][0]["ap_ratio"].update({"bp": demo.INT64_MAX}), "multiplication overflow")
    bad_content("growth_overflow", lambda d: d["parameters"]["STAT.MaxHP.PerLevel"].update({"p2": demo.INT64_MAX}), "multiplication overflow")
    bad_content("inverted_bounds", lambda d: d["parameters"]["STAT.AD.Min"].update({"p2": d["parameters"]["STAT.AD.Max"]["p2"]}), "outside")
    # Every attribute clamps to its bounds (#112 §4); a deduction only a wild card may name is refused instead.
    bad_content("tame_card_penalty", lambda d: d["attributes"][0].update({"penalties": {"MaxHPFlat": {"unit": "hp", "p2": 100}}}), "penalties")
    bad_content("bad_xp_derived", lambda d: d["progression"]["xp_to_next"].__setitem__(0, 1), "linear XP")
    bad_content("bad_opening_weight", lambda d: d["progression"]["opening_offer"].update({"matched_skill_weight": 0}), "integer outside")
    bad_content("bad_opening_card_stats", lambda d: d["opening_cards"][0]["stats"].pop(next(iter(d["opening_cards"][0]["stats"]))),
                "opening card stats")
    bad_content("unsorted_reroll_levels", lambda d: d["progression"].update({"reroll_levels": [7, 4]}), "reroll levels")
    bad_content("missing_draftable", lambda d: d["abilities"][0].pop("draftable"), "missing")
    bad_content("bad_upgraded_stats", lambda d: next(iter(d["equipment"][0]["upgraded_stats"].values())).update(
        {"p2": 1} if "p2" in next(iter(d["equipment"][0]["upgraded_stats"].values())) else {"bp": 1}), "upgraded stats")
    bad_content("inexact_recovery_ratio", lambda d: d["parameters"]["REGEN.OutOfCombatMaxHPRatioPerSec"].update({"bp": 155}), "recovery")
    bad_content("bad_interval", lambda d: d["character"]["basic_attack"].update({"interval_ms": 1}), "interval")
    bad_content("bad_sell_projection", lambda d: d["equipment"][0].update({"sell_gold": d["equipment"][0]["buy_price"]}), "sell gold")
    bad_content("missing_passive", lambda d: next(row for row in d["equipment"] if row["passive"] is not None).update({"passive": None}), "presence")
    bad_content("equipment_active_field", lambda d: d["equipment"][0].update({"active": None}), "unknown field")
    bad_content("bad_passive_stat", lambda d: next(row for row in d["abilities"] if row["id"] == "passive_lucky_star")["ranks"][0]["effects"][0]["stats"].update({"ADFlat": {"unit": "points", "p2": 1}}), "passive stat")
    slow = {"op": "slow", "opcode": ids_data["effect_ops"]["slow"], "ratio": {"unit": "ratio", "bp": 2500},
            "duration": {"unit": "ms", "ms": 1500}}
    bad_content("unexpected_slow", lambda d: d["abilities"][0]["ranks"][0]["effects"].append(deepcopy(slow)), "missing or extra effect")
    bad_content("full_slow", lambda d: d["abilities"][0]["ranks"][0]["effects"].append(
        {**deepcopy(slow), "ratio": {"unit": "ratio", "bp": 10000}}), "integer outside")
    bad_content("timed_resource_bonus", lambda d: d["abilities"][0]["ranks"][0]["effects"].append(
        {"op": "timed_stat_bonus", "opcode": ids_data["effect_ops"]["timed_stat_bonus"],
         "stats": {"MaxHPFlat": {"unit": "hp", "p2": 100}}, "duration": {"unit": "ms", "ms": 1000}}), "resource maxima")
    bad_content("nonreduced_rational", lambda d: d["parameters"]["UNIT.MoveSpeedCmPerSec"].update({"numerator": 1000, "denominator": 2}), "not reduced")
    bad_content("zero_denominator", lambda d: d["parameters"]["UNIT.MoveSpeedCmPerSec"].update({"denominator": 0}), "integer outside")
    bad_content("wrong_monster", lambda d: d["camps"]["placements"][0].update({"monster_id": "monster_missing"}), "camp/monster")
    bad_content("bad_perception", lambda d: d["camps"]["monsters"][0]["perception_radius"].update({"numerator": 301}), "perception")
    bad_content("bad_world_ref", lambda d: d["world"].update({"shop_ids": ["shop_missing"]}), "anchor reference")
    bad_content("duplicate_world_ref", lambda d: d["world"]["spawn_ids"].__setitem__(1, d["world"]["spawn_ids"][0]), "duplicate")
    bad_content("bad_display_key", lambda d: d["display"][0].update({"name_key": "wrong.name"}), "display reference")
    bad_content("missing_display", lambda d: d["display"].pop(), "array length")
    bad_content("extra_portrait", lambda d: d["display"][0].update({"portrait_icon_id": "portrait_wrong"}), "unknown field")
    bad_content("missing_display_text", lambda d: d["display"][0].pop("name_text"), "missing")
    bad_content("display_text_type", lambda d: d["display"][1].update({"name_text": 1}), "expected string")
    bad_content("empty_display_description", lambda d: d["display"][1].update({"description_text": ""}), "string length")
    bad_content("long_display_name", lambda d: d["display"][1].update({"name_text": "\u540d" * 33}), "string length")
    bad_content("display_text_control", lambda d: d["display"][1].update({"description_text": "\u6362\u884c\n\u8bf4\u660e"}), "forbidden character")
    bad_content("display_text_invisible", lambda d: d["display"][1].update({"name_text": "\u540d\u79f0\u200b"}), "forbidden character")
    bad_content("display_text_brace", lambda d: d["display"][1].update({"description_text": "\u9020\u6210{Damage}"}), "forbidden character")
    bad_content("display_text_astral", lambda d: d["display"][1].update({"name_text": "\u540d\U0001f600"}), "forbidden character")
    bad_content("display_text_edge_space", lambda d: d["display"][1].update({"name_text": "\u540d\u79f0\u3000"}), "leading or trailing space")
    bad_content("display_description_digit", lambda d: d["display"][1].update({"description_text": "\u9020\u621070"}), "contains digits")
    bad_content("display_description_wide_digit", lambda d: d["display"][1].update({"description_text": "\u9020\u6210\uff17\uff10"}), "contains digits")
    bad_content("duplicate_display_name", lambda d: d["display"][2].update({"name_text": d["display"][1]["name_text"]}), "duplicate display name")
    bad_content("unapproved_metadata", lambda d: d["identity"]["approval"].update({"status": "candidate"}), "approval metadata")
    bad_content("bad_policy", lambda d: d["policies"].update({"DuplicateEquipment": "Disallowed"}), "unsupported constant")
    bad_content("bad_boolean_policy", lambda d: d["policies"].update({"ShopRequiresLineOfSight": True}), "boolean policy")
    bad_content("bad_formula", lambda d: d["formulas"].update({"Damage": "RoundInstead"}), "unsupported constant")
    bad_content("bad_revision", lambda d: d["identity"].update({"projection_revision": 1}), "integer outside")
    bad_content("missing_acquisition_formula", lambda d: d["formulas"].pop("AttackMoveAcquireRadius"), "missing")
    bad_content("bad_acquisition_formula", lambda d: d["formulas"].update({"AttackMoveAcquireRadius": "UnboundedNearest"}), "unsupported constant")
    bad_content("aliased_root_key", lambda d: d.update({"Parameters": d.pop("parameters")}), "missing")
    bad_content("aliased_parameter", lambda d: d["parameters"].update({"STAT.ad.Base": d["parameters"].pop("STAT.AD.Base")}), "missing")
    bad_content("aliased_target", lambda d: d["abilities"][0]["target"].update({"mode": "directionprojectile"}), "unsupported target")
    bad_content("aliased_damage_type", lambda d: next(a for a in d["abilities"] if a["id"] == "active_snark_bolt")["ranks"][0]["effects"][0]
        .update({"damage_type": "magical"}), "unknown damage type")
    bad_content("aliased_policy", lambda d: d["policies"].update({"DuplicateEquipment": "allowed"}), "unsupported constant")
    bad_content("aliased_approval", lambda d: d["identity"]["approval"].update({"status": "APPROVED"}), "approval metadata")
    bad_content("aliased_display_namespace", lambda d: d["display"][0].update({"namespace": "democontent"}), "display reference")
    bad_content("aliased_formula", lambda d: d["formulas"].update({"Damage": "floorP2ThenShieldThenHP"}), "unsupported constant")

    for name, literal, error in (
        ("int64_overflow", b"9223372036854775808", "int64 token"),
        ("int64_underflow", b"-9223372036854775809", "int64 token"),
        ("decimal_integer", b"10.0", "int64 token"), ("exponent_integer", b"1e1", "int64 token"),
        ("nonfinite_integer", b"1e309", "int64 token"),
    ):
        changed = deepcopy(data)
        changed["parameters"]["SHOP.StartingGold"] = "__invalid_literal__"
        raw = encode(demo.canonical(changed)).replace(b'"__invalid_literal__"', literal)
        bundle, digest = _tamper(base, content_raw=raw)
        invalid(name, bundle, digest, error)

    for name, replacement, error in (
        ("invalid_utf8", b"\xc0\xaf", "strict UTF-8"),
        ("lone_surrogate", b"\\ud800", "Unicode"),
    ):
        changed = deepcopy(data)
        changed["identity"]["approval"]["decision"] = "__invalid_string__"
        raw = encode(demo.canonical(changed)).replace(b"__invalid_string__", replacement)
        bundle, digest = _tamper(base, content_raw=raw)
        invalid(name, bundle, digest, error)
    raw = base.outputs[demo.CONTENT_OUTPUT].replace(b"{\n", b'{\n  "abilities": [],\n', 1)
    bundle, digest = _tamper(base, content_raw=raw)
    invalid("duplicate_json_key", bundle, digest, "duplicate object key")
    bundle, digest = _tamper(base, content_raw=base.outputs[demo.CONTENT_OUTPUT] + b"\n")
    invalid("noncanonical_bytes", bundle, digest, "canonical encoding")

    def low_camp(world):
        return next(row for row in world["anchors"] if row["kind"] == "Camp" and len(row["members"]) == 2)

    def tree(world):
        return next(row for row in world["geometry"]["blockers"] if row["shape"] == "cylinder")

    def ramp(world, index=0):
        return world["geometry"]["elevation"]["ramps"][index]

    def plateau(world, index=0):
        return world["geometry"]["elevation"]["plateaus"][index]

    def shift(vertex, axis, amount):
        vertex[axis].update({"numerator": vertex[axis]["numerator"] + amount})

    def cm(value):
        return value["numerator"] / value["denominator"]

    def detach(world, level):
        # Slide the first ramp of this level 1 m down its own slope, so its high edge leaves the plateau above
        # without reaching any other ground.
        row = next(r for r in world["geometry"]["elevation"]["ramps"] if r["lower_level"] == level)
        p = row["polygon_cm"]
        dx = (cm(p[0]["x"]) + cm(p[1]["x"])) - (cm(p[2]["x"]) + cm(p[3]["x"]))
        dy = (cm(p[0]["y"]) + cm(p[1]["y"])) - (cm(p[2]["y"]) + cm(p[3]["y"]))
        length = (dx * dx + dy * dy) ** 0.5
        for vertex in p:
            for axis, delta in (("x", dx), ("y", dy)):
                shift(vertex, axis, round(100 * delta / length) * vertex[axis]["denominator"])

    def last_waypoint(world):
        return max(i for i, row in enumerate(world["anchors"]) if row["kind"] == "BossWaypoint")

    for name, mutate, error in (
        ("world_missing_anchor", lambda w: w["anchors"].pop(last_waypoint(w)), "four Boss waypoints"),
        ("world_wrong_surface", lambda w: w["anchors"][0].update({"surface_id": "missing_surface"}), "unknown surface"),
        ("world_duplicate_home", lambda w: low_camp(w)["members"].__setitem__(1, deepcopy(low_camp(w)["members"][0])), "duplicate"),
        ("world_member_count", lambda w: low_camp(w)["members"].pop(), "member-home count"),
        ("world_out_of_bounds", lambda w: w["anchors"][0]["position_cm"]["x"].update({"numerator": 6000}), "outside"),
        ("world_bad_bounds", lambda w: w["geometry"]["surfaces"][0]["max_cm"]["x"].update({"numerator": -5000}), "nonempty"),
        ("world_bad_flags", lambda w: w["geometry"]["blockers"][0].update({"blocks_movement": False, "blocks_sight": False}), "stops movement"),
        ("world_unknown_field", lambda w: w["anchors"][0].update({"home_default": True}), "unknown field"),
        ("world_aliased_kind", lambda w: w["anchors"][0].update({"kind": "spawn"}), "unknown anchor kind"),
        ("world_old_projection", lambda w: w.update({"projection_revision": 1}), "integer outside"),
        ("world_old_schema", lambda w: w.update({"schema_version": 1}), "integer outside"),
        ("world_clockwise_polygon", lambda w: w["geometry"]["blockers"][0]["polygon_cm"].reverse(), "counterclockwise"),
        ("world_duplicate_vertex", lambda w: w["geometry"]["blockers"][0]["polygon_cm"].__setitem__(1, deepcopy(w["geometry"]["blockers"][0]["polygon_cm"][0])), "counterclockwise"),
        ("world_missing_polygon", lambda w: w["geometry"]["blockers"][0].pop("polygon_cm"), "missing"),
        ("world_missing_offset", lambda w: w["placement"]["spawn_offsets_cm"].pop(), "array length"),
        ("world_duplicate_offset", lambda w: w["placement"]["spawn_offsets_cm"].__setitem__(1, deepcopy(w["placement"]["spawn_offsets_cm"][0])), "overlap"),
        ("world_unknown_shape", lambda w: tree(w).update({"shape": "box"}), "unknown blocker shape"),
        ("world_tree_polygon_field", lambda w: tree(w).update({"polygon_cm": []}), "unknown field"),
        ("world_tree_off_ground", lambda w: tree(w)["center_cm"]["z"].update({"numerator": 1}), "cylinder must stand"),
        ("world_tree_outside", lambda w: tree(w)["center_cm"]["x"].update({"numerator": -4980}), "cylinder must stand"),
        ("world_plateau_height", lambda w: shift(plateau(w)["polygon_cm"][0], "z", 1), "plateau vertex"),
        ("world_plateau_level", lambda w: plateau(w).update({"level": 3}), "integer outside"),
        ("world_ramp_height", lambda w: shift(ramp(w)["polygon_cm"][2], "z", -1), "ramp low edge"),
        ("world_ramp_steep", lambda w: [shift(ramp(w)["polygon_cm"][i], "y", 200) for i in (0, 1)], "steeper"),
        ("world_ramp_skewed", lambda w: shift(ramp(w)["polygon_cm"][3], "y", 50), "parallel"),
        ("world_ramp_detached", lambda w: detach(w, 0), "one level up"),
        ("world_upper_ramp_detached", lambda w: detach(w, 1), "one level up"),
        ("world_tree_on_cliff", lambda w: tree(w)["center_cm"].update({"x": {"numerator": 2900, "denominator": 1},
                                                                      "y": {"numerator": 3700, "denominator": 1}}), "one flat level"),
        ("world_inner_wall_outside", lambda w: [v["x"].update({"numerator": v["x"]["numerator"] + 10000}) for v in w["geometry"]["blockers"][0]["polygon_cm"]], "closing a ground edge"),
        ("world_duplicate_parameter", lambda w: w["parameter_refs"].__setitem__(1, w["parameter_refs"][0]), "parameter reference"),
        ("world_source_identity", lambda w: w.update({"source_sha256": "0" * 64}), "file bytes"),
        ("world_schema_identity", lambda w: w.update({"author_schema_sha256": "0" * 64}), "file bytes"),
        ("signed_world_boundary", lambda w: w["geometry"]["surfaces"][0]["min_cm"]["x"].update({"numerator": demo.INT64_MIN}), "boundary"),
    ):
        changed = deepcopy(world_data)
        mutate(changed)
        bundle, digest = _tamper(base, world=changed)
        invalid(name, bundle, digest, error)

    ids = deepcopy(ids_data)
    ids["abilities"]["active_snark_bolt"] = 95
    bundle, digest = _tamper(base, ids=ids)
    invalid("renumbered_id_map", bundle, digest, "integer outside")
    for name, change, error in (
        ("manifest_self_hash", lambda m: m.update({"catalog_sha256": "0" * 64}), "unknown field"),
        ("manifest_revision", lambda m: m.update({"handler_revision": 2}), "integer outside"),
        ("manifest_path", lambda m: m["content"].update({"file": "..\\demo_content.json"}), "unsupported constant"),
    ):
        bundle, digest = _tamper(base, manifest_change=change)
        invalid(name, bundle, digest, error)
    for number, path in enumerate(BUNDLE_PATHS):
        bundle = _bundle(base)
        bundle[path] += b"\n"
        invalid(f"byte_identity_{number}", bundle, base.digest.hex(), "mismatch" if path == demo.MANIFEST_OUTPUT else
                "content contract" if path == demo.CONTENT_CONTRACT else "file bytes")
    bundle = _bundle(base)
    bundle.pop(demo.WORLD_OUTPUT)
    invalid("missing_world", bundle, base.digest.hex(), "missing")
    invalid("stale_expected_root", _bundle(base), "0" * 64, "expected root")


CACHE_FORMAT = "demo-native-fixture-cache-2"
CACHE_MANIFEST = "cache.json"
# Cache entries kept beside the current one, for the worktree state before the last input change.
CACHE_KEEP = 1
# A staging directory (<key>.<pid>.tmp) belongs to an export that may still be running in another process; it is removed
# only once it is far older than any export takes, so its writer has certainly ended.
STAGING_STALE_SECONDS = 6 * 3600


def cache_inputs(root=None):
    """Every file the export reads, relative to the project root."""
    root = Path(root or demo.ROOT)
    scripts = Path(__file__).resolve().parent
    files = {f"Scripts/{name}.py" for name in python_closure(scripts, [Path(__file__).stem], Path(__file__).name)}
    for folder in ("SourceData", demo.DATA.as_posix()):
        files.update(p.relative_to(root).as_posix() for p in (root / folder).rglob("*") if p.is_file())
    files.update(p.as_posix() for p in (demo.NATIVE_IDS, demo.CONTENT_CONTRACT, demo.WORLD_CONTRACT))
    return sorted(files)


def cache_key(root=None, profile="full"):
    root = Path(root or demo.ROOT)
    demo.require(profile in PROFILES, "FixtureProfile", root, f"profile must be one of {PROFILES}")
    digest = hashlib.sha256(
        f"{CACHE_FORMAT}\0profile {profile}\0python {sys.version_info[0]}.{sys.version_info[1]}\0".encode())
    for relative in cache_inputs(root):
        raw = (root / relative).read_bytes()
        digest.update(f"{relative}\0{len(raw)}\0".encode())
        digest.update(raw)
    return digest.hexdigest()


def _file_hashes(folder):
    folder = Path(folder)
    return {p.relative_to(folder).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(folder.rglob("*")) if p.is_file() and p.name != CACHE_MANIFEST}


def _cache_valid(entry, key, profile):
    try:
        manifest = json.loads((entry / CACHE_MANIFEST).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return False
    return (isinstance(manifest, dict) and manifest.get("format") == CACHE_FORMAT and manifest.get("key") == key
            and manifest.get("profile") == profile
            and manifest.get("files") == _file_hashes(entry))


def _prune(cache, current):
    entries = [p for p in cache.iterdir() if p.is_dir() and p != current]
    staging = [p for p in entries if p.name.endswith(".tmp")]
    entries = [p for p in entries if p not in staging]
    stale = [p for p in entries if not (p / CACHE_MANIFEST).is_file()]
    stale += [p for p in staging if time.time() - p.stat().st_mtime > STAGING_STALE_SECONDS]
    kept = sorted((p for p in entries if p not in stale), key=lambda p: (p / CACHE_MANIFEST).stat().st_mtime, reverse=True)
    for path in stale + kept[CACHE_KEEP:]:
        # Another run may still read an old entry; whatever cannot be removed now goes on a later run.
        shutil.rmtree(path, ignore_errors=True)


def export_cached(cache, *, profile="full", jobs=None, root=None):
    """Return (fixture directory, reused) for the current inputs, exporting only when no valid entry exists."""
    cache = Path(cache)
    resolved = cache.resolve()
    artifacts = (demo.ROOT / "Artifacts").resolve()
    demo.require(resolved.is_relative_to(artifacts) and resolved != artifacts, "FixturePath", cache,
                 "the fixture cache must be a directory below this worktree's Artifacts")
    demo.require(profile in PROFILES, "FixtureProfile", cache, f"profile must be one of {PROFILES}")
    key = cache_key(root, profile)
    entry = resolved / key
    if _cache_valid(entry, key, profile):
        os.utime(entry / CACHE_MANIFEST)
        _prune(resolved, entry)
        return entry, True
    shutil.rmtree(entry, ignore_errors=True)
    demo.require(not entry.exists(), "FixtureCache", entry, "an invalid cache entry could not be removed")
    resolved.mkdir(parents=True, exist_ok=True)
    staging = resolved / f"{key}.{os.getpid()}.tmp"
    shutil.rmtree(staging, ignore_errors=True)
    written = {}
    try:
        export(staging, profile=profile, jobs=jobs, written=written)
        demo.require(cache_key(root, profile) == key, "InputsChanged", cache, "fixture inputs changed during the export")
        # The manifest is what the export wrote, not what is on disk afterwards; a file lost or added meanwhile refuses.
        demo.require(_file_hashes(staging) == written, "FixtureCache", staging, "staged fixtures differ from what the export wrote")
    except BaseException:
        shutil.rmtree(staging, ignore_errors=True)
        raise
    manifest = {"format": CACHE_FORMAT, "profile": profile, "key": key, "files": written}
    (staging / CACHE_MANIFEST).write_text(json.dumps(manifest, sort_keys=True), encoding="utf-8")
    try:
        staging.rename(entry)
    except OSError:
        # A concurrent run published the same key first; use that entry if it is complete.
        shutil.rmtree(staging, ignore_errors=True)
        demo.require(_cache_valid(entry, key, profile), "FixtureCache", entry, "could not publish the exported fixtures")
    _prune(resolved, entry)
    return entry, False


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    target = parser.add_mutually_exclusive_group(required=True)
    target.add_argument("--output", type=Path, help="export into this new directory")
    target.add_argument("--cache", type=Path, help="reuse or refresh the input-keyed export below this directory")
    parser.add_argument("--profile", choices=PROFILES, default="full",
                        help="runtime writes only cases needed outside the import group; full writes every case")
    parser.add_argument("--jobs", type=int, default=None, help="worker processes for valid cases (default: CPU count)")
    args = parser.parse_args()
    try:
        if args.cache:
            path, reused = export_cached(args.cache, profile=args.profile, jobs=args.jobs)
            print(f"TEST ONLY: {'reused native fixtures for unchanged inputs' if reused else 'exported native fixtures for changed inputs'}: {path}")
            print(f"FIXTURES={path}")
            return
        index = export(args.output, profile=args.profile, jobs=args.jobs)
    except (demo.ContentError, OSError) as error:
        parser.exit(1, f"Demo native fixture export failed: {error}\n")
    print(f"TEST ONLY: {len(index['valid'])} valid and {len(index['invalid'])} refusal cases: {args.output}")


if __name__ == "__main__":
    main()
