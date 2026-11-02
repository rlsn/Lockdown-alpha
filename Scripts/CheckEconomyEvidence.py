"""Independently replay the current FLedger export, without legacy market policy."""

import argparse
import json
from pathlib import Path
import re
from uuid import UUID

MAX_GOLD = 2**63 - 1
MAX_REVISION = 2**32 - 1


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


def integer(value, name, minimum=0, maximum=MAX_REVISION):
    if isinstance(value, float) and value.is_integer():
        value = int(value)
    require(type(value) is int and minimum <= value <= maximum, f"Invalid integer: {name}")
    return value


def amount(value, name, minimum=0):
    require(isinstance(value, str) and re.fullmatch(r"-?(0|[1-9][0-9]*)", value), f"{name} must be a decimal int64 string")
    result = int(value)
    require(minimum <= result <= MAX_GOLD, f"Out-of-range amount: {name}")
    return result


def name(value, label, allow_none=False):
    require(isinstance(value, str) and value, f"Invalid name: {label}")
    require(allow_none or value.casefold() != "none", f"Missing name: {label}")
    return value.casefold()


def identity(value, label):
    require(isinstance(value, str), f"Invalid identity: {label}")
    try:
        result = UUID(value)
    except ValueError as error:
        raise ValueError(f"Invalid identity: {label}") from error
    require(result.int != 0, f"Zero identity: {label}")
    return str(result)


def unique(records, key, label):
    result = {}
    for record in records:
        value = key(record)
        require(value not in result, f"Duplicate {label}: {value}")
        result[value] = record
    return result


def item_state(row):
    require(type(field(row, "consumed")) is bool, "Invalid item consumed flag")
    return {
        "kind": integer(field(row, "kind"), "item kind", maximum=255),
        "location": name(field(row, "location"), "item location", True),
        "consumed": row["consumed"],
        "quality": integer(field(row, "quality"), "item quality", maximum=2),
        "skill": integer(field(row, "skill"), "item skill", maximum=255),
        "origin": identity(field(row, "origin"), "item origin"),
        "parents": [identity(parent, "item parent") for parent in rows(row, "parents")],
    }


def verify_data(data):
    require(integer(field(data, "schemaVersion"), "schemaVersion") == 1, "Unsupported ledger schema")
    identity(field(data, "match"), "match")
    final_accounts = unique(rows(data, "accounts"), lambda row: name(field(row, "id"), "account"), "account")
    final_budgets = unique(rows(data, "budgets"), lambda row: name(field(row, "id"), "budget"), "budget")
    final_items = unique(rows(data, "items"), lambda row: identity(field(row, "id"), "item"), "item")
    accounts = {key: amount(field(row, "endowment"), "endowment") for key, row in final_accounts.items()}
    revisions = {key: 1 for key in accounts}
    budgets = {key: integer(field(row, "initial"), "initial budget", maximum=2**31 - 1) for key, row in final_budgets.items()}
    start = amount(field(data, "start"), "start")
    require(sum(accounts.values()) == start, "Starting endowments do not reconcile")
    items, item_revisions, seen = {}, {}, set()
    mint = burn = 0
    for serial, receipt in enumerate(rows(data, "receipts"), 1):
        receipt_id = identity(field(receipt, "id"), "receipt")
        require(amount(field(receipt, "serial"), "serial") == serial and receipt_id not in seen, "Receipt order or identity reused")
        seen.add(receipt_id)
        cause = name(field(receipt, "cause"), "cause")
        issued, spent = amount(field(receipt, "mint"), "mint"), amount(field(receipt, "burn"), "burn")
        require(mint + issued <= MAX_GOLD and burn + spent <= MAX_GOLD and start + mint + issued <= MAX_GOLD, "Cumulative classification overflow")
        debits = unique(rows(receipt, "gold"), lambda row: name(field(row, "account"), "account"), "account delta")
        net = 0
        for account, delta in debits.items():
            require(account in accounts, "Unregistered account")
            expected = integer(field(delta, "expected"), "account expected revision", minimum=1)
            change = amount(field(delta, "delta"), "gold delta", -(2**63))
            require(expected == revisions[account] < MAX_REVISION, "Stale account revision")
            net += change
            require(-(2**63) <= net <= MAX_GOLD, "Gold delta overflow")
            require(0 <= accounts[account] + change <= MAX_GOLD, "Insufficient or overflowing account")
            accounts[account] += change
            revisions[account] += 1
        require(net == issued - spent, "Gold deltas do not match mint/burn")
        mint += issued
        burn += spent
        require(0 <= sum(accounts.values()) <= MAX_GOLD, "Aggregate gold overflow")
        budget = name(field(receipt, "budget"), "budget", True)
        units = integer(field(receipt, "units"), "budget units", maximum=2**31 - 1)
        require((budget == "none") == (units == 0), "Budget identity and units disagree")
        bounds = [] if not units else [(budget, units)]
        for debit in rows(receipt, "additionalBudgets"):
            bounds.append((name(field(debit, "id"), "additional budget"), integer(field(debit, "units"), "additional units", minimum=1, maximum=2**31 - 1)))
        require(len({key for key, _ in bounds}) == len(bounds), "Duplicate budget debit")
        for key, count in bounds:
            require(key in budgets and budgets[key] >= count, "Missing or exhausted budget")
            budgets[key] -= count
        changes = unique(rows(receipt, "items"), lambda row: identity(field(row, "id"), "item delta"), "item delta")
        prior_live = {key for key, item in items.items() if not item["consumed"]}
        for delta in changes.values():
            integer(field(delta, "op"), "item operation", maximum=4)
        consumed = {key for key, delta in changes.items() if delta["op"] in (2, 4)}
        transient = {key for key, delta in changes.items() if delta["op"] == 4}
        for key, delta in changes.items():
            operation = delta["op"]
            kind = integer(field(delta, "kind"), "item kind", maximum=255)
            expected = integer(field(delta, "expected"), "item expected revision")
            quality = integer(field(delta, "quality"), "item quality", maximum=255)
            skill = integer(field(delta, "skill"), "item skill", maximum=255)
            source = name(field(delta, "from"), "item source", True)
            target = name(field(delta, "to"), "item destination", True)
            parents = [identity(parent, "parent") for parent in rows(delta, "parents")]
            if operation in (0, 4):
                require(key not in items and expected == 0 and source == "none" and target != "none" and quality <= 2, "Invalid item creation")
                require(len(set(parents)) == len(parents), "Duplicate parent")
                require(all(parent in consumed and (parent in prior_live or parent in transient) for parent in parents), "Missing or unconsumed parent")
                if operation == 4:
                    require(not parents and quality == 0 and cause == "equipmentworkorder", "Invalid transient operation")
                items[key] = {"kind": kind, "location": target if operation == 0 else "none", "consumed": operation == 4, "quality": quality, "skill": skill, "origin": receipt_id, "parents": parents}
                item_revisions[key] = 1
            else:
                require(key in items and not items[key]["consumed"] and not parents, "Unknown or consumed item")
                item = items[key]
                require(expected == item_revisions[key] < MAX_REVISION and item["kind"] == kind and item["location"] == source, "Item ownership or revision changed")
                if operation == 1:
                    require(target != "none" and target != source, "Invalid transfer destination")
                elif operation == 2:
                    require(target == "none", "Consumed item retains a destination")
                else:
                    require(target == source and quality == item["quality"] + 1 <= 2, "Invalid quality revision")
                    item["quality"] = quality
                item["location"] = target
                item["consumed"] = operation == 2
                item_revisions[key] += 1
    for key, account in final_accounts.items():
        require(type(field(account, "retired")) is bool, "Invalid account retirement flag")
        require(accounts[key] == amount(field(account, "gold"), "final gold"), "Final account mismatch")
        require(revisions[key] + int(account["retired"]) == integer(field(account, "revision"), "final account revision", minimum=1), "Final account revision mismatch")
    require(sum(accounts.values()) == start + mint - burn == amount(field(data, "end"), "end"), "Total gold does not reconcile")
    require((mint, burn) == (amount(field(data, "minted"), "minted"), amount(field(data, "burned"), "burned")), "Classification totals differ")
    require(set(items) == set(final_items), "Final item identity set differs")
    for key, row in final_items.items():
        require(items[key] == item_state(row), "Final item state or origin differs")
        require(all(parent in items and items[parent]["consumed"] for parent in items[key]["parents"]), "Final parent history differs")
    require(budgets == {key: integer(field(row, "remaining"), "remaining budget", maximum=2**31 - 1) for key, row in final_budgets.items()}, "Final budget state differs")
    return {"receipts": len(seen), "accounts": len(accounts), "items": len(items), "gold": sum(accounts.values())}


def verify(path):
    result = verify_data(json.loads(Path(path).read_text(encoding="utf-8-sig")))
    print(f"PASS {path}: {result['receipts']} receipts, {result['accounts']} accounts, {result['items']} unique items, {result['gold']} gold")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reports", nargs="+", type=Path)
    args = parser.parse_args()
    for report in args.reports:
        try:
            verify(report)
        except (OSError, ValueError) as error:
            parser.exit(1, f"Invalid ledger evidence {report}: {error}\n")
