"""Shared isolated-profile construction for the real-engine probes.

A saved game records one exact mod revision. Copying only the well-known mod
names leaves a workshop-installed revision resolvable solely through the user's
workshop cache, which makes a probe depend on external state and fail offline
with "The exact mod version for this game could not be loaded". So every mod
directory present in the source profile is copied, and the active mod is taken
from that profile's own metadata rather than hardcoded.
"""
import configparser
from contextlib import contextmanager
import importlib.util
from pathlib import Path
import re
import shutil

MINIMAL_INI = ('[Video]\nPhysical Width = 640\nPhysical Height = 480\nWidth = 640\nHeight = 480\n'
               'Fullscreen = false\n[General]\nPlay Intro = false\n')


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


@contextmanager
def managed_profile(profile):
    """Use the installed agent lifecycle on managed hosts; CI remains standalone.

    Only disposable copied assets are cleaned. Logs, saves and exact source
    revision evidence remain alongside the profile. Failures retain assets.
    """
    helper = Path.home() / '.local/share/agent-workspace/workspace.py'
    config = Path.home() / '.config/agent-workspace/projects.json'
    if not helper.is_file() or not config.is_file():
        yield
        return
    spec = importlib.util.spec_from_file_location('dunecity_agent_workspace', helper)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
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


def prepare_profile(profile, source=None):
    """Create an isolated profile, optionally mirroring a source profile's mods."""
    profile.mkdir(parents=True, exist_ok=True)
    (profile / 'Dune City.ini').write_text(MINIMAL_INI)
    if source is None:
        return None
    shutil.copyfile(source / 'Dune City.ini', profile / 'Dune City.ini')
    mods = source / 'mods'
    if mods.is_dir():
        for mod in sorted(path for path in mods.iterdir() if path.is_dir()):
            shutil.copytree(mod, profile / 'mods' / mod.name, dirs_exist_ok=True)
    # Installed immutable ws-* directories still depend on their revision
    # manifests/files. Across a version bump the shipped mod changes, so the
    # saved revision must remain resolvable from this private cache. Copy only
    # content revisions, never the publisher's owner token or publication queue.
    revisions = source / 'workshop' / 'revisions'
    if revisions.is_dir():
        shutil.copytree(revisions, profile / 'workshop' / 'revisions', dirs_exist_ok=True)
    active = active_mod_name(source)
    (profile / 'mods').mkdir(parents=True, exist_ok=True)
    (profile / 'mods/active_mod.txt').write_text(active)
    return active
