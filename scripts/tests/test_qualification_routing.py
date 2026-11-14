import importlib.util
import json
from pathlib import Path
import subprocess
import unittest

spec = importlib.util.spec_from_file_location('routing', Path(__file__).resolve().parents[1] / 'qualification-routing.py')
routing = importlib.util.module_from_spec(spec)
spec.loader.exec_module(routing)


class RoutingTests(unittest.TestCase):
    def setUp(self):
        self.env = dict(CANDIDATE_SHA='a' * 40, GITHUB_REF_NAME='integrate/feature-batch',
                        GITHUB_REPOSITORY=routing.REPOSITORY, GITHUB_RUN_ID='123', GITHUB_RUN_ATTEMPT='2')
        self.calls, self.elapsed = [], 0

    def simulate(self, states, fail_dispatch=False, fail_cancel=False):
        states = iter(states)
        last = 'queued'

        def call(*args):
            nonlocal last
            self.calls.append(args)
            if args[:3] == ('gh', 'workflow', 'run'):
                if fail_dispatch:
                    raise subprocess.CalledProcessError(1, args)
                return ''
            if args[-1].endswith('/cancel'):
                if fail_cancel:
                    raise subprocess.CalledProcessError(1, args)
                return ''
            last = next(states, last)
            jobs = [] if last == 'missing' else [dict(name='qualify', status=last)]
            return json.dumps(dict(total_count=len(jobs), jobs=jobs))

        def sleep(seconds):
            self.elapsed += seconds

        routing.watch(self.env, call, lambda: self.elapsed, sleep)

    def test_available_or_completed_mini_never_retries_tests(self):
        for state in ('in_progress', 'completed'):
            with self.subTest(state=state):
                self.calls.clear()
                self.simulate(['queued', state])
                self.assertFalse(any(call[:3] == ('gh', 'workflow', 'run') for call in self.calls))

    def test_offline_or_unassigned_mini_gets_one_hosted_dispatch_then_cancel(self):
        for state in ('queued', 'missing'):
            with self.subTest(state=state):
                self.calls.clear(); self.elapsed = 0
                self.simulate([state])
                self.assertEqual(self.elapsed, routing.QUEUE_SECONDS)
                self.assertIn('backend=hosted', self.calls[-2])
                self.assertEqual(sum(call[:3] == ('gh', 'workflow', 'run') for call in self.calls), 1)
                self.assertTrue(self.calls[-1][-1].endswith('/runs/123/cancel'))

    def test_assignment_at_deadline_wins_over_fallback(self):
        self.simulate(['queued'] * 8 + ['in_progress'])
        self.assertEqual(self.elapsed, 120)
        self.assertFalse(any(call[:3] == ('gh', 'workflow', 'run') for call in self.calls))

    def test_failed_dispatch_does_not_cancel_the_original(self):
        with self.assertRaises(subprocess.CalledProcessError):
            self.simulate(['queued'], fail_dispatch=True)
        self.assertFalse(any(call[-1].endswith('/cancel') for call in self.calls))

    def test_failed_cancel_is_reported(self):
        with self.assertRaises(subprocess.CalledProcessError):
            self.simulate(['queued'], fail_cancel=True)

    def test_unknown_or_incomplete_job_state_fails_closed(self):
        for data in (dict(total_count=101, jobs=[]), dict(total_count=2, jobs=[dict(name='qualify', status='queued')]),
                     dict(total_count=2, jobs=[dict(name='qualify', status='queued')] * 2)):
            with self.subTest(data=data), self.assertRaises(ValueError):
                routing.watch(self.env, lambda *args: json.dumps(data))

    def test_untrusted_context_cannot_dispatch(self):
        for key, value in [('CANDIDATE_SHA', 'main'), ('GITHUB_REF_NAME', 'untrusted'),
                           ('GITHUB_REPOSITORY', 'fork/dunecity'), ('GITHUB_RUN_ID', '0'),
                           ('GITHUB_RUN_ATTEMPT', '1/other')]:
            with self.subTest(key=key), self.assertRaises(ValueError):
                routing.context(dict(self.env, **{key: value}))

    def refusal(self, admitted, steps):
        calls = []
        def call(*args):
            calls.append(args)
            return json.dumps(dict(total_count=1, jobs=[dict(name='qualify', conclusion='failure', steps=steps)]))
        routing.refused(dict(self.env, MINI_ADMITTED=admitted), call)
        self.assertIn('backend=hosted', calls[-1])

    def test_known_refusal_and_pre_job_denial_can_fall_back(self):
        self.refusal('false', [dict(name='Report whether Mini qualification actually started', conclusion='success')])
        self.refusal('', [])

    def test_real_failure_or_lost_marker_output_cannot_fall_back(self):
        lane = dict(name='Qualify frozen staging source before production selection', status='completed', conclusion='failure')
        for admitted in ('true', '', 'false'):
            with self.subTest(admitted=admitted), self.assertRaisesRegex(ValueError, 'may have started'):
                self.refusal(admitted, [lane])


if __name__ == '__main__':
    unittest.main()
