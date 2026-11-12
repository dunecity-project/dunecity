"""Shared isolated-profile construction and scratch lifecycle for the real-engine probes.

A saved game records one exact mod revision. Copying only the well-known mod
names leaves a workshop-installed revision resolvable solely through the user's
workshop cache, which makes a probe depend on external state and fail offline
with "The exact mod version for this game could not be loaded". So every mod
directory present in the source profile is cloned, and the active mod is taken
from that profile's own metadata rather than hardcoded.

Every game also makes the engine reinstall the bundled mods (~36 MB) and
capture workshop revisions (~30 MB) into its fresh profile. Batches therefore
run in leased scratch (scratch_dir), delete each game's directory once its rows
are written, and check free space before they start (require_free_space).
"""
import configparser
import gzip
import hashlib
from contextlib import contextmanager
import importlib.util
import os
import json
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import threading
import time

MINIMAL_INI = ('[Video]\nPhysical Width = 640\nPhysical Height = 480\nWidth = 640\nHeight = 480\n'
               'Fullscreen = false\n[General]\nPlay Intro = false\n')
PROJECT = 'dunecity'
# Regenerated or copied bulk of a finished game's profile; never evidence.
DISPOSABLE_PROFILE_ASSETS = ('mods', 'workshop/revisions')
# A batch keeps at most this many failed games (logs, saves, replays; no bulk) in scratch/failed.
FAILED_GAMES_KEEP = 5
_failed_lock = threading.Lock()
HASH = re.compile(rb'[0-9a-f]{64}')


def remove_tree(path):
    """Remove a private probe tree, including read-only Windows snapshots."""
    def retry_readonly(function, filename, error):
        if isinstance(error, FileNotFoundError):
            return
        if (sys.platform != 'win32' or not isinstance(error, PermissionError)
                or function not in (os.unlink, os.rmdir)):
            raise error
        os.chmod(filename, stat.S_IWRITE)
        function(filename)

    if sys.version_info >= (3, 12):
        shutil.rmtree(path, onexc=retry_readonly)
    else:
        def onerror(function, filename, exc_info):
            retry_readonly(function, filename, exc_info[1])
        shutil.rmtree(path, onerror=onerror)

def headless_asset_arguments():
    """Batch-only opt-in; direct CTest/UI probes retain the normal asset loader."""
    return ['--no-headless-assets'] if os.environ.get('DUNECITY_HEADLESS_ASSETS') == '0' else ['--headless-assets']


def shared_headless_mods(binary, build, env, fingerprint, lock_exclusive):
    """Publish complete native rules and installer artwork once, never user mods.

    The seed-only engine authors all logical files and declares its artwork
    input. That exact trusted input is staged before the immutable publication;
    ordinary game starts, approval and Workshop revision bytes stay unchanged.
    """
    if not fingerprint:
        return None
    binary, build = Path(binary), Path(build)
    staged = None

    def payload_hashes(directory):
        if directory.is_symlink() or not directory.is_dir():
            raise RuntimeError('Missing or linked shared headless mods')
        result = {}
        for path in sorted(directory.rglob('*')):
            if path.is_symlink():
                raise RuntimeError('Linked file in shared headless mods')
            if path.is_file():
                result[path.relative_to(directory).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
            elif not path.is_dir():
                raise RuntimeError('Non-regular file in shared headless mods')
        return result

    started = time.monotonic()
    try:
        print('HEADLESS_MOD_PHASE: validating bundled inputs', flush=True)
        runtime = build / ('bin/dunecity.app/Contents/Resources' if sys.platform == 'darwin' else 'bin')
        trusted = runtime.resolve(strict=True)
        artwork = (trusted / 'mods/dunecity/graphics_skins').resolve(strict=True)
        if not artwork.is_relative_to(trusted):
            raise RuntimeError('Bundled artwork escapes the configured runtime')
        art_files = payload_hashes(artwork)
        templates = {}
        for name in ('ObjectData.ini', 'QuantBot Config.ini'):
            path = trusted / 'config' / (name + '.default')
            if path.is_symlink() or not path.resolve(strict=True).is_relative_to(trusted):
                raise RuntimeError('Bundled rules escape the configured runtime')
            templates[name] = hashlib.sha256(path.read_bytes()).hexdigest()
        bundled = hashlib.sha256(json.dumps({'artwork': art_files, 'templates': templates},
                                           sort_keys=True).encode()).hexdigest()
        identity = {'format': 2, 'binary': hashlib.sha256(binary.read_bytes()).hexdigest(),
                    'source': fingerprint, 'bundled': bundled}
        key = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()
        cache = build / 'headless-mod-cache'
        destination = cache / key
        cache.mkdir(parents=True, exist_ok=True)
        with (cache / (key + '.lock')).open('a+b') as handle:
            if handle.tell() == 0:
                handle.write(b'\0')
                handle.flush()
            handle.seek(0)
            lock_exclusive(handle)
            if destination.exists():
                manifest = json.loads((destination / 'manifest.json').read_text())
                if manifest['identity'] != identity or manifest['files'] != payload_hashes(destination / 'mods'):
                    raise RuntimeError('Shared headless mod fingerprint differs')
                print('HEADLESS_MOD_PHASE: reused %s (%.3fs)' % (destination, time.monotonic() - started), flush=True)
                return destination / 'mods'
            with scratch_dir('headless-mod-seed') as scratch:
                seed_env = dict(env, DUNECITY_USERDIR=str(scratch / 'profile'), DUNECITY_HEADLESS_ASSETS='1',
                                DUNECITY_HEADLESS_SEED_ONLY='1')
                for name in ('DUNECITY_SHARED_MODS', 'DUNECITY_SIM_MEASURE', 'DUNECITY_SIM_SAMPLE_SECONDS',
                             'BALANCE_HEAP_PROFILE', 'BALANCE_HEAP_TOOLS', 'MallocStackLogging'):
                    seed_env.pop(name, None)
                print('HEADLESS_MOD_PHASE: native rules seed (120s bound)', flush=True)
                with (scratch / 'seed.log').open('w') as log:
                    subprocess.run([str(binary), '--window', '--showlog', '--non-interactive'],
                                   cwd=scratch, env=seed_env, stdout=log, stderr=subprocess.STDOUT,
                                   check=True, timeout=120)
                marker = 'HEADLESS_MOD_ARTWORK_V1: '
                declarations = [line.split(marker, 1)[1] for line in (scratch / 'seed.log').read_text().splitlines()
                                if marker in line]
                if len(declarations) != 1:
                    raise RuntimeError('Native seed must declare exactly one bundled artwork input')
                declared = Path(declarations[0])
                if not declared.is_absolute() or declared.resolve(strict=True) != artwork:
                    raise RuntimeError('Native artwork declaration differs from the trusted bundle')
                native = scratch / 'profile/mods'
                native_files = payload_hashes(native)
                for mod in ('vanilla', 'dunecity'):
                    for name in ('ObjectData.ini', 'QuantBot Config.ini', 'GameOptions.ini', 'mod.ini'):
                        if mod + '/' + name not in native_files:
                            raise RuntimeError('Native seed omitted required rules or metadata')
                    for name, digest in templates.items():
                        if native_files[mod + '/' + name] != digest:
                            raise RuntimeError('Native seed rules differ from the trusted bundle')
                staged = Path(tempfile.mkdtemp(prefix=key + '.incoming-', dir=cache))
                for name in ('vanilla', 'dunecity'):
                    clone_tree(native / name, staged / 'mods' / name)
                target = staged / 'mods/dunecity/graphics_skins'
                if target.exists() or target.is_symlink():
                    raise RuntimeError('Native seed shadows the staged artwork target')
                # Reuse only byte-verified, complete publications of this harness.
                # Their logical files are never reused: the current engine authored
                # those above. The trusted bundle remains the artwork authority.
                source = artwork
                prefix = 'dunecity/graphics_skins/'
                cache_stamp = '.bundled-skin-fingerprint'
                for previous in sorted(cache.glob('*/manifest.json')):
                    if (previous.is_symlink() or previous.parent.is_symlink()
                            or not re.fullmatch('[0-9a-f]{64}', previous.parent.name)):
                        continue
                    try:
                        old = json.loads(previous.read_text())
                        old_files = old['files']
                        if not isinstance(old_files, dict):
                            continue
                        old_art = {name[len(prefix):]: digest for name, digest in old_files.items()
                                   if name.startswith(prefix) and name != prefix + cache_stamp}
                        if old_art != art_files or old_files != payload_hashes(previous.parent / 'mods'):
                            continue
                    except (OSError, ValueError, KeyError, TypeError, RuntimeError):
                        continue
                    source = previous.parent / 'mods/dunecity/graphics_skins'
                    break
                print('HEADLESS_MOD_PHASE: staging %d artwork files from %s' % (len(art_files), source), flush=True)
                clone_tree(source, target)
                # This local refresh stamp is excluded by both canonical mod
                # approval and Workshop manifests, and is not installer artwork.
                stamp = target / cache_stamp
                if source != artwork and cache_stamp not in art_files and stamp.exists():
                    target.chmod(0o755)
                    stamp.chmod(0o644)
                    stamp.unlink()
                files = payload_hashes(staged / 'mods')
                copied_art = {name[len(prefix):]: digest for name, digest in files.items() if name.startswith(prefix)}
                if copied_art != art_files:
                    raise RuntimeError('Staged artwork membership or bytes differ from the trusted bundle')
                (staged / 'manifest.json').write_text(
                    json.dumps({'identity': identity, 'files': files}, sort_keys=True) + '\n')
            for path in staged.rglob('*'):
                if path.is_file():
                    path.chmod(0o444)
                elif sys.platform != 'win32':
                    path.chmod(0o555)
            staged.rename(destination)
            staged = None
            print('HEADLESS_MOD_PHASE: published %s (%.3fs)' % (destination, time.monotonic() - started), flush=True)
            return destination / 'mods'
    except (OSError, ValueError, KeyError, TypeError, RuntimeError, subprocess.SubprocessError) as error:
        print(f'Shared headless mods unavailable; using private copies: {error}', file=sys.stderr)
        return None
    finally:
        if staged is not None:
            for path in staged.rglob('*'):
                path.chmod(0o755 if path.is_dir() else 0o644)
            remove_tree(staged)



def default_build_dir(root):
    """Reuse a compatible configured tree, never a clone's absolute CMake path."""
    root = Path(root).resolve()
    for name in ('build-int', 'build'):
        candidate = root / name
        cache = candidate / 'CMakeCache.txt'
        if cache.is_file():
            home = re.search(r'^CMAKE_HOME_DIRECTORY:INTERNAL=(.*)$', cache.read_text(), re.M)
            if home and Path(home.group(1)).resolve() == root:
                return candidate
    if (root / 'build/CMakeCache.txt').is_file():
        raise RuntimeError('No compatible configured build; pass --build-dir for this checkout')
    return root / 'build'


def probe_binary(out):
    """The balance-probe binary run-campaign-balance.py prepared for --output-dir out, for
    --prebuilt-probe reuse. Windows and Linux run it from the game's output directory (data is
    found beside the executable), so the run directory records its path instead of holding a copy."""
    out = Path(out)
    if sys.platform != 'darwin':
        return Path((out / 'balance-probe-path.txt').read_text().strip())
    return out / 'balance-probe.app/Contents/MacOS/balance-probe'


def _installed(name):
    """An installed agent-workspace module on managed hosts; None on CI and other machines."""
    helper = Path.home() / '.local/share/agent-workspace' / (name + '.py')
    config = Path.home() / '.config/agent-workspace/projects.json'
    if not helper.is_file() or not config.is_file():
        return None
    spec = importlib.util.spec_from_file_location('dunecity_agent_' + name, helper)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


@contextmanager
def managed_profile(profile):
    """Use the installed agent lifecycle on managed hosts; CI remains standalone.

    Only disposable copied assets are cleaned. Logs, saves and exact source
    revision evidence remain alongside the profile. Failures retain assets.
    """
    module = _installed('workspace')
    if module is None:
        yield
        return
    workspace = module.Workspace()
    profile = Path(profile).resolve()
    if not any(any(module.beneath(profile, root) for root in workspace.project(name)['artifact_roots'])
               for name in workspace.projects):
        # An unrelated CI checkout or user-selected output path is not silently
        # adopted by the private Mac's cleanup policy.
        yield
        return
    with module.managed_profile(profile):
        yield


@contextmanager
def scratch_dir(purpose, explicit=None):
    """Scratch for a batch of games: the explicit directory if given, else a leased one.

    The leased run directory (agent-workspace scratch root, e.g.
    /private/tmp/dunecity-work/<owner>/<purpose>-<stamp>-<id>) is deleted when the
    batch succeeds. A batch that raised, or kept failed games (finish_game), is marked
    failed instead: the hourly sweep removes it after 72 hours or when the failure caps
    are exceeded. Hosts without agent-workspace get a private temporary directory that
    is deleted the same way (and kept, with its path printed, after a failure).
    Results belong in the caller's results directory, never in scratch.
    """
    if explicit:
        path = Path(explicit).resolve()
        path.mkdir(parents=True, exist_ok=True)
        yield path
        return
    module = _installed('scratch')
    managed = module is not None and PROJECT in module.Scratch().projects
    if managed:
        scratch = module.Scratch()
        owner = os.environ.get('AGENT_WORKSPACE_OWNER', 'local:%d' % os.getpid())
        path = scratch.create(PROJECT, owner, purpose)
    else:
        path = Path(tempfile.mkdtemp(prefix='dunecity-' + purpose + '-'))
    failed = True
    try:
        yield path
        failed = any((path / 'failed').glob('*'))
    finally:
        if failed:
            print('Kept failed scratch for inspection: ' + str(path), file=sys.stderr)
        if managed:
            scratch.finish(path, not failed)
        elif not failed:
            remove_tree(path)


def finish_game(scratch, out, failed):
    """Call once a game's rows are written: delete its run directory straight away.

    A failed game keeps its logs, saves and replays (not the regenerated mods, workshop
    revisions or AI decision log) under scratch/failed, at most FAILED_GAMES_KEEP per
    batch; scratch_dir then keeps the batch as a failed run."""
    out = Path(out)
    if failed and out.is_dir():
        with _failed_lock:
            kept = Path(scratch) / 'failed'
            kept.mkdir(exist_ok=True)
            if sum(1 for _ in kept.iterdir()) < FAILED_GAMES_KEEP:
                discard_profile_assets(out / 'profile', telemetry=True)
                out.rename(kept / out.name)
                return
    remove_tree(out)


def require_free_space(min_gb=None, *, sweep=True):
    """Refuse to start a batch on a nearly full disk, optionally sweeping stale scratch.

    The threshold is the project's min_free_gb on managed hosts (25 GB), overridable
    with DUNECITY_MIN_FREE_GB or the argument. Native UI checks can use the
    existing 'if-low' guard mode; simulation batches retain the full sweep."""
    if min_gb is None and os.environ.get('DUNECITY_MIN_FREE_GB'):
        min_gb = float(os.environ['DUNECITY_MIN_FREE_GB'])
    module = _installed('scratch')
    if module is not None and PROJECT in module.Scratch().projects:
        try:
            return module.Scratch().guard(PROJECT, min_gb, sweep=sweep)
        except module.LowDisk as e:
            sys.exit('Refusing to start: ' + str(e))
    need = 25.0 if min_gb is None else float(min_gb)
    free = shutil.disk_usage(tempfile.gettempdir()).free / 1e9
    if free < need:
        sys.exit('Refusing to start: only %.1f GB free (threshold %g GB).' % (free, need))
    return {'free_gb': round(free, 1), 'min_free_gb': need}


def discard_profile_assets(profile, telemetry=False):
    """Delete a finished game's regenerated bulk; keep logs, saves, replays and summaries.

    telemetry=True also removes the AI decision log once its rows were extracted."""
    names = DISPOSABLE_PROFILE_ASSETS + (('ai-decisions',) if telemetry else ())
    for name in names:
        path = Path(profile) / name
        if path.is_dir() and not path.is_symlink():
            remove_tree(path)


def prune_runs(parent, prefix, keep):
    """Keep only the `keep` newest run directories named prefix* below parent."""
    runs = sorted((p for p in Path(parent).glob(prefix + '*') if p.is_dir() and not p.is_symlink()),
                  key=lambda p: p.stat().st_mtime)
    for stale in runs[:max(0, len(runs) - keep)]:
        remove_tree(stale)


def clone_tree(source, destination):
    """Copy a directory tree as APFS clones where possible (no extra disk until written)."""
    destination = Path(destination)
    destination.mkdir(parents=True, exist_ok=True)
    if sys.platform == 'darwin':
        done = subprocess.run(['/bin/cp', '-cRp', str(Path(source)) + '/.', str(destination)],
                              capture_output=True)
        if done.returncode == 0:
            return
    shutil.copytree(source, destination, dirs_exist_ok=True)


def needed_revisions(store, profile=None, saves=()):
    """Workshop revisions a game needs from store: those of the installed immutable
    ws-<hash> mods in profile, those a save pins (stored as hex text), and the mod
    revision each of these depends on. Never the whole store."""
    store = Path(store)
    wanted = set()
    mods = Path(profile) / 'mods' if profile else None
    if mods and mods.is_dir():
        wanted.update(p.name[3:] for p in mods.iterdir() if p.name.startswith('ws-'))
    for save in saves:
        wanted.update(h.decode() for h in HASH.findall(Path(save).read_bytes()))
    found, pending = set(), [h for h in wanted if re.fullmatch('[0-9a-f]{64}', h)]
    while pending:
        name = pending.pop()
        manifest = store / name / 'manifest'
        if name in found or not manifest.is_file():
            continue
        found.add(name)
        for line in manifest.read_text(errors='replace').splitlines():
            if line.startswith('mod=') and re.fullmatch('[0-9a-f]{64}', line[4:].strip()):
                pending.append(line[4:].strip())
    return sorted(found)


def copy_revisions(source_profile, profile, saves=()):
    """Clone only needed_revisions from source_profile's store into profile's store."""
    store = Path(source_profile) / 'workshop' / 'revisions'
    if not store.is_dir():
        return []
    names = needed_revisions(store, source_profile, saves)
    for name in names:
        clone_tree(store / name, Path(profile) / 'workshop' / 'revisions' / name)
    return names


def restore_fixture_revisions(profile, saves):
    """Restore the exact archived mod revisions the committed save fixtures name.

    Historical configuration bytes live beside their original manifests. Unchanged
    Dune City graphics come from the source tree; retired artwork is already kept
    by the eight-house performance fixture. Every payload must match its original
    SHA-256 and size. Embedded maps need no separate Workshop-store copy.
    """
    source_root = Path(__file__).resolve().parents[2]
    store = source_root / 'tests/saves/data/workshop/revisions'
    names = needed_revisions(store, saves=saves)
    for name in names:
        archived = store / name
        manifest = (archived / 'manifest').read_bytes()
        if hashlib.sha256(manifest).hexdigest() != name:
            raise RuntimeError('Archived fixture manifest does not match revision ' + name)
        target = Path(profile) / 'workshop/revisions' / name
        for line in manifest.decode().splitlines():
            if not line.startswith('file='):
                continue
            digest, size, encoded = line[5:].split(',', 2)
            relative = bytes.fromhex(encoded).decode()
            source = archived / 'files' / relative
            if source.is_file():
                data = source.read_bytes()
            else:
                current = source_root / 'mods/dunecity' / relative
                data = current.read_bytes() if current.is_file() else b''
                if hashlib.sha256(data).hexdigest() != digest:
                    old_art = source_root / 'tests/performance/data/eight-house-384/files' / (digest + '.png.gz')
                    if old_art.is_file():
                        data = gzip.decompress(old_art.read_bytes())
            if len(data) != int(size) or hashlib.sha256(data).hexdigest() != digest:
                raise RuntimeError('Archived fixture bytes changed: ' + relative + ' for revision ' + name)
            destination = target / 'files' / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
        (target / 'manifest').write_bytes(manifest)
        shutil.copyfile(archived / 'version', target / 'version')
    return names


def active_mod_name(source):
    """The mod the source profile says is active: explicit marker first, then the ini."""
    marker = source / 'mods/active_mod.txt'
    if marker.exists():
        name = marker.read_text().strip()
        if name:
            return name
    config = configparser.ConfigParser(interpolation=None, strict=False)
    config.read(source / 'Dune City.ini')
    for section in ('General', 'general'):
        if config.has_section(section) and config.has_option(section, 'Custom Game Mod'):
            # Values carry trailing "# comment" text in this ini.
            name = config.get(section, 'Custom Game Mod').split('#')[0].strip()
            if name:
                return name
    return 'dunecity'


def prepare_profile(profile, source=None, saves=()):
    """Create an isolated profile, optionally mirroring a source profile's mods.

    saves: the saved games the probe will load, so the revisions they pin are copied."""
    profile.mkdir(parents=True, exist_ok=True)
    (profile / 'Dune City.ini').write_text(MINIMAL_INI)
    if source is None:
        return None
    shutil.copyfile(source / 'Dune City.ini', profile / 'Dune City.ini')
    mods = source / 'mods'
    if mods.is_dir():
        for mod in sorted(path for path in mods.iterdir() if path.is_dir()):
            clone_tree(mod, profile / 'mods' / mod.name)
    # Installed immutable ws-* directories still depend on their revision
    # manifests/files. Across a version bump the shipped mod changes, so the
    # saved revision must remain resolvable from this private cache. Clone only
    # the revisions those mods and the saves need, never the whole store, the
    # publisher's owner token or the publication queue.
    copy_revisions(source, profile, saves)
    active = active_mod_name(source)
    (profile / 'mods').mkdir(parents=True, exist_ok=True)
    (profile / 'mods/active_mod.txt').write_text(active)
    return active
