#!/usr/bin/env python3
"""Measure Trooper Anti-Air with real weapon fire and Game::updateGameState.

Uses the existing macOS Ninja Release game objects; it does not configure or build
another CMake tree. The diagnostic main selects a mod and drives actual 3/5/8-man
Trooper groups against a stock 25-HP Ornithopter on a single straight overhead pass,
with the switch off/on. An outbound single-shot case keeps flying past the natural
arming-counter expiry. Each encounter is replayed exactly and unrelated content
must keep identical object/projectile bytes and RNG in both switch states.

The output contains encounters.csv, frames.csv, projectiles.csv, summary.csv and
per-mod logs. Profiles use the shared leased scratch lifecycle. Run only after the
integrator's single combined build, through the managed game/probe cluster queue.
"""
import argparse
import csv
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
from collections import defaultdict

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/performance'))
from probe_profile import default_build_dir, discard_profile_assets, require_free_space, scratch_dir

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, default=default_build_dir(root))
parser.add_argument('--output-dir', type=Path, required=True)
parser.add_argument('--mods', default='dunecity,CivWars,vanilla,Dune2R',
                    help='Comma-separated installed mod ids; the defaults include both DuneCity-content mods and two controls')
parser.add_argument('--seeds', default='11,22,33', help='Comma-separated deterministic game seeds')
parser.add_argument('--wall-timeout', type=int, default=1800, help='Maximum runtime seconds per mod')
args = parser.parse_args()
build, out = args.build_dir.resolve(), args.output_dir.resolve()
mods = [mod for mod in args.mods.split(',') if mod]
seeds = [int(seed) for seed in args.seeds.split(',')]
if not mods or len(set(mods)) != len(mods) or any('/' in mod or '\\' in mod or mod in ('.', '..') for mod in mods):
    parser.error('--mods must contain distinct installed mod ids, not paths')
if not seeds or any(seed < 0 or seed > 0x7fffffff for seed in seeds) or len(set(seeds)) != len(seeds):
    parser.error('--seeds must contain distinct nonnegative signed 32-bit integers')
require_free_space()
out.mkdir(parents=True, exist_ok=True)
subprocess.run([sys.executable, str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)

target = 'bin/dunecity.app/Contents/MacOS/dunecity'
commands = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', target], text=True).splitlines()
main = (root / 'src/main.cpp').read_text(encoding='utf-8')
needle = 'int menuResult = MainMenu().showMenu();'
if main.count(needle) != 1:
    raise RuntimeError('Main-menu injection point changed; update the Trooper Anti-Air consumer runner.')
main = main.replace(needle, 'int menuResult = runTrooperAntiAirProbe();')
main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
position = main.index('int main(')
main = main[:position] + '#include "' + (root / 'tests/units/trooper-aa-probe.inc').as_posix() + '"\n' + main[position:]
source, obj = out / 'trooper-aa-main.cpp', out / 'trooper-aa-main.o'
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
app = out / 'trooper-aa-probe.app/Contents'
(app / 'MacOS').mkdir(parents=True, exist_ok=True)
if not (app / 'Resources').exists():
    (app / 'Resources').symlink_to(build / 'bin/dunecity.app/Contents/Resources')
binary = app / 'MacOS/trooper-aa-probe'
link = shlex.split(next(line for line in commands if ' -o ' + target + ' ' in line))
link = link[link.index('&&') + 1:]
link = link[:link.index('&&')]
link[link.index('-o') + 1] = str(binary)
link = [str(obj) if argument.endswith('/main.cpp.o') else argument for argument in link]
with (out / 'build.log').open('w') as log:
    subprocess.run(compile_command, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)

rows = []
with scratch_dir('trooper-aa-probe') as scratch:
    for mod in mods:
        profile, results = scratch / ('profile-' + mod), out / mod
        profile.mkdir()
        results.mkdir(parents=True, exist_ok=True)
        (profile / 'Dune City.ini').write_text(
            '[Video]\nPhysical Width = 640\nPhysical Height = 480\nWidth = 640\nHeight = 480\n'
            'Fullscreen = false\nPreferred Renderer = software\n[General]\nPlay Intro = false\nPlayer Name = AA tester\n',
            encoding='utf-8')
        env = dict(os.environ, DUNECITY_USERDIR=str(profile), SDL_VIDEODRIVER='dummy',
                   SDL_AUDIODRIVER='dummy', SDL_RENDER_DRIVER='software', DUNECITY_PATH_WORKERS='1',
                   TROOPER_AA_PROBE_MOD=mod, TROOPER_AA_PROBE_OUT=str(results),
                   TROOPER_AA_PROBE_SEEDS=','.join(str(seed) for seed in seeds))
        logfile = results / 'run.log'
        with logfile.open('w') as log:
            subprocess.run([str(binary), '--window', '--showlog'], cwd=out, env=env,
                           stdout=log, stderr=subprocess.STDOUT, check=True, timeout=args.wall_timeout)
        text = logfile.read_text(errors='replace')
        markers = [line for line in text.splitlines() if 'TROOPER_AA_PROBE_PASS:' in line]
        if len(markers) != 1 or 'TROOPER_AA_PROBE_FAIL:' in text:
            raise RuntimeError('Missing or failed Trooper Anti-Air consumer result; see ' + str(logfile))
        with (results / 'encounters.csv').open(newline='') as handle:
            current = list(csv.DictReader(handle))
        # Two directions, four cases (3/5/8 overhead + outbound), two switch states,
        # and two identical replays of each state. These are fixed-seed repeats,
        # not independent battle samples or a win-rate estimate.
        if len(current) != len(seeds) * 2 * 4 * 2 * 2:
            raise RuntimeError('Incomplete Trooper Anti-Air encounter matrix: ' + str(results))
        rows.extend(current)
        discard_profile_assets(profile)
        shutil.copytree(profile, results / 'profile', dirs_exist_ok=True)
        shutil.rmtree(profile)
        print(markers[0])

with (out / 'encounters.csv').open('w', newline='') as handle:
    writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
    writer.writeheader()
    writer.writerows(rows)
groups = defaultdict(list)
for row in rows:
    if row['repeat'] == '0':
        groups[(row['mod'], row['trooper_anti_air'], row['scenario'], row['troopers'])].append(row)
with (out / 'summary.csv').open('w', newline='') as handle:
    writer = csv.writer(handle)
    writer.writerow(['mod', 'trooper_anti_air', 'scenario', 'troopers', 'encounters', 'kills', 'escapes',
                     'mean_health_loss_capped', 'min_health_loss_capped', 'max_health_loss_capped',
                     'shots', 'goal_updates', 'armed_goal_updates', 'armed_heading_changes'])
    for key, group in sorted(groups.items()):
        losses = [int(row['health_loss_capped']) for row in group]
        writer.writerow([*key, len(group), sum(int(row['killed']) for row in group),
                         sum(int(row['exit_crossed']) for row in group), sum(losses) / len(losses),
                         min(losses), max(losses),
                         *[sum(int(row[field]) for row in group) for field in
                           ('shots', 'goal_updates', 'armed_goal_updates', 'armed_heading_changes')]])
subprocess.run([sys.executable, str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
print('Trooper Anti-Air real-engine telemetry: ' + str(out / 'summary.csv'))
