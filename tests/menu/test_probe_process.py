"""Real child-tree cancellation and sealed fixture cache boundary regressions."""
import concurrent.futures
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest

from probe_fixture import file_hash, prepare_fixture
from probe_process import ProbeProcesses


TREE = r'''
import json, os, pathlib, signal, subprocess, sys, time
root = pathlib.Path(sys.argv[1])
root.mkdir(parents=True, exist_ok=True)
leaf = subprocess.Popen([sys.executable, '-c',
    'import pathlib,signal,sys,time; signal.signal(signal.SIGTERM,signal.SIG_IGN); '
    'pathlib.Path(sys.argv[1]).write_text("ready"); time.sleep(60)', str(root / 'leaf-ready')])
signal.signal(signal.SIGTERM, signal.SIG_IGN)
deadline = time.monotonic() + 10
while not (root / 'leaf-ready').exists():
    if time.monotonic() > deadline:
        raise RuntimeError('descendant failed to start')
    time.sleep(.01)
(root / 'pids.json').write_text(json.dumps([os.getpid(), leaf.pid]))
print('owned tree ready', flush=True)
time.sleep(60)
'''


def wait_for(path, timeout=10):
    deadline = time.monotonic() + timeout
    while not path.exists():
        if time.monotonic() >= deadline:
            raise AssertionError('Missing child evidence: ' + str(path))
        time.sleep(.01)


def running(pid):
    if os.name == 'nt':
        import ctypes
        from ctypes import wintypes as w
        api = ctypes.WinDLL('kernel32', use_last_error=True)
        api.OpenProcess.argtypes = (w.DWORD, w.BOOL, w.DWORD)
        api.OpenProcess.restype = w.HANDLE
        api.WaitForSingleObject.argtypes = (w.HANDLE, w.DWORD)
        api.CloseHandle.argtypes = (w.HANDLE,)
        handle = api.OpenProcess(0x100000, False, pid)
        if not handle:
            return False
        try:
            return api.WaitForSingleObject(handle, 0) == 0x102
        finally:
            api.CloseHandle(handle)
    result = subprocess.run(['ps', '-o', 'stat=', '-p', str(pid)], capture_output=True, text=True)
    return bool(result.stdout.strip()) and not result.stdout.lstrip().startswith('Z')


class ProbeProcessTests(unittest.TestCase):
    def assert_stopped(self, pids):
        deadline = time.monotonic() + 3
        while any(running(pid) for pid in pids) and time.monotonic() < deadline:
            time.sleep(.02)
        self.assertFalse(any(running(pid) for pid in pids), 'owned child tree survived cancellation')
        if os.name != 'nt':
            with self.assertRaises(ChildProcessError):
                os.waitpid(pids[0], os.WNOHANG)

    def test_timeout_kills_descendants_reaps_leader_and_keeps_log(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            owner = ProbeProcesses(grace=.1)
            with (out / 'run.log').open('w') as log:
                with self.assertRaises(subprocess.TimeoutExpired):
                    owner.run([sys.executable, '-c', TREE, str(out)], stdout=log,
                              stderr=subprocess.STDOUT, timeout=3, check=True)
            self.assertIn('owned tree ready', (out / 'run.log').read_text())
            self.assert_stopped(json.loads((out / 'pids.json').read_text()))
            self.assertFalse(owner._active)

    def test_parallel_cancel_does_not_touch_an_unrelated_process(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            owner = ProbeProcesses(grace=.1)
            unrelated = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(60)'],
                                         stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                         stderr=subprocess.DEVNULL)
            try:
                with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
                    futures = [pool.submit(owner.run, [sys.executable, '-c', TREE, str(out / str(index))],
                                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=20)
                               for index in range(2)]
                    for index in range(2):
                        wait_for(out / str(index) / 'pids.json')
                    owner.cancel()
                    for future in futures:
                        self.assertNotEqual(future.result(timeout=5).returncode, 0)
                for index in range(2):
                    self.assert_stopped(json.loads((out / str(index) / 'pids.json').read_text()))
                self.assertIsNone(unrelated.poll())
                with self.assertRaisesRegex(RuntimeError, 'cancelled'):
                    owner.run([sys.executable, '-c', 'raise RuntimeError("must not launch")'])
                self.assertFalse(owner._active)
            finally:
                unrelated.kill()
                unrelated.wait()

    def test_failure_status_and_captured_output_are_unchanged(self):
        owner = ProbeProcesses(grace=.1)
        with self.assertRaises(subprocess.CalledProcessError) as failure:
            owner.run([sys.executable, '-c', 'print("actual error", flush=True); raise SystemExit(7)'],
                      capture_output=True, text=True, check=True)
        self.assertEqual(failure.exception.returncode, 7)
        self.assertEqual(failure.exception.stdout, 'actual error\n')
        self.assertFalse(owner._active)

    @unittest.skipIf(os.name == 'nt', 'POSIX SIGTERM delivery; Windows tree cancellation is covered above')
    def test_sigterm_finishes_scratch_only_after_owned_children_exit(self):
        self._assert_sigterm_cleanup('external')

    @unittest.skipIf(os.name == 'nt', 'POSIX Popen waitpid lock')
    def test_sigterm_unwinds_the_actual_waitpid_lock_before_cleanup(self):
        self._assert_sigterm_cleanup('wait-lock')

    @unittest.skipIf(os.name == 'nt', 'POSIX Popen and owned-entry cleanup locks')
    def test_sigterm_during_cleanup_wait_is_deferred_until_locks_exit(self):
        self._assert_sigterm_cleanup('cleanup-lock')

    def _assert_sigterm_cleanup(self, checkpoint):
        helpers = Path(__file__).resolve().parent
        profiles = helpers.parent / 'performance'
        program = '''
import json, os, pathlib, signal, subprocess, sys, time
sys.path[:0] = [sys.argv[1], sys.argv[2]]
from probe_process import processes, ProbeProcesses
from probe_profile import scratch_dir
out = pathlib.Path(sys.argv[3])
checkpoint = sys.argv[5]
processes.grace = .1
processes.install_signal_handlers()
sent = False
prepared = False

def trace_start(frame, event, arg):
    global prepared
    if prepared or event != 'call' or frame.f_code is not ProbeProcesses._communicate.__code__:
        return
    process = frame.f_locals['process']
    if checkpoint == 'wait-lock':
        # Force the normal fallback only for the real waitpid-lock regression.
        for name in ('_wait_pidfd', '_wait_kqueue'):
            if hasattr(process, name):
                setattr(process, name, lambda timeout: False)
    deadline = time.monotonic() + 10
    while not (scratch / 'pids.json').exists():
        if time.monotonic() >= deadline:
            raise AssertionError('Owned descendants did not become ready')
        time.sleep(.01)
    (out / 'tree-ready').write_text('ready')
    while not (out / 'tree-observed').exists():
        if time.monotonic() >= deadline:
            raise AssertionError('Parent did not observe owned descendants')
        time.sleep(.01)
    prepared = True
    return trace_wait

def trace_dispatch(frame, event, arg):
    if not prepared:
        return trace_start(frame, event, arg)
    return trace_wait(frame, event, arg)

def trace_wait(frame, event, arg):
    global sent
    if sent or event != 'call':
        return trace_wait
    if checkpoint == 'cleanup-lock' and frame.f_code is ProbeProcesses._signal_group.__code__:
        finish = frame.f_back
        if finish is None or finish.f_code is not ProbeProcesses._finish.__code__:
            return trace_wait
        entry_locked = finish.f_locals['entry'].lock._is_owned()
        deferred = getattr(processes._local, 'deferred', 0) > 0
        if not entry_locked or not deferred:
            raise AssertionError('Cleanup checkpoint must hold the entry lock and defer signals')
        (out / 'checkpoint.json').write_text(json.dumps(
            {'checkpoint': checkpoint, 'entry_locked': entry_locked, 'deferred': deferred}))
        sent = True
        os.kill(os.getpid(), signal.SIGTERM)
    elif checkpoint == 'wait-lock' and frame.f_code is subprocess.Popen._try_wait.__code__:
        process = frame.f_locals['self']
        if not process._waitpid_lock.locked():
            raise AssertionError('Signal checkpoint did not hold the real Popen waitpid lock')
        (out / 'checkpoint.json').write_text(json.dumps(
            {'checkpoint': checkpoint, 'waitpid_locked': True}))
        sent = True
        os.kill(os.getpid(), signal.SIGTERM)
    return trace_wait

try:
    with scratch_dir('probe-cancellation-contract') as scratch:
        (out / 'scratch-path').write_text(str(scratch))
        if checkpoint != 'external':
            sys.settrace(trace_dispatch)
        processes.run([sys.executable, '-c', sys.argv[4], str(scratch)],
                      timeout=1 if checkpoint == 'cleanup-lock' else 30)
finally:
    sys.settrace(None)
    (out / 'finalized').write_text('closed')
'''
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            config = out / 'projects.json'
            config.write_text(json.dumps({'projects': {'dunecity': {
                'scratch_root': str(out / 'scratch-root')}}}))
            state = out / 'workspace-state'
            env = dict(os.environ, AGENT_WORKSPACE_CONFIG=str(config), AGENT_WORKSPACE_STATE=str(state))
            with (out / 'supervisor.log').open('w') as log:
                supervisor = subprocess.Popen([sys.executable, '-c', program, str(helpers), str(profiles),
                                               str(out), TREE, checkpoint], stdout=log, stderr=subprocess.STDOUT,
                                              env=env)
                try:
                    wait_for(out / 'scratch-path')
                    scratch = Path((out / 'scratch-path').read_text())
                    wait_for(scratch / 'pids.json')
                    pids = json.loads((scratch / 'pids.json').read_text())
                    if checkpoint != 'external':
                        wait_for(out / 'tree-ready')
                        (out / 'tree-observed').write_text('observed')
                    if checkpoint == 'external':
                        supervisor.send_signal(signal.SIGTERM)
                    else:
                        wait_for(out / 'checkpoint.json')
                        observed = json.loads((out / 'checkpoint.json').read_text())
                        self.assertEqual(observed['checkpoint'], checkpoint)
                        if checkpoint == 'cleanup-lock':
                            self.assertTrue(observed['entry_locked'])
                            self.assertTrue(observed['deferred'])
                        else:
                            self.assertTrue(observed['waitpid_locked'])
                    self.assertEqual(supervisor.wait(timeout=10), 128 + signal.SIGTERM)
                    self.assertTrue((out / 'finalized').is_file())
                    self.assert_stopped(pids)
                    lease = scratch / '.agent-lease.json'
                    if lease.exists():
                        self.assertNotEqual(json.loads(lease.read_text())['state'], 'active')
                    # The successful contract check can now finish its own intentional failure fixture.
                    sys.path.insert(0, str(profiles))
                    from probe_profile import _installed
                    module = _installed('scratch')
                    if lease.exists() and module is not None:
                        module.Scratch(state=state, config=config).finish(scratch, True)
                    else:
                        shutil.rmtree(scratch)
                finally:
                    if supervisor.poll() is None:
                        supervisor.kill()
                    supervisor.wait()


class FixtureIdentityTests(unittest.TestCase):
    def test_preserved_mtime_write_and_symlink_retarget_invalidate_content_cache(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            first, second = out / 'first', out / 'second'
            first.write_bytes(b'first')
            second.write_bytes(b'other')
            cache = {}
            before = file_hash(first, cache)
            info = first.stat()
            first.write_bytes(b'after')
            os.utime(first, ns=(info.st_atime_ns, info.st_mtime_ns))
            self.assertNotEqual(file_hash(first, cache), before)
            if os.name != 'nt':
                link = out / 'link'
                link.symlink_to(first)
                before = file_hash(link, cache)
                link.unlink()
                link.symlink_to(second)
                self.assertNotEqual(file_hash(link, cache), before)

    def test_fixture_reuse_stale_runtime_and_binary_tampering_fail_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)
            subprocess.run(['git', 'init', '-q', str(source)], check=True)
            (source / 'main.cpp').write_text('source input\n')
            build = source / 'build'
            app = build / 'bin/dunecity.app/Contents'
            (app / 'MacOS').mkdir(parents=True)
            shutil.copy2(sys.executable, app / 'MacOS/dunecity')
            (app / 'Resources').mkdir()
            (app / 'Resources/asset').write_bytes(b'first')
            (build / 'CMakeCache.txt').write_text('configured toolchain\n')
            (build / 'build.ninja').write_text('configured commands\n')
            commands = [str(sys.executable) + ' -c main.cpp']
            publications = []

            def publish(work):
                publications.append(work)
                shutil.copy2(sys.executable, work / 'probe')
                (work / 'build.log').write_text('publication completed\n')

            def prepare(required=False):
                return prepare_fixture(source, build, build / 'results', build / 'fixtures', 'probe',
                                       commands, [], publish, require_prepared=required)

            binary = prepare()
            self.assertEqual(prepare(True), binary)
            self.assertEqual(len(publications), 1)
            asset = app / 'Resources/asset'
            info = asset.stat()
            asset.write_bytes(b'other')
            os.utime(asset, ns=(info.st_atime_ns, info.st_mtime_ns))
            with self.assertRaisesRegex(RuntimeError, 'Missing current fixture'):
                prepare(True)
            changed = prepare()
            self.assertNotEqual(changed, binary)
            self.assertEqual(len(publications), 2)
            (app / 'Resources/new-asset').write_bytes(b'new membership')
            with self.assertRaisesRegex(RuntimeError, 'Missing current fixture'):
                prepare(True)
            current = prepare()
            current.write_bytes(b'modified executable')
            with self.assertRaisesRegex(RuntimeError, 'seal differs'):
                prepare(True)
            self.assertTrue(current.with_name('build.log').is_file())


if __name__ == '__main__':
    unittest.main()
