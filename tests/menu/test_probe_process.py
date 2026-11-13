"""Real child-tree cancellation and sealed fixture cache boundary regressions."""
import concurrent.futures
import copy
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import tarfile
import time
import unittest
from unittest.mock import patch
import zipfile

from probe_fixture import file_hash, prepare_fixture
from probe_process import ProbeProcesses
import pointer_artifact as pointer


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
tmp = root / 'pids.json.tmp'
tmp.write_text(json.dumps([os.getpid(), leaf.pid]))
tmp.replace(root / 'pids.json')
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
        tmp = out / 'checkpoint.json.tmp'
        tmp.write_text(json.dumps(
            {'checkpoint': checkpoint, 'entry_locked': entry_locked, 'deferred': deferred}))
        tmp.replace(out / 'checkpoint.json')
        sent = True
        os.kill(os.getpid(), signal.SIGTERM)
    elif checkpoint == 'wait-lock' and frame.f_code is subprocess.Popen._try_wait.__code__:
        process = frame.f_locals['self']
        if not process._waitpid_lock.locked():
            raise AssertionError('Signal checkpoint did not hold the real Popen waitpid lock')
        tmp = out / 'checkpoint.json.tmp'
        tmp.write_text(json.dumps(
            {'checkpoint': checkpoint, 'waitpid_locked': True}))
        tmp.replace(out / 'checkpoint.json')
        sent = True
        os.kill(os.getpid(), signal.SIGTERM)
    return trace_wait

try:
    with scratch_dir('probe-cancellation-contract') as scratch:
        tmp = out / 'scratch-path.tmp'
        tmp.write_text(str(scratch))
        tmp.replace(out / 'scratch-path')
        if checkpoint != 'external':
            sys.settrace(trace_dispatch)
        processes.run([sys.executable, '-c', sys.argv[4], str(scratch)],
                      timeout=1 if checkpoint == 'cleanup-lock' else 30)
finally:
    sys.settrace(None)
    (out / 'finalized').write_text('closed')
'''
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory).resolve()
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
                except Exception as error:
                    log.flush()
                    with (out / 'supervisor.log').open('rb') as evidence:
                        evidence.seek(0, os.SEEK_END)
                        evidence.seek(max(0, evidence.tell() - 16384))
                        output = evidence.read().decode('utf-8', errors='replace')
                    raise AssertionError(
                        f'{error}\nsupervisor returncode: {supervisor.poll()}\n'
                        f'supervisor log (last 16384 bytes):\n{output}') from error
                finally:
                    if supervisor.poll() is None:
                        supervisor.terminate()
                        try:
                            supervisor.wait(timeout=5)
                        except subprocess.TimeoutExpired:
                            if os.name != 'nt' and (out / 'scratch-path').exists():
                                pid_file = Path((out / 'scratch-path').read_text()) / 'pids.json'
                                if pid_file.exists():
                                    leader = json.loads(pid_file.read_text())[0]
                                    try:
                                        os.killpg(leader, signal.SIGKILL)
                                    except ProcessLookupError:
                                        pass
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


class PointerArtifactTests(unittest.TestCase):
    """Pure contract/extraction checks; never launch a GUI or build an SDK."""
    def setUp(self):
        self.source = dict(commit='a'*40, fingerprint='b'*64)
        self.package = 'DuneCity-1.0.865-macOS.zip'
        self.files = {'Contents/MacOS/dunecity': 'c'*64, 'Contents/Resources/DUNE.PAK': 'd'*64}
        self.binary = 'e'*64
        manifest = pointer.read_json(pointer.ROOT/'vcpkg.json')
        versions = {item['name']: item['version'] for item in manifest['overrides']}
        self.sdk = dict(prefix='/producer/build/vcpkg_installed/arm64-osx',
                        baseline=manifest['builtin-baseline'], archives={}, ports={},
                        version=versions['sdl2'], headerSha256='f'*64)
        for library, port in pointer.PORTS.items():
            if library == 'libSDL2main.a':
                continue
            archive_hash = hashlib.sha256(library.encode()).hexdigest()
            abi = 'portfile.cmake %s\nvcpkg.json %s\ntriplet arm64-osx\ntriplet_abi fixed-triplet-input\n' % ('1'*64, '2'*64)
            document = dict(packages=[
                dict(SPDXID='SPDXRef-port', name=port, versionInfo=versions[port],
                     downloadLocation='git+https://github.com/Microsoft/vcpkg@'+'3'*40),
                dict(SPDXID='SPDXRef-binary', name=port+':arm64-osx',
                     versionInfo=hashlib.sha256(abi.encode()).hexdigest()),
                dict(SPDXID='SPDXRef-resource-0', downloadLocation='git+https://example.org/source@pinned',
                     checksums=[dict(algorithm='SHA512', checksumValue='4'*128)])], files=[])
            for name, prefix, checksum in [('portfile.cmake', 'port', '1'*64), ('vcpkg.json', 'port', '2'*64),
                    ('lib/'+library, 'binary', archive_hash)]:
                document['files'].append(dict(fileName='./'+name, SPDXID='SPDXRef-'+prefix+'-file-'+name,
                    checksums=[dict(algorithm='SHA256', checksumValue=checksum)]))
            if port == 'sdl2':
                document['files'].append(dict(fileName='./include/SDL2/SDL_version.h', SPDXID='SPDXRef-binary-file-header',
                    checksums=[dict(algorithm='SHA256', checksumValue=self.sdk['headerSha256'])]))
            self.sdk['ports'][port] = dict(spdx=document, abiInfo=abi)
            self.sdk['archives'][self.sdk['prefix']+'/lib/'+library] = dict(
                port=port, relative='lib/'+library, sha256=archive_hash)
        self.production = ['/usr/bin/clang++', 'main.cpp.o', *self.sdk['archives'], '-o', pointer.TARGET]
        self.inputs = dict(source=self.source, commands=['cd /producer/build && '+' '.join(self.production)+' && :'],
            productionLink=self.production, normalizedLink=self.production, sdk=self.sdk,
            context={'/usr/bin/clang++': '5'*64, '/producer/build/CMakeCache.txt': '6'*64})
        producer = dict(repository='dunecity-project/dunecity', runId='123', runAttempt='1',
            sourceCommit='7'*40, workflowCommit='7'*40,
            workflowRef='dunecity-project/dunecity/.github/workflows/build.yml@refs/heads/main',
            checkedOutWorkflowSha256=file_hash(pointer.ROOT/'.github/workflows/build.yml'),
            frontendSha256=file_hash(pointer.ROOT/'scripts/macos-ci-lane.sh'))
        self.manifest = dict(schema=1, sourceCommit=self.source['commit'], repository=producer['repository'],
            runId='123', runAttempt='1', producerSourceCommit=producer['sourceCommit'],
            producerWorkflowPath='.github/workflows/build.yml', sha256={self.package: '8'*64}, artifacts=[])
        self.receipt = dict(schema=1, kind='producer-static-sdl-full-menu-pointer-fixture',
            actualPackageExecutable=False, producer=producer, source=self.source, inputs=self.inputs,
            inputsSha256=pointer.value_hash(self.inputs),
            fixture=dict(sha256=self.binary, recipe=dict(buildRoot='/producer/build', compile=[['clang++', '-c', 'fixture.cpp']],
                         link=[*self.production[:-1], '/producer/fixture/menu-probe']),
                         inputHashes={path: item['sha256'] for path, item in self.sdk['archives'].items()}),
            candidate=dict(package=self.package, packageSha256='8'*64, files=self.files))

    def validate(self, receipt=None, manifest=None, source=None, files=None, binary=None):
        return pointer.validate_receipt(receipt or self.receipt, manifest or self.manifest,
            source or self.source, files or self.files, binary or self.binary)

    def seal_manifest(self, archive, path):
        self.manifest['artifacts'] = [dict(id=17,
            name='DuneCity-macOS-pointer-fixture-'+self.source['commit']+'-1', expired=False,
            size_in_bytes=archive.stat().st_size, digest='sha256:'+file_hash(archive))]
        path.write_bytes(pointer.encoded(self.manifest))
        return file_hash(path)

    def envelope(self, directory, members=None, zip_name=pointer.ARCHIVE):
        content = io.BytesIO()
        if members is None:
            members = [('menu-probe', b'fixture'), ('receipt.json', pointer.encoded(self.receipt))]
        with tarfile.open(fileobj=content, mode='w:gz') as archive:
            for name, data in members:
                item = tarfile.TarInfo(name)
                item.mode, item.uid, item.gid = 0o777, 12345, 12345
                if data is None:
                    item.type, item.linkname = tarfile.SYMTYPE, '/tmp/foreign'
                    archive.addfile(item)
                else:
                    item.size = len(data)
                    archive.addfile(item, io.BytesIO(data))
        path = directory/'diagnostic.zip'
        with zipfile.ZipFile(path, 'w') as archive:
            archive.writestr(zip_name, content.getvalue())
        return path


    def test_full_receipt_binds_source_producer_candidate_fixture_and_sdk(self):
        self.assertEqual(self.validate(), self.sdk['version'])
        variants = []
        for field in ('runId', 'runAttempt', 'repository', 'workflowCommit', 'checkedOutWorkflowSha256', 'frontendSha256'):
            value = copy.deepcopy(self.receipt)
            value['producer'][field] = 'foreign'
            variants.append(value)
        value = copy.deepcopy(self.receipt)
        value['actualPackageExecutable'] = True
        variants.append(value)
        value = copy.deepcopy(self.receipt)
        value['fixture']['inputHashes'].clear()
        variants.append(value)
        value = copy.deepcopy(self.receipt)
        value['candidate']['packageSha256'] = '9'*64
        variants.append(value)
        for value in variants:
            with self.subTest(value=value['producer']), self.assertRaisesRegex(RuntimeError, 'POINTER_PREREQUISITE_BLOCKED'):
                self.validate(receipt=value)
        for arguments in [dict(source=dict(commit='0'*40, fingerprint='b'*64)),
                          dict(files={**self.files, 'Contents/MacOS/dunecity': '0'*64}), dict(binary='0'*64)]:
            with self.subTest(arguments=arguments), self.assertRaises(RuntimeError):
                self.validate(**arguments)

    def test_archive_recipe_abi_and_upstream_hashes_are_required(self):
        first = next(iter(self.sdk['archives']))
        variants = []
        for mutation in ('archive', 'abi', 'recipe', 'source', 'header', 'baseline'):
            sdk = copy.deepcopy(self.sdk)
            if mutation == 'archive': sdk['archives'][first]['sha256'] = '0'*64
            if mutation == 'abi': sdk['ports']['sdl2']['abiInfo'] += 'unknown changed\n'
            if mutation == 'recipe': sdk['ports']['sdl2']['spdx']['files'][0]['checksums'][0]['checksumValue'] = '0'*64
            if mutation == 'source': sdk['ports']['sdl2']['spdx']['packages'][2]['checksums'] = []
            if mutation == 'header': sdk['headerSha256'] = '0'*64
            if mutation == 'baseline': sdk['baseline'] = '0'*40
            variants.append((mutation, sdk))
        manifest = pointer.read_json(pointer.ROOT/'vcpkg.json')
        for name, sdk in variants:
            with self.subTest(mutation=name), self.assertRaises(RuntimeError):
                pointer.validate_sdk(sdk, manifest, self.production)
        for command in [self.production+['/opt/homebrew/lib/libSDL2.dylib'],
                        self.production+['-lSDL2'], self.production+['-framework', 'SDL2']]:
            with self.subTest(command=command), self.assertRaisesRegex(RuntimeError, 'foreign, implicit or different SDL'):
                pointer.validate_sdk(self.sdk, manifest, self.production, command)

    def test_publication_pin_authenticates_raw_actions_zip_and_old_missing_is_blocked(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            archive = self.envelope(folder)
            manifest = folder/'publication-inputs.json'
            pin = self.seal_manifest(archive, manifest)
            self.assertEqual(pointer.authenticate_artifact(archive, manifest, pin, self.source['commit'])[1]['id'], 17)
            with self.assertRaises(RuntimeError):
                pointer.authenticate_artifact(archive, manifest, '0'*64, self.source['commit'])
            with self.assertRaises(RuntimeError):
                pointer.authenticate_artifact(archive, manifest, pin, '0'*40)
            archive.write_bytes(archive.read_bytes()+b'tampered')
            with self.assertRaisesRegex(RuntimeError, 'tampered'):
                pointer.authenticate_artifact(archive, manifest, pin, self.source['commit'])
            for entries in ([], [dict(self.manifest['artifacts'][0], expired=True)]):
                self.manifest['artifacts'] = entries
                manifest.write_bytes(pointer.encoded(self.manifest))
                with self.assertRaises(RuntimeError):
                    pointer.authenticate_artifact(archive, manifest, file_hash(manifest), self.source['commit'])


    def test_safe_owned_extraction_ignores_archive_permissions_and_rejects_links_traversal_duplicates(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            archive = self.envelope(folder)
            payload = pointer.extract_artifact(archive, folder/'good')
            self.assertEqual((payload/'menu-probe').stat().st_mode & 0o777, 0o644)
            self.assertEqual((payload/'menu-probe').stat().st_uid, os.getuid())
            for index, members in enumerate([
                    [('../escape', b'foreign'), ('receipt.json', b'{}')],
                    [('menu-probe', None), ('receipt.json', b'{}')],
                    [('menu-probe', b'one'), ('menu-probe', b'two'), ('receipt.json', b'{}')]]):
                archive = self.envelope(folder, members)
                with self.subTest(index=index), self.assertRaisesRegex(RuntimeError, 'unsafe'):
                    pointer.extract_artifact(archive, folder/str(index))
            archive = self.envelope(folder, zip_name='../'+pointer.ARCHIVE)
            with self.assertRaisesRegex(RuntimeError, 'unsafe'):
                pointer.extract_artifact(archive, folder/'bad-envelope')
            archive = self.envelope(folder)
            with patch.object(pointer, 'MAX_PAYLOAD', 1), self.assertRaisesRegex(RuntimeError, 'oversized'):
                pointer.extract_artifact(archive, folder/'oversized')
            with self.assertRaisesRegex(RuntimeError, 'already exists'):
                pointer.extract_artifact(archive, folder/'good')
            folder.chmod(0o777)
            try:
                with self.assertRaisesRegex(RuntimeError, 'private owned'):
                    pointer.extract_artifact(archive, folder/'not-private')
            finally:
                folder.chmod(0o700)

    def test_static_environment_needs_receipt_and_strips_inherited_dyld_authority(self):
        with tempfile.TemporaryDirectory() as directory:
            app = Path(directory)/'candidate.app'
            app.mkdir()
            frameworks = app/'Contents/Frameworks'
            frameworks.mkdir(parents=True)
            alias = Path(directory)/'candidate-alias.app'
            alias.symlink_to(app, target_is_directory=True)
            env = dict(DYLD_INSERT_LIBRARIES='/opt/homebrew/foreign.dylib', DYLD_LIBRARY_PATH='/opt/homebrew/lib',
                       DYLD_FRAMEWORK_PATH='/opt/homebrew', DYLD_FALLBACK_FRAMEWORK_PATH='/opt/homebrew',
                       MENU_POINTER_VERIFIED_RECEIPT='0'*64, OTHER='retained')
            static = 'dunecity:\n /usr/lib/libSystem.B.dylib (compatibility version 1.0)\n'
            with patch.object(pointer, 'check_output', return_value=static):
                with self.assertRaisesRegex(RuntimeError, 'no local SDK fallback'):
                    pointer.runtime_environment(env, alias)
                result, _ = pointer.runtime_environment(env, alias, self.receipt, '1'*64)
                self.assertNotIn('DYLD_INSERT_LIBRARIES', result)
                self.assertEqual(result['DYLD_LIBRARY_PATH'], str(frameworks.resolve()))
                self.assertEqual(result['DYLD_FRAMEWORK_PATH'], str(frameworks.resolve()))
                self.assertNotIn('DYLD_FALLBACK_FRAMEWORK_PATH', result)
                self.assertEqual(result['MENU_POINTER_VERIFIED_RECEIPT'], '1'*64)
                self.assertEqual(result['MENU_POINTER_LINKAGE'], 'static')
                self.assertEqual(result['OTHER'], 'retained')
            with patch.object(pointer, 'check_output', return_value='dunecity:\n @rpath/libSDL2.dylib (compatibility version 1.0)\n'):
                dynamic, _ = pointer.runtime_environment(env, alias)
                self.assertEqual(dynamic['DYLD_LIBRARY_PATH'], str(frameworks.resolve()))
                self.assertEqual(dynamic['DYLD_FRAMEWORK_PATH'], str(frameworks.resolve()))
                self.assertEqual(dynamic['MENU_POINTER_LINKAGE'], 'dynamic')
                self.assertNotIn('MENU_POINTER_VERIFIED_RECEIPT', dynamic)
                with self.assertRaisesRegex(RuntimeError, 'dynamic candidate'):
                    pointer.runtime_environment(env, alias, self.receipt, '1'*64)

    def test_consumer_only_extracts_verified_fixture_and_never_invokes_sdk_frontend(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            candidate = folder/'candidate.app'
            (candidate/'Contents/MacOS').mkdir(parents=True)
            (candidate/'Contents/Resources').mkdir()
            (candidate/'Contents/MacOS/dunecity').write_bytes(b'candidate Mach-O stand-in')
            (candidate/'Contents/Resources/DUNE.PAK').write_bytes(b'candidate resources')
            self.files = pointer.bundle_files(candidate)
            self.receipt['candidate']['files'] = self.files
            self.receipt['fixture']['sha256'] = hashlib.sha256(b'fixture').hexdigest()
            archive = self.envelope(folder)
            manifest = folder/'publication-inputs.json'
            pin = self.seal_manifest(archive, manifest)
            output = folder/'evidence'
            output.mkdir()
            def commands(command, **kwargs):
                self.assertEqual(command[:2], ['otool', '-L'])
                return 'menu-probe:\n /usr/lib/libSystem.B.dylib (compatibility version 1.0)\n'
            with patch.object(pointer, 'clean_source', return_value=self.source), patch.object(pointer, 'check_output', side_effect=commands):
                binary, receipt, receipt_hash = pointer.consume_fixture(archive, manifest, pin, self.source['commit'],
                    candidate, folder/'extracted', folder/'unused-build', output)
            self.assertEqual(binary.read_bytes(), b'fixture')
            self.assertFalse(receipt['actualPackageExecutable'])
            self.assertEqual(receipt_hash, file_hash(output/'pointer-producer-receipt.json'))
            self.assertEqual((binary.parents[1]/'Resources').resolve(), (candidate/'Contents/Resources').resolve())
            self.assertFalse((folder/'unused-build').exists())


    def test_final_zip_member_is_the_candidate_macho_not_a_filename_claim(self):
        with tempfile.TemporaryDirectory() as directory:
            package = Path(directory)/self.package
            content = b'actual packaged Mach-O bytes'
            with zipfile.ZipFile(package, 'w') as archive:
                archive.writestr('dunecity.app/Contents/MacOS/dunecity', content)
            self.assertEqual(pointer.package_executable_hash(package), hashlib.sha256(content).hexdigest())
            with zipfile.ZipFile(package, 'w') as archive:
                archive.writestr('../dunecity.app/Contents/MacOS/dunecity', content)
            with self.assertRaisesRegex(RuntimeError, 'package Mach-O'):
                pointer.package_executable_hash(package)


    def test_external_cache_churn_preserves_source_but_real_untracked_source_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            source = folder/'candidate'
            source.mkdir()
            (source/'main.cpp').write_bytes(b'exact committed input\n')
            subprocess.run(['git', 'init', '-q', str(source)], check=True)
            subprocess.run(['git', 'config', 'core.excludesFile', os.devnull], cwd=source, check=True)
            subprocess.run(['git', 'add', 'main.cpp'], cwd=source, check=True)
            subprocess.run(['git', '-c', 'user.name=Pointer contract test', '-c', 'user.email=pointer-test@invalid',
                            '-c', 'core.hooksPath='+os.devnull, '-c', 'commit.gpgsign=false',
                            'commit', '-qm', 'Exact candidate'], cwd=source, check=True)
            expected = pointer.clean_source(source, source/'build', folder/'evidence')
            status_output = folder/'gate-status.json'
            cache = source/'.ccache/0/stats'
            cache.parent.mkdir(parents=True)
            cache.write_bytes(b'compiler cache output\n')
            with self.assertRaisesRegex(RuntimeError, 'source is not the exact committed input') as failure:
                pointer.clean_source(source, source/'build', folder/'evidence',
                                     expected_commit=expected['commit'], status_output=status_output)
            diagnostic = pointer.read_json(status_output)
            self.assertEqual(diagnostic, json.loads(str(failure.exception).split('; ', 1)[1]))
            self.assertEqual(diagnostic['status'], 'blocked-prerequisite')
            self.assertEqual(diagnostic['reasonCode'], 'source-fingerprint-mismatch')
            self.assertEqual(diagnostic['sourceCommit'], expected['commit'])
            self.assertEqual(diagnostic['sourceRoot'], str(source.resolve()))
            self.assertNotEqual(diagnostic['worktreeFingerprint'], diagnostic['committedFingerprint'])
            self.assertTrue(any('.ccache/0/stats' in path for path in diagnostic['dirtyPaths']))
            external_cache = folder/'compiler-cache'
            shutil.move(str(source/'.ccache'), external_cache)
            for content in (b'new cached object\n', b'more compiler-cache churn\n'):
                (external_cache/'0/stats').write_bytes(content)
                self.assertEqual(pointer.clean_source(source, source/'build', folder/'evidence',
                    expected_commit=expected['commit']), expected)
            (source/'untracked-source.cpp').write_bytes(b'real untracked source input\n')
            with self.assertRaisesRegex(RuntimeError, 'source is not the exact committed input'):
                pointer.clean_source(source, source/'build', folder/'evidence',
                                     expected_commit=expected['commit'], status_output=status_output)
            self.assertTrue(any('untracked-source.cpp' in path for path in pointer.read_json(status_output)['dirtyPaths']))
            with self.assertRaisesRegex(RuntimeError, 'HEAD differs from the selected source'):
                pointer.clean_source(source, source/'build', folder/'evidence',
                                     expected_commit='0'*40, status_output=status_output)
            diagnostic = pointer.read_json(status_output)
            self.assertEqual(diagnostic['reasonCode'], 'source-head-mismatch')
            self.assertEqual(diagnostic['sourceCommit'], expected['commit'])
            self.assertEqual(diagnostic['expectedSourceCommit'], '0'*40)


if __name__ == '__main__':
    unittest.main()
