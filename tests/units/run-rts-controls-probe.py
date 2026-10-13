#!/usr/bin/env python3
"""Modern RTS controls on real units over real game cycles (docs/rts-controls.md).

Requires the existing macOS Ninja Release build and bundled game assets. Uses an isolated
profile and dummy SDL drivers. Logs and the test executable are retained in --output-dir.
"""
import argparse
import csv
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, default=root / 'build')
parser.add_argument('--output-dir', type=Path)
parser.add_argument('--smoke-dir', type=Path, default=Path('/tmp/dunecity-rts-controls-smoke'))
args = parser.parse_args()
build = args.build_dir.resolve()
out = args.output_dir.resolve() if args.output_dir else Path(tempfile.mkdtemp(prefix='dunecity-rts-controls-probe-'))
out.mkdir(parents=True, exist_ok=True)
subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
target = 'bin/dunecity.app/Contents/MacOS/dunecity'
lines = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', target], text=True).splitlines()
main = (root / 'src/main.cpp').read_text()
needle = 'int menuResult = MainMenu().showMenu();'
if main.count(needle) != 1:
    raise RuntimeError('Main menu entry changed; update the test injection point.')
main = main.replace(needle, 'int menuResult = runRtsControlsProbe();')
main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
pos = main.index('int main(')
include = root / 'tests/units/rts-controls-probe.inc'
main = main[:pos] + '#include "' + str(include) + '"\n' + main[pos:]
source = out / 'rts-controls-probe-main.cpp'
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
obj = out / 'rts-controls-probe-main.o'
cc[cc.index('-o') + 1] = str(obj)
cc[cc.index('-c') + 1] = str(source)
map_path = root / 'data/maps/multiplayer/3P - 128x128 - FFA - Tuono-Avon.ini'
cc.append('-DPROBE_MAP_PATH="' + str(map_path) + '"')
cc.append('-DRTS_FIXTURE_PATH="' + str(root / 'tests/units/fixtures/save-9854-orders.dls') + '"')
cc.append('-fno-access-control')  # Inspect production order and UI state only in this diagnostic binary.
app = out / 'rts-controls-probe.app/Contents'
(app / 'MacOS').mkdir(parents=True, exist_ok=True)
resources = app / 'Resources'
if not resources.exists():
    resources.symlink_to(build / 'bin/dunecity.app/Contents/Resources')
binary = app / 'MacOS/rts-controls-probe'
link = shlex.split(next(line for line in lines if ' -o ' + target + ' ' in line))
link = link[link.index('&&')+1:]
link = link[:link.index('&&')]
link[link.index('-o')+1] = str(binary)
link = [str(obj) if arg.endswith('/main.cpp.o') else arg for arg in link]
with (out / 'build.log').open('w') as log:
    subprocess.run(cc, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
smoke = args.smoke_dir.resolve()
smoke.mkdir(parents=True, exist_ok=True)
profile = out / 'profile'
profile.mkdir(exist_ok=True)
(profile / 'Dune City.ini').write_text('[Video]\nPhysical Width = 1280\nPhysical Height = 800\nWidth = 1280\nHeight = 800\n'
                                      'Interface Height = 800\nFullscreen = false\n[General]\nPlay Intro = false\n')
env = dict(os.environ, DUNECITY_USERDIR=str(profile), SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
           RTS_PROBE_OUT=str(out), RTS_PROBE_SMOKE=str(smoke))
logfile = out / 'run.log'
with logfile.open('w') as log:
    subprocess.run([str(binary), '--window', '--showlog'], cwd=out, env=env,
                   stdout=log, stderr=subprocess.STDOUT, check=True, timeout=900)
text = logfile.read_text()
for marker in ('RTS_CONTROLS_LEGACY_PASS:', 'RTS_CONTROLS_ATTACKMOVE_PASS:', 'RTS_CONTROLS_FORMATION_ORDERS_PASS:', 'RTS_CONTROLS_HOLD_PASS:',
               'RTS_CONTROLS_QUEUE_PASS:', 'RTS_CONTROLS_REPEAT_PASS:', 'RTS_CONTROLS_SAVE_PASS:',
               'RTS_CONTROLS_SMOKE_PASS:', 'RTS_CONTROLS_PROBE_PASS:'):
    if marker not in text:
        raise RuntimeError('Missing ' + marker + ' in ' + str(logfile))
for save in out.glob('*.dls'):
    save.unlink()
subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
print('RTS controls probe passed. Logs: ' + str(out) + ' Smoke frames: ' + str(smoke))
