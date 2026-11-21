"""Strict Demo author projection and file-byte identity; never supplies gameplay defaults."""
from collections.abc import Mapping
from dataclasses import dataclass
from decimal import Context, Decimal, DecimalException, Inexact, localcontext
from fractions import Fraction
import hashlib
from copy import deepcopy
import json
from pathlib import Path
import re
from types import MappingProxyType

from CatalogTools import encode, native_ids


ROOT = Path(__file__).resolve().parents[1]
SOURCE = Path("SourceData", "Demo")
CONTENT_SOURCE = SOURCE / "content.json"
SCHEMA_SOURCE = SOURCE / "content.schema.json"
IDS_SOURCE = SOURCE / "native_ids.json"
NATIVE_IDS = Path("Source", "Lockdown", "Public", "Development", "LDDemoContentIds.h")
DATA = Path("Content", "Lockdown", "Data", "Demo")
CONTENT_OUTPUT = DATA / "demo_content.json"
MANIFEST_OUTPUT = DATA / "manifest.json"
CONTENT_CONTRACT = Path("Source", "Lockdown", "Private", "Development", "LDDemoContentContract.h")
WORLD_OUTPUT = DATA / "world.json"
WORLD_CONTRACT = Path("Source", "Lockdown", "Private", "World", "LDDemoWorldContract.h")
OUTPUT_PATHS = (CONTENT_OUTPUT, CONTENT_CONTRACT, MANIFEST_OUTPUT)
AUTHOR_INPUT_PATHS = (CONTENT_SOURCE, SCHEMA_SOURCE, IDS_SOURCE, NATIVE_IDS)
WORLD_SOURCE = Path("SourceData", "World", "demo_world.json")
WORLD_SCHEMA_SOURCE = Path("SourceData", "World", "demo_world.schema.json")
INPUT_PATHS = (*AUTHOR_INPUT_PATHS, WORLD_SOURCE, WORLD_SCHEMA_SOURCE, WORLD_OUTPUT, WORLD_CONTRACT)
SCHEMA_VERSION = 7
HANDLER_REVISION = 14
PROJECTION_REVISION = 10
NATIVE_IDS_SCHEMA_VERSION = 2
INT64_MAX = (1 << 63) - 1
INT64_MIN = -(1 << 63)
MAX_JSON_BYTES = 8 * 1024 * 1024
ID_PATTERN = re.compile(r"[a-z][a-z0-9_]{0,62}")
HEX_PATTERN = re.compile(r"[0-9a-f]{64}")
NATIVE_ENUMS = {
    "character": "ELDDemoCharacterId", "abilities": "ELDDemoAbilityId",
    "stats": "ELDDemoStatId", "cards": "ELDDemoCardId",
    "final_mods": "ELDDemoFinalModId",
    "monsters": "ELDDemoMonsterId", "statuses": "ELDDemoStatusId",
    "effect_ops": "ELDDemoEffectOp",
}
# Inclusive native numbers that are never reused (#198 retires the passives, cower, attribute cards, their statuses and
# unused ops; #287 P2 retires the components 1-63 below the cards; P4 retires the grievous status and operation and the
# projectile zone).
RETIRED_RANGES = {"abilities": [[0, 91], [104, 113], [119, 125]], "stats": [[0, 26]], "cards": [[0, 199], [214, 218], [237, 242], [255, 255]],
                  "final_mods": [[2, 2], [5, 5], [11, 11], [15, 15], [21, 24]],
                  "statuses": [[7, 8], [11, 11], [15, 19], [21, 24], [28, 29], [33, 33]],
                  "effect_ops": [[5, 6], [9, 11], [15, 17], [19, 19], [23, 23], [25, 30], [32, 36], [38, 39], [41, 49], [51, 51], [53, 53], [55, 68], [72, 76], [83, 89], [96, 96], [100, 100], [102, 102], [104, 104], [120, 129], [131, 137], [139, 145], [147, 151], [154, 154], [159, 159], [161, 161], [165, 169], [195, 195], [235, 235]]}
# #97: three camp difficulty tiers, one monster definition each; the Boss is its own block.
CAMP_TIERS = frozenset(("Low", "High", "Elite", "Ancient"))
# The operations only a skill branch adds to an active (issue #287 spec §5.1); rank 1 never carries them.
BRANCH_OPS = frozenset(("vulnerable", "next_basic_amp", "momentum_gain", "momentum_steal", "follow_up", "recast", "dash_back",
                        "self_buff", "burn_burst", "pulse", "zone_guard", "zone_momentum", "overcharge", "follow_basic",
                        "shield_burst_break", "basic_rider", "delayed_burst", "shield_reflect", "shield_end_heal",
                        "shield_hit_bonus", "brace_stun", "brace_heal", "brace_stack", "brace_shove", "brace_shield"))
# Workshop mod operations a skill branch may also give an active: the branch's own terms reuse the mod's handler.
BRANCH_MOD_OPS = frozenset(("pull", "projectile_return", "projectile_pierce", "echo_impact", "zone_tether", "final_knockback",
                            "empower_reach"))
# The rank-step order of an ability's ranks and the steps each branch path applies over the level-1 definition.
RANK_STEPS = ("2A", "3A1", "3A2", "2B", "3B1", "3B2")
BRANCH_PATHS = {"A": ("2A",), "B": ("2B",), "A1": ("2A", "3A1"), "A2": ("2A", "3A2"), "B1": ("2B", "3B1"), "B2": ("2B", "3B2")}
STEP_PATH = {"2A": "A", "2B": "B", "3A1": "A1", "3A2": "A2", "3B1": "B1", "3B2": "B2"}
# Every operation the importer accepts: the actives' own operations, the skill branch operations and the card operations.
EFFECT_OPS = frozenset(("damage", "shield", "dispel_root", "root", "dash",
                       "slow", "timed_stat_bonus", "self_area",
                       "taunt", "disarm", "stun", "empower_basic", "dispel_slow",
                       "knockback", "zone", "zone_slow", "zone_damage",
                       "silence", "breach", "burn", "zone_burn", "channel_cone", "ground_impact", "attack_slow",
                       "contact_trap", "dash_contact",
                       # #198 session A: elements (wet/electrify), S10 bounce, S16 brace, S20 zone terms, S21 anti-heal.
                       "wet", "electrify", "chain_bounce", "brace", "zone_wet", "zone_haste",
                       # Issue #287 P4: chill and rot, the ground states a skill leaves and N2's fan.
                       "chill", "rot", "ground_state", "fan", "bag",
                       "projectile_return", "projectile_pierce", "echo_impact", "zone_tether", "final_knockback",
                       "empower_reach", "pull",
                       # Skill branch operations (issue #287 spec §5.1).
                       *BRANCH_OPS))
# Card operations (issue #287 spec §7); burn, breach and empower_basic also have an active form.
CARD_ONLY_OPS = frozenset(("thorns", "lifesteal", "triggered_shield", "basic_element", "idle_ready_amp", "cooldown_cost",
                           "knockback_impact", "skill_lifesteal", "hit_haste_other", "setup_mark", "reaction_haste",
                           "combo_momentum", "basic_cooldown_cut", "combo_skill_hit", "combo_knockback", "combo_double",
                           "overcap_extra_hit", "basic_ap_rider", "focus_attack_speed", "skill_hit_momentum", "auto_recast",
                           "skill_damage_factor", "hp_cost", "skill_ad_rider", "skill_growth", "skill_element",
                           "dash_contact_reset", "dash_split", "dash_phase", "dash_contact_element", "takedown_reset",
                           "last_stand", "reaction_momentum", "reaction_element", "skill_crit", "lifesteal_skills",
                           "armor_to_hp", "overheal_shield", "missing_hp_attack_speed", "haste_momentum", "heal_growth",
                           "kill_growth", "control_growth", "idle_charge", "element_aura", "element_duration",
                           "reaction_factor", "burn_cap", "dash_ice", "ground_relocate", "double_stall", "ground_growth",
                           # Issue #292 P5 cards (spec §7).
                           "basic_projectile", "combo_sweep", "combo_cast", "speed_factor", "hit_growth", "control_amp",
                           "reshape", "shield_spend", "recent_cast_amp", "dodge_free_cast", "dash_ram", "overdrive_short",
                           "tally"))
# burn, breach and empower_basic also have an active form; shield_burst_break is S13 A1's branch and Y06's card.
CARD_OPS = CARD_ONLY_OPS | {"burn", "breach", "empower_basic", "shield_burst_break"}
EFFECT_OPS = EFFECT_OPS | CARD_ONLY_OPS
# The operations a card of each form may carry: skill cards change or answer their host skill, dash cards the D dash,
# body cards the holder and its basic attack; an element medal names the element of a skill's or the body's element slot.
FORM_OPS = {
    "skill": frozenset(("hit_haste_other", "skill_hit_momentum", "setup_mark", "auto_recast", "cooldown_cost",
                        "skill_damage_factor", "hp_cost", "skill_ad_rider", "skill_lifesteal", "idle_ready_amp",
                        "skill_growth", "reaction_haste", "takedown_reset", "control_amp", "reshape",
                        "shield_spend", "recent_cast_amp")),
    "dash": frozenset(("dash_contact_reset", "hit_haste_other", "empower_basic", "dash_split", "dash_phase",
                       "dash_ice", "ground_relocate", "dodge_free_cast", "dash_ram", "cooldown_cost", "hit_growth")),
    "body": frozenset(("thorns", "takedown_reset", "last_stand", "reaction_momentum", "reaction_element", "triggered_shield",
                       "knockback_impact", "skill_crit", "lifesteal_skills", "armor_to_hp", "overheal_shield",
                       "missing_hp_attack_speed", "haste_momentum", "lifesteal", "heal_growth", "kill_growth",
                       "control_growth", "idle_charge", "element_aura", "element_duration", "reaction_factor", "burn_cap",
                       "double_stall", "ground_growth", "shield_burst_break", "overdrive_short", "speed_factor", "tally",
                       "breach", "combo_momentum", "basic_cooldown_cut", "combo_skill_hit", "combo_knockback", "combo_double",
                       "overcap_extra_hit", "basic_ap_rider", "focus_attack_speed", "basic_projectile", "combo_sweep",
                       "combo_cast", "hit_growth")),
    "medal": frozenset(("skill_element",)),
}
CARD_FORMS = ("skill", "dash", "body", "medal")
CARD_TYPES = ("trigger", "modify", "convert", "grow", "element")
# Spec §6.3 vocabulary keys in bit order (ELDCardVocab).
CARD_VOCAB = ("basic_hit", "combo3", "skill_hit", "skill_cast", "overdrive", "reaction", "element", "dash", "dash_contact",
              "perfect_dodge", "knockback", "wall_impact", "shield", "shield_break", "controlled", "control", "damaged",
              "low_hp", "kill", "idle", "ground", "heal", "momentum", "attack_speed", "move_speed", "crit", "armor", "max_hp")
# Content element names (electric is the shock element); Y25's aura may name any.
ELEMENTS = ("fire", "water", "electric", "chill", "rot")
AURA_ELEMENTS = ("fire", "water", "electric", "chill", "rot")
GROUND_KINDS = ("puddle", "ice", "fire", "miasma", "field")
# Effects delivered to each hit target; on Self/DirectionDash they need self_area to name their targets.
HOSTILE_OPS = frozenset(("damage", "root", "slow"))
# Elements an active attaches to the bodies it hits (#198 CONTENT_AND_BALANCE §4.4).
ELEMENT_OPS = frozenset(("wet", "electrify", "chill", "rot"))
# Effects a self area delivers to the bodies it names.
AREA_HIT_OPS = frozenset((*HOSTILE_OPS, "stun", "knockback", "silence", "breach", "burn", *ELEMENT_OPS))
# The payloads that give a zone a reason to exist; zone_haste only buffs its owner while inside one.
ZONE_PAYLOAD_OPS = frozenset(("zone_slow", "zone_damage", "zone_burn", "zone_wet"))
# Temporary and conditional bonuses never change resource maxima, so no current-resource conversion is needed.
# Every bonus stat name; a card placeholder may read one stat of its effect.
STAT_FIELDS = frozenset(("ADFlat", "APFlat", "MaxHPFlat", "ArmorFlat", "MagicResistFlat", "CooldownHasteFlat",
                         "AttackRangeCmFlat", "AttackSpeedBonusRatio", "MoveSpeedBonusRatio", "CritChanceBonusRatio",
                         "ADBonusRatio", "HPRegenFlat", "APBonusRatio", "MomentumRegenFlat"))
TEMPORARY_STATS = frozenset(("ADFlat", "APFlat", "ArmorFlat", "MagicResistFlat", "CooldownHasteFlat",
                             "AttackRangeCmFlat", "AttackSpeedBonusRatio", "MoveSpeedBonusRatio",
                             "CritChanceBonusRatio", "ADBonusRatio", "APBonusRatio"))
# #198 outer flow: three timed field segments precede the final field (FLDTimelineParameters::TimedSegments). Map build
# reward targets (airdrops, Boss, camp markers) live only inside them.
TIMED_FIELD_SEGMENTS = 3
# Display text shares one character contract with the native importer. Invisible, control,
# bidirectional and astral characters are refused; braces are reserved by FText formatting.
DISPLAY_TEXT_FORBIDDEN = re.compile(
    "[\u0000-\u001f\u007f-\u009f\u200b-\u200f\u2028-\u202e\u2060-\u206f\ufeff{}\ud800-\udfff\U00010000-\U0010ffff]")
DISPLAY_TEXT_EDGE = frozenset((" ", "\u3000"))
# Numbers shown to players come from typed fields, never from description prose.
DISPLAY_DESCRIPTION_DIGIT = re.compile("[0-9\uff10-\uff19]")


class ContentError(ValueError):
    def __init__(self, code, path, detail):
        self.code = code
        self.path = str(path)
        self.detail = detail
        super().__init__(f"{code}: {path}: {detail}")

    def __reduce__(self):
        # Keeps the error intact when it crosses a process boundary (parallel fixture export).
        return type(self), (self.code, self.path, self.detail)


def require(condition, code, path, detail):
    if not condition:
        raise ContentError(code, path, detail)


def sha256(raw):
    return hashlib.sha256(raw).hexdigest()


def read_bytes(root, relative):
    path = Path(root) / relative
    require(path.is_file(), "MissingInput", relative, "required file is absent; no legacy fallback")
    return path.read_bytes()


def parse_json(raw, label):
    require(type(raw) is bytes and len(raw) <= MAX_JSON_BYTES,
            "InvalidJSON", label, "expected bounded UTF-8 bytes")

    def pairs(entries):
        result = {}
        for key, value in entries:
            require(key not in result, "DuplicateKey", label, f"duplicate object key {key!r}")
            result[key] = value
        return result

    def constant(token):
        raise ContentError("NonFinite", label, f"non-finite JSON constant {token}")

    try:
        return json.loads(raw.decode("utf-8"), object_pairs_hook=pairs,
                          parse_float=Decimal, parse_int=int, parse_constant=constant)
    except ContentError:
        raise
    except (ValueError, DecimalException, RecursionError) as error:
        raise ContentError("InvalidJSON", label, str(error)) from error


def integer(value, path, minimum=0, maximum=INT64_MAX):
    require(type(value) is int, "IntegerType", path, "expected an integer, not bool or decimal")
    require(minimum <= value <= maximum, "NumericOverflow", path, "integer outside declared range")
    return value


def checked_add(left, right, path):
    return integer(left + right, path, INT64_MIN)


def checked_mul(left, right, path):
    return integer(left * right, path, INT64_MIN)


def decimal_parts(value, path):
    require(type(value) in (int, Decimal), "NumericType", path,
            "expected raw JSON integer/Decimal; binary float and bool are not accepted")
    value = Decimal(value) if type(value) is int else value
    require(value.is_finite(), "NonFinite", path, "number must be finite")
    sign, digits, exponent = value.as_tuple()
    if not any(digits):
        return 0, 0
    digits = list(digits)
    while digits[-1] == 0:
        digits.pop()
        exponent += 1
    # Any terminating decimal representable as an int64 pair fits this budget.
    require(len(digits) <= 128 and -128 <= exponent <= 128,
            "NumericOverflow", path, "decimal cannot fit the bounded native representation")
    coefficient = int("".join(str(digit) for digit in digits))
    return -coefficient if sign else coefficient, exponent


def fixed(value, places, path):
    coefficient, exponent = decimal_parts(value, path)
    exponent += places
    require(exponent >= 0 or coefficient == 0, "PrecisionLoss", path,
            f"value is not exactly representable with {places} decimal places")
    if coefficient == 0:
        return 0
    require(len(str(abs(coefficient))) + exponent <= 19, "NumericOverflow", path,
            "scaled value exceeds int64")
    return integer(coefficient * 10 ** exponent, path, INT64_MIN)


def rational(value, path):
    coefficient, exponent = decimal_parts(value, path)
    result = (Fraction(coefficient * 10 ** exponent) if exponent >= 0
              else Fraction(coefficient, 10 ** -exponent))
    integer(result.numerator, path, INT64_MIN)
    integer(result.denominator, path, 1)
    return result


def canonical(value):
    if isinstance(value, Mapping):
        return {key: canonical(value[key]) for key in sorted(value)}
    if isinstance(value, (tuple, list)):
        return [canonical(child) for child in value]
    return value


def freeze(value):
    if isinstance(value, dict):
        return MappingProxyType({key: freeze(child) for key, child in value.items()})
    if isinstance(value, list):
        return tuple(freeze(child) for child in value)
    return value


def exact_equal(left, right):
    if type(left) is not type(right):
        return False
    if isinstance(left, list):
        return len(left) == len(right) and all(exact_equal(a, b) for a, b in zip(left, right))
    if isinstance(left, dict):
        return left.keys() == right.keys() and all(exact_equal(left[k], right[k]) for k in left)
    return left == right


SCHEMA_KEYS = frozenset(("$schema", "$defs", "$ref", "title", "description", "type", "const",
                         "enum", "oneOf", "properties", "required", "additionalProperties",
                         "items", "minItems", "maxItems", "minLength", "maxLength", "pattern",
                         "minimum", "maximum", "exclusiveMinimum", "minProperties", "x-quantity"))
QUANTITY_UNITS = {
    "P2": frozenset(("hp", "momentum", "points")), "R4": frozenset(("ratio",)), "Sec": frozenset(("s",)),
    "Rational": frozenset(("cm", "cm/s", "attacks/s", "hp/s", "momentum/s", "degrees")),
}


class AuthorSchema:
    """Closed schema subset with exact numbers and explicit quantity projection."""
    def __init__(self, schema):
        self.schema = schema
        self._inspect(schema, "schema", ())

    def _resolve(self, reference, path):
        require(type(reference) is str and reference.startswith("#/$defs/")
                and reference.count("/") == 2, "InvalidSchema", path, "only local definition references")
        name = reference[len("#/$defs/"):]
        require(name in self.schema.get("$defs", {}), "InvalidSchema", path, "unknown schema reference")
        return self.schema["$defs"][name]

    def _inspect(self, node, path, references):
        require(type(node) is dict and set(node) <= SCHEMA_KEYS,
                "InvalidSchema", path, "unsupported schema keyword or node")
        for group in ("$defs", "properties"):
            require(type(node.get(group, {})) is dict, "InvalidSchema", path, f"{group} must be an object")
            require(all(type(child) is dict for child in node.get(group, {}).values()),
                    "InvalidSchema", path, f"{group} contains a non-schema value")
        for key in ("required", "enum", "oneOf"):
            if key in node:
                require(type(node[key]) is list, "InvalidSchema", path, f"{key} must be an array")
        if "required" in node:
            require(all(type(key) is str for key in node["required"])
                    and len(set(node["required"])) == len(node["required"]),
                    "InvalidSchema", path, "invalid or duplicate required field")
        for key in ("minItems", "maxItems", "minLength", "maxLength", "minProperties"):
            if key in node:
                require(type(node[key]) is int and 0 <= node[key] <= INT64_MAX,
                        "InvalidSchema", path, f"{key} must be a nonnegative bounded integer")
        for key in ("minimum", "maximum", "exclusiveMinimum"):
            if key in node:
                require(type(node[key]) in (int, Decimal)
                        and (type(node[key]) is int or node[key].is_finite()),
                        "InvalidSchema", path, f"{key} must be finite numeric data")
        if "type" in node:
            require(node["type"] in ("object", "array", "string", "integer", "number", "boolean", "null"),
                    "InvalidSchema", path, "unsupported schema type")
        if "oneOf" in node or "enum" in node:
            require(node.get("oneOf", node.get("enum")), "InvalidSchema", path, "empty variant/enum")
        if "pattern" in node:
            require(type(node["pattern"]) is str, "InvalidSchema", path, "pattern must be a string")
            try:
                re.compile(node["pattern"])
            except re.error as error:
                raise ContentError("InvalidSchema", path, "invalid regular expression") from error
        if "$ref" in node:
            require(set(node) == {"$ref"} and node["$ref"] not in references,
                    "InvalidSchema", path, "reference siblings or recursive schema")
            self._inspect(self._resolve(node["$ref"], path), path, references + (node["$ref"],))
        if "x-quantity" in node:
            require(type(node["x-quantity"]) is str and node["x-quantity"] in QUANTITY_UNITS
                    and node.get("type") == "object"
                    and set(node.get("properties", {})) == {"unit", "value"}
                    and set(node.get("required", [])) == {"unit", "value"}
                    and node.get("additionalProperties") is False,
                    "InvalidSchema", path, "invalid quantity contract")
            require(type(node["properties"]["unit"].get("const")) is str
                    and node["properties"]["unit"]["const"] in QUANTITY_UNITS[node["x-quantity"]]
                    and node["properties"]["value"].get("type") == "number",
                    "InvalidSchema", path, "unsupported unit or numeric quantity type")
        if node.get("type") == "object":
            require(node.get("additionalProperties") is False, "InvalidSchema", path,
                    "author objects must reject undeclared fields")
            require(set(node.get("required", [])) <= set(node.get("properties", {})),
                    "InvalidSchema", path, "unknown required field")
        for group in ("$defs", "properties"):
            for key, child in node.get(group, {}).items():
                self._inspect(child, f"{path}.{key}", references)
        if "items" in node:
            self._inspect(node["items"], path + "[]", references)
        for index, child in enumerate(node.get("oneOf", [])):
            self._inspect(child, f"{path}.oneOf[{index}]", references)

    def validate(self, value, node=None, path="content"):
        node = self.schema if node is None else node
        if "$ref" in node:
            return self.validate(value, self._resolve(node["$ref"], path), path)
        types = {
            "object": lambda x: type(x) is dict, "array": lambda x: type(x) is list,
            "string": lambda x: type(x) is str, "integer": lambda x: type(x) is int,
            "number": lambda x: type(x) in (int, Decimal),
            "boolean": lambda x: type(x) is bool, "null": lambda x: x is None,
        }
        if "type" in node:
            require(node["type"] in types, "InvalidSchema", path, "unsupported type")
            require(types[node["type"]](value), "SchemaType", path, f"expected {node['type']}")
        if "const" in node:
            require(exact_equal(value, node["const"]), "SchemaConstant", path, "wrong constant or type")
        if "enum" in node:
            require(any(exact_equal(value, choice) for choice in node["enum"]),
                    "UnknownEnum", path, f"unsupported value {value!r}")
        if "oneOf" in node:
            matches, errors = [], []
            for branch in node["oneOf"]:
                try:
                    self.validate(value, branch, path)
                    matches.append(branch)
                except ContentError as error:
                    errors.append(str(error))
            require(len(matches) == 1, "SchemaVariant", path,
                    "expected exactly one typed variant; " + " | ".join(errors))
        if type(value) is dict:
            properties = node.get("properties", {})
            if "properties" in node:
                require(set(node.get("required", [])) <= set(value), "MissingField", path,
                        f"missing {sorted(set(node.get('required', [])) - set(value))}")
                require(set(value) <= set(properties), "UnknownField", path,
                        f"unexpected {sorted(set(value) - set(properties))}")
                for key, child in value.items():
                    self.validate(child, properties[key], f"{path}.{key}")
            require(len(value) >= node.get("minProperties", 0), "EmptyDefinition", path, "empty object")
        if type(value) is list:
            require(node.get("minItems", 0) <= len(value) <= node.get("maxItems", INT64_MAX),
                    "ArrayLength", path, "wrong array length")
            if "items" in node:
                for index, child in enumerate(value):
                    self.validate(child, node["items"], f"{path}[{index}]")
        if type(value) is str:
            require(node.get("minLength", 0) <= len(value) <= node.get("maxLength", INT64_MAX),
                    "StringLength", path, "wrong string length")
            require("pattern" not in node or re.fullmatch(node["pattern"], value) is not None,
                    "InvalidString", path, "invalid identifier or text")
            try:
                value.encode("utf-8")
            except UnicodeEncodeError as error:
                raise ContentError("InvalidString", path, "invalid UTF-8 scalar") from error
        if type(value) in (int, Decimal):
            require(type(value) is int or value.is_finite(), "NonFinite", path, "number must be finite")
            require("minimum" not in node or value >= node["minimum"], "NumericDomain", path, "below minimum")
            require("maximum" not in node or value <= node["maximum"], "NumericOverflow", path, "above maximum")
            require("exclusiveMinimum" not in node or value > node["exclusiveMinimum"],
                    "NumericDomain", path, "must be positive")
        if "x-quantity" in node:
            self._quantity(value, node["x-quantity"], path)

    @staticmethod
    def _quantity(value, storage, path):
        raw = value["value"]
        if storage == "Rational":
            fraction = rational(raw, path)
            return {"unit": value["unit"], "numerator": fraction.numerator, "denominator": fraction.denominator}
        places, key = {"P2": (2, "p2"), "R4": (4, "bp"), "Sec": (3, "ms")}[storage]
        return {"unit": "ms" if storage == "Sec" else value["unit"], key: fixed(raw, places, path)}

    def project(self, value, node=None, path="content", fields=None):
        node = self.schema if node is None else node
        fields = {} if fields is None else fields
        if "$ref" in node:
            return self.project(value, self._resolve(node["$ref"], path), path, fields)
        if "oneOf" in node:
            for branch in node["oneOf"]:
                try:
                    self.validate(value, branch, path)
                except ContentError:
                    continue
                return self.project(value, branch, path, fields)
            raise ContentError("SchemaVariant", path, "unvalidated variant")
        if "x-quantity" in node:
            result = self._quantity(value, node["x-quantity"], path)
            fields[path + ".unit"] = (path + ".unit",)
            fields[path + ".value"] = tuple(path + "." + key for key in result if key != "unit")
            return result
        if type(value) is dict:
            return {key: self.project(child, node["properties"][key], f"{path}.{key}", fields)
                    for key, child in value.items()}
        if type(value) is list:
            return [self.project(child, node.get("items", {}), f"{path}[{index}]", fields)
                    for index, child in enumerate(value)]
        require(type(value) is not Decimal, "UnprojectedNumber", path, "number has no typed storage")
        if type(value) is int:
            integer(value, path, INT64_MIN)
        fields[path] = (path,)
        return value


def keys(value, expected, path):
    require(isinstance(value, Mapping) and set(value) == set(expected), "ObjectShape", path,
            f"expected fields {sorted(expected)}")


def definition_id(value, path):
    require(type(value) is str and ID_PATTERN.fullmatch(value) is not None,
            "InvalidId", path, "expected canonical lowercase ASCII, at most 63 bytes")
    return value


def rows_by_id(rows, path, key="id"):
    result = {}
    for row in rows:
        identity = definition_id(row[key], path)
        require(identity not in result, "DuplicateId", path, f"duplicate {identity}")
        result[identity] = row
    return result


def validate_native_ids(root, value):
    keys(value, {"schema_version", "retired_ranges", *NATIVE_ENUMS}, IDS_SOURCE)
    integer(value["schema_version"], IDS_SOURCE, NATIVE_IDS_SCHEMA_VERSION, NATIVE_IDS_SCHEMA_VERSION)
    require(exact_equal(value["retired_ranges"], RETIRED_RANGES),
            "RetiredId", IDS_SOURCE, "occupied legacy ranges must remain reserved")
    for section, enum_name in NATIVE_ENUMS.items():
        mapping = value[section]
        require(type(mapping) is dict and mapping, "NativeId", section, "missing ID mapping")
        for name, number in mapping.items():
            definition_id(name, section)
            integer(number, name, 0, 65535 if section == "cards" else 255)
        require(len(set(mapping.values())) == len(mapping), "DuplicateId", section, "duplicate native number")
        declared = native_ids(root, NATIVE_IDS, enum_name)
        expected = {re.sub(r"(?<!^)(?=[A-Z])", "_", name).lower(): number for name, number in declared.items()}
        require(mapping == expected, "NativeId", section, "renumbered, unknown or unmapped native definition")
        retired = value["retired_ranges"].get(section, [])
        require(not any(first <= number <= last for number in mapping.values() for first, last in retired),
                "RetiredId", section, "cannot reuse a retired identity")
    require(set(value["effect_ops"]) == EFFECT_OPS, "MissingHandler", IDS_SOURCE,
            "effect projection handlers and native operation contract differ")


def quantity(value):
    if "p2" in value:
        return Fraction(value["p2"], 100)
    if "bp" in value:
        return Fraction(value["bp"], 10000)
    if "ms" in value:
        return Fraction(value["ms"], 1000)
    return Fraction(value["numerator"], value["denominator"])


def ceil_fraction(value, path):
    return integer(-(-value.numerator // value.denominator), path)


def validate_integer_tree(value, path):
    if type(value) is dict:
        for key, child in value.items():
            require(type(key) is str, "WireShape", path, "object key is not a string")
            validate_integer_tree(child, f"{path}.{key}")
    elif type(value) is list:
        for index, child in enumerate(value):
            validate_integer_tree(child, f"{path}[{index}]")
    elif type(value) is int:
        integer(value, path, INT64_MIN)
    else:
        require(value is None or type(value) in (str, bool), "WireShape", path,
                "wire tree must not contain floating-point/Decimal values")


def _effect_set(effects, path):
    operations = [effect["op"] for effect in effects]
    require(len(set(operations)) == len(operations), "DuplicateEffect", path, "repeated operation")
    require(set(operations) <= EFFECT_OPS, "MissingHandler", path, "unsupported effect operation")
    return frozenset(operations)


def _positive_stats(stats, path):
    require(stats and all(quantity(value) > 0 for value in stats.values()),
            "IneffectiveChoice", path, "every declared stat bonus must be positive")


# Positive-magnitude fields of the #112 handlers: at least one of each group must be positive.
_NEW_OP_MAGNITUDES = {
    "zone_damage": ("base", "ad_ratio", "ap_ratio"), "knockback": ("distance",), "zone": ("radius",), "zone_slow": ("ratio",),
}


# #147 optional terms that only come in pairs (the native importer refuses the same shapes).
_PAIRED_FIELDS = {
    "lifesteal": (("hp_below", "low_hp_multiplier"),),
    "triggered_shield": (("burst_ratio", "burst_radius"),),
    "channel_cone": (("slow", "slow_duration"),),
}


def _validate_new_effect(effect, path):
    for pair in _PAIRED_FIELDS.get(effect["op"], ()):
        require(all(key in effect for key in pair) or not any(key in effect for key in pair), "ObjectShape", path,
                f"{effect['op']} needs {' and '.join(pair)} together")
    fields = _NEW_OP_MAGNITUDES.get(effect["op"])
    if fields is not None:
        require(any(quantity(effect[key]) > 0 for key in fields), "IneffectiveEffect", path,
                f"{effect['op']} has neither magnitude nor scaling")
    if effect["op"] == "zone_slow":
        require(quantity(effect["ratio"]) < 1, "NumericDomain", path, "a slow must keep something")
    if effect["op"] == "zone":
        require(effect["delay"]["ms"] + effect["interval"]["ms"] * (effect["count"] - 1) <= effect["duration"]["ms"],
                "InvalidTiming", path, "zone pulses must fall inside its duration")
    if effect["op"] == "attack_slow":
        require(0 < quantity(effect["ratio"]) < 1, "NumericDomain", path,
                "reduction ratio must be strictly between zero and one")
    if effect["op"] in ("ground_impact", "contact_trap"):
        inner = "inner_radius" if effect["op"] == "ground_impact" else "trigger_radius"
        require(0 < quantity(effect[inner]) <= quantity(effect["radius"]), "InvalidGeometry", path,
                "inner/trigger radius must be positive and fit the effect radius")
    if effect["op"] == "ground_impact":
        require(0 < quantity(effect["outer_ratio"]) < 1, "NumericDomain", path,
                "outer damage must be a positive fraction of central damage")
    if effect["op"] == "channel_cone":
        checked_mul(effect["interval"]["ms"], effect["count"] - 1, path)
    if "slow" in effect and effect["op"] == "channel_cone":
        require(0 < quantity(effect["slow"]) < 1, "NumericDomain", path, "slow must be a positive fraction")
    # #198 session A (CONTENT_AND_BALANCE §4.2).
    if effect["op"] in ("chain_bounce", "brace", "zone_haste"):
        require(0 < quantity(effect["ratio"]) < 1, "NumericDomain", path,
                f"{effect['op']} ratio must be strictly between zero and one")
    if effect["op"] in ("chain_bounce", "brace"):
        require(quantity(effect["radius"]) > 0, "InvalidGeometry", path, f"{effect['op']} radius must be positive")
    if effect["op"] == "brace":
        require(quantity(effect["limit"]) > 0 and quantity(effect["record_ratio"]) > 0, "IneffectiveEffect", path,
                "a brace records a positive amount and releases a positive share of it")
    # Issue #287 P4 (CONTENT_AND_BALANCE §4.4).
    if effect["op"] == "rot" and "ratio" in effect:
        require(0 < quantity(effect["ratio"]) < 1, "NumericDomain", path, "a rot cut is strictly between zero and one")
    if effect["op"] == "ground_state":
        require(effect["kind"] in GROUND_KINDS, "InvalidReference", path, "unknown ground state")
        if "attack_slow" in effect:
            require(0 < quantity(effect["attack_slow"]) < 1, "NumericDomain", path, "an attack slow keeps something")
    if effect["op"] == "bag":
        require(quantity(effect["hp_base"]) > 0 and quantity(effect["radius"]) > 0 and quantity(effect["size"]) > 0,
                "InvalidGeometry", path, "a bag has life, a body and a blast")
    if effect["op"] == "fan":
        require(effect["interval"]["ms"] <= effect["duration"]["ms"], "InvalidTiming", path, "a fan blows inside its life")
        require(quantity(effect["length"]) > 0 and quantity(effect["width"]) > 0 and quantity(effect["distance"]) > 0,
                "InvalidGeometry", path, "a fan blows a positive rectangle a positive distance")


def _validate_action(target, effects, path):
    mode = target["mode"]
    allowed = {
        "Self": frozenset(("shield", "dispel_root", "dispel_slow", "timed_stat_bonus", "self_area", "taunt", "stun",
                           "knockback", *HOSTILE_OPS, "silence", "breach", "burn", "empower_basic", *ELEMENT_OPS,
                           "brace", "vulnerable", "momentum_gain", "pulse", "burn_burst", "self_buff", "basic_rider",
                           "shield_reflect", "shield_end_heal", "shield_hit_bonus", "brace_stun", "brace_heal", "brace_stack",
                           "brace_shove", "brace_shield", "pull")),
        "Unit": frozenset(("damage", "root", "slow", "disarm", "stun", "silence", "breach", "burn", "knockback",
                           *ELEMENT_OPS, "chain_bounce", "vulnerable", "next_basic_amp", "momentum_gain",
                           "momentum_steal", "follow_up", "recast", "dash_back", "self_buff", "pull", "overcharge")),
        "DirectionProjectile": frozenset(("damage", "slow", "silence", "breach", "burn", "attack_slow", *ELEMENT_OPS,
                                          "knockback", "pull", "projectile_return", "projectile_pierce",
                                          "next_basic_amp", "ground_state", "follow_up", "momentum_gain")),
        "DirectionLine": frozenset(("damage", "knockback", "slow", "burn", *ELEMENT_OPS, "pull", "vulnerable", "momentum_gain")),
        "DirectionDash": frozenset(("dash", "self_area", "empower_basic", "zone", "zone_damage",
                                    *HOSTILE_OPS, "breach", "burn", "dash_contact", "shield", *ELEMENT_OPS,
                                    "vulnerable", "follow_basic", "recast", "momentum_gain", "delayed_burst", "basic_rider",
                                    "shield_burst_break", "empower_reach", "final_knockback")),
        "GroundPoint": frozenset(("zone", "zone_slow", "zone_damage", "zone_burn", "zone_wet", "zone_haste",
                                  "ground_impact", "contact_trap", "damage", "root", "slow", "burn", "knockback", *ELEMENT_OPS,
                                  "zone_tether", "zone_guard", "zone_momentum", "echo_impact", "momentum_gain", "fan",
                                  "bag", "ground_state")),
        "DirectionCone": frozenset(("channel_cone", "damage", "knockback", "momentum_gain", "final_knockback", "silence", *ELEMENT_OPS,
                                    "ground_state")),
    }[mode]
    ops = _effect_set(effects, path)
    require(not ops & CARD_ONLY_OPS, "CardOnlyEffect", path,
            f"an active cannot carry card operations {sorted(ops & CARD_ONLY_OPS)}")
    require(not any("trigger" in effect for effect in effects if effect["op"] == "empower_basic"), "ObjectShape", path,
            "an active's empowered basic is armed by its release, not by a trigger")
    require(ops <= allowed, "TargetEffectMismatch", path, "effect cannot consume this target form")
    if mode in ("Self", "DirectionDash") and "dash_contact" not in ops:
        require(("self_area" in ops) == bool(ops & AREA_HIT_OPS), "TargetEffectMismatch", path,
                "hit effects of a self or dash action need exactly one self_area to name their targets")
    if mode == "DirectionProjectile":
        require("damage" in ops, "TargetEffectMismatch", path, "a projectile carries damage")
    require("ground_state" not in ops or mode in ("DirectionProjectile", "DirectionCone") or "bag" in ops, "TargetEffectMismatch",
            path, "a skill leaves its ground state where its projectile lands, over its cone or where its bag blows")
    require(mode != "DirectionDash" or not ops & ELEMENT_OPS or "dash_contact" in ops, "TargetEffectMismatch", path,
            "a dash attaches its element to the bodies it actually touches")
    require("brace" not in ops or ops <= {"brace", "brace_stun", "brace_heal", "brace_stack", "brace_shove", "brace_shield"},
            "TargetEffectMismatch", path, "a brace is the whole action: it delivers nothing else")
    require(not ops & {"brace_stun", "brace_heal", "brace_stack", "brace_shove", "brace_shield"} or "brace" in ops,
            "MissingHandler", path, "brace terms need their brace")
    require(not ops & {"shield_reflect", "shield_end_heal", "shield_hit_bonus", "shield_burst_break"} or "shield" in ops,
            "MissingHandler", path, "shield terms need their shield")
    require("shield_hit_bonus" not in ops or "self_area" in ops, "MissingHandler", path, "a shield hit bonus needs a self area")
    require(not ops & {"zone_tether", "zone_guard", "zone_momentum"} or "zone" in ops, "MissingHandler", path,
            "zone terms need their zone")
    require("echo_impact" not in ops or "ground_impact" in ops, "MissingHandler", path, "an echo repeats a ground impact")
    require("pull" not in ops or bool(ops & {"knockback", "taunt"}), "MissingHandler", path,
            "a pull turns a knockback or a taunt toward the caster")
    require(not ops & {"projectile_return", "projectile_pierce"} or mode == "DirectionProjectile", "MissingHandler", path,
            "return and pierce change a projectile")
    require(not ops & {"empower_reach", "final_knockback"} or bool(ops & {"empower_basic", "channel_cone"}), "MissingHandler",
            path, "reach and final knockback change empowered basics or the last wave")
    require("pulse" not in ops or "self_area" in ops, "MissingHandler", path, "a pulse repeats the self area")
    require("burn_burst" not in ops or "burn" in ops, "MissingHandler", path, "a burn burst reads the burn it adds")
    require("recast" not in ops or "damage" in ops, "MissingHandler", path, "a recast repeats a damaging release")
    require("follow_basic" not in ops or "dash_contact" in ops, "MissingHandler", path, "a follow-up basic needs a contact")
    require("chain_bounce" not in ops or "damage" in ops, "MissingHandler", path, "a chain bounce carries damage")
    require(mode != "GroundPoint" or len(ops & {"zone", "ground_impact", "contact_trap", "fan", "bag"}) == 1,
            "MissingHandler", path, "a ground point needs exactly one delivery handler")
    require(mode != "DirectionCone" or {"channel_cone", "damage"} <= ops,
            "MissingHandler", path, "a cone needs its channel and damage")
    require("dash_contact" not in ops or ("damage" in ops and "self_area" not in ops),
            "TargetEffectMismatch", path, "contact damage must use the actual dash, not a release area")
    require(not ops.intersection({"ground_impact", "contact_trap"}) or "damage" in ops,
            "MissingHandler", path, "a delayed impact or trap needs damage")
    require(mode != "DirectionDash" or "dash" in ops, "MissingHandler", path, "a direction dash needs its dash")
    require(not (ops & (ZONE_PAYLOAD_OPS | {"zone_haste"})) or "zone" in ops,
            "MissingHandler", path, "zone payloads need their zone")
    require("zone" not in ops or bool(ops & ZONE_PAYLOAD_OPS),
            "IneffectiveEffect", path, "a zone needs a payload")
    for effect in effects:
        _validate_new_effect(effect, path)
        if effect["op"] == "empower_basic":
            require(any(quantity(effect[key]) > 0 for key in ("base", "ad_ratio", "ap_ratio")),
                    "IneffectiveEffect", path, "an empowered attack adds neither damage nor scaling")
            if "stats" in effect:
                require(set(effect["stats"]) <= TEMPORARY_STATS, "UnknownField", path,
                        "timed bonuses cannot change resource maxima")
                _positive_stats(effect["stats"], path)
        if effect["op"] in ("breach", "burn"):
            require("stacks" in effect and "basic_stacks" not in effect, "ObjectShape", path,
                    "an active applies its stacks to the bodies it hits")
        if effect["op"] == "dash":
            require(quantity(effect["distance"]) > 0, "NumericDomain", path, "geometry must be positive")
        if effect["op"] in ("damage", "shield"):
            quantities = [effect["base"], effect["ap_ratio"]] + ([effect["max_hp_ratio"]] if "max_hp_ratio" in effect else [])
            if effect["op"] == "damage":
                quantities += [effect["ad_ratio"]]
            require(any(quantity(value) > 0 for value in quantities),
                    "IneffectiveEffect", path, "effect has neither magnitude nor scaling")
        if effect["op"] == "slow":
            require(0 < quantity(effect["ratio"]) < 1, "NumericDomain", path, "slow must keep some move speed")
        if effect["op"] == "self_area":
            require(quantity(effect["radius"]) > 0, "NumericDomain", path, "area radius must be positive")
        if effect["op"] == "timed_stat_bonus":
            require(set(effect["stats"]) <= TEMPORARY_STATS, "UnknownField", path,
                    "timed bonuses cannot change resource maxima")
            _positive_stats(effect["stats"], path)
        if effect["op"] == "taunt":
            require(quantity(effect["radius"]) > 0, "NumericDomain", path, "taunt radius must be positive")
    if mode in ("Unit", "DirectionProjectile", "GroundPoint", "DirectionCone", "DirectionLine"):
        require(quantity(target["range"]) > 0, "NumericDomain", path, "cast range must be positive")
    if mode == "DirectionProjectile":
        require(quantity(target["projectile_radius"]) > 0, "NumericDomain", path, "projectile radius must be positive")
    if mode == "DirectionLine":
        require(quantity(target["half_width"]) > 0, "NumericDomain", path, "line half width must be positive")


# Effect fields that make a card operation do something (the native importer refuses the same shapes).
_CARD_MAGNITUDES = {
    "combo_momentum": ("momentum",), "basic_cooldown_cut": ("cooldown",), "combo_knockback": ("distance",),
    "combo_double": ("damage_ratio",), "overcap_extra_hit": ("threshold", "step", "ratio"), "basic_ap_rider": ("ap_ratio",),
    "focus_attack_speed": ("ratio",), "skill_hit_momentum": ("momentum",), "auto_recast": ("damage_ratio",),
    "skill_damage_factor": ("ratio",), "hp_cost": ("ratio",), "skill_ad_rider": ("ad_ratio",), "skill_growth": ("ratio",),
    "dash_split": ("distance",), "last_stand": ("threshold",), "reaction_momentum": ("momentum",), "armor_to_hp": ("max_hp",),
    "missing_hp_attack_speed": ("ratio",), "haste_momentum": ("haste", "momentum"),
    "heal_growth": ("heal", "max_hp"), "idle_charge": ("ratio",), "element_aura": ("radius",), "reaction_factor": ("ratio",),
    "dash_ice": ("width", "duration"), "ground_growth": ("ratio", "interval"),
    "idle_ready_amp": ("ratio",), "thorns": ("ratio",), "skill_lifesteal": ("ratio",), "lifesteal": ("ratio",),
    "basic_projectile": ("distance", "speed", "radius"), "combo_sweep": ("angle", "ratio"), "combo_cast": ("damage_ratio",),
    "speed_factor": ("ratio",), "control_amp": ("ratio",), "shield_spend": ("ratio",), "recent_cast_amp": ("ratio",),
    "dash_ram": ("ratio", "distance"), "overdrive_short": ("momentum", "multiplier"), "tally": ("base",),
    "shield_burst_break": ("radius",),
}
# Spec §7 P5: skill cards that only fit some hosts name them (K07, K10 by delivery; K05, K11 a host that controls).
CONTROL_OPS = frozenset(("root", "stun", "silence", "disarm", "taunt"))


def _validate_cards(data):
    """Spec §6: one card row per definition in ascending native order; each carries only the operations of its form."""
    previous = 0
    names = set()
    for row in data["cards"]:
        identity = row["id"]
        require(row["native_id"] > previous, "CardOrder", "cards", "cards are authored once each in ascending native ID order")
        previous = row["native_id"]
        require(len(set(row["vocab"])) == len(row["vocab"]), "InvalidReference", identity, "vocabulary keys are distinct")
        ops = _effect_set(row["effects"], identity)
        require(ops <= FORM_OPS[row["form"]], "CardForm", identity,
                f"a {row['form']} card cannot carry {sorted(ops - FORM_OPS[row['form']])}")
        require(row["form"] == "skill" or ("host_hit_types" not in row and "host_control" not in row), "CardForm", identity,
                "only a skill card limits its host")
        if "host_hit_types" in row:
            require(len(set(row["host_hit_types"])) == len(row["host_hit_types"]), "InvalidReference", identity,
                    "host hit types are distinct")
        require(("reshape" in ops) <= ("host_hit_types" in row or "host_control" in row), "CardHost", identity,
                "a reshaping card names the hosts it fits")
        require(("control_amp" in ops) <= ("host_control" in row), "CardHost", identity,
                "a control card fits a host that controls")
        require(("choose_element" in row) == ("element_aura" in ops), "CardElementChoice", identity,
                "an element aura names its element choices and nothing else does")
        if "choose_element" in row:
            require(len(set(row["choose_element"])) == len(row["choose_element"]), "InvalidReference", identity,
                    "element choices are distinct")
            require(set(row["choose_element"]) <= set(AURA_ELEMENTS), "InvalidReference", identity,
                    "an aura holds an element a target can hold")
        for effect in row["effects"]:
            if effect["op"] == "overheal_shield":
                # Y14 shields overflowing heals (cap_ratio), Y29 the burn it deals (burn_ratio): exactly one, positive.
                shares = [key for key in ("cap_ratio", "burn_ratio") if key in effect]
                require(len(shares) == 1 and quantity(effect[shares[0]]) > 0, "IneffectiveEffect", identity,
                        "an overheal shield names one positive cap_ratio or burn_ratio")
            fields = _CARD_MAGNITUDES.get(effect["op"])
            if fields is not None:
                require(all(quantity(effect[key]) > 0 for key in fields), "IneffectiveEffect", identity,
                        f"{effect['op']} needs positive {', '.join(fields)}")
            if effect["op"] in ("breach", "burn"):
                require("basic_stacks" in effect and "stacks" not in effect, "ObjectShape", identity,
                        "a card applies its stacks with each primary basic hit")
            if effect["op"] == "empower_basic":
                require(effect.get("trigger") == "DashComplete" and "charges" in effect, "ObjectShape", identity,
                        "a card's empowered basic is armed by a completed dash and names its charges")
                require(any(quantity(effect[key]) > 0 for key in ("base", "ad_ratio", "ap_ratio")), "IneffectiveEffect",
                        identity, "an empowered attack adds neither damage nor scaling")
            if effect["op"] == "cooldown_cost":
                require(quantity(effect["cooldown_ratio"]) > 0 and quantity(effect["cost_ratio"]) > 0, "NumericDomain",
                        identity, "cooldown and cost ratios are positive")
            if effect["op"] in ("kill_growth", "control_growth"):
                _positive_stats(effect["stats"], identity)
                require(set(effect["stats"]) <= {"ADFlat", "APFlat", "ArmorFlat", "MagicResistFlat"}, "UnknownField",
                        identity, "match growth adds flat attack, power or resistances")
            if effect["op"] == "speed_factor":
                below = effect["trigger"] == "AttackSpeedBelow"
                require(("threshold" in effect and "step" in effect and "step_ratio" not in effect) if below
                        else ("step_ratio" in effect and "threshold" not in effect and "step" not in effect), "ObjectShape",
                        identity, "an attack-speed factor names its threshold and step; a move-speed factor its step ratio")
                require(row["form"] == "body", "CardForm", identity, "a speed factor is a body card")
            if effect["op"] == "hit_growth":
                _positive_stats(effect["stats"], identity)
                basic = effect["trigger"] == "BasicHit"
                require(row["form"] == ("body" if basic else "dash"), "CardForm", identity,
                        "basic hits grow a body card; perfect dodges a dash card")
                require(set(effect["stats"]) <= ({"ADFlat", "APFlat"} if basic else {"MoveSpeedBonusRatio"}), "UnknownField",
                        identity, "basic hits grow attack or power; perfect dodges grow move speed")
            if effect["op"] == "reshape":
                fields = {key for key in ("area", "range", "far", "far_ratio", "control") if key in effect}
                require(fields and all(quantity(effect[key]) > 0 for key in fields), "IneffectiveEffect", identity,
                        "a reshape changes at least one term")
                require(("far" in fields) == ("far_ratio" in fields), "ObjectShape", identity, "a far bonus names its distance")
                require("area" not in fields or quantity(effect["area"]) > 1, "NumericDomain", identity, "an area only grows")
            if effect["op"] == "cooldown_cost" and "momentum" in effect:
                require(row["form"] == "dash" and quantity(effect["momentum"]) > 0, "CardForm", identity,
                        "only the D dash takes a flat momentum cost")
            if effect["op"] == "overdrive_short":
                require(quantity(effect["multiplier"]) > 1, "NumericDomain", identity, "an overdrive still scales up")
            if effect["op"] == "element_duration":
                require(quantity(effect["multiplier"]) > 1, "NumericDomain", identity, "an element duration only grows")


def ability_vocab(ability):
    """Spec §6.3: a skill's vocabulary follows from its element, delivery, control, displacement and shield."""
    ops = {effect["op"] for effect in ability["effects"]}
    vocab = set()
    if ability["element"] != "none" or ops & {"wet", "electrify", "chill", "rot", "burn", "zone_burn", "zone_wet"}:
        vocab.add("element")
    if ops & {"ground_state", "fan"}:
        vocab.add("ground")
    if ability["target"]["mode"] == "DirectionDash":
        vocab.update(("dash", "dash_contact") if "dash_contact" in ops else ("dash",))
    if ops & {"zone", "contact_trap", "ground_impact", "bag"}:
        vocab.add("ground")
    if ops & {"root", "stun", "silence", "disarm", "taunt"}:
        vocab.add("control")
    if ops & {"knockback", "pull"}:
        vocab.add("knockback")
    if ops & {"shield", "brace"}:
        vocab.add("shield")
    if ops & {"empower_basic"}:
        vocab.add("basic_hit")
    return [key for key in CARD_VOCAB if key in vocab]


ACTIVE_CONTRACTS = {
    "active_elbow": ("Unit", {"damage", "knockback"}),
    "active_pants_down": ("Unit", {"damage", "root"}),
    "active_crotch_grab": ("Unit", {"damage", "stun"}),
    "active_account_banned": ("Unit", {"damage", "silence"}),
    "active_unplug": ("Unit", {"damage", "disarm"}),
    "active_snark_bolt": ("DirectionProjectile", {"damage", "slow"}),
    "active_seethe": ("Self", {"self_area", "damage", "burn"}),
    "active_quick_meeting": ("GroundPoint", {"zone", "zone_damage", "zone_slow", "zone_burn"}),
    "active_bail_out": ("DirectionDash", {"dash", "empower_basic"}),
    "active_apology_slide": ("DirectionDash", {"dash", "dash_contact", "damage", "slow"}),
    "active_not_mad": ("Self", {"shield", "dispel_slow", "dispel_root"}),
    "active_stare_down": ("Self", {"taunt", "timed_stat_bonus"}),
    "active_triple_combo": ("Self", {"empower_basic"}),
    "active_calculus_drop": ("GroundPoint", {"ground_impact", "damage"}),
    "active_flyer_trap": ("GroundPoint", {"contact_trap", "damage", "slow"}),
    "active_open_mic": ("DirectionCone", {"channel_cone", "damage", "knockback"}),
    "active_hose_down": ("DirectionLine", {"damage", "knockback", "wet"}),
    "active_zap": ("Unit", {"damage", "electrify", "chain_bounce"}),
    "active_charged_ram": ("DirectionDash", {"dash", "shield", "dash_contact", "damage", "electrify"}),
    "active_brace": ("Self", {"brace"}),
    "active_dirty_puddle": ("GroundPoint", {"zone", "zone_damage", "zone_wet", "zone_haste"}),
    "active_sock_shot": ("DirectionProjectile", {"damage", "attack_slow", "rot", "knockback"}),
    # Issue #287 P4 skills N1-N3 (spec §5).
    "active_cola_shake": ("DirectionCone", {"channel_cone", "damage", "chill", "knockback"}),
    "active_desk_fan": ("GroundPoint", {"fan"}),
    "active_stale_takeout": ("DirectionProjectile", {"damage", "rot", "ground_state", "knockback"}),
    "active_school_bag": ("GroundPoint", {"bag", "damage"}),
}
TIMED_STATS = {"active_stare_down": {"ArmorFlat", "MagicResistFlat"}}
# #198: the five point-bought stats and the bonuses one point of each adds (flat and additive percent terms).
STAT_CONTRACTS = {
    "stat_physique": {"MaxHPFlat", "HPRegenFlat"},
    "stat_thick_skin": {"ArmorFlat", "MagicResistFlat"},
    "stat_strength": {"ADFlat", "CritChanceBonusRatio"},
    "stat_brains": {"APFlat", "CooldownHasteFlat", "MomentumRegenFlat"},
    "stat_agility": {"AttackSpeedBonusRatio", "MoveSpeedBonusRatio"},
}
# The opening opportunity draws this many distinct actives from the opening pool.
OPENING_OFFER_SIZE = 3
# The eight universal final mods and the terms each one reads (App Spec §2.2, content §4.7).
FINAL_MOD_CONTRACTS = {
    "final_hit_and_run": {"sprays", "fade"},
    "final_clear_out": set(),
    "final_kill_seconds": {"guard"},
    "final_usurp": set(),
    "final_carry_flag": set(),
    "final_cage": {"duration", "hp"},
    "final_private_pole": {"spray", "guard", "radius"},
    "final_double_yellow": {"radius"},
}


def validate_definitions(data, ids):
    collections = {"character": [data["character"]], "abilities": data["abilities"], "stats": data["stats"],
                   "cards": data["cards"], "final_mods": data["final_mods"],
                   "monsters": data["camps"]["monsters"] + [data["boss"]]}
    all_ids = set()
    for section, rows in collections.items():
        index = rows_by_id(rows, section)
        require(set(index) <= set(ids[section]), "UnknownId", section, "unknown or retired content ID")
        require(not all_ids.intersection(index), "DuplicateId", section, "definition appears in another category")
        all_ids.update(index)
        for row in rows:
            row["native_id"] = ids[section][row["id"]]

    for ability in data["abilities"]:
        identity = ability["id"]
        require(identity in ACTIVE_CONTRACTS, "MissingHandler", identity, "no active definition projection")
        mode, effects = ACTIVE_CONTRACTS[identity]
        require(ability["target"]["mode"] == mode, "TargetEffectMismatch", identity, "wrong definition target")
        _validate_action(ability["target"], ability["effects"], identity)
        require(_effect_set(ability["effects"], identity) == effects, "MissingHandler", identity,
                "definition has missing or extra effects")
        if identity in TIMED_STATS:
            require(all(set(effect["stats"]) == TIMED_STATS[identity] for effect in ability["effects"]
                        if effect["op"] == "timed_stat_bonus"), "UnknownField", identity, "wrong timed stat")
        family = "cd_" + identity.removeprefix("active_")
        require(ability["cooldown_family"] == family, "InvalidReference", identity, "wrong cooldown family")
        require(not _effect_set(ability["effects"], identity) & (BRANCH_OPS | BRANCH_MOD_OPS), "BranchOnlyEffect", identity,
                "rank 1 carries no branch operation")
        require([branch["path"] for branch in ability["branches"]] == list(BRANCH_PATHS), "BranchSet", identity,
                "an active has exactly the six branch paths")
        for branch in ability["branches"]:
            branch_path = f"{identity}.branches.{branch['path']}"
            _validate_action(branch["target"], branch["effects"], branch_path)
    require(sum(1 for row in data["abilities"] if row["opening"]) >= OPENING_OFFER_SIZE, "OpeningPool", "abilities",
            "the opening opportunity needs three distinct actives in the opening pool")
    stats = rows_by_id(data["stats"], "stats")
    require(set(stats) == set(STAT_CONTRACTS), "StatSet", "stats", "exactly the five point-bought stats are required")
    for identity, stat in stats.items():
        require(set(stat["per_point"]) == STAT_CONTRACTS[identity], "UnknownField", identity,
                "stat definition does not declare these per-point bonuses")
        _positive_stats(stat["per_point"], identity)
    progression = data["progression"]
    levels = progression["active_offer_levels"]
    require(levels == sorted(set(levels)) and all(2 <= level <= progression["max_level"] for level in levels),
            "ActiveOfferLevels", "progression.active_offer_levels",
            "active offer levels must be distinct, ascending and reachable by levelling up")
    _validate_cards(data)
    for ability in data["abilities"]:
        ability["vocab"] = ability_vocab(ability)
        for branch in ability["branches"]:
            branch["vocab"] = ability_vocab(branch)
    finals = rows_by_id(data["final_mods"], "final_mods")
    require(set(finals) == set(FINAL_MOD_CONTRACTS), "FinalModSet", "final_mods",
            "exactly the eight implemented final mods are required")
    for identity, fields in FINAL_MOD_CONTRACTS.items():
        row = finals[identity]
        keys(row, {"id", "native_id", *fields}, identity)
        for field in fields:
            value = row[field] if field == "sprays" else quantity(row[field])
            require(value > 0, "FinalModDomain", identity + "." + field, "final mod terms must be positive")
    require(2 <= finals["final_hit_and_run"]["sprays"] <= 1000, "FinalModDomain", "final_hit_and_run.sprays",
            "a spray-count win needs 2 to 1000 sprays")
    require(quantity(finals["final_private_pole"]["spray"]) < quantity(finals["final_private_pole"]["guard"]),
            "FinalModDomain", "final_private_pole", "the private pole is guarded longer than it is sprayed")

    for row in (*data["abilities"], *data["cards"]):
        for effect in all_effects(row):
            effect["opcode"] = ids["effect_ops"][effect["op"]]

    display = rows_by_id(data["display"], "display")
    branch_display = {f"{row['id']}_{step.lower()}": (row, step) for row in data["abilities"] for step in RANK_STEPS}
    expected_display = all_ids | set(ids["statuses"]) | set(branch_display)
    require(set(display) == expected_display, "DisplayReference", "display", "missing or extra display definition")
    abilities = rows_by_id(data["abilities"], "abilities")
    cards = rows_by_id(data["cards"], "cards")
    names = set()
    for identity, row in display.items():
        icon = "icon_" + branch_display[identity][0]["id"] if identity in branch_display else "icon_" + identity
        require(row["name_key"] == identity + ".name" and row["description_key"] == identity + ".description"
                and row["icon_id"] == icon, "DisplayReference", identity, "non-canonical display reference")
        if identity == data["character"]["id"]:
            require(row.get("portrait_icon_id") == "portrait_" + identity,
                    "DisplayReference", identity, "missing character portrait")
        else:
            require("portrait_icon_id" not in row, "DisplayReference", identity, "portrait belongs only to character")
        for field in ("name_text", "description_text"):
            display_text(row[field], f"{identity}.{field}", field == "name_text")
        if identity in abilities:
            card_template(row["description_text"], abilities[identity]["effects"], f"{identity}.description_text")
        elif identity in branch_display:
            ability, step = branch_display[identity]
            variant = next(branch for branch in ability["branches"] if branch["path"] == STEP_PATH[step])
            card_template(row["description_text"], variant["effects"], f"{identity}.description_text")
        elif identity in cards:
            card_template(row["description_text"], cards[identity]["effects"], f"{identity}.description_text")
        elif identity in stats:
            stat_template(row["description_text"], stats[identity]["per_point"], f"{identity}.description_text")
        require(row["name_text"] not in names, "DisplayText", identity, "duplicate display name")
        names.add(row["name_text"])
    return collections


CARD_FIELDS = frozenset(("amount", "duration", "ratio", "count", "radius", "distance", "interval", "delay",
                         "secondary_ratio", "secondary_duration", "basic_count", "refund", "burst", "limit",
                         "multiplier", "angle", "cost_ratio", "extra_duration", "step", "threshold"))
CARD_PLACEHOLDER = re.compile(r"\[([a-z_]+)\.([A-Za-z_]+)\]")
# A stat card names one bonus of its single point: [per_point.<Stat>].
PER_POINT = "per_point"


def _template_brackets(text, path):
    require("[" not in CARD_PLACEHOLDER.sub("", text) and "]" not in CARD_PLACEHOLDER.sub("", text),
            "CardTemplate", path, "a bracket outside a [op.field] placeholder")


def card_template(text, effects, path):
    """#147/#198: [op.field] placeholders of an active's card text read one effect of its single value set."""
    _template_brackets(text, path)
    for op, field in CARD_PLACEHOLDER.findall(text):
        require(op in EFFECT_OPS and (field in CARD_FIELDS or field in STAT_FIELDS), "CardTemplate", path,
                f"unknown placeholder [{op}.{field}]")
        effect = next((effect for effect in effects if effect["op"] == op), None)
        require(effect is not None and (field in CARD_FIELDS or field in effect.get("stats", {})), "CardTemplate", path,
                f"[{op}.{field}] names no effect of this active")


def stat_template(text, per_point, path):
    """#198: [per_point.<Stat>] placeholders of a stat's card text read one bonus of a single point."""
    _template_brackets(text, path)
    for op, field in CARD_PLACEHOLDER.findall(text):
        require(op == PER_POINT and field in per_point, "CardTemplate", path,
                f"[{op}.{field}] names no per-point bonus of this stat")


def display_text(text, path, allow_digits):
    require(type(text) is str and text != "", "DisplayText", path, "display text is required")
    require(DISPLAY_TEXT_FORBIDDEN.search(text) is None, "DisplayText", path,
            "display text contains a forbidden character")
    require(text[0] not in DISPLAY_TEXT_EDGE and text[-1] not in DISPLAY_TEXT_EDGE, "DisplayText", path,
            "display text has leading or trailing space")
    require(allow_digits or DISPLAY_DESCRIPTION_DIGIT.search(text) is None, "DisplayText", path,
            "display description contains digits; numbers come from typed fields")


def all_effects(row):
    yield from row.get("effects", [])
    for branch in row.get("branches", []):
        yield from branch["effects"]


def _patch_effect(variant, op, path):
    found = [effect for effect in variant["effects"] if effect["op"] == op]
    require(len(found) == 1, "PatchTarget", path, f"patch names {op}, which the definition does not hold exactly once")
    return found[0]


def apply_patch(variant, patch, path):
    """One registered branch patch on an author-form definition (target, set, scale, add, remove, cooldown). Patch arithmetic
    is exact whatever the caller's decimal context: an inexact product or sum is refused, never rounded."""
    with localcontext(Context(prec=200, traps=[Inexact])):
        try:
            _apply_patch(variant, patch, path)
        except Inexact:
            raise ContentError("PrecisionLoss", path, "a branch patch would round an author value") from None


def _apply_patch(variant, patch, path):
    kind = patch["op"]
    if kind == "target":
        fields = {key: deepcopy(value) for key, value in patch.items() if key != "op"}
        if "mode" in fields:
            variant["target"] = fields
        else:
            require(set(fields) <= set(variant["target"]) - {"mode"}, "PatchTarget", path,
                    "a target patch changes fields the target form has")
            variant["target"].update(fields)
    elif kind == "set":
        effect = _patch_effect(variant, patch["effect"], path)
        require(patch["field"] != "op", "PatchTarget", path, "a patch cannot rename an operation")
        effect[patch["field"]] = deepcopy(patch["value"])
    elif kind == "scale":
        effect = _patch_effect(variant, patch["effect"], path)
        for field in patch["fields"]:
            value = effect.get(field)
            require(type(value) is dict and "value" in value, "PatchTarget", path, f"{patch['effect']}.{field} is no quantity")
            effect[field] = {"unit": value["unit"], "value": value["value"] * patch["ratio"]}
    elif kind == "add":
        require(not any(effect["op"] == patch["effect"]["op"] for effect in variant["effects"]), "DuplicateEffect", path,
                f"{patch['effect']['op']} is already there")
        variant["effects"].append(deepcopy(patch["effect"]))
    elif kind == "remove":
        variant["effects"].remove(_patch_effect(variant, patch["effect"], path))
    elif kind == "cooldown":
        variant["cooldown"] = {"unit": variant["cooldown"]["unit"], "value": variant["cooldown"]["value"] + patch["add"]["value"]}
    else:
        raise ContentError("UnknownPatch", path, f"unregistered patch {kind}")


# The delivery a branch's new target form carries when a rank step changes the form (the level-1 hit type is authored).
BRANCH_FORM_HIT_TYPES = {"Unit": "targeted", "DirectionProjectile": "projectile", "DirectionDash": "dash",
                         "GroundPoint": "area", "DirectionCone": "area", "DirectionLine": "area"}


def resolve_branches(ability):
    """The six branch variants of an author-form ability: each applies its rank steps' patches over the level-1 definition."""
    out = []
    for name, steps in BRANCH_PATHS.items():
        variant = {key: deepcopy(ability[key]) for key in ("target", "windup", "element", "hit_type", "momentum_cost", "cooldown",
                                                           "effects")}
        for step in steps:
            for index, patch in enumerate(ability["ranks"][step]["patches"]):
                apply_patch(variant, patch, f"{ability['id']}.ranks.{step}.patches[{index}]")
        mode = variant["target"]["mode"]
        if mode == "Self" and any(effect["op"] == "self_area" for effect in variant["effects"]):
            # A self-centred form that names an area hits that area, whether the step changed the form or only added it.
            variant["hit_type"] = "area"
        elif mode != ability["target"]["mode"]:
            variant["hit_type"] = "self" if mode == "Self" else BRANCH_FORM_HIT_TYPES[mode]
        out.append({"path": name, **variant})
    return out


def validate_numbers(data):
    params = data["parameters"]
    p = lambda key: quantity(params[key])
    level_steps = data["progression"]["max_level"] - 1
    for stat in ("MaxHP", "AD", "AP", "Armor", "MagicResist", "HPRegen"):
        key = "STAT." + stat
        minimum, maximum = p(key + ".Min"), p(key + ".Max")
        base, growth = p(key + ".Base"), p(key + ".PerLevel")
        require(minimum <= base <= base + level_steps * growth <= maximum,
                "StatBounds", key, "base/level growth is outside the declared projection bounds")
        if stat != "HPRegen":
            checked_add(params[key + ".Base"]["p2"], checked_mul(level_steps, params[key + ".PerLevel"]["p2"], key), key)
    require(p("STAT.MaxHP.Min") > 0 and p("STAT.MaxHP.Base") > 0,
            "NumericDomain", "STAT.MaxHP", "alive characters require positive HP")
    require(p("STAT.AD.Base") > 0, "NumericDomain", "STAT.AD.Base", "initial basic attack requires positive damage")
    require(p("STAT.CooldownHaste.Base") <= p("STAT.CooldownHaste.Max"), "StatBounds", "Haste", "inverted bounds")
    for base, prefix in (("STAT.BaseAttackSpeed", "STAT.AttackSpeed"),
                         ("UNIT.MoveSpeedCmPerSec", "STAT.MoveSpeed"),
                         ("UNIT.BasicAttackRangeCm", "STAT.AttackRange")):
        require(0 < p(prefix + ".Min") <= p(base) <= p(prefix + ".Max"),
                "StatBounds", prefix, "base must be within positive bounds")
    require(p("UNIT.CollisionRadiusCm") > 0 and p("UNIT.BasicAttackRangeCm") >= 2 * p("UNIT.CollisionRadiusCm"),
            "InvalidGeometry", "UNIT", "basic attack cannot require overlapping bodies")
    for key in ("VISION.RadiusCm", "CAPTURE.RadiusCm", "CAMP.ResetArrivalRadiusCm",
                "AIRDROP.PickupRadiusCm", "REST.ReturnSearchRadiusCm", "THROW.RadiusCm", "THROW.RangeCm",
                "SHOWDOWN.RingFinalRadiusCm",
                "OBJECTIVE.FlagRadiusCm", "OBJECTIVE.ArenaRadiusCm", "EXPLORE.LockerRadiusCm", "EXPLORE.PeddlerOffsetCm",
                "DAILY.TyphoonPushCm"):
        require(p(key) > 0, "InvalidGeometry", key, "radius must be positive")
    for name in ("ResistanceScale", "HasteScale"):
        require(quantity(data["formulas"][name]) > 0, "NumericDomain", name, "formula denominator scale must be positive")
    require(p("STAT.CritMultiplier") >= 1, "NumericDomain", "STAT.CritMultiplier", "critical multiplier must not reduce damage")
    require(quantity(data["character"]["basic_attack"]["ad_ratio"]) == 1,
            "NumericDomain", "basic_attack", "initial basic damage is exactly AD")
    dash = data["character"]["dash"]
    require(quantity(dash["distance"]) > 0, "InvalidGeometry", "character.dash", "dash distance must be positive")
    require(dash["duration"]["ms"] < dash["cooldown"]["ms"], "InvalidTiming", "character.dash",
            "dash must end before its cooldown does")
    interval = ceil_fraction(1000 / p("STAT.BaseAttackSpeed"), "basic attack interval")
    require(0 < params["UNIT.BasicAttackWindupSec"]["ms"] < interval,
            "InvalidTiming", "basic_attack", "windup must be shorter than attack interval")
    data["character"]["basic_attack"]["interval_ms"] = interval
    unlock, duration = p("SHOWDOWN.UnlockDelaySec"), p("SHOWDOWN.DurationSec")
    require(unlock + p("CAPTURE.SpraySec") + p("CAPTURE.GuardSec") <= duration
            and duration * 1000 <= 2147483647
            and 0 < p("SHOWDOWN.RespawnCutoffSec") < duration
            and unlock <= p("SHOWDOWN.ClosureStartsSec") < duration
            and params["SHOWDOWN.RespawnLimit"] <= 2,
            "InvalidTiming", "SHOWDOWN", "objective, respawn cutoff, closure and terminal clocks are inconsistent")
    for key in ("REGEN.OutOfCombatMaxHPRatioPerSec", "DORM.MaxHPRatioPerSec"):
        # A whole 0.001 share keeps every P2 maximum an exact 1e-6 P2 per millisecond rate.
        require(params[key]["bp"] > 0 and params[key]["bp"] % 10 == 0, "NumericDomain", key,
                "out-of-combat recovery must be a positive multiple of 0.001 of the maximum per second")
    require(p("AI.RetreatHPRatio") < p("AI.ResumeFarmHPRatio") <= p("AI.HighCampEntryHPRatio"),
            "InvalidPolicy", "AI", "retreat/resume/high-camp thresholds are inconsistent")
    require(p("AI.LowCampReserveHPRatio") <= p("AI.HighCampReserveHPRatio") < p("AI.HighCampEntryHPRatio"),
            "InvalidPolicy", "AI", "camp reserve thresholds are inconsistent")
    require(p("AI.RetryPolicy.InitialBackoffSec") <= p("AI.RetryPolicy.MaxBackoffSec"),
            "InvalidTiming", "AI.RetryPolicy", "backoff interval exceeds its cap")
    # D27 (#79): the leader circle always contains the leader and the minimum level exists.
    require(0 < p("LEADER.CircleMaxOffsetCm") <= p("LEADER.CircleRadiusCm")
            and params["LEADER.MinLevel"] <= data["progression"]["max_level"],
            "InvalidPolicy", "LEADER", "the circle must contain the leader and the level must exist")
    # #198 outer §5: the approved drops land at explicit field progress instants T (strictly increasing), each announced
    # AIRDROP.AnnounceLeadSec earlier and gone unclaimed AIRDROP.ExpireSec after landing, before the next announcement;
    # the last one is gone before the final field, which has no build reward target.
    timed_field = p("MATCH.FieldSegmentSec") * TIMED_FIELD_SEGMENTS
    lands = [quantity(value) for value in params["AIRDROP.LandSec"]]
    lead, expire = p("AIRDROP.AnnounceLeadSec"), p("AIRDROP.ExpireSec")
    require(lead <= lands[0] and all(land + expire + lead <= after for land, after in zip(lands, lands[1:]))
            and lands[-1] + expire <= timed_field,
            "InvalidTiming", "AIRDROP", "drops must be announced after T=0, leave before the next announcement and before the final field")
    # #198 outer §5 camp markers: one reveal per timed field segment, CAMPMARK.RevealSec after its start.
    require(p("CAMPMARK.RevealSec") < p("MATCH.FieldSegmentSec"), "InvalidTiming", "CAMPMARK.RevealSec",
            "a camp marker must be revealed inside its field segment")
    # #198 elements and reactions (CONTENT_AND_BALANCE §4.4).
    for key in ("COMBAT.FogRadiusCm", "COMBAT.ConductRadiusCm", "COMBAT.OverloadRadiusCm", "COMBAT.ShockSplashRadiusCm",
                "COMBAT.MethaneRadiusCm", "GROUND.PuddleRadiusCm", "GROUND.IceRadiusCm", "GROUND.FireRadiusCm",
                "GROUND.MiasmaRadiusCm", "GROUND.FieldRadiusCm"):
        require(p(key) > 0, "InvalidGeometry", key, "element and ground-state distances must be positive")
    for key in ("COMBAT.WetSlow", "COMBAT.ConductSlow", "COMBAT.ChillAttackSlow", "COMBAT.ChillMoveSlow", "COMBAT.RotHealCut",
                "COMBAT.FermentHealCut", "COMBAT.FrostbiteAttackSlow", "GROUND.IceAccelScale"):
        require(0 < p(key) < 1, "NumericDomain", key, "a slow or cut must be strictly between zero and one")
    require(p("GROUND.IceDisplacementScale") > 1, "NumericDomain", "GROUND.IceDisplacementScale", "ice only lengthens a move")
    for tick, life in (("COMBAT.RotTickSec", "COMBAT.RotSec"), ("GROUND.PuddleTickSec", "GROUND.PuddleSec"),
                       ("GROUND.FireTickSec", "GROUND.FireSec"), ("GROUND.MiasmaTickSec", "GROUND.MiasmaSec"),
                       ("GROUND.FieldTickSec", "GROUND.FieldSec")):
        require(p(tick) <= p(life), "InvalidTiming", tick, "a ticking element or ground state ticks inside its life")
    require(params["GROUND.MaxPerOwner"] <= params["GROUND.MaxTotal"] <= 40, "NumericDomain", "GROUND.MaxTotal",
            "the ground-state caps fit the observation bound of 40")
    require(p("COMBAT.FogIntervalSec") <= p("COMBAT.FogSec"), "InvalidTiming", "COMBAT.FogIntervalSec",
            "fog pulses must fall inside the fog")
    require(params["COMBAT.OverloadBreachStacks"] <= params["COMBAT.BreachMaxStacks"], "NumericDomain",
            "COMBAT.OverloadBreachStacks", "overload cannot apply more breach stacks than the breach cap")
    require(p("AIRDROP.HealMaxHPRatio") > 0 and p("AIRDROP.Momentum") > 0
            and p("AIRDROP.MoveSpeedRatio") > 0 and p("AIRDROP.MoveSpeedSec") > 0,
            "InvalidPolicy", "AIRDROP", "healing, momentum and move-speed rewards must all be positive")
    # #198: PK sign-up is the last part of every rest build window (R0 and R1-R3), not an extra wait.
    require(p("REST.SignupSec") <= p("REST.BuildSec") and p("REST.SignupSec") <= p("REST.OpeningBuildSec"),
            "InvalidTiming", "REST.SignupSec", "PK sign-up must fit inside every rest build window")
    # #198 PK ring: the shrink steps are ordered and end before the PK cap, and every ring lies strictly inside the last.
    require(0 < p("PK.RingShrinkStartSec") < p("PK.RingMidSec") < p("PK.RingFinalSec") < p("REST.PKMaxSec"),
            "InvalidTiming", "PK.RingFinalSec", "ring shrink steps must be ordered and finish before the PK cap")
    require(0 < p("PK.RingFinalRadiusCm") < p("PK.RingMidRadiusCm") < p("PK.ArenaRadiusCm"),
            "InvalidGeometry", "PK.RingFinalRadiusCm", "ring radii must shrink strictly inside the arena")
    for key in ("PK.RingDamageEarlyMaxHPRatioPerSec", "PK.RingDamageMidMaxHPRatioPerSec",
                "PK.RingDamageFinalMaxHPRatioPerSec", "THROW.SlowRatio"):
        require(params[key]["bp"] > 0, "NumericDomain", key, "ring damage and throw slow must be positive")
    # The final showdown ring shrinks linearly from SHOWDOWN.RingRadiusCm to SHOWDOWN.RingFinalRadiusCm and hurts outside.
    require(0 < p("SHOWDOWN.RingFinalRadiusCm") < p("SHOWDOWN.RingRadiusCm"), "InvalidGeometry", "SHOWDOWN.RingFinalRadiusCm",
            "the showdown ring must shrink")
    for key in ("SHOWDOWN.RingDamageEarlyMaxHPRatioPerSec", "SHOWDOWN.RingDamageLateMaxHPRatioPerSec"):
        require(params[key]["bp"] > 0, "NumericDomain", key, "ring damage must be positive")
    require(p("SHOWDOWN.RingDamageTickSec") < p("SHOWDOWN.DurationSec") - p("SHOWDOWN.ClosureStartsSec"), "InvalidTiming",
            "SHOWDOWN.RingDamageTickSec", "the ring must tick inside the closure")
    _validate_pacing(data, timed_field)

    _validate_momentum(data)

    thresholds = [0]
    for level in range(1, data["progression"]["max_level"]):
        increment = checked_add(params["XP.BaseToNext"],
                                checked_mul(level - 1, params["XP.IncreasePerLevel"], "XP"), "XP")
        thresholds.append(checked_add(thresholds[-1], increment, "XP"))
    require(data["progression"]["xp_thresholds"] == thresholds,
            "XPThresholds", "progression", "thresholds do not match the explicit linear XP parameters")
    data["progression"]["xp_to_next"] = [b - a for a, b in zip(thresholds, thresholds[1:])]
    checked_add(params["REWARD.PlayerXP.Base"], checked_mul(level_steps, params["REWARD.PlayerXP.PerLevel"], "REWARD.PlayerXP"),
                "REWARD.PlayerXP")
    # #198 points: the opening grant and one grant per level-up to the cap stay inside int64; past the cap every full
    # GROWTH.MaxLevelPointsXP beyond the top threshold grants GROWTH.MaxLevelPoints.
    checked_add(params["GROWTH.OpeningPoints"], checked_mul(level_steps, params["GROWTH.PointsPerLevel"], "GROWTH.PointsPerLevel"),
                "GROWTH.OpeningPoints")
    # Spec §3.12: capacity grows one slot every CARDS.LevelsPerSlot levels; an offer shows CARDS.OfferSize distinct cards.
    require(params["CARDS.OfferSize"] <= len(data["cards"]), "CardPool", "CARDS.OfferSize", "an offer needs distinct cards")
    require(params["CARDS.CapacityBase"] + (data["progression"]["max_level"] - 1) // params["CARDS.LevelsPerSlot"] <= 32,
            "CardCapacity", "CARDS.CapacityBase", "the workshop capacity stays within the build wire")
    require(quantity(params["DAMAGE.FactorTechCap"]) > 1, "NumericDomain", "DAMAGE.FactorTechCap",
            "the technical cap sits above one")
    # Spec §3.13: the rest shop stocks SHOP.SkillSlots skills and SHOP.CardSlots cards (SHOP.VocabCardSlots of them by the
    # SHOP.VocabWeight weighting); the shelf travels as one fixed array.
    require(params["SHOP.VocabCardSlots"] <= params["SHOP.CardSlots"], "ShopShelf", "SHOP.VocabCardSlots",
            "the weighted card slots are some of the card slots")
    require(params["SHOP.CardSlots"] + params["SHOP.SkillSlots"] <= 8, "ShopShelf", "SHOP.CardSlots",
            "the shelf stays within the owner view")
    require(params["SHOP.CardSlots"] <= len(data["cards"]), "ShopShelf", "SHOP.CardSlots", "a shelf shows distinct cards")

    monsters = rows_by_id(data["camps"]["monsters"], "monsters")
    require({monster["tier"] for monster in monsters.values()} == CAMP_TIERS and all(sum(m["tier"] == tier for m in monsters.values()) == 3 for tier in CAMP_TIERS),
            "CampTiers", "monsters", "exactly three species per camp tier (Low, High, Elite, Ancient) are required")
    # #97 camp growth: a camp life takes stage min(MaxStage, floor(spawn time / StageSec)) at spawn and keeps it; the
    # native stage is one byte. Scaled values stay integral in their stored units (P2, bp) at the cap.
    max_stage = params["CAMP.GrowthMaxStage"]
    require(params["CAMP.GrowthStageSec"]["ms"] > 0 and 1 <= max_stage <= 255, "NumericDomain", "CAMP.Growth",
            "camp growth needs a positive stage length and 1..255 stages")
    for key in ("CAMP.GrowthHPRatioPerStage", "CAMP.GrowthAttackRatioPerStage", "CAMP.GrowthRewardXPRatioPerStage"):
        checked_add(10000, checked_mul(params[key]["bp"], max_stage, key), key)
    checked_mul(params["CAMP.GrowthResistPerStage"]["p2"], max_stage, "CAMP.GrowthResistPerStage")
    for monster in monsters.values():
        path = monster["id"]
        require(quantity(monster["hp"]) > 0 and quantity(monster["attack"]) > 0,
                "NumericDomain", path, "monster HP/attack must be positive")
        require(quantity(monster["collision_radius"]) > 0
                and quantity(monster["attack_range"]) >= quantity(monster["collision_radius"]) + p("UNIT.CollisionRadiusCm")
                and quantity(monster["leash_radius"]) > quantity(monster["attack_range"]),
                "InvalidGeometry", path, "invalid collision/attack/leash geometry")
        # A camp member perceives out to its leash, and TraceSight, built at VISION.RadiusCm, supplies its candidates; a
        # wider perception faults the camp decision at runtime (#97), so the data is refused here instead.
        require(quantity(monster["leash_radius"]) <= p("VISION.RadiusCm"), "InvalidGeometry", path,
                "camp perception (leash radius) exceeds VISION.RadiusCm, the sight range that supplies its candidates")
        interval = ceil_fraction(1000 / quantity(monster["attack_speed"]), path)
        require(monster["attack_windup"]["ms"] < interval, "InvalidTiming", path, "windup exceeds attack interval")
        for field in ("hp_regen", "ap", "crit_chance"):
            require(quantity(monster[field]) == 0, "UnsupportedMonster", path,
                    f"{field} requires a different monster handler contract")
        checked_mul(monster["count_per_camp"], monster["reward_xp"], path)
        monster["attack_interval_ms"] = interval
        monster["perception_radius"] = dict(monster["leash_radius"])
    for tier in CAMP_TIERS:
        pool = [m for m in monsters.values() if m["tier"] == tier]
        for field in ("count_per_camp", "reward_xp", "respawn"):
            require(all(m[field] == pool[0][field] for m in pool), "CampPool", tier,
                    f"same-tier species must share {field}")
        for m in pool:
            s = m["skill"]
            display_text(s["name"],m["id"]+".skill.name",False)
            require(1 <= s["every"] <= 100 and 1 <= s["pulses"] <= 3 and s["angle"] <= 180
                    and s["pulse_angle"] <= 120 and 0 < quantity(s["range"]) <= quantity(m["leash_radius"])
                    and quantity(s["ratio"]) * s["pulses"] <= Fraction(9, 4)
                    and s["evolve_stage"] <= 255 and s["echo_stage"] <= 255,
                    "MonsterSkill", m["id"], "invalid skill cadence, shape or total damage budget")
    # D26 (#79): the 画饼老板 Boss is one roaming body: its own block, not a camp tier.
    boss = data["boss"]
    require(boss["id"] not in monsters, "DuplicateId", "boss", "the Boss is not a camp monster")
    require(quantity(boss["hp"]) > 0 and quantity(boss["attack"]) > 0, "NumericDomain", "boss", "Boss HP/attack must be positive")
    require(quantity(boss["collision_radius"]) == p("UNIT.CollisionRadiusCm")
            and quantity(boss["attack_range"]) >= quantity(boss["collision_radius"]) + p("UNIT.CollisionRadiusCm")
            and quantity(boss["aggro_radius"]) > quantity(boss["attack_range"]),
            "InvalidGeometry", "boss", "invalid Boss collision/attack/aggro geometry")
    require(boss["attack_windup"]["ms"] < boss["attack_interval"]["ms"], "InvalidTiming", "boss", "windup exceeds attack interval")
    require(p("STAT.MoveSpeed.Min") <= quantity(boss["move_speed"]) <= p("STAT.MoveSpeed.Max"),
            "NumericDomain", "boss", "Boss move speed outside the move speed clamp")
    # #198 outer §5: the Boss appears once at field T=appear_at and leaves unkilled at T=leave_at, never in the final field.
    require(quantity(boss["appear_at"]) < quantity(boss["leave_at"]) <= timed_field, "InvalidTiming", "boss",
            "the Boss must appear before it leaves, and leave before the final field")
    placements = rows_by_id(data["camps"]["placements"], "camps", "anchor_id")
    require(all(row["monster_id"] in monsters for row in placements.values()),
            "InvalidReference", "camps", "unknown monster reference")
    require({monsters[row["monster_id"]]["tier"] for row in placements.values()} == CAMP_TIERS,
            "CampTiers", "camps", "every camp tier must be placed")
    # Camp markers never pick the previous segment's camp, so each marked tier needs at least two camps.
    for tier in ("High", "Elite"):
        require(sum(monsters[row["monster_id"]]["tier"] == tier for row in placements.values()) >= 2,
                "CampTiers", "camps", f"camp markers need at least two {tier} camps")
    _validate_build_envelope(data)


# Spec §10/§11 (P6): mid-field objectives, respawn, daily modifiers, camp affixes and hidden things. Every timed rule must fit
# its field segment; offer sizes fit the five-card choice row.
MAX_OFFER_CARDS = 5
DAILY_MODIFIERS = 12


def _validate_pacing(data, timed_field):
    params = data["parameters"]
    p = lambda key: quantity(params[key])
    segment = p("MATCH.FieldSegmentSec")
    start, lead = p("OBJECTIVE.StartSec"), p("OBJECTIVE.AnnounceLeadSec")
    require(lead <= start and start + p("OBJECTIVE.FlagWindowSec") <= segment and start + p("OBJECTIVE.ArenaSec") <= segment,
            "InvalidTiming", "OBJECTIVE", "an objective is announced inside its segment and ends before the segment does")
    require(p("OBJECTIVE.FlagSpraySec") < p("OBJECTIVE.FlagWindowSec"), "InvalidTiming", "OBJECTIVE.FlagSpraySec",
            "a mini flag must be sprayable inside its window")
    for key in ("OBJECTIVE.FlagCardOffer", "OBJECTIVE.ArenaCardOffer", "AFFIX.FridgeCardOffer", "AFFIX.BonusCardOffer",
                "EXPLORE.LockerCardOffer", "EXPLORE.PeddlerCardOffer"):
        require(params["CARDS.OfferSize"] <= params[key] <= min(MAX_OFFER_CARDS, len(data["cards"])), "CardPool", key,
                "an extra offer shows at least the normal offer and at most five distinct cards")
    require(params["LIFE.SpawnChoiceCount"] <= 4, "InvalidPolicy", "LIFE.SpawnChoiceCount", "the death screen shows at most four spots")
    require(params["DAILY.Count"] <= DAILY_MODIFIERS, "InvalidPolicy", "DAILY.Count", "the draw picks distinct modifiers")
    require(params["DAILY.RumorLeaders"] == 2, "InvalidPolicy", "DAILY.RumorLeaders", "the leader view carries two trending contenders")
    require(params["DAILY.RainPuddles"] <= params["GROUND.MaxTotal"], "InvalidPolicy", "DAILY.RainPuddles",
            "the rain fits the ground-state cap")
    require(p("DAILY.RainPuddleSec") <= p("DAILY.RainIntervalSec"), "InvalidTiming", "DAILY.RainPuddleSec",
            "rain puddles are gone before the next shower")
    require(p("DAILY.HeatBurnTickScale") > 1 and p("DAILY.BlackoutShockScale") > 1 and p("DAILY.ExamMonsterXPScale") > 1,
            "NumericDomain", "DAILY", "a modifier scale strengthens its rule")
    for key in ("DAILY.BlackoutVisionCut", "DAILY.SalePriceScale", "DAILY.GymDashCooldownCut", "AFFIX.AuditTelegraphCut"):
        require(0 < p(key) < 1, "NumericDomain", key, "a cut stays strictly between zero and one")
    lands = [quantity(value) for value in params["DAILY.DeliveryLandSec"]]
    lead, expire = p("AIRDROP.AnnounceLeadSec"), p("DAILY.DeliveryExpireSec")
    require(lead <= lands[0] and all(land + expire + lead <= after for land, after in zip(lands, lands[1:]))
            and lands[-1] + expire <= timed_field,
            "InvalidTiming", "DAILY.DeliveryLandSec", "delivery-day drops follow the airdrop spacing and leave before T=480")
    require(p("DAILY.PatrolBossAppearSec") < quantity(data["boss"]["leave_at"]), "InvalidTiming", "DAILY.PatrolBossAppearSec",
            "the early Boss still appears before it leaves")
    require(p("AFFIX.TeambuildXPScale") >= 1, "NumericDomain", "AFFIX", "an affix reward never shrinks the base reward")
    require(0 < p("EXPLORE.PeddlerEarliestSec") <= p("EXPLORE.PeddlerLatestSec")
            and p("EXPLORE.PeddlerLatestSec") + p("EXPLORE.PeddlerStaySec") <= timed_field,
            "InvalidTiming", "EXPLORE.Peddler", "the dealer appears and leaves inside the timed field")
    require(p("STAT.MoveSpeed.Min") <= p("EXPLORE.PeddlerMoveSpeed") <= p("STAT.MoveSpeed.Max"), "NumericDomain",
            "EXPLORE.PeddlerMoveSpeed", "the dealer moves inside the move speed clamp")
    require(p("EXPLORE.LockerChannelSec") < p("EXPLORE.LockerStaySec"), "InvalidTiming", "EXPLORE.LockerChannelSec",
            "a locker can be opened while it is out")
    require(0 < p("EXPLORE.LockerEarliestSec") <= p("EXPLORE.LockerLatestSec")
            and p("EXPLORE.LockerLatestSec") + p("EXPLORE.LockerStaySec") <= timed_field,
            "InvalidTiming", "EXPLORE.Locker", "every locker comes out and leaves inside the timed field")


def _validate_momentum(data):
    """Momentum, combo, overdrive and skill ranks (CONTENT_AND_BALANCE §3.3-§3.6)."""
    params = data["parameters"]
    p = lambda key: quantity(params[key])
    top = p("MOMENTUM.Max")
    require(0 < p("MOMENTUM.Start") <= top and 0 < p("OVERDRIVE.Threshold") <= top, "NumericDomain", "MOMENTUM",
            "the start and the overdrive threshold must lie in (0, max]")
    for key in ("MOMENTUM.CombatPerSec", "MOMENTUM.IdleReturnPerSec", "MOMENTUM.BasicPerRefInterval", "MOMENTUM.SkillHit",
                "MOMENTUM.Reaction", "MOMENTUM.PerfectDodge", "AIRDROP.Momentum"):
        require(0 < p(key) <= top, "NumericDomain", key, "a momentum income must be positive and fit the bar")
    require(params["MOMENTUM.BasicRefIntervalSec"]["ms"] > 0, "NumericDomain", "MOMENTUM.BasicRefIntervalSec",
            "the reference interval must be positive")
    ratios = [quantity(value) for value in params["COMBO.StepRatios"]]
    require(all(ratio > 0 for ratio in ratios), "NumericDomain", "COMBO.StepRatios", "every combo step deals damage")
    require(p("OVERDRIVE.Scale") >= 1 and p("OVERDRIVE.RotDurationScale") >= 1, "NumericDomain", "OVERDRIVE",
            "overdrive never weakens a release")
    require(0 < p("OVERDRIVE.WaterSlow") < 1, "NumericDomain", "OVERDRIVE.WaterSlow", "a slow must keep some move speed")
    base_scales = [quantity(value) for value in params["SKILL.RankBaseScale"]]
    ratio_scales = [quantity(value) for value in params["SKILL.RankRatioScale"]]
    require(base_scales[0] == 1 and ratio_scales[0] == 1 and base_scales == sorted(base_scales)
            and ratio_scales == sorted(ratio_scales), "NumericDomain", "SKILL.Rank",
            "rank 1 is the authored value set and higher ranks never weaken it")
    levels = params["SKILL.PointLevels"]
    require(levels == sorted(set(levels)) and all(2 <= level <= data["progression"]["max_level"] for level in levels),
            "NumericDomain", "SKILL.PointLevels", "skill point levels are distinct, ascending and reachable")
    require(len(levels) <= 2 * data["progression"]["active_slots"], "NumericDomain", "SKILL.PointLevels",
            "no more skill points than ranks to buy")
    require(params["SKILL.CooldownFloorSec"]["ms"] > 0, "NumericDomain", "SKILL.CooldownFloorSec", "the floor must be positive")


def _validate_build_envelope(data):
    # Every attribute clamps to its catalog bounds in the projection (#112 §4), so stat points are never refused;
    # what remains is that every damage or shield term stays inside int64 at the extreme attributes.
    params = data["parameters"]
    max_ad = params["STAT.AD.Max"]["p2"]
    max_ap = params["STAT.AP.Max"]["p2"]
    max_hp = params["STAT.MaxHP.Max"]["p2"]
    # G(L) = 1 + SKILL.GrowthPerLevel x (L - 1); the top rank and overdrive scale the grown base amounts again.
    top_level = data["progression"]["max_level"]
    factor = checked_add(10000, checked_mul(top_level - 1, params["SKILL.GrowthPerLevel"]["bp"], "SKILL.GrowthPerLevel"),
                         "SKILL.GrowthPerLevel")
    rank_factor = checked_mul(params["SKILL.RankBaseScale"][-1]["bp"], params["OVERDRIVE.Scale"]["bp"], "SKILL.RankBaseScale")
    grow = lambda amount, path: checked_mul(checked_mul(amount, factor, path) // 10000, rank_factor, path) // 100000000
    checked_add(grow(params["COMBAT.BurnPerSec"]["p2"], "COMBAT.BurnPerSec"),
                checked_mul(max_ap, params["COMBAT.BurnAPRatioPerSec"]["bp"], "COMBAT.BurnAPRatioPerSec") // 10000, "COMBAT.BurnPerSec")
    # #198 reaction R = Base + (L - 1) x PerLevel + APRatio x AP; it never grows with G.
    reaction = checked_add(params["COMBAT.ReactionBase"]["p2"],
                           checked_mul(top_level - 1, params["COMBAT.ReactionPerLevel"]["p2"], "COMBAT.ReactionPerLevel"),
                           "COMBAT.ReactionBase")
    checked_add(reaction, checked_mul(max_ap, params["COMBAT.ReactionAPRatio"]["bp"], "COMBAT.ReactionAPRatio") // 10000,
                "COMBAT.ReactionAPRatio")
    for row in (*data["abilities"], *data["cards"]):
        for effect in all_effects(row):
            base = effect["base"]["p2"] if "base" in effect else 0
            base = grow(base, row["id"])
            if effect["op"] == "timed_stat_bonus":
                for stat in effect.get("stats", {}).values():
                    for storage in ("p2", "bp"):
                        if storage in stat:
                            grow(stat[storage], row["id"])
            if effect["op"] in ("damage", "shield", "empower_basic", "zone_damage"):
                numerator = checked_mul(base, 10000, row["id"])
                numerator = checked_add(numerator, checked_mul(max_ap, effect["ap_ratio"]["bp"], row["id"]), row["id"])
                if effect["op"] != "shield":
                    numerator = checked_add(numerator, checked_mul(max_ad, effect["ad_ratio"]["bp"], row["id"]), row["id"])
                if effect["op"] == "damage":
                    checked_add(numerator, checked_mul(max_hp, effect["max_hp_ratio"]["bp"], row["id"]), row["id"])
            if effect["op"] == "brace":
                # The release is the grown base plus its share of the grown record cap.
                limit = grow(effect["limit"]["p2"], row["id"])
                checked_add(checked_mul(base, 10000, row["id"]),
                            checked_mul(limit, effect["record_ratio"]["bp"], row["id"]), row["id"])


def _wire_fraction(value, path):
    keys(value, {"numerator", "denominator"}, path)
    numerator = integer(value["numerator"], path, INT64_MIN)
    denominator = integer(value["denominator"], path, 1)
    result = Fraction(numerator, denominator)
    require(result.numerator == numerator and result.denominator == denominator,
            "WireShape", path, "rational pair must be reduced with positive denominator")
    return result


def _vector(value, path):
    keys(value, {"x", "y", "z"}, path)
    return tuple(_wire_fraction(value[axis], f"{path}.{axis}") for axis in ("x", "y", "z"))


def _world_box(row, path):
    minimum, maximum = _vector(row["min_cm"], path), _vector(row["max_cm"], path)
    require(all(a <= b for a, b in zip(minimum, maximum)) and minimum[0] < maximum[0] and minimum[1] < maximum[1],
            "InvalidGeometry", path, "invalid world bounds")
    return minimum, maximum


def validate_world(raw):
    from DemoWorld import validate_payload

    return validate_payload(raw)


def parse_world_contract(raw, world_raw):
    from DemoWorld import SCHEMA_VERSION as world_schema, PROJECTION_REVISION as world_projection

    try:
        text = raw.decode("ascii")
    except UnicodeDecodeError as error:
        raise ContentError("WorldContract", WORLD_CONTRACT, "expected ASCII generated contract") from error
    pattern = (r'#pragma once\r?\n'
               r'#define LD_DEMO_WORLD_SCHEMA_VERSION ([1-9][0-9]*)\r?\n'
               r'#define LD_DEMO_WORLD_PROJECTION_REVISION ([1-9][0-9]*)\r?\n'
               r'#define LD_DEMO_WORLD_PAYLOAD_SHA256 TEXT\("([0-9a-f]{64})"\)\r?\n')
    match = re.fullmatch(pattern, text)
    require(match is not None, "WorldContract", WORLD_CONTRACT, "missing, extra or malformed contract fields")
    schema, projection, digest = match.groups()
    require(int(schema) == world_schema and int(projection) == world_projection,
            "WorldContract", WORLD_CONTRACT, "unsupported world revision")
    require(digest == sha256(world_raw), "WorldIntegrity", WORLD_OUTPUT, "actual payload bytes do not match world contract")
    return {"schema_version": int(schema), "projection_revision": int(projection), "sha256": sha256(raw)}


def validate_world_references(data, world, anchors):
    refs = data["world"]
    require(refs["map_id"] == world["map_id"], "InvalidReference", "world.map_id", "different world identity")
    expected = {"Spawn": refs["spawn_ids"], "Camp": refs["camp_ids"], "Capture": [refs["capture_id"]],
                "PKSpawn": refs["pk_spawn_ids"], "PKThrow": [refs["pk_throw_id"]]}
    for kind, identities in expected.items():
        require(len(set(identities)) == len(identities), "DuplicateId", kind, "repeated anchor reference")
        require(set(identities) == {identity for identity, row in anchors.items() if row["kind"] == kind},
                "InvalidReference", kind, "missing, extra or wrong-kind world anchor")
    # #198: the PK ring is read in index order, so the content lists it exactly as pk_spawn_0..5.
    require(list(refs["pk_spawn_ids"]) == [f"pk_spawn_{n}" for n in range(6)],
            "InvalidReference", "world.pk_spawn_ids", "PK spawns must be listed as pk_spawn_0 to pk_spawn_5 in order")
    # Spec §10.4/§11.3: spawn choice draws distinct spawn anchors; lockers draw distinct locker anchors.
    require(data["parameters"]["LIFE.SpawnChoiceCount"] <= len(refs["spawn_ids"]), "InvalidReference", "LIFE.SpawnChoiceCount",
            "spawn choice needs that many distinct spawn anchors")
    lockers = sum(row["kind"] == "Locker" for row in anchors.values())
    require(data["parameters"]["EXPLORE.LockerCount"] <= lockers, "InvalidReference", "EXPLORE.LockerCount",
            "stocked lockers need that many locker anchors")
    placements = rows_by_id(data["camps"]["placements"], "camps", "anchor_id")
    require(set(placements) == set(refs["camp_ids"]), "InvalidReference", "camps", "camp placement set differs from world")
    monsters = rows_by_id(data["camps"]["monsters"], "monsters")
    for identity, row in placements.items():
        require(len(anchors[identity]["members"]) == monsters[row["monster_id"]]["count_per_camp"],
                "WorldAnchors", identity, "member-home count differs from content")


def definition_to_wire(identity):
    definition_id(identity, "DefinitionId")
    return identity.encode("ascii").ljust(64, b"\0")


def definition_from_wire(raw):
    require(type(raw) is bytes and len(raw) == 64 and b"\0" in raw,
            "WireShape", "DefinitionId", "expected 64 bytes with a terminator")
    head, _, padding = raw.partition(b"\0")
    require(not any(padding), "WireShape", "DefinitionId", "nonzero padding")
    try:
        identity = head.decode("ascii")
    except UnicodeDecodeError as error:
        raise ContentError("WireShape", "DefinitionId", "non-ASCII definition") from error
    return definition_id(identity, "DefinitionId")


def digest_to_wire(digest):
    require(type(digest) is str and HEX_PATTERN.fullmatch(digest) is not None,
            "WireShape", "CatalogDigest", "expected 64 lowercase hexadecimal characters")
    return bytes.fromhex(digest)


def digest_from_wire(raw):
    require(type(raw) is bytes and len(raw) == 32, "WireShape", "CatalogDigest", "expected 32 raw bytes")
    return raw.hex()


@dataclass(frozen=True)
class Catalog:
    data: Mapping[str, object]
    definitions: Mapping[str, Mapping[str, object]]
    field_projection: Mapping[str, tuple[str, ...]]

    def definition(self, identity):
        definition_id(identity, "DefinitionId")
        require(identity in self.definitions, "UnknownId", identity, "definition is not enabled in this catalog")
        return self.definitions[identity]

    def parameter(self, identity):
        require(identity in self.data["parameters"], "UnknownParameter", identity, "parameter is not declared")
        return self.data["parameters"][identity]


@dataclass(frozen=True)
class Compilation:
    catalog: Catalog
    outputs: Mapping[Path, bytes]
    inputs: Mapping[Path, bytes]
    digest: bytes
    bundle_kind: str = "content"


@dataclass(frozen=True)
class AuthorCompilation:
    catalog: Catalog
    inputs: Mapping[Path, bytes]


def content_contract_bytes(payload_digest, catalog_digest):
    raw_digest = digest_to_wire(catalog_digest)
    return (
        "#pragma once\n"
        f"#define LD_DEMO_CONTENT_SCHEMA_VERSION {SCHEMA_VERSION}\n"
        f"#define LD_DEMO_CONTENT_HANDLER_REVISION {HANDLER_REVISION}\n"
        f"#define LD_DEMO_CONTENT_PROJECTION_REVISION {PROJECTION_REVISION}\n"
        f'#define LD_DEMO_CONTENT_PAYLOAD_SHA256 TEXT("{payload_digest}")\n'
        f'#define LD_DEMO_CATALOG_SHA256 TEXT("{catalog_digest}")\n'
        "#define LD_DEMO_CATALOG_DIGEST_BYTES {" + ", ".join(f"0x{byte:02x}" for byte in raw_digest) + "}\n"
    ).encode("ascii")


def _sort_definitions(data, fields):
    prefixes = {}
    arrays = [(f"content.{key}", data[key], "id") for key in ("abilities", "stats", "final_mods", "display")]
    arrays.extend((("content.camps.monsters", data["camps"]["monsters"], "id"),
                   ("content.camps.placements", data["camps"]["placements"], "anchor_id"),
                   ("content.world.spawn_ids", data["world"]["spawn_ids"], None),
                   ("content.world.camp_ids", data["world"]["camp_ids"], None)))
    for path, rows, key in arrays:
        identity_for = (lambda row: row[key]) if key is not None else (lambda row: row)
        order = [identity_for(row) for row in rows]
        rows.sort(key=identity_for)
        indices = {identity_for(row): index for index, row in enumerate(rows)}
        for index, identity in enumerate(order):
            prefixes[f"{path}[{index}]"] = f"{path}[{indices[identity]}]"
    for source, destinations in fields.items():
        mapped = []
        for destination in destinations:
            for old, new in prefixes.items():
                if destination == old or destination.startswith(old + "."):
                    destination = new + destination[len(old):]
                    break
            mapped.append(destination)
        fields[source] = tuple(mapped)


def project_author(schema, author, fields=None):
    """The typed projection of a validated author document: each ability's rank steps resolve into its six branch variants
    (projected like an ability's own values) and the steps' names and one-line texts become display rows."""
    fields = {} if fields is None else fields
    # Branch values are compiled from the rank steps, not copied from one author leaf, so they stay out of the leaf map.
    derived = {}
    projected_author = deepcopy(author)
    rank_display = []
    branches = []
    for index, ability in enumerate(projected_author["abilities"]):
        resolved = resolve_branches(ability)
        for branch in resolved:
            schema.validate(branch, schema.schema["$defs"]["branch"], f"content.abilities[{index}].branches.{branch['path']}")
        branches.append([schema.project(branch, schema.schema["$defs"]["branch"],
                                        f"content.abilities[{index}].branches[{position}]", derived)
                         for position, branch in enumerate(resolved)])
        for step in RANK_STEPS:
            identity = f"{ability['id']}_{step.lower()}"
            rank_display.append({"id": identity, "namespace": "DemoContent", "name_key": identity + ".name",
                                 "name_text": ability["ranks"][step]["name"], "description_key": identity + ".description",
                                 "description_text": ability["ranks"][step]["text"], "icon_id": "icon_" + ability["id"]})
        del ability["ranks"]
    data = schema.project(projected_author, fields=fields)
    for ability, resolved in zip(data["abilities"], branches):
        ability["branches"] = resolved
    data["display"].extend(rank_display)
    return data


def compile_author(root=ROOT):
    root = Path(root)
    inputs = {relative: read_bytes(root, relative) for relative in AUTHOR_INPUT_PATHS}
    author = parse_json(inputs[CONTENT_SOURCE], CONTENT_SOURCE)
    require(type(author) is dict and type(author.get("identity")) is dict
            and type(author["identity"].get("approval")) is dict
            and author["identity"]["approval"].get("status") == "approved",
            "ApprovalRequired", CONTENT_SOURCE, "production content requires an explicit reviewed decision")
    schema = AuthorSchema(parse_json(inputs[SCHEMA_SOURCE], SCHEMA_SOURCE))
    schema.validate(author)
    ids = parse_json(inputs[IDS_SOURCE], IDS_SOURCE)
    validate_native_ids(root, ids)
    fields = {}
    data = project_author(schema, author, fields)
    require(data["identity"]["schema_version"] == SCHEMA_VERSION
            and data["identity"]["handler_revision"] == HANDLER_REVISION
            and data["identity"]["projection_revision"] == PROJECTION_REVISION,
            "ContractRevision", CONTENT_SOURCE, "compiler and author revisions differ")
    collections = validate_definitions(data, ids)
    validate_numbers(data)
    _sort_definitions(data, fields)
    validate_integer_tree(data, "compiled content")
    data = canonical(data)
    definitions = {row["id"]: freeze(row) for rows in collections.values() for row in rows}
    catalog = Catalog(freeze(data), MappingProxyType(definitions), MappingProxyType(fields))
    for relative, raw in inputs.items():
        require(read_bytes(root, relative) == raw, "InputsChanged", relative, "author input changed during validation")
    return AuthorCompilation(catalog, MappingProxyType(inputs))


def compile_content(root=ROOT):
    from DemoWorld import compile_world

    root = Path(root)
    author = compile_author(root)
    generated_world = compile_world(root, author)
    inputs = dict(generated_world.inputs)
    for relative, expected in generated_world.outputs.items():
        raw = read_bytes(root, relative)
        inputs[relative] = raw
    world, anchors = validate_world(inputs[WORLD_OUTPUT])
    world_contract = parse_world_contract(inputs[WORLD_CONTRACT], inputs[WORLD_OUTPUT])
    validate_world_references(author.catalog.data, world, anchors)
    for relative, expected in generated_world.outputs.items():
        require(inputs[relative] == expected, "StaleWorldOutput", relative,
                "world bytes differ from the sole approved author/schema producer; run SyncDemoWorldData.py")
    data = canonical(author.catalog.data)
    content_raw = encode(data)
    manifest = canonical({
        "format": "lockdown-demo-content",
        "schema_version": SCHEMA_VERSION,
        "handler_revision": HANDLER_REVISION,
        "projection_revision": PROJECTION_REVISION,
        "author_schema_sha256": sha256(inputs[SCHEMA_SOURCE]),
        "native_ids_sha256": sha256(inputs[IDS_SOURCE]),
        "native_contract_sha256": sha256(inputs[NATIVE_IDS]),
        "display_sha256": sha256(encode(data["display"])),
        "content": {"file": "demo_content.json", "sha256": sha256(content_raw)},
        "world": {"file": "world.json", "sha256": sha256(inputs[WORLD_OUTPUT]), "contract": world_contract},
    })
    manifest_raw = encode(manifest)
    digest = sha256(manifest_raw)
    outputs = {
        CONTENT_OUTPUT: content_raw,
        CONTENT_CONTRACT: content_contract_bytes(sha256(content_raw), digest),
        MANIFEST_OUTPUT: manifest_raw,
    }
    for relative, raw in inputs.items():
        require(read_bytes(root, relative) == raw, "InputsChanged", relative,
                "input changed while its projection was being validated")
    return Compilation(author.catalog, MappingProxyType(outputs), MappingProxyType(inputs), digest_to_wire(digest))


def build_outputs(root=ROOT):
    return compile_content(root).outputs


def check_outputs(root, outputs):
    require(set(outputs) == set(OUTPUT_PATHS) and all(type(raw) is bytes for raw in outputs.values()),
            "OutputShape", root, "expected the complete generated byte set")
    for relative in OUTPUT_PATHS:
        expected = outputs[relative]
        path = Path(root) / relative
        require(path.is_file() and path.read_bytes() == expected, "StaleOutput", relative,
                "missing or different bytes; regenerate from approved inputs")


def verify_runtime_files(root, expected_digest):
    """File integrity only. Does not certify handlers, assets, approval or playable state."""
    root = Path(root)
    expected_hex = digest_from_wire(expected_digest)
    manifest_raw = read_bytes(root, MANIFEST_OUTPUT)
    require(sha256(manifest_raw) == expected_hex, "CatalogIntegrity", MANIFEST_OUTPUT, "root digest mismatch")
    manifest = parse_json(manifest_raw, MANIFEST_OUTPUT)
    keys(manifest, {"format", "schema_version", "handler_revision", "projection_revision", "author_schema_sha256",
                    "native_ids_sha256", "native_contract_sha256", "display_sha256", "content", "world"}, "manifest")
    require(manifest["format"] == "lockdown-demo-content", "CatalogIntegrity", "manifest", "wrong format")
    for key, expected in (("schema_version", SCHEMA_VERSION), ("handler_revision", HANDLER_REVISION),
                          ("projection_revision", PROJECTION_REVISION)):
        integer(manifest[key], key, expected, expected)
    for key in ("author_schema_sha256", "native_ids_sha256", "native_contract_sha256", "display_sha256"):
        digest_to_wire(manifest[key])
    keys(manifest["content"], {"file", "sha256"}, "manifest.content")
    keys(manifest["world"], {"file", "sha256", "contract"}, "manifest.world")
    require(manifest["content"]["file"] == "demo_content.json" and manifest["world"]["file"] == "world.json",
            "CatalogIntegrity", "manifest", "unexpected payload path")
    require(manifest_raw == encode(canonical(manifest)), "NonCanonical", MANIFEST_OUTPUT, "manifest bytes are not canonical")
    content_raw = read_bytes(root, CONTENT_OUTPUT)
    world_raw = read_bytes(root, WORLD_OUTPUT)
    for section, raw in (("content", content_raw), ("world", world_raw)):
        require(manifest[section]["sha256"] == sha256(raw), "CatalogIntegrity", section, "payload bytes changed")
    world_contract = parse_world_contract(read_bytes(root, WORLD_CONTRACT), world_raw)
    require(exact_equal(manifest["world"]["contract"], world_contract),
            "WorldIntegrity", WORLD_CONTRACT, "world contract bytes or revisions changed")
    require(read_bytes(root, CONTENT_CONTRACT) == content_contract_bytes(sha256(content_raw), expected_hex),
            "CatalogIntegrity", CONTENT_CONTRACT, "generated content contract changed")
    content = parse_json(content_raw, CONTENT_OUTPUT)
    validate_integer_tree(content, "content")
    require(type(content) is dict and "display" in content
            and manifest["display_sha256"] == sha256(encode(content["display"])),
            "CatalogIntegrity", "display", "display projection differs")
    validate_world(world_raw)
    return expected_digest
