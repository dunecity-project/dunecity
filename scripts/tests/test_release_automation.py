import argparse
import base64
import copy
from contextlib import redirect_stdout
import datetime as dt
import importlib.util
import hashlib
import io
import json
import os
from pathlib import Path
import re
import subprocess
import tarfile
import tempfile
import unittest
from unittest.mock import patch

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

ROOT = Path(__file__).resolve().parents[2]


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'scripts' / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


automation = load('release_automation', 'release-automation.py')
updates = load('release_updates', 'prepare-desktop-updates.py')
SHA = 'a' * 40
OTHER = 'b' * 40
VERSION = re.search(r'project\(DuneCity VERSION ([0-9.]+)', (ROOT / 'CMakeLists.txt').read_text())[1]


def browser_fixture(root, sha=SHA, channel='staging'):
    root.mkdir(parents=True, exist_ok=True)
    names = ['index.html', 'shell.js', 'shell.css', 'p2p-direct.js',
             'dunecity.js', 'dunecity.wasm', 'dunecity.data']
    for name in names:
        (root / name).write_bytes(('browser fixture ' + name).encode())
    (root / '.htaccess').write_text("Header always set Content-Security-Policy \"default-src 'none'\"\n")
    build = dict(sourceCommit=sha, version=VERSION, channel=channel,
                 releaseBuild='OFF' if channel == 'staging' else 'ON', artifacts=names,
                 sha256={name: automation.digest(root / name) for name in names})
    automation.write_json(root / 'build.json', build)
    return build


class StagingSelectionTests(unittest.TestCase):
    def test_snapshot_identity_is_immutable_and_requires_full_sha(self):
        self.assertEqual(automation.snapshot_tag(SHA), automation.snapshot_tag(SHA))
        self.assertNotEqual(automation.snapshot_tag(SHA), automation.snapshot_tag(OTHER))
        self.assertTrue(automation.snapshot_tag(SHA).startswith('staging-' + SHA + '-'))
        for value in ('main', 'abc12345', SHA.upper(), SHA + '\n'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                automation.snapshot_tag(value)

    def test_existing_snapshot_is_not_rebuilt_even_on_manual_request(self):
        now = dt.datetime(2026, 11, 12, tzinfo=dt.timezone.utc)
        selected, reason = automation.package_decision(SHA, True, {'draft': False}, [], now, manual=True)
        self.assertFalse(selected)
        self.assertIn('no-new-inputs', reason)

    def test_current_red_head_never_falls_back_to_previous_green(self):
        now = dt.datetime(2026, 11, 12, tzinfo=dt.timezone.utc)
        selected, reason = automation.package_decision(SHA, False, None, [], now)
        self.assertFalse(selected)
        self.assertIn(SHA, reason)

    def test_debounce_boundary_and_explicit_on_demand(self):
        now = dt.datetime(2026, 11, 12, 4, tzinfo=dt.timezone.utc)
        run = dict(id=7, display_title='Staging packages ' + OTHER, created_at='2026-11-12T02:00:01Z')
        self.assertFalse(automation.package_decision(SHA, True, None, [run], now)[0])
        self.assertTrue(automation.package_decision(SHA, True, None, [run], now, manual=True)[0])
        self.assertTrue(automation.package_decision(SHA, True, None, [run], now, current_run=7)[0])
        run['created_at'] = '2026-11-12T02:00:00Z'
        self.assertTrue(automation.package_decision(SHA, True, None, [run], now)[0])

    def test_receipt_requires_every_platform_and_test_job_at_the_exact_source(self):
        source = dict(head_sha=SHA, head_branch=automation.STAGING, event='push')
        names = ['test-linux', 'relay-tests', 'signaling-tests', 'build-emscripten',
                 'build-windows', 'build-linux', 'build-macos']
        jobs = [dict(name=name, conclusion='success') for name in names]
        self.assertTrue(automation.integration_green(source, jobs, SHA))
        self.assertFalse(automation.integration_green(source, jobs[:-1], SHA))
        jobs[-1]['conclusion'] = 'failure'
        self.assertFalse(automation.integration_green(source, jobs, SHA))
        jobs[-1]['conclusion'] = 'success'
        self.assertFalse(automation.integration_green(source, jobs, OTHER))
        source['event'] = 'pull_request'
        self.assertFalse(automation.integration_green(source, jobs, SHA))

    def scheduled_plan(self, directory, nightly=False, existing=None):
        event = Path(directory) / 'event.json'
        automation.write_json(event, {'schedule': '47 3 * * *' if nightly else '17 */2 * * *'})
        selected = Path(directory) / 'selection.json'
        runs = [dict(id=7, head_sha=SHA, head_branch=automation.STAGING,
                     event='push', conclusion='success', display_title='CI integration')]
        with patch.dict(os.environ, GITHUB_EVENT_PATH=str(event), GITHUB_EVENT_NAME='schedule',
                        GITHUB_REF='refs/heads/main', GITHUB_SHA=OTHER, GITHUB_RUN_ID='8',
                        GITHUB_OUTPUT='', GITHUB_STEP_SUMMARY=''), \
             patch.object(automation, 'api', return_value={'object': {'sha': SHA}}), \
             patch.object(automation, 'workflow_runs', return_value=runs), \
             patch.object(automation.subprocess, 'run', return_value=subprocess.CompletedProcess([], 1, '', 'HTTP 404')), \
             patch.object(automation, 'optional_release', return_value=existing):
            automation.plan(argparse.Namespace(output=selected))
        return json.loads(selected.read_text())

    def test_schedule_explicitly_selects_staging_not_default_branch_source(self):
        with tempfile.TemporaryDirectory() as directory:
            selected = self.scheduled_plan(directory)
            self.assertEqual(selected['sha'], SHA)
            self.assertTrue(selected['staging'])
            self.assertTrue(selected['package'])
            self.assertFalse(selected['stable'])

    def test_daily_nightly_still_runs_without_repackaging_unchanged_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            selected = self.scheduled_plan(directory, nightly=True, existing={'draft': False})
            self.assertEqual(selected['sha'], SHA)
            self.assertTrue(selected['run'])
            self.assertFalse(selected['package'])
            self.assertEqual(selected['tier'], 'nightly')

    def nightly_probe_plan(self, directory, **inputs):
        event = Path(directory) / 'event.json'
        automation.write_json(event, {'inputs': dict(nightly_probe='true', **inputs)})
        selected = Path(directory) / 'selection.json'
        with patch.dict(os.environ, GITHUB_EVENT_PATH=str(event), GITHUB_EVENT_NAME='workflow_dispatch',
                        GITHUB_REF='refs/heads/' + automation.STAGING, GITHUB_SHA=SHA, GITHUB_RUN_ID='8',
                        GITHUB_OUTPUT='', GITHUB_STEP_SUMMARY=''):
            automation.plan(argparse.Namespace(output=selected))
        return json.loads(selected.read_text())

    def test_nightly_probe_selects_the_nightly_tier_and_never_packages(self):
        with tempfile.TemporaryDirectory() as directory:
            selected = self.nightly_probe_plan(directory)
            self.assertEqual((selected['tier'], selected['run'], selected['package'], selected['staging']),
                             ('nightly', True, False, False))
            self.assertEqual(selected['sha'], SHA)
            with self.assertRaisesRegex(ValueError, 'runs alone'):
                self.nightly_probe_plan(directory, macos_skin_probe='true')

    def test_documentation_filter_does_not_skip_build_or_signing_inputs(self):
        self.assertTrue(automation.docs_only(['docs/release-operations.md', 'HANDOVER.md']))
        self.assertFalse(automation.docs_only(['README.md'] * 300))
        for path in ('CMakeLists.txt', 'cmake/update-public-key.txt', 'config/Game Options.ini',
                     '.github/workflows/build.yml', 'src/main.cpp'):
            self.assertFalse(automation.docs_only([path]))


    def test_mini_admission_requires_active_authority_and_the_actual_source(self):
        state = dict(active=True, id='release-capability', sid=SHA, owner='operator')
        self.assertEqual(automation.release_token(state, SHA, SHA), 'release-capability')
        for invalid in (None, {}, dict(state, active=False), dict(state, active='true'),
                        dict(state, sid=OTHER), dict(state, id=''), dict(state, id='bad\nid')):
            with self.subTest(state=invalid), self.assertRaisesRegex(ValueError, 'admission blocked'):
                automation.release_token(invalid, SHA, SHA)
        with self.assertRaisesRegex(ValueError, 'admission blocked'):
            automation.release_token(state, SHA, OTHER)


class FrozenArtifactTests(unittest.TestCase):
    def test_browser_source_channel_completeness_and_content_are_checked(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build = browser_fixture(root)
            automation.verify_browser(root, 'staging', SHA)
            for channel, sha in [('stable', SHA), ('staging', OTHER)]:
                with self.assertRaisesRegex(ValueError, 'channel/source'):
                    automation.verify_browser(root, channel, sha)
            build['releaseBuild'] = 'ON'
            automation.write_json(root / 'build.json', build)
            with self.assertRaisesRegex(ValueError, 'official identity'):
                automation.verify_browser(root, 'staging', SHA)
            build['releaseBuild'] = 'OFF'
            build['artifacts'].remove('dunecity.js')
            automation.write_json(root / 'build.json', build)
            with self.assertRaisesRegex(ValueError, 'incomplete runtime'):
                automation.verify_browser(root, 'staging', SHA)
            build['artifacts'].append('dunecity.js')
            automation.write_json(root / 'build.json', build)
            (root / 'dunecity.wasm').write_bytes(b'wrong source bytes')
            with self.assertRaisesRegex(ValueError, 'hash mismatch'):
                automation.verify_browser(root, 'staging', SHA)

    def test_archive_unsafe_members_are_rejected_before_extraction(self):
        cases = [('../outside', tarfile.REGTYPE), ('/absolute', tarfile.REGTYPE),
                 ('engine', tarfile.SYMTYPE), ('engine', tarfile.LNKTYPE),
                 ('engine', tarfile.FIFOTYPE), ('engine', tarfile.CHRTYPE)]
        for name, kind in cases:
            with self.subTest(name=name, kind=kind), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                archive = root / 'package.tar.gz'
                with tarfile.open(archive, 'w:gz') as output:
                    safe = tarfile.TarInfo('safe-prefix')
                    safe.size = 4
                    output.addfile(safe, io.BytesIO(b'data'))
                    member = tarfile.TarInfo(name)
                    member.type = kind
                    member.linkname = '/etc/passwd'
                    if member.isfile():
                        member.size = 4
                        output.addfile(member, io.BytesIO(b'data'))
                    else:
                        output.addfile(member)
                with self.assertRaises(ValueError):
                    automation.extract_browser(archive, root / 'restore')
                self.assertFalse((root / 'outside').exists())
                self.assertFalse((root / 'restore/safe-prefix').exists())

    def test_regular_archive_payloads_extract_without_applying_unsafe_modes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            restore = root / 'restore'
            restore.mkdir(mode=0o700)
            archive = root / 'package.tar.gz'
            payload = b'frozen browser payload' * 65536
            with tarfile.open(archive, 'w:gz') as output:
                for name in ('.', 'runtime'):
                    member = tarfile.TarInfo(name)
                    member.type = tarfile.DIRTYPE
                    member.mode = 0o7777
                    output.addfile(member)
                for name in ('runtime/dunecity.wasm', 'implicit/nested/dunecity.data'):
                    member = tarfile.TarInfo(name)
                    member.size = len(payload)
                    member.mode = 0o7777
                    output.addfile(member, io.BytesIO(payload))
            automation.extract_browser(archive, restore)
            self.assertEqual(restore.stat().st_mode & 0o7777, 0o700)
            for name in ('runtime/dunecity.wasm', 'implicit/nested/dunecity.data'):
                path = restore / name
                self.assertEqual(path.read_bytes(), payload)
                self.assertEqual(path.stat().st_mode & 0o7111, 0)
                self.assertEqual(path.stat().st_mode & 0o022, 0)
            for name in ('runtime', 'implicit', 'implicit/nested'):
                self.assertEqual((restore / name).stat().st_mode & 0o7022, 0)

    def test_archive_cannot_follow_existing_destination_symlinks(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            restore, outside = root / 'restore', root / 'outside'
            restore.mkdir()
            outside.mkdir()
            (restore / 'runtime').symlink_to(outside, target_is_directory=True)
            archive = root / 'package.tar.gz'
            with tarfile.open(archive, 'w:gz') as output:
                member = tarfile.TarInfo('runtime/dunecity.wasm')
                member.size = 4
                output.addfile(member, io.BytesIO(b'data'))
            with self.assertRaises(ValueError):
                automation.extract_browser(archive, restore)
            self.assertEqual(list(outside.iterdir()), [])

    def test_manifest_refuses_partial_platform_success_and_mixed_sources(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            args = argparse.Namespace(directory=root, sha=SHA, stable=False, tag=automation.snapshot_tag(SHA))
            with self.assertRaisesRegex(ValueError, 'Missing required'):
                automation.make_manifest(args)
            for name in automation.desktop_names(VERSION) | {automation.BROWSER, 'DuneCity-p2p-service.tar.gz'}:
                (root / name).write_bytes(('package ' + name).encode())
            for platform in ('windows-x64', 'macos-arm64', 'linux-x86_64', 'emscripten'):
                automation.write_json(root / f'build-inputs-{platform}.json', dict(
                    sourceCommit=SHA if platform != 'macos-arm64' else OTHER,
                    releaseBuild='OFF', configuration={'DUNECITY_RELEASE_BUILD:BOOL': 'OFF'}))
            with self.assertRaisesRegex(ValueError, 'identity mismatch: macos-arm64'):
                automation.make_manifest(args)

    def test_published_set_rejects_corruption_and_a_different_source(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            payload = root / 'package.zip'
            payload.write_bytes(b'frozen source')
            manifest = dict(tag=automation.snapshot_tag(SHA), sourceCommit=SHA,
                            sha256={'package.zip': automation.digest(payload)})
            automation.verify_manifest(root, manifest, manifest['tag'], SHA)
            extra = root / 'unfrozen.zip'
            extra.write_bytes(b'unqualified input')
            with self.assertRaisesRegex(ValueError, 'asset set differs'):
                automation.verify_manifest(root, manifest, manifest['tag'], SHA)
            extra.unlink()
            with self.assertRaisesRegex(ValueError, 'selected commit'):
                automation.verify_manifest(root, manifest, manifest['tag'], OTHER)
            payload.write_bytes(b'replaced data')
            with self.assertRaisesRegex(ValueError, 'hash mismatch'):
                automation.verify_manifest(root, manifest, manifest['tag'], SHA)

    def test_real_updater_signatures_bind_archive_version_url_and_appcast(self):
        key = Ed25519PrivateKey.generate()
        public = key.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for platform, name in [('macos-arm64', f'DuneCity-{VERSION}-macOS.zip'),
                                   ('windows-x64', f'DuneCity-{VERSION}-Windows-x64.exe'),
                                   ('linux-x86_64', f'DuneCity-{VERSION}-Linux-x86_64.AppImage')]:
                archive = root / name
                archive.write_bytes(('archive ' + platform).encode())
                updates.prepare(VERSION, platform, archive, 'https://github.com/example/game/releases/download/v' + VERSION, key, root)
            paths = automation.asset_paths(root)
            automation.verify_updates(paths, VERSION, repo='example/game', public_key=public)
            (root / f'DuneCity-{VERSION}-macOS.zip').write_bytes(b'other signed-version package')
            with self.assertRaisesRegex(ValueError, 'disagrees with frozen package'):
                automation.verify_updates(paths, VERSION, repo='example/game', public_key=public)

    def test_failed_build_resume_never_reruns_successful_platforms(self):
        source = dict(path='.github/workflows/build.yml', event='push', head_branch='v' + VERSION,
                      display_title='CI stable')
        with patch.object(automation, 'api', return_value=source), \
             patch.object(automation.subprocess, 'run') as command:
            automation.resume(argparse.Namespace(channel='build', run_id='123', tag=None))
            self.assertIn('--failed', command.call_args.args[0])
            self.assertNotIn('--all', command.call_args.args[0])
            self.assertNotIn('workflow', command.call_args.args[0])
        source['event'] = 'pull_request'
        with patch.object(automation, 'api', return_value=source), \
             patch.object(automation.subprocess, 'run') as command:
            with self.assertRaisesRegex(ValueError, 'not a trusted'):
                automation.resume(argparse.Namespace(channel='build', run_id='123', tag=None))
            command.assert_not_called()


    def test_complete_staging_manifest_persists_frozen_run_artifact_ids_and_hashes(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            dist = base / 'dist'
            dist.mkdir()
            browser = base / 'browser'
            browser_fixture(browser)
            for name in automation.desktop_names(VERSION):
                (dist / name).write_bytes(('platform package ' + name).encode())
            (dist / 'DuneCity-p2p-service.tar.gz').write_bytes(b'frozen matching signaling source')
            with tarfile.open(dist / automation.BROWSER, 'w:gz') as archive:
                for path in browser.iterdir():
                    archive.add(path, arcname=path.name)
            for platform in ('windows-x64', 'macos-arm64', 'linux-x86_64', 'emscripten'):
                automation.write_json(dist / f'build-inputs-{platform}.json', dict(
                    sourceCommit=SHA, releaseBuild='OFF',
                    configuration={'DUNECITY_RELEASE_BUILD:BOOL': 'OFF'}))
            artifact = dict(id=71, name='DuneCity-Emscripten', digest='sha256:' + 'c' * 64,
                            size_in_bytes=123, expired=False)
            def response(path):
                return ({'artifacts': [artifact]} if '/artifacts?' in path else
                        {'id': 41, 'run_attempt': 1, 'head_sha': OTHER,
                         'event': 'workflow_dispatch', 'head_branch': automation.STAGING,
                         'path': automation.BUILD_WORKFLOW})
            args = argparse.Namespace(directory=dist, sha=SHA, stable=False, tag=automation.snapshot_tag(SHA))
            with patch.dict(os.environ, GITHUB_RUN_ID='41'), \
                 patch.object(automation, 'api', side_effect=response), \
                 patch.object(automation, 'qualification_for', return_value={
                     'id': 7, 'run_attempt': 1, 'head_sha': SHA}):
                automation.make_manifest(args)
            manifest = json.loads((dist / automation.MANIFEST).read_text())
            self.assertEqual((manifest['sourceCommit'], manifest['releaseBuild'], manifest['runId']), (SHA, 'OFF', 41))
            self.assertEqual(manifest['artifacts'][0]['id'], 71)
            self.assertEqual(manifest['qualificationSourceCommit'], SHA)
            self.assertEqual(manifest['qualificationRunId'], 7)
            self.assertEqual(manifest['producerSourceCommit'], OTHER)
            automation.verify_manifest(dist, manifest, args.tag, SHA)

    def test_recovery_after_publication_is_read_only_and_rejects_changed_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            package = root / 'package.zip'
            package.write_bytes(b'frozen bytes')
            manifest = dict(tag=automation.snapshot_tag(SHA), sourceCommit=SHA, releaseBuild='OFF',
                            sha256={'package.zip': automation.digest(package)})
            automation.write_json(root / automation.MANIFEST, manifest)
            original = json.loads(json.dumps(manifest))
            existing = dict(draft=False, prerelease=True, assets=[
                dict(name='package.zip', digest='sha256:' + original['sha256']['package.zip'])])
            def restore(tag, destination, names):
                automation.write_json(destination / automation.MANIFEST, original)
                return existing
            args = argparse.Namespace(directory=root, sha=SHA, tag=manifest['tag'])
            with patch.object(automation, 'optional_release', return_value=existing), \
                 patch.object(automation, 'resolve_tag', return_value=SHA), \
                 patch.object(automation, 'download_release', side_effect=restore), \
                 patch.object(automation.subprocess, 'run') as command:
                automation.publish(args)
                command.assert_not_called()
                package.write_bytes(b'changed bytes')
                manifest['sha256']['package.zip'] = automation.digest(package)
                automation.write_json(root / automation.MANIFEST, manifest)
                with self.assertRaisesRegex(ValueError, 'Refusing to change a published release'):
                    automation.publish(args)
                command.assert_not_called()

    def test_staging_browser_retry_uses_the_exact_published_snapshot_not_latest(self):
        tag = automation.snapshot_tag(SHA)
        release = dict(draft=False, prerelease=True, assets=[dict(name=automation.BROWSER)])
        with patch.object(automation, 'optional_release', return_value=release), \
             patch.object(automation.subprocess, 'run') as command:
            automation.resume(argparse.Namespace(channel='browser', run_id=None, tag=tag))
            argv = command.call_args.args[0]
            self.assertIn('web.yml', argv)
            self.assertIn('tag=' + tag, argv)
            self.assertNotIn('build.yml', argv)
        with patch.object(automation.subprocess, 'run') as command:
            with self.assertRaisesRegex(ValueError, 'exact published'):
                automation.resume(argparse.Namespace(channel='sourceforge', run_id=None, tag=tag))
            command.assert_not_called()

    def test_failed_overall_run_does_not_hide_a_successful_immutable_publication(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            tag = 'v' + VERSION
            event = dict(workflow_run=dict(id=42, event='push', conclusion='failure',
                path='.github/workflows/build.yml', head_repository=dict(full_name=automation.repository()),
                head_branch=tag, head_sha=SHA))
            event_path = root / 'event.json'
            automation.write_json(event_path, event)
            output = root / 'inputs'
            def restore(*args, **kwargs):
                automation.write_json(output / automation.MANIFEST,
                    dict(runId=42, tag=tag, sourceCommit=SHA))
            with patch.dict(os.environ, GITHUB_EVENT_NAME='workflow_run',
                            GITHUB_EVENT_PATH=str(event_path), GITHUB_OUTPUT='', GITHUB_STEP_SUMMARY=''), \
                 patch.object(automation, 'api', return_value={'artifacts': [
                     dict(id=71, name='DuneCity-Publication-inputs-1', expired=False)]}), \
                 patch.object(automation, 'optional_release', return_value={'draft': False}), \
                 patch.object(automation.subprocess, 'run', side_effect=restore), \
                 redirect_stdout(io.StringIO()) as text:
                automation.select_publication(argparse.Namespace(tag='', directory=output, stable_only=True))
            self.assertIn('eligible=true', text.getvalue())
            self.assertIn('tag=' + tag, text.getvalue())


    def test_legacy_browser_resume_freezes_existing_website_bytes_without_a_game_build(self):
        release = dict(draft=False, prerelease=False, assets=[])
        build = dict(version=VERSION, sourceCommit=SHA)
        def response(path):
            if '/git/ref/heads/main' in path:
                return {'object': {'sha': OTHER}}
            return {'content': base64.b64encode(json.dumps(build).encode()).decode()}
        args = argparse.Namespace(channel='browser', tag='v' + VERSION, run_id=None)
        with patch.object(automation, 'optional_release', return_value=release), \
             patch.object(automation, 'api', side_effect=response), \
             patch.object(automation, 'resolve_tag', return_value=SHA), \
             patch.object(automation.subprocess, 'run') as command:
            automation.resume(args)
            argv = command.call_args.args[0]
            self.assertIn('deploy-metaserver-droplet.yml', argv)
            self.assertIn('source_sha=' + OTHER, argv)
            self.assertNotIn('build.yml', argv)
            self.assertNotIn('release', argv)
            command.reset_mock()
            build['sourceCommit'] = OTHER
            with self.assertRaisesRegex(ValueError, 'exact legacy release'):
                automation.resume(args)
            command.assert_not_called()

    def test_deploy_retry_requires_a_retained_frozen_receipt(self):
        receipt = dict(name='Website-deployment-inputs-1', expired=False)
        def response(path):
            if '/artifacts?' in path:
                return {'artifacts': [receipt]}
            return dict(path='.github/workflows/deploy-metaserver-droplet.yml', event='push')
        args = argparse.Namespace(channel='deploy', tag=None, run_id='123')
        with patch.object(automation, 'api', side_effect=response), \
             patch.object(automation.subprocess, 'run') as command:
            automation.resume(args)
            self.assertIn('--failed', command.call_args.args[0])
            self.assertIn('VR48/dunelegacy.com', command.call_args.args[0])
            command.reset_mock()
            receipt['expired'] = True
            with self.assertRaisesRegex(ValueError, 'frozen input receipt'):
                automation.resume(args)
            command.assert_not_called()


class DraftPublicationRecoveryTests(unittest.TestCase):
    """Exercise real publisher/consumer code, replacing only GitHub's transport."""

    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        environment = patch.dict(os.environ, GITHUB_ACTIONS='', GITHUB_JOB='',
                                 DUNECITY_STAGING_LIFECYCLE='', RUNNER_TEMP='', GITHUB_STEP_SUMMARY='')
        environment.start()
        self.addCleanup(environment.stop)
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.tag = automation.snapshot_tag(SHA)
        names = automation.desktop_names(VERSION) | {automation.BROWSER, 'DuneCity-p2p-service.tar.gz'}
        names |= {f'build-inputs-{platform}.json' for platform in
                  ('windows-x64', 'macos-arm64', 'linux-x86_64', 'emscripten')}
        for name in names:
            (self.root / name).write_bytes(('frozen package ' + name).encode())
        self.manifest = dict(schema=1, tag=self.tag, sourceCommit=SHA, version=VERSION,
            repository=automation.repository(), releaseBuild='OFF', recipe=automation.RECIPE,
            inputIdentity=hashlib.sha256((SHA + '\n' + automation.RECIPE).encode()).hexdigest(),
            runId=41, runAttempt=1, producerSourceCommit=SHA, producerEvent='workflow_dispatch',
            producerHeadBranch=automation.STAGING, producerWorkflowPath=automation.BUILD_WORKFLOW,
            qualificationRunId=11, qualificationRunAttempt=1, qualificationSourceCommit=SHA,
            sha256={name: automation.digest(self.root / name) for name in sorted(names)})
        automation.write_json(self.root / automation.MANIFEST, self.manifest)
        (self.root / 'SHA256SUMS').write_text(''.join(
            f'{checksum}  {name}\n' for name, checksum in self.manifest['sha256'].items()))
        self.original_manifest = (self.root / automation.MANIFEST).read_bytes()
        self.release = dict(id=443652159, tag_name=self.tag, target_commitish=SHA,
                            draft=True, prerelease=True,
                            assets=[self.asset(path) for path in sorted(self.root.iterdir())])
        self.assertEqual(len(self.release['assets']), 16)
        self.template = copy.deepcopy(self.release)
        self.pointer = dict(id=442990483, tag_name='latest-dev', target_commitish=OTHER,
                            draft=False, prerelease=True, body='Historical Linux staging from ' + OTHER,
                            assets=[dict(name=f'legacy-linux-{number}', size=10) for number in range(4)])
        self.previous_releases = {}
        self.ref_sha = None
        self.ref_name = 'refs/tags/' + self.tag
        self.pages = None
        self.failures = {}
        self.commands = []
        self.mutations = []
        self.prefix = f'repos/{automation.repository()}/'
        self.qualification = dict(id=11, run_attempt=1, status='completed', conclusion='success',
            path=automation.BUILD_WORKFLOW, head_sha=SHA, head_branch=automation.STAGING,
            event='push', repository={'full_name': automation.repository()})
        self.producer = dict(self.qualification, id=41, conclusion='failure', event='workflow_dispatch',
                             display_title='Staging packages ' + SHA)
        self.retry = dict(self.producer, run_attempt=2, conclusion='success')
        self.input_jobs = [dict(name=name, conclusion='success') for name in sorted(automation.CI_JOBS)]
        self.producer_jobs = self.input_jobs + [dict(name='dev-release', conclusion='failure')]
        self.retry_jobs = [dict(name='dev-release', conclusion='success')]
        self.responses = {
            'actions/runs/11/attempts/1': self.qualification,
            'actions/runs/11/attempts/1/jobs?per_page=100': {'jobs': copy.deepcopy(self.input_jobs)},
            'actions/runs/41/attempts/1': self.producer,
            'actions/runs/41/attempts/1/jobs?per_page=100': {'jobs': self.producer_jobs},
            'actions/runs/41': self.retry,
            'actions/runs/41/attempts/2': self.retry,
            'actions/runs/41/attempts/2/jobs?per_page=100': {'jobs': self.retry_jobs},
        }
        transport = patch.object(automation.subprocess, 'run', side_effect=self.command)
        transport.start()
        self.addCleanup(transport.stop)
        retention = patch.object(automation, 'prune_staging')
        self.retention_patch = retention
        self.retention = retention.start()
        self.addCleanup(retention.stop)

    def asset(self, path):
        return dict(name=path.name, size=path.stat().st_size,
                    digest='sha256:' + automation.digest(path))

    def command(self, argv, **kwargs):
        argv = tuple(str(value) for value in argv)
        self.commands.append(argv)
        value, error, code = {}, '', 0
        if argv[:2] == ('gh', 'api'):
            method = argv[3] if argv[2] == '--method' else 'GET'
            path = argv[4] if method != 'GET' else argv[2]
            suffix = path.removeprefix(self.prefix)
            if method + ' ' + suffix in self.failures:
                code, error = self.failures[method + ' ' + suffix]
            elif method == 'POST':
                self.assertEqual(suffix, 'git/refs')
                self.assertIsNone(self.ref_sha)
                self.assertIn('ref=refs/tags/' + self.tag, argv)
                self.assertIn('sha=' + SHA, argv)
                self.assertNotIn('--force', argv)
                self.ref_sha = SHA
                self.mutations.append((method, suffix))
            elif method == 'PATCH':
                if suffix == 'releases/442990483':
                    self.assertEqual(argv[5], '-f')
                    self.assertEqual(len(argv), 7)
                    self.assertTrue(argv[6].startswith('body='))
                    self.pointer['body'] = argv[6].removeprefix('body=')
                    self.mutations.append(('POINTER', 'latest-dev'))
                    value = self.pointer
                else:
                    self.assertEqual(suffix, 'releases/443652159')
                    self.assertIn('draft=false', argv)
                    self.assertIn('make_latest=false', argv)
                    self.release['draft'] = False
                    self.mutations.append((method, suffix))
                    value = self.release
            elif suffix in self.failures:
                code, error = self.failures[suffix]
            elif suffix == 'releases/tags/' + self.tag:
                if self.release is None or self.release['draft']:
                    code, error = 1, 'gh: Not Found (HTTP 404)'
                else:
                    value = self.release
            elif suffix == 'releases/tags/latest-dev':
                if self.pointer is None:
                    code, error = 1, 'gh: Not Found (HTTP 404)'
                else:
                    value = self.pointer
            elif suffix.startswith('releases/tags/') and suffix.removeprefix('releases/tags/') in self.previous_releases:
                value = self.previous_releases[suffix.removeprefix('releases/tags/')]
            elif suffix.startswith('releases?per_page=100&page='):
                page = int(suffix.rsplit('=', 1)[1])
                if self.pages is None:
                    value = [self.release] if self.release else []
                else:
                    value = self.pages[page - 1] if page <= len(self.pages) else []
            elif suffix == 'git/ref/tags/' + self.tag:
                if self.ref_sha is None:
                    code, error = 1, 'gh: Not Found (HTTP 404)'
                else:
                    value = dict(ref=self.ref_name, object=dict(type='commit', sha=self.ref_sha))
            elif suffix == 'git/ref/heads/' + automation.STAGING:
                value = {'object': {'sha': SHA}}
            else:
                self.assertIn(suffix, self.responses)
                value = self.responses[suffix]
        elif argv[:3] == ('gh', 'release', 'create'):
            if argv[3] == 'latest-dev':
                self.assertIsNone(self.pointer)
                self.assertIn('--prerelease', argv)
                self.assertIn('--latest=false', argv)
                self.assertNotIn('--draft', argv)
                self.assertEqual(argv[argv.index('--target') + 1], SHA)
                self.pointer = dict(id=442990483, tag_name='latest-dev', target_commitish=SHA,
                                    draft=False, prerelease=True, assets=[],
                                    body=argv[argv.index('--notes') + 1])
                self.mutations.append(('CREATE_POINTER', 'latest-dev'))
            else:
                self.assertIsNone(self.release)
                self.assertEqual(self.ref_sha, SHA)
                self.assertIn('--draft', argv)
                self.assertEqual(argv[argv.index('--target') + 1], SHA)
                self.release = copy.deepcopy(self.template)
                self.mutations.append(('CREATE', self.tag))
        elif argv[:3] == ('gh', 'release', 'upload'):
            self.assertNotIn('--clobber', argv)
            for filename in argv[3:]:
                if filename.startswith(str(self.root) + '/'):
                    item = self.asset(Path(filename))
                    self.assertNotIn(item['name'], {asset['name'] for asset in self.release['assets']})
                    self.release['assets'].append(item)
            self.mutations.append(('UPLOAD', self.tag))
        elif argv[:3] == ('gh', 'run', 'rerun'):
            self.assertEqual(argv[3], '41')
            self.assertIn('--failed', argv)
            self.assertNotIn('--all', argv)
            self.mutations.append(('RERUN_FAILED', '41'))
        elif argv[:3] == ('gh', 'release', 'download'):
            self.assertEqual(argv[argv.index('--pattern') + 1], automation.MANIFEST)
            folder = Path(argv[argv.index('--dir') + 1])
            (folder / automation.MANIFEST).write_bytes(self.original_manifest)
        else:
            self.fail('Unexpected GitHub command: ' + repr(argv))
        if code and kwargs.get('check'):
            raise subprocess.CalledProcessError(code, argv, output='', stderr=error)
        return subprocess.CompletedProcess(argv, code, json.dumps(value), error)

    def publish(self):
        return automation.publish(argparse.Namespace(directory=self.root, sha=SHA, tag=self.tag))

    def qualified(self):
        self.release['draft'] = False
        self.ref_sha = SHA
        return automation.qualified_staging_snapshot(self.tag, SHA)[0]

    def admit_pointer_writer(self, attempt=2):
        environment = patch.dict(os.environ, GITHUB_ACTIONS='true', GITHUB_JOB='dev-release',
                                 DUNECITY_STAGING_LIFECYCLE='true', GITHUB_RUN_ID='41',
                                 GITHUB_RUN_ATTEMPT=str(attempt))
        environment.start()
        self.addCleanup(environment.stop)
        live = dict(self.retry, status='in_progress', conclusion=None, run_attempt=attempt)
        self.responses['actions/runs/41'] = live
        self.responses[f'actions/runs/41/attempts/{attempt}'] = live
        jobs = [dict(name='dev-release', status='in_progress', conclusion=None)]
        if attempt == 1:
            jobs = copy.deepcopy(self.input_jobs) + jobs
        self.responses[f'actions/runs/41/attempts/{attempt}/jobs?per_page=100'] = {'jobs': jobs}

    def previous_pointer(self, status='ahead', source=OTHER):
        tag = automation.snapshot_tag(OTHER)
        self.previous_releases[tag] = dict(tag_name=tag, draft=False, prerelease=True)
        self.responses['git/ref/tags/' + tag] = dict(object=dict(type='commit', sha=source))
        self.responses[f'compare/{source}...{SHA}'] = dict(status=status)
        self.pointer['body'] = automation.staging_pointer_body(self.pointer['body'], tag)
        return tag

    def reconcile_pointer(self):
        self.release['draft'] = False
        self.ref_sha = SHA
        with redirect_stdout(io.StringIO()):
            return automation.advance_staging_pointer(self.tag, SHA)

    def test_verified_first_publication_initializes_legacy_pointer_without_relabelling_old_assets(self):
        self.admit_pointer_writer(attempt=1)
        legacy = copy.deepcopy(self.pointer)
        assets = copy.deepcopy(self.release['assets'])
        with redirect_stdout(io.StringIO()):
            result = self.publish()
        self.assertEqual(result['pointer']['status'], 'advanced')
        self.assertEqual(automation.staging_pointer_tag(self.pointer['body']), self.tag)
        self.assertIn(legacy['body'], self.pointer['body'])
        self.assertIn(f'https://github.com/{automation.repository()}/releases/tag/{self.tag}', self.pointer['body'])
        for key in ('id', 'tag_name', 'target_commitish', 'assets'):
            self.assertEqual(self.pointer[key], legacy[key])
        self.assertEqual(self.release['assets'], assets)
        self.assertEqual((self.root / automation.MANIFEST).read_bytes(), self.original_manifest)
        self.assertEqual(self.mutations, [
            ('POST', 'git/refs'), ('PATCH', 'releases/443652159'), ('POINTER', 'latest-dev')])

    def test_existing_published_retry_reconciles_pointer_and_is_idempotent_without_changing_input_bytes(self):
        self.admit_pointer_writer()
        self.release['draft'] = False
        self.ref_sha = SHA
        frozen = copy.deepcopy(self.release)
        # The retry's regenerated local receipt must never replace frozen attempt one.
        self.manifest['runAttempt'] = 2
        automation.write_json(self.root / automation.MANIFEST, self.manifest)
        with redirect_stdout(io.StringIO()):
            first = self.publish()
            body = self.pointer['body']
            second = self.publish()
        self.assertEqual(first['pointer']['status'], 'advanced')
        self.assertEqual(second['pointer']['status'], 'unchanged')
        self.assertEqual(self.pointer['body'], body)
        self.assertEqual(self.pointer['body'].count('dunecity-staging-tag'), 1)
        self.assertEqual(self.release, frozen)
        self.assertEqual(self.mutations, [('POINTER', 'latest-dev')])
        self.assertEqual(json.loads(self.original_manifest)['runAttempt'], 1)

    def test_ahead_source_replaces_managed_link_and_unique_marker_preserving_history(self):
        self.admit_pointer_writer()
        previous = self.previous_pointer()
        legacy_assets = copy.deepcopy(self.pointer['assets'])
        result = self.reconcile_pointer()
        self.assertEqual(result['status'], 'advanced')
        self.assertEqual(result['previousTag'], previous)
        self.assertEqual(automation.staging_pointer_tag(self.pointer['body']), self.tag)
        self.assertNotIn(previous, self.pointer['body'])
        self.assertEqual(self.pointer['body'].count(automation.POINTER_BEGIN), 1)
        self.assertIn('Historical Linux staging from ' + OTHER, self.pointer['body'])
        self.assertEqual(self.pointer['assets'], legacy_assets)

    def test_stale_diverged_unknown_or_misidentified_previous_source_preserves_pointer(self):
        self.admit_pointer_writer()
        for status in ('behind', 'diverged', 'identical', 'unknown'):
            with self.subTest(status=status):
                self.previous_pointer(status)
                frozen = copy.deepcopy(self.pointer)
                result = self.reconcile_pointer()
                self.assertEqual(result['status'], 'held')
                self.assertIn(status, result['reason'])
                self.assertEqual(self.pointer, frozen)
        self.previous_pointer(source='c' * 40)
        frozen = copy.deepcopy(self.pointer)
        self.assertEqual(self.reconcile_pointer()['status'], 'held')
        self.assertEqual(self.pointer, frozen)
        self.assertEqual(self.mutations, [])

    def test_same_source_different_inputs_or_unpublished_previous_target_is_uncertain_not_replaceable(self):
        self.admit_pointer_writer()
        tag = automation.snapshot_tag(SHA, 'older-input-recipe')
        self.pointer['body'] = f'<!-- dunecity-staging-tag: {tag} -->'
        self.previous_releases[tag] = dict(tag_name=tag, draft=False, prerelease=True)
        self.responses['git/ref/tags/' + tag] = dict(object=dict(type='commit', sha=SHA))
        frozen = copy.deepcopy(self.pointer)
        result = self.reconcile_pointer()
        self.assertEqual(result['status'], 'held')
        self.assertIn('different immutable inputs', result['reason'])
        self.assertEqual(self.pointer, frozen)
        for field, value in (('draft', True), ('prerelease', False)):
            with self.subTest(field=field), patch.dict(self.previous_releases[tag], {field: value}):
                self.assertEqual(self.reconcile_pointer()['status'], 'held')
                self.assertEqual(self.pointer, frozen)
        self.assertEqual(self.mutations, [])

    def test_malformed_or_multiple_markers_and_managed_blocks_never_initialize_or_overwrite(self):
        self.admit_pointer_writer()
        marker = f'<!-- dunecity-staging-tag: {self.tag} -->'
        for body in (marker + marker, '<!-- dunecity-staging-tag: invalid -->', 0, [],
                     marker + '<!-- dunecity-staging-tag: invalid -->',
                     automation.POINTER_BEGIN, automation.POINTER_END + marker,
                     automation.POINTER_BEGIN + marker + automation.POINTER_BEGIN + automation.POINTER_END):
            with self.subTest(body=body):
                self.pointer['body'] = body
                frozen = copy.deepcopy(self.pointer)
                result = self.reconcile_pointer()
                self.assertEqual(result['status'], 'held')
                self.assertEqual(self.pointer, frozen)
        self.assertEqual(self.mutations, [])

    def test_pointer_lookup_compare_and_patch_api_failures_preserve_previous_marker_with_actual_reason(self):
        self.admit_pointer_writer()
        self.previous_pointer()
        for endpoint in ('releases/tags/latest-dev', f'compare/{OTHER}...{SHA}',
                         'PATCH releases/442990483'):
            with self.subTest(endpoint=endpoint):
                self.failures = {endpoint: (1, 'gh: Forbidden (HTTP 403)')}
                frozen = copy.deepcopy(self.pointer)
                result = self.reconcile_pointer()
                self.assertEqual(result['status'], 'held')
                self.assertIn('HTTP 403', result['reason'])
                self.assertEqual(self.pointer, frozen)
        self.assertEqual(self.mutations, [])

    def test_missing_pointer_release_is_created_as_nonlatest_prerelease_only(self):
        self.admit_pointer_writer()
        self.pointer = None
        result = self.reconcile_pointer()
        self.assertEqual(result['status'], 'advanced')
        self.assertTrue(self.pointer['prerelease'])
        self.assertFalse(self.pointer['draft'])
        self.assertEqual(self.pointer['assets'], [])
        self.assertEqual(automation.staging_pointer_tag(self.pointer['body']), self.tag)
        self.assertEqual(self.mutations, [('CREATE_POINTER', 'latest-dev')])

    def test_missing_serialization_or_wrong_lifecycle_owner_holds_pointer_and_all_retention(self):
        self.retention_patch.stop()
        self.release['draft'] = False
        self.ref_sha = SHA
        frozen = copy.deepcopy(self.pointer)
        with redirect_stdout(io.StringIO()):
            result = self.publish()
        self.assertEqual(result['pointer']['status'], 'held')
        self.assertIn('admitted Game staging-lifecycle', result['pointer']['reason'])
        self.assertEqual(result['retention']['deleted'], [])
        self.assertEqual(result['retention']['eligible'], [])
        self.assertIn(result['pointer']['reason'], result['retention']['reason'])
        self.assertEqual(self.pointer, frozen)
        self.admit_pointer_writer()
        for field, value in (('id', 42), ('run_attempt', 3), ('status', 'completed'),
                             ('path', '.github/workflows/other.yml'),
                             ('repository', {'full_name': 'wrong/repository'})):
            with self.subTest(field=field), patch.dict(self.responses['actions/runs/41'], {field: value}):
                self.assertEqual(self.reconcile_pointer()['status'], 'held')
                self.assertEqual(self.pointer, frozen)
        self.assertEqual(self.mutations, [])

    def test_unverified_snapshot_assets_qualification_and_live_publication_cannot_advance_pointer(self):
        self.admit_pointer_writer()
        frozen = copy.deepcopy(self.pointer)
        for name in (automation.BROWSER, 'DuneCity-p2p-service.tar.gz', 'SHA256SUMS'):
            item = next(asset for asset in self.release['assets'] if asset['name'] == name)
            with self.subTest(name=name), patch.dict(item, digest='sha256:' + '0' * 64):
                self.assertEqual(self.reconcile_pointer()['status'], 'held')
                self.assertEqual(self.pointer, frozen)
        with patch.dict(self.qualification, conclusion='failure'):
            self.assertEqual(self.reconcile_pointer()['status'], 'held')
        live = self.responses['actions/runs/41/attempts/2/jobs?per_page=100']['jobs']
        for conclusion in ('failure', 'skipped', 'success'):
            with self.subTest(conclusion=conclusion), patch.dict(live[0], conclusion=conclusion):
                self.assertEqual(self.reconcile_pointer()['status'], 'held')
        live.append(dict(name='build-linux', conclusion='failure'))
        self.assertEqual(self.reconcile_pointer()['status'], 'held')
        self.assertEqual(self.pointer, frozen)
        self.assertEqual(self.mutations, [])

    def test_live_first_input_attempt_cannot_publish_pointer_with_failed_skipped_or_missing_inputs(self):
        self.admit_pointer_writer(attempt=1)
        jobs = self.responses['actions/runs/41/attempts/1/jobs?per_page=100']['jobs']
        frozen = copy.deepcopy(self.pointer)
        for conclusion in ('failure', 'skipped', None):
            with self.subTest(conclusion=conclusion), patch.dict(jobs[0], conclusion=conclusion):
                self.assertEqual(self.reconcile_pointer()['status'], 'held')
                self.assertEqual(self.pointer, frozen)
        jobs.pop(0)
        self.assertEqual(self.reconcile_pointer()['status'], 'held')
        self.assertEqual(self.pointer, frozen)
        self.assertEqual(self.mutations, [])

    def test_draft_404_on_later_page_resumes_by_id_without_rewriting_any_frozen_asset(self):
        unrelated = dict(tag_name=automation.snapshot_tag(OTHER), draft=True)
        self.pages = [[unrelated] * 100, [self.release]]
        assets = copy.deepcopy(self.release['assets'])
        self.publish()
        self.assertEqual(self.mutations, [('POST', 'git/refs'), ('PATCH', 'releases/443652159')])
        self.assertEqual(self.release['assets'], assets)
        self.assertEqual((self.root / automation.MANIFEST).read_bytes(), self.original_manifest)
        self.assertFalse(any(argv[:2] == ('gh', 'release') for argv in self.commands))
        self.assertTrue(any('releases?per_page=100&page=2' in argv[-1] for argv in self.commands))

    def test_new_draft_gets_only_its_exact_staging_ref_before_creation(self):
        self.release = None
        self.publish()
        self.assertEqual(self.mutations, [
            ('POST', 'git/refs'), ('CREATE', self.tag), ('PATCH', 'releases/443652159')])
        self.assertEqual(self.ref_sha, SHA)

    def test_duplicate_drafts_on_different_pages_are_an_error_before_any_mutation(self):
        unrelated = dict(tag_name=automation.snapshot_tag(OTHER), draft=True)
        self.pages = [[copy.deepcopy(self.release)] + [unrelated] * 99, [self.release]]
        with self.assertRaisesRegex(ValueError, 'Ambiguous release tag'):
            self.publish()
        self.assertEqual(self.mutations, [])

    def test_unrelated_draft_is_not_selected_and_listing_disagreement_is_not_hidden(self):
        self.pages = [[dict(self.release, tag_name=automation.snapshot_tag(OTHER))]]
        self.assertIsNone(automation.optional_release(self.tag))
        self.pages = [[dict(self.release, draft=False)]]
        with self.assertRaisesRegex(ValueError, 'disagrees'):
            automation.optional_release(self.tag)
        self.assertEqual(self.mutations, [])

    def test_draft_target_or_existing_ref_mismatch_never_retags_or_publishes(self):
        self.release['target_commitish'] = OTHER
        with self.assertRaisesRegex(ValueError, 'source differs'):
            self.publish()
        self.release['target_commitish'] = SHA
        self.ref_sha = OTHER
        with self.assertRaisesRegex(ValueError, 'Immutable staging tag'):
            self.publish()
        self.ref_sha = SHA
        self.ref_name = 'refs/tags/wrong'
        with self.assertRaisesRegex(ValueError, 'Ambiguous immutable staging tag'):
            self.publish()
        self.assertEqual(self.mutations, [])

    def test_auth_network_and_listing_errors_are_not_treated_as_absent_drafts(self):
        endpoint = 'releases/tags/' + self.tag
        for error in ('gh: Forbidden (HTTP 403)', 'dial tcp: network unavailable'):
            with self.subTest(error=error):
                self.commands.clear()
                self.failures[endpoint] = (1, error)
                with self.assertRaisesRegex(RuntimeError, re.escape(error)):
                    self.publish()
                self.assertEqual(len(self.commands), 1)
        self.failures = {'releases?per_page=100&page=1': (1, 'HTTP 401')}
        with self.assertRaises(subprocess.CalledProcessError):
            self.publish()
        self.assertEqual(self.mutations, [])

    def test_existing_ref_auth_error_does_not_authorize_tag_creation(self):
        self.failures['git/ref/tags/' + self.tag] = (1, 'HTTP 403')
        with self.assertRaisesRegex(RuntimeError, 'HTTP 403'):
            self.publish()
        self.assertEqual(self.mutations, [])

    def test_partial_draft_uploads_only_missing_files_without_clobber(self):
        self.ref_sha = SHA
        missing = self.release['assets'].pop()
        retained = copy.deepcopy(self.release['assets'])
        self.publish()
        upload = next(argv for argv in self.commands if argv[:3] == ('gh', 'release', 'upload'))
        self.assertEqual(upload[-1], str(self.root / missing['name']))
        self.assertEqual(self.release['assets'][:-1], retained)
        self.assertEqual(self.mutations, [('UPLOAD', self.tag), ('PATCH', 'releases/443652159')])

    def test_conflicting_frozen_draft_assets_are_not_replaced(self):
        self.ref_sha = SHA
        self.release['assets'][0]['digest'] = 'sha256:' + '0' * 64
        with self.assertRaisesRegex(ValueError, 'Refusing to rewrite a frozen draft asset'):
            self.publish()
        self.assertEqual(self.mutations, [])

    def test_missing_stable_ref_is_never_created_implicitly(self):
        self.tag = 'v' + VERSION
        self.manifest.update(tag=self.tag, releaseBuild='ON')
        automation.write_json(self.root / automation.MANIFEST, self.manifest)
        self.release = None
        with self.assertRaises(subprocess.CalledProcessError):
            self.publish()
        with self.assertRaisesRegex(ValueError, 'Only the expected immutable staging tag'):
            automation.ensure_staging_tag(self.tag, SHA)
        self.assertEqual(self.mutations, [])

    def test_already_published_retry_keeps_input_attempt_one_and_qualifies_retry_two_separately(self):
        self.release['draft'] = False
        self.ref_sha = SHA
        assets = copy.deepcopy(self.release['assets'])
        self.manifest['runAttempt'] = 2
        automation.write_json(self.root / automation.MANIFEST, self.manifest)
        with redirect_stdout(io.StringIO()) as text:
            self.publish()
        self.assertIn('already match', text.getvalue())
        manifest = self.qualified()
        self.assertEqual(manifest['runAttempt'], 1)
        self.assertEqual(self.release['assets'], assets)
        self.assertEqual(self.mutations, [])
        requested = {argv[2] for argv in self.commands if argv[:2] == ('gh', 'api')}
        self.assertIn(self.prefix + 'actions/runs/41/attempts/1/jobs?per_page=100', requested)
        self.assertIn(self.prefix + 'actions/runs/41/attempts/2/jobs?per_page=100', requested)

    def test_publication_retry_cannot_resurrect_failed_or_skipped_original_inputs(self):
        for job in self.input_jobs:
            for conclusion in ('failure', 'skipped'):
                with self.subTest(name=job['name'], conclusion=conclusion), \
                     patch.dict(job, conclusion=conclusion):
                    with self.assertRaisesRegex(ValueError, 'frozen attempt'):
                        self.qualified()
        missing = self.producer_jobs.pop(0)
        with self.assertRaisesRegex(ValueError, 'frozen attempt'):
            self.qualified()
        self.producer_jobs.append(missing)
        self.producer_jobs.append(dict(missing, conclusion='skipped'))
        with self.assertRaisesRegex(ValueError, 'frozen attempt'):
            self.qualified()

    def test_relabelling_inputs_as_retry_two_cannot_inherit_attempt_one_successes(self):
        self.manifest['runAttempt'] = 2
        with self.assertRaisesRegex(ValueError, 'frozen attempt'):
            automation.validate_staging_receipts(self.manifest)

    def test_retry_may_inherit_absent_successes_but_never_override_explicit_failure_or_skip(self):
        for name in sorted(automation.CI_JOBS):
            for conclusion in ('failure', 'skipped'):
                with self.subTest(name=name, conclusion=conclusion):
                    self.retry_jobs[:] = [dict(name='dev-release', conclusion='success'),
                                         dict(name=name, conclusion=conclusion)]
                    with self.assertRaisesRegex(ValueError, 'failed/skipped'):
                        self.qualified()

    def test_retry_must_be_successful_at_the_same_run_source_and_workflow(self):
        for field, value in (('id', 42), ('run_attempt', 1), ('head_sha', OTHER),
                             ('head_branch', 'untrusted'), ('event', 'push'),
                             ('path', '.github/workflows/other.yml'),
                             ('repository', {'full_name': 'wrong/repository'}),
                             ('status', 'in_progress'), ('conclusion', 'failure')):
            with self.subTest(field=field), patch.dict(self.retry, {field: value}):
                with self.assertRaisesRegex(ValueError, 'publication retry'):
                    self.qualified()
        for conclusion in ('failure', 'skipped'):
            self.retry_jobs[:] = [dict(name='dev-release', conclusion=conclusion)]
            with self.assertRaisesRegex(ValueError, 'publication retry'):
                self.qualified()
        self.retry_jobs.clear()
        with self.assertRaisesRegex(ValueError, 'publication retry'):
            self.qualified()


    def test_frozen_staging_producer_recovery_reruns_only_failed_jobs_not_platforms(self):
        self.responses['actions/runs/41'] = self.producer
        automation.resume(argparse.Namespace(channel='build', run_id='41', tag=None))
        self.assertEqual(self.mutations, [('RERUN_FAILED', '41')])
        self.assertEqual(self.producer_jobs[-1], dict(name='dev-release', conclusion='failure'))
        self.assertTrue(all(job['conclusion'] == 'success' for job in self.input_jobs))
        self.assertFalse(any(argv[:3] == ('gh', 'workflow', 'run') for argv in self.commands))


class StagingRetentionTests(unittest.TestCase):
    def setUp(self):
        self.tags = [automation.snapshot_tag(f'{number:040x}') for number in range(10)]
        self.releases = [dict(tag_name=tag, draft=False, prerelease=True, body='',
                              published_at=f'2026-11-{number + 1:02d}T00:00:00Z')
                         for number, tag in enumerate(self.tags)]
        self.game_runs, self.website_runs = [], []

    def test_newest_seven_known_good_current_and_active_consumers_are_preserved(self):
        unrelated = [dict(tag_name='v1.0.865', draft=False, prerelease=False),
                     dict(tag_name='latest-dev', draft=False, prerelease=True)]
        eligible, held = automation.staging_retention_plan(
            self.releases + unrelated, self.tags[-1], self.tags[1], {self.tags[0]})
        self.assertEqual([item['tag_name'] for item in eligible], [self.tags[2]])
        self.assertTrue(set(self.tags[3:]).issubset(held))
        self.assertNotIn('v1.0.865', held)
        self.assertNotIn('latest-dev', held)

    def test_explicit_and_malformed_investigation_holds_both_prevent_deletion(self):
        self.releases[0]['body'] = '<!-- dunecity-retain: investigating packaged GUI -->'
        self.releases[2]['body'] = '<!-- dunecity-retain -->'
        eligible, held = automation.staging_retention_plan(self.releases, self.tags[-1], self.tags[1])
        self.assertEqual(eligible, [])
        self.assertIn('investigation:', held[self.tags[0]])
        self.assertIn('unknown investigation hold syntax', held[self.tags[2]])

    def test_unknown_dates_or_consumer_authority_retain_with_named_reasons(self):
        self.releases[0]['published_at'] = None
        eligible, held = automation.staging_retention_plan(
            self.releases, self.tags[-1], self.tags[-1], uncertain='consumer API unavailable')
        self.assertEqual(eligible, [])
        self.assertIn('unknown publication date', held[self.tags[0]])
        self.assertIn('consumer API unavailable', held[self.tags[1]])

    def request(self, tag):
        return dict(schema=1, repository=automation.repository(), releaseTag=tag,
                    sourceCommit=automation.STAGING_TAG.fullmatch(tag)[1], websiteCodeCommit=OTHER,
                    publicationManifestSha256='c' * 64, browserArchiveSha256='d' * 64,
                    serviceArchiveSha256='e' * 64)

    def api(self, path):
        if '/contents/deploy/staging-browser.json?' in path:
            tag = self.tags[8] if path.endswith('ref=main') else self.tags[0]
            return dict(type='file', encoding='base64',
                        content=base64.b64encode(json.dumps(self.request(tag)).encode()).decode())
        if '/actions/runs?' in path:
            rows = self.website_runs if path.startswith('repos/VR48/dunelegacy.com/') else self.game_runs
            selected = rows if '?status=in_progress&' in path else []
            return dict(total_count=len(selected), workflow_runs=selected)
        raise AssertionError('Unexpected API request: ' + path)

    def test_current_request_queued_game_tag_and_running_website_commit_pin_sources(self):
        self.game_runs = [dict(id=11, status='queued', head_branch=self.tags[1], path=automation.BUILD_WORKFLOW)]
        self.website_runs = [dict(id=22, status='in_progress', head_sha=OTHER,
                                  path='.github/workflows/deploy-metaserver-droplet.yml')]
        with patch.object(automation, 'api', side_effect=self.api):
            self.assertEqual(automation.staging_consumer_pins(), {self.tags[8], self.tags[1], self.tags[0]})

    def test_unresolved_game_browser_dispatch_is_held_not_guessed_from_default_branch(self):
        self.game_runs = [dict(id=11, status='queued', head_branch='main', path='.github/workflows/web.yml')]
        with patch.object(automation, 'api', side_effect=self.api):
            with self.assertRaisesRegex(ValueError, 'unresolved dispatch inputs'):
                automation.staging_consumer_pins()

    def test_truncated_or_unknown_active_status_is_not_a_safe_deletion_view(self):
        for response in (dict(total_count=1001, workflow_runs=[]),
                         dict(total_count=1, workflow_runs=[dict(id=1, status='unknown')])):
            with patch.object(automation, 'api', return_value=response), self.assertRaises(ValueError):
                list(automation.active_runs(automation.repository()))

    def test_api_failure_retains_everything_without_changing_package_qualification(self):
        with patch.dict(os.environ, RUNNER_TEMP='', GITHUB_STEP_SUMMARY=''), \
             patch.object(automation, 'api', side_effect=RuntimeError('consumer API unavailable')), \
             patch.object(automation.subprocess, 'run') as command, redirect_stdout(io.StringIO()):
            result = automation.prune_staging(argparse.Namespace(current=self.tags[-1], dry_run=False))
        self.assertEqual(result['deleted'], [])
        self.assertIn('retention held:', result['reason'])
        command.assert_not_called()

    def test_writer_body_is_consumed_as_exact_known_good_pin_without_touching_legacy_release(self):
        legacy = 'Historical source and attachments remain unchanged.'
        body = automation.staging_pointer_body(legacy, self.tags[0])
        body = automation.staging_pointer_body(body, self.tags[1])
        pointer = dict(tag_name='latest-dev', draft=False, prerelease=True, body=body)
        self.releases[2]['body'] = '<!-- dunecity-retain: active investigation -->'
        with patch.dict(os.environ, RUNNER_TEMP='', GITHUB_STEP_SUMMARY=''), \
             patch.object(automation, 'api', return_value=self.releases), \
             patch.object(automation, 'optional_release', return_value=pointer), \
             patch.object(automation, 'staging_consumer_pins', return_value={self.tags[0]}), \
             patch.object(automation.subprocess, 'run') as command, redirect_stdout(io.StringIO()):
            result = automation.prune_staging(argparse.Namespace(current=self.tags[-1], dry_run=True))
        self.assertEqual(result['eligible'], [])
        self.assertEqual(result['deleted'], [])
        self.assertIn(self.tags[1], result['held'])
        self.assertIn(legacy, pointer['body'])
        self.assertEqual(automation.staging_pointer_tag(pointer['body']), self.tags[1])
        self.assertEqual(pointer['body'].count('dunecity-staging-tag'), 1)
        command.assert_not_called()

    def test_missing_malformed_duplicate_and_nonpublic_pointer_authority_retains_all(self):
        valid = f'<!-- dunecity-staging-tag: {self.tags[0]} -->'
        cases = [None, dict(draft=False, prerelease=True, body='legacy without marker'),
                 dict(draft=False, prerelease=True, body=valid + valid),
                 dict(draft=False, prerelease=True, body='<!-- dunecity-staging-tag: broken -->'),
                 dict(draft=True, prerelease=True, body=valid),
                 dict(draft=False, prerelease=False, body=valid)]
        for pointer in cases:
            with self.subTest(pointer=pointer), \
                 patch.dict(os.environ, RUNNER_TEMP='', GITHUB_STEP_SUMMARY=''), \
                 patch.object(automation, 'api', return_value=self.releases), \
                 patch.object(automation, 'optional_release', return_value=pointer), \
                 patch.object(automation.subprocess, 'run') as command, redirect_stdout(io.StringIO()):
                result = automation.prune_staging(argparse.Namespace(current=self.tags[-1], dry_run=False))
            self.assertEqual(result['eligible'], [])
            self.assertEqual(result['deleted'], [])
            self.assertIn('retention held:', result['reason'])
            command.assert_not_called()

    def test_retired_snapshot_and_unknown_retirement_state_fail_before_expensive_builds(self):
        tag = self.tags[0]
        with patch.object(automation.subprocess, 'run', return_value=subprocess.CompletedProcess(
                [], 0, json.dumps({'ref': 'refs/tags/' + tag}), '')):
            with self.assertRaisesRegex(ValueError, 'Retired staging snapshot'):
                automation.require_unretired_staging(automation.STAGING_TAG.fullmatch(tag)[1], None)
        with patch.object(automation.subprocess, 'run', return_value=subprocess.CompletedProcess([], 1, '', 'HTTP 503')):
            with self.assertRaisesRegex(RuntimeError, 'Cannot establish'):
                automation.require_unretired_staging(SHA, None)
        with patch.object(automation.subprocess, 'run') as command:
            automation.require_unretired_staging(SHA, {'draft': False})
            command.assert_not_called()

    def test_explicit_retired_browser_request_is_red_and_never_builds_a_replacement(self):
        with tempfile.TemporaryDirectory() as directory:
            event = Path(directory) / 'event.json'
            event.write_text('{}')
            with patch.dict(os.environ, GITHUB_EVENT_PATH=str(event), GITHUB_EVENT_NAME='workflow_dispatch'), \
                 patch.object(automation, 'optional_release', return_value=None), \
                 patch.object(automation.subprocess, 'run') as command:
                with self.assertRaisesRegex(ValueError, 'retired or unavailable'):
                    automation.select_publication(argparse.Namespace(tag=self.tags[0], stable_only=False))
            command.assert_not_called()


    def test_frozen_browser_recipe_is_not_reinterpreted_by_current_control_scripts(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            source, inputs = base / 'source', base / 'inputs'
            inputs.mkdir()
            browser_fixture(source)
            recipe = 'previous-pinned-toolchain-recipe'
            tag = automation.snapshot_tag(SHA, recipe)
            self.assertNotEqual(tag, automation.snapshot_tag(SHA))
            with tarfile.open(inputs / automation.BROWSER, 'w:gz') as archive:
                for path in source.iterdir():
                    archive.add(path, arcname=path.name)
            automation.write_json(inputs / automation.MANIFEST, dict(
                tag=tag, sourceCommit=SHA, recipe=recipe, runId=41,
                sha256={automation.BROWSER: automation.digest(inputs / automation.BROWSER)}))
            args = argparse.Namespace(tag=tag, directory=inputs, output=base / 'play-staging',
                                      service_output=base / 'service')
            with patch.dict(os.environ, GITHUB_OUTPUT='', GITHUB_STEP_SUMMARY=''), \
                 patch.object(automation, 'download_release', return_value={'prerelease': True}), \
                 redirect_stdout(io.StringIO()):
                automation.browser_inputs(args)
            self.assertEqual(automation.digest(args.output / 'dunecity.wasm'), automation.digest(source / 'dunecity.wasm'))


    def test_existing_draft_resumes_publication_instead_of_rebuilding_identical_inputs(self):
        selected, reason = automation.package_decision(
            SHA, True, {'draft': True}, [], dt.datetime(2026, 11, 12, tzinfo=dt.timezone.utc), manual=True)
        self.assertFalse(selected)
        self.assertIn('resume its producing workflow', reason)


class HostedHeadlessQualificationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.names = ['quantbot_all_against_atreides', 'quantbot_scenario_matrix', 'quantbot_ffa_army_deploys']
        self.plan = {'tests': [dict(name=name, properties=[dict(name='LABELS', value=['headless', 'nightly', 'macOS'])])
                               for name in self.names]}
        automation.write_json(self.root / automation.HEADLESS_PLAN, self.plan)
        self.xml = '<testsuite>' + ''.join(f'<testcase name="{name}" status="run"/>' for name in self.names) + '</testsuite>'
        (self.root / automation.HEADLESS_RESULTS).write_text(self.xml)
        (self.root / 'CMakeCache.txt').write_text('DUNECITY_RELEASE_BUILD:BOOL=OFF\nDUNECITY_BUILD_TESTS:BOOL=ON\n')
        args = argparse.Namespace(platform='macos-arm64-headless-qualification', build_root=self.root,
                                  output=self.root / automation.HEADLESS_RECEIPT)
        with patch.dict(os.environ, DUNECITY_RELEASE_BUILD='OFF', PACKAGE_BUILD='false',
                        GITHUB_RUN_ID='41', GITHUB_RUN_ATTEMPT='1'), \
             patch.object(automation, 'run', side_effect=lambda *command: SHA if command[0] == 'git' else 'cmake fixture'):
            automation.record_build(args)
        self.paths = {name: self.root / name for name in
                      (automation.HEADLESS_PLAN, automation.HEADLESS_RESULTS, automation.HEADLESS_RECEIPT)}

    def test_hosted_off_qualification_binds_all_selected_tests_source_and_result_hashes(self):
        with patch.dict(os.environ, GITHUB_RUN_ID='41'):
            automation.verify_headless_qualification(self.paths, SHA)
        receipt = json.loads(self.paths[automation.HEADLESS_RECEIPT].read_text())
        self.assertEqual(receipt['qualification']['tests'], sorted(self.names))
        self.assertEqual(receipt['releaseBuild'], 'OFF')
        self.assertIs(receipt['packageBuild'], False)
        self.assertEqual(receipt['qualification']['resultsSha256'], automation.digest(self.paths[automation.HEADLESS_RESULTS]))

    def test_failed_skipped_and_missing_execution_cannot_qualify_stable(self):
        for kind in ('failure', 'error', 'skipped'):
            tree = automation.ET.fromstring(self.xml)
            automation.ET.SubElement(tree[0], kind)
            self.paths[automation.HEADLESS_RESULTS].write_text(automation.ET.tostring(tree, encoding='unicode'))
            with self.subTest(kind=kind), self.assertRaisesRegex(ValueError, 'failed, skipped'):
                automation.headless_qualification(self.root)
        self.paths[automation.HEADLESS_RESULTS].write_text('<testsuite/>')
        with self.assertRaisesRegex(ValueError, 'missing or incomplete'):
            automation.headless_qualification(self.root)

    def test_wrong_source_identity_run_or_changed_frozen_results_cannot_publish(self):
        with patch.dict(os.environ, GITHUB_RUN_ID='41'):
            with self.assertRaisesRegex(ValueError, 'source/configuration mismatch'):
                automation.verify_headless_qualification(self.paths, OTHER)
            receipt = json.loads(self.paths[automation.HEADLESS_RECEIPT].read_text())
            receipt['configuration']['DUNECITY_RELEASE_BUILD:BOOL'] = 'ON'
            automation.write_json(self.paths[automation.HEADLESS_RECEIPT], receipt)
            with self.assertRaisesRegex(ValueError, 'source/configuration mismatch'):
                automation.verify_headless_qualification(self.paths, SHA)
            receipt['configuration']['DUNECITY_RELEASE_BUILD:BOOL'] = 'OFF'
            automation.write_json(self.paths[automation.HEADLESS_RECEIPT], receipt)
            self.paths[automation.HEADLESS_RESULTS].write_text(self.xml + '\n')
            with self.assertRaisesRegex(ValueError, 'frozen test results'):
                automation.verify_headless_qualification(self.paths, SHA)
        with patch.dict(os.environ, GITHUB_RUN_ID='42'):
            with self.assertRaisesRegex(ValueError, 'different producing run'):
                automation.verify_headless_qualification(self.paths, SHA)

    def test_empty_or_gui_only_inventory_is_not_headless_nightly_coverage(self):
        for tests in ([], [dict(name='gui', properties=[dict(name='LABELS', value=['gui', 'nightly'])])]):
            automation.write_json(self.paths[automation.HEADLESS_PLAN], {'tests': tests})
            with self.assertRaisesRegex(ValueError, 'nonempty headless nightly'):
                automation.headless_qualification(self.root)


if __name__ == '__main__':
    unittest.main()
