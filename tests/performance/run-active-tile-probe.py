#!/usr/bin/env python3
"""Check the Stage 1a active-corpse registry, A* generation stamp and field split.

Drives the production Map, Tile and AStarSearch objects in a headless game, so
the registry and the scratch stamp are exercised as the engine uses them rather
than through a test model. Requires the existing macOS Ninja build.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, default=root / 'build')
parser.add_argument('--output-dir', type=Path)
args = parser.parse_args()
build = args.build_dir.resolve()
out = args.output_dir.resolve() if args.output_dir else Path(tempfile.mkdtemp(prefix='dunecity-active-tile-probe-'))
out.mkdir(parents=True, exist_ok=True)
subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
target = 'bin/dunecity.app/Contents/MacOS/dunecity'
lines = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', target], text=True).splitlines()
main = (root / 'src/main.cpp').read_text()
needle = 'int menuResult = MainMenu().showMenu();'
if main.count(needle) != 1:
    raise RuntimeError('Main menu entry changed; update the test injection point.')
main = main.replace(needle, 'int menuResult = runActiveTileProbe();')
main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
position = main.index('int main(')
main = main[:position] + '#include "' + str(root / 'tests/performance/active-tile-probe.inc') + '"\n' + main[position:]
source = out / 'active-tile-probe-main.cpp'
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
obj = out / 'active-tile-probe-main.o'
cc[cc.index('-o') + 1] = str(obj)
cc[cc.index('-c') + 1] = str(source)
cc.append('-DPROBE_MAP_PATH="' + str(root / 'data/maps/multiplayer/2P - 51x31 - 1v1 - Habbanya-Autumn.ini') + '"')
cc.append('-fno-access-control')  # Reach the A* scratch pool to force a generation wrap.
app = out / 'active-tile-probe.app/Contents'
(app / 'MacOS').mkdir(parents=True, exist_ok=True)
resources = app / 'Resources'
if not resources.exists():
    resources.symlink_to(build / 'bin/dunecity.app/Contents/Resources')
binary = app / 'MacOS/active-tile-probe'
link = shlex.split(next(line for line in lines if ' -o ' + target + ' ' in line))
link = link[link.index('&&')+1:]
link = link[:link.index('&&')]
link[link.index('-o')+1] = str(binary)
link = [str(obj) if arg.endswith('/main.cpp.o') else arg for arg in link]
with (out / 'build.log').open('w') as log:
    subprocess.run(cc, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
env = dict(os.environ, DUNECITY_USERDIR=str(out / 'profile'), SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
logfile = out / 'run.log'
with logfile.open('w') as log:
    subprocess.run([str(binary), '--window', '--showlog'], cwd=out, env=env,
                   stdout=log, stderr=subprocess.STDOUT, check=True, timeout=600)
results = [line for line in logfile.read_text().splitlines() if 'ACTIVE_TILE_PROBE_' in line]
if not any('ACTIVE_TILE_PROBE_PASS:' in line for line in results):
    raise RuntimeError('Missing active tile result: ' + str(logfile))
print('\n'.join(results))
