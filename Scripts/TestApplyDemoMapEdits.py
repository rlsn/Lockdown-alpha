import copy
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import unittest

from ApplyDemoMapEdits import build_candidate
import DemoContent as content


class SavedSceneExportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original = (content.ROOT / content.WORLD_SOURCE).read_bytes()
        cls.source = content.parse_json(cls.original, 'test world')

    def export(self, original=None):
        raw = self.original if original is None else original
        src = content.parse_json(raw, 'test world')
        geometry = src['geometry']
        return {
            'schema_version': 1, 'source_sha256': hashlib.sha256(raw).hexdigest(),
            'surfaces': copy.deepcopy(geometry['surfaces']),
            'plateaus': [{'id': p['id'], 'polygon_cm': p['polygon_cm']} for p in geometry['elevation']['plateaus']],
            'ramps': [{'id': p['id'], 'polygon_cm': p['polygon_cm']} for p in geometry['elevation']['ramps']],
            'blockers': copy.deepcopy(geometry['blockers']),
            'anchors': [{'id': a['id'], 'position_cm': a['position_cm'], 'members': a.get('members', [])} for a in src['anchors']],
        }

    def raw(self, value):
        from ApplyDemoMapEdits import _json
        return _json(value).encode()

    def test_unchanged_export_preserves_every_byte(self):
        self.assertEqual(build_candidate(self.original, self.raw(self.export())), self.original)

    def test_native_canonical_order_preserves_author_bytes(self):
        export = self.export()
        export['blockers'].sort(key=lambda row: row['id'])
        self.assertEqual(build_candidate(self.original, self.raw(export)), self.original)

    def test_tree_move_changes_only_coordinates_and_revisions(self):
        export = self.export()
        tree = next(row for row in export['blockers'] if row['shape'] == 'cylinder')
        tree['center_cm']['x'] += Decimal('.125')
        result = build_candidate(self.original, self.raw(export))
        after = content.parse_json(result, 'result')
        expected = copy.deepcopy(self.source)
        next(row for row in expected['geometry']['blockers'] if row['id'] == tree['id'])['center_cm']['x'] += Decimal('.125')
        expected['layout_revision'] += 1
        expected['geometry_revision'] += 1
        self.assertEqual(after, expected)
        self.assertEqual(result.count(b'\r\n'), self.original.count(b'\r\n'))
        self.assertIn(b'"approval": {', result)

    def test_stale_source_is_rejected(self):
        export = self.export()
        export['source_sha256'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'source changed'):
            build_candidate(self.original, self.raw(export))

    def test_duplicate_and_unrelated_fields_are_rejected(self):
        export = self.export()
        export['blockers'].append(copy.deepcopy(export['blockers'][0]))
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            build_candidate(self.original, self.raw(export))
        export = self.export()
        export['parameters'] = {'price': 1}
        with self.assertRaisesRegex(ValueError, 'unsupported scene export fields'):
            build_candidate(self.original, self.raw(export))

    def test_layout_points_cannot_be_silently_lost(self):
        export = self.export()
        export['anchors'].pop()
        with self.assertRaisesRegex(ValueError, 'identities cannot change'):
            build_candidate(self.original, self.raw(export))

    def test_copied_tree_can_have_a_fresh_identity(self):
        export = self.export()
        tree = copy.deepcopy(next(row for row in export['blockers'] if row['shape'] == 'cylinder'))
        tree['id'] = 'tree_edit_test_copy'
        export['blockers'].append(tree)
        after = content.parse_json(build_candidate(self.original, self.raw(export)), 'result')
        self.assertEqual(after['geometry']['blockers'][-1]['id'], tree['id'])
        self.assertEqual(after['approval'], self.source['approval'])


if __name__ == '__main__':
    unittest.main()
