#!/usr/bin/env python3
"""Measure complete ground routes using the production game loop.

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
parser.add_argument('--record-baseline', action='store_true')
parser.add_argument('--projectile-trace', action='store_true', help='Trace missiles for comparison to original Dynasty')
parser.add_argument('--projectile-continuation', action='store_true', help='Check in-flight save and observer restoration')
parser.add_argument('--projectile-combat', action='store_true', help='Run full anti-air attack passes')
parser.add_argument('--weapon-reloads', action='store_true', help='Measure actual turret/launcher shot timestamps')
parser.add_argument('--rocket-reload-cycles', type=int,
                    help='Diagnostic combat-only turret reload override (shipped data is unchanged)')
parser.add_argument('--deviation-reward', action='store_true', help='Measure the Deviator contribution ledger')
parser.add_argument('--projectiles', action='store_true', help='Audit projectile mechanics instead of ground routes')
parser.add_argument('--continuation', action='store_true', help='Check exact mid-route save/observer continuation')
args = parser.parse_args()
if args.rocket_reload_cycles is not None and (not args.projectile_combat or not 1 <= args.rocket_reload_cycles <= 10000):
    parser.error('--rocket-reload-cycles requires --projectile-combat and a value in 1..10000')
build = args.build_dir.resolve()
out = args.output_dir.resolve() if args.output_dir else Path(tempfile.mkdtemp(prefix='dunecity-unit-speed-probe-'))
out.mkdir(parents=True, exist_ok=True)
subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
target = 'bin/dunecity.app/Contents/MacOS/dunecity'
lines = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', target], text=True).splitlines()
main = (root / 'src/main.cpp').read_text()
needle = 'int menuResult = MainMenu().showMenu();'
if main.count(needle) != 1:
    raise RuntimeError('Main menu entry changed; update the test injection point.')
main = main.replace(needle, 'int menuResult = runUnitSpeedProbe();')
main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
pos = main.index('int main(')
include = root / ('tests/units/deviation-reward.inc' if args.deviation_reward else 'tests/units/weapon-reload-probe.inc' if args.weapon_reloads else 'tests/units/projectile-trace.inc' if args.projectile_trace else 'tests/units/projectile-continuation.inc' if args.projectile_continuation else 'tests/units/projectile-combat.inc' if args.projectile_combat else 'tests/units/projectile-audit.inc' if args.projectiles else 'tests/units/unit-route-continuation.inc' if args.continuation else 'tests/units/unit-route-probe.inc')
main = main[:pos] + '#include "' + str(include) + '"\n' + main[pos:]
source = out / 'unit-speed-probe-main.cpp'
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
obj = out / 'unit-speed-probe-main.o'
cc[cc.index('-o') + 1] = str(obj)
cc[cc.index('-c') + 1] = str(source)
map_path = root / ('data/maps/multiplayer/3P - 128x128 - FFA - Tuono-Avon.ini' if args.deviation_reward
                   else 'data/maps/multiplayer/2P - 51x31 - 1v1 - Habbanya-Autumn.ini')
cc.append('-DPROBE_MAP_PATH="' + str(map_path) + '"')
cc.append('-fno-access-control')  # Inspect production movement state only in this diagnostic binary.
app = out / 'unit-speed-probe.app/Contents'
(app / 'MacOS').mkdir(parents=True, exist_ok=True)
resources = app / 'Resources'
if not resources.exists():
    resources.symlink_to(build / 'bin/dunecity.app/Contents/Resources')
binary = app / 'MacOS/unit-speed-probe'
link = shlex.split(next(line for line in lines if ' -o ' + target + ' ' in line))
link = link[link.index('&&')+1:]
link = link[:link.index('&&')]
link[link.index('-o')+1] = str(binary)
link = [str(obj) if arg.endswith('/main.cpp.o') else arg for arg in link]
with (out / 'build.log').open('w') as log:
    subprocess.run(cc, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
modes = ('vanilla', 'dunecity', 'Dune2R', 'Tornie') if args.weapon_reloads else ('vanilla', 'dunecity', 'Dune2R')
for mod in modes:
    env = dict(os.environ, DUNECITY_USERDIR=str(out / ('profile-' + mod)),
               SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               UNIT_SPEED_PROBE_MOD=mod, UNIT_SPEED_PROBE_OUT=str(out),
               UNIT_SPEED_PROBE_BASELINE='1' if args.record_baseline else '0')
    env.pop('AA_RELOAD_CYCLES', None)
    if args.rocket_reload_cycles is not None:
        env['AA_RELOAD_CYCLES'] = str(args.rocket_reload_cycles)
    logfile = out / ('run-' + mod + '.log')
    with logfile.open('w') as log:
        subprocess.run([str(binary), '--window', '--showlog'], cwd=out, env=env,
                       stdout=log, stderr=subprocess.STDOUT, check=True, timeout=600)
    if 'UNIT_SPEED_PROBE_PASS:' not in logfile.read_text():
        raise RuntimeError('Missing unit speed result: ' + str(logfile))
if len({(out / (mod + '.csv')).read_bytes() for mod in ('vanilla', 'dunecity', 'Dune2R')}) != 1:
    raise RuntimeError('The three modes produced different unit trajectories')
if args.deviation_reward:
    # Amounts follow each mod's prices; the accounting decisions must not.
    for mod in ('vanilla', 'dunecity', 'Dune2R'):
        rows = (out / (mod + '-deviation.csv')).read_text().splitlines()
        if len(rows) < 2:
            raise RuntimeError('No deviation amounts recorded for ' + mod)
if args.projectile_combat:
    deviator = [out / (mod + '-deviator.csv') for mod in ('vanilla', 'dunecity', 'Dune2R')]
    if len({path.read_bytes() for path in deviator}) != 1:
        raise RuntimeError('The three modes produced different gas deviation results')
subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
print('Unit scenarios passed for modes ' + ', '.join(modes) + '. Logs: ' + str(out))
