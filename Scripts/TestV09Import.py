#!/usr/bin/env python3
"""Negative source-import checks in temporary copies; never edit adopted input."""
import json
import shutil
import tempfile
import unittest
from pathlib import Path
import SyncV09Data as sync


class SpatialImportTests(unittest.TestCase):
    def setUp(self):
        self.original = sync.SOURCE
        self.temp = tempfile.TemporaryDirectory()
        sync.SOURCE = Path(self.temp.name)
        for name in sync.INPUTS:
            shutil.copyfile(self.original / (name + '.json'), sync.SOURCE / (name + '.json'))

    def tearDown(self):
        sync.SOURCE = self.original
        self.temp.cleanup()

    def reject(self, name, edit):
        path = sync.SOURCE / (name + '.json')
        data = json.loads(path.read_text())
        edit(data)
        path.write_text(json.dumps(data))
        with self.assertRaises((AssertionError, KeyError)):
            sync.compile_map()

    def test_reproducible(self):
        self.assertEqual(sync.compile_map(), sync.compile_map())

    def test_unknown_semantic_field(self):
        self.reject('pois', lambda d: d['rows'][0].update(unimplemented_teleport=True))

    def test_duplicate_identity(self):
        self.reject('spawn_pads', lambda d: d['rows'][1].update(id=d['rows'][0]['id']))

    def test_coordinate_unit_drift(self):
        self.reject('pois', lambda d: d['rows'][0].update(position_cm=[400000, 400000, 40000]))

    def test_noninteger_centimeters(self):
        self.reject('pois', lambda d: d['rows'][0].update(position_cm=[4000.5, 4000, 400]))

    def test_unknown_route_target(self):
        self.reject('routes', lambda d: d['rows'][0].update(to='missing_target'))

    def test_profile_mixing(self):
        self.reject('rules', lambda d: d['world'].update(seed=7001))

    def test_unbound_service(self):
        self.reject('services', lambda d: d['rows'][0].update(poi_id='ruin_a'))

    def test_unknown_service_function(self):
        self.reject('services', lambda d: d['rows'][0].update(functions=['free_teleport']))

    def test_unknown_poi_function(self):
        self.reject('pois', lambda d: d['rows'][0].update(functions=['route_hub', 'free_teleport']))

    def test_duplicate_service(self):
        self.reject('services', lambda d: d['rows'][1].update(id=d['rows'][0]['id']))

    def test_alias_target(self):
        self.reject('legacy_aliases', lambda d: d['aliases'].update(City='missing'))

    def test_security_unit_drift(self):
        self.reject('security', lambda d: d['civic_zone_radius_cm'].update(capital=120))


if __name__ == '__main__':
    unittest.main()
