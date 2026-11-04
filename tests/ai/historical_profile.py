"""Baseline-only compatibility copy of the shared probe scratch lifecycle."""

import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading
from contextlib import contextmanager

PROJECT = 'dunecity'
DISPOSABLE_PROFILE_ASSETS = ('mods', 'workshop/revisions')
FAILED_GAMES_KEEP = 5
_failed_lock = threading.Lock()

def probe_binary(out):
    """The balance-probe binary run-campaign-balance.py prepared for --output-dir out, for
    --prebuilt-probe reuse. Windows runs it from the game's output directory (data is found
    beside the executable), so the run directory records its path instead of holding a copy."""
    out = Path(out)
    if sys.platform == 'win32':
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
