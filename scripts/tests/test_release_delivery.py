"""Exercise release delivery with isolated source, keys and artifact directories."""
import base64
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('website_release', ROOT / 'scripts/update-website-release.py')
website = importlib.util.module_from_spec(spec)
spec.loader.exec_module(website)


class WebsiteReleaseTests(unittest.TestCase):
    def test_current_desktop_labels_and_links_change_mobile_and_history_do_not(self):
        before = '''<p class="version-info">Windows/Linux: 1.0.865 · macOS: 1.0.865 · Android: 0.2.33</p>
<p>New in 1.0.865 (Windows and Linux): old features</p>
<p>Windows and Linux 1.0.865 · macOS 1.0.865 · Android 0.2.33 (engine 1.0.858) · TestFlight (engine 1.0.857).</p>
<a href="https://github.com/dunecity-project/dunecity/releases/download/v1.0.865/DuneCity-1.0.865-macOS.dmg">Download 1.0.865 DMG</a>
<a href="/v1.0.865/DuneCity-1.0.865-Windows-x64.zip">Download ZIP</a>
<a href="/v1.0.865/dunecity_1.0.865_amd64.deb">.deb</a>
<p>Dune City 1.0.865 is current for Windows and Linux; macOS 1.0.865.</p>
<p>Releases before 1.0.732 still need this manual installation.</p>'''
        after = website.update(before, '1.0.867')
        self.assertNotIn('1.0.865', after)
        self.assertNotIn('old features', after)
        for value in ['Android 0.2.33 (engine 1.0.858)', 'TestFlight (engine 1.0.857)',
                      'before 1.0.732', 'Download 1.0.867 DMG', 'Download Installer',
                      '/v1.0.867/dunecity_1.0.867_amd64.deb', 'New in 1.0.867']:
            self.assertIn(value, after)
        self.assertEqual(website.update(after, '1.0.867'), after)

    def test_current_authored_news_is_preserved(self):
        html = '<p>New in 1.0.867: <strong>campaign improvements</strong></p>'
        self.assertEqual(website.update(html, '1.0.867'), html)
        with self.assertRaises(ValueError):
            website.update(html, 'v1.0.867')

    def test_changed_template_cannot_silently_keep_stale_desktop_copy(self):
        with self.assertRaisesRegex(ValueError, 'Stale current desktop'):
            website.update('<p class="version-info">PC and Mac version 1.0.865 · Android 0.2.33</p>', '1.0.867')
        with self.assertRaisesRegex(ValueError, 'Stale desktop download'):
            website.update('<a href="/v1.0.865/DuneCity-1.0.865-macOS.dmg">Get Mac 1.0.865</a>', '1.0.867')


class FeedWorkspaceTests(unittest.TestCase):
    def test_real_feed_signing_keeps_source_clean_and_missing_inputs_fail(self):
        with tempfile.TemporaryDirectory(prefix='release-delivery-') as directory:
            root = Path(directory)
            source, temp, home = [root / name for name in ('source', 'temp', 'home')]
            (source / 'scripts').mkdir(parents=True)
            (source / 'cmake').mkdir()
            temp.mkdir()
            for name in ('macos-ci-lane.sh', 'prepare-desktop-updates.py'):
                (source / 'scripts' / name).write_bytes((ROOT / 'scripts' / name).read_bytes())
            key = Ed25519PrivateKey.generate()
            public = key.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
            (source / 'cmake/update-public-key.txt').write_text(base64.b64encode(public).decode())
            keys = home / 'Library/Application Support/DuneCity Signing/updates'
            keys.mkdir(parents=True)
            (keys / 'update-key-password').write_bytes(b'fixture-password')
            (keys / 'update-ed25519.pem').write_bytes(key.private_bytes(
                serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                serialization.BestAvailableEncryption(b'fixture-password')))
            python = temp / 'dunecity-update-signing-python/bin/python'
            python.parent.mkdir(parents=True)
            python.symlink_to(sys.executable)
            env = dict(os.environ, HOME=str(home), GITHUB_WORKSPACE=str(source), RUNNER_TEMP=str(temp),
                       GITHUB_RUN_ID='123', GITHUB_RUN_ATTEMPT='2', DUNECITY_RELEASE_BUILD='ON',
                       UPDATE_TAG='v1.0.867', UPDATE_REPO='dunecity-project/dunecity', PYTHONDONTWRITEBYTECODE='1',
                       # HOME is isolated from production keys; retain this test
                       # interpreter's already-importable dependency locations.
                       PYTHONPATH=os.pathsep.join(path for path in sys.path if path))
            artifacts = [('DuneCity-macOS-DMG', 'DuneCity-1.0.867-macOS.zip'),
                         ('DuneCity-Windows-Installer', 'DuneCity-1.0.867-Windows-x64.exe'),
                         ('DuneCity-Linux-Packages', 'DuneCity-1.0.867-Linux-x86_64.AppImage')]
            for folder, name in artifacts:
                archive = temp / 'dunecity-update-input-123-2' / folder / name
                archive.parent.mkdir(parents=True)
                archive.write_bytes(('fixture-' + name).encode())
            def git(*args):
                return subprocess.check_output(['git', '-C', str(source), *args], env=env, text=True)
            git('init', '-q')
            git('add', '.')
            git('-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid',
                '-c', 'commit.gpgsign=false', 'commit', '-qm', 'fixture')
            result = subprocess.run(['bash', 'scripts/macos-ci-lane.sh', 'feeds'], cwd=source,
                                    env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(git('status', '--porcelain', '--untracked-files=normal'), '')
            output = temp / 'dunecity-update-feeds-123-2'
            self.assertEqual(len(list(output.glob('updates-*.txt'))), 3)
            for manifest in output.glob('updates-*.txt'):
                lines = manifest.read_bytes().splitlines(keepends=True)
                key.public_key().verify(base64.b64decode(lines[-1]), b''.join(lines[:-1]))
            # Another attempt cannot silently pick up old downloaded packages.
            env['GITHUB_RUN_ATTEMPT'] = '3'
            failed = subprocess.run(['bash', 'scripts/macos-ci-lane.sh', 'feeds'], cwd=source,
                                    env=env, capture_output=True, text=True)
            self.assertNotEqual(failed.returncode, 0)
            self.assertFalse((temp / 'dunecity-update-feeds-123-3').exists())
            self.assertEqual(git('status', '--porcelain', '--untracked-files=normal'), '')


if __name__ == '__main__':
    unittest.main()
