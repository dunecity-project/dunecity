#!/usr/bin/env python3
"""Exercise selected zone icons and fallbacks against real initialized game objects.

Requires the existing macOS Ninja Release build and bundled game assets. Uses an isolated
profile and dummy SDL drivers. Logs and the test executable are retained in --output-dir.
"""
import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, default=root / 'build')
parser.add_argument('--output-dir', type=Path)
args = parser.parse_args()
build = args.build_dir.resolve()
out = args.output_dir.resolve() if args.output_dir else Path(tempfile.mkdtemp(prefix='dunecity-skin-probe-'))
out.mkdir(parents=True, exist_ok=True)
subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
target = 'bin/dunecity.app/Contents/MacOS/dunecity'
lines = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', target], text=True).splitlines()
main = (root / 'src/main.cpp').read_text()
needle = 'int menuResult = MainMenu().showMenu();'
if main.count(needle) != 1:
    raise RuntimeError('Main menu entry changed; update the test injection point.')
main = main.replace(needle, 'int menuResult = runSkinProbe();')
main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
# SDL's dummy video backend has no accelerated renderer. Match the existing
# headless Classic UI probe without changing the production renderer settings.
renderer_flags = 'Uint32 rendererFlags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE;'
if main.count(renderer_flags) != 1:
    raise RuntimeError('Renderer initialization changed; update the headless probe.')
main = main.replace(renderer_flags,
    'Uint32 rendererFlags = SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE;')
pos = main.index('int main(')
include = root / 'tests/skins/skin-probe.inc'
main = main[:pos] + '#include "' + str(include) + '"\n' + main[pos:]
source = out / 'skin-probe-main.cpp'
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
obj = out / 'skin-probe-main.o'
cc[cc.index('-o') + 1] = str(obj)
cc[cc.index('-c') + 1] = str(source)
map_path = root / 'data/maps/multiplayer/2P - 51x31 - 1v1 - Habbanya-Autumn.ini'
cc.append('-DPROBE_MAP_PATH="' + str(map_path) + '"')
cc.append('-fno-access-control')  # Inspect real sidebar/modal state only in this diagnostic binary.
app = out / 'skin-probe.app/Contents'
(app / 'MacOS').mkdir(parents=True, exist_ok=True)
resources = app / 'Resources'
# Copy only the skin payload: cache tests mutate this isolated bundled source.
if resources.is_symlink():
    resources.unlink()
resources.mkdir(exist_ok=True)
original = build / 'bin/dunecity.app/Contents/Resources'
for child in original.iterdir():
    destination = resources / child.name
    if child.name == 'mods':
        destination.mkdir(exist_ok=True)
        for mod in child.iterdir():
            target_mod = destination / mod.name
            if mod.name == 'dunecity':
                shutil.copytree(mod, target_mod, dirs_exist_ok=True)
            elif not target_mod.exists():
                target_mod.symlink_to(mod)
    elif not destination.exists():
        destination.symlink_to(child)
binary = app / 'MacOS/skin-probe'
link = shlex.split(next(line for line in lines if ' -o ' + target + ' ' in line))
link = link[link.index('&&')+1:]
link = link[:link.index('&&')]
link[link.index('-o')+1] = str(binary)
link = [str(obj) if arg.endswith('/main.cpp.o') else arg for arg in link]
with (out / 'build.log').open('w') as log:
    subprocess.run(cc, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
env = dict(os.environ, DUNECITY_USERDIR=str(out / 'profile'), SDL_VIDEODRIVER='dummy',
           SDL_AUDIODRIVER='dummy', SDL_RENDER_DRIVER='software')
with (out / 'run.log').open('w') as log:
    subprocess.run([str(binary), '--window', '--showlog'], cwd=out, env=env,
                   stdout=log, stderr=subprocess.STDOUT, check=True, timeout=60)
text = (out / 'run.log').read_text()
if 'SKIN_PROBE_PASS:' not in text:
    raise RuntimeError('Missing skin probe result; see ' + str(out / 'run.log'))
print(next(line for line in text.splitlines() if 'SKIN_PROBE_PASS:' in line))
