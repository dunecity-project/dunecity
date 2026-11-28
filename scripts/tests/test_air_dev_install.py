"""Exercise the real installer's copy/stamp and remote swap paths in isolated apps."""
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
                'codesign': '''if [ "$1" = --force ]; then
                    for app; do :; done
                    cat "$app/Contents/Resources/air-dev-version.txt" >> "$TEST_SIGNED_VERSIONS"
                    for field in CFBundleShortVersionString CFBundleVersion CFBundleGetInfoString; do
                        /usr/libexec/PlistBuddy -c "Print $field" "$app/Contents/Info.plist" >> "$TEST_SIGNED_VERSIONS"
                    done
                fi''',
                'otool': 'exit 0',
                'pgrep': 'test "$GAME_RUNNING" = 1'}.items():
            path = self.tools / name
            path.write_text('#!/bin/sh\n' + body + '\n')
            path.chmod(0o700)
        self.app(self.apps / 'dunecity.app', 'old')
        script = SCRIPT.read_text()
        helper = script.split('prepare_dev_app() {\n', 1)[1].split('\n}\n', 1)[0]
        self.prepare_shell = 'set -euo pipefail\nprepare_dev_app() {\n' + helper + '\n}\nprepare_dev_app "$@"\n'
        copy = script.split('copy_dev_app() {\n', 1)[1].split('\n}\n', 1)[0]
        self.copy_shell = ('set -euo pipefail\nprepare_dev_app() {\n' + helper
                           + '\n}\ncopy_dev_app() {\n' + copy + '\n}\ncopy_dev_app "$@"\n')
        source = script.split("<<'REMOTE'\n", 1)[1].rsplit('\nREMOTE', 1)[0]
        self.shell = source.replace('/Applications/', str(self.apps) + '/')

    def app(self, path, sha=None):
        resources = path / 'Contents/Resources'
        (resources / 'config').mkdir(parents=True)
        (resources / 'config/QuantBot Config.ini.default').write_text('EconomyFirst=0\nRepairReturnCheck=0\n')
        if sha is not None:
            (resources / 'air-dev-source.txt').write_text(sha + '\n')
        (path / 'Contents/MacOS').mkdir()
        (path / 'Contents/MacOS/dunecity').write_bytes(b'fixture executable\n')
        (path / 'Contents/Info.plist').write_bytes(plistlib.dumps({
            'CFBundleShortVersionString': '9.8.765',
            'CFBundleVersion': '9.8.765',
            'CFBundleGetInfoString': 'Dune City 9.8.765dev',
            'CFBundleIdentifier': 'org.dunecity.fixture',
        }))
        return path

    def environment(self, running=False):
        return dict(os.environ, PATH=str(self.tools) + ':/usr/bin:/bin', HOME=str(self.root),
                    GAME_RUNNING=str(int(running)), TEST_APPS=str(self.apps),
                    TEST_SIGNED_VERSIONS=str(self.root / 'signed-versions.txt'))

    def prepare(self, built, dest, sha=A):
        result = subprocess.run(
            ['bash', '-s', '--', str(built), str(dest), sha, 'EconomyFirst'],
            input=self.prepare_shell, text=True, capture_output=True, env=self.environment())
        self.assertEqual(result.returncode, 0, result.stderr)
        return result

    @staticmethod
    def snapshot(app):
        return {str(path.relative_to(app)): path.read_bytes()
                for path in app.rglob('*') if path.is_file()}

    @staticmethod
    def version(app):
        return (app / 'Contents/Resources/air-dev-version.txt').read_text().strip()

    def invoke(self, sha=A, swap=False, running=False):
        dest = self.root / sha[:9]
        dest.mkdir(exist_ok=True)
        return subprocess.run(['bash', '-s', '--', str(dest), '0', sha, str(int(swap))],
                              input=self.shell, text=True, capture_output=True, env=self.environment(running))

    def pending(self, sha=A):
        built = self.app(self.root / 'built' / sha / 'dunecity.app')
        pending = self.root / sha[:9] / 'dunecity.app'
        pending.parent.mkdir(exist_ok=True)
        self.prepare(built, pending, sha)
        return pending

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
        self.assertIn('GarrisonHold', switches)
        self.assertIn('AirliftColonists', switches)
        self.assertNotIn('EconomyFirstUnitPacing', switches)
        self.assertNotIn('EconomyFirstTurrets', switches)

    def test_unreachable_air_fails_before_any_local_app_preparation(self):
        ssh = self.tools / 'ssh'
        ssh.write_text('#!/bin/sh\nexit 255\n')
        ssh.chmod(0o700)
        result = subprocess.run(['bash', str(SCRIPT), '--no-build'],
            env=dict(self.environment(), DUNECITY_AIR_BUILD_SOURCE=A,
                     DUNECITY_AIR_HOST='unreachable-fixture'), text=True, capture_output=True)
        self.assertEqual(result.returncode, 255, result.stderr)
        self.assertFalse((self.root / 'signed-versions.txt').exists())

    def test_copy_boundary_uses_only_shared_performance_resource(self):
        cli = self.tools / 'agent-workspace'
        cli.write_text('#!/usr/bin/python3\nimport json,os,sys\n'
            'assert sys.argv[1:6] == ["performance-lock","--project","dunecity","--shared","--"]\n'
            'open(os.environ["TEST_COPY_ARGS"],"w").write(json.dumps(sys.argv[1:]))\n'
            'os.execvp(sys.argv[6],sys.argv[6:])\n')
        cli.chmod(0o700)
        ssh = self.tools / 'ssh'
        ssh.write_text('#!/bin/sh\nfor last; do :; done\n'
                       'printf "%s\\n" "$last" >> "$TEST_COPY_SSH"\n'
                       'case "$last" in "tar -xf -"*) cat >/dev/null;; esac\nexit 0\n')
        ssh.chmod(0o700)
        built = self.app(self.root / 'built.app')
        work = self.root / 'copied work'
        work.mkdir()
        remote = '/tmp/remote copy'
        args, commands = self.root / 'args.json', self.root / 'ssh.txt'
        result = subprocess.run(['bash', '-s', '--', str(built), str(work), B,
                                 'fixture-host', remote, 'EconomyFirst'],
            input=self.copy_shell, text=True, capture_output=True,
            env=dict(self.environment(), TEST_COPY_ARGS=str(args), TEST_COPY_SSH=str(commands)))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('"--shared"', args.read_text())
        self.assertNotIn('"run"', args.read_text())
        self.assertEqual(commands.read_text().splitlines(),
            [f'mkdir -p "{remote}"; rm -rf "{remote}/dunecity.app"', f'tar -xf - -C "{remote}"'])
        self.assertTrue(self.version(work / 'dunecity.app').endswith('-bbbbbbb'))

    def test_copy_stamps_exact_version_before_signing_without_changing_built_app(self):
        built = self.app(self.root / 'built.app')
        original = self.snapshot(built)
        copied = self.root / 'copied.app'
        result = self.prepare(built, copied, B)
        version = self.version(copied)
        self.assertRegex(version, r'^9\.8\.765-t[0-9]{8}\.[0-9]{6}\.[0-9]{6}-bbbbbbb$')
        info = plistlib.loads((copied / 'Contents/Info.plist').read_bytes())
        self.assertEqual(info['CFBundleShortVersionString'], version)
        self.assertEqual(info['CFBundleVersion'], version)
        self.assertEqual(info['CFBundleGetInfoString'], 'Dune City ' + version)
        self.assertEqual(info['CFBundleIdentifier'], 'org.dunecity.fixture')
        self.assertEqual((copied / 'Contents/Resources/air-dev-source.txt').read_text(), B + '\n')
        self.assertEqual((copied / 'Contents/Resources/air-dev-version.txt').read_text(), version + '\n')
        self.assertEqual((self.root / 'signed-versions.txt').read_text().splitlines(),
                         [version, version, version, 'Dune City ' + version])
        self.assertIn(f'LOCAL_COPY_READY {B} ({version})', result.stdout)
        defaults = (copied / 'Contents/Resources/config/QuantBot Config.ini.default').read_text()
        self.assertEqual(defaults, 'EconomyFirst=1\nRepairReturnCheck=0\n')
        self.assertEqual(self.snapshot(built), original)

    def test_each_copy_of_same_source_gets_a_distinct_version(self):
        built = self.app(self.root / 'built.app')
        first, second = self.root / 'first.app', self.root / 'second.app'
        self.prepare(built, first)
        self.prepare(built, second)
        self.assertNotEqual(self.version(first), self.version(second))
        self.assertTrue(self.version(first).endswith('-aaaaaaa'))
        self.assertTrue(self.version(second).endswith('-aaaaaaa'))

    def test_running_game_stages_without_replacing_running_app(self):
        version = self.version(self.pending())
        result = self.invoke(running=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(f'STAGED {A} ({version})', result.stdout)
        self.assertEqual(self.source(), 'old')
        self.assertEqual(self.source('.dunecity-staged.app'), A)

    def test_exit_swaps_existing_stage_without_local_build_or_copy(self):
        pending = self.pending()
        version, original = self.version(pending), self.snapshot(pending)
        signed = (self.root / 'signed-versions.txt').read_bytes()
        self.assertEqual(self.invoke(running=True).returncode, 0)
        # Multiple game-wait ticks must neither copy/sign nor regenerate the identity.
        for _ in range(2):
            staged = self.invoke(swap=True, running=True)
            self.assertEqual(staged.returncode, 0, staged.stderr)
            self.assertIn(f'STAGED {A} ({version})', staged.stdout)
            self.assertEqual(self.snapshot(self.apps / '.dunecity-staged.app'), original)
        result = self.invoke(swap=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(f'INSTALLED {A} ({version})', result.stdout)
        self.assertEqual(self.snapshot(self.apps / 'dunecity.app'), original)
        self.assertEqual((self.root / 'signed-versions.txt').read_bytes(), signed)
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
