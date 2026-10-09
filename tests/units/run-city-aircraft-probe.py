#!/usr/bin/env python3
"""Drive the Micropolis city aircraft against the real engine.

Builds tests/units/city-aircraft-probe.inc against the existing macOS Ninja
Release objects and runs it with an isolated profile and dummy SDL drivers, so
it never touches the normal game configuration, saves or logs. Logs, the CSV of
measured results and the test executable stay in --output-dir.
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
parser.add_argument('--mods', default='dunecity',
                    help='comma separated mods to run the probe under')
args = parser.parse_args()
build = args.build_dir.resolve()
out = args.output_dir.resolve() if args.output_dir else Path(tempfile.mkdtemp(prefix='dunecity-city-aircraft-probe-'))
out.mkdir(parents=True, exist_ok=True)

subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
target = 'bin/dunecity.app/Contents/MacOS/dunecity'
lines = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', target], text=True).splitlines()

main = (root / 'src/main.cpp').read_text()
needle = 'int menuResult = MainMenu().showMenu();'
if main.count(needle) != 1:
    raise RuntimeError('Main menu entry changed; update the test injection point.')
main = main.replace(needle, 'int menuResult = runCityAircraftProbe();')
main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
pos = main.index('int main(')
include = root / 'tests/units/city-aircraft-probe.inc'
main = main[:pos] + '#include "' + str(include) + '"\n' + main[pos:]
source = out / 'city-aircraft-probe-main.cpp'
source.write_text(main)

cc = shlex.split(next(line for line in lines if ' -c ' in line and '/src/main.cpp' in line))
# Test compilation must not overwrite Ninja's production dependency records.
for option in ('-include', '-MT', '-MF'):
    if option in cc:
        i = cc.index(option)
        del cc[i:i + 2]
for option in ('-MD', '-MMD'):
    if option in cc:
        cc.remove(option)
obj = out / 'city-aircraft-probe-main.o'
cc[cc.index('-o') + 1] = str(obj)
cc[cc.index('-c') + 1] = str(source)
map_path = root / 'data/maps/multiplayer/2P - 51x31 - 1v1 - Habbanya-Autumn.ini'
cc.append('-DPROBE_MAP_PATH="' + str(map_path) + '"')
# The fixture writes the production traffic-density layer and reads private unit
# state. Only this diagnostic binary gets that access; nothing ships with it.
cc.append('-fno-access-control')

app = out / 'city-aircraft-probe.app/Contents'
(app / 'MacOS').mkdir(parents=True, exist_ok=True)
resources = app / 'Resources'
if not resources.exists():
    resources.symlink_to(build / 'bin/dunecity.app/Contents/Resources')
binary = app / 'MacOS/city-aircraft-probe'
link = shlex.split(next(line for line in lines if ' -o ' + target + ' ' in line))
link = link[link.index('&&') + 1:]
link = link[:link.index('&&')]
link[link.index('-o') + 1] = str(binary)
link = [str(obj) if arg.endswith('/main.cpp.o') else arg for arg in link]

with (out / 'build.log').open('w') as log:
    subprocess.run(cc, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)

for mod in [m.strip() for m in args.mods.split(',') if m.strip()]:
    env = dict(os.environ, DUNECITY_USERDIR=str(out / ('profile-' + mod)),
               SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               CITY_AIRCRAFT_PROBE_MOD=mod, CITY_AIRCRAFT_PROBE_OUT=str(out))
    logfile = out / ('run-' + mod + '.log')
    with logfile.open('w') as log:
        subprocess.run([str(binary), '--window', '--showlog'], cwd=out, env=env,
                       stdout=log, stderr=subprocess.STDOUT, check=True, timeout=1800)
    if 'CITY_AIRCRAFT_PROBE_PASS:' not in logfile.read_text():
        raise RuntimeError('Missing city-aircraft result: ' + str(logfile))

subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
print('City aircraft launch, patrol, flight, air defence and save continuation passed. Logs: ' + str(out))
