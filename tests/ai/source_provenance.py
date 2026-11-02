"""Content identity for simulation sources, not toolchains or rendered assets.

Git's dirty flag also sees generated build products. Hash the actual tracked and
non-ignored source-tree files instead, excluding only this invocation's build
and output trees plus Python/npm caches. The queue's immutable source snapshot
keeps those inputs fixed while a match runs. An incomplete identity is an error,
never a claim that an unknown source matches another run.
"""
import hashlib
from pathlib import Path
import subprocess


def source_fingerprint(root: Path, build: Path, output: Path) -> str:
    root = root.resolve()
    excluded = (build.resolve(), output.resolve())
    names = subprocess.check_output(
        ['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard'],
        cwd=root).decode('utf-8', errors='surrogateescape').split('\0')
    digest = hashlib.sha256(b'dunecity-simulation-source-v1\0')
    for name in sorted(set(filter(None, names))):
        relative = Path(name)
        if any(part in ('__pycache__', 'node_modules', '.pytest_cache') for part in relative.parts):
            continue
        path = root / relative
        resolved = path.resolve()
        if any(resolved == target or target in resolved.parents for target in excluded):
            continue
        if not path.is_file():
            raise RuntimeError(f'Cannot fingerprint source input: {name}')
        encoded = name.encode('utf-8', errors='surrogateescape')
        digest.update(len(encoded).to_bytes(8, 'big'))
        digest.update(encoded)
        digest.update(path.stat().st_size.to_bytes(8, 'big'))
        with path.open('rb') as stream:
            while True:
                chunk = stream.read(1024 * 1024)
                if not chunk:
                    break
                digest.update(chunk)
    return digest.hexdigest()
