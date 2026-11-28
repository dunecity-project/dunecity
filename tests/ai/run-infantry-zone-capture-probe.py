#!/usr/bin/env python3
"""Bounded real-engine infantry capture and threatened-zone rush activation probes.

Reuses the configured production build's objects/assets, replacing only main.cpp. Capture and
rush are independently selectable; no treatment injects Capture orders or calls the new chooser.
"""
import argparse
from collections import Counter
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/performance'))
sys.path.insert(0, str(root / 'tests/menu'))
from probe_profile import (NATIVE_TARGET, discard_profile_assets, native_build_commands,
                           native_probe_binary, native_probe_includes, require_free_space, scratch_dir)
from probe_process import processes, run

CAPTURE = {
    'quantbot_player_r', 'quantbot_player_c', 'quantbot_player_i', 'quantbot_command',
    'ai_idle', 'ai_attack_move', 'human_hunt_below', 'human_hunt_exact', 'human_hunt_above',
    'ai_hunt_below', 'ai_hunt_exact', 'ai_hunt_above', 'permission_off_attack',
    'soldier_hunt_exact', 'ai_wounded_attack',
    'permission_off_human_hunt', 'permission_off_ai_hunt', 'full_enemy', 'unreachable',
    'human_attack', 'human_hunt_forced_attack', 'nonzone_human', 'nonzone_ai',
    'mixed_human_player', 'mixed_human_command', 'mixed_bot_player', 'mixed_bot_command',
    'squad_children', 'capture_threshold',
}
RUSH = {
    'rush_distance_3', 'rush_distance_6', 'rush_distance_9', 'rush_distance_10',
    'rush_distance_15', 'rush_distance_25', 'rush_existing_soldier',
    'rush_soldier_replacement', 'rush_fairness', 'rush_active_attack', 'rush_base_defence',
    'rush_air', 'rush_damage', 'rush_threshold',
    'rush_attack_move_protected',
}
EXCLUSIONS = ('excluded_health', 'excluded_permission', 'excluded_full',
              'excluded_unreachable', 'excluded_human', 'excluded_nonzone')


def fields(line):
    return {key: int(value) if re.fullmatch(r'-?\d+', value) else value
            for key, value in re.findall(r'(\w+)=([^\s]+)', line)}


def inspect_log(text, returncode, group, out):
    rows, cases, totals, onsets = [], [], [], []
    for line in text.splitlines():
        if 'INFANTRY_ZONE_' not in line:
            continue
        line = line[line.index('INFANTRY_ZONE_'):]
        print(line, flush=True)
        record = fields(line)
        if line.startswith('INFANTRY_ZONE_CAPTURE_RESULT:'):
            rows.append(record)
        elif line.startswith('INFANTRY_ZONE_RUSH_RESULT:'):
            rows.append(record)
        elif line.startswith('INFANTRY_ZONE_RUSH_ONSET:'):
            onsets.append(record)
        elif line.startswith('INFANTRY_ZONE_CASE:'):
            cases.append(record)
        elif line.startswith('INFANTRY_ZONE_GROUP:'):
            totals.append(record)
    (out / 'observations.json').write_text(json.dumps(
        {'group': group, 'returncode': returncode, 'cases': cases, 'observations': rows,
         'onsets': onsets, 'totals': totals}, indent=2) + '\n', encoding='utf-8')
    if returncode != 0 or 'INFANTRY_ZONE_FAIL:' in text or text.count('INFANTRY_ZONE_PASS:') != 1:
        raise RuntimeError('Native infantry zone assertions failed: ' + str(out / 'run.log'))
    expected = (CAPTURE if group == 'capture' else RUSH if group == 'rush' else CAPTURE | RUSH)
    if Counter(case['name'] for case in cases) != Counter(expected) or any(case['pass'] != 1 for case in cases):
        raise RuntimeError('Missing, duplicate or unsuccessful case markers')
    selected = {'capture', 'rush'} if group == 'all' else {group}
    if Counter(row['group'] for row in totals) != Counter(selected):
        raise RuntimeError('Missing or duplicate independent group summary')
    capture = [row for row in rows if row['name'] in CAPTURE]
    rush = [row for row in rows if row['name'] in RUSH]
    if 'capture' in selected:
        expected_rows = Counter(CAPTURE - {'capture_threshold'})
        expected_rows['squad_children'] = 3
        if Counter(row['name'] for row in capture) != expected_rows:
            raise RuntimeError('Missing capture/exclusion observation, including all three delivered children')
        eligible = sum(row['eligible'] for row in capture)
        captured = sum(row['captures'] for row in capture if row['eligible'])
        damage = sum(row['damage_raw'] for row in capture if row['eligible'])
        if eligible <= 0 or captured * 100 < eligible * 90 or damage != 0:
            raise RuntimeError('No activation, <90% eligible capture rate, or capturable-zone damage')
        for row in capture:
            if row['engagements'] != 1 or row['cycles'] <= 0:
                raise RuntimeError('No actual native engagement/cycle advancement: ' + row['name'])
            if row['eligible']:
                if row['captures'] != 1 or row['captured_unit'] != row['unit'] or row['damage_raw'] != 0:
                    raise RuntimeError('Eligible engagement lacks the original physically captured unit: ' + row['name'])
                if sum(row[key] for key in EXCLUSIONS) != 0 or row['permission'] != 1:
                    raise RuntimeError('Eligible engagement has an exclusion or lacks permission')
            elif row['captures'] != 0 or row['damage_raw'] <= 0 or sum(row[key] for key in EXCLUSIONS) != 1:
                raise RuntimeError('Excluded engagement did not actually shoot/damage without capture: ' + row['name'])
        print(f'INFANTRY_ZONE_CAPTURE_NUMBERS: eligible={eligible} captures={captured} '
              f'damage_raw={damage} excluded={len(capture) - eligible}', flush=True)
    if 'rush' in selected:
        expected_rows = Counter(RUSH - {'rush_threshold'})
        expected_rows['rush_fairness'] = 3
        if Counter(row['name'] for row in rush) != expected_rows:
            raise RuntimeError('Missing rush observation, including every fairness zone')
        if len(onsets) != len(rush) or any(row['before_order'] != 1 or row['available'] <= 0 for row in onsets):
            raise RuntimeError('Rush availability was not observed before orders, or there was no activation opportunity')
        for row in rush:
            if (row['available'] <= 0 or not row['onset'] <= row['order_cycle']
                    or (not row['order_within1'] and not (
                        row['far_deviation'] and (row['zone_lost'] or row['unit_dead'])))):
                raise RuntimeError('A legal nearest-free rush order was not issued within1s: ' + row['name'])
            if row['ordered'] != 4294967295 and row['ordered'] != row['nearest']:
                raise RuntimeError('Rush order did not choose the nearest free Trooper: ' + row['name'])
            if row['entry_observed'] and (row['first'] != row['nearest']
                    or row['elapsed_cycles'] < 0 or not row['order_cycle'] <= row['entry_cycle']):
                raise RuntimeError('Rush entry/progress identity mismatch: ' + row['name'])
            if not row['entry_observed'] and not row['far_deviation']:
                raise RuntimeError('A nearby rush did not physically enter: ' + row['name'])
        near = [row for row in rush if row['near']]
        fast = sum(row['within20'] for row in near)
        if not near or fast * 100 < len(near) * 80:
            raise RuntimeError('No nearby activation or <80% nearby entries within20 simulated seconds')
        for name, exclusion in (('rush_active_attack', 'excluded_attack'), ('rush_base_defence', 'excluded_defence')):
            row = next(row for row in rush if row['name'] == name)
            if row[exclusion] <= 0 or row['protected_samples'] <= 0:
                raise RuntimeError('Protected assignment never actually activated: ' + name)
        print(f'INFANTRY_ZONE_RUSH_NUMBERS: eligible={len(rush)} entries={sum(row["entry_observed"] for row in rush)} '
              f'orders_within1={sum(row["order_within1"] for row in rush)} near={len(near)} near_within20={fast} '
              f'far_deviations={sum(row["far_deviation"] for row in rush)}', flush=True)


processes.install_signal_handlers()
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, default=root / 'build')
parser.add_argument('--output-dir', type=Path, required=True)
parser.add_argument('--case-group', choices=('capture', 'rush', 'all'), default='capture')
parser.add_argument('--wall-timeout', type=int, default=240)
args = parser.parse_args()
build, out = args.build_dir.resolve(), args.output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)
require_free_space()
run([sys.executable, str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
commands = native_build_commands(build)
main = (root / 'src/main.cpp').read_text()
needle = 'int menuResult = MainMenu().showMenu();'
if main.count(needle) != 1:
    raise RuntimeError('Main menu entry changed; update the assertion injection point')
main = main.replace(needle, 'int menuResult = runInfantryZoneCaptureProbe();')
main = main.replace('if(shouldPlayIntro && (bFirstInit==true))',
                    'if(false && shouldPlayIntro && (bFirstInit==true))')
position = main.index('int main(')
includes = ''.join('#include "' + path.as_posix() + '"\n' for path in native_probe_includes(root))
includes += '#include "' + str(root / 'tests/ai/infantry-zone-capture-probe.inc') + '"\n'
source, obj = out / 'infantry-zone-capture-main.cpp', out / 'infantry-zone-capture-main.o'
source.write_text(main[:position] + includes + main[position:], encoding='utf-8')
cc = shlex.split(next(line for line in commands if ' -c ' in line and '/src/main.cpp' in line))
for option in ('-include', '-MT', '-MF'):
    if option in cc:
        index = cc.index(option)
        del cc[index:index + 2]
for option in ('-MD', '-MMD'):
    if option in cc:
        cc.remove(option)
cc[cc.index('-o') + 1] = str(obj)
cc[cc.index('-c') + 1] = str(source)
cc.append('-fno-access-control')
binary = native_probe_binary(build, out, 'infantry-zone-capture-probe')
link = shlex.split(next(line for line in commands if ' -o ' + NATIVE_TARGET + ' ' in line))
link = link[link.index('&&') + 1:]
link = link[:link.index('&&')]
link[link.index('-o') + 1] = str(binary)
main_objects = [i for i, argument in enumerate(link) if argument.endswith('/main.cpp.o')]
if len(main_objects) != 1:
    raise RuntimeError('Production link does not contain exactly one main object')
link[main_objects[0]] = str(obj)
with (out / 'build.log').open('w') as log:
    run(cc, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)

with scratch_dir('infantry-zone-capture-probe') as scratch:
    profile = scratch / 'profile'
    profile.mkdir()
    (profile / 'Dune City.ini').write_text(
        '[Video]\nPhysical Width = 640\nPhysical Height = 480\nWidth = 640\nHeight = 480\n'
        'Fullscreen = false\nPreferred Renderer = software\n[General]\nPlay Intro = false\n'
        'Player Name = Capture observer\n', encoding='utf-8')
    env = dict(os.environ, DUNECITY_USERDIR=str(profile), SDL_VIDEODRIVER='dummy',
               SDL_AUDIODRIVER='dummy', SDL_RENDER_DRIVER='software', DUNECITY_PATH_WORKERS='1',
               INFANTRY_ZONE_CASE_GROUP=args.case_group)
    env.pop('DUNECITY_TEST_GARRISON_EVACUATE_PERCENT', None)
    try:
        with (out / 'run.log').open('w') as log:
            done = run([str(binary), '--window', '--showlog', '--non-interactive'], cwd=out, env=env,
                       stdout=log, stderr=subprocess.STDOUT, timeout=args.wall_timeout)
        inspect_log((out / 'run.log').read_text(errors='replace'), done.returncode, args.case_group, out)
    finally:
        discard_profile_assets(profile)
print('Infantry zone activation passed. Evidence: ' + str(out), flush=True)
