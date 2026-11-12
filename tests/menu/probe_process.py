"""Owned probe subprocesses: cancel private trees before profile/scratch finalization."""
import atexit
from contextlib import contextmanager
import os
import signal
import subprocess
import threading
import time


class _WindowsJob:
    """A suspended child enters a kill-on-close job before any descendants can start."""
    def __init__(self):
        import ctypes
        from ctypes import wintypes as w

        class Limits(ctypes.Structure):
            _fields_ = [('process_time', ctypes.c_longlong), ('job_time', ctypes.c_longlong),
                        ('flags', w.DWORD), ('minimum_working_set', ctypes.c_size_t),
                        ('maximum_working_set', ctypes.c_size_t), ('active_processes', w.DWORD),
                        ('affinity', ctypes.c_size_t), ('priority', w.DWORD), ('scheduling', w.DWORD)]

        class IO(ctypes.Structure):
            _fields_ = [(name, ctypes.c_ulonglong) for name in
                        ('read_ops', 'write_ops', 'other_ops', 'read_bytes', 'write_bytes', 'other_bytes')]

        class ExtendedLimits(ctypes.Structure):
            _fields_ = [('basic', Limits), ('io', IO), ('process_memory', ctypes.c_size_t),
                        ('job_memory', ctypes.c_size_t), ('peak_process_memory', ctypes.c_size_t),
                        ('peak_job_memory', ctypes.c_size_t)]

        self.api = ctypes.WinDLL('kernel32', use_last_error=True)
        self.api.CreateJobObjectW.argtypes = (ctypes.c_void_p, w.LPCWSTR)
        self.api.CreateJobObjectW.restype = w.HANDLE
        self.api.SetInformationJobObject.argtypes = (w.HANDLE, ctypes.c_int, ctypes.c_void_p, w.DWORD)
        self.api.SetInformationJobObject.restype = w.BOOL
        self.api.AssignProcessToJobObject.argtypes = (w.HANDLE, w.HANDLE)
        self.api.AssignProcessToJobObject.restype = w.BOOL
        self.api.CloseHandle.argtypes = (w.HANDLE,)
        self.api.CloseHandle.restype = w.BOOL
        self.handle = self.api.CreateJobObjectW(None, None)
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())
        limits = ExtendedLimits()
        limits.basic.flags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        if not self.api.SetInformationJobObject(self.handle, 9, ctypes.byref(limits), ctypes.sizeof(limits)):
            error = ctypes.WinError(ctypes.get_last_error())
            self.close()
            raise error

    def attach(self, process):
        import ctypes
        from ctypes import wintypes as w
        if not self.api.AssignProcessToJobObject(self.handle, w.HANDLE(int(process._handle))):
            raise ctypes.WinError(ctypes.get_last_error())
        resume = ctypes.WinDLL('ntdll').NtResumeProcess
        resume.argtypes, resume.restype = (w.HANDLE,), ctypes.c_long
        status = resume(w.HANDLE(int(process._handle)))
        if status < 0:
            raise OSError('Unable to resume owned probe process: NTSTATUS %08x' % (status & 0xffffffff))

    def close(self):
        if self.handle:
            self.api.CloseHandle(self.handle)
            self.handle = None


class _OwnedProcess:
    def __init__(self, process, job):
        self.process = process
        self.job = job
        self.lock = threading.RLock()
        self.finished = False


class ProbeProcesses:
    def __init__(self, grace=3):
        self.grace = grace
        self._lock = threading.RLock()
        self._active = {}
        self._stopping = False
        self._local = threading.local()
        self._pending_signal = None
        self._installed = False

    def install_signal_handlers(self):
        if self._installed:
            return
        for signum in (signal.SIGTERM, signal.SIGINT):
            signal.signal(signum, self._on_signal)
        atexit.register(self.cancel)
        self._installed = True

    def _on_signal(self, signum, frame=None):
        # Never acquire ownership/entry/Popen locks or reap from a signal frame.
        # A normal wait must unwind its _waitpid_lock before run() cancels peers.
        if self._pending_signal is None:
            self._pending_signal = signum
        self._stopping = True
        if not getattr(self._local, 'deferred', 0):
            self._raise_pending_signal()

    def _raise_pending_signal(self):
        if self._pending_signal is not None:
            raise SystemExit(128 + self._pending_signal)

    @contextmanager
    def _defer_signals(self):
        # Launch cannot lose a child between Popen returning and registration.
        # Cleanup must finish its existing lock/wait before propagating a signal.
        previous = getattr(self._local, 'deferred', 0)
        self._local.deferred = previous + 1
        try:
            yield
        finally:
            self._local.deferred = previous

    @staticmethod
    def _signal_group(process, signum):
        try:
            os.killpg(process.pid, signum)
        except ProcessLookupError:
            pass

    def _finish(self, entry, deadline=None):
        process, job = entry.process, entry.job
        with self._defer_signals(), entry.lock:
            if entry.finished:
                return
            if os.name == 'nt':
                # Closing the private job also catches descendants after their leader exits.
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=self.grace)
                    except subprocess.TimeoutExpired:
                        pass
                job.close()
            else:
                self._signal_group(process, signal.SIGTERM)
                deadline = deadline if deadline is not None else time.monotonic() + self.grace
                while True:
                    try:
                        os.killpg(process.pid, 0)
                    except ProcessLookupError:
                        break
                    if time.monotonic() >= deadline:
                        self._signal_group(process, signal.SIGKILL)
                        break
                    time.sleep(0.02)
            # Reap the direct child before a wrapper can release its profile/scratch lease.
            process.wait()
            entry.finished = True

    def cancel(self):
        with self._defer_signals():
            with self._lock:
                self._stopping = True
                active = list(self._active.values())
            deadline = time.monotonic() + self.grace
            if os.name != 'nt':
                for entry in active:
                    with entry.lock:
                        if not entry.finished:
                            self._signal_group(entry.process, signal.SIGTERM)
            for entry in active:
                self._finish(entry, deadline)

    def _communicate(self, process, input, timeout):
        # Main-thread cancellation may be unwinding a ThreadPoolExecutor join.
        # Workers observe it outside Popen's locks without extending the deadline
        # or resending input; communicate() retains partial output across retries.
        deadline = None if timeout is None else time.monotonic() + timeout
        while True:
            self._raise_pending_signal()
            remaining = None if deadline is None else max(0, deadline - time.monotonic())
            interval = .1 if remaining is None else min(.1, remaining)
            try:
                return process.communicate(input, timeout=interval)
            except subprocess.TimeoutExpired as error:
                input = None
                self._raise_pending_signal()
                if deadline is not None and time.monotonic() >= deadline:
                    error.timeout = timeout
                    raise

    def run(self, command, **options):
        try:
            result = self._run_process(command, **options)
            self._raise_pending_signal()
            return result
        except BaseException:
            if self._pending_signal is not None:
                # Popen/entry/launch critical sections have unwound. Stop and
                # reap every peer before the caller can finalize its lease.
                self.cancel()
                self._raise_pending_signal()
            raise

    def _run_process(self, command, *, check=False, timeout=None, input=None, capture_output=False, phase=None, **options):
        started = time.monotonic() if phase else None
        if capture_output:
            if 'stdout' in options or 'stderr' in options:
                raise ValueError('capture_output cannot be combined with stdout/stderr')
            options.update(stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if input is not None:
            if 'stdin' in options:
                raise ValueError('stdin cannot be combined with input')
            options['stdin'] = subprocess.PIPE
        options.setdefault('stdin', subprocess.DEVNULL)
        if 'start_new_session' in options or 'preexec_fn' in options:
            raise ValueError('Probe process ownership cannot be overridden')
        process, entry, job = None, None, None
        try:
            with self._defer_signals(), self._lock:
                self._raise_pending_signal()
                if self._stopping:
                    raise RuntimeError('Probe batch cancelled; refusing a new subprocess')
                if os.name == 'nt':
                    job = _WindowsJob()
                    options['creationflags'] = options.get('creationflags', 0) | subprocess.CREATE_NEW_PROCESS_GROUP | 0x4
                else:
                    options['start_new_session'] = True
                process = subprocess.Popen(command, **options)
                entry = _OwnedProcess(process, job)
                self._active[process] = entry
                if job is not None:
                    job.attach(process)
            self._raise_pending_signal()
            stdout, stderr = self._communicate(process, input, timeout)
            result = subprocess.CompletedProcess(command, process.returncode, stdout, stderr)
        finally:
            with self._defer_signals():
                if entry is not None:
                    self._finish(entry)
                    with self._lock:
                        self._active.pop(process, None)
                elif job is not None:
                    job.close()
                if phase:
                    print('%s: %.3fs' % (phase, time.monotonic() - started), flush=True)
        self._raise_pending_signal()
        if check:
            result.check_returncode()
        return result


processes = ProbeProcesses()
run = processes.run


def check_output(command, **options):
    return run(command, stdout=subprocess.PIPE, check=True, **options).stdout
