import copy
import hashlib
import json
import unittest
from unittest.mock import patch
from SyncB91Objects import ROOT, compile_objects


class ObjectsImport(unittest.TestCase):
    def test_finite_census(self):
        data = json.loads(compile_objects())
        self.assertEqual(len(data['rows']), 156)
        self.assertEqual(sum(x['count'] for r in data['rows'] for x in r['contents']), 36)
        self.assertEqual(sum(r['kind'] in (4, 5) for r in data['rows']), 48)

    def test_source_closure(self):
        data = json.loads(compile_objects())
        for name, digest in data['source_sha256'].items():
            self.assertEqual(hashlib.sha256((ROOT/'docs/v09/data'/(name+'.json')).read_bytes()).hexdigest(), digest)

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
