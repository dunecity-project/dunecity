"""Opt-in native probe accounting, used by simulation-throughput.py on cluster hosts.

RSS is resident memory on every platform (not Windows commit). CPU is the native
process's user + kernel time. Sampling/profile-tool costs are kept separate.
"""
import json
import os
import platform
import re
import statistics
import subprocess
import sys
import threading
import time
from pathlib import Path
from contextlib import nullcontext


def loop_steady_rss(samples, startup_wall_s, loop_ms):
    """Median only within the measured simulation loop, excluding asset startup."""
    if startup_wall_s is None or loop_ms is None:
        return None
    seconds = float(loop_ms) / 1000
    lower = float(startup_wall_s) + max(0.25, seconds * 0.25)
    upper = float(startup_wall_s) + seconds * 0.9
    values = [rss for wall, rss in samples if lower <= wall <= upper]
    return round(statistics.median(values), 3) if values else None


def measure_runner(command, console, *, env, cwd):
    """Account the Python launcher and its descendants, matching headless-bench."""
    with Path(console).open('w') as handle:
        if sys.platform != 'win32':
            process = subprocess.Popen(command, cwd=cwd, env=env, stdout=handle, stderr=subprocess.STDOUT)
            _, status, usage = os.wait4(process.pid, 0)
            process.returncode = os.waitstatus_to_exitcode(status)
            rss = usage.ru_maxrss / ((1 << 20) if sys.platform == 'darwin' else 1024)
            return process.returncode, usage.ru_utime + usage.ru_stime, rss
        import ctypes
        import struct
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.CreateJobObjectW.restype = ctypes.c_void_p
        kernel.AssignProcessToJobObject.argtypes = (ctypes.c_void_p, ctypes.c_void_p)
        kernel.QueryInformationJobObject.argtypes = (ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p,
                                                     ctypes.c_uint32, ctypes.c_void_p)
        kernel.CloseHandle.argtypes = (ctypes.c_void_p,)
        job = kernel.CreateJobObjectW(None, None)
        if not job:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            process = subprocess.Popen(command, cwd=cwd, env=env, stdout=handle, stderr=subprocess.STDOUT)
            if not kernel.AssignProcessToJobObject(job, ctypes.c_void_p(int(process._handle))):
                process.kill()
                process.wait()
                raise ctypes.WinError(ctypes.get_last_error())
            returncode = process.wait()
            accounting = ctypes.create_string_buffer(48)
            if not kernel.QueryInformationJobObject(job, 1, accounting, 48, None):
                raise ctypes.WinError(ctypes.get_last_error())
            user, system = struct.unpack_from('<qq', accounting.raw, 0)
            return returncode, (user + system) / 1e7, 0
        finally:
            kernel.CloseHandle(job)


def _windows_counters(process):
    import ctypes
    from ctypes import wintypes

    class Memory(ctypes.Structure):
        _fields_ = [('cb', wintypes.DWORD), ('faults', wintypes.DWORD)] + [
            (name, ctypes.c_size_t) for name in ('peak_rss', 'rss', 'peak_paged', 'paged',
                                                'peak_nonpaged', 'nonpaged', 'pagefile', 'peak_pagefile')]

    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    psapi = ctypes.WinDLL('psapi', use_last_error=True)
    kernel.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
    psapi.GetProcessMemoryInfo.argtypes = [wintypes.HANDLE, ctypes.POINTER(Memory), wintypes.DWORD]
    memory = Memory()
    memory.cb = ctypes.sizeof(memory)
    created, ended, system, user = (wintypes.FILETIME() for _ in range(4))
    handle = wintypes.HANDLE(int(process._handle))
    if not psapi.GetProcessMemoryInfo(handle, ctypes.byref(memory), memory.cb) and process.poll() is None:
        raise ctypes.WinError(ctypes.get_last_error())
    if not kernel.GetProcessTimes(handle, *[ctypes.byref(value) for value in (created, ended, system, user)]):
        raise ctypes.WinError(ctypes.get_last_error())
    ticks = lambda value: (value.dwHighDateTime << 32) | value.dwLowDateTime
    return memory.rss / (1 << 20), memory.peak_rss / (1 << 20), (ticks(system) + ticks(user)) / 1e7


def measure_process(command, *, cwd, env, log, timeout, output, sample_seconds=0):
    """Accept the runner's log path or an already-open capture stream."""
    context = Path(log).open('w') if isinstance(log, (str, os.PathLike)) else nullcontext(log)
    with context as capture:
        return _measure_process(command, cwd=cwd, env=env, log=capture, timeout=timeout,
                                output=output, sample_seconds=sample_seconds)


def _measure_process(command, *, cwd, env, log, timeout, output, sample_seconds):
    """Run one checked probe and persist measurements even if it fails.

    DUNECITY_SIM_MEASURE is deliberately opt-in: ordinary tests do no polling or
    profiler work. macOS sample runs once, after process start-up, when requested.
    """
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    started, monitor_started = time.monotonic(), time.process_time()
    env = dict(env)
    if sys.platform == 'darwin' and env.get('DUNECITY_SIM_MALLOC_STACKS'):
        env['MallocStackLogging'] = env.pop('DUNECITY_SIM_MALLOC_STACKS')
    process = subprocess.Popen(command, cwd=cwd, env=env, stdout=log, stderr=subprocess.STDOUT)
    samples, tools, errors = [], [], []
    probe_cpu, peak, returncode = 0.0, 0.0, None
    sample_thread = None
    timed_out = False
    startup_wall = None
    log_position = 0

    def profile():
        destination = output.with_suffix('.sample.txt')
        try:
            with output.with_suffix('.sample-console.txt').open('w') as console:
                result = subprocess.run(['/usr/bin/sample', str(process.pid), str(sample_seconds), '1',
                                         '-mayDie', '-file', str(destination)],
                                        stdout=console, stderr=subprocess.STDOUT,
                                        timeout=max(300, sample_seconds + 60))
            tools.append({'tool': 'sample', 'returncode': result.returncode, 'file': destination.name})
        except (OSError, subprocess.TimeoutExpired) as error:
            errors.append(str(error))

    try:
        while returncode is None:
            now = time.monotonic()
            if now - started >= timeout:
                timed_out = True
                process.kill()
            if startup_wall is None:
                with Path(log.name).open(errors='replace') as capture:
                    capture.seek(log_position)
                    chunk = capture.read()
                    log_position = capture.tell()
                if 'ASSET_STARTUP: phase=probe_setup' in chunk:
                    startup_wall = now - started
            if sys.platform == 'win32':
                rss, observed_peak, probe_cpu = _windows_counters(process)
                peak = max(peak, observed_peak)
                returncode = process.poll()
                if returncode is not None:
                    break
            else:
                pid, status, usage = os.wait4(process.pid, os.WNOHANG)
                if pid:
                    returncode = os.waitstatus_to_exitcode(status)
                    process.returncode = returncode
                    probe_cpu = usage.ru_utime + usage.ru_stime
                    peak = usage.ru_maxrss / ((1 << 20) if sys.platform == 'darwin' else 1024)
                    break
                observed = subprocess.run(['/bin/ps', '-o', 'rss=', '-p', str(process.pid)],
                                          capture_output=True, text=True, check=False)
                rss = float(observed.stdout.strip() or 0) / 1024
            samples.append([round(now - started, 3), round(rss, 3)])
            if sample_seconds and sys.platform == 'darwin' and sample_thread is None and startup_wall is not None:
                sample_thread = threading.Thread(target=profile, name='probe-sample', daemon=True)
                sample_thread.start()
            if returncode is None:
                time.sleep(0.25)
    finally:
        if returncode is None:
            process.kill()
            if sys.platform == 'win32':
                process.wait()
            else:
                _, status, usage = os.wait4(process.pid, 0)
                returncode = os.waitstatus_to_exitcode(status)
                process.returncode = returncode
                probe_cpu = usage.ru_utime + usage.ru_stime
                peak = usage.ru_maxrss / ((1 << 20) if sys.platform == 'darwin' else 1024)
        elapsed = time.monotonic() - started
        if sample_thread:
            sample_thread.join(timeout=max(310, sample_seconds + 70))
        log_text = Path(log.name).read_text(errors='replace')
        timing = re.search(r'PROBE_TIMING: [^\n]*\bloop_ms=(\d+)', log_text)
        loop_ms = int(timing[1]) if timing else None
        row = dict(schema=1, host=platform.node(), platform=sys.platform, cpu_count=os.cpu_count(),
                   command=[str(value) for value in command], wall_s=round(elapsed, 6),
                   cpu_s=round(probe_cpu, 6), peak_rss_mib=round(peak, 3),
                   startup_wall_s=round(startup_wall, 3) if startup_wall is not None else None,
                   startup_resolution_s=0.25,
                   loop_ms=loop_ms, steady_rss_mib=loop_steady_rss(samples, startup_wall, loop_ms),
                   rss_samples=samples, monitor_cpu_s=round(time.process_time() - monitor_started, 6),
                   profile_tools=tools, errors=errors, returncode=returncode, timed_out=timed_out)
        output.write_text(json.dumps(row, indent=2) + '\n')
    if timed_out:
        raise subprocess.TimeoutExpired(command, timeout)
    if returncode:
        raise subprocess.CalledProcessError(returncode, command)
    return row
