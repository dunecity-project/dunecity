#!/usr/bin/env python3
"""Exercise selected zone icons and fallbacks against real initialized game objects.

Requires the existing macOS Ninja Release build and bundled game assets. Uses an isolated
profile and dummy SDL drivers. Logs remain in --output-dir; sealed compilation is shared in the build.
The software-driver environment exercises the real startup renderer selection;
the probe does not replace renderer initialization in its private source.
"""
import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import sys

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/menu'))
from probe_process import processes, run, check_output
from probe_fixture import prepare_fixture
sys.path.insert(0, str(root / 'tests/performance'))
from probe_profile import scratch_dir, require_free_space, discard_profile_assets
processes.install_signal_handlers()
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, default=root / 'build')
parser.add_argument('--output-dir', type=Path)
args = parser.parse_args()
build = args.build_dir.resolve()
out = args.output_dir.resolve() if args.output_dir else Path(tempfile.mkdtemp(prefix='dunecity-skin-probe-'))
out.mkdir(parents=True, exist_ok=True)
require_free_space(sweep='if-low')
run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
target = 'bin/dunecity.app/Contents/MacOS/dunecity'
lines = check_output(['ninja', '-C', str(build), '-t', 'commands', target], text=True).splitlines()
production_link = shlex.split(next(line for line in lines if ' -o ' + target + ' ' in line))
production_link = production_link[production_link.index('&&') + 1:]
production_link = production_link[:production_link.index('&&')]

def compile_fixture(work):
    main = (root / 'src/main.cpp').read_text()
    needle = 'int menuResult = MainMenu().showMenu();'
    if main.count(needle) != 1:
        raise RuntimeError('Main menu entry changed; update the test injection point.')
    main = main.replace(needle, 'int menuResult = runSkinProbe();')
    main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
    pos = main.index('int main(')
    include = root / 'tests/skins/skin-probe.inc'
    main = main[:pos] + '#include "' + str(include) + '"\n' + main[pos:]
    source = work / 'skin-probe-main.cpp'
    source.write_text(main)
    cc = shlex.split(next(line for line in lines if ' -c ' in line and '/src/main.cpp' in line))
    # Test compilation must not overwrite Ninja's production dependency records.
    for option in ('-include', '-MT', '-MF'):
        if option in cc:
            i = cc.index(option)
            del cc[i:i+2]
    for option in ('-MD', '-MMD'):
        if option in cc:
            cc.remove(option)
    obj = work / 'skin-probe-main.o'
    cc[cc.index('-o') + 1] = str(obj)
    cc[cc.index('-c') + 1] = str(source)
    map_path = root / 'data/maps/multiplayer/2P - 51x31 - 1v1 - Habbanya-Autumn.ini'
    cc.append('-DPROBE_MAP_PATH="' + str(map_path) + '"')
    cc.append('-fno-access-control')  # Inspect real sidebar/modal state only in this diagnostic binary.
    app = work / 'skin-probe.app/Contents'
    (app / 'MacOS').mkdir(parents=True)
    (app / 'Resources').symlink_to(build / 'bin/dunecity.app/Contents/Resources')
    binary = app / 'MacOS/skin-probe'
    link = shlex.split(next(line for line in lines if ' -o ' + target + ' ' in line))
    link = link[link.index('&&')+1:]
    link = link[:link.index('&&')]
    link[link.index('-o')+1] = str(binary)
    link = [str(obj) if arg.endswith('/main.cpp.o') else arg for arg in link]
    with (work / 'build.log').open('w') as log:
        run(cc, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
        run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)

fixture_binary = prepare_fixture(root, build, out, build / 'skin-probe-fixture',
                                 'skin-probe.app/Contents/MacOS/skin-probe', lines, production_link, compile_fixture)
print('SKIN_RUNTIME_PHASE: begin', flush=True)
with scratch_dir('skin-probe') as scratch:
    app = scratch / 'skin-probe.app/Contents'
    (app / 'MacOS').mkdir(parents=True)
    binary = app / 'MacOS/skin-probe'
    shutil.copy2(fixture_binary, binary)
    resources = app / 'Resources'
    resources.mkdir()
    # Cache-invalidating mutations belong to this run, never the sealed fixture.
    original = build / 'bin/dunecity.app/Contents/Resources'
    for child in original.iterdir():
        destination = resources / child.name
        if child.name == 'mods':
            destination.mkdir()
            for mod in child.iterdir():
                target_mod = destination / mod.name
                if mod.name == 'dunecity':
                    shutil.copytree(mod, target_mod)
                else:
                    target_mod.symlink_to(mod)
        else:
            destination.symlink_to(child)
    profile = scratch / 'profile'
    env = dict(os.environ, DUNECITY_USERDIR=str(profile), SDL_VIDEODRIVER='dummy',
               SDL_AUDIODRIVER='dummy', SDL_RENDER_DRIVER='software')
    with (out / 'run.log').open('w') as log:
        run([str(binary), '--window', '--showlog'], phase='SKIN_RUNTIME_PHASE', cwd=scratch, env=env,
            stdout=log, stderr=subprocess.STDOUT, check=True, timeout=60)
    text = (out / 'run.log').read_text()
    if 'SKIN_PROBE_PASS:' not in text:
        raise RuntimeError('Missing skin probe result; see ' + str(out / 'run.log'))
    discard_profile_assets(profile)
    shutil.copytree(profile, out / 'profile', dirs_exist_ok=True)
    print(next(line for line in text.splitlines() if 'SKIN_PROBE_PASS:' in line))
