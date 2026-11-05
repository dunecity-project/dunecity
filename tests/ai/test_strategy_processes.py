"""Native probe subprocess/fatal-path contracts; not AI outcome measurements."""
import argparse
import ast
import json
from contextlib import redirect_stderr
import io
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch


runner = Path(__file__).with_name('run-campaign-balance.py')
nodes = [node for node in ast.parse(runner.read_text()).body
         if isinstance(node, ast.FunctionDef)
         and node.name in ('run_strategy_processes', 'run_controls_process')]
scope = dict(subprocess=subprocess, os=os, sys=sys, shutil=shutil, WINDOWS=sys.platform == 'win32')
exec(compile(ast.Module(body=nodes, type_ignores=[]), str(runner), 'exec'), scope)
run_batch = scope['run_strategy_processes']
run_controls = scope['run_controls_process']
GAME_BINARY = None

CHILD = """
import json, os, pathlib, sys, time
root = pathlib.Path(os.environ['CONTRACT_ROOT'])
index = int(os.environ['CONTRACT_INDEX'])
(root / (str(index) + '.ready')).write_text('ready')
deadline = time.monotonic() + 5
while len(list(root.glob('*.ready'))) < 3:
    if time.monotonic() >= deadline:
        raise RuntimeError('Three independent children did not start')
    time.sleep(0.01)
if os.environ['CONTRACT_MODE'] == 'failure':
    if index == 0:
        sys.exit(7)
    time.sleep(30)
elif os.environ['CONTRACT_MODE'] == 'timeout':
    time.sleep(30)
else:
    time.sleep(0.15)
print(json.dumps(dict(index=index, cwd=str(pathlib.Path.cwd()))))
"""


class StrategyProcessesTests(unittest.TestCase):
    def cases(self, root, count, mode):
        return [dict(cwd=root / ('seed-' + str(index)), log=root / ('seed-' + str(index) + '.log'),
                     env=dict(os.environ, CONTRACT_ROOT=str(root), CONTRACT_INDEX=str(index), CONTRACT_MODE=mode))
                for index in range(count)]

    def test_bounded_independent_processes_keep_their_working_directories_and_logs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            cases = self.cases(root, 6, 'success')
            processes, peaks = [], []
            native_popen = subprocess.Popen

            def observe(*args, **kwargs):
                process = native_popen(*args, **kwargs)
                processes.append(process)
                peaks.append(sum(process.poll() is None for process in processes))
                return process

            with patch.object(subprocess, 'Popen', side_effect=observe):
                run_batch([sys.executable, '-c', CHILD], cases, workers=3, timeout=10)
            for index, case in enumerate(cases):
                result = json.loads(case['log'].read_text())
                self.assertEqual(result['index'], index)
                self.assertEqual(result['cwd'], str(case['cwd']))
            # Process-local monotonic origins in Apple's Python 3.9 cannot
            # establish overlap; observe the real active child handles instead.
            self.assertGreater(max(peaks), 1)
            self.assertLessEqual(max(peaks), 3)
            self.assertEqual(len(processes), 6)
            self.assertTrue(all(process.poll() is not None for process in processes))

    def test_failure_terminates_and_reaps_running_siblings(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            cases = self.cases(root, 6, 'failure')
            processes = []
            native_popen = subprocess.Popen

            def observe(*args, **kwargs):
                process = native_popen(*args, **kwargs)
                processes.append(process)
                return process

            with patch.object(subprocess, 'Popen', side_effect=observe):
                with self.assertRaises(subprocess.CalledProcessError):
                    run_batch([sys.executable, '-c', CHILD], cases, workers=3, timeout=10)
            self.assertGreaterEqual(len(processes), 3)
            self.assertTrue(all(process.poll() is not None for process in processes))

    def test_timeout_kills_and_reaps_the_batch(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            cases = self.cases(root, 3, 'timeout')
            processes = []
            native_popen = subprocess.Popen

            def observe(*args, **kwargs):
                process = native_popen(*args, **kwargs)
                processes.append(process)
                return process

            with patch.object(subprocess, 'Popen', side_effect=observe):
                with self.assertRaises(subprocess.TimeoutExpired):
                    run_batch([sys.executable, '-c', CHILD], cases, workers=3, timeout=1)
            self.assertEqual(len(processes), 3)
            self.assertTrue(all(process.poll() is not None for process in processes))

class ControlsProcessTests(unittest.TestCase):
    def test_failure_preserves_exit_status_and_actual_error_log(self):
        with tempfile.TemporaryDirectory() as directory:
            cwd = Path(directory)
            log_path = cwd/'run.log'
            command = [sys.executable, '-c',
                       'import sys; print("CONTROLS_FAIL: fixture assertion", flush=True); sys.exit(7)']
            stderr = io.StringIO()
            with redirect_stderr(stderr), self.assertRaises(subprocess.CalledProcessError) as failure:
                run_controls(command, cwd, dict(os.environ), log_path, 5)
            self.assertEqual(failure.exception.returncode, 7)
            self.assertEqual(log_path.read_text(), 'CONTROLS_FAIL: fixture assertion\n')
            self.assertIn('CONTROLS_FAIL: fixture assertion', stderr.getvalue())
            self.assertIn(str(log_path), stderr.getvalue())
            self.assertFalse((cwd/'run-timeout.log').exists())

    def test_child_deadline_cannot_exceed_120_seconds(self):
        with tempfile.TemporaryDirectory() as directory:
            cwd = Path(directory)
            process = Mock()
            process.wait.return_value = 0
            with patch.object(subprocess, 'Popen', return_value=process):
                self.assertEqual(run_controls(['fixture'], cwd, {}, cwd/'run.log', 1800), 0)
            process.wait.assert_called_once_with(timeout=120)

    def test_timeout_preserves_logs_and_reaps_only_the_isolated_child_group(self):
        with tempfile.TemporaryDirectory() as directory:
            cwd = Path(directory)
            processes, options = [], []
            native_popen = subprocess.Popen

            def observe(*args, **kwargs):
                options.append(kwargs)
                process = native_popen(*args, **kwargs)
                processes.append(process)
                return process

            command = [sys.executable, '-c',
                       'import time; print("fixture entered wait", flush=True); time.sleep(30)']
            group_signal = patch.object(os, 'killpg', wraps=os.killpg) if os.name == 'posix' else None
            with patch.object(shutil, 'which', return_value=None), \
                    patch.object(subprocess, 'Popen', side_effect=observe), \
                    redirect_stderr(io.StringIO()):
                if group_signal:
                    group_signal.start()
                try:
                    with self.assertRaises(subprocess.TimeoutExpired) as failure:
                        run_controls(command, cwd, dict(os.environ), cwd/'run.log', 1)
                    if group_signal:
                        self.assertTrue(options[0]['start_new_session'])
                        os.killpg.assert_called_once_with(processes[0].pid, signal.SIGKILL)
                finally:
                    if group_signal:
                        group_signal.stop()
            self.assertEqual(failure.exception.timeout, 1)
            self.assertEqual(len(processes), 1)
            self.assertIsNotNone(processes[0].poll())
            self.assertIn('fixture entered wait', (cwd/'run.log').read_text())
            diagnostic = (cwd/'run-timeout.log').read_text()
            self.assertIn('exceeded 1s child wall deadline', diagnostic)
            self.assertIn('No supported stack sampler found', diagnostic)
            self.assertIn(str(cwd/'run.log'), diagnostic)

    @unittest.skipUnless(os.name == 'posix', 'macOS stack sampler / POSIX group contract')
    def test_stack_capture_is_bounded_and_precedes_group_cleanup(self):
        with tempfile.TemporaryDirectory() as directory:
            cwd = Path(directory)
            events = []
            native_killpg = os.killpg

            def capture(command, **options):
                events.append('sample')
                self.assertEqual(command[0], '/fixture/sample')
                self.assertEqual(command[2:4], ['1', '1'])
                self.assertEqual(options['timeout'], 5)
                raise subprocess.TimeoutExpired(command, 5)

            def kill_group(pid, sig):
                events.append('kill')
                native_killpg(pid, sig)

            with patch.object(sys, 'platform', 'darwin'), \
                    patch.object(shutil, 'which', return_value='/fixture/sample'), \
                    patch.object(subprocess, 'run', side_effect=capture) as sampler, \
                    patch.object(os, 'killpg', side_effect=kill_group), \
                    redirect_stderr(io.StringIO()):
                with self.assertRaises(subprocess.TimeoutExpired) as failure:
                    run_controls([sys.executable, '-c', 'import time; time.sleep(30)'],
                                 cwd, dict(os.environ), cwd/'run.log', 0.2)
            self.assertEqual(failure.exception.timeout, 0.2)
            self.assertEqual(events, ['sample', 'kill'])
            self.assertEqual(sampler.call_count, 1)
            self.assertIn('Stack capture failed:', (cwd/'run-timeout.log').read_text())

    def test_real_native_fatal_error_exits_nonzero_without_a_dialog(self):
        if GAME_BINARY is None:
            self.skipTest('Pass --game-binary to exercise the real native fatal path')
        with tempfile.TemporaryDirectory() as directory:
            cwd = Path(directory)
            profile = cwd/'profile'
            profile.mkdir()
            (profile/'Dune City.ini').write_text('[General]\nLanguage = en\nPlay Intro = false\n')
            unavailable_driver = 'dunecity-fatal-path-unavailable-driver'
            environment = dict(os.environ, DUNECITY_USERDIR=str(profile), SDL_AUDIODRIVER='dummy',
                               SDL_VIDEODRIVER=unavailable_driver)
            command = [str(GAME_BINARY), '--window', '--showlog', '--non-interactive']
            with self.assertRaises(subprocess.CalledProcessError) as failure:
                run_controls(command, cwd, environment, cwd/'run.log', 15)
            self.assertNotEqual(failure.exception.returncode, 0)
            text = (cwd/'run.log').read_text(errors='replace')
            self.assertIn("An unhandled exception of type 'sdl_error' was thrown:", text)
            self.assertIn("Couldn't initialize SDL:", text)
            self.assertIn(unavailable_driver, text)
            self.assertIn('DuneCity will now be terminated!', text)
            self.assertNotIn('CAMPAIGN_BALANCE_RESULT:', text)
            self.assertFalse((cwd/'run-timeout.log').exists())



if __name__ == '__main__':
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--game-binary', type=Path)
    arguments, unittest_arguments = parser.parse_known_args()
    GAME_BINARY = arguments.game_binary.resolve() if arguments.game_binary else None
    unittest.main(argv=[sys.argv[0]] + unittest_arguments)
