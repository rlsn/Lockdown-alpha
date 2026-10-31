#!/usr/bin/env python3
"""Reject altered finite foliage contracts before native admission."""
import json
import shutil
import tempfile
import unittest
from pathlib import Path
import SyncB91Grass as compiler


class GrassImport(unittest.TestCase):
    def setUp(self):
        self.original = compiler.ROOT
        self.temp = tempfile.TemporaryDirectory()
        compiler.ROOT = Path(self.temp.name)
        target = compiler.ROOT/'SourceData/World'
        target.mkdir(parents=True)
        for name in ('props', 'object_types', 'world_objects_rules'):
            shutil.copyfile(self.original/'SourceData/World'/(name+'.json'), target/(name+'.json'))

    def tearDown(self):
        compiler.ROOT = self.original
        self.temp.cleanup()

    def mutate(self, name, change):
        path = compiler.ROOT/'SourceData/World'/(name+'.json')
        data = json.loads(path.read_text(encoding='utf-8-sig'))
        change(data)
        path.write_text(json.dumps(data), encoding='utf-8')
        with self.assertRaises(AssertionError):
            compiler.compile_grass()

    def test_deterministic_finite_disks(self):
        first = compiler.compile_grass()
        self.assertEqual(first, compiler.compile_grass())
        data = json.loads(first)
        self.assertEqual(len(data['rows']), 96)
        self.assertEqual(data['radius_cm'], 250)
        self.assertFalse(any('contents' in r or 'reward' in r for r in data['rows']))

    def test_duplicate_source(self):
        def change(d):
            rows = [r for r in d['rows'] if r['type_id'] == 'tall_grass']
            rows[1]['id'] = rows[0]['id']
        self.mutate('props', change)

    def test_new_reward_rejects(self):
        self.mutate('props', lambda d: next(r for r in d['rows'] if r['type_id'] == 'tall_grass').update(harvest_output='Fiber'))

    def test_boolean_coordinate_rejects(self):
        self.mutate('props', lambda d: next(r for r in d['rows'] if r['type_id'] == 'tall_grass')['position_cm'].__setitem__(0, True))

    def test_solid_grass_rejects(self):
        self.mutate('object_types', lambda d: next(r for r in d['rows'] if r['id'] == 'tall_grass').update(blocks_walk=True))

    def test_timer_change_rejects(self):
        self.mutate('world_objects_rules', lambda d: d['grass'].update(reveal_after_offense_ms=1000))


if __name__ == '__main__':
    unittest.main()
