"""Independently replay a Demo ledger evidence export (schema 3): audit prefix + journal tail must equal the live state.

The export comes from LDEconomy::ExportEvidence. Every rule mirrors LDEconomy::ApplyEntry: one account per entry,
DeathReward = mint, Buy = burn + create, Sell = consume + mint, AccountRetire = retire only, two equipment slots,
instance identities that encode the match and issue serial, and gold that reconciles with initial + mint - burn.
DevFixture = mint from the DEV growth fixture (-LDDevGrowthFixture). Its grants are listed in devFixture for the whole
match; they are accepted only with --dev-fixture and only from an export whose process ran with the fixture.
"""

import argparse
import json
from pathlib import Path
import re
from uuid import UUID

SCHEMA_VERSION = 3
INT64_MIN, INT64_MAX = -(2**63), 2**63 - 1
UINT32_MAX, UINT64_MAX = 2**32 - 1, 2**64 - 1
EQUIPMENT_SLOTS = 2
JOURNAL_TAIL_CAPACITY = 64
CAUSES = ("DeathReward", "Buy", "Sell", "AccountRetire")
DEV_FIXTURE = "DevFixture"
DEFINITION = re.compile(r"[a-z][a-z0-9_]{0,62}")
UNSIGNED = re.compile(r"0|[1-9][0-9]*")
SIGNED = re.compile(r"0|-?[1-9][0-9]*")
TERMS = re.compile(r"[0-9a-f]{64}")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def field(row, name):
    require(isinstance(row, dict) and name in row, f"Missing ledger field: {name}")
    return row[name]


def rows(row, name):
    value = field(row, name)
    require(isinstance(value, list), f"{name} must be an array")
    return value


def optional(row, name):
    value = field(row, name)
    require(value is None or isinstance(value, dict), f"{name} must be an object or null")
    return value


def u64(value, label, minimum=0):
    require(isinstance(value, str) and UNSIGNED.fullmatch(value), f"{label} must be a decimal uint64 string")
    result = int(value)
    require(minimum <= result <= UINT64_MAX, f"Out-of-range {label}")
    return result


def i64(value, label, minimum=INT64_MIN):
    require(isinstance(value, str) and SIGNED.fullmatch(value), f"{label} must be a decimal int64 string")
    result = int(value)
    require(minimum <= result <= INT64_MAX, f"Out-of-range {label}")
    return result


def small(value, label, maximum):
    require(type(value) is int and 0 <= value <= maximum, f"Invalid integer: {label}")
    return value


def flag(value, label):
    require(type(value) is bool, f"Invalid boolean: {label}")
    return value


def guid(value, label):
    require(isinstance(value, str), f"Invalid identity: {label}")
    try:
        result = UUID(value)
    except ValueError as error:
        raise ValueError(f"Invalid identity: {label}") from error
    require(str(result) == value, f"Identity is not canonical lowercase: {label}")
    require(result.int != 0, f"Zero identity: {label}")
    return value


def definition(value, label):
    require(isinstance(value, str) and DEFINITION.fullmatch(value), f"Invalid definition: {label}")
    return value


def instance_for(match, serial):
    """LDEconomy::FLedger::InstanceFor: the match's A/B words and the 64-bit issue serial."""
    return str(UUID(int=(UUID(match).int >> 64 << 64) | serial))


def load_state(row, match, label):
    require(isinstance(row, dict), f"{label} must be an object")
    state = {
        "through": u64(field(row, "throughSerial"), f"{label}.throughSerial"),
        "issued": u64(field(row, "issuedThrough"), f"{label}.issuedThrough"),
        "initial": i64(field(row, "initialGoldTotal"), f"{label}.initialGoldTotal", 0),
        "minted": i64(field(row, "minted"), f"{label}.minted", 0),
        "burned": i64(field(row, "burned"), f"{label}.burned", 0),
        "accounts": {},
        "inventories": {},
        "items": {},
    }
    for account in rows(row, "accounts"):
        key = guid(field(account, "id"), f"{label} account")
        require(key not in state["accounts"], f"Duplicate account: {key}")
        retired = flag(field(account, "retired"), f"{label} account retired")
        record = {
            "gold": i64(field(account, "gold"), f"{label} account gold", 0),
            "revision": u64(field(account, "revision"), f"{label} account revision", 1),
            "retired": retired,
            "retireSerial": u64(field(account, "retireSerial"), f"{label} retireSerial"),
            "retireLife": small(field(account, "retireLife"), f"{label} retireLife", UINT32_MAX),
            "retireLifeRevision": u64(field(account, "retireLifeRevision"), f"{label} retireLifeRevision"),
        }
        if retired:
            require(1 <= record["retireSerial"] <= state["through"] and record["retireLife"] >= 1
                    and record["retireLifeRevision"] >= 1, f"Retired account lacks its retirement serial or leave stamp: {key}")
        else:
            require(record["retireSerial"] == 0 and record["retireLife"] == 0 and record["retireLifeRevision"] == 0,
                    f"Active account carries a retirement record: {key}")
        state["accounts"][key] = record
    for inventory in rows(row, "inventories"):
        key = guid(field(inventory, "owner"), f"{label} inventory owner")
        require(key not in state["inventories"], f"Duplicate inventory: {key}")
        state["inventories"][key] = u64(field(inventory, "revision"), f"{label} inventory revision", 1)
    require(set(state["inventories"]) == set(state["accounts"]), f"{label}: every account needs exactly one inventory revision")
    positions = set()
    for item in rows(row, "items"):
        key = guid(field(item, "instance"), f"{label} item")
        require(key not in state["items"], f"Duplicate item: {key}")
        serial = u64(field(item, "issueSerial"), f"{label} item issueSerial", 1)
        require(serial <= state["issued"], f"Item serial exceeds issuedThrough: {key}")
        require(key == instance_for(match, serial), f"Item identity does not encode its match and issue serial: {key}")
        record = {
            "serial": serial,
            "definition": definition(field(item, "definition"), f"{label} item definition"),
            "owner": guid(field(item, "owner"), f"{label} item owner"),
            "slot": small(field(item, "slot"), f"{label} item slot", EQUIPMENT_SLOTS - 1),
            "revision": u64(field(item, "revision"), f"{label} item revision", 1),
        }
        require(record["owner"] in state["accounts"], f"Item owner has no account: {key}")
        require((record["owner"], record["slot"]) not in positions, f"Two items share one equipment slot: {key}")
        positions.add((record["owner"], record["slot"]))
        state["items"][key] = record
    total = sum(account["gold"] for account in state["accounts"].values())
    require(total <= INT64_MAX, f"{label}: total gold overflows")
    require(total == state["initial"] + state["minted"] - state["burned"],
            f"{label}: gold does not reconcile with the initial total, mint and burn")
    return state


def load_fixture(data, dev_fixture):
    """The devFixture block: whether the exporting process ran with the fixture and every DevFixture mint by serial."""
    block = field(data, "devFixture")
    require(isinstance(block, dict), "devFixture must be an object")
    enabled = flag(field(block, "enabled"), "devFixture.enabled")
    grants, total = {}, 0
    for grant in rows(block, "grants"):
        serial = u64(field(grant, "serial"), "devFixture grant serial", 1)
        require(serial not in grants, f"DevFixture grant {serial} is listed twice")
        grants[serial] = (guid(field(grant, "account"), "devFixture grant account"),
                          i64(field(grant, "gold"), "devFixture grant gold", 1))
        total += grants[serial][1]
    require(total == i64(field(block, "minted"), "devFixture.minted", 0), "devFixture.minted is not the sum of its listed grants")
    require(enabled or not grants, "DevFixture grants are listed by a process without the DEV growth fixture")
    if dev_fixture:
        require(enabled, "--dev-fixture was given, but the evidence comes from a process without -LDDevGrowthFixture")
    else:
        require(not enabled, "The evidence comes from a run with the DEV growth fixture; only --dev-fixture accepts it")
    return grants, total


def apply_entry(state, entry, match, fixture_grants=None):
    """Mirror of LDEconomy::ApplyEntry in replay mode; fixture_grants is None unless the DEV fixture is accepted."""
    serial = u64(field(entry, "serial"), "serial")
    require(serial == state["through"] + 1, "Journal serial is not the next contiguous commit")
    cause = field(entry, "cause")
    require(cause in CAUSES or (cause == DEV_FIXTURE and fixture_grants is not None), f"Unknown cause: {cause}")
    mint = i64(field(entry, "mint"), "mint", 0)
    burn = i64(field(entry, "burn"), "burn", 0)
    gold, create, consume, retire = (optional(entry, name) for name in ("gold", "create", "consume", "retire"))
    delta = i64(field(gold, "delta"), "gold delta") if gold is not None else None
    if cause in ("DeathReward", DEV_FIXTURE):
        shape = gold is not None and create is None and consume is None and retire is None and delta > 0 and mint == delta and burn == 0
    elif cause == "Buy":
        shape = (gold is not None and create is not None and consume is None and retire is None and delta < 0
                 and burn == -delta and mint == 0 and field(create, "owner") == field(gold, "account"))
    elif cause == "Sell":
        shape = (gold is not None and consume is not None and create is None and retire is None and delta >= 0
                 and mint == delta and burn == 0 and field(consume, "owner") == field(gold, "account"))
    else:
        shape = retire is not None and gold is None and create is None and consume is None and mint == 0 and burn == 0
    require(shape, "Transaction shape does not match its cause")
    if cause == DEV_FIXTURE:
        require(fixture_grants.get(serial) == (field(gold, "account"), delta), f"DevFixture serial {serial} is not listed in devFixture")
    minted, burned = state["minted"] + mint, state["burned"] + burn
    require(minted <= INT64_MAX and burned <= INT64_MAX, "Ledger mint or burn total would overflow")

    accounts, inventories, items = state["accounts"], state["inventories"], state["items"]
    changed, new_account, result_account = None, None, 0
    if gold is not None:
        key = guid(field(gold, "account"), "gold account")
        account = accounts.get(key)
        require(account is not None, "Gold account is not part of this match")
        require(not account["retired"], "Gold account is retired")
        require(account["revision"] == u64(field(gold, "expectedRevision"), "gold expectedRevision"), "Gold account revision changed")
        new_gold = account["gold"] + delta
        require(INT64_MIN <= new_gold <= INT64_MAX and account["revision"] < UINT64_MAX, "Gold balance or account revision would overflow")
        require(new_gold >= 0, "Gold balance is insufficient")
        result_account = account["revision"] + 1
        changed, new_account = key, dict(account, gold=new_gold, revision=result_account)

    inventory_owner, new_inventory, created_serial, created_instance, new_item = None, 0, 0, None, None
    if create is not None:
        owner = guid(field(create, "owner"), "create owner")
        require(owner in accounts and owner in inventories, "Equipment owner is not part of this match")
        require(not accounts[owner]["retired"], "Equipment owner is retired")
        require(inventories[owner] == u64(field(create, "expectedInventoryRevision"), "create expectedInventoryRevision"),
                "Inventory revision changed")
        item_definition = definition(field(create, "definition"), "create definition")
        slot = small(field(create, "slot"), "create slot", 255)
        require(slot < EQUIPMENT_SLOTS, "Created equipment needs slot 0 or 1")
        held = [item for item in items.values() if item["owner"] == owner]
        require(len(held) < EQUIPMENT_SLOTS, "Both equipment slots are occupied")
        require(all(item["slot"] != slot for item in held), "The target equipment slot is occupied")
        require(state["issued"] < UINT64_MAX and inventories[owner] < UINT64_MAX, "Issue serial or inventory revision would overflow")
        created_serial = state["issued"] + 1
        created_instance = instance_for(match, created_serial)
        require(created_instance not in items, "Issued equipment identity is already live")
        inventory_owner, new_inventory = owner, inventories[owner] + 1
        new_item = {"serial": created_serial, "definition": item_definition, "owner": owner, "slot": slot, "revision": 1}

    consumed = None
    if consume is not None:
        key = guid(field(consume, "instance"), "consume instance")
        owner = guid(field(consume, "owner"), "consume owner")
        item = items.get(key)
        require(item is not None, "Equipment instance is not live")
        require(item["owner"] == owner, "Equipment instance belongs to another owner")
        require(item["revision"] == u64(field(consume, "expectedItemRevision"), "consume expectedItemRevision"), "Equipment item revision changed")
        require(owner in accounts and owner in inventories, "Equipment owner is not part of this match")
        require(not accounts[owner]["retired"], "Equipment owner is retired")
        require(inventories[owner] == u64(field(consume, "expectedInventoryRevision"), "consume expectedInventoryRevision"),
                "Inventory revision changed")
        require(inventories[owner] < UINT64_MAX, "Inventory revision would overflow")
        inventory_owner, new_inventory, consumed = owner, inventories[owner] + 1, key

    if retire is not None:
        key = guid(field(retire, "account"), "retire account")
        account = accounts.get(key)
        require(account is not None, "Retired account is not part of this match")
        require(not account["retired"], "Account is already retired")
        require(account["revision"] == u64(field(retire, "expectedRevision"), "retire expectedRevision"),
                "Account revision changed before retirement")
        life = small(field(retire, "life"), "retire life", UINT32_MAX)
        life_revision = u64(field(retire, "lifeRevision"), "retire lifeRevision")
        require(life != 0 and life_revision != 0 and account["revision"] < UINT64_MAX, "Retirement needs the core leave stamp")
        result_account = account["revision"] + 1
        changed = key
        new_account = dict(account, retired=True, revision=result_account, retireSerial=serial,
                           retireLife=life, retireLifeRevision=life_revision)

    total = sum(new_account["gold"] if key == changed else account["gold"] for key, account in accounts.items())
    require(total <= INT64_MAX, "Total gold would overflow")
    require(total == state["initial"] + minted - burned, "Gold does not reconcile with the initial total, mint and burn")
    recorded_instance = field(entry, "createdInstance")
    require(recorded_instance is None or isinstance(recorded_instance, str), "createdInstance must be a string or null")
    if recorded_instance is not None:
        guid(recorded_instance, "createdInstance")
    require(u64(field(entry, "resultAccountRevision"), "resultAccountRevision") == result_account
            and u64(field(entry, "resultInventoryRevision"), "resultInventoryRevision") == new_inventory
            and recorded_instance == created_instance
            and u64(field(entry, "createdIssueSerial"), "createdIssueSerial") == created_serial,
            "Recorded journal results differ from the independent replay")

    if changed is not None:
        accounts[changed] = new_account
    if inventory_owner is not None:
        inventories[inventory_owner] = new_inventory
    if new_item is not None:
        items[created_instance] = new_item
        state["issued"] = created_serial
    if consumed is not None:
        del items[consumed]
    state["minted"], state["burned"], state["through"] = minted, burned, serial


def compare(replayed, current):
    for name, label in (("through", "committed serial"), ("issued", "issued equipment serial"), ("initial", "initial gold total"),
                        ("minted", "mint total"), ("burned", "burn total"), ("accounts", "accounts"),
                        ("inventories", "inventory revisions"), ("items", "live equipment")):
        require(replayed[name] == current[name], f"Replayed {label} differ from the exported live state")


def verify_data(data, dev_fixture=False):
    require(isinstance(data, dict), "Evidence must be a JSON object")
    require(type(field(data, "schemaVersion")) is int and data["schemaVersion"] == SCHEMA_VERSION, "Unsupported ledger evidence schema")
    match = guid(field(data, "match"), "match")
    fixture_grants, fixture_minted = load_fixture(data, dev_fixture)
    starting_gold = i64(field(data, "startingGold"), "startingGold", 0)
    prefix = load_state(field(data, "prefix"), match, "prefix")
    current = load_state(field(data, "current"), match, "current")
    require(prefix["initial"] == starting_gold * len(prefix["accounts"]),
            "The initial gold total is not the approved starting gold for every roster account")
    tail = rows(data, "tail")
    require(len(tail) <= JOURNAL_TAIL_CAPACITY, "The journal tail exceeds its capacity")
    keys, batch = set(), 0
    for entry in tail:
        key = guid(field(entry, "businessKey"), "businessKey")
        require(key not in keys, f"Duplicate business key in the journal tail: {key}")
        keys.add(key)
        require(isinstance(field(entry, "terms"), str) and TERMS.fullmatch(entry["terms"]), "terms must be 64 lowercase hex digits")
        entry_batch = u64(field(entry, "commitBatch"), "commitBatch", 1)
        require(entry_batch >= batch, "Commit batches go backwards in the journal tail")
        batch = entry_batch
        apply_entry(prefix, entry, match, fixture_grants if dev_fixture else None)
    compare(prefix, current)
    require(all(serial <= current["through"] and account in current["accounts"] for serial, (account, _) in fixture_grants.items()),
            "A listed DevFixture grant lies beyond the committed serial or names no account")
    require(fixture_minted <= current["minted"], "Listed DevFixture grants exceed the ledger mint total")
    result = {
        "tail": len(tail),
        "throughSerial": current["through"],
        "accounts": len(current["accounts"]),
        "items": len(current["items"]),
        "gold": sum(account["gold"] for account in current["accounts"].values()),
    }
    if dev_fixture:
        result.update(devFixtureGrants=len(fixture_grants), devFixtureMinted=fixture_minted)
    return result


def verify(path, dev_fixture=False):
    result = verify_data(json.loads(Path(path).read_text(encoding="utf-8-sig")), dev_fixture)
    fixture = (f"; DEV fixture: {result['devFixtureGrants']} grants minting {result['devFixtureMinted']} gold"
               if dev_fixture else "")
    print(f"PASS {path}: {result['tail']} tail entries through serial {result['throughSerial']}, "
          f"{result['accounts']} accounts, {result['items']} live items, {result['gold']} gold{fixture}")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reports", nargs="+", type=Path)
    parser.add_argument("--dev-fixture", action="store_true",
                        help="the run used the DEV growth fixture (-LDDevGrowthFixture); accept and report its DevFixture mints")
    args = parser.parse_args()
    for report in args.reports:
        try:
            verify(report, args.dev_fixture)
        except (OSError, ValueError) as error:
            parser.exit(1, f"Invalid ledger evidence {report}: {error}\n")