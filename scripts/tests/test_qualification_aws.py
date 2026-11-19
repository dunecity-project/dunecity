"""Mini qualification with its test games on AWS: split, submission, merge and receipt.

Fixtures only: no queue, CTest run or game. The real queue's JUnit writer is tested in
dunecity-runner (lib/test_ctest_report.py); these cases use its exact shape.
"""
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
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
                          '--machines', 'aws', 'aws-mel2'], command[:9])
        self.assertEqual(aws.regex(sorted(GAMES)), command[command.index('-R') + 1])
        self.assertEqual('github:77:1:qualify', command[command.index('--owner') + 1])
        self.assertEqual(('nightly', 'network'), (command[command.index('-L') + 1], command[command.index('-LE') + 1]))
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
    def test_mini_waits_a_bounded_time_and_follows_aws_outside_its_gate(self):
        workflow = (ROOT / '.github/workflows/qualify-release.yml').read_text()
        step = workflow[workflow.index('Qualify frozen staging source'):workflow.index('Report whether Mini')]
        self.assertIn('deadline=$((SECONDS + 1800))', step)
        self.assertIn('[ -f "$MARKER" ]', step)   # A started lane is never retried.
        loop_end = step.index('done\n')
        self.assertLess(step.index('agent-workspace run'), loop_end)
        self.assertGreater(step.index('qualification-aws.py finish'), loop_end)
        self.assertIn('qualification-aws.py cancel', step)


if __name__ == '__main__':
    unittest.main()
