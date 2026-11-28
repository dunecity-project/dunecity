#!/usr/bin/env python3
"""Tabulate what Troopers do against Ornithopters in a player's own settings, reach off and on.

The Dune City mod's effective game options (the map starts explored, no Fog of War), in the
existing Ninja Release build: a Trooper and the three a Troopers order deploys, in every stance
a player or QuantBot leaves them in (Guard, Area Guard, Ambush, Hunt, walking on a move order,
on an attack-move), against an Ornithopter that attacks them, attacks a Harvester three tiles
away, or flies past two tiles off. A Launcher and a Rocket Turret in the same spot are the
yardsticks. Every Trooper case is played with [Game Options] "Trooper Ornithopter Reach" off
(the rules up to 1.0.876) and on.

Per case, means over the seeds: whether the Ornithopter was ever picked as a target, the
distance at the first pick and the first shot, shots, shots with the gun, hits, hit points
removed (an Ornithopter has 25), kills, shooters lost, the seconds the Ornithopter spent in
weapon range, and how the shooters spent that time: without a target, reloading, turning,
moving, or ready.

The test fails if a Trooper ever fires its gun at the Ornithopter, if the reach lowers the
Troopers' kills or raises their losses over all cases, or if Troopers standing on Guard do
not shoot down more Ornithopters with it.

Outputs: stances.csv, summary.md, run.log.
"""
import argparse
import csv
import os
from pathlib import Path
import shlex
import statistics
import subprocess
import sys
from collections import defaultdict

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/performance'))
from probe_profile import NATIVE_TARGET, native_build_commands, native_probe_binary, native_probe_includes  # noqa: E402
from probe_profile import default_build_dir, discard_profile_assets, require_free_space, scratch_dir  # noqa: E402

NATIVE_INCLUDES = ''.join('#include "' + p.as_posix() + '"\n' for p in native_probe_includes(root))
CYCLE_SECONDS = 0.016


def seed_list(text):
    seeds = [int(part) for part in text.split(',')]
    if not seeds or len(set(seeds)) != len(seeds) or min(seeds) < 0:
        raise argparse.ArgumentTypeError('seeds must be distinct nonnegative integers, e.g. 1,2,3,4')
    return seeds


parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--build-dir', type=Path, default=default_build_dir(root))
parser.add_argument('--output-dir', type=Path, required=True)
parser.add_argument('--seeds', type=seed_list, default=seed_list('1,2,3,4'))
parser.add_argument('--report', action='store_true',
                    help='Only recompute the report from an existing stances.csv in --output-dir')
parser.add_argument('--wall-timeout', type=int, default=1500)
args = parser.parse_args()
build, out = args.build_dir.resolve(), args.output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)

if not args.report:
    require_free_space()
    subprocess.run([sys.executable, str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
    commands = native_build_commands(build)
    main = (root / 'src/main.cpp').read_text(encoding='utf-8')
    needle = 'int menuResult = MainMenu().showMenu();'
    if main.count(needle) != 1:
        raise RuntimeError('Main-menu injection point changed; update the Trooper stance runner.')
    main = main.replace(needle, 'int menuResult = runTrooperStanceProbe();')
    main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
    position = main.index('int main(')
    main = (main[:position] + NATIVE_INCLUDES + '#include "'
            + (root / 'tests/units/trooper-stance-probe.inc').as_posix() + '"\n' + main[position:])
    source, obj = out / 'trooper-stance-probe-main.cpp', out / 'trooper-stance-probe-main.o'
    source.write_text(main, encoding='utf-8')
    compile_command = shlex.split(next(line for line in commands if ' -c ' in line and '/src/main.cpp' in line))
    # A private diagnostic object must not overwrite Ninja's production depfile/PCH records.
    for option in ('-include', '-MT', '-MF'):
        if option in compile_command:
            position = compile_command.index(option)
            del compile_command[position:position + 2]
    for option in ('-MD', '-MMD'):
        if option in compile_command:
            compile_command.remove(option)
    compile_command[compile_command.index('-o') + 1] = str(obj)
    compile_command[compile_command.index('-c') + 1] = str(source)
    compile_command.append('-fno-access-control')
    binary = native_probe_binary(build, out, 'trooper-stance-probe')
    link = shlex.split(next(line for line in commands if ' -o ' + NATIVE_TARGET + ' ' in line))
    link = link[link.index('&&') + 1:]
    link = link[:link.index('&&')]
    link[link.index('-o') + 1] = str(binary)
    link = [str(obj) if argument.endswith('/main.cpp.o') else argument for argument in link]
    with (out / 'build.log').open('w') as log:
        subprocess.run(compile_command, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)

    with scratch_dir('trooper-stance-probe') as scratch:
        profile = scratch / 'profile'
        profile.mkdir()
        (profile / 'Dune City.ini').write_text(
            '[Video]\nPhysical Width = 640\nPhysical Height = 480\nWidth = 640\nHeight = 480\n'
            'Fullscreen = false\nPreferred Renderer = software\n[General]\nPlay Intro = false\nPlayer Name = AA tester\n',
            encoding='utf-8')
        env = dict(os.environ, DUNECITY_USERDIR=str(profile), SDL_VIDEODRIVER='dummy',
                   SDL_AUDIODRIVER='dummy', SDL_RENDER_DRIVER='software', DUNECITY_PATH_WORKERS='1',
                   TROOPER_STANCE_PROBE_OUT=str(out),
                   TROOPER_STANCE_PROBE_SEEDS=','.join(str(seed) for seed in args.seeds))
        logfile = out / 'run.log'
        with logfile.open('w') as log:
            subprocess.run([str(binary), '--window', '--showlog'], cwd=out, env=env,
                           stdout=log, stderr=subprocess.STDOUT, check=True, timeout=args.wall_timeout)
        text = logfile.read_text(errors='replace')
        if text.count('TROOPER_STANCE_PROBE_PASS:') != 1 or 'TROOPER_STANCE_PROBE_FAIL:' in text:
            raise RuntimeError('Missing or failed Trooper stance run; see ' + str(logfile))
        discard_profile_assets(profile)
    subprocess.run([sys.executable, str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)


with (out / 'stances.csv').open(newline='') as handle:
    rows = list(csv.DictReader(handle))
cases = defaultdict(list)
for row in rows:
    cases[(row['shooter'], row['stance'], row['raid'], row['reach'])].append(row)


def mean(group, field, only=lambda row: True):
    values = [float(row[field]) for row in group if only(row)]
    return statistics.mean(values) if values else None


def cell(value, pattern='%.1f'):
    return '-' if value is None else pattern % value


report = ['# Troopers against Ornithopters in the Dune City mod\'s own options', '',
          'Map starts explored, no Fog of War. Seeds: ' + ','.join(sorted({row['seed'] for row in rows}, key=int))
          + '. Means per case. Distances in tiles; an Ornithopter has 25 hit points.',
          'Reach off is the rule up to 1.0.876. The last six columns are the share of the time the',
          'Ornithopter was in weapon range that the shooters spent without a target, with another',
          'target, reloading, turning, moving, or ready to fire.', '',
          '| Shooter | Stance | Ornithopter | Reach | Picked | Pick at | First shot at | Shots | Gun | Hits | HP removed '
          '| Kills | Lost | In range s | No target | Other | Reload | Turn | Move | Ready |',
          '|---|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|']
for (shooter, stance, raid, reach), group in cases.items():
    spent = {name: sum(int(row[name]) for row in group)
             for name in ('no_target', 'other_target', 'reloading', 'turning', 'moving', 'ready')}
    total = sum(spent.values())
    share = [('%.0f%%' % (100 * spent[name] / total)) if total else '-' for name in spent]
    report.append('| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |' % (
        shooter.replace('_', ' '), stance.replace('_', ' '), raid.replace('_', ' '),
        ('on' if reach == '1' else 'off') if shooter.startswith('trooper') else '-',
        cell(mean(group, 'picked'), '%.2f'),
        cell(mean(group, 'first_pick_distance', lambda row: int(row['first_pick_distance']) >= 0) and
             mean(group, 'first_pick_distance', lambda row: int(row['first_pick_distance']) >= 0) / 64),
        cell(mean(group, 'first_shot_distance', lambda row: int(row['first_shot_distance']) >= 0) and
             mean(group, 'first_shot_distance', lambda row: int(row['first_shot_distance']) >= 0) / 64),
        cell(mean(group, 'shots')), cell(mean(group, 'gun_shots')), cell(mean(group, 'damage_events')),
        cell(mean(group, 'damage')), cell(mean(group, 'killed'), '%.2f'), cell(mean(group, 'shooters_lost'), '%.2f'),
        cell(mean(group, 'in_range_ticks') * CYCLE_SECONDS), ' | '.join(share)))

troopers = [row for row in rows if row['shooter'].startswith('trooper')]


def total(field, reach, only=lambda row: True):
    return sum(int(row[field]) for row in troopers if row['reach'] == reach and only(row))


def guard(row):
    return row['stance'] == 'guard'


report += ['', '## Troopers, all cases together', '',
           '| | Reach off | Reach on |', '|---|---:|---:|',
           '| Ornithopters shot down | %d | %d |' % (total('killed', '0'), total('killed', '1')),
           '| Troopers lost | %d | %d |' % (total('shooters_lost', '0'), total('shooters_lost', '1')),
           '| Rockets fired | %d | %d |' % (total('shots', '0'), total('shots', '1')),
           '| Hit points removed | %d | %d |' % (total('damage', '0'), total('damage', '1')),
           '| Gun shots at the Ornithopter | %d | %d |' % (total('gun_shots', '0'), total('gun_shots', '1')),
           '| On Guard: Ornithopters shot down | %d | %d |' % (total('killed', '0', guard), total('killed', '1', guard)),
           '| On Guard: Troopers lost | %d | %d |' % (total('shooters_lost', '0', guard), total('shooters_lost', '1', guard)),
           '']
(out / 'summary.md').write_text('\n'.join(report) + '\n', encoding='utf-8')
print('\n'.join(report))

failures = []
if total('gun_shots', '0') or total('gun_shots', '1'):
    failures.append('a Trooper fired its gun at an Ornithopter')
if total('killed', '1') < total('killed', '0'):
    failures.append('the reach lowered the Ornithopters shot down')
if total('shooters_lost', '1') > total('shooters_lost', '0'):
    failures.append('the reach raised the Trooper losses')
if not total('killed', '1', guard) > total('killed', '0', guard):
    failures.append('Troopers on Guard did not shoot down more Ornithopters with the reach')
if failures:
    sys.exit('TROOPER STANCE PROBE FAILED: ' + '; '.join(failures))
print('Trooper stance table: ' + str(out / 'summary.md'))
