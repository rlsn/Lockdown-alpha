import copy
import unittest

from CheckEconomyEvidence import verify_data

MATCH = "11111111-2222-3333-4444-555566667777"
DEFINITION = "item_edge_blade"
TERMS = "ab" * 32


def account_id(value):
    return f"000000e1-0000-0000-0000-{value:012x}"


P1, P2, P3 = account_id(1), account_id(2), account_id(3)


def instance(serial):
    # LDEconomy::FLedger::InstanceFor(Match, Serial): the match's first eight bytes, then the 64-bit serial.
    return f"11111111-2222-3333-{serial >> 48:04x}-{serial & 0xFFFFFFFFFFFF:012x}"


def account(gold, revision, retired=False, retire_serial=0, life=0, life_revision=0):
    return {"gold": str(gold), "revision": str(revision), "retired": retired, "retireSerial": str(retire_serial),
            "retireLife": life, "retireLifeRevision": str(life_revision)}


def state(through, issued, minted, burned, accounts, inventories, items):
    return {
        "throughSerial": str(through), "issuedThrough": str(issued), "initialGoldTotal": "450",
        "minted": str(minted), "burned": str(burned),
        "accounts": [dict(account_row, id=key) for key, account_row in accounts.items()],
        "inventories": [{"owner": key, "revision": str(value)} for key, value in inventories.items()],
        "items": [{"instance": instance(serial), "issueSerial": str(serial), "definition": DEFINITION, "owner": owner,
                   "slot": slot, "revision": "1"} for serial, owner, slot in items],
    }


def entry(serial, cause, gold=None, create=None, consume=None, retire=None, mint=0, burn=0,
          result_account=0, result_inventory=0, created=0):
    return {
        "serial": str(serial), "cause": cause, "businessKey": f"6b657900-0000-0000-0000-{200 + serial:012x}",
        "terms": TERMS, "commitBatch": str(200 + serial), "mint": str(mint), "burn": str(burn),
        "gold": None if gold is None else {"account": gold[0], "expectedRevision": str(gold[1]), "delta": str(gold[2])},
        "create": None if create is None else {"owner": create[0], "expectedInventoryRevision": str(create[1]),
                                                "definition": DEFINITION, "slot": create[2]},
        "consume": None if consume is None else {"instance": instance(consume[0]), "expectedItemRevision": "1",
                                                  "owner": consume[1], "expectedInventoryRevision": str(consume[2])},
        "retire": None if retire is None else {"account": retire[0], "expectedRevision": str(retire[1]),
                                                "life": retire[2], "lifeRevision": str(retire[3])},
        "resultAccountRevision": str(result_account), "resultInventoryRevision": str(result_inventory),
        "createdInstance": instance(created) if created else None, "createdIssueSerial": str(created),
    }


def history():
    return [
        entry(1, "Buy", gold=(P2, 1, -100), create=(P2, 1, 0), burn=100, result_account=2, result_inventory=2, created=1),
        entry(2, "AccountRetire", retire=(P3, 1, 2, 7), result_account=2),
        entry(3, "DeathReward", gold=(P1, 1, 80), mint=80, result_account=2),
        entry(4, "Buy", gold=(P1, 2, -100), create=(P1, 1, 0), burn=100, result_account=3, result_inventory=2, created=2),
        entry(5, "Buy", gold=(P1, 3, -100), create=(P1, 2, 1), burn=100, result_account=4, result_inventory=3, created=3),
        entry(6, "Sell", gold=(P1, 4, 65), consume=(2, P1, 3), mint=65, result_account=5, result_inventory=4),
        entry(7, "Sell", gold=(P2, 2, 65), consume=(1, P2, 2), mint=65, result_account=3, result_inventory=3),
        entry(8, "AccountRetire", retire=(P2, 3, 1, 3), result_account=4),
    ]


def initial_state():
    return state(0, 0, 0, 0, {P1: account(150, 1), P2: account(150, 1), P3: account(150, 1)}, {P1: 1, P2: 1, P3: 1}, [])


def compacted_prefix():
    return state(2, 1, 0, 100, {P1: account(150, 1), P2: account(50, 2), P3: account(150, 2, True, 2, 2, 7)},
                 {P1: 1, P2: 2, P3: 1}, [(1, P2, 0)])


def live_state():
    return state(8, 3, 210, 300,
                 {P1: account(95, 5), P2: account(115, 4, True, 8, 1, 3), P3: account(150, 2, True, 2, 2, 7)},
                 {P1: 4, P2: 3, P3: 1}, [(3, P1, 1)])


def fixture():
    return {"schemaVersion": 2, "match": MATCH, "startingGold": "150", "prefix": compacted_prefix(),
            "tail": history()[2:], "current": live_state()}


class EconomyEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.data = fixture()

    def rejects(self, data, pattern=None):
        if pattern is None:
            with self.assertRaises(ValueError):
                verify_data(data)
        else:
            with self.assertRaisesRegex(ValueError, pattern):
                verify_data(data)

    def test_replays_compacted_prefix_and_every_cause(self):
        self.assertEqual(verify_data(self.data), {"tail": 6, "throughSerial": 8, "accounts": 3, "items": 1, "gold": 360})

    def test_replays_uncompacted_history(self):
        self.data.update(prefix=initial_state(), tail=history())
        self.assertEqual(verify_data(self.data)["tail"], 8)

    def test_empty_tail_must_equal_prefix(self):
        self.data.update(tail=[], current=compacted_prefix())
        self.assertEqual(verify_data(self.data)["throughSerial"], 2)
        self.data["current"] = live_state()
        self.rejects(self.data, "differ from the exported live state")

    def test_missing_fields_never_default(self):
        for path in (("tail", 0, "gold"), ("tail", 1, "createdInstance"), ("prefix", "accounts", 0, "retireLife"),
                     ("current", "items", 0, "slot"), ("current", "minted"), ("startingGold",)):
            with self.subTest(path=path):
                data = copy.deepcopy(self.data)
                parent = data
                for step in path[:-1]:
                    parent = parent[step]
                del parent[path[-1]]
                self.rejects(data, "Missing ledger field")

    def test_schema_match_and_identity_spelling(self):
        for value in (1, 3, True, "2", None):
            with self.subTest(schema=value):
                data = copy.deepcopy(self.data)
                data["schemaVersion"] = value
                self.rejects(data, "schema")
        for value in ("00000000-0000-0000-0000-000000000000", "ABCDEF01-2222-3333-4444-555566667777", "not-a-guid", None):
            with self.subTest(match=value):
                data = copy.deepcopy(self.data)
                data["match"] = value
                self.rejects(data)

    def test_amount_shapes(self):
        for value in ("1.5", "080", "-0", 80, "8e1", str(2**63), "+80"):
            with self.subTest(value=value):
                data = copy.deepcopy(self.data)
                data["tail"][0]["gold"]["delta"] = value
                data["tail"][0]["mint"] = value
                self.rejects(data)

    def test_serials_cannot_gap_or_reorder(self):
        gap = copy.deepcopy(self.data)
        gap["tail"][0]["serial"] = "4"
        self.rejects(gap, "contiguous")
        self.data["tail"][1], self.data["tail"][2] = self.data["tail"][2], self.data["tail"][1]
        self.rejects(self.data)

    def test_business_keys_terms_and_batches(self):
        duplicate = copy.deepcopy(self.data)
        duplicate["tail"][1]["businessKey"] = duplicate["tail"][0]["businessKey"]
        self.rejects(duplicate, "Duplicate business key")
        for terms in ("AB" * 32, "ab" * 31, None):
            with self.subTest(terms=terms):
                data = copy.deepcopy(self.data)
                data["tail"][0]["terms"] = terms
                self.rejects(data, "terms")
        backwards = copy.deepcopy(self.data)
        backwards["tail"][1]["commitBatch"] = "1"
        self.rejects(backwards, "backwards")
        self.data["tail"][0]["commitBatch"] = "0"
        self.rejects(self.data, "commitBatch")

    def test_cause_shapes(self):
        mutations = {
            "negative reward": lambda tail: tail[0]["gold"].update(delta="-80"),
            "reward with burn": lambda tail: tail[0].update(burn="80"),
            "buy without create": lambda tail: tail[1].update(create=None),
            "buy for another owner": lambda tail: tail[1]["create"].update(owner=P2),
            "sell with burn": lambda tail: tail[3].update(burn="65"),
            "sell without consume": lambda tail: tail[3].update(consume=None),
            "retire with gold": lambda tail: tail[5].update(gold={"account": P2, "expectedRevision": "3", "delta": "0"}),
            "unknown cause": lambda tail: tail[0].update(cause="Craft"),
        }
        for label, mutate in mutations.items():
            with self.subTest(label=label):
                data = copy.deepcopy(self.data)
                mutate(data["tail"])
                self.rejects(data, "shape|Unknown cause")

    def test_stale_revisions(self):
        for label, mutate, pattern in (
                ("account", lambda tail: tail[0]["gold"].update(expectedRevision="2"), "account revision changed"),
                ("inventory", lambda tail: tail[1]["create"].update(expectedInventoryRevision="2"), "Inventory revision changed"),
                ("item", lambda tail: tail[3]["consume"].update(expectedItemRevision="2"), "item revision changed"),
                ("retirement", lambda tail: tail[5]["retire"].update(expectedRevision="2"), "before retirement")):
            with self.subTest(label=label):
                data = copy.deepcopy(self.data)
                mutate(data["tail"])
                self.rejects(data, pattern)

    def test_insufficient_gold(self):
        self.data["tail"][1]["gold"]["delta"] = "-1000"
        self.data["tail"][1]["burn"] = "1000"
        self.rejects(self.data, "insufficient")

    def test_equipment_slots(self):
        occupied = copy.deepcopy(self.data)
        occupied["tail"][2]["create"]["slot"] = 0
        self.rejects(occupied, "slot is occupied")
        bad_slot = copy.deepcopy(self.data)
        bad_slot["tail"][2]["create"]["slot"] = 2
        self.rejects(bad_slot, "slot 0 or 1")
        third = self.data["tail"][3]
        third.update(cause="Buy", mint="0", burn="10", consume=None,
                     gold={"account": P1, "expectedRevision": "4", "delta": "-10"},
                     create={"owner": P1, "expectedInventoryRevision": "3", "definition": DEFINITION, "slot": 0})
        self.rejects(self.data, "Both equipment slots")

    def test_sale_needs_the_live_owned_instance(self):
        for serial, pattern in ((3, "another owner"), (9, "not live")):
            with self.subTest(serial=serial):
                data = copy.deepcopy(self.data)
                data["tail"][4]["consume"]["instance"] = instance(serial)
                self.rejects(data, pattern)

    def test_retired_accounts_cannot_change(self):
        self.data["tail"][0]["gold"].update(account=P3, expectedRevision="2")
        self.rejects(self.data, "retired")

    def test_recorded_results_must_match_replay(self):
        for key, value in (("resultAccountRevision", "4"), ("resultInventoryRevision", "3"),
                           ("createdInstance", instance(5)), ("createdIssueSerial", "5")):
            with self.subTest(key=key):
                data = copy.deepcopy(self.data)
                data["tail"][1][key] = value
                self.rejects(data, "Recorded journal results")

    def test_live_state_must_match_replay(self):
        revision = copy.deepcopy(self.data)
        revision["current"]["accounts"][0]["revision"] = "6"
        self.rejects(revision, "Replayed accounts")
        inventory = copy.deepcopy(self.data)
        inventory["current"]["inventories"][0]["revision"] = "5"
        self.rejects(inventory, "Replayed inventory")
        gold = copy.deepcopy(self.data)
        gold["current"]["accounts"][0]["gold"] = "96"
        self.rejects(gold, "reconcile")

    def test_item_identity_and_positions(self):
        encoded = copy.deepcopy(self.data)
        encoded["current"]["items"][0]["instance"] = instance(4)
        self.rejects(encoded, "does not encode")
        shared = copy.deepcopy(self.data)
        shared["current"]["items"].append(dict(shared["current"]["items"][0], instance=instance(2), issueSerial="2"))
        self.rejects(shared, "share one equipment slot")
        beyond = copy.deepcopy(self.data)
        beyond["current"]["issuedThrough"] = "2"
        self.rejects(beyond, "issuedThrough")

    def test_duplicates_and_missing_inventories(self):
        for key in ("accounts", "inventories", "items"):
            with self.subTest(key=key):
                data = copy.deepcopy(self.data)
                data["current"][key].append(copy.deepcopy(data["current"][key][0]))
                self.rejects(data, "Duplicate")
        self.data["prefix"]["inventories"].pop()
        self.rejects(self.data, "inventory revision")

    def test_retirement_records(self):
        active = copy.deepcopy(self.data)
        active["prefix"]["accounts"][0]["retireSerial"] = "1"
        self.rejects(active, "Active account carries")
        stamp = copy.deepcopy(self.data)
        stamp["prefix"]["accounts"][2]["retireLife"] = 0
        self.rejects(stamp, "leave stamp")

    def test_starting_gold_and_capacity(self):
        starting = copy.deepcopy(self.data)
        starting["startingGold"] = "151"
        self.rejects(starting, "approved starting gold")
        self.data["tail"] = [copy.deepcopy(self.data["tail"][0]) for _ in range(65)]
        self.rejects(self.data, "capacity")


if __name__ == "__main__":
    unittest.main()