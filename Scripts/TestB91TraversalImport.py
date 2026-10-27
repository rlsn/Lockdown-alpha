import json
import math
import unittest
from unittest.mock import patch
from SyncB91Traversal import compile_traversal


class OptionalLinks(unittest.TestCase):
    def test_closed_pairs(self):
        data = json.loads(compile_traversal())
        self.assertEqual(len(data['ladders']), 12)
        self.assertEqual(len(data['anchors']), 16)
        for row in data['anchors']:
            for end in ('lower', 'upper'):
                self.assertLessEqual(math.dist(row[end][:2], row['source_'+end][:2]), 1000)
            self.assertEqual(row['upper'][2]-row['lower'][2], 400)

    def changed(self, change):
        original = json.loads
        def load(value):
            data = original(value)
            change(data)
            return data
        with patch('SyncB91Traversal.json.loads', load):
            with self.assertRaises(AssertionError):
                compile_traversal()

    def test_reject_parallel_users(self):
        self.changed(lambda d: [r.__setitem__('capacity', 2) for r in d.get('rows', []) if 'climb_ms' in r])

    def test_reject_missing_permanent_route(self):
        self.changed(lambda d: [r.__setitem__('permanent_alternative', None) for r in d.get('rows', []) if 'climb_ms' in r])

    def test_reject_changed_source_endpoint(self):
        self.changed(lambda d: [r['upper_cm'].__setitem__(2, r['upper_cm'][2]+100) for r in d.get('rows', []) if 'climb_ms' in r])

    def test_reject_unbound_attach(self):
        self.changed(lambda d: d['rope'].__setitem__('action_requires_binding', False) if 'rope' in d else None)

    def test_reject_consumed_rope(self):
        self.changed(lambda d: d['rope'].__setitem__('consume_on_attach', True) if 'rope' in d else None)


if __name__ == '__main__':
    unittest.main()
