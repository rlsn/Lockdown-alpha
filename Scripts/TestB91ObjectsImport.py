import copy
import hashlib
import json
import unittest
from unittest.mock import patch
from SyncB91Objects import ROOT, compile_objects


class ObjectsImport(unittest.TestCase):
    def test_finite_census(self):
        data = json.loads(compile_objects())
        self.assertEqual(len(data['rows']), 252)
        self.assertEqual(sum(x['count'] for r in data['rows'] for x in r['contents']), 36)
        self.assertEqual(sum(r['kind'] in (4, 5) for r in data['rows']), 48)

    def test_furniture(self):
        rows = [r for r in json.loads(compile_objects())['rows'] if r['kind'] == 12]
        self.assertEqual(len(rows), 32)
        self.assertTrue(all(not r['contents'] for r in rows))

    def test_reject_furniture_sight_blocker(self):
        original = json.loads
        def altered(value):
            data = original(value)
            for r in data.get('rows', []):
                if r.get('id') == 'furniture': r['blocks_sight'] = True
            return data
        with patch('SyncB91Objects.json.loads', altered):
            with self.assertRaises(AssertionError): compile_objects()

    def test_source_closure(self):
        data = json.loads(compile_objects())
        for name, digest in data['source_sha256'].items():
            self.assertEqual(hashlib.sha256((ROOT/'docs/v09/data'/(name+'.json')).read_bytes()).hexdigest(), digest)

    def test_sapling_sources(self):
        rows = [r for r in json.loads(compile_objects())['rows'] if r['kind'] == 13]
        self.assertEqual(len(rows), 64)
        self.assertEqual(len({r['harvest_source'] for r in rows}), 64)
        self.assertTrue(all(not r['contents'] for r in rows))

    def test_reject_duplicate_sapling_source(self):
        original = json.loads
        def altered(value):
            data = original(value)
            rows = [r for r in data.get('rows', []) if r.get('type_id') == 'sapling']
            if rows: rows[1]['one_time_source_id'] = rows[0]['one_time_source_id']
            return data
        with patch('SyncB91Objects.json.loads', altered):
            with self.assertRaises(AssertionError): compile_objects()

    def test_reject_destruction_reward(self):
        original = json.loads
        def altered(value):
            data = original(value)
            if 'sapling' in data: data['sapling']['destruction_fiber'] = 1
            return data
        with patch('SyncB91Objects.json.loads', altered):
            with self.assertRaises(AssertionError): compile_objects()

    def test_reject_extra_endowment(self):
        original = json.loads
        def altered(value):
            data = original(value)
            if 'rows' in data and any('initial_contents' in r for r in data['rows']):
                data = copy.deepcopy(data)
                next(r for r in data['rows'] if r.get('initial_contents'))['initial_contents'][0]['count'] += 1
            return data
        with patch('SyncB91Objects.json.loads', altered):
            with self.assertRaises(AssertionError):
                compile_objects()

    def test_reject_respawn(self):
        original = json.loads
        def altered(value):
            data = original(value)
            if 'rows' in data and any(r.get('type_id') == 'chest' for r in data['rows']):
                next(r for r in data['rows'] if r['type_id'] == 'chest')['respawn'] = True
            return data
        with patch('SyncB91Objects.json.loads', altered):
            with self.assertRaises(AssertionError):
                compile_objects()


if __name__ == '__main__':
    unittest.main()
