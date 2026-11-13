"""Shared rest-room geometry and author-input refusals."""
from copy import deepcopy
from pathlib import Path
import unittest

import DemoContent as content
import DemoWorld as world


class SharedRestRoom(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = Path(__file__).resolve().parents[1]
        cls.data = content.compile_author(root).catalog.data
        raw = (root / world.SOURCE).read_bytes()
        schema = (root / world.SCHEMA_SOURCE).read_bytes()
        cls.payload = world.project_author(content.parse_json(raw, world.SOURCE), raw, schema)

    def test_closed_room_and_service_access(self):
        geometry = world.Geometry(self.payload)
        anchors = {row["id"]: row for row in self.payload["anchors"]}
        signups = [world.point(anchors[f"pk_signup_{seat}"]["position_cm"], "signup") for seat in range(6)]
        centre = tuple(sum(position[axis] for position in signups) / 6 for axis in range(3))
        radius = content.quantity(self.data["parameters"]["AIRDROP.PickupRadiusCm"])
        body = content.quantity(self.data["parameters"]["UNIT.CollisionRadiusCm"])
        self.assertTrue(geometry.legal(centre, radius + body))
        for seat in range(6):
            start = world.point(anchors[f"rest_spawn_{seat}"]["position_cm"], "rest")
            self.assertLessEqual(world.squared_distance(signups[seat], centre), (radius - body) ** 2)
            for other in signups[:seat]:
                self.assertGreaterEqual(world.squared_distance(signups[seat], other), (2 * body) ** 2)
            for offset in self.payload["placement"]["spawn_offsets_cm"]:
                delta = world.point(offset, "spawn offset")
                self.assertGreater(world.squared_distance(tuple(a + b for a, b in zip(start, delta)), centre), radius ** 2)
            for service in range(1, 4):
                end = world.point(anchors[f"rest_service_{service}"]["position_cm"], "service")
                for step in range(21):
                    foot = tuple(a + (b - a) * step / 20 for a, b in zip(start, end))
                    self.assertTrue(geometry.legal(foot, 40), (seat, service, step))
        services = [world.point(anchors[f"rest_service_{index}"]["position_cm"], "service") for index in range(1, 4)]
        self.assertLessEqual(max(position[0] for position in services) - min(position[0] for position in services), 800)
        for foot in ((6150, 0, 0), (8850, 0, 0), (7500, -1150, 0), (7500, 1150, 0), (5150, 0, 0)):
            self.assertFalse(geometry.legal(foot, 40), foot)

    def test_public_training_row_is_legal_and_canonical(self):
        geometry = world.Geometry(self.payload)
        anchors = {row["id"]: row for row in self.payload["anchors"]}
        for slot in range(6):
            position = world.point(anchors[f"training_dummy_{slot}"]["position_cm"], "training dummy")
            self.assertEqual(position, (6500 + 400 * slot, 700, 0))
            self.assertTrue(geometry.legal(position, 40), position)

    def test_missing_service_or_displaced_signup_refuses(self):
        missing = deepcopy(self.payload)
        missing["anchors"] = [row for row in missing["anchors"] if row["id"] != "rest_service_2"]
        with self.assertRaises(content.ContentError):
            world.validate_bindings(missing, self.data)
        moved = deepcopy(self.payload)
        signup = next(row for row in moved["anchors"] if row["id"] == "pk_signup_0")
        signup["position_cm"]["x"]["numerator"] += 2000
        with self.assertRaises(content.ContentError):
            world.validate_bindings(moved, self.data)
        overlapping = deepcopy(self.payload)
        positions = {row["id"]: row for row in overlapping["anchors"]}
        positions["pk_signup_1"]["position_cm"] = deepcopy(positions["pk_signup_0"]["position_cm"])
        with self.assertRaises(content.ContentError):
            world.validate_bindings(overlapping, self.data)
        missing_dummy = deepcopy(self.payload)
        missing_dummy["anchors"] = [row for row in missing_dummy["anchors"] if row["id"] != "training_dummy_5"]
        with self.assertRaises(content.ContentError):
            world.validate_bindings(missing_dummy, self.data)
        displaced_dummy = deepcopy(self.payload)
        dummy = next(row for row in displaced_dummy["anchors"] if row["id"] == "training_dummy_0")
        dummy["position_cm"]["y"]["numerator"] += 1
        with self.assertRaises(content.ContentError):
            world.validate_bindings(displaced_dummy, self.data)


if __name__ == "__main__":
    unittest.main()
