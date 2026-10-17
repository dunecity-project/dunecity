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
from contextlib import contextmanager
import importlib.util
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import threading

MINIMAL_INI = ('[Video]\nPhysical Width = 640\nPhysical Height = 480\nWidth = 640\nHeight = 480\n'
               'Fullscreen = false\n[General]\nPlay Intro = false\n')
PROJECT = 'dunecity'
# Regenerated or copied bulk of a finished game's profile; never evidence.
DISPOSABLE_PROFILE_ASSETS = ('mods', 'workshop/revisions')
# A batch keeps at most this many failed games (logs, saves, replays; no bulk) in scratch/failed.
FAILED_GAMES_KEEP = 5
_failed_lock = threading.Lock()
HASH = re.compile(rb'[0-9a-f]{64}')


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
            shutil.rmtree(path, ignore_errors=True)


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
    shutil.rmtree(out, ignore_errors=True)


def require_free_space(min_gb=None):
    """Refuse to start a batch on a nearly full disk, after sweeping stale agent scratch.

    The threshold is the project's min_free_gb on managed hosts (25 GB), overridable
    with DUNECITY_MIN_FREE_GB or the argument."""
    if min_gb is None and os.environ.get('DUNECITY_MIN_FREE_GB'):
        min_gb = float(os.environ['DUNECITY_MIN_FREE_GB'])
    module = _installed('scratch')
    if module is not None and PROJECT in module.Scratch().projects:
        try:
            return module.Scratch().guard(PROJECT, min_gb)
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
            shutil.rmtree(path, ignore_errors=True)


def prune_runs(parent, prefix, keep):
    """Keep only the `keep` newest run directories named prefix* below parent."""
    runs = sorted((p for p in Path(parent).glob(prefix + '*') if p.is_dir() and not p.is_symlink()),
                  key=lambda p: p.stat().st_mtime)
    for stale in runs[:max(0, len(runs) - keep)]:
        shutil.rmtree(stale, ignore_errors=True)


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
