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


def package(build_root, play_root, relay_origin=None, allow_loopback_relay=False, channel='staging'):
    sources = relay_sources(relay_origin, allow_loopback_relay) if relay_origin else []
    repo = Path(__file__).resolve().parents[1]
    version = re.search(r'project\(DuneCity VERSION ([0-9.]+)', (repo / 'CMakeLists.txt').read_text())[1]
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repo, text=True).strip()
    if channel not in ('stable', 'staging'):
        raise ValueError('Expected stable or staging browser channel')
    if (channel == 'staging') != (play_root.name == 'play-staging'):
        raise ValueError('Staging browser packages must use play-staging, never the stable play directory')
    cache = build_root / 'CMakeCache.txt'
    expected_identity = 'OFF' if channel == 'staging' else 'ON'
    if not cache.exists() or not re.search(
            r'^DUNECITY_RELEASE_BUILD:BOOL=' + expected_identity + r'$', cache.read_text(), re.MULTILINE):
        raise ValueError(f'{channel} browser packages require a configured DUNECITY_RELEASE_BUILD={expected_identity}')
    display_version = version if channel == 'stable' else f'{version}dev+{commit[:12]}'
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
    token = channel + '-' + display_version + '-' + runtime_hash.hexdigest()[:12]
    play_root.mkdir(parents=True, exist_ok=True)
    for name, source in files.items():
        shutil.copyfile(source, play_root / name)
    index = play_root / 'index.html'
    html = index.read_text()
    if channel == 'staging':
        label = f'Staging {display_version}'
        if '<span>Browser edition</span>' in html:
            html = html.replace('<span>Browser edition</span>', f'<span>{label}</span>')
        else:
            html = html.replace('<body>', f'<body><p id="staging-identity">{label}</p>', 1)
        # IDBFS names its database after the mountpoint. Keep the game's home
        # path, but link it to a separate database so dev never rewrites stable saves.
        shell = play_root / 'shell.js'
        shell_text = shell.read_text()
        mount = "FS.mount(IDBFS, {}, '/home/web_user');"
        if shell_text.count(mount) != 1:
            raise RuntimeError('Expected one browser persistence mount')
        shell.write_text(shell_text.replace(mount,
            "FS.mkdirTree('/home/web_user_staging');\n"
            "        FS.mount(IDBFS, {}, '/home/web_user_staging');\n"
            "        FS.rmdir('/home/web_user');\n"
            "        FS.symlink('/home/web_user_staging', '/home/web_user');"))
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
    if channel == 'staging':
        headers.write_text(headers.read_text() +
                           '\n<IfModule mod_headers.c>\n'
                           '  Header always set Service-Worker-Allowed "/play-staging/"\n'
                           '</IfModule>\n')
    names = [name for name in files if name != '.htaccess']
    hashes = {name: hashlib.sha256((play_root / name).read_bytes()).hexdigest() for name in names}
    manifest = {
        'version': version,
        'sourceCommit': commit,
        'builtAtUtc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'channel': channel,
        'displayVersion': display_version,
        'cacheScope': '/play-staging/' if channel == 'staging' else '/play/',
        'releaseBuild': 'OFF' if channel == 'staging' else 'ON',
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
    parser.add_argument('--channel', choices=('stable', 'staging'), default='staging')
    parser.add_argument('--signaling-origin', '--relay-origin', dest='relay_origin',
                        help='Exact HTTPS origin allowed for lobby and connection introductions')
    parser.add_argument('--allow-loopback-signaling', '--allow-loopback-relay',
                        dest='allow_loopback_relay', action='store_true',
                        help='Allow HTTP signaling on explicit loopback development endpoints')
    args = parser.parse_args()
    package(args.build_root, args.play_root, args.relay_origin, args.allow_loopback_relay, args.channel)
