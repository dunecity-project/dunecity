#!/usr/bin/env python3
"""Package an already-built Emscripten game for the Play Online website."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
from urllib.parse import urlsplit


def relay_sources(origin, allow_loopback=False):
    """Legacy helper name: allow HTTPS introductions only, never a gameplay socket."""
    parsed = urlsplit(origin)
    host = parsed.hostname or ''
    if (parsed.scheme not in ('http', 'https') or not host
            or parsed.username is not None or parsed.password is not None
            or parsed.path not in ('', '/') or parsed.query or parsed.fragment
            or not re.fullmatch(r'[A-Za-z0-9.:-]+', host)
            or any(char.isspace() for char in origin)):
        raise ValueError('Signaling must be an exact HTTP(S) origin without credentials or a path')
    port = parsed.port  # validates the port, including its range
    if parsed.scheme == 'http' and not (
            allow_loopback and host in ('127.0.0.1', 'localhost', '::1')):
        raise ValueError('Plain HTTP signaling requires explicit loopback development opt-in')
    authority = f'[{host}]' if ':' in host else host
    if port is not None:
        authority += ':' + str(port)
    canonical = parsed.scheme + '://' + authority
    if origin.rstrip('/') != canonical:
        raise ValueError('Signaling must be a canonical origin')
    return [canonical]


def allow_relay_connections(text, sources):
    if not sources:
        return text
    text, count = re.subn(r'connect-src ([^;"<>]+);',
                         lambda match: 'connect-src ' + match[1] + ' ' + ' '.join(sources) + ';', text)
    if count != 1:
        raise RuntimeError('Expected exactly one connect-src policy')
    return text

# GitHub rejects files over 100 MB, and the website repository stores the package. A larger
# artifact is committed as ordered parts; the website deploy joins them (deploy/play_parts.py)
# before compressing and serving, so browsers still fetch one file.
PART_BYTES = 64 * 1024 * 1024


def split_large_artifacts(play_root, names):
    split = {}
    for name in names:
        for stale in play_root.glob(name + '.part-*'):
            stale.unlink()
        path = play_root / name
        if path.stat().st_size <= PART_BYTES:
            continue
        parts = []
        with path.open('rb') as source:
            for chunk in iter(lambda: source.read(PART_BYTES), b''):
                part = f'{name}.part-{len(parts):03d}'
                (play_root / part).write_bytes(chunk)
                parts.append(part)
        path.unlink()
        split[name] = parts
    return split


def package(build_root, play_root, relay_origin=None, allow_loopback_relay=False):
    sources = relay_sources(relay_origin, allow_loopback_relay) if relay_origin else []
    repo = Path(__file__).resolve().parents[1]
    version = re.search(r'project\(DuneCity VERSION ([0-9.]+)', (repo / 'CMakeLists.txt').read_text())[1]
    output = build_root / 'bin'
    files = {'index.html': output / 'dunecity.html',
             **{name: output / name for name in ('dunecity.js', 'dunecity.wasm', 'dunecity.data', 'p2p-direct.js')},
             **{name: repo / 'web' / name for name in ('shell.js', 'shell.css', '.htaccess')}}
    for source in files.values():
        if not source.is_file():
            raise RuntimeError(f'Missing browser artifact: {source}')
    # The bridge can change without recompiling wasm. Version the complete runtime so a
    # transport-only rebuild cannot silently keep an old peer protocol in the browser cache.
    runtime_hash = hashlib.sha256()
    for name in sorted(files):
        if name not in ('index.html', '.htaccess'):
            runtime_hash.update(name.encode() + b'\0')
            runtime_hash.update(hashlib.sha256(files[name].read_bytes()).digest())
    token = version + '-' + runtime_hash.hexdigest()[:12]
    play_root.mkdir(parents=True, exist_ok=True)
    for name, source in files.items():
        shutil.copyfile(source, play_root / name)
    index = play_root / 'index.html'
    html = index.read_text()
    for name in ('shell.css', 'shell.js', 'p2p-direct.js', 'dunecity.js'):
        # Release HTML is minified and may have unquoted attributes.
        html, count = re.subn(r"\b(src|href)=[\"']?" + re.escape(name) + r"(?:[\"']|(?=[\s>]))",
                              lambda match: match[1] + '="' + name + '?v=' + token + '"', html)
        if count != 1:
            raise RuntimeError(f'Expected exactly one HTML reference to {name}, found {count}')
    index.write_text(allow_relay_connections(html, sources))
    headers = play_root / '.htaccess'
    header_text = allow_relay_connections(headers.read_text(), sources)
    if sources and sources[0].startswith('http:'):
        # This opt-in artifact is only for a loopback development server.
        header_text = header_text.replace('; upgrade-insecure-requests', '')
    headers.write_text(header_text)
    names = [name for name in files if name != '.htaccess']
    hashes = {name: hashlib.sha256((play_root / name).read_bytes()).hexdigest() for name in names}
    manifest = {
        'version': version,
        'sourceCommit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repo, text=True).strip(),
        'builtAtUtc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'artifacts': names,
        'relayOrigins': [],  # retained for old manifest readers; gameplay is direct-only
        'signalingOrigins': sources,
        'gameTransport': 'direct-webrtc',
        'sha256': hashes,
    }
    split = split_large_artifacts(play_root, names)
    if split:
        manifest['split'] = split
    (play_root / 'build.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'Packaged DuneCity {version} at {play_root}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-root', type=Path, required=True)
    parser.add_argument('--play-root', type=Path, required=True)
    parser.add_argument('--signaling-origin', '--relay-origin', dest='relay_origin',
                        help='Exact HTTPS origin allowed for lobby and connection introductions')
    parser.add_argument('--allow-loopback-signaling', '--allow-loopback-relay',
                        dest='allow_loopback_relay', action='store_true',
                        help='Allow HTTP signaling on explicit loopback development endpoints')
    args = parser.parse_args()
    package(args.build_root, args.play_root, args.relay_origin, args.allow_loopback_relay)
