"""Strict Demo author projection and file-byte identity; never supplies gameplay defaults."""
from collections.abc import Mapping
from dataclasses import dataclass
from decimal import Decimal, DecimalException
from fractions import Fraction
import hashlib
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
SCHEMA_VERSION = 2
HANDLER_REVISION = 3
PROJECTION_REVISION = 5
INT64_MAX = (1 << 63) - 1
INT64_MIN = -(1 << 63)
MAX_JSON_BYTES = 8 * 1024 * 1024
ID_PATTERN = re.compile(r"[a-z][a-z0-9_]{0,62}")
HEX_PATTERN = re.compile(r"[0-9a-f]{64}")
NATIVE_ENUMS = {
    "character": "ELDDemoCharacterId", "abilities": "ELDDemoAbilityId",
    "stats": "ELDDemoStatId", "components": "ELDDemoComponentId",
    "monsters": "ELDDemoMonsterId", "statuses": "ELDDemoStatusId",
    "effect_ops": "ELDDemoEffectOp",
}
# Inclusive native numbers that are never reused (#198 retires the passives, cower, attribute cards, their statuses and
# unused ops).
RETIRED_RANGES = {"abilities": [[0, 91], [104, 113], [119, 125]], "stats": [[0, 26]],
                  "statuses": [[7, 8], [11, 11], [15, 19], [21, 24], [28, 29]],
                  "effect_ops": [[6, 6], [9, 11], [16, 16], [19, 19], [23, 23], [25, 25], [27, 30], [32, 34], [38, 39],
                                 [41, 49], [51, 51], [53, 53], [55, 66], [68, 68], [72, 76], [83, 85], [89, 89]]}
# #97: three camp difficulty tiers, one monster definition each; the Boss is its own block.
CAMP_TIERS = frozenset(("Low", "High", "Elite"))
# Passive-form operations (thorns, lifesteal, stat bonuses, triggered effects, dash contact terms) stay importable for the
# #198 base components; no active may use them.
EFFECT_OPS = frozenset(("damage", "shield", "dispel_root", "root", "stat_bonus", "lifesteal", "dash",
                       "slow", "timed_stat_bonus", "self_area", "low_hp_stat_bonus",
                       "control_damage_amp", "taunt", "disarm", "stun", "empower_basic", "thorns",
                       "cooldown_refund", "triggered_shield", "nth_basic_bonus", "mana_on_hit", "dispel_slow",
                       "knockback", "zone", "zone_slow", "zone_damage", "shield_break_retort",
                       "silence", "breach", "burn", "zone_burn", "channel_cone", "ground_impact", "attack_slow",
                       "contact_trap", "dash_contact", "primary_guard", "dash_contact_damage", "dash_contact_refund",
                       "ledger", "basic_element", "basic_damage_scale", "idle_ready_amp", "active_element"))
# #198 B: the operations Combat reads from an installed component (ALDCombatDirector::CollectPassiveEffects). burn, breach
# and empower_basic also have an active form; the rest are component-only and no active may carry them.
COMPONENT_OPS = frozenset(("burn", "breach", "empower_basic", "ledger", "thorns", "lifesteal", "dash_contact_damage",
                           "nth_basic_bonus", "control_damage_amp", "triggered_shield", "basic_element", "stat_bonus",
                           "basic_damage_scale", "low_hp_stat_bonus", "primary_guard", "idle_ready_amp", "active_element"))
COMPONENT_ONLY_OPS = COMPONENT_OPS - {"burn", "breach", "empower_basic"}
ELEMENTS = ("fire", "water", "electric")
CATEGORY_KEYS = ("base", "mod", "connector")
# Effects delivered to each hit target; on Self/DirectionDash they need self_area to name their targets.
HOSTILE_OPS = frozenset(("damage", "root", "slow"))
# Effects a self area delivers to the bodies it names.
AREA_HIT_OPS = frozenset((*HOSTILE_OPS, "stun", "knockback", "silence", "breach", "burn"))
# Temporary and conditional bonuses never change resource maxima, so no current-resource conversion is needed.
# Every bonus stat name; a card placeholder may read one stat of its effect.
STAT_FIELDS = frozenset(("ADFlat", "APFlat", "MaxHPFlat", "MaxMPFlat", "ArmorFlat", "MagicResistFlat", "CooldownHasteFlat",
                         "AttackRangeCmFlat", "AttackSpeedBonusRatio", "MoveSpeedBonusRatio", "CritChanceBonusRatio",
                         "ADBonusRatio", "HPRegenFlat", "APBonusRatio", "MPRegenFlat"))
TEMPORARY_STATS = frozenset(("ADFlat", "APFlat", "ArmorFlat", "MagicResistFlat", "CooldownHasteFlat",
                             "AttackRangeCmFlat", "AttackSpeedBonusRatio", "MoveSpeedBonusRatio",
                             "CritChanceBonusRatio", "ADBonusRatio", "APBonusRatio"))
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
    "P2": frozenset(("hp", "mp", "points")), "R4": frozenset(("ratio",)), "Sec": frozenset(("s",)),
    "Rational": frozenset(("cm", "cm/s", "attacks/s", "hp/s", "mp/s", "degrees")),
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
    integer(value["schema_version"], IDS_SOURCE, SCHEMA_VERSION, SCHEMA_VERSION)
    require(exact_equal(value["retired_ranges"], RETIRED_RANGES),
            "RetiredId", IDS_SOURCE, "occupied legacy ranges must remain reserved")
    for section, enum_name in NATIVE_ENUMS.items():
        mapping = value[section]
        require(type(mapping) is dict and mapping, "NativeId", section, "missing ID mapping")
        for name, number in mapping.items():
            definition_id(name, section)
            integer(number, name, 0, 255)
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
    "lifesteal": (("hp_below", "low_hp_multiplier"),), "thorns": (("slow", "slow_duration"),),
    "triggered_shield": (("burst_ratio", "burst_radius"),),
    "channel_cone": (("slow", "slow_duration"),),
    "dash_contact_damage": (("slow", "slow_duration"),),
    "dash_contact_refund": (("mana_ratio", "mana_cap"),),
    "primary_guard": (("slow_resistance", "duration"),),
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
    if effect["op"] in ("attack_slow", "primary_guard"):
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
    if effect["op"] == "dash_contact_damage":
        require(any(quantity(effect[key]) > 0 for key in ("base", "ad_ratio", "ap_ratio")),
                "IneffectiveEffect", path, "additional damage needs magnitude or scaling")
    for field in ("slow", "slow_resistance", "mana_ratio"):
        if field in effect and effect["op"] in ("channel_cone", "dash_contact_damage", "primary_guard", "dash_contact_refund"):
            require(0 < quantity(effect[field]) < 1, "NumericDomain", path, f"{field} must be a positive fraction")
    if effect["op"] == "primary_guard":
        require(quantity(effect["base"]) > 0, "IneffectiveEffect", path, "cap must be positive")
    if effect["op"] == "dash_contact_refund" and "mana_cap" in effect:
        require(quantity(effect["mana_cap"]) > 0, "IneffectiveEffect", path, "mana cap must be positive")


def _validate_action(target, effects, path):
    mode = target["mode"]
    allowed = {
        "Self": frozenset(("shield", "dispel_root", "dispel_slow", "timed_stat_bonus", "self_area", "taunt", "stun",
                           "knockback", *HOSTILE_OPS, "silence", "breach", "burn", "empower_basic")),
        "Unit": frozenset(("damage", "root", "slow", "disarm", "stun", "silence", "breach", "burn", "knockback")),
        "DirectionProjectile": frozenset(("damage", "slow", "silence", "breach", "burn", "attack_slow")),
        "DirectionDash": frozenset(("dash", "self_area", "empower_basic", "zone", "zone_damage",
                                    *HOSTILE_OPS, "breach", "burn", "dash_contact")),
        "GroundPoint": frozenset(("zone", "zone_slow", "zone_damage", "zone_burn",
                                  "ground_impact", "contact_trap", "damage", "root", "slow")),
        "DirectionCone": frozenset(("channel_cone", "damage")),
    }[mode]
    ops = _effect_set(effects, path)
    require(not ops & COMPONENT_ONLY_OPS, "ComponentOnlyEffect", path,
            f"an active cannot carry component operations {sorted(ops & COMPONENT_ONLY_OPS)}")
    require(not any("trigger" in effect for effect in effects if effect["op"] == "empower_basic"), "ObjectShape", path,
            "an active's empowered basic is armed by its release, not by a trigger")
    require(ops <= allowed, "TargetEffectMismatch", path, "effect cannot consume this target form")
    if mode in ("Self", "DirectionDash") and "dash_contact" not in ops:
        require(("self_area" in ops) == bool(ops & AREA_HIT_OPS), "TargetEffectMismatch", path,
                "hit effects of a self or dash action need exactly one self_area to name their targets")
    if mode == "DirectionProjectile":
        require("damage" in ops, "TargetEffectMismatch", path, "a projectile carries damage")
    require(mode != "GroundPoint" or len(ops & {"zone", "ground_impact", "contact_trap"}) == 1,
            "MissingHandler", path, "a ground point needs exactly one delivery handler")
    require(mode != "DirectionCone" or {"channel_cone", "damage"} <= ops,
            "MissingHandler", path, "a cone needs its channel and damage")
    require("dash_contact" not in ops or ("damage" in ops and "self_area" not in ops),
            "TargetEffectMismatch", path, "contact damage must use the actual dash, not a release area")
    require(not ops.intersection({"ground_impact", "contact_trap"}) or "damage" in ops,
            "MissingHandler", path, "a delayed impact or trap needs damage")
    require(mode != "DirectionDash" or "dash" in ops, "MissingHandler", path, "a direction dash needs its dash")
    require(not (ops & {"zone_slow", "zone_damage", "zone_burn"}) or "zone" in ops,
            "MissingHandler", path, "zone payloads need their zone")
    require("zone" not in ops or bool(ops & {"zone_slow", "zone_damage", "zone_burn"}),
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
    if mode in ("Unit", "DirectionProjectile", "GroundPoint", "DirectionCone"):
        require(quantity(target["range"]) > 0, "NumericDomain", path, "cast range must be positive")
    if mode == "DirectionProjectile":
        require(quantity(target["projectile_radius"]) > 0, "NumericDomain", path, "projectile radius must be positive")


def _validate_component_effects(row, abilities, path):
    """#198 B: a component carries only component operations, each in its component form."""
    effects = row["effects"]
    ops = _effect_set(effects, path)
    require(ops <= COMPONENT_OPS, "ComponentEffect", path, f"not a component operation: {sorted(ops - COMPONENT_OPS)}")
    for effect in effects:
        _validate_new_effect(effect, path)
        op = effect["op"]
        if op in ("burn", "breach"):
            require("basic_stacks" in effect and "stacks" not in effect, "ObjectShape", path,
                    "a component applies its stacks with each primary basic hit")
        if op == "thorns":
            require(any(quantity(effect[key]) > 0 for key in ("base", "armor_ratio", "max_hp_ratio") if key in effect),
                    "IneffectiveEffect", path, "a reflection needs magnitude or scaling")
            require(quantity(effect["radius"]) > 0 and quantity(effect["critical_multiplier"]) >= 1, "NumericDomain", path,
                    "a reflection needs a positive radius and a critical multiplier of at least one")
        if op == "lifesteal":
            require(quantity(effect["ratio"]) > 0, "IneffectiveEffect", path, "lifesteal ratio must be positive")
        if op == "nth_basic_bonus":
            require(quantity(effect["distance"]) > 0, "NumericDomain", path, "the knockback distance must be positive")
        if op in ("control_damage_amp", "idle_ready_amp"):
            require(quantity(effect["ratio"]) > 0, "IneffectiveEffect", path, "damage bonus must be positive")
        if op == "triggered_shield":
            require(quantity(effect["base"]) > 0 or quantity(effect["max_hp_ratio"]) > 0, "IneffectiveEffect", path,
                    "a shield needs magnitude or scaling")
        if op == "empower_basic":
            require(effect.get("trigger") == "DashComplete" and "charges" in effect, "ObjectShape", path,
                    "a component's empowered basic needs its DashComplete trigger and its charges")
            require(any(quantity(effect[key]) > 0 for key in ("base", "ad_ratio", "ap_ratio")), "IneffectiveEffect", path,
                    "an empowered attack adds neither damage nor scaling")
            require(effect["damage_type"] == "Physical", "NumericDomain", path, "an empowered basic stays physical")
            if "stats" in effect:
                require(set(effect["stats"]) <= TEMPORARY_STATS, "UnknownField", path, "timed bonuses cannot change resource maxima")
                _positive_stats(effect["stats"], path)
        if op == "ledger":
            require(any(quantity(effect[key]) > 0 for key in ("base", "ad_ratio", "ap_ratio")), "IneffectiveEffect", path,
                    "a ledger entry needs magnitude or scaling")
        if op == "basic_damage_scale":
            require(0 < quantity(effect["ratio"]) < 1, "NumericDomain", path,
                    "the primary basic's scale must be strictly between zero and one")
        if op == "stat_bonus":
            _positive_stats(effect["stats"], path)
        if op == "low_hp_stat_bonus":
            require(0 < quantity(effect["threshold"]) < 1, "NumericDomain", path, "the HP threshold must be a fraction")
            require(set(effect["stats"]) <= TEMPORARY_STATS, "UnknownField", path, "conditional bonuses cannot change resource maxima")
            _positive_stats(effect["stats"], path)
    binding = row.get("binding")
    require(("active_element" in ops) == (binding is not None), "ComponentBinding", path,
            "active_element and its active/element binding come together")
    if binding is not None:
        actives = binding["actives"]
        require(len(set(actives)) == len(actives) and set(actives) <= set(abilities), "InvalidReference", path,
                "a binding names distinct known actives")
        require(all({"damage", "zone_damage"} & {effect["op"] for effect in abilities[identity]["effects"]} for identity in actives),
                "ComponentBinding", path, "a bound active needs native direct damage")
        elements = binding["elements"]
        require(len(set(elements)) == len(elements) and set(elements) <= set(ELEMENTS), "ComponentBinding", path,
                "binding elements are distinct fire/water/electric")


def _validate_components(data, abilities):
    previous = 0
    for row in data["components"]:
        identity = row["id"]
        require(row["native_id"] > previous, "ComponentOrder", "components",
                "components are authored once each in ascending native ID order")
        previous = row["native_id"]
        opening = row.get("opening")
        if opening is not None:
            require(row["kind"] == "base" and row["pool"] == "common", "OpeningPool", identity,
                    "only common base components are opening options")
        if type(opening) is not str and opening is not None:
            actives = opening["actives"]
            require(len(set(actives)) == len(actives) and set(actives) <= set(abilities), "InvalidReference", identity,
                    "opening actives are distinct known actives")
            require(all(abilities[active]["opening"] for active in actives), "OpeningPool", identity,
                    "an opening condition names actives of the opening pool")
        _validate_component_effects(row, abilities, identity)


def _validate_category_weights(progression):
    """#198 B: one band per level range covering every level-up 2..max_level once, each a percentage split."""
    expected = 2
    for index, band in enumerate(progression["component_category_weights"]):
        path = f"progression.component_category_weights[{index}]"
        low, high = band["levels"]
        require(low == expected and low <= high <= progression["max_level"], "CategoryWeights", path,
                "bands must cover levels 2..max_level contiguously, ascending and exactly once")
        require(sum(band[key] for key in CATEGORY_KEYS) == 100, "CategoryWeights", path,
                "base, mod and connector percentages must add up to 100")
        expected = high + 1
    require(expected == progression["max_level"] + 1, "CategoryWeights", "progression.component_category_weights",
            "bands must reach max_level")


# #198 PR1 actives (CONTENT_AND_BALANCE): one value set each; (target mode, handlers).
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
    "active_open_mic": ("DirectionCone", {"channel_cone", "damage"}),
}
TIMED_STATS = {"active_stare_down": {"ArmorFlat", "MagicResistFlat"}}
# #198: the five point-bought stats and the bonuses one point of each adds (flat and additive percent terms).
STAT_CONTRACTS = {
    "stat_physique": {"MaxHPFlat", "HPRegenFlat"},
    "stat_thick_skin": {"ArmorFlat", "MagicResistFlat"},
    "stat_strength": {"ADFlat", "CritChanceBonusRatio"},
    "stat_brains": {"APFlat", "CooldownHasteFlat", "MPRegenFlat"},
    "stat_agility": {"AttackSpeedBonusRatio", "MoveSpeedBonusRatio"},
}
# The opening opportunity draws this many distinct actives from the opening pool.
OPENING_OFFER_SIZE = 3


def validate_definitions(data, ids):
    collections = {"character": [data["character"]], "abilities": data["abilities"], "stats": data["stats"],
                   "components": data["components"], "monsters": data["camps"]["monsters"] + [data["boss"]]}
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
    _validate_category_weights(progression)
    _validate_components(data, rows_by_id(data["abilities"], "abilities"))

    for row in (*data["abilities"], *data["components"]):
        for effect in all_effects(row):
            effect["opcode"] = ids["effect_ops"][effect["op"]]

    display = rows_by_id(data["display"], "display")
    expected_display = all_ids | set(ids["statuses"])
    require(set(display) == expected_display, "DisplayReference", "display", "missing or extra display definition")
    abilities = rows_by_id(data["abilities"], "abilities")
    components = rows_by_id(data["components"], "components")
    names = set()
    for identity, row in display.items():
        require(row["name_key"] == identity + ".name" and row["description_key"] == identity + ".description"
                and row["icon_id"] == "icon_" + identity, "DisplayReference", identity, "non-canonical display reference")
        if identity == data["character"]["id"]:
            require(row.get("portrait_icon_id") == "portrait_" + identity,
                    "DisplayReference", identity, "missing character portrait")
        else:
            require("portrait_icon_id" not in row, "DisplayReference", identity, "portrait belongs only to character")
        for field in ("name_text", "description_text"):
            display_text(row[field], f"{identity}.{field}", field == "name_text")
        if identity in abilities:
            card_template(row["description_text"], abilities[identity]["effects"], f"{identity}.description_text")
        elif identity in components:
            card_template(row["description_text"], components[identity]["effects"], f"{identity}.description_text")
        elif identity in stats:
            stat_template(row["description_text"], stats[identity]["per_point"], f"{identity}.description_text")
        require(row["name_text"] not in names, "DisplayText", identity, "duplicate display name")
        names.add(row["name_text"])
    return collections


CARD_FIELDS = frozenset(("amount", "duration", "ratio", "count", "radius", "distance", "interval", "delay",
                         "secondary_ratio", "secondary_duration", "basic_count", "refund", "burst", "limit"))
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


def validate_numbers(data):
    params = data["parameters"]
    p = lambda key: quantity(params[key])
    level_steps = data["progression"]["max_level"] - 1
    for stat in ("MaxHP", "MaxMP", "AD", "AP", "Armor", "MagicResist", "HPRegen", "MPRegen"):
        key = "STAT." + stat
        minimum, maximum = p(key + ".Min"), p(key + ".Max")
        base, growth = p(key + ".Base"), p(key + ".PerLevel")
        require(minimum <= base <= base + level_steps * growth <= maximum,
                "StatBounds", key, "base/level growth is outside the declared projection bounds")
        if stat not in ("HPRegen", "MPRegen"):
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
                "AIRDROP.PickupRadiusCm", "REST.ReturnSearchRadiusCm"):
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
    for key in ("REGEN.OutOfCombatMaxHPRatioPerSec", "REGEN.OutOfCombatMaxMPRatioPerSec",
                "DORM.MaxHPRatioPerSec", "DORM.MaxMPRatioPerSec"):
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
    # D26 (#79): every drop is announced after the match starts, and one announcement ends before the next begins.
    require(p("AIRDROP.AnnounceLeadSec") <= p("AIRDROP.FirstLandSec")
            and p("AIRDROP.AnnounceLeadSec") < p("AIRDROP.IntervalSec"),
            "InvalidTiming", "AIRDROP", "the announcement lead must fit before the first landing and inside one interval")
    # #198: PK sign-up is the last part of every rest build window (R0 and R1-R3), not an extra wait.
    require(p("REST.SignupSec") <= p("REST.BuildSec") and p("REST.SignupSec") <= p("REST.OpeningBuildSec"),
            "InvalidTiming", "REST.SignupSec", "PK sign-up must fit inside every rest build window")

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
    # #198 B: a level-up whose component roll finds the common pool empty adds GROWTH.EmptyComponentPoolPoints on top.
    checked_add(params["GROWTH.OpeningPoints"], checked_mul(level_steps, checked_add(
        params["GROWTH.PointsPerLevel"], params["GROWTH.EmptyComponentPoolPoints"], "GROWTH.EmptyComponentPoolPoints"),
        "GROWTH.EmptyComponentPoolPoints"), "GROWTH.EmptyComponentPoolPoints")

    monsters = rows_by_id(data["camps"]["monsters"], "monsters")
    require({monster["tier"] for monster in monsters.values()} == CAMP_TIERS and len(monsters) == len(CAMP_TIERS),
            "CampTiers", "monsters", "exactly one monster per camp tier (Low, High, Elite) is required")
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
        for field in ("hp_regen", "max_mp", "mp_regen", "ap", "crit_chance"):
            require(quantity(monster[field]) == 0, "UnsupportedMonster", path,
                    f"{field} requires a different monster handler contract")
        checked_mul(monster["count_per_camp"], monster["reward_xp"], path)
        monster["attack_interval_ms"] = interval
        monster["perception_radius"] = dict(monster["leash_radius"])
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
    stats = boss["approval"]["stats"]
    require(set(stats) <= TEMPORARY_STATS and all(key.endswith("BonusRatio") for key in stats), "UnknownField", "boss.approval",
            "the approval buff takes temporary ratio stats only")
    _positive_stats(stats, "boss.approval")
    placements = rows_by_id(data["camps"]["placements"], "camps", "anchor_id")
    require(all(row["monster_id"] in monsters for row in placements.values()),
            "InvalidReference", "camps", "unknown monster reference")
    require({monsters[row["monster_id"]]["tier"] for row in placements.values()} == CAMP_TIERS,
            "CampTiers", "camps", "every camp tier must be placed")
    _validate_build_envelope(data)


def _validate_build_envelope(data):
    # Every attribute clamps to its catalog bounds in the projection (#112 §4), so stat points are never refused;
    # what remains is that every damage or shield term stays inside int64 at the extreme attributes.
    params = data["parameters"]
    max_ad = params["STAT.AD.Max"]["p2"]
    max_ap = params["STAT.AP.Max"]["p2"]
    max_hp = params["STAT.MaxHP.Max"]["p2"]
    # #166: levels 2..10 grow by the first step, levels 11..16 by the second.
    top_level = data["progression"]["max_level"]
    first_steps = min(max(top_level - 1, 0), 9)
    later_steps = max(top_level - 10, 0)
    factor = checked_add(10000, checked_mul(first_steps, params["SKILL.GrowthPerLevel"]["bp"], "SKILL.GrowthPerLevel"),
                         "SKILL.GrowthPerLevel")
    factor = checked_add(factor, checked_mul(later_steps, params["SKILL.GrowthPerLevelAfter10"]["bp"],
                                            "SKILL.GrowthPerLevelAfter10"), "SKILL.GrowthPerLevelAfter10")
    grow = lambda amount, path: checked_mul(amount, factor, path) // 10000
    checked_add(grow(params["COMBAT.BurnPerSec"]["p2"], "COMBAT.BurnPerSec"),
                checked_mul(max_ap, params["COMBAT.BurnAPRatioPerSec"]["bp"], "COMBAT.BurnAPRatioPerSec") // 10000, "COMBAT.BurnPerSec")
    for row in (*data["abilities"], *data["components"]):
        for effect in all_effects(row):
            base = effect["base"]["p2"] if "base" in effect else 0
            if effect["op"] != "mana_on_hit":
                base = grow(base, row["id"])
            if effect["op"] == "timed_stat_bonus":
                for stat in effect.get("stats", {}).values():
                    for storage in ("p2", "bp"):
                        if storage in stat:
                            grow(stat[storage], row["id"])
            if effect["op"] in ("damage", "shield", "empower_basic", "zone_damage", "dash_contact_damage", "ledger"):
                numerator = checked_mul(base, 10000, row["id"])
                numerator = checked_add(numerator, checked_mul(max_ap, effect["ap_ratio"]["bp"], row["id"]), row["id"])
                if effect["op"] != "shield":
                    numerator = checked_add(numerator, checked_mul(max_ad, effect["ad_ratio"]["bp"], row["id"]), row["id"])
                if effect["op"] == "damage":
                    checked_add(numerator, checked_mul(max_hp, effect["max_hp_ratio"]["bp"], row["id"]), row["id"])


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
    expected = {"Spawn": refs["spawn_ids"], "Camp": refs["camp_ids"], "Capture": [refs["capture_id"]]}
    for kind, identities in expected.items():
        require(len(set(identities)) == len(identities), "DuplicateId", kind, "repeated anchor reference")
        require(set(identities) == {identity for identity, row in anchors.items() if row["kind"] == kind},
                "InvalidReference", kind, "missing, extra or wrong-kind world anchor")
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
    arrays = [(f"content.{key}", data[key], "id") for key in ("abilities", "stats", "display")]
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
    data = schema.project(author, fields=fields)
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
