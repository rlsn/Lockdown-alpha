"""Shared validation for authored catalogs and native identity mappings."""
import json
import math
from pathlib import Path
import re


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read_json(path):
    def reject_constant(value):
        raise ValueError(f"{path}: non-finite JSON constant {value}")
    return json.loads(Path(path).read_text(encoding="utf-8-sig"),
                      parse_constant=reject_constant)


def encode(value):
    return (json.dumps(value, indent=2, ensure_ascii=False, allow_nan=False) + "\n").encode("utf-8")


def rows_by_id(rows, label):
    require(isinstance(rows, list), f"{label}: expected rows")
    require(all(isinstance(row, dict) and isinstance(row.get("id"), str) and row["id"]
                for row in rows), f"{label}: missing or invalid ID")
    result = {row["id"]: row for row in rows}
    require(len(result) == len(rows), f"{label}: duplicate IDs")
    return result


def validate_schema(value, schema, path="catalog"):
    """Evaluate only the schema keywords supported by the authored contract."""
    supported = {"$schema", "title", "type", "required", "properties", "items",
                 "const", "enum", "oneOf", "anyOf", "minItems", "maxItems",
                 "minLength", "pattern", "minimum", "maximum", "additionalProperties"}
    require(set(schema) <= supported, f"{path}: unsupported schema keywords")
    types = {"object": lambda x: isinstance(x, dict),
             "array": lambda x: isinstance(x, list),
             "string": lambda x: isinstance(x, str),
             "integer": lambda x: type(x) is int,
             "number": lambda x: type(x) in (int, float) and math.isfinite(x),
             "boolean": lambda x: type(x) is bool,
             "null": lambda x: x is None}
    if "type" in schema:
        require(schema["type"] in types, f"{path}: unknown schema type")
        require(types[schema["type"]](value), f"{path}: expected {schema['type']}")
    if "const" in schema:
        require(value == schema["const"], f"{path}: wrong constant")
    if "enum" in schema:
        require(value in schema["enum"], f"{path}: unknown enum {value}")
    for keyword in ("oneOf", "anyOf"):
        if keyword not in schema:
            continue
        matches = 0
        for branch in schema[keyword]:
            try:
                validate_schema(value, branch, path)
                matches += 1
            except ValueError:
                pass
        require(matches == 1 if keyword == "oneOf" else matches > 0,
                f"{path}: {keyword} mismatch")
    if isinstance(value, dict):
        require(set(schema.get("required", [])) <= value.keys(), f"{path}: missing required fields")
        properties = schema.get("properties", {})
        additional = schema.get("additionalProperties", True)
        for key, child in value.items():
            if key in properties:
                validate_schema(child, properties[key], f"{path}.{key}")
            elif isinstance(additional, dict):
                validate_schema(child, additional, f"{path}.{key}")
            else:
                require(additional, f"{path}: unexpected {key}")
    if isinstance(value, list):
        require(schema.get("minItems", 0) <= len(value) <= schema.get("maxItems", float("inf")),
                f"{path}: wrong array length")
        if "items" in schema:
            for index, child in enumerate(value):
                validate_schema(child, schema["items"], f"{path}[{index}]")
    if isinstance(value, str):
        require(len(value) >= schema.get("minLength", 0), f"{path}: empty string")
        require("pattern" not in schema or re.search(schema["pattern"], value), f"{path}: invalid ID")
    if type(value) in (int, float):
        require(math.isfinite(value), f"{path}: non-finite number")
        require(schema.get("minimum", -float("inf")) <= value <= schema.get("maximum", float("inf")),
                f"{path}: numeric bounds")


def native_ids(root, relative_path, name):
    source = (Path(root) / relative_path).read_text(encoding="utf-8-sig")
    match = re.search(r"enum class " + re.escape(name) + r"\s*:\s*uint8\s*\{([^}]+)\}", source)
    require(match is not None, f"Missing native enum: {name}")
    result = {}
    next_id = 0
    for entry in match[1].split(","):
        entry = entry.strip()
        if not entry:
            continue
        identifier, assigned, number = entry.partition("=")
        identifier = identifier.strip()
        require(re.fullmatch(r"[A-Za-z_]\w*", identifier), f"{name}: unsupported enum entry {entry}")
        if assigned:
            require(re.fullmatch(r"\d+", number.strip()), f"{name}: unsupported assignment {entry}")
            next_id = int(number.strip())
        require(identifier not in result and next_id not in result.values(),
                f"{name}: duplicate native identity")
        require(0 <= next_id < 256, f"{name}: identity outside uint8")
        result[identifier] = next_id
        next_id += 1
    return result
