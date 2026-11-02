import copy
import unittest

from CheckEconomyEvidence import verify_data


def guid(value):
    return f"00000000-0000-0000-0000-{value:012x}"


def transition(item, operation, source="None", target="Owner", expected=0, parents=(), quality=0):
    return {
        "id": guid(item), "op": operation, "kind": 3, "expected": expected,
        "from": source, "to": target, "parents": [guid(parent) for parent in parents],
        "quality": quality, "skill": 0,
    }


def receipt(identity, serial, items=()):
    return {
        "id": guid(identity), "serial": str(serial), "cause": "Fixture",
        "mint": "0", "burn": "0", "gold": [], "budget": "None",
        "units": 0, "additionalBudgets": [], "items": list(items),
    }


def fixture():
    create = receipt(11, 1, [transition(0xA101, 0)])
    replace = receipt(12, 2, [
        transition(0xA101, 2, "Owner", "None", 1),
        transition(0xA102, 0, parents=[0xA101]),
    ])
    replace.update(burn="2", gold=[{"account": "Owner", "expected": 1, "delta": "-2"}],
                   budget="Global", units=1, additionalBudgets=[{"id": "OwnerBound", "units": 1}])
    revise = receipt(13, 3, [transition(0xA102, 3, "Owner", "Owner", 1, quality=1)])
    return {
        "schemaVersion": 1, "match": guid(1), "start": "10",
        "minted": "0", "burned": "2", "end": "8",
        "accounts": [
            {"id": "Owner", "endowment": "10", "gold": "8", "revision": 2, "retired": False},
            {"id": "Dormant", "endowment": "0", "gold": "0", "revision": 2, "retired": True},
        ],
        "budgets": [{"id": "Global", "initial": 2, "remaining": 1},
                    {"id": "OwnerBound", "initial": 1, "remaining": 0}],
        "items": [
            {"id": guid(0xA101), "kind": 3, "location": "None", "consumed": True,
             "quality": 0, "skill": 0, "origin": guid(11), "parents": []},
            {"id": guid(0xA102), "kind": 3, "location": "Owner", "consumed": False,
             "quality": 1, "skill": 0, "origin": guid(12), "parents": [guid(0xA101)]},
        ],
        "receipts": [create, replace, revise],
    }


class EconomyEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.data = fixture()

    def test_replays_real_export_shape(self):
        self.assertEqual(verify_data(self.data), {"receipts": 3, "accounts": 2, "items": 2, "gold": 8})

    def test_parent_is_validated_before_same_receipt_consumption(self):
        self.data["receipts"][1]["items"].reverse()
        self.assertEqual(verify_data(self.data)["items"], 2)

    def test_empty_initialized_domain(self):
        self.data.update(start="0", minted="0", burned="0", end="0",
                         accounts=[], budgets=[], items=[], receipts=[])
        self.assertEqual(verify_data(self.data)["receipts"], 0)

    def test_duplicate_account_budget_and_item_rejected(self):
        for key in ("accounts", "budgets", "items"):
            with self.subTest(key=key):
                data = copy.deepcopy(self.data)
                duplicate = copy.deepcopy(data[key][0])
                duplicate["id"] = duplicate["id"].upper()
                data[key].append(duplicate)
                with self.assertRaisesRegex(ValueError, "Duplicate"):
                    verify_data(data)

    def test_missing_fields_never_supply_zero_defaults(self):
        for key in ("quality", "skill", "origin", "parents"):
            with self.subTest(key=key):
                data = copy.deepcopy(self.data)
                del data["items"][0][key]
                with self.assertRaisesRegex(ValueError, "Missing ledger field"):
                    verify_data(data)

    def test_bad_schema_and_identity(self):
        for value in (0, 2, True, None):
            with self.subTest(schema=value):
                data = copy.deepcopy(self.data)
                data["schemaVersion"] = value
                with self.assertRaises(ValueError):
                    verify_data(data)
        for value in (guid(0), "not-a-guid", None):
            with self.subTest(match=value):
                data = copy.deepcopy(self.data)
                data["match"] = value
                with self.assertRaises(ValueError):
                    verify_data(data)

    def test_receipts_cannot_repeat_or_reorder(self):
        for key, value in (("id", guid(11)), ("serial", "1")):
            with self.subTest(key=key):
                data = copy.deepcopy(self.data)
                data["receipts"][1][key] = value
                with self.assertRaisesRegex(ValueError, "Receipt order or identity"):
                    verify_data(data)

    def test_amount_shape_and_bounds(self):
        for value in ("-1", "1.5", "0.0", "1e3", 0, False, str(2**63)):
            with self.subTest(value=value):
                data = copy.deepcopy(self.data)
                data["receipts"][1]["mint"] = value
                with self.assertRaises(ValueError):
                    verify_data(data)

    def test_duplicate_or_stale_account_delta(self):
        duplicate = copy.deepcopy(self.data)
        duplicate["receipts"][1]["gold"].append(copy.deepcopy(duplicate["receipts"][1]["gold"][0]))
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            verify_data(duplicate)
        self.data["receipts"][1]["gold"][0]["expected"] = 2
        with self.assertRaisesRegex(ValueError, "Stale account"):
            verify_data(self.data)

    def test_insufficient_and_unclassified_gold(self):
        for change in ("-11", "-1", "1"):
            with self.subTest(delta=change):
                data = copy.deepcopy(self.data)
                data["receipts"][1]["gold"][0]["delta"] = change
                with self.assertRaises(ValueError):
                    verify_data(data)

    def test_aggregate_and_retirement_totals(self):
        for key in ("start", "end", "minted", "burned"):
            with self.subTest(total=key):
                data = copy.deepcopy(self.data)
                data[key] = str(int(data[key]) + 1)
                with self.assertRaises(ValueError):
                    verify_data(data)
        self.data["accounts"][1]["retired"] = False
        with self.assertRaisesRegex(ValueError, "revision mismatch"):
            verify_data(self.data)

    def test_bounds_are_atomic_and_not_duplicated(self):
        exhausted = copy.deepcopy(self.data)
        exhausted["budgets"][1]["initial"] = 0
        with self.assertRaisesRegex(ValueError, "exhausted budget"):
            verify_data(exhausted)
        self.data["receipts"][1]["additionalBudgets"][0]["id"] = "Global"
        with self.assertRaisesRegex(ValueError, "Duplicate budget"):
            verify_data(self.data)

    def test_budget_identity_matches_units(self):
        self.data["receipts"][0]["budget"] = "Global"
        with self.assertRaisesRegex(ValueError, "identity and units"):
            verify_data(self.data)

    def test_duplicate_missing_or_stale_item(self):
        for mutation in ("duplicate", "missing", "stale"):
            with self.subTest(mutation=mutation):
                data = copy.deepcopy(self.data)
                change = data["receipts"][1]["items"][0]
                if mutation == "duplicate":
                    data["receipts"][1]["items"].append(copy.deepcopy(change))
                elif mutation == "missing":
                    change["id"] = guid(999)
                else:
                    change["expected"] = 2
                with self.assertRaises(ValueError):
                    verify_data(data)

    def test_output_requires_current_consumed_parents(self):
        for parents in ([guid(999)], [guid(0xA101), guid(0xA101)]):
            with self.subTest(parents=parents):
                data = copy.deepcopy(self.data)
                data["receipts"][1]["items"][1]["parents"] = parents
                with self.assertRaises(ValueError):
                    verify_data(data)
        self.data["receipts"][1]["items"][0]["op"] = 1
        self.data["receipts"][1]["items"][0]["to"] = "World"
        with self.assertRaisesRegex(ValueError, "unconsumed parent"):
            verify_data(self.data)

    def test_invalid_operation_and_destination(self):
        for operation, target in ((99, "None"), (1, "Owner"), (2, "Owner")):
            with self.subTest(operation=operation):
                data = copy.deepcopy(self.data)
                data["receipts"][1]["items"][0].update(op=operation, to=target)
                with self.assertRaises(ValueError):
                    verify_data(data)

    def test_quality_and_origin_are_preserved(self):
        for quality in (0, 2, 3):
            with self.subTest(quality=quality):
                data = copy.deepcopy(self.data)
                data["receipts"][2]["items"][0]["quality"] = quality
                with self.assertRaises(ValueError):
                    verify_data(data)
        self.data["items"][1]["origin"] = guid(11)
        with self.assertRaisesRegex(ValueError, "state or origin"):
            verify_data(self.data)

    def test_existing_transient_operation_still_replays(self):
        transfer = receipt(20, 1, [transition(501, 4), transition(502, 0, parents=[501])])
        transfer["cause"] = "EquipmentWorkOrder"
        self.data.update(receipts=[transfer], budgets=[], start="10", end="10", minted="0", burned="0",
                         accounts=[{"id": "Owner", "endowment": "10", "gold": "10", "revision": 1, "retired": False}],
                         items=[
                             {"id": guid(501), "kind": 3, "location": "None", "consumed": True, "quality": 0,
                              "skill": 0, "origin": guid(20), "parents": []},
                             {"id": guid(502), "kind": 3, "location": "Owner", "consumed": False, "quality": 0,
                              "skill": 0, "origin": guid(20), "parents": [guid(501)]},
                         ])
        self.assertEqual(verify_data(self.data)["items"], 2)
        transfer["cause"] = "InvalidTransientCause"
        with self.assertRaisesRegex(ValueError, "transient"):
            verify_data(self.data)


if __name__ == "__main__":
    unittest.main()
