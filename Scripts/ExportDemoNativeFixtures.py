"""Export compiler-backed, TEST-ONLY inputs for Lockdown.Rules.DemoContent.

Nothing here is a production content source or an approval decision. All output
must be a new directory under this worktree's ignored Artifacts directory.
"""
import argparse
from copy import deepcopy
from decimal import Decimal
import json
from pathlib import Path

from CatalogTools import encode
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
    "None", "AddFlatThenAdditivePercent", "Allowed", "OncePerFamily", "IndependentSources",
    "ContinueServerTime", "PreserveRatio", "ContinueForRemainingContender", "CancelUnsettledNoRefund",
    "BreakOnAggressiveIntent", "OncePerAttackId", "PrimaryBasicAttackHPLost", "Disabled",
    "CarrySubP2Remainder", "ReplaceIfNotWeaker", "ConfirmedCombatHit", "ReturnThenReset", "NotApplicable",
)
BONUS_STATS = (
    "ADFlat", "APFlat", "MaxHPFlat", "MaxMPFlat", "ArmorFlat", "MagicResistFlat", "CooldownHasteFlat",
    "AttackRangeCmFlat", "AttackSpeedBonusRatio", "MoveSpeedBonusRatio", "CritChanceBonusRatio", "ADBonusRatio",
)
TARGETS = ("Self", "Unit", "DirectionProjectile", "DirectionDash", "GroundPoint")
MECHANIC_TAGS = ("BasicAttack", "Mobility", "Control", "Shield", "Tank", "Breach")
BUNDLE_PATHS = (
    demo.MANIFEST_OUTPUT, demo.CONTENT_OUTPUT, demo.WORLD_OUTPUT, demo.CONTENT_CONTRACT,
    demo.WORLD_CONTRACT, demo.SCHEMA_SOURCE, demo.IDS_SOURCE, demo.NATIVE_IDS,
    demo.WORLD_SOURCE, demo.WORLD_SCHEMA_SOURCE,
)


def scalar(value):
    return value[next(key for key in ("p2", "bp", "ms") if key in value)]


def expected_world_projection(compilation):
    world = json.loads(compilation.inputs[demo.WORLD_OUTPUT])

    def point(value):
        return {axis.upper(): rational(value[axis]) for axis in ("x", "y", "z")}

    return {
        "MapId": world["map_id"], "LayoutRevision": world["layout_revision"],
        "GeometryRevision": world["geometry_revision"],
        "SourceDigest": {"Bytes": list(bytes.fromhex(world["source_sha256"]))},
        "AuthorSchemaDigest": {"Bytes": list(bytes.fromhex(world["author_schema_sha256"]))},
        "ApprovalDecision": world["approval"]["decision"],
        "Surfaces": [{"Id": row["id"], "MinCm": point(row["min_cm"]), "MaxCm": point(row["max_cm"])}
                     for row in world["geometry"]["surfaces"]],
        "Blockers": [{
            "Id": row["id"], "SurfaceId": row["surface_id"],
            "PolygonCm": [point(vertex) for vertex in row["polygon_cm"]],
            "HeightCm": rational(row["height_cm"]), "BlocksMovement": row["blocks_movement"],
            "BlocksSight": row["blocks_sight"],
        } for row in world["geometry"]["blockers"]],
        "SightSampleHeightCm": rational(world["geometry"]["sight_sample_height_cm"]),
        "CampClearanceCm": rational(world["geometry"]["camp_clearance_cm"]),
        "SpawnOffsetsCm": [point(value) for value in world["placement"]["spawn_offsets_cm"]],
        "SpawnValidationRadiusCm": rational(world["placement"]["spawn_validation_radius_cm"]),
        "Connections": [{"Id": row["id"], "WallIds": row["wall_ids"], "CampId": row["camp_id"]}
                        for row in world["connections"]],
        "ParameterRefs": world["parameter_refs"],
        "Anchors": [{
            "Id": row["id"], "Kind": ("Spawn", "Camp", "Shop", "Capture").index(row["kind"]),
            "SurfaceId": row["surface_id"], "PositionCm": point(row["position_cm"]),
            "Members": [{"Id": member["id"], "PositionCm": point(member["position_cm"])}
                        for member in row.get("members", [])],
        } for row in world["anchors"]],
    }


def rational(value=None):
    return {"Numerator": 0, "Denominator": 1} if value is None else {
        "Numerator": value["numerator"], "Denominator": value["denominator"],
    }


def stats(values):
    return [{"Stat": BONUS_STATS.index(key), "Amount": 0 if key == "AttackRangeCmFlat" else scalar(value),
             "Cm": rational(value if key == "AttackRangeCmFlat" else None)}
            for key, value in sorted(values.items())]


def target(value=None):
    value = {} if value is None else value
    return {
        "Mode": TARGETS.index(value.get("mode", "Self")),
        "RangeCm": rational(value.get("range")),
        "ProjectileRadiusCm": rational(value.get("projectile_radius")),
        "ProjectileSpeedCmPerSec": rational(value.get("projectile_speed")),
        "AttackRangeRatioBp": scalar(value["attack_range_ratio"]) if "attack_range_ratio" in value else 0,
        "AttackRangeBonusCapCm": rational(value.get("attack_range_bonus_cap")),
    }


TRIGGERS = ("None", "Release", "EmpowerHit", "BasicHit", "BasicCrit", "Cleanse", "LandingHit", "LowHP", "AbilityHit", "Control")
COOLDOWN_ENTRIES = ("None", "Basic", "Self", "OtherActives", "Actives")
CRIT_MODES = ("None", "Impaired", "Chance")


def effect(value):
    op = value.get("op")
    result = {
        "Opcode": value["opcode"], "BaseP2": 0, "ADRatioBp": 0, "APRatioBp": 0, "MaxHPRatioBp": 0, "DamageType": 0,
        "DurationMs": 0, "DelayMs": 0, "RatioBp": 0,
        "DistanceCm": rational(), "RadiusCm": rational(), "CooldownRatioBp": 0, "DistanceBonusBp": 0, "Stats": [],
        "ArmorRatioBp": 0, "IntervalMs": 0, "CriticalMultiplierBp": 0, "bAbilityTrigger": False, "bRearExempt": False,
        "Trigger": 0, "Entries": 0, "CritMode": 0, "AmountMs": 0, "MagicResistRatioBp": 0, "VampRatioBp": 0,
        "MultiplierBp": 0, "Count": 0, "bImpaired": False, "bAmbushReady": False, "SkillCooldownRatioBp": 0,
        "SkillDistanceBonusBp": 0, "AbilityIds": [], "PerCountP2": 0, "CapP2": 0, "CapBp": 0, "MaxHPPerSecBp": 0,
    }
    for source, dest in (
        ("base", "BaseP2"), ("ad_ratio", "ADRatioBp"), ("ap_ratio", "APRatioBp"), ("max_hp_ratio", "MaxHPRatioBp"),
        ("duration", "DurationMs"), ("delay", "DelayMs"), ("ratio", "RatioBp"), ("threshold", "RatioBp"),
        ("move_speed_penalty", "RatioBp"), ("cooldown_ratio", "CooldownRatioBp"), ("distance_bonus", "DistanceBonusBp"),
        ("armor_ratio", "ArmorRatioBp"), ("interval", "IntervalMs"), ("critical_multiplier", "CriticalMultiplierBp"),
        ("in_combat_ratio", "RatioBp"), ("shred_ratio", "RatioBp"), ("cripple_ratio", "RatioBp"),
        ("windup_reduction", "RatioBp"), ("surge_duration", "DurationMs"), ("shred_duration", "DurationMs"),
        ("cripple_duration", "DurationMs"), ("dash_credit", "AmountMs"), ("cooldown", "AmountMs"), ("window", "DelayMs"),
        ("surge_multiplier", "MultiplierBp"), ("cleanse_multiplier", "MultiplierBp"), ("per_hit", "PerCountP2"),
        ("magic_resist_ratio", "MagicResistRatioBp"), ("mana", "BaseP2"), ("vamp", "VampRatioBp"),
        ("omnivamp", "VampRatioBp"), ("skill_cooldown_ratio", "SkillCooldownRatioBp"),
        ("skill_distance_bonus", "SkillDistanceBonusBp"),
    ):
        if source in value:
            result[dest] = scalar(value[source])
    if op == "out_of_combat_regen":
        result["MaxHPRatioBp"] = 0
        result["MaxHPPerSecBp"] = scalar(value["max_hp_ratio"])
    if "cap" in value:
        result["CapBp" if op == "long_reach" else "CapP2"] = scalar(value["cap"])
    for source in ("count", "max_stacks", "max_hits"):
        if source in value:
            result["Count"] = value[source]
    for source, dest in (("distance", "DistanceCm"), ("radius", "RadiusCm"), ("reveal_radius", "RadiusCm"),
                         ("half_width", "RadiusCm"), ("cast_range", "DistanceCm")):
        if source in value:
            result[dest] = rational(value[source])
    if "damage_type" in value:
        result["DamageType"] = ("Physical", "Magical").index(value["damage_type"])
    if "stats" in value:
        result["Stats"] = stats(value["stats"])
    if "trigger" in value:
        if op == "thorns":
            result["bAbilityTrigger"] = ("Basic", "BasicOrAbility").index(value["trigger"]) == 1
        else:
            result["Trigger"] = TRIGGERS.index(value["trigger"])
    if "entries" in value:
        result["Entries"] = COOLDOWN_ENTRIES.index(value["entries"])
    if "mode" in value:
        result["CritMode"] = CRIT_MODES.index(value["mode"])
    if "abilities" in value:
        result["AbilityIds"] = list(value["abilities"])
    if "rear_exempt" in value:
        result["bRearExempt"] = value["rear_exempt"]
    result["bImpaired"] = value.get("impaired", False)
    result["bAmbushReady"] = value.get("ambush_ready", False)
    return result


def rank(value=None):
    value = {} if value is None else value
    return {
        "Rank": value.get("rank", 0),
        "ManaCostP2": scalar(value["mana_cost"]) if "mana_cost" in value else 0,
        "CooldownMs": scalar(value["cooldown"]) if "cooldown" in value else 0,
        "Effects": [effect(row) for row in value.get("effects", [])],
    }


def active(value=None):
    value = {} if value is None else value
    return {
        "Target": target(value.get("target")),
        "WindupMs": scalar(value["windup"]) if "windup" in value else 0,
        "CooldownFamily": value.get("cooldown_family", ""), "Rank": rank(value.get("rank")),
        "HasUpgradedRank": "upgraded_rank" in value, "UpgradedRank": rank(value.get("upgraded_rank")),
    }


def tags(values):
    return [MECHANIC_TAGS.index(value) for value in values]


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
        ("STAT.CooldownHaste.Max", "CooldownHasteMaxP2"), ("UNIT.BasicAttackWindupSec", "BasicAttackWindupMs"),
        ("CAPTURE.SpraySec", "CaptureSprayMs"), ("CAPTURE.GuardSec", "CaptureGuardMs"), ("TIMING.DefendRouteBudgetSec", "DefendRouteBudgetMs"),
        ("MATCH.DevelopmentSec", "DevelopmentMs"), ("LIFE.RespawnSec", "RespawnMs"), ("LIFE.ProtectionSec", "ProtectionMs"),
        ("SHOP.SellRatio", "SellRatioBp"), ("REGEN.OutOfCombatDelaySec", "OutOfCombatRegenDelayMs"),
        ("REWARD.AssistWindowSec", "AssistWindowMs"), ("REWARD.AssistGoldRatio", "AssistGoldBp"),
        ("SHOP.SellRatio", "SellRatioBp"), ("SHOP.UpgradeStatBonusRatio", "UpgradeStatBonusBp"),
        ("REGEN.OutOfCombatDelaySec", "OutOfCombatRegenDelayMs"),
        ("REGEN.OutOfCombatMaxHPRatioPerSec", "OutOfCombatMaxHPRegenBp"),
        ("REGEN.OutOfCombatMaxMPRatioPerSec", "OutOfCombatMaxMPRegenBp"),
    ):
        result[dest] = scalar(parameters[source])
    for source, dest in (
        ("UNIT.CollisionRadiusCm", "CollisionRadiusCm"), ("VISION.RadiusCm", "VisionRadiusCm"),
        ("SHOP.InteractRadiusCm", "ShopInteractRadiusCm"), ("CAPTURE.RadiusCm", "CaptureRadiusCm"),
        ("CAMP.ResetArrivalRadiusCm", "CampResetArrivalRadiusCm"), ("COMBAT.RearArcDeg", "RearArcDeg"),
    ):
        result[dest] = rational(parameters[source])
    for source, dest in (
        ("SHOP.StartingGold", "StartingGold"), ("SHOP.UpgradeCost", "UpgradeCost"),
        ("XP.BaseToNext", "XPBaseToNext"), ("XP.IncreasePerLevel", "XPIncreasePerLevel"),
        ("REWARD.PlayerXP.Base", "PlayerXPBase"), ("REWARD.PlayerXP.PerLevel", "PlayerXPPerLevel"),
        ("REWARD.PlayerGold.Base", "PlayerGoldBase"), ("REWARD.PlayerGold.PerLevel", "PlayerGoldPerLevel"),
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
        ("UpgradeWeight", "UpgradeWeightBp"), ("NewActiveOpenSlotWeight", "NewActiveOpenSlotWeightBp"),
        ("NewActiveFullSlotWeight", "NewActiveFullSlotWeightBp"), ("NewPassiveWeight", "NewPassiveWeightBp"),
        ("AffinityStep", "AffinityStepBp"), ("AffinityCap", "AffinityCapBp"),
        ("ThirdSlotAttributeChance", "ThirdSlotAttributeBp"),
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
    innate_dash = active({"target": {"mode": "DirectionDash"}, "rank": {
        "rank": 1, "mana_cost": dash["mana_cost"], "cooldown": dash["cooldown"],
        "effects": [{"opcode": dash_opcode, "distance": dash["distance"], "duration": dash["duration"]}]}})
    result = {
        "Identity": identity, "Parameters": parameter_projection(data["parameters"]), "Formulas": formulas, "Policies": policies,
        "Character": {"Id": character["id"], "NativeId": character["native_id"], "BasicTarget": 1, "BasicDamageType": 0,
                      "BasicADRatioBp": scalar(character["basic_attack"]["ad_ratio"]),
                      "BasicIntervalMs": character["basic_attack"]["interval_ms"], "Dash": innate_dash},
        "Abilities": [{
            "Id": row["id"], "NativeId": row["native_id"], "Kind": ("active", "passive").index(row["kind"]),
            "Tags": tags(row["tags"]), "Draftable": row["draftable"], "Target": target(row["target"]), "WindupMs": scalar(row["windup"]) if "windup" in row else 0,
            "CooldownFamily": row.get("cooldown_family", ""), "Ranks": [rank(item) for item in row["ranks"]],
        } for row in data["abilities"]],
        "Attributes": [{"Id": row["id"], "NativeId": row["native_id"], "Stats": stats(row["stats"])} for row in data["attributes"]],
        "Equipment": [{
            "Id": row["id"], "NativeId": row["native_id"], "BuyPrice": row["buy_price"], "SellGold": row["sell_gold"],
            "Tags": tags(row["tags"]), "Stats": stats(row["stats"]), "UpgradedStats": stats(row["upgraded_stats"]),
            "HasActive": row["active"] is not None, "Active": active(row["active"]),
            "HasPassive": row["passive"] is not None, "Passive": passive(row["passive"]),
        } for row in data["equipment"]],
        "Monsters": [], "Camps": [{"AnchorId": row["anchor_id"], "MonsterId": row["monster_id"]} for row in data["camps"]["placements"]],
        "Progression": {**{dest: data["progression"][source] for source, dest in (
            ("max_level", "MaxLevel"), ("max_choices", "MaxChoices"), ("max_skill_rank", "MaxSkillRank"),
            ("active_slots", "ActiveSlots"), ("equipment_slots", "EquipmentSlots"), ("xp_start", "XPStart"),
            ("xp_thresholds", "XPThresholds"), ("xp_to_next", "XPToNext"),
        )}, **{dest: data["progression"]["opening_offer"][source] for source, dest in (
            ("active_count", "OpeningActiveCount"), ("passive_count", "OpeningPassiveCount"),
        )}, "RerollLevels": data["progression"]["reroll_levels"]},
        "World": {dest: data["world"][source] for source, dest in (
            ("map_id", "MapId"), ("spawn_ids", "SpawnIds"), ("camp_ids", "CampIds"), ("shop_id", "ShopId"), ("capture_id", "CaptureId"),
        )},
        "Display": [{
            "Id": row["id"], "Namespace": row["namespace"], "NameKey": row["name_key"], "NameText": row["name_text"],
            "DescriptionKey": row["description_key"], "DescriptionText": row["description_text"],
            "IconId": row["icon_id"], "PortraitIconId": row.get("portrait_icon_id", ""),
        } for row in data["display"]],
    }
    for row in data["camps"]["monsters"]:
        monster = {"Id": row["id"], "NativeId": row["native_id"], "Tier": ("Low", "High").index(row["tier"])}
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


def export(output, *, rank_mutations=True):
    # Most cases share the fixture world and its binding sections; validate each distinct pair once.
    with DemoWorld.reuse_binding_reports():
        return _export(output, rank_mutations=rank_mutations)


def _export(output, *, rank_mutations):
    output = Path(output)
    resolved = output.resolve()
    artifacts = (demo.ROOT / "Artifacts").resolve()
    demo.require(resolved.is_relative_to(artifacts) and resolved != artifacts and not output.exists(),
                 "FixturePath", output, "test output must be a NEW directory below this worktree's Artifacts")
    output = resolved
    output.mkdir(parents=True)
    index = {"format": "demo-native-fixtures-1", "test_only": True, "valid": [], "invalid": []}
    base, base_assertions = _valid_case(output, "base")
    index["valid"].append({"name": "base", "catalog_sha256": base.digest.hex(), "changed_paths": [], "mutated_quantity": ""})
    approved = _approved_case(output)
    index["valid"].append({"name": "approved_base", "catalog_sha256": approved.digest.hex(),
                          "changed_paths": [], "mutated_quantity": ""})

    def valid(name, author=None, world=None, literal=None, mutated_quantity=""):
        compilation, assertions = _valid_case(output, name, author, world, literal)
        changes = [key for key, value in assertions.items() if not key.startswith("Identity.") and base_assertions.get(key) != value]
        index["valid"].append({"name": name, "catalog_sha256": compilation.digest.hex(),
                              "changed_paths": changes, "mutated_quantity": mutated_quantity})

    exact = fixtures.content_fixture()
    exact["parameters"]["LIFE.RespawnSec"]["value"] = "__exact_literal__"
    valid("int64_max_ms", exact, literal=b"9223372036854775.807")
    valid("above_double_integer", exact, literal=b"9007199254740.993")
    exact = fixtures.content_fixture()
    exact["parameters"]["SHOP.StartingGold"] = 9007199254740993
    valid("exact_gold", exact)
    exact = fixtures.content_fixture()
    exact["parameters"]["STAT.MPRegen.PerLevel"]["value"] = "__exact_literal__"
    valid("exact_rational", exact, literal=b"0.000000000000000001")
    world = fixtures.world_source_fixture()
    next(row for row in world["anchors"] if row["id"] == "s00")["position_cm"]["x"] += 1
    valid("world_spawn_shift", world=world)
    unicode_author = fixtures.content_fixture()
    unicode_author["identity"]["approval"]["decision"] = "TEST ONLY \u6d4b\u8bd5 \U0001f9ea"
    valid("unicode_metadata", unicode_author)
    unicode_author["identity"]["approval"]["decision"] = "TEST " + "a" * 506 + "\U0001f9ea"
    valid("unicode_scalar_boundary", unicode_author)

    if rank_mutations:
        author = fixtures.content_fixture()
        paths = [path for path, _ in _quantities(author) if "ranks" in path or ("equipment" in path and "rank" in path)]
        for index_number, path in enumerate(paths):
            changed = deepcopy(author)
            quantity = _at(changed, path)
            delta = Decimal("0.001") if quantity["unit"] == "s" else Decimal("0.01")
            quantity["value"] = float(Decimal(str(quantity["value"])) + delta)
            source_path = "content" + "".join(f"[{key}]" if type(key) is int else "." + key for key in path) + ".value"
            native_quantity = base.catalog.field_projection[source_path][0].rsplit(".", 1)[0]
            valid(f"rank_field_{index_number:03d}", changed, mutated_quantity=native_quantity)

    data = json.loads(base.outputs[demo.CONTENT_OUTPUT])
    world_data = json.loads(base.inputs[demo.WORLD_OUTPUT])
    ids_data = json.loads(base.inputs[demo.IDS_SOURCE])

    def invalid(name, bundle, digest, error):
        root = output / name
        for relative, raw in bundle.items():
            _save(root / relative, raw)
        index["invalid"].append({"name": name, "catalog_sha256": digest, "error_contains": error})

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
    bad_content("alias_id", lambda d: d["abilities"][0].update({"id": "Active_arc_bolt"}), "canonical lowercase")
    bad_content("long_id", lambda d: d["abilities"][0].update({"id": "a" * 64}), "string length")
    bad_content("duplicate_ability", lambda d: d["abilities"].__setitem__(1, deepcopy(d["abilities"][0])), "duplicate")
    bad_content("missing_rank", lambda d: d["abilities"][0]["ranks"].pop(), "array length")
    bad_content("wrong_rank", lambda d: d["abilities"][0]["ranks"][1].update({"rank": 3}), "integer outside")
    bad_content("wrong_opcode", lambda d: d["abilities"][0]["ranks"][0]["effects"][0].update({"opcode": 255}), "opcode")
    bad_content("unknown_effect", lambda d: d["abilities"][0]["ranks"][0]["effects"][0].update({"op": "grant_gold"}), "opcode")
    bad_content("duplicate_effect", lambda d: d["abilities"][0]["ranks"][0]["effects"].append(deepcopy(d["abilities"][0]["ranks"][0]["effects"][0])), "duplicate effect")
    bad_content("unknown_effect_field", lambda d: d["abilities"][0]["ranks"][0]["effects"][0].update({"unexpected": 1}), "unknown field")
    bad_content("bad_damage_type", lambda d: d["abilities"][0]["ranks"][0]["effects"][0].update({"damage_type": "True"}), "unknown damage type")
    bad_content("wrong_target", lambda d: d["abilities"][0].update({"target": {"mode": "Self"}}), "wrong definition target")
    bad_content("wrong_family", lambda d: d["abilities"][0].update({"cooldown_family": "cd_wrong"}), "cooldown family")
    bad_content("zero_cooldown", lambda d: d["abilities"][0]["ranks"][0]["cooldown"].update({"ms": 0}), "integer outside")
    bad_content("negative_cost", lambda d: d["abilities"][0]["ranks"][0]["mana_cost"].update({"p2": -1}), "integer outside")
    bad_content("coefficient_overflow", lambda d: d["abilities"][0]["ranks"][0]["effects"][0]["ap_ratio"].update({"bp": demo.INT64_MAX}), "multiplication overflow")
    bad_content("growth_overflow", lambda d: d["parameters"]["STAT.MaxHP.PerLevel"].update({"p2": demo.INT64_MAX}), "multiplication overflow")
    bad_content("inverted_bounds", lambda d: d["parameters"]["STAT.AD.Min"].update({"p2": d["parameters"]["STAT.AD.Max"]["p2"]}), "outside")
    bad_content("build_envelope", lambda d: d["attributes"][0]["stats"]["APFlat"].update({"p2": d["parameters"]["STAT.AP.Max"]["p2"]}), "build")
    bad_content("bad_xp_derived", lambda d: d["progression"]["xp_to_next"].__setitem__(0, 1), "linear XP")
    bad_content("bad_opening_counts", lambda d: d["progression"]["opening_offer"].update({"active_count": 3}), "opening offer")
    bad_content("unsorted_reroll_levels", lambda d: d["progression"].update({"reroll_levels": [7, 4]}), "reroll levels")
    bad_content("unknown_mechanic_tag", lambda d: d["abilities"][0].update({"tags": ["Burst"]}), "mechanic tag")
    bad_content("duplicate_mechanic_tag", lambda d: d["equipment"][0].update({"tags": ["Tank", "Tank"]}), "mechanic tag")
    bad_content("missing_draftable", lambda d: d["abilities"][0].pop("draftable"), "missing")
    bad_content("bad_upgraded_stats", lambda d: next(iter(d["equipment"][0]["upgraded_stats"].values())).update(
        {"p2": 1} if "p2" in next(iter(d["equipment"][0]["upgraded_stats"].values())) else {"bp": 1}), "upgraded stats")
    bad_content("inexact_recovery_ratio", lambda d: d["parameters"]["REGEN.OutOfCombatMaxHPRatioPerSec"].update({"bp": 155}), "recovery")
    bad_content("bad_interval", lambda d: d["character"]["basic_attack"].update({"interval_ms": 1}), "interval")
    bad_content("bad_sell_projection", lambda d: d["equipment"][0].update({"sell_gold": d["equipment"][0]["buy_price"]}), "sell gold")
    bad_content("missing_active", lambda d: d["equipment"][0].update({"active": None}), "presence")
    bad_content("bad_passive_stat", lambda d: d["abilities"][3]["ranks"][0]["effects"][0]["stats"].update({"ADFlat": {"unit": "points", "p2": 1}}), "passive stat")
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
    bad_content("bad_world_ref", lambda d: d["world"].update({"shop_id": "shop_missing"}), "anchor reference")
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
    bad_content("aliased_damage_type", lambda d: d["abilities"][0]["ranks"][0]["effects"][0].update({"damage_type": "magical"}), "unknown damage type")
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
        return next(row for row in world["anchors"] if row["id"] == "l1")

    for name, mutate, error in (
        ("world_missing_anchor", lambda w: w["anchors"].pop(), "array length"),
        ("world_wrong_surface", lambda w: w["anchors"][0].update({"surface_id": "missing_surface"}), "unknown surface"),
        ("world_duplicate_home", lambda w: low_camp(w)["members"].__setitem__(1, deepcopy(low_camp(w)["members"][0])), "duplicate"),
        ("world_member_count", lambda w: low_camp(w)["members"].pop(), "member-home count"),
        ("world_out_of_bounds", lambda w: w["anchors"][0]["position_cm"]["x"].update({"numerator": 6000}), "outside"),
        ("world_bad_bounds", lambda w: w["geometry"]["surfaces"][0]["max_cm"]["x"].update({"numerator": -5000}), "nonempty"),
        ("world_bad_flags", lambda w: w["geometry"]["blockers"][0].update({"blocks_movement": False, "blocks_sight": False}), "classification"),
        ("world_unknown_field", lambda w: w["anchors"][0].update({"home_default": True}), "unknown field"),
        ("world_aliased_kind", lambda w: w["anchors"][0].update({"kind": "spawn"}), "unknown anchor kind"),
        ("world_old_projection", lambda w: w.update({"projection_revision": 1}), "integer outside"),
        ("world_old_schema", lambda w: w.update({"schema_version": 1}), "integer outside"),
        ("world_clockwise_polygon", lambda w: w["geometry"]["blockers"][0]["polygon_cm"].reverse(), "counterclockwise"),
        ("world_duplicate_vertex", lambda w: w["geometry"]["blockers"][0]["polygon_cm"].__setitem__(1, deepcopy(w["geometry"]["blockers"][0]["polygon_cm"][0])), "counterclockwise"),
        ("world_missing_polygon", lambda w: w["geometry"]["blockers"][0].pop("polygon_cm"), "missing"),
        ("world_missing_offset", lambda w: w["placement"]["spawn_offsets_cm"].pop(), "array length"),
        ("world_duplicate_offset", lambda w: w["placement"]["spawn_offsets_cm"].__setitem__(1, deepcopy(w["placement"]["spawn_offsets_cm"][0])), "overlap"),
        ("world_duplicate_connection", lambda w: w["connections"].__setitem__(1, deepcopy(w["connections"][0])), "duplicate connection"),
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
    ids["abilities"]["active_arc_bolt"] = 63
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
    _save(output / "index.json", encode(demo.canonical(index)))
    return index


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        index = export(args.output)
    except (demo.ContentError, OSError) as error:
        parser.exit(1, f"Demo native fixture export failed: {error}\n")
    print(f"TEST ONLY: {len(index['valid'])} valid and {len(index['invalid'])} refusal cases: {args.output}")


if __name__ == "__main__":
    main()
