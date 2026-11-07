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
SCHEMA_VERSION = 1
HANDLER_REVISION = 1
PROJECTION_REVISION = 3
INT64_MAX = (1 << 63) - 1
INT64_MIN = -(1 << 63)
MAX_JSON_BYTES = 8 * 1024 * 1024
ID_PATTERN = re.compile(r"[a-z][a-z0-9_]{0,62}")
HEX_PATTERN = re.compile(r"[0-9a-f]{64}")
NATIVE_ENUMS = {
    "character": "ELDDemoCharacterId", "abilities": "ELDDemoAbilityId",
    "attributes": "ELDDemoAttributeId", "equipment": "ELDDemoEquipmentId",
    "monsters": "ELDDemoMonsterId", "statuses": "ELDDemoStatusId",
    "effect_ops": "ELDDemoEffectOp",
}
EFFECT_OPS = frozenset(("damage", "shield", "dispel_root", "root", "stat_bonus",
                       "lifesteal", "dash", "cleave", "dash_boost",
                       "slow", "timed_stat_bonus", "self_area", "low_hp_stat_bonus",
                       "control_damage_amp", "taunt", "disarm", "stun", "empower_basic", "thorns",
                       "cooldown_refund", "ability_crit", "triggered_shield", "crit_damage_reduction", "nth_basic_bonus",
                       "mana_on_hit", "dispel_slow", "path_slow",
                       # #112 §5 mechanics still used by the catalog.
                       "knockback", "zone", "zone_slow", "zone_root", "zone_damage",
                       "cast_frenzy", "affliction_amp", "shield_break_retort", "hit_haste",
                       # #147 build v7 keyword primitives (docs/systems/COMBAT_AND_GROWTH.md).
                       "silence", "breach", "burn", "execute", "pierce", "echo", "taunt_shield", "dispel_disarm",
                       "zone_burn"))
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
    "Rational": frozenset(("cm", "cm/s", "attacks/s", "hp/s", "mp/s")),
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
    require(exact_equal(value["retired_ranges"], {"abilities": [[0, 91]], "attributes": [[0, 14]],
                                                  "equipment": [[0, 98], [107, 107], [113, 114]],
                                                  "statuses": [[7, 8], [11, 11], [15, 19], [21, 24]],
                                                  "effect_ops": [[6, 6], [9, 9], [16, 16], [19, 19], [23, 23], [25, 25],
                                                                 [28, 30], [32, 33], [39, 39], [41, 49], [51, 51],
                                                                 [55, 64]]}),
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


def _same_handlers(base, upgraded):
    return [effect["op"] for effect in base] == [effect["op"] for effect in upgraded]


def _positive_stats(stats, path):
    require(stats and all(quantity(value) > 0 for value in stats.values()),
            "IneffectiveChoice", path, "every declared stat bonus must be positive")


def _validate_thorns(effect, path):
    require(quantity(effect["base"]) > 0 or quantity(effect["armor_ratio"]) > 0,
            "IneffectiveEffect", path, "a reflection has neither magnitude nor armor scaling")
    require(quantity(effect["radius"]) > 0, "NumericDomain", path, "reflection reach must be positive")
    require(quantity(effect["critical_multiplier"]) >= 1, "NumericDomain", path,
            "a critical hit cannot shrink the reflection")


# Positive-magnitude fields of the #112 handlers: at least one of each group must be positive.
_NEW_OP_MAGNITUDES = {
    "zone_damage": ("base", "ad_ratio", "ap_ratio"), "taunt_shield": ("base", "max_hp_ratio"), "execute": ("ratio",),
    "knockback": ("distance",), "zone": ("radius",), "zone_slow": ("ratio",),
}


# #147 optional terms that only come in pairs (the native importer refuses the same shapes).
_PAIRED_FIELDS = {
    "lifesteal": (("hp_below", "low_hp_multiplier"),), "thorns": (("slow", "slow_duration"),),
    "triggered_shield": (("burst_ratio", "burst_radius"),), "knockback": (("wall_distance", "wall_stun"),),
    "empower_basic": (("cleave_ratio", "cleave_radius"),),
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
    if effect["op"] == "cast_frenzy":
        require(set(effect["stats"]) <= TEMPORARY_STATS, "UnknownField", path, "timed bonuses cannot change resource maxima")
        _positive_stats(effect["stats"], path)


def _validate_action(target, ranks, path):
    mode = target["mode"]
    allowed = {
        "Self": frozenset(("shield", "dispel_root", "dispel_slow", "timed_stat_bonus", "self_area", "taunt", "stun",
                           "knockback", *HOSTILE_OPS,
                           "silence", "breach", "burn", "execute", "echo", "taunt_shield", "dispel_disarm", "empower_basic")),
        "Unit": frozenset(("damage", "root", "slow", "disarm", "stun",
                           "silence", "breach", "burn", "execute", "knockback")),
        "DirectionProjectile": frozenset(("damage", "slow", "silence", "breach", "burn", "execute", "pierce")),
        "DirectionDash": frozenset(("dash", "self_area", "empower_basic", "path_slow", "zone", "zone_damage",
                                    *HOSTILE_OPS, "breach", "burn")),
        "GroundPoint": frozenset(("zone", "zone_slow", "zone_root", "zone_damage", "zone_burn")),
    }[mode]
    signature = None
    for rank in ranks:
        ops = _effect_set(rank["effects"], path)
        require(ops <= allowed, "TargetEffectMismatch", path, "effect cannot consume this target form")
        # Lower ranks share their handlers; the advanced (top) rank may change them (#147 "高级 = 数值 + 一条玩法变化"),
        # checked by its contract.
        require(signature is None or rank["rank"] == len(ranks) or signature == ops, "RankShape", path,
                "ranks must use the same handlers")
        signature = signature if signature is not None else ops
        if mode in ("Self", "DirectionDash"):
            require(("self_area" in ops) == bool(ops & AREA_HIT_OPS), "TargetEffectMismatch", path,
                    "hit effects of a self or dash action need exactly one self_area to name their targets")
        if mode == "DirectionProjectile":
            require("damage" in ops, "TargetEffectMismatch", path, "a projectile carries damage")
        require(mode != "GroundPoint" or "zone" in ops, "MissingHandler", path, "a ground point is consumed by a zone")
        require(mode != "DirectionDash" or "dash" in ops, "MissingHandler", path, "a direction dash needs its dash")
        require(not (ops & {"zone_slow", "zone_root", "zone_damage", "zone_burn"}) or "zone" in ops,
                "MissingHandler", path, "zone payloads need their zone")
        require("zone" not in ops or bool(ops & {"zone_slow", "zone_root", "zone_damage", "zone_burn"}),
                "IneffectiveEffect", path, "a zone needs a payload")
        # #147: an execute bonus and a second blast amplify or repeat a damage effect; a pierce rides on a projectile;
        # a per-taunt shield counts its own taunt's victims.
        require("execute" not in ops or "damage" in ops, "MissingHandler", path, "an execute bonus amplifies a damage effect")
        require("echo" not in ops or {"damage", "self_area"} <= ops, "MissingHandler", path,
                "a second blast repeats a self-area damage effect")
        require("taunt_shield" not in ops or "taunt" in ops, "MissingHandler", path, "a taunt shield needs its taunt")
        for effect in rank["effects"]:
            _validate_new_effect(effect, path)
            if effect["op"] == "empower_basic":
                require(any(quantity(effect[key]) > 0 for key in ("base", "ad_ratio", "ap_ratio")),
                        "IneffectiveEffect", path, "an empowered attack adds neither damage nor scaling")
                require(("cleave_ratio" in effect) == ("cleave_radius" in effect), "ObjectShape", path,
                        "an empowered cleave needs its ratio and radius")
                if "stats" in effect:
                    require(set(effect["stats"]) <= TEMPORARY_STATS, "UnknownField", path,
                            "timed bonuses cannot change resource maxima")
                    _positive_stats(effect["stats"], path)
            if effect["op"] in ("breach", "burn"):
                require("stacks" in effect and "basic_stacks" not in effect, "ObjectShape", path,
                        "an active applies its stacks to the bodies it hits")
            if effect["op"] == "knockback":
                require(("wall_distance" in effect) == ("wall_stun" in effect), "ObjectShape", path,
                        "a wall stun needs its travel threshold and duration")
                require("wall_distance" not in effect or 0 < quantity(effect["wall_distance"]) < quantity(effect["distance"]),
                        "NumericDomain", path, "the wall threshold lies strictly inside the push distance")
            if effect["op"] in ("dash", "cleave"):
                field = "distance" if effect["op"] == "dash" else "radius"
                require(quantity(effect[field]) > 0, "NumericDomain", path, "geometry must be positive")
            if effect["op"] in ("damage", "shield"):
                quantities = [effect["base"], effect["ap_ratio"]] + ([effect["max_hp_ratio"]] if "max_hp_ratio" in effect else [])
                if effect["op"] == "damage":
                    quantities += [effect["ad_ratio"]]
                require(any(quantity(value) > 0 for value in quantities),
                        "IneffectiveEffect", path, "effect has neither magnitude nor scaling")
            if effect["op"] in ("slow", "path_slow"):
                require(0 < quantity(effect["ratio"]) < 1, "NumericDomain", path, "slow must keep some move speed")
            if effect["op"] == "self_area":
                require(quantity(effect["radius"]) > 0, "NumericDomain", path, "area radius must be positive")
            if effect["op"] == "timed_stat_bonus":
                require(set(effect["stats"]) <= TEMPORARY_STATS, "UnknownField", path,
                        "timed bonuses cannot change resource maxima")
                _positive_stats(effect["stats"], path)
            if effect["op"] == "taunt":
                require(quantity(effect["radius"]) > 0, "NumericDomain", path, "taunt radius must be positive")
    if mode in ("Unit", "DirectionProjectile", "GroundPoint"):
        require(quantity(target["range"]) > 0, "NumericDomain", path, "cast range must be positive")
    if mode == "DirectionProjectile":
        require(quantity(target["projectile_radius"]) > 0, "NumericDomain", path, "projectile radius must be positive")


def validate_definitions(data, ids):
    collections = {"character": [data["character"]], "abilities": data["abilities"],
                   "attributes": data["attributes"], "equipment": data["equipment"],
                   "monsters": data["camps"]["monsters"] + [data["boss"]]}
    all_ids = set()
    for section, rows in collections.items():
        index = rows_by_id(rows, section)
        require(set(index) <= set(ids[section]), "UnknownId", section, "unknown or retired content ID")
        require(not all_ids.intersection(index), "DuplicateId", section, "definition appears in another category")
        all_ids.update(index)
        for row in rows:
            row["native_id"] = ids[section][row["id"]]

    # #147 v7 catalog. Actives: (target mode, basic-rank handlers, advanced-rank handlers).
    active_contracts = {
        "active_pants_down": ("Unit", {"damage", "root"}, {"damage", "root"}),
        "active_crotch_grab": ("Unit", {"damage", "stun"}, {"damage", "stun", "execute"}),
        "active_snark_bolt": ("DirectionProjectile", {"damage", "slow"}, {"damage", "slow", "pierce"}),
        "active_seethe": ("Self", {"self_area", "damage", "burn"}, {"self_area", "damage", "burn", "echo"}),
        "active_bail_out": ("DirectionDash", {"dash", "empower_basic"}, {"dash", "empower_basic", "path_slow"}),
        "active_elbow": ("Unit", {"damage", "knockback"}, {"damage", "knockback"}),
        "active_not_mad": ("Self", {"shield", "dispel_slow"}, {"shield", "dispel_slow", "dispel_root", "dispel_disarm"}),
        "active_stare_down": ("Self", {"taunt", "timed_stat_bonus"}, {"taunt", "timed_stat_bonus", "taunt_shield"}),
        "active_quick_meeting": ("GroundPoint", {"zone", "zone_damage", "zone_slow", "zone_burn"},
                                 {"zone", "zone_damage", "zone_slow", "zone_burn", "zone_root"}),
        "active_account_banned": ("Unit", {"damage", "silence"}, {"damage", "silence", "breach"}),
        "active_unplug": ("Unit", {"damage", "disarm"}, {"damage", "disarm", "slow"}),
        "active_triple_combo": ("Self", {"empower_basic"}, {"empower_basic"}),
    }
    timed_stats = {"active_stare_down": {"ArmorFlat", "MagicResistFlat"}}
    # Passives: (basic-rank handlers, advanced-rank handlers, stats of a stat-bearing handler or None).
    passive_contracts = {
        "passive_pressed": ({"low_hp_stat_bonus"}, {"low_hp_stat_bonus"}, {"AttackSpeedBonusRatio", "MoveSpeedBonusRatio"}),
        "passive_tilted": ({"breach"}, {"breach"}, None),
        "passive_stoke": ({"burn"}, {"burn"}, None),
        "passive_kick_down": ({"control_damage_amp"}, {"control_damage_amp"}, None),
        "passive_thick_face": ({"thorns"}, {"thorns"}, None),
        "passive_freeloader": ({"lifesteal"}, {"lifesteal"}, None),
        "passive_special_forces": ({"cast_frenzy"}, {"cast_frenzy", "dash_boost"}, None),
        "passive_moral_victory": ({"triggered_shield"}, {"triggered_shield"}, None),
        "passive_iced_americano": ({"stat_bonus"}, {"stat_bonus", "cooldown_refund"}, {"CooldownHasteFlat", "MPRegenFlat"}),
        "passive_lucky_star": ({"stat_bonus"}, {"stat_bonus", "ability_crit"}, {"CritChanceBonusRatio"}),
    }
    top_rank = data["progression"]["max_skill_rank"]
    for ability in data["abilities"]:
        identity = ability["id"]
        require([rank["rank"] for rank in ability["ranks"]] == list(range(1, top_rank + 1)),
                "RankShape", identity, "every rank from 1 to max_skill_rank, in order, is required")
        if ability["kind"] == "active":
            require(identity in active_contracts, "MissingHandler", identity, "no active definition projection")
            mode, effects, advanced = active_contracts[identity]
            require(ability["target"]["mode"] == mode, "TargetEffectMismatch", identity, "wrong definition target")
            _validate_action(ability["target"], ability["ranks"], identity)
            require(all(_effect_set(rank["effects"], identity) == (advanced if rank["rank"] == top_rank else effects)
                        for rank in ability["ranks"]),
                    "MissingHandler", identity, "definition has missing or extra effects")
            if identity in timed_stats:
                require(all(set(effect["stats"]) == timed_stats[identity] for rank in ability["ranks"]
                            for effect in rank["effects"] if effect["op"] == "timed_stat_bonus"),
                        "UnknownField", identity, "wrong timed stat")
            family = "cd_" + identity.removeprefix("active_")
            require(ability["cooldown_family"] == family, "InvalidReference", identity, "wrong cooldown family")
        else:
            require(identity in passive_contracts, "MissingHandler", identity, "no passive definition projection")
            operations, advanced, stat_fields = passive_contracts[identity]
            for rank in ability["ranks"]:
                require(_effect_set(rank["effects"], identity) == (advanced if rank["rank"] == top_rank else operations),
                        "MissingHandler", identity, "invalid passive handlers")
                for effect in rank["effects"]:
                    operation = effect["op"]
                    _validate_new_effect(effect, identity)
                    if operation in ("stat_bonus", "low_hp_stat_bonus") and stat_fields is not None:
                        require(set(effect["stats"]) == stat_fields, "UnknownField", identity, "wrong passive stat")
                        _positive_stats(effect["stats"], identity)
                    if operation == "low_hp_stat_bonus":
                        require(0 < quantity(effect["threshold"]) < 1, "NumericDomain", identity,
                                "the HP threshold must lie strictly inside the pool")
                    elif operation == "thorns":
                        _validate_thorns(effect, identity)
                    elif operation == "control_damage_amp":
                        require(quantity(effect["ratio"]) > 0, "NumericDomain", identity, "amplification must be positive")
                    elif operation == "ability_crit":
                        require(set(effect["abilities"]) <= set(active_contracts),
                                "InvalidReference", identity, "a passive ability crit names approved actives")
                    elif operation in ("breach", "burn"):
                        require("stacks" in effect or "basic_stacks" in effect, "ObjectShape", identity,
                                "a passive keyword applies stacks on Q/W or basic hits")
        # D2: the advanced rank keeps every coefficient and multiplies each growing base by 1.5 (#147).
        basic, advanced_rank = ability["ranks"][0]["effects"], ability["ranks"][-1]["effects"]
        for effect in basic:
            twin = next((row for row in advanced_rank if row["op"] == effect["op"]), None)
            if twin is None:
                continue
            for field in ("ad_ratio", "ap_ratio", "max_hp_ratio", "armor_ratio"):
                if field in effect:
                    require(exact_equal(effect[field], twin.get(field)), "AdvancedRank", identity,
                            f"{effect['op']}.{field} changes between ranks")
            if "base" in effect and effect["op"] != "mana_on_hit":
                require(quantity(twin["base"]) == quantity(effect["base"]) * Fraction(3, 2), "AdvancedRank", identity,
                        f"{effect['op']}.base must be 1.5x in the advanced rank")
    attribute_stats = {
        "attr_protein": {"ADFlat", "MaxHPFlat"}, "attr_fast_hands": {"AttackSpeedBonusRatio", "ADFlat"},
        "attr_all_nighter": {"APFlat", "CooldownHasteFlat"}, "attr_canteen": {"MaxHPFlat", "ArmorFlat", "MagicResistFlat"},
        "attr_big_talk": {"APFlat", "MaxHPFlat"}, "attr_morning_rush": {"MoveSpeedBonusRatio", "MaxHPFlat"},
        "attr_hand_out": {"AttackRangeCmFlat", "AttackSpeedBonusRatio"},
        "attr_wild_frenzy": {"ADFlat", "AttackSpeedBonusRatio"},
    }
    # Wild cards name their deductions explicitly (positive magnitudes the projection subtracts).
    attribute_penalties = {"attr_wild_frenzy": {"MaxHPFlat"}}
    for attribute in data["attributes"]:
        require(attribute["id"] in attribute_stats
                and set(attribute["stats"]) == attribute_stats[attribute["id"]],
                "UnknownField", attribute["id"], "attribute definition does not declare these stats")
        _positive_stats(attribute["stats"], attribute["id"])
        penalties = attribute_penalties.get(attribute["id"])
        require(("penalties" in attribute) == (penalties is not None)
                and (penalties is None or set(attribute["penalties"]) == penalties),
                "UnknownField", attribute["id"], "attribute definition does not declare these penalties")
        if penalties is not None:
            _positive_stats(attribute["penalties"], attribute["id"])
    progression = data["progression"]
    opening = progression["opening_offer"]
    draftable = {kind: sum(1 for row in data["abilities"] if row["kind"] == kind and row["draftable"])
                 for kind in ("active", "passive")}
    require(opening["active_count"] + opening["passive_count"] == 3
            and opening["active_count"] <= draftable["active"] and opening["passive_count"] <= draftable["passive"],
            "OpeningOffer", "progression.opening_offer",
            "the opening offer must draw three distinct new skills from the draftable actives and passives")
    reroll_levels = progression["reroll_levels"]
    require(reroll_levels == sorted(set(reroll_levels))
            and all(2 <= level <= progression["max_level"] for level in reroll_levels),
            "RerollLevels", "progression.reroll_levels",
            "reroll levels must be distinct, ascending and reachable by levelling up")

    item_stats = {
        "item_iron_plate": {"MaxHPFlat", "ArmorFlat"},
        "item_edge_blade": {"ADFlat", "AttackSpeedBonusRatio"},
        "item_runic_focus": {"APFlat", "CooldownHasteFlat"},
        "item_siphon_emblem": {"ADFlat", "MagicResistFlat"},
        "item_phase_anchor": {"MoveSpeedBonusRatio"},
        "item_arcane_battery": {"MaxMPFlat", "MagicResistFlat"},
        "item_echo_maul": {"ADFlat", "AttackRangeCmFlat"},
        "item_slingshot": {"AttackRangeCmFlat", "AttackSpeedBonusRatio"},
        "item_nail_uniform": {"MaxHPFlat", "ArmorFlat"},
        "item_megaphone": {"APFlat", "CooldownHasteFlat"},
        "item_dossier": {"APFlat", "MaxHPFlat"},
        "item_privacy_screen": {"MaxHPFlat", "APFlat"},
        "item_skateboard": {"ADFlat", "MoveSpeedBonusRatio"},
    }
    item_passives = {
        "item_iron_plate": ("passive_crit_guard", {"crit_damage_reduction"}),
        "item_edge_blade": ("passive_third_strike", {"nth_basic_bonus"}),
        "item_runic_focus": ("passive_mana_refund", {"mana_on_hit"}),
        "item_siphon_emblem": ("passive_siphon", {"lifesteal"}),
        "item_phase_anchor": ("passive_stride", {"dash_boost"}),
        "item_echo_maul": ("passive_cleave", {"cleave"}),
        "item_nail_uniform": ("passive_prickle", {"thorns"}),
        "item_megaphone": ("passive_megaphone", {"affliction_amp"}),
        "item_privacy_screen": ("passive_privacy_screen", {"shield_break_retort"}),
        "item_skateboard": ("passive_skateboard", {"hit_haste"}),
    }
    passive_families = {}
    for item in data["equipment"]:
        identity = item["id"]
        require(identity in item_stats and set(item["stats"]) == item_stats[identity],
                "UnknownField", identity, "equipment definition does not declare these stats")
        _positive_stats(item["stats"], identity)
        passive = item["passive"]
        require((passive is not None) == (identity in item_passives), "MissingHandler", identity, "wrong passive presence")
        if passive is not None:
            family, effects = item_passives[identity]
            require(passive["family"] == family and _effect_set(passive["effects"], identity) == effects,
                    "InvalidReference", identity, "wrong passive family or effects")
            upgraded_effects = passive["upgraded"]["effects"] if "upgraded" in passive else None
            require(upgraded_effects is None or _same_handlers(passive["effects"], upgraded_effects), "UpgradeShape", identity,
                    "the upgraded passive must keep the base handlers in the same order")
            terms = (passive["effects"], upgraded_effects)
            previous = passive_families.setdefault(family, terms)
            require(previous == terms, "PassiveConflict", identity, "same family has different terms")
            for effect in (*passive["effects"], *(upgraded_effects or ())):
                if effect["op"] == "dash_boost":
                    # D20: the innate dash cooldown is multiplied by cooldown_ratio and its distance by 1 + distance_bonus.
                    require(quantity(effect["cooldown_ratio"]) > 0, "NumericDomain", identity,
                            "a dash cooldown ratio must keep a positive cooldown")
                    require(quantity(effect["cooldown_ratio"]) < 1 or quantity(effect["distance_bonus"]) > 0,
                            "IneffectiveEffect", identity, "dash boost changes neither cooldown nor distance")
                    continue
                if effect["op"] == "thorns":
                    _validate_thorns(effect, identity)
                    continue
                if effect["op"] in ("affliction_amp", "shield_break_retort", "hit_haste"):
                    ratios = ("dot_ratio", "slow_scale") if effect["op"] == "affliction_amp" else ("ratio",)
                    require(all(quantity(effect[field]) > 0 for field in ratios), "NumericDomain", identity,
                            "passive ratio must be positive")
                    continue
                if effect["op"] in ("nth_basic_bonus", "mana_on_hit"):
                    continue
                require(quantity(effect["ratio"]) > 0, "NumericDomain", identity, "passive ratio must be positive")
                if effect["op"] == "cleave":
                    require(quantity(effect["radius"]) > 0, "NumericDomain", identity, "cleave radius must be positive")

    for collection in (data["abilities"], data["equipment"]):
        for row in collection:
            for effect in all_effects(row):
                effect["opcode"] = ids["effect_ops"][effect["op"]]

    display = rows_by_id(data["display"], "display")
    expected_display = all_ids | set(ids["statuses"])
    require(set(display) == expected_display, "DisplayReference", "display", "missing or extra display definition")
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
        ability = next((ability for ability in data["abilities"] if ability["id"] == identity), None)
        require(("advanced_key" in row) == ("advanced_text" in row) and (ability is not None or "advanced_text" not in row),
                "DisplayReference", identity, "only a skill has an advanced line, with its key")
        if "advanced_text" in row:
            require(row["advanced_key"] == identity + ".advanced", "DisplayReference", identity, "non-canonical advanced line reference")
            display_text(row["advanced_text"], f"{identity}.advanced_text", False)
        if ability is not None:
            card_template(row["description_text"], ability["ranks"], f"{identity}.description_text")
            card_template(row.get("advanced_text", ""), ability["ranks"][-1:], f"{identity}.advanced_text")
        require(row["name_text"] not in names, "DisplayText", identity, "duplicate display name")
        names.add(row["name_text"])
    return collections


CARD_FIELDS = frozenset(("amount", "duration", "ratio", "count", "radius", "distance", "interval", "delay",
                         "secondary_ratio", "secondary_duration", "basic_count", "cooldown_cut", "refund", "burst"))
CARD_PLACEHOLDER = re.compile(r"\[([a-z_]+)\.([A-Za-z_]+)\]")


def card_template(text, ranks, path):
    """#147: [op.field] placeholders of a skill's card text read an effect present in every rank it is shown for."""
    require("[" not in CARD_PLACEHOLDER.sub("", text) and "]" not in CARD_PLACEHOLDER.sub("", text),
            "CardTemplate", path, "a bracket outside a [op.field] placeholder")
    for op, field in CARD_PLACEHOLDER.findall(text):
        require(op in EFFECT_OPS and (field in CARD_FIELDS or field in STAT_FIELDS), "CardTemplate", path,
                f"unknown placeholder [{op}.{field}]")
        for rank in ranks:
            effect = next((effect for effect in rank["effects"] if effect["op"] == op), None)
            require(effect is not None and (field in CARD_FIELDS or field in effect.get("stats", {})), "CardTemplate", path,
                    f"[{op}.{field}] names no effect of rank {rank['rank']}")


def display_text(text, path, allow_digits):
    require(type(text) is str and text != "", "DisplayText", path, "display text is required")
    require(DISPLAY_TEXT_FORBIDDEN.search(text) is None, "DisplayText", path,
            "display text contains a forbidden character")
    require(text[0] not in DISPLAY_TEXT_EDGE and text[-1] not in DISPLAY_TEXT_EDGE, "DisplayText", path,
            "display text has leading or trailing space")
    require(allow_digits or DISPLAY_DESCRIPTION_DIGIT.search(text) is None, "DisplayText", path,
            "display description contains digits; numbers come from typed fields")


def all_effects(row):
    for rank in row.get("ranks", []):
        yield from rank["effects"]
    passive = row.get("passive")
    if passive is not None:
        yield from passive["effects"]
        if "upgraded" in passive:
            yield from passive["upgraded"]["effects"]


def upgraded_stats(stats, ratio_bp, path):
    """112 §7: an upgraded item's fixed stats are 1 + SHOP.UpgradeStatBonusRatio times the base, floored per field
    precision (P2 points/resources, R4 ratios, whole centimetres of attack range)."""
    result = {}
    for key, value in stats.items():
        if "p2" in value or "bp" in value:
            storage = "p2" if "p2" in value else "bp"
            amount = checked_mul(value[storage], checked_add(10000, ratio_bp, path), path)
            result[key] = {"unit": value["unit"], storage: amount // 10000}
        else:
            scaled = quantity(value) * Fraction(10000 + ratio_bp, 10000)
            result[key] = {"unit": value["unit"], "numerator": integer(scaled.numerator // scaled.denominator, path),
                           "denominator": 1}
    return result


def validate_numbers(data):
    params = data["parameters"]
    p = lambda key: quantity(params[key])
    for stat in ("MaxHP", "MaxMP", "AD", "AP", "Armor", "MagicResist", "HPRegen", "MPRegen"):
        key = "STAT." + stat
        minimum, maximum = p(key + ".Min"), p(key + ".Max")
        base, growth = p(key + ".Base"), p(key + ".PerLevel")
        require(minimum <= base <= base + 9 * growth <= maximum,
                "StatBounds", key, "base/level growth is outside the declared projection bounds")
        if stat not in ("HPRegen", "MPRegen"):
            checked_add(params[key + ".Base"]["p2"], checked_mul(9, params[key + ".PerLevel"]["p2"], key), key)
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
    for key in ("VISION.RadiusCm", "SHOP.InteractRadiusCm", "CAPTURE.RadiusCm", "CAMP.ResetArrivalRadiusCm",
                "AIRDROP.PickupRadiusCm"):
        require(p(key) > 0, "InvalidGeometry", key, "radius must be positive")
    for name in ("ResistanceScale", "HasteScale"):
        require(quantity(data["formulas"][name]) > 0, "NumericDomain", name, "formula denominator scale must be positive")
    require(p("STAT.CritMultiplier") >= 1, "NumericDomain", "STAT.CritMultiplier", "critical multiplier must not reduce damage")
    require(p("STAT.AbilityCritMultiplier") >= 1, "NumericDomain", "STAT.AbilityCritMultiplier",
            "ability critical multiplier must not reduce damage")
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
    # D27 (#79): the leader circle always contains the leader, the minimum level exists, and the cap is reachable.
    require(0 < p("LEADER.CircleMaxOffsetCm") <= p("LEADER.CircleRadiusCm")
            and params["LEADER.MinLevel"] <= data["progression"]["max_level"]
            and params["LEADER.BountyGoldPerRefresh"] <= params["LEADER.BountyGoldMax"],
            "InvalidPolicy", "LEADER", "the circle must contain the leader, the level must exist and the cap must be reachable")
    # D26 (#79): every drop is announced after the match starts, and one announcement ends before the next begins.
    require(p("AIRDROP.AnnounceLeadSec") <= p("AIRDROP.FirstLandSec")
            and p("AIRDROP.AnnounceLeadSec") < p("AIRDROP.IntervalSec"),
            "InvalidTiming", "AIRDROP", "the announcement lead must fit before the first landing and inside one interval")

    thresholds = [0]
    for level in range(1, 10):
        increment = checked_add(params["XP.BaseToNext"],
                                checked_mul(level - 1, params["XP.IncreasePerLevel"], "XP"), "XP")
        thresholds.append(checked_add(thresholds[-1], increment, "XP"))
    require(data["progression"]["xp_thresholds"] == thresholds,
            "XPThresholds", "progression", "thresholds do not match the explicit linear XP parameters")
    data["progression"]["xp_to_next"] = [b - a for a, b in zip(thresholds, thresholds[1:])]
    for reward in ("REWARD.PlayerXP", "REWARD.PlayerGold"):
        checked_add(params[reward + ".Base"], checked_mul(9, params[reward + ".PerLevel"], reward), reward)

    for item in data["equipment"]:
        price = item["buy_price"]
        numerator = checked_mul(price, params["SHOP.SellRatio"]["bp"], item["id"])
        item["sell_gold"] = numerator // 10000
        require(item["sell_gold"] < price, "Arbitrage", item["id"], "buy/sell must lose gold")
        checked_add(params["SHOP.StartingGold"], price, item["id"])
        item["upgraded_stats"] = upgraded_stats(item["stats"], params["SHOP.UpgradeStatBonusRatio"]["bp"], item["id"])
    require(params["CHOICE.NewActiveFullSlotWeight"]["bp"] > 0, "NumericDomain", "CHOICE.NewActiveFullSlotWeight",
            "a draft weight must be positive")
    checked_add(params["SHOP.StartingGold"], params["SHOP.UpgradeCost"], "SHOP.UpgradeCost")

    monsters = rows_by_id(data["camps"]["monsters"], "monsters")
    require({monster["tier"] for monster in monsters.values()} == {"Low", "High"},
            "CampTiers", "monsters", "both difficulty tiers are required")
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
        checked_mul(monster["count_per_camp"], monster["reward_gold"], path)
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
    require({monsters[row["monster_id"]]["tier"] for row in placements.values()} == {"Low", "High"},
            "CampTiers", "camps", "both tiers must appear in the five camps")
    _validate_build_envelope(data)


def _validate_build_envelope(data):
    # Every attribute clamps to its catalog bounds in the projection (#112 §4), so repeatable cards are never refused;
    # what remains is that every damage or shield term stays inside int64 at the extreme attributes.
    params = data["parameters"]
    max_ad = params["STAT.AD.Max"]["p2"]
    max_ap = params["STAT.AP.Max"]["p2"]
    max_armor = params["STAT.Armor.Max"]["p2"]
    max_hp = params["STAT.MaxHP.Max"]["p2"]
    for row in (*data["abilities"], *data["equipment"]):
        for effect in all_effects(row):
            if effect["op"] in ("damage", "shield", "empower_basic", "zone_damage"):
                numerator = checked_mul(effect["base"]["p2"], 10000, row["id"])
                numerator = checked_add(numerator, checked_mul(max_ap, effect["ap_ratio"]["bp"], row["id"]), row["id"])
                if effect["op"] != "shield":
                    numerator = checked_add(numerator, checked_mul(max_ad, effect["ad_ratio"]["bp"], row["id"]), row["id"])
                if effect["op"] == "damage":
                    checked_add(numerator, checked_mul(max_hp, effect["max_hp_ratio"]["bp"], row["id"]), row["id"])
            elif effect["op"] == "thorns":
                numerator = checked_mul(effect["base"]["p2"], 10000, row["id"])
                numerator = checked_add(numerator, checked_mul(max_armor, effect["armor_ratio"]["bp"], row["id"]), row["id"])
                if "max_hp_ratio" in effect:
                    numerator = checked_add(numerator, checked_mul(max_hp, effect["max_hp_ratio"]["bp"], row["id"]), row["id"])
                checked_mul(numerator, effect["critical_multiplier"]["bp"], row["id"])


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
    expected = {"Spawn": refs["spawn_ids"], "Camp": refs["camp_ids"], "Shop": refs["shop_ids"], "Capture": [refs["capture_id"]]}
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
    arrays = [(f"content.{key}", data[key], "id") for key in ("abilities", "attributes", "equipment", "display")]
    arrays.extend((("content.camps.monsters", data["camps"]["monsters"], "id"),
                   ("content.camps.placements", data["camps"]["placements"], "anchor_id"),
                   ("content.world.spawn_ids", data["world"]["spawn_ids"], None),
                   ("content.world.camp_ids", data["world"]["camp_ids"], None),
                   ("content.world.shop_ids", data["world"]["shop_ids"], None)))
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
