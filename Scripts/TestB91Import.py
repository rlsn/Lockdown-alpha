import json
import shutil
import tempfile
import unittest
from pathlib import Path
import SyncB91Data as sync


class EnvironmentImportTests(unittest.TestCase):
    def setUp(self):
        self.original = sync.SOURCE
        self.directory = tempfile.TemporaryDirectory()
        sync.SOURCE = Path(self.directory.name)
        for name in ('buildings', 'props', 'traversal_anchors', 'world_objects_rules'):
            shutil.copyfile(self.original/(name+'.json'), sync.SOURCE/(name+'.json'))

    def tearDown(self):
        sync.SOURCE = self.original
        self.directory.cleanup()

    def reject(self, name, edit):
        p = sync.SOURCE/(name+'.json')
        data = json.loads(p.read_bytes())
        edit(data)
        p.write_text(json.dumps(data))
        with self.assertRaises((AssertionError, KeyError, StopIteration)):
            sync.compile_environment()

    def test_repeat_compile(self):
        self.assertEqual(sync.compile_environment(), sync.compile_environment())

    def test_unknown_door_verb(self):
        self.reject('props', lambda d: d['rows'][0].update(free_teleport=True))

    def test_moved_door(self):
        self.reject('props', lambda d: d['rows'][0].update(position_cm=[1, 2, 3]))

    def test_public_lock(self):
        self.reject('props', lambda d: next(p for p in d['rows'] if p.get('hinge_side') == 'south').update(lockable=True))

    def test_foreign_resident(self):
        self.reject('props', lambda d: d['rows'][0].update(lock_owner='contender:any'))

    def test_missing_roof(self):
        self.reject('buildings', lambda d: d['rows'][0].update(roof_access=False))

    def test_bad_units(self):
        self.reject('world_objects_rules', lambda d: d.update(door_open_ms=.3))


if __name__ == '__main__':
    unittest.main()
