"""The native probes' shared, sealed source/toolchain/runtime fixture publication."""
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import shlex
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ai'))
from source_provenance import source_fingerprint


def _snapshot(path):
    path = Path(path)
    canonical = path.resolve(strict=True)
    link, target = path.lstat(), canonical.stat()
    fields = lambda info: [info.st_dev, info.st_ino, info.st_mode, info.st_size,
                           info.st_mtime_ns, info.st_ctime_ns]
    return [str(canonical), fields(link), fields(target)]


def file_hash(path, cache=None):
    """Local coherence cache, not an attestation against privileged metadata rewriting."""
    path = Path(path)
    before = _snapshot(path)
    record = (cache or {}).get(str(path))
    if (isinstance(record, dict) and record.get('snapshot') == before
            and isinstance(record.get('digest'), str) and len(record['digest']) == 64
            and all(value in '0123456789abcdef' for value in record['digest'])):
        return record['digest']
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    if _snapshot(path) != before:
        raise RuntimeError('Fixture input changed while hashing: ' + str(path))
    value = digest.hexdigest()
    if cache is not None:
        cache[str(path)] = {'snapshot': before, 'digest': value}
    return value


@contextmanager
def _publication_lock(path):
    with path.open('a+b') as lock:
        if os.name == 'nt':
            import msvcrt
            if lock.tell() == 0:
                lock.write(b'\0')
                lock.flush()
            lock.seek(0)
            msvcrt.locking(lock.fileno(), msvcrt.LK_LOCK, 1)
        else:
            import fcntl
            fcntl.flock(lock, fcntl.LOCK_EX)
        yield


def _runtime_files(directory, ancestors=()):
    canonical = directory.resolve(strict=True)
    if canonical in ancestors:
        raise RuntimeError('Cyclic runtime asset directory: ' + str(directory))
    for child in sorted(directory.iterdir()):
        if child.is_dir():
            yield from _runtime_files(child, ancestors + (canonical,))
        elif child.is_file():
            yield child
        elif child.is_symlink():
            raise RuntimeError('Dangling runtime asset link: ' + str(child))


def fixture_inputs(build, target, commands, production_link, runtime_app=None):
    """Only configured/link/toolchain/runtime inputs, never arbitrary build outputs."""
    build = Path(build)
    inputs = {build / target, build / 'CMakeCache.txt', build / 'build.ninja'}
    inputs.update(build / arg for arg in production_link if arg.endswith(('.o', '.a', '.dylib', '.so')))
    inputs.update(Path(arg) for arg in production_link
                  if Path(arg).is_absolute() and Path(arg).is_file() and os.access(arg, os.X_OK))
    for line in commands:
        if ' -c ' not in line:
            continue
        command = shlex.split(line)
        for arg in command[:command.index('-c')]:
            if Path(arg).is_absolute() and Path(arg).is_file() and os.access(arg, os.X_OK):
                inputs.add(Path(arg))
    runtimes = {(build / target).parents[2].resolve()}
    if runtime_app is not None:
        runtimes.add(Path(runtime_app).resolve())
    for runtime in sorted(runtimes):
        inputs.add(runtime / 'Contents/MacOS/dunecity')
        path = runtime / 'Contents/Info.plist'
        if path.is_file():
            inputs.add(path)
        for name in ('Frameworks', 'Resources'):
            directory = runtime / 'Contents' / name
            if directory.exists():
                inputs.update(_runtime_files(directory))
    return sorted(inputs)


def prepare_fixture(root, build, output, directory, executable, commands, production_link, compile_fixture,
                    *, target='bin/dunecity.app/Contents/MacOS/dunecity', variant=None,
                    runtime_app=None, require_prepared=False):
    """Publish once, or fail closed on a missing/stale runtime-only fixture.

    Source membership is validated once per wrapper invocation, outside its case
    loop. Per-file content hashes are reused only after fresh canonical/stat checks.
    Failed/incomplete publications and their build logs are kept for inspection.
    """
    root, build, output, directory = (Path(path).resolve() for path in (root, build, output, directory))
    executable = Path(executable)
    if executable.is_absolute() or '..' in executable.parts:
        raise ValueError('Fixture executable must be inside its publication directory')
    directory.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    identity_directory = build / 'native-probe-identity'
    identity_directory.mkdir(parents=True, exist_ok=True)
    with _publication_lock(directory / 'publication.lock'), _publication_lock(identity_directory / 'publication.lock'):
        cache_path = identity_directory / 'fingerprint-cache.json'
        try:
            original = cache_path.read_text()
            cache = json.loads(original)
        except (OSError, ValueError):
            original, cache = None, {}
        if not isinstance(cache, dict):
            cache = {}
        if not isinstance(cache.get('files'), dict):
            cache['files'] = {}
        if not isinstance(cache.get('source'), dict):
            cache['source'] = {}

        def identity():
            paths = fixture_inputs(build, target, commands, production_link, runtime_app)
            before = {str(path): _snapshot(path) for path in paths}
            inputs = {str(path): file_hash(path, cache['files']) for path in paths}
            source = source_fingerprint(root, build, output, cache=cache['source'])
            after_paths = fixture_inputs(build, target, commands, production_link, runtime_app)
            if before != {str(path): _snapshot(path) for path in after_paths}:
                raise RuntimeError('Fixture input membership/content changed during identity validation')
            return {'version': 1, 'source_root': str(root), 'build_root': str(build),
                    'source': source, 'commands': commands, 'variant': variant, 'inputs': inputs}

        expected = identity()
        key = hashlib.sha256(json.dumps(expected, sort_keys=True).encode()).hexdigest()
        fixture = directory / key
        if not fixture.exists():
            if require_prepared:
                raise RuntimeError('Missing current fixture; run --prepare-only with the same --fixture-dir: ' + str(directory))
            staged = Path(tempfile.mkdtemp(prefix=key + '.incoming-', dir=directory))
            try:
                print('PROBE_FIXTURE_PHASE: compile ' + str(staged), flush=True)
                compile_started = time.monotonic()
                try:
                    compile_fixture(staged)
                finally:
                    print('PROBE_FIXTURE_COMPILE: %.3fs' % (time.monotonic() - compile_started), flush=True)
                if identity() != expected:
                    raise RuntimeError('Source/toolchain/runtime changed during fixture preparation')
                (staged / 'manifest.json').write_text(json.dumps(
                    {'identity': expected, 'binary': file_hash(staged / executable)}, sort_keys=True) + '\n')
                staged.rename(fixture)
            except BaseException:
                print('Kept incomplete fixture evidence: ' + str(staged), file=sys.stderr)
                raise
        manifest = json.loads((fixture / 'manifest.json').read_text())
        binary = fixture / executable
        if (fixture.is_symlink() or binary.is_symlink() or manifest['identity'] != expected
                or manifest['binary'] != file_hash(binary, cache['files'])):
            raise RuntimeError('Fixture seal differs; refusing stale or modified executable: ' + str(fixture))
        updated = json.dumps(cache, sort_keys=True) + '\n'
        if updated != original:
            temporary = None
            try:
                with tempfile.NamedTemporaryFile('w', dir=identity_directory, prefix='fingerprint-', suffix='.incoming', delete=False) as stream:
                    temporary = Path(stream.name)
                    stream.write(updated)
                temporary.replace(cache_path)
            finally:
                if temporary is not None:
                    temporary.unlink(missing_ok=True)
        output.mkdir(parents=True, exist_ok=True)
        (output / 'fixture-reference.json').write_text(json.dumps(
            {'fixture': str(fixture), 'manifest': str(fixture / 'manifest.json'), 'key': key,
             'source': expected['source'], 'source_root': str(root), 'build_root': str(build),
             'binary': str(binary), 'binary_sha256': manifest['binary']}, sort_keys=True) + '\n')
    print('PROBE_FIXTURE_READY: %.3fs %s' % (time.monotonic() - started, fixture), flush=True)
    return binary
