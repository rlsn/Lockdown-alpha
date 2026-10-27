import json
import unittest
from unittest.mock import patch
from SyncB91Tools import compile_tools


class PortableOverlay(unittest.TestCase):
    def test_exact_overlay(self):
        data = json.loads(compile_tools())
        self.assertEqual(len(data['rows']), 48)
        self.assertEqual({r['native_kind'] for r in data['definitions']}, {94, 95, 96})
        self.assertEqual({r['id']: r['fuel_ms'] for r in data['definitions']},
                         dict(torch=180000, lantern=300000, rope=0))

    def changed(self, change):
        original = json.loads
        def load(value):
            data = original(value)
            change(data)
            return data
        with patch('SyncB91Tools.json.loads', load):
            with self.assertRaises(AssertionError):
                compile_tools()

    def test_reject_passive_stat(self):
        def change(data):
            if 'rows' in data:
                for row in data['rows']:
                    if row.get('id') == 'torch' and 'stats' in row:
                        row['stats']['armor'] = 1
        self.changed(change)

    def test_reject_refill(self):
        self.changed(lambda d: d['torch'].__setitem__('refill', True) if 'torch' in d else None)

    def test_reject_sight(self):
        self.changed(lambda d: d['lantern'].__setitem__('sight_bonus_cm', 100) if 'lantern' in d else None)

    def test_reject_extra_instance(self):
        def change(data):
            if 'rows' in data and any(r.get('type_id') == 'torch' for r in data['rows']):
                data['rows'].append(next(r.copy() for r in data['rows'] if r.get('type_id') == 'torch'))
        self.changed(change)


if __name__ == '__main__':
    unittest.main()
