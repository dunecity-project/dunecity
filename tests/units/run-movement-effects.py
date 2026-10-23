#!/usr/bin/env python3
"""Movement measurements to compare two builds (tests/units/movement-effects.inc).

Phases (default all): speed (cycles per 12-tile route per ground unit type, straight and diagonal,
sand and rock, 16 legs each), crush (infantry crushed per game minute by tracked vehicles shuttling
through enemy infantry; nobody can die of weapon fire) and carryall (how a unit drives on after a
400-cycle carryall wait that ends without a lift). Writes movement-*.csv to --output-dir and
prints a summary; --compare DIR prints the same summary for an earlier run beside it. Not a test:
it never fails on a difference. Requires the macOS Ninja Release build; isolated profile, dummy SDL.
"""
import argparse
import csv
import os
import shlex
import statistics
import subprocess
import tempfile
from collections import defaultdict
from pathlib import Path

root = Path(__file__).resolve().parents[2]
PHASES = ('speed', 'crush', 'carryall')
NAMES = {28: 'Devastator', 29: 'Deviator', 31: 'Harvester', 32: 'Soldier', 33: 'Launcher', 34: 'MCV', 36: 'Quad',
         37: 'Saboteur', 39: 'SiegeTank', 40: 'SonicTank', 41: 'Tank', 42: 'Trike', 43: 'RaiderTrike', 44: 'Trooper'}


def build_probe(build, out):
    subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
    target = 'bin/dunecity.app/Contents/MacOS/dunecity'
    lines = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', target], text=True).splitlines()
    main = (root / 'src/main.cpp').read_text()
    needle = 'int menuResult = MainMenu().showMenu();'
    if main.count(needle) != 1:
        raise RuntimeError('Main menu entry changed; update the injection point.')
    main = main.replace(needle, 'int menuResult = runMovementEffects();')
    main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
    pos = main.index('int main(')
    main = main[:pos] + '#include "' + str(root / 'tests/units/movement-effects.inc') + '"\n' + main[pos:]
    source = out / 'movement-effects-main.cpp'
    source.write_text(main)
    cc = shlex.split(next(line for line in lines if ' -c ' in line and '/src/main.cpp' in line))
    for option in ('-include', '-MT', '-MF'):
        if option in cc:
            i = cc.index(option)
            del cc[i:i + 2]
    for option in ('-MD', '-MMD'):
        if option in cc:
            cc.remove(option)
    obj = out / 'movement-effects-main.o'
    cc[cc.index('-o') + 1] = str(obj)
    cc[cc.index('-c') + 1] = str(source)
    cc.append('-DPROBE_MAP_PATH="' + str(root / 'data/maps/multiplayer/2P - 51x31 - 1v1 - Habbanya-Autumn.ini') + '"')
    cc.append('-fno-access-control')
    app = out / 'movement-effects.app/Contents'
    (app / 'MacOS').mkdir(parents=True, exist_ok=True)
    if not (app / 'Resources').exists():
        (app / 'Resources').symlink_to(build / 'bin/dunecity.app/Contents/Resources')
    binary = app / 'MacOS/movement-effects'
    link = shlex.split(next(line for line in lines if ' -o ' + target + ' ' in line))
    link = link[link.index('&&') + 1:]
    link = link[:link.index('&&')]
    link[link.index('-o') + 1] = str(binary)
    link = [str(obj) if arg.endswith('/main.cpp.o') else arg for arg in link]
    with (out / 'build.log').open('w') as log:
        subprocess.run(cc, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    return binary


def run(binary, out, phase):
    profile = out / 'profile'
    profile.mkdir(exist_ok=True)
    (profile / 'Dune City.ini').write_text('[General]\nPlay Intro = false\n')
    env = dict(os.environ, DUNECITY_USERDIR=str(profile), SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               MOVEMENT_EFFECTS_OUT=str(out), MOVEMENT_EFFECTS_PHASE=phase)
    logfile = out / f'run-{phase}.log'
    with logfile.open('w') as log:
        subprocess.run([str(binary), '--showlog'], cwd=out, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=3600)
    if f'MOVEMENT_EFFECTS_DONE: phase={phase}' not in logfile.read_text(errors='replace'):
        raise SystemExit(f'Phase {phase} failed; see {logfile}')


def rows(path):
    return list(csv.DictReader(path.open())) if path.exists() else []


def summary(out):
    lines = []
    speed = defaultdict(list)
    for r in rows(out / 'movement-speed.csv'):
        speed[(r['terrain'], r['diagonal'], int(r['item']))].append(int(r['cycles']))
    if speed:
        lines.append('speed: mean cycles per 12-tile route (min-max) over 16 legs')
        for key in sorted(speed):
            v = speed[key]
            lines.append(f'  {key[0]} {"diagonal" if key[1] == "1" else "straight"} {NAMES.get(key[2], key[2])}: '
                         f'{statistics.mean(v):.1f} ({min(v)}-{max(v)})')
    crush = defaultdict(int)
    for r in rows(out / 'movement-crush.csv'):
        crush[(r['terrain'], r['lane'], int(r['crusher_item']))] += int(r['kills'])
        crush[(r['terrain'], 'all', 0)] += int(r['kills'])
    if crush:
        lines.append('crush: infantry crushed in 10 game minutes')
        for key in sorted(crush):
            lines.append(f'  {key[0]} {key[1]} {NAMES.get(key[2], "all crushers")}: {crush[key]} ({crush[key] / 10:.1f}/min)')
    carry = defaultdict(list)
    for r in rows(out / 'movement-carryall.csv'):
        carry[(r['terrain'], int(r['item']))].append(r)
    if carry:
        lines.append('carryall: the 300 cycles after a cancelled 400-cycle wait')
        for key in sorted(carry):
            v = carry[key]
            fast = sum(1 for r in v if float(r['moved']) > 2 * max(float(r['step_speed']), 0.01) + 0.05)
            lines.append(f'  {key[0]} {NAMES.get(key[1], key[1])}: tiles after 60 cycles '
                         f'{next((r["tiles"] for r in v if r["after"] == "60"), "?")}, after 300 {v[-1]["tiles"]}; '
                         f'max world units per cycle {max(float(r["moved"]) for r in v):.2f}; '
                         f'cycles faster than 2x the step speed {fast}')
    return lines


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, default=root / 'build')
parser.add_argument('--output-dir', type=Path)
parser.add_argument('--phases', nargs='+', choices=PHASES, default=list(PHASES))
parser.add_argument('--prebuilt-probe', type=Path, help='Probe binary of an earlier run (e.g. another build)')
parser.add_argument('--compare', type=Path, help='Also print the summary of this earlier output directory')
args = parser.parse_args()
out = args.output_dir.resolve() if args.output_dir else Path(tempfile.mkdtemp(prefix='dunecity-movement-effects-'))
out.mkdir(parents=True, exist_ok=True)
binary = args.prebuilt_probe or build_probe(args.build_dir.resolve(), out)
for phase in args.phases:
    run(binary, out, phase)
if args.compare:
    print(f'== {args.compare}')
    print('\n'.join(summary(args.compare.resolve())))
print(f'== {out}')
print('\n'.join(summary(out)))
