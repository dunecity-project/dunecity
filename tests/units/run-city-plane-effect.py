#!/usr/bin/env python3
"""Measure the city plane rules off against on, in the existing Ninja Release build.

Three match rules, each played off (the game up to 1.0.876) and on in the same binary, in the
Dune City mod's own options (explored map, no fog, Rocket Turrets need power):

- City Plane Defence. An enemy city airplane crosses a site defended by 1, 2, 4 or 6 Rocket
  Turrets, alone on open ground or around a base, on three headings and five lateral offsets.
  Reported per arm: the share of passes in which a turret fires at the plane, rockets fired,
  hit points taken off the plane, planes shot down per pass, what the defender loses to the
  crash, and where the turret-cycles with the plane inside weapon range go (held, no target
  yet, turning, reloading). Bombing is off in both arms.
- City Plane Bombing. A plane crosses an undefended base of 22 buildings on three headings and
  thirteen offsets: bombs dropped, buildings and credits destroyed per run. Three arms: off, on
  with one aimed 100-point bomb per building tile (the first rule), and on with the stick
  (City Plane Carpet Bombing: 60-point bombs, two a tile, no rack).
- Airport Ornithopter Patrol and the fixed cases (checks.csv): what one run does to a Wind
  Trap, a Construction Yard and a Palace, own and allied buildings, the bomb load, the saved
  bomb load, and what an Airport launches in eleven game minutes; for the stick the same
  buildings, a road between two buildings, own buildings beside the target, forty walls in
  one stick, tanks under it and a repairing building; the fall time, the impact tile and a
  bomb's blast centred on five aircraft; pictures of a run (aimed.png, stick.png); and City
  Plane Purchase: price, no limit on planes in the air, the one-minute cooldown, another house's Buy
  command and a house that cannot pay.

Differences are on minus off, paired by encounter, with a 95% bootstrap interval
(tests/ai/match_score.py). The test requires: with the defence rule more passes engaged, more
damage to the plane and more planes shot down (each whole interval above 0), and the defender
losing under 5 hit points and under 0.02 buildings a pass to its own fire (a rocket that meets
the plane over an own building does its 30 there too) and to crashes; with bombing off no bomb and
no damage, with it on buildings destroyed in some runs but never a Construction Yard or a
Palace in a single run, in both bombing arms; and the stick destroying more buildings a run
than the aimed bombs.

Outputs: passes.csv, runs.csv, checks.csv, summary.md, run.log.
"""
import argparse
import csv
import os
from pathlib import Path
import shlex
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/performance'))
sys.path.insert(0, str(root / 'tests/ai'))
from probe_profile import NATIVE_TARGET, native_build_commands, native_probe_binary, native_probe_includes  # noqa: E402
from probe_profile import default_build_dir, discard_profile_assets, require_free_space, scratch_dir  # noqa: E402
from match_score import bootstrap  # noqa: E402

NATIVE_INCLUDES = ''.join('#include "' + p.as_posix() + '"\n' for p in native_probe_includes(root))
PASS_KEY = ('layout', 'turrets', 'direction', 'offset', 'seed')
RUN_KEY = ('direction', 'offset', 'seed')


def seed_list(text):
    seeds = []
    for part in text.split(','):
        if '-' in part:
            first, last = part.split('-')
            seeds.extend(range(int(first), int(last) + 1))
        else:
            seeds.append(int(part))
    if not seeds or len(set(seeds)) != len(seeds) or min(seeds) < 0:
        raise argparse.ArgumentTypeError('seeds must be distinct nonnegative integers, e.g. 1-4 or 3,5,8')
    return seeds


def pair_up(rows, key, arm, first='0', second='1', every=('0', '1')):
    arms = {}
    for row in rows:
        arms.setdefault(tuple(row[field] for field in key), {})[row[arm]] = row
    pairs = []
    for name, both in sorted(arms.items()):
        if set(both) != set(every):
            raise RuntimeError('Encounter without every arm: ' + repr(name))
        pairs.append((name, both[first], both[second]))
    return pairs


def paired(pairs, value):
    """Arm means, mean paired difference (on - off) and its 95% bootstrap interval."""
    off = [value(pair[1]) for pair in pairs]
    on = [value(pair[2]) for pair in pairs]
    low, high = bootstrap([b - a for a, b in zip(off, on)], 20261123)[-2:]
    count = len(pairs)
    return sum(off) / count, sum(on) / count, (sum(on) - sum(off)) / count, low, high


def line(label, result, digits=2):
    form = '%.' + str(digits) + 'f'
    return ('| %s | ' + form + ' | ' + form + ' | %+' + '.%df' % digits + ' | %+' + '.%df' % digits + ' to %+'
            + '.%df' % digits + ' |') % ((label,) + result)


def share(rows, field):
    total = sum(int(row['in_range']) for row in rows)
    return 100.0 * sum(int(row[field]) for row in rows) / total if total else 0.0


parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--build-dir', type=Path, default=default_build_dir(root))
parser.add_argument('--output-dir', type=Path, default=default_build_dir(root) / 'city-plane-effect')
parser.add_argument('--seeds', type=seed_list, default=seed_list('1-4'),
                    help='Seeds for the turret passes (120 pass kinds per seed), e.g. 1-4')
parser.add_argument('--run-seeds', type=seed_list, default=seed_list('1-3'),
                    help='Seeds for the bombing runs (39 run kinds per seed)')
parser.add_argument('--report', action='store_true',
                    help='Only recompute the report from the CSVs in --output-dir')
parser.add_argument('--wall-timeout', type=int, default=3000)
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
        raise RuntimeError('Main-menu injection point changed; update the city plane effect runner.')
    main = main.replace(needle, 'int menuResult = runCityPlaneEffect();')
    main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
    position = main.index('int main(')
    main = (main[:position] + NATIVE_INCLUDES + '#include "'
            + (root / 'tests/units/city-plane-effect.inc').as_posix() + '"\n' + main[position:])
    source, obj = out / 'city-plane-effect-main.cpp', out / 'city-plane-effect-main.o'
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
    binary = native_probe_binary(build, out, 'city-plane-effect')
    link = shlex.split(next(line for line in commands if ' -o ' + NATIVE_TARGET + ' ' in line))
    link = link[link.index('&&') + 1:]
    link = link[:link.index('&&')]
    link[link.index('-o') + 1] = str(binary)
    link = [str(obj) if argument.endswith('/main.cpp.o') else argument for argument in link]
    with (out / 'build.log').open('w') as log:
        subprocess.run(compile_command, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)

    with scratch_dir('city-plane-effect') as scratch:
        profile = scratch / 'profile'
        profile.mkdir()
        (profile / 'Dune City.ini').write_text(
            '[Video]\nPhysical Width = 640\nPhysical Height = 480\nWidth = 640\nHeight = 480\n'
            'Fullscreen = false\nPreferred Renderer = software\n[General]\nPlay Intro = false\nPlayer Name = AA tester\n',
            encoding='utf-8')
        env = dict(os.environ, DUNECITY_USERDIR=str(profile), SDL_VIDEODRIVER='dummy',
                   SDL_AUDIODRIVER='dummy', SDL_RENDER_DRIVER='software', DUNECITY_PATH_WORKERS='1',
                   CITY_PLANE_EFFECT_OUT=str(out),
                   CITY_PLANE_EFFECT_SEEDS=','.join(str(seed) for seed in args.seeds),
                   CITY_PLANE_EFFECT_RUN_SEEDS=','.join(str(seed) for seed in args.run_seeds))
        logfile = out / 'run.log'
        with logfile.open('w') as log:
            subprocess.run([str(binary), '--window', '--showlog'], cwd=out, env=env,
                           stdout=log, stderr=subprocess.STDOUT, check=True, timeout=args.wall_timeout)
        text = logfile.read_text(errors='replace')
        if text.count('CITY_PLANE_EFFECT_PASS:') != 1 or 'CITY_PLANE_EFFECT_FAIL:' in text:
            raise RuntimeError('Missing or failed city plane effect run; see ' + str(logfile))
        discard_profile_assets(profile)
    (out / 'city-plane-effect.dls').unlink(missing_ok=True)
    subprocess.run([sys.executable, str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)

with (out / 'passes.csv').open(newline='') as handle:
    passes = pair_up(list(csv.DictReader(handle)), PASS_KEY, 'defence')
with (out / 'runs.csv').open(newline='') as handle:
    run_rows = list(csv.DictReader(handle))
runs = pair_up(run_rows, RUN_KEY, 'bombing', '0', '1', ('0', '1', '2'))
stick_runs = pair_up(run_rows, RUN_KEY, 'bombing', '1', '2', ('0', '1', '2'))
with (out / 'checks.csv').open(newline='') as handle:
    checks = list(csv.reader(handle))[1:]

report = ['# City plane rules: off against on', '',
          'Off is the game up to 1.0.876. Dune City mod options: explored map, no fog. One cycle is 16 ms.', '']
failures = []


def defence_block(chosen, label):
    engaged = paired(chosen, lambda row: 100.0 * (int(row['shots']) > 0))
    shots = paired(chosen, lambda row: float(row['shots']))
    damage = paired(chosen, lambda row: float(row['plane_damage']))
    killed = paired(chosen, lambda row: float(row['killed']))
    own = paired(chosen, lambda row: float(row['own_damage']))
    lost = paired(chosen, lambda row: float(row['own_destroyed']))
    off, on = [pair[1] for pair in chosen], [pair[2] for pair in chosen]
    crashes = [int(row['crash_distance']) / 64.0 for row in on if int(row['killed'])]
    lines = ['### ' + label + ' (' + str(len(chosen)) + ' paired passes)', '',
             '| Measure | Defence off | Defence on | Paired difference (on - off) | 95% interval |',
             '|---|---:|---:|---:|---|',
             line('Passes with a rocket fired at the plane, %', engaged, 1),
             line('Rockets fired per pass', shots),
             line('Hit points taken off the plane per pass (of 100)', damage, 1),
             line('Planes shot down per pass', killed),
             line('Defender hit points lost to own rockets and crashes per pass', own, 1),
             line('Defender buildings lost to own rockets and crashes per pass', lost, 3), '',
             'Turret-cycles with the plane inside weapon range, off -> on: held %.0f%% -> %.0f%%, no target yet '
             '%.0f%% -> %.0f%%, turning %.0f%% -> %.0f%%, reloading %.0f%% -> %.0f%%.'
             % (share(off, 'hold'), share(on, 'hold'), share(off, 'no_target'), share(on, 'no_target'),
                share(off, 'turning'), share(on, 'turning'), share(off, 'reloading'), share(on, 'reloading')),
             'Planes shot down with the rule on came down a mean %.1f tiles from the middle of the site (nearest %.1f).'
             % (sum(crashes) / len(crashes), min(crashes)) if crashes else 'No plane was shot down with the rule on.', '']
    return lines, {'engaged': engaged, 'damage': damage, 'killed': killed, 'own': own, 'lost': lost}


lines, overall = defence_block(passes, 'City Plane Defence, all passes')
report += ['## Rocket Turrets against a crossing plane', ''] + lines
for layout, title in (('base', 'Turrets around a base'), ('open', 'Turrets alone on open ground')):
    lines, _ = defence_block([pair for pair in passes if pair[0][0] == layout], title)
    report += lines
for turrets in ('1', '2', '4', '6'):
    lines, _ = defence_block([pair for pair in passes if pair[0][1] == turrets],
                             turrets + ' Rocket Turret' + ('' if turrets == '1' else 's'))
    report += lines
for name, text in (('engaged', 'passes engaged'), ('damage', 'damage to the plane'), ('killed', 'planes shot down')):
    if not overall[name][3] > 0:
        failures.append('defence: %s did not improve, interval %+.3f to %+.3f' % ((text,) + overall[name][3:5]))
if overall['own'][1] >= 5 or overall['lost'][1] >= 0.02:
    failures.append('defence: the defender lost %.1f hit points and %.3f buildings a pass to its own fire and crashes'
                    % (overall['own'][1], overall['lost'][1]))

bombs = paired(runs, lambda row: float(row['bombs']))
damage = paired(runs, lambda row: float(row['damage']))
destroyed = paired(runs, lambda row: float(row['destroyed']))
credits = paired(runs, lambda row: float(row['credits']))
on_runs = [pair[2] for pair in runs]
with_loss = sum(1 for row in on_runs if int(row['destroyed']) > 0)
with_bombs = sum(1 for row in on_runs if int(row['bombs']) > 0)
report += ['## Bombing runs over an undefended base of 22 buildings', '',
           '### City Plane Bombing, one aimed bomb per building tile (' + str(len(runs)) + ' paired runs)', '',
           '| Measure | Bombing off | Bombing on | Paired difference (on - off) | 95% interval |',
           '|---|---:|---:|---:|---|',
           line('Bombs dropped per run (load 6)', bombs),
           line('Building hit points removed per run', damage, 1),
           line('Buildings destroyed per run', destroyed),
           line('Credits of buildings destroyed per run', credits, 1), '',
           'With bombing on %d of %d runs dropped a bomb and %d destroyed at least one building; the most in one run '
           'was %d buildings (%d credits). Construction Yards lost in a single run: %d; Palaces: %d.'
           % (with_bombs, len(on_runs), with_loss, max(int(row['destroyed']) for row in on_runs),
              max(int(row['credits']) for row in on_runs), sum(int(row['yard_lost']) for row in on_runs),
              sum(int(row['palace_lost']) for row in on_runs)), '']
lost = {}
for row in on_runs:
    for name in row['lost'].split('+'):
        if name != '-':
            lost[name] = lost.get(name, 0) + 1
report += ['Buildings destroyed over all runs with bombing on: '
           + (', '.join('%s %d' % item for item in sorted(lost.items(), key=lambda item: -item[1])) or 'none') + '.', '']
if not destroyed[3] > 0:
    failures.append('bombing: no buildings destroyed, interval %+.3f to %+.3f' % destroyed[3:5])
if any(int(pair[1]['bombs']) or int(pair[1]['damage']) for pair in runs):
    failures.append('bombing: a plane bombed with the rule off')
if any(int(row['yard_lost']) or int(row['palace_lost']) for row in on_runs):
    failures.append('bombing: a Construction Yard or Palace fell to a single run')
if with_loss == len(on_runs):
    failures.append('bombing: every run destroyed a building')

stick_bombs = paired(stick_runs, lambda row: float(row['bombs']))
stick_damage = paired(stick_runs, lambda row: float(row['damage']))
stick_destroyed = paired(stick_runs, lambda row: float(row['destroyed']))
stick_credits = paired(stick_runs, lambda row: float(row['credits']))
stick_on = [pair[2] for pair in stick_runs]
stick_loss = sum(1 for row in stick_on if int(row['destroyed']) > 0)
report += ['### City Plane Carpet Bombing, the stick against the aimed bombs (' + str(len(stick_runs)) + ' paired runs)', '',
           '| Measure | Aimed bombs | Stick | Paired difference (stick - aimed) | 95% interval |',
           '|---|---:|---:|---:|---|',
           line('Bombs dropped per run (a rack of six against no rack)', stick_bombs),
           line('Building hit points removed per run', stick_damage, 1),
           line('Buildings destroyed per run', stick_destroyed),
           line('Credits of buildings destroyed per run', stick_credits, 1), '',
           'With the stick %d of %d runs dropped a bomb and %d destroyed at least one building; the most in one run '
           'was %d buildings (%d credits) of 22. Construction Yards lost in a single run: %d; Palaces: %d.'
           % (sum(1 for row in stick_on if int(row['bombs']) > 0), len(stick_on), stick_loss,
              max(int(row['destroyed']) for row in stick_on), max(int(row['credits']) for row in stick_on),
              sum(int(row['yard_lost']) for row in stick_on), sum(int(row['palace_lost']) for row in stick_on)), '']
lost = {}
for row in stick_on:
    for name in row['lost'].split('+'):
        if name != '-':
            lost[name] = lost.get(name, 0) + 1
report += ['Buildings destroyed over all runs with the stick: '
           + (', '.join('%s %d' % item for item in sorted(lost.items(), key=lambda item: -item[1])) or 'none') + '.', '']
if not stick_destroyed[3] > 0:
    failures.append('stick: not more buildings destroyed than the aimed bombs, interval %+.3f to %+.3f' % stick_destroyed[3:5])
if any(int(row['yard_lost']) or int(row['palace_lost']) for row in stick_on):
    failures.append('stick: a Construction Yard or Palace fell to a single run')
if max(int(row['destroyed']) for row in stick_on) >= 11:
    failures.append('stick: one run destroyed half the base')

report += ['## Fixed cases', ''] + ['- ' + name + ': ' + detail for name, detail in checks] + ['']
(out / 'summary.md').write_text('\n'.join(report) + '\n', encoding='utf-8')
print('\n'.join(report))

if failures:
    sys.exit('CITY PLANE EFFECT FAILED: ' + '; '.join(failures))
print('City plane effect: ' + str(out / 'summary.md'))
