import copy
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('ios_release_automation', ROOT / 'scripts/release-automation.py')
automation = importlib.util.module_from_spec(spec)
spec.loader.exec_module(automation)
SHA, CONTROL = 'a' * 40, 'b' * 40
TAG = automation.snapshot_tag(SHA)
REPOSITORY = 'VR48/dunecity'


class IosBuildNumberTests(unittest.TestCase):
    def test_numbers_are_canonical_unique_and_monotonic_across_runs_and_attempts(self):
        values = [automation.ios_build_number(run, attempt)
                  for run, attempt in ((1, 1), (1, 2), (99, 99), (100, 1), (899999, 99))]
        for value in values:
            parts = value.split('.')
            self.assertEqual(len(parts), 3)
            self.assertTrue(all(part.isascii() and part.isdecimal() for part in parts))
            self.assertEqual('.'.join(str(int(part)) for part in parts), value)
            self.assertGreater(int(parts[0]), 0)
            self.assertLessEqual(len(parts[0]), 4)
            self.assertTrue(all(len(part) <= 2 for part in parts[1:]))
        self.assertEqual(len(values), len(set(values)))
        self.assertEqual(sorted(tuple(map(int, value.split('.'))) for value in values),
                         [tuple(map(int, value.split('.'))) for value in values])

    def test_overflow_and_noncanonical_numbers_fail_instead_of_wrapping(self):
        for run, attempt in ((0, 1), (-1, 1), (900000, 1), (1, 0), (1, 100)):
            with self.subTest(run=run, attempt=attempt), self.assertRaises(ValueError):
                automation.ios_build_number(run, attempt)
        for value in ('0.1.1', '0100.1.1', '1000.01.1', '1000.1.01', '10000.1.1',
                      '1000.100.1', '1000.1.100', '20261112.1', '1000.1.1\n', '1000.1'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                automation.validate_ios_build_number(value)

    def command(self, *arguments):
        with tempfile.TemporaryDirectory() as directory:
            binaries = Path(directory) / 'bin'
            binaries.mkdir()
            (binaries / 'python3').symlink_to(Path(sys.executable).resolve())
            environment = dict(os.environ, DEVELOPER_DIR=directory,
                               PATH=str(binaries) + os.pathsep + os.environ.get('PATH', ''))
            return subprocess.run(['/bin/bash', str(ROOT / 'scripts/build-ios.sh'), *arguments],
                                  env=environment, text=True, capture_output=True)

    def test_internal_export_cannot_upload_sign_official_install_or_launch_in_either_order(self):
        conflicts = [('--testflight',), ('--ipa',), ('--release-build',), ('--run',),
                     ('--install', 'device'), ('--identity', 'Apple Development'), ('--profile', 'profile')]
        for conflict in conflicts:
            for arguments in (('--internal-staging', *conflict), (*conflict, '--internal-staging')):
                with self.subTest(arguments=arguments):
                    result = self.command(*arguments, '--build-number', '1000.1.1')
                    self.assertEqual(result.returncode, 2, result.stderr)
        result = self.command('--internal-staging', '--simulator', '--build-number', '1000.1.1')
        self.assertEqual(result.returncode, 2, result.stderr)

    def test_number_is_required_and_validated_before_touching_the_sdk(self):
        missing = self.command('--internal-staging')
        self.assertEqual(missing.returncode, 2, missing.stderr)
        invalid = self.command('--internal-staging', '--build-number', '20261112.1')
        self.assertEqual(invalid.returncode, 2, invalid.stderr)


class IosReceiptTests(unittest.TestCase):
    def setUp(self):
        self.qualification = dict(id=11, run_attempt=2, status='completed', conclusion='success',
                                  path=automation.BUILD_WORKFLOW, head_sha=SHA,
                                  head_branch=automation.STAGING, event='push',
                                  repository={'full_name': REPOSITORY})
        self.producer = dict(id=41, run_attempt=1, status='completed', conclusion='success',
                             path=automation.BUILD_WORKFLOW, head_sha=CONTROL,
                             head_branch='main', event='schedule', repository={'full_name': REPOSITORY})
        self.qjobs = [dict(name=name, conclusion='success') for name in automation.CI_JOBS]
        self.pjobs = copy.deepcopy(self.qjobs) + [dict(name='dev-release', conclusion='success')]
        self.manifest = dict(sourceCommit=SHA, qualificationSourceCommit=SHA,
                             qualificationRunId=11, qualificationRunAttempt=2,
                             runId=41, runAttempt=1, producerSourceCommit=CONTROL,
                             producerEvent='schedule', producerHeadBranch='main',
                             producerWorkflowPath=automation.BUILD_WORKFLOW)

    def api(self, path):
        suffix = path.removeprefix(f'repos/{REPOSITORY}/')
        return {'actions/runs/11/attempts/2': self.qualification,
                'actions/runs/11/attempts/2/jobs?per_page=100': {'jobs': self.qjobs},
                'actions/runs/41/attempts/1': self.producer,
                'actions/runs/41/attempts/1/jobs?per_page=100': {'jobs': self.pjobs}}[suffix]

    def validate(self):
        with patch.object(automation, 'repository', return_value=REPOSITORY), \
             patch.object(automation, 'api', side_effect=self.api):
            automation.validate_staging_receipts(self.manifest)

    def test_package_control_head_may_differ_but_qualification_must_match_actual_source(self):
        self.validate()
        self.manifest['qualificationSourceCommit'] = CONTROL
        with self.assertRaisesRegex(ValueError, 'qualification source'):
            self.validate()

    def test_red_or_untrusted_package_producer_does_not_qualify_mobile(self):
        for field, value in (('status', 'in_progress'), ('conclusion', 'failure'),
                             ('event', 'pull_request'), ('head_branch', 'untrusted'),
                             ('head_sha', SHA), ('path', '.github/workflows/other.yml')):
            with self.subTest(field=field), patch.dict(self.producer, {field: value}):
                with self.assertRaisesRegex(ValueError, 'Package producer'):
                    self.validate()
        self.pjobs = [job for job in self.pjobs if job['name'] != 'build-macos']
        with self.assertRaisesRegex(ValueError, 'Package producer'):
            self.validate()

    def test_source_qualification_cannot_be_the_packaging_run_or_a_red_push(self):
        for field, value in (('head_sha', CONTROL), ('head_branch', 'main'),
                             ('event', 'workflow_dispatch'), ('conclusion', 'failure')):
            with self.subTest(field=field), patch.dict(self.qualification, {field: value}):
                with self.assertRaisesRegex(ValueError, 'qualification is not'):
                    self.validate()


class IosSelectionTests(unittest.TestCase):
    def test_manual_immutable_tag_selects_only_internal_mobile_job(self):
        values = {}
        with patch.object(automation, 'qualified_staging_snapshot', return_value=({'tag': TAG}, CONTROL)), \
             patch.object(automation, 'ready_ios_artifact', return_value=None):
            automation.plan_ios(values, {}, 'workflow_dispatch', 'refs/tags/' + TAG, SHA,
                                {'ios_staging': True, 'staging_sha': SHA})
        self.assertTrue(values['ios'])
        self.assertEqual(values['sha'], SHA)
        self.assertEqual(values['currentIntegrationHead'], CONTROL)
        self.assertFalse(values['run'] or values['package'] or values['stable'] or values['ios_dispatch'])
        self.assertIn('untested', values['reason'])

    def test_branch_pr_and_mismatched_source_cannot_admit_privileged_mobile(self):
        for event, ref, source in (('workflow_dispatch', 'refs/heads/main', SHA),
                                   ('workflow_dispatch', 'refs/heads/' + automation.STAGING, SHA),
                                   ('pull_request', 'refs/tags/' + TAG, SHA),
                                   ('workflow_dispatch', 'refs/tags/' + TAG, CONTROL)):
            with self.subTest(event=event, ref=ref), self.assertRaises(ValueError):
                automation.plan_ios({}, {}, event, ref, SHA, {'ios_staging': True, 'staging_sha': source})

    def test_unchanged_verified_artifact_is_reused_without_another_build(self):
        values = {}
        with patch.object(automation, 'qualified_staging_snapshot', return_value=({}, CONTROL)), \
             patch.object(automation, 'ready_ios_artifact', return_value=(71, 91)):
            automation.plan_ios(values, {}, 'workflow_dispatch', 'refs/tags/' + TAG, SHA,
                                {'ios_staging': True})
        self.assertFalse(values['ios'])
        self.assertEqual(values['ios_artifact_id'], 91)
        self.assertIn('producing run 71', values['reason'])

    def test_weekly_driver_dispatches_qualified_source_not_default_branch_head(self):
        values = {}
        release = dict(tag_name=TAG, draft=False, prerelease=True, published_at='2026-11-13T01:00:00Z')
        with patch.object(automation, 'api', side_effect=[{'object': {'sha': CONTROL}}, [release]]), \
             patch.object(automation, 'qualified_staging_snapshot', return_value=({'tag': TAG, 'sourceCommit': SHA}, CONTROL)):
            automation.plan_ios(values, {'schedule': automation.IOS_CRON}, 'schedule',
                                'refs/heads/main', CONTROL, {})
        self.assertTrue(values['ios_dispatch'])
        self.assertFalse(values['ios'] or values['run'] or values['package'] or values['stable'])
        self.assertEqual((values['sha'], values['tag']), (SHA, TAG))

    def test_weekly_without_published_snapshot_is_explicitly_held(self):
        values = {}
        with patch.object(automation, 'api', side_effect=[{'object': {'sha': SHA}}, []]):
            automation.plan_ios(values, {'schedule': automation.IOS_CRON}, 'schedule', 'refs/heads/main', CONTROL, {})
        self.assertFalse(values['ios'] or values['ios_dispatch'] or values['run'])
        self.assertIn('held: no published qualified staging snapshot', values['reason'])

    def ios_run(self):
        return dict(id=71, head_sha=SHA, event='workflow_dispatch', path=automation.BUILD_WORKFLOW,
                    head_branch=TAG, status='completed', conclusion='success',
                    display_title='iOS internal staging ' + TAG)

    def test_retained_artifact_requires_nonexpired_content_and_authoritative_digest(self):
        artifact = dict(id=91, name=f'DuneCity-iOS-internal-{SHA}-1', expired=False,
                        size_in_bytes=100, digest='sha256:' + 'c' * 64)
        def response(path):
            return {'artifacts': [artifact]} if '/artifacts?' in path else {'workflow_runs': [self.ios_run()], 'total_count': 1}
        with patch.object(automation, 'api', side_effect=response):
            self.assertEqual(automation.ready_ios_artifact(SHA), (71, 91))
            artifact['expired'] = True
            self.assertIsNone(automation.ready_ios_artifact(SHA))
            artifact['expired'] = False
            artifact['digest'] = None
            with self.assertRaisesRegex(ValueError, 'authoritative digest'):
                automation.ready_ios_artifact(SHA)

    def test_reuse_searches_later_pages_and_fails_clear_at_api_history_limit(self):
        ignored = dict(self.ios_run(), conclusion='failure')
        artifact = dict(id=91, name=f'DuneCity-iOS-internal-{SHA}-1', expired=False,
                        size_in_bytes=100, digest='sha256:' + 'c' * 64)
        def response(path):
            if '/artifacts?' in path:
                return {'artifacts': [artifact]}
            page = int(path.rsplit('page=', 1)[1])
            return {'workflow_runs': [ignored] * 100 if page == 1 else [self.ios_run()], 'total_count': 101}
        with patch.object(automation, 'api', side_effect=response):
            self.assertEqual(automation.ready_ios_artifact(SHA), (71, 91))
        with patch.object(automation, 'api', return_value={'workflow_runs': [ignored] * 100, 'total_count': 1001}):
            with self.assertRaisesRegex(ValueError, 'exceeds the API limit'):
                automation.ready_ios_artifact(SHA)


if __name__ == '__main__':
    unittest.main()
