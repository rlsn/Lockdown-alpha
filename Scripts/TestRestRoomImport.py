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
        for seat in range(6):
            start = world.point(anchors[f"rest_spawn_{seat}"]["position_cm"], "rest")
            signup = world.point(anchors[f"pk_signup_{seat}"]["position_cm"], "signup")
            self.assertEqual(world.squared_distance(start, signup), 300 ** 2)
            for service in range(1, 4):
                end = world.point(anchors[f"rest_service_{service}"]["position_cm"], "service")
                for step in range(21):
                    foot = tuple(a + (b - a) * step / 20 for a, b in zip(start, end))
                    self.assertTrue(geometry.legal(foot, 40), (seat, service, step))
        for foot in ((6150, 0, 0), (8850, 0, 0), (7500, -1150, 0), (7500, 1150, 0), (5150, 0, 0)):
            self.assertFalse(geometry.legal(foot, 40), foot)

    def test_missing_service_or_displaced_signup_refuses(self):
        missing = deepcopy(self.payload)
        missing["anchors"] = [row for row in missing["anchors"] if row["id"] != "rest_service_2"]
        with self.assertRaises(content.ContentError):
            world.validate_bindings(missing, self.data)
        moved = deepcopy(self.payload)
        signup = next(row for row in moved["anchors"] if row["id"] == "pk_signup_0")
        signup["position_cm"]["x"]["numerator"] += 1
        with self.assertRaises(content.ContentError):
            world.validate_bindings(moved, self.data)


if __name__ == "__main__":
    unittest.main()
