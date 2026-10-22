#!/usr/bin/env python3
"""Smooth unit motion at 2 ms game speed (tests/units/unit-motion-probe.inc).

Ground units shuttle across open sand while the interactive main loop runs at Stefan's 2 ms game
speed and 1440x900 with its real frame pacing (several cycles per rendered frame). Fails if a
moving unit covers more than its step speed in one cycle, stands still mid-step, or is drawn
further from its previous frame than the cycles run in between allow (never a whole tile unless
its speed covers one). Requires the macOS Ninja Release build and the bundled assets; isolated
profile, dummy SDL drivers. Logs, the probe binary and the per-cycle/per-frame traces
(unit-motion-cycles.csv, unit-motion-frames.csv) stay in --output-dir.
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
out = args.output_dir.resolve() if args.output_dir else Path(tempfile.mkdtemp(prefix='dunecity-unit-motion-probe-'))
out.mkdir(parents=True, exist_ok=True)
subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
target = 'bin/dunecity.app/Contents/MacOS/dunecity'
lines = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', target], text=True).splitlines()
main = (root / 'src/main.cpp').read_text()
needle = 'int menuResult = MainMenu().showMenu();'
if main.count(needle) != 1:
    raise RuntimeError('Main menu entry changed; update the test injection point.')
main = main.replace(needle, 'int menuResult = runUnitMotionProbe();')
main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
pos = main.index('int main(')
main = main[:pos] + '#include "' + str(root / 'tests/units/unit-motion-probe.inc') + '"\n' + main[pos:]
source = out / 'unit-motion-probe-main.cpp'
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
obj = out / 'unit-motion-probe-main.o'
cc[cc.index('-o') + 1] = str(obj)
cc[cc.index('-c') + 1] = str(source)
map_path = root / 'data/maps/multiplayer/2P - 51x31 - 1v1 - Habbanya-Autumn.ini'
cc.append('-DPROBE_MAP_PATH="' + str(map_path) + '"')
cc.append('-fno-access-control')  # Read production movement state only in this diagnostic binary.
cc.append('-g')                   # Symbolised crash reports for the probe's own code.
app = out / 'unit-motion-probe.app/Contents'
(app / 'MacOS').mkdir(parents=True, exist_ok=True)
resources = app / 'Resources'
if not resources.exists():
    resources.symlink_to(build / 'bin/dunecity.app/Contents/Resources')
binary = app / 'MacOS/unit-motion-probe'
link = shlex.split(next(line for line in lines if ' -o ' + target + ' ' in line))
link = link[link.index('&&')+1:]
link = link[:link.index('&&')]
link[link.index('-o')+1] = str(binary)
link = [str(obj) if arg.endswith('/main.cpp.o') else arg for arg in link]
with (out / 'build.log').open('w') as log:
    subprocess.run(cc, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
profile = out / 'profile'
profile.mkdir(exist_ok=True)
# Stefan's window: 1440x900, no zoom, no vsync (his FrameLimit = false).
(profile / 'Dune City.ini').write_text('[Video]\nPhysical Width = 1440\nPhysical Height = 900\nWidth = 1440\n'
                                       'Height = 900\nInterface Height = 900\nFullscreen = false\nFrameLimit = false\n'
                                       'Preferred Zoom Level = 0\n[General]\nPlay Intro = false\n')
env = dict(os.environ, DUNECITY_USERDIR=str(profile), SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
           UNIT_MOTION_PROBE_OUT=str(out))
logfile = out / 'run.log'
with logfile.open('w') as log:
    done = subprocess.run([str(binary), '--window', '--showlog'], cwd=out, env=env,
                          stdout=log, stderr=subprocess.STDOUT, timeout=300)
text = logfile.read_text(errors='replace')
summary = next((line for line in text.splitlines() if 'UNIT_MOTION_PROBE:' in line), None)
if summary:
    print(summary.split('UNIT_MOTION_PROBE:', 1)[1].strip())
failure = next((line for line in text.splitlines() if 'UNIT_MOTION_PROBE_FIRST_FAILURE:' in line), None)
if failure:
    print(failure.strip())
if done.returncode != 0 or 'UNIT_MOTION_PROBE_PASS' not in text:
    raise SystemExit('Unit motion probe failed (exit %d); see %s' % (done.returncode, logfile))
subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
print('Unit motion probe passed. Logs and traces: ' + str(out))
