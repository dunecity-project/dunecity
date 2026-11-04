"""Strategy subprocess lifecycle contracts; these are not AI outcome measurements."""
import ast
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


runner = Path(__file__).with_name('run-campaign-balance.py')
node = next(node for node in ast.parse(runner.read_text()).body
            if isinstance(node, ast.FunctionDef) and node.name == 'run_strategy_processes')
scope = {'subprocess': subprocess}
exec(compile(ast.Module(body=[node], type_ignores=[]), str(runner), 'exec'), scope)
run_batch = scope['run_strategy_processes']

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


if __name__ == '__main__':
    unittest.main()
