import subprocess
import tempfile
import unittest
from pathlib import Path

import CIReleaseVersion as release

INI = (b'[/Script/EngineSettings.GeneralProjectSettings]\n'
       b'ProjectID=6337CB244B330EA0E383B0B9C55A3705\n'
       b'ProjectVersion=0.10.0\n'
       b'\n'
       b'[/Script/Engine.GameSession]\n'
       b'MaxPlayers=3\n')


class NextVersionTest(unittest.TestCase):
    def test_patch_after_the_latest_tag(self):
        self.assertEqual(release.next_version('0.10.0', ['v0.10.0', 'v0.10.1']), ('0.10.2', True))
        self.assertEqual(release.next_version('0.10.1', ['v0.9.9', 'v0.10.1', 'demo', 'v0.10.1-rc']), ('0.10.2', True))

    def test_untagged_newer_ini_is_released_as_is(self):
        # An earlier bump whose release failed, or a hand-edited minor bump.
        self.assertEqual(release.next_version('0.10.2', ['v0.10.0', 'v0.10.1']), ('0.10.2', False))
        self.assertEqual(release.next_version('0.11.0', ['v0.10.5']), ('0.11.0', False))
        self.assertEqual(release.next_version('0.10.0', []), ('0.10.0', False))

    def test_untagged_older_ini_moves_past_the_latest_tag(self):
        self.assertEqual(release.next_version('0.9.0', ['v0.10.1']), ('0.10.2', True))

    def test_versions_compare_numerically(self):
        self.assertEqual(release.next_version('0.10.0', ['v0.9.0', 'v0.10.0']), ('0.10.1', True))
        self.assertEqual(release.next_version('0.10.9', ['v0.10.9']), ('0.10.10', True))

    def test_bad_ini_version_is_refused(self):
        for bad in ('0.10', '0.10.0-beta', 'v0.10.0', ''):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                release.next_version(bad, ['v0.10.0'])


class ProjectVersionTest(unittest.TestCase):
    def test_reads_and_writes_only_the_value(self):
        self.assertEqual(release.read_project_version(INI), '0.10.0')
        changed = release.set_project_version(INI, '0.10.2')
        self.assertEqual(changed, INI.replace(b'ProjectVersion=0.10.0', b'ProjectVersion=0.10.2'))
        crlf = INI.replace(b'\n', b'\r\n')
        self.assertEqual(release.set_project_version(crlf, '1.0.0'),
                         crlf.replace(b'ProjectVersion=0.10.0', b'ProjectVersion=1.0.0'))

    def test_other_sections_and_keys_are_ignored(self):
        raw = b'[Other]\nProjectVersion=9.9.9\n' + INI + b'[/Script/EngineSettings.GeneralProjectSettings2]\nProjectVersion=8.8.8\n'
        self.assertEqual(release.read_project_version(raw), '0.10.0')
        self.assertEqual(release.read_project_version(INI.replace(b'ProjectID=', b'ProjectVersionNote=x\nProjectID=')), '0.10.0')

    def test_missing_duplicate_or_malformed_value_is_refused(self):
        for raw in (INI.replace(b'ProjectVersion=0.10.0\n', b''),
                    INI.replace(b'ProjectVersion=0.10.0\n', b'ProjectVersion=0.10.0\nProjectVersion=0.10.1\n'),
                    INI.replace(b'0.10.0', b'0.10')):
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                release.read_project_version(raw)
        with self.assertRaises(ValueError):
            release.set_project_version(INI, '0.10.2 ')


class PlanTest(unittest.TestCase):
    def git(self, root, *args):
        subprocess.run(['git', '-C', str(root), '-c', 'user.name=t', '-c', 'user.email=t@t', *args],
                       check=True, capture_output=True)

    def test_plan_reads_the_repository(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'Config').mkdir()
            (root / 'Config' / 'DefaultGame.ini').write_bytes(INI)
            self.git(root, 'init', '-q')
            self.git(root, 'add', '.')
            self.git(root, 'commit', '-q', '-m', 'one')
            self.assertEqual(release.plan(root), {'version': '0.10.0', 'bump': False, 'released': False})
            self.git(root, 'tag', 'v0.10.0')
            self.assertEqual(release.plan(root), {'version': '0.10.1', 'bump': True, 'released': True})
            release.apply('0.10.1', root)
            self.git(root, 'commit', '-q', '-am', 'bump')
            self.assertEqual(release.plan(root), {'version': '0.10.1', 'bump': False, 'released': False})
            self.assertEqual((root / 'Config' / 'DefaultGame.ini').read_bytes(),
                             INI.replace(b'0.10.0', b'0.10.1'))

    def test_a_tag_on_an_unpushed_version_commit_releases_its_parent(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'Config').mkdir()
            (root / 'Config' / 'DefaultGame.ini').write_bytes(INI)
            self.git(root, 'init', '-q', '-b', 'main')
            self.git(root, 'add', '.')
            self.git(root, 'commit', '-q', '-m', 'one')
            self.git(root, 'tag', 'v0.10.0')
            # The nightly commits the version on a detached HEAD and pushes only the tag; main keeps 0.10.0.
            self.git(root, 'switch', '-q', '--detach')
            release.apply('0.10.1', root)
            self.git(root, 'commit', '-q', '-am', 'bump')
            self.git(root, 'tag', 'v0.10.1')
            self.git(root, 'switch', '-q', 'main')
            self.assertEqual(release.plan(root), {'version': '0.10.2', 'bump': True, 'released': True})
            self.git(root, 'commit', '-q', '--allow-empty', '-m', 'two')
            self.assertEqual(release.plan(root), {'version': '0.10.2', 'bump': True, 'released': False})
            self.git(root, 'tag', 'v0.10.2-rc')
            self.assertEqual(release.plan(root)['released'], False)


if __name__ == '__main__':
    unittest.main()
