"""Content identity for simulation sources, not toolchains or rendered assets.

Git's dirty flag also sees generated build products. Hash the actual tracked and
non-ignored source-tree files instead, excluding only this invocation's build
and output trees plus Python/npm caches. The queue's immutable source snapshot
keeps those inputs fixed while a match runs. An incomplete identity is an error,
never a claim that an unknown source matches another run.

Version 2 hashes logical symbolic-link targets, not the files they point to.
Git's mode-120000 Windows placeholders and real Unix symlinks therefore describe
the same source entry without ignoring files or normalizing ordinary source bytes.
"""
import hashlib
import os
from pathlib import Path
import subprocess


SOURCE_FINGERPRINT_VERSION = 'dunecity-simulation-source-v2'


def _entry_header(digest, name, logical_link, size):
    encoded = name.encode('utf-8', errors='surrogateescape')
    digest.update(len(encoded).to_bytes(8, 'big'))
    digest.update(encoded)
    digest.update(b'L' if logical_link else b'F')
    digest.update(size.to_bytes(8, 'big'))


def source_fingerprint(root: Path, build: Path, output: Path) -> str:
    root = root.resolve()
    excluded = (build.resolve(), output.resolve())
    names = subprocess.check_output(
        ['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard'],
        cwd=root).decode('utf-8', errors='surrogateescape').split('\0')
    entries = subprocess.check_output(['git', 'ls-files', '--stage', '-z'], cwd=root).split(b'\0')
    tracked_links = {entry.split(b'\t', 1)[1].decode('utf-8', errors='surrogateescape')
                     for entry in entries if entry.startswith(b'120000 ')}
    digest = hashlib.sha256(SOURCE_FINGERPRINT_VERSION.encode() + b'\0')
    for name in sorted(set(filter(None, names))):
        relative = Path(name)
        if any(part in ('__pycache__', 'node_modules', '.pytest_cache') for part in relative.parts):
            continue
        path = root / relative
        actual_link = path.is_symlink()
        logical_link = actual_link or name in tracked_links
        resolved = path.absolute() if logical_link else path.resolve()
        if any(resolved == target or target in resolved.parents for target in excluded):
            continue
        if not actual_link and not path.is_file():
            raise RuntimeError(f'Cannot fingerprint source input: {name}')
        if logical_link:
            target = os.fsencode(os.readlink(path)) if actual_link else path.read_bytes()
            _entry_header(digest, name, True, len(target))
            digest.update(target)
            continue
        _entry_header(digest, name, False, path.stat().st_size)
        with path.open('rb') as stream:
            while True:
                chunk = stream.read(1024 * 1024)
                if not chunk:
                    break
                digest.update(chunk)
    return digest.hexdigest()


def pinned_source_fingerprint(root: Path, source: str) -> str:
    """The same v2 identity, derived from immutable Git blobs rather than a dirty checkout."""
    tree = subprocess.check_output(['git', 'ls-tree', '-rz', '--full-tree', source], cwd=root)
    entries = []
    for entry in tree.split(b'\0'):
        if not entry:
            continue
        metadata, raw_name = entry.split(b'\t', 1)
        mode, kind, oid = metadata.split()
        name = raw_name.decode('utf-8', errors='surrogateescape')
        if any(part in ('__pycache__', 'node_modules', '.pytest_cache') for part in Path(name).parts):
            continue
        if kind != b'blob' or mode not in (b'100644', b'100755', b'120000'):
            raise RuntimeError('Cannot certify source entry: ' + name)
        entries.append((name, mode, oid))
    digest = hashlib.sha256(SOURCE_FINGERPRINT_VERSION.encode() + b'\0')
    process = subprocess.Popen(['git', 'cat-file', '--batch'], cwd=root,
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        for name, mode, oid in sorted(entries):
            process.stdin.write(oid + b'\n')
            process.stdin.flush()
            header = process.stdout.readline().split()
            if len(header) != 3 or header[1] != b'blob':
                raise RuntimeError('Cannot certify source blob: ' + name)
            remaining = int(header[2])
            _entry_header(digest, name, mode == b'120000', remaining)
            while remaining:
                chunk = process.stdout.read(min(1024 * 1024, remaining))
                if not chunk:
                    raise RuntimeError('Truncated source blob: ' + name)
                digest.update(chunk)
                remaining -= len(chunk)
            if process.stdout.read(1) != b'\n':
                raise RuntimeError('Invalid source blob boundary: ' + name)
    finally:
        process.stdin.close()
        process.stdout.close()
        errors = process.stderr.read()
        process.stderr.close()
        status = process.wait()
    if status:
        raise RuntimeError('Cannot certify source tree: ' + errors.decode('utf-8', errors='replace'))
    return digest.hexdigest()
