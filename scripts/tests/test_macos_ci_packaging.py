"""Exercise the real lane packaging shell without compiling or launching an app."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile


ROOT = Path(__file__).resolve().parents[2]
SOURCE = 'a1' * 20
APP_BYTES = b'packaging fixture app bytes\n'
# Stand in for external macOS tools, not the lane's routing or marker writer.
# The fixture produces a real ZIP, checks the exact ditto arguments and models
# the pointer producer boundary by comparing the installed and archived bytes.
TOOLS = r'''
import json, os, pathlib, sys, zipfile
command = pathlib.Path(sys.argv[0]).name
args = sys.argv[1:]
with open(os.environ['FIXTURE_LOG'], 'a') as log:
    log.write(json.dumps([command, *args]) + '\n')
root = pathlib.Path.cwd()
if command == 'git':
    assert args == ['rev-parse', 'HEAD'], args
    print(os.environ['FIXTURE_SOURCE'])
elif command == 'cmake':
    assert args[:2] == ['--install', 'build'], args
    assert args[-2:] == ['--config', 'Release'], args
    app = pathlib.Path(args[args.index('--prefix') + 1]) / 'dunecity.app/Contents/MacOS/dunecity'
    app.parent.mkdir(parents=True, exist_ok=True)
    app.write_bytes(b'packaging fixture app bytes\n')
elif command == 'cpack':
    assert args == ['-G', 'DragNDrop', '-C', 'Release'], args
    (root / 'DuneCity-1.2.3-macOS.dmg').write_bytes(b'fixture dmg')
elif command == 'ditto':
    assert args[:5] == ['-c', '-k', '--sequesterRsrc', '--keepParent',
                        os.environ['RUNNER_TEMP'] + '/dunecity-mac-install/dunecity.app'], args
    if os.environ.get('FIXTURE_ZIP_FAIL') == '1':
        sys.exit(7)
    if os.environ.get('FIXTURE_ZIP_MISSING') != '1':
        app = pathlib.Path(args[-2])
        with zipfile.ZipFile(args[-1], 'w') as archive:
            for path in app.rglob('*'):
                if path.is_file():
                    archive.write(path, str(path.relative_to(app.parent)))
elif command == 'python3':
    if args[0] != 'tests/menu/run-menu-probe.py':
        os.execv(sys.executable, [sys.executable, *args])
    assert '--prepare-only' in args, args
    assert '--pointer-mapping' in args, args
    assert args[args.index('--build-dir') + 1] == 'build', args
    producer = pathlib.Path(args[args.index('--producer-inputs') + 1])
    assert json.loads(producer.read_text()) == {'fixture': 'static SDL producer'}
    app = pathlib.Path(args[args.index('--runtime-app') + 1])
    package = pathlib.Path(args[args.index('--package-zip') + 1])
    with zipfile.ZipFile(package) as archive:
        assert archive.read('dunecity.app/Contents/MacOS/dunecity') == (app / 'Contents/MacOS/dunecity').read_bytes()
    if os.environ.get('FIXTURE_POINTER_FAIL') == '1':
        sys.exit(9)
    output = pathlib.Path(args[args.index('--export-pointer-artifact') + 1])
    output.mkdir(parents=True, exist_ok=True)
    (output / 'producer-checked').write_text('fixture exported\n')
else:
    raise AssertionError('Unexpected fixture command: ' + command)
'''


class MacPackagingTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='mac-packaging-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.build = self.root / 'build'
        self.build.mkdir()
        (self.root / 'CMakeLists.txt').write_text('project(DuneCity VERSION 1.2.3 LANGUAGES CXX)\n')
        (self.build / 'pointer-producer-inputs.json').write_text(
            json.dumps({'fixture': 'static SDL producer'}))
        (self.root / 'scripts').mkdir()
        (self.root / 'scripts/verify-macos-dmg.sh').write_text(
            'set -eu\n[ "$#" = 1 ]\n[ -s "$1" ]\n'
            'printf "verified\\n" > "$RUNNER_TEMP/dmg-verified"\n')
        self.bin = self.root / 'bin'
        self.bin.mkdir()
        dispatch = self.bin / 'fixture-tool'
        dispatch.write_text('#!' + sys.executable + '\n' + TOOLS)
        dispatch.chmod(0o755)
        for name in ('cmake', 'cpack', 'ditto', 'python3', 'git'):
            (self.bin / name).symlink_to(dispatch)
        self.temp = self.root / 'temp'
        self.temp.mkdir()
        self.log = self.root / 'commands.jsonl'
        self.env = dict(os.environ, PATH=str(self.bin) + os.pathsep + os.environ['PATH'],
                        RUNNER_TEMP=str(self.temp), GITHUB_RUN_ID='123', GITHUB_RUN_ATTEMPT='2',
                        RUNNER_ENVIRONMENT='github-hosted', STABLE='OFF', PACKAGE_BUILD='true',
                        DUNECITY_MAC_DEFER_DMG='true', FIXTURE_LOG=str(self.log), FIXTURE_SOURCE=SOURCE,
                        POINTER_OUT=str(self.temp / 'pointer'), SIGNED_OUT=str(self.temp / 'signed'),
                        PYTHONDONTWRITEBYTECODE='1')
        # Run the actual owned packaging section, not copied control flow. Earlier
        # compile/integration work belongs to the lane's separate existing gates.
        lane = (ROOT / 'scripts/macos-ci-lane.sh').read_text()
        self.packaging = lane[lane.index('if [ "${PACKAGE_BUILD:-false}" != true ]; then exit 0; fi'):]
        self.marker = self.build / 'macos-dmg-deferred.json'
        self.zip = self.build / 'DuneCity-1.2.3-macOS.zip'
        self.dmg = self.build / 'DuneCity-1.2.3-macOS.dmg'

    def run_packaging(self, **overrides):
        env = dict(self.env, **overrides)
        return subprocess.run(['bash', '-c', 'set -euo pipefail\n' + self.packaging],
                              cwd=self.root, env=env, capture_output=True, text=True)

    def commands(self):
        return [json.loads(line) for line in self.log.read_text().splitlines()] if self.log.exists() else []

    def assert_no_marker(self):
        self.assertFalse(self.marker.exists())
        self.assertFalse(self.marker.with_suffix('.json.tmp').exists())

    def test_deferred_emits_exact_zip_hash_source_and_pointer_export_not_dmg(self):
        self.dmg.write_text('stale dmg')
        self.zip.write_text('stale zip')
        self.marker.write_text('stale marker')
        self.marker.with_suffix('.json.tmp').write_text('stale partial marker')
        result = self.run_packaging()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(json.loads(self.marker.read_text()), {
            'schema': 1, 'sourceCommit': SOURCE, 'version': '1.2.3', 'zip': self.zip.name,
            'sha256': hashlib.sha256(self.zip.read_bytes()).hexdigest()})
        with zipfile.ZipFile(self.zip) as archive:
            self.assertEqual(archive.read('dunecity.app/Contents/MacOS/dunecity'), APP_BYTES)
        self.assertTrue((self.build / 'pointer-diagnostic-123-2/producer-checked').is_file())
        self.assertFalse(self.dmg.exists())
        self.assertFalse((self.temp / 'dmg-verified').exists())
        self.assertFalse(self.marker.with_suffix('.json.tmp').exists())
        self.assertNotIn('cpack', [command[0] for command in self.commands()])
        self.assertEqual([command[0] for command in self.commands()][:3], ['cmake', 'ditto', 'python3'])

    def test_normal_unsigned_keeps_dmg_and_verification_and_removes_marker(self):
        for value in ('false', 'TRUE', '1', ''):
            with self.subTest(flag=value):
                self.marker.write_text('stale deferred marker')
                result = self.run_packaging(DUNECITY_MAC_DEFER_DMG=value)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertTrue(self.dmg.is_file())
                self.assertTrue((self.temp / 'dmg-verified').is_file())
                self.assertTrue(self.zip.is_file())
                self.assert_no_marker()
        self.assertIn(['cpack', '-G', 'DragNDrop', '-C', 'Release'], self.commands())

    def test_defer_rejects_official_or_non_hosted_identity_before_install(self):
        for overrides in ({'STABLE': 'ON'}, {'STABLE': 'on'},
                          {'RUNNER_ENVIRONMENT': 'self-hosted'}, {'RUNNER_ENVIRONMENT': ''}):
            with self.subTest(overrides=overrides):
                self.dmg.write_text('stale dmg')
                self.marker.write_text('stale marker')
                result = self.run_packaging(**overrides)
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                self.assertIn('DMG deferral requires dev identity', result.stdout)
                self.assertFalse(self.dmg.exists())
                self.assert_no_marker()
                self.assertEqual(self.commands(), [])

    def test_failed_retry_cannot_reuse_successful_marker_or_zip(self):
        result = self.run_packaging()
        self.assertEqual(result.returncode, 0, result.stderr)
        result = self.run_packaging(FIXTURE_ZIP_FAIL='1')
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        self.assert_no_marker()
        self.assertFalse(self.zip.exists())
        self.assertFalse(self.dmg.exists())

    def test_missing_zip_or_failed_pointer_export_cannot_emit_marker(self):
        for overrides in ({'FIXTURE_ZIP_MISSING': '1'}, {'FIXTURE_POINTER_FAIL': '1'}):
            with self.subTest(overrides=overrides):
                self.marker.write_text('stale marker')
                result = self.run_packaging(**overrides)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assert_no_marker()
                self.assertFalse(self.dmg.exists())

    def test_marker_requires_full_git_commit_and_numeric_version(self):
        result = self.run_packaging(FIXTURE_SOURCE='short-sha')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('full source commit', result.stderr)
        self.assert_no_marker()
        (self.root / 'CMakeLists.txt').write_text('project(DuneCity VERSION invalid LANGUAGES CXX)\n')
        result = self.run_packaging()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('numeric version', result.stderr)
        self.assert_no_marker()


if __name__ == '__main__':
    unittest.main()
