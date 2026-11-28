"""Exercise the real installer's remote shell against an isolated app directory."""
import os
from pathlib import Path
import plistlib
import subprocess
import tempfile
import unittest

SCRIPT = Path(os.environ.get('AIR_INSTALL_SCRIPT', Path(__file__).resolve().parents[1] / 'air-dev-install.sh'))
A, B = 'a' * 40, 'b' * 40


class StagedSwapTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.apps = self.root / 'Applications'
        self.apps.mkdir()
        self.tools = self.root / 'bin'
        self.tools.mkdir()
        for name, body in {
                'codesign': 'exit 0', 'otool': 'exit 0',
                'pgrep': 'test "$GAME_RUNNING" = 1'}.items():
            path = self.tools / name
            path.write_text('#!/bin/sh\n' + body + '\n')
            path.chmod(0o700)
        self.app(self.apps / 'dunecity.app', 'old')
        source = SCRIPT.read_text().split("<<'REMOTE'\n", 1)[1].rsplit('\nREMOTE', 1)[0]
        self.shell = source.replace('/Applications/', str(self.apps) + '/')

    def app(self, path, sha):
        resources = path / 'Contents/Resources'
        resources.mkdir(parents=True)
        (resources / 'air-dev-source.txt').write_text(sha + '\n')
        (path / 'Contents/Info.plist').write_bytes(plistlib.dumps({'CFBundleShortVersionString': 'test-dev'}))
        return path

    def invoke(self, sha=A, swap=False, running=False):
        dest = self.root / sha[:9]
        dest.mkdir(exist_ok=True)
        env = dict(os.environ, PATH=str(self.tools) + ':/usr/bin:/bin', HOME=str(self.root),
                   GAME_RUNNING=str(int(running)), TEST_APPS=str(self.apps))
        return subprocess.run(['bash', '-s', '--', str(dest), '0', sha, str(int(swap))],
                              input=self.shell, text=True, capture_output=True, env=env)

    def pending(self, sha=A):
        return self.app(self.root / sha[:9] / 'dunecity.app', sha)

    def source(self, name='dunecity.app'):
        return (self.apps / name / 'Contents/Resources/air-dev-source.txt').read_text().strip()

    def test_default_profile_selects_canonical_economy_a(self):
        declaration = next(line for line in SCRIPT.read_text().splitlines()
                           if line.startswith('DEV_SWITCHES='))
        result = subprocess.run(
            ['bash', '-c', declaration + '\nprintf "%s\\n" "${DEV_SWITCHES[@]}"'],
            check=True, text=True, capture_output=True)
        switches = result.stdout.splitlines()
        self.assertIn('EconomyFirst', switches)
        self.assertIn('RepairReturnCheck', switches)
        self.assertNotIn('EconomyFirstUnitPacing', switches)
        self.assertNotIn('EconomyFirstTurrets', switches)

    def test_running_game_stages_without_replacing_running_app(self):
        self.pending()
        result = self.invoke(running=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('STAGED ' + A, result.stdout)
        self.assertEqual(self.source(), 'old')
        self.assertEqual(self.source('.dunecity-staged.app'), A)

    def test_exit_swaps_existing_stage_without_local_build_or_copy(self):
        self.pending()
        self.assertEqual(self.invoke(running=True).returncode, 0)
        result = self.invoke(swap=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('INSTALLED ' + A, result.stdout)
        self.assertEqual(self.source(), A)
        self.assertFalse((self.apps / '.dunecity-staged.app').exists())
        self.assertEqual(len(list((self.root / A[:9]).glob('replaced-*.app'))), 1)

    def test_newest_stage_replaces_older_pending_source(self):
        self.pending()
        self.assertEqual(self.invoke(running=True).returncode, 0)
        self.pending(B)
        self.assertEqual(self.invoke(B, running=True).returncode, 0)
        wrong = self.invoke(A, swap=True)
        self.assertEqual(wrong.returncode, 4)
        self.assertEqual(self.source(), 'old')
        result = self.invoke(B, swap=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.source(), B)

    def test_missing_stage_fails_without_touching_installed_app(self):
        result = self.invoke(swap=True)
        self.assertEqual(result.returncode, 4)
        self.assertEqual(self.source(), 'old')

    def test_swap_failure_restores_previous_app(self):
        self.pending()
        self.assertEqual(self.invoke(running=True).returncode, 0)
        move = self.tools / 'mv'
        move.write_text('#!/bin/sh\nif [ "$1" = "$TEST_APPS/.dunecity-staged.app" ]; then exit 1; fi\nexec /bin/mv "$@"\n')
        move.chmod(0o700)
        result = self.invoke(swap=True)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(self.source(), 'old')
        self.assertEqual(self.source('.dunecity-staged.app'), A)


if __name__ == '__main__':
    unittest.main()
