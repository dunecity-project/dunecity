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


def source_fingerprint(root: Path, build: Path, output: Path) -> str:
    root = root.resolve()
    excluded = (build.resolve(), output.resolve())
    names = subprocess.check_output(
        ['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard'],
        cwd=root).decode('utf-8', errors='surrogateescape').split('\0')
    entries = subprocess.check_output(['git', 'ls-files', '--stage', '-z'], cwd=root).split(b'\0')
    tracked_links = {entry.split(b'\t', 1)[1].decode('utf-8', errors='surrogateescape')
                     for entry in entries if entry.startswith(b'120000 ')}
    digest = hashlib.sha256(b'dunecity-simulation-source-v2\0')
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
        encoded = name.encode('utf-8', errors='surrogateescape')
        digest.update(len(encoded).to_bytes(8, 'big'))
        digest.update(encoded)
        digest.update(b'L' if logical_link else b'F')
        if logical_link:
            target = os.fsencode(os.readlink(path)) if actual_link else path.read_bytes()
            digest.update(len(target).to_bytes(8, 'big'))
            digest.update(target)
            continue
        digest.update(path.stat().st_size.to_bytes(8, 'big'))
        with path.open('rb') as stream:
            while True:
                chunk = stream.read(1024 * 1024)
                if not chunk:
                    break
                digest.update(chunk)
    return digest.hexdigest()
