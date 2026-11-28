"""Mini qualification with its test games on AWS: split, submission, merge and receipt.

Fixtures and metadata-only CTest reads: no queue or game runs. The real queue's
JUnit writer is tested in dunecity-runner; these cases use its exact shape.
"""
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'scripts' / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


aws = load('qualification_aws', 'qualification-aws.py')
automation = load('release_automation', 'release-automation.py')
SHA = 'a' * 40
JOB = '20261119T230000-qualify-1-abc123'
GAMES = {'menu_navigation_probe': dict(PROCESSORS=2, FIXTURES_REQUIRED=['menu_native_fixture']),
         'menu_probe_fixture': dict(PROCESSORS=2, FIXTURES_SETUP=['menu_native_fixture']),
         'quantbot_all_against_atreides': dict(PROCESSORS=1, RUN_SERIAL=True),
         'original_campaign_matches': dict(PROCESSORS=3),
         'skirmish_generator_probe_legacy-map_1024x768': dict(PROCESSORS=2)}
QUICK = {'dunelegacy_tests': {}, 'probe_wrapper_lifecycle': dict(PROCESSORS=1), 'research.report_contract': {}}


def plan(tests):
    return {'tests': [dict(name=name, properties=[dict(name='LABELS', value=['headless', 'nightly'])]
                           + [dict(name=key, value=value) for key, value in props.items()])
                      for name, props in tests.items()]}


def mac_junit(names, failed=()):
    cases = ''.join(
        f'<testcase name="{name}" classname="{name}" time="1.0" status="{"fail" if name in failed else "run"}">'
        f'<properties><property name="cmake_labels" value="headless;nightly"/></properties>'
        + ('<failure message="Failed"/>' if name in failed else '') + '<system-out>ok</system-out></testcase>'
        for name in names)
    return (f'<?xml version="1.0" encoding="UTF-8"?><testsuite name="Darwin-c++" tests="{len(names)}" '
            f'failures="{len(failed)}" hostname="claw.local">{cases}</testsuite>')


def aws_junit(names, job=JOB, commit=SHA, state='done', attempts=None, failed=()):
    attempts = attempts or {}
    root = ET.Element('testsuite', name='dunecity-queue', tests=str(len(names)), failures=str(len(failed)))
    props = ET.SubElement(root, 'properties')
    for name, value in (('queue_job', job), ('queue_state', state), ('source_commit', commit), ('source_id', commit)):
        ET.SubElement(props, 'property', name=name, value=value)
    for name in names:
        case = ET.SubElement(root, 'testcase', name=name, classname=name, time='2.00',
                             status='fail' if name in failed else 'run')
        case_props = ET.SubElement(case, 'properties')
        ET.SubElement(case_props, 'property', name='machine', value='aws')
        ET.SubElement(case_props, 'property', name='attempts',
                      value=attempts.get(name, 'aws:' + ('Failed' if name in failed else 'Passed')))
        if name in failed:
            ET.SubElement(case, 'failure', message='Failed')
        ET.SubElement(case, 'system-out').text = '==== aws: Passed ===='
    return ET.tostring(root, encoding='unicode')


class SplitTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.build = Path(temporary.name)
        (self.build / aws.PLAN).write_text(json.dumps(plan({**GAMES, **QUICK})))

    def test_every_test_that_plays_a_game_goes_to_aws_and_the_rest_stays_on_the_mac(self):
        state = aws.split(self.build, SHA)
        self.assertEqual(sorted(GAMES), state['aws'])
        self.assertEqual(sorted(QUICK), state['mac'])
        self.assertEqual((SHA, None), (state['sha'], state['job']))

    def test_regex_selects_exactly_the_named_tests(self):
        pattern = re.compile(aws.regex(sorted(QUICK)))
        self.assertTrue(all(pattern.match(name) for name in QUICK))
        for other in ('research_report_contract', 'dunelegacy_tests_extra', 'xdunelegacy_tests'):
            self.assertIsNone(pattern.match(other), other)
        with self.assertRaisesRegex(ValueError, 'plain identifiers'):
            aws.regex(['bad name'])
        with self.assertRaisesRegex(ValueError, 'plain identifiers'):
            aws.regex([])

    def test_split_needs_the_full_sha_and_both_kinds_of_test(self):
        with self.assertRaisesRegex(ValueError, 'full SHA'):
            aws.split(self.build, 'HEAD')
        (self.build / aws.PLAN).write_text(json.dumps(plan(QUICK)))
        with self.assertRaisesRegex(ValueError, 'both test games and quick tests'):
            aws.split(self.build, SHA)

    def test_main_writes_state_and_prints_the_mac_regex(self):
        state_path = self.build / 'state.json'
        from contextlib import redirect_stdout
        import io
        out = io.StringIO()
        with redirect_stdout(out):
            aws.main(['split', '--build', str(self.build), '--sha', SHA, '--state', str(state_path)])
        self.assertEqual(aws.regex(sorted(QUICK)), out.getvalue().strip())
        self.assertEqual(sorted(GAMES), json.loads(state_path.read_text())['aws'])


class CiPartitionTests(unittest.TestCase):
    def case(self, name, labels, **props):
        test = plan({name: props})['tests'][0]
        test['properties'][0]['value'] = labels
        return test

    def test_each_game_shape_and_a_new_probe_cannot_creep_into_bounded_ci(self):
        for name, props in {**GAMES, 'brand_new_probe': dict(PROCESSORS=2)}.items():
            with self.subTest(name=name):
                test = self.case(name, ['headless', 'nightly', 'integration'], **props)
                with self.assertRaisesRegex(ValueError, re.escape(name)):
                    aws.check_ci_partition([test])

    def test_mac_specific_native_gui_perf_and_infrastructure_checks_stay(self):
        tests = [
            self.case('native_unit', ['headless', 'nightly', 'integration']),
            self.case('mac_native', ['headless', 'nightly', 'integration', 'macos'], PROCESSORS=2),
            self.case('native_gui', ['gui', 'nightly', 'integration'], RUN_SERIAL=True),
            self.case('calibrated_perf', ['headless', 'nightly', 'integration', 'perf'], RUN_SERIAL=True),
            self.case('admission_contract', ['headless', 'nightly', 'integration', 'infrastructure'],
                      PROCESSORS=2),
            self.case('lifecycle_contract', ['headless', 'nightly', 'integration', 'infrastructure'],
                      RUN_SERIAL=True),
        ]
        aws.check_ci_partition(tests)

    def test_partition_check_does_not_modify_or_remove_any_required_aws_case(self):
        document = plan({**GAMES, **QUICK})
        before = json.dumps(document, sort_keys=True)
        aws.check_ci_partition(document['tests'])
        self.assertEqual(json.dumps(document, sort_keys=True), before)
        self.assertEqual({test['name'] for test in document['tests']
                          if aws.plays_a_game(aws.properties(test))}, set(GAMES))

    def test_cli_checks_real_ctest_metadata_without_running_the_game(self):
        from contextlib import redirect_stdout
        import io
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for labels, rejected in (
                    ('headless;nightly;integration', True),
                    ('headless;nightly', False),
                    ('headless;nightly;integration;infrastructure', False)):
                with self.subTest(labels=labels):
                    (root / 'CTestTestfile.cmake').write_text(
                        f'add_test(brand_new_probe "{sys.executable}" "-c" "raise SystemExit(99)")\n'
                        f'set_tests_properties(brand_new_probe PROPERTIES LABELS "{labels}" PROCESSORS 2)\n')
                    with redirect_stdout(io.StringIO()):
                        if rejected:
                            with self.assertRaisesRegex(ValueError, 'brand_new_probe'):
                                aws.main(['check-ci-plan', '--build', directory])
                        else:
                            self.assertEqual(aws.main(['check-ci-plan', '--build', directory]), 0)

    def test_existing_commands_still_require_their_state_receipt(self):
        from contextlib import redirect_stderr
        import io
        for command in ('split', 'submit', 'finish', 'cancel'):
            with self.subTest(command=command), redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as raised:
                    aws.main([command])
                self.assertEqual(raised.exception.code, 2)


class SubmitAndFinishTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.build = Path(temporary.name)
        self.state = dict(sha=SHA, aws=sorted(GAMES), mac=sorted(QUICK), job=JOB)
        self.env = dict(GITHUB_RUN_ID='77', GITHUB_RUN_ATTEMPT='1', GITHUB_WORKSPACE='/work/dunecity')
        self.calls = []

    def test_submit_pins_the_sha_scopes_aws_and_records_the_job(self):
        def run(command, **kwargs):
            self.calls.append(command)
            return SimpleNamespace(returncode=0, stdout=f'{JOB}\n')
        state = aws.submit(dict(self.state, job=None), run=run, env=self.env)
        self.assertEqual(JOB, state['job'])
        command = self.calls[0]
        self.assertEqual(['dunecity-queue', 'ctest', '--cwd', '/work/dunecity', '--rev', SHA,
                          '--machines', 'aws', 'aws-mel2', 'aws-mel4'], command[:10])
        self.assertEqual(aws.regex(sorted(GAMES)), command[command.index('-R') + 1])
        self.assertEqual('github:77:1:qualify', command[command.index('--owner') + 1])
        self.assertEqual(('nightly', 'network'), (command[command.index('-L') + 1], command[command.index('-LE') + 1]))
        self.assertEqual('110', command[command.index('--priority') + 1])
        self.assertIn('--no-diagnostic-retry', command)
        with self.assertRaisesRegex(ValueError, 'already submitted'):
            aws.submit(state, run=run, env=self.env)

    def test_refused_submission_fails_the_lane(self):
        with self.assertRaisesRegex(ValueError, 'refused'):
            aws.submit(dict(self.state, job=None), env=self.env,
                       run=lambda command, **kwargs: SimpleNamespace(returncode=3, stdout=''))

    def fake_queue(self, aws_xml, wait_timeout=False, check_rc=0):
        def run(command, **kwargs):
            self.calls.append(command)
            if command[:2] == ['dunecity-queue', 'wait']:
                if wait_timeout:
                    raise subprocess.TimeoutExpired(command, kwargs.get('timeout'))
                return SimpleNamespace(returncode=0)
            if command[:2] == ['dunecity-queue', 'report']:
                Path(command[command.index('--junit') + 1]).write_text(aws_xml)
                return SimpleNamespace(returncode=0)
            if 'check-headless' in command:
                return SimpleNamespace(returncode=check_rc)
            return SimpleNamespace(returncode=0)
        return run

    def test_finish_merges_aws_games_into_the_mac_results_and_checks_them(self):
        (self.build / aws.MAC_RESULTS).write_text(mac_junit(sorted(QUICK)))
        aws.finish(self.build, self.state, 2400, run=self.fake_queue(aws_junit(sorted(GAMES))))
        check = self.calls[-1]
        self.assertEqual(['check-headless', '--directory', str(self.build), '--ctest-status', '0'], check[2:])
        root = ET.parse(self.build / aws.RESULTS).getroot()
        self.assertEqual(sorted({**GAMES, **QUICK}), sorted(case.get('name') for case in root.iter('testcase')))
        self.assertEqual((str(len(GAMES) + len(QUICK)), '0'), (root.get('tests'), root.get('failures')))
        merged = next(case for case in root.iter('testcase') if case.get('name') == 'menu_navigation_probe')
        facts = {item.get('name'): item.get('value') for item in merged.iter('property')}
        self.assertEqual((JOB, SHA, 'aws'), (facts['queue_job'], facts['source_commit'], facts['machine']))

    def test_a_pass_that_needed_a_retry_is_a_failure(self):
        (self.build / aws.MAC_RESULTS).write_text(mac_junit(sorted(QUICK)))
        retried = aws_junit(sorted(GAMES), attempts={'menu_navigation_probe': 'aws:Failed;aws-mel2:Passed'})
        with self.assertRaisesRegex(ValueError, '1 AWS and 1 total'):
            aws.finish(self.build, self.state, 2400, run=self.fake_queue(retried, check_rc=1))
        self.assertEqual('8', self.calls[-1][-1])
        case = next(case for case in ET.parse(self.build / aws.RESULTS).getroot().iter('testcase')
                    if case.get('name') == 'menu_navigation_probe')
        self.assertEqual('fail', case.get('status'))
        self.assertIn('passed only on a retry', case.find('failure').get('message'))

    def test_results_from_another_job_source_or_test_set_are_refused(self):
        (self.build / aws.MAC_RESULTS).write_text(mac_junit(sorted(QUICK)))
        for xml, message in ((aws_junit(sorted(GAMES), job='other'), 'different job or source'),
                             (aws_junit(sorted(GAMES), commit='b' * 40), 'different job or source'),
                             (aws_junit(sorted(GAMES), state='cancelled'), 'did not finish'),
                             (aws_junit(sorted(GAMES)[1:]), 'different set of test games')):
            with self.subTest(message=message), self.assertRaisesRegex(ValueError, message):
                aws.finish(self.build, self.state, 2400, run=self.fake_queue(xml))
        (self.build / aws.MAC_RESULTS).write_text(mac_junit(sorted(QUICK)[1:]))
        with self.assertRaisesRegex(ValueError, 'different set of quick tests'):
            aws.finish(self.build, self.state, 2400, run=self.fake_queue(aws_junit(sorted(GAMES))))

    def test_an_unfinished_aws_job_is_cancelled_not_waited_on_forever(self):
        with self.assertRaisesRegex(ValueError, 'did not finish within 40 minutes; cancelled'):
            aws.finish(self.build, self.state, 2400, run=self.fake_queue('', wait_timeout=True))
        self.assertEqual(['dunecity-queue', 'cancel', JOB], self.calls[-1])

    def test_cancel_without_a_job_does_nothing(self):
        aws.cancel({}, run=lambda *args, **kwargs: self.fail('nothing to cancel'))


class ReceiptTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.build = Path(temporary.name)
        (self.build / automation.HEADLESS_PLAN).write_text(json.dumps(plan({**GAMES, **QUICK})))
        (self.build / aws.MAC_RESULTS).write_text(mac_junit(sorted(QUICK)))
        self.state = dict(sha=SHA, aws=sorted(GAMES), mac=sorted(QUICK), job=JOB)

    def merged(self, **kwargs):
        (self.build / aws.AWS_RESULTS).write_text(aws_junit(sorted(GAMES), **kwargs))
        return aws.merge(self.build, self.state)

    def test_receipt_names_the_aws_job_source_and_games(self):
        self.assertEqual(0, self.merged())
        result = automation.headless_qualification(self.build)
        self.assertEqual('success', result['status'])
        self.assertEqual(sorted({**GAMES, **QUICK}), result['tests'])
        self.assertEqual(dict(queueJob=JOB, sourceCommit=SHA, machines=['aws', 'aws-mel2'], tests=sorted(GAMES)),
                         result['linux'])

    def test_a_mac_only_receipt_has_no_linux_section(self):
        (self.build / automation.HEADLESS_RESULTS).write_text(mac_junit(sorted({**GAMES, **QUICK})))
        self.assertNotIn('linux', automation.headless_qualification(self.build))

    def test_receipt_check_refuses_a_retried_pass_even_if_merged_by_hand(self):
        self.merged()
        tree = ET.parse(self.build / automation.HEADLESS_RESULTS)
        case = next(case for case in tree.getroot().iter('testcase') if case.get('name') == 'menu_probe_fixture')
        next(item for item in case.iter('property') if item.get('name') == 'attempts').set(
            'value', 'aws:Failed;aws-mel2:Passed')
        tree.write(self.build / automation.HEADLESS_RESULTS)
        with self.assertRaisesRegex(ValueError, 'passed only on a retry'):
            automation.headless_qualification(self.build)

    def test_aws_failure_needs_a_committed_decision_like_any_other(self):
        self.assertEqual(1, self.merged(failed=('menu_probe_fixture',)))
        with self.assertRaisesRegex(ValueError, 'unaccepted failure: menu_probe_fixture'):
            automation.headless_qualification(self.build)


class WorkflowTests(unittest.TestCase):
    def test_aws_games_only_with_a_bounded_wait_and_following_aws_outside_the_gate(self):
        workflow = (ROOT / '.github/workflows/qualify-release.yml').read_text()
        step = workflow[workflow.index('Qualify frozen staging source'):workflow.index('Report whether Mini')]
        if "DUNECITY_QUALIFY_GAMES_ON_AWS: '1'" not in step:
            # Until main's control workflow enables it, the Mini plays every game itself.
            self.assertNotIn('qualification-aws.py', step)
            return
        self.assertIn('deadline=$((SECONDS + 1800))', step)
        self.assertIn('[ -f "$MARKER" ]', step)   # A started lane is never retried.
        loop_end = step.index('done\n')
        self.assertLess(step.index('agent-workspace run'), loop_end)
        self.assertGreater(step.index('qualification-aws.py finish'), loop_end)
        self.assertIn('qualification-aws.py cancel', step)

    def test_automatic_qualification_overlaps_platform_ci(self):
        workflow = (ROOT / '.github/workflows/qualify-release.yml').read_text()
        self.assertIn('types: [requested]', workflow)
        self.assertNotIn('types: [completed]', workflow)

    def test_warm_mini_cache_does_not_roundtrip_through_github(self):
        import textwrap
        workflow = (ROOT / '.github/workflows/qualify-release.yml').read_text()
        block = workflow.split('id: compiler-cache\n', 1)[1].split('      - uses:', 1)[0]
        shell = textwrap.dedent(block.split('run: |\n', 1)[1])
        with tempfile.TemporaryDirectory() as folder:
            home = Path(folder)
            output, environment = home / 'output', home / 'environment'
            env = dict(os.environ, HOME=str(home), RUNNER_ENVIRONMENT='self-hosted',
                       RUNNER_TEMP=str(home / 'ephemeral'), GITHUB_ENV=str(environment),
                       GITHUB_OUTPUT=str(output))
            subprocess.run(['bash', '-eu', '-c', shell], env=env, check=True)
            self.assertIn('warm=false', output.read_text())
            cache = home / '.cache/dunecity-qualification/ccache'
            self.assertIn('CCACHE_DIR=' + str(cache), environment.read_text())
            (cache / '.persistent-ready').touch()
            output.write_text('')
            subprocess.run(['bash', '-eu', '-c', shell], env=env, check=True)
            self.assertIn('warm=true', output.read_text())
        self.assertIn("if: runner.environment != 'self-hosted' || steps.compiler-cache.outputs.warm != 'true'", workflow)
        self.assertIn('max-size: 2G', workflow)
        self.assertIn("save: ${{ runner.environment != 'self-hosted' }}", workflow)

    def test_long_game_tier_cut_preserves_complete_nightly_coverage(self):
        source = (ROOT / 'tests/CMakeLists.txt').read_text()
        self.assertIn('set(labels ${capability} nightly ${tiers} ${CASE_LABELS})', source)
        names = ('original_campaign_ai', 'shipped_mod_update', 'quantbot_repair_return_check',
                 'repair_capacity_${repair_mod}')
        registrations = 0
        for line in source.splitlines():
            if 'dunecity_add_test(' in line and any('NAME ' + name in line for name in names):
                registrations += 1
                self.assertIn('dunecity_add_test(headless nightly', line)
        self.assertGreater(registrations, 0)
        lane = (ROOT / 'scripts/macos-ci-lane.sh').read_text()
        self.assertIn('--parallel "$CMAKE_BUILD_PARALLEL_LEVEL" -L integration -LE gui', lane)
        self.assertIn('export CCACHE_BASEDIR="$GITHUB_WORKSPACE"', lane)


if __name__ == '__main__':
    unittest.main()
